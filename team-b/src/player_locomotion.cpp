// sr3player - player runtime unit U3: the locomotion bridge.
// spec-player-runtime.md Sec5 is the ONLY source; see
// include/sr3player/movement.h for scope, OPEN gates and CHOSEN readings.
#include "sr3player/movement.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sr3player {

namespace {
const char* const kSecComposer = "spec-player-runtime.md Sec5.4.3";
}

// --- the locomotion-state update 0x00985df0(human, sprint, stopped, 1) (Sec5.5) ----------------
void PlayerMovement::updateLocomotionState(const MoveFrameInput& f, bool sprint, bool stopped) {
    // Step 1.
    if (world.climbingOrVaulting.get() || diving()) return;

    const int64_t now = f.nowMs;
    const int32_t cur = st.locomotionState;
    const bool sprinting = cur == kLocoSprint;

    // Step 2: sprint ramp +0x2058. Armed to 300 * min(1, (+0xd1c - 5)/3)
    // ms - re-armed every frame while sprinting with the request held, and
    // once when the request ends. CHOSEN: "the request ends" is detected
    // against the previous frame's raw request (prevSprintRequest, not a
    // field of the human); ms rounding is truncation toward zero.
    const bool rawRequest = sprint;
    if (sprinting && (rawRequest || st.prevSprintRequest)) {
        const double ms = 300.0 * std::min(1.0, (st.speedD1c - 5.0) / 3.0);
        st.sprintRamp2058.arm(now, static_cast<int64_t>(std::trunc(ms)));  // <= 0: already expired
    }
    st.prevSprintRequest = rawRequest;
    if (st.sprintRamp2058.set) {
        if (st.sprintRamp2058.running(now)) sprint = sprinting;  // true tail / false hold-off
        else st.sprintRamp2058.unset();
    }

    // Step 3: the new +0xccc.
    int32_t ns;
    if (world.riding.get()) ns = kLocoIdle;
    else if (world.swimming.get()) ns = world.swimStop9b5830.get() ? kLocoIdle : kLocoRun;
    else if (world.keepState9b5600.get()) ns = cur;
    else if (sprint) ns = kLocoSprint;
    else if (st.speedD1c > 2.5 && !st.fineAim && !world.inCover.get()) ns = kLocoRun;
    else ns = kLocoWalk;  // includes "a run in fine aim becomes 1"

    if (world.startMoveState2f9Active.get()) {
        // CHOSEN: "a run start" = a new state of 2 or 3.
        const bool runStart = ns == kLocoRun || ns == kLocoSprint;
        if (runStart && (st.b1d96 & stance::kSprintWanted)) {
            // ends state 0x2f9 (blend 0.3) - animation, no-op here
        } else if (!(world.startMoveState2f9Progress.get() > 0.55)) {
            ns = kLocoIdle;
        }
    }
    if (stopped && world.canMoveOnGround.get()) ns = kLocoIdle;  // (sprint stop 0x336/0x1d4c: animation)
    if (cur == kLocoIdle && ns != kLocoIdle) st.leaveIdleAa0.arm(now, 500);
    if (ns == kLocoSprint && cur != kLocoSprint) st.sprintStart2054.arm(now, 750);
    // (local player: 0x007f0140(1), 0x007059f0(5) - not modelled)

    // Step 4. (Ending 0x1d66 and the 0x309 blend are animation.)
    playerGaveInput(0.3);
    // 0x00942110(human, -1, state, +0xa60): condition kept, state stored,
    // a positive speed multiplied by +0xa64; -1 stored unscaled.
    st.locomotionState = ns;
    if (st.desiredSpeedA60 > 0) st.desiredSpeedA60 *= st.speedScaleA64;
    // Step 5: stat ticks - not modelled.
}

// --- the velocity composer 0x0099d1b0(human, 0), player branch (Sec5.4.3/Sec5.4.4) ---------------
void PlayerMovement::composeVelocity(const MoveFrameInput& f) {
    // Step 1: a pre-set velocity (+0xe8 bit 0x80000000 from the synced-pair
    // step, or +0xec bit 0x01) is not modelled, so +0x1cec is cleared.
    st.controllerVelocity1cec = {};
    // Step 2: +0xcd2 = 0 - the direct integrator IS the character proxy.
    // Step 3: root-motion read. T (locomotion clips) and X are animation-
    // side (OPEN 39); E is the frame input. The 0x009987d0 vertical flag is
    // folded into MovementWorld::animationDrivesVertical.
    Vec3 E = f.animationTranslationE;
    // Step 4.
    double s = st.desiredSpeedA60;
    Vec3 u = st.facingA50;
    if (s == -1.0)
        throw OpenStateError("+0xa60 = -1 'animation speed' sentinel (needs |T| and +0x1a90)", kSecComposer);
    // Step 5: choice of velocity, first match.
    Vec3 v;
    if (world.controllerFlag100.get()) {
        throw OpenStateError("root-motion-driven velocity R.T/dt (animation-controller flag 0x100; T is OPEN 39)",
                             kSecComposer);
    } else if (world.accelShaping9573d0.get()) {
        v = {};
    } else {
        if (world.controllerFlag4.get()) E = {};
        // 0x00941e60: +0xccc = 3 and not seated.
        if (st.locomotionState == kLocoSprint && !world.riding.get()) {
            u = world.orientation.get().forward;
        } else {
            const int32_t ccd = world.ccd.get();
            if (ccd == 2 || ccd == 3)
                throw OpenStateError("+0xccd = 2/3 routes the player through NPC shaping 0x009918b0 (OPEN 40; NPC path not in this pass)",
                                     kSecComposer);
        }
        v = u * s;
    }
    // Steps 6-7: lean and stride matching (+0x1a90) are animation-only.
    // Step 8.
    if (world.onMovingSupport.get())
        throw OpenStateError("moving support velocity +0x1c60 (+0x1c9c bit 0x2000)", kSecComposer);
    // Step 9: E <- R.E, upgraded jump. CHOSEN: skipped for an exactly-zero
    // E (R.0 = 0), so a caller with no root motion need not seed R.
    const bool eZero = E.x == 0 && E.y == 0 && E.z == 0;
    if (!eZero) {
        E = world.orientation.get().apply(E);
        if (world.sprintUpgrade.get() && world.jumpUpgrade.get() && !world.supportedByGround.get() &&
            world.jumping.get() && !world.flagEc800.get() && !world.cceIs27.get() &&
            in_.held(MoveAction::Sprint, nullptr))  // CBA_OFC_SPRINT level read at 0x0099e1af
            E = E * 1.5;                            // 0x0130a7b4
    }
    // Step 10.
    if (eZero) {
        st.controllerVelocity1cec = v;
    } else {
        if (!(f.dt > 0)) throw std::invalid_argument("MoveFrameInput::dt (0x0132a0ac) must be > 0 with a non-zero E");
        st.controllerVelocity1cec = v + E * (1.0 / f.dt);
    }
}

// --- the direct integrator (replaces Sec5.6.4-Sec5.6.5, licensed by Sec5.11 item 5) ---------------
void PlayerMovement::integrate(const MoveFrameInput& f) {
    capsuleStepped = false;
    // 0x0099e7e0 does nothing when dt is exactly 0 (Sec5.6.2).
    if (f.dt == 0) return;
    if (f.dt < 0) throw std::invalid_argument("MoveFrameInput::dt must be >= 0");
    // Gather (Sec5.6.3), player record.
    Vec3 vel = st.controllerVelocity1cec;
    if (world.swimming.get())
        throw OpenStateError("swimming velocity adjustment 0x009b5c50", "spec-player-runtime.md Sec5.6.3 (d)");
    if (!world.animationDrivesVertical.get() && !world.supportedByGround.get()) {
        // (e) airborne: the player's stick-scaled air control, applied to
        // all three components, A/B = the published walk axes.
        if (!world.freeFall.get() && !world.onMovingSupport.get()) {
            const double c = std::clamp(std::fabs(static_cast<double>(g.publishedA)) +
                                            std::fabs(static_cast<double>(g.publishedB)),
                                        0.0, 1.0);
            vel = vel * c;
        }
    }
    // The controller sets the capsule velocity ONCE from the record
    // (Sec5.9 item 2); no support query, slope removal, gravity or speed
    // cap - those are 0x007a5b10 internals (OPEN 35), not the contract.
    capsuleVelocity = vel;
    st.capsulePosition = st.capsulePosition + vel * f.dt;
    capsuleStepped = true;
}

// --- write-back (Sec5.6.6) and position sync (Sec5.7) ------------------------------------------------
void PlayerMovement::publish() {
    if (capsuleStepped) {
        // +0xd04/+0xd08/+0xd0c = the proxy velocity; +0xd1c = its length.
        st.velocityD04 = capsuleVelocity;
        st.speedD1c = Length(capsuleVelocity);
        // Landing, the 300/400/200 ms timers, knock-downs and the ground
        // probe need support/contact state this project does not have.
    }
    // 0x0096e710: position <- proxy when it differs by more than 0.0008 on any axis.
    const Vec3 dp = st.capsulePosition - st.position;
    if (std::fabs(dp.x) > 0.0008 || std::fabs(dp.y) > 0.0008 || std::fabs(dp.z) > 0.0008) st.position = st.capsulePosition;
}

// --- frame runners (Sec5.1, Sec5.9) ---------------------------------------------------------------
void PlayerMovement::runFrame(const MoveFrameInput& f) {
    if (traceEnabled) trace.clear();
    const auto mark = [&](FramePhase p) {
        if (traceEnabled) trace.push_back(p);
    };
    // Controls (state 2 per-frame 0x005b7430). The controls-disabled gate
    // early-outs with "no intent" and clears 0x0140e6f8, so neither the
    // action dispatch nor the move slot runs (Sec2.7).
    if (world.readControlsDisabled()) {
        mark(FramePhase::NoIntent);
        writeNoIntent(f);
    } else {
        mark(FramePhase::OnFootActions);
        crouchRoutine(f);  // Sec3.6.1 step 6
        sprintRoutine(f);  // Sec3.6.1 step 7
        if (f.controlsRanThisFrame) {
            mark(FramePhase::MoveSlot);
            moveSlot(f);
        }
    }
    // Bridge: compose once, before physics; step once; publish once.
    mark(FramePhase::Compose);
    composeVelocity(f);
    mark(FramePhase::Integrate);
    integrate(f);
    mark(FramePhase::Publish);
    publish();
}

void PlayerMovement::runLockedFrame(const MoveFrameInput& f) {
    if (traceEnabled) trace.clear();
    const auto mark = [&](FramePhase p) {
        if (traceEnabled) trace.push_back(p);
    };
    mark(FramePhase::NoIntent);
    writeNoIntent(f);
    updateLocomotionState(f, false, true);
    notSprintingStep();
    mark(FramePhase::Compose);
    composeVelocity(f);
    mark(FramePhase::Integrate);
    integrate(f);
    mark(FramePhase::Publish);
    publish();
}

}  // namespace sr3player
