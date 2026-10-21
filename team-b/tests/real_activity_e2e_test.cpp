// Real-data end-to-end activity runs (Team C, HANDOFF-C.md section 8). For each activity
// below it launches the freshly built sr3_viewer.exe playtest headless with
// `--activity <instance> --auto-activity` against the real game cache: the harness walks the
// player into the instance's start point, the run starts, the harness drives it (Snatch:
// reach the hostages, lead them to the drop-off; Insurance Fraud: scripted car strikes), and
// the test checks the logged result and the 6.1 reward grant. The harness stand-ins are
// CHOSEN (include/sr3activity/harness.h, viewer_playtest_activity.cpp); what this test
// guards is the wiring from start point to reward on real instance data.
//
// Exits 77 (ctest SKIPPED) when CRREISH_GAME_CACHE_DIR has no game cache, the same
// convention as real_playtest_smoke_test. One viewer at a time (RESOURCE_LOCK
// sr3_gpu_device in CMakeLists.txt).
//
// Usage: real_activity_e2e_tests --bin-dir <dir of sr3_viewer.exe> --source-dir <repo root>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW, the same shape as real_playtest_smoke_test's runIn().
int runIn(const std::string& cwd, const std::string& command) {
    std::string fullCmd = "cmd.exe /c \"" + command + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdLineBuf(fullCmd.begin(), fullCmd.end());
    cmdLineBuf.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, cmdLineBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             cwd.c_str(), &si, &pi);
    if (!ok) {
        std::printf("FATAL: CreateProcess failed for '%s' (GetLastError=%lu)\n", fullCmd.c_str(),
                    static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

struct Case {
    const char* instance;
    int frames;  // upper bound; the harness ends the run 15 frames after the result
    std::vector<std::string> mustContain;
};

}  // namespace

int main(int argc, char** argv) {
    std::string binDir, sourceDir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bin-dir" && i + 1 < argc) binDir = argv[++i];
        else if (arg == "--source-dir" && i + 1 < argc) sourceDir = argv[++i];
    }
    if (binDir.empty() || sourceDir.empty()) {
        std::printf("FATAL: --bin-dir and --source-dir are required\n");
        return 1;
    }
    const fs::path cache = CRREISH_GAME_CACHE_DIR;
    const std::vector<fs::path> archives = {cache / "characters.vpp_pc", cache / "misc_tables.vpp_pc",
                                            cache / "sr3_city_0.vpp_pc", cache / "sr3_city_1.vpp_pc",
                                            cache / "misc.vpp_pc", cache / "interface.vpp_pc"};
    for (const fs::path& a : archives) {
        if (!fs::exists(a)) {
            std::printf("SKIPPED: real game cache not found under '%s' (set CRREISH_GAME_CACHE_DIR)\n",
                        cache.string().c_str());
            return 77;
        }
    }
    const fs::path hudList = fs::path(sourceDir) / "tools" / "lua_all_registered_1490_tagged.txt";
    if (!fs::exists(hudList)) {
        std::printf("FATAL: HUD registration list not found: %s\n", hudList.string().c_str());
        return 1;
    }

    const std::vector<Case> cases = {
        {"_a_sn_ne_01", 5000,
         {"activity : _a_sn_ne_01 armed: start point 'local_start'",
          "activity : entered the start point of _a_sn_ne_01 ('local_start') - starting",
          "activity : _a_sn_ne_01 started: deliver 4 in 320 s", "hostage(s) joined",
          "activity : delivered ", "activity : _a_sn_ne_01 SUCCEEDED", "activity : unlock Activity_Unlock_SN",
          "activity : rewards $2000.00, respect now 200"}},
        {"_a_if_dt_01", 7000,
         {"activity : _a_if_dt_01 armed: start point 'local_start'",
          "activity : entered the start point of _a_if_dt_01 ('local_start') - starting",
          "activity : _a_if_dt_01 started: goal $80000 in 210 s", "activity : harness car strike ",
          "activity : _a_if_dt_01 SUCCEEDED", "activity : unlock Activity_Unlock_IF",
          "activity : rewards $2000.00, respect now 200"}},
    };

    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    bool allOk = true;
    for (const Case& c : cases) {
        const fs::path out = fs::path(binDir) / ("activity_e2e_" + std::string(c.instance) + ".txt");
        std::error_code ec;
        fs::remove(out, ec);
        std::string cmd = exe + " playtest";
        for (const fs::path& a : archives) cmd += " \"" + a.string() + "\"";
        cmd += " --headless --no-input --frames " + std::to_string(c.frames) + " --activity " + c.instance +
               " --auto-activity --hud \"" + hudList.string() + "\" \"" + (cache / "misc.vpp_pc").string() + "\" > \"" +
               out.string() + "\" 2>&1";
        const int rc = runIn(binDir, cmd);
        bool ok = rc == 0;
        if (rc != 0) std::printf("FAIL %s: sr3_viewer.exe returned %d (see %s)\n", c.instance, rc, out.string().c_str());
        std::vector<std::string> lines;
        {
            std::ifstream in(out);
            std::string line;
            while (std::getline(in, line)) lines.push_back(line);
        }
        for (const std::string& want : c.mustContain) {
            bool found = false;
            for (const std::string& l : lines) found = found || l.find(want) != std::string::npos;
            if (!found) {
                std::printf("FAIL %s: stdout never contained '%s' (see %s)\n", c.instance, want.c_str(),
                            out.string().c_str());
                ok = false;
            }
        }
        for (const std::string& l : lines) {
            if (l.find("activity : ") == 0 && l.find(" FAILED") != std::string::npos) {
                std::printf("FAIL %s: %s\n", c.instance, l.c_str());
                ok = false;
            }
            if (l.find("activity : ") == 0 &&
                (l.find("SUCCEEDED") != std::string::npos || l.find("rewards") != std::string::npos))
                std::printf("%s: %s\n", c.instance, l.c_str());
        }
        std::printf("%s %s\n", c.instance, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }
    std::printf("\nREAL_ACTIVITY_E2E_TEST %s\n", allOk ? "PASS" : "FAIL");
    return allOk ? 0 : 1;
}
