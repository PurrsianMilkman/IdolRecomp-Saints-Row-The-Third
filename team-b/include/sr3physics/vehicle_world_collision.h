#pragma once

// sr3physics/vehicle_world_collision.h - vehicle-vs-world collision RESPONSE
// (vehicle-world-collision follow-up task, 2026-10-08, spec-physics-
// runtime.md Sec10a: "Contact-listener collision detection and response -
// vehicle and generic").
//
// BACKGROUND: `tools/sr3_viewer.cpp`'s playtest build
// (sr3physics::VehicleSubstepSimulator via PlaytestVehicleRig) had a
// working vehicle entry/driving feature with NO tie to this project's real
// world-collision geometry (VehicleSubstepSimulator is a planar, self-
// contained model - see that header's own "PLANAR SCOPE" banner). The
// orchestrator personally observed the car driving through a street-light
// pole. Sec10a's own key finding is good news for scope: "There is no
// separate Volition-authored impulse/restitution formula at the game layer
// for the generic case - Havok's own solver does it directly" - i.e. the
// real RESPONSE shape is a plain physics impulse (stop/redirect velocity at
// the contact), not a bespoke game-specific formula this project would have
// to reverse-engineer. This header implements exactly that shape.
//
// Sec10a also traces a 300-record deferred contact-event queue, drained
// once per frame on the real engine's own main thread - architecture for
// the real engine's MULTI-THREADED Havok step (contacts are detected on a
// separate physics-step thread, then applied later on the main thread).
// This project's playtest loop is single-threaded and synchronous, so
// ApplyVehicleWorldCollisionResponse() below applies the response
// IMMEDIATELY, in the same call that detected it, rather than staging it
// into any queue - a CHOSEN, explicitly-stated simplification (there is no
// second thread/frame boundary in this harness to defer across), not a
// silent omission of the real architecture.
//
// Deliberately decoupled from any concrete collision-world/shape-cast
// implementation (no dependency on sr3player::CameraCollision, Vec3, or any
// D3D11 type) so the response logic itself is independently testable with a
// synthetic double - the same "pure geometry helper, independently
// testable" discipline sr3player/character_collision.h already uses for its
// own sweep primitives (ClosestPointOnTriangle3D/SweptSphereVsTriangle3D
// etc.). The one real, concrete adapter in this project
// (tools/sr3_viewer.cpp's RealCameraCollisionAsVehicleWorld, wrapping
// RealCameraCollision::sweepSphere() - the SAME real `.clmesh_pc` triangle
// data/BVH the player's own CharacterProxyDriver and the follow camera
// already query) lives there, not here; see PlaytestVehicleRig::
// ApplyWorldCollisionResponse()'s own doc comment for the live wiring.
//
// Vehicle-vs-vehicle collision/damage and the damage-AMOUNT formula are
// explicitly OUT of scope here (Sec10a's own "genuinely OPEN" item; a
// separate, larger, not-yet-dispatched unit per this project's own priority
// queue) - this header is the physical collision RESPONSE only (stop/
// deflect at solid geometry instead of driving through it).

namespace sr3physics {

// One swept-sphere contact, in the caller's own world-space XYZ (plain
// doubles - no dependency on any particular Vec3 type, so this header has
// none).
struct VehicleWorldContact {
    double x = 0.0, y = 0.0, z = 0.0;     // the sweep's own non-penetrating contact position
    double nx = 0.0, ny = 0.0, nz = 0.0;  // unit normal, points away from the surface into open space
};

// The query seam ApplyVehicleWorldCollisionResponse() needs: "sweep a
// sphere of `radius` from (x0,y0,z0) to (x1,y1,z1), report the FIRST
// contact". One concrete adapter exists in this project
// (tools/sr3_viewer.cpp's RealCameraCollisionAsVehicleWorld); synthetic test
// doubles (tests/synthetic_physics_vehicle_world_collision_test.cpp) are a
// second, built directly against a flat test wall with no real triangle
// data at all - this header's own geometry (where exactly the contact
// sits/what its normal is) is the caller's responsibility, same as
// sr3player::ICollisionWorld's own injectable-seam discipline.
class IVehicleWorldCollisionQuery {
public:
    virtual ~IVehicleWorldCollisionQuery() = default;
    virtual bool SweepSphere(double x0, double y0, double z0, double x1, double y1, double z1, double radius,
                              VehicleWorldContact& out) const = 0;
};

// The vehicle planar state this response reads/writes - mirrors the fields
// of sr3physics::VehicleInstanceState (vehicle_substeps.h) this task
// actually touches (posX/posZ/heading/velForwardBody/velRightBody), kept as
// its own small struct rather than taking a VehicleInstanceState& directly
// so this header stays includable/testable on its own, with no dependency
// on vehicle_substeps.h's own much larger state.
struct VehicleWorldCollisionState {
    double posX = 0.0, posZ = 0.0;
    double heading = 0.0;                                 // see vehicle_substeps.h: (cos(heading),sin(heading)) = chassis forward in (X,Z)
    double velForwardBody = 0.0, velRightBody = 0.0;        // body frame: x=forward, y=right - SAME convention as VehicleInstanceState
};

// Tests the attempted move (prevX,prevZ) -> (state.posX,state.posZ) (both at
// `worldY` - this model is PLANAR, vehicle_substeps.h's own "no vertical
// chassis motion" scope note, so there is only ever one world Y to sweep
// at) against `world` using a bounding sphere of `radius`, and on a hit:
//   1. Snaps state.posX/posZ to the sweep's own non-penetrating contact
//      position - "keep the position from penetrating the surface".
//   2. Applies a plain physics impulse matching Sec10a's own finding
//      ("Havok's own solver does it directly", no bespoke restitution
//      formula): zeroes the WORLD-space velocity component pointing INTO
//      the contact normal's own XZ projection (the normal's Y component, if
//      any - a sloped/angled triangle - is deliberately dropped: this
//      model has no vertical velocity state to apply it to, same PLANAR
//      boundary as posY), leaving the tangential component untouched. A
//      head-on hit (velocity ~antiparallel to the normal) stops the vehicle
//      almost dead; a glancing hit (velocity mostly tangential) removes
//      only the small inward sliver and the vehicle keeps sliding along
//      the surface - the believable "stop/deflect" shape this task's own
//      brief asks for, not a bespoke bounce/restitution formula.
// Yaw rate is deliberately left untouched - a real rotational response
// needs a contact-point lever-arm/angular-impulse formula Sec10a does not
// give (its own "no separate Volition-authored formula" finding covers the
// LINEAR impulse only); inventing one would be exactly the "no invented
// fixes" violation this project's own standing rule forbids.
//
// Returns true iff a contact was found AND a velocity response was applied
// (so a caller/test can tell a true "no obstacle" pass-through from a "hit,
// but the normal had no meaningful planar component" near-miss, in which
// the position clamp from step 1 above was still applied but step 2 was
// not). A zero-length attempted move (prevX==state.posX && prevZ==state.posZ,
// within a small epsilon) is treated as "nothing to test" and always
// returns false unchanged - a zero-length sweep has no well-defined
// direction.
bool ApplyVehicleWorldCollisionResponse(const IVehicleWorldCollisionQuery& world, double prevX, double prevZ,
                                         double worldY, double radius, VehicleWorldCollisionState& state);

}  // namespace sr3physics
