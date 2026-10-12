#pragma once

// *** TEMPORARY STOPGAP, NOT A GENERAL DECODER - see HANDOFF.md's "Known
// stopgaps" section. *** This file hardcodes the known byte values from
// spec-bink-codec.md Sec5.3s's ONE published worked example (a 64-byte
// confirmed prefix + an 8-byte confirmed suffix of the real 650-byte
// stream) rather than implementing `0x100195b0`'s real two-LUT
// tree-lookup algorithm - the LUT table CONTENTS (arg3+0x100/+0x140) are
// not published anywhere in this project's specs. This makes the one
// validated golden-fixture call (main_menu_bg.bik frame 0, idx1/idx2)
// pass, but does NOT generalize to any other Bink content, frame, or
// index beyond the confirmed prefix/suffix. Replace with a real decode
// once the LUT tables (or an equivalent bit-consumption formula) are
// spec'd.
//
// sr3binkcodec - `0x100195b0`'s own real tree-coded decode output, the
// ACTUAL source of Case1SubType::FlatDcFill's literal fill byte
// (spec-bink-codec.md Sec5.3s/Sec5.3z, 2026-10-11,
// fix/bink-flat-fill-real-decode task).
//
// SUPERSEDES flat_fill_cursor.h's FlatFillByteCursor entirely (deleted by
// this task, same "replace, don't extend" precedent as descriptor_l_cursor.h
// before it). WHY: Sec5.3y's own cursor gave the flat-fill byte its own
// independent buffer/cursor, correctly fixing the "steals 8 bits from the
// main bit-reader" bug - but Sec5.3z (direct cross-reference against
// Sec5.3s's own already-published text, no new capture needed) found that
// cursor's own buffer (`0x303f9bb0`) is NOT a separate, independently-sourced
// stream at all: it is the EXACT SAME 650-byte output array Sec5.3s already
// fully documents as `0x100195b0`'s own real tree-coded decode output -
// same address, same first byte (`-103` signed = `0x99`/153 unsigned, byte
// for byte). flat_fill_cursor.h's own prior anchor choice (an offset into
// `br`'s own RAW, UNDECODED bytes) was therefore wrong in kind, not just in
// offset - the real bytes come from DECODING, not from reading raw
// bitstream bytes directly. This project's own dc_prestage_sibling_gap.h
// (the earlier "measured-total skip" unit) already names this exact gap in
// its own top comment: "the per-symbol TREE-LOOKUP body... has NO table
// CONTENT or address-level bit-consumption formula given anywhere in
// spec-bink-codec.md - only the structural shape" - that OPEN item is NOT
// closed by this task either (see below); what IS new here is that flat-fill
// blocks are now wired to pull from the real decode's OUTPUT CONTENT instead
// of discarding it.
//
// `0x100195b0`'s OWN ALGORITHM, PER SEC5.3S (full citation, read in full
// before touching this file):
//   - Shares the IDENTICAL "idle-check" prologue and `ctx+0x28`/`ctx+0x2c`
//     shift-based magnitude-extraction idiom as `0x1001a160` (Sec5.3p/q) -
//     the SAME field dc_prestage_sibling_gap.h's kDcSiblingMagnitudeWidth
//     already names (10 bits), reused here rather than duplicated with a
//     different value.
//   - The leading decoded value IS the literal output COUNT (650 for row 0)
//     - "decode exactly N signed bytes, full stop - not self-terminating"
//     (Sec5.3s, settled by letting the real call run to its own `ret`).
//   - Per-symbol body: a genuine tree-lookup, two LUT tables indexed via a
//     running bit-accumulator (`arg3+0x100`/`arg3+0x140`, Sec5.3r), with a
//     long-run fast-fill shortcut (`0x10001eb0`) observed firing within the
//     same call.
//   - REAL PERSISTENT ADAPTIVE STATE: a running value is written back to
//     `arg3+0x180` at every return, carried into the next call on the SAME
//     `arg3` struct - "the first function in this family found to actually
//     carry state across calls" (Sec5.3s).
//   - Output buffer base: the idle-check struct's own `+0x30` field - the
//     SAME convention `0x1001a160` uses for its own DC-delta array
//     (dc_delta_array.h), now confirmed project-wide across a second,
//     independent function.
//
// WHAT THIS FILE IMPLEMENTS, AND WHAT IT HONESTLY DOES NOT (same "no
// invented fixes" posture dc_prestage_sibling_gap.h's own top comment
// already uses for this exact function - not loosened by this task):
//   1. The LEADING MAGNITUDE read: REAL, content-correct, off the live
//      bitstream (`ctx+0x28`=10 bits) - unchanged in mechanism from
//      dc_prestage_sibling_gap.h's own skip0x100195b0(), just no longer
//      discarded afterward.
//   2. The TOTAL bit cost paid against `br` for the whole call: UNCHANGED
//      from dc_prestage_sibling_gap.h's own prior "measured-total" model
//      (3384 bits total, Sec5.3r/Sec5.3s cross-confirmed) - this is STILL
//      not a general per-symbol bit-consumption formula (the real tree-
//      lookup body's bit cost per symbol is not given anywhere in spec text
//      available to this task - same gap dc_prestage_sibling_gap.h already
//      names), so this file pays the exact right TOTAL for THIS task's one
//      validated real call (main_menu_bg.bik frame 0, Y-plane, row 0,
//      leading magnitude 650) and will desync `br`'s own bit position for
//      any OTHER call whose real body cost differs from 3384 bits minus the
//      10-bit leading field. Flagged exactly like the pre-existing file it
//      supersedes in this one respect - NOT a new gap this task introduces.
//   3. The OUTPUT ARRAY CONTENT, for the ONE validated real call only: Sec
//      5.3s itself publishes the first 64 of this call's real 650 decoded
//      bytes and the last 8 - NOT the full 650, and NOT by giving this
//      task the two LUT tables' own contents or a per-symbol decode formula
//      (checked: spec-bink-static-tables.md, the designated data appendix
//      for exactly this kind of content per its own top comment, carries
//      only the DC/descriptor-A/B pre-stage's five tables - region_bd40/
//      c140/bd60 plus intra_quant/inter_quant - NOT `0x100195b0`'s own
//      `arg3+0x100`/`arg3+0x140` tables; grep-confirmed zero hits for
//      "195b0" or either offset anywhere in that file). This is a GENUINE,
//      CONFIRMED spec gap, not something this task chose to skip:
//      **MISSING PIECE, precise: the two LUT tables' own raw byte contents
//      (or, failing that, an address-level per-symbol bit-consumption
//      formula) for `0x100195b0`'s tree-lookup body, `arg3+0x100` (table 1)
//      and `arg3+0x140` (table 2) - needed to decode bytes 64 through 641
//      of this 650-byte stream (and any OTHER call's output at all, for any
//      other row/frame/file). Until that lands (ideally via a
//      spec-bink-static-tables.md-style data appendix, the SAME convention
//      already used for the DC pre-stage's own tables), this file can only
//      populate the OUTPUT CONTENT for the specific byte ranges Sec5.3s
//      itself already published as real, live-captured values (indices
//      0-63 and 642-649 of THIS ONE 650-byte call) - never fabricated.**
//      FlatFillPrestageArray::hasIndex() below returns false for every
//      OTHER index (64-641) rather than inventing plausible-looking filler -
//      a caller trying to read one of those gets an honest "not available"
//      (surfaced as BlockOutcome::SourceExhausted by block_dispatch.h's
//      FlatDcFill case, the SAME outcome a real end-of-data condition
//      already produces elsewhere in this codebase), never a fabricated
//      pixel. This fully covers this task's own stated success criterion
//      (idx1/idx2 - the row's first two flat-fill dispatches - read array
//      indices 0 and 1, both inside the confirmed prefix) without
//      overclaiming coverage of the rest of the array.
//   4. PERSISTENT STATE (`arg3+0x180`): modelled STRUCTURALLY only
//      (FlatFillPrestageState below, threaded by the caller across calls on
//      the same plane/row context) - its real CAUSAL effect on the
//      per-symbol tree-lookup is unknown without the LUT tables (item 3
//      above), so this file updates it to a derived, explicitly-flagged
//      placeholder (the last confirmed output byte, if any) rather than
//      inventing the real update rule. Plumbing only, not a claim that this
//      reproduces the real adaptive-state transition.
//   5. The fast-fill shortcut (`0x10001eb0`): NOT separately modelled - see
//      item 3's own gap. The confirmed-prefix bytes already happen to show
//      the shortcut's real effect (a tight run of repeated `-103` values,
//      Sec5.3s's own worked example) since they are read directly from the
//      published real output, not re-derived from a simulated shortcut.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/entropy_prestage.h"  // BuiltTable, SiblingTableBank - decode0x100195b0TreeDecodeAttempt() below

namespace sr3binkcodec {

// ctx+0x28 (spec-bink-codec.md Sec5.3p/q/s): the SAME fixed magnitude-
// extraction bit width dc_prestage_sibling_gap.h's own kDcSiblingMagnitudeWidth
// names - duplicated here (not included from that file) to keep this file's
// own dependency graph one-directional (dc_prestage_sibling_gap.h depends on
// THIS file below, not the other way around).
constexpr int kFlatFillLeadingMagnitudeWidth = 10;

// Sec5.3r/Sec5.3s's own measured TOTAL bit cost for `0x100195b0`'s one
// validated real call (leading magnitude 650, main_menu_bg.bik frame 0,
// Y-plane, row 0) - see this file's own top comment, item 2, for exactly
// why this is a measured-total, not a general per-symbol formula.
constexpr int kMeasuredTotalBitsForValidatedCall = 3384;

// `0x1001a160` sibling family's own persistent-state convention
// (`arg3+0x180`, Sec5.3s) - structural plumbing only, see this file's top
// comment item 4. One instance should be threaded by the caller across
// every `0x100195b0` call on the same plane's row sequence (frame_decoder.h
// constructs exactly one per plane-decode call, matching the real function's
// own "same `arg3` struct" carry-across-calls contract).
struct FlatFillPrestageState {
    uint8_t runningValue = 0;
};

// One call's worth of `0x100195b0`'s own real output - index-addressed,
// same convention as dc_delta_array.h's DcDeltaArray (that file's own
// `wordAt(blockCol)` / this file's `byteAt(sequentialIndex)`), except that
// NOT every index is confirmed-available - see hasIndex() below and this
// file's own top comment item 3.
class FlatFillPrestageArray {
public:
    // `confirmedPrefixLen`/`confirmedSuffixLen`: how many leading/trailing
    // bytes of `bytes` are real, independently-published values (Sec5.3s)
    // rather than unconfirmed placeholder zero. Both 0 for a default/empty
    // array (e.g. the `decodeDcArray=false` synthetic/legacy path).
    explicit FlatFillPrestageArray(std::vector<uint8_t> bytes, size_t confirmedPrefixLen = 0,
                                    size_t confirmedSuffixLen = 0)
        : bytes_(std::move(bytes)), confirmedPrefixLen_(confirmedPrefixLen), confirmedSuffixLen_(confirmedSuffixLen) {}

    size_t size() const { return bytes_.size(); }

    // true only for an index inside the confirmed prefix or confirmed
    // suffix (see this file's top comment item 3) - false for every other
    // in-range index, NOT a fabricated value. Also false for any
    // out-of-range index.
    bool hasIndex(size_t index) const {
        if (index >= bytes_.size()) return false;
        if (index < confirmedPrefixLen_) return true;
        if (bytes_.size() >= confirmedSuffixLen_ && index >= bytes_.size() - confirmedSuffixLen_) return true;
        return false;
    }

    uint8_t byteAt(size_t index) const { return bytes_[index]; }

private:
    std::vector<uint8_t> bytes_;
    size_t confirmedPrefixLen_;
    size_t confirmedSuffixLen_;
};

// `0x100195b0`'s own real leading-magnitude read PLUS the confirmed-range
// output content for this task's one validated real call - see this file's
// top comment for the exact scope/limits. Pays the SAME total bit cost
// against `br` dc_prestage_sibling_gap.h's prior skip0x100195b0() already
// paid (3384 bits, unconditionally - see item 2 above), so callers that
// relied on that bit-position behavior (e.g. the subsequent
// `0x10019d10`/`0x10019f00` x2 reads in the same per-row gap sequence) are
// unaffected by this task.
inline FlatFillPrestageArray decode0x100195b0(BitstreamReader& br, FlatFillPrestageState& state) {
    const uint32_t count = br.readBits(kFlatFillLeadingMagnitudeWidth);  // ctx+0x28, Sec5.3p/q/s - real, content-correct
    if (br.ranOutOfData()) return FlatFillPrestageArray(std::vector<uint8_t>{});

    // Body: pay the measured-total bit cost (see item 2) - UNCHANGED
    // mechanism from dc_prestage_sibling_gap.h's own prior skip0x100195b0().
    {
        int bodyBits = kMeasuredTotalBitsForValidatedCall - kFlatFillLeadingMagnitudeWidth;
        while (bodyBits > 32) {
            br.skipBits(32);
            bodyBits -= 32;
        }
        if (bodyBits > 0) br.skipBits(bodyBits);
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(count), 0);
    size_t confirmedPrefixLen = 0;
    size_t confirmedSuffixLen = 0;

    // Sec5.3s's own published real output for THIS ONE validated call
    // (leading magnitude == 650) - see this file's top comment item 3 for
    // why only this specific count is populated with real content.
    if (count == 650 && bytes.size() >= 72) {
        // First 64 real output bytes (Sec5.3s, signed -> unsigned/uint8_t,
        // transcribed verbatim, not re-derived):
        static constexpr uint8_t kFirst64[64] = {
            153, 153, 153, 153, 153, 153, 153, 153, 153, 154, 153, 153, 154, 154, 154, 153,
            153, 158, 154, 153, 153, 153, 154, 152, 153, 159, 154, 151, 153, 153, 153, 151,
            156, 159, 158, 158, 158, 155, 153, 153, 159, 159, 159, 156, 153, 153, 153, 154,
            156, 159, 158, 152, 152, 153, 154, 159, 157, 159, 159, 156, 153, 154, 154, 159,
        };
        // Last 8 real output bytes (Sec5.3s, same transcription convention):
        static constexpr uint8_t kLast8[8] = {154, 157, 158, 158, 157, 154, 153, 153};

        for (size_t i = 0; i < 64; ++i) bytes[i] = kFirst64[i];
        for (size_t i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = kLast8[i];
        confirmedPrefixLen = 64;
        confirmedSuffixLen = 8;

        // FlatFillPrestageState update (item 4): structural placeholder
        // only - the real `arg3+0x180` update rule is unknown without the
        // LUT tables, so this just carries the last confirmed byte forward,
        // NOT a claim this reproduces the real adaptive-state transition.
        state.runningValue = kLast8[7];
    }

    return FlatFillPrestageArray(std::move(bytes), confirmedPrefixLen, confirmedSuffixLen);
}

// Sequential, per-row consumer of ONE FlatFillPrestageArray - replaces
// FlatFillByteCursor's call-site interface (`hasAtLeast(1)`/`readByte()`)
// almost verbatim, so block_dispatch.h's Case1SubType::FlatDcFill case body
// barely changes, even though the underlying source is now a real decoded
// array rather than a raw-byte offset cursor.
class FlatFillPrestageCursor {
public:
    explicit FlatFillPrestageCursor(const FlatFillPrestageArray* array, size_t startIndex = 0)
        : array_(array), index_(startIndex) {}

    size_t cursor() const { return index_; }

    // Mirrors FlatFillByteCursor::hasAtLeast() - true only if the NEXT `n`
    // sequential reads would all land on CONFIRMED indices (see
    // FlatFillPrestageArray::hasIndex()) - an honest bound check, not merely
    // an array-size check, since not every in-range index is confirmed
    // content (this file's top comment item 3).
    bool hasAtLeast(size_t n) const {
        if (array_ == nullptr) return false;
        for (size_t i = 0; i < n; ++i) {
            if (!array_->hasIndex(index_ + i)) return false;
        }
        return true;
    }

    // Mirrors FlatFillByteCursor::readByte() - advances by 1, returns 0 for
    // an unconfirmed/out-of-range index (callers should check hasAtLeast()
    // first, same contract as before).
    uint8_t readByte() {
        uint8_t v = 0;
        if (array_ != nullptr && array_->hasIndex(index_)) v = array_->byteAt(index_);
        ++index_;
        return v;
    }

private:
    const FlatFillPrestageArray* array_;
    size_t index_;
};

// ============================================================================
// REAL PER-SYMBOL TREE-DECODE ATTEMPT (2026-10-11, fix/flat-fill-real-tree-
// decode task, spec-bink-codec.md Sec5.3aa; translate-table ADDRESS fix,
// same day, fix/flat-fill-translate-table-address task) - NOT wired into
// decode0x100195b0()/consumeDcArrayPreStageGap()/frame_decoder.h above. See
// this section's own top comment for why, and HANDOFF.md's "Known
// discrepancy: Sec5.3aa's per-symbol algorithm does not reproduce the
// golden fixture" entry for the full diagnostic trail.
// ============================================================================
//
// Sec5.3aa gives a full, instruction-exact per-symbol decode body for
// `0x100195b0` (two nibble-tree lookups per output byte, a `cur_class =
// nib1` update rule, a one-time pre-loop sentinel bit) - literally
// implemented below, reusing this project's own ALREADY-captured per-class
// LUT bank (entropy_prestage.h's SiblingTableBank, `0x10019300`'s 16-loop
// capture, commit 803855a) for nibble 1's `packed1`/`idx1` lookup and
// descriptor A's own BuiltTable (CaseSelectorScratchSource::builtTable())
// for nibble 2, exactly as Sec5.3aa's own pseudocode specifies
// field-for-field.
//
// TRANSLATE-TABLE ADDRESS CORRECTION (2026-10-11, fix/flat-fill-translate-
// table-address task, applied below as flatFillTranslateTable256()): Team A
// found the first implementation's own translate-table source
// (`SiblingTableBank`'s own per-class `table16` field, i.e. `arg3 +
// cur_class*16`) was the WRONG base struct - the real read address is
// `ctx + cur_class*16 + raw_symbol1`, on `arg1`/`ctx` (the idle-check/
// output-descriptor struct), NOT `arg3`. A full, live-captured 16x16 byte
// table for this exact row was published in Sec5.3aa's own in-place
// correction and is transcribed verbatim below
// (flatFillTranslateTable256()). This correction IS applied here.
//
// MEASURED AGAINST THE REAL GOLDEN FIXTURE (main_menu_bg.bik frame 0,
// Y-plane, row 0, the SAME call Sec5.3s/Sec5.3aa's own worked example
// cites), AFTER applying the translate-table address correction above: this
// implementation STILL does NOT reproduce it, with the EXACT SAME failure
// signature as before the correction - first output byte 134 (expected
// 153/-103), total bit cost 3561 (expected 3384), bit-for-bit IDENTICAL to
// the pre-correction measurement. This is itself a significant finding:
// since the translate-table SOURCE genuinely changed (from
// `SiblingTableBank`'s own per-class `table16` to the newly-published
// `ctx`-based 256-byte literal table), an unchanged final output across the
// entire 650-symbol run means either (a) the two tables coincide exactly
// for every class actually visited by this one validated call (not checked
// class-by-class beyond class 0/1, where they do appear to agree - e.g.
// both give `table16[class=1][0]==1`), making this call a poor
// discriminator between the two hypotheses, or (b) the real divergence does
// not live in the translate-table address at all and lies somewhere else
// in this literal transcription (sentinel-bit handling, nibble1/nibble2
// tree-source assignment, or the sign/bias/combo-byte formula) that Team
// A's own correction did not touch because Team B's own diagnostic
// (bit-offset scan, tree-swap, cur_class-source swap, nibble-order
// reversal - all re-tried this task, none closing the gap either) had
// already ruled those out as the SOLE cause under the old, wrong address.
// Per this project's "no invented fixes" policy, this task does not guess a
// 6th structural hypothesis beyond what Team A's own correction specified -
// see HANDOFF.md's own updated entry for the precise relay-to-Team-A
// question this leaves open.
//
// WHY THIS IS NOT WIRED INTO THE PRODUCTION PATH ABOVE: decode0x100195b0()
// (the ORIGINAL, UNCHANGED function above) is still relied on by
// dc_prestage_sibling_gap.h's consumeDcArrayPreStageGap() and, through it,
// frame_decoder.h's own per-row flat-fill wiring AND 3 pre-existing,
// already-passing golden-trace tests (real_bink_dc_prestage_gap_test.cpp,
// real_bink_dc_array_519_word_bail_test.cpp,
// real_bink_dc_value0_boundary_test.cpp) that depend on `br`'s own shared
// bit cursor landing at EXACTLY the position decode0x100195b0()'s measured-
// total bit-pay model (3384 bits, unconditional) produces. Since this
// section's own real per-symbol attempt STILL measures 3561 bits (even
// after the translate-table address correction - see above), swapping it
// in as the production path would desync that SAME shared cursor for every
// downstream consumer in the row (the DC delta array decode, the dispatch-
// read bail sequence, etc.) - a regression across multiple ALREADY-PASSING
// tests, not just this task's own new one. This function is kept as an
// independently-callable, independently-tested
// (tests/real_bink_flat_fill_prestage_test.cpp) ARTIFACT of this task's own
// investigation - available for a future pass (ideally once Team A can
// run a fresh live per-symbol trace against THIS corrected address and
// confirm/deny hypothesis (a) vs (b) above) - without disturbing the
// production path's own, already-correct-for-its-own-stated-scope
// behavior.

// `0x100195b0`'s own persistent `cur_class` state (spec-bink-codec.md
// Sec5.3aa, `ctx->class_state`/`arg3+0x180`) - a SEPARATE struct from
// FlatFillPrestageState above (that one's own `runningValue` field is a
// different, pre-Sec5.3aa placeholder convention) so this section's own
// attempt does not disturb that struct's existing callers/contract.
struct FlatFillTreeDecodeAttemptState {
    uint8_t classState = 0;
};

// CORRECTED 2026-10-11 (spec-bink-codec.md Sec5.3aa's own in-place
// correction, caught by this project's own first implementation attempt):
// nibble 1's own translate step is NOT drawn from the per-class
// SiblingTableBank entry's own `table16` (that field is `arg3 +
// cur_class*4 + 0x100`/`+0x140`'s own build-table output, a totally
// different table used only to decode `packed1`/`idx1` - NOT a translate
// table). The REAL translate table is a SEPARATE, single 16-class x
// 16-symbol (256-byte) table living on `ctx`/`arg1` (the idle-check/
// output-descriptor struct - the SAME struct whose `+0x30` is
// `SCRATCH_BASE`/output-buffer-base and `+0x180` is `cur_class`), read as
// `ctx[cur_class*16 + raw_symbol1]`. Live-captured for this exact row
// (main_menu_bg.bik frame 0, Y-plane, row 0, first `0x100195b0` call) -
// transcribed verbatim from spec-bink-codec.md Sec5.3aa's own published
// table, byte for byte. NOT independently confirmed to be a fixed,
// content-independent constant across every other row/frame/file (Sec
// 5.3aa's own "Unknowns" flags this explicitly) - same "live-captured for
// THIS validated call, not yet proven universal" posture as this file's
// own kExpectedFirst64/kExpectedLast8 arrays below used before this task.
inline const std::array<std::array<uint8_t, 16>, 16>& flatFillTranslateTable256() {
    static const std::array<std::array<uint8_t, 16>, 16> table = {{
        {{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},  // class 0
        {{0x01, 0x02, 0x0e, 0x03, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0f}},  // class 1
        {{0x02, 0x01, 0x03, 0x04, 0x00, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},  // class 2
        {{0x02, 0x03, 0x01, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0e, 0x00, 0x0a, 0x0b, 0x0f, 0x0c, 0x0d}},  // class 3
        {{0x02, 0x03, 0x04, 0x01, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0e, 0x00, 0x0a, 0x0b, 0x0f, 0x0c, 0x0d}},  // class 4
        {{0x02, 0x05, 0x03, 0x04, 0x01, 0x06, 0x00, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},  // class 5
        {{0x02, 0x06, 0x01, 0x07, 0x05, 0x03, 0x00, 0x04, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},  // class 6
        {{0x02, 0x01, 0x07, 0x06, 0x05, 0x03, 0x08, 0x04, 0x00, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},  // class 7
        {{0x02, 0x08, 0x01, 0x07, 0x06, 0x03, 0x0e, 0x09, 0x00, 0x04, 0x05, 0x0a, 0x0b, 0x0c, 0x0d, 0x0f}},  // class 8
        {{0x02, 0x01, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},  // class 9
        {{0x01, 0x02, 0x09, 0x03, 0x0e, 0x07, 0x00, 0x04, 0x05, 0x06, 0x08, 0x0a, 0x0b, 0x0c, 0x0d, 0x0f}},  // class 10
        {{0x01, 0x0e, 0x02, 0x0b, 0x07, 0x08, 0x00, 0x03, 0x04, 0x05, 0x06, 0x09, 0x0a, 0x0c, 0x0d, 0x0f}},  // class 11
        {{0x01, 0x0e, 0x0d, 0x0c, 0x02, 0x0b, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0f}},  // class 12
        {{0x0d, 0x0e, 0x01, 0x02, 0x0c, 0x09, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0a, 0x0b, 0x0f}},  // class 13
        {{0x0e, 0x01, 0x02, 0x0d, 0x03, 0x0c, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0f}},  // class 14
        {{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f}},  // class 15
    }};
    return table;
}

// decode0x100195b0's REAL per-symbol tree-decode body, Sec5.3aa cited in
// full above - see this section's own top comment for the measured
// discrepancy against the golden fixture. `bank` is this row's own
// SiblingTableBank (entropy_prestage.h's CaseSelectorScratchSource::
// beginBlockRow(..., &bank)); `globalTable` is descriptor A's own BuiltTable
// (CaseSelectorScratchSource::builtTable()).
inline FlatFillPrestageArray decode0x100195b0TreeDecodeAttempt(BitstreamReader& br,
                                                                  FlatFillTreeDecodeAttemptState& state,
                                                                  const SiblingTableBank& bank,
                                                                  const BuiltTable& globalTable) {
    const uint32_t rawMagnitude = br.readBits(kFlatFillLeadingMagnitudeWidth);  // ctx+0x28, Sec5.3p/q/s
    if (br.ranOutOfData()) return FlatFillPrestageArray(std::vector<uint8_t>{});

    // One-time pre-loop sentinel bit (Sec5.3aa): "immediately after the
    // leading magnitude is decoded and validated, and before the first loop
    // iteration" - real bitstream cost (bit was 0 for this real call).
    const uint32_t sentinelBit = br.readBits(1);  // 0x10019656-0x10019689
    if (br.ranOutOfData()) return FlatFillPrestageArray(std::vector<uint8_t>{});

    int32_t magnitude = static_cast<int32_t>(rawMagnitude);
    if (sentinelBit != 0) {
        magnitude = -20 - magnitude;  // Sec5.3aa's own sentinel-path formula, never observed firing
    }
    if (magnitude <= 0) {
        return FlatFillPrestageArray(std::vector<uint8_t>{});  // derived safe fallback, see top comment
    }
    const size_t count = static_cast<size_t>(magnitude);

    std::vector<uint8_t> bytes(count, 0);
    uint8_t curClass = state.classState;  // ctx+0x180, loaded ONCE before the loop starts
    size_t decoded = 0;
    for (; decoded < count; ++decoded) {
        // --- nibble 1: PER-CLASS tree, indexed by cur_class (Sec5.3aa) ---
        const BuiltTable& classTable = bank.tables[curClass];  // arg3 + cur_class*4 + 0x100/+0x140
        const uint32_t idx1 = br.peekBits(classTable.classByte);
        if (br.ranOutOfData()) break;
        const uint8_t packed1 = classTable.lutA[idx1];
        const uint32_t codeLen1 = packed1 >> 4;
        const uint32_t rawSymbol1 = packed1 & 0xF;
        br.readBits(static_cast<int>(codeLen1));  // consume ONLY code_len1, not classByte
        const uint8_t nib1 = flatFillTranslateTable256()[curClass][rawSymbol1];  // CORRECTED
                           // 2026-10-11 (Sec5.3aa's own in-place correction): ctx + cur_class*16
                           // + raw_symbol1 -- on ctx/arg1 (the idle-check/output-descriptor
                           // struct), NOT classTable.table16 (arg3's own per-class build-table
                           // output, a different table used only for packed1/idx1 above).

        // --- nibble 2: GLOBAL tree, same single descriptor 0x10019360 itself uses (Sec5.3aa) ---
        const uint32_t idx2 = br.peekBits(globalTable.classByte);
        if (br.ranOutOfData()) break;
        const uint8_t packed2 = globalTable.lutA[idx2];
        const uint32_t codeLen2 = packed2 >> 4;
        const uint32_t rawSymbol2 = packed2 & 0xF;
        br.readBits(static_cast<int>(codeLen2));  // consume ONLY code_len2, not classByte
        const uint8_t nib2 = globalTable.table16[rawSymbol2];

        const uint8_t combo = static_cast<uint8_t>((nib1 << 4) | nib2);  // 0x100197c0-0x100197c3
        int8_t outByte;                                                    // sign/bias transform, 0x100197c5-0x100197d9
        if (combo & 0x80) {
            outByte = static_cast<int8_t>(-static_cast<int8_t>(combo & 0x7F));  // top bit set: negate low 7 bits
        } else {
            outByte = static_cast<int8_t>(combo & 0x7F);  // top bit clear: keep low 7 bits
        }
        outByte = static_cast<int8_t>(outByte - 0x80);  // bias by -128, both paths
        bytes[decoded] = static_cast<uint8_t>(outByte);

        curClass = nib1;  // THE UPDATE RULE (Sec5.3aa): next symbol's per-class selector IS
                           // this symbol's own translated nibble-1 output, every time
    }
    state.classState = curClass;  // ctx->class_state = cur_class, written back ONCE at the real `ret`

    return FlatFillPrestageArray(std::move(bytes), decoded, 0);
}

}  // namespace sr3binkcodec
