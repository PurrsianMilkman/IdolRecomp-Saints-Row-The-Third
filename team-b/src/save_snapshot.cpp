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

uint32_t floatToBits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, sizeof v);
    return v;
}

// ---- little-endian write helpers (same house style as src/wav_writer.cpp's
// putU32LE/putU16LE, applied here to an in-place buffer instead of an
// append-only one since every offset below is a fixed, known position) ----
void putU32LE(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void putAsciiCStringBounded(std::vector<uint8_t>& b, size_t off, const std::string& s, size_t maxLen) {
    if (s.size() + 1 > maxLen) {
        throw FormatError("level/zone name '" + s + "' (spec Sec3/Sec6.4, offset 0x014) does not leave room for a "
                           "NUL terminator within the " + std::to_string(maxLen) + "-byte reserved region");
    }
    std::memcpy(&b[off], s.data(), s.size());
    b[off + s.size()] = 0;
}

// Encodes `s` (ASCII only) as a NUL-terminated UTF-16LE string, mirroring
// readAsciiUtf16LEBounded's own reverse convention (low byte = the ASCII
// char, high byte = 0). Throws FormatError if it would not leave room for
// the 2-byte terminator within `maxBytes` (Sec3's own 18-byte reserved room
// for the save-time/save-date fields).
void putAsciiUtf16LEBounded(std::vector<uint8_t>& b, size_t off, const std::string& s, size_t maxBytes) {
    if (s.size() * 2 + 2 > maxBytes) {
        throw FormatError("UTF-16LE field value '" + s + "' (spec Sec3) does not leave room for its 2-byte NUL "
                           "terminator within the " + std::to_string(maxBytes) + "-byte reserved region");
    }
    for (size_t i = 0; i < s.size(); ++i) {
        b[off + i * 2] = static_cast<uint8_t>(s[i]);
        b[off + i * 2 + 1] = 0;
    }
    b[off + s.size() * 2] = 0;
    b[off + s.size() * 2 + 1] = 0;
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

    // garage: u32 count at 0xB760, 0x70-byte records from 0xB764, next-id at 0xF9E4
    s.garageRecordCount_ = bytes.readU32LE(kOffsetGarageCount);
    if (s.garageRecordCount_ <= kGarageCapacity) {
        for (uint32_t i = 0; i < s.garageRecordCount_; ++i) {
            size_t o = kOffsetGarageArray + i * kGarageRecordStride;
            GarageRecord r;
            r.recordId = bytes.readU32LE(o);
            r.blobLength = bytes.readU32LE(o + 4);
            r.unknownFlag = bitsToFloat(bytes.readU32LE(o + 8));
            r.vehicleTypeIndex = bytes.readU16LE(o + 0xC);
            r.enumE = bytes.readU16LE(o + 0xE);
            r.enum10 = bytes.readU16LE(o + 0x10);
            r.byte12 = bytes.at(o + 0x12);
            for (size_t k = 0; k < 32; ++k) r.optionSet[k] = bytes.at(o + 0x13 + k);
            for (size_t k = 0; k < 57; ++k) r.blob[k] = bytes.at(o + 0x33 + k);
            r.stateFlags = bytes.at(o + 0x6C);
            s.garageRecords_.push_back(r);
        }
    }
    s.garageNextId_ = bytes.readU32LE(kOffsetGarageNextId);

    // wardrobe: u32 count at 0x4F6C, 8-byte records from 0x4F70 (Sec10.1/10.2/10.3)
    s.wardrobeItemCount_ = bytes.readU32LE(kOffsetWardrobeCount);
    if (s.wardrobeItemCount_ <= kWardrobeCapacity) {
        for (uint32_t i = 0; i < s.wardrobeItemCount_; ++i) {
            size_t o = kOffsetWardrobeArray + i * kWardrobeRecordStride;
            WardrobeItemRecord r;
            r.itemId = bytes.readU32LE(o);
            r.colour0 = bytes.at(o + 4);
            r.colour1 = bytes.at(o + 5);
            r.colour2 = bytes.at(o + 6);
            uint8_t packed = bytes.at(o + 7);
            r.wearOption = packed & 0x0Fu;
            r.flag = (packed & 0x10u) != 0;
            r.spareBits = (packed >> 5) & 0x07u;
            s.wardrobeItems_.push_back(r);
        }
    }

    // weapon inventory: u32 count at 0x4A74, 28-byte records from 0x4A78 (Sec10.3)
    s.weaponInventoryCount_ = bytes.readU32LE(kOffsetWeaponInventoryCount);
    if (s.weaponInventoryCount_ <= kWeaponInventoryCapacity) {
        for (uint32_t i = 0; i < s.weaponInventoryCount_; ++i) {
            size_t o = kOffsetWeaponInventoryArray + i * kWeaponInventoryRecordStride;
            WeaponInventoryRecord r;
            r.weaponDefId = bytes.readU32LE(o);
            r.unused = bytes.readU32LE(o + 0x04);
            r.ammo[0] = static_cast<int32_t>(bytes.readU32LE(o + 0x08));
            r.ammo[1] = static_cast<int32_t>(bytes.readU32LE(o + 0x0C));
            r.ammo[2] = static_cast<int32_t>(bytes.readU32LE(o + 0x10));
            r.ammo[3] = static_cast<int32_t>(bytes.readU32LE(o + 0x14));
            r.flag0 = bytes.at(o + 0x18);
            r.flag1 = bytes.at(o + 0x19);
            r.pad[0] = bytes.at(o + 0x1A);
            r.pad[1] = bytes.at(o + 0x1B);
            s.weaponInventory_.push_back(r);
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

std::vector<uint8_t> SaveSnapshot::build(const SaveSnapshotFields& f, bool computeHash) {
    // Sec6.3: "allocate 0x1AA00 bytes from a dedicated save arena, zero the
    // whole buffer, run the serializer driver." This project's own writer
    // only has 108,776 (kSaveSnapshotSize) bytes of that arena to fill -
    // the trailing 0x118-byte tail the real file never carries either
    // (Sec6.1) - so every un-modeled span simply stays at this zero-fill,
    // the format's own documented placeholder.
    std::vector<uint8_t> b(kSaveSnapshotSize, 0);

    putU32LE(b, kOffsetFormatVersion, f.formatVersion);
    putU32LE(b, kOffsetSecondaryVersion, f.secondaryVersion);
    putU32LE(b, kOffsetBuildStamp, f.buildStamp);
    putAsciiCStringBounded(b, kOffsetLevelName, f.levelName, kLevelNameMaxBytes);
    putU32LE(b, kOffsetObjectHandle, f.objectHandleLo);
    putU32LE(b, kOffsetObjectHandle + 4, f.objectHandleHi);
    putAsciiUtf16LEBounded(b, kOffsetSaveTime, f.saveTime, 18);
    putAsciiUtf16LEBounded(b, kOffsetSaveDate, f.saveDate, 18);
    putU32LE(b, kOffsetLabelIndex, f.labelIndex);
    putU32LE(b, kOffsetCompletionPercent, f.completionPercent);
    putU32LE(b, kOffsetClockPlayFloat, floatToBits(f.playClockFloat));
    putU32LE(b, kOffsetPlayTimeSeconds, f.playTimeSeconds);
    b[kOffsetCheatsUsed] = f.cheatsUsed;
    putU32LE(b, kOffsetDifficulty, f.difficulty);
    putU32LE(b, kOffsetBarnstorms, f.barnstormsFound);
    putU32LE(b, kOffsetStuntJumps, f.stuntJumpsFound);
    putU32LE(b, kOffsetRespectTotal, f.respectTotal);
    putU32LE(b, kOffsetRespectInLevel, f.respectInLevel);
    putU32LE(b, kOffsetPlayerLevel, f.playerLevel);
    putU32LE(b, kOffsetCash, static_cast<uint32_t>(f.cashRaw));

    if (f.collectibles.size() > kCollectibleCapacity) {
        throw FormatError("SaveSnapshotFields::collectibles has " + std::to_string(f.collectibles.size()) +
                           " entries, over the real 80-record capacity (Sec10.3)");
    }
    putU32LE(b, kOffsetCollectibleCount, static_cast<uint32_t>(f.collectibles.size()));
    for (size_t i = 0; i < f.collectibles.size(); ++i) {
        size_t o = kOffsetCollectibles + i * 16;
        const CollectibleRecord& r = f.collectibles[i];
        putU32LE(b, o, r.handleLo);
        putU32LE(b, o + 4, r.handleHi);
        putU32LE(b, o + 8, r.group);
        putU32LE(b, o + 12, r.reserved);
    }

    if (f.garageRecords.size() > kGarageCapacity) {
        throw FormatError("SaveSnapshotFields::garageRecords has " + std::to_string(f.garageRecords.size()) +
                           " entries, over the real " + std::to_string(kGarageCapacity) +
                           "-record capacity (Sec9.2)");
    }
    putU32LE(b, kOffsetGarageCount, static_cast<uint32_t>(f.garageRecords.size()));
    for (size_t i = 0; i < f.garageRecords.size(); ++i) {
        size_t o = kOffsetGarageArray + i * kGarageRecordStride;
        const GarageRecord& r = f.garageRecords[i];
        putU32LE(b, o, r.recordId);
        putU32LE(b, o + 4, r.blobLength);
        putU32LE(b, o + 8, floatToBits(r.unknownFlag));
        b[o + 0xC] = static_cast<uint8_t>(r.vehicleTypeIndex & 0xFF);
        b[o + 0xD] = static_cast<uint8_t>((r.vehicleTypeIndex >> 8) & 0xFF);
        b[o + 0xE] = static_cast<uint8_t>(r.enumE & 0xFF);
        b[o + 0xF] = static_cast<uint8_t>((r.enumE >> 8) & 0xFF);
        b[o + 0x10] = static_cast<uint8_t>(r.enum10 & 0xFF);
        b[o + 0x11] = static_cast<uint8_t>((r.enum10 >> 8) & 0xFF);
        b[o + 0x12] = r.byte12;
        for (size_t k = 0; k < 32; ++k) b[o + 0x13 + k] = r.optionSet[k];
        for (size_t k = 0; k < 57; ++k) b[o + 0x33 + k] = r.blob[k];
        b[o + 0x6C] = r.stateFlags;
        // +0x6D..+0x6F left zero (real pad, Sec9.2)
    }
    putU32LE(b, kOffsetGarageNextId, f.garageNextId);

    if (f.wardrobeItems.size() > kWardrobeCapacity) {
        throw FormatError("SaveSnapshotFields::wardrobeItems has " + std::to_string(f.wardrobeItems.size()) +
                           " entries, over the real " + std::to_string(kWardrobeCapacity) +
                           "-record capacity (Sec10.1/10.2/10.3)");
    }
    putU32LE(b, kOffsetWardrobeCount, static_cast<uint32_t>(f.wardrobeItems.size()));
    for (size_t i = 0; i < f.wardrobeItems.size(); ++i) {
        size_t o = kOffsetWardrobeArray + i * kWardrobeRecordStride;
        const WardrobeItemRecord& r = f.wardrobeItems[i];
        putU32LE(b, o, r.itemId);
        b[o + 4] = r.colour0;
        b[o + 5] = r.colour1;
        b[o + 6] = r.colour2;
        uint8_t packed = static_cast<uint8_t>((r.wearOption & 0x0Fu) | (r.flag ? 0x10u : 0u) | ((r.spareBits & 0x07u) << 5));
        b[o + 7] = packed;
    }

    if (f.weaponInventory.size() > kWeaponInventoryCapacity) {
        throw FormatError("SaveSnapshotFields::weaponInventory has " + std::to_string(f.weaponInventory.size()) +
                           " entries, over the real " + std::to_string(kWeaponInventoryCapacity) +
                           "-record capacity (Sec10.3)");
    }
    putU32LE(b, kOffsetWeaponInventoryCount, static_cast<uint32_t>(f.weaponInventory.size()));
    for (size_t i = 0; i < f.weaponInventory.size(); ++i) {
        size_t o = kOffsetWeaponInventoryArray + i * kWeaponInventoryRecordStride;
        const WeaponInventoryRecord& r = f.weaponInventory[i];
        putU32LE(b, o, r.weaponDefId);
        putU32LE(b, o + 0x04, r.unused);
        putU32LE(b, o + 0x08, static_cast<uint32_t>(r.ammo[0]));
        putU32LE(b, o + 0x0C, static_cast<uint32_t>(r.ammo[1]));
        putU32LE(b, o + 0x10, static_cast<uint32_t>(r.ammo[2]));
        putU32LE(b, o + 0x14, static_cast<uint32_t>(r.ammo[3]));
        b[o + 0x18] = r.flag0;
        b[o + 0x19] = r.flag1;
        b[o + 0x1A] = r.pad[0];
        b[o + 0x1B] = r.pad[1];
    }

    if (f.regionRecords.size() > kRegionRecordCapacity) {
        throw FormatError("SaveSnapshotFields::regionRecords has " + std::to_string(f.regionRecords.size()) +
                           " entries, over the real " + std::to_string(kRegionRecordCapacity) +
                           "-record room before 0x11D80 (Sec12.4.1)");
    }
    putU32LE(b, kOffsetRegionCount, static_cast<uint32_t>(f.regionRecords.size()));
    for (size_t i = 0; i < f.regionRecords.size(); ++i) {
        size_t o = kOffsetRegionArray + i * kRegionRecordStride;
        const RegionRecord& r = f.regionRecords[i];
        putU32LE(b, o, r.handleLo);
        putU32LE(b, o + 4, r.handleHi);
        b[o + 8] = r.controlled ? 1 : 0;
        for (size_t k = 0; k < 7; ++k) b[o + 9 + k] = r.tail[k];
    }

    if (f.activityRecords.size() > kActivityTableCapacity) {
        throw FormatError("SaveSnapshotFields::activityRecords has " + std::to_string(f.activityRecords.size()) +
                           " entries, over the real 64-record capacity (Sec9.3a)");
    }
    putU32LE(b, kOffsetActivityTable + kActivityTableCountOffset, static_cast<uint32_t>(f.activityRecords.size()));
    for (size_t i = 0; i < f.activityRecords.size(); ++i) {
        size_t o = kOffsetActivityTable + kActivityTableRecordsOffset + i * 12;
        const ActivityRecord& r = f.activityRecords[i];
        putU32LE(b, o, r.nameHash);
        putU32LE(b, o + 4, r.levelsCompleted);
        b[o + 8] = r.completedMask;
        b[o + 9] = r.secondMask;
        b[o + 10] = r.pad[0];
        b[o + 11] = r.pad[1];
    }
    putU32LE(b, kOffsetActivityMarkerBits, static_cast<uint32_t>(f.activityMarkerFlags & 0xFFFFFFFFu));
    putU32LE(b, kOffsetActivityMarkerBits + 4, static_cast<uint32_t>(f.activityMarkerFlags >> 32));

    for (size_t i = 0; i < kSnapshotStatCount; ++i) {
        putU32LE(b, kOffsetStatistics + 8 * i, f.statistics[i].rawValue);
        putU32LE(b, kOffsetStatistics + 8 * i + 4, f.statistics[i].denominatorStatId);
    }

    if (f.activeCheatIds.size() > kCheatIdCapacity) {
        throw FormatError("SaveSnapshotFields::activeCheatIds has " + std::to_string(f.activeCheatIds.size()) +
                           " entries, over the real 200-id capacity (Sec9.9)");
    }
    putU32LE(b, kOffsetCheatCount, static_cast<uint32_t>(f.activeCheatIds.size()));
    for (size_t i = 0; i < f.activeCheatIds.size(); ++i) {
        putU32LE(b, kOffsetCheatIds + 4 * i, f.activeCheatIds[i]);
    }

    // Sec6.1/Sec6.3: the hash is stamped on save over file[0x004:0x1A8E8]
    // followed by a 0x118-byte zero tail this project's buffer never
    // carries (the on-disk size IS kSaveSnapshotSize, so computeSnapshotHash
    // already applies that tail internally). Sec11.2's checkpoint mode
    // never hashes at all (the real "no hash" buffer just stays at the
    // zero-fill offset 0x000 already has).
    if (computeHash) {
        uint32_t hash = computeSnapshotHash(ByteView(b.data(), b.size()));
        putU32LE(b, kOffsetChecksum, hash);
    }
    return b;
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
