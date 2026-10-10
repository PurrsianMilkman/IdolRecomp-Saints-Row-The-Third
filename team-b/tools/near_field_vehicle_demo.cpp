// near_field_vehicle_demo - a deterministic, headless demo tool for the
// near-field (fully-simulated) ambient-vehicle maintenance system
// (spec-ai-runtime.md Sec17.4c,
// include/sr3airuntime/near_field_vehicle_population.h). Sibling to
// tools/ambient_population_demo.cpp (the distant/LOD tier's own demo) -
// same synthetic-grid/schematic-top-down-capture idiom, same reason for a
// dedicated tool rather than extending city_corner (see that file's own top
// comment; unchanged here: no C++ reader for the real Traffic/Sidewalks
// `.czn_pc` content kinds exists in this project yet).
//
// WHAT THIS PROVES, end to end against the real algorithm (not a mock-up):
//   1. The near-field tier's own 48-vehicle cap and the distant tier's own
//      75-vehicle cap (ambient_population.h) are genuinely independent -
//      both pools are driven toward their own caps SIMULTANEOUSLY in the
//      SAME simulated run, via two separate std::vector<T> lists of two
//      separate types, with zero shared state (Sec17.4c's own "honest
//      negative" - no object hand-off between tiers).
//   2. Despawn fires for an out-of-range, unimportant-occupant near-field
//      vehicle (the per-vehicle eligibility predicate, 0x00906b00).
//   3. An important-occupant near-field vehicle is never despawned, even
//      when far out of range - scripted explicitly, not left to chance.
//   4. The shared 150-human headroom gate actually blocks a near-field
//      spawn attempt when the budget is exhausted, even with cap headroom
//      and a valid spawn candidate available.
//
// DETERMINISM. Every input is fixed (grid layout, driver timer seams,
// scripted scenario vehicles, RNG seed, frame count) - no wall-clock, no
// live input - so stdout is byte-reproducible run to run. This tool is
// NOT wired into tools/golden_scene_check.cpp's frozen-baseline system
// (tests/golden/ is gitignored/"regenerated locally" per this repo's own
// .gitignore comment, and no such baseline exists for this brand-new tool
// in a fresh worktree yet) - its own proof is the printed stdout narrative
// below, read directly, same spirit as the synthetic ctest coverage
// (tests/synthetic_near_field_vehicle_population_test.cpp) this tool is a
// companion to, not a replacement for.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_vehicle_population.h"

using namespace sr3airuntime;

namespace {

// A small synthetic road grid, the SAME scale idiom
// ambient_population_demo.cpp's own BuildRoadGrid() uses (not re-derived
// from a real Traffic/Sidewalks read - see this file's own top comment).
constexpr double kGridLines[5] = {-600.0, -300.0, 0.0, 300.0, 600.0};
constexpr double kSidewalkStep = 20.0;
constexpr uint32_t kRoadSplineTypeMask = 0x1u;

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

// Up to kNearFieldSpawnCandidateSlots (3) candidate positions for the
// near-field spawn buffer - CHOSEN candidate source (near_field_vehicle_
// population.h's own file header, CHOSEN item 1): reuses the distant
// tier's own road-graph gather, sampled down to 3 points, rather than a
// second graph-walk invented from nothing.
std::vector<Vec3> GatherNearFieldCandidates(const std::vector<TrafficSegment>& segments, const Vec3& predictedPos,
                                             double outerDist, const std::function<size_t(size_t)>& pickUniform) {
    auto segCandidates = GatherTrafficCandidates(segments, predictedPos, /*innerDist=*/0.0, outerDist,
                                                  kRoadSplineTypeMask);
    std::vector<Vec3> out;
    for (size_t i = 0; i < kNearFieldSpawnCandidateSlots && !segCandidates.empty(); ++i) {
        size_t idx = pickUniform(segCandidates.size());
        if (idx >= segCandidates.size()) idx = 0;
        out.push_back(segCandidates[idx]->PointAt(0.5));
    }
    return out;
}

void PrintSeparator() { std::printf("------------------------------------------------------------\n"); }

}  // namespace

int main(int argc, char** argv) {
    int frames = 1200;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "near_field_vehicle_demo: unknown argument '%s'\n", a.c_str());
            return 1;
        }
    }

    std::printf(
        "near_field_vehicle_demo: spec-ai-runtime.md Sec17.4c near-field (fully-simulated) ambient-vehicle "
        "population\n");
    std::printf("cap=%d (near-field) vs. cap=%d (distant/LOD tier, ambient_population.h) - two INDEPENDENT pools\n",
                kNearFieldVehicleCap, kVehiclePopulationTarget);
    std::printf("shared human headroom ceiling=%d\n", kHumanEligibilityCeiling);
    std::printf("\n");

    // ===== Scenario 1: both pools driven toward their own caps simultaneously =====
    PrintSeparator();
    std::printf("SCENARIO 1: near-field 48-cap and distant 75-cap filled SIMULTANEOUSLY, independently\n");
    PrintSeparator();

    const std::vector<TrafficSegment> segments = BuildRoadGrid();
    const std::vector<VehicleTrafficType> distantTypes = {
        {/*nameHash=*/0x1234u, /*splineTypeMask=*/kRoadSplineTypeMask, /*lengthPerSpawnReciprocal=*/100.0,
         /*noSpawnZone=*/0.0, /*dummyCarLength=*/4.5}};

    std::vector<NearFieldVehicle> nearFieldLive;
    std::vector<LiveVehicle> distantLive;
    uint64_t nextNearFieldId = 1, nextDistantId = 1;

    NearFieldVehicleDriver nearFieldDriver;
    AmbientPopulationDriver distantDriver(/*heavyPassPeriodMs=*/1000);
    AmbientToggleState toggles;  // "os_vehicle" + every other category default-enabled
    DeterministicRng rng(20261008u);
    auto pickUniform = MakePickUniform(rng);

    SpawnDespawnParams distantVehicleParams;
    distantVehicleParams.lengthPerSpawnReciprocal = 100.0;
    distantVehicleParams.baseSpawningDist = 200.0;
    distantVehicleParams.maxSpawningDist = 1200.0;
    distantVehicleParams.despawnExpansionDistance = 25.0;
    distantVehicleParams.despawnAngleCosThreshold = -1.0;  // CHOSEN for this demo only: no angle cull, isolate the cap/budget behaviour being demonstrated from the distant tier's own camera-cone despawn

    const Vec3 cameraPos{0, 0, 0};
    const Vec3 cameraForward{0, 0, 1.0};
    const int64_t dtMs = 16;
    int eligibleHumanCount = 0;  // plenty of headroom in this scenario - the cap, not the human ceiling, is what's exercised here

    for (int frame = 0; frame < frames; ++frame) {
        const int64_t nowMs = static_cast<int64_t>(frame) * dtMs;

        // Near-field tier.
        bool nfMaintenance = nearFieldDriver.ShouldRunMaintenancePass(nowMs, toggles, /*zoneDataLoaded=*/true);
        bool nfDespawnSchedule = nearFieldDriver.ShouldRunDespawnSchedule(nowMs);
        if (nfMaintenance || nfDespawnSchedule) {
            std::vector<Vec3> candidates = GatherNearFieldCandidates(segments, cameraPos, 400.0, pickUniform);
            RunNearFieldVehicleMaintenancePass(nearFieldLive, /*runDespawn=*/nfDespawnSchedule,
                                                /*runSpawn=*/nfMaintenance, cameraPos, /*distSq=*/1e12,
                                                eligibleHumanCount, candidates, pickUniform, nextNearFieldId);
        }

        // Distant/LOD tier - entirely separate call, entirely separate list.
        distantDriver.BeginFrame(dtMs);
        distantDriver.RefreshCameraPrediction(cameraPos, Vec3{0, 0, 0}, cameraForward, 0.25);
        if (distantDriver.ShouldRunVehicleHeavyPass()) {
            SpawnRadiusResult sr = ComputeSpawnRadius(distantVehicleParams, dtMs / 1000.0, RandomUnitFloat01(rng));
            RunVehicleHeavyPass(distantLive, segments, distantTypes, distantDriver.predictedPosition(),
                                 distantDriver.cameraForward(), distantVehicleParams, /*innerDist=*/0.0, sr.spawnDist,
                                 sr.despawnRadius, /*minSlotSeparation=*/6.0, /*requestedSlotCount=*/5, pickUniform,
                                 nextDistantId);
        }
    }

    std::printf("after %d simulated frames (%.1fs):\n", frames, (frames * dtMs) / 1000.0);
    std::printf("  near-field live vehicles: %zu / %d cap\n", nearFieldLive.size(), kNearFieldVehicleCap);
    std::printf("  distant live vehicles   : %zu / %d cap\n", distantLive.size(), kVehiclePopulationTarget);
    const bool bothNearOwnCaps =
        static_cast<int>(nearFieldLive.size()) == kNearFieldVehicleCap && static_cast<int>(distantLive.size()) > kNearFieldVehicleCap;
    std::printf("  RESULT: %s - near-field reached its own %d-cap while distant independently exceeded it toward "
                "its own %d-cap; two separate pools, not one shared list\n",
                bothNearOwnCaps ? "PASS" : "FAIL", kNearFieldVehicleCap, kVehiclePopulationTarget);
    std::printf("\n");

    // ===== Scenario 2/3: despawn eligibility - out-of-range culled, important-occupant protected =====
    PrintSeparator();
    std::printf("SCENARIO 2/3: despawn fires for an out-of-range/unimportant vehicle; an important-occupant\n");
    std::printf("              vehicle is NEVER despawned, even though it is just as far out of range\n");
    PrintSeparator();

    std::vector<NearFieldVehicle> scriptedLive;
    NearFieldVehicle farUnimportant;
    farUnimportant.id = 101;
    farUnimportant.pos = Vec3{50000, 0, 0};
    farUnimportant.inPlayerWorldCell = true;
    scriptedLive.push_back(farUnimportant);

    NearFieldVehicle farImportant;
    farImportant.id = 102;
    farImportant.pos = Vec3{50000, 0, 0};  // identically far
    farImportant.inPlayerWorldCell = true;
    farImportant.seatHasImportantOccupant[2] = true;  // one flagged-important occupant, seat 2 of 8
    scriptedLive.push_back(farImportant);

    const double despawnDistSq = 10000.0;  // matches spec-tables-traffic-ai.md Sec26.3's own real sample's player_vehicle_despawn_distance=10000 reading
    std::printf("before: vehicle #%llu (far, no important occupant), vehicle #%llu (far, seat 2 important)\n",
                static_cast<unsigned long long>(farUnimportant.id), static_cast<unsigned long long>(farImportant.id));
    uint64_t scratchId = 1000;
    std::vector<Vec3> noCandidates;
    NearFieldMaintenanceResult scriptedResult =
        RunNearFieldVehicleMaintenancePass(scriptedLive, /*runDespawn=*/true, /*runSpawn=*/false, Vec3{0, 0, 0},
                                            despawnDistSq, /*eligibleHumanCount=*/0, noCandidates, pickUniform,
                                            scratchId);
    bool unimportantGone = true, importantSurvived = false;
    for (const auto& v : scriptedLive) {
        if (v.id == 101) unimportantGone = false;
        if (v.id == 102) importantSurvived = true;
    }
    std::printf("after : despawned=%d, vehicle #101 present=%s, vehicle #102 present=%s\n", scriptedResult.despawned,
                unimportantGone ? "false (correctly culled)" : "TRUE (should have been culled!)",
                importantSurvived ? "true (correctly protected)" : "FALSE (should never happen!)");
    const bool scenario23Pass = unimportantGone && importantSurvived && scriptedResult.despawned == 1;
    std::printf("  RESULT: %s\n", scenario23Pass ? "PASS" : "FAIL");
    std::printf("\n");

    // ===== Scenario 4: human-ceiling gate blocks a spawn attempt =====
    PrintSeparator();
    std::printf("SCENARIO 4: shared %d-human headroom gate blocks a near-field spawn attempt when exhausted\n",
                kHumanEligibilityCeiling);
    PrintSeparator();

    std::vector<NearFieldVehicle> headroomLive;  // empty - plenty of vehicle-cap headroom
    std::vector<Vec3> validCandidates = {Vec3{100, 0, 0}};
    uint64_t headroomNextId = 1;
    std::printf("vehicle-cap headroom: %zu / %d live (plenty of room); candidate slots supplied: %zu\n",
                headroomLive.size(), kNearFieldVehicleCap, validCandidates.size());

    std::printf("-- attempt with eligibleHumanCount=%d (headroom=%d, positive) --\n", kHumanEligibilityCeiling - 1,
                RemainingHumanHeadroom(kHumanEligibilityCeiling - 1));
    NearFieldMaintenanceResult headroomOkResult = RunNearFieldVehicleMaintenancePass(
        headroomLive, /*runDespawn=*/false, /*runSpawn=*/true, Vec3{0, 0, 0}, /*distSq=*/1.0,
        /*eligibleHumanCount=*/kHumanEligibilityCeiling - 1, validCandidates, pickUniform, headroomNextId);
    std::printf("   spawned=%d (expected 1)\n", headroomOkResult.spawned);

    std::printf("-- attempt with eligibleHumanCount=%d (headroom=%d, NOT positive) --\n", kHumanEligibilityCeiling,
                RemainingHumanHeadroom(kHumanEligibilityCeiling));
    NearFieldMaintenanceResult headroomBlockedResult = RunNearFieldVehicleMaintenancePass(
        headroomLive, /*runDespawn=*/false, /*runSpawn=*/true, Vec3{0, 0, 0}, /*distSq=*/1.0,
        /*eligibleHumanCount=*/kHumanEligibilityCeiling, validCandidates, pickUniform, headroomNextId);
    std::printf("   spawned=%d (expected 0 - blocked despite cap headroom and a valid candidate)\n",
                headroomBlockedResult.spawned);
    const bool scenario4Pass = headroomOkResult.spawned == 1 && headroomBlockedResult.spawned == 0;
    std::printf("  RESULT: %s\n", scenario4Pass ? "PASS" : "FAIL");
    std::printf("\n");

    PrintSeparator();
    const bool allPass = bothNearOwnCaps && scenario23Pass && scenario4Pass;
    std::printf("OVERALL: %s\n", allPass ? "ALL SCENARIOS PASS" : "AT LEAST ONE SCENARIO FAILED");
    PrintSeparator();

    return allPass ? 0 : 1;
}
