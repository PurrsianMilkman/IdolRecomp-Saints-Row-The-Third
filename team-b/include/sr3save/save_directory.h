#pragma once

// Reader for savedir.sr3d_pc (spec-save-format.md Sec2 + Sec6.1 + Sec8).
// A fixed 608-byte flat buffer, outside the .vpp_pc/.str2_pc container system
// entirely (Sec1) - reuses only this project's generic ByteView utility.
//
//   0x000  u32   CRC-32 of the 600-byte slot table (init 0, no final XOR)   Sec6.1
//   0x004  u32   active slot count
//   0x008  24 x 25-byte slot records                                        Sec2
//                 +0x00      active flag (0 = free)
//                 +0x01..0x18  ONE bit-packed 24-byte save summary          Sec8
//
// The three "8-byte Field A/B/C" of the original Sec2.2 were an artefact of
// reading a bit stream as byte fields (superseded by Sec8); they are gone.
//
// Hash policy: the game DOES verify the directory hash on load (a mismatch is
// error 4 and the whole directory is then treated as empty - Sec6.1), so
// parse() enforces it by default. The snapshot's hash, by contrast, is never
// verified by the game (Sec11.1) - see save_snapshot.h.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sr3save/errors.h"
#include "vpp/byte_view.h"

namespace sr3save {

using vpp::ByteView;

constexpr size_t kSaveDirectorySize = 0x260; // 608 bytes, CONFIRMED exact (Sec2.1)
constexpr size_t kSlotCount = 24;            // CONFIRMED (Sec2)
constexpr size_t kSlotRecordStride = 25;     // CONFIRMED (Sec2.2)
constexpr size_t kSlotTableOffset = 0x008;   // CONFIRMED (Sec2.1)
constexpr size_t kSlotSummarySize = 24;      // bytes after the active flag (Sec8)

// Decoded 24-byte slot summary (Sec8.2). Field numbers are the "#" column of the
// spec's table (write order). Rows 2 and 4 come from live game state with no
// snapshot equivalent and their meaning is OPEN - they are exposed under
// deliberately non-committal names.
struct SlotSummary {
    uint32_t reserved1 = 0;            // #1  6 bits, always 0
    uint32_t liveCountA_Open = 0;      // #2  7 bits, live-world list count (0-127); meaning OPEN
    uint32_t flaggedRecordCount = 0;   // #3  9 bits, number of the snapshot's 16-byte region records whose
                                       //     flag bit 0 is set (Sec3/Sec6.5; the records are the 154
                                       //     city-takeover regions' "controlled" bit, Sec12.4.1)
    uint32_t liveCounterB_Open = 0;    // #4  5 bits, second live-world list count (0-31); meaning OPEN
    uint32_t playMinutes = 0;          // #5  17 bits, trunc(snapshot 0x0A0 seconds / 60.0)
    uint32_t difficulty = 0;           // #6  2 bits, snapshot 0x2B64 low 2 bits (0 casual, 1 normal, 2 hardcore)
    bool cheatsUsed = false;           // #7  1 bit, snapshot 0x0C8 bit 0 (Sec10.3)
    bool autosave = false;             // #8  1 bit, caller-supplied; 1 in slot 0 of every real directory
    uint32_t completionPercent = 0;    // #9  7 bits, snapshot 0x088 low 7 bits
    uint32_t formatVersion = 0;        // #10 8 bits, snapshot 0x008 (94)
    uint32_t locationLabelId = 0;      // #11 7 bits, snapshot 0x084 (label index; id-to-name mapping not decoded)
    uint32_t reserved12 = 0;           // #12 5 bits, always 0
    uint32_t month = 0;                // #13 4 bits  (1-12)
    uint32_t day = 0;                  // #14 5 bits
    uint32_t year2 = 0;                // #15 7 bits, 2-digit year
    uint32_t hour = 0;                 // #16 5 bits
    uint32_t minute = 0;               // #17 6 bits
    uint32_t second = 0;               // #18 6 bits
    int32_t cashDiv100 = 0;            // #19 32 bits, snapshot 0x4A70 / 100 (signed integer division)
    uint32_t level = 0;                // #20 6 bits, snapshot 0x4420 low 6 bits
    uint32_t regionRecordCount = 0;    // #21 9 bits, snapshot 0x113B8 low 9 bits
    uint32_t pointsInLevel = 0;        // #22 32 bits raw, snapshot 0x441C

    bool operator==(const SlotSummary& o) const {
        return reserved1 == o.reserved1 && liveCountA_Open == o.liveCountA_Open &&
               flaggedRecordCount == o.flaggedRecordCount && liveCounterB_Open == o.liveCounterB_Open &&
               playMinutes == o.playMinutes && difficulty == o.difficulty && cheatsUsed == o.cheatsUsed &&
               autosave == o.autosave && completionPercent == o.completionPercent && formatVersion == o.formatVersion &&
               locationLabelId == o.locationLabelId && reserved12 == o.reserved12 && month == o.month &&
               day == o.day && year2 == o.year2 && hour == o.hour && minute == o.minute && second == o.second &&
               cashDiv100 == o.cashDiv100 && level == o.level && regionRecordCount == o.regionRecordCount &&
               pointsInLevel == o.pointsInLevel;
    }
};

// One (byte, bit) position, both relative to the first summary byte.
struct BitPos {
    int byte = 0;
    int bit = 0;
    bool operator==(const BitPos& o) const { return byte == o.byte && bit == o.bit; }
};

constexpr size_t kSlotSummaryFieldCount = 22;

// Bit positions of each of the 22 fields, LSB of the value first, transcribed
// from the spec's Sec8.2 "Where" column.
const std::array<std::vector<BitPos>, kSlotSummaryFieldCount>& slotSummaryBitPositionsFromSpecTable();

// The same positions, derived independently by simulating the writer rule of
// Sec8.1 (widths in write order; an n-bit field stores its n div 8 whole low
// bytes byte-aligned at the next unused byte and the remaining n mod 8 low
// bits continue the partially filled byte; the final field is a raw dword).
// The two derivations must agree (checked by tests); they exist so that the
// spec's prose rule and its table cross-check each other.
std::array<std::vector<BitPos>, kSlotSummaryFieldCount> slotSummaryBitPositionsFromWriterRule();

// Decodes / encodes the 24 summary bytes (encode is for a from-spec writer and
// for building synthetic fixtures).
SlotSummary decodeSlotSummary(const uint8_t* bytes24);
std::array<uint8_t, kSlotSummarySize> encodeSlotSummary(const SlotSummary& s);

// One 25-byte slot record.
struct SlotRecord {
    bool active = false;                        // +0x00, CONFIRMED - disassembly + empirical
    std::array<uint8_t, kSlotSummarySize> summaryRaw{}; // +0x01..+0x18 verbatim
    SlotSummary summary;                        // decoded (meaningful for occupied slots; zero bytes decode to zeros)
};

// Parsed savedir.sr3d_pc: 24 slot records (array index == the NN of that
// slot's sr3save_NN.sr3s_pc, Sec2.2/Sec8.3) plus the header fields.
class SaveDirectory {
public:
    enum class HashPolicy {
        Enforce, // throw FormatError on a hash mismatch (what the game does: error 4, directory treated as empty)
        Ignore   // parse regardless; inspect hashValid() (for tools that want to show a tampered directory)
    };

    // Parses `bytes`, which must be exactly kSaveDirectorySize bytes (Sec2.1).
    // Also enforces the CONFIRMED active-count invariant (header count must equal
    // the number of records with a non-zero active flag - error 5 in the game) and,
    // under HashPolicy::Enforce, the CRC of Sec6.1 (error 4 in the game).
    // Throws FormatError on violation.
    static SaveDirectory parse(ByteView bytes, HashPolicy policy = HashPolicy::Enforce);

    // Hash a directory file must carry (Sec6.1): init-0 no-final-XOR CRC-32
    // over file[0x008, 0x260). A from-spec writer must set this.
    static uint32_t computeHash(ByteView bytes);

    uint32_t hashStored() const { return checksumRaw_; }
    uint32_t hashComputed() const { return hashComputed_; }
    bool hashValid() const { return checksumRaw_ == hashComputed_; }
    // Back-compat name for hashStored().
    uint32_t checksumRaw() const { return checksumRaw_; }

    uint32_t activeSlotCount() const { return activeSlotCount_; }
    const std::array<SlotRecord, kSlotCount>& slots() const { return slots_; }

    // Indices of occupied slots, ascending (each is the NN for that slot's
    // sr3save_NN.sr3s_pc filename).
    std::vector<int> occupiedSlotIndices() const;

private:
    uint32_t checksumRaw_ = 0;
    uint32_t hashComputed_ = 0;
    uint32_t activeSlotCount_ = 0;
    std::array<SlotRecord, kSlotCount> slots_{};
};

} // namespace sr3save
