#pragma once

// Struct layout, offsets, and constants in this file are transcribed
// directly from spec-vpp-container.md (the clean-room spec this whole
// library is built from — see that document for full evidence and
// confidence ratings behind every field). Only fields the spec rates
// CONFIRMED or HIGH CONFIDENCE are given real, typed meaning here. Fields
// still rated OPEN/UNKNOWN or HYPOTHESIS at spec time are kept only as
// opaque raw values (clearly labeled) and are never used to drive parsing
// decisions or control flow.

#include <cstdint>
#include <stdexcept>
#include <string>

#include "vpp/byte_view.h"

namespace vpp {

// spec §1, offset 0x000. On-disk bytes are CE 0A 89 51 (little-endian).
// [CONFIRMED - disassembly + empirical]
constexpr uint32_t kMagic = 0x51890ACEu;

// spec §1, offset 0x004. The loader accepts 5-6 inclusive; only 6 was ever
// observed on disk, so version-5 behavior is UNTESTED, not just
// unimplemented. [CONFIRMED - disassembly]
constexpr uint32_t kVersionMin = 5;
constexpr uint32_t kVersionMax = 6;

// spec §1.1: every region is block-quantized to this size, chained one
// after another (dir_base -> name_base -> payload_start). [CONFIRMED -
// empirical, cross-checked across 6 archives up to 110 entries]
constexpr size_t kBlockSize = 0x800;

constexpr size_t kDirectoryOffset = 0x800;   // spec §1.1/§2: dir_base = container_base + 0x800, fixed
constexpr size_t kDirectoryEntryStride = 24; // spec §2, confirmed stride

// spec §2/§3.1: directory-entry field +0x10 reads this sentinel when the
// entry is stored raw/uncompressed rather than a real compressed size.
constexpr uint32_t kRawSentinel = 0xFFFFFFFFu;

// spec §2 (+0x00 row, CONFIRMED - disassembly, this pass): directory-entry
// field +0x00 reads this sentinel when the entry has no name at all,
// rather than a real byte offset into the filename table. Numerically
// identical to kRawSentinel but semantically distinct (different field,
// different meaning) - kept as its own named constant for clarity.
constexpr uint32_t kNoNameSentinel = 0xFFFFFFFFu;

// spec §1 (0x14C row) / §3.2 / §3.3: archive-level flags bit 0x2, CONFIRMED
// via disassembly (Ghidra decompilation) as the mode-(a)-vs-(b) compression
// grouping switch: clear = mode (a), independent per-entry zlib streams;
// set = mode (b), one shared stream spanning multiple entries (spec
// §3.2b). This is a real traced mechanism (the loader's per-entry-position
// bookkeeping routine only runs at all when this bit is clear), not a
// correlation guess.
constexpr uint32_t kSharedStreamModeFlag = 0x2u;

// spec §2 (+0x04 row) / §3.3 and HANDOFF §9.78: archive-level flags bit 0x1
// selects which size the loader's per-entry slot accumulation uses. Set: a
// COMPRESSED entry's slot is round_up(+0x10 compressed size, 0x800); clear:
// every slot is round_up(+0x0C, 0x800). That accumulation is the real physical
// layout of a mode-(a) container's compressed streams (4,272/4,272 entries
// across all ten shipped mode-(a) containers, all with this bit set).
constexpr uint32_t kSlotsByCompressedSizeFlag = 0x1u;

// Rounds `value` up to the next multiple of `kBlockSize` (spec §1.1's
// chained block-quantization rule, and spec §5.1's per-entry padding
// rule: round_up(uncompressed_size, 0x800)).
constexpr size_t roundUpBlock(size_t value) {
    return (value + (kBlockSize - 1)) / kBlockSize * kBlockSize;
}

// Thrown for anything the spec marks as a hard, CONFIRMED validation rule
// (magic, version range) or an invariant that held in every sample checked
// (directory-table-size == entry_count * 24). Never thrown on account of a
// field the spec marks OPEN/UNKNOWN/HYPOTHESIS - those are simply not
// interpreted, not treated as validation failures.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

// Fixed archive header (spec §1, offsets 0x000-0x17F).
struct Header {
    uint32_t version = 0;            // 0x004, CONFIRMED (range-checked 5-6)
    uint32_t flagsRaw = 0;           // 0x14C, CONFIRMED field exists/loaded. Bit 0x2 is CONFIRMED (disassembly, spec §3.3) as the mode-(a)-vs-(b) switch - see isSharedStreamMode() below. Bit 0x1 (kSlotsByCompressedSizeFlag) selects which size field the loader's per-entry slot accumulation uses - and that accumulation IS the physical position of a mode-(a) compressed stream (HANDOFF §9.78; the spec's earlier "write-only, not useful" reading of +0x04 was wrong about its usefulness). Other bits not individually confirmed - do not branch on them.
    uint32_t entryCount = 0;         // 0x154, CONFIRMED
    uint32_t totalSize = 0;          // 0x158, HIGH CONFIDENCE (inferred, 8/8 samples)
    uint32_t directoryTableSize = 0; // 0x15C, CONFIRMED (== entryCount * 24, validated below)
    uint32_t nameTableSize = 0;      // 0x160, HIGH CONFIDENCE (inferred)

    // OPEN/UNKNOWN fields - kept only for diagnostics, never interpreted
    // or used to drive parsing (spec §1, §6).
    uint32_t field_0x150_unknown = 0; // OPEN/UNKNOWN
    uint32_t field_0x164_unknown = 0; // OPEN/UNKNOWN

    // 0x168 is CONFIRMED as a cached copy of (totalSize - payloadStart()),
    // or the 0xFFFFFFFF sentinel when every entry in this container is
    // stored raw (spec §1). Per the spec's own practical note, it never
    // needs to be read: payloadStart() below is computed directly from
    // the confirmed block-chaining formula (spec §1.1) and is correct in
    // both cases, so this field is intentionally not stored or used here.

    // spec §1.1 (corrected): dir_base -> name_base -> payload_start is a
    // chain of block-quantized regions, NOT fixed offsets. An earlier
    // draft of this spec (and an earlier revision of this parser) assumed
    // the filename table always starts at a fixed 0x1000 - that only
    // happened to hold for archives with <= 85 entries and is wrong in
    // general (confirmed via a 110-entry sample). Always compute these,
    // never hard-code them.
    size_t directoryBase() const { return kDirectoryOffset; }
    size_t nameBase() const {
        return directoryBase() + roundUpBlock(directoryTableSize);
    }
    size_t payloadStart() const {
        return nameBase() + roundUpBlock(nameTableSize);
    }

    // spec §3.2/§3.3, CONFIRMED via disassembly, and now the authoritative
    // switch (HANDOFF §9.78: 6,239/6,239 shipped mode-(b) containers decode
    // as ONE stream from payloadStart whose length is exactly the sum of the
    // compressed entries' +0x0C, each entry a slice at its +0x08): true if
    // this container's compressed entries share a single zlib stream rather
    // than each having an independent stream. Applies to the container as a
    // whole, not per-entry.
    bool isSharedStreamMode() const { return (flagsRaw & kSharedStreamModeFlag) != 0; }

    // Parses and validates the fixed header from `container`, which must
    // start at byte 0 of *this* container (for a nested .str2_pc, that
    // means the sub-view starting at that entry's own payload offset, not
    // the parent file's byte 0 - spec §1 "for a nested .str2_pc, this is
    // the start of that sub-package's own data").
    //
    // Throws FormatError if the magic or version fail the CONFIRMED
    // validation rules, or if the directory-table-size invariant doesn't
    // hold (spec §1/§2).
    static Header parse(ByteView container);
};

// One 24-byte directory entry (spec §2, fully resolved). All five fields
// are transcribed as CONFIRMED.
struct DirectoryEntry {
    uint32_t nameOffset = 0; // +0x00, CONFIRMED: byte offset of this entry's name, relative to Header::nameBase() - OR kNoNameSentinel (0xFFFFFFFF) meaning this entry has no name at all (spec §2/§3.3, confirmed via disassembly including this exact edge case). A from-disk parser must check for the sentinel before treating this as an offset - it is not a valid byte offset.
    uint32_t field_0x04_unknown = 0; // +0x04, CONFIRMED meaning (spec §3.3) but not on-disk data: a loader-internal, write-only scratch position computed and stored here at load time (never read back for seeking). Always zero on disk. Explicitly not a substitute for +0x08, and never interpreted here.
    uint32_t dataOffset = 0; // +0x08, CONFIRMED: byte offset of this entry's payload, relative to Header::payloadStart()
    uint32_t uncompressedSize = 0; // +0x0C, CONFIRMED: decompressed size in bytes (also this entry's on-disk padded-slot size, rounded up to kBlockSize - spec §5.1)
    uint32_t compressedSizeOrSentinel = 0; // +0x10, CONFIRMED signal: kRawSentinel (0xFFFFFFFF) means stored raw; otherwise the per-entry compression flag AND (for the "mode (a)" majority - spec §3.2) the on-disk compressed byte length
    // +0x14 is runtime-only: zero on disk in every sample, overwritten at
    // load time with an in-memory back-pointer (spec §2). Not stored -
    // not meaningful on-disk data.

    static DirectoryEntry parse(ByteView container, size_t entryIndex);
};

} // namespace vpp
