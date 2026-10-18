// Golden-scene regression harness (AI-WORKFLOW-INSTRUCTIONS.md Sec13 - "end-
// to-end validation that doesn't need a human": freeze a fixed scene's
// image + draw-stream summary, then re-render and diff against it on every
// future change).
//
// CONVENTION (read this before adding another scene):
//   tests/golden/<scene>/            - the FROZEN baseline artifacts for one
//                                       scene, committed/kept permanently.
//     COMMAND.txt or STATUS.txt      - which real command produced this
//                                       baseline (or, if the scene is not
//                                       frozen yet, why not - see tree/
//                                       STATUS.txt for a worked example of
//                                       an honestly-reported gap).
//     baseline_*.png                 - the frozen output image(s), byte-
//                                       exact PNGs from this project's own
//                                       sr3render::writePng (or a tool that
//                                       calls it).
//     baseline_stdout.txt            - the frozen real draw-stream summary
//                                       (that command's own real stdout,
//                                       never a format invented for this
//                                       harness).
//   tests/golden/_work/              - scratch: where a re-render's fresh
//                                       output lands before being diffed
//                                       against the frozen baseline. Not
//                                       part of the frozen record; safe to
//                                       delete any time.
//   tests/golden/_bin/               - HISTORICAL ONLY, no longer populated:
//                                       this used to be tests/golden/
//                                       build.bat's own stand-alone cl.exe/
//                                       link.exe build output, kept separate
//                                       from CMakeLists.txt to avoid a
//                                       shared-file race with a concurrent
//                                       task editing tools/sr3_viewer.cpp.
//                                       That race is long over: every tool
//                                       build.bat used to build
//                                       (sr3_viewer, golden_scene_check
//                                       itself, tree_baseline_render,
//                                       prototype_lit_clmesh_tower2, and the
//                                       rest) is now a real CMake target
//                                       (CMakeLists.txt's own "Fold tests/
//                                       golden/build.bat's remaining
//                                       standalone tools" comment), build.bat
//                                       has been deleted, and every
//                                       checkXScene() below resolves its
//                                       tool's path via toolExePath()/
//                                       --bin-dir (set by CMakeLists.txt's
//                                       add_test() to the real CMake build
//                                       output directory) rather than this
//                                       path. --bin-dir defaults to empty,
//                                       which still falls back to this
//                                       exact tests/golden/_bin/ location -
//                                       kept only so a human manually
//                                       placing binaries there by hand
//                                       still works, not because anything
//                                       still builds there automatically.
//
// To add a new scene: freeze its real command's PNG(s) + stdout under
// tests/golden/<scene>/ (see brad/COMMAND.txt for the documented example),
// then add one checkXScene() function below plus one call to it in main() -
// same shape as checkBradScene().
//
// COMPARISON POLICY: byte-exact file comparison is the primary, authoritative
// check for both the PNG and the stdout summary - this project's own
// renders are deterministic (no RNG, no wall-clock input, a fixed WARP-or-
// hardware device path reported in stdout rather than hidden) and this was
// directly confirmed by running brad's own baseline command twice in a row
// and diffing (see the harness build notes) - MD5-identical both times,
// matching HANDOFF.md Sec9.105's own independently-recorded hashes exactly.
// No tolerance/fuzzy-diff fallback is used anywhere in this file because no
// concrete non-determinism has been found - per this task's own instruction,
// a fallback is added only if one is.
//
// When a byte-exact PNG compare fails, this tool ALSO decodes both PNGs (a
// small decoder tailored to exactly this project's own sr3render::writePng
// output shape - single IHDR/IDAT/IEND, 8-bit RGBA, no interlace, filter-
// type 0 on every scanline; NOT a general PNG reader) so the failure report
// names actual differing pixels and a bounding box, not just "differs".

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

// --selftest: proves the comparison logic itself catches a real regression.
// Makes a byte-identical COPY of a frozen baseline PNG, flips one pixel's
// worth of bytes in the copy (a trivial, deliberate break), confirms
// comparePngExact() reports FAIL with a real differing-pixel location, then
// confirms comparing the TRUE unmodified baseline against itself reports
// PASS. Never modifies the real baseline file itself - both copies live
// under tests/golden/_work/, named from `workStem` so brad/vehicle_genki/
// vehicle_standard's own selftest artifacts don't collide.
bool selfTestOneBaseline(const std::string& label, const std::string& baseline, const std::string& workDir,
                        const std::string& workStem) {
    const std::string corrupted = workDir + "\\selftest_corrupted_" + workStem + ".png";
    const std::string clean = workDir + "\\selftest_clean_" + workStem + ".png";

    bool ok = false;
    std::vector<uint8_t> bytes = readFileBytes(baseline, ok);
    if (!ok || bytes.empty()) {
        std::printf("SELFTEST FATAL [%s]: could not read baseline '%s'\n", label.c_str(), baseline.c_str());
        return false;
    }

    // Clean copy: byte-for-byte identical -> must PASS.
    {
        std::ofstream f(clean, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // Corrupted copy: decode the real baseline to real pixels, flip ONE
    // real pixel's colour (a trivial, deliberate break at a known image
    // location), then re-encode - a real, decodable PNG that differs by
    // exactly one pixel, so the FAIL report below can show an actual
    // pixel-level diff (location + bounding box), not just an undecodable
    // corrupt stream.
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    std::string decodeErr;
    if (!decodeOwnPng(baseline, w, h, px, decodeErr)) {
        std::printf("SELFTEST FATAL [%s]: could not decode baseline for pixel-level corruption: %s\n",
                   label.c_str(), decodeErr.c_str());
        return false;
    }
    const uint32_t brokenX = w / 3, brokenY = h / 3;
    size_t brokenIdx = (static_cast<size_t>(brokenY) * w + brokenX) * 4;
    px[brokenIdx + 0] ^= 0xFF;
    px[brokenIdx + 1] ^= 0xFF;
    px[brokenIdx + 2] ^= 0xFF;
    if (!encodeOwnPngForSelfTest(corrupted, w, h, px)) {
        std::printf("SELFTEST FATAL [%s]: could not re-encode corrupted PNG\n", label.c_str());
        return false;
    }
    std::printf("[selftest:%s] flipped pixel (x=%u, y=%u) in a %ux%u image\n", label.c_str(), brokenX, brokenY, w, h);

    std::string report;
    bool passClean = comparePngExact(baseline, clean, report);
    std::printf("[selftest:%s] baseline vs UNMODIFIED copy -> %s : %s\n", label.c_str(),
               passClean ? "PASS" : "FAIL", report.c_str());

    bool passCorrupted = comparePngExact(baseline, corrupted, report);
    std::printf("[selftest:%s] baseline vs DELIBERATELY-CORRUPTED copy -> %s : %s\n", label.c_str(),
               passCorrupted ? "PASS (WRONG - selftest itself is broken)" : "FAIL (correct)", report.c_str());

    bool passRecheck = comparePngExact(baseline, clean, report);
    std::printf("[selftest:%s] baseline vs unmodified copy AGAIN -> %s : %s\n", label.c_str(),
               passRecheck ? "PASS" : "FAIL", report.c_str());

    bool ok2 = passClean && !passCorrupted && passRecheck;
    std::printf("SELFTEST[%s] %s\n", label.c_str(),
               ok2 ? "OK - harness correctly detects a real regression and is clean against the true baseline"
                   : "FAILED - the comparison logic itself is not behaving as expected");
    return ok2;
}

int runSelfTest(const std::string& root) {
    const std::string workDir = root + "\\tests\\golden\\_work";
    _mkdir(workDir.c_str());

    bool okBrad = selfTestOneBaseline("brad", root + "\\tests\\golden\\brad\\baseline_bind.png", workDir, "bind");
    bool okGenki = selfTestOneBaseline("vehicle_genki",
                                       root + "\\tests\\golden\\vehicle_genki\\baseline_capture.png", workDir,
                                       "vehicle_genki");
    bool okStandard = selfTestOneBaseline("vehicle_standard",
                                          root + "\\tests\\golden\\vehicle_standard\\baseline_capture.png", workDir,
                                          "vehicle_standard");
    bool okZoneTile = selfTestOneBaseline("zone_tile",
                                          root + "\\tests\\golden\\zone_tile\\baseline_capture.png", workDir,
                                          "zone_tile");
    bool okTree = selfTestOneBaseline("tree", root + "\\tests\\golden\\tree\\baseline_render.png", workDir,
                                      "tree");
    bool okClmesh = selfTestOneBaseline("clmesh_lite_fixh",
                                        root + "\\tests\\golden\\clmesh_lite_fixh\\baseline_capture.png", workDir,
                                        "clmesh_lite_fixh");
    bool okClmeshAirport = selfTestOneBaseline(
        "clmesh_airport_controltower",
        root + "\\tests\\golden\\clmesh_airport_controltower\\baseline_capture.png", workDir,
        "clmesh_airport_controltower");
    bool okClmeshAirportLit = selfTestOneBaseline(
        "clmesh_airport_controltower_lit",
        root + "\\tests\\golden\\clmesh_airport_controltower_lit\\baseline_capture.png", workDir,
        "clmesh_airport_controltower_lit");

    bool okVintHud = selfTestOneBaseline("vint_hud_running_man",
                                         root + "\\tests\\golden\\vint_hud_running_man\\baseline_capture.png", workDir,
                                         "vint_hud_running_man");
    bool okHudLiveDemo = selfTestOneBaseline(
        "hud_live_demo", root + "\\tests\\golden\\hud_live_demo\\baseline_frame1_damage.png", workDir, "hud_live_demo");

    bool selfTestOk = okBrad && okGenki && okStandard && okZoneTile && okTree && okClmesh && okClmeshAirport &&
                       okClmeshAirportLit && okVintHud && okHudLiveDemo;
    std::printf("\nSELFTEST %s\n", selfTestOk ? "OK - every scene's comparison logic correctly detects a real "
                                                 "regression and is clean against its true baseline"
                                              : "FAILED - see the per-scene SELFTEST[...] lines above");
    return selfTestOk ? 0 : 1;
}

} // namespace golden_scene_check

using namespace golden_scene_check;

int main(int argc, char** argv) {
    std::string root = "D:\\Project Crreish\\TEAM B";
    std::string dataRoot = "D:\\Project Crreish\\Saints Row 3 CRREISH\\packfiles\\pc\\cache";
    bool selfTest = false;
    bool selfTestTolerance = false;
    bool forceHw = false;

    // --diff-tolerance (WARP-migration task, 2026-10-07): a standalone
    // utility mode, NOT part of the golden-scene regression flow below -
    // exposes comparePngTolerance() (B3 task) directly on the command line
    // so a hardware-vs-WARP migration (or any other "two legitimately-
    // different renders of the same scene" comparison) can be driven from
    // a script without writing new C++ for it, reusing the exact same
    // vetted comparison logic runToleranceSelfTest() already proves
    // correct. Takes two PNG paths positionally; everything else is an
    // optional override of comparePngTolerance()'s own named parameters
    // (same defaults runToleranceSelfTest() uses: +-2/255 per-channel band,
    // meanAbsDiff <= 1.5, maxAbsDiff <= 32).
    bool diffToleranceMode = false;
    std::string diffBaseline, diffFresh, diffOut;
    uint8_t diffPerChannelTol = 2;
    double diffMeanTol = 1.5;
    uint32_t diffHardMax = 32;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) root = argv[++i];
        else if (arg == "--data-root" && i + 1 < argc) dataRoot = argv[++i];
        // --bin-dir (CMake-fold task, 2026-10-08): see toolExePath()'s own
        // doc comment above - points every checkXScene() at the real CMake
        // build output directory instead of the old build.bat-only
        // tests/golden/_bin/. CMakeLists.txt's add_test() passes
        // $<TARGET_FILE_DIR:sr3_viewer> here.
        else if (arg == "--bin-dir" && i + 1 < argc) g_binDir = argv[++i];
        else if (arg == "--selftest") selfTest = true;
        else if (arg == "--selftest-tolerance") selfTestTolerance = true;
        else if (arg == "--hw") forceHw = true;
        else if (arg == "--diff-tolerance" && i + 2 < argc) {
            diffToleranceMode = true;
            diffBaseline = argv[++i];
            diffFresh = argv[++i];
        }
        else if (arg == "--diff-out" && i + 1 < argc) diffOut = argv[++i];
        else if (arg == "--per-channel-tol" && i + 1 < argc) diffPerChannelTol = static_cast<uint8_t>(std::atoi(argv[++i]));
        else if (arg == "--mean-tol" && i + 1 < argc) diffMeanTol = std::atof(argv[++i]);
        else if (arg == "--hard-max" && i + 1 < argc) diffHardMax = static_cast<uint32_t>(std::atoi(argv[++i]));
        else {
            std::printf("unknown argument '%s'\n", arg.c_str());
            return 1;
        }
    }

    if (diffToleranceMode) {
        ToleranceDiffResult r = comparePngTolerance(diffBaseline, diffFresh, diffPerChannelTol, diffMeanTol,
                                                     diffHardMax, diffOut);
        std::printf("%s\n", r.report.c_str());
        return r.pass ? 0 : 1;
    }

    if (selfTest) return runSelfTest(root);
    if (selfTestTolerance) return runToleranceSelfTest(root) ? 0 : 1;

    // SR3_FORCE_WARP (B3 task, 2026-10-07): this harness - and only this
    // harness - forces every subprocess it spawns below onto the WARP
    // software rasterizer (src/device.cpp's own initialise() reads this
    // env var; see that file's own comment for the full reasoning). This
    // is the "golden harness sets an env var" path the task asked for,
    // NOT a global engine change - sr3_viewer run directly by a human, or
    // by any other tool, still prefers real hardware by default, exactly
    // as before. _putenv_s affects this process's own environment block,
    // which runIn()'s std::system() calls below inherit into every
    // subprocess they spawn.
    //
    // --hw / SR3_FORCE_HW (WARP-migration task, 2026-10-07): a permanent
    // escape hatch in the OPPOSITE direction, OFF by default - every
    // golden scene still defaults to forced WARP exactly as the B3 merge
    // established (this project's now-standing policy: goldens are frozen
    // under WARP, not whatever hardware adapter happens to be present).
    // This exists so hardware rendering stays available ON DEMAND: visual
    // review of a FAIL, or producing a real-hardware image to show the
    // user, which per this task's own instruction MUST come from real
    // hardware, never WARP. Does NOT touch src/device.cpp - that file
    // already supports both directions unconditionally (hardware-
    // preferred when SR3_FORCE_WARP is unset/falsy, WARP-forced when it's
    // set truthy); this flag only controls what THIS harness's own main()
    // puts in that env var before spawning its subprocesses.
    if (!forceHw) {
        const char* hwEnv = std::getenv("SR3_FORCE_HW");
        forceHw = (hwEnv != nullptr && hwEnv[0] != '\0' && hwEnv[0] != '0');
    }
    if (!forceHw) {
        _putenv_s("SR3_FORCE_WARP", "1");
    } else {
        // Explicit hardware request: clear any SR3_FORCE_WARP=1 this
        // process might have inherited from its own parent shell/CI step -
        // "force", not "prefer", has to mean the same thing in both
        // directions, so --hw/SR3_FORCE_HW must be able to override an
        // inherited SR3_FORCE_WARP, not just fail to add to it.
        _putenv_s("SR3_FORCE_WARP", "0");
        std::printf("--hw / SR3_FORCE_HW: forcing HARDWARE rendering for this run (WARP will not be used).\n");
    }

    // Record the OS build + WARP DLL fingerprint ALONGSIDE this run's own
    // metadata (B3 task) - see queryHostEnvironmentInfo()'s own doc
    // comment for why this is a sidecar file rather than folded into any
    // byte-exact stdout comparison. Written once per run (the host/WARP
    // identity doesn't vary per scene); whoever re-freezes a golden under
    // forced WARP should copy this alongside that scene's own COMMAND.txt.
    {
        _mkdir((root + "\\tests\\golden").c_str());
        _mkdir((root + "\\tests\\golden\\_work").c_str());
        std::ofstream envFile(root + "\\tests\\golden\\_work\\ENV.txt");
        if (envFile) {
            envFile << (forceHw ? "SR3_FORCE_HW=1 (this run, hardware forced)\n" : "SR3_FORCE_WARP=1 (this run)\n")
                    << queryHostEnvironmentInfo() << "\n";
        }
    }

    std::vector<SceneResult> results;
    results.push_back(checkBradScene(root, dataRoot));
    results.push_back(checkVehicleGenkiScene(root, dataRoot));
    results.push_back(checkVehicleStandardScene(root, dataRoot));
    results.push_back(checkDriveDemoCoverageScene(root, dataRoot));
    results.push_back(checkZoneTileScene(root, dataRoot));
    results.push_back(checkTreeScene(root, dataRoot));
    results.push_back(checkClmeshLiteFixhScene(root, dataRoot));
    results.push_back(checkClmeshAirportControlTowerScene(root, dataRoot));
    results.push_back(checkClmeshAirportControlTowerLitScene(root, dataRoot));
    results.push_back(checkCityCornerScene(root, dataRoot));
    results.push_back(checkAmbientPopulationDemoScene(root, dataRoot));
    results.push_back(checkWalkDemoScene(root, dataRoot));
    results.push_back(checkWalkDemoV2Scene(root, dataRoot));
    results.push_back(checkVintHudRunningManScene(root, dataRoot));
    results.push_back(checkHudMaskScene(root, dataRoot));
    results.push_back(checkHudLiveDemoScene(root, dataRoot));

    int failures = 0;
    std::printf("\n=== golden-scene regression check ===\n");
    for (const auto& r : results) {
        printResult(r);
        if (r.status == "FAIL") ++failures;
    }
    std::printf("\n%d scene(s) FAIL, exit code = failure count.\n", failures);
    return failures;
}
