// Permanent regression test for spec-bink-codec.md Sec5.3v's own golden
// 4-stage worked example, frame0/Y-plane/block(0,0) of main_menu_bg.bik
// (fix/bink-block00-4stage-trace task, 2026-10-11).
//
// Sec5.3v is a live-traced, independently cross-confirmed (real binkw32.dll
// AND a from-scratch Python re-derivation, both agreeing) worked example
// for this exact block, routed to Team B to localize a long-standing
// 141-vs-24 pixel divergence:
//   Stage 1 (AC-tree decode): 17 bits consumed total.
//   Stage 2 (raw, pre-dequant coefficients): DC(k=0)=118, k=8=-1, k=16=-1,
//            every other of the 64 coefficients exactly 0.
//   Stage 3 (dequantized, quantizer-select nibble 2): k=0 -> 6293,
//            k=8 -> -102, k=16 -> -118 (sar(raw*quant, 0xb) per coefficient).
//   Stage 4 (8x8 IDCT output, core grid before any upsample): rows 0-1 all
//            24, rows 2-7 all 25.
//
// THIS TASK'S OWN FINDING (see ac_tree_decoder.h's top comment for the
// full write-up): stages 3-4 (dequant + IDCT) are bit-exact CONFIRMED when
// fed stage 2's exact literal coefficients directly - the divergence is
// entirely upstream, in stage 1 (THIS file does not exercise it, since
// decodeAcCoefficientTree() does not currently reproduce Sec5.3v's own
// 17-bit/2-nonzero-coefficient result against the real bitstream - tracked
// as an open/parked gap, not asserted here). This test locks in the part
// that IS independently verified: dequantization (using this project's own
// kIntraQuantCompiled table, quantizer nibble 2) and the shared IDCT
// butterfly (idct_butterfly.h, already bit-exact-verified elsewhere for a
// DIFFERENT real block) both reproduce Sec5.3v's stage 3/4 numbers exactly
// when given stage 2's literal, hand-transcribed-from-spec coefficients -
// no fixture file, no decode, no AC-tree involvement at all, so this test
// never skips (unlike the other real_bink_*_test.cpp files, which depend on
// gitignored golden fixtures).

#include <cstdint>
#include <iostream>

#include "sr3binkcodec/entropy_prestage_tables.h"
#include "sr3binkcodec/idct_butterfly.h"

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
    std::cout << "real_bink_block00_4stage_dequant_idct_test: spec-bink-codec.md Sec5.3v's own "
                 "block(0,0) dequant+IDCT stages, fed its literal stage-2 ground-truth coefficients\n";

    // Stage 2 (Sec5.3v): raw, pre-dequant coefficients. Only k=0 (DC),
    // k=8, k=16 are nonzero; everything else is exactly 0.
    int16_t coeffs[64] = {0};
    coeffs[0] = 118;
    coeffs[8] = -1;
    coeffs[16] = -1;

    // Stage 3 (Sec5.3v): quantizer-select nibble 2, this project's own
    // kIntraQuantCompiled table (quant_byte_offset(k)=4k, same k indexing
    // as coeffs, Sec5.3b/Sec5.3j).
    constexpr uint32_t kQuantNibble = 2;
    int32_t quantRow[64];
    for (int k = 0; k < 64; ++k) {
        const size_t off = static_cast<size_t>(kQuantNibble) * 256 + static_cast<size_t>(k) * 4;
        uint32_t v = static_cast<uint32_t>(kIntraQuantCompiled[off]) |
                     (static_cast<uint32_t>(kIntraQuantCompiled[off + 1]) << 8) |
                     (static_cast<uint32_t>(kIntraQuantCompiled[off + 2]) << 16) |
                     (static_cast<uint32_t>(kIntraQuantCompiled[off + 3]) << 24);
        quantRow[k] = static_cast<int32_t>(v);
    }

    // Sec5.3v's own cited quant table values at these 3 k's, re-verified
    // against this project's own compiled table.
    CHECK(quantRow[0] == 109227);
    CHECK(quantRow[8] == 208315);
    CHECK(quantRow[16] == 240826);

    const int32_t dequant0 = sar11(static_cast<int64_t>(coeffs[0]) * quantRow[0]);
    const int32_t dequant8 = sar11(static_cast<int64_t>(coeffs[8]) * quantRow[8]);
    const int32_t dequant16 = sar11(static_cast<int64_t>(coeffs[16]) * quantRow[16]);
    std::cout << "  dequant k=0: " << dequant0 << " (expect 6293)\n";
    std::cout << "  dequant k=8: " << dequant8 << " (expect -102)\n";
    std::cout << "  dequant k=16: " << dequant16 << " (expect -118)\n";
    CHECK(dequant0 == 6293);
    CHECK(dequant8 == -102);
    CHECK(dequant16 == -118);

    // Stage 4 (Sec5.3v): the full 8x8 IDCT, core grid before any
    // engine-specific upsample. Expected: rows 0-1 all 24, rows 2-7 all 25.
    uint8_t pixels[64];
    idctBlock8x8(coeffs, quantRow, pixels);

    uint8_t expected[64];
    for (int r = 0; r < 8; ++r) {
        const uint8_t v = (r < 2) ? 24 : 25;
        for (int c = 0; c < 8; ++c) expected[r * 8 + c] = v;
    }

    bool allMatch = true;
    for (int i = 0; i < 64; ++i) {
        if (pixels[i] != expected[i]) {
            allMatch = false;
            std::cout << "  MISMATCH at pixel " << i << " (row " << (i / 8) << ", col " << (i % 8)
                       << "): got " << static_cast<int>(pixels[i]) << " expected " << static_cast<int>(expected[i])
                       << "\n";
        }
    }
    CHECK(allMatch);
    std::cout << "IDCT core-grid result vs. Sec5.3v's own ground truth: "
              << (allMatch ? "BIT-EXACT MATCH, all 64 values (rows 0-1=24, rows 2-7=25)" : "MISMATCH - see above")
              << "\n";

    // Pixel (0,0) of block (0,0) specifically - the task's own headline
    // success criterion (stages 3-4 only; stage 1/AC-tree is NOT exercised
    // by this test - see this file's own top comment and
    // ac_tree_decoder.h's).
    CHECK(pixels[0] == 24);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "real_bink_block00_4stage_dequant_idct_test: all checks passed\n";
    return 0;
}
