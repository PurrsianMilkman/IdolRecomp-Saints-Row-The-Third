// Ad hoc real-data validation (not a deliverable): runs the real
// sr3foliage::FoliageMesh parser over every .cfmesh_pc in an archive and
// reproduces spec-foliage-format.md's population statistics.
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "sr3foliage/foliage_mesh.h"
#include "vpp/container.h"

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

int g_files = 0, g_ok = 0, g_fail = 0, g_gfmesh = 0;
int g_meshV9 = 0, g_records = 0;
size_t g_minSize = SIZE_MAX, g_maxSize = 0;
std::set<uint32_t> g_shaderHashes, g_variantHashes, g_slotHashes;
std::set<std::string> g_texNames;
std::map<std::string, int> g_shapes;   // "(A,B,C)" -> count
std::map<uint32_t, int> g_matFlags;
std::map<size_t, int> g_matCounts, g_lodCounts, g_matBlockTexCounts;
int g_bindings = 0;

void checkOne(const std::string& name, vpp::ByteView bytes) {
    ++g_files;
    if (bytes.size() < g_minSize) g_minSize = bytes.size();
    if (bytes.size() > g_maxSize) g_maxSize = bytes.size();
    try {
        sr3foliage::ByteView content(bytes.data(), bytes.size());
        sr3foliage::FoliageMesh f = sr3foliage::FoliageMesh::parse(content);
        ++g_ok;
        if (f.hasMeshSubBlock()) ++g_meshV9;
        ++g_matCounts[f.materials().size()];
        ++g_lodCounts[f.lodRecords().size()];
        ++g_matBlockTexCounts[f.materialBlock().textureNames.size()];
        for (const auto& n : f.textureNames()) g_texNames.insert(n);
        for (const auto& m : f.materials()) {
            ++g_records;
            g_shaderHashes.insert(m.shaderHash);
            g_variantHashes.insert(m.variantHash);
            ++g_matFlags[m.flags];
            char buf[64];
            snprintf(buf, sizeof(buf), "(%zu, %zu, %zu)", m.textureBindings.size(),
                     m.constantNameHashes.size(), m.constantValues.size());
            ++g_shapes[buf];
            for (const auto& b : m.textureBindings) {
                ++g_bindings;
                g_slotHashes.insert(b.slotHash);
            }
        }
    } catch (const std::exception& ex) {
        ++g_fail;
        printf("FAIL %s (%zu bytes): %s\n", name.c_str(), bytes.size(), ex.what());
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".gfmesh_pc")) ++g_gfmesh;
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                try {
                    vpp::Container nested = c.openNested(i);
                    walk(nested);
                    continue;
                } catch (const vpp::FormatError&) {
                }
                if (endsWith(e.name, ".cfmesh_pc")) checkOne(e.name, c.rawEntryBytes(i));
            } else {
                // Only inflate the entries we want: decompressing every entry of
                // a ~1.5 GB archive is what made this walk take tens of minutes.
                if (!endsWith(e.name, ".cfmesh_pc")) continue;
                auto r = c.decompressEntry(i);
                bool usable = r.status == vpp::DecodeStatus::Ok ||
                              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
                if (usable && endsWith(e.name, ".cfmesh_pc")) {
                    checkOne(e.name, vpp::ByteView(r.data.data(), r.data.size()));
                }
            }
        } catch (const std::exception&) {
            continue;
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> bytes = readFile(argv[i]);
        if (bytes.empty()) { printf("could not read %s\n", argv[i]); continue; }
        if (endsWith(argv[i], ".cfmesh_pc")) { // a loose, already-extracted file
            checkOne(argv[i], vpp::ByteView(bytes.data(), bytes.size()));
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c);
            printf("scanned %s\n", argv[i]);
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== .cfmesh_pc validation ===\n");
    printf("files found        : %d  (spec Sec2: 19, the complete population)\n", g_files);
    printf("parsed OK          : %d\n", g_ok);
    printf("failed             : %d\n", g_fail);
    printf(".gfmesh_pc seen    : %d  (spec Sec2: 0)\n", g_gfmesh);
    printf("size range         : %zu .. %zu  (spec Sec2: 1,564 .. 24,564)\n", g_minSize, g_maxSize);
    printf("Mesh sub-block reads version 9 : %d / %d  (spec Sec1: 19/19)\n", g_meshV9, g_ok);

    printf("\n-- materials (spec Sec2: 33 records, exactly 2 shapes; Sec6: 2 shader hashes, 13 variants) --\n");
    printf("material records   : %d  (spec: 33)\n", g_records);
    printf("distinct shaderHash: %zu  (spec Sec6: 2)\n", g_shaderHashes.size());
    printf("distinct variantHash: %zu  (spec Sec6: 13)\n", g_variantHashes.size());
    printf("shapes (A,B,C)     : (spec Sec6: (3,9,11) x23 and (1,1,3) x10)\n");
    for (const auto& kv : g_shapes) printf("   %s x%d\n", kv.first.c_str(), kv.second);
    printf("material flags     : (spec Sec6: 0xC2 plants, 0x40 litter)\n");
    for (const auto& kv : g_matFlags) printf("   0x%02x x%d\n", kv.first, kv.second);

    printf("\n-- texture bindings (spec Sec6.1: 79 bindings, 3 distinct slotHash, 16 distinct names) --\n");
    printf("bindings           : %d  (spec: 79)\n", g_bindings);
    printf("distinct slotHash  : %zu  (spec: 3)\n", g_slotHashes.size());
    printf("distinct .tga names: %zu  (spec Sec6.3: 16)\n", g_texNames.size());
    for (const auto& n : g_texNames) printf("   %s\n", n.c_str());

    printf("\n-- per-file counts --\n");
    printf("materials per file (spec Sec2: 1-4):\n");
    for (const auto& kv : g_matCounts) printf("   %zu materials: %d files\n", kv.first, kv.second);
    printf("LOD records per file (spec Sec7: 1-3):\n");
    for (const auto& kv : g_lodCounts) printf("   %zu records: %d files\n", kv.first, kv.second);
    printf("material-block texture names per file (spec Sec2: 2-12):\n");
    for (const auto& kv : g_matBlockTexCounts) printf("   %zu names: %d files\n", kv.first, kv.second);

    return g_fail == 0 ? 0 : 1;
}
