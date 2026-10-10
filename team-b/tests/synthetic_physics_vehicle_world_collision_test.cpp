// Synthetic tests for sr3physics/vehicle_world_collision.h (vehicle-world-
// collision follow-up task, 2026-10-08, spec-physics-runtime.md Sec10a).
// See that header's own doc comment for the full sourcing/scope account.
//
// These test ApplyVehicleWorldCollisionResponse() directly against a small
// synthetic flat-wall test double (FlatWallTestWorld below) - NOT the real
// `.clmesh_pc` triangle data (that is tools/sr3_viewer.cpp's own
// ClmeshTriangleCollisionWorld/RealCameraCollision, already covered by
// tests/synthetic_player_character_collision_test.cpp's
// SweptSphereVsTriangle3D cases; this file's own job is the RESPONSE
// function, independent of any particular collision-world shape, same
// "pure helper, independently testable" discipline that header's own doc
// comment describes). Proves:
//   * a vehicle approaching a flat wall HEAD-ON stops/clamps at the wall
//     (testHeadOnWallStopsVehicle);
//   * a vehicle approaching at a GLANCING angle slides/deflects along the
//     surface instead of stopping dead (testGlancingWallSlidesAlongSurface);
//   * a vehicle not approaching any obstacle drives completely unaffected -
//     no false-positive collision (testNoObstacleDrivesNormally);
//   * two documented edge cases: a zero-length attempted move is a no-op
//     even right at a wall (testStationaryVehicleIsNoOp), and a contact
//     whose own velocity-into-normal is already >= 0 (or whose normal has
//     no meaningful XZ component) still clamps POSITION but leaves velocity
//     untouched (testMovingAwayFromWallClampsPositionOnlyNotVelocity,
//     testNormalWithNoPlanarComponentClampsPositionOnly).
#include <cmath>
#include <iostream>

#include "sr3physics/vehicle_world_collision.h"

using namespace sr3physics;

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

// A single infinite flat wall at world X = wallX_, normal (-1,0,0) (points
// away from the wall, into the open space the vehicle starts in - the SAME
// convention sr3player::WallContact/character_collision.h's own sweep
// primitives already document). Models the sweep as "does the SPHERE
// CENTER's own attempted X move cross wallX_ - radius" (the sphere-surface-
// touches-wall threshold), exactly the shape SweepCircleVsAabbXZ's own
// "expand the obstacle by the radius" construction already uses elsewhere
// in this project - a plain, honest synthetic double, not real triangle
// geometry.
class FlatWallTestWorld : public IVehicleWorldCollisionQuery {
public:
    explicit FlatWallTestWorld(double wallX) : wallX_(wallX) {}
    bool SweepSphere(double x0, double y0, double z0, double x1, double y1, double z1, double radius,
                      VehicleWorldContact& out) const override {
        const double surfaceX = wallX_ - radius;
        if (x0 >= surfaceX) return false;  // already at/through the wall - not exercised by these tests
        if (x1 < surfaceX) return false;   // sweep never reaches the wall
        const double f = (surfaceX - x0) / (x1 - x0);
        out.x = x0 + (x1 - x0) * f;
        out.y = y0 + (y1 - y0) * f;
        out.z = z0 + (z1 - z0) * f;
        out.nx = -1.0;
        out.ny = 0.0;
        out.nz = 0.0;
        return true;
    }

private:
    double wallX_;
};

// Same shape, but the wall's own normal points straight UP (ny=1) instead
// of sideways - a stand-in for "a near-horizontal surface" (e.g. the
// underside of an overhang), used ONLY by
// testNormalWithNoPlanarComponentClampsPositionOnly below to exercise that
// one documented branch (the header's own "near-horizontal normal... no
// meaningful planar direction to clamp against" case).
class VerticalNormalTestWorld : public IVehicleWorldCollisionQuery {
public:
    explicit VerticalNormalTestWorld(double wallX) : wallX_(wallX) {}
    bool SweepSphere(double x0, double y0, double z0, double x1, double y1, double z1, double radius,
                      VehicleWorldContact& out) const override {
        const double surfaceX = wallX_ - radius;
        if (x0 >= surfaceX || x1 < surfaceX) return false;
        const double f = (surfaceX - x0) / (x1 - x0);
        out.x = x0 + (x1 - x0) * f;
        out.y = y0 + (y1 - y0) * f;
        out.z = z0 + (z1 - z0) * f;
        out.nx = 0.0;
        out.ny = 1.0;
        out.nz = 0.0;
        return true;
    }

private:
    double wallX_;
};

// ===========================================================================
// ApplyVehicleWorldCollisionResponse
// ===========================================================================

void testHeadOnWallStopsVehicle() {
    // Wall at x=10, vehicle (radius 1.0) driving straight at it along +X
    // (heading=0 -> forward=(1,0)) at 5 m/s. The attempted move (8.5 -> 9.5)
    // crosses the sphere-surface threshold (10 - 1 = 9) at fraction 0.5.
    FlatWallTestWorld world(10.0);
    VehicleWorldCollisionState state;
    state.posX = 9.5;
    state.posZ = 0.0;
    state.heading = 0.0;
    state.velForwardBody = 5.0;
    state.velRightBody = 0.0;
    const bool applied = ApplyVehicleWorldCollisionResponse(world, /*prevX=*/8.5, /*prevZ=*/0.0, /*worldY=*/0.0,
                                                             /*radius=*/1.0, state);
    CHECK(applied);
    CHECK(deq(state.posX, 9.0));           // clamped to the sweep's own non-penetrating contact
    CHECK(deq(state.posZ, 0.0));
    CHECK(deq(state.velForwardBody, 0.0, 1e-9));  // head-on: velocity INTO the wall fully removed - stops
    CHECK(deq(state.velRightBody, 0.0, 1e-9));
}

void testGlancingWallSlidesAlongSurface() {
    // Same wall, but the vehicle approaches at a shallow 80-degree angle off
    // the wall's own normal (mostly moving ALONG the wall, only drifting
    // into it slightly) at 10 m/s - a believable glancing hit, not a
    // single-axis clamp.
    const double headingRad = 80.0 * 3.14159265358979323846 / 180.0;
    const double c = std::cos(headingRad), s = std::sin(headingRad);
    FlatWallTestWorld world(10.0);
    VehicleWorldCollisionState state;
    // Attempted move consistent with forward=(c,s) at 10 m/s over a short
    // dt=0.1s, starting from just inside the sphere-surface threshold (9.0)
    // so it just barely penetrates past it - a grazing contact.
    const double prevX = 8.9, prevZ = 0.0;
    state.posX = prevX + 10.0 * c * 0.1;  // ~9.0736 - just past the 9.0 threshold
    state.posZ = prevZ + 10.0 * s * 0.1;  // ~0.9848
    state.heading = headingRad;
    state.velForwardBody = 10.0;
    state.velRightBody = 0.0;
    const double originalSpeed =
        std::sqrt(state.velForwardBody * state.velForwardBody + state.velRightBody * state.velRightBody);

    const bool applied = ApplyVehicleWorldCollisionResponse(world, prevX, prevZ, /*worldY=*/0.0, /*radius=*/1.0, state);
    CHECK(applied);
    CHECK(deq(state.posX, 9.0, 1e-6));  // position still clamped to the wall

    const double newSpeed =
        std::sqrt(state.velForwardBody * state.velForwardBody + state.velRightBody * state.velRightBody);
    // Slides, does not stop dead: most of the original speed survives
    // (only the small component pointing INTO the wall was removed).
    CHECK(newSpeed > 0.9 * originalSpeed);
    // ... but it is not a complete pass-through either - SOME speed was
    // actually removed (distinguishes this from testNoObstacleDrivesNormally).
    CHECK(newSpeed < originalSpeed - 1e-6);

    // The remaining world-space velocity has (within tolerance) no
    // component left pointing into the wall's own normal (-1,0) - the
    // defining property of "clamped the inward component, kept the rest".
    const double wc = std::cos(state.heading), ws = std::sin(state.heading);
    const double worldVelX = state.velForwardBody * wc + state.velRightBody * ws;
    CHECK(deq(worldVelX, 0.0, 1e-6));
}

void testNoObstacleDrivesNormally() {
    // The wall is far away (x=1000) - nothing along this attempted move
    // (0 -> 1) ever reaches it. Must be a complete no-op: no false-positive
    // collision on a clear path.
    FlatWallTestWorld world(1000.0);
    VehicleWorldCollisionState state;
    state.posX = 1.0;
    state.posZ = 0.0;
    state.heading = 0.0;
    state.velForwardBody = 5.0;
    state.velRightBody = 0.0;
    const bool applied =
        ApplyVehicleWorldCollisionResponse(world, /*prevX=*/0.0, /*prevZ=*/0.0, /*worldY=*/0.0, /*radius=*/1.0, state);
    CHECK(!applied);
    CHECK(deq(state.posX, 1.0));
    CHECK(deq(state.posZ, 0.0));
    CHECK(deq(state.velForwardBody, 5.0));
    CHECK(deq(state.velRightBody, 0.0));
}

void testStationaryVehicleIsNoOp() {
    // Parked right at the wall's own doorstep (prevX == posX, no attempted
    // move at all) - a zero-length sweep has no well-defined direction, so
    // this must be a no-op even though a moving vehicle at this exact
    // position would hit.
    FlatWallTestWorld world(10.0);
    VehicleWorldCollisionState state;
    state.posX = 8.9;
    state.posZ = 0.0;
    state.heading = 0.0;
    state.velForwardBody = 0.0;
    state.velRightBody = 0.0;
    const bool applied =
        ApplyVehicleWorldCollisionResponse(world, /*prevX=*/8.9, /*prevZ=*/0.0, /*worldY=*/0.0, /*radius=*/1.0, state);
    CHECK(!applied);
    CHECK(deq(state.posX, 8.9));
}

void testMovingAwayFromWallClampsPositionOnlyNotVelocity() {
    // A hit IS found geometrically (the attempted X move crosses the
    // threshold, same as testHeadOnWallStopsVehicle - this is a synthetic
    // double, not real physics, so an inconsistent "attempted move" is fine
    // for isolating one branch, same discipline this project's other
    // synthetic geometry tests already use, e.g.
    // testRayVsTriangleParallelToPlaneMisses()), but the vehicle's own
    // heading (180 degrees - forward=(-1,0), i.e. the vehicle is actually
    // facing/moving AWAY from the wall at x=10 toward open space at -X)
    // means its world-space velocity has a POSITIVE (not negative) dot
    // product with the wall's own (-1,0) normal - "into" is comfortably
    // positive, not a borderline-zero case sensitive to floating-point
    // noise in cos/sin. The response must still clamp POSITION (never let
    // the vehicle penetrate, regardless of which way it is nominally
    // facing) but must leave velocity completely untouched, and report "no
    // velocity response applied" via its return value.
    FlatWallTestWorld world(10.0);
    VehicleWorldCollisionState state;
    state.posX = 9.5;
    state.posZ = 0.0;
    state.heading = 3.14159265358979323846;  // 180 degrees: forward=(-1,0)
    state.velForwardBody = 5.0;
    state.velRightBody = 0.0;
    const bool applied = ApplyVehicleWorldCollisionResponse(world, /*prevX=*/8.5, /*prevZ=*/0.0, /*worldY=*/0.0,
                                                             /*radius=*/1.0, state);
    CHECK(!applied);                 // "a contact was found AND a velocity response was applied" - false here
    CHECK(deq(state.posX, 9.0));     // ... but the position clamp (step 1) still ran
    CHECK(deq(state.velForwardBody, 5.0));  // velocity completely untouched
    CHECK(deq(state.velRightBody, 0.0));
}

void testNormalWithNoPlanarComponentClampsPositionOnly() {
    // The contact's own normal points straight up (0,1,0) - no XZ
    // component at all (the header's own documented "near-horizontal
    // normal... no meaningful planar direction to clamp against" case).
    // Position must still be clamped; velocity must be untouched; return
    // value must be false (no velocity response applied).
    VerticalNormalTestWorld world(10.0);
    VehicleWorldCollisionState state;
    state.posX = 9.5;
    state.posZ = 0.0;
    state.heading = 0.0;
    state.velForwardBody = 5.0;
    state.velRightBody = 0.0;
    const bool applied = ApplyVehicleWorldCollisionResponse(world, /*prevX=*/8.5, /*prevZ=*/0.0, /*worldY=*/0.0,
                                                             /*radius=*/1.0, state);
    CHECK(!applied);
    CHECK(deq(state.posX, 9.0));
    CHECK(deq(state.velForwardBody, 5.0));
    CHECK(deq(state.velRightBody, 0.0));
}

}  // namespace

int main() {
    testHeadOnWallStopsVehicle();
    testGlancingWallSlidesAlongSurface();
    testNoObstacleDrivesNormally();
    testStationaryVehicleIsNoOp();
    testMovingAwayFromWallClampsPositionOnlyNotVelocity();
    testNormalWithNoPlanarComponentClampsPositionOnly();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All synthetic_physics_vehicle_world_collision_test checks passed\n";
    return 0;
}
