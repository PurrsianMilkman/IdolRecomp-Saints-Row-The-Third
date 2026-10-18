// Checks sr3physics/vehicle_spec_data.h against the numeric test vectors of
// spec-physics-runtime.md 10.11.9 (car_4dr_genki, exoticsports01, pickup03).
#include <cmath>
#include <cstdio>

#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3xtbl/xtbl.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
#define NEAR(a, b, tol) do { double A_ = (a), B_ = (b); if (!(std::fabs(A_ - B_) <= (tol))) { std::printf("FAIL line %d: %s=%.6f vs %.6f\n", __LINE__, #a, A_, B_); ++g_fail; } } while (0)

using namespace sr3physics;

int main() {
    // 10.11.5 sag, genki: g_eff -19.8, zF 1.8841, zR -1.4943, zC 0.2355, strength 40/30, 2 wheels per axle.
    {
        double sf = 0, sr = 0;
        CHECK(StaticSagFront(1.8841, -1.4943, 0.2355, -19.8, 2, 40.0, sf));
        CHECK(StaticSagRear(1.8841, -1.4943, 0.2355, -19.8, 2, 30.0, sr));
        NEAR(sf, 0.12672, 2e-4);
        NEAR(sr, 0.16103, 2e-4);
        // pickup03
        CHECK(StaticSagFront(1.6593, -1.3966, 0.45, -19.8, 2, 50.0, sf));
        CHECK(StaticSagRear(1.6593, -1.3966, 0.45, -19.8, 2, 30.0, sr));
        NEAR(sf, 0.11965, 3e-4);
        NEAR(sr, 0.13059, 3e-4);
        // front axle not ahead of COM -> fallback signalled
        CHECK(!StaticSagFront(0.0, -1.0, 0.5, -19.8, 2, 40.0, sf));
    }
    // 10.11.5 per-mass -> per-wheel: genki 40 * 1500 = 60000 N/m, damp 5*1500 = 7500.
    NEAR(PerMassToPerWheel(40.0, 1500.0), 60000.0, 1e-9);
    NEAR(PerMassToPerWheel(5.0, 1500.0), 7500.0, 1e-9);

    // 10.11.6 friction ratio x (genki front: c_z 1.6486, c_y -0.27832, wheel mass 2*30).
    {
        const double x = FrictionRatioX(60.0, 1500.0, 1.6486, -0.27832, 0.27, 2.18906, 0.4, 681.3 / 1500.0);
        NEAR(x, 0.056137, 2e-4);
    }
    // 10.11.6 friction table, genki F (x = 0.056137) and exotic R (x = 0.082354).
    {
        const FrictionSlipTable t = BuildFrictionSlipTableFromSpec(0.056137);
        NEAR(t.entries[0], 0.060690, 2e-4);
        NEAR(t.entries[1], 0.087291, 2e-4);
        NEAR(t.entries[7], 0.591848, 5e-4);
        NEAR(t.entries[10], 0.946923, 5e-4);
        NEAR(t.entries[14], 1.0, 1e-5);
        CHECK(t.entries[15] == 1.0);
        NEAR(t.q, 1.45767, 1e-4);
        NEAR(t.start, 0.0035086, 1e-6);
        const FrictionSlipTable e = BuildFrictionSlipTableFromSpec(0.082354);
        NEAR(e.entries[0], 0.060739, 2e-4);
        NEAR(e.entries[5], 0.307530, 5e-4);
        NEAR(e.q, 1.42090, 1e-4);
        for (int i = 1; i < 16; ++i) CHECK(e.entries[i] >= e.entries[i - 1]);
    }
    // 10.11.7 steering profile: genki V 18.85, damp 0.4363 -> A 407.15; full lock 35 deg in 77.5 ms.
    {
        const double a = SteeringLimiterAccel(18.850, 0.4363);
        NEAR(a, 407.15, 0.5);
        NEAR(SteeringFullLockTimeSec(0.61087, 18.850, a), 0.0775, 5e-4);
        // exotic: V 9.425, damp 0.1745, A 254.47, 50 deg in 129.6 ms
        const double ae = SteeringLimiterAccel(9.425, 0.1745);
        NEAR(ae, 254.47, 0.5);
        NEAR(SteeringFullLockTimeSec(0.87266, 9.425, ae), 0.1296, 5e-4);
        CHECK(SteeringLimiterAccel(10.0, 0.0) == 0.0);
    }
    // 10.11.4 wheel radius: genki size 11 -> 0.2458; exotic front size 9 -> 0.2253; pickup size 14 -> 0.2765.
    NEAR(WheelRadiusFromSizeIndex(11), 0.2458, 1e-4);
    NEAR(WheelRadiusFromSizeIndex(9), 0.2253, 1e-4);
    NEAR(WheelRadiusFromSizeIndex(14), 0.2765, 1e-4);
    NEAR(WheelRadiusFromSizeIndex(999), 0.2048 * 2.8, 1e-9);
    NEAR(WheelRadiusFromSizeIndex(-3), 0.2048 * 0.4, 1e-9);

    // 10.11.8 lift: genki Cl -5, rho 1.3, A 1.0 -> -3.25 v^2.
    NEAR(LiftForceN(1.3, -5.0, 1.0, 10.0), -325.0, 1e-9);

    // 10.11.8 velocity damper: genki normal 2, collision 6, threshold 4.0143.
    NEAR(ApplyVelocityDamper(1.0, 2.0, 6.0, 4.0143, 0.01), 0.98, 1e-12);
    NEAR(ApplyVelocityDamper(5.0, 2.0, 6.0, 4.0143, 0.01), 5.0 * 0.94, 1e-12);
    NEAR(ApplyVelocityDamper(5.0, 2.0, 6.0, 4.0143, 1.0), 0.0, 1e-12);  // max(0, ...) floor
    NEAR(ApplyVelocityDamper(1.0, 0.0, 0.0, 0.0, 0.1), 1.0, 1e-12);       // off by default

    // 10.11.10 Maximum_Speed hard clamp (3-4-5 triangle scaled to cap 10).
    {
        double f = 9.0, r = 12.0;  // speed 15
        ClampPlanarSpeed(f, r, 10.0);
        NEAR(std::sqrt(f * f + r * r), 10.0, 1e-12);
        NEAR(f / r, 0.75, 1e-12);
        double f2 = 3.0, r2 = 4.0;
        ClampPlanarSpeed(f2, r2, 10.0);
        CHECK(f2 == 3.0 && r2 == 4.0);
        ClampPlanarSpeed(f2, r2, 0.0);  // cap off
        CHECK(f2 == 3.0 && r2 == 4.0);
    }

    // Wiring: FromVehicleEntry sources the FINAL fields; the simulator clamps speed to Maximum_Speed.
    {
        const char* xml =
            "<root><Table><Vehicle><Name>t</Name><Mass>1500</Mass>"
            "<Engine><Torque>2000</Torque><Min_RPM>800</Min_RPM><Opt_RPM>4500</Opt_RPM><Max_RPM>6500</Max_RPM></Engine>"
            "<Steering><Max_Steering_Angle>35</Max_Steering_Angle><Max_Speed_Steering>42.5</Max_Speed_Steering>"
            "<Steering_Wheel_Max_Speed>1080</Steering_Wheel_Max_Speed></Steering>"
            "<Yaw_Unit_Inertia>2.0</Yaw_Unit_Inertia>"
            "<Angular_Damping><Normal_Spin_Damping>2</Normal_Spin_Damping><Collision_Spin_Damping>6</Collision_Spin_Damping>"
            "</Angular_Damping>"
            "<Maximum_Speed>20</Maximum_Speed>"
            "<Vehicle_Type><Automobile></Automobile></Vehicle_Type></Vehicle></Table></root>";
        sr3xtbl::Document doc = sr3xtbl::ParseDocument(xml);
        const sr3xtbl::Node* row = sr3xtbl::FindChild(doc.table(), "Vehicle");
        const sr3vehicleinfo::VehicleEntry entry = sr3vehicleinfo::ParseVehicleEntry(row);
        const VehicleTuning t = VehicleTuning::FromVehicleEntry(entry);
        NEAR(t.yawInertia, 3000.0, 1e-6);
        NEAR(t.normalSpinDamping, 2.0, 1e-6);
        NEAR(t.collisionSpinDamping, 6.0, 1e-6);
        CHECK(t.steeringSpeedThresholdMPerSec > 8.0);  // 42.5 mph converted to m/s by the reader, replaces the 8.0 default
        CHECK(t.maximumSpeedMs > 0.0);
        VehicleSubstepSimulator sim(t);
        DriverInput in;
        in.throttle = 1.0;
        double maxSpeed = 0.0;
        for (int i = 0; i < 60 * 40; ++i) {
            sim.Step(1.0 / 60.0, in);
            const auto& s = sim.state_;
            maxSpeed = std::max(maxSpeed, std::sqrt(s.velForwardBody * s.velForwardBody + s.velRightBody * s.velRightBody));
        }
        std::printf("max speed reached %.3f cap %.3f\n", maxSpeed, t.maximumSpeedMs);
        CHECK(maxSpeed <= t.maximumSpeedMs + 1e-9);
        CHECK(maxSpeed > 0.5 * t.maximumSpeedMs);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("vehicle_spec_data tests passed\n");
    return 0;
}
