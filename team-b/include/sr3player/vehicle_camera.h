// sr3player - the driver chase camera, spec-player-runtime.md Sec4.15.2-Sec4.15.4 and Sec4.15.6.
// Pure arithmetic, no engine state: the caller feeds the vehicle pose, velocity and a collision query.
//
// FINAL (spec): follow distance d = d0*(M+(1-M)*t), d0 = sqrt(FD^2+FH^2), t = clamp(1-speed/S,0,1);
// elevation atan(FH/FD); total pitch clamped to [Min,Max] pitch; back-vector y floor at the proximity
// lock -0.2; FOV = min((base+(MaxFov-base)*speed/S)*fovScale, MaxFov) (S = 0: not written);
// position lag none; follow swing a = 1-(1-q*r)^n on the 120 Hz tick, heading += (1-hr)*delta;
// pitch ease 0.01^step; pad hold 10 ms moving / 500 ms stopped; r = 1-exp(K/Swing_Rate).
// CHOSEN stand-ins are labelled in vehicle_camera.cpp.
#pragma once
#include <optional>

#include "sr3player/camera.h"

namespace sr3player {

inline constexpr double kVehCamSwingK = -0.038376417011022570;  // Sec4.15.3 (double at 0x01184050)
inline constexpr double kVehCamSwingTickSeconds = 1.0 / 120.0;  // Sec4.15.3 (0x0111bde0)
inline constexpr double kVehCamProximityLockY = -0.2;           // Sec4.15.2 item 2
inline constexpr double kVehCamMaxSpeedCapMs = 44.704;          // Sec4.15.2 item 4
inline constexpr double kVehCamPi = 3.14159265358979323846;

// One vehicle's resolved camera row (Sec4.15.2: installed record after inheritance).
struct VehicleCameraParams {
    double lookatOffsetY = 1.15;  // Lookat_Offset.Y
    double followDistance = 7.5;  // FD
    double followHeight = 0.7;    // FH
    double minPitch = -35.0 * kVehCamPi / 180.0;
    double maxPitch = 55.0 * kVehCamPi / 180.0;
    double baseFov = 50.0 * kVehCamPi / 180.0;  // blended base_fov
    double maxFov = 75.0 * kVehCamPi / 180.0;   // Max_FOV
    double maxSpeedMs = 37.9984;                // S, already min(mph*0.44704, 44.704)
    double minFollowDistMult = 1.0;             // M
    double fovScale = 1.0;                      // camera_fov_scale, clamped [0,1]
    double swingRate = 5.0;                     // authored Swing_Rate_Fwd (= Rev for the test groups)
    double headingRetention = 0.2;              // Heading_Retention_Normal/Turning (0.2/0.2 in test groups)
};

// The `car_4dr_genki` row of Sec4.15.4.
VehicleCameraParams GenkiCameraParams();

double VehicleCamFollowDistance(const VehicleCameraParams& p, double speed);  // d (Sec4.15.2)
double VehicleCamFov(const VehicleCameraParams& p, double speed, bool* written = nullptr);  // radians
double VehicleCamSwingRate(double authoredSwingRate);                         // r = 1-exp(K/rate), [0,1]
double VehicleCamSwingA(double q, double r, double step);                     // a = 1-(1-f)^n

// One swing step on horizontal vectors. Angles are atan2(first, second) of each vector (so with
// (lateral, along) components the angle runs from +along towards +lateral, as in the Sec4.15.4 vector).
struct SwingStepResult {
    double nFirst = 0, nSecond = 0;  // N = C + (D-C)*a
    bool nearZero = false;           // N within 1e-5 on all axes: the spec stops, no heading change
    double deltaRad = 0;             // signed angle(N) - angle(C), wrapped to [-pi, pi)
    double appliedRad = 0;           // (1-hr)*delta
};
SwingStepResult VehicleCamSwingStep(double cFirst, double cSecond, double dFirst, double dSecond, double a,
                                    double headingRetention);

struct VehicleCameraInput {
    Vec3 referencePoint;        // vehicle reference point (OPEN in spec; caller supplies)
    double vehicleHeading = 0;  // radians, forward = (cos h, 0, sin h); used only to seed a cut
    double velX = 0, velZ = 0;  // world horizontal velocity (swing reference D)
    double speed = 0;           // 3-D body speed (FOV and distance)
    double step = 1.0 / 30.0;   // seconds
};

struct VehicleCameraOutput {
    Vec3 eye, pivot;
    double fov = 0;             // radians (last written value if S == 0)
    double distance = 0;        // eye-to-pivot after collision
    double followDistance = 0;  // d before collision
    bool collided = false;
};

class VehicleChaseCamera {
public:
    explicit VehicleChaseCamera(VehicleCameraParams p = GenkiCameraParams()) : p_(p) {}
    void setParams(const VehicleCameraParams& p) { p_ = p; }
    const VehicleCameraParams& params() const { return p_; }
    // Camera cut: the next update places the camera directly behind the vehicle heading.
    void reset() { initialised_ = false; }
    // Manual look: yaw offsets the camera heading, pitch adds to the user pitch (clamped at update); both start a
    // swing hold of 10 ms moving / 500 ms stopped (pad values, Sec4.15.2).
    void applyLook(double yawRad, double pitchRad, bool stopped);
    VehicleCameraOutput update(const VehicleCameraInput& in, CameraCollision* collision);
    double heading() const { return heading_; }
    double userPitch() const { return userPitch_; }
    double holdSeconds() const { return holdSeconds_; }

private:
    VehicleCameraParams p_;
    bool initialised_ = false;
    double heading_ = 0;  // camera yaw (atan2(z, x) of the camera forward)
    double userPitch_ = 0;
    double holdSeconds_ = 0;
    double fov_ = -1;
    Vec3 eye_;  // committed camera position
};

// Headless trace accumulator for the "vehicle-camera-trace:" line.
struct VehicleCameraStats {
    long frames = 0;
    double distSum = 0, distMin = 1e30, distMax = 0;
    double fovMin = 1e30, fovMax = 0;  // radians
    long collided = 0;
    void add(double distance, double fovRad, bool hit);
};

}  // namespace sr3player
