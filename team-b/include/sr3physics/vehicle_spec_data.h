#pragma once

// sr3physics/vehicle_spec_data.h - pure formulas spec-physics-runtime.md
// 10.11 ("Vehicle component values from data", 2026-10-14) marks FINAL, plus
// the geometry/suspension/friction-table formulas it gives with a numeric
// test vector (10.11.9). Every formula is quoted from the spec and checked
// against that section's genki/exotic/pickup rows in
// tests/synthetic_physics_vehicle_spec_data_test.cpp.
//
// Wired into the sim (VehicleTuning / VehicleSubstepSimulator): 10.11.7
// steering rates + Max_Speed_Steering, 10.11.2 yaw inertia, 10.11.8 velocity
// damper, 10.11.10 Maximum_Speed hard clamp. Pure + tested but NOT wired (the
// planar sim has no vertical state / the real slip-table lookup is OPEN):
// suspension rates and sag (10.11.5), friction table (10.11.6), lift (10.11.8).

#include <array>
#include <cstddef>

#include "sr3physics/vehicle_driving_slice.h"
#include "sr3vehicleinfo/vehicle_entry.h"

namespace sr3physics {

struct VehicleTuning;

// 10.11.7 / 10.11.2 / 10.11.8 / 10.11.10: copies the FINAL data-sourced
// chassis values from a parsed entry into `t` (only when the field is present).
void ApplySpecChassisData(VehicleTuning& t, const sr3vehicleinfo::VehicleEntry& entry);

// 10.11.8 velocity damper (FINAL): omega *= max(0, 1 - D*dt), D = collision
// damping when |omega|^2 > threshold^2, else normal damping. A zero threshold
// therefore selects collision damping for any nonzero spin (spec wording).
double ApplyVelocityDamper(double omega, double normalDamping, double collisionDamping, double threshold, double dt);

// Plain planar rescale helper (kept for callers without the full limiter state).
void ClampPlanarSpeed(double& vForward, double& vRight, double cap);

// 10.11.10 Maximum_Speed hard clamp, full rule (`0x00a86620`, FINAL with the
// 2026-10-14 clarification). Per call, per vehicle:
//   skip entirely if wrecked/dead;
//   cap = overrideCap (>= 0, the `vehicle_max_speed` script override +0x1d84) else maximumSpeed;
//   unless class is 3/4/5 (Helicopter, Vtol, Watercraft): blend = 1 while nitrous,
//     else max(0, blend - 0.5/30); cap += (maximumSpeedWithNitrous - cap) * blend;
//   limit = cap + bonus (bonus inputs' meaning is OPEN, caller-supplied, 0 by default);
//   if the full 3-D speed exceeds the limit, ALL three components are rescaled to it.
struct MaxSpeedLimitInput {
    double maximumSpeed = 0.0;
    double maximumSpeedWithNitrous = 0.0;
    double overrideCap = -1.0;   // negative = no script override
    int vehicleClass = 0;        // 0 Automobile .. 5 Watercraft
    bool nitrousActive = false;
    bool wrecked = false;
    double bonus = 0.0;
};
struct MaxSpeedLimiterState {
    double nitroBlend = 0.0;  // vehicle +0x1a34
};
// Returns true when the velocity was rescaled.
bool ApplyMaximumSpeedLimit(double vel[3], MaxSpeedLimiterState& state, const MaxSpeedLimitInput& in);

// 10.11.8 extra-gravity action (`0x00ae3850`, FINAL incl. the 2026-10-14 notes).
struct ExtraGravityInput {
    int vehicleClass = 0;              // enabled for everything except 3 (Helicopter) and 4 (Vtol)
    double extraGravity = 0.0;         // Extra_Gravity
    double mass = 0.0;
    unsigned contactMask = 0;          // action does nothing when non-zero
    bool allWheelsInContact = false;
    bool anyWheelInContact = false;
    double meanNormal[3] = {0, 1, 0}; // normalised mean contact normal
    bool coastTermEnabled = true;      // byte +0x2d (cleared for vehicles towing a trailer)
    double throttle = 0.0;             // physics-object +0x128; term only when exactly 0
    double velocity[3] = {0, 0, 0};    // chassis linear velocity
    double forward[3] = {0, 0, 1};     // chassis forward axis (+0x1b0)
};
// Writes the force (N) to out[3]. Airborne: (0, -10*Mass, 0). All grounded:
// Mass*Extra_Gravity*n (zero if n.y < 0.5) plus the coasting damping term
// (-0.89408*9.8*Mass*d*t when |d| < 0.89408, added even when n.y < 0.5).
// Mixed contact: nothing.
void ComputeExtraGravityForce(const ExtraGravityInput& in, double out[3]);

// 10.3 reverse rule (FINAL for the car path, 2026-10-14). Inputs: command
// x = throttle - brake in [-1,1], s = velocity . forward (m/s), handbrake,
// `fullLogic` = the byte at (chassis wrapper +0x20 object)+0x14 (its source
// was not traced), vehicleClass (1 = Motorcycle special-cases), current flag.
struct ReverseDecision {
    double accel = 0.0;
    double brake = 0.0;
    bool reverseFlag = false;
};
ReverseDecision DecideReverse(bool fullLogic, double x, double s, bool handbrake, int vehicleClass, bool flagIn);
// Wheel torque ratio (`0x00c59b40`): -(reverse * diff) in reverse, else gear * diff.
// Reverse is selected when the flag is set and the gear index is <= 0 (`0x00c59b10`).
double WheelTorqueRatio(bool reverseFlag, int gearIndexZeroBased, double reverseRatio, double gearRatio, double diffRatio);

// 10.11.7: steering limiter acceleration A = V^2 / (2 * dampAngle) (dampAngle<=0 -> 0).
double SteeringLimiterAccel(double maxSpeedRadPerSec, double dampAngleRad);
// 10.11.7: full-lock step from rest: 2*sqrt(theta/A) if theta <= V^2/A else 2V/A + (theta - V^2/A)/V.
double SteeringFullLockTimeSec(double thetaRad, double maxSpeedRadPerSec, double accelRadPerSec2);

// 10.11.4: wheel radius = 0.2048 * Ts[size] (index clamped to 26).
double WheelRadiusFromSizeIndex(int sizeIndex);

// 10.11.5: per-wheel spring rate / damper = per-unit-mass value * chassis mass.
inline double PerMassToPerWheel(double perMassValue, double massKg) { return perMassValue * massKg; }

// 10.11.5 ride-height sag per axle (game formula). zF/zR = mean wheel z of the axles, zC = COM z,
// gEff = world gravity y + Extra_Gravity (negative), n = wheels on axle, strength = Wheel_Spring_Strength.
// Returns false (output 0) when zF <= zC (spec: the game falls back to a radius/2 offset instead).
bool StaticSagFront(double zF, double zR, double zC, double gEff, int nF, double strengthF, double& sagF);
bool StaticSagRear(double zF, double zR, double zC, double gEff, int nR, double strengthR, double& sagR);

// 10.11.6 friction ratio x_k = (sum wheel mass on axle) * m_eff^-1 with
// m_eff^-1 = invMass + cz^2*(yawTF/yawUI)/mass + cy^2*(rollTF/rollUI)/mass (unrotated chassis).
double FrictionRatioX(double sumWheelMass, double mass, double cZ, double cY, double yawTF, double yawUI,
                      double rollTF, double rollUI);

// 10.11.6 friction table: table[i] = 1-(1-s_i)^(1/x), s_0 = x/16, s_{i+1} = s_i*q, q=(16/x)^(1/15); [15]=1.
// start = x/16 (desc +0x44), q = step (desc +0x40), the spec's corrected field roles.
// The lookup is LookupFrictionSlipTable() (vehicle_driving_slice.h); the force application is undecoded, so
// no axle installs this table.
FrictionSlipTable BuildFrictionSlipTableFromSpec(double x);

// 10.11.8 lift force 0.5*rho*Cl*A*v^2 along chassis up (negative Cl = downforce).
double LiftForceN(double airDensity, double liftCoefficient, double frontalArea, double forwardSpeed);

}  // namespace sr3physics
