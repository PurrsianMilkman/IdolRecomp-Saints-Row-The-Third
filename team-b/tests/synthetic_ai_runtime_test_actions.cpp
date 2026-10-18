// Synthetic tests for the AI runtime units A0-A3, spec-ai-runtime.md
// Sec2-Sec4/Sec6 (A0), Sec9 (A1), Sec10 (A2), Sec11 (A3). Every expected
// value below is a rule quoted from that spec (cited per block); world
// facts come from plain struct fields set directly in each test - injected
// test values, not data.

#include "synthetic_ai_runtime_test_common.h"

namespace airtest {

// ===================================================================
// THE 8 NON-PURE ROUTINES (Sec11.2): side effects fire exactly where the
// chooser calls them (the goal loop, and the ordered re-assert path), and
// nowhere a routine is merely probed without being reached by the chooser.
// ===================================================================

// NOTE: wires h.actionRoutine into the OUT PARAMETER h directly, not into a
// local later returned by value. ActionRegistry::wire()'s own contract
// (actions.h) captures &h for the lifetime of the wiring, so wiring a local
// and then `return`ing it by value leaves the lambda referring to a
// destroyed temporary unless the compiler happens to apply NRVO - which
// MSVC does in a Release build but not reliably in Debug, so that version
// silently depended on an unspecified optimization. This is a test-harness
// lifetime bug, not a spec or production-code issue.
void MakeActionHost(HumanAiState& h, ActionRegistry& registry, ActionWorldFacts& facts, int64_t nowMs) {
    h = MakeBaseline();
    registry.wire(h, facts, nowMs);
}

// Runs ONE action through the chooser's goal loop (goal 0, one-entry list),
// with its ability enabled, matching pre_flags and an expired timer, so the
// chooser is guaranteed to actually reach the routine call.
uint8_t RunViaGoalLoop(HumanAiState& h, int actionId, int64_t nowMs) {
    h.behavior.goalEnabled[goal::ChangeMode] = 1;
    h.goalActionLists[goal::ChangeMode][0] = static_cast<uint8_t>(actionId);
    h.behavior.abilities[static_cast<size_t>(actionId)].enabled = true;
    h.preFlags[static_cast<size_t>(actionId)] = 0x07;
    // Armed and long expired: an unarmed timer blocks its action outright
    // (Sec9.7), which would prevent the chooser from ever reaching the
    // routine call this helper exists to exercise.
    h.actionRepeatTimers[static_cast<size_t>(actionId)].arm(nowMs - 10000, 1);
    uint8_t reason = 0;
    ActionBlock block;
    return Chooser(h, reason, block, nowMs);
}

void testNonPureRoutineDiscipline() {
    ActionRegistry registry;

    // 50 vehicle extract: backs itself off by 1000ms BEFORE testing
    // anything - even when the very first test (a live target) fails.
    {
        ActionWorldFacts facts;  // no target set -> every later test would fail too
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        CHECK(h.actionRepeatTimers[action::VehicleExtract].remainingMs(0) <= 0);
        uint8_t r = RunViaGoalLoop(h, action::VehicleExtract, 0);
        CHECK(r != action::VehicleExtract);  // refused (no target)
        CHECK(h.actionRepeatTimers[action::VehicleExtract].remainingMs(0) > 0);  // armed regardless
    }

    // 26 move sidestep: two distinct back-off durations depending on WHICH
    // test failed.
    {
        ActionWorldFacts facts;
        facts.targetWithinFacingCone = true;
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        h.world.targetHandleSet = true;
        h.world.targetHandle = 7;
        h.world.targetResolvesToUsableBaseTypeObject = true;
        // flag 0x400 (LosValid) is required by targetLineOfSightValidOrNone
        // (0x004fdae0) before the routine's own gating even starts.
        h.perception.postStimulus(plist::KnownHostiles, 7, {}, 0, pflag::ActivelyKnown | pflag::LosValid, 1, 0, 0);
        // Engageable (so the dodge-roll branch runs), roll fails, not
        // aimed-at/shot-at -> 1000ms back-off.
        h.perception.postStimulus(plist::LineOfFire, 7, {}, 0, 0x06000000u, 1, 0, 0);
        facts.dodgeRollSucceeded = false;
        CHECK(h.actionRepeatTimers[action::MoveSidestep].remainingMs(0) <= 0);
        uint8_t r = RunViaGoalLoop(h, action::MoveSidestep, 0);
        CHECK(r != action::MoveSidestep);
        CHECK(h.actionRepeatTimers[action::MoveSidestep].remainingMs(0) == 1000);
    }
    {
        ActionWorldFacts facts;
        facts.targetWithinFacingCone = true;
        facts.sidestepCandidateFound = false;  // no clear lateral candidate -> 5000ms
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        h.world.targetHandleSet = true;
        h.world.targetHandle = 7;
        h.world.targetResolvesToUsableBaseTypeObject = true;
        h.perception.postStimulus(plist::KnownHostiles, 7, {}, 0, pflag::ActivelyKnown | pflag::LosValid, 1, 0, 0);
        // Not engageable (no list-2 entry, old sighting) -> skip the dodge
        // roll and reach the candidate search directly.
        uint8_t r = RunViaGoalLoop(h, action::MoveSidestep, 0);
        CHECK(r != action::MoveSidestep);
        CHECK(h.actionRepeatTimers[action::MoveSidestep].remainingMs(0) == 5000);
    }

    // 45 pickup weapon / 46 human shield grab / 51 brute throw prop: back
    // off on refusal with their own documented durations.
    {
        ActionWorldFacts facts;  // pickupCandidateFound = false
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        uint8_t r = RunViaGoalLoop(h, action::PickupWeapon, 0);
        CHECK(r != action::PickupWeapon);
        CHECK(h.actionRepeatTimers[action::PickupWeapon].remainingMs(0) == 700);
    }
    {
        ActionWorldFacts facts;
        facts.canGrabVictim = true;  // reach the candidate check, then fail it
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        uint8_t r = RunViaGoalLoop(h, action::HumanShieldGrab, 0);
        CHECK(r != action::HumanShieldGrab);
        CHECK(h.actionRepeatTimers[action::HumanShieldGrab].remainingMs(0) == 400);
    }
    {
        ActionWorldFacts facts;
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        h.world.targetHandleSet = true;
        h.world.targetHandle = 9;
        h.world.targetResolvesToUsableBaseTypeObject = true;
        uint8_t r = RunViaGoalLoop(h, action::BruteThrowProp, 0);
        CHECK(r != action::BruteThrowProp);
        CHECK(h.actionRepeatTimers[action::BruteThrowProp].remainingMs(0) == 1000);
    }

    // 61 avatar teleport stomp: backs off a DIFFERENT action (58), and
    // leaves its OWN timer untouched.
    {
        ActionWorldFacts facts;
        facts.targetRagdoll = true;
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        h.world.targetHandleSet = true;
        h.world.targetHandle = 3;
        h.world.targetResolvesToUsableBaseTypeObject = true;
        CHECK(h.actionRepeatTimers[action::AvatarStomp].remainingMs(0) <= 0);
        uint8_t r = RunViaGoalLoop(h, action::AvatarTeleportStomp, 0);
        CHECK(r != action::AvatarTeleportStomp);
        CHECK(h.actionRepeatTimers[action::AvatarStomp].remainingMs(0) == 7000);     // the OTHER action
        CHECK(h.actionRepeatTimers[action::AvatarTeleportStomp].remainingMs(0) <= 0);  // its own: untouched
    }

    // 42 investigate look: the snap + flag-clear write-back fires
    // regardless of whether the routine goes on to accept or refuse.
    {
        ActionWorldFacts facts;
        facts.snappedCandidatePosition = Vec3{5, 0, 0};
        facts.snappedPositionDiffersBeyondThreshold = true;  // moved > 0.25 -> clears flag 0x10
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        h.behavior.investigateSeenInLastMs = 100000;
        h.perception.postStimulus(plist::LastSightings, 11, Vec3{0, 0, 0}, 50,
                                   pflag::PositionVerified, 1, 0, 0);
        uint8_t r = RunViaGoalLoop(h, action::InvestigateLook, 0);
        CHECK(r != action::InvestigateLook);  // refused: flag cleared by the snap before the gate re-checks it
        const PerceptionEntry* e = h.perception.find(plist::LastSightings, 11);
        CHECK(e != nullptr);
        CHECK(e->position.x == 5);                       // position written back regardless of outcome
        CHECK(!(e->flags & pflag::PositionVerified));     // cleared by the snap
    }
    {
        ActionWorldFacts facts;
        facts.snappedCandidatePosition = Vec3{1, 0, 0};
        facts.snappedPositionDiffersBeyondThreshold = false;  // small snap -> flag survives -> accept
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        h.behavior.investigateSeenInLastMs = 100000;
        h.perception.postStimulus(plist::LastSightings, 11, Vec3{0, 0, 0}, 50,
                                   pflag::PositionVerified, 1, 0, 0);
        uint8_t r = RunViaGoalLoop(h, action::InvestigateLook, 0);
        CHECK(r == action::InvestigateLook);
        const PerceptionEntry* e = h.perception.find(plist::LastSightings, 11);
        CHECK(e != nullptr && (e->flags & pflag::PositionVerified));
    }

    // Discipline check: when the chooser's OWN gating (ability disabled)
    // never reaches a non-pure routine, its side effect does not happen -
    // demonstrating the side effect is tied to the chooser's call, not to
    // anything else.
    {
        ActionWorldFacts facts;
        HumanAiState h;
        MakeActionHost(h, registry, facts, 0);
        h.behavior.goalEnabled[goal::ChangeMode] = 1;
        h.goalActionLists[goal::ChangeMode][0] = action::PickupWeapon;
        h.behavior.abilities[action::PickupWeapon].enabled = false;  // never reaches the routine
        h.preFlags[action::PickupWeapon] = 0x07;
        uint8_t reason = 0;
        ActionBlock block;
        Chooser(h, reason, block, 0);
        CHECK(h.actionRepeatTimers[action::PickupWeapon].remainingMs(0) <= 0);  // never armed
    }
}

// ===================================================================
// A3 - a sample of implemented action routines (Sec11.3)
// ===================================================================

void testActionRoutineSample() {
    ActionRegistry registry;

    // 69 idle: accepted on foot, not a follower, not free-fall/rappel.
    {
        ActionWorldFacts facts;
        HumanAiState h = MakeBaseline();
        ActionBlock block;
        CHECK(registry.call(action::Idle, h, facts, block, 0));
        h.world.isFollower = true;
        CHECK(!registry.call(action::Idle, h, facts, block, 0));
    }

    // 48 vehicle passenger: exactly the in-vehicle test.
    {
        ActionWorldFacts facts;
        HumanAiState h = MakeBaseline();
        ActionBlock block;
        CHECK(!registry.call(action::VehiclePassenger, h, facts, block, 0));
        h.world.inVehicle = true;
        CHECK(registry.call(action::VehiclePassenger, h, facts, block, 0));
    }

    // 0 fire: needs canFireWithGrace750 AND an engageable target.
    {
        ActionWorldFacts facts;
        HumanAiState h = MakeBaseline();
        ActionBlock block;
        CHECK(!registry.call(action::Fire, h, facts, block, 0));  // no target
        h.world.targetHandleSet = true;
        h.world.targetHandle = 1;
        h.world.targetResolvesToUsableBaseTypeObject = true;
        CHECK(!registry.call(action::Fire, h, facts, block, 0));  // can't fire yet
        facts.canFireWithGrace750 = true;
        CHECK(!registry.call(action::Fire, h, facts, block, 0));  // not engageable (no list-0 entry)
        h.perception.postStimulus(plist::KnownHostiles, 1, {}, 0, pflag::ActivelyKnown, 1, 0, 0);
        h.perception.postStimulus(plist::LineOfFire, 1, {}, 0, 0x06000000u, 1, 0, 0);
        CHECK(registry.call(action::Fire, h, facts, block, 0));
    }

    // 47 taunt: list 6 or list 0 non-empty.
    {
        ActionWorldFacts facts;
        HumanAiState h = MakeBaseline();
        ActionBlock block;
        CHECK(!registry.call(action::Taunt, h, facts, block, 0));
        h.perception.postStimulus(plist::Hearsay, 2, {}, 50, 0, 1, 0, 0);
        CHECK(registry.call(action::Taunt, h, facts, block, 0));
    }

    // 11 cover stay down: ability-enabled recheck, +0xe3c, radius, writes
    // the handle and (unless the animation is playing) the geometry fields.
    {
        ActionWorldFacts facts;
        facts.flagE3c = 0.5;
        facts.coverFireRadius = 2.0;
        facts.coverEvaluatorApproves = true;
        facts.coverFireGeomPoint = Vec3{1, 2, 3};
        facts.coverObjectHandle = 55;
        HumanAiState h = MakeBaseline();
        h.behavior.abilities[action::CoverStayDown].enabled = true;
        h.world.hasCoverObject = true;
        h.world.coverObjectPosition = Vec3{0, 0, 0};
        h.position = Vec3{1, 0, 0};
        ActionBlock block;
        CHECK(registry.call(action::CoverStayDown, h, facts, block, 0));
        CHECK(block.objectHandle == 55);
        CHECK(block.secondaryPoint.x == 1 && block.secondaryPoint.y == 2 && block.secondaryPoint.z == 3);
    }

    // Family A stubs: 9 and 14 always accept; 58 always refuses.
    {
        ActionWorldFacts facts;
        HumanAiState h = MakeBaseline();
        ActionBlock block;
        CHECK(registry.call(action::CoverTakeOff, h, facts, block, 0));
        CHECK(registry.call(action::HoldPosition, h, facts, block, 0));
        CHECK(!registry.call(action::AvatarStomp, h, facts, block, 0));
    }

    // 5 throw weapon / 4 throw grenade (Sec11.3 H, gating resolved by unit
    // A5's Sec13.2) are now implemented, not OpenStateError stubs.
    CHECK(registry.isImplemented(action::ThrowWeapon));
    CHECK(registry.isImplemented(action::ThrowGrenade));

    // 5 throw weapon: needs a throwable held item, a target that exists
    // but is NOT visible, not in a vehicle, clear probes, a point P, and a
    // real trajectory solve.
    {
        ActionWorldFacts facts;
        HumanAiState h = MakeBaseline();
        ActionBlock block;
        h.world.targetHandleSet = true;
        h.world.targetHandle = 9;
        h.world.targetResolvesToUsableBaseTypeObject = true;
        // No KnownHostiles stimulus posted -> visibleNow() is false, i.e.
        // "T exists and is not visible" (Sec11.3 H's own requirement).
        CHECK(!registry.call(action::ThrowWeapon, h, facts, block, 0));  // not throwable yet
        facts.heldItemThrowableNow = true;
        CHECK(!registry.call(action::ThrowWeapon, h, facts, block, 0));  // no P yet (precomputedPoint unset)
        facts.precomputedPoint = Vec3{10, 0, 0};
        facts.throwArc = 1.0;
        facts.throwSpeed = 20.0;
        h.position = Vec3{0, 0, 0};
        CHECK(registry.call(action::ThrowWeapon, h, facts, block, 0));
        CHECK(block.secondaryPoint.x == 10 && block.secondaryPoint.y == 0 && block.secondaryPoint.z == 0);
        // The oracle's own "flat, same height" row (see
        // testThrowTrajectoryOracle) for this exact origin/target/arc/speed.
        CHECK(std::fabs(block.tertiaryVector.x - 0.999922) < 1e-4);
        CHECK(std::fabs(block.tertiaryVector.y - (-0.012502)) < 1e-4);
        // Visible target refuses outright.
        h.perception.postStimulus(plist::KnownHostiles, 9, {}, 0, pflag::ActivelyKnown | pflag::InView, 1, 0, 0);
        CHECK(!registry.call(action::ThrowWeapon, h, facts, block, 0));
    }

    // 4 throw grenade: a grenade in hand, the can-throw timer, a target,
    // the unread precondition, then tries candidates in turn, screening
    // each against the avoid list before the trajectory solve.
    {
        ActionWorldFacts facts;
        HumanAiState h = MakeBaseline();
        ActionBlock block;
        h.position = Vec3{0, 0, 0};
        facts.throwArc = 1.0;
        facts.throwSpeed = 20.0;
        CHECK(!registry.call(action::ThrowGrenade, h, facts, block, 0));  // no grenade in hand
        facts.grenadeInHand = true;
        CHECK(!registry.call(action::ThrowGrenade, h, facts, block, 0));  // can't throw yet (timer)
        facts.canThrowGrenadeNow = true;
        CHECK(!registry.call(action::ThrowGrenade, h, facts, block, 0));  // no target
        facts.grenadeHasTarget = true;
        CHECK(!registry.call(action::ThrowGrenade, h, facts, block, 0));  // no candidate points at all
        // First candidate blocked by an ally within the avoid radius;
        // second candidate clear -> the first that SOLVES wins.
        facts.grenadeCandidatePoints = {Vec3{10, 0, 0}, Vec3{0, 0, 10}};
        facts.grenadeAvoidPositions = {Vec3{10, 0, 0.5}};
        facts.grenadeAvoidRadius = 2.0;
        CHECK(registry.call(action::ThrowGrenade, h, facts, block, 0));
        CHECK(block.secondaryPoint.x == 0 && block.secondaryPoint.z == 10);  // the second candidate, not the blocked first
        CHECK(std::fabs(block.tertiaryVector.z - 0.999922) < 1e-4);          // the oracle's "target along Z" row
        // The unread precondition (0x0054d930's own 0x0054d180 call),
        // forced false by the host, refuses regardless of candidates.
        facts.grenadePrecondition0x0054d180 = false;
        CHECK(!registry.call(action::ThrowGrenade, h, facts, block, 0));
    }
}

// ===================================================================
// Unit A5 (Sec13.2.1): the shared ballistic-angle core SolveThrowTrajectory
// (0x0054c630), checked against all 11 of that section's own Unicorn-
// emulation oracle vectors (real function bytes, zero stubs needed for the
// core itself) - the exact values quoted in spec-ai-runtime.md's own
// table.
// ===================================================================

void testThrowTrajectoryOracle() {
    auto checkVec = [](const Vec3& got, double x, double y, double z, double eps = 1e-4) {
        CHECK(std::fabs(got.x - x) < eps);
        CHECK(std::fabs(got.y - y) < eps);
        CHECK(std::fabs(got.z - z) < eps);
    };

    struct Case {
        Vec3 origin, target;
        double arc, speed;
        Vec3 outA, outB;
    };
    const Case cases[] = {
        {{0, 0, 0}, {10, 0, 0}, 1.0, 20.0, {0.999922, -0.012502, 0.0}, {0.012501, -0.999922, 0.0}},
        {{0, 0, 0}, {10, 2, 0}, 1.0, 20.0, {0.982950, 0.183874, 0.0}, {0.012470, -0.999922, 0.0}},
        {{0, 0, 0}, {10, -2, 0}, 1.0, 20.0, {0.978046, -0.208390, 0.0}, {0.012532, -0.999921, 0.0}},
        {{0, 0, 0}, {5, 0, 0}, 1.0, 20.0, {0.999980, -0.006250, 0.0}, {0.006250, -0.999980, 0.0}},
        {{0, 0, 0}, {20, 0, 0}, 1.0, 20.0, {0.999687, -0.025007, 0.0}, {0.025008, -0.999687, 0.0}},
        {{0, 0, 0}, {10, 0, 0}, 1.0, 10.0, {0.998746, -0.050063, 0.0}, {0.050063, -0.998746, 0.0}},
        {{0, 0, 0}, {10, 0, 0}, 1.0, 40.0, {0.999995, -0.003125, 0.0}, {0.003125, -0.999995, 0.0}},
        {{0, 0, 0}, {10, 0, 0}, 9.8, 20.0, {0.992352, -0.123444, 0.0}, {0.123444, -0.992352, 0.0}},
        {{0, 0, 0}, {10, 0, 0}, 0.0, 20.0, {1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}},
        {{0, 0, 0}, {0, 0, 10}, 1.0, 20.0, {0.0, -0.012502, 0.999922}, {0.0, -0.999922, 0.012501}},
        {{5, 1, 5}, {15, 1, 9}, 1.0, 20.0, {0.928393, -0.013464, 0.371357}, {0.012501, -0.999909, 0.005000}},
    };
    for (const Case& c : cases) {
        ThrowTrajectoryResult r = SolveThrowTrajectory(c.origin, c.target, c.arc, c.speed);
        CHECK(r.solved);
        checkVec(r.outA, c.outA.x, c.outA.y, c.outA.z);
        checkVec(r.outB, c.outB.x, c.outB.y, c.outB.z);
        // Both buffers are always unit-length (Sec13.2.1's own confirmed finding).
        CHECK(std::fabs(Length(r.outA) - 1.0) < 1e-6);
        CHECK(std::fabs(Length(r.outB) - 1.0) < 1e-6);
    }

    // The degenerate-distance guard (step2): origin == target horizontally
    // still returns a solved, unit-length result rather than a NaN/crash.
    {
        ThrowTrajectoryResult r = SolveThrowTrajectory(Vec3{1, 0, 1}, Vec3{1, 5, 1}, 1.0, 20.0);
        CHECK(r.solved);
        CHECK(std::fabs(Length(r.outA) - 1.0) < 1e-6);
    }
    // No real solution (disc < 0): too slow / too much arc for the distance.
    {
        ThrowTrajectoryResult r = SolveThrowTrajectory(Vec3{0, 0, 0}, Vec3{1000, 0, 0}, 50.0, 1.0);
        CHECK(!r.solved);
    }
}


}  // namespace airtest
