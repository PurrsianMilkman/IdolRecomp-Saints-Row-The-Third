// Synthetic tests for the AI runtime units A0-A3, spec-ai-runtime.md
// Sec2-Sec4/Sec6 (A0), Sec9 (A1), Sec10 (A2), Sec11 (A3). Every expected
// value below is a rule quoted from that spec (cited per block); world
// facts come from plain struct fields set directly in each test - injected
// test values, not data.

#include "synthetic_ai_runtime_test_common.h"

namespace airtest {

// ===================================================================
// Unit A5 (Sec13.1): the 7 begin functions that unit resolved (Cower,
// Flee - both bodies, Gunfire wary, Passenger, RollerBlader, Veh evade,
// Veh threat's own call), replacing their former OpenStateError stubs.
// ===================================================================

void testA5BeginFunctions() {
    // --- Cower (Sec13.1.1) ---------------------------------------------
    // Decline: the shared class gate.
    {
        ModeMachine m;
        m.beginInputs.classAllowsScriptedModeEntry = false;
        CHECK(m.request(mode::Cower, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
    }
    // Decline: not locally owned.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = false;
        CHECK(m.request(mode::Cower, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
    }
    // A fresh, real transition requires OPEN item1 to be resolved first -
    // the "skip and revisit" discipline exercised for real.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.cowerUsable = true;
        m.beginInputs.forceAlwaysCower = true;  // step4: always_cower substitutes for a resolved target reference
        m.beginInputs.forceCantFlee = true;     // step5: skip the extra approach gate entirely
        CHECK(m.request(mode::Cower, 0));
        bool threw = false;
        try {
            m.apply(0);
        } catch (const OpenStateError&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(m.state.currentMode == mode::kNone);
    }
    // Same setup, OPEN item1 now resolved -> real transition.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.cowerUsable = true;
        m.beginInputs.forceAlwaysCower = true;
        m.beginInputs.forceCantFlee = true;
        m.beginInputs.cowerFlagsByteBit0.set(false);
        CHECK(m.request(mode::Cower, 0));
        m.state.pendingTargetHandle = 777;  // request() itself clears this; set it AFTER, as apply()'s own input
        m.apply(0);
        CHECK(m.state.currentMode == mode::Cower);
    }
    // Already-Cower fast path: refreshes the stored target handle, no
    // re-transition (Sec13.1.1 step6).
    {
        ModeMachine m;
        int enterCount = 0;
        m.hooks[mode::Cower].onEnter = [&]() { ++enterCount; };
        m.transition(mode::Cower, false, 0);  // seed: already in Cower
        CHECK(enterCount == 1);
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.cowerUsable = true;
        m.beginInputs.forceAlwaysCower = true;
        m.beginInputs.forceCantFlee = true;
        CHECK(m.request(mode::Cower, 0));
        m.state.pendingTargetHandle = 999;  // request() clears this; set it after
        m.apply(0);
        CHECK(m.state.currentMode == mode::Cower);
        CHECK(enterCount == 1);  // no re-transition -> onEnter did not fire again
        CHECK(m.state.cowerStoredTargetHandle == 999);
    }

    // --- Flee (Sec13.1.2) ----------------------------------------------
    // Bit set (direct 0x004d91a0 call), not owned: returns true internally
    // but never transitions (the one begin shaped this way).
    {
        ModeMachine m;
        m.state.bit4a6_2_fleeVariant = true;
        m.beginInputs.isLocallyOwned = false;
        CHECK(m.request(mode::Flee, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
    }
    // Bit set, owned, a real escape point: transitions into Flee.
    {
        ModeMachine m;
        m.state.bit4a6_2_fleeVariant = true;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.fleeCanApproachTarget = true;
        m.beginInputs.fleeHasEscapePoint = true;
        m.beginInputs.fleeEscapePoint = Vec3{1, 0, 0};
        CHECK(m.request(mode::Flee, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Flee);
    }
    // Bit set, already Cower and too exposed to break cover: absorbed,
    // stays in Cower.
    {
        ModeMachine m;
        m.transition(mode::Cower, false, 0);
        m.state.bit4a6_2_fleeVariant = true;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.fleeCanApproachTarget = true;
        m.beginInputs.fleeTooExposedToBreakCover = true;
        CHECK(m.request(mode::Flee, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Cower);
    }
    // Bit clear (0x004da0b0 candidate search): already-fleeing-same-target
    // short-circuit - no re-transition.
    {
        ModeMachine m;
        int enterCount = 0;
        m.hooks[mode::Flee].onEnter = [&]() { ++enterCount; };
        m.transition(mode::Flee, false, 0);
        CHECK(enterCount == 1);
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.fleeCanApproachTarget = true;
        m.beginInputs.fleeAlreadyFleeingSameTargetArmed = true;
        CHECK(m.request(mode::Flee, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Flee);
        CHECK(enterCount == 1);
    }
    // Bit clear: the near-cover-probe-fails branch redirects into Cower,
    // and the second call (the "already Cower" fast path) refreshes the
    // stored target handle - a concrete instance of "a begin function may
    // enter a different mode" (Sec3.3, reconfirmed by Sec13.1.2).
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.fleeCanApproachTarget = true;
        m.beginInputs.fleeNearCoverGateBits = true;
        m.beginInputs.fleeNearCoverProbeFails = true;
        m.beginInputs.cowerUsable = true;
        m.beginInputs.forceAlwaysCower = true;
        m.beginInputs.forceCantFlee = true;
        m.beginInputs.cowerFlagsByteBit0.set(false);
        CHECK(m.request(mode::Flee, 0));
        m.state.pendingTargetHandle = 42;  // request() clears this; set it after
        m.apply(0);
        CHECK(m.state.currentMode == mode::Cower);        // redirected, never reached Flee's own transition
        CHECK(m.state.cowerStoredTargetHandle == 42);      // the second call's fast-path refresh
    }
    // Bit clear: normalized-direction candidate succeeds directly via
    // 0x004d91a0.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.fleeCanApproachTarget = true;
        m.beginInputs.fleeNormalizedDirectionValid = true;
        m.beginInputs.fleeNormalizedDirectionPoint = Vec3{0, 0, 1};
        CHECK(m.request(mode::Flee, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Flee);
    }
    // Bit clear: the longer path-search candidate transitions directly.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.fleeCanApproachTarget = true;
        m.beginInputs.fleePathSearchSucceeded = true;
        CHECK(m.request(mode::Flee, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Flee);
    }
    // Bit clear, current mode Idle, the "insufficient time passed" bullet
    // declines outright.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.fleeCanApproachTarget = true;
        m.beginInputs.fleeIdleInsufficientTimePassed = true;
        CHECK(m.request(mode::Flee, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);  // still Idle/none - never entered Flee
    }

    // --- Gunfire wary (Sec13.1.3) ---------------------------------------
    // The prehook's (0x004e8070) own "in a vehicle" branch (-> requests
    // Passenger) is only reachable, self-consistently, through a call site
    // whose OWN mode accepts "in a vehicle" - Gunfire wary's own mode
    // acceptance (Sec3.4) requires NOT in a vehicle, so that branch cannot
    // fire through GunfireWary's own apply path at all (acceptance would
    // already have refused the outer request); it is exercised instead via
    // Passenger's own call site below ("the seat does not resolve"), the
    // other confirmed call site for this same pre-hook.
    //
    // The prehook's non-player, non-in-vehicle, force-flag branch: stages a
    // Cower request (never itself transitions mode17).
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.forceAlwaysCower = true;
        m.beginInputs.gunfireWaryTargetValid = false;  // 0x004dbd90 never runs - isolates the prehook's own effect
        CHECK(m.request(mode::GunfireWary, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);                               // never mode17
        CHECK(m.state.switchPending && m.state.requestedMode == mode::Cower);    // staged by the prehook
    }
    // The prehook's is-a-player branch stages a Player request directly.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.isPlayer = true;
        m.beginInputs.gunfireWaryTargetValid = false;
        CHECK(m.request(mode::GunfireWary, 0));
        m.apply(0);
        CHECK(m.state.switchPending && m.state.requestedMode == mode::Player);
    }
    // The real entry (0x004dbd90): accept path.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.gunfireWaryTargetValid = true;
        CHECK(m.request(mode::GunfireWary, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::GunfireWary);
    }
    // The real entry: the paired-gate decline runs its side-effect handler
    // then always declines.
    {
        ModeMachine m;
        bool handlerRan = false;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.gunfireWaryTargetValid = true;
        m.beginInputs.pairedGate0x008c01b0 = true;
        m.beginInputs.pairedHandler0x008c0240 = [&]() { handlerRan = true; };
        CHECK(m.request(mode::GunfireWary, 0));
        m.apply(0);
        CHECK(handlerRan);
        CHECK(m.state.currentMode == mode::kNone);
    }

    // --- Passenger (Sec13.1.4) ------------------------------------------
    // AcceptanceTest only accepts a Passenger REQUEST while already marked
    // "in a vehicle" (Sec3.4 step4's vehicle whitelist includes Passenger;
    // confirmed above by testAcceptanceTest's own "!AcceptanceTest(st,
    // mode::Passenger, in)" check with the default in-vehicle=false), so
    // every scenario below sets acceptance.inVehicle = true up front.
    //
    // Action-level pre-check declines outright.
    {
        ModeMachine m;
        m.acceptance.inVehicle = true;
        m.state.currentAction = 0x32;
        CHECK(m.request(mode::Passenger, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
    }
    // A player redirects into Player's own begin outright.
    {
        ModeMachine m;
        m.acceptance.inVehicle = true;
        m.beginInputs.isPlayer = true;
        m.beginInputs.isLocallyOwned = true;
        CHECK(m.request(mode::Passenger, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Player);
    }
    // Non-player, real gate resolves: a real transition, with the
    // current-action/committed-block side effects (Sec13.1.4 step5).
    {
        ModeMachine m;
        m.acceptance.inVehicle = true;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.passengerSeatResolves = true;
        CHECK(m.request(mode::Passenger, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Passenger);
        CHECK(m.state.currentAction == 0x30);
    }
    // The seat does not resolve: declines, but still runs 0x004e8070 as a
    // side effect (the second confirmed call site for that pre-hook) -
    // exercising its "in a vehicle" branch (-> requests Passenger again),
    // which is self-consistent here since this whole scenario is already
    // "in a vehicle".
    {
        ModeMachine m;
        m.acceptance.inVehicle = true;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.passengerSeatResolves = false;
        m.beginInputs.gunfireWaryPrehookInVehicle = true;  // makes the pre-hook's own side effect observable
        CHECK(m.request(mode::Passenger, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
        CHECK(m.state.switchPending && m.state.requestedMode == mode::Passenger);
    }

    // --- RollerBlader (Sec13.1.5) ----------------------------------------
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = false;
        CHECK(m.request(mode::RollerBlader, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
    }
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.rollerBladerDescriptorBit = false;
        CHECK(m.request(mode::RollerBlader, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
    }
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.rollerBladerDescriptorBit = true;
        CHECK(m.request(mode::RollerBlader, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::RollerBlader);
    }

    // --- Veh evade (Sec13.1.6) -------------------------------------------
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = false;
        CHECK(m.request(mode::VehEvade, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::kNone);
    }
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.vehEvadeTargetResolves = true;
        m.beginInputs.vehEvadeNavmeshQuerySucceeds = true;
        CHECK(m.request(mode::VehEvade, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::VehEvade);
    }
    // From Idle, with the substate decline (Sec13.1.6 step4).
    {
        ModeMachine m;
        m.transition(mode::Idle, false, 0);
        m.state.subState = 0x26;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.vehEvadeTargetResolves = true;
        m.beginInputs.vehEvadeNavmeshQuerySucceeds = true;
        CHECK(m.request(mode::VehEvade, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::Idle);  // declined, stayed Idle
    }
    // From Idle, the elapsed-time gate passes.
    {
        ModeMachine m;
        m.transition(mode::Idle, false, 0);
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.vehEvadeTargetResolves = true;
        m.beginInputs.vehEvadeNavmeshQuerySucceeds = true;
        m.beginInputs.vehEvadeIdleTimerElapsed = true;
        CHECK(m.request(mode::VehEvade, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::VehEvade);
    }

    // --- Veh threat's own call (Sec13.1.7) --------------------------------
    // A real accept, with Dbno's own fall-through gate declining (isDown
    // false) so Veh threat's own transition is left standing afterward.
    {
        ModeMachine m;
        m.beginInputs.isLocallyOwned = true;
        m.beginInputs.classAllowsScriptedModeEntry = true;
        m.beginInputs.isDown = false;  // Dbno's own begin declines, so it never overrides
        m.beginInputs.vehThreatTargetResolves = true;
        m.beginInputs.vehThreatRelevancePredicate = true;
        m.beginInputs.vehThreatHelper0x004e9110 = true;
        m.beginInputs.vehThreatHelper0x009d8ca0 = true;
        CHECK(m.request(mode::VehThreat, 0));
        m.apply(0);
        CHECK(m.state.currentMode == mode::VehThreat);
    }
}

void testEventPump() {
    ModeMachine m;
    int consumedCount = 0, keptRearmedCount = 0;
    m.hooks[mode::CombatMovement].onEvent = [&](AiEvent& ev) {
        if (ev.kind == 1) return EventDisposition::Consumed;
        if (ev.kind == 2) return EventDisposition::Kept;
        if (ev.kind == 3) {
            ++keptRearmedCount;
            return EventDisposition::KeptRearmed;
        }
        ++consumedCount;
        return EventDisposition::Rejected;
    };
    m.state.currentMode = mode::CombatMovement;

    AiEvent e1;
    e1.kind = 1;
    e1.deliverAtMs = 0;
    m.state.events.push_back(e1);
    AiEvent e2;
    e2.kind = 2;
    e2.deliverAtMs = 0;
    m.state.events.push_back(e2);
    AiEvent e3;
    e3.kind = 3;
    e3.deliverAtMs = 0;
    m.state.events.push_back(e3);

    m.pumpEvents(false, 1000);
    CHECK(m.state.events.size() == 2);  // kind 1 (Consumed) dropped
    bool foundKind2 = false, foundKind3 = false;
    for (auto& e : m.state.events) {
        if (e.kind == 2) foundKind2 = true;
        if (e.kind == 3) {
            foundKind3 = true;
            CHECK(e.deliverAtMs > 1000);  // re-armed 250-750ms out (Sec2.3)
        }
    }
    CHECK(foundKind2 && foundKind3);
    CHECK(keptRearmedCount == 1);

    // A record not yet due is left alone, not delivered (Sec2.3).
    m = ModeMachine{};
    m.state.currentMode = mode::CombatMovement;
    bool delivered = false;
    m.hooks[mode::CombatMovement].onEvent = [&](AiEvent&) {
        delivered = true;
        return EventDisposition::Consumed;
    };
    AiEvent future;
    future.kind = 1;
    future.deliverAtMs = 5000;
    m.state.events.push_back(future);
    m.pumpEvents(false, 1000);
    CHECK(!delivered);
    CHECK(m.state.events.size() == 1);

    // flagOnly delivers nothing (Sec1.4's "flag only" calls).
    m.pumpEvents(true, 9999);
    CHECK(!delivered);

    // Mode/sub-state filter mismatch -> consumed without delivery (Sec2.3).
    m = ModeMachine{};
    m.state.currentMode = mode::CombatMovement;
    m.state.subState = 5;
    delivered = false;
    m.hooks[mode::CombatMovement].onEvent = [&](AiEvent&) {
        delivered = true;
        return EventDisposition::Consumed;
    };
    AiEvent filtered;
    filtered.kind = 1;
    filtered.modeFilter = mode::Idle;  // not the current mode
    filtered.deliverAtMs = 0;
    m.state.events.push_back(filtered);
    m.pumpEvents(false, 1000);
    CHECK(!delivered);
    CHECK(m.state.events.empty());

    // postEvent + deliverStagedEvents (Sec10.5): staged this frame, live next.
    m = ModeMachine{};
    AiEvent staged;
    staged.kind = 7;
    m.postEvent(staged, 500, 1000);
    CHECK(m.state.staging.size() == 1 && m.state.events.empty());
    m.deliverStagedEvents();
    CHECK(m.state.staging.empty() && m.state.events.size() == 1);
    CHECK(m.state.events[0].deliverAtMs == 1500);
}

void testFinishAndOrderSeam() {
    ModeMachine m;
    m.state.currentAction = 5;
    m.state.actionFlags = 0;
    ModeMachine::FinishInputs in;
    in.stagedActionCategoryMode = mode::kNone;
    m.finishAction(in, 1000);
    CHECK((m.state.actionFlags & ModeState::kActionFinished) != 0);
    CHECK(m.state.currentAction == 0x47);
    CHECK(m.state.idleTimer.isArmed());

    // Ordered completion (Sec4.1 step 3): only when staged id == finished
    // action and the staged category == current mode.
    m = ModeMachine{};
    m.state.currentMode = mode::CombatMovement;
    m.state.currentAction = 14;
    m.state.actionFlags = ModeState::kOrdered;
    m.state.stagedBlock.actionId = 14;
    in.stagedActionCategoryMode = mode::CombatMovement;
    bool ranCompletion = m.finishAction(in, 0);
    CHECK(ranCompletion);
    CHECK((m.state.actionFlags & ModeState::kOrdered) == 0);
    CHECK((m.state.actionFlags & ModeState::kOrderedCompleted) != 0);

    // A mismatched category mode means the completion step does NOT run
    // (Sec4.1 step 3's precondition; Sec9.10 item 5's "re-issued" consequence).
    m = ModeMachine{};
    m.state.currentMode = mode::Flee;  // NOT the staged category
    m.state.currentAction = 14;
    m.state.actionFlags = ModeState::kOrdered;
    m.state.stagedBlock.actionId = 14;
    in.stagedActionCategoryMode = mode::CombatMovement;
    CHECK(!m.finishAction(in, 0));
    CHECK((m.state.actionFlags & ModeState::kOrdered) != 0);  // still staged

    // The ordered-action seam (Sec4.4).
    ModeMachine::OrderIdPool pool;
    m = ModeMachine{};
    ActionBlock block;
    block.actionId = 9;
    m.commitOrder(9, mode::CombatMovement, block, false, pool, 100);
    CHECK((m.state.actionFlags & ModeState::kOrdered) != 0);
    CHECK(m.state.currentAction == 9);
    CHECK(m.state.committedReason == 0x12);
    CHECK(m.state.stagedRequestIdValid);
    CHECK(pool.isOpen(m.state.stagedRequestId));
    CHECK(m.state.requestedMode == mode::CombatMovement);  // requested, even though apply hasn't run

    m.cancelOrder(pool);
    CHECK((m.state.actionFlags & ModeState::kOrdered) == 0);
    CHECK(!pool.isOpen(1));
    CHECK((m.state.actionFlags & ModeState::kActionFinished) != 0);  // staged==current -> finished set
}


}  // namespace airtest
