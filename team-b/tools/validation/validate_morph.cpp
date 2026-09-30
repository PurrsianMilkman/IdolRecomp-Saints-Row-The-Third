// Ad hoc real-data validation (not a deliverable): runs the real
// sr3morph::MorphFile parser over every .cmorph_pc in the given archives
// and reproduces spec-morph-format.md's own population statistics -
// including its Sec5 vertex-index control test - so the reader is checked
// against the spec's numbers, not just "it didn't throw".
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "sr3morph/morph_file.h"
#include "vpp/container.h"

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

long long g_entries = 0, g_gmorphSeen = 0;
long long g_ok = 0, g_fail = 0;
long long g_records = 0, g_ascending = 0, g_uniqueIdx = 0, g_maxMatches = 0;
long long g_elements = 0, g_f10Under4096 = 0;
long long g_idxAtZeroAscending = 0;
long long g_trivialRecords = 0, g_nonTrivialRecords = 0;
long long g_emptyRecords = 0, g_singleRecords = 0, g_singleAscendingAtZero = 0;
long long g_idxAtZeroAscendingNonTrivial = 0, g_ascendingNonTrivial = 0;
std::map<uint32_t, long long> g_modes;
std::map<uint32_t, long long> g_targetCounts;
std::set<std::pair<std::string, size_t>> g_unique;
size_t g_minSize = SIZE_MAX, g_maxSize = 0;
std::map<size_t, long long> g_sizeHistogram;

void checkOne(const std::string& name, vpp::ByteView bytes) {
    ++g_entries;
    if (!g_unique.insert({name, bytes.size()}).second) return; // dedup by (name, size)
    if (bytes.size() < g_minSize) g_minSize = bytes.size();
    if (bytes.size() > g_maxSize) g_maxSize = bytes.size();
    ++g_sizeHistogram[bytes.size()];

    try {
        sr3morph::ByteView content(bytes.data(), bytes.size());
        sr3morph::MorphFile m = sr3morph::MorphFile::parse(content);
        ++g_ok;
        ++g_modes[m.mode()];
        ++g_targetCounts[static_cast<uint32_t>(m.targets().size())];

        for (size_t d = 0; d < m.descriptors().size(); ++d) {
            ++g_records;
            const auto& desc = m.descriptors()[d];
            bool ascending = true, ascendingAtZero = true;
            std::set<uint16_t> seen;
            uint16_t maxIdx = 0;
            int32_t prev = -1, prevZero = -1;
            for (size_t e = 0; e < desc.affectedVertexCount; ++e) {
                sr3morph::Element el = m.elementAt(d, e, content);
                ++g_elements;
                if (el.field_10 < 4096) ++g_f10Under4096;
                if (static_cast<int32_t>(el.vertexIndex) <= prev) ascending = false;
                prev = el.vertexIndex;
                if (static_cast<int32_t>(el.component0) <= prevZero) ascendingAtZero = false;
                prevZero = el.component0;
                seen.insert(el.vertexIndex);
                if (el.vertexIndex > maxIdx) maxIdx = el.vertexIndex;
            }
            // A record with N<=1 passes "strictly ascending" vacuously, so
            // including such records inflates the CONTROL (whose whole
            // point is to show a low rate) while leaving the positive
            // result at 100% either way. Track both denominators.
            if (desc.affectedVertexCount == 0) ++g_emptyRecords;
            if (desc.affectedVertexCount == 1) {
                ++g_singleRecords;
                if (ascendingAtZero) ++g_singleAscendingAtZero;
            }
            if (desc.affectedVertexCount <= 1) ++g_trivialRecords;
            else {
                ++g_nonTrivialRecords;
                if (ascendingAtZero) ++g_idxAtZeroAscendingNonTrivial;
                if (ascending) ++g_ascendingNonTrivial;
            }
            if (ascending) ++g_ascending;
            if (ascendingAtZero) ++g_idxAtZeroAscending;
            if (seen.size() == desc.affectedVertexCount) ++g_uniqueIdx;
            if (desc.affectedVertexCount > 0 && maxIdx == desc.maxVertexIndex) ++g_maxMatches;
            else if (desc.affectedVertexCount == 0) ++g_maxMatches; // vacuous
        }
    } catch (const std::exception& ex) {
        ++g_fail;
        if (g_fail <= 12) printf("FAIL %s (%zu bytes): %s\n", name.c_str(), bytes.size(), ex.what());
    }
}

void walk(const vpp::Container& c, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        if (endsWith(e.name, ".gmorph_pc")) ++g_gmorphSeen;
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                try {
                    vpp::Container nested = c.openNested(i);
                    walk(nested, path + "/" + e.name);
                    continue;
                } catch (const vpp::FormatError&) {
                }
                if (endsWith(e.name, ".cmorph_pc")) checkOne(e.name, c.rawEntryBytes(i));
            } else {
                auto r = c.decompressEntry(i);
                bool usable = r.status == vpp::DecodeStatus::Ok ||
                              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
                if (usable && endsWith(e.name, ".cmorph_pc")) {
                    checkOne(e.name, vpp::ByteView(r.data.data(), r.data.size()));
                }
            }
        } catch (const std::exception&) {
            continue;
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> bytes = readFile(argv[i]);
        if (bytes.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c, argv[i]);
            printf("scanned %s\n", argv[i]);
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== .cmorph_pc validation ===\n");
    printf("entries encountered      : %lld  (spec Sec2: 2,946)\n", g_entries);
    printf("unique by (name, size)   : %zu  (spec Sec2: 1,541)\n", g_unique.size());
    printf("parsed OK                : %lld\n", g_ok);
    printf("failed                   : %lld\n", g_fail);
    printf(".gmorph_pc entries seen  : %lld  (spec Sec6: 0)\n", g_gmorphSeen);
    printf("size range               : %zu .. %zu  (spec Sec2: 324 .. 805,236)\n", g_minSize, g_maxSize);

    printf("\n-- spec Sec4: exact-size replay --\n");
    printf("every parsed file's walk landed exactly on EOF with the 0x0BADBEEF sentinel:\n");
    printf("  %lld / %lld  (parse() enforces both, so OK count == replay count)\n", g_ok, g_ok);

    printf("\n-- spec Sec5: the vertex index is at element +6 --\n");
    printf("records                              : %lld\n", g_records);
    printf("index at +6 strictly ascending       : %lld / %lld\n", g_ascending, g_records);
    printf("index at +6 unique within record     : %lld / %lld\n", g_uniqueIdx, g_records);
    printf("max(index) == descriptor +0x06       : %lld / %lld\n", g_maxMatches, g_records);
    printf("CONTROL, u16 at +0 ascending         : %lld / %lld  (all records)\n",
           g_idxAtZeroAscending, g_records);
    printf("\n  -- same figures excluding N<=1 records, which pass ANY 'ascending'\n");
    printf("     test vacuously and so inflate a control whose point is a LOW rate --\n");
    printf("  trivial (N<=1) records            : %lld\n", g_trivialRecords);
    printf("  non-trivial (N>=2) records        : %lld  (spec Sec5 denominator: 25,789)\n",
           g_nonTrivialRecords);
    printf("  index at +6 ascending, N>=2       : %lld / %lld\n", g_ascendingNonTrivial,
           g_nonTrivialRecords);
    printf("  CONTROL at +0 ascending, N>=2     : %lld / %lld = %.2f%%  (spec: 324 / 25,789 = 1.3%%)\n",
           g_idxAtZeroAscendingNonTrivial, g_nonTrivialRecords,
           g_nonTrivialRecords ? 100.0 * g_idxAtZeroAscendingNonTrivial / g_nonTrivialRecords : 0.0);

    printf("\n  -- record N distribution, measured directly --\n");
    printf("  N == 0 (empty, no bulk)           : %lld\n", g_emptyRecords);
    printf("  N == 1 (single element)           : %lld, of which +0-ascending: %lld\n",
           g_singleRecords, g_singleAscendingAtZero);
    printf("  N >= 2 (control is meaningful)    : %lld\n", g_nonTrivialRecords);
    printf("  N >= 1 total                      : %lld  (spec Sec5 denominator is 25,789)\n",
           g_singleRecords + g_nonTrivialRecords);
    printf("  N>=1 +0-ascending (spec's basis)  : %lld  (spec: 324)\n",
           g_singleAscendingAtZero + g_idxAtZeroAscendingNonTrivial);

    printf("\n-- spec Sec3.5: element +10 --\n");
    printf("elements                 : %lld\n", g_elements);
    printf("+10 < 4096               : %lld / %lld\n", g_f10Under4096, g_elements);

    printf("\n-- modes (spec Sec6: every shipped file is mode 1) --\n");
    for (const auto& kv : g_modes) printf("  mode %u: %lld\n", kv.first, kv.second);

    printf("\n-- most common size (spec Sec2: 17,220 bytes x 218 files) --\n");
    size_t bestSize = 0; long long bestCount = 0;
    for (const auto& kv : g_sizeHistogram) {
        if (kv.second > bestCount) { bestCount = kv.second; bestSize = kv.first; }
    }
    printf("  %zu bytes x %lld files\n", bestSize, bestCount);

    printf("\n-- target counts (spec Sec9: NPC heads 1, player face 95, bodies 118-119) --\n");
    int shown = 0;
    for (auto it = g_targetCounts.rbegin(); it != g_targetCounts.rend() && shown < 6; ++it, ++shown) {
        printf("  %u targets: %lld files\n", it->first, it->second);
    }
    printf("  (1 target: %lld files)\n", g_targetCounts.count(1) ? g_targetCounts[1] : 0);

    return g_fail == 0 ? 0 : 1;
}
