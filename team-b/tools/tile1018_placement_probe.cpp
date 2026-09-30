// Throwaway investigation tool (follow-on task, 2026-09-30): tests, against
// REAL bytes for tile 1018 specifically, whether SR3Z record `+12`'s
// "name index into the source zone file's own level-mesh name list"
// (spec-world-streaming.md Sec10.7 bullet 3, include/sr3zone/zone_header.h
// RecordNameIndex() - existence/boundedness CONFIRMED, TARGET explicitly
// OPEN there) correlates with any real, observable ordering this project
// can already read: the tile's own `.asm_pc` manifest registration order
// (sr3asm::AsmManifest - the "1018" ContainerRecord's own entries(), which
// per spec-world-streaming.md Sec10.5 receives a `~L1` registration for
// EVERY level-mesh object placed anywhere in the tile, unconditionally -
// the natural candidate for "the source zone file's own level-mesh name
// list"), or as a control, "1018h0"'s own manifest entries() directly.
//
// THE TEST, stated precisely so it is falsifiable: h0 is fine-cell N=0,
// geometrically the (low x, high z) quadrant of the tile (spec-world-
// streaming.md Sec10.1/Sec10.6(g): "row 0 being the higher-z row", N=0 at
// tile-centre offset (-80, +70)). If a candidate name-index ordering is
// REAL, then among the tile's real SR3Z records, the subset whose resolved
// name (via that ordering) is one of 1018h0's own 22 real .clmesh_pc names
// should show RecordPosition() values concentrated in the tile's (low x,
// high z) quadrant - a real geometric signature a wrong/coincidental
// ordering would not reproduce (a wrong join is noise, spread over all 4
// quadrants roughly evenly). This is checked directly below, not assumed.
//
// Usage: tile1018_placement_probe <sr3_city_0.vpp_pc>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3asm/manifest.h"
#include "sr3zone/zone_header.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> buf;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return buf;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0) {
        buf.resize(static_cast<size_t>(size));
        if (fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
    }
    fclose(f);
    return buf;
}

bool endsWithNoCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool entryBytes(const vpp::Container& container, size_t index, std::vector<uint8_t>& out) {
    const vpp::Entry& entry = container.entries()[index];
    if (entry.payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView raw = container.rawEntryBytes(index);
        out.assign(raw.data(), raw.data() + raw.size());
        return true;
    }
    vpp::DecompressResult r = container.decompressEntry(index);
    bool usable = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                  r.status == vpp::DecodeStatus::ContentValidated ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                  r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!usable) return false;
    out = std::move(r.data);
    return true;
}

bool findEntryTopLevel(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    }
    return false;
}

// Finds `name` recursively, descending into nested Raw containers.
bool findEntryDeep(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = c.openNested(i);
            if (findEntryDeep(nested, name, out)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

std::vector<std::string> clmeshNamesInManifestOrder(const sr3asm::AsmManifest& mani, const std::string& recordName) {
    std::vector<std::string> out;
    for (const auto& rec : mani.records()) {
        if (rec.name != recordName) continue;
        for (const auto& e : rec.entries) {
            if (endsWithNoCase(e.name, ".clmesh_pc")) out.push_back(e.name);
        }
        break;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: tile1018_placement_probe <sr3_city_0.vpp_pc>\n");
        return 1;
    }
    std::vector<uint8_t> archive = readFile(argv[1]);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", argv[1]);
        return 1;
    }
    vpp::Container top(vpp::ByteView(archive.data(), archive.size()));

    // ---- 1. The manifest: real registration order for "1018" (candidate B,
    // the tile-wide distinct level-mesh list) and "1018h0" (candidate C,
    // the fine-cell's own list, and also this session's own ground truth
    // for "which 22 names actually live in h0"). --------------------------
    std::vector<uint8_t> asmBytes;
    if (!findEntryTopLevel(top, "1018.asm_pc", asmBytes) || asmBytes.empty()) {
        std::fprintf(stderr, "could not find top-level '1018.asm_pc'\n");
        return 1;
    }
    sr3asm::AsmManifest mani;
    try {
        mani = sr3asm::AsmManifest::parse(vpp::ByteView(asmBytes.data(), asmBytes.size()));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "1018.asm_pc parse failed: %s\n", ex.what());
        return 1;
    }
    std::printf("[asm] 1018.asm_pc: %zu container record(s)\n", mani.records().size());
    for (const auto& rec : mani.records()) {
        std::printf("  record '%s' kind=%u entryCount=%d (declared)\n", rec.name.c_str(), rec.containerKind,
                    rec.entryCount);
    }

    std::vector<std::string> candB = clmeshNamesInManifestOrder(mani, "1018");
    std::vector<std::string> candC = clmeshNamesInManifestOrder(mani, "1018h0");
    std::printf("\n[candidate B] '1018' record's own .clmesh_pc entries, manifest order (%zu):\n", candB.size());
    for (size_t i = 0; i < candB.size(); ++i) std::printf("  [%zu] %s\n", i, candB[i].c_str());
    std::printf("\n[candidate C] '1018h0' record's own .clmesh_pc entries, manifest order (%zu):\n", candC.size());
    for (size_t i = 0; i < candC.size(); ++i) std::printf("  [%zu] %s\n", i, candC[i].c_str());

    // Ground truth: which 22 real base names live in 1018h0 (lower-cased
    // stem, no extension) - independent of manifest entry order, straight
    // from the real container directory (the same 22 clmesh_probe already
    // found).
    std::set<std::string> h0Stems;
    {
        std::vector<uint8_t> h0Bytes;
        if (findEntryTopLevel(top, "1018h0.str2_pc", h0Bytes) && !h0Bytes.empty()) {
            vpp::Container h0(vpp::ByteView(h0Bytes.data(), h0Bytes.size()));
            for (const auto& e : h0.entries()) {
                if (endsWithNoCase(e.name, ".clmesh_pc")) {
                    std::string stem = e.name.substr(0, e.name.size() - 10);
                    for (auto& c : stem) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                    h0Stems.insert(stem);
                }
            }
        }
    }
    std::printf("\n[ground truth] %zu real .clmesh_pc stems physically inside 1018h0.str2_pc\n", h0Stems.size());

    // ---- 2. The real SR3Z placement records for the WHOLE tile, from the
    // FAST header (sr3_city~f1018.czh_pc - the one with real records per
    // spec-world-streaming.md Sec10.7: "all 248 files WITH records are ~f").
    std::vector<uint8_t> czhBytes;
    if (!findEntryDeep(top, "sr3_city~f1018.czh_pc", czhBytes) || czhBytes.empty()) {
        std::fprintf(stderr, "could not find 'sr3_city~f1018.czh_pc'\n");
        return 1;
    }
    sr3zone::ZoneHeader zh;
    try {
        zh = sr3zone::ZoneHeader::parse(vpp::ByteView(czhBytes.data(), czhBytes.size()));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "sr3_city~f1018.czh_pc parse failed: %s\n", ex.what());
        return 1;
    }
    std::printf("\n[czh] sr3_city~f1018.czh_pc: recordCount=%u headerOrigin=(%.3f,%.3f,%.3f) zoneType=%u\n",
                zh.recordCount(), zh.headerOrigin()[0], zh.headerOrigin()[1], zh.headerOrigin()[2],
                zh.fieldAt0x1E());

    if (zh.records().empty()) {
        std::fprintf(stderr, "zero SR3Z records - nothing to correlate\n");
        return 1;
    }

    // ---- 3. Tile-local quadrant split: min/max of x and z across every
    // real record position (origin-relative, per RecordPosition()'s own
    // contract) - the empirical tile extents, not a guessed formula.
    float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    for (const auto& r : zh.records()) {
        auto p = sr3zone::RecordPosition(r);
        if (p[0] < minX) minX = p[0];
        if (p[0] > maxX) maxX = p[0];
        if (p[2] < minZ) minZ = p[2];
        if (p[2] > maxZ) maxZ = p[2];
    }
    const float midX = (minX + maxX) * 0.5f;
    const float midZ = (minZ + maxZ) * 0.5f;
    std::printf("[tile extents] x[%.2f..%.2f] z[%.2f..%.2f] mid=(%.2f,%.2f)\n", minX, maxX, minZ, maxZ, midX, midZ);

    // h0 = N=0 = (low x, high z) quadrant, per spec-world-streaming.md
    // Sec10.1/Sec10.6(g): col offset 0 (low x), row 0 = higher-z row.
    auto inH0Quadrant = [&](const std::array<float, 3>& p) { return p[0] < midX && p[2] > midZ; };

    // ---- 4. Test each candidate ordering: for records whose resolved name
    // (via that ordering, indexed by RecordNameIndex()) is one of h0's own
    // 22 real stems, what fraction fall in the h0 quadrant? A real,
    // structural join should score far above the base rate (~25% for 4
    // roughly-equal quadrants); a coincidental/wrong join should not.
    auto stemOf = [](std::string name) {
        if (endsWithNoCase(name, ".clmesh_pc")) name = name.substr(0, name.size() - 10);
        // Strip a "~L1"/"~L2"/... tilde-suffix (spec-world-streaming.md
        // Sec10.5: the tile's own manifest registers the SAME base mesh
        // under "<base>~L1.clmesh_pc" for its own reduced-detail copy) so
        // candidate B's names compare equal to h0's own base-named stems.
        size_t tilde = name.find('~');
        if (tilde != std::string::npos) name = name.substr(0, tilde);
        for (auto& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return name;
    };

    // Raw nameIndex distribution, so the candidate list SIZE this project
    // is testing against is at least plausible (not testing a 28-entry
    // list against indices that obviously need a much bigger one).
    {
        uint16_t niMin = 0xFFFF, niMax = 0;
        for (const auto& r : zh.records()) {
            uint16_t ni = sr3zone::RecordNameIndex(r);
            if (ni < niMin) niMin = ni;
            if (ni > niMax) niMax = ni;
        }
        std::printf("\n[nameIndex range] min=%u max=%u across %zu real records\n", niMin, niMax, zh.records().size());
    }

    auto testCandidate = [&](const char* label, const std::vector<std::string>& cand) {
        int matchedH0 = 0, matchedH0InQuadrant = 0;
        int outOfRange = 0;
        std::map<std::string, int> matchedStemCounts;
        for (const auto& r : zh.records()) {
            uint16_t ni = sr3zone::RecordNameIndex(r);
            if (ni >= cand.size()) { ++outOfRange; continue; }
            std::string stem = stemOf(cand[ni]);
            if (h0Stems.count(stem) == 0) continue;
            ++matchedH0;
            ++matchedStemCounts[stem];
            auto p = sr3zone::RecordPosition(r);
            if (inH0Quadrant(p)) ++matchedH0InQuadrant;
        }
        double rate = matchedH0 > 0 ? 100.0 * matchedH0InQuadrant / matchedH0 : 0.0;
        std::printf("\n[test: %s] candidate list size=%zu, out-of-range nameIndex=%d/%zu\n", label, cand.size(),
                    outOfRange, zh.records().size());
        std::printf("  records whose resolved name is one of h0's 22 real stems: %d\n", matchedH0);
        std::printf("  of those, in h0's own (low x, high z) quadrant: %d (%.1f%%)\n", matchedH0InQuadrant, rate);
        std::printf("  distinct matched stems: %zu of h0's %zu\n", matchedStemCounts.size(), h0Stems.size());
        for (const auto& kv : matchedStemCounts) std::printf("    %-30s x%d\n", kv.first.c_str(), kv.second);
    };

    testCandidate("B: tile '1018' manifest order (.clmesh_pc entries only, filtered position)", candB);
    testCandidate("C: fine-cell '1018h0' manifest order", candC);

    // Candidate D: the tile "1018" record's FULL, UNFILTERED entry list (all
    // 102 declared entries, every resource type, in real manifest/directory
    // order) - tests whether nameIndex is a position in the record's WHOLE
    // entry list rather than a position among level-mesh entries only.
    {
        std::vector<std::string> candD;
        for (const auto& rec : mani.records()) {
            if (rec.name != "1018") continue;
            for (const auto& e : rec.entries) candD.push_back(e.name);
            break;
        }
        std::printf("\n[candidate D] '1018' record's FULL unfiltered entry list, manifest order (%zu entries):\n",
                    candD.size());
        for (size_t i = 0; i < candD.size(); ++i) std::printf("  [%zu] %s\n", i, candD[i].c_str());
        testCandidate("D: tile '1018' manifest order (full unfiltered entry list)", candD);
    }

    // ---- 5. Baseline control: what fraction of ALL records (regardless of
    // name) fall in the h0 quadrant? This is the rate a random/uncorrelated
    // join would be expected to hit for ITS matched subset, roughly.
    {
        int inQuad = 0;
        for (const auto& r : zh.records())
            if (inH0Quadrant(sr3zone::RecordPosition(r))) ++inQuad;
        std::printf("\n[control] of ALL %zu real records (regardless of name), %d (%.1f%%) fall in h0's quadrant\n",
                    zh.records().size(), inQuad, 100.0 * inQuad / static_cast<double>(zh.records().size()));
    }

    return 0;
}
