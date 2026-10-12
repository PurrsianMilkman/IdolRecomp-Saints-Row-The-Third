// Targeted real-data confirmation for the fix/bink-dc-array-value0-boundary-
// 5.3u task: include/sr3binkcodec/dc_delta_array.h's decodeRowDcArray(),
// after the value[0]/values[1+] boundary fix, against spec-bink-codec.md
// Sec5.3u's own live per-symbol trace of `0x1001a160`'s first call, frame 0/
// Y-plane/row 0 - the exact ground-truth worked example that resolved the
// Sec5.3m-vs-Sec5.3q "contradiction".
//
// Same harness convention as tests/real_bink_dc_prestage_gap_test.cpp
// (reused verbatim): a cold BitstreamReader constructed directly at
// frame0_raw.bin byte offset 4 (Y-plane's own sub-chunk data start), run
// through one real beginBlockRow() + consumeDcArrayPreStageGap() (both
// unchanged by this task) to land at the already-independently-confirmed
// bit position 9256, then decodeRowDcArray() for exactly 8 words.
//
// WHAT THIS TEST CHECKS: the first 8 words come out EXACTLY
// 118, 199, 212, 51, 25, 118, 119, 26 - Sec5.3u's own reconciled trace,
// confirmed a third independent time by this task's own real-bitstream
// read of the fixed-bound path's own two fields (10-bit sanity + 11-bit
// direct value, see dc_delta_array.h's own top comment for how this task
// derived that shape). Per this project's "no invented fixes" rule: if this
// diverges, this test reports exactly what it measured instead of adjusting
// the expected values to force a pass.

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
        std::cout << "real_bink_dc_value0_boundary_test: SKIPPED - Bink case-selector static tables not "
                      "available (no compiled-in data, and no runtime loader data).\n";
        return 77;
    }

    const std::string path = std::string(BINK_MMBG_GOLDEN_DIR) + "/frame0_raw.bin";
    const std::vector<uint8_t> frame0Raw = readFile(path);
    if (frame0Raw.empty()) {
        std::cout << "real_bink_dc_value0_boundary_test: SKIPPED - golden fixture not found at '" << path
                   << "' (gitignored, local-only; same convention as every other real_*_test in this repo).\n";
        return 77;
    }
    const std::string actualSha = bink_test_sha256::computeSha256Hex(frame0Raw.data(), frame0Raw.size());
    if (frame0Raw.size() != kFrame0Size || actualSha != kFrame0Sha) {
        std::cout << "real_bink_dc_value0_boundary_test: SKIPPED - golden fixture present but does not match its "
                      "designated hash (expected "
                   << kFrame0Size << " bytes / sha256 " << kFrame0Sha << ", got " << frame0Raw.size()
                   << " bytes / sha256 " << actualSha << ").\n";
        return 77;
    }
    if (frame0Raw.size() < 4) {
        std::cout << "real_bink_dc_value0_boundary_test: SKIPPED - fixture too small.\n";
        return 77;
    }

    BitstreamReader br(frame0Raw.data() + 4, frame0Raw.size() - 4);
    CaseSelectorScratchSource caseSelectorA(br, kLumaWidthPixels);
    CaseSelectorScratchSource caseSelectorB(br, kLumaWidthPixels, caseSelectorLengthFieldBitWidthB,
                                             kDescriptorBSiblingTableIndex);
    CHECK(caseSelectorA.beginBlockRow(&caseSelectorB) == CaseSelectorStatus::Ok);
    consumeDcArrayPreStageGap(br, caseSelectorA.builtTable());  // UPDATED 2026-10-11, fix/bink-0x10019d10-real-decode: now 2-arg
    CHECK(br.bitsConsumedSoFar() == 9256);

    DcDeltaArray row = decodeRowDcArray(br, 8);
    std::cout << "decodeRowDcArray() first 8 words: ";
    for (size_t i = 0; i < row.size() && i < 8; ++i) std::cout << row.wordAt(i) << " ";
    std::cout << "\n";

    const uint16_t kExpected[8] = {118, 199, 212, 51, 25, 118, 119, 26};
    CHECK(row.size() >= 8);
    for (int i = 0; i < 8 && row.hasIndex(static_cast<size_t>(i)); ++i) {
        if (row.wordAt(static_cast<size_t>(i)) != kExpected[i]) {
            std::cerr << "CHECK FAILED: word[" << i << "] = " << row.wordAt(static_cast<size_t>(i))
                      << ", expected " << kExpected[i] << " (spec-bink-codec.md Sec5.3u)\n";
            ++g_failures;
        }
    }

    if (g_failures == 0) {
        std::cout << "\nreal_bink_dc_value0_boundary_test: all checks passed (exact Sec5.3u trace reproduced: "
                   << "118, 199, 212, 51, 25, 118, 119, 26)\n";
        return 0;
    }
    std::cout << "\nreal_bink_dc_value0_boundary_test: " << g_failures << " check(s) FAILED\n";
    return 1;
}
