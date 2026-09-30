// Real-data validation for the three spec-world-streaming.md Sec10.7
// "header-level side results" added to sr3zone::ZoneHeader in this pass:
//
//  (a) SR3Z +0x1E zone type -> engine container-kind mapping (bullet 1).
//      Cross-references each `.czh_pc`'s +0x1E value against the
//      `container_kind` byte the SAME container carries in its own
//      `.asm_pc` manifest record (sr3asm::ContainerRecord::containerKind,
//      table 3), exactly the comparison the spec itself describes
//      ("kind taken from its manifest record").
//  (b) SR3Z +0x0C..+0x17 header origin is zero iff the file has zero
//      records (bullet 2), scoped to sr3_city_0/1 the way the spec itself
//      scoped it (1,265 headers), with a full-population figure reported
//      alongside for comparison.
//  (c) RecordPosition() + headerOrigin() lands in a plausible world-
//      coordinate range - a loose sanity bound, not the tile-centre
//      formula of Sec10.6(g) (that is a much bigger cross-reference this
//      harness does not attempt).
//
// Reuses the walk/locate pattern of validate_zone.cpp (this project's
// existing `.czh_pc` harness) and the manifest-parsing / archive-set
// conventions of validate_asm_population.cpp, rather than reinventing
// either. Read-only on game data (D:\...\Saints Row 3 CRREISH\...).
//
// Usage: validate_zoneheader_extension [archive.vpp_pc ...]
//   With no arguments: every *.vpp_pc in the game's packfiles\pc\cache.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3asm/manifest.h"
#include "sr3zone/zone_header.h"
#include "vpp/container.h"

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

const char* kGameRoot = "D:\\Project Crreish\\Saints Row 3 CRREISH";

// A loose sanity bound for a world-space coordinate (meters), per
// spec-world-streaming.md Sec10.6(g)'s tile lattice (columns/rows produce
// centres roughly within a few thousand meters of the origin) - NOT the
// exact tile-centre formula, just a plausibility envelope.
constexpr float kWorldBoundMeters = 5000.0f;

Bytes readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    Bytes b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool endsWithCI(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return lower(s.substr(s.size() - suffix.size())) == lower(suffix);
}

bool stemFor(const std::string& n, const std::string& ext, std::string& stem) {
    if (endsWithCI(n, ext)) {
        stem = n.substr(0, n.size() - ext.size());
        return true;
    }
    return false;
}

// The manifest lookup key for a container name: strip a trailing
// `.str2_pc` (the documented "normally its backing file" convention,
// sr3asm/manifest.h's own comment on ContainerRecord::name), or failing
// that, strip from the last '.' onward (mirrors validate_asm_population.
// cpp's dual `<name>.str2_pc` / `<name minus extension>.str2_pc` strategy,
// applied in reverse).
std::string manifestKeyFor(const std::string& containerEntryName) {
    if (endsWithCI(containerEntryName, ".str2_pc"))
        return containerEntryName.substr(0, containerEntryName.size() - 8);
    size_t dot = containerEntryName.find_last_of('.');
    if (dot != std::string::npos) return containerEntryName.substr(0, dot);
    return containerEntryName;
}

// Fallback key when the primary manifestKeyFor() lookup misses: strip a
// trailing `~f`/`~s`/`~...` fast/slow/variant marker (spec-world-
// streaming.md Sec10.7 bullet 2 documents `~f`/`~s` zone-file markers;
// Sec10.3's suffix-tolerant name parser documents `~` generally as a
// variant marker character).
std::string stripTildeMarker(const std::string& key) {
    size_t tilde = key.find_last_of('~');
    if (tilde != std::string::npos) return key.substr(0, tilde);
    return key;
}

bool entryBytes(const vpp::Container& c, size_t i, Bytes& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

// --------------------------------------------------------------- phase 1:
// collect every .asm_pc manifest anywhere in one archive into a single
// name -> container_kind map (spec 7.2/7.3; sr3asm::ContainerRecord).
void collectManifests(const vpp::Container& c, int depth, std::map<std::string, uint8_t>& kindByName,
                      long long& collisions, long long& manifestsParsed, long long& manifestsFailed) {
    if (depth > 10) return;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        if (e.payload.kind != vpp::PayloadKind::Raw) continue;
        vpp::ByteView rb;
        try {
            rb = c.rawEntryBytes(i);
        } catch (const std::exception&) {
            continue;
        }
        if (endsWithCI(e.name, ".asm_pc")) {
            try {
                sr3asm::AsmManifest m = sr3asm::AsmManifest::parse(rb);
                ++manifestsParsed;
                for (const auto& rec : m.records()) {
                    auto it = kindByName.find(rec.name);
                    if (it == kindByName.end()) kindByName.emplace(rec.name, rec.containerKind);
                    else if (it->second != rec.containerKind) ++collisions;
                }
            } catch (const std::exception&) {
                ++manifestsFailed;
            }
            continue; // an .asm_pc is never itself a nested vpp container
        }
        if (rb.size() < 4 || rb.readU32LE(0) != vpp::kMagic) continue;
        try {
            vpp::Container n = c.openNested(i);
            collectManifests(n, depth + 1, kindByName, collisions, manifestsParsed, manifestsFailed);
        } catch (const std::exception&) {
            // one bad nested container must not take out its siblings
        }
    }
}

// --------------------------------------------------------------- stats

struct Stats {
    // .czh_pc discovery / parse.
    long long czhFound = 0, czhParsedOk = 0, czhParseFailed = 0;

    // Gate (a): zone type -> container kind.
    long long gateAManifestMatched = 0, gateANoManifest = 0;
    long long gateAMatch = 0, gateAMismatch = 0;
    long long gateABaselineMatch = 0; // control: "always predict Zone"
    std::map<uint16_t, long long> zoneTypeCount;      // population per observed +0x1E value
    std::map<uint16_t, long long> zoneTypeMatchCount; // of those, how many matched their manifest kind
    int mismatchDumped = 0;

    // Gate (b): header origin zero iff zero records.
    long long gateBTotal = 0, gateBMatch = 0;                 // full population
    long long gateBScopedTotal = 0, gateBScopedMatch = 0;     // sr3_city_0/1 only
    std::vector<bool> scopedHasRecords;   // parallel arrays, sr3_city_0/1 only, for the shift control
    std::vector<bool> scopedOriginNonzero;

    // Gate (c): world-position sanity.
    long long gateCTotal = 0, gateCPass = 0;          // origin + RecordPosition()
    long long gateCControlPass = 0;                    // control: origin + 100x RecordPosition() (should mostly fail)
    float minW[3] = {0, 0, 0}, maxW[3] = {0, 0, 0};
    bool haveW = false;
};

void processCzh(const std::string& stem, const std::string& containerEntryName, const std::string& archiveName,
                const Bytes& bytes, const std::map<std::string, uint8_t>& kindByName, Stats& st) {
    (void)stem;
    ++st.czhFound;
    sr3zone::ZoneHeader h;
    try {
        h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(bytes.data(), bytes.size()));
    } catch (const std::exception&) {
        ++st.czhParseFailed;
        return;
    }
    ++st.czhParsedOk;

    // --- Gate (a) ---
    uint16_t zt = h.fieldAt0x1E();
    uint8_t predicted = static_cast<uint8_t>(sr3zone::ContainerKindForZoneType(zt));
    std::string key = manifestKeyFor(containerEntryName);
    auto it = kindByName.find(key);
    if (it == kindByName.end()) it = kindByName.find(stripTildeMarker(key));
    if (it != kindByName.end()) {
        ++st.gateAManifestMatched;
        uint8_t actual = it->second;
        bool matches = predicted == actual;
        if (matches) ++st.gateAMatch;
        else {
            ++st.gateAMismatch;
            if (st.mismatchDumped < 10) {
                ++st.mismatchDumped;
                std::printf("  GATE-A MISMATCH  container=%-24s zoneType=%u predictedKind=0x%02X actualKind=0x%02X\n",
                            key.c_str(), zt, predicted, actual);
            }
        }
        if (actual == static_cast<uint8_t>(sr3zone::ZoneContainerKind::Zone)) ++st.gateABaselineMatch;
        st.zoneTypeCount[zt] += 1;
        if (matches) st.zoneTypeMatchCount[zt] += 1;
    } else {
        ++st.gateANoManifest;
    }

    // --- Gate (b) ---
    const auto& o = h.headerOrigin();
    bool originZero = (o[0] == 0.0f && o[1] == 0.0f && o[2] == 0.0f);
    bool hasRecords = h.recordCount() > 0;
    bool consistent = (hasRecords == !originZero);
    ++st.gateBTotal;
    if (consistent) ++st.gateBMatch;
    if (archiveName == "sr3_city_0.vpp_pc" || archiveName == "sr3_city_1.vpp_pc") {
        ++st.gateBScopedTotal;
        if (consistent) ++st.gateBScopedMatch;
        st.scopedHasRecords.push_back(hasRecords);
        st.scopedOriginNonzero.push_back(!originZero);
    }

    // --- Gate (c) ---
    for (const auto& rec : h.records()) {
        auto pos = sr3zone::RecordPosition(rec);
        float w[3] = {o[0] + pos[0], o[1] + pos[1], o[2] + pos[2]};
        ++st.gateCTotal;
        bool inBound = std::fabs(w[0]) <= kWorldBoundMeters && std::fabs(w[1]) <= kWorldBoundMeters &&
                       std::fabs(w[2]) <= kWorldBoundMeters;
        if (inBound) ++st.gateCPass;

        // Control that could fail: the SAME computation with the record's
        // contribution deliberately scaled 100x. If the bound were
        // vacuous (satisfied by any number), this would pass just as
        // often as the real computation; it should instead collapse for
        // any record whose raw position is more than ~1/20th of the
        // bound width from crossing it.
        float wc[3] = {o[0] + pos[0] * 100.0f, o[1] + pos[1] * 100.0f, o[2] + pos[2] * 100.0f};
        bool controlInBound = std::fabs(wc[0]) <= kWorldBoundMeters && std::fabs(wc[1]) <= kWorldBoundMeters &&
                              std::fabs(wc[2]) <= kWorldBoundMeters;
        if (controlInBound) ++st.gateCControlPass;

        if (!st.haveW) {
            st.haveW = true;
            for (int k = 0; k < 3; ++k) st.minW[k] = st.maxW[k] = w[k];
        } else {
            for (int k = 0; k < 3; ++k) {
                st.minW[k] = std::min(st.minW[k], w[k]);
                st.maxW[k] = std::max(st.maxW[k], w[k]);
            }
        }
    }
}

void walkCzh(const vpp::Container& c, const std::string& containerEntryName, const std::string& archiveName,
            int depth, const std::map<std::string, uint8_t>& kindByName, Stats& st) {
    if (depth > 10) return;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        std::string stem;
        if (stemFor(e.name, ".czh_pc", stem)) {
            try {
                Bytes b;
                if (entryBytes(c, i, b) && !b.empty()) processCzh(stem, containerEntryName, archiveName, b, kindByName, st);
            } catch (const std::exception&) {
                // one bad entry must not take out its siblings
            }
            continue;
        }
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::ByteView rb = c.rawEntryBytes(i);
                if (rb.size() < 4 || rb.readU32LE(0) != vpp::kMagic) continue;
                vpp::Container n = c.openNested(i);
                walkCzh(n, e.name, archiveName, depth + 1, kindByName, st);
            } catch (const std::exception&) {
                continue;
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    } else {
        try {
            for (const auto& de : fs::directory_iterator(std::string(kGameRoot) + "\\packfiles\\pc\\cache"))
                if (de.is_regular_file() && lower(de.path().extension().string()) == ".vpp_pc")
                    archives.push_back(de.path().string());
        } catch (const std::exception& ex) {
            std::printf("FAILED to enumerate %s\\packfiles\\pc\\cache: %s\n", kGameRoot, ex.what());
            return 1;
        }
        std::sort(archives.begin(), archives.end());
    }

    Stats st;
    long long manifestCollisions = 0, manifestsParsed = 0, manifestsFailed = 0;
    long long archivesOpened = 0, archivesFailed = 0;

    for (const std::string& path : archives) {
        Bytes b = readFile(path);
        if (b.empty()) {
            std::printf("skip (empty/unreadable): %s\n", path.c_str());
            continue;
        }
        std::string archiveName = fs::path(path).filename().string();
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            ++archivesOpened;

            std::map<std::string, uint8_t> kindByName;
            collectManifests(c, 0, kindByName, manifestCollisions, manifestsParsed, manifestsFailed);
            walkCzh(c, "", archiveName, 0, kindByName, st);

            std::printf("scanned %s (manifests found in archive so far: %lld)\n", path.c_str(),
                        manifestsParsed);
            std::fflush(stdout);
        } catch (const std::exception& ex) {
            ++archivesFailed;
            std::printf("FAILED to open %s: %s\n", path.c_str(), ex.what());
        }
    }

    std::printf("\n=== sr3zone: ZoneHeader Sec10.7 extension validation ===\n");
    std::printf("archives opened / failed         : %lld / %lld\n", archivesOpened, archivesFailed);
    std::printf(".czh_pc found                    : %lld\n", st.czhFound);
    std::printf("  parsed OK                       : %lld\n", st.czhParsedOk);
    std::printf("  failed to parse                 : %lld\n", st.czhParseFailed);
    std::printf(".asm_pc manifests parsed / failed : %lld / %lld\n", manifestsParsed, manifestsFailed);
    std::printf("manifest name -> kind collisions (same name, different kind, within one archive) : %lld\n",
                manifestCollisions);

    // ---------------------------------------------------------- gate (a)
    std::printf("\n--- GATE (a): SR3Z +0x1E zone type -> container kind ---\n");
    std::printf("(spec-world-streaming.md Sec10.7 bullet 1: 0 exceptions over 2,971/2,971)\n");
    std::printf(".czh_pc containers matched to a manifest record : %lld / %lld   (unmatched, excluded below: %lld)\n",
                st.gateAManifestMatched, st.czhParsedOk, st.gateANoManifest);
    std::printf("  predicted-kind == manifest-kind : %lld / %lld   exceptions: %lld\n", st.gateAMatch,
                st.gateAManifestMatched, st.gateAMismatch);
    std::printf("  CONTROL (trivial \"always predict Zone\" baseline) : %lld / %lld matched   "
                "(gate should clear this baseline by a wide margin, not merely tie it)\n",
                st.gateABaselineMatch, st.gateAManifestMatched);
    std::printf("  GATE: %s\n", (st.gateAManifestMatched > 0 && st.gateAMismatch == 0) ? "PASS" : "FAIL");
    std::printf("  population per observed zone-type value (matched-to-manifest subset only):\n");
    for (const auto& kv : st.zoneTypeCount) {
        long long matched = 0;
        auto mit = st.zoneTypeMatchCount.find(kv.first);
        if (mit != st.zoneTypeMatchCount.end()) matched = mit->second;
        sr3zone::ZoneContainerKind k = sr3zone::ContainerKindForZoneType(kv.first);
        std::printf("    zoneType=%-3u -> predicted 0x%02X : count %lld, matched-manifest %lld\n", kv.first,
                    static_cast<unsigned>(k), kv.second, matched);
    }

    // ---------------------------------------------------------- gate (b)
    std::printf("\n--- GATE (b): SR3Z +0x0C..+0x17 header origin, zero iff zero records ---\n");
    std::printf("(spec-world-streaming.md Sec10.7 bullet 2: measured over 1,265 sr3_city_0/1 headers)\n");
    std::printf("full population   : %lld / %lld consistent\n", st.gateBMatch, st.gateBTotal);
    std::printf("sr3_city_0/1 only : %lld / %lld consistent   (spec denominator: 1,265)\n", st.gateBScopedMatch,
                st.gateBScopedTotal);
    long long shiftMatch = 0;
    const size_t n = st.scopedHasRecords.size();
    for (size_t i = 0; i < n; ++i) {
        bool hr = st.scopedHasRecords[i];
        bool on = st.scopedOriginNonzero[(i + 1) % n];
        if (n > 1 && hr == on) ++shiftMatch;
    }
    std::printf("  CONTROL (shift-by-one: file i's recordCount>0 vs file i+1's origin-nonzero) : %lld / %zu   "
                "(should NOT reproduce the near-exact match above)\n",
                shiftMatch, n);
    std::printf("  GATE (sr3_city_0/1 scope): %s\n",
                (st.gateBScopedTotal > 0 && st.gateBScopedMatch == st.gateBScopedTotal) ? "PASS" : "FAIL");

    // ---------------------------------------------------------- gate (c)
    std::printf("\n--- GATE (c): RecordPosition() + headerOrigin() world-coordinate sanity ---\n");
    std::printf("(loose bound +-%.0f m, NOT the exact tile-centre formula of Sec10.6(g))\n", kWorldBoundMeters);
    std::printf("records checked                  : %lld\n", st.gateCTotal);
    std::printf("  within bound                    : %lld / %lld (%.4f%%)\n", st.gateCPass, st.gateCTotal,
                st.gateCTotal > 0 ? 100.0 * static_cast<double>(st.gateCPass) / static_cast<double>(st.gateCTotal)
                                  : 0.0);
    std::printf("  CONTROL (record contribution scaled 100x) within bound : %lld / %lld (%.4f%%)   "
                "(should be markedly lower than the real figure above)\n",
                st.gateCControlPass, st.gateCTotal,
                st.gateCTotal > 0
                    ? 100.0 * static_cast<double>(st.gateCControlPass) / static_cast<double>(st.gateCTotal)
                    : 0.0);
    if (st.haveW) {
        std::printf("  observed range: x [%.1f, %.1f]  y [%.1f, %.1f]  z [%.1f, %.1f]\n", st.minW[0], st.maxW[0],
                    st.minW[1], st.maxW[1], st.minW[2], st.maxW[2]);
    }
    const bool gateCPass99 = st.gateCTotal > 0 && (100.0 * static_cast<double>(st.gateCPass) /
                                                   static_cast<double>(st.gateCTotal)) >= 99.0;
    std::printf("  GATE (>=99%% within loose bound, informative only): %s\n", gateCPass99 ? "PASS" : "FAIL");

    std::printf("\n=== summary ===\n");
    const bool gateA = st.gateAManifestMatched > 0 && st.gateAMismatch == 0;
    const bool gateB = st.gateBScopedTotal > 0 && st.gateBScopedMatch == st.gateBScopedTotal;
    std::printf("gate (a) zone-type->kind mapping : %s\n", gateA ? "PASS" : "FAIL");
    std::printf("gate (b) origin zero-iff-records : %s\n", gateB ? "PASS" : "FAIL");
    std::printf("gate (c) position sanity (informative) : %s\n", gateCPass99 ? "PASS" : "FAIL");
    return (gateA && gateB && archivesFailed == 0) ? 0 : 1;
}
