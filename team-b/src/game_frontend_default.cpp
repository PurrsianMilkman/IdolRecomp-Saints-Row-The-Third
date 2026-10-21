// DEFAULT front end for sr3.exe (FIRST PLAYABLE): a plain text menu drawn through FrontendCanvas, arrow keys +
// Enter. It exists so sr3.exe is playable before Team C's real menus land; Team C replaces THIS FILE by
// implementing the same functions (include/sr3game/frontend.h) and swapping the CMake target
// `sr3game_frontend_default` for their own library in the `sr3` executable.
//
// Spec basis (spec-frontend-menu-runtime.md): the main menu launches main_menu_new_game (Sec2) or
// main_menu_continue (Sec3: most recent valid slot; "MAIN_MENU_CONTINUE_UNAVAILABLE" notice if none); the pause
// menu is a screen pushed over a frozen game (Sec7). EVERYTHING ELSE here is CHOSEN: the entry list/wording
// ("Load Game", "Quit", "Save Game", "Quit to Main Menu", "Quit to Desktop"), the colours, the layout.
#include "sr3game/frontend.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace sr3game {

namespace {

constexpr Rgba kTitle{255, 200, 60, 255};
constexpr Rgba kNormal{220, 220, 220, 255};
constexpr Rgba kSelected{255, 255, 255, 255};
constexpr Rgba kDim{140, 140, 150, 255};
constexpr Rgba kWarn{255, 120, 90, 255};

void DrawCentred(FrontendCanvas& c, const std::string& text, double y, Rgba col) {
    c.DrawText(text, (c.Width() - c.TextWidth(text)) * 0.5, y, col);
}

std::string FormatSave(const SaveFileInfo& s) {
    char buf[160];
    if (!s.valid) {
        std::snprintf(buf, sizeof(buf), "%s  (unreadable)", s.displayName.c_str());
        return buf;
    }
    const uint32_t m = s.playTimeSeconds / 60;
    std::time_t t = static_cast<std::time_t>(s.lastWriteUnix);
    char when[32] = "";
    std::tm tmv{};
    if (localtime_s(&tmv, &t) == 0) std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tmv);
    std::snprintf(buf, sizeof(buf), "%s   %s   play %uh%02um   $%lld   respect lvl %u", s.displayName.c_str(), when,
                  m / 60, m % 60, static_cast<long long>(s.cashRaw / 100), s.respectLevel);
    return buf;
}

// Moves a selection with up/down, wrapping.
void Navigate(int& sel, int count, const FrontendInput& in) {
    if (count <= 0) return;
    if (in.down) sel = (sel + 1) % count;
    if (in.up) sel = (sel + count - 1) % count;
}

void DrawMenuList(FrontendCanvas& c, const std::vector<std::string>& items, int sel, double y0, double step) {
    for (size_t i = 0; i < items.size(); ++i) {
        const double y = y0 + step * static_cast<double>(i);
        const bool s = static_cast<int>(i) == sel;
        if (s) {
            const double w = c.TextWidth(items[i]);
            const double x = (c.Width() - w) * 0.5;
            c.FillRect(x - 28, y - 4, x + w + 28, y + step - 8, Rgba{70, 90, 140, 200});
        }
        DrawCentred(c, items[i], y, s ? kSelected : kNormal);
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------------------------------------------
MainMenuResult RunMainMenu(FrontendContext& ctx) {
    enum class Screen { Top, Load };
    Screen screen = Screen::Top;
    int sel = 0, loadSel = 0;
    std::string notice;
    std::vector<SaveFileInfo> saves = ctx.listSaves ? ctx.listSaves() : std::vector<SaveFileInfo>{};
    if (ctx.log) ctx.log("frontend : main menu opened (default text menu; " + std::to_string(saves.size()) + " save file(s) in " + ctx.saveDir + ")");

    const std::vector<std::string> top = {"NEW GAME", "CONTINUE", "LOAD GAME", "QUIT"};
    while (true) {
        if (!ctx.beginFrame || !ctx.beginFrame(Rgba{16, 18, 30, 255})) return {MainMenuResult::Kind::Quit, {}};
        const FrontendInput in = ctx.pollInput ? ctx.pollInput() : FrontendInput{};
        FrontendCanvas& c = *ctx.canvas;

        MainMenuResult chosen;
        bool done = false;
        if (screen == Screen::Top) {
            Navigate(sel, static_cast<int>(top.size()), in);
            if (in.accept) {
                notice.clear();
                switch (sel) {
                    case 0:
                        chosen.kind = MainMenuResult::Kind::NewGame;  // main_menu_new_game (spec Sec2)
                        done = true;
                        break;
                    case 1: {  // main_menu_continue (spec Sec3): most recent valid slot
                        saves = ctx.listSaves ? ctx.listSaves() : saves;
                        auto it = std::find_if(saves.begin(), saves.end(), [](const SaveFileInfo& s) { return s.valid; });
                        if (it == saves.end()) {
                            notice = "No saved game to continue.";  // stands in for MAIN_MENU_CONTINUE_UNAVAILABLE
                        } else {
                            chosen.kind = MainMenuResult::Kind::LoadSave;
                            chosen.savePath = it->path;
                            done = true;
                        }
                        break;
                    }
                    case 2:
                        saves = ctx.listSaves ? ctx.listSaves() : saves;
                        loadSel = 0;
                        screen = Screen::Load;
                        break;
                    default:
                        chosen.kind = MainMenuResult::Kind::Quit;
                        done = true;
                        break;
                }
            }
        } else {
            Navigate(loadSel, static_cast<int>(saves.size()), in);
            if (in.back) screen = Screen::Top;
            if (in.accept && !saves.empty()) {
                if (saves[static_cast<size_t>(loadSel)].valid) {
                    chosen.kind = MainMenuResult::Kind::LoadSave;
                    chosen.savePath = saves[static_cast<size_t>(loadSel)].path;
                    done = true;
                } else {
                    notice = "That save cannot be read.";
                }
            }
        }

        // ---- draw ----
        const double w = c.Width(), h = c.Height();
        c.FillRect(0, 0, w, h * 0.5, Rgba{22, 26, 48, 255});
        c.FillRect(0, h * 0.2, w, h * 0.2 + 4, Rgba{255, 200, 60, 255});
        DrawCentred(c, "SAINTS ROW: THE THIRD", h * 0.12, kTitle);
        DrawCentred(c, "recompilation - first playable", h * 0.12 + 36, kDim);
        if (screen == Screen::Top) {
            DrawMenuList(c, top, sel, h * 0.34, 44);
            DrawCentred(c, "Up/Down: choose    Enter: select", h - 60, kDim);
        } else {
            DrawCentred(c, "LOAD GAME", h * 0.28, kTitle);
            if (saves.empty()) {
                DrawCentred(c, "(no save files in " + ctx.saveDir + ")", h * 0.4, kDim);
            } else {
                std::vector<std::string> lines;
                const size_t first = static_cast<size_t>(std::max(0, loadSel - 5));
                for (size_t i = first; i < saves.size() && i < first + 10; ++i) lines.push_back(FormatSave(saves[i]));
                DrawMenuList(c, lines, loadSel - static_cast<int>(first), h * 0.36, 34);
            }
            DrawCentred(c, "Up/Down: choose    Enter: load    Esc: back", h - 60, kDim);
        }
        if (!notice.empty()) DrawCentred(c, notice, h * 0.8, kWarn);
        ctx.endFrame();
        ++ctx.frameIndex;
        if (done) {
            if (ctx.log) {
                ctx.log(std::string("frontend : main menu result = ") +
                        (chosen.kind == MainMenuResult::Kind::NewGame    ? "NewGame"
                         : chosen.kind == MainMenuResult::Kind::LoadSave ? "LoadSave " + chosen.savePath
                                                                          : "Quit"));
            }
            return chosen;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Pause menu
// ---------------------------------------------------------------------------------------------------------------
struct PauseMenu {
    int sel = 0;
    std::string status;
    FrontendContext* ctx = nullptr;
};

namespace {
const std::vector<std::string>& PauseItems() {
    static const std::vector<std::string> items = {"RESUME", "SAVE GAME", "QUIT TO MAIN MENU", "QUIT TO DESKTOP"};
    return items;
}
}  // namespace

PauseMenu* PauseMenuCreate(FrontendContext& ctx) {
    PauseMenu* m = new PauseMenu();
    m->ctx = &ctx;
    return m;
}
void PauseMenuDestroy(PauseMenu* menu) { delete menu; }

void PauseMenuOpened(PauseMenu& menu) {
    menu.sel = 0;
    menu.status.clear();
}

PauseMenuAction PauseMenuUpdate(PauseMenu& menu, const FrontendInput& in) {
    Navigate(menu.sel, static_cast<int>(PauseItems().size()), in);
    if (in.back) return PauseMenuAction::Resume;
    if (!in.accept) return PauseMenuAction::None;
    switch (menu.sel) {
        case 0: return PauseMenuAction::Resume;
        case 1: return PauseMenuAction::Save;
        case 2: return PauseMenuAction::QuitToMainMenu;
        default: return PauseMenuAction::QuitToDesktop;
    }
}

void PauseMenuDraw(PauseMenu& menu, FrontendCanvas& c) {
    const double h = c.Height();
    DrawCentred(c, "PAUSED", h * 0.25, kTitle);
    DrawMenuList(c, PauseItems(), menu.sel, h * 0.36, 44);
    if (!menu.status.empty()) DrawCentred(c, menu.status, h * 0.36 + 44 * 4 + 20, kWarn);
    DrawCentred(c, "Up/Down: choose   Enter: select   Esc: resume", h - 60, kDim);
}

void PauseMenuSetStatus(PauseMenu& menu, const std::string& text) { menu.status = text; }

}  // namespace sr3game
