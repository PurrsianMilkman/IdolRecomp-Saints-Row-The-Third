// Permanent regression test reproducing spec-bink-codec.md Sec5.3w's full,
// independently-cross-confirmed 17-bit golden trace for frame 0/Y-plane/
// block (0,0): the exact bit sequence that resolves Team B's top
// video-decode blocker (2026-10-11, fix/bink-ac-tree-gate-a-b task).
//
// This is a DIRECT transcription of Sec5.3w's own bit-by-bit trace (see
// include/sr3binkcodec/ac_tree_decoder.h's own top comment for the full
// citation), NOT a synthetic/hand-derived bitstream like
// ac_tree_phase2_test.cpp's - every bit value below is taken verbatim from
// the spec text's own trace table. The spec gives the real decoder's entry/
// exit bit-reader state as accum=0x000007a1/bits_avail=13 (entry) and
// accum=0x07364522/bits_avail=28 (exit) - those are the REAL binkw32-
// convention 31-bit-countdown-refill register values, a different internal
// representation from this project's own BitstreamReader (dword-at-a-time
// refill - see bitstream_reader.h's own header comment on why that is a
// "functionally identical bit sequence, just a different refill
// granularity"). This test therefore checks the thing that actually
// matters for bit-exactness - the SEQUENCE of bit VALUES consumed and the
// resulting dispatch/coefficient outcome - not the internal register
// representation, which this project's reader does not expose and is not
// claimed to mirror byte-for-byte.
#include <cstdint>
#include <iostream>
#include <vector>

#include "sr3binkcodec/ac_tree_decoder.h"
#include "sr3binkcodec/bitstream_reader.h"

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

class BitPacker {
public:
    void appendBits(uint32_t value, int width) {
        for (int i = 0; i < width; ++i) {
            const uint32_t bit = (value >> i) & 1u;
            if (bitPos_ == 0) bytes_.push_back(0);
            bytes_.back() |= static_cast<uint8_t>(bit << bitPos_);
            bitPos_ = (bitPos_ + 1) % 8;
        }
    }
    std::vector<uint8_t> bytes(size_t padToAtLeast = 4) const {
        std::vector<uint8_t> out = bytes_;
        while (out.size() < padToAtLeast) out.push_back(0x00);
        return out;
    }

private:
    std::vector<uint8_t> bytes_;
    int bitPos_ = 0;
};

// Builds the exact 17-bit sequence Sec5.3w's trace walks through, bit for
// bit:
//   bits 1-4   level field, A = 1
//   bit 5      Gate A, seed slot 0 (tag 0x10, cat 4)   -> 0  (inert)
//   bit 6      Gate A, seed slot 1 (tag 0x60, cat 24)  -> 1  (dispatch, case 0)
//   bit 7      Gate B copy #1 (cat 24)                 -> 0  (resolve now)
//   bit 8      sign bit for cat 24                      -> 1  (-> -1)
//   bit 9      Gate B copy #2 (cat 25)                 -> 1  (defer, dead)
//   bit 10     Gate B copy #3 (cat 26)                 -> 1  (defer, dead)
//   bit 11     Gate B copy #4 (cat 27)                 -> 1  (defer, dead)
//   bit 12     Gate A, re-visiting slot 1 (tag 0x71)   -> 0  (inert, retire)
//   bit 13     Gate A, seed slot 2 (tag 0xb0, cat 44)  -> 0  (inert)
//   bit 14     Gate A, seed slot 3 (tag 0x07, cat 1)   -> 0  (inert)
//   bit 15     Gate A, seed slot 4 (tag 0x0b, cat 2)   -> 1  (dispatch, case 3)
//   bit 16     sign bit for cat 2                       -> 1  (-> -1)
//   bit 17     Gate A, seed slot 5 (tag 0x0f, cat 3)    -> 0  (inert, end)
std::vector<uint8_t> buildGoldenTraceBits() {
    BitPacker p;
    p.appendBits(1u, 4);  // bits 1-4: A = 1
    p.appendBits(0u, 1);  // bit 5
    p.appendBits(1u, 1);  // bit 6
    p.appendBits(0u, 1);  // bit 7
    p.appendBits(1u, 1);  // bit 8
    p.appendBits(1u, 1);  // bit 9
    p.appendBits(1u, 1);  // bit 10
    p.appendBits(1u, 1);  // bit 11
    p.appendBits(0u, 1);  // bit 12
    p.appendBits(0u, 1);  // bit 13
    p.appendBits(0u, 1);  // bit 14
    p.appendBits(1u, 1);  // bit 15
    p.appendBits(1u, 1);  // bit 16
    p.appendBits(0u, 1);  // bit 17
    return p.bytes(/*padToAtLeast=*/8);
}

void testGoldenTrace17Bits() {
    std::vector<uint8_t> bits = buildGoldenTraceBits();
    BitstreamReader br(bits.data(), bits.size());
    AcTreeDecodeResult result = decodeAcCoefficientTree(br);

    CHECK(!br.ranOutOfData());
    // Sec5.3w: "exactly 2 nonzero leaves, both magnitude 1, both negative -
    // 2 nonzero AC coefficients (k=8, k=16, both -1)."
    CHECK(result.countResolved == 2);
    CHECK(result.coeffs[8] == -1);
    CHECK(result.coeffs[16] == -1);
    for (size_t k = 0; k < result.coeffs.size(); ++k) {
        if (k == 8 || k == 16) continue;
        CHECK(result.coeffs[k] == 0);
    }
    // Exactly 17 bits consumed by this call.
    CHECK(br.bitsConsumedSoFar() == 17);
}

}  // namespace

int main() {
    testGoldenTrace17Bits();

    if (g_failures == 0) {
        std::cout << "ac_tree_gate_ab_golden_trace_test: all checks passed\n";
        return 0;
    }
    std::cerr << "ac_tree_gate_ab_golden_trace_test: " << g_failures << " check(s) failed\n";
    return 1;
}
