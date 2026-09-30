// THE OPEN CAVEAT from probe_anim_duration_unit.cpp: the header field at
// +0x06 (field_0x06_rawCount() / durationTotal(), a u16) is recorded in
// spec-anim-format.md as ranging 2-1099 EMPIRICALLY over 4,209 clips. But the
// per-track duration total S measured by that probe reaches 2016 (median
// 240, p90 946) - well above 1099. A u16 field that never empirically
// exceeds 1099 cannot represent an S above that ceiling, so within the
// "tight" stratum (clips whose headline tracks agree on S to within 1%,
// where +0x06 matched S in 81.46% of clips vs 0.07% in the loose stratum)
// the ~19% of clips where +0x06 != S is a candidate explanation away from
// being a loose end:
//
//   PREDICTION (stated in advance, falsifiable): the tight-stratum clips
//   where +0x06 != S are the SAME SET as the clips where S > 1099.
//
// This is a claim about SET MEMBERSHIP, not about matching counts - two
// same-sized sets that don't overlap would be a coincidence of magnitude,
// not confirmation. So this probe builds the actual 2x2 contingency table
// (S<=1099 / S>1099) x (+0x06==S / +0x06!=S) within the tight stratum, and
// reads the verdict off that table rather than off two separate counts:
//   - CONFIRMED  if mismatches cluster almost entirely in the S>1099 row
//                and the S<=1099 row is almost entirely matches
//   - REFUTED    if mismatches appear substantially in the S<=1099 row too
//
// For any S<=1099 mismatches (prediction-violating cases, should be rare or
// absent if confirmed) this probe also reports what +0x06 holds instead, in
// case a second effect is visible in the residue.
//
// Data source and gating: identical to probe_anim_duration_unit.cpp and
// probe_anim_delta_mode.cpp - the existing reader only (vpp::Container,
// sr3anim::Animation::parse, sr3anim::Payload::walk). Population: clips
// without flags & kExtraPayloadFlag whose walk both completes and lands on
// the declared end. Per clip, S = median of per-track byte sums over tracks
// with translationKeys >= 2 (single-key tracks excluded - a lone byte caps
// at 0-255 and cannot represent totals in the hundreds/thousands, same
// exclusion probe_anim_duration_unit.cpp uses); at least 2 such tracks
// required per clip, matching that probe's methodology exactly so the
// "tight stratum" and "81.46%" figures reproduce here as a sanity check
// before the new table is read. This file only reads; it modifies no
// existing file, reader, or probe.
//
// Usage: probe_anim_duration_residual <archive.vpp_pc>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
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
    if (r.status != vpp::DecodeStatus::Ok &&
        r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}

// ---- population counters -------------------------------------------------
int g_clips = 0;
long long g_anim_pc_seen = 0;
long long g_skipped_parse_failed = 0;
long long g_skipped_extra_payload_flag = 0;
long long g_skipped_walk_incomplete_or_off_end = 0;

int g_tracks = 0;
long long g_skipped_no_keys = 0;
long long g_skipped_wide_translation_stride = 0;
long long g_skipped_control_walk_short = 0;
long long g_skipped_oob = 0;
long long g_tracksOneKey = 0;
long long g_clipsHeadlineLT2 = 0;
long long g_clipsHeadlineGE2 = 0;

struct ClipRec {
    std::string name;
    double S = 0.0;
    double field06 = 0.0;
    double relSpread = 0.0; // (max-min)/S across headline tracks
};
std::vector<ClipRec> g_clipRecs;

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1))];
}

struct TrackResult {
    bool ok = false;
    uint32_t translationKeys = 0;
    double byteSum = 0.0;
};

// Identical gating to probe_anim_duration_unit.cpp::doTrack.
TrackResult doTrack(vpp::ByteView bv, const sr3anim::TrackBlock& blk) {
    TrackResult tr;
    if (blk.translationKeys == 0) { ++g_skipped_no_keys; return tr; }
    if (blk.translationControlStride != 8) {
        ++g_skipped_wide_translation_stride;
        return tr;
    }
    if (blk.translationControlCount == 0) { ++g_skipped_control_walk_short; return tr; }

    const size_t sampleBase = blk.translationSampleOffset;
    const size_t extraBase = sampleBase + blk.translationKeys * 3;
    if (extraBase + blk.translationKeys > bv.size()) { ++g_skipped_oob; return tr; }

    size_t key = 0;
    for (size_t r = 0; r < blk.translationControlCount && key < blk.translationKeys; ++r) {
        const size_t at = blk.translationControlOffset + r * blk.translationControlStride;
        if (at + 8 > bv.size()) break;
        const uint32_t span = bv.readU16LE(at);
        if (span == 0) break;
        for (uint32_t s = 0; s < span && key < blk.translationKeys; ++s, ++key) {}
    }
    if (key != blk.translationKeys) { ++g_skipped_control_walk_short; return tr; }

    double sum = 0.0;
    for (size_t k = 0; k < blk.translationKeys; ++k) sum += static_cast<double>(bv.at(extraBase + k));

    tr.ok = true;
    tr.translationKeys = static_cast<uint32_t>(blk.translationKeys);
    tr.byteSum = sum;
    return tr;
}

void doClip(const std::string& name, const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { ++g_skipped_parse_failed; return; }
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) { ++g_skipped_extra_payload_flag; return; }
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) { ++g_skipped_walk_incomplete_or_off_end; return; }
    ++g_clips;

    std::vector<double> headlineSums;
    for (const auto& blk : pl.tracks()) {
        TrackResult tr = doTrack(bv, blk);
        if (!tr.ok) continue;
        ++g_tracks;
        if (tr.translationKeys == 1) { ++g_tracksOneKey; continue; }
        headlineSums.push_back(tr.byteSum);
    }

    if (headlineSums.size() < 2) { ++g_clipsHeadlineLT2; return; }
    ++g_clipsHeadlineGE2;

    const double S = median(headlineSums);
    double relSpread = 0.0;
    const double mn = *std::min_element(headlineSums.begin(), headlineSums.end());
    const double mx = *std::max_element(headlineSums.begin(), headlineSums.end());
    if (S > 0.0) {
        relSpread = (mx - mn) / S;
    } else if (mx == mn) {
        relSpread = 0.0;
    } else {
        relSpread = 1.0;
    }

    ClipRec rec;
    rec.name = name;
    rec.S = S;
    rec.field06 = static_cast<double>(a.durationTotal());
    rec.relSpread = relSpread;
    g_clipRecs.push_back(rec);
}

void walkArchive(const vpp::Container& c, const std::string& archivePath) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            ++g_anim_pc_seen;
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) doClip(archivePath + "/" + c.entries()[i].name, b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkArchive(c.openNested(i), archivePath + "/" + c.entries()[i].name); } catch (...) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkArchive(c, argv[i]);
        } catch (const std::exception& e) { printf("%s\n", e.what()); }
        printf("scanned %s\n", argv[i]);
    }

    printf("\n=== does the +0x06 mismatch equal the S>1099 set? (u16 ceiling"
           " hypothesis) ===\n");

    printf("\n--- population and skips (identical gating to"
           " probe_anim_duration_unit.cpp) ---\n");
    printf(".anim_pc entries seen (all archives): %lld\n", g_anim_pc_seen);
    printf("  skipped, parse failed: %lld\n", g_skipped_parse_failed);
    printf("  skipped, flags & 0x40 (unaccounted payload): %lld\n", g_skipped_extra_payload_flag);
    printf("  skipped, walk incomplete or missed declared end: %lld\n", g_skipped_walk_incomplete_or_off_end);
    printf("clean clips used: %d\n", g_clips);
    printf("tracks used (passed all QC gates): %d\n", g_tracks);
    printf("  tracks skipped, translationKeys == 0: %lld\n", g_skipped_no_keys);
    printf("  tracks skipped, wide (0x20) translation stride: %lld\n", g_skipped_wide_translation_stride);
    printf("  tracks skipped, control-record walk did not cover all keys: %lld\n", g_skipped_control_walk_short);
    printf("  tracks skipped, extra-byte region out of file bounds: %lld\n", g_skipped_oob);
    printf("tracks with translationKeys == 1 (excluded from S): %lld / %d\n", g_tracksOneKey, g_tracks);
    printf("clips with <2 headline tracks (S not computable, excluded): %lld\n", g_clipsHeadlineLT2);
    printf("clips with >=2 headline tracks (S computed): %lld\n", g_clipsHeadlineGE2);
    printf("clips with usable S: %zu\n", g_clipRecs.size());

    // Split into tight / loose strata exactly as probe_anim_duration_unit.cpp
    // does: relSpread <= 0.01 is "tight".
    std::vector<const ClipRec*> tight, loose;
    for (const auto& r : g_clipRecs) {
        if (r.relSpread <= 0.01) tight.push_back(&r);
        else loose.push_back(&r);
    }

    printf("\n--- REPORT ITEM 1: the tight stratum (per-track S agrees to"
           " within 1%%) ---\n");
    long long tightMatch = 0, tightMismatch = 0;
    for (auto* r : tight) { if (r->S == r->field06) ++tightMatch; else ++tightMismatch; }
    printf("tight-stratum clips: N = %zu (loose stratum: %zu)\n", tight.size(), loose.size());
    printf("  +0x06 == S : %lld (%.2f%%)\n", tightMatch,
           tight.size() ? 100.0 * static_cast<double>(tightMatch) / static_cast<double>(tight.size()) : 0.0);
    printf("  +0x06 != S : %lld (%.2f%%)\n", tightMismatch,
           tight.size() ? 100.0 * static_cast<double>(tightMismatch) / static_cast<double>(tight.size()) : 0.0);

    printf("\n--- REPORT ITEM 2: the 2x2 CONTINGENCY TABLE within the tight"
           " stratum ---\n");
    printf("rows: S<=1099 / S>1099      cols: +0x06==S / +0x06!=S\n");
    long long n_le_match = 0, n_le_mismatch = 0, n_gt_match = 0, n_gt_mismatch = 0;
    std::vector<const ClipRec*> mismatchesLE; // the prediction-violating cases
    for (auto* r : tight) {
        const bool match = (r->S == r->field06);
        const bool le = (r->S <= 1099.0);
        if (le && match) ++n_le_match;
        else if (le && !match) { ++n_le_mismatch; mismatchesLE.push_back(r); }
        else if (!le && match) ++n_gt_match;
        else ++n_gt_mismatch;
    }
    printf("%-16s %14s %14s %10s\n", "", "+0x06==S", "+0x06!=S", "row total");
    printf("%-16s %14lld %14lld %10lld\n", "S <= 1099", n_le_match, n_le_mismatch, n_le_match + n_le_mismatch);
    printf("%-16s %14lld %14lld %10lld\n", "S > 1099", n_gt_match, n_gt_mismatch, n_gt_match + n_gt_mismatch);
    printf("%-16s %14lld %14lld %10lld\n", "col total", n_le_match + n_gt_match, n_le_mismatch + n_gt_mismatch,
           static_cast<long long>(tight.size()));

    printf("\n--- REPORT ITEM 3: mechanical verdict ---\n");
    const long long totalMismatch = n_le_mismatch + n_gt_mismatch;
    const long long totalLE = n_le_match + n_le_mismatch;
    const long long totalGT = n_gt_match + n_gt_mismatch;
    const double leMismatchRate = totalLE ? 100.0 * static_cast<double>(n_le_mismatch) / static_cast<double>(totalLE) : 0.0;
    const double gtMismatchShare = totalMismatch ? 100.0 * static_cast<double>(n_gt_mismatch) / static_cast<double>(totalMismatch) : 0.0;
    printf("of all tight-stratum mismatches (%lld total), %lld (%.2f%%) fall in the"
           " S>1099 row.\n", totalMismatch, n_gt_mismatch, gtMismatchShare);
    printf("of the S<=1099 row (%lld clips), %lld (%.2f%%) mismatch.\n", totalLE, n_le_mismatch, leMismatchRate);
    printf("of the S>1099 row (%lld clips), %lld (%.2f%%) mismatch.\n", totalGT, n_gt_mismatch,
           totalGT ? 100.0 * static_cast<double>(n_gt_mismatch) / static_cast<double>(totalGT) : 0.0);
    if (leMismatchRate <= 2.0 && gtMismatchShare >= 90.0 && totalGT > 0) {
        printf("\n  VERDICT: CONFIRMED - essentially all S<=1099 clips match (%.2f%% mismatch)"
               " and essentially all mismatches (%.2f%%) are concentrated in the S>1099 row.\n"
               "  The tight-stratum mismatch set and the S>1099 set are the same set: the"
               " field-06 u16 simply cannot represent S once S exceeds its empirical ceiling.\n",
               leMismatchRate, gtMismatchShare);
    } else if (leMismatchRate > 10.0) {
        printf("\n  VERDICT: REFUTED - mismatches appear substantially (%.2f%%) even within"
               " S<=1099, where the u16-ceiling explanation predicts none. Something else is"
               " wrong with the identification; the S>1099 explanation does not account for"
               " the tight-stratum mismatches by itself.\n", leMismatchRate);
    } else {
        printf("\n  VERDICT: AMBIGUOUS - neither cleanly confirmed nor cleanly refuted by the"
               " thresholds above; read the table directly.\n");
    }

    printf("\n--- REPORT ITEM 4: the S<=1099 mismatches themselves (should be"
           " rare/absent if confirmed) ---\n");
    printf("count: %zu\n", mismatchesLE.size());
    std::sort(mismatchesLE.begin(), mismatchesLE.end(),
              [](const ClipRec* a, const ClipRec* b) { return a->S < b->S; });
    const size_t showN = std::min<size_t>(20, mismatchesLE.size());
    if (showN > 0) {
        printf("%-60s %8s %8s %10s %10s\n", "clip", "S", "+0x06", "diff", "ratio(f06/S)");
        for (size_t i = 0; i < showN; ++i) {
            const ClipRec* r = mismatchesLE[i];
            const double diff = r->field06 - r->S;
            const double ratio = r->S != 0.0 ? r->field06 / r->S : 0.0;
            printf("%-60s %8.1f %8.1f %10.1f %10.4f\n", r->name.c_str(), r->S, r->field06, diff, ratio);
        }
        if (mismatchesLE.size() > showN) printf("... (%zu more not shown)\n", mismatchesLE.size() - showN);
    } else {
        printf("(none - no S<=1099 mismatches in the tight stratum)\n");
    }

    printf("\n--- REPORT ITEM 5: observed maxima (tight stratum, n=%zu) ---\n", tight.size());
    {
        double maxField06 = 0.0, maxS = 0.0;
        for (auto* r : tight) {
            maxField06 = std::max(maxField06, r->field06);
            maxS = std::max(maxS, r->S);
        }
        printf("observed max +0x06 : %.1f%s\n", maxField06, maxField06 > 1099.0 ? "  <-- EXCEEDS the relayed 2-1099 empirical range" : "  (within relayed 2-1099 range)");
        printf("observed max S     : %.1f\n", maxS);
    }

    // Whole-population (not just tight-stratum) maxima too, for completeness -
    // the relayed range was over "4,209 clips", not just the tight stratum.
    printf("\n--- for reference: observed maxima over ALL clips with usable S"
           " (n=%zu, tight+loose) ---\n", g_clipRecs.size());
    {
        double maxField06All = 0.0, maxSAll = 0.0;
        for (const auto& r : g_clipRecs) {
            maxField06All = std::max(maxField06All, r.field06);
            maxSAll = std::max(maxSAll, r.S);
        }
        printf("observed max +0x06 : %.1f%s\n", maxField06All, maxField06All > 1099.0 ? "  <-- EXCEEDS the relayed 2-1099 empirical range" : "  (within relayed 2-1099 range)");
        printf("observed max S     : %.1f\n", maxSAll);
    }

    printf("\n=== end of measurements. Verdict line above is a mechanical threshold"
           " check on the printed table, not a substitute for reading it. ===\n");
    return 0;
}
