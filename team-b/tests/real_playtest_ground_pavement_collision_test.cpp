// Real-data smoke test for the street-collision-0x2237-mopp follow-up task
// (2026-10-11, spec-player-runtime.md Sec5.6.5's own "RESOLVED, 2026-10-11"
// finding / spec-zone-data-format.md Sec13.6): `sr3_viewer playtest` now
// samples the real `0x2237` ground-pavement COLLISION mesh (the SAME
// vertex/triangle data already decoded for rendering, see
// SampleGroundMeshHeight()'s own doc comment above loadCityGroundTile() in
// tools/sr3_viewer.cpp) for the player's and vehicle's own vertical ground
// support, in preference to the pre-existing Heightmap (0x2244)/flat-plane
// fallback chain - both of which are independently known to be WRONG for
// this exact tile/position (see below).
//
// A sibling of tests/real_playtest_vehicle_smoke_test.cpp (that file's own
// header comment has the full CreateProcess/--bin-dir account this test
// reuses verbatim) - kept as a separate executable, same "lower merge risk"
// convention every other real_playtest_* sibling test already follows.
//
// WHY THIS EXACT SPAWN POINT: tile `sr3_city~s0916` is the spec's own
// confirmed real, non-trivial MOPP-populated flat-pavement tile (Sec5.6.5's
// own real-bytes citation - 484,684-byte `0x2237` record, MOPP magic at
// record-relative offset 247,156). (-1085,-820) is a point on this
// project's own decoded real collision mesh for that tile (237 triangles,
// measured world-Y bounds [18.15, 31.20] - NOT a flat Y~0 plane) -
// confirmed empirically against the real shipped file during this task's
// own development (not asserted blind). At this EXACT point:
//   - the real `0x2237` collision mesh resolves a real triangle at Y~29.6m
//     (`SampleGroundMeshHeight()`);
//   - the pre-existing Heightmap (0x2244) sampler reads Y=-5.0m at this
//     same (x,z) - a genuinely WRONG value for this tile/position (not
//     merely "less precise" - a completely different content kind that
//     happens to carry a different number here), confirming this fix is
//     not redundant with the existing 0x2244-based fallback;
//   - the OLD behaviour (before this task) would have left the player/
//     vehicle sitting at the flat `finalGroundY` plane (the tile's own
//     measured mid-bounds value, ~24.68m here) and NEVER moving from it,
//     since nothing fed a per-frame target that differed meaningfully
//     from that flat starting guess for a position the Heightmap sampler
//     also gets wrong.
// This test's own assertion range [15, 35] is real-measured-bounds-derived
// (the tile's own [18.15, 31.20] decoded render bounds, widened by ~3m of
// margin for the capsule's own half-height/ground-follower easing, per
// sr3player/ground_follow.h's own `kDefaultGroundFollowSupportedThreshold`/
// per-frame cap) - wide enough to tolerate real easing dynamics, narrow
// enough to definitively exclude both the Heightmap's wrong -5.0m AND any
// regression back to a flat Y=0 plane (this project's own very first,
// pre-Heightmap-fix "sky-blue gap" bug - see sr3player/ground_follow.h's
// own top comment for that history).
//
// Checks (both on foot AND driving, in ONE run - --script-vehicle's own
// scripted enter/drive/exit sequence runs inside the SAME walk the ground-
// mesh assertion above already needs, so a second archive-load pass is not
// required):
//   1. clean exit (code 0, no crash);
//   2. stdout shows the real `0x2237` collision-mesh spawn probe
//      ("groundmesh: real 0x2237 collision-mesh sample at spawn") - proving
//      SampleGroundMeshHeight() actually resolved a real triangle here, not
//      merely that the code path is wired but inert;
//   3. the ON-FOOT captured frame's own player Y lands in [15, 35] - real
//      pavement support, not the Heightmap's wrong -5.0m nor a flat-plane
//      regression;
//   4. the DRIVING captured frame's own vehicle Y (now printed in this
//      line by this task, see tools/sr3_viewer.cpp's own "DRIVING -
//      vehicle pos" doc comment) ALSO lands in [15, 35] - the vehicle has
//      no vertical physics model of its own (ground_follow.h's own top
//      comment), so this is the ONLY evidence the vehicle itself rests on
//      real pavement rather than an arbitrary fallback;
//   5. stdout's own final "frames   : N presented" line, confirming the
//      run reached the end of the loop.
// Exits 77 (ctest SKIPPED, never a pass) when CRREISH_GAME_CACHE_DIR's
// archives are absent - same convention every other real_*_test uses.
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// CreateProcess + CREATE_NO_WINDOW - same shape as every other real_*_test's
// own runIn() (copied, not shared - standalone test executable).
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

// Parses "captured : <path> (frame N, player pos (X Y Z) heading ...)" ->
// Y. Returns false if the line does not match this exact ON-FOOT shape
// (e.g. a DRIVING line, which has its own parseVehicleY() below).
bool parsePlayerY(const std::string& line, double& outY) {
    const size_t p = line.find("player pos (");
    if (p == std::string::npos) return false;
    std::istringstream iss(line.substr(p + std::string("player pos (").size()));
    double x = 0, y = 0, z = 0;
    if (!(iss >> x >> y >> z)) return false;
    outY = y;
    return true;
}

// Parses "captured : <path> (frame N, DRIVING - vehicle pos (X Y Z) ...)"
// -> Y (this task added the Y field to this line - see tools/sr3_viewer.cpp's
// own "DRIVING - vehicle pos" doc comment).
bool parseVehicleY(const std::string& line, double& outY) {
    const size_t p = line.find("DRIVING - vehicle pos (");
    if (p == std::string::npos) return false;
    std::istringstream iss(line.substr(p + std::string("DRIVING - vehicle pos (").size()));
    double x = 0, y = 0, z = 0;
    if (!(iss >> x >> y >> z)) return false;
    outY = y;
    return true;
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

    const fs::path workDir = fs::path(binDir);
    const fs::path capturePath = workDir / "playtest_ground_pavement.png";
    const fs::path stdoutPath = workDir / "playtest_ground_pavement_stdout.txt";
    std::error_code ec;
    fs::remove(capturePath, ec);
    fs::remove(stdoutPath, ec);

    const std::string exe = "\"" + (fs::path(binDir) / "sr3_viewer.exe").string() + "\"";
    // --spawn (-1085,-820): confirmed real `0x2237` collision-mesh coverage
    // for tile 0916, see this file's own top comment. --vehicle-spawn 2m
    // away (same convention real_playtest_vehicle_smoke_test.cpp's own
    // header comment already establishes - inside the real 3m entry-radius
    // gate) at a SEPARATELY confirmed real-mesh point.
    // --script-forward-frames 0 --script-turn-frames 0 keeps the player
    // stationary next to the car for the scripted E-press (same reasoning
    // as real_playtest_vehicle_smoke_test.cpp), so this test's own ON-FOOT
    // capture (frame 5, before the vehicle enters at frame 10) still shows
    // the player resting at the ORIGINAL spawn point's own real mesh
    // height, not wherever a walk script would have carried it by then.
    const std::string cmd = exe + " playtest \"" + characters.string() + "\" \"" + miscTables.string() + "\" \"" +
                            cityArchive.string() +
                            "\" --headless --no-input --script-vehicle --frames 180"
                            " --script-forward-frames 0 --script-turn-frames 0"
                            " --spawn -1085 -820 --vehicle-spawn -1085 -818"
                            " --capture \"" + capturePath.string() + "\" --capture-frames 5,70" +
                            " > \"" + stdoutPath.string() + "\" 2>&1";
    const int rc = runIn(workDir.string(), cmd);
    if (rc != 0) {
        std::printf("FAIL: sr3_viewer.exe playtest returned exit code %d (see %s)\n", rc, stdoutPath.string().c_str());
        return 1;
    }

    bool ok = true;
    std::ifstream in(stdoutPath);
    std::string line, lastFramesLine;
    bool sawFramesLine = false, sawGroundMeshReal = false, sawOnFootY = false, sawVehicleY = false;
    double onFootY = 0.0, vehicleY = 0.0;
    while (std::getline(in, line)) {
        if (line.rfind("frames   : ", 0) == 0 && line.find("presented") != std::string::npos) {
            sawFramesLine = true;
            lastFramesLine = line;
        }
        if (line.rfind("groundmesh: real 0x2237 collision-mesh sample at spawn", 0) == 0) sawGroundMeshReal = true;
        double y = 0.0;
        if (!sawOnFootY && parsePlayerY(line, y)) { onFootY = y; sawOnFootY = true; }
        if (parseVehicleY(line, y)) { vehicleY = y; sawVehicleY = true; }  // last one wins - mid/late drive
    }

    if (!sawGroundMeshReal) {
        std::printf("FAIL: stdout never printed the real 0x2237 collision-mesh spawn probe (see %s) - "
                   "SampleGroundMeshHeight() did not resolve a real triangle at the spawn point\n",
                   stdoutPath.string().c_str());
        ok = false;
    }
    if (!sawOnFootY) {
        std::printf("FAIL: stdout never printed an ON-FOOT 'player pos' capture line (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else if (onFootY < 15.0 || onFootY > 35.0) {
        std::printf("FAIL: on-foot player Y=%.2f outside the real tile-0916 mesh bounds [15,35] (see %s) - "
                   "still on the Heightmap's wrong -5.0m value or a flat-plane fallback, not the real "
                   "0x2237 collision mesh\n",
                   onFootY, stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("on-foot  : player Y=%.2f (within real tile-0916 mesh bounds [15,35])\n", onFootY);
    }
    if (!sawVehicleY) {
        std::printf("FAIL: stdout never printed a DRIVING 'vehicle pos' capture line with a Y field (see %s) - "
                   "either the scripted vehicle entry never fired, or the Y field this task added is missing\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else if (vehicleY < 15.0 || vehicleY > 35.0) {
        std::printf("FAIL: driving vehicle Y=%.2f outside the real tile-0916 mesh bounds [15,35] (see %s)\n",
                   vehicleY, stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("driving  : vehicle Y=%.2f (within real tile-0916 mesh bounds [15,35])\n", vehicleY);
    }
    if (!sawFramesLine) {
        std::printf("FAIL: stdout never printed the final 'frames   : N presented' line (see %s)\n",
                   stdoutPath.string().c_str());
        ok = false;
    } else {
        std::printf("stdout   : %s\n", lastFramesLine.c_str());
    }

    std::printf("\nREAL_PLAYTEST_GROUND_PAVEMENT_COLLISION_TEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
