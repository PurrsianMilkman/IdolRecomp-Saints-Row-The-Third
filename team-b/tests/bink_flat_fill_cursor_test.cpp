// Dedicated coverage for flat_fill_prestage_array.h's FlatFillPrestageArray/
// FlatFillPrestageCursor and their wiring into block_dispatch.h's
// Case1SubType::FlatDcFill case (spec-bink-codec.md Sec5.3y/Sec5.3z,
// fix/bink-flat-fill-real-decode task, 2026-10-11 - SUPERSEDES this file's
// own prior FlatFillByteCursor coverage, same filename/CMake target kept for
// minimal churn). Synthetic/deterministic only - no real-data fixture
// needed, never skips.
//
// What this test pins down, concretely:
//   1. FlatFillPrestageArray/FlatFillPrestageCursor in isolation: a
//      confirmed-range-aware index cursor (not every index is "available" -
//      see FlatFillPrestageArray::hasIndex()'s own doc comment for exactly
//      why, Sec5.3z's own named LUT-content gap).
//   2. The real bug Sec5.3z's own cross-reference found (SUPERSEDING the
//      prior fix/bink-dc-literal-fillbyte-separate-cursors task's own real
//      bug fix, which this task's Part 3 below still demonstrates
//      unchanged): PRIOR code (before even that task) read the flat-fill
//      literal via `br.readAlignedByte()` directly on the shared main
//      bit-reader - this test builds a hand-crafted bitstream where a
//      FlatDcFill block is immediately followed by a second block whose own
//      dispatch nibble would come out WRONG if the first block had stolen 8
//      bits from `br` (matching Sec5.3y's own live evidence: two
//      consecutive secondary=3 dispatches land on the IDENTICAL
//      main-bitstream bit position). With `flatFillCursor` wired, the
//      second block's dispatch nibble must come out correct; with
//      `flatFillCursor == nullptr` (the legacy fallback), it is expected to
//      desync.

#include <cstdint>
#include <iostream>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/block_dispatch.h"
#include "sr3binkcodec/flat_fill_prestage_array.h"
#include "sr3binkcodec/planar_buffer.h"

using namespace sr3binkcodec;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)
}  // namespace

int main() {
    std::cout << "bink_flat_fill_cursor_test: FlatFillPrestageArray/Cursor + Case1SubType::FlatDcFill wiring\n";

    // ---- Part 1: FlatFillPrestageArray/Cursor in isolation ----
    {
        // A fully-confirmed 4-byte array (synthetic test, confirmedPrefixLen
        // covers the whole thing - unlike the real decode0x100195b0() case,
        // where only a prefix/suffix of a 650-byte array is confirmed).
        std::vector<uint8_t> bytes = {0x11, 0x22, 0x33, 0x44};
        FlatFillPrestageArray array(bytes, /*confirmedPrefixLen=*/4, /*confirmedSuffixLen=*/0);
        FlatFillPrestageCursor cur(&array, /*startIndex=*/1);
        CHECK(cur.cursor() == 1);
        CHECK(cur.hasAtLeast(3));   // indices 1,2,3 all confirmed
        CHECK(!cur.hasAtLeast(4));  // index 4 is out of range
        CHECK(cur.readByte() == 0x22);
        CHECK(cur.cursor() == 2);
        CHECK(cur.readByte() == 0x33);
        CHECK(cur.readByte() == 0x44);
        CHECK(cur.cursor() == 4);
        CHECK(!cur.hasAtLeast(1));  // exhausted (out of range)
        std::cout << "  Part 1 (isolated array/cursor mechanics, fully-confirmed range): OK\n";
    }

    // ---- Part 1b: the CONFIRMED-RANGE honesty check (Sec5.3z's own named
    // LUT-content gap) - an index inside the array's bounds but OUTSIDE the
    // confirmed prefix/suffix must be reported as unavailable, never a
    // fabricated value. ----
    {
        std::vector<uint8_t> bytes(10, 0xEE);  // middle bytes are placeholder, NOT real content
        bytes[0] = 0xAA;
        bytes[1] = 0xBB;
        bytes[8] = 0xCC;
        bytes[9] = 0xDD;
        FlatFillPrestageArray array(bytes, /*confirmedPrefixLen=*/2, /*confirmedSuffixLen=*/2);
        CHECK(array.hasIndex(0));
        CHECK(array.hasIndex(1));
        CHECK(!array.hasIndex(2));  // in range, but NOT confirmed
        CHECK(!array.hasIndex(7));  // in range, but NOT confirmed
        CHECK(array.hasIndex(8));
        CHECK(array.hasIndex(9));
        CHECK(!array.hasIndex(10));  // out of range entirely

        FlatFillPrestageCursor cur(&array, 0);
        CHECK(cur.hasAtLeast(2));   // indices 0,1: confirmed
        CHECK(!cur.hasAtLeast(3));  // index 2 is NOT confirmed - must refuse, not fabricate
        std::cout << "  Part 1b (unconfirmed middle range honestly reported unavailable, Sec5.3z gap): OK\n";
    }

    // ---- Part 2: a SEPARATE array, zero interaction with BitstreamReader ----
    {
        // `br`'s own buffer: a primary=1/sub=3 (FlatDcFill) dispatch,
        // hand-built as raw 4-bit nibbles (br.readBits(4) path - this is a
        // synthetic/hand-built-nibble test, so the legacy direct-nibble
        // dispatch convention applies, matching every other synthetic test
        // in this codebase). primary nibble = 1, sub nibble = 3+3=6 (sub=3
        // after the CONFIRMED "subtract 3").
        // Nibble packing (LSB-first, 2 nibbles/byte): byte0 = (6<<4)|1 = 0x61
        const uint8_t brBytes[4] = {0x61, 0x00, 0x00, 0x00};
        BitstreamReader br(brBytes, sizeof(brBytes));

        // A completely separate flat-fill array with its own bytes - the
        // whole point of Sec5.3y's finding (two physically different
        // buffers) PLUS Sec5.3z's correction (the real buffer is decoded
        // content, not a raw-byte window) - here represented as a
        // fully-confirmed synthetic array, since this part only needs to
        // prove `br` itself is untouched by the fill-byte read.
        std::vector<uint8_t> fillBytes = {0x99};
        FlatFillPrestageArray fillArray(fillBytes, /*confirmedPrefixLen=*/1, /*confirmedSuffixLen=*/0);
        FlatFillPrestageCursor flatFillCursor(&fillArray, 0);

        PlanarYuvFrame frame;
        frame.allocate(FrameDimensions::fromRoundedLuma(16, 16));
        PlaneView dest = frame.luma().view();

        const size_t bitsBefore = br.bitsConsumedSoFar();
        BlockRecord rec = decodeOneBlock(br, dest, nullptr, /*blockRow=*/0, /*blockCol=*/0,
                                          /*caseSelector=*/nullptr, /*caseSelectorB=*/nullptr,
                                          /*dcArray=*/nullptr, &flatFillCursor);
        const size_t bitsAfter = br.bitsConsumedSoFar();

        CHECK(rec.outcome == BlockOutcome::Reconstructed);
        CHECK(rec.primary == PrimaryBlockType::SubDispatch);
        CHECK(rec.sub.has_value() && *rec.sub == Case1SubType::FlatDcFill);
        // Exactly the 2 dispatch nibbles (primary + sub) = 8 bits; nothing
        // more - the fill byte itself must NOT have advanced `br`.
        CHECK(bitsAfter - bitsBefore == 8);
        CHECK(flatFillCursor.cursor() == 1);  // the cursor's OWN position did advance, by exactly 1 index
        // Pixel value must be the SEPARATE array's byte (0x99), not
        // anything read from `br`'s own bytes (0x00 past the dispatch
        // nibbles).
        CHECK(dest.at(0, 0) == 0x99);
        CHECK(dest.at(7, 7) == 0x99);
        std::cout << "  Part 2 (FlatDcFill reads from the SEPARATE decoded array, `br` untouched beyond its "
                     "own dispatch nibbles): "
                  << (rec.outcome == BlockOutcome::Reconstructed && (bitsAfter - bitsBefore) == 8 &&
                              dest.at(0, 0) == 0x99
                          ? "OK"
                          : "FAILED")
                  << "\n";
    }

    // ---- Part 3: the concrete bug the predecessor task fixed, demonstrated
    // directly (unchanged by this task - still a real regression risk if
    // ever reverted) ----
    // Two consecutive blocks: block 0 is FlatDcFill (sub=3), block 1 is
    // ALSO FlatDcFill (sub=3) - mirroring Sec5.3y's own idx1/idx2 real-data
    // pair (both secondary=3, consecutive). With the fix (flatFillCursor
    // wired), block 1's dispatch nibbles are read from EXACTLY where `br`
    // left off after block 0's own 2 dispatch nibbles (8 bits) - i.e. `br`
    // byte 1. Legacy fallback code (exercised here by passing
    // flatFillCursor=nullptr) would have additionally burned a byte-aligned
    // 8 bits for block 0's OWN fill literal, pushing block 1's dispatch
    // read to byte 2 instead - a real desync. This test asserts the FIXED
    // behavior (byte 1) directly, and separately confirms the OLD behavior
    // really would have landed on byte 2, demonstrating the bug the
    // predecessor task closed was real, not hypothetical.
    {
        // byte0 = dispatch nibbles for block0 (primary=1 sub=3 -> 0x61,
        // same packing as Part 2). byte1 = dispatch nibbles for block1
        // (same encoding, 0x61 again). byte2 = a DIFFERENT, deliberately
        // out-of-range nibble pair the OLD buggy code would land on instead
        // (0xFF -> primary nibble 15, out of range) - if the fix is
        // present, block1 must NOT see this.
        const uint8_t brBytes[4] = {0x61, 0x61, 0xFF, 0x00};

        PlanarYuvFrame frame3;
        frame3.allocate(FrameDimensions::fromRoundedLuma(32, 16));
        PlaneView dest3 = frame3.luma().view();

        // Fixed path: flatFillCursor wired, pointing at its own separate array.
        {
            BitstreamReader br(brBytes, sizeof(brBytes));
            std::vector<uint8_t> fillBytes = {0xAA, 0xBB};
            FlatFillPrestageArray fillArray(fillBytes, /*confirmedPrefixLen=*/2, /*confirmedSuffixLen=*/0);
            FlatFillPrestageCursor flatFillCursor(&fillArray, 0);
            BlockRecord rec0 = decodeOneBlock(br, dest3, nullptr, 0, 0, nullptr, nullptr, nullptr,
                                                &flatFillCursor);
            BlockRecord rec1 = decodeOneBlock(br, dest3, nullptr, 0, 1, nullptr, nullptr, nullptr,
                                                &flatFillCursor);
            CHECK(rec0.outcome == BlockOutcome::Reconstructed);
            CHECK(rec1.outcome == BlockOutcome::Reconstructed);  // block1 reads byte1 (0x61) -> valid dispatch
            CHECK(rec1.primary == PrimaryBlockType::SubDispatch);
            CHECK(rec1.sub.has_value() && *rec1.sub == Case1SubType::FlatDcFill);
            std::cout << "  Part 3a (FIXED: flatFillCursor wired, block1's dispatch unaffected by block0's "
                         "fill byte): "
                      << (rec1.outcome == BlockOutcome::Reconstructed ? "OK" : "FAILED") << "\n";
        }

        // Legacy/unfixed path: flatFillCursor == nullptr, the pre-Sec5.3y
        // behavior. Demonstrates the bug was real: block1's dispatch now
        // lands on byte2 (0xFF), out-of-range, confirming the desync.
        {
            BitstreamReader br(brBytes, sizeof(brBytes));
            BlockRecord rec0 = decodeOneBlock(br, dest3, nullptr, 0, 0);  // flatFillCursor defaults to nullptr
            BlockRecord rec1 = decodeOneBlock(br, dest3, nullptr, 0, 1);
            CHECK(rec0.outcome == BlockOutcome::Reconstructed);
            CHECK(rec1.outcome == BlockOutcome::OutOfRangeDispatch);  // the real, concrete bug
            std::cout << "  Part 3b (UNFIXED/legacy fallback, flatFillCursor=nullptr: block1's dispatch IS "
                         "desynced by block0's own stolen fill byte, confirming the bug was real): "
                      << (rec1.outcome == BlockOutcome::OutOfRangeDispatch ? "OK" : "FAILED") << "\n";
        }
    }

    if (g_failures != 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "\nbink_flat_fill_cursor_test: all checks passed\n";
    return 0;
}
