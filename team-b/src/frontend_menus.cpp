// Team C front end for sr3.exe: implements include/sr3game/frontend.h (RunMainMenu and the pause
// menu) over sr3frontend::MainMenuModel / PauseMenuModel, drawn through FrontendCanvas, with the
// host's keyboard input merged with the gamepad (sr3frontend::GamepadReader). Linked into `sr3`
// instead of Team B's stand-in `sr3game_frontend_default`. Structure from
// spec-frontend-menu-runtime.md Sec2/Sec3/Sec7; layout, colours and wording are CHOSEN
// (HANDOFF-C.md section 10).
#include <algorithm>
#include <cstdio>
#include <ctime>

#include <cstdlib>
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "sr3frontend/bindings.h"
#include "sr3frontend/gamepad.h"
#include "sr3frontend/menu_model.h"
#include "sr3game/frontend.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

namespace sr3game {

namespace {

using sr3frontend::DefaultText;

constexpr Rgba kBackdrop{12, 10, 18, 255};
constexpr Rgba kPanel{20, 18, 30, 220};
constexpr Rgba kAccent{150, 60, 200, 255};  // the Saints purple (CHOSEN)
constexpr Rgba kTitle{255, 255, 255, 255};
constexpr Rgba kNormal{205, 200, 215, 255};
constexpr Rgba kSelected{255, 255, 255, 255};
constexpr Rgba kDim{130, 125, 145, 255};
constexpr Rgba kWarn{255, 140, 110, 255};
constexpr Rgba kHighlight{150, 60, 200, 200};

double LineH(const FrontendCanvas& c) { return std::max(28.0, c.Height() / 22.0); }

void Centred(FrontendCanvas& c, const std::string& t, double y, Rgba col) {
    c.DrawText(t, (c.Width() - c.TextWidth(t)) * 0.5, y, col);
}

// A vertical list of entries centred at x, the selected one on a highlight bar.
void List(FrontendCanvas& c, const std::vector<std::string>& items, int sel, double y0, double x0, double x1) {
    const double h = LineH(c);
    for (size_t i = 0; i < items.size(); ++i) {
        const double y = y0 + h * static_cast<double>(i);
        const bool s = static_cast<int>(i) == sel;
        if (s) c.FillRect(x0, y - 3, x1, y + h - 7, kHighlight);
        c.DrawText(items[i], x0 + 16, y, s ? kSelected : kNormal);
    }
}

void Panel(FrontendCanvas& c, double x0, double y0, double x1, double y1) {
    c.FillRect(x0, y0, x1, y1, kPanel);
    c.FillRect(x0, y0, x1, y0 + 4, kAccent);
}

// A destructive two-button confirmation (dialog.lua style): title key, body key, Yes / No.
void ConfirmBox(FrontendCanvas& c, const std::string& title, const std::string& question, int sel) {
    const double w = c.Width(), h = c.Height(), lh = LineH(c);
    const double bw = std::max(c.TextWidth(question) + 60.0, w * 0.4);
    const double x0 = (w - bw) * 0.5, y0 = h * 0.38;
    Panel(c, x0, y0, x0 + bw, y0 + lh * 4.6);
    Centred(c, title, y0 + lh * 0.4, kTitle);
    Centred(c, question, y0 + lh * 1.4, kNormal);
    const std::string yes = DefaultText("CONTROL_YES"), no = DefaultText("CONTROL_NO");
    const double yy = y0 + lh * 3.0, gap = 80.0;
    const double xYes = w * 0.5 - gap - c.TextWidth(yes), xNo = w * 0.5 + gap;
    if (sel == sr3frontend::kConfirmYes) c.FillRect(xYes - 14, yy - 3, xYes + c.TextWidth(yes) + 14, yy + lh - 7, kHighlight);
    else c.FillRect(xNo - 14, yy - 3, xNo + c.TextWidth(no) + 14, yy + lh - 7, kHighlight);
    c.DrawText(yes, xYes, yy, kSelected);
    c.DrawText(no, xNo, yy, kSelected);
}

std::string SaveLine(const SaveFileInfo& s) {
    char buf[200];
    if (!s.valid) {
        std::snprintf(buf, sizeof buf, "%s   (unreadable)", s.displayName.c_str());
        return buf;
    }
    std::time_t t = static_cast<std::time_t>(s.lastWriteUnix);
    char when[32] = "";
    std::tm tmv{};
    if (localtime_s(&tmv, &t) == 0) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
    const uint32_t m = s.playTimeSeconds / 60;
    std::snprintf(buf, sizeof buf, "%s   %s   %uh %02um   $%lld   Level %u", s.displayName.c_str(), when, m / 60,
                  m % 60, static_cast<long long>(s.cashRaw / 100), s.respectLevel + 1);
    return buf;
}

// A one-button notice box.
void NoticeBox(FrontendCanvas& c, const std::string& title, const std::string& text) {
    const double w = c.Width(), h = c.Height(), lh = LineH(c);
    const double bw = std::max(c.TextWidth(text) + 60.0, w * 0.36);
    const double bx = (w - bw) * 0.5, by = h * 0.4;
    Panel(c, bx, by, bx + bw, by + lh * 4.0);
    Centred(c, title, by + lh * 0.4, kTitle);
    Centred(c, text, by + lh * 1.5, kNormal);
    Centred(c, "OK", by + lh * 2.7, kSelected);
}

// INTERIM (HANDOFF-C 10): the contract gives the menus no game-data access, so the shipped
// control_binding_sets.xtbl is read from misc_tables.vpp_pc under %CRREISH_GAME_CACHE_DIR%, else the
// build-time cache dir. Proposal for Team B: a FrontendContext table-reader hook.
std::optional<sr3frontend::BindingModel> LoadBindings() {
    static std::optional<sr3frontend::BindingModel> cached;
    static bool tried = false;
    if (tried) return cached;
    tried = true;
    std::string dir;
    {
        char* e = nullptr;
        size_t len = 0;
        if (_dupenv_s(&e, &len, "CRREISH_GAME_CACHE_DIR") == 0 && e) dir = e;
        std::free(e);
    }
#ifdef CRREISH_GAME_CACHE_DIR
    if (dir.empty()) dir = CRREISH_GAME_CACHE_DIR;
#endif
    if (dir.empty() || !std::ifstream(dir + "/misc_tables.vpp_pc", std::ios::binary).good()) return cached;
    try {
        auto misc = vppcache::openArchive(dir + "/misc_tables.vpp_pc");
        const vpp::Container& c = misc->container;
        for (size_t i = 0; i < c.entries().size(); ++i) {
            if (c.entries()[i].name != "control_binding_sets.xtbl") continue;
            std::vector<uint8_t> bytes;
            if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
                const vpp::ByteView r = c.rawEntryBytes(i);
                bytes.assign(r.data(), r.data() + r.size());
            } else {
                bytes = c.decompressEntry(i).data;
            }
            const auto doc = sr3xtbl::ParseDocument(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            cached = sr3frontend::BindingModel::FromTable(sr3tables_ui_controls::ParseControlBindingSetsTable(doc));
        }
    } catch (const std::exception&) {
    }
    return cached;
}

// "Press any key": the next newly pressed key or mouse button (Win32 polling, edge-triggered against the
// state when the capture began). Esc is left to the menu Back (cancel). The mouse wheel cannot be
// captured this way (not polled).
class CapturePoller {
public:
    void prime() {
        for (int vk = 1; vk < 256; ++vk) down_[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    std::optional<sr3frontend::BindInput> poll() {
        std::optional<sr3frontend::BindInput> got;
        for (int vk = 1; vk < 256; ++vk) {
            const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
            const bool edge = now && !down_[vk];
            down_[vk] = now;
            if (!edge || got || vk == VK_ESCAPE) continue;
            switch (vk) {
            case VK_LBUTTON: got = sr3frontend::BindInput{true, 0}; continue;
            case VK_MBUTTON: got = sr3frontend::BindInput{true, 1}; continue;
            case VK_RBUTTON: got = sr3frontend::BindInput{true, 2}; continue;
            case VK_XBUTTON1: got = sr3frontend::BindInput{true, 3}; continue;
            case VK_XBUTTON2: got = sr3frontend::BindInput{true, 4}; continue;
            case VK_SHIFT: case VK_CONTROL: case VK_MENU: continue;  // the left/right variants follow
            default: break;
            }
            const UINT sc = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
            if (sc == 0) continue;
            const int code = (sc & 0xFF00) ? static_cast<int>(0x80 | (sc & 0x7F)) : static_cast<int>(sc & 0xFF);
            got = sr3frontend::BindInput{false, code};
        }
        return got;
    }

private:
    bool down_[256] = {};
};

std::string SlotText(const sr3frontend::BindSlot& s) {
    if (s.mouse >= 0) return sr3frontend::MouseButtonName(s.mouse);
    return s.key.empty() ? std::string("UNBOUND") : s.key;
}

void DrawRemap(FrontendCanvas& c, const sr3frontend::OptionsModel& m, double x0, double y0) {
    using sr3frontend::OptionsScreen;
    const double w = c.Width(), lh = LineH(c);
    const double x1 = x0 + std::max(760.0, w * 0.55);
    const auto& cats = m.bindings()->categories();
    if (m.screen() == OptionsScreen::RemapCategories || m.screen() == OptionsScreen::ConfirmRemapReset) {
        Panel(c, x0 - 10, y0 - lh * 1.4, x1 + 10, y0 + lh * (cats.size() + 1.6));
        c.DrawText("Remap Controls", x0 + 6, y0 - lh, kTitle);
        std::vector<std::string> items = {"Restore Defaults"};
        for (const auto& cat : cats) items.push_back(cat.label);
        List(c, items, m.selection(), y0 + lh * 0.3, x0, x1);
        if (m.screen() == OptionsScreen::ConfirmRemapReset)
            ConfirmBox(c, "Restore Defaults", "Restore every control set to its default bindings?", m.selection());
        return;
    }
    const auto& cat = cats[static_cast<size_t>(m.remapCategory())];
    constexpr int kRows = 14;
    const int n = static_cast<int>(cat.rows.size());
    const int top = std::clamp(m.remapRow() - kRows / 2, 0, std::max(0, n - kRows));
    Panel(c, x0 - 10, y0 - lh * 1.4, x1 + 10, y0 + lh * (kRows + 1.0));
    c.DrawText(cat.label + "   (left/right: primary / secondary)", x0 + 6, y0 - lh, kTitle);
    const double colA = x0 + (x1 - x0) * 0.52, colB = x0 + (x1 - x0) * 0.77;
    for (int i = top; i < std::min(n, top + kRows); ++i) {
        const auto& r = cat.rows[static_cast<size_t>(i)];
        const double y = y0 + lh * 0.3 + lh * (i - top);
        if (i == m.remapRow()) {
            const double cx = m.remapSecondary() ? colB : colA;
            c.FillRect(cx - 8, y - 3, cx + (x1 - x0) * 0.23, y + lh - 7, kHighlight);
        }
        std::string name = r.action;
        if (r.axis) name += r.negative ? " (-)" : " (+)";
        c.DrawText(name, x0 + 6, y, i == m.remapRow() ? kSelected : kNormal);
        c.DrawText(SlotText(r.primary), colA, y, kNormal);
        c.DrawText(SlotText(r.secondary), colB, y, kNormal);
    }
    if (m.screen() == OptionsScreen::RemapCapture) {
        const auto& r = cat.rows[static_cast<size_t>(m.remapRow())];
        const double bw = std::max(c.TextWidth(r.action) + 80.0, w * 0.36);
        const double bx = (w - bw) * 0.5, by = c.Height() * 0.4;
        Panel(c, bx, by, bx + bw, by + lh * 4.0);
        Centred(c, "Press a key or mouse button", by + lh * 0.4, kTitle);
        Centred(c, r.action, by + lh * 1.5, kNormal);
        Centred(c, "Esc cancels", by + lh * 2.6, kDim);
    }
}

// The options list / page / restore-defaults confirm (spec-tables-ui-controls Sec18.2).
void DrawOptions(FrontendCanvas& c, const sr3frontend::OptionsModel& m, double x0, double y0) {
    using sr3frontend::OptionsScreen;
    if (m.screen() >= OptionsScreen::RemapCategories) {
        DrawRemap(c, m, x0, y0);
        return;
    }
    const double w = c.Width(), lh = LineH(c);
    const double x1 = x0 + std::max(560.0, w * 0.42);
    if (m.screen() == OptionsScreen::List) {
        Panel(c, x0 - 10, y0 - lh * 1.4, x1 + 10, y0 + lh * 4.6);
        c.DrawText("Options", x0 + 6, y0 - lh, kTitle);
        std::vector<std::string> pages;
        for (int i = 0; i < 4; ++i) pages.push_back(sr3frontend::OptionsPageName(static_cast<sr3frontend::OptionsPage>(i)));
        List(c, pages, m.selection(), y0 + lh * 0.3, x0, x1);
        return;
    }
    const auto items = sr3frontend::OptionItems(m.page());
    Panel(c, x0 - 10, y0 - lh * 1.4, x1 + 10, y0 + lh * (items.size() + 1.0));
    c.DrawText(sr3frontend::OptionsPageName(m.page()), x0 + 6, y0 - lh, kTitle);
    std::vector<std::string> labels;
    for (const auto& it : items) labels.push_back(it.label);
    List(c, labels, m.pageSelection(), y0 + lh * 0.3, x0, x1);
    for (size_t i = 0; i < items.size(); ++i) {
        const std::string v = m.valueText(items[i]);
        if (v.empty()) continue;
        const std::string shown = items[i].kind == sr3frontend::OptionKind::Slider ? v : "< " + v + " >";
        c.DrawText(shown, x1 - 16 - c.TextWidth(shown), y0 + lh * 0.3 + lh * static_cast<double>(i), kSelected);
    }
    if (m.screen() == OptionsScreen::ConfirmRestore)
        ConfirmBox(c, "Restore Defaults", "Restore this page to its default settings?", m.selection());
}

std::string EntryText(sr3frontend::MainEntry e) {
    switch (e) {
    case sr3frontend::MainEntry::Continue: return DefaultText("MAINMENU_CONTINUE");
    case sr3frontend::MainEntry::Campaign: return DefaultText("MAINMENU_CAMPAIGN");
    case sr3frontend::MainEntry::Options: return DefaultText("MAINMENU_OPTIONS");
    case sr3frontend::MainEntry::Quit: return DefaultText("PLT_QUIT_GAME");
    }
    return {};
}

void DrawLoadList(FrontendCanvas& c, const sr3frontend::MainMenuModel& m) {
    const double w = c.Width(), h = c.Height(), lh = LineH(c);
    const double x0 = w * 0.12, x1 = w * 0.88, y0 = h * 0.28;
    Panel(c, x0, y0, x1, y0 + lh * (sr3frontend::MainMenuModel::kListRows + 3.0));
    c.DrawText(DefaultText("SAVELOAD_LOAD_GAME"), x0 + 16, y0 + lh * 0.4, kTitle);
    if (m.saves().empty()) {  // one non-selectable row (spec-frontend-flow Sec5.1)
        c.DrawText(DefaultText("SAVELOAD_NOTHING_TO_LOAD"), x0 + 24, y0 + lh * 1.8, kDim);
        return;
    }
    std::vector<std::string> rows;
    const int top = m.listTop();
    const int n = static_cast<int>(m.saves().size());
    for (int i = top; i < std::min(n, top + sr3frontend::MainMenuModel::kListRows); ++i)
        rows.push_back(SaveLine(m.saves()[static_cast<size_t>(i)]));
    List(c, rows, m.selection() - top, y0 + lh * 1.8, x0 + 8, x1 - 8);
    if (top > 0) c.DrawText("^", x1 - 30, y0 + lh * 1.4, kDim);
    if (top + sr3frontend::MainMenuModel::kListRows < n) c.DrawText("v", x1 - 30, y0 + lh * 10.0, kDim);
}

void DrawMain(FrontendCanvas& c, const sr3frontend::MainMenuModel& m, bool padConnected) {
    using sr3frontend::MainScreen;
    const double w = c.Width(), h = c.Height(), lh = LineH(c);
    c.FillRect(0, 0, w, h * 0.22, Rgba{30, 14, 44, 255});
    c.FillRect(0, h * 0.22, w, h * 0.22 + 4, kAccent);
    Centred(c, "SAINTS ROW: THE THIRD", h * 0.08, kTitle);
    Centred(c, "IdolRecomp clean-room build", h * 0.08 + lh, kDim);
    const std::string hint = padConnected ? "A select   B back   D-pad / stick move" : "Enter select   Esc back   Arrows move";
    Centred(c, hint, h - lh * 1.6, kDim);
    const double x0 = w * 0.08, x1 = w * 0.08 + std::max(260.0, w * 0.24), y0 = h * 0.36;

    switch (m.screen()) {
    case MainScreen::LoadList:
    case MainScreen::LoadError:
        DrawLoadList(c, m);
        if (m.screen() == MainScreen::LoadError)
            NoticeBox(c, DefaultText("SAVELOAD_ERROR"), DefaultText("SAVELOAD_FILE_NOT_LOADABLE"));
        return;
    case MainScreen::Campaign:
        c.DrawText(DefaultText("MAINMENU_CAMPAIGN"), x0 + 16, y0 - lh * 1.2, kTitle);
        List(c, {DefaultText("MAINMENU_NEW"), DefaultText("SAVELOAD_LOAD_GAME")}, m.selection(), y0, x0, x1);
        return;
    case MainScreen::Difficulty:
        c.DrawText(DefaultText("MAINMENU_NEW_GAME_DIFF_HDR"), x0 + 16, y0 - lh * 1.2, kTitle);
        List(c, {DefaultText("DLT_CASUAL"), DefaultText("DLT_NORMAL"), DefaultText("DLT_HARDCORE")}, m.selection(), y0,
             x0, x1);
        return;
    case MainScreen::Options: DrawOptions(c, m.options(), x0, y0); return;
    default: break;
    }
    std::vector<std::string> items;
    for (sr3frontend::MainEntry e : m.entries()) items.push_back(EntryText(e));
    List(c, items, m.screen() == MainScreen::Top ? m.selection() : -1, y0, x0, x1);
    const int ci = m.continueIndex();
    if (ci >= 0) c.DrawText(SaveLine(m.saves()[static_cast<size_t>(ci)]), x1 + 30, y0, kDim);
    if (m.screen() == MainScreen::Notice)
        NoticeBox(c, DefaultText("MENU_TITLE_NOTICE"), DefaultText("MAIN_MENU_CONTINUE_UNAVAILABLE"));
    else if (m.screen() == MainScreen::ConfirmQuit)
        ConfirmBox(c, DefaultText("PLT_QUIT_GAME"), DefaultText("PLT_QUIT_GAME_WARNING"), m.selection());
}

void Log(FrontendContext& ctx, const std::string& s) {
    if (ctx.log) ctx.log(s);
}

}  // namespace

MainMenuResult RunMainMenu(FrontendContext& ctx) {
    sr3frontend::MainMenuModel model;
    model.options() = sr3frontend::OptionsModel(sr3frontend::LoadOptions(ctx.saveDir));  // the profile's options
    if (auto b = LoadBindings()) model.options().setBindings(*b);
    CapturePoller capture;
    bool capturing = false;
    sr3frontend::GamepadReader pad;
    if (ctx.listSaves) model.setSaves(ctx.listSaves());
    Log(ctx, "frontend : main menu (Team C) - " + std::to_string(model.saves().size()) + " save(s), continue " +
                 (model.continueIndex() >= 0 ? model.saves()[static_cast<size_t>(model.continueIndex())].displayName
                                             : std::string("unavailable")));
    sr3frontend::MainScreen lastScreen = model.screen();
    for (;;) {
        if (!ctx.beginFrame || !ctx.beginFrame(kBackdrop)) {
            Log(ctx, "frontend : window closed -> Quit");
            return MainMenuResult{};
        }
        const sr3frontend::GamepadEdges g = pad.poll();
        const FrontendInput in = sr3frontend::Merge(ctx.pollInput ? ctx.pollInput() : FrontendInput{}, g.menu);
        const std::optional<MainMenuResult> r = model.update(in);
        const bool nowCapturing = model.screen() == sr3frontend::MainScreen::Options &&
                                  model.options().screen() == sr3frontend::OptionsScreen::RemapCapture;
        if (nowCapturing && !capturing) capture.prime();
        if (nowCapturing && capturing)
            if (const auto got = capture.poll()) model.options().capture(*got);
        capturing = nowCapturing;
        if (model.options().takeCommitted())  // pause_menu_accept_options: write the profile
            Log(ctx, std::string("frontend : options committed -> ") +
                         (sr3frontend::SaveOptions(ctx.saveDir, model.options().options()) ? "saved " : "NOT saved ") +
                         sr3frontend::ProfilePath(ctx.saveDir));
        if (model.screen() != lastScreen) {
            if (model.screen() == sr3frontend::MainScreen::LoadList && lastScreen == sr3frontend::MainScreen::Campaign &&
                ctx.listSaves)
                model.setSaves(ctx.listSaves());  // a fresh listing when the load list opens
            static const char* kNames[] = {"top", "campaign", "difficulty", "load list",
                                           "notice MAIN_MENU_CONTINUE_UNAVAILABLE", "error SAVELOAD_FILE_NOT_LOADABLE",
                                           "confirm PLT_QUIT_GAME", "options"};
            Log(ctx, std::string("frontend : screen ") + kNames[static_cast<int>(model.screen())]);
            lastScreen = model.screen();
        }
        if (ctx.canvas) DrawMain(*ctx.canvas, model, g.connected);
        if (ctx.endFrame) ctx.endFrame();
        ++ctx.frameIndex;
        if (r) {
            static const char* kKinds[] = {"NewGame", "LoadSave", "Quit"};  // the stand-in's log format
            Log(ctx, std::string("frontend : main menu result = ") + kKinds[static_cast<int>(r->kind)] +
                         (r->savePath.empty() ? "" : " " + r->savePath));
            if (r->kind == MainMenuResult::Kind::NewGame)  // not in the contract yet: logged only
                Log(ctx, "frontend : difficulty " + std::to_string(model.difficulty()) + " (0 Casual, 1 Normal, 2 Hardcore)");
            return *r;
        }
    }
}

struct PauseMenu {
    FrontendContext* ctx = nullptr;
    sr3frontend::PauseMenuModel model;
    sr3frontend::GamepadReader pad;
    bool padConnected = false;
    CapturePoller capture;
    bool capturing = false;
};

PauseMenu* PauseMenuCreate(FrontendContext& ctx) {
    PauseMenu* m = new PauseMenu;
    m->ctx = &ctx;
    m->model.options() = sr3frontend::OptionsModel(sr3frontend::LoadOptions(ctx.saveDir));
    if (auto b = LoadBindings()) m->model.options().setBindings(*b);
    return m;
}

void PauseMenuDestroy(PauseMenu* menu) { delete menu; }

void PauseMenuOpened(PauseMenu& menu) {
    menu.model.opened();
    menu.pad.poll();  // swallow the press that opened it
    Log(*menu.ctx, "frontend : pause menu opened");
}

PauseMenuAction PauseMenuUpdate(PauseMenu& menu, const FrontendInput& in) {
    const sr3frontend::GamepadEdges g = menu.pad.poll();
    menu.padConnected = g.connected;
    if (g.start) return PauseMenuAction::Resume;  // Start closes the pause menu (CHOSEN)
    const PauseMenuAction a = menu.model.update(sr3frontend::Merge(in, g.menu));
    const bool nowCapturing = menu.model.screen() == sr3frontend::PauseScreen::Options &&
                              menu.model.options().screen() == sr3frontend::OptionsScreen::RemapCapture;
    if (nowCapturing && !menu.capturing) menu.capture.prime();
    if (nowCapturing && menu.capturing)
        if (const auto got = menu.capture.poll()) menu.model.options().capture(*got);
    menu.capturing = nowCapturing;
    if (menu.model.options().takeCommitted())
        Log(*menu.ctx, std::string("frontend : options committed -> ") +
                           (sr3frontend::SaveOptions(menu.ctx->saveDir, menu.model.options().options()) ? "saved " : "NOT saved ") +
                           sr3frontend::ProfilePath(menu.ctx->saveDir));
    if (a != PauseMenuAction::None) {
        static const char* kA[] = {"None", "Resume", "Save", "QuitToMainMenu", "QuitToDesktop"};
        Log(*menu.ctx, std::string("frontend : pause menu -> ") + kA[static_cast<int>(a)]);
    }
    return a;
}

void PauseMenuDraw(PauseMenu& menu, FrontendCanvas& c) {
    using sr3frontend::PauseEntry;
    const double w = c.Width(), h = c.Height(), lh = LineH(c);
    const double x0 = w * 0.08, x1 = x0 + std::max(300.0, w * 0.26), y0 = h * 0.24;
    Panel(c, x0 - 10, y0 - lh * 1.4, x1 + 10, y0 + lh * 6.0);
    c.DrawText(DefaultText("SYSTEM_PAUSED"), x0 + 6, y0 - lh, kTitle);
    std::vector<std::string> items;
    for (PauseEntry e : menu.model.entries()) {
        switch (e) {
        case PauseEntry::SaveGame: items.push_back(DefaultText("SAVELOAD_SAVE_GAME")); break;
        case PauseEntry::Options: items.push_back(DefaultText("MAINMENU_OPTIONS")); break;
        case PauseEntry::Quit: items.push_back(DefaultText("PAUSE_MENU_QUIT")); break;
        case PauseEntry::QuitToDesktop: items.push_back(DefaultText("PAUSE_MENU_QUIT_DESKTOP")); break;
        }
    }
    const bool top = menu.model.screen() == sr3frontend::PauseScreen::Top;
    List(c, items, top ? menu.model.selection() : -1, y0 + lh * 0.3, x0, x1);
    if (!menu.model.status().empty()) c.DrawText(menu.model.status(), x0 + 6, y0 + lh * 4.8, kWarn);
    c.DrawText(menu.padConnected ? "A select   B / Start resume" : "Enter select   Esc resume", x0, h - lh * 1.6, kDim);
    if (menu.model.screen() == sr3frontend::PauseScreen::Options) DrawOptions(c, menu.model.options(), x1 + 40, y0);
    if (menu.model.screen() == sr3frontend::PauseScreen::ConfirmQuitToMenu)
        ConfirmBox(c, DefaultText("PAUSE_MENU_QUIT_TITLE"), DefaultText("QUIT_GAME_TEXT_FULL"), menu.model.selection());
    else if (menu.model.screen() == sr3frontend::PauseScreen::ConfirmQuitToDesktop)
        ConfirmBox(c, DefaultText("PLT_QUIT_GAME"), DefaultText("QUIT_DESKTOP_TEXT"), menu.model.selection());
}

void PauseMenuSetStatus(PauseMenu& menu, const std::string& text) {
    menu.model.setStatus(text);
    Log(*menu.ctx, "frontend : pause status: " + text);
}

}  // namespace sr3game
