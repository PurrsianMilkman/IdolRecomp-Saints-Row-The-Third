// Follow-up diagnostic for HANDOFF.md Sec9.55.1 Gap 2's still-open remainder:
// of the 70 non-empty-.gzn_pc pairs where sr3zone::ZoneGeometry::locate()
// finds zero validated Mesh blocks, 52 (74%) have NO g-backed-flagged anchor
// candidate (a 4-aligned u32==9 whose flags-byte peek says "bulk in g-file")
// findable anywhere in the .czn_pc content at all. diag_zone_gaps.cpp already
// COUNTS this bucket; this tool IDENTIFIES its members explicitly - which
// archive, which container path, which stem - and gathers, per member, the
// facts needed to tell "genuinely too short / no geometry" apart from
// "encoded differently" apart from "scan artifact":
//
//   - exact .czn_pc and .gzn_pc sizes, vs the minimum bytes a bare Mesh
//     sub-block header needs (kHeaderStart + kHeaderSize, 0x80 bytes);
//   - an UNALIGNED rescan of the .czn_pc content for u32==9 at every byte
//     offset (not just 4-aligned) - diagnostic-only, does not touch
//     src/zone_geometry.cpp - to see whether a plausible anchor exists but
//     sits off the 4-alignment production locate() assumes;
//   - the .czn_pc's own leading {id, length} peek (first two u32 words) -
//     spec-zone-data-format.md Sec2's top-level record - to check whether
//     the 52 skew toward one of the three known leading ids (0x80002233 /
//     0x80002237 / 0x80002234) vs the population at large;
//   - the paired .czh_pc's SR3Z recordCount() (spec-ctorless-types.md Sec5),
//     when a same-stem .czh_pc is found in the same container - HANDOFF
//     Sec9.55's own confirmed record-count data, to check correlation with
//     unusually large/small top-level record chains;
//   - a hex preview of the first bytes of .gzn_pc, to characterize what IS
//     there when no Mesh anchor can be found in the .czn_pc at all.
//
// Read-only instrumentation, same as diag_zone_gaps.cpp: reuses
// sr3zone::ZoneGeometry::locate() as the authoritative pass/fail signal and
// sr3zone::ZoneHeader::parse() for the .czh_pc side, duplicates only the
// candidate-side scan (the same kind of independent-replica control
// validate_groupsearch.cpp/diag_zone_gaps.cpp already use).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
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

uint32_t rawReadU32LE(const uint8_t* data, size_t pos) {
    return static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
}

std::string hex32(uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X", v);
    return buf;
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

// --- Bucket classification, mirroring diag_zone_gaps.cpp exactly ----------
struct CandidateScan {
    long long gBackedCandidates = 0;
    long long inlineCandidates = 0;
};

CandidateScan scanCandidates4Aligned(const std::vector<uint8_t>& cb) {
    CandidateScan s;
    for (size_t pos = 0; pos + 4 <= cb.size(); pos += 4) {
        if (rawReadU32LE(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
        if (pos + sr3mesh::kHeaderStart + 1 > cb.size()) continue;
        uint8_t flags = cb[pos + sr3mesh::kHeaderStart + sr3mesh::kFlagsOffset];
        bool wantsGFile = (flags & sr3mesh::kFlagBulkInGFile) != 0;
        if (wantsGFile) ++s.gBackedCandidates; else ++s.inlineCandidates;
    }
    return s;
}

// Diagnostic-only unaligned rescan: every BYTE offset, not just 4-aligned.
// Never used to change what locate() does - purely to answer "is there a
// plausible anchor sitting off the alignment the production scan assumes".
struct UnalignedHit {
    size_t pos;
    int mod4;
    bool flagsByteReachable;
    uint8_t flagsByte;
    bool wantsGFile;
};

std::vector<UnalignedHit> scanUnaligned(const std::vector<uint8_t>& cb, size_t cap) {
    std::vector<UnalignedHit> hits;
    for (size_t pos = 0; pos + 4 <= cb.size() && hits.size() < cap; ++pos) {
        if (rawReadU32LE(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
        UnalignedHit h;
        h.pos = pos;
        h.mod4 = static_cast<int>(pos % 4);
        h.flagsByteReachable = (pos + sr3mesh::kHeaderStart + 1 <= cb.size());
        h.flagsByte = h.flagsByteReachable ? cb[pos + sr3mesh::kHeaderStart + sr3mesh::kFlagsOffset] : 0;
        h.wantsGFile = h.flagsByteReachable && (h.flagsByte & sr3mesh::kFlagBulkInGFile) != 0;
        hits.push_back(h);
    }
    return hits;
}

std::string hexPreview(const std::vector<uint8_t>& b, size_t n) {
    n = std::min(n, b.size());
    std::string out;
    char buf[4];
    for (size_t i = 0; i < n; ++i) {
        snprintf(buf, sizeof(buf), "%02X ", b[i]);
        out += buf;
        if (i % 16 == 15) out += " ";
    }
    return out;
}

// --- Per-member record for the bucket under investigation ------------------
struct Member {
    std::string containerPath;
    std::string stem;
    size_t cznSize = 0;
    size_t gznSize = 0;
    uint32_t leadingId = 0;
    uint32_t leadingLen = 0;
    bool czhFound = false;
    bool czhParsedOk = false;
    uint16_t czhRecordCount = 0;
    std::string czhError;
    size_t unalignedHitCount = 0; // total, capped scan below
    std::vector<UnalignedHit> unalignedSample; // first few
    std::string gznPreview;
};

std::vector<Member> g_bucket52;   // 0 g-backed, 0 inline candidates at all
std::vector<std::pair<std::string,std::string>> g_bucket5;  // candidate but no check-value match
std::vector<std::pair<std::string,std::string>> g_bucket13; // match but MeshBlock::parse rejected

long long g_pairsNonemptyGzn = 0;
long long g_pairsValidated = 0;
long long g_pairsZeroBlocks = 0;

// Population-wide leading-id tally (for the correlation check), gathered for
// EVERY .czn_pc visited regardless of pass/fail, and separately for the
// bucket-52 subset (see printReport).
std::map<uint32_t, long long> g_leadingIdAllCzn;
long long g_leadingIdAllCznTotal = 0;

void checkCznGzn(const std::string& containerPath, const std::string& stem, const std::vector<uint8_t>& cb,
                 bool hasGznSibling, const std::vector<uint8_t>& gb,
                 const std::map<std::string, std::vector<uint8_t>>& czhBytesByStem) {
    // Population-wide leading-id tally, independent of gzn pairing.
    if (cb.size() >= 4) {
        ++g_leadingIdAllCznTotal;
        ++g_leadingIdAllCzn[rawReadU32LE(cb.data(), 0)];
    }

    if (!hasGznSibling || gb.empty()) return; // scope: non-empty .gzn_pc pairs only, matching validate_zone.cpp

    ++g_pairsNonemptyGzn;

    sr3zone::ByteView gv(gb.data(), gb.size());
    auto blocks = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(cb.data(), cb.size()), gv);
    if (!blocks.empty()) { ++g_pairsValidated; return; }

    ++g_pairsZeroBlocks;

    CandidateScan scan = scanCandidates4Aligned(cb);

    // Recompute the check-value-match count for the "candidate but no match"
    // vs "match but parse rejected" split (same predicate diag_zone_gaps.cpp
    // uses), only needed when gBackedCandidates > 0.
    long long gBackedWithMatch = 0;
    if (scan.gBackedCandidates > 0) {
        std::unordered_map<uint32_t, int> gIndexCount;
        for (size_t p = 0; p + 4 <= gb.size(); p += 16) ++gIndexCount[rawReadU32LE(gb.data(), p)];
        for (size_t pos = 0; pos + 4 <= cb.size(); pos += 4) {
            if (rawReadU32LE(cb.data(), pos) != sr3mesh::kMeshVersion) continue;
            if (pos + sr3mesh::kHeaderStart + 1 > cb.size()) continue;
            uint8_t flags = cb[pos + sr3mesh::kHeaderStart + sr3mesh::kFlagsOffset];
            if ((flags & sr3mesh::kFlagBulkInGFile) == 0) continue;
            if (pos + sr3mesh::kCheckValueOffset + 4 > cb.size()) continue;
            uint32_t cv = rawReadU32LE(cb.data(), pos + sr3mesh::kCheckValueOffset);
            if (gIndexCount.count(cv)) ++gBackedWithMatch;
        }
    }

    if (scan.gBackedCandidates == 0 && scan.inlineCandidates == 0) {
        // --- THE BUCKET UNDER INVESTIGATION (the 52) ---
        Member m;
        m.containerPath = containerPath;
        m.stem = stem;
        m.cznSize = cb.size();
        m.gznSize = gb.size();
        if (cb.size() >= 4) m.leadingId = rawReadU32LE(cb.data(), 0);
        if (cb.size() >= 8) m.leadingLen = rawReadU32LE(cb.data(), 4);

        auto czhIt = czhBytesByStem.find(stem);
        if (czhIt != czhBytesByStem.end()) {
            m.czhFound = true;
            try {
                sr3zone::ZoneHeader h =
                    sr3zone::ZoneHeader::parse(sr3zone::ByteView(czhIt->second.data(), czhIt->second.size()));
                m.czhParsedOk = true;
                m.czhRecordCount = h.recordCount();
            } catch (const std::exception& ex) {
                m.czhParsedOk = false;
                m.czhError = ex.what();
            }
        }

        auto unaligned = scanUnaligned(cb, 200000); // cap just to bound pathological inputs
        m.unalignedHitCount = unaligned.size();
        for (size_t i = 0; i < unaligned.size() && i < 6; ++i) m.unalignedSample.push_back(unaligned[i]);

        m.gznPreview = hexPreview(gb, 64);

        g_bucket52.push_back(std::move(m));
    } else if (scan.gBackedCandidates > 0 && gBackedWithMatch == 0) {
        g_bucket5.push_back({containerPath, stem});
    } else if (scan.gBackedCandidates > 0 && gBackedWithMatch > 0) {
        g_bucket13.push_back({containerPath, stem});
    }
    // (scan.gBackedCandidates == 0 && scan.inlineCandidates > 0) is a
    // different, not-yet-named bucket (inline-flagged candidates only, no
    // g-backed one) - out of scope for this specific 52-file task, not
    // tallied here to keep this tool's purpose narrow.
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

    // Only decode .czh_pc bytes for stems that actually appear as a .czn_pc
    // in this SAME container - avoids paying decode cost for the ~2,342
    // zero-record .czh_pc files this diagnostic doesn't need at all when
    // their .czn_pc sibling isn't even in the fail bucket. Decoded lazily,
    // keyed by stem, only for stems this container's czn map has.
    std::map<std::string, std::vector<uint8_t>> czhBytesByStem;
    for (const auto& kv : czn) {
        auto it = czh.find(kv.first);
        if (it == czh.end()) continue;
        std::vector<uint8_t> b;
        if (getEntry(c, it->second, b) && !b.empty()) czhBytesByStem[kv.first] = std::move(b);
    }

    for (const auto& kv : czn) {
        try {
            std::vector<uint8_t> cb;
            if (!getEntry(c, kv.second, cb)) continue;
            auto git = gzn.find(kv.first);
            std::vector<uint8_t> gb;
            bool hasGznSibling = false;
            if (git != gzn.end()) hasGznSibling = getEntry(c, git->second, gb);
            checkCznGzn(containerPath, kv.first, cb, hasGznSibling, gb, czhBytesByStem);
        } catch (const std::exception&) {
            continue; // one bad stem must not take out its siblings (Sec9.55.1 lesson)
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
    printf("\n=== POPULATION ===\n");
    printf("non-empty .gzn_pc pairs scanned : %lld\n", g_pairsNonemptyGzn);
    printf("  >=1 validated Mesh block      : %lld\n", g_pairsValidated);
    printf("  0 validated Mesh blocks       : %lld\n", g_pairsZeroBlocks);
    printf("    bucket 'no g-backed or inline candidate at all' : %zu\n", g_bucket52.size());
    printf("    bucket 'candidate, no check-value match'        : %zu\n", g_bucket5.size());
    printf("    bucket 'check-value match, parse rejected'      : %zu\n", g_bucket13.size());

    printf("\n=== leading-id tally, ALL .czn_pc visited (%lld total) ===\n", g_leadingIdAllCznTotal);
    for (const auto& kv : g_leadingIdAllCzn) {
        if (kv.second >= 5) // suppress the long tail of one-off ids for readability
            printf("  %s : %lld\n", hex32(kv.first).c_str(), kv.second);
    }
    long long taggedSmall = 0;
    for (const auto& kv : g_leadingIdAllCzn) if (kv.second < 5) taggedSmall += kv.second;
    printf("  (ids with <5 occurrences, summed) : %lld\n", taggedSmall);

    printf("\n=== leading-id tally, BUCKET-52 SUBSET (%zu files) ===\n", g_bucket52.size());
    std::map<uint32_t, long long> leadingIdBucket52;
    for (const auto& m : g_bucket52) ++leadingIdBucket52[m.leadingId];
    for (const auto& kv : leadingIdBucket52) printf("  %s : %lld\n", hex32(kv.first).c_str(), kv.second);

    printf("\n=== BUCKET 52 MEMBERS (full detail) ===\n");
    int idx = 0;
    for (const auto& m : g_bucket52) {
        ++idx;
        printf("\n--- #%d  %s :: %s ---\n", idx, m.containerPath.c_str(), m.stem.c_str());
        printf("  czn size = %zu bytes   gzn size = %zu bytes   (Mesh header alone needs >= %zu bytes)\n",
               m.cznSize, m.gznSize, sr3mesh::kHeaderStart + sr3mesh::kHeaderSize);
        printf("  leading czn {id,len} peek: id=%s len=%u (%s)\n", hex32(m.leadingId).c_str(), m.leadingLen,
               (m.leadingId == 0x80002233 || m.leadingId == 0x80002237 || m.leadingId == 0x80002234)
                   ? "one of the three known leading ids"
                   : "NOT one of the three known leading ids");
        if (m.czhFound) {
            if (m.czhParsedOk)
                printf("  paired .czh_pc: parsed OK, recordCount=%u\n", m.czhRecordCount);
            else
                printf("  paired .czh_pc: FOUND but failed to parse: %s\n", m.czhError.c_str());
        } else {
            printf("  paired .czh_pc: NOT FOUND in this container\n");
        }
        printf("  unaligned rescan (every byte offset, not just 4-aligned) for u32==9: %zu hit(s)%s\n",
               m.unalignedHitCount, m.unalignedHitCount >= 200000 ? " (capped)" : "");
        for (const auto& h : m.unalignedSample) {
            printf("    czn+0x%zx (mod4=%d)%s\n", h.pos, h.mod4,
                   h.flagsByteReachable
                       ? (h.wantsGFile ? "  flags peek: g-backed" : "  flags peek: inline/other")
                       : "  (too close to end to reach flags byte)");
        }
        printf("  gzn preview (first %zu bytes): %s\n", std::min<size_t>(64, m.gznSize), m.gznPreview.c_str());
    }

    printf("\n=== BUCKET 5 (candidate found, no check-value match in .gzn_pc) - stems only ===\n");
    for (const auto& p : g_bucket5) printf("  %s :: %s\n", p.first.c_str(), p.second.c_str());

    printf("\n=== BUCKET 13 (check-value match found, MeshBlock::parse rejected all) - stems only ===\n");
    for (const auto& p : g_bucket13) printf("  %s :: %s\n", p.first.c_str(), p.second.c_str());
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
