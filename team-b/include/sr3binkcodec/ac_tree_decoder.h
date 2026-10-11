#pragma once

// sr3binkcodec - the AC-coefficient tree decoder, `0x10024eb0`, spec-bink-
// codec.md Sec5.3j Part 2 (2026-10-10).
//
// CONFIRMED (Sec5.3j): function extent/calling convention, the single-bit
// LSB-first bit-read primitive (modelled here via BitstreamReader::
// readBits(1), which draws from the SAME LSB-first dword-refill
// accumulator - functionally identical bit sequence to the real 31-bit
// countdown refill, just a different refill GRANULARITY, which does not
// change which bits are consumed in which order), the front/tail
// two-pointer BFS worklist mechanism and its tag arithmetic (the exact
// `(tag>>2)*4 + 0x11/0x12/0x22/0x32/2` formulas quoted in the spec text),
// both sign-recovery mechanisms, the bit-reader write-back (not modelled
// separately here - this decoder shares `br` directly with every other
// read in the caller, so there is no separate write-back step to model),
// one explicit word-level write-out instance (k=1) confirming
// struct_byte_offset(k)=2k on the write side, and the real
// front-catches-tail-AND-level-reaches-1 termination condition (task item
// (b): NOT a fixed coefficient count - this decoder resolves however many
// leaves the tree's own BFS expansion naturally produces, up to the
// struct's own 63-AC-coefficient capacity, and stops there).
//
// RESOLVED 2026-10-10 per spec-bink-codec.md Sec5.3j Part 3, INCLUDING its
// 2026-10-10 follow-up splice closing the one item Part 3 originally left
// OPEN (Phase-2 sel==1/sel==3). Team A's instruction-level re-disassembly
// now gives a COMPLETE sign-recovery dispatch map, no remaining open items
// in that mapping. Both of this file's own previous "judgment calls"
// (below, kept for history) are superseded by this:
//
//   1. Sign-mechanism selection is NOT a function of which Phase-1 case (2
//      vs 3) a leaf came from, as this file previously guessed. The real,
//      now-complete rule (Sec5.3j Part 3 + its follow-up splice):
//        - Phase-1 case 2 AND case 3 (0x10025087.. / 0x100253cb..,
//          re-disassembled at 0x10025439-0x10025466 and the byte-identical
//          0x100250d2-0x100250f9 chained instance) BOTH resolve immediately,
//          in place, via mechanism 1 (general multi-bit read). Case 3 never
//          reaches mechanism 2's code at all - this file's PRIOR mapping of
//          case 3 -> mechanism 2 was wrong and is now fixed below.
//        - Mechanism 1 is used ONLY by these two Phase-1 arms. It has no
//          other home.
//        - Mechanism 2 (`and esi,1; neg esi; or esi,1` - magnitude always
//          exactly 1) has exactly two homes, BOTH inside Phase 2 (a
//          structurally separate dispatch at 0x100254ab-0x10025762, walked
//          by its own worklist pointer `front2`/`[ebp-0x98]` up to the SAME
//          `tail`/`[ebp-0xa0]` Phase 1 left behind - not a later iteration
//          of this file's own Phase-1 loop):
//            * Phase-2 `sel==2` (0x1002556a-0x10025637): GATED - reads one
//              categorization bit first; bit clear -> re-queues the node
//              with a `+3`-tag for further sub-expansion (no value yet);
//              bit set -> reads one more bit, which IS mechanism 2's sign
//              bit, and resolves the leaf (magnitude 1).
//            * Phase-2 `sel==3` (0x10025716-0x1002575c): UNCONDITIONAL -
//              no gating bit, resolves straight to mechanism 2 every time.
//        - Phase-2 `sel==1` (0x10025522-0x1002555f) resolves NO sign at
//          all: it is a pure tag-arithmetic requeue, structurally identical
//          to Phase-1's own case 1 (retags itself to a sel==2 shape in
//          place, pushes 3 more sel==2-tagged children), deferring value
//          resolution entirely to whatever later visits those children.
//      **This file still only implements Phase 1's single front/tail
//      queue** (see the CONFIRMED block above) - Phase 2's own queue
//      (`front2`), its distinct dispatch table, and the exact tag-level
//      mechanics of WHICH worklist entries get routed through Phase 2
//      rather than fully resolving within Phase 1's own pass (not given by
//      the spec text - only Phase 2's internal BEHAVIOR per sel value is
//      specified, not its entry condition/seed relative to Phase 1's own
//      front/tail array) are a genuinely separate, still-open structural
//      gap, NOT a sign-mechanism-mapping gap (that part IS now fully
//      closed). Per this project's "no invented fixes" rule, Phase 2 is
//      deliberately NOT implemented here rather than guessing at its
//      front2/tag-routing mechanics without a worked trace. Whether Phase 2
//      is reachable with real content in main_menu_bg.bik remains OPEN per
//      Part 3 ("not tested live this pass, only disassembled") - if it is,
//      that is this decoder's next known gap, not a silently-wrong guess.
//   2. Mechanism 1's extra-bits field WIDTH and sign-test operand: CONFIRMED
//      exactly as this file's ORIGINAL judgment call already implemented
//      for case 2 (Part 3 re-confirms this independently, instruction by
//      instruction, in both re-disassembled sites) - read `level` raw bits
//      into their own untouched snapshot, compute `masked = (raw &
//      (bias_mask-1)) | bias_mask` as the magnitude, and take the sign from
//      the ORIGINAL, pre-mask `raw`'s own bit at `bias_mask` (NOT
//      `masked`'s, which always has that bit set by construction - testing
//      `masked` would be the tautology Team B originally flagged). No
//      category/tag-dependent width variation exists. This file's case-2
//      code already matched this; case 3 is now changed to match it too.
//   3. The per-node `(tag>>2)` "category" value's full semantic meaning
//      beyond tree-expansion shape remains OPEN per the spec (unchanged by
//      Part 3).
//
// Given the above, this file's case-2/case-3 leaf resolution (Phase 1) is
// now spec-CONFIRMED, not a judgment call - but this decoder's real-data
// output is still only partially independently verified: see the real-data
// FFmpeg cross-check this task's own verification run recorded (HANDOFF.md)
// for concrete block/pixel-match numbers, and the Phase-2-not-modeled gap
// above for what could still cause a real-file divergence deeper in a
// block's tree than Phase 1's own queue reaches.

#include <array>
#include <cstdint>

#include "sr3binkcodec/bitstream_reader.h"

namespace sr3binkcodec {

// Generous worklist capacity - the real function's own scratch buffer is
// ~120-124 bytes (per Sec5.3j's own stack-offset citation), so 256 entries
// is comfortably more than the real tree can ever need (63 AC coefficients
// max, each leaf contributing at most 1 node to the final tally, plus
// internal/shape nodes).
constexpr size_t kAcTreeWorklistCapacity = 256;

struct AcTreeDecodeResult {
    // coeffs[1..63] = resolved AC coefficients, row-major struct_byte_offset
    // (k)=2k indexing (same k convention idct_butterfly.h's `coeffs[64]`
    // array uses). coeffs[0] (DC) is never written here - the caller is
    // responsible for that slot (Sec5.3i: DC is a separate 16-bit literal
    // read, upstream of this function). Any AC slot the tree never resolved
    // stays 0 (a real possible outcome, not a bug - Sec5.3j: "the number of
    // AC coefficients actually produced is exactly however many terminal
    // leaves the tree's own BFS expansion naturally produces").
    std::array<int16_t, 64> coeffs{};
    int countResolved = 0;
};

// Decodes the AC-coefficient tree for one block, reading from `br` (the
// SAME shared per-row bitstream every other read in this codec uses -
// Sec5.3j confirms this function writes its bit-reader state BACK into the
// shared context at the end, i.e. it leaves `br` positioned exactly where
// the next read should continue, same convention this project's `br`
// already models implicitly by being one continuous cursor).
inline AcTreeDecodeResult decodeAcCoefficientTree(BitstreamReader& br) {
    AcTreeDecodeResult result;

    // Phase 0 (Sec5.3j): read the 4-bit level/max-depth nibble, seed the
    // worklist with 6 literal bytes, compute the initial bias_mask.
    const uint32_t A = br.readBits(4);
    std::array<uint8_t, kAcTreeWorklistCapacity> tags{};
    size_t front = 0;
    size_t tail = 0;
    tags[tail++] = 0x10;
    tags[tail++] = 0x60;
    tags[tail++] = 0xb0;
    tags[tail++] = 0x07;
    tags[tail++] = 0x0b;
    tags[tail++] = 0x0f;

    uint32_t level = A;
    uint32_t biasMask = (level >= 1) ? (1u << (level - 1)) : 0u;

    // running coefficient-count (Sec5.3j: "idx = running coefficient-count,
    // [ebp-0x9c]"), starting at AC index 1 (DC, k=0, is handled by the
    // caller before this function is ever invoked).
    int idx = 1;

    // Phase 1 (Sec5.3j): front/tail BFS drain, then the outer level/
    // bias_mask decrement - see this file's own top comment for why the
    // outer decrement is a no-op once the inner while has fully drained
    // (every pushed child is processed within the SAME inner while call,
    // since tail only ever grows and front only ever advances), and why
    // that is still faithful to the spec's own CONFIRMED termination
    // condition (front==tail AND level==1).
    do {
        while (front < tail) {
            const uint8_t tag = tags[front];
            if (tag == 0) {
                // Already-resolved leaf slot - just advance, no bit read
                // (Sec5.3j: "already-resolved leaf, just advance").
                ++front;
                continue;
            }
            const uint32_t sel = tag & 3u;
            const uint32_t base = static_cast<uint32_t>(tag >> 2) * 4u;

            switch (sel) {
                case 0: {
                    // ONE new child pushed, tag' = (tag>>2)*4 + 0x11.
                    if (tail < tags.size()) {
                        tags[tail++] = static_cast<uint8_t>(base + 0x11);
                    }
                    ++front;
                    break;
                }
                case 1: {
                    // Node resolved in place (tag' = (tag>>2)*4+2) AND
                    // THREE new children pushed (tags +0x12/+0x22/+0x32) -
                    // a 4-way split.
                    tags[front] = static_cast<uint8_t>(base + 2);
                    if (tail + 3 <= tags.size()) {
                        tags[tail++] = static_cast<uint8_t>(base + 0x12);
                        tags[tail++] = static_cast<uint8_t>(base + 0x22);
                        tags[tail++] = static_cast<uint8_t>(base + 0x32);
                    }
                    ++front;
                    break;
                }
                case 2:
                case 3: {
                    // Terminal leaf, general multi-bit sign-recovery
                    // mechanism (mechanism 1) - Sec5.3j Part 3, CONFIRMED by
                    // direct re-disassembly that BOTH case 2 and case 3 use
                    // this same mechanism (see this file's own top comment).
                    // Mechanism 2 (the single-extra-bit shortcut) does NOT
                    // belong here at all - it is exclusive to the Phase-2
                    // sel==2 re-dispatch arm, which this file does not
                    // model (top comment). Resolves AC coefficient `idx`.
                    tags[front] = 0;
                    if (idx < 64 && level >= 1) {
                        const uint32_t raw = br.readBits(static_cast<int>(level));
                        const uint32_t masked = (raw & (biasMask - 1u)) | biasMask;
                        int32_t value = static_cast<int32_t>(masked);
                        if (raw & biasMask) value = -value;  // sign bit taken from the pre-mask raw snapshot, Sec5.3j Part 3
                        result.coeffs[static_cast<size_t>(idx)] = static_cast<int16_t>(value);
                        ++idx;
                        ++result.countResolved;
                    }
                    ++front;
                    break;
                }
                default:
                    ++front;  // unreachable (sel is a 2-bit value, 0-3 exhaustive)
                    break;
            }
        }
        if (level == 0) break;
        biasMask >>= 1;
        --level;
    } while (level > 1);

    return result;
}

}  // namespace sr3binkcodec
