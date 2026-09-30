#include "sr3save/save_snapshot.h"

#include <cstring>
#include <sstream>

#include "sr3save/save_crc.h"

namespace sr3save {

namespace {

// Reads a null-terminated 1-byte-per-character ASCII string starting at
// `offset`, scanning at most `maxLen` bytes for the terminator. Throws
// FormatError if none is found in that range - a corrupt file or a wrong
// offset, not something to silently paper over.
std::string readAsciiCStringBounded(ByteView bytes, size_t offset, size_t maxLen) {
    for (size_t i = 0; i < maxLen; ++i) {
        if (bytes.at(offset + i) == 0) {
            ByteView sub = bytes.subview(offset, i);
            return std::string(reinterpret_cast<const char*>(sub.data()), sub.size());
        }
    }
    throw FormatError(
        "level/zone name field (spec Sec3/Sec6.4, offset 0x014) has no null "
        "terminator within the expected " + std::to_string(maxLen) +
        "-byte region before the next confirmed field");
}

// Decodes a null-terminated UTF-16LE string that spec Sec3 confirms
// contains only ASCII characters (digits, ':', '.') for the save-time and
// save-date fields: reads 2 bytes per character, taking the low byte, and
// stops at a 2-byte null terminator or `maxBytes` bytes (the field's
// reserved region), whichever comes first - a malformed/unterminated
// string can't run past its reserved region.
std::string readAsciiUtf16LEBounded(ByteView bytes, size_t offset, size_t maxBytes) {
    std::string out;
    for (size_t i = 0; i + 1 < maxBytes; i += 2) {
        uint8_t lo = bytes.at(offset + i);
        uint8_t hi = bytes.at(offset + i + 1);
        if (lo == 0 && hi == 0) {
            return out;
        }
        out.push_back(static_cast<char>(lo));
    }
    std::ostringstream offsetHex;
    offsetHex << std::hex << offset;
    throw FormatError(
        "UTF-16LE string field at offset 0x" + offsetHex.str() +
        " (spec Sec3) has no null terminator within its " +
        std::to_string(maxBytes) + "-byte reserved region");
}

// "%02d<sep>%02d<sep>%02d": three 1-2 digit numbers separated by `sep`.
bool parseThree(const std::string& s, char sep, int& a, int& b, int& c) {
    int* out[3] = {&a, &b, &c};
    size_t pos = 0;
    for (int k = 0; k < 3; ++k) {
        size_t digits = 0;
        int v = 0;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9' && digits < 2) {
            v = v * 10 + (s[pos] - '0');
            ++pos;
            ++digits;
        }
        if (digits == 0) return false;
        *out[k] = v;
        if (k < 2) {
            if (pos >= s.size() || s[pos] != sep) return false;
            ++pos;
        }
    }
    return pos == s.size();
}

float bitsToFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
}

} // namespace

SaveSnapshot SaveSnapshot::parse(ByteView bytes) {
    if (bytes.size() != kSaveSnapshotSize) {
        throw FormatError(
            "sr3save_NN.sr3s_pc must be exactly 108776 bytes (spec Sec1/Sec11.1, "
            "CONFIRMED - the real loader requires the read to return exactly the reported size); got " +
            std::to_string(bytes.size()) + " bytes");
    }

    SaveSnapshot s;
    s.checksumRaw_ = bytes.readU32LE(kOffsetChecksum);
    s.hashComputed_ = computeSnapshotHash(bytes);
    s.formatVersion_ = bytes.readU32LE(kOffsetFormatVersion);
    s.secondaryVersion_ = bytes.readU32LE(kOffsetSecondaryVersion);
    s.buildStamp_ = bytes.readU32LE(kOffsetBuildStamp);
    s.levelName_ = readAsciiCStringBounded(bytes, kOffsetLevelName, kLevelNameMaxBytes);
    s.objectHandleLo_ = bytes.readU32LE(kOffsetObjectHandle);
    s.objectHandleHi_ = bytes.readU32LE(kOffsetObjectHandle + 4);
    s.saveTime_ = readAsciiUtf16LEBounded(bytes, kOffsetSaveTime, 18);
    s.saveDate_ = readAsciiUtf16LEBounded(bytes, kOffsetSaveDate, 18);

    {
        SaveTimestamp t;
        int h = 0, mi = 0, se = 0, mo = 0, d = 0, y = 0;
        bool okT = parseThree(s.saveTime_, ':', h, mi, se);
        bool okD = parseThree(s.saveDate_, '.', mo, d, y);
        if (okT && okD) {
            t.valid = true;
            t.hour = h;
            t.minute = mi;
            t.second = se;
            t.month = mo;
            t.day = d;
            t.year2 = y;
        }
        s.timestamp_ = t;
    }

    s.labelIndex_ = bytes.readU32LE(kOffsetLabelIndex);
    s.completionPercent_ = bytes.readU32LE(kOffsetCompletionPercent);
    s.playClockFloat_ = bitsToFloat(bytes.readU32LE(kOffsetClockPlayFloat));
    s.playTimeSeconds_ = bytes.readU32LE(kOffsetPlayTimeSeconds);
    s.cheatsUsed_ = bytes.at(kOffsetCheatsUsed);
    s.difficulty_ = bytes.readU32LE(kOffsetDifficulty);
    s.barnstorms_ = bytes.readU32LE(kOffsetBarnstorms);
    s.stuntJumps_ = bytes.readU32LE(kOffsetStuntJumps);
    s.respectTotal_ = bytes.readU32LE(kOffsetRespectTotal);
    s.respectInLevel_ = bytes.readU32LE(kOffsetRespectInLevel);
    s.playerLevel_ = bytes.readU32LE(kOffsetPlayerLevel);
    s.cashRaw_ = static_cast<int32_t>(bytes.readU32LE(kOffsetCash));

    // collectibles: u32 count + up to 80 x 16 bytes
    s.collectibleCount_ = bytes.readU32LE(kOffsetCollectibleCount);
    if (s.collectibleCount_ <= kCollectibleCapacity) {
        for (uint32_t i = 0; i < s.collectibleCount_; ++i) {
            size_t o = kOffsetCollectibles + i * 16;
            CollectibleRecord r;
            r.handleLo = bytes.readU32LE(o);
            r.handleHi = bytes.readU32LE(o + 4);
            r.group = bytes.readU32LE(o + 8);
            r.reserved = bytes.readU32LE(o + 12);
            s.collectibles_.push_back(r);
        }
    }

    // region records: u32 count at 0x113B8, 16-byte records from 0x113C0
    s.regionRecordCount_ = bytes.readU32LE(kOffsetRegionCount);
    if (s.regionRecordCount_ <= kRegionRecordCapacity) {
        for (uint32_t i = 0; i < s.regionRecordCount_; ++i) {
            size_t o = kOffsetRegionArray + i * kRegionRecordStride;
            RegionRecord r;
            r.handleLo = bytes.readU32LE(o);
            r.handleHi = bytes.readU32LE(o + 4);
            r.controlled = (bytes.at(o + 8) & 1u) != 0;
            for (size_t k = 0; k < 7; ++k) r.tail[k] = bytes.at(o + 9 + k);
            s.regionRecords_.push_back(r);
        }
        ByteView span = bytes.subview(kOffsetRegionArray, static_cast<size_t>(s.regionRecordCount_) * kRegionRecordStride);
        s.regionRecordsRaw_.assign(span.data(), span.data() + span.size());
    }

    // activity progress table (0x308-byte struct at 0x2B90)
    s.activityTableCount_ = bytes.readU32LE(kOffsetActivityTable + kActivityTableCountOffset);
    if (s.activityTableCount_ <= kActivityTableCapacity) {
        for (uint32_t i = 0; i < s.activityTableCount_; ++i) {
            size_t o = kOffsetActivityTable + kActivityTableRecordsOffset + i * 12;
            ActivityRecord r;
            r.nameHash = bytes.readU32LE(o);
            r.levelsCompleted = bytes.readU32LE(o + 4);
            r.completedMask = bytes.at(o + 8);
            r.secondMask = bytes.at(o + 9);
            r.pad[0] = bytes.at(o + 10);
            r.pad[1] = bytes.at(o + 11);
            s.activityRecords_.push_back(r);
        }
    }
    s.activityMarkerFlags_ = static_cast<uint64_t>(bytes.readU32LE(kOffsetActivityMarkerBits)) |
                             (static_cast<uint64_t>(bytes.readU32LE(kOffsetActivityMarkerBits + 4)) << 32);

    // statistics 68..216: 149 rows of (value, denominator-stat id)
    for (size_t i = 0; i < kSnapshotStatCount; ++i) {
        s.statistics_[i].rawValue = bytes.readU32LE(kOffsetStatistics + 8 * i);
        s.statistics_[i].denominatorStatId = bytes.readU32LE(kOffsetStatistics + 8 * i + 4);
    }

    // active cheat ids
    s.activeCheatCount_ = bytes.readU32LE(kOffsetCheatCount);
    if (s.activeCheatCount_ <= kCheatIdCapacity) {
        for (uint32_t i = 0; i < s.activeCheatCount_; ++i) {
            s.activeCheatIds_.push_back(bytes.readU32LE(kOffsetCheatIds + 4 * static_cast<size_t>(i)));
        }
    }

    return s;
}

int64_t SaveSnapshot::saveSortKey() const {
    if (!timestamp_.valid) return 0;
    return static_cast<int64_t>(timestamp_.second) + 60LL * timestamp_.minute + 3600LL * timestamp_.hour +
           86400LL * timestamp_.day + 2678400LL * timestamp_.month + 32140800LL * timestamp_.year2;
}

uint32_t SaveSnapshot::playTimeMinutes() const {
    // The game biases a set sign bit by 2^32 (i.e. reads the counter as unsigned),
    // divides by the double constant 60.0 and truncates (Sec6.2).
    return static_cast<uint32_t>(static_cast<double>(playTimeSeconds_) / 60.0);
}

const char* SaveSnapshot::difficultyName() const {
    switch (difficulty_) {
    case 0: return "casual";
    case 1: return "normal";
    case 2: return "hardcore";
    default: return "unknown";
    }
}

uint32_t SaveSnapshot::controlledRegionCount() const {
    uint32_t n = 0;
    for (const auto& r : regionRecords_) {
        if (r.controlled) ++n;
    }
    return n;
}

SlotSummary SaveSnapshot::deriveDirectorySummary(bool autosave) const {
    SlotSummary s;
    s.flaggedRecordCount = controlledRegionCount() & 0x1FFu;
    s.playMinutes = playTimeMinutes() & 0x1FFFFu;
    s.difficulty = difficulty_ & 0x3u;
    s.cheatsUsed = (cheatsUsed_ & 1u) != 0;
    s.autosave = autosave;
    s.completionPercent = completionPercent_ & 0x7Fu;
    s.formatVersion = formatVersion_ & 0xFFu;
    s.locationLabelId = labelIndex_ & 0x7Fu;
    if (timestamp_.valid) {
        s.month = static_cast<uint32_t>(timestamp_.month) & 0xFu;
        s.day = static_cast<uint32_t>(timestamp_.day) & 0x1Fu;
        s.year2 = static_cast<uint32_t>(timestamp_.year2) & 0x7Fu;
        s.hour = static_cast<uint32_t>(timestamp_.hour) & 0x1Fu;
        s.minute = static_cast<uint32_t>(timestamp_.minute) & 0x3Fu;
        s.second = static_cast<uint32_t>(timestamp_.second) & 0x3Fu;
    }
    s.cashDiv100 = cashRaw_ / 100;
    s.level = playerLevel_ & 0x3Fu;
    s.regionRecordCount = regionRecordCount_ & 0x1FFu;
    s.pointsInLevel = respectInLevel_;
    return s;
}

} // namespace sr3save
