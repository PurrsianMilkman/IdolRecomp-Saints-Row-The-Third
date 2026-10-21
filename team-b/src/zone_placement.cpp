#include "sr3zone/zone_placement.h"

#include <cmath>
#include <cstring>

namespace sr3zone {

static int16_t s16At(const ZoneHeaderRecord& r, size_t o) {
    return static_cast<int16_t>(static_cast<uint16_t>(r[o]) | (static_cast<uint16_t>(r[o + 1]) << 8));
}

std::array<float, 9> PlacementRotationRows(float a, float b, float c) {
    const float sa = std::sin(a), ca = std::cos(a);
    const float sb = std::sin(b), cb = std::cos(b);
    const float sc = std::sin(c), cc = std::cos(c);
    return {
        sa * sb * sc + cb * cc, ca * sb, sa * sb * cc - sc * cb,   // r0
        sa * sc * cb - sb * cc, ca * cb, sa * cb * cc + sc * sb,   // r1
        ca * sc,                -sa,     ca * cc,                  // r2
    };
}

std::array<float, 3> PlacementTransformPoint(const PlacedObject& o, const std::array<float, 3>& p) {
    std::array<float, 3> out;
    for (size_t j = 0; j < 3; ++j)
        out[j] = o.position[j] + p[0] * o.rows[j] + p[1] * o.rows[3 + j] + p[2] * o.rows[6 + j];
    return out;
}

bool BindPlacementName(const uint8_t* pool, size_t poolLen, uint16_t offset, std::string& meshName) {
    meshName.clear();
    if (offset >= poolLen) return false;
    if (offset != 0 && pool[offset - 1] != 0) return false;
    size_t end = offset;
    while (end < poolLen && pool[end] != 0) ++end;
    if (end >= poolLen) return false; // unterminated
    const std::string s(reinterpret_cast<const char*>(pool + offset), end - offset);
    static const char kExt[] = ".lmeshx";
    const size_t el = sizeof(kExt) - 1;
    if (s.size() <= el || s.compare(s.size() - el, el, kExt) != 0) return false;
    meshName = s.substr(0, s.size() - el);
    return true;
}

PlacedObject DecodePlacement(const ZoneHeaderRecord& rec, const std::array<float, 3>& origin,
                             const uint8_t* pool, size_t poolLen) {
    PlacedObject o;
    for (size_t i = 0; i < 3; ++i)
        o.position[i] = origin[i] + static_cast<float>(s16At(rec, i * 2)) * (1.0f / 64.0f);
    for (size_t i = 0; i < 3; ++i)
        o.angles[i] = static_cast<float>(s16At(rec, 6 + i * 2)) * (1.0f / 4096.0f);
    o.rows = PlacementRotationRows(o.angles[0], o.angles[1], o.angles[2]);
    o.bound = BindPlacementName(pool, poolLen, RecordNameIndex(rec), o.meshName);
    return o;
}

std::vector<PlacedObject> DecodeZonePlacements(vpp::ByteView czh) {
    const ZoneHeader zh = ZoneHeader::parse(czh);
    std::vector<PlacedObject> out;
    if (czh.size() < 0x20) return out;
    const size_t ntl = czh.readU32LE(4);
    const size_t poolLen = (0x20 + ntl <= czh.size()) ? ntl : 0;
    const uint8_t* pool = czh.data() + 0x20;
    out.reserve(zh.records().size());
    for (const auto& r : zh.records()) out.push_back(DecodePlacement(r, zh.headerOrigin(), pool, poolLen));
    return out;
}

} // namespace sr3zone
