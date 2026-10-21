// sr3player/freefall.h - free-roam freefall + parachute for the local player.
//
// SPEC SOURCE (the ONLY source of engine facts): spec-player-runtime.md Sec19 "Freefall and parachute (free roam,
// local player) (2026-10-18)": 19.2 constants, 19.3 states, 19.4 start / deploy / cut-away / landing, 19.5 movement
// law, 19.6 test vectors (all 12 reproduced in tests/synthetic_freefall_test.cpp). Vertical motion uses the
// character fall law of Sec5.6.5 with the terminal speed T of 19.5 step 1.
//
// CHOSEN (spec silent / residual):
//   * Heading convention for the turn angle +0xa68 (= heading(camera) - heading(r) + bank): heading(x,z) =
//     atan2(x, z), result wrapped to (-pi, pi]. Q-F1.
//   * The spec's port recipe for the facing turn rate is itself a GUESS ("follow the camera at the walk turn
//     rate"); this header exposes the target heading (camera heading + bank) and leaves the rate to the caller.
//   * "Deploy ... blend 0.2": the meaning of that animation blend value is not stated; it is stored in
//     FreefallState::deployBlend and not otherwise used. Q-F2.
//   * Freefall start inputs (activity mode, support, 0x0096f540 ...) are plain booleans supplied by the caller.
//   * Mission skydiving (0x009adb00, Skydive_vertical_dive_vel) and Player_ragdoll_to_freefall_* are residual and
//     not modelled.
//
// Pure, header-only. Units: m, s, rad; angles/times as in the spec (ms for the timers).
#pragma once

#include <algorithm>
#include <cmath>

namespace sr3player {
namespace freefall {

inline constexpr double kPi = 3.14159265358979323846;

// 19.2 constants (shipped tweak_table values).
struct Constants {
    double ffNormalT = 45.0, ffForwardT = 67.0, ffBackT = 36.0;            // Freefall_*_terminal_velocity
    double pcNormalT = 9.0, pcForwardT = 11.5, pcBackT = 6.7;              // Parachute_*_terminal_velocity
    double ffMaxForward = 30.0, ffMaxReverse = 3.0, ffMaxBank = 20.0;      // Freefall_max_forward/_reverse/_bank_speed
    double ffMaxBankRot = 0.6, ffApproachRot = 0.75;                       // Freefall_max_bank_rotation_rad / approach
    double pcSlow = 2.0, pcNormal = 8.0, pcFast = 20.0;                    // Parachute_slow/normal/fast_forward_speed
    double pcMaxBankRot = 1.0;                                             // Parachute_max_bank_rotation_rad
    double accelLimit = 10.0;                                              // Player_freefall_acceleration
    double animLerp = 2.0;                                                 // Parachute_anim_lerp_percent
    double allowDeployMs = 1500.0;                                         // Parachute_allow_deploy_ms
    double minClearDistance = 10.0;                                        // Freefall_min_clear_distance
    double minFallSpeed = 10.0;                                            // Freefall_min_fall_speed
    double speedUpFov = 8.0, slowDownFov = -24.0;                          // Parachute_speed_up_fov / slow_down_fov
    double chuteFadeMs = 1000.0;                                           // chute object's own timer
    double defaultFallCap = 44.704;                                        // 0x01119748 (not in freefall)
};
inline const Constants& DefaultConstants() {
    static const Constants c;
    return c;
}

// 19.3 state (Human +0xe8 bits 0x2000000 / 0x4000000, +0xaf4, +0xaf8, +0x203c, +0x20fc ...).
struct FreefallState {
    bool inFreefall = false;
    bool chuteOpen = false;
    double b = 0.0;            // +0xaf4 dive blend in [-1,1]
    double bank = 0.0;         // +0xaf8
    double rx = 0.0, rz = 0.0; // +0x203c horizontal velocity r
    double lockMs = 0.0;       // +0x20fc deploy lock timer
    double chuteTimerMs = 0.0; // chute object's own timer (fade); >0 while a released chute is fading
    double terminalT = 44.704; // controller +0x170
    double deployBlend = 0.0;  // Q-F2
    // outputs of the last frame (+0xa50 facing, +0xa60 speed, +0xa68 turn angle)
    double facingX = 0.0, facingZ = 0.0, speed = 0.0, turnAngle = 0.0;
    double fovOffset = 0.0;
};

// ---- 19.4 step 1: freefall start ---------------------------------------------------------------------------------
struct StartInputs {
    bool activityMode2 = false;      // "not in activity mode 2"
    bool onMovingSupport = false;    // +0x1c9c bit 0x2000
    bool exempt0096f540 = false;
    int locomotion = 6;              // +0xccd, must be 6 (falling)
    int cc8 = 0;                     // +0xcc8, must not be 2
    double fallSpeed = 0.0;          // +0x1aa4
    bool c8Bit0 = false;             // +0xc8 bit 0 must be clear
    bool groundWithinClearDistance = false;  // the straight-down ray of Freefall_min_clear_distance hit something
};
inline bool CanStartFreefall(const StartInputs& in, const Constants& c = DefaultConstants()) {
    return !in.activityMode2 && !in.onMovingSupport && !in.exempt0096f540 && in.locomotion == 6 && in.cc8 != 2 &&
           in.fallSpeed > c.minFallSpeed && !in.c8Bit0 && !in.groundWithinClearDistance;
}
// 0x00995410(human, 1, 0): set the freefall bit, b = 0, bank = 0; player arms the deploy lock; r = velocity; T = 45.
inline void StartFreefall(FreefallState& s, double velX, double velZ, bool isPlayer, bool armLock = true,
                          const Constants& c = DefaultConstants()) {
    s.inFreefall = true;
    s.b = 0.0;
    s.bank = 0.0;
    if (isPlayer && armLock) s.lockMs = c.allowDeployMs;
    s.rx = velX;
    s.rz = velZ;
    s.terminalT = c.ffNormalT;
}

// ---- 19.4 step 2: the deploy / cut-away button -------------------------------------------------------------------
enum class ButtonResult { None, Deployed, CutAway };
struct ButtonInputs {
    bool isPlayer = true;
    bool alive = true;
    bool activity15 = false;
    bool onMovingSupport = false;
    double fallSpeed = 0.0;
    bool jumpingAllowsDeploy = false;  // 0x0098b970 with +0x1d9c bit 0
    bool cc8IsTwoOrSkydive = true;     // +0xcc8 == 2 or mission skydiving 0x009adb00
};
// 0x00995830 "may deploy".
inline bool MayDeploy(const FreefallState& s, const ButtonInputs& in) {
    if (!in.isPlayer || !in.alive || in.activity15) return false;
    if (in.onMovingSupport) return in.fallSpeed > 0.0;
    if (in.jumpingAllowsDeploy) return true;
    return !(s.lockMs > 0.0) && in.cc8IsTwoOrSkydive && !s.chuteOpen;
}
// 0x00995620 cut-away.
inline void CutAway(FreefallState& s, const Constants& c = DefaultConstants()) {
    s.chuteOpen = false;
    s.lockMs = c.allowDeployMs;
    s.chuteTimerMs = c.chuteFadeMs;
    s.terminalT = c.ffNormalT;  // T follows the freefall pair from the next movement step; 45 with no stick
}
// 0x00995f30 deploy.
inline void Deploy(FreefallState& s, double velX, double velZ, const Constants& c = DefaultConstants()) {
    s.chuteOpen = true;
    s.chuteTimerMs = c.chuteFadeMs;  // "+0x100 timer 1000 ms" on the new chute object
    if (!s.inFreefall) StartFreefall(s, velX, velZ, true, false, c);
    s.deployBlend = 0.2;
    s.lockMs = c.allowDeployMs;      // re-arm
}
// One press of CBA_GAC_ACTION while falling: toggles.
inline ButtonResult PressActionButton(FreefallState& s, const ButtonInputs& in, double velX, double velZ,
                                      const Constants& c = DefaultConstants()) {
    if (s.chuteOpen) {
        if (s.lockMs > 0.0) return ButtonResult::None;
        CutAway(s, c);
        return ButtonResult::CutAway;
    }
    if (!MayDeploy(s, in)) return ButtonResult::None;
    Deploy(s, velX, velZ, c);
    return ButtonResult::Deployed;
}

// ---- 19.4 step 3/4: per-frame timers, end -------------------------------------------------------------------------
inline void TickTimers(FreefallState& s, double dt) {
    s.lockMs = std::max(0.0, s.lockMs - dt * 1000.0);
    s.chuteTimerMs = std::max(0.0, s.chuteTimerMs - dt * 1000.0);
}
// 0x00995950: cut the chute if open (it fades), clear flags, T = 44.704.
inline void EndFreefall(FreefallState& s, const Constants& c = DefaultConstants()) {
    if (s.chuteOpen) {
        s.chuteOpen = false;
        s.chuteTimerMs = c.chuteFadeMs;
    }
    s.inFreefall = false;
    s.terminalT = c.defaultFallCap;
    s.fovOffset = 0.0;
}
// Per-frame end conditions: landed (and +0xec bit 0x100 clear) -> landing animation + end; +0xc8 bit 0 or
// +0xccd != 8 -> end. Returns true when freefall ended this call.
struct EndInputs {
    bool landed = false;          // 0x0096e670, or airborne > 1 s with fall speed <= 0.01
    bool ec_bit100 = false;
    bool c8Bit0 = false;
    int locomotion = 8;           // +0xccd
};
inline bool CheckEnd(FreefallState& s, const EndInputs& in, bool& playLandingAnim, const Constants& c = DefaultConstants()) {
    playLandingAnim = false;
    if (!s.inFreefall) return false;
    if (in.landed && !in.ec_bit100) {
        playLandingAnim = true;
        EndFreefall(s, c);
        return true;
    }
    if (in.c8Bit0 || in.locomotion != 8) {
        EndFreefall(s, c);
        return true;
    }
    return false;
}

// ---- 19.5 movement law --------------------------------------------------------------------------------------------
struct MoveInputs {
    double A = 0.0;                 // strafe axis
    double B = 0.0;                 // forward axis
    double fwdX = 0.0, fwdZ = 1.0;  // camera forward on the ground plane (will be normalised)
    double rtX = 1.0, rtZ = 0.0;    // camera right on the ground plane (normalised)
    double faceX = 0.0, faceZ = 1.0; // +0x64 character facing (parachute direction)
    bool groundNear = false;        // the 10 m probe hit
    bool localPlayer = true;
};

inline double Heading(double x, double z) { return std::atan2(x, z); }  // CHOSEN Q-F1
inline double WrapPi(double a) {
    a = std::fmod(a, 2 * kPi);
    if (a <= -kPi) a += 2 * kPi;
    if (a > kPi) a -= 2 * kPi;
    return a;
}

// Step 1: dive blend and terminal speed. Updates b, terminalT, fovOffset.
inline void UpdateTerminalSpeed(FreefallState& s, double B, double dt, bool local = true, const Constants& c = DefaultConstants()) {
    const double lam = dt * c.animLerp;
    if (!s.chuteOpen && B > 0.2)
        s.b += (1.0 - s.b) * lam;
    else
        s.b += (B - s.b) * lam;
    const double N = s.chuteOpen ? c.pcNormalT : c.ffNormalT;
    const double Fw = s.chuteOpen ? c.pcForwardT : c.ffForwardT;
    const double Bk = s.chuteOpen ? c.pcBackT : c.ffBackT;
    s.terminalT = s.b > 0.0 ? N + (Fw - N) * s.b : N + (Bk - N) * (-s.b);
    if (local) s.fovOffset = s.b > 0.0 ? s.b * c.speedUpFov : -s.b * c.slowDownFov;
}

inline void Normalize2(double& x, double& z, double fx, double fz) {
    const double l = std::sqrt(x * x + z * z);
    if (l > 1e-12) {
        x /= l;
        z /= l;
    } else {
        x = fx;
        z = fz;
    }
}

// Steps 1-5. Call every frame while in freefall. Returns the target horizontal velocity D via out params.
inline void UpdateMovement(FreefallState& s, const MoveInputs& in, double dt, double* outDx = nullptr, double* outDz = nullptr,
                           const Constants& c = DefaultConstants()) {
    UpdateTerminalSpeed(s, in.B, dt, in.localPlayer, c);
    if (in.groundNear) return;  // step 2: no steering
    double fx = in.fwdX, fz = in.fwdZ, rx = in.rtX, rz = in.rtZ;
    Normalize2(fx, fz, 0.0, 1.0);
    Normalize2(rx, rz, 1.0, 0.0);
    double Dx, Dz, beta;
    if (!s.chuteOpen) {
        const double sp = in.B * (in.B > 0.0 ? c.ffMaxForward : c.ffMaxReverse);
        Dx = fx * sp + rx * in.A * c.ffMaxBank;
        Dz = fz * sp + rz * in.A * c.ffMaxBank;
        beta = in.A * c.ffMaxBankRot;
    } else {
        double sp = c.pcNormal;
        if (!(s.lockMs > 0.0)) sp = in.B > 0.0 ? c.pcNormal + (c.pcFast - c.pcNormal) * in.B : c.pcNormal + (c.pcSlow - c.pcNormal) * (-in.B);
        double ux = in.faceX, uz = in.faceZ;
        Normalize2(ux, uz, 0.0, 1.0);
        Dx = ux * sp;
        Dz = uz * sp;
        beta = in.A * c.pcMaxBankRot;
    }
    if (outDx) *outDx = Dx;
    if (outDz) *outDz = Dz;
    // step 4: acceleration limit
    const double ex = Dx - s.rx, ez = Dz - s.rz;
    const double el = std::sqrt(ex * ex + ez * ez);
    if (el > 1e-12) {
        const double m = std::min(el, c.accelLimit * dt);
        s.rx += ex / el * m;
        s.rz += ez / el * m;
    }
    // step 5
    s.bank += (beta - s.bank) * dt * c.ffApproachRot;
    s.speed = std::sqrt(s.rx * s.rx + s.rz * s.rz);
    if (s.speed > 1e-6) {
        s.facingX = s.rx / s.speed;
        s.facingZ = s.rz / s.speed;
    }
    s.turnAngle = WrapPi(Heading(fx, fz) - Heading(s.rx, s.rz) + s.bank);
}

// Step 7 / Sec5.6.5 gravity: vy' = vy + g*(1 - (sink/T)^2)*dt, g = -9.8, only while sinking (sink > 0).
inline double StepVertical(double vy, double T, double dt, double g = -9.8) {
    const double sink = -vy;
    double gg = g;
    if (sink > 0.0) {
        const double ratio = sink / T;
        gg *= (1.0 - ratio * ratio);
    }
    return vy + gg * dt;
}

}  // namespace freefall
}  // namespace sr3player
