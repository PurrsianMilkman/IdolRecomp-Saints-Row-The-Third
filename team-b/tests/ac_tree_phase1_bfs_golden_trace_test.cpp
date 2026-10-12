// Permanent regression test reproducing the CONFIRMED PREFIX of spec-bink-
// codec.md Sec5.3ac's own bit-by-bit trace for dispatch idx3 (fix/bink-
// actree-phase1-gateb task, 2026-10-11): the first 19 of idx3's 83 bits -
// the only portion Sec5.3ac's own prose gives bit-by-bit (the remaining 64
// bits are summarized, not individually transcribed, and this project does
// not have access to Team A's own raw per-instruction event log
// (`scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/events_idx3.txt`,
// explicitly out of Team B's clean-room scope) to reconstruct them without
// inventing bit values this project's own "no invented fixes" policy
// forbids).
//
// Sec5.3ac (dispatch idx3, main_menu_bg.bik frame 0, A=2 so Phase 1's real
// BFS runs - unlike Sec5.3w's own trivial A=1 example) is the FIRST live
// trace of Phase 1's own BFS loop and Gate B's defer mechanism with real
// bitstream content, and the direct motivation for this task's rewrite of
// include/sr3binkcodec/ac_tree_decoder.h's A>1 branch (see that header's
// own top comment for the full citation/judgment-call breakdown). This
// test transcribes exactly what Sec5.3ac's prose gives bit-for-bit:
//   bit 5  Gate A, seed slot 0 (tag 0x10, cat 4)  -> 1 (dispatch, case 0)
//   bit 6  Gate B copy 1 (cat 4)  -> 0 (resolve now)
//   bits 7-8  magnitude/sign field (level=2)       -> raw 0b01 -> +3 (k2)
//   bit 9  Gate B copy 2 (cat 5)  -> 0 (resolve now)
//   bits 10-11  field                              -> raw 0b10 -> -2 (k3)
//   bit 12  Gate B copy 3 (cat 6)  -> 0 (resolve now)
//   bits 13-14  field                              -> raw 0b10 -> -2 (k10)
//   bit 15  Gate B copy 4 (cat 7)  -> 1 (defer)
//   bit 16  Gate A, re-visiting slot 0 (now cat 8, sel 1) -> 0 (inert,
//           retire - Sec5.3ac's own "Bit 13 - Gate A, next worklist node
//           -> 0 -> skip")
//   bit 17  Gate A, seed slot 1 (tag 0x60, cat 24) -> 1 (dispatch, case 0
//           again - Sec5.3ac's own "Bit 14 - Gate A -> 1 -> dispatch,
//           sel=0 (case 0 again)")
//   bit 18  Gate B copy 1 (cat 24) -> 1 (defer)
//   bit 19  Gate B copy 2 (cat 25) -> 0 (resolve now)
//   bits 20-21  field                              -> raw 0b10 -> -2 (k17)
//
// This project's own BitstreamReader does not expose the real binkw32
// accum/bits_avail register pair (dword-at-a-time refill, a different
// internal representation from the real decoder's 31-bit countdown refill
// - same documented non-claim as ac_tree_gate_ab_golden_trace_test.cpp's
// own top comment), so this test checks the thing that actually matters
// for bit-exactness - the SEQUENCE of bit VALUES consumed and the
// resulting coefficient outcome - not bit-position labels taken literally
// (Sec5.3ac's own "Bits N-M" prose labels, read strictly, are tighter than
// a literal 1-gate+2-field-bit budget allows for this excerpt - a tension
// this project cannot resolve without the raw event log; see
// ac_tree_decoder.h's own top comment for the full honest flag). The
// SEQUENCE and RESOLVED VALUES below are independently confirmed - every
// one of k2=+3, k3=-2, k10=-2, k17=-2 is byte-for-byte identical to
// Sec5.3ac's own published idx3 ground-truth coefficient list.
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

std::vector<uint8_t> buildIdx3ConfirmedPrefixBits() {
    BitPacker p;
    p.appendBits(2u, 4);  // level nibble A = 2 (Sec5.3ac: idx3's own confirmed level)

    p.appendBits(1u, 1);  // Gate A, seed slot 0 (cat 4) -> dispatch, case 0
    p.appendBits(0u, 1);  // Gate B copy 1 (cat 4) -> resolve now
    p.appendBits(0b01u, 2);  // field -> masked=3, sign+ -> +3 (k2)
    p.appendBits(0u, 1);  // Gate B copy 2 (cat 5) -> resolve now
    p.appendBits(0b10u, 2);  // field -> masked=2, sign- -> -2 (k3)
    p.appendBits(0u, 1);  // Gate B copy 3 (cat 6) -> resolve now
    p.appendBits(0b10u, 2);  // field -> -2 (k10)
    p.appendBits(1u, 1);  // Gate B copy 4 (cat 7) -> defer

    p.appendBits(0u, 1);  // Gate A re-visiting slot 0 (now cat 8, sel 1) -> inert, retire
    p.appendBits(1u, 1);  // Gate A, seed slot 1 (cat 24) -> dispatch, case 0 again
    p.appendBits(1u, 1);  // Gate B copy 1 (cat 24) -> defer
    p.appendBits(0u, 1);  // Gate B copy 2 (cat 25) -> resolve now
    p.appendBits(0b10u, 2);  // field -> -2 (k17)

    // The remaining content of idx3's own 83-bit stream is NOT individually
    // transcribed here (see top-of-file comment) - zero-padded, which will
    // drive further (unverified-against-ground-truth) dispatches past this
    // point. This test only checks the 4 coefficients already resolved
    // above, which a later zero-padded dispatch cannot un-write (each
    // category's k-slot is written at most once).
    return p.bytes(/*padToAtLeast=*/16);
}

void testIdx3ConfirmedPrefixMatchesGroundTruth() {
    std::vector<uint8_t> bits = buildIdx3ConfirmedPrefixBits();
    BitstreamReader br(bits.data(), bits.size());
    AcTreeDecodeResult result = decodeAcCoefficientTree(br);

    // Sec5.3ac's own published idx3 nonzero AC list includes k2=-2? NO -
    // k2:3, k3:-2, k10:-2, k17:-2 (direct quote: "1:-2, 2:3, 3:-2, 4:-1,
    // 8:-3, 9:3, 10:-2, 11:1, ..., 17:-2, ...").
    CHECK(result.coeffs[2] == 3);
    CHECK(result.coeffs[3] == -2);
    CHECK(result.coeffs[10] == -2);
    CHECK(result.coeffs[17] == -2);
}

}  // namespace

int main() {
    testIdx3ConfirmedPrefixMatchesGroundTruth();

    if (g_failures == 0) {
        std::cout << "ac_tree_phase1_bfs_golden_trace_test: all checks passed\n";
        return 0;
    }
    std::cerr << "ac_tree_phase1_bfs_golden_trace_test: " << g_failures << " check(s) failed\n";
    return 1;
}
