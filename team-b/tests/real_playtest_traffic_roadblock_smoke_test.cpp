// Real-data smoke test for sr3_viewer's `playtest` command's new near-field
// ambient-vehicle-traffic/police-roadblock WIRING (2026-10-09,
// playtest-nearfield-traffic-roadblock task). A SIBLING of
// tests/real_playtest_vehicle_smoke_test.cpp (that test's own header
// comment has the full account of the base vehicle-entry smoke test this
// one extends the same convention from) - kept as a SEPARATE executable/
// test, same "lower merge risk" reasoning that file's own header gives.
//
// SCOPE (per this task's own brief: "you likely only need to test the new
// per-frame call-site wiring ... not re-test the already-proven population
// logic"). The underlying algorithms
// (NearFieldVehicleDriver/RunNearFieldVehicleMaintenancePass,
// RoadblockPopulationDriver/RoadblockSpawnManager) already have their own
// full synthetic coverage (tests/synthetic_near_field_vehicle_population_
// test.cpp, tests/synthetic_police_roadblock_spawn_test.cpp) - this test
// does NOT re-derive or re-check any of that. It checks ONLY that
// runPlaytest()'s own NEW glue actually reaches those real functions every
// frame from LIVE state:
//   1. clean exit (code 0, no crash);
//   2. stdout shows the near-field traffic system actually ticking and
//      accumulating a non-zero live population over the run (the
//      "[traffic] frame N near-field live=X/48 ..." periodic line, and the
//      final summary line's own "near-field traffic live=X/48" field with
//      X > 0) - proving the per-frame maintenance-tick call site actually
//      reaches RunNearFieldVehicleMaintenancePass() with live candidates,
//      not just that it compiles;
//   3. stdout shows BOTH "[roadblock] armed" and "[roadblock] disarmed" -
//      proving the new edge-triggered 'R'-key toggle (--script-roadblock's
//      scripted press pair) correctly flips RoadblockPopulationDriver::armed
//      exactly once per scripted press, the same edge-trigger debounce
//      shape vehEEdge/prevEDown already establish for 'E';
//   4. stdout shows at least one roadblock slot actually filled while armed
//      (the final summary line's own "roadblock armed=... filled=X/8" field
//      with X > 0 at some point, read from the periodic "[traffic]" lines) -
//      proving the tick's own RunTick() call site, anchored at the live
//      player position, actually reaches a real spawn dispatch, not just
//      that ShouldRunTick() was polled;
//   5. a real non-trivial capture PNG was written (visual sanity only - this
//      test does not pixel-compare it against anything, see this project's
//      own "no golden-frame comparison for playtest" note elsewhere).
// Exits 77 (ctest SKIPPED, never a pass) when CRREISH_GAME_CACHE_DIR's
// archives are absent - same convention every other real_*_test uses.
// Nothing from the game is copied into the repository; this test only
// reads the real install at run time, same as sr3_viewer itself.
//
// --bin-dir is required (the directory holding the freshly-built
// sr3_viewer.exe for THIS config - passed by CMakeLists.txt as
// $<TARGET_FILE_DIR:sr3_viewer>, the SAME genex every other real_playtest_*
// test already uses).
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW - same shape every other real_*_test's
// own runIn() uses (copied, not shared - standalone test executable
// convention this project already follows).
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
    if (!fs::exists(characters) || !fs::exists(miscTables) || !fs::exists(cityArchive)) {
        std::printf("SKIPPED: real game cache not found under '%s' (set CRREISH_GAME_CACHE_DIR)\n",
                   dataRoot.string().c_str());
        return 77;
    }

    const fs::path workDir = fs::path(binDir);  // cwd = the exe's own directory, so a relative --capture lands beside it
    const fs::path capturePath = workDir / "playtest_traffic_roadblock_smoke.png";
    const fs::path stdoutPath = workDir / "playtest_traffic_roadblock_smoke_stdout.txt";
    std::error_code ec;
    fs::remove(capturePath, ec);
    fs::remove(stdoutPath, ec);

    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    // --no-vehicle is NOT passed - near-field traffic/roadblock vehicle
    // drawing both reuse the player's own loaded vehicleRig (see
    // runPlaytest()'s own doc comment), so the default (vehicle enabled)
    // exercises that reuse path too, not just the population bookkeeping.
    // --script-roadblock presses 'R' at frame 20 (arm) and frame 280
    // (disarm) - see kScriptRoadblockArmFrame/kScriptRoadblockDisarmFrame in
    // runPlaytest() itself. --capture-frame 250 lands well after the arm
    // press, inside the window where the roadblock table has had many
    // ticks to populate and near-field traffic has had time to spawn.
    const std::string cmd = exe + " playtest \"" + characters.string() + "\" \"" + miscTables.string() + "\" \"" +
                            cityArchive.string() +
                            "\" --headless --no-input --script-roadblock --frames 300"
                            " --capture \"" + capturePath.string() + "\" --capture-frame 250" +
                            " > \"" + stdoutPath.string() + "\" 2>&1";
    const int rc = runIn(workDir.string(), cmd);
    if (rc != 0) {
        std::printf("FAIL: sr3_viewer.exe playtest returned exit code %d (see %s)\n", rc, stdoutPath.string().c_str());
        return 1;
    }

    bool ok = true;
    if (!fs::exists(capturePath) || fs::file_size(capturePath) < 1024) {
        std::printf("FAIL: capture PNG missing or implausibly small: %s\n", capturePath.string().c_str());
        ok = false;
    } else {
        std::printf("capture  : %s (%llu bytes)\n", capturePath.string().c_str(),
                   static_cast<unsigned long long>(fs::file_size(capturePath)));
    }

    std::ifstream in(stdoutPath);
    std::string line, lastFramesLine;
    bool sawFramesLine = false, sawArmed = false, sawDisarmed = false;
    bool sawNonzeroTraffic = false, sawNonzeroRoadblockFilled = false;
    while (std::getline(in, line)) {
        if (line.rfind("frames   : 300 presented", 0) == 0) { sawFramesLine = true; lastFramesLine = line; }
        if (line.rfind("[roadblock] armed", 0) == 0) sawArmed = true;
        if (line.rfind("[roadblock] disarmed", 0) == 0) sawDisarmed = true;
        if (line.rfind("[traffic] frame ", 0) == 0) {
            size_t liveEq = line.find("near-field live=");
            if (liveEq != std::string::npos) {
                int liveCount = std::atoi(line.c_str() + liveEq + std::string("near-field live=").size());
                if (liveCount > 0) sawNonzeroTraffic = true;
            }
            size_t filledEq = line.find("filled=");
            if (filledEq != std::string::npos) {
                int filledCount = std::atoi(line.c_str() + filledEq + std::string("filled=").size());
                if (filledCount > 0) sawNonzeroRoadblockFilled = true;
            }
        }
    }
    if (!sawFramesLine) {
        std::printf("FAIL: stdout never printed 'frames   : 300 presented' (see %s)\n", stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", lastFramesLine.c_str());
    }
    if (!sawArmed) {
        std::printf("FAIL: stdout never printed '[roadblock] armed' - the scripted 'R' press at frame 20 never "
                   "toggled RoadblockPopulationDriver::armed (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawDisarmed) {
        std::printf("FAIL: stdout never printed '[roadblock] disarmed' - the scripted second 'R' press at frame "
                   "280 never toggled it back off (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawNonzeroTraffic) {
        std::printf("FAIL: stdout never showed a non-zero near-field traffic live count in any '[traffic]' line "
                   "- the per-frame maintenance-tick wiring never actually populated anything (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawNonzeroRoadblockFilled) {
        std::printf("FAIL: stdout never showed a non-zero roadblock filled-slot count in any '[traffic]' line "
                   "while armed - the roadblock tick wiring never actually dispatched a real creation (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }

    std::printf("\nREAL_PLAYTEST_TRAFFIC_ROADBLOCK_SMOKE_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
