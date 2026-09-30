#pragma once

// Reader for sr3def_profile (spec-save-format.md Sec7, RESOLVED; supersedes
// the struck-through Sec4). Header-only so the sr3save library needs no new
// translation unit.
//
// What the format is (Sec7): NOT a tagged record stream at file level. It is
// a raw memory image of one 16,968-byte (0x4248) profile object, written and
// read with a single plain copy. There is no checksum, hash, encryption or
// compression on this file. Five regions tile the file exactly:
//
//   0x0000 0x1000  A0  stat chunk 0   length-prefixed stat records
//   0x1000 0x1000  A1  stat chunk 1   (empty in every real file)
//   0x2000 0x0400  B   settings block (magic 0x10, options, ...)
//   0x2400 0x0C00      reserved (zero)
//   0x3000 0x0028  H   header
//   0x3028 0x0CD0  T-A 164 x 20-byte input-binding records
//   0x3CF8 0x0550  T-B  34 x 40-byte input-binding records   (ends at 0x4248)
//
// The stat stream (Sec7.4): chunk c starts at 0x1000*c and is `used_c` bytes
// long (u32 in the header at +4*c). Records are back to back,
// [u8 length L][L payload bytes]; ONE record per *serialisable* stat, in
// ascending stat-id order. Ids 0..216 exist; the 38 ids listed in
// kProfileStatsWithoutRecord have no record, leaving 179 records - exactly
// what real files hold. The payload is a 4-byte value (int32 or float32 by
// stat type - the typing lives in game data, NOT in the spec, so this reader
// exposes the raw dword and lets the caller pick), except id 45 whose record
// is empty (L = 0). The leading byte is a LENGTH prefix, not a type tag; the
// single zero-length record is why a fixed 5-byte stride desyncs at record
// index 20 (file offset 0x64).
//
// Everything the spec marks HYPOTHESIS or OPEN is exposed only as a raw
// value under an explicitly non-committal name.

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "sr3save/errors.h"
#include "vpp/byte_view.h"

namespace sr3save {

using vpp::ByteView;

// ---- file layout constants (Sec7.2, CONFIRMED - disassembly + empirical) ----
constexpr size_t kProfileSize = 0x4248;           // 16,968
constexpr size_t kProfileStatChunkSize = 0x1000;
constexpr size_t kProfileStatChunk0 = 0x0000;
constexpr size_t kProfileStatChunk1 = 0x1000;
constexpr size_t kProfileSettingsOffset = 0x2000;
constexpr size_t kProfileSettingsSize = 0x0400;
constexpr size_t kProfileReservedOffset = 0x2400;
constexpr size_t kProfileReservedSize = 0x0C00;
constexpr size_t kProfileHeaderOffset = 0x3000;
constexpr size_t kProfileHeaderSize = 0x0028;
constexpr size_t kProfileTableAOffset = 0x3028;
constexpr size_t kProfileTableARecordSize = 20;
constexpr uint32_t kProfileTableACount = 164;     // 0xA4
constexpr size_t kProfileTableBOffset = 0x3CF8;
constexpr size_t kProfileTableBRecordSize = 40;
constexpr uint32_t kProfileTableBCount = 34;      // 0x22

constexpr uint32_t kProfileSettingsMagic = 0x10;  // u32 at 0x2000
constexpr uint32_t kProfileOptionsVersion = 5;    // u32 at 0x2004
constexpr uint32_t kProfileTableVersion = 5;      // u32 at 0x301C

constexpr uint32_t kProfileStatIdCount = 217;     // ids 0..216 (Sec7.4)
constexpr uint32_t kProfileStatIdNoPayload = 45;  // "unique vehicles owned": empty record (Sec7.4)

// Stat ids that have NO record in the stream (Sec7.4, derived by the spec team
// from the handler table: handler types without a serialise slot).
// 18..41 (24 ids), 43, 50, 51, 58..61, 66, 67, 69, 126, 150, 163, 164 = 38.
inline bool profileStatHasRecord(uint32_t id) {
    if (id >= kProfileStatIdCount) return false;
    if (id >= 18 && id <= 41) return false;
    if (id >= 58 && id <= 61) return false;
    switch (id) {
    case 43: case 50: case 51: case 66: case 67: case 69: case 126: case 150: case 163: case 164:
        return false;
    default:
        return true;
    }
}

// The ids that carry a record, ascending (179 of them).
inline std::vector<uint32_t> profileSerialisedStatIds() {
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i < kProfileStatIdCount; ++i) {
        if (profileStatHasRecord(i)) ids.push_back(i);
    }
    return ids;
}

// Expected payload length of a record for a stat id (Sec7.4): 0 for id 45,
// otherwise 4 (a boolean-typed stat would be 1, but none of the 179 in real
// files is boolean-typed and the boolean ids are not in the spec).
inline uint32_t profileExpectedRecordLength(uint32_t id) {
    return id == kProfileStatIdNoPayload ? 0u : 4u;
}

// One [len][payload] record located in a stat chunk.
struct ProfileStatSpan {
    size_t fileOffset = 0; // offset of the length byte
    uint8_t length = 0;    // payload length L
};

// Result of walking one stat chunk (Sec7.4 framing).
struct ProfileChunkWalk {
    std::vector<ProfileStatSpan> spans;
    size_t consumed = 0;     // bytes consumed by whole records
    bool exact = false;      // consumed == used and no record ran past `used`
    bool overrun = false;    // a record's length ran past `used`
};

// Walks chunk `chunkBase` (0 or 0x1000) of `bytes` for `used` bytes.
// Bytes after `used` are never read (Sec7.4). used > 0xFFF is refused (the
// writer starts a new chunk when used + 1 + L > 0xFFF).
inline ProfileChunkWalk walkProfileStatChunk(ByteView bytes, size_t chunkBase, uint32_t used) {
    ProfileChunkWalk w;
    if (used > 0xFFFu || chunkBase + used > bytes.size()) {
        w.overrun = true;
        return w;
    }
    size_t pos = 0;
    while (pos < used) {
        uint8_t len = bytes.at(chunkBase + pos);
        if (pos + 1 + len > used) {
            w.overrun = true;
            break;
        }
        w.spans.push_back({chunkBase + pos, len});
        pos += 1 + len;
    }
    w.consumed = pos;
    w.exact = !w.overrun && pos == used;
    return w;
}

// A stat record assigned to its stat id.
struct ProfileStatRecord {
    uint32_t statId = 0;       // the stat's id (0..216)
    size_t fileOffset = 0;     // offset of the length byte
    uint8_t length = 0;        // payload length as stored
    uint32_t rawDword = 0;     // payload as little-endian u32 when length == 4, else 0
    bool hasDword = false;     // length == 4
    // Payload reinterpreted as float32 (only meaningful for float-typed stats;
    // the typing is game data, not spec - the caller decides).
    float asFloat() const {
        float f;
        std::memcpy(&f, &rawDword, sizeof f);
        return f;
    }
    int32_t asInt() const { return static_cast<int32_t>(rawDword); }
};

// Header at 0x3000 (Sec7.3).
struct ProfileHeader {
    uint32_t used0 = 0;             // 0x3000: bytes in use in chunk 0 (0x37B in all real files)
    uint32_t used1 = 0;             // 0x3004: bytes in use in chunk 1 (0 in all real files)
    uint32_t settingsBlockSize = 0; // 0x3008: 0x400; 0 means "no profile data present"
    uint32_t settingsPointerRaw = 0; // 0x300C: runtime self-pointer - NOT meaningful in a file; ignore
    uint64_t timeStamp = 0;         // 0x3010: 0 in practice
    uint8_t profileValid = 0;       // 0x3018: tables ignored if 0
    uint8_t dirty = 0;              // 0x3019: always 1 in a saved file
    uint8_t rebuildSettings = 0;    // 0x301A: 0 in a saved file
    uint8_t padding = 0;            // 0x301B
    uint32_t tableVersion = 0;      // 0x301C: must be 5 for the tables to apply
    uint32_t tableACount = 0;       // 0x3020: must be 0xA4
    uint32_t tableBCount = 0;       // 0x3024: must be 0x22
};

// Options struct at 0x2004 (25 dwords, Sec7.5). Named where the spec labels the
// field [CONFIRMED - disassembly]; the rest are raw and explicitly unlabelled.
struct ProfileOptions {
    uint32_t formatVersion = 0;   // +0x00, must be 5
    uint8_t flagsA = 0;           // +0x04: bit0 invert look Y, bit1 invert rotation, bit2 (unnamed),
                                  //        bit3 vibration, bit4 crouch, bit5 (unnamed UI toggle)
    int32_t item08Open = 0;       // +0x08: constrained to 3 or 4; meaning OPEN
    float lookSensitivityH = 0;   // +0x0C
    float lookSensitivityV = 0;   // +0x10
    int32_t controlPresetList1 = 0; // +0x14
    int32_t controlPresetList2 = 0; // +0x18
    float gamma = 0;              // +0x1C (clamped to [0,1] on apply)
    uint8_t subtitles = 0;        // +0x20
    uint8_t staticMinimap = 0;    // +0x21
    uint8_t gameplayVsync = 0;    // +0x22
    uint8_t cutsceneVsyncByte = 0; // +0x23: NOT boolean-valued in real files (0xF3/0x33/0x13); OPEN
    float brightness = 0;         // +0x24
    std::array<float, 7> audioVolume{}; // +0x28..+0x40: channel 0..6 (1 music, 2 voice, 3-5 SFX, 0/6 unlabelled)
    int32_t radio = 0;            // +0x44 ("radio on")
    int32_t audioItem7Open = 0;   // +0x48: unlabelled, OPEN
    uint8_t flagsMouse = 0;       // +0x4C: bit0 pause on focus loss, bit1 mouse invert Y, bit2 mouse invert rotation
    float mouseSensitivityX = 0;  // +0x50 (internal value)
    float mouseSensitivityY = 0;  // +0x54
    int32_t mouseItem5Open = 0;   // +0x58: unlabelled, OPEN
    int32_t item5COpen = 0;       // +0x5C: no consumer other than collect/apply, OPEN
    uint8_t flagsVehicle = 0;     // +0x60: bit0 vehicle camera snap, bit1 airplane controls

    bool invertLookY() const { return (flagsA & 0x01) != 0; }
    bool invertRotation() const { return (flagsA & 0x02) != 0; }
    bool vibration() const { return (flagsA & 0x08) != 0; }
    bool crouchSetting() const { return (flagsA & 0x10) != 0; }
    bool pauseOnFocusLoss() const { return (flagsMouse & 0x01) != 0; }
    bool mouseInvertY() const { return (flagsMouse & 0x02) != 0; }
    bool mouseInvertRotation() const { return (flagsMouse & 0x04) != 0; }
    bool vehicleCameraSnap() const { return (flagsVehicle & 0x01) != 0; }
    bool airplaneControls() const { return (flagsVehicle & 0x02) != 0; }
};

// Table A record (Sec7.6): { action index, key code 1, key code 2, mouse button 1, mouse button 2 }.
struct ProfileBindingA {
    int32_t actionIndex = 0;
    int32_t keyCode1 = 0;
    int32_t keyCode2 = 0;
    int32_t mouseButton1 = 0; // -1 = none, 0..6 = mouse button index
    int32_t mouseButton2 = 0;

    // The loader accepts a record only if both key codes are < 0x100 and both
    // mouse selectors are -1 or < 7 (signed compare: any negative passes); a
    // completely-zero record is skipped (Sec7.6).
    bool loaderRanges() const {
        return static_cast<uint32_t>(keyCode1) < 0x100u && static_cast<uint32_t>(keyCode2) < 0x100u &&
               mouseButton1 < 7 && mouseButton2 < 7;
    }
    bool allZero() const {
        return actionIndex == 0 && keyCode1 == 0 && keyCode2 == 0 && mouseButton1 == 0 && mouseButton2 == 0;
    }
};

// Table B record (Sec7.6): { index, four key codes, four mouse selectors, direction }.
// File order f1..f8 is k1 k2 k3 k4 m1 m2 m3 m4; which key pairs with which
// button in the runtime slot is OPEN (Sec7.9 item 4), so the fields are
// exposed in file order only.
struct ProfileBindingB {
    int32_t index = 0;
    std::array<int32_t, 4> keyCodes{};       // each < 0x100
    std::array<int32_t, 4> mouseSelectors{}; // each -1 or < 7
    int32_t direction = 0;                   // -1, 0 or 1
    bool rangesOk() const {
        for (int32_t k : keyCodes) {
            if (static_cast<uint32_t>(k) >= 0x100u) return false;
        }
        for (int32_t m : mouseSelectors) {
            if (m != -1 && !(m >= 0 && m < 7)) return false;
        }
        return direction >= -1 && direction <= 1;
    }
};

class SaveProfile {
public:
    // Parses `bytes`, which must be exactly kProfileSize bytes (Sec7.7, the
    // only hard requirement; everything else is reported through the
    // *Ok()/consumption accessors so a caller can see exactly which gate
    // failed instead of getting one opaque exception).
    static SaveProfile parse(ByteView bytes) {
        if (bytes.size() != kProfileSize) {
            throw FormatError("sr3def_profile must be exactly 16968 (0x4248) bytes (spec Sec7.2); got " +
                              std::to_string(bytes.size()) + " bytes");
        }
        SaveProfile p;
        p.readHeader(bytes);
        p.readSettings(bytes);
        p.readStats(bytes);
        p.readTables(bytes);
        return p;
    }

    const ProfileHeader& header() const { return header_; }

    // ---- gates (each is a plain condition from Sec7.3/Sec7.5/Sec7.7) -------
    bool settingsMagicOk() const { return settingsMagic_ == kProfileSettingsMagic; } // else the game re-initialises
    uint32_t settingsMagic() const { return settingsMagic_; }
    bool optionsApplied() const { return options_.formatVersion == kProfileOptionsVersion; } // else ignored
    bool tablesApplied() const {
        return header_.profileValid != 0 && header_.tableVersion == kProfileTableVersion &&
               header_.tableACount == kProfileTableACount && header_.tableBCount == kProfileTableBCount;
    }
    // Chunk-0/1 records consume exactly `used` bytes.
    bool chunk0Exact() const { return chunk0_.exact; }
    bool chunk1Exact() const { return chunk1_.exact; }
    // Number of records found across both chunks equals the 179 serialisable ids.
    bool recordCountMatchesIds() const {
        return chunk0_.spans.size() + chunk1_.spans.size() == profileSerialisedStatIds().size();
    }
    const ProfileChunkWalk& chunk0Walk() const { return chunk0_; }
    const ProfileChunkWalk& chunk1Walk() const { return chunk1_; }

    // True only if the framing is exact in both chunks, the record count equals
    // the number of serialisable ids, and every record's length matches its
    // stat's serialiser. The game discards *all* persisted statistics if any
    // record fails (Sec7.4 loader behaviour), so statRecords() are only
    // trustworthy when this is true.
    bool statsConsistent() const { return statsConsistent_; }

    // Records assigned to ids (empty unless the count matched 179, because the
    // id assignment is positional and unreliable otherwise).
    const std::vector<ProfileStatRecord>& statRecords() const { return records_; }
    const ProfileStatRecord* findStat(uint32_t id) const {
        for (const auto& r : records_) {
            if (r.statId == id) return &r;
        }
        return nullptr;
    }

    const ProfileOptions& options() const { return options_; }
    // 0x2068: the session setting (values 0/1/2 in real files). Its meaning is a
    // HYPOTHESIS in the spec (3-level difficulty) - exposed raw, unnamed.
    uint32_t sessionSettingRaw() const { return sessionSettingRaw_; }
    // 0x206C: co-op friendly fire (CONFIRMED - disassembly).
    uint32_t friendlyFire() const { return friendlyFire_; }
    // 0x2400..0x2FFF is reserved and zero-filled by the game; 0x21CC is a
    // runtime-only flag forced to 0 by the game's copy routine.
    bool reservedRegionAllZero() const { return reservedZero_; }

    const std::vector<ProfileBindingA>& tableA() const { return tableA_; }
    const std::vector<ProfileBindingB>& tableB() const { return tableB_; }
    // Table A record i has index i (164/164 in real files).
    uint32_t tableAIndexMatches() const { return tableAIndexMatches_; }
    uint32_t tableBIndexMatches() const { return tableBIndexMatches_; }
    uint32_t tableALoaderAccepts() const { return tableAAccepts_; }
    uint32_t tableBRangesOk() const { return tableBRangesOk_; }

private:
    static uint32_t rd32(ByteView b, size_t o) { return b.readU32LE(o); }
    static int32_t rdI32(ByteView b, size_t o) { return static_cast<int32_t>(b.readU32LE(o)); }
    static float rdF32(ByteView b, size_t o) {
        uint32_t v = b.readU32LE(o);
        float f;
        std::memcpy(&f, &v, sizeof f);
        return f;
    }

    void readHeader(ByteView b) {
        const size_t h = kProfileHeaderOffset;
        header_.used0 = rd32(b, h + 0x00);
        header_.used1 = rd32(b, h + 0x04);
        header_.settingsBlockSize = rd32(b, h + 0x08);
        header_.settingsPointerRaw = rd32(b, h + 0x0C);
        header_.timeStamp = static_cast<uint64_t>(rd32(b, h + 0x10)) | (static_cast<uint64_t>(rd32(b, h + 0x14)) << 32);
        header_.profileValid = b.at(h + 0x18);
        header_.dirty = b.at(h + 0x19);
        header_.rebuildSettings = b.at(h + 0x1A);
        header_.padding = b.at(h + 0x1B);
        header_.tableVersion = rd32(b, h + 0x1C);
        header_.tableACount = rd32(b, h + 0x20);
        header_.tableBCount = rd32(b, h + 0x24);
    }

    void readSettings(ByteView b) {
        settingsMagic_ = rd32(b, kProfileSettingsOffset);
        const size_t o = kProfileSettingsOffset + 4; // options struct at 0x2004
        options_.formatVersion = rd32(b, o + 0x00);
        options_.flagsA = b.at(o + 0x04);
        options_.item08Open = rdI32(b, o + 0x08);
        options_.lookSensitivityH = rdF32(b, o + 0x0C);
        options_.lookSensitivityV = rdF32(b, o + 0x10);
        options_.controlPresetList1 = rdI32(b, o + 0x14);
        options_.controlPresetList2 = rdI32(b, o + 0x18);
        options_.gamma = rdF32(b, o + 0x1C);
        options_.subtitles = b.at(o + 0x20);
        options_.staticMinimap = b.at(o + 0x21);
        options_.gameplayVsync = b.at(o + 0x22);
        options_.cutsceneVsyncByte = b.at(o + 0x23);
        options_.brightness = rdF32(b, o + 0x24);
        for (size_t i = 0; i < 7; ++i) options_.audioVolume[i] = rdF32(b, o + 0x28 + 4 * i);
        options_.radio = rdI32(b, o + 0x44);
        options_.audioItem7Open = rdI32(b, o + 0x48);
        options_.flagsMouse = b.at(o + 0x4C);
        options_.mouseSensitivityX = rdF32(b, o + 0x50);
        options_.mouseSensitivityY = rdF32(b, o + 0x54);
        options_.mouseItem5Open = rdI32(b, o + 0x58);
        options_.item5COpen = rdI32(b, o + 0x5C);
        options_.flagsVehicle = b.at(o + 0x60);
        sessionSettingRaw_ = rd32(b, 0x2068);
        friendlyFire_ = rd32(b, 0x206C);
        reservedZero_ = true;
        for (size_t i = 0; i < kProfileReservedSize; ++i) {
            if (b.at(kProfileReservedOffset + i) != 0) {
                reservedZero_ = false;
                break;
            }
        }
    }

    void readStats(ByteView b) {
        chunk0_ = walkProfileStatChunk(b, kProfileStatChunk0, header_.used0);
        chunk1_ = walkProfileStatChunk(b, kProfileStatChunk1, header_.used1);
        std::vector<uint32_t> ids = profileSerialisedStatIds();
        size_t total = chunk0_.spans.size() + chunk1_.spans.size();
        statsConsistent_ = chunk0_.exact && chunk1_.exact && total == ids.size();
        if (total != ids.size()) return; // positional id assignment would be a guess
        size_t k = 0;
        for (const ProfileChunkWalk* w : {&chunk0_, &chunk1_}) {
            for (const auto& s : w->spans) {
                ProfileStatRecord r;
                r.statId = ids[k++];
                r.fileOffset = s.fileOffset;
                r.length = s.length;
                if (s.length == 4) {
                    r.rawDword = rd32(b, s.fileOffset + 1);
                    r.hasDword = true;
                }
                if (s.length != profileExpectedRecordLength(r.statId)) statsConsistent_ = false;
                records_.push_back(r);
            }
        }
    }

    void readTables(ByteView b) {
        for (uint32_t i = 0; i < kProfileTableACount; ++i) {
            size_t o = kProfileTableAOffset + i * kProfileTableARecordSize;
            ProfileBindingA r;
            r.actionIndex = rdI32(b, o + 0);
            r.keyCode1 = rdI32(b, o + 4);
            r.keyCode2 = rdI32(b, o + 8);
            r.mouseButton1 = rdI32(b, o + 12);
            r.mouseButton2 = rdI32(b, o + 16);
            if (r.actionIndex == static_cast<int32_t>(i)) ++tableAIndexMatches_;
            if (r.loaderRanges()) ++tableAAccepts_;
            tableA_.push_back(r);
        }
        for (uint32_t i = 0; i < kProfileTableBCount; ++i) {
            size_t o = kProfileTableBOffset + i * kProfileTableBRecordSize;
            ProfileBindingB r;
            r.index = rdI32(b, o + 0);
            for (size_t k = 0; k < 4; ++k) r.keyCodes[k] = rdI32(b, o + 4 + 4 * k);
            for (size_t k = 0; k < 4; ++k) r.mouseSelectors[k] = rdI32(b, o + 20 + 4 * k);
            r.direction = rdI32(b, o + 36);
            if (r.index == static_cast<int32_t>(i)) ++tableBIndexMatches_;
            if (r.rangesOk()) ++tableBRangesOk_;
            tableB_.push_back(r);
        }
    }

    ProfileHeader header_;
    uint32_t settingsMagic_ = 0;
    ProfileOptions options_;
    uint32_t sessionSettingRaw_ = 0;
    uint32_t friendlyFire_ = 0;
    bool reservedZero_ = false;
    ProfileChunkWalk chunk0_, chunk1_;
    bool statsConsistent_ = false;
    std::vector<ProfileStatRecord> records_;
    std::vector<ProfileBindingA> tableA_;
    std::vector<ProfileBindingB> tableB_;
    uint32_t tableAIndexMatches_ = 0, tableBIndexMatches_ = 0, tableAAccepts_ = 0, tableBRangesOk_ = 0;
};

} // namespace sr3save
