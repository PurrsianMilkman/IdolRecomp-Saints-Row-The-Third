// Synthetic tests for the AI runtime units A0-A3, spec-ai-runtime.md
// Sec2-Sec4/Sec6 (A0), Sec9 (A1), Sec10 (A2), Sec11 (A3). Every expected
// value below is a rule quoted from that spec (cited per block); world
// facts come from plain struct fields set directly in each test - injected
// test values, not data.
#include <iostream>
#include <string>

#include "sr3airuntime/actions.h"
#include "sr3airuntime/decision.h"
#include "sr3airuntime/mode_table.h"
#include "sr3airuntime/perception.h"
#include "sr3luahost/open_state.h"

using namespace sr3airuntime;
using sr3luahost::OpenStateError;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

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

// ===================================================================
// A2 - perception storage/producers sanity (Sec10), beyond what A1/A3
// tests above already exercise through real use.
// ===================================================================

void testPerceptionListMechanics() {
    PerceptionState p;
    // Capacity + score-ascending eviction (Sec10.1): list 8 has capacity 1.
    CHECK(p.postStimulus(plist::FriendlyFire, 1, {}, 50, 0, 1, 0, 0) == PostResult::Created);
    CHECK(p.postStimulus(plist::FriendlyFire, 2, {}, 80, 0, 1, 0, 0) == PostResult::Refused);  // worse, full
    CHECK(p.postStimulus(plist::FriendlyFire, 2, {}, 10, 0, 1, 0, 0) == PostResult::Created);  // better, evicts
    CHECK(p.entries(plist::FriendlyFire).size() == 1);
    CHECK(p.entries(plist::FriendlyFire)[0].handle == 2);

    // notice()/see() flag transitions (Sec10.3).
    p = PerceptionState{};
    CHECK(p.notice(10, {}, /*attitude=*/0, false, false, false, 0) == PostResult::Created);
    const PerceptionEntry* e = p.find(plist::KnownHostiles, 10);
    CHECK(e && (e->flags & pflag::ActivelyKnown) && !(e->flags & pflag::InView));
    CHECK(p.see(10, {}, 0, false, false, false, 100) == PostResult::Refreshed);
    e = p.find(plist::KnownHostiles, 10);
    CHECK(e && (e->flags & pflag::InView) && (e->flags & pflag::VisuallyConfirmedOnce));
    CHECK(p.hasEntry(plist::LastSightings, 10));  // "see" also writes list 1

    // Expiry (Sec10.2): list 2 has a 4000ms lifetime.
    p = PerceptionState{};
    p.postStimulus(plist::LineOfFire, 1, {}, 0, 0, 1, 0, 0);
    p.expireEntries(5000);
    CHECK(!p.hasEntry(plist::LineOfFire, 1));

    // The damage accumulator formula (Sec10.3's "the fade"): K=100, L=30000.
    p = PerceptionState{};
    PerceptionState::DamageProducerInput dmg;
    dmg.attackerHandle = 1;
    dmg.damage = 10;
    dmg.maxHealth = 100;
    dmg.attackerAttitude = 4;  // friendly-fire branch, avoids the notice-without-sight path
    p.recordDamage(dmg, 0);
    CHECK(p.damageAccumulator(0) > 0.09 && p.damageAccumulator(0) < 0.11);  // ~10% of max health
    CHECK(p.damageAccumulator(30000) < 1e-9);  // fully faded after 30s

    // The query (Sec10.7): priority by row order, then list order.
    p = PerceptionState{};
    p.postStimulus(plist::KnownHostiles, 1, {}, 0, 0, 1, 0, 0);
    p.postStimulus(plist::LastSightings, 2, {}, 0, 0, 1, 0, 0);
    std::vector<PerceptionState::QueryRow> rows;
    rows.push_back({plist::LastSightings, 0, -1, 0, 0, 0});
    rows.push_back({plist::KnownHostiles, 0, -1, 0, 0, 0});
    const PerceptionEntry* match = p.query(rows, 0);
    CHECK(match && match->handle == 2);  // first row (list 1) wins over the second (list 0)
}

}  // namespace

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
