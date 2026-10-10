#pragma once

// sr3airuntime/ambient_population.h - the real distant (LOD-cheap) ambient
// pedestrian/vehicle spawn-despawn system: spec-ai-runtime.md Sec17,
// "Ambient population runtime: distant pedestrian/vehicle spawn-despawn"
// (2026-10-08).
//
// SCOPE. Sec17 covers ONLY the distant/LOD population tier (the one driven
// by the flat 75-entity budgets and the per-frame dispatcher below) - the
// near-field, full-detail vehicle population's OWN spawn algorithm, and the
// distant<->near promotion/demotion boundary, are both explicitly OPEN in
// Sec17 itself (Sec17.3's closing paragraph, Sec17.7) and are NOT attempted
// here; see this file's own "OPEN, NOT IMPLEMENTED HERE" list at the bottom
// of this comment.
//
// CONFIRMED vs CHOSEN vs OPEN, same confidence key spec-ai-runtime.md's own
// top-of-file "Confidence key" paragraph defines, and the same discipline
// navpoint_selector.h/cover_node_selector.h already apply in this project:
// port the CONFIRMED shape exactly; where Sec17 names a quantity/mechanism
// but not its exact formula, label the choice CHOSEN at the point it's
// made rather than silently guess; where Sec17 flags something OPEN
// outright, don't implement it at all.
//
// CONFIRMED, ported directly (Sec17.2/17.3/17.4/17.5/17.6, full disassembly
// per that section's own confidence summary, Sec17.7):
//   - the flat global budgets (75 ped / 75 vehicle) and per-pass refill
//     caps (25 ped / 20 vehicle) - kPedPopulationTarget etc below;
//   - the spawn-radius-multiplier formula (ComputeSpawnRadius below) - the
//     exact dt-scaled, Base/Max-clamped pseudocode Sec17.2 quotes for the
//     ped tick (0x00b90960), HIGH CONFIDENCE for the vehicle tick
//     (0x00b93670, "structurally identical" per Sec17.2's own words, not
//     independently re-read to its own last instruction);
//   - camera-lookahead prediction (PredictCameraPosition below): predicted
//     position = current + velocity * Camera_Lookahead_Time;
//   - ped spawn selection's CONFIRMED shape: tally live occupancy across up
//     to 8 type-slots, then ONE weighted-random pick via the shared
//     uniform-random-pick helper (0x00dab660) - the SAME helper
//     navpoint_selector.h's SelectNavpointDestination already reuses
//     (Sec17.3's own words: "this is its third confirmed consumer");
//   - vehicle spawn selection's CONFIRMED shape, genuinely different from
//     ped's (Sec17.3's own words: "not two implementations of one shared
//     algorithm"): a Spline_Type one-hot-mask AND-test gates candidate road
//     segments, then an evenly-spaced, occupancy-filtered, qsort'd slot
//     search along the winning segment, final slot picked via the same
//     shared helper;
//   - despawn's CONFIRMED shape: a per-entity distance+angle cull against
//     the predicted camera position, once per maintenance pass, PLUS an
//     unconditional whole-category mass-despawn path (Sec17.4a/b);
//   - the 6 named 8-byte toggle-category arrays sharing one registration
//     primitive (Sec17.6, correcting spec-zone-data-format.md Sec13.4's
//     stale "0-18 slots" claim - this file implements against the
//     corrected 8-slot version only, per this task's own instruction not
//     to re-fix that spec file itself);
//   - the driver's CONFIRMED shape (Sec17.5): a per-frame dispatcher calls
//     both ticks unconditionally every frame (modulo the pause/loading
//     gate, a caller-level concern this header does not model - same scope
//     split as every other sr3airuntime unit); each tick's own expensive
//     work is throttled to roughly once per second behind a SHARED
//     one-shot-per-frame latch plus its own countdown, and the ped/vehicle
//     heavy passes mutually exclude each other within a single frame
//     (vehicle is asked first, matching Sec17.5's own real call order).
//
// CHOSEN - the exact point of each call is documented in place, not just
// listed here, but collected for visibility:
//   1. The 0x0130F4D8 field-identity question (Sec17.2's own OPEN item:
//      "either that field-to-offset mapping needs a second look, or
//      Double_Spawn_Height genuinely double-duties as the vehicle analogue
//      of the ped spacing constant"). CHOSEN interpretation, per this
//      task's own instruction to pick the reading the spec leans toward:
//      the runtime trace itself is the more concrete fact here - Sec17.2
//      states directly that 0x00b93670 uses 0x0130F4D8 "in the same role"
//      as the ped tick's own Length_Per_Spawn-reciprocal divisor,
//      structurally, not just by analogy. This file therefore models the
//      vehicle analogue of that field as
//      SpawnDespawnParams::lengthPerSpawnReciprocal regardless of which
//      XTBL element name currently maps to that address - the field's
//      RUNTIME ROLE (confirmed) is what this file ports, not the loader-
//      side name (still open, and none of this file's business to settle).
//   2. Ped type-slot WEIGHT formula (ComputePedTypeWeights) - Sec17.3 names
//      the shape ("population-weighted") but not the formula.
//   3. Vehicle graph-node (segment) selection reusing the SAME shared
//      uniform-pick helper as the final slot pick (SelectVehicleSpawn) -
//      Sec17.3 only explicitly confirms the helper's reuse for the slot
//      pick; reusing it again for the segment pick is this file's own
//      consistent extension.
//   4. The vehicle candidate distance-band's reference point (segment
//      midpoint, GatherTrafficCandidates) and its inner bound (caller-
//      supplied, no confirmed numeric default) - Sec17.3 says "[inner,
//      outer] distance band" without stating either value's source.
//   5. The slot-sort key (ascending position-along-segment,
//      SortedUnoccupiedSlots) - Sec17.3 says "qsort's the remaining valid
//      slots" without stating the sort key.
//   6. The despawn pass/fail COMBINATION rule (ShouldDespawnEntity: EITHER
//      the distance OR the angle test failing culls the entity) - Sec17.4
//      names both tests without stating how they combine.
//   7. The blocked heavy-pass's countdown semantics
//      (AmbientPopulationDriver::ShouldRunPedHeavyPass) - Sec17.5 says "the
//      other defers to its own next expiry". CHOSEN reading: the blocked
//      side's countdown is left AS-IS (still <= 0, i.e. still expired)
//      rather than re-armed to a full fresh period, so it runs on the very
//      next frame once the latch is free again, then re-arms to the full
//      period from that point - "its own next expiry" read as "the next
//      time the latch lets it through", not "wait a further full ~1s".
//      Traced through by hand before settling on this reading: the OTHER
//      candidate choice (re-arm the blocked side to a full fresh period
//      immediately) was tried first and rejected because, with both
//      countdowns using the identical period and both getting reset at the
//      same simulated instant, it makes the two heavy passes re-expire in
//      perpetual lockstep with vehicle (asked first, Sec17.5's own call
//      order) always winning the latch and the ped side never running at
//      all - a starvation artifact with no support in Sec17.5's own text
//      (which plainly expects BOTH to run, roughly once a second each).
//      This file's chosen reading produces a small, stable one-frame stagger
//      between the two passes instead, with neither starving.
//   8. The vehicle TYPE-selection policy before Spline_Type gating
//      (RunVehicleHeavyPass's round-robin over caller-supplied traffic
//      types) - Sec17.3 never names a weighting scheme for vehicle TYPE the
//      way it does for ped type slots.
//
// SYNTHETIC-ONLY ZONE BRIDGE, same idiom cover_node_selector.h's own
// CoverNode uses. This file does NOT parse real `.czn_pc` Traffic (0x2236)
// / Sidewalks (0x2238) content - a source survey before writing this file
// found no C++ reader for either content kind anywhere in this project yet
// (only sr3zone::zone_objects.h's 0x2234 Objects reader exists). Building
// that byte format from spec-zone-data-format.md Sec13.2's own prose
// description (14 count/pointer sub-array pairs, a segment->point graph,
// two position codecs - itself still partially OPEN on exact field
// semantics) would be a separate, substantial reverse-engineering/format
// unit of its own, not invented here. SidewalkNode/TrafficSegment below are
// reduced to exactly what Sec17.3's algorithms read (position; for a
// segment, its own endpoints and Spline_Type-compatible mask) - a caller
// (a future real Traffic/Sidewalks reader, or - as this file's own demo/
// golden scene does - a clearly-labelled stand-in graph) supplies them.
//
// THE RNG SEAM. Exactly navpoint_selector.h's own seam, reused rather than
// reinvented: `pickUniform` callables and sr3airuntime::DeterministicRng
// are NOT a bit-exact port of the untraced 0x00dab660 generator - the SHAPE
// of every pick here (uniform, or weighted-via-uniform) is CONFIRMED/CHOSEN
// as documented above; the specific SEQUENCE for a given seed is this
// project's own.
//
// OPEN, NOT IMPLEMENTED HERE (Sec17.7's own OPEN list): the distant-to-near
// promotion/demotion boundary - Sec17.4c (2026-10-08, see below) confirms
// this as an honest NEGATIVE result (no object hand-off exists between the
// two tiers), not merely unimplemented, and this file still does not model
// one. The near-field vehicle population's OWN spawn-candidate algorithm
// and despawn-schedule tick, previously listed here as OPEN, is now
// resolved by new Sec17.4c and implemented in the sibling file
// sr3airuntime/near_field_vehicle_population.h (added 2026-10-08) - a
// genuinely SEPARATE pool/module, per that section's own "structurally
// separate system" framing, not an extension of this file's own distant/LOD
// types. Spawn_Angle's
// exact role at spawn time (SpawnDespawnParams::spawnAngleCosThreshold is
// carried as data, matching Sec14.3's own schema, but Sec17.3's own traced
// pseudocode never wires it into the spawn-candidate filter the way
// despawnAngleCosThreshold is confirmed wired into despawn - so this file
// does not invent a symmetric spawn-cone test for it).

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3airuntime/navpoint_selector.h"  // reuses DeterministicRng + the pickUniform seam idiom - the SAME shared uniform-random-pick helper (0x00dab660) Sec17.3 names as this ped/vehicle code's own real primitive

namespace sr3airuntime {

// ===========================================================================
// Sec17.2: flat global budget/refill constants. CONFIRMED (clean single-
// reader/zero-writer xref census, Sec17.2's own words).
// ===========================================================================
inline constexpr int kPedPopulationTarget = 75;      // @ 0x0130f41c
inline constexpr int kVehiclePopulationTarget = 75;  // @ 0x0130f4d4
inline constexpr int kPedRefillCapPerPass = 25;       // 0x19 literal inside 0x00b90960
inline constexpr int kVehicleRefillCapPerPass = 20;   // @ 0x0130f4d0 (0x14)

// ===========================================================================
// Sec14.3/Sec17.2: spawn/despawn tuning parameters, already decoded the way
// this project's own distant_ped_spawn_parameters.xtbl/
// distant_vehicle_spawn_parameters.xtbl loader (src/tables_trafficai.cpp)
// produces them - angle fields are already-converted dot-product thresholds
// (cos(angle * k)); lengthPerSpawnReciprocal is already 1/value. This
// struct is the RUNTIME-SIDE shape Sec17.2's tick functions consume, not a
// second parser - a caller feeds it from that existing loader's own output.
// Defaults below are the documented pre-load image values (Sec14.3's own
// "image values before the file loads" table) for the PED side; a vehicle
// caller should supply the vehicle-side defaults/loaded values explicitly
// (see this struct's own per-field comment) rather than rely on these.
// ===========================================================================
struct SpawnDespawnParams {
    // Ped: real Length_Per_Spawn reciprocal (0x0130F400). Vehicle: the
    // field this file's own CHOSEN item 1 (file header) resolves - whatever
    // name currently maps to 0x0130F4D8, this is its RUNTIME role.
    double lengthPerSpawnReciprocal = 0.04;
    double baseSpawningDist = 200.0;
    double maxSpawningDist = 1200.0;
    double despawnExpansionDistance = 25.0;
    double cameraLookaheadTimeSeconds = 0.25;
    // Carried as data (Sec14.3's own schema) but NOT wired into spawn
    // filtering below - see this file's own "OPEN" note, file header.
    double spawnAngleCosThreshold = 0.8;
    // Sec17.4(a): the despawn cull's own angle threshold. Ped default 0.6
    // (~53 degree half-cone); a vehicle caller should pass -0.5 (~120
    // degrees, Sec14.3's own vehicle image value) instead of this default.
    double despawnAngleCosThreshold = 0.6;
};

// ===========================================================================
// Sec17.3: camera-motion-predicted future position. CONFIRMED.
// ===========================================================================
inline Vec3 PredictCameraPosition(const Vec3& currentPos, const Vec3& velocity, double lookaheadTimeSeconds) {
    return currentPos + velocity * lookaheadTimeSeconds;
}

// ===========================================================================
// Sec17.2: the dt-scaled, Base/Max-clamped spawn-radius-multiplier formula,
// CONFIRMED by decompile for the ped tick (0x00b90960), HIGH CONFIDENCE
// (structurally identical, per that section's own words) for the vehicle
// tick (0x00b93670). `randFloat01` is this file's own RNG seam (see file
// header) standing in for whatever random-float primitive the real tick
// calls at this exact point - not independently traced to its own address,
// same "SHAPE confirmed, SEQUENCE this project's own" discipline as every
// other injected seam here.
// ===========================================================================
struct SpawnRadiusResult {
    double spawnDist = 0;
    double despawnRadius = 0;
};

inline SpawnRadiusResult ComputeSpawnRadius(const SpawnDespawnParams& p, double frameDeltaTimeSeconds,
                                             double randFloat01) {
    double spawnMul;
    if (frameDeltaTimeSeconds > 0.0 && p.lengthPerSpawnReciprocal != 0.0) {
        spawnMul = frameDeltaTimeSeconds / p.lengthPerSpawnReciprocal + 1.0;
        if (p.maxSpawningDist < spawnMul * p.baseSpawningDist) {
            spawnMul = p.maxSpawningDist / p.baseSpawningDist;
        }
    } else {
        // Sec17.2's own branch ("else spawnMul = 1.0") - this file also
        // takes this branch defensively when lengthPerSpawnReciprocal==0 to
        // avoid a divide-by-zero the real pseudocode's own precondition
        // (dt>0) does not separately guard; not itself a disassembly-
        // confirmed branch condition, just a safe superset of it.
        spawnMul = 1.0;
    }
    double spawnDist = p.baseSpawningDist * randFloat01 * spawnMul;
    if (p.maxSpawningDist <= spawnDist) spawnDist = p.maxSpawningDist;

    SpawnRadiusResult r;
    r.spawnDist = spawnDist;
    r.despawnRadius = p.despawnExpansionDistance + spawnDist;
    return r;
}

// [0,1) via the shared DeterministicRng seam (navpoint_selector.h) - this
// file's own stand-in for ComputeSpawnRadius's `randFloat01` argument, not
// a port of 0x00dab660's own float-random variant (untraced).
inline double RandomUnitFloat01(DeterministicRng& rng) {
    constexpr size_t kBuckets = 1'000'000;
    return static_cast<double>(rng.next(kBuckets)) / static_cast<double>(kBuckets);
}

// ===========================================================================
// SYNTHETIC-ONLY candidate graph data (see file header). Reduced to exactly
// what Sec17.3's algorithms read.
// ===========================================================================
struct SidewalkNode {
    uint64_t id = 0;
    Vec3 pos;
};

struct TrafficSegment {
    uint64_t id = 0;
    Vec3 start, end;
    uint32_t splineTypeMask = 0;  // +0x1c (Sec17.3): one-hot-OR-able eligibility mask

    double Length() const { return Distance(start, end); }
    Vec3 PointAt(double t) const { return start + (end - start) * t; }  // t in [0,1]
};

// distant_vehicle_traffic_types.xtbl row (Sec14.4), as this file's own
// consumer needs it - a caller feeds this from that table's own existing
// loader (src/tables_trafficai.cpp), not re-parsed here.
struct VehicleTrafficType {
    uint32_t nameHash = 0;
    uint32_t splineTypeMask = 0;  // 1 << n one-hot (Sec14.4)
    double lengthPerSpawnReciprocal = 0;
    double noSpawnZone = 0;
    double dummyCarLength = 0;
};

struct LivePed {
    uint64_t id = 0;
    Vec3 pos;
    int typeSlot = -1;  // which of up to 8 type slots (Sec17.3)
};

struct LiveVehicle {
    uint64_t id = 0;
    Vec3 pos;
    uint32_t trafficTypeMask = 0;
};

// ===========================================================================
// Sec17.3: pedestrian spawn selection.
// ===========================================================================
inline constexpr size_t kMaxPedTypeSlots = 8;  // Sec17.3: "up to 8 type-slots"

// Gather candidate Sidewalks nodes within `maxDist` of the predicted
// position. Sec17.3 does not give an explicit annulus (inner bound) for the
// PED side the way it does for vehicles - a single upper-bound disc is this
// file's own documented reading (distinct from the vehicle side's explicit
// [inner, outer] band, GatherTrafficCandidates below).
inline std::vector<const SidewalkNode*> GatherSidewalkCandidates(const std::vector<SidewalkNode>& nodes,
                                                                  const Vec3& predictedPos, double maxDist) {
    std::vector<const SidewalkNode*> out;
    const double maxDistSq = maxDist * maxDist;
    for (const auto& n : nodes) {
        if (DistanceSq(n.pos, predictedPos) <= maxDistSq) out.push_back(&n);
    }
    return out;
}

// CHOSEN weight formula (file header item 2): inverse-occupancy, biased
// toward under-represented slots without ever reaching zero.
inline std::array<double, kMaxPedTypeSlots> ComputePedTypeWeights(
    const std::array<int, kMaxPedTypeSlots>& counts) {
    int maxCount = 0;
    for (int c : counts) maxCount = std::max(maxCount, c);
    std::array<double, kMaxPedTypeSlots> weights{};
    for (size_t i = 0; i < kMaxPedTypeSlots; ++i) weights[i] = static_cast<double>(maxCount - counts[i] + 1);
    return weights;
}

// Generic weighted-random index pick: cumulative-sum-then-uniform-pick-in-
// range, built on the SAME pickUniform seam idiom navpoint_selector.h
// establishes. Returns `weights.size()` (one past the end) if every weight
// is <= 0 - callers must check against that, same defensive-clamp
// discipline as SelectNavpointDestination's own safeIdx.
inline size_t PickWeightedIndex(const std::vector<double>& weights, const std::function<size_t(size_t)>& pickUniform) {
    double total = 0;
    for (double w : weights) total += std::max(0.0, w);
    if (total <= 0.0) return weights.size();

    constexpr size_t kResolution = 1'000'000;
    const size_t roll = pickUniform(kResolution);
    const double target = (static_cast<double>(roll) / static_cast<double>(kResolution)) * total;

    double running = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        running += std::max(0.0, weights[i]);
        if (target < running) return i;
    }
    return weights.size() - 1;  // float rounding at the tail - last nonzero-weight slot, not out of range
}

struct PedSpawnResult {
    bool found = false;
    Vec3 position;
    int typeSlot = -1;
};

// Full ped spawn-candidate pipeline (Sec17.3): a position pick over nearby
// Sidewalks nodes (CHOSEN uniform pick, file header item 3's sibling for
// the ped side - Sec17.3 does not independently confirm a node-position
// pick mechanism beyond "selected dynamically each pass"), plus the
// CONFIRMED-shape weighted type-slot pick.
inline PedSpawnResult SelectPedSpawn(const std::vector<SidewalkNode>& sidewalkNodes, const Vec3& predictedPos,
                                      double spawnDist, const std::array<int, kMaxPedTypeSlots>& typeOccupancyCounts,
                                      const std::function<size_t(size_t)>& pickUniform) {
    PedSpawnResult result;

    auto candidates = GatherSidewalkCandidates(sidewalkNodes, predictedPos, spawnDist);
    if (candidates.empty()) return result;

    size_t idx = pickUniform(candidates.size());
    if (idx >= candidates.size()) idx = 0;
    result.position = candidates[idx]->pos;

    auto weightsArr = ComputePedTypeWeights(typeOccupancyCounts);
    std::vector<double> weights(weightsArr.begin(), weightsArr.end());
    size_t slot = PickWeightedIndex(weights, pickUniform);
    if (slot >= weights.size()) return result;  // position found but no weighted slot - shouldn't happen (weights always >= 1), defensive only

    result.typeSlot = static_cast<int>(slot);
    result.found = true;
    return result;
}

// ===========================================================================
// Sec17.3: vehicle spawn selection - genuinely different shape from ped
// (file header's own note, quoting Sec17.3 directly).
// ===========================================================================
inline bool SplineTypeEligible(uint32_t segmentMask, uint32_t vehicleTypeMask) {
    return (segmentMask & vehicleTypeMask) != 0;  // CONFIRMED AND-test, Sec17.3
}

// CHOSEN distance-band reference point (segment midpoint) and inner bound
// (caller-supplied) - file header item 4.
inline std::vector<const TrafficSegment*> GatherTrafficCandidates(const std::vector<TrafficSegment>& segments,
                                                                   const Vec3& predictedPos, double innerDist,
                                                                   double outerDist, uint32_t vehicleTypeMask) {
    std::vector<const TrafficSegment*> out;
    const double innerSq = innerDist * innerDist;
    const double outerSq = outerDist * outerDist;
    for (const auto& seg : segments) {
        if (!SplineTypeEligible(seg.splineTypeMask, vehicleTypeMask)) continue;
        const Vec3 mid = seg.PointAt(0.5);
        const double dSq = DistanceSq(mid, predictedPos);
        if (dSq >= innerSq && dSq <= outerSq) out.push_back(&seg);
    }
    return out;
}

inline constexpr size_t kMaxVehicleSlotsPerSegment = 5;  // Sec17.3 CONFIRMED cap

struct VehicleSlot {
    double t = 0;  // position along the segment, [0,1]
    Vec3 pos;
    bool occupied = false;
};

// Sec17.3: "builds a byte array of evenly-spaced slots (segment length /
// slot count, capped at 5 slots)". CHOSEN placement rule (file header item
// 5's sibling): slot i sits at the centre of its own 1/slotCount share of
// the segment, (i+0.5)/slotCount - one internally-consistent "evenly
// spaced" reading; the spacing value itself is exposed separately by
// SegmentSlotSpacing() for callers/tests that want the named quantity
// Sec17.3's own wording cites directly.
inline std::vector<VehicleSlot> BuildSegmentSlots(const TrafficSegment& seg, size_t requestedSlotCount) {
    const size_t slotCount = std::min(kMaxVehicleSlotsPerSegment, std::max<size_t>(1, requestedSlotCount));
    std::vector<VehicleSlot> slots(slotCount);
    for (size_t i = 0; i < slotCount; ++i) {
        const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(slotCount);
        slots[i].t = t;
        slots[i].pos = seg.PointAt(t);
    }
    return slots;
}

inline double SegmentSlotSpacing(const TrafficSegment& seg, size_t slotCount) {
    slotCount = std::min(kMaxVehicleSlotsPerSegment, std::max<size_t>(1, slotCount));
    return seg.Length() / static_cast<double>(slotCount);
}

inline void MarkOccupiedSlots(std::vector<VehicleSlot>& slots, const std::vector<Vec3>& liveVehiclePositions,
                               double minSeparation) {
    const double minSepSq = minSeparation * minSeparation;
    for (auto& slot : slots) {
        for (const auto& livePos : liveVehiclePositions) {
            if (DistanceSq(slot.pos, livePos) < minSepSq) {
                slot.occupied = true;
                break;
            }
        }
    }
}

// CHOSEN sort key (file header item 5): ascending position-along-segment.
inline std::vector<const VehicleSlot*> SortedUnoccupiedSlots(const std::vector<VehicleSlot>& slots) {
    std::vector<const VehicleSlot*> out;
    for (const auto& s : slots) {
        if (!s.occupied) out.push_back(&s);
    }
    std::sort(out.begin(), out.end(), [](const VehicleSlot* a, const VehicleSlot* b) { return a->t < b->t; });
    return out;
}

struct VehicleSpawnResult {
    bool found = false;
    Vec3 position;
    uint32_t trafficTypeMask = 0;
};

// Full vehicle spawn-candidate pipeline (Sec17.3): Spline_Type+distance-band
// segment gather, a CHOSEN graph-node (segment) pick (file header item 3)
// via the shared helper, then the CONFIRMED-shape evenly-spaced/occupancy-
// filtered/sorted slot search, final slot picked via the same helper.
inline VehicleSpawnResult SelectVehicleSpawn(const std::vector<TrafficSegment>& segments, const Vec3& predictedPos,
                                              double innerDist, double outerDist, uint32_t vehicleTypeMask,
                                              const std::vector<Vec3>& liveVehiclePositions, double minSlotSeparation,
                                              size_t requestedSlotCount,
                                              const std::function<size_t(size_t)>& pickUniform) {
    VehicleSpawnResult result;

    auto segCandidates = GatherTrafficCandidates(segments, predictedPos, innerDist, outerDist, vehicleTypeMask);
    if (segCandidates.empty()) return result;

    size_t segIdx = pickUniform(segCandidates.size());
    if (segIdx >= segCandidates.size()) segIdx = 0;
    const TrafficSegment* seg = segCandidates[segIdx];

    auto slots = BuildSegmentSlots(*seg, requestedSlotCount);
    MarkOccupiedSlots(slots, liveVehiclePositions, minSlotSeparation);
    auto sorted = SortedUnoccupiedSlots(slots);
    if (sorted.empty()) return result;  // every slot on the chosen segment is occupied this pass

    size_t slotIdx = pickUniform(sorted.size());
    if (slotIdx >= sorted.size()) slotIdx = 0;

    result.found = true;
    result.position = sorted[slotIdx]->pos;
    result.trafficTypeMask = seg->splineTypeMask & vehicleTypeMask;
    return result;
}

// ===========================================================================
// Sec17.4: despawn.
// ===========================================================================

// (a) Per-entity distance+angle cull against the predicted camera position.
// CHOSEN combination rule (file header item 6): EITHER test failing culls
// the entity (too far, OR outside the forward angle cone).
inline bool ShouldDespawnEntity(const Vec3& entityPos, const Vec3& predictedCameraPos, const Vec3& cameraForward,
                                 double despawnRadius, double despawnAngleCosThreshold) {
    const double distSq = DistanceSq(entityPos, predictedCameraPos);
    if (distSq > despawnRadius * despawnRadius) return true;

    const Vec3 toEntity = entityPos - predictedCameraPos;
    const double len = Length(toEntity);
    if (len <= 1e-9) return false;  // degenerate: entity effectively at the camera - never culled on angle alone
    const double cosAngle = Dot(toEntity, cameraForward) / len;
    return cosAngle < despawnAngleCosThreshold;
}

// (b) Whole-category mass despawn (Sec17.4b/Sec17.6): unconditional, every
// live entry released regardless of distance - a hard reset, not a
// gradual cull.
template <typename Entity>
inline void MassDespawnCategory(std::vector<Entity>& liveList) {
    liveList.clear();
}

// ===========================================================================
// Sec17.6 (CONFIRMED, correcting spec-zone-data-format.md Sec13.4's stale
// "0-18 slots" claim): 6 named toggle categories, each its own 8-byte
// enable array, sharing one registration primitive (0x0086d770). This file
// does not reproduce the Lua-visible registration machinery itself
// (spec-lua-api-behaviour.md already owns that side) - only the gameplay
// EFFECT this module's own driver needs: when a category is OFF, mass-
// despawn its matching pool.
// ===========================================================================
enum class ToggleCategory {
    kDistantVehicle = 0,  // "os_distant_vehicle", @ 0x0260937c, notify 0x00b935d0
    kDistancePed = 1,     // "os_distance_ped",    @ 0x02609384, notify 0x00b90440
    kBoat = 2,             // "os_boat",            @ 0x0260938c
    kParking = 3,          // "os_parking",         @ 0x02609394 (spec-zone-data-format.md Sec13.4)
    kVehicle = 4,          // "os_vehicle",         @ 0x0260939c, notify 0x009073a0
    kPed = 5,              // "os_ped",             @ 0x026093a4
    kCount = 6
};

struct AmbientToggleState {
    // Each category's own 8-byte enable array (Sec17.6: "the query
    // accessor's 'all' branch loops a hard 8"). This file's own driver only
    // needs ONE bit of meaning per category (is the category enabled at
    // all, gating the mass-despawn trigger below) - the per-slot meaning of
    // the other 7 bytes is not stated anywhere Sec17.6 cites and is left
    // OPEN/unused here rather than guessed at.
    bool enabled[static_cast<size_t>(ToggleCategory::kCount)][8];

    AmbientToggleState() {
        for (auto& cat : enabled)
            for (bool& slot : cat) slot = true;  // default: every category on
    }

    bool AnyEnabled(ToggleCategory cat) const {
        for (bool b : enabled[static_cast<size_t>(cat)]) {
            if (b) return true;
        }
        return false;
    }
};

// ===========================================================================
// Sec17.4(a) despawn-cull + Sec17.2 capped refill, combined into one
// maintenance pass per population (the shape a caller's own heavy-pass tick
// runs once per ~second, see AmbientPopulationDriver below). Operates on a
// caller-owned live list - deliberately NOT a global/static singleton, same
// "no hidden process-wide state" discipline navpoint_selector.h's own
// BuildNavpointArray() documents.
// ===========================================================================
inline int RunPedHeavyPass(std::vector<LivePed>& liveList, const std::vector<SidewalkNode>& sidewalkNodes,
                            const Vec3& predictedPos, const Vec3& cameraForward, const SpawnDespawnParams& params,
                            double spawnDist, double despawnRadius, const std::function<size_t(size_t)>& pickUniform,
                            uint64_t& nextIdInOut) {
    liveList.erase(std::remove_if(liveList.begin(), liveList.end(),
                                   [&](const LivePed& p) {
                                       return ShouldDespawnEntity(p.pos, predictedPos, cameraForward, despawnRadius,
                                                                   params.despawnAngleCosThreshold);
                                   }),
                   liveList.end());

    std::array<int, kMaxPedTypeSlots> counts{};
    for (const auto& p : liveList) {
        if (p.typeSlot >= 0 && static_cast<size_t>(p.typeSlot) < kMaxPedTypeSlots) ++counts[static_cast<size_t>(p.typeSlot)];
    }

    const int budgetRemaining = kPedPopulationTarget - static_cast<int>(liveList.size());
    const int toSpawn = std::min(kPedRefillCapPerPass, std::max(0, budgetRemaining));
    int spawned = 0;
    for (int i = 0; i < toSpawn; ++i) {
        PedSpawnResult r = SelectPedSpawn(sidewalkNodes, predictedPos, spawnDist, counts, pickUniform);
        if (!r.found) break;  // Sec17.3 states no retry policy on an empty-candidate pass; stop rather than loop forever
        LivePed np;
        np.id = nextIdInOut++;
        np.pos = r.position;
        np.typeSlot = r.typeSlot;
        liveList.push_back(np);
        if (r.typeSlot >= 0 && static_cast<size_t>(r.typeSlot) < kMaxPedTypeSlots) ++counts[static_cast<size_t>(r.typeSlot)];
        ++spawned;
    }
    return spawned;
}

// Vehicle sibling of RunPedHeavyPass. `trafficTypes` round-robins (file
// header item 8, CHOSEN) over whatever traffic types the caller supplies.
inline int RunVehicleHeavyPass(std::vector<LiveVehicle>& liveList, const std::vector<TrafficSegment>& segments,
                                const std::vector<VehicleTrafficType>& trafficTypes, const Vec3& predictedPos,
                                const Vec3& cameraForward, const SpawnDespawnParams& params, double innerDist,
                                double spawnDist, double despawnRadius, double minSlotSeparation,
                                size_t requestedSlotCount, const std::function<size_t(size_t)>& pickUniform,
                                uint64_t& nextIdInOut) {
    liveList.erase(std::remove_if(liveList.begin(), liveList.end(),
                                   [&](const LiveVehicle& v) {
                                       return ShouldDespawnEntity(v.pos, predictedPos, cameraForward, despawnRadius,
                                                                   params.despawnAngleCosThreshold);
                                   }),
                   liveList.end());

    if (trafficTypes.empty()) return 0;

    const int budgetRemaining = kVehiclePopulationTarget - static_cast<int>(liveList.size());
    const int toSpawn = std::min(kVehicleRefillCapPerPass, std::max(0, budgetRemaining));
    int spawned = 0;
    for (int i = 0; i < toSpawn; ++i) {
        const VehicleTrafficType& type = trafficTypes[static_cast<size_t>(i) % trafficTypes.size()];
        std::vector<Vec3> livePositions;
        livePositions.reserve(liveList.size());
        for (const auto& v : liveList) livePositions.push_back(v.pos);

        VehicleSpawnResult r = SelectVehicleSpawn(segments, predictedPos, innerDist, spawnDist, type.splineTypeMask,
                                                   livePositions, minSlotSeparation, requestedSlotCount, pickUniform);
        if (!r.found) continue;  // this type/segment combination had no free slot this attempt - try the next type (CHOSEN retry policy) rather than aborting the whole pass
        LiveVehicle nv;
        nv.id = nextIdInOut++;
        nv.pos = r.position;
        nv.trafficTypeMask = r.trafficTypeMask;
        liveList.push_back(nv);
        ++spawned;
    }
    return spawned;
}

// ===========================================================================
// Sec17.5: the real driver's CONFIRMED shape - a shared one-shot-per-frame
// latch plus a per-tick ~1-second countdown, vehicle asked before ped
// (matching the real per-frame dispatcher 0x0090faf0's own call order).
// This class owns ONLY the throttle/mutual-exclusion bookkeeping; the
// actual heavy-pass work (RunPedHeavyPass/RunVehicleHeavyPass above) is the
// caller's to invoke when ShouldRunXHeavyPass() says to. This header does
// NOT model the global pause/loading gate Sec17.5 also cites (0x00720640/
// 0x006e44f0) - a caller-level concern, same scope split every other
// sr3airuntime unit already uses (e.g. cover_node_selector.h's own
// standalone-module note).
// ===========================================================================
class AmbientPopulationDriver {
public:
    explicit AmbientPopulationDriver(int64_t heavyPassPeriodMs = 1000) : heavyPassPeriodMs_(heavyPassPeriodMs) {}

    // Lightweight, every-frame bookkeeping (Sec17.5): refreshes the
    // predicted camera position from THIS frame's camera - cheap, no graph
    // walk, no spawn/despawn. Call every frame regardless of whether a
    // heavy pass also runs this frame.
    void RefreshCameraPrediction(const Vec3& cameraPos, const Vec3& cameraVelocity, const Vec3& cameraForward,
                                  double lookaheadTimeSeconds) {
        predictedPos_ = PredictCameraPosition(cameraPos, cameraVelocity, lookaheadTimeSeconds);
        cameraForward_ = cameraForward;
    }

    const Vec3& predictedPosition() const { return predictedPos_; }
    const Vec3& cameraForward() const { return cameraForward_; }

    // Call ONCE per frame, before asking ShouldRunXHeavyPass() - resets the
    // shared latch and advances both countdowns.
    void BeginFrame(int64_t dtMs) {
        latchConsumedThisFrame_ = false;
        vehicleCountdownMs_ -= dtMs;
        pedCountdownMs_ -= dtMs;
    }

    // Sec17.5: vehicle is asked first (real call order). Symmetric with
    // ShouldRunPedHeavyPass()'s own ordering below, even though in practice
    // (being asked first) vehicle is never itself the blocked side under
    // this class's own intended call order.
    bool ShouldRunVehicleHeavyPass() {
        if (vehicleCountdownMs_ > 0) return false;
        if (latchConsumedThisFrame_) return false;
        latchConsumedThisFrame_ = true;
        vehicleCountdownMs_ = heavyPassPeriodMs_;
        return true;
    }

    bool ShouldRunPedHeavyPass() {
        if (pedCountdownMs_ > 0) return false;
        if (latchConsumedThisFrame_) return false;  // blocked by the vehicle pass this frame - CHOSEN: countdown left
                                                      // expired (not re-armed), see file header item 7
        latchConsumedThisFrame_ = true;
        pedCountdownMs_ = heavyPassPeriodMs_;
        return true;
    }

private:
    int64_t heavyPassPeriodMs_;
    // Expired at construction (0), so the first frame's pass runs
    // immediately - this file's own choice (a freshly-booted population
    // wants to fill right away); Sec17.5 does not state the real engine's
    // own initial countdown value.
    int64_t vehicleCountdownMs_ = 0;
    int64_t pedCountdownMs_ = 0;
    bool latchConsumedThisFrame_ = false;
    Vec3 predictedPos_;
    Vec3 cameraForward_{1, 0, 0};
};

}  // namespace sr3airuntime
