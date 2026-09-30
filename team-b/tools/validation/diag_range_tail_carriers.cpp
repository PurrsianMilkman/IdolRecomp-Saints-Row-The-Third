// Scope check for the per-draw-range 8-byte tail array found after the
// 20-byte draw-range records in the Mesh sub-block (HANDOFF Sec9.63.10).
//
// The finding was made on skinned CHARACTER meshes, where byte +0x00 of
// each record is the bone-palette SET index. This harness asks the
// separate, carrier-neutral question the Sec9.63 "scoping gap" note says
// to ask before generalising: does the ARRAY ITSELF (one 8-byte record
// per draw range, ending exactly 4 bytes before the Mesh sub-block's
// declared c-length, with those 4 bytes being the block's own check
// value) exist on a carrier that has no bone palette at all?
//
// Carriers checked: characters (.ccmesh_pc/.gcmesh_pc) and vehicles
// (.ccar_pc/.gcar_pc). Reports, per carrier: how many blocks the
// structural bookend test passes on, and the histogram of byte +0x00.
//
// Usage: diag_range_tail_carriers <archive.vpp_pc> [more...]
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "sr3vehicle/vehicle.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) { f.seekg(0); f.read(reinterpret_cast<char*>(b.data()), n); }
    return b;
}

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}

struct Pair { std::vector<uint8_t> c, g; bool hasC = false, hasG = false; };

std::string stemOf(const std::string& name, const char* ext) {
    size_t n = std::strlen(ext);
    if (name.size() > n && name.compare(name.size() - n, n, ext) == 0) return name.substr(0, name.size() - n);
    return std::string();
}

size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }
uint32_t rdU32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

struct GroupLayout { bool located = false; size_t rangesAt = 0, totalRanges = 0; };

GroupLayout locateGroups(const std::vector<uint8_t>& c, size_t meshOffset, const sr3mesh::MeshBlock& mesh) {
    GroupLayout out;
    const size_t header = meshOffset + sr3mesh::kHeaderStart;
    if (header + sr3mesh::kHeaderSize > c.size()) return out;
    uint32_t channelCount = rdU32(c, header + sr3mesh::kChannelCountOffset);
    uint32_t groupCount = rdU32(c, header + 0x04);
    const size_t recordsAt = header + sr3mesh::kHeaderSize;
    const size_t searchStart = recordsAt + static_cast<size_t>(channelCount) * sr3mesh::kChannelRecordSize;
    const size_t searchLimit = std::min(c.size(), searchStart + 4096);
    uint32_t largest = 0;
    for (const auto& ch : mesh.channels()) if (ch.elementCount > largest) largest = ch.elementCount;
    if (groupCount == 0) return out;
    for (size_t cand = alignUp(searchStart, 16); cand + 4 <= searchLimit; cand += 16) {
        std::vector<uint32_t> rc; size_t total = 0; bool shapeOk = true;
        for (uint32_t g = 0; g < groupCount; ++g) {
            size_t at = cand + static_cast<size_t>(g) * 0x30;
            if (at + 0x30 > c.size()) { shapeOk = false; break; }
            uint32_t n = rdU32(c, at);
            if (n == 0 || n > 4096) { shapeOk = false; break; }
            rc.push_back(n); total += n;
        }
        if (!shapeOk || total == 0) continue;
        const size_t rangesAt = cand + static_cast<size_t>(groupCount) * 0x30;
        if (rangesAt + total * 20 > c.size()) continue;
        size_t cur = rangesAt; bool valid = true; uint32_t expected = 0; size_t groups = 0;
        for (uint32_t g = 0; g < groupCount && valid; ++g) {
            for (uint32_t r = 0; r < rc[g]; ++r) {
                uint32_t start = rdU32(c, cur + 0x04), cnt = rdU32(c, cur + 0x08);
                uint32_t mn = rdU32(c, cur + 0x0C), mx = rdU32(c, cur + 0x10);
                cur += 20;
                if (start != expected || cnt == 0 || mn > mx || mx >= largest) { valid = false; break; }
                expected = start + cnt;
            }
            if (!valid) break;
            if (expected > mesh.indexCount()) { valid = false; break; }
            ++groups;
        }
        if (valid && expected != mesh.indexCount()) valid = false;
        if (valid && groups == groupCount) { out.located = true; out.rangesAt = rangesAt; out.totalRanges = total; return out; }
    }
    return out;
}

struct Stats {
    size_t blocks = 0, located = 0, bookend = 0, gapIsFour = 0, allValuesZero = 0;
    std::map<uint32_t, size_t> valueHist;
    std::map<int, size_t> tailByteNonZero;
    std::map<long long, size_t> gapHist;
};

void check(const std::vector<uint8_t>& c, size_t meshOffset, const sr3mesh::MeshBlock& mesh, Stats& st) {
    ++st.blocks;
    GroupLayout gl = locateGroups(c, meshOffset, mesh);
    if (!gl.located) return;
    ++st.located;
    const size_t rangesEnd = gl.rangesAt + gl.totalRanges * 20;
    const size_t arrayEnd = rangesEnd + gl.totalRanges * 8;
    if (arrayEnd + 4 > c.size()) return;
    uint32_t cLen = rdU32(c, meshOffset + sr3mesh::kCLengthOffset);
    long long gap = (long long)(meshOffset + cLen) - (long long)arrayEnd;
    st.gapHist[gap]++;
    if (gap == 4) ++st.gapIsFour;
    if (rdU32(c, arrayEnd) == mesh.checkValue()) ++st.bookend;
    bool allZero = true;
    for (size_t i = 0; i < gl.totalRanges; ++i) {
        st.valueHist[c[rangesEnd + i * 8]]++;
        if (c[rangesEnd + i * 8] != 0) allZero = false;
        for (int b = 1; b < 8; ++b)
            if (c[rangesEnd + i * 8 + static_cast<size_t>(b)] != 0) st.tailByteNonZero[b]++;
    }
    if (allZero) ++st.allValuesZero;
}

void report(const char* name, const Stats& st) {
    std::printf("\n=== %s ===\n", name);
    std::printf("blocks parsed                  : %zu\n", st.blocks);
    std::printf("draw-group array located       : %zu\n", st.located);
    std::printf("gap (meshEnd - arrayEnd) hist  :");
    for (auto& kv : st.gapHist) std::printf(" %lld:%zu", kv.first, kv.second);
    std::printf("\ngap == 4                       : %zu / %zu\n", st.gapIsFour, st.located);
    std::printf("4 bytes at arrayEnd == check   : %zu / %zu\n", st.bookend, st.located);
    std::printf("byte +0x00 value histogram     :");
    for (auto& kv : st.valueHist) std::printf(" %u:%zu", kv.first, kv.second);
    std::printf("\nbytes +1..+7 non-zero counts   :");
    for (int b = 1; b < 8; ++b) {
        auto it = st.tailByteNonZero.find(b);
        std::printf(" +%d:%zu", b, it == st.tailByteNonZero.end() ? 0 : it->second);
    }
    std::printf("\nblocks whose byte +0 is all 0  : %zu / %zu\n", st.allValuesZero, st.located);
}

Stats chars, vehicles;

void walk(const vpp::Container& c) {
    std::map<std::string, Pair> mesh, car;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string s;
        try {
            if (!(s = stemOf(n, ".ccmesh_pc")).empty()) { if (entryBytes(c, i, mesh[s].c)) mesh[s].hasC = true; }
            else if (!(s = stemOf(n, ".gcmesh_pc")).empty()) { if (entryBytes(c, i, mesh[s].g)) mesh[s].hasG = true; }
            else if (!(s = stemOf(n, ".ccar_pc")).empty()) { if (entryBytes(c, i, car[s].c)) car[s].hasC = true; }
            else if (!(s = stemOf(n, ".gcar_pc")).empty()) { if (entryBytes(c, i, car[s].g)) car[s].hasG = true; }
        } catch (const std::exception&) {}
    }
    for (auto& kv : mesh) {
        if (!(kv.second.hasC && kv.second.hasG)) continue;
        try {
            vpp::ByteView cv(kv.second.c.data(), kv.second.c.size());
            sr3geometry::MaterialBlock mb = sr3geometry::MaterialBlock::parse(cv);
            sr3geometry::GeometryBlock gb = sr3geometry::GeometryBlock::parse(cv, mb);
            if (!gb.hasMeshSubBlock()) continue;
            sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(cv, gb.meshSubBlockOffset(),
                                                             vpp::ByteView(kv.second.g.data(), kv.second.g.size()));
            check(kv.second.c, gb.meshSubBlockOffset(), m, chars);
        } catch (const std::exception&) {}
    }
    for (auto& kv : car) {
        if (!(kv.second.hasC && kv.second.hasG)) continue;
        try {
            vpp::ByteView cv(kv.second.c.data(), kv.second.c.size());
            sr3vehicle::Vehicle v = sr3vehicle::Vehicle::parse(cv);
            sr3geometry::GeometryBlock gb = sr3geometry::GeometryBlock::parseAt(cv, v.meshRegionOffset());
            if (!gb.hasMeshSubBlock()) continue;
            sr3mesh::MeshBlock m = sr3mesh::MeshBlock::parse(cv, gb.meshSubBlockOffset(),
                                                             vpp::ByteView(kv.second.g.data(), kv.second.g.size()));
            check(kv.second.c, gb.meshSubBlockOffset(), m, vehicles);
        } catch (const std::exception&) {}
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try { vpp::Container nested = c.openNested(i); walk(nested); } catch (const std::exception&) {}
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: diag_range_tail_carriers <archive.vpp_pc> [more...]\n"); return 1; }
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> bytes = readFile(argv[i]);
        if (bytes.empty()) { std::fprintf(stderr, "could not read %s\n", argv[i]); continue; }
        try { vpp::Container c(vpp::ByteView(bytes.data(), bytes.size())); walk(c); }
        catch (const std::exception& e) { std::fprintf(stderr, "%s: %s\n", argv[i], e.what()); }
        std::printf("done %s\n", argv[i]);
    }
    report("CHARACTER MESHES (.ccmesh_pc)", chars);
    report("VEHICLES (.ccar_pc, first Mesh sub-block only)", vehicles);
    return 0;
}
