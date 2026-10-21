// Conformance tests for Team A's review of the free-roam merge (spec-conformance-review.md, findings P-1..P-12).
// Every case cites the spec section and uses the spec's own worked vectors where it gives them.
#include <cmath>
#include <cstdio>

#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3xtbl/xtbl.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
#define NEAR(a, b, tol) do { double A_ = (a), B_ = (b); if (!(std::fabs(A_ - B_) <= (tol))) { std::printf("FAIL line %d: %s=%.6f vs %.6f\n", __LINE__, #a, A_, B_); ++g_fail; } } while (0)

using namespace sr3physics;

// Time for the profile limiter to bring the angle within 1e-7 rad of the target, stepping at `dt`.
static double ProfileTime(SteeringProfileLimiter& lim, double target, double dt) {
    double t = 0.0;
    for (int i = 0; i < 200000; ++i) {
        lim.Update(target, dt);
        t += dt;
        if (std::fabs(lim.current() - target) < 1e-7) return t;
    }
    return -1.0;
}

static void testP1_LookupWalk() {
    // 10.11.6 vectors, genki front table: x_k 0.056043, q 1.457828, start 0.0035027.
    const FrictionSlipTable t = BuildFrictionSlipTableFromSpec(0.056043);
    NEAR(t.q, 1.457828, 2e-5);
    NEAR(t.start, 0.0035027, 1e-6);
    FrictionSlipLookup r = LookupFrictionSlipTable(t, 1.0, 1.0);
    CHECK(!r.belowCircle);
    CHECK(r.stopIndex == 11);
    NEAR(r.t, 0.7601, 2e-3);
    NEAR(r.u, 0.2047, 2e-3);
    NEAR(r.v, 0.9785, 2e-3);
    r = LookupFrictionSlipTable(t, 1.2, 0.8);
    CHECK(r.stopIndex == 14);
    NEAR(r.t, 0.1234, 2e-3);
    NEAR(r.u, 0.5965, 2e-3);
    NEAR(r.v, 0.8000, 2e-3);
    r = LookupFrictionSlipTable(t, 1.5, 1.5);
    CHECK(r.stopIndex == 8);
    NEAR(r.t, 0.5093, 3e-3);
    NEAR(r.u, 0.0907, 2e-3);
    NEAR(r.v, 0.9959, 2e-3);
    r = LookupFrictionSlipTable(t, 0.6, 0.6);  // S = 0.72 < 1: no correction
    CHECK(r.belowCircle);
    // u* is clamped to +-1.1 (spec): a huge A with B = 0 reaches entry 15 where u_15 = A.
    r = LookupFrictionSlipTable(t, 5.0, 0.0);
    CHECK(std::fabs(r.u) <= 1.1 + 1e-12);
    // A^2 + B^2 exactly 1 is inside the circle (<= 1.0).
    CHECK(LookupFrictionSlipTable(t, 1.0, 0.0).belowCircle);
}

static void testP2_FieldRoles() {
    // 10.11.6: desc +0x40 = q (a1 +0x48), desc +0x44 = start (a1 +0x4C). genki F: q 1.45767, start 0.0035086.
    const FrictionSlipTable t = BuildFrictionSlipTableFromSpec(0.056137);
    NEAR(t.q, 1.45767, 1e-4);
    NEAR(t.start, 0.0035086, 1e-6);
    // The ladder u_i = start * q^i ends at exactly 1.0 at i = 15.
    NEAR(t.start * std::pow(t.q, 15), 1.0, 1e-9);
}

static void testP3_TorqueModifier() {
    // 10.11.10: returned torque = net x P x (0.85 if gated) x G, G = 1.25. net 1000 -> 1250, gated 1062.5.
    EngineCurveTuning e;
    e.torque = 1000.0; e.minRpm = 1000.0; e.optRpm = 4500.0; e.maxRpm = 6500.0;  // D-17 table roles: at Opt_RPM, T = Torque, R = 0
    e.clutchSlipRpm = 1750.0;
    const double base = EngineNetTorque(e, 4500.0, 1.0, false, 1.0);
    NEAR(base, 1000.0, 1e-9);
    NEAR(EngineNetTorque(e, 4500.0, 1.0, false, 1.25), 1250.0, 1e-9);
    NEAR(EngineNetTorque(e, 4500.0, 1.0, true, 1.25), 1062.5, 1e-9);
    // The simulator's default tuning carries G = 1.25 (was 1.0).
    VehicleTuning vt;
    NEAR(vt.engineTorqueModifier, 1.25, 1e-12);
}

static void testP4_MaximumSpeed() {
    MaxSpeedLimiterState st;
    MaxSpeedLimitInput in;
    in.maximumSpeed = 10.0;
    in.maximumSpeedWithNitrous = 20.0;
    // 3-D speed: (3,4,12) = 13 > 10 -> all components rescaled to exactly 10.
    double v[3] = {3.0, 4.0, 12.0};
    CHECK(ApplyMaximumSpeedLimit(v, st, in));
    NEAR(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 10.0, 1e-12);
    NEAR(v[1] / v[0], 4.0 / 3.0, 1e-12);
    NEAR(v[2] / v[0], 4.0, 1e-12);
    // A purely vertical speed over the cap is limited too (planar clamp would miss it).
    double up[3] = {0.0, 15.0, 0.0};
    CHECK(ApplyMaximumSpeedLimit(up, st, in));
    NEAR(up[1], 10.0, 1e-12);
    // Script override (+0x1d84) replaces the cap when >= 0.
    MaxSpeedLimitInput ov = in;
    ov.overrideCap = 5.0;
    MaxSpeedLimiterState st2;
    double w[3] = {8.0, 0.0, 0.0};
    ApplyMaximumSpeedLimit(w, st2, ov);
    NEAR(w[0], 5.0, 1e-12);
    // Nitrous: blend jumps to 1 -> cap = nitro max; then decays 0.5/30 per call, to 0 after 60 calls (2 s at 30 Hz).
    MaxSpeedLimiterState st3;
    MaxSpeedLimitInput nit = in;
    nit.nitrousActive = true;
    double a[3] = {30.0, 0.0, 0.0};
    ApplyMaximumSpeedLimit(a, st3, nit);
    NEAR(st3.nitroBlend, 1.0, 1e-12);
    NEAR(a[0], 20.0, 1e-12);
    nit.nitrousActive = false;
    for (int i = 1; i <= 60; ++i) {
        double b[3] = {30.0, 0.0, 0.0};
        ApplyMaximumSpeedLimit(b, st3, nit);
        NEAR(st3.nitroBlend, std::max(0.0, 1.0 - i * 0.5 / 30.0), 1e-9);
        if (i == 30) NEAR(b[0], 10.0 + 10.0 * st3.nitroBlend, 1e-9);
    }
    NEAR(st3.nitroBlend, 0.0, 1e-9);
    // Class 3/4/5 are exempt from the nitrous blend even when nitrous is on.
    for (int cls = 3; cls <= 5; ++cls) {
        MaxSpeedLimiterState s;
        MaxSpeedLimitInput x = nit;
        x.nitrousActive = true;
        x.vehicleClass = cls;
        double c[3] = {30.0, 0.0, 0.0};
        ApplyMaximumSpeedLimit(c, s, x);
        NEAR(c[0], 10.0, 1e-12);
        NEAR(s.nitroBlend, 0.0, 1e-12);
    }
    // Bonus is added to the limit.
    MaxSpeedLimiterState s4;
    MaxSpeedLimitInput bo = in;
    bo.bonus = 2.0;
    double d[3] = {30.0, 0.0, 0.0};
    ApplyMaximumSpeedLimit(d, s4, bo);
    NEAR(d[0], 12.0, 1e-12);
    // Wrecked vehicles are skipped entirely.
    MaxSpeedLimiterState s5;
    MaxSpeedLimitInput wr = in;
    wr.wrecked = true;
    double e[3] = {30.0, 0.0, 0.0};
    CHECK(!ApplyMaximumSpeedLimit(e, s5, wr));
    NEAR(e[0], 30.0, 1e-12);
}

static void testP5_ExtraGravity() {
    ExtraGravityInput in;
    in.mass = 1500.0;
    in.extraGravity = -10.0;
    double f[3];
    // Class 3 / 4: no force. Extra_Gravity 0: no force.
    in.vehicleClass = 3;
    in.allWheelsInContact = in.anyWheelInContact = false;
    ComputeExtraGravityForce(in, f);
    CHECK(f[0] == 0.0 && f[1] == 0.0 && f[2] == 0.0);
    in.vehicleClass = 4;
    ComputeExtraGravityForce(in, f);
    CHECK(f[1] == 0.0);
    in.vehicleClass = 0;
    ExtraGravityInput zero = in;
    zero.extraGravity = 0.0;
    ComputeExtraGravityForce(zero, f);
    CHECK(f[1] == 0.0);
    // Airborne: (0, -10*Mass, 0) = (0, -15000, 0).
    ComputeExtraGravityForce(in, f);
    NEAR(f[1], -15000.0, 1e-9);
    NEAR(f[0], 0.0, 1e-12);
    // Contact mask non-zero: nothing.
    ExtraGravityInput masked = in;
    masked.contactMask = 1;
    ComputeExtraGravityForce(masked, f);
    CHECK(f[1] == 0.0);
    // Mixed contact: nothing.
    ExtraGravityInput mixed = in;
    mixed.anyWheelInContact = true;
    mixed.allWheelsInContact = false;
    ComputeExtraGravityForce(mixed, f);
    CHECK(f[0] == 0.0 && f[1] == 0.0 && f[2] == 0.0);
    // All grounded, flat: Mass * Extra_Gravity * n, no coast term when moving straight (v = (0,0,10): d = 1).
    ExtraGravityInput g = in;
    g.allWheelsInContact = g.anyWheelInContact = true;
    g.velocity[2] = 10.0;
    ComputeExtraGravityForce(g, f);
    NEAR(f[1], 1500.0 * -10.0, 1e-9);
    NEAR(f[0], 0.0, 1e-9);
    NEAR(f[2], 0.0, 1e-9);
    // Spec vector: flat ground, v = (3,0,3), throttle 0 -> force (-6571.6, 0, -6571.6) from the coast term
    // (extra-gravity part is vertical, so use the horizontal components).
    g.velocity[0] = 3.0; g.velocity[1] = 0.0; g.velocity[2] = 3.0;
    ComputeExtraGravityForce(g, f);
    NEAR(f[0], -6571.6, 0.2);
    NEAR(f[2], -6571.6, 0.2);
    // Throttle non-zero: no coast term.
    g.throttle = 0.5;
    ComputeExtraGravityForce(g, f);
    NEAR(f[0], 0.0, 1e-9);
    // Coast term also applies when n.y < 0.5 (steep normal): extra-gravity part is zeroed, coast stays.
    ExtraGravityInput steep = in;
    steep.allWheelsInContact = steep.anyWheelInContact = true;
    steep.meanNormal[0] = 1.0; steep.meanNormal[1] = 0.0; steep.meanNormal[2] = 0.0;
    steep.velocity[0] = 0.0; steep.velocity[1] = 0.0; steep.velocity[2] = 3.0;  // vt = v, d = 1.0 -> no term
    ComputeExtraGravityForce(steep, f);
    NEAR(f[0], 0.0, 1e-9);
    // Sim wiring (planar coasting term): two otherwise identical sims, one with Extra_Gravity.
    VehicleTuning base;
    VehicleTuning eg = base;
    eg.extraGravity = -10.0;
    VehicleSubstepSimulator a(base), b(eg);
    a.state_.velForwardBody = b.state_.velForwardBody = 3.0;
    a.state_.velRightBody = b.state_.velRightBody = 3.0;
    DriverInput idle;
    const double dt = 1.0 / 60.0;
    a.Step(dt, idle);
    b.Step(dt, idle);
    const double expectDv = (-6571.6 / base.mass) * dt;
    NEAR(b.state_.velForwardBody - a.state_.velForwardBody, expectDv, 2e-3);
    NEAR(b.state_.velRightBody - a.state_.velRightBody, expectDv, 2e-3);
}

static void testP7_SlotMap() {
    CHECK(static_cast<int>(VehicleComponentSlot::DriverInput) == 0x20);
    CHECK(static_cast<int>(VehicleComponentSlot::Steering) == 0x24);
    CHECK(static_cast<int>(VehicleComponentSlot::Engine) == 0x28);
    CHECK(static_cast<int>(VehicleComponentSlot::Transmission) == 0x2c);
    CHECK(static_cast<int>(VehicleComponentSlot::Brakes) == 0x30);
    CHECK(static_cast<int>(VehicleComponentSlot::Suspension) == 0x34);
    CHECK(static_cast<int>(VehicleComponentSlot::Aerodynamics) == 0x38);
    CHECK(static_cast<int>(VehicleComponentSlot::WheelCollide) == 0x3c);
    CHECK(static_cast<int>(VehicleComponentSlot::VelocityDamper) == 0x44);
}

static void testP8_SteeringProfile() {
    // genki: V 18.85, damp 0.4363 -> A 407.15. Full lock 35 deg in 77.5 ms from rest.
    {
        const double A = SteeringLimiterAccel(18.850, 0.4363);
        SteeringProfileLimiter lim(SteeringProfileParams{18.850, A, 18.850, A});
        NEAR(ProfileTime(lim, 0.61087, 1e-5), 0.0775, 5e-4);
        NEAR(lim.current(), 0.61087, 1e-7);
    }
    // genki at 20 m/s: target reduced by (18.999/20)^2 -> 31.585 deg in 73.6 ms.
    {
        const double A = SteeringLimiterAccel(18.850, 0.4363);
        SteeringProfileLimiter lim(SteeringProfileParams{18.850, A, 18.850, A});
        const double target = 0.61087 * (18.999 / 20.0) * (18.999 / 20.0);
        NEAR(target * 180.0 / 3.14159265358979, 31.585, 2e-3);
        NEAR(ProfileTime(lim, target, 1e-5), 0.0736, 5e-4);
    }
    // exotic: V 9.425, damp 0.1745 -> A 254.47; 50 deg in 129.6 ms.
    {
        const double A = SteeringLimiterAccel(9.425, 0.1745);
        SteeringProfileLimiter lim(SteeringProfileParams{9.425, A, 18.850, SteeringLimiterAccel(18.850, 0.1745)});
        NEAR(ProfileTime(lim, 0.87266, 1e-5), 0.1296, 5e-4);
    }
    // pickup: V 18.85, damp 0.5236 -> A 339.29; 35 deg in 84.9 ms.
    {
        const double A = SteeringLimiterAccel(18.850, 0.5236);
        SteeringProfileLimiter lim(SteeringProfileParams{18.850, A, 18.850, A});
        NEAR(ProfileTime(lim, 0.61087, 1e-5), 0.0849, 5e-4);
    }
    // Frame-sized steps still land exactly on the target and never leave it afterwards (min-time, no overshoot drift).
    {
        const double A = SteeringLimiterAccel(18.850, 0.4363);
        SteeringProfileLimiter lim(SteeringProfileParams{18.850, A, 18.850, A});
        for (int i = 0; i < 6; ++i) lim.Update(0.61087, 1.0 / 60.0);
        NEAR(lim.current(), 0.61087, 1e-6);
        NEAR(lim.velocity(), 0.0, 1e-6);
    }
    // Return pair is used when the target is 0: exotic returns with A 1017.88 (faster than its turn-in 254.47).
    {
        SteeringProfileLimiter lim(SteeringProfileParams{9.425, SteeringLimiterAccel(9.425, 0.1745), 18.850,
                                                         SteeringLimiterAccel(18.850, 0.1745)});
        NEAR(SteeringLimiterAccel(18.850, 0.1745), 1017.88, 0.5);
        ProfileTime(lim, 0.87266, 1e-5);
        NEAR(ProfileTime(lim, 0.0, 1e-5), SteeringFullLockTimeSec(0.87266, 18.850, SteeringLimiterAccel(18.850, 0.1745)), 5e-4);
    }
}


static void testP9_DamperThreshold() {
    // 10.11.8: D = collision damping if |w|^2 > threshold^2, else normal. Zero threshold -> any nonzero spin is "collision".
    NEAR(ApplyVelocityDamper(0.5, 2.0, 6.0, 0.0, 0.01), 0.5 * (1.0 - 6.0 * 0.01), 1e-12);
    // Negative spin uses |w|.
    NEAR(ApplyVelocityDamper(-5.0, 2.0, 6.0, 4.0143, 0.01), -5.0 * 0.94, 1e-12);
    // Exactly at the threshold is not above it.
    NEAR(ApplyVelocityDamper(4.0, 2.0, 6.0, 4.0, 0.01), 4.0 * 0.98, 1e-12);
}

static void testP10_Reverse() {
    // 10.3 vectors (handbrake off, full logic).
    ReverseDecision r = DecideReverse(true, -1.0, 0.5, false, 0, false);
    CHECK(r.reverseFlag && std::fabs(r.accel - 1.0) < 1e-12 && r.brake == 0.0);
    r = DecideReverse(true, -1.0, 1.2, false, 0, true);
    CHECK(!r.reverseFlag && std::fabs(r.brake - 1.0) < 1e-12 && r.accel == 0.0);
    r = DecideReverse(true, -1.0, -1.5, false, 0, false);
    CHECK(r.reverseFlag && std::fabs(r.accel - 1.0) < 1e-12);
    r = DecideReverse(true, 1.0, -1.5, false, 0, true);
    CHECK(std::fabs(r.brake - 1.0) < 1e-12 && r.accel == 0.0);
    // (a) x = 0 clears the flag only when |s| <= 1.
    CHECK(!DecideReverse(true, 0.0, 0.5, false, 0, true).reverseFlag);
    CHECK(DecideReverse(true, 0.0, -3.0, false, 0, true).reverseFlag);
    // Plain mode: accel = max(x,0), brake = max(-x,0), flag untouched.
    r = DecideReverse(false, -0.4, 0.0, false, 0, true);
    CHECK(r.reverseFlag && r.accel == 0.0 && std::fabs(r.brake - 0.4) < 1e-12);
    // Wheel ratio: diff 4.2, reverse 3.38 -> -14.196; forward gear keeps gear*diff.
    NEAR(WheelTorqueRatio(true, 0, 3.38, 3.4, 4.2), -14.196, 1e-9);
    NEAR(WheelTorqueRatio(false, 0, 3.38, 3.4, 4.2), 3.4 * 4.2, 1e-9);
    // Sim: full logic on, brake at standstill engages reverse and drives backwards.
    const char* xml =
        "<root><Table><Vehicle><Name>t</Name><Mass>1400</Mass>"
        "<Engine><Torque>280</Torque><Min_RPM>800</Min_RPM><Opt_RPM>4500</Opt_RPM><Max_RPM>6500</Max_RPM>"
        "<Min_RPM_Torque_Factor>1.0</Min_RPM_Torque_Factor><Max_RPM_Torque_Factor>1.0</Max_RPM_Torque_Factor>"
        "<Min_RPM_Resistance>0.02</Min_RPM_Resistance><Opt_RPM_Resistance>0.0</Opt_RPM_Resistance>"
        "<Max_RPM_Resistance>0.001</Max_RPM_Resistance></Engine>"
        "<Axles><Axle><Wheel_Does_Steer>yes</Wheel_Does_Steer><Wheel_Friction>1.0</Wheel_Friction>"
        "<Wheel_Max_Friction>1.0</Wheel_Max_Friction><Wheel_Braking_Torque>800</Wheel_Braking_Torque>"
        "<Wheel_Engine_Torque>0.0</Wheel_Engine_Torque></Axle>"
        "<Axle><Wheel_Does_Steer>no</Wheel_Does_Steer><Wheel_Friction>1.0</Wheel_Friction>"
        "<Wheel_Max_Friction>1.0</Wheel_Max_Friction><Wheel_Braking_Torque>600</Wheel_Braking_Torque>"
        "<Wheel_Engine_Torque>1.0</Wheel_Engine_Torque></Axle></Axles>"
        "<Transmission><Gear_Ratios><Element>3.4</Element><Element>2.1</Element></Gear_Ratios>"
        "<Diff_Gear_Ratio>3.7</Diff_Gear_Ratio><Reverse_Gear_Ratio>3.38</Reverse_Gear_Ratio></Transmission>"
        "<Steering><Max_Steering_Angle>35</Max_Steering_Angle></Steering>"
        "<Vehicle_Type><Automobile></Automobile></Vehicle_Type></Vehicle></Table></root>";
    sr3xtbl::Document doc = sr3xtbl::ParseDocument(xml);
    const sr3vehicleinfo::VehicleEntry entry = sr3vehicleinfo::ParseVehicleEntry(sr3xtbl::FindChild(doc.table(), "Vehicle"));
    VehicleTuning t = VehicleTuning::FromVehicleEntry(entry);
    NEAR(t.reverseGearRatio, 3.38, 1e-6);
    t.reverseFullLogic = true;
    VehicleSubstepSimulator sim(t);
    DriverInput in;
    in.brake = 1.0;
    for (int i = 0; i < 180; ++i) sim.Step(1.0 / 60.0, in);
    CHECK(sim.state_.reverseFlag);
    CHECK(sim.state_.velForwardBody < -0.5);
    CHECK(sim.state_.drivetrainRatio < 0.0);
    // Default tuning (plain mode) never engages reverse.
    VehicleTuning pt = t;
    pt.reverseFullLogic = false;
    VehicleSubstepSimulator plain(pt);
    for (int i = 0; i < 60; ++i) plain.Step(1.0 / 60.0, in);
    CHECK(!plain.state_.reverseFlag);
}

static void testP11_P12_Rows() {
    // 10.11.9 exotic sag: zF mean(1.3417, 1.3331), zR mean(-1.5947, -1.5925), zC -0.2531, strength 70/70.
    double sf = 0, sr = 0;
    CHECK(StaticSagFront(1.3374, -1.5936, -0.2531, -19.8, 2, 70.0, sf));
    CHECK(StaticSagRear(1.3374, -1.5936, -0.2531, -19.8, 2, 70.0, sr));
    NEAR(sf, 0.06468, 2e-4);
    NEAR(sr, 0.07675, 2e-4);
    // Sag with world g only (no extra gravity), genki: 0.06272 / 0.07970.
    CHECK(StaticSagFront(1.8841, -1.4943, 0.2355, -9.8, 2, 40.0, sf));
    CHECK(StaticSagRear(1.8841, -1.4943, 0.2355, -9.8, 2, 30.0, sr));
    NEAR(sf, 0.06272, 2e-4);
    NEAR(sr, 0.07970, 2e-4);
    // Full friction tables, all six rows of 10.11.9 (x from the table rows, q cross-check).
    struct Row { double x, q; double e[16]; };
    static const Row rows[] = {
        {0.056137, 1.45767, {0.060690, 0.087291, 0.124795, 0.176866, 0.247557, 0.340408, 0.456582, 0.591848, 0.733338, 0.859289, 0.946923, 0.988440, 0.999037, 0.999988, 1.000000, 1.0}},
        {0.058265, 1.45405, {0.060694, 0.087092, 0.124231, 0.175704, 0.245488, 0.337096, 0.451792, 0.585690, 0.726558, 0.853304, 0.943142, 0.987042, 0.998832, 0.999983, 1.000000, 1.0}},
        {0.091156, 1.41131, {0.060755, 0.084755, 0.117679, 0.162314, 0.221821, 0.299276, 0.396696, 0.513313, 0.643215, 0.773347, 0.884487, 0.958670, 0.991958, 0.999524, 0.999999, 1.0}},
        {0.082354, 1.42090, {0.060739, 0.085275, 0.119129, 0.165257, 0.226995, 0.307530, 0.408773, 0.529400, 0.662281, 0.792609, 0.899891, 0.967217, 0.994508, 0.999752, 1.000000, 1.0}},
        {0.047479, 1.47403, {0.060674, 0.088195, 0.127365, 0.182193, 0.257065, 0.355630, 0.478508, 0.619725, 0.763350, 0.884720, 0.961897, 0.993351, 0.999623, 0.999998, 1.000000, 1.0}},
        {0.058785, 1.45319, {0.060695, 0.087044, 0.124098, 0.175428, 0.244997, 0.336310, 0.450654, 0.584224, 0.724936, 0.851860, 0.942216, 0.986690, 0.998779, 0.999982, 1.000000, 1.0}},
    };
    for (const Row& r : rows) {
        const FrictionSlipTable t = BuildFrictionSlipTableFromSpec(r.x);
        NEAR(t.q, r.q, 2e-4);
        NEAR(t.start, r.x / 16.0, 1e-12);
        for (int i = 0; i < 16; ++i) NEAR(t.entries[static_cast<size_t>(i)], r.e[i], 1.5e-3);
    }
}
int main() {
    testP1_LookupWalk();
    testP2_FieldRoles();
    testP3_TorqueModifier();
    testP4_MaximumSpeed();
    testP5_ExtraGravity();
    testP7_SlotMap();
    testP8_SteeringProfile();
    testP9_DamperThreshold();
    testP10_Reverse();
    testP11_P12_Rows();
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("review conformance tests passed\n");
    return 0;
}
