// Synthetic tests for the CHOSEN traffic-AI lane-following/graph-hopping
// module (sr3airuntime::TrafficLaneGraph/StepLaneFollow,
// include/sr3airuntime/traffic_lane_follower.h) - spec-ai-runtime.md Sec18,
// read alongside spec-zone-data-format.md Sec13.2.
//
// Fixtures go through sr3zone::TrafficGraph::parseAt() (via
// BuildLaneGraphFixture() below) rather than constructing
// sr3zone::TrafficRecord field-by-field, because sr3zone::TrafficPointPool
// only ever decodes from real backing bytes (no "build from a plain point
// list" convenience exists, deliberately - see traffic_graph.h's own file
// comment on why the point pool's shape is itself a CHOSEN, not
// assumed-trivial, decode). This file's own job is to prove
// TrafficLaneGraph's CHOSEN adjacency/target/junction algorithms, not to
// re-test sr3zone's byte-level parsing (already covered by
// synthetic_traffic_graph_test.cpp).
#include <array>
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
    uint8_t kind = 0;
    std::vector<std::array<float, 3>> points;  // this segment's own CSR point list, in order
};

// Builds a Traffic (0x2236) buffer (12-byte wrapper + payload) for an
// arbitrary set of segments, each with its own kind and CSR point list,
// INTO `outBuf` (cleared first), and parses it - see this file's own
// header comment for why this goes through real bytes rather than struct
// literals. The caller owns `outBuf` and MUST keep it alive for as long as
// the returned TrafficRecord (and anything built from it, e.g. a
// TrafficLaneGraph) is used - TrafficRecord::pointPool holds a ByteView
// into `outBuf`'s own memory, not a copy.
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
        const uint32_t rawField = static_cast<uint32_t>(s.kind & 0x3u) << 28;
        appendExtendedSegmentRecord(b, rawField, cumulative);
    }
    for (size_t i = 0; i < n; ++i) appendSegmentRecord(b);
    for (const auto& s : segs)
        for (const auto& p : s.points) appendPoint(b, p[0], p[1], p[2]);

    ByteView view(b.data(), b.size());
    return TrafficGraph::parseAt(view, payloadOffset);
}

// ===========================================================================
// TrafficLaneGraph: polyline/kind decode and CHOSEN geometric adjacency.
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
// ChooseNextSegment(): kind-preference and the RNG seam's defensive clamp.
// ===========================================================================

void testChooseNextSegmentPrefersKindZero() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {5, 0, 0}}},
        SegSpec{0, {{5, 0, 0}, {10, 0, 0}}},  // kind 0 - the preferred candidate
        SegSpec{1, {{5, 0, 0}, {0, 0, 10}}},  // kind 1 - a junction/branch candidate
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    const std::vector<size_t>& candidates = graph.NextSegments(0);
    CHECK(candidates.size() == 2);  // both segment 1 and segment 2 start at (5,0,0)

    // Every pickUniform response (even an out-of-range one, exercising the
    // defensive clamp) must still resolve to segment 1 - the only kind-0
    // candidate - under this file's own "prefer kind 0" reading.
    for (size_t fakePick : {static_cast<size_t>(0), static_cast<size_t>(1), static_cast<size_t>(999)}) {
        auto chosen = ChooseNextSegment(graph, candidates, [fakePick](size_t) { return fakePick; });
        CHECK(chosen.has_value());
        CHECK(*chosen == 1);
    }
}

void testChooseNextSegmentFallsBackWhenAllNonZeroKind() {
    std::vector<SegSpec> segs = {
        SegSpec{0, {{0, 0, 0}, {5, 0, 0}}},
        SegSpec{1, {{5, 0, 0}, {10, 0, 0}}},
        SegSpec{2, {{5, 0, 0}, {0, 0, 10}}},
    };
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    const std::vector<size_t>& real = graph.NextSegments(0);
    CHECK(real.size() == 2);

    auto chosenFirst = ChooseNextSegment(graph, real, [](size_t) { return static_cast<size_t>(0); });
    CHECK(chosenFirst.has_value());
    CHECK(*chosenFirst == real[0]);

    auto chosenSecond = ChooseNextSegment(graph, real, [](size_t) { return static_cast<size_t>(1); });
    CHECK(chosenSecond.has_value());
    CHECK(*chosenSecond == real[1]);
}

void testChooseNextSegmentEmptyCandidatesReturnsNullopt() {
    // No real graph needed - candidates.empty() short-circuits before
    // `graph` is ever touched. A trivially-empty fixture is enough.
    std::vector<SegSpec> segs;  // zero segments
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    std::vector<size_t> none;
    auto chosen = ChooseNextSegment(graph, none, [](size_t) { return static_cast<size_t>(0); });
    CHECK(!chosen.has_value());
}

// ===========================================================================
// StepLaneFollow(): the full target-point/advance/junction-hop loop,
// end to end - the exact "traces a real segment's point path, not a
// straight line" proof the orchestrator asked for, at unit-test scale.
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

    // A trivial "snap exactly onto the aimed-at point every tick"
    // integrator - sufficient to prove the FOLLOWER's own sequencing
    // (which points it aims at, in which order, when it hops segments),
    // independent of any real vehicle dynamics (the real physics
    // integration is exercised separately by the sr3_viewer
    // `traffic_lane_demo` command, not by this unit test).
    double vx = 0.0, vz = 0.0;
    std::vector<sr3zone::TrafficPoint> targets;
    bool advancedSeen = false;
    bool stuckSeen = false;
    for (int tick = 0; tick < 10 && !stuckSeen; ++tick) {
        LaneFollowCommand cmd =
            StepLaneFollow(graph, state, vx, vz, /*heading=*/0.0, [](size_t) { return static_cast<size_t>(0); });
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
    CHECK(targets.size() == 3);
    CHECK(targets[0].x == 10.0f && targets[0].z == 0.0f);
    CHECK(targets[1].x == 20.0f && targets[1].z == 0.0f);
    CHECK(targets[2].x == 20.0f && targets[2].z == 10.0f);  // the turn - z changes, NOT a straight line in x alone

    // Non-straight-line proof: the visited targets do not all share the
    // same z (a straight run along +X would never leave z==0).
    bool sawNonZeroZ = false;
    for (const auto& t : targets) {
        if (t.z != 0.0f) sawNonZeroZ = true;
    }
    CHECK(sawNonZeroZ);
}

void testStepLaneFollowReportsStuckWhenNeverStarted() {
    std::vector<SegSpec> segs;  // zero segments - segment 0 simply doesn't exist
    std::vector<uint8_t> buf;
    TrafficRecord rec = BuildLaneGraphFixture(segs, buf);
    TrafficLaneGraph graph(rec);

    LaneFollowState state = LaneFollowState::Begin(graph, 0);
    CHECK(!state.valid);

    LaneFollowCommand cmd = StepLaneFollow(graph, state, 0, 0, 0, [](size_t) { return static_cast<size_t>(0); });
    CHECK(cmd.stuck);
    CHECK(cmd.brake == 1.0);
    CHECK(cmd.steer == 0.0);
    CHECK(cmd.throttle == 0.0);
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
    run("testChooseNextSegmentPrefersKindZero", testChooseNextSegmentPrefersKindZero);
    run("testChooseNextSegmentFallsBackWhenAllNonZeroKind", testChooseNextSegmentFallsBackWhenAllNonZeroKind);
    run("testChooseNextSegmentEmptyCandidatesReturnsNullopt", testChooseNextSegmentEmptyCandidatesReturnsNullopt);
    run("testStepLaneFollowTracesBendAndStopsAtDeadEnd", testStepLaneFollowTracesBendAndStopsAtDeadEnd);
    run("testStepLaneFollowReportsStuckWhenNeverStarted", testStepLaneFollowReportsStuckWhenNeverStarted);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic traffic-lane-follower tests passed.\n";
    return 0;
}
