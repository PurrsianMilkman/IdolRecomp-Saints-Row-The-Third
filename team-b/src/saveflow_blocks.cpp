// Snapshot blocks for the free-roam load/save flow. See
// include/sr3saveflow/snapshot_blocks.h.

#include "sr3saveflow/snapshot_blocks.h"

#include <cmath>
#include <cstring>

#include "sr3save/errors.h"
#include "sr3save/save_crc.h"
#include "sr3save/save_snapshot.h"

namespace sr3saveflow {

namespace {

uint16_t RdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t RdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
float RdF32(const uint8_t* p) {
    const uint32_t u = RdU32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
void WrU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void WrU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void WrF32(uint8_t* p, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    WrU32(p, u);
}

void CheckImage(const std::vector<uint8_t>& image) {
    if (image.size() != sr3save::kSaveSnapshotSize)
        throw sr3save::FormatError("snapshot image must be exactly 0x1A8E8 bytes");
}

ObjectHandle RdHandle(const uint8_t* p) { return {RdU32(p), RdU32(p + 4)}; }
void WrHandle(uint8_t* p, const ObjectHandle& h) {
    WrU32(p, h.lo);
    WrU32(p + 4, h.hi);
}

}  // namespace

// ---------------------------------------------------------------------------

GameClock GameClock::decode(const uint8_t* b) {
    GameClock c;
    c.year = RdU16(b + 0x00);
    c.month = b[0x02];
    c.day = b[0x03];
    c.hour = b[0x04];
    c.minute = b[0x05];
    c.second = b[0x06];
    c.weekday = b[0x07];
    std::memcpy(c.moonRaw.data(), b + 0x08, 4);
    c.timeOfDayFixed = RdU32(b + 0x0C);
    c.playTime = RdF32(b + 0x10);
    return c;
}

void GameClock::encode(uint8_t* b) const {
    WrU16(b + 0x00, year);
    b[0x02] = month;
    b[0x03] = day;
    b[0x04] = hour;
    b[0x05] = minute;
    b[0x06] = second;
    b[0x07] = weekday;
    std::memcpy(b + 0x08, moonRaw.data(), 4);
    WrU32(b + 0x0C, timeOfDayFixed);
    WrF32(b + 0x10, playTime);
}

uint32_t TimeOfDayFixed(int hour, int minute, int second) {
    const uint64_t secs = static_cast<uint64_t>(hour) * 3600u + static_cast<uint64_t>(minute) * 60u +
                          static_cast<uint64_t>(second);
    return static_cast<uint32_t>((secs << 32) / 86400u);
}

double TimeOfDaySeconds(uint32_t fixed) {
    return static_cast<double>(fixed) * 86400.0 / 4294967296.0;
}

void SetClockTimeOfDay(GameClock& clock, int hour, int minute, int second) {
    clock.hour = static_cast<uint8_t>(hour);
    clock.minute = static_cast<uint8_t>(minute);
    clock.second = static_cast<uint8_t>(second);
    clock.timeOfDayFixed = TimeOfDayFixed(hour, minute, second);
}

// ---------------------------------------------------------------------------

const std::array<KnownCrib, kCribCapacity> kKnownCribs = {{
    {"Crib_3Count", {0x013A0292u, 0x02C481D8u}, 1201.1115, 232.6758, 1449.1624},
    {"Crib_Angels", {0x0DBE7DC3u, 0x005780E4u}, 764.0321, 10.2656, 1000.6411},
    {"Plane_Hanger", {0x0E03AD1Eu, 0x003880E4u}, -1425.1514, 18.0781, -1209.8158},
    {"Dock_Purchase", {0x0E0084EAu, 0x003980E4u}, 431.2953, 8.0703, 936.4601},
    {"Crib_BDSM", {0x03A45BBDu, 0x02D081D8u}, -1171.9233, 92.8258, -558.0479},
    {"Crib_Zimos", {0x0E8B78B2u, 0x021E80E4u}, -1242.6135, 13.7072, 153.9828},
    {"Crib_Shaundis_Ex", {0x0E8AF45Du, 0x016480E4u}, 1267.2749, 30.7344, 111.7629},
    {"Crib_Kinzie", {0x0E80FE1Cu, 0x016280E4u}, 2125.9683, 5.1075, -271.3368},
    {"Crib_Nuke", {0x0E8D45FFu, 0x02C980E4u}, 1325.3776, 198.9453, -1213.6958},
    {"Crib_SaintsHQ", {0x0E998295u, 0x004280E4u}, 96.874, 145.75, 1053.672},
}};

int KnownCribIndex(const ObjectHandle& h) {
    for (size_t i = 0; i < kKnownCribs.size(); ++i)
        if (kKnownCribs[i].handle == h) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------

SnapshotBlocks SnapshotBlocks::read(const std::vector<uint8_t>& image) {
    CheckImage(image);
    const uint8_t* p = image.data();
    SnapshotBlocks s;

    size_t n = 0;
    while (n < kZoneNameRoom && p[kOffsetZoneName + n] != 0) ++n;
    if (n == kZoneNameRoom) throw sr3save::FormatError("zone name has no terminator");
    s.zoneName.assign(reinterpret_cast<const char*>(p + kOffsetZoneName), n);

    s.respawnCrib = RdHandle(p + kOffsetRespawnCrib);
    s.clock = GameClock::decode(p + kOffsetClock);
    s.playTimeSeconds = RdU32(p + kOffsetPlayTimeSeconds);
    std::memcpy(s.flagSet.data(), p + kOffsetFlagSet, kFlagSetBytes);

    const uint32_t mo = RdU32(p + kOffsetMissionObjectCount);
    if (mo > kMissionObjectCapacity) throw sr3save::FormatError("mission-object count past 128");
    for (uint32_t i = 0; i < mo; ++i) {
        const uint8_t* r = p + kOffsetMissionObjects + 16u * i;
        s.missionObjects.push_back({RdHandle(r), RdU32(r + 8)});
    }

    s.respectTotal = RdU32(p + kOffsetRespectTotal);
    s.respectInLevel = RdU32(p + kOffsetRespectInLevel);
    s.respectLevel = RdU32(p + kOffsetRespectLevel);
    s.respectMultiplier = RdF32(p + kOffsetRespectMultiplier);
    s.cashRaw = static_cast<int32_t>(RdU32(p + kOffsetCash));

    s.cribs.cribsVariable = RdU16(p + kOffsetCribsVariable);
    const uint16_t cc = RdU16(p + kOffsetCribCount);
    if (cc > kCribCapacity) throw sr3save::FormatError("crib count past 10");
    for (uint16_t i = 0; i < cc; ++i) {
        const uint8_t* r = p + kOffsetCribRecords + kCribRecordStride * i;
        CribRecord c;
        c.handle = RdHandle(r);
        c.flagByteRaw = r[8];
        c.locked = (r[8] & 1u) != 0;
        c.a = RdU32(r + 0x0C);
        c.b = RdU32(r + 0x10);
        s.cribs.records.push_back(c);
    }
    s.cribs.stash = RdU32(p + kOffsetCribStash);
    return s;
}

void WriteZoneName(std::vector<uint8_t>& image, const std::string& zone) {
    CheckImage(image);
    if (zone.size() >= kZoneNameRoom) throw sr3save::FormatError("zone name too long");
    std::memset(image.data() + kOffsetZoneName, 0, kZoneNameRoom);
    std::memcpy(image.data() + kOffsetZoneName, zone.data(), zone.size());
}

void WriteRespawnCrib(std::vector<uint8_t>& image, const ObjectHandle& h) {
    CheckImage(image);
    WrHandle(image.data() + kOffsetRespawnCrib, h);
}

void WriteClock(std::vector<uint8_t>& image, const GameClock& clock) {
    CheckImage(image);
    clock.encode(image.data() + kOffsetClock);
    const double t = std::trunc(static_cast<double>(clock.playTime));
    const uint32_t secs = t <= 0.0 ? 0u : static_cast<uint32_t>(t);
    WrU32(image.data() + kOffsetPlayTimeSeconds, secs);
}

void WriteRespect(std::vector<uint8_t>& image, uint32_t total, uint32_t inLevel, uint32_t level) {
    CheckImage(image);
    WrU32(image.data() + kOffsetRespectTotal, total);
    WrU32(image.data() + kOffsetRespectInLevel, inLevel);
    WrU32(image.data() + kOffsetRespectLevel, level);
}

void WriteRespectMultiplier(std::vector<uint8_t>& image, float multiplier) {
    CheckImage(image);
    WrF32(image.data() + kOffsetRespectMultiplier, multiplier);
}

int32_t ClampCash(int64_t raw) {
    if (raw > kCashClamp) return kCashClamp;
    if (raw < -kCashClamp) return -kCashClamp;
    return static_cast<int32_t>(raw);
}

void WriteCash(std::vector<uint8_t>& image, int64_t cashRaw) {
    CheckImage(image);
    WrU32(image.data() + kOffsetCash, static_cast<uint32_t>(ClampCash(cashRaw)));
}

void WriteCribBlock(std::vector<uint8_t>& image, const CribBlock& cribs) {
    CheckImage(image);
    if (cribs.records.size() > kCribCapacity) throw sr3save::FormatError("more than 10 cribs");
    uint8_t* p = image.data();
    WrU16(p + kOffsetCribsVariable, cribs.cribsVariable);
    WrU16(p + kOffsetCribCount, static_cast<uint16_t>(cribs.records.size()));
    // Records past the count are cleared; a written record keeps its
    // unmodelled bytes (+0x09..+0x0B, +0x14).
    for (size_t i = cribs.records.size(); i < kCribCapacity; ++i)
        std::memset(p + kOffsetCribRecords + kCribRecordStride * i, 0, kCribRecordStride);
    for (size_t i = 0; i < cribs.records.size(); ++i) {
        uint8_t* r = p + kOffsetCribRecords + kCribRecordStride * i;
        const CribRecord& c = cribs.records[i];
        WrHandle(r, c.handle);
        r[8] = static_cast<uint8_t>((c.flagByteRaw & ~1u) | (c.locked ? 1u : 0u));
        WrU32(r + 0x0C, c.a);
        WrU32(r + 0x10, c.b);
    }
    WrU32(p + kOffsetCribStash, cribs.stash);
}

void StampSnapshotHash(std::vector<uint8_t>& image) {
    CheckImage(image);
    const uint32_t h = sr3save::computeSnapshotHash(vpp::ByteView(image.data(), image.size()));
    WrU32(image.data(), h);
}

}  // namespace sr3saveflow
