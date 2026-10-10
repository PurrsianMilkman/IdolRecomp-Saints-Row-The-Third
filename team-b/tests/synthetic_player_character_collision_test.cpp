// Synthetic tests for sr3player/character_collision.h (2026-10-07 character-
// proxy/collision follow-up task, extended 2026-10-08 for the real-
// `.clmesh_pc`-triangle-collision follow-up) - see that header's own doc
// comment for full sourcing. These exercise:
//   * SweepCircleVsAabbXZ(): the pure swept-circle-vs-box geometry test the
//     former AABB-stand-in ICollisionWorld (tools/sr3_viewer.cpp's walk
//     demo) was built from;
//   * SweepCircleVsSegmentXZ()/SweepCircleVsTriangleXZ() (2026-10-08): the
//     pure swept-circle-vs-real-collision-triangle geometry tests
//     ClmeshTriangleCollisionWorld (tools/sr3_viewer.cpp) is built from -
//     flat-span contact, round end-cap contact, already-inside, miss, and a
//     real wedge-shaped triangle (head-on face + apex-vertex cases);
//   * CharacterProxyDriver::step(): drives the REAL
//     sr3physics::CharacterProxyStepper lock-step loop (CONFIRMED cap 10,
//     character_proxy.h) and CharacterProxyListener queue-only contract
//     against synthetic ICollisionWorld test doubles - open space, a single
//     head-on wall, and a two-wall corner (exercising > 1 pass);
//   * the CONFIRMED gravity constant and the grounded/falling binary;
//   * PlayerMovement::collisionHook's own wiring in src/player_locomotion.cpp
//     - unset (nullptr) must reproduce the EXACT prior direct-integrator
//       behaviour (byte for byte - this is the compatibility guarantee the
//       walk_demo v1 golden depends on), set must route through it.
#include <cmath>
#include <iostream>
#include <vector>

#include "sr3player/character_collision.h"
#include "sr3player/movement.h"

using namespace sr3player;

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

// ===========================================================================
// SweepCircleVsAabbXZ
// ===========================================================================

void testSweepCircleOpenSpace() {
    double f;
    Vec3 n;
    // A box far away in +x; a sweep straight along +z never comes near it.
    CHECK(!SweepCircleVsAabbXZ(0, 0, 0, 10, 0.5, 100, 110, 100, 110, f, n));
}

void testSweepCircleHeadOnWall() {
    // A 2x2 box centred at (10,0); approaching along +x with radius 0.5
    // should contact at x = 9 - 0.5 = 8.5 (the box's near face, expanded by
    // the radius), i.e. fraction 8.5/20 along a 0->20 sweep.
    double f;
    Vec3 n;
    CHECK(SweepCircleVsAabbXZ(0, 0, 20, 0, 0.5, 9, 11, -1, 1, f, n));
    CHECK(deq(f, 8.5 / 20.0, 1e-9));
    CHECK(veq(n, {-1, 0, 0}));
}

void testSweepCircleMissesToTheSide() {
    // The same box, but the sweep passes well to the side in z - no contact.
    double f;
    Vec3 n;
    CHECK(!SweepCircleVsAabbXZ(0, 10, 20, 10, 0.5, 9, 11, -1, 1, f, n));
}

void testSweepCircleAlreadyInside() {
    // Starting already inside the expanded box: CHOSEN "no new contact"
    // (see the function's own doc comment).
    double f;
    Vec3 n;
    CHECK(!SweepCircleVsAabbXZ(10, 0, 12, 0, 0.5, 9, 11, -1, 1, f, n));
}

// ===========================================================================
// SweepCircleVsSegmentXZ / SweepCircleVsTriangleXZ (2026-10-08 real-
// `.clmesh_pc`-collision follow-up task) - the replacement for the AABB
// stand-in above.
// ===========================================================================

void testSweepSegmentOpenSpace() {
    double f;
    Vec3 n;
    // A segment far away in +x; a sweep straight along +z never comes near.
    CHECK(!SweepCircleVsSegmentXZ(0, 0, 0, 10, 0.5, 100, -5, 100, 5, f, n));
}

void testSweepSegmentFlatSpanHeadOn() {
    // A long wall segment along z (x=9, z in [-50,50]); approaching along +x
    // with radius 0.5 should contact at x = 9 - 0.5 = 8.5, matching the AABB
    // near-face case exactly (same fraction/normal), since a long flat span
    // IS the box's near face.
    double f;
    Vec3 n;
    CHECK(SweepCircleVsSegmentXZ(0, 0, 20, 0, 0.5, 9, -50, 9, 50, f, n));
    CHECK(deq(f, 8.5 / 20.0, 1e-9));
    CHECK(veq(n, {-1, 0, 0}));
}

void testSweepSegmentMissesToTheSide() {
    double f;
    Vec3 n;
    CHECK(!SweepCircleVsSegmentXZ(0, 10, 20, 10, 0.5, 9, -1, 9, 1, f, n));
}

void testSweepSegmentAlreadyInside() {
    // Starting within radius of the segment's own flat span: CHOSEN "no new
    // contact", same convention as SweepCircleVsAabbXZ.
    double f;
    Vec3 n;
    CHECK(!SweepCircleVsSegmentXZ(9, 0, 9, 5, 0.5, 9, -50, 9, 50, f, n));
}

void testSweepSegmentRoundEndCap() {
    // A SHORT segment (x=9, z in [-1,1]) approached diagonally so the
    // nearest feature is the segment's own END VERTEX (9,1), not its flat
    // span: the contact should land exactly `radius` from that vertex, with
    // the normal radiating away from it (not axis-aligned).
    double f;
    Vec3 n;
    // Aim from (20,20) straight at the end vertex (9,1).
    CHECK(SweepCircleVsSegmentXZ(20, 20, 9, 1, 0.5, 9, -1, 9, 1, f, n));
    const double hitX = 20 + (9 - 20) * f;
    const double hitZ = 20 + (1 - 20) * f;
    const double distToVertex = std::sqrt((hitX - 9) * (hitX - 9) + (hitZ - 1) * (hitZ - 1));
    CHECK(deq(distToVertex, 0.5, 1e-6));
    // Normal must radiate away from the vertex, i.e. point back the way the
    // sweep came from (same direction as (hitX-9,hitZ-1), normalized).
    CHECK(deq(n.x, (hitX - 9) / 0.5, 1e-6));
    CHECK(deq(n.z, (hitZ - 1) / 0.5, 1e-6));
}

void testSweepSegmentParallelApproachNoNewContact() {
    // Moving exactly parallel to the segment's own line, already within
    // radius the whole time: no NEW contact (c1 == 0 branch).
    double f;
    Vec3 n;
    CHECK(!SweepCircleVsSegmentXZ(9.3, -10, 9.3, 10, 0.5, 9, -50, 9, 50, f, n));
}

// A right-triangle wall, vertices (10,-2)/(10,2)/(14,0) in XZ (a wedge with
// its flat face toward -x). Matches the real shape a `.clmesh_pc` collision
// triangle decodes to (ClmeshTriangleCollisionWorld).
void testSweepTriangleHeadOnFace() {
    double f;
    Vec3 n;
    // Approaching the flat A-B face (x=10, z in [-2,2]) head-on along +x.
    CHECK(SweepCircleVsTriangleXZ(0, 0, 20, 0, 0.5, 10, -2, 10, 2, 14, 0, f, n));
    CHECK(deq(f, 9.5 / 20.0, 1e-9));  // contact at x = 10 - 0.5 = 9.5
    CHECK(veq(n, {-1, 0, 0}));
}

void testSweepTriangleMisses() {
    double f;
    Vec3 n;
    // Passes well to the side (z=20 the whole sweep) - never comes close.
    CHECK(!SweepCircleVsTriangleXZ(0, 20, 20, 20, 0.5, 10, -2, 10, 2, 14, 0, f, n));
}

void testSweepTriangleAlreadyInside() {
    double f;
    Vec3 n;
    // (11,0) is inside the triangle's own footprint: CHOSEN "no new contact".
    CHECK(!SweepCircleVsTriangleXZ(11, 0, 12, 0, 0.3, 10, -2, 10, 2, 14, 0, f, n));
}

void testSweepTriangleCornerVertex() {
    double f;
    Vec3 n;
    // Aimed at the far corner vertex (14,0) from well outside - the apex of
    // the wedge, where the two slanted edges' round end-caps both apply;
    // either edge's cap test should agree on the same contact.
    CHECK(SweepCircleVsTriangleXZ(30, 0, 14, 0, 0.5, 10, -2, 10, 2, 14, 0, f, n));
    const double hitX = 30 + (14 - 30) * f;
    const double hitZ = 0 + (0 - 0) * f;
    const double distToApex = std::sqrt((hitX - 14) * (hitX - 14) + hitZ * hitZ);
    CHECK(deq(distToApex, 0.5, 1e-6));
}

// ===========================================================================
// ClosestPointOnTriangle3D / RayVsTriangle3D / SweptSphereVsTriangle3D
// (2026-10-08 castRay/shape-cast follow-up task - genuinely 3D, NOT
// XZ-plane-locked like the triangle tests just above).
// ===========================================================================

// A horizontal (XZ-plane-normal-pointing-+Y) triangle, apex-up wedge shape
// irrelevant here - this one is flat on the ground, y=0, so Y is the "up
// out of the floor" axis instead of the XZ tests' Z - deliberately a
// DIFFERENT orientation from the XZ tests above, to prove these functions
// are not secretly still 2D.
void testClosestPointOnTriangleVertexRegion() {
    // P way off past vertex A=(0,0,0): closest point must be A itself.
    const Vec3 p = ClosestPointOnTriangle3D(-10, 0, -10, 0, 0, 0, 4, 0, 0, 0, 0, 4);
    CHECK(veq(p, {0, 0, 0}));
}

void testClosestPointOnTriangleInteriorRegion() {
    // P directly above the triangle's own interior: closest point is its
    // straight-down projection onto the triangle's plane (y=0 here).
    const Vec3 p = ClosestPointOnTriangle3D(1, 5, 1, 0, 0, 0, 4, 0, 0, 0, 0, 4);
    CHECK(veq(p, {1, 0, 1}));
}

void testClosestPointOnTriangleEdgeRegion() {
    // P beyond edge A-B's own midpoint, off to the side in the triangle's
    // own plane: closest point lands on that edge, not at either endpoint.
    const Vec3 p = ClosestPointOnTriangle3D(2, 0, -5, 0, 0, 0, 4, 0, 0, 0, 0, 4);
    CHECK(veq(p, {2, 0, 0}));
}

void testRayVsTriangleHeadOnHit() {
    // A vertical (XY-plane, normal +Z) triangle at z=5; a ray straight
    // along +z through its interior.
    double f;
    Vec3 n;
    CHECK(RayVsTriangle3D(0, 0, 0, 0, 0, 20, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
    CHECK(deq(f, 5.0 / 20.0, 1e-9));
    CHECK(n.z < 0.0);  // faces back toward the ray's own origin (z=0 side)
}

void testRayVsTriangleMissesOutsideFootprint() {
    // Same triangle's plane, but the ray passes outside its 2D footprint
    // (y=50, well above the triangle's own y in [-5,5]) - must not hit the
    // infinite-plane extension.
    double f;
    Vec3 n;
    CHECK(!RayVsTriangle3D(0, 50, 0, 0, 50, 20, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
}

void testRayVsTriangleStopsShortNeverReachesIt() {
    // Segment ends at z=4, the triangle is at z=5 - never gets there.
    double f;
    Vec3 n;
    CHECK(!RayVsTriangle3D(0, 0, 0, 0, 0, 4, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
}

void testRayVsTriangleParallelToPlaneMisses() {
    // Ray runs parallel to the triangle's own plane (along +x, triangle
    // normal +z) - det ~ 0, must report a clean miss, not a divide blow-up.
    double f;
    Vec3 n;
    CHECK(!RayVsTriangle3D(0, 0, 0, 20, 0, 0, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
}

void testSweptSphereHeadOnFaceHit() {
    // Same vertical triangle at z=5; a sphere of radius 0.5 swept along +z
    // should stop with its surface touching the face, i.e. centre at
    // z = 5 - 0.5 = 4.5.
    double f;
    Vec3 n;
    CHECK(SweptSphereVsTriangle3D(0, 0, 0, 0, 0, 20, 0.5, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
    const double hitZ = 0 + (20 - 0) * f;
    CHECK(deq(hitZ, 4.5, 1e-6));
    CHECK(n.z < 0.0);
}

void testSweptSphereMissesEntirely() {
    // Swept well above the triangle's own y in [-5,5] and z in [-5,5]
    // footprint (plus the radius) - never comes close.
    double f;
    Vec3 n;
    CHECK(!SweptSphereVsTriangle3D(0, 50, 0, 0, 50, 20, 0.5, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
}

void testSweptSphereAlreadyOverlappingIsNoNewContact() {
    // Centre starts within radius of the triangle's own plane/footprint at
    // t=0 (z=4.8, radius 0.5 easily reaches z=5's face) - CHOSEN "no new
    // contact", same convention as the XZ sweep tests above.
    double f;
    Vec3 n;
    CHECK(!SweptSphereVsTriangle3D(0, 0, 4.8, 0, 0, 6, 0.5, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
}

void testSweptSphereVertexContact() {
    // Aimed squarely at the triangle's own top apex (0,5,5), from well
    // outside along -y (straight down onto the point) - a pure vertex
    // contact, no edge or face involved.
    double f;
    Vec3 n;
    CHECK(SweptSphereVsTriangle3D(0, 20, 5, 0, -20, 5, 0.5, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
    const double hitY = 20 + (-20 - 20) * f;
    const double distToApex = std::sqrt((hitY - 5) * (hitY - 5));
    CHECK(deq(distToApex, 0.5, 1e-6));
}

void testSweptSphereEdgeContact() {
    // Aimed at edge A-B's own line (x=0,z=5 matches that edge's line
    // exactly - this isoceles triangle's own axis of symmetry), approaching
    // from BELOW the base (y=-20 toward y=+20) so the base edge (y=-5) is
    // reached well before the apex (y=5) could ever matter. Excludes the
    // face sub-test (D has no z-component - sweeping exactly parallel to
    // the triangle's own plane, the same "parallel" no-op shape the face
    // test's own denom check above skips) and excludes either vertex A/B
    // (the line's own perpendicular distance to each is 5, far outside
    // radius 0.5) - a pure edge contact.
    double f;
    Vec3 n;
    CHECK(SweptSphereVsTriangle3D(0, -20, 5, 0, 20, 5, 0.5, -5, -5, 5, 5, -5, 5, 0, 5, 5, f, n));
    const double hitY = -20 + (20 - (-20)) * f;
    CHECK(deq(hitY, -5.5, 1e-6));  // touches 0.5 short of y=-5, the edge's own line
}

// ===========================================================================
// CharacterProxyDriver::step() - open space, a wall, a corner, gravity
// ===========================================================================

void testDriverOpenSpaceOnePass() {
    EmptyCollisionWorld world;
    CharacterProxyDriver driver(world, /*radius=*/0.4, /*height=*/1.8, /*groundY=*/0.0);
    const CapsuleStepResult r = driver.step(Vec3{0, 0, 0}, Vec3{1, 0, 0}, 1.0);
    // Open space: the pass consumes all remaining time in ONE pass.
    CHECK(r.passesRun == 1);
    CHECK(veq(r.position, {1, 0, 0}));
    CHECK(veq(r.velocity, {1, 0, 0}));
    CHECK(r.grounded);
    CHECK(r.hits.empty());
}

// A single wall as an ICollisionWorld test double: an infinite-in-z plane
// at x = wallX, blocking any sweep that crosses it (normal = -x, i.e.
// pushes back toward where the character came from).
struct SingleWallWorld : ICollisionWorld {
    double wallX;
    explicit SingleWallWorld(double x) : wallX(x) {}
    bool sweepCylinder(const Vec3& from, const Vec3& to, double, double, double, WallContact& out) const override {
        if (from.x < wallX && to.x >= wallX) {
            const double f = (wallX - from.x) / (to.x - from.x);
            out.travelFraction = f;
            out.normal = Vec3{-1, 0, 0};
            out.point = Vec3{wallX, from.y, from.z + (to.z - from.z) * f};
            out.surfaceId = 1;
            return true;
        }
        return false;
    }
};

void testDriverStopsAtWall() {
    SingleWallWorld world(5.0);
    CharacterProxyDriver driver(world, 0.4, 1.8, 0.0);
    Vec3 pos{0, 0, 0};
    int totalHits = 0;
    bool everHit = false;
    // Walk toward the wall at 2 units/s for 10 one-second frames (20 units
    // of travel - well past the wall at x=5) - brad must stop at/before it,
    // never pass through.
    for (int i = 0; i < 10; ++i) {
        const CapsuleStepResult r = driver.step(pos, Vec3{2, 0, 0}, 1.0);
        pos = r.position;
        totalHits += static_cast<int>(r.hits.size());
        if (!r.hits.empty()) everHit = true;
        CHECK(pos.x <= 5.0 + 1e-6);  // never penetrates past the wall plane
        CHECK(r.passesRun >= 1 && r.passesRun <= sr3physics::CharacterProxyStepper::kMaxPasses);
    }
    CHECK(everHit);              // the listener's queue-only contract actually fired
    CHECK(totalHits >= 1);
    CHECK(pos.x > 4.0);          // actually got close to the wall, not stuck far away
    CHECK(pos.x <= 5.0 + 1e-6);
}

// A corner: one wall blocking +x beyond x=5, a second blocking +z beyond
// z=5. A character aimed diagonally into the corner should need MORE than
// one pass to resolve (hit the first wall, slide, then hit the second) -
// this is the concrete reason spec-player-runtime.md Sec5.6.4 step 3's pass
// loop is multi-pass at all.
struct CornerWorld : ICollisionWorld {
    bool sweepCylinder(const Vec3& from, const Vec3& to, double, double, double, WallContact& out) const override {
        double bestF = 1.0;
        bool found = false;
        if (from.x < 5.0 && to.x >= 5.0) {
            const double f = (5.0 - from.x) / (to.x - from.x);
            if (f < bestF) {
                bestF = f;
                out.normal = Vec3{-1, 0, 0};
                out.point = Vec3{5.0, from.y, from.z + (to.z - from.z) * f};
                out.surfaceId = 1;
                found = true;
            }
        }
        if (from.z < 5.0 && to.z >= 5.0) {
            const double f = (5.0 - from.z) / (to.z - from.z);
            if (f < bestF) {
                bestF = f;
                out.normal = Vec3{0, 0, -1};
                out.point = Vec3{from.x + (to.x - from.x) * f, from.y, 5.0};
                out.surfaceId = 2;
                found = true;
            }
        }
        if (found) out.travelFraction = bestF;
        return found;
    }
};

void testDriverCornerNeedsMultiplePasses() {
    CornerWorld world;
    CharacterProxyDriver driver(world, 0.3, 1.8, 0.0);
    // Aimed straight at the corner - large velocity so the FULL diagonal
    // move for this one frame would blow through both walls if only one
    // pass ever ran.
    const CapsuleStepResult r = driver.step(Vec3{0, 0, 0}, Vec3{20, 0, 20}, 1.0);
    CHECK(r.passesRun > 1);
    CHECK(r.passesRun <= sr3physics::CharacterProxyStepper::kMaxPasses);
    CHECK(r.position.x <= 5.0 + 1e-6);
    CHECK(r.position.z <= 5.0 + 1e-6);
    // Both walls' listener records should have fired (merged by surfaceId,
    // so at most one entry per wall per frame - the running-average merge
    // rule, spec-physics-runtime.md Sec2.2).
    bool sawWall1 = false, sawWall2 = false;
    for (const auto& h : r.hits) {
        if (h.surfaceId == 1) sawWall1 = true;
        if (h.surfaceId == 2) sawWall2 = true;
    }
    CHECK(sawWall1);
    CHECK(sawWall2);
}

void testDriverGravityAndGrounded() {
    EmptyCollisionWorld world;
    CharacterProxyDriver driver(world, 0.4, 1.8, /*groundY=*/0.0);
    // Starting airborne at y=10 with no vertical velocity: falls under the
    // CONFIRMED -9.8 constant (spec-physics-runtime.md Sec4.1 step 5).
    Vec3 pos{0, 10, 0};
    CapsuleStepResult r = driver.step(pos, Vec3{0, 0, 0}, 0.1);
    CHECK(!r.grounded);
    // vy after one 0.1s step: -9.8*0.1 = -0.98; y: 10 + (-0.98)*0.1 = 9.902.
    CHECK(deq(r.velocity.y, -0.98, 1e-9));
    CHECK(deq(r.position.y, 9.902, 1e-9));
    pos = r.position;
    // Run until grounded.
    bool landed = false;
    for (int i = 0; i < 200 && !landed; ++i) {
        r = driver.step(pos, Vec3{0, 0, 0}, 0.1);
        pos = r.position;
        if (r.grounded) landed = true;
    }
    CHECK(landed);
    CHECK(deq(pos.y, 0.0, 1e-6));
}

// Spec-player-runtime.md Sec5.6.5 (closes OPEN 35, 2026-10-07 unit W-U2):
// "gravity *= (1.0 - ratio*ratio)" where ratio = sinkSpeed / 44.704 (the
// CONFIRMED controller+0x170 speed cap, Sec5.6.1) - the quadratic terminal-
// velocity falloff. Recomputes the SAME formula independently here (not a
// tautology against the implementation: a transcription bug in the
// implementation - wrong sign, forgetting to square, scaling AFTER adding
// gravity*dt instead of before - would still be caught by this comparison).
void testDriverGravityTerminalVelocityFalloff() {
    EmptyCollisionWorld world;
    CharacterProxyDriver driver(world, 0.4, 1.8, /*groundY=*/-1000.0);  // far below - never grounds this step
    const double vyIn = -40.0;  // already sinking fast, close to the 44.704 cap
    const CapsuleStepResult r = driver.step(Vec3{0, 500, 0}, Vec3{0, vyIn, 0}, 1.0);
    const double sinkSpeed = -vyIn;
    const double ratio = sinkSpeed / kControllerTerminalFallSpeedCap;
    const double expectedGravity = kHavokWorldGravityY * (1.0 - ratio * ratio);
    const double expectedVy = vyIn + expectedGravity * 1.0;
    CHECK(deq(r.velocity.y, expectedVy, 1e-9));
    // Sanity: the falloff must genuinely reduce the net acceleration's
    // magnitude relative to the naive (uncapped) -9.8 integration, and must
    // never add energy (flip the sign of the net acceleration).
    const double naiveVy = vyIn + kHavokWorldGravityY * 1.0;
    CHECK(std::fabs(expectedVy - vyIn) < std::fabs(naiveVy - vyIn));
    CHECK(expectedGravity < 0.0);
}

void testDriverAlreadyGroundedStaysPut() {
    EmptyCollisionWorld world;
    CharacterProxyDriver driver(world, 0.4, 1.8, 0.0);
    const CapsuleStepResult r = driver.step(Vec3{0, 0, 0}, Vec3{1, 0, 0}, 1.0);
    CHECK(r.grounded);
    CHECK(deq(r.position.y, 0.0, 1e-9));
    CHECK(deq(r.velocity.y, 0.0, 1e-9));
}

// setGroundY() (2026-10-08 live-playtest ground-following follow-up task) -
// purely additive: updates the SAME flat groundY_ target step() already
// integrates/snaps against (see this method's own header comment).
void testDriverSetGroundYUpdatesTarget() {
    EmptyCollisionWorld world;
    CharacterProxyDriver driver(world, 0.4, 1.8, /*groundY=*/0.0);
    // Starts well above both the original and the soon-to-be-raised target.
    CapsuleStepResult r = driver.step(Vec3{0, 20, 0}, Vec3{0, 0, 0}, 0.1);
    CHECK(!r.grounded);
    CHECK(deq(r.velocity.y, -0.98, 1e-9));

    // Raise the ground target (a per-frame real heightmap sample, in the
    // real caller, mid-fall) - setGroundY() updates the SAME target step()
    // already falls/snaps against.
    driver.setGroundY(5.0);
    Vec3 pos = r.position;
    bool landed = false;
    for (int i = 0; i < 200 && !landed; ++i) {
        r = driver.step(pos, Vec3{0, 0, 0}, 0.1);
        pos = r.position;
        if (r.grounded) landed = true;
    }
    CHECK(landed);
    CHECK(deq(pos.y, 5.0, 1e-6));  // settles at the NEW target, not the original construction-time 0.0
}

void testDriverZeroDtIsNoOp() {
    SingleWallWorld world(5.0);
    CharacterProxyDriver driver(world, 0.4, 1.8, 0.0);
    const CapsuleStepResult r = driver.step(Vec3{1, 0, 0}, Vec3{2, 0, 0}, 0.0);
    CHECK(veq(r.position, {1, 0, 0}));  // unchanged - mirrors 0x0099e7e0's own dt==0 no-op (Sec5.6.2)
}

// ===========================================================================
// PlayerMovement::collisionHook wiring (src/player_locomotion.cpp)
// ===========================================================================

struct FixedSource : MoveInputSource {
    float walkAxisA() override { return 0; }
    float walkAxisB() override { return 0; }
    bool gamepadActive() override { return true; }
    bool held(MoveAction, int*) override { return false; }
    bool pressed(MoveAction, int*) override { return false; }
    void consume(MoveAction) override {}
    bool optionBit0() override { return true; }
};
struct FixedStamina : SprintStamina {
    void notSprintingStep(PlayerMovementState&) override {}
    bool sprintingStep(PlayerMovementState&) override { return true; }
    double available() override { return 1.0; }
    void startPenalty() override {}
    void jumpPenalty() override {}
    void step9ff270() override {}
};

void testCollisionHookUnsetMatchesPlainIntegrator() {
    FixedSource src;
    FixedStamina stamina;
    PlayerMovement pm(src, stamina);
    SeedOnFootCommonCase(pm.world);
    pm.world.controlsDisabled.set(false);
    pm.world.movementDisabled.set(false);
    pm.world.permissions2078.set(0x4000u | 0x2000u);
    pm.tweaks.maxRunSpeed.set(10);
    pm.tweaks.maxSprintSpeed.set(15);
    pm.tweaks.crouchMoveSpeed.set(4);
    pm.tweaks.maxFineAimSpeed.set(3);
    pm.tweaks.acceleration.set(20);
    pm.tweaks.diveRollAcceleration.set(40);
    pm.tweaks.crouchClickSensitivity.set(0.5);
    pm.warp(Vec3{0, 0, 0});
    pm.world.orientation.set(Orientation{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}});

    MoveFrameInput f;
    f.nowMs = 0;
    f.dt = 1.0 / 30.0;
    f.camera.row0 = {1, 0, 0};
    f.camera.row2 = {0, 0, 1};
    pm.st.controllerVelocity1cec = {3, 0, 0};  // bypass the composer; test integrate() directly
    pm.integrate(f);
    const Vec3 noHookPos = pm.st.capsulePosition;
    CHECK(veq(noHookPos, {3.0 / 30.0, 0, 0}));  // exactly the plain "position += velocity*dt" path
    CHECK(pm.st.grounded);  // untouched by the plain path - stays at its constructed default (true)

    // Now with a hook installed: integrate() must route through it instead.
    bool hookCalled = false;
    pm.st.capsulePosition = Vec3{0, 0, 0};
    pm.collisionHook = [&](Vec3 from, Vec3 vel, double dt) -> PlayerMovement::CollisionHookResult {
        hookCalled = true;
        return PlayerMovement::CollisionHookResult{from + Vec3{0, 0, 0}, vel * 0.5, false};  // pretend a wall halved it
    };
    pm.st.controllerVelocity1cec = {3, 0, 0};
    pm.integrate(f);
    CHECK(hookCalled);
    CHECK(veq(pm.st.capsulePosition, {0, 0, 0}));       // the hook's own returned position, not position+vel*dt
    CHECK(veq(pm.capsuleVelocity, {1.5, 0, 0}));         // the hook's own returned (halved) velocity
    CHECK(!pm.st.grounded);                              // the hook's grounded flag is published
}

}  // namespace

int main() {
    testSweepCircleOpenSpace();
    testSweepCircleHeadOnWall();
    testSweepCircleMissesToTheSide();
    testSweepCircleAlreadyInside();
    testSweepSegmentOpenSpace();
    testSweepSegmentFlatSpanHeadOn();
    testSweepSegmentMissesToTheSide();
    testSweepSegmentAlreadyInside();
    testSweepSegmentRoundEndCap();
    testSweepSegmentParallelApproachNoNewContact();
    testSweepTriangleHeadOnFace();
    testSweepTriangleMisses();
    testSweepTriangleAlreadyInside();
    testSweepTriangleCornerVertex();
    testClosestPointOnTriangleVertexRegion();
    testClosestPointOnTriangleInteriorRegion();
    testClosestPointOnTriangleEdgeRegion();
    testRayVsTriangleHeadOnHit();
    testRayVsTriangleMissesOutsideFootprint();
    testRayVsTriangleStopsShortNeverReachesIt();
    testRayVsTriangleParallelToPlaneMisses();
    testSweptSphereHeadOnFaceHit();
    testSweptSphereMissesEntirely();
    testSweptSphereAlreadyOverlappingIsNoNewContact();
    testSweptSphereVertexContact();
    testSweptSphereEdgeContact();
    testDriverOpenSpaceOnePass();
    testDriverStopsAtWall();
    testDriverCornerNeedsMultiplePasses();
    testDriverGravityAndGrounded();
    testDriverGravityTerminalVelocityFalloff();
    testDriverAlreadyGroundedStaysPut();
    testDriverSetGroundYUpdatesTarget();
    testDriverZeroDtIsNoOp();
    testCollisionHookUnsetMatchesPlainIntegrator();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_player_character_collision_test: all checks passed\n";
    return 0;
}
