// Unit test for include/sr3player/jump_crouch.h (free-roam T2): gate (Sec3.6.4), launch (Sec5.6.5), and the flight
// through the real CharacterProxyDriver (gravity + landing on the ground chain value). Crouch stance logic (Sec3.6.3)
// is the pre-existing PlayerMovement::crouchRoutine, already covered by synthetic_player_movement_test (hold/toggle/
// crouch speed); the viewer wiring of it is exercised by the headless jump-crouch-trace run.
#include <cmath>
#include <cstdio>

#include "sr3player/character_collision.h"
#include "sr3player/jump_crouch.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("CHECK FAILED: %s (line %d)\n", #c, __LINE__); ++g_fail; } } while (0)

using namespace sr3player;

static void testLaunchFormula() {
    CHECK(std::fabs(JumpLaunchSpeed(9.8, 1.0) - std::sqrt(19.6)) < 1e-12);
    CHECK(std::fabs(JumpLaunchSpeed(-9.8, 2.0) - std::sqrt(39.2)) < 1e-12);  // |g|, |h|
    CHECK(JumpLaunchSpeed(9.8, 0.0) == 0.0);
    CHECK(JumpLaunchSpeed(9.8, -1.0) == 0.0);  // gated on heightArg > 0
}

static void testGate() {
    JumpGateInputs g;
    g.jumpPressed = true;
    CHECK(JumpGate(g));
    { auto h = g; h.jumpPressed = false; CHECK(!JumpGate(h)); }
    { auto h = g; h.permitted = false; CHECK(!JumpGate(h)); }
    { auto h = g; h.meleeStarted = true; CHECK(!JumpGate(h)); }
    { auto h = g; h.controlsDisabled = true; CHECK(!JumpGate(h)); }
    { auto h = g; h.airborne = true; CHECK(!JumpGate(h)); }
    { auto h = g; h.blocked = true; CHECK(!JumpGate(h)); }
    { auto h = g; h.heightAboveGround = 0.09; CHECK(JumpGate(h)); }
    { auto h = g; h.heightAboveGround = 1.5; CHECK(!JumpGate(h)); }  // unsupported: no (double) jump
}

static void flight(double dt, double groundY, double height, double* peak, double* airMs, int* frames, double* finalY,
                   double* minAboveGround) {
    EmptyCollisionWorld world;
    CharacterProxyDriver drv(world, 0.4, 1.8, groundY);
    JumpController jc(height);
    Vec3 pos{0, groundY, 0};
    for (int f = 0; f < 5; ++f) {  // grounded frames, no jump
        const double vy = jc.beginFrame(JumpGateInputs{}, pos.y);
        const CapsuleStepResult r = drv.step(pos, Vec3{0, vy, 0}, dt);
        jc.endFrame(r.position.y, r.velocity.y, r.grounded, dt);
        pos = r.position;
        CHECK(std::fabs(pos.y - groundY) < 1e-12);
        CHECK(!jc.airborne());
    }
    jc.requestJump();
    int n = 0;
    *minAboveGround = 0;
    do {
        bool launched = false;
        const double vy = jc.beginFrame(JumpGateInputs{}, pos.y, &launched);
        CHECK(launched == (n == 0));
        const CapsuleStepResult r = drv.step(pos, Vec3{0, vy, 0}, dt);
        jc.endFrame(r.position.y, r.velocity.y, r.grounded, dt);
        pos = r.position;
        *minAboveGround = std::fmin(*minAboveGround, pos.y - groundY);
        if (n == 3) jc.requestJump();  // a second press mid-air must not relaunch
        ++n;
    } while (jc.airborne() && n < 1000);
    *frames = n;
    *peak = jc.stats().lastPeakAboveTakeoff;
    *airMs = jc.stats().lastAirtimeMs;
    *finalY = pos.y;
    CHECK(jc.stats().jumps == 1 && jc.stats().landings == 1);
}

// Regression (2026-10-18 spawn "drift"): the ground target steps down 2.27 m (32.15 -> 29.88, a road-node step while
// walking). Without carrying the fall velocity across frames the player sank ~|g|*dt^2 = 0.011 m/frame (200+ frames);
// with it, gravity accumulates and the drop takes ~0.7 s, ending exactly on the new ground.
static void testStepDownFallsUnderGravity() {
    const double dt = 1.0 / 30.0;
    EmptyCollisionWorld world;
    CharacterProxyDriver drv(world, 0.4, 1.8, 32.15);
    JumpController jc;
    Vec3 pos{0, 32.15, 0};
    drv.setGroundY(29.88);
    int n = 0;
    double firstStep = 0.0;
    while (pos.y > 29.88 + 1e-9 && n < 400) {
        const double vy = jc.beginFrame(JumpGateInputs{}, pos.y);
        const CapsuleStepResult r = drv.step(pos, Vec3{0, vy, 0}, dt);
        jc.endFrame(r.position.y, r.velocity.y, r.grounded, dt);
        if (n == 0) firstStep = pos.y - r.position.y;
        pos = r.position;
        ++n;
    }
    CHECK(std::fabs(firstStep - 9.8 * dt * dt) < 1e-9);  // first frame is still the small step
    CHECK(n <= 30);                                       // ~0.7 s analytic, not a 0.011 m/frame crawl
    CHECK(std::fabs(pos.y - 29.88) < 1e-12);
    // Grounded afterwards: no residual velocity.
    CHECK(jc.beginFrame(JumpGateInputs{}, pos.y) == 0.0);
    std::printf("  step-down 2.27 m settled in %d frames\n", n);
}

int main() {
    testLaunchFormula();
    testGate();
    testStepDownFallsUnderGravity();
    for (double dt : {1.0 / 30.0, 1.0 / 60.0}) {
        for (double groundY : {0.0, 32.15}) {
            double peak, air, finalY, below;
            int frames;
            flight(dt, groundY, 1.0, &peak, &air, &frames, &finalY, &below);
            const double v0 = std::sqrt(2 * 9.8 * 1.0);
            CHECK(std::fabs(peak - 1.0) <= v0 * dt);  // semi-implicit Euler: within one frame's travel
            CHECK(std::fabs(air / 1000.0 - 2 * v0 / 9.8) <= 2 * dt + 1e-9);  // analytic 0.904 s
            CHECK(std::fabs(finalY - groundY) < 1e-12);  // landed on the ground value
            CHECK(below >= -1e-12);                      // never sank through it
            std::printf("  dt=%.4f ground=%.2f peak=%.4f airtime=%.0fms frames=%d\n", dt, groundY, peak, air, frames);
        }
    }
    {
        double peak, air, finalY, below;
        int frames;
        flight(1.0 / 60.0, 0.0, 2.0, &peak, &air, &frames, &finalY, &below);
        CHECK(std::fabs(peak - 2.0) <= std::sqrt(2 * 9.8 * 2.0) / 60.0);
    }
    if (g_fail) { std::printf("jump_crouch tests FAILED: %d\n", g_fail); return 1; }
    std::printf("jump_crouch tests passed\n");
    return 0;
}
