// sr3physics/aircraft_watercraft.h - player-flown helicopter / airplane / VTOL / watercraft per-step laws.
//
// SPEC SOURCE (the ONLY source of engine facts): spec-physics-runtime.md Sec10.13 "Player-flown aircraft and
// watercraft: the per-step laws (2026-10-18)": 10.13.1 step order + altitude ceiling, 10.13.2 helicopter, 10.13.3
// airplane, 10.13.4 VTOL, 10.13.5 watercraft, 10.13.6 buoyancy (PARTIAL), 10.13.7 damage, 10.13.8 wiring.
// Labels: FINAL/HIGH items are implemented as written; anything the spec leaves open is marked CHOSEN below and has
// a question for Team A in the task report. Test vectors H1-H10, P1-P10, V1-V8, B1-B8, W1-W6, D1-D4 are reproduced
// in tests/synthetic_aircraft_watercraft_test.cpp.
//
// Pure, header-only, no engine/Havok dependency. Everything works on a small CraftBody (rigid body with force /
// torque helpers); the caller owns collision, contacts and the water-height source.
//
// CHOSEN (spec silent):
//   * CraftBody inertia: a diagonal local inverse inertia supplied by the caller (Havok owns the real one; the spec
//     only gives mass = 1/invMass). Default 1/(mass*4) per axis. Q-A1.
//   * Body linear/angular damping is applied as v *= max(0, 1 - d*dt) (spec 10.13.2 step 9 gives the (lin, ang)
//     pairs but not Havok's damping formula). Q-A2.
//   * Plane bank sign: phi > 0 = right wing down, so a positive roll stick (phi_t = +pi/3) banks right and the yaw
//     law (U' = R x fwd, which algebraically points DOWN when level) turns the nose right. The spec gives the
//     magnitudes only. Q-A3.
//   * Plane roll limiter: V already contains W*L, the limiter output is used unscaled (the spec says both "scaled
//     by W*L" for V and for the result; applying it twice is not assumed). Q-A4.
//   * Min-time limiter 0x00dae970 is the same piecewise accel/cruise/brake profile used for the steering limiter
//     (vehicle_driving_slice.h SteeringProfileLimiter); its first-step output stays HIGH/residual per spec.
//   * Heli resting predicate: the "looser bound with c < -0.4" is not given; exposed as a parameter. Q-A5.
//   * Auto-level axis when U.y < -0.95 and angular velocity is zero: falls back to the body right axis. Q-A6.
//   * Boat thrust direction/point geometry (0x00aedc80/0x00aedd30 are residual): D = fwd yawed by -rudder about up
//     then pitched down by Engine_Thrust_Pitch_Angle about right; point = body position + local thrust position.
//     Q-A7.
//   * Boat submerged volume without the shape-clipping routine: slab approximation (SubmergedBoxApprox). Water
//     height source is OPEN in the spec (caller supplies). Underwater drag is the spec's GUESS placeholder
//     v *= (1 - c*dt) with caller-supplied c (default 0.5, value CHOSEN). Q-A8.
//   * VTOL camera blend re-base when elapsed >= 2000: restarts from 0. Q-A9.
//   * Barrel roll is reduced to: rate Barrel_Roll_Max_Angular_Speed for pi/rate seconds then it ends (the
//     blend-back / 0.349 end rule is described qualitatively). Q-A10.
//   * Plane ground pitch-down correction (HIGH), assist-off surface model, rotor wash (gains unread) and
//     dead-aircraft behaviour are NOT modelled (spec residual).
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace sr3physics {
namespace craft {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kGravity = 9.8;  // world gravity (0,-9.8,0), spec Sec4.1 / 10.13.1

// ---------------------------------------------------------------------------------------------------------------
// Small vector / body helpers
// ---------------------------------------------------------------------------------------------------------------
struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator*(double s, V3 a) { return a * s; }
inline V3& operator+=(V3& a, V3 b) { a = a + b; return a; }
inline double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline V3 Norm(V3 a, V3 fallback = {0, 1, 0}) {
    const double l = Len(a);
    return l > 1e-12 ? a * (1.0 / l) : fallback;
}

struct Basis {
    V3 right{1, 0, 0}, up{0, 1, 0}, fwd{0, 0, 1};  // body +0xf0 / +0x100 / +0x110
    V3 ToWorld(V3 local) const { return right * local.x + up * local.y + fwd * local.z; }
};

// Basis tilted about the world X axis (pitch) - convenience for tests / callers. Positive angle = nose down:
// fwd = (0,-sin a, cos a), up = (0, cos a, sin a).
inline Basis BasisNoseDown(double a) {
    Basis b;
    b.fwd = {0, -std::sin(a), std::cos(a)};
    b.up = {0, std::cos(a), std::sin(a)};
    return b;
}

struct CraftBody {
    V3 pos{0, 0, 0}, vel{0, 0, 0}, angVel{0, 0, 0};
    Basis basis;
    double mass = 1000.0;
    V3 invInertiaLocal{0, 0, 0};  // (right, up, fwd); zero -> default 1/(4*mass), CHOSEN Q-A1

    V3 InvInertiaApply(V3 tau) const {
        V3 ii = invInertiaLocal;
        const double d = 1.0 / (4.0 * mass);
        if (ii.x == 0 && ii.y == 0 && ii.z == 0) ii = {d, d, d};
        return basis.right * (ii.x * Dot(basis.right, tau)) + basis.up * (ii.y * Dot(basis.up, tau)) +
               basis.fwd * (ii.z * Dot(basis.fwd, tau));
    }
    // motion vtable +0x5c: force at the centre of mass for dt.
    void ForceCom(V3 f, double dt) { vel += f * (dt / mass); }
    // +0x58: force at a world point P for dt.
    void ForceAt(V3 f, V3 p, double dt) {
        vel += f * (dt / mass);
        angVel += InvInertiaApply(Cross(p - pos, f)) * dt;
    }
    // +0x60: torque for dt.
    void Torque(V3 tau, double dt) { angVel += InvInertiaApply(tau) * dt; }
    // +0x4c: linear impulse.
    void Impulse(V3 j) { vel += j * (1.0 / mass); }
};

inline double WrapPi(double a) {  // to (-pi, pi]
    a = std::fmod(a, 2 * kPi);
    if (a <= -kPi) a += 2 * kPi;
    if (a > kPi) a -= 2 * kPi;
    return a;
}
inline double Wrap2Pi(double a) {  // to [0, 2pi)
    a = std::fmod(a, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    return a;
}
inline double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Body damping (0x00ada960). CHOSEN formula (Q-A2): v *= max(0, 1 - d*dt).
inline void ApplyBodyDamping(CraftBody& b, double linear, double angular, double dt) {
    b.vel = b.vel * std::max(0.0, 1.0 - linear * dt);
    b.angVel = b.angVel * std::max(0.0, 1.0 - angular * dt);
}

// Spin damping (0x00ad9500), shared by helicopter and watercraft: w *= max(0, 1 - dt*d), d = collision when
// |w| > threshold else normal.
inline void ApplySpinDamping(CraftBody& b, double normalDamping, double collisionDamping, double threshold, double dt) {
    const double d = Len(b.angVel) > threshold ? collisionDamping : normalDamping;
    b.angVel = b.angVel * std::max(0.0, 1.0 - dt * d);
}

// Top-speed clamp (10.11.10 FINAL, 0x00a86620 step 4): the 3-D rigid-body speed above cap + bonus is rescaled to exactly
// the limit (all three components). cap <= 0 means "no cap configured" (CHOSEN: the caller did not supply one).
inline void ApplyMaximumSpeedClamp(CraftBody& b, double cap, double bonus = 0.0) {
    if (!(cap > 0.0)) return;
    const double limit = cap + bonus;
    const double sp = Len(b.vel);
    if (sp > limit) b.vel = b.vel * (limit / sp);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x00dae970 minimum-time limiter (see vehicle_driving_slice.h SteeringProfileLimiter): the "position" x is given
// by the caller each call, the velocity state v is carried. Returns the new velocity state. A <= 0 or V <= 0 with a
// positive damp distance is not representable; A <= 0 (damp 0) snaps to the clamped one-step velocity.
// ---------------------------------------------------------------------------------------------------------------
inline double MinTimeLimiterStep(double x, double v, double target, double V, double A, double dt) {
    if (!(V > 0.0)) return 0.0;
    if (!(A > 0.0)) return Clamp((target - x) / std::max(dt, 1e-9), -V, V);
    double rem = dt;
    for (int seg = 0; seg < 8 && rem > 1e-9; ++seg) {
        const double e = target - x;
        const double s = (e > 0.0) ? 1.0 : -1.0;
        const double ae = std::fabs(e);
        const double w = v * s;
        if (ae < 1e-9 && std::fabs(w) < 1e-9) {
            v = 0.0;
            break;
        }
        double accel, tEvent;
        if (w < 0.0) {
            accel = A;
            tEvent = -w / A;
        } else if (w * w / (2.0 * A) >= ae * (1.0 - 1e-12)) {
            accel = -A;
            tEvent = w / A;
        } else if (w >= V - 1e-9) {
            accel = 0.0;
            tEvent = (ae - V * V / (2.0 * A)) / V;
        } else {
            accel = A;
            const double tV = (V - w) / A;
            const double tB = (-w + std::sqrt(w * w * 0.5 + A * ae)) / A;
            tEvent = std::min(tV, tB);
        }
        const double h = std::min(rem, std::max(tEvent, 0.0));
        const double step = (h > 0.0) ? h : rem;
        const double wNew = Clamp(w + accel * step, -V, V);
        x += s * (w * step + 0.5 * accel * step * step);
        v = s * wNew;
        rem -= step;
        if (h <= 0.0) break;
    }
    return v;
}

// ---------------------------------------------------------------------------------------------------------------
// 10.13.1 Altitude ceiling 0x00ad95e0 (FINAL), all three flight modes. Tweaks Max_Vehicle_Altitude 1333 m,
// Max_Vehicle_Altitude_Ramp_Time_Ms 6000.
// ---------------------------------------------------------------------------------------------------------------
struct AltitudeCeilingState {
    double t = 0.0;  // accumulator +0x584 (seconds)
};
inline void ApplyAltitudeCeiling(AltitudeCeilingState& s, double height, V3& vel, double dt, double maxAltitude = 1333.0,
                                 double rampMs = 6000.0) {
    if (height < maxAltitude) {
        s.t = 0.0;
        return;
    }
    s.t += dt;
    if (vel.y > 0.0) {
        const double k = std::min(1.0, 1000.0 * s.t / rampMs);
        vel.y *= (1.0 - k);
        vel.y += -kGravity * k * dt;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 10.13.2 Helicopter
// ---------------------------------------------------------------------------------------------------------------
struct HeliParams {  // helicopter block (spec-vehicle-data Sec7.5); units as authored / after load
    double mass = 1360.0;
    double elevationSpeed = 11.176, elevationAccel = 9.0, elevationDamp = 1.25;
    double tiltLiftLoss = 0.0;
    double steeringAccel = 0.05, pitchBias = 14.0;
    double tailAccel = 4.0;
    V3 tailRotorCenter{0, -0.12, -7.0};  // local
    V3 rotorCenter{0, 0, 0};             // local
    double rotorRadius = 4.0, rotorTilt = 0.0;
    double extraSpeedAccel = 45.0;
    double dragCoefficient = 0.2;
    double autolevelAccel = 2.7053;
    double normalSpinDamping = 0.0, collisionSpinDamping = 0.0, collisionSpinThreshold = 1e9;
    double maximumSpeed = 31.2928;  // 70 mph
    double looserRestSpeed = 3.0;   // CHOSEN Q-A5
};

struct HeliInput {
    double collective = 0.0;  // c in [-1,1]
    double cyclicMag = 0.0;   // m in [0,1]
    double cyclicAngle = 0.0; // psi in [0,2pi]
    double yaw = 0.0;         // +0x950 = clamp(-y)
};

struct HeliState {
    double aLim = 0.0;       // +0x970
    double accel = 0.0;      // +0x974
    double settleMs = 0.0;   // +0x978
    AltitudeCeilingState ceiling;
};

struct HeliFlags {
    bool rotorHealthy = true;     // 0x00af0c20 (VTOL: always true; heli: false after tail loss)
    bool hasDriverOrRemote = true;
    bool resting = false;         // pilot-record landing flags / contact + 0x00add4a0 (computed by caller)
};

// Stick -> (magnitude, angle). X = -walk turn axis, Y = -walk forward axis (already negated by the caller).
// psi = atan-quadrant in [0,2pi]; stick forward (Y=-1) = 3pi/2, right (X=-1) = pi.
inline void HeliCyclicFromXY(double X, double Y, double& mag, double& psi) {
    mag = Clamp(std::sqrt(X * X + Y * Y), 0.0, 1.0);
    psi = (X == 0.0 && Y == 0.0) ? 0.0 : Wrap2Pi(std::atan2(Y, X));
}
// Keyboard/pad yaw: TURN_LEFT -> y=-1, TURN_RIGHT -> +1; stored +0x950 = clamp(-y).
inline double HeliYawFromKeys(bool left, bool right) {
    double y = (right ? 1.0 : 0.0) - (left ? 1.0 : 0.0);
    return Clamp(-y, -1.0, 1.0);
}
struct HeliCameraRelative {
    double psi;      // adjusted cyclic angle
    double yaw;      // auto-yaw or the passed key yaw
    bool autoYawed;
};
// d = wrap(heading(vehicle) - heading(camera right row)) in (-pi,pi]; heading convention is the caller's.
inline HeliCameraRelative HeliApplyCameraRelative(double psi, double mag, double d, double keyYaw, bool turnKeyHeld) {
    HeliCameraRelative r{Wrap2Pi(psi + d), keyYaw, false};
    if (mag >= 0.1 && std::fabs(d) >= 0.1 && !turnKeyHeld) {
        r.yaw = Clamp(d / (kPi / 4.0), -1.0, 1.0);
        r.autoYawed = true;
    }
    return r;
}

// Step 1-3: vertical target, limiter, gravity compensation. Updates s.aLim and s.accel. vy = world vertical speed.
inline void HeliCollective(const HeliParams& p, HeliState& s, double collective, double upY, double vy, bool rotorHealthy,
                           bool hasDriverOrRemote, double dt) {
    const double sgn = upY > 0 ? 1.0 : (upY < 0 ? -1.0 : 0.0);
    const double vt = collective * p.elevationSpeed - p.tiltLiftLoss * sgn * (1.0 - std::fabs(upY));
    const double A = p.elevationDamp > 0 ? p.elevationAccel * p.elevationAccel / (2.0 * p.elevationDamp) : 0.0;
    s.aLim = MinTimeLimiterStep(vy, s.aLim, vt, p.elevationAccel, A, dt);
    const bool settled = s.settleMs > 500.0;
    const bool full = (!settled || collective > 0.0) && rotorHealthy && hasDriverOrRemote;
    s.accel = s.aLim + (full ? kGravity : 4.9);
}

struct RotorForce {
    V3 point, force;
};
// Step 4: twelve ring forces (world). k_i forced to 0 while settled. a = s.accel (total vertical accel).
inline void HeliRotorForces(const HeliParams& p, const CraftBody& b, double a, double mag, double psi, bool settled,
                            RotorForce out[12]) {
    const double ct = std::cos(p.rotorTilt), st = std::sin(p.rotorTilt);
    const V3 D = b.basis.ToWorld({0, ct, st});  // block +0x9c, thrust axis
    // e / g (Pitch_Bias weight): e = |pi/2 - psi| (or |3pi/2 - psi| when psi > pi); g = cos(e)^4 if e <= 0.785385.
    const double e = psi > kPi ? std::fabs(3 * kPi / 2 - psi) : std::fabs(kPi / 2 - psi);
    const double g = e <= 0.785385 ? std::pow(std::cos(e), 4.0) : 0.0;
    for (int i = 0; i < 12; ++i) {
        const double phi = 2.0 * kPi * i / 12.0;
        // ring point: Rotor_Center + tilt(R cos phi, 0, R sin phi); tilt = rotation about x by Rotor_Tilt
        const double rx = p.rotorRadius * std::cos(phi), rz = p.rotorRadius * std::sin(phi);
        const V3 local = {p.rotorCenter.x + rx, p.rotorCenter.y - rz * st, p.rotorCenter.z + rz * ct};
        const double delta = std::fabs(WrapPi(psi - phi));  // angular distance <= pi
        const double w = 1.0 - 2.0 * delta / kPi;
        double k = p.steeringAccel * mag * w * (1.0 + (p.pitchBias - 1.0) * g);
        if (settled) k = 0.0;
        out[i].point = b.pos + b.basis.ToWorld(local);
        out[i].force = D * (b.mass * (a / 12.0 + k));
    }
}

// Step 5: tail rotor force (not while settled). Returns the force (world) applied at the world tail centre.
inline V3 HeliTailForce(const HeliParams& p, const CraftBody& b, double yaw, bool rotorHealthy) {
    const double s = rotorHealthy ? yaw * p.tailAccel : p.tailAccel;
    return b.basis.right * (b.mass * s);
}

// Step 6: extra-speed force at the COM. h = horizontal part of U, length capped at 0.70711. timerMs = settle timer.
inline V3 HeliExtraSpeedForce(const HeliParams& p, const CraftBody& b, double settleMs, double extraAccelOverride = -1.0) {
    const V3 W{0, 1, 0};
    V3 h = b.basis.up - W * Dot(b.basis.up, W);
    const double l = Len(h);
    if (l > 0.70711) h = h * (0.70711 / l);
    const double E = extraAccelOverride >= 0.0 ? extraAccelOverride : p.extraSpeedAccel;
    const double f = settleMs > 0.0 ? 1.0 - settleMs / 500.0 : 1.0;
    return h * (b.mass * E * f);
}
inline void HeliLinearDrag(const HeliParams& p, CraftBody& b, double dt) {
    b.vel = b.vel * std::max(0.0, 1.0 - p.dragCoefficient * dt);
}
// Step 8 auto-level: w += axis * sqrt(2 theta/pi) * A * dt.
inline void HeliAutoLevel(const HeliParams& p, CraftBody& b, double dt, double accelScale = 1.0) {
    const V3 U = b.basis.up;
    V3 axis;
    if (U.y < -0.95)
        axis = Norm(b.angVel, b.basis.right);
    else
        axis = Norm(Cross(U, V3{0, 1, 0}), {0, 0, 0});
    const double theta = std::acos(Clamp(U.y, -1.0, 1.0));
    b.angVel += axis * (std::sqrt(2.0 * theta / kPi) * p.autolevelAccel * accelScale * dt);
}
// Step 10 settle timer 0x00add700.
inline void HeliSettleTimer(HeliState& s, bool resting, double upY, double dtSeconds) {
    s.settleMs = std::max(0.0, s.settleMs - dtSeconds * 1000.0);
    if (resting) s.settleMs = 800.0;
    if (upY < 0.766 && s.settleMs >= 500.0) s.settleMs = 499.0;
}
// 0x00add4a0 resting test: |v| < 3 with c < 0.4, or a looser bound with c < -0.4 (CHOSEN parameter, Q-A5).
inline bool HeliRestPredicate(const HeliParams& p, bool contact, double speed, double collective) {
    if (!contact) return false;
    return (speed < 3.0 && collective < 0.4) || (speed < p.looserRestSpeed && collective < -0.4);
}

// One full flight step (10.13.1 order for the helicopter model). Applies everything to `b`.
inline void HeliStep(const HeliParams& p, HeliState& s, const HeliInput& in, const HeliFlags& fl, CraftBody& b, double dt) {
    HeliSettleTimer(s, fl.resting, b.basis.up.y, dt);                    // step 3 / 10
    const bool settled = s.settleMs > 500.0;
    ApplyBodyDamping(b, settled ? 1.0 : 0.4, settled ? 5.0 : 0.05, dt);  // step 4 / 9
    HeliCollective(p, s, in.collective, b.basis.up.y, b.vel.y, fl.rotorHealthy, fl.hasDriverOrRemote, dt);
    RotorForce rf[12];
    HeliRotorForces(p, b, s.accel, in.cyclicMag, in.cyclicAngle, settled, rf);  // step 6
    for (const RotorForce& r : rf) b.ForceAt(r.force, r.point, dt);
    if (!settled) {
        const V3 tailP = b.pos + b.basis.ToWorld(p.tailRotorCenter);
        b.ForceAt(HeliTailForce(p, b, in.yaw, fl.rotorHealthy), tailP, dt);
        b.ForceCom(HeliExtraSpeedForce(p, b, s.settleMs), dt);           // step 8
        HeliLinearDrag(p, b, dt);
        HeliAutoLevel(p, b, dt);
        ApplySpinDamping(b, p.normalSpinDamping, p.collisionSpinDamping, p.collisionSpinThreshold, dt);
    }
    ApplyAltitudeCeiling(s.ceiling, b.pos.y, b.vel, dt);                 // step 9
    ApplyMaximumSpeedClamp(b, p.maximumSpeed);                           // step 11: 10.11.10 (class 3 has no bonus term)
}

// ---------------------------------------------------------------------------------------------------------------
// 10.13.3 Airplane
// ---------------------------------------------------------------------------------------------------------------
struct PlaneParams {
    double mass = 1500.0;
    double engineAccel = 7.5, engineReverseAccel = 4.5, rampUp = 2.0, rampDown = 2.0, reverseSpeed = 4.470;
    double liftMinSpeed = 29.058;
    double rollMax = 1.6581, rollDamp = 0.3491;
    double horizMax = 0.9076, horizAccel = 1.7453, horizDecel = 1.7453;
    double vertMax = 0.5585, vertAccel = 3.0020;
    double turnLinearAccel = 600.0;
    double barrelRollMax = 6.1087;
    double maximumSpeed = 0.0;  // Maximum_Speed (m/s) for the 10.11.10 clamp; 0 = unset, no clamp (CHOSEN: caller supplies)
    std::vector<V3> enginePoints{V3{0, 0, 0}};  // local
};
struct PlaneInput {
    double roll = 0.0;      // X' (post circle->square, clamped)
    double pitch = 0.0;     // Y'
    double rudder = 0.0;    // +0x140 = clamp(-turn)
    double throttle = 0.0;  // +0x154
    bool inverted = false;  // +0x26 (writer not traced)
};
struct PlaneState {
    double engineAccel = 0.0;   // +0x8c0 applied acceleration
    double rollRate = 0.0;      // roll limiter velocity state
    double yawRate = 0.0;       // rate about U'
    double pitchRate = 0.0;     // rate about R
    bool barrelActive = false;
    double barrelElapsed = 0.0;
    int barrelDir = 1;  // remembered turn direction (+0x14c/+0x14d): +1 = positive bank (right wing down), -1 = left
    AltitudeCeilingState ceiling;
};
struct PlaneFlags {
    double wingLeft = 1.0, wingRight = 1.0;  // Sec10.13.7 factors
    bool onGroundPartial = false;            // partial ground contact
    bool onGround = false;                   // reversing floor applies
};

// Circle-to-square remap of 0x00a91800 (unclamped, as the spec). theta folded into [-pi/4, pi/4] for r' = r/cos.
inline void PlaneCircleToSquare(double X, double Y, double& Xp, double& Yp) {
    const double r = std::sqrt(X * X + Y * Y);
    if (r < 1e-12) {
        Xp = Yp = 0;
        return;
    }
    const double th = std::atan2(Y, X);
    double f = std::fmod(th, kPi / 2.0);  // fold to nearest axis
    if (f > kPi / 4.0) f -= kPi / 2.0;
    if (f < -kPi / 4.0) f += kPi / 2.0;
    const double rp = r / std::cos(f);
    Xp = rp * std::cos(th);
    Yp = rp * std::sin(th);
}

inline double PlaneWingFactor(int intactParts, int totalParts) {
    return totalParts > 0 ? double(intactParts) / double(totalParts) : 1.0;
}
inline double PlaneWingIntegrity(double left, double right) { return 0.5 * (left + right); }

// Step 1 engine 0x00ade700: advances s.engineAccel; returns M*a (force along fwd at EVERY engine point).
inline double PlaneEngineStep(const PlaneParams& p, PlaneState& s, double throttle, double forwardSpeed, bool partialGround,
                              bool reversingOnGround, double dt) {
    const double T = std::fabs(throttle) * (throttle >= 0.0 ? p.engineAccel : -p.engineReverseAccel);
    const double diff = T - s.engineAccel;
    const bool sameDir = (throttle > 0.0 && diff > 0.0) || (throttle < 0.0 && diff < 0.0);
    const double rate = (sameDir ? p.engineAccel / p.rampUp : p.engineAccel / p.rampDown) * dt;
    s.engineAccel += Clamp(diff, -rate, rate);
    double lo = -p.engineReverseAccel;
    if (partialGround) lo *= Clamp(forwardSpeed / 13.4112, 0.0, 1.0);
    s.engineAccel = Clamp(s.engineAccel, lo, p.engineAccel);
    if (reversingOnGround && s.engineAccel < 0.0 && dt > 0.0) {  // forward speed stays >= -Reverse_Speed (HIGH)
        const double minAcc = (-p.reverseSpeed - forwardSpeed) / dt;
        s.engineAccel = std::max(s.engineAccel, std::min(0.0, minAcc));
    }
    return p.mass * s.engineAccel;
}
// Step 2 lift fraction L = clamp(u/LiftMin,0,1)^2.
inline double PlaneLiftFraction(const PlaneParams& p, double forwardSpeed) {
    const double q = Clamp(forwardSpeed / p.liftMinSpeed, 0.0, 1.0);
    return q * q;
}
// Step 3 bank target.
inline double PlaneBankTarget(const PlaneInput& in, double wingLeft, double wingRight) {
    if (in.inverted) return kPi;
    if (std::fabs(in.rudder) > 0.1) return in.rudder > 0 ? kPi / 2 : -kPi / 2;
    const double r = -Clamp(in.roll + (wingRight - wingLeft), -1.0, 1.0);
    if (std::fabs(r) > 0.1) return -r * kPi / 3.0;
    return 0.0;
}
// Level right axis R = normalise(fwd.z, 0, -fwd.x). Bank phi > 0 = right wing down (CHOSEN, Q-A3).
inline V3 PlaneLevelRight(const Basis& b) { return Norm(V3{b.fwd.z, 0, -b.fwd.x}, {1, 0, 0}); }
inline double PlaneBank(const Basis& b) {
    const V3 R = PlaneLevelRight(b);
    const double up = Dot(Cross(R, b.right), b.fwd);  // +: right wing up about +fwd
    return -std::atan2(up, Dot(R, b.right));
}
inline double PlaneBankTriangle(double phi) {  // 0 level, 1 at 90, 0 at 180
    return std::max(0.0, 1.0 - std::fabs(std::fabs(phi) / (kPi / 2.0) - 1.0));
}
inline void PlaneBarrelRollStart(PlaneState& s, int direction = 1) {
    s.barrelActive = true;
    s.barrelElapsed = 0.0;
    s.barrelDir = direction < 0 ? -1 : 1;  // CHOSEN: which of +0x14c/+0x14d maps to which sign (spec says only left/right)
}

// One full airplane-model step (assist on) in the 10.13.1 order. Applies to `b`.
inline void PlaneStep(const PlaneParams& p, PlaneState& s, const PlaneInput& in, const PlaneFlags& fl, CraftBody& b, double dt) {
    ApplyBodyDamping(b, 0.01, 0.05, dt);  // step 4 (damping)
    const double u = Dot(b.vel, b.basis.fwd);
    const double F = PlaneEngineStep(p, s, in.throttle, u, fl.onGroundPartial, fl.onGround && in.throttle < 0.0, dt);
    for (const V3& e : p.enginePoints) b.ForceAt(b.basis.fwd * F, b.pos + b.basis.ToWorld(e), dt);
    const double W = PlaneWingIntegrity(fl.wingLeft, fl.wingRight);
    const double L = PlaneLiftFraction(p, u);
    b.vel.y += W * L * kGravity * dt;  // lift
    const double WL = W * L;
    const V3 fwd = b.basis.fwd;
    const V3 R = PlaneLevelRight(b.basis);
    const V3 Up = Cross(R, fwd);  // U' = R x fwd (spec)
    const double phi = PlaneBank(b.basis);
    const double phiT = PlaneBankTarget(in, fl.wingLeft, fl.wingRight);
    const double rCmd = -Clamp(in.roll + (fl.wingRight - fl.wingLeft), -1.0, 1.0);
    // roll
    double Vroll = p.rollMax * WL;
    if (std::fabs(phiT - phi) > 0.785 && fl.wingLeft == fl.wingRight) Vroll *= 1.5;
    if (s.barrelActive) {
        // Spec 10.13.3 step 4: signed Barrel_Roll_Max_Angular_Speed for pi/rate seconds, then blended back to the normal
        // rate once the error is under pi/2, ending under 0.349 rad (P9). CHOSEN blend: weight 1 - err/(pi/2) on the
        // normal limiter rate (the spec gives no curve).
        s.barrelElapsed += dt;
        const double barrelRate = s.barrelDir * p.barrelRollMax;
        const double err = std::fabs(WrapPi(phiT - phi));
        if (s.barrelElapsed < kPi / p.barrelRollMax) {
            s.rollRate = barrelRate;
        } else if (err < 0.349) {
            s.barrelActive = false;
            s.rollRate = 0.0;
        } else {
            const double A = p.rollDamp > 0 ? Vroll * Vroll / (2.0 * p.rollDamp) : 0.0;
            const double normal = MinTimeLimiterStep(phi, s.rollRate, phiT, Vroll, A, dt);
            const double w = err < kPi / 2.0 ? 1.0 - err / (kPi / 2.0) : 0.0;
            s.rollRate = w * normal + (1.0 - w) * barrelRate;
        }
    } else {
        const double A = p.rollDamp > 0 ? Vroll * Vroll / (2.0 * p.rollDamp) : 0.0;
        s.rollRate = MinTimeLimiterStep(phi, s.rollRate, phiT, Vroll, A, dt);
    }
    // yaw
    const double q = Clamp(std::fabs(phi) / (kPi / 2.0), 0.0, 1.0);
    const double yawTarget = q * WL * p.horizMax * rCmd;
    {
        const double lim = (std::fabs(yawTarget) < 0.001 ? p.horizDecel : p.horizAccel) * WL * dt;
        s.yawRate += Clamp(yawTarget - s.yawRate, -lim, lim);
    }
    // pitch
    const double pc = -Clamp(in.pitch, -1.0, 1.0);
    const double pitchTarget = (1.0 - 0.5 * PlaneBankTriangle(phi)) * WL * p.vertMax * pc;
    {
        const double lim = p.vertAccel * WL * dt;
        s.pitchRate += Clamp(pitchTarget - s.pitchRate, -lim, lim);
    }
    // replace the three angular-velocity components (about fwd, U', R)
    b.angVel = b.angVel - fwd * Dot(b.angVel, fwd) - Up * Dot(b.angVel, Up) - R * Dot(b.angVel, R);
    b.angVel += fwd * (-s.rollRate) + Up * s.yawRate + R * s.pitchRate;  // phi grows with right wing down = rot about -fwd
    // nose follows velocity
    const double sp = Len(b.vel);
    if (sp > 1e-6) {
        const V3 vh = b.vel * (1.0 / sp);
        const double slip = Len(Cross(fwd, vh));
        const V3 n = Norm(fwd - vh * Dot(fwd, vh), {0, 0, 0});
        b.vel += n * (slip * WL * p.turnLinearAccel * dt);
    }
    ApplyAltitudeCeiling(s.ceiling, b.pos.y, b.vel, dt);
    ApplyMaximumSpeedClamp(b, p.maximumSpeed);  // step 8: 10.11.10 (class 2 has no bonus term)
}

// ---------------------------------------------------------------------------------------------------------------
// 10.13.4 VTOL
// ---------------------------------------------------------------------------------------------------------------
struct VtolState {
    int heliModel = 1;       // +0x9b0: 1 hover, 0 jet
    int request = 1;         // +0x9bc
    double kickMs = 0.0;     // +0x9b4
    double camElapsedMs = 0.0;  // +0x9b8
    int machine = 0;         // +0x5e8 state machine 0..3
    bool jetWanted = false;  // vehicle +0xf8 bit 0
};
// 0x00ad9730 (player only): applies a change of request. Hover->jet: model=0, kick 1200 ms. Jet->hover: model=1.
inline void VtolApplyRequest(VtolState& s) {
    if (s.request == s.heliModel) return;
    if (s.request == 0) {
        s.heliModel = 0;
        s.kickMs = 1200.0;
    } else {
        s.heliModel = 1;
    }
    // re-base camera blend 2000 ms so a reversal keeps its progress (Q-A9 for e >= 2000)
    s.camElapsedMs = s.camElapsedMs < 2000.0 ? 2000.0 - s.camElapsedMs : 0.0;
}
// Jet kick 0x00adddd0: returns the linear impulse along forward (M*k*dt) magnitude; k from forward speed u.
inline double VtolJetKickAccel(double u) {
    if (u >= 300.0) return 0.0;
    return u > 290.0 ? (300.0 - u) / 10.0 * 50.0 : 50.0;
}
inline void VtolKickStep(VtolState& s, CraftBody& b, double dt) {
    if (s.kickMs <= 0.0 || s.heliModel != 0) return;
    const double u = Dot(b.vel, b.basis.fwd);
    b.Impulse(b.basis.fwd * (b.mass * VtolJetKickAccel(u) * dt));
    s.kickMs = std::max(0.0, s.kickMs - dt * 1000.0);
}
// State machine 0x00af0070. `toggle`: the transition press toggles jetWanted (binding HIGH). Anim flags: wing
// animation states 0x1dab (to jet) / 0x1dac (to hover) reached.
inline void VtolMachineStep(VtolState& s, bool togglePressed, bool animJetReached, bool animHoverReached) {
    if (togglePressed) s.jetWanted = !s.jetWanted;
    switch (s.machine) {
        case 0:
            if (s.jetWanted) s.machine = 1;
            break;
        case 1:
            if (animJetReached) {
                s.request = 0;
                s.machine = 2;
            }
            break;
        case 2:
            if (!s.jetWanted) {
                s.request = 1;
                s.machine = 3;
            }
            break;
        case 3:
            if (animHoverReached) s.machine = 0;
            break;
    }
}
inline bool VtolRotorHealthy() { return true; }  // 0x00af0c20 = 1 always (V8)

// ---------------------------------------------------------------------------------------------------------------
// 10.13.5 Watercraft controller
// ---------------------------------------------------------------------------------------------------------------
struct BoatParams {
    double mass = 8600.0;
    double maxForwardAccel = 20.0, maxReverseAccel = 7.0;
    double rampUp = 1.0, rampDown = 1.0;
    double steeringInputRate = 3.0, pitchInputRate = 3.0;
    double maxSteeringAngle = 0.5;
    bool lbs = true;
    double lbsMaxLean = 0.99484;  // 57 deg
    double lbsMinRadius = 10.0, lbsTurnRadiusModifier = 0.5, lbsCorrectionAccel = 17.4533;  // 1000 deg/s^2
    double steeringWhileCoasting = 5.0;
    double maximumSpeed = 29.0576;  // 65 mph
    double boostSpeedBonus = 0.0;   // controller +0x7c, added to the cap while the boost bit is set (10.11.10 step 3; meaning OPEN)
    double normalSpinDamping = 0.0, collisionSpinDamping = 0.0, collisionSpinThreshold = 1e9;  // entry +0x848..+0x850
    double leanComOffset = 0.0;
    double thrustPitchAngle = 0.0;
    V3 thrustPosition{0, 0, -3};  // local, CHOSEN default Q-A7
    // Air_Control (Watercraft_Defaults 180 deg/s, 360 deg/s^2)
    double maxRollSpeed = 3.14159265, maxPitchSpeed = 6.2831853, rollAccel = 6.2831853, pitchAccel = 12.566371;
};
struct BoatState {
    double thrust = 0.0;       // +0x80
    double steer = 0.0;        // +0x90
    double pitch = 0.0;        // +0x9c
    double rudder = 0.0;       // +0x94
    double leanTarget = 0.0;   // +0xb4
};
struct BoatInput {
    double steer = 0.0, pitch = 0.0, throttle = 0.0;  // +0x60 / +0x64 / +0x68
    bool stopAssist = false;                          // +0x6c
    bool boost = false;
    double boostScale = 1.0;                          // +0x78
};

// 0x00aed440 thrust state.
inline void BoatThrustStateStep(const BoatParams& p, BoatState& s, const BoatInput& in, double forwardSpeed, double dt) {
    double target = Clamp(in.throttle, -1.0, 1.0);
    if (in.stopAssist) {
        const double acc = forwardSpeed > 0.0 ? p.maxReverseAccel : p.maxForwardAccel;
        target = Clamp(-forwardSpeed / (std::max(dt, 1e-9) * std::max(acc, 1e-9)), -1.0, 1.0);
    }
    if (s.thrust * target < 0.0) s.thrust = 0.0;  // sign disagrees with the input -> reset
    const double scale = in.boost ? in.boostScale : 1.0;
    const double rate = (target > s.thrust ? p.rampUp : p.rampDown) * scale * dt;
    s.thrust += Clamp(target - s.thrust, -rate, rate);
}
// In-water smoothing: +0x90 -> steer, +0x9c -> pitch, rudder, lean target.
inline void BoatSmoothInputs(const BoatParams& p, BoatState& s, const BoatInput& in, double dt) {
    auto move = [](double v, double t, double r) { return v + Clamp(t - v, -r, r); };
    s.steer = move(s.steer, in.steer, p.steeringInputRate * dt);
    s.pitch = move(s.pitch, in.pitch, p.pitchInputRate * dt);
    s.rudder = p.maxSteeringAngle * s.steer;
    if (p.lbs) s.leanTarget = Clamp(p.lbsMaxLean * s.steer, -1.55332, 1.55332);
}
// Thrust force magnitude vector: F = M * (accel by sign) * state * D.
inline V3 BoatThrustDirection(const BoatParams& p, const Basis& b, double rudder) {  // CHOSEN Q-A7
    const double cr = std::cos(-rudder), sr = std::sin(-rudder);
    V3 d = b.fwd * cr + b.right * sr;  // yaw about up by -rudder
    const double cp = std::cos(p.thrustPitchAngle), sp = std::sin(p.thrustPitchAngle);
    return Norm(d * cp - b.up * sp, b.fwd);
}
inline V3 BoatThrustForce(const BoatParams& p, const BoatState& s, const CraftBody& b) {
    const double acc = s.thrust >= 0.0 ? p.maxForwardAccel : p.maxReverseAccel;
    return BoatThrustDirection(p, b.basis, s.rudder) * (b.mass * acc * s.thrust);
}
// LBS kinematic yaw (applied when thrust and lean target are non-zero). Returns the new world-up yaw rate.
inline double BoatLbsYawRate(const BoatParams& p, const BoatState& s, double horizSpeed, bool movingBackward, double currentYawRate,
                             double dt) {
    if (s.thrust == 0.0 || s.leanTarget == 0.0) return currentYawRate;
    double R = horizSpeed * horizSpeed / (kGravity * std::tan(s.leanTarget));
    R *= 1.0 + (p.lbsTurnRadiusModifier - 1.0) * std::max(s.pitch, 0.0);
    if (std::fabs(R) < p.lbsMinRadius) R = R < 0 ? -p.lbsMinRadius : p.lbsMinRadius;
    double target = horizSpeed / R;
    if (movingBackward) target = -target;
    const double lim = p.lbsCorrectionAccel * dt;
    return currentYawRate + Clamp(target - currentYawRate, -lim, lim);
}
// Coasting steer: angular acceleration about up (prop in water, throttle input exactly 0).
inline double BoatCoastingYawAccel(const BoatParams& p, const BoatState& s, double speed, bool movingBackward) {
    double a = p.steeringWhileCoasting * Clamp(speed / p.maximumSpeed, 0.0, 1.0) * s.steer;
    return movingBackward ? -a : a;
}
inline double BoatLeanComX(const BoatParams& p, double baseX, const BoatState& s, double speed) {
    return baseX + p.leanComOffset * s.steer * Clamp(speed / 7.0, 0.0, 1.0);
}
// Out of water, off the ground: air control. Moves omega about forward (roll) and right (pitch) toward input*max.
inline void BoatAirControl(const BoatParams& p, CraftBody& b, double steerIn, double pitchIn, double dt) {
    const double wr = Dot(b.angVel, b.basis.fwd), wp = Dot(b.angVel, b.basis.right);
    const double tr = steerIn * p.maxRollSpeed, tp = pitchIn * p.maxPitchSpeed;
    const double dr = Clamp(tr - wr, -p.rollAccel * dt, p.rollAccel * dt);
    const double dp = Clamp(tp - wp, -p.pitchAccel * dt, p.pitchAccel * dt);
    b.angVel += b.basis.fwd * dr + b.basis.right * dp;
}

// Full controller step. inWater: vehicle +0xc8 bit 0; propUnderWater: the thrust-point test 0x00aed6e0.
inline void BoatStep(const BoatParams& p, BoatState& s, const BoatInput& in, bool inWater, bool propUnderWater, bool onGround,
                     CraftBody& b, double dt) {
    const double u = Dot(b.vel, b.basis.fwd);
    BoatThrustStateStep(p, s, in, u, dt);
    if (inWater) {
        BoatSmoothInputs(p, s, in, dt);
        const double speed = Len(b.vel);
        if (propUnderWater && s.thrust != 0.0) {
            const V3 F = BoatThrustForce(p, s, b);
            const V3 P = b.pos + b.basis.ToWorld(p.thrustPosition);
            if (p.lbs) {  // LBS: force at the centre of mass always; only the yaw-rate step is skipped at lean target 0
                b.ForceCom(F, dt);
                if (s.leanTarget != 0.0) {
                    const V3 vh{b.vel.x, 0, b.vel.z};
                    b.angVel.y = BoatLbsYawRate(p, s, Len(vh), u < 0.0, b.angVel.y, dt);
                }
            } else {
                b.ForceAt(F, P, dt);
            }
        }
        if (propUnderWater && in.throttle == 0.0) b.angVel.y += BoatCoastingYawAccel(p, s, speed, u < 0.0) * dt;
    } else if (!onGround) {
        BoatAirControl(p, b, in.steer, in.pitch, dt);
    }
    ApplySpinDamping(b, p.normalSpinDamping, p.collisionSpinDamping, p.collisionSpinThreshold, dt);  // step 3 (0x00ad9500)
    ApplyMaximumSpeedClamp(b, p.maximumSpeed, in.boost ? p.boostSpeedBonus : 0.0);  // 10.11.10; class 5 bonus with boost bit
}

// ---------------------------------------------------------------------------------------------------------------
// 10.13.6 Water (PARTIAL)
// ---------------------------------------------------------------------------------------------------------------
inline constexpr double kWaterDensity = 1000.0;  // 0x012a2d90
// F = (0, -1000 * V * g_y * k, 0) at the centre of buoyancy; g_y = -9.8.
inline V3 BuoyancyForce(double submergedVolume, double buoyancyModifier) {
    return {0.0, -kWaterDensity * submergedVolume * (-kGravity) * buoyancyModifier, 0.0};
}
// Submerged volume at which buoyancy equals weight (W1-W3).
inline double RestSubmergedVolume(double mass, double buoyancyModifier) { return mass / (kWaterDensity * buoyancyModifier); }
struct SubmergedSample {
    double volume = 0.0;
    V3 centroid;
};
// CHOSEN (Q-A8): slab approximation for a box of width x length x height whose centre is at centerY.
inline SubmergedSample SubmergedBoxApprox(V3 center, double halfHeight, double width, double length, double waterY) {
    SubmergedSample s;
    const double bottom = center.y - halfHeight;
    const double depth = Clamp(waterY - bottom, 0.0, 2.0 * halfHeight);
    s.volume = width * length * depth;
    s.centroid = {center.x, bottom + 0.5 * depth, center.z};
    return s;
}
// GUESS placeholder drag (10.13.8 item 2): v *= (1 - c*dt) while in water.
inline void WaterDragPlaceholder(CraftBody& b, double c, double dt) {
    const double k = std::max(0.0, 1.0 - c * dt);
    b.vel = b.vel * k;
    b.angVel = b.angVel * k;
}
inline void BuoyancyStep(CraftBody& b, const SubmergedSample& smp, double k, double dt) {
    if (smp.volume <= 0.0) return;
    b.ForceAt(BuoyancyForce(smp.volume, k), smp.centroid, dt);
}

// ---------------------------------------------------------------------------------------------------------------
// 10.13.8 helper: class -> law selection (Vehicle_Type: 0 automobile, 1 motorcycle, 2 airplane, 3 helicopter, 4 VTOL,
// 5 watercraft).
// ---------------------------------------------------------------------------------------------------------------
enum class CraftLaw { None, Airplane, Helicopter, Vtol, Watercraft };
inline CraftLaw CraftLawForVehicleClass(int vehicleClass) {
    switch (vehicleClass) {
        case 2: return CraftLaw::Airplane;
        case 3: return CraftLaw::Helicopter;
        case 4: return CraftLaw::Vtol;
        case 5: return CraftLaw::Watercraft;
        default: return CraftLaw::None;
    }
}

}  // namespace craft
}  // namespace sr3physics
