#pragma once
// sr3airuntime/freeroam_population.h - free-roam population rules,
// spec-ai-runtime.md Sec20.6 (ce704b4): TOD index, vehicle/ped target live
// counts, moving cone, ring crossing, despawn radii, ped reject rules, bulk
// fill, lateral offset. Header-only, pure functions. Test vectors: Sec20.6.4.
#include <algorithm>
#include <cmath>

#include "sr3airuntime/common.h"

namespace sr3airuntime {

// Sec20.6.0(a): TOD segment starts 500/930/1430/2100 switch at midpoints:
// 01:00-07:15 -> 0, 07:15-12:00 -> 1, 12:00-17:45 -> 2, 17:45-01:00 -> 3. FINAL.
inline int TodDensityIndex(int hhmm) {
    if (hhmm >= 100 && hhmm < 715) return 0;
    if (hhmm >= 715 && hhmm < 1200) return 1;
    if (hhmm >= 1200 && hhmm < 1745) return 2;
    return 3;
}
inline int TodDensityIndexFromDayFraction(double dayFraction) {
    double f = dayFraction - std::floor(dayFraction);
    int minutes = static_cast<int>(f * 1440.0 + 1e-6);
    return TodDensityIndex((minutes / 60) * 100 + (minutes % 60));
}

// A spawn category's CarDay/CarNight/PedDay/PedNight (Sec20.6.0(a)).
struct SpawnCategoryDensity {
    float carDay = 0.8f, carNight = 0.5f, pedDay = 0.8f, pedNight = 0.4f;
};
// CHOSEN stand-in: hood->category mapping (Sec20.6.8) not wired; Default Spawn.
inline SpawnCategoryDensity DefaultSpawnCategory() { return {0.8f, 0.5f, 0.8f, 0.4f}; }

inline float TodBlend(float day, float night, int idx) {
    switch (idx) {
        case 0: return night;
        case 1: return (day + night) * 0.5f;
        default: return day;
    }
}
// Sec20.6.0(c) notoriety scale, levels 0..5.
inline float NotorietyDensityScale(int level) {
    static const float t[6] = {1.0f, 1.0f, 0.75f, 0.5f, 0.25f, 0.1f};
    return t[std::clamp(level, 0, 5)];
}

// Sec20.6.1(a): base 12 on foot / 18 driving; capV default 1.0.
inline int VehicleTargetLiveCount(const SpawnCategoryDensity& c, int todIdx, int notorietyLevel,
                                  bool playerDriving, float capV = 1.0f) {
    const float base = playerDriving ? 18.0f : 12.0f;
    float d = TodBlend(c.carDay, c.carNight, todIdx);
    float s = std::min(d * NotorietyDensityScale(notorietyLevel), capV);
    return std::max(1, static_cast<int>(base * s));
}
// Sec20.6.2(a): base 40; d *= weather PedDensity; capP default 1.0.
inline int PedTargetLiveCount(const SpawnCategoryDensity& c, int todIdx, int notorietyLevel,
                              float weatherPedDensity, float capP = 1.0f, int base = 40) {
    float d = TodBlend(c.pedDay, c.pedNight, todIdx) * weatherPedDensity;
    float s = std::min(NotorietyDensityScale(notorietyLevel) * d, capP);
    return std::min(base, std::max(1, static_cast<int>(static_cast<float>(base) * s)));
}

// Sec20.6.1(c) moving cone. Moving threshold 2.2352 m/s, full at 13.4112 m/s.
inline constexpr double kCameraMovingSpeed = 2.2352;
inline double MovingConeMinDot(double cameraSpeed) {
    if (cameraSpeed <= kCameraMovingSpeed) return -1.0;
    return 0.5 * std::min(cameraSpeed, 13.4112) / 13.4112;
}

// Sec20.6.1(c) spawn points: where the chord A->B crosses the circle (x-z plane).
// Returns the first crossing along A->B with t in [0,1].
inline bool ChordCircleCrossing(Vec3 a, Vec3 b, Vec3 centre, double radius, Vec3& out) {
    const double dx = b.x - a.x, dz = b.z - a.z;
    const double fx = a.x - centre.x, fz = a.z - centre.z;
    const double A = dx * dx + dz * dz;
    if (A < 1e-12) return false;
    const double B = 2.0 * (fx * dx + fz * dz);
    const double C = fx * fx + fz * fz - radius * radius;
    const double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return false;
    const double sq = std::sqrt(disc);
    for (double t : {(-B - sq) / (2.0 * A), (-B + sq) / (2.0 * A)}) {
        if (t >= 0.0 && t <= 1.0) {
            out = {a.x + dx * t, a.y + (b.y - a.y) * t, a.z + dz * t};
            return true;
        }
    }
    return false;
}

// Both chord/circle crossings (x-z plane) with t in [0,1], in order along A->B. Returns the count (0..2).
// Sec20.6.1(c): "where the segment's chord crosses the circle" - a chord that enters and leaves the ring yields
// two points (the review RG-7 found only the first was kept).
inline int ChordCircleCrossings(Vec3 a, Vec3 b, Vec3 centre, double radius, Vec3 out[2]) {
    const double dx = b.x - a.x, dz = b.z - a.z;
    const double fx = a.x - centre.x, fz = a.z - centre.z;
    const double A = dx * dx + dz * dz;
    if (A < 1e-12) return 0;
    const double B = 2.0 * (fx * dx + fz * dz);
    const double C = fx * fx + fz * fz - radius * radius;
    const double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return 0;
    const double sq = std::sqrt(disc);
    int n = 0;
    double prevT = -1.0;
    for (double t : {(-B - sq) / (2.0 * A), (-B + sq) / (2.0 * A)}) {
        if (t >= 0.0 && t <= 1.0 && !(n > 0 && std::fabs(t - prevT) < 1e-12)) {
            out[n++] = {a.x + dx * t, a.y + (b.y - a.y) * t, a.z + dz * t};
            prevT = t;
        }
    }
    return n;
}

// Sec20.6.1(b) camera speed = |displacement this frame| / (1/30 s): a FIXED 30 Hz factor (0x0132a0ac = 0.0333),
// not the real frame time. Sec20.6.1(d) 2026-10-15 FINAL: the despawn "moving" gate is
// dot(camera forward, displacement) x 30 > 2.2352, and the "behind" test is dot(vehicle - camera, displacement) < 0.
inline constexpr double kCameraSpeedHz = 30.0;
inline Vec3 CameraVelocityFromDisplacement(const Vec3& delta) {
    return {delta.x * kCameraSpeedHz, delta.y * kCameraSpeedHz, delta.z * kCameraSpeedHz};
}
inline double DespawnForwardSpeed(const Vec3& camForward, const Vec3& delta) {
    return (camForward.x * delta.x + camForward.y * delta.y + camForward.z * delta.z) * kCameraSpeedHz;
}
inline bool DespawnCameraMoving(const Vec3& camForward, const Vec3& delta) {
    return DespawnForwardSpeed(camForward, delta) > kCameraMovingSpeed;
}
inline bool VehicleBehindCameraMotion(const Vec3& offsetFromCamera, const Vec3& delta) {
    return offsetFromCamera.x * delta.x + offsetFromCamera.y * delta.y + offsetFromCamera.z * delta.z < 0.0;
}

// Sec20.6.1(c) highway rule: when the player's current segment has flag bit 4 and fewer than trunc(0.75 x target)
// near-field vehicles are on bit-4 segments, only bit-4 segments are accepted.
inline bool HighwayRuleActive(bool playerSegmentIsHighway, int vehiclesOnHighwaySegments, int targetLiveCount) {
    return playerSegmentIsHighway && vehiclesOnHighwaySegments < static_cast<int>(0.75 * targetLiveCount);
}

// Sec20.6.1(c) "Inner ring" visibility: is the +-halfExtent box around `centre` inside the camera frustum?
// vp is a row-vector view*projection matrix (clip = [x y z 1] * vp, D3D clip volume -w<x<w, -w<y<w, 0<z<w).
// Conservative box test: not visible only when all 8 corners are outside one plane. (The engine also applies an
// occlusion test, 0x005dcad0, which is not modelled here.)
inline bool BoxInFrustum(const float vp[16], const Vec3& centre, double halfExtent) {
    int outside[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 8; ++i) {
        const double x = centre.x + ((i & 1) ? halfExtent : -halfExtent);
        const double y = centre.y + ((i & 2) ? halfExtent : -halfExtent);
        const double z = centre.z + ((i & 4) ? halfExtent : -halfExtent);
        double c[4];
        for (int k = 0; k < 4; ++k) c[k] = x * vp[0 + k] + y * vp[4 + k] + z * vp[8 + k] + vp[12 + k];
        if (c[0] < -c[3]) ++outside[0];
        if (c[0] > c[3]) ++outside[1];
        if (c[1] < -c[3]) ++outside[2];
        if (c[1] > c[3]) ++outside[3];
        if (c[2] < 0.0) ++outside[4];
        if (c[2] > c[3]) ++outside[5];
    }
    for (int p = 0; p < 6; ++p)
        if (outside[p] == 8) return false;
    return true;
}

// Sec20.6.1(d) vehicle distance despawn.
struct VehicleDespawnContext {
    bool playerOnFoot = true;
    bool visible = false;
    bool cameraMoving = false;
    bool behindCamera = false;
    bool specialClass = false;
    bool activityMode2Still = false;
    double distanceScale = 1.0;
};
inline double VehicleDespawnDistance(const VehicleDespawnContext& c) {
    double d = 140.0;
    if (c.specialClass) d = 250.0;
    else if (!c.visible && c.cameraMoving && c.behindCamera) d = 65.0;
    else if (c.activityMode2Still) d = 90.0;
    if (c.visible) d = std::max(d, 140.0);
    return (c.playerOnFoot ? d : d + 60.0) * c.distanceScale;
}
inline bool ShouldDespawnVehicleByDistance(const VehicleDespawnContext& c, double dist) {
    return dist > VehicleDespawnDistance(c);
}

// Sec20.6.2(c) ped creation rejects 1,2,3,5 (ground/clearance/nav/resource are caller-side).
enum class PedSpawnReject { None, TooFar, TooClose, VisibleNear, Crowded };
struct PedSpawnPointFacts {
    double distToNearestViewer = 50.0;
    bool visible = false;
    int pedsWithin20m = 0;
    bool bulkPath = false;
};
inline PedSpawnReject EvaluatePedSpawnReject(const PedSpawnPointFacts& f) {
    if (f.distToNearestViewer > 90.0) return PedSpawnReject::TooFar;
    if (!f.bulkPath) {
        if (f.distToNearestViewer < 10.0) return PedSpawnReject::TooClose;
        if (f.visible && f.distToNearestViewer < 70.0) return PedSpawnReject::VisibleNear;
    }
    if (f.pedsWithin20m >= 7) return PedSpawnReject::Crowded;
    return PedSpawnReject::None;
}
// Sec20.6.2(d): horizontal distance > 90 m from every player.
inline bool ShouldDespawnPedByDistance(double horizDist) { return horizDist > 90.0; }
// Sec20.6.2(b): bulk fill n = trunc(0.33 * min(target - live, headroom)).
inline int PedBulkFillCount(int target, int live, int headroom) {
    const int need = std::min(target - live, headroom);
    return need > 0 ? static_cast<int>(0.33f * static_cast<float>(need)) : 0;
}
// Sec20.6.2(c) point: lateral offset uniform in +-width/2.
inline double PedLateralOffsetFromRand(double rand01, double width) { return (rand01 - 0.5) * width; }

}  // namespace sr3airuntime
