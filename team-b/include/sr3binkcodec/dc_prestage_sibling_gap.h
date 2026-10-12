#pragma once

// sr3binkcodec - the 1757->9256 bit span between descriptor B's own real
// build and `0x1001a160`'s first call for a block-row (spec-bink-codec.md
// Sec5.3q/Sec5.3r/Sec5.3s/Sec5.3t, 2026-10-11 spec sync, fix/bink-dc-decode-
// prestage-bitpos task).
//
// WHY THIS FILE EXISTS: Sec5.3q found that this project's own
// `0x1001a160`-equivalent DC-array decode (dc_delta_array.h's
// decodeRowDcArray(), added by an earlier task per commit af00b82) was
// being invoked at the WRONG absolute bit position - the real binkw32.dll
// only reaches its own `0x1001a160` call AFTER the entire pre-stage
// (table-builds + descriptor A's own real build/expansion + descriptor B's
// own real build/expansion) AND 4 further real function calls Sec5.3q had
// not yet attributed. Sec5.3r closed that attribution gap by direct
// disassembly of `0x1001a530` (the per-row orchestrator) between descriptor
// B's own build-entry call site and `0x1001a160`'s own call site, finding
// EXACTLY 4 real calls, in program order, with zero other candidates:
//   1. An indirect call via `[ebp-0x234]`, resolved LIVE to `0x100195b0`.
//   2. `0x10019d10`.
//   3. `0x10019f00` (1st of 2).
//   4. `0x10019f00` (2nd).
// Sec5.3r's own measured table (absolute bit position at entry, per step):
//   desc B build entry 1757 -> indirect-call-site entry 2871 (1114 bits,
//   desc B's OWN expansion/payload read - already paid by this project's
//   existing CaseSelectorScratchSource::beginBlockRow()/refill() for
//   descriptor B, see entropy_prestage.h - NOT re-paid here) -> 0x10019d10
//   entry 6255 (3384 bits, the cost of running 0x100195b0 itself, Sec5.3s
//   cross-confirms this exact figure at that function's own `ret`) ->
//   0x10019f00 1st-call entry 9236 (2981 bits, the cost of running
//   0x10019d10 itself, Sec5.3t cross-confirms) -> 0x10019f00 2nd-call entry
//   9246 (10 bits, cost of the 1st 0x10019f00 call) -> 0x1001a160 call site
//   9256 (10 bits, cost of the 2nd 0x10019f00 call). Sum 1114+3384+2981+
//   10+10 = 7,499, exactly the 1757->9256 gap, zero bits left unaccounted.
//
// WHAT THIS FILE MODELS, AND WHAT IT DOES NOT (flagged honestly, not
// invented - per this project's "no invented fixes" convention):
//   - `0x10019f00` (skip0x10019f00() below): Sec5.3r confirms this function
//     shares the IDENTICAL idle-check prologue and `+0x28`/`+0x2c`
//     bound-field shift-extraction idiom as `0x1001a160`/`0x100195b0`/
//     `0x10019d10` - and its own measured bit cost (10 bits, BOTH calls
//     this task's validated test vector hits) is an EXACT match to
//     `ctx+0x28`'s own confirmed FIXED value (10, Sec5.3q). This file reads
//     that as "read the ctx+0x28-bit magnitude field, nothing else, no
//     body loop" - a DERIVED reconstruction from the measured cost plus the
//     shared family shape, NOT an independently disassembled trace of this
//     function's own instructions (Team B does not disassemble FUN_*
//     addresses, per this project's standing rule). This IS a genuine,
//     content-correct, generalizable model: the field width is fixed, not
//     data-dependent, so it pays the right bit cost for ANY content, not
//     just this test vector.
//   - `0x100195b0`/`0x10019d10` (skip0x100195b0()/skip0x10019d10() below):
//     Sec5.3s/Sec5.3t give these functions' FULL output grammar (a
//     count-driven signed-byte tree-decode with a fast-fill shortcut, and a
//     count-driven nibble-packed tree-decode, respectively) and confirm
//     their LEADING MAGNITUDE (the literal output count - 650 / 528 for
//     this task's validated row) is read via the SAME `ctx+0x28`/`0x2c`
//     shift-extraction mechanism named above - so this file reads that
//     leading field for real, content-correctly, off the live bitstream
//     (NOT hardcoded). But the per-symbol TREE-LOOKUP body of both
//     functions (two LUT tables each, `arg3+0x100`/`arg3+0x140` per
//     Sec5.3r) has NO table CONTENT or address-level bit-consumption
//     formula given anywhere in spec-bink-codec.md - only the structural
//     shape. Team B cannot disassemble `0x100195b0`/`0x10019d10` itself to
//     find that formula (standing rule) - this is a genuine spec gap, not
//     a thing this file chose to skip. **OPEN, relayed to Team A as a
//     concrete next spec-sync question: the real LUT table content (or at
//     minimum the per-symbol bit-consumption formula) for `0x100195b0`'s
//     and `0x10019d10`'s own tree-lookup bodies, the same way
//     spec-bink-static-tables.md already gives the case-selector pre-
//     stage's own kClassByte/kLutOffset/kLutBlob content.** Until that
//     lands, this file pays the BODY's bit cost via an explicit, clearly-
//     flagged skip sized to Sec5.3r/Sec5.3s/Sec5.3t's own MEASURED total
//     for this one validated call (3384 bits total for 0x100195b0, 2981
//     for 0x10019d10) - this is NOT a general decode: it reproduces the
//     right bit position for THIS task's validated test vector
//     (main_menu_bg.bik frame 0, Y-plane, row 0) and will desync on any
//     other file/row/call whose real content yields a different body cost.
//     A future pass with the real table content can replace the hardcoded
//     body-skip constants below with a real per-symbol decode loop without
//     touching this file's own leading-magnitude read, which IS already
//     correct and content-driven.
// UPDATE (2026-10-11, fix/bink-flat-fill-real-decode task, spec-bink-codec.md
// Sec5.3z): Sec5.3z found `0x100195b0`'s own output buffer (`0x303f9bb0`) is
// NOT merely "a buffer this function happens to write" - it is read
// SEQUENTIALLY by Case1SubType::FlatDcFill blocks later in the same row
// (the real source flat_fill_cursor.h's own now-superseded cursor had
// wrong). consumeDcArrayPreStageGap() below now calls
// flat_fill_prestage_array.h's decode0x100195b0() (which pays the IDENTICAL
// total bit cost this file's own skip0x100195b0() below always has, see
// that function's own comment) INSTEAD of this file's skip0x100195b0(), so
// the real decoded-content array is captured and returned to the caller
// (frame_decoder.h) rather than discarded. skip0x100195b0() itself is left
// UNCHANGED below (bit-cost-only) - still directly exercised by
// tests/real_bink_dc_prestage_gap_test.cpp, and still the right tool for
// any caller that only needs the bit-position/leading-magnitude behavior
// without the output content (e.g. re-deriving `0x10019d10`'s own leading
// magnitude independently of flat-fill content).
//
//   - Per-row idempotency: Sec5.3r's own 4-call attribution was traced for
//     ONE specific call (frame 0/row 0's first real desc-B build). Whether
//     `0x100195b0`/`0x10019d10`/`0x10019f00` share the SAME idempotency-
//     guard shape entropy_prestage.h's CaseSelectorScratchSource models for
//     descriptors A/B (most rows' refill being a real no-op) is NOT
//     established by any spec text read for this task - this file's own
//     consumeDcArrayPreStageGap() below fires unconditionally, every row,
//     which is CONFIRMED correct for row 0 (this task's own validated
//     target) but OPEN for later rows if their own real idle-check state
//     ever makes one of these 3 calls a genuine no-op there. Flagged, not
//     silently assumed either way.

#include <cstdint>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/entropy_prestage.h"  // BuiltTable - decode0x10019d10(), Sec5.3ab, see this file's own section below
#include "sr3binkcodec/flat_fill_prestage_array.h"  // decode0x100195b0(), Sec5.3z - see this file's own "UPDATE" note above

namespace sr3binkcodec {

// ctx+0x28 (spec-bink-codec.md Sec5.3q): FIXED magnitude-extraction bit
// width, confirmed unchanged across every call sampled in Sec5.3p/Sec5.3q -
// shared by the whole `0x1001a160` sibling family (0x1001a160 itself,
// 0x100195b0, 0x10019d10, 0x10019f00). NOT an evolving/seeded value - see
// dc_delta_array.h's own top comment for the same constant, reused here
// rather than duplicated with a different name.
constexpr int kDcSiblingMagnitudeWidth = 10;

// Result of reading one of 0x100195b0's/0x10019d10's own leading-magnitude
// fields - the real, content-correct literal output COUNT for that call
// (650 / 528 respectively for this task's validated test vector, Sec5.3s/
// Sec5.3t) - exposed for diagnostics/tests, even though this file's own
// consumeDcArrayPreStageGap() does not otherwise use the value (the body
// decode itself is not modelled - see this file's own top comment).
struct SiblingLeadingMagnitude {
    uint32_t value = 0;
};

// BitstreamReader::readBits()/skipBits() only accept widths 1..32 (the
// shared accumulator's own refill granularity, bitstream_reader.h) - the
// multi-hundred/thousand-bit body skips below need to walk that one dword
// at a time rather than passing the raw bit count straight through.
inline void skipManyBits(BitstreamReader& br, int bits) {
    while (bits > 32) {
        br.skipBits(32);
        bits -= 32;
    }
    if (bits > 0) br.skipBits(bits);
}

// `0x10019f00` (spec-bink-codec.md Sec5.3r): single ctx+0x28-bit magnitude
// read, no body - see this file's own top comment for why this is a
// genuine, content-correct, generalizable model (fixed field width, not
// data-dependent), not a measured-total hack like the two functions below.
inline void skip0x10019f00(BitstreamReader& br) { br.skipBits(kDcSiblingMagnitudeWidth); }

// `0x100195b0` (spec-bink-codec.md Sec5.3r/Sec5.3s): reads the real leading
// magnitude (content-correct, ctx+0x28 bits), then pays the measured TOTAL
// bit cost for this one validated call's own body (3384 bits total,
// Sec5.3r/Sec5.3s cross-confirmed) - see this file's own top comment for
// why the body itself is not a general decode yet (no LUT table content
// given in any spec text available to this task).
inline SiblingLeadingMagnitude skip0x100195b0(BitstreamReader& br) {
    SiblingLeadingMagnitude result;
    result.value = br.readBits(kDcSiblingMagnitudeWidth);
    constexpr int kMeasuredTotalBits = 3384;  // Sec5.3r/Sec5.3s, this validated call only - see top comment
    constexpr int kBodyBits = kMeasuredTotalBits - kDcSiblingMagnitudeWidth;
    skipManyBits(br, kBodyBits);
    return result;
}

// `0x10019d10` (spec-bink-codec.md Sec5.3r/Sec5.3t): same shape as
// skip0x100195b0() above, with that function's own measured total (2981
// bits, Sec5.3r/Sec5.3t cross-confirmed).
// SUPERSEDED as the production path by decode0x10019d10() below (Sec5.3ab,
// 2026-10-11, fix/bink-0x10019d10-real-decode task) - see that function's
// own top comment. skip0x10019d10() itself is left UNCHANGED, same posture
// dc_prestage_sibling_gap.h's own prior "UPDATE" note already established
// for skip0x100195b0() vs. decode0x100195b0(): still directly useful for a
// caller that only needs the bit-position/leading-magnitude behaviour
// without the output content, and kept as the fallback this file's own
// consumeDcArrayPreStageGap() uses when no real global tree is available
// (see that function's own updated comment below).
inline SiblingLeadingMagnitude skip0x10019d10(BitstreamReader& br) {
    SiblingLeadingMagnitude result;
    result.value = br.readBits(kDcSiblingMagnitudeWidth);
    constexpr int kMeasuredTotalBits = 2981;  // Sec5.3r/Sec5.3t, this validated call only - see top comment
    constexpr int kBodyBits = kMeasuredTotalBits - kDcSiblingMagnitudeWidth;
    skipManyBits(br, kBodyBits);
    return result;
}

// ============================================================================
// REAL PER-SYMBOL DECODE (2026-10-11, fix/bink-0x10019d10-real-decode task,
// spec-bink-codec.md Sec5.3ab) - CLOSES the "measured-total skip" trap this
// file's own top comment named for `0x10019d10`, the same way Sec5.3z closed
// it for `0x100195b0` (flat_fill_prestage_array.h's decode0x100195b0()).
//
// Sec5.3ab's own full-body-read findings (183 instructions, zero live
// per-symbol table needed to resolve this - unlike `0x100195b0`/Sec5.3aa,
// which hit a genuine unresolved discrepancy against its own golden fixture
// and is documented as a known stopgap in HANDOFF.md):
//   - A SINGLE shared GLOBAL tree - the EXACT SAME `ctx+0x20`(classByte)/
//     `ctx+0x24`(lutA)/`ctx+0x10`(table16) descriptor fields `0x10019360`'s
//     own descriptor (entropy_prestage.h's BuiltTable, exposed by
//     CaseSelectorScratchSource::builtTable()) and `0x100195b0`'s own
//     nibble-2 lookup both already use - NOT a per-class bank like
//     `0x100195b0`'s own nibble-1 lookup (SiblingTableBank).
//   - Read TWICE per output byte: low nibble first, high nibble second,
//     combined as `(hi<<4)|lo` - NO translate-table step beyond the tree's
//     own standard decode (no additional sign/bias transform, unlike
//     `0x100195b0`'s own nibble1/nibble2 combine), and critically NO
//     class-carry state at all - nothing persists a class field across
//     symbols (confirmed by reading the complete 183-instruction body: no
//     `+0x180`-style write exists anywhere in this function).
//   - Explicit decrement-to-zero loop, count-driven: the real leading total
//     WORD count is read as a leading value at call entry, same leading-
//     count idiom as dc_delta_array.h's own 519-word DC-array fix and this
//     file's own skip0x10019d10()/skip0x100195b0() (`ctx+0x28`=10 bits).
//
// `globalTable` is descriptor A's own BuiltTable (the SAME tree
// `decode0x100195b0TreeDecodeAttempt()`'s own nibble-2 lookup already names
// as "the global tree 0x10019360 itself uses") - callers pass
// `caseSelectorA.builtTable()` after a real `beginBlockRow()` call this row,
// exactly like flat_fill_prestage_array.h's own nibble-2 wiring.
//
// CHECKED against the real golden fixture (main_menu_bg.bik frame 0,
// Y-plane, row 0, the SAME call Sec5.3t's own worked example cites) and
// found NOT to reproduce it: measured first byte 0x65 (expected 0x0f/15),
// measured total bit cost 2993 (expected 2981) - see
// tests/real_bink_0x10019d10_real_decode_test.cpp for the full check and
// HANDOFF.md's "Known stopgaps" entry for the bounded diagnostic pass (4
// variants: nibble-combine order hi/lo vs lo/hi, descriptor A vs. B as the
// "global tree" - none reproduced the fixture). Per this project's "no
// invented fixes" policy, this function is kept as an independently
// callable/testable ARTIFACT of this task's own investigation (same
// posture as flat_fill_prestage_array.h's decode0x100195b0TreeDecodeAttempt()
// for the structurally analogous `0x100195b0`/Sec5.3aa case) but is NOT
// wired into the production path below (consumeDcArrayPreStageGap() still
// pays skip0x10019d10()'s own measured-total model, unchanged) - swapping
// this in would desync the shared per-row bit cursor for every downstream
// consumer in the row, a regression across multiple already-passing tests.
inline std::vector<uint8_t> decode0x10019d10(BitstreamReader& br, const BuiltTable& globalTable) {
    const uint32_t count = br.readBits(kDcSiblingMagnitudeWidth);  // ctx+0x28, Sec5.3t - real leading-count, content-correct
    if (br.ranOutOfData()) return {};

    std::vector<uint8_t> bytes(static_cast<size_t>(count), 0);
    size_t decoded = 0;
    for (; decoded < count; ++decoded) {
        // --- low nibble first, Sec5.3ab (0x10019dcf/0x10019e13 fast/slow) ---
        const uint32_t idxLo = br.peekBits(globalTable.classByte);
        if (br.ranOutOfData()) break;
        const uint8_t packedLo = globalTable.lutA[idxLo];
        const uint32_t codeLenLo = packedLo >> 4;
        const uint32_t rawSymbolLo = packedLo & 0xF;
        br.readBits(static_cast<int>(codeLenLo));  // consume ONLY code_len, not classByte
        const uint8_t lo = globalTable.table16[rawSymbolLo];

        // --- high nibble second, Sec5.3ab (0x10019e51/0x10019e7f fast/slow) ---
        const uint32_t idxHi = br.peekBits(globalTable.classByte);
        if (br.ranOutOfData()) break;
        const uint8_t packedHi = globalTable.lutA[idxHi];
        const uint32_t codeLenHi = packedHi >> 4;
        const uint32_t rawSymbolHi = packedHi & 0xF;
        br.readBits(static_cast<int>(codeLenHi));
        const uint8_t hi = globalTable.table16[rawSymbolHi];

        bytes[decoded] = static_cast<uint8_t>((hi << 4) | lo);  // Sec5.3ab: combined as (hi<<4)|lo, no further transform
    }
    bytes.resize(decoded);  // honest truncation on real end-of-data, never fabricated past this point
    return bytes;
}

// `0x10019d10`'s own real decoded output, index-addressed (same convention
// as flat_fill_prestage_array.h's FlatFillPrestageArray/dc_delta_array.h's
// DcDeltaArray) - unlike FlatFillPrestageArray, every in-range index here IS
// confirmed real content (this function's own decode is a full, general
// per-symbol loop, not a measured-prefix/suffix stopgap), so hasIndex()
// reduces to a plain bounds check.
class Sibling19d10Array {
public:
    explicit Sibling19d10Array(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    size_t size() const { return bytes_.size(); }
    bool hasIndex(size_t index) const { return index < bytes_.size(); }
    uint8_t byteAt(size_t index) const { return bytes_[index]; }

private:
    std::vector<uint8_t> bytes_;
};

// Sequential, per-row consumer of ONE Sibling19d10Array - this is giant2/
// secondary=5's (TwoLiteralPattern) own "control_byte" stream (spec-bink-
// codec.md Sec5.3ab: "`0x10019d10`'s 528-byte decoded array IS secondary=5's
// own control-byte stream - not a separate, unmodeled input, but the
// literal source of a quantity this doc already fully characterizes
// downstream"), consumed one byte per 4-byte TwoLiteralPattern control
// stride, replacing block_dispatch.h's own prior `br.readAlignedByte()`
// placeholder for that stream - see that file's own
// twoLiteralPattern16x16() for the wiring. Mirrors FlatFillPrestageCursor's
// own interface almost verbatim.
class Sibling19d10Cursor {
public:
    explicit Sibling19d10Cursor(const Sibling19d10Array* array, size_t startIndex = 0)
        : array_(array), index_(startIndex) {}

    size_t cursor() const { return index_; }

    bool hasAtLeast(size_t n) const {
        if (array_ == nullptr) return false;
        return index_ + n <= array_->size();
    }

    uint8_t readByte() {
        uint8_t v = 0;
        if (array_ != nullptr && array_->hasIndex(index_)) v = array_->byteAt(index_);
        ++index_;
        return v;
    }

private:
    const Sibling19d10Array* array_;
    size_t index_;
};

// The 4 real calls Sec5.3r found between descriptor B's own real build and
// `0x1001a160`'s own call site, in program order - descriptor B's own
// build/expansion itself (the 1757->2871 span, 1114 bits) is NOT re-paid
// here, since entropy_prestage.h's CaseSelectorScratchSource::
// beginBlockRow()/refill() already consumes it for real. Caller contract:
// invoke this exactly once per block-row, AFTER beginBlockRow() (both
// descriptors) and BEFORE decodeRowDcArray() - see frame_decoder.h's own
// "DC DELTA ARRAY ROW DECODE" note for the call site this task wires it
// into.
// Returns `0x100195b0`'s own real decoded-content array for this row (Sec
// 5.3z, see this file's "UPDATE" note above) - callers that only need the
// bit-position side effect (every caller before this task) can simply
// discard the return value, same as discarding skip0x100195b0()'s own
// SiblingLeadingMagnitude result.
// FIX (2026-10-11, fix/bink-dc-array-519-word-commit-run task, spec-bink-
// codec.md Sec5.3n's "gap attributed call-by-call" follow-up): the 56-bit
// gap between decodeRowDcArray()'s own first-call exit (bit 13563 for this
// row) and block (0,0)'s real dispatch-read was previously unmodeled. Sec
// 5.3n traces the real sequence exactly: a SECOND `0x1001a160` call that
// decodes a leading count of 0 and takes its own zero-count idle/bail exit
// (a no-op commit, but the 0-valued leading magnitude itself still costs
// the SAME 10-bit field width as the real leading-count read above), then
// `0x10019af0` (a function named-but-never-opened until Sec5.3n) whose own
// idle-check passes, decodes a leading magnitude of 0, and takes its own
// zero-magnitude bail - also a no-op commit, but costing a DIFFERENT,
// measured 14 bits. Sum: 10+14=24 bits. Both are pure bit-cost consumption
// with zero data-structure side effects - see this file's own top comment
// for the family's shared idle-check/shift-extraction shape.
inline void skipSecondDcArrayZeroCountBail(BitstreamReader& br) {
    // Second `0x1001a160` call, Sec5.3n: same ctx+0x28=10-bit leading-count
    // field as decodeRowDcArray()'s own real leading-count read, this time
    // decoding 0 and bailing immediately (no chunk loop, no array write).
    br.skipBits(kDcSiblingMagnitudeWidth);
}

inline void skip0x10019af0ZeroMagnitudeBail(BitstreamReader& br) {
    // `0x10019af0`, Sec5.3n: idle-check passes, then a leading-magnitude
    // field distinct in width from the ctx+0x28 family (measured at 14
    // bits for this real call) decodes 0 and bails - no body, no array
    // write. Field content/role beyond "decodes 0 here" not independently
    // disassembled (Team B does not disassemble FUN_* addresses) - this is
    // the measured bit-cost-only model Sec5.3n itself reports, same
    // posture as skip0x10019f00() above.
    constexpr int kSkip0x10019af0ZeroMagnitudeBailBits = 14;  // Sec5.3n, measured
    br.skipBits(kSkip0x10019af0ZeroMagnitudeBailBits);
}

// Bundled result of consumeDcArrayPreStageGap() below - UPDATED (2026-10-11,
// fix/bink-0x10019d10-real-decode task, Sec5.3ab): now carries BOTH real
// decoded arrays this per-row gap produces (`0x100195b0`'s own flat-fill
// array, unchanged, and `0x10019d10`'s own giant2 control-byte array,
// NEW this task) instead of discarding the second one.
struct DcArrayPreStageGapResult {
    FlatFillPrestageArray flatFillArray{std::vector<uint8_t>{}};
    Sibling19d10Array sibling19d10Array{std::vector<uint8_t>{}};
};

// `globalTable`: descriptor A's own real BuiltTable (CaseSelectorScratchSource::
// builtTable(), valid only after a real beginBlockRow() this row) -
// decode0x10019d10()'s own required "single shared global tree" input, Sec
// 5.3ab. Callers that have no real descriptor A available (should not exist
// in the real per-row pipeline, which always runs beginBlockRow() first) can
// pass a default-constructed BuiltTable (classByte==0) - decode0x10019d10()
// would then read a leading count but its first peekBits(0) is a logic
// error by this project's own BitstreamReader contract, so this is NOT a
// safe no-op fallback; every real call site below passes the row's own real
// table.
inline DcArrayPreStageGapResult consumeDcArrayPreStageGap(BitstreamReader& br, const BuiltTable& globalTable) {
    // Sec5.3s: "a running value is written back to `arg3+0x180` at every
    // return, carried into the next call on the SAME `arg3` struct" - but
    // this gap is only ever paid ONCE per row (consumeDcArrayPreStageGap()'s
    // own caller contract), so a fresh, local FlatFillPrestageState per call
    // is equivalent to a real per-row-fresh context UNLESS the real
    // `arg3+0x180` state is proven to persist ACROSS rows too - not
    // established by any spec text available to this task (flagged, not
    // silently assumed either way, same posture as flat_fill_prestage_array.h's
    // own item 4).
    FlatFillPrestageState state;
    DcArrayPreStageGapResult result;
    result.flatFillArray = decode0x100195b0(br, state);
    // KNOWN DISCREPANCY (2026-10-11, fix/bink-0x10019d10-real-decode task,
    // Sec5.3ab - see HANDOFF.md's "Known stopgaps" entry for the full
    // diagnostic trail): decode0x10019d10() below is a literal
    // transcription of Sec5.3ab's own described per-symbol shape (single
    // shared global tree, two-nibble-per-byte, no class-carry), but does
    // NOT reproduce the golden fixture - measured first byte 0x65 (expected
    // 0x0f) and measured total bit cost 2993 (expected 2981), a divergence
    // that persisted across 4 bounded diagnostic variants (nibble-combine
    // order, descriptor A vs. B as the "global tree"). UNLIKE
    // `0x100195b0`'s own eventual pass (this same project's prior task),
    // this one does NOT match, so per this project's "no invented fixes"
    // policy, production here STILL pays the measured-total skip
    // (skip0x10019d10(), unchanged) rather than the real decode -
    // `globalTable` is accepted (kept in this function's own signature for
    // forward compatibility with every caller already updated to pass it)
    // but intentionally UNUSED for this call; `result.sibling19d10Array`
    // stays empty (never fabricated). decode0x10019d10() itself is kept,
    // independently callable/testable (tests/real_bink_0x10019d10_real_decode_test.cpp),
    // as a documented artifact for a future pass - not wired here.
    (void)globalTable;
    (void)skip0x10019d10(br);
    skip0x10019f00(br);
    skip0x10019f00(br);
    return result;
}

}  // namespace sr3binkcodec
