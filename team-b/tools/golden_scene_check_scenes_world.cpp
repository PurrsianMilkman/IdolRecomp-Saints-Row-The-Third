// golden_scene_check_scenes_world.cpp
// Scene checks: zone tile, tree, clmesh, city corner, ambient population, walk demos, HUD.
//
// Split out of tools/golden_scene_check.cpp (Team C refactor, 2026-10-17): the
// code below moved verbatim from that file's anonymous namespace into
// namespace golden_scene_check. See golden_scene_check.cpp's header comment for the
// harness's scope, scenes and design notes.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Windows.h (B3 task, 2026-10-07): RegGetValueA/GetSystemDirectoryA/
// GetFileAttributesExA for queryHostEnvironmentInfo() below (OS build +
// WARP rasterizer DLL fingerprint) - advapi32/kernel32 are already part of
// MSVC's own default link-library set, so this needs no new
// target_link_libraries entry in CMakeLists.txt. NOMINMAX: this file uses
// std::min/std::max (comparePngTolerance() below) - without it Windows.h's
// own min/max MACROS shadow those calls and fail to compile.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "zlib.h"

#include "golden_scene_check.h"

namespace golden_scene_check {

// The 4th golden scene (extends checkBradScene()/checkVehicleScene() above
// to the `zone` command - see tests/golden/zone_tile/COMMAND.txt for the
// real archive/tile/why). Same shape as checkVehicleScene(): one
// --capture PNG + one stdout capture, run through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat produces (build_verify/
// sr3_viewer.exe predates the `zone` command entirely and does not
// recognise it, exactly like it predates `vehicle` - see build.bat's own
// comments). No shaders archive is needed here (no confirmed real
// material/texture binding exists for zone data - see runZoneTile()'s own
// doc comment - so this is not routed through checkVehicleScene()'s shared
// per-material-shader helper).
//
// RE-FROZEN 2026-09-30 (HANDOFF.md Sec9.126/Sec9.127): switched from vertex
// layout code 24 (a real, UV-less companion stream; ROLE OPEN - an earlier
// "probable proxy" guess was tested by a direct composite render against
// layout code 0 and did not hold up, see runZoneTile()'s own doc comment's
// 2026-09-30 update and runZoneTileComposite()'s own doc comment for the
// real visual evidence) to layout code 0
// (the real UV-bearing render geometry) - see runZoneTile()'s own doc
// comment for the full rationale, including why `--group 0` was DROPPED
// from the real command here (channel 1's own real draw ranges are split,
// gap-free, across BOTH located LOD groups for this tile, not contained in
// group 0 alone; the command's new default auto-unions every located
// group's own ranges for the resolved channel). The superseded code-24
// baseline is preserved verbatim under `_code24_proxy`-suffixed filenames
// in the same directory (COMMAND_code24_proxy.txt,
// baseline_capture_code24_proxy.png, baseline_stdout_code24_proxy.txt) -
// same convention tests/golden/tree/COMMAND.txt already established with
// its own `_group0_only` suffix.
SceneResult checkZoneTileScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "zone_tile (sr3_viewer zone, vertex layout code 0)";

    const std::string goldenDir = root + "\\tests\\golden\\zone_tile";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string cznName = "sr3_city~f0816~al.czn_pc";
    const std::string workStem = "zone_tile";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/zone_tile.png") that
    // tests/golden/zone_tile/COMMAND.txt records was used to freeze the
    // baseline - sr3_viewer echoes this argument back verbatim in its
    // "captured:" line, so using anything else would make an otherwise-
    // identical stdout report a false mismatch (the same caller-chosen-
    // path detail brad/vehicle_genki's own COMMAND.txt files document). NO
    // --group is passed (deliberately, since the 2026-09-30 re-freeze -
    // see this function's own comment above): the command's new default
    // auto-unions every located LOD group's own ranges for the resolved
    // channel, which is what the real command that froze this baseline
    // actually used.
    std::string cmd = "\"" + viewerExe + "\" zone \"" + archive + "\" " + cznName +
                      " --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 5th golden scene: a real tree, layout codes 11/12 (Position now
// decodable via FLOAT16x3 at +0, spec-vertex-format.md Sec12.12 - HIGH
// CONFIDENCE, NOT CONFIRMED; Normal/Tangent at +8/+12 are CONFIRMED - see
// tests/golden/tree/COMMAND.txt for the full, plainly-stated confidence
// breakdown, deliberately one tier lower than zone_tile's own Position
// field). Supersedes the honest STATUS.txt gap this directory held before
// (HANDOFF.md Sec9.110 keeps that history verbatim). Same shape as
// checkZoneTileScene() above - one real command, one PNG + one stdout
// capture - except the real tool here is tools/tree_baseline_render.cpp.
//
// RE-FROZEN A SECOND TIME the same day (still Sec9.120's own thread,
// closing that section's own Addendum): the FIRST freeze drew only the
// lowest-index present LOD slot's group 0, which turned out (measured
// directly, not assumed - see tests/golden/tree/COMMAND.txt's own "WHAT
// CHANGED AND WHY") to be ONLY the trunk (materialId 0) - st_pine_tall's
// other 2 present materials (branch, materialId 1; pineneedles, materialId
// 2) live in their OWN, SEPARATE tree-level LOD slots (1 and 2), not as
// extra ranges inside slot 0's own draw-group array. tools/
// tree_baseline_render.cpp was extended (this time - the prior freeze's
// own "Files" list explicitly said this tool was untouched) to draw group
// 0 of EVERY PRESENT LOD slot, composited into one frame. Material 3 (the
// billboard impostor) has no present LOD slot in this tree's data at all -
// a real, checked data absence, not a bug. The file paths this function
// checks against did NOT change (same baseline_render.png/baseline_stdout.txt
// names) - only their CONTENT did; the superseded group-0-only baseline is
// preserved alongside under `_group0_only`-suffixed filenames in the same
// directory, per tests/golden/tree/COMMAND.txt's own "Preserved here" list.
SceneResult checkTreeScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "tree (tools/tree_baseline_render, all present LOD slots/materials, layout codes 11/12)";

    const std::string goldenDir = root + "\\tests\\golden\\tree";
    const std::string workDir = root + "\\tests\\golden";
    const std::string toolExe = toolExePath(root, "tree_baseline_render.exe");
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string stem = "st_pine_tall";
    const std::string workStem = "tree";

    if (!fileExists(goldenDir + "\\baseline_render.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(toolExe)) {
        r.status = "FAIL";
        r.details.push_back("tree_baseline_render.exe not found at " + toolExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path ("_work/tree.png", forward slash, positional - this
    // tool takes <out.png> directly, not a --capture flag) that
    // tests/golden/tree/COMMAND.txt records was used to freeze the
    // baseline - tree_baseline_render echoes this argument's basename back
    // verbatim in its own "wrote:" line, so using anything else would make
    // an otherwise-identical stdout report a false mismatch (the same one
    // caller-chosen-path detail every other scene's own COMMAND.txt in
    // this directory already documents).
    std::string cmd = "\"" + toolExe + "\" \"" + archive + "\" " + stem + " _work/" + workStem +
                      ".png > _work\\" + workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("tree_baseline_render.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_render.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("render.png  : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 6th golden scene (follow-on task, 2026-09-30): a real `.clmesh_pc`/
// `.glmesh_pc` static-prop render with real per-material textures - see
// tests/golden/clmesh_lite_fixh/COMMAND.txt for the full real-command/why
// writeup (the spec's own worked example, real texture-binding mechanism
// measurement, honest shader-scope note). NEW scene, not a re-freeze - same
// shape as checkVehicleScene()/checkZoneTileScene() above (one real command,
// one --capture PNG, one stdout capture, through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat produces - extended this
// session to also compile src/level_mesh.cpp/sr3clmesh, see build.bat's own
// new section).
SceneResult checkClmeshLiteFixhScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "clmesh_lite_fixh (sr3_viewer clmesh, real .clmesh_pc texture binding)";

    const std::string goldenDir = root + "\\tests\\golden\\clmesh_lite_fixh";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string clmeshName = "lite_fixh.clmesh_pc";
    const std::string workStem = "clmesh_lite_fixh";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/clmesh_lite_fixh.png") that
    // tests/golden/clmesh_lite_fixh/COMMAND.txt records was used to freeze
    // the baseline - sr3_viewer echoes this argument back verbatim in its
    // "captured:" line, so using anything else would make an otherwise-
    // identical stdout report a false mismatch (the same caller-chosen-path
    // detail every other scene's own COMMAND.txt in this directory
    // documents).
    std::string cmd = "\"" + viewerExe + "\" clmesh \"" + archive + "\" " + clmeshName +
                      " --rendergroup 0 --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 7th golden scene (redispatch of agent `aecbcba5573eda88c`, 2026-09-30):
// a real `.clmesh_pc`/`.glmesh_pc` BUILDING render - see tests/golden/
// clmesh_airport_controltower/COMMAND.txt for the full real-command/why
// writeup (same prop HANDOFF.md Sec9.133/Sec9.136 already rendered and
// independently viewed, just never frozen as its own golden scene until
// now). Exact same shape as checkClmeshLiteFixhScene() above (one real
// command, one --capture PNG, one stdout capture, through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat produces - no build.bat change
// needed, it already compiles src/level_mesh.cpp/sr3clmesh from the Sec9.136
// freeze).
SceneResult checkClmeshAirportControlTowerScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "clmesh_airport_controltower (sr3_viewer clmesh, real .clmesh_pc BUILDING texture binding)";

    const std::string goldenDir = root + "\\tests\\golden\\clmesh_airport_controltower";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string clmeshName = "airport_controltower.clmesh_pc";
    const std::string workStem = "clmesh_airport_controltower";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/clmesh_airport_controltower.png")
    // that tests/golden/clmesh_airport_controltower/COMMAND.txt records was
    // used to freeze the baseline - sr3_viewer echoes this argument back
    // verbatim in its "captured:" line, so using anything else would make an
    // otherwise-identical stdout report a false mismatch (the same caller-
    // chosen-path detail every other scene's own COMMAND.txt in this
    // directory documents).
    std::string cmd = "\"" + viewerExe + "\" clmesh \"" + archive + "\" " + clmeshName +
                      " --rendergroup 0 --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 9th golden scene (2026-10-03, orchestrator request): the new `city`
// command (tools/sr3_viewer.cpp's multi-tile streaming viewer, see
// runCity()'s own top comment) at a fixed camera position over the real
// corner where 4 real hN cells from 3 different tiles (0916, 1016, 1015)
// meet - the box-overlap corner at focus (-960, -700), where cell 1016h0's
// box [-960,-800]x[-840,-700], 0916h1's [-1120,-960]x[-840,-700], 1015h2's
// [-1120,-960]x[-700,-560] and 1015h3's [-960,-800]x[-700,-560] all touch,
// confirmed directly from `city --list`'s own real centres before freezing
// (not assumed). A single still frame, not a scripted flight: this is a
// STATIC regression scene like every other golden here, not a re-run of the
// implementing agent's own flight-log evidence. Same shape as
// checkZoneTileScene()/checkClmeshLiteFixhScene() above (one real `city`
// subcommand, one --capture PNG, one stdout capture, through the SAME fresh
// tests/golden/_bin/sr3_viewer.exe build.bat already produces - no build.bat
// change needed, `city` uses only readers that script already compiles for
// `zone`/`clmesh`) - EXCEPT the stdout comparison uses
// compareTextMaskingTiming() instead of compareTextExact(): `city` is the
// first golden command whose own stdout embeds a real measured wall-clock
// duration per line (the per-cell "NNN ms" load time, the one-time texture-
// cache "indexed in N.N s" build time), a genuine, directly-confirmed source
// of run-to-run non-determinism in TEXT even though the rendered PNG itself
// is still byte-identical (see maskTimingText()'s own doc comment for the
// confirmation). Within-cell prop placement is still the CHOSEN fake local
// grid every other clmesh/city scene already uses (NOT real - see
// runCity()'s own top comment) - this golden catches a regression in BOTH
// which real cells stream in/out (tile lattice, box rule) AND how their real
// textures resolve (the cross-archive texture-fallback this merge added),
// the orchestrator's own stated reason for freezing this scene.
SceneResult checkCityCornerScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "city_corner (sr3_viewer city, 4-cell real tile-boundary corner, real textures)";

    const std::string goldenDir = root + "\\tests\\golden\\city_corner";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string workStem = "city_corner";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/city_corner.png") and the same
    // --eye/--yaw/--pitch the baseline was frozen with (tests/golden/
    // city_corner/COMMAND.txt records the exact command) - sr3_viewer echoes
    // the --capture argument back verbatim in its own "captured:" line, same
    // caller-chosen-path convention every other scene here documents.
    // --no-input makes the run deterministic (ignores the live keyboard);
    // --frames 1 --capture-frame 0 captures the first (only) frame.
    std::string cmd = "\"" + viewerExe + "\" city \"" + archive +
                      "\" --eye -960 250 -700 --yaw 0 --pitch -75 --no-input --frames 1 --capture _work/" +
                      workStem + ".png --capture-frame 0 > _work\\" + workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextMaskingTiming(goldenDir + "\\baseline_stdout.txt",
                                       workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The `ambient_population_demo` golden scene (spec-ai-runtime.md Sec17,
// this task 2026-10-08): a dedicated, deterministic, headless demo tool
// (tools/ambient_population_demo.cpp, NOT sr3_viewer - this scene needs no
// archive/dataRoot at all, see that tool's own top comment for why a NEW
// scene rather than extending city_corner above) exercising the real
// distant ambient pedestrian/vehicle spawn-despawn algorithm
// (include/sr3airuntime/ambient_population.h) end to end against a
// clearly-labelled synthetic stand-in road/sidewalk grid, and capturing a
// schematic top-down frame of the result. `dataRoot` is accepted only for
// call-site uniformity with every other checkXScene() here - this tool
// takes no archive argument.
SceneResult checkAmbientPopulationDemoScene(const std::string& root, const std::string& dataRoot) {
    (void)dataRoot;
    SceneResult r;
    r.name = "ambient_population_demo (distant ped/vehicle spawn-despawn, spec-ai-runtime.md Sec17, schematic top-down)";

    const std::string goldenDir = root + "\\tests\\golden\\ambient_population_demo";
    const std::string workDir = root + "\\tests\\golden";
    const std::string toolExe = toolExePath(root, "ambient_population_demo.exe");
    const std::string workStem = "ambient_population_demo";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(toolExe)) {
        r.status = "FAIL";
        r.details.push_back("ambient_population_demo.exe not found at " + toolExe +
                            " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same --frames/--capture arguments (forward-slash "_work/
    // ambient_population_demo") that tests/golden/ambient_population_demo/
    // COMMAND.txt records were used to freeze the baseline - this tool
    // takes no archive/eye/yaw/pitch arguments at all (fully synthetic, no
    // real zone data read), same caller-chosen-path convention every other
    // scene here documents.
    std::string cmd = "\"" + toolExe + "\" --frames 1200 --capture _work/" + workStem + " > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("ambient_population_demo.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    // Plain byte-exact compare (NOT compareTextMaskingTiming) - this tool
    // creates no D3D11 device and reads no wall clock anywhere (confirmed
    // directly: tests/golden/ambient_population_demo/COMMAND.txt records
    // running the exact same command twice and diffing both outputs
    // byte-identical), so there is no timing text to mask in the first
    // place.
    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt", workDir + "\\_work\\" + workStem + "_check_stdout.txt",
                               report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The `walk_demo` golden scene (2026-10-07): the new `walk` command
// (tools/sr3_viewer.cpp's runWalkDemo() - see its own doc comment for the
// full REAL/CHOSEN/judgement-call breakdown) - brad driven by the real
// sr3playeranim::PlayerAnimBridge/"player state machine" node graph through
// a short scripted on-foot sequence (40 frames forward, 20 "turning", 20
// stopped), placed on the real city_corner ground, captured at the last
// frame (fully stopped). Same shape as checkCityCornerScene() above - one
// real `walk` subcommand, one --capture PNG, one stdout capture, through
// the SAME tests/golden/_bin/sr3_viewer.exe build.bat already produces
// (build.bat DOES need a change for this one, unlike `city`/`zone`/
// `clmesh`: `walk` is the first golden command needing sr3player's
// movement/locomotion/camera translation units [player_follow_camera.cpp
// included - see build.bat's own comment on why a command that never calls
// CameraSystem::update() still needs it] + sr3playeranim + sr3animruntime's
// own node-graph sources + sr3tables_animation) - and the SAME
// compareTextMaskingTiming() stdout treatment as `city_corner`: `walk`
// reuses runCity()'s own loadCell() verbatim, so its stdout embeds the
// identical real per-cell "NNN ms" load time lines city_corner's own
// baseline already established as a genuine, directly-confirmed source of
// run-to-run timing jitter even though the rendered PNG itself stays
// byte-identical.
//
// CAMERA - CHOSEN DEVIATION from the task's own initial reuse recipe
// (documented here, not silently done): the literal recipe was
// city_corner's own framing, `--eye -960 250 -700 --yaw 0 --pitch -75`.
// Measured directly (decoding the captured PNG and locating the character
// by its own real simulated world position via the SAME eye/right/up/
// forward math runWalkDemo()'s own buildFreeViewProjection uses): at that
// literal framing the character is a real, correctly-positioned, but
// EXACTLY 1x4 pixel sliver in a 1400x900 frame - present and provably
// right (its on-screen location matches the hand-computed camera
// projection of its own real end-of-sequence position to the pixel), but
// not a "visible milestone" by any human-legible standard. Since this
// specific scene's whole point (per the task brief) is a VISIBLE character-
// walking demo, not a ground/texture regression check (`city_corner`
// already owns that job), the capture here instead uses a close, oblique
// framing chosen by direct iteration against the real render (7 candidates
// tried; recorded in this session's own working notes) - `--eye -957.66 15
// -691.66 --yaw -20 --pitch -55` - which renders brad as a clearly
// recognisable ~40x70 px checkered humanoid figure on real city_corner
// ground (a highway-ramp support wall, a dumpster, a building - all real
// streamed geometry/textures, not fabricated), still right next to the
// SAME real start corner city_corner's own golden names. The command's own
// --eye/--yaw/--pitch default flags still match city_corner's literal
// framing (so a caller who wants that framing back gets it with no flags),
// and the `walk` command's own stdout banner states this measurement
// plainly - this override is specific to freezing a legible golden image,
// not a claim that the default framing is wrong for its own purpose.
SceneResult checkWalkDemoScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "walk_demo (sr3_viewer walk, player<->animation bridge, city_corner ground)";

    const std::string goldenDir = root + "\\tests\\golden\\walk_demo";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string charactersArchive = dataRoot + "\\characters.vpp_pc";
    const std::string miscTablesArchive = dataRoot + "\\misc_tables.vpp_pc";
    const std::string cityArchive = dataRoot + "\\sr3_city_0.vpp_pc";
    const std::string workStem = "walk_demo";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path / --eye/--yaw/--pitch/--frames/--capture-frame the
    // baseline was frozen with (tests/golden/walk_demo/COMMAND.txt records
    // the exact command) - sr3_viewer echoes --capture back verbatim in its
    // own "captured:" line, same caller-chosen-path convention every other
    // scene here documents. --no-input makes the run deterministic.
    std::string cmd = "\"" + viewerExe + "\" walk \"" + charactersArchive + "\" \"" + miscTablesArchive + "\" \"" +
                      cityArchive +
                      "\" --eye -957.66 15 -691.66 --yaw -20 --pitch -55 --no-input --frames 80 --capture _work/" +
                      workStem + ".png --capture-frame 79 > _work\\" + workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextMaskingTiming(goldenDir + "\\baseline_stdout.txt",
                                       workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The `walk_demo_v2` golden scene (2026-10-07 follow-up task): `walk --v2`
// (tools/sr3_viewer.cpp's runWalkDemo() - see its own "V2" doc block for the
// full REAL/CHOSEN/FIX breakdown) fixes three real problems a review of the
// v1 walk_demo baseline above found - brad's real per-material diffuse
// textures bound instead of flat Checker (FIX 1), the capture rendered from
// the real sr3player::CameraSystem follow camera's own COMMITTED output
// instead of a hand-picked debug --eye/--yaw/--pitch (FIX 2), and the city
// streaming keyed off brad's own live position instead of the free-fly
// debug eye (FIX 3) - captured across FOUR frames (0/15/30/45) via the new
// --capture-frames flag, not just one end-state frame, so the walk is
// actually visible as a short sequence. This is a NEW golden directory, not
// a re-freeze of tests/golden/walk_demo/ in place - see
// tests/golden/walk_demo_v2/COMMAND.txt's own header for why (the artifact
// SHAPE changed, four PNGs instead of one, matching this project's
// "separate directory per real iteration" precedent rather than the
// suffixed-files-in-place one). checkWalkDemoScene() above is UNCHANGED and
// continues to check the untouched v1 baseline - confirmed directly this
// session (see that COMMAND.txt's own "Regression" section): the exact v1
// command re-run against this task's own rebuilt sr3_viewer.exe is
// byte-identical to tests/golden/walk_demo/baseline_capture.png.
// build.bat needed NO edit for --v2 (checked directly, see
// tests/golden/walk_demo_v2/COMMAND.txt): every object --v2's new code
// calls into (player_camera.obj/player_follow_camera.obj,
// tables_environment.obj/xtbl.obj, material_block.obj/geometry_block.obj/
// material_binding.obj) was already linked into sr3_viewer.exe for v1's own
// `walk` command; the one new header (camera_free_seed.h) is header-only.
SceneResult checkWalkDemoV2Scene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "walk_demo_v2 (sr3_viewer walk --v2, real follow camera + real brad textures + position-driven streaming)";

    const std::string goldenDir = root + "\\tests\\golden\\walk_demo_v2";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string charactersArchive = dataRoot + "\\characters.vpp_pc";
    const std::string miscTablesArchive = dataRoot + "\\misc_tables.vpp_pc";
    const std::string cityArchive = dataRoot + "\\sr3_city_0.vpp_pc";
    // SAME stem the baseline was frozen with (tests/golden/walk_demo_v2/
    // COMMAND.txt: "--capture _work/walk_demo_v2") - sr3_viewer echoes
    // --capture back verbatim in its own "captured:" line per frame, so a
    // different stem here would make an otherwise byte-identical stdout a
    // false mismatch (the same lesson checkWalkDemoScene() above already
    // documents for its own single-frame workStem).
    const std::string workStem = "walk_demo_v2";
    static const int kFrames[] = {0, 15, 30, 45};

    bool haveBaseline = fileExists(goldenDir + "\\baseline_stdout.txt");
    for (int f : kFrames)
        haveBaseline = haveBaseline && fileExists(goldenDir + "\\baseline_capture_frame" + std::to_string(f) + ".png");
    if (!haveBaseline) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same --v2/--capture-frames shape tests/golden/walk_demo_v2/COMMAND.txt
    // records was used to freeze the baseline - no --eye/--yaw/--pitch
    // (--v2 ignores them for the render view, see that COMMAND.txt's own
    // note). sr3_viewer echoes --capture back verbatim per captured frame
    // ("_frameN.png" appended), same caller-chosen-path convention every
    // other scene here documents.
    std::string cmd = "\"" + viewerExe + "\" walk \"" + charactersArchive + "\" \"" + miscTablesArchive + "\" \"" +
                      cityArchive + "\" --no-input --frames 80 --v2 --capture _work/" + workStem +
                      " --capture-frames 0,15,30,45 > _work\\" + workStem + "_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    for (int f : kFrames) {
        const std::string suffix = "_frame" + std::to_string(f) + ".png";
        bool p = comparePngExact(goldenDir + "\\baseline_capture_frame" + std::to_string(f) + ".png",
                                 workDir + "\\_work\\" + workStem + suffix, report);
        r.details.push_back("capture frame " + std::to_string(f) + " : " + (p ? "PASS - " : "FAIL - ") + report);
        allPass = allPass && p;
    }

    bool pStdout = compareTextMaskingTiming(goldenDir + "\\baseline_stdout.txt",
                                            workDir + "\\_work\\" + workStem + "_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (pStdout ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && pStdout;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 8th golden scene, RE-FROZEN (orchestrator task, 2026-09-30, verifying
// two leads against real bytes): a real 3-pass deferred-LIT `.clmesh_pc`
// BUILDING render (G-buffer prepass -> real directional light -> real
// material pass) - see tests/golden/clmesh_airport_controltower_lit/
// COMMAND.txt for the full real-command/why writeup of the CURRENT baseline,
// and COMMAND_sparse_silhouette_v1.txt for the ORIGINAL (preserved, not
// overwritten - this project's own re-freeze convention, precedent
// HANDOFF.md Sec9.124/Sec9.127) sparse-silhouette baseline this one
// replaces. The tool changed too: tools/prototype_lit_clmesh_tower2.cpp (a
// NEW fork of tools/prototype_lit_clmesh_tower.cpp, which is itself
// untouched - "fork, don't modify") generalizes VS/PS resolution (Lead 1)
// and wires real per-material B/C shader constants (Lead 2) into the same
// pipeline shape - see that file's own top comment. Same shape as
// checkTreeScene() above otherwise (one real STANDALONE tool taking a
// single positional out-path argument, no --capture flag), through the SAME
// fresh tests/golden/_bin build tests/golden/build.bat produces (extended
// this session with a new section for prototype_lit_clmesh_tower2.exe).
SceneResult checkClmeshAirportControlTowerLitScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "clmesh_airport_controltower_lit (prototype_lit_clmesh_tower2, re-frozen real 3-pass deferred-lit .clmesh_pc render)";
    (void)dataRoot; // this tool hardcodes its own real archive path, like every prototype_*.cpp in this family

    const std::string goldenDir = root + "\\tests\\golden\\clmesh_airport_controltower_lit";
    const std::string workDir = root + "\\tests\\golden";
    const std::string toolExe = toolExePath(root, "prototype_lit_clmesh_tower2.exe");
    const std::string workStem = "clmesh_airport_controltower_lit";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(toolExe)) {
        r.status = "FAIL";
        r.details.push_back("prototype_lit_clmesh_tower2.exe not found at " + toolExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path (forward slash, "_work/clmesh_airport_controltower_lit
    // .png") that tests/golden/clmesh_airport_controltower_lit/COMMAND.txt
    // records was used to freeze the baseline - this tool echoes this
    // argument back verbatim in its own "[png] wrote ..." line, so using
    // anything else would make an otherwise-identical stdout report a false
    // mismatch (the same caller-chosen-path detail every other scene in
    // this directory already documents).
    std::string cmd = "\"" + toolExe + "\" _work/" + workStem + ".png > _work\\" + workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("prototype_lit_clmesh_tower.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// The 10th golden scene (2026-10-03): the FIRST Vint UI render - `sr3_viewer
// vint-doc` on the real shipped HUD document hud_running_man.vint_doc
// (interface_startup.vpp_pc), every `bitmap` element drawn as a real textured
// quad from real bitmap-sheet sub-images (spec-vint-render.md Sec12.1/
// Sec12.4, sheets per spec-tables-environment.md Sec17.4), `group` drawing
// nothing (Sec12.3), `text` only a labelled PLACEHOLDER marker - see
// tests/golden/vint_hud_running_man/COMMAND.txt for the real command, why
// this document, and every CHOSEN value the frame depends on, and
// runVintDoc()'s own top comment in tools/sr3_viewer.cpp. Same shape as
// checkClmeshAirportControlTowerScene() (one real sr3_viewer subcommand, a
// positional out-path PNG, one stdout capture, byte-exact on both - this
// command prints no wall-clock timing, two runs were byte-identical apart
// from the echoed out path), through the SAME fresh tests/golden/_bin/
// sr3_viewer.exe build.bat produces (extended with a `vint-doc` section:
// sr3vintdoc + sr3luahost + Lua 5.1, packed into vint_luahost.lib).
SceneResult checkVintHudRunningManScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "vint_hud_running_man (sr3_viewer vint-doc, Vint UI render: real bitmap quads + real text, Sec12/Sec14)";

    const std::string goldenDir = root + "\\tests\\golden\\vint_hud_running_man";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string workStem = "vint_hud_running_man";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out path ("_work/vint_hud_running_man.png", echoed verbatim in the
    // command's own "wrote" line) and the same archives/flags the baseline was
    // frozen with (COMMAND.txt records the exact command).
    std::string cmd = "\"" + viewerExe + "\" vint-doc \"" + dataRoot + "\\interface_startup.vpp_pc\" " +
                      "hud_running_man.vint_doc \"" + dataRoot + "\\misc_tables.vpp_pc\" _work/" + workStem +
                      ".png --tables \"" + dataRoot + "\\patch_compressed.vpp_pc\" --textures \"" + dataRoot +
                      "\\misc.vpp_pc\" --textures \"" + dataRoot + "\\interface.vpp_pc\" > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// hud.vint_doc: the main HUD document, covering real stencil masking (7
// mask elements) and non-filter render_mode blend codes (15 elements) -
// spec-vint-render.md Sec15 (UI draw unit U6). hud_running_man.vint_doc's
// own golden exercises neither (COMMAND.txt's own "why this document"
// note), so this is the dedicated regression coverage for both of U6's
// biggest additions, same pattern as checkVintHudRunningManScene above.
SceneResult checkHudMaskScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "hud_mask (sr3_viewer vint-doc hud.vint_doc: real stencil masking + non-filter render_mode blend codes + real text, Sec15.4/15.6/Sec14)";

    const std::string goldenDir = root + "\\tests\\golden\\hud_mask";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string workStem = "hud_mask";

    if (!fileExists(goldenDir + "\\baseline_capture.png") || !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    std::string cmd = "\"" + viewerExe + "\" vint-doc \"" + dataRoot + "\\interface_startup.vpp_pc\" " +
                      "hud.vint_doc \"" + dataRoot + "\\misc_tables.vpp_pc\" _work/" + workStem +
                      ".png --tables \"" + dataRoot + "\\patch_compressed.vpp_pc\" --textures \"" + dataRoot +
                      "\\misc.vpp_pc\" --textures \"" + dataRoot + "\\interface.vpp_pc\" > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_capture.png", workDir + "\\_work\\" + workStem + ".png", report);
    r.details.push_back(std::string("capture.png : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// hud-live-demo (spec-hud-runtime.md, feature/hud-live-values task): the
// focused health/ammo/money/notoriety overlay, driven by the REAL producer
// natives (get_current_hit_points/set_current_hit_points/get_max_hit_points,
// cash_add, inv_weapon_add_temporary/game_hud_update_inventory,
// notoriety_set/on_notoriety_event) through a real Host/EngineState, NOT the
// hud_display_*/hud_bar_* pools spec-hud-runtime.md Sec1 CONFIRMS are ruled
// out as the core-stat path. 6 frames in one run (see
// tests/golden/hud_live_demo/COMMAND.txt for what each one proves,
// frame3_ammo_pending's own byte-identical-to-frame2_cash pixel proof of the
// event-driven ammo cadence in particular) - every PNG plus the one shared
// stdout compared exactly, same convention as checkHudMaskScene() above.
SceneResult checkHudLiveDemoScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "hud_live_demo (sr3_viewer hud-live-demo: live core-stat HUD overlay, spec-hud-runtime.md)";

    const std::string goldenDir = root + "\\tests\\golden\\hud_live_demo";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string workStem = "hud_live_demo";
    static const char* const kFrames[] = {"frame0_baseline", "frame1_damage",       "frame2_cash",
                                          "frame3_ammo_pending", "frame4_ammo_updated", "frame5_notoriety"};

    if (!fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    for (const char* f : kFrames) {
        if (!fileExists(goldenDir + "\\baseline_" + f + ".png")) {
            r.status = "SKIP";
            r.details.push_back("no frozen baseline_" + std::string(f) + ".png under " + goldenDir);
            return r;
        }
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    std::string cmd = "\"" + viewerExe + "\" hud-live-demo \"" + root + "\\tools\\lua_all_registered_1490_tagged.txt\" " +
                      "_work/" + workStem + " --textures \"" + dataRoot + "\\misc.vpp_pc\" > _work\\" + workStem +
                      "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    for (const char* f : kFrames) {
        bool p = comparePngExact(goldenDir + "\\baseline_" + f + ".png",
                                 workDir + "\\_work\\" + workStem + "_" + f + ".png", report);
        r.details.push_back(std::string(f) + ".png : " + (p ? "PASS - " : "FAIL - ") + report);
        allPass = allPass && p;
    }
    bool p2 = compareTextExact(goldenDir + "\\baseline_stdout.txt",
                               workDir + "\\_work\\" + workStem + "_check_stdout.txt", report);
    r.details.push_back(std::string("stdout      : ") + (p2 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p2;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

} // namespace golden_scene_check
