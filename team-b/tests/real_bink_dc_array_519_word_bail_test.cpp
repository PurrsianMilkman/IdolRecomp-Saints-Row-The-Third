// Permanent golden-trace regression for the fix/bink-dc-array-519-word-
// commit-run task (2026-10-11): spec-bink-codec.md Sec5.3n's "first call's
// exact count PINNED" + "gap attributed call-by-call" findings, worked
// against real `binkw32.dll`/`main_menu_bg.bik` frame 0, Y-plane row 0's
// first `0x1001a160` call.
//
// Same harness convention as tests/real_bink_dc_value0_boundary_test.cpp /
// tests/real_bink_dc_prestage_gap_test.cpp (reused verbatim): a cold
// BitstreamReader constructed directly at frame0_raw.bin byte offset 4
// (Y-plane's own sub-chunk data start), run through one real
// beginBlockRow() + consumeDcArrayPreStageGap() (both unchanged by this
// task) to land at the already-independently-confirmed bit position 9256.
//
// WHAT THIS TEST PINS (per this task's own success criteria, Sec5.3n):
//   1. decodeRowDcArray()'s own first-call real word count == 519 (not
//      240/`blockCols`) - via DcDeltaArray::size().
//   2. The call's own exit bit position == 13563 (9256 + 4307, Sec5.3n's
//      own measured total for this call).
//   3. After the two no-op bail steps this task adds
//      (skipSecondDcArrayZeroCountBail() + skip0x10019af0ZeroMagnitudeBail(),
//      dc_prestage_sibling_gap.h), the resulting bit position == 13587
//      (13563 + 24), matching Sec5.3n's own "landing the real dispatch-read
//      at absolute bit 13587 - not landing at 13619" finding. The 32-bit
//      residual against the OLDER Sec5.3y figure (13619) is a separate,
//      not-yet-root-caused cross-script bit-counting convention mismatch
//      per Sec5.3n - this test pins 13587 as the measured, honest value,
//      not 13619.
//   4. The first 8 real decoded words still match Sec5.3u's own worked
//      example (118, 199, 212, 51, 25, 118, 119, 26) - unchanged by this
//      task, confirming the 519-word fix did not perturb the already-
//      correct early values.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bink_sha256.h"
#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/dc_delta_array.h"
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

constexpr size_t kFrame0Size = 209288;
constexpr const char* kFrame0Sha = "613043f0f32ce10c0f058959bc0483a2f4fc0938c33bc971f0bb5825931e803c";
constexpr int kLumaWidthPixels = 1920;

}  // namespace

int main() {
    if (!caseSelectorTablesAvailable()) {
        std::cout << "real_bink_dc_array_519_word_bail_test: SKIPPED - Bink case-selector static tables not "
                      "available (no compiled-in data, and no runtime loader data).\n";
        return 77;
    }

    const std::string path = std::string(BINK_MMBG_GOLDEN_DIR) + "/frame0_raw.bin";
    const std::vector<uint8_t> frame0Raw = readFile(path);
    if (frame0Raw.empty()) {
        std::cout << "real_bink_dc_array_519_word_bail_test: SKIPPED - golden fixture not found at '" << path
                   << "' (gitignored, local-only; same convention as every other real_*_test in this repo).\n";
        return 77;
    }
    const std::string actualSha = bink_test_sha256::computeSha256Hex(frame0Raw.data(), frame0Raw.size());
    if (frame0Raw.size() != kFrame0Size || actualSha != kFrame0Sha) {
        std::cout << "real_bink_dc_array_519_word_bail_test: SKIPPED - golden fixture present but does not match "
                      "its designated hash (expected "
                   << kFrame0Size << " bytes / sha256 " << kFrame0Sha << ", got " << frame0Raw.size()
                   << " bytes / sha256 " << actualSha << ").\n";
        return 77;
    }
    if (frame0Raw.size() < 4) {
        std::cout << "real_bink_dc_array_519_word_bail_test: SKIPPED - fixture too small.\n";
        return 77;
    }

    BitstreamReader br(frame0Raw.data() + 4, frame0Raw.size() - 4);
    CaseSelectorScratchSource caseSelectorA(br, kLumaWidthPixels);
    CaseSelectorScratchSource caseSelectorB(br, kLumaWidthPixels, caseSelectorLengthFieldBitWidthB,
                                             kDescriptorBSiblingTableIndex);
    CHECK(caseSelectorA.beginBlockRow(&caseSelectorB) == CaseSelectorStatus::Ok);
    consumeDcArrayPreStageGap(br, caseSelectorA.builtTable());  // UPDATED 2026-10-11, fix/bink-0x10019d10-real-decode: now 2-arg
    CHECK(br.bitsConsumedSoFar() == 9256);

    // blockCols for this row (240, Y-plane width 1920 / 8) passed as the
    // caller's own minimum-need hint - no longer the loop's termination
    // bound (see dc_delta_array.h's own comment).
    DcDeltaArray row = decodeRowDcArray(br, /*minCount=*/240);

    std::cout << "decodeRowDcArray() word count: " << row.size() << " (expect 519, Sec5.3n)\n";
    CHECK(row.size() == 519);

    std::cout << "bit position after first call: " << br.bitsConsumedSoFar() << " (expect 13563, Sec5.3n)\n";
    CHECK(br.bitsConsumedSoFar() == 13563);

    const uint16_t kExpectedFirst8[8] = {118, 199, 212, 51, 25, 118, 119, 26};
    for (int i = 0; i < 8 && row.hasIndex(static_cast<size_t>(i)); ++i) {
        CHECK(row.wordAt(static_cast<size_t>(i)) == kExpectedFirst8[i]);
    }

    // The two no-op bail steps (Sec5.3n's "gap attributed call-by-call"):
    // second 0x1001a160 call (leading count 0, 10 bits), then 0x10019af0
    // (leading magnitude 0, 14 bits). 24 bits total.
    skipSecondDcArrayZeroCountBail(br);
    skip0x10019af0ZeroMagnitudeBail(br);

    std::cout << "bit position after 24-bit bail sequence: " << br.bitsConsumedSoFar()
              << " (expect 13587, Sec5.3n - NOT 13619, see this file's own top comment)\n";
    CHECK(br.bitsConsumedSoFar() == 13587);

    if (g_failures == 0) {
        std::cout << "\nreal_bink_dc_array_519_word_bail_test: all checks passed (519-word first call, "
                      "exit bit 13563, bail sequence lands dispatch-read prep at 13587 - spec-bink-codec.md "
                      "Sec5.3n reproduced exactly)\n";
        return 0;
    }
    std::cout << "\nreal_bink_dc_array_519_word_bail_test: " << g_failures << " check(s) FAILED\n";
    return 1;
}
