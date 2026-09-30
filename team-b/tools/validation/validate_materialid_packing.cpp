// Is the draw-range material field packed 16:16 on CHARACTER meshes too?
//
// Team A reports the vehicle draw-range material field is
//     material_id = field & 0xFFFF
//     submesh_idx = field >> 16
// and suggests masking to the low 16 bits UNCONDITIONALLY rather than
// branching per format - explicitly as a safety argument, not a
// measurement, since they have not checked whether the packing exists on
// `.ccmesh_pc`.
//
// That distinction matters here, because `sr3mesh` already reads this field
// as a plain u32 and HANDOFF §9.19/§9.24 built the whole material partition
// and texture binding on it. If any character mesh has non-zero high bits,
// shipped behaviour is wrong. If none do, masking is a provable no-op and
// safe to adopt for uniformity.
//
// So: count, per population, how many draw ranges carry a non-zero high
// half - and report the maximum low and high halves seen, because "always
// zero" and "never checked" look identical from the outside.
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3vehicle/vehicle.h"
#include "vpp/container.h"

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok ||
              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

struct Pop {
    int files = 0;
    long long ranges = 0, highNonZero = 0;
    uint32_t maxLow = 0, maxHigh = 0, maxRaw = 0;
    std::map<uint32_t, long long> highValues;
};
Pop g_char, g_veh;

void tally(Pop& pop, const sr3mesh::MeshBlock& mesh) {
    ++pop.files;
    for (const auto& group : mesh.drawGroups()) {
        for (const auto& r : group) {
            ++pop.ranges;
            const uint32_t raw = r.materialId;
            const uint32_t low = raw & 0xFFFFu;
            const uint32_t high = raw >> 16;
            if (high != 0) ++pop.highNonZero;
            if (low > pop.maxLow) pop.maxLow = low;
            if (high > pop.maxHigh) pop.maxHigh = high;
            if (raw > pop.maxRaw) pop.maxRaw = raw;
            if (pop.highValues.size() < 24) pop.highValues[high] += 1;
            else if (pop.highValues.count(high)) pop.highValues[high] += 1;
        }
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        const bool isMesh = endsWith(n, ".ccmesh_pc");
        const bool isCar = endsWith(n, ".ccar_pc");
        if (isMesh || isCar) {
            std::string gn = n;
            size_t d = gn.find_last_of('.');
            if (d != std::string::npos) gn[d + 1] = 'g';
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j)
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            if (gb.empty()) continue;
            try {
                size_t sub = 0;
                if (isCar) {
                    sr3vehicle::Vehicle v =
                        sr3vehicle::Vehicle::parse(vpp::ByteView(cb.data(), cb.size()));
                    sub = v.meshRegionOffset();
                } else {
                    sr3geometry::MaterialBlock mat =
                        sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
                    sub = sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                                            mat).offset();
                }
                sr3geometry::GeometryBlock geo =
                    sr3geometry::GeometryBlock::parseAt(vpp::ByteView(cb.data(), cb.size()), sub);
                if (!geo.hasMeshSubBlock()) continue;
                sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
                    vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
                    vpp::ByteView(gb.data(), gb.size()));
                if (!mesh.drawGroupsLocated()) continue;
                tally(isCar ? g_veh : g_char, mesh);
            } catch (const std::exception&) {}
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

void report(const char* label, const Pop& p) {
    printf("\n=== %s ===\n", label);
    printf("  files %d, draw ranges %lld\n", p.files, p.ranges);
    printf("  ranges with a NON-ZERO high half : %lld / %lld\n", p.highNonZero, p.ranges);
    printf("  max raw u32 %u   max low16 %u   max high16 %u\n", p.maxRaw, p.maxLow, p.maxHigh);
    printf("  high-half values seen:");
    for (const auto& kv : p.highValues) printf(" %u(x%lld)", kv.first, kv.second);
    printf("\n");
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception&) {}
    }
    report("CHARACTER MESHES (.ccmesh_pc)", g_char);
    report("VEHICLES (.ccar_pc)", g_veh);
    printf("\nVERDICT\n");
    if (g_char.highNonZero == 0)
        printf("  Characters: high half is ZERO in every range - masking is a provable no-op,\n"
               "  and the existing plain-u32 reading was never wrong there.\n");
    else
        printf("  Characters: %lld ranges carry a non-zero high half - the shipped reading IS\n"
               "  wrong and the material partition needs re-checking.\n", g_char.highNonZero);
    return 0;
}
