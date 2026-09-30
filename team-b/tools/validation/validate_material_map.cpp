// What does a draw range's material id actually index?
//
// spec-vertex-format.md §8.2 says the 20-byte draw range's `+0x00` is a
// "material / slot id - indexes the material set, and repeats across
// groups". It does NOT say that set is the material block's flat texture
// NAME list, and the two are different kinds of thing: a material normally
// owns several textures (diffuse, normal, specular...), so
// `textureNames[materialId]` is a guess, not a reading.
//
// Rendering per-material needs that mapping, so measure it before building
// anything on it. Per mesh this reports:
//
//   * the distinct material ids actually used by the draw ranges, and
//     whether they are dense from 0;
//   * the material block's textureSlotCount and names;
//   * whether max(materialId) < textureSlotCount at all (if not, the
//     direct reading is dead on arrival);
//   * whether textureSlotCount is an exact multiple of the material count
//     - which is what a fixed number of texture slots per material looks
//     like, and the ratio would then BE that number;
//   * the geometry block's six array element counts, since §8.2 notes
//     arrays 2-3 hold per-material float data - an array whose count
//     equals the material count is independent corroboration.
//
// Deliberately reports the relationship rather than asserting one. If the
// ratio is not constant across the population there is no fixed
// slots-per-material rule and the honest answer is that the mapping is
// still open.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
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

int g_meshes = 0, g_located = 0;
int g_idsDenseFromZero = 0;
int g_maxIdWithinSlots = 0;
int g_slotsIsMultiple = 0;
std::map<int, int> g_ratioCounts;   // textureSlotCount / materialCount -> meshes
std::map<int, int> g_materialCounts;
int g_printed = 0;
int g_declaredTotal = 0, g_declaredMatches = 0, g_controlMatches = 0;
std::vector<std::string> g_declaredMismatches;
std::map<int, int> g_offsetHits;
int g_scanTotal = 0;
int g_scanSkipped = 0;
int g_subTotal = 0, g_subMatches = 0, g_subControl = 0, g_subMagicOk = 0;
int g_subMultiTotal = 0, g_subMultiMatches = 0, g_subMultiControl = 0;

void examine(const std::string& name, const std::vector<uint8_t>& cb,
             const std::vector<uint8_t>& gb) {
    try {
        sr3geometry::MaterialBlock mat =
            sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
        sr3geometry::GeometryBlock geo =
            sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
        if (!geo.hasMeshSubBlock()) return;
        sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
            vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
            vpp::ByteView(gb.data(), gb.size()));
        ++g_meshes;
        if (!mesh.drawGroupsLocated()) return;
        ++g_located;

        std::set<uint32_t> ids;
        size_t totalRanges = 0;
        for (const auto& group : mesh.drawGroups()) {
            totalRanges += group.size();
            for (const auto& r : group) ids.insert(r.materialId);
        }
        if (ids.empty()) return;

        uint32_t maxId = *ids.rbegin();
        bool dense = (*ids.begin() == 0) && (ids.size() == maxId + 1u);
        if (dense) ++g_idsDenseFromZero;

        // Team A relayed: material count is a u16 at the outer sub-header
        // +0x0C, equal to 1 + max(materialId) in 428/428. The "outer
        // sub-header" is the 0x70 header that begins at the Mesh block's
        // +0x10 - the same one whose +0x04 holds the group count (spec
        // §8.2). Verified here rather than adopted, as every relayed claim
        // has been. A CONTROL reads the neighbouring u16 at +0x0E: if that
        // scores comparably the offset is not pinned, only the magnitude.
        // LOCATION FIX (relayed): "outer sub-header" is the 0x424BD00D
        // geometry sub-header, not the Mesh block's 0x70 header - which is
        // why my search found nothing: it sits BEFORE the Mesh block, well
        // outside the +/-256 window I scanned. Their arithmetic is
        // align16(material_block_end); this reader already tracks that same
        // position as GeometryBlock::offset(), so using it cross-checks
        // their derivation instead of just re-implementing it.
        const size_t sub = geo.offset();
        if (sub + 0x10 <= cb.size()) {
            const uint32_t magic = static_cast<uint32_t>(
                cb[sub] | (cb[sub+1] << 8) | (cb[sub+2] << 16) | (cb[sub+3] << 24));
            const uint16_t declared =
                static_cast<uint16_t>(cb[sub + 0x0C] | (cb[sub + 0x0D] << 8));
            if (magic == 0x424BD00Du) ++g_subMagicOk;
            ++g_subTotal;
            if (declared == maxId + 1u) ++g_subMatches;
            // Control: the neighbouring u16. Material counts are small, so
            // an adjacent slot matching often would mean the offset is not
            // actually pinned.
            const uint16_t ctrl =
                static_cast<uint16_t>(cb[sub + 0x0E] | (cb[sub + 0x0F] << 8));
            if (ctrl == maxId + 1u) ++g_subControl;
            if (ids.size() > 1) {
                ++g_subMultiTotal;
                if (declared == maxId + 1u) ++g_subMultiMatches;
                if (ctrl == maxId + 1u) ++g_subMultiControl;
            }
        }

        const size_t headerAt = geo.meshSubBlockOffset() + 0x10;
        if (headerAt + 0x10 <= cb.size()) {
            const uint16_t declared =
                static_cast<uint16_t>(cb[headerAt + 0x0C] | (cb[headerAt + 0x0D] << 8));
            const uint16_t control =
                static_cast<uint16_t>(cb[headerAt + 0x0E] | (cb[headerAt + 0x0F] << 8));
            ++g_declaredTotal;
            if (declared == maxId + 1u) ++g_declaredMatches;
            if (control == maxId + 1u) ++g_controlMatches;
            if (declared != maxId + 1u && g_declaredMismatches.size() < 8)
                g_declaredMismatches.push_back(name + ": declared " + std::to_string(declared) +
                                               " vs 1+max " + std::to_string(maxId + 1));
        }

        // My reading of "outer sub-header +0x0C" came out 0/549 with the
        // field reading zero everywhere, which says my offset is wrong -
        // not that the claim is. So SEARCH for it instead of guessing
        // again: 1 + max(materialId) is an oracle computed from the draw
        // ranges, entirely independently of wherever this field lives.
        // Scan every u16 offset in a window around the Mesh sub-block and
        // see which one agrees across the whole population. Same technique
        // that pinned the pre-header offsets and the draw-group array.
        //
        // The control is built in: material counts are small (1-7), so many
        // offsets will match on any single mesh by luck. Only an offset
        // that matches on ALL of them means anything.
        // Scan MULTI-material meshes only. 272 of 549 have a single
        // material, so their oracle value is 1 - and a u16 reading 1 is
        // everywhere in a header, which floors every offset at ~50% and
        // buries any real signal. Excluding them is the same wrong-
        // denominator correction spec §8.2 records for the vertex-bound
        // check; including them would be measuring how common the number 1
        // is.
        const long base = static_cast<long>(geo.meshSubBlockOffset());
        if (ids.size() < 2) { ++g_scanSkipped; goto afterScan; }
        for (long d = -0x80; d <= 0x100; d += 2) {
            const long at = base + d;
            if (at < 0 || static_cast<size_t>(at) + 2 > cb.size()) continue;
            const uint16_t v = static_cast<uint16_t>(cb[static_cast<size_t>(at)] |
                                                     (cb[static_cast<size_t>(at) + 1] << 8));
            if (v == maxId + 1u) ++g_offsetHits[static_cast<int>(d)];
        }
        ++g_scanTotal;
afterScan:;

        const uint32_t materialCount = static_cast<uint32_t>(ids.size());
        ++g_materialCounts[static_cast<int>(materialCount)];

        if (maxId < mat.textureSlotCount) ++g_maxIdWithinSlots;
        if (materialCount > 0 && mat.textureSlotCount % materialCount == 0) {
            ++g_slotsIsMultiple;
            ++g_ratioCounts[static_cast<int>(mat.textureSlotCount / materialCount)];
        }

        if (g_printed < 6) {
            ++g_printed;
            printf("\n%s\n", name.c_str());
            printf("  groups %zu, ranges %zu, distinct material ids %zu (max %u, dense-from-0 %s)\n",
                   mesh.drawGroups().size(), totalRanges, ids.size(), maxId,
                   dense ? "yes" : "NO");
            printf("  textureSlotCount %u", mat.textureSlotCount);
            if (materialCount > 0 && mat.textureSlotCount % materialCount == 0)
                printf("   -> %u per material", mat.textureSlotCount / materialCount);
            printf("\n  texture names:");
            for (size_t i = 0; i < mat.textureNames.size() && i < 12; ++i)
                printf(" [%zu]%s", i, mat.textureNames[i].c_str());
            printf("\n  geometry arrays (count x stride):");
            for (size_t i = 0; i < 6; ++i)
                printf(" %zu:%ux%zu", i, geo.arrays()[i].count,
                       static_cast<size_t>(geo.arrays()[i].stride));
            printf("\n  ranges by group:");
            for (const auto& group : mesh.drawGroups()) {
                printf(" [");
                for (const auto& r : group) printf("%u ", r.materialId);
                printf("]");
            }
            printf("\n");
        }
    } catch (const std::exception&) {}
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".ccmesh_pc")) {
            std::string gn = n;
            size_t d = gn.find_last_of('.');
            if (d != std::string::npos) gn[d + 1] = 'g';
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j)
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            if (!gb.empty()) examine(n, cb, gb);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
            printf("\nscanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception&) {}
    }

    printf("\n=== what does draw-range materialId index? ===\n");
    printf("meshes parsed                    : %d\n", g_meshes);
    printf("draw groups located              : %d\n", g_located);
    printf("material ids dense from 0        : %d / %d\n", g_idsDenseFromZero, g_located);
    printf("max materialId < textureSlotCount: %d / %d\n", g_maxIdWithinSlots, g_located);
    printf("textureSlotCount %% materialCount == 0 : %d / %d\n", g_slotsIsMultiple, g_located);
    printf("ratio textureSlotCount / materialCount:\n");
    for (const auto& kv : g_ratioCounts)
        printf("    %d textures per material : %d mesh(es)\n", kv.first, kv.second);
    printf("\nmaterial count at the 0x424BD00D SUB-HEADER +0x0C (the location fix):\n");
    printf("    magic 0x424BD00D confirmed : %d / %d\n", g_subMagicOk, g_subTotal);
    printf("    == 1 + max(materialId)     : %d / %d\n", g_subMatches, g_subTotal);
    printf("    CONTROL (+0x0E instead)    : %d / %d\n", g_subControl, g_subTotal);
    printf("    MULTI-material only        : %d / %d   (control %d)\n", g_subMultiMatches,
           g_subMultiTotal, g_subMultiControl);

    printf("\n[superseded] u16 at the 0x70-header +0x0C, my original reading:\n");
    printf("    == 1 + max(materialId)   : %d / %d\n", g_declaredMatches, g_declaredTotal);
    printf("    CONTROL (+0x0E instead)  : %d / %d\n", g_controlMatches, g_declaredTotal);
    for (const auto& m : g_declaredMismatches) printf("    mismatch: %s\n", m.c_str());
    printf("\noffset scan: u16 == 1+max(materialId), relative to the Mesh sub-block\n");
    {
        std::vector<std::pair<int, int>> best(g_offsetHits.begin(), g_offsetHits.end());
        std::sort(best.begin(), best.end(),
                  [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                      return a.second > b.second;
                  });
        for (size_t i = 0; i < best.size() && i < 8; ++i)
            printf("    %+5d : %d / %d  (%.1f%%)\n", best[i].first, best[i].second, g_scanTotal,
                   g_scanTotal ? 100.0 * best[i].second / g_scanTotal : 0.0);
    }
    printf("\nmaterial counts seen:\n");
    for (const auto& kv : g_materialCounts)
        printf("    %d material(s) : %d mesh(es)\n", kv.first, kv.second);
    return 0;
}
