// Synthetic tests for the `.czn_pc` Traffic (0x2236) record reader
// (sr3zone::TrafficGraph, include/sr3zone/traffic_graph.h) -
// spec-zone-data-format.md Sec13.2.
//
// Fixtures are built directly from Sec13.2's own field offsets (the
// 0x70-byte 14-pair header, the 24-byte segment record, the 64-byte
// extended-segment record, the CHOSEN float3 point-pool stride - see
// traffic_graph.h's own file comment for which parts are CONFIRMED vs
// CHOSEN) - never by calling into src/traffic_graph.cpp itself, the same
// discipline synthetic_zone_objects_test.cpp already documents for this
// project's other `.czn_pc`-interior fixtures.
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "sr3zone/traffic_graph.h"

using namespace sr3zone;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

#define CHECK_THROWS(expr)                                                     \
    do {                                                                       \
        bool threw = false;                                                    \
        try {                                                                  \
            expr;                                                              \
        } catch (const std::exception&) {                                     \
            threw = true;                                                      \
        }                                                                      \
        if (!threw) {                                                         \
            std::cerr << "CHECK FAILED (expected throw): " #expr " at "        \
                      << __FILE__ << ":" << __LINE__ << "\n";                   \
            ++g_failures;                                                      \
        }                                                                      \
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
void appendS16(std::vector<uint8_t>& b, int16_t v) { appendU16(b, static_cast<uint16_t>(v)); }
void appendF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    appendU32(b, bits);
}
void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void appendHeaderPair(std::vector<uint8_t>& b, int32_t count, int32_t rawPointer) {
    appendU32(b, static_cast<uint32_t>(count));
    appendU32(b, static_cast<uint32_t>(rawPointer));
}

void appendExtendedSegmentRecord(std::vector<uint8_t>& b, uint16_t anchorIndex, uint32_t rawField0x1c,
                                  const int16_t offsetA[3], const int16_t offsetB[3], uint16_t cumulativeCount) {
    const size_t start = b.size();
    appendU16(b, anchorIndex);                 // +0x00
    while (b.size() < start + 0x1c) b.push_back(0);
    appendU32(b, rawField0x1c);                 // +0x1c
    while (b.size() < start + 0x24) b.push_back(0);
    for (int i = 0; i < 3; ++i) appendS16(b, offsetA[i]);  // +0x24
    for (int i = 0; i < 3; ++i) appendS16(b, offsetB[i]);  // +0x2a
    while (b.size() < start + 0x3e) b.push_back(0);
    appendU16(b, cumulativeCount);               // +0x3e
    while (b.size() < start + kExtendedSegmentRecordStride) b.push_back(0);
}

void appendSegmentRecord(std::vector<uint8_t>& b, uint32_t fieldAt0x00, int32_t pointOffsetRaw) {
    const size_t start = b.size();
    appendU32(b, fieldAt0x00);                    // +0x00
    appendU32(b, static_cast<uint32_t>(pointOffsetRaw));  // +0x04
    while (b.size() < start + kSegmentRecordStride) b.push_back(0);  // +0x08..+0x17
}

void appendPoint(std::vector<uint8_t>& b, float x, float y, float z) {
    appendF32(b, x);
    appendF32(b, y);
    appendF32(b, z);
}

// Builds the "bend" fixture Sec13.2/spec-ai-runtime.md Sec18 motivate:
// segment 0 runs (0,0,0)->(10,0,0)->(20,0,0), segment 1 runs
// (20,0,0)->(20,0,10) - a real 90-degree turn, not a straight line, so a
// lane-follower walking it visibly changes direction. The junction point
// (20,0,0) is authored TWICE in the point pool (indices 2 and 3) - see
// traffic_lane_follower.h's own "LANE ADJACENCY" note for why this
// reader's own adjacency rule matches on coincident COORDINATES, not
// shared pool indices.
//
// Returns the full buffer, including a 12-byte top-level
// `{tag,length,extra}` wrapper (for locate() tests) at `wrapperOffset`
// (0 by default). `payloadOffsetOut` receives the 0x70-byte header's own
// start (wrapperOffset + 12).
std::vector<uint8_t> BuildBendTrafficFixture(size_t* payloadOffsetOut = nullptr, size_t wrapperOffset = 0) {
    std::vector<uint8_t> b(wrapperOffset, 0);

    appendU32(b, 0x80000000u | kTrafficSectionTag);  // tag
    appendU32(b, 0);                                  // length (not validated by this reader)
    appendU32(b, 0);                                  // extra

    const size_t payloadOffset = b.size();
    if (payloadOffsetOut) *payloadOffsetOut = payloadOffset;

    // --- header: 14 pairs, 8 bytes each ---
    const size_t extendedRel = kTrafficHeaderSize;                                    // 0x70
    const size_t segmentRel = extendedRel + 2 * kExtendedSegmentRecordStride;          // 0x70 + 128 = 0xF0
    const size_t pointPoolRel = segmentRel + 2 * kSegmentRecordStride;                 // 0xF0 + 48 = 0x120

    appendHeaderPair(b, 2, static_cast<int32_t>(extendedRel));  // slot 0: +0x00/+0x04 - extended array
    for (int slot = 1; slot <= 5; ++slot) appendHeaderPair(b, 0, kSelfRelativeNullSentinel);  // slots 1-5: absent
    appendHeaderPair(b, 2, static_cast<int32_t>(segmentRel));    // slot 6: +0x30/+0x34 - segment array
    appendHeaderPair(b, 5, static_cast<int32_t>(pointPoolRel));  // slot 7: +0x38/+0x3c - point pool
    appendHeaderPair(b, 0, kSelfRelativeNullSentinel);            // slot 8: +0x40/+0x44 - active traffic pool (absent)
    appendHeaderPair(b, 0, kSelfRelativeNullSentinel);            // slot 9: +0x48/+0x4c - sidewalks cross-link (absent)
    for (int slot = 10; slot <= 13; ++slot) appendHeaderPair(b, 0, kSelfRelativeNullSentinel);  // slots 10-13: absent

    CHECK(b.size() == payloadOffset + extendedRel);

    // --- extended array (pair at +0x00), 2 records ---
    const int16_t zero3[3] = {0, 0, 0};
    const int16_t offsetA0[3] = {100, 0, 0};
    const int16_t offsetB0[3] = {200, 0, 0};
    appendExtendedSegmentRecord(b, /*anchor=*/0, /*rawField0x1c=*/0, offsetA0, offsetB0, /*cumulativeCount=*/3);
    appendExtendedSegmentRecord(b, /*anchor=*/2, /*rawField0x1c=*/0, zero3, zero3, /*cumulativeCount=*/5);
    CHECK(b.size() == payloadOffset + segmentRel);

    // --- segment array (pair at +0x30), 2 records - +0x04 left as the null
    // sentinel throughout this fixture (this reader does not rely on it for
    // adjacency; see traffic_lane_follower.h) ---
    appendSegmentRecord(b, /*fieldAt0x00=*/0, /*pointOffsetRaw=*/kSelfRelativeNullSentinel);
    appendSegmentRecord(b, /*fieldAt0x00=*/0, /*pointOffsetRaw=*/kSelfRelativeNullSentinel);
    CHECK(b.size() == payloadOffset + pointPoolRel);

    // --- point pool (pair at +0x38), 5 float3 points ---
    appendPoint(b, 0.0f, 0.0f, 0.0f);    // p0
    appendPoint(b, 10.0f, 0.0f, 0.0f);   // p1
    appendPoint(b, 20.0f, 0.0f, 0.0f);   // p2 - end of segment 0
    appendPoint(b, 20.0f, 0.0f, 0.0f);   // p3 - start of segment 1 (SAME coordinates as p2 - the junction)
    appendPoint(b, 20.0f, 0.0f, 10.0f);  // p4 - end of segment 1

    return b;
}

// ===========================================================================
// parseAt(): header pairs.
// ===========================================================================

void testParseAtRecoversHeaderPairs() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    ByteView view(buf.data(), buf.size());

    TrafficRecord rec = TrafficGraph::parseAt(view, payloadOffset);
    CHECK(rec.payloadOffset == payloadOffset);

    const TrafficSubArrayRef& extended = rec.rawPairs[kExtendedSegmentArrayHeaderOffset / 8];
    CHECK(extended.present);
    CHECK(extended.count == 2);
    CHECK(extended.resolvedOffset == payloadOffset + kTrafficHeaderSize);

    const TrafficSubArrayRef& segArr = rec.rawPairs[kSegmentArrayHeaderOffset / 8];
    CHECK(segArr.present);
    CHECK(segArr.count == 2);

    const TrafficSubArrayRef& pool = rec.rawPairs[kPointPoolHeaderOffset / 8];
    CHECK(pool.present);
    CHECK(pool.count == 5);

    // Slots never touched by this fixture are absent, not zero-garbage.
    for (size_t slot : {1u, 2u, 3u, 4u, 5u, 10u, 11u, 12u, 13u}) {
        CHECK(!rec.rawPairs[slot].present);
        CHECK(rec.rawPairs[slot].count == 0);
    }
    CHECK(!rec.activeTrafficPoolRef().present);
    CHECK(!rec.sidewalksCrossLinkRef().present);
}

// ===========================================================================
// parseAt(): segment array (+0x30) and extended-segment array (+0x00).
// ===========================================================================

void testParseAtRecoversSegmentArray() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    ByteView view(buf.data(), buf.size());
    TrafficRecord rec = TrafficGraph::parseAt(view, payloadOffset);

    CHECK(rec.segments.size() == 2);
    for (size_t i = 0; i < 2; ++i) {
        CHECK(rec.segments[i].index == i);
        CHECK(!rec.segments[i].hasPointOffset);  // this fixture leaves +0x04 as the null sentinel
        CHECK(rec.segments[i].pointOffsetRaw == kSelfRelativeNullSentinel);
        for (uint8_t raw : rec.segments[i].rawOpenBytes) CHECK(raw == 0);
    }
}

void testParseAtRecoversExtendedSegmentArray() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    ByteView view(buf.data(), buf.size());
    TrafficRecord rec = TrafficGraph::parseAt(view, payloadOffset);

    CHECK(rec.extendedSegments.size() == 2);
    CHECK(rec.extendedSegments[0].index == 0);
    CHECK(rec.extendedSegments[0].anchorPointIndex == 0);
    CHECK(rec.extendedSegments[0].quantizedOffsetA[0] == 100);
    CHECK(rec.extendedSegments[0].quantizedOffsetB[0] == 200);
    CHECK(rec.extendedSegments[0].cumulativePointCount == 3);

    CHECK(rec.extendedSegments[1].index == 1);
    CHECK(rec.extendedSegments[1].anchorPointIndex == 2);
    CHECK(rec.extendedSegments[1].cumulativePointCount == 5);
}

// ===========================================================================
// TrafficSegmentKind() - bits 28-29 extractor (pure struct-literal test, no
// bytes needed).
// ===========================================================================

void testTrafficSegmentKindExtractsBits2829Only() {
    TrafficSegmentExtended ext;

    ext.rawFieldAt0x1c = 0x0FFFFFFFu;  // every bit EXCEPT 28/29 set
    CHECK(TrafficSegmentKind(ext) == 0);

    ext.rawFieldAt0x1c = 0x10000000u;  // bit 28 only
    CHECK(TrafficSegmentKind(ext) == 1);

    ext.rawFieldAt0x1c = 0x20000000u;  // bit 29 only
    CHECK(TrafficSegmentKind(ext) == 2);

    ext.rawFieldAt0x1c = 0x30000000u;  // both
    CHECK(TrafficSegmentKind(ext) == 3);

    ext.rawFieldAt0x1c = 0xFFFFFFFFu;
    CHECK(TrafficSegmentKind(ext) == 3);
}

// ===========================================================================
// Point pool + CSR range/point-list decode.
// ===========================================================================

void testPointPoolBoundsAndAccess() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    ByteView view(buf.data(), buf.size());
    TrafficRecord rec = TrafficGraph::parseAt(view, payloadOffset);

    CHECK(rec.pointPool.size() == 5);
    TrafficPoint p2 = rec.pointPool.at(2);
    CHECK(p2.x == 20.0f && p2.y == 0.0f && p2.z == 0.0f);
    TrafficPoint p4 = rec.pointPool.at(4);
    CHECK(p4.x == 20.0f && p4.z == 10.0f);
    CHECK_THROWS(rec.pointPool.at(5));
}

void testSegmentCsrRangeAndPointList() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    ByteView view(buf.data(), buf.size());
    TrafficRecord rec = TrafficGraph::parseAt(view, payloadOffset);

    SegmentPointRange r0 = SegmentCsrRange(rec.extendedSegments, 0);
    CHECK(r0.begin == 0 && r0.end == 3);
    SegmentPointRange r1 = SegmentCsrRange(rec.extendedSegments, 1);
    CHECK(r1.begin == 3 && r1.end == 5);

    std::vector<TrafficPoint> poly0 = DecodeSegmentPointList(rec.extendedSegments, 0, rec.pointPool);
    CHECK(poly0.size() == 3);
    CHECK(poly0[0].x == 0.0f);
    CHECK(poly0[1].x == 10.0f);
    CHECK(poly0[2].x == 20.0f);

    std::vector<TrafficPoint> poly1 = DecodeSegmentPointList(rec.extendedSegments, 1, rec.pointPool);
    CHECK(poly1.size() == 2);
    CHECK(poly1[0].x == 20.0f && poly1[0].z == 0.0f);
    CHECK(poly1[1].x == 20.0f && poly1[1].z == 10.0f);

    // The junction: segment 0's last point and segment 1's first point are
    // the SAME physical coordinate, authored at two different pool indices
    // (2 and 3) - see traffic_lane_follower.h's own adjacency note.
    CHECK(poly0.back().x == poly1.front().x);
    CHECK(poly0.back().z == poly1.front().z);

    CHECK(DecodeSegmentPointList(rec.extendedSegments, 99, rec.pointPool).empty());
}

// ===========================================================================
// DecodeQuantizedSegmentPoints() - the second (anchor+int16-offset) codec.
// ===========================================================================

void testDecodeQuantizedSegmentPoints() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    ByteView view(buf.data(), buf.size());
    TrafficRecord rec = TrafficGraph::parseAt(view, payloadOffset);

    // record 0: anchor = pool[0] = (0,0,0); offsetA=(100,0,0), offsetB=(200,0,0); default scale 1.0.
    QuantizedSegmentPoints qp = DecodeQuantizedSegmentPoints(rec.extendedSegments[0], rec.pointPool);
    CHECK(qp.valid);
    CHECK(qp.pointA.x == 100.0f && qp.pointA.y == 0.0f && qp.pointA.z == 0.0f);
    CHECK(qp.pointB.x == 200.0f);

    // A non-default scale multiplies the int16 offsets, not the anchor.
    QuantizedSegmentPoints qpScaled = DecodeQuantizedSegmentPoints(rec.extendedSegments[0], rec.pointPool, 0.5);
    CHECK(std::fabs(qpScaled.pointA.x - 50.0f) < 1e-6f);

    // record 1: anchor index 2 -> pool[2] = (20,0,0); zero offsets -> both points == anchor.
    QuantizedSegmentPoints qp1 = DecodeQuantizedSegmentPoints(rec.extendedSegments[1], rec.pointPool);
    CHECK(qp1.valid);
    CHECK(qp1.pointA.x == 20.0f && qp1.pointB.x == 20.0f);

    // Out-of-range anchor index -> valid=false, never throws.
    TrafficSegmentExtended badAnchor = rec.extendedSegments[0];
    badAnchor.anchorPointIndex = 999;
    QuantizedSegmentPoints qpBad = DecodeQuantizedSegmentPoints(badAnchor, rec.pointPool);
    CHECK(!qpBad.valid);
}

// ===========================================================================
// Structural-violation rejections.
// ===========================================================================

void testHeaderDoesNotFitThrows() {
    std::vector<uint8_t> tiny(20, 0);
    ByteView view(tiny.data(), tiny.size());
    CHECK_THROWS(TrafficGraph::parseAt(view, 0));
}

void testCountMismatchThrows() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    // Corrupt the extended array's own count field (slot 0, +0x00 relative
    // to payloadOffset) so it disagrees with the segment array's count (2).
    putU32At(buf, payloadOffset + kExtendedSegmentArrayHeaderOffset, 3);
    ByteView view(buf.data(), buf.size());
    CHECK_THROWS(TrafficGraph::parseAt(view, payloadOffset));
}

void testNegativeNonSentinelPointerThrows() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    // -2 is negative but not the -1 sentinel - a structural violation.
    putU32At(buf, payloadOffset + kExtendedSegmentArrayHeaderOffset + 4, static_cast<uint32_t>(-2));
    ByteView view(buf.data(), buf.size());
    CHECK_THROWS(TrafficGraph::parseAt(view, payloadOffset));
}

void testResolvedOffsetPastBufferThrows() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset);
    putU32At(buf, payloadOffset + kExtendedSegmentArrayHeaderOffset + 4, 0x7FFFFFFF);
    ByteView view(buf.data(), buf.size());
    CHECK_THROWS(TrafficGraph::parseAt(view, payloadOffset));
}

// ===========================================================================
// locate(): top-level tag scan.
// ===========================================================================

void testLocateFindsTagAndParses() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset, /*wrapperOffset=*/0);
    ByteView view(buf.data(), buf.size());

    std::vector<TrafficRecord> found = TrafficGraph::locate(view);
    CHECK(found.size() == 1);
    CHECK(found[0].payloadOffset == payloadOffset);
    CHECK(found[0].segments.size() == 2);
}

void testLocateAtNonZeroWrapperOffset() {
    size_t payloadOffset = 0;
    // Pad with 16 bytes of unrelated content before the wrapper, to prove
    // locate() does not assume the record starts at byte 0.
    std::vector<uint8_t> buf = BuildBendTrafficFixture(&payloadOffset, /*wrapperOffset=*/16);
    ByteView view(buf.data(), buf.size());

    std::vector<TrafficRecord> found = TrafficGraph::locate(view);
    CHECK(found.size() == 1);
    CHECK(found[0].payloadOffset == payloadOffset);
    CHECK(payloadOffset == 16 + 12);
}

void testLocateIgnoresNonMatchingTag() {
    std::vector<uint8_t> buf;
    appendU32(buf, 0x80002237u);  // a DIFFERENT content-kind tag (0x2237, not 0x2236)
    appendU32(buf, 0);
    appendU32(buf, 0);
    buf.resize(buf.size() + 200, 0);
    ByteView view(buf.data(), buf.size());
    CHECK(TrafficGraph::locate(view).empty());
}

void testLocateSkipsInvalidCandidateGracefully() {
    std::vector<uint8_t> buf;
    appendU32(buf, 0x80000000u | kTrafficSectionTag);
    appendU32(buf, 0);
    appendU32(buf, 0);
    // Too short for even the 0x70-byte header - locate() must report "none
    // found", never throw.
    buf.resize(buf.size() + 10, 0);
    ByteView view(buf.data(), buf.size());
    std::vector<TrafficRecord> found;
    bool threw = false;
    try {
        found = TrafficGraph::locate(view);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw);
    CHECK(found.empty());
}

void testLocateEmptyBufferFindsNothing() {
    ByteView empty;
    CHECK(TrafficGraph::locate(empty).empty());
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
    run("testParseAtRecoversHeaderPairs", testParseAtRecoversHeaderPairs);
    run("testParseAtRecoversSegmentArray", testParseAtRecoversSegmentArray);
    run("testParseAtRecoversExtendedSegmentArray", testParseAtRecoversExtendedSegmentArray);
    run("testTrafficSegmentKindExtractsBits2829Only", testTrafficSegmentKindExtractsBits2829Only);
    run("testPointPoolBoundsAndAccess", testPointPoolBoundsAndAccess);
    run("testSegmentCsrRangeAndPointList", testSegmentCsrRangeAndPointList);
    run("testDecodeQuantizedSegmentPoints", testDecodeQuantizedSegmentPoints);
    run("testHeaderDoesNotFitThrows", testHeaderDoesNotFitThrows);
    run("testCountMismatchThrows", testCountMismatchThrows);
    run("testNegativeNonSentinelPointerThrows", testNegativeNonSentinelPointerThrows);
    run("testResolvedOffsetPastBufferThrows", testResolvedOffsetPastBufferThrows);
    run("testLocateFindsTagAndParses", testLocateFindsTagAndParses);
    run("testLocateAtNonZeroWrapperOffset", testLocateAtNonZeroWrapperOffset);
    run("testLocateIgnoresNonMatchingTag", testLocateIgnoresNonMatchingTag);
    run("testLocateSkipsInvalidCandidateGracefully", testLocateSkipsInvalidCandidateGracefully);
    run("testLocateEmptyBufferFindsNothing", testLocateEmptyBufferFindsNothing);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic traffic-graph tests passed.\n";
    return 0;
}
