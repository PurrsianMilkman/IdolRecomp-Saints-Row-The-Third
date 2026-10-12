#pragma once

// sr3binkcodec - the per-row DC delta-accumulation array, `0x1001a160`.
//
// SUPERSEDES descriptor_l_cursor.h (2026-10-11, this task): spec-bink-codec.md
// Sec5.3p retracts that file's own "descriptor L is a SEPARATE byte-cursor
// stream" framing. Direct address+value correlation (a live Unicorn
// write-watch, `scratchpad/descriptorL_source_2026-10-11/identify_ctx.py`)
// found "descriptor L"'s buffer is NOT a separate pre-stage output at all -
// it IS `0x1001a160`'s own per-row DC-delta-accumulation array, written in
// place as a side effect of that function's own primary decode loop, which
// reads directly from the SAME main per-row compressed bitstream the
// AC-coefficient tree decode and per-block dispatch nibbles also consume
// (NOT a separately pre-decompressed buffer, NOT the entropy pre-stage's
// own case-selector stream either). The exact write that produces block
// (0,0)'s own real DC literal (address `0x303f8620`, value `0x0076`=118) is
// the FIRST word this mechanism writes for the frame's Y-plane context -
// Sec5.3p: "the cursor for block (0,0) starts at the buffer's own base,
// offset 0".
//
// FIX (2026-10-11, fix/bink-dc-array-value0-boundary-5.3u task, closes the
// Sec5.3m-vs-Sec5.3q "contradiction" per the now-resolved Sec5.3u): the
// EARLIER version of this file applied the chunk-loop's class-nibble-driven
// variable-width read to value[0] too (reading a phantom 4-bit nibble
// before it that does not exist in the real stream at that point - Sec5.3u's
// own hypothesis (a), confirmed correct by this task). That desynced every
// value downstream (value[0] came out 249 instead of 118). Sec5.3u's own
// live per-symbol trace against the real binkw32.dll/main_menu_bg.bik
// resolves this: value[0] and values[1+] are TWO DIFFERENT MECHANISMS, not
// one:
//   - value[0] (the very first DC value of a call): the FIXED-bound path
//     ONLY (Sec5.3p/Sec5.3q, `0x1001a1fa`-`0x1001a2fd`) - no class nibble, no
//     predictor accumulation (there is no "previous value" yet). 21 bits
//     total for this real sample.
//   - values[1+] (every value AFTER value[0], same call): Sec5.3m's chunk
//     loop (`0x1001a330`-`0x1001a456`) - UNCHANGED from the prior version of
//     this file, just no longer reached for value[0].
//
// VALUE[0]'S OWN BIT-LEVEL SHAPE, DERIVED this task from the real compressed
// bitstream (frame0_raw.bin, main_menu_bg.bik, Y-plane row 0 - the SAME
// bytes Sec5.3u's own live trace reads, just read directly as data here,
// never via disassembly/Unicorn re-execution, per this project's standing
// "Team B never disassembles" rule): at the already-independently-confirmed
// bit position 9256 (dc_prestage_sibling_gap.h's consumeDcArrayPreStageGap(),
// Sec5.3q/r, unchanged by this task), the real bitstream yields, in order:
// a 10-bit field (519 for this sample) then an 11-bit field (118 for this
// sample - EXACTLY the real DC literal, no arithmetic needed) - 21 bits
// total, an exact match to Sec5.3u's own measured cost. The two field
// widths are not invented: they are the SAME two constants Sec5.3q's own
// live ctx dump names for this exact call - `ctx+0x28=10` (the width this
// project already uses for the sibling family's own magnitude field,
// dc_prestage_sibling_gap.h's kDcSiblingMagnitudeWidth) and `ctx+0x08=0xb`
// (11 - Sec5.3q flags this as "a third persistent small constant, role not
// traced this pass"; this task's own real-data read resolves that role: it
// is the WIDTH of the real DC value's own direct-read field, immediately
// after the first 10-bit field). Read as: `sanity10 = readBits(10)` (the
// `ctx+0x2c=1504`-halved-to-752 overflow-SANITY field Sec5.3q names - this
// sample's real value, 519, is comfortably under 752, so no bail/poison
// branch is exercised or modelled here, same convention this project
// already uses for the structurally-identical LEN/CAP=752 field in
// entropy_prestage.h's CaseSelectorScratchSource::refill() top comment:
// "LEN > desc->CAP half NOT checked"); then `value0 = readBits(11)` directly,
// no sign, no transform - matching Sec5.3u's own trace table ("formula:
// direct"). OPEN, flagged honestly, not modelled (same posture as the
// already-accepted CAP convention above): what happens when sanity10 >= 752
// (an overflow/poison branch, not exercised by this real sample, not given
// at the instruction level in any spec text available to this task).
//
// THE CHUNK-LOOP ALGORITHM THIS FILE IMPLEMENTS FOR values[1+] (spec-bink-
// codec.md Sec5.3m, "Full chunk-loop disassembly pins the exact commit
// mechanism" - CONFIRMED, address-exact, independently re-disassembled
// against the real `binkw32.dll` before splicing; UNCHANGED by this task):
//   Per chunk (each chunk covers up to 8 output words):
//     1. Read a 4-bit "class" nibble (`0x1001a363`/`0x1001a375`) - from the
//        SAME shared bit-accumulator state the row's main bitstream uses
//        (Sec5.3p: "ebx/edx/esi, loaded at function entry from the
//        function's own 2nd argument's +0/+4/+8 fields - the already-known
//        pointer/bit-position/accumulator triple for a row's main
//        bitstream").
//     2. `nibble == 0`: flat-fill path (`rep stosd`/`rep stosw`,
//        `0x1001a421`-`0x1001a44d`) - all (up to) 8 words in this chunk get
//        the PREVIOUS committed accumulator value, UNCHANGED, with no
//        further bitstream consumption beyond the 4-bit nibble itself.
//     3. `nibble != 0`: for each of up to 8 words in the chunk:
//        a. Extract `nibble`-bit raw magnitude (`0x1001a394`-`0x1001a3a0`).
//        b. If magnitude == 0, the delta is 0 and the sign bit is skipped
//           entirely (`je 0x1001a405` from the zero-test at `0x1001a3de`).
//        c. Otherwise consume exactly one more bit (`test cl,1` at
//           `0x1001a3fe`) and negate the magnitude if it is set
//           (`neg eax`, `0x1001a403`).
//        d. The signed delta is then added to the previous committed value
//           (`0x1001a405`/`0x1001a408`), truncated to 16 bits, and becomes
//           the new committed/output value AND the new "previous" for the
//           next word in the same chunk (`0x1001a40a`-`0x1001a417` -
//           Sec5.3n's own corrected hook placement: the real write happens
//           at `0x1001a40d`, one instruction after the stale
//           `mov eax,[ebp-0xc]` §5.3m's own first pass mis-hooked).
//     4. Loop continues while chunk words remain (`0x1001a41d`), then falls
//        through to the outer per-chunk-count decrement
//        (`0x1001a450`-`0x1001a456`).
//   Sec5.3u's own live-captured EXACT per-symbol trace, first 8 values (now
//   a confirmed ground-truth worked example, asserted verbatim by
//   tests/real_bink_dc_value0_boundary_test.cpp added by this task):
//   118 (value[0], fixed-bound), 199, 212, 51, 25, 118, 119, 26 (values[1-7],
//   all chunk-loop, nibble=8 for all 7).
//
// GRANULARITY CHOICE, DERIVED not independently confirmed (unchanged by this
// task, flagged per this project's "no invented fixes" convention): this
// file's decodeRowDcArray() runs ONCE PER ROW, directly off the row's own
// main bitstream (`br`), BEFORE any block in that row reads from it -
// matching this task's own explicit instruction ("the buffer fill... must
// happen for the WHOLE ROW before any block in that row reads from it")
// and the same per-row re-anchor granularity descriptors A/B already use
// for their own `0x10019360`-style refill. UPDATED 2026-10-11 (fix/bink-dc-
// array-519-word-commit-run task, Sec5.3n): the call no longer produces
// exactly `blockCols` words - it produces its own REAL leading-count-driven
// total (519 for this row, not a clean multiple of 240), matching Sec5.3n's
// own live telemetry ("a single `0x1001a160` CALL's commit-run length is
// 336-607, not a clean multiple of 240") exactly, now for the right reason
// (the call's own real word count, not an early stop at the caller's
// column count). Call-granularity and row-granularity are still NOT proven
// identical in general for calls OTHER than this row's first one - that
// broader mapping remains OPEN (Sec5.3n's own item 1) - but this task's own
// scope (frame 0/Y-plane row 0's first call) is now pinned, not derived.
//
// WHAT REMAINS GENUINELY OPEN (per Sec5.3p's own named items, not resolved
// here): the exact condition selecting the `0x1001a307` (first-symbol-of-
// call) vs. `0x1001a40d` (subsequent-symbol) write site - this file does
// not need to distinguish them structurally beyond the value[0]/values[1+]
// split this task implements, since Sec5.3u's own trace already pins which
// mechanism fires for which position; and the `ctx+0`/`ctx+4` commit-style
// bookkeeping pair's own real role across MULTIPLE rows/calls (this file
// never needs it, since it re-derives a fresh array from `br`'s own current
// position every row rather than persisting any cross-row state of its
// own).
//
// HISTORY: this file previously (incorrectly) applied the chunk-loop's
// nibble-driven read to value[0] as well, seeding its accumulator at a
// DERIVED 0 - both the nibble-on-value[0] bug AND the seed-at-0 assumption
// are now moot, since value[0] has its own real decode above and the chunk
// loop's own "previous" for value[1] is simply value[0] itself (Sec5.3u
// item 3: "value[0] seeds the predictor for the chunk loop's own first
// `prev_value`"), not an independent seed choice.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/dc_prestage_sibling_gap.h"  // kDcSiblingMagnitudeWidth, reused for value[0]'s own sanity field

namespace sr3binkcodec {

// value[0]'s own real DC-value-field width (spec-bink-codec.md Sec5.3q's
// live ctx dump: `ctx+0x08=0xb`=11) - see this file's own top comment for
// how this task derived its role from the real bitstream content, not
// independently confirmed by disassembly.
constexpr int kDcValue0FieldWidth = 11;

// One row's worth of DC-delta-accumulated 16-bit words, index-addressed
// (Sec5.3p: "a plain array index... index N reads the Nth 16-bit word the
// array-decode step produced so far"). Produced ONCE per row by
// decodeRowDcArray() below, consumed by block_dispatch.h's
// Case1SubType::DcAcIdctEngine2 case via wordAt(blockCol) - see that file's
// own "DC ARRAY WIRING" note.
class DcDeltaArray {
public:
    explicit DcDeltaArray(std::vector<uint16_t> words) : words_(std::move(words)) {}

    bool hasIndex(size_t index) const { return index < words_.size(); }
    uint16_t wordAt(size_t index) const { return words_[index]; }
    size_t size() const { return words_.size(); }

private:
    std::vector<uint16_t> words_;
};

// FIX (2026-10-11, fix/bink-dc-array-519-word-commit-run task, spec-bink-
// codec.md Sec5.3n's "first call's exact count PINNED" follow-up): the
// PRIOR version of this function discarded the 10-bit field read right
// before value[0] (`sanity10`, "value itself unused by this project's
// model") and instead stopped the chunk loop as soon as `minCount`
// (`blockCols`, 240 for this row) words had been produced. Sec5.3n pins
// that discarded field as the REAL thing: "the identical leading-count
// idiom Sec5.3s documents for 0x100195b0's 650 and Sec5.3t for 0x10019d10's
// 528 - the count pins the call's own output END address... BEFORE the
// chunk-decode loop begins; the chunk loop terminates purely on the
// remaining count reaching zero - no bitstream sentinel, no fixed caller
// argument, no other terminator exists in the function body. The count is
// a word count directly, no byte->word translation needed." For frame 0's
// Y-plane row 0's first call specifically this field reads 519 (Sec5.3n,
// independently cross-validated two ways), not 240 - stopping at 240 left
// ~279 words' worth of real chunk-loop bits (and therefore `br`'s own
// downstream bit position for every later read in the row) unconsumed.
// This is very likely why row 0's AC-tree/dispatch reads for idx0/3/4 were
// landing on garbage despite the AC-tree/dequant/IDCT/flat-fill stages all
// being independently correct in isolation.
inline constexpr int kDcValue0LeadingCountFieldWidth = kDcSiblingMagnitudeWidth;  // ctx+0x28=10, same field, now used for real

// Runs the Sec5.3u-resolved two-stage algorithm against `br`'s CURRENT bit
// position - the SAME shared per-row bitstream the AC-tree decode/dispatch
// nibbles also consume (Sec5.3p). The call's REAL word count is read as a
// leading 10-bit field at call entry (Sec5.3n's leading-count idiom, NOT
// the caller-supplied `minCount`) and the chunk loop runs until that many
// words are produced, exactly like the real `0x1001a160`. `minCount` is
// kept only as the caller's own minimum-need hint/diagnostic (a caller
// still only reads the first `minCount` of the words actually produced,
// via hasIndex()/wordAt()) - it is NOT the loop's own termination bound
// any more. Stops early (returning however many words were produced so
// far) if `br` runs out of real data mid-decode, or if the leading count
// itself reads as 0 (Sec5.3n's own "second call, zero-count idle/bail"
// shape - a genuine no-op commit for THIS call) - callers must check
// hasIndex() before wordAt(), exactly like the superseded DescriptorLCursor's
// own bound check.
inline DcDeltaArray decodeRowDcArray(BitstreamReader& br, int minCount) {
    std::vector<uint16_t> words;
    (void)minCount;  // no longer the loop bound - see this function's own comment above

    // LEADING COUNT: the real per-call word count (Sec5.3n: 519 for frame 0's
    // Y-plane row 0's first call), read BEFORE value[0]'s own data, same
    // ctx+0x28=10-bit field width as the sibling family's own leading-count
    // reads (dc_prestage_sibling_gap.h's skip0x100195b0()/skip0x10019d10()).
    const uint32_t leadingCount = br.readBits(kDcValue0LeadingCountFieldWidth);
    if (br.ranOutOfData()) return DcDeltaArray(std::move(words));
    const int realCount = static_cast<int>(leadingCount);
    if (realCount <= 0) return DcDeltaArray(std::move(words));  // zero-count bail, no array write (Sec5.3n)
    words.reserve(static_cast<size_t>(realCount));

    // VALUE[0]: fixed-bound path ONLY (Sec5.3u item 1) - no class nibble, no
    // predictor accumulation. See this file's own top comment for the
    // real-data-derived 10-bit-leading-count + 11-bit-direct shape.
    {
        const uint32_t value0 = br.readBits(kDcValue0FieldWidth);  // ctx+0x08=0xb=11, Sec5.3q/this task - direct value, no sign
        if (br.ranOutOfData()) return DcDeltaArray(std::move(words));
        words.push_back(static_cast<uint16_t>(value0));
    }

    uint16_t prev = words[0];  // Sec5.3u item 3: value[0] seeds the chunk loop's own first prev_value.

    // VALUES[1+]: Sec5.3m's chunk loop, now bounded by the call's own REAL
    // word count (`realCount`, Sec5.3n) instead of the caller-supplied
    // `minCount`. FIX (this task, Sec5.3n's own "1 + 64x8 + 6 = 519" live
    // write-count breakdown): a chunk covers "up to 8" output words, not
    // ALWAYS exactly 8 - the live count breaks down as 64 FULL chunks of 8
    // plus one FINAL PARTIAL chunk of only 6 (not 8), the loop terminating
    // "purely on remaining count reaching zero" (Sec5.3n). The prior version
    // of this loop always pushed a full 8 per chunk (harmless when the
    // caller's own `minCount` bound simply discarded the overshoot via
    // hasIndex()/wordAt(), but WRONG now that `realCount` is the real
    // array's own total size and must land exactly on it) - each chunk
    // below is now capped at `realCount - words.size()` words, matching the
    // real decoder's own partial last chunk.
    while (static_cast<int>(words.size()) < realCount) {
        const uint32_t classNibble = br.readBits(4);  // Sec5.3m: 0x1001a363/0x1001a375
        if (br.ranOutOfData()) break;

        const int wordsThisChunk = std::min(8, realCount - static_cast<int>(words.size()));

        if (classNibble == 0) {
            // Sec5.3m: flat-fill - previous accumulator unchanged, no
            // further bitstream consumption this chunk.
            for (int i = 0; i < wordsThisChunk; ++i) words.push_back(prev);
            continue;
        }

        bool exhausted = false;
        for (int i = 0; i < wordsThisChunk; ++i) {
            const uint32_t magnitude = br.readBits(static_cast<int>(classNibble));  // Sec5.3m: nibble-bit magnitude
            if (br.ranOutOfData()) {
                exhausted = true;
                break;
            }
            int32_t delta = static_cast<int32_t>(magnitude);
            if (magnitude != 0) {
                // Sec5.3m: sign bit skipped entirely when magnitude==0
                // (0x1001a3de's je) - otherwise one more bit, neg if set.
                const uint32_t signBit = br.readBits(1);
                if (br.ranOutOfData()) {
                    exhausted = true;
                    break;
                }
                if (signBit != 0) delta = -delta;
            }
            prev = static_cast<uint16_t>(static_cast<int32_t>(prev) + delta);  // truncate to 16 bits, Sec5.3m
            words.push_back(prev);
        }
        if (exhausted) break;
    }
    return DcDeltaArray(std::move(words));
}

}  // namespace sr3binkcodec
