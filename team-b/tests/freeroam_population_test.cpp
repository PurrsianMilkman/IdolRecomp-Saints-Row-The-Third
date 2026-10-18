// Tests for include/sr3airuntime/freeroam_population.h - spec-ai-runtime.md
// Sec20.6 free-roam population rules, checked against the spec's own test
// vectors (Sec20.6.4).
#include <cmath>
#include <cstdio>

#include "sr3airuntime/freeroam_population.h"

using namespace sr3airuntime;

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);      \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

int main() {
    // --- Sec20.6.0(a) TOD index ---
    CHECK(TodDensityIndex(300) == 0);
    CHECK(TodDensityIndex(800) == 1);
    CHECK(TodDensityIndex(1400) == 2);
    CHECK(TodDensityIndex(2300) == 3);
    CHECK(TodDensityIndex(100) == 0);    // 01:00 boundary belongs to 0
    CHECK(TodDensityIndex(59) == 3);     // 00:59 still evening segment
    CHECK(TodDensityIndex(714) == 0);
    CHECK(TodDensityIndex(715) == 1);
    CHECK(TodDensityIndex(1200) == 2);
    CHECK(TodDensityIndex(1745) == 3);
    CHECK(TodDensityIndexFromDayFraction(3.0 / 24.0) == 0);
    CHECK(TodDensityIndexFromDayFraction(14.0 / 24.0) == 2);

    // --- Sec20.6.4 vehicle/ped target vectors ---
    const SpawnCategoryDensity def{0.8f, 0.5f, 0.8f, 0.4f};
    CHECK(VehicleTargetLiveCount(def, 0, 0, false) == 6);
    CHECK(VehicleTargetLiveCount(def, 0, 0, true) == 9);
    CHECK(VehicleTargetLiveCount(def, 1, 0, false) == 7);
    CHECK(VehicleTargetLiveCount(def, 1, 0, true) == 11);
    CHECK(VehicleTargetLiveCount(def, 2, 0, false) == 9);
    CHECK(VehicleTargetLiveCount(def, 2, 0, true) == 14);
    CHECK(VehicleTargetLiveCount(def, 3, 0, false) == 9);
    CHECK(PedTargetLiveCount(def, 0, 0, 1.0f) == 16);
    CHECK(PedTargetLiveCount(def, 1, 0, 1.0f) == 24);
    CHECK(PedTargetLiveCount(def, 2, 0, 1.0f) == 32);
    CHECK(PedTargetLiveCount(def, 3, 0, 1.0f) == 32);
    const SpawnCategoryDensity yw{0.8f, 0.6f, 0.8f, 0.4f};
    CHECK(VehicleTargetLiveCount(yw, 0, 0, false) == 7);
    CHECK(VehicleTargetLiveCount(yw, 1, 0, false) == 8);
    CHECK(VehicleTargetLiveCount(yw, 2, 0, false) == 9);
    CHECK(VehicleTargetLiveCount(yw, 0, 0, true) == 10);
    CHECK(VehicleTargetLiveCount(yw, 1, 0, true) == 12);
    CHECK(VehicleTargetLiveCount(yw, 2, 0, true) == 14);
    const SpawnCategoryDensity cut{1.0f, 0.5f, 1.0f, 0.3f};
    CHECK(VehicleTargetLiveCount(cut, 2, 0, false) == 12);
    CHECK(VehicleTargetLiveCount(cut, 2, 0, true) == 18);
    CHECK(PedTargetLiveCount(cut, 2, 0, 1.0f) == 40);
    const SpawnCategoryDensity ro{0.5f, 0.3f, 0.5f, 0.3f};
    CHECK(VehicleTargetLiveCount(ro, 0, 0, false) == 3);
    CHECK(VehicleTargetLiveCount(ro, 0, 0, true) == 5);
    CHECK(PedTargetLiveCount(ro, 0, 0, 1.0f) == 12);
    // notoriety and weather
    CHECK(VehicleTargetLiveCount(def, 2, 3, false) == 4);
    CHECK(PedTargetLiveCount(def, 2, 3, 1.0f) == 16);
    CHECK(VehicleTargetLiveCount(def, 2, 5, false) == 1);
    CHECK(PedTargetLiveCount(def, 2, 5, 1.0f) == 3);
    CHECK(PedTargetLiveCount(def, 2, 0, 0.1f) == 3);  // Heavy Rain

    // --- moving cone ---
    CHECK(MovingConeMinDot(0.0) == -1.0);
    CHECK(MovingConeMinDot(2.2352) == -1.0);
    CHECK(std::fabs(MovingConeMinDot(5.0) - 0.1864) < 1e-3);
    CHECK(std::fabs(MovingConeMinDot(10.0) - 0.3728) < 1e-3);
    CHECK(std::fabs(MovingConeMinDot(13.4112) - 0.5) < 1e-9);
    CHECK(std::fabs(MovingConeMinDot(40.0) - 0.5) < 1e-9);

    // --- ring crossing: chord from (0,0) to (200,0) around centre (0,0) R=130 ---
    {
        Vec3 out;
        CHECK(ChordCircleCrossing(Vec3{0, 0, 0}, Vec3{200, 0, 0}, Vec3{0, 0, 0}, 130.0, out));
        CHECK(std::fabs(out.x - 130.0) < 1e-6);
        CHECK(!ChordCircleCrossing(Vec3{0, 0, 0}, Vec3{100, 0, 0}, Vec3{0, 0, 0}, 130.0, out));
        CHECK(!ChordCircleCrossing(Vec3{140, 0, 0}, Vec3{200, 0, 0}, Vec3{0, 0, 0}, 130.0, out));
    }

    // --- vehicle despawn distance ---
    {
        VehicleDespawnContext c;
        CHECK(VehicleDespawnDistance(c) == 140.0);
        c.playerOnFoot = false;
        CHECK(VehicleDespawnDistance(c) == 200.0);  // 140 + 60
        c = VehicleDespawnContext{};
        c.visible = false; c.cameraMoving = true; c.behindCamera = true;
        CHECK(VehicleDespawnDistance(c) == 65.0);
        c.playerOnFoot = false;
        CHECK(VehicleDespawnDistance(c) == 125.0);  // (65 + 60)
        c = VehicleDespawnContext{};
        c.specialClass = true;
        CHECK(VehicleDespawnDistance(c) == 250.0);
        c = VehicleDespawnContext{};
        c.visible = true; c.cameraMoving = true; c.behindCamera = true;
        CHECK(VehicleDespawnDistance(c) == 140.0);  // visible: at least 140
        c = VehicleDespawnContext{};
        c.distanceScale = 2.0;
        CHECK(VehicleDespawnDistance(c) == 280.0);
        CHECK(ShouldDespawnVehicleByDistance(VehicleDespawnContext{}, 141.0));
        CHECK(!ShouldDespawnVehicleByDistance(VehicleDespawnContext{}, 139.0));
    }

    // --- ped spawn rejects (Sec20.6.2(c)) ---
    {
        PedSpawnPointFacts f;
        f.distToNearestViewer = 50.0; f.visible = false; f.pedsWithin20m = 0;
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::None);
        f.distToNearestViewer = 91.0;
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::TooFar);
        f.distToNearestViewer = 9.0;
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::TooClose);
        f.distToNearestViewer = 69.0; f.visible = true;
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::VisibleNear);
        f.distToNearestViewer = 80.0;  // visible but >= 70: accepted
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::None);
        f.visible = false; f.distToNearestViewer = 30.0; f.pedsWithin20m = 7;
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::Crowded);
        f.pedsWithin20m = 6;
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::None);
        // bulk path skips near + visibility
        f.bulkPath = true; f.distToNearestViewer = 5.0; f.visible = true;
        CHECK(EvaluatePedSpawnReject(f) == PedSpawnReject::None);
    }

    // --- ped despawn ---
    CHECK(ShouldDespawnPedByDistance(90.1));
    CHECK(!ShouldDespawnPedByDistance(89.9));

    // --- bulk fill ---
    CHECK(PedBulkFillCount(32, 0, 150) == 10);  // trunc(0.33*32)
    CHECK(PedBulkFillCount(32, 32, 150) == 0);

    // --- lateral offset ---
    CHECK(std::fabs(PedLateralOffsetFromRand(0.0, 3.0) - (-1.5)) < 1e-9);
    CHECK(std::fabs(PedLateralOffsetFromRand(1.0, 3.0) - 1.5) < 1e-9);

    std::printf(g_fail ? "freeroam_population_test: %d FAILED\n" : "freeroam_population_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
