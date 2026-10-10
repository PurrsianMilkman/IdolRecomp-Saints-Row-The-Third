// Synthetic tests for the `.czn_pc` Heightmap (0x2244) record reader +
// sampler (sr3zone::Heightmap / ResolveHeightmapCell / SampleHeightmapHeight,
// include/sr3zone/zone_heightmap.h) - spec-zone-data-format.md Sec13.5.
//
// Fixtures are built directly from Sec13.5's own field offsets (the 16-byte
// mini-header, the flat row-major u16 height-sample array) - never by
// calling into src/zone_heightmap.cpp itself - the same discipline
// synthetic_traffic_graph_test.cpp already documents for this project's
// other `.czn_pc`-interior fixtures.
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "sr3zone/zone_heightmap.h"

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

// Builds one Heightmap (0x2244) fixture: a `gridW x gridH` grid, origin
// (originX, originZ), with height samples supplied row-major (width
// fast-varying) by `heights` (must have exactly gridW*gridH entries).
// Returns the full buffer including a 12-byte top-level {tag,length,extra}
// wrapper (for locate() tests) at `wrapperOffset` (0 by default);
// `payloadOffsetOut`, if given, receives the 16-byte mini-header's own
// start (wrapperOffset + 12).
std::vector<uint8_t> BuildHeightmapFixture(int32_t originX, int32_t originZ, uint16_t gridW, uint16_t gridH,
                                            const std::vector<int16_t>& heights, size_t* payloadOffsetOut = nullptr,
                                            size_t wrapperOffset = 0) {
    std::vector<uint8_t> b(wrapperOffset, 0);

    appendU32(b, 0x80000000u | kHeightmapSectionTag);  // tag
    appendU32(b, 0);                                    // length (not validated by this reader)
    appendU32(b, 0);                                    // extra

    const size_t payloadOffset = b.size();
    if (payloadOffsetOut) *payloadOffsetOut = payloadOffset;

    appendU32(b, static_cast<uint32_t>(originX));  // +0x00
    appendU32(b, static_cast<uint32_t>(originZ));  // +0x04
    appendU16(b, gridW);                            // +0x08
    appendU16(b, 0);                                 // +0x0a unused
    appendU16(b, gridH);                             // +0x0c
    appendU16(b, 0);                                 // +0x0e unused
    CHECK(b.size() == payloadOffset + kHeightmapMiniHeaderSize);

    for (int16_t h : heights) appendS16(b, h);
    return b;
}

// A simple 4x3 grid (gridW=4, gridH=3), origin (0,0), spacing 10 - heights
// chosen so row-major indexing (row*gridW+col) is unambiguous: cell
// (col,row) stores value (row*10 + col).
std::vector<int16_t> Simple4x3Heights() {
    std::vector<int16_t> h(12);
    for (uint16_t row = 0; row < 3; ++row)
        for (uint16_t col = 0; col < 4; ++col) h[row * 4 + col] = static_cast<int16_t>(row * 10 + col);
    return h;
}

// ===========================================================================
// parseAt(): mini-header + height array.
// ===========================================================================

void testParseAtRecoversMiniHeaderAndHeights() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildHeightmapFixture(100, -200, 4, 3, Simple4x3Heights(), &payloadOffset);
    ByteView view(buf.data(), buf.size());

    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::Ordinary);
    CHECK(rec.payloadOffset == payloadOffset);
    CHECK(rec.originX == 100);
    CHECK(rec.originZ == -200);
    CHECK(rec.gridW == 4);
    CHECK(rec.gridH == 3);
    CHECK(rec.unusedAt0x0a == 0);
    CHECK(rec.unusedAt0x0e == 0);
    CHECK(rec.containerKind == HeightmapContainerKind::Ordinary);
    CHECK(rec.heights.size() == 12);
    CHECK(rec.heights[0] == 0);       // (col0,row0)
    CHECK(rec.heights[4 * 1 + 2] == 12);  // (col2,row1) -> row*10+col = 12
    CHECK(rec.spacing() == kHeightmapSpacingOrdinary);
}

void testParseAtNegativeHeightsRoundTrip() {
    // Sec13.5's own measured global range is -5..+1,498 - negative values
    // are real, not a theoretical edge case.
    size_t payloadOffset = 0;
    std::vector<int16_t> heights = {-5, -1, 0, 1498};
    std::vector<uint8_t> buf = BuildHeightmapFixture(0, 0, 2, 2, heights, &payloadOffset);
    ByteView view(buf.data(), buf.size());

    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::Ordinary);
    CHECK(rec.heights[0] == -5);
    CHECK(rec.heights[1] == -1);
    CHECK(rec.heights[2] == 0);
    CHECK(rec.heights[3] == 1498);
}

void testParseAtAlwaysLoadedContainerKindSetsSpacing() {
    size_t payloadOffset = 0;
    std::vector<int16_t> heights(8 * 7, 0);
    std::vector<uint8_t> buf = BuildHeightmapFixture(0, 0, 8, 7, heights, &payloadOffset);
    ByteView view(buf.data(), buf.size());

    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::AlwaysLoaded);
    CHECK(rec.containerKind == HeightmapContainerKind::AlwaysLoaded);
    CHECK(rec.spacing() == kHeightmapSpacingAlwaysLoaded);
    CHECK(rec.spacing() == 40.0);
}

void testParseAtHeaderDoesNotFitThrows() {
    std::vector<uint8_t> tiny(8, 0);
    ByteView view(tiny.data(), tiny.size());
    CHECK_THROWS(Heightmap::parseAt(view, 0, HeightmapContainerKind::Ordinary));
}

void testParseAtHeightArrayDoesNotFitThrows() {
    // Declares a 10x10 grid but supplies a buffer only big enough for the
    // 16-byte mini-header - a genuine structural violation.
    std::vector<uint8_t> buf;
    appendU32(buf, 0);
    appendU32(buf, 0);
    appendU16(buf, 10);
    appendU16(buf, 0);
    appendU16(buf, 10);
    appendU16(buf, 0);
    ByteView view(buf.data(), buf.size());
    CHECK_THROWS(Heightmap::parseAt(view, 0, HeightmapContainerKind::Ordinary));
}

// ===========================================================================
// locate(): top-level tag scan.
// ===========================================================================

void testLocateFindsTagAndParses() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildHeightmapFixture(0, 0, 4, 3, Simple4x3Heights(), &payloadOffset);
    ByteView view(buf.data(), buf.size());

    std::vector<HeightmapRecord> found = Heightmap::locate(view, HeightmapContainerKind::Ordinary);
    CHECK(found.size() == 1);
    CHECK(found[0].payloadOffset == payloadOffset);
    CHECK(found[0].heights.size() == 12);
}

void testLocateAtNonZeroWrapperOffset() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf =
        BuildHeightmapFixture(0, 0, 4, 3, Simple4x3Heights(), &payloadOffset, /*wrapperOffset=*/16);
    ByteView view(buf.data(), buf.size());

    std::vector<HeightmapRecord> found = Heightmap::locate(view, HeightmapContainerKind::Ordinary);
    CHECK(found.size() == 1);
    CHECK(found[0].payloadOffset == payloadOffset);
    CHECK(payloadOffset == 16 + 12);
}

void testLocateIgnoresNonMatchingTag() {
    std::vector<uint8_t> buf;
    appendU32(buf, 0x80002237u);  // a DIFFERENT content-kind tag (0x2237, not 0x2244)
    appendU32(buf, 0);
    appendU32(buf, 0);
    buf.resize(buf.size() + 100, 0);
    ByteView view(buf.data(), buf.size());
    CHECK(Heightmap::locate(view, HeightmapContainerKind::Ordinary).empty());
}

void testLocateSkipsInvalidCandidateGracefully() {
    std::vector<uint8_t> buf;
    appendU32(buf, 0x80000000u | kHeightmapSectionTag);
    appendU32(buf, 0);
    appendU32(buf, 0);
    // Too short for even the 16-byte mini-header - locate() must report
    // "none found", never throw.
    buf.resize(buf.size() + 4, 0);
    ByteView view(buf.data(), buf.size());
    bool threw = false;
    std::vector<HeightmapRecord> found;
    try {
        found = Heightmap::locate(view, HeightmapContainerKind::Ordinary);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw);
    CHECK(found.empty());
}

void testLocateEmptyBufferFindsNothing() {
    ByteView empty;
    CHECK(Heightmap::locate(empty, HeightmapContainerKind::Ordinary).empty());
}

// ===========================================================================
// ResolveHeightmapCell(): the confirmed grid-cell resolver formula.
// ===========================================================================

void testResolveCellMatchesConfirmedFormula() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildHeightmapFixture(1000, -500, 4, 3, Simple4x3Heights(), &payloadOffset);
    ByteView view(buf.data(), buf.size());
    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::Ordinary);
    CHECK(rec.spacing() == 10.0);

    // Exactly at the origin -> (col0,row0).
    HeightmapCell c0 = ResolveHeightmapCell(rec, 1000.0, -500.0);
    CHECK(c0.valid && c0.col == 0 && c0.row == 0);

    // One cell over in X, two cells over in Z.
    HeightmapCell c1 = ResolveHeightmapCell(rec, 1000.0 + 10.0, -500.0 + 20.0);
    CHECK(c1.valid && c1.col == 1 && c1.row == 2);

    // Mid-cell (not an exact multiple of spacing) truncates toward the
    // containing cell's own low corner - col = (int)(15/10) = 1, matching
    // the CONFIRMED (int)-cast-of-a-double-division formula exactly (not
    // rounded).
    HeightmapCell c2 = ResolveHeightmapCell(rec, 1000.0 + 15.0, -500.0 + 25.0);
    CHECK(c2.valid && c2.col == 1 && c2.row == 2);

    // Upper boundary, INCLUSIVE: gridW=4, so worldX = origin + 4*spacing
    // resolves to col==gridW==4, which the CONFIRMED bounds check accepts
    // ("boundary-inclusive: col==gridW/row==gridH accepted").
    HeightmapCell c3 = ResolveHeightmapCell(rec, 1000.0 + 4.0 * 10.0, -500.0 + 3.0 * 10.0);
    CHECK(c3.valid && c3.col == 4 && c3.row == 3);

    // Past the boundary: col=5 > gridW=4 -> fails.
    HeightmapCell c4 = ResolveHeightmapCell(rec, 1000.0 + 5.0 * 10.0, -500.0);
    CHECK(!c4.valid);

    // Negative (below origin): CHOSEN rejection (see header comment) - the
    // confirmed formula's own upper-bound-only test does not itself reject
    // this, but this reader's unsigned index type does, defensively.
    HeightmapCell c5 = ResolveHeightmapCell(rec, 1000.0 - 10.0, -500.0);
    CHECK(!c5.valid);
}

void testResolveCellTruncatesTowardZeroNotFloor() {
    // Reproduces the CONFIRMED literal `(int)` cast (truncation toward
    // zero), not floor(), through the ACTUAL reader (not a standalone cast
    // expression) - a genuinely discriminating real case, not merely
    // restating the C++ language rule: a worldX a fraction of one spacing
    // step BELOW the record's own origin. (worldX-originX)/spacing is then
    // a small negative fraction - (int) truncation gives 0 (a valid,
    // in-range cell: this reader's own CHOSEN negative-rejection only
    // rejects a cast result that is ACTUALLY negative), but floor() would
    // give -1 (which this reader's negative-rejection WOULD reject). The
    // two interpretations therefore diverge on whether this query is valid
    // at all, not just on which cell index comes back.
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildHeightmapFixture(1000, -500, 4, 3, Simple4x3Heights(), &payloadOffset);
    ByteView view(buf.data(), buf.size());
    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::Ordinary);

    HeightmapCell c = ResolveHeightmapCell(rec, 1000.0 - 0.5, -500.0);  // -0.05 cells in X
    CHECK(c.valid);   // truncation: (int)(-0.05) == 0, a valid cell - NOT rejected
    CHECK(c.col == 0);
    CHECK(c.row == 0);
}

// ===========================================================================
// SampleHeightmapHeight(): the CHOSEN direct literal read.
// ===========================================================================

void testSampleHeightReturnsExactGridValues() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildHeightmapFixture(0, 0, 4, 3, Simple4x3Heights(), &payloadOffset);
    ByteView view(buf.data(), buf.size());
    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::Ordinary);

    float h = 0.0f;
    CHECK(SampleHeightmapHeight(rec, 0.0, 0.0, h));
    CHECK(h == 0.0f);  // (col0,row0) = row*10+col = 0

    CHECK(SampleHeightmapHeight(rec, 25.0, 15.0, h));  // col = 25/10 = 2, row = 15/10 = 1
    CHECK(h == 12.0f);                                  // row*10+col = 1*10+2 = 12

    CHECK(SampleHeightmapHeight(rec, 35.0, 25.0, h));  // col = 3, row = 2 (last real row/col)
    CHECK(h == 23.0f);
}

void testSampleHeightClampsAtInclusiveBoundary() {
    // gridW=4, gridH=3: the confirmed resolver accepts col==4/row==3 (one
    // PAST the last real index 3/2) - SampleHeightmapHeight() must clamp to
    // the last real cell rather than read past the array (see header
    // comment's own CHOSEN boundary-clamp rationale).
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildHeightmapFixture(0, 0, 4, 3, Simple4x3Heights(), &payloadOffset);
    ByteView view(buf.data(), buf.size());
    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::Ordinary);

    float h = 0.0f;
    CHECK(SampleHeightmapHeight(rec, 40.0, 30.0, h));  // col=4 (==gridW), row=3 (==gridH)
    CHECK(h == 23.0f);                                  // clamped to (col3,row2) = 2*10+3 = 23, the real last cell
}

void testSampleHeightFailsOutsideGrid() {
    size_t payloadOffset = 0;
    std::vector<uint8_t> buf = BuildHeightmapFixture(0, 0, 4, 3, Simple4x3Heights(), &payloadOffset);
    ByteView view(buf.data(), buf.size());
    HeightmapRecord rec = Heightmap::parseAt(view, payloadOffset, HeightmapContainerKind::Ordinary);

    float h = 99.0f;
    CHECK(!SampleHeightmapHeight(rec, 50.0, 0.0, h));   // col=5 > gridW=4
    CHECK(h == 99.0f);                                   // untouched on failure
    CHECK(!SampleHeightmapHeight(rec, -10.0, 0.0, h));  // negative -> rejected
}

void testSampleHeightFailsOnZeroSizedGrid() {
    HeightmapRecord rec;  // default: gridW=gridH=0, empty heights
    float h = 1.0f;
    CHECK(!SampleHeightmapHeight(rec, 0.0, 0.0, h));
    CHECK(h == 1.0f);
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
    run("testParseAtRecoversMiniHeaderAndHeights", testParseAtRecoversMiniHeaderAndHeights);
    run("testParseAtNegativeHeightsRoundTrip", testParseAtNegativeHeightsRoundTrip);
    run("testParseAtAlwaysLoadedContainerKindSetsSpacing", testParseAtAlwaysLoadedContainerKindSetsSpacing);
    run("testParseAtHeaderDoesNotFitThrows", testParseAtHeaderDoesNotFitThrows);
    run("testParseAtHeightArrayDoesNotFitThrows", testParseAtHeightArrayDoesNotFitThrows);
    run("testLocateFindsTagAndParses", testLocateFindsTagAndParses);
    run("testLocateAtNonZeroWrapperOffset", testLocateAtNonZeroWrapperOffset);
    run("testLocateIgnoresNonMatchingTag", testLocateIgnoresNonMatchingTag);
    run("testLocateSkipsInvalidCandidateGracefully", testLocateSkipsInvalidCandidateGracefully);
    run("testLocateEmptyBufferFindsNothing", testLocateEmptyBufferFindsNothing);
    run("testResolveCellMatchesConfirmedFormula", testResolveCellMatchesConfirmedFormula);
    run("testResolveCellTruncatesTowardZeroNotFloor", testResolveCellTruncatesTowardZeroNotFloor);
    run("testSampleHeightReturnsExactGridValues", testSampleHeightReturnsExactGridValues);
    run("testSampleHeightClampsAtInclusiveBoundary", testSampleHeightClampsAtInclusiveBoundary);
    run("testSampleHeightFailsOutsideGrid", testSampleHeightFailsOutsideGrid);
    run("testSampleHeightFailsOnZeroSizedGrid", testSampleHeightFailsOnZeroSizedGrid);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic zone-heightmap tests passed.\n";
    return 0;
}
