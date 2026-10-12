// Targeted real-data confirmation for the fix/bink-dc-decode-prestage-bitpos
// task: include/sr3binkcodec/dc_prestage_sibling_gap.h's
// consumeDcArrayPreStageGap(), wired into frame_decoder.h's
// decodePlaneRasterOrder() right before decodeRowDcArray(), against the
// EXACT bit positions and leading-magnitude values spec-bink-codec.md
// Sec5.3q/Sec5.3r/Sec5.3s/Sec5.3t give for main_menu_bg.bik frame 0's
// Y-plane row 0.
//
// Same harness convention as tests/real_bink_merge_group_trace_test.cpp
// (reused verbatim): a cold BitstreamReader constructed directly at
// frame0_raw.bin byte offset 4 (Y-plane's own sub-chunk data start, past its
// 4-byte length prefix) - Sec5.3g's own "clean cold start" point, the SAME
// reference point Sec5.3q/Sec5.3r measure "absolute bit position" from.
//
// WHAT THIS TEST CHECKS:
//   1. The real descriptor-A/B CaseSelectorScratchSource pair
//      (entropy_prestage.h, identical construction to frame_decoder.h's
//      decodeLumaPlane()) run one real beginBlockRow() - Sec5.3r's own
//      "descriptor B's own real build (entry) 1757 -> indirect-call-site
//      entry 2871 (1114 bits)" span is exactly what this pays.
//   2. consumeDcArrayPreStageGap() then pays the remaining 4 real calls
//      Sec5.3r attributes (0x100195b0: 3384 bits: 0x10019d10: 2981 bits;
//      0x10019f00 x2: 10 bits each) - landing the reader at EXACTLY bit
//      9256, Sec5.3q's own independently-measured `0x1001a160` call-site
//      entry position for this exact file/frame/row ("Absolute bit 9256 at
//      the 0x1001a160 call site exactly reproduces Sec5.3q's own
//      independently-measured entry value").
//   3. skip0x100195b0()'s/skip0x10019d10()'s own leading-magnitude reads are
//      checked against Sec5.3s's "Initial decoded magnitude... 650" and
//      Sec5.3t's "Leading decoded magnitude: 528" - these ARE real,
//      content-correct reads off the live bitstream (not hardcoded), so
//      matching them independently cross-confirms the reader is at the
//      right bit position BEFORE the (measured-total, not-yet-general)
//      body skip even runs, the same kind of mid-sequence cross-check
//      Sec5.3r's own table uses to validate itself.
//
// Per this project's "no invented fixes" rule: if any of these diverges,
// this test reports exactly what it measured instead of adjusting the
// expected values to force a pass.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bink_sha256.h"
#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/dc_prestage_sibling_gap.h"
#include "sr3binkcodec/entropy_prestage.h"

#ifndef BINK_MMBG_GOLDEN_DIR
#define BINK_MMBG_GOLDEN_DIR "tests/golden/bink_main_menu_bg"
#endif

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

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    if (n <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(buf.data()), n);
    if (!f) return {};
    return buf;
}

// Reused verbatim from tests/real_bink_test_vector_validation_test.cpp /
// tests/real_bink_merge_group_trace_test.cpp's own kFrame0Size/kFrame0Sha
// (same fixture, same pin) - not re-derived here.
constexpr size_t kFrame0Size = 209288;
constexpr const char* kFrame0Sha = "613043f0f32ce10c0f058959bc0483a2f4fc0938c33bc971f0bb5825931e803c";

constexpr int kLumaWidthPixels = 1920;

}  // namespace

int main() {
    if (!caseSelectorTablesAvailable()) {
        std::cout << "real_bink_dc_prestage_gap_test: SKIPPED - Bink case-selector static tables not "
                      "available (no compiled-in data, and no runtime loader data).\n";
        return 77;
    }

    const std::string path = std::string(BINK_MMBG_GOLDEN_DIR) + "/frame0_raw.bin";
    const std::vector<uint8_t> frame0Raw = readFile(path);
    if (frame0Raw.empty()) {
        std::cout << "real_bink_dc_prestage_gap_test: SKIPPED - golden fixture not found at '" << path
                   << "' (gitignored, local-only; same convention as every other real_*_test in this repo).\n";
        return 77;
    }
    const std::string actualSha = bink_test_sha256::computeSha256Hex(frame0Raw.data(), frame0Raw.size());
    if (frame0Raw.size() != kFrame0Size || actualSha != kFrame0Sha) {
        std::cout << "real_bink_dc_prestage_gap_test: SKIPPED - golden fixture present but does not match its "
                      "designated hash (expected "
                   << kFrame0Size << " bytes / sha256 " << kFrame0Sha << ", got " << frame0Raw.size()
                   << " bytes / sha256 " << actualSha << ").\n";
        return 77;
    }
    std::cout << "real_bink_dc_prestage_gap_test: golden fixture verified (" << frame0Raw.size() << " bytes, sha256 "
              << actualSha << ")\n";
    if (frame0Raw.size() < 4) {
        std::cout << "real_bink_dc_prestage_gap_test: SKIPPED - fixture too small.\n";
        return 77;
    }

    // Cold BitstreamReader at Y-plane's own sub-chunk data start (frame0_raw.bin
    // byte offset 4, past its own 4-byte length prefix - Sec5.3g's "clean cold
    // start"), the same reference point Sec5.3q/Sec5.3r measure "absolute bit
    // position" from.
    BitstreamReader br(frame0Raw.data() + 4, frame0Raw.size() - 4);
    CHECK(br.bitsConsumedSoFar() == 0);

    CaseSelectorScratchSource caseSelectorA(br, kLumaWidthPixels);
    CaseSelectorScratchSource caseSelectorB(br, kLumaWidthPixels, caseSelectorLengthFieldBitWidthB,
                                             kDescriptorBSiblingTableIndex);
    const CaseSelectorStatus rowStatus = caseSelectorA.beginBlockRow(&caseSelectorB);
    std::cout << "\n--- beginBlockRow() (23 table builds + descriptor A/B real refill) ---\n";
    std::cout << "  status: " << (rowStatus == CaseSelectorStatus::Ok ? "Ok" : "NOT Ok") << "\n";
    CHECK(rowStatus == CaseSelectorStatus::Ok);

    const size_t bitsAfterBeginBlockRow = br.bitsConsumedSoFar();
    std::cout << "  cumulative bits after beginBlockRow(): " << bitsAfterBeginBlockRow
               << " (Sec5.3r: descriptor B's own real build/expansion ends at bit 2871)\n";
    CHECK(bitsAfterBeginBlockRow == 2871);

    std::cout << "\n--- consumeDcArrayPreStageGap() (Sec5.3r's remaining 4 calls: 0x100195b0, 0x10019d10, "
                 "0x10019f00 x2) ---\n";
    const SiblingLeadingMagnitude m195b0 = skip0x100195b0(br);
    std::cout << "  0x100195b0 leading magnitude: " << m195b0.value << " (Sec5.3s: expected 650)\n";
    CHECK(m195b0.value == 650);
    const size_t bitsAfter195b0 = br.bitsConsumedSoFar();
    std::cout << "  cumulative bits after 0x100195b0: " << bitsAfter195b0 << " (Sec5.3r: expected 6255)\n";
    CHECK(bitsAfter195b0 == 6255);

    const SiblingLeadingMagnitude m19d10 = skip0x10019d10(br);
    std::cout << "  0x10019d10 leading magnitude: " << m19d10.value << " (Sec5.3t: expected 528)\n";
    CHECK(m19d10.value == 528);
    const size_t bitsAfter19d10 = br.bitsConsumedSoFar();
    std::cout << "  cumulative bits after 0x10019d10: " << bitsAfter19d10 << " (Sec5.3r: expected 9236)\n";
    CHECK(bitsAfter19d10 == 9236);

    skip0x10019f00(br);
    const size_t bitsAfter1st19f00 = br.bitsConsumedSoFar();
    std::cout << "  cumulative bits after 0x10019f00 (1st): " << bitsAfter1st19f00 << " (Sec5.3r: expected 9246)\n";
    CHECK(bitsAfter1st19f00 == 9246);

    skip0x10019f00(br);
    const size_t bitsAfter2nd19f00 = br.bitsConsumedSoFar();
    std::cout << "  cumulative bits after 0x10019f00 (2nd): " << bitsAfter2nd19f00
               << " (Sec5.3r/Sec5.3q: expected 9256 - the real 0x1001a160 call-site entry position)\n";
    CHECK(bitsAfter2nd19f00 == 9256);
    CHECK(!br.ranOutOfData());

    std::cout << "\n--- consumeDcArrayPreStageGap() as a single call, from a fresh reader at the same start ---\n";
    BitstreamReader br2(frame0Raw.data() + 4, frame0Raw.size() - 4);
    CaseSelectorScratchSource caseSelectorA2(br2, kLumaWidthPixels);
    CaseSelectorScratchSource caseSelectorB2(br2, kLumaWidthPixels, caseSelectorLengthFieldBitWidthB,
                                              kDescriptorBSiblingTableIndex);
    SiblingTableBank siblingBank2;
    CHECK(caseSelectorA2.beginBlockRow(&caseSelectorB2, &siblingBank2) == CaseSelectorStatus::Ok);
    // UPDATE (2026-10-11, fix/bink-0x10019d10-real-decode task): now takes
    // descriptor A's own BuiltTable (decode0x10019d10()'s own required
    // input, Sec5.3ab) and returns the bundled DcArrayPreStageGapResult
    // instead of a bare FlatFillPrestageArray - see dc_prestage_sibling_gap.h.
    // UPDATE (2026-10-12, fix/flat-fill-perplane-width-promotion task): now
    // also takes the row's own SiblingTableBank + blockCols, required by the
    // now-production real flat-fill decode.
    consumeDcArrayPreStageGap(br2, caseSelectorA2.builtTable(), siblingBank2, kLumaWidthPixels / 8);
    std::cout << "  cumulative bits: " << br2.bitsConsumedSoFar() << " (expected 9256)\n";
    CHECK(br2.bitsConsumedSoFar() == 9256);
    CHECK(!br2.ranOutOfData());

    if (g_failures == 0) {
        std::cout << "\nreal_bink_dc_prestage_gap_test: all checks passed\n";
        return 0;
    }
    std::cout << "\nreal_bink_dc_prestage_gap_test: " << g_failures << " check(s) FAILED\n";
    return 1;
}
