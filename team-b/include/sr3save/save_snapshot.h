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
constexpr size_t kOffsetClockStruct = 0x08C;     // 20-byte in-world clock struct copy (Sec6.2)
constexpr size_t kOffsetClockPlayFloat = 0x09C;  // float32 play-time clock (last dword of the struct)
constexpr size_t kOffsetPlayTimeSeconds = 0x0A0; // u32 whole game-clock SECONDS (Sec6.2, CONFIRMED)
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
constexpr size_t kOffsetRespectTotal = 0x4418;   // total respect (Sec10.3, CONFIRMED)
constexpr size_t kOffsetRespectInLevel = 0x441C; // respect accumulated inside the current level
constexpr size_t kOffsetPlayerLevel = 0x4420;    // player level 0..50
constexpr size_t kOffsetCash = 0x4A70;           // cash on hand x100, clamped to 2,000,000,000 (Sec10.3)
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

class SaveSnapshot {
public:
    // Parses `bytes`, which must be exactly kSaveSnapshotSize bytes (the game's
    // reader requires the returned size to match). Throws FormatError otherwise,
    // or if the level-name / time / date strings have no terminator in their
    // reserved room. Does NOT throw on a hash mismatch or a version other than 94.
    static SaveSnapshot parse(ByteView bytes);

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
