// Real-data validation for `sr3vehicle`, against spec-vehicle-geometry.md's
// own published population figures - 393 pairs, 9,951 parts, and the
// structural invariants that make those numbers meaningful.
//
// The interesting checks are the CROSS-FORMAT ones, because nothing in
// either decoding refers to the other:
//
//   * the `.gcar_pc`'s leading u32 must equal the embedded Mesh sub-block's
//     check value (spec §8, 393/393). Two separately located structures in
//     two separate files agreeing on a 32-bit value is not something a
//     wrong offset produces;
//   * the `.ccmesh_pc` chain reached from header +0x04 must land on a Mesh
//     sub-block, i.e. `sr3geometry` and `sr3mesh` must parse a vehicle with
//     no changes at all - which is the spec's central claim (§1): this is
//     an assembly container, not a new mesh format.
//
// Also checks the 4x4 transform's row-major signature (`[3][3] == 1.0` and
// column 3 of rows 0-2 zero, 9,951/9,951) with the column-major reading as
// its own control.
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

int g_pairs = 0, g_parsed = 0, g_failed = 0;
long long g_parts = 0;
int g_minParts = 1 << 30, g_maxParts = 0;
int g_withMorph = 0, g_morphAbsent = 0, g_gcarOffsetBeyondCcar = 0;
int g_meshChainOk = 0, g_crossRefOk = 0, g_meshTried = 0;
long long g_rowMajor = 0, g_colMajor = 0, g_transforms = 0;
long long g_parentValid = 0, g_parentTotal = 0;
int g_rootsTotal = 0;
std::map<uint32_t, long long> g_types;
std::set<std::string> g_sampleNames;
std::vector<std::string> g_failures;
std::map<std::string, int> g_meshFailKinds;
std::map<uint32_t, long long> g_layoutCodes;
std::map<std::string, int> g_decodeFailKinds;
long long g_channelsDecoded = 0, g_channelsFailed = 0, g_vehVerts = 0;
long long g_normalUnit = 0, g_normalTotal = 0;
int g_strideViolations = 0;
uint32_t g_maxRigidPart = 0;
int g_groupsLocated = 0, g_groupsNotLocated = 0;
size_t g_maxGroups = 0, g_maxRangesInAGroup = 0;
long long g_vehRanges = 0;
std::vector<std::string> g_notLocatedNames;
int g_printed = 0;

void examine(const std::string& name, const std::vector<uint8_t>& cb,
             const std::vector<uint8_t>& gb) {
    ++g_pairs;
    sr3vehicle::Vehicle veh;
    try {
        veh = sr3vehicle::Vehicle::parse(vpp::ByteView(cb.data(), cb.size()));
        ++g_parsed;
    } catch (const std::exception& ex) {
        ++g_failed;
        if (g_failures.size() < 8) g_failures.push_back(name + ": " + ex.what());
        return;
    }

    const int partCount = static_cast<int>(veh.parts().size());
    g_parts += partCount;
    if (partCount < g_minParts) g_minParts = partCount;
    if (partCount > g_maxParts) g_maxParts = partCount;
    if (veh.hasMorph()) {
        ++g_withMorph;
        if (veh.gcarMorphOffset() >= cb.size()) ++g_gcarOffsetBeyondCcar;
    } else {
        ++g_morphAbsent;
    }

    for (const auto& p : veh.parts()) {
        ++g_transforms;
        // Row-major signature: [3][3] == 1 and column 3 of rows 0-2 zero.
        const bool row = std::fabs(p.transform[15] - 1.0f) < 1e-6f &&
                         std::fabs(p.transform[3]) < 1e-6f &&
                         std::fabs(p.transform[7]) < 1e-6f &&
                         std::fabs(p.transform[11]) < 1e-6f;
        // Column-major control: bottom ROW zero instead.
        const bool col = std::fabs(p.transform[15] - 1.0f) < 1e-6f &&
                         std::fabs(p.transform[12]) < 1e-6f &&
                         std::fabs(p.transform[13]) < 1e-6f &&
                         std::fabs(p.transform[14]) < 1e-6f;
        if (row) ++g_rowMajor;
        if (col) ++g_colMajor;

        ++g_parentTotal;
        if (p.isRoot() || p.parentIndex < static_cast<uint32_t>(partCount)) ++g_parentValid;
        if (p.isRoot()) ++g_rootsTotal;
        ++g_types[p.partType];
        if (g_sampleNames.size() < 16) g_sampleNames.insert(p.name);
    }

    // CROSS-FORMAT: the embedded .ccmesh_pc chain must parse with the
    // existing readers, unchanged, and its check value must equal the
    // .gcar_pc's leading word.
    ++g_meshTried;
    try {
        // The material block is ABSENT in 393/393 vehicles, so the geometry
        // block must be entered at the offset the vehicle header states -
        // parseAt, not parse.
        sr3geometry::GeometryBlock geo = sr3geometry::GeometryBlock::parseAt(
            vpp::ByteView(cb.data(), cb.size()), veh.meshRegionOffset());
        if (!geo.hasMeshSubBlock()) {
            if (g_failures.size() < 8) g_failures.push_back(name + ": no Mesh sub-block");
            return;
        }
        sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
            vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
            vpp::ByteView(gb.data(), gb.size()));
        ++g_meshChainOk;

        // Team A just found an invented group-count cap of 64 silently
        // dropping 286/372 vehicles from their own statistics, with the
        // harness reporting a confident N/N and never naming what it
        // excluded. `sr3mesh` has caps of exactly that shape - a range
        // count rejected above 4096, and a draw-group search window of
        // 4096 bytes - and my own §9.28 packing measurement SKIPPED any
        // vehicle where drawGroupsLocated() was false, without counting
        // them. So: count the exclusions instead of stepping over them.
        if (mesh.drawGroupsLocated()) {
            ++g_groupsLocated;
            size_t rangeCount = 0, maxPerGroup = 0;
            for (const auto& gr : mesh.drawGroups()) {
                rangeCount += gr.size();
                if (gr.size() > maxPerGroup) maxPerGroup = gr.size();
            }
            if (mesh.drawGroups().size() > g_maxGroups) g_maxGroups = mesh.drawGroups().size();
            if (maxPerGroup > g_maxRangesInAGroup) g_maxRangesInAGroup = maxPerGroup;
            g_vehRanges += static_cast<long long>(rangeCount);
        } else {
            ++g_groupsNotLocated;
            if (g_notLocatedNames.size() < 8) g_notLocatedNames.push_back(name);
        }

        // "The Mesh sub-block parses" is NOT "the geometry decodes". Layout
        // codes 100/101 carry a rigid PART INDEX where characters carry
        // blend weights and indices, and that branch of decodeChannel() has
        // never been exercised - reaching vehicles was the whole point of
        // getting here. Decode every channel and report, rather than
        // assuming a clean parse implies clean vertices.
        for (size_t ci = 0; ci < mesh.channels().size(); ++ci) {
            const auto& ch = mesh.channels()[ci];
            ++g_layoutCodes[ch.layoutCode];
            if (!ch.strideMatchesLaw) ++g_strideViolations;
            try {
                std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(ci);
                ++g_channelsDecoded;
                if (verts.empty()) continue;
                g_vehVerts += static_cast<long long>(verts.size());
                for (const auto& v : verts) {
                    float len = std::sqrt(v.normal[0]*v.normal[0] + v.normal[1]*v.normal[1] +
                                          v.normal[2]*v.normal[2]);
                    ++g_normalTotal;
                    if (len > 0.94f && len < 1.06f) ++g_normalUnit;
                    if (v.rigidPartIndex > g_maxRigidPart) g_maxRigidPart = v.rigidPartIndex;
                }
            } catch (const std::exception& ex) {
                ++g_channelsFailed;
                ++g_decodeFailKinds[ex.what()];
            }
        }

        uint32_t xref = 0;
        if (sr3vehicle::readGcarCrossReference(vpp::ByteView(gb.data(), gb.size()), xref) &&
            xref == mesh.checkValue()) {
            ++g_crossRefOk;
        }
    } catch (const std::exception& ex) {
        ++g_meshFailKinds[ex.what()];
        // Reported via the counters, not thrown - the mesh region offset
        // points at the 0x424BD00D sub-header, and MeshBlock expects the
        // Mesh sub-block itself, so a mismatch here is informative.
    }

    if (g_printed < 3) {
        ++g_printed;
        printf("\n%s  (%d parts, anchor \"%s\", morph %s)\n", name.c_str(), partCount,
               veh.anchorName().c_str(), veh.hasMorph() ? "yes" : "no");
        for (int i = 0; i < partCount && i < 8; ++i) {
            const auto& p = veh.parts()[static_cast<size_t>(i)];
            const char* tn = sr3vehicle::partTypeName(p.partType);
            printf("    [%2d] %-22s type %2u %-22s parent %3d  t(%.2f %.2f %.2f)\n", i,
                   p.name.c_str(), p.partType, tn ? tn : "(open)",
                   p.isRoot() ? -1 : static_cast<int>(p.parentIndex), p.translationX(),
                   p.translationY(), p.translationZ());
        }
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".ccar_pc")) {
            std::string gn = n;
            size_t d = gn.find_last_of('.');
            if (d != std::string::npos) gn[d + 1] = 'g';
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j)
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            examine(n, cb, gb);
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
            printf("\nscanned %s (%d pairs so far)\n", argv[i], g_pairs);
            fflush(stdout);
        } catch (const std::exception&) {}
    }

    printf("\n=== sr3vehicle validation (spec-vehicle-geometry.md) ===\n");
    printf(".ccar_pc found        : %d   (spec §2: 393)\n", g_pairs);
    printf("parsed OK             : %d\n", g_parsed);
    printf("failed                : %d\n", g_failed);
    printf("parts total           : %lld   (spec §1: 9,951)\n", g_parts);
    if (g_parsed) printf("part count range      : %d .. %d   (spec §3: 2 .. 43)\n",
                         g_minParts, g_maxParts);
    printf("with embedded morph   : %d   (spec §2: 326)\n", g_withMorph);
    printf("morph absent (-1)     : %d   (spec §2: 67)\n", g_morphAbsent);
    printf("gcar morph offset >= ccar size : %d / %d   (spec §3: all 326)\n",
           g_gcarOffsetBeyondCcar, g_withMorph);
    printf("\nCROSS-FORMAT (nothing in either decoding refers to the other):\n");
    printf("  embedded Mesh sub-block parsed by sr3mesh unchanged : %d / %d\n",
           g_meshChainOk, g_meshTried);
    printf("  .gcar_pc leading u32 == Mesh check value            : %d / %d   (spec §8: 393/393)\n",
           g_crossRefOk, g_meshChainOk);
    printf("\nEXCLUSIONS - what this harness silently skipped, now counted:\n");
    printf("  draw groups located / NOT located : %d / %d\n", g_groupsLocated, g_groupsNotLocated);
    printf("  max groups in one vehicle         : %zu\n", g_maxGroups);
    printf("  max ranges in one group           : %zu   (sr3mesh rejects a count > 4096)\n",
           g_maxRangesInAGroup);
    printf("  total draw ranges                 : %lld\n", g_vehRanges);
    for (const auto& n : g_notLocatedNames) printf("    not located: %s\n", n.c_str());

    printf("\nVERTEX DECODE (does the geometry actually READ, not just parse?):\n");
    printf("  channels decoded / failed : %lld / %lld\n", g_channelsDecoded, g_channelsFailed);
    printf("  vertices decoded          : %lld\n", g_vehVerts);
    printf("  normals unit within 6%%    : %lld / %lld\n", g_normalUnit, g_normalTotal);
    printf("  stride-law violations     : %d\n", g_strideViolations);
    printf("  max rigid part index      : %u\n", g_maxRigidPart);
    printf("  layout codes seen         :");
    for (const auto& kv : g_layoutCodes) printf(" %u(x%lld)", kv.first, kv.second);
    printf("\n");
    for (const auto& kv : g_decodeFailKinds) printf("    decode fail x%d: %s\n", kv.second,
                                                    kv.first.c_str());

    printf("\nTRANSFORM ORIENTATION (spec §4):\n");
    printf("  row-major signature   : %lld / %lld\n", g_rowMajor, g_transforms);
    printf("  column-major CONTROL  : %lld / %lld\n", g_colMajor, g_transforms);
    printf("\nHIERARCHY:\n");
    printf("  parent -1 or < count  : %lld / %lld   (spec §4: 9,951/9,951)\n",
           g_parentValid, g_parentTotal);
    printf("  roots (forest, not one tree) : %d\n", g_rootsTotal);
    printf("\npart types seen:\n");
    for (const auto& kv : g_types) {
        const char* tn = sr3vehicle::partTypeName(kv.first);
        printf("    %2u : %6lld  %s\n", kv.first, kv.second, tn ? tn : "(OPEN - too rare to read)");
    }
    printf("\nsample part names: ");
    for (const auto& n : g_sampleNames) printf("%s ", n.c_str());
    printf("\n");
    if (!g_meshFailKinds.empty()) {
        printf("\nmesh-parse failure kinds:\n");
        for (const auto& kv : g_meshFailKinds)
            printf("  x%-4d %s\n", kv.second, kv.first.c_str());
    }
    if (!g_failures.empty()) {
        printf("\nfailures:\n");
        for (const auto& f : g_failures) printf("    %s\n", f.c_str());
    }
    return 0;
}
