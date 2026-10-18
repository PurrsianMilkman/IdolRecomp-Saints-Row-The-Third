// Snapshot blocks the free-roam load/save flow needs beyond what
// sr3save::SaveSnapshot decodes: the game clock, the 256-bit flag set, the
// respawn-crib handle, the respect multiplier, the crib block and the
// mission-object table (spec-save-format.md 6.2, 10.3/10.6, 10.5, 9.3(c) and
// "Free-roam load and save flow"; clock layout spec-lua-api-behaviour.md
// 48.1). Each block reads from and patches into a full snapshot image, so a
// save can be written over a loaded image without touching any span this
// project does not model.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sr3saveflow {

// ---------------------------------------------------------------------------
// Offsets (snapshot file)
// ---------------------------------------------------------------------------
constexpr size_t kOffsetZoneName = 0x014;          // NUL-terminated, room to 0x058
constexpr size_t kZoneNameRoom = 0x058 - 0x014;
constexpr size_t kOffsetRespawnCrib = 0x058;       // 8-byte crib handle
constexpr size_t kOffsetClock = 0x08C;             // 20-byte clock struct
constexpr size_t kOffsetPlayTimeSeconds = 0x0A0;   // u32, trunc of the clock's play float
constexpr size_t kOffsetFlagSet = 0x0A4;           // 256-bit flag set
constexpr size_t kFlagSetBytes = 32;
constexpr size_t kOffsetMissionObjectCount = 0x37E8;
constexpr size_t kOffsetMissionObjects = 0x37F0;   // 16-byte records
constexpr size_t kMissionObjectCapacity = 128;
constexpr size_t kOffsetRespectTotal = 0x4418;
constexpr size_t kOffsetRespectInLevel = 0x441C;
constexpr size_t kOffsetRespectLevel = 0x4420;
constexpr size_t kOffsetRespectMultiplier = 0x4A5C;  // float32
constexpr size_t kOffsetCash = 0x4A70;               // int32 cents
constexpr size_t kOffsetCribsVariable = 0x4E60;      // u16, script variable "cribs"
constexpr size_t kOffsetCribCount = 0x4E62;          // u16
constexpr size_t kOffsetCribRecords = 0x4E68;        // 24-byte records
constexpr size_t kCribRecordStride = 24;
constexpr size_t kCribCapacity = 10;                 // records end at 0x4F58
constexpr size_t kOffsetCribStash = 0x4F58;          // u32

constexpr int32_t kCashClamp = 2000000000;           // +- raw cents

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------

// An 8-byte object handle as stored: the u32 at +0 and the u32 at +4.
struct ObjectHandle {
    uint32_t lo = 0;
    uint32_t hi = 0;
    bool isZero() const { return lo == 0 && hi == 0; }
    bool operator==(const ObjectHandle& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const ObjectHandle& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Game clock (20 bytes)
// ---------------------------------------------------------------------------

// year u16 +0; month 0-based +2; day of month +3; hour/minute/second
// +4/+5/+6; weekday 0 = Monday +7; the 30-day moon counter at +8 (its
// width is not stated; the four bytes +8..+0xB are kept verbatim); the
// authoritative time of day +0xC (u32, 2^32 units per day; hour, minute
// and second are derived from it); the real-seconds play-time float +0x10.
struct GameClock {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;
    std::array<uint8_t, 4> moonRaw{};
    uint32_t timeOfDayFixed = 0;
    float playTime = 0.0f;

    static GameClock decode(const uint8_t* bytes20);
    void encode(uint8_t* bytes20) const;
};

// floor(seconds_of_day * 2^32 / 86400); e.g. 14:30:00 -> 0x9AAAAAAA.
uint32_t TimeOfDayFixed(int hour, int minute, int second);
double TimeOfDaySeconds(uint32_t fixed);
// Sets hour/minute/second and the fixed-point value together.
void SetClockTimeOfDay(GameClock& clock, int hour, int minute, int second);

// ---------------------------------------------------------------------------
// Cribs (entry 9)
// ---------------------------------------------------------------------------

struct CribRecord {
    ObjectHandle handle;
    bool locked = true;      // byte +8 bit 0: 1 = not yet owned
    uint8_t flagByteRaw = 1; // the whole +8 byte, other bits kept
    uint32_t a = 0;          // +0x0C: field + 1 (0 = never unlocked)
    uint32_t b = 0;          // +0x10: current interior level + 1 (0 = none)
};

struct CribBlock {
    uint16_t cribsVariable = 0;  // the "cribs" script variable
    std::vector<CribRecord> records;  // pool order; count = records.size()
    uint32_t stash = 0;          // crib money stash, clamped [0, 2e9] by the game
};

// The ten crib objects of sr3_city, in the save's pool order, with their
// zone-record handles and positions (spec-save-format.md A.3).
struct KnownCrib {
    const char* name;
    ObjectHandle handle;
    double x, y, z;
};
extern const std::array<KnownCrib, kCribCapacity> kKnownCribs;
// Index into kKnownCribs, or -1.
int KnownCribIndex(const ObjectHandle& h);

// ---------------------------------------------------------------------------
// Mission objects (entry 6, 0x37E8)
// ---------------------------------------------------------------------------

struct MissionObjectRecord {
    ObjectHandle handle;
    uint32_t flags = 0;  // +8
};

// ---------------------------------------------------------------------------
// Reading and patching a full snapshot image
// ---------------------------------------------------------------------------

// The fields above, read from a full snapshot image (kSaveSnapshotSize
// bytes). Throws sr3save::FormatError on a wrong size, a zone name with no
// terminator, or a crib/mission-object count past its capacity.
struct SnapshotBlocks {
    std::string zoneName;
    ObjectHandle respawnCrib;
    GameClock clock;
    uint32_t playTimeSeconds = 0;
    std::array<uint8_t, kFlagSetBytes> flagSet{};
    std::vector<MissionObjectRecord> missionObjects;
    uint32_t respectTotal = 0;
    uint32_t respectInLevel = 0;
    uint32_t respectLevel = 0;
    float respectMultiplier = 0.0f;
    int32_t cashRaw = 0;
    CribBlock cribs;

    static SnapshotBlocks read(const std::vector<uint8_t>& image);
};

void WriteZoneName(std::vector<uint8_t>& image, const std::string& zone);
void WriteRespawnCrib(std::vector<uint8_t>& image, const ObjectHandle& h);
// Writes the clock at 0x08C (its play float lands at 0x09C) and 0x0A0 =
// the play float truncated toward zero.
void WriteClock(std::vector<uint8_t>& image, const GameClock& clock);
void WriteRespect(std::vector<uint8_t>& image, uint32_t total, uint32_t inLevel, uint32_t level);
void WriteRespectMultiplier(std::vector<uint8_t>& image, float multiplier);
// Cash in raw cents through the +-2,000,000,000 clamp.
void WriteCash(std::vector<uint8_t>& image, int64_t cashRaw);
void WriteCribBlock(std::vector<uint8_t>& image, const CribBlock& cribs);
// Stamps the snapshot CRC at 0x000.
void StampSnapshotHash(std::vector<uint8_t>& image);

int32_t ClampCash(int64_t raw);

}  // namespace sr3saveflow
