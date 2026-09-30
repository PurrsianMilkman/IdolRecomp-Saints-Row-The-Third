// Breadth sweep of the whole mesh pipeline: container -> material block ->
// geometry block -> Mesh sub-block -> channel decode -> draw groups ->
// triangle expansion. Reports FAILURES, not successes.
//
// Every mesh rendered so far has been a character on layout code 3. That
// is one code path exercised many times. Vehicles use codes 100/101 - a
// rigid PART INDEX where characters carry blend weights and indices - and
// that path has never run end to end here. So this sweeps characters AND
// vehicles and breaks the results down by layout code, because "1,800
// meshes passed" hides which of them were all the same shape.
//
// Bridge counting is included per mesh because it is the one check that
// catches a wrong triangle partition rather than a parse error: a long
// edge relative to the model's own size means two unrelated pieces got
// joined (spec §8.2). Measured against the mesh's OWN bounding box, so it
// scales from a pistol to a bus without a magic constant.
#include <algorithm>
#include <cmath>
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

struct CodeStats {
    int meshes = 0;
    int groupsLocated = 0;
    int decodeFailed = 0;
    int withBridges = 0;
    long long triangles = 0;
    long long bridges = 0;
    int multiChannel = 0;
    int strideLawViolations = 0;
    long long wholeBufferLong = 0;
    int singleRange = 0;
};
std::map<int, CodeStats> g_byCode;
int g_pairs = 0, g_parseFailed = 0, g_noMeshBlock = 0;
std::vector<std::string> g_failures;
std::map<std::string, int> g_failureKinds;
std::vector<std::string> g_vehicleExamples;

void examine(const std::string& name, const std::vector<uint8_t>& cb,
             const std::vector<uint8_t>& gb) {
    ++g_pairs;
    try {
        sr3geometry::MaterialBlock mat =
            sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
        sr3geometry::GeometryBlock geo =
            sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
        if (!geo.hasMeshSubBlock()) { ++g_noMeshBlock; return; }
        sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
            vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
            vpp::ByteView(gb.data(), gb.size()));
        if (mesh.channels().empty()) return;

        const int code = static_cast<int>(mesh.channels()[0].layoutCode);
        CodeStats& s = g_byCode[code];
        ++s.meshes;
        if (mesh.channels().size() > 1) ++s.multiChannel;
        for (const auto& ch : mesh.channels())
            if (!ch.strideMatchesLaw) ++s.strideLawViolations;
        if (mesh.drawGroupsLocated()) ++s.groupsLocated;
        size_t rangeCount = 0;
        for (const auto& gr : mesh.drawGroups()) rangeCount += gr.size();
        if (rangeCount <= 1) ++s.singleRange;

        if ((code == 100 || code == 101) && g_vehicleExamples.size() < 8)
            g_vehicleExamples.push_back(name + " (code " + std::to_string(code) + ", " +
                                        std::to_string(mesh.channels()[0].elementCount) +
                                        " verts)");

        std::vector<sr3mesh::Vertex> verts;
        try {
            verts = mesh.decodeChannel(0);
        } catch (const std::exception& ex) {
            ++s.decodeFailed;
            ++g_failureKinds[std::string("decodeChannel: ") + ex.what()];
            if (g_failures.size() < 20) g_failures.push_back(name + ": decode: " + ex.what());
            return;
        }
        if (verts.empty()) return;

        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& v : verts)
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], v.position[static_cast<size_t>(a)]);
                hi[a] = std::max(hi[a], v.position[static_cast<size_t>(a)]);
            }
        const float diag = std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0]) + (hi[1]-lo[1])*(hi[1]-lo[1]) +
                                     (hi[2]-lo[2])*(hi[2]-lo[2]));
        if (diag <= 0.0f) return;

        // A "long edge" is a PROXY for a partition error, and a proxy has to
        // be checked against what it is proxying for. Counting long edges in
        // the per-range expansion alone is not that check: the expansion
        // restarts at every boundary, so a cross-boundary triangle cannot be
        // constructed and anything it flags is either genuine long geometry
        // or the threshold being too tight for a compact mesh.
        //
        // The measurement that means something is the DIFFERENCE: expand the
        // whole buffer as one strip (the wrong way), expand per range (the
        // right way), and the drop between them is what respecting the
        // partition actually removes. What survives in the per-range figure
        // is the mesh's own geometry, not an error.
        std::vector<uint32_t> tris = mesh.drawGroupsLocated() ? mesh.triangleListForGroup(0)
                                                             : mesh.triangleListIndices();
        std::vector<uint32_t> whole = mesh.triangleListIndices();
        long long wholeLong = 0;
        for (size_t t = 0; t + 2 < whole.size(); t += 3) {
            if (whole[t] >= verts.size() || whole[t+1] >= verts.size() ||
                whole[t+2] >= verts.size()) continue;
            const uint32_t e[3][2] = {{whole[t],whole[t+1]},{whole[t+1],whole[t+2]},
                                      {whole[t],whole[t+2]}};
            for (const auto& ed : e) {
                const auto& p0 = verts[ed[0]].position;
                const auto& p1 = verts[ed[1]].position;
                float d = std::sqrt((p1[0]-p0[0])*(p1[0]-p0[0]) + (p1[1]-p0[1])*(p1[1]-p0[1]) +
                                    (p1[2]-p0[2])*(p1[2]-p0[2]));
                if (d > 0.2f * diag) { ++wholeLong; break; }
            }
        }
        s.wholeBufferLong += wholeLong;
        long long bridges = 0, count = 0;
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            if (tris[t] >= verts.size() || tris[t+1] >= verts.size() || tris[t+2] >= verts.size())
                continue;
            ++count;
            const uint32_t e[3][2] = {{tris[t],tris[t+1]},{tris[t+1],tris[t+2]},{tris[t],tris[t+2]}};
            for (const auto& ed : e) {
                const auto& p0 = verts[ed[0]].position;
                const auto& p1 = verts[ed[1]].position;
                float d = std::sqrt((p1[0]-p0[0])*(p1[0]-p0[0]) + (p1[1]-p0[1])*(p1[1]-p0[1]) +
                                    (p1[2]-p0[2])*(p1[2]-p0[2]));
                if (d > 0.2f * diag) { ++bridges; break; }
            }
        }
        s.triangles += count;
        s.bridges += bridges;
        if (bridges > 0) {
            ++s.withBridges;
            if (g_failures.size() < 20)
                g_failures.push_back(name + ": " + std::to_string(bridges) + " bridge(s) of " +
                                     std::to_string(count) + " (code " + std::to_string(code) + ")");
        }
    } catch (const std::exception& ex) {
        ++g_parseFailed;
        ++g_failureKinds[std::string("parse: ") + ex.what()];
        if (g_failures.size() < 20) g_failures.push_back(name + ": parse: " + ex.what());
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".ccmesh_pc") || endsWith(n, ".csmesh_pc")) {
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
        if (b.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
            printf("scanned %s (%d pairs so far)\n", argv[i], g_pairs);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("container failed %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== pipeline breadth sweep ===\n");
    printf("c/g pairs found        : %d\n", g_pairs);
    printf("parse failures         : %d\n", g_parseFailed);
    printf("no Mesh sub-block      : %d\n", g_noMeshBlock);
    printf("\nby layout code (spec §5):\n");
    printf("  long-edge triangles, whole-buffer strip (WRONG) vs per-range (RIGHT).\n");
    printf("  What the partition removes is the difference; what survives is geometry.\n\n");
    printf("  code  meshes  groups  1-range  decode  stride    whole ->  per-range   removed\n");
    for (const auto& kv : g_byCode) {
        const CodeStats& s = kv.second;
        printf("  %4d  %6d  %6d  %7d  %6d  %6d  %8lld -> %8lld   %5.1f%%\n", kv.first, s.meshes,
               s.groupsLocated, s.singleRange, s.decodeFailed, s.strideLawViolations,
               s.wholeBufferLong, s.bridges,
               s.wholeBufferLong > 0
                   ? 100.0 * static_cast<double>(s.wholeBufferLong - s.bridges) /
                         static_cast<double>(s.wholeBufferLong)
                   : 0.0);
    }
    if (!g_vehicleExamples.empty()) {
        printf("\nvehicle-layout meshes (codes 100/101), first few:\n");
        for (const auto& v : g_vehicleExamples) printf("    %s\n", v.c_str());
    }
    if (!g_failureKinds.empty()) {
        printf("\nfailure kinds:\n");
        for (const auto& kv : g_failureKinds)
            printf("  x%-4d %s\n", kv.second, kv.first.c_str());
    }
    if (!g_failures.empty()) {
        printf("\nfirst failures:\n");
        for (const auto& f : g_failures) printf("    %s\n", f.c_str());
    } else {
        printf("\nno failures.\n");
    }
    return 0;
}
