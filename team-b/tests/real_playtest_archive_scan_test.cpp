// Real-data integration test for `sr3_viewer.cpp`'s `playtest` command's
// cache-directory archive-scan fallback (fix/m05-zscene-m13-systemlib,
// 2026-10-12): `expandArchivePathsWithCacheDirScan()`, called from
// runPlaytest() right after parsing the caller's own `cityArchivePaths`.
//
// Root cause this task fixed (see tools/sr3_viewer.cpp's own doc comments
// above expandArchivePathsWithCacheDirScan() and
// missionzoneobj::installZsceneTableFromArchives()): this command's
// `playtest --mission=<stem>` path only ever looked at archives the CALLER
// explicitly named on argv, unlike `tools/lua_host_run.cpp`'s own mission-
// drive loop, which scans the whole cache directory for everything it
// needs. Two real, previously-OPEN mission blockers traced directly to
// this one gap:
//   - m05: `zscene_is_loaded('05_in')` refused OPEN because the real
//     zscene/cutscene scene table (cutscene_tables.vpp_pc) was never
//     installed - EngineState::zsceneIsLoaded()'s own kind-gate
//     (`if (!r.loadable) return true;`, Sec14.23) was already correct, it
//     just had no real data to answer from (see HANDOFF.md 2026-10-12).
//   - m13: `_GetAnyGlobalSilent is not a function` when thread_new runs,
//     because system_lib.lua (real home: patch_compressed.vpp_pc) was
//     never found among the caller-supplied archives and so never
//     preloaded into the gameplay state (spec-lua-bindings.md Sec16.2).
//
// This test proves BOTH are now resolved WITHOUT the caller having to name
// cutscene_tables.vpp_pc or patch_compressed.vpp_pc on the command line -
// only the same five archives tests/real_playtest_mission_test.cpp already
// passes (characters/misc_tables/city/misc/interface), exactly the
// "a user doesn't need to guess and manually pass the right archive"
// property this task's brief asks for:
//   (A) m05: the real zscene/cutscene table load line reports a non-empty
//       "installed from ...cutscene_tables.vpp_pc" summary, and the
//       mission's own per-frame drive advances past `fade_out_block` AND
//       past `zscene_is_loaded` without erroring on it (the OLD failure
//       text from HANDOFF.md's 2026-10-12 "fade_out_block was a timeout
//       artifact" entry must never appear).
//   (B) m13: both "preloaded system_lib.lua" and "preloaded game_lib.lua"
//       lines are printed (proving the scan found patch_compressed.vpp_pc
//       on its own), and the OLD `_GetAnyGlobalSilent is not a function`
//       failure text never appears.
//
// Same CreateProcess+CREATE_NO_WINDOW/SKIP_RETURN_CODE 77 convention every
// other real_*_test in this project uses (copied, not shared, from
// tests/real_playtest_mission_test.cpp - see that file's own header
// comment for the full account of why each real_*_test keeps its own
// copy). Nothing from the game is copied into the repository; this test
// only reads the real install at run time, same as sr3_viewer itself.
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
    std::vector<char> cmdLineBuf(fullCmd.begin(), fullCmd.end());
    cmdLineBuf.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, cmdLineBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             cwd.c_str(), &si, &pi);
    if (!ok) {
        std::printf("FATAL: CreateProcess failed for '%s' (cwd '%s', GetLastError=%lu)\n", fullCmd.c_str(),
                   cwd.c_str(), static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

bool fileContainsLinePrefix(const fs::path& path, const std::string& prefix) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

bool fileContainsSubstring(const fs::path& path, const std::string& needle) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::string binDir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bin-dir" && i + 1 < argc) binDir = argv[++i];
    }
    if (binDir.empty()) {
        std::printf("FATAL: --bin-dir not given (expected $<TARGET_FILE_DIR:sr3_viewer> from CMakeLists.txt)\n");
        return 1;
    }

    const fs::path dataRoot = CRREISH_GAME_CACHE_DIR;
    const fs::path characters = dataRoot / "characters.vpp_pc";
    const fs::path miscTables = dataRoot / "misc_tables.vpp_pc";
    const fs::path cityArchive = dataRoot / "sr3_city_0.vpp_pc";
    const fs::path miscArchive = dataRoot / "misc.vpp_pc";
    const fs::path interfaceArchive = dataRoot / "interface.vpp_pc";
    // Deliberately NOT passed on the command line below - cutscene_tables.vpp_pc/
    // patch_compressed.vpp_pc must be found by the cache-directory scan alone,
    // that is the whole point of this test.
    const fs::path cutsceneTables = dataRoot / "cutscene_tables.vpp_pc";
    const fs::path patchCompressed = dataRoot / "patch_compressed.vpp_pc";
    const fs::path registrationList = fs::path(CRREISH_TOOLS_DIR) / "lua_all_registered_1490_tagged.txt";
    if (!fs::exists(characters) || !fs::exists(miscTables) || !fs::exists(cityArchive) ||
        !fs::exists(miscArchive) || !fs::exists(interfaceArchive)) {
        std::printf("SKIPPED: real game cache not found under '%s' (set CRREISH_GAME_CACHE_DIR)\n",
                   dataRoot.string().c_str());
        return 77;
    }
    if (!fs::exists(cutsceneTables) || !fs::exists(patchCompressed)) {
        // Honest skip, not a fabricated pass: without these two archives
        // actually present in the cache directory, this test cannot tell a
        // working scan apart from one that silently found nothing.
        std::printf("SKIPPED: cutscene_tables.vpp_pc/patch_compressed.vpp_pc not found under '%s' - cannot "
                   "exercise the cache-directory scan this test checks\n",
                   dataRoot.string().c_str());
        return 77;
    }
    if (!fs::exists(registrationList)) {
        std::printf("SKIPPED: registration list not found at '%s'\n", registrationList.string().c_str());
        return 77;
    }

    const fs::path workDir = fs::path(binDir);
    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    const std::string commonArgs = " playtest \"" + characters.string() + "\" \"" + miscTables.string() + "\" \"" +
                                   cityArchive.string() + "\" \"" + miscArchive.string() + "\" \"" +
                                   interfaceArchive.string() + "\"" +
                                   " --headless --no-input --frames 30 --mission-registration-list \"" +
                                   registrationList.string() + "\" --mission-timeout=180";

    bool ok = true;

    // --- (A) m05: zscene/cutscene table scan fix -----------------------
    {
        const fs::path stdoutPath = workDir / "playtest_archive_scan_m05_stdout.txt";
        std::error_code ec;
        fs::remove(stdoutPath, ec);
        const std::string cmd = exe + commonArgs + " --mission=m05" + " > \"" + stdoutPath.string() + "\" 2>&1";
        const int rc = runIn(workDir.string(), cmd);
        if (rc != 0) {
            std::printf("FAIL(A): sr3_viewer.exe playtest --mission=m05 returned exit code %d, expected 0 (see %s)\n",
                       rc, stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsSubstring(stdoutPath, "real zscene/cutscene table load: installed from")) {
            std::printf("FAIL(A): the cache-directory scan never found/installed cutscene_tables.vpp_pc for "
                       "--mission=m05 (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (fileContainsSubstring(stdoutPath, "zscene_is_loaded")) {
            std::printf("FAIL(A): --mission=m05 still hits the OLD zscene_is_loaded OPEN-state failure (the "
                       "cache-directory scan should have resolved it - see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : m05_start() called, suspended")) {
            std::printf("FAIL(A): stdout never printed the real suspended-_start line for m05 (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (ok) std::printf("sub-test A (m05 zscene/cutscene table cache-dir scan): PASS\n");
    }

    // --- (B) m13: system_lib.lua archive-location scan fix --------------
    {
        const fs::path stdoutPath = workDir / "playtest_archive_scan_m13_stdout.txt";
        std::error_code ec;
        fs::remove(stdoutPath, ec);
        const std::string cmd = exe + commonArgs + " --mission=m13" + " > \"" + stdoutPath.string() + "\" 2>&1";
        const int rc = runIn(workDir.string(), cmd);
        bool okB = true;
        if (rc != 0) {
            std::printf("FAIL(B): sr3_viewer.exe playtest --mission=m13 returned exit code %d, expected 0 (see %s)\n",
                       rc, stdoutPath.string().c_str());
            okB = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : preloaded system_lib.lua") ||
            !fileContainsLinePrefix(stdoutPath, "mission  : preloaded game_lib.lua")) {
            std::printf("FAIL(B): the cache-directory scan never found/preloaded system_lib.lua/game_lib.lua for "
                       "--mission=m13 without naming patch_compressed.vpp_pc explicitly (see %s)\n",
                       stdoutPath.string().c_str());
            okB = false;
        }
        if (fileContainsSubstring(stdoutPath, "_GetAnyGlobalSilent is not a function")) {
            std::printf("FAIL(B): --mission=m13 still hits the OLD _GetAnyGlobalSilent OPEN-state failure (see "
                       "%s)\n",
                       stdoutPath.string().c_str());
            okB = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : m13_start() called, suspended")) {
            std::printf("FAIL(B): stdout never printed the real suspended-_start line for m13 (see %s)\n",
                       stdoutPath.string().c_str());
            okB = false;
        }
        if (okB) std::printf("sub-test B (m13 system_lib.lua cache-dir scan): PASS\n");
        ok = ok && okB;
    }

    return ok ? 0 : 1;
}
