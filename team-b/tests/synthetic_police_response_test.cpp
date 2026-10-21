// Synthetic tests for include/sr3airuntime/police_response.h: the spec-ai-runtime.md Sec19.4 binary hostility rule,
// the Sec22.2 chase speed law / hand-off / siren, and the CHOSEN dispatch pacing and stand-down rules.
#include <algorithm>
#include <cmath>
#include <iostream>

#include "sr3airuntime/police_response.h"

using namespace sr3airuntime;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
bool Near(double a, double b, double e = 1e-9) { return std::fabs(a - b) <= e; }

void TestHostileIsBinary() {
    CHECK(!PoliceHostile(0));
    for (int l = 1; l <= 5; ++l) CHECK(PoliceHostile(l));
}

void TestDispatchLevels() {
    PoliceDispatcher d;
    auto r = d.Update(0, 0, 0, 0);
    CHECK(r.disengage && !r.spawnFoot && !r.spawnCar);
    r = d.Update(1000, 1, 0, 0);  // level 1: foot only, first unit at once, then paced
    CHECK(r.spawnFoot && !r.spawnCar && !r.disengage);
    r = d.Update(1100, 1, 1, 0);
    CHECK(!r.spawnFoot);  // inside the 4000 ms interval
    r = d.Update(5100, 1, 1, 0);
    CHECK(r.spawnFoot);
    r = d.Update(9200, 1, 2, 0);
    CHECK(!r.spawnFoot);  // target 2 reached
    PoliceDispatcher e;   // level 3: cars appear
    r = e.Update(0, 3, 0, 0);
    CHECK(r.spawnFoot && r.spawnCar);
    r = e.Update(10, 3, 1, 1);
    CHECK(!r.spawnCar);  // car target at level 3 is 1
    for (int l = 1; l < 5; ++l) {
        CHECK(kChosenDispatchRows[l + 1].footTarget >= kChosenDispatchRows[l].footTarget);
        CHECK(kChosenDispatchRows[l + 1].carTarget >= kChosenDispatchRows[l].carTarget);
    }
    r = e.Update(20, 0, 3, 1);  // dropping to 0 resets the pacing: a new engagement spawns at once
    CHECK(r.disengage);
    r = e.Update(30, 2, 0, 0);
    CHECK(r.spawnFoot);
}

void TestRingAndStandDown() {
    for (double u : {0.0, 0.5, 1.0}) {
        const Vec3 p = PoliceRingPoint({10, 0, 20}, 1.0, u, 75, 90);
        const double r = std::hypot(p.x - 10, p.z - 20);
        CHECK(r >= 75 - 1e-9 && r <= 90 + 1e-9);
    }
    CHECK(!PoliceShouldRemoveAfterStandDown(0, 500));
    CHECK(PoliceShouldRemoveAfterStandDown(1, 95));
    CHECK(!PoliceShouldRemoveAfterStandDown(1000, 30));
    CHECK(PoliceShouldRemoveAfterStandDown(kChosenStandDownMaxMs, 5));
}

void TestChaseSpeedLaw() {
    // Far (g >= 4): v_cmd = v_t + sqrt(1.2 * a_b * (g - s)); g = 29, s = 4, a_b = 5, v_t = 10 -> 10 + sqrt(150)
    ChaseSpeedCommand c = ChaseCommandedSpeed(0.0, 10.0, 5.0, 29.0, false);
    CHECK(Near(c.vCmd, 10.0 + std::sqrt(1.2 * 5.0 * 25.0)));
    CHECK(Near(c.decel, 0.0));
    c = ChaseCommandedSpeed(0.0, 10.0, 5.0, 29.0, true);  // flank manoeuvre doubles m
    CHECK(Near(c.vCmd, 10.0 + std::sqrt(2.0 * 1.2 * 5.0 * 25.0)));
    // Faster than the command, far: decel = min(a_b, (v^2 - vt^2) / (2 (g-s))) = 800/50 = 16 -> capped at a_b
    c = ChaseCommandedSpeed(30.0, 10.0, 5.0, 29.0, false);
    CHECK(Near(c.decel, 5.0));
    c = ChaseCommandedSpeed(20.0, 10.0, 5.0, 54.0, false);  // vCmd 10 + sqrt(300) > 20: no decel
    CHECK(c.vCmd > 20.0 && Near(c.decel, 0.0));
    c = ChaseCommandedSpeed(5.0, 0.0, 5.0, 4.05, false);  // under 0.1 m past the stand-off: full a_b
    CHECK(Near(c.vCmd, std::sqrt(1.2 * 5.0 * 0.05)));
    CHECK(Near(c.decel, 5.0));
    c = ChaseCommandedSpeed(0.0, 10.0, 5.0, 2.0, false);  // near (g = 2 < 4): v_t - 5 * (1 - 0.5)
    CHECK(Near(c.vCmd, 7.5));
}

void TestChaseNearDecelAndDeadBand() {
    // near, v_own above: decel = (v_own / g) * (v_own - v_cmd) = 5 * 2.5 = 12.5 (above the 2.5 dead band)
    ChaseSpeedCommand c = ChaseCommandedSpeed(10.0, 10.0, 5.0, 2.0, false);
    CHECK(Near(c.decel, 12.5));
    c = ChaseCommandedSpeed(7.6, 10.0, 5.0, 2.0, false);  // (7.6/2)*(0.1) = 0.38 < 2.5 -> dead-banded
    CHECK(Near(c.decel, 0.0));
    c = ChaseCommandedSpeed(8.0, 10.0, 5.0, -1.0, false, true);  // overlap stop (bit 6, g < 0)
    CHECK(Near(c.vCmd, 0.0));
}

void TestHandoffAndSiren() {
    CHECK(ChaseHandoff(true, true, false, 99.0));
    CHECK(!ChaseHandoff(true, true, false, 100.0));  // strictly less than 100.0
    CHECK(!ChaseHandoff(false, true, false, 1.0));
    CHECK(!ChaseHandoff(true, false, false, 1.0));
    CHECK(ChaseHandoff(true, true, true, 10000.0));  // garage/store interaction: any distance
    CHECK(ChaseSirenOn(5, false) && ChaseSirenOn(6, false) && ChaseSirenOn(7, false));
    CHECK(!ChaseSirenOn(4, false) && !ChaseSirenOn(5, true));
}

void TestChaseIntegration() {
    // A chaser starting 100 m behind a player doing 15 m/s closes to the stand-off and matches speed, never overlaps.
    double px = 100.0, pv = 15.0, cx = 0.0, cv = 0.0;
    const double dt = 1.0 / 30.0;
    double minGap = 1e9;
    for (int i = 0; i < 30 * 120; ++i) {
        px += pv * dt;
        const ChaseSpeedCommand c = ChaseCommandedSpeed(cv, pv, 6.0, px - cx, false);
        cv = ChaseIntegrateSpeed(cv, c, 6.0, dt);
        cx += cv * dt;
        minGap = std::min(minGap, px - cx);
    }
    CHECK(px - cx > 1.0);
    CHECK(std::fabs(cv - pv) < 1.0);
    CHECK(std::fabs((px - cx) - kChaseStandoffM) < 2.5);
    CHECK(minGap > 0.5);
}

void TestSteer() {
    CHECK(Near(ChaseSteerHeading(0.0, 10.0, 50.0, 0.0, 0.033), 0.0));  // straight ahead: unchanged
    double h = 0.0;
    for (int i = 0; i < 200; ++i) h = ChaseSteerHeading(h, 8.0, 0.0, 50.0, 0.033);  // target on +z: want pi/2
    CHECK(std::fabs(h - 1.5707963267948966) < 1e-6);
    const double h2 = ChaseSteerHeading(3.1, 8.0, std::cos(-3.0), std::sin(-3.0), 0.033);  // short way round
    CHECK(h2 > 3.1);
    CHECK(Near(ChaseSteerHeading(0.3, 0.0, 0.0, 50.0, 0.033), 0.3));  // no movement without speed
}
}  // namespace

int main() {
    TestHostileIsBinary();
    TestDispatchLevels();
    TestRingAndStandDown();
    TestChaseSpeedLaw();
    TestChaseNearDecelAndDeadBand();
    TestHandoffAndSiren();
    TestChaseIntegration();
    TestSteer();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "police_response tests passed\n";
    return 0;
}
