#include "sr3zone/hood_volumes.h"

#include <cstring>
#include <optional>

#include "sr3save/save_crc.h"
#include "sr3zone/zone_objects.h"

namespace sr3zone {

namespace {

constexpr double kBehindEpsilon = 0.0001;  // Sec20.6.8: s < -0.0001 -> behind

std::optional<std::string> TextProp(const ObjectRecord& rec, uint32_t id) {
    const ObjectPropertyEntry* e = FindTailEntryById(rec, id);
    if (!e) return std::nullopt;
    size_t n = 0;
    while (n < e->data.size() && e->data[n] != 0) ++n;  // NUL-terminated string
    return std::string(reinterpret_cast<const char*>(e->data.data()), n);
}

float F32At(const std::vector<uint8_t>& b, size_t o) {
    float f;
    std::memcpy(&f, b.data() + o, 4);
    return f;
}
uint32_t U32At(const std::vector<uint8_t>& b, size_t o) {
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
uint16_t U16At(const std::vector<uint8_t>& b, size_t o) {
    uint16_t v;
    std::memcpy(&v, b.data() + o, 2);
    return v;
}

// Blob: u32 planeCount, u32 nodeCount, 8 runtime bytes, planes[], nodes[].
bool ParseTree(const std::vector<uint8_t>& b, HoodVolume& out) {
    if (b.size() < 16) return false;
    const uint32_t pc = U32At(b, 0), nc = U32At(b, 4);
    const size_t need = 16 + static_cast<size_t>(pc) * 16 + static_cast<size_t>(nc) * 6;
    if (pc > 4096 || nc > 8192 || need > b.size()) return false;
    out.planes.resize(pc);
    for (uint32_t i = 0; i < pc; ++i) {
        const size_t o = 16 + static_cast<size_t>(i) * 16;
        out.planes[i] = {F32At(b, o), F32At(b, o + 4), F32At(b, o + 8), F32At(b, o + 12)};
    }
    out.nodes.resize(nc);
    const size_t nbase = 16 + static_cast<size_t>(pc) * 16;
    for (uint32_t i = 0; i < nc; ++i) {
        const size_t o = nbase + static_cast<size_t>(i) * 6;
        out.nodes[i] = {U16At(b, o), U16At(b, o + 2), U16At(b, o + 4)};
        if (out.nodes[i].plane >= pc) return false;
    }
    return nc > 0;
}

}  // namespace

bool HoodVolume::Contains(double x, double y, double z) const {
    if (nodes.empty()) return false;
    size_t idx = 0;
    for (size_t guard = 0; guard <= nodes.size(); ++guard) {
        const Node& n = nodes[idx];
        const Plane& p = planes[n.plane];
        const double s = p.nx * x + p.ny * y + p.nz * z + p.d;
        const uint16_t next = s < -kBehindEpsilon ? n.behind : n.front;
        if (next == 0) return s >= -kBehindEpsilon;  // behind -> outside, front -> inside
        if (next >= nodes.size()) return false;
        idx = next;
    }
    return false;  // cycle in a malformed tree
}

std::vector<HoodVolume> LoadHoodVolumes(vpp::ByteView czn) {
    std::vector<HoodVolume> out;
    const uint32_t bspId = sr3save::nameHash("hood_bsp_tree");
    const uint32_t catId = sr3save::nameHash("hood_spawn_category");
    const uint32_t altId = sr3save::nameHash("hood_alt_spawn_category");
    for (const ObjectsSection& sec : ZoneObjects::locate(czn)) {
        for (const ObjectRecord& rec : sec.records) {
            const ObjectPropertyEntry* bsp = FindTailEntryById(rec, bspId);
            if (!bsp) continue;
            HoodVolume h;
            if (!ParseTree(bsp->data, h)) continue;
            h.name = rec.nameText.value_or(std::string());
            h.spawnCategory = TextProp(rec, catId).value_or(std::string());
            h.altSpawnCategory = TextProp(rec, altId).value_or(std::string());
            out.push_back(std::move(h));
        }
    }
    return out;
}

const HoodVolume* FindHood(const std::vector<HoodVolume>& hoods, double x, double y, double z) {
    for (const HoodVolume& h : hoods)
        if (h.Contains(x, y, z)) return &h;
    return nullptr;
}

}  // namespace sr3zone
