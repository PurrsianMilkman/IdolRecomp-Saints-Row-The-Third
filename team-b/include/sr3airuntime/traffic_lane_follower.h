#pragma once

// sr3airuntime/traffic_lane_follower.h - CHOSEN graph-hopping lane-follow
// logic for ambient traffic AI: spec-ai-runtime.md Sec18 ("Traffic AI
// lane-following: consumer module, pool mechanics, and Havok integration"),
// read alongside spec-zone-data-format.md Sec13.2 (the 0x2236 Traffic
// record this file consumes via sr3zone::TrafficGraph/TrafficRecord,
// sr3zone/traffic_graph.h - not re-parsed here).
//
// WHY THIS FILE IS A CHOSEN STAND-IN, NOT A PORT OF CONFIRMED CODE. Sec18.1/
// Sec18.2 are both explicit, honest negatives: no traced function computes
// a target point/steering angle from a vehicle's position (Sec18.1 -
// "every function opened this pass is pure data-resolution code - none
// reads a vehicle position, heading, or velocity"), and no traced function
// picks which neighbour to advance into at a junction (Sec18.2 - "no RNG
// call, no destination/goal-comparison code, and no lowest-cost-neighbor
// loop was found in any function opened this pass"). This file implements
// the two CHOSEN stand-ins the orchestrator task brief names explicitly for
// exactly these two gaps, in the same documentation convention
// cover_node_selector.h's own step6 CHOSEN stand-in uses (see that file's
// own "THE STEP-6 REACHABILITY SWITCH" banner for the house style this
// follows):
//
//  1. TARGET-POINT COMPUTATION (CHOSEN): "aim at the next graph point along
//     the segment, advance to the next segment at the graph node." A
//     segment's own "points along the segment" are its CONFIRMED CSR point
//     list (sr3zone::DecodeSegmentPointList() - Sec13.2's own "variable-
//     length per-segment 3-D point-list copy"); this file sequentially
//     targets each point in that list, in order, advancing to the next
//     only once the vehicle is within kArrivalRadius of the current one.
//  2. JUNCTION/BRANCH CHOICE (CHOSEN): "pick uniformly at random among
//     valid non-'kind 0'-respecting next segments" - ChooseNextSegment()
//     below, reusing the SAME injected-uniform-pick seam idiom (NOT a
//     bit-exact replay of the real, untraced 0x00dab660 generator)
//     navpoint_selector.h's own SelectNavpointDestination() already uses -
//     see that header's own "THE RNG SEAM" note for why a bit-exact replay
//     of an untraced generator would not be a clean-room port of anything
//     CONFIRMED. "Respecting kind" is this file's OWN documented reading,
//     not a disassembly-confirmed selection rule - see ChooseNextSegment()'s
//     own comment.
//
// LANE ADJACENCY - A THIRD CHOSEN FACT THIS FILE OWNS (sr3zone/
// traffic_graph.h explicitly declines to decide this - see that header's
// own closing note). Sec13.2 confirms the Traffic record is "a genuine
// segment->point cross-reference graph" and Sec18.1 confirms the real
// consumer "hops between graph nodes/segments," but neither states exactly
// which bytes encode segment-to-segment adjacency (pair #7's own +0x04
// field resolves to A point in the pool, never confirmed to specifically
// be "the next segment's own entry point"). This file's CHOSEN reading:
// segment j is adjacent-after segment i iff the LAST point of i's own
// decoded CSR polyline lies within kJunctionEpsilon of the FIRST point of
// j's own decoded CSR polyline - i.e. adjacency is discovered GEOMETRICALLY
// from the already-CONFIRMED-shape/CHOSEN-stride decoded coordinates
// themselves (two segments "meet" at a shared physical point), rather than
// by assuming any particular meaning for pair #7's own pointer field (left
// entirely unconsumed by this file). This is a REASONABLE, clearly-labelled
// approximation of "a segment graph," not the real registry/index-based
// mechanism - the same "reasonable approximation... NOT the real mechanism"
// framing cover_node_selector.h's own step6 stand-in uses. O(N^2) over the
// segment count by construction (a per-pair endpoint-distance check) -
// acceptable for this file's own scope (a CHOSEN stand-in over an honestly
// OPEN algorithm, not a hot real-time path this project claims is correct
// end-to-end); a future unit that traces the real adjacency encoding should
// replace this wholesale, not tune its complexity.
//
// PHYSICS INTEGRATION - THE ONE REAL WRITE SURFACE, SAME SHAPE AS EVERY
// OTHER DRIVER (Sec18.3). Sec18.3 confirms `hk_custom_vehicle_driver_input`
// (spec-physics-runtime.md Sec10.6, sr3physics::ApplyThrottleResponseCurve()
// + the `DriverInput{throttle,brake,steer}` fields
// sr3physics::VehicleSubstepSimulator::Step() already consumes,
// include/sr3physics/vehicle_substeps.h) is the ONE RTTI class in the
// entire binary that converts game-side control fields into Havok's
// analog-input struct - "whatever drives a vehicle (player, AI passenger,
// or ambient traffic) MUST write through these same fields, there is no
// second code path." This file does NOT itself depend on sr3physics
// (matching navpoint_selector.h/cover_node_selector.h's own "standalone,
// decoupled module" convention - see their own file comments) - it
// produces its own small LaneFollowCommand{steer,throttle,brake} struct, in
// the IDENTICAL [-1,1]/[0,1] domains sr3physics::DriverInput already uses,
// and a caller (e.g. a demo/integration site) is expected to construct a
// real sr3physics::DriverInput from it one field at a time and feed it
// into VehicleSubstepSimulator::Step() - the one real integration point
// Sec18.3 confirms exists. The literal write site INSIDE the traffic
// module itself was not located by Sec18.3 ("an honest negative/
// inconclusive result, not 'no such write exists'") - this project's own
// job here is exactly what the orchestrator task brief asks: feed the same
// fields from this file's own lane-following logic via the one real
// integration point that DOES exist.
//
// SYNTHETIC-ONLY, SAME CONVENTION AS navpoint_selector.h/
// cover_node_selector.h. This file's own tests build sr3zone::TrafficRecord
// fixtures from hand-written bytes via sr3zone::TrafficGraph::parseAt() -
// never from real shipped `.czn_pc` content, which this clean-room project
// does not read or possess.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "sr3airuntime/navpoint_selector.h"  // reuses DeterministicRng/MakePickUniform - same RNG-seam idiom, not a second copy
#include "sr3zone/traffic_graph.h"

namespace sr3airuntime {

// Own names (NOT sr3airuntime::kPi/kTwoPi - those belong to
// cover_node_selector.h; a distinct name avoids a duplicate-definition
// error if a future TU includes both headers).
inline constexpr double kLaneFollowPi = 3.14159265358979323846;
inline constexpr double kLaneFollowTwoPi = 6.28318530717958647692;

// CHOSEN (world units): two segment endpoints within this distance of each
// other are considered "the same graph node" by TrafficLaneGraph's own
// adjacency rule - see file header comment.
inline constexpr double kJunctionEpsilon = 0.01;

namespace traffic_lane_detail {
inline double Dist2D(const sr3zone::TrafficPoint& a, const sr3zone::TrafficPoint& b) {
    // Y (vertical) deliberately excluded - this project's own planar
    // vehicle model (sr3physics/vehicle_substeps.h: "ground plane = world
    // X/Z") has no meaningful vertical state to compare against; matches
    // StepLaneFollow() below, which only ever reads/targets X/Z.
    const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
    const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
    return std::sqrt(dx * dx + dz * dz);
}
}  // namespace traffic_lane_detail

// One segment's own decoded lane geometry plus the data this file's CHOSEN
// algorithms need - built once per sr3zone::TrafficRecord by
// TrafficLaneGraph's own constructor, not re-decoded per tick.
struct TrafficLaneSegment {
    size_t index = 0;
    uint8_t kind = 0;  // sr3zone::TrafficSegmentKind() - CONFIRMED extractor (bits 28-29 of +0x1c)
    std::vector<sr3zone::TrafficPoint> polyline;  // sr3zone::DecodeSegmentPointList() - may be empty
};

// Builds the lane graph once from a parsed sr3zone::TrafficRecord: decodes
// every segment's own CSR polyline, then derives CHOSEN adjacency (see
// file header comment) between segments whose endpoints geometrically
// coincide.
class TrafficLaneGraph {
public:
    explicit TrafficLaneGraph(const sr3zone::TrafficRecord& record) {
        segments_.reserve(record.extendedSegments.size());
        for (size_t i = 0; i < record.extendedSegments.size(); ++i) {
            TrafficLaneSegment seg;
            seg.index = i;
            seg.kind = sr3zone::TrafficSegmentKind(record.extendedSegments[i]);
            seg.polyline = sr3zone::DecodeSegmentPointList(record.extendedSegments, i, record.pointPool);
            segments_.push_back(std::move(seg));
        }
        BuildAdjacency();
    }

    size_t segmentCount() const { return segments_.size(); }
    const TrafficLaneSegment& segment(size_t i) const { return segments_.at(i); }

    // CHOSEN adjacency - see file header comment. Empty if `segmentIndex`
    // is out of range or its own polyline is empty (no exit point to match
    // against).
    const std::vector<size_t>& NextSegments(size_t segmentIndex) const {
        static const std::vector<size_t> kEmpty;
        if (segmentIndex >= adjacency_.size()) return kEmpty;
        return adjacency_[segmentIndex];
    }

private:
    void BuildAdjacency() {
        adjacency_.assign(segments_.size(), {});
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (segments_[i].polyline.empty()) continue;
            const sr3zone::TrafficPoint& exitPoint = segments_[i].polyline.back();
            for (size_t j = 0; j < segments_.size(); ++j) {
                if (i == j || segments_[j].polyline.empty()) continue;
                const sr3zone::TrafficPoint& entryPoint = segments_[j].polyline.front();
                if (traffic_lane_detail::Dist2D(exitPoint, entryPoint) <= kJunctionEpsilon) {
                    adjacency_[i].push_back(j);
                }
            }
        }
    }

    std::vector<TrafficLaneSegment> segments_;
    std::vector<std::vector<size_t>> adjacency_;
};

// Picks ONE of `candidates` to advance into at a junction - CHOSEN, see
// file header comment item 2. "Respecting kind": this file's OWN reading -
// if at least one candidate has kind == 0 ("normal" lane continuation per
// spec-zone-data-format.md Sec13.2's own framing), the pick is restricted
// to those, preferring ordinary lane flow over a junction/branch hop when
// both are available; only when EVERY candidate is non-zero-kind does the
// pick fall back to considering all of them. Not a disassembly-confirmed
// selection rule - Sec18.2 is explicit no such rule was found anywhere.
inline std::optional<size_t> ChooseNextSegment(const TrafficLaneGraph& graph, const std::vector<size_t>& candidates,
                                                const std::function<size_t(size_t)>& pickUniform) {
    if (candidates.empty()) return std::nullopt;

    std::vector<size_t> kindZero;
    kindZero.reserve(candidates.size());
    for (size_t c : candidates) {
        if (graph.segment(c).kind == 0) kindZero.push_back(c);
    }
    const std::vector<size_t>& pool = kindZero.empty() ? candidates : kindZero;

    size_t idx = pickUniform(pool.size());
    // Defensive clamp - same discipline navpoint_selector.h's own
    // SelectNavpointDestination() applies to every injected RNG seam.
    if (idx >= pool.size()) idx = 0;
    return pool[idx];
}

// Cursor state for one vehicle following the lane graph.
struct LaneFollowState {
    bool valid = false;  // false once the follower hits a dead end with no next segment (or never started)
    size_t currentSegmentIndex = 0;
    size_t pointCursor = 0;  // index into segment(currentSegmentIndex).polyline

    // Begins at `startSegmentIndex`'s own first polyline point. `valid`
    // stays false if that segment doesn't exist or has no points at all.
    static LaneFollowState Begin(const TrafficLaneGraph& graph, size_t startSegmentIndex) {
        LaneFollowState s;
        if (startSegmentIndex < graph.segmentCount() && !graph.segment(startSegmentIndex).polyline.empty()) {
            s.valid = true;
            s.currentSegmentIndex = startSegmentIndex;
            s.pointCursor = 0;
        }
        return s;
    }
};

// Tunables for the CHOSEN target-point/steering computation - none of
// these are spec-given values (Sec18.1 found no real formula at all); see
// file header comment item 1.
struct LaneFollowConfig {
    double arrivalRadius = 1.5;  // CHOSEN (world units) - a target point counts as "reached" within this distance
    double fullLockHeadingErrorRad = kLaneFollowPi / 4.0;  // CHOSEN - heading error at/beyond which steer saturates to +-1
    double cruiseThrottle = 0.5;          // CHOSEN - base throttle fraction on a straight run
    double minThrottleInSharpTurn = 0.15;  // CHOSEN - throttle floor when heading error is at/beyond fullLockHeadingErrorRad
};

// What this tick's lane-follow step decided - feed steer/throttle/brake
// into a real sr3physics::DriverInput (see file header comment's "PHYSICS
// INTEGRATION" section); the rest is diagnostic/trace-output only.
struct LaneFollowCommand {
    bool stuck = false;            // true at a dead end (no valid next segment) - steer/throttle are 0, brake is 1
    bool advancedSegment = false;  // true the tick the follower hopped segments (diagnostic)
    double steer = 0.0;            // [-1,1] - feed into sr3physics::DriverInput::steer
    double throttle = 0.0;         // [0,1]  - feed into sr3physics::DriverInput::throttle
    double brake = 0.0;            // [0,1]  - feed into sr3physics::DriverInput::brake
    sr3zone::TrafficPoint target;  // the point this tick aimed at (meaningless if `stuck`) - diagnostic/trace output
};

// Computes ONE tick's steering command and advances `state` in place -
// "aim at the next graph point along the segment, advance to the next
// segment at the graph node" (CHOSEN, see file header comment item 1).
//
// `vehicleX`/`vehicleZ`/`vehicleHeadingRad`: the vehicle's own current
// planar pose, in the SAME convention sr3physics::VehicleInstanceState
// already uses ("ground plane = world X/Z... heading... so
// (cos(heading), sin(heading)) is the chassis forward direction") - a
// caller can pass a live VehicleSubstepSimulator's own state_.posX/posZ/
// heading directly, no conversion needed.
//
// `pickUniform`: this file's own RNG seam (navpoint_selector.h's
// DeterministicRng + MakePickUniform(), or any `size_t(size_t)` callable) -
// only ever invoked at a real multi-candidate junction (ChooseNextSegment()
// above).
inline LaneFollowCommand StepLaneFollow(const TrafficLaneGraph& graph, LaneFollowState& state, double vehicleX,
                                         double vehicleZ, double vehicleHeadingRad,
                                         const std::function<size_t(size_t)>& pickUniform,
                                         const LaneFollowConfig& config = LaneFollowConfig{}) {
    LaneFollowCommand cmd;
    if (!state.valid) {
        cmd.stuck = true;
        cmd.brake = 1.0;
        return cmd;
    }

    // Bounded rather than an unconditional loop: a pathological/cyclic
    // fixture (every segment's own single point within kJunctionEpsilon of
    // the next, candidateless beyond that) must be reported as stuck, not
    // spin the caller's own tick forever. Generous on purpose - this is a
    // safety net, not a tuned limit.
    size_t guard = graph.segmentCount() * 2 + 4;
    while (guard-- > 0) {
        const TrafficLaneSegment& seg = graph.segment(state.currentSegmentIndex);

        if (state.pointCursor >= seg.polyline.size()) {
            // Reached the end of this segment's own polyline - hop to the
            // next segment at the graph node.
            const std::vector<size_t>& candidates = graph.NextSegments(state.currentSegmentIndex);
            std::optional<size_t> chosen = ChooseNextSegment(graph, candidates, pickUniform);
            if (!chosen) {
                state.valid = false;
                cmd.stuck = true;
                cmd.brake = 1.0;
                return cmd;
            }
            cmd.advancedSegment = true;
            state.currentSegmentIndex = *chosen;
            // Start at point index 1 of the new segment: index 0 is the
            // SAME physical junction point the vehicle just arrived at
            // (this file's own geometric adjacency rule), so aiming at it
            // again would stall a tick. A new segment with 0 or 1 points
            // simply leaves pointCursor out of range, which the top of
            // this loop's own check handles by hopping again.
            state.pointCursor = 1;
            continue;
        }

        const sr3zone::TrafficPoint& target = seg.polyline[state.pointCursor];
        const double dx = static_cast<double>(target.x) - vehicleX;
        const double dz = static_cast<double>(target.z) - vehicleZ;
        const double dist = std::sqrt(dx * dx + dz * dz);

        if (dist <= config.arrivalRadius) {
            state.pointCursor += 1;
            continue;  // re-evaluate the NEW target (or hop) the same tick
        }

        cmd.target = target;
        // Bearing/heading-error convention matches cover_node_selector.h's
        // own BearingTo()/WrapAngle() exactly (atan2(dz,dx), wrapped to
        // [-pi,pi)) - not reused directly (this file does not include
        // cover_node_selector.h) to keep this module's own dependency
        // surface limited to navpoint_selector.h, per its file comment's
        // "standalone module" convention.
        const double bearing = std::atan2(dz, dx);
        double headingError = bearing - vehicleHeadingRad;
        while (headingError > kLaneFollowPi) headingError -= kLaneFollowTwoPi;
        while (headingError < -kLaneFollowPi) headingError += kLaneFollowTwoPi;

        cmd.steer = std::clamp(headingError / config.fullLockHeadingErrorRad, -1.0, 1.0);
        const double turnSeverity = std::clamp(std::fabs(headingError) / config.fullLockHeadingErrorRad, 0.0, 1.0);
        cmd.throttle = config.cruiseThrottle - turnSeverity * (config.cruiseThrottle - config.minThrottleInSharpTurn);
        cmd.brake = 0.0;
        return cmd;
    }

    // Guard exhausted - reported as stuck rather than spinning forever.
    state.valid = false;
    cmd.stuck = true;
    cmd.brake = 1.0;
    return cmd;
}

}  // namespace sr3airuntime
