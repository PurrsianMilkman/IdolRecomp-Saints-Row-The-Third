// Synthetic tests for the AI runtime units A0-A3, spec-ai-runtime.md
// Sec2-Sec4/Sec6 (A0), Sec9 (A1), Sec10 (A2), Sec11 (A3). Every expected
// value below is a rule quoted from that spec (cited per block); world
// facts come from plain struct fields set directly in each test - injected
// test values, not data.

#include "synthetic_ai_runtime_test_common.h"

namespace airtest {

// ===================================================================
// A1 - the decision step, gate, chooser, goal tests (Sec9)
// ===================================================================

HumanAiState MakeBaseline() {
    HumanAiState h;
    h.world.healthState = 0;
    h.killNearPlayerRadius.set(50.0);
    return h;
}

void testBuildStateMaskAndPrecondition() {
    HumanAiState h = MakeBaseline();
    CHECK((BuildStateMask(h) & 0x01) != 0);  // on foot
    h.world.inVehicle = true;
    CHECK((BuildStateMask(h) & 0x02) != 0);
    CHECK((BuildStateMask(h) & 0x01) == 0);
    h.world.inVehicle = false;
    h.world.healthState = 3;
    CHECK((BuildStateMask(h) & 0x04) != 0);  // in water
    h.forceFlags.inScriptedCover = true;
    CHECK((BuildStateMask(h) & 0x10) != 0);  // can do in cover

    // Precondition test (Sec9.4): common medium, target requirement, cover requirement.
    CHECK(!PreconditionTest(0, 0x01));         // zero mask never passes
    CHECK(PreconditionTest(0x01, 0x01));       // shared medium
    CHECK(!PreconditionTest(0x08, 0x01));      // needs target, state lacks it
    CHECK(PreconditionTest(0x09, 0x09));       // needs target, state has it (+ shares bit0)
    CHECK(!PreconditionTest(0x01, 0x11));      // state in cover, action not cover-capable
    CHECK(PreconditionTest(0x11, 0x11));
}

void testLiveTargetAndGate() {
    HumanAiState h = MakeBaseline();
    CHECK(!LiveTarget(h).has);  // no target handle set
    h.world.targetHandleSet = true;
    h.world.targetHandle = 42;
    h.world.targetResolvesToUsableBaseTypeObject = true;
    CHECK(LiveTarget(h).has);
    CHECK(LiveTarget(h).handle == 42);
    h.forceFlags.cantAttack = true;
    CHECK(!LiveTarget(h).has);
    h.forceFlags.cantAttack = false;
    h.world.targetIsDownOrDead = true;
    CHECK(!LiveTarget(h).has);

    // Gate (Sec9.3): always_cower blocks everything.
    h = MakeBaseline();
    h.forceFlags.alwaysCower = true;
    CHECK(!Gate(h, 0));
    h = MakeBaseline();
    h.world.healthState = 5;
    CHECK(!Gate(h, 0));
    h = MakeBaseline();
    h.world.heldInGrab = true;
    CHECK(!Gate(h, 0));
    // No finished/re-evaluate bit -> no decision (Sec9.1).
    h = MakeBaseline();
    CHECK(!Gate(h, 0));
    h.mode.state.actionFlags = ModeState::kActionFinished;
    CHECK(Gate(h, 0));
    // Free-fall (+0xcc8==14) forces a Skydiving request and refuses (Sec9.3 test 7).
    h = MakeBaseline();
    h.world.healthState = 14;
    h.mode.state.actionFlags = ModeState::kActionFinished;
    CHECK(!Gate(h, 0));
    CHECK(h.mode.state.switchPending && h.mode.state.requestedMode == mode::Skydiving);
}

void testGoalTestsSample() {
    // `survive` (Sec9.6).
    HumanAiState h = MakeBaseline();
    h.health = 10;
    h.maxHealth = 100;
    h.behavior.lowHealthPct = 0.5;
    CHECK(GoalSurvive(h, 0));
    h.health = 90;
    CHECK(!GoalSurvive(h, 0));

    // `idle` (Sec9.6): empty list 0, or goalsCanProcessEarly, or old enough head.
    h = MakeBaseline();
    h.behavior.investigateSeenInLastMs = 1000;
    CHECK(GoalIdle(h, 0));  // empty list 0
    h.perception.postStimulus(plist::KnownHostiles, 1, {}, 0, pflag::ActivelyKnown, 1, 0, 1000);
    CHECK(!GoalIdle(h, 1000));   // just noticed: age 0 < 1000
    CHECK(GoalIdle(h, 2600));    // age 1600 >= 1000

    // `reload` (Sec9.6): both canReload and the clip-fraction threshold.
    h = MakeBaseline();
    h.world.canReload = true;
    h.world.clipFraction = 0.1;
    h.behavior.clipPct = 0.5;
    CHECK(GoalReload(h, 0));
    h.world.canReload = false;
    CHECK(!GoalReload(h, 0));

    // `kill near player` (Sec9.6 goal 2): the OPEN tunable throws until seeded.
    {
        HumanAiState h2;  // killNearPlayerRadius left OPEN
        bool threw = false;
        try {
            GoalKillNearPlayer(h2, 0);
        } catch (const OpenStateError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // `frustrated` (Sec9.6): both timers armed, >=8000ms gap, head flag 0x100.
    // EngineTimer::restart(nowMs) records nowMs as the START time, so the
    // elapsed-time GAP between two timers is (laterStart - earlierStart),
    // independent of when it's later queried.
    h = MakeBaseline();
    h.mode.state.idleTimer.restart(0);      // idle started first (further in the past)
    h.mode.state.commitTimer.restart(9000); // commit started 9000ms after idle
    h.perception.postStimulus(plist::KnownHostiles, 1, {}, 0,
                               pflag::ActivelyKnown | pflag::VisuallyConfirmedOnce, 1, 0, 0);
    CHECK(GoalFrustrated(h, 20000));  // gap 9000 >= 8000
    h.mode.state.commitTimer.restart(3000);  // gap now only 3000
    CHECK(!GoalFrustrated(h, 20000));
}

void testChooserFallbackAndKeep() {
    // With no goals enabled and no ability data, the chooser falls back to
    // 14 (hold position) on foot, 48 (vehicle passenger) in a vehicle (Sec9.4 step 7).
    HumanAiState h = MakeBaseline();
    ActionBlock block;
    uint8_t reason = 0;
    uint8_t r = Chooser(h, reason, block, 0);
    CHECK(r == action::HoldPosition);
    CHECK(reason == 17);

    h.world.inVehicle = true;
    r = Chooser(h, reason, block, 0);
    CHECK(r == action::VehiclePassenger);

    // ignore_ai always proposes 14, reason NOT written (Sec9.4 step 4).
    h = MakeBaseline();
    h.forceFlags.ignoreAi = true;
    reason = 99;
    r = Chooser(h, reason, block, 0);
    CHECK(r == action::HoldPosition);
    CHECK(reason == 99);  // untouched

    // A real goal-driven accept, through a wired routine: set up goal 0
    // ("change mode", always true) naming one action with its ability
    // enabled, pre_flags/state sharing a medium bit, timer expired, and an
    // accepting routine.
    h = MakeBaseline();
    h.behavior.goalEnabled[goal::ChangeMode] = 1;
    h.goalActionLists[goal::ChangeMode][0] = 69;  // idle
    h.behavior.abilities[69].enabled = true;
    h.preFlags[69] = 0x01;  // on-foot medium bit
    h.actionRepeatTimers[69].arm(-10000, 1);  // armed and long expired (Sec9.7: unarmed blocks the action)
    bool routineCalled = false;
    h.actionRoutine = [&](int id, ActionBlock&) {
        routineCalled = true;
        return id == 69;
    };
    reason = 0;
    r = Chooser(h, reason, block, 0);
    CHECK(routineCalled);
    CHECK(r == 69);
    CHECK(reason == goal::ChangeMode);

    // "Keep": reaching the (goal, action) pair that is already current and
    // unfinished returns 0x47 (Sec9.4 step 6).
    h = MakeBaseline();
    h.behavior.goalEnabled[goal::ChangeMode] = 1;
    h.goalActionLists[goal::ChangeMode][0] = 69;
    h.behavior.abilities[69].enabled = true;
    h.preFlags[69] = 0x01;
    h.mode.state.committedReason = goal::ChangeMode;
    h.mode.state.currentAction = 69;
    h.mode.state.actionFlags = 0;  // bit 0 (finished) clear
    h.actionRoutine = [](int, ActionBlock&) { return true; };  // would otherwise accept
    reason = 0;
    r = Chooser(h, reason, block, 0);
    CHECK(r == ActionBlock::kNoAction);
}

void testDecisionStepIntegration() {
    // A finished human with a satisfied goal commits a new action and mode
    // request in one call (Sec9.2, Sec4.3).
    HumanAiState h = MakeBaseline();
    h.mode.state.actionFlags = ModeState::kActionFinished;
    h.behavior.goalEnabled[goal::ChangeMode] = 1;
    h.goalActionLists[goal::ChangeMode][0] = 69;  // idle -> category mode::Idle
    h.behavior.abilities[69].enabled = true;
    h.preFlags[69] = 0x01;
    h.actionRepeatTimers[69].arm(-10000, 1);
    h.actionRoutine = [](int id, ActionBlock&) { return id == 69; };

    DecisionStep(h, 1000);
    CHECK(h.mode.state.currentAction == 69);
    CHECK(h.mode.state.committedReason == goal::ChangeMode);
    CHECK(h.mode.state.switchPending && h.mode.state.requestedMode == mode::Idle);
    CHECK((h.mode.state.actionFlags & ModeState::kActionFinished) == 0);

    // A gate refusal clears bit 0 and leaves everything else untouched (Sec9.2).
    h = MakeBaseline();
    h.forceFlags.alwaysCower = true;
    h.mode.state.actionFlags = ModeState::kActionFinished;
    DecisionStep(h, 0);
    CHECK((h.mode.state.actionFlags & ModeState::kActionFinished) == 0);
    CHECK(h.mode.state.currentAction == 0x47);
}

// ===================================================================
// THE NULL-ROUTINE GUARD (Sec9.5, Sec11.1): actions 24/49 must never be
// dispatchable, in every reachable chooser state - not just when their id
// is passed directly to the registry.
// ===================================================================

void testNullRoutineGuard() {
    CHECK(HasNullRoutine(action::MoveScripted));
    CHECK(HasNullRoutine(action::VehicleEnterScpt));
    CHECK(!HasNullRoutine(action::Idle));

    ActionRegistry registry;
    ActionWorldFacts facts;
    ActionBlock probe;
    HumanAiState dummyHost = MakeBaseline();
    for (int nullAction : {action::MoveScripted, action::VehicleEnterScpt}) {
        bool threw = false;
        try {
            registry.call(nullAction, dummyHost, facts, probe, 0);
        } catch (const OpenStateError&) {
            threw = true;
        }
        CHECK(threw);
    }

    // Across several different gate-relevant states (on foot, in a
    // vehicle, with a target, with the action pre-enabled and its
    // precondition/timer satisfied), a goal list naming 24 or 49 must
    // never make the chooser select it - the goal loop keeps going and
    // ends at the fallback or a later real action, exactly as Sec9.5/
    // Sec11.1's "a port must still treat 24 and 49 as unchoosable by
    // goals" requires.
    struct Scenario {
        bool inVehicle;
        int32_t healthState;
    };
    const Scenario scenarios[] = {{false, 0}, {false, 7}, {true, 0}};
    for (int nullAction : {action::MoveScripted, action::VehicleEnterScpt}) {
        for (const auto& sc : scenarios) {
            HumanAiState h = MakeBaseline();
            h.world.inVehicle = sc.inVehicle;
            h.world.healthState = sc.healthState;
            h.behavior.goalEnabled[goal::ChangeMode] = 1;
            h.goalActionLists[goal::ChangeMode][0] = static_cast<uint8_t>(nullAction);
            h.goalActionLists[goal::ChangeMode][1] = action::HoldPosition;
            h.behavior.abilities[static_cast<size_t>(nullAction)].enabled = true;
            h.behavior.abilities[action::HoldPosition].enabled = true;
            h.preFlags[static_cast<size_t>(nullAction)] = 0x07;  // matches any medium
            h.preFlags[action::HoldPosition] = 0x07;
            h.actionRepeatTimers[static_cast<size_t>(nullAction)].arm(-10000, 1);
            h.actionRepeatTimers[action::HoldPosition].arm(-10000, 1);
            bool anyRoutineCalledForNullAction = false;
            h.actionRoutine = [&](int id, ActionBlock&) {
                if (id == nullAction) anyRoutineCalledForNullAction = true;
                return true;  // would accept anything, if ever reached
            };
            uint8_t reason = 0;
            ActionBlock block;
            uint8_t r = Chooser(h, reason, block, 0);
            CHECK(!anyRoutineCalledForNullAction);
            CHECK(r != static_cast<uint8_t>(nullAction));
            CHECK(r == action::HoldPosition);  // falls through to the next list entry
        }
    }
}


}  // namespace airtest
