// Front-end menu state machines. See include/sr3frontend/menu_model.h.

#include "sr3frontend/menu_model.h"

#include <algorithm>
#include <map>

namespace sr3frontend {

std::string DefaultText(const std::string& key) {
    static const std::map<std::string, std::string> kText = {
        {"MAINMENU_CONTINUE", "Continue"},
        {"MAINMENU_CAMPAIGN", "Campaign"},
        {"MAINMENU_OPTIONS", "Options"},
        {"PLT_QUIT_GAME", "Quit Game"},
        {"PLT_QUIT_GAME_WARNING", "Are you sure you want to quit the game?"},
        {"MAINMENU_NEW", "New Game"},
        {"SAVELOAD_LOAD_GAME", "Load Game"},
        {"MAINMENU_NEW_GAME_DIFF_HDR", "Select Difficulty"},
        {"DLT_CASUAL", "Casual"},
        {"DLT_NORMAL", "Normal"},
        {"DLT_HARDCORE", "Hardcore"},
        {"MENU_TITLE_NOTICE", "Notice"},
        {"MAIN_MENU_CONTINUE_UNAVAILABLE", "There is no saved game to continue."},
        {"SAVELOAD_NOTHING_TO_LOAD", "No saved games"},
        {"SAVELOAD_ERROR", "Error"},
        {"SAVELOAD_FILE_NOT_LOADABLE", "This save file cannot be loaded."},
        {"SAVELOAD_SAVEFILE_CORRUPT_TITLE", "Corrupt save"},
        {"CONTROL_YES", "Yes"},
        {"CONTROL_NO", "No"},
        {"SYSTEM_PAUSED", "Paused"},
        {"SAVELOAD_SAVE_GAME", "Save Game"},
        {"PAUSE_MENU_QUIT", "Quit"},
        {"PAUSE_MENU_QUIT_TITLE", "Quit Game"},
        {"QUIT_GAME_TEXT_FULL", "Quit to the main menu? Unsaved progress will be lost."},
        {"PAUSE_MENU_QUIT_DESKTOP", "Quit to Desktop"},
        {"QUIT_DESKTOP_TEXT", "Quit to the desktop? Unsaved progress will be lost."},
        {"SAVELOAD_SAVING_MESSAGE_TITLE", "Saving..."},
    };
    const auto it = kText.find(key);
    return it == kText.end() ? key : it->second;
}

namespace {

void Wrap(int& sel, int count, const sr3game::FrontendInput& in) {
    if (count <= 0) return;
    if (in.down) sel = (sel + 1) % count;
    if (in.up) sel = (sel + count - 1) % count;
}

void Toggle(int& sel, const sr3game::FrontendInput& in) {
    if (in.left || in.right || in.up || in.down) sel = 1 - sel;
}

}  // namespace

std::vector<sr3game::SaveFileInfo> SortSavesForLoad(std::vector<sr3game::SaveFileInfo> saves) {
    std::stable_sort(saves.begin(), saves.end(), [](const sr3game::SaveFileInfo& a, const sr3game::SaveFileInfo& b) {
        if (a.valid != b.valid) return a.valid;
        return a.valid && a.lastWriteUnix > b.lastWriteUnix;
    });
    return saves;
}

MainMenuModel::MainMenuModel() { setSaves({}); }

void MainMenuModel::setSaves(std::vector<sr3game::SaveFileInfo> saves) {
    saves_ = SortSavesForLoad(std::move(saves));
    entries_.clear();
    if (continueIndex() >= 0) entries_.push_back(MainEntry::Continue);  // only with a readable save (Sec3.4)
    entries_.push_back(MainEntry::Campaign);
    entries_.push_back(MainEntry::Options);
    entries_.push_back(MainEntry::Quit);
    if (screen_ == MainScreen::Top) sel_ = 0;  // the list is rebuilt with the cursor on its first entry
}

int MainMenuModel::continueIndex() const {
    return !saves_.empty() && saves_.front().valid ? 0 : -1;  // sorted: the newest readable first
}

void MainMenuModel::enter(MainScreen s, int sel) {
    screen_ = s;
    sel_ = sel;
    listTop_ = 0;
}

std::optional<sr3game::MainMenuResult> MainMenuModel::update(const sr3game::FrontendInput& in) {
    using R = sr3game::MainMenuResult;
    auto indexOf = [&](MainEntry e) {
        for (size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i] == e) return static_cast<int>(i);
        return 0;
    };
    switch (screen_) {
    case MainScreen::Top: {
        Wrap(sel_, static_cast<int>(entries_.size()), in);
        if (in.back) {
            enter(MainScreen::ConfirmQuit, 1);
            return std::nullopt;
        }
        if (!in.accept) return std::nullopt;
        switch (entries_[static_cast<size_t>(sel_)]) {
        case MainEntry::Continue: {
            const int c = continueIndex();
            if (c < 0) {
                enter(MainScreen::Notice);
                return std::nullopt;
            }
            R r;
            r.kind = R::Kind::LoadSave;
            r.savePath = saves_[static_cast<size_t>(c)].path;
            return r;
        }
        case MainEntry::Campaign: enter(MainScreen::Campaign); return std::nullopt;
        case MainEntry::Options:
            options_.opened();
            enter(MainScreen::Options);
            return std::nullopt;
        case MainEntry::Quit: enter(MainScreen::ConfirmQuit, 1); return std::nullopt;
        }
        return std::nullopt;
    }
    case MainScreen::Campaign:
        Wrap(sel_, 2, in);
        if (in.back) {
            enter(MainScreen::Top, indexOf(MainEntry::Campaign));
            return std::nullopt;
        }
        if (in.accept) {
            if (sel_ == 0) enter(MainScreen::Difficulty, difficulty_);  // cursor on the current difficulty
            else enter(MainScreen::LoadList);
        }
        return std::nullopt;
    case MainScreen::Difficulty:
        Wrap(sel_, 3, in);
        if (in.back) {
            enter(MainScreen::Campaign, 0);
            return std::nullopt;
        }
        if (in.accept) {
            difficulty_ = sel_;  // game_difficulty_select(id - 1)
            R r;
            r.kind = R::Kind::NewGame;
            return r;
        }
        return std::nullopt;
    case MainScreen::LoadList: {
        if (in.back) {
            enter(MainScreen::Campaign, 1);
            return std::nullopt;
        }
        const int n = static_cast<int>(saves_.size());
        if (n == 0) return std::nullopt;  // the SAVELOAD_NOTHING_TO_LOAD row is not selectable
        Wrap(sel_, n, in);
        if (sel_ < listTop_) listTop_ = sel_;
        if (sel_ >= listTop_ + kListRows) listTop_ = sel_ - kListRows + 1;
        if (in.accept) {
            const sr3game::SaveFileInfo& s = saves_[static_cast<size_t>(sel_)];
            if (!s.valid) {
                const int keepSel = sel_, keepTop = listTop_;
                screen_ = MainScreen::LoadError;
                sel_ = keepSel;
                listTop_ = keepTop;
                return std::nullopt;
            }
            R r;
            r.kind = R::Kind::LoadSave;
            r.savePath = s.path;
            return r;
        }
        return std::nullopt;
    }
    case MainScreen::LoadError:
        if (in.accept || in.back) screen_ = MainScreen::LoadList;  // keeps the row and scroll
        return std::nullopt;
    case MainScreen::Notice:
        if (in.accept || in.back) enter(MainScreen::Top, 0);
        return std::nullopt;
    case MainScreen::Options:
        if (options_.update(in)) enter(MainScreen::Top, indexOf(MainEntry::Options));
        difficulty_ = options_.difficulty();
        return std::nullopt;
    case MainScreen::ConfirmQuit:
        Toggle(sel_, in);
        if (in.back) {
            enter(MainScreen::Top, indexOf(MainEntry::Quit));
            return std::nullopt;
        }
        if (in.accept) {
            if (sel_ == kConfirmYes) {
                R r;
                r.kind = R::Kind::Quit;
                return r;
            }
            enter(MainScreen::Top, indexOf(MainEntry::Quit));
        }
        return std::nullopt;
    }
    return std::nullopt;
}

PauseMenuModel::PauseMenuModel()
    : entries_{PauseEntry::SaveGame, PauseEntry::Options, PauseEntry::Quit, PauseEntry::QuitToDesktop} {}

void PauseMenuModel::opened() {
    screen_ = PauseScreen::Top;
    sel_ = 0;
    status_.clear();
}

sr3game::PauseMenuAction PauseMenuModel::update(const sr3game::FrontendInput& in) {
    using A = sr3game::PauseMenuAction;
    if (screen_ == PauseScreen::Options) {
        if (options_.update(in)) {
            screen_ = PauseScreen::Top;
            sel_ = 1;
        }
        return A::None;
    }
    if (screen_ != PauseScreen::Top) {
        Toggle(sel_, in);
        const bool toDesktop = screen_ == PauseScreen::ConfirmQuitToDesktop;
        const int entry = toDesktop ? 3 : 2;
        if (in.back) {
            screen_ = PauseScreen::Top;
            sel_ = entry;
            return A::None;
        }
        if (in.accept) {
            if (sel_ == kConfirmYes) return toDesktop ? A::QuitToDesktop : A::QuitToMainMenu;
            screen_ = PauseScreen::Top;
            sel_ = entry;
        }
        return A::None;
    }
    Wrap(sel_, static_cast<int>(entries_.size()), in);
    if (in.back) return A::Resume;  // Back on the top list closes the pause menu
    if (!in.accept) return A::None;
    switch (entries_[static_cast<size_t>(sel_)]) {
    case PauseEntry::SaveGame: status_ = DefaultText("SAVELOAD_SAVING_MESSAGE_TITLE"); return A::Save;
    case PauseEntry::Options:
        options_.opened();
        screen_ = PauseScreen::Options;
        return A::None;
    case PauseEntry::Quit:
        screen_ = PauseScreen::ConfirmQuitToMenu;
        sel_ = 1;
        return A::None;
    case PauseEntry::QuitToDesktop:
        screen_ = PauseScreen::ConfirmQuitToDesktop;
        sel_ = 1;
        return A::None;
    }
    return A::None;
}

}  // namespace sr3frontend
