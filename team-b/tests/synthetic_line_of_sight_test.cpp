// Synthetic tests for sr3airuntime/line_of_sight.h (2026-10-08 castRay/
// shape-cast follow-up task) - see that header's own doc comment for full
// sourcing (spec-ai-runtime.md Sec12.4, spec-physics-runtime.md
// Sec8.8-Sec8.11). No real game data needed - every triangle here is a
// hand-built fixture, matching this project's "synthetic" test convention
// (tests/synthetic_navpoint_selector_test.cpp's own split: byte-level real
// data belongs to a different, "real_*"-named test binary).
//
// Covers: the headline case the task brief itself asks for (a wall between
// two points = blocked, a clear line = unblocked), plus the closest-hit
// reduction (nearer of two walls wins, not the first one scanned), a ray
// that passes through a triangle's plane but outside its footprint (must
// NOT report a hit), the ILineOfSightWorld/NoHitLineOfSightWorld seam
// itself, and IsLineOfSightBlocked()'s own boolean framing.
#include <cmath>
#include <iostream>
#include <vector>

#include "sr3airuntime/line_of_sight.h"

using namespace sr3airuntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool deq(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }
bool veq(Vec3 a, Vec3 b, double tol = 1e-6) { return deq(a.x, b.x, tol) && deq(a.y, b.y, tol) && deq(a.z, b.z, tol); }

// A single axis-aligned wall in the XZ plane at x=5, spanning z in
// [-5,5], y in [0,3] - built from two triangles (a quad split on the
// diagonal), the same "flat span" shape SweepCircleVsSegmentXZ's own test
// fixtures already use elsewhere in this project, just lifted to a real 3D
// quad instead of a 2D segment.
std::vector<LosTriangle> OneWallAtX5() {
    const Vec3 p00{5, 0, -5}, p01{5, 0, 5}, p10{5, 3, -5}, p11{5, 3, 5};
    return {LosTriangle{p00, p01, p11}, LosTriangle{p00, p11, p10}};
}

// ===================================================================
// Part A: the task's own headline case - wall blocks, clear line doesn't.
// ===================================================================

void testWallBetweenTwoPointsBlocksLineOfSight() {
    const std::vector<LosTriangle> wall = OneWallAtX5();
    LosRayHit hit;
    // Straight through the wall's own middle, perpendicular to it.
    const bool blocked = ClosestHitRayVsTriangles(Vec3{0, 1.5, 0}, Vec3{10, 1.5, 0}, wall, &hit);
    CHECK(blocked);
    CHECK(deq(hit.position.x, 5.0));
    CHECK(deq(hit.position.y, 1.5));
    CHECK(deq(hit.position.z, 0.0));
    // The wall faces -x (toward the ray's own origin side, x=0).
    CHECK(hit.normal.x < 0.0);
}

void testClearLineOfSightIsUnblocked() {
    const std::vector<LosTriangle> wall = OneWallAtX5();
    LosRayHit hit;
    // Same two endpoints' Y/Z shape but entirely on the near side of the
    // wall (never reaches x=5) - must report clear.
    const bool blocked = ClosestHitRayVsTriangles(Vec3{0, 1.5, 0}, Vec3{4, 1.5, 0}, wall, &hit);
    CHECK(!blocked);
}

void testLineOfSightAbovetheWallIsUnblocked() {
    const std::vector<LosTriangle> wall = OneWallAtX5();
    // The wall only spans y in [0,3] - a line well above it must pass clear
    // even though it crosses the same x=5 plane and the same z=0 span.
    const bool blocked = ClosestHitRayVsTriangles(Vec3{0, 10, 0}, Vec3{10, 10, 0}, wall, nullptr);
    CHECK(!blocked);
}

void testLineOfSightBesideTheWallIsUnblocked() {
    const std::vector<LosTriangle> wall = OneWallAtX5();
    // The wall only spans z in [-5,5] - a line that crosses x=5 well
    // outside that span (the triangle's own plane, but outside its
    // footprint) must pass clear, not report a false hit off the
    // infinite-plane extension.
    const bool blocked = ClosestHitRayVsTriangles(Vec3{0, 1.5, 50}, Vec3{10, 1.5, 50}, wall, nullptr);
    CHECK(!blocked);
}

// ===================================================================
// Part B: closest-hit reduction - the nearer of two obstructions wins.
// ===================================================================

void testClosestHitPicksNearerWallNotFirstScanned() {
    // A far wall at x=5 listed FIRST, a near wall at x=2 listed SECOND -
    // the closest-hit collector (Sec8.9's own kind==0 branch) must still
    // pick the nearer one regardless of scan order.
    std::vector<LosTriangle> both = OneWallAtX5();
    const Vec3 q00{2, 0, -5}, q01{2, 0, 5}, q10{2, 3, -5}, q11{2, 3, 5};
    both.push_back(LosTriangle{q00, q01, q11});
    both.push_back(LosTriangle{q00, q11, q10});

    LosRayHit hit;
    CHECK(ClosestHitRayVsTriangles(Vec3{0, 1.5, 0}, Vec3{10, 1.5, 0}, both, &hit));
    CHECK(deq(hit.position.x, 2.0));  // the NEAR wall, not the far one listed first
}

// ===================================================================
// Part C: the ILineOfSightWorld seam and IsLineOfSightBlocked() framing.
// ===================================================================

void testNoHitWorldIsAlwaysClear() {
    NoHitLineOfSightWorld world;
    CHECK(!IsLineOfSightBlocked(world, Vec3{0, 0, 0}, Vec3{1000, 1000, 1000}));
}

void testRealTriangleSoupWorldMatchesThePureFunction() {
    RealTriangleSoupLineOfSightWorld world(OneWallAtX5());
    CHECK(IsLineOfSightBlocked(world, Vec3{0, 1.5, 0}, Vec3{10, 1.5, 0}));
    CHECK(!IsLineOfSightBlocked(world, Vec3{0, 1.5, 0}, Vec3{4, 1.5, 0}));
}

void testCastRayFillsOutHitOnlyWhenRequested() {
    RealTriangleSoupLineOfSightWorld world(OneWallAtX5());
    // outHit == nullptr must not crash and must still report the correct
    // boolean (IsLineOfSightBlocked()'s own usage pattern).
    CHECK(world.castRay(Vec3{0, 1.5, 0}, Vec3{10, 1.5, 0}, nullptr));
    LosRayHit hit{};
    CHECK(world.castRay(Vec3{0, 1.5, 0}, Vec3{10, 1.5, 0}, &hit));
    CHECK(veq(hit.position, Vec3{5, 1.5, 0}));
}

}  // namespace

int main() {
    testWallBetweenTwoPointsBlocksLineOfSight();
    testClearLineOfSightIsUnblocked();
    testLineOfSightAbovetheWallIsUnblocked();
    testLineOfSightBesideTheWallIsUnblocked();
    testClosestHitPicksNearerWallNotFirstScanned();
    testNoHitWorldIsAlwaysClear();
    testRealTriangleSoupWorldMatchesThePureFunction();
    testCastRayFillsOutHitOnlyWhenRequested();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_line_of_sight_test: all checks passed\n";
    return 0;
}
