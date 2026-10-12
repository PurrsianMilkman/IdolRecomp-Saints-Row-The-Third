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

// Decodes the AC-coefficient tree for one block, reading from `br` (the
// SAME shared per-row bitstream every other read in this codec uses -
// Sec5.3j confirms this function writes its bit-reader state BACK into the
// shared context at the end, i.e. it leaves `br` positioned exactly where
// the next read should continue, same convention this project's `br`
// already models implicitly by being one continuous cursor).
inline AcTreeDecodeResult decodeAcCoefficientTree(BitstreamReader& br) {
    AcTreeDecodeResult result;

    // Phase 0 (Sec5.3j): read the 4-bit level/max-depth nibble, seed the
    // worklist with 6 literal bytes.
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

    // Writes a resolved coefficient through the category->raster-k
    // permutation (Sec5.3w).
    auto writeCoeff = [&](uint32_t category, int32_t value) {
        if (category < kCategoryToRasterK.size()) {
            const uint32_t k = kCategoryToRasterK[category];
            if (k != 0 && k < result.coeffs.size()) {
                result.coeffs[k] = static_cast<int16_t>(value);
                ++result.countResolved;
            }
        }
    };
    auto pushTag = [&](uint8_t t) {
        if (tail < tags.size()) {
            tags[tail++] = t;
        }
    };

    if (A <= 1) {
        // Sec5.3w: Phase 1's BFS is skipped entirely. Gate A + the
        // Phase-2-style dispatch arms run directly on the 6 literal seed
        // tags, draining the SAME front/tail worklist (which grows via
        // case 1's children and Gate B's deferred pushes) until front
        // catches tail.
        while (front < tail) {
            const uint32_t gateA = br.readBits(1);
            if (gateA == 0) {
                // Inert node - skip entirely, no dispatch, no further bits.
                ++front;
                continue;
            }
            const uint8_t tag = tags[front];
            const uint32_t sel = tag & 3u;
            const uint32_t category = static_cast<uint32_t>(tag >> 2);
            const uint32_t base = category * 4u;

            switch (sel) {
                case 0: {
                    // Rewrite own slot in place; do NOT advance front - the
                    // same slot is Gate-A-checked again next iteration.
                    tags[front] = static_cast<uint8_t>(base + 0x11);
                    // Gate B: 4 unrolled 1-bit copies, categories
                    // category..category+3. All 4 ALWAYS run (per the
                    // trace: copies after an earlier resolve still defer).
                    for (uint32_t i = 0; i < 4; ++i) {
                        const uint32_t gateB = br.readBits(1);
                        const uint32_t catI = category + i;
                        if (gateB == 0) {
                            const uint32_t signBit = br.readBits(1);
                            writeCoeff(catI, (signBit & 1u) ? -1 : 1);
                        } else {
                            // Defer: a synthetic case-3-shaped entry is
                            // conceptually pushed onto a SEPARATE, backward-
                            // growing deferred list (Sec5.3w) - NOT this
                            // function's own front/tail worklist. Per the
                            // golden trace, this entry is never read again
                            // within this call (genuinely dead work in the
                            // traced case) and, critically, must NOT affect
                            // this call's front/tail termination - pushing
                            // it onto `tags`/`tail` would make the main loop
                            // keep running past the trace's own exit point
                            // ("front now equals tail" at bit 17). The bit
                            // itself is still consumed above (structural
                            // cost paid); the synthetic tag value has no
                            // further effect modelled here.
                            (void)catI;
                        }
                    }
                    break;
                }
                case 1: {
                    // 4-way split, consumes zero bits, never reaches Gate B.
                    tags[front] = static_cast<uint8_t>(base + 2);
                    pushTag(static_cast<uint8_t>(base + 0x12));
                    pushTag(static_cast<uint8_t>(base + 0x22));
                    pushTag(static_cast<uint8_t>(base + 0x32));
                    ++front;
                    break;
                }
                case 2: {
                    // Gated arm: one bit decides re-queue vs resolve-now.
                    tags[front] = 0;
                    ++front;
                    const uint32_t gateBit = br.readBits(1);
                    if (gateBit == 0) {
                        pushTag(static_cast<uint8_t>(base + 3));
                    } else {
                        const uint32_t signBit = br.readBits(1);
                        writeCoeff(category, (signBit & 1u) ? -1 : 1);
                    }
                    break;
                }
                case 3: {
                    // Unconditional arm: always resolves, magnitude 1.
                    tags[front] = 0;
                    ++front;
                    const uint32_t signBit = br.readBits(1);
                    writeCoeff(category, (signBit & 1u) ? -1 : 1);
                    break;
                }
                default:
                    ++front;  // unreachable (sel is a 2-bit value, 0-3 exhaustive)
                    break;
            }
        }
        return result;
    }

    // A > 1: genuine Phase-1 BFS (Sec5.3j Part 2), now Gate-A-gated on every
    // node visit per Part 2's own prose ("read the NEXT node's own 1-bit
    // 'is-leaf' flag ... before looping back to front's successor") -
    // previously unmodeled, judged UNIVERSAL (not Phase-2-only) per
    // Sec5.3w's own "for EVERY node visited in the main loop" phrasing and
    // Part 2's identical wording for this exact loop.
    uint32_t level = A;
    uint32_t biasMask = (level >= 1) ? (1u << (level - 1)) : 0u;

    // Mechanism 1 (Sec5.3j Part 3, bit-exact-confirmed live by Sec5.3ac for
    // case 3's own terminal leaves and Gate B's own resolve-now branch):
    // masked-magnitude + pre-mask sign snapshot, field width = the CURRENT
    // outer `level` (confirmed to track the live level, not a fixed
    // constant, by idx4's 3-bit-vs-idx3's-2-bit field-width difference).
    auto resolveMechanism1 = [&](uint32_t cat) {
        if (level == 0) return;
        const uint32_t raw = br.readBits(static_cast<int>(level));
        const uint32_t masked = (raw & (biasMask - 1u)) | biasMask;
        int32_t value = static_cast<int32_t>(masked);
        if (raw & biasMask) value = -value;  // sign from the pre-mask raw snapshot, Sec5.3j Part 3
        writeCoeff(cat, value);
    };

    // Gate B's defer target: a SEPARATE, backward-growing list (Sec5.3ac),
    // NOT this loop's own front/tail worklist - see top-of-file comment for
    // why (the trace's own "front catches tail... before Phase 2 drains the
    // deferred entries" sequencing). Merged into `tags`/`tail` only once
    // Phase 1's own BFS fully completes, below.
    std::array<uint8_t, kAcTreeWorklistCapacity> deferred{};
    size_t deferredCount = 0;
    auto pushDeferred = [&](uint8_t t) {
        if (deferredCount < deferred.size()) {
            deferred[deferredCount++] = t;
        }
    };

    // Gate B (Sec5.3ac): 4 unrolled 1-bit copies, categories cat..cat+3 -
    // reached from BOTH case 0 and case 2 (confirmed live by the address
    // map: "Gate B's 4 unrolled copies (Phase 1, reached from case 0/case
    // 2)"). Each copy: bit clear -> resolve NOW via mechanism 1; bit set ->
    // defer (push a synthetic (cat+i)*4+3 ["case 3"-shaped] tag onto the
    // side list above). `catI = cat + i` regardless of resolve-vs-defer -
    // independently cross-checked against Sec5.3ac's own "cat increments
    // once per copy regardless of resolve-vs-defer" finding (see top-of-
    // file comment).
    auto runGateB = [&](uint32_t cat) {
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t gateB = br.readBits(1);
            const uint32_t catI = cat + i;
            if (gateB == 0) {
                resolveMechanism1(catI);
            } else {
                pushDeferred(static_cast<uint8_t>(catI * 4u + 3u));
            }
        }
    };

    do {
        while (front < tail) {
            const uint32_t gateA = br.readBits(1);
            if (gateA == 0) {
                ++front;
                continue;
            }
            const uint8_t tag = tags[front];
            if (tag == 0) {
                ++front;
                continue;
            }
            const uint32_t sel = tag & 3u;
            const uint32_t category = static_cast<uint32_t>(tag >> 2);
            const uint32_t base = category * 4u;

            switch (sel) {
                case 0: {
                    // Rewrite own slot in place; do NOT advance front - the
                    // same slot is Gate-A-checked again next iteration
                    // (same shape as the A<=1 branch's own case 0, now
                    // confirmed live for A>1 too by Sec5.3ac).
                    tags[front] = static_cast<uint8_t>(base + 0x11);
                    runGateB(category);
                    break;
                }
                case 1: {
                    // Node resolved in place (tag' = (tag>>2)*4+2) AND
                    // THREE new children pushed - a 4-way split, zero bits.
                    tags[front] = static_cast<uint8_t>(base + 2);
                    pushTag(static_cast<uint8_t>(base + 0x12));
                    pushTag(static_cast<uint8_t>(base + 0x22));
                    pushTag(static_cast<uint8_t>(base + 0x32));
                    ++front;
                    break;
                }
                case 2: {
                    // Sec5.3ac (2026-10-11 live update): case 2 falls into
                    // Gate B's shared chain via its OWN distinct entry
                    // address (0x100250b8, confirmed different from case
                    // 0's 0x1002523d) - CONFIRMED LIVE (idx4 trace, sel=2
                    // dispatched at bit 33, Gate B entered at bit 34) that
                    // case 2, unlike case 0, DOES advance front before Gate
                    // B. No longer a judgment call.
                    tags[front] = static_cast<uint8_t>(base + 0x11);
                    ++front;
                    runGateB(category);
                    break;
                }
                case 3: {
                    // Case dispatch's OWN resolve write (0x1002546c) - a
                    // single unconditional mechanism-1 resolve, confirmed
                    // live by the idx3 trace's "three case-3 resolutions in
                    // a row."
                    tags[front] = 0;
                    ++front;
                    resolveMechanism1(category);
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

    // Merge Gate B's deferred list into the SAME tags/tail worklist Phase 2
    // below walks, as fresh sel==3-tagged nodes (Sec5.3ac: "Phase 2
    // resolves them via its own sel==3 arm").
    for (size_t i = 0; i < deferredCount; ++i) {
        pushTag(deferred[i]);
    }

    // Phase 2 (Sec5.3j Part 2/Part 3) - unchanged in overall mechanics from
    // the prior version of this file (NOT given its own Gate A - no golden
    // trace to date exercises A>1's Phase 2, so this is left as the
    // already-confirmed Part 3 mechanics rather than speculatively
    // extended). See top-of-file comment for the (a)-(d) CHOSEN judgment
    // calls this section relies on. Gated on the ORIGINAL level nibble `A`.
    if (A != 0) {
        size_t front2 = 0;  // starts at buffer index 0 (judgment call (a))
        while (front2 < tail) {
            const uint8_t tag2 = tags[front2];
            if (tag2 == 0) {
                ++front2;
                continue;
            }
            const uint32_t sel2 = tag2 & 3u;
            // Spec-CONFIRMED (Part 3, 0x1002556a / 0x10025716): the
            // coefficient-array index Phase 2 writes to is `tag>>2`
            // directly, now routed through the Sec5.3w category->raster-k
            // permutation (judgment call, see top-of-file comment).
            const uint32_t category = static_cast<uint32_t>(tag2 >> 2);
            const uint32_t base2 = category * 4u;

            switch (sel2) {
                case 0: {
                    // Judgment call (c): undocumented arm, treated as a pure
                    // pass-through.
                    tags[front2] = 0;
                    ++front2;
                    break;
                }
                case 1: {
                    // Spec-CONFIRMED (Part 3, 0x10025522-0x1002555f): pure
                    // structural requeue, NO sign recovery - retag in place
                    // to a sel==2 shape, push 3 more sel==2-tagged children.
                    // Judgment call (d): front2 does NOT advance here - the
                    // same slot is re-dispatched next iteration under its
                    // freshly-written sel==2 tag.
                    tags[front2] = static_cast<uint8_t>(base2 + 2);
                    pushTag(static_cast<uint8_t>(base2 + 0x12));
                    pushTag(static_cast<uint8_t>(base2 + 0x22));
                    pushTag(static_cast<uint8_t>(base2 + 0x32));
                    break;
                }
                case 2: {
                    // Spec-CONFIRMED (Part 3, 0x1002556a-0x10025637): mark
                    // the slot consumed immediately, THEN read one
                    // categorization bit. Bit clear -> defer further via a
                    // fresh sel==3-tagged ("+3") node, no value yet. Bit set
                    // -> read one more bit as the sign and resolve now via
                    // mechanism 2 (magnitude always exactly 1).
                    tags[front2] = 0;
                    ++front2;
                    const uint32_t gateBit = br.readBits(1);
                    if (gateBit == 0) {
                        pushTag(static_cast<uint8_t>(base2 + 3));
                    } else {
                        const uint32_t signBit = br.readBits(1);
                        writeCoeff(category, (signBit & 1u) ? -1 : 1);  // mechanism 2: "and esi,1; neg esi; or esi,1"
                    }
                    break;
                }
                case 3: {
                    // Spec-CONFIRMED (Part 3, 0x10025716-0x1002575c):
                    // unconditional mechanism 2 - no gating bit, always read
                    // one bit as the sign, magnitude always exactly 1.
                    tags[front2] = 0;
                    ++front2;
                    const uint32_t signBit = br.readBits(1);
                    writeCoeff(category, (signBit & 1u) ? -1 : 1);
                    break;
                }
                default:
                    ++front2;  // unreachable (sel is a 2-bit value, 0-3 exhaustive)
                    break;
            }
        }
    }

    return result;
}

}  // namespace sr3binkcodec
