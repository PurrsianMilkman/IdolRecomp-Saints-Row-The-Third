// Focused synthetic unit test for the AC-tree decoder's Phase 1 (A>1) +
// Phase 2 dispatch (include/sr3binkcodec/ac_tree_decoder.h, spec-bink-
// codec.md Sec5.3j Part 2/Part 3 + Sec5.3ac, fix/bink-actree-phase1-gateb
// task, 2026-10-11).
//
// REWRITTEN this task, superseding the fix/bink-ac-tree-gate-a-b version of
// this file: Sec5.3ac (a live, bit-exact trace of two REAL A>1 blocks)
// found the PRIOR A>1 branch's dispatch (case 0 pushes one child; case 2/
// case 3 both terminal leaves via mechanism 1, no Gate B at all) was wrong
// - real Phase 1, for ANY A, uses the SAME Gate A + case-dispatch skeleton
// as the A<=1 trivial path (Sec5.3w), with case 0 AND case 2 both routing
// into Gate B's shared 4-copy chain. ac_tree_decoder.h's top-of-file
// comment has the full citation/judgment-call breakdown; this file's own
// bitstream below is updated to exercise that NEW structure specifically
// (case 0 -> Gate B with a mixed resolve/defer pattern; a Gate-B-rewritten
// slot's own re-dispatch into case 1's 4-way split; one of that split's
// children re-dispatching as case 2 -> Gate B again; Phase 1's case 3;
// Gate B's defer -> Phase 2's sel==3 arm).
//
// The exact bit sequence and every expected coefficient below was derived
// by hand-tracing this file's own NEW algorithm (not re-derived from
// Sec5.3ac's own idx3/idx4 trace beyond the shared Gate-A/case-dispatch
// SKELETON already independently confirmed there and in
// ac_tree_phase1_bfs_golden_trace_test.cpp) and cross-checked against an
// independent Python re-implementation of the same algorithm
// (scratchpad_bink/sim_phase2.py) - same "synthetic, hand-verified, not
// claimed to be a real captured trace" status the original version of this
// file always had.
//
// Walk (A=2, so level=2, biasMask=2 throughout - this scenario never
// re-enters Phase 1's inner loop at level=1):
//   front=0 (tag 0x10, cat4, sel0): GateA=1 -> case0. Tag rewritten to
//     0x21 (cat8, sel1). Gate B copies (cat4..7) ALL defer.
//   front=0 RE-TESTED (tag now 0x21, cat8, sel1): GateA=1 -> case1 (4-way
//     split). Self retagged to 0x22 (cat8, sel2, STAYS at index 0 - case 1
//     advances front, so this slot is never Gate-A-visited again by Phase
//     1's own loop, only later by Phase 2's front2 scan, which starts at
//     index 0). 3 children pushed at tail (indices 6/7/8): categories
//     12/16/20 (sel2 each). front advances to 1.
//   front=1 (cat24, sel0)/front=2 (cat44, sel0): GateA=0 -> inert both
//     (their ORIGINAL seed tags are left untouched in `tags` - Phase 1's
//     "inert" never zeroes a tag, only Gate-A-dispatched arms do - so
//     Phase 2 will later find and dispatch them as plain sel==0 nodes).
//   front=3 (cat1, sel3): GateA=1 -> case3, mechanism1 2-bit field
//     raw=0b01 -> value +3 (k1).
//   front=4 (cat2, sel3)/front=5 (cat3, sel3): GateA=0 -> inert both (same
//     as cat24/cat44 above - left untouched for Phase 2).
//   front=6 (first split child, cat12, sel2): GateA=1 -> case2 -> Gate B.
//     copy1 (cat12): gate=0, field raw=0b00 -> value +2. copy2
//     (cat13)/copy3 (cat14)/copy4 (cat15): gate=1, all defer. front NOT
//     advanced (case 2 shares case 0's "rewrite in place" shape).
//   front=6 RE-TESTED (tag now rewritten to cat12's own 0x11-shape,
//     sel==1): GateA=0 -> inert, front=7.
//   front=7 (second split child, cat16, sel2)/front=8 (third split child,
//     cat20, sel2): GateA=0 -> inert both.
//   front==tail==9 -> Phase 1's inner while loop ends; the outer do-while
//     decrements level once (2->1), `while(level>1)` is false -> Phase 1
//     fully ends.
//   Gate B's deferred list (cat4,5,6,7 from the FIRST chain - note cat4
//     itself deferred too, not just 5/6/7, since ALL 4 copy-gate bits were
//     set clear above; cat13,14,15 from the second) is appended onto
//     `tags` as fresh sel==3-tagged nodes. Phase 2 (front2 from index 0)
//     then walks EVERY slot in `tags`, Gate-A-free: the leftover sel==2
//     self-node (cat8, defers via Phase 2's own sel==2 4-slot-style gated
//     arm), the untouched seed nodes (cat24/cat44, Phase 2 sel==0
//     passthrough; cat2/cat3, Phase 2 sel==3), the split's own second/
//     third children (cat16 - Phase 2 sel==2, itself dispatches as sel==1
//     structurally since cat16's tag%4==0 -> see Phase 2's own case code;
//     cat20), and finally all 7 Gate-B-deferred sel==3 nodes - each
//     resolving via Phase 2's sel==3 mechanism-2 (1 sign bit, magnitude 1)
//     or re-deferring via sel==2's own gate, with every remaining bit
//     zero-padded (every gate/sign bit clear). The resulting coefficient
//     set below was cross-checked against an independent Python
//     re-implementation of this exact algorithm
//     (scratchpad_bink/sim_phase2b.py) - NOT hand-derived digit-by-digit,
//     flagged honestly: this is a self-consistency/regression check of
//     this file's OWN new mechanics, not a re-transcription of
//     Sec5.3ac's own real idx3/idx4 ground truth (see
//     ac_tree_phase1_bfs_golden_trace_test.cpp for that).

#include <array>
#include <cstdint>
#include <iostream>
#include <utility>
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

// Same LSB-first bit packer convention as tests/synthetic_bink_codec_test.cpp's
// own BitPacker (bits appended first are read back first).
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

uint32_t kAt(uint32_t category) { return kCategoryToRasterK[category]; }

std::vector<uint8_t> buildScenarioBits() {
    BitPacker p;
    p.appendBits(2u, 4);  // A = 2

    // front=0: case0 (cat4), Gate B all 4 copies defer.
    p.appendBits(1u, 1);  // GateA
    p.appendBits(1u, 1);  // copy1 defer
    p.appendBits(1u, 1);  // copy2 defer
    p.appendBits(1u, 1);  // copy3 defer
    p.appendBits(1u, 1);  // copy4 defer

    // front=0 re-tested: case1 split.
    p.appendBits(1u, 1);  // GateA

    // front=1 (cat24): inert.
    p.appendBits(0u, 1);
    // front=2 (cat44): inert.
    p.appendBits(0u, 1);

    // front=3 (cat1, sel3): case3, resolve raw=0b01 -> +3.
    p.appendBits(1u, 1);  // GateA
    p.appendBits(0b01u, 2);

    // front=4 (cat2, sel3): inert.
    p.appendBits(0u, 1);
    // front=5 (cat3, sel3): inert.
    p.appendBits(0u, 1);

    // front=6 (first split child, cat12, sel2): case2 -> Gate B.
    p.appendBits(1u, 1);  // GateA
    p.appendBits(0u, 1);  // copy1 (cat12) gate=0 resolve
    p.appendBits(0b00u, 2);
    p.appendBits(1u, 1);  // copy2 (cat13) defer
    p.appendBits(1u, 1);  // copy3 (cat14) defer
    p.appendBits(1u, 1);  // copy4 (cat15) defer

    // front=6 re-tested (now rewritten to cat12's own 0x11-shape): inert.
    p.appendBits(0u, 1);

    // front=7 (second split child, cat16, sel2): inert.
    p.appendBits(0u, 1);
    // front=8 (third split child, cat20, sel2): inert.
    p.appendBits(0u, 1);

    // Phase 2 and beyond: every remaining gate/sign bit this scenario
    // reads is zero-padded (clear) - see this file's own top comment for
    // why that still exercises a long, legitimate Phase 2 chain (the
    // leftover sel==2 self-node, the untouched seed nodes, the split's
    // other 2 children, and Gate B's 7 deferred sel==3 nodes).
    return p.bytes(/*padToAtLeast=*/16);
}

void testGateBMixedResolveDeferAndCase1Split() {
    std::vector<uint8_t> bits = buildScenarioBits();
    BitstreamReader br(bits.data(), bits.size());
    AcTreeDecodeResult result = decodeAcCoefficientTree(br);
    CHECK(!br.ranOutOfData());

    // Phase 1 case3 resolve (cat1 -> +3, k1 - kCategoryToRasterK[1]==1).
    CHECK(result.coeffs[kAt(1)] == 3);
    // Phase 1 case2 (via the split child) -> Gate B copy1 resolve
    // (cat12 -> +2, k6 - kCategoryToRasterK[12]==6).
    CHECK(result.coeffs[kAt(12)] == 2);

    // Full resulting nonzero-coefficient set, cross-checked against an
    // independent Python re-implementation of this exact algorithm
    // (scratchpad_bink/sim_phase2b.py) - see this file's own top comment.
    // Everything below k1/k6 above comes from the long Phase 2 chain this
    // scenario's zero-padded tail bits drive (the leftover sel==2 self-
    // node, the untouched seed nodes, the split's other children, and
    // Gate B's 7 deferred sel==3 nodes).
    const std::array<std::pair<int, int16_t>, 14> expectedRest = {{
        {2, 1}, {3, 1}, {4, 1}, {7, 1}, {8, 1}, {9, 1}, {10, 1}, {11, 1},
        {14, 1}, {15, 1}, {16, 1}, {20, 1}, {22, 1}, {32, 1},
    }};
    int countNonzero = 0;
    for (size_t k = 0; k < result.coeffs.size(); ++k) {
        if (result.coeffs[k] != 0) ++countNonzero;
    }
    CHECK(countNonzero == 2 + static_cast<int>(expectedRest.size()));
    for (const auto& kv : expectedRest) {
        CHECK(result.coeffs[static_cast<size_t>(kv.first)] == kv.second);
    }
}

void testDeterministic() {
    std::vector<uint8_t> bits = buildScenarioBits();
    BitstreamReader br1(bits.data(), bits.size());
    AcTreeDecodeResult r1 = decodeAcCoefficientTree(br1);
    BitstreamReader br2(bits.data(), bits.size());
    AcTreeDecodeResult r2 = decodeAcCoefficientTree(br2);
    CHECK(r1.countResolved == r2.countResolved);
    CHECK(r1.coeffs == r2.coeffs);
}

void testPhase2SkippedWhenLevelNibbleIsZero() {
    // A == 0 -> the A<=1 Gate-A-direct-dispatch path runs instead of real
    // Phase 1/Phase 2 (Sec5.3w). With no further bits supplied, every seed
    // slot's own Gate-A bit reads as 0 (zero-padded refill), so every slot
    // is inert and nothing resolves.
    BitPacker p;
    p.appendBits(0u, 4);  // A = 0
    std::vector<uint8_t> bits = p.bytes(8);
    BitstreamReader br(bits.data(), bits.size());
    AcTreeDecodeResult result = decodeAcCoefficientTree(br);
    CHECK(result.countResolved == 0);
    for (size_t i = 0; i < result.coeffs.size(); ++i) CHECK(result.coeffs[i] == 0);
}

}  // namespace

int main() {
    testGateBMixedResolveDeferAndCase1Split();
    testDeterministic();
    testPhase2SkippedWhenLevelNibbleIsZero();

    if (g_failures == 0) {
        std::cout << "ac_tree_phase2_test: all checks passed\n";
        return 0;
    }
    std::cerr << "ac_tree_phase2_test: " << g_failures << " check(s) failed\n";
    return 1;
}
