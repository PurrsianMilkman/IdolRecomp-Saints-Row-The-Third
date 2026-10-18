#include "sr3physics/vehicle_spec_data.h"

#include <algorithm>
#include <cmath>

#include "sr3physics/vehicle_substeps.h"

namespace sr3physics {

void ApplySpecChassisData(VehicleTuning& t, const sr3vehicleinfo::VehicleEntry& entry) {
    // 10.11.2 chassis inertia = Mass x Yaw_Unit_Inertia (rigid body yaw axis).
    if (entry.yawUnitInertia.present && entry.yawUnitInertia.value > 0.0f)
        t.yawInertia = t.mass * entry.yawUnitInertia.value;
    // 10.11.7 steering: max angular speed = turn-in / return speed; threshold = Max_Speed_Steering (m/s).
    if (entry.steeringWheelMaxSpeedRadians.present && entry.steeringWheelMaxSpeedRadians.value > 0.0f)
        t.steeringRateRadPerSec = entry.steeringWheelMaxSpeedRadians.value;
    if (entry.steeringWheelMaxReturnSpeedRadians.present && entry.steeringWheelMaxReturnSpeedRadians.value > 0.0f)
        t.steeringCenteringRateRadPerSec = entry.steeringWheelMaxReturnSpeedRadians.value;
    if (entry.maxSpeedSteeringMs.present && entry.maxSpeedSteeringMs.value > 0.0f)
        t.steeringSpeedThresholdMPerSec = entry.maxSpeedSteeringMs.value;
    // 10.11.8 velocity damper + lift, 10.11.10 Maximum_Speed clamp.
    if (entry.normalSpinDamping.present) t.normalSpinDamping = entry.normalSpinDamping.value;
    if (entry.collisionSpinDamping.present) t.collisionSpinDamping = entry.collisionSpinDamping.value;
    if (entry.collisionSpinThresholdRadians.present) t.collisionSpinThresholdRad = entry.collisionSpinThresholdRadians.value;
    if (entry.liftCoefficient.present) t.liftCoefficient = entry.liftCoefficient.value;
    if (entry.maximumSpeedMs.present) t.maximumSpeedMs = entry.maximumSpeedMs.value;
    if (entry.maximumSpeedWithNitrousMs.present) t.maximumSpeedWithNitrousMs = entry.maximumSpeedWithNitrousMs.value;
    t.vehicleClass = static_cast<int>(entry.vehicleType.vehicleClass);
    if (entry.extraGravity.present) t.extraGravity = entry.extraGravity.value;
    if (entry.steeringWheelDampAngleRadians.present) t.steeringTurnInDampAngleRad = entry.steeringWheelDampAngleRadians.value;
    if (entry.steeringWheelReturnDampAngleRadians.present)
        t.steeringReturnDampAngleRad = entry.steeringWheelReturnDampAngleRadians.value;
}

double ApplyVelocityDamper(double omega, double normalDamping, double collisionDamping, double threshold, double dt) {
    const double d = (omega * omega > threshold * threshold) ? collisionDamping : normalDamping;
    return omega * std::max(0.0, 1.0 - d * dt);
}

void ClampPlanarSpeed(double& vForward, double& vRight, double cap) {
    if (!(cap > 0.0)) return;
    const double speed = std::sqrt(vForward * vForward + vRight * vRight);
    if (speed > cap) {
        const double k = cap / speed;
        vForward *= k;
        vRight *= k;
    }
}

bool ApplyMaximumSpeedLimit(double vel[3], MaxSpeedLimiterState& state, const MaxSpeedLimitInput& in) {
    if (in.wrecked) return false;
    double cap = (in.overrideCap >= 0.0) ? in.overrideCap : in.maximumSpeed;
    const bool exempt = in.vehicleClass == 3 || in.vehicleClass == 4 || in.vehicleClass == 5;
    if (!exempt) {
        state.nitroBlend = in.nitrousActive ? 1.0 : std::max(0.0, state.nitroBlend - 0.5 / 30.0);
        cap += (in.maximumSpeedWithNitrous - cap) * state.nitroBlend;
    }
    const double limit = cap + in.bonus;
    if (!(limit > 0.0)) return false;  // no cap data (feature off)
    const double speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    if (speed <= limit) return false;
    const double k = limit / speed;
    vel[0] *= k;
    vel[1] *= k;
    vel[2] *= k;
    return true;
}

void ComputeExtraGravityForce(const ExtraGravityInput& in, double out[3]) {
    out[0] = out[1] = out[2] = 0.0;
    if (in.vehicleClass == 3 || in.vehicleClass == 4) return;
    if (in.contactMask != 0 || in.extraGravity == 0.0) return;
    if (!in.anyWheelInContact) {
        out[1] = -10.0 * in.mass;
        return;
    }
    if (!in.allWheelsInContact) return;  // mixed contact: nothing
    const double* n = in.meanNormal;
    if (n[1] >= 0.5) {
        for (int k = 0; k < 3; ++k) out[k] = in.mass * in.extraGravity * n[k];
    }
    if (in.coastTermEnabled && in.throttle == 0.0) {
        const double* v = in.velocity;
        const double vn = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
        double vt[3] = {v[0] - n[0] * vn, v[1] - n[1] * vn, v[2] - n[2] * vn};
        const double len = std::sqrt(vt[0] * vt[0] + vt[1] * vt[1] + vt[2] * vt[2]);
        double t[3] = {0, 0, 0};
        if (len > 0.001) {
            for (int k = 0; k < 3; ++k) t[k] = vt[k] / len;
        }
        const double d = t[0] * in.forward[0] + t[1] * in.forward[1] + t[2] * in.forward[2];
        if (std::fabs(d) < 0.89408) {
            for (int k = 0; k < 3; ++k) out[k] += -0.89408 * 9.8 * in.mass * d * t[k];
        }
    }
}

ReverseDecision DecideReverse(bool fullLogic, double x, double s, bool handbrake, int vehicleClass, bool flagIn) {
    ReverseDecision r;
    r.reverseFlag = flagIn;
    x = std::clamp(x, -1.0, 1.0);
    if (!fullLogic) {  // plain mode: flag untouched
        r.accel = std::max(x, 0.0);
        r.brake = std::max(-x, 0.0);
        return r;
    }
    if (x == 0.0) {  // (a)
        if (std::fabs(s) <= 1.0) r.reverseFlag = false;
        return r;
    }
    if (s > 1.0) {  // (b)
        if (!handbrake) r.reverseFlag = false;
        if (x > 0.0) r.accel = x; else r.brake = -x;
        return r;
    }
    if (s < -1.0) {  // (c)
        if (!handbrake) r.reverseFlag = true;
        if (x > 0.0) r.brake = x;
        else if (vehicleClass != 1) r.accel = -x;  // class value 1 returns without touching accel
        return r;
    }
    // (d) |s| <= 1.0
    r.accel = std::fabs(x);
    r.brake = 0.0;
    if (vehicleClass == 1 && !(x > 0.0 || s > 0.0)) r.accel = 0.0;  // class-1 only when x > 0 or s > 0
    r.reverseFlag = (x < 0.0);
    return r;
}

double WheelTorqueRatio(bool reverseFlag, int gearIndexZeroBased, double reverseRatio, double gearRatio, double diffRatio) {
    if (reverseFlag && gearIndexZeroBased <= 0) return -(reverseRatio * diffRatio);
    return gearRatio * diffRatio;
}

double SteeringLimiterAccel(double v, double damp) { return damp > 0.0 ? v * v / (2.0 * damp) : 0.0; }

double SteeringFullLockTimeSec(double theta, double v, double a) {
    if (!(a > 0.0) || !(v > 0.0)) return 0.0;
    if (theta <= v * v / a) return 2.0 * std::sqrt(theta / a);
    return 2.0 * v / a + (theta - v * v / a) / v;
}

double WheelRadiusFromSizeIndex(int idx) {
    static const double Ts[27] = {0.4,  0.5,  0.63, 0.75, 0.8,  0.9,  0.95, 1.0,  1.05, 1.1,  1.15, 1.2,  1.25, 1.3,
                                  1.35, 1.4,  1.45, 1.5,  1.55, 1.6,  1.7,  1.75, 1.85, 1.9,  2.2,  2.4,  2.8};
    return 0.2048 * Ts[std::clamp(idx, 0, 26)];
}

bool StaticSagFront(double zF, double zR, double zC, double gEff, int nF, double strengthF, double& sagF) {
    const double L = zF - zR;
    sagF = 0.0;
    if (!(zF > zC) || !(L > 0.0) || nF <= 0 || !(strengthF > 0.0)) return false;
    sagF = -gEff * (zC - zR) / L / (nF * strengthF);
    return true;
}

bool StaticSagRear(double zF, double zR, double zC, double gEff, int nR, double strengthR, double& sagR) {
    const double L = zF - zR;
    sagR = 0.0;
    if (!(zF > zC) || !(L > 0.0) || nR <= 0 || !(strengthR > 0.0)) return false;
    sagR = -gEff * (zF - zC) / L / (nR * strengthR);
    return true;
}

double FrictionRatioX(double sumWheelMass, double mass, double cZ, double cY, double yawTF, double yawUI,
                      double rollTF, double rollUI) {
    if (!(mass > 0.0) || !(yawUI > 0.0) || !(rollUI > 0.0)) return 0.0;
    const double invMass = 1.0 / mass;
    const double mEffInv = invMass + cZ * cZ * (yawTF / yawUI) * invMass + cY * cY * (rollTF / rollUI) * invMass;
    return sumWheelMass * mEffInv;
}

FrictionSlipTable BuildFrictionSlipTableFromSpec(double x) {
    FrictionSlipTable t;
    if (!(x > 0.0)) return t;
    const double q = std::pow(16.0 / x, 1.0 / 15.0);
    double s = x / 16.0;
    for (int i = 0; i < 15; ++i) {
        t.entries[static_cast<size_t>(i)] = 1.0 - std::pow(1.0 - s, 1.0 / x);
        s *= q;
    }
    t.entries[15] = 1.0;
    t.start = x / 16.0;
    t.q = q;
    return t;
}

double LiftForceN(double rho, double cl, double area, double v) { return 0.5 * rho * cl * area * v * v; }

}  // namespace sr3physics
