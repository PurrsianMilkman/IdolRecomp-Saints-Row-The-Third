// Real-data smoke test for sr3_viewer's `playtest` command's new vehicle
// entry/driving interaction (2026-10-08, playtest-vehicle task). A sibling
// of tests/real_playtest_smoke_test.cpp (that test's own header comment has
// the full account of the base playtest loop this one extends) - kept as a
// SEPARATE executable/test rather than editing that one in place, so the
// existing base smoke test's own behaviour/assertions stay byte-for-byte
// unchanged (lower merge risk - see this task's own brief about a sibling
// agent simultaneously touching runPlaytest() in a different worktree).
//
// Launches the real sr3_viewer.exe `playtest` subcommand --headless
// --no-input --script-vehicle against the real game cache
// (CRREISH_GAME_CACHE_DIR), with --spawn/--vehicle-spawn placed close
// together (2m apart - inside the real kVehicleEnterRadius=3m gate, see
// runPlaytest()'s own "VEHICLE ENTRY/DRIVING" doc comment) so the SAME real
// proximity-gated entry code path a live player would exercise is what
// actually fires the scripted entry at frame kScriptVehicleEnterFrame=10 -
// this test does not bypass that gate. Checks:
//   1. clean exit (code 0, no crash - the real sr3physics::VehicleSubstep
//      Simulator Step()/the real mesh re-upload/pose path all ran without
//      throwing or returning a non-zero fprintf-then-return-1 error path);
//   2. stdout shows the vehicle loaded (not disabled by a load failure);
//   3. stdout shows BOTH "[vehicle] entered" and "[vehicle] exited" -
//      proving the scripted E-press edge trigger actually fired the real
//      proximity-gated state machine twice (enter, then exit), not just
//      that the flag was accepted;
//   4. the real captured frame (one PNG, taken mid-drive - see
//      --capture-frame below) was actually written and is a plausible size;
//   5. stdout's own final "frames   : 300 presented" line, confirming the
//      run reached the end of the loop (back on foot again by then, since
//      kScriptVehicleExitFrame=130 < 300) rather than breaking early.
// Exits 77 (ctest SKIPPED, never a pass) when CRREISH_GAME_CACHE_DIR's
// archives are absent - same convention every other real_*_test uses.
// Nothing from the game is copied into the repository; this test only
// reads the real install at run time, same as sr3_viewer itself.
//
// --bin-dir is required (the directory holding the freshly-built
// sr3_viewer.exe for THIS config - passed by CMakeLists.txt as
// $<TARGET_FILE_DIR:sr3_viewer>, the SAME genex golden_scene_check.cpp's
// own ctest registration already uses, not a hardcoded path).
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW - same shape as
// real_playtest_smoke_test.cpp's own runIn() (copied, not shared - this is
// a standalone test executable, same convention every other real_*_test
// follows).
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
    const fs::path capturePath = workDir / "playtest_vehicle_smoke.png";
    const fs::path stdoutPath = workDir / "playtest_vehicle_smoke_stdout.txt";
    std::error_code ec;
    fs::remove(capturePath, ec);
    fs::remove(stdoutPath, ec);

    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    // --spawn/--vehicle-spawn 2m apart (same X, Z+2) - inside the real 3m
    // entry radius from frame 0, so the scripted E-press at frame 10 (see
    // this file's own header comment) exercises the SAME real proximity
    // gate a live player's own walk-up would. --script-forward-frames 0
    // --script-turn-frames 0 (THIS TASK): the BASE playtest --no-input
    // fallback (real_playtest_smoke_test.cpp's own default) walks the
    // character forward from frame 0 to exercise the walk/camera path -
    // directly measured this task that doing so here carries the character
    // out of the 3m entry radius well before frame 10 (the scripted E-press
    // frame), since "forward" is camera-relative and the default camera
    // does not face the parked vehicle. This test's own job is the vehicle
    // state machine, not the walk choreography, so the character is kept
    // stationary (already standing next to the car) instead. --capture-frame
    // 70 lands inside the scripted drive window (frames 11..129), so the
    // captured PNG actually shows the vehicle mid-drive, not parked.
    const std::string cmd = exe + " playtest \"" + characters.string() + "\" \"" + miscTables.string() + "\" \"" +
                            cityArchive.string() +
                            "\" --headless --no-input --script-vehicle --frames 300"
                            " --script-forward-frames 0 --script-turn-frames 0"
                            " --spawn -960 -698 --vehicle-spawn -960 -700"
                            " --capture \"" + capturePath.string() + "\" --capture-frame 70" +
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
    bool sawFramesLine = false, sawVehicleParked = false, sawEntered = false, sawExited = false, sawDisabled = false;
    while (std::getline(in, line)) {
        if (line.rfind("frames   : 300 presented", 0) == 0) { sawFramesLine = true; lastFramesLine = line; }
        if (line.rfind("vehicle  : ", 0) == 0 && line.find("parked at") != std::string::npos) sawVehicleParked = true;
        if (line.rfind("vehicle  : DISABLED", 0) == 0) sawDisabled = true;
        if (line.rfind("[vehicle] entered", 0) == 0) sawEntered = true;
        if (line.rfind("[vehicle] exited", 0) == 0) sawExited = true;
    }
    if (sawDisabled) {
        std::printf("FAIL: vehicle load was DISABLED (see %s for the reason)\n", stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawVehicleParked) {
        std::printf("FAIL: stdout never printed the vehicle 'parked at' load line (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawEntered) {
        std::printf("FAIL: stdout never printed '[vehicle] entered' - the scripted E-press never satisfied the "
                   "real proximity gate (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawExited) {
        std::printf("FAIL: stdout never printed '[vehicle] exited' (see %s)\n", stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawFramesLine) {
        std::printf("FAIL: stdout never printed 'frames   : 300 presented' (see %s)\n", stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", lastFramesLine.c_str());
    }

    std::printf("\nREAL_PLAYTEST_VEHICLE_SMOKE_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
