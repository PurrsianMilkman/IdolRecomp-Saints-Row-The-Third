// Population gate for sr3vehicle::CvtfCatalog (.cvtf_pc,
// spec-geometry-format.md §5.1) over every archive given on argv. Added
// 2026-10-03 as part of a reader-correctness cross-check against Team A's
// §5.1 "the real header is 48 bytes, not 16" HIGH CONFIDENCE finding: a
// direct real-byte check (hex dump of 5 byte-identical real copies of
// `car_4dr_standard03.cvtf_pc`) confirmed the finding and the reader was
// fixed accordingly (src/cvtf.cpp's own top comment has the full story).
// This tool exists to confirm the fix holds across the WHOLE real
// population, not just the one worked sample the fix was derived from -
// this project's standing "verify against real data, don't stop at one
// sample" discipline.
//
// Usage: validate_cvtf_population <archive.vpp_pc> [...]
//
// Reuses the recursive archive-walking pattern of tools/vpp_extract.cpp
// (vpp::Container, raw AND compressed entries, nested str2_pc containers)
// but does not write anything to disk - every entry literally named
// "*.cvtf_pc" is parsed in memory via CvtfCatalog::tryParse and the
// result tallied.
//
// Gates
//   G1  survey: every "*.cvtf_pc" entry found, per archive, plus a
//       byte-identical-content dedup count (this project's established
//       "distinct real files" convention, spec §5.1's own "120 distinct
//       real .cvtf_pc files" figure for the ID-2 population).
//   G2  parse success: CvtfCatalog::tryParse() succeeds (magic, section
//       count, the resolved Region-1 offset/count fit inside the file).
//       CONTROL: a deliberately-corrupted copy (first header byte
//       flipped) of the first real file found must fail to parse - an
//       oracle that can't fail proves nothing (this project's rule).
//   G3  region1Count() distribution - the real per-file Region-1 size,
//       confirming the new (offset-based) Region-1 location still lands
//       on a sane, in-bounds quad table for every real file, not just the
//       one sample the fix's exact field semantics were read from.
//   G4  wheelNames()/familyNames() size distribution (array1 +0x20/+0x24,
//       array2 +0x28/+0x2C) - settles whether the "45 wheel names / 7
//       family names" figures measured on car_4dr_standard03.cvtf_pc
//       generalize across the real population, or whether that was itself
//       only true for this one vehicle.

#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3vehicle/cvtf.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
using sr3vehicle::CvtfCatalog;

bool hasSuffixCI(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i], b = suffix[i];
        if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b))) return false;
    }
    return true;
}

struct Stats {
    size_t filesFound = 0;
    size_t parseOk = 0;
    size_t parseFail = 0;
    std::map<uint32_t, size_t> region1CountHist;
    std::map<uint32_t, size_t> wheelArrayCountHist;
    std::map<uint32_t, size_t> familyArrayCountHist;
    size_t wheelArrayAbsent = 0;
    size_t familyArrayAbsent = 0;
    std::set<std::string> distinctContent;  // keyed by raw byte content - these files are small (~tens of KB)
    std::vector<std::string> failures;      // first few failure reasons, capped
};

void recordFile(const std::string& path, const Bytes& data, Stats& stats, Bytes* firstRealFileOut) {
    stats.filesFound++;
    stats.distinctContent.emplace(reinterpret_cast<const char*>(data.data()), data.size());
    if (firstRealFileOut && firstRealFileOut->empty()) *firstRealFileOut = data;

    CvtfCatalog cat;
    std::string why;
    vpp::ByteView view(data.data(), data.size());
    if (!CvtfCatalog::tryParse(view, cat, why)) {
        stats.parseFail++;
        if (stats.failures.size() < 20) stats.failures.push_back(path + ": " + why);
        return;
    }
    stats.parseOk++;
    stats.region1CountHist[cat.region1Count()]++;
    if (cat.wheelNames().empty()) stats.wheelArrayAbsent++;
    else stats.wheelArrayCountHist[static_cast<uint32_t>(cat.wheelNames().size())]++;
    if (cat.familyNames().empty()) stats.familyArrayAbsent++;
    else stats.familyArrayCountHist[static_cast<uint32_t>(cat.familyNames().size())]++;
}

void walk(const vpp::Container& c, const std::string& pathPrefix, Stats& stats, Bytes* firstRealFileOut) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        std::string path = pathPrefix + "/" + e.name;
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                bool openedAsContainer = false;
                try {
                    vpp::Container nested = c.openNested(i);
                    walk(nested, path, stats, firstRealFileOut);
                    openedAsContainer = true;
                } catch (const vpp::FormatError&) {
                    // Not a nested container - genuine leaf raw content.
                }
                if (!openedAsContainer && hasSuffixCI(e.name, ".cvtf_pc")) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    Bytes data(raw.data(), raw.data() + raw.size());
                    recordFile(path, data, stats, firstRealFileOut);
                }
                continue;
            }
            if (hasSuffixCI(e.name, ".cvtf_pc")) {
                vpp::DecompressResult r = c.decompressEntry(i);
                switch (r.status) {
                    case vpp::DecodeStatus::Ok:
                    case vpp::DecodeStatus::OkUnconfirmedContent:
                    case vpp::DecodeStatus::ContentValidated:
                    case vpp::DecodeStatus::RecoveredSharedStream:
                    case vpp::DecodeStatus::RecoveredSharedStreamLongChain:
                        recordFile(path, r.data, stats, firstRealFileOut);
                        break;
                    default:
                        break;  // not extractable - not this tool's concern
                }
            }
        } catch (const std::exception&) {
            // One bad entry should not abort the whole walk.
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: validate_cvtf_population <archive.vpp_pc> [...]\n");
        return 1;
    }
    Stats stats;
    Bytes firstRealFile;
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary | std::ios::ate);
        if (!f) {
            std::fprintf(stderr, "could not open %s\n", argv[i]);
            continue;
        }
        std::streamsize size = f.tellg();
        f.seekg(0);
        Bytes bytes(static_cast<size_t>(size));
        if (size > 0 && !f.read(reinterpret_cast<char*>(bytes.data()), size)) {
            std::fprintf(stderr, "failed reading %s\n", argv[i]);
            continue;
        }
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(root, argv[i], stats, &firstRealFile);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "%s: %s\n", argv[i], ex.what());
        }
    }

    std::printf("G1 survey: %zu .cvtf_pc entries found, %zu distinct by exact byte content\n", stats.filesFound,
                stats.distinctContent.size());
    std::printf("G2 parse: %zu ok, %zu FAILED\n", stats.parseOk, stats.parseFail);
    for (auto& f : stats.failures) std::printf("  FAIL: %s\n", f.c_str());

    int exitCode = (stats.parseFail > 0) ? 3 : 0;

    if (!firstRealFile.empty()) {
        Bytes corrupted = firstRealFile;
        corrupted[0] ^= 0xFF;  // flip a magic byte
        CvtfCatalog cat;
        std::string why;
        bool ok = CvtfCatalog::tryParse(vpp::ByteView(corrupted.data(), corrupted.size()), cat, why);
        std::printf("G2 CONTROL (flipped first magic byte of a real file): tryParse() %s (%s)\n",
                    ok ? "WRONGLY SUCCEEDED" : "correctly failed", why.c_str());
        if (ok) exitCode = 2;  // the oracle can't fail - something is wrong with tryParse's own gating
    } else {
        std::printf("G2 CONTROL: SKIPPED - no real .cvtf_pc file found on any given archive\n");
    }

    std::printf("G3 region1Count() distribution:\n");
    for (auto& kv : stats.region1CountHist) std::printf("  count=%u: %zu files\n", kv.first, kv.second);

    std::printf("G4 wheelNames() size distribution (array1, +0x20/+0x24): %zu files with an absent/empty array\n",
                stats.wheelArrayAbsent);
    for (auto& kv : stats.wheelArrayCountHist) std::printf("  size=%u: %zu files\n", kv.first, kv.second);
    std::printf("G4 familyNames() size distribution (array2, +0x28/+0x2C): %zu files with an absent/empty array\n",
                stats.familyArrayAbsent);
    for (auto& kv : stats.familyArrayCountHist) std::printf("  size=%u: %zu files\n", kv.first, kv.second);

    return exitCode;
}
