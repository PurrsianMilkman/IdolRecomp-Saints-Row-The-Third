// Synthetic tests for the driver chase camera (sr3player/vehicle_camera.h). Every expected value is a
// spec-player-runtime.md Sec4.15.4 test vector (or a rule quoted from Sec4.15.2/Sec4.15.3).
#include <cmath>
#include <iostream>

#include "sr3player/vehicle_camera.h"

using namespace sr3player;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
double deg(double r) { return r * 180.0 / kVehCamPi; }
double rad(double d) { return d * kVehCamPi / 180.0; }

struct Row {
    double lookY, fd, fh, S, maxFov;
};

VehicleCameraParams make(const Row& r) {
    VehicleCameraParams p;
    p.lookatOffsetY = r.lookY;
    p.followDistance = r.fd;
    p.followHeight = r.fh;
    p.maxSpeedMs = r.S;
    p.maxFov = rad(r.maxFov);
    return p;
}

class WallCollision : public CameraCollision {
public:
    std::optional<CameraSweepHit> sweepSphere(Vec3 from, Vec3 to, double) override {
        last = {from, to};
        return CameraSweepHit{Vec3{from.x * 0.5 + to.x * 0.5, from.y * 0.5 + to.y * 0.5, from.z * 0.5 + to.z * 0.5},
                              Vec3{0, 1, 0}};
    }
    std::pair<Vec3, Vec3> last;
};
}  // namespace

int main() {
    // --- Sec4.15.4 chase-cam table ---
    const Row genki{1.15, 7.5, 0.7, 37.9984, 75};
    const Row sports{0.9, 7.5, 0.5, 44.704, 75};
    const Row pickup{0.8, 7.65, 0.7, 33.528, 65};
    const auto pg = make(genki), ps = make(sports), pp = make(pickup);
    CHECK(near(VehicleCamFollowDistance(pg, 0), 7.53260, 5e-5));
    CHECK(near(VehicleCamFollowDistance(pg, 30), 7.53260, 5e-5));  // M = 1: constant
    CHECK(near(VehicleCamFollowDistance(ps, 0), 7.51665, 5e-5));
    CHECK(near(VehicleCamFollowDistance(pp, 0), 7.68196, 5e-5));
    CHECK(near(deg(std::atan2(0.7, 7.5)), 5.3322, 5e-4));
    CHECK(near(deg(std::atan2(0.5, 7.5)), 3.8141, 5e-4));
    CHECK(near(deg(std::atan2(0.7, 7.65)), 5.2282, 5e-4));
    CHECK(near(deg(VehicleCamFov(pg, 0)), 50.000, 5e-4));
    CHECK(near(deg(VehicleCamFov(pg, 30)), 69.738, 5e-4));
    CHECK(near(deg(VehicleCamFov(ps, 30)), 66.777, 5e-4));
    CHECK(near(deg(VehicleCamFov(pp, 30)), 63.422, 5e-4));
    CHECK(near(deg(VehicleCamFov(pg, 1000)), 75.0, 1e-9));  // Max_FOV caps (no clamp on speed/S)
    bool written = true;
    VehicleCameraParams p0 = pg;
    p0.maxSpeedMs = 0;
    VehicleCamFov(p0, 5, &written);
    CHECK(!written);  // S = 0: FOV not written

    // Min_Follow_Dist_Multiplier: closer as speed rises.
    VehicleCameraParams pm = pg;
    pm.minFollowDistMult = 0.5;
    const double d0 = std::sqrt(7.5 * 7.5 + 0.7 * 0.7);
    CHECK(near(VehicleCamFollowDistance(pm, 0), d0, 1e-12));
    CHECK(near(VehicleCamFollowDistance(pm, 37.9984), d0 * 0.5, 1e-9));

    // --- Swing rate: Swing_Rate 5 -> r = 0.0076459 ---
    const double r = VehicleCamSwingRate(5.0);
    CHECK(near(r, 0.0076459, 5e-8));
    CHECK(VehicleCamSwingRate(0.0) == 1.0);
    // --- One smoothing step (genki, q = 1, step 1/30, n = 4) ---
    const double a = VehicleCamSwingA(1.0, r, 1.0 / 30.0);
    CHECK(near(a, 0.0302346, 5e-8));
    const SwingStepResult s = VehicleCamSwingStep(3.76630, 6.52342, 0.0, 7.53260, a, 0.2);
    CHECK(near(s.nFirst, 3.65243, 5e-5));
    CHECK(near(s.nSecond, 6.55393, 5e-5));
    CHECK(near(deg(std::atan2(s.nFirst, s.nSecond)), 29.13039, 5e-5));
    CHECK(near(deg(s.deltaRad), -0.86961, 5e-5));
    CHECK(near(deg(s.appliedRad), -0.69569, 5e-5));
    CHECK(near(30.0 + deg(s.appliedRad), 29.30431, 5e-5));
    // On foot comparison (q = 1, r = 1 -> a = 1): not part of this module, but a == 1 must hold.
    CHECK(near(VehicleCamSwingA(1.0, 1.0, 1.0 / 30.0), 1.0, 1e-12));
    // Pitch ease keeps 0.857696 per 1/30 s.
    CHECK(near(10.0 * std::pow(0.01, 1.0 / 30.0), 8.57696, 5e-5));

    // --- Placement: camera 1.85 above the reference point at zero pitch, horizontal FD behind ---
    {
        VehicleChaseCamera cam(pg);
        VehicleCameraInput in;
        in.referencePoint = Vec3{10, 5, -3};
        in.vehicleHeading = 0.0;  // forward = +x
        in.speed = 0;
        const auto o = cam.update(in, nullptr);
        CHECK(near(o.eye.y - in.referencePoint.y, 1.85, 1e-9));
        CHECK(near(in.referencePoint.x - o.eye.x, 7.5, 1e-9));
        CHECK(near(o.eye.z, -3.0, 1e-9));
        CHECK(near(o.distance, 7.53260, 5e-5));
        CHECK(near(deg(o.fov), 50.0, 1e-9));
    }
    // --- Pitch clamp [-35, 55] and proximity lock -0.2 ---
    {
        VehicleChaseCamera cam(pg);
        VehicleCameraInput in;
        cam.update(in, nullptr);
        cam.applyLook(0, rad(170), false);
        in.step = 0.0001;  // negligible ease; hold keeps the swing off
        auto o = cam.update(in, nullptr);
        CHECK(near((o.eye.y - o.pivot.y) / o.distance, std::sin(rad(55)), 1e-9));
        CHECK(near(deg(cam.userPitch() + std::atan2(0.7, 7.5)), 55.0, 1e-9));
        cam.applyLook(0, rad(-400), false);
        o = cam.update(in, nullptr);
        CHECK(near(deg(cam.userPitch() + std::atan2(0.7, 7.5)), -35.0, 1e-9));
        // Below -0.2 on back.y the lock raises it: back.y = sin(-35 deg) = -0.5736 -> floored to -0.2, then re-normalised.
        { const double cp = std::cos(rad(-35)); CHECK(near((o.eye.y - o.pivot.y) / o.distance, -0.2 / std::sqrt(0.04 + cp * cp), 1e-9)); }
    }
    // --- Swing: camera lags the heading by (1-hr)*delta*a per step and never lags in position ---
    {
        VehicleChaseCamera cam(pg);
        VehicleCameraInput in;
        in.vehicleHeading = 0.0;
        in.speed = 20;
        in.velX = 20;
        cam.update(in, nullptr);  // cut: camera heading 0
        // Vehicle now travels 30 deg off the camera's heading.
        in.velX = 20 * std::cos(rad(30));
        in.velZ = 20 * std::sin(rad(30));
        in.step = 1.0 / 30.0;
        cam.update(in, nullptr);
        // Vehicle-travel direction is 30 deg off; C is 0 deg: heading moves towards +30 by 0.8*delta.
        CHECK(cam.heading() > 0.0 && cam.heading() < rad(1.0));
        // Converges towards the travel direction.
        for (int i = 0; i < 4000; ++i) cam.update(in, nullptr);
        CHECK(near(deg(cam.heading()), 30.0, 0.05));
    }
    // --- Standing still: |velocity|^2 < 0.01 means no swing ---
    {
        VehicleChaseCamera cam(pg);
        VehicleCameraInput in;
        in.vehicleHeading = 1.0;
        cam.update(in, nullptr);
        in.vehicleHeading = 2.0;
        in.velX = 0.05;
        cam.update(in, nullptr);
        CHECK(near(cam.heading(), 1.0, 1e-12));
    }
    // --- Manual look hold: 10 ms moving / 500 ms stopped ---
    {
        VehicleChaseCamera cam(pg);
        VehicleCameraInput in;
        in.velX = 20;
        cam.update(in, nullptr);
        cam.applyLook(0.3, 0, true);
        CHECK(near(cam.holdSeconds(), 0.5, 1e-12));
        cam.applyLook(0, 0, false);
        CHECK(near(cam.holdSeconds(), 0.01, 1e-12));
    }
    // --- Collision: the sweep runs pivot -> eye and its hit position becomes the eye ---
    {
        VehicleChaseCamera cam(pg);
        WallCollision wall;
        VehicleCameraInput in;
        const auto o = cam.update(in, &wall);
        CHECK(o.collided);
        CHECK(near(wall.last.first.y, in.referencePoint.y + 1.15, 1e-12));
        CHECK(near(o.distance, 7.53260 * 0.5, 5e-5));
    }
    // --- Stats ---
    {
        VehicleCameraStats st;
        st.add(7.0, 1.0, false);
        st.add(9.0, 1.2, true);
        CHECK(st.frames == 2 && st.collided == 1 && st.distMin == 7.0 && st.distMax == 9.0 && st.fovMax == 1.2);
    }

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "synthetic_player_vehicle_camera_test: all checks passed\n";
    return 0;
}
