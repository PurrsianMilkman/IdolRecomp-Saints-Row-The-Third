// sr3player - player runtime unit U2: normal-condition movement and the
// intent fields. spec-player-runtime.md Sec3 is the ONLY source; see
// include/sr3player/movement.h for scope, OPEN gates and CHOSEN readings.
#include "sr3player/movement.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace sr3player {

namespace {

const char* const kSecWriter = "spec-player-runtime.md Sec3.5.4";

// 0x004d6210 (Sec3.5.1 step 3): y (lane 1) zeroed by the caller, normalise
// x, y, z with w forced to 0; (1, 0, 0) when every component is within 1e-5
// of 0. CHOSEN boundary: "within" read as <=, as input.h's kZeroTolerance.
Vec3 FlattenNormalise(Vec3 row) {
    row.y = 0;
    if (std::fabs(row.x) <= 1e-5 && std::fabs(row.y) <= 1e-5 && std::fabs(row.z) <= 1e-5) return {1, 0, 0};
    return row * (1.0 / Length(row));
}

// 0x00db92d0 (Sec3.5.2 step 6): normalise a 2-vector; a zero vector
// becomes (1, 0). CHOSEN: "zero" read as exactly zero length.
void Normalise2(double& x, double& y) {
    const double l = std::sqrt(x * x + y * y);
    if (l == 0.0) {
        x = 1;
        y = 0;
        return;
    }
    x /= l;
    y /= l;
}

bool IEquals(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return i == a.size() && b[i] == 0;
}

}  // namespace

bool IsZero7(Vec3 v) {
    return std::fabs(v.x) <= 1e-7 && std::fabs(v.y) <= 1e-7 && std::fabs(v.z) <= 1e-7;
}

Vec3 NormaliseOrZero(Vec3 v) {
    if (IsZero7(v)) return {};
    return v * (1.0 / Length(v));
}

// --- tweaks ------------------------------------------------------------------------
int MoveTweaks::seedFromTweakRows(const std::vector<std::pair<std::string, double>>& rows) {
    struct Named {
        const char* name;
        OpenValue<double>* v;
    };
    // Registry names exactly as spec-player-runtime.md Sec3.5.4 / Sec3.6.3
    // quote them. The boosted sprint 0x0263ae34 has no registered name.
    const Named table[] = {
        {"Player_max_run_speed", &maxRunSpeed},
        {"Player_max_sprint_speed", &maxSprintSpeed},
        {"Player_max_heavy_weapon_sprint_speed", &maxHeavyWeaponSprintSpeed},
        {"Player_max_heavy_weapon_speed", &maxHeavyWeaponSpeed},
        {"Player_max_avatar_move_speed", &maxAvatarMoveSpeed},
        {"Player_max_swimming_speed", &maxSwimmingSpeed},
        {"Player_max_fine_aim_speed", &maxFineAimSpeed},
        {"Player_max_riot_move_speed", &maxRiotMoveSpeed},
        {"Player_skydive_move_speed", &skydiveMoveSpeed},
        {"Player_rappel_move_speed", &rappelMoveSpeed},
        {"Player_crouch_move_speed", &crouchMoveSpeed},
        {"Player_acceleration", &acceleration},
        {"Player_dive_roll_acceleration", &diveRollAcceleration},
        {"Crouch_click_sensitivity", &crouchClickSensitivity},
    };
    int seeded = 0;
    for (const auto& row : rows) {
        for (const auto& n : table) {
            if (IEquals(row.first, n.name)) {
                // Later rows win, as the loader applies rows in file order
                // (each row is a plain store into the destination).
                n.v->set(row.second);
                ++seeded;
            }
        }
    }
    return seeded;
}

// --- common case ---------------------------------------------------------------------
void SeedOnFootCommonCase(MovementWorld& w) {
    w.objectReady33.set(true);
    w.state346.set(false);
    w.enteringVehicle.set(false);
    w.dead.set(false);
    w.scriptControlled.set(false);
    w.flagE8_100000.set(false);
    w.missionType1a.set(false);
    w.inCover.set(false);
    w.coverMode18d4.set(0);
    w.pred9a9550.set(false);
    w.controllerState285.set(false);
    w.ragdolled.set(false);
    w.aiSubState500.set(0);
    w.aiMode501.set(43);  // `Player` (Sec3.4), i.e. not Idle
    w.drunkEffectInactive.set(true);
    // OPEN 22: the image value of 0x012ec730 (0) WOULD invert both axes
    // until the mission-16 reset runs; the common case assumes it has run.
    w.mission16InvertActive.set(false);
    w.activityInvertActive.set(false);
    w.moveToObjectHandleSet.set(false);
    w.forcedTargetPoint.set(Vec3{});
    w.diving.set(false);
    w.cceIs3.set(false);
    w.ccd.set(0);
    w.climbingOrVaulting.set(false);
    w.jumping.set(false);
    w.controllerFlag10.set(false);
    w.controllerFlag4.set(false);
    w.controllerFlag800.set(false);
    w.controllerFlag100.set(false);
    w.sprintAnimStateActive.set(false);
    w.momentumState1d80.set(false);
    w.momentumState1d8f.set(false);
    w.state1d50Active.set(false);
    w.startMoveState2f9Active.set(false);
    w.heavyWeapon.set(false);
    w.avatar.set(false);
    w.swimming.set(false);
    w.sprintUpgrade.set(false);
    w.riotMode.set(false);
    w.skydiving.set(false);
    w.rappelling.set(false);
    w.movementFlagsD40.set(0);
    w.flagE0_1000.set(false);
    w.accelShaping9573d0.set(false);
    w.supportedByGround.set(true);
    w.flagE8_20.set(false);
    w.combatReady.set(false);
    w.facingOverrideActive.set(false);
    w.sprintSystemEnabled.set(true);
    w.stateTest985d80.set(false);
    w.canMoveOnGround.set(true);
    w.flagE8_100.set(false);
    w.pred9ba300.set(false);
    w.crouchPossible.set(true);
    w.riding.set(false);
    w.swimStop9b5830.set(false);
    w.keepState9b5600.set(false);
    w.onMovingSupport.set(false);
    w.jumpUpgrade.set(false);
    w.flagEc800.set(false);
    w.cceIs27.set(false);
    w.animationDrivesVertical.set(false);
    w.freeFall.set(false);
}

// --- input adapter ---------------------------------------------------------------------
int InputSystemMoveSource::cba(MoveAction a) const {
    switch (a) {
        case MoveAction::Sprint: return kCbaOfcSprint;
        case MoveAction::SprintToggle: return kCbaOfcSprintToggle;
        case MoveAction::Walk: return kCbaOfcWalk;
        case MoveAction::WalkToggle: return walkToggleCba.get();
        case MoveAction::Crouch: return kCbaOfcCrouch;
        case MoveAction::CrouchToggle: return crouchToggleCba.get();
    }
    throw std::logic_error("unknown MoveAction");
}

bool InputSystemMoveSource::optionBit0() {
    if (!in_.optionFlagBits0to3)
        throw OpenStateError("option byte 0x01411b00 bit 0 (set by the first binding application)",
                             "spec-player-runtime.md Sec1.7.1 / Sec3.6.3");
    return (*in_.optionFlagBits0to3 & 1u) != 0;
}

// --- helpers ------------------------------------------------------------------------------
void PlayerMovement::playerGaveInput(double blend) {
    ++playerGaveInputCalls;
    if (onPlayerGaveInput) onPlayerGaveInput(blend);
}

// 0x009fef60: its one unconditional, CONFIRMED effect on the player is
// "always clears +0x1d94 bit 0x10" (Sec3.3); the rest is the stamina's.
void PlayerMovement::notSprintingStep() {
    st.b1d94 &= static_cast<uint8_t>(~stance::kSprint);
    stamina_.notSprintingStep(st);
}

// --- crouch (Sec3.6.3) ---------------------------------------------------------------------
void PlayerMovement::crouchRoutine(const MoveFrameInput&) {
    using namespace stance;
    const auto standUp = [&] { st.crouched = false; };  // 0x00988610
    const auto crouchIfPossible = [&] {                  // 0x00986810 / 0x00987730
        if (!st.crouched && world.crouchPossible.get()) st.crouched = true;
    };

    const bool runs = (world.permissions2078.get() & 0x2000u) != 0 &&
                      !in_.held(MoveAction::Sprint, nullptr) && (st.b1d96 & kSprintLatch) == 0;
    if (!runs) {
        if (st.crouched) standUp();
        return;
    }

    // Step 1: queries, in the spec's order.
    int o1 = kOutNone, o2 = kOutNone;
    const bool togglePressed = in_.pressed(MoveAction::CrouchToggle, &o1);
    const bool crouchHeld = in_.held(MoveAction::Crouch, &o2);
    const bool crouchPressed = in_.pressed(MoveAction::Crouch, nullptr);
    const bool kb = o1 == kOutKeyOrMouse || o2 == kOutKeyOrMouse;

    if (crouchHeld && (kb || !in_.optionBit0())) {
        // Step 2: hold mode (the first-use hint 0x007169e0(0x52) is UI).
        crouchIfPossible();
        st.b1d96 &= static_cast<uint8_t>(~kCrouchLatch);
        in_.consume(MoveAction::Crouch);
    } else if ((crouchPressed && !kb && in_.optionBit0()) || (togglePressed && kb)) {
        // Step 3: toggle mode.
        st.b1d96 ^= kCrouchLatch;
        const bool on = (st.b1d96 & kCrouchLatch) != 0;
        if (on && !st.crouched) {
            bool allowed = world.crouchStickTestBypass != 0;
            if (!allowed) {
                // CHOSEN: the spec writes "A^2 + B^2" without saying which
                // copy; the walk axes are read fresh (the U0 queries are pure
                // functions of the latched frame, so this equals the move
                // slot's unmodified read). Never reached with the image
                // value 0x012e6ced = 1.
                const double A = in_.walkAxisA(), B = in_.walkAxisB();
                allowed = A * A + B * B < tweaks.crouchClickSensitivity.get();
            }
            if (allowed) crouchIfPossible();
        } else if (!on && st.crouched) {
            standUp();
        }
        // Re-synchronise the latch to the actual crouched state.
        if (st.crouched) st.b1d96 |= kCrouchLatch;
        else st.b1d96 &= static_cast<uint8_t>(~kCrouchLatch);
        in_.consume(MoveAction::Crouch);
        in_.consume(MoveAction::CrouchToggle);
    } else if (st.crouched && (st.b1d96 & kCrouchLatch) == 0) {
        // Step 4: hold released.
        standUp();
    }
}

// --- walk 0x005ab860 (Sec3.6.2) ------------------------------------------------------------
void PlayerMovement::walkRoutine() {
    using namespace stance;
    st.b1d94 &= static_cast<uint8_t>(~kWalk);
    const bool gate = (world.permissions2078.get() & 0x4000u) != 0 && !in_.held(MoveAction::Sprint, nullptr) &&
                      !world.readMovementDisabled() && !world.controllerFlag10.get();
    if (!gate) {
        st.b1d96 &= static_cast<uint8_t>(~kWalkLatch);
        return;
    }
    if (in_.held(MoveAction::Walk, nullptr)) {
        st.b1d94 |= kWalk;
        st.b1d96 &= static_cast<uint8_t>(~(kWalkLatch | kSprintLatch));
        return;
    }
    // CHOSEN reading: the latch flips on the press, and bit 0x20 is set on
    // EVERY frame the latch is set - bit 0x20 is cleared at the top of
    // every frame, so a toggle could not otherwise hold walk.
    if (in_.pressed(MoveAction::WalkToggle, nullptr)) st.b1d96 ^= kWalkLatch;
    if (st.b1d96 & kWalkLatch) {
        st.b1d94 |= kWalk;
        st.b1d96 &= static_cast<uint8_t>(~kSprintLatch);
    }
}

// --- sprint 0x005ab930 (Sec3.6.2) -----------------------------------------------------------
void PlayerMovement::sprintRoutine(const MoveFrameInput& f) {
    using namespace stance;
    const int64_t now = f.nowMs;

    // Step 1.
    walkRoutine();
    st.b1d96 &= static_cast<uint8_t>(~kSprintWanted);
    const bool prevSprint = sprintBit();
    st.b1d94 &= static_cast<uint8_t>(~kSprint);
    if (!world.sprintSystemEnabled.get()) {
        notSprintingStep();
        return;
    }
    const auto restorePrevSprint = [&] {
        if (prevSprint) st.b1d94 |= kSprint;
    };

    // Step 2.
    bool togglePressed = in_.pressed(MoveAction::SprintToggle, nullptr);
    bool sprintHeld = in_.held(MoveAction::Sprint, nullptr);
    bool sprintPressed = in_.pressed(MoveAction::Sprint, nullptr);
    const bool walkHeld = in_.held(MoveAction::Walk, nullptr);

    // Step 3: keyboard idle test.
    const bool pad = in_.gamepadActive();
    bool keysIdle = false;
    if (!pad) {
        const float A = in_.walkAxisA(), B = in_.walkAxisB();
        keysIdle = WithinZero(A) && WithinZero(B);
        if (keysIdle && g.keyboardSprintGrace.unsetOrExpired(now)) {
            st.b1d96 &= static_cast<uint8_t>(~kSprintLatch);
            togglePressed = sprintHeld = sprintPressed = false;
        }
    }

    // Step 4. CHOSEN reading of the sentence order: walk-held clears the
    // latch first, then held sprint wins, else the toggle flips the latch.
    const uint32_t mask = world.permissions2078.get();
    bool want = false;
    if (walkHeld) st.b1d96 &= static_cast<uint8_t>(~kSprintLatch);
    if (sprintHeld) {
        want = true;
        st.b1d96 &= static_cast<uint8_t>(~kSprintLatch);
    } else {
        if (togglePressed && (mask & 0x4000u)) st.b1d96 ^= kSprintLatch;
        want = (st.b1d96 & kSprintLatch) != 0 || diving();
    }
    if (world.readMovementDisabled() || world.controllerFlag10.get()) want = false;

    // Step 6 (also reached from step 5 under 0x009a1dc0).
    const auto step6 = [&] {
        if (world.stateTest985d80.get()) {
            stamina_.step9ff270();
            restorePrevSprint();
        } else {
            notSprintingStep();
        }
    };

    if (want && (mask & 0x4000u)) {
        // Step 5.
        st.b1d94 &= static_cast<uint8_t>(~kWalk);
        st.b1d96 &= static_cast<uint8_t>(~kWalkLatch);
        st.b1d96 |= kSprintWanted;
        // 0x009562b0 -> 0x0095acb0 (animation) and +0xe0 bit 0x10000000:
        // not modelled. CHOSEN reading of "leave scope view, or stand up
        // when crouched": the second only when not scoped.
        if (st.b1d95 & kScopeView) st.b1d95 &= static_cast<uint8_t>(~kScopeView);  // 0x009e6510(player, 0)
        else if (st.crouched) st.crouched = false;                                 // 0x00988610
        st.fineAim = false;                                                         // 0x009dcf10

        if (world.ragdolled.get()) {
            step6();
        } else if (world.stateTest985d80.get()) {
            stamina_.step9ff270();
            restorePrevSprint();
        } else {
            const bool ok = ((world.canMoveOnGround.get() || world.inCover.get()) && !world.flagE8_100.get()) || diving();
            if (!ok) {
                notSprintingStep();
            } else {
                const bool already = st.locomotionState == kLocoSprint;
                // CHOSEN: an UNSET cool-down counts as expired - nothing in
                // U2/U3 arms +0x2060, so the literal "expired" would make
                // sprint impossible.
                const bool canStart = already ||
                                      (!world.cceIs3.get() && stamina_.available() >= 0 &&
                                       st.sprintCooldown2060.unsetOrExpired(now) && !world.state1d50Active.get() &&
                                       !world.controllerFlag10.get());
                if (canStart) {
                    if (st.speedD1c > 4.0 || already || world.inCover.get() || world.heavyWeapon.get() ||
                        world.pred9ba300.get()) {
                        if (stamina_.sprintingStep(st)) {  // 0x009ffd00
                            st.b1d94 |= kSprint;
                            st.b1d95 &= static_cast<uint8_t>(~kPanting);
                        }
                        if (sprintPressed) stamina_.startPenalty();  // 0x009fffc0
                    } else {
                        st.b1d94 |= kBurst;  // burst from (near) standstill
                    }
                }
            }
        }
    } else {
        step6();
    }

    // Step 7. CHOSEN: "0x0098b970/0x00989670 hold" read as EITHER (the
    // jump handler leaves the player either jumping or climbing, Sec3.6.4).
    if (f.jumpedThisFrame && (world.jumping.get() || world.climbingOrVaulting.get())) stamina_.jumpPenalty();

    // Step 3's grace timer. CHOSEN: "sprinting" = the sprint bit just
    // derived (+0x1d94 bit 0x10).
    if (!pad && !keysIdle && sprintBit()) g.keyboardSprintGrace.arm(now, 300);
}

// --- axis read and conditioning 0x005ad130 (Sec3.5.2) --------------------------------------------
PlayerMovement::AxisResult PlayerMovement::readAxes(const MoveFrameInput& f) {
    AxisResult r;
    // Step 1: hard block 0x005ad050, lazily in the spec's order. "No local
    // player" cannot hold: this object IS the local player.
    const bool block = [&] {
        if (!world.objectReady33.get()) return true;
        if (world.state346.get()) return true;
        if (world.readControlsDisabled() || world.readMovementDisabled()) return true;
        if (world.enteringVehicle.get()) return true;
        if (world.dead.get()) return true;
        if (world.scriptControlled.get()) return true;
        if (world.flagE8_100000.get()) return true;
        if (world.missionType1a.get()) return true;
        if (world.inCover.get() && !(world.coverMode18d4.get() == 1 && !world.pred9a9550.get())) return true;
        if (world.controllerState285.get()) return true;
        if (world.ragdolled.get()) return true;
        if (world.aiSubState500.get() == 0x8c) return true;  // `turnaround`
        return false;
    }();

    if (block) {
        r.A = r.B = 0;
        r.stopped = r.centred = true;
        g.stoppedLatch = true;
        // -> step 8 (publication of step 7 is skipped)
    } else {
        // Step 2.
        float B = in_.walkAxisB();
        float A = in_.walkAxisA();
        // Step 3: add_camera_rotate is 0 on the live path.
        // Step 4: modifiers, in order.
        if (!world.drunkEffectInactive.get())
            throw OpenStateError("drunk/weed axis effect 0x009758b0 (internals OPEN 17)", "spec-player-runtime.md Sec3.5.2 step 4");
        if (world.mission16InvertActive.get()) {
            A = -A;
            B = -B;
        }
        if (world.activityInvertActive.get()) {
            A = -A;
            B = -B;
        }
        // Step 5: cover, dominant axis only (a tie keeps A).
        if (world.inCover.get() && !sprintBit() && world.coverMode18d4.get() == 1) {
            if (std::fabs(B) > std::fabs(A)) A = 0;
            else B = 0;
        }
        // Step 6: release detector.
        if (!(f.axisDt > 0)) throw std::invalid_argument("MoveFrameInput::axisDt (0x0132a0b4) must be > 0");
        double ar = (static_cast<double>(A) - g.prevA) / f.axisDt;
        double br = (static_cast<double>(B) - g.prevB) / f.axisDt;
        double nx = A, ny = B, mx = ar, my = br;
        Normalise2(nx, ny);
        Normalise2(mx, my);
        const double k = nx * mx + ny * my;
        g.releaseEnergy = static_cast<float>((k * ar) * (k * ar) + (k * br) * (k * br));
        // 0x00dad830(v, 0, 1e-4) is "|v| < 1e-4" (strict, as the spec states).
        r.centred = std::fabs(A) < 1e-4f && std::fabs(B) < 1e-4f;
        r.stopped = g.stoppedLatch ? r.centred : (r.centred && g.releaseEnergy < 1.0f);
        g.stoppedLatch = r.stopped;
        // Step 7: publish.
        g.publishedA = A;
        g.publishedB = B;
        g.prevA = A;
        g.prevB = B;
        r.A = A;
        r.B = B;
    }
    // Step 8 (the +0xe8 bit 0x80000 idle-interrupt branch is HYPOTHESIS and
    // not modelled; `moving` is not read by the move slot).
    if (r.centred) r.A = r.B = 0;
    return r;
}

// --- target point 0x005b31f0, flag = 1 (Sec3.5.3) -------------------------------------------------
Vec3 PlayerMovement::targetPoint(float B, float A, Vec3 F, Vec3 Rt) {
    if (world.moveToObjectHandleSet.get())
        throw OpenStateError("move-to-object target (+0x1a28 handle resolution)", "spec-player-runtime.md Sec3.5.3");
    const Vec3 forced = world.forcedTargetPoint.get();
    if (!IsZero7(forced)) return forced;
    // Step 1-2.
    const Vec3 v = (A == 0 && B == 0) ? Vec3{} : F * static_cast<double>(B) + Rt * static_cast<double>(A);
    Vec3 u = NormaliseOrZero(v);
    // Step 3: flag = 1 from the move slot - no auto-steer.
    // Step 4.
    if (IsZero7(u) && (sprintBit() || diving())) u = world.orientation.get().forward;
    // Step 5: 5.0 is the double at 0x012a2f20.
    return st.position + u * 5.0;
}

// --- stance maximum speed 0x009dca30 (Sec3.5.4 step 5) -----------------------------------------
void PlayerMovement::stanceMaxSpeed(int64_t nowMs, double& M, double& M0) {
    const bool sp = sprintBit() && !world.sprintAnimStateActive.get();
    double v;
    if (world.heavyWeapon.get()) v = sp ? tweaks.maxHeavyWeaponSprintSpeed.get() : tweaks.maxHeavyWeaponSpeed.get();
    else if (world.avatar.get()) v = tweaks.maxAvatarMoveSpeed.get();
    else if (world.swimming.get()) v = tweaks.maxSwimmingSpeed.get();
    else if (sp) v = world.sprintUpgrade.get() ? tweaks.boostedSprintSpeed.get() : tweaks.maxSprintSpeed.get();
    else if (st.fineAim) v = tweaks.maxFineAimSpeed.get();
    else if (world.riotMode.get()) v = tweaks.maxRiotMoveSpeed.get();
    else if (world.skydiving.get()) v = tweaks.skydiveMoveSpeed.get();
    else if (world.rappelling.get()) v = tweaks.rappelMoveSpeed.get();
    else if (st.crouched) v = tweaks.crouchMoveSpeed.get();
    else v = tweaks.maxRunSpeed.get();

    // x0.8 (0x0130b0b0): backpedal bit 0x2000, or bits 0x20 and 0x1000.
    const uint32_t d40 = world.movementFlagsD40.get();
    if ((d40 & 0x2000u) || ((d40 & 0x20u) && (d40 & 0x1000u))) v *= 0.8;
    M0 = v;
    M = v;
    // Sprint ramp over +0x2058 (300.0 is the double at 0x01129698).
    const int64_t r = st.sprintRamp2058.remaining(nowMs);
    if (st.sprintRamp2058.set && r > 0) {
        const double S = world.sprintUpgrade.get() ? 30.0 : tweaks.maxSprintSpeed.get();
        const double t = static_cast<double>(r) / 300.0;
        M = (1.0 - t) * M0 + t * S;
    }
}

// --- the intent writer 0x005aee30 (Sec3.5.4) -------------------------------------------------------
void PlayerMovement::writeIntent(const MoveFrameInput& f, Vec3 target, double s) {
    using namespace stance;
    // Step 1.
    Vec3 d = target - st.position;
    // Step 2: burst.
    if ((st.b1d94 & kBurst) && IsZero7(d)) {
        d = world.orientation.get().forward;
        st.b1d94 &= static_cast<uint8_t>(~kBurst);
        s = 1.0;
    }
    // Step 3: animation lock.
    if (world.controllerFlag10.get()) {
        if (static_cast<float>(s) > 0.01f) st.b1d94 |= kMoveWhileLocked;
        d = {};
        s = 0;
        st.turnAngleA68 = 0;
    }
    // Step 4: timers +0x2068/+0x206c feed only step 13 - not kept.
    // Step 5.
    double M = 0, M0 = 0;
    stanceMaxSpeed(f.nowMs, M, M0);
    // Step 6: momentum states.
    const bool m1d80 = world.momentumState1d80.get();
    const bool momentum = m1d80 || world.momentumState1d8f.get();
    if (momentum) {
        if (s > 0.2) st.b1d95 |= kStartTurnOwed;
        if (s <= 1e-4 || IsZero7(d)) d = st.velocityD04;
        if (M0 != 0) s = std::max(s, st.speedD1c / M0);
        if (m1d80) s = 1.0;
    }
    // Step 7: scale.
    if (s > 1e-4 && !IsZero7(d)) {
        const Vec3 u = NormaliseOrZero(d);
        d = u * (std::clamp(s, 0.0, 1.0) * M);
    }
    // Step 8.
    st.desiredMove = d;
    // Step 9: reversal check ("sprinting" = +0xccc = 3, Sec3.2.2).
    Vec3 p = st.smoothedMove;
    if (st.locomotionState != kLocoSprint || world.controllerFlag800.get()) {
        if (!IsZero7(st.controllerVelocity1cec) &&
            Dot(NormaliseOrZero(st.controllerVelocity1cec), NormaliseOrZero(d)) < 0)
            p = {};
    }
    // Step 10: stopping.
    const bool wasStopping = (st.b1d97 & kStopping) != 0;
    st.b1d97 &= static_cast<uint8_t>(~kStopping);
    if (IsZero7(d) && (!IsZero7(p) || (wasStopping && world.flagE0_1000.get()))) st.b1d97 |= kStopping;
    // Step 11 (e feeds only step 13).
    const double delta = Length(d - p);
    // Steps 12-13: locomotion mode and animation requests - no-ops (Sec3.8 item 4).
    // Step 14: acceleration.
    double a = tweaks.acceleration.get();
    if (world.accelShaping9573d0.get())
        throw OpenStateError("0x009573d0 acceleration shaping (+0xa70/+0xa80, OPEN 13)", kSecWriter);
    if (world.movementFlagsD40.get() & 0x2000u) a *= 0.8;
    if (!world.supportedByGround.get()) a *= 0.5;
    else if (world.flagE8_20.get()) a *= 2.0;
    if (diving()) a = tweaks.diveRollAcceleration.get();
    // Step 15: approach.
    const double stepLen = a * (1.0 + 0.5 * delta) * f.dt;
    Vec3 r = p;
    if (delta > 0) {
        const auto approach = [&](double pi, double di) {
            const double diff = di - pi;
            const double move = stepLen * std::fabs(diff) / delta;
            if (move >= std::fabs(diff)) return di;  // no overshoot
            return pi + (diff > 0 ? move : -move);
        };
        r.x = approach(p.x, d.x);
        r.z = approach(p.z, d.z);
    }
    r.y = d.y;
    // Step 16: sprint style (+0x1d97 bits 0x10/0x20, timer +0x205c) - animation only, not kept.
    // Step 17.
    if (momentum) r = d;
    // Step 18.
    st.smoothedMove = r;
    st.moveTargetCb0 = st.position + r;  // 0x00987360
    if (world.ccd.get() == 0) world.ccd.set(1);
    const double len = Length(r);
    st.desiredSpeedA60 = std::min(len, M);
    if (len > 0) st.smoothedMove = r * (st.desiredSpeedA60 / len);
    // Step 19: facing = +0xcb0 - position = r (Sec5.4.4).
    const Vec3 fa = st.moveTargetCb0 - st.position;
    if (!IsZero7(fa)) {
        st.facingA50 = NormaliseOrZero(fa);
    } else {
        st.facingA50 = fa;
        if (st.fineAim)
            throw OpenStateError("fine-aim facing fallback (camera mode 1 / facing axis, U4)", kSecWriter);
        if ((st.b1d97 & kStopping) && world.combatReady.get())
            throw OpenStateError("stopping/combat-ready facing q (needs +0xa68, step 20 not modelled)", kSecWriter);
    }
    // The facing override (+0xa6c/+0xa7c/+0xa70/+0xa80) is read on every
    // path, so a set override never silently loses its effect.
    if (world.facingOverrideActive.get())
        throw OpenStateError("facing override +0xa6c/+0xa7c/+0xa70/+0xa80 (OPEN 13)", kSecWriter);
    // Step 20: turn angle +0xa68 (HIGH) - not modelled.
}

// --- the move slot 0x005b3e80 (Sec3.5.1) -------------------------------------------------------------
void PlayerMovement::moveSlot(const MoveFrameInput& f) {
    // Step 1: +0xa20 is dormant in the shipped build (Sec3.5.6).
    // Step 2.
    const AxisResult ax = readAxes(f);
    // Step 3 (the state-5 override is unreachable in shipped play).
    const Vec3 F = FlattenNormalise(f.camera.row2);
    const Vec3 Rt = FlattenNormalise(f.camera.row0);
    // Step 4: stale-intent gate.
    if (world.enteringVehicle.get() || world.aiMode501.get() == 0 || world.aiSubState500.get() == 0x8b) return;
    // Step 5.
    g.targetPoint = targetPoint(ax.B, ax.A, F, Rt);
    // Step 6.
    if (ax.B != 0 || ax.A != 0) playerGaveInput(0.3);
    // Step 7: speed scalar.
    double s;
    if (sprintBit() || diving()) s = 1.0;
    else if (!in_.gamepadActive() && (st.b1d94 & stance::kWalk)) s = 0.3;  // 0x012e6d08
    else s = static_cast<double>(g.publishedA) * g.publishedA + static_cast<double>(g.publishedB) * g.publishedB;
    // Step 8.
    writeIntent(f, g.targetPoint, s);
    // Step 9.
    const bool sprint = sprintBit();
    const bool fstop = ax.stopped && !sprint && st.desiredSpeedA60 == 0.0;
    updateLocomotionState(f, sprint, fstop);
}

}  // namespace sr3player
