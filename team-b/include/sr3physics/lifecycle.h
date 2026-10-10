#pragma once

// sr3physics/lifecycle.h - the bring-up and shutdown ORDER contract, Sec4.1
// ("Bring-up") and Sec4.5 ("Shutdown") of spec-physics-runtime.md, SPEC
// TEAM (cleanroom reverse-engineering process) - the ONLY source for every
// ordering below.
//
// WHAT IS IMPLEMENTED: the ORDER a replacement must run its own bring-up
// and shutdown hooks in, plus the guard-byte discipline each end has
// (bring-up refuses a second call; shutdown does nothing unless bring-up's
// guard is set, Sec4.1/Sec4.5 preambles). This is the part of the lifecycle
// this unit's scope covers (frame contract and init/shutdown ORDER); the
// granular internal steps of bring-up (world-cinfo fields, the collision-
// filter chain, the two query phantoms' exact construction, and so on) are
// per-unit material (P2-P5) and are grouped here into six ordered PHASES
// coarse enough to not assert anything about their own internals - a port
// fills each phase's hook with whatever its own equivalent work is (or
// leaves it a no-op against a null backend, which the task explicitly
// requires this reproduce the order even then).
//
// Shutdown's order is the one both this file and spec-physics-runtime.md
// Sec4.5 state most precisely (CONFIRMED, all five lifecycle functions read
// in full): game pool -> phantoms -> listeners -> job queue -> world ->
// utility -> per-thread memory -> base system -> clear guard byte. "The job
// queue is destroyed before the world" (Sec4.5's own explicit emphasis,
// repeated in its order-contract line) is reproduced as job queue (step 5)
// strictly before world (step 6).
//
// WHAT IS NOT IMPLEMENTED / OPEN:
//   * Sec6 OPEN item 1 (where the constraint filter's listener sub-object
//     is added - shutdown removes it, Sec4.5 step 3, but no bring-up body
//     adds it; "it may happen inside Havok's own world constructor") - has
//     no effect on the ORDER this file models, since it is scoped to
//     Havok's own internals, not a game-side hook boundary; not represented
//     by a hook here.
//   * Sec6 OPEN item 2 (construction-info field names) - field-naming only,
//     no effect on order; not represented.
//   * Real Havok memory/base-system bring-up, the real collision-filter
//     chain, the real body tables, the real two query phantoms (Scope
//     discipline: this is the ORDER contract, not a Havok re-implementation).

#include <functional>

namespace sr3physics {

// Bring-up (Sec4.1) and shutdown (Sec4.5), as six/nine ordered, optional
// hooks plus the guard-byte discipline (0x0161d39f, Sec4.1/Sec4.5
// preambles: bring-up refuses a second call; shutdown is a no-op unless
// bring-up's guard is set).
class PhysicsLifecycle {
public:
    // --- bring-up (Sec4.1), order only ----------------------------------
    std::function<void()> bringUpMemoryAndBaseSystem;       // Sec4.1 step 1
    std::function<void()> bringUpWorld;                      // Sec4.1 steps 4-9 (world-cinfo, filter chain, the world itself)
    std::function<void()> bringUpJobQueue;                   // Sec4.1 step 10
    std::function<void()> bringUpBorderAndUtility;           // Sec4.1 steps 11, 14 (broadphase border, suspend-inactive-agents utility)
    std::function<void()> bringUpBodyTables;                 // Sec4.1 step 12 (the 5,000-slot body table)
    std::function<void()> bringUpPerThreadMemoryAndPhantoms;  // Sec4.1 step 15 (per-thread memory, watchdog, the two query phantoms, water)

    // Runs the six hooks above, in that order, each optional. A second call
    // while already brought up is a no-op (Sec4.1 preamble: the guard byte
    // is "checked first by bring-up to refuse a second call").
    void bringUp();

    // --- shutdown (Sec4.5), CONFIRMED order -----------------------------
    std::function<void()> shutdownGamePool;        // Sec4.5 step 1: every pooled body object released
    std::function<void()> shutdownPhantoms;         // Sec4.5 step 2: world removal of both query phantoms
    std::function<void()> shutdownListeners;        // Sec4.5 steps 3-4: constraint listener detached, border handler cleared
    std::function<void()> shutdownJobQueue;         // Sec4.5 step 5 - BEFORE the world (Sec4.5's own explicit emphasis)
    std::function<void()> shutdownWorld;             // Sec4.5 step 6: the world itself released
    std::function<void()> shutdownUtility;           // Sec4.5 step 7: suspend-inactive-agents utility released
    std::function<void()> shutdownPerThreadMemory;   // Sec4.5 step 8 (per-worker Havok memory torn down)
    std::function<void()> shutdownBaseSystem;         // Sec4.5 step 8 (Havok's own base system shut down)

    // Runs the eight hooks above, in that order, each optional, then clears
    // the guard byte (Sec4.5 step 9). Does nothing (no hook runs) unless
    // bringUp() has run and shutdown() has not already run since (Sec4.5
    // preamble: "does nothing unless bring-up's guard byte is set").
    void shutdown();

    bool isBroughtUp() const { return broughtUp_; }

private:
    bool broughtUp_ = false;
};

}  // namespace sr3physics
