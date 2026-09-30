// Real-data validation for sr3zone: the `.czh_pc` SR3Z header (spec-
// ctorless-types.md Sec5) and the Mesh sub-blocks embedded in
// `.czn_pc`/`.gzn_pc` (spec-zone-data-format.md Sec7/Sec9.4).
//
// Walks the same archive set spec-zone-data-format.md Sec9.4 used for its
// own whole-population replay (sr3_city_0/1, sr3_city_missions, dlc1-3,
// misc, startup, patch_compressed/uncompressed), matching `.czh_pc`/
// `.czn_pc`/`.gzn_pc` triples by base name WITHIN one container level (the
// same pattern validate_rig.cpp uses for mesh/rig pairs) - `.gzn_pc`'s name
// is derived from `.czn_pc`'s by the standard c-to-g leading-character swap
// this project's c/g pairs always use; `.czh_pc` shares the SAME base name
// but a different extension word entirely, so it is matched by stem, not
// by the swap trick.
//
// TWO CONTROLS, mirroring the two the spec itself ran for the same claims:
//
//  1. The SR3Z record-array stride control (spec-ctorless-types.md Sec5:
//     "re-running the same arithmetic with strides 12/13/15/16 matches
//     2,342/2,971 in each case - exactly the zero-record files ... the
//     correct stride 14 matches 629/629 [non-zero-record files] and every
//     wrong stride matches 0/629"). Reproduced directly here against
//     whatever population this run covers - see runStrideControl().
//  2. A candidate-vs-validated control for the Mesh-block anchor scan: raw
//     4-aligned `u32 == 9` occurrences in `.czn_pc` content vs how many
//     actually survive sr3mesh::MeshBlock::parse()'s own invariants (exact
//     check-value/length/bookend contract). The gap between these two
//     numbers is this harness's own measure of how selective the anchor
//     really is - the spec's Sec7.2 ran the equivalent measurement
//     ("no other offset shows a version-9 reading above 4/1,002").
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
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
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

// --- .czh_pc stats -------------------------------------------------------
long long g_czhFound = 0, g_czhParsedOk = 0, g_czhFailed = 0;
long long g_versionCounts[3] = {0, 0, 0}; // index 0=27, 1=28, 2=29
long long g_runtimeSlotZero = 0, g_runtimeSlotNonzero = 0;
long long g_recordsTotal = 0;
long long g_czhWithRecords = 0, g_czhZeroRecords = 0;
int g_czhFailDumped = 0;

// Stride control (spec-ctorless-types.md Sec5): strides tried, index ->
// stride value.
const size_t kStrides[5] = {12, 13, 14, 15, 16};
long long g_strideMatchAll[5] = {0, 0, 0, 0, 0};       // whole population
long long g_strideMatchRecordsOnly[5] = {0, 0, 0, 0, 0}; // restricted to recordCount > 0

void checkCzh(const std::string& name, const std::vector<uint8_t>& b) {
    ++g_czhFound;
    try {
        sr3zone::ZoneHeader h = sr3zone::ZoneHeader::parse(sr3zone::ByteView(b.data(), b.size()));
        ++g_czhParsedOk;

        if (h.version() >= 27 && h.version() <= 29) ++g_versionCounts[h.version() - 27];
        if (h.runtimePointerSlotOnDisk() == 0) ++g_runtimeSlotZero;
        else ++g_runtimeSlotNonzero;
        g_recordsTotal += h.recordCount();
        if (h.recordCount() > 0) ++g_czhWithRecords;
        else ++g_czhZeroRecords;

        // --- Stride control: base is fixed (sr3zOffset + 0x40, already
        // 4-aligned), only the per-record multiplier varies. ---
        const size_t base = h.sr3zOffset() + 0x40;
        for (size_t s = 0; s < 5; ++s) {
            bool matches = (base + static_cast<size_t>(h.recordCount()) * kStrides[s] == b.size());
            if (matches) ++g_strideMatchAll[s];
            if (h.recordCount() > 0 && matches) ++g_strideMatchRecordsOnly[s];
        }
    } catch (const std::exception& ex) {
        ++g_czhFailed;
        if (g_czhFailDumped < 6) {
            ++g_czhFailDumped;
            printf("  CZH FAIL %s: %s\n", name.c_str(), ex.what());
        }
    }
}

// --- .czn_pc/.gzn_pc geometry stats --------------------------------------
long long g_pairsTotal = 0;          // every .czn_pc found, regardless of .gzn_pc
long long g_pairsWithGznSibling = 0; // has a .gzn_pc entry at all (may be zero-byte)
long long g_pairsWithNonemptyGzn = 0;
long long g_zeroByteGzn = 0;

long long g_pairsWithAtLeastOneBlock = 0;
long long g_pairsWithZeroBlocks = 0;
long long g_pairsWithNonemptyGznAndBlock = 0; // for the Sec9.4-style restricted comparison

long long g_totalBlocksFound = 0, g_gBackedBlocks = 0, g_inlineBlocks = 0;
long long g_candidateNineWords = 0; // raw 4-aligned u32==9 occurrences, across all .czn_pc scanned
int g_dumped = 0;

void checkCznGzn(const std::string& name, const std::vector<uint8_t>& cb, bool hasGznSibling,
                 const std::vector<uint8_t>& gb) {
    ++g_pairsTotal;
    if (hasGznSibling) {
        ++g_pairsWithGznSibling;
        if (!gb.empty()) ++g_pairsWithNonemptyGzn;
        else ++g_zeroByteGzn;
    }

    // Candidate-vs-validated control: raw 4-aligned u32==9 occurrences.
    for (size_t pos = 0; pos + 4 <= cb.size(); pos += 4) {
        uint32_t v = static_cast<uint32_t>(cb[pos]) | (static_cast<uint32_t>(cb[pos + 1]) << 8) |
                    (static_cast<uint32_t>(cb[pos + 2]) << 16) |
                    (static_cast<uint32_t>(cb[pos + 3]) << 24);
        if (v == 9) ++g_candidateNineWords;
    }

    sr3zone::ByteView gv = gb.empty() ? sr3zone::ByteView() : sr3zone::ByteView(gb.data(), gb.size());
    auto blocks = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(cb.data(), cb.size()), gv);

    g_totalBlocksFound += static_cast<long long>(blocks.size());
    for (const auto& e : blocks) {
        if (e.fromGFile) ++g_gBackedBlocks;
        else ++g_inlineBlocks;
    }
    if (!blocks.empty()) {
        ++g_pairsWithAtLeastOneBlock;
        if (hasGznSibling && !gb.empty()) ++g_pairsWithNonemptyGznAndBlock;
    } else {
        ++g_pairsWithZeroBlocks;
    }

    if (g_dumped < 5 && !blocks.empty()) {
        ++g_dumped;
        std::string extra;
        if (blocks[0].fromGFile) extra = " (gzn+0x" + std::to_string(blocks[0].gznOffset) + ")";
        printf("  %-40s %zu block(s), first: %s at czn+0x%zx%s\n", name.c_str(), blocks.size(),
               blocks[0].fromGFile ? "g-backed" : "inline", blocks[0].cznOffset, extra.c_str());
    }
}

void walk(const vpp::Container& c) {
    std::map<std::string, size_t> czh, czn, gzn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czh_pc", stem)) czh[stem] = i;
        else if (stemFor(n, ".czn_pc", stem)) czn[stem] = i;
        else if (stemFor(n, ".gzn_pc", stem)) gzn[stem] = i;
    }

    for (const auto& kv : czh) {
        std::vector<uint8_t> b;
        if (entryBytes(c, kv.second, b) && !b.empty()) checkCzh(kv.first, b);
    }
    for (const auto& kv : czn) {
        // Per-stem guard (bug found 2026-09-12, HANDOFF Sec9.55.1): a
        // malformed .gzn_pc sibling's declared Raw payload range can
        // exceed its containing .str2_pc's own size, and entryBytes()
        // throws for that - uncaught here, that exception used to abort
        // every REMAINING stem in this container (map-sorted order) plus
        // the whole "recurse into nested containers" loop below, losing
        // entire subtrees as collateral damage from one bad file. One bad
        // stem must not take out its siblings.
        try {
            std::vector<uint8_t> cb;
            if (!entryBytes(c, kv.second, cb)) continue;
            auto git = gzn.find(kv.first);
            std::vector<uint8_t> gb;
            bool hasGznSibling = false;
            if (git != gzn.end()) hasGznSibling = entryBytes(c, git->second, gb);
            checkCznGzn(kv.first, cb, hasGznSibling, gb);
        } catch (const std::exception&) {
            continue;
        }
    }

    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
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
            walk(c);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== sr3zone: .czh_pc / SR3Z header (spec-ctorless-types.md Sec5) ===\n");
    printf(".czh_pc found          : %lld   (spec: 2,971 across the full install)\n", g_czhFound);
    printf("parsed OK              : %lld\n", g_czhParsedOk);
    printf("failed                 : %lld\n", g_czhFailed);
    printf("version == 27 / 28 / 29 : %lld / %lld / %lld   (spec: every shipped file reads 29)\n",
           g_versionCounts[0], g_versionCounts[1], g_versionCounts[2]);
    printf("+0x18 runtime slot == 0 on disk : %lld / %lld   (spec: 2,971/2,971)\n",
           g_runtimeSlotZero, g_runtimeSlotZero + g_runtimeSlotNonzero);
    printf("records total          : %lld   (spec: 276,012 across 2,971 files)\n", g_recordsTotal);
    printf("files with 0 records   : %lld   (spec: 2,342/2,971)\n", g_czhZeroRecords);
    printf("files with >0 records  : %lld   (spec: 629/2,971)\n", g_czhWithRecords);

    printf("\n--- stride control (spec Sec5: correct=14 must uniquely match the\n");
    printf("    records-only subset; every wrong stride must match ONLY the\n");
    printf("    zero-record files, i.e. match count == g_czhZeroRecords) ---\n");
    for (size_t s = 0; s < 5; ++s) {
        printf("  stride %2zu : whole population %lld / %lld   records-only subset %lld / %lld\n",
               kStrides[s], g_strideMatchAll[s], g_czhParsedOk, g_strideMatchRecordsOnly[s],
               g_czhWithRecords);
    }

    printf("\n=== sr3zone: .czn_pc/.gzn_pc Mesh-block geometry (spec-zone-data-format.md "
           "Sec7/Sec9.4) ===\n");
    printf(".czn_pc found                    : %lld   (spec: 2,971)\n", g_pairsTotal);
    printf("  with a .gzn_pc sibling entry    : %lld   (spec: 1,083)\n", g_pairsWithGznSibling);
    printf("  .gzn_pc sibling, non-empty      : %lld\n", g_pairsWithNonemptyGzn);
    printf("  .gzn_pc sibling, zero bytes     : %lld   (spec: 81/1,083)\n", g_zeroByteGzn);
    printf("pairs with >=1 validated Mesh block : %lld\n", g_pairsWithAtLeastOneBlock);
    printf("pairs with 0 validated Mesh blocks  : %lld\n", g_pairsWithZeroBlocks);
    printf("  of which zero-byte .gzn_pc        : %lld   (spec: these should coincide)\n",
           g_zeroByteGzn);
    printf("restricted to non-empty .gzn_pc: >=1 block : %lld / %lld   (spec Sec9.4: 1,002/1,002)\n",
           g_pairsWithNonemptyGznAndBlock, g_pairsWithNonemptyGzn);
    printf("total Mesh blocks validated      : %lld   (g-backed %lld, inline %lld)\n",
           g_totalBlocksFound, g_gBackedBlocks, g_inlineBlocks);

    printf("\n--- anchor-selectivity control ---\n");
    printf("raw 4-aligned u32==9 occurrences scanned : %lld\n", g_candidateNineWords);
    printf("of which survived to a validated block    : %lld   (%.4f%% of candidates)\n",
           g_totalBlocksFound,
           g_candidateNineWords > 0
               ? 100.0 * static_cast<double>(g_totalBlocksFound) / static_cast<double>(g_candidateNineWords)
               : 0.0);

    return (g_czhFailed == 0) ? 0 : 1;
}
