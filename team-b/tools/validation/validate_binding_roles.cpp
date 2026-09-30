// Is the texture role carried by the SLOT INDEX or by the PARAMETER HASH?
//
// `sr3geometry::MaterialBinding::diffuse()` currently returns slot 0, and
// HANDOFF §9.24 recorded "slot 0 diffuse, slot 1 normal" from character
// meshes. Team A now reports that is an over-generalization: the same
// sampler hash 0x2808EB90 sits at slot 1 on character meshes and slot 0 on
// vehicles, so the slot index is only position-within-material and the role
// travels with the hash.
//
// If true, diffuse() returns the WRONG texture on vehicles while looking
// entirely healthy - the silently-plausible failure this project keeps
// refusing to ship. So it gets measured before anything is changed.
//
// The test: tabulate (parameter hash x slot index) separately over
// characters and vehicles. If roles are slot-keyed, each hash sticks to one
// slot in both populations. If they are hash-keyed, a hash will appear at
// DIFFERENT slots in the two populations while keeping its identity.
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_binding.h"
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
static std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

struct Pop {
    int files = 0, located = 0;
    long long runs = 0, entries = 0;
    std::map<uint32_t, std::map<uint32_t, long long>> hashBySlot; // hash -> slot -> count
    std::map<uint32_t, long long> hashEndsD, hashEndsN, hashTotal;
    std::set<std::string> sampleNames;
};
Pop g_char, g_veh;

void tally(Pop& pop, const sr3geometry::MaterialBindings& b) {
    ++pop.files;
    if (!b.located()) return;
    ++pop.located;
    for (const auto& m : b.materials()) {
        ++pop.runs;
        for (const auto& t : m.textures) {
            ++pop.entries;
            pop.hashBySlot[t.paramHash][t.slot] += 1;
            ++pop.hashTotal[t.paramHash];
            const std::string low = lower(t.name);
            if (endsWith(low, "_d.tga") || endsWith(low, "_dp.tga")) ++pop.hashEndsD[t.paramHash];
            if (endsWith(low, "_n.tga")) ++pop.hashEndsN[t.paramHash];
            if (pop.sampleNames.size() < 14) pop.sampleNames.insert(t.name);
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
                    sr3geometry::GeometryBlock geo = sr3geometry::GeometryBlock::parse(
                        vpp::ByteView(cb.data(), cb.size()), mat);
                    sub = geo.offset();
                }
                sr3geometry::GeometryBlock geo =
                    sr3geometry::GeometryBlock::parseAt(vpp::ByteView(cb.data(), cb.size()), sub);
                if (!geo.hasMeshSubBlock()) continue;
                sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
                    vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
                    vpp::ByteView(gb.data(), gb.size()));
                sr3geometry::MaterialBindings b = sr3geometry::MaterialBindings::parse(
                    vpp::ByteView(cb.data(), cb.size()), sub,
                    geo.meshSubBlockOffset() + mesh.cLength());
                tally(isCar ? g_veh : g_char, b);
            } catch (const std::exception&) {}
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

void report(const char* label, const Pop& p) {
    printf("\n=== %s ===\n", label);
    printf("files %d, bindings located %d, runs %lld, entries %lld\n", p.files, p.located,
           p.runs, p.entries);
    printf("  hash        total   slot distribution            _d      _n\n");
    int shown = 0;
    for (const auto& kv : p.hashTotal) {
        if (kv.second < 20) continue;
        printf("  0x%08X %7lld   ", kv.first, kv.second);
        std::string slots;
        for (const auto& s : p.hashBySlot.at(kv.first))
            slots += "s" + std::to_string(s.first) + ":" + std::to_string(s.second) + " ";
        printf("%-28s", slots.c_str());
        auto d = p.hashEndsD.find(kv.first);
        auto n = p.hashEndsN.find(kv.first);
        printf("%6lld  %6lld\n", d == p.hashEndsD.end() ? 0 : d->second,
               n == p.hashEndsN.end() ? 0 : n->second);
        if (++shown >= 10) break;
    }
    printf("  sample names:");
    for (const auto& s : p.sampleNames) printf(" %s", s.c_str());
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
    report("CHARACTER MESHES", g_char);
    report("VEHICLES", g_veh);

    printf("\n=== DOES A HASH KEEP ITS SLOT ACROSS POPULATIONS? ===\n");
    for (const auto& kv : g_char.hashBySlot) {
        auto v = g_veh.hashBySlot.find(kv.first);
        if (v == g_veh.hashBySlot.end()) continue;
        printf("  0x%08X  characters:", kv.first);
        for (const auto& s : kv.second) printf(" s%u", s.first);
        printf("   vehicles:");
        for (const auto& s : v->second) printf(" s%u", s.first);
        bool same = kv.second.size() == v->second.size();
        if (same) {
            auto a = kv.second.begin();
            auto b2 = v->second.begin();
            for (; a != kv.second.end(); ++a, ++b2)
                if (a->first != b2->first) same = false;
        }
        printf("   -> %s\n", same ? "SAME slot(s)" : "DIFFERENT slot(s)");
    }
    return 0;
}
