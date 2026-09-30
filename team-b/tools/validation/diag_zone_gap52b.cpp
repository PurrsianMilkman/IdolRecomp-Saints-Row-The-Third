// Second-pass diagnostic for HANDOFF.md §9.55.1 Gap 2's still-open remainder
// (the "52 files with no g-backed anchor candidate at all" bucket).
//
// diag_zone_gap52.cpp already itemizes that bucket. This tool is a superset:
// it keeps that itemization and adds the checks that were missing from it,
// each of which exists to separate a specific rival explanation:
//
//   (1) BUCKET COMPLETENESS. diag_zone_gap52.cpp files a member only when
//       gBackedCandidates == 0 AND inlineCandidates == 0; a pair with 0
//       g-backed but >=1 inline-flagged candidate is silently filed nowhere,
//       so its bucket totals need not sum to the fail count. Here all four
//       buckets are itemized and the sum is asserted in the report.
//
//   (2) REVERSE-DIRECTION TEST (the main new measurement). The forward scan
//       asks "is there a u32==9 in .czn_pc?". This asks the complementary
//       question with an independent oracle: spec-zone-data-format.md §7.1
//       confirms 1,002/1,002 that a .gzn_pc SEGMENT opens on its own check
//       value followed by three zero words. So enumerate the segment starts
//       IN THE G-FILE by that signature, then search the .czn_pc for each
//       such check value at every byte offset. Three outcomes, and they mean
//       different things:
//         - check value found in .czn_pc AND u32 at (found-4) == 9  => the
//           anchor exists and the forward scan missed it (alignment/size
//           cutoff bug in the scan).
//         - check value found, but no 9 at -4 => the block is referenced but
//           the pre-header layout differs here (different encoding).
//         - check value NOT found anywhere in .czn_pc => the .czn_pc does not
//           reference this g-segment at all (wrong pairing, wrong bytes, or
//           the g-file is not a mesh g-file).
//       The same test is run over the PASSING pairs as a control, so the
//       negative has a baseline rather than standing alone.
//
//   (3) G-FILE SHAPE CHARACTERIZATION. For each fail member: does .gzn_pc
//       even LOOK like a mesh g-segment (u32@0 >= 0x10000, u32@4/8/12 == 0,
//       u16@0x10 == 0 per §7.1)? Plus zero-byte fraction, distinct-byte
//       count, and a 256-byte hex+ASCII preview - so "what IS in there"
//       is described from the bytes rather than assumed.
//
//   (4) SIZE CONTEXT. czn/gzn size distributions for the PASS bucket vs the
//       fail buckets, so "too short to hold a Mesh sub-block" is answered
//       with the population's own numbers instead of an eyeballed threshold.
//
// Read-only instrumentation. Nothing in src/ is modified or called
// differently from production; sr3zone::ZoneGeometry::locate() remains the
// authoritative pass/fail signal and only the candidate-side scan is
// duplicated (the independent-replica control pattern already used by
// validate_groupsearch.cpp and diag_zone_gaps.cpp).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3mesh/mesh_block.h"
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

uint32_t rawReadU32LE(const uint8_t* d, size_t p) {
    return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
           (static_cast<uint32_t>(d[p + 2]) << 16) | (static_cast<uint32_t>(d[p + 3]) << 24);
}

std::string hex32(uint32_t v) {
    char b[16];
    snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

bool getEntry(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
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

// ---------------------------------------------------------------------------
// Forward candidate scan - byte-for-byte the same predicate production
// zone_geometry.cpp::locate() uses, replicated so the bucket split here is
// directly comparable to diag_zone_gaps.cpp's.
// ---------------------------------------------------------------------------
struct CandidateScan {
    long long alignedNineTotal = 0;      // every 4-aligned u32 == 9, no other condition
    long long alignedNineFlagsReachable = 0;
    long long gBacked = 0;
    long long inlineFlagged = 0;
};

CandidateScan scanCandidates4Aligned(const std::vector<uint8_t>& cb) {
    CandidateScan s;
    for (size_t pos = 0; pos + 4 <= cb.size(); pos += 4) {
        if (rawReadU32LE(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
        ++s.alignedNineTotal;
        if (pos + sr3mesh::kHeaderStart + 1 > cb.size()) continue;
        ++s.alignedNineFlagsReachable;
        uint8_t flags = cb[pos + sr3mesh::kHeaderStart + sr3mesh::kFlagsOffset];
        if ((flags & sr3mesh::kFlagBulkInGFile) != 0) ++s.gBacked; else ++s.inlineFlagged;
    }
    return s;
}

// Diagnostic-only unaligned rescan - EVERY byte offset. Never feeds back into
// locate(); it exists only to answer "is a plausible anchor sitting off the
// 4-alignment the production scan assumes".
struct UnalignedHit {
    size_t pos = 0;
    int mod4 = 0;
    bool flagsReachable = false;
    uint8_t flagsByte = 0;
    bool wantsGFile = false;
};

std::vector<UnalignedHit> scanUnaligned(const std::vector<uint8_t>& cb, size_t cap, size_t& totalOut) {
    std::vector<UnalignedHit> hits;
    totalOut = 0;
    for (size_t pos = 0; pos + 4 <= cb.size(); ++pos) {
        if (rawReadU32LE(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
        ++totalOut;
        if (hits.size() >= cap) continue;
        UnalignedHit h;
        h.pos = pos;
        h.mod4 = static_cast<int>(pos % 4);
        h.flagsReachable = (pos + sr3mesh::kHeaderStart + 1 <= cb.size());
        h.flagsByte = h.flagsReachable ? cb[pos + sr3mesh::kHeaderStart + sr3mesh::kFlagsOffset] : 0;
        h.wantsGFile = h.flagsReachable && (h.flagsByte & sr3mesh::kFlagBulkInGFile) != 0;
        hits.push_back(h);
    }
    return hits;
}

// ---------------------------------------------------------------------------
// Reverse-direction test. spec-zone-data-format.md §7.1 (CONFIRMED 1,002/
// 1,002): a .gzn_pc segment opens on the block's own check value followed by
// three zero words. Enumerate segment starts by that signature, then look for
// each check value inside the .czn_pc at EVERY byte offset.
// ---------------------------------------------------------------------------
struct GSegment {
    size_t gOffset = 0;
    uint32_t checkValue = 0;
};

std::vector<GSegment> findGSegments(const std::vector<uint8_t>& gb, size_t cap) {
    std::vector<GSegment> out;
    for (size_t p = 0; p + 16 <= gb.size() && out.size() < cap; p += 16) {
        uint32_t cv = rawReadU32LE(gb.data(), p);
        if (cv < 0x10000) continue;                       // §7.3: check values are hash-like, >= 0x10000
        if (rawReadU32LE(gb.data(), p + 4) != 0) continue;  // three zero words
        if (rawReadU32LE(gb.data(), p + 8) != 0) continue;
        if (rawReadU32LE(gb.data(), p + 12) != 0) continue;
        out.push_back({p, cv});
    }
    return out;
}

struct ReverseHit {
    size_t cznPos = 0;
    uint32_t wordBefore = 0;   // u32 at cznPos-4; the Mesh pre-header says this must be 9
    bool hasWordBefore = false;
};

std::vector<ReverseHit> findValueInC(const std::vector<uint8_t>& cb, uint32_t value, size_t cap) {
    std::vector<ReverseHit> hits;
    for (size_t p = 0; p + 4 <= cb.size() && hits.size() < cap; ++p) {
        if (rawReadU32LE(cb.data(), p) != value) continue;
        ReverseHit h;
        h.cznPos = p;
        h.hasWordBefore = (p >= 4);
        if (h.hasWordBefore) h.wordBefore = rawReadU32LE(cb.data(), p - 4);
        hits.push_back(h);
    }
    return hits;
}

std::string hexAsciiPreview(const std::vector<uint8_t>& b, size_t n) {
    n = std::min(n, b.size());
    std::string out;
    char buf[8];
    for (size_t row = 0; row < n; row += 16) {
        snprintf(buf, sizeof(buf), "%04zX  ", row);
        out += buf;
        for (size_t i = row; i < row + 16; ++i) {
            if (i < n) { snprintf(buf, sizeof(buf), "%02X ", b[i]); out += buf; }
            else out += "   ";
        }
        out += " |";
        for (size_t i = row; i < row + 16 && i < n; ++i)
            out += (b[i] >= 32 && b[i] < 127) ? static_cast<char>(b[i]) : '.';
        out += "|\n           ";
    }
    return out;
}

struct ByteStats {
    double zeroFraction = 0.0;
    size_t distinctBytes = 0;
};

ByteStats byteStats(const std::vector<uint8_t>& b) {
    ByteStats s;
    if (b.empty()) return s;
    size_t counts[256] = {0};
    for (uint8_t v : b) ++counts[v];
    s.zeroFraction = static_cast<double>(counts[0]) / static_cast<double>(b.size());
    for (size_t i = 0; i < 256; ++i) if (counts[i]) ++s.distinctBytes;
    return s;
}

// ---------------------------------------------------------------------------
// Per-member record
// ---------------------------------------------------------------------------
struct Member {
    std::string containerPath;
    std::string stem;
    int bucket = 0;               // 1 = no candidate at all, 2 = inline-flagged only
    size_t cznSize = 0;
    size_t gznSize = 0;
    uint32_t leadingId = 0;
    uint32_t leadingLen = 0;
    bool czhFound = false;
    bool czhParsedOk = false;
    uint16_t czhRecordCount = 0;
    std::string czhError;
    CandidateScan scan;
    size_t unalignedTotal = 0;
    std::vector<UnalignedHit> unalignedSample;
    // g-file shape
    uint32_t gWord0 = 0, gWord1 = 0, gWord2 = 0, gWord3 = 0;
    uint16_t gU16At0x10 = 0;
    bool gLooksLikeSegmentOpen = false;
    ByteStats gStats, cStats;
    // reverse test
    size_t gSegmentCount = 0;
    size_t gSegmentsFoundInC = 0;
    size_t gSegmentsFoundInCWithNineBefore = 0;
    std::vector<std::string> reverseDetail;
    std::string gznPreview;
    std::string cznPreview;
};

std::vector<Member> g_members;                                  // buckets 1 + 2, full detail
std::vector<std::pair<std::string, std::string>> g_bucketNoMatch;   // bucket 3
std::vector<std::pair<std::string, std::string>> g_bucketParseReject; // bucket 4

long long g_pairsNonemptyGzn = 0, g_pairsValidated = 0, g_pairsZeroBlocks = 0;

// Reverse-test CONTROL over the PASSING pairs.
long long g_ctlPassPairs = 0, g_ctlPassSegments = 0, g_ctlPassSegFoundInC = 0,
          g_ctlPassSegFoundWithNine = 0;

std::map<uint32_t, long long> g_leadingIdAll, g_leadingIdMembers;
long long g_leadingIdAllTotal = 0;

std::vector<size_t> g_passCznSizes, g_passGznSizes, g_memberCznSizes, g_memberGznSizes;
std::map<uint16_t, long long> g_recCountPass, g_recCountMember;

void sizeSummary(const char* label, std::vector<size_t> v) {
    if (v.empty()) { printf("  %-28s (none)\n", label); return; }
    std::sort(v.begin(), v.end());
    printf("  %-28s n=%zu  min=%zu  p25=%zu  median=%zu  p75=%zu  max=%zu\n", label, v.size(), v.front(),
           v[v.size() / 4], v[v.size() / 2], v[v.size() * 3 / 4], v.back());
}

void checkCznGzn(const std::string& containerPath, const std::string& stem, const std::vector<uint8_t>& cb,
                 bool hasGzn, const std::vector<uint8_t>& gb,
                 const std::map<std::string, std::vector<uint8_t>>& czhBytesByStem) {
    if (cb.size() >= 4) { ++g_leadingIdAllTotal; ++g_leadingIdAll[rawReadU32LE(cb.data(), 0)]; }
    if (!hasGzn || gb.empty()) return;

    ++g_pairsNonemptyGzn;

    auto blocks = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(cb.data(), cb.size()),
                                                sr3zone::ByteView(gb.data(), gb.size()));
    uint16_t recCount = 0;
    bool haveRec = false;
    auto czhIt = czhBytesByStem.find(stem);
    std::string czhErr;
    bool czhFound = czhIt != czhBytesByStem.end();
    if (czhFound) {
        try {
            sr3zone::ZoneHeader h =
                sr3zone::ZoneHeader::parse(sr3zone::ByteView(czhIt->second.data(), czhIt->second.size()));
            recCount = h.recordCount();
            haveRec = true;
        } catch (const std::exception& ex) { czhErr = ex.what(); }
    }

    if (!blocks.empty()) {
        ++g_pairsValidated;
        g_passCznSizes.push_back(cb.size());
        g_passGznSizes.push_back(gb.size());
        if (haveRec) ++g_recCountPass[recCount];
        // Reverse-test control on a passing pair: the same measurement,
        // where the answer is already known to be "yes".
        ++g_ctlPassPairs;
        // Only the FIRST g-segment per passing pair: this is a control, one
        // measurement per file is enough, and scanning the c-file once per
        // segment over 932 passing pairs would dominate the run time.
        auto segs = findGSegments(gb, 1);
        for (const auto& s : segs) {
            ++g_ctlPassSegments;
            auto hits = findValueInC(cb, s.checkValue, 8);
            if (!hits.empty()) {
                ++g_ctlPassSegFoundInC;
                for (const auto& h : hits)
                    if (h.hasWordBefore && h.wordBefore == sr3mesh::kMeshVersion) {
                        ++g_ctlPassSegFoundWithNine;
                        break;
                    }
            }
        }
        return;
    }

    ++g_pairsZeroBlocks;
    CandidateScan scan = scanCandidates4Aligned(cb);

    if (scan.gBacked > 0) {
        // Split bucket 3 vs 4 with the same predicate diag_zone_gaps.cpp uses.
        std::unordered_map<uint32_t, int> gIndex;
        for (size_t p = 0; p + 4 <= gb.size(); p += 16) ++gIndex[rawReadU32LE(gb.data(), p)];
        long long withMatch = 0;
        for (size_t pos = 0; pos + 4 <= cb.size(); pos += 4) {
            if (rawReadU32LE(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
            if (pos + sr3mesh::kHeaderStart + 1 > cb.size()) continue;
            if ((cb[pos + sr3mesh::kHeaderStart + sr3mesh::kFlagsOffset] & sr3mesh::kFlagBulkInGFile) == 0)
                continue;
            if (pos + sr3mesh::kCheckValueOffset + 4 > cb.size()) continue;
            if (gIndex.count(rawReadU32LE(cb.data(), pos + sr3mesh::kCheckValueOffset))) ++withMatch;
        }
        if (withMatch == 0) g_bucketNoMatch.push_back({containerPath, stem});
        else g_bucketParseReject.push_back({containerPath, stem});
        return;
    }

    // --- buckets 1 and 2: no g-backed candidate anywhere in the .czn_pc ---
    Member m;
    m.containerPath = containerPath;
    m.stem = stem;
    m.bucket = (scan.inlineFlagged == 0) ? 1 : 2;
    m.cznSize = cb.size();
    m.gznSize = gb.size();
    m.scan = scan;
    if (cb.size() >= 4) m.leadingId = rawReadU32LE(cb.data(), 0);
    if (cb.size() >= 8) m.leadingLen = rawReadU32LE(cb.data(), 4);
    m.czhFound = czhFound;
    m.czhParsedOk = haveRec;
    m.czhRecordCount = recCount;
    m.czhError = czhErr;
    if (haveRec) ++g_recCountMember[recCount];

    m.unalignedSample = scanUnaligned(cb, 8, m.unalignedTotal);

    if (gb.size() >= 4) m.gWord0 = rawReadU32LE(gb.data(), 0);
    if (gb.size() >= 8) m.gWord1 = rawReadU32LE(gb.data(), 4);
    if (gb.size() >= 12) m.gWord2 = rawReadU32LE(gb.data(), 8);
    if (gb.size() >= 16) m.gWord3 = rawReadU32LE(gb.data(), 12);
    if (gb.size() >= 18) m.gU16At0x10 = static_cast<uint16_t>(gb[16] | (gb[17] << 8));
    m.gLooksLikeSegmentOpen = gb.size() >= 16 && m.gWord0 >= 0x10000 && m.gWord1 == 0 && m.gWord2 == 0 &&
                              m.gWord3 == 0;
    m.gStats = byteStats(gb);
    m.cStats = byteStats(cb);

    auto segs = findGSegments(gb, 64);
    m.gSegmentCount = segs.size();
    for (const auto& s : segs) {
        auto hits = findValueInC(cb, s.checkValue, 8);
        if (hits.empty()) {
            if (m.reverseDetail.size() < 8)
                m.reverseDetail.push_back("gzn+0x" + std::to_string(s.gOffset) + " cv=" + hex32(s.checkValue) +
                                          " -> NOT FOUND anywhere in .czn_pc");
            continue;
        }
        ++m.gSegmentsFoundInC;
        bool nine = false;
        std::string detail = "gzn+0x" + std::to_string(s.gOffset) + " cv=" + hex32(s.checkValue) + " -> found at czn offsets:";
        for (const auto& h : hits) {
            char buf[96];
            snprintf(buf, sizeof(buf), " 0x%zx(mod4=%d,before=%s)", h.cznPos, static_cast<int>(h.cznPos % 4),
                     h.hasWordBefore ? hex32(h.wordBefore).c_str() : "n/a");
            detail += buf;
            if (h.hasWordBefore && h.wordBefore == sr3mesh::kMeshVersion) nine = true;
        }
        if (nine) ++m.gSegmentsFoundInCWithNineBefore;
        if (m.reverseDetail.size() < 8) m.reverseDetail.push_back(detail);
    }

    m.gznPreview = hexAsciiPreview(gb, 256);
    m.cznPreview = hexAsciiPreview(cb, 128);

    ++g_leadingIdMembers[m.leadingId];
    g_memberCznSizes.push_back(cb.size());
    g_memberGznSizes.push_back(gb.size());
    g_members.push_back(std::move(m));
}

void walk(const vpp::Container& c, const std::string& containerPath) {
    std::map<std::string, size_t> czh, czn, gzn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czh_pc", stem)) czh[stem] = i;
        else if (stemFor(n, ".czn_pc", stem)) czn[stem] = i;
        else if (stemFor(n, ".gzn_pc", stem)) gzn[stem] = i;
    }

    std::map<std::string, std::vector<uint8_t>> czhBytesByStem;
    for (const auto& kv : czn) {
        auto it = czh.find(kv.first);
        if (it == czh.end()) continue;
        std::vector<uint8_t> b;
        try {
            if (getEntry(c, it->second, b) && !b.empty()) czhBytesByStem[kv.first] = std::move(b);
        } catch (const std::exception&) {}
    }

    for (const auto& kv : czn) {
        try {
            std::vector<uint8_t> cb;
            if (!getEntry(c, kv.second, cb)) continue;
            auto git = gzn.find(kv.first);
            std::vector<uint8_t> gb;
            bool hasGzn = false;
            if (git != gzn.end()) hasGzn = getEntry(c, git->second, gb);
            checkCznGzn(containerPath, kv.first, cb, hasGzn, gb, czhBytesByStem);
        } catch (const std::exception&) {
            continue; // §9.55.1's lesson: one bad stem must not take out its siblings
        }
    }

    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i), containerPath + " > " + c.entries()[i].name); }
            catch (const std::exception&) {}
        }
    }
}

void printReport() {
    printf("\n=== POPULATION ===\n");
    printf("non-empty .gzn_pc pairs scanned : %lld\n", g_pairsNonemptyGzn);
    printf("  >=1 validated Mesh block      : %lld\n", g_pairsValidated);
    printf("  0 validated Mesh blocks       : %lld\n", g_pairsZeroBlocks);
    long long b1 = 0, b2 = 0;
    for (const auto& m : g_members) { if (m.bucket == 1) ++b1; else ++b2; }
    printf("    bucket 1 'no 4-aligned u32==9 candidate of ANY kind' : %lld\n", b1);
    printf("    bucket 2 'inline-flagged candidates only, 0 g-backed': %lld\n", b2);
    printf("    bucket 3 'g-backed candidate, no check-value match'  : %zu\n", g_bucketNoMatch.size());
    printf("    bucket 4 'check-value match, MeshBlock::parse reject': %zu\n", g_bucketParseReject.size());
    printf("    SUM = %lld (must equal the 0-validated count above)\n",
           b1 + b2 + static_cast<long long>(g_bucketNoMatch.size()) +
               static_cast<long long>(g_bucketParseReject.size()));

    printf("\n=== REVERSE-TEST CONTROL, over PASSING pairs ===\n");
    printf("  passing pairs measured                                  : %lld\n", g_ctlPassPairs);
    printf("  g-segments located in .gzn_pc by the §7.1 signature      : %lld\n", g_ctlPassSegments);
    printf("  ... whose check value IS found somewhere in the .czn_pc  : %lld (%.2f%%)\n",
           g_ctlPassSegFoundInC,
           g_ctlPassSegments ? 100.0 * (double)g_ctlPassSegFoundInC / (double)g_ctlPassSegments : 0.0);
    printf("  ... with u32==9 exactly 4 bytes before that position     : %lld (%.2f%%)\n",
           g_ctlPassSegFoundWithNine,
           g_ctlPassSegments ? 100.0 * (double)g_ctlPassSegFoundWithNine / (double)g_ctlPassSegments : 0.0);

    printf("\n=== SIZE DISTRIBUTIONS ===\n");
    sizeSummary("PASS .czn_pc size", g_passCznSizes);
    sizeSummary("PASS .gzn_pc size", g_passGznSizes);
    sizeSummary("MEMBER .czn_pc size", g_memberCznSizes);
    sizeSummary("MEMBER .gzn_pc size", g_memberGznSizes);
    printf("  (a bare Mesh sub-block pre-header + header needs %zu bytes)\n",
           sr3mesh::kHeaderStart + sr3mesh::kHeaderSize);

    printf("\n=== .czh_pc recordCount() distribution ===\n");
    printf("PASS bucket:\n");
    for (const auto& kv : g_recCountPass)
        if (kv.second >= 3) printf("  recordCount=%u : %lld\n", kv.first, kv.second);
    printf("MEMBER bucket (every value listed):\n");
    for (const auto& kv : g_recCountMember) printf("  recordCount=%u : %lld\n", kv.first, kv.second);

    printf("\n=== leading .czn_pc id, ALL visited (%lld) ===\n", g_leadingIdAllTotal);
    long long tail = 0;
    for (const auto& kv : g_leadingIdAll) {
        if (kv.second >= 5) printf("  %s : %lld\n", hex32(kv.first).c_str(), kv.second);
        else tail += kv.second;
    }
    printf("  (ids occurring <5 times, summed) : %lld\n", tail);
    printf("=== leading .czn_pc id, MEMBER subset ===\n");
    for (const auto& kv : g_leadingIdMembers) printf("  %s : %lld\n", hex32(kv.first).c_str(), kv.second);

    printf("\n=== MEMBER LIST (compact) ===\n");
    int idx = 0;
    for (const auto& m : g_members) {
        ++idx;
        const std::string recStr = m.czhParsedOk ? std::to_string(m.czhRecordCount) : std::string("?");
        const std::string leadStr = hex32(m.leadingId);
        printf("%3d  b%d  czn=%-9zu gzn=%-9zu  ali9=%-4lld unali9=%-4zu  gSeg=%-3zu revFound=%-3zu rev9=%-3zu"
               "  lead=%s rec=%s  gOpen=%s gzero=%.2f  %s :: %s\n",
               idx, m.bucket, m.cznSize, m.gznSize, m.scan.alignedNineTotal, m.unalignedTotal,
               m.gSegmentCount, m.gSegmentsFoundInC, m.gSegmentsFoundInCWithNineBefore, leadStr.c_str(),
               recStr.c_str(), m.gLooksLikeSegmentOpen ? "Y" : "n", m.gStats.zeroFraction,
               m.containerPath.c_str(), m.stem.c_str());
    }

    printf("\n=== MEMBER DETAIL ===\n");
    idx = 0;
    for (const auto& m : g_members) {
        ++idx;
        printf("\n--- #%d  bucket %d  %s :: %s ---\n", idx, m.bucket, m.containerPath.c_str(), m.stem.c_str());
        printf("  czn size = %zu   gzn size = %zu   (Mesh pre-header+header alone needs >= %zu)\n", m.cznSize,
               m.gznSize, sr3mesh::kHeaderStart + sr3mesh::kHeaderSize);
        printf("  czn leading {id,len}: id=%s len=%u (%s)\n", hex32(m.leadingId).c_str(), m.leadingLen,
               (m.leadingId == 0x80002233 || m.leadingId == 0x80002237 || m.leadingId == 0x80002234)
                   ? "a known leading id"
                   : "NOT one of the three known leading ids");
        if (m.czhFound) {
            if (m.czhParsedOk) printf("  paired .czh_pc: parsed OK, recordCount=%u\n", m.czhRecordCount);
            else printf("  paired .czh_pc: FOUND but failed to parse: %s\n", m.czhError.c_str());
        } else printf("  paired .czh_pc: NOT FOUND in this container\n");
        printf("  4-aligned u32==9 in czn: %lld total, %lld with flags byte reachable"
               " (g-backed %lld / inline-flagged %lld)\n",
               m.scan.alignedNineTotal, m.scan.alignedNineFlagsReachable, m.scan.gBacked, m.scan.inlineFlagged);
        printf("  UNALIGNED u32==9 in czn (every byte offset): %zu\n", m.unalignedTotal);
        for (const auto& h : m.unalignedSample)
            printf("    czn+0x%zx (mod4=%d)%s\n", h.pos, h.mod4,
                   h.flagsReachable ? (h.wantsGFile ? "  flags peek: g-backed" : "  flags peek: inline/other")
                                    : "  (too close to end to reach flags byte)");
        printf("  gzn head words: w0=%s w1=%s w2=%s w3=%s  u16@0x10=%u  looksLikeSegmentOpen=%s\n",
               hex32(m.gWord0).c_str(), hex32(m.gWord1).c_str(), hex32(m.gWord2).c_str(),
               hex32(m.gWord3).c_str(), m.gU16At0x10, m.gLooksLikeSegmentOpen ? "YES" : "no");
        printf("  gzn bytes: zeroFraction=%.4f distinctByteValues=%zu | czn bytes: zeroFraction=%.4f "
               "distinctByteValues=%zu\n",
               m.gStats.zeroFraction, m.gStats.distinctBytes, m.cStats.zeroFraction, m.cStats.distinctBytes);
        printf("  REVERSE TEST: %zu g-segment opens found in gzn; %zu of their check values appear in czn; "
               "%zu of those have u32==9 four bytes before\n",
               m.gSegmentCount, m.gSegmentsFoundInC, m.gSegmentsFoundInCWithNineBefore);
        for (const auto& d : m.reverseDetail) printf("    %s\n", d.c_str());
        printf("  gzn preview (first %zu bytes):\n           %s\n", std::min<size_t>(256, m.gznSize),
               m.gznPreview.c_str());
        printf("  czn preview (first %zu bytes):\n           %s\n", std::min<size_t>(128, m.cznSize),
               m.cznPreview.c_str());
    }

    printf("\n=== BUCKET 3 (g-backed candidate, no check-value match) ===\n");
    for (const auto& p : g_bucketNoMatch) printf("  %s :: %s\n", p.first.c_str(), p.second.c_str());
    printf("\n=== BUCKET 4 (check-value match, parse rejected) ===\n");
    for (const auto& p : g_bucketParseReject) printf("  %s :: %s\n", p.first.c_str(), p.second.c_str());
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("skip (empty/unreadable): %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c, argv[i]);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", argv[i], ex.what());
        }
    }
    printReport();
    return 0;
}
