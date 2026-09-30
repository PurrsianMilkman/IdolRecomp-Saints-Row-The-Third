// Diagnostic harness for the three unreconciled sr3zone gaps flagged in
// HANDOFF.md Sec9.55 / validate_zone.cpp's own report:
//
//   (1) 46-file .czn_pc extraction gap (2,971 entries exist, 2,925 usable)
//   (2) 93.1% vs 100% Mesh-block hit rate on non-empty .gzn_pc pairs
//   (3) 0% vs spec Sec7.3's ~23% inline-mode sample estimate
//
// This does NOT change src/zone_geometry.cpp, src/zone_header.cpp, or
// vpp::Container - it is read-only instrumentation on top of them, built to
// answer three specific factual questions:
//
//   (a) for every .czn_pc entry that fails to yield usable bytes: which
//       archive, which entry name, its exact vpp::DecodeStatus, whether it
//       is entry index 0 or later within ITS OWN container, and whether
//       that container is multi-entry mode-(a) (the shape
//       DecodeStatus::OkUnconfirmedContent's own doc names as the at-risk
//       one) - Team A's exact requested population/predicate for gap 1.
//   (b) among .czn_pc/.gzn_pc pairs where .gzn_pc reads non-empty (the same
//       "non-empty" test validate_zone.cpp already uses) but 0 Mesh blocks
//       validate: does that correlate with a specific DecodeStatus (esp.
//       OkUnconfirmedContent) on either side of the pair, vs the pairs that
//       DO validate - and, independent of decode status, how far the
//       anchor scan even gets (no g-backed candidate found at all / a
//       candidate found but no check-value match in .gzn_pc / a check-value
//       match found but sr3mesh::MeshBlock::parse rejected it).
//   (c) for every 4-aligned `u32 == 9` candidate in `.czn_pc` whose flags
//       byte peek says "inline" (kFlagBulkInGFile clear - the same peek
//       zone_geometry.cpp's locate() uses): does it actually pass
//       sr3mesh::MeshBlock::parse(), and if not, which invariant fails and
//       by how many bytes - distinguishing "real block, parser too strict"
//       from "coincidental byte pattern, not a real block" with evidence
//       rather than assertion.
//
// Reuses sr3zone::ZoneGeometry::locate() as the AUTHORITATIVE validated-
// block count (so PASS/FAIL bucketing here matches validate_zone.cpp's own
// 932/1,001 exactly) and duplicates only the CANDIDATE-side scan - the same
// kind of independent-replica control validate_groupsearch.cpp already uses
// for a different invariant.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3mesh/mesh_block.h"
#include "sr3zone/zone_geometry.h"
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

uint32_t rawReadU32LE(const uint8_t* data, size_t pos) {
    return static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
}

size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }

std::string hex32(uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X", v);
    return buf;
}

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
    return "?unknown?";
}

bool isUsable(vpp::DecodeStatus s) {
    return s == vpp::DecodeStatus::Ok || s == vpp::DecodeStatus::OkUnconfirmedContent ||
           s == vpp::DecodeStatus::ContentValidated || s == vpp::DecodeStatus::RecoveredSharedStream ||
           s == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
}

// One entry's extraction outcome, kind and status preserved regardless of
// whether it was usable - gap 1/2 both need the exact status, not just ok/
// not-ok.
struct EntryStatus {
    bool ok = false;
    bool isRaw = false;
    vpp::DecodeStatus status = vpp::DecodeStatus::Ok;
    std::string diagnostic;
    std::vector<uint8_t> data;
};

EntryStatus getEntry(const vpp::Container& c, size_t i) {
    EntryStatus es;
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        es.isRaw = true;
        es.ok = true;
        vpp::ByteView r = c.rawEntryBytes(i);
        es.data.assign(r.data(), r.data() + r.size());
        return es;
    }
    auto r = c.decompressEntry(i);
    es.status = r.status;
    es.diagnostic = r.diagnostic;
    es.ok = isUsable(r.status);
    es.data = std::move(r.data);
    return es;
}

// --- Gap 1: failed .czn_pc extractions ----------------------------------
struct FailedExtraction {
    std::string containerPath;   // breadcrumb: top-level archive [> nested...]
    std::string entryName;       // the .czn_pc entry's own name
    size_t entryIndex = 0;       // index within ITS OWN container
    size_t containerEntryCount = 0;
    bool containerSharedStreamMode = false;
    std::string status;
    std::string diagnostic;
};
std::vector<FailedExtraction> g_failedCzn;
long long g_totalCznAttempted = 0;
std::vector<std::string> g_allAttemptedStems;
long long g_cznGznThrew = 0;
std::vector<std::string> g_cznGznThrowExamples;

// --- Gap 2: status correlation for non-empty .gzn_pc pairs --------------
long long g_pairsNonemptyGzn = 0;
long long g_pairsNonemptyGznValidated = 0;   // >=1 Mesh block (authoritative, via locate())
long long g_pairsNonemptyGznZeroBlocks = 0;

std::map<std::string, long long> g_gznStatusWhenPass, g_gznStatusWhenFail;
std::map<std::string, long long> g_cznStatusWhenPass, g_cznStatusWhenFail;

// container.cpp tags every RecoveredSharedStream(LongChain) diagnostic with
// one of these three prefixes (src/container.cpp lines ~154/160/183),
// recording whether the disassembly-confirmed archive-level flags bit 0x2
// AGREED with the independently-derived offset-heuristic recovery for this
// specific entry - a much finer signal than the DecodeStatus enum value
// alone, which is identical (RecoveredSharedStream/LongChain) for the
// overwhelming majority of entries in these archives regardless of PASS/FAIL.
std::string diagPrefix(const std::string& d) {
    if (d.find("[MODE DISAGREEMENT]") != std::string::npos) return "MODE_DISAGREEMENT";
    if (d.find("[flags-confirmed]") != std::string::npos) return "flags-confirmed";
    if (d.find("[note]") != std::string::npos) return "note(heuristic-could-not-corroborate)";
    return "(no tag / Ok / primary-mode-a)";
}
std::map<std::string, long long> g_gznDiagPrefixWhenPass, g_gznDiagPrefixWhenFail;
std::map<std::string, long long> g_cznDiagPrefixWhenPass, g_cznDiagPrefixWhenFail;

// Among the FAIL bucket, how far did the g-backed anchor scan get, before
// even reaching MeshBlock::parse's own invariants?
long long g_failNoGBackedCandidateAtAll = 0;
long long g_failCandidateButNoCheckValueMatch = 0;
long long g_failMatchButParseRejected = 0;
// (a pair can show >0 candidates found and >0 matches but 0 validated - the
// last bucket - or >0 candidates and 0 matches - the middle bucket - or 0
// candidates at all - the first. These three are mutually exclusive and
// should sum to g_pairsNonemptyGznZeroBlocks minus any pair whose only
// candidates were inline-flagged (tracked separately, see below).)
long long g_failOnlyInlineFlaggedCandidates = 0; // 0 g-backed candidates, but >=1 inline-flagged candidate existed

// --- Gap 3: inline-candidate categorization ------------------------------
long long g_totalGBackedCandidates = 0;
long long g_totalGBackedWithCheckMatch = 0;
long long g_totalInlineCandidates = 0;

std::map<std::string, long long> g_inlineCategoryCounts;
std::map<std::string, int> g_inlineExampleCap;
std::vector<std::string> g_inlineExamples;
constexpr int kMaxExamplesPerCategory = 4;

struct InlineDiag {
    std::string category;
    std::string detail;
};

// Duplicates sr3mesh::MeshBlock::parse()'s inline-mode walk step by step
// (mesh_block.cpp lines ~58-184), but never throws - it classifies exactly
// which invariant is the first to fail and reports the numeric gap, which
// parse()'s own exception text does not always spell out (e.g. the overrun
// amount for the index/channel-data steps).
InlineDiag diagnoseInline(const std::vector<uint8_t>& cb, size_t pos) {
    const size_t csize = cb.size();
    auto u32 = [&](size_t off) { return rawReadU32LE(cb.data(), off); };

    if (pos + sr3mesh::kHeaderStart + sr3mesh::kHeaderSize > csize) {
        return {"A_header_oob", "need " +
                                     std::to_string(pos + sr3mesh::kHeaderStart + sr3mesh::kHeaderSize) +
                                     " bytes for header, have " + std::to_string(csize)};
    }

    uint32_t checkValue = u32(pos + sr3mesh::kCheckValueOffset);
    uint32_t gLength = u32(pos + sr3mesh::kGLengthOffset);
    const size_t header = pos + sr3mesh::kHeaderStart;
    uint8_t flags = cb[header + sr3mesh::kFlagsOffset];
    uint32_t channelCount = u32(header + sr3mesh::kChannelCountOffset);

    if (flags & sr3mesh::kFlagMultiStream) {
        return {"B_multistream_flag_set", "flags=" + hex32(flags) + " (out of scope, not comparable)"};
    }

    const size_t recordsAt = header + sr3mesh::kHeaderSize;
    const size_t neededForRecords = recordsAt + static_cast<size_t>(channelCount) * sr3mesh::kChannelRecordSize;
    if (neededForRecords > csize) {
        return {"C_channel_records_oob", "channelCount=" + std::to_string(channelCount) + " needs " +
                                              std::to_string(neededForRecords) + ", have " +
                                              std::to_string(csize)};
    }

    const size_t segmentBase = neededForRecords; // inline: segment == cContent, base after channel records
    if (segmentBase + gLength > csize) {
        long long over = static_cast<long long>(segmentBase + gLength) - static_cast<long long>(csize);
        return {"D_segment_oob", "declared gLength=" + std::to_string(gLength) + " overruns c-file end by " +
                                      std::to_string(over) + " bytes"};
    }
    if (segmentBase + 4 > csize) {
        return {"D2_segment_lt4", "gLength=" + std::to_string(gLength) + " leaves < 4 bytes for the leading check"};
    }

    uint32_t leading = u32(segmentBase + 0);
    if (leading != checkValue) {
        return {"E_leading_check_mismatch",
                "leading=" + hex32(leading) + " declaredCheck=" + hex32(checkValue)};
    }

    size_t cursor = alignUp(4, 16);
    uint32_t indexCount = u32(header + sr3mesh::kIndexCountOffset);
    uint8_t indexElemSize = cb[header + sr3mesh::kIndexSizeOffset];
    size_t indexBytes = static_cast<size_t>(indexCount) * indexElemSize;
    if (cursor + indexBytes > gLength) {
        long long over = static_cast<long long>(cursor + indexBytes) - static_cast<long long>(gLength);
        return {"F_index_overrun", "indexCount=" + std::to_string(indexCount) + " elemSize=" +
                                        std::to_string(static_cast<int>(indexElemSize)) +
                                        " overruns declared gLength by " + std::to_string(over) + " bytes"};
    }
    cursor += indexBytes;

    for (uint32_t ci = 0; ci < channelCount; ++ci) {
        size_t at = recordsAt + static_cast<size_t>(ci) * sr3mesh::kChannelRecordSize;
        uint32_t elementCount = u32(at + 0x00);
        uint8_t sizeA = cb[at + 0x04];
        uint8_t sizeB = cb[at + 0x07];
        size_t stride = static_cast<size_t>(sizeA) + sizeB;
        cursor = alignUp(cursor, 16);
        size_t dataBytes = static_cast<size_t>(elementCount) * stride;
        if (cursor + dataBytes > gLength) {
            long long over = static_cast<long long>(cursor + dataBytes) - static_cast<long long>(gLength);
            return {"G_channel_overrun", "channel " + std::to_string(ci) + "/" + std::to_string(channelCount) +
                                              " overruns declared gLength by " + std::to_string(over) + " bytes"};
        }
        cursor += dataBytes;
    }

    cursor = alignUp(cursor, 4);
    if (cursor + 4 != gLength) {
        long long residual = static_cast<long long>(gLength) - static_cast<long long>(cursor + 4);
        return {"H_walk_residual", "walk ended at " + std::to_string(cursor + 4) + ", declared gLength=" +
                                        std::to_string(gLength) + ", residual=" + std::to_string(residual) +
                                        " bytes"};
    }
    uint32_t trailing = u32(segmentBase + cursor);
    if (trailing != checkValue) {
        return {"I_trailing_check_mismatch",
                "trailing=" + hex32(trailing) + " declaredCheck=" + hex32(checkValue)};
    }
    return {"J_would_validate", "all invariants satisfied - this IS a real inline block MeshBlock::parse would accept"};
}

void recordInlineCategory(const std::string& label, size_t pos, const InlineDiag& d) {
    ++g_inlineCategoryCounts[d.category];
    int& cap = g_inlineExampleCap[d.category];
    if (cap < kMaxExamplesPerCategory) {
        ++cap;
        char off[32];
        snprintf(off, sizeof(off), "0x%zx", pos);
        g_inlineExamples.push_back(label + " czn+" + off + ": " + d.category + " - " + d.detail);
    }
}

// --- .czh_pc pass-through (unchanged shape, kept only so this harness can
// walk the same archives without needing a second pass) ------------------

// Candidate scan: runs over EVERY `.czn_pc` file regardless of whether it
// has a non-empty `.gzn_pc` sibling - inline-mode blocks by construction
// need no g-file at all, so restricting this scan to gzn-paired files would
// systematically MISS exactly the population most likely to contain a real
// inline block (a zone whose only geometry is inline needs no non-empty
// .gzn_pc to exist at all). gIndexCount is empty (thus no g-backed match is
// ever found) when there is no non-empty .gzn_pc - that's correct: a
// g-backed candidate genuinely cannot be validated without one.
void scanCandidates(const std::string& label, const std::vector<uint8_t>& cb,
                     const std::unordered_map<uint32_t, int>& gIndexCount, long long& gBackedCandidatesOut,
                     long long& gBackedWithMatchOut, long long& inlineCandidatesOut) {
    long long gBackedCandidates = 0, gBackedWithMatch = 0, inlineCandidates = 0;
    for (size_t pos = 0; pos + 4 <= cb.size(); pos += 4) {
        if (rawReadU32LE(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
        if (pos + sr3mesh::kHeaderStart + 1 > cb.size()) continue;
        uint8_t flags = cb[pos + sr3mesh::kHeaderStart + sr3mesh::kFlagsOffset];
        bool wantsGFile = (flags & sr3mesh::kFlagBulkInGFile) != 0;
        if (wantsGFile) {
            ++gBackedCandidates;
            if (pos + sr3mesh::kCheckValueOffset + 4 <= cb.size()) {
                uint32_t cv = rawReadU32LE(cb.data(), pos + sr3mesh::kCheckValueOffset);
                if (gIndexCount.count(cv)) ++gBackedWithMatch;
            }
        } else {
            ++inlineCandidates;
            InlineDiag d = diagnoseInline(cb, pos);
            recordInlineCategory(label, pos, d);
        }
    }
    g_totalGBackedCandidates += gBackedCandidates;
    g_totalGBackedWithCheckMatch += gBackedWithMatch;
    g_totalInlineCandidates += inlineCandidates;
    gBackedCandidatesOut = gBackedCandidates;
    gBackedWithMatchOut = gBackedWithMatch;
    inlineCandidatesOut = inlineCandidates;
}

void checkCznGzn(const std::string& label, const std::vector<uint8_t>& cb, const EntryStatus& cznStatus,
                  bool hasGznSibling, const std::vector<uint8_t>& gb, const EntryStatus& gznStatusInfo) {
    std::unordered_map<uint32_t, int> gIndexCount;
    for (size_t p = 0; p + 4 <= gb.size(); p += 16) ++gIndexCount[rawReadU32LE(gb.data(), p)];

    long long gBackedCandidates = 0, gBackedWithMatch = 0, inlineCandidates = 0;
    scanCandidates(label, cb, gIndexCount, gBackedCandidates, gBackedWithMatch, inlineCandidates);

    if (!hasGznSibling || gb.empty()) return; // gap 2 pairing is scoped to non-empty .gzn_pc pairs, same as validate_zone.cpp

    ++g_pairsNonemptyGzn;

    sr3zone::ByteView gv(gb.data(), gb.size());
    auto blocks = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(cb.data(), cb.size()), gv);
    bool pass = !blocks.empty();

    const std::string cznSt = cznStatus.isRaw ? "Raw" : statusName(cznStatus.status);
    const std::string gznSt = gznStatusInfo.isRaw ? "Raw" : statusName(gznStatusInfo.status);

    if (pass) {
        ++g_pairsNonemptyGznValidated;
        ++g_gznStatusWhenPass[gznSt];
        ++g_cznStatusWhenPass[cznSt];
        ++g_gznDiagPrefixWhenPass[diagPrefix(gznStatusInfo.diagnostic)];
        ++g_cznDiagPrefixWhenPass[diagPrefix(cznStatus.diagnostic)];
    } else {
        ++g_pairsNonemptyGznZeroBlocks;
        ++g_gznStatusWhenFail[gznSt];
        ++g_cznStatusWhenFail[cznSt];
        ++g_gznDiagPrefixWhenFail[diagPrefix(gznStatusInfo.diagnostic)];
        ++g_cznDiagPrefixWhenFail[diagPrefix(cznStatus.diagnostic)];

        if (gBackedCandidates == 0) {
            if (inlineCandidates > 0) ++g_failOnlyInlineFlaggedCandidates;
            else ++g_failNoGBackedCandidateAtAll;
        } else if (gBackedWithMatch == 0) {
            ++g_failCandidateButNoCheckValueMatch;
        } else {
            ++g_failMatchButParseRejected;
        }
    }
}

void walk(const vpp::Container& c, const std::string& containerPath) {
    std::map<std::string, size_t> czn, gzn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czn_pc", stem)) czn[stem] = i;
        else if (stemFor(n, ".gzn_pc", stem)) gzn[stem] = i;
    }

    const size_t containerEntryCount = c.entries().size();
    const bool sharedStreamMode = c.header().isSharedStreamMode();

    for (const auto& kv : czn) {
        size_t i = kv.second;
        ++g_totalCznAttempted;
        g_allAttemptedStems.push_back(containerPath + " :: " + kv.first);
        // IMPORTANT: this try/catch is a fix for a real bug this investigation found in BOTH
        // this harness (before this line existed) and the original validate_zone.cpp: with no
        // per-entry guard here, an exception thrown ANYWHERE while processing one stem
        // (getEntry()'s decompressEntry() calls, or checkCznGzn's call chain -
        // scanCandidates/diagnoseInline/ZoneGeometry::locate/MeshBlock::parse) escapes this
        // whole walk() call - silently swallowed by the CALLER's
        // `try { walk(c.openNested(i)); } catch {}` one frame up - which abandons every
        // remaining sibling .czn_pc entry in this SAME container's map (whatever sorts after
        // the throwing stem) AND skips the recursion-into-nested-containers loop below
        // entirely for THIS container, losing every nested str2_pc under it too. That exactly
        // matches the shape measured here: whole nested containers missing, not just one file.
        try {
            EntryStatus cznStatus = getEntry(c, i);
            if (!cznStatus.ok) {
                FailedExtraction fe;
                fe.containerPath = containerPath;
                fe.entryName = c.entries()[i].name;
                fe.entryIndex = i;
                fe.containerEntryCount = containerEntryCount;
                fe.containerSharedStreamMode = sharedStreamMode;
                fe.status = cznStatus.isRaw ? "Raw" : statusName(cznStatus.status);
                fe.diagnostic = cznStatus.diagnostic;
                g_failedCzn.push_back(std::move(fe));
                continue;
            }

            auto git = gzn.find(kv.first);
            std::vector<uint8_t> gb;
            EntryStatus gznStatus;
            bool hasGznSibling = false;
            if (git != gzn.end()) {
                gznStatus = getEntry(c, git->second);
                hasGznSibling = true;
                gb = gznStatus.data;
            }

            checkCznGzn(containerPath + " : " + kv.first, cznStatus.data, cznStatus, hasGznSibling, gb,
                        gznStatus);
        } catch (const std::exception& ex) {
            ++g_cznGznThrew;
            g_cznGznThrowExamples.push_back(containerPath + " :: " + kv.first + " -> threw: " + ex.what());
        }
    }

    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                walk(c.openNested(i), containerPath + " > " + c.entries()[i].name);
            } catch (const std::exception&) {
            }
        }
    }
}

void printReport() {
    // --- Gap 1 -----------------------------------------------------------
    printf("\n=== GAP 1: .czn_pc entries that did not yield usable bytes ===\n");
    printf("total .czn_pc entries attempted (all distinct stems visited): %lld\n", g_totalCznAttempted);
    printf("total failed extractions: %zu\n", g_failedCzn.size());
    printf("checkCznGzn() calls that THREW (per-entry exception, now caught here - see comment at the "
           "call site): %lld\n\n",
           g_cznGznThrew);
    for (const auto& ex : g_cznGznThrowExamples) printf("  THROW: %s\n", ex.c_str());
    for (const auto& fe : g_failedCzn) {
        printf("  entryIndex=%zu of %zu-entry container, sharedStreamMode=%s, status=%s\n"
               "    entry: %s\n"
               "    container: %s\n"
               "    diagnostic: %s\n",
               fe.entryIndex, fe.containerEntryCount, fe.containerSharedStreamMode ? "true" : "false",
               fe.status.c_str(), fe.entryName.c_str(), fe.containerPath.c_str(), fe.diagnostic.c_str());
    }
    long long firstEntryFails = 0, laterEntryFails = 0;
    std::map<std::string, long long> statusTally;
    for (const auto& fe : g_failedCzn) {
        if (fe.entryIndex == 0) ++firstEntryFails; else ++laterEntryFails;
        ++statusTally[fe.status];
    }
    printf("\nsummary: entryIndex==0: %lld   entryIndex>0: %lld\n", firstEntryFails, laterEntryFails);
    printf("status breakdown:\n");
    for (const auto& kv : statusTally) printf("  %-32s %lld\n", kv.first.c_str(), kv.second);

    // --- Gap 2 -------------------------------------------------------------
    printf("\n=== GAP 2: non-empty .gzn_pc pairs, validated vs not ===\n");
    printf("non-empty .gzn_pc pairs scanned : %lld\n", g_pairsNonemptyGzn);
    printf("  >=1 validated Mesh block      : %lld\n", g_pairsNonemptyGznValidated);
    printf("  0 validated Mesh blocks       : %lld\n", g_pairsNonemptyGznZeroBlocks);

    printf("\n.gzn_pc DecodeStatus, PASS bucket (%lld pairs):\n", g_pairsNonemptyGznValidated);
    for (const auto& kv : g_gznStatusWhenPass) printf("  %-32s %lld\n", kv.first.c_str(), kv.second);
    printf(".gzn_pc DecodeStatus, FAIL bucket (%lld pairs):\n", g_pairsNonemptyGznZeroBlocks);
    for (const auto& kv : g_gznStatusWhenFail) printf("  %-32s %lld\n", kv.first.c_str(), kv.second);

    printf("\n.czn_pc DecodeStatus, PASS bucket (%lld pairs):\n", g_pairsNonemptyGznValidated);
    for (const auto& kv : g_cznStatusWhenPass) printf("  %-32s %lld\n", kv.first.c_str(), kv.second);
    printf(".czn_pc DecodeStatus, FAIL bucket (%lld pairs):\n", g_pairsNonemptyGznZeroBlocks);
    for (const auto& kv : g_cznStatusWhenFail) printf("  %-32s %lld\n", kv.first.c_str(), kv.second);

    printf("\n--- finer signal: does the archive-level flags-bit-0x2 prediction AGREE with the\n"
           "    offset-heuristic recovery for THIS entry (src/container.cpp ~L154/160/183)? ---\n");
    printf(".gzn_pc diagnostic tag, PASS bucket (%lld pairs):\n", g_pairsNonemptyGznValidated);
    for (const auto& kv : g_gznDiagPrefixWhenPass) printf("  %-40s %lld\n", kv.first.c_str(), kv.second);
    printf(".gzn_pc diagnostic tag, FAIL bucket (%lld pairs):\n", g_pairsNonemptyGznZeroBlocks);
    for (const auto& kv : g_gznDiagPrefixWhenFail) printf("  %-40s %lld\n", kv.first.c_str(), kv.second);
    printf(".czn_pc diagnostic tag, PASS bucket (%lld pairs):\n", g_pairsNonemptyGznValidated);
    for (const auto& kv : g_cznDiagPrefixWhenPass) printf("  %-40s %lld\n", kv.first.c_str(), kv.second);
    printf(".czn_pc diagnostic tag, FAIL bucket (%lld pairs):\n", g_pairsNonemptyGznZeroBlocks);
    for (const auto& kv : g_cznDiagPrefixWhenFail) printf("  %-40s %lld\n", kv.first.c_str(), kv.second);

    printf("\nFAIL bucket breakdown by how far the g-backed anchor scan got:\n");
    printf("  0 g-backed candidates, 0 inline-flagged candidates either : %lld\n", g_failNoGBackedCandidateAtAll);
    printf("  0 g-backed candidates, but >=1 inline-flagged candidate   : %lld\n",
           g_failOnlyInlineFlaggedCandidates);
    printf("  >=1 g-backed candidate, but 0 check-value matches in gzn  : %lld\n",
           g_failCandidateButNoCheckValueMatch);
    printf("  >=1 check-value match, but MeshBlock::parse rejected all  : %lld\n", g_failMatchButParseRejected);

    // --- Gap 3 ---------------------------------------------------------
    printf("\n=== GAP 3: inline-flagged candidate classification ===\n");
    printf("g-backed-flagged candidates (u32==9, flags bit SET)   : %lld\n", g_totalGBackedCandidates);
    printf("  of which check-value found in paired .gzn_pc        : %lld\n", g_totalGBackedWithCheckMatch);
    printf("inline-flagged candidates   (u32==9, flags bit CLEAR) : %lld\n", g_totalInlineCandidates);
    printf("  => sample-estimate cross-check: inline / (inline+g-backed) = %.2f%%"
           " (spec Sec7.3 sample: ~23%% = 1,030/4,478)\n",
           (g_totalGBackedCandidates + g_totalInlineCandidates) > 0
               ? 100.0 * static_cast<double>(g_totalInlineCandidates) /
                     static_cast<double>(g_totalGBackedCandidates + g_totalInlineCandidates)
               : 0.0);

    printf("\ninline-candidate outcome categories (in first-failing-invariant order):\n");
    printf("  A_header_oob                 : too small to even reach the header\n");
    printf("  B_multistream_flag_set        : flags bit 2 set, out of scope\n");
    printf("  C_channel_records_oob        : channel record array runs past content end\n");
    printf("  D_segment_oob / D2_segment_lt4: declared gLength runs past content end\n");
    printf("  E_leading_check_mismatch      : segment does NOT open on the declared check value"
           " (strong evidence: NOT a real block)\n");
    printf("  F_index_overrun               : leading check OK, index buffer overruns gLength\n");
    printf("  G_channel_overrun             : leading check OK, channel data overruns gLength\n");
    printf("  H_walk_residual               : leading check OK, walk ends short/long of gLength\n");
    printf("  I_trailing_check_mismatch     : leading check OK, walk exact, trailing bookend wrong\n");
    printf("  J_would_validate              : ALL invariants satisfied - a real inline block\n\n");
    long long leadingCheckOkTotal = 0;
    for (const auto& kv : g_inlineCategoryCounts) {
        printf("  %-30s %lld\n", kv.first.c_str(), kv.second);
        if (kv.first[0] == 'F' || kv.first[0] == 'G' || kv.first[0] == 'H' || kv.first[0] == 'I' ||
            kv.first[0] == 'J')
            leadingCheckOkTotal += kv.second;
    }
    printf("\n  of %lld total inline-flagged candidates, %lld (%.4f%%) passed the leading-check-value test"
           " (categories F/G/H/I/J) - these are the only ones that could plausibly be real blocks;\n"
           "  everything in category E is a coincidental u32==9 with no structural relationship to\n"
           "  what follows, by construction (the declared check value does not match the segment start).\n",
           g_totalInlineCandidates, leadingCheckOkTotal,
           g_totalInlineCandidates > 0
               ? 100.0 * static_cast<double>(leadingCheckOkTotal) / static_cast<double>(g_totalInlineCandidates)
               : 0.0);

    printf("\nexample near-misses (up to %d per category):\n", kMaxExamplesPerCategory);
    for (const auto& ex : g_inlineExamples) printf("  %s\n", ex.c_str());

    printf("\n=== ALL_ATTEMPTED_STEMS (%zu) ===\n", g_allAttemptedStems.size());
    for (const auto& s : g_allAttemptedStems) printf("STEM: %s\n", s.c_str());
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
            printf("\n############ running totals after %s ############\n", argv[i]);
            printReport();
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n############ FINAL REPORT (all archives given on the command line) ############\n");
    printReport();
    return 0;
}
