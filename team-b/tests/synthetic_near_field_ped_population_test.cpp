// Synthetic tests for the near-field (fully-simulated) on-foot ambient-
// pedestrian spawn, Idle-mode sidewalk/wander sub-behaviour, and
// gunfire-to-flee/cower reaction system, spec-ai-runtime.md Sec20
// (include/sr3airuntime/near_field_ped_population.h).
//
// Covers: the sidewalk-graph, length-weighted spawn mechanism (Sec20.1);
// Idle's sidewalk-follow/wander sub-behaviours, including sidewalking as the
// TRUE default at creation with no force-flag call (Sec20.2/20.3); the
// action_node_npcs.xtbl-driven Idle cooldown-roll idiom (Sec20.3); the
// CIVILIAN-team-byte-gated gunfire flee/cower trigger (Sec20.4); and a
// regression guard confirming Panic (mode 24) is never requested anywhere
// in this module, matching Sec20.4's own honest negative finding.
//
// All RNG seams use sr3airuntime::DeterministicRng (navpoint_selector.h) -
// never a bit-exact replay of the real engine's own untraced
// 0x00dab5e0/0x00dab660 (near_field_ped_population.h's/near_field_vehicle_
// population.h's own file comments explain why).

#include <cmath>
#include <iostream>
#include <vector>

#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_ped_population.h"
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
// Part A: the spawn mechanism (Sec20.1) - gather, length-weighted pick,
// retry-of-3, and every documented gate.
// ===========================================================================
void testGatherSidewalkSegmentCandidatesFiltersByMidpointDistance() {
    std::vector<SidewalkSegment> segs = {
        {1, Vec3{0, 0, 0}, Vec3{10, 0, 0}},     // midpoint (5,0,0) - close
        {2, Vec3{1000, 0, 0}, Vec3{1010, 0, 0}},  // midpoint far away
    };
    auto out = GatherSidewalkSegmentCandidates(segs, Vec3{0, 0, 0}, 100.0);
    CHECK(out.size() == 1);
    CHECK(out[0]->id == 1);
}

void testSelectNearFieldPedSpawnNoCandidatesNotFound() {
    std::vector<SidewalkSegment> empty;
    NearFieldPedSpawnGates gates;
    DeterministicRng rng(1);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(empty, gates, Vec3{0, 0, 0}, pick);
    CHECK(!r.found);
}

// Degenerate (zero-length) segments carry weight 0 in the length-weighted
// pick (ambient_population.h's own PickWeightedIndex) - a real, positive-
// length segment must always win when it is the only non-degenerate
// candidate, deterministically (not just "usually"), proving the pick is
// genuinely LENGTH-WEIGHTED, not a flat uniform pick over the slot buffer
// the way the vehicle sibling's own SelectNearFieldVehicleSpawn works.
void testSelectNearFieldPedSpawnPrefersRealSegmentOverDegenerateOnes() {
    std::vector<SidewalkSegment> segs = {
        {1, Vec3{500, 0, 0}, Vec3{500, 0, 0}},  // degenerate, zero length -> weight 0
        {2, Vec3{500, 0, 0}, Vec3{500, 0, 0}},  // degenerate, zero length -> weight 0
        {3, Vec3{500, 0, 0}, Vec3{520, 0, 0}},  // real length 20 -> the only nonzero weight
    };
    NearFieldPedSpawnGates gates;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        DeterministicRng rng(seed);
        auto pick = MakePickUniform(rng);
        NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
        CHECK(r.found);
        CHECK(r.segmentId == 3);  // the only segment with positive weight, every seed
    }
}

void testSelectNearFieldPedSpawnRejectsWithinCameraNearDistance() {
    // The entire segment sits well inside the ~90-unit near-camera reject
    // radius - every point sampled along it fails the gate on every retry,
    // so all 3 attempts must be exhausted and the pick must fail.
    std::vector<SidewalkSegment> segs = {{1, Vec3{0, 0, 0}, Vec3{10, 0, 0}}};
    NearFieldPedSpawnGates gates;
    DeterministicRng rng(2);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
    CHECK(!r.found);
    CHECK(r.attemptsUsed == kNearFieldPedMaxSpawnRetries);
}

void testSelectNearFieldPedSpawnAcceptsBeyondNearDistance() {
    std::vector<SidewalkSegment> segs = {{1, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    DeterministicRng rng(3);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
    CHECK(r.found);
    CHECK(r.segmentId == 1);
    CHECK(DistanceSq(r.position, Vec3{0, 0, 0}) >=
          kNearFieldPedCameraNearRejectDistApprox * kNearFieldPedCameraNearRejectDistApprox);
}

void testSelectNearFieldPedSpawnRejectsBeyondFarDistanceWhenGateEnabled() {
    std::vector<SidewalkSegment> segs = {{1, Vec3{5000, 0, 0}, Vec3{5020, 0, 0}}};
    NearFieldPedSpawnGates gates;
    gates.cameraFarRejectDist = 1000.0;  // segment is far beyond this - every sample must fail
    DeterministicRng rng(4);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
    CHECK(!r.found);
}

void testSelectNearFieldPedSpawnRejectsWhenDifficultyGateFails() {
    std::vector<SidewalkSegment> segs = {{1, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    gates.difficultyGatePasses = [] { return false; };
    DeterministicRng rng(5);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
    CHECK(!r.found);
}

void testSelectNearFieldPedSpawnRejectsWhenVisibleToCamera() {
    std::vector<SidewalkSegment> segs = {{1, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    gates.isVisibleToCamera = [](const Vec3&) { return true; };
    DeterministicRng rng(6);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
    CHECK(!r.found);
}

void testSelectNearFieldPedSpawnRejectsWhenGroundInvalid() {
    std::vector<SidewalkSegment> segs = {{1, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    gates.isGroundValid = [](const Vec3&) { return false; };
    DeterministicRng rng(7);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
    CHECK(!r.found);
}

void testSelectNearFieldPedSpawnAppliesSidewaysOffset() {
    // A segment running along +X; a nonzero sideways offset must move the
    // picked point off the line itself (perpendicular displacement), not
    // just somewhere along it.
    std::vector<SidewalkSegment> segs = {{1, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    gates.sidewaysOffsetDistance = 5.0;
    DeterministicRng rng(8);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, Vec3{0, 0, 0}, pick);
    CHECK(r.found);
    CHECK(std::abs(r.position.z) > 1e-6);  // offset is along Z for an X-aligned segment (CHOSEN planar left-normal)
}

// ===========================================================================
// Part A2 (ped-spawn-distance-spec-tuning task, 2026-10-10): ApproxVisibleToCamera
// - the CHOSEN horizontal-FOV-cone stand-in for Sec20.1's own CONFIRMED,
// separate "rejects if visible to the current camera" gate. See this
// function's own header comment (near_field_ped_population.h) for the full
// CONFIRMED-vs-CHOSEN breakdown: the ~90-unit near-reject floor above is
// unconditional, this is the SEPARATE gate on top of it.
// ===========================================================================
void testApproxVisibleToCameraTrueDirectlyAhead() {
    // Camera at the origin facing +X; a point straight ahead must read as
    // visible (within the CHOSEN half-FOV cone).
    const Vec3 cam{0, 0, 0};
    const Vec3 fwd{1, 0, 0};
    CHECK(ApproxVisibleToCamera(Vec3{100, 0, 0}, cam, fwd));
}

void testApproxVisibleToCameraFalseDirectlyBehind() {
    const Vec3 cam{0, 0, 0};
    const Vec3 fwd{1, 0, 0};
    CHECK(!ApproxVisibleToCamera(Vec3{-100, 0, 0}, cam, fwd));
}

void testApproxVisibleToCameraFalseDirectlyToTheSide() {
    // Perpendicular to facing - well outside any plausible FOV half-angle
    // (CHOSEN 50 degrees default), must read as not visible.
    const Vec3 cam{0, 0, 0};
    const Vec3 fwd{1, 0, 0};
    CHECK(!ApproxVisibleToCamera(Vec3{0, 0, 100}, cam, fwd));
}

void testApproxVisibleToCameraRespectsExplicitHalfFov() {
    // A point 80 degrees off-axis: not visible under a narrow 50-degree
    // CHOSEN default, but visible under an explicit wide 85-degree half-FOV
    // passed by the caller - proves the half-angle argument is genuinely
    // consumed, not ignored.
    const Vec3 cam{0, 0, 0};
    const Vec3 fwd{1, 0, 0};
    const double angleRad = 80.0 * 3.14159265358979323846 / 180.0;
    const Vec3 point{100.0 * std::cos(angleRad), 0.0, 100.0 * std::sin(angleRad)};
    CHECK(!ApproxVisibleToCamera(point, cam, fwd));
    CHECK(ApproxVisibleToCamera(point, cam, fwd, 85.0));
}

// End-to-end through the real gate struct: a candidate well past the
// ~90-unit floor but BEHIND the camera must be accepted when
// ApproxVisibleToCamera supplies isVisibleToCamera - proving the "spawn
// closer when off-screen" behaviour this task implements actually threads
// through SelectNearFieldPedSpawn, not just the helper in isolation.
void testSelectNearFieldPedSpawnAcceptsCloseCandidateBehindCamera() {
    // Segment at distance ~100 units, directly BEHIND a camera facing +X.
    std::vector<SidewalkSegment> segs = {{1, Vec3{-105, 0, 0}, Vec3{-95, 0, 0}}};
    const Vec3 cam{0, 0, 0};
    const Vec3 fwd{1, 0, 0};
    NearFieldPedSpawnGates gates;
    gates.isVisibleToCamera = [&](const Vec3& p) { return ApproxVisibleToCamera(p, cam, fwd); };
    DeterministicRng rng(42);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, cam, pick);
    CHECK(r.found);
}

// The mirror case: the SAME distance, but directly AHEAD (in view) - must be
// rejected, matching the real engine's own "rejects if visible" behaviour.
void testSelectNearFieldPedSpawnRejectsCloseCandidateAheadOfCamera() {
    std::vector<SidewalkSegment> segs = {{1, Vec3{95, 0, 0}, Vec3{105, 0, 0}}};
    const Vec3 cam{0, 0, 0};
    const Vec3 fwd{1, 0, 0};
    NearFieldPedSpawnGates gates;
    gates.isVisibleToCamera = [&](const Vec3& p) { return ApproxVisibleToCamera(p, cam, fwd); };
    DeterministicRng rng(43);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, cam, pick);
    CHECK(!r.found);
}

// Even with a real visibility gate wired up, the ~90-unit floor remains
// unconditional (Sec20.1's own three-separate-conditions reading, see
// NearFieldPedSpawnGates::isVisibleToCamera's own comment) - a candidate
// BEHIND the camera but inside that floor must still be rejected, not
// accepted just because it's off-screen.
void testSelectNearFieldPedSpawnStillRejectsWithinFloorEvenWhenBehindCamera() {
    std::vector<SidewalkSegment> segs = {{1, Vec3{-10, 0, 0}, Vec3{-5, 0, 0}}};
    const Vec3 cam{0, 0, 0};
    const Vec3 fwd{1, 0, 0};
    NearFieldPedSpawnGates gates;
    gates.isVisibleToCamera = [&](const Vec3& p) { return ApproxVisibleToCamera(p, cam, fwd); };
    DeterministicRng rng(44);
    auto pick = MakePickUniform(rng);
    NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(segs, gates, cam, pick);
    CHECK(!r.found);
}

// ===========================================================================
// Part B: the combined spawn pass (Sec20.1) - local cap, shared human
// headroom ceiling (reused from near_field_vehicle_population.h), spawn-only
// (no despawn surface exists in this file - file header item 3).
// ===========================================================================
void testRunNearFieldPedSpawnPassRespectsLocalCap() {
    std::vector<NearFieldPed> live(5);  // already at the (tiny, test-chosen) local cap
    std::vector<SidewalkSegment> segs = {{1, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    DeterministicRng rng(9);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 100;
    NearFieldPedMaintenanceResult r =
        RunNearFieldPedSpawnPass(live, /*localCap=*/5, /*eligibleHumanCount=*/0, segs, gates, Vec3{0, 0, 0}, pick,
                                  nextId);
    CHECK(r.spawned == 0);
    CHECK(live.size() == 5);
}

void testRunNearFieldPedSpawnPassBlocksOnSharedHumanHeadroom() {
    std::vector<NearFieldPed> live;
    std::vector<SidewalkSegment> segs = {{1, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    DeterministicRng rng(10);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 1;
    // eligibleHumanCount == kHumanEligibilityCeiling (reused from
    // near_field_vehicle_population.h) -> headroom exactly 0, not positive.
    NearFieldPedMaintenanceResult r = RunNearFieldPedSpawnPass(live, /*localCap=*/100, kHumanEligibilityCeiling, segs,
                                                                gates, Vec3{0, 0, 0}, pick, nextId);
    CHECK(r.spawned == 0);
    CHECK(live.empty());
}

void testRunNearFieldPedSpawnPassSpawnsOnePedAlreadySnappedOntoSegment() {
    std::vector<NearFieldPed> live;
    std::vector<SidewalkSegment> segs = {{42, Vec3{500, 0, 0}, Vec3{520, 0, 0}}};
    NearFieldPedSpawnGates gates;
    DeterministicRng rng(11);
    auto pick = MakePickUniform(rng);
    uint64_t nextId = 1;
    NearFieldPedMaintenanceResult r =
        RunNearFieldPedSpawnPass(live, /*localCap=*/100, /*eligibleHumanCount=*/0, segs, gates, Vec3{0, 0, 0}, pick,
                                  nextId);
    CHECK(r.spawned == 1);
    CHECK(live.size() == 1);
    CHECK(live[0].onSegment);              // spawn pipeline already resolved a segment - no "waiting to snap" phase
    CHECK(live[0].currentSegmentId == 42);
    CHECK(live[0].idleSubState == PedIdleSubState::Sidewalk);  // steady-state 14, not 13
}

// ===========================================================================
// Part C: Idle mode - sidewalk/wander are sub-behaviours, sidewalking is the
// TRUE default at creation (Sec20.2/20.3; file header item 6).
// ===========================================================================
void testSidewalkIsTheTrueDefaultAtCreation() {
    NearFieldPed ped;  // bare construction - no spawn pipeline, no setter call of any kind
    CHECK(ped.idleSubState == PedIdleSubState::SidewalkStart);
    CHECK(TickGroupFor(ped.idleSubState) == PedIdleTickGroup::Sidewalk);
    CHECK(!ped.onSegment);  // starts un-snapped - must snap via TickSidewalk's own gate, not force-flagged into any state
}

void testTickGroupRoutingMatchesTheRealSubStateTable() {
    CHECK(TickGroupFor(PedIdleSubState::SidewalkStart) == PedIdleTickGroup::Sidewalk);
    CHECK(TickGroupFor(PedIdleSubState::Sidewalk) == PedIdleTickGroup::Sidewalk);
    CHECK(TickGroupFor(PedIdleSubState::SidewalkFormation) == PedIdleTickGroup::Sidewalk);
    CHECK(TickGroupFor(PedIdleSubState::WanderPause) == PedIdleTickGroup::Wander);
    CHECK(TickGroupFor(PedIdleSubState::WanderChooseDest) == PedIdleTickGroup::Wander);
    CHECK(TickGroupFor(PedIdleSubState::WanderMove) == PedIdleTickGroup::Wander);
    CHECK(TickGroupFor(PedIdleSubState::WanderToSidewalk) == PedIdleTickGroup::OneFrameTransition);
    CHECK(TickGroupFor(PedIdleSubState::Idle) == PedIdleTickGroup::Other);
    CHECK(TickGroupFor(PedIdleSubState::Sleeping) == PedIdleTickGroup::Other);
    CHECK(TickGroupFor(PedIdleSubState::Sitting) == PedIdleTickGroup::Other);
}

void testTickSidewalkSnapsOntoGraphThenAdvances() {
    NearFieldPed ped;  // onSegment=false, sub-state 13
    SidewalkTickParams params;
    params.snapSegmentId = 77;
    TickSidewalk(ped, params);
    CHECK(ped.onSegment);
    CHECK(ped.currentSegmentId == 77);
    CHECK(ped.idleSubState == PedIdleSubState::Sidewalk);  // 13 -> 14
    CHECK(ped.segmentProgress == 0.0);

    params.advancePerTick = 0.3;
    TickSidewalk(ped, params);
    CHECK(ped.segmentProgress > 0.29 && ped.segmentProgress < 0.31);
    TickSidewalk(ped, params);
    TickSidewalk(ped, params);
    TickSidewalk(ped, params);  // 4 ticks of 0.3 would overshoot 1.0
    CHECK(ped.segmentProgress <= 1.0);
}

void testTickSidewalkWithoutSnapIdDoesNotAdvance() {
    NearFieldPed ped;
    SidewalkTickParams params;  // snapSegmentId left at 0 (no resolution yet)
    params.advancePerTick = 0.5;
    TickSidewalk(ped, params);
    CHECK(!ped.onSegment);
    CHECK(ped.segmentProgress == 0.0);
    CHECK(ped.idleSubState == PedIdleSubState::SidewalkStart);  // unchanged - still waiting to snap
}

void testTickSidewalkAbandonsToWanderChooseDest() {
    NearFieldPed ped;
    ped.onSegment = true;
    ped.idleSubState = PedIdleSubState::Sidewalk;
    SidewalkTickParams params;
    params.shouldAbandonToWander = [] { return true; };
    TickSidewalk(ped, params);
    CHECK(ped.idleSubState == PedIdleSubState::WanderChooseDest);  // 0x0051bd20's own force-transition
    CHECK(!ped.onSegment);
}

void testWanderPauseElapsesIntoChooseDest() {
    NearFieldPed ped;
    ped.idleSubState = PedIdleSubState::WanderPause;
    WanderTickParams params;
    params.pauseElapsed = [] { return true; };
    Vec3 dest{0, 0, 0};
    TickWander(ped, params, dest);
    CHECK(ped.idleSubState == PedIdleSubState::WanderChooseDest);
}

void testWanderPauseStaysPutUntilElapsed() {
    NearFieldPed ped;
    ped.idleSubState = PedIdleSubState::WanderPause;
    WanderTickParams params;
    params.pauseElapsed = [] { return false; };
    Vec3 dest{0, 0, 0};
    TickWander(ped, params, dest);
    CHECK(ped.idleSubState == PedIdleSubState::WanderPause);
}

void testWanderChooseDestCommitsDestinationAndMoves() {
    NearFieldPed ped;
    ped.idleSubState = PedIdleSubState::WanderChooseDest;
    WanderTickParams params;
    params.chooseRandomDestination = [] { return std::optional<Vec3>(Vec3{10, 0, 20}); };
    Vec3 dest{0, 0, 0};
    TickWander(ped, params, dest);
    CHECK(ped.idleSubState == PedIdleSubState::WanderMove);  // Sec20.2: "transitioning to sub-state 34 and calling 0x00993630"
    CHECK(dest.x == 10 && dest.z == 20);
}

void testWanderChooseDestWithNoCandidateStaysPut() {
    NearFieldPed ped;
    ped.idleSubState = PedIdleSubState::WanderChooseDest;
    WanderTickParams params;  // default chooseRandomDestination returns nullopt
    Vec3 dest{0, 0, 0};
    TickWander(ped, params, dest);
    CHECK(ped.idleSubState == PedIdleSubState::WanderChooseDest);
}

void testWanderMoveReachesDestinationThenTransitionsToWanderToSidewalk() {
    NearFieldPed ped;
    ped.idleSubState = PedIdleSubState::WanderMove;
    WanderTickParams params;
    params.hasReachedDestination = [](const Vec3&) { return true; };
    Vec3 dest{1, 2, 3};
    TickWander(ped, params, dest);
    CHECK(ped.idleSubState == PedIdleSubState::WanderToSidewalk);
}

void testOneFrameTransitionReturnsToSidewalkStart() {
    NearFieldPed ped;
    ped.idleSubState = PedIdleSubState::WanderToSidewalk;
    ped.onSegment = true;  // stale from before wandering - must be cleared
    TickOneFrameTransition(ped);
    CHECK(ped.idleSubState == PedIdleSubState::SidewalkStart);
    CHECK(!ped.onSegment);
}

void testTickIdleSubBehaviourDispatchesToTheRightTickFunction() {
    NearFieldPed ped;  // sub-state 13 (sidewalk group)
    SidewalkTickParams sp;
    sp.snapSegmentId = 5;
    WanderTickParams wp;
    Vec3 dest{0, 0, 0};
    TickIdleSubBehaviour(ped, sp, wp, dest);
    CHECK(ped.idleSubState == PedIdleSubState::Sidewalk);  // routed through TickSidewalk, not TickWander

    ped.idleSubState = PedIdleSubState::WanderPause;
    wp.pauseElapsed = [] { return true; };
    TickIdleSubBehaviour(ped, sp, wp, dest);
    CHECK(ped.idleSubState == PedIdleSubState::WanderChooseDest);  // routed through TickWander this time

    ped.idleSubState = PedIdleSubState::WanderToSidewalk;
    TickIdleSubBehaviour(ped, sp, wp, dest);
    CHECK(ped.idleSubState == PedIdleSubState::SidewalkStart);  // routed through TickOneFrameTransition
}

// ===========================================================================
// Part D: action_node_npcs.xtbl-driven Idle cooldown data (Sec20.3).
// ===========================================================================
void testActionNodeIdleCooldownAbsentRowReturnsZero() {
    ActionNodeIdleTiming timing;  // present=false
    auto randomRangeMs = [](int64_t lo, int64_t hi) { return (lo + hi) / 2; };
    CHECK(RollActionNodeIdleCooldownMs(timing, randomRangeMs) == 0);
}

void testActionNodeIdleCooldownRollsWithinTheRowsOwnRange() {
    ActionNodeIdleTiming timing;
    timing.present = true;
    timing.minAnimDelayMs = 250;
    timing.maxAnimDelayMs = 1000;
    // Fixed seam pinned to the high end, same determinism idiom
    // near_field_vehicle_population.h's own driver test uses.
    auto randomRangeMs = [](int64_t /*lo*/, int64_t hi) { return hi; };
    CHECK(RollActionNodeIdleCooldownMs(timing, randomRangeMs) == 1000);

    auto randomRangeMsLo = [](int64_t lo, int64_t /*hi*/) { return lo; };
    CHECK(RollActionNodeIdleCooldownMs(timing, randomRangeMsLo) == 250);
}

void testActionNodeIdleCooldownToleratesSwappedMinMax() {
    ActionNodeIdleTiming timing;
    timing.present = true;
    timing.minAnimDelayMs = 5000;  // swapped on purpose
    timing.maxAnimDelayMs = 3000;
    auto randomRangeMsLo = [](int64_t lo, int64_t /*hi*/) { return lo; };
    CHECK(RollActionNodeIdleCooldownMs(timing, randomRangeMsLo) == 3000);  // the function sorts before calling the seam
}

// ===========================================================================
// Part E: the CIVILIAN-team-byte-gated gunfire flee/cower trigger (Sec20.4).
// ===========================================================================
NearFieldPed MakeCivilianPed() {
    NearFieldPed ped;
    ped.team = kCivilianTeamByte;
    ped.isFollower = false;
    return ped;
}

void testCivilianTeamByteIs7() { CHECK(kCivilianTeamByte == 7); }

void testNonCivilianTeamAlwaysGoesGunfireWaryRegardlessOfLethality() {
    NearFieldPed ped;
    ped.team = 3;  // not 7
    GunfireReactionResult rLethal = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/true);
    GunfireReactionResult rNonLethal = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/false);
    CHECK(rLethal.mode == PedReactionMode::GunfireWary);
    CHECK(rNonLethal.mode == PedReactionMode::GunfireWary);  // unconditional, Sec20.4
}

void testCivilianFollowerIsTreatedAsNonCivilianByTheFollowerGate() {
    // Sec20.4's own test is "NOT-follower AND team==7" - a civilian WHO IS A
    // FOLLOWER must take the same branch as a non-civilian (GunfireWary),
    // proving team==7 alone is not sufficient.
    NearFieldPed ped = MakeCivilianPed();
    ped.isFollower = true;
    GunfireReactionResult r = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/true);
    CHECK(r.mode == PedReactionMode::GunfireWary);
}

void testCivilianNonFollowerLethalGoesToFleeByDefault() {
    NearFieldPed ped = MakeCivilianPed();
    GunfireReactionResult r = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/true);
    CHECK(r.mode == PedReactionMode::Flee);  // CHOSEN default preferFlee=true
    CHECK(!r.spokeLineOnly);
}

void testCivilianNonFollowerNonLethalOnlySpeaksALineNoModeChange() {
    NearFieldPed ped = MakeCivilianPed();
    GunfireReactionResult r = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/false);
    CHECK(r.mode == PedReactionMode::None);
    CHECK(r.spokeLineOnly);  // Sec20.4: "it only speaks a line and posts a 250-750ms delayed re-check (kind 67)"
}

void testCivilianFleeFallsThroughToCowerWhenFleeIneligible() {
    NearFieldPed ped = MakeCivilianPed();
    FleeOrCowerChoice choice;
    choice.preferFlee = true;
    choice.fleeEligible = false;
    choice.cowerEligible = true;
    GunfireReactionResult r = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/true, choice);
    CHECK(r.mode == PedReactionMode::Cower);  // Sec20.4: "a failed Flee falls through to attempt Cower"
}

void testCivilianPreferCowerOrderingWhenNotPreferringFlee() {
    NearFieldPed ped = MakeCivilianPed();
    FleeOrCowerChoice choice;
    choice.preferFlee = false;
    choice.fleeEligible = true;
    choice.cowerEligible = true;
    GunfireReactionResult r = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/true, choice);
    CHECK(r.mode == PedReactionMode::Cower);
}

void testCivilianNeitherEligibleProducesNoModeChange() {
    NearFieldPed ped = MakeCivilianPed();
    FleeOrCowerChoice choice;
    choice.fleeEligible = false;
    choice.cowerEligible = false;
    GunfireReactionResult r = EvaluateGunfireReaction(ped, /*isLethalOrAttacked=*/true, choice);
    CHECK(r.mode == PedReactionMode::None);
}

void testNoOpGatesBlockEverythingRegardlessOfTeamOrLethality() {
    FleeOrCowerChoice choice;  // all-eligible, prefer flee
    {
        NearFieldPed ped = MakeCivilianPed();
        ped.downOrDead = true;
        CHECK(EvaluateGunfireReaction(ped, true, choice).mode == PedReactionMode::None);
    }
    {
        NearFieldPed ped = MakeCivilianPed();
        ped.ignoreAttacks = true;
        CHECK(EvaluateGunfireReaction(ped, true, choice).mode == PedReactionMode::None);
    }
    {
        NearFieldPed ped = MakeCivilianPed();
        ped.combatDisabled = true;
        CHECK(EvaluateGunfireReaction(ped, true, choice).mode == PedReactionMode::None);
    }
    {
        NearFieldPed ped = MakeCivilianPed();
        ped.dontReactToPlayer = true;
        CHECK(EvaluateGunfireReaction(ped, true, choice).mode == PedReactionMode::None);
    }
}

// ===========================================================================
// Part F: THE REQUIRED REGRESSION GUARD - Panic (mode 24) is never
// requested anywhere in this module, across an exhaustive matrix of inputs.
// Sec20.4's own honest negative: "'Panic' (mode 24) is never requested
// anywhere in this entire chain... nothing in the gunfire/attack reaction
// path ever requests it."
// ===========================================================================
void testModePanicLiteralIs24() { CHECK(kModePanic == 24); }

void testPanicIsNeverRequestedAcrossAllInputCombinations() {
    const bool bools[2] = {false, true};
    for (bool downOrDead : bools)
        for (bool ignoreAttacks : bools)
            for (bool combatDisabled : bools)
                for (bool dontReactToPlayer : bools)
                    for (bool isFollower : bools)
                        for (int team : {kCivilianTeamByte, 0, 1, 2, 3, 9})
                            for (bool isLethalOrAttacked : bools)
                                for (bool preferFlee : bools)
                                    for (bool fleeEligible : bools)
                                        for (bool cowerEligible : bools) {
                                            NearFieldPed ped;
                                            ped.downOrDead = downOrDead;
                                            ped.ignoreAttacks = ignoreAttacks;
                                            ped.combatDisabled = combatDisabled;
                                            ped.dontReactToPlayer = dontReactToPlayer;
                                            ped.isFollower = isFollower;
                                            ped.team = team;
                                            FleeOrCowerChoice choice;
                                            choice.preferFlee = preferFlee;
                                            choice.fleeEligible = fleeEligible;
                                            choice.cowerEligible = cowerEligible;
                                            GunfireReactionResult r =
                                                EvaluateGunfireReaction(ped, isLethalOrAttacked, choice);
                                            CHECK(r.mode != PedReactionMode::Panic);
                                        }
}

void testChooseFleeOrCowerDirectlyNeverReturnsPanic() {
    const bool bools[2] = {false, true};
    for (bool preferFlee : bools)
        for (bool fleeEligible : bools)
            for (bool cowerEligible : bools) {
                FleeOrCowerChoice choice{preferFlee, fleeEligible, cowerEligible};
                CHECK(ChooseFleeOrCower(choice) != PedReactionMode::Panic);
            }
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
    run("testGatherSidewalkSegmentCandidatesFiltersByMidpointDistance",
        testGatherSidewalkSegmentCandidatesFiltersByMidpointDistance);
    run("testSelectNearFieldPedSpawnNoCandidatesNotFound", testSelectNearFieldPedSpawnNoCandidatesNotFound);
    run("testSelectNearFieldPedSpawnPrefersRealSegmentOverDegenerateOnes",
        testSelectNearFieldPedSpawnPrefersRealSegmentOverDegenerateOnes);
    run("testSelectNearFieldPedSpawnRejectsWithinCameraNearDistance",
        testSelectNearFieldPedSpawnRejectsWithinCameraNearDistance);
    run("testSelectNearFieldPedSpawnAcceptsBeyondNearDistance", testSelectNearFieldPedSpawnAcceptsBeyondNearDistance);
    run("testSelectNearFieldPedSpawnRejectsBeyondFarDistanceWhenGateEnabled",
        testSelectNearFieldPedSpawnRejectsBeyondFarDistanceWhenGateEnabled);
    run("testSelectNearFieldPedSpawnRejectsWhenDifficultyGateFails",
        testSelectNearFieldPedSpawnRejectsWhenDifficultyGateFails);
    run("testSelectNearFieldPedSpawnRejectsWhenVisibleToCamera",
        testSelectNearFieldPedSpawnRejectsWhenVisibleToCamera);
    run("testSelectNearFieldPedSpawnRejectsWhenGroundInvalid",
        testSelectNearFieldPedSpawnRejectsWhenGroundInvalid);
    run("testSelectNearFieldPedSpawnAppliesSidewaysOffset", testSelectNearFieldPedSpawnAppliesSidewaysOffset);

    run("testApproxVisibleToCameraTrueDirectlyAhead", testApproxVisibleToCameraTrueDirectlyAhead);
    run("testApproxVisibleToCameraFalseDirectlyBehind", testApproxVisibleToCameraFalseDirectlyBehind);
    run("testApproxVisibleToCameraFalseDirectlyToTheSide", testApproxVisibleToCameraFalseDirectlyToTheSide);
    run("testApproxVisibleToCameraRespectsExplicitHalfFov", testApproxVisibleToCameraRespectsExplicitHalfFov);
    run("testSelectNearFieldPedSpawnAcceptsCloseCandidateBehindCamera",
        testSelectNearFieldPedSpawnAcceptsCloseCandidateBehindCamera);
    run("testSelectNearFieldPedSpawnRejectsCloseCandidateAheadOfCamera",
        testSelectNearFieldPedSpawnRejectsCloseCandidateAheadOfCamera);
    run("testSelectNearFieldPedSpawnStillRejectsWithinFloorEvenWhenBehindCamera",
        testSelectNearFieldPedSpawnStillRejectsWithinFloorEvenWhenBehindCamera);

    run("testRunNearFieldPedSpawnPassRespectsLocalCap", testRunNearFieldPedSpawnPassRespectsLocalCap);
    run("testRunNearFieldPedSpawnPassBlocksOnSharedHumanHeadroom",
        testRunNearFieldPedSpawnPassBlocksOnSharedHumanHeadroom);
    run("testRunNearFieldPedSpawnPassSpawnsOnePedAlreadySnappedOntoSegment",
        testRunNearFieldPedSpawnPassSpawnsOnePedAlreadySnappedOntoSegment);

    run("testSidewalkIsTheTrueDefaultAtCreation", testSidewalkIsTheTrueDefaultAtCreation);
    run("testTickGroupRoutingMatchesTheRealSubStateTable", testTickGroupRoutingMatchesTheRealSubStateTable);
    run("testTickSidewalkSnapsOntoGraphThenAdvances", testTickSidewalkSnapsOntoGraphThenAdvances);
    run("testTickSidewalkWithoutSnapIdDoesNotAdvance", testTickSidewalkWithoutSnapIdDoesNotAdvance);
    run("testTickSidewalkAbandonsToWanderChooseDest", testTickSidewalkAbandonsToWanderChooseDest);
    run("testWanderPauseElapsesIntoChooseDest", testWanderPauseElapsesIntoChooseDest);
    run("testWanderPauseStaysPutUntilElapsed", testWanderPauseStaysPutUntilElapsed);
    run("testWanderChooseDestCommitsDestinationAndMoves", testWanderChooseDestCommitsDestinationAndMoves);
    run("testWanderChooseDestWithNoCandidateStaysPut", testWanderChooseDestWithNoCandidateStaysPut);
    run("testWanderMoveReachesDestinationThenTransitionsToWanderToSidewalk",
        testWanderMoveReachesDestinationThenTransitionsToWanderToSidewalk);
    run("testOneFrameTransitionReturnsToSidewalkStart", testOneFrameTransitionReturnsToSidewalkStart);
    run("testTickIdleSubBehaviourDispatchesToTheRightTickFunction",
        testTickIdleSubBehaviourDispatchesToTheRightTickFunction);

    run("testActionNodeIdleCooldownAbsentRowReturnsZero", testActionNodeIdleCooldownAbsentRowReturnsZero);
    run("testActionNodeIdleCooldownRollsWithinTheRowsOwnRange",
        testActionNodeIdleCooldownRollsWithinTheRowsOwnRange);
    run("testActionNodeIdleCooldownToleratesSwappedMinMax", testActionNodeIdleCooldownToleratesSwappedMinMax);

    run("testCivilianTeamByteIs7", testCivilianTeamByteIs7);
    run("testNonCivilianTeamAlwaysGoesGunfireWaryRegardlessOfLethality",
        testNonCivilianTeamAlwaysGoesGunfireWaryRegardlessOfLethality);
    run("testCivilianFollowerIsTreatedAsNonCivilianByTheFollowerGate",
        testCivilianFollowerIsTreatedAsNonCivilianByTheFollowerGate);
    run("testCivilianNonFollowerLethalGoesToFleeByDefault", testCivilianNonFollowerLethalGoesToFleeByDefault);
    run("testCivilianNonFollowerNonLethalOnlySpeaksALineNoModeChange",
        testCivilianNonFollowerNonLethalOnlySpeaksALineNoModeChange);
    run("testCivilianFleeFallsThroughToCowerWhenFleeIneligible",
        testCivilianFleeFallsThroughToCowerWhenFleeIneligible);
    run("testCivilianPreferCowerOrderingWhenNotPreferringFlee",
        testCivilianPreferCowerOrderingWhenNotPreferringFlee);
    run("testCivilianNeitherEligibleProducesNoModeChange", testCivilianNeitherEligibleProducesNoModeChange);
    run("testNoOpGatesBlockEverythingRegardlessOfTeamOrLethality",
        testNoOpGatesBlockEverythingRegardlessOfTeamOrLethality);

    run("testModePanicLiteralIs24", testModePanicLiteralIs24);
    run("testPanicIsNeverRequestedAcrossAllInputCombinations", testPanicIsNeverRequestedAcrossAllInputCombinations);
    run("testChooseFleeOrCowerDirectlyNeverReturnsPanic", testChooseFleeOrCowerDirectlyNeverReturnsPanic);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic near-field ped population tests passed.\n";
    return 0;
}
