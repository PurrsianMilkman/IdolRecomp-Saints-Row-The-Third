// golden_scene_check_scenes_vehicle.cpp
// Scene checks: Brad, vehicles, drive-demo coverage.
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

SceneResult checkBradScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "brad (single-bone pose, sr3_viewer)";

    const std::string goldenDir = root + "\\tests\\golden\\brad";
    const std::string workDir = root + "\\tests\\golden";
    // tests/golden/_bin/sr3_viewer.exe (WARP-migration task, 2026-10-07),
    // NOT build_verify/sr3_viewer.exe: this is the one scene in this file
    // that had been left on the stale build_verify/ binary since before
    // every other scene here moved to the fresh tests/golden/_bin/ build
    // (see checkVehicleScene()'s own comment on exactly this same lesson
    // for `vehicle`) - confirmed directly a real problem for THIS task
    // specifically: build_verify/sr3_viewer.exe (last built 2026-09-29)
    // predates src/device.cpp's SR3_FORCE_WARP support entirely, so it
    // silently ignores SR3_FORCE_WARP and always renders hardware
    // regardless of this harness's own forced-WARP env var - the first
    // freeze attempt here under this task produced a baseline whose own
    // "device" stdout line read HARDWARE even with SR3_FORCE_WARP=1 set,
    // caught by re-reading the frozen baseline_stdout.txt right after
    // freezing it rather than trusting the process silently. Pointing
    // brad at the same fresh, WARP-aware tests/golden/_bin/sr3_viewer.exe
    // every other scene already uses fixes this and brings brad in line
    // with this file's own standing convention.
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\characters.vpp_pc";

    if (!fileExists(goldenDir + "\\baseline_bind.png") || !fileExists(goldenDir + "\\baseline_posed.png") ||
        !fileExists(goldenDir + "\\baseline_stdout.txt")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe);
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    // Same out_prefix ("_work/brad", forward slash) that COMMAND.txt records
    // was used to freeze the baseline - sr3_viewer echoes this argument back
    // verbatim in its "wrote:" lines, so using anything else (a different
    // name, or backslashes) would make an otherwise-identical stdout report
    // a false mismatch. This is the one caller-chosen-path detail in an
    // otherwise fully real, unedited stdout capture.
    std::string cmd = "\"" + viewerExe + "\" pose \"" + archive +
                      "\" brad.ccmesh_pc brad.rig_pc _work/brad > _work\\brad_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    bool allPass = true;
    std::string report;
    bool p1 = comparePngExact(goldenDir + "\\baseline_bind.png", workDir + "\\_work\\brad_bind.png", report);
    r.details.push_back(std::string("bind.png  : ") + (p1 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p1;

    bool p2 =
        comparePngExact(goldenDir + "\\baseline_posed.png", workDir + "\\_work\\brad_posed.png", report);
    r.details.push_back(std::string("posed.png : ") + (p2 ? "PASS - " : "FAIL - ") + report);
    allPass = allPass && p2;

    bool p3 = compareTextExact(goldenDir + "\\baseline_stdout.txt", workDir + "\\_work\\brad_check_stdout.txt",
                               report);
    r.details.push_back(std::string("stdout    : ") + (p3 ? "PASS - " : "FAIL -\n" + report));
    allPass = allPass && p3;

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

// Shared by checkVehicleGenkiScene()/checkVehicleStandardScene() below - both
// are the same shape as checkBradScene() above (one archive, one .ccar_pc,
// the shared shaders.vpp_pc, one --capture PNG, one stdout capture), just
// parameterised over the bits that differ per vehicle. See
// tests/golden/vehicle_genki/COMMAND.txt and tests/golden/vehicle_standard/
// COMMAND.txt for why each vehicle's own archive/group was chosen.
SceneResult checkVehicleScene(const std::string& root, const std::string& dataRoot, const std::string& sceneName,
                              const std::string& sceneDir, const std::string& vehicleArchive,
                              const std::string& ccarName, const std::string& workStem) {
    SceneResult r;
    r.name = sceneName + " (sr3_viewer vehicle)";

    const std::string goldenDir = root + "\\tests\\golden\\" + sceneDir;
    const std::string workDir = root + "\\tests\\golden";
    // NOT build_verify\sr3_viewer.exe: that binary predates this session's
    // `vehicle` command and does not recognise it ("unknown command:
    // vehicle", confirmed directly) - see tests/golden/build.bat, which
    // builds this exe fresh from the unmodified tools/sr3_viewer.cpp instead.
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    const std::string archive = dataRoot + "\\" + vehicleArchive;
    const std::string shadersArchive = dataRoot + "\\shaders.vpp_pc";

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
    // Same out path (forward slash, "_work/<workStem>.png") that each
    // scene's own COMMAND.txt records was used to freeze the baseline -
    // sr3_viewer echoes this argument back verbatim in its "captured:" line,
    // so using anything else would make an otherwise-identical stdout report
    // a false mismatch (the same one caller-chosen-path detail
    // brad/COMMAND.txt already documents for its own baseline).
    std::string cmd = "\"" + viewerExe + "\" vehicle \"" + archive + "\" " + ccarName + " \"" + shadersArchive +
                      "\" --group 0 --frames 1 --capture _work/" + workStem + ".png --capture-frame 0 > _work\\" +
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

SceneResult checkVehicleGenkiScene(const std::string& root, const std::string& dataRoot) {
    return checkVehicleScene(root, dataRoot, "vehicle_genki", "vehicle_genki", "dlc1.vpp_pc",
                             "car_4dr_genki_0.ccar_pc", "vehicle_genki");
}

SceneResult checkVehicleStandardScene(const std::string& root, const std::string& dataRoot) {
    return checkVehicleScene(root, dataRoot, "vehicle_standard", "vehicle_standard", "vehicles.vpp_pc",
                             "car_4dr_standard03_4.ccar_pc", "vehicle_standard");
}

// ---------------------------------------------------------------------------
// drive_demo coverage/bbox regression (this task, 2026-10-08 - the
// orchestrator's own explicit request after the drive_demo v2 mesh fix
// merged to main with an incomplete follow-through: it masked the vehicle's
// single upload()/draw() call down to draw-group-0's genuinely-channel-0
// ranges, which stopped an index-buffer corruption bug but ALSO silently
// dropped the OTHER channels' real geometry - the car's middle body
// (doors/cabin walls, submeshIndex 1/2) - with nothing automated catching
// it. A human had to notice a missing car body from a screenshot
// (HANDOFF.md). This check exists so that class of regression fails a
// build, not a glance.
//
// WHY coverage/bbox, not byte-exact (every OTHER scene in this file IS
// byte-exact - see this file's own COMPARISON POLICY note up top): a
// byte-exact compare is strictly stronger when it is usable, and drive_demo
// frame 0 of the scripted straight-phase sequence IS fully deterministic
// (fixed VehicleTuning fixture + fixed start pose, confirmed by this
// project's own existing 125/125 ctest baseline never flaking) - so this
// check ALSO freezes and compares its own self-baseline byte-exactly
// (comparePngExact below), the real PRIMARY regression gate. What a
// self-only compare cannot give is an independent cross-check: this file's
// own MeshRenderer/D3D11 state/shader source is shared code every scene
// depends on, so a self-only compare could pass even if some OTHER shared
// regression broke BOTH the frozen baseline and every fresh render the same
// way. tests/golden/vehicle_genki/baseline_capture.png renders the EXACT
// SAME real vehicle (car_4dr_genki_0.ccar_pc) through a genuinely
// independent path (the `vehicle` command's own loadVehicleObject(), a
// different camera - a fixed yaw/pitch/distance orbit around the mesh's own
// bounds, not a trailing chase - and a different resolution), so comparing
// COVERAGE/BBOX FRACTIONS (of each image's own frame, not absolute pixel
// counts - the two renders are not otherwise comparable) against it is a
// genuinely independent secondary signal: this is the orchestrator's own
// explicitly requested cross-check, deliberately loose (see
// kMinCrossVehicleCoverageRatio below) since it exists to catch "the car is
// essentially gone", not to replace the tight self-baseline gate above.
//
// Background pixels are detected by sampling the image's own corner pixel
// (every scene in this file clears to one flat colour before drawing
// anything - confirmed by reading every swapChain.clear() call site - and
// drive_demo's own --no-city car-alone frame never covers its own corner)
// rather than hardcoding a clear-colour constant here, so this check does
// not need to track runDriveDemo()'s own clear-colour literal separately.
struct CoverageStats {
    bool ok = false;
    uint32_t width = 0, height = 0;
    uint64_t coveredPixels = 0;
    uint32_t minX = 0, minY = 0, maxX = 0, maxY = 0; // valid only when coveredPixels > 0
    double coverageFraction() const {
        return (width > 0 && height > 0)
                   ? static_cast<double>(coveredPixels) / (static_cast<double>(width) * static_cast<double>(height))
                   : 0.0;
    }
    double bboxWidthFraction() const {
        return (width > 0 && coveredPixels > 0) ? static_cast<double>(maxX - minX + 1) / static_cast<double>(width)
                                                 : 0.0;
    }
    double bboxHeightFraction() const {
        return (height > 0 && coveredPixels > 0)
                   ? static_cast<double>(maxY - minY + 1) / static_cast<double>(height)
                   : 0.0;
    }
};

CoverageStats computeCoverageStats(const std::string& pngPath, std::string& error) {
    CoverageStats s;
    std::vector<uint8_t> rgba;
    if (!decodeOwnPng(pngPath, s.width, s.height, rgba, error)) return s;
    if (s.width == 0 || s.height == 0 || rgba.size() < 4) {
        error = "zero-sized image: " + pngPath;
        return s;
    }
    const uint8_t bg[4] = {rgba[0], rgba[1], rgba[2], rgba[3]}; // top-left corner, see this block's own doc comment
    uint32_t minX = s.width, minY = s.height, maxX = 0, maxY = 0;
    uint64_t covered = 0;
    for (uint32_t y = 0; y < s.height; ++y) {
        for (uint32_t x = 0; x < s.width; ++x) {
            const uint8_t* p = &rgba[(static_cast<size_t>(y) * s.width + x) * 4];
            if (p[0] != bg[0] || p[1] != bg[1] || p[2] != bg[2] || p[3] != bg[3]) {
                ++covered;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    s.coveredPixels = covered;
    if (covered > 0) {
        s.minX = minX;
        s.minY = minY;
        s.maxX = maxX;
        s.maxY = maxY;
    }
    s.ok = true;
    return s;
}

// ---------------------------------------------------------------------
// Wheel-specific coverage gate (this task, 2026-10-08 - the orchestrator's
// own direct instruction: "a car with no wheels now FAILS it", since the
// existing coverage/bbox gates above all passed on a wheel-less car - the
// drive_demo_coverage self-baseline itself was frozen BEFORE wheels were
// ever attached). Unlike the gates above (which compare the FRESH capture
// against a frozen baseline/cross-vehicle reference), this one inspects
// the FRESH capture's OWN bbox SHAPE directly, independent of any
// baseline - so it keeps failing a future wheel-less regression even if
// some later change ever re-freezes a baseline without wheels in it.
//
// Heuristic, chosen AFTER a simpler one was tried and measured to fail
// (see below): wheels sit BELOW the car body, so a car WITH wheels has a
// taller bounding box RELATIVE TO ITS OWN WIDTH than the same car WITHOUT
// them - the body's own width barely changes (wheels tuck inside the
// body's own left/right silhouette from this side-on camera), only the
// height grows. kMinWheelHeightToWidthRatio is CHOSEN but MEASURED, not
// guessed: this task's own real captures of the exact same vehicle/camera
// (sr3_viewer drive_demo --no-city --frames 1 --capture-frames 0) gave a
// bbox of 478x208 px (ratio 0.435) WITH wheels attached and 478x136 px
// (ratio 0.285) when run with --no-wheels (simulating exactly the
// regression this gate exists to catch) - width is UNCHANGED (478 both
// times) and height alone carries the whole signal. 0.35 sits
// comfortably between the two with margin on both sides.
//
// A FIRST VERSION OF THIS GATE WAS WRONG, not hypothetically but
// measured: it sampled the bottom 35% of the vehicle's own bbox HEIGHT,
// split left/right, and required each half to carry some minimum pixel
// count - this looked reasonable but FAILED TO DISCRIMINATE when actually
// run against a real --no-wheels capture (left=9558px, right=9739px,
// comfortably "passing" a car with zero wheels), because removing the
// wheels also SHRINKS the bbox the band itself is measured against, so
// "the bottom 35%" silently re-anchors onto the solid body instead of the
// now-absent wheel region. Replaced with this absolute-ratio approach
// instead of only widening the old thresholds, once the first version's
// own false-pass was directly observed (not just reasoned about).
//
// RECALIBRATED (2026-10-08, the wheel-PLACEMENT task, not the original
// wheel-ATTACHMENT task above): the original 0.35 threshold was measured
// against a build with a real double-translation bug (see
// tools/sr3_viewer.cpp's own vehicleBasePositions doc comment) that
// dragged the car's body down and sideways, artificially INFLATING the
// with-wheels bbox height (478x208px, ratio 0.435) relative to a correctly
// fitted one - once that bug was fixed, the genuinely correct "with
// wheels" capture measures SMALLER (520x170px, ratio 0.327, this task's
// own direct measurement) because a wheel properly tucked inside its arch
// contributes much less extra silhouette height than one hanging
// disconnected below the body - and 0.327 now sits BELOW the old 0.35
// threshold, which would make this gate FAIL on the correct, fixed render.
// Re-measured both ends on the FIXED binary: with wheels 520x170px (ratio
// 0.327) vs --no-wheels 520x153px (ratio 0.294, width unchanged as before)
// - a much narrower real margin (0.033, vs the original 0.15) because
// correctly-fitted wheels are mostly HIDDEN behind the body from this
// side-on camera, which is itself evidence the fix is working, not gate
// fragility. 0.31 sits in that narrower real band with a small margin on
// both sides. This gate's own original purpose - catching "wheels entirely
// absent" - is now a secondary signal: the silhouette-connectivity gate
// below is the PRIMARY, much stronger check for the specific "wheels
// attached but disconnected/misplaced" regression class this task fixed.
constexpr double kMinWheelHeightToWidthRatio = 0.31;

// ---------------------------------------------------------------------
// Silhouette connectivity gate (this task, 2026-10-08 - the orchestrator's
// own direct instruction: "tighten this scene's own coverage test so
// wheels sitting outside the body's expected lower bbox band FAIL the
// test", after a real wheel-placement regression (a double-translation
// bug - see tools/sr3_viewer.cpp's own vehicleBasePositions doc comment)
// shipped with every existing gate above, including the wheel-aspect-ratio
// one, still PASSING: a wheel dangling disconnected well below the body is
// STILL "taller" than a wheel-less car, so that gate's own signal (bbox
// height/width ratio) cannot tell "wheels attached in the right place"
// apart from "wheels attached in the wrong place" - only "wheels attached
// somewhere" vs "no wheels at all". This gate measures the thing the
// orchestrator actually asked about: silhouette CONNECTIVITY.
//
// MEASURED (this task, real captures, not guessed): an 8-connected flood
// fill over the fresh capture's own silhouette (same background-corner-
// sample rule computeCoverageStats already uses). On this task's own
// FIXED capture (sr3_viewer drive_demo --no-city --frames 1
// --capture-frames 0, the exact command this scene runs below), the whole
// vehicle - body AND both wheels - is ONE connected silhouette piece: 1
// component, area 58,579 px, bbox (362,350)-(881,519). The real pre-fix
// regression (temporarily reintroduced this task to prove this gate
// catches it - see the commit/report for the restore step) produces THREE
// disjoint components instead on the identical command: the body alone
// (area 45,192 px, bbox (460,312)-(937,452)) plus two separately-floating
// wheel blobs, each fully surrounded by background with not one shared
// pixel against the body (area 3,089 px, bbox (777,456)-(838,519); area
// 3,031 px, bbox (478,454)-(537,516)). kMinSilhouetteComponentAreaPx is
// set with a wide margin below the smaller real wheel blob (3,031 px) and
// well above plausible anti-aliasing noise - the measured noise floor is
// exactly ZERO in both real captures (no stray small components were
// found at all; antenna flags/trim render pixel-connected to the body
// already, not as separate flecks).
constexpr uint64_t kMinSilhouetteComponentAreaPx = 200;

struct SilhouetteComponent {
    uint64_t area = 0;
    uint32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
};

// Counts disjoint (8-connected) non-background regions at or above
// kMinSilhouetteComponentAreaPx, largest-first - see
// kMinSilhouetteComponentAreaPx's own doc comment for the real measured
// numbers behind the threshold. Same background-detection rule as
// computeCoverageStats (top-left corner pixel; every scene in this file
// clears to one flat colour before drawing, see that function's own doc
// comment).
std::vector<SilhouetteComponent> computeSilhouetteComponents(const std::string& pngPath, std::string& error) {
    std::vector<SilhouetteComponent> result;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
    if (!decodeOwnPng(pngPath, width, height, rgba, error)) return result;
    if (width == 0 || height == 0 || rgba.size() < 4) {
        error = "zero-sized image: " + pngPath;
        return result;
    }
    const uint8_t bg[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    auto isBg = [&](uint32_t x, uint32_t y) {
        const uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
        return p[0] == bg[0] && p[1] == bg[1] && p[2] == bg[2] && p[3] == bg[3];
    };
    std::vector<uint8_t> visited(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    std::vector<uint32_t> stackX, stackY;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (visited[idx] || isBg(x, y)) continue;
            SilhouetteComponent comp;
            comp.minX = comp.maxX = x;
            comp.minY = comp.maxY = y;
            stackX.clear();
            stackY.clear();
            stackX.push_back(x);
            stackY.push_back(y);
            visited[idx] = 1;
            while (!stackX.empty()) {
                const uint32_t cx = stackX.back();
                stackX.pop_back();
                const uint32_t cy = stackY.back();
                stackY.pop_back();
                ++comp.area;
                if (cx < comp.minX) comp.minX = cx;
                if (cx > comp.maxX) comp.maxX = cx;
                if (cy < comp.minY) comp.minY = cy;
                if (cy > comp.maxY) comp.maxY = cy;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int64_t nx = static_cast<int64_t>(cx) + dx;
                        const int64_t ny = static_cast<int64_t>(cy) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int64_t>(width) || ny >= static_cast<int64_t>(height))
                            continue;
                        const size_t nidx = static_cast<size_t>(ny) * width + static_cast<size_t>(nx);
                        if (visited[nidx] || isBg(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))) continue;
                        visited[nidx] = 1;
                        stackX.push_back(static_cast<uint32_t>(nx));
                        stackY.push_back(static_cast<uint32_t>(ny));
                    }
                }
            }
            if (comp.area >= kMinSilhouetteComponentAreaPx) result.push_back(comp);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const SilhouetteComponent& a, const SilhouetteComponent& b) { return a.area > b.area; });
    return result;
}

SceneResult checkDriveDemoCoverageScene(const std::string& root, const std::string& dataRoot) {
    SceneResult r;
    r.name = "drive_demo_coverage (sr3_viewer drive_demo, multi-channel mesh coverage/bbox regression)";

    const std::string goldenDir = root + "\\tests\\golden\\drive_demo_coverage";
    const std::string workDir = root + "\\tests\\golden";
    const std::string viewerExe = toolExePath(root, "sr3_viewer.exe");
    // Same real archive/ccar pair drive_demo defaults to, and vehicle_genki's
    // own golden uses - the SAME real vehicle, see this scene's own top doc
    // comment for why that matters for the cross-vehicle sanity check below.
    const std::string archive = dataRoot + "\\dlc1.vpp_pc";
    const std::string ccarName = "car_4dr_genki_0.ccar_pc";
    const std::string vehicleGenkiBaseline = root + "\\tests\\golden\\vehicle_genki\\baseline_capture.png";

    if (!fileExists(goldenDir + "\\baseline_capture.png")) {
        r.status = "SKIP";
        r.details.push_back("no frozen baseline under " + goldenDir);
        return r;
    }
    if (!fileExists(vehicleGenkiBaseline)) {
        r.status = "SKIP";
        r.details.push_back("no frozen vehicle_genki baseline (needed as this scene's cross-vehicle sanity "
                            "reference) under " + root + "\\tests\\golden\\vehicle_genki");
        return r;
    }
    if (!fileExists(viewerExe)) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe not found at " + viewerExe + " - build it via CMake (lands alongside golden_scene_check.exe) or pass --bin-dir");
        return r;
    }

    _mkdir((workDir + "\\_work").c_str());
    const std::string workStem = "drive_demo_coverage";
    // --no-city (this scene's own task brief: "render the car alone"): no
    // real sr3_city_0.vpp_pc dependency at all. Frame 0 of the scripted
    // straight-phase sequence is the same "single static frame" convention
    // every other scene in this file already uses - deterministic here too
    // (fixed VehicleTuning fixture + fixed start pose, not sampled from any
    // RNG/wall-clock input - see runDriveDemo()'s own doc comment).
    // --capture-frames 0 (NOT --capture-frame, confirmed directly by testing
    // this exact command): drive_demo's own --capture-frame alone only sets
    // the FALLBACK frame used when --capture-frames resolves empty: its
    // default --capture-frames value is the non-empty 3-phase list {89, 179,
    // 239} (see runDriveDemo()'s own captureFrames default), which still
    // wins over a bare --capture-frame 0 and would capture nothing at all
    // under --frames 1. Every OTHER scene in this file uses a different
    // command (`vehicle`/`zone`/...) whose own --capture-frame has no such
    // list to be overridden by - this is specific to drive_demo's own
    // multi-phase default.
    std::string cmd = "\"" + viewerExe + "\" drive_demo \"" + archive + "\" " + ccarName +
                      " --no-city --frames 1 --capture _work/" + workStem + ".png --capture-frames 0 > _work\\" +
                      workStem + "_check_stdout.txt 2>&1";
    int rc = runIn(workDir, cmd);
    if (rc != 0) {
        r.status = "FAIL";
        r.details.push_back("sr3_viewer.exe returned exit code " + std::to_string(rc));
        return r;
    }

    // drive_demo's own capture-path naming (runDriveDemo(), the per-frame
    // capture block): a NON-EMPTY --capture-frames list - true here even
    // though it names only one frame - always appends "_frameN.png" to the
    // --capture path given, rather than writing that path verbatim (that
    // bare form is only used when --capture-frames resolves empty). The
    // real written file is therefore "<workStem>.png_frame0.png", not
    // "<workStem>.png" - confirmed directly by running this exact command.
    const std::string freshPngPath = workDir + "\\_work\\" + workStem + ".png_frame0.png";

    std::string error;
    bool allPass = true;

    // PRIMARY gate: byte-exact against this scene's OWN frozen baseline -
    // same camera, same resolution, same command, so this is strictly the
    // strongest check available (same convention as every other scene in
    // this file).
    std::string pngReport;
    bool pExact = comparePngExact(goldenDir + "\\baseline_capture.png", freshPngPath, pngReport);
    r.details.push_back(std::string("self-baseline byte-exact: ") + (pExact ? "PASS - " : "FAIL - ") + pngReport);
    allPass = allPass && pExact;

    CoverageStats fresh = computeCoverageStats(freshPngPath, error);
    if (!fresh.ok) {
        r.status = "FAIL";
        r.details.push_back("could not decode fresh capture for coverage/bbox: " + error);
        return r;
    }
    CoverageStats selfBaseline = computeCoverageStats(goldenDir + "\\baseline_capture.png", error);
    if (!selfBaseline.ok) {
        r.status = "FAIL";
        r.details.push_back("could not decode self baseline for coverage/bbox: " + error);
        return r;
    }
    CoverageStats vehicleGenki = computeCoverageStats(vehicleGenkiBaseline, error);
    if (!vehicleGenki.ok) {
        r.status = "FAIL";
        r.details.push_back("could not decode vehicle_genki cross-check baseline: " + error);
        return r;
    }

    // SECONDARY gate (still against the self-baseline): coverage/bbox,
    // computed EVEN WHEN the byte-exact gate above already passed - this is
    // deliberate belt-and-suspenders, not redundant: the orchestrator's own
    // request was specifically for a coverage/bbox signal that degrades
    // gracefully (and still reports a concrete number) under a FUTURE
    // legitimate pixel-level change (say, a lighting tweak) that a byte-
    // exact gate would reject outright - kMinCoverageRatio/kMinBboxRatio are
    // the orchestrator's own stated thresholds ("drops more than ~5%"
    // coverage; bbox "shrinks significantly" - CHOSEN as 10%, a margin
    // wider than the 5% coverage gate since a bbox edge can move a few
    // pixels from a legitimate rendering difference without the underlying
    // geometry actually being wrong).
    const double kMinCoverageRatio = 0.95;
    const double kMinBboxRatio = 0.90;

    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "self-baseline coverage: fresh %.2f%% (%llu px) vs baseline %.2f%% (%llu px) of %ux%u",
                  fresh.coverageFraction() * 100.0, static_cast<unsigned long long>(fresh.coveredPixels),
                  selfBaseline.coverageFraction() * 100.0,
                  static_cast<unsigned long long>(selfBaseline.coveredPixels), fresh.width, fresh.height);
    r.details.push_back(buf);
    bool pCoverage = selfBaseline.coveredPixels == 0 ||
                     static_cast<double>(fresh.coveredPixels) >=
                         kMinCoverageRatio * static_cast<double>(selfBaseline.coveredPixels);
    r.details.push_back(std::string("  coverage vs self-baseline: ") +
                        (pCoverage ? "PASS" : "FAIL (coverage dropped more than ~5% vs the frozen baseline)"));
    allPass = allPass && pCoverage;

    const double freshBW = fresh.bboxWidthFraction(), freshBH = fresh.bboxHeightFraction();
    const double baseBW = selfBaseline.bboxWidthFraction(), baseBH = selfBaseline.bboxHeightFraction();
    std::snprintf(buf, sizeof(buf),
                  "self-baseline bbox: fresh %ux%u px (%.1f%% x %.1f%% of frame) vs baseline %ux%u px (%.1f%% x "
                  "%.1f%% of frame)",
                  fresh.coveredPixels ? (fresh.maxX - fresh.minX + 1) : 0u,
                  fresh.coveredPixels ? (fresh.maxY - fresh.minY + 1) : 0u, freshBW * 100.0, freshBH * 100.0,
                  selfBaseline.coveredPixels ? (selfBaseline.maxX - selfBaseline.minX + 1) : 0u,
                  selfBaseline.coveredPixels ? (selfBaseline.maxY - selfBaseline.minY + 1) : 0u, baseBW * 100.0,
                  baseBH * 100.0);
    r.details.push_back(buf);
    bool pBbox = (baseBW <= 0.0 || freshBW >= kMinBboxRatio * baseBW) &&
                (baseBH <= 0.0 || freshBH >= kMinBboxRatio * baseBH);
    r.details.push_back(
        std::string("  bbox vs self-baseline: ") +
        (pBbox ? "PASS" : "FAIL (bounding box shrank significantly - a body panel/channel is likely missing again)"));
    allPass = allPass && pBbox;

    // SECONDARY, cross-vehicle sanity signal (the orchestrator's own
    // explicit request - see this scene's own top doc comment for why this
    // is deliberately loose rather than a second tight gate).
    const double kMinCrossVehicleCoverageRatio = 0.5;
    std::snprintf(buf, sizeof(buf),
                  "cross-vehicle sanity: drive_demo coverage %.2f%% vs vehicle_genki coverage %.2f%% (loose "
                  "cross-check across a different camera/resolution/render-path, see top doc comment)",
                  fresh.coverageFraction() * 100.0, vehicleGenki.coverageFraction() * 100.0);
    r.details.push_back(buf);
    bool pCross = vehicleGenki.coverageFraction() <= 0.0 ||
                  fresh.coverageFraction() >= kMinCrossVehicleCoverageRatio * vehicleGenki.coverageFraction();
    r.details.push_back(
        std::string("  cross-vehicle sanity: ") +
        (pCross ? "PASS" : "FAIL (car coverage implausibly small vs the independent vehicle_genki render)"));
    allPass = allPass && pCross;

    // WHEEL-SPECIFIC gate (this task, 2026-10-08 - see
    // kMinWheelHeightToWidthRatio's own doc comment above for the full
    // why/calibration, including the first, measured-broken version of
    // this gate). Independent of the self-baseline/cross-vehicle gates
    // above: inspects the FRESH capture's own bbox shape only, so it
    // keeps catching a wheel-less car even if a future baseline update
    // ever re-freezes one without wheels.
    if (fresh.coveredPixels == 0 || fresh.maxX < fresh.minX) {
        r.details.push_back("wheel-aspect-ratio: could not compute (empty fresh bbox)");
        allPass = false;
    } else {
        const double freshWidthPx = static_cast<double>(fresh.maxX - fresh.minX + 1);
        const double freshHeightPx = static_cast<double>(fresh.maxY - fresh.minY + 1);
        const double freshRatio = freshWidthPx > 0.0 ? freshHeightPx / freshWidthPx : 0.0;
        std::snprintf(buf, sizeof(buf),
                      "wheel-aspect-ratio: fresh bbox %.0fx%.0f px, height/width ratio %.3f (need >=%.2f)",
                      freshWidthPx, freshHeightPx, freshRatio, kMinWheelHeightToWidthRatio);
        r.details.push_back(buf);
        bool pWheelAspect = freshRatio >= kMinWheelHeightToWidthRatio;
        r.details.push_back(std::string("  wheel-aspect-ratio: ") +
                            (pWheelAspect ? "PASS"
                                          : "FAIL (bbox is too flat/wide for its own width - wheels are likely "
                                            "missing, which removes the car's own lower height without "
                                            "shrinking its width)"));
        allPass = allPass && pWheelAspect;
    }

    // SILHOUETTE-CONNECTIVITY gate (this task, 2026-10-08) - see
    // kMinSilhouetteComponentAreaPx's own doc comment above for the full
    // why/calibration and the real measured before/after component counts.
    // Independent of the self-baseline/cross-vehicle/aspect-ratio gates
    // above, same as those: inspects the FRESH capture's own silhouette
    // structure directly, so it keeps catching a disconnected-wheel
    // regression even if a future baseline update ever re-freezes one with
    // the bug back in it.
    {
        std::string compError;
        std::vector<SilhouetteComponent> comps = computeSilhouetteComponents(freshPngPath, compError);
        if (comps.empty() && !compError.empty()) {
            r.details.push_back("silhouette-connectivity: could not compute (" + compError + ")");
            allPass = false;
        } else {
            std::string compList;
            for (size_t i = 0; i < comps.size() && i < 6; ++i) {
                char cbuf[160];
                std::snprintf(cbuf, sizeof(cbuf), "%s#%zu area=%llu bbox=(%u,%u)-(%u,%u)", i == 0 ? "" : ", ", i,
                              static_cast<unsigned long long>(comps[i].area), comps[i].minX, comps[i].minY,
                              comps[i].maxX, comps[i].maxY);
                compList += cbuf;
            }
            std::snprintf(buf, sizeof(buf),
                          "silhouette-connectivity: %zu significant component(s) (>= %llu px) %s", comps.size(),
                          static_cast<unsigned long long>(kMinSilhouetteComponentAreaPx), compList.c_str());
            r.details.push_back(buf);
            bool pConnectivity = comps.size() <= 1;
            r.details.push_back(
                std::string("  silhouette-connectivity: ") +
                (pConnectivity ? "PASS"
                                : "FAIL (vehicle silhouette is split into multiple disjoint pieces - a wheel (or "
                                  "other part) is likely floating disconnected from the body, not tucked into its "
                                  "own arch/socket)"));
            allPass = allPass && pConnectivity;
        }
    }

    r.status = allPass ? "PASS" : "FAIL";
    return r;
}

} // namespace golden_scene_check
