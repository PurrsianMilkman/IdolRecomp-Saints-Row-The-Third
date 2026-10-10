// Synthetic tests for the near-field (fully-simulated) ambient-vehicle
// maintenance system, spec-ai-runtime.md Sec17.4c
// (include/sr3airuntime/near_field_vehicle_population.h).
//
// Covers: the 48-vehicle hard cap and 150-human shared headroom ceiling
// (Sec17.4c); the per-vehicle despawn-eligibility predicate (force-no-spawn,
// 8-seat important-occupant check, player-cell-gated squared-distance test);
// the 3-slot spawn final-pick; the despawn-schedule bucket-threshold gate;
// the dual randomized-timer cadence (200-300ms maintenance, 400-600ms
// despawn-schedule, independent of each other); and the combined
// maintenance pass - including the required proof that this tier's own
// 48-cap and ambient_population.h's distant-tier 75-cap are genuinely
// independent (both can run near their own caps simultaneously with no
// cross-interference, since they are two separate pools/types, not one
// list with a tier flag).
//
// All RNG seams use sr3airuntime::DeterministicRng (navpoint_selector.h) -
// never a bit-exact replay of the real engine's own untraced 0x00dab660
// (ambient_population.h's/near_field_vehicle_population.h's own file
// comments explain why).

#include <iostream>
#include <vector>

#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_vehicle_population.h"

using namespace sr3airuntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

// ===========================================================================
// Part A: concrete population numbers (Sec17.4c).
// ===========================================================================
void testNearFieldVehicleCapIs48() {
    CHECK(kNearFieldVehicleCap == 48);
}

void testHumanEligibilityCeilingIs150() {
    CHECK(kHumanEligibilityCeiling == 150);
}

void testHasNearFieldVehicleCapacity() {
    CHECK(HasNearFieldVehicleCapacity(0));
    CHECK(HasNearFieldVehicleCapacity(47));
    CHECK(!HasNearFieldVehicleCapacity(48));
    CHECK(!HasNearFieldVehicleCapacity(49));
}

void testHumanHeadroomGate() {
    CHECK(RemainingHumanHeadroom(0) == 150);
    CHECK(RemainingHumanHeadroom(149) == 1);
    CHECK(RemainingHumanHeadroom(150) == 0);
    CHECK(HasHumanHeadroomForSpawn(149));   // 150-149=1 > 0
    CHECK(!HasHumanHeadroomForSpawn(150));  // 150-150=0, not positive -> blocks the spawn pass
    CHECK(!HasHumanHeadroomForSpawn(151));  // over ceiling: still blocked, not negative-headroom UB
}

// ===========================================================================
// Part B: the per-vehicle despawn-eligibility predicate (Sec17.4c,
// 0x00906b00) - the required "despawn fires for X / never for Y" proof.
// ===========================================================================
void testDespawnEligibleOutOfRangeUnimportantInPlayerCell() {
    NearFieldVehicle v;
    v.pos = Vec3{10000, 0, 0};  // far from the player
    v.inPlayerWorldCell = true;
    v.forceNoSpawnBlocked = false;
    // seatHasImportantOccupant defaults to all-false (no important occupant)
    CHECK(IsNearFieldVehicleDespawnEligible(v, Vec3{0, 0, 0}, /*distSq=*/100.0 * 100.0));
}

void testDespawnNotEligibleWhenInRange() {
    NearFieldVehicle v;
    v.pos = Vec3{50, 0, 0};  // within the despawn distance
    v.inPlayerWorldCell = true;
    CHECK(!IsNearFieldVehicleDespawnEligible(v, Vec3{0, 0, 0}, /*distSq=*/100.0 * 100.0));
}

void testDespawnNeverEligibleWithImportantOccupantRegardlessOfDistance() {
    NearFieldVehicle v;
    v.pos = Vec3{999999, 0, 0};  // extremely far - would otherwise be eligible
    v.inPlayerWorldCell = true;
    v.seatHasImportantOccupant[3] = true;  // one flagged-important occupant, seat 3 of 8
    CHECK(!IsNearFieldVehicleDespawnEligible(v, Vec3{0, 0, 0}, /*distSq=*/100.0 * 100.0));
}

void testDespawnNeverEligibleWhenForceNoSpawnBlocked() {
    NearFieldVehicle v;
    v.pos = Vec3{999999, 0, 0};
    v.inPlayerWorldCell = true;
    v.forceNoSpawnBlocked = true;
    CHECK(!IsNearFieldVehicleDespawnEligible(v, Vec3{0, 0, 0}, /*distSq=*/100.0 * 100.0));
}

void testDespawnEligibleOutsidePlayerCellDistanceGateDoesNotApply() {
    // CHOSEN reading (this file's own header note): outside the player's
    // own world-cell, the distance half of the test does not veto - a
    // vehicle right next to the player's world position but flagged as
    // outside their cell is still eligible (force-no-spawn/important-
    // occupant are the only vetoes that still apply).
    NearFieldVehicle v;
    v.pos = Vec3{0, 0, 0};  // right at the player's position
    v.inPlayerWorldCell = false;
    CHECK(IsNearFieldVehicleDespawnEligible(v, Vec3{0, 0, 0}, /*distSq=*/100.0 * 100.0));
}

void testAnySeatHasImportantOccupant() {
    std::array<bool, kNearFieldSeatCount> seats{};
    CHECK(!AnySeatHasImportantOccupant(seats));
    seats[7] = true;  // last seat
    CHECK(AnySeatHasImportantOccupant(seats));
}

// ===========================================================================
// Part C: spawn final-pick (Sec17.4c, 3-slot buffer + shared uniform pick).
// ===========================================================================
void testSelectNearFieldVehicleSpawnNoCandidatesNotFound() {
    std::vector<Vec3> candidates;
    DeterministicRng rng(1);
    auto pick = MakePickUniform(rng);
    NearFieldSpawnResult r = SelectNearFieldVehicleSpawn(candidates, pick);
    CHECK(!r.found);
}

void testSelectNearFieldVehicleSpawnPicksAmongUpToThreeSlots() {
    std::vector<Vec3> candidates = {Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{3, 0, 0}};
    CHECK(candidates.size() == kNearFieldSpawnCandidateSlots);
    DeterministicRng rng(777);
    auto pick = MakePickUniform(rng);
    NearFieldSpawnResult r = SelectNearFieldVehicleSpawn(candidates, pick);
    CHECK(r.found);
    bool matchesACandidate = false;
    for (const auto& c : candidates) {
        if (c.x == r.position.x) matchesACandidate = true;
    }
    CHECK(matchesACandidate);
}

// ===========================================================================
// Part D: despawn-schedule bucket-threshold gate (Sec17.4c CHOSEN item 2).
// Real sample shape from spec-tables-traffic-ai.md Sec26.3's own extracted
// vehicle_despawn.xtbl: in-view {5 cars,30s}...{8 cars,0s} (4 entries),
// out-of-view {5,10s}...{7,0s} (3 entries) - only the documented endpoints
// are real; the middle entries below are this test's own filler, not
// claimed as the real file's exact values.
// ===========================================================================
std::vector<NearFieldDespawnBucket> SampleInViewBuckets() {
    return {{5, 30000}, {6, 20000}, {7, 10000}, {8, 0}};
}
std::vector<NearFieldDespawnBucket> SampleOutOfViewBuckets() {
    return {{5, 10000}, {6, 5000}, {7, 0}};
}

void testDespawnScheduleGateCancelsBelowThreshold() {
    NearFieldDespawnScheduleParams params;
    params.inViewBuckets = SampleInViewBuckets();
    params.outOfViewBuckets = SampleOutOfViewBuckets();
    // Smaller of the two entry-0 thresholds is 5 (both lists happen to
    // start at 5 in this sample) - below that, no despawn is pending.
    DespawnScheduleGateResult r = EvaluateDespawnScheduleGate(params, /*liveCount=*/4);
    CHECK(!r.pending);
}

void testDespawnScheduleGatePrefersOutOfViewList() {
    NearFieldDespawnScheduleParams params;
    params.inViewBuckets = SampleInViewBuckets();
    params.outOfViewBuckets = SampleOutOfViewBuckets();
    DespawnScheduleGateResult r = EvaluateDespawnScheduleGate(params, /*liveCount=*/6);
    CHECK(r.pending);
    CHECK(r.fromOutOfView);
    CHECK(r.waitMs == 5000);  // out-of-view bucket {6,5000}
}

void testDespawnScheduleGateFallsBackToInViewWhenOutOfViewEmpty() {
    NearFieldDespawnScheduleParams params;
    params.inViewBuckets = SampleInViewBuckets();
    // outOfViewBuckets left empty.
    DespawnScheduleGateResult r = EvaluateDespawnScheduleGate(params, /*liveCount=*/7);
    CHECK(r.pending);
    CHECK(!r.fromOutOfView);
    CHECK(r.waitMs == 10000);  // in-view bucket {7,10000}
}

// ===========================================================================
// Part E: dual-timer cadence (Sec17.4c) - independent 200-300ms maintenance
// / 400-600ms despawn-schedule timers.
// ===========================================================================
void testDriverFirstTickRunsBothPassesImmediately() {
    AmbientToggleState toggles;  // all categories default-enabled, including "os_vehicle"
    NearFieldVehicleDriver driver;
    CHECK(driver.ShouldRunMaintenancePass(0, toggles, /*zoneDataLoaded=*/true));
    CHECK(driver.ShouldRunDespawnSchedule(0));
}

void testDriverMaintenancePassGatedOnToggleCategory() {
    AmbientToggleState toggles;
    for (bool& b : toggles.enabled[static_cast<size_t>(ToggleCategory::kVehicle)]) b = false;  // "os_vehicle" OFF
    NearFieldVehicleDriver driver;
    CHECK(!driver.ShouldRunMaintenancePass(0, toggles, /*zoneDataLoaded=*/true));
}

void testDriverMaintenancePassGatedOnZoneDataLoaded() {
    AmbientToggleState toggles;
    NearFieldVehicleDriver driver;
    CHECK(!driver.ShouldRunMaintenancePass(0, toggles, /*zoneDataLoaded=*/false));
}

void testDriverTimersRearmToTheirOwnRangeAndStayIndependent() {
    AmbientToggleState toggles;
    // Fixed, non-midpoint randomRangeMs seam so each re-arm is pinned to a
    // known value: always returns `hi` (the slower end of each range).
    NearFieldVehicleDriver driver([](int64_t /*lo*/, int64_t hi) { return hi; });

    CHECK(driver.ShouldRunMaintenancePass(0, toggles, true));  // re-arms maintenance to +300ms (now=0)
    CHECK(driver.ShouldRunDespawnSchedule(0));                 // re-arms despawn-schedule to +600ms (now=0)

    // At t=299ms: maintenance timer not yet expired (300ms armed), despawn-
    // schedule timer also not expired (600ms armed) - neither should fire.
    CHECK(!driver.ShouldRunMaintenancePass(299, toggles, true));
    CHECK(!driver.ShouldRunDespawnSchedule(299));

    // At t=300ms: maintenance expires and re-arms; despawn-schedule is
    // still armed until 600ms - proves the two timers are independent, not
    // coupled to a shared latch the way the distant tier's ped/vehicle
    // heavy passes are (ambient_population.h's AmbientPopulationDriver).
    CHECK(driver.ShouldRunMaintenancePass(300, toggles, true));
    CHECK(!driver.ShouldRunDespawnSchedule(300));

    // At t=600ms: despawn-schedule finally expires too.
    CHECK(driver.ShouldRunDespawnSchedule(600));
}

// ===========================================================================
// Part F: the combined maintenance pass, and the REQUIRED independence
// proof between this tier's 48-cap and the distant tier's 75-cap.
// ===========================================================================
void testMaintenancePassDespawnsOutOfRangeAndRespectsCapOnSpawn() {
    std::vector<NearFieldVehicle> live;
    NearFieldVehicle farUnimportant;
    farUnimportant.id = 1;
    farUnimportant.pos = Vec3{100000, 0, 0};
    farUnimportant.inPlayerWorldCell = true;
    live.push_back(farUnimportant);

    NearFieldVehicle nearImportant;
    nearImportant.id = 2;
    nearImportant.pos = Vec3{0, 0, 0};  // right at the player - would survive on distance alone anyway
    nearImportant.inPlayerWorldCell = true;
    nearImportant.seatHasImportantOccupant[0] = true;
    live.push_back(nearImportant);

    std::vector<Vec3> candidates = {Vec3{10, 0, 0}};
    DeterministicRng rng(1);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 3;

    NearFieldMaintenanceResult r =
        RunNearFieldVehicleMaintenancePass(live, /*runDespawn=*/true, /*runSpawn=*/true, Vec3{0, 0, 0},
                                            /*distSq=*/100.0 * 100.0, /*eligibleHumanCount=*/0, candidates, pick,
                                            nextId);

    CHECK(r.despawned == 1);  // only the far, unimportant-occupant vehicle
    CHECK(r.spawned == 1);
    // The important-occupant vehicle must still be present.
    bool importantSurvived = false;
    for (const auto& v : live) {
        if (v.id == 2) importantSurvived = true;
        CHECK(v.id != 1);  // the far vehicle must be gone
    }
    CHECK(importantSurvived);
}

// Regression lock for the bug near_field_vehicle_demo.cpp's own run caught
// (see near_field_vehicle_population.h's file header, CHOSEN item 4): a
// freshly-spawned vehicle must be marked inPlayerWorldCell=true so it is
// actually distance-gated on a later despawn-schedule tick, not treated as
// despawn-eligible outright by the "gate doesn't apply outside the cell"
// rule. Without the fix, this test's own second maintenance call would
// despawn the vehicle this test just spawned one tick earlier even though
// it is well within the despawn distance.
void testSpawnedVehicleIsInPlayerWorldCellAndSurvivesADespawnTick() {
    std::vector<NearFieldVehicle> live;
    std::vector<Vec3> candidates = {Vec3{5, 0, 0}};  // well within any reasonable despawn distance of the player
    DeterministicRng rng(4);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 1;
    const double distSq = 100.0 * 100.0;  // 100-unit despawn distance, squared

    NearFieldMaintenanceResult spawnResult =
        RunNearFieldVehicleMaintenancePass(live, /*runDespawn=*/false, /*runSpawn=*/true, Vec3{0, 0, 0}, distSq,
                                            /*eligibleHumanCount=*/0, candidates, pick, nextId);
    CHECK(spawnResult.spawned == 1);
    CHECK(live.size() == 1);
    CHECK(live[0].inPlayerWorldCell);  // the fix this test locks in

    // A later tick runs ONLY the despawn-schedule sub-step (no new
    // candidates even supplied) - the vehicle just spawned, still close to
    // the player, must survive it.
    NearFieldMaintenanceResult despawnResult =
        RunNearFieldVehicleMaintenancePass(live, /*runDespawn=*/true, /*runSpawn=*/false, Vec3{0, 0, 0}, distSq,
                                            /*eligibleHumanCount=*/0, /*spawnCandidateSlots=*/{}, pick, nextId);
    CHECK(despawnResult.despawned == 0);
    CHECK(live.size() == 1);
}

void testMaintenancePassBlocksSpawnWhenHumanHeadroomExhausted() {
    std::vector<NearFieldVehicle> live;
    std::vector<Vec3> candidates = {Vec3{10, 0, 0}};
    DeterministicRng rng(2);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 1;

    // eligibleHumanCount == kHumanEligibilityCeiling -> headroom is exactly
    // 0, not positive -> the spawn pass must not proceed even though the
    // 48-cap has plenty of room and a valid candidate exists.
    NearFieldMaintenanceResult r = RunNearFieldVehicleMaintenancePass(
        live, /*runDespawn=*/false, /*runSpawn=*/true, Vec3{0, 0, 0}, /*distSq=*/1.0,
        /*eligibleHumanCount=*/kHumanEligibilityCeiling, candidates, pick, nextId);

    CHECK(r.spawned == 0);
    CHECK(live.empty());
}

void testMaintenancePassBlocksSpawnWhenAtVehicleCap() {
    std::vector<NearFieldVehicle> live;
    for (int i = 0; i < kNearFieldVehicleCap; ++i) {
        NearFieldVehicle v;
        v.id = static_cast<uint64_t>(i);
        v.pos = Vec3{0, 0, 0};
        v.inPlayerWorldCell = true;
        live.push_back(v);
    }
    CHECK(static_cast<int>(live.size()) == kNearFieldVehicleCap);

    std::vector<Vec3> candidates = {Vec3{10, 0, 0}};
    DeterministicRng rng(3);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 1000;

    // runDespawn=false so the cap-at-48 list is not thinned first - proves
    // the spawn gate itself checks capacity, not just "there happened to be
    // room after despawn."
    NearFieldMaintenanceResult r =
        RunNearFieldVehicleMaintenancePass(live, /*runDespawn=*/false, /*runSpawn=*/true, Vec3{0, 0, 0},
                                            /*distSq=*/1.0, /*eligibleHumanCount=*/0, candidates, pick, nextId);
    CHECK(r.spawned == 0);
    CHECK(static_cast<int>(live.size()) == kNearFieldVehicleCap);  // unchanged, never exceeds the hard cap
}

// THE REQUIRED PROOF: the near-field tier's own 48-cap and the distant
// tier's own 75-cap (ambient_population.h) are genuinely independent pools
// - both can sit at (or near) their own caps SIMULTANEOUSLY with zero
// cross-interference, because they are two separate std::vector<T> lists of
// two separate types (NearFieldVehicle vs. LiveVehicle), not one list with a
// tier flag. This is the direct test-level expression of Sec17.4c's own
// "honest negative" finding (no object hand-off/continuity between tiers).
void testNearFieldAndDistantPoolCapsAreIndependent() {
    // Fill the near-field pool to its own 48-cap.
    std::vector<NearFieldVehicle> nearFieldLive;
    for (int i = 0; i < kNearFieldVehicleCap; ++i) {
        NearFieldVehicle v;
        v.id = static_cast<uint64_t>(i);
        v.pos = Vec3{static_cast<double>(i), 0, 0};
        nearFieldLive.push_back(v);
    }

    // Independently, fill the DISTANT tier's own pool toward its own
    // (different, larger) 75-cap, using ambient_population.h's own
    // RunVehicleHeavyPass/types - a completely separate call, separate
    // list, separate type, never touching nearFieldLive at all.
    std::vector<TrafficSegment> segments;
    for (int i = 0; i < 20; ++i) {
        double x = static_cast<double>(i) * 20.0;
        segments.push_back({static_cast<uint64_t>(i), Vec3{x, 0, 0}, Vec3{x + 10, 0, 0}, 0x1u});
    }
    std::vector<VehicleTrafficType> types = {{0xAAu, 0x1u, 0.04, 0.0, 4.5}};
    std::vector<LiveVehicle> distantLive;
    SpawnDespawnParams distantParams;
    DeterministicRng distantRng(55);
    auto distantPick = MakePickUniform(distantRng);
    uint64_t distantNextId = 1;
    for (int pass = 0; pass < 8; ++pass) {  // kVehicleRefillCapPerPass=20 per pass; several passes to approach 75
        RunVehicleHeavyPass(distantLive, segments, types, Vec3{0, 0, 0}, Vec3{1, 0, 0}, distantParams,
                             /*innerDist=*/0.0, /*spawnDist=*/400.0, /*despawnRadius=*/1e9, /*minSlotSeparation=*/0.5,
                             /*requestedSlotCount=*/5, distantPick, distantNextId);
    }

    // Both pools are at/near their OWN caps, simultaneously, with no
    // shared state between them.
    CHECK(static_cast<int>(nearFieldLive.size()) == kNearFieldVehicleCap);  // exactly 48
    CHECK(kNearFieldVehicleCap == 48);
    CHECK(kVehiclePopulationTarget == 75);
    CHECK(static_cast<int>(distantLive.size()) <= kVehiclePopulationTarget);
    CHECK(static_cast<int>(distantLive.size()) > kNearFieldVehicleCap);  // distant tier's own cap is strictly larger

    // Mutating the near-field pool (another despawn+spawn pass) must not
    // change the distant pool's size at all, and vice versa - genuinely
    // separate pools, not a shared list filtered by a tier flag.
    const size_t distantSizeBefore = distantLive.size();
    std::vector<Vec3> nfCandidates = {Vec3{500, 0, 0}};
    DeterministicRng nfRng(9);
    auto nfPick = MakePickUniform(nfRng);
    uint64_t nfNextId = 1000;
    RunNearFieldVehicleMaintenancePass(nearFieldLive, /*runDespawn=*/false, /*runSpawn=*/true, Vec3{0, 0, 0},
                                        /*distSq=*/1.0, /*eligibleHumanCount=*/0, nfCandidates, nfPick, nfNextId);
    CHECK(distantLive.size() == distantSizeBefore);  // untouched by the near-field pass
    CHECK(static_cast<int>(nearFieldLive.size()) == kNearFieldVehicleCap);  // still capped at 48, spawn attempt blocked
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
    run("testNearFieldVehicleCapIs48", testNearFieldVehicleCapIs48);
    run("testHumanEligibilityCeilingIs150", testHumanEligibilityCeilingIs150);
    run("testHasNearFieldVehicleCapacity", testHasNearFieldVehicleCapacity);
    run("testHumanHeadroomGate", testHumanHeadroomGate);

    run("testDespawnEligibleOutOfRangeUnimportantInPlayerCell", testDespawnEligibleOutOfRangeUnimportantInPlayerCell);
    run("testDespawnNotEligibleWhenInRange", testDespawnNotEligibleWhenInRange);
    run("testDespawnNeverEligibleWithImportantOccupantRegardlessOfDistance",
        testDespawnNeverEligibleWithImportantOccupantRegardlessOfDistance);
    run("testDespawnNeverEligibleWhenForceNoSpawnBlocked", testDespawnNeverEligibleWhenForceNoSpawnBlocked);
    run("testDespawnEligibleOutsidePlayerCellDistanceGateDoesNotApply",
        testDespawnEligibleOutsidePlayerCellDistanceGateDoesNotApply);
    run("testAnySeatHasImportantOccupant", testAnySeatHasImportantOccupant);

    run("testSelectNearFieldVehicleSpawnNoCandidatesNotFound", testSelectNearFieldVehicleSpawnNoCandidatesNotFound);
    run("testSelectNearFieldVehicleSpawnPicksAmongUpToThreeSlots",
        testSelectNearFieldVehicleSpawnPicksAmongUpToThreeSlots);

    run("testDespawnScheduleGateCancelsBelowThreshold", testDespawnScheduleGateCancelsBelowThreshold);
    run("testDespawnScheduleGatePrefersOutOfViewList", testDespawnScheduleGatePrefersOutOfViewList);
    run("testDespawnScheduleGateFallsBackToInViewWhenOutOfViewEmpty",
        testDespawnScheduleGateFallsBackToInViewWhenOutOfViewEmpty);

    run("testDriverFirstTickRunsBothPassesImmediately", testDriverFirstTickRunsBothPassesImmediately);
    run("testDriverMaintenancePassGatedOnToggleCategory", testDriverMaintenancePassGatedOnToggleCategory);
    run("testDriverMaintenancePassGatedOnZoneDataLoaded", testDriverMaintenancePassGatedOnZoneDataLoaded);
    run("testDriverTimersRearmToTheirOwnRangeAndStayIndependent",
        testDriverTimersRearmToTheirOwnRangeAndStayIndependent);

    run("testMaintenancePassDespawnsOutOfRangeAndRespectsCapOnSpawn",
        testMaintenancePassDespawnsOutOfRangeAndRespectsCapOnSpawn);
    run("testSpawnedVehicleIsInPlayerWorldCellAndSurvivesADespawnTick",
        testSpawnedVehicleIsInPlayerWorldCellAndSurvivesADespawnTick);
    run("testMaintenancePassBlocksSpawnWhenHumanHeadroomExhausted",
        testMaintenancePassBlocksSpawnWhenHumanHeadroomExhausted);
    run("testMaintenancePassBlocksSpawnWhenAtVehicleCap", testMaintenancePassBlocksSpawnWhenAtVehicleCap);
    run("testNearFieldAndDistantPoolCapsAreIndependent", testNearFieldAndDistantPoolCapsAreIndependent);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic near-field vehicle population tests passed.\n";
    return 0;
}
