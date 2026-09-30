// What does the material-binding run scan actually FIND on vehicles?
//
// `MaterialBindings` locates bindings on 3 of 372 vehicles. The shipping
// reader accepts a file only when `found.size() == materialCount`, and on
// vehicles `+0x0C` is an UPPER BOUND (the material set is sized for the
// whole multi-part asset), so that equality structurally cannot hold. That
// leaves a question the existing tooling cannot answer:
//
//   is the SCAN failing on vehicles, or only the ACCEPTANCE GUARD?
//
// `validate_binding_roles` cannot tell us - its tally() returns early on
// !located(), so every figure it prints for vehicles comes from the 3 that
// passed, not the 372. (Noticed while reading it, after briefly drawing a
// conclusion from `runs 70` as though the denominator were 372. It is 3.)
//
// So this harness replicates the scan from src/material_binding.cpp AS IT
// STOOD BEFORE 9.34.1, WITHOUT the acceptance guard, and reports what it
// sees. The reader has since gained a name-start check; this copy keeps the
// looser form ON PURPOSE, because it is the instrument that measures the raw
// population including the truncations the reader now filters. It does not
// modify the shipping reader and does not propose loosening it: a
// relaxed guard that binds a wrong texture to a real surface is exactly the
// silently-plausible failure this project refuses.
//
// Team A confirmed (spec-vehicle-geometry.md 7.2) that vehicles are walked
// by the IDENTICAL .ccmesh_pc code - there is no vehicle-specific material
// walk - so any structure found here is found by the same logic that is
// 6,064/6,064 correct on characters. That is what makes the comparison
// meaningful rather than a fishing expedition.
//
// CONTROLS, because a scan that finds things is worthless without a scan
// that shouldn't:
//   * scan start shifted +2 (breaks 4-byte alignment)
//   * name base shifted +1 (every name offset resolves one byte late)
// Both should collapse. If they don't, the "structure" is noise that passes
// the guards.
//
// Usage: validate_vehicle_runs <archive.vpp_pc> [...]
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3mesh/mesh_block.h"
#include "sr3vehicle/vehicle.h"
#include "vpp/container.h"

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok ||
              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

// ---- the scan, as src/material_binding.cpp stood BEFORE 9.34.1 ----------
constexpr size_t kNameBlobOffset = 0x88;
constexpr size_t kNameBlobFirstName = 9;
constexpr size_t kEntrySize = 12;
constexpr size_t kMaterialCountOffset = 0x0C;

bool resolveName(const std::vector<uint8_t>& c, size_t base, uint32_t offset, size_t blobEnd,
                 std::string& out) {
    size_t at = base + offset;
    if (at >= blobEnd || at >= c.size()) return false;
    std::string s;
    while (at < blobEnd && at < c.size()) {
        uint8_t ch = c[at];
        if (ch == 0) break;
        if (ch < 32 || ch >= 127) return false;
        s.push_back(static_cast<char>(ch));
        if (s.size() > 128) return false;
        ++at;
    }
    if (s.size() < 3) return false;
    if (s.find('.') == std::string::npos) return false;
    out = s;
    return true;
}
uint32_t rd32(const std::vector<uint8_t>& c, size_t at) {
    uint32_t v;
    std::memcpy(&v, c.data() + at, 4);
    return v;
}

// The 2x claim: vehicles declare exactly twice the materials their draw
// ranges reference; characters declare exactly what they draw. Measured per
// population, with the dense-prefix property checked separately so "exactly
// 2x" and "the unreferenced half is a contiguous suffix" stay two claims.
struct RatioPop {
    int files = 0, exactly2x = 0, twoXminus1 = 0, inBand = 0, tight = 0, densePrefix = 0;
    double maxRatio = 0.0, sumRatio = 0.0;
    long long slack = 0, declaredTotal = 0, distinctTotal = 0;
};
RatioPop g_rChar, g_rVeh;

void tallyRatio(RatioPop& p, uint16_t declared,
                const std::vector<std::vector<sr3mesh::MeshBlock::DrawRange>>& groups) {
    if (groups.empty() || declared == 0) return;
    std::set<uint32_t> ids;
    for (const auto& g : groups)
        for (const auto& r : g) ids.insert(r.materialId);
    if (ids.empty()) return;
    const size_t d = ids.size();
    ++p.files;
    p.declaredTotal += declared;
    p.distinctTotal += static_cast<long long>(d);
    if (declared >= d) p.slack += static_cast<long long>(declared - d);
    const double ratio = static_cast<double>(declared) / static_cast<double>(d);
    p.sumRatio += ratio;
    if (ratio > p.maxRatio) p.maxRatio = ratio;
    if (declared == 2 * d) ++p.exactly2x;
    if (declared + 1 == 2 * d) ++p.twoXminus1;
    if (ratio >= 1.9 && ratio <= 2.1) ++p.inBand;
    if (declared == d) ++p.tight;
    // Dense prefix: the referenced ids are exactly {0 .. d-1}.
    bool dense = true;
    uint32_t expect = 0;
    for (uint32_t id : ids) { if (id != expect) { dense = false; break; } ++expect; }
    if (dense) ++p.densePrefix;
}

struct Run {
    size_t at;
    size_t length;
    std::vector<std::string> names;
    std::vector<uint32_t> hashes;
    std::vector<bool> nameAtStart;
};

// startBias: 0 for the real scan, 2 for the misalignment control.
// nameBias:  0 for the real scan, 1 for the wrong-name-base control.
// loose: reconciliation mode. Keeps the sliding search and the length>=2
// rule, but drops the CONTENT guards - no non-zero-hash requirement, and a
// name offset need only land inside the blob rather than resolve to a
// printable dotted string. This is the difference between "the position
// could be a binding" and "the bytes there look like one", and it is the
// candidate explanation for another team finding ~2x this project's run
// count on the identical population.
std::vector<Run> scan(const std::vector<uint8_t>& c, size_t sub, size_t meshBlockEnd,
                      size_t startBias, size_t nameBias, bool loose = false) {
    std::vector<Run> out;
    const size_t blobAt = sub + kNameBlobOffset;
    if (blobAt + 4 > c.size()) return out;
    const uint32_t blobLength = rd32(c, blobAt);
    const size_t blobBase = blobAt + kNameBlobFirstName + nameBias;
    size_t blobEnd = blobAt + static_cast<size_t>(blobLength) + kNameBlobFirstName;
    if (blobEnd > c.size()) blobEnd = c.size();
    if (blobBase >= blobEnd) return out;
    if (meshBlockEnd >= c.size()) return out;

    for (size_t at = meshBlockEnd + startBias; at + 2 * kEntrySize <= c.size(); at += 4) {
        if (rd32(c, at + 0x08) != 0) continue;
        size_t length = 1;
        while (at + (length + 1) * kEntrySize <= c.size() &&
               rd32(c, at + length * kEntrySize + 0x08) == static_cast<uint32_t>(length)) {
            ++length;
        }
        if (length < 2) continue;
        Run r;
        r.at = at;
        r.length = length;
        bool ok = true;
        for (size_t e = 0; e < length; ++e) {
            const size_t entryAt = at + e * kEntrySize;
            const uint32_t nameOffset = rd32(c, entryAt + 0x00);
            const uint32_t paramHash = rd32(c, entryAt + 0x04);
            if (!loose && paramHash == 0) { ok = false; break; }
            std::string nm;
            if (loose) {
                if (blobBase + nameOffset >= blobEnd) { ok = false; break; }
                nm.clear();
            } else if (!resolveName(c, blobBase, nameOffset, blobEnd, nm)) {
                ok = false;
                break;
            }
            r.names.push_back(nm);
            r.hashes.push_back(paramHash);
            // ORACLE, independent of the run search: a name offset that is
            // really a name offset must land on a name START - i.e. at the
            // blob base, or immediately after a NUL. A truncation ("_df.tga"
            // cut out of "xx_df.tga") lands mid-string and fails this, while
            // still satisfying resolveName's printable/has-a-dot test. This
            // is what distinguishes "the scan found runs" from "the runs are
            // correctly resolved", which are NOT the same claim.
            const size_t nameAt = blobBase + nameOffset;
            const bool atStart = (nameAt == blobBase) ||
                                 (nameAt > 0 && nameAt - 1 < c.size() && c[nameAt - 1] == 0);
            r.nameAtStart.push_back(atStart);
        }
        if (!ok) continue;
        out.push_back(std::move(r));
    }
    return out;
}

// ---- tallies --------------------------------------------------------------
int g_files = 0, g_skippedNoMesh = 0, g_skippedThrew = 0, g_skippedNoGcar = 0;
int g_zeroRuns = 0;
long long g_runs = 0, g_entries = 0;
long long g_ctlStartRuns = 0, g_ctlNameRuns = 0;
long long g_looseRuns = 0;
int g_lt = 0, g_eq = 0, g_gt = 0;
int g_drawCounted = 0, g_drawFiles = 0, g_dlt = 0, g_deq = 0, g_dgt = 0;
long long g_drawRanges = 0;
int g_ilt = 0, g_ieq = 0, g_igt = 0, g_idOutOfRange = 0, g_tight = 0;
long long g_slack = 0;
std::vector<std::vector<sr3mesh::MeshBlock::DrawRange>> g_meshGroups;
int g_overlapFiles = 0;
long long g_overlaps = 0;
std::map<uint32_t, long long> g_hash;
std::map<int, int> g_runHist;
std::set<std::string> g_names;
std::vector<std::string> g_examples;

// The name-start oracle, tallied separately for the two populations.
// CHARACTERS ARE THE CONTROL: `MaterialBindings` is 546/546 located there
// with a 6,064/6,064 independent cross-check, so whatever this oracle scores
// on characters is what "correct" looks like for it. A vehicle score far
// below that is the measurement; a vehicle score at parity would mean the
// truncations seen in the name list are not what they appear to be.
struct NameOracle {
    long long entries = 0;
    long long atStart = 0;
    long long runsAllAtStart = 0;
    long long runs = 0;
    long long smallIntHash = 0;   // paramHash < 256 or 0xFFFFFFFF: not a hash
};
NameOracle g_oChar, g_oVeh;

void tallyOracle(NameOracle& o, const std::vector<Run>& runs) {
    for (const Run& r : runs) {
        ++o.runs;
        bool all = true;
        for (size_t i = 0; i < r.nameAtStart.size(); ++i) {
            ++o.entries;
            if (r.nameAtStart[i]) ++o.atStart; else all = false;
            const uint32_t h = r.hashes[i];
            if (h < 256u || h == 0xFFFFFFFFu) ++o.smallIntHash;
        }
        if (all) ++o.runsAllAtStart;
    }
}

void doFile(const std::string& name, const std::vector<uint8_t>& cb,
            const std::vector<uint8_t>& gb) {
    ++g_files;
    size_t sub = 0, meshEnd = 0;
    try {
        sr3vehicle::Vehicle v = sr3vehicle::Vehicle::parse(vpp::ByteView(cb.data(), cb.size()));
        sub = v.meshRegionOffset();
        sr3geometry::GeometryBlock geo =
            sr3geometry::GeometryBlock::parseAt(vpp::ByteView(cb.data(), cb.size()), sub);
        if (!geo.hasMeshSubBlock()) { ++g_skippedNoMesh; return; }
        sr3mesh::MeshBlock mesh =
            sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                      geo.meshSubBlockOffset(),
                                      vpp::ByteView(gb.data(), gb.size()));
        meshEnd = geo.meshSubBlockOffset() + mesh.cLength();
        g_meshGroups = mesh.drawGroupsLocated() ? mesh.drawGroups()
                                               : std::vector<std::vector<sr3mesh::MeshBlock::DrawRange>>();
    } catch (const std::exception&) {
        ++g_skippedThrew;
        return;
    }

    if (sub + kMaterialCountOffset + 2 > cb.size()) { ++g_skippedThrew; return; }
    uint16_t declared;
    std::memcpy(&declared, cb.data() + sub + kMaterialCountOffset, 2);

    std::vector<Run> runs = scan(cb, sub, meshEnd, 0, 0);
    tallyOracle(g_oVeh, runs);
    g_runs += static_cast<long long>(runs.size());
    g_runHist[static_cast<int>(runs.size() > 40 ? 41 : runs.size())] += 1;
    if (runs.empty()) ++g_zeroRuns;

    for (const Run& r : runs) {
        g_entries += static_cast<long long>(r.length);
        for (uint32_t h : r.hashes) g_hash[h] += 1;
        for (const std::string& n : r.names)
            if (g_names.size() < 4000) g_names.insert(n);
    }

    if (runs.size() < declared) ++g_lt;
    else if (runs.size() == declared) ++g_eq;
    else ++g_gt;

    // DRAW RANGES vs materialCount - a DIFFERENT quantity from binding runs.
    // spec-vertex-format.md 8.4.1 cites this project's binding-run
    // measurement (369 <, 3 ==, 0 >) as evidence for "draw runs <=
    // materialCount". Those are not the same thing, so measure the draw-range
    // relation directly instead of letting one number stand for both.
    if (g_drawCounted >= 0) {
        long long ranges = 0;
        for (const auto& grp : g_meshGroups) ranges += static_cast<long long>(grp.size());
        if (!g_meshGroups.empty()) {
            ++g_drawFiles;
            g_drawRanges += ranges;
            if (ranges < declared) ++g_dlt;
            else if (ranges == declared) ++g_deq;
            else ++g_dgt;
            // The replacement claim: distinct MASKED ids vs materialCount.
            std::set<uint32_t> ids;
            uint32_t maxId = 0;
            for (const auto& grp : g_meshGroups)
                for (const auto& r : grp) { ids.insert(r.materialId); if (r.materialId > maxId) maxId = r.materialId; }
            const size_t d = ids.size();
            if (d < declared) ++g_ilt; else if (d == declared) ++g_ieq; else ++g_igt;
            if (declared > 0 && maxId >= declared) ++g_idOutOfRange;
            if (declared >= d) g_slack += static_cast<long long>(declared - d);
            if (declared == d) ++g_tight;
            tallyRatio(g_rVeh, declared, g_meshGroups);
        }
    }

    // AMBIGUITY: do located runs overlap each other? A greedy 4-byte step
    // can latch onto the same array from several starts, and overlapping
    // "materials" would mean the run count is not a material count.
    long long ov = 0;
    for (size_t i = 1; i < runs.size(); ++i) {
        const size_t prevEnd = runs[i - 1].at + runs[i - 1].length * kEntrySize;
        if (runs[i].at < prevEnd) ++ov;
    }
    if (ov > 0) { ++g_overlapFiles; g_overlaps += ov; }

    g_looseRuns += static_cast<long long>(scan(cb, sub, meshEnd, 0, 0, true).size());
    g_ctlStartRuns += static_cast<long long>(scan(cb, sub, meshEnd, 2, 0).size());
    g_ctlNameRuns += static_cast<long long>(scan(cb, sub, meshEnd, 0, 1).size());

    if (g_examples.size() < 6 && !runs.empty()) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s: declared=%u runs=%zu first=[%s x%zu]", name.c_str(),
                 declared, runs.size(), runs[0].names[0].c_str(), runs[0].length);
        g_examples.push_back(buf);
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".ccar_pc")) {
            const std::string gn = n.substr(0, n.size() - 8) + ".gcar_pc";
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j)
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            if (gb.empty()) { ++g_skippedNoGcar; continue; }
            doFile(n, cb, gb);
        }
        // CHARACTER CONTROL: same scan, same oracle, on the population where
        // the binding is already known correct.
        if (endsWith(n, ".ccmesh_pc")) {
            const std::string gn = n.substr(0, n.size() - 10) + ".gcmesh_pc";
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j)
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            if (gb.empty()) continue;
            try {
                sr3geometry::MaterialBlock mat =
                    sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
                sr3geometry::GeometryBlock geo =
                    sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
                if (!geo.hasMeshSubBlock()) continue;
                sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
                    vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
                    vpp::ByteView(gb.data(), gb.size()));
                tallyOracle(g_oChar, scan(cb, geo.offset(),
                                          geo.meshSubBlockOffset() + mesh.cLength(), 0, 0));
                if (geo.offset() + kMaterialCountOffset + 2 <= cb.size()) {
                    uint16_t cdec;
                    std::memcpy(&cdec, cb.data() + geo.offset() + kMaterialCountOffset, 2);
                    tallyRatio(g_rChar, cdec,
                               mesh.drawGroupsLocated()
                                   ? mesh.drawGroups()
                                   : std::vector<std::vector<sr3mesh::MeshBlock::DrawRange>>());
                }
            } catch (const std::exception&) {}
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
        } catch (const std::exception& e) {
            printf("archive %s: %s\n", argv[i], e.what());
        }
        printf("scanned %s\n", argv[i]);
    }

    printf("\n=== vehicle material-run scan, acceptance guard REMOVED ===\n");
    printf("vehicles examined      : %d\n", g_files);
    printf("  skipped: no .gcar    : %d\n", g_skippedNoGcar);
    printf("  skipped: no Mesh     : %d\n", g_skippedNoMesh);
    printf("  skipped: threw       : %d\n", g_skippedThrew);
    printf("vehicles with 0 runs   : %d\n", g_zeroRuns);
    printf("total runs / entries   : %lld / %lld\n", g_runs, g_entries);

    printf("\nRECONCILIATION - same sliding search, CONTENT guards dropped:\n");
    printf("  runs with no hash/name validation : %lld\n", g_looseRuns);
    printf("  (vs %lld with them) - the gap IS the guards, not the search\n", g_runs);

    printf("\nCONTROLS (same scan, deliberately wrong):\n");
    printf("  scan start +2 (misaligned) : %lld runs\n", g_ctlStartRuns);
    printf("  name base  +1 (off by one) : %lld runs\n", g_ctlNameRuns);

    printf("\nrun count vs declared materialCount (+0x0C):\n");
    printf("  runs <  declared : %d\n", g_lt);
    printf("  runs == declared : %d\n", g_eq);
    printf("  runs >  declared : %d\n", g_gt);

    printf("\nAMBIGUITY - overlapping runs:\n");
    printf("  files with overlaps : %d\n", g_overlapFiles);
    printf("  overlapping pairs   : %lld\n", g_overlaps);

    printf("\nrun-count histogram (41 = 41+):\n");
    for (const auto& kv : g_runHist) printf("  %3d runs : %d files\n", kv.first, kv.second);

    printf("\nparam hashes seen (all vehicles, not just guard-passing):\n");
    std::vector<std::pair<long long, uint32_t>> hs;
    for (const auto& kv : g_hash) hs.push_back({kv.second, kv.first});
    std::sort(hs.rbegin(), hs.rend());
    for (size_t i = 0; i < hs.size() && i < 14; ++i)
        printf("  0x%08X : %lld\n", hs[i].second, hs[i].first);
    printf("  distinct hashes : %zu\n", g_hash.size());

    printf("\ndistinct names resolved : %zu\n", g_names.size());
    int shown = 0;
    for (const std::string& n : g_names) {
        if (shown++ >= 18) break;
        printf("  %s\n", n.c_str());
    }
    printf("\n=== THE 2x CLAIM: declared vs REFERENCED materials ===\n");
    printf("Team A: vehicles declare exactly twice the materials their draw\n");
    printf("ranges reference, capped at 2.000; characters declare what they\n");
    printf("draw. Verified here independently, characters as the control.\n\n");
    struct RRow { const char* label; const RatioPop* p; } rrows[] = {
        {"CHARACTERS (control)", &g_rChar}, {"VEHICLES", &g_rVeh}};
    for (const RRow& rr : rrows) {
        const RatioPop& p = *rr.p;
        if (p.files == 0) { printf("%-22s no files\n", rr.label); continue; }
        printf("%-22s files %d\n", rr.label, p.files);
        printf("  declared / referenced totals : %lld / %lld\n", p.declaredTotal, p.distinctTotal);
        printf("  mean slack                   : %.2f  (%.1f%% of declared)\n",
               static_cast<double>(p.slack) / p.files,
               100.0 * static_cast<double>(p.slack) / static_cast<double>(p.declaredTotal));
        printf("  declared == referenced       : %d / %d\n", p.tight, p.files);
        printf("  declared == 2x referenced    : %d / %d\n", p.exactly2x, p.files);
        printf("  declared == 2x-1             : %d / %d\n", p.twoXminus1, p.files);
        printf("  ratio within [1.9, 2.1]      : %d / %d\n", p.inBand, p.files);
        printf("  mean ratio / MAX ratio       : %.4f / %.4f\n",
               p.sumRatio / p.files, p.maxRatio);
        printf("  referenced ids a dense prefix: %d / %d\n", p.densePrefix, p.files);
    }

    printf("\n=== THE REPLACEMENT CLAIM: distinct MASKED material ids ===\n");
    printf("spec 8.4.1 now says the bound holds for distinct materials\n");
    printf("REFERENCED, with the id masked (16:16). Held to the same bar as\n");
    printf("the claim it replaces - including whether it COULD have failed.\n");
    printf("  vehicles measured            : %d\n", g_drawFiles);
    printf("  distinct ids <  materialCount: %d\n", g_ilt);
    printf("  distinct ids == materialCount: %d\n", g_ieq);
    printf("  distinct ids >  materialCount: %d\n", g_igt);
    printf("  max(id) >= materialCount     : %d   (would be an out-of-range id)\n", g_idOutOfRange);
    printf("  total slack (count - distinct): %lld over %d files, mean %.1f\n",
           g_slack, g_drawFiles,
           g_drawFiles ? static_cast<double>(g_slack) / g_drawFiles : 0.0);
    printf("  files where slack == 0 (tight): %d\n", g_tight);

    printf("\n=== DRAW RANGES vs materialCount (a DIFFERENT quantity) ===\n");
    printf("spec 8.4.1 cites this project's BINDING-run measurement as its\n");
    printf("evidence for a claim about DRAW runs. Measured directly:\n");
    printf("  vehicles with draw groups : %d\n", g_drawFiles);
    printf("  total draw ranges         : %lld\n", g_drawRanges);
    printf("  ranges <  materialCount   : %d\n", g_dlt);
    printf("  ranges == materialCount   : %d\n", g_deq);
    printf("  ranges >  materialCount   : %d\n", g_dgt);

    printf("\n=== NAME-START ORACLE ===\n");
    printf("A real name offset lands on a name START (blob base, or just\n");
    printf("after a NUL). A truncation passes resolveName but fails this.\n");
    printf("Characters are the control: bindings there are 546/546 located\n");
    printf("with a 6,064/6,064 cross-check, so their score is what correct\n");
    printf("looks like for this oracle.\n\n");
    struct Row { const char* label; const NameOracle* o; } rows[] = {
        {"CHARACTERS (control)", &g_oChar}, {"VEHICLES", &g_oVeh}};
    for (const Row& row : rows) {
        const NameOracle& o = *row.o;
        const double pe = o.entries ? 100.0 * static_cast<double>(o.atStart) /
                                      static_cast<double>(o.entries) : 0.0;
        const double pr = o.runs ? 100.0 * static_cast<double>(o.runsAllAtStart) /
                                   static_cast<double>(o.runs) : 0.0;
        printf("%-22s runs %lld, entries %lld\n", row.label, o.runs, o.entries);
        printf("  entries whose name starts at a name boundary : %lld (%.2f%%)\n", o.atStart, pe);
        printf("  runs where EVERY name does                   : %lld (%.2f%%)\n",
               o.runsAllAtStart, pr);
        printf("  entries whose 'param hash' is <256 or -1     : %lld\n", o.smallIntHash);
    }

    printf("\nexamples:\n");
    for (const std::string& s : g_examples) printf("  %s\n", s.c_str());
    return 0;
}
