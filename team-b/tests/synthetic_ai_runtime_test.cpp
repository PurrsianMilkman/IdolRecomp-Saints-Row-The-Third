// Synthetic tests for the AI runtime units A0-A3, spec-ai-runtime.md
// Sec2-Sec4/Sec6 (A0), Sec9 (A1), Sec10 (A2), Sec11 (A3). Every expected
// value below is a rule quoted from that spec (cited per block); world
// facts come from plain struct fields set directly in each test - injected
// test values, not data.

#include "synthetic_ai_runtime_test_common.h"

namespace airtest {

int g_failures = 0;

// ===================================================================
// A0 - the mode table and the request/apply/transition/acceptance
// protocol (Sec2-Sec4, Sec6)
// ===================================================================

void testModeTableFillCounts() {
    // Sec6's own "Fill counts" table, re-derived from kModeTable.
    int initNull = 0, initReal = 0, netWriteNull = 0, netReadNull = 0, remoteCount = 0;
    int ret1 = 0, ret3 = 0, ret4 = 0, real = 0;
    for (const auto& row : kModeTable) {
        if (row.initParams == SlotFill::Null) ++initNull;
        if (row.initParams != SlotFill::Null) ++initReal;
        if (row.netWrite == SlotFill::Null) ++netWriteNull;
        if (row.netRead == SlotFill::Null) ++netReadNull;
        if (row.remoteRequest) ++remoteCount;
        switch (row.eventSlot) {
            case EventSlotKind::ConstReturn1: ++ret1; break;
            case EventSlotKind::ConstReturn3: ++ret3; break;
            case EventSlotKind::ConstReturn4: ++ret4; break;
            case EventSlotKind::Real: ++real; break;
        }
    }
    CHECK(initNull == 14 && initReal == 30);         // +0x14: 30 non-null, 14 null (Sec6)
    CHECK(netWriteNull == 3);                         // +0x18: 3 null
    CHECK(netReadNull == 3);                          // +0x1c: 3 null
    CHECK(remoteCount == 16);                         // +0x20: 16 non-null
    CHECK(ret1 == 5 && ret3 == 1 && ret4 == 1 && real == 37);  // +0x10 stubs (Sec2.2, Sec6)

    int caeCount = 0, generic = 0, begin = 0, ignored = 0;
    for (const auto& row : kModeTable) {
        if (row.cae) ++caeCount;
        switch (row.applyGroup) {
            case ApplyGroup::Generic: ++generic; break;
            case ApplyGroup::Begin: ++begin; break;
            case ApplyGroup::Ignored: ++ignored; break;
        }
    }
    CHECK(caeCount == 20);   // Sec9.10 item 6: "exactly 20", not 27
    CHECK(generic == 18);    // Sec3.2's own group table
    CHECK(begin == 13);
    CHECK(ignored == 13);
    CHECK(mode::kCount == 44);
    CHECK(kModeTable[mode::Idle].name == std::string("Idle"));
    CHECK(kModeTable[mode::Player].name == std::string("Player"));
}

void testAcceptanceTest() {
    ModeState st;
    AcceptanceInputs in;

    // Stunned lock (Sec3.4 step 1): only Panic, only with the force flag.
    st.currentMode = mode::Stunned;
    st.subState = 9;
    in.stunnedLockForceFlagSet = false;
    CHECK(!AcceptanceTest(st, mode::Panic, in));
    CHECK(!AcceptanceTest(st, mode::Idle, in));
    in.stunnedLockForceFlagSet = true;
    CHECK(AcceptanceTest(st, mode::Panic, in));
    CHECK(!AcceptanceTest(st, mode::Idle, in));

    // Captured/locked (Sec3.4 step 2): only Stunned.
    st = ModeState{};
    in = AcceptanceInputs{};
    in.capturedOrLocked = true;
    CHECK(AcceptanceTest(st, mode::Stunned, in));
    CHECK(!AcceptanceTest(st, mode::Idle, in));

    // Health/state (Sec3.4 step 3).
    in = AcceptanceInputs{};
    in.healthState = 5;
    CHECK(AcceptanceTest(st, mode::Stunned, in));
    CHECK(!AcceptanceTest(st, mode::Panic, in));
    in.healthState = 7;
    CHECK(AcceptanceTest(st, mode::HumanShield, in));
    CHECK(AcceptanceTest(st, mode::Panic, in));
    CHECK(AcceptanceTest(st, mode::Stunned, in));
    CHECK(!AcceptanceTest(st, mode::Idle, in));

    // In a vehicle (Sec3.4 step 4): only the nine whitelisted modes.
    in = AcceptanceInputs{};
    in.inVehicle = true;
    for (int m : kVehicleCompatibleModes) CHECK(AcceptanceTest(st, m, in));
    CHECK(!AcceptanceTest(st, mode::Idle, in));
    CHECK(!AcceptanceTest(st, mode::CombatMovement, in));

    // On foot (Sec3.4 step 5): Passenger never; Reload refused only in Flee.
    in = AcceptanceInputs{};
    CHECK(!AcceptanceTest(st, mode::Passenger, in));
    CHECK(AcceptanceTest(st, mode::Reload, in));
    st.currentMode = mode::Flee;
    CHECK(!AcceptanceTest(st, mode::Reload, in));
    CHECK(AcceptanceTest(st, mode::Idle, in));  // everything else accepted
}

void testRequestLayer() {
    ModeMachine m;
    // A plain request succeeds and only sets the pending bit (Sec3.1):
    // no mode change yet.
    CHECK(m.request(mode::CombatMovement, 0));
    CHECK(m.state.switchPending);
    CHECK(m.state.requestedMode == mode::CombatMovement);
    CHECK(m.state.currentMode == mode::kNone);  // untouched

    // A pending Stunned request cannot be overridden by a later request (Sec3.1).
    m = ModeMachine{};
    CHECK(m.request(mode::Stunned, 0));
    CHECK(!m.request(mode::Idle, 0));
    CHECK(m.state.requestedMode == mode::Stunned);

    // Any OTHER pending request is simply overwritten.
    m = ModeMachine{};
    CHECK(m.request(mode::CombatMovement, 0));
    CHECK(m.request(mode::Idle, 0));
    CHECK(m.state.requestedMode == mode::Idle);

    // Requesting Idle clears +0x4a6 bit 4 and the +0x458 handle (Sec3.1 step 2).
    m = ModeMachine{};
    m.state.bit4a6_4_cowerForced = true;
    m.state.secondaryHandle458 = 0xABCD;
    CHECK(m.request(mode::Idle, 0));
    CHECK(!m.state.bit4a6_4_cowerForced);
    CHECK(m.state.secondaryHandle458 == 0);

    // mode >= 0x2c is rejected outright (Sec2.1/Sec3.1).
    m = ModeMachine{};
    CHECK(!m.request(44, 0));
    CHECK(!m.request(mode::kNone, 0));
}

void testTransitionPrimitive() {
    ModeMachine m;
    std::vector<std::string> calls;
    m.hooks[mode::Idle].onExit = [&](int32_t) { calls.push_back("idle-exit"); };
    m.hooks[mode::Idle].onEnter = [&]() { calls.push_back("idle-enter"); };
    m.hooks[mode::CombatMovement].onEnter = [&]() { calls.push_back("combat-enter"); };
    m.hooks[mode::CombatMovement].onExit = [&](int32_t) { calls.push_back("combat-exit"); };

    // From "no mode": exit is NOT called (current mode is kNone, Sec3.3 step 1).
    m.transition(mode::Idle, false, 0);
    CHECK(calls.size() == 1 && calls[0] == "idle-enter");
    CHECK(m.state.currentMode == mode::Idle);
    CHECK(m.state.previousMode == mode::kNone);
    CHECK(m.state.subState == mode::kSubStateNone);
    CHECK(m.state.justEntered);

    // Idle -> Combat Movement: exit(old) then enter(new), in that order (Sec3.3).
    calls.clear();
    m.transition(mode::CombatMovement, false, 0);
    CHECK(calls.size() == 2);
    CHECK(calls[0] == "idle-exit" && calls[1] == "combat-enter");
    CHECK(m.state.previousMode == mode::Idle);
    CHECK(m.state.currentMode == mode::CombatMovement);

    // Re-entry into the SAME mode still fires exit then enter (Sec4.3's
    // "exit and enter fire even when old mode = new mode").
    calls.clear();
    m.transition(mode::CombatMovement, false, 0);
    CHECK(calls.size() == 2);
    CHECK(calls[0] == "combat-exit" && calls[1] == "combat-enter");

    // +0x44c bits 0/1 are cleared on every transition (Sec3.3 step 4).
    m.state.actionFlags = ModeState::kActionFinished | ModeState::kReEvaluateRequested | ModeState::kOrdered;
    m.transition(mode::Idle, false, 0);
    CHECK((m.state.actionFlags & ModeState::kActionFinished) == 0);
    CHECK((m.state.actionFlags & ModeState::kReEvaluateRequested) == 0);
    CHECK((m.state.actionFlags & ModeState::kOrdered) != 0);  // untouched
}

void testApplyDispatch() {
    // Generic group: apply runs the transition primitive unconditionally (Sec3.2).
    {
        ModeMachine m;
        bool entered = false;
        m.hooks[mode::CombatMovement].onEnter = [&]() { entered = true; };
        CHECK(m.request(mode::CombatMovement, 0));
        m.apply(0);
        CHECK(entered);
        CHECK(m.state.currentMode == mode::CombatMovement);
        CHECK(!m.state.switchPending);
    }
    // Ignored group: request consumed, no mode change (Sec3.2).
    {
        ModeMachine m;
        CHECK(m.request(mode::Aim, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
        CHECK(!m.state.switchPending);
    }
    // Begin group, Player: enters only when locally owned and is-player (Sec3.2).
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.isPlayer = true;
        CHECK(m.request(mode::Player, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Player);
    }
    {
        ModeMachine m;
        m.beginInputs.isPlayer = false;
        CHECK(m.request(mode::Player, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);  // declined
    }
    // Begin group, Stunned: declines for a player or if already stunned (Sec3.2).
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.isPlayer = false;
        CHECK(m.request(mode::Stunned, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Stunned);
    }
    // Apply refuses at apply time even if request-time accepted (Sec3.2 step 2):
    // simulate by making the acceptance state change between request and apply.
    {
        ModeMachine m;
        CHECK(m.request(mode::Idle, 0));
        m.acceptance.healthState = 5;  // now only Stunned is accepted
        m.apply(0);
        CHECK(!m.state.switchPending);       // dropped
        CHECK(m.state.currentMode == mode::kNone);  // never transitioned
    }
    // "Veh threat" falls through into "Dbno"'s begin (Sec3.2), REGARDLESS
    // of whether Veh threat's own call (0x00523db0, Sec13.1.7) itself
    // succeeds or declines - here it declines (no target resolved) but
    // Dbno is still entered.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.isDown = true;
        m.beginInputs.vehThreatTargetResolves = false;  // Veh threat's own call declines
        CHECK(m.request(mode::VehThreat, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Dbno);  // the fall-through entered Dbno, not VehThreat
    }
    // As of unit A5 (Sec13.1), all 13 begin-group mode ids have a dedicated
    // implementation - see testA5BeginFunctions() below for Cower, Flee,
    // Gunfire wary, Passenger, RollerBlader, Veh evade and Veh threat's own
    // call. The beginHooks fallback (and its OpenStateError) is therefore
    // not reachable from any real mode id any more; it remains only as a
    // defensive seam for a future spec unit adding a 14th begin-group mode.
}


}  // namespace airtest

using namespace airtest;

int main() {
    testModeTableFillCounts();
    testAcceptanceTest();
    testRequestLayer();
    testTransitionPrimitive();
    testApplyDispatch();
    testA5BeginFunctions();
    testEventPump();
    testFinishAndOrderSeam();

    testBuildStateMaskAndPrecondition();
    testLiveTargetAndGate();
    testGoalTestsSample();
    testChooserFallbackAndKeep();
    testDecisionStepIntegration();

    testNullRoutineGuard();
    testNonPureRoutineDiscipline();
    testActionRoutineSample();
    testThrowTrajectoryOracle();

    testPerceptionListMechanics();

    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_ai_runtime_test: all checks passed\n";
    return 0;
}
