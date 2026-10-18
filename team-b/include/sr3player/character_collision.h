#pragma once

// sr3player/character_collision.h - 2026-10-07 follow-up task: wires the
// REAL character-proxy lock-step loop and listener queue-only contract
// (include/sr3physics/character_proxy.h, already built by a prior unit) to
// drive PlayerMovement's own capsule against real collision geometry,
// instead of movement.h's licensed-but-naive direct integrator
// ("position += velocity*dt", spec-player-runtime.md Sec5.11 item 5).
//
// WHAT THIS FILE REUSES, UNMODIFIED (per the task's own instruction - drive
// the existing contract, do not rebuild it):
//   * sr3physics::CharacterProxyStepper::run() - the up-to-10-pass lock-step
//     cap, CONFIRMED (spec-physics-runtime.md Sec4.6 item 3 / spec-player-
//     runtime.md Sec5.6.4 step 3: "every listed proxy advances one cast-
//     and-solve over its remaining time... joins the next list... At most
//     10 passes per frame"). Correction to this task's own brief, stated
//     plainly: the brief calls this an "8-pass lock-step" - the CONFIRMED,
//     already-coded number (character_proxy.h's own kMaxPasses, cited to
//     the same two spec sections) is 10, not 8. This file uses the real
//     constant, not the brief's number.
//   * sr3physics::CharacterProxyListener<T,T>'s queue-only contract
//     (spec-physics-runtime.md Sec2.2, "ALL THREE ONLY QUEUE"; spec-player-
//     runtime.md Sec5.8): a wall touch is pushed through
//     EnqueueOnly<WallHitRecord>::push() from inside a pass (mirroring
//     objectInteractionCallback -> queue 0x0223d3b0, since this is a
//     character-touches-world-object interaction, not a character-touches-
//     character one), and the queue is flushed into ordinary game code
//     (CapsuleStepResult::hits) OUTSIDE the pass loop - "flushed back into
//     game state afterward by ordinary game code" (spec-player-runtime.md
//     Sec5.8, CONFIRMED timing).
//
// WHAT WAS CHECKED AND DELIBERATELY NOT USED:
// spec-physics-runtime.md Sec9 (W-U0, job_dispatch.h's own
// JobQueuePhysicsBackend/JobTypeTable) models a DIFFERENT mechanism: the
// Havok world-step's own inner job-TYPE dispatch (hkJobQueue::
// processAllJobs, types 0-3, reached through IPhysicsBackend::drainJobs()
// inside PhysicsFrameDriver::runFrame()'s single world step). The
// character-proxy's own cast-and-solve passes run on a THIRD, separate
// mechanism - "the scheduler group 0x0223ac40, which 0x0079b470 creates
// when it submits them" (spec-player-runtime.md Sec5.6.4 step 3) - which
// neither frame_contract.h's IGameJobScheduler (kStepJobCount=2, the OUTER
// per-step game-job fan-out) nor job_dispatch.h's JobTypeTable (the INNER
// Havok job-TYPE table, Sec9.1's kShippedJobTypeCount=4) models at all.
// character_proxy.h's own file doc comment already says so explicitly:
// "Scheduler job group 0x0223ac40 itself - no real job scheduler exists in
// this project (Scope discipline); passes here run synchronously, in
// whatever order the caller drives them." This file follows that: passes
// run synchronously on the caller's thread, not through JobQueuePhysicsBackend
// or any worker pool - there is no real multithreaded job scheduler in this
// project for ANY of the three job mechanisms the spec names, and bolting
// the character-proxy passes onto job_dispatch.h's unrelated hkJobQueue
// type-dispatch table would invent a relationship no spec section states.
//
// UPDATE 2026-10-07 (this follow-up task, after merging Team A's fresh spec
// sync): spec-player-runtime.md Sec5.6.5 now CLOSES OPEN 35 ("Controller
// motion 0x007a5b10... CONFIRMED by full listing (closes OPEN 35, 2026-10-07,
// unit W-U2)") - the text below is written against that CLOSED state, not
// the still-open state a prior pass of this file was written against
// (spec-physics-runtime.md Sec7.11's "OPEN 35... Not resolved" is itself the
// OLDER W-U1 unit's own text, superseded for the state-machine question by
// the later W-U2 unit Sec5.6.5 cites). What is now CONFIRMED and IS
// implemented below, reconciled against the complete spec text:
//   * The exact gravity-integration formula (spec-player-runtime.md Sec5.6.5,
//     "Gravity integration (the actual formula)"): while unsupported and
//     already sinking (sinkSpeed = -dot(velocity,up) > 0), gravity is scaled
//     by (1 - ratio^2) where ratio = sinkSpeed / controller+0x170 - a
//     quadratic terminal-velocity falloff against the CONFIRMED speed cap
//     44.704 (100 mph, spec-player-runtime.md Sec5.6.1) - before
//     velocity += gravity * dt. The gravity vector itself is the Havok
//     world's own CONFIRMED construction constant (spec-physics-runtime.md
//     Sec4.1 step 5 / Table row 5: "gravity (0, -9.8, 0) (Y-up)").
//
// What remains a DELIBERATE, documented simplification - NOT because the
// spec is silent (it no longer is), but because reproducing it needs a real
// Havok support query / step-assist raycasts this project's Scope discipline
// explicitly excludes (no real Havok collision shapes):
//   * The support resolution itself (spec-player-runtime.md Sec5.6.5
//     "Support resolution", the three raycasts of "Step-up/step-down/ledge-
//     assist", and state bits 0x02/0x08/0x10/0x20/0x40/0x80 those raycasts
//     and the slope-removal/too-steep logic maintain) - this file uses a
//     simple ground-plane binary (position.y <= groundY) in its place, the
//     same stand-in a prior pass of this file chose, now stated as a
//     CHOSEN simplification rather than "the state machine is unresolved".
//   * Jump launch (same section, "gated on heightArg > 0.0") - the formula
//     is now fully known (speed = sqrt(2*|gravity|*heightArg), launched
//     opposite gravity) but is NOT implemented: the walk demo this ships
//     with has no jump input bound, so adding an unexercised code path
//     would not demonstrate anything and risks an untested formula
//     transcription - left OPEN for whichever unit actually wires jump
//     input, not fabricated here.
//   * The collision capsule's own radius/height (0.4 m / 1.8 m default
//     below) are ordinary human-scale placeholders, not recovered engine
//     values - only the Havok PROXY's two AABB-growth margins (+0x88/+0x8c)
//     are spec-confirmed (spec-physics-runtime.md Sec7.1/Sec7.5), and those
//     pad an AABB for broadphase, not a capsule radius for this port's own
//     narrow-phase sweep.
//   * ICollisionWorld is an injectable seam; the only concrete
//     implementation in this project (tools/sr3_viewer.cpp's walk-demo
//     --v2 path) is an explicit, orchestrator-approved interim stand-in -
//     see that file's own doc comment for exactly what geometry it uses and
//     why real Havok collision shapes are out of this task's scope.

#include <cstdint>
#include <functional>
#include <vector>

#include "sr3physics/character_proxy.h"
#include "sr3player/movement.h"

namespace sr3player {

// CONFIRMED - spec-physics-runtime.md Sec4.1 step 5 / Table 5 row 5 ("Bring-
// up, once: ... one world (gravity (0,-9.8,0), broadphase box ...)"). The
// one vertical-motion fact either physics spec file states outright; see
// this header's own top comment for what is and is not modelled beyond it.
inline constexpr double kHavokWorldGravityY = -9.8;

// CONFIRMED - controller+0x170, spec-player-runtime.md Sec5.6.1 ("+0x170 =
// 44.704 (100 mph; HIGH: a speed cap)") and Sec5.6.5's gravity formula,
// which divides sinkSpeed by exactly this field to build the quadratic
// terminal-velocity falloff ratio (see CharacterProxyDriver::step()).
inline constexpr double kControllerTerminalFallSpeedCap = 44.704;

// One contact an ICollisionWorld sweep reports along an attempted
// horizontal move.
struct WallContact {
    Vec3 point;
    Vec3 normal;                   // unit-length, points away from the wall into open space
    double travelFraction = 1.0;   // in [0,1]: how far along (from -> to) the contact sits
    uint32_t surfaceId = 0;        // caller-defined (diagnostic only - which prop/box was hit)
};

// The injectable collision seam this driver queries once per lock-step
// pass. See this header's own top comment for the one concrete
// implementation in this project and why it is a deliberate stand-in.
class ICollisionWorld {
public:
    virtual ~ICollisionWorld() = default;
    // Sweeps a vertical cylinder (radius `radius`, spanning world Y
    // [groundY, groundY+height]) horizontally from `from` to `to` (both at
    // the SAME Y - the vertical axis is integrated separately, see
    // CharacterProxyDriver::step()). Returns true and fills `out` with the
    // FIRST (smallest travelFraction) contact found; false if the segment
    // is clear.
    virtual bool sweepCylinder(const Vec3& from, const Vec3& to, double radius, double groundY, double height,
                                WallContact& out) const = 0;
};

// A collision world with no geometry at all - every sweep is clear. Used
// where no real geometry exists yet (tests, or a caller that has not wired
// one in) so PlayerMovement::collisionHook can still be exercised through
// the real pass loop without a NULL-world special case.
class EmptyCollisionWorld : public ICollisionWorld {
public:
    bool sweepCylinder(const Vec3&, const Vec3&, double, double, double, WallContact&) const override {
        return false;
    }
};

// One touched-wall record, queued through sr3physics::CharacterProxyListener
// (queue-only - see this header's own top comment) and flushed into
// CapsuleStepResult::hits outside the pass loop.
struct WallHitRecord {
    uint32_t surfaceId = 0;
    Vec3 point;
    Vec3 normal;
    int mergeCount = 1;
};

// CharacterProxyDriver::step()'s own result: the resolved position/velocity
// for this frame, the grounded/falling state bit, how many lock-step passes
// actually ran (<= sr3physics::CharacterProxyStepper::kMaxPasses, CONFIRMED
// 10), and every wall touched this frame (already flushed, see above).
struct CapsuleStepResult {
    Vec3 position;
    Vec3 velocity;
    bool grounded = true;
    int passesRun = 0;
    std::vector<WallHitRecord> hits;
};

// Pure geometry helper, exposed so it is independently testable: sweeps a
// circle of radius `radius` from (x0,z0) to (x1,z1) against an axis-aligned
// rectangle [minX,maxX]x[minZ,maxZ], via the standard "expand the box by the
// radius, then slab-test the segment" construction (exact on the box's four
// flat faces; a conservative over-approximation of the true rounded-corner
// Minkowski sum at the four corners - acceptable for this project's AABB-as-
// collision stand-in, not claimed as exact corner behaviour). Returns true
// and fills outF (the contact's fraction along the segment, in [0,1]) and
// outNormal (unit, axis-aligned) on the first contact; false if the swept
// circle never enters the expanded box, or if it starts the sweep already
// inside it (CHOSEN: treated as "no new contact to report" rather than an
// immediate degenerate fraction-0 hit with no well-defined face normal).
bool SweepCircleVsAabbXZ(double x0, double z0, double x1, double z1, double radius, double minX, double maxX,
                          double minZ, double maxZ, double& outF, Vec3& outNormal);

// ---- real `.clmesh_pc` triangle collision (2026-10-07 follow-up task) ----
// Replaces the render-mesh-AABB stand-in above with real collision
// triangles decoded from `.clmesh_pc`'s own embedded Mesh sub-block (see
// ClmeshTriangleCollisionWorld, tools/sr3_viewer.cpp, for the real data
// source and its own BVH). The two functions below are the pure geometry
// primitives that world uses, exposed here so they are independently
// testable exactly like SweepCircleVsAabbXZ above.

// Swept circle (centre moving (x0,z0) -> (x1,z1), radius `radius`) vs ONE
// 2D line segment [A,B] - the "stadium" Minkowski shape (the segment
// expanded by `radius`: a flat span plus two round end-caps). CHOSEN,
// exact closed-form sweep (not sampled): the flat span's perpendicular
// (2D cross-product) distance from a linearly-moving point to a FIXED
// line is itself linear in t, so that crossing is a plain linear solve;
// each round end-cap is the standard swept-circle-vs-point quadratic.
// Returns true and fills outF/outNormal (unit, radiating away from
// whichever feature - flat span or end-cap vertex - was actually hit) on
// the EARLIEST contact; false if the swept circle never enters the
// stadium, or if it starts the sweep already inside it (CHOSEN: "no new
// contact to report", the same convention SweepCircleVsAabbXZ's own doc
// comment states for its box - a pre-existing graze never produces a
// degenerate zero-fraction hit with no well-defined normal). Used by
// SweepCircleVsTriangleXZ below, one call per edge.
bool SweepCircleVsSegmentXZ(double x0, double z0, double x1, double z1, double radius,
                             double ax, double az, double bx, double bz,
                             double& outF, Vec3& outNormal);

// Swept circle vs ONE 2D triangle (A, B, C) - the real `.clmesh_pc`
// collision-triangle shape, in the SAME XZ-plane circle-sweep shape
// SweepCircleVsAabbXZ already uses, just against a triangle instead of a
// box. CHOSEN algorithm (this task, 2026-10-08): test the swept circle
// against each of the triangle's 3 edges via SweepCircleVsSegmentXZ above
// and report the EARLIEST of the 3 contacts; "already inside the
// triangle's own 2D footprint at t=0" is CHOSEN to report "no new
// contact" (same convention as SweepCircleVsAabbXZ's box).
//
// KNOWN SIMPLIFICATION, stated plainly rather than hidden: each edge is
// tested independently of its two neighbours, so for a MESH of several
// triangles sharing edges, a sweep that first grazes a SHARED (interior)
// edge from the enterable side can in principle register a contact a
// true single-silhouette sweep of the whole mesh's outer boundary would
// not - the same category of approximation this project's own
// SweepCircleVsAabbXZ already accepts for its box corners (see that
// function's own doc comment). Not expected to matter for this project's
// actual `.clmesh_pc` content (small, mostly-convex street-furniture/wall
// shapes - see ClmeshTriangleCollisionWorld's own doc comment for the real
// population this was checked against); the caller only ever keeps the
// SINGLE earliest contact across the whole scene regardless, same as
// every other ICollisionWorld implementation in this project.
bool SweepCircleVsTriangleXZ(double x0, double z0, double x1, double z1, double radius,
                              double ax, double az, double bx, double bz, double cx, double cz,
                              double& outF, Vec3& outNormal);

// ---- real 3D ray/sphere geometry (2026-10-08 castRay/shape-cast follow-up
// task) - spec-physics-runtime.md Sec8.8-Sec8.11: the game's shared
// on-demand Havok query mechanism (camera collision + AI line-of-sight,
// both CONFIRMED by Sec8.11 to funnel through one of two shared wrappers,
// 0x0075c880 the shape/sphere-cast side, 0x0075d7e0 the ray side). Unlike
// SweepCircleVsTriangleXZ above - deliberately XZ-plane-locked, matching
// the character proxy's own horizontal-only movement integration
// (CharacterProxyDriver::step() integrates the vertical axis separately) -
// neither the camera nor an AI's line-of-sight check is constrained to a
// horizontal plane, so this is a genuinely 3D pair of primitives, not a
// reuse of the 2D ones. CHOSEN exact closed-form algorithms throughout (the
// same "algorithm is CHOSEN, the call CONTRACT is CONFIRMED" discipline
// SweepCircleVsSegmentXZ's own doc comment already states) - Sec8.9's own
// "call-shape only, not opened" caveat for the two inner Havok entries
// (0x00c170e0/0x00d4ccf0) means the real narrowphase MATH inside Havok's
// own hkpTriangle/hkpSphereShape routines was never read; only the outer
// CALL CONTRACT (castRay(world, rayInput, collector); Sec8.10's "a port
// does not need Havok's own broadphase if it reproduces the game's own
// broadphase-border interception first" - this project's own per-prop BVH,
// built for the clmesh_pc character-collision merge, IS that
// broadphase-border substitute) is CONFIRMED.
//
// SHARED, REUSABLE: both functions below are called from BOTH the real
// camera collision (tools/sr3_viewer.cpp's RealCameraCollision, a swept-
// sphere query) and the real AI line-of-sight test
// (include/sr3airuntime/line_of_sight.h's ClosestHitRayVsTriangles, a
// closest-hit ray query) - ONE implementation, two consumers, both
// querying the SAME real `.clmesh_pc` collision-triangle source the prior
// clmesh_pc merge built (ClmeshTriangleCollisionWorld's own
// CollisionBvh/CollisionTriangle, tools/sr3_viewer.cpp) - not a second
// triangle decode, just a second query SHAPE (ray or sphere) over the
// identical data.

// Closest point on triangle (A,B,C) to point P - the standard Voronoi-
// region decomposition (Ericson, "Real-Time Collision Detection" Sec5.1.5 -
// a textbook public algorithm, not a disassembly of anything). Exposed,
// same "pure geometry helper, independently testable" discipline as every
// other primitive in this file; also used internally by
// SweptSphereVsTriangle3D's own "already overlapping at t=0" check below.
Vec3 ClosestPointOnTriangle3D(double px, double py, double pz, double ax, double ay, double az, double bx,
                               double by, double bz, double cx, double cy, double cz);

// Real 3D ray(segment)-vs-triangle intersection (Moeller-Trumbore) - AI
// line-of-sight's own geometry primitive (spec-ai-runtime.md Sec12.4 /
// spec-physics-runtime.md Sec8.11's real resolver 0x004eda70, the kind==0
// closest-ray-hit-collector chain). `from`(x0,y0,z0)/`to`(x1,y1,z1) span
// the WHOLE tested segment (an AI's eye position to a target position) -
// outF is a FRACTION along [from,to] in [0,1], the same convention every
// other sweep function in this file uses, not a raw distance. outNormal
// faces back toward `from` (the raw face normal is flipped if it would
// otherwise point along the segment's own direction). Unlike the sweep
// function below, a ray has no "already inside" case to special-case (a
// zero-radius probe has no volume to already overlap) - a hit at outF==0
// (origin exactly on the triangle) is reported like any other hit.
bool RayVsTriangle3D(double x0, double y0, double z0, double x1, double y1, double z1, double ax, double ay,
                      double az, double bx, double by, double bz, double cx, double cy, double cz, double& outF,
                      Vec3& outNormal);

// Real swept-sphere-vs-triangle test (radius `radius`, centre moving
// (x0,y0,z0) -> (x1,y1,z1)) - camera collision's own geometry primitive
// (spec-physics-runtime.md Sec8.8's shape-cast wrapper 0x0075c880, "builds
// an hkpSphereShape"; Sec8.11 "Camera collision - CONFIRMED, through the
// shape-cast wrapper"). CHOSEN exact closed-form algorithm: the standard
// face + 3-edges + 3-vertices decomposition of a swept sphere against a
// static triangle (equivalent to a zero-radius sweep against the
// triangle's own Minkowski sum with a sphere of radius `radius` - two
// offset half-planes clipped to the triangle's footprint for the face, a
// capsule/cylinder test per edge, a sphere test per vertex; the earliest of
// the up to 7 candidates wins). "Already touching/overlapping the triangle
// at t=0" is CHOSEN to report "no new contact" (returns false, outF/
// outNormal untouched) - the SAME convention SweepCircleVsAabbXZ /
// SweepCircleVsSegmentXZ / SweepCircleVsTriangleXZ above already use for
// exactly the same reason (a degenerate zero-distance hit has no
// well-defined face to slide along, or in the camera's case, to push the
// eye away from).
bool SweptSphereVsTriangle3D(double x0, double y0, double z0, double x1, double y1, double z1, double radius,
                              double ax, double ay, double az, double bx, double by, double bz, double cx,
                              double cy, double cz, double& outF, Vec3& outNormal);

// Drives sr3physics::CharacterProxyStepper + CharacterProxyListener for one
// character each frame, against an injected ICollisionWorld. Construct one
// instance per character (MyCharacterListener is itself a per-proxy object,
// spec-player-runtime.md Sec5.6.1, so one driver/listener per human matches
// that shape).
class CharacterProxyDriver {
public:
    // radius/height/groundY: see this header's own top comment on why these
    // are ordinary placeholders, not recovered values.
    explicit CharacterProxyDriver(const ICollisionWorld& world, double radius = 0.4, double height = 1.8,
                                   double groundY = 0.0);

    // One frame: integrates gravity on the vertical axis (grounded binary +
    // the CONFIRMED -9.8 constant, see this header's top comment), then
    // drives the REAL up-to-10-pass lock-step over the horizontal axes,
    // sliding along any wall contact the pass loop's SolvePass reports
    // through `world` and queuing it through the listener before the next
    // pass. `desiredVelocity` is the same per-frame velocity
    // PlayerMovement::composeVelocity() already produces (+0x1cec) - this
    // function consumes it and publishes position/velocity exactly as
    // Sec5.11 item 5 licenses a replacement integrator to do.
    CapsuleStepResult step(const Vec3& position, const Vec3& desiredVelocity, double dt);

    // Updates the flat ground-plane constant step() integrates/snaps
    // against (2026-10-08 live-playtest ground-following follow-up task).
    // ADDITIVE ONLY: every pre-existing caller that never calls this keeps
    // the EXACT constructor-supplied groundY_ for the whole run, byte for
    // byte - this is purely a way for a caller that now HAS a per-frame
    // real ground sample (sr3zone::SampleHeightmapHeight(), sr3zone/
    // zone_heightmap.h) to update the target this driver already uses,
    // instead of forcing a second constant for the whole run. The caller
    // is expected to smooth/cap how much `groundY` itself moves between
    // calls (sr3player/ground_follow.h's own GroundFollower) - this
    // setter applies whatever value it is given immediately, with no
    // smoothing of its own, exactly like the constructor argument it
    // updates.
    void setGroundY(double groundY) { groundY_ = groundY; }
    double groundY() const { return groundY_; }

private:
    const ICollisionWorld& world_;
    double radius_;
    double height_;
    double groundY_;
    sr3physics::CharacterProxyStepper stepper_;
};

}  // namespace sr3player
