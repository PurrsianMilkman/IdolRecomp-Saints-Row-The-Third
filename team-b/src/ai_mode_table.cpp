#include "sr3airuntime/mode_table.h"

#include <algorithm>

namespace sr3airuntime {

bool IsVehicleCompatibleMode(int32_t m) {
    for (int32_t v : kVehicleCompatibleModes) {
        if (v == m) return true;
    }
    return false;
}

EventDisposition DispositionFromHandlerResult(int32_t result) {
    switch (result) {
        case 1: return EventDisposition::Consumed;
        case 2: return EventDisposition::Rejected;
        case 3: return EventDisposition::Kept;
        case 4: return EventDisposition::KeptRearmed;
        default: return EventDisposition::Consumed;  // "any other value" -> consumed (Sec2.3)
    }
}

// --- Sec3.4: acceptance test 0x004de110(human, mode) -----------------------------
bool AcceptanceTest(const ModeState& st, int32_t requestedMode, const AcceptanceInputs& in) {
    // 1. Stunned lock.
    if (st.currentMode == mode::Stunned && st.subState == 9) {
        return requestedMode == mode::Panic && in.stunnedLockForceFlagSet;
    }
    // 2. Captured or locked character.
    if (in.capturedOrLocked) {
        return requestedMode == mode::Stunned;
    }
    // 3. Health/state field +0xcc8.
    if (in.healthState == 5 || in.healthState == 6) {
        return requestedMode == mode::Stunned;
    }
    if (in.healthState == 7) {
        return requestedMode == mode::HumanShield || requestedMode == mode::Panic ||
               requestedMode == mode::Stunned;
    }
    // 4. In a vehicle: only the nine vehicle-compatible modes.
    if (in.inVehicle) {
        return IsVehicleCompatibleMode(requestedMode);
    }
    // 5. On foot.
    if (requestedMode == mode::Passenger) return false;  // only accepted while in a vehicle (handled above)
    if (requestedMode == mode::Reload && st.currentMode == mode::Flee) return false;
    return true;
}

// --- Sec3.1: request ---------------------------------------------------------------
bool ModeMachine::request(int32_t requestedMode, int64_t nowMs) {
    if (requestedMode < 0 || requestedMode >= mode::kCount) return false;
    if (state.switchPending && state.requestedMode == mode::Stunned) return false;  // cannot be overridden
    if (!AcceptanceTest(state, requestedMode, acceptance)) return false;

    if (requestedMode == mode::Idle) {
        state.bit4a6_4_cowerForced = false;
        state.secondaryHandle458 = 0;
    }
    clearPendingTargetHandle();

    state.switchPending = true;
    state.actionFlags &= ~(ModeState::kActionFinished | ModeState::kReEvaluateRequested);
    state.requestedMode = requestedMode;
    // Step 5's 0x008addb0(human, 0) call is a dead call (Sec3.1); omitted.
    (void)nowMs;
    return true;
}

void ModeMachine::clearPendingTargetHandle() {
    state.hasPendingTargetHandle = false;
    state.pendingTargetHandle = 0;
    // "If the object is owned elsewhere, sends a network record to the
    // owner instead" (Sec3.1 step 3) - out of scope (single-player model;
    // see AcceptanceInputs' own note on locally-owned assumptions).
}

// --- Sec3.3: transition primitive 0x004dd380 --------------------------------------
void ModeMachine::transition(int32_t newMode, bool hasBlock, int64_t nowMs) {
    (void)hasBlock;  // the 0x90-byte live mode parameter block is mode CONTENT, not modelled (see ModeState comment)
    (void)nowMs;
    if (state.currentMode != mode::kNone) {
        ModeHooks& h = hooks[state.currentMode];
        if (h.onExit) h.onExit(newMode);
    }
    state.previousMode = state.currentMode;
    state.currentMode = newMode;
    state.subState = mode::kSubStateNone;
    state.justEntered = true;
    state.actionFlags &= ~(ModeState::kActionFinished | ModeState::kReEvaluateRequested);
    if (newMode != mode::kNone) {
        ModeHooks& h = hooks[newMode];
        if (h.onEnter) h.onEnter();
    }
}

// --- Sec3.2: apply ------------------------------------------------------------------
void ModeMachine::apply(int64_t nowMs) {
    if (!state.switchPending) return;
    if (!AcceptanceTest(state, state.requestedMode, acceptance)) {
        state.switchPending = false;
        return;
    }
    state.switchPending = false;
    state.switchInProgress = true;

    const int32_t target = state.requestedMode;
    const ModeRow& row = kModeTable[target];
    switch (row.applyGroup) {
        case ApplyGroup::Generic:
            // "If locally owned: transition primitive. If not owned: send a
            // remote mode request instead" (Sec3.2) - this project models
            // only the locally-owned path (see AcceptanceInputs' note).
            transition(target, false, nowMs);
            break;
        case ApplyGroup::Begin: {
            if (target == mode::VehThreat) {
                // "Veh threat" falls through into the "Dbno" case
                // unconditionally (Sec3.2's fall-through note, reconfirmed
                // by Sec13.1.7: "apply's case ALWAYS falls through into the
                // Dbno begin afterward regardless of whether this call runs
                // at all"). Veh threat's own call 0x00523db0 is now fully
                // implemented (Sec13.1.7); its return value is discarded
                // here exactly as apply discards it in the original -
                // only its side effect (a real transition into VehThreat,
                // on its own accept path) matters, and the Dbno
                // fall-through always runs regardless.
                runVehThreatOwnBegin(nowMs);
                runDbnoBegin(nowMs);
            } else if (target == mode::Cower) {
                // Sec13.1.1's apply-side bracket: see cowerPauseResumeHook's
                // own doc comment.
                if (state.bit4a6_1_cantFleeToggle && cowerPauseResumeHook) cowerPauseResumeHook(false);
                runBeginFunction(target, nowMs);
                if (state.bit4a6_1_cantFleeToggle && cowerPauseResumeHook) cowerPauseResumeHook(true);
            } else {
                runBeginFunction(target, nowMs);
            }
            break;
        }
        case ApplyGroup::Ignored:
            // "Nothing. The request is consumed with no mode change." (Sec3.2)
            break;
    }

    state.switchInProgress = false;
    clearPendingTargetHandle();
}

bool ModeMachine::runBeginFunction(int32_t m, int64_t nowMs) {
    switch (m) {
        case mode::Idle: return runIdleBegin(nowMs);
        case mode::Player: return runPlayerBegin(nowMs);
        case mode::Stunned: return runStunnedBegin(nowMs);
        case mode::HumanShield: return runHumanShieldBegin(nowMs);
        case mode::Panic: return runPanicBegin(nowMs);
        case mode::Dbno: return runDbnoBegin(nowMs);
        case mode::Cower: return runCowerBegin(nowMs);
        case mode::Flee: return runFleeBegin(nowMs);
        case mode::GunfireWary: return runGunfireWaryBegin(nowMs);
        case mode::Passenger: return runPassengerBegin(nowMs);
        case mode::RollerBlader: return runRollerBladerBegin(nowMs);
        case mode::VehEvade: return runVehEvadeBegin(nowMs);
        // mode::VehThreat is special-cased in apply() itself (it always
        // also runs Dbno's begin afterward) and never reaches this switch.
        default:
            if (beginHooks[m]) return beginHooks[m]();
            throw OpenStateError(std::string("begin function for mode '") + kModeTable[m].name + "'",
                                  "spec-ai-runtime.md Sec3.2 (begin-function table gives only the call "
                                  "signature for this mode, not its internal gating)");
    }
}

// "Idle" begin 0x00512f90(human, has_point, point_handle) (Sec3.2).
bool ModeMachine::runIdleBegin(int64_t nowMs) {
    if (beginInputs.humanUnusable) return false;
    if (!AcceptanceTest(state, mode::Idle, acceptance)) return false;
    if (beginInputs.isPlayer) return runBeginFunction(mode::Player, nowMs);          // "enters Player instead"
    if (acceptance.inVehicle) return runBeginFunction(mode::Passenger, nowMs);       // "enters Passenger instead"
    if (!beginInputs.isLocallyOwned) return false;  // proxy: forwarded as message 0x1b, no local entry
    if (beginInputs.vehicleReentrySucceeded) {
        // "tries vehicle re-entry (0x00521640/0x00521700)" - its outcome on
        // success is not detailed beyond "failing that, it ... enters
        // Idle", so a success path has no described result here.
        throw OpenStateError("Idle begin 0x00512f90: vehicle re-entry success path (0x00521640/0x00521700)",
                              "spec-ai-runtime.md Sec3.2 (Idle begin; outcome on success not detailed)");
    }
    if (beginInputs.hasPoint) {
        state.hasIdleSeedPoint = true;
        state.idleSeedPoint = beginInputs.point;
    }
    transition(mode::Idle, false, nowMs);
    return true;
}

// "Player" begin 0x004f13a0 (Sec3.2): "enters 43 only if locally owned and
// 0x00853b30(human, 0x21)".
bool ModeMachine::runPlayerBegin(int64_t nowMs) {
    if (!beginInputs.isLocallyOwned || !beginInputs.isPlayer) return false;
    transition(mode::Player, false, nowMs);
    return true;
}

// "Stunned" begin 0x0050bdc0 (Sec3.2): "enters 34 (null block) unless the
// human is a player or already stunned, and only if its own handle is
// locally owned (0x008ae480)". "Already stunned" is read as the current
// mode already being Stunned - the spec names no other flag for it.
bool ModeMachine::runStunnedBegin(int64_t nowMs) {
    if (!beginInputs.isLocallyOwned) return false;
    if (beginInputs.isPlayer) return false;
    if (state.currentMode == mode::Stunned) return false;
    transition(mode::Stunned, false, nowMs);
    return true;
}

// "Human shield" begin 0x0050ec80 (Sec3.2): "it resolves a victim from
// +0x458 or picks one (0x009660e0), validates it (0x009a9830), stores its
// handle in +0x458, and enters 21." The picking algorithm itself is not
// detailed, so the host supplies the already-resolved candidate; "validates
// it" is read (JUDGMENT CALL) as a gate: an invalid candidate declines
// rather than entering with a bad handle, since the spec never says what
// entering with no valid victim would mean.
bool ModeMachine::runHumanShieldBegin(int64_t nowMs) {
    if (!beginInputs.humanShieldVictimValid) return false;
    state.secondaryHandle458 = beginInputs.humanShieldVictimHandle;
    transition(mode::HumanShield, false, nowMs);
    return true;
}

// "Panic" begin 0x004efaf0 (Sec3.2): always ends up calling 0x004ef450,
// which "initialises Panic's block ... and enters 24" - no refusal
// condition is given. For the local player, reason = the saved global
// 0x012e1b84, then reset to -1. Otherwise "it derives a panic reason code
// 0, 1, 2, 3 or 5 from several tests" that the spec does not enumerate; a
// host that has not resolved that derivation gets OpenStateError rather
// than a guessed reason (the MODE ENTRY itself is confirmed regardless, but
// this unit cannot commit a fabricated reason code).
bool ModeMachine::runPanicBegin(int64_t nowMs) {
    int32_t reason;
    if (beginInputs.panicIsLocalPlayer) {
        reason = panicSavedGlobalReason;
        panicSavedGlobalReason = -1;
    } else {
        if (!beginInputs.panicDerivedReasonProvided) {
            throw OpenStateError("Panic begin 0x004efaf0: non-local-player reason derivation",
                                  "spec-ai-runtime.md Sec3.2 (Panic begin; \"several tests\" not enumerated)");
        }
        reason = beginInputs.panicDerivedReason;
    }
    state.panicReason = reason;
    transition(mode::Panic, false, nowMs);
    return true;
}

// "Dbno" begin 0x0050bd40, read via the "Veh threat" fall-through paragraph
// (Sec3.2): "enters Dbno only if the human's handle is locally owned and
// 0x009715d0(human) holds."
bool ModeMachine::runDbnoBegin(int64_t nowMs) {
    if (!beginInputs.isLocallyOwned || !beginInputs.isDown) return false;
    transition(mode::Dbno, false, nowMs);
    return true;
}

// =====================================================================
// Unit A5 (spec-ai-runtime.md Sec13.1): the 7 begin functions that unit
// resolved, replacing their former OpenStateError stubs. Every numbered
// step below cites its own Sec13.1.x subsection; "shared preamble" is
// Sec13.1.0. Each begin's own BeginFunctionInputs fields are grouped and
// commented in mode_table.h next to this same citation.
// =====================================================================

// "Cower" begin 0x004d5050(human, target_lo, target_hi, flag5, flag6,
// flag7, flag0) (Sec13.1.1). runCowerBegin is apply's own dispatch target
// (it reads the 4 flag bits apply itself unpacked from +0x4a5/+0x4a6 at
// its own call site, per Sec13.1.1's push survey); runCowerBeginCore does
// the real work and is also called a SECOND time by Flee's own redirect
// (Sec13.1.2 step2) with all 4 forced to 0, bypassing the state-bit unpack.
bool ModeMachine::runCowerBegin(int64_t nowMs) {
    return runCowerBeginCore(state.bit4a5_5_cowerFlag5, state.bit4a5_6_cowerFlag6,
                              state.bit4a5_7_cowerFlag7, state.bit4a6_0_cowerFlag0, nowMs);
}

bool ModeMachine::runCowerBeginCore(bool flag5, bool flag6, bool flag7, bool flag0, int64_t nowMs) {
    // Shared preamble (Sec13.1.0): null-check is moot in this per-instance
    // model; the class gate is item 2.
    if (!beginInputs.classAllowsScriptedModeEntry) return false;
    // Step 2: not owned -> network-forward (0x1b, target handle via
    // 0x0086f4b0, the 4 flags via four 0x004d46e0 calls) and decline - this
    // project models only the locally-owned path, matching every other
    // begin's identical precedent (e.g. runIdleBegin's own comment).
    if (!beginInputs.isLocallyOwned) return false;
    // Step 3.
    if (!beginInputs.cowerUsable) return false;
    // Step 4.
    if (!beginInputs.targetReferenceResolves && !beginInputs.forceAlwaysCower) return false;
    // Step 5: skipped entirely when cant_flee (+0x2ba bit5) is set.
    if (!beginInputs.forceCantFlee) {
        if (!beginInputs.cowerCanApproachTarget || !beginInputs.helper0x00a00560) return false;
    }
    // Step 6: already in Cower - refresh the stored target handle only; no
    // re-transition.
    if (state.currentMode == mode::Cower) {
        state.cowerStoredTargetHandle = state.pendingTargetHandle;  // +0x8d8/+0x8dc <- target_lo/target_hi
        if (state.subState == 0x54) {
            // 0x0094b070 (animation/pose helper) - mode CONTENT, skipped.
            setSubState(mode::Cower, 0x53);
        }
        return true;
    }
    // Step 7: real entry. The packed flags byte is embedded in the (not
    // modelled - see ModeState's own comment) +0x8d0 live parameter block,
    // so it has no further observable effect in this port; it is still
    // computed (reading the OPEN item1 OpenValue below) so that a fresh
    // Cower entry genuinely depends on the host having resolved it, per
    // this project's no-invented-fixes rule, rather than silently skipping
    // a byte the spec flags as a real gap.
    uint8_t flags = 0;
    if (beginInputs.cowerFlagsByteBit0.get()) flags |= 0x01;  // OPEN item1
    if (flag6 || beginInputs.flagE8Bit0x200) flags |= 0x02;
    if (flag5) flags |= 0x04;
    if (flag7) flags |= 0x10;
    if (flag0) flags |= 0x20;
    (void)flags;
    transition(mode::Cower, true, nowMs);
    return true;
}

// "Flee" - both bodies, selected by apply via state.bit4a6_2_fleeVariant
// (Sec13.1.2). runFleeBegin is apply's own dispatch target.
bool ModeMachine::runFleeBegin(int64_t nowMs) {
    if (state.bit4a6_2_fleeVariant) {
        // Bit set: apply itself pre-computes escape_point via
        // 0x00da38e0(&human+0x474, &escape_point) and calls 0x004d91a0
        // directly - out-of-scope geometry, modelled as a host fact.
        return runFleeWithPoint(beginInputs.fleeHasEscapePoint, beginInputs.fleeEscapePoint,
                                 beginInputs.fleeField4a0, nowMs);
    }
    return runFleeCandidateSearch(nowMs);
}

// 0x004d91a0(human, target_lo, target_hi, &escape_point, field_0x4a0).
bool ModeMachine::runFleeWithPoint(bool hasPoint, const Vec3& point, uint8_t field4a0, int64_t nowMs) {
    if (!beginInputs.classAllowsScriptedModeEntry) return false;
    if (!beginInputs.isLocallyOwned) {
        // Not owned: calls the shared replication helper 0x004d7b80 (mode
        // CONTENT, skipped) and returns TRUE UNCONDITIONALLY, never
        // transitioning - the one shortcut of this shape in this unit.
        return true;
    }
    if (!beginInputs.fleeCanApproachTarget) return false;
    // Step 4's reference-point resolve only matters, for THIS function, as
    // the input to step 5's Cower-check below; that check's own result is
    // injected directly (beginInputs.fleeTooExposedToBreakCover), so the
    // actual coordinates are not separately needed here.
    if (state.currentMode == mode::Cower && beginInputs.fleeTooExposedToBreakCover) {
        return true;  // absorbed into staying in Cower - no transition
    }
    // Step 6: build the escape point and transition. The further geometry
    // (0x004d6980/0x004d7fa0/navmesh/0x004e5d20) is out of scope; the
    // destination is simply the escape_point this function was given.
    // JUDGMENT CALL: a null escape_point reaching this point (not exercised
    // by either call site this unit models - apply's own call always
    // resolves one, and the candidate search below always passes a real
    // candidate) is treated as nothing to flee toward, since the text gives
    // no behaviour for that case.
    if (!hasPoint) return false;
    (void)point;
    (void)field4a0;  // destination/flags-byte content - unmodelled (transition()'s own "hasBlock is a no-op" convention)
    transition(mode::Flee, true, nowMs);
    return true;
}

// 0x004da0b0(human, target_lo, target_hi, field_0x4a0): the candidate-
// search dispatcher. CONFIRMED structure per Sec13.1.2; the three
// candidates' own geometry resolution is injected as host facts (the
// project's established geometry seam - actions.h's own SCOPE note), since
// none of 0x00bc0800/0x00bbfbe0/0x00dab810/0x004d4bf0/0x004d7e20/0x00b71950
// is a confirmed closed-form formula this unit can compute directly.
bool ModeMachine::runFleeCandidateSearch(int64_t nowMs) {
    if (!beginInputs.classAllowsScriptedModeEntry) return false;
    if (!beginInputs.isLocallyOwned) return true;  // same 0x004d7b80 shortcut, mirroring 0x004d91a0's own not-owned path ("as above")
    if (!beginInputs.fleeCanApproachTarget) return false;

    // Step 1: already-fleeing-same-target short-circuit.
    if (state.currentMode == mode::Flee && beginInputs.fleeAlreadyFleeingSameTargetArmed) {
        return true;  // no re-transition
    }

    // Step 2 / the first candidate-search bullet (Sec13.1.2 explicitly
    // confirms these are "the same branch, not two independent features"):
    // if already Cower, the exposure check alone decides; otherwise, a
    // failing near-cover probe is what gates an attempt at the Cower
    // redirect (requiring 0x004eaa40/cowerUsable too, per the text's own
    // "and 0x004eaa40(human) holds").
    if (state.currentMode == mode::Cower) {
        if (beginInputs.fleeTooExposedToBreakCover) return true;
    } else {
        const bool nearCoverApplies = beginInputs.fleeNearCoverGateBits && ((beginInputs.fleeField4a0 & 0x40) == 0);
        if (nearCoverApplies && beginInputs.fleeNearCoverProbeFails && beginInputs.cowerUsable) {
            if (runCowerBeginCore(false, false, false, false, nowMs)) {
                // "A second call ... takes the 'already Cower' fast path:
                // just refresh the target handle" (Sec13.1.2 step2).
                runCowerBeginCore(false, false, false, false, nowMs);
                return true;
            }
        }
    }

    // The Idle-only decline bullet ("along the way", Sec13.1.2 step3's last
    // bullet). JUDGMENT CALL: the prose does not pin down its exact
    // position relative to the three candidates; placed here (after the
    // short-circuits, before the remaining two candidates) as a
    // backoff/cooldown-style check that would naturally run before
    // attempting the more expensive geometry candidates below.
    if (state.currentMode == mode::Idle && (beginInputs.fleeField4a0 & 0x08) == 0 && !beginInputs.flagE8Bit0x200 &&
        beginInputs.fleeIdleInsufficientTimePassed) {
        return false;
    }

    // Candidate: normalized direction - if valid, delegates to 0x004d91a0 directly.
    if (beginInputs.fleeNormalizedDirectionValid) {
        if (runFleeWithPoint(true, beginInputs.fleeNormalizedDirectionPoint,
                              static_cast<uint8_t>(beginInputs.fleeField4a0 | 0x20), nowMs)) {
            return true;
        }
    }

    // Candidate: longer path-search - on success, builds its OWN point and
    // transitions directly (not via 0x004d91a0).
    if (beginInputs.fleePathSearchSucceeded) {
        transition(mode::Flee, true, nowMs);
        return true;
    }

    return false;
}

// "Gunfire wary" - 0x004e8070(human, 0) then 0x004dbd90(human, target)
// (Sec13.1.3). 0x004e8070 NEVER itself transitions into mode 17 (the
// important correction Sec13.1.3 makes to the call-signature-only
// framing); it is a general-purpose reaction pre-hook (also the exact
// second confirmed call site Passenger's own begin uses, Sec13.1.4 step4)
// that stages OTHER requests. The real mode-17 entry is entirely inside
// 0x004dbd90, run only if a target resolved.
void ModeMachine::runGunfireWaryPrehook(int64_t nowMs) {
    if (!beginInputs.isLocallyOwned) {
        // The "second, always-false-here" ownership re-check (Sec13.1.3
        // step1) is dead code on this call path, per the spec's own note -
        // not modelled. Falls through to a telemetry/debug log (0x004f5dd0
        // family, HYPOTHESIS, not followed) - no mode change.
        return;
    }
    if (beginInputs.isPlayer) {
        // "Decline checks mirroring the acceptance test almost exactly"
        // (Stunned-lock, captured, +0xcc8 in {5,6,7}, the Sec3.4 in-vehicle
        // whitelist for mode 0x2b=43=Player) is exactly this unit's own
        // AcceptanceTest for mode::Player, plus one extra guard the text
        // names separately (the pending-switch-to-Stunned guard).
        const bool pendingSwitchToStunned = state.switchPending && state.requestedMode == mode::Stunned;
        if (!AcceptanceTest(state, mode::Player, acceptance) || pendingSwitchToStunned) return;
        // Directly writes the pending-request fields apply itself reads
        // (the exact same fields Idle's own null-handle clear and a normal
        // request() touch).
        clearPendingTargetHandle();
        state.switchPending = true;
        state.actionFlags &= ~(ModeState::kActionFinished | ModeState::kReEvaluateRequested);
        state.requestedMode = mode::Player;
        // "calls 0x008addb0(human,0) once more and ignores the result" - no
        // observable effect, not modelled.
        return;
    }
    // Non-player branch: weapon draw / threat resolution / several checks
    // (0x0097cdd0+0x1c9a bit0, +0x9b0 vs a tunable, 0x009bd730, 0x00943ea0)
    // are reaction/perception CONTENT this unit's own text gives no further
    // detail on beyond "runs several checks" - not modelled. Only the two
    // OBSERVABLE mode-request outcomes the text does name are implemented:
    if (beginInputs.gunfireWaryPrehookInVehicle) {
        request(mode::Passenger, nowMs);
    } else if (beginInputs.forceAlwaysCower) {
        request(mode::Cower, nowMs);
        // 0x004dd990(human,0,0,0,0,0) - mode CONTENT side effect, skipped.
    }
    // else: "checks 0x004f3a90/0x004f50b0" - HYPOTHESIS, no further text on
    // what they would do; no action taken (no invented fixes).
}

// 0x004dbd90(human, target) - the real mode-17 entry (Sec13.1.3).
bool ModeMachine::runGunfireWaryEntry(int64_t nowMs) {
    if (!beginInputs.gunfireWaryTargetValid) return false;  // null/self checks
    if (!beginInputs.classAllowsScriptedModeEntry) return false;
    if (!beginInputs.isLocallyOwned) return false;  // network-forward (tag 0x11), no local entry
    if (beginInputs.gunfireWaryIsFollower) return false;
    if (acceptance.inVehicle) return false;
    if (beginInputs.forceIgnoreAttacks || beginInputs.forceCombatDisabled) return false;
    if (beginInputs.pairedGate0x008c01b0) {
        if (beginInputs.pairedHandler0x008c0240) beginInputs.pairedHandler0x008c0240();
        return false;
    }
    // Build a watch-stance point / read the target's position - geometry,
    // unmodelled.
    transition(mode::GunfireWary, true, nowMs);
    return true;
}

bool ModeMachine::runGunfireWaryBegin(int64_t nowMs) {
    runGunfireWaryPrehook(nowMs);
    if (!beginInputs.gunfireWaryTargetValid) return false;  // "called second and only if a target resolved"
    return runGunfireWaryEntry(nowMs);
}

// "Passenger" begin 0x0051ed00(human) (Sec13.1.4).
bool ModeMachine::runPassengerBegin(int64_t nowMs) {
    // Step 1: action-level pre-check, ahead of the usual preamble.
    if (state.currentAction == 0x32) return false;  // 50 = "vehicle extract" (decision.h/A1's action::VehicleExtract)
    // Step 2: a player redirects into Player's own begin outright.
    if (beginInputs.isPlayer) return runPlayerBegin(nowMs);
    if (!beginInputs.classAllowsScriptedModeEntry) return false;
    // Step 3: ownership OR the single-player session flag.
    if (!beginInputs.isLocallyOwned && !beginInputs.singlePlayerSessionFlag) return false;
    // Step 4: real gate.
    if (!beginInputs.passengerSeatResolves) {
        runGunfireWaryPrehook(nowMs);  // side effect only - the second confirmed call site for 0x004e8070
        return false;
    }
    // Step 5.
    state.currentAction = 0x30;            // 48 = "vehicle passenger"
    state.committedBlock = ActionBlock{};  // reset from the fixed global template (Sec4.1's own template-reset convention)
    // A one-bit flags byte from an elapsed-time check (tunable 0x012a2ff0)
    // is unmodelled content (transition()'s own "hasBlock is a no-op"
    // convention).
    transition(mode::Passenger, true, nowMs);
    return true;
}

// "RollerBlader" begin 0x00530530(human, target) (Sec13.1.5) - the
// simplest of the 7. JUDGMENT CALL: Sec13.1.0's opening sentence claims a
// shared class gate for "every one of the 7", but this mode's own specific
// listing (Sec13.1.5) enumerates only 2 gates total and explicitly notes it
// skips the network-forward convention "entirely" - read here as a
// complete, exhaustive 2-gate function, following the specific listing
// over the general opening sentence.
bool ModeMachine::runRollerBladerBegin(int64_t nowMs) {
    if (!beginInputs.isLocallyOwned) return false;  // 0x008ae480 - no network-forward fallback (the one exception among the 7)
    if (!beginInputs.rollerBladerDescriptorBit) return false;
    // 0x009659f0(human, target) - side effect only, return value discarded
    // ("snap to/attach to a skate path", HYPOTHESIS).
    // Parameter block: four -1 sentinel dwords and a zero word - no target
    // handle, no flag byte; unmodelled content.
    transition(mode::RollerBlader, true, nowMs);
    return true;
}

// "Veh evade" begin 0x005232a0(human, [human+0x458], [human+0x45c]) (Sec13.1.6).
bool ModeMachine::runVehEvadeBegin(int64_t nowMs) {
    if (!beginInputs.classAllowsScriptedModeEntry) return false;
    if (!beginInputs.isLocallyOwned) return false;  // network-forward (tag 0x27), no local entry
    if (!beginInputs.vehEvadeTargetResolves) return false;
    if (acceptance.inVehicle) return false;  // either variant, 0x009b9160 or 0x009b91e0
    if (beginInputs.helper0x00a00560) return false;  // same raw fact as Cower's gate; OPPOSITE polarity here (declines when it holds)
    if (beginInputs.enteringVehicle0x009b9210) return false;
    if (beginInputs.flagE8Bit0x1000) return false;
    if (beginInputs.isBruteSubtype0x009422d0) return false;
    if (beginInputs.pairedGate0x008c01b0) {
        if (beginInputs.pairedHandler0x008c0240) beginInputs.pairedHandler0x008c0240();
        return false;
    }
    if (beginInputs.vehEvadeGameModeSwitchIsTwo && beginInputs.helper0x0097e9b0) return false;
    // Step 3: escape point / FOV tunable / navmesh query - geometry, out of
    // scope beyond its own confirmed pass/fail outcome.
    if (!beginInputs.vehEvadeNavmeshQuerySucceeds) return false;
    // Step 4: current-mode/substate gate.
    if (state.currentMode == mode::Idle) {
        if (state.subState == 0x26) return false;
        if (beginInputs.flagE8Bit0x200) {
            // 0x00511010(human,1) - mode CONTENT side effect, skipped.
            return false;
        }
        if (!beginInputs.vehEvadeIdleTimerElapsed) {
            // "arm a randomized back-off (0x00d9e180, 3000ms/1500ms
            // jitter)" - this unit has no per-mode-begin backoff-timer
            // mechanism (only A1's actionRepeatTimers models a backoff, at
            // a different layer); the decline itself is modelled, the
            // backoff arming is not.
            return false;
        }
    }
    // Step 5: flags byte / position overrides - unmodelled content.
    // Step 6 (OPEN item2): the manual rep movsd into +0x8d0 ahead of the
    // transition primitive's own copy is not separately modelled -
    // transition() already treats the +0x8d0 block as "mode content, not
    // modelled" (its own doc comment), so the "duplicate write or not"
    // question has no separate observable effect in this port either way.
    transition(mode::VehEvade, true, nowMs);
    return true;
}

// "Veh threat"'s own call 0x00523db0(human, target_lo, target_hi, 1)
// (Sec13.1.7). apply's own call site always passes the 4th argument as the
// literal 1; the 4th-argument-zero branch the listing also supports is
// exercised only from other, unexamined call sites (Sec13.1.7's own note)
// and is not implemented here. apply() always ALSO runs Dbno's begin
// afterward regardless of this function's own result (unchanged from
// pre-A5; see apply()'s own VehThreat special case above).
bool ModeMachine::runVehThreatOwnBegin(int64_t nowMs) {
    if (!beginInputs.classAllowsScriptedModeEntry) return false;
    if (!beginInputs.isLocallyOwned) return false;  // network-forward (tag 0x28), no local entry
    if (beginInputs.enteringVehicle0x009b9210) return false;
    // OPEN item 3's possession/camera gate - see beginInputs' own doc
    // comment (BeginFunctionInputs::vehThreatPossessionGateDeclines).
    if (beginInputs.vehThreatPossessionGateDeclines) return false;
    if (!beginInputs.vehThreatTargetResolves) return false;
    // Step 6, 4th-argument-nonzero branch (apply's own call: arg4 is always 1).
    if (!beginInputs.vehThreatRelevancePredicate) return false;
    if (!beginInputs.vehThreatHelper0x004e9110) return false;
    if (!beginInputs.vehThreatHelper0x009d8ca0) return false;
    // Packs a 2-bit flags byte, writes the target handle - unmodelled content.
    transition(mode::VehThreat, true, nowMs);
    return true;
}

// --- Sec2.5: sub-state setter -------------------------------------------------------
bool ModeMachine::setSubState(int32_t expectedMode, uint8_t subState) {
    if (state.currentMode != expectedMode) return false;
    state.subState = subState;
    // The notifiers (0x0094f720/0x0094f890/0x0094b070, Sec2.5) are mode
    // CONTENT side effects, out of scope for this unit.
    return true;
}

// --- Sec2.3: event pump --------------------------------------------------------------
void ModeMachine::pumpEvents(bool flagOnly, int64_t nowMs) {
    std::vector<AiEvent> kept;
    kept.reserve(state.events.size());
    for (AiEvent& ev : state.events) {
        if (flagOnly || ev.deliverAtMs > nowMs) {
            kept.push_back(ev);  // "left as is" (not yet due, or a flag-only pass)
            continue;
        }
        const bool modeMatches = (ev.modeFilter == mode::kNone) || (ev.modeFilter == state.currentMode);
        const bool subStateMatches = (ev.subStateFilter == 0xb2) || (ev.subStateFilter == state.subState);
        if (!modeMatches || !subStateMatches) {
            continue;  // consumed: "aimed at a mode the human has already left"
        }

        EventDisposition d = EventDisposition::Rejected;  // 0x004ec4d0's "not locally owned / no mode / unusable" -> 2
        if (state.currentMode != mode::kNone) {
            ModeHooks& h = hooks[state.currentMode];
            if (h.onEvent) d = h.onEvent(ev);
            // An unset slot would fall to the default handler 0x004ebc70
            // (Sec8 item 3), which is reactive-mode CONTENT this unit does
            // not implement; modelled as Rejected, matching "a zero result
            // from the default handler becomes 2" for the common case of
            // "nothing reacts".
        }

        switch (d) {
            case EventDisposition::Kept:
                kept.push_back(ev);
                break;
            case EventDisposition::KeptRearmed:
                ev.deliverAtMs = nowMs + randomRangeMs(250, 750);
                kept.push_back(ev);
                break;
            case EventDisposition::Consumed:
            case EventDisposition::Rejected:
                break;
        }
    }
    state.events = std::move(kept);
}

void ModeMachine::postEvent(AiEvent ev, int64_t delayMs, int64_t nowMs) {
    if (delayMs < 0) delayMs = randomRangeMs(250, 750);
    ev.postedAtMs = nowMs;
    ev.deliverAtMs = nowMs + delayMs;
    state.staging.push_back(std::move(ev));
}

void ModeMachine::deliverStagedEvents() {
    for (AiEvent& ev : state.staging) state.events.push_back(std::move(ev));
    state.staging.clear();
}

// --- Sec4.1: finish routine 0x004f4f50 ----------------------------------------------
bool ModeMachine::finishAction(const FinishInputs& in, int64_t nowMs) {
    const uint8_t a = state.currentAction;
    // Step 1 (the per-action cool-down roll) lives in A1 (HumanAiState),
    // which owns the behaviour record and the +0x320+4*a timer array; A1's
    // wrapper performs that roll around this call.
    (void)a;

    state.actionFlags |= ModeState::kActionFinished;
    state.idleTimer.restart(nowMs, 0);

    bool orderedCompletionRan = false;
    if ((state.actionFlags & ModeState::kOrdered) != 0 && state.stagedBlock.actionId == a &&
        in.stagedActionCategoryMode == state.currentMode) {
        state.actionFlags &= ~(ModeState::kOrdered | ModeState::kOrderedForceRecheck);
        state.actionFlags |= ModeState::kOrderedCompleted;
        state.stagedBlock = ActionBlock{};
        orderedCompletionRan = true;
        // Completing the kind-3 id and waking a waiting script thread
        // (Sec4.1 step 3) are outside this unit (Lua/mission layer, A0's
        // OrderIdPool only stands in for the id's lifecycle - see
        // commitOrder/cancelOrder). The host does both when this returns
        // true.
    }

    state.currentAction = 0x47;
    state.committedBlock = ActionBlock{};
    return orderedCompletionRan;
}

// --- Sec4.4: the ordered-action seam -------------------------------------------------
uint32_t ModeMachine::OrderIdPool::allocate() {
    uint32_t id = next_++;
    open_.push_back(id);
    return id;
}
void ModeMachine::OrderIdPool::complete(uint32_t id) {
    open_.erase(std::remove(open_.begin(), open_.end(), id), open_.end());
}
void ModeMachine::OrderIdPool::cancel(uint32_t id) {
    open_.erase(std::remove(open_.begin(), open_.end(), id), open_.end());
}
bool ModeMachine::OrderIdPool::isOpen(uint32_t id) const {
    return std::find(open_.begin(), open_.end(), id) != open_.end();
}

void ModeMachine::commitOrder(uint8_t actionId, int32_t categoryMode, const ActionBlock& block, bool noId,
                               OrderIdPool& pool, int64_t nowMs) {
    // Step 1: write the action id into the staged block. This unit does not
    // model a separate earlier "stage" call, so the caller's whole block is
    // taken as the staged content (net effect: same observable state).
    state.stagedBlock = block;
    state.stagedBlock.actionId = actionId;

    if (!noId) {
        state.stagedRequestId = pool.allocate();
        state.stagedRequestIdValid = true;
        // +0x4fe bit 6 (Sec4.4) is mode/order CONTENT, not read by this unit.
    }
    // "If not locally owned, forwards to the owner (net sub-type 0) and
    // stops." - single-player model (see AcceptanceInputs' note).
    // Step 4: the precondition flags (0x004f3560/0x004f25c0, A1's tables)
    // "do not stop the commit" even on failure (Sec4.4) - so this function
    // never evaluates them; A1's caller may evaluate them for its own
    // purposes but must still call this unconditionally.

    state.committedBlock = state.stagedBlock;
    state.actionFlags &= ~(ModeState::kActionFinished | ModeState::kOrderedCompleted | ModeState::kOrderedForceRecheck);
    state.actionFlags |= ModeState::kOrdered;
    state.idleTimer.restart(nowMs, 0);
    state.commitTimer.restart(nowMs, 0);
    state.committedReason = 0x12;
    state.currentAction = actionId;

    request(categoryMode, nowMs);  // "ignoring whether the request is accepted" (Sec4.4)
}

void ModeMachine::cancelOrder(OrderIdPool& pool) {
    const bool staysAsFinishedCurrent = (state.stagedBlock.actionId == state.currentAction);
    state.actionFlags &= ~(ModeState::kOrderedCompleted | ModeState::kOrdered | ModeState::kOrderedForceRecheck);
    if (staysAsFinishedCurrent) state.actionFlags |= ModeState::kActionFinished;
    if (state.stagedRequestIdValid) {
        pool.cancel(state.stagedRequestId);
        state.stagedRequestIdValid = false;
    }
    state.stagedRequestId = 0;
}

}  // namespace sr3airuntime
