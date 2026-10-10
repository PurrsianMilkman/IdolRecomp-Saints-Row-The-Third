// src/physics_vehicle_world_collision.cpp - see include/sr3physics/
// vehicle_world_collision.h for the full citation/doc comment.
#include "sr3physics/vehicle_world_collision.h"

#include <cmath>

namespace sr3physics {

bool ApplyVehicleWorldCollisionResponse(const IVehicleWorldCollisionQuery& world, double prevX, double prevZ,
                                         double worldY, double radius, VehicleWorldCollisionState& state) {
    if (std::abs(state.posX - prevX) < 1e-9 && std::abs(state.posZ - prevZ) < 1e-9)
        return false;  // stationary this step - a zero-length sweep has no well-defined direction to test

    VehicleWorldContact hit;
    if (!world.SweepSphere(prevX, worldY, prevZ, state.posX, worldY, state.posZ, radius, hit)) return false;

    // Step 1: stop penetration - snap to the sweep's own non-penetrating
    // contact position (XZ only; this model carries no vertical state).
    state.posX = hit.x;
    state.posZ = hit.z;

    double nx = hit.nx, nz = hit.nz;
    const double nLen = std::sqrt(nx * nx + nz * nz);
    if (nLen < 1e-6) return false;  // near-horizontal normal (e.g. the underside of an overhang) - no
                                      // meaningful planar direction to clamp against; position clamp above still applied
    nx /= nLen;
    nz /= nLen;

    // Step 2: plain impulse - zero the world-space velocity component
    // pointing INTO the contact normal. Body<->world convention matches
    // VehicleSubstepSimulator::IntegrateChassis() (src/physics_vehicle_
    // substeps.cpp) exactly: worldVel = velForwardBody*(c,s) + velRightBody*(s,-c),
    // where (c,s) = (cos(heading),sin(heading)). That 2x2 matrix is its own
    // inverse (an involution: M*M = I, straightforward from c^2+s^2=1), so
    // the SAME formula converts world space back to body space.
    const double c = std::cos(state.heading), s = std::sin(state.heading);
    double worldVelX = state.velForwardBody * c + state.velRightBody * s;
    double worldVelZ = state.velForwardBody * s - state.velRightBody * c;

    const double into = worldVelX * nx + worldVelZ * nz;  // normal points away from the surface,
                                                             // so "into" the wall is negative
    if (into >= 0.0) return false;  // already moving away from (or parallel to) the contact - no clamp needed

    worldVelX -= into * nx;
    worldVelZ -= into * nz;
    state.velForwardBody = worldVelX * c + worldVelZ * s;
    state.velRightBody = worldVelX * s - worldVelZ * c;
    return true;
}

}  // namespace sr3physics
