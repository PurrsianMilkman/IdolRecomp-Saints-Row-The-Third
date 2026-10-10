// Real-data regression test for the world-streaming reconcile loop's
// steady-state behaviour (2026-10-08, world-streaming-reconcile task,
// spec-world-streaming.md Sec10.4/Sec11). A SIBLING of
// tests/real_playtest_smoke_test.cpp (that test's own header comment has
// the full account of the base playtest loop this one extends) - kept as a
// SEPARATE executable/test, same reasoning as
// real_playtest_vehicle_smoke_test.cpp's own header comment: lower merge
// risk, and the base smoke test's own 300-frame/default-spawn assertions
// stay byte-for-byte unchanged.
//
// What this proves, that real_playtest_smoke_test.cpp's short 300-frame run
// cannot (the default scripted walk only covers ~36m before the real city's
// own building collision geometry blocks it - see runPlaytest()'s own
// "--no-collision" doc comment): the just-landed Sec10.4 finding that the
// real engine's reconciliation (0x008610e0) re-triggers EVERY rendered
// frame with NO per-tick acquire/release cap actually holds for THIS
// project's own playtest reconcile() loop over a long walk that crosses
// real level-4 fine-cell AND tile boundaries:
//   1. the wanted-cell-list genuinely changes as the focus position moves
//      (frame 0's "[stream] ... wanted = {...}" set differs from the last
//      one printed);
//   2. at least one cell that was resident is later evicted (a
//      "[stream] frame N EVICT <name> ..." line appears) - proving the
//      release path this project's own CityLoadedCell/MeshRenderer
//      destructors already implement is actually exercised, not just
//      theoretically wired;
//   3. the ground tile (CityGroundTile, tile-granularity cache) belonging
//      to the ORIGINAL spawn tile is also eventually evicted (a
//      "[ground] frame N EVICT tile ..." line appears) - the SAME release
//      path, one level up;
//   4. the final summary line's own real counters (added this task) show
//      at least one real cell load AND at least one real cell eviction,
//      not merely that reconcile() was *called*.
// --no-collision (added this task, OFF by default everywhere else) is used
// here specifically so a long, deterministic, collision-free walk can cross
// tile boundaries without a hand-picked clear corridor - this is a
// streaming/focus-tracking regression, not a collision regression (that
// stays covered, untouched, by real_playtest_smoke_test.cpp's own default
// run with collision ON). 750 scripted-forward frames (25s at the fixed
// --no-input 1/30s step, ~10 m/s synthetic run speed - see runPlaytest()'s
// own pm.tweaks.maxRunSpeed.set(10)) comfortably covers more than one
// 280x320m tile, confirmed empirically this task (results/
// verify_world_streaming_reconcile_20261008/longwalk/).
// Exits 77 (ctest SKIPPED, never a pass) when CRREISH_GAME_CACHE_DIR's
// archives are absent - same convention every other real_*_test uses.
// Nothing from the game is copied into the repository; this test only
// reads the real install at run time, same as sr3_viewer itself.
//
// --bin-dir is required (the directory holding the freshly-built
// sr3_viewer.exe for THIS config - passed by CMakeLists.txt as
// $<TARGET_FILE_DIR:sr3_viewer>, the SAME genex every other real_*_test
// uses).
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW - same shape every other real_*_test in
// this directory uses (copied, not shared - standalone test executable).
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

    const fs::path workDir = fs::path(binDir); // cwd = the exe's own directory
    const fs::path stdoutPath = workDir / "playtest_streaming_longwalk_stdout.txt";
    std::error_code ec;
    fs::remove(stdoutPath, ec);

    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    // --no-collision + a long straight scripted walk (no capture - this test only needs
    // stdout, not a PNG) - see this file's own header comment for why. No --spawn override:
    // deliberately relies on runPlaytest()'s own default spawn, same as
    // real_playtest_smoke_test.cpp, so this test tracks that default rather than freezing a
    // second copy of it.
    const std::string cmd = exe + " playtest \"" + characters.string() + "\" \"" + miscTables.string() + "\" \"" +
                            cityArchive.string() +
                            "\" --headless --no-input --no-collision --no-vehicle"
                            " --frames 750 --script-forward-frames 750 --script-turn-frames 0"
                            " > \"" + stdoutPath.string() + "\" 2>&1";
    const int rc = runIn(workDir.string(), cmd);
    if (rc != 0) {
        std::printf("FAIL: sr3_viewer.exe playtest returned exit code %d (see %s)\n", rc, stdoutPath.string().c_str());
        return 1;
    }

    std::ifstream in(stdoutPath);
    std::string line;
    std::string firstWanted, lastWanted;
    bool sawFirstWanted = false;
    bool sawCellEvict = false, sawGroundEvict = false, sawFramesLine = false;
    std::string lastFramesLine;
    size_t finalLoads = 0, finalEvicts = 0;
    while (std::getline(in, line)) {
        const size_t wantedTag = line.find("wanted = {");
        if (line.rfind("[stream] frame ", 0) == 0 && wantedTag != std::string::npos) {
            const std::string set = line.substr(wantedTag);
            if (!sawFirstWanted) { firstWanted = set; sawFirstWanted = true; }
            lastWanted = set; // keeps overwriting - ends up holding the LAST one seen
        }
        if (line.rfind("[stream] frame ", 0) == 0 && line.find(" EVICT ") != std::string::npos) sawCellEvict = true;
        if (line.rfind("[ground] frame ", 0) == 0 && line.find(" EVICT tile ") != std::string::npos)
            sawGroundEvict = true;
        if (line.rfind("frames   : 750 presented", 0) == 0) {
            sawFramesLine = true;
            lastFramesLine = line;
            // Parses "frames   : 750 presented (%zu cell loads, %zu cell evictions, ...)" -
            // the exact format runPlaytest()'s own final summary print uses (THIS task's own
            // addition to that line, appended AFTER "presented" - see that printf's own doc
            // comment for why real_playtest_smoke_test.cpp's/
            // real_playtest_vehicle_smoke_test.cpp's own prefix checks stay unaffected).
            size_t loads = 0, evicts = 0;
            if (std::sscanf(line.c_str(), "frames   : 750 presented (%zu cell loads, %zu cell evictions", &loads,
                            &evicts) == 2) {
                finalLoads = loads;
                finalEvicts = evicts;
            }
        }
    }

    bool ok = true;
    if (!sawFirstWanted) {
        std::printf("FAIL: stdout never printed a '[stream] ... wanted = {...}' line (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else if (firstWanted == lastWanted) {
        std::printf("FAIL: the wanted-cell-list at the end of the run is IDENTICAL to frame 0's own "
                   "('%s') - the focus position never moved the streaming window, or reconcile() stopped "
                   "updating it (see %s)\n",
                   firstWanted.c_str(), stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("wanted   : frame 0 %s\n           final  %s\n", firstWanted.c_str(), lastWanted.c_str());
    }
    if (!sawCellEvict) {
        std::printf("FAIL: stdout never printed a '[stream] frame N EVICT ...' line - no fine cell was ever "
                   "released as the focus moved away from it (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawGroundEvict) {
        std::printf("FAIL: stdout never printed a '[ground] frame N EVICT tile ...' line - the original spawn "
                   "tile's own ground/terrain resource was never released (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawFramesLine) {
        std::printf("FAIL: stdout never printed 'frames   : 750 presented' (see %s)\n", stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", lastFramesLine.c_str());
        if (finalLoads == 0) {
            std::printf("FAIL: final summary line shows 0 cell loads - reconcile() never loaded anything\n");
            ok = false;
        }
        if (finalEvicts == 0) {
            std::printf("FAIL: final summary line shows 0 cell evictions - reconcile() never released anything\n");
            ok = false;
        }
    }

    std::printf("\nREAL_PLAYTEST_STREAMING_LONGWALK_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
