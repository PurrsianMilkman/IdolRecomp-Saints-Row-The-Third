#pragma once

// sr3binkcodec - the per-block 10-way dispatch (+ case 1's 7-way
// sub-dispatch), spec-bink-codec.md Sec5.3/Sec5.3a.
//
// CONFIRMED (Sec5.3): "Inside the shared per-row loop: a byte is read from
// the bitstream, range-checked (0-9), dispatched via
// `jmp dword ptr [edx*4 + 0x1001cab8]` - a genuine 10-way jump table.
// Value 1's case body reads a second byte, subtracts 3, range-checks
// (0-6), and dispatches via a SECOND jump table, 0x1001cae0 - which is
// exactly 0x1001cab8 + 0x28: table 2's 7 entries are bit-identical to
// table 1's own entries 10-16."
//
// DERIVED bit width for both dispatch reads (flagged, not CONFIRMED at the
// instruction level): Sec5.3a's own exhaustive enumeration of this shared
// accumulator's fixed widths is {1, 3, 4, 7} bits - 8 bits is NOT in that
// list. A 0-9 range (and a 0-6 range after the case-1 "subtract 3") both
// fit a 4-bit field (0-15) and do not fit a 3-bit field (0-7 - too narrow
// for 0-9) or a 1-bit/7-bit field cleanly either. This file therefore reads
// BOTH the primary and case-1 sub-dispatch values via BitstreamReader::
// readBits(4), reading Sec5.3's own informal "a byte is read" as pre-dating
// the more precise Sec5.3a bit-width finding, not as literally an 8-bit
// read. This is this project's own DERIVED reconciliation of two spec
// passages, not an independently quoted fact - see HANDOFF.md / this
// agent's own report for the real-data measurement that motivated it.
//
// ENTROPY PRE-STAGE WIRING (2026-10-08, fix/bink-entropy-prestage task,
// UPDATED 2026-10-08 by fix/bink-entropy-full-algorithm): spec-bink-video-
// test-vectors.md's real case_selector_scratch_frame0.bin (a direct
// Unicorn-CPU-emulation capture of binkw32.dll's own 0x10019360 pre-stage
// OUTPUT, spec-bink-codec.md Sec5.3c) is 350 plain BYTES, every one <= 9 -
// the real dispatch value this binary actually consumes is a decompressed
// byte produced by that pre-stage, not a nibble read out of the raw
// per-row bitstream. decodeOneBlock() below models this: when given a
// non-null CaseSelectorScratchSource (entropy_prestage.h), the primary
// dispatch byte is pulled from it instead of from `br` directly.
//
// spec-bink-codec.md Sec5.3g (2026-10-08) delivered the FULL instruction-
// level algorithm entropy_prestage.h's prior state was missing (the main
// expansion loop, all 5 table-build strategies, the merge primitive, real
// per-row call granularity, and the real starting bit position) - see that
// file's own top comment for the complete accounting. CaseSelectorScratchSource
// now performs a REAL decode (LEN field, whole-call mode bit, then either a
// flat fill or a table-driven canonical-Huffman literal/run-length
// expansion) instead of stopping at the two confirmed reads. It can still
// report CaseSelectorStatus::SourceExhausted (ran off the real bitstream)
// or ::Poisoned (hit the algorithm's own CONFIRMED permanent "-4 stall",
// spec-bink-codec.md Sec5.3g) - both mapped to their own BlockOutcome below
// - but ::PreStageOpen is no longer produced by a correctly-functioning
// refill() (retained only for enum stability, see entropy_prestage.h).
//
// This task's own exhaustive real-data check (every bit position across
// the entire real Y-plane row-0 sub-chunk of main_menu_bg.bik frame 0, plus
// 32 systematic table-build-strategy bit-direction variants) could NOT
// reproduce case_selector_scratch_frame0.bin byte-exact, or even
// approximately (best observed: 10 of 350 consecutive bytes, consistent
// with random chance against a ~13-valued alphabet) - see
// tests/real_bink_test_vector_validation_test.cpp's own STAGE 1 report for
// the full account. This is reported here precisely, per this project's
// "no invented fixes" rule, rather than silently assumed either way: the
// algorithm below is a complete, faithful implementation of every
// instruction spec-bink-codec.md Sec5.3g gives (all 23 per-row table-build
// calls, the exact merge network, both non-merge-network strategies, the
// literal/run-length dispatch on the TRANSLATED symbol), independently
// cross-checked against spec-bink-static-tables.md's own SHA-256 for every
// static table it uses - but closing the algorithm did not, by itself,
// prove out against the one available real-data ground truth. A genuine
// remaining gap (most likely in this project's own understanding of the
// exact bit position/structure surrounding the captured instance, NOT a
// re-opening of the four blockers Sec5.3g closed), not a guess either way.
//
// The `caseSelector == nullptr` path below (this file's own prior
// behaviour, `br.readBits(4)` directly) is kept, unchanged, for every
// existing synthetic/hand-built-nibble test that constructs its own raw
// dispatch-nibble stream on purpose - those tests exercise CASE BODIES,
// not the pre-stage, and are unaffected by any of the above.
//
// Case bodies CONFIRMED (Sec5.3's own table, UPDATED by Sec5.3b's 2026-10-08
// full case-by-case map) and what this file does with each:
//
//   Primary 0, 9          - Block copy: Sec5.3b's own closing case-by-case
//                           table states plainly "0, 9 | Plain block copy |
//                           No - previous frame" - the per-value mapping gap
//                           that used to block primary 9 specifically (see
//                           the superseded comment below) is CLOSED for this
//                           one value. FULLY RECONSTRUCTED here, identical
//                           code path to primary 0.
//   Primary 1, sub 1 or 4 - Mark skip flag, no pixel write. FULLY
//                           RECONSTRUCTED here (trivially - there is no
//                           pixel math to get wrong).
//   Primary 1, sub 3      - Flat/DC fill: one literal byte, broadcast
//                           across the block. FULLY RECONSTRUCTED here.
//   Primary 1, sub 6      - Sec5.3b (2026-10-08) CLOSES this sub-case:
//                           previously mis-grouped with sub 3 as "DC/flat
//                           fill" (a transcription slip the spec itself now
//                           calls out), it is actually a literal/raw
//                           copy-with-dual-row-duplication engine: "an
//                           alignment test selects a byte-granular
//                           'unaligned' copy loop or a word-granular
//                           'aligned' copy loop, each repacking literal
//                           source bytes/words into a dword written to two
//                           destination rows at once, with zero bitstream
//                           bit-reads, table lookups or external calls - a
//                           sibling of the plain block-copy cases (primary
//                           0/9)". FULLY RECONSTRUCTED here (see
//                           rawCopyDualRowDuplicate8x8() below for the exact
//                           DERIVED byte-order/traversal choice this file
//                           makes where the spec gives the shape but not the
//                           literal-to-pixel assignment order - the
//                           byte-vs-word "aligned/unaligned" split itself is
//                           a pure x86-pointer-alignment performance
//                           artifact that does not change which bytes end up
//                           in which pixels, so this file collapses both
//                           paths into one byte-granular read).
//   Primary 1, sub 5      - Two-literal PATTERN entry: two literal bytes
//                           are read (modelled here, byte-aligned per
//                           Sec5.3a), then Sec5.3 says the real case body
//                           "tail-jumps into the largest bitstream engine"
//                           for the actual per-pixel blend pattern. Sec5.3a
//                           (2026-10-08) has SINCE dumped the real 2-bit
//                           blend-mask table pair this engine selects from
//                           (0x1003c130/0x1003c200) - but NOT the per-block
//                           traversal that maps bitstream control-byte 2-bit
//                           fields onto specific pixel positions (how many
//                           control bytes, what raster order) - still a
//                           genuine gap, not just the table lookup, so this
//                           stays an OPEN hook: this file reads the two
//                           literals (keeping bitstream accounting honest up
//                           to that point) and then reports OPEN rather than
//                           guessing the spatial mapping.
//   Primary 1, sub 0      - "Giant1": Sec5.3a names this the run-length
//                           literal/replicate engine, 4-bit-nibble-selected
//                           through the now-FULLY-DUMPED 64-byte scan-order
//                           permutation table (0x100378c0, transcribed in
//                           spec-bink-codec.md Sec5.3a in full), then 2x
//                           pixel-doubled/upsampled. The table landing closes
//                           only the COEFFICIENT-ORDERING half of this gap -
//                           the engine's own bit-level run-length/literal
//                           TOKEN format (how many bits encode a run count,
//                           what marks "literal" vs "replicate", at which of
//                           the four CONFIRMED widths {1,3,4,7}) was never
//                           wired per-field for this specific engine, only
//                           classified in the aggregate ("fixed-width +
//                           table-driven, not Huffman" - Sec5.3a's own
//                           title). Implementing pixel output here would mean
//                           inventing that wiring, not reading it off the
//                           page - stays an OPEN hook. (NOTE: an earlier
//                           relayed task framing assumed this case was
//                           "already implemented" elsewhere in this project;
//                           checked directly against this file's own history
//                           and found NOT to be the case - correcting that
//                           assumption here rather than silently building on
//                           it.)
//   Primary 2             - Sec5.3b (2026-10-08): "a third motion-copy
//                           variant (CONFIRMED). Signed dy/dx motion vector,
//                           dual bounds-checked, straight 8x8 block copy from
//                           the computed reference offset". The STRUCTURE is
//                           confirmed, but no spec text available to this
//                           task gives the motion vector's own bit-width or
//                           encoding (how many bits, signed-magnitude vs
//                           two's-complement, any escape/extension code) -
//                           without that, decoding a real dy/dx pair would be
//                           invented, not read. OPEN hook.
//   Primary 3             - Sec5.3b (2026-10-08): "giant1's full-resolution
//                           twin... the exact same run-length/literal-
//                           replicate engine as 0x1001af5e... but without
//                           giant1's final 2x pixel-doubling upsample". Same
//                           engine as primary 1 sub 0 above, therefore
//                           blocked by the exact same bit-level token-format
//                           gap (the scan-order table landing closes the
//                           ordering half, not the token-decode half). OPEN
//                           hook, same justification as 1-sub-0.
//   Primary 1, sub 2      - Sec5.3b (2026-10-08) CLOSES the former "DC/flat
//                           fill" mis-grouping for this sub-case too: it is a
//                           "third DC+AC dequant/IDCT engine (direct-store)"
//                           - real content, a real DCT, sibling of primary
//                           5/7's IDCT pair. Blocked by the SAME two gaps
//                           Sec8 item 5a still lists open for primary 5/7
//                           below (quantization table byte contents not
//                           transcribed; AC-coefficient byte-offset-to-index
//                           mapping not given) - OPEN hook, not a guess.
//   Primary 4             - Sec5.3b: motion compensation (same unresolved
//                           motion-vector-encoding gap as primary 2) PLUS a
//                           Huffman-tree-coded residual add (the tree
//                           STRUCTURE - breadth-first worklist, 3-bit depth,
//                           1-bit-per-node decisions - is described, but its
//                           own 4-entry dispatch table's literal byte values
//                           were not transcribed into spec text, and the
//                           residual-add's own position-to-coefficient
//                           mapping is not given either). Two independent
//                           invention risks stacked - OPEN hook.
//   Primary 5             - Sec5.3b: "Intra 2-stage fixed-point 8-point
//                           IDCT" - real DCT, but Sec8 item 5a (as of the
//                           2026-10-08 sync still current) explicitly leaves
//                           OPEN "the precise byte-offset-to-coefficient-
//                           index mapping inside 0x10024eb0's output struct
//                           (zig-zag/row-major/other...)" AND the
//                           0x10037cc0/0x10038cc0 16-row quantization
//                           matrices are only described ("clean linear
//                           quantizer-strength progression") never
//                           transcribed byte-for-byte the way the scan-order/
//                           blend-mask tables were. Both are load-bearing for
//                           a correct dequantize+IDCT; neither is available.
//                           OPEN hook.
//   Primary 6             - Never individually named in Sec5.3b's own
//                           closing case-by-case table (which explicitly
//                           lists 0,9/1-subs/2/3/4/5/7 but has no row for 6)
//                           - genuinely un-mapped, not merely hard. OPEN
//                           hook.
//   Primary 7             - Sec5.3b: "Motion compensation + 2-stage
//                           fixed-point 8-point IDCT residual" - stacks
//                           primary 2's unresolved motion-vector gap on top
//                           of primary 5's unresolved quant-table/index-
//                           mapping gap. OPEN hook.
//   Primary 8             - Sec5.3a: a 4-bit/16-entry PATTERN variant
//                           (0x1001c608), blend-mask table pair NOW fully
//                           dumped (0x1003c180/0x1003c1c0) - but, same as
//                           primary-1-sub-5 above, the control-nibble-to-
//                           pixel-position spatial mapping was never given.
//                           OPEN hook.
//
// Former blanket framing (now superseded, kept here for history): earlier
// this file treated ALL of primary 4-9 uniformly as OPEN because "WHICH of
// the six numeric primary values (4..9) maps to which named behaviour is NOT
// stated anywhere in spec-bink-codec.md". Sec5.3b (2026-10-08) closed that
// per-value mapping for every one of 2/3/4/5/7/9 (and primary 1's own
// sub-dispatch for sub 2/6) by name - what stays OPEN now is each
// INDIVIDUAL case's own remaining data/wiring gap (cited above per case),
// not an unresolved "which case is which" ambiguity.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/entropy_prestage.h"
#include "sr3binkcodec/planar_buffer.h"
#include "sr3luahost/open_state.h"

namespace sr3binkcodec {

// spec-bink-codec.md Sec5.3's own 10 primary values. Names follow Sec5.3b's
// (2026-10-08) closing case-by-case map, which names every one of these by
// role even where the pixel math itself stays OPEN (see this file's own top
// comment for the per-case citation and exactly what remains open for each).
enum class PrimaryBlockType : uint8_t {
    BlockCopy = 0,
    SubDispatch = 1,
    MotionCopyVariant3 = 2,     // OPEN: motion-vector bit-width/encoding not given
    Giant1FullResTwin = 3,      // OPEN: giant1's own bit-level token format not given
    MotionHuffmanResidual4 = 4, // OPEN: motion-vector gap + tree-table values not given
    IntraIdct5 = 5,             // OPEN: quant-table bytes + AC index mapping not given
    Unmapped6 = 6,              // OPEN: never individually named by any spec pass
    InterIdctResidual7 = 7,     // OPEN: same gaps as 5, plus the motion-vector gap
    PatternBlend4Bit8 = 8,      // OPEN: control-nibble-to-pixel spatial mapping not given
    PlainBlockCopy9 = 9,        // CONFIRMED (Sec5.3b): same as primary 0
};

// spec-bink-codec.md Sec5.3's case-1 sub-values (already offset by the
// confirmed "subtract 3"). Sec5.3b (2026-10-08) individually named sub 2 and
// sub 6 (previously lumped together as "DC/flat fill", a transcription slip
// the spec itself now flags) - sub 0/5 were already named by Sec5.3/Sec5.3a.
enum class Case1SubType : uint8_t {
    Giant1RleUpsample0 = 0,  // OPEN: see Primary3/Giant1FullResTwin's citation above
    SkipFlag1 = 1,
    DcAcIdctEngine2 = 2,     // OPEN: same quant-table/index-mapping gap as primary 5/7
    FlatDcFill = 3,
    SkipFlag4 = 4,
    TwoLiteralPattern = 5,   // OPEN: control-byte spatial mapping not given
    RawCopyDualRowDup6 = 6,  // CONFIRMED (Sec5.3b): see rawCopyDualRowDuplicate8x8() below
};

enum class BlockOutcome : uint8_t {
    Reconstructed,       // pixel/flag output is a real, CONFIRMED-shape result
    OpenHook,            // dispatch recognised; pixel math is a known OPEN gap
    OutOfRangeDispatch,  // a dispatch value failed its own CONFIRMED range check
    SourceExhausted,     // ran off the end of the real input while reading
    PreStageOpen,        // RETAINED FOR ENUM STABILITY - see entropy_prestage.h's own
                         // CaseSelectorStatus::PreStageOpen doc comment: no longer produced
                         // by a correctly-functioning pre-stage as of spec-bink-codec.md
                         // Sec5.3g's full algorithm (2026-10-08, fix/bink-entropy-full-
                         // algorithm task).
    CaseSelectorPoisoned,  // the case-selector entropy pre-stage hit its own CONFIRMED
                           // "-4 permanent stall" poison state (entropy_prestage.h's
                           // CaseSelectorStatus::Poisoned, spec-bink-codec.md Sec5.3g's
                           // 0x10019586-0x1001958e) - a REAL decode outcome (decoded LEN==0,
                           // or the real bitstream ran out mid-expansion), not a bug in this
                           // decoder, distinct from the generic SourceExhausted above.
};

enum class StopReason : uint8_t {
    None,                 // decode is still going (trace in progress)
    CompletedAllBlocks,   // every block in the requested grid was processed
    HitOpenHook,          // stopped at a block whose pixel math is OPEN
    HitOutOfRangeDispatch,
    SourceExhausted,
    HitPreStageOpen,  // RETAINED FOR ENUM STABILITY - see BlockOutcome::PreStageOpen
    HitCaseSelectorPoison,  // the case-selector entropy pre-stage hit its own CONFIRMED permanent
                            // poison state - see BlockOutcome::CaseSelectorPoisoned
};

struct BlockRecord {
    int blockRow = 0;
    int blockCol = 0;
    PrimaryBlockType primary = PrimaryBlockType::BlockCopy;
    std::optional<Case1SubType> sub;
    BlockOutcome outcome = BlockOutcome::Reconstructed;

    // The actual dispatch value read for the primary dispatch - either a
    // raw 4-bit bitstream nibble (legacy/synthetic-test path, no
    // CaseSelectorScratchSource given) or a decoded entropy-pre-stage byte
    // (entropy_prestage.h, real-data path) - ALWAYS set when a value was
    // actually obtained (unlike `primary` above, which is only meaningful
    // when outcome != OutOfRangeDispatch/SourceExhausted/PreStageOpen - its
    // default-constructed value is PrimaryBlockType::BlockCopy, i.e. 0,
    // which would otherwise silently mislabel a real out-of-range nibble
    // like 12 or 15 as "primary=0" in any trace/histogram). NOT meaningful
    // for outcome == PreStageOpen (no byte was obtained at all - the
    // pre-stage itself stopped first, see BlockOutcome's own doc comment).
    // Found while running this project's own real-data FFmpeg validation
    // task (2026-10-08): two of the three target frames hit
    // BlockOutcome::OutOfRangeDispatch on their very first block, and the
    // existing trace printer reported "primary=0" for them - indistinguishable
    // from a real BlockCopy dispatch until this field was added. A real bug
    // found by measurement, not a cosmetic change - see this project's own
    // measurement-discipline note for why this matters.
    uint32_t rawPrimaryNibble = 0;
};

// 8x8 pixels, 1 byte/pixel - the one block size spec-bink-codec.md Sec5.6
// pins at the instruction level ("an unrolled 8x8-pixel/1-byte-per-pixel
// copy", confirmed both by Sec5.3's own "unrolled 16-dword copy" - 16
// dwords = 64 bytes = 8x8x1 - and Sec5.6's literal store trace). This file
// uses the SAME 8x8 grid for every primary/sub case it reconstructs,
// including DC-fill (Sec5.3 hedges DC-fill's own extent as "~40
// consecutive locations spanning a whole macroblock" - approximate, not
// byte-exact) - unifying DC-fill onto the byte-exact 8x8 unit is this
// file's own DERIVED simplification, flagged here rather than silently
// assumed identical to block-copy's independently-confirmed size.
constexpr int kBlockSize = 8;

[[noreturn]] inline void throwGiantEngineOpen(const char* which) {
    throw sr3luahost::OpenStateError(
        which,
        "spec-bink-codec.md Sec5.3/Sec5.3a/Sec8 item 2: dispatch to this case is CONFIRMED (jump-table "
        "structure, reachability, zero-SIMD/zero-FPU fingerprint), but its exact bit-level pixel-producing "
        "math was not traced this pass - never invented here");
}

// Copies one 8x8 block from `prev` to `dest` at the given block coordinates
// (spec-bink-codec.md Sec5.6: "the copy *source*... resolves to the SAME
// +0x40 field but of the *previous*-frame slot - confirming textbook
// 'skip block = copy from last frame' semantics"). If `prev` is null (this
// project's own frame-0 test harness has no real previous frame - frame 0
// is a keyframe with nothing before it), the destination block is left
// untouched (still holding PlanarYuvFrame::kUndecodedSentinel) rather than
// inventing previous-frame content; the dispatch TYPE is still recorded as
// reconstructed, since the block-copy case itself is fully understood -
// only the source pixels are unavailable in this specific test scenario.
inline void copyBlock8x8(PlaneView dest, const PlaneView* prev, int blockRow, int blockCol) {
    if (!prev) return;
    const int x0 = blockCol * kBlockSize;
    const int y0 = blockRow * kBlockSize;
    for (int y = 0; y < kBlockSize; ++y) {
        for (int x = 0; x < kBlockSize; ++x) {
            dest.at(x0 + x, y0 + y) = prev->at(x0 + x, y0 + y);
        }
    }
}

inline void fillBlock8x8(PlaneView dest, int blockRow, int blockCol, uint8_t value) {
    const int x0 = blockCol * kBlockSize;
    const int y0 = blockRow * kBlockSize;
    for (int y = 0; y < kBlockSize; ++y) {
        for (int x = 0; x < kBlockSize; ++x) {
            dest.at(x0 + x, y0 + y) = value;
        }
    }
}

// spec-bink-codec.md Sec5.3b (2026-10-08): case 1 sub 6, "a literal/raw
// copy-with-dual-row-duplication engine... an alignment test selects a
// byte-granular 'unaligned' copy loop or a word-granular 'aligned' copy
// loop, each repacking literal source bytes/words into a dword written to
// two destination rows at once, with zero bitstream bit-reads, table
// lookups or external calls".
//
// This file collapses the byte-granular/word-granular split into one
// uniform byte-granular read: that split is the original x86 code
// branching on SOURCE POINTER ALIGNMENT to pick a faster instruction
// sequence (4 single-byte reads vs 2 word reads) - a CPU-performance
// artifact of operating on a raw memory pointer, not a different decode
// outcome (either path reads the same 4 logical bytes off the same byte
// stream and builds the same dword). A from-scratch reimplementation
// reading from an abstract byte buffer has no such alignment concept to
// preserve, so reproducing the branch itself would add nothing but
// unreachable-in-practice complexity - this is the same category of
// simplification copyBlock8x8() above already makes (not reproducing
// primary 0's own "unrolled 16-dword copy" instruction count).
//
// What IS a genuine DERIVED choice (not read directly off the spec text,
// flagged honestly): the exact traversal order pairing each freshly-read
// 4-byte dword to a (row-pair, column-group) position. The spec confirms
// the SHAPE (one dword, two rows, zero bit-packing) but not which of the
// 8 dword-writes an 8x8 block needs (4 row-pairs x 2 column-groups) reads
// its 4 source bytes first. Since the 8 writes are mutually
// non-overlapping (this is not a case where traversal order changes the
// final pixel values except through which SOURCE bytes land where), this
// file picks the same raster convention (top-to-bottom row-pairs, then
// left-to-right column-groups within each) every other case handler in
// this file already uses (copyBlock8x8/fillBlock8x8's own y-outer/x-inner
// loops) - consistent with this project's house style of flagging a
// necessary ordering choice rather than leaving an otherwise-fully-shaped
// case entirely OPEN (the same tradeoff this file's own top comment
// already documents for the 4-bit dispatch-width DERIVED choice).
inline bool rawCopyDualRowDuplicate8x8(BitstreamReader& br, PlaneView dest, int blockRow, int blockCol) {
    const int x0 = blockCol * kBlockSize;
    const int y0 = blockRow * kBlockSize;
    constexpr int kRowPairs = kBlockSize / 2;       // 4
    constexpr int kColGroups = kBlockSize / 4;       // 2 (4 bytes/dword)
    for (int rowPair = 0; rowPair < kRowPairs; ++rowPair) {
        const int y = y0 + rowPair * 2;
        for (int colGroup = 0; colGroup < kColGroups; ++colGroup) {
            const int x = x0 + colGroup * 4;
            uint8_t bytes[4];
            for (uint8_t& b : bytes) {
                b = br.readAlignedByte();
                if (br.ranOutOfData()) return false;
            }
            for (int i = 0; i < 4; ++i) {
                dest.at(x + i, y) = bytes[i];
                dest.at(x + i, y + 1) = bytes[i];
            }
        }
    }
    return true;
}

// Decodes exactly one block at (blockRow, blockCol), writing into `dest`
// (and reading from `*prev` for the block-copy case, if given). Returns
// the BlockRecord describing what happened; throws sr3luahost::
// OpenStateError only when the CALLER asked for strict mode (see
// frame_decoder.h, which catches this and turns it into a trace stop
// instead of letting it propagate) - decodeOneBlock() itself always
// returns normally, using BlockOutcome::OpenHook, so a caller that wants
// to inspect every block's classification (not just stop at the first
// OPEN one) can do so.
//
// `caseSelector`: when non-null, the primary dispatch byte is pulled from
// this entropy-pre-stage-backed source (entropy_prestage.h) instead of
// read as a raw nibble off `br` - see this file's own top comment
// ("ENTROPY PRE-STAGE WIRING") for exactly what that source can and
// cannot do today. When null (the default - every existing synthetic/
// hand-built-nibble test uses this path, unchanged), `br.readBits(4)` is
// used directly, preserving this function's original behaviour exactly.
inline BlockRecord decodeOneBlock(BitstreamReader& br, PlaneView dest, const PlaneView* prev, int blockRow,
                                   int blockCol, CaseSelectorScratchSource* caseSelector = nullptr) {
    BlockRecord rec;
    rec.blockRow = blockRow;
    rec.blockCol = blockCol;

    uint32_t primaryRaw = 0;
    if (caseSelector != nullptr) {
        uint8_t dispatchByte = 0;
        const CaseSelectorStatus status = caseSelector->next(dispatchByte);
        if (status == CaseSelectorStatus::SourceExhausted) {
            rec.outcome = BlockOutcome::SourceExhausted;
            return rec;
        }
        if (status == CaseSelectorStatus::PreStageOpen) {
            rec.outcome = BlockOutcome::PreStageOpen;
            return rec;
        }
        if (status == CaseSelectorStatus::Poisoned) {
            rec.outcome = BlockOutcome::CaseSelectorPoisoned;
            return rec;
        }
        primaryRaw = dispatchByte;
    } else {
        primaryRaw = br.readBits(4);  // DERIVED width - see top-of-file comment
        if (br.ranOutOfData()) {
            rec.outcome = BlockOutcome::SourceExhausted;
            return rec;
        }
    }
    rec.rawPrimaryNibble = primaryRaw;  // always set when a value was obtained - see BlockRecord's own doc comment
    if (primaryRaw > 9) {
        rec.outcome = BlockOutcome::OutOfRangeDispatch;
        return rec;
    }
    rec.primary = static_cast<PrimaryBlockType>(primaryRaw);

    if (rec.primary == PrimaryBlockType::BlockCopy || rec.primary == PrimaryBlockType::PlainBlockCopy9) {
        // Sec5.3b's own closing case-by-case table: "0, 9 | Plain block
        // copy | No - previous frame" - identical handling for both values.
        copyBlock8x8(dest, prev, blockRow, blockCol);
        rec.outcome = BlockOutcome::Reconstructed;
        return rec;
    }

    if (rec.primary != PrimaryBlockType::SubDispatch) {
        // Primary 2/3/4/5/6/7/8: each individually named by Sec5.3b now
        // (see this file's own top comment for the per-case citation), but
        // each still blocked by its own specific remaining data/wiring gap
        // (motion-vector encoding, quant-table byte contents, AC-coefficient
        // index mapping, or - for primary 6 - never having been mapped by
        // any spec pass at all). OPEN by name, not by omission.
        rec.outcome = BlockOutcome::OpenHook;
        return rec;
    }

    const uint32_t subRaw = br.readBits(4);  // DERIVED width - see top-of-file comment
    if (br.ranOutOfData()) {
        rec.outcome = BlockOutcome::SourceExhausted;
        return rec;
    }
    const int32_t sub = static_cast<int32_t>(subRaw) - 3;  // CONFIRMED: "subtracts 3"
    if (sub < 0 || sub > 6) {
        rec.outcome = BlockOutcome::OutOfRangeDispatch;
        return rec;
    }
    rec.sub = static_cast<Case1SubType>(sub);

    switch (*rec.sub) {
        case Case1SubType::SkipFlag1:
        case Case1SubType::SkipFlag4:
            // CONFIRMED: "writes a literal 1 into a per-macroblock flag
            // array... no pixel write." No flag-array model exists in this
            // skeleton (out of scope - nothing downstream of this test
            // reads it), so this is a true no-op; still Reconstructed,
            // since the full behaviour (no pixels, flag set) is known.
            rec.outcome = BlockOutcome::Reconstructed;
            return rec;
        case Case1SubType::FlatDcFill: {
            const uint8_t literal = br.readAlignedByte();  // CONFIRMED: "reads one literal byte"
            if (br.ranOutOfData()) {
                rec.outcome = BlockOutcome::SourceExhausted;
                return rec;
            }
            fillBlock8x8(dest, blockRow, blockCol, literal);
            rec.outcome = BlockOutcome::Reconstructed;
            return rec;
        }
        case Case1SubType::TwoLiteralPattern: {
            // CONFIRMED up to here: "reads two literal bytes, each
            // broadcast and stored" - then tail-jumps into the largest
            // bitstream engine (OPEN). Read the two literals so the
            // bitstream position this far is honestly accounted for, but
            // do not guess the blend pattern that follows.
            (void)br.readAlignedByte();
            (void)br.readAlignedByte();
            if (br.ranOutOfData()) {
                rec.outcome = BlockOutcome::SourceExhausted;
                return rec;
            }
            rec.outcome = BlockOutcome::OpenHook;
            return rec;
        }
        case Case1SubType::RawCopyDualRowDup6: {
            // Sec5.3b (2026-10-08) CLOSES this sub-case: see
            // rawCopyDualRowDuplicate8x8()'s own doc comment for the full
            // citation and the one DERIVED traversal-order choice it makes.
            const bool ok = rawCopyDualRowDuplicate8x8(br, dest, blockRow, blockCol);
            if (!ok) {
                rec.outcome = BlockOutcome::SourceExhausted;
                return rec;
            }
            rec.outcome = BlockOutcome::Reconstructed;
            return rec;
        }
        case Case1SubType::Giant1RleUpsample0:
        case Case1SubType::DcAcIdctEngine2:
            rec.outcome = BlockOutcome::OpenHook;
            return rec;
    }
    rec.outcome = BlockOutcome::OpenHook;  // unreachable, defensive
    return rec;
}

// Strict variant: throws sr3luahost::OpenStateError instead of returning
// BlockOutcome::OpenHook, for callers that want the OpenValue-family
// convention's usual "throw on OPEN" behaviour directly (e.g. a future
// game-side integration, matching how every other OPEN engine-state read in
// this codebase behaves). frame_decoder.h's own trace-building loop uses
// the non-throwing decodeOneBlock() above instead, specifically so it can
// keep counting dispatch TYPES even past the point pixel reconstruction
// stops.
inline BlockRecord decodeOneBlockStrict(BitstreamReader& br, PlaneView dest, const PlaneView* prev, int blockRow,
                                         int blockCol, CaseSelectorScratchSource* caseSelector = nullptr) {
    BlockRecord rec = decodeOneBlock(br, dest, prev, blockRow, blockCol, caseSelector);
    if (rec.outcome == BlockOutcome::OpenHook) {
        std::string which = "primary=" + std::to_string(static_cast<int>(rec.primary));
        if (rec.sub) which += " sub=" + std::to_string(static_cast<int>(*rec.sub));
        throwGiantEngineOpen(which.c_str());
    }
    if (rec.outcome == BlockOutcome::PreStageOpen) {
        throwGiantEngineOpen("case-selector entropy pre-stage (entropy_prestage.h) - see that file's own top "
                              "comment for exactly what is/isn't modelled");
    }
    if (rec.outcome == BlockOutcome::CaseSelectorPoisoned) {
        throwGiantEngineOpen("case-selector entropy pre-stage hit its own CONFIRMED permanent poison state "
                              "(entropy_prestage.h's CaseSelectorStatus::Poisoned, spec-bink-codec.md Sec5.3g) - "
                              "a real decode outcome, not an unimplemented gap");
    }
    return rec;
}

}  // namespace sr3binkcodec
