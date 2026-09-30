// Real-data validation for `sr3clmesh::LevelMesh` - the `.clmesh_pc`
// "Level_Mesh" container and the sub-parser chain spec-physics-format.md
// Sec4.2 / Sec4.4.6 characterise.
//
// WHAT THE GO/NO-GO TEST IS, AND WHY THIS ONE. This format has exactly one
// structural landmark a walk can be graded against: `FUN_00749e80`'s
// trailing nested group is the LAST thing in the file, so a correct walk
// ends EXACTLY at end-of-file, to the byte. Everything here is built on
// that. "Parses without throwing" would prove nothing - the tail counts are
// zero in most files, so a do-nothing walk would sail past.
//
// WHAT CHANGED (2026-09-13). The first version of this harness scored a
// SEARCHED tail: the middle region had no known size, so the tail's start
// was found by trying every 8-aligned position and keeping the ones that
// landed on EOF. spec-physics-format.md Sec4.4.6 sized the middle, so the
// whole file is now one computed cursor and there is nothing to search for
// and nothing to disambiguate. The ambiguity distribution this harness used
// to print is gone because the quantity it measured no longer exists.
//
// The controls are what make the landing rate mean anything, and they are
// now per-TERM rather than global: each of the FIFTEEN disables or perturbs
// exactly one confirmed step of the walk, so each row is that step's own
// evidence (Sec4.4.6(e) scores ten of them independently). A walk that
// cannot be broken by removing one of its terms is not measuring the file.
// Two of the fifteen are not Sec4.4.6's - they are the two places its prose
// is ambiguous and this reader had to settle the question by measurement:
// the hull gate's bit, and whether the do-nothing branch aligns.
//
// Usage:  validate_clmesh <archive.vpp_pc> [more archives...]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3clmesh/level_mesh.h"
#include "sr3geometry/material_block.h"
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

// ---------------------------------------------------------------- counters
long long g_pairs = 0, g_paired = 0, g_parsed = 0, g_parseFailed = 0;

long long g_land = 0, g_noLand = 0;
long long g_headOk = 0, g_middleOk = 0, g_tailOk = 0;

// Controls. Each must FAIL to land on EOF (or land on far fewer entries).
long long g_ctrlMidPlus4 = 0, g_ctrlMidPlus16 = 0;
long long g_ctrlTailPlus16 = 0, g_ctrlTailMinus16 = 0;
long long g_ctrlNoNul = 0, g_ctrlOneGroup = 0, g_ctrlNoLookup = 0, g_ctrlMeshRound16 = 0;
long long g_ctrlGatedZero = 0, g_ctrlNoHeadIdx = 0, g_ctrlNoHeadIdxDiscriminating = 0;
long long g_ctrl70Plus1 = 0, g_ctrlFcCount4 = 0, g_ctrlProseOrder = 0;
long long g_ctrlHullMask02 = 0, g_ctrlNoneBranchAligns = 0;
long long g_specLiteralExact = 0;

// The `+0xb8` record's own census (spec-physics-format.md Sec4.4.6(d)).
long long g_recMatSetZero = 0, g_recMatSetMinus1 = 0;
long long g_recLookupZero = 0, g_recGroup1Zero = 0;
long long g_recGate2Zero = 0, g_recGate2Minus1 = 0;
long long g_rec24NonZero = 0, g_rec34NonZero = 0, g_rec24Equals34 = 0;
long long g_recTrailerZero = 0;
std::map<uint32_t, long long> g_recGroupACount, g_recGroupBCount, g_materialCount;
long long g_twoRenderGroups = 0;
long long g_middleAtBodyStart = 0, g_middleAfterRealHead = 0;

double g_middleTotal = 0.0;
size_t g_middleMin = static_cast<size_t>(-1), g_middleMax = 0;
std::vector<size_t> g_middleLengths;
std::map<size_t, long long> g_middleLengthHistogram;

// The gated `+0x78`/`+0x7c` pair, now three real branches.
long long g_sentinelFiles = 0, g_realPairFiles = 0;
long long g_pairOneValue = 0, g_pairTwoValues = 0;
long long g_branchNone = 0, g_branchList = 0, g_branchHull = 0;
long long g_hullVertices = 0, g_hullIndices = 0, g_hullBlobBytes = 0;
std::map<size_t, long long> g_listBlockSizes;

long long g_refAFiles = 0, g_refAEntries = 0, g_refAResolved = 0, g_refAFullyResolved = 0;
long long g_meshAnchorAtStart = 0;
long long g_headIndexListBlocks = 0, g_headIndexListElements = 0;
long long g_boundsComputed = 0, g_boundsFinite = 0;
double g_radiusMin = 1e30, g_radiusMax = -1e30;

std::map<uint32_t, long long> g_countFc, g_count88, g_countA8, g_count98, g_count70,
    g_countD8, g_countRefA, g_slotB8;
long long g_threeCountsEqual = 0;
std::map<std::string, long long> g_failSignature;
std::vector<std::string> g_failExamples;
long long g_group88Entries = 0, g_group88InnerTotal = 0;
long long g_groupFcEntries = 0, g_groupFcInnerTotal = 0;

// A named sample of the 129 entries the SEARCHED model could not place at
// all (HANDOFF Sec9.59 enumerated them as an `~l1` highway-signage set).
// Under the computed walk they should land like everything else; this counts
// them by name so the claim is checkable rather than an aggregate assertion.
const char* kPreviouslyUnplaceable[] = {
    "hwy_sm_green~l1",  "hwy_sm_orange~l1",   "hwy_sm_blue~l1",     "hwy_sm_yellow~l1",
    "hwy_red_ls~l1",    "hwy_green_sa_r~l1",  "terminal_sign_a~l1", "terminal_sign_c~l1",
};
long long g_prevUnplaceableSeen = 0, g_prevUnplaceableLand = 0;
std::set<std::string> g_prevUnplaceableStems;

bool landsWith(const vpp::ByteView& content, size_t headerOffset, const sr3clmesh::WalkOptions& opt) {
    try {
        sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parseAt(content, headerOffset, opt);
        return m.landsOnEof();
    } catch (const std::exception&) {
        return false;
    }
}

// Runs the MIDDLE from a deliberately wrong start and then the TAIL from
// wherever that lands. Both shifts must cost the exact landing.
bool landsWithMiddleShift(const vpp::ByteView& content, const sr3clmesh::LevelMesh& m,
                          const uint32_t* hw, size_t shift) {
    sr3clmesh::MiddleLayout mid =
        sr3clmesh::walkMiddle(content, m.headEnd() + shift, content.size());
    if (!mid.ok) return false;
    sr3clmesh::TailWalk t = sr3clmesh::walkTail(content, mid.end, hw, content.size());
    return t.ok && t.end == content.size();
}

bool landsWithTailShift(const vpp::ByteView& content, const sr3clmesh::LevelMesh& m,
                        const uint32_t* hw, long long shift) {
    const long long start = static_cast<long long>(m.tailStart()) + shift;
    if (start < 0 || static_cast<size_t>(start) > content.size()) return false;
    sr3clmesh::TailWalk t =
        sr3clmesh::walkTail(content, static_cast<size_t>(start), hw, content.size());
    return t.ok && t.end == content.size();
}

void check(const std::string& name, const std::vector<uint8_t>& cb, const std::vector<uint8_t>& gb) {
    ++g_pairs;
    if (!gb.empty()) ++g_paired;
    vpp::ByteView content(cb.data(), cb.size());
    vpp::ByteView gcontent(gb.data(), gb.size());

    sr3clmesh::LevelMesh lm;
    try {
        lm = sr3clmesh::LevelMesh::parse(content);
    } catch (const std::exception&) {
        ++g_parseFailed;
        return;
    }
    ++g_parsed;

    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = content.readU32LE(lm.headerOffset() + i * 4);
    }
    const uint32_t* hw = words.data();

    // --- header field population ------------------------------------------
    const uint32_t cRefA = lm.headerField(sr3clmesh::kCountRefA);
    const uint32_t cRefB = lm.headerField(sr3clmesh::kCountRefB);
    const uint32_t c70 = lm.headerField(sr3clmesh::kCount70);
    g_countRefA[cRefA]++;
    g_count70[c70]++;
    g_count88[lm.headerField(sr3clmesh::kCount88)]++;
    g_count98[lm.headerField(sr3clmesh::kCount98)]++;
    g_countA8[lm.headerField(sr3clmesh::kCountA8)]++;
    g_countD8[lm.headerField(sr3clmesh::kCountD8)]++;
    g_countFc[lm.headerField(sr3clmesh::kCountFc)]++;
    g_slotB8[lm.headerField(sr3clmesh::kOutSingleRefB8)]++;
    if (cRefA == cRefB && cRefB == c70) ++g_threeCountsEqual;

    if (lm.pairIsSentinel()) {
        ++g_sentinelFiles;
    } else {
        ++g_realPairFiles;
        if (lm.pairSecond() == sr3clmesh::kPairSentinel) ++g_pairOneValue;
        else ++g_pairTwoValues;
    }

    for (const char* s : kPreviouslyUnplaceable) {
        if (name == s) {
            ++g_prevUnplaceableSeen;
            g_prevUnplaceableStems.insert(name);
            if (lm.landsOnEof()) ++g_prevUnplaceableLand;
            break;
        }
    }

    if (lm.headEnd() != 0) ++g_headOk;
    if (lm.middle().ok) ++g_middleOk;
    if (lm.walkComplete()) ++g_tailOk;

    // --- the go/no-go test: does the computed walk land exactly on EOF? ---
    if (!lm.landsOnEof()) {
        ++g_noLand;
        char sig[220];
        snprintf(sig, sizeof(sig),
                 "head=%d mid=%d tail=%d end=%zu size=%zu c48=%u c58=%u c70=%u pair=%d flags=%02x "
                 "c88=%u c98=%u ca8=%u cd8=%u ce8=%u",
                 lm.headEnd() != 0 ? 1 : 0, lm.middle().ok ? 1 : 0, lm.walkComplete() ? 1 : 0,
                 lm.tailEnd(), cb.size(), cRefA, cRefB, c70, lm.pairFirst(),
                 lm.headerField(sr3clmesh::kFlagsByte) & 0xFFu,
                 lm.headerField(sr3clmesh::kCount88), lm.headerField(sr3clmesh::kCount98),
                 lm.headerField(sr3clmesh::kCountA8), lm.headerField(sr3clmesh::kCountD8),
                 lm.headerField(sr3clmesh::kCountE8));
        g_failSignature[sig]++;
        if (g_failExamples.size() < 12) g_failExamples.push_back(name + "  [" + sig + "]");
        return;
    }
    ++g_land;

    // --- what the walk located -------------------------------------------
    g_group88Entries += static_cast<long long>(lm.group88().size());
    for (const auto& e : lm.group88()) g_group88InnerTotal += static_cast<long long>(e.innerCount);
    g_groupFcEntries += static_cast<long long>(lm.groupFc().size());
    for (const auto& e : lm.groupFc()) g_groupFcInnerTotal += static_cast<long long>(e.innerCount);
    g_headIndexListBlocks += static_cast<long long>(lm.headIndexLists().size());
    for (const auto& b : lm.headIndexLists()) g_headIndexListElements += static_cast<long long>(b.count);

    const sr3clmesh::MiddleLayout& mid = lm.middle();
    const size_t midLen = mid.length();
    g_middleTotal += static_cast<double>(midLen);
    g_middleMin = std::min(g_middleMin, midLen);
    g_middleMax = std::max(g_middleMax, midLen);
    g_middleLengths.push_back(midLen);
    g_middleLengthHistogram[midLen]++;
    g_materialCount[mid.materialCount]++;
    if (mid.renderGroupCount == 2) ++g_twoRenderGroups;
    if (lm.middleOffset() == lm.bodyStart()) ++g_middleAtBodyStart;
    else ++g_middleAfterRealHead;

    const size_t R = mid.recordOffset;
    auto rec = [&](size_t off) { return content.readU32LE(R + off); };
    if (rec(sr3clmesh::kMidMatSetSlot) == 0) ++g_recMatSetZero;
    if (rec(sr3clmesh::kMidMatSetSlot) == 0xFFFFFFFFu) ++g_recMatSetMinus1;
    if (rec(sr3clmesh::kMidLookupSlot) == 0) ++g_recLookupZero;
    if (rec(sr3clmesh::kMidGroup1Slot) == 0) ++g_recGroup1Zero;
    if (rec(sr3clmesh::kMidGroup2Gate) == 0) ++g_recGate2Zero;
    if (rec(sr3clmesh::kMidGroup2Gate) == 0xFFFFFFFFu) ++g_recGate2Minus1;
    if (rec(0x24) != 0) ++g_rec24NonZero;
    if (rec(0x34) != 0) ++g_rec34NonZero;
    if (rec(0x24) == rec(0x34)) ++g_rec24Equals34;
    if (rec(sr3clmesh::kMidTrailerCount) == 0) ++g_recTrailerZero;
    g_recGroupACount[rec(sr3clmesh::kMidGroupACount)]++;
    g_recGroupBCount[rec(sr3clmesh::kMidGroupBCount)]++;

    const sr3clmesh::GatedPair& gp = lm.gatedPair();
    switch (gp.branch) {
        case sr3clmesh::GatedPairBranch::None: ++g_branchNone; break;
        case sr3clmesh::GatedPairBranch::ListBlock:
            ++g_branchList;
            g_listBlockSizes[gp.blockSize]++;
            break;
        case sr3clmesh::GatedPairBranch::CollisionHull:
            ++g_branchHull;
            g_hullVertices += gp.hull.vertexCount;
            g_hullIndices += gp.hull.indexCount;
            g_hullBlobBytes += gp.hull.blobLength;
            break;
    }

    // ---- CONTROLS. Each must FAIL to land on EOF. -----------------------
    if (landsWithMiddleShift(content, lm, hw, 4)) ++g_ctrlMidPlus4;
    if (landsWithMiddleShift(content, lm, hw, 16)) ++g_ctrlMidPlus16;
    if (landsWithTailShift(content, lm, hw, 16)) ++g_ctrlTailPlus16;
    if (landsWithTailShift(content, lm, hw, -16)) ++g_ctrlTailMinus16;

    {
        sr3clmesh::WalkOptions o;
        o.nameTableNulByte = false;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlNoNul;
    }
    {
        sr3clmesh::WalkOptions o;
        o.secondRenderGroup = false;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlOneGroup;
    }
    {
        sr3clmesh::WalkOptions o;
        o.lookupTableArray = false;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlNoLookup;
    }
    {
        sr3clmesh::WalkOptions o;
        o.meshStepRoundUp16 = true;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlMeshRound16;
    }
    {
        sr3clmesh::WalkOptions o;
        o.gatedPairZeroCost = true;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlGatedZero;
    }
    {
        // Only files with count(+0x58) > 0 can discriminate this term at all:
        // with no array-2 entries there are no index-list blocks to omit.
        sr3clmesh::WalkOptions o;
        o.headIndexLists = false;
        if (cRefB > 0) {
            ++g_ctrlNoHeadIdxDiscriminating;
            if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlNoHeadIdx;
        }
    }
    {
        sr3clmesh::WalkOptions o;
        o.trailingGroupProseOrder = true;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlProseOrder;
    }
    {
        // The two ambiguities in Sec4.4.6's own prose that this reader settled
        // by measurement rather than by reading: "bit 2" of the flags byte as
        // the literal mask 0x02 instead of the bit index (mask 0x04), and the
        // do-nothing third outcome 16-aligning anyway.
        sr3clmesh::WalkOptions o;
        o.hullGateMask = 0x02;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlHullMask02;
    }
    {
        sr3clmesh::WalkOptions o;
        o.gatedPairNoneBranchAligns = true;
        if (landsWith(content, lm.headerOffset(), o)) ++g_ctrlNoneBranchAligns;
    }
    {
        std::vector<uint32_t> p = words;
        p[sr3clmesh::kCount70 / 4] += 1;
        auto t = sr3clmesh::walkTail(content, lm.tailStart(), p.data(), cb.size());
        if (t.ok && t.end == cb.size()) ++g_ctrl70Plus1;
    }
    {
        std::vector<uint32_t> p = words;
        p[sr3clmesh::kCountFc / 4] = 4;
        auto t = sr3clmesh::walkTail(content, lm.tailStart(), p.data(), cb.size());
        if (t.ok && t.end == cb.size()) ++g_ctrlFcCount4;
    }
    {
        // The spec-literal reading of Sec4.1: one cursor threaded straight
        // from the head into FUN_00749b40, i.e. no MIDDLE at all.
        auto t = sr3clmesh::walkTail(content, lm.headEnd(), hw, cb.size());
        if (t.ok && t.end == cb.size()) ++g_specLiteralExact;
    }

    // --- the MeshBlock reuse for the `+0x48` resolved references ---------
    if (cRefA > 0) {
        ++g_refAFiles;
        g_refAEntries += cRefA;
        if (lm.meshBlockStart() + 4 <= cb.size() &&
            content.readU32LE(lm.meshBlockStart()) == sr3mesh::kMeshVersion) {
            ++g_meshAnchorAtStart;
        }
        std::vector<sr3mesh::MeshBlock> meshes = lm.resolveReferencedMeshes(content, gcontent);
        g_refAResolved += static_cast<long long>(meshes.size());
        if (meshes.size() == cRefA) ++g_refAFullyResolved;
        for (const sr3mesh::MeshBlock& m : meshes) {
            sr3clmesh::BoundingVolume bv = sr3clmesh::computeBoundingVolume(m);
            if (!bv.valid) continue;
            ++g_boundsComputed;
            if (std::isfinite(bv.radius) && bv.radius > 0.0f) {
                ++g_boundsFinite;
                g_radiusMin = std::min<double>(g_radiusMin, bv.radius);
                g_radiusMax = std::max<double>(g_radiusMax, bv.radius);
            }
        }
    }
}

void walk(const vpp::Container& c) {
    std::map<std::string, size_t> cl, gl;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".clmesh_pc", stem)) cl[stem] = i;
        else if (stemFor(n, ".glmesh_pc", stem)) gl[stem] = i;
    }
    for (const auto& kv : cl) {
        try {
            std::vector<uint8_t> cb;
            if (!entryBytes(c, kv.second, cb)) continue;
            std::vector<uint8_t> gb;
            auto git = gl.find(kv.first);
            if (git != gl.end()) entryBytes(c, git->second, gb);
            check(kv.first, cb, gb);
        } catch (const std::exception&) {
            continue;
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                walk(c.openNested(i));
            } catch (const std::exception&) {
            }
        }
    }
}

void printHistogram(const char* label, const std::map<uint32_t, long long>& h, size_t maxRows) {
    printf("  %-26s", label);
    size_t shown = 0;
    long long other = 0;
    for (const auto& kv : h) {
        if (shown < maxRows) {
            printf(" %u:%lld", kv.first, kv.second);
            ++shown;
        } else {
            other += kv.second;
        }
    }
    if (other) printf(" (+%lld in %zu more values)", other, h.size() - shown);
    printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: validate_clmesh <archive.vpp_pc> [more...]\n");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        const long long entriesBefore = g_pairs, landBefore = g_land;
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) {
            printf("skip: %s\n", argv[i]);
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
            printf("scanned %s : %lld entries, %lld land exactly\n", argv[i],
                   g_pairs - entriesBefore, g_land - landBefore);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== `.clmesh_pc` population ===\n");
    printf("  entries attempted            : %lld\n", g_pairs);
    printf("    with a paired `.glmesh_pc` : %lld\n", g_paired);
    printf("  header parsed (magic+ver 20) : %lld\n", g_parsed);
    printf("  header rejected              : %lld\n", g_parseFailed);

    printf("\n=== GO/NO-GO: does the COMPUTED walk land EXACTLY on EOF? ===\n");
    printf("  (one cursor, bodyStart -> EOF, nothing searched for)\n");
    printf("  exact landing                : %lld / %lld", g_land, g_parsed);
    if (g_parsed) printf("   (%.4f%%)", 100.0 * static_cast<double>(g_land) / static_cast<double>(g_parsed));
    printf("\n");
    printf("  did NOT land                 : %lld\n", g_noLand);
    printf("  HEAD walked / MIDDLE walked / TAIL walked : %lld / %lld / %lld\n", g_headOk,
           g_middleOk, g_tailOk);
    printf("  candidate layouts to disambiguate : 0 (the walk is computed; there is no search)\n");

    printf("\n=== CONTROLS - each disables exactly ONE confirmed term ===\n");
    printf("  MIDDLE start + 4             : %lld still land\n", g_ctrlMidPlus4);
    printf("  MIDDLE start + 16            : %lld still land\n", g_ctrlMidPlus16);
    printf("  TAIL start + 16              : %lld still land\n", g_ctrlTailPlus16);
    printf("  TAIL start - 16              : %lld still land\n", g_ctrlTailMinus16);
    printf("  name table without the +1 NUL: %lld still land\n", g_ctrlNoNul);
    printf("  one render group, not two    : %lld still land\n", g_ctrlOneGroup);
    printf("  omit R[+0x08]'s count x 8    : %lld still land\n", g_ctrlNoLookup);
    printf("  Mesh step roundUp16(cLength) : %lld still land\n", g_ctrlMeshRound16);
    printf("  gated `+0x78` pair costs 0   : %lld still land\n", g_ctrlGatedZero);
    printf("  HEAD without array 2's index lists : %lld still land of %lld that can discriminate\n",
           g_ctrlNoHeadIdx, g_ctrlNoHeadIdxDiscriminating);
    printf("  trailing group in PROSE order: %lld still land\n", g_ctrlProseOrder);
    printf("  count(+0x70) + 1             : %lld still land\n", g_ctrl70Plus1);
    printf("  count(+0xfc) = 4             : %lld still land\n", g_ctrlFcCount4);
    printf("  hull gate read as mask 0x02  : %lld still land\n", g_ctrlHullMask02);
    printf("  no-op gated branch 16-aligns : %lld still land\n", g_ctrlNoneBranchAligns);
    printf("  SPEC-LITERAL one-cursor walk : %lld / %lld land exactly\n", g_specLiteralExact,
           g_parsed);

    printf("\n=== the previously-unplaceable `~l1` signage sample (HANDOFF Sec9.59's 129) ===\n");
    printf("  named entries seen           : %lld  (%zu distinct stems)\n", g_prevUnplaceableSeen,
           g_prevUnplaceableStems.size());
    printf("  of those, land exactly       : %lld\n", g_prevUnplaceableLand);

    printf("\n=== the MIDDLE (thunk_FUN_00e3e590) ===\n");
    if (!g_middleLengths.empty()) {
        std::sort(g_middleLengths.begin(), g_middleLengths.end());
        const size_t n = g_middleLengths.size();
        printf("  byte length: min %zu  p25 %zu  median %zu  p75 %zu  max %zu  mean %.0f\n",
               g_middleMin, g_middleLengths[n / 4], g_middleLengths[n / 2],
               g_middleLengths[n * 3 / 4], g_middleMax, g_middleTotal / static_cast<double>(n));
        std::vector<std::pair<long long, size_t>> top;
        for (const auto& kv : g_middleLengthHistogram) top.emplace_back(kv.second, kv.first);
        std::sort(top.rbegin(), top.rend());
        printf("  most common exact lengths  :");
        for (size_t i = 0; i < top.size() && i < 4; ++i) printf(" %zu x%lld", top[i].second, top[i].first);
        printf("\n");
    }
    printf("  starts at bodyStart (empty HEAD) / after a real HEAD : %lld / %lld\n",
           g_middleAtBodyStart, g_middleAfterRealHead);
    printf("  two render-mesh groups       : %lld / %lld\n", g_twoRenderGroups, g_land);
    printHistogram("material record count", g_materialCount, 9);
    printf("  `+0xb8` record census (of the %lld that land):\n", g_land);
    printf("    +0x00 zero %lld, -1 %lld   |  +0x08 zero %lld  |  +0x10 zero %lld\n",
           g_recMatSetZero, g_recMatSetMinus1, g_recLookupZero, g_recGroup1Zero);
    printf("    +0x18 zero %lld, -1 %lld   |  +0x48 zero %lld\n", g_recGate2Zero, g_recGate2Minus1,
           g_recTrailerZero);
    printf("    +0x24 non-zero %lld  |  +0x34 non-zero %lld  |  +0x24 == +0x34 in %lld\n",
           g_rec24NonZero, g_rec34NonZero, g_rec24Equals34);
    printHistogram("  +0x20 group-1 records", g_recGroupACount, 6);
    printHistogram("  +0x30 group-2 records", g_recGroupBCount, 6);

    printf("\n=== header count fields (value:files) ===\n");
    printHistogram("count(+0x48) refs A", g_countRefA, 8);
    printHistogram("count(+0x70) 56-byte", g_count70, 8);
    printHistogram("count(+0x88) nested", g_count88, 8);
    printHistogram("count(+0x98) 52-byte", g_count98, 8);
    printHistogram("count(+0xa8) 96-byte", g_countA8, 8);
    printHistogram("count(+0xd8) 8-byte", g_countD8, 8);
    printHistogram("count(+0xfc) trailer", g_countFc, 8);
    printHistogram("slot(+0xb8) on disk", g_slotB8, 4);
    printf("  count(+0x48)==count(+0x58)==count(+0x70) : %lld / %lld\n", g_threeCountsEqual,
           g_parsed);
    printf("  nested group(+0x88): %lld outer entries, %lld inner 12-byte elements\n",
           g_group88Entries, g_group88InnerTotal);
    printf("  nested group(+0xfc): %lld outer entries, %lld inner 4-byte elements\n",
           g_groupFcEntries, g_groupFcInnerTotal);

    printf("\n=== the gated `+0x78`/`+0x7c` pair - THREE branches ===\n");
    printf("  header says sentinel -1      : %lld\n", g_sentinelFiles);
    printf("  header says real values      : %lld  (one value %lld, two values %lld)\n",
           g_realPairFiles, g_pairOneValue, g_pairTwoValues);
    printf("  branch taken: none %lld | FUN_00748c70 list block %lld | FUN_00749ba0 hull %lld\n",
           g_branchNone, g_branchList, g_branchHull);
    printf("  hull totals: %lld vertices, %lld u16 indices, %lld opaque blob bytes\n",
           g_hullVertices, g_hullIndices, g_hullBlobBytes);
    {
        printf("  list-block declared sizes  :");
        size_t shown = 0;
        long long other = 0;
        for (const auto& kv : g_listBlockSizes) {
            if (shown < 6) { printf(" %zu:%lld", kv.first, kv.second); ++shown; }
            else other += kv.second;
        }
        if (other) printf(" (+%lld in %zu more)", other, g_listBlockSizes.size() - shown);
        printf("\n");
    }

    printf("\n=== MeshBlock reuse for the `+0x48` resolved references ===\n");
    printf("  files with count(+0x48) > 0  : %lld  (%lld entries)\n", g_refAFiles, g_refAEntries);
    printf("  Mesh version-9 word at meshBlockStart(): %lld / %lld files\n", g_meshAnchorAtStart,
           g_refAFiles);
    printf("  MeshBlock::parse succeeded   : %lld / %lld entries\n", g_refAResolved, g_refAEntries);
    printf("  files where ALL resolved     : %lld / %lld\n", g_refAFullyResolved, g_refAFiles);
    printf("  HEAD index-list blocks (array 2) : %lld blocks, %lld 4-byte elements\n",
           g_headIndexListBlocks, g_headIndexListElements);

    printf("\n=== FUN_0074a110 finalization (AABB -> centre/half-extent/radius) ===\n");
    printf("  bounding volumes computed    : %lld\n", g_boundsComputed);
    printf("  finite, positive radius      : %lld\n", g_boundsFinite);
    if (g_boundsFinite > 0) printf("  radius range                 : %.6f .. %.6f\n", g_radiusMin, g_radiusMax);

    if (!g_failSignature.empty()) {
        printf("\n=== entries that did NOT land ===\n");
        size_t shown = 0;
        std::vector<std::pair<long long, std::string>> byCount;
        for (const auto& kv : g_failSignature) byCount.emplace_back(kv.second, kv.first);
        std::sort(byCount.rbegin(), byCount.rend());
        for (const auto& kv : byCount) {
            if (shown++ >= 12) break;
            printf("    %6lld  %s\n", kv.first, kv.second.c_str());
        }
        if (byCount.size() > shown) printf("    ... %zu more distinct signatures\n", byCount.size() - shown);
        printf("  first few by name:\n");
        for (const std::string& s : g_failExamples) printf("    %s\n", s.c_str());
    }
    return 0;
}
