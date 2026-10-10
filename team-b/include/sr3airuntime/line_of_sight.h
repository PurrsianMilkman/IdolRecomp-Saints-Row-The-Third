#pragma once

// sr3airuntime/line_of_sight.h - the real AI line-of-sight raycast,
// spec-ai-runtime.md Sec12.4 / spec-physics-runtime.md Sec8.8-Sec8.11
// (unit Q-U1, 2026-10-08 castRay/shape-cast follow-up task).
//
// WHY THIS EXISTS. perception.h's own file comment (unit A2) explicitly
// scopes OUT "the vision line-of-sight service (0x004ed1e0/0x004ed550) or
// any 3-D maths/raycast" as a later unit's job, noting A4 (Sec12.4) later
// read both halves in full and "confirmed the underlying raycast execution
// itself is a physics-domain primitive; not folded in here". This file is
// that later unit's own deliverable: the real raycast primitive itself,
// matching the injectable-seam house pattern this project already uses for
// every other not-yet-real-engine subsystem (sr3player::ICollisionWorld /
// sr3player::CameraCollision, actions.h's ActionWorldFacts).
//
// THE REAL CALL CONTRACT (spec-physics-runtime.md Sec8.9/Sec8.11). AI
// line-of-sight's real resolver, `0x004eda70`, "reads the two query
// positions/handles for a pending node... calls 0x0075d7e0 directly with a
// zero-initialized input (so it runs the kind==0/closest-collector branch
// ... the exact chain Sec8.6 already confirmed for the AI 'line of fire'
// probe), takes the boolean result and flips bit 0 of the answer node's
// +0x1c byte" - i.e. CONFIRMED: a single closest-hit ray test, boolean
// blocked/clear, decoded into perception.h's own pflag::LosValid bit
// (0x400) perception.h already defines but never writes (A2's own stated
// scope boundary, above). Sec8.10's own practical port note: "an on-demand
// ray/shape query does not need to reproduce Havok's own broadphase at all
// if the port also reproduces the game's own broadphase-border
// interception first" - this file does that at the SIMPLEST possible
// level (a linear scan of a flat triangle list; a real integration can
// substitute a BVH-accelerated scan over the SAME triangle data without
// changing this file's own interface, exactly the way
// tools/sr3_viewer.cpp's ClmeshTriangleCollisionWorld already BVH-
// accelerates sr3player::ICollisionWorld without changing that interface
// either).
//
// NOT REPRODUCED HERE, STATED PLAINLY (narrower scope than Sec12.4's full
// text, a deliberate choice, not an oversight):
//   * The async/dedup/request-queue/scheduler apparatus Sec12.4's own text
//     spends most of its words on (0x004ecfc0/0x004ecf40/0x004ed320/
//     0x004ed550/0x004edc80 - "the entire async/dedup/scheduler apparatus
//     exists purely to throttle and cache calls into one shared wrapper,
//     not to avoid it"). That apparatus is a CACHING/SCHEDULING layer on
//     top of the raycast, not the raycast itself - this file implements
//     the thing being throttled (the real closest-hit ray test), not the
//     throttle. A future unit that wants the exact re-check-interval/
//     dedup-by-same-four-key-dwords timing should add it on top of
//     IsLineOfSightBlocked() below, the same "layer on top of a real
//     primitive" shape navpoint_selector.h's own DeterministicRng seam
//     already uses for its own non-reproduced engine internal (0x00dab660).
//   * The two-layer filter spec-physics-runtime.md Sec8.9 documents
//     (Havok's own world-level group filter, and the query-local
//     caller-mode-byte filter 0x0075b780 that excludes three independently
//     selectable named-region categories via a resolved object's +0x14c
//     flag field). Both filters exist to exclude CHARACTERS/VEHICLES/
//     named-region objects from blocking a query - this file's own real
//     geometry source (reused from the clmesh_pc collision merge,
//     ClmeshTriangleCollisionWorld's CollisionBvh/CollisionTriangle,
//     tools/sr3_viewer.cpp) contains ONLY static props; there is no
//     character, vehicle, or named-region object in that triangle soup for
//     either filter layer to ever need to exclude. Adding an unused
//     filter-mode-byte parameter with nothing to filter would be
//     speculative plumbing, not a port of anything exercised - reported
//     here as an honest, deliberate divergence rather than silently
//     matched, exactly as this task's own brief asked for.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3player/character_collision.h"  // sr3player::RayVsTriangle3D - the SAME shared 3D ray
                                              // primitive camera collision's own RealCameraCollision
                                              // (tools/sr3_viewer.cpp) uses, reused here rather than
                                              // re-implemented (this task's own "shared, reusable
                                              // primitive" instruction). sr3airuntime links sr3player
                                              // for exactly this (CMakeLists.txt, this task's own addition).

namespace sr3airuntime {

// One real collision triangle, world-space - the same shape as
// tools/sr3_viewer.cpp's own CollisionTriangle (independently declared
// here since that struct is file-local to the viewer tool, not a header;
// see this file's own doc comment above for why that is NOT a second
// decode of `.clmesh_pc`, just a second struct naming the identical
// triangle data a real integration copies in).
struct LosTriangle {
    Vec3 a, b, c;
};

// The result of a blocked closest-hit query - the hit position/normal,
// exposed for diagnostics/tests even though AI code itself (per Sec12.4's
// own decode) only ever reads the boolean blocked/clear bit.
struct LosRayHit {
    Vec3 position;
    Vec3 normal;
};

// The injectable seam (same house pattern as sr3player::ICollisionWorld /
// sr3player::CameraCollision): AI code queries this once per line-of-sight
// check instead of calling a global.
class ILineOfSightWorld {
public:
    virtual ~ILineOfSightWorld() = default;
    // Closest-hit ray test from `from` to `to` (spec-physics-runtime.md
    // Sec8.9's castRay(world, rayInput, collector) contract, kind==0
    // branch). Returns true (blocked) and fills `outHit` (if non-null) with
    // the first obstruction; false (clear) if nothing lies between the two
    // points.
    virtual bool castRay(const Vec3& from, const Vec3& to, LosRayHit* outHit) const = 0;
};

// CHOSEN opt-in for a world with no geometry at all - every ray is clear.
// Same role as sr3player::EmptyCollisionWorld / NoHitCameraCollision: a
// caller that has not wired real geometry in yet (or a test exercising
// "what if LOS is never blocked") gets an explicit, labelled stand-in
// instead of a null-world special case.
class NoHitLineOfSightWorld : public ILineOfSightWorld {
public:
    bool castRay(const Vec3&, const Vec3&, LosRayHit*) const override { return false; }
};

// Pure geometry helper, exposed so it is independently testable (same
// discipline as every pure function in character_collision.h): closest-hit
// ray test against a flat triangle soup, via sr3player::RayVsTriangle3D
// (the shared 3D primitive - see this file's own top doc comment). A
// linear scan is the simplest possible reproduction of Sec8.10's own port
// note ("does not need Havok's own broadphase if the port reproduces the
// game's own broadphase-border interception first"); a real integration
// over hundreds of props should accelerate this with the SAME per-prop BVH
// ClmeshTriangleCollisionWorld already builds, without changing this
// function's own contract (triangles in, closest hit out).
inline bool ClosestHitRayVsTriangles(const Vec3& from, const Vec3& to, const std::vector<LosTriangle>& triangles,
                                      LosRayHit* outHit) {
    double bestF = 1.0;
    sr3player::Vec3 bestNormal{};
    bool found = false;
    for (const LosTriangle& t : triangles) {
        double f;
        sr3player::Vec3 n;
        if (sr3player::RayVsTriangle3D(from.x, from.y, from.z, to.x, to.y, to.z, t.a.x, t.a.y, t.a.z, t.b.x, t.b.y,
                                        t.b.z, t.c.x, t.c.y, t.c.z, f, n) &&
            f < bestF) {
            bestF = f;
            bestNormal = n;
            found = true;
        }
    }
    if (!found) return false;
    if (outHit) {
        outHit->position = from + (to - from) * bestF;
        outHit->normal = Vec3{bestNormal.x, bestNormal.y, bestNormal.z};
    }
    return true;
}

// The real implementation of ILineOfSightWorld over a flat triangle list -
// the SAME real `.clmesh_pc` collision-triangle source the character-proxy
// collision merge built (see this file's own top doc comment), just handed
// in as a flat std::vector rather than tools/sr3_viewer.cpp's own per-prop
// CollisionBvh (that acceleration structure is a tool-local type, not part
// of any header - a caller that already has one should scan it directly
// with sr3player::RayVsTriangle3D per leaf triangle, the same way this
// class does per-element here, rather than flattening into this class
// first).
class RealTriangleSoupLineOfSightWorld : public ILineOfSightWorld {
public:
    explicit RealTriangleSoupLineOfSightWorld(std::vector<LosTriangle> triangles) : triangles_(std::move(triangles)) {}

    bool castRay(const Vec3& from, const Vec3& to, LosRayHit* outHit) const override {
        return ClosestHitRayVsTriangles(from, to, triangles_, outHit);
    }

private:
    std::vector<LosTriangle> triangles_;
};

// The real higher-level query AI code calls (spec-ai-runtime.md Sec12.4's
// own decode: "2 = visible (bit clear) or 3 = blocked (bit set)" - this
// function returns the same boolean the real `+0x1c` bit 0 encodes, true
// meaning blocked/3, false meaning visible/2). Named for what it answers,
// not for the mechanism (castRay), matching perception.h's own
// targetLineOfSightValidOrNone naming convention.
inline bool IsLineOfSightBlocked(const ILineOfSightWorld& world, const Vec3& from, const Vec3& to) {
    return world.castRay(from, to, nullptr);
}

}  // namespace sr3airuntime
