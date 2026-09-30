// Does the extra byte after each translation key's 3 sample bytes encode a
// PER-KEY DURATION (frames/ticks this key holds before the next key)?
//
// spec-anim-format.md Sec6c.5 confirms the translation sample layout
// (axis = base/64 + delta/4000, one control record's base plus a per-key
// signed byte delta); Sec6c.6 item 2 leaves the trailing extra byte - one
// more byte per translation key, after the transKeys*3 sample bytes - OPEN.
//
// A prior probe (tools/validation/probe_anim_fourth_byte.cpp) characterised
// it over 465,547 keys from 3,687 clean clips: small, right-skewed, UNSIGNED
// (median 2, mean 7.49, stddev 12.0; 71.5% in 1-7; only 3.4% zero; 96.6%
// positive under int8); NOT constant within a control record's run (46.95%
// vs a 47.71% control); NOT correlated with that key's own delta magnitude
// (|r| <= 0.03); NOT a simple ascending index (r = -0.196, wrong sign) or a
// countdown index (both failed their own controls); 6.42% exceed the
// track's total key count (not a slot number). Its STRONGEST correlations
// are with STRUCTURE: r = -0.337 with the track's translationKeys, r=-0.186
// with the covering run's span.
//
// THIS PROBE tests the hypothesis those negative correlations predict
// directly: the byte is a PER-KEY DURATION. Every track in a clip animates
// the SAME clip over the SAME total duration, so if the byte is a duration,
// sum(byte) over a track should land NEARLY EQUAL across all tracks of one
// clip - a densely-keyed track packs many small gaps into a fixed length; a
// sparse track has fewer, larger gaps, but the total should still land near
// the same place. That is the decisive, falsifiable prediction, checked
// with two controls that could each independently sink it:
//
//   CONTROL 1 (cross-clip) - the identical within-GROUP spread statistic,
//   computed over per-track sums drawn from groups assembled across
//   DIFFERENT clips (same group sizes, values pulled from a flattened pool
//   offset by half its length - same technique the two probes this copies
//   from use for their own unrelated-pairing controls). Says what
//   "unrelated totals" looks like. If within-clip spread is not dramatically
//   lower than this, the hypothesis is refuted outright.
//
//   CONTROL 2 (shuffle, the important one) - the identical within-clip
//   spread statistic recomputed on a KNOWN non-duration per-key quantity
//   with a similar distribution: sum(|dx|), the absolute value of the first
//   translation sample delta byte, over the SAME tracks. If this ALSO comes
//   out low-spread within a clip, low spread is an artefact of track length
//   / population structure, not evidence the extra byte specifically is a
//   duration - the test would not have discriminated, and that is reported
//   plainly rather than papered over.
//
// Also checked: whether a track's sum(byte) correlates with, or equals, that
// track's rotationKeys, and the rotation side's own total span sum (rotation
// control records carry a run span in the low 6 bits of byte 3: span = 1 +
// (b3 & 0x3F), and by the walk's own terminating condition these sum to
// approximately rotationKeys) - a duration total might equal a frame count
// the rotation side also encodes.
//
// A SECOND, SMALLER QUESTION: the prior probe found byte == the key's own
// track-local index in 19.2% of keys against a 2.02% shuffled control, but
// flagged this could be a small-number coincidence (median byte is 2; small
// indices are also the most common indices) rather than a real relationship,
// since the control drew indices from the WHOLE track-length range while
// both marginals concentrate near zero. This probe stratifies that same
// match rate by index value (0-7 / 8-31 / 32+): if the excess over control
// is confined to small indices and vanishes at large ones, that is the
// coincidence explanation, not an index relationship, and this probe says so
// plainly if that is what the numbers show.
//
// Data source: the existing reader only - vpp::Container, sr3anim::
// Animation::parse per .anim_pc entry, sr3anim::Payload::walk for track
// layout - restricted to the same clean, oracle-confirmed population as the
// two probes this copies scaffolding from: clips WITHOUT flags bit 0x40
// (Sec6c.4) whose walk both completes and lands exactly on the file's
// declared end (Sec6c.1). This file only reads; it modifies no existing
// file, reader, or probe.
//
// Usage: probe_anim_fourth_duration <archive.vpp_pc>
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
long long g_skipped_oob = 0;
long long g_skipped_control_walk_short = 0;

// translationKeys == 1: sum(byte) still computed (used in the rotation
// correlation below) but EXCLUDED from the headline within-clip spread test
// - a lone value cannot show "spread" against itself, and folding a
// degenerate single-value track into a clip's min/max would let one track
// dominate the statistic for reasons that have nothing to do with duration.
long long g_tracksOneKey = 0;

// ---- flattened per-track sums (headline tracks only, translationKeys>=2),
// appended clip-by-clip, so each clip's group occupies a contiguous run
// [start, start+len) - used to build CONTROL 1 (cross-clip) after the whole
// archive has been scanned.
std::vector<double> g_flatByteSum;
std::vector<size_t> g_flatGroupStart;
std::vector<size_t> g_flatGroupLen;

// ---- within-clip spread distributions, one entry per qualifying clip -----
std::vector<double> g_relSpreadByte, g_cvByte;           // REAL reading
std::vector<double> g_relSpreadAbsDx, g_cvAbsDx;         // CONTROL 2 (shuffle quantity)
std::vector<double> g_relSpreadCrossClip, g_cvCrossClip; // CONTROL 1 (cross-clip)

long long g_clipsHeadlineGE2 = 0;     // clips with >=2 headline tracks: usable
long long g_clipsHeadlineLT2 = 0;     // clips with 0-1 headline tracks: excluded, reported
long long g_clipsZeroMeanSkipped = 0; // degenerate: mean byte sum == 0 across the group

// ---- rotation correlation, pooled over every qualifying track ------------
std::vector<double> g_trackByteSumAll;
std::vector<double> g_trackRotationKeysAll;
std::vector<double> g_trackRotationSpanSumAll;
long long g_eqRotationKeys = 0;
long long g_eqRotationSpanSum = 0;
long long g_rotationSpanSumEqualsKeys = 0;
long long g_rotationSpanSumTracks = 0; // denominator for the equality rate above
std::vector<double> g_rotationSpanOvershoot; // spanSum - rotationKeys, when > 0

// ---- Part B: stratified index-coincidence check ---------------------------
std::vector<double> g_byteU;            // per-key extra byte, unsigned
std::vector<double> g_idxInTrackForKey; // per-key 0-based index within its track

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}
double stddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double m = mean(v);
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size()));
}
double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1))];
}
double fracBelow(const std::vector<double>& v, double thresh) {
    if (v.empty()) return 0.0;
    long long c = 0;
    for (double x : v) if (x < thresh) ++c;
    return 100.0 * static_cast<double>(c) / static_cast<double>(v.size());
}
double pearson(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = x.size();
    if (n < 2 || y.size() != n) return 0.0;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sx += x[i]; sy += y[i];
        sxx += x[i] * x[i]; syy += y[i] * y[i];
        sxy += x[i] * y[i];
    }
    const double num = static_cast<double>(n) * sxy - sx * sy;
    const double den = std::sqrt((static_cast<double>(n) * sxx - sx * sx) *
                                  (static_cast<double>(n) * syy - sy * sy));
    return den > 0 ? num / den : 0.0;
}

struct TrackResult {
    bool ok = false;
    uint32_t translationKeys = 0;
    double byteSum = 0.0;
    double absDxSum = 0.0;
    double rotationKeys = 0.0;
    double rotationSpanSum = 0.0;
};

TrackResult doTrack(vpp::ByteView bv, const sr3anim::TrackBlock& blk) {
    TrackResult tr;
    if (blk.translationKeys == 0) { ++g_skipped_no_keys; return tr; }
    if (blk.translationControlStride != 8) {
        // Sec6c.1: the 0x20 wide-translation-record branch is disassembly-
        // only and UNEXERCISED in this archive (0 clips per the fourth-byte
        // probe). Reported, not silently dropped.
        ++g_skipped_wide_translation_stride;
        return tr;
    }
    if (blk.translationControlCount == 0) { ++g_skipped_control_walk_short; return tr; }

    const size_t sampleBase = blk.translationSampleOffset;
    const size_t extraBase = sampleBase + blk.translationKeys * 3;
    if (extraBase + blk.translationKeys > bv.size()) { ++g_skipped_oob; return tr; }

    std::vector<uint8_t> trackBytes(blk.translationKeys);
    std::vector<int8_t> tdx(blk.translationKeys);
    for (size_t k = 0; k < blk.translationKeys; ++k) {
        const size_t sa = sampleBase + k * 3;
        tdx[k] = static_cast<int8_t>(bv.at(sa + 0));
        trackBytes[k] = bv.at(extraBase + k);
    }

    // Walk the control records purely to confirm the run structure covers
    // every key - the same QC gate probe_anim_fourth_byte.cpp applies. This
    // probe needs no per-run boundary, only the pass/fail of the walk.
    size_t key = 0;
    for (size_t r = 0; r < blk.translationControlCount && key < blk.translationKeys; ++r) {
        const size_t at = blk.translationControlOffset + r * blk.translationControlStride;
        if (at + 8 > bv.size()) break;
        const uint32_t span = bv.readU16LE(at);
        if (span == 0) break;
        for (uint32_t s = 0; s < span && key < blk.translationKeys; ++s, ++key) {}
    }
    if (key != blk.translationKeys) { ++g_skipped_control_walk_short; return tr; }

    ++g_tracks;
    double byteSum = 0.0, absDxSum = 0.0;
    for (size_t k = 0; k < blk.translationKeys; ++k) {
        byteSum += static_cast<double>(trackBytes[k]);
        absDxSum += static_cast<double>(std::abs(static_cast<int>(tdx[k])));
        g_byteU.push_back(static_cast<double>(trackBytes[k]));
        g_idxInTrackForKey.push_back(static_cast<double>(k));
    }

    // Rotation side: walk the SAME rotation control records the Payload walk
    // already validated (rotationControlOffset/Count come from that walk),
    // summing spans = 1 + (byte3 & 0x3F) - spec Sec6c.1's own terminating
    // rule for rotKeys, replayed here to see whether it lands on rotationKeys
    // exactly or overshoots on the last record.
    double rotationSpanSum = 0.0;
    for (size_t r = 0; r < blk.rotationControlCount; ++r) {
        const size_t at = blk.rotationControlOffset + r * 4;
        if (at + 4 > bv.size()) break;
        const uint8_t b3 = bv.at(at + 3);
        rotationSpanSum += static_cast<double>(1 + (b3 & 0x3F));
    }

    tr.ok = true;
    tr.translationKeys = static_cast<uint32_t>(blk.translationKeys);
    tr.byteSum = byteSum;
    tr.absDxSum = absDxSum;
    tr.rotationKeys = static_cast<double>(blk.rotationKeys);
    tr.rotationSpanSum = rotationSpanSum;
    return tr;
}

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { ++g_skipped_parse_failed; return; }
    // Only the clean, oracle-confirmed population: clips without flags 0x40
    // whose walk lands on the declared end (Sec6c.1 / Sec6c.4).
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) { ++g_skipped_extra_payload_flag; return; }
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) { ++g_skipped_walk_incomplete_or_off_end; return; }
    ++g_clips;

    std::vector<double> headlineByteSum, headlineAbsDxSum;
    for (const auto& blk : pl.tracks()) {
        TrackResult tr = doTrack(bv, blk);
        if (!tr.ok) continue;

        g_trackByteSumAll.push_back(tr.byteSum);
        g_trackRotationKeysAll.push_back(tr.rotationKeys);
        g_trackRotationSpanSumAll.push_back(tr.rotationSpanSum);
        if (tr.byteSum == tr.rotationKeys) ++g_eqRotationKeys;
        ++g_rotationSpanSumTracks;
        if (tr.byteSum == tr.rotationSpanSum) ++g_eqRotationSpanSum;
        if (tr.rotationSpanSum == tr.rotationKeys) ++g_rotationSpanSumEqualsKeys;
        else if (tr.rotationSpanSum > tr.rotationKeys) g_rotationSpanOvershoot.push_back(tr.rotationSpanSum - tr.rotationKeys);

        if (tr.translationKeys == 1) { ++g_tracksOneKey; continue; }
        headlineByteSum.push_back(tr.byteSum);
        headlineAbsDxSum.push_back(tr.absDxSum);
    }

    if (headlineByteSum.size() < 2) { ++g_clipsHeadlineLT2; return; }
    ++g_clipsHeadlineGE2;

    const double m = mean(headlineByteSum);
    if (m <= 0.0) { ++g_clipsZeroMeanSkipped; return; }
    const double mn = *std::min_element(headlineByteSum.begin(), headlineByteSum.end());
    const double mx = *std::max_element(headlineByteSum.begin(), headlineByteSum.end());
    g_relSpreadByte.push_back((mx - mn) / m);
    g_cvByte.push_back(stddev(headlineByteSum) / m);

    const double m2 = mean(headlineAbsDxSum);
    if (m2 > 0.0) {
        const double mn2 = *std::min_element(headlineAbsDxSum.begin(), headlineAbsDxSum.end());
        const double mx2 = *std::max_element(headlineAbsDxSum.begin(), headlineAbsDxSum.end());
        g_relSpreadAbsDx.push_back((mx2 - mn2) / m2);
        g_cvAbsDx.push_back(stddev(headlineAbsDxSum) / m2);
    }

    g_flatGroupStart.push_back(g_flatByteSum.size());
    g_flatGroupLen.push_back(headlineByteSum.size());
    for (double v : headlineByteSum) g_flatByteSum.push_back(v);
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            ++g_anim_pc_seen;
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) doClip(b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkArchive(c.openNested(i)); } catch (...) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walkArchive(c);
        } catch (const std::exception& e) { printf("%s\n", e.what()); }
        printf("scanned %s\n", argv[i]);
    }

    // CONTROL 1 (cross-clip): for each real clip's headline group (size k,
    // flattened start s), build a same-size group from a DIFFERENT part of
    // the flattened pool, offset by half the pool's total length - the same
    // "unrelated pairing" technique probe_anim_fourth_byte.cpp and
    // probe_anim_delta_mode.cpp use for their own shuffle controls. This
    // says what "unrelated per-track totals, same group sizes" looks like.
    {
        const size_t N = g_flatByteSum.size();
        if (N > 0) {
            for (size_t gi = 0; gi < g_flatGroupStart.size(); ++gi) {
                const size_t s = g_flatGroupStart[gi];
                const size_t k = g_flatGroupLen[gi];
                std::vector<double> ctlGroup(k);
                for (size_t j = 0; j < k; ++j) ctlGroup[j] = g_flatByteSum[(s + j + N / 2) % N];
                const double cm = mean(ctlGroup);
                if (cm <= 0.0) continue;
                const double cmn = *std::min_element(ctlGroup.begin(), ctlGroup.end());
                const double cmx = *std::max_element(ctlGroup.begin(), ctlGroup.end());
                g_relSpreadCrossClip.push_back((cmx - cmn) / cm);
                g_cvCrossClip.push_back(stddev(ctlGroup) / cm);
            }
        }
    }

    printf("\n=== is the fourth output lane (spec Sec6c.6 item 2) a PER-KEY"
           " DURATION? ===\n");

    printf("\n--- population and skips ---\n");
    printf(".anim_pc entries seen (all archives): %lld\n", g_anim_pc_seen);
    printf("  skipped, parse failed: %lld\n", g_skipped_parse_failed);
    printf("  skipped, flags & 0x40 (unaccounted payload, Sec6c.4): %lld\n",
           g_skipped_extra_payload_flag);
    printf("  skipped, walk incomplete or missed declared end: %lld\n",
           g_skipped_walk_incomplete_or_off_end);
    printf("clean clips used: %d\n", g_clips);
    printf("tracks used (passed all QC gates below): %d\n", g_tracks);
    printf("  tracks skipped, translationKeys == 0: %lld\n", g_skipped_no_keys);
    printf("  tracks skipped, wide (0x20) translation stride (unexercised branch): %lld\n",
           g_skipped_wide_translation_stride);
    printf("  tracks skipped, control-record walk did not cover all keys: %lld\n",
           g_skipped_control_walk_short);
    printf("  tracks skipped, extra-byte region out of file bounds: %lld\n", g_skipped_oob);
    printf("translation keys examined (population N for Part B below): %zu\n", g_byteU.size());
    printf("tracks with translationKeys == 1 (sum computed, EXCLUDED from the\n"
           "  headline spread test below - a lone value has no spread against\n"
           "  itself): %lld / %d (%.2f%%)\n",
           g_tracksOneKey, g_tracks,
           g_tracks ? 100.0 * static_cast<double>(g_tracksOneKey) / static_cast<double>(g_tracks) : 0.0);
    printf("clips with >=2 headline tracks (usable for the spread test): %lld\n", g_clipsHeadlineGE2);
    printf("clips with < 2 headline tracks (excluded, reported not silently dropped): %lld\n",
           g_clipsHeadlineLT2);
    printf("clips skipped, headline byte-sum mean == 0 (degenerate, div-by-zero guard): %lld\n",
           g_clipsZeroMeanSkipped);

    printf("\n--- THE DECISIVE TEST: within-clip spread of sum(byte) per track ---\n");
    printf("predicate: (max-min)/mean and stddev/mean of per-track sum(byte),\n"
           "computed across the headline tracks of ONE clip, distribution built\n"
           "across clips.\n");
    printf("FAILING CASE for the hypothesis: real (within-clip) relative spread is\n"
           "NOT dramatically lower than CONTROL 1 (cross-clip, unrelated totals) -\n"
           "that would mean same-clip membership predicts nothing about how close\n"
           "two tracks' byte totals are, refuting the duration reading outright.\n"
           "SEPARATELY, if CONTROL 2 (sum of |dx|, a known non-duration per-key\n"
           "quantity, same tracks) is ALSO low-spread within-clip, low spread is an\n"
           "artefact of track-length / population structure rather than evidence\n"
           "specific to this byte - the test has NOT discriminated, and that is the\n"
           "single most valuable finding this probe could produce either way.\n\n");

    printf("%-42s %8s %8s %8s %8s %8s %8s\n", "", "n", "med", "p90", "<0.01", "<0.05", "<0.25");
    struct Row { const char* name; std::vector<double>* v; };
    Row rows[] = {
        {"REAL: sum(byte), within-clip", &g_relSpreadByte},
        {"CONTROL 1: sum(byte), cross-clip", &g_relSpreadCrossClip},
        {"CONTROL 2: sum(|dx|), within-clip", &g_relSpreadAbsDx},
    };
    for (auto& r : rows) {
        printf("%-42s %8zu %8.4f %8.4f %7.2f%% %7.2f%% %7.2f%%\n",
               r.name, r.v->size(), pct(*r.v, 0.50), pct(*r.v, 0.90),
               fracBelow(*r.v, 0.01), fracBelow(*r.v, 0.05), fracBelow(*r.v, 0.25));
    }
    printf("\ncoefficient of variation (stddev/mean), same three groups:\n");
    printf("%-42s %8s %8s %8s\n", "", "n", "med", "p90");
    Row cvRows[] = {
        {"REAL: sum(byte), within-clip", &g_cvByte},
        {"CONTROL 1: sum(byte), cross-clip", &g_cvCrossClip},
        {"CONTROL 2: sum(|dx|), within-clip", &g_cvAbsDx},
    };
    for (auto& r : cvRows)
        printf("%-42s %8zu %8.4f %8.4f\n", r.name, r.v->size(), pct(*r.v, 0.50), pct(*r.v, 0.90));

    {
        const double realMed = pct(g_relSpreadByte, 0.50);
        const double crossMed = pct(g_relSpreadCrossClip, 0.50);
        const double ctl2Med = pct(g_relSpreadAbsDx, 0.50);
        printf("\nseparation, median relative spread:\n");
        printf("  cross-clip control / real  = %.2fx  (>>1 favours the duration reading)\n",
               realMed > 0 ? crossMed / realMed : 0.0);
        printf("  control-2 (|dx|) / real    = %.2fx  (near 1 means control 2 is ALSO low"
               " - test did not discriminate)\n",
               realMed > 0 ? ctl2Med / realMed : 0.0);
        printf("\nauto-verdict (mechanical threshold on the ratios above, stated so the"
               " logic is auditable - read the numbers yourself before trusting this line):\n");
        const double crossRatio = realMed > 0 ? crossMed / realMed : 0.0;
        const double ctl2Ratio = realMed > 0 ? ctl2Med / realMed : 0.0;
        if (ctl2Ratio < 2.0 && ctl2Ratio > 0.0) {
            printf("  NOT DISCRIMINATED - control 2 (|dx| sums) shows within-clip spread"
                   " within 2x of the real reading's, so low spread here cannot be"
                   " attributed to duration semantics specifically.\n");
        } else if (crossRatio < 2.0) {
            printf("  REFUTED - within-clip spread is not meaningfully lower than the"
                   " cross-clip (unrelated totals) control.\n");
        } else {
            printf("  SUPPORTED - within-clip spread is dramatically lower than both the"
                   " cross-clip control and the |dx| shuffle control.\n");
        }
    }

    printf("\n--- does sum(byte) match a frame count the ROTATION side also encodes? ---\n");
    printf("predicate: Pearson r between a track's sum(byte) and (a) its rotationKeys,\n"
           "(b) the rotation control records' own span sum (span = 1 + (b3 & 0x3F),\n"
           "spec Sec6c.1's terminating rule for rotKeys, replayed independently here).\n");
    printf("failing case: r near 0 and exact-equality rate near 0 for both.\n");
    printf("tracks in this pool (both 1-key and headline tracks): %zu\n", g_trackByteSumAll.size());
    printf("r(sum(byte), rotationKeys)      = %+.4f   n=%zu\n",
           pearson(g_trackByteSumAll, g_trackRotationKeysAll), g_trackByteSumAll.size());
    printf("r(sum(byte), rotation span sum) = %+.4f   n=%zu\n",
           pearson(g_trackByteSumAll, g_trackRotationSpanSumAll), g_trackByteSumAll.size());
    printf("sum(byte) == rotationKeys       : %lld / %zu (%.4f%%)\n",
           g_eqRotationKeys, g_trackByteSumAll.size(),
           g_trackByteSumAll.size() ? 100.0 * static_cast<double>(g_eqRotationKeys) /
                                          static_cast<double>(g_trackByteSumAll.size())
                                    : 0.0);
    printf("sum(byte) == rotation span sum  : %lld / %zu (%.4f%%)\n",
           g_eqRotationSpanSum, g_trackByteSumAll.size(),
           g_trackByteSumAll.size() ? 100.0 * static_cast<double>(g_eqRotationSpanSum) /
                                          static_cast<double>(g_trackByteSumAll.size())
                                    : 0.0);
    printf("\n(sanity check on the rotation span sum itself: by construction the walk's\n"
           "own terminating rule stops once the accumulated span >= rotationKeys, so\n"
           "the span sum should equal rotationKeys almost always and can only OVERSHOOT,\n"
           "never fall short - if it correlates with sum(byte) about as strongly as\n"
           "rotationKeys itself does, that is the SAME information, not independent\n"
           "corroboration.)\n");
    printf("rotation span sum == rotationKeys exactly : %lld / %lld (%.2f%%)\n",
           g_rotationSpanSumEqualsKeys, g_rotationSpanSumTracks,
           g_rotationSpanSumTracks ? 100.0 * static_cast<double>(g_rotationSpanSumEqualsKeys) /
                                          static_cast<double>(g_rotationSpanSumTracks)
                                    : 0.0);
    printf("  overshoot (span sum > rotationKeys), median / p90 / n : %.2f / %.2f / %zu\n",
           pct(g_rotationSpanOvershoot, 0.50), pct(g_rotationSpanOvershoot, 0.90),
           g_rotationSpanOvershoot.size());

    printf("\n=== SECOND QUESTION: is byte==trackLocalIndex (19.2%% / control 2.02%%"
           " previously) a small-number coincidence? ===\n");
    printf("predicate: stratify the SAME equality test (byte[k] == k, k = 0-based index\n"
           "within the track) by the value of k itself, bucketed 0-7 / 8-31 / 32+, against\n"
           "the SAME unrelated-key control the prior probe used (byte[i] paired with an\n"
           "UNRELATED key's index, offset by half the population).\n");
    printf("failing case for the coincidence explanation: the real rate stays clearly\n"
           "above the control rate at 32+ as well as at 0-7 - i.e. a real relationship\n"
           "that survives once both distributions stop concentrating near zero.\n"
           "Conversely, if the real rate collapses onto the control rate at 32+ while\n"
           "only 0-7 shows a gap, that IS the coincidence: two independently-small\n"
           "numbers agreeing more often near zero, with no per-key relationship at all.\n");
    {
        struct Bucket { long long n = 0, matches = 0; };
        Bucket realB[3], ctlB[3];
        auto bucketOf = [](double idx) -> int {
            if (idx <= 7.0) return 0;
            if (idx <= 31.0) return 1;
            return 2;
        };
        const size_t n = g_byteU.size();
        long long totalReal = 0, totalCtl = 0;
        for (size_t i = 0; i < n; ++i) {
            const double idx = g_idxInTrackForKey[i];
            const int b = bucketOf(idx);
            ++realB[b].n;
            if (g_byteU[i] == idx) { ++realB[b].matches; ++totalReal; }

            const size_t j = (i + n / 2) % n;
            const double cidx = g_idxInTrackForKey[j];
            const int cb = bucketOf(cidx);
            ++ctlB[cb].n;
            if (g_byteU[i] == cidx) { ++ctlB[cb].matches; ++totalCtl; }
        }
        printf("\noverall (sanity check against the prior probe's pooled figures):\n");
        printf("  byte == own track index            : %lld / %zu (%.4f%%)\n",
               totalReal, n, n ? 100.0 * static_cast<double>(totalReal) / static_cast<double>(n) : 0.0);
        printf("  CONTROL, unrelated key's index      : %lld / %zu (%.4f%%)\n",
               totalCtl, n, n ? 100.0 * static_cast<double>(totalCtl) / static_cast<double>(n) : 0.0);
        printf("\nstratified by index value (bucket population is identical for real and\n"
               "control by construction - the control is a permutation of the same index\n"
               "values, just reassigned to different keys):\n");
        printf("%-10s %10s %14s %10s %14s %10s\n", "bucket", "n", "real matches",
               "real %", "ctl matches", "ctl %");
        const char* labels[3] = {"0-7", "8-31", "32+"};
        for (int b = 0; b < 3; ++b) {
            printf("%-10s %10lld %14lld %9.4f%% %14lld %9.4f%%\n", labels[b], realB[b].n,
                   realB[b].matches, realB[b].n ? 100.0 * static_cast<double>(realB[b].matches) /
                                                       static_cast<double>(realB[b].n)
                                                 : 0.0,
                   ctlB[b].matches, ctlB[b].n ? 100.0 * static_cast<double>(ctlB[b].matches) /
                                                     static_cast<double>(ctlB[b].n)
                                               : 0.0);
        }
        printf("\n(if the 32+ row shows real%% and ctl%% nearly equal while 0-7 shows a large\n"
               "gap, the 19.2%%-vs-2.02%% headline figure is driven entirely by the small-\n"
               "index bucket, i.e. is the small-number coincidence the background warns\n"
               "about, not an index relationship - say so plainly.)\n");
    }

    printf("\n=== end of measurements. Verdict lines above are mechanical threshold"
           " checks on the printed numbers, not a substitute for reading them. ===\n");
    return 0;
}
