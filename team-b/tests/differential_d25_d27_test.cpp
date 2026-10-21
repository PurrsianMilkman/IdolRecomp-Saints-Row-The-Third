// Team D differential vectors D-25 (suspension force), D-26 (reload timer), D-27 (downed get-up heal, weapon band).
// Vectors are the tables in spec-differential-results.md (original values). Team D numbers are printed to 4-6 digits.
#include <cmath>
#include <cstdio>
#include <string>

#include "sr3airuntime/combat_ai.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3weapons/weapon_inventory.h"

static int g_fail = 0, g_total = 0;
#define CHECK(c, ...)                                            \
    do {                                                         \
        ++g_total;                                               \
        if (!(c)) { ++g_fail; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } \
    } while (0)

static void d25() {
    struct R { double M, L, S, dc, dr, k, C, v, exp; };
    const R rows[] = {
        {1, 0.416666, 45, 3.88874, 1.92427, 0.594123, 0.428187, 1, -2.2323},
        {1, 0.224375, 23.187, 1.93013, 4, 1, 0.579344, -2.30095, -3.7895},
        {1, 0.4, 30, 3.5, 0.948351, 0.566586, 0.0464535, -1.70752, 11.9858},
        {1500, 0.633398, 46.0377, 2, 2.5, 1, 0.155242, -1, 36019.7734},
        {1500, 0.4, 45, 2, 7.96128, 1, 0.185009, -1, 17511.9121},
        {800, 0.3, 30, 2, 4, 1, 0.782144, 0, -11571.4473},
        {800, 0.3, 69.9246, 2.05048, 1.64099, 1.45409, 0.33839, -1, -1482.2975},
    };
    for (const R& r : rows) {
        sr3physics::SuspensionWheelTuning t;
        t.restLength = r.L; t.spring = r.S; t.damperCompression = r.dc; t.damperRelaxation = r.dr;
        t.loadFactor = r.k; t.chassisMass = r.M;
        const double got = sr3physics::SuspensionSpringDamperForceNoXDenominator(t, r.C, r.v);
        CHECK(std::fabs(got - r.exp) <= 2e-4 * std::fabs(r.exp) + 1e-3, "D-25 M %g v %g: got %.4f exp %.4f", r.M, r.v, got, r.exp);
    }
    sr3physics::SuspensionWheelTuning t;
    CHECK(sr3physics::SuspensionSpringDamperForceNoXDenominator(t, 0.1, 1.0, false) == 0.0, "D-25 airborne wheel must be 0");
}

static sr3tables_weapons::Weapon W(const char* group, float ov) {
    sr3tables_weapons::Weapon w;
    w.name = "x";
    w.animationGroup = std::string(group);
    w.magazineSize.present = true; w.magazineSize.value = 10;
    w.invSlot = 1;
    if (ov > 0.0f) w.reloadOverrideTimeSec = ov;
    return w;
}

static void d26() {
    struct R { const char* grp; float ov; double scalar; double exp; };
    const R rows[] = {  // 44 f pistol, 71 f rifle, 96 f shotgun, 79 f sniper
        {"Shotgun", 0, 1.25, 2559}, {"Pistol", 0.1f, 1.25, 79}, {"Pistol", 3.5f, 1.25, 2799}, {"Rifle", 0.7f, 1.75, 400},
        {"Shotgun", 1.6f, 1.25, 1279}, {"Shotgun", 4.9f, 1.75, 2799}, {"Sniper", 0.7f, 1.25, 560}, {"Sniper", 3.3f, 1.5, 2200},
        {"Pistol", 0, 1.0, 1466}, {"Pistol", 0, 1.75, 838},
    };
    for (const R& r : rows) {
        const auto p = sr3weapons::PlanReload(W(r.grp, r.ov), r.scalar);
        CHECK(p.ms == r.exp, "D-26 %s ov %g s %g: got %g exp %g", r.grp, double(r.ov), r.scalar, p.ms, r.exp);
    }
}

static void d27() {
    struct R { int hp; bool enemy; float exp; };
    const R rows[] = {{1, true, 0.25f}, {3, true, 0.75f}, {7, true, 1.75f}, {99, true, 24.75f}, {101, true, 25.25f},
                      {150, true, 37.5f}, {333, true, 83.25f}, {1000, true, 250.f}, {150, false, 150.f},
                      {16777217, false, 16777216.f}};
    for (const R& r : rows) {
        const float got = sr3airuntime::combat::DownedGetUpHeal(r.hp, r.enemy);
        CHECK(got == r.exp, "D-27 hp %d enemy %d: got %.6f exp %.6f", r.hp, int(r.enemy), double(got), double(r.exp));
    }
    // weapon band: too-close test is d2 < float32(a*a); out fraction is float32 0.1 widened.
    using namespace sr3airuntime::combat;
    const double a = 5.3, b = 20.0;
    const float a2 = static_cast<float>(a * a);
    CHECK(WeaponBand(static_cast<double>(a2), a, b) == 0, "D-27 band edge d2 == float32(a*a) is inside");
    CHECK(WeaponBand(static_cast<double>(std::nextafter(a2, 0.0f)), a, b) == -1, "D-27 band edge just below is too close");
    const double out = WeaponBandOutDistance(-1, a, b);
    CHECK(out == static_cast<double>(static_cast<float>(a + (b - a) * 0.10000000149011612)), "D-27 out distance float32");
}

int main() {
    d25(); d26(); d27();
    std::printf("D-25/26/27: %d vectors, %d failures\n", g_total, g_fail);
    return g_fail ? 1 : 0;
}
