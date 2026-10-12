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
// DESCRIPTOR B / CASE-1 SUB-DISPATCH WIRING (2026-10-09, fix/bink-
// descriptor-b-case1-selector task, spec-bink-codec.md Sec5.3h): case 1's
// own secondary/sub-type nibble - the "subtract 3, range-check 0-6" value
// this file used to read via `br.readBits(4)` directly off the raw
// bitstream - is likewise NOT a raw bitstream read. Sec5.3h, extending
// Sec5.3c/Sec5.3g's already-confirmed treatment of the PRIMARY dispatch
// byte, live-traces the SECONDARY byte to the exact same shape: "a
// zero-bit-cost memory read from descriptor A's and descriptor B's own
// pre-decoded SCRATCH buffers respectively... the secondary/sub-dispatch
// byte (confirmed value 5, `-3` -> 2, matching the already-documented
// 'byte minus 3' formula)". decodeOneBlock() below models this exactly like
// the primary byte: when given a non-null `caseSelectorB`
// (entropy_prestage.h's CaseSelectorScratchSource, constructed with
// descriptor B's own LEN-field formula and sibling-table index - see that
// file's own caseSelectorLengthFieldBitWidthB()/
// kDescriptorBSiblingTableIndex), the sub-dispatch byte is pulled from
// `caseSelectorB->next()` instead of `br.readBits(4)`. When `caseSelectorB`
// is null (every existing synthetic/hand-built-sub-nibble test, unchanged),
// the original `br.readBits(4)` path is used, exactly as before.
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
//                           pixel math to get wrong). UPDATED 2026-10-10
//                           (Sec5.3i): live register trace of a real
//                           dispatch (frame 0, Y-plane, dispatch index 178,
//                           `main_menu_bg.bik`) found the real binary does
//                           NOT reach this behaviour via a raw secondary byte
//                           value of 1 or 4 at all - it tests bit 3 of a
//                           per-call flags byte at `[ebp-0x210]` BEFORE ever
//                           reading the secondary byte, and jumps straight
//                           to the shared skip-epilogue if set. Every one of
//                           5,222 "secondary=None" dispatches this pass
//                           observed in frame 0 took that flag-bit path;
//                           none was observed reaching sub=1/4 via the
//                           literal 7-way jump table. Sec5.3i leaves OPEN,
//                           not resolved, exactly where that flags byte's
//                           own value comes from (whether it is a real
//                           field of the same shared stack slot descriptor
//                           L's byte-cursor lives at, or two unrelated
//                           variables coincidentally sharing an offset) -
//                           without that, this file cannot wire a REAL
//                           bit-exact "read the flag, gate before the
//                           sub-byte read" check (there is nothing concrete
//                           to read it FROM). The sub=1/4-triggered behaviour
//                           below is kept as the best-available functional
//                           stand-in - it produces the identical observable
//                           outcome (skip, no pixel write) the real
//                           flag-gate produces, just reached via a different,
//                           not-necessarily-real trigger path. Flagged
//                           honestly rather than silently presented as the
//                           real mechanism; implementing the real gate is a
//                           distinct, still-open follow-up pending the flag
//                           byte's own provenance.
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
//   Primary 1, sub 5      - Two-literal PATTERN entry: UPDATED 2026-10-09
//                           (fix/bink-pixel-accuracy-oracle task, spec-bink-
//                           codec.md's extended Sec5.3 TwoLiteralPattern
//                           paragraph, "CONFIRMED by live execution against a
//                           real block, 2026-10-09") - the full byte-level
//                           grammar that former OPEN gap named is now closed:
//                           after the 2 literal bytes (read exactly as
//                           before), the real case body consumes EXACTLY 8
//                           raw control bytes, each split into 4 two-bit
//                           fields, each field indexing the already-dumped
//                           `0x1003c130`/`0x1003c200` 2-bit blend-mask pair
//                           to produce one blended dword
//                           (`TABLE1[idx] & literal2_broadcast` OR'd with
//                           `TABLE2[idx] & literal1_broadcast`), written to
//                           TWO destination rows (current + one stride below)
//                           simultaneously - 4 fields x 4 pixels/field = 16
//                           columns, 8 control bytes x 2 rows/byte = 16 rows,
//                           a fixed 16x16-pixel output block whose size is
//                           spec-confirmed to be block-type-specific
//                           (hardcoded immediates), NOT an instance of this
//                           project's own general 8x8 per-block dispatch-grid
//                           convention used everywhere else in this file (see
//                           twoLiteralPattern16x16() below for the exact
//                           CHOSEN/DERIVED bit-order and dword-byte-order
//                           choices the spec text itself leaves unstated, and
//                           this task's own report for the still-OPEN
//                           question this does NOT resolve: how the OUTER
//                           per-row/per-column dispatch loop,
//                           frame_decoder.h's decodePlaneRasterOrder(), is
//                           meant to step its own row/col position after one
//                           of these - spec text confirms the case body's OWN
//                           16x16 output shape, not the calling loop's
//                           cell-advance behaviour, which stays a genuine gap,
//                           flagged rather than invented).
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
//                           UPDATED 2026-10-10 (Sec5.3i): a follow-up task's
//                           own relayed framing claimed this sub-case should
//                           be implemented "by analogy with primary case 3's
//                           own already-implemented shape." Checked directly
//                           against THIS file, again: PrimaryBlockType::
//                           Giant1FullResTwin (primary 3) is, right now,
//                           still an OPEN hook below (see the `rec.primary !=
//                           PrimaryBlockType::SubDispatch` branch) - it has
//                           no implemented pixel-producing body to copy from.
//                           A second, independently-checked instance of a
//                           relayed "already implemented" claim not holding
//                           up (per this project's own peer-relay-
//                           authorization discipline: verify, don't just
//                           relay). Sec5.3i's own text is honest about this
//                           same gap from its side: it gives the shape by
//                           analogy (CONFIRMED structurally: same 64-byte
//                           permutation table, same engine family as case 3,
//                           minus the 2x upsample) at explicitly LOWER
//                           CONFIDENCE than sub 1/2/3/4/6, and separately
//                           reports a genuine, honestly-obtained NEGATIVE
//                           live-occurrence result: zero real sub=0
//                           dispatches found across 20 sampled frame
//                           instances (3 files, keyframes and P-frames
//                           alike) using the same methodology that easily
//                           found sub=2 (2,457x), sub=3 (1,284x), sub=5
//                           (183x), and sub=6 (258x) in the very same
//                           frames. Net: implementing this sub-case for real
//                           would mean inventing the RLE/literal token
//                           format on top of an unimplemented analogy
//                           target, with no live worked example to check
//                           against either way - stays an OPEN hook, now
//                           with its true (doubled) blocker cited precisely
//                           instead of the single-engine framing above.
//                           UPDATED 2026-10-10 (3rd independent check, Bink
//                           descriptor-B merge task): a THIRD relayed task
//                           framing repeated the same refuted claim again,
//                           this time inverted - "primary case 3 needs
//                           Sec5.3i's sub=2 (DcAcIdctEngine2) as ITS OWN
//                           donor" (sub=2 is the IDCT engine; case 3 is the
//                           RLE engine - the two are unrelated families per
//                           Sec5.3b's own case table). Checked directly
//                           against this file again: still no implemented
//                           donor exists for either the RLE token format
//                           (case 3/sub-0) or the AC-coefficient tree
//                           decoder 0x10024eb0 sub=2 actually needs (grepped
//                           this whole codebase for 0x100258d0/breadth-
//                           first-worklist-tree content - NOT implemented
//                           anywhere). Both stay OPEN hooks for the same
//                           reason as before: no citable instruction-level
//                           algorithm to copy, only structural/aggregate
//                           descriptions. Flagging a THIRD time rather than
//                           silently building on an unverified claim, per
//                           this project's own peer-relay-authorization
//                           discipline.
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
//                           5/7's IDCT pair. UPDATED 2026-10-10 (Sec5.3i):
//                           the two gaps this comment used to cite (quant
//                           table bytes; AC-coefficient index mapping) were
//                           in fact ALREADY CLOSED before this update - both
//                           tables are byte-exact dumped in spec-bink-
//                           static-tables.md and the index mapping is
//                           row-major/struct_byte_offset(k)=2k, confirmed in
//                           spec-bink-codec.md around line 743-748 - this
//                           comment was simply stale, a real finding caught
//                           by verifying the relayed premise against this
//                           file's own prior state rather than trusting it.
//                           Sec5.3i itself live-traces this sub-case's full
//                           byte-level sequence: a descriptor-L bound check,
//                           a 16-bit LE DC literal read straight off the
//                           literal-byte stream (zero bit-packing - a plain
//                           pointer read/advance), `call 0x10024eb0` for the
//                           63 AC coefficients, an inline 4-bit quant-row
//                           nibble, then `call 0x100247d0` - a THIRD,
//                           direct-store IDCT entry point with its OWN
//                           calling convention, different from
//                           0x100244c0's (Sec5.3i: "the two 'sibling' IDCT
//                           entry points do not share one calling
//                           convention"). What Sec5.3i does NOT give, and
//                           what genuinely remains OPEN: `0x10024eb0`'s own
//                           private 4-entry dispatch table and seed bytes
//                           (spec-bink-codec.md line ~669: "shared seed
//                           bytes, same cache-then-refill bit read, a
//                           private 4-entry dispatch table" - never
//                           transcribed byte-for-byte anywhere, unlike the
//                           sibling 0x10018ca0 tree's own 0x1003bd44/
//                           0x1003c140 tables). Without that table, decoding
//                           a real AC coefficient VALUE from bits would mean
//                           inventing a tree, not reading one off the page -
//                           UPDATED 2026-10-10 (Sec5.3j): both gaps CLOSED -
//                           0x10024eb0's real instruction-level tree-decode
//                           algorithm and the IDCT's exact bit-exact
//                           rounding sequence are now implemented
//                           (ac_tree_decoder.h/idct_butterfly.h) and wired
//                           in below. The IDCT (Part 1) is bit-exact
//                           VERIFIED against a real FFmpeg-matched block;
//                           the tree decoder (Part 2) has NO live bit-exact
//                           verification available anywhere in the spec
//                           corpus and required several honestly-flagged
//                           interpretive judgment calls (see
//                           ac_tree_decoder.h's own top comment) - its
//                           real-data bit-cost is therefore NOT confirmed
//                           the way the IDCT's is.
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

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "sr3binkcodec/ac_tree_decoder.h"
#include "sr3binkcodec/bitstream_reader.h"
#include "sr3binkcodec/dc_delta_array.h"
#include "sr3binkcodec/dc_prestage_sibling_gap.h"  // Sibling19d10Cursor - giant2/secondary=5's real control-byte stream (Sec5.3ab)
#include "sr3binkcodec/entropy_prestage.h"
#include "sr3binkcodec/entropy_prestage_tables.h"
#include "sr3binkcodec/flat_fill_prestage_array.h"  // FlatFillPrestageCursor - supersedes flat_fill_cursor.h (2026-10-11, Sec5.3z, fix/bink-flat-fill-real-decode)
#include "sr3binkcodec/idct_butterfly.h"
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
//
// Sec5.3i (2026-10-10) delivered the full 7-way byte-level grammar and, as
// a cross-cutting finding, confirmed sub 2/3/5 all write a physical 16x16
// (not 8x8) pixel footprint - a block-type-FAMILY-wide convention for
// primary=1's own "new content" sub-cases, distinct from the 8x8 grid used
// everywhere else in this file/frame_decoder.h. GENUINE ARCHITECTURAL GAP,
// flagged honestly rather than silently patched: this project's own block
// grid (frame_decoder.h's kBlockSize==8, blockGridCols/Rows, and
// decodePlaneRasterOrder's one-dispatch-per-8x8-cell raster walk) has no
// representation for a dispatch that covers a 16x16 footprint without
// colliding with - or silently absorbing - whatever the grid's NEXT 8x8
// cell would otherwise dispatch as its own, separate block. Reconciling
// that (most likely: primary=1's own secondary dispatch loop advances by 2
// grid columns per dispatch for sub 2/3/5 specifically, not 1) is real,
// scoped, un-invented follow-up work, not done here - doing it without
// first confirming the real per-dispatch column-advance amount against live
// data would risk silently breaking every OTHER already-correct 8x8 case
// this file reconstructs. The functions below therefore keep writing their
// previously-established 8x8 footprint where they write pixels at all
// (FlatDcFill) - the 16x16 CONFIRMED-real footprint is cited in each case's
// own comment for the follow-up work, not applied yet.
enum class Case1SubType : uint8_t {
    Giant1RleUpsample0 = 0,  // OPEN: see Primary3/Giant1FullResTwin's citation above - Sec5.3i:
                             // LOWER CONFIDENCE, by-analogy-only, zero live occurrences in 20 sampled frames
    SkipFlag1 = 1,           // Sec5.3i: real mechanism is an earlier flag-bit gate, not this value - see above
    DcAcIdctEngine2 = 2,     // Sec5.3i/Sec5.3j: DC literal + bound check, AC tree decode (0x10024eb0), inline
                             // quant nibble, and the bit-exact IDCT are ALL implemented now (see decodeOneBlock) -
                             // see ac_tree_decoder.h's own top comment for the honestly-flagged judgment calls
                             // the tree decoder still carries (no live bit-exact verification for Part 2)
    FlatDcFill = 3,          // Sec5.3i: CONFIRMED real footprint is 16x16 (64 dwords) - this file still
                             // writes 8x8 pending the block-grid reconciliation noted above
    SkipFlag4 = 4,           // Sec5.3i: same early flag-bit gate as sub 1 - see above
    TwoLiteralPattern = 5,   // CONFIRMED (2026-10-09 spec sync, feature/bink-pixel-accuracy-oracle task):
                             // see twoLiteralPattern16x16() below - produces a block-type-specific 16x16-pixel
                             // output, not this file's usual 8x8 unit (CHOSEN reconciliation with the outer
                             // 8x8 dispatch grid flagged at that function's own doc comment; same grid caveat
                             // Sec5.3i raises for sub 3 above)
    RawCopyDualRowDup6 = 6,  // CONFIRMED (Sec5.3b): see rawCopyDualRowDuplicate8x8() below. Sec5.3i does
                             // NOT include this sub-case in its own 16x16-footprint finding (explicitly
                             // "not measured this pass" for sub 6's output size) - footprint left at 8x8,
                             // not extrapolated from the sub 2/3/5 finding
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

    // Set only for Case1SubType::DcAcIdctEngine2 (spec-bink-codec.md
    // Sec5.3i, added 2026-10-10): the 16-bit little-endian DC literal word
    // read directly from the literal-byte stream ("descriptor L") - a real,
    // CONFIRMED, bit-position-safe read (zero bit-packing, a plain
    // pointer read/advance). This is as far as the pixel-producing grammar
    // is safe to read without inventing the AC-coefficient tree decoder's
    // own untranscribed dispatch table (see Case1SubType::DcAcIdctEngine2's
    // own comment above) - outcome is still OpenHook for this sub-case, but
    // the DC value is real and worth keeping for any caller that wants to
    // inspect how far the real grammar got. std::nullopt for every other
    // case/outcome.
    std::optional<uint16_t> dcLiteral;

    // Diagnostic bit-position instrumentation (2026-10-11, diag/bink-bitpos-
    // vs-5.3y task, spec-bink-codec.md Sec5.3y): the shared main bit-reader
    // `br`'s own bitsConsumedSoFar() at the three measurement points Sec5.3y's
    // own live-captured table uses - "the block-type dispatch read, the
    // AC-tree entry, and the AC-tree exit". Added purely for measurement
    // (this task is diagnostic-only, no fix implied); negligible cost
    // (one size_t read per block, two more for DcAcIdctEngine2 specifically).
    // `dispatchBitPos` is ALWAYS set (the position `br` is at when this
    // block's own decodeOneBlock() call begins, before anything - including
    // the entropy-pre-stage-backed dispatch byte, which costs 0 bits of
    // `br` itself per Sec5.3y Finding 1 - touches it). `acEntryBitPos`/
    // `acExitBitPos` are set only for Case1SubType::DcAcIdctEngine2 (the only
    // case that calls decodeAcCoefficientTree()), std::nullopt otherwise.
    std::optional<size_t> dispatchBitPos;
    std::optional<size_t> acEntryBitPos;
    std::optional<size_t> acExitBitPos;

    // fix/bink-actree-phase1-gateb task (2026-10-11): the raw resolved AC
    // coefficient array decodeAcCoefficientTree() produced for this block
    // (already through the category->raster-k permutation, k-indexed,
    // k=0/DC never written here - same convention AcTreeDecodeResult::coeffs
    // itself uses), kept for direct golden-trace comparison against
    // spec-bink-codec.md Sec5.3ac's own published coefficient lists. Set
    // only for Case1SubType::DcAcIdctEngine2, std::nullopt otherwise - same
    // gating as acEntryBitPos/acExitBitPos above.
    std::optional<std::array<int16_t, 64>> acCoeffs;
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

// Unpacks one 256-byte row (64 little-endian int32 entries,
// quant_byte_offset(k)=4k) of kIntraQuantCompiled/kInterQuantCompiled
// (entropy_prestage_tables.h, spec-bink-static-tables.md) into a plain
// int32 array idct_butterfly.h's idctColumnPass() can consume directly.
// `nibble` is the 4-bit quant-row selector (0-15) read inline during
// Case1SubType::DcAcIdctEngine2's own decode (Sec5.3i: "an inline 4-bit
// quant-row nibble"). Byte-by-byte LE reconstruction (not a reinterpret
// of the raw array) to stay portable/alignment-safe.
inline std::array<int32_t, 64> unpackQuantRow(const std::array<uint8_t, 4096>& table, uint32_t nibble) {
    std::array<int32_t, 64> row{};
    const size_t base = static_cast<size_t>(nibble & 0xF) * 256;
    for (int k = 0; k < 64; ++k) {
        const size_t off = base + static_cast<size_t>(k) * 4;
        uint32_t v = static_cast<uint32_t>(table[off]) | (static_cast<uint32_t>(table[off + 1]) << 8) |
                     (static_cast<uint32_t>(table[off + 2]) << 16) | (static_cast<uint32_t>(table[off + 3]) << 24);
        row[static_cast<size_t>(k)] = static_cast<int32_t>(v);
    }
    return row;
}

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

// spec-bink-codec.md Sec5.3's extended TwoLiteralPattern paragraph ("CONFIRMED
// by live execution against a real block, 2026-10-09"): the 2-bit blend-mask
// pair at `0x1003c130` (TABLE1) / `0x1003c200` (TABLE2), `TABLE2[i] == ~TABLE1[i]`
// exactly for every entry - transcribed byte-for-byte from that section's own
// table.
constexpr uint32_t kTwoLiteralBlendTable1[4] = {0x00000000u, 0x0000FFFFu, 0xFFFF0000u, 0xFFFFFFFFu};
constexpr uint32_t kTwoLiteralBlendTable2[4] = {0xFFFFFFFFu, 0xFFFF0000u, 0x0000FFFFu, 0x00000000u};

// TwoLiteralPattern (primary=1/secondary=5) payload grammar, spec-bink-
// codec.md Sec5.3's extended paragraph, CONFIRMED by live execution 2026-10-09:
// after the 2 literal bytes (already read by the caller, immediately before
// this function is invoked - see decodeOneBlock() below), the real case body
// ("tail-jumps into `0x1001b5a3`") consumes EXACTLY 8 further raw control
// bytes (byte-aligned, no bit-packing - same convention as every other
// pattern-control-byte read in this codec, readAlignedByte()). Each control
// byte is split into 4 two-bit fields; each field's value indexes
// kTwoLiteralBlendTable1/2 above, producing one blended dword
// (`TABLE1[idx] & literal2_broadcast` OR'd with `TABLE2[idx] & literal1_broadcast`)
// that is written to TWO destination rows (current row + one stride below)
// simultaneously - "4 fields x 4 pixels/field = 16 columns, 8 control bytes x
// 2 rows/byte = 16 rows", a FIXED 16x16-pixel output block, confirmed
// block-type-specific (hardcoded immediates), NOT an instance of this
// project's own general 8x8 dispatch-grid unit used by every other case in
// this file.
//
// Two CHOSEN/DERIVED choices the spec text itself does not pin down at the
// bit level (flagged honestly, not independently confirmed facts):
//   1. Two-bit field extraction order within each control byte: this
//      function reads LSB-first (`idx = (byte >> (field*2)) & 3`), matching
//      this codec's own general LSB-first bit-reading convention
//      (bitstream_reader.h's own confirmed accumulator shape) and the
//      existing idx-selection formula spec-bink-codec.md Sec5.3a's own table
//      section already shows for this exact table pair ("idx = (control_byte
//      >> shift) & 3").
//   2. Field-to-column and dword-byte-to-pixel order: field 0 -> leftmost
//      4-pixel group (columns x0..x0+3), field 3 -> rightmost (x0+12..x0+15);
//      within one field's blended dword, byte i (0=LSB) -> column
//      fieldBase+i. This reproduces each table entry's own documented shape
//      exactly as a 2-colour horizontal split (idx 0 = solid literal1, idx 3
//      = solid literal2, idx 1/2 = half-literal2/half-literal1 and its
//      mirror) - the same "this project's own raster convention, flagged
//      rather than left OPEN" tradeoff rawCopyDualRowDuplicate8x8() above
//      already makes for its own traversal-order gap.
//
// CHOSEN reconciliation with this project's own 8x8 per-block dispatch grid
// (frame_decoder.h's decodePlaneRasterOrder(), NOT settled by spec text -
// see this task's own report / block_dispatch.h's top comment for the exact
// still-OPEN question this leaves): this function writes the full 16x16
// output anchored at the dispatching block's own (blockCol*8, blockRow*8)
// pixel origin, clipped to `dest`'s real width/height (never writing out of
// bounds near a plane edge). It does NOT alter the OUTER loop's own
// row/column stepping - the loop still advances exactly one 8x8 cell at a
// time and will attempt a fresh dispatch read at (blockRow, blockCol+1) next,
// which may re-dispatch (and partially overwrite) pixels this call just
// wrote. Spec text confirms the case body's OWN output shape, not whether
// the real game's per-row loop skips the 3 additional 8x8 cells a 16x16
// output already covers - inventing that loop-skip behaviour here would be
// exactly the kind of invented fix this project's "no invented fixes" policy
// forbids, so it is left as-is and flagged rather than guessed. (Neither real
// golden fixture this project's existing tests use hits this case today -
// see this task's own report - so this gap does not affect any current
// regression.)
// `controlCursor` (2026-10-11, spec-bink-codec.md Sec5.3ab, fix/bink-
// 0x10019d10-real-decode task): an OPTIONAL hook for giant2/secondary=5's
// own real control-byte stream (dc_prestage_sibling_gap.h's
// Sibling19d10Cursor, `0x10019d10`'s own decoded output). Sec5.3ab's own
// live memory-read watch found the 8 control bytes this case body consumes
// are NOT a fresh, independent read off the main per-row bitstream `br` -
// they are `0x10019d10`'s own 528-byte decoded array, read sequentially,
// the SAME relationship flat_fill_prestage_array.h's FlatFillPrestageCursor
// already established for Case1SubType::FlatDcFill's own fill byte. NOT
// ACTUALLY WIRED YET, though: this project's own decode0x10019d10()
// (dc_prestage_sibling_gap.h) does not reproduce the golden fixture (see
// that file's own top comment / HANDOFF.md), so every real call site below
// still passes `nullptr` here, keeping the PRIOR `br.readAlignedByte()`
// fallback as the actual production behaviour, unchanged by this task -
// this parameter exists so a future pass with a real, verified
// `0x10019d10` decode can wire it in without touching this function's own
// signature again.
inline bool twoLiteralPattern16x16(BitstreamReader& br, PlaneView dest, int blockRow, int blockCol, uint8_t literal1,
                                    uint8_t literal2, Sibling19d10Cursor* controlCursor = nullptr) {
    const int x0 = blockCol * kBlockSize;
    const int y0 = blockRow * kBlockSize;
    const uint32_t literal1Broadcast = static_cast<uint32_t>(literal1) * 0x01010101u;
    const uint32_t literal2Broadcast = static_cast<uint32_t>(literal2) * 0x01010101u;
    constexpr int kOutputSize = 16;       // CONFIRMED fixed size, see this function's own doc comment
    constexpr int kRowPairs = kOutputSize / 2;  // 8 control bytes
    constexpr int kFieldsPerByte = 4;
    constexpr int kPixelsPerField = 4;
    for (int rowPair = 0; rowPair < kRowPairs; ++rowPair) {
        uint8_t controlByte = 0;
        if (controlCursor != nullptr) {
            if (!controlCursor->hasAtLeast(1)) return false;
            controlByte = controlCursor->readByte();
        } else {
            controlByte = br.readAlignedByte();
            if (br.ranOutOfData()) return false;
        }
        const int y = y0 + rowPair * 2;
        for (int field = 0; field < kFieldsPerByte; ++field) {
            const uint32_t idx = (controlByte >> (field * 2)) & 3u;
            const uint32_t blended =
                (kTwoLiteralBlendTable1[idx] & literal2Broadcast) | (kTwoLiteralBlendTable2[idx] & literal1Broadcast);
            const int xBase = x0 + field * kPixelsPerField;
            for (int i = 0; i < kPixelsPerField; ++i) {
                const uint8_t pixel = static_cast<uint8_t>((blended >> (8 * i)) & 0xFFu);
                const int x = xBase + i;
                if (x < dest.width && y < dest.height) dest.at(x, y) = pixel;
                if (x < dest.width && (y + 1) < dest.height) dest.at(x, y + 1) = pixel;
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
//
// `caseSelectorB`: same idea, for case 1's own secondary/sub-dispatch byte
// (see this file's own top comment, "DESCRIPTOR B / CASE-1 SUB-DISPATCH
// WIRING"). Only consulted when `rec.primary == SubDispatch`; every other
// primary value never touches it.
//
// `dcArray`: the row's own DC delta-accumulation array (dc_delta_array.h,
// spec-bink-codec.md Sec5.3m/Sec5.3p, 2026-10-11 fix/bink-dc-array-indexing
// task - SUPERSEDES the retracted descriptor_l_cursor.h "separate byte
// cursor" model) - consulted only by Case1SubType::DcAcIdctEngine2's own
// DC-literal read below, indexed by `blockCol` (Sec5.3p: "the cursor for
// block (0,0) starts at the buffer's own base, offset 0" - i.e. column 0
// reads array index 0, column 1 reads index 1, etc., one word per
// block-column of the row). nullptr for any existing caller/test that
// hasn't wired this array at all (every synthetic/hand-built-nibble test
// that constructs its own raw case body input directly) - that path keeps
// the PRIOR `br.readAlignedByte()` behaviour unchanged, unaffected by this
// task.
// `flatFillCursor`: Case1SubType::FlatDcFill's own independent, zero-main-
// bitstream-cost cursor (flat_fill_prestage_array.h, spec-bink-codec.md
// Sec5.3y/Sec5.3z, 2026-10-11 fix/bink-flat-fill-real-decode task -
// SUPERSEDES the now-deleted flat_fill_cursor.h's FlatFillByteCursor) -
// zero main-bitstream cost from `br`'s own perspective per Sec5.3y, but
// NOT a separate independently-sourced buffer - Sec5.3z found it is read
// SEQUENTIALLY out of `0x100195b0`'s own real decoded output array (see
// flat_fill_prestage_array.h's own top comment for the full citation and
// the genuine remaining LUT-content gap). `nullptr` (every existing
// synthetic/hand-built-nibble test, unaffected by this task) keeps the
// PRIOR `br.readAlignedByte()` fallback behaviour unchanged - see
// Case1SubType::FlatDcFill's own case body below for exactly why that prior
// behaviour was a real, concretely-evidenced bug for the real-data path
// (not merely a structural nicety).
inline BlockRecord decodeOneBlock(BitstreamReader& br, PlaneView dest, const PlaneView* prev, int blockRow,
                                   int blockCol, CaseSelectorScratchSource* caseSelector = nullptr,
                                   CaseSelectorScratchSource* caseSelectorB = nullptr,
                                   const DcDeltaArray* dcArray = nullptr,
                                   FlatFillPrestageCursor* flatFillCursor = nullptr,
                                   Sibling19d10Cursor* twoLiteralControlCursor = nullptr) {
    BlockRecord rec;
    rec.blockRow = blockRow;
    rec.blockCol = blockCol;
    rec.dispatchBitPos = br.bitsConsumedSoFar();  // diag/bink-bitpos-vs-5.3y, Sec5.3y's own "dispatch bitpos" point

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

    uint32_t subRaw = 0;
    if (caseSelectorB != nullptr) {
        // spec-bink-codec.md Sec5.3h: zero-bit-cost read from descriptor B's
        // own pre-decoded SCRATCH buffer - see this file's own top comment
        // ("DESCRIPTOR B / CASE-1 SUB-DISPATCH WIRING").
        uint8_t secondaryByte = 0;
        const CaseSelectorStatus statusB = caseSelectorB->next(secondaryByte);
        if (statusB == CaseSelectorStatus::SourceExhausted) {
            rec.outcome = BlockOutcome::SourceExhausted;
            return rec;
        }
        if (statusB == CaseSelectorStatus::PreStageOpen) {
            rec.outcome = BlockOutcome::PreStageOpen;
            return rec;
        }
        if (statusB == CaseSelectorStatus::Poisoned) {
            rec.outcome = BlockOutcome::CaseSelectorPoisoned;
            return rec;
        }
        subRaw = secondaryByte;
    } else {
        subRaw = br.readBits(4);  // DERIVED width - legacy/synthetic path, unchanged (see top-of-file comment)
        if (br.ranOutOfData()) {
            rec.outcome = BlockOutcome::SourceExhausted;
            return rec;
        }
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
            // FIX (2026-10-11, spec-bink-codec.md Sec5.3y/Sec5.3z,
            // fix/bink-flat-fill-real-decode task, SUPERSEDES the
            // fix/bink-dc-literal-fillbyte-separate-cursors task's own
            // FlatFillByteCursor): the fill byte is CONFIRMED "one literal
            // byte" (Sec5.3i), Sec5.3y's own live bit-position trace proves
            // it must cost ZERO bits of the MAIN per-row bit-reader `br`
            // (idx1/idx2, both secondary=3, show the IDENTICAL dispatch bit
            // position), and Sec5.3z further found the byte comes from
            // `0x100195b0`'s own real decoded output array (read
            // SEQUENTIALLY, one byte per FlatDcFill dispatch), not from a
            // raw-byte offset into `br`'s own undecoded buffer (the prior
            // FlatFillByteCursor's own wrong anchor choice, now deleted).
            // `flatFillCursor` (flat_fill_prestage_array.h's
            // FlatFillPrestageCursor) wraps that real decoded array - see
            // that file's own top comment for exactly what is/is not
            // confirmed about array content (indices 64-641 of the 650-byte
            // array are NOT confirmed - the LUT table content needed to
            // decode them is a genuine, named spec gap). `flatFillCursor ==
            // nullptr` (every existing synthetic/hand-built-nibble test)
            // keeps the exact prior fallback (`br.readAlignedByte()`) for
            // backward compatibility only - that path is now known-wrong
            // for any REAL decode, kept solely so hand-built tests that
            // construct their own raw case-body byte stream on purpose keep
            // working unchanged.
            uint8_t literal = 0;
            if (flatFillCursor != nullptr) {
                if (!flatFillCursor->hasAtLeast(1)) {
                    rec.outcome = BlockOutcome::SourceExhausted;
                    return rec;
                }
                literal = flatFillCursor->readByte();
            } else {
                literal = br.readAlignedByte();
                if (br.ranOutOfData()) {
                    rec.outcome = BlockOutcome::SourceExhausted;
                    return rec;
                }
            }
            fillBlock8x8(dest, blockRow, blockCol, literal);
            rec.outcome = BlockOutcome::Reconstructed;
            return rec;
        }
        case Case1SubType::TwoLiteralPattern: {
            // CONFIRMED (2026-10-09 spec sync, see twoLiteralPattern16x16()'s
            // own doc comment for the full grammar and the CHOSEN/DERIVED
            // bit-order choices): "reads two literal bytes, each broadcast
            // and stored", then the real case body consumes exactly 8 more
            // raw control bytes producing a fixed 16x16-pixel output block.
            const uint8_t literal1 = br.readAlignedByte();
            const uint8_t literal2 = br.readAlignedByte();
            if (br.ranOutOfData()) {
                rec.outcome = BlockOutcome::SourceExhausted;
                return rec;
            }
            const bool ok =
                twoLiteralPattern16x16(br, dest, blockRow, blockCol, literal1, literal2, twoLiteralControlCursor);
            if (!ok) {
                rec.outcome = BlockOutcome::SourceExhausted;
                return rec;
            }
            rec.outcome = BlockOutcome::Reconstructed;
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
            // Sec5.3i (2026-10-10): LOWER CONFIDENCE, by-analogy-only (to
            // primary case 3 - which is ITSELF still an OPEN hook in this
            // file, see Case1SubType::Giant1RleUpsample0's own top-of-file
            // comment for the full citation) - zero real live occurrences
            // found in 20 sampled frame instances across 3 files. No bytes
            // are read here: the engine's own bit-level RLE/literal token
            // format was never given, and reading even a provisional nibble
            // would risk desyncing the bitstream for every later block on a
            // guess, not a confirmed field width for THIS engine.
            rec.outcome = BlockOutcome::OpenHook;
            return rec;
        case Case1SubType::DcAcIdctEngine2: {
            // Sec5.3i (2026-10-10), live-traced byte-level sequence: a
            // descriptor-L bound check (>=16 bytes remaining in the shared
            // literal-byte stream), then a plain 16-bit little-endian DC
            // literal word read directly off that stream (zero bit-packing
            // - `cx = word[eax]; eax += 2`).
            //
            // UPDATED 2026-10-10 (spec-bink-codec.md Sec5.3j): the two
            // remaining gaps this comment used to cite - 0x10024eb0's own
            // tree-decode algorithm, and the IDCT butterfly's exact
            // rounding sequence - are now CLOSED (Sec5.3j Parts 1/2,
            // ac_tree_decoder.h/idct_butterfly.h). This case now reads the
            // DC literal, decodes the AC-coefficient tree
            // (decodeAcCoefficientTree(), see that header's own top comment
            // for the real algorithm AND the honestly-flagged judgment
            // calls Part 2's own lack of a bit-exact worked example forced
            // - unlike the IDCT below, the tree decoder's bit-cost/output
            // is NOT independently verified against live data), reads the
            // inline 4-bit quant-row nibble (Sec5.3i), and runs the real
            // bit-exact IDCT (Sec5.3j Part 1, CONFIRMED) via 0x100247d0's
            // shared butterfly network to produce real pixels. Uses
            // kIntraQuantCompiled (this sub-case reads new intra content,
            // not a motion-compensated residual - no reference-byte-add
            // step is traced anywhere in Sec5.3i/Sec5.3j for sub=2).
            //
            // FIX (2026-10-11, fix/bink-dc-array-indexing task, spec-bink-
            // codec.md Sec5.3p): "descriptor L" is NOT a separate byte-
            // cursor stream (the now-RETRACTED Sec5.3o/Sec5.3i model this
            // file used before this task, descriptor_l_cursor.h) - it IS
            // `0x1001a160`'s own per-row DC-delta-accumulation array
            // (dc_delta_array.h), read here by a plain ARRAY INDEX
            // (`blockCol` - Sec5.3p: "the cursor for block (0,0) starts at
            // the buffer's own base, offset 0", i.e. column 0 -> index 0,
            // column 1 -> index 1, one word per block-column of the row),
            // word-granular, not bit-granular, with NO interaction with
            // `br`'s own bit-accumulator at the point of THIS read (the
            // array itself was already fully decoded for the whole row
            // before any block's dispatch ran - see frame_decoder.h's own
            // "DC DELTA ARRAY ROW DECODE" note - so by the time any block
            // reaches this line, `dcArray` is a plain already-decoded
            // lookup table, not a live stream). `dcArray == nullptr` (every
            // synthetic/hand-built-nibble test, unaffected by this task)
            // keeps the exact prior fallback behaviour (`br.readAlignedByte()`
            // x2) for backward compatibility - see dc_delta_array.h's own
            // top comment for what is and is NOT confirmed about this
            // array's real decode algorithm (CONFIRMED per-chunk mechanics,
            // Sec5.3m; DERIVED row-granularity/predictor-seed choices,
            // flagged there, not invented silently).
            uint16_t dcLiteral = 0;
            if (dcArray != nullptr) {
                if (!dcArray->hasIndex(static_cast<size_t>(blockCol))) {
                    rec.outcome = BlockOutcome::SourceExhausted;
                    return rec;
                }
                dcLiteral = dcArray->wordAt(static_cast<size_t>(blockCol));
            } else {
                const uint8_t lo = br.readAlignedByte();
                const uint8_t hi = br.readAlignedByte();
                if (br.ranOutOfData()) {
                    rec.outcome = BlockOutcome::SourceExhausted;
                    return rec;
                }
                dcLiteral = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
            }
            rec.dcLiteral = dcLiteral;

            rec.acEntryBitPos = br.bitsConsumedSoFar();  // diag/bink-bitpos-vs-5.3y, Sec5.3y's own "AC-entry bitpos"
            const AcTreeDecodeResult ac = decodeAcCoefficientTree(br);
            rec.acExitBitPos = br.bitsConsumedSoFar();  // diag/bink-bitpos-vs-5.3y, Sec5.3y's own "AC-exit bitpos"
            rec.acCoeffs = ac.coeffs;  // fix/bink-actree-phase1-gateb task
            if (br.ranOutOfData()) {
                rec.outcome = BlockOutcome::SourceExhausted;
                return rec;
            }

            const uint32_t quantNibble = br.readBits(4);  // Sec5.3i: "an inline 4-bit quant-row nibble"
            if (br.ranOutOfData()) {
                rec.outcome = BlockOutcome::SourceExhausted;
                return rec;
            }

            std::array<int16_t, 64> coeffs = ac.coeffs;
            coeffs[0] = static_cast<int16_t>(dcLiteral);  // k=0 = DC, struct_byte_offset(0)=0

            const std::array<int32_t, 64> quantRow = unpackQuantRow(kIntraQuantCompiled, quantNibble);
            uint8_t pixels[64];
            idctBlock8x8(coeffs.data(), quantRow.data(), pixels);

            const int x0 = blockCol * kBlockSize;
            const int y0 = blockRow * kBlockSize;
            for (int y = 0; y < kBlockSize; ++y) {
                for (int x = 0; x < kBlockSize; ++x) {
                    dest.at(x0 + x, y0 + y) = pixels[y * kBlockSize + x];
                }
            }
            rec.outcome = BlockOutcome::Reconstructed;
            return rec;
        }
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
                                         int blockCol, CaseSelectorScratchSource* caseSelector = nullptr,
                                         CaseSelectorScratchSource* caseSelectorB = nullptr,
                                         const DcDeltaArray* dcArray = nullptr,
                                         FlatFillPrestageCursor* flatFillCursor = nullptr) {
    BlockRecord rec =
        decodeOneBlock(br, dest, prev, blockRow, blockCol, caseSelector, caseSelectorB, dcArray, flatFillCursor);
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
