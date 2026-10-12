#pragma once

// Reader for sr3save_NN.sr3s_pc (spec-save-format.md Sec3, Sec6, Sec8-Sec12).
// A flat, fixed-size 108,776-byte (0x1A8E8) buffer - not compressed, not
// chunked, unrelated to the vpp/str2 container format beyond reusing this
// project's generic ByteView utility.
//
// Scope. The file is filled by 32 per-subsystem serializers at fixed absolute
// offsets (Sec6.3-6.4). This reader exposes:
//   - the header (version, build stamp, zone name, handle, date/time, label,
//     completion, clock, play time) and the hash;
//   - the fields whose meaning the spec marks CONFIRMED or HIGH CONFIDENCE:
//     difficulty, respect/level, cash, cheats-used flag, barnstorm/stunt
//     counters, collectibles, the 154 region records (controlled flag), the
//     60-record activity progress table and its 64-bit marker set, the
//     active cheat ids.
// It deliberately does NOT interpret anything the spec marks HYPOTHESIS or
// OPEN (for example 0x0A4's 256-bit set, 0x3F4, 0x4424, the hitman/chop-shop
// flags, the crib "A/B" fields, the wardrobe/outfit/garage bit-packed blobs);
// those bytes stay unparsed. The rest of the file (about 99.5 % is attributed
// to a serializer by Sec6.4; the *contents* are mostly undecoded) is not
// mapped here.
//
// Sec16 (2026-10-08, "Write/Read Symmetry") independently re-confirms this
// reader's/writer's header offsets from the LOAD side, not just the save
// side's own region map (Sec6.3/6.4): the real header load routine
// (0x00B9B530) consumes EXACTLY the two spans the header save routine
// (0x00B9A380) writes - the 20-byte clock struct at 0x08C (kOffsetClockStruct)
// and the 256-bit flag set at 0x0A4, each through its own dedicated
// apply/build routine pair at the same offset on both sides. This project
// still only models the clock struct's trailing float32 sub-field
// (kOffsetClockPlayFloat, 0x09C) and the separate playTimeSeconds word right
// after the struct ends (0x0A0) - no in-game calendar clock and no meaning
// for the flag set are in this project's EngineState scope - so nothing
// changes here; Sec16 confirms the offsets this file already used were
// right, it does not add new fields to populate.
//
// Hash policy: the snapshot hash is written on save and NEVER verified by the
// game on load (Sec11.1, CONFIRMED - static). Consequently parse() does not
// fail on a hash mismatch; hashValid() reports it. The only content gate the
// game applies is the version word at 0x008 == 94 (a different value skips the
// state application silently - Sec11.1); versionSupported() reports that.

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "sr3save/errors.h"
#include "sr3save/save_directory.h"
#include "vpp/byte_view.h"

namespace sr3save {

using vpp::ByteView;

constexpr size_t kSaveSnapshotSize = 108776; // 0x1A8E8, CONFIRMED exact (Sec1, Sec6.3)
constexpr uint32_t kSnapshotFormatVersion = 94; // u32 at 0x008 (Sec3, Sec11.1)

// ---- offsets ---------------------------------------------------------------
constexpr size_t kOffsetChecksum = 0x000;        // CRC-32 (init 0, no final XOR), Sec6.1
constexpr size_t kOffsetFormatVersion = 0x008;   // 94
constexpr size_t kOffsetSecondaryVersion = 0x00C; // 1
constexpr size_t kOffsetBuildStamp = 0x010;      // literal 0x000F0F6E in current builds
constexpr size_t kOffsetLevelName = 0x014;       // NUL-terminated ASCII, capacity to 0x058 (Sec6.4, contiguity)
constexpr size_t kOffsetObjectHandle = 0x058;    // two u32 (Sec6.4 shape; Sec12.7.1 uses it as a follower anchor)
constexpr size_t kOffsetSaveTime = 0x060;        // UTF-16LE "HH:MM:SS", 18 bytes reserved
constexpr size_t kOffsetSaveDate = 0x072;        // UTF-16LE "MM.DD.YY", 18 bytes reserved
constexpr size_t kOffsetLabelIndex = 0x084;      // active-mission index / display-label id (Sec12.10.1, HIGH CONFIDENCE)
constexpr size_t kOffsetCompletionPercent = 0x088; // Sec6.5 (weights CONFIRMED; meaning HIGH CONFIDENCE)
constexpr size_t kOffsetClockStruct = 0x08C;     // 20-byte in-world clock struct copy (Sec6.2; load-side offset re-confirmed Sec16)
constexpr size_t kOffsetClockPlayFloat = 0x09C;  // float32 play-time clock (last dword of the struct)
constexpr size_t kOffsetPlayTimeSeconds = 0x0A0; // u32 whole game-clock SECONDS (Sec6.2, CONFIRMED; immediately after the Sec16-confirmed 20-byte clock struct, NOT part of it)
constexpr size_t kOffsetPlayTimeTicks = kOffsetPlayTimeSeconds; // legacy name; the unit is seconds, not ticks
constexpr size_t kOffsetCheatsUsed = 0x0C8;      // byte, cheats-used flag (Sec10.3, CONFIRMED)
constexpr size_t kOffsetCheatCount = 0x0CC;      // u32 count, then u32 ids at 0x0D0 (Sec9.5)
constexpr size_t kOffsetCheatIds = 0x0D0;
constexpr size_t kCheatIdCapacity = 200;         // by contiguity (Sec9.9)
constexpr size_t kOffsetBarnstorms = 0x3F0;      // u32 barnstorms found (Sec10.3)
constexpr size_t kOffsetStuntJumps = 0x3F8;      // u32 stunt jumps found (Sec10.3)
constexpr size_t kOffsetCollectibleCount = 0x3FC; // u32 count + 80 x 16-byte records from 0x400 (Sec10.3)
constexpr size_t kOffsetCollectibles = 0x400;
constexpr size_t kCollectibleCapacity = 80;
constexpr size_t kOffsetDifficulty = 0x2B64;     // u32 difficulty index 0..2 (Sec6.5, HIGH CONFIDENCE)
constexpr size_t kOffsetActivityTable = 0x2B90;  // 0x308-byte activity progress table (Sec9.3a)
constexpr size_t kActivityTableRecordsOffset = 4;   // within the struct: 64 x 12-byte records
constexpr size_t kActivityTableCountOffset = 0x304; // within the struct: u32 used-record count (60)
constexpr size_t kActivityTableCapacity = 64;
constexpr size_t kOffsetActivityMarkerBits = 0x2E98; // 64-bit set, 60 bits used (Sec9.3b)
constexpr size_t kOffsetStatistics = 0x3340;     // 149 x (value, denominator-stat id): statistic ids 68..216 (Sec12.6.1, CONFIRMED)
constexpr uint32_t kFirstSnapshotStatId = 68;
constexpr size_t kSnapshotStatCount = 149;
constexpr size_t kOffsetGarageCount = 0xB760;    // u32 count, Sec9.2 entry 5
constexpr size_t kOffsetGarageArray = 0xB764;    // 0x70-byte records from here
constexpr size_t kGarageRecordStride = 0x70;
constexpr size_t kGarageCapacity = 152;          // cap 0x98, Sec9.2
constexpr size_t kOffsetGarageNextId = 0xF9E4;   // u32 next record id (Sec9.2)
constexpr size_t kOffsetWardrobeCount = 0x4F6C;  // u32 count, Sec10.1/10.2/10.3
constexpr size_t kOffsetWardrobeArray = 0x4F70;  // 8-byte records from here
constexpr size_t kWardrobeRecordStride = 8;
constexpr size_t kWardrobeCapacity = 2048;       // span size 16384/8 = 2048 (0x4F6C-0x8F6F)
constexpr size_t kOffsetRespectTotal = 0x4418;   // total respect (Sec10.3, CONFIRMED)
constexpr size_t kOffsetRespectInLevel = 0x441C; // respect accumulated inside the current level
constexpr size_t kOffsetPlayerLevel = 0x4420;    // player level 0..50
constexpr size_t kOffsetCash = 0x4A70;           // cash on hand x100, clamped to 2,000,000,000 (Sec10.3)
constexpr size_t kOffsetWeaponInventoryCount = 0x4A74; // u32 count, Sec10.3's own span table row
constexpr size_t kOffsetWeaponInventoryArray = 0x4A78; // 28-byte records from here
constexpr size_t kWeaponInventoryRecordStride = 0x1C;  // 28
constexpr size_t kWeaponInventoryCapacity = 12;        // up to 8 primary + 4 secondary slots
constexpr size_t kOffsetRegionCount = 0x113B8;   // u32 count (154 in 16/16)
constexpr size_t kOffsetRegionArray = 0x113C0;   // 16-byte records; the array starts HERE, not at 0x113C8 (Sec6.5)
constexpr size_t kRegionRecordStride = 16;
constexpr size_t kRegionArrayEnd = 0x11D80;      // entry 25's byte lives here: room for 156 records
constexpr size_t kRegionRecordCapacity = (kRegionArrayEnd - kOffsetRegionArray) / kRegionRecordStride; // 156

// Legacy names (spec revisions before Sec6.5/Sec12.4.1 called the region records
// "activity records" and put the array 8 bytes too high). The array base is
// corrected; the name is kept so existing callers still compile.
constexpr size_t kOffsetActivityCount = kOffsetRegionCount;
constexpr size_t kOffsetActivityArray = kOffsetRegionArray;
constexpr size_t kActivityRecordStride = kRegionRecordStride;

// Level-name capacity: 0x014..0x057 (68 bytes, Sec6.4 - the next span starts at 0x058).
constexpr size_t kLevelNameMaxBytes = kOffsetObjectHandle - kOffsetLevelName;

// One of the 154 city-takeover-region records (Sec12.4.1): the class is HIGH
// CONFIDENCE, the structure is CONFIRMED.
struct RegionRecord {
    uint32_t handleLo = 0;   // +0
    uint32_t handleHi = 0;   // +4
    bool controlled = false; // +8 bit 0
    std::array<uint8_t, 7> tail{}; // +9..+15, zero in every real sample
};

// A collected collectible (Sec10.3): 8-byte object handle, group 0..3 (0 drug
// packages, 1 money pallets, 2 sex dolls, 3 photo ops), then a zero dword.
struct CollectibleRecord {
    uint32_t handleLo = 0;
    uint32_t handleHi = 0;
    uint32_t group = 0;
    uint32_t reserved = 0;
};

// One of the garage's up to 152 owned-vehicle records (Sec9.2, entry 5).
// NOTE: this does NOT include a live vehicle handle - the real load routine
// always sets a fixed null-handle constant on load (binding to a live
// vehicle happens later, via the player's own use of the garage_retrieve_
// vehicle native or a mission-checkpoint restore, CONFIRMED 2026-10-11,
// Sec9.2) - so there is nothing to round-trip there. The 57-byte
// customization blob (+0x33) is a bit-packed buffer this project does not
// decode; it is carried through as opaque bytes so a round-trip preserves
// it exactly without needing to understand its internal bit layout.
struct GarageRecord {
    uint32_t recordId = 0;        // +0x00, strictly ascending within the list
    uint32_t blobLength = 0;      // +0x04, length in bytes of the blob below (0..57)
    float unknownFlag = 0.0f;     // +0x08, 0.0 or 500.0 in every real record; meaning OPEN
    uint16_t vehicleTypeIndex = 0; // +0x0C, 0..103 (base), 111..119 (DLC)
    uint16_t enumE = 0;           // +0x0E, small enum 0..5
    uint16_t enum10 = 0;          // +0x10, small enum 0..8
    uint8_t byte12 = 0xFF;        // +0x12, 0xFF or 1..10; meaning OPEN
    std::array<uint8_t, 32> optionSet{}; // +0x13, 256-bit customization option set
    std::array<uint8_t, 57> blob{};      // +0x33, opaque customization blob (only blobLength bytes valid)
    uint8_t stateFlags = 0;       // +0x6C, state flags copied from live vehicle state
    // +0x6D..+0x6F: zero pad, not modeled (always written 0)
};

// One of the wardrobe's up to 2048 owned-item records (Sec10.1/10.2's own
// "Owned-item record" row, block at 0x4F6C): a u32 item id (the engine's
// own name-hash, opaque to this project - round-tripped raw, never
// interpreted), 3 colour-slot indices (0xFF = none), and a packed final
// byte: a 4-bit wear-option/variant index, a 1-bit flag, and 3 spare bits
// (0 in every real sample checked so far, per Sec10.2 - still round-tripped
// rather than hardcoded, in case a real save ever sets them). Simpler than
// GarageRecord: no bit-packed sub-byte customization blob, just 4 clean
// byte-aligned sub-fields in the last byte.
struct WardrobeItemRecord {
    uint32_t itemId = 0;      // +0x00, engine name-hash, opaque
    uint8_t colour0 = 0xFF;   // +0x04, 0xFF = none
    uint8_t colour1 = 0xFF;   // +0x05, 0xFF = none
    uint8_t colour2 = 0xFF;   // +0x06, 0xFF = none
    uint8_t wearOption = 0;   // +0x07 bits 0-3, 0..15
    bool flag = false;        // +0x07 bit 4
    uint8_t spareBits = 0;    // +0x07 bits 5-7, 0..7, always 0 in every real sample
};

// One of the weapon inventory's up to 12 owned-weapon records (Sec10.3's
// own span table row for 0x4A74/0x4A78-0x4BC7): a u32 weapon-definition id
// (the engine's own name-hash, opaque to this project, round-tripped raw),
// an unused u32 (always 0 in every real sample, still round-tripped rather
// than hardcoded), four signed "ammo-shaped" i32s (individual meaning -
// loaded/reserve/capacity split - OPEN/unresolved, round-tripped raw as a
// fixed-size array), two single-byte flags (0 or 1 in all samples), and 2
// bytes of pad (always 0 in every real sample, still round-tripped).
// Simpler than GarageRecord: no bit-packed sub-byte customization blob,
// just clean byte-aligned sub-fields, same shape style as
// WardrobeItemRecord.
struct WeaponInventoryRecord {
    uint32_t weaponDefId = 0;          // +0x00, engine name-hash, opaque
    uint32_t unused = 0;               // +0x04, always 0 in every real sample
    std::array<int32_t, 4> ammo{};     // +0x08, +0x0C, +0x10, +0x14; meaning OPEN
    uint8_t flag0 = 0;                 // +0x18, 0 or 1
    uint8_t flag1 = 0;                 // +0x19, 0 or 1
    std::array<uint8_t, 2> pad{};      // +0x1A, +0x1B; always 0 in every real sample
};

// One of the 60 used activity progress records (Sec9.3a).
struct ActivityRecord {
    uint32_t nameHash = 0;        // engine name hash of the activity instance's name (Sec9.8)
    uint32_t levelsCompleted = 0; // 0 or 1 in all 960 real records
    uint8_t completedMask = 0;    // bit L = level L done
    uint8_t secondMask = 0;       // subset of the first
    std::array<uint8_t, 2> pad{};
};

// One of the 149 per-save statistics (ids 68..216, Sec12.6.1): the same 32-bit value the
// profile stores for ids 0..67 - an int32 for integer / percent-numerator stats, a float32 for
// float/distance/time/money stats (the typing is game data, not spec, so the raw dword is
// exposed) - plus the id of the denominator statistic, non-zero only for the eight "hit
// percent" statistics (ids 94, 96, ..., 108, whose denominator is id-1).
struct SnapshotStat {
    uint32_t rawValue = 0;
    uint32_t denominatorStatId = 0;
    int32_t asInt() const { return static_cast<int32_t>(rawValue); }
    float asFloat() const {
        float f;
        std::memcpy(&f, &rawValue, sizeof f);
        return f;
    }
};

// Date/time parsed from the two UTF-16 strings with the game's patterns
// "%02d:%02d:%02d" (time) and "%02d.%02d.%02d" (date: month.day.year2), Sec3.
struct SaveTimestamp {
    bool valid = false;
    int month = 0, day = 0, year2 = 0;
    int hour = 0, minute = 0, second = 0;
};

// A from-spec WRITER's input: the subset of the real snapshot this project
// can actually populate from live state (added 2026-10-08 to implement
// spec-save-format.md Sec15's save-trigger wiring - mission_autosave/
// game_autosave/mission_set_checkpoint, all funneling through the single
// real save entry 0x00B94A10, Sec6.3). Every member below is a field
// SaveSnapshot::parse() above already reads back (same offsets, same
// shapes) - this struct is deliberately NOT a full 32-entry model: bytes it
// does not mention are left at the all-zero value Sec6.3 itself specifies
// ("allocate... zero the whole buffer") for every serializer this project
// has no live counterpart for, which is the format's OWN placeholder
// convention, not an invented one. Defaults below match the real "new
// game" save's own reported values (Sec6.5/Sec10.3: level 0, respect 0,
// difficulty 1 "normal", cheats unused, 0 barnstorms/stunt jumps, no active
// mission) - see sr3luahost::SaveGameState (save_game_state.h) for the
// live engine-state struct this is built from.
struct SaveSnapshotFields {
    uint32_t formatVersion = kSnapshotFormatVersion; // 94, Sec3/Sec11.1
    uint32_t secondaryVersion = 1;                   // Sec3
    uint32_t buildStamp = 0x000F0F6E;                // Sec6.3/Sec6.5, current-build literal
    std::string levelName;                           // Sec6.3 header; must leave room for a NUL (< kLevelNameMaxBytes)
    uint32_t objectHandleLo = 0, objectHandleHi = 0; // Sec6.4 shape; no real handle modeled, left 0
    std::string saveTime = "00:00:00";               // "HH:MM:SS", Sec3
    std::string saveDate = "01.01.00";               // "MM.DD.YY", Sec3
    uint32_t labelIndex = 0x7F;                       // 0x084, Sec12.10.1: 0x7F = "no active mission"; display-only on load
    uint32_t completionPercent = 0;                   // 0x088, Sec6.5
    float playClockFloat = 0.0f;                      // 0x09C; this project tracks no in-game clock
    uint32_t playTimeSeconds = 0;                      // 0x0A0; same reason
    uint8_t cheatsUsed = 0;                            // 0x0C8, Sec10.3
    uint32_t difficulty = 1;                           // 0x2B64, Sec6.5: 0 casual/1 normal/2 hardcore
    uint32_t barnstormsFound = 0;                      // 0x3F0, Sec10.3
    uint32_t stuntJumpsFound = 0;                      // 0x3F8, Sec10.3
    uint32_t respectTotal = 0, respectInLevel = 0, playerLevel = 0; // 0x4418/0x441C/0x4420, Sec10.3
    int32_t cashRaw = 0;                               // 0x4A70, cash on hand x100, clamped +-2,000,000,000 (Sec10.3)
    std::vector<CollectibleRecord> collectibles;       // 0x3FC, Sec10.3; must fit kCollectibleCapacity (80)
    std::vector<GarageRecord> garageRecords;           // 0xB760, Sec9.2; must fit kGarageCapacity (152)
    uint32_t garageNextId = 1;                         // 0xF9E4, Sec9.2: next record id (starts at 1 on new game)
    std::vector<WardrobeItemRecord> wardrobeItems;     // 0x4F6C, Sec10.1/10.2/10.3; must fit kWardrobeCapacity (2048)
    std::vector<WeaponInventoryRecord> weaponInventory; // 0x4A74/0x4A78, Sec10.3; must fit kWeaponInventoryCapacity (12)
    std::vector<RegionRecord> regionRecords;           // 0x113B8/0x113C0, Sec12.4.1; must fit kRegionRecordCapacity (156)
    std::vector<ActivityRecord> activityRecords;       // 0x2B90, Sec9.3a; must fit kActivityTableCapacity (64)
    uint64_t activityMarkerFlags = 0;                  // 0x2E98, Sec9.3b
    std::vector<uint32_t> activeCheatIds;              // 0x0CC, Sec9.5; must fit kCheatIdCapacity (200)
    // 0x3340: statistics 68..216 (Sec12.6.1). Optional - defaults to all
    // zero (every statistic's own documented "not yet earned" value), same
    // placeholder convention as every other un-modeled span.
    std::array<SnapshotStat, kSnapshotStatCount> statistics{};
};

class SaveSnapshot {
public:
    // Parses `bytes`, which must be exactly kSaveSnapshotSize bytes (the game's
    // reader requires the returned size to match). Throws FormatError otherwise,
    // or if the level-name / time / date strings have no terminator in their
    // reserved room. Does NOT throw on a hash mismatch or a version other than 94.
    static SaveSnapshot parse(ByteView bytes);

    // Builds a real kSaveSnapshotSize-byte buffer from `fields`, following
    // Sec6.3's own write recipe: start from an all-zero buffer (every
    // un-modeled span stays zero, the format's own documented placeholder,
    // never an invented value), write each field this project models at its
    // real offset, then (iff `computeHash` - default true) stamp the Sec6.1
    // CRC at offset 0x000, exactly as the real save entry does for a file
    // write. Pass `computeHash = false` for Sec11.2's OTHER real mode - a
    // mission checkpoint, built into caller-owned memory with "no hash, no
    // file" (left 0, matching the real zeroed-and-never-hashed buffer) -
    // SaveSnapshot::parse() accepts either, since the game's own loader
    // never checks this field either way (Sec11.1).
    //
    // Throws FormatError if a field does not fit its real format room
    // (levelName/saveTime/saveDate too long to leave a terminator, or a
    // collection past its real capacity) - the same "a CONFIRMED hard rule,
    // never a HYPOTHESIS/OPEN one" discipline errors.h documents for parse().
    static std::vector<uint8_t> build(const SaveSnapshotFields& fields, bool computeHash = true);

    // ---- hash (Sec6.1; written on save, never verified on load) ----------
    uint32_t hashStored() const { return checksumRaw_; }
    uint32_t hashComputed() const { return hashComputed_; }
    bool hashValid() const { return checksumRaw_ == hashComputed_; }
    uint32_t checksumRaw() const { return checksumRaw_; } // legacy name for hashStored()

    // ---- header ----------------------------------------------------------
    uint32_t formatVersion() const { return formatVersion_; }
    bool versionSupported() const { return formatVersion_ == kSnapshotFormatVersion; }
    uint32_t secondaryVersion() const { return secondaryVersion_; }
    // 0x010: 0x000F0F6E in 14/16 real files, 0x000EA708 / 0x000E8CE7 in the two
    // oldest. The literal is CONFIRMED; "build stamp" is HIGH CONFIDENCE.
    uint32_t buildStamp() const { return buildStamp_; }
    const std::string& levelName() const { return levelName_; }
    uint32_t objectHandleLo() const { return objectHandleLo_; }
    uint32_t objectHandleHi() const { return objectHandleHi_; }
    const std::string& saveTime() const { return saveTime_; }  // "HH:MM:SS"
    const std::string& saveDate() const { return saveDate_; }  // "MM.DD.YY"
    const SaveTimestamp& timestamp() const { return timestamp_; }
    // Save-list sort key (Sec6.5 corrected): second + 60*minute + 3600*hour +
    // 86400*day + 2678400*month + 32140800*year (year factor 32,140,800, NOT
    // 31,536,000). Computed from the parsed strings; 0 if they did not parse.
    // Returned as int64 so no 32-bit wrap; 32140800*99 still fits in uint32.
    int64_t saveSortKey() const;

    // 0x084: index of the active mission in a 50-slot table, 0x7F = none
    // (Sec12.10.1, HIGH CONFIDENCE); also the value the save list uses to look up
    // the slot's display label (Sec6.5).
    uint32_t labelIndex() const { return labelIndex_; }
    // 0x088: completion percentage, 35a+40b+15c+10d (Sec6.5: weights CONFIRMED,
    // "percent" HIGH CONFIDENCE). Samples hold 1..78.
    uint32_t completionPercent() const { return completionPercent_; }

    // 0x09C: float32 play-time clock; 0x0A0: the same clock truncated to whole
    // game-clock SECONDS (Sec6.2, CONFIRMED 16/16). The save list shows
    // trunc(seconds / 60.0) whole minutes.
    float playClockFloat() const { return playClockFloat_; }
    uint32_t playTimeSeconds() const { return playTimeSeconds_; }
    uint32_t playTimeMinutes() const;
    // Legacy accessor. The field is whole seconds (Sec6.2), NOT ticks; the old
    // name is kept only so existing callers still compile.
    uint32_t playTimeTicksRaw() const { return playTimeSeconds_; }

    // ---- player state (Sec10.3, Sec6.5) ---------------------------------
    bool cheatsUsed() const { return cheatsUsed_ != 0; }
    uint32_t difficultyIndex() const { return difficulty_; }
    // "casual", "normal", "hardcore" for 0/1/2 (Sec6.5: the save list's 3-entry
    // label table, HIGH CONFIDENCE); "unknown" otherwise.
    const char* difficultyName() const;
    uint32_t barnstormsFound() const { return barnstorms_; }
    uint32_t stuntJumpsFound() const { return stuntJumps_; }
    uint32_t respectTotal() const { return respectTotal_; }
    uint32_t respectInLevel() const { return respectInLevel_; }
    uint32_t playerLevel() const { return playerLevel_; }
    // Cash on hand x100, clamped to +-2,000,000,000 by the game (Sec10.3).
    int32_t cashRaw() const { return cashRaw_; }
    // The save list / directory show signed integer division by 100.
    int32_t cashDiv100() const { return cashRaw_ / 100; }

    // ---- collections -----------------------------------------------------
    // 0x3FC: collected collectibles. Empty (and collectiblesInBounds() false) if the
    // stored count exceeds the 80-record capacity.
    uint32_t collectibleCount() const { return collectibleCount_; }
    bool collectiblesInBounds() const { return collectibleCount_ <= kCollectibleCapacity; }
    const std::vector<CollectibleRecord>& collectibles() const { return collectibles_; }

    // 0xB760: the garage's owned-vehicle records (Sec9.2). Empty (and garageRecordsInBounds()
    // false) if the stored count exceeds the 152-record capacity.
    uint32_t garageRecordCount() const { return garageRecordCount_; }
    bool garageRecordsInBounds() const { return garageRecordCount_ <= kGarageCapacity; }
    const std::vector<GarageRecord>& garageRecords() const { return garageRecords_; }
    // 0xF9E4: next record id to assign (1 + the largest id seen, Sec9.2).
    uint32_t garageNextId() const { return garageNextId_; }

    // 0x4F6C: the wardrobe's owned-item records (Sec10.1/10.2/10.3). Empty
    // (and wardrobeItemsInBounds() false) if the stored count exceeds the
    // 2048-record capacity.
    uint32_t wardrobeItemCount() const { return wardrobeItemCount_; }
    bool wardrobeItemsInBounds() const { return wardrobeItemCount_ <= kWardrobeCapacity; }
    const std::vector<WardrobeItemRecord>& wardrobeItems() const { return wardrobeItems_; }

    // 0x4A74/0x4A78: the weapon inventory's owned-weapon records (Sec10.3's
    // own span table row). Empty (and weaponInventoryInBounds() false) if
    // the stored count exceeds the 12-record capacity.
    uint32_t weaponInventoryCount() const { return weaponInventoryCount_; }
    bool weaponInventoryInBounds() const { return weaponInventoryCount_ <= kWeaponInventoryCapacity; }
    const std::vector<WeaponInventoryRecord>& weaponInventory() const { return weaponInventory_; }

    // 0x113B8 / 0x113C0: the region records (154 in 16/16). regionRecords() is empty
    // when the claimed count does not fit in the room before 0x11D80 (degrades
    // gracefully rather than treating the unverified count as a format violation).
    uint32_t regionRecordCount() const { return regionRecordCount_; }
    bool regionRecordsInBounds() const { return regionRecordCount_ <= kRegionRecordCapacity; }
    const std::vector<RegionRecord>& regionRecords() const { return regionRecords_; }
    // Number of records whose flag bit 0 is set - what the descriptor counts.
    uint32_t controlledRegionCount() const;
    // Legacy names for the two above.
    uint32_t activityRecordCount() const { return regionRecordCount_; }
    const std::vector<uint8_t>& activityRecordsRaw() const { return regionRecordsRaw_; }

    // 0x2B90: the activity progress table: used-record count (60 in 16/16) and the
    // records (name-hash key, levels completed, completed-level mask, second mask).
    uint32_t activityTableCount() const { return activityTableCount_; }
    bool activityTableInBounds() const { return activityTableCount_ <= kActivityTableCapacity; }
    const std::vector<ActivityRecord>& activityRecords() const { return activityRecords_; }
    // 0x2E98: 64-bit marker-flag set, bit i = flag on the marker object linked to
    // activity i. The meaning of the flag is OPEN (Sec9.3b) - exposed raw only.
    uint64_t activityMarkerFlagsRaw() const { return activityMarkerFlags_; }

    // 0x0CC: ids of the currently active cheat-table entries (name hashes, Sec9.5).
    // Empty if the stored count exceeds the capacity of 200.
    // 0x3340: statistics 68..216. statistic(id) returns nullptr outside that range.
    const SnapshotStat* statistic(uint32_t statId) const {
        if (statId < kFirstSnapshotStatId || statId >= kFirstSnapshotStatId + kSnapshotStatCount) return nullptr;
        return &statistics_[statId - kFirstSnapshotStatId];
    }

    uint32_t activeCheatCount() const { return activeCheatCount_; }
    const std::vector<uint32_t>& activeCheatIds() const { return activeCheatIds_; }

    // The directory-slot summary (Sec8.2) as the game derives it from this
    // snapshot - the 17 snapshot-derived rows plus the two reserved-zero rows.
    // Rows 2 and 4 (live-world counters, OPEN) are left 0 and row 8 (autosave) is
    // taken from `autosave`. Compare against a real directory's decoded summary to
    // re-check Sec8 (harness validate_save_summary).
    SlotSummary deriveDirectorySummary(bool autosave) const;

private:
    uint32_t checksumRaw_ = 0;
    uint32_t hashComputed_ = 0;
    uint32_t formatVersion_ = 0;
    uint32_t secondaryVersion_ = 0;
    uint32_t buildStamp_ = 0;
    std::string levelName_;
    uint32_t objectHandleLo_ = 0, objectHandleHi_ = 0;
    std::string saveTime_;
    std::string saveDate_;
    SaveTimestamp timestamp_;
    uint32_t labelIndex_ = 0;
    uint32_t completionPercent_ = 0;
    float playClockFloat_ = 0;
    uint32_t playTimeSeconds_ = 0;
    uint8_t cheatsUsed_ = 0;
    uint32_t difficulty_ = 0;
    uint32_t barnstorms_ = 0, stuntJumps_ = 0;
    uint32_t respectTotal_ = 0, respectInLevel_ = 0, playerLevel_ = 0;
    int32_t cashRaw_ = 0;
    uint32_t collectibleCount_ = 0;
    std::vector<CollectibleRecord> collectibles_;
    uint32_t garageRecordCount_ = 0;
    std::vector<GarageRecord> garageRecords_;
    uint32_t garageNextId_ = 0;
    uint32_t wardrobeItemCount_ = 0;
    std::vector<WardrobeItemRecord> wardrobeItems_;
    uint32_t weaponInventoryCount_ = 0;
    std::vector<WeaponInventoryRecord> weaponInventory_;
    uint32_t regionRecordCount_ = 0;
    std::vector<RegionRecord> regionRecords_;
    std::vector<uint8_t> regionRecordsRaw_;
    uint32_t activityTableCount_ = 0;
    std::vector<ActivityRecord> activityRecords_;
    uint64_t activityMarkerFlags_ = 0;
    uint32_t activeCheatCount_ = 0;
    std::vector<uint32_t> activeCheatIds_;
    std::array<SnapshotStat, kSnapshotStatCount> statistics_{};
};

} // namespace sr3save
