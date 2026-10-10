// Real-data integration test for sr3_viewer's `playtest` command's
// MISSION-HOST PHASE 1 `--mission=<stem>` debug flag (2026-10-09, branch
// feature/mission-host-phase1-debug-flag) AND PHASE 2's own live position
// bridge (2026-10-09, branch feature/mission-host-phase2-live-bridge,
// results/design_mission_host_in_engine_loop_20261009.md §3(c)) - the new
// sub-test C below checks the one-time "live position bridge active" line
// tools/sr3_viewer.cpp's own runPlaytest() now prints on frame 0 whenever
// --mission is given, proving EngineState::bridgeLiveCharacterPosition()
// really is called from the live --mission path (not just exercised in
// isolation - see tests/synthetic_mission_host_live_bridge_test.cpp for
// the EngineState-level mechanism coverage). This file does NOT claim the
// real mm_p_03 script itself ever reaches a position-shaped native
// (get_dist etc.) - sub-test A below still shows it erroring on an
// earlier, unrelated OPEN gap (zscene_is_loaded) before any such call -
// this is the honest, measured Phase 2 delta: the bridge runs, every
// frame, starting before that gap is ever reached; it does not change
// which gap mm_p_03 hits first. A sibling of
// tests/real_playtest_smoke_test.cpp/tests/real_playtest_vehicle_smoke_test.cpp
// (same CreateProcess+CREATE_NO_WINDOW convention, copied not shared - every
// real_*_test in this project carries its own copy) - kept as a SEPARATE
// executable so the base smoke test's own behaviour/assertions stay
// byte-for-byte unchanged (that test's own unmodified pass, with no
// --mission given, IS this task's own "without the flag, nothing changes"
// regression proof - not duplicated here).
//
// This test exercises TWO real sub-cases against the real game cache
// (CRREISH_GAME_CACHE_DIR) plus this repo's own tools/lua_all_registered_
// 1490_tagged.txt (CRREISH_TOOLS_DIR, the same compile-definition convention
// tests/synthetic_luahost_roster_test.cpp already uses) - both using the
// REAL shipped `mm_p_03` mission script (tools/mission_package_per_
// container.tsv's own real row, sr3_city_0.vpp_pc), chosen because this
// task's own manual verification (see its HANDOFF/commit) found it reaches
// a genuinely suspended `mm_p_03_start` coroutine (waiting inside
// game_lib.lua's own group_create) before erroring on its very next
// per-frame scheduler-pass resume on a real, pre-existing OPEN engine-state
// gap (zscene_is_loaded, spec-lua-api-behaviour.md Sec26.25/Sec14.23) that
// is NOT this task's own fault or this task's to fix (Team B's "no invented
// fixes" rule) - exactly the kind of real resolve-load-bind-drive path this
// flag exists to exercise, with a real, honest, non-fatal outcome:
//
//   (A) default --mission-timeout: the per-frame scheduler-pass drive
//       actually RUNS (proving runScriptThreadSchedulerPass/
//       fireConfirmedHooks are really being called once per rendered
//       frame, not just at bind time) and the mission's own later error
//       is caught and reported WITHOUT crashing the rest of the live loop
//       (process still exits 0, the full --frames count is still
//       presented) - the core "a runaway/erroring mission must not stall
//       real frame delivery" property this task's watchdog/error-handling
//       exists for.
//   (B) a deliberately tiny --mission-timeout=0.01 (less than one
//       --no-input frame's own fixed 1/30s step): the NEW wall-clock
//       safety cutoff fires on the very first frame after bind,
//       deterministically (--no-input's fixed dt makes this exact, not
//       timing-flaky), BEFORE the scheduler pass that would otherwise hit
//       the real error above ever runs - proving the timeout path itself
//       works, independent of what any particular mission script does.
//
// Both sub-cases also check the gameplay-state preload chain (spec-lua-
// bindings.md Sec16.2: system_lib.lua then game_lib.lua, loaded from real
// misc.vpp_pc/interface.vpp_pc - see runPlaytest()'s own doc comment for
// why those two archives specifically, found by this task's own manual
// grep of the real cache) actually ran, since mm_p_03_start's own first
// call already depends on a real game_lib.lua helper.
//
// Exits 77 (ctest SKIPPED, never a pass) when CRREISH_GAME_CACHE_DIR's
// archives are absent - same convention every other real_*_test uses.
// Nothing from the game is copied into the repository; this test only
// reads the real install at run time, same as sr3_viewer itself.
//
// --bin-dir is required (the directory holding the freshly-built
// sr3_viewer.exe for THIS config - passed by CMakeLists.txt as
// $<TARGET_FILE_DIR:sr3_viewer>, the SAME genex every other real_*_test's
// own ctest registration already uses).
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW - same shape as every other real_*_test's
// own runIn() (copied, not shared - this is a standalone test executable,
// same convention every other real_*_test follows).
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
    const fs::path miscArchive = dataRoot / "misc.vpp_pc";           // real game_lib.lua carrier
    const fs::path interfaceArchive = dataRoot / "interface.vpp_pc"; // real system_lib.lua carrier
    const fs::path registrationList = fs::path(CRREISH_TOOLS_DIR) / "lua_all_registered_1490_tagged.txt";
    if (!fs::exists(characters) || !fs::exists(miscTables) || !fs::exists(cityArchive) ||
        !fs::exists(miscArchive) || !fs::exists(interfaceArchive)) {
        std::printf("SKIPPED: real game cache not found under '%s' (set CRREISH_GAME_CACHE_DIR)\n",
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
                                   " --headless --no-input --frames 30"
                                   " --mission=mm_p_03 --mission-registration-list \"" +
                                   registrationList.string() + "\"";

    bool ok = true;

    // --- (A) default --mission-timeout: the real per-frame drive runs, the
    // mission's own real error is caught, the rest of the loop is unaffected. ---
    {
        const fs::path stdoutPath = workDir / "playtest_mission_default_stdout.txt";
        std::error_code ec;
        fs::remove(stdoutPath, ec);
        const std::string cmd = exe + commonArgs + " > \"" + stdoutPath.string() + "\" 2>&1";
        const int rc = runIn(workDir.string(), cmd);
        if (rc != 0) {
            std::printf("FAIL(A): sr3_viewer.exe playtest --mission=mm_p_03 (default timeout) returned exit code "
                       "%d, expected 0 (a mission-level error must not crash the live loop) - see %s\n",
                       rc, stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : resolved --mission=mm_p_03")) {
            std::printf("FAIL(A): stdout never printed the mission resolution line (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : preloaded system_lib.lua") ||
            !fileContainsLinePrefix(stdoutPath, "mission  : preloaded game_lib.lua")) {
            std::printf("FAIL(A): stdout never printed both gameplay-state preload lines (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : mm_p_03_start() called, suspended")) {
            std::printf("FAIL(A): stdout never printed the real suspended-_start line - the per-frame drive never "
                       "actually started (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        // PHASE 2 (this task): the live position bridge runs every frame mission
        // mode is on, INDEPENDENTLY of whether the mission's own coroutine is
        // suspended/erroring/timed out - proves EngineState::
        // bridgeLiveCharacterPosition() is really wired into this live path, not
        // just reachable in isolation.
        if (!fileContainsLinePrefix(stdoutPath, "mission  : live position bridge active for '#PLAYER1#'")) {
            std::printf("FAIL(A): stdout never printed the Phase 2 live-position-bridge line - the new "
                       "per-frame bridge call never actually ran (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : --mission=mm_p_03 thread 'mm_p_03_start' errored on resume")) {
            std::printf("FAIL(A): stdout never printed the real per-frame scheduler-pass error line - the "
                       "per-frame drive (runScriptThreadSchedulerPass) never actually resumed the suspended "
                       "coroutine (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "frames   : 30 presented")) {
            std::printf("FAIL(A): stdout never printed 'frames   : 30 presented' - the mission's own error "
                       "appears to have disrupted the rest of the live loop (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (ok) std::printf("sub-test A (default timeout, real per-frame drive + real error, loop unaffected): PASS\n");
    }

    // --- (B) a tiny --mission-timeout: fires deterministically on frame 1
    // (under --no-input's fixed 1/30s dt), before the scheduler-pass error
    // above ever has a chance to fire. ---
    {
        const fs::path stdoutPath = workDir / "playtest_mission_timeout_stdout.txt";
        std::error_code ec;
        fs::remove(stdoutPath, ec);
        const std::string cmd = exe + commonArgs + " --mission-timeout=0.01 > \"" + stdoutPath.string() + "\" 2>&1";
        const int rc = runIn(workDir.string(), cmd);
        if (rc != 0) {
            std::printf("FAIL(B): sr3_viewer.exe playtest --mission=mm_p_03 --mission-timeout=0.01 returned exit "
                       "code %d, expected 0 (a mission timeout must not crash the live loop) - see %s\n",
                       rc, stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : mm_p_03_start() called, suspended")) {
            std::printf("FAIL(B): stdout never printed the real suspended-_start line (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        // PHASE 2 (this task): the bridge runs on frame 0 even in this tiny-
        // timeout sub-case, BEFORE the timeout itself stops the drive - same
        // proof as sub-test A, independently confirmed here too.
        if (!fileContainsLinePrefix(stdoutPath, "mission  : live position bridge active for '#PLAYER1#'")) {
            std::printf("FAIL(B): stdout never printed the Phase 2 live-position-bridge line (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "mission  : --mission=mm_p_03 stopped - wall-clock timeout")) {
            std::printf("FAIL(B): stdout never printed the wall-clock timeout stop line - the NEW timeout "
                       "mechanism never fired (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (fileContainsLinePrefix(stdoutPath, "mission  : --mission=mm_p_03 thread 'mm_p_03_start' errored on resume")) {
            std::printf("FAIL(B): stdout printed the scheduler-pass error line - the tiny timeout did NOT win the "
                       "race against the first resume as expected (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (!fileContainsLinePrefix(stdoutPath, "frames   : 30 presented")) {
            std::printf("FAIL(B): stdout never printed 'frames   : 30 presented' - the timeout appears to have "
                       "disrupted the rest of the live loop (see %s)\n",
                       stdoutPath.string().c_str());
            ok = false;
        }
        if (ok) std::printf("sub-test B (tiny timeout, deterministic clean stop, loop unaffected): PASS\n");
    }

    std::printf("\nREAL_PLAYTEST_MISSION_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
