// Targeted real-data confirmation for the merge-group bit-consumption fix
// (commit 84820a4, this branch fix/bink-merge-group-bit-consumption):
// include/sr3binkcodec/entropy_prestage.h's buildCaseSelectorTable() merge-
// network call sites (mode==3 groups) now peekBits(N) -> mergeBitSelected()
// -> skipBits(actual-consumed-count) instead of unconditionally committing
// the nominal N-bit peek width via readBits(N). That fix was previously only
// mutation-tested (synthetic_bink_codec_test.cpp) - this test checks it
// against the REAL captured data spec-bink-codec.md Sec5.3g's "full
// call-by-call re-trace" section describes, from the exact cold bit-reader
// starting position Team A's trace is about.
//
// Per the orchestrator's own instruction: tests/real_bink_test_vector_
// validation_test.cpp and tests/real_bink_frame0_decode_test.cpp are the
// WRONG harness for this - neither starts a bit reader at the exact byte
// offset this trace is about, nor exercises buildCaseSelectorTable()
// directly 23 times in a row and inspects the bit cursor after each call.
//
// WHAT THIS TEST CHECKS (spec-bink-codec.md Sec5.3g, quoted/cited inline):
//
//   1. tests/golden/bink_main_menu_bg/frame0_raw.bin is present and matches
//      the exact (size, sha256) pin tests/real_bink_test_vector_validation_
//      test.cpp already hardcodes for this same file (kFrame0Size/kFrame0Sha
//      there) - reused verbatim here, not re-derived, per this task's own
//      instruction.
//
//   2. Sec5.3g's own "The absolute number, pinned down 2026-10-08" paragraph:
//      "Y-plane's sub-chunk data start... sits at byte offset 4 relative to
//      frame 0's own raw blob... The bit-reader's cursor is exactly byte-
//      and word-aligned at this exact point, with zero bits pre-cached
//      (accum=0, bits_avail=0) - a clean cold start." A BitstreamReader is
//      constructed directly over (frame0Raw.data()+4, frame0Raw.size()-4) -
//      no earlier plane/header parsing, matching that cold-start claim
//      exactly. The same paragraph's own 16-byte test vector at this exact
//      offset (`bc 15 86 9c bc 65 98 dc 80 17 e2 83 27 31 a4 0e`) is checked
//      directly against the fixture bytes below, independent of the sha256
//      pin on the whole file, as an extra, cheap, exact-offset cross-check.
//
//   3/4. Sec5.3g: "before ever calling 0x10019360 for a row, 0x1001a530
//      executes a fixed sequence of 23 calls to 0x10018ca0... all 23 share
//      the identical sequential bit-reader cursor." buildCaseSelectorTable()
//      (entropy_prestage.h) is called 23 times in a row against the SAME
//      reader (matching kSiblingTableBuildsPerBlockRow there), recording
//      BitstreamReader::bitsConsumedSoFar() after each call.
//
//   5. Each call's post-call cumulative bit position is asserted against
//      Sec5.3g's own "Full call-by-call re-trace" paragraph: "the 23 calls'
//      own measured costs (32, 32, 4, 24, 24, 46, 46, 32, 32, 40, 40, 16, 32,
//      32, 32, 32, 32, 4, 4, 40, 4, 4, 4) sum to exactly 588 with zero gaps
//      or overlaps." Calls #6/#7 (46 bits each, the two full-depth mode==3
//      merge-network calls) are exactly the ones "A genuine, previously-
//      under-specified subtlety was found in the merge-network strategy"
//      names as "the *only* two of the 23 whose bit cost is not a fixed
//      function of {nibble, flag, shift}" - the two calls this fix touches.
//
//   6. After all 23 calls, the next 10 bits (N=10, caseSelectorLengthFieldBitWidth(1920)
//      == bitLength(1920/8+0x1ff) == bitLength(751) == 10, matching Sec5.3c's
//      own worked example) are read as the real LEN field for Y-plane frame
//      0 row 0 descriptor A. Sec5.3g: "the real LEN for Y-plane frame 0's
//      first block-row (descriptor A) is 528 (0x210), not 350" and "the LEN
//      read begins... at Y=588 bits" (i.e. the cumulative consumed-bit count
//      immediately BEFORE this 10-bit read must be exactly 588 - the exact
//      quantity the "580-vs-588" bug this fix exists to resolve is about;
//      588+10=598 after the read). Re-extracting at the WRONG bit 580 (the
//      pre-fix, shortfall-by-8-bits position) is independently confirmed by
//      Sec5.3g to decode to LEN==0 on these exact real file bytes ("Re-
//      extracting the real 10-bit LEN field starting at bit 580... decodes
//      to exactly 0... starting at the true bit 588 decodes to 528") - this
//      test does not need to recompute that itself, only confirm this own
//      reimplementation lands on 588/528, not 580/0.
//
// Per this project's "no invented fixes" rule: if any of the 23 per-call
// positions, or the final LEN/588 check, diverges from the numbers above,
// this test reports exactly which call first diverges and by how many bits
// rather than adjusting either the fix or the expected values to force a
// pass.

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "bink_sha256.h"
#include "sr3binkcodec/bitstream_reader.h"
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

// Reused verbatim from tests/real_bink_test_vector_validation_test.cpp's own
// kFrame0Size/kFrame0Sha (same fixture, same pin) - not re-derived here, per
// this task's own explicit instruction.
constexpr size_t kFrame0Size = 209288;
constexpr const char* kFrame0Sha = "613043f0f32ce10c0f058959bc0483a2f4fc0938c33bc971f0bb5825931e803c";

// Sec5.3g's own "16-byte test vector at this exact offset" (absolute file
// offset 0x2910 = frame-0-blob start 0x290c + 4, i.e. frame0_raw.bin byte 4):
// "bc 15 86 9c bc 65 98 dc 80 17 e2 83 27 31 a4 0e".
constexpr std::array<uint8_t, 16> kExpectedByteOffset4Vector = {
    0xbc, 0x15, 0x86, 0x9c, 0xbc, 0x65, 0x98, 0xdc, 0x80, 0x17, 0xe2, 0x83, 0x27, 0x31, 0xa4, 0x0e,
};

// Sec5.3g's "Full call-by-call re-trace" paragraph: the 23 calls' own
// measured per-call bit costs, summing to exactly 588. Converted here to
// cumulative post-call positions (the quantity BitstreamReader::
// bitsConsumedSoFar() actually reports) for direct comparison.
constexpr std::array<int, 23> kPerCallCost = {32, 32, 4, 24, 24, 46, 46, 32, 32, 40, 40,
                                                16, 32, 32, 32, 32, 32, 4,  4,  40, 4,  4, 4};

// Luma width for main_menu_bg.bik (1920, Sec1.2/Sec5.3c's own worked
// example) - matches kRawWidth in real_bink_test_vector_validation_test.cpp.
constexpr int kLumaWidthPixels = 1920;

}  // namespace

int main() {
    const std::string path = std::string(BINK_MMBG_GOLDEN_DIR) + "/frame0_raw.bin";
    const std::vector<uint8_t> frame0Raw = readFile(path);
    if (frame0Raw.empty()) {
        std::cout << "real_bink_merge_group_trace_test: SKIPPED - golden fixture not found at '" << path
                   << "' (gitignored, local-only; same convention as every other real_*_test in this repo).\n";
        return 77;
    }
    const std::string actualSha = bink_test_sha256::computeSha256Hex(frame0Raw.data(), frame0Raw.size());
    if (frame0Raw.size() != kFrame0Size || actualSha != kFrame0Sha) {
        std::cout << "real_bink_merge_group_trace_test: SKIPPED - golden fixture present but does not match its "
                      "designated hash (expected "
                   << kFrame0Size << " bytes / sha256 " << kFrame0Sha << ", got " << frame0Raw.size()
                   << " bytes / sha256 " << actualSha
                   << "). Per this project's own convention, stopping rather than re-deriving it.\n";
        return 77;
    }
    std::cout << "real_bink_merge_group_trace_test: golden fixture verified (" << frame0Raw.size()
              << " bytes, sha256 " << actualSha << ")\n";

    if (frame0Raw.size() < 20) {
        std::cout << "real_bink_merge_group_trace_test: SKIPPED - fixture too small to hold byte offset 4's own "
                     "16-byte test vector.\n";
        return 77;
    }

    // Cross-check Sec5.3g's own 16-byte test vector at absolute byte offset 4
    // of this exact file, independent of the whole-file sha256 pin above.
    bool vectorMatches = true;
    for (size_t i = 0; i < kExpectedByteOffset4Vector.size(); ++i) {
        if (frame0Raw[4 + i] != kExpectedByteOffset4Vector[i]) {
            vectorMatches = false;
            break;
        }
    }
    std::cout << "  byte-offset-4 16-byte vector (Sec5.3g 'absolute number' paragraph) matches: "
              << (vectorMatches ? "yes" : "NO") << "\n";
    CHECK(vectorMatches);

    // Cold BitstreamReader starting at absolute byte offset 4 of frame0_raw.bin
    // (Sec5.3g: "a clean cold start... accum=0, bits_avail=0" at exactly this
    // point - Y-plane's sub-chunk data start, past its own 4-byte length
    // prefix).
    BitstreamReader br(frame0Raw.data() + 4, frame0Raw.size() - 4);
    CHECK(br.bitsConsumedSoFar() == 0);

    std::cout << "\n--- 23-call case-selector table-build trace (spec-bink-codec.md Sec5.3g) ---\n";
    std::array<size_t, 23> cumulativeExpected{};
    {
        size_t running = 0;
        for (size_t i = 0; i < kPerCallCost.size(); ++i) {
            running += static_cast<size_t>(kPerCallCost[i]);
            cumulativeExpected[i] = running;
        }
    }
    CHECK(cumulativeExpected[22] == 588);  // Sec5.3g: "sum to exactly 588"

    bool allCallsMatch = true;
    int firstDivergentCall = -1;
    long firstDivergenceDeltaBits = 0;
    for (int call = 0; call < 23; ++call) {
        buildCaseSelectorTable(br);  // 0x10018ca0, see entropy_prestage.h
        const size_t actual = br.bitsConsumedSoFar();
        const size_t expected = cumulativeExpected[static_cast<size_t>(call)];
        const bool ok = (actual == expected);
        std::cout << "  call #" << (call + 1) << ": expected cumulative bits=" << expected
                   << " actual=" << actual << (ok ? "  MATCH" : "  *** DIVERGE ***")
                   << (call == 5 || call == 6 ? "   (full-depth merge-network call, this fix's own target)" : "")
                   << "\n";
        if (!ok && allCallsMatch) {
            allCallsMatch = false;
            firstDivergentCall = call + 1;
            firstDivergenceDeltaBits = static_cast<long>(actual) - static_cast<long>(expected);
        }
    }
    if (!allCallsMatch) {
        std::cout << "  FIRST DIVERGENCE: call #" << firstDivergentCall << ", off by " << firstDivergenceDeltaBits
                   << " bits (actual - expected). Per this project's 'no invented fixes' rule, this is reported "
                      "exactly as measured - the merge-group fix is NOT confirmed correct against real data.\n";
    }
    CHECK(allCallsMatch);

    const size_t bitsBeforeLen = br.bitsConsumedSoFar();
    std::cout << "\n--- LEN field read (spec-bink-codec.md Sec5.3g/Sec5.3c) ---\n";
    std::cout << "  cumulative bits consumed immediately before LEN read: " << bitsBeforeLen
               << " (Sec5.3g: correct value is 588; the pre-fix bug landed on 580)\n";
    CHECK(bitsBeforeLen == 588);

    const int lengthFieldBits = caseSelectorLengthFieldBitWidth(kLumaWidthPixels);
    CHECK(lengthFieldBits == 10);  // Sec5.3c's own worked example for a 1920-wide frame
    const uint32_t len = br.readBits(lengthFieldBits);
    const size_t bitsAfterLen = br.bitsConsumedSoFar();
    std::cout << "  decoded LEN=" << len << " (Sec5.3g: correct value is 528; the pre-fix bug read 0 at bit 580)\n";
    std::cout << "  cumulative bits consumed after LEN read: " << bitsAfterLen << " (expected 598 = 588+10)\n";
    CHECK(len == 528);
    CHECK(bitsAfterLen == 598);
    CHECK(!br.ranOutOfData());

    if (g_failures != 0) {
        std::cerr << "\n" << g_failures << " check(s) failed\n";
        std::cerr << "real_bink_merge_group_trace_test: the merge-group bit-consumption fix is NOT confirmed "
                      "correct against this real captured data - see the divergence reported above.\n";
        return 1;
    }
    std::cout << "\nreal_bink_merge_group_trace_test: all checks passed - the merge-group bit-consumption fix "
                  "(commit 84820a4) is CONFIRMED correct against real captured data: all 23 per-row "
                  "buildCaseSelectorTable() calls land on spec-bink-codec.md Sec5.3g's own traced bit positions, "
                  "the LEN field starts at bit 588 (not 580), and decodes to 528 (not 0).\n";
    return 0;
}
