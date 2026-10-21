// Short stability soak for sr3.exe (first-playable stability pass). Runs the chaos driver (--script-soak=SEED, see
// tools/sr3_viewer/viewer_playtest_soak.cpp) headless for 3000 frames in 1000-frame rounds, so the run includes two
// Save -> Quit to Main Menu -> Load transitions (fresh playtest session each), and checks:
//   * exit code 0 (no crash: the in-process SEH filter prints "CRASH" and the exit code is the exception code),
//   * no "CRASH", "WATCHDOG" or "MEMORY LIMIT" line in the log,
//   * the heartbeat log reached the full frame budget (3 rounds), and private bytes at the last heartbeat stay under
//     2.5 GB (a coarse leak/regression tripwire; the working set is not used: it includes the mapped game archives).
// Regression for: police models cached in a function-level static dangled after the first Load (stepRender crash).
// Exits 77 (SKIPPED) when the real game cache is absent. --bin-dir = directory of the freshly built sr3.exe.
#include <windows.h>

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
    const fs::path work = fs::path(binDir) / "first_playable_soak";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    const fs::path out = work / "soak.txt";
    const std::string exe = "\"" + (fs::path(binDir) / "sr3.exe").string() + "\"";
    const std::string cmd = exe + " --script-soak=7 --soak-frames=3000 --soak-round-max=1000 --soak-heartbeat=250 --headless "
                                  "--watchdog-sec=240 --save-dir=\"" + (work / "saves").string() + "\" > \"" + out.string() +
                            "\" 2>&1";
    const int rc = runIn(binDir, cmd);
    const std::string log = readAll(out);
    bool ok = true;
    if (rc != 0) { std::printf("FAIL: exit code %d (expected 0)\n", rc); ok = false; }
    for (const char* bad : {"CRASH    :", "WATCHDOG :", "MEMORY LIMIT", "TERMINATE:"})
        if (log.find(bad) != std::string::npos) { std::printf("FAIL: log contains '%s'\n", bad); ok = false; }
    int hbs = 0, maxRound = -1;
    double lastPriv = 0;
    for (size_t p = 0; (p = log.find("[soak] hb round=", p)) != std::string::npos; p += 10) {
        ++hbs;
        int round = -1, frame = -1;
        std::sscanf(log.c_str() + p, "[soak] hb round=%d frame=%d", &round, &frame);
        if (round > maxRound) maxRound = round;
        const size_t q = log.find("priv=", p);
        const size_t eol = log.find('\n', p);
        if (q != std::string::npos && q < eol) lastPriv = std::atof(log.c_str() + q + 5);
    }
    std::printf("soak     : rc=%d heartbeats=%d rounds=%d last private bytes=%.0f MB\n", rc, hbs, maxRound + 1, lastPriv);
    if (maxRound < 2) { std::printf("FAIL: expected 3 rounds (2 Loads), saw %d\n", maxRound + 1); ok = false; }
    if (hbs < 10) { std::printf("FAIL: too few heartbeats (%d)\n", hbs); ok = false; }
    if (lastPriv > 2500.0) { std::printf("FAIL: private bytes %.0f MB > 2500 MB\n", lastPriv); ok = false; }
    if (log.find("flow     : exit 0") == std::string::npos) { std::printf("FAIL: no 'flow     : exit 0'\n"); ok = false; }
    if (!ok) std::printf("(log: %s)\n", out.string().c_str());
    std::printf("\nREAL_FIRST_PLAYABLE_SOAK_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
