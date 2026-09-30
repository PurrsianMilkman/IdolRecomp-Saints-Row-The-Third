// Reader for the `_media.bnk_pc` wrapper - the `VWSBPC` block directory
// (spec-audio-format.md Sec4, the CONFIRMED, replay-verified part of that
// spec).
//
// ---------------------------------------------------------------------
// SCOPE - read this before extending anything in this module.
// ---------------------------------------------------------------------
// spec-audio-format.md Sec1 draws an explicit third-party boundary and this
// reader sits entirely on one side of it. The plain `<name>.bnk_pc` sibling
// is a genuine, publicly-documented Audiokinetic Wwise SoundBank (Sec3), and
// the bytes each record below points at are Wwise-owned payload - Ogg
// Vorbis, per that spec's Sec6.2. Both are third-party formats, OUT of this
// project's cleanroom scope, the same category as Bink video and the
// Steamworks shim (spec-output.md Sec1).
//
// What IS in scope, and all this module does, is the Volition-authored
// DIRECTORY wrapped around them: an 8-byte magic, a 0x20-byte header, and a
// table of 16-byte records chained by a 0x800-block quantization rule that
// is structurally the same convention `vpp::Container` itself already uses
// (spec-vpp-container.md Sec1.1 - `dir_base`/`name_base`/`payload_start` and
// the per-entry `round_up(uncompressed_size, 0x800)` padding rule).
//
// This reader therefore returns a directory of OPAQUE BYTE RANGES and never
// looks inside one. There is deliberately no `HIRC` walker, no `DIDX`
// walker, no `STID`/`ENVS` dispatch and no codec here, and none should be
// added: Sec6.3 establishes that Wwise's own statically-linked code is what
// recognises those tags, and re-deriving public Wwise internals is exactly
// what this project's cleanroom rule exists to avoid.
//
// ---------------------------------------------------------------------
// Header layout (offsets relative to the start of the entry's OWN payload -
// i.e. the first byte after the outer `.vpp_pc` container has located it;
// spec-audio-format.md Sec4.1)
// ---------------------------------------------------------------------
//
//   +0x00  8  Magic, exact bytes 56 57 53 42 50 43 20 20            CONFIRMED
//             (ASCII `VWSBPC` + two space-pad bytes). Identical
//             across all 536 shipped files. parse() REJECTS a
//             mismatch - this is the format's one hard gate.
//   +0x08  8  Constant in every sample checked:                     OPEN (role)
//             00 00 00 00 02 00 01 00. Sec4.1 rates the VALUE
//             confirmed-by-repetition but the ROLE open: it never
//             varied, so no file-side test could distinguish
//             "format version" from any other constant. Surfaced
//             raw, plus a predicate for the shipped value, and NOT
//             enforced by parse() - see constantAt0x08() below.
//   +0x10  4  u32 cross-reference id. Equals the sibling Wwise      CONFIRMED
//             bank's own `BKHD` SoundBankID (that bank's +0x0C)
//             exactly, 260/260 in-archive pairs plus one verified
//             cross-archive pair (Sec5). See wwise_bank_id.h for
//             the 16-byte read that recovers the other half.
//   +0x14  4  u32, varies per file. Directly RULED OUT as the       OPEN
//             record count (Sec4.1/Sec10). No candidate tested in
//             that pass fit it. Exposed raw, never interpreted.
//   +0x18  4  u32 record count. Equals the real walked record       CONFIRMED
//             count exactly in 536/536 files (Sec4.1/Sec4.2).
//             Exposed, but deliberately NOT used to terminate or
//             gate the walk - see walk()'s own comment.
//   +0x1C  4  u32, varies per file, no candidate matched.           OPEN
//   +0x20  -- Start of the record table.                            CONFIRMED
//
// ---------------------------------------------------------------------
// Record table (spec-audio-format.md Sec4.2)
// ---------------------------------------------------------------------
// Each record is 16 bytes, four little-endian u32 fields in this order:
// `{offset, extra, size, tag}`.
//
//   offset  This record's payload start, as a byte offset from THIS
//           wrapper's own +0x00 (file-relative within the one entry, not
//           the outer archive). See the anchor note below for record 0.
//   extra   A genuine, load-bearing, ADDITIVE field - not padding. When
//           non-zero it is always an exact multiple of 0x800, and the set of
//           values observed matches Sec4.2's exactly: 2048, 4096, 6144,
//           8192, 10240, 12288, 14336, 16384. Its SEMANTIC identity is OPEN.
//
//           HOW COMMON IT IS - a second correction to Sec4.2, measured on
//           the same population. Sec4.2 calls it "zero in the overwhelming
//           majority of records" and Sec4.3 reports that a model ignoring it
//           still walked cleanly on 530/536 files, breaking on "exactly 6".
//           This project measured the opposite proportion:
//
//             records with extra != 0        82,261 / 89,631  (91.8%)
//             files containing any extra != 0    281 / 536
//             naive (extra-ignoring) walk clean  255 / 536
//
//           Those numbers are internally consistent in a way that is worth
//           stating, because it is what makes them hard to dismiss as a
//           reading error: 255 + 281 = 536 exactly, and the naive model's
//           clean set is EXACTLY the set of files with no non-zero extra -
//           it fails on every file that has one, and on no other. The
//           value-set agreement with Sec4.2 also confirms both passes are
//           reading the same field at +0x04, not different ones.
//
//           Sec4.3's conclusion - that `extra` is real, additive, and not
//           padding - is unaffected and independently reproduced here. Only
//           its frequency, and the 530/6 split that frequency implies, are
//           contradicted.
//
//           NOTE on which middle field is which: the chain rule adds `size`
//           and `extra`, so it cannot distinguish {offset, extra, size} from
//           {offset, size, extra} - both replay 536/536 identically. What
//           separates them is their value distributions (+0x04 is
//           low-cardinality and always 0x800-aligned; +0x08 is
//           high-cardinality and mostly not), measured and printed by
//           tools/validation/validate_media_bank.cpp so the assignment above
//           rests on evidence rather than on the chain replay that cannot
//           test it.
//   size    This record's own payload length in bytes.
//   tag     32-bit value, differs per record. Role undetermined - OPEN
//           (Sec4.2 names it the most valuable single field left to explain).
//           Surfaced raw, never interpreted, the same practice sr3zone uses
//           for its opaque 14-byte SR3Z records.
//
// Chain rule, replay-verified over 536/536 files and 89,631 records with
// zero chain breaks and zero overshoots:
//
//     offset[i+1] = offset[i] + round_up(size[i] + extra[i], 0x800)
//
// ---------------------------------------------------------------------
// RECORD 0's ANCHOR - a correction to Sec4.2, measured by this project
// ---------------------------------------------------------------------
// Sec4.2 states "Record 0's offset is always 0x800 - the first 0x800-byte
// block after the header region". The general sentence is right; the literal
// constant is not. Measured directly over the same 536-file population
// (tools/validation/diag_media_bank_first_offset.cpp, all four raw (flags
// 0x0) audio archives - the spec's "mode-(b) archives", label corrected
// 2026-09-30):
//
//   offset[0] == 0x800                                    278 / 536
//   offset[0] == round_up(0x20 + recordCount*16, 0x800)   536 / 536
//   neither                                                 0 / 536
//
// Observed offset[0] values: 0x800 (x278), 0x1000 (x36), 0x1800 (x204),
// 0x2000 (x6), 0x2800 (x3), 0x3000 (x1), 0x3800 (x1), 0x6800 (x1),
// 0x7000 (x2), 0x7800 (x4).
//
// The "header region" the first record follows therefore includes the
// RECORD TABLE, not just the 0x20-byte header - and 0x800 is simply the
// sub-case where the table fits inside the first block (a table of 127
// records or fewer). In all 278 files where offset[0] really is 0x800, the
// block-aligned-table-end rule independently predicts 0x800 too, 278/278 -
// so the corrected rule is a strict generalisation of the spec's, not a
// rival to it.
//
// This is NOT a refutation of the chain rule itself, and the distinction
// cost this pass a full measurement cycle to make: the two claims were
// implemented as one predicate (start the cursor at 0x800, then check every
// record against it), which fails at record 0 on a non-0x800 file and so
// never tests the chain at all. Separated, and re-run from each file's own
// real offset[0], the chain rule reproduces Sec4.2's headline figures
// EXACTLY - 536/536 files closing on the declared size with zero overshoots,
// 89,631 records, and +0x18 equal to the walked count in 536/536.
//
// walk() therefore READS record 0's offset out of the table rather than
// assuming any value for it, per this project's standing preference for an
// oracle the file declares over an endpoint chosen for it. The derived
// block-aligned-table-end rule is exposed separately, as
// blockAlignedTableEnd(), for callers that want to MEASURE the agreement -
// it is not used to drive the walk, because doing so would make the walk
// depend on the +0x18 count field and destroy that field's independence.
//
// ---------------------------------------------------------------------
// Why walk() takes the total size as a parameter
// ---------------------------------------------------------------------
// The walk terminates the moment the running cursor EQUALS the outer
// `.vpp_pc` container's own declared uncompressed size for this entry (that
// directory's +0x0C field - `vpp::PayloadLocation::decompressedLength`).
// That is an oracle the container publishes, not an endpoint this reader
// could choose for itself, so it is an explicit caller-supplied parameter
// rather than something this class tries to find on its own.
//
// This is not incidental API taste; it is the correction Sec4.3/Sec11
// record. An earlier walk that trusted an in-wrapper count field instead
// continued past the real end of the table into the wrapper's zero-padded
// reserved space (the table is over-allocated relative to the real record
// count in EVERY file), where every all-zero record trivially satisfies
// `0 == 0 + round_up(0, 0x800)` - silently reinflating the apparent match
// rate. walk() below refuses a zero-advance record outright for exactly
// that reason, rather than looping on it.
//
// Like `vpp::Container`, this class does not own the underlying bytes: the
// caller must keep the backing buffer alive for the lifetime of a MediaBank.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sr3audio/errors.h"
#include "vpp/byte_view.h"

namespace sr3audio {

using vpp::ByteView;

// Sec4.1: the exact 8 magic bytes, ASCII `VWSBPC` plus two 0x20 pad bytes.
constexpr std::array<uint8_t, 8> kMediaBankMagic = {0x56, 0x57, 0x53, 0x42,
                                                    0x50, 0x43, 0x20, 0x20};

// Sec4.1: the 8 bytes at +0x08, constant across all 536 shipped files. Kept
// as a named constant so a harness can MEASURE agreement rather than assert
// it; parse() never enforces this (the field's role is OPEN).
constexpr std::array<uint8_t, 8> kMediaBankConstantAt0x08 = {0x00, 0x00, 0x00, 0x00,
                                                             0x02, 0x00, 0x01, 0x00};

constexpr size_t kMediaBankHeaderSize = 0x20; // CONFIRMED - record table starts here
constexpr size_t kMediaBankRecordSize = 16;   // CONFIRMED - four LE u32 fields

// The wrapper's block-quantization unit. Numerically identical to
// `vpp::kBlockSize`, and Sec4.2 notes the wrapper deliberately mirrors the
// outer container's convention - but it is restated here rather than
// imported, for the same reason src/zone_header.cpp restates the material
// block's pad rule instead of importing it: this is a property of THIS
// format, and a future finding that changes one must not silently change
// the other.
constexpr size_t kMediaBankBlockSize = 0x800;

// One 16-byte record. All four fields are exposed raw; only `offset`,
// `extra` and `size` have CONFIRMED roles (Sec4.2), `tag` is OPEN.
struct MediaBankRecord {
    uint32_t offset = 0;
    uint32_t extra = 0;
    uint32_t size = 0;
    uint32_t tag = 0;
};

// `round_up(size + extra, 0x800)` - the per-record advance of the CONFIRMED
// chain rule (Sec4.2). A free function so a test or harness can state the
// rule independently of the walk that applies it.
size_t recordBlockSpan(const MediaBankRecord& r);

class MediaBank {
public:
    // Cheap discriminator: do the first 8 bytes match the CONFIRMED magic?
    // Never throws - returns false for a short buffer. For a caller sorting
    // archive entries by shape before committing to a parse.
    static bool looksLikeMediaBank(ByteView content);

    // Parses the 0x20-byte header. Throws FormatError if `content` is too
    // small to hold it, or if the magic does not match. Does NOT walk the
    // record table - that needs the caller's size oracle, see walk().
    static MediaBank parse(ByteView content);

    // +0x08, raw. Role OPEN; compare against kMediaBankConstantAt0x08 via
    // constantAt0x08MatchesShipped() if you want to measure agreement.
    const std::array<uint8_t, 8>& constantAt0x08() const { return constantAt0x08_; }
    bool constantAt0x08MatchesShipped() const {
        return constantAt0x08_ == kMediaBankConstantAt0x08;
    }

    // +0x10. CONFIRMED equal to the sibling plain `.bnk_pc`'s SoundBankID.
    uint32_t crossReferenceId() const { return crossReferenceId_; }

    // +0x14. OPEN - ruled out as the record count, no candidate fit. Raw.
    uint32_t fieldAt0x14() const { return fieldAt0x14_; }

    // +0x18. CONFIRMED to equal the walked record count 536/536. Named
    // "declared" because walk() does not rely on it; compare the two.
    uint32_t declaredRecordCount() const { return declaredRecordCount_; }

    // +0x1C. OPEN. Raw.
    uint32_t fieldAt0x1C() const { return fieldAt0x1C_; }

    // `round_up(0x20 + declaredRecordCount() * 16, 0x800)` - the first
    // 0x800 block after the header AND the record table. Measured equal to
    // the real offset[0] in 536/536 shipped files (see the anchor note at
    // the top of this file), which is the corrected form of Sec4.2's
    // "always 0x800".
    //
    // Provided for callers that want to MEASURE that agreement. walk()
    // deliberately does not use it: it reads offset[0] from the table
    // instead, so that the +0x18 count field this derivation depends on
    // stays an independent signal rather than a walk input.
    size_t blockAlignedTableEnd() const;

    // The bytes this MediaBank was parsed from (non-owning).
    ByteView content() const { return content_; }

    // Walks the record chain per Sec4.2's CONFIRMED rule, terminating the
    // moment the running cursor EQUALS `totalEntrySize` - the outer
    // container's own declared uncompressed size for this entry
    // (`vpp::PayloadLocation::decompressedLength`, the +0x0C directory
    // field). Returns the records in table order.
    //
    // Throws FormatError - never silently truncates - when:
    //   * the cursor overshoots `totalEntrySize` (a chain that cannot land
    //     on the declared end);
    //   * record 0's own offset field - which seeds the cursor - is zero or
    //     not a multiple of 0x800 (every offset[0] in the 536-file shipped
    //     population is a non-zero multiple of 0x800; see the anchor note);
    //   * a record's own `offset` field disagrees with the running cursor
    //     (a chain break, for every record after the first);
    //   * a record advances the cursor by zero bytes, i.e. `size == 0 &&
    //     extra == 0` - the vacuous `0 == 0 + round_up(0, 0x800)` predicate
    //     Sec4.3/Sec11 document. (Ordinary zero padding past the real end of
    //     the table is already caught by the chain-break check above, since
    //     padding reads offset 0 and the cursor never does; this guard
    //     covers the residual case of a zero-span record sitting exactly at
    //     the cursor, which would otherwise loop forever.)
    //   * the table would read past the end of `content()` before the
    //     cursor reaches `totalEntrySize` (an undershooting chain).
    //
    // Deliberately does NOT compare the walked count against
    // declaredRecordCount(): that equality is a CONFIRMED empirical finding
    // (536/536) worth MEASURING as an independent second signal, and turning
    // it into a parse gate would destroy its value as one. The single
    // exception is the degenerate zero-record shape, which has no record 0
    // to read an anchor from: there, and only there, walk() uses +0x18 to
    // recognise that the chain is empty and must end at
    // blockAlignedTableEnd().
    std::vector<MediaBankRecord> walk(size_t totalEntrySize) const;

private:
    ByteView content_;
    std::array<uint8_t, 8> constantAt0x08_{};
    uint32_t crossReferenceId_ = 0;
    uint32_t fieldAt0x14_ = 0;
    uint32_t declaredRecordCount_ = 0;
    uint32_t fieldAt0x1C_ = 0;
};

} // namespace sr3audio
