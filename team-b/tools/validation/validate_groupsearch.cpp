// How many candidate offsets satisfy the draw-group invariants?
//
// `sr3mesh` locates the draw-group array by SEARCH, and `mesh_block.h`
// claims the invariants are "strong enough that a wrong offset cannot
// pass". That claim was asserted, never measured: the search `break`s on
// the FIRST candidate that validates and never counts the rest. Under
// §3's ambiguity-count lesson - a match rate without a survivor count is
// not a measurement - "we always find one" says nothing about whether the
// one we find is the only one.
//
// So this replicates the search INDEPENDENTLY (it does not call the
// reader, so it cannot inherit the reader's bug) and counts every
// candidate that satisfies all the invariants, instead of stopping at the
// first:
//
//   * groupCount 0x30-byte group records, each leading with a range count
//     in 1..4096;
//   * then totalRanges 20-byte ranges, contiguous from 0 across ALL groups
//     in order, together covering the index buffer exactly;
//   * every range minVertex <= maxVertex < largest channel's vertex count.
//
// Outcome that would vindicate the header comment: exactly 1 survivor on
// every mesh. Anything else means the reader has been picking the first of
// several and the claim needs rewriting.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
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
static size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

std::map<int, int> g_survivorCounts;   // survivors -> meshes
int g_meshes = 0, g_firstIsOnly = 0;
std::vector<std::string> g_multi;

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
        if (mesh.channels().empty() || !mesh.drawGroupsLocated()) return;
        ++g_meshes;

        const size_t headerAt = geo.meshSubBlockOffset() + 0x10;
        const size_t recordsAt = headerAt + 0x70;
        const uint32_t channelCount = u32at(cb, headerAt + 0x10);
        const uint32_t groupCount = u32at(cb, headerAt + 0x04);
        const uint32_t indexCount = mesh.indexCount();
        uint32_t largest = 0;
        for (const auto& ch : mesh.channels()) largest = std::max(largest, ch.elementCount);
        if (groupCount == 0) return;

        const size_t searchStart = recordsAt + static_cast<size_t>(channelCount) * 24;
        const size_t searchLimit = std::min(cb.size(), searchStart + 4096);

        int survivors = 0;
        size_t firstSurvivor = 0;
        for (size_t cand = alignUp(searchStart, 16); cand + 4 <= searchLimit; cand += 16) {
            std::vector<uint32_t> rangeCounts;
            size_t totalRanges = 0;
            bool shapeOk = true;
            for (uint32_t g = 0; g < groupCount; ++g) {
                size_t at = cand + static_cast<size_t>(g) * 0x30;
                if (at + 0x30 > cb.size()) { shapeOk = false; break; }
                uint32_t n = u32at(cb, at);
                if (n == 0 || n > 4096) { shapeOk = false; break; }
                rangeCounts.push_back(n);
                totalRanges += n;
            }
            if (!shapeOk || totalRanges == 0) continue;

            const size_t rangesAt = cand + static_cast<size_t>(groupCount) * 0x30;
            if (rangesAt + totalRanges * 20 > cb.size()) continue;

            size_t cursor = rangesAt;
            bool valid = true;
            uint32_t expectedStart = 0;
            size_t groupsBuilt = 0;
            for (uint32_t g = 0; g < groupCount && valid; ++g) {
                for (uint32_t r = 0; r < rangeCounts[g]; ++r) {
                    const uint32_t start = u32at(cb, cursor + 0x04);
                    const uint32_t count = u32at(cb, cursor + 0x08);
                    const uint32_t minV = u32at(cb, cursor + 0x0C);
                    const uint32_t maxV = u32at(cb, cursor + 0x10);
                    cursor += 20;
                    if (start != expectedStart) { valid = false; break; }
                    if (count == 0) { valid = false; break; }
                    if (minV > maxV) { valid = false; break; }
                    if (maxV >= largest) { valid = false; break; }
                    expectedStart = start + count;
                }
                if (!valid) break;
                if (expectedStart > indexCount) { valid = false; break; }
                ++groupsBuilt;
            }
            if (valid && expectedStart != indexCount) valid = false;
            if (valid && groupsBuilt == groupCount) {
                if (survivors == 0) firstSurvivor = cand;
                ++survivors;
            }
        }

        ++g_survivorCounts[survivors];
        if (survivors == 1) ++g_firstIsOnly;
        if (survivors > 1 && g_multi.size() < 10)
            g_multi.push_back(name + ": " + std::to_string(survivors) +
                              " survivors, first at " + std::to_string(firstSurvivor));
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
    printf("\n=== draw-group search: AMBIGUITY COUNT ===\n");
    printf("The reader takes the FIRST candidate that validates. The question\n");
    printf("is how many would have.\n\n");
    printf("meshes with located groups : %d\n", g_meshes);
    printf("exactly one survivor       : %d / %d\n", g_firstIsOnly, g_meshes);
    for (const auto& kv : g_survivorCounts)
        printf("    %d surviving candidate(s) : %d mesh(es)\n", kv.first, kv.second);
    if (!g_multi.empty()) {
        printf("\nAMBIGUOUS - the reader was picking one of several:\n");
        for (const auto& m : g_multi) printf("    %s\n", m.c_str());
    } else {
        printf("\nNo mesh had more than one surviving candidate.\n");
    }
    return 0;
}
