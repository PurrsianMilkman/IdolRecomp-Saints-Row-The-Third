// Reader for `.czh_pc` - the zone header (spec-zone-data-format.md Sec4/
// Sec9.1, spec-ctorless-types.md Sec5, spec-terrain-format.md Sec2).
//
// THIS READER TARGETS ONLY THE CONFIRMED PART OF THIS FORMAT. A `.czh_pc`
// file is, in order:
//
//   1. the shared material-reference block (sr3geometry::MaterialBlock) -
//      REUSED, not reimplemented, exactly per this project's standing rule
//      about not duplicating shared parsing logic. spec-terrain-format.md
//      Sec2 additionally records that this block's own name TABLE is
//      internally split into ground/splat-texture and `.fmeshx`
//      foliage-mesh-source groups by embedded `cc:N` marker strings - that
//      grouping is real but is text content *inside* the name list the
//      shared block already walks byte-for-byte, not a different binary
//      layout, so it needs no separate code here; `N`'s meaning is OPEN.
//   2. the mandatory 16-byte pad (spec-geometry-format.md Sec3.1.1 - ALWAYS
//      1-16 bytes, a full 16 when the material block already ends
//      16-aligned; NOT a plain round-up-to-16). Reused via
//      sr3geometry::roundUp16-style arithmetic, restated here rather than
//      imported, since the pad is a general rule about the material block
//      itself, not a sr3geometry-specific helper.
//   3. the `SR3Z` block (spec-ctorless-types.md Sec5, CONFIRMED via
//      disassembly + a 2,971/2,971 whole-population exact-size replay):
//
//        +0x00  u32  magic 'SR3Z' (0x5A335253)                 CONFIRMED
//        +0x04  u32  version, accepted range 27-29 inclusive,   CONFIRMED
//                    every shipped file reads 29 (the real loader
//                    rejects anything outside the range with its
//                    own error string, so this reader does too)
//        +0x08  --   documented by spec-zone-data-format.md Sec4 (the
//                    disassembly pass) as where the loader STORES the
//                    already-parsed material block's in-memory pointer -
//                    i.e. a RUNTIME field the loader writes after
//                    construction, not a value read from the shipped
//                    file. NOT read by this parser; see notes below.
//        +0x0C  f32[3] world-space origin the record array's position
//                    words (record +0/+2/+4) are relative to - RESOLVED;
//                    previously "unaccounted... OPEN, not read" (that
//                    comment predated spec-world-streaming.md Sec10.7's
//                    finding). Zero in every file with zero records,
//                    non-zero in every file WITH records (measured over
//                    the 1,265 tile/sub-area/activity-zone headers of
//                    sr3_city_0/1 - NOT enforced by parse() as a hard
//                    rule, since a file legitimately empty of records
//                    reads zero here too).      CONFIRMED - disassembly
//                    (used as the translation) + empirical.
//        +0x18  u32  runtime pointer slot: fixed up to the record array
//                    at load time, "zero on disk in 2,971/2,971" -        CONFIRMED
//                    read and exposed AS DIAGNOSTIC DATA (expected zero),
//                    never required to be zero by parse() itself, since a
//                    future sample being nonzero would be a finding, not
//                    necessarily a parse failure.
//        +0x1C  u16  record count (0 .. 3,701 observed)                  CONFIRMED
//        +0x1E  u16  zone type: selects the engine's own container kind
//                    the zone registers as - RESOLVED, spec-world-
//                    streaming.md Sec10.7 bullet 1 (see ZoneContainerKind
//                    / ContainerKindForZoneType() below). 2 and 3
//                    dominate (1,194 files each), matching the "Zone" and
//                    "Level Always Loaded" buckets respectively.  CONFIRMED
//        +0x20..+0x3F  unaccounted header bytes                          OPEN
//        +0x40  --   end of the fixed header; align to 4 (a no-op here -
//                    +0x40 is already a multiple of 4), then
//                    `record count x 14` bytes of opaque records.
//
//      The 14-byte record's OWN internal layout is explicitly measured-
//      not-resolved by spec-ctorless-types.md Sec5.1 (floats ruled out;
//      several sub-fields look hash-like/high-entropy; +12 is the only
//      bounded, index-shaped field) - so records are surfaced here as raw
//      14-byte blobs, not decoded fields, exactly this project's standing
//      practice for a structure whose existence is CONFIRMED but whose
//      interior is OPEN (mirrors sr3geometry::GeometryBlock's arrays 1/2/
//      3/5, sr3rig's attachment table, etc.).
//
//      spec-world-streaming.md Sec10.7 bullet 3 (the hN packing routine's
//      OWN consumption of this record) resolves three of those five
//      leading bytes without contradicting Sec5.1's "not resolved from
//      the header alone" verdict for the record as a whole:
//        +0  s16  position.x, 1/64 m per unit (2^-6)      CONFIRMED
//        +2  s16  position.y, 1/64 m per unit (2^-6)      CONFIRMED
//        +4  s16  position.z, 1/64 m per unit (2^-6)      CONFIRMED
//        +6  s16  scaled by 2^-12; a rotation angle (radians) fits the
//                 zero-heavy distribution better than a scale factor,
//                 but that reading is NOT established        HYPOTHESIS
//        +8  --   not read by the hN packing consumer                OPEN
//        +10 --   not read by the hN packing consumer                OPEN
//        +12 u16  name index into the SOURCE ZONE FILE's own level-mesh
//                 name list - NOT the shared material-block name table
//                 this reader already exposes, and not resolvable to an
//                 actual name from the header alone (Sec10.7's own OPEN
//                 note). Field's existence/boundedness CONFIRMED; the
//                 index's TARGET is OPEN.
//      The free functions RecordPosition() / RecordNameIndex() /
//      RecordFieldSixRaw() / RecordFieldSixAsHypothesizedRadians() below
//      expose exactly these three fields (+6 as BOTH a raw value and a
//      clearly separate hypothesis helper, never silently only the
//      guess); +8/+10 stay unexposed beyond the raw ZoneHeaderRecord
//      bytes that already existed, per the OPEN status above.
//
// WHAT THIS READER DOES NOT ATTEMPT: the separate, much larger `.czn_pc`
// file's own top-level typed-record chain (`{id, length}`, high bit set on
// id) is explicitly HYPOTHESIS-unconfirmed in spec-zone-data-format.md
// Sec2/Sec3 and a CONFIRMED refuted uniform-chunk-walk model (Sec2's own
// table: 0/2,971 files land exactly on EOF under any of three alignments
// tried) - so there is no reader for it here, or anywhere in this project,
// deliberately. See zone_geometry.h for the one part of `.czn_pc` this
// project CAN read: its embedded Mesh sub-blocks, located independently of
// that unresolved top-level chain.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sr3geometry/material_block.h"
#include "sr3zone/errors.h"
#include "vpp/byte_view.h"

namespace sr3zone {

using vpp::ByteView;

constexpr uint32_t kSR3ZMagic = 0x5A335253u; // 'SR3Z', CONFIRMED (spec-ctorless-types.md Sec5)
constexpr uint32_t kSR3ZVersionMin = 27;      // inclusive, CONFIRMED - the real loader's own accepted range
constexpr uint32_t kSR3ZVersionMax = 29;      // inclusive; every shipped file reads exactly 29
constexpr size_t kSR3ZFixedHeaderSize = 0x40; // CONFIRMED - end of the fixed header, start of the record array
constexpr size_t kSR3ZRecordSize = 14;        // CONFIRMED (exact-size replay, 2,971/2,971); interior OPEN

// One opaque 14-byte `SR3Z` record (spec-ctorless-types.md Sec5.1). Byte 12
// (index 12 here) is the record's one bounded, index-shaped field per the
// spec's own profiling; every other byte's meaning is OPEN. Surfaced raw
// rather than field-decoded, for the same reason sr3geometry::GeometryBlock
// exposes its unresolved arrays as raw bytes rather than guessed structs.
using ZoneHeaderRecord = std::array<uint8_t, kSR3ZRecordSize>;

// Record field accessors (spec-world-streaming.md Sec10.7 bullet 3 - the
// hN packing routine's own consumption of this record; see the file-level
// comment above for the full field table). Free functions, not methods,
// since ZoneHeaderRecord is a plain std::array alias, matching how this
// project already treats other raw-bytes-typedef structures.

// +0, +2, +4: s16 position words, 1/64 m per unit (2^-6), in that axis
// order - NOT yet added to ZoneHeader::headerOrigin(); the caller adds it,
// since a record and its header travel separately in this API (a
// ZoneHeaderRecord carries no back-reference to the ZoneHeader it came
// from). CONFIRMED, spec-world-streaming.md Sec10.7 bullet 3 (validated:
// record positions + header origin reproduce the 320x280 m tile lattice,
// Sec10.6(g)).
std::array<float, 3> RecordPosition(const ZoneHeaderRecord& record);

// +12: u16 name index into the SOURCE ZONE FILE's own level-mesh name
// list. NOT an index into this reader's own MaterialBlock name table (that
// table holds textures/`.fmeshx` names, not level-mesh names - spec-
// terrain-format.md Sec2), and not resolvable to an actual name from the
// header alone (Sec10.7's own OPEN note) - exposed as a raw index only,
// deliberately no name-resolution attempt is made here.
uint16_t RecordNameIndex(const ZoneHeaderRecord& record);

// +6, raw: s16, no scaling applied. Field's existence/boundedness is
// CONFIRMED; its meaning is not - see RecordFieldSixAsHypothesizedRadians()
// for the untested interpretation, exposed separately so a caller is never
// silently handed only the guess.
int16_t RecordFieldSixRaw(const ZoneHeaderRecord& record);

// +6 scaled by 2^-12, AS IF it were a rotation angle in radians - a
// reading spec-world-streaming.md Sec10.7 bullet 3 states fits the
// zero-heavy distribution better than a scale factor, but explicitly does
// NOT establish. HYPOTHESIS - unconfirmed; use RecordFieldSixRaw() if the
// scaled/rotation reading is not wanted.
float RecordFieldSixAsHypothesizedRadians(const ZoneHeaderRecord& record);

// Container kinds the SR3Z +0x1E zone-type selector can resolve to.
// Values equal the engine's own container-kind ids (the same ids
// sr3asm::ContainerRecord::containerKind carries as a raw uint8_t off the
// manifest's table 3 - not redeclared there; this is an independent
// free function over the same id space, per this task's boundary that
// include/sr3asm/manifest.h stays untouched).
enum class ZoneContainerKind : uint8_t {
    LevelAlwaysLoaded = 0x1B, // 27
    Zone              = 0x1D, // 29 - the switch's default/"anything else" bucket
    InteriorZone      = 0x1F, // 31
    LargeInteriorZone = 0x20, // 32
    Mission           = 0x21, // 33
    LargeMission      = 0x22, // 34
};

// Resolves SR3Z +0x1E's zone-type selector to the container kind the
// engine's own registration switch chooses (spec-world-streaming.md
// Sec10.7 bullet 1, quoted precisely from the switch's own case list):
//   {1, 3, 8, 10, 11, 13} -> LevelAlwaysLoaded
//   {5, 6}                -> Mission
//   7                     -> InteriorZone
//   9                     -> LargeInteriorZone
//   12                    -> LargeMission
//   anything else (incl. 2, the common case) -> Zone
// Case value 11 is the only one never observed over the 2,971/2,971
// shipped population (0 exceptions to this mapping) - modelled here as
// part of the LevelAlwaysLoaded case per the switch's own structure
// (per the spec's exact case list above), not special-cased as an error;
// the spec reports it as an unobserved case value, not a rejected one.
// [CONFIRMED - disassembly + empirical, spec-world-streaming.md Sec10.7.]
ZoneContainerKind ContainerKindForZoneType(uint16_t zoneType);

class ZoneHeader {
public:
    // Parses a `.czh_pc` buffer in full: the shared material block, the
    // mandatory pad, then the `SR3Z` block. Throws FormatError if:
    //   - the material block itself fails to validate (propagates
    //     sr3geometry::FormatError's own message, not re-thrown as this
    //     type, so the underlying cause is never hidden);
    //   - the `SR3Z` magic does not match at the computed pad offset;
    //   - the version is outside the CONFIRMED 27-29 accepted range;
    //   - the fixed header plus `record count x 14` bytes does not land
    //     EXACTLY on the end of `content` - the population-level exact-
    //     size invariant spec-ctorless-types.md Sec5 reports as holding
    //     2,971/2,971 with zero residual in every file that has one.
    static ZoneHeader parse(ByteView content);

    const sr3geometry::MaterialBlock& materialBlock() const { return materialBlock_; }

    // Absolute offset of the `SR3Z` magic within the buffer passed to
    // parse() - i.e. material_block.totalSize, pad-adjusted.
    size_t sr3zOffset() const { return sr3zOffset_; }

    uint32_t version() const { return version_; }

    // +0x18. Diagnostic only - see the file-level comment. Expected zero
    // on every shipped file; not enforced by parse().
    uint32_t runtimePointerSlotOnDisk() const { return runtimePointerSlotOnDisk_; }

    // +0x1C. CONFIRMED field, OPEN meaning beyond "record count".
    uint16_t recordCount() const { return recordCount_; }

    // +0x1E. The zone-type selector - RESOLVED, spec-world-streaming.md
    // Sec10.7 bullet 1: it chooses the engine's own container kind for
    // this zone. Kept as the raw field for backward compatibility; see
    // zoneContainerKind() / ContainerKindForZoneType() for the resolved,
    // typed reading of the same field.
    uint16_t fieldAt0x1E() const { return fieldAt0x1E_; }

    // Typed reading of fieldAt0x1E() - see ContainerKindForZoneType() for
    // the exact case mapping and its spec citation.
    ZoneContainerKind zoneContainerKind() const { return ContainerKindForZoneType(fieldAt0x1E_); }

    // +0x0C..+0x17: three f32, the world-space origin the record array's
    // position words (RecordPosition()) are relative to - RESOLVED,
    // spec-world-streaming.md Sec10.7 bullet 2. Zero on every file with
    // zero records; non-zero on every file WITH records, over the
    // 1,265-file sr3_city_0/1 subset the spec measured this against - NOT
    // enforced by parse() as a hard rule (a legitimately empty file reads
    // zero here too, that is not itself an error).
    const std::array<float, 3>& headerOrigin() const { return headerOrigin_; }

    // recordCount() opaque 14-byte records, in file order. Empty when
    // recordCount() == 0 (2,342 / 2,971 shipped files, per the spec).
    const std::vector<ZoneHeaderRecord>& records() const { return records_; }

private:
    sr3geometry::MaterialBlock materialBlock_;
    size_t sr3zOffset_ = 0;
    uint32_t version_ = 0;
    uint32_t runtimePointerSlotOnDisk_ = 0;
    uint16_t recordCount_ = 0;
    uint16_t fieldAt0x1E_ = 0;
    std::array<float, 3> headerOrigin_{0.0f, 0.0f, 0.0f};
    std::vector<ZoneHeaderRecord> records_;
};

} // namespace sr3zone
