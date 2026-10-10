#pragma once

// sr3airuntime/near_field_vehicle_population.h - the real near-field
// (fully-simulated) ambient-vehicle maintenance system: spec-ai-runtime.md
// Sec17.4c, "Near-field (fully-simulated) ambient-vehicle population"
// (2026-10-08).
//
// RELATIONSHIP TO ambient_population.h (the DISTANT/LOD tier, Sec17.2-17.6).
// Sec17.4c itself is explicit this is "a second, structurally separate
// ambient-vehicle maintenance system", driven from the SAME per-frame
// dispatcher (0x0090faf0, Sec17.5) as the distant tier but via a
// previously-undocumented sibling call (0x0090f2c0), with its OWN live-
// entity list (0x026093ac here vs. 0x0130f434/0x0130f51c distant), its OWN
// bootstrap chain (0x00908fb0->0x009069e0 here vs. 0x00b90900/0x00b92e70
// distant), and NO shared-pointer traffic found between them.
// Sec17.4c's own "honest negative result": no code evidence of an explicit
// same-entity hand-off between this tier and the distant tier was found -
// each tier most plausibly maintains its own population band
// independently, with no object continuity across the switch. This file
// therefore does NOT model any promotion/demotion path, and deliberately
// does NOT share a live-entity list, a cap, or a budget counter with
// ambient_population.h - NearFieldVehicle/the counters below are their own
// type/constants, not a tier flag bolted onto LiveVehicle. ambient_population.h's
// own file header previously listed this tier's spawn-candidate algorithm
// and despawn-schedule tick as "OPEN, NOT IMPLEMENTED HERE" (Sec17.7's OPEN
// list as it stood before this pass) - this file is that follow-up unit,
// cross-referenced from there.
//
// WHAT'S REUSED, NOT REINVENTED (this task's own explicit instruction):
//   - sr3airuntime::DeterministicRng / MakePickUniform (navpoint_selector.h):
//     the SAME shared uniform-random-pick primitive (0x00dab660) Sec17.4c
//     names as this tier's own spawn final-pick mechanism - "its fourth
//     confirmed consumer" per that section's own words (after navpoint
//     selection, ped type-slot pick, vehicle slot pick).
//   - sr3airuntime::EngineTimer (common.h): Sec17.5's correction trail ties
//     0x00d9e180/0x00d9e1e0/0x00d9e250/0x00d9e4c0 together as one generic
//     countdown-timer family running on a wraparound global clock -
//     EngineTimer already models the "armed+expired" half of that family
//     (0x00d9e1e0's own test) for every other sr3airuntime unit; this file
//     reuses it as-is rather than adding a second timer type.
//   - the 0x00d9e180(timerSlot, min, max) randomized-arm call itself: this
//     project's existing idiom for injecting it is
//     ModeMachine::randomRangeMs (include/sr3airuntime/mode_table.h),
//     `std::function<int64_t(int64_t,int64_t)>` returning a value in
//     [lo,hi] - documented there as "the engine's own RNG, not specified
//     by this spec beyond its range... hosts that need real randomness
//     inject it here; the default is the deterministic midpoint... NOT a
//     recovered engine value". This file reuses the SAME seam shape
//     (NearFieldVehicleDriver::randomRangeMs_ below) for the two
//     independent randomized timers Sec17.4c names (200-300ms maintenance,
//     400-600ms despawn-schedule) rather than inventing a second
//     randomization idiom.
//   - ambient_population.h's AmbientToggleState/ToggleCategory::kVehicle
//     ("os_vehicle", already registered by the SAME bootstrap Sec17.5
//     documents): Sec17.4c states the maintenance pass is gated on this
//     EXACT category being enabled - reused directly (this file includes
//     ambient_population.h for it), not re-declared.
//
// CONFIRMED vs CHOSEN vs OPEN - same discipline ambient_population.h's own
// file header applies, and spec-ai-runtime.md's own top-of-file confidence
// key.
//
// CONFIRMED, ported directly (Sec17.4c):
//   - the hard cap, 48 concurrently-live near-field vehicles
//     (kNearFieldVehicleCap, @ 0x02609120 < 0x30, gate 0x0090ee10);
//   - the shared headroom ceiling, 150 eligible AI humans
//     (kHumanEligibilityCeiling, 0x009086d0: returns 150-count, spawn
//     proceeds only if positive) - HasHumanHeadroomForSpawn() below;
//   - the per-vehicle despawn-eligibility predicate (0x00906b00): NOT
//     force-no-spawn-blocked (0x008addb0 - confirmed, per Sec17.4c's own
//     side-finding, a GENERIC ambient-actor flag, not human-exclusive
//     despite spec-lua-api-behaviour.md Sec3.6/Sec6.10/spec-lua-api-
//     behaviour.md Sec19.4's human-only framing), AND none of its 8 seats
//     holds a flagged-important occupant (0x00ab5070/0x0097e280 loop seats
//     0-7), AND (only when in the player's own world-cell) farther than
//     player_vehicle_despawn_distance from the player, compared SQUARED
//     (no sqrt) - IsNearFieldVehicleDespawnEligible() below;
//   - the dual-timer maintenance cadence: a randomized 200-300ms timer
//     gates the whole pass (also requires the "os_vehicle" toggle category
//     enabled and the Traffic zone-data singleton loaded), a SEPARATE
//     randomized 400-600ms timer gates the despawn-schedule sub-step -
//     NearFieldVehicleDriver below;
//   - the spawn final-pick shape: a 3-candidate-slot buffer, final pick via
//     the shared uniform-random-pick primitive - SelectNearFieldVehicleSpawn
//     below.
//
// CHOSEN - the exact point of each call is documented in place, collected
// here for visibility:
//   1. Candidate GENERATION for the 3-slot spawn buffer. Sec17.4c's own
//      call-graph sketch names the spawn section only as "(own body)
//      spawn-candidate section: 3-slot buffer, shared uniform-random-pick" -
//      WHERE the up-to-3 candidate positions come from is not stated
//      (Sec17.7's own OPEN note on "0x0090f060, 0x0090ef90, 0x00906c40
//      bookkeeping siblings (not fully resolved)" most plausibly covers
//      this). Nothing in Sec17.4c suggests a candidate SOURCE structurally
//      different from the distant tier's own road-graph gather - only a
//      different final-pick buffer size (3, not an evenly-spaced/occupancy-
//      sorted slot search) and population target (48, not 75/20-per-pass).
//      This file therefore does NOT invent a second graph-walk: callers
//      supply up to kNearFieldSpawnCandidateSlots (3) pre-filtered
//      candidate positions (e.g. via ambient_population.h's own
//      GatherTrafficCandidates over the SAME Traffic graph, sampled down to
//      3) and SelectNearFieldVehicleSpawn's own job is ONLY the CONFIRMED
//      final-pick step.
//   2. The despawn-SCHEDULE tick's bucket-threshold walk (0x0090eaf0,
//      beyond the per-vehicle eligibility PREDICATE above, which is fully
//      CONFIRMED and is this file's primary despawn surface). Sec17.4c's
//      own prose: "if the live count is below the smaller of the two
//      arrays' entry-0 num_cars threshold, cancels any pending despawn;
//      otherwise walks the out-of-view bucket list first..., falling back
//      to the in-view list, each gated by its own abandon_despawn_delay-
//      derived timer" - EvaluateDespawnScheduleGate() below is a literal,
//      line-by-line port of exactly that paraphrase (cancel-threshold
//      check, out-of-view-then-in-view walk, picking the tightest bucket
//      whose num_cars threshold the live count satisfies), not a guess at
//      numbers beyond it. It is kept SEPARATE from, and not required by,
//      the per-vehicle eligibility predicate or the 4 proof scenarios this
//      unit's own test/demo coverage targets - a secondary, best-effort
//      convenience for a caller who wants the full pacing behaviour, not
//      load-bearing for anything this file asserts as CONFIRMED.
//   3. Spawn RATE (how many spawn attempts per maintenance-pass tick).
//      Sec17.4c: "Spawn rate is a dynamically-computed value (0x00910940),
//      scaled by a camera/view-fraction term and clamped by two engine
//      constants - not a fixed literal" - explicitly NOT a pinned number.
//      CHOSEN: this file's own RunNearFieldVehicleMaintenancePass attempts
//      at most ONE spawn per tick (the simplest faithful default consistent
//      with "not a fixed literal" - this file does not invent the camera/
//      view-fraction formula or its two clamp constants).
//   4. A freshly-spawned vehicle's own inPlayerWorldCell value. Sec17.4c
//      never traces how a vehicle's world-cell membership gets assigned at
//      spawn time. CHOSEN (found and fixed via this unit's own demo tool,
//      tools/near_field_vehicle_demo.cpp, which caught the live population
//      stalling near 1-2 vehicles instead of filling toward 48 - exactly
//      the kind of defect this project's measurement discipline exists to
//      catch): RunNearFieldVehicleMaintenancePass sets a newly spawned
//      vehicle's inPlayerWorldCell to true, not the bare type default
//      (false). A vehicle this tier just spawned came from the near-field
//      candidate buffer, itself gathered within this tier's own spawn
//      radius of the player/camera - by construction, that IS the player's
//      own world-cell. Leaving the bare default (false) would make
//      IsNearFieldVehicleDespawnEligible() treat every fresh spawn as
//      despawn-eligible on its very next despawn-schedule tick regardless
//      of distance (CHOSEN item on that predicate: "the gate does not apply
//      outside the player's cell" is not a protection, it is "distance
//      doesn't veto") - the live population would never grow past a
//      spawn/despawn churn of 1-2, contradicting the whole point of a
//      48-vehicle cap. See testSpawnedVehicleIsInPlayerWorldCellAndSurvivesADespawnTick
//      (tests/synthetic_near_field_vehicle_population_test.cpp) for the
//      regression lock.
//
// OPEN, NOT IMPLEMENTED HERE: the distant-to-near promotion/demotion
// boundary (Sec17.4c's own honest negative, see this file's own top note -
// correctly NOT implemented, not merely unimplemented); the exact
// candidate-generation algorithm behind the 3-slot buffer (CHOSEN item 1
// above substitutes the distant tier's own graph gather rather than
// guessing a near-field-specific one); the camera/view-fraction spawn-rate
// formula (CHOSEN item 3).

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "sr3airuntime/ambient_population.h"  // AmbientToggleState / ToggleCategory::kVehicle ("os_vehicle") - reused, not redeclared
#include "sr3airuntime/common.h"              // Vec3, DistanceSq, EngineTimer
#include "sr3airuntime/navpoint_selector.h"    // DeterministicRng + the pickUniform seam idiom - the SAME shared uniform-random-pick helper (0x00dab660)

namespace sr3airuntime {

// ===========================================================================
// Sec17.4c: concrete population numbers. CONFIRMED.
// ===========================================================================
inline constexpr int kNearFieldVehicleCap = 48;         // @ 0x02609120 < 0x30 (48), gate 0x0090ee10; smaller than the distant tier's flat 75 - "consistent with full physics/AI simulation being more expensive than LOD rendering" (Sec17.4c's own words)
inline constexpr int kHumanEligibilityCeiling = 150;    // 0x009086d0: 150 - (eligible human count); spawn proceeds only if positive
inline constexpr size_t kNearFieldSpawnCandidateSlots = 3;  // "a 3-candidate-slot buffer" (Sec17.4c)
inline constexpr size_t kNearFieldSeatCount = 8;         // 0x00ab5070/0x0097e280 loop over seats 0-7

inline bool HasNearFieldVehicleCapacity(size_t liveCount) {
    return liveCount < static_cast<size_t>(kNearFieldVehicleCap);
}

// CONFIRMED (Sec17.4c: "0x009086d0 ... returns 150 - count; the spawn pass
// only proceeds if this is positive"). This file does not itself track a
// live-human roster - that is a different subsystem's own data (this
// task's own brief: "a simple counter satisfies the spec, don't overbuild
// this"); callers supply the CURRENT eligible-human count however they
// already track it.
inline int RemainingHumanHeadroom(int eligibleHumanCount) { return kHumanEligibilityCeiling - eligibleHumanCount; }
inline bool HasHumanHeadroomForSpawn(int eligibleHumanCount) { return RemainingHumanHeadroom(eligibleHumanCount) > 0; }

// ===========================================================================
// Sec17.4c: a near-field vehicle, reduced to exactly what the despawn-
// eligibility predicate (0x00906b00) and this file's own maintenance pass
// read. A genuinely SEPARATE type from ambient_population.h::LiveVehicle -
// see this file's own top comment on why (two independent pools, no
// hand-off).
// ===========================================================================
struct NearFieldVehicle {
    uint64_t id = 0;
    Vec3 pos;
    // 0x008addb0 - a generic ambient-actor eligibility flag (Sec17.4c's own
    // side-finding: confirmed NOT human-exclusive, both 0x00906b00 and
    // 0x0090eaf0 call it identically on vehicle handles).
    bool forceNoSpawnBlocked = false;
    // 0x00ab5070/0x0097e280's own per-seat loop, seats 0-7.
    std::array<bool, kNearFieldSeatCount> seatHasImportantOccupant{};
    // Gates the distance half of the eligibility test (Sec17.4c: "only when
    // the vehicle is in the player's own world-cell").
    bool inPlayerWorldCell = false;
};

inline bool AnySeatHasImportantOccupant(const std::array<bool, kNearFieldSeatCount>& seats) {
    for (bool occupied : seats) {
        if (occupied) return true;
    }
    return false;
}

// ===========================================================================
// Sec17.4c: the per-vehicle despawn-eligibility predicate, CONFIRMED
// (0x00906b00). A candidate is eligible ONLY IF (a) not force-no-spawn-
// blocked, (b) none of its 8 seats holds a flagged-important occupant, AND
// (c) - only when the vehicle is in the player's own world-cell - farther
// from the player than player_vehicle_despawn_distance, compared SQUARED.
//
// CHOSEN reading of (c)'s own "only when" qualifier (not independently
// spelled out by Sec17.4c beyond that phrase): when the vehicle is NOT in
// the player's own world-cell, the distance half of the test simply does
// not apply (neither passes nor fails it) rather than forcing ineligibility
// outright - "only when X, test Y" read as "Y is one of the gates, but only
// under precondition X", the same way ambient_population.h's own CHOSEN
// item 6 documents a combination-rule judgment call for a comparably
// underspecified AND/OR wording. Documented here, not silently assumed.
inline bool IsNearFieldVehicleDespawnEligible(const NearFieldVehicle& v, const Vec3& playerPos,
                                               double playerVehicleDespawnDistanceSq) {
    if (v.forceNoSpawnBlocked) return false;
    if (AnySeatHasImportantOccupant(v.seatHasImportantOccupant)) return false;
    if (v.inPlayerWorldCell) {
        return DistanceSq(v.pos, playerPos) > playerVehicleDespawnDistanceSq;
    }
    return true;  // the distance gate does not apply outside the player's own world-cell (CHOSEN, see above)
}

// ===========================================================================
// Sec17.4c: spawn final-pick. CONFIRMED shape (3-slot buffer, shared
// uniform-random-pick final selection); candidate GENERATION is CHOSEN item
// 1 (file header) - callers supply up to kNearFieldSpawnCandidateSlots
// pre-filtered positions.
// ===========================================================================
struct NearFieldSpawnResult {
    bool found = false;
    Vec3 position;
};

inline NearFieldSpawnResult SelectNearFieldVehicleSpawn(const std::vector<Vec3>& candidateSlots,
                                                         const std::function<size_t(size_t)>& pickUniform) {
    NearFieldSpawnResult result;
    if (candidateSlots.empty()) return result;
    size_t idx = pickUniform(candidateSlots.size());
    if (idx >= candidateSlots.size()) idx = 0;  // defensive clamp, same discipline as every other injected pickUniform seam in this project
    result.position = candidateSlots[idx];
    result.found = true;
    return result;
}

// ===========================================================================
// Sec17.4c CHOSEN item 2 (file header): the despawn-SCHEDULE tick's
// bucket-threshold walk (0x0090eaf0), a literal port of Sec17.4c's own
// paraphrase. Secondary/best-effort - NOT required by, and kept separate
// from, the per-vehicle eligibility predicate above.
// ===========================================================================
struct NearFieldDespawnBucket {
    int numCars = 0;      // num_cars (spec-tables-traffic-ai.md Sec18)
    int64_t timeMs = 0;   // time, already seconds*1000->ms (engine's own conversion, Sec18)
};

struct NearFieldDespawnScheduleParams {
    std::vector<NearFieldDespawnBucket> inViewBuckets;     // in_view_despawn_info/abandon_despawn_info* (Sec18: up to 10 real entries fit)
    std::vector<NearFieldDespawnBucket> outOfViewBuckets;  // out_of_view_despawn_info/abandon_despawn_info* (Sec18)
    int64_t abandonDespawnDelayMs = 0;                     // abandon_despawn_delay, seconds*1000->ms (Sec18)
};

// Finds the tightest (highest numCars) bucket whose threshold the live
// count satisfies - CHOSEN tie-break for "shorter wait the more vehicles
// are live" (Sec17.4c): buckets are read in ascending-numCars file order
// (Sec18's own real sample, e.g. in-view {5 cars,30s}...{8 cars,0s}), so the
// LAST bucket whose numCars <= liveCount is the most-populated tier
// currently active, and later tiers carry shorter `time` values in every
// real sample this project has seen (Sec18's own §26.3 extraction).
inline const NearFieldDespawnBucket* FindActiveBucket(const std::vector<NearFieldDespawnBucket>& buckets,
                                                        int liveCount) {
    const NearFieldDespawnBucket* active = nullptr;
    for (const auto& b : buckets) {
        if (b.numCars <= liveCount) active = &b;
    }
    return active;
}

struct DespawnScheduleGateResult {
    bool pending = false;       // false = "cancels any pending despawn" (Sec17.4c)
    int64_t waitMs = 0;         // the active bucket's own wait time, if pending
    bool fromOutOfView = false;  // which list supplied the active bucket
};

// CONFIRMED cancel-check (Sec17.4c: "if the live near-field-vehicle count
// is below the smaller of the two arrays' entry-0 num_cars threshold, it
// cancels any pending despawn"); CHOSEN walk order beyond that (out-of-view
// first, falling back to in-view) per this file's own header note above.
inline DespawnScheduleGateResult EvaluateDespawnScheduleGate(const NearFieldDespawnScheduleParams& params,
                                                               int liveCount) {
    DespawnScheduleGateResult result;
    const bool haveIn = !params.inViewBuckets.empty();
    const bool haveOut = !params.outOfViewBuckets.empty();
    if (!haveIn && !haveOut) return result;

    const int inThreshold0 = haveIn ? params.inViewBuckets.front().numCars : params.outOfViewBuckets.front().numCars;
    const int outThreshold0 = haveOut ? params.outOfViewBuckets.front().numCars : params.inViewBuckets.front().numCars;
    const int cancelThreshold = std::min(inThreshold0, outThreshold0);
    if (liveCount < cancelThreshold) return result;  // "cancels any pending despawn"

    if (const NearFieldDespawnBucket* outBucket = FindActiveBucket(params.outOfViewBuckets, liveCount)) {
        result.pending = true;
        result.waitMs = outBucket->timeMs;
        result.fromOutOfView = true;
        return result;
    }
    if (const NearFieldDespawnBucket* inBucket = FindActiveBucket(params.inViewBuckets, liveCount)) {
        result.pending = true;
        result.waitMs = inBucket->timeMs;
        result.fromOutOfView = false;
    }
    return result;
}

// ===========================================================================
// Sec17.4c: the dual-timer maintenance cadence. Both randomized, both
// CONFIRMED ranges; both independent (one gates the whole pass, a SEPARATE
// one gates the despawn-schedule sub-step). Net cadence ~3-5x/second
// (Sec17.4c's own words), faster than the distant tier's ~1Hz heavy pass
// (ambient_population.h's AmbientPopulationDriver).
// ===========================================================================
class NearFieldVehicleDriver {
public:
    // randomRangeMs: the 0x00d9e180(lo,hi) seam, SAME idiom
    // ModeMachine::randomRangeMs (mode_table.h) already establishes
    // project-wide for this exact function. Default: deterministic
    // midpoint, same documented test-placeholder disclaimer as that file's
    // own default - NOT a recovered engine value.
    explicit NearFieldVehicleDriver(std::function<int64_t(int64_t, int64_t)> randomRangeMs =
                                         [](int64_t lo, int64_t hi) { return (lo + hi) / 2; })
        : randomRangeMs_(std::move(randomRangeMs)) {
        // Both timers start already-expired-at-time-0 (armed with duration
        // 0) so the first real tick's maintenance/despawn-schedule passes
        // run immediately - same CHOSEN "a freshly-booted population wants
        // to fill right away" reasoning ambient_population.h's own
        // AmbientPopulationDriver documents for its own countdowns.
        maintenanceTimer_.arm(0, 0);
        despawnScheduleTimer_.arm(0, 0);
    }

    // Sec17.4c: "the whole pass only runs when a randomized 200-300ms
    // timer... has expired, the 'os_vehicle' toggle category (Sec17.6) is
    // enabled, and the Traffic zone-data singleton is loaded
    // (0x00a69720())." zoneDataLoaded models that singleton check - a
    // caller-supplied fact (this file owns no zone-loading state, same
    // scope split every other sr3airuntime unit uses).
    bool ShouldRunMaintenancePass(int64_t nowMs, const AmbientToggleState& toggles, bool zoneDataLoaded) {
        if (!zoneDataLoaded) return false;
        if (!toggles.AnyEnabled(ToggleCategory::kVehicle)) return false;
        if (!maintenanceTimer_.expired(nowMs)) return false;
        maintenanceTimer_.arm(nowMs, randomRangeMs_(200, 300));
        return true;
    }

    // Sec17.4c: "the despawn-schedule sub-step is itself gated by a
    // second, independent randomized 400-600ms timer." Independent of
    // ShouldRunMaintenancePass()'s own gate above - a caller may check both
    // the same tick (both expiring together is possible, not mutually
    // exclusive the way the distant tier's shared latch is).
    bool ShouldRunDespawnSchedule(int64_t nowMs) {
        if (!despawnScheduleTimer_.expired(nowMs)) return false;
        despawnScheduleTimer_.arm(nowMs, randomRangeMs_(400, 600));
        return true;
    }

private:
    std::function<int64_t(int64_t, int64_t)> randomRangeMs_;
    EngineTimer maintenanceTimer_;
    EngineTimer despawnScheduleTimer_;
};

// ===========================================================================
// Sec17.4c: one combined maintenance pass - despawn (via the CONFIRMED
// per-vehicle eligibility predicate) then capped/headroom-gated spawn (via
// the CONFIRMED 3-slot final-pick). Operates on a caller-owned live list,
// deliberately NOT a global/static singleton - same "no hidden process-
// wide state" discipline ambient_population.h's own RunPedHeavyPass/
// RunVehicleHeavyPass already apply.
//
// `runDespawn`/`runSpawn`: the caller's own NearFieldVehicleDriver
// ShouldRunDespawnSchedule()/ShouldRunMaintenancePass() results for this
// tick - kept as explicit booleans here (rather than this function owning
// a driver itself) so a caller can run either half independently in a test
// without needing to fast-forward a real clock.
// ===========================================================================
struct NearFieldMaintenanceResult {
    int despawned = 0;
    int spawned = 0;
};

inline NearFieldMaintenanceResult RunNearFieldVehicleMaintenancePass(
    std::vector<NearFieldVehicle>& liveList, bool runDespawn, bool runSpawn, const Vec3& playerPos,
    double playerVehicleDespawnDistanceSq, int eligibleHumanCount, const std::vector<Vec3>& spawnCandidateSlots,
    const std::function<size_t(size_t)>& pickUniform, uint64_t& nextIdInOut) {
    NearFieldMaintenanceResult result;

    if (runDespawn) {
        const size_t before = liveList.size();
        liveList.erase(std::remove_if(liveList.begin(), liveList.end(),
                                       [&](const NearFieldVehicle& v) {
                                           return IsNearFieldVehicleDespawnEligible(v, playerPos,
                                                                                     playerVehicleDespawnDistanceSq);
                                       }),
                       liveList.end());
        result.despawned = static_cast<int>(before - liveList.size());
    }

    if (runSpawn) {
        // Sec17.4c CHOSEN item 3 (file header): at most one spawn attempt
        // per tick - the real spawn RATE (camera/view-fraction scaled,
        // clamp-constant bounded) is explicitly OPEN, not a fixed literal.
        if (HasNearFieldVehicleCapacity(liveList.size()) && HasHumanHeadroomForSpawn(eligibleHumanCount)) {
            NearFieldSpawnResult r = SelectNearFieldVehicleSpawn(spawnCandidateSlots, pickUniform);
            if (r.found) {
                NearFieldVehicle nv;
                nv.id = nextIdInOut++;
                nv.pos = r.position;
                // CHOSEN (not independently stated by Sec17.4c, which never
                // traces a spawned vehicle's own world-cell assignment): a
                // vehicle this tier JUST spawned came from the near-field
                // candidate buffer, itself gathered within this tier's own
                // (comparatively short) spawn radius of the player/camera -
                // by construction, that is the player's own world-cell, not
                // a distant one. Defaulting a fresh spawn to
                // inPlayerWorldCell=false (the type's own bare default)
                // would make IsNearFieldVehicleDespawnEligible() above treat
                // it as eligible-for-despawn on the very next despawn-
                // schedule tick regardless of distance (the CHOSEN "gate
                // does not apply outside the player's cell" reading means
                // NOT-in-cell is NOT a protection - it is "distance doesn't
                // veto," no special-cased save for freshly spawned entities
                // either) - i.e. every spawn would be despawned almost
                // immediately, which contradicts the whole point of a
                // 48-vehicle LIVE population. Set true here so a freshly
                // spawned vehicle is actually distance-gated like every
                // other in-cell vehicle, same as the real system's own
                // vehicles would be while near the player.
                nv.inPlayerWorldCell = true;
                liveList.push_back(nv);
                result.spawned = 1;
            }
        }
    }

    return result;
}

}  // namespace sr3airuntime
