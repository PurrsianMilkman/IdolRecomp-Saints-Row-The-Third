// Unit tests for sr3physics/aircraft_watercraft.h - spec-physics-runtime.md Sec10.13 vectors
// H1-H10 (helicopter), P1-P10 (airplane), V1-V8 (VTOL), B1-B8 (watercraft), W1-W6 (water), D1-D4 (damage).
#include <cmath>
#include <cstdio>

#include "sr3physics/aircraft_watercraft.h"

using namespace sr3physics::craft;

static int g_fail = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                      \
            ++g_fail;                                                             \
        }                                                                         \
    } while (0)
static bool Near(double a, double b, double e) { return std::fabs(a - b) <= e; }
static const double dt = 1.0 / 30.0;
static const double kDeg = kPi / 180.0;

static HeliParams HeliStd() {
    HeliParams p;  // heli_standard_02
    p.mass = 1360;
    p.elevationSpeed = 25 * 0.44704;
    p.elevationAccel = 9;
    p.elevationDamp = 1.25;
    p.autolevelAccel = 155 * kDeg;
    p.steeringAccel = 0.05;
    p.pitchBias = 14;
    p.tailAccel = 4;
    p.tailRotorCenter = {0, -0.12, -7};
    p.rotorRadius = 4;
    p.extraSpeedAccel = 45;
    p.tiltLiftLoss = 0;
    p.dragCoefficient = 0.2;
    p.maximumSpeed = 70 * 0.44704;
    return p;
}

static PlaneParams PlaneStd() {
    PlaneParams p;  // plane_standard01
    p.mass = 1500;
    p.engineAccel = 7.5;
    p.engineReverseAccel = 4.5;
    p.rampUp = p.rampDown = 2.0;
    p.reverseSpeed = 10 * 0.44704;
    p.liftMinSpeed = 65 * 0.44704;
    p.rollMax = 95 * kDeg;
    p.rollDamp = 20 * kDeg;
    p.horizMax = 52 * kDeg;
    p.horizAccel = p.horizDecel = 100 * kDeg;
    p.vertMax = 32 * kDeg;
    p.vertAccel = 172 * kDeg;
    p.turnLinearAccel = 600;
    p.barrelRollMax = 350 * kDeg;
    return p;
}

static BoatParams BoatStd() {
    BoatParams p;  // boat_speed01
    p.mass = 8600;
    p.maxForwardAccel = 20;
    p.maxReverseAccel = 7;
    p.rampUp = p.rampDown = 1.0;
    p.steeringInputRate = 3;
    p.lbs = true;
    p.lbsMaxLean = 57 * kDeg;
    p.lbsMinRadius = 10;
    p.lbsTurnRadiusModifier = 0.5;
    p.lbsCorrectionAccel = 1000 * kDeg;
    p.steeringWhileCoasting = 5;
    p.maximumSpeed = 65 * 0.44704;
    return p;
}

// Net torque of a force set about the body centre.
static V3 TorqueAbout(V3 c, const RotorForce* f, int n) {
    V3 t;
    for (int i = 0; i < n; ++i) t += Cross(f[i].point - c, f[i].force);
    return t;
}

static void TestHeli() {
    const HeliParams p = HeliStd();
    // H1: level, c = 0, vy = 0, piloted: a_lim = 0, a = 9.8.
    {
        HeliState s;
        HeliCollective(p, s, 0.0, 1.0, 0.0, true, true, dt);
        CHECK(Near(s.aLim, 0.0, 1e-12));
        CHECK(Near(s.accel, 9.8, 1e-12));
    }
    // H2: hover c = 1: v_t = 11.176; a_lim rises <= 32.4 m/s^3 to <= 9; about 1.5 s to 11.18 m/s.
    {
        HeliState s;
        double vy = 0.0, tReach = -1.0, maxA = 0.0, jerkMax = 0.0, prev = 0.0;
        for (int i = 0; i < 300; ++i) {
            HeliCollective(p, s, 1.0, 1.0, vy, true, true, dt);
            maxA = std::max(maxA, s.aLim);
            jerkMax = std::max(jerkMax, (s.aLim - prev) / dt);
            prev = s.aLim;
            vy += s.aLim * dt;
            if (tReach < 0 && vy >= 11.176 - 0.05) tReach = (i + 1) * dt;
        }
        CHECK(maxA <= 9.0 + 1e-9);
        CHECK(jerkMax <= 32.4 + 1e-6);
        CHECK(tReach > 1.2 && tReach < 1.9);
        CHECK(Near(vy, 11.176, 0.05));
    }
    // H3: nose down 20 deg, c = 0: horizontal accel 45 sin20 + 9.8 sin20 = 18.74.
    {
        CraftBody b;
        b.mass = p.mass;
        b.basis = BasisNoseDown(20 * kDeg);
        const V3 fx = HeliExtraSpeedForce(p, b, 0.0);
        const double ax = fx.z / b.mass;
        CHECK(Near(Len(fx) / b.mass, 45 * std::sin(20 * kDeg), 1e-9));
        CHECK(Near(ax, 15.39, 0.01));
        // lift along D (= up, tilt 0) with a = 9.8: horizontal part 9.8 sin20 = 3.35; vertical shortfall 0.59
        RotorForce rf[12];
        HeliRotorForces(p, b, 9.8, 0.0, 0.0, false, rf);
        V3 sum;
        for (auto& r : rf) sum += r.force;
        CHECK(Near(sum.z / b.mass, 9.8 * std::sin(20 * kDeg), 1e-9));
        CHECK(Near(9.8 - sum.y / b.mass, 0.59, 0.005));
        CHECK(Near(ax + sum.z / b.mass, 18.74, 0.01));
    }
    // H4: nose down 60 deg: extra speed uses the 45 deg cap = 45 * 0.70711.
    {
        CraftBody b;
        b.mass = p.mass;
        b.basis = BasisNoseDown(60 * kDeg);
        CHECK(Near(Len(HeliExtraSpeedForce(p, b, 0.0)) / b.mass, 31.82, 0.005));
    }
    // H5: tilt 20 deg, w = 0: dw per 1/30 s = 0.0425; at 90 deg 0.0902.
    {
        CraftBody b;
        b.basis = BasisNoseDown(20 * kDeg);
        HeliAutoLevel(p, b, dt);
        CHECK(Near(Len(b.angVel), 0.0425, 0.0001));
        CraftBody c;
        c.basis = BasisNoseDown(90 * kDeg);
        HeliAutoLevel(p, c, dt);
        CHECK(Near(Len(c.angVel), 0.0902, 0.0001));
        // direction: pitched nose down 20 deg -> torque back toward level (rotation about -x lifts the nose)
        CHECK(b.angVel.x < 0.0);
    }
    // H6: cyclic full forward (psi = 3pi/2): g = 1, k_max = 0.7, pitch torque 18949 N m nose down.
    {
        CraftBody b;
        b.mass = p.mass;
        RotorForce rf[12];
        double m, psi;
        HeliCyclicFromXY(0.0, -1.0, m, psi);
        CHECK(Near(m, 1.0, 1e-12) && Near(psi, 1.5 * kPi, 1e-12));
        HeliRotorForces(p, b, 0.0, m, psi, false, rf);
        const V3 t = TorqueAbout({0, 0, 0}, rf, 12);
        CHECK(Near(t.x, 18949.0, 2.0));  // +x torque lowers the nose (rear lifts)
        CHECK(Near(t.y, 0.0, 1e-6) && Near(std::fabs(t.z), 0.0, 1.0));
        // settled: no cyclic
        HeliRotorForces(p, b, 0.0, m, psi, true, rf);
        CHECK(Near(Len(TorqueAbout({0, 0, 0}, rf, 12)), 0.0, 1e-6));
        // net force is a pure lift when a = 9.8: sum = M*a along D
        HeliRotorForces(p, b, 9.8, m, psi, false, rf);
        V3 sum;
        for (auto& r : rf) sum += r.force;
        CHECK(Near(sum.y, p.mass * 9.8, 1e-6));
    }
    // H7: cyclic full right (psi = pi): g = 0, roll torque 1353 N m; diagonal 5pi/4: g = 0.
    {
        CraftBody b;
        b.mass = p.mass;
        RotorForce rf[12];
        double m, psi;
        HeliCyclicFromXY(-1.0, 0.0, m, psi);
        CHECK(Near(psi, kPi, 1e-12));
        HeliRotorForces(p, b, 0.0, m, psi, false, rf);
        const V3 t = TorqueAbout({0, 0, 0}, rf, 12);
        CHECK(Near(std::fabs(t.z), 1353.0, 1.5));
        CHECK(Near(Len(t) / 18949.0, 1353.0 / 18949.0, 1e-3));
        // diagonal: identical to the g = 0 weight (k_max = 0.05)
        HeliRotorForces(p, b, 0.0, 1.0, 1.25 * kPi, false, rf);
        const V3 td = TorqueAbout({0, 0, 0}, rf, 12);
        HeliParams nb = p;
        nb.pitchBias = 1.0;  // g has no effect when the weight is 1: the diagonal must equal that
        RotorForce r2[12];
        HeliRotorForces(nb, b, 0.0, 1.0, 1.25 * kPi, false, r2);
        CHECK(Near(Len(td), Len(TorqueAbout({0, 0, 0}, r2, 12)), 1e-6));
    }
    // H8: TURN_LEFT held: +0x950 = +1, yaw torque -28*M about y.
    {
        CHECK(HeliYawFromKeys(true, false) == 1.0);
        CHECK(HeliYawFromKeys(false, true) == -1.0);
        CraftBody b;
        b.mass = p.mass;
        const V3 f = HeliTailForce(p, b, 1.0, true);
        CHECK(Near(f.x, 4.0 * b.mass, 1e-9));
        const V3 tail = b.pos + b.basis.ToWorld(p.tailRotorCenter);
        const V3 tq = Cross(tail - b.pos, f);
        CHECK(Near(tq.y, -28.0 * b.mass, 1e-6));
    }
    // H9: camera 30 deg off, stick deflected: auto-yaw 0.667; 60 deg: 1.0.
    {
        HeliCameraRelative r = HeliApplyCameraRelative(1.5 * kPi, 1.0, 30 * kDeg, 0.0, false);
        CHECK(r.autoYawed && Near(r.yaw, 0.5236 / 0.7854, 1e-3));
        r = HeliApplyCameraRelative(1.5 * kPi, 1.0, 60 * kDeg, 0.0, false);
        CHECK(Near(r.yaw, 1.0, 1e-12));
        r = HeliApplyCameraRelative(1.5 * kPi, 1.0, 60 * kDeg, 0.25, true);  // turn key held: key yaw kept
        CHECK(!r.autoYawed && Near(r.yaw, 0.25, 1e-12));
        r = HeliApplyCameraRelative(1.5 * kPi, 0.05, 60 * kDeg, 0.0, false);  // m < 0.1
        CHECK(!r.autoYawed);
        CHECK(Near(HeliApplyCameraRelative(0.5, 1.0, -1.0, 0, false).psi, 2 * kPi - 0.5, 1e-12));
    }
    // H10: y = 1400, vy = +10, 3.0 s above 1333 m: k = 0.5, vy = 4.837; after 6 s any positive vy -> -0.327.
    {
        AltitudeCeilingState st;
        V3 v{0, 10.0, 0};
        for (int i = 0; i < 89; ++i) ApplyAltitudeCeiling(st, 1400.0, v, dt);  // accumulate 89 frames with vy kept
        (void)v;
        AltitudeCeilingState st2;
        st2.t = 3.0 - dt;
        V3 v2{0, 10.0, 0};
        ApplyAltitudeCeiling(st2, 1400.0, v2, dt);
        CHECK(Near(st2.t, 3.0, 1e-12));
        CHECK(Near(v2.y, 5.0 - 9.8 * 0.5 / 30.0, 1e-9));
        CHECK(Near(v2.y, 4.837, 0.001));
        AltitudeCeilingState st3;
        st3.t = 6.0 - dt;
        V3 v3{0, 3.0, 0};
        ApplyAltitudeCeiling(st3, 1400.0, v3, dt);
        CHECK(Near(v3.y, -0.327, 0.001));
        AltitudeCeilingState st4;
        st4.t = 5.0;
        V3 v4{0, 3.0, 0};
        ApplyAltitudeCeiling(st4, 1000.0, v4, dt);  // below ceiling: reset, untouched
        CHECK(st4.t == 0.0 && v4.y == 3.0);
    }
    // Settle timer / damping / drag / full step.
    {
        HeliState s;
        HeliSettleTimer(s, true, 1.0, dt);
        CHECK(s.settleMs == 800.0);
        HeliSettleTimer(s, true, 0.7, dt);  // tilt > 40 deg while resting -> 499
        CHECK(s.settleMs == 499.0);
        HeliSettleTimer(s, false, 1.0, dt);
        CHECK(Near(s.settleMs, 499.0 - 1000.0 * dt, 1e-9));
        CraftBody b;
        b.vel = {10, 0, 0};
        HeliLinearDrag(p, b, dt);
        CHECK(Near(b.vel.x, 10.0 * (1.0 - 0.2 / 30.0), 1e-12));
        // full step, hovering: net altitude held, a stable idle (a=9.8 lift vs gravity applied by the caller)
        HeliState hs;
        CraftBody hb;
        hb.mass = p.mass;
        hb.pos = {0, 100, 0};
        HeliInput in;
        HeliFlags fl;
        for (int i = 0; i < 30; ++i) {
            HeliStep(p, hs, in, fl, hb, dt);
            hb.vel.y -= 9.8 * dt;  // caller-applied gravity
        }
        CHECK(Near(hs.accel, 9.8, 0.2));  // the limiter trims the small damping sink
        CHECK(Near(hb.vel.y, 0.0, 0.01));
        // tail loss: lift comp 4.9
        HeliState ts;
        HeliCollective(p, ts, 0.0, 1.0, 0.0, false, true, dt);
        CHECK(Near(ts.accel, 4.9, 1e-12));
    }
    // 0x00add4a0 resting predicate
    {
        HeliParams q = p;
        CHECK(HeliRestPredicate(q, true, 2.0, 0.0));
        CHECK(!HeliRestPredicate(q, true, 4.0, 0.0));
        CHECK(!HeliRestPredicate(q, false, 0.0, 0.0));
        CHECK(!HeliRestPredicate(q, true, 2.0, 0.5));
    }
}

static void TestPlane() {
    const PlaneParams p = PlaneStd();
    // P1/P2: lift fraction.
    {
        CHECK(Near(PlaneLiftFraction(p, 29.06), 1.0, 1e-3));
        CHECK(Near(PlaneLiftFraction(p, p.liftMinSpeed / 2.0), 0.25, 1e-9));
        CHECK(Near(9.8 * PlaneLiftFraction(p, 14.529), 2.45, 1e-3));
        CHECK(Near(9.8 - 2.45, 7.35, 1e-9));
        CraftBody b;
        b.vel = {0, 0, 29.058};
        PlaneState s;
        PlaneInput in;
        PlaneFlags fl;
        PlaneParams nt = p;
        nt.turnLinearAccel = 0.0;  // isolate the lift from the nose-follows-velocity realignment
        PlaneStep(nt, s, in, fl, b, dt);
        CHECK(Near(b.vel.y, 9.8 * dt, 1e-3));  // P1: L = 1, lift 9.8 m/s^2
        CraftBody b2;
        b2.vel = {0, 0, p.liftMinSpeed / 2.0};
        PlaneState s2;
        PlaneStep(nt, s2, in, fl, b2, dt);
        CHECK(Near(b2.vel.y, 2.45 * dt, 1e-3));  // P2: L = 0.25 (net sink 7.35 with gravity)
    }
    // P3: full throttle from a = 0: a after 1 s = 3.75, after 2 s = 7.5; force 11250 N.
    {
        PlaneState s;
        double F = 0;
        for (int i = 0; i < 30; ++i) F = PlaneEngineStep(p, s, 1.0, 0.0, false, false, dt);
        CHECK(Near(s.engineAccel, 3.75, 1e-9));
        for (int i = 0; i < 30; ++i) F = PlaneEngineStep(p, s, 1.0, 0.0, false, false, dt);
        CHECK(Near(s.engineAccel, 7.5, 1e-9));
        CHECK(Near(F, 11250.0, 1e-6));
        // beyond 2 s: stays clamped
        for (int i = 0; i < 30; ++i) PlaneEngineStep(p, s, 1.0, 0.0, false, false, dt);
        CHECK(Near(s.engineAccel, 7.5, 1e-9));
    }
    // P4: full roll stick, level: phi_t = pi/3; max rate 1.6581; braking 3.94 rad/s^2.
    {
        PlaneInput in;
        in.roll = 1.0;
        CHECK(Near(PlaneBankTarget(in, 1, 1), kPi / 3.0, 1e-12));
        CHECK(Near(p.rollMax, 1.6581, 1e-3));
        CHECK(Near(p.rollMax * p.rollMax / (2.0 * p.rollDamp), 3.94, 0.01));
        // drive the limiter: rate saturates at the max rate
        double phi = 0.0, rate = 0.0, peak = 0.0;
        const double A = p.rollMax * p.rollMax / (2.0 * p.rollDamp);
        for (int i = 0; i < 90; ++i) {
            rate = MinTimeLimiterStep(phi, rate, kPi / 3.0, p.rollMax, A, dt);
            phi += rate * dt;
            peak = std::max(peak, rate);
        }
        CHECK(Near(peak, 1.6581, 1e-3));
        CHECK(Near(phi, kPi / 3.0, 0.01));
    }
    // P5/P6/P7 via the full step with the airplane at u >= Lift_Min_Speed.
    {
        // P5: banked 60 deg, full roll stick held: yaw-rate target 0.667 * 0.9076 = 0.605 reached at 1.745 rad/s^2.
        CraftBody b;
        b.mass = p.mass;
        // bank 60 deg right-wing-down about fwd = z: right = (cos, -sin, 0)
        b.basis.right = {std::cos(60 * kDeg), -std::sin(60 * kDeg), 0};
        b.basis.up = {std::sin(60 * kDeg), std::cos(60 * kDeg), 0};
        CHECK(Near(PlaneBank(b.basis), 60 * kDeg, 1e-9));
        b.vel = {0, 0, 40};
        PlaneState s;
        PlaneInput in;
        in.roll = 1.0;
        PlaneFlags fl;
        double last = 0.0;
        PlaneStep(p, s, in, fl, b, dt);
        CHECK(Near(std::fabs(s.yawRate), 1.7453 * dt, 1e-4));  // accel-limited first step
        for (int i = 0; i < 60; ++i) {
            b.basis.right = {std::cos(60 * kDeg), -std::sin(60 * kDeg), 0};
            b.basis.up = {std::sin(60 * kDeg), std::cos(60 * kDeg), 0};
            b.basis.fwd = {0, 0, 1};
            b.vel = {0, 0, 40};
            PlaneStep(p, s, in, fl, b, dt);
            last = s.yawRate;
        }
        CHECK(Near(std::fabs(last), 0.667 * 0.9076, 0.005));
    }
    {
        // P6: full pitch, level / 90 deg bank: 0.5585 / 0.279 rad/s, limit 3.002 rad/s^2.
        PlaneInput in;
        in.pitch = 1.0;
        PlaneFlags fl;
        PlaneState s;
        CraftBody b;
        b.vel = {0, 0, 40};
        PlaneStep(p, s, in, fl, b, dt);
        CHECK(Near(std::fabs(s.pitchRate), 3.0020 * dt, 1e-4));
        for (int i = 0; i < 60; ++i) {
            b.basis = Basis();
            b.vel = {0, 0, 40};
            PlaneStep(p, s, in, fl, b, dt);
        }
        CHECK(Near(std::fabs(s.pitchRate), 0.5585, 1e-3));
        PlaneState s2;
        CraftBody b2;
        for (int i = 0; i < 60; ++i) {
            b2.basis.right = {0, -1, 0};
            b2.basis.up = {1, 0, 0};
            b2.basis.fwd = {0, 0, 1};
            b2.vel = {0, 0, 40};
            PlaneStep(p, s2, in, fl, b2, dt);
        }
        CHECK(Near(PlaneBankTriangle(90 * kDeg), 1.0, 1e-12));
        CHECK(Near(std::fabs(s2.pitchRate), 0.279, 1e-3));
        CHECK(Near(PlaneBankTriangle(0.0), 0.0, 1e-12) && Near(PlaneBankTriangle(kPi), 0.0, 1e-12));
    }
    {
        // P7: slip 10 deg at u >= Lift_Min_Speed: lateral accel 600 sin10 = 104.2 m/s^2 toward the nose.
        CraftBody b;
        b.mass = p.mass;
        const double u = 40.0;
        // velocity direction rotated 10 deg away from the nose (nose along +z, velocity toward +x side)
        // vy pre-set so that the damped velocity plus the step's lift (9.8 dt) is exactly horizontal
        b.vel = {u * std::sin(10 * kDeg), -9.8 * dt / (1.0 - 0.01 * dt), u * std::cos(10 * kDeg)};
        PlaneState s;
        PlaneInput in;
        PlaneFlags fl;
        const V3 v0 = b.vel;
        PlaneStep(p, s, in, fl, b, dt);
        // forward speed = v . fwd = 40 cos10 > LiftMin, L = 1
        // acceleration magnitude perpendicular to v: 600 sin 10 (damping excluded: compare before damping)
        CraftBody c;
        c.vel = v0;
        PlaneParams q = p;
        q.enginePoints.clear();
        PlaneState s2;
        // damping and lift also act; isolate via difference with a zero-turn plane
        PlaneParams z = q;
        z.turnLinearAccel = 0.0;
        CraftBody c0;
        c0.vel = v0;
        PlaneStep(q, s2, in, fl, c, dt);
        PlaneState s3;
        PlaneStep(z, s3, in, fl, c0, dt);
        const V3 turn = (c.vel - c0.vel) * (1.0 / dt);
        CHECK(Near(Len(turn), 600.0 * std::sin(10 * kDeg), 0.05));
        CHECK(Near(Len(turn), 104.2, 0.05));
        CHECK(turn.x < 0.0);  // toward the nose
    }
    // P8: rudder held: phi_t = +-90 deg.
    {
        PlaneInput in;
        in.rudder = 0.5;
        CHECK(Near(PlaneBankTarget(in, 1, 1), kPi / 2, 1e-12));
        in.rudder = -0.5;
        CHECK(Near(PlaneBankTarget(in, 1, 1), -kPi / 2, 1e-12));
        in.rudder = 0.05;
        in.roll = 0.0;
        CHECK(PlaneBankTarget(in, 1, 1) == 0.0);
        in.inverted = true;
        CHECK(Near(PlaneBankTarget(in, 1, 1), kPi, 1e-12));
    }
    // P9: barrel roll: 6.1087 rad/s for pi/6.1087 = 0.514 s.
    {
        PlaneState s;
        PlaneBarrelRollStart(s);
        CraftBody b;
        b.vel = {0, 0, 40};
        PlaneInput in;
        PlaneFlags fl;
        double t = 0.0;
        while (s.barrelActive && t < 2.0) {
            b.basis = Basis();
            b.vel = {0, 0, 40};
            PlaneStep(p, s, in, fl, b, dt);
            t += dt;
            if (s.barrelActive) CHECK(Near(s.rollRate, 6.1087, 1e-3));
        }
        CHECK(Near(t, kPi / 6.1087, dt + 1e-9));
        CHECK(Near(kPi / 6.1087, 0.514, 0.001));
    }
    // P10: reversing on the ground: forward speed floor -4.470.
    {
        PlaneState s;
        s.engineAccel = -4.5;
        const double fl = -p.reverseSpeed;  // 10 mph = 4.4704 (spec rounds to 4.470)
        double F = PlaneEngineStep(p, s, -1.0, fl + 0.07, false, true, dt);
        // a limited so that u + a dt >= floor
        CHECK(fl + 0.07 + s.engineAccel * dt >= fl - 1e-9);
        CHECK(Near(s.engineAccel, -2.1, 1e-6));
        CHECK(Near(F, p.mass * s.engineAccel, 1e-9));
        PlaneState s2;
        s2.engineAccel = -4.5;
        PlaneEngineStep(p, s2, -1.0, fl, false, true, dt);
        CHECK(Near(s2.engineAccel, 0.0, 1e-9));
        CHECK(Near(p.reverseSpeed, 4.470, 1e-3));
    }
    // partial ground contact scales the reverse bound by clamp(u / 13.4112)
    {
        PlaneState s;
        s.engineAccel = -4.0;
        PlaneEngineStep(p, s, -1.0, 6.7056, true, false, 1.0);
        CHECK(Near(s.engineAccel, -2.25, 1e-9));
    }
    // circle-to-square remap
    {
        double x, y;
        PlaneCircleToSquare(1.0, 0.0, x, y);
        CHECK(Near(x, 1.0, 1e-12) && Near(y, 0.0, 1e-12));
        PlaneCircleToSquare(std::sqrt(0.5), std::sqrt(0.5), x, y);
        CHECK(Near(x, 1.0, 1e-9) && Near(y, 1.0, 1e-9));
        PlaneCircleToSquare(0.0, 0.5, x, y);
        CHECK(Near(x, 0.0, 1e-12) && Near(y, 0.5, 1e-12));
    }
    // D2/D3 wing damage
    {
        PlaneInput in;
        in.roll = 0.0;
        CHECK(Near(PlaneWingIntegrity(0.0, 1.0), 0.5, 1e-12));
        CHECK(Near(PlaneBankTarget(in, 0.0, 1.0), kPi / 3.0, 1e-12));  // roll command +1
        CHECK(Near(PlaneWingIntegrity(PlaneWingFactor(1, 2), PlaneWingFactor(1, 2)), 0.5, 1e-12));
        CHECK(PlaneBankTarget(in, 0.5, 0.5) == 0.0);  // no roll bias
        // D2: lift halved
        CraftBody b;
        b.vel = {0, 0, 40};
        PlaneState s;
        PlaneFlags fl;
        fl.wingLeft = 0.0;
        fl.wingRight = 1.0;
        PlaneParams nt = p;
        nt.turnLinearAccel = 0.0;
        PlaneStep(nt, s, in, fl, b, dt);
        CHECK(Near(b.vel.y, 0.5 * 9.8 * dt, 1e-3));
    }
}

static void TestVtol() {
    // V3/V4 jet kick
    {
        CHECK(VtolJetKickAccel(0.0) == 50.0);
        CHECK(Near(VtolJetKickAccel(295.0), 25.0, 1e-12));
        CHECK(VtolJetKickAccel(300.0) == 0.0);
        CHECK(VtolJetKickAccel(350.0) == 0.0);
        VtolState s;
        s.request = 0;
        VtolApplyRequest(s);
        CHECK(s.heliModel == 0 && s.kickMs == 1200.0);
        CraftBody b;
        b.mass = 12000;
        for (int i = 0; i < 40; ++i) VtolKickStep(s, b, dt);
        CHECK(Near(b.vel.z, 60.0, 0.5));  // +50 m/s^2 for 1.2 s from the kick alone
        CHECK(s.kickMs == 0.0);
    }
    // V5: L = 1 after V3 with Lift_Min_Speed 75 mph.
    {
        PlaneParams p;
        p.liftMinSpeed = 75 * 0.44704;
        CHECK(Near(p.liftMinSpeed, 33.53, 0.005));
        CHECK(Near(PlaneLiftFraction(p, 60.0), 1.0, 1e-12));
    }
    // V1: hover with Elevation_Speed 40 mph = 17.88, Elevation_Accel 16
    {
        HeliParams p = HeliStd();
        p.elevationSpeed = 40 * 0.44704;
        p.elevationAccel = 16;
        CHECK(Near(p.elevationSpeed, 17.88, 0.005));
        HeliState s;
        double vy = 0.0;
        for (int i = 0; i < 300; ++i) {
            HeliCollective(p, s, 1.0, 1.0, vy, VtolRotorHealthy(), true, dt);
            vy += s.aLim * dt;
        }
        CHECK(Near(vy, 17.88, 0.05));
    }
    // V2 + V6 + V7: state machine and reversal.
    {
        VtolState s;
        VtolMachineStep(s, true, false, false);
        CHECK(s.machine == 1 && s.heliModel == 1);  // still hover until the wing animation ends
        VtolMachineStep(s, false, false, false);
        CHECK(s.machine == 1 && s.heliModel == 1);
        VtolMachineStep(s, false, true, false);
        CHECK(s.machine == 2 && s.request == 0);
        VtolApplyRequest(s);
        CHECK(s.heliModel == 0);
        VtolMachineStep(s, true, false, false);  // press again: back to hover
        CHECK(s.machine == 3 && s.request == 1);
        VtolApplyRequest(s);
        CHECK(s.heliModel == 1);  // V6: next step, no ramp
        VtolMachineStep(s, false, false, true);
        CHECK(s.machine == 0);
        VtolState r;
        r.camElapsedMs = 800.0;
        r.request = 0;
        VtolApplyRequest(r);
        CHECK(Near(r.camElapsedMs, 1200.0, 1e-9));  // V7
    }
    // V8: VTOL never gets the tail-loss spin.
    CHECK(VtolRotorHealthy());
    CHECK(CraftLawForVehicleClass(4) == CraftLaw::Vtol && CraftLawForVehicleClass(3) == CraftLaw::Helicopter &&
          CraftLawForVehicleClass(2) == CraftLaw::Airplane && CraftLawForVehicleClass(5) == CraftLaw::Watercraft &&
          CraftLawForVehicleClass(0) == CraftLaw::None);
}

static void TestBoat() {
    const BoatParams p = BoatStd();
    // B1: throttle 0 -> 1: thrust state 1.0 after 1 s; full force 172000 N.
    {
        BoatState s;
        BoatInput in;
        in.throttle = 1.0;
        for (int i = 0; i < 30; ++i) BoatThrustStateStep(p, s, in, 0.0, dt);
        CHECK(Near(s.thrust, 1.0, 1e-9));
        CraftBody b;
        b.mass = p.mass;
        CHECK(Near(Len(BoatThrustForce(p, s, b)), 172000.0, 1e-6));
    }
    // B2: throttle +1 -> -1 at state 0.6: reset to 0, then falls at 1/s.
    {
        BoatState s;
        s.thrust = 0.6;
        BoatInput in;
        in.throttle = -1.0;
        BoatThrustStateStep(p, s, in, 0.0, dt);
        CHECK(Near(s.thrust, -dt, 1e-9));
        for (int i = 0; i < 29; ++i) BoatThrustStateStep(p, s, in, 0.0, dt);
        CHECK(Near(s.thrust, -1.0, 1e-9));
    }
    // stop assist
    {
        BoatState s;
        BoatInput in;
        in.stopAssist = true;
        BoatThrustStateStep(p, s, in, 0.1, 1.0);  // u = 0.1 forward: target = -0.1/(1*7)
        CHECK(s.thrust < 0.0);
        CHECK(Near(s.thrust, -0.1 / 7.0, 1e-9));
    }
    // B3: prop above the water: no thrust; out of water: air control only.
    {
        BoatState s;
        s.thrust = 1.0;
        BoatInput in;
        in.throttle = 1.0;
        CraftBody b;
        b.mass = p.mass;
        BoatStep(p, s, in, true, false, false, b, dt);
        CHECK(Len(b.vel) == 0.0);
        CraftBody c;
        c.mass = p.mass;
        BoatState s2;
        s2.thrust = 1.0;
        in.steer = 1.0;
        BoatStep(p, s2, in, false, false, false, c, dt);
        CHECK(Len(c.vel) == 0.0);
        CHECK(Len(c.angVel) > 0.0);  // air control
        CHECK(Near(Len(c.angVel), p.rollAccel * dt, 1e-9));
        // thrust under water accelerates
        CraftBody d;
        d.mass = p.mass;
        BoatState s3;
        s3.thrust = 1.0;
        BoatInput in2;
        in2.throttle = 1.0;
        BoatStep(p, s3, in2, true, true, false, d, dt);
        CHECK(Near(d.vel.z, 20.0 * dt, 1e-6));
    }
    // B4/B5/B6: LBS yaw rate.
    {
        BoatState s;
        s.thrust = 1.0;
        s.leanTarget = 57 * kDeg;
        s.pitch = 0.0;
        double w = BoatLbsYawRate(p, s, 20.0, false, 0.0, 10.0);  // large dt: target reached
        const double R = 400.0 / (9.8 * std::tan(0.9948));
        CHECK(Near(R, 26.5, 0.05));
        CHECK(Near(w, 0.755, 0.002));
        w = BoatLbsYawRate(p, s, 5.0, false, 0.0, 10.0);  // R = 1.66 -> floor 10
        CHECK(Near(w, 0.5, 1e-9));
        s.pitch = 1.0;
        w = BoatLbsYawRate(p, s, 20.0, false, 0.0, 10.0);
        CHECK(Near(w, 1.51, 0.005));
        // approached at <= 17.45 rad/s^2
        w = BoatLbsYawRate(p, s, 20.0, false, 0.0, dt);
        CHECK(Near(w, 17.4533 * dt, 1e-4));
        // reversing negates; zero lean target or zero thrust skips
        CHECK(BoatLbsYawRate(p, s, 20.0, true, 0.0, 10.0) < 0.0);
        s.leanTarget = 0.0;
        CHECK(BoatLbsYawRate(p, s, 20.0, false, 0.3, 1.0) == 0.3);
    }
    // B7: steer stick 0 -> 1: +0x90 = 1 after 0.333 s.
    {
        BoatState s;
        BoatInput in;
        in.steer = 1.0;
        for (int i = 0; i < 10; ++i) BoatSmoothInputs(p, s, in, dt);
        CHECK(Near(s.steer, 1.0, 1e-9));
        CHECK(Near(s.leanTarget, 57 * kDeg, 1e-9));
        BoatState t;
        for (int i = 0; i < 9; ++i) BoatSmoothInputs(p, t, in, dt);
        CHECK(t.steer < 1.0);
    }
    // B8: coasting at 10 m/s, steer 1: yaw accel = 5 * 10/29.06 = 1.72.
    {
        BoatState s;
        s.steer = 1.0;
        CHECK(Near(BoatCoastingYawAccel(p, s, 10.0, false), 1.72, 0.01));
        CHECK(Near(BoatCoastingYawAccel(p, s, 10.0, true), -1.72, 0.01));
        CHECK(Near(BoatCoastingYawAccel(p, s, 100.0, false), 5.0, 1e-9));  // clamped
        CHECK(Near(BoatLeanComX(BoatParams{}, 0.0, s, 7.0), 0.0, 1e-12));
    }
}

static void TestWater() {
    // W1-W3: rest submerged volume = M / (1000 k).
    CHECK(Near(RestSubmergedVolume(8600.0, 0.8), 10.75, 1e-9));
    CHECK(Near(RestSubmergedVolume(800.0, 1.0), 0.80, 1e-9));
    CHECK(Near(RestSubmergedVolume(20000.0, 0.7), 28.57, 0.005));
    // buoyancy = weight at rest volume
    CHECK(Near(BuoyancyForce(10.75, 0.8).y, 8600.0 * 9.8, 1e-6));
    // W4: car without Water_Physics: k = default/group modifier (passes straight through)
    CHECK(Near(BuoyancyForce(1.0, 1.0).y, 9800.0, 1e-9));
    // W5: C 0.5 m aft of the COM: pitch torque = 0.5 * F
    {
        CraftBody b;
        b.mass = 1000.0;
        SubmergedSample smp;
        smp.volume = 1.0;
        smp.centroid = {0, 0, -0.5};
        const V3 F = BuoyancyForce(smp.volume, 1.0);
        CHECK(Near(Cross(smp.centroid - b.pos, F).x, 0.5 * F.y, 1e-9));
        BuoyancyStep(b, smp, 1.0, 1.0);
        CHECK(Len(b.angVel) > 0.0);
    }
    // W6: fully out: V = 0, no force
    {
        CraftBody b;
        SubmergedSample smp = SubmergedBoxApprox({0, 10, 0}, 1.0, 2.0, 4.0, 0.0);
        CHECK(smp.volume == 0.0);
        BuoyancyStep(b, smp, 1.0, dt);
        CHECK(Len(b.vel) == 0.0 && Len(b.angVel) == 0.0);
        const SubmergedSample half = SubmergedBoxApprox({0, 0, 0}, 1.0, 2.0, 4.0, 0.0);
        CHECK(Near(half.volume, 8.0, 1e-12) && Near(half.centroid.y, -0.5, 1e-12));
        // placeholder drag
        CraftBody d;
        d.vel = {10, 0, 0};
        WaterDragPlaceholder(d, 0.5, 1.0);
        CHECK(Near(d.vel.x, 5.0, 1e-12));
    }
}

static void TestDamage() {
    const HeliParams p = HeliStd();
    // D1: tail rotor shot off, no yaw input: constant yaw torque -28*M; net sink 4.9 m/s^2 at c = 0.
    {
        CraftBody b;
        b.mass = p.mass;
        const V3 f = HeliTailForce(p, b, 0.0, false);
        const V3 tail = b.pos + b.basis.ToWorld(p.tailRotorCenter);
        CHECK(Near(Cross(tail - b.pos, f).y, -28.0 * b.mass, 1e-6));
        HeliState s;
        HeliCollective(p, s, 0.0, 1.0, 0.0, false, true, dt);
        CHECK(Near(s.accel - 9.8, -4.9, 1e-12));
    }
    // D4: VTOL hit anywhere: never the tail-loss spin (rotor healthy forced true).
    {
        CHECK(VtolRotorHealthy());
        CraftBody b;
        b.mass = p.mass;
        const V3 f = HeliTailForce(p, b, 0.0, VtolRotorHealthy());
        CHECK(Len(f) == 0.0);
    }
    // wing factor
    CHECK(Near(PlaneWingFactor(1, 2), 0.5, 1e-12) && Near(PlaneWingFactor(0, 0), 1.0, 1e-12));
}

// R5 review fixes: AW-4 speed clamp, AW-6 signed barrel roll, AW-9 boat spin damping, AW-10 LBS at lean 0.
static void TestReviewR5() {
    // AW-4: clamp rescales the full 3-D velocity to exactly cap (+ bonus).
    {
        CraftBody b;
        b.vel = {30, 40, 0};  // 50 m/s
        ApplyMaximumSpeedClamp(b, 25.0);
        CHECK(Near(Len(b.vel), 25.0, 1e-9) && Near(b.vel.y / b.vel.x, 40.0 / 30.0, 1e-9));
        b.vel = {3, 4, 0};
        ApplyMaximumSpeedClamp(b, 25.0, 0.0);
        CHECK(Near(Len(b.vel), 5.0, 1e-12));
        ApplyMaximumSpeedClamp(b, 0.0);  // unset cap: untouched
        CHECK(Near(Len(b.vel), 5.0, 1e-12));
        b.vel = {0, 0, 50};
        ApplyMaximumSpeedClamp(b, 25.0, 5.0);  // bonus raises the limit
        CHECK(Near(Len(b.vel), 30.0, 1e-9));
    }
    {  // heli step clamps
        const HeliParams p = HeliStd();
        HeliState s;
        CraftBody b;
        b.mass = p.mass;
        b.vel = {0, 0, 100};
        HeliStep(p, s, HeliInput(), HeliFlags(), b, dt);
        CHECK(Len(b.vel) <= p.maximumSpeed + 1e-9);
    }
    {  // plane step clamps when a cap is configured
        PlaneParams p = PlaneStd();
        p.maximumSpeed = 40.0;
        PlaneState s;
        CraftBody b;
        b.mass = p.mass;
        b.vel = {0, 0, 80};
        PlaneStep(p, s, PlaneInput(), PlaneFlags(), b, dt);
        CHECK(Near(Len(b.vel), 40.0, 1e-9));
    }
    {  // boat step clamps; boost bonus raises the limit
        BoatParams p = BoatStd();
        p.boostSpeedBonus = 10.0;
        BoatState s;
        BoatInput in;
        CraftBody b;
        b.mass = p.mass;
        b.vel = {0, 0, 60};
        BoatStep(p, s, in, true, false, false, b, dt);
        CHECK(Near(Len(b.vel), p.maximumSpeed, 1e-9));
        b.vel = {0, 0, 60};
        in.boost = true;
        BoatState s2;
        BoatStep(p, s2, in, true, false, false, b, dt);
        CHECK(Near(Len(b.vel), p.maximumSpeed + 10.0, 1e-9));
    }
    // AW-6: barrel roll direction signs the rate; stays active after the timed phase while the bank error is large.
    {
        const PlaneParams p = PlaneStd();
        for (int dir : {1, -1}) {
            PlaneState s;
            PlaneBarrelRollStart(s, dir);
            CraftBody b;
            b.vel = {0, 0, 40};
            PlaneStep(p, s, PlaneInput(), PlaneFlags(), b, dt);
            CHECK(Near(s.rollRate, dir * 6.1087, 1e-3));
        }
        PlaneState s;
        PlaneBarrelRollStart(s, 1);
        s.barrelElapsed = 10.0;
        CraftBody b;
        b.vel = {0, 0, 40};
        b.basis.right = {0, -1, 0};  // 90 deg banked, target 0: error ~ pi/2 >= 0.349
        b.basis.up = {1, 0, 0};
        PlaneStep(p, s, PlaneInput(), PlaneFlags(), b, dt);
        CHECK(s.barrelActive);
        PlaneState s2;  // within 0.349 rad it ends
        PlaneBarrelRollStart(s2, 1);
        s2.barrelElapsed = 10.0;
        CraftBody b2;
        b2.vel = {0, 0, 40};
        PlaneStep(p, s2, PlaneInput(), PlaneFlags(), b2, dt);
        CHECK(!s2.barrelActive);
    }
    // AW-9: boat spin damping runs after both branches.
    {
        BoatParams p = BoatStd();
        p.normalSpinDamping = 1.0;
        p.collisionSpinDamping = 5.0;
        p.collisionSpinThreshold = 2.0;
        BoatState s;
        CraftBody b;
        b.mass = p.mass;
        b.angVel = {0, 1.0, 0};
        BoatStep(p, s, BoatInput(), true, false, false, b, 0.1);  // below threshold: x(1-0.1)
        CHECK(Near(b.angVel.y, 0.9, 1e-9));
        BoatState s2;
        CraftBody b2;
        b2.mass = p.mass;
        b2.angVel = {0, 4.0, 0};
        BoatStep(p, s2, BoatInput(), false, false, true, b2, 0.1);  // grounded branch, above threshold: x(1-0.5)
        CHECK(Near(b2.angVel.y, 2.0, 1e-9));
    }
    // AW-10: LBS with a centred stick still applies the force at the centre of mass (no torque), no yaw-rate step.
    {
        BoatParams p = BoatStd();
        BoatState s;
        s.thrust = 1.0;
        BoatInput in;
        in.throttle = 1.0;
        CraftBody b;
        b.mass = p.mass;
        BoatStep(p, s, in, true, true, false, b, dt);
        CHECK(b.vel.z > 0.0);
        CHECK(Near(Len(b.angVel), 0.0, 1e-12));
    }
}

int main() {
    TestReviewR5();
    TestHeli();
    TestPlane();
    TestVtol();
    TestBoat();
    TestWater();
    TestDamage();
    if (g_fail) {
        std::printf("aircraft/watercraft tests FAILED: %d\n", g_fail);
        return 1;
    }
    std::printf("aircraft/watercraft tests passed\n");
    return 0;
}
