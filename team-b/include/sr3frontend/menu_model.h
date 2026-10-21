// Front-end menu state machines for sr3.exe (Team C, HANDOFF-C.md section 10): the main menu
// and the pause menu, independent of drawing so they can be tested. Structure from
// spec-frontend-flow.md (Sec3 main menu, Sec4.1 campaign/difficulty, Sec5 load list, Sec8 quit)
// and spec-tables-ui-controls.md Sec18.1 (pause top list):
//   * PC accepts press start automatically (Sec3.2), so the player lands on the top list;
//   * top list: Continue (only when a readable save exists; the newest one), Campaign, Quit Game;
//     Back/Esc on the top list = the PLT_QUIT_GAME confirmation (Sec8.2);
//   * Campaign: New Game -> difficulty (Casual/Normal/Hardcore), Load Game -> the load list;
//   * load list (Sec5.1): readable saves newest first, unreadable ones last; no saves -> one
//     SAVELOAD_NOTHING_TO_LOAD row; from the main menu a readable row loads at once (Sec5.2);
//     an unreadable row shows SAVELOAD_ERROR / SAVELOAD_FILE_NOT_LOADABLE (Sec7.4);
//   * pause (Sec18.1): header SYSTEM_PAUSED; Save Game, Quit (PAUSE_MENU_QUIT_TITLE /
//     QUIT_GAME_TEXT_FULL -> the main menu); Back resumes (there is no Resume entry).
//   * Options (PC) on both lists (spec-tables-ui-controls Sec18.2, sr3frontend/options.h).
// CHOSEN (labelled in HANDOFF-C 10): entries not built yet are left out (Co-op, Whored,
// DLC, Extras, pause Load Game / Cancel Mission / record mode); "Quit to Desktop" is an extra
// pause entry (PC convenience; retail quits to the main menu); the display-calibration screen
// before a new game is skipped; the English text for each key (DefaultText); confirms start on No.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sr3frontend/options.h"
#include "sr3game/frontend.h"

namespace sr3frontend {

// English text for the localisation keys the menus use (CHOSEN wording; the real strings live in
// the game's localisation tables).
std::string DefaultText(const std::string& key);

enum class MainScreen { Top, Campaign, Difficulty, LoadList, Notice, LoadError, ConfirmQuit, Options };
enum class MainEntry { Continue, Campaign, Options, Quit };
enum class CampaignEntry { NewGame, LoadGame };

// Readable saves newest first, then unreadable ones (spec-frontend-flow Sec5.1 order; the
// autosave-first rule needs an autosave marker the listing does not carry yet).
std::vector<sr3game::SaveFileInfo> SortSavesForLoad(std::vector<sr3game::SaveFileInfo> saves);

class MainMenuModel {
public:
    MainMenuModel();
    void setSaves(std::vector<sr3game::SaveFileInfo> saves);  // rebuilds the top list
    std::optional<sr3game::MainMenuResult> update(const sr3game::FrontendInput& in);

    MainScreen screen() const { return screen_; }
    int selection() const { return sel_; }
    const std::vector<MainEntry>& entries() const { return entries_; }
    const std::vector<sr3game::SaveFileInfo>& saves() const { return saves_; }
    int listTop() const { return listTop_; }
    int continueIndex() const;  // the newest readable save, or -1
    int difficulty() const { return difficulty_; }  // 0 Casual, 1 Normal, 2 Hardcore (the last chosen)
    OptionsModel& options() { return options_; }
    const OptionsModel& options() const { return options_; }
    static constexpr int kListRows = 8;

private:
    void enter(MainScreen s, int sel = 0);
    MainScreen screen_ = MainScreen::Top;
    std::vector<MainEntry> entries_;
    std::vector<sr3game::SaveFileInfo> saves_;
    int sel_ = 0;
    int listTop_ = 0;
    int difficulty_ = 1;  // Normal (the shipped default, spec-tables-ui-controls Sec18.2)
    OptionsModel options_;
};

enum class PauseScreen { Top, ConfirmQuitToMenu, ConfirmQuitToDesktop, Options };
enum class PauseEntry { SaveGame, Options, Quit, QuitToDesktop };

class PauseMenuModel {
public:
    PauseMenuModel();
    void opened();  // selection back to the first entry, status cleared
    sr3game::PauseMenuAction update(const sr3game::FrontendInput& in);
    void setStatus(const std::string& s) { status_ = s; }

    PauseScreen screen() const { return screen_; }
    int selection() const { return sel_; }
    const std::vector<PauseEntry>& entries() const { return entries_; }
    const std::string& status() const { return status_; }
    OptionsModel& options() { return options_; }
    const OptionsModel& options() const { return options_; }

private:
    OptionsModel options_;
    PauseScreen screen_ = PauseScreen::Top;
    std::vector<PauseEntry> entries_;
    int sel_ = 0;
    std::string status_;
};

constexpr int kConfirmYes = 0;  // two-button confirms: 0 = Yes, 1 = No

}  // namespace sr3frontend
