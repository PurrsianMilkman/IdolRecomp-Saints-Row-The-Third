// sr3player - the driver chase camera (spec-player-runtime.md Sec4.15.2-Sec4.15.4). See the header.
#include <algorithm>
#include <cmath>

#include "sr3player/vehicle_camera.h"

namespace sr3player {

namespace {
double wrapPi(double a) {
    const double twoPi = 2.0 * kVehCamPi;
    a = std::fmod(a + kVehCamPi, twoPi);
    if (a < 0) a += twoPi;
    return a - kVehCamPi;
}
}  // namespace

VehicleCameraParams GenkiCameraParams() { return VehicleCameraParams{}; }

double VehicleCamFollowDistance(const VehicleCameraParams& p, double speed) {
    const double d0 = std::sqrt(p.followDistance * p.followDistance + p.followHeight * p.followHeight);
    if (p.maxSpeedMs <= 0.0) return d0;  // the speed rule needs S > 0
    const double t = std::clamp(1.0 - speed / p.maxSpeedMs, 0.0, 1.0);
    const double m = p.minFollowDistMult;
    return d0 * (m + (1.0 - m) * t);
}

double VehicleCamFov(const VehicleCameraParams& p, double speed, bool* written) {
    if (written) *written = p.maxSpeedMs > 0.0;
    if (p.maxSpeedMs <= 0.0) return p.baseFov;  // S = 0: not written this update
    const double scale = std::clamp(p.fovScale, 0.0, 1.0);
    return std::min((p.baseFov + (p.maxFov - p.baseFov) * speed / p.maxSpeedMs) * scale, p.maxFov);
}

double VehicleCamSwingRate(double authoredSwingRate) {
    if (authoredSwingRate == 0.0) return 1.0;  // an authored 0 gives 1.0
    return std::clamp(1.0 - std::exp(kVehCamSwingK / authoredSwingRate), 0.0, 1.0);
}

double VehicleCamSwingA(double q, double r, double step) {
    const double f = std::clamp(q * r, 0.0, 1.0);
    const double n = step / kVehCamSwingTickSeconds;
    return 1.0 - std::pow(1.0 - f, n);
}

SwingStepResult VehicleCamSwingStep(double cA, double cB, double dA, double dB, double a, double hr) {
    SwingStepResult r;
    r.nFirst = cA + (dA - cA) * a;
    r.nSecond = cB + (dB - cB) * a;
    if (std::fabs(r.nFirst) <= 1e-5 && std::fabs(r.nSecond) <= 1e-5) {  // cmpleps: <=
        r.nearZero = true;
        return r;
    }
    r.deltaRad = wrapPi(std::atan2(r.nFirst, r.nSecond) - std::atan2(cA, cB));
    r.appliedRad = (1.0 - hr) * r.deltaRad;
    return r;
}

void VehicleChaseCamera::applyLook(double yawRad, double pitchRad, bool stopped) {
    heading_ += yawRad;
    userPitch_ += pitchRad;
    // Pad hold (Sec4.15.2): 10 ms while moving, 500 ms when speed^2 < 1.0. CHOSEN: the mouse hold (250 ms when
    // 0x0056a850 holds, else 0 ms) is not modelled; the pad values are used for every look source.
    holdSeconds_ = stopped ? 0.5 : 0.01;
}

VehicleCameraOutput VehicleChaseCamera::update(const VehicleCameraInput& in, CameraCollision* collision) {
    VehicleCameraOutput out;
    const double d = VehicleCamFollowDistance(p_, in.speed);
    const double e0 = std::atan2(p_.followHeight, p_.followDistance);
    // CHOSEN: pivot = reference point + (0, Lookat_Offset.Y, 0); the spec rotates the offset by heading but the
    // road-vehicle row is Y-only (Sec4.15.4), so rotation is moot here.
    const Vec3 pivot{in.referencePoint.x, in.referencePoint.y + p_.lookatOffsetY, in.referencePoint.z};
    auto place = [&](double h, double pitchTotal) {
        const double cp = std::cos(pitchTotal);
        Vec3 back{-cp * std::cos(h), std::sin(pitchTotal), -cp * std::sin(h)};
        if (back.y < kVehCamProximityLockY) {  // raised to at least -0.2, then re-normalised (Sec4.15.2)
            back.y = kVehCamProximityLockY;
            back = back * (1.0 / Length(back));
        }
        return pivot + back * d;
    };
    const auto clampedPitch = [&]() {
        const double total = std::clamp(e0 + userPitch_, p_.minPitch, p_.maxPitch);
        userPitch_ = total - e0;
        return total;
    };

    if (!initialised_) {
        heading_ = in.vehicleHeading;
        initialised_ = true;
        eye_ = place(heading_, clampedPitch());
    } else {
        // Follow swing (Sec4.15.3): q = 1, r from the authored rate; D = horizontal velocity, normalised, x d;
        // C = pivot - committed camera position. No swing while |D|^2 < 0.01 or during a manual-look hold.
        const double vlen2 = in.velX * in.velX + in.velZ * in.velZ;
        if (holdSeconds_ > 0.0) {
            holdSeconds_ = std::max(0.0, holdSeconds_ - in.step);
        } else if (vlen2 >= 0.01) {
            const double inv = d / std::sqrt(vlen2);
            const double a = VehicleCamSwingA(1.0, VehicleCamSwingRate(p_.swingRate), in.step);
            // Components are passed as (z, x): angle = atan2(z, x) = the heading convention.
            const SwingStepResult s = VehicleCamSwingStep(pivot.z - eye_.z, pivot.x - eye_.x, in.velZ * inv,
                                                          in.velX * inv, a, p_.headingRetention);
            if (!s.nearZero) {
                heading_ += s.appliedRad;
                heading_ = std::fmod(heading_, 2.0 * kVehCamPi);  // wrapped into [0, 2pi)
                if (heading_ < 0) heading_ += 2.0 * kVehCamPi;
                // Pitch ease 0.01^step, while min elevation < 0 and |delta| > 0.001.
                if (std::fabs(s.deltaRad) > 0.001 && p_.minPitch < 0.0) userPitch_ *= std::pow(0.01, in.step);
            }
        }
        eye_ = place(heading_, clampedPitch());
    }

    // Collision. CHOSEN: Sec4.15 specifies none for the driver camera; the on-foot sweep (Sec4.9: sphere radius
    // 0.12, from the pivot to the placed eye, stop at the hit position) is reused.
    Vec3 eye = eye_;
    if (collision) {
        if (const auto hit = collision->sweepSphere(pivot, eye_, kCameraCollisionRadius)) {
            eye = hit->position;
            out.collided = true;
        }
    }
    // The committed position includes the collision result (Sec4.15.3: C = pivot - committed camera position).
    eye_ = eye;

    bool written = false;
    const double fov = VehicleCamFov(p_, in.speed, &written);
    if (written || fov_ < 0) fov_ = fov;
    out.eye = eye;
    out.pivot = pivot;
    out.fov = fov_;
    out.followDistance = d;
    out.distance = Length(pivot - eye);
    return out;
}

void VehicleCameraStats::add(double distance, double fovRad, bool hit) {
    ++frames;
    distSum += distance;
    distMin = std::min(distMin, distance);
    distMax = std::max(distMax, distance);
    fovMin = std::min(fovMin, fovRad);
    fovMax = std::max(fovMax, fovRad);
    if (hit) ++collided;
}

}  // namespace sr3player
