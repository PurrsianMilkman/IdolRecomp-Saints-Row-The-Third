// Team D differential vectors, combat and health (spec-differential-results.md D-9..D-13, spec-differential-vectors-combat.md).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "diff_vectors.h"
#include "sr3player/vitals.h"
#include "sr3weapons/damage_model.h"

static int g_fail = 0;
static void report(const char* area, int total, int fail) {
    std::printf("%s: %d vectors, %d failures\n", area, total, fail);
    g_fail += fail;
    if (total == 0) ++g_fail;
}

static void c1() {  // D-9 shotgun pellet factor with the random draw
    int total = 0, fail = 0;
    for (const auto& r : dv::rows("spec-differential-vectors-combat.md", "## C1.")) {
        double dist, range, shots, u, exp;
        if (r.size() < 5 || !dv::num(r[0], dist) || !dv::num(r[1], range) || !dv::num(r[2], shots) || !dv::num(r[3], u) || !dv::num(r[4], exp)) continue;
        ++total;
        const float got = sr3weapons::ShotgunPelletFactor(float(dist), float(range), int(shots), uint32_t(u));
        if (got != float(exp) && ++fail <= 5) std::printf("FAIL C1 dist %g range %g shots %g u %.0f: got %g exp %g\n", dist, range, shots, u, got, exp);
    }
    report("C1 pellet", total, fail);
}

static void c2() {  // D-10 base damage
    int total = 0, fail = 0;
    for (const auto& r : dv::rows("spec-differential-vectors-combat.md", "## C2.")) {
        float mx, mn, maxD, minD, d2, fire, riotMult, exp;
        double riot, col;
        if (r.size() < 10 || !dv::num(r[0], col) || !dv::numf(r[1], mx) || !dv::numf(r[2], mn) || !dv::numf(r[3], maxD) || !dv::numf(r[4], minD) ||
            !dv::numf(r[5], d2) || !dv::numf(r[6], fire) || !dv::num(r[7], riot) || !dv::numf(r[8], riotMult) || !dv::numf(r[9], exp))
            continue;
        ++total;
        float d = sr3weapons::BaseDamageRaw(mx, mn, maxD, minD, d2);
        d *= fire;
        if (riot != 0.0) d *= riotMult;
        if (d != exp && ++fail <= 5) std::printf("FAIL C2 max %g min %g md %g nd %g d2 %.9g: got %.9g exp %.9g\n", mx, mn, maxD, minD, d2, d, exp);
    }
    report("C2 base damage", total, fail);
}

static void c3() {  // D-11 damage assist
    int total = 0, fail = 0;
    for (const auto& r : dv::rows("spec-differential-vectors-combat.md", "## C3.")) {
        float dmg, cur, mx, start, scale, exp;
        if (r.size() < 6 || !dv::numf(r[0], dmg) || !dv::numf(r[1], cur) || !dv::numf(r[2], mx) || !dv::numf(r[3], start) || !dv::numf(r[4], scale) || !dv::numf(r[5], exp)) continue;
        ++total;
        const float got = sr3weapons::DamageAssist(dmg, cur, mx, start, scale);
        if (got != exp && ++fail <= 5) std::printf("FAIL C3 dmg %g cur %g max %g: got %.9g exp %.9g\n", dmg, cur, mx, got, exp);
    }
    report("C3 damage assist", total, fail);
}

static void c4() {  // D-12 regen wait
    int total = 0, fail = 0;
    for (const auto& r : dv::rows("spec-differential-vectors-combat.md", "## C4.")) {
        double red, mult, shield, exp;
        if (r.size() < 4 || !dv::num(r[0], red) || !dv::num(r[1], mult) || !dv::num(r[2], shield) || !dv::num(r[3], exp)) continue;
        ++total;
        float m;
        dv::numf(r[1], m);
        const int64_t got = sr3player::RegenWaitMs(red, m, shield != 0.0, false);
        if (got != int64_t(exp) && ++fail <= 5) std::printf("FAIL C4 red %g mult %g: got %lld exp %.0f\n", red, mult, (long long)got, exp);
    }
    report("C4 regen wait", total, fail);
}

static void c5() {  // D-13 regen ticks
    int total = 0, fail = 0;
    for (const auto& r : dv::rows("spec-differential-vectors-combat.md", "## C5.")) {
        double hp0, mx, shield;
        float speed, unlock;
        if (r.size() < 6 || !dv::num(r[0], hp0) || !dv::num(r[1], mx) || !dv::numf(r[2], speed) || !dv::numf(r[3], unlock) || !dv::num(r[4], shield)) continue;
        int expTicks = 0;
        double expFirst = 0;
        if (std::sscanf(r[5].c_str(), "ticks=%d first=%lf", &expTicks, &expFirst) != 2) continue;
        ++total;
        const double rate = sr3player::RegenRatePerSec(speed, unlock, shield != 0.0);
        sr3player::HealthRegen acc;
        double hp = hp0, first = 0;
        int ticks = 0;
        while (hp < mx && ticks < 100000) {
            hp = acc.step(hp, mx, true, rate, 1.0 / 30.0);
            if (++ticks == 1) first = hp;
        }
        if ((ticks != expTicks || std::fabs(first - expFirst) > 6e-4) && ++fail <= 5)
            std::printf("FAIL C5 hp %g max %g speed %g unlock %g shield %g: ticks %d (exp %d) first %.4f (exp %.3f)\n", hp0, mx, speed, unlock, shield, ticks, expTicks, first, expFirst);
    }
    report("C5 regen ticks", total, fail);
}

int main() {
    c1(); c2(); c3(); c4(); c5();
    return g_fail ? 1 : 0;
}
