// Synthetic tests for the police/roadblock spawn MECHANISM, spec-ai-runtime.md
// Sec19.7 (include/sr3airuntime/police_roadblock_spawn.h).
//
// Covers: the per-category per-tick spawn-rate throttle (CONFIRMED exact
// numbers 1/1/2/4, reset every tick, NOT a total-live-count cap); the
// sliding anchor (CONFIRMED subtraction formula, re-centers only when no
// unfilled slot exists); the position formula (CONFIRMED anchor + direction
// * forward_offset) and its sign-correction helper; the 5-way type dispatch
// (case 0/1 skipped, case 2 ped, case 3 vehicle incl. the literal
// "Spike_strip" case-insensitive match, case 4 third category); and the
// manager/driver's full per-tick orchestration, including a many-tick
// scenario proving the anchor slides (not fixed) once the slot table fills.

#include <iostream>
#include <vector>

#include "sr3airuntime/police_roadblock_spawn.h"

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
// Part A: throttle (Sec19.7 CONFIRMED exact numbers).
// ===========================================================================
void testThrottleExactCapsPerCategory() {
    CHECK(RoadblockThrottleState::kCategory01PerTickMax == 1);
    CHECK(RoadblockThrottleState::kCategory2PerTickMax == 2);
    CHECK(RoadblockThrottleState::kCategory3PerTickMax == 4);
}

void testThrottleCategory01IsSharedBetweenCoopSlots() {
    RoadblockThrottleState t;
    CHECK(t.TryConsume(RoadblockCategory::kCoopPlayerSlotA));   // 1st of the shared cap
    CHECK(!t.TryConsume(RoadblockCategory::kCoopPlayerSlotB));  // same shared counter, already at cap
}

void testThrottleCategory2AllowsExactlyTwoPerTick() {
    RoadblockThrottleState t;
    CHECK(t.TryConsume(RoadblockCategory::kPed));
    CHECK(t.TryConsume(RoadblockCategory::kPed));
    CHECK(!t.TryConsume(RoadblockCategory::kPed));  // 3rd this tick - throttled
}

void testThrottleCategory3AllowsExactlyFourPerTick() {
    RoadblockThrottleState t;
    for (int i = 0; i < 4; ++i) CHECK(t.TryConsume(RoadblockCategory::kVehicle));
    CHECK(!t.TryConsume(RoadblockCategory::kVehicle));  // 5th this tick - throttled
}

void testThrottleCategory4IsNeverThrottled() {
    RoadblockThrottleState t;
    for (int i = 0; i < 1000; ++i) CHECK(t.TryConsume(RoadblockCategory::kThirdCategory));
}

void testThrottleResetForTickClearsAllThreeCounters() {
    RoadblockThrottleState t;
    t.TryConsume(RoadblockCategory::kCoopPlayerSlotA);
    for (int i = 0; i < 2; ++i) t.TryConsume(RoadblockCategory::kPed);
    for (int i = 0; i < 4; ++i) t.TryConsume(RoadblockCategory::kVehicle);
    CHECK(!t.TryConsume(RoadblockCategory::kPed));  // saturated before reset

    t.ResetForTick();
    CHECK(t.TryConsume(RoadblockCategory::kCoopPlayerSlotB));  // shared counter free again
    CHECK(t.TryConsume(RoadblockCategory::kPed));
    CHECK(t.TryConsume(RoadblockCategory::kVehicle));
}

// ===========================================================================
// Part B: the sliding anchor (Sec19.7 CONFIRMED subtraction formula).
// ===========================================================================
void testAnchorRecenterSubtractsReferenceDelta() {
    RoadblockAnchorTracker anchor;
    anchor.InitializeIfNeeded(Vec3{0, 0, 0});
    CHECK(anchor.anchorPos.x == 0 && anchor.anchorPos.z == 0);

    anchor.Recenter(Vec3{10, 0, 5});  // reference moved +10 on X, +5 on Z since init
    // CONFIRMED formula: anchor -= (referencePos_now - referencePos_last).
    CHECK(anchor.anchorPos.x == -10.0);
    CHECK(anchor.anchorPos.z == -5.0);
}

void testAnchorRecenterIsCumulativeAcrossTicks() {
    RoadblockAnchorTracker anchor;
    anchor.InitializeIfNeeded(Vec3{0, 0, 0});
    anchor.Recenter(Vec3{5, 0, 0});
    anchor.Recenter(Vec3{12, 0, 0});  // second delta: +7
    CHECK(anchor.anchorPos.x == -12.0);
}

void testAnchorNeverRecenteredStaysAtInitialValue() {
    RoadblockAnchorTracker anchor;
    anchor.InitializeIfNeeded(Vec3{3, 0, 4});
    CHECK(anchor.anchorPos.x == 3.0);
    CHECK(anchor.anchorPos.z == 4.0);
    // No Recenter() call at all - the "fixed" case, contrasted against the
    // scenario tests below where repeated ticks DO move it.
}

// ===========================================================================
// Part C: position formula + direction helpers (Sec19.7 CONFIRMED).
// ===========================================================================
void testResolveSpawnPositionAddsScaledDirection() {
    Vec3 anchor{100, 0, 200};
    Vec3 dir{0, 0, 1};
    Vec3 spawn = ResolveSpawnPosition(anchor, dir, /*forwardOffsetMetres=*/50.0);
    CHECK(spawn.x == 100.0);
    CHECK(spawn.z == 250.0);
}

void testSignCorrectDirectionFlipsOpposingCandidate() {
    Vec3 candidate{0, 0, -1};    // points backward relative to...
    Vec3 referenceForward{0, 0, 1};  // ...the reference heading
    Vec3 corrected = SignCorrectDirection(candidate, referenceForward);
    CHECK(corrected.z == 1.0);  // flipped to match the reference's forward sense
}

void testSignCorrectDirectionKeepsAlignedCandidate() {
    Vec3 candidate{0, 0, 1};
    Vec3 referenceForward{0, 0, 1};
    Vec3 corrected = SignCorrectDirection(candidate, referenceForward);
    CHECK(corrected.z == 1.0);
}

void testResolveDirectionAlongHeadingNormalizes() {
    Vec3 heading{0, 0, 5};  // not unit length
    Vec3 dir = ResolveDirectionAlongHeading(heading);
    CHECK(std::abs(Length(dir) - 1.0) < 1e-9);
    CHECK(dir.z == 1.0);
}

// ===========================================================================
// Part D: the "Spike_strip" literal comparison (Sec19.7 case 3 CONFIRMED).
// ===========================================================================
void testIsSpikeStripDefinitionNameCaseInsensitive() {
    CHECK(IsSpikeStripDefinitionName("Spike_strip"));
    CHECK(IsSpikeStripDefinitionName("SPIKE_STRIP"));
    CHECK(IsSpikeStripDefinitionName("spike_strip"));
    CHECK(!IsSpikeStripDefinitionName("spike strip"));  // underscore matters - exact literal, not a loose match
    CHECK(!IsSpikeStripDefinitionName("police_roadblock_car"));
}

// ===========================================================================
// Part E: the 5-way type dispatch (Sec19.7 CONFIRMED structure).
// ===========================================================================
void testDispatchCoopSlotsAreSkippedNotCreated() {
    RoadblockSlot slotA;
    slotA.category = RoadblockCategory::kCoopPlayerSlotA;
    auto resultA = DispatchRoadblockSlot(slotA, Vec3{0, 0, 0});
    CHECK(resultA.outcome == RoadblockDispatchOutcome::kSkippedCoopHandoff);
    CHECK(!resultA.ped.has_value());
    CHECK(!resultA.vehicle.has_value());
    CHECK(!resultA.thirdCategory.has_value());

    RoadblockSlot slotB;
    slotB.category = RoadblockCategory::kCoopPlayerSlotB;
    auto resultB = DispatchRoadblockSlot(slotB, Vec3{0, 0, 0});
    CHECK(resultB.outcome == RoadblockDispatchOutcome::kSkippedCoopHandoff);
}

void testDispatchPedCreatesAtResolvedPosition() {
    RoadblockSlot slot;
    slot.category = RoadblockCategory::kPed;
    Vec3 pos{12, 0, 34};
    auto result = DispatchRoadblockSlot(slot, pos);
    CHECK(result.outcome == RoadblockDispatchOutcome::kPedCreated);
    CHECK(result.ped.has_value());
    CHECK(result.ped->position.x == 12.0 && result.ped->position.z == 34.0);
}

void testDispatchVehicleWithSpikeStripNameIsTaggedSpikeStrip() {
    RoadblockSlot slot;
    slot.category = RoadblockCategory::kVehicle;
    slot.vehicleDefinitionName = "Spike_strip";
    auto result = DispatchRoadblockSlot(slot, Vec3{0, 0, 0});
    CHECK(result.outcome == RoadblockDispatchOutcome::kVehicleCreated);
    CHECK(result.vehicle.has_value());
    CHECK(result.vehicle->kind == RoadblockVehicleKind::kSpikeStrip);
    CHECK(result.vehicle->vehicleDefinitionName == "Spike_strip");
}

void testDispatchVehicleWithoutSpikeStripNameIsGeneric() {
    RoadblockSlot slot;
    slot.category = RoadblockCategory::kVehicle;
    slot.vehicleDefinitionName = "some_other_car";
    auto result = DispatchRoadblockSlot(slot, Vec3{0, 0, 0});
    CHECK(result.vehicle->kind == RoadblockVehicleKind::kGeneric);
    CHECK(result.vehicle->vehicleDefinitionName == "some_other_car");
}

void testDispatchVehicleWithEmptyNameFallsBackToChosenGeneric() {
    RoadblockSlot slot;
    slot.category = RoadblockCategory::kVehicle;
    auto result = DispatchRoadblockSlot(slot, Vec3{0, 0, 0});
    CHECK(result.vehicle->kind == RoadblockVehicleKind::kGeneric);
    CHECK(result.vehicle->vehicleDefinitionName == std::string(kChosenGenericVehicleName));
}

void testDispatchSpikeStripAndGenericVehicleAreDistinguishable() {
    // Directly proves this task's own requirement: a spike-strip vehicle
    // category is distinguishable from a generic roadblock vehicle in this
    // project's own vehicle-spawn representation (VehicleCreationRequest).
    RoadblockSlot spikeSlot;
    spikeSlot.category = RoadblockCategory::kVehicle;
    spikeSlot.vehicleDefinitionName = kChosenSpikeStripName;
    RoadblockSlot genericSlot;
    genericSlot.category = RoadblockCategory::kVehicle;
    genericSlot.vehicleDefinitionName = kChosenGenericVehicleName;

    auto spikeResult = DispatchRoadblockSlot(spikeSlot, Vec3{0, 0, 0});
    auto genericResult = DispatchRoadblockSlot(genericSlot, Vec3{0, 0, 0});

    CHECK(spikeResult.vehicle->kind != genericResult.vehicle->kind);
    CHECK(spikeResult.vehicle->kind == RoadblockVehicleKind::kSpikeStrip);
    CHECK(genericResult.vehicle->kind == RoadblockVehicleKind::kGeneric);
}

void testDispatchThirdCategoryCreatesAGuessedPropNotAVehicle() {
    RoadblockSlot slot;
    slot.category = RoadblockCategory::kThirdCategory;
    auto result = DispatchRoadblockSlot(slot, Vec3{0, 0, 0});
    CHECK(result.outcome == RoadblockDispatchOutcome::kThirdCategoryCreated);
    CHECK(result.thirdCategory.has_value());
    CHECK(!result.vehicle.has_value());
    CHECK(result.thirdCategory->propDefinitionName == std::string(kChosenThirdCategoryPropName));
}

// ===========================================================================
// Part F: RoadblockSpawnManager / RoadblockPopulationDriver orchestration.
// ===========================================================================
auto straightAheadDirection = [](const Vec3&) -> Vec3 { return Vec3{0, 0, 1}; };

void testManagerThrottleLimitsSpawnsPerTickToConfirmedNumbers() {
    // 3 ped (cat2, cap 2), 6 vehicle (cat3, cap 4), 1 third-category (cat4,
    // uncapped) - all starting unfilled, so a single tick's walk considers
    // every one of them. (Coop cat0/1 slots are exercised separately below,
    // in testManagerCoopSlotsStayPerpetuallyUnfilledButRespectSharedCap -
    // they interact with "filled" bookkeeping differently, see that test's
    // own comment, and mixing the two in one assertion block would obscure
    // both.)
    std::vector<RoadblockSlot> slots;
    RoadblockSlot s;
    s.category = RoadblockCategory::kPed;
    for (int i = 0; i < 3; ++i) slots.push_back(s);
    s.category = RoadblockCategory::kVehicle;
    s.vehicleDefinitionName = "generic";
    for (int i = 0; i < 6; ++i) slots.push_back(s);
    s.category = RoadblockCategory::kThirdCategory;
    slots.push_back(s);

    RoadblockSpawnManager manager(slots);
    manager.BeginTick();
    RoadblockTickResult tick = manager.RunSpawnAttemptWalk(Vec3{0, 0, 0}, straightAheadDirection);

    CHECK(tick.attemptedSlotCount == 10);
    // Exactly how many of the 10 attempted slots actually dispatched this
    // tick: 2 (ped, cap 2) + 4 (vehicle, cap 4) + 1 (third, uncapped) = 7.
    CHECK(static_cast<int>(tick.dispatches.size()) == 7);
    CHECK(tick.throttledSlotCount == 10 - 7);

    int pedCreated = 0, vehicleCreated = 0, thirdCreated = 0;
    for (const auto& d : tick.dispatches) {
        if (d.outcome == RoadblockDispatchOutcome::kPedCreated) ++pedCreated;
        if (d.outcome == RoadblockDispatchOutcome::kVehicleCreated) ++vehicleCreated;
        if (d.outcome == RoadblockDispatchOutcome::kThirdCategoryCreated) ++thirdCreated;
    }
    CHECK(pedCreated == 2);
    CHECK(vehicleCreated == 4);
    CHECK(thirdCreated == 1);

    // Re-running BeginTick() resets the throttle - the remaining throttled
    // slots (1 ped + 2 vehicle = 3) now get through.
    manager.BeginTick();
    RoadblockTickResult tick2 = manager.RunSpawnAttemptWalk(Vec3{0, 0, 1}, straightAheadDirection);
    CHECK(tick2.attemptedSlotCount == 3);
    CHECK(static_cast<int>(tick2.dispatches.size()) == 3);  // all 3 remaining fit well inside the fresh caps
    CHECK(tick2.throttledSlotCount == 0);
}

void testManagerCoopSlotsStayPerpetuallyUnfilledButRespectSharedCap() {
    // Documents a real, deliberate consequence of skipping case 0/1 (see
    // police_roadblock_spawn.h's file header): DispatchRoadblockSlot() never
    // reports kSkippedCoopHandoff as "created", so RunSpawnAttemptWalk()
    // never marks a coop slot filled - without a live-coop-player system,
    // there is nothing to occupy it, so it is legitimately "unfilled"
    // forever. It still correctly participates in, and is capped by, the
    // shared cat0/1 throttle counter every single tick.
    std::vector<RoadblockSlot> slots(2);
    slots[0].category = RoadblockCategory::kCoopPlayerSlotA;
    slots[1].category = RoadblockCategory::kCoopPlayerSlotB;
    RoadblockSpawnManager manager(slots);

    for (int tickIndex = 0; tickIndex < 5; ++tickIndex) {
        manager.BeginTick();
        RoadblockTickResult tick = manager.RunSpawnAttemptWalk(Vec3{0, 0, 0}, straightAheadDirection);
        CHECK(tick.attemptedSlotCount == 2);     // both still unfilled, every tick
        CHECK(tick.throttledSlotCount == 1);      // shared cap lets only one through
        CHECK(static_cast<int>(tick.dispatches.size()) == 1);
        CHECK(tick.dispatches[0].outcome == RoadblockDispatchOutcome::kSkippedCoopHandoff);
        CHECK(!manager.slots()[0].filled);
        CHECK(!manager.slots()[1].filled);
    }
}

void testManagerAnchorDoesNotRecenterWhileSlotsRemainUnfilled() {
    std::vector<RoadblockSlot> slots(1);
    slots[0].category = RoadblockCategory::kPed;
    RoadblockSpawnManager manager(slots);

    manager.BeginTick();
    RoadblockTickResult tick = manager.RunSpawnAttemptWalk(Vec3{0, 0, 0}, straightAheadDirection);
    CHECK(!tick.anchorRecentered);  // one unfilled slot existed and was just filled
}

void testManagerAnchorRecentersOnceTableIsFullAndSlidesWithReference() {
    // A tiny table (1 ped slot) fills on tick 1; every subsequent tick has
    // NO unfilled slot, so the anchor recenters every time thereafter -
    // this is the scenario that proves the anchor actually SLIDES as the
    // tracked reference position moves, not a fixed world point.
    std::vector<RoadblockSlot> slots(1);
    slots[0].category = RoadblockCategory::kPed;
    slots[0].despawnSecondsRemaining = 1'000'000.0;  // never expires mid-test
    RoadblockSpawnManager manager(slots);

    manager.BeginTick();
    manager.RunEligibilityAndDespawnPass(/*dtSeconds=*/0.0);
    RoadblockTickResult tick1 = manager.RunSpawnAttemptWalk(Vec3{0, 0, 0}, straightAheadDirection);
    CHECK(!tick1.anchorRecentered);
    Vec3 anchorAfterFill = manager.anchorPosition();

    manager.BeginTick();
    manager.RunEligibilityAndDespawnPass(0.0);
    RoadblockTickResult tick2 = manager.RunSpawnAttemptWalk(Vec3{0, 0, 20}, straightAheadDirection);  // reference moved +20 on Z
    CHECK(tick2.anchorRecentered);
    CHECK(manager.anchorPosition().z != anchorAfterFill.z);  // NOT fixed - moved in response to the reference's own movement
    CHECK(manager.anchorPosition().z == anchorAfterFill.z - 20.0);  // CONFIRMED subtraction formula, exact value

    manager.BeginTick();
    manager.RunEligibilityAndDespawnPass(0.0);
    RoadblockTickResult tick3 = manager.RunSpawnAttemptWalk(Vec3{0, 0, 50}, straightAheadDirection);  // reference moved +30 more
    CHECK(tick3.anchorRecentered);
    CHECK(manager.anchorPosition().z == anchorAfterFill.z - 50.0);  // cumulative across ticks
}

void testManagerDespawnTimerFreesSlotForReFill() {
    std::vector<RoadblockSlot> slots(1);
    slots[0].category = RoadblockCategory::kPed;
    RoadblockSpawnManager manager(slots);

    manager.BeginTick();
    manager.RunEligibilityAndDespawnPass(0.0);
    manager.RunSpawnAttemptWalk(Vec3{0, 0, 0}, straightAheadDirection);
    CHECK(manager.slots()[0].filled);

    // Advance past the CHOSEN occupant lifetime - the slot should free.
    manager.BeginTick();
    manager.RunEligibilityAndDespawnPass(kChosenOccupantLifetimeSeconds + 1.0);
    CHECK(!manager.slots()[0].filled);

    // And the walk can now re-fill it.
    RoadblockTickResult tick = manager.RunSpawnAttemptWalk(Vec3{0, 0, 0}, straightAheadDirection);
    CHECK(manager.slots()[0].filled);
    CHECK(static_cast<int>(tick.dispatches.size()) == 1);
}

void testDriverShouldRunTickRequiresBothCvarAndArmed() {
    RoadblockPopulationDriver driver;
    CHECK(driver.roadblockCvarEnabled);  // CONFIRMED default: forced ON at bootstrap
    CHECK(!driver.armed);                // CHOSEN default: not auto-armed (see header's notoriety-gate note)
    CHECK(!driver.ShouldRunTick());

    driver.armed = true;
    CHECK(driver.ShouldRunTick());

    driver.roadblockCvarEnabled = false;  // simulates a debug-console override
    CHECK(!driver.ShouldRunTick());
}

void testDriverRunTickDelegatesFullChain() {
    RoadblockPopulationDriver driver;
    driver.armed = true;

    std::vector<RoadblockSlot> slots(2);
    slots[0].category = RoadblockCategory::kPed;
    slots[1].category = RoadblockCategory::kVehicle;
    RoadblockSpawnManager manager(slots);

    RoadblockTickResult tick = driver.RunTick(manager, /*dtSeconds=*/0.016, Vec3{0, 0, 0}, straightAheadDirection);
    CHECK(tick.attemptedSlotCount == 2);
    CHECK(static_cast<int>(tick.dispatches.size()) == 2);
    CHECK(manager.slots()[0].filled);
    CHECK(manager.slots()[1].filled);
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
    run("testThrottleExactCapsPerCategory", testThrottleExactCapsPerCategory);
    run("testThrottleCategory01IsSharedBetweenCoopSlots", testThrottleCategory01IsSharedBetweenCoopSlots);
    run("testThrottleCategory2AllowsExactlyTwoPerTick", testThrottleCategory2AllowsExactlyTwoPerTick);
    run("testThrottleCategory3AllowsExactlyFourPerTick", testThrottleCategory3AllowsExactlyFourPerTick);
    run("testThrottleCategory4IsNeverThrottled", testThrottleCategory4IsNeverThrottled);
    run("testThrottleResetForTickClearsAllThreeCounters", testThrottleResetForTickClearsAllThreeCounters);

    run("testAnchorRecenterSubtractsReferenceDelta", testAnchorRecenterSubtractsReferenceDelta);
    run("testAnchorRecenterIsCumulativeAcrossTicks", testAnchorRecenterIsCumulativeAcrossTicks);
    run("testAnchorNeverRecenteredStaysAtInitialValue", testAnchorNeverRecenteredStaysAtInitialValue);

    run("testResolveSpawnPositionAddsScaledDirection", testResolveSpawnPositionAddsScaledDirection);
    run("testSignCorrectDirectionFlipsOpposingCandidate", testSignCorrectDirectionFlipsOpposingCandidate);
    run("testSignCorrectDirectionKeepsAlignedCandidate", testSignCorrectDirectionKeepsAlignedCandidate);
    run("testResolveDirectionAlongHeadingNormalizes", testResolveDirectionAlongHeadingNormalizes);

    run("testIsSpikeStripDefinitionNameCaseInsensitive", testIsSpikeStripDefinitionNameCaseInsensitive);

    run("testDispatchCoopSlotsAreSkippedNotCreated", testDispatchCoopSlotsAreSkippedNotCreated);
    run("testDispatchPedCreatesAtResolvedPosition", testDispatchPedCreatesAtResolvedPosition);
    run("testDispatchVehicleWithSpikeStripNameIsTaggedSpikeStrip", testDispatchVehicleWithSpikeStripNameIsTaggedSpikeStrip);
    run("testDispatchVehicleWithoutSpikeStripNameIsGeneric", testDispatchVehicleWithoutSpikeStripNameIsGeneric);
    run("testDispatchVehicleWithEmptyNameFallsBackToChosenGeneric", testDispatchVehicleWithEmptyNameFallsBackToChosenGeneric);
    run("testDispatchSpikeStripAndGenericVehicleAreDistinguishable", testDispatchSpikeStripAndGenericVehicleAreDistinguishable);
    run("testDispatchThirdCategoryCreatesAGuessedPropNotAVehicle", testDispatchThirdCategoryCreatesAGuessedPropNotAVehicle);

    run("testManagerThrottleLimitsSpawnsPerTickToConfirmedNumbers", testManagerThrottleLimitsSpawnsPerTickToConfirmedNumbers);
    run("testManagerCoopSlotsStayPerpetuallyUnfilledButRespectSharedCap", testManagerCoopSlotsStayPerpetuallyUnfilledButRespectSharedCap);
    run("testManagerAnchorDoesNotRecenterWhileSlotsRemainUnfilled", testManagerAnchorDoesNotRecenterWhileSlotsRemainUnfilled);
    run("testManagerAnchorRecentersOnceTableIsFullAndSlidesWithReference", testManagerAnchorRecentersOnceTableIsFullAndSlidesWithReference);
    run("testManagerDespawnTimerFreesSlotForReFill", testManagerDespawnTimerFreesSlotForReFill);

    run("testDriverShouldRunTickRequiresBothCvarAndArmed", testDriverShouldRunTickRequiresBothCvarAndArmed);
    run("testDriverRunTickDelegatesFullChain", testDriverRunTickDelegatesFullChain);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic police-roadblock-spawn tests passed.\n";
    return 0;
}
