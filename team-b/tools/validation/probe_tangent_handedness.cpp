// Independent verification of the claim in include/sr3mesh/mesh_block.h:
// "the tangent's fourth byte takes exactly two values, 0 and 255: it is a
// bitangent-handedness sign" (tangentW / tangentHandedness / bitangent()).
//
// A relayed report claims this holds at ~100% on five carriers but only
// 17.94% extreme values (0 or 255) on vehicles - i.e. on vehicle meshes the
// byte would NOT be concentrated at the two extremes, meaning bitangent()
// would return garbage there. Rather than accept that relayed number, this
// probe re-derives the tangentW distribution directly from our own data,
// using the SHIPPING readers only (MaterialBlock/GeometryBlock/Vehicle/
// MeshBlock) - no reimplementation of the format.
//
// Carriers, kept SEPARATE, never pooled:
//   - "characters_items (.ccmesh_pc family)" - the .ccmesh_pc/.gcmesh_pc
//     pair format, read identically whether it came from characters.vpp_pc
//     or items.vpp_pc (same parse chain: MaterialBlock -> GeometryBlock ->
//     MeshBlock). This mirrors how the task itself groups them as one
//     carrier bullet.
//   - "vehicles (.ccar_pc family)" - the .ccar_pc/.gcar_pc pair format
//     (Vehicle -> GeometryBlock::parseAt -> MeshBlock).
//
// The carrier a pair belongs to is decided by the ENTRY'S OWN extension,
// not by which archive it was found in - so if a .ccar_pc entry ever turned
// up while walking characters.vpp_pc (or vice versa), it would still land
// in the correct carrier bucket. A secondary per-ARCHIVE breakdown is also
// tallied and printed, purely as a transparency/sanity check that no such
// cross-contamination is in fact occurring, and that characters and items
// individually look like each other before being read together as one
// carrier.
//
// Per carrier this reports: the tangentW histogram, the exact-0 and
// exact-255 counts, the extreme (0 or 255) fraction, the intermediate
// (1..254) fraction with its most common values, N, and every skip reason
// (no g-pair, parse threw, no Mesh sub-block, unprobed layout code, channel
// known to have no tangent field at all, channel decode threw) - a "0" in
// any skip bucket is itself reported, not omitted.
//
// Usage: probe_tangent_handedness <archive.vpp_pc> [...]

#include <algorithm>
#include <array>
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

// ---- I/O + container-walk helpers, copied from validate_vehicle_runs.cpp
// (that file is not modified; this is a fresh copy of its proven scaffolding
// so this probe reads the archives the same way that harness does). ----
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

// ---- tallies ---------------------------------------------------------------
struct Stats {
    std::string label;

    long long pairsFound = 0;
    long long skippedCUnreadable = 0;   // the .ccmesh_pc/.ccar_pc entry itself didn't decode
    long long skippedNoPair = 0;        // no matching .gcmesh_pc/.gcar_pc entry found/readable
    long long skippedParseThrew = 0;    // MaterialBlock/GeometryBlock/Vehicle/MeshBlock::parse threw
    long long skippedNoMeshSubBlock = 0;

    long long meshesOk = 0;
    long long meshesWithTangentChannel = 0;
    long long meshesNoTangentChannel = 0;
    long long meshesAllChannelsUnknownLayout = 0; // couldn't tell for ANY channel in this mesh

    long long channelsTotal = 0;
    long long channelsUnknownLayout = 0;   // layoutInfoFor(...).known == false (unprobed code)
    long long channelsKnownNoTangent = 0;  // known, but that layout carries no tangent field
    long long channelsWithTangent = 0;     // known, has tangent, successfully decoded
    long long channelDecodeThrew = 0;

    std::map<int, long long> unknownLayoutCodes;

    long long verticesTallied = 0;
    std::array<uint64_t, 256> tangentWHist{};

    // Extension sanity, archive-level buckets only (see below).
    long long entriesCcmeshSeen = 0;
    long long entriesCcarSeen = 0;
};

std::map<std::string, Stats> g_carrierStats; // keyed by format ("characters_items ..." / "vehicles ...")
std::map<std::string, Stats> g_archiveStats; // keyed by originating archive file name, for transparency

Stats& statsFor(std::map<std::string, Stats>& m, const std::string& key) {
    auto it = m.find(key);
    if (it == m.end()) {
        Stats s;
        s.label = key;
        it = m.emplace(key, s).first;
    }
    return it->second;
}

// Applies the same tally to every Stats object in `targets` (the carrier
// bucket and the archive bucket at once), decoding each tangent-bearing
// channel exactly once regardless of how many targets there are.
void tallyMesh(std::vector<Stats*>& targets, const sr3mesh::MeshBlock& mesh) {
    for (Stats* st : targets) ++st->meshesOk;

    std::vector<char> anyTangent(targets.size(), 0);
    std::vector<char> anyKnown(targets.size(), 0);

    for (size_t ci = 0; ci < mesh.channels().size(); ++ci) {
        const sr3mesh::Channel& ch = mesh.channels()[ci];
        for (Stats* st : targets) ++st->channelsTotal;

        sr3mesh::LayoutInfo info = sr3mesh::layoutInfoFor(ch.layoutCode);
        if (!info.known) {
            for (Stats* st : targets) {
                ++st->channelsUnknownLayout;
                st->unknownLayoutCodes[static_cast<int>(ch.layoutCode)] += 1;
            }
            continue;
        }
        for (size_t t = 0; t < targets.size(); ++t) anyKnown[t] = 1;

        if (!info.hasTangent) {
            for (Stats* st : targets) ++st->channelsKnownNoTangent;
            continue;
        }
        for (size_t t = 0; t < targets.size(); ++t) anyTangent[t] = 1;

        try {
            std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(ci);
            for (Stats* st : targets) ++st->channelsWithTangent;
            for (const sr3mesh::Vertex& v : verts) {
                for (Stats* st : targets) {
                    ++st->tangentWHist[v.tangentW];
                    ++st->verticesTallied;
                }
            }
        } catch (const std::exception&) {
            for (Stats* st : targets) ++st->channelDecodeThrew;
        }
    }

    for (size_t t = 0; t < targets.size(); ++t) {
        if (anyTangent[t]) ++targets[t]->meshesWithTangentChannel;
        else ++targets[t]->meshesNoTangentChannel;
        if (!anyKnown[t]) ++targets[t]->meshesAllChannelsUnknownLayout;
    }
}

void handleEntry(const vpp::Container& c, size_t i, const std::string& n,
                 const std::string& archiveLabel, const std::string& gExt,
                 size_t cExtLen, bool isVehicle) {
    const std::string carrierKey = isVehicle ? "vehicles (.ccar_pc family)"
                                             : "characters_items (.ccmesh_pc family)";
    Stats& cst = statsFor(g_carrierStats, carrierKey);
    Stats& ast = statsFor(g_archiveStats, archiveLabel);
    if (isVehicle) ++ast.entriesCcarSeen; else ++ast.entriesCcmeshSeen;
    std::vector<Stats*> both{&cst, &ast};

    std::vector<uint8_t> cb, gb;
    if (!entryBytes(c, i, cb)) {
        for (Stats* s : both) ++s->skippedCUnreadable;
        return;
    }

    const std::string gn = n.substr(0, n.size() - cExtLen) + gExt;
    bool foundG = false;
    for (size_t j = 0; j < c.entries().size(); ++j) {
        if (c.entries()[j].name == gn) { foundG = entryBytes(c, j, gb); break; }
    }
    if (!foundG || gb.empty()) {
        for (Stats* s : both) ++s->skippedNoPair;
        return;
    }
    for (Stats* s : both) ++s->pairsFound;

    try {
        sr3geometry::GeometryBlock geo;
        if (isVehicle) {
            sr3vehicle::Vehicle v = sr3vehicle::Vehicle::parse(vpp::ByteView(cb.data(), cb.size()));
            geo = sr3geometry::GeometryBlock::parseAt(vpp::ByteView(cb.data(), cb.size()),
                                                      v.meshRegionOffset());
        } else {
            sr3geometry::MaterialBlock mat =
                sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
            geo = sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
        }
        if (!geo.hasMeshSubBlock()) {
            for (Stats* s : both) ++s->skippedNoMeshSubBlock;
            return;
        }
        sr3mesh::MeshBlock mesh =
            sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
                                      vpp::ByteView(gb.data(), gb.size()));
        tallyMesh(both, mesh);
    } catch (const std::exception&) {
        for (Stats* s : both) ++s->skippedParseThrew;
    }
}

void walk(const vpp::Container& c, const std::string& archiveLabel) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".ccmesh_pc")) {
            handleEntry(c, i, n, archiveLabel, ".gcmesh_pc", 10, false);
        }
        if (endsWith(n, ".ccar_pc")) {
            handleEntry(c, i, n, archiveLabel, ".gcar_pc", 8, true);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i), archiveLabel); } catch (const std::exception&) {}
        }
    }
}

void printCarrier(const Stats& st) {
    printf("\n--- carrier: %s ---\n", st.label.c_str());
    printf("  c/g pairs found                 : %lld\n", st.pairsFound);
    printf("  skipped: c entry unreadable      : %lld\n", st.skippedCUnreadable);
    printf("  skipped: no g-pair / unreadable  : %lld\n", st.skippedNoPair);
    printf("  skipped: parse threw             : %lld\n", st.skippedParseThrew);
    printf("  skipped: no Mesh sub-block       : %lld\n", st.skippedNoMeshSubBlock);
    printf("  meshes parsed OK                 : %lld\n", st.meshesOk);
    printf("  meshes with >=1 tangent channel  : %lld\n", st.meshesWithTangentChannel);
    printf("  meshes with 0 tangent channels   : %lld\n", st.meshesNoTangentChannel);
    printf("  meshes: every channel's layout code unprobed (open item, spec Sec5): %lld\n",
           st.meshesAllChannelsUnknownLayout);
    printf("  channels total                   : %lld\n", st.channelsTotal);
    printf("  channels: unknown/unprobed layout: %lld", st.channelsUnknownLayout);
    if (!st.unknownLayoutCodes.empty()) {
        printf("  (codes:");
        for (const auto& kv : st.unknownLayoutCodes) printf(" %d x%lld", kv.first, kv.second);
        printf(")");
    }
    printf("\n");
    printf("  channels: known layout, no tangent field: %lld\n", st.channelsKnownNoTangent);
    printf("  channels: has tangent, decoded   : %lld\n", st.channelsWithTangent);
    printf("  channels: decode threw           : %lld\n", st.channelDecodeThrew);

    const uint64_t extreme0 = st.tangentWHist[0];
    const uint64_t extreme255 = st.tangentWHist[255];
    const uint64_t extreme = extreme0 + extreme255;
    uint64_t intermediate = 0;
    for (int v = 1; v <= 254; ++v) intermediate += st.tangentWHist[v];
    const uint64_t N = extreme + intermediate;

    printf("  tangentW N (vertices tallied)    : %llu\n", static_cast<unsigned long long>(N));
    if (N == 0) {
        printf("  -- no tangentW samples for this carrier --\n");
        return;
    }
    printf("  == 0                             : %llu (%.4f%%)\n",
           static_cast<unsigned long long>(extreme0), 100.0 * static_cast<double>(extreme0) / N);
    printf("  == 255                           : %llu (%.4f%%)\n",
           static_cast<unsigned long long>(extreme255), 100.0 * static_cast<double>(extreme255) / N);
    printf("  extreme (0 or 255) TOTAL         : %llu (%.4f%%)\n",
           static_cast<unsigned long long>(extreme), 100.0 * static_cast<double>(extreme) / N);
    printf("  intermediate (1..254) TOTAL      : %llu (%.4f%%)\n",
           static_cast<unsigned long long>(intermediate), 100.0 * static_cast<double>(intermediate) / N);

    std::vector<std::pair<uint64_t, int>> interm;
    for (int v = 1; v <= 254; ++v) {
        if (st.tangentWHist[v] > 0) interm.push_back({st.tangentWHist[v], v});
    }
    std::sort(interm.rbegin(), interm.rend());
    printf("  distinct intermediate values seen: %zu\n", interm.size());
    printf("  most common intermediate values (value : count):\n");
    for (size_t k = 0; k < interm.size() && k < 10; ++k) {
        printf("    %3d : %llu\n", interm[k].second, static_cast<unsigned long long>(interm[k].first));
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string path = argv[i];
        std::vector<uint8_t> b = readFile(path);
        if (b.empty()) { printf("could not read %s\n", path.c_str()); continue; }
        size_t slash = path.find_last_of("/\\");
        std::string label = (slash == std::string::npos) ? path : path.substr(slash + 1);
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c, label);
        } catch (const std::exception& e) {
            printf("archive %s: %s\n", path.c_str(), e.what());
        }
        printf("scanned %s\n", path.c_str());
    }

    printf("\n============================================================\n");
    printf("TANGENT FOURTH-BYTE (tangentW) HANDEDNESS PROBE\n");
    printf("============================================================\n");
    printf("Claim under test (include/sr3mesh/mesh_block.h): the tangent's\n");
    printf("fourth byte takes EXACTLY two values, 0 and 255 - a bitangent-\n");
    printf("handedness sign (tangentHandedness / bitangent()).\n\n");
    printf("Relayed report under test: ~100%% extreme on five carriers, but\n");
    printf("only 17.94%% extreme on vehicles. Measured independently below,\n");
    printf("per carrier, never pooled with each other.\n");

    printf("\n============================================================\n");
    printf("PER-CARRIER RESULTS (the mandated split, by entry extension)\n");
    printf("============================================================\n");
    for (auto& kv : g_carrierStats) printCarrier(kv.second);

    printf("\n============================================================\n");
    printf("PER-ARCHIVE DETAIL (supplementary transparency/sanity check -\n");
    printf("same underlying data, broken out further by originating file)\n");
    printf("============================================================\n");
    for (auto& kv : g_archiveStats) {
        printf("\n[archive %s] entries seen: .ccmesh_pc-family=%lld  .ccar_pc-family=%lld\n",
               kv.second.label.c_str(), kv.second.entriesCcmeshSeen, kv.second.entriesCcarSeen);
        printCarrier(kv.second);
    }

    return 0;
}
