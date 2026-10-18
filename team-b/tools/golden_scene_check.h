// golden_scene_check.h - shared declarations of the golden-scene harness
// (tools/golden_scene_check*.cpp). Split out of golden_scene_check.cpp
// (Team C refactor, 2026-10-17); the structs moved verbatim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace golden_scene_check {

// --- Tolerance-based image diff (B3 task, 2026-10-07) ----------------------
// This is NOT a replacement for comparePngExact() above, which remains the
// PRIMARY/authoritative check for every existing golden scene, unchanged -
// per this file's own top-of-file COMPARISON POLICY comment, this
// project's renders are deterministic and byte-exact comparison against a
// FROZEN baseline is the right check for that. This is for a DIFFERENT
// comparison the task that added it was explicit about: a NEW render
// against a baseline that was legitimately frozen under a DIFFERENT
// device (e.g. forcing WARP here when the original baseline was frozen on
// whatever hardware adapter happened to be present) - two correct renders
// of the same scene that may legitimately differ by a few LSBs without
// either one being wrong. A small, explicitly-authored reimplementation of
// pixelmatch's documented approach (https://github.com/mapbox/pixelmatch,
// ISC licence) - per-channel delta thresholding plus an overall
// differing-pixel accounting - NOT copied code, and NOT NVIDIA FLIP (BSD-3
// Clause): FLIP is a full perceptual-difference metric (its own CUDA/CPU
// implementation, HDR support, visualization pipeline) and is a lot of
// surface area to vendor for a project that just needs a fixed ±1-2/255
// per-channel band plus an overall max/mean gate - writing the documented
// idea directly is both smaller and easier to audit than vendoring it.
struct ToleranceDiffResult {
    bool pass = false;
    uint64_t diffPixelCount = 0;  // pixels where some channel exceeds perChannelTolerance
    uint64_t totalPixels = 0;
    double meanAbsDiff = 0.0;     // mean per-channel abs diff, over every channel of every pixel
    uint32_t maxAbsDiff = 0;      // worst single channel diff anywhere in the image
    std::string report;
};

struct SceneResult {
    std::string name;
    // "PASS", "FAIL", or "SKIP"
    std::string status;
    std::vector<std::string> details;
};

std::vector<uint8_t> readFileBytes(const std::string& path, bool& ok);
uint32_t readU32BE(const uint8_t* p);
bool decodeOwnPng(const std::string& path, uint32_t& width, uint32_t& height,
                  std::vector<uint8_t>& rgbaOut, std::string& error);
void appendU32BEPng(std::vector<uint8_t>& out, uint32_t v);
bool encodeOwnPngForSelfTest(const std::string& path, uint32_t width, uint32_t height,
                            const std::vector<uint8_t>& rgba);
bool comparePngExact(const std::string& baselinePath, const std::string& freshPath, std::string& report);
bool compareTextExact(const std::string& baselinePath, const std::string& freshPath, std::string& report);
std::string maskTimingText(const std::string& line);
bool compareTextMaskingTiming(const std::string& baselinePath, const std::string& freshPath, std::string& report);
ToleranceDiffResult comparePngTolerance(const std::string& baselinePath, const std::string& freshPath,
                                        uint8_t perChannelTolerance, double meanDiffThreshold255,
                                        uint32_t hardMaxDiff, const std::string& diffPngOutPath);
bool runToleranceSelfTest(const std::string& root);
int runIn(const std::string& cwd, const std::string& command);
bool fileExists(const std::string& path);
extern std::string g_binDir;
std::string toolExePath(const std::string& root, const std::string& exeName);
std::string queryHostEnvironmentInfo();
void printResult(const SceneResult& r);
SceneResult checkBradScene(const std::string& root, const std::string& dataRoot);
SceneResult checkVehicleScene(const std::string& root, const std::string& dataRoot, const std::string& sceneName,
                              const std::string& sceneDir, const std::string& vehicleArchive,
                              const std::string& ccarName, const std::string& workStem);
SceneResult checkVehicleGenkiScene(const std::string& root, const std::string& dataRoot);
SceneResult checkVehicleStandardScene(const std::string& root, const std::string& dataRoot);
SceneResult checkDriveDemoCoverageScene(const std::string& root, const std::string& dataRoot);
SceneResult checkZoneTileScene(const std::string& root, const std::string& dataRoot);
SceneResult checkTreeScene(const std::string& root, const std::string& dataRoot);
SceneResult checkClmeshLiteFixhScene(const std::string& root, const std::string& dataRoot);
SceneResult checkClmeshAirportControlTowerScene(const std::string& root, const std::string& dataRoot);
SceneResult checkCityCornerScene(const std::string& root, const std::string& dataRoot);
SceneResult checkAmbientPopulationDemoScene(const std::string& root, const std::string& dataRoot);
SceneResult checkWalkDemoScene(const std::string& root, const std::string& dataRoot);
SceneResult checkWalkDemoV2Scene(const std::string& root, const std::string& dataRoot);
SceneResult checkClmeshAirportControlTowerLitScene(const std::string& root, const std::string& dataRoot);
SceneResult checkVintHudRunningManScene(const std::string& root, const std::string& dataRoot);
SceneResult checkHudMaskScene(const std::string& root, const std::string& dataRoot);
SceneResult checkHudLiveDemoScene(const std::string& root, const std::string& dataRoot);

} // namespace golden_scene_check
