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
void testStepLaneFollowFullLockSteerAndAlignmentGate() {
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
    CHECK(cmd.steer == 1.0 || cmd.steer == -1.0);  // CONFIRMED full-lock magnitude, never proportional
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
    run("testStepLaneFollowFullLockSteerAndAlignmentGate", testStepLaneFollowFullLockSteerAndAlignmentGate);
    run("testStepLaneFollowThrottleZeroWhenMisaligned", testStepLaneFollowThrottleZeroWhenMisaligned);
    run("testStepLaneFollowCachedQueryThrottle", testStepLaneFollowCachedQueryThrottle);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic traffic-lane-follower tests passed.\n";
    return 0;
}
