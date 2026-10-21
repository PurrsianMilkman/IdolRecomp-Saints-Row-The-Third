// Real-data smoke test for sr3.exe (FIRST PLAYABLE). Two scripted boots, headless, no input:
//   run 1: --script-menu=new --frames 300  -> main menu -> New Game -> 300 free-roam frames -> pause -> Save Game ->
//          Quit to Desktop. Checks exit code 0, the flow log lines, a 108776-byte save file and its .sr3pos sidecar.
//   run 2: --script-menu=load:<that file> --frames 5 -> Load Save puts the player back at the saved x/z (the load log
//          prints the placement) and the run again exits 0.
// Exits 77 (SKIPPED) when the real game cache is absent. --bin-dir = directory of the freshly built sr3.exe.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

int runIn(const std::string& cwd, const std::string& command) {
    std::string fullCmd = "cmd.exe /c \"" + command + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(fullCmd.begin(), fullCmd.end());
    buf.push_back('\0');
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi)) {
        std::printf("FATAL: CreateProcess failed (%lu)\n", static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool expect(bool cond, const char* what, bool& ok) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ok = false;
    }
    return cond;
}

}  // namespace

int main(int argc, char** argv) {
    std::string binDir;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--bin-dir" && i + 1 < argc) binDir = argv[++i];
    if (binDir.empty()) {
        std::printf("FATAL: --bin-dir not given\n");
        return 1;
    }
    const fs::path cache = CRREISH_GAME_CACHE_DIR;
    for (const char* a : {"characters.vpp_pc", "misc_tables.vpp_pc", "sr3_city_0.vpp_pc", "sr3_city_1.vpp_pc",
                          "misc.vpp_pc", "interface.vpp_pc"}) {
        if (!fs::exists(cache / a)) {
            std::printf("SKIPPED: real game cache not found under '%s'\n", cache.string().c_str());
            return 77;
        }
    }

    const fs::path work = fs::path(binDir) / "first_playable_smoke";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    const fs::path saveDir = work / "saves";
    const fs::path out1 = work / "run1_new.txt", out2 = work / "run2_load.txt";
    const std::string exe = "\"" + (fs::path(binDir) / "sr3.exe").string() + "\"";
    bool ok = true;

    // ---- run 1: New Game ----
    const std::string cmd1 = exe + " --script-menu=new --frames 300 --headless --save-dir=\"" + saveDir.string() +
                             "\" --capture-dir=\"" + (work / "caps").string() + "\" > \"" + out1.string() + "\" 2>&1";
    const int rc1 = runIn(binDir, cmd1);
    expect(rc1 == 0, "run 1 exit code is 0", ok);
    const std::string log1 = readAll(out1);
    for (const char* want : {"startup movies SKIPPED", "main menu result = NewGame", "main menu -> New Game",
                             "entering free roam", "pause menu opened", "pause menu -> Save Game: saved",
                             "pause menu -> Quit to Desktop", "flow     : exit 0"}) {
        if (log1.find(want) == std::string::npos) {
            std::printf("FAIL: run 1 log lacks '%s' (see %s)\n", want, out1.string().c_str());
            ok = false;
        }
    }
    const fs::path save = saveDir / "sr3save_01.sr3s_pc";
    const bool saved = fs::exists(save) && fs::file_size(save) == 108776;
    expect(saved, "run 1 wrote sr3save_01.sr3s_pc (108776 bytes)", ok);
    const fs::path pos = saveDir / "sr3save_01.sr3s_pc.sr3pos";
    expect(fs::exists(pos), "run 1 wrote the .sr3pos sidecar", ok);
    for (const char* png : {"main_menu.png", "game_frame299.png", "game_frame303.png", "game_frame310.png"})
        expect(fs::exists(work / "caps" / png) && fs::file_size(work / "caps" / png) > 1024, png, ok);
    std::printf("run 1    : rc=%d save=%s\n", rc1, saved ? "ok" : "MISSING");

    // ---- run 2: Load Save puts the player back at the saved x/z ----
    if (saved && fs::exists(pos)) {
        double px = 0, py = 0, pz = 0;
        {
            std::ifstream pin(pos);
            pin >> px >> py >> pz;
        }
        const std::string cmd2 = exe + " --script-menu=load:\"" + save.string() + "\" --frames 60 --headless --save-dir=\"" +
                                 saveDir.string() + "\" > \"" + out2.string() + "\" 2>&1";
        const int rc2 = runIn(binDir, cmd2);
        expect(rc2 == 0, "run 2 exit code is 0", ok);
        const std::string log2 = readAll(out2);
        char want[128];
        std::snprintf(want, sizeof(want), "placed at saved position (.sr3pos sidecar) (%.1f, %.1f)", px, pz);
        if (log2.find(want) == std::string::npos) {
            std::printf("FAIL: run 2 log lacks '%s' (see %s)\n", want, out2.string().c_str());
            ok = false;
        }
        expect(log2.find("flow     : exit 0") != std::string::npos, "run 2 reached 'exit 0'", ok);
        // Regression (auto-walk after Load): the loaded player must stand where the save put it. 60 frames of the playtest's
        // scripted forward walk would move it several metres; allow 1.5 m (ground snap / settle).
        const size_t fp = log2.find("final player pos (");
        double fx = 0, fy = 0, fz = 0;
        if (expect(fp != std::string::npos && std::sscanf(log2.c_str() + fp, "final player pos (%lf %lf %lf)", &fx, &fy, &fz) == 3,
                   "run 2 prints the final player pos", ok)) {
            const double d = std::hypot(fx - px, fz - pz);
            std::printf("run 2    : final player pos (%.2f %.2f %.2f), %.2f m from the saved x/z\n", fx, fy, fz, d);
            expect(d < 1.5, "run 2: the loaded player did not auto-walk away from the saved position", ok);
        }
        std::printf("run 2    : rc=%d saved position (%.2f, %.2f, %.2f)\n", rc2, px, py, pz);
    }

    std::printf("\nREAL_FIRST_PLAYABLE_SMOKE_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
