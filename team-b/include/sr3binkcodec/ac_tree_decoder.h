#pragma once

// sr3binkcodec - the AC-coefficient tree decoder, `0x10024eb0`, spec-bink-
// codec.md Sec5.3j Part 2/Part 3 + Sec5.3w (2026-10-11).
//
// REWRITTEN 2026-10-11 per spec-bink-codec.md Sec5.3w, which gives a full,
// independently-cross-confirmed, bit-by-bit 17-bit golden trace for frame0/
// Y-plane/block(0,0) and names the previously-never-modeled mechanism that
// explains why this file's earlier case0/case1 tag arithmetic produced a
// bitstream-INDEPENDENT tree shape (a bug: a real variable-length entropy
// code must vary in shape, not just leaf value, depending on the actual
// bits). The fix is "Gate A": for EVERY node visited in the main worklist
// loop, read exactly 1 bit BEFORE computing `sel`/dispatching at all - bit
// clear means the node is INERT (skipped with no dispatch, no further bit
// consumption); bit set means proceed to compute `sel = tag & 3` and
// dispatch per the case 0/1/2/3 table, exactly as Part 2's own prose already
// hinted but never modeled: "then: read the NEXT node's own 1-bit 'is-leaf'
// flag ... before looping back to front's successor".
//
// Sec5.3w's full 17-bit trace only exercises the A<=1 path (Phase 1's BFS
// is SKIPPED ENTIRELY when the level nibble A<=1 - confirmed explicitly by
// the trace's own prose). In that path, every seed-tag visit goes straight
// through Gate A, then dispatches using what were previously Phase-2-only
// arms:
//   sel==0 (case 0): REWRITES its own slot's tag in place (tag' =
//     category*4+0x11, zero bits consumed for the rewrite itself), then
//     falls straight into "Gate B" in the SAME visit (front is NOT
//     advanced - the same, now-rewritten slot is Gate-A-checked again next
//     iteration). Gate B is 4 unrolled 1-bit copies, ALWAYS all 4 run
//     regardless of whether an earlier copy already resolved a value (the
//     trace shows copies 2-4 all deferring even after copy 1 resolved):
//     each copy i (0..3) addresses category+i; bit clear -> resolve NOW via
//     the 1-bit sign mechanism (magnitude always 1); bit set -> defer: push
//     a synthetic (category+i)*4+3 ("case 3"-shaped) entry onto the tail -
//     this can be genuinely dead work (never read again this call) but
//     must still be structurally "spent" for bit-exact consumption.
//   sel==1 (case 1): pure arithmetic 4-way split - retags itself
//     (base+2) and pushes 3 new case-2-shaped children (+0x12/+0x22/+0x32),
//     consumes ZERO bits, never reaches Gate B, front advances normally.
//   sel==2: Phase-2-style GATED arm - one bit decides re-queue (push a
//     fresh (+3)-tagged node, no value) vs resolve-now (one more bit is the
//     sign, magnitude always 1).
//   sel==3: Phase-2-style UNCONDITIONAL arm - no gating bit, resolves
//     straight to the 1-bit sign mechanism every time (magnitude always 1).
//
// For A>1, Phase 1's own genuine BFS still runs (case0 pushes one new
// child rather than rewriting in place; case1 is the same 4-way split;
// case2/case3 resolve immediately via "mechanism 1", the general multi-bit
// masked-magnitude + pre-mask-snapshot sign read, Sec5.3j Part 3) followed
// by the existing Phase 2 pass over anything Phase 1 left as a pure shape
// marker - UNCHANGED in overall structure from the prior version of this
// file, EXCEPT Gate A is now applied on every node visited in Phase 1's own
// main loop too, per Part 2's own prose quoted above (this is the "whether
// Gate A is Phase-2-only or universal" judgment call the task names -
// resolved here as UNIVERSAL, since Part 2's prose describes the exact same
// 1-bit-flag-before-dispatch idiom for Phase 1's own loop, not just a
// Phase-2 construct). Phase 2 itself is NOT given its own Gate A here (not
// exercised by any golden trace to date; left as the already-confirmed
// mechanics from Part 3, a deliberately conservative choice to avoid
// touching code with no verifying data).
//
// CATEGORY -> RASTER-K PERMUTATION (new this task, Sec5.3w): resolved
// coefficients are NOT written at their "category" index directly - that
// index must go through a fixed, compiler-unrolled category(1-63) ->
// raster-k(1-63) permutation (independently confirmed bijective over
// {1..63}) before being stored into the coefficient array. Applied here to
// every value-producing write site (Gate B, Phase-2-style sel==2/sel==3,
// AND Phase 1's own mechanism-1 leaves for A>1 - the latter is a JUDGMENT
// CALL: no A>1 trace independently re-confirms the permutation applies
// there too, but using the SAME "category" concept (tag>>2) consistently
// for the raster destination, rather than reintroducing a separate
// sequential-idx convention the new trace data does not support, is the
// more internally-consistent reading).
//
// Both previous Phase-2 CHOSEN judgment calls (front2 starts at index 0;
// entry gated on the ORIGINAL A nibble; sel==0 is a pass-through; front2
// does not advance on sel==1) are kept, UNCHANGED, for the A>1 path - see
// git history for their original rationale.
//
// REWRITTEN AGAIN 2026-10-11 (fix/bink-actree-phase1-gateb task) per spec-
// bink-codec.md Sec5.3ac, a full bit-by-bit Gate A/Gate B/Phase-1-BFS trace
// for two REAL A>1 blocks (dispatch idx3, A=2, 83 bits/20 AC; idx4, A=3,
// 136 bits/30 AC), both independently cross-confirmed bit-exact against
// ground truth. This is the FIRST live trace of Phase 1's own BFS loop with
// real content (Sec5.3w's own 17-bit trace never left the A<=1 shortcut
// below) - it reveals the PRIOR A>1 branch (case0 pushes one child; case1
// resolves+splits; case2/case3 both terminal leaves via mechanism1) was
// WRONG: real Phase 1, for ANY A (not just A<=1), uses the SAME Gate A +
// case-dispatch skeleton as the A<=1 branch below, with two confirmed
// differences from that trivial case:
//   - case 0 AND case 2 BOTH fall into Gate B's shared 4-copy chain (per
//     Sec5.3ac's own address map: "Gate B's 4 unrolled copies (Phase 1,
//     reached from case 0/case 2)") - case 2 is NOT its own single-bit
//     gated arm the way the A<=1 branch's sel==2 models it (that modelling
//     is specific to the A<=1 trivial path, per Sec5.3w, and is UNCHANGED
//     below). JUDGMENT CALL: case 2 is treated identically to case 0 here
//     (tag rewritten to base+0x11, front NOT advanced, same runGateB() call)
//     since no trace excerpt gives case 2's own entry setup independently -
//     the address map only confirms both share the SAME Gate B decision
//     sites, not whether case 2's own pre-Gate-B setup differs from case
//     0's. Flagged, not silently assumed.
//   - case 3 keeps its own single unconditional mechanism-1 resolve (its
//     own jump-table resolve write, 0x1002546c - NOT through Gate B),
//     confirmed live by the idx3 trace's "three case-3 resolutions in a
//     row."
// Gate B's defer branch (bit set) does NOT push onto this SAME front/tail
// worklist - Sec5.3ac's own trace shows "front catches tail" (Phase 1's
// loop ending) WHILE deferred entries from Gate B still remain undrained,
// which only resolve later via a DISTINCT "Phase 2" pass. Modelled here by
// collecting Gate B's defers into a side list, then appending them (as
// fresh sel==3-tagged, i.e. "(cat+i)*4+3", nodes) onto the Phase-1 tags
// array ONLY after Phase 1's own BFS fully completes - so Phase 2's
// existing front2 walk (UNCHANGED below) discovers and resolves them via
// its own sel==3 arm, exactly as the trace states: "Phase 2 resolves them
// via its own sel==3 arm (mechanism 2, magnitude always 1)."
//
// Gate B's OWN resolve-now mechanism (bit clear): Sec5.3ac's address map
// and prose both describe it as a "level"-width magnitude/sign field (the
// SAME mechanism-1 case 3's own terminal leaves use, confirmed live by
// "idx4... one level deeper than idx3's fields... the field width tracks
// the CURRENT outer level, not a fixed constant"). Implemented here as the
// SAME mechanism1 lambda case 3 uses. NOTE (honestly flagged, not hidden):
// the trace prose's own "Bits N-M" bit-position labels for individual Gate
// B copies, read strictly, are tighter than a literal 1(gate)+level(field)
// bit budget would allow for idx3's first node - this project does not have
// access to Team A's raw per-instruction event log
// (`scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/events_idx3.txt`, Team
// A's own working file, out of Team B's clean-room scope) to resolve that
// tension directly, so the mechanism is implemented per the address map's
// own repeated, explicit "level"-width description and then CHECKED
// empirically against the real `main_menu_bg.bik` fixture's own actual bit
// consumption (not re-derived from the possibly-loose prose bit-labels
// alone) - see HANDOFF.md for the measured result of that check.
//
// Gate B's category-index convention (copy i resolves/defers category+i,
// REGARDLESS of whether an earlier copy in the same chain resolved or
// deferred) was independently cross-checked against Sec5.3ac's own named
// example ("Gate B copy 2... write category 25 = -2 (k=17)... the FIRST
// live-captured instance of a Gate B copy resolving a category that was
// NOT the chain's own starting category - cat increments once per copy
// regardless of resolve-vs-defer") - this already matches the A<=1
// branch's own pre-existing `catI = category + i` convention exactly, so
// NO code change was needed there. This also means the newly-reported
// "copy 1 uses index register ebx, copies 2-4 use edi" finding does NOT,
// on inspection, correspond to any observable difference in WHICH category
// each copy resolves/defers - `category + i` for i=0..3 already reproduces
// every spot-checked value in Sec5.3ac's own trace (cat4=+3->k2, cat5=-2-
// >k3, cat6=-2->k10, cat25=-2->k17, all confirmed). Read as: the ebx/edi
// split is a real x86 register-allocation detail of the disassembly that
// does not, by itself, imply any change to this port's category bookkeeping
// - no further code change made for it beyond what `category + i` already
// provides, flagged here rather than silently assumed equivalent.
//
// Phase 2's own newly-reported "sel==2 4-slot requeue/resolve chain"
// (Sec5.3ac's address map: 4 decision sites, 4 requeue writes, 4 resolve
// writes) was checked against this file's EXISTING Phase 2 code (below,
// unchanged) rather than assumed to need a rewrite: case 1's own 4-way
// split (self retagged sel==2 + 3 sel==2-tagged children pushed) always
// produces EXACTLY 4 sel==2-tagged siblings per split, each visited in its
// own front2 iteration through the SAME single-decision sel==2 handler
// below - i.e. this file's existing per-node loop, run exactly 4 times for
// one case-1 split's output, already IS a "4-slot requeue/resolve chain"
// for that split; the disassembly's own "4 decision sites" most likely
// reflects the real function literally hand-unrolling those 4 known-fixed
// visits rather than looping, not a FOURTH distinct code path this file is
// missing. JUDGMENT CALL, not independently re-verified bit-for-bit against
// Sec5.3ac's own idx3/idx4 trace (neither trace excerpt gives Phase 2's
// sel==2 arm its own bit-by-bit breakdown) - flagged, not silently assumed.

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

// Sec5.3w: the fixed category(1-63) -> raster-k(1-63) permutation, read
// off the spec's own worked table. Index 0 is unused (DC is never produced
// by this function). Independently confirmed bijective: domain and range
// both exactly {1..63}, no collisions or gaps.
constexpr std::array<uint8_t, 64> kCategoryToRasterK = {{
    0,                                                                   // 0 (unused, DC)
    1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,                   // cat 1-15
    20, 21, 28, 29, 22, 23, 30, 31, 16, 17, 24, 25, 32, 33, 40, 41,      // cat 16-31
    34, 35, 42, 43, 48, 49, 56, 57, 50, 51, 58, 59, 18, 19, 26, 27,      // cat 32-47
    36, 37, 44, 45, 38, 39, 46, 47, 52, 53, 60, 61, 54, 55, 62, 63,      // cat 48-63
}};

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

// REWRITTEN 2026-10-13 (fix/bink-9c70c61, spec-bink-codec.md Sec5.3ai answer 1 + Sec5.3ac's own trace):
// the prior "Phase 1 BFS for levels A..2, then a separate Phase 2" split did NOT reproduce Sec5.3ac's own
// published 83-bit / 136-bit totals (it stopped after 54 / 36 bits: Phase 1 matched the trace bit-for-bit up
// to the Phase-1/Phase-2 boundary at bit 35, Phase 2 diverged). The measured fix: there is no distinct
// Phase 2. The same worklist pass (Gate A, case 0/1/2/3 dispatch, Gate B 4-copy chain) runs once per level
// A, A-1, ..., 1 over ONE persistent worklist; the level-L field is L bits (L-1 magnitude bits then a sign
// bit), so level 1 is the old "mechanism 2" (magnitude always 1, 1 sign bit). Dead (tag 0) entries are
// skipped without a Gate A bit; Gate B defers go to the FRONT of the worklist (last deferred first), where
// the next level's pass picks them up. Case 2 advances past its (zeroed) entry before its 4-copy chain, case
// 0 does not. Verified bit-exact on Sec5.3ac's idx3 (83 bits) and idx4 (136 bits) and on block (0,0).
// The prior structure is retired; see git history.
//
// Decodes the AC-coefficient tree for one block, reading from `br` (the SAME shared per-row bitstream
// every other read in this codec uses).
inline AcTreeDecodeResult decodeAcCoefficientTree(BitstreamReader& br) {
    AcTreeDecodeResult result;

    const uint32_t A = br.readBits(4);

    // One persistent worklist: live range [start, end). Deferred entries are pushed at --start, new
    // children at end++. 6 literal seeds (cat*4 + sel).
    constexpr size_t kCap = 2 * kAcTreeWorklistCapacity;
    std::array<uint8_t, kCap> tags{};
    size_t start = kAcTreeWorklistCapacity;
    size_t end = start;
    for (const uint8_t seed : {0x10, 0x60, 0xb0, 0x07, 0x0b, 0x0f}) tags[end++] = seed;

    auto writeCoeff = [&](uint32_t category, int32_t value) {
        if (category < kCategoryToRasterK.size()) {
            const uint32_t k = kCategoryToRasterK[category];
            if (k != 0 && k < result.coeffs.size()) {
                result.coeffs[k] = static_cast<int16_t>(value);
                ++result.countResolved;
            }
        }
    };

    for (uint32_t level = A; level >= 1; --level) {
        const uint32_t biasMask = 1u << (level - 1);
        // Level-L value: (L-1) magnitude bits | implicit top bit, then a sign bit == a single L-bit read
        // whose top bit is the sign (LSB-first reader).
        auto resolve = [&](uint32_t cat) {
            const uint32_t raw = br.readBits(static_cast<int>(level));
            const uint32_t masked = (raw & (biasMask - 1u)) | biasMask;
            int32_t value = static_cast<int32_t>(masked);
            if (raw & biasMask) value = -value;
            writeCoeff(cat, value);
        };
        auto gateBChain = [&](uint32_t cat) {
            for (uint32_t i = 0; i < 4; ++i) {
                if (br.readBits(1) != 0) {
                    if (start > 0) tags[--start] = static_cast<uint8_t>((cat + i) * 4u + 3u);
                } else {
                    resolve(cat + i);
                }
            }
        };
        size_t pos = start;
        while (pos < end) {
            const uint8_t tag = tags[pos];
            if (tag == 0 || br.readBits(1) == 0) {  // dead entries cost no Gate A bit
                ++pos;
                continue;
            }
            const uint32_t sel = tag & 3u;
            const uint32_t category = static_cast<uint32_t>(tag >> 2);
            const uint32_t base = category * 4u;
            switch (sel) {
                case 0:
                    tags[pos] = static_cast<uint8_t>(base + 0x11);  // pos not advanced
                    gateBChain(category);
                    break;
                case 1:
                    tags[pos] = static_cast<uint8_t>(base + 2);  // pos NOT advanced: re-gated as sel 2 (measured, Sec5.3ac idx4)
                    if (end + 3 <= kCap) {
                        tags[end++] = static_cast<uint8_t>(base + 0x12);
                        tags[end++] = static_cast<uint8_t>(base + 0x22);
                        tags[end++] = static_cast<uint8_t>(base + 0x32);
                    }
                    
                    break;
                case 2:
                    tags[pos] = 0;
                    ++pos;
                    gateBChain(category);
                    break;
                default:  // 3
                    tags[pos] = 0;
                    ++pos;
                    resolve(category);
                    break;
            }
        }
    }
    return result;
}

}  // namespace sr3binkcodec
