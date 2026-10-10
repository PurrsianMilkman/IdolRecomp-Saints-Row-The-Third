// ambient_population_demo - a deterministic, headless demo/golden-scene tool
// for the distant ambient pedestrian/vehicle spawn-despawn system
// (spec-ai-runtime.md Sec17, include/sr3airuntime/ambient_population.h).
// Invoked by tools/golden_scene_check.cpp's checkAmbientPopulationDemoScene()
// (the ambient_population_demo golden scene).
//
// WHY A NEW SCENE RATHER THAN EXTENDING city_corner. city_corner's own
// frozen baseline (tests/golden/city_corner/) is produced by `sr3_viewer
// city`, which renders real textured zone geometry but never loads or
// parses the Traffic (0x2236)/Sidewalks (0x2238) `.czn_pc` content kinds -
// a source survey before writing this tool found no C++ reader for either
// content kind anywhere in this project yet (only sr3zone::zone_objects.h's
// 0x2234 Objects reader exists; see ambient_population.h's own "SYNTHETIC-
// ONLY ZONE BRIDGE" note). Building that byte format from
// spec-zone-data-format.md Sec13.2's own prose description would be a
// separate reverse-engineering/format task, not invented here, and editing
// city_corner's own already-frozen, shared baseline (several other agents'
// worktrees reference it) to bolt on unrelated content would both overreach
// this task's charter and risk a shared-state collision. This tool instead
// exercises the REAL ambient_population.h algorithm end to end against a
// clearly-labelled SYNTHETIC stand-in road/sidewalk grid (sized and spaced
// to resemble city_corner's own city-block scale, not an arbitrary toy
// shape) and captures its own schematic top-down frame - a real, decodable
// PNG from this project's own sr3render::writePng, produced by rendering
// actual simulation state (not a mock-up), exactly like every other golden
// scene in this project, just not a textured 3-D view (no distant-ped/
// vehicle LOD mesh renderer exists in this project either - out of this
// task's scope, see ambient_population.h's own OPEN list).
//
// DETERMINISM. Every input is fixed at compile time or by this file's own
// argument defaults (grid layout, spawn/despawn parameters, camera path,
// frame count, RNG seed) - no wall-clock, no live input - so stdout and the
// captured PNG are byte-reproducible run to run, letting
// checkAmbientPopulationDemoScene() use a plain byte-exact compare (no
// timing-text masking needed, unlike city_corner's own per-cell load-time
// lines).
//
// WHAT THIS PROVES: the real Sec17 algorithm shapes (budget-capped refill,
// camera-lookahead-predicted spawn/despawn radii, ped weighted type-slot
// pick, vehicle Spline_Type-gated graph-node-then-slot-search pick,
// distance+angle despawn cull, the shared-latch/mutual-exclusion ~1s-
// throttled driver) actually run, against actual candidate data, and
// actually fill both populations toward their real 75/75 budgets over a
// few simulated seconds - "the real algorithm's shape," per this task's own
// brief, not merely that the header compiles.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sr3airuntime/ambient_population.h"
#include "sr3render/png_writer.h"

using namespace sr3airuntime;

namespace {

// ===========================================================================
// SYNTHETIC stand-in candidate graph: a 5x5 city-block street grid, lines
// 300 units apart (a plausible city-block scale, not a disassembly-derived
// number), sidewalks offset 6 units from each road, sampled every 20 units.
// See this file's own top comment for why this is synthetic, not a real
// Traffic/Sidewalks zone-data read.
// ===========================================================================
constexpr double kGridLines[5] = {-600.0, -300.0, 0.0, 300.0, 600.0};
constexpr double kSidewalkOffset = 6.0;
constexpr double kSidewalkStep = 20.0;
constexpr uint32_t kRoadSplineTypeMask = 0x1u;

// Each of the 5+5 grid lines is subdivided into kSidewalkStep-length
// sub-segments (matching the sidewalk sampling granularity, CHOSEN purely
// for this demo's own visual/numeric consistency) rather than one long
// segment per city block - this is closer to the real Traffic graph's own
// actual granularity (spec-zone-data-format.md Sec13.2's 24-byte-stride
// segment records are road-piece-sized, not whole-block-sized) and gives
// GatherTrafficCandidates's distance-band test many nearby candidates to
// find regardless of exactly how large a given pass's randomized spawnDist
// rolls - a coarse one-segment-per-block grid was tried first and produced
// long stretches where no candidate segment fell within a small spawnDist
// roll, starving the vehicle side; this finer subdivision fixes that
// without changing any real algorithm code, only this demo's own synthetic
// input data.
std::vector<TrafficSegment> BuildRoadGrid() {
    std::vector<TrafficSegment> segs;
    uint64_t id = 1;
    for (double z : kGridLines) {
        for (double x = kGridLines[0]; x < kGridLines[4] - 1e-6; x += kSidewalkStep) {
            segs.push_back({id++, Vec3{x, 0, z}, Vec3{x + kSidewalkStep, 0, z}, kRoadSplineTypeMask});
        }
    }
    for (double x : kGridLines) {
        for (double z = kGridLines[0]; z < kGridLines[4] - 1e-6; z += kSidewalkStep) {
            segs.push_back({id++, Vec3{x, 0, z}, Vec3{x, 0, z + kSidewalkStep}, kRoadSplineTypeMask});
        }
    }
    return segs;
}

std::vector<SidewalkNode> BuildSidewalkGrid() {
    std::vector<SidewalkNode> nodes;
    uint64_t id = 1;
    for (double z : kGridLines) {
        for (double x = kGridLines[0]; x <= kGridLines[4] + 1e-6; x += kSidewalkStep) {
            nodes.push_back({id++, Vec3{x, 0, z + kSidewalkOffset}});
        }
    }
    for (double x : kGridLines) {
        for (double z = kGridLines[0]; z <= kGridLines[4] + 1e-6; z += kSidewalkStep) {
            nodes.push_back({id++, Vec3{x + kSidewalkOffset, 0, z}});
        }
    }
    return nodes;
}

// ===========================================================================
// Minimal CPU-side schematic top-down canvas - this tool's own visualization
// code, NOT a 3-D renderer and not claiming to be one. Dots/lines are drawn
// directly into an RGBA buffer, then handed to sr3render::writePng exactly
// like every other golden scene's own captured frame.
// ===========================================================================
struct Canvas {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

Canvas MakeCanvas(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    Canvas c;
    c.width = w;
    c.height = h;
    c.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < c.rgba.size(); i += 4) {
        c.rgba[i] = r;
        c.rgba[i + 1] = g;
        c.rgba[i + 2] = b;
        c.rgba[i + 3] = 255;
    }
    return c;
}

void SetPixel(Canvas& c, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= static_cast<int>(c.width) || y >= static_cast<int>(c.height)) return;
    size_t i = (static_cast<size_t>(y) * c.width + static_cast<size_t>(x)) * 4;
    c.rgba[i] = r;
    c.rgba[i + 1] = g;
    c.rgba[i + 2] = b;
    c.rgba[i + 3] = 255;
}

void DrawDot(Canvas& c, int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) SetPixel(c, cx + dx, cy + dy, r, g, b);
        }
    }
}

void DrawLine(Canvas& c, double x0, double y0, double x1, double y1, uint8_t r, uint8_t g, uint8_t b) {
    const double dist = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, static_cast<int>(dist));
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        SetPixel(c, static_cast<int>(x0 + (x1 - x0) * t), static_cast<int>(y0 + (y1 - y0) * t), r, g, b);
    }
}

void DrawCircleOutline(Canvas& c, int cx, int cy, double radius, uint8_t r, uint8_t g, uint8_t b) {
    if (radius <= 0.0) return;
    const int steps = std::max(32, static_cast<int>(radius));
    for (int i = 0; i < steps; ++i) {
        const double angle = (2.0 * 3.14159265358979323846 * i) / steps;
        SetPixel(c, cx + static_cast<int>(radius * std::cos(angle)), cy + static_cast<int>(radius * std::sin(angle)),
                  r, g, b);
    }
}

// World (x,z, top-down - y/height is ignored, same convention a top-down
// map uses) -> screen, centred on `origin` (this tool centres on the final
// camera position so the camera is always visible in frame).
struct ViewTransform {
    double scale = 1.0;
    Vec3 origin;
    uint32_t width = 0, height = 0;

    void ToScreen(const Vec3& p, double& outX, double& outY) const {
        outX = width / 2.0 + (p.x - origin.x) * scale;
        outY = height / 2.0 - (p.z - origin.z) * scale;
    }
};

}  // namespace

int main(int argc, char** argv) {
    int frames = 1200;
    std::string capturePath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (a == "--capture" && i + 1 < argc) {
            capturePath = argv[++i];
        } else {
            std::fprintf(stderr, "ambient_population_demo: unknown argument '%s'\n", a.c_str());
            return 1;
        }
    }

    const std::vector<TrafficSegment> segments = BuildRoadGrid();
    const std::vector<SidewalkNode> sidewalks = BuildSidewalkGrid();
    // Single synthetic vehicle traffic type whose Spline_Type mask matches
    // every grid road - exercises the real Spline_Type AND-test
    // (SplineTypeEligible) without needing a second road category just to
    // prove the filter runs (a future unit could add a second type/road
    // category to exercise the REJECTING side of the filter explicitly;
    // testSplineTypeEligibleAndTest in the unit tests already covers that).
    const std::vector<VehicleTrafficType> trafficTypes = {
        {/*nameHash=*/0x1234u, /*splineTypeMask=*/kRoadSplineTypeMask, /*lengthPerSpawnReciprocal=*/100.0,
         /*noSpawnZone=*/0.0, /*dummyCarLength=*/4.5}};

    // Sec14.3's own documented pre-load image values (spec-tables-traffic-
    // ai.md Sec14.3) - ped side verbatim; vehicle side's
    // lengthPerSpawnReciprocal is the Double_Spawn_Height image value (100)
    // under this project's own CHOSEN field-identity reading (see
    // ambient_population.h's file header, CHOSEN item 1).
    SpawnDespawnParams pedParams;
    pedParams.lengthPerSpawnReciprocal = 0.04;
    pedParams.baseSpawningDist = 200.0;
    pedParams.maxSpawningDist = 1200.0;
    pedParams.despawnExpansionDistance = 25.0;
    pedParams.cameraLookaheadTimeSeconds = 0.25;
    pedParams.spawnAngleCosThreshold = 0.8;
    pedParams.despawnAngleCosThreshold = 0.6;

    SpawnDespawnParams vehicleParams;
    vehicleParams.lengthPerSpawnReciprocal = 100.0;
    vehicleParams.baseSpawningDist = 200.0;
    vehicleParams.maxSpawningDist = 1200.0;
    vehicleParams.despawnExpansionDistance = 25.0;
    vehicleParams.cameraLookaheadTimeSeconds = 0.25;
    vehicleParams.spawnAngleCosThreshold = -0.4;
    vehicleParams.despawnAngleCosThreshold = -0.5;

    std::vector<LivePed> livePeds;
    std::vector<LiveVehicle> liveVehicles;
    uint64_t nextPedId = 1, nextVehicleId = 1;

    AmbientPopulationDriver driver(/*heavyPassPeriodMs=*/1000);
    DeterministicRng rng(20261008u);  // fixed seed - this tool's own seam (see ambient_population.h's RNG-seam note), not the untraced 0x00dab660
    auto pickUniform = MakePickUniform(rng);

    Vec3 cameraPos{0, 0, 0};
    const Vec3 cameraVelocity{0, 0, 20.0};  // 20 units/s "forward" (+z)
    const Vec3 cameraForward{0, 0, 1.0};
    const int64_t dtMs = 16;

    double lastVehicleSpawnDist = 0, lastVehicleDespawnRadius = 0;
    double lastPedSpawnDist = 0, lastPedDespawnRadius = 0;
    int passIndex = 0;

    std::printf("ambient_population_demo: spec-ai-runtime.md Sec17 distant ambient population, synthetic grid\n");
    std::printf(
        "NOTE: candidate sidewalk/traffic graph is a CHOSEN synthetic stand-in (5x5 street grid, 300.0 unit "
        "spacing) - no real Traffic (0x2236)/Sidewalks (0x2238) .czn_pc reader exists in this project yet "
        "(ambient_population.h's own file comment).\n");
    std::printf("grid: %zu road segments, %zu sidewalk nodes\n", segments.size(), sidewalks.size());
    std::printf("budget: ped_target=%d ped_refill_cap=%d vehicle_target=%d vehicle_refill_cap=%d\n",
                kPedPopulationTarget, kPedRefillCapPerPass, kVehiclePopulationTarget, kVehicleRefillCapPerPass);
    std::printf("camera: start=(%.2f, %.2f, %.2f) velocity=(%.2f, %.2f, %.2f) forward=(%.2f, %.2f, %.2f)\n",
                cameraPos.x, cameraPos.y, cameraPos.z, cameraVelocity.x, cameraVelocity.y, cameraVelocity.z,
                cameraForward.x, cameraForward.y, cameraForward.z);
    std::printf("simulating %d frames @ %lldms (%.3fs total), heavy-pass period 1000ms\n", frames,
                static_cast<long long>(dtMs), (frames * dtMs) / 1000.0);
    std::printf("\n");

    for (int frame = 0; frame < frames; ++frame) {
        const double tSeconds = (frame * dtMs) / 1000.0;
        cameraPos = cameraPos + cameraVelocity * (static_cast<double>(dtMs) / 1000.0);

        driver.BeginFrame(dtMs);
        // Lightweight, every-frame bookkeeping (Sec17.5) - runs regardless
        // of whether a heavy pass also fires this frame.
        driver.RefreshCameraPrediction(cameraPos, cameraVelocity, cameraForward, vehicleParams.cameraLookaheadTimeSeconds);

        if (driver.ShouldRunVehicleHeavyPass()) {
            const double randF = RandomUnitFloat01(rng);
            SpawnRadiusResult sr = ComputeSpawnRadius(vehicleParams, dtMs / 1000.0, randF);
            lastVehicleSpawnDist = sr.spawnDist;
            lastVehicleDespawnRadius = sr.despawnRadius;
            const int spawned =
                RunVehicleHeavyPass(liveVehicles, segments, trafficTypes, driver.predictedPosition(),
                                     driver.cameraForward(), vehicleParams, /*innerDist=*/0.0, sr.spawnDist,
                                     sr.despawnRadius, /*minSlotSeparation=*/6.0, /*requestedSlotCount=*/5,
                                     pickUniform, nextVehicleId);
            ++passIndex;
            std::printf(
                "[pass %d] t=%.3fs VEHICLE heavy pass: spawned=%d live_vehicles=%zu/%d (spawnDist=%.1f "
                "despawnRadius=%.1f)\n",
                passIndex, tSeconds, spawned, liveVehicles.size(), kVehiclePopulationTarget, sr.spawnDist,
                sr.despawnRadius);
        }
        if (driver.ShouldRunPedHeavyPass()) {
            const double randF = RandomUnitFloat01(rng);
            SpawnRadiusResult sr = ComputeSpawnRadius(pedParams, dtMs / 1000.0, randF);
            lastPedSpawnDist = sr.spawnDist;
            lastPedDespawnRadius = sr.despawnRadius;
            const int spawned = RunPedHeavyPass(livePeds, sidewalks, driver.predictedPosition(),
                                                 driver.cameraForward(), pedParams, sr.spawnDist, sr.despawnRadius,
                                                 pickUniform, nextPedId);
            ++passIndex;
            std::printf(
                "[pass %d] t=%.3fs PED heavy pass: spawned=%d live_peds=%zu/%d (spawnDist=%.1f "
                "despawnRadius=%.1f)\n",
                passIndex, tSeconds, spawned, livePeds.size(), kPedPopulationTarget, sr.spawnDist, sr.despawnRadius);
        }
    }

    std::printf("\nFINAL LIVE POPULATION: %zu pedestrians, %zu vehicles spawned\n", livePeds.size(),
               liveVehicles.size());

    if (capturePath.empty()) {
        std::printf("(no --capture path given, skipping PNG capture)\n");
        return 0;
    }

    // --- schematic top-down capture -----------------------------------
    const uint32_t kWidth = 900, kHeight = 900;
    Canvas canvas = MakeCanvas(kWidth, kHeight, 30, 30, 36);  // dark asphalt-ish background

    ViewTransform view;
    view.width = kWidth;
    view.height = kHeight;
    view.origin = cameraPos;  // keep the camera centred in frame
    // Zoomed to a fixed +-350-unit local half-extent around the camera
    // (NOT the whole 1200-unit grid) - the live population clusters near
    // the camera's own spawn/despawn radii (low hundreds of units), so
    // framing the whole grid would shrink it to an unreadable speck; this
    // is this tool's own visualization choice, not a spec quantity.
    view.scale = (std::min(kWidth, kHeight) - 40.0) / 700.0;

    // Roads.
    for (const auto& seg : segments) {
        double x0, y0, x1, y1;
        view.ToScreen(seg.start, x0, y0);
        view.ToScreen(seg.end, x1, y1);
        DrawLine(canvas, x0, y0, x1, y1, 95, 95, 102);
    }
    // Sidewalks.
    for (const auto& n : sidewalks) {
        double x, y;
        view.ToScreen(n.pos, x, y);
        DrawDot(canvas, static_cast<int>(x), static_cast<int>(y), 1, 200, 190, 150);
    }
    // Despawn-radius rings (last computed values) around the predicted camera position.
    {
        double px, py;
        view.ToScreen(driver.predictedPosition(), px, py);
        DrawCircleOutline(canvas, static_cast<int>(px), static_cast<int>(py), lastPedDespawnRadius * view.scale, 90,
                          140, 90);
        DrawCircleOutline(canvas, static_cast<int>(px), static_cast<int>(py), lastVehicleDespawnRadius * view.scale,
                          140, 90, 80);
    }
    // Live vehicles (red/orange).
    for (const auto& v : liveVehicles) {
        double x, y;
        view.ToScreen(v.pos, x, y);
        DrawDot(canvas, static_cast<int>(x), static_cast<int>(y), 4, 230, 90, 60);
    }
    // Live pedestrians (green).
    for (const auto& p : livePeds) {
        double x, y;
        view.ToScreen(p.pos, x, y);
        DrawDot(canvas, static_cast<int>(x), static_cast<int>(y), 3, 60, 200, 90);
    }
    // Camera (blue) + forward tick.
    {
        double cx, cy;
        view.ToScreen(cameraPos, cx, cy);
        DrawDot(canvas, static_cast<int>(cx), static_cast<int>(cy), 5, 80, 160, 255);
        Vec3 tip = cameraPos + cameraForward * 40.0;
        double tx, ty;
        view.ToScreen(tip, tx, ty);
        DrawLine(canvas, cx, cy, tx, ty, 80, 160, 255);
    }
    // Predicted camera position (yellow).
    {
        double px, py;
        view.ToScreen(driver.predictedPosition(), px, py);
        DrawDot(canvas, static_cast<int>(px), static_cast<int>(py), 4, 255, 220, 60);
    }

    const std::string pngPath = capturePath + ".png";
    std::string error;
    if (!sr3render::writePng(pngPath, kWidth, kHeight, canvas.rgba, error)) {
        std::fprintf(stderr, "ambient_population_demo: failed to write '%s': %s\n", pngPath.c_str(), error.c_str());
        return 1;
    }
    std::printf("captured: %s (%ux%u schematic top-down frame - roads/sidewalks grey/tan, live pedestrians green, "
               "live vehicles red, camera blue, predicted camera yellow, despawn radii outlined)\n",
               pngPath.c_str(), kWidth, kHeight);
    return 0;
}
