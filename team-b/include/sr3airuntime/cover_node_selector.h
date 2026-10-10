#pragma once

// sr3airuntime/cover_node_selector.h - the real cover_node selection
// algorithm: spec-ai-runtime.md Sec15.7, "The real cover_node selection
// algorithm, traced end to end" (2026-10-08), read alongside Sec15/Sec15.1/
// Sec15.3 for context.
//
// WHY THIS IS A SEPARATE FILE FROM navpoint_selector.h. Sec15.3 (Navpoint's
// own destination search) and Sec15.7 (cover_node's own search) are
// "genuinely different selection systems" by the spec's own words - flat
// annulus-filtered uniform-random vs. AABB-gathered/arc-scored/path-query-
// resolved - not two variations on one theme (Sec15.7's own closing
// paragraph, and navpoint_selector.h's own "NOTE ON cover_node's own
// destination-selection algorithm" left this deliberately unfilled for
// exactly this reason). This file is the follow-up unit that fills it.
//
// SOURCE AND CONFIDENCE. Sec15.7: direct, complete disassembly+decompile of
// the core gather/filter/score/pick pipeline (0x0050a690, 1,282
// instructions) and the full arc-test formula (0x008cd1d0) - CONFIRMED.
// HIGH CONFIDENCE but not independently disassembled to their own last
// instruction, per Sec15.7's own text: a scripted-interrupt early-return
// edge case inside 0x005429b0, a "hold position"-only refinement, several
// additive cost sub-terms inside the scoring step's own path/geometry
// refinement, and 0x00ad5020's exact aerial-vehicle-bypass semantics. This
// file implements every CONFIRMED step in full; where Sec15.7 names a term
// or input but does not give its exact coefficients/shape (the scoring
// formula's precise weights; the 5.0-unit "degenerate-proximity special
// case"; the 128/328-unit path-search budget's own applicability), this
// file says so in the comment at that exact point rather than silently
// guessing - the same discipline navpoint_selector.h's own RNG seam
// documents ("the SHAPE is confirmed...the specific SEQUENCE is this
// project's own").
//
// THE STEP-6 REACHABILITY SWITCH - READ THIS BEFORE CHANGING ANYTHING ELSE
// IN THIS FILE. See kCoverPathQueryUseStandIn below (search for "PENDING
// CORRECTED SPEC FROM TEAM A"). Short version, and a correction to this
// file's own earlier working assumption: the "statically dead navmesh
// registry" premise spec-ai-runtime.md Sec14/Sec14.7 raised for this exact
// path query (0x00a12a80, the same one action 45 `pickup weapon` uses,
// Sec15.7 step5) has been REFUTED by a live capture of the real running
// game - the query returned TRUE 10 of 13 times right after NPCs tried to
// pick cover, i.e. the registry IS populated at runtime, not permanently
// zero as the static census alone suggested. Team A's own corrected
// spec-ai-runtime.md Sec15.7 text (what actually populates the registry at
// runtime) has not landed as of this file's writing. Given that, this file
// does NOT hard-code "always unreachable" (refuted) and does NOT hard-code
// "always reachable" either (never confirmed, and Sec15.7's own "picks the
// cheapest PATH-REACHABLE candidate" framing is meaningless if reachability
// is unconditional). Instead, step 6 uses a clearly labelled CHOSEN
// stand-in - straight-line distance within the query's own range, AND no
// collision blocking the straight 2D segment between the human and the
// candidate, reusing this project's own real swept-circle-vs-triangle
// primitives (sr3player::SweepCircleVsTriangleXZ,
// include/sr3player/character_collision.h, added this session by a
// different agent for real `.clmesh_pc` collision - not reinvented here).
// This is a reasonable approximation of "is there plausibly a clear path,"
// NOT the real registry-based mechanism - swap it out the moment Team A's
// corrected Sec15.7 specifies what actually populates the registry. Steps
// 1-5 below (gather, filter, arc test, scoring) do NOT depend on this
// question at all and are implemented for real regardless of how it
// resolves.
//
// SCOPE: THE SELECTION ALGORITHM ONLY, NOT THE ACTION-8 INTEGRATION. Like
// navpoint_selector.h, this file is a standalone, pure-function module - it
// does not reach into decision.h's HumanAiState/Chooser or actions.h's
// ActionWorldFacts/CoverTakeDef routine. Wiring this selector's output into
// action 8 `cover take def`'s own gate (src/ai_actions.cpp's
// action::CoverTakeDef, which currently reads h.world.hasCoverObject/
// f.atOwnCoverPoint as already-resolved facts, the same seam idiom
// actions.h's file comment documents) is a separate future integration
// step, not attempted here - there is no CONFIRMED text describing exactly
// how 0x00542bc0/0x005429b0's own "scripted override, else search" branch
// feeds its result back into the ModeState/ActionBlock fields actions.h
// already models, and guessing that wiring would risk breaking the existing,
// already-CONFIRMED action-8 gate tests.
//
// SYNTHETIC-ONLY. No real .czn_pc cover_node population is read by this
// file or its own tests - CoverNode below is a reduced, already-decoded
// struct (position/yaw/angle-properties as plain doubles), the same "reduced
// to exactly what the algorithm reads" idiom navpoint_selector.h's own
// Navpoint struct uses. A real zone-data bridge (decoding cover_angle_left/
// right/cover_def_angle_left/right and the cover's own world-placement yaw
// off a real sr3zone::ObjectRecord) is NOT built here - sr3zone/
// zone_objects.h's own ObjectPosition() comment states plainly that no
// Navpoint/cover_node orientation decoder exists yet ("nothing this project
// currently builds needs a Navpoint/cover_node's ORIENTATION, only its
// position"); extending that reader is a separate future unit, left OPEN.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3airuntime/navpoint_selector.h"  // reuses DeterministicRng - this file's own RNG seam idiom, not 0x0050a690's untraced generator
#include "sr3airuntime/perception.h"          // PerceptionState, plist::CoverMemory20
#include "sr3player/character_collision.h"    // sr3player::SweepCircleVsTriangleXZ - reused by the step6 CHOSEN stand-in, not reinvented

namespace sr3airuntime {

// --- a simple axis-aligned bounding box (Sec15.7 step1). Deliberately NOT
// the project's existing sr3physics::AABB3 (vehicle_driving_slice.h) - that
// type lives in a different unit's namespace with its own Vec3, has no
// Contains()/ExpandToInclude() of its own, and is scoped to wheel-raycast
// use; a survey of the codebase before writing this file found no
// cross-project shared AABB type to reuse (the same "each unit defines its
// own type" house convention common.h's own file comment documents for
// Vec3/EngineTimer). ---
struct AABB {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};

    bool Contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    void ExpandToInclude(const Vec3& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }
};

// --- angle dequantization (Sec15.7's arc-test paragraph, CONFIRMED exactly):
// "each dequantized from their on-disk byte as (byte/255)*2*PI - PI". ---
inline constexpr double kTwoPi = 6.28318530717958647692;
inline constexpr double kPi = 3.14159265358979323846;

inline double DequantizeAngleByte(uint8_t raw) {
    return (static_cast<double>(raw) / 255.0) * kTwoPi - kPi;
}

// Wraps any angle into [-PI, PI) (same fmod-based convention this project
// already uses in src/player_camera.cpp for the identical formula) - the
// same reference range dequantized cover_angle_*/cover_def_angle_*
// properties live in, needed before comparing a raw atan2() bearing
// (range (-PI, PI]) against them.
inline double WrapAngle(double a) {
    double r = std::fmod(a + kPi, kTwoPi);
    if (r < 0) r += kTwoPi;
    return r - kPi;
}

// The cover's own forward vector from its generic world-placement yaw
// (Sec15.7: "the cover_node's own generic world-placement yaw... the shared
// base-class transform"). Uses the SAME (dx,dz)<->(cos,sin) axis convention
// as the bearing formula below (bearing = atan2(dz,dx)) so that a yaw of 0
// means "facing +X" and the two stay mutually consistent - Sec15.7 does not
// spell out the engine's own axis convention beyond the atan2(dz,dx)
// bearing formula itself, so this is this file's own documented, internally
// consistent choice.
inline Vec3 YawForward(double yaw) { return Vec3{std::cos(yaw), 0.0, std::sin(yaw)}; }

// --- a reduced cover_node, decoded to exactly what Sec15.7's algorithm
// reads (position, world-placement yaw, the 4 angle properties already
// dequantized, the open-cover override fields, the runtime usable flag, and
// an ownership-claim handle) - the same "reduced struct" idiom
// navpoint_selector.h's own Navpoint uses. ---
struct CoverNode {
    uint64_t handle = 0;
    Vec3 position;
    double yaw = 0;  // the cover's own generic world-placement yaw (Sec15.7) - NOT cover_orient (role OPEN/unrelated, Sec15.7)

    // cover_angle_left/right (+0x80/+0x81), already dequantized via
    // DequantizeAngleByte() - primary arc, tested from the cover's own
    // position (Sec15.7).
    double coverAngleLeft = 0, coverAngleRight = 0;
    // cover_def_angle_left/right (+0x82/+0x83), already dequantized -
    // secondary arc, tested from a point 0.45 units behind the cover along
    // its forward vector (Sec15.7).
    double coverDefAngleLeft = 0, coverDefAngleRight = 0;

    // The override: "a separate open-cover flag" plus "a third stored
    // direction" (Sec15.7) - neither is cover_orient (that property's role
    // stays OPEN/unrelated per Sec15.7) and neither is independently named
    // in confirmed text beyond this description, so this file models them
    // as explicit fields a caller/test fixture supplies directly rather
    // than guessing at which other named property they might be.
    bool openCoverOverride = false;
    double openCoverDirection = 0;

    bool isOpenUsable = true;    // the "open/usable runtime flag" (Sec15.7 step3)
    uint64_t claimedBy = 0;      // ownership handle, 0 = unclaimed (Sec15.7 step2/step7: +0x70/+0x74)
};

// --- the arc test, 0x008cd1d0(coverNode, threat, threatPos), read in full
// (Sec15.7). Returns true when `cover` is EXPOSED to a threat at
// `threatPos` (the test's own "fails"/exposed outcome) - i.e. the selection
// evaluator (below) wants this false for a candidate to be considered safe,
// while action 12 CoverFire's own already-ported
// ActionWorldFacts::coverShieldsFromTarget wants the OPPOSITE sense
// (exposure is what lets a human peek out and fire) - both read the same
// underlying CONFIRMED test, just for opposite purposes; this file does not
// change actions.h's own existing coverShieldsFromTarget seam. ---

// AngleWithinArc: Sec15.7 does not state whether cover_angle_left or
// cover_angle_right is numerically the larger bound (nor correspondingly for
// cover_def_angle_left/right), so this takes min/max of the pair - an
// order-independent, documented choice, not an assumed sign convention.
inline bool AngleWithinArc(double relative, double angleA, double angleB) {
    const double lo = std::min(angleA, angleB);
    const double hi = std::max(angleA, angleB);
    return relative >= lo && relative <= hi;
}

inline double BearingTo(const Vec3& from, const Vec3& to) {
    return std::atan2(to.z - from.z, to.x - from.x);
}

// 0.45 units, Sec15.7: "a point pulled back 0.45 units behind the cover
// along its own forward vector".
inline constexpr double kSecondaryArcPullback = 0.45;
// 30 degrees in radians, Sec15.7's override cone.
inline constexpr double kOpenCoverConeHalfAngle = 30.0 * kPi / 180.0;

inline bool CoverArcTestExposed(const CoverNode& cover, const Vec3& threatPos, bool threatIsAerialVehicle) {
    // "An aerial-vehicle threat bypasses the whole arc test" (Sec15.7) -
    // never exposed via this path for such a threat.
    if (threatIsAerialVehicle) return false;

    // Primary arc, from the cover's own position.
    const double bearing = BearingTo(cover.position, threatPos);
    const double relative = WrapAngle(bearing - cover.yaw);
    if (AngleWithinArc(relative, cover.coverAngleLeft, cover.coverAngleRight)) return true;

    // Secondary arc, from a point pulled back along the cover's forward
    // vector.
    const Vec3 forward = YawForward(cover.yaw);
    const Vec3 basePoint = cover.position - forward * kSecondaryArcPullback;
    const double bearing2 = BearingTo(basePoint, threatPos);
    const double relative2 = WrapAngle(bearing2 - cover.yaw);
    bool secondaryExposed = AngleWithinArc(relative2, cover.coverDefAngleLeft, cover.coverDefAngleRight);

    // Override: forces "exposed" when the open-cover flag is set AND the
    // threat falls within a 30-degree cone of the third stored direction
    // (Sec15.7). Apex/reference point for this cone is this file's own
    // documented choice (the cover's own position, the same apex the
    // primary arc already uses) - Sec15.7 names the override's existence
    // and its 30-degree width exactly but does not state which point the
    // cone's apex is measured from.
    if (cover.openCoverOverride) {
        const double coneRelative = WrapAngle(bearing - cover.openCoverDirection);
        if (std::fabs(coneRelative) <= kOpenCoverConeHalfAngle) secondaryExposed = true;
    }

    return secondaryExposed;
}

// --- the per-threat input a caller/host supplies (Sec15.7's "ENTIRE
// perception list, every currently-perceived threat, not just one").
// isLocalPlayer/shieldOk are this file's own injected seam for the
// shield/visibility test Sec15.7 attributes to 0x00509e50 - a raycast/
// render-state query this project's AI runtime unit does not own, the same
// seam idiom actions.h's own ActionWorldFacts uses for comparable
// physics/rendering-domain facts. ---
struct PerceivedThreat {
    uint64_t handle = 0;
    Vec3 position;
    bool isAerialVehicle = false;
    bool isLocalPlayer = false;
    bool playerShieldOk = true;  // meaningless unless isLocalPlayer; see shield/visibility seam note above
};

inline bool AnyPerceivedPlayerThreatFailsShieldGate(const std::vector<PerceivedThreat>& threats) {
    for (const auto& t : threats) {
        if (t.isLocalPlayer && !t.playerShieldOk) return true;
    }
    return false;
}

// The full cover evaluator, 0x00509e50 (Sec15.7 step3/step4's "arc/exposure
// penalty" source and the hard player gate). A candidate must not be
// exposed to ANY perceived (non-aerial) threat to be considered safe - Sec
// 15.7 does not state how multiple simultaneous threats combine into one
// pass/fail verdict; "not exposed to any known threat" is this file's own
// conservative, documented choice, not a disassembly-confirmed aggregation
// rule.
inline bool EvaluateCoverCandidate(const CoverNode& cover, const std::vector<PerceivedThreat>& threats) {
    // Hard player gate, independent of score (Sec15.7): instantly
    // disqualifies regardless of the arc test's own outcome.
    if (AnyPerceivedPlayerThreatFailsShieldGate(threats)) return false;
    for (const auto& t : threats) {
        if (CoverArcTestExposed(cover, t.position, t.isAerialVehicle)) return false;
    }
    return true;
}

// --- Sec15.7 step1: the AABB search volume. ---
inline constexpr double kCoverSearchBaseRadius = 14.0;    // non-brute
inline constexpr double kCoverSearchBruteRadius = 15.0;   // ">=15.0 for brute-class enemies" (Sec15.7)
inline constexpr double kCoverSearchVerticalHalfHeight = 4.0;  // fixed (Sec15.7)

// The human/searcher's own inputs (Sec15.7's own "Inputs to the overall
// selection" paragraph) - this file's reduced, decoupled-from-HumanAiState
// struct, matching this file's own standalone-module scope note above.
struct CoverSearchHuman {
    Vec3 position;
    bool isBrute = false;

    bool hasPostArea = false;
    Vec3 postAreaPosition;
    double postAreaRadius = 0;

    bool isFollower = false;
    bool hasGroupLeader = false;
    Vec3 groupLeaderPosition;
};

// Builds the search AABB (Sec15.7 step1): base radius (widened for brutes),
// fixed vertical half-height, clipped to the post/defend area if set,
// widened toward the group leader's position for followers.
//
// NOT IMPLEMENTED - Sec15.7's own "5.0-unit degenerate-proximity special
// case" (cited in its "Distance/radius limits" paragraph alongside the
// radii this function DOES implement). Its exact control-flow effect is not
// given in the text available to this file (no scratchpad breakdown was
// present in this worktree - see this file's own header comment); rather
// than guess at what it does, this function omits it entirely. Left OPEN.
inline AABB BuildCoverSearchAabb(const CoverSearchHuman& human) {
    const double radius = human.isBrute ? kCoverSearchBruteRadius : kCoverSearchBaseRadius;
    AABB box;
    box.min = Vec3{human.position.x - radius, human.position.y - kCoverSearchVerticalHalfHeight,
                   human.position.z - radius};
    box.max = Vec3{human.position.x + radius, human.position.y + kCoverSearchVerticalHalfHeight,
                   human.position.z + radius};

    // "widened toward the group leader's position for followers" (Sec15.7):
    // this file's own documented reading is "extend the box's own bounds, if
    // necessary, to include the leader's position" - a directional stretch,
    // not a recentre around the leader.
    if (human.isFollower && human.hasGroupLeader) box.ExpandToInclude(human.groupLeaderPosition);

    // "clipped to the human's own post/defend area if one is set" (Sec15.7):
    // intersected in X/Z against a SQUARE footprint of half-width
    // postAreaRadius around postAreaPosition - Sec15.7 does not state the
    // post area's own shape (circle vs. square) or whether it bounds the
    // vertical axis at all, so this is a documented simplification (square,
    // vertical bounds left untouched), not a confirmed shape. A post area
    // far enough away to invert min>max on an axis correctly yields an empty
    // search volume (AABB::Contains() excludes everything), not a crash or a
    // silent ignore.
    if (human.hasPostArea) {
        box.min.x = std::max(box.min.x, human.postAreaPosition.x - human.postAreaRadius);
        box.max.x = std::min(box.max.x, human.postAreaPosition.x + human.postAreaRadius);
        box.min.z = std::max(box.min.z, human.postAreaPosition.z - human.postAreaRadius);
        box.max.z = std::min(box.max.z, human.postAreaPosition.z + human.postAreaRadius);
    }
    return box;
}

// --- Sec15.7 step2: brute-force linear scan of every live cover_node
// instance, filtered by AABB containment and unclaimed status. Deliberately
// a plain O(n) scan over `pool` (NOT a spatial grid/BVH) - Sec15.7's own
// words: "a brute-force linear scan... not a spatial grid". ---
inline std::vector<CoverNode*> GatherCoverCandidates(const AABB& box, std::vector<CoverNode>& pool) {
    std::vector<CoverNode*> out;
    out.reserve(pool.size());
    for (auto& node : pool) {
        if (node.claimedBy != 0) continue;
        if (!box.Contains(node.position)) continue;
        out.push_back(&node);
    }
    return out;
}

// --- Sec15.7 step3: not recently failed (perception list 20), the
// open/usable flag, and the arc/player-gate evaluator. ---
inline std::vector<CoverNode*> FilterCoverCandidates(const std::vector<CoverNode*>& gathered,
                                                      const PerceptionState& perception,
                                                      const std::vector<PerceivedThreat>& threats) {
    std::vector<CoverNode*> out;
    out.reserve(gathered.size());
    for (CoverNode* node : gathered) {
        if (perception.hasEntry(plist::CoverMemory20, node->handle)) continue;  // "not recently failed" (confirmed list index 20)
        if (!node->isOpenUsable) continue;
        if (!EvaluateCoverCandidate(*node, threats)) continue;
        out.push_back(node);
    }
    return out;
}

// --- Sec15.7 step4: scoring. The confirmed SHAPE is "a cost combining an
// arc/exposure penalty, path-reachability/distance terms, and group-
// cohesion penalties, clamped to [0,100] and quantized" - implemented here
// are the three parts the orchestrator's own task framing calls out as real
// and navmesh-independent (arc/exposure, distance, group-cohesion). The
// WEIGHTS below (kCoverScoreWeights' own defaults) are this file's own
// documented choice, not a byte-exact port of the real coefficients (which
// live in the scratchpad breakdown Sec15.7 cites - not present in this
// worktree, see this file's header comment) - same "SHAPE confirmed, exact
// constants this project's own" discipline as navpoint_selector.h's RNG. ---
struct CoverScoreWeights {
    double exposureWeight = 30.0;
    double distanceWeight = 40.0;
    double groupCohesionWeight = 30.0;
};

// How close a threat's bearing sits to the EDGE of the primary safe arc,
// normalised to [0,1] (1 = dead-centre safest, 0 = right at the boundary).
// Used only for survivors (step3 already excludes anyone actually exposed),
// so this grades RELATIVE safety margin among already-safe candidates, per
// Sec15.7's "cost combining an arc/exposure penalty" wording - a graded cost,
// not a second pass/fail gate.
inline double ArcSafetyMargin(const CoverNode& cover, const Vec3& threatPos) {
    const double bearing = BearingTo(cover.position, threatPos);
    const double relative = WrapAngle(bearing - cover.yaw);
    const double lo = std::min(cover.coverAngleLeft, cover.coverAngleRight);
    const double hi = std::max(cover.coverAngleLeft, cover.coverAngleRight);
    const double halfWidth = (hi - lo) / 2.0;
    if (halfWidth <= 1e-9) return 0.0;
    const double center = (hi + lo) / 2.0;
    const double distFromCenter = std::fabs(WrapAngle(relative - center));
    return std::clamp(1.0 - (distFromCenter / halfWidth), 0.0, 1.0);
}

inline double ScoreCoverCandidate(const CoverSearchHuman& human, const CoverNode& cover,
                                   const std::vector<PerceivedThreat>& threats, double searchRadiusUsed,
                                   const CoverScoreWeights& weights = {}) {
    // --- arc/exposure term ---
    double worstMargin = 1.0;
    for (const auto& t : threats) {
        if (t.isAerialVehicle) continue;  // bypasses the arc test entirely (Sec15.7) - no margin contribution
        worstMargin = std::min(worstMargin, ArcSafetyMargin(cover, t.position));
    }
    const double exposurePenalty = (1.0 - worstMargin) * weights.exposureWeight;

    // --- distance term --- (path-reachability folds in as a per-destination
    // bias at the FINAL-PICK step, Sec15.7 step5; this is the plain-distance
    // half of "path-reachability/distance terms" this function can compute
    // without navmesh data).
    const double dist = Distance(human.position, cover.position);
    const double distanceTerm =
        (searchRadiusUsed > 1e-9) ? std::clamp(dist / searchRadiusUsed, 0.0, 1.0) * weights.distanceWeight : 0.0;

    // --- group-cohesion term ---
    double cohesionPenalty = 0.0;
    if (human.isFollower && human.hasGroupLeader) {
        const double leaderDist = Distance(cover.position, human.groupLeaderPosition);
        const double span = searchRadiusUsed * 2.0;
        cohesionPenalty = (span > 1e-9) ? std::clamp(leaderDist / span, 0.0, 1.0) * weights.groupCohesionWeight : 0.0;
    }

    const double raw = exposurePenalty + distanceTerm + cohesionPenalty;
    const double clamped = std::clamp(raw, 0.0, 100.0);
    // "quantized" (Sec15.7) - the real quantization step isn't stated beyond
    // that word; nearest whole point is this file's own documented choice.
    return std::round(clamped);
}

// ===========================================================================
// Sec15.7 step6: final pick via the same multi-destination path-
// reachability query action 45 `pickup weapon` already uses (0x00a12a80).
// ===========================================================================
//
// PENDING CORRECTED SPEC FROM TEAM A - this stand-in may be replaced wholesale.
// A live capture of the real running game REFUTED this task's original
// "statically dead navmesh registry" premise for this exact query: it
// returned TRUE 10 of 13 times right after NPCs tried to pick cover, so the
// registry IS populated at runtime. Team A's corrected spec-ai-runtime.md
// Sec15.7 text (what actually populates it, and how) has not landed as of
// this file's writing. Pending that, kCoverPathQueryUseStandIn (below)
// selects a CHOSEN, clearly-labelled stand-in instead of either hard-coded
// extreme:
//   - reachable  iff  straight-line distance(from,to) is within the query's
//     own range AND no collision blocks the straight 2D (XZ) segment between
//     them;
//   - the collision half reuses this project's own REAL swept-circle-vs-
//     triangle primitive, sr3player::SweepCircleVsTriangleXZ
//     (include/sr3player/character_collision.h, merged into this branch
//     this session from feature/real-clmesh-collision-triangles - not yet
//     in main as of this file's writing) - NOT reinvented here;
//   - this is a REASONABLE APPROXIMATION of "is there plausibly a clear
//     path," NOT the real registry-based mechanism. Swap it out (flip
//     kCoverPathQueryUseStandIn to false and supply the real mechanism via
//     `realPathQuery`) the moment Team A's corrected Sec15.7 specifies what
//     actually populates the registry at runtime - this is the ONE place
//     that decision is made; nothing else in this file reads or branches on
//     it except through CoverCandidateIsPathReachable() below.
inline bool kCoverPathQueryUseStandIn = true;

// The injectable REAL-mechanism seam (same idiom as actions.h's
// ActionWorldFacts and navpoint_selector.h's pickUniform), for once Team A's
// corrected spec lands and kCoverPathQueryUseStandIn is flipped to false. A
// null `realPathQuery` with the switch flipped off reports unreachable - no
// mechanism to confirm reachability is not the same as confirming it.
using CoverPathQueryFn = std::function<bool(const Vec3& from, const Vec3& to)>;

// A real `.clmesh_pc`-shaped collision triangle, in the SAME XZ-plane shape
// sr3player::SweepCircleVsTriangleXZ already consumes (see that function's
// own doc comment for the real data source). This file owns no collision
// geometry DATA - a host or test supplies the relevant triangles (possibly
// none, meaning "no known obstacles" - see CoverStandInSegmentBlocked()).
struct CoverCollisionTriangle {
    double ax = 0, az = 0, bx = 0, bz = 0, cx = 0, cz = 0;
};

// A human's rough horizontal collision radius for the stand-in's own sweep
// test - CHOSEN placeholder (this file's own documented value, not a cited
// spec constant), matching the rough scale sr3player's own character-proxy
// capsule tests use elsewhere in this project.
inline constexpr double kCoverStandInSweepRadius = 0.5;

// True if the straight segment `from`->`to` is blocked by ANY of
// `triangles`, via sr3player::SweepCircleVsTriangleXZ (reused, not
// reinvented). Empty `triangles` (the default) means "no known obstacles,"
// not "assume blocked" - a test that wants to exercise the blocked branch
// supplies at least one triangle that actually intersects the segment.
inline bool CoverStandInSegmentBlocked(const Vec3& from, const Vec3& to,
                                        const std::vector<CoverCollisionTriangle>& triangles) {
    for (const auto& tri : triangles) {
        double outF = 0.0;
        sr3player::Vec3 outNormal;  // unused by this boolean query; sr3player's own Vec3, not this file's
        if (sr3player::SweepCircleVsTriangleXZ(from.x, from.z, to.x, to.z, kCoverStandInSweepRadius, tri.ax, tri.az,
                                                tri.bx, tri.bz, tri.cx, tri.cz, outF, outNormal)) {
            return true;
        }
    }
    return false;
}

// Sec15.7: "Distance/radius limits:...a 128/328-unit path-search budget."
// Two numbers, no statement of which applies in which context. This file's
// CHOSEN stand-in uses kCoverPathSearchBudgetA (the smaller, more
// conservative figure) as its own default "the query's own range" - a
// documented choice, not a resolution of which (if either) the real engine
// actually applies here; a future unit with the missing scratchpad
// breakdown should settle that and update this default if it's wrong.
inline constexpr double kCoverPathSearchBudgetA = 128.0;
inline constexpr double kCoverPathSearchBudgetB = 328.0;  // the alternate figure Sec15.7 also names; not used as a default

// Returns true iff `to` is path-reachable from `from`.
//   - kCoverPathQueryUseStandIn == false: defers to `realPathQuery` (the
//     real, Team-A-specced mechanism, once available); null reports
//     unreachable.
//   - kCoverPathQueryUseStandIn == true (the default): the CHOSEN stand-in
//     above - distance within `rangeLimit` AND no collision in
//     `collisionGeometry` blocking the straight segment.
inline bool CoverCandidateIsPathReachable(const Vec3& from, const Vec3& to, const CoverPathQueryFn& realPathQuery,
                                           const std::vector<CoverCollisionTriangle>& collisionGeometry,
                                           double rangeLimit = kCoverPathSearchBudgetA) {
    if (!kCoverPathQueryUseStandIn) return realPathQuery ? realPathQuery(from, to) : false;
    if (Distance(from, to) > rangeLimit) return false;
    return !CoverStandInSegmentBlocked(from, to, collisionGeometry);
}

// --- Sec15.7 step7: claim via compare-and-set ownership handle (0x008cfeb0).
// This project has no multithreading anywhere in the AI runtime (confirmed
// by a source survey before writing this file), so this is a plain
// single-writer compare-and-set, not an atomic - the same simplification
// this project applies elsewhere when a real engine mechanism exists to
// serialise concurrent writers this reimplementation doesn't have. ---
inline bool TryClaimCoverNode(CoverNode& node, uint64_t claimantHandle) {
    if (node.claimedBy != 0 && node.claimedBy != claimantHandle) return false;
    node.claimedBy = claimantHandle;
    return true;
}

// "blacklists every survivor into the same perception-list slot 20"
// (Sec15.7 step7) - reuses PerceptionState::postStimulus(), the same write
// path every other list-20 entry goes through; list 20's own 12000ms
// lifetime/MostRecentFirst order (perception.h's kPerceptionListMeta[20])
// is what later makes a blacklisted candidate "not recently failed" again
// on its own - this file does not reimplement that expiry.
inline void BlacklistSurvivorsIntoCoverMemory(PerceptionState& perception, const std::vector<CoverNode*>& survivors,
                                               int64_t nowMs) {
    for (CoverNode* node : survivors) {
        perception.postStimulus(plist::CoverMemory20, node->handle, node->position, /*score=*/0.0, /*flags=*/0,
                                 /*kind=*/-1, /*updateMode=*/0, nowMs);
    }
}

// "throttled upstream by a 6-8s random back-off" (Sec15.7 step7) - this file
// computes the duration; arming an actual per-human timer with it is the
// caller's own job (this standalone module owns no HumanAiState timer
// field, per this file's own scope note). `rng` null returns the
// deterministic midpoint (7000ms) rather than silently picking one end of
// the range.
inline int64_t RandomCoverSearchBackoffMs(DeterministicRng* rng) {
    constexpr int64_t kMinMs = 6000, kMaxMs = 8000;
    if (!rng) return (kMinMs + kMaxMs) / 2;
    return kMinMs + static_cast<int64_t>(rng->next(static_cast<size_t>(kMaxMs - kMinMs + 1)));
}

// --- the full pipeline, Sec15.7 steps 1-7 end to end. ---
struct CoverSelectionResult {
    bool found = false;
    uint64_t coverHandle = 0;
    Vec3 coverPosition;
    double score = 0;
    int64_t backoffMs = 0;  // meaningful only when !found (Sec15.7 step7's 6-8s throttle)
    // Every candidate that survived gather+filter (step2/step3), in no
    // particular order - exposed for tests/diagnostics, same idiom as
    // navpoint_selector.h's own SelectionResult::candidates. Copies, not
    // pointers: `pool` is the caller's own vector and may be mutated
    // (claimedBy) or go out of scope independently of this result.
    std::vector<CoverNode> survivors;
};

// `pool`: every live cover_node instance (Sec15.7 step2: "the class's own
//   object pool") - mutated in place when a candidate is claimed.
// `humanHandle`: this searcher's own handle, used as the claim's ownership
//   value.
// `threats`: the human's ENTIRE perceived-threat list (Sec15.7: "not just
//   one").
// `perception`: the human's own PerceptionState - read for list-20 "recently
//   failed" exclusion (step3), written on total failure (step7).
// `realPathQuery`/`collisionGeometry`/`backoffRng`: injected seams, see
//   their own declarations above. `realPathQuery` is only consulted when
//   kCoverPathQueryUseStandIn is flipped to false (not yet, pending Team
//   A's corrected spec); `collisionGeometry` feeds the CHOSEN stand-in's
//   own collision half while that switch stays at its default.
inline CoverSelectionResult SelectCoverNode(const CoverSearchHuman& human, uint64_t humanHandle,
                                             std::vector<CoverNode>& pool, const std::vector<PerceivedThreat>& threats,
                                             PerceptionState& perception, int64_t nowMs,
                                             const CoverPathQueryFn& realPathQuery = nullptr,
                                             const std::vector<CoverCollisionTriangle>& collisionGeometry = {},
                                             DeterministicRng* backoffRng = nullptr) {
    CoverSelectionResult result;

    const AABB box = BuildCoverSearchAabb(human);
    const double radiusUsed = human.isBrute ? kCoverSearchBruteRadius : kCoverSearchBaseRadius;

    const std::vector<CoverNode*> gathered = GatherCoverCandidates(box, pool);
    const std::vector<CoverNode*> survivors = FilterCoverCandidates(gathered, perception, threats);
    result.survivors.reserve(survivors.size());
    for (CoverNode* node : survivors) result.survivors.push_back(*node);

    if (survivors.empty()) {
        // Nothing gathered/filtered through at all - nothing to blacklist,
        // but still throttled the same way (Sec15.7 step7's back-off is
        // upstream of the search, not conditioned on having had candidates).
        result.backoffMs = RandomCoverSearchBackoffMs(backoffRng);
        return result;
    }

    // Step5: score + sort ascending (cheapest first).
    std::vector<std::pair<double, CoverNode*>> scored;
    scored.reserve(survivors.size());
    for (CoverNode* node : survivors) scored.emplace_back(ScoreCoverCandidate(human, *node, threats, radiusUsed), node);
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // Step6: "the cheapest path-reachable candidate" - ONE winner, picked by
    // walking the score-ascending order and taking the first the path query
    // confirms reachable. With kCoverPathQueryUseStandIn at its documented
    // default, "the path query" is the CHOSEN distance+collision stand-in,
    // not the real registry-based mechanism (see that switch's own doc
    // comment above).
    CoverNode* winner = nullptr;
    double winnerScore = 0.0;
    for (auto& entry : scored) {
        if (CoverCandidateIsPathReachable(human.position, entry.second->position, realPathQuery, collisionGeometry)) {
            winner = entry.second;
            winnerScore = entry.first;
            break;
        }
    }

    // Step7: claim the ONE winner. Sec15.7: "on contention or total
    // path-query failure, blacklists every survivor...and returns no cover"
    // - both failure modes (no reachable winner at all, or the one claim
    // attempt on the winner losing contention) take the identical path
    // below, matching "the winner" being singular in the spec's own text
    // (this file does not fall back to the next-cheapest candidate after a
    // contended claim - a documented reading, see this function's own
    // comment history if that turns out wrong once the missing scratchpad
    // breakdown is available).
    if (winner != nullptr && TryClaimCoverNode(*winner, humanHandle)) {
        result.found = true;
        result.coverHandle = winner->handle;
        result.coverPosition = winner->position;
        result.score = winnerScore;
        return result;
    }

    BlacklistSurvivorsIntoCoverMemory(perception, survivors, nowMs);
    result.backoffMs = RandomCoverSearchBackoffMs(backoffRng);
    return result;
}

}  // namespace sr3airuntime
