// Synthetic tests for the distant ambient pedestrian/vehicle spawn-despawn
// system, spec-ai-runtime.md Sec17 (include/sr3airuntime/ambient_population.h).
//
// Covers: the flat global budget/refill constants (Sec17.2); the dt-scaled
// spawn-radius formula (Sec17.2); camera-lookahead prediction (Sec17.3); the
// ped weighted type-slot pick and the generic weighted-index primitive it's
// built on; the vehicle Spline_Type AND-test, distance-band gather, evenly-
// spaced/occupancy-filtered/sorted slot search (Sec17.3 - deliberately a
// DIFFERENT shape from ped, exercised separately so the two pipelines are
// never silently collapsed into one); despawn's distance+angle cull and the
// whole-category mass-despawn path (Sec17.4); the 6-category toggle state
// (Sec17.6); the capped-refill heavy-pass helpers; and the driver's shared-
// latch/mutual-exclusion throttle (Sec17.5).
//
// All RNG seams use sr3airuntime::DeterministicRng (navpoint_selector.h) or
// a fixed-roll test double where a specific pick needs to be pinned down -
// never a bit-exact replay of the real engine's own untraced 0x00dab660
// (ambient_population.h's own file comment explains why).

#include <iostream>
#include <vector>

#include "sr3airuntime/ambient_population.h"

using namespace sr3airuntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

// ===========================================================================
// Part A: budget/refill constants (Sec17.2).
// ===========================================================================
void testBudgetConstants() {
    CHECK(kPedPopulationTarget == 75);
    CHECK(kVehiclePopulationTarget == 75);
    CHECK(kPedRefillCapPerPass == 25);
    CHECK(kVehicleRefillCapPerPass == 20);
}

// ===========================================================================
// Part B: spawn-radius formula (Sec17.2).
// ===========================================================================
void testSpawnRadiusZeroDeltaTimeTakesIdentityBranch() {
    SpawnDespawnParams p;
    p.baseSpawningDist = 200.0;
    p.maxSpawningDist = 1200.0;
    p.despawnExpansionDistance = 25.0;
    // dt <= 0 -> spawnMul = 1.0 -> spawnDist = base * rand.
    SpawnRadiusResult r = ComputeSpawnRadius(p, /*dt=*/0.0, /*randFloat01=*/0.5);
    CHECK(r.spawnDist == 100.0);
    CHECK(r.despawnRadius == 125.0);
}

void testSpawnRadiusClampsToMax() {
    SpawnDespawnParams p;
    p.lengthPerSpawnReciprocal = 0.04;  // ped default (Sec14.3 image value)
    p.baseSpawningDist = 200.0;
    p.maxSpawningDist = 1200.0;
    p.despawnExpansionDistance = 25.0;
    // A large dt drives spawnMul huge; the base*mul product must clamp to
    // maxSpawningDist/baseSpawningDist before the final spawnDist clamp.
    SpawnRadiusResult r = ComputeSpawnRadius(p, /*dt=*/100.0, /*randFloat01=*/1.0);
    CHECK(r.spawnDist <= p.maxSpawningDist);
    CHECK(r.spawnDist == p.maxSpawningDist);  // randFloat01=1.0 pins the clamp exactly
}

void testSpawnRadiusZeroReciprocalDoesNotDivideByZero() {
    SpawnDespawnParams p;
    p.lengthPerSpawnReciprocal = 0.0;  // defensive branch, not a disassembly-confirmed guard (see file comment)
    p.baseSpawningDist = 200.0;
    p.maxSpawningDist = 1200.0;
    SpawnRadiusResult r = ComputeSpawnRadius(p, /*dt=*/16.0, /*randFloat01=*/0.25);
    CHECK(r.spawnDist == 50.0);  // identity-branch result: base * rand
}

void testDespawnRadiusAddsExpansionDistance() {
    SpawnDespawnParams p;
    p.baseSpawningDist = 200.0;
    p.maxSpawningDist = 1200.0;
    p.despawnExpansionDistance = 40.0;
    SpawnRadiusResult r = ComputeSpawnRadius(p, 0.0, 0.0);
    CHECK(r.spawnDist == 0.0);
    CHECK(r.despawnRadius == 40.0);
}

// ===========================================================================
// Part C: camera-lookahead prediction (Sec17.3).
// ===========================================================================
void testPredictCameraPosition() {
    Vec3 pos{10, 0, 0};
    Vec3 vel{4, 0, 2};
    Vec3 predicted = PredictCameraPosition(pos, vel, 0.5);
    CHECK(predicted.x == 12.0);
    CHECK(predicted.y == 0.0);
    CHECK(predicted.z == 1.0);
}

// ===========================================================================
// Part D: pedestrian selection (Sec17.3).
// ===========================================================================
void testGatherSidewalkCandidatesDistanceBoundary() {
    std::vector<SidewalkNode> nodes = {
        {1, Vec3{0, 0, 0}},
        {2, Vec3{5, 0, 0}},   // exactly at the boundary
        {3, Vec3{5.1, 0, 0}}, // just outside
    };
    auto cand = GatherSidewalkCandidates(nodes, Vec3{0, 0, 0}, 5.0);
    CHECK(cand.size() == 2);
}

void testComputePedTypeWeightsFavoursUnderRepresentedSlots() {
    std::array<int, kMaxPedTypeSlots> counts{};
    counts[0] = 5;
    counts[1] = 0;
    auto weights = ComputePedTypeWeights(counts);
    CHECK(weights[1] > weights[0]);  // slot 1 (count 0) outweighs slot 0 (count 5)
    for (double w : weights) CHECK(w >= 1.0);  // never zeroed out
}

void testPickWeightedIndexAllZeroReturnsSentinel() {
    std::vector<double> weights = {0.0, 0.0, 0.0};
    auto pick = [](size_t) -> size_t { return 0; };
    CHECK(PickWeightedIndex(weights, pick) == weights.size());
}

void testPickWeightedIndexRespectsCumulativeBoundaries() {
    // weights {1, 3} over resolution 1'000'000: index 0 owns [0, 250000),
    // index 1 owns [250000, 1000000).
    std::vector<double> weights = {1.0, 3.0};
    auto rollAt = [](size_t roll) {
        return [roll](size_t) -> size_t { return roll; };
    };
    CHECK(PickWeightedIndex(weights, rollAt(0)) == 0);
    CHECK(PickWeightedIndex(weights, rollAt(249999)) == 0);
    CHECK(PickWeightedIndex(weights, rollAt(250001)) == 1);
    CHECK(PickWeightedIndex(weights, rollAt(999999)) == 1);
}

void testSelectPedSpawnNoCandidatesNotFound() {
    std::vector<SidewalkNode> nodes = {{1, Vec3{1000, 0, 0}}};
    std::array<int, kMaxPedTypeSlots> counts{};
    auto pick = [](size_t n) -> size_t { return n > 0 ? n - 1 : 0; };
    PedSpawnResult r = SelectPedSpawn(nodes, Vec3{0, 0, 0}, /*spawnDist=*/10.0, counts, pick);
    CHECK(!r.found);
}

void testSelectPedSpawnFindsPositionAndTypeSlot() {
    std::vector<SidewalkNode> nodes = {{1, Vec3{1, 0, 0}}, {2, Vec3{2, 0, 0}}, {3, Vec3{3, 0, 0}}};
    std::array<int, kMaxPedTypeSlots> counts{};
    DeterministicRng rng(12345);
    auto pick = MakePickUniform(rng);
    PedSpawnResult r = SelectPedSpawn(nodes, Vec3{0, 0, 0}, /*spawnDist=*/10.0, counts, pick);
    CHECK(r.found);
    CHECK(r.typeSlot >= 0 && static_cast<size_t>(r.typeSlot) < kMaxPedTypeSlots);
    bool matchesACandidate = false;
    for (const auto& n : nodes) {
        if (n.pos.x == r.position.x && n.pos.y == r.position.y && n.pos.z == r.position.z) matchesACandidate = true;
    }
    CHECK(matchesACandidate);
}

// ===========================================================================
// Part E: vehicle selection (Sec17.3) - a DIFFERENT shape from ped, tested
// separately on purpose.
// ===========================================================================
void testSplineTypeEligibleAndTest() {
    CHECK(SplineTypeEligible(0b0110u, 0b0010u));   // overlap
    CHECK(!SplineTypeEligible(0b0110u, 0b1000u));  // no overlap
}

void testGatherTrafficCandidatesFiltersByBandAndSplineType() {
    TrafficSegment nearEligible{1, Vec3{0, 0, 0}, Vec3{2, 0, 0}, 0x1u};
    TrafficSegment nearIneligible{2, Vec3{0, 0, 0}, Vec3{2, 0, 0}, 0x2u};
    TrafficSegment farEligible{3, Vec3{500, 0, 0}, Vec3{502, 0, 0}, 0x1u};
    std::vector<TrafficSegment> segs = {nearEligible, nearIneligible, farEligible};

    auto cand = GatherTrafficCandidates(segs, Vec3{0, 0, 0}, /*inner=*/0.0, /*outer=*/10.0, /*vehicleTypeMask=*/0x1u);
    CHECK(cand.size() == 1);
    CHECK(cand[0]->id == 1);
}

void testBuildSegmentSlotsCapsAtFive() {
    TrafficSegment seg{1, Vec3{0, 0, 0}, Vec3{100, 0, 0}, 0x1u};
    auto slots = BuildSegmentSlots(seg, /*requestedSlotCount=*/999);
    CHECK(slots.size() == kMaxVehicleSlotsPerSegment);
    CHECK(SegmentSlotSpacing(seg, 999) == 100.0 / static_cast<double>(kMaxVehicleSlotsPerSegment));
}

void testBuildSegmentSlotsEvenlySpaced() {
    TrafficSegment seg{1, Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0x1u};
    auto slots = BuildSegmentSlots(seg, 2);
    CHECK(slots.size() == 2);
    CHECK(slots[0].pos.x == 2.5);  // (0+0.5)/2 * 10
    CHECK(slots[1].pos.x == 7.5);  // (1+0.5)/2 * 10
}

void testMarkOccupiedSlotsAndSortedUnoccupied() {
    TrafficSegment seg{1, Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0x1u};
    auto slots = BuildSegmentSlots(seg, 5);  // 5 slots at x=1,3,5,7,9
    std::vector<Vec3> liveVehicles = {Vec3{3, 0, 0}};
    MarkOccupiedSlots(slots, liveVehicles, /*minSeparation=*/0.5);
    auto sorted = SortedUnoccupiedSlots(slots);
    CHECK(sorted.size() == 4);  // the slot at x=3 is occupied, the rest survive
    for (size_t i = 0; i + 1 < sorted.size(); ++i) CHECK(sorted[i]->t < sorted[i + 1]->t);  // ascending order
    bool stillHasOccupiedSlot = false;
    for (const auto* s : sorted) {
        if (s->pos.x == 3.0) stillHasOccupiedSlot = true;
    }
    CHECK(!stillHasOccupiedSlot);
}

void testSelectVehicleSpawnFindsFreeSlot() {
    TrafficSegment seg{1, Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0x1u};
    std::vector<TrafficSegment> segs = {seg};
    DeterministicRng rng(42);
    auto pick = MakePickUniform(rng);
    VehicleSpawnResult r =
        SelectVehicleSpawn(segs, Vec3{0, 0, 0}, 0.0, 20.0, 0x1u, /*liveVehiclePositions=*/{}, 0.5, 5, pick);
    CHECK(r.found);
    CHECK(r.trafficTypeMask == 0x1u);
}

void testSelectVehicleSpawnAllSlotsOccupiedNotFound() {
    TrafficSegment seg{1, Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0x1u};
    std::vector<TrafficSegment> segs = {seg};
    // Every one of the 5 evenly-spaced slots (x=1,3,5,7,9) within 0.5 of a
    // live vehicle.
    std::vector<Vec3> liveVehicles = {Vec3{1, 0, 0}, Vec3{3, 0, 0}, Vec3{5, 0, 0}, Vec3{7, 0, 0}, Vec3{9, 0, 0}};
    DeterministicRng rng(7);
    auto pick = MakePickUniform(rng);
    VehicleSpawnResult r =
        SelectVehicleSpawn(segs, Vec3{0, 0, 0}, 0.0, 20.0, 0x1u, liveVehicles, 0.5, 5, pick);
    CHECK(!r.found);
}

void testSelectVehicleSpawnNoEligibleSegmentNotFound() {
    TrafficSegment seg{1, Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0x2u};  // wrong Spline_Type
    std::vector<TrafficSegment> segs = {seg};
    DeterministicRng rng(1);
    auto pick = MakePickUniform(rng);
    VehicleSpawnResult r = SelectVehicleSpawn(segs, Vec3{0, 0, 0}, 0.0, 20.0, 0x1u, {}, 0.5, 5, pick);
    CHECK(!r.found);
}

// ===========================================================================
// Part F: despawn (Sec17.4).
// ===========================================================================
void testShouldDespawnEntityDistanceCull() {
    Vec3 cam{0, 0, 0};
    Vec3 forward{1, 0, 0};
    CHECK(ShouldDespawnEntity(Vec3{100, 0, 0}, cam, forward, /*radius=*/50.0, /*cosThresh=*/-1.0));
    CHECK(!ShouldDespawnEntity(Vec3{10, 0, 0}, cam, forward, /*radius=*/50.0, /*cosThresh=*/-1.0));
}

void testShouldDespawnEntityAngleCull() {
    Vec3 cam{0, 0, 0};
    Vec3 forward{1, 0, 0};
    // Directly behind the camera (cos = -1) with a 0.6 threshold -> culled
    // even though well within radius.
    CHECK(ShouldDespawnEntity(Vec3{-10, 0, 0}, cam, forward, /*radius=*/50.0, /*cosThresh=*/0.6));
    // Directly ahead (cos = 1) survives.
    CHECK(!ShouldDespawnEntity(Vec3{10, 0, 0}, cam, forward, /*radius=*/50.0, /*cosThresh=*/0.6));
}

void testMassDespawnCategoryClearsEverything() {
    std::vector<LivePed> peds = {{1, Vec3{}, 0}, {2, Vec3{}, 1}};
    MassDespawnCategory(peds);
    CHECK(peds.empty());
}

void testAmbientToggleStateDefaultsAllEnabled() {
    AmbientToggleState s;
    for (int i = 0; i < static_cast<int>(ToggleCategory::kCount); ++i) {
        CHECK(s.AnyEnabled(static_cast<ToggleCategory>(i)));
    }
}

void testAmbientToggleStateDisablingAllSlotsTurnsCategoryOff() {
    AmbientToggleState s;
    for (bool& b : s.enabled[static_cast<size_t>(ToggleCategory::kPed)]) b = false;
    CHECK(!s.AnyEnabled(ToggleCategory::kPed));
    CHECK(s.AnyEnabled(ToggleCategory::kVehicle));  // unaffected
}

// ===========================================================================
// Part G: capped-refill heavy-pass helpers (Sec17.2/17.4 combined).
// ===========================================================================
void testRunPedHeavyPassRespectsPerPassCapAndBudget() {
    std::vector<SidewalkNode> nodes;
    for (int i = 0; i < 100; ++i) nodes.push_back({static_cast<uint64_t>(i), Vec3{static_cast<double>(i), 0, 0}});

    std::vector<LivePed> live;  // starts empty
    SpawnDespawnParams params;
    DeterministicRng rng(99);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 1;

    int spawned = RunPedHeavyPass(live, nodes, Vec3{0, 0, 0}, Vec3{1, 0, 0}, params, /*spawnDist=*/50.0,
                                   /*despawnRadius=*/1e9, pick, nextId);
    CHECK(spawned == kPedRefillCapPerPass);  // first pass is cap-limited, not budget-limited
    CHECK(live.size() == static_cast<size_t>(kPedRefillCapPerPass));

    // Run enough passes to approach the budget; never exceeds it.
    for (int pass = 0; pass < 5; ++pass) {
        RunPedHeavyPass(live, nodes, Vec3{0, 0, 0}, Vec3{1, 0, 0}, params, 50.0, 1e9, pick, nextId);
        CHECK(static_cast<int>(live.size()) <= kPedPopulationTarget);
    }
    CHECK(static_cast<int>(live.size()) == kPedPopulationTarget);
}

void testRunPedHeavyPassDespawnsOutOfRangeEntities() {
    std::vector<SidewalkNode> nodes = {{1, Vec3{0, 0, 0}}};
    std::vector<LivePed> live = {{1, Vec3{10000, 0, 0}, 0}};  // far outside any reasonable radius
    SpawnDespawnParams params;
    DeterministicRng rng(1);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 2;
    RunPedHeavyPass(live, nodes, Vec3{0, 0, 0}, Vec3{1, 0, 0}, params, /*spawnDist=*/1.0, /*despawnRadius=*/50.0,
                     pick, nextId);
    for (const auto& p : live) CHECK(p.id != 1);  // the far entity was culled, not left in the list
}

void testRunVehicleHeavyPassRespectsPerPassCap() {
    std::vector<TrafficSegment> segs;
    for (int i = 0; i < 20; ++i) {
        double x = static_cast<double>(i) * 20.0;
        segs.push_back({static_cast<uint64_t>(i), Vec3{x, 0, 0}, Vec3{x + 10, 0, 0}, 0x1u});
    }
    std::vector<VehicleTrafficType> types = {{0xAAu, 0x1u, 0.04, 0.0, 4.5}};
    std::vector<LiveVehicle> live;
    SpawnDespawnParams params;
    DeterministicRng rng(55);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 1;

    int spawned = RunVehicleHeavyPass(live, segs, types, Vec3{0, 0, 0}, Vec3{1, 0, 0}, params, /*innerDist=*/0.0,
                                       /*spawnDist=*/400.0, /*despawnRadius=*/1e9, /*minSlotSeparation=*/0.5,
                                       /*requestedSlotCount=*/5, pick, nextId);
    CHECK(spawned <= kVehicleRefillCapPerPass);
    CHECK(static_cast<int>(live.size()) == spawned);
}

// ===========================================================================
// Part H: driver throttle/mutual-exclusion (Sec17.5).
// ===========================================================================
void testDriverFirstFrameRunsVehicleBeforePed() {
    AmbientPopulationDriver driver;
    driver.RefreshCameraPrediction(Vec3{0, 0, 0}, Vec3{0, 0, 0}, Vec3{1, 0, 0}, 0.25);
    driver.BeginFrame(16);
    // Both countdowns start expired (0) - vehicle is asked first and wins
    // the shared latch; ped must defer to its own next expiry.
    CHECK(driver.ShouldRunVehicleHeavyPass());
    CHECK(!driver.ShouldRunPedHeavyPass());
}

void testDriverLatchResetsEachFrame() {
    AmbientPopulationDriver driver(1000);
    driver.BeginFrame(16);
    CHECK(driver.ShouldRunVehicleHeavyPass());
    CHECK(!driver.ShouldRunPedHeavyPass());  // blocked this frame, deferred

    // Next frame: vehicle's countdown was just reset to 1000ms, so it must
    // NOT fire again immediately. Ped's countdown was left expired (CHOSEN
    // semantics, file header item 7, not re-armed), so with the latch fresh
    // again this frame, ped now runs - one frame after being blocked, not
    // starved for a further ~1s.
    driver.BeginFrame(16);
    CHECK(!driver.ShouldRunVehicleHeavyPass());
    CHECK(driver.ShouldRunPedHeavyPass());
}

void testDriverPedNeverStarvesAcrossManyFrames() {
    // Regression guard for the starvation pathology this file's own CHOSEN
    // note (item 7) explains was found and rejected: with equal periods and
    // vehicle always asked first, a naive "re-arm the blocked side to a
    // full fresh period" policy would let vehicle win the shared latch
    // forever and ped would never run. Simulate several seconds and assert
    // ped actually gets to run more than once.
    AmbientPopulationDriver driver(1000);
    int pedRuns = 0, vehicleRuns = 0;
    for (int frame = 0; frame < 400; ++frame) {  // 400 * 16ms = 6.4s simulated
        driver.BeginFrame(16);
        if (driver.ShouldRunVehicleHeavyPass()) ++vehicleRuns;
        if (driver.ShouldRunPedHeavyPass()) ++pedRuns;
    }
    CHECK(vehicleRuns >= 5);  // ~1 per second over 6.4s
    CHECK(pedRuns >= 5);      // must NOT be stuck at 0 or 1
}

void testDriverCameraPredictionReflectsLatestRefresh() {
    AmbientPopulationDriver driver;
    driver.RefreshCameraPrediction(Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{0, 0, 1}, 0.5);
    CHECK(driver.predictedPosition().x == 5.0);
    CHECK(driver.cameraForward().z == 1.0);
}

}  // namespace

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

int main() {
    run("testBudgetConstants", testBudgetConstants);

    run("testSpawnRadiusZeroDeltaTimeTakesIdentityBranch", testSpawnRadiusZeroDeltaTimeTakesIdentityBranch);
    run("testSpawnRadiusClampsToMax", testSpawnRadiusClampsToMax);
    run("testSpawnRadiusZeroReciprocalDoesNotDivideByZero", testSpawnRadiusZeroReciprocalDoesNotDivideByZero);
    run("testDespawnRadiusAddsExpansionDistance", testDespawnRadiusAddsExpansionDistance);

    run("testPredictCameraPosition", testPredictCameraPosition);

    run("testGatherSidewalkCandidatesDistanceBoundary", testGatherSidewalkCandidatesDistanceBoundary);
    run("testComputePedTypeWeightsFavoursUnderRepresentedSlots", testComputePedTypeWeightsFavoursUnderRepresentedSlots);
    run("testPickWeightedIndexAllZeroReturnsSentinel", testPickWeightedIndexAllZeroReturnsSentinel);
    run("testPickWeightedIndexRespectsCumulativeBoundaries", testPickWeightedIndexRespectsCumulativeBoundaries);
    run("testSelectPedSpawnNoCandidatesNotFound", testSelectPedSpawnNoCandidatesNotFound);
    run("testSelectPedSpawnFindsPositionAndTypeSlot", testSelectPedSpawnFindsPositionAndTypeSlot);

    run("testSplineTypeEligibleAndTest", testSplineTypeEligibleAndTest);
    run("testGatherTrafficCandidatesFiltersByBandAndSplineType", testGatherTrafficCandidatesFiltersByBandAndSplineType);
    run("testBuildSegmentSlotsCapsAtFive", testBuildSegmentSlotsCapsAtFive);
    run("testBuildSegmentSlotsEvenlySpaced", testBuildSegmentSlotsEvenlySpaced);
    run("testMarkOccupiedSlotsAndSortedUnoccupied", testMarkOccupiedSlotsAndSortedUnoccupied);
    run("testSelectVehicleSpawnFindsFreeSlot", testSelectVehicleSpawnFindsFreeSlot);
    run("testSelectVehicleSpawnAllSlotsOccupiedNotFound", testSelectVehicleSpawnAllSlotsOccupiedNotFound);
    run("testSelectVehicleSpawnNoEligibleSegmentNotFound", testSelectVehicleSpawnNoEligibleSegmentNotFound);

    run("testShouldDespawnEntityDistanceCull", testShouldDespawnEntityDistanceCull);
    run("testShouldDespawnEntityAngleCull", testShouldDespawnEntityAngleCull);
    run("testMassDespawnCategoryClearsEverything", testMassDespawnCategoryClearsEverything);
    run("testAmbientToggleStateDefaultsAllEnabled", testAmbientToggleStateDefaultsAllEnabled);
    run("testAmbientToggleStateDisablingAllSlotsTurnsCategoryOff", testAmbientToggleStateDisablingAllSlotsTurnsCategoryOff);

    run("testRunPedHeavyPassRespectsPerPassCapAndBudget", testRunPedHeavyPassRespectsPerPassCapAndBudget);
    run("testRunPedHeavyPassDespawnsOutOfRangeEntities", testRunPedHeavyPassDespawnsOutOfRangeEntities);
    run("testRunVehicleHeavyPassRespectsPerPassCap", testRunVehicleHeavyPassRespectsPerPassCap);

    run("testDriverFirstFrameRunsVehicleBeforePed", testDriverFirstFrameRunsVehicleBeforePed);
    run("testDriverLatchResetsEachFrame", testDriverLatchResetsEachFrame);
    run("testDriverPedNeverStarvesAcrossManyFrames", testDriverPedNeverStarvesAcrossManyFrames);
    run("testDriverCameraPredictionReflectsLatestRefresh", testDriverCameraPredictionReflectsLatestRefresh);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic ambient-population tests passed.\n";
    return 0;
}
