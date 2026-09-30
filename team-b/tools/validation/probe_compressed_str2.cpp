// Focused probe for gap 1 (the 46 .czn_pc entries validate_zone.cpp's walk()
// never counts): both validate_zone.cpp's and diag_zone_gaps.cpp's walk()
// only recurse into a nested container when
// entries()[i].payload.kind == PayloadKind::Raw - a COMPRESSED `.str2_pc`
// entry at any level is never decompressed-then-opened as a nested
// container by either harness, so any `.czn_pc`/`.czh_pc` files living
// inside a str2_pc that itself ships COMPRESSED (rather than raw) would be
// silently invisible to both, with zero exceptions thrown and zero failed-
// decode statuses recorded - a undercount in the POPULATION the walk visits
// at all, not a decode failure within it. This probe tests that directly:
// for every entry anywhere in the recursive structure named "*.str2_pc"
// with payload.kind == Compressed, decompress it and try to construct a
// Container from the result, then count .czh_pc/.czn_pc/.gzn_pc entries
// found inside.
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

bool endsWith(const std::string& n, const std::string& ext) {
    return n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0;
}

bool stemFor(const std::string& n, const std::string& ext, std::string& stem) {
    if (endsWith(n, ext)) {
        stem = n.substr(0, n.size() - ext.size());
        return true;
    }
    return false;
}

// Second hypothesis for gap 1: validate_zone.cpp's/diag_zone_gaps.cpp's walk()
// both key `.czn_pc` entries into a `std::map<std::string, size_t>` by STEM
// within one container level. If two directory entries in the SAME
// container level produce the IDENTICAL stem (i.e. a literal duplicate
// name, or two different names that strip to the same stem), the map
// silently keeps only the LAST one written - the other is never even
// visited, let alone decoded, and produces neither a success nor a failure
// anywhere. That would look exactly like this population: 0 recorded
// extraction failures, yet fewer entries processed than exist on disk.
long long g_totalCznEntries = 0;   // raw count of name-matches, no dedup
long long g_distinctCznStems = 0;  // count after the SAME map-keying validate_zone.cpp/diag_zone_gaps.cpp use
long long g_totalCzhEntries = 0;   // raw .czh_pc name-matches, for the "was it ever 2971 czn to begin with?" check
std::vector<std::string> g_duplicateExamples;
std::vector<std::string> g_allCznStems; // path + stem, for direct diffing against another walker's attempted list

void checkCznDuplicates(const vpp::Container& c, const std::string& path) {
    std::map<std::string, int> stemCount;
    long long total = 0;
    long long czh = 0;
    for (const auto& e : c.entries()) {
        std::string stem;
        if (stemFor(e.name, ".czn_pc", stem)) {
            ++total;
            ++stemCount[stem];
            g_allCznStems.push_back(path + " :: " + stem);
        } else if (endsWith(e.name, ".czh_pc")) {
            ++czh;
        }
    }
    long long distinct = static_cast<long long>(stemCount.size());
    g_totalCznEntries += total;
    g_distinctCznStems += distinct;
    g_totalCzhEntries += czh;
    if (total != distinct) {
        for (const auto& kv : stemCount) {
            if (kv.second > 1) {
                g_duplicateExamples.push_back(path + " : stem '" + kv.first + "' appears " +
                                               std::to_string(kv.second) + " times as a .czn_pc entry");
            }
        }
    }
}

long long g_compressedStr2Found = 0;
long long g_compressedStr2OpenedOk = 0;
long long g_czhInsideCompressedStr2 = 0;
long long g_cznInsideCompressedStr2 = 0;
long long g_gznInsideCompressedStr2 = 0;

const char* statusName(vpp::DecodeStatus s) {
    switch (s) {
        case vpp::DecodeStatus::Ok: return "Ok";
        case vpp::DecodeStatus::OkUnconfirmedContent: return "OkUnconfirmedContent";
        case vpp::DecodeStatus::ContentValidated: return "ContentValidated";
        case vpp::DecodeStatus::ContentValidationFailed: return "ContentValidationFailed";
        case vpp::DecodeStatus::RecoveredSharedStream: return "RecoveredSharedStream";
        case vpp::DecodeStatus::RecoveredSharedStreamLongChain: return "RecoveredSharedStreamLongChain";
        case vpp::DecodeStatus::NoValidZlibHeaderAtOffset: return "NoValidZlibHeaderAtOffset";
        case vpp::DecodeStatus::ZlibStreamError: return "ZlibStreamError";
        case vpp::DecodeStatus::SizeMismatch: return "SizeMismatch";
    }
    return "?";
}

void walkRaw(const vpp::Container& c, const std::string& path, int depth);

void walk(const vpp::Container& c, const std::string& path, int depth) {
    checkCznDuplicates(c, path);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try {
                walk(c.openNested(i), path + " > " + e.name, depth + 1);
            } catch (const std::exception&) {
            }
        } else if (endsWith(e.name, ".str2_pc")) {
            ++g_compressedStr2Found;
            auto r = c.decompressEntry(i);
            printf("  [depth %d] COMPRESSED str2_pc entry '%s' in %s : decompressEntry status=%s, "
                   "data size=%zu (declared uncompressedSize=%u)\n",
                   depth, e.name.c_str(), path.c_str(), statusName(r.status), r.data.size(),
                   e.payload.decompressedLength);
            if (!r.data.empty()) {
                try {
                    vpp::Container nested(vpp::ByteView(r.data.data(), r.data.size()));
                    ++g_compressedStr2OpenedOk;
                    long long czh = 0, czn = 0, gzn = 0;
                    for (const auto& ne : nested.entries()) {
                        if (endsWith(ne.name, ".czh_pc")) ++czh;
                        else if (endsWith(ne.name, ".czn_pc")) ++czn;
                        else if (endsWith(ne.name, ".gzn_pc")) ++gzn;
                    }
                    g_czhInsideCompressedStr2 += czh;
                    g_cznInsideCompressedStr2 += czn;
                    g_gznInsideCompressedStr2 += gzn;
                    printf("    -> opened as container OK: %zu entries, .czh_pc=%lld .czn_pc=%lld .gzn_pc=%lld\n",
                           nested.entries().size(), czh, czn, gzn);
                    // Recurse into it too, in case of deeper nesting.
                    walk(nested, path + " > " + e.name + " (decompressed)", depth + 1);
                } catch (const std::exception& ex) {
                    printf("    -> decompressed bytes do NOT parse as a Container: %s\n", ex.what());
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("skip (empty/unreadable): %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c, argv[i], 0);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== Compressed .str2_pc entries (never opened as nested containers by "
           "validate_zone.cpp/diag_zone_gaps.cpp's walk(), which only recurses into "
           "PayloadKind::Raw entries) ===\n");
    printf("compressed .str2_pc entries found : %lld\n", g_compressedStr2Found);
    printf("  successfully decompressed AND opened as a Container : %lld\n", g_compressedStr2OpenedOk);
    printf("  .czh_pc entries found inside them  : %lld\n", g_czhInsideCompressedStr2);
    printf("  .czn_pc entries found inside them  : %lld\n", g_cznInsideCompressedStr2);
    printf("  .gzn_pc entries found inside them  : %lld\n", g_gznInsideCompressedStr2);

    printf("\n=== Duplicate-stem check (would explain 0-recorded-failures + fewer-than-expected "
           "processed, since std::map<string,size_t> silently keeps only the LAST entry per stem) ===\n");
    printf("total .czn_pc name-matches (raw, no dedup) : %lld\n", g_totalCznEntries);
    printf("distinct stems after map-keying             : %lld\n", g_distinctCznStems);
    printf("difference (entries the map-based walk() NEVER visits) : %lld\n",
           g_totalCznEntries - g_distinctCznStems);
    for (const auto& ex : g_duplicateExamples) printf("  %s\n", ex.c_str());

    printf("\n=== ALL_CZN_STEMS (%zu) ===\n", g_allCznStems.size());
    for (const auto& s : g_allCznStems) printf("STEM: %s\n", s.c_str());
    return 0;
}
