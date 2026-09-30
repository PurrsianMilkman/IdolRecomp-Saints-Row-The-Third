// Verify the material -> texture binding before wiring it into the reader.
//
// §9.22.1 requires that whoever implements this re-derives the run-location
// heuristic AND its guard against the shipped archives, reporting the
// AMBIGUITY COUNT alongside the hit rate rather than instead of it. This is
// that step, and nothing is committed to `sr3geometry` until it passes.
//
// Structure (relayed, verified here):
//   subheader     = GeometryBlock::offset()      0x424BD00D magic
//   materialCount = u16(subheader + 0x0C)
//   name blob     = subheader + 0x88; u32 total length, then MIXED-case
//                   NUL-terminated names beginning at blob + 9. Entry
//                   offsets are relative to that first name's start -
//                   measured from brad, where entry offset 22 lands exactly
//                   on the second name.
//   per-material records live past the end of the Mesh sub-block; each
//   holds a run of 12-byte entries:
//       +0x00 u32 name offset   +0x04 u32 param hash   +0x08 u32 slot index
//
// THE GUARD (§9.22.1): accept a run only if length >= 2 AND every parameter
// hash is non-zero. Single-entry zero-hash runs are placeholder/LOD meshes
// with no real binding, and Team A found six or more positions tying for
// best in ALL 300 such cases - resolving one arbitrarily would bind a
// wrong-but-real-looking texture and render a plausible, silently incorrect
// character.
//
// INDEPENDENT CROSS-CHECK: every resolved name, lowercased, must appear in
// the material block's own lowercase texture list. That list is parsed by a
// completely separate reader from a different part of the file, so
// agreement is evidence rather than self-consistency.
#include <algorithm>
#include <cstdio>
#include <cstring>
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
static uint32_t u32at(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) return 0;
    return static_cast<uint32_t>(b[o] | (b[o+1] << 8) | (b[o+2] << 16) |
                                 (static_cast<uint32_t>(b[o+3]) << 24));
}
static uint16_t u16at(const std::vector<uint8_t>& b, size_t o) {
    if (o + 2 > b.size()) return 0;
    return static_cast<uint16_t>(b[o] | (b[o+1] << 8));
}
static std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
// Resolves a NUL-terminated name, refusing anything that is not a plausible
// filename - so a wrong offset fails loudly instead of returning noise.
static bool resolveName(const std::vector<uint8_t>& b, size_t base, uint32_t off,
                        size_t blobEnd, std::string& out) {
    size_t at = base + off;
    if (at >= blobEnd || at >= b.size()) return false;
    std::string s;
    while (at < blobEnd && at < b.size() && b[at] != 0) {
        uint8_t c = b[at];
        if (c < 32 || c >= 127) return false;
        s.push_back(static_cast<char>(c));
        ++at;
        if (s.size() > 128) return false;
    }
    if (s.size() < 3) return false;
    if (s.find('.') == std::string::npos) return false;
    out = s;
    return true;
}

int g_meshes = 0;
int g_countMatches = 0, g_strideConstant = 0;
std::map<int, int> g_ambiguity;        // survivors - materialCount -> meshes
int g_exactlyMaterialCount = 0;
long long g_slot0 = 0, g_slot0Diffuse = 0;
long long g_slot0Multi = 0, g_slot0MultiDiffuse = 0;
long long g_slot0Single = 0, g_slot0SingleDiffuse = 0;
long long g_slot1 = 0, g_slot1Normal = 0, g_slot1Hash = 0;
long long g_slot2 = 0, g_slot2Hash = 0;
long long g_namesResolved = 0, g_namesInMaterialBlock = 0;
int g_noBinding = 0;
std::vector<std::string> g_examples;
std::vector<std::string> g_problems;

struct Run { size_t at; size_t len; };

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
        if (!mesh.drawGroupsLocated()) return;
        ++g_meshes;

        const size_t sub = geo.offset();
        const uint16_t materialCount = u16at(cb, sub + 0x0C);
        const size_t blobAt = sub + 0x88;
        const uint32_t blobLen = u32at(cb, blobAt);
        const size_t blobBase = blobAt + 9;
        const size_t blobEnd = std::min(cb.size(), blobAt + blobLen + 9);
        const size_t meshEnd = geo.meshSubBlockOffset() + mesh.cLength();
        if (materialCount == 0 || meshEnd >= cb.size()) return;

        std::set<std::string> blockNames;
        for (const auto& n : mat.textureNames) blockNames.insert(lower(n));

        // Collect EVERY run passing the guard, not the first.
        std::vector<Run> runs;
        for (size_t at = meshEnd; at + 24 <= cb.size(); at += 4) {
            if (u32at(cb, at + 0x08) != 0) continue;
            size_t len = 1;
            while (at + (len + 1) * 12 <= cb.size() &&
                   u32at(cb, at + len * 12 + 0x08) == static_cast<uint32_t>(len))
                ++len;
            if (len < 2) continue;                       // GUARD 1
            bool ok = true;
            for (size_t e = 0; e < len; ++e) {
                if (u32at(cb, at + e * 12 + 0x04) == 0) { ok = false; break; }  // GUARD 2
                std::string s;
                if (!resolveName(cb, blobBase, u32at(cb, at + e * 12 + 0x00), blobEnd, s)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            runs.push_back({at, len});
        }

        if (runs.empty()) { ++g_noBinding; return; }

        const int delta = static_cast<int>(runs.size()) - static_cast<int>(materialCount);
        ++g_ambiguity[delta];
        if (runs.size() == materialCount) ++g_exactlyMaterialCount;
        else if (g_problems.size() < 10)
            g_problems.push_back(name + ": " + std::to_string(runs.size()) + " runs vs " +
                                 std::to_string(materialCount) + " materials");

        // Stride: the runs should be evenly spaced, one record per material.
        if (runs.size() >= 2) {
            const size_t stride = runs[1].at - runs[0].at;
            bool constant = true;
            for (size_t i = 2; i < runs.size(); ++i)
                if (runs[i].at - runs[i-1].at != stride) constant = false;
            if (constant) ++g_strideConstant;
        } else {
            ++g_strideConstant; // a single run is trivially constant
        }
        if (runs.size() == materialCount) ++g_countMatches;

        for (const Run& r : runs) {
            for (size_t e = 0; e < r.len; ++e) {
                const uint32_t off = u32at(cb, r.at + e * 12 + 0x00);
                const uint32_t hash = u32at(cb, r.at + e * 12 + 0x04);
                std::string s;
                if (!resolveName(cb, blobBase, off, blobEnd, s)) continue;
                ++g_namesResolved;
                if (blockNames.count(lower(s))) ++g_namesInMaterialBlock;
                const std::string low = lower(s);
                if (e == 0) {
                    ++g_slot0;
                    bool isD = endsWith(low, "_d.tga") || endsWith(low, "_dp.tga");
                    if (isD) ++g_slot0Diffuse;
                    if (materialCount > 1) { ++g_slot0Multi; if (isD) ++g_slot0MultiDiffuse; }
                    else { ++g_slot0Single; if (isD) ++g_slot0SingleDiffuse; }
                } else if (e == 1) {
                    ++g_slot1;
                    if (endsWith(low, "_n.tga")) ++g_slot1Normal;
                    if (hash == 0x2808EB90u) ++g_slot1Hash;
                } else if (e == 2) {
                    ++g_slot2;
                    if (hash == 0xDFE71DA8u) ++g_slot2Hash;
                }
            }
        }

        if (g_examples.size() < 4) {
            std::string line = name + "  (" + std::to_string(materialCount) + " materials, " +
                               std::to_string(runs.size()) + " runs)";
            for (size_t i = 0; i < runs.size() && i < 8; ++i) {
                line += "\n        mat " + std::to_string(i) + ":";
                for (size_t e = 0; e < runs[i].len && e < 3; ++e) {
                    std::string s;
                    if (resolveName(cb, blobBase, u32at(cb, runs[i].at + e * 12), blobEnd, s))
                        line += " [" + std::to_string(e) + "] " + s;
                }
            }
            g_examples.push_back(line);
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
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception&) {}
    }

    printf("\n=== material -> texture binding ===\n");
    printf("meshes examined                  : %d\n", g_meshes);
    printf("no run passing the guard         : %d   (placeholder/LOD, correctly NO BINDING)\n",
           g_noBinding);
    printf("\nAMBIGUITY (runs found vs materialCount declared):\n");
    for (const auto& kv : g_ambiguity)
        printf("    %+3d : %d mesh(es)%s\n", kv.first, kv.second,
               kv.first == 0 ? "   <- exact" : "");
    printf("  exactly materialCount runs     : %d / %d\n", g_exactlyMaterialCount,
           g_meshes - g_noBinding);
    printf("  run spacing constant           : %d / %d\n", g_strideConstant,
           g_meshes - g_noBinding);
    printf("\nSLOT SEMANTICS:\n");
    printf("  slot 0 name ends _d/_dp        : %lld / %lld\n", g_slot0Diffuse, g_slot0);
    printf("    ...multi-material meshes     : %lld / %lld\n", g_slot0MultiDiffuse, g_slot0Multi);
    printf("    ...single-material meshes    : %lld / %lld\n", g_slot0SingleDiffuse, g_slot0Single);
    printf("  slot 1 name ends _n            : %lld / %lld\n", g_slot1Normal, g_slot1);
    printf("  slot 1 hash == 0x2808EB90      : %lld / %lld\n", g_slot1Hash, g_slot1);
    printf("  slot 2 hash == 0xDFE71DA8      : %lld / %lld\n", g_slot2Hash, g_slot2);
    printf("\nCROSS-CHECK (independent reader):\n");
    printf("  resolved names also in the material block's own list : %lld / %lld\n",
           g_namesInMaterialBlock, g_namesResolved);
    if (!g_problems.empty()) {
        printf("\nrun-count mismatches:\n");
        for (const auto& p : g_problems) printf("    %s\n", p.c_str());
    }
    printf("\nexamples:\n");
    for (const auto& e : g_examples) printf("    %s\n", e.c_str());
    return 0;
}
