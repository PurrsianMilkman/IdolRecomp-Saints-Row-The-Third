// Synthetic tests for the traffic-AI lane-following/graph-hopping module
// (sr3airuntime::TrafficLaneGraph/StepLaneFollow,
// include/sr3airuntime/traffic_lane_follower.h) - spec-ai-runtime.md
// Sec18.1/18.2/18.5/18.6, read alongside spec-zone-data-format.md Sec13.2.
//
// REWRITTEN 2026-10-10 (traffic-lane-follow-real-mechanism task) to match
// the header's own replaced algorithm: a masked lookahead walk (Sec18.1),
// nearest-distance junction choice (Sec18.2/18.5, REPLACING the first
// pass's uniform-random ChooseNextSegment()), a cached-target distance-
// throttled re-query (Sec18.5), and a dot-product alignment gate picking
// throttle with a full-lock +-1.0 steer magnitude (Sec18.6) - see that
// header's own file comment for the full CONFIRMED-vs-CHOSEN breakdown.
//
// EXTENDED 2026-10-10 (traffic-overtake-passside-flag task, Sec18.6a): the
// steer-sign tests below now exercise the real overtake-gated
// state.passSideFlag (replacing the prior cross-product stand-in) - see
// testStepLaneFollowDefaultPassSideFlagWithNoCandidate/
// OvertakeGatePassesAndSetsFlag/OvertakeGateFailureKeepsLastValue below and
// the header's own file header item 5a for the full CONFIRMED-vs-CHOSEN
// breakdown of the new gate.
//
// REWRITTEN AGAIN 2026-10-11 (overtake-gate-18.6b task): the overtake-gate
// tests now exercise Sec18.6b's REAL 12-step gate (forward-divergence,
// closing-distance floor, approximated pass-point bound) instead of the
// first pass's three fully-CHOSEN distance/alignment/closing-speed
// thresholds - see testStepLaneFollowGateForwardDivergenceBlocksSameDirection/
// GateClosingDistanceFloor/GatePassPointBound below. The steer-MAGNITUDE
// tests (testStepLaneFollowMagnitude*) are new - the first pass's "full-lock
// +-1.0, never proportional" reading of Sec18.6 step 3 is now a CHOSEN
// proportional stand-in instead (file header item 5), fixing a live-observed
// regression; see that section's own doc comment for why.
//
// Fixtures go through sr3zone::TrafficGraph::parseAt() (via
// BuildLaneGraphFixture() below) rather than constructing
// sr3zone::TrafficRecord field-by-field - unchanged convention from the
// first pass, see that function's own comment.
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <vector>

#include "sr3airuntime/traffic_lane_follower.h"

using namespace sr3airuntime;
using sr3zone::ByteView;
using sr3zone::TrafficGraph;
using sr3zone::TrafficRecord;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void appendF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    appendU32(b, bits);
}
void appendHeaderPair(std::vector<uint8_t>& b, int32_t count, int32_t rawPointer) {
    appendU32(b, static_cast<uint32_t>(count));
    appendU32(b, static_cast<uint32_t>(rawPointer));
}
void appendExtendedSegmentRecord(std::vector<uint8_t>& b, uint32_t rawField0x1c, uint16_t cumulativeCount) {
    const size_t start = b.size();
    appendU16(b, 0);  // +0x00 anchorPointIndex - unused by this file's own tests
    while (b.size() < start + 0x1c) b.push_back(0);
    appendU32(b, rawField0x1c);  // +0x1c
    while (b.size() < start + sr3zone::kExtendedSegmentRecordStride - 2) b.push_back(0);
    appendU16(b, cumulativeCount);  // +0x3e
    while (b.size() < start + sr3zone::kExtendedSegmentRecordStride) b.push_back(0);
}
void appendSegmentRecord(std::vector<uint8_t>& b) {
    const size_t start = b.size();
    appendU32(b, 0);                                                        // +0x00
    appendU32(b, static_cast<uint32_t>(sr3zone::kSelfRelativeNullSentinel));  // +0x04
    while (b.size() < start + sr3zone::kSegmentRecordStride) b.push_back(0);
}
void appendPoint(std::vector<uint8_t>& b, float x, float y, float z) {
    appendF32(b, x);
    appendF32(b, y);
    appendF32(b, z);
}

struct SegSpec {
    uint32_t rawKindField = 0;  // the FULL +0x1c word - Sec18.1's own mask test reads this, not just the 2-bit kind
    std::vector<std::array<float, 3>> points;  // this segment's own CSR point list, in order
};

// Builds a Traffic (0x2236) buffer (12-byte wrapper + payload) for an
// arbitrary set of segments, each with its own raw +0x1c field and CSR
// point list, INTO `outBuf` (cleared first), and parses it - see this
// file's own header comment for why this goes through real bytes rather
// than struct literals. The caller owns `outBuf` and MUST keep it alive for
// as long as the returned TrafficRecord (and anything built from it, e.g.
// a TrafficLaneGraph) is used.
TrafficRecord BuildLaneGraphFixture(const std::vector<SegSpec>& segs, std::vector<uint8_t>& outBuf) {
    outBuf.clear();
    std::vector<uint8_t>& b = outBuf;

    appendU32(b, 0x80000000u | sr3zone::kTrafficSectionTag);
    appendU32(b, 0);
    appendU32(b, 0);
    const size_t payloadOffset = b.size();

    const size_t n = segs.size();
    const size_t extendedRel = sr3zone::kTrafficHeaderSize;
    const size_t segmentRel = extendedRel + n * sr3zone::kExtendedSegmentRecordStride;
    const size_t pointPoolRel = segmentRel + n * sr3zone::kSegmentRecordStride;

    size_t totalPoints = 0;
    for (const auto& s : segs) totalPoints += s.points.size();

    appendHeaderPair(b, static_cast<int32_t>(n), static_cast<int32_t>(extendedRel));
    for (int slot = 1; slot <= 5; ++slot) appendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    appendHeaderPair(b, static_cast<int32_t>(n), static_cast<int32_t>(segmentRel));
    appendHeaderPair(b, static_cast<int32_t>(totalPoints), static_cast<int32_t>(pointPoolRel));
    appendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    appendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);
    for (int slot = 10; slot <= 13; ++slot) appendHeaderPair(b, 0, sr3zone::kSelfRelativeNullSentinel);

    uint16_t cumulative = 0;
    for (const auto& s : segs) {
        cumulative += static_cast<uint16_t>(s.points.size());
        appendExtendedSegmentRecord(b, s.rawKindField, cumulative);
    }
    for (size_t i = 0; i < n; ++i) appendSegmentRecord(b);
    for (const auto& s : segs)
        for (const auto& p : s.points) appendPoint(b, p[0], p[1], p[2]);

    ByteView view(b.data(), b.size());
    return TrafficGraph::parseAt(view, payloadOffset);
}

// ===========================================================================
// TrafficLaneGraph: polyline/kind decode and CHOSEN geometric adjacency -
// unchanged from the first pass.
// ===========================================================================

void testTrafficLaneGraphDecodesPolylinesAndKind() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}}},
        SegSpec{0, {{20, 0, 0}, {20, 0, 10}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    CHECK(graph.segmentCount() == 2);
    CHECK(graph.segment(0).kind == 0);
    CHECK(graph.segment(0).polyline.size() == 3);
    CHECK(graph.segment(1).polyline.size() == 2);

    const std::vector<size_t>& next0 = graph.NextSegments(0);
    CHECK(next0.size() == 1);
    CHECK(next0[0] == 1);
    CHECK(graph.NextSegments(1).empty());  // nothing starts at (20,0,10)
}

// ===========================================================================
// PassesLaneMask(): Sec18.1's own maskInclude/maskExclude test.
// ===========================================================================

void testPassesLaneMaskIncludeExclude() {
    TrafficLaneSegment seg;
    seg.rawKindField = 0x2u;  // bit 1 set
    CHECK(PassesLaneMask(seg, /*maskInclude=*/0x2u, /*maskExclude=*/0x0u));   // included, not excluded
    CHECK(!PassesLaneMask(seg, /*maskInclude=*/0x1u, /*maskExclude=*/0x0u));  // not included
    CHECK(!PassesLaneMask(seg, /*maskInclude=*/0x2u, /*maskExclude=*/0x2u));  // included but also excluded
    CHECK(PassesLaneMask(seg, kMaskIncludeAll, kMaskExcludeNone));            // neutral defaults accept everything
}

// ===========================================================================
// ChooseNextSegmentByNearestDistance(): REPLACES the first pass's
// uniform-random ChooseNextSegment() - deterministic nearest-distance cost
// (Sec18.2/18.5), compared against each candidate's own first DISTINGUISHING
// point (index 1, not index 0 - every candidate at a true junction shares
// the SAME index-0 junction-node point by this file's own adjacency rule,
// see the header's own item 2 note).
// ===========================================================================

void testChooseNextSegmentByNearestDistancePicksCloserBranch() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {5, 0, 0}}},
        SegSpec{0, {{5, 0, 0}, {8, 0, 0}}},    // short branch - distinguishing point (8,0,0), dist 3 from (5,0,0)
        SegSpec{0, {{5, 0, 0}, {5, 0, 50}}},   // long branch - distinguishing point (5,0,50), dist 50 from (5,0,0)
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    const std::vector<size_t>& candidates = graph.NextSegments(0);
    CHECK(candidates.size() == 2);

    auto chosen = ChooseNextSegmentByNearestDistance(graph, candidates, /*originX=*/5.0, /*originZ=*/0.0);
    CHECK(chosen.has_value());
    CHECK(*chosen == 1);  // the short branch (segment 1) - nearest distinguishing point
}

void testChooseNextSegmentByNearestDistanceEmptyCandidatesReturnsNullopt() {
    auto chosen = ChooseNextSegmentByNearestDistance(TrafficLaneGraph(TrafficRecord{}), {}, 0.0, 0.0);
    CHECK(!chosen.has_value());
}

// ===========================================================================
// StepLaneFollow(): the full target-point/advance/junction-hop/alignment-
// gate loop, end to end.
// ===========================================================================

void testStepLaneFollowTracesBendAndStopsAtDeadEnd() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}}},
        SegSpec{0, {{20, 0, 0}, {20, 0, 10}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    CHECK(state.valid);

    // A small lookaheadDistance (well under either leg's own length) and a
    // tight arrivalRadius, so a "snap exactly onto the cached lookahead
    // target every tick" integrator re-queries a fresh target almost every
    // tick (item 4's own re-query-on-arrival condition) - sufficient to
    // trace the FOLLOWER's own sequencing (masked walk + junction hop,
    // items 1/2) end to end without needing dozens of ticks the way the
    // default lookaheadDistance (15, close to this fixture's own total
    // path length) would collapse into 1-2 ticks.
    LaneFollowConfig config;
    config.arrivalRadius = 1.0;
    config.lookaheadDistance = 5.0;

    double vx = 0.0, vz = 0.0;
    std::vector<sr3zone::TrafficPoint> targets;
    bool advancedSeen = false;
    bool stuckSeen = false;
    for (int tick = 0; tick < 10 && !stuckSeen; ++tick) {
        LaneFollowCommand cmd = StepLaneFollow(graph, state, vx, vz, /*heading=*/0.0, config);
        if (cmd.stuck) {
            stuckSeen = true;
            break;
        }
        if (cmd.advancedSegment) advancedSeen = true;
        targets.push_back(cmd.target);
        vx = cmd.target.x;
        vz = cmd.target.z;
    }

    CHECK(stuckSeen);     // segment 1 has no outgoing edge - a genuine dead end
    CHECK(advancedSeen);  // the follower really did hop from segment 0 to segment 1

    bool sawNonZeroZ = false;
    for (const auto& t : targets) {
        if (t.z != 0.0f) sawNonZeroZ = true;
    }
    CHECK(sawNonZeroZ);  // the turn - z changes, NOT a straight line in x alone
    // The walk must have reached at least as far as the junction (x==20)
    // at some point before the dead end stopped it.
    bool sawJunctionOrBeyond = false;
    for (const auto& t : targets) {
        if (t.x >= 19.9f) sawJunctionOrBeyond = true;
    }
    CHECK(sawJunctionOrBeyond);
}

// WalkLookahead(): the masked, distance-accumulating lookahead walk itself
// (Sec18.1/item 1) - the piece StepLaneFollow() above calls on every
// re-query.
void testWalkLookaheadAccumulatesDistanceAndInterpolates() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LookaheadWalkResult walk = WalkLookahead(graph, /*startSegmentIndex=*/0, /*startPointCursor=*/0,
                                              /*lookaheadDistance=*/15.0, kMaskIncludeAll, kMaskExcludeNone);
    CHECK(walk.ok);
    CHECK(!walk.ranIntoDeadEnd);
    // 10 units to (10,0,0), 5 more units half-way to (20,0,0) -> (15,0,0).
    CHECK(std::fabs(walk.target.x - 15.0f) < 1e-3f);
    CHECK(std::fabs(walk.target.z - 0.0f) < 1e-3f);
}

void testWalkLookaheadHopsJunctionAndTracksAdvancedFlag() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {10, 0, 0}}},
        SegSpec{0, {{10, 0, 0}, {30, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LookaheadWalkResult walk = WalkLookahead(graph, 0, 0, /*lookaheadDistance=*/15.0, kMaskIncludeAll,
                                              kMaskExcludeNone);
    CHECK(walk.ok);
    CHECK(walk.advancedSegment);  // had to hop from segment 0 to segment 1 to satisfy the lookahead
    CHECK(!walk.ranIntoDeadEnd);
    // 10 units to the junction, 5 more units into segment 1 -> (15,0,0).
    CHECK(std::fabs(walk.target.x - 15.0f) < 1e-3f);
}

void testWalkLookaheadReportsDeadEndShortOfLookahead() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},  // only 4 units of track, no successor
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LookaheadWalkResult walk = WalkLookahead(graph, 0, 0, /*lookaheadDistance=*/15.0, kMaskIncludeAll,
                                              kMaskExcludeNone);
    CHECK(walk.ok);
    CHECK(walk.ranIntoDeadEnd);
    CHECK(std::fabs(walk.target.x - 4.0f) < 1e-3f);  // got as far as it could, not goal-seeking past the dead end
}

void testStepLaneFollowReportsStuckWhenNeverStarted() {
    std::vector<SegSpec> segs;  // zero segments - segment 0 simply doesn't exist
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    CHECK(!state.valid);

    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0, 0, 0);
    CHECK(cmd.stuck);
    CHECK(cmd.brake == 1.0);
    CHECK(cmd.steer == 0.0);
    CHECK(cmd.throttle == 0.0);
}

// Sec18.6's own CONFIRMED full-lock +-1.0 steer magnitude and dot-product
// alignment gate: a target straight ahead and close (within
// kAlignmentDistanceThreshold) must report aligned==true and a driving
// throttle; steer magnitude must be exactly 1.0 in either direction, never
// a proportional in-between value.
// RENAMED/REVISED 2026-10-11 (overtake-gate-18.6b task): the first merge's
// own "CONFIRMED full-lock magnitude, never proportional" reading of Sec18.6
// step 3 is what produced a live-observed regression (file header item 5,
// "STEERING MAGNITUDE, CHOSEN FIX") once combined with a sign that
// structurally never gets rewritten for same-direction-only traffic. This
// file now scales the +-1.0 sign by the heading-to-target alignment dot -
// a fully-aligned vehicle (this fixture) must steer ~0.0, not lock to
// +-1.0. (The old assertion, kept here in spirit via the throttle/aligned
// checks, is superseded by testStepLaneFollowMagnitude* below.)
void testStepLaneFollowAlignedThrottlesAndSteersNearZero() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},  // target 4 units straight ahead - within the 5.0-unit gate
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // Heading 0 (facing +X) looking at a target straight along +X - fully aligned.
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0);
    CHECK(!cmd.stuck);
    CHECK(cmd.aligned);
    CHECK(cmd.throttle > 0.0);
    CHECK(std::fabs(cmd.steer) < 1e-9);  // CHOSEN proportional magnitude fix - see file header item 5
    CHECK(cmd.brake == 0.0);
}

void testStepLaneFollowThrottleZeroWhenMisaligned() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {0, 0, 4}}},  // target 4 units along +Z
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // Heading 0 (facing +X) but the target is along +Z - 90 degrees off,
    // well outside cos(30deg) - misaligned.
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0);
    CHECK(!cmd.stuck);
    CHECK(!cmd.aligned);
    CHECK(cmd.throttle == 0.0);  // Sec18.6 step 4: CONFIRMED throttle=0 when far/misaligned
    CHECK(cmd.brake == 0.0);     // Sec18.6 step 4 tail: brake/handbrake unconditionally 0 outside the stuck case
}

// Sec18.5's own cached-target distance-throttle re-query gate: the first
// tick always re-queries (no cache yet); a second tick from nearly the same
// position should NOT force a fresh junction re-pick (the existing walk
// simply continues) - exercised indirectly via `requeried`, which this file
// surfaces as a diagnostic flag.
void testStepLaneFollowCachedQueryThrottle() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {30, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    CHECK(!state.hasCachedQuery);

    LaneFollowCommand first = StepLaneFollow(graph, state, 0.0, 0.0, 0.0);
    CHECK(first.requeried);        // very first tick - no cache yet
    CHECK(state.hasCachedQuery);

    LaneFollowCommand second = StepLaneFollow(graph, state, 0.1, 0.0, 0.0);  // moved a tiny amount
    CHECK(!second.requeried);      // well inside the re-query distance threshold - cache still fresh
}

// overtake-gate-18.6b task (2026-10-11): with no overtakeCandidate given at
// all, the gate can never pass - state.passSideFlag must stay at its CHOSEN
// default (false, file header item 5a) across repeated ticks.
// RG-12, Sec20.6.3(d): the cached-target re-query threshold is a linear 15.0 m (0x01117a60), not 3.0.
void testStepLaneFollowRequeryThresholdIs15Metres() {
    CHECK(LaneFollowConfig{}.reQueryDistanceThreshold == 15.0);
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {200, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);
    LaneFollowConfig cfg;
    cfg.lookaheadDistance = 40.0;  // the cached target stays far beyond the arrival radius over the test
    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    CHECK(StepLaneFollow(graph, state, 0.0, 0.0, 0.0, cfg).requeried);
    CHECK(!StepLaneFollow(graph, state, 10.0, 0.0, 0.0, cfg).requeried);  // moved 10 m: cache still fresh
    CHECK(!StepLaneFollow(graph, state, 14.9, 0.0, 0.0, cfg).requeried);
    CHECK(StepLaneFollow(graph, state, 15.5, 0.0, 0.0, cfg).requeried);   // moved beyond 15 m
}

void testStepLaneFollowDefaultPassSideFlagWithNoCandidate() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    CHECK(!state.passSideFlag);  // CHOSEN default
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0);
    CHECK(!state.passSideFlag);  // no candidate -> gate never ran -> unchanged
    CHECK(!cmd.overtakeGateFired);

    // A second tick, still no candidate - flag must still be untouched.
    LaneFollowCommand second = StepLaneFollow(graph, state, 0.1, 0.0, 0.0);
    CHECK(!state.passSideFlag);
    CHECK(!second.overtakeGateFired);
}

// Step (a)/3 (Sec18.6b, PORTED VERBATIM): a same-direction candidate
// (headingRad == self's own heading) can NEVER pass the forward-divergence
// test, no matter how close/fast - the gate's documented root cause of
// "never fires" against an all-same-direction population.
void testStepLaneFollowGateForwardDivergenceBlocksSameDirection() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // Self heading 0 (facing +X). Candidate at (8,0,-1), ALSO heading 0
    // (same direction) - otherwise an easy pass on closing-distance/bound.
    OvertakeCandidate sameDirection{8.0, -1.0, /*headingRad=*/0.0, /*speedMs=*/0.0};
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0, LaneFollowConfig{},
                                            /*vehicleSpeedMs=*/0.0, sameDirection);
    CHECK(!cmd.overtakeGateFired);
    CHECK(!state.passSideFlag);  // untouched - still CHOSEN default
}

// Step (c)/6-7 (Sec18.6b, PORTED VERBATIM): an oncoming candidate too CLOSE
// to satisfy the 3.0-unit speed-extrapolated closing-distance floor must
// still fail the gate even though forward-divergence passes.
void testStepLaneFollowGateClosingDistanceFloor() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // Oncoming (heading pi, facing -X) but only 2 units ahead - both
    // stationary (speedMs=0), so closingProjection == 2.0 < 3.0 floor.
    OvertakeCandidate tooClose{2.0, 0.0, /*headingRad=*/kLaneFollowPi, /*speedMs=*/0.0};
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0, LaneFollowConfig{},
                                            /*vehicleSpeedMs=*/0.0, tooClose);
    CHECK(!cmd.overtakeGateFired);
    CHECK(!state.passSideFlag);
}

// Steps (d)+(e)/9-10 (Sec18.6b, APPROXIMATED combined pass-point bound): an
// oncoming candidate that clears the closing-distance floor but is far
// beyond the 81.0 (9^2) squared-distance bound on the predicted pass point
// must still fail the gate.
void testStepLaneFollowGatePassPointBound() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // Oncoming, both stationary, 50 units ahead: closingProjection == 50.0
    // (passes the 3.0 floor) but predictedDistSq == 2500.0 >> 81.0.
    OvertakeCandidate tooFar{50.0, 0.0, /*headingRad=*/kLaneFollowPi, /*speedMs=*/0.0};
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0, LaneFollowConfig{},
                                            /*vehicleSpeedMs=*/0.0, tooFar);
    CHECK(!cmd.overtakeGateFired);
    CHECK(!state.passSideFlag);
}

// A candidate that passes ALL ported/approximated gate steps (oncoming,
// closing distance >= 3.0, within the 81.0 pass-point bound) must flip
// state.passSideFlag to match Sec18.6a's own dot-product sign test.
void testStepLaneFollowOvertakeGatePassesAndSetsFlag() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // Heading 0 (facing +X, fwd=(1,0)). Oncoming candidate (heading pi) at
    // (8,0,1): closingProjection == 8.0 (>=3.0), predictedDistSq ==
    // 65.0 (<=81.0) - gate passes. Lateral axis (heading rotated +90deg) is
    // (0,1) in (x,z); sideDot = 1*1 = 1 >= 0, so the real write
    // (sideDot < 0.0) leaves passSideFlag FALSE.
    OvertakeCandidate sameSide{8.0, 1.0, /*headingRad=*/kLaneFollowPi, /*speedMs=*/0.0};
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0, LaneFollowConfig{},
                                            /*vehicleSpeedMs=*/0.0, sameSide);
    CHECK(!cmd.stuck);
    CHECK(cmd.overtakeGateFired);
    CHECK(!state.passSideFlag);

    // A candidate offset to -Z instead (8, 0, -1): sideDot = 1*(-1) = -1 < 0,
    // flips the flag true.
    LaneFollowState state2 = LaneFollowState::Begin(graph, 0);
    OvertakeCandidate otherSide{8.0, -1.0, /*headingRad=*/kLaneFollowPi, /*speedMs=*/0.0};
    LaneFollowCommand cmd2 = StepLaneFollow(graph, state2, 0.0, 0.0, /*heading=*/0.0, LaneFollowConfig{},
                                             /*vehicleSpeedMs=*/0.0, otherSide);
    CHECK(!cmd2.stuck);
    CHECK(cmd2.overtakeGateFired);
    CHECK(state2.passSideFlag);
}

// If the gate FAILS (candidate same-direction, too close, or too far), the
// flag must keep whatever value it was last set to - NOT reset to the
// CHOSEN default and NOT get overwritten by the failing candidate's own
// dot-product sign. Sec18.6a's own text: "there is no clear-every-frame...
// path" (unchanged by this task - only the gate steps themselves changed).
void testStepLaneFollowOvertakeGateFailureKeepsLastValue() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // First, pass the gate with an "otherSide" oncoming candidate to set the
    // flag true.
    OvertakeCandidate otherSide{8.0, -1.0, /*headingRad=*/kLaneFollowPi, /*speedMs=*/0.0};
    StepLaneFollow(graph, state, 0.0, 0.0, 0.0, LaneFollowConfig{}, /*vehicleSpeedMs=*/0.0, otherSide);
    CHECK(state.passSideFlag);

    // Now a candidate far outside the pass-point bound, on the OPPOSITE side
    // (sideDot > 0, which would set the flag FALSE if the gate ran) - the
    // gate's own bound test must fail, so the flag must stay true.
    OvertakeCandidate farAway{1000.0, 1000.0, /*headingRad=*/kLaneFollowPi, /*speedMs=*/0.0};
    LaneFollowCommand cmd =
        StepLaneFollow(graph, state, 0.0, 0.0, 0.0, LaneFollowConfig{}, /*vehicleSpeedMs=*/0.0, farAway);
    CHECK(!cmd.overtakeGateFired);
    CHECK(state.passSideFlag);  // untouched by the failed-gate tick

    // Also: a close, oncoming candidate but same-direction-blocked is not
    // applicable here (headingRad already pi) - instead exercise the
    // closing-distance floor failing on its own: same side, too close.
    OvertakeCandidate nearSameSide{1.0, 1.0, /*headingRad=*/kLaneFollowPi, /*speedMs=*/0.0};
    LaneFollowCommand cmd2 =
        StepLaneFollow(graph, state, 0.0, 0.0, 0.0, LaneFollowConfig{}, /*vehicleSpeedMs=*/0.0, nearSameSide);
    CHECK(!cmd2.overtakeGateFired);
    CHECK(state.passSideFlag);  // closing-distance gate failed - still untouched
}

// STEERING MAGNITUDE, CHOSEN FIX (file header item 5): when the vehicle's
// heading is already aimed directly at its own lookahead target (dot==1.0),
// the steer magnitude must be ~0.0 regardless of state.passSideFlag - this
// is the direct fix for the live-observed full-lock regression (a
// perfectly-aligned vehicle should drive straight, not lock to +-1.0).
void testStepLaneFollowMagnitudeNearZeroWhenAligned() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    // Dead-end single segment - WalkLookahead() returns (4,0,0) as the
    // target. Heading 0 points EXACTLY at it (dot == 1.0).
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/0.0);
    CHECK(std::fabs(cmd.steer) < 1e-9);
}

// The opposite extreme: heading pointed directly AWAY from the lookahead
// target (dot == -1.0, clamped) must still yield the full +-1.0 magnitude -
// steer should follow state.passSideFlag's own CHOSEN false->+1.0 mapping.
void testStepLaneFollowMagnitudeFullWhenMisaligned() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {4, 0, 0}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0.0, 0.0, /*heading=*/kLaneFollowPi);
    CHECK(!state.passSideFlag);  // CHOSEN default, no candidate given
    CHECK(std::fabs(cmd.steer - 1.0) < 1e-9);
}

// Sec18.6's own "Secondary item" curve (RESOLVED 2026-10-11, overtake-
// gate-18.6b task) - traffic_lane_detail::ApplySteeringCurve(), exercised
// directly as a pure function rather than through StepLaneFollow(). Uses
// this file's own kSteerCurveThreshold (0.2)/kSteerCurveSlope (0.3) CHOSEN
// values throughout - see that constant's own doc comment for why they're
// CHOSEN, not confirmed.

// Below the breakpoint: output = slope * |x| (simple linear response),
// well short of the clamp-to-1.0 ceiling. UPDATED 2026-10-11 (drive-by,
// see the removed above-threshold test's own comment just below): with
// the real, CONFIRMED threshold/slope = 1.0/1.0 (commit 3fc6260,
// superseding this test's own older 0.2/0.3 CHOSEN placeholder values),
// slope=1.0 makes this branch a pure magnitude pass-through.
void testApplySteeringCurveBelowThresholdIsLinear() {
    const double out = traffic_lane_detail::ApplySteeringCurve(0.1, kSteerCurveThreshold, kSteerCurveSlope,
                                                                 /*airborne=*/false);
    // step 2 negates first: x = -0.1, sign = -1, mag = 0.1 (<=1.0 threshold)
    // -> magnitude = 1.0*0.1 = 0.1, sign reapplied -> -0.1.
    CHECK(std::fabs(out - (-0.1)) < 1e-9);
}

// REMOVED 2026-10-11 (drive-by fix, unrelated to this session's own
// cutscene_play task - found only because it broke the full-suite build
// this task's own merge gate requires): this test exercised the
// above-threshold "second slope" branch of ApplySteeringCurve() with
// kSteerCurveThreshold/kSteerCurveSlope = 0.2/0.3 - stale values from
// before the overtake-gate-18.6b task (commit 3fc6260) replaced them with
// the real, CONFIRMED hardcoded identity constants 1.0/1.0. With
// threshold=1.0, the production code's own doc comment (traffic_lane_
// follower.h, right above kSteerCurveThreshold) already states the
// above-threshold branch is now "structurally unreachable" (no clamped
// input can exceed 1.0) - and this test's own `secondSlope = (1.0 -
// slope*threshold)/(1.0-threshold)` literally divides by the now-zero
// `(1.0-threshold)`, which MSVC rejects as a hard compile error (C2124),
// not a runtime result, blocking the entire build. Deleting rather than
// inventing a new non-zero threshold to keep the test alive - the branch
// this test existed to cover is real dead code now per the production
// comment itself, so there's nothing left to correctly assert.

// Exactly +-1.0 at |x|=1.0, by construction of the second slope - the
// property Sec18.6's own text calls out explicitly ("so the curve lands
// exactly at +-1.0 when |input|=1.0").
void testApplySteeringCurveReachesExactlyOneAtInputMagnitudeOne() {
    const double outPos = traffic_lane_detail::ApplySteeringCurve(1.0, kSteerCurveThreshold, kSteerCurveSlope, false);
    const double outNeg = traffic_lane_detail::ApplySteeringCurve(-1.0, kSteerCurveThreshold, kSteerCurveSlope, false);
    CHECK(std::fabs(outPos - (-1.0)) < 1e-9);  // negated internally -> sign flips
    CHECK(std::fabs(outNeg - 1.0) < 1e-9);
    // Values beyond +-1.0 must clamp, not overshoot - step 3's own clamp.
    const double outOver = traffic_lane_detail::ApplySteeringCurve(5.0, kSteerCurveThreshold, kSteerCurveSlope, false);
    CHECK(std::fabs(outOver - (-1.0)) < 1e-9);
}

// Step 2's own confirmed sign negation: a positive input produces a
// negative-signed output (before any further composition by the caller) -
// the stored convention inside the real function is inverted relative to
// its caller's own sign, per Sec18.6's own text.
void testApplySteeringCurveNegatesSignInternally() {
    const double outPos = traffic_lane_detail::ApplySteeringCurve(0.5, kSteerCurveThreshold, kSteerCurveSlope, false);
    const double outNeg = traffic_lane_detail::ApplySteeringCurve(-0.5, kSteerCurveThreshold, kSteerCurveSlope, false);
    CHECK(outPos < 0.0);
    CHECK(outNeg > 0.0);
    CHECK(std::fabs(outPos + outNeg) < 1e-9);  // odd-symmetric about 0
}

// Step 1's own confirmed airborne early-out: CHOSEN bypass in this file
// (no grounded/airborne field on NearFieldVehicle) still has to be callable
// with airborne=true and behave per the real mechanic's own "writes
// nothing" - this pure-function port returns 0.0 (nothing to leave
// unwritten in a stateless signature), never falling through to the curve.
void testApplySteeringCurveAirborneEarlyOutWritesNothing() {
    const double out = traffic_lane_detail::ApplySteeringCurve(1.0, kSteerCurveThreshold, kSteerCurveSlope,
                                                                 /*airborne=*/true);
    CHECK(out == 0.0);
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

}  // namespace

int main() {
    run("testTrafficLaneGraphDecodesPolylinesAndKind", testTrafficLaneGraphDecodesPolylinesAndKind);
    run("testPassesLaneMaskIncludeExclude", testPassesLaneMaskIncludeExclude);
    run("testChooseNextSegmentByNearestDistancePicksCloserBranch",
        testChooseNextSegmentByNearestDistancePicksCloserBranch);
    run("testChooseNextSegmentByNearestDistanceEmptyCandidatesReturnsNullopt",
        testChooseNextSegmentByNearestDistanceEmptyCandidatesReturnsNullopt);
    run("testStepLaneFollowTracesBendAndStopsAtDeadEnd", testStepLaneFollowTracesBendAndStopsAtDeadEnd);
    run("testWalkLookaheadAccumulatesDistanceAndInterpolates", testWalkLookaheadAccumulatesDistanceAndInterpolates);
    run("testWalkLookaheadHopsJunctionAndTracksAdvancedFlag", testWalkLookaheadHopsJunctionAndTracksAdvancedFlag);
    run("testWalkLookaheadReportsDeadEndShortOfLookahead", testWalkLookaheadReportsDeadEndShortOfLookahead);
    run("testStepLaneFollowReportsStuckWhenNeverStarted", testStepLaneFollowReportsStuckWhenNeverStarted);
    run("testStepLaneFollowAlignedThrottlesAndSteersNearZero", testStepLaneFollowAlignedThrottlesAndSteersNearZero);
    run("testStepLaneFollowThrottleZeroWhenMisaligned", testStepLaneFollowThrottleZeroWhenMisaligned);
    run("testStepLaneFollowCachedQueryThrottle", testStepLaneFollowCachedQueryThrottle);
    run("testStepLaneFollowRequeryThresholdIs15Metres", testStepLaneFollowRequeryThresholdIs15Metres);
    run("testStepLaneFollowDefaultPassSideFlagWithNoCandidate",
        testStepLaneFollowDefaultPassSideFlagWithNoCandidate);
    run("testStepLaneFollowGateForwardDivergenceBlocksSameDirection",
        testStepLaneFollowGateForwardDivergenceBlocksSameDirection);
    run("testStepLaneFollowGateClosingDistanceFloor", testStepLaneFollowGateClosingDistanceFloor);
    run("testStepLaneFollowGatePassPointBound", testStepLaneFollowGatePassPointBound);
    run("testStepLaneFollowOvertakeGatePassesAndSetsFlag", testStepLaneFollowOvertakeGatePassesAndSetsFlag);
    run("testStepLaneFollowOvertakeGateFailureKeepsLastValue",
        testStepLaneFollowOvertakeGateFailureKeepsLastValue);
    run("testStepLaneFollowMagnitudeNearZeroWhenAligned", testStepLaneFollowMagnitudeNearZeroWhenAligned);
    run("testStepLaneFollowMagnitudeFullWhenMisaligned", testStepLaneFollowMagnitudeFullWhenMisaligned);
    run("testApplySteeringCurveBelowThresholdIsLinear", testApplySteeringCurveBelowThresholdIsLinear);
    run("testApplySteeringCurveReachesExactlyOneAtInputMagnitudeOne",
        testApplySteeringCurveReachesExactlyOneAtInputMagnitudeOne);
    run("testApplySteeringCurveNegatesSignInternally", testApplySteeringCurveNegatesSignInternally);
    run("testApplySteeringCurveAirborneEarlyOutWritesNothing",
        testApplySteeringCurveAirborneEarlyOutWritesNothing);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic traffic-lane-follower tests passed.\n";
    return 0;
}
