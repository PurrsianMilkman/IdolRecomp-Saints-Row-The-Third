// Real-data validation for sr3tree::Tree (spec-tree-format.md). Walks a
// .vpp_pc archive recursively, finds every .csrt_pc/.gsrt_pc pair, parses
// each, and reports population statistics against the spec's own claims:
// 11 distinct trees, 28 shipped copies across 8 bundles, 3-4 materials
// each (37 total, one shader hash), LOD slot counts, collision capsule
// counts, and the walk landing exactly on EOF in every file (spec Sec3's
// own oracle - a successful parse() IS that check).
//
// Usage: validate_tree <archive.vpp_pc> [more archives...]

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tree/tree.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    if (n < 0) n = 0;
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool stemFor(const std::string& n, const std::string& ext, std::string& stem) {
    if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) {
        stem = n.substr(0, n.size() - ext.size());
        return true;
    }
    return false;
}

bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    const bool ok = r.status == vpp::DecodeStatus::Ok ||
                    r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                    r.status == vpp::DecodeStatus::ContentValidated ||
                    r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                    r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

long long g_attempted = 0, g_parsed = 0, g_failed = 0;
std::map<std::string, long long> g_distinctByStem;
std::map<uint32_t, long long> g_materialCountHist, g_capsuleCountHist, g_presentLodHist;
std::set<uint32_t> g_shaderHashes;
long long g_totalMaterials = 0;
double g_radiusMin = 1e30, g_radiusMax = -1e30;

void check(const std::string& archive, const std::string& stem, const std::vector<uint8_t>& c,
          const std::vector<uint8_t>& g) {
    ++g_attempted;
    ++g_distinctByStem[stem];
    try {
        sr3tree::Tree t = sr3tree::Tree::parse(vpp::ByteView(c.data(), c.size()),
                                               vpp::ByteView(g.data(), g.size()));
        ++g_parsed;
        g_materialCountHist[t.materialCount()]++;
        g_capsuleCountHist[static_cast<uint32_t>(t.collisionCapsules().size())]++;
        g_presentLodHist[static_cast<uint32_t>(t.presentLodCount())]++;
        g_totalMaterials += static_cast<long long>(t.materials().size());
        for (const auto& m : t.materials()) {
            g_shaderHashes.insert(m.shaderHash);
        }
        for (const auto& cap : t.collisionCapsules()) {
            if (cap.radius < g_radiusMin) g_radiusMin = cap.radius;
            if (cap.radius > g_radiusMax) g_radiusMax = cap.radius;
        }
    } catch (const std::exception& e) {
        ++g_failed;
        std::printf("FAIL archive=%s stem=%s : %s\n", archive.c_str(), stem.c_str(), e.what());
    }
}

void walk(const vpp::Container& c, const std::string& archive) {
    std::map<std::string, size_t> csrt, gsrt;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".csrt_pc", stem)) csrt[stem] = i;
        else if (stemFor(n, ".gsrt_pc", stem)) gsrt[stem] = i;
    }
    for (const auto& kv : csrt) {
        std::vector<uint8_t> cb, gb;
        if (!entryBytes(c, kv.second, cb)) continue;
        auto git = gsrt.find(kv.first);
        if (git == gsrt.end()) {
            std::printf("NO_G_PAIR archive=%s stem=%s\n", archive.c_str(), kv.first.c_str());
            continue;
        }
        if (!entryBytes(c, git->second, gb)) continue;
        check(archive, kv.first, cb, gb);
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                walk(c.openNested(i), archive);
            } catch (const std::exception&) {
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <archive.vpp_pc> [more...]\n", argv[0]);
        return 1;
    }
    for (int a = 1; a < argc; ++a) {
        std::vector<uint8_t> bytes = readFile(argv[a]);
        if (bytes.empty()) {
            std::printf("OPEN_FAILED %s : could not read file\n", argv[a]);
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c, argv[a]);
        } catch (const std::exception& e) {
            std::printf("OPEN_FAILED %s : %s\n", argv[a], e.what());
        }
    }

    std::printf("\n=== .csrt_pc/.gsrt_pc population ===\n");
    std::printf("  shipped copies attempted : %lld\n", g_attempted);
    std::printf("  distinct stems            : %zu\n", g_distinctByStem.size());
    std::printf("  parsed OK                 : %lld\n", g_parsed);
    std::printf("  failed                    : %lld\n", g_failed);
    std::printf("  total materials decoded   : %lld\n", g_totalMaterials);
    std::printf("  distinct shader hashes     : %zu\n", g_shaderHashes.size());
    std::printf("  capsule radius range       : %.6f .. %.6f\n", g_radiusMin, g_radiusMax);

    std::printf("  material count histogram  :");
    for (const auto& kv : g_materialCountHist) std::printf(" %u:%lld", kv.first, kv.second);
    std::printf("\n");

    std::printf("  present-LOD-count histogram:");
    for (const auto& kv : g_presentLodHist) std::printf(" %u:%lld", kv.first, kv.second);
    std::printf("\n");

    std::printf("  collision-capsule-count histogram:");
    for (const auto& kv : g_capsuleCountHist) std::printf(" %u:%lld", kv.first, kv.second);
    std::printf("\n");

    return 0;
}
