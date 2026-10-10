#pragma once

// sr3airuntime/near_field_ped_population.h - the real near-field
// (fully-simulated) on-foot ambient-pedestrian spawn, Idle-mode sidewalk/
// wander sub-behaviour, and gunfire-to-flee/cower reaction chain:
// spec-ai-runtime.md Sec20 ("On-foot ambient pedestrian runtime: sidewalk
// spawn, path-following, idle behaviour, and the gunfire-to-flee trigger
// chain", 2026-10-09).
//
// RELATIONSHIP TO near_field_vehicle_population.h (the sibling this file
// was explicitly asked to mirror) AND ambient_population.h (the DISTANT/LOD
// ped tier, Sec17.3/Sec17.6). Sec20.1 itself draws the parallel directly:
// "a second, previously entirely undocumented near-field PEDESTRIAN spawn
// subsystem exists, structurally parallel to Sec17.4c's near-field VEHICLE
// system" - SAME shared per-frame dispatcher (0x0090faf0), SAME "os_ped"/
// "os_vehicle"-style toggle-category gate (ToggleCategory::kPed, reused
// directly from ambient_population.h, not re-declared), SAME "no promotion/
// hand-off between distant and near-field tiers" honest negative Sec17.4c
// already established for vehicles - Sec20.1's own words: "this directly
// closes Sec17.3's open promotion/distant-vs-near-field question for
// pedestrians the same way Sec17.4c closed it for vehicles... the near-field
// tier spawns its own, independent population directly on the sidewalk
// graph." This file therefore does NOT model any promotion/demotion path
// and deliberately does NOT share a live-entity list, cap, or budget counter
// with ambient_population.h's LivePed - NearFieldPed is its own type.
//
// WHAT'S REUSED, NOT REINVENTED (this task's own explicit instruction,
// mirroring near_field_vehicle_population.h's own file-header discipline):
//   - sr3airuntime::DeterministicRng / MakePickUniform (navpoint_selector.h):
//     the SAME shared RNG-seam idiom near_field_vehicle_population.h already
//     reuses - here it underlies PickWeightedIndex's own pickUniform call,
//     not a second RNG abstraction.
//   - sr3airuntime::EngineTimer (common.h) and the ModeMachine::randomRangeMs
//     seam idiom (mode_table.h's own `std::function<int64_t(int64_t,int64_t)>`
//     convention, "the engine's own RNG... the default is the deterministic
//     midpoint... NOT a recovered engine value") - reused as-is for this
//     file's own NearFieldPedDriver tick timer AND for
//     RollActionNodeIdleCooldownMs's cooldown roll (Sec20.3's own "genuine
//     cooldown roll" idiom), not a third randomization mechanism.
//   - ambient_population.h's AmbientToggleState / ToggleCategory::kPed
//     ("os_ped", @ 0x026093a4): Sec20.1's own correction confirms this
//     EXACT toggle is what "0x0090fa60 reads... every frame" to gate this
//     whole chain - reused directly (this file includes
//     ambient_population.h for it), not re-declared.
//   - ambient_population.h's PickWeightedIndex: a NEW reuse this file adds
//     (near_field_vehicle_population.h never needed it, since the vehicle
//     side's own final pick is flat-uniform, not weighted) - Sec20.1's own
//     "LENGTH-WEIGHTED cumulative distribution" over Sidewalks segments is
//     exactly PickWeightedIndex's own generic shape, weights = segment
//     lengths, no second weighted-pick implementation invented here.
//   - near_field_vehicle_population.h's kHumanEligibilityCeiling /
//     RemainingHumanHeadroom / HasHumanHeadroomForSpawn: Sec20.1's own text
//     is explicit - "spawn budget = min(local headroom, the SAME shared
//     150-human ceiling 0x009086d0 Sec17.4c already documents) x a rate
//     constant" - the identical global human-population ceiling, reused by
//     #include, not a second 150 constant redeclared here.
//
// GENUINE STRUCTURAL DIFFERENCES FROM near_field_vehicle_population.h (the
// task's own explicit ask: mirror closely, but call out real differences,
// not paper over them):
//   1. Spawn final-pick mechanism genuinely differs. The vehicle sibling's
//      own final pick is a flat 3-slot UNIFORM pick (0x00dab660, the SAME
//      shared uniform-random-pick primitive navpoint_selector.h's own file
//      header discusses at length). Sec20.1 names a DIFFERENT primitive for
//      peds, 0x00dab5e0, driving a LENGTH-WEIGHTED cumulative distribution
//      over candidate Sidewalks segments - not a flat pick among a small
//      fixed slot buffer. This is a CONFIRMED, not CHOSEN, difference: Sec20.1
//      states the weighting explicitly ("a genuine consumption of the
//      graph's real topology, not just its node count (unlike the distant
//      tick)"). SelectNearFieldPedSpawn below reflects this honestly rather
//      than reusing SelectNearFieldVehicleSpawn's own flat-pick shape.
//   2. A CONFIRMED per-attempt retry loop (<=3 retries, Sec20.1's own text)
//      exists on the ped side with no vehicle-sibling equivalent - the
//      vehicle file's own CHOSEN item 3 settles for "at most one spawn
//      attempt per tick" with no retry concept at all (the real vehicle
//      spawn-rate formula being OPEN, not a retry-on-rejection loop).
//   3. NO DESPAWN PREDICATE IS IMPLEMENTED HERE. near_field_vehicle_population.h's
//      primary surface is its own CONFIRMED despawn-eligibility predicate
//      (0x00906b00). spec-ai-runtime.md Sec20 (spawn/placement Sec20.1,
//      walking/pathing Sec20.2, idle behaviour Sec20.3, gunfire reaction
//      Sec20.4) never traces a near-field PEDESTRIAN despawn predicate at
//      all - genuinely out of this investigation's scope, not merely
//      unimplemented. This file's own maintenance pass
//      (RunNearFieldPedSpawnPass) is therefore SPAWN-ONLY, unlike the
//      vehicle sibling's combined despawn+spawn RunNearFieldVehicleMaintenancePass.
//      A future unit that traces the real ped despawn predicate should add
//      it here, not have this file guess at reusing the vehicle's own
//      0x00906b00 shape for a structurally unconfirmed ped equivalent.
//   4. NO CONFIRMED NUMERIC CADENCE for the ped tick's own throttle gate.
//      Sec20.1's 0x009149a0 confirms the STRUCTURE ("two independent
//      per-registry elapsed-time checks both pass") but gives neither
//      check's own numeric range - unlike the vehicle sibling's own
//      clearly-stated 200-300ms/400-600ms dual timers. NearFieldPedDriver
//      below therefore takes the ms range as a CALLER-SUPPLIED parameter
//      (no baked literal masquerading as confirmed) and models only ONE
//      randomized gate, not two - flagged in place below for a future pass
//      that finds the real two-registry numbers.
//   5. Two entirely new top-level mechanisms with no vehicle equivalent at
//      all: the Idle-mode sidewalk/wander sub-state machine (Sec20.2/20.3)
//      and the gunfire-to-flee/cower reaction chain gated on the human's own
//      team byte (Sec20.4) - vehicles have neither an "Idle mode" nor a
//      "team byte" the way on-foot humans do.
//   6. The ped sibling of the vehicle file's own CHOSEN item 4
//      (inPlayerWorldCell's missing spawn-time default, found via that
//      file's own demo tool). Sec20.1's own directly-checked negative: the
//      ped creation routine (0x0090b0a0) never calls any of the 4
//      sidewalking/wandering/sidewalk_leashed/dont_use_sidewalk force-flag
//      setters - "sidewalking" is most plausibly the AI sub-object's own
//      construction-time default (HYPOTHESIS 1, not independently proven
//      causally), not something the spawner sets. This file models the
//      OBSERVABLE BEHAVIOUR faithfully: NearFieldPed's own idleSubState
//      defaults directly to a sidewalk-group sub-state at construction, and
//      neither this file's spawn pipeline nor its Idle tick dispatcher ever
//      calls (or even declares) a sidewalking/wandering force-flag setter -
//      see PedIdleSubState's own comment and testSidewalkIsTheTrueDefaultAtCreation.
//
// CONFIRMED vs CHOSEN vs OPEN - same discipline every other sr3airuntime
// unit applies, and spec-ai-runtime.md's own top-of-file confidence key.
// Every function/struct below cites its own section; see each one in place
// rather than re-deriving the full list here.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "sr3airuntime/ambient_population.h"           // AmbientToggleState / ToggleCategory::kPed ("os_ped"); PickWeightedIndex (length-weighted pick, reused not reinvented)
#include "sr3airuntime/common.h"                       // Vec3, Distance, DistanceSq, Length, EngineTimer
#include "sr3airuntime/near_field_vehicle_population.h"  // kHumanEligibilityCeiling / HasHumanHeadroomForSpawn - the SAME shared 150-human ceiling (Sec20.1's own text)

namespace sr3airuntime {

// ===========================================================================
// Sec20.1: the Sidewalks graph's own 24-byte-stride SEGMENT sub-array
// (spec-zone-data-format.md Sec13.2 - "two u16 node-index fields per
// record", DISTINCT from the already-documented 36-byte-stride one), as
// this file's own spawn algorithm needs it. This file does NOT read the
// real Sidewalks singleton (0x02924d40) itself - no C++ reader for that
// content kind exists in this project yet (same scope-split
// near_field_vehicle_population.h's own CHOSEN item 1 documents for the
// Traffic graph: callers supply pre-resolved candidate geometry, this file
// owns only the CONFIRMED selection algorithm over it).
// ===========================================================================
struct SidewalkSegment {
    uint64_t id = 0;
    Vec3 start, end;  // the two resolved endpoint node positions (Sec20.1)

    double Length() const { return Distance(start, end); }
    Vec3 PointAt(double t) const { return start + (end - start) * t; }  // t in [0,1]
};

// Gather candidate segments within `maxDist` of `predictedPos`, filtered by
// segment MIDPOINT distance (CHOSEN reference point, same reasoning
// ambient_population.h's own GatherTrafficCandidates applies to its segment
// midpoint test). A single upper-bound disc, not an [inner,outer] annulus -
// Sec20.1 does not state an inner bound for this near-field case (it only
// ever describes "a filtered candidate-segment array", no explicit band).
inline std::vector<const SidewalkSegment*> GatherSidewalkSegmentCandidates(
    const std::vector<SidewalkSegment>& segments, const Vec3& predictedPos, double maxDist) {
    std::vector<const SidewalkSegment*> out;
    const double maxDistSq = maxDist * maxDist;
    for (const auto& seg : segments) {
        const Vec3 mid = seg.PointAt(0.5);
        if (DistanceSq(mid, predictedPos) <= maxDistSq) out.push_back(&seg);
    }
    return out;
}

// ===========================================================================
// Sec20.1: spawn-candidate gates inside the creation routine (0x0090b0a0,
// "~1980 bytes, the largest function in the chain"). Several of these are
// caller-supplied facts this file deliberately does not own (camera/
// rendering/ground-physics/registry concerns), the SAME scope-split
// discipline near_field_vehicle_population.h applies to playerPos/
// eligibleHumanCount - a predicate with no CONFIRMED formula gets an
// injectable seam with an honestly-documented CHOSEN default, not an
// invented formula.
// ===========================================================================
inline constexpr size_t kNearFieldPedMaxSpawnRetries = 3;  // Sec20.1 CONFIRMED: "per attempt (<=3 retries)"
// Sec20.1: "rejects within ~90 units of the camera (no pop-in next to the
// player)". The spec's own text already carries the "~" (approximate, not
// an exact disassembled literal) - kept as such, not rounded into a false
// precision.
inline constexpr double kNearFieldPedCameraNearRejectDistApprox = 90.0;

struct NearFieldPedSpawnGates {
    // OPEN: "a difficulty gate" is named but never characterised beyond that
    // (Sec20.1). CHOSEN default: always passes (no gate blocks by default).
    std::function<bool()> difficultyGatePasses = [] { return true; };
    // OPEN: Sec20.1's own "a second distance threshold" (the FAR reject) is
    // never given a literal, unlike the near reject's own "~90 units". CHOSEN:
    // <=0 disables this gate entirely (treated as "no far limit") - a caller
    // that wants it enforced must supply a real value; this file does not
    // invent one.
    double cameraFarRejectDist = 0.0;
    // CONFIRMED a real, separate gate exists (Sec20.1: "rejects if visible to
    // the current (or splitscreen) camera" - distinct from, and in ADDITION
    // to, the ~90-unit near-distance reject above: the spec's own text lists
    // "rejects within ~90 units... and beyond a second distance threshold;
    // rejects if visible to the current camera" as three SEPARATE reject
    // conditions, not alternatives - the ~90-unit floor is unconditional
    // regardless of visibility, and the visibility test applies ON TOP OF it
    // for any candidate that clears the distance gates). The real
    // FOV/clip-plane/occlusion parameters the engine's own frustum test uses
    // are OPEN (not given by Sec20.1 beyond naming the check) - camera/
    // rendering state is out of this module's scope either way. CHOSEN
    // default here: never visible (never rejects) - a caller owning the real
    // camera/frustum state supplies the real test; ApproxVisibleToCamera
    // below is this project's own CHOSEN, honestly-approximate stand-in for
    // callers that want the real "can spawn closer when off-screen" behaviour
    // without a real renderer-owned frustum to query.
    std::function<bool(const Vec3&)> isVisibleToCamera = [](const Vec3&) { return false; };
    // OPEN: "a ground-validity check" - the real probe is a physics-side
    // concern, out of scope. CHOSEN default: always valid.
    std::function<bool(const Vec3&)> isGroundValid = [](const Vec3&) { return true; };
    // OPEN: Sec20.1's own "offset sideways" names the step but gives no
    // magnitude. CHOSEN default 0.0 (no offset) - a caller that wants the
    // real offset applied supplies a real distance.
    double sidewaysOffsetDistance = 0.0;
    // Character-model resolution, several "attribute/eligibility rolls," and
    // the actual engine creation call's own post-creation setup (Sec20.1)
    // are explicitly NOT modelled here at all (not even as a seam) - this
    // project has no character-definition/model registry anywhere (a
    // stated, project-wide gap, the SAME one sr3tables_trafficai/tables.h's
    // own file header documents for every "external, unresolved here" xtbl
    // field) and this task's own brief says not to overbuild.
};

// ApproxVisibleToCamera - a CHOSEN, deliberately simple stand-in for
// Sec20.1's own CONFIRMED-to-exist-but-not-fully-specified "rejects if
// visible to the current (or splitscreen) camera" test. Sec20.1 confirms the
// CHECK exists and that it is separate from the distance gates (see
// NearFieldPedSpawnGates::isVisibleToCamera's own comment above) but gives
// neither the real FOV/clip-plane shape nor an aspect ratio/vertical-FOV
// term - this project's own standing discipline (don't invent precise
// frustum math wholesale, e.g. don't guess at FOV angles the spec doesn't
// give) means this is explicitly NOT a claim about the real engine's own
// clip planes. It is a planar (XZ, horizontal-only; no pitch/vertical FOV
// term - on-foot ambient peds are ground-level, so the missing vertical term
// is not expected to matter in practice), single-cone "is this point roughly
// in front of the camera within a CHOSEN half-angle" test: a dot-product
// test against the camera's own forward direction, exactly the shape every
// other "roughly facing" test in this project already uses (e.g.
// ResolveDirectionAlongHeading's own forward-dot idiom). kApproxHalfFovDeg
// is a CHOSEN placeholder (a plausible third-person-camera horizontal half-
// FOV), not a recovered engine constant - callers that have a real FOV
// figure should pass it explicitly rather than relying on the default.
inline constexpr double kApproxHalfFovDegChosen = 50.0;  // CHOSEN, not CONFIRMED - see this function's own comment

inline bool ApproxVisibleToCamera(const Vec3& point, const Vec3& cameraPos, const Vec3& cameraForwardXZ,
                                   double halfFovDeg = kApproxHalfFovDegChosen) {
    Vec3 toPoint{point.x - cameraPos.x, 0.0, point.z - cameraPos.z};
    const double dist = Length(toPoint);
    if (dist < 1e-6) return true;  // degenerate (camera and candidate coincide) - treat as visible, the conservative (reject-the-spawn) direction
    toPoint = toPoint * (1.0 / dist);

    Vec3 fwd{cameraForwardXZ.x, 0.0, cameraForwardXZ.z};
    const double fwdLen = Length(fwd);
    if (fwdLen < 1e-6) return true;  // no real facing supplied - conservative (reject), same reasoning as above
    fwd = fwd * (1.0 / fwdLen);

    const double cosAngle = toPoint.x * fwd.x + toPoint.z * fwd.z;
    const double cosHalfFov = std::cos(halfFovDeg * 3.14159265358979323846 / 180.0);
    return cosAngle >= cosHalfFov;  // within the cone -> "visible" -> caller's gate rejects it
}

struct NearFieldPedSpawnResult {
    bool found = false;
    Vec3 position;
    uint64_t segmentId = 0;
    int attemptsUsed = 0;  // 1..kNearFieldPedMaxSpawnRetries when found; test/diagnostic visibility into the CONFIRMED retry loop
};

// Sec20.1's own CONFIRMED pipeline, ported literally: "per attempt (<=3
// retries): weighted-random segment pick (0x00dab5e0) -> 0x0090b840 -> ONE
// SPAWN ATTEMPT: random point along the chosen segment, offset sideways ->
// 0x0090b0a0 (CREATE+INITIALIZE, gated on difficulty/distance/visibility/
// ground-validity)". The length-weighted pick itself is
// ambient_population.h's own PickWeightedIndex, weights = each candidate
// segment's own Length() - CONFIRMED shape (file header item: "a genuine
// consumption of the graph's real topology"), not reinvented here.
inline NearFieldPedSpawnResult SelectNearFieldPedSpawn(const std::vector<SidewalkSegment>& candidateSegments,
                                                        const NearFieldPedSpawnGates& gates, const Vec3& cameraPos,
                                                        const std::function<size_t(size_t)>& pickUniform) {
    NearFieldPedSpawnResult result;
    if (candidateSegments.empty()) return result;

    std::vector<double> lengths;
    lengths.reserve(candidateSegments.size());
    for (const auto& seg : candidateSegments) lengths.push_back(seg.Length());

    for (size_t attempt = 0; attempt < kNearFieldPedMaxSpawnRetries; ++attempt) {
        result.attemptsUsed = static_cast<int>(attempt) + 1;

        const size_t segIdx = PickWeightedIndex(lengths, pickUniform);
        if (segIdx >= candidateSegments.size()) continue;  // every weight <= 0 (e.g. all-degenerate segments) - this attempt is a miss, not a hard failure
        const SidewalkSegment& seg = candidateSegments[segIdx];

        constexpr size_t kTResolution = 1'000'000;
        const double t = static_cast<double>(pickUniform(kTResolution)) / static_cast<double>(kTResolution);
        Vec3 point = seg.PointAt(t);

        // Sideways offset: perpendicular to the segment direction, CHOSEN
        // planar (XZ) left-normal (Sec20.1 does not specify the offset
        // axis/sign) - documented, not confirmed.
        if (gates.sidewaysOffsetDistance != 0.0) {
            const Vec3 dir = seg.end - seg.start;
            Vec3 normal{-dir.z, 0.0, dir.x};
            const double nLen = Length(normal);
            if (nLen > 1e-9) {
                normal = normal * (gates.sidewaysOffsetDistance / nLen);
                point = point + normal;
            }
        }

        if (!gates.difficultyGatePasses()) continue;
        const double distSqToCamera = DistanceSq(point, cameraPos);
        if (distSqToCamera < kNearFieldPedCameraNearRejectDistApprox * kNearFieldPedCameraNearRejectDistApprox) continue;
        if (gates.cameraFarRejectDist > 0.0 && distSqToCamera > gates.cameraFarRejectDist * gates.cameraFarRejectDist) continue;
        if (gates.isVisibleToCamera(point)) continue;
        if (!gates.isGroundValid(point)) continue;

        result.found = true;
        result.position = point;
        result.segmentId = seg.id;
        return result;
    }
    // Sec20.1 does not state a further fallback beyond the <=3 retries -
    // same "stop rather than loop forever" discipline ambient_population.h's
    // own RunPedHeavyPass applies to an empty-candidate pass.
    return result;
}

// ===========================================================================
// Sec20.2/20.3: Idle-mode sidewalk-follow/wander sub-behaviours. Real
// sub-state indices (human+0x500, the 177-entry sub-state name table
// 0x012e1168) are used directly as this enum's own values, so a reader
// cross-checking against the spec's own table finds the SAME numbers here -
// matching this project's own convention of mirroring confirmed indices
// directly (e.g. kNearFieldVehicleCap's own "< 0x30" comment).
// ===========================================================================
enum class PedIdleSubState : int {
    Idle = 9,                 // Idle::enter()'s own generic fallback (Sec20.3) - NOT reachable from this file's own spawn path; see file header item 6
    WaitToHolsterWeapon = 11,  // Idle::enter()'s inventory-held-item branch (Sec20.3) - not driven by this file
    SidewalkStart = 13,        // CHOSEN construction-time default (file header item 6) - "needs to snap onto the graph" (0x0051bb70's own gate)
    Sidewalk = 14,             // steady-state sidewalk-follow, once snapped
    SidewalkFormation = 15,    // routes through the SAME tick as 13/14 (Sec20.2); no behavioural difference modelled here (Sec20.2 does not describe one beyond the name)
    WanderPause = 32,
    WanderChooseDest = 33,
    WanderMove = 34,
    WanderToSidewalk = 35,     // "a one-frame transitional no-op" (Sec20.2)
    Sleeping = 36,             // Idle::enter()'s own pre-set-flag exception (Sec20.3) - not driven by this file
    Sitting = 37,              // ditto
};

enum class PedIdleTickGroup { Sidewalk, Wander, OneFrameTransition, Other };

// Sec20.2: "sub-states 13/14/15 all route to 0x0051bc60 ('sidewalk' tick);
// sub-states 32/33/34 all route to 0x0051c860 ('wander' tick); sub-state 35
// is a one-frame transitional no-op." A direct, literal port of that routing
// table - the SAME single-dispatcher structure 0x00513270 itself is
// confirmed to implement.
inline PedIdleTickGroup TickGroupFor(PedIdleSubState s) {
    switch (s) {
        case PedIdleSubState::SidewalkStart:
        case PedIdleSubState::Sidewalk:
        case PedIdleSubState::SidewalkFormation:
            return PedIdleTickGroup::Sidewalk;
        case PedIdleSubState::WanderPause:
        case PedIdleSubState::WanderChooseDest:
        case PedIdleSubState::WanderMove:
            return PedIdleTickGroup::Wander;
        case PedIdleSubState::WanderToSidewalk:
            return PedIdleTickGroup::OneFrameTransition;
        default:
            return PedIdleTickGroup::Other;
    }
}

// A near-field ped, reduced to exactly what this file's own Idle-tick and
// gunfire-reaction logic read - the SAME "reduced to exactly what the
// algorithm reads" discipline NearFieldVehicle's own struct comment applies.
// A genuinely SEPARATE type from ambient_population.h::LivePed (file header:
// no hand-off between tiers).
struct NearFieldPed {
    uint64_t id = 0;
    Vec3 pos;

    // human+0xb94: "a per-human pointer directly into the pair-0 segment
    // array" - the human's own "current segment" (Sec20.2, confirmed
    // network-replicated like other per-character state). This file carries
    // only the id; resolving it against a real Sidewalks graph is the
    // caller's own job (file header: no C++ Sidewalks reader exists here).
    uint64_t currentSegmentId = 0;
    bool onSegment = false;       // true once "snapped onto the graph" (sub-state 13's own gate, 0x0051bb70)
    double segmentProgress = 0.0;  // 0..1 along currentSegmentId - CHOSEN linear model of 0x0051b960's own per-frame advance (HIGH CONFIDENCE shape, not independently disassembled to its own last instruction, Sec20.2's own honest OPEN item)

    // CHOSEN default: a bare-constructed ped starts in the sidewalk group,
    // never wander/idle/other - see file header item 6 and
    // testSidewalkIsTheTrueDefaultAtCreation. Deliberately NOT set via any
    // force-flag setter call anywhere in this file (Sec20.1's own directly-
    // checked negative: the real creation routine never calls one either).
    PedIdleSubState idleSubState = PedIdleSubState::SidewalkStart;

    // --- gunfire-reaction inputs (Sec20.4) - caller-supplied facts this
    // file does not own, same scope-split discipline as above. ---
    int team = -1;  // human+0x1ca8 (CHOSEN sentinel -1 "never set", mirrors sr3luahost::CharacterState::currentTeamCode's OWN documented convention for the identical offset, spec-lua-api-behaviour.md Sec13.18). NOTE: a DIFFERENT numbering space from action_node_npcs.xtbl's own `Team` element (Civilian=0/Police=1/Gang=2/Saints=3, spec-tables-traffic-ai.md Sec13.3/sr3tables_trafficai::ActionNodeNpc::team) - the two are never conflated by this file; see kCivilianTeamByte below.
    bool isFollower = false;  // 0x00943ea0's own "NOT-follower" half of its test - a role fact this file does not derive (OPEN: the follower-status writer was not traced this pass)
    // 0x004ebb00's own no-op gates: "no-ops on down/dead/ignore_attacks/
    // combat_disabled/dont_react_to_player". downOrDead is a status fact;
    // the other three are already-documented force-flag bits elsewhere in
    // this spec (combat_disabled: +0x2b8 bit 4; dont_react_to_player:
    // +0x2b9 bit 2; ignore_attacks: +0x2b9 bit 3 - spec-ai-runtime.md Sec9.9's
    // own force-flag table) - this file carries only the resolved booleans,
    // not the bit-level force-flag storage (out of this file's own scope).
    bool downOrDead = false, ignoreAttacks = false, combatDisabled = false, dontReactToPlayer = false;
};

// ---------------------------------------------------------------------------
// Sidewalk tick group (sub-states 13/14/15, Sec20.2's own 0x0051bc60).
// ---------------------------------------------------------------------------
struct SidewalkTickParams {
    // OPEN: 0x0051b960's own per-frame advance math (Sec20.2: "HIGH
    // CONFIDENCE, not independently disassembled to its own last
    // instruction") - CHOSEN linear model, caller supplies the per-tick
    // progress delta (0 disables movement; a test/demo supplies a real
    // value).
    double advancePerTick = 0.0;
    // OPEN: 0x0051bd20's own real abandon/retarget TRIGGER was not
    // independently traced this pass (Sec20.2 names the force-transition
    // itself - "sidewalk" -> "wander choose dest" - but not what decides to
    // call it). CHOSEN default: never abandons; a caller may inject a real
    // trigger.
    std::function<bool()> shouldAbandonToWander = [] { return false; };
    // "snap onto the graph" (sub-state 13's own gate, 0x0051bb70 ->
    // 0x00bbfbe0): resolving the NEAREST segment from the human's own cached
    // cell-key (+0xb40) is this file's caller's own job (the Sidewalks graph
    // is not owned here - file header's own "no C++ reader" note). 0 means
    // "no resolution available yet this tick."
    uint64_t snapSegmentId = 0;
};

inline void TickSidewalk(NearFieldPed& ped, const SidewalkTickParams& params) {
    if (!ped.onSegment) {
        if (params.snapSegmentId != 0) {
            ped.currentSegmentId = params.snapSegmentId;
            ped.onSegment = true;
            ped.segmentProgress = 0.0;
            ped.idleSubState = PedIdleSubState::Sidewalk;  // 13 -> 14 once snapped (CHOSEN transition target; Sec20.2 names 14 as the steady-state tick)
        }
        return;  // still waiting on a graph snap this tick - no advance
    }

    if (params.shouldAbandonToWander()) {
        // 0x0051bd20: "sidewalk" -> "wander choose dest" - the real
        // abandon/retarget path (Sec20.2).
        ped.idleSubState = PedIdleSubState::WanderChooseDest;
        ped.onSegment = false;
        return;
    }

    // OPEN beyond this point (Sec20.2): what happens at segmentProgress==1
    // (reaching the segment's end - pick an adjacent segment? stop? loop?)
    // is not independently confirmed. This file leaves a ped parked at
    // progress==1 rather than inventing a graph-hop the spec does not
    // describe.
    ped.segmentProgress = std::min(1.0, ped.segmentProgress + params.advancePerTick);
}

// ---------------------------------------------------------------------------
// Wander tick group (sub-states 32/33/34, Sec20.2's own 0x0051c860).
// ---------------------------------------------------------------------------
struct WanderTickParams {
    // OPEN: Sec20.2 never gives "wander pause"'s own duration (unlike the
    // SEPARATE, CONFIRMED 15-35s figure below, which is for a different
    // purpose - the wander-destination-commit event's own re-post cadence,
    // not this pause). CHOSEN default: always elapsed immediately; a caller
    // injects a real timer.
    std::function<bool()> pauseElapsed = [] { return true; };
    // Sec20.2: "(for 'wander choose dest,' sub-state 33) generates a
    // validated random candidate point... calling 0x00993630" - the real
    // navmesh/navigator-chain candidate generator and its own validator are
    // OUT OF THIS FILE'S SCOPE (0x00993630 is the already-named "human
    // navigator chain," Sec14.2/Sec20.2's own closed cross-reference - a
    // separate unit's job, not reimplemented here). CHOSEN default: no
    // destination (nullopt) - never auto-advances past "choose dest" on its
    // own.
    std::function<std::optional<Vec3>()> chooseRandomDestination = [] { return std::optional<Vec3>{}; };
    // OPEN: wander-move completion criteria (reaching the destination) was
    // identified by call shape only, not independently confirmed (Sec20.2).
    // CHOSEN default: always reached immediately.
    std::function<bool(const Vec3&)> hasReachedDestination = [](const Vec3&) { return true; };
};

// Event kind 81 (the wander-destination-commit step, posted via 0x004f5dd0,
// sub-state filter 34, 15-35s delay - CONFIRMED, Sec20.2) is this project's
// own perception/event-posting machinery's job (mode_table.h's own
// postEvent/randomRangeMs - "a negative value means 'random 250-750ms'" is
// the SAME idiom, just a different range here) - posting it is the caller's
// own responsibility once WanderMove is entered; this file does not
// duplicate mode_table.h's event-posting plumbing, only the sub-state
// transition that would trigger a real caller to post it.
inline void TickWander(NearFieldPed& ped, const WanderTickParams& params, Vec3& wanderDestinationInOut) {
    switch (ped.idleSubState) {
        case PedIdleSubState::WanderPause:
            if (params.pauseElapsed()) ped.idleSubState = PedIdleSubState::WanderChooseDest;
            break;
        case PedIdleSubState::WanderChooseDest: {
            std::optional<Vec3> dest = params.chooseRandomDestination();
            if (dest.has_value()) {
                wanderDestinationInOut = *dest;
                ped.idleSubState = PedIdleSubState::WanderMove;  // Sec20.2: "transitioning to sub-state 34 and calling 0x00993630"
            }
            break;
        }
        case PedIdleSubState::WanderMove:
            if (params.hasReachedDestination(ped.pos)) {
                ped.idleSubState = PedIdleSubState::WanderToSidewalk;  // Sec20.2's own named next sub-state
            }
            break;
        default:
            break;  // not a wander-group sub-state - caller routed incorrectly; a no-op, not an error (same defensive discipline as every other injected-seam clamp in this project)
    }
}

// Sub-state 35's own "one-frame transitional no-op" (Sec20.2). CHOSEN
// target: back onto the sidewalk graph via sub-state 13 ("sidewalk start"),
// matching the sub-state's own name ("wander TO sidewalk") - not
// independently confirmed beyond the name itself; Sec20.2 does not trace
// 0x0051bd20's OWN target sub-state for this specific transition.
inline void TickOneFrameTransition(NearFieldPed& ped) {
    if (ped.idleSubState == PedIdleSubState::WanderToSidewalk) {
        ped.idleSubState = PedIdleSubState::SidewalkStart;
        ped.onSegment = false;  // must re-snap, consistent with how a ped also enters via sub-state 13 at creation
    }
}

// Top-level dispatcher mirroring 0x00513270's own routing (Sec20.2) exactly:
// sidewalk group -> TickSidewalk, wander group -> TickWander, 35 ->
// TickOneFrameTransition. Sleeping/sitting/holster-weapon/idle(9) (Sec20.3)
// have their own tick routines this file does not model (out of this task's
// own scope - file header item 5's "no vehicle equivalent" note applies in
// reverse here too: this file only owns the near-field spawner's own
// sidewalk/wander/gunfire surface, not every Idle sub-state in the game).
inline void TickIdleSubBehaviour(NearFieldPed& ped, const SidewalkTickParams& sidewalkParams,
                                  const WanderTickParams& wanderParams, Vec3& wanderDestinationInOut) {
    switch (TickGroupFor(ped.idleSubState)) {
        case PedIdleTickGroup::Sidewalk:
            TickSidewalk(ped, sidewalkParams);
            break;
        case PedIdleTickGroup::Wander:
            TickWander(ped, wanderParams, wanderDestinationInOut);
            break;
        case PedIdleTickGroup::OneFrameTransition:
            TickOneFrameTransition(ped);
            break;
        case PedIdleTickGroup::Other:
            break;
    }
}

// ===========================================================================
// Sec20.3: action_node_npcs.xtbl-driven Idle data. A caller populates this
// struct from a real sr3tables_trafficai::ActionNodeNpc row's own
// Min_anim_delay/Max_anim_delay (seconds; that header's own comment: "engine
// *1000 -> ms") - this file deliberately does NOT #include
// sr3tables_trafficai/tables.h or re-parse the xtbl itself. This is the SAME
// decoupling ambient_population.h's own VehicleTrafficType struct documents
// for distant_vehicle_traffic_types.xtbl ("a caller feeds this from that
// table's own existing loader... not re-parsed here") - sr3airuntime stays
// decoupled from the sr3tables_trafficai library; a caller that already owns
// a parsed ActionNodeNpc row applies the documented *1000 conversion itself
// and hands this file only the POST-CONVERSION ms values, exactly the way
// ambient_population.h's own RunVehicleHeavyPass caller is expected to
// supply lengthPerSpawnReciprocal already inverted.
// ===========================================================================
struct ActionNodeIdleTiming {
    bool present = false;  // mirrors sr3xtbl::Always<float>::present - false means "row/field absent", NOT "0ms" (same discipline, sr3xtbl/xtbl.h)
    int64_t minAnimDelayMs = 0;
    int64_t maxAnimDelayMs = 0;
};

// Sec20.3: "the default, sub-state 9 'idle,' with a genuine cooldown roll
// via 0x00dab660 (250-1000ms or 3000-5000ms) - the same per-action cooldown
// idiom Sec4.1's decision-layer finish routine already documents, now
// confirmed used directly inside Idle's own enter too." This file reuses
// the SAME randomRangeMs seam (mode_table.h's own idiom) to roll a cooldown
// from action_node_npcs.xtbl's own per-row Min/Max_anim_delay range instead
// of a fixed literal pair - Sec20.3's own text: "Action-node idle/loitering
// behaviour is DATA-DRIVEN... the specific runtime function that reads a
// record's State/Action array and rolls the anim-delay timers was not found
// this pass" - so this is this file's own CHOSEN placement of that
// CONFIRMED-to-exist roll against the one CONFIRMED-shape cooldown idiom the
// spec already names, not a guess at the unfound runtime function's own
// internals.
inline int64_t RollActionNodeIdleCooldownMs(const ActionNodeIdleTiming& timing,
                                             const std::function<int64_t(int64_t, int64_t)>& randomRangeMs) {
    if (!timing.present || timing.maxAnimDelayMs <= 0) return 0;  // absent row: no CONFIRMED fallback literal to invent - 0 signals "should not have been called"
    const int64_t lo = std::min(timing.minAnimDelayMs, timing.maxAnimDelayMs);
    const int64_t hi = std::max(timing.minAnimDelayMs, timing.maxAnimDelayMs);
    return randomRangeMs(lo, hi);
}

// ===========================================================================
// Sec20.4: the real flee-or-react-to-gunfire chain, gated on the human's own
// team byte (human+0x1ca8) being exactly 7 - "not by any abstract 'is this a
// civilian' flag."
// ===========================================================================
// HIGH CONFIDENCE "ordinary civilian", confirmed BEHAVIOURALLY at six
// independent code sites (Sec20.4) but NOT confirmed by name - no string/
// line table was read, and a whole-.text scan for a literal byte-7 MOV found
// zero hits (team assignment is dynamic, via the team-registry write side,
// Sec12.7's 0x009b7130 - not a compile-time literal anywhere this was
// checked). Kept as a plain named int constant, the SAME representation
// convention perception.h's own TeamRelationship(teamA, teamB) uses for team
// ids elsewhere in this project (plain int, no enum class) - NOT the same
// numbering space as action_node_npcs.xtbl's own `Team` element (see
// NearFieldPed::team's own comment above).
inline constexpr int kCivilianTeamByte = 7;

// Real mode ids Sec20.4 names for this chain (GunfireWary=17, Cower=9,
// Flee=13, Panic=24) - kept as named int constants (not baked into
// PedReactionMode's own ordinals) since nothing here dispatches by raw
// mode-id integer the way PedIdleSubState deliberately mirrors the real
// sub-state table's own numbering.
inline constexpr int kModeGunfireWary = 17, kModeCower = 9, kModeFlee = 13, kModePanic = 24;

// Panic is modelled here SOLELY so a regression test can assert this module
// never produces it - Sec20.4's own honest negative: "'Panic' (mode 24) is
// never requested anywhere in this entire chain - it is explicitly guarded
// against being overridden by a pending request, but nothing in the
// gunfire/attack reaction path ever requests it." See
// testPanicIsNeverRequestedAcrossAllInputCombinations below.
enum class PedReactionMode { None, GunfireWary, Cower, Flee, Panic };

struct GunfireReactionResult {
    PedReactionMode mode = PedReactionMode::None;
    // Sec20.4: team==7 heard distant NON-LETHAL gunfire -> "it only speaks a
    // line and posts a 250-750ms delayed re-check (kind 67) - no mode
    // change." Posting the real delayed re-check is mode_table.h's own
    // postEvent/randomRangeMs job (same 250-750ms idiom cited there), not
    // duplicated here - this flag just signals that the caller should do so.
    bool spokeLineOnly = false;
};

// 0x004eaae0's own "prefer Flee" boolean plus the real Flee/Cower
// eligibility gates (0x004eaa40, 0x004d8ff0 - "the same helper Flee's/
// Cower's own begin functions already use") are DOWNSTREAM machinery
// Sec20.4 explicitly says "this investigation did not need to re-read" -
// out of THIS file's scope, the SAME "nothing to wrap" discipline
// navpoint_selector.h's own cover_node note applies to an algorithm with no
// traced control flow to port. Caller supplies the already-resolved facts.
struct FleeOrCowerChoice {
    bool preferFlee = true;    // OPEN: the real "prefer Flee" formula is not given by Sec20.4 - CHOSEN default (Flee tried first, matching the spec's own "either...or" ordering)
    bool fleeEligible = true;  // OPEN: 0x004eaa40's own full eligibility check, not re-read this pass
    bool cowerEligible = true;  // OPEN: 0x004d8ff0, ditto
};

// Sec20.4: "a failed Flee falls through to attempt Cower, not mutually
// exclusive."
inline PedReactionMode ChooseFleeOrCower(const FleeOrCowerChoice& choice) {
    if (choice.preferFlee) {
        if (choice.fleeEligible) return PedReactionMode::Flee;
        if (choice.cowerEligible) return PedReactionMode::Cower;
        return PedReactionMode::None;
    }
    if (choice.cowerEligible) return PedReactionMode::Cower;
    if (choice.fleeEligible) return PedReactionMode::Flee;
    return PedReactionMode::None;
}

// The full CONFIRMED chain for event kind 76 ("gunfire heard"/"shot at")
// AND kind 12 ("attacked"/distress, always lethal-flagged, via a different
// producer 0x004eb920 but the SAME downstream 0x004eaf80 entry point -
// modelled here by the caller forcing isLethalOrAttacked=true for that
// case): kind 76 -> 0x00512680 -> 0x005113e0 -> case 0x00511b1f -> resolve
// shooter (0x004e4ce0) -> 0x00950550 same-entity guard (the TRUE/same-entity
// branch is a pure no-op, not separately represented - only the FALSE
// continuation is modelled) -> 0x004ebb00's no-op gates -> 0x00943ea0
// (NOT-follower AND team==7): FALSE -> pre-hook + REQUEST MODE 17 "Gunfire
// wary" UNCONDITIONALLY (0x004e4130(human,0x11)); TRUE -> crime/witness
// notify attempt (not modelled - out of scope) + resolve weapon lethality +
// 0x004eaf80(human, shooter, isLethal, 0, 1): team==7 and NOT
// lethal/attacked -> speak a line + post delayed re-check only; otherwise
// gate on flee/cower eligibility and call 0x004eaae0 (ChooseFleeOrCower
// above).
inline GunfireReactionResult EvaluateGunfireReaction(const NearFieldPed& ped, bool isLethalOrAttacked,
                                                      const FleeOrCowerChoice& chooserFacts = {}) {
    GunfireReactionResult result;
    // 0x004ebb00: "no-ops on down/dead/ignore_attacks/combat_disabled/dont_react_to_player"
    if (ped.downOrDead || ped.ignoreAttacks || ped.combatDisabled || ped.dontReactToPlayer) return result;

    // 0x00943ea0: NOT-follower AND team==7
    const bool civilianNotFollower = !ped.isFollower && ped.team == kCivilianTeamByte;
    if (!civilianNotFollower) {
        result.mode = PedReactionMode::GunfireWary;  // unconditional REQUEST MODE 17, Sec20.4
        return result;
    }

    if (!isLethalOrAttacked) {
        result.spokeLineOnly = true;  // team==7, heard non-lethal distant gunfire - no mode change
        return result;
    }

    // 0x004eaae0 "guards against clobbering a pending Panic (the only Panic
    // reference in the whole chain, never a request)" - modelled as a pure
    // pass-through here since this file never produces Panic to begin with;
    // see testPanicIsNeverRequestedAcrossAllInputCombinations.
    result.mode = ChooseFleeOrCower(chooserFacts);
    return result;
}

// ===========================================================================
// Sec20.1: the dual-timer throttle gate structure (0x009149a0: "not blocked,
// 'os_ped' toggle on, both registries non-empty, two independent per-
// registry elapsed-time checks both pass"). See file header item 4: unlike
// the vehicle sibling's own CONFIRMED 200-300ms/400-600ms literals, NEITHER
// numeric range is given here - this driver models ONE randomized gate with
// caller-supplied bounds rather than inventing two.
// ===========================================================================
class NearFieldPedDriver {
public:
    explicit NearFieldPedDriver(std::function<int64_t(int64_t, int64_t)> randomRangeMs =
                                     [](int64_t lo, int64_t hi) { return (lo + hi) / 2; })
        : randomRangeMs_(std::move(randomRangeMs)) {
        // Fires immediately on the first real tick - same "a freshly-booted
        // population wants to fill right away" precedent
        // NearFieldVehicleDriver's own constructor documents.
        tickTimer_.arm(0, 0);
    }

    // Sec20.1: "checks 'os_ped' toggle array 0x026093a4..+7, then Sidewalks
    // load-gate 0x00bbdd90" (sidewalksLoaded models that singleton-loaded
    // check - a caller-supplied fact, this file owns no zone-loading state,
    // same scope split every other sr3airuntime driver uses). tickMinMs/
    // tickMaxMs: caller-supplied (file header item 4 - no CONFIRMED literal
    // to bake in).
    bool ShouldRunTick(int64_t nowMs, const AmbientToggleState& toggles, bool sidewalksLoaded, int64_t tickMinMs,
                        int64_t tickMaxMs) {
        if (!sidewalksLoaded) return false;
        if (!toggles.AnyEnabled(ToggleCategory::kPed)) return false;
        if (!tickTimer_.expired(nowMs)) return false;
        tickTimer_.arm(nowMs, randomRangeMs_(tickMinMs, tickMaxMs));
        return true;
    }

private:
    std::function<int64_t(int64_t, int64_t)> randomRangeMs_;
    EngineTimer tickTimer_;
};

// ===========================================================================
// Sec20.1: the combined per-tick spawn pass. SPAWN-ONLY (file header item
// 3 - no despawn predicate exists in this spec for near-field peds).
// Operates on a caller-owned live list - deliberately NOT a global/static
// singleton, the SAME "no hidden process-wide state" discipline every other
// sr3airuntime heavy-pass function applies.
// ===========================================================================
struct NearFieldPedMaintenanceResult {
    int spawned = 0;
};

// `localCap`: Sec20.1's own "local headroom" is never given a numeric
// literal (unlike the vehicle tier's CONFIRMED 48) - CALLER-SUPPLIED, not
// invented here (this project's own no-invented-fixes discipline: a genuine
// decision-fork with no known value is deferred to the caller, not
// fabricated).
inline NearFieldPedMaintenanceResult RunNearFieldPedSpawnPass(std::vector<NearFieldPed>& liveList, size_t localCap,
                                                               int eligibleHumanCount,
                                                               const std::vector<SidewalkSegment>& candidateSegments,
                                                               const NearFieldPedSpawnGates& gates,
                                                               const Vec3& cameraPos,
                                                               const std::function<size_t(size_t)>& pickUniform,
                                                               uint64_t& nextIdInOut) {
    NearFieldPedMaintenanceResult result;
    if (liveList.size() >= localCap) return result;
    // Reused directly from near_field_vehicle_population.h - the SAME
    // shared 150-human ceiling (file header: "the SAME shared 150-human
    // ceiling 0x009086d0 Sec17.4c already documents").
    if (!HasHumanHeadroomForSpawn(eligibleHumanCount)) return result;

    // Sec20.1: "spawn budget = ... x a rate constant" is not a fixed
    // literal (file header item: genuinely OPEN, same shape as the vehicle
    // sibling's own CHOSEN item 3). CHOSEN: this file applies the SAME
    // simplification precedent near_field_vehicle_population.h's own
    // RunNearFieldVehicleMaintenancePass uses for its own unspecified spawn
    // RATE - at most ONE new pedestrian per tick.
    const NearFieldPedSpawnResult r = SelectNearFieldPedSpawn(candidateSegments, gates, cameraPos, pickUniform);
    if (!r.found) return result;

    NearFieldPed np;
    np.id = nextIdInOut++;
    np.pos = r.position;
    np.currentSegmentId = r.segmentId;
    np.onSegment = true;  // the spawn pipeline already resolved a real segment+position - there is nothing left to "snap onto" (unlike a bare-constructed ped, which starts un-snapped at sub-state 13; see NearFieldPed's own comment)
    np.segmentProgress = 0.0;
    np.idleSubState = PedIdleSubState::Sidewalk;  // CHOSEN: already placed on the chosen segment -> steady-state 14, bypassing 13's own "waiting to snap" phase
    liveList.push_back(np);
    result.spawned = 1;
    return result;
}

}  // namespace sr3airuntime
