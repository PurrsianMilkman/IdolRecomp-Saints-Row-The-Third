#pragma once

// Reader for .asm_pc manifest files, implemented from spec-asm-format.md
// sections 6-10 (the disassembly-derived, population-validated layout; the
// spec's own sections 1-5 are the superseded black-box pass and are NOT what
// this reader follows). Version 11 only - the only version present in any
// shipped file (805/805).
//
// A manifest is:
//   8-byte header        magic u32 (0xBEEFFEED), version u16, record_count u16
//   3 tables             u32 count, then count x { lpstr name; u8 file_id }
//                        (1 = memory pools, 2 = resource types,
//                         3 = container kinds; spec 7.2 / 8)
//   record_count records (spec 7.3):
//       lpstr name; u8 container_kind; u16 record_flags; s16 entry_count;
//       u32 header_region_size; lpstr source_name; u32 extra_len;
//       u8 extra[extra_len]; u32 payload_length;
//       entry_count x { u32 primary, u32 secondary }   (size table)
//       entry_count x entry                            (spec 7.4)
//   entry: lpstr name + exactly 13 bytes:
//       u8 type_id, u8 pool_id, u8 entry_flags, u8 variant_select,
//       u32 primary_size, u32 secondary_size, u8 alloc_group
// (`lpstr` = u16 length + that many bytes, no terminator; all integers
// little-endian.)
//
// Strictness relative to the engine (spec 6.2): the engine never compares
// the magic and never checks for end-of-file after the last record. This
// reader DOES reject a wrong magic and a version other than 11 (those are
// caller-visible sanity checks, and no shipped file trips them), but it
// does NOT throw on trailing bytes - it reports them through
// trailingBytes() so a validation harness can apply the stricter
// exact-consumption gate of spec 9.2 itself.
//
// Names: the engine keeps at most 63 characters of a name and discards the
// rest (spec 6.1). This reader keeps every character it reads; the longest
// name in the shipped population is 41 characters, so the two agree there.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3asm/errors.h"
#include "vpp/byte_view.h"

namespace sr3asm {

using vpp::ByteView;

constexpr uint32_t kMagic = 0xBEEFFEEDu;       // spec 7.1 (files carry it; the engine never checks it)
constexpr uint16_t kSupportedVersion = 11;      // spec 7.1: 805/805 shipped files
constexpr size_t kHeaderSize = 0x08;            // magic(4) + version(2) + record_count(2)
constexpr size_t kEntryFixedBytes = 13;         // spec 7.4: bytes after an entry's name
constexpr size_t kRecordTailBytes = 19;         // spec 7.3: fixed bytes after `name` when entry_count == 0

// record_flags bits (spec 7.3).
constexpr uint16_t kRecordFlagSizeHintsLive = 0x0080; // header_region_size / payload_length / size table are consumed
constexpr uint16_t kRecordFlagProperty0100 = 0x0100;  // forwarded to container creation as a property flag

// entry_flags bits (spec 7.4).
constexpr uint8_t kEntryFlagPaired = 0x04;     // entry has a g-side secondary file
constexpr uint8_t kEntryFlagTolerant = 0x40;   // failed allocation drops the entry (HIGH CONFIDENCE reading)
constexpr uint8_t kEntryFlagOverride = 0x20;   // role OPEN

// alloc_group value meaning "allocated alone" (spec 7.4).
constexpr uint8_t kAllocGroupAlone = 0xFF;

// One row of one of the three tables (spec 7.2): a name and its file id.
struct LookupEntry {
    std::string name;
    uint8_t id = 0;
};

// One of the three per-manifest name lists (spec 7.2 / 8). Ids need not be
// contiguous. The engine treats them as per-parse id-translation lists; in
// shipped data the file ids equal the engine ids (spec 8.2).
struct LookupTable {
    std::vector<LookupEntry> entries;

    // Name listed for `id`, or nullptr when this table does not list it.
    // If an id were listed twice the first listing wins.
    const std::string* findName(uint8_t id) const;
};

// The (primary, secondary) pair of the size table (spec 7.3). In the
// shipped population every pair equals the entry's own trailer sizes
// (spec 9.3, 390,134/390,134); they are kept separately so a caller can
// check that rather than assume it.
struct SizePair {
    uint32_t primary = 0;
    uint32_t secondary = 0;
};

// One entry: a name plus exactly 13 bytes (spec 7.4).
struct ManifestEntry {
    std::string name;
    uint8_t typeId = 0;         // file id in table 2 (= the engine's resource-type id in shipped data)
    uint8_t poolId = 0;         // file id in table 1; 0 = inherit the container kind's default pool
    uint8_t entryFlags = 0;     // see kEntryFlag*; only 0x04, 0x40, 0x20 occur
    uint8_t variantSelect = 0;  // low 2 bits used by the engine; 0xFF only on type 39
    uint32_t primarySize = 0;   // c-side size, exact unpadded byte count
    uint32_t secondarySize = 0; // g-side size (0 when unpaired or g-file is empty)
    uint8_t allocGroup = 0;     // 0xFF = allocated alone

    bool paired() const { return (entryFlags & kEntryFlagPaired) != 0; }
};

// One container record (spec 7.3).
struct ContainerRecord {
    std::string name;               // the container's name; `<name>.str2_pc` is normally its backing file (spec 9.4)
    uint8_t containerKind = 0;      // file id in table 3
    uint16_t recordFlags = 0;       // see kRecordFlag*
    int16_t entryCount = 0;         // signed on disk (max 3,820 in the population)
    uint32_t headerRegionSize = 0;  // = the sibling .str2_pc's payload_start when the 0x0080 flag is set, else 0
    std::string sourceName;         // empty in 5,149 of 8,362 shipped records
    std::vector<uint8_t> extra;     // opaque blob; empty in 8,362/8,362 shipped records (role OPEN)
    uint32_t payloadLength = 0;     // = the sibling .str2_pc's header field 0x168 (0 for the all-raw sentinel), else 0
    std::vector<SizePair> sizeTable;      // entryCount pairs, always present at version 11
    std::vector<ManifestEntry> entries;   // entryCount entries, directory order of the primary files

    bool sizeHintsLive() const { return (recordFlags & kRecordFlagSizeHintsLive) != 0; }
};

class AsmManifest {
public:
    // Parses `bytes` as a version-11 manifest. Throws FormatError if the
    // magic is wrong, the version is not 11, or any read (the 8-byte header,
    // a table, a record, an entry) would run past the end of `bytes`, or if
    // a record declares a negative entry_count. Trailing bytes after the
    // last record are NOT an error (the engine does not check either); see
    // trailingBytes().
    static AsmManifest parse(ByteView bytes);

    uint16_t version() const { return version_; }

    // Header field at 0x06: the loop bound of the record reader (spec 7.1).
    // parse() reads exactly this many records or throws, so it always equals
    // records().size() on a successfully parsed manifest.
    uint16_t declaredRecordCount() const { return declaredRecordCount_; }

    // Tables 1, 2, 3 in file order: memory pools, resource types, container kinds.
    const std::array<LookupTable, 3>& fixedTables() const { return fixedTables_; }
    const LookupTable& poolTable() const { return fixedTables_[0]; }
    const LookupTable& typeTable() const { return fixedTables_[1]; }
    const LookupTable& kindTable() const { return fixedTables_[2]; }

    const std::vector<ContainerRecord>& records() const { return records_; }

    // Number of bytes the parse consumed, from offset 0 through the last
    // byte of the last record.
    size_t bytesConsumed() const { return bytesConsumed_; }

    // Bytes of the input left over after the last record (0 on every
    // shipped file - spec 9.2's exact-consumption gate).
    size_t trailingBytes() const { return trailingBytes_; }

    // Always true after a successful parse() (a short record set throws
    // instead). Kept for source compatibility with the previous reader.
    bool recordsFullyParsed() const { return records_.size() == declaredRecordCount_; }

    // Total number of entries over all records.
    size_t totalEntries() const;

private:
    uint16_t version_ = 0;
    uint16_t declaredRecordCount_ = 0;
    std::array<LookupTable, 3> fixedTables_{};
    std::vector<ContainerRecord> records_;
    size_t bytesConsumed_ = 0;
    size_t trailingBytes_ = 0;
};

} // namespace sr3asm
