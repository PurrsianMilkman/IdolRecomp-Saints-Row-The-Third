// FIRST PLAYABLE front end contract (sr3.exe) - the interface Team C implements to RENDER and NAVIGATE the menus.
//
// Who owns what
//   * Team B (tools/sr3_game/main.cpp, "the host") owns the process, the window/device/swap chain, the world
//     build and the live game loop. It fills a FrontendContext and calls the functions below.
//   * Team C owns the menus themselves: implement every function in the "Team C implements" sections below in
//     your own .cpp and link it INSTEAD of src/game_frontend_default.cpp (the CMake target
//     `sr3game_frontend_default` is the stand-in; swap it for yours in the `sr3` target). No host change is needed.
//
// What is spec'd and what is CHOSEN (spec-frontend-menu-runtime.md)
//   * The retail structure is: main menu -> main_menu_new_game (Sec2) | main_menu_continue (Sec3, picks the most
//     recent valid save slot; MAIN_MENU_CONTINUE_UNAVAILABLE dialog when none) ; pause menu = a screen pushed on
//     the UI screen stack with the shared game-pause depth raised (Sec7, CBA_GAC_PAUSE_MENU, "dialog_pause_show").
//   * CHOSEN (not spec'd): the entry list and wording of both menus, the arrow-key/Enter navigation, the "Load Game"
//     slot list (the retail directory screen is not reproduced), "Quit to Desktop", the save file naming.
//     The default implementation labels each of these.
//
// Threading: everything is called on the main thread, once per frame, from the host's loop.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sr3game {

struct Rgba {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

// ---- input -----------------------------------------------------------------------------------------------
// One frame of menu input, EDGE-triggered (true for exactly the frame the key went down). The host fills it from
// the keyboard (arrows/WASD, Enter/Space = accept, Esc/Backspace = back) or from a script (--script-menu=...).
struct FrontendInput {
    bool up = false, down = false, left = false, right = false;
    bool accept = false;  // Enter / Space
    bool back = false;    // Esc / Backspace
};

// ---- drawing ---------------------------------------------------------------------------------------------
// Pixel coordinates in the window's client area, (0,0) top-left. Valid only between FrontendContext::beginFrame()
// and endFrame() (main menu) or inside PauseMenuDraw() (the host has already drawn the 3D scene + HUD and a dim
// rectangle; do not clear or present there).
class FrontendCanvas {
public:
    virtual ~FrontendCanvas() = default;
    virtual uint32_t Width() const = 0;
    virtual uint32_t Height() const = 0;
    virtual void FillRect(double x0, double y0, double x1, double y1, Rgba colour) = 0;
    // The game's own body font (font id 0 of the loaded .vf3_pc), raw ASCII/Latin-1 text. Single line; '\n' is not
    // interpreted. Native glyph size (no scale parameter).
    virtual void DrawText(const std::string& text, double x, double y, Rgba colour) = 0;
    // Approximate advance width of `text` in pixels (for centring). CHOSEN estimate: fixed fraction of line height.
    virtual double TextWidth(const std::string& text) const = 0;
    // Native handles for a menu that wants to render with its own D3D11 pipeline (ID3D11Device*,
    // ID3D11DeviceContext*, the HWND). The back buffer is already bound as the render target.
    virtual void* NativeDevice() const = 0;
    virtual void* NativeContext() const = 0;
    virtual void* NativeWindow() const = 0;
};

// ---- save files ------------------------------------------------------------------------------------------
struct SaveFileInfo {
    std::string path;          // full path of the .sr3s_pc
    std::string displayName;   // file stem, e.g. "sr3save_01"
    int64_t lastWriteUnix = 0; // seconds since the Unix epoch (file mtime)
    bool valid = false;        // right size and version 94 (spec-save-format.md); invalid ones are listed but not loadable
    uint32_t playTimeSeconds = 0;
    int64_t cashRaw = 0;       // cents
    uint32_t respectLevel = 0;
};

// ---- the context the host hands to the menus ----------------------------------------------------------------
struct FrontendContext {
    FrontendCanvas* canvas = nullptr;
    // Main-menu loop plumbing. beginFrame(): pumps window messages, clears the back buffer to `clear` and
    // returns false when the window was closed (treat as Quit). endFrame(): presents.
    std::function<bool(Rgba clear)> beginFrame;
    std::function<void()> endFrame;
    // Polls this frame's menu input (live keyboard, or the --script-menu script).
    std::function<FrontendInput()> pollInput;
    // Fresh listing of the save directory, newest first. Call when entering a screen that shows saves.
    std::function<std::vector<SaveFileInfo>()> listSaves;
    std::string saveDir;       // where saves are written / listed (host CHOSEN default, see frontend_host.h notes)
    std::function<void(const std::string&)> log;  // stdout log line (flow log)
    uint64_t frameIndex = 0;   // host-incremented menu frame counter
};

// ---- main menu (Team C implements) -----------------------------------------------------------------------------
struct MainMenuResult {
    enum class Kind { NewGame, LoadSave, Quit } kind = Kind::Quit;
    std::string savePath;  // for LoadSave: the chosen SaveFileInfo::path
};

// Blocking: runs its own loop (beginFrame / draw / endFrame / pollInput) until the player chooses.
// Retail-structure entries (spec Sec2/Sec3): New Game (main_menu_new_game), Continue (main_menu_continue = the most
// recent valid save; show the MAIN_MENU_CONTINUE_UNAVAILABLE notice when there is none), plus CHOSEN Load Game / Quit.
MainMenuResult RunMainMenu(FrontendContext& ctx);

// ---- pause menu (Team C implements) ------------------------------------------------------------------------------
// Non-blocking, opaque, per-frame: the host keeps the sim frozen and calls Update (logic) then, in the HUD pass of
// the same frame, Draw. The host opens it on Esc (the existing playtest pause binding) or on a script event, and
// also closes it on Esc itself; a Back press is reported as Resume.
enum class PauseMenuAction {
    None,
    Resume,
    Save,           // host writes the save, then calls PauseMenuSetStatus with the outcome
    QuitToMainMenu, // host ends the session and returns to RunMainMenu
    QuitToDesktop,  // host exits the process
};

struct PauseMenu;  // opaque
PauseMenu* PauseMenuCreate(FrontendContext& ctx);
void PauseMenuDestroy(PauseMenu* menu);
void PauseMenuOpened(PauseMenu& menu);                       // reset selection each time it opens
PauseMenuAction PauseMenuUpdate(PauseMenu& menu, const FrontendInput& in);
void PauseMenuDraw(PauseMenu& menu, FrontendCanvas& canvas); // overlay on the frozen scene
void PauseMenuSetStatus(PauseMenu& menu, const std::string& text);  // e.g. "Saved: sr3save_03" / "Save failed"

// ---- host helpers (Team B implements, src/game_saves.cpp - Team C only CALLS these) -----------------------------
// CHOSEN default save directory: %USERPROFILE%\Documents\SR3Recomp\saves (override with sr3.exe --save-dir=DIR).
std::string DefaultSaveDirectory();
// Lists *.sr3s_pc in `dir`, newest first, with the header summary read via sr3saveflow/sr3save.
std::vector<SaveFileInfo> ListSaveFiles(const std::string& dir);
// "sr3save_NN.sr3s_pc" with the first NN (01..99) not present in `dir` (CHOSEN naming).
std::string NextSavePath(const std::string& dir);

}  // namespace sr3game
