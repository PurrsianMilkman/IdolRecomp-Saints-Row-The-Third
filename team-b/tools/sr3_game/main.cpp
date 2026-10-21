// sr3.exe - FIRST PLAYABLE: boot -> main menu -> New Game / Load Save -> free roam with HUD -> pause (Resume / Save /
// Quit) -> save -> quit.
//
// Architecture: this is a thin host over the SAME free-roam code sr3_viewer's `playtest` runs (the static library
// sr3viewer_core). The window/device/swap chain are created once by the playtest's own setup; the front end plugs in
// through PlaytestHooks (tools/sr3_viewer/viewer_playtest_session.h):
//   afterWindow  -> window title + first cleared frame
//   beforeWorld  -> RunMainMenu() on the live window/HUD font, then New Game / Load Save is applied
//   pauseInput   -> pause menu logic (Esc opens it as before; arrows + Enter navigate; Save writes the .sr3s_pc)
//   pauseDraw    -> PauseMenuDraw() over the frozen scene
// The menus themselves live behind include/sr3game/frontend.h (Team C replaces src/game_frontend_default.cpp).
//
// Usage: sr3 [--cache=DIR] [--save-dir=DIR] [--hud-list=FILE] [--size W H] [--headless]
//            [--script-menu=new|load:PATH] [--frames N] [--capture-dir=DIR] [--activities] [<any playtest flag>]
//   no arguments needed to launch (cache: --cache, $CRREISH_GAME_CACHE_DIR, then the build-time default).
//   --script-menu: scripted boot with no input (implies the playtest's deterministic --no-input): main menu ->
//   New Game (or Load Save PATH) -> --frames N (default 300) free-roam frames -> open pause -> Save -> Quit to
//   Desktop. Exit code 0 on success.
#include "viewer_platform.h"
#include "viewer_playtest_session.h"

#include <psapi.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <thread>

#include <cstdio>
#include <exception>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "sr3game/frontend.h"
#include "sr3render/texture_upload.h"
#include "sr3saveflow/freeroam_flow.h"

namespace fs = std::filesystem;
using namespace sr3viewer;

#ifndef SR3_DEFAULT_CACHE_DIR
#define SR3_DEFAULT_CACHE_DIR ""
#endif
#ifndef SR3_SOURCE_DIR
#define SR3_SOURCE_DIR ""
#endif

namespace {

struct Config {
    std::string cache, saveDir, hudList, scriptMenu, captureDir;
    int roamFrames = 300;
    bool headless = false;
    bool script = false;
    bool roundtrip = false;     // --script-roundtrip: scripted Quit to Main Menu, then a scripted Load of that save
    bool toMenuAtEnd = false;   // internal: this round ends with Quit to Main Menu
    bool soak = false;          // --script-soak=SEED: chaos soak (viewer_playtest_soak.cpp); implies --script-menu=new
    unsigned soakSeed = 0;
    int soakFrames = 20000;     // --soak-frames=N: total free-roam frames across all rounds
    int soakHeartbeat = 300;    // --soak-heartbeat=N
    int soakRoundMax = 4000;    // --soak-round-max=N: longest single round (rounds are 1500..N frames, or exactly N when N <= 1500)
    int watchdogSec = 0;        // --watchdog-sec=N (soak default 180): abort when no frame completes for N seconds
    int soakRemaining = 0;
    int soakRound = 0;
    bool soakMidPause = false;  // this round also opens + resumes the pause menu once, mid-run
    std::vector<std::string> pass;  // forwarded verbatim to the playtest
};

void Log(const std::string& s) {
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------------------------------------------
// Live keyboard -> FrontendInput (edge-triggered). Polled EVERY frame (even in free roam, result discarded) so the
// edge state is always fresh when the pause menu opens.
// ---------------------------------------------------------------------------------------------------------------
class KeyPoller {
public:
    sr3game::FrontendInput Poll() {
        sr3game::FrontendInput in;
        in.up = Edge(0, down(VK_UP) || down('W'));
        in.down = Edge(1, down(VK_DOWN) || down('S'));
        in.left = Edge(2, down(VK_LEFT) || down('A'));
        in.right = Edge(3, down(VK_RIGHT) || down('D'));
        in.accept = Edge(4, down(VK_RETURN) || down(VK_SPACE));
        in.back = Edge(5, down(VK_ESCAPE) || down(VK_BACK));
        return in;
    }

private:
    static bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    bool Edge(int i, bool now) {
        const bool e = now && !prev_[i];
        prev_[i] = now;
        return e;
    }
    bool prev_[6] = {};
};

// ---------------------------------------------------------------------------------------------------------------
// FrontendCanvas over the playtest session's HUD pipeline + body font (the "existing quad/text renderer").
// ---------------------------------------------------------------------------------------------------------------
class SessionCanvas final : public sr3game::FrontendCanvas {
public:
    explicit SessionCanvas(PlaytestSession& s) : s_(s) {
        label_ = s_.hudEs->registerTypedVdoObjectForTesting("frontend_text", "text", 0, 0);
    }
    uint32_t Width() const override { return s_.swapChain_->width(); }
    uint32_t Height() const override { return s_.swapChain_->height(); }
    void FillRect(double x0, double y0, double x1, double y1, sr3game::Rgba c) override {
        const uint8_t rgba[4] = {c.r, c.g, c.b, c.a};
        s_.hudPipe_->rect(s_.device_->context(), x0, y0, x1, y1, rgba);
    }
    void DrawText(const std::string& text, double x, double y, sr3game::Rgba c) override {
        if (text.empty()) return;
        sr3luahost::VintTaggedValue vx, vy, vt;
        vx.kind = vy.kind = sr3luahost::VintTaggedValue::Kind::Number;
        vx.number = x;
        vy.number = y;
        vt.kind = sr3luahost::VintTaggedValue::Kind::String;
        vt.text = text;
        s_.hudEs->setVintProperty(label_, "offset", {vx, vy});
        s_.hudEs->setVintProperty(label_, "text_tag", {vt});
        const uint8_t rgba[4] = {c.r, c.g, c.b, c.a};
        drawHudLabel(*s_.hudEs, label_, *s_.hudFont_, *s_.hudPipe_, s_.device_->context(), rgba);
    }
    double TextWidth(const std::string& text) const override {
        const sr3font::Font& f = s_.hudFont_->font;
        double w = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            const uint32_t ch = static_cast<uint8_t>(text[i]);
            const uint32_t nx = i + 1 < text.size() ? static_cast<uint8_t>(text[i + 1]) : 0;
            w += sr3font::advanceForLayout(f, ch, nx);
        }
        return w;
    }
    void* NativeDevice() const override { return s_.device_->device(); }
    void* NativeContext() const override { return s_.device_->context(); }
    void* NativeWindow() const override { return s_.window_->nativeHandle(); }

private:
    PlaytestSession& s_;
    uint32_t label_ = 0;
};

enum class EndState { Desktop, MainMenu };

// Everything the hooks share for one session.
struct Host {
    Config cfg;
    EndState end = EndState::Desktop;
    bool menuExit = false;  // Quit chosen in the main menu
    KeyPoller keys;
    std::unique_ptr<SessionCanvas> canvas;
    sr3game::FrontendContext ctx;
    sr3game::PauseMenu* pause = nullptr;
    bool pauseWasOpen = false;
    int pauseOpenFrame = -1;
    bool midPauseActive = false, midPauseDone = false;
    std::string sessionSavePath;  // first save in a session picks NextSavePath; later saves overwrite it
    int menuCaptureFrame = 6;
    bool menuCaptured = false;
    ~Host() {
        if (pause) sr3game::PauseMenuDestroy(pause);
    }
};

bool CaptureBackBuffer(PlaytestSession& s, const std::string& path) {
    std::vector<uint8_t> px;
    std::string err;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (!s.swapChain_->capture(s.device_->context(), px, err) ||
        !sr3render::writePng(path, s.swapChain_->width(), s.swapChain_->height(), px, err)) {
        Log("capture  : FAILED " + path + " (" + err + ")");
        return false;
    }
    Log("captured : " + path);
    return true;
}

// Scripted pause-menu input, relative to the frame the pause opened on (the real input path is exercised: these
// FrontendInputs go through PauseMenuUpdate exactly like key presses).
sr3game::FrontendInput ScriptedPauseInput(int rel, bool toMenu) {
    sr3game::FrontendInput in;
    // Team C pause list (spec-tables-ui-controls Sec18.1): Save Game, Options, Quit, Quit to Desktop (no Resume).
    if (rel == 7) in.accept = true;   // Save
    if (rel == 12) in.down = true;    // -> Options
    if (rel == 13) in.down = true;    // -> Quit (to main menu)
    if (rel == 14 && !toMenu) in.down = true;  // -> Quit to Desktop (skipped when the script quits to the main menu)
    if (rel == 15) in.accept = true;  // quit
    if (rel == 17) in.left = true;    // Team C menus confirm the quit: No -> Yes
    if (rel == 18) in.accept = true;
    return in;
}

// Scripted main-menu input by menu frame.
sr3game::FrontendInput ScriptedMenuInput(uint64_t frame) {
    sr3game::FrontendInput in;
    // Team C menus (spec-frontend-flow Sec3.4/Sec4.1): top list (no saves) = Campaign, Quit Game;
    // Campaign = New Game, Load Game; then the difficulty list (cursor on Normal).
    if (frame == 10) in.accept = true;  // Campaign
    if (frame == 12) in.accept = true;  // New Game
    if (frame == 14) in.accept = true;  // Normal
    return in;
}

void InstallHooks(PlaytestSession& sess, Host& h) {
    sess.writePositionSidecar = true;
    PlaytestHooks& hk = sess.hooks;

    hk.afterWindow = [](PlaytestSession& s) {
        SetWindowTextA(static_cast<HWND>(s.window_->nativeHandle()), "Saints Row: The Third - recomp (first playable)");
        Log("flow     : window + device created; loading game data...");
    };

    hk.beforeWorld = [&h](PlaytestSession& s) -> int {
        Config& cfg = h.cfg;
        h.canvas = std::make_unique<SessionCanvas>(s);
        sr3game::FrontendContext& ctx = h.ctx;
        ctx.canvas = h.canvas.get();
        ctx.saveDir = cfg.saveDir;
        ctx.log = Log;
        ctx.listSaves = [&cfg]() { return sr3game::ListSaveFiles(cfg.saveDir); };
        ctx.beginFrame = [&s](sr3game::Rgba c) -> bool {
            g_lastTickMs.store(GetTickCount64());
            g_curStep.store("frontend menu frame");
            if (!s.window_->pumpMessages()) return false;
            std::string err;
            s.swapChain_->resize(s.window_->width(), s.window_->height(), err);
            s.swapChain_->bind(s.device_->context());
            s.swapChain_->clear(s.device_->context(), c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
            s.hudPipe_->begin(s.device_->context(), s.swapChain_->width(), s.swapChain_->height(), false);
            return true;
        };
        ctx.endFrame = [&s, &h]() {
            if (!h.cfg.captureDir.empty() && !h.menuCaptured && static_cast<int>(h.ctx.frameIndex) == h.menuCaptureFrame) {
                h.menuCaptured = CaptureBackBuffer(s, h.cfg.captureDir + "/main_menu.png");
            }
            s.swapChain_->present(true);
        };
        ctx.pollInput = [&h]() -> sr3game::FrontendInput {
            return h.cfg.script ? ScriptedMenuInput(h.ctx.frameIndex) : h.keys.Poll();
        };

        Log("flow     : boot complete - startup movies SKIPPED (not played)");
        sr3game::MainMenuResult r;
        const bool scriptLoad = h.cfg.scriptMenu.rfind("load:", 0) == 0;
        if (scriptLoad) {
            // Scripted LOAD bypasses the list UI (the file may live outside the save dir), but still renders menu frames.
            Log("flow     : --script-menu=" + h.cfg.scriptMenu + " - showing the main menu for 12 frames, then choosing Load Game");
            for (int i = 0; i < 12; ++i) {
                if (!ctx.beginFrame(sr3game::Rgba{16, 18, 30, 255})) return 0;
                ctx.canvas->DrawText("(scripted load)", 40, 40, sr3game::Rgba{255, 255, 255, 255});
                ctx.endFrame();
                ++ctx.frameIndex;
            }
            r.kind = sr3game::MainMenuResult::Kind::LoadSave;
            r.savePath = h.cfg.scriptMenu.substr(5);
        } else {
            r = sr3game::RunMainMenu(ctx);
        }

        using K = sr3game::MainMenuResult::Kind;
        if (r.kind == K::Quit) {
            Log("flow     : main menu -> Quit");
            h.menuExit = true;
            return 0;
        }
        if (r.kind == K::LoadSave) {
            Log("flow     : main menu -> Load Save " + r.savePath);
            s.loadSavePath = r.savePath;
            h.sessionSavePath = r.savePath;  // CHOSEN: a loaded game saves back over its own file
        } else {
            // spec-save-format.md A.3 / spec-frontend-menu-runtime.md Sec2: the new-game snapshot starts at 10:00:00
            // with the recommended $57.00 cash and no respawn crib (the player-start marker places the player).
            Log("flow     : main menu -> New Game (sr3saveflow new-game snapshot: 10:00, $57.00, player-start marker)");
            s.todFraction = 10.0 / 24.0;
            if (s.hudEs) s.hudEs->playerCash() = static_cast<double>(sr3saveflow::kRecommendedStartingCashRaw);
        }
        if (std::getenv("SR3_DIAG_SKIP_PAUSEDOC")) s.pauseDocLoadAttempted = true;
        h.pause = sr3game::PauseMenuCreate(ctx);
        Log("flow     : entering free roam");
        g_curStep.store("world load (after the front end)");
        g_lastTickMs.store(GetTickCount64());
        return -1;
    };

    hk.pauseInput = [&h](PlaytestSession& s, PlaytestFrame&) -> PauseRequest {
        const sr3game::FrontendInput live = h.cfg.script ? sr3game::FrontendInput{} : h.keys.Poll();
        if (!h.pause) return PauseRequest::None;
        // Scripted: open the pause menu after cfg.roamFrames frames of free roam.
        if (!s.pauseMenuOpen) {
            h.pauseWasOpen = false;
            if (h.cfg.script && s.frames == h.cfg.roamFrames) {
                Log("flow     : script: " + std::to_string(s.frames) + " free-roam frames done - opening the pause menu");
                h.pauseOpenFrame = s.frames;
                return PauseRequest::Open;
            }
            if (h.cfg.script && h.cfg.soakMidPause && !h.midPauseDone && s.frames == h.cfg.roamFrames / 2) {
                Log("flow     : soak: mid-run pause open (frame " + std::to_string(s.frames) + ")");
                h.midPauseDone = h.midPauseActive = true;
                h.pauseOpenFrame = s.frames;
                return PauseRequest::Open;
            }
            return PauseRequest::None;
        }
        if (!h.pauseWasOpen) {
            h.pauseWasOpen = true;
            if (h.pauseOpenFrame < 0) h.pauseOpenFrame = s.frames;
            sr3game::PauseMenuOpened(*h.pause);
            Log("flow     : pause menu opened (frame " + std::to_string(s.frames) + ")");
            return PauseRequest::None;  // ignore the opening frame's input (the Esc/Enter that opened it)
        }
        if (h.midPauseActive && s.frames - h.pauseOpenFrame >= 20) {
            h.midPauseActive = false;
            h.pauseOpenFrame = -1;
            Log("flow     : soak: mid-run pause -> Resume (frame " + std::to_string(s.frames) + ")");
            return PauseRequest::Close;
        }
        if (h.midPauseActive) return PauseRequest::None;
        const sr3game::FrontendInput in =
            h.cfg.script ? ScriptedPauseInput(s.frames - h.pauseOpenFrame, h.cfg.toMenuAtEnd) : live;
        switch (sr3game::PauseMenuUpdate(*h.pause, in)) {
            case sr3game::PauseMenuAction::Resume:
                Log("flow     : pause menu -> Resume");
                h.pauseOpenFrame = -1;
                return PauseRequest::Close;
            case sr3game::PauseMenuAction::Save: {
                if (h.sessionSavePath.empty()) h.sessionSavePath = sr3game::NextSavePath(h.cfg.saveDir);
                std::error_code ec;
                fs::create_directories(fs::path(h.sessionSavePath).parent_path(), ec);
                const bool ok = writeSaveTo(s, h.sessionSavePath);
                Log(std::string("flow     : pause menu -> Save Game: ") + (ok ? "saved " : "FAILED ") + h.sessionSavePath);
                sr3game::PauseMenuSetStatus(*h.pause, ok ? "Saved: " + fs::path(h.sessionSavePath).stem().string()
                                                         : std::string("Save failed"));
                return PauseRequest::None;
            }
            case sr3game::PauseMenuAction::QuitToMainMenu:
                Log("flow     : pause menu -> Quit to Main Menu");
                h.end = EndState::MainMenu;
                return PauseRequest::Quit;
            case sr3game::PauseMenuAction::QuitToDesktop:
                Log("flow     : pause menu -> Quit to Desktop");
                h.end = EndState::Desktop;
                return PauseRequest::Quit;
            case sr3game::PauseMenuAction::None:
                break;
        }
        return PauseRequest::None;
    };

    hk.pauseDraw = [&h](PlaytestSession&) {
        static bool once = true;
        if (once) { Log("flow     : pauseDraw first call"); }
        try {
            if (h.pause) sr3game::PauseMenuDraw(*h.pause, *h.canvas);
        } catch (const std::exception& e) {
            Log(std::string("flow     : pauseDraw EXCEPTION ") + e.what());
        }
        if (once) { Log("flow     : pauseDraw first call done"); once = false; }
    };
}

std::string FirstExisting(const std::vector<std::string>& cands) {
    std::error_code ec;
    for (const std::string& c : cands)
        if (!c.empty() && fs::exists(c, ec)) return c;
    return {};
}

void PrintMemory() {
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        std::printf("memory   : peak working set %.0f MB, peak pagefile (commit) %.0f MB\n",
                    pmc.PeakWorkingSetSize / 1048576.0, pmc.PeakPagefileUsage / 1048576.0);
    }
}

// Symbolised stack of a faulting context (needs the .pdb next to the exe; falls back to module+rva).
void PrintStackFromContext(CONTEXT* ctxIn) {
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    CONTEXT ctx = *ctxIn;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 24; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) || sf.AddrPC.Offset == 0)
            break;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        const bool hs = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym) != 0;
        const bool hl = SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ldisp, &line) != 0;
        std::printf("  #%02d %p %s+0x%llX %s:%lu\n", i, reinterpret_cast<void*>(sf.AddrPC.Offset), hs ? sym->Name : "?",
                    static_cast<unsigned long long>(disp), hl ? line.FileName : "", hl ? line.LineNumber : 0ul);
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    std::printf("CRASH    : unhandled SEH exception 0x%08lX at %p (step: %s)\n", ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress, g_curStep.load());
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        std::printf("           access violation: %s address %p\n", ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                    reinterpret_cast<void*>(ep->ExceptionRecord->ExceptionInformation[1]));
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(ep->ExceptionRecord->ExceptionAddress), &mod))
        std::printf("           module base %p, rva 0x%llX\n", static_cast<void*>(mod),
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) -
                                                    reinterpret_cast<uintptr_t>(mod)));
    PrintStackFromContext(ep->ContextRecord);
    PrintMemory();
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;  // exit code = the exception code
}

// Watchdog: the playtest frame loop stamps g_lastTickMs every frame (and the front end every menu frame). When it stops
// advancing for `sec` seconds the process is hung: print the step it is stuck in and terminate with exit code 4.
void StartWatchdog(int sec) {
    g_lastTickMs.store(GetTickCount64());
    std::thread([sec] {
        for (;;) {
            Sleep(2000);
            const unsigned long long now = GetTickCount64(), last = g_lastTickMs.load();
            if (now > last && now - last > static_cast<unsigned long long>(sec) * 1000ull) {
                std::printf("WATCHDOG : no frame completed for %llu s - HANG in step '%s'\n", (now - last) / 1000ull,
                            g_curStep.load());
                PrintMemory();
                std::fflush(stdout);
                TerminateProcess(GetCurrentProcess(), 4);
            }
        }
    }).detach();
}

}  // namespace

int main(int argc, char** argv) {
    SetUnhandledExceptionFilter(CrashFilter);
    if (std::getenv("SR3_TRACE_STEPS")) setvbuf(stdout, nullptr, _IONBF, 0);
    std::set_terminate([] {
        std::printf("TERMINATE: uncaught exception\n");
        try { throw; } catch (const std::exception& e) { std::printf("  what: %s\n", e.what()); } catch (...) {}
        std::fflush(stdout);
        std::abort();
    });
    if (std::getenv("SR3_UNBUFFERED")) std::setvbuf(stdout, nullptr, _IONBF, 0);
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--cache=", 0) == 0) cfg.cache = a.substr(8);
        else if (a.rfind("--save-dir=", 0) == 0) cfg.saveDir = a.substr(11);
        else if (a.rfind("--hud-list=", 0) == 0) cfg.hudList = a.substr(11);
        else if (a.rfind("--script-menu=", 0) == 0) cfg.scriptMenu = a.substr(14);
        else if (a.rfind("--capture-dir=", 0) == 0) cfg.captureDir = a.substr(14);
        else if (a == "--frames" && i + 1 < argc) cfg.roamFrames = std::atoi(argv[++i]);
        else if (a == "--headless") cfg.headless = true;
        else if (a == "--script-roundtrip") cfg.roundtrip = true;
        else if (a.rfind("--script-soak=", 0) == 0) { cfg.soak = true; cfg.soakSeed = static_cast<unsigned>(std::strtoul(a.c_str() + 14, nullptr, 10)); }
        else if (a.rfind("--soak-frames=", 0) == 0) cfg.soakFrames = std::atoi(a.c_str() + 14);
        else if (a.rfind("--soak-heartbeat=", 0) == 0) cfg.soakHeartbeat = std::atoi(a.c_str() + 17);
        else if (a.rfind("--soak-round-max=", 0) == 0) cfg.soakRoundMax = std::atoi(a.c_str() + 17);
        else if (a.rfind("--watchdog-sec=", 0) == 0) cfg.watchdogSec = std::atoi(a.c_str() + 15);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: sr3 [--cache=DIR] [--save-dir=DIR] [--hud-list=FILE] [--size W H] [--headless]\n"
                        "           [--script-menu=new|load:PATH] [--frames N] [--capture-dir=DIR] [<playtest flags>]\n");
            return 0;
        } else cfg.pass.push_back(a);
    }
    std::mt19937 soakRng(cfg.soakSeed * 7919u + 17u);
    if (cfg.soak) {
        std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep the last log lines when the process dies
        if (cfg.scriptMenu.empty()) cfg.scriptMenu = "new";
        cfg.soakRemaining = cfg.soakFrames;
        if (cfg.watchdogSec <= 0) cfg.watchdogSec = 180;
    }
    cfg.script = !cfg.scriptMenu.empty();
    if (cfg.watchdogSec > 0) StartWatchdog(cfg.watchdogSec);
    // One soak round = one session: roam a random 1500-4000 frames (less when the budget is nearly spent), pause, save,
    // Quit to Main Menu; the next round is a Load of that save (the Continue flow). The last round quits to Desktop.
    auto planSoakRound = [&cfg, &soakRng]() {
        const int want = cfg.soakRoundMax <= 1500
                             ? cfg.soakRoundMax
                             : 1500 + static_cast<int>(soakRng() % static_cast<unsigned>(cfg.soakRoundMax - 1500 + 1));
        cfg.roamFrames = (std::min)(want, cfg.soakRemaining);
        if (cfg.soakRemaining - cfg.roamFrames < 600) cfg.roamFrames = cfg.soakRemaining;  // no tiny tail round
        cfg.soakRemaining -= cfg.roamFrames;
        cfg.toMenuAtEnd = cfg.soakRemaining > 0;
        cfg.soakMidPause = cfg.roamFrames >= 800;
    };
    if (cfg.soak) planSoakRound();
    if (cfg.script && cfg.scriptMenu != "new" && cfg.scriptMenu.rfind("load:", 0) != 0) {
        std::fprintf(stderr, "sr3: --script-menu must be 'new' or 'load:PATH'\n");
        return 2;
    }
    if (cfg.cache.empty()) {
        const char* e = std::getenv("CRREISH_GAME_CACHE_DIR");
        cfg.cache = e && *e ? e : SR3_DEFAULT_CACHE_DIR;  // CHOSEN default = the build-time CRREISH_GAME_CACHE_DIR
    }
    if (cfg.saveDir.empty()) cfg.saveDir = sr3game::DefaultSaveDirectory();
    if (cfg.hudList.empty()) {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        cfg.hudList = FirstExisting({(fs::path(exe).parent_path() / "lua_all_registered_1490_tagged.txt").string(),
                                     (fs::path(SR3_SOURCE_DIR) / "tools" / "lua_all_registered_1490_tagged.txt").string()});
    }

    const fs::path cache = cfg.cache;
    const std::vector<std::string> archives = {"characters.vpp_pc", "misc_tables.vpp_pc", "sr3_city_0.vpp_pc",
                                               "sr3_city_1.vpp_pc", "misc.vpp_pc",        "interface.vpp_pc"};
    for (const std::string& a : archives) {
        if (!fs::exists(cache / a)) {
            std::fprintf(stderr, "sr3: game data not found: %s\n     (pass --cache=DIR, or set CRREISH_GAME_CACHE_DIR)\n",
                         (cache / a).string().c_str());
            return 2;
        }
    }
    if (cfg.hudList.empty() || !fs::exists(cfg.hudList)) {
        std::fprintf(stderr, "sr3: HUD registration list (lua_all_registered_1490_tagged.txt) not found; pass --hud-list=FILE\n");
        return 2;
    }

    std::printf("sr3      : FIRST PLAYABLE - cache %s, saves %s%s\n", cfg.cache.c_str(), cfg.saveDir.c_str(),
                cfg.script ? (" , scripted (" + cfg.scriptMenu + ")").c_str() : "");
    Log("flow     : startup movies SKIPPED (intro videos are not played; logged only)");

    int rc = 0;
    if (!cfg.soak) cfg.toMenuAtEnd = cfg.roundtrip && cfg.script && cfg.scriptMenu == "new";
    for (int round = 0;; ++round) {
        Host host;
        host.cfg = cfg;
        std::vector<std::string> args = {"sr3", "playtest"};
        for (const std::string& a : archives) args.push_back((cache / a).string());
        args.insert(args.end(), {"--hud", cfg.hudList, (cache / "misc.vpp_pc").string()});
        if (cfg.headless) args.push_back("--headless");
        if (cfg.script) args.push_back("--no-input");
        // --no-input alone caps the playtest at 300 frames (its smoke default), the frame the script opens the pause
        // menu on; give the scripted pause sequence room (Team C, first-playable smoke).
        if (cfg.script) args.insert(args.end(), {"--frames", std::to_string(cfg.roamFrames + 200)});
        if (cfg.script && !cfg.captureDir.empty()) {
            const int r = cfg.roamFrames;
            std::string frames = (r >= 1 ? std::to_string(r - 1) + "," : std::string()) + std::to_string(r + 3) + "," +
                                 std::to_string(r + 10);
            args.insert(args.end(), {"--capture", cfg.captureDir + "/game", "--capture-frames", frames});
        }
        // Known glitch fix: the playtest's --no-input fallback walks forward --script-forward-frames (default 200) and then
        // turns --script-turn-frames (60) counted from frame 0 OF EVERY SESSION, so a scripted Load (a fresh session) made the
        // just-restored player auto-walk (locomotion 2, heading 0). Nothing is wrong in the save/restore itself; a loaded game
        // must start standing still, so the scripted walk is switched off for Load rounds (explicit flags still override).
        if (cfg.script && cfg.scriptMenu.rfind("load:", 0) == 0)
            args.insert(args.end(), {"--script-forward-frames", "0", "--script-turn-frames", "0"});
        if (cfg.soak) {
            args.insert(args.end(), {"--script-soak=" + std::to_string(cfg.soakSeed), "--soak-round=" + std::to_string(cfg.soakRound),
                                     "--soak-heartbeat=" + std::to_string(cfg.soakHeartbeat),
                                     "--call-homie=__soak__@2000000000"});  // loads the homie roster; the dummy dial never fires
        }
        args.insert(args.end(), cfg.pass.begin(), cfg.pass.end());
        std::vector<char*> av;
        for (std::string& s : args) av.push_back(s.data());

        auto sess = std::make_unique<PlaytestSession>();
        InstallHooks(*sess, host);
        rc = runPlaytestSession(*sess, static_cast<int>(av.size()), av.data());
        sess.reset();
        {
            // The playtest's texture holders are raw SRV pointers nobody releases; with the session gone, drop them now or
            // every Load leaks all its textures and keeps the D3D device alive (~250 MB private bytes per round).
            PROCESS_MEMORY_COUNTERS_EX before{};
            before.cb = sizeof(before);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before), sizeof(before));
            const size_t freed = sr3render::releaseTrackedSrvs();
            PROCESS_MEMORY_COUNTERS_EX after{};
            after.cb = sizeof(after);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after), sizeof(after));
            Log("flow     : session teardown: released " + std::to_string(freed) + " leaked texture views; private bytes " +
                std::to_string(static_cast<long long>(before.PrivateUsage >> 20)) + " MB -> " +
                std::to_string(static_cast<long long>(after.PrivateUsage >> 20)) + " MB");
        }
        // Destroying the previous window posted WM_QUIT (PostQuitMessage in its WM_DESTROY); drop it so the next round's
        // window does not see it and close at once.
        { MSG quitMsg; while (PeekMessageW(&quitMsg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {} }
        if (rc != 0 || host.menuExit || host.end != EndState::MainMenu) break;
        Log("flow     : returning to the main menu");
        if (cfg.soak && cfg.toMenuAtEnd && !host.sessionSavePath.empty()) {
            cfg.scriptMenu = "load:" + host.sessionSavePath;
            ++cfg.soakRound;
            planSoakRound();
            Log("flow     : soak: round " + std::to_string(cfg.soakRound) + " - loading " + host.sessionSavePath + ", " +
                std::to_string(cfg.roamFrames) + " frames, " + std::to_string(cfg.soakRemaining) + " remaining after it");
            continue;
        }
        if (cfg.script && cfg.toMenuAtEnd && !host.sessionSavePath.empty()) {
            // --script-roundtrip: second round = Continue/Load of the save just written, then Quit to Desktop.
            cfg.scriptMenu = "load:" + host.sessionSavePath;
            cfg.roamFrames = 30;
            cfg.toMenuAtEnd = false;
            if (!cfg.captureDir.empty()) cfg.captureDir += "/after_load";
            Log("flow     : script: round 2 - loading " + host.sessionSavePath + " from the main menu");
            continue;
        }
        if (cfg.script) break;  // a scripted run never loops otherwise
    }
    PrintMemory();
    Log(rc == 0 ? "flow     : exit 0" : "flow     : exit with error");
    return rc;
}
