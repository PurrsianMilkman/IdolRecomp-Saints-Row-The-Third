#pragma once

// PUBLIC-BUILD SPLIT (see include/sr3binkcodec/entropy_prestage_tables.h and
// include/sr3binkcodec/entropy_prestage_runtime.h): this file is ALGORITHM
// ONLY (dispatch logic, bitstream reading, structure) - the real Bink
// case-selector lookup-table DATA it reads (kRunLength/kClassByte/
// kLutOffset/kLutBlob) lives in mutable storage declared by
// entropy_prestage_runtime.h and defined in src/bink_entropy_tables.cpp,
// which populates it from either the compiled-in dev data (when present -
// the public source export's "exclude the game/middleware DATA" decision
// excludes it) or sr3runtimedata's runtime loader, reading + verifying it
// directly from the player's own real game install - see that header's own
// top comment. caseSelectorTablesAvailable()/CaseSelectorStatus::
// TablesNotLoaded (below) let callers detect and report the "neither source
// has populated this yet" case instead of silently decoding against a
// zeroed placeholder - same "absent data degrades gracefully, never fails
// to link/crashes" principle as sr3audioruntime's wwise codebook library
// (see include/sr3audioruntime/wwise_codebook_library.h).
//
// sr3binkcodec - the case-selector entropy pre-stage, FULL instruction-level
// algorithm (spec-bink-codec.md Sec5.3g, 2026-10-08 Team A delivery -
// "closes all 4 of Team B's named blockers": the main expansion loop, the
// table-build mechanics, call granularity, and the real starting bit
// position). Supersedes this file's own prior state (fix/bink-entropy-
// prestage task), which modelled only the two CONFIRMED reads (the LEN
// field's bit-width, the table-selector nibble) before reporting
// CaseSelectorStatus::PreStageOpen - that stop is now CLOSED for this
// descriptor's own case-selector byte stream; see "WHAT REMAINS OPEN" below
// for the handful of named sub-questions Sec5.3g itself still flags.
//
// THE FULL ALGORITHM (spec-bink-codec.md Sec5.3g, cited inline below):
//
//   1. `0x10019360` (entropy_prestage()/refill() below): per block-row, per
//      descriptor, idempotency-guarded (only does real work once the
//      PREVIOUS scratch range is fully drained). Reads an N-bit LEN field
//      (N = caseSelectorLengthFieldBitWidth(), unchanged from the prior
//      pass), range-checks it, then reads ONE whole-call mode bit selecting
//      between a flat memset-fill path (reads a fresh 4-bit fill value) and
//      a table-driven canonical-Huffman expansion loop. That loop peeks
//      `classByte` bits, looks up a packed `code_len<<4|raw_symbol` byte in
//      `lutA`, consumes only `code_len` bits, translates `raw_symbol`
//      through `table16` to get `bl`, then either emits `bl` as a literal
//      (bl < 0xC) or replicates the last literal `kRunLength[bl-0xC]` times
//      (bl in 0xC..0xF).
//   2. `0x10018ca0` (buildCaseSelectorTable() below): the table-build
//      dispatcher - reads a 4-bit nibble selecting `classByte`/`lutA` from
//      the static tables (spec-bink-static-tables.md, below), then builds
//      `table16` via one of 5 strategies: identity (nibble 0), a 4-level
//      bit-reversal merge network (0-3 levels deep, selected by a 2-bit
//      `mode` after a 1-bit "use the merge network at all" flag), or
//      `build_explicit_prefix` (buildExplicitPrefixTable() below, reached
//      INSTEAD of the merge network when that 1-bit flag is 1).
//   3. `0x10018c20` (mergeBitSelected() below): the 2-stream bit-selected
//      merge primitive the merge network calls at each of its 3 inner
//      levels.
//   4. Call granularity (Sec5.3g, empirically confirmed via live Unicorn
//      execution): `0x10019360` fires exactly once per 8-pixel block-row,
//      per descriptor - but `0x10018ca0` fires 23 TIMES per block-row
//      BEFORE either descriptor's own `0x10019360` call (2 direct + a
//      16-call loop + 1 more + 4 more direct calls, all sharing the SAME
//      sequential bit cursor) - building tables for 22 other, sibling
//      per-row sub-streams this project does not consume (OPEN, not
//      guessed at here), PLUS (confirmed by exact address cross-reference,
//      Sec5.3g) THIS descriptor's own table16/classByte/lutA, which is
//      always call #1 of those 23. CaseSelectorScratchSource::
//      beginBlockRow() below models this: it is the caller's
//      responsibility (frame_decoder.h) to invoke it exactly once per
//      8-pixel block-row, before any next() call for that row's blocks -
//      see frame_decoder.h's own "ENTROPY PRE-STAGE ROW SEQUENCING" note.
//   5. Real starting bit position (Sec5.3g): NOT a fixed protocol constant
//      - it is whatever bit position naturally falls out of correctly
//      executing the 23 build-table calls (and, before those, nothing else
//      this file models) from bit 0 of a plane's own sub-chunk data (past
//      its own 4-byte length prefix, Sec5.3c). This file does not hardcode
//      or target any specific bit offset; it is a CONSEQUENCE of correct
//      execution against real content, not an input to it. (Sec5.3g's own
//      measured values - Y=588, chromaA=262, chromaB=701 bits - are the
//      independently-reported RESULT of this same algorithm against the
//      real file, not a target this file aims for directly.)
//
// STATIC TABLES (spec-bink-static-tables.md, already in this project's
// committed history - independently re-verified by this task against that
// file's own SHA-256 for each region before use, same convention as that
// file's own "Provenance" section asks for):
//   - `kRunLength` = RUNLEN[0xC..0xF] (0x1003bd54-57 == region_bd40 bytes
//     20-23): {4, 8, 12, 32}.
//   - `kClassByte` = CLASSBYTE[nibble] (0x1003bd44 == region_bd40 bytes
//     4-19): max Huffman code length ("A") per 0x10018ca0 selector nibble.
//     CONFIRMED (Sec5.3g) by exact address cross-reference to be the SAME
//     field as desc+0x20 ("classbyte == A... not naming inference").
//   - `kLutOffset` = OFFSET[nibble] (0x1003c140 == region_c140, 16 LE
//     dwords): byte offset into kLutBlob where nibble's own
//     2^kClassByte[nibble]-entry packed LUT begins. This task independently
//     RE-DERIVED these 16 offsets as the running sum of 2^kClassByte[i] for
//     i<nibble and confirmed they match region_c140's own dumped dwords
//     exactly (0, 16, 48, 80, 112, 144, 176, 208, 272, 336, 400, 464, 528,
//     592, 720, 848) before trusting either source alone.
//   - `kLutBlob` = region_bd60 (0x1003bd60, 1104 bytes): the shared
//     variable-record blob kLutOffset indexes into. Embedded here in full
//     (not sliced down to "only the 976 bytes any nibble 0-15 actually
//     addresses") for one-to-one fidelity with the spec's own SHA-256'd
//     region - the trailing 128 bytes (offset 976-1103) are simply never
//     read by caseSelectorLutFor(), same "preserved verbatim, role not
//     pinned down" treatment spec-bink-static-tables.md's own region_bd40
//     note already gives its own trailing 72 bytes.
//
// WHAT REMAINS OPEN (named precisely, not silently assumed either way):
//   - desc->CAP (the scratch-buffer capacity LEN is range-checked against,
//     desc+0x2c): Sec5.3g names this FIELD ("+0x2c = the LEN capacity") but
//     never gives its numeric VALUE anywhere in spec-bink-codec.md. This
//     file therefore only range-checks "LEN == 0" (the half of "LEN == 0 ||
//     LEN > desc->CAP" that IS fully specified) - a real LEN that is 0 is
//     correctly poisoned (see below), but a real LEN that is merely "too
//     large for the real capacity but not 0" would NOT be caught here. Not
//     reachable by the one real test vector this file is validated against
//     (LEN=350, far below any plausible capacity) - flagged rather than
//     papered over with an invented constant.
//   - The POISON path's own downstream meaning for descriptor B / for
//     chromaA/chromaB (Sec5.3g's own "bonus" finding: chromaB's real
//     decoded LEN evaluates to 0/over-capacity from its 2nd call onward,
//     root cause still genuinely OPEN in the spec itself). This file
//     faithfully MODELS the poison mechanism itself (CaseSelectorStatus::
//     Poisoned below, matching 0x10019586-0x1001958e exactly) since that
//     mechanism IS fully confirmed - it does NOT attempt to explain or fix
//     WHY chromaB's content reaches it, per this task's own explicit scope
//     boundary (a separate, still-open issue - see frame_decoder.h, which
//     does not yet call this for chromaA/chromaB at all).
//   - Descriptor B (the second sibling output stream at the real DLL's
//     `[ebp-0x1a4]`, "built/expanded identically" per Sec5.3g): WHICH of
//     the 23 per-row 0x10018ca0 calls feeds ITS OWN table16/classByte/lutA,
//     and who downstream CONSUMES its byte stream, are both still OPEN in
//     Sec5.3g itself (no address cross-reference given for B, unlike A's
//     confirmed "call #1"). CaseSelectorScratchSource below models
//     descriptor A only - the one the real case_selector_scratch_frame0.bin
//     test vector captures and the one block_dispatch.h's primary dispatch
//     byte has always been sourced from (Sec5.3c). A second instance of
//     this same class COULD model B mechanically, but WHERE in the 23-call
//     sequence to snapshot its table is not something this file guesses.
//   - The 22 non-A/non-B sibling 0x10018ca0 calls' own downstream consumers
//     (motion-vector-adjacent and/or other alphabets, Sec5.3g) - not
//     consumed by sr3binkcodec at all today; their bit COST is correctly
//     paid (beginBlockRow() executes all 23 calls for real), their RESULTS
//     are correctly discarded (OPEN, not invented).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/entropy_prestage_runtime.h"

namespace sr3binkcodec {

// Number of bits needed to represent `x` (0 -> 0; matches the standard
// "highest set bit position + 1" definition spec-bink-codec.md Sec5.3c's
// own "N = bit_length(Width/8 + 0x1ff)" formula uses literally).
inline int bitLength(uint32_t x) {
    int n = 0;
    while (x != 0) {
        ++n;
        x >>= 1;
    }
    return n;
}

// spec-bink-codec.md Sec5.3c, CONFIRMED (re-checked, unchanged, by Sec5.3g):
// "a bit-packed length... at a bit-width computed from the row's own
// geometry (N = bit_length(Width/8 + 0x1ff) - for a 1920-wide frame, a
// concrete, re-checkable 10-bit field)." bitLength(1920/8 + 0x1ff) ==
// bitLength(751) == 10, matching that worked example exactly.
inline int caseSelectorLengthFieldBitWidth(int lumaWidthPixels) {
    return bitLength(static_cast<uint32_t>(lumaWidthPixels) / 8 + 0x1ff);
}

// `0x10018ca0`'s own CONFIRMED trivial fast path (spec-bink-codec.md
// Sec5.3c/Sec5.3g): selector nibble 0 "fills a 16-byte local with the
// literal identity sequence 0..0xF."
inline std::array<uint8_t, 16> caseSelectorIdentityTable() {
    std::array<uint8_t, 16> table{};
    for (int i = 0; i < 16; ++i) {
        table[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    }
    return table;
}

// One buildCaseSelectorTable() call's worth of output - the three fields
// spec-bink-codec.md Sec5.3g names as desc+0x10 (table16), desc+0x20
// (classByte, "A"), desc+0x24 (lutA, the 2^A-entry packed LUT pointer).
struct BuiltTable {
    std::array<uint8_t, 16> table16{};
    uint8_t classByte = 0;
    const uint8_t* lutA = nullptr;
};

// `0x10018c20` (spec-bink-codec.md Sec5.3g): the bit-selected 2-stream
// merge primitive the table-build merge network calls at each inner level.
// "merges N bytes from Q and N bytes from P into OUT, one bit of V per
// output item (LSB-first via shr), 1=take from P, 0=take from Q; once
// either stream exhausts, the rest of the OTHER stream is appended verbatim
// without consuming more of V." Returns bits of V actually consumed - EVERY
// caller below (buildCaseSelectorTable()'s 3 merge-network call sites) now
// uses this return value to advance the real bitstream cursor by exactly
// this many bits (via BitstreamReader::skipBits()) rather than by the
// nominal field width that was peeked - see those call sites' own comments
// and spec-bink-codec.md Sec5.3g's "genuine, previously-under-specified
// subtlety" for why the nominal width is only a maximum, not the real cost.
inline uint32_t mergeBitSelected(uint32_t v, const uint8_t* q, const uint8_t* p, uint8_t* out, uint32_t n) {
    uint32_t pLeft = n, qLeft = n, bitsConsumed = 0;
    while (pLeft != 0 && qLeft != 0) {
        ++bitsConsumed;
        if (v & 1u) {
            *out++ = *p++;
            --pLeft;
        } else {
            *out++ = *q++;
            --qLeft;
        }
        v >>= 1;
    }
    while (qLeft != 0) {  // 0x10018c60 drain
        *out++ = *q++;
        --qLeft;
    }
    while (pLeft != 0) {  // 0x10018c80 drain
        *out++ = *p++;
        --pLeft;
    }
    return bitsConsumed;
}

// `build_explicit_prefix` (0x1001922e-0x100192dd, spec-bink-codec.md
// Sec5.3g), construction strategy 5: an explicit prefix (1-8 positions,
// each an arbitrary 4-bit symbol value), then the unused symbol values
// filled in ascending order. The outer `while (true)` loop is bounded here
// at 64 iterations (a DERIVED safety net, not part of the real
// instructions) purely to turn a theoretical malformed-content infinite
// loop (every usedMask bit already consumed, e.g. from a duplicate prefix
// value) into a clean stop instead of a hang - never reached for any
// legitimately-encoded content, where the (shift+1) prefix values are
// always distinct by construction.
inline void buildExplicitPrefixTable(BitstreamReader& br, std::array<uint8_t, 16>& table16) {
    const uint32_t shift = br.readBits(3);  // 0x1001922e-0x1001925e
    uint16_t usedMask = 0xFFFF;
    uint32_t pos = 0;
    for (; pos <= shift; ++pos) {  // runs (shift+1) times, 1..8
        const uint32_t value = br.readBits(4);
        table16[pos] = static_cast<uint8_t>(value);  // 0x100192b8: sequential position i
        usedMask = static_cast<uint16_t>(usedMask & ~(1u << value));
    }
    uint32_t candidate = 0;  // 0x100192c3-0x100192db
    for (int guard = 0; guard < 64 && pos <= 0xF; ++guard) {
        if (usedMask & 1u) {
            table16[pos++] = static_cast<uint8_t>(candidate);
        }
        ++candidate;
        usedMask = static_cast<uint16_t>(usedMask >> 1);
    }
}

// `0x10018ca0` (spec-bink-codec.md Sec5.3g), the table-build dispatcher: 5
// distinct construction strategies, selected by a 4-bit nibble plus a 1-bit
// flag plus a 2-bit mode. Always consumes the 4-bit nibble (and sets
// classByte/lutA from the static tables above) even on the nibble==0
// trivial path, matching the real function's own unconditional
// `*LUT_A_ptr_out = ...; *classbyte_out = ...;` before its early return.
inline BuiltTable buildCaseSelectorTable(BitstreamReader& br) {
    BuiltTable result;
    const uint32_t nibble = br.readBits(4);  // 0x10018ccd-0x10018cfa
    result.classByte = kClassByte[nibble];
    result.lutA = &kLutBlob[kLutOffset[nibble]];

    if (nibble == 0) {  // 0x10018d1b: trivial identity
        result.table16 = caseSelectorIdentityTable();
        return result;
    }
    const uint32_t flag = br.readBits(1);  // 0x10018d30-0x10018d4b
    if (flag == 1) {
        buildExplicitPrefixTable(br, result.table16);  // strategy 5, 0x1001922e
        return result;
    }
    const uint32_t mode = br.readBits(2);  // 0x10018d51-0x10018d86
    std::array<uint8_t, 16> temp{};
    uint8_t* const x = (mode == 0) ? result.table16.data() : temp.data();
    for (int i = 0; i < 16; i += 2) {  // 0x10018d88-0x10018de5: ONE bit per adjacent pair
        const uint32_t bit = br.readBits(1);
        if (bit == 0) {
            x[i] = static_cast<uint8_t>(i);
            x[i + 1] = static_cast<uint8_t>(i + 1);
        } else {
            x[i] = static_cast<uint8_t>(i + 1);
            x[i + 1] = static_cast<uint8_t>(i);
        }
    }
    if (mode == 0) return result;

    // spec-bink-codec.md Sec5.3g's merge-network subtlety: each of the 3
    // selector fields below (3/7/15 bits) is only a MAXIMUM peek width -
    // mergeBitSelected() (0x10018c20) stops consuming V as soon as either
    // input side (P or Q) exhausts, and returns the ACTUAL number of bits it
    // drew. The real DLL's bit cursor advances by exactly that returned
    // count, not by the nominal field width - the two full-depth
    // (mode==3) merge calls in Sec5.3g's own 23-call trace (#6/#7) consumed
    // 7+5 bits at level 2, not the nominal 7+7=14 and not a naive 4+4=8; an
    // "always consume the nominal width" reimplementation over-reads by the
    // shortfall every time a merge exhausts early, which desyncs every bit
    // read afterward (Sec5.3g traces this exact bug to the observed
    // 580-vs-588 bit offset and the resulting LEN==0 instead of LEN==528).
    // So: peekBits() to get the selector value without committing it, feed
    // it to mergeBitSelected() and capture its real bitsConsumed return
    // value, then skipBits() by exactly that many bits.
    uint8_t level1[4][4];
    for (int g = 0; g < 4; ++g) {  // 0x10018e39/e84/ed2/f20, each a 3-bit MAXIMUM peek per group
        const uint32_t v3 = br.peekBits(3);
        uint8_t* dst = (mode == 1) ? &result.table16[static_cast<size_t>(4 * g)] : level1[g];
        const uint32_t consumed = mergeBitSelected(v3, &temp[static_cast<size_t>(4 * g)], &temp[static_cast<size_t>(4 * g + 2)], dst, 2);
        br.skipBits(static_cast<int>(consumed));  // advance by the REAL consumed count (Sec5.3g), not nominal 3
    }
    if (mode == 1) return result;

    uint8_t level2[2][8];
    const uint32_t v7a = br.peekBits(7);
    const uint32_t consumedA = mergeBitSelected(v7a, level1[0], level1[1], (mode == 2) ? &result.table16[0] : level2[0], 4);  // 0x100190d1
    br.skipBits(static_cast<int>(consumedA));  // advance by the REAL consumed count (Sec5.3g), not nominal 7
    const uint32_t v7b = br.peekBits(7);
    const uint32_t consumedB = mergeBitSelected(v7b, level1[2], level1[3], (mode == 2) ? &result.table16[8] : level2[1], 4);  // 0x1001911f
    br.skipBits(static_cast<int>(consumedB));  // advance by the REAL consumed count (Sec5.3g), not nominal 7
    if (mode == 2) return result;

    const uint32_t v15 = br.peekBits(15);  // 0x7fff-masked, confirmed 15 bits MAXIMUM peek
    const uint32_t consumed15 = mergeBitSelected(v15, level2[0], level2[1], result.table16.data(), 8);  // 0x10019205, mov edx,8 before call
    br.skipBits(static_cast<int>(consumed15));  // advance by the REAL consumed count (Sec5.3g), not nominal 15
    return result;
}

// Number of 0x10018ca0 calls `0x1001a530` makes per block-row BEFORE
// calling 0x10019360 for either sibling descriptor (spec-bink-codec.md
// Sec5.3g): "2 direct + 1 call to 0x10019300's own 16-loop + 1 more + 4
// more direct calls" = 2 + 16 + 1 + 4. Call #1 of these 23 is CONFIRMED (by
// exact address cross-reference, Sec5.3g) to be this descriptor's (A's) own
// table build; the other 22 build tables for sibling per-row sub-streams
// this project does not consume.
constexpr int kSiblingTableBuildsPerBlockRow = 23;

// Outcome of asking CaseSelectorScratchSource for the next dispatch byte,
// or of its own beginBlockRow() call.
enum class CaseSelectorStatus : uint8_t {
    Ok,               // outByte was filled with a real decoded dispatch byte (next()), or
                      // beginBlockRow() completed its row-start bookkeeping normally
    SourceExhausted,  // ran off the end of the real bitstream while reading (either during
                      // the 23 per-row table-builds, or during a LEN/mode-bit read that is
                      // NOT one of the two paths Sec5.3g names as sharing the POISON target)
    PreStageOpen,     // RETAINED FOR ENUM STABILITY ONLY - no longer produced by a
                      // correctly-functioning refill()/beginBlockRow() as of Sec5.3g's full
                      // algorithm (see this file's own top comment, "WHAT REMAINS OPEN", for
                      // the handful of named sub-gaps that remain - none of them are "the
                      // algorithm itself is unknown" any more, so none of them produce this
                      // value). Earlier callers that still switch on it keep compiling.
    Poisoned,         // 0x10019586-0x1001958e (spec-bink-codec.md Sec5.3g): decoded LEN==0 (or,
                      // inside the main expansion loop, hit the real bitstream's own end) -
                      // desc->cursor/bound are set to the CONFIRMED permanent "-4" stall state,
                      // so every future call for this SAME descriptor becomes a no-op forever
                      // (the idempotency guard never sees cursor==bound again). This is a REAL,
                      // CONFIRMED decode outcome, not a bug in this file - see this file's own
                      // top comment for why chromaB's own real content reaching this path is
                      // explicitly NOT something this file attempts to explain or fix.
    TablesNotLoaded,  // NOT a real decode outcome - this build was compiled without the public-
                      // build-excluded case-selector static lookup tables (entropy_prestage_
                      // tables.h, see this file's own top comment and caseSelectorTablesAvailable()
                      // above). beginBlockRow() reports this immediately, before building any
                      // table from the zeroed placeholder, so a caller never silently decodes
                      // garbage. A later runtime-loader task makes this unreachable once the
                      // real data is loaded from the player's own game install.
};

// Models one `0x1001a530`-call's worth of case-selector dispatch bytes for
// ONE descriptor (A - see this file's own top comment for why descriptor B
// is not modelled here), refilling from the entropy pre-stage (`0x10019360`)
// on demand, with its own table (`0x10018ca0`) rebuilt once per block-row.
class CaseSelectorScratchSource {
public:
    CaseSelectorScratchSource(BitstreamReader& br, int lumaWidthPixels)
        : br_(br), lumaWidthPixels_(lumaWidthPixels) {}

    // Must be called exactly once per 8-pixel block-row, BEFORE any next()
    // call for that row's blocks (spec-bink-codec.md Sec5.3g: "before ever
    // calling 0x10019360 for a row, 0x1001a530 executes a fixed sequence of
    // 23 calls to 0x10018ca0... all 23 share the identical sequential
    // bit-reader cursor the case-selector pre-stage itself reads from").
    // Runs all 23 calls for real (paying their actual, content-dependent
    // bit cost against the SAME shared `br` this source refills from),
    // keeping only call #1's result (this descriptor's own fresh
    // table16/classByte/lutA - CONFIRMED by address cross-reference, see
    // this file's own top comment) and discarding the other 22's. THEN
    // performs 0x10019360's own idempotency-guarded refill using that
    // freshly-built table, exactly as the real per-row call order does.
    CaseSelectorStatus beginBlockRow() {
        if (!caseSelectorTablesAvailable()) return CaseSelectorStatus::TablesNotLoaded;  // see this
                                                                                           // file's own top comment
        if (poisoned_) return CaseSelectorStatus::Poisoned;  // permanent, see refill()
        BuiltTable fresh;
        for (int i = 0; i < kSiblingTableBuildsPerBlockRow; ++i) {
            BuiltTable built = buildCaseSelectorTable(br_);
            if (br_.ranOutOfData()) return CaseSelectorStatus::SourceExhausted;
            if (i == 0) fresh = built;  // CONFIRMED: call #1 feeds this descriptor
        }
        builtTable_ = fresh;
        haveTable_ = true;
        if (pos_ < buffer_.size()) return CaseSelectorStatus::Ok;  // idempotency guard: prior
                                                                     // range not yet drained
        return refill();
    }

    // Returns the next case-selector dispatch byte (spec-bink-codec.md
    // Sec5.3c: "already one full byte per block-type dispatch, NOT a 4-bit
    // field"). Refills via refill() when the currently-buffered scratch
    // range is exhausted - a defensive fallback only; a correct caller
    // always calls beginBlockRow() first for every row, so this should
    // normally find pos_ < buffer_.size() already.
    CaseSelectorStatus next(uint8_t& outByte) {
        if (poisoned_) return CaseSelectorStatus::Poisoned;
        if (pos_ >= buffer_.size()) {
            if (!haveTable_) {
                // beginBlockRow() was never called for this instance - no real
                // table16/classByte/lutA exists yet to decode a table-driven
                // refill with (classByte==0 would make refill()'s own
                // peekBits(0) throw). A misuse of this class's own contract,
                // not a real decode outcome - reported the same way as a
                // genuinely exhausted source rather than crashing.
                return CaseSelectorStatus::SourceExhausted;
            }
            const CaseSelectorStatus st = refill();
            if (st != CaseSelectorStatus::Ok) return st;
        }
        outByte = buffer_[pos_++];
        return CaseSelectorStatus::Ok;
    }

private:
    // One real `0x10019360` call (spec-bink-codec.md Sec5.3g's own
    // pseudocode, reproduced 1:1): LEN field, idempotency-irrelevant here
    // (the caller - beginBlockRow()/next() - only calls this once the
    // guard has already confirmed cursor==bound), whole-call mode bit,
    // then either the flat memset-fill path or the table-driven
    // canonical-Huffman expansion using `builtTable_` (this row's own
    // freshly-built table, set by beginBlockRow() immediately before this
    // call).
    CaseSelectorStatus refill() {
        const int lengthFieldBits = caseSelectorLengthFieldBitWidth(lumaWidthPixels_);
        const uint32_t len = br_.readBits(lengthFieldBits);  // 0x10019382-0x100193d1
        if (br_.ranOutOfData()) return CaseSelectorStatus::SourceExhausted;
        if (len == 0) {  // 0x100193d4-0x100193df (LEN > desc->CAP half NOT checked - see top comment)
            buffer_.clear();
            pos_ = 0;
            poisoned_ = true;  // 0x10019586-0x1001958e: permanent -4 stall
            return CaseSelectorStatus::Poisoned;
        }
        buffer_.assign(len, 0);
        pos_ = 0;

        const uint32_t modeBit = br_.readBits(1);  // 0x100193f3-0x10019417
        if (br_.ranOutOfData()) return CaseSelectorStatus::SourceExhausted;
        if (modeBit == 1) {  // 0x10019537-0x10019584: flat fill
            const uint32_t fillValue = br_.readBits(4);
            if (br_.ranOutOfData()) return CaseSelectorStatus::SourceExhausted;
            std::fill(buffer_.begin(), buffer_.end(), static_cast<uint8_t>(fillValue));
            return CaseSelectorStatus::Ok;
        }

        // mode_bit == 0: table-driven canonical-Huffman expansion, 0x1001941d-0x1001952f
        uint8_t* out = buffer_.data();
        uint32_t remaining = static_cast<uint32_t>(buffer_.size());
        uint8_t lastLiteral = 0;
        while (remaining > 0) {
            const uint32_t idx = br_.peekBits(builtTable_.classByte);  // PEEK, do not consume yet
            if (br_.ranOutOfData()) {  // 0x1001946a-0x1001946d: same poison target as LEN==0
                buffer_.clear();
                pos_ = 0;
                poisoned_ = true;
                return CaseSelectorStatus::Poisoned;
            }
            const uint8_t packed = builtTable_.lutA[idx];        // 0x10019451/0x1001948f
            const uint32_t codeLen = packed >> 4;                 // 0x10019457/0x1001949f
            const uint32_t rawSymbol = packed & 0xF;                // 0x1001945a/0x10019496
            br_.readBits(static_cast<int>(codeLen));                 // consume ONLY codeLen, not classByte
            const uint8_t bl = builtTable_.table16[rawSymbol];     // 0x1001945d/0x1001949b: translate
            if (bl < 0xC) {                                          // 0x100194c4/0x100194c7: on the
                                                                        // TRANSLATED value, not raw_symbol
                *out++ = bl;
                --remaining;
                lastLiteral = bl;  // 0x1001951f-0x19525
            } else {               // bl in {0xC,0xD,0xE,0xF}
                const uint32_t run = kRunLength[static_cast<size_t>(bl) - 0xC];
                if (run > remaining) {  // 0x100194ec: truncate - stop filling right here. The
                    // real DLL leaves the tail holding stale scratch-buffer bytes from a
                    // PREVIOUS call; this from-scratch reimplementation has no such "stale
                    // memory" concept, so the tail stays at its zero-initialized default
                    // instead - a DERIVED choice for a path the one real test vector this file
                    // is validated against does not hit (Sec5.3g's own "bit-for-bit...
                    // bound-cursor==350" cross-check).
                    return CaseSelectorStatus::Ok;
                }
                std::fill(out, out + run, lastLiteral);  // 0x100194f8-0x19518
                out += run;
                remaining -= run;
            }
        }
        return CaseSelectorStatus::Ok;
    }

    BitstreamReader& br_;
    int lumaWidthPixels_;
    std::vector<uint8_t> buffer_;
    size_t pos_ = 0;
    BuiltTable builtTable_;
    bool haveTable_ = false;
    bool poisoned_ = false;
};

}  // namespace sr3binkcodec
