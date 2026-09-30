// What is the extra byte that follows the three translation-key deltas?
//
// spec-anim-format.md Sec6c.1's per-track walk (confirmed) ends translation
// samples with:
//     p += transKeys * 3   // three signed-byte axis deltas per key
//     p += transKeys       // ONE MORE BYTE PER KEY - meaning unknown
// Sec6c.6 item 2 calls this "the fourth output lane" and records it OPEN:
// the decoder writes four floats per key, axis = base/64 + delta/4000 is
// confirmed for the first three, and what the fourth carries is unread.
//
// This harness does not guess an answer. It characterises the byte against
// several falsifiable hypotheses and reports the actual numbers, including
// what a FAILING case would have looked like for each - a check that cannot
// fail proves nothing (HANDOFF's own lesson from the vacuous rotation
// checks, restated in payload.h's header comment).
//
// Hypotheses tested, each against its own failing case:
//   H1  Degenerate distribution - mostly a single value (probably zero).
//       Failing case: values spread roughly uniformly over 0-255.
//   H2  Per-run property (constant across a control record's span), not
//       per-key. Failing case: a run's bytes vary as much as an arbitrary
//       same-length window that ignores run boundaries - i.e. no more
//       constant than the population's own bias predicts.
//   H3  Time/frame index within the track (monotonic, or literally equal to
//       the key's position). Failing case: monotonic rate no higher than a
//       known non-index per-key signal (the sample delta bytes) gets on the
//       same tracks, and equality-to-index rate no higher than chance
//       (1/256 for a uniform byte against a fixed small index).
//   H4  Shared exponent/scale selector correlated with delta magnitude.
//       Failing case: Pearson r near 0 against |delta| magnitude measures.
//   H5  Index-like bound (never exceeds the track's key count).
//       Failing case: values exceed translationKeys about as often as a
//       uniform byte would given the track's key count.
//
// Data source: the existing reader only - vpp::Container to open the
// archive, sr3anim::Animation::parse per .anim_pc entry, sr3anim::Payload::
// walk for the track layout. Only the clean, oracle-confirmed population is
// used: clips without flags bit 0x40 (Sec6c.4) whose walk both completes and
// lands exactly on the file's declared end (Sec6c.1). Mixing in the other
// population would contaminate every figure below, exactly as it would for
// probe_anim_delta_mode.cpp, whose scaffolding this copies.
//
// Usage: probe_anim_fourth_byte <archive.vpp_pc>
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
long long g_skipped_control_walk_short = 0; // runs did not cover all keys

// ---- per-key accumulators (population for every measurement below) ------
std::vector<double> g_byteU;      // byte value, unsigned 0..255
std::vector<double> g_byteS;      // same byte, reinterpreted signed -128..127
std::vector<double> g_mag;        // sqrt(dx^2+dy^2+dz^2)
std::vector<double> g_maxabs;     // max(|dx|,|dy|,|dz|)
std::vector<double> g_absdx, g_absdy, g_absdz;
std::vector<double> g_runSpanForKey;
std::vector<double> g_trackKeyCountForKey; // this key's track's translationKeys

long long g_byteEqIdxInTrack = 0;
long long g_byteEqIdxInRun = 0;
long long g_byteExceedsKeyCount = 0;   // byte > translationKeys of its track
long long g_keysWithKeyCountLE255 = 0; // denominator for the exceed test being meaningful
long long g_eqMaxAbs = 0;
long long g_eqSumMod256 = 0;
long long g_eqXor = 0;
std::vector<double> g_idxInTrackForKey, g_idxInRunForKey; // for a correlation, not just an equality count
long long g_keysWithIdxInTrackLE255 = 0;   // denominator where byte==idx is even reachable
long long g_byteEqIdxInTrack_feasible = 0; // numerator restricted to that denominator
long long g_keysWithIdxInRunLE255 = 0;
long long g_byteEqIdxInRun_feasible = 0;
// Reverse (countdown) index candidates, added because H4's correlation signs
// (negative against both idxInTrack and runSpan) do not fit a simple
// ascending index but are the shape a countdown would produce.
long long g_byteEqRevIdxInTrack = 0; // byte == translationKeys-1-idxInTrack
long long g_byteEqRevIdxInRun = 0;   // byte == runSpan-1-idxInRun
std::vector<double> g_revIdxInTrackForKey, g_revIdxInRunForKey;

// ---- per-run constancy (H2) ----------------------------------------------
long long g_runsGE2 = 0, g_runsGE2Constant = 0;
long long g_ctlWindowsGE2 = 0, g_ctlWindowsGE2Constant = 0;

// ---- per-track monotonicity (H3) -----------------------------------------
long long g_tracksGE2 = 0;
long long g_tracksNonDecreasing = 0, g_tracksNonIncreasing = 0;
long long g_tracksAllSameByte = 0;      // makes non-decreasing/non-increasing trivial
long long g_dxTracksNonDecreasing = 0;  // control: same test on a KNOWN non-index signal
long long g_rotTracksNonDecreasing = 0; // control: SAME multiset, order destroyed by rotation
std::vector<double> g_distinctPerTrack; // how many distinct byte values a track shows
std::vector<double> g_trackKeyCounts;   // translationKeys, one entry per track

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

void doTrack(vpp::ByteView bv, const sr3anim::TrackBlock& blk) {
    if (blk.translationKeys == 0) { ++g_skipped_no_keys; return; }
    if (blk.translationControlStride != 8) {
        // Sec6c.1: the 0x20 wide-translation-record branch is disassembly-only
        // and UNEXERCISED in this archive (0 clips). Reported, not silently
        // dropped, so a nonzero count here would itself be a finding.
        ++g_skipped_wide_translation_stride;
        return;
    }
    if (blk.translationControlCount == 0) { ++g_skipped_control_walk_short; return; }

    const size_t sampleBase = blk.translationSampleOffset;
    const size_t extraBase = sampleBase + blk.translationKeys * 3;
    if (extraBase + blk.translationKeys > bv.size()) { ++g_skipped_oob; return; }

    std::vector<uint8_t> trackBytes(blk.translationKeys);
    std::vector<int8_t> tdx(blk.translationKeys), tdy(blk.translationKeys), tdz(blk.translationKeys);
    for (size_t k = 0; k < blk.translationKeys; ++k) {
        const size_t sa = sampleBase + k * 3;
        tdx[k] = static_cast<int8_t>(bv.at(sa + 0));
        tdy[k] = static_cast<int8_t>(bv.at(sa + 1));
        tdz[k] = static_cast<int8_t>(bv.at(sa + 2));
        trackBytes[k] = bv.at(extraBase + k);
    }

    // Walk the control records to find run boundaries, exactly as
    // Payload::translations does (translationControlStride/8-byte records:
    // u16 span, i16 x, i16 y, i16 z). Only the span is needed here.
    std::vector<std::pair<size_t, size_t>> runs; // (start key index, length)
    size_t key = 0;
    for (size_t r = 0; r < blk.translationControlCount && key < blk.translationKeys; ++r) {
        const size_t at = blk.translationControlOffset + r * blk.translationControlStride;
        if (at + 8 > bv.size()) break;
        const uint32_t span = bv.readU16LE(at);
        if (span == 0) break;
        const size_t start = key;
        size_t len = 0;
        for (uint32_t s = 0; s < span && key < blk.translationKeys; ++s, ++key) ++len;
        if (len > 0) runs.push_back({start, len});
    }
    if (key != blk.translationKeys) { ++g_skipped_control_walk_short; return; }

    ++g_tracks;
    g_trackKeyCounts.push_back(static_cast<double>(blk.translationKeys));

    std::vector<uint32_t> idxInRun(blk.translationKeys, 0);
    std::vector<uint32_t> runSpanOf(blk.translationKeys, 0);
    for (auto& run : runs) {
        for (size_t i = 0; i < run.second; ++i) {
            idxInRun[run.first + i] = static_cast<uint32_t>(i);
            runSpanOf[run.first + i] = static_cast<uint32_t>(run.second);
        }
    }

    // H2: per-run constancy, plus a same-track control window of identical
    // length that ignores run boundaries. If the byte's marginal
    // distribution is skewed (e.g. mostly one value), an unaligned window
    // will ALSO look "constant" often just by that bias - so the control
    // must be judged on the same track, same length, not on a global rate.
    for (auto& run : runs) {
        const size_t start = run.first, len = run.second;
        if (len < 2) continue; // a length-1 run is trivially "constant" - no information
        ++g_runsGE2;
        bool constant = true;
        const uint8_t v0 = trackBytes[start];
        for (size_t i = 1; i < len; ++i) if (trackBytes[start + i] != v0) { constant = false; break; }
        if (constant) ++g_runsGE2Constant;

        const size_t ctlStart = (start + blk.translationKeys / 2) % blk.translationKeys;
        ++g_ctlWindowsGE2;
        bool ctlConstant = true;
        const uint8_t cv0 = trackBytes[ctlStart];
        for (size_t i = 1; i < len; ++i) {
            const size_t idx = (ctlStart + i) % blk.translationKeys;
            if (trackBytes[idx] != cv0) { ctlConstant = false; break; }
        }
        if (ctlConstant) ++g_ctlWindowsGE2Constant;
    }

    // H3: monotonicity / index equality, whole-track.
    if (blk.translationKeys >= 2) {
        ++g_tracksGE2;
        bool nondec = true, noninc = true, allsame = true;
        for (size_t k = 1; k < blk.translationKeys; ++k) {
            if (trackBytes[k] < trackBytes[k - 1]) nondec = false;
            if (trackBytes[k] > trackBytes[k - 1]) noninc = false;
            if (trackBytes[k] != trackBytes[0]) allsame = false;
        }
        if (nondec) ++g_tracksNonDecreasing;
        if (noninc) ++g_tracksNonIncreasing;
        if (allsame) ++g_tracksAllSameByte;

        bool dxNondec = true;
        for (size_t k = 1; k < blk.translationKeys; ++k) if (tdx[k] < tdx[k - 1]) { dxNondec = false; break; }
        if (dxNondec) ++g_dxTracksNonDecreasing;

        // A FAIRER control than dx: dx has a different marginal distribution
        // (near-uniform signed byte) than the extra byte (heavily skewed,
        // many ties), so a monotonic rate measured on dx is not a like-for-
        // like baseline. Rotate THIS track's own extra-byte values by half
        // its length - same multiset, same tie structure, order destroyed -
        // and run the identical non-decreasing test on that.
        {
            const size_t len = blk.translationKeys;
            bool rotNondec = true;
            uint8_t prev = trackBytes[len / 2];
            for (size_t i = 1; i < len; ++i) {
                const uint8_t cur = trackBytes[(len / 2 + i) % len];
                if (cur < prev) { rotNondec = false; break; }
                prev = cur;
            }
            if (rotNondec) ++g_rotTracksNonDecreasing;
        }

        std::vector<uint8_t> uniq(trackBytes.begin(), trackBytes.end());
        std::sort(uniq.begin(), uniq.end());
        uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
        g_distinctPerTrack.push_back(static_cast<double>(uniq.size()));
    }

    // Per-key accumulation for the rest of the measurements (H1, H3 index
    // equality, H4 correlation, H5 bound).
    const bool keyCountLE255 = blk.translationKeys <= 255;
    for (size_t k = 0; k < blk.translationKeys; ++k) {
        const uint8_t b = trackBytes[k];
        g_byteU.push_back(static_cast<double>(b));
        g_byteS.push_back(static_cast<double>(static_cast<int8_t>(b)));

        const int adx = std::abs(static_cast<int>(tdx[k]));
        const int ady = std::abs(static_cast<int>(tdy[k]));
        const int adz = std::abs(static_cast<int>(tdz[k]));
        const double mag = std::sqrt(static_cast<double>(adx) * adx +
                                      static_cast<double>(ady) * ady +
                                      static_cast<double>(adz) * adz);
        const int maxabs = std::max({adx, ady, adz});
        g_mag.push_back(mag);
        g_maxabs.push_back(static_cast<double>(maxabs));
        g_absdx.push_back(static_cast<double>(adx));
        g_absdy.push_back(static_cast<double>(ady));
        g_absdz.push_back(static_cast<double>(adz));
        g_runSpanForKey.push_back(static_cast<double>(runSpanOf[k]));
        g_trackKeyCountForKey.push_back(static_cast<double>(blk.translationKeys));

        if (static_cast<uint32_t>(b) == static_cast<uint32_t>(k)) ++g_byteEqIdxInTrack;
        if (static_cast<uint32_t>(b) == idxInRun[k]) ++g_byteEqIdxInRun;
        g_idxInTrackForKey.push_back(static_cast<double>(k));
        g_idxInRunForKey.push_back(static_cast<double>(idxInRun[k]));
        if (k <= 255) {
            ++g_keysWithIdxInTrackLE255;
            if (static_cast<uint32_t>(b) == static_cast<uint32_t>(k)) ++g_byteEqIdxInTrack_feasible;
        }
        if (idxInRun[k] <= 255) {
            ++g_keysWithIdxInRunLE255;
            if (static_cast<uint32_t>(b) == idxInRun[k]) ++g_byteEqIdxInRun_feasible;
        }
        {
            const uint32_t revTrack = static_cast<uint32_t>(blk.translationKeys) - 1 - static_cast<uint32_t>(k);
            const uint32_t revRun = runSpanOf[k] - 1 - idxInRun[k];
            if (static_cast<uint32_t>(b) == revTrack) ++g_byteEqRevIdxInTrack;
            if (static_cast<uint32_t>(b) == revRun) ++g_byteEqRevIdxInRun;
            g_revIdxInTrackForKey.push_back(static_cast<double>(revTrack));
            g_revIdxInRunForKey.push_back(static_cast<double>(revRun));
        }
        if (keyCountLE255) {
            ++g_keysWithKeyCountLE255;
            if (static_cast<uint32_t>(b) > blk.translationKeys) ++g_byteExceedsKeyCount;
        }
        if (b == static_cast<uint8_t>(maxabs)) ++g_eqMaxAbs;
        const uint8_t sumMod = static_cast<uint8_t>(tdx[k] + tdy[k] + tdz[k]);
        if (b == sumMod) ++g_eqSumMod256;
        const uint8_t xorv = static_cast<uint8_t>(tdx[k] ^ tdy[k] ^ tdz[k]);
        if (b == xorv) ++g_eqXor;
    }
}

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { ++g_skipped_parse_failed; return; }
    // Only the clean, oracle-confirmed population: clips without flags 0x40
    // whose walk lands on the declared end (Sec6c.1 / Sec6c.4). Mixing in
    // clips whose payload is not fully accounted for would contaminate
    // every figure below.
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) { ++g_skipped_extra_payload_flag; return; }
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) { ++g_skipped_walk_incomplete_or_off_end; return; }
    ++g_clips;
    for (const auto& blk : pl.tracks()) doTrack(bv, blk);
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

    printf("\n=== the fourth output lane: the extra byte per translation key"
           " (spec Sec6c.6 item 2) ===\n");

    printf("\n--- population and skips ---\n");
    printf(".anim_pc entries seen (all archives): %lld\n", g_anim_pc_seen);
    printf("  skipped, parse failed: %lld\n", g_skipped_parse_failed);
    printf("  skipped, flags & 0x40 (unaccounted payload, Sec6c.4): %lld\n",
           g_skipped_extra_payload_flag);
    printf("  skipped, walk incomplete or missed declared end: %lld\n",
           g_skipped_walk_incomplete_or_off_end);
    printf("clean clips used: %d\n", g_clips);
    printf("tracks used: %d\n", g_tracks);
    printf("  tracks skipped, translationKeys == 0: %lld\n", g_skipped_no_keys);
    printf("  tracks skipped, wide (0x20) translation stride (unexercised branch): %lld\n",
           g_skipped_wide_translation_stride);
    printf("  tracks skipped, control-record walk did not cover all keys: %lld\n",
           g_skipped_control_walk_short);
    printf("  tracks skipped, extra-byte region out of file bounds: %lld\n", g_skipped_oob);
    printf("translation keys examined (population N for every stat below): %zu\n",
           g_byteU.size());

    printf("\ntranslationKeys per track: median %.0f, p90 %.0f, max %.0f"
           " (n=%zu tracks)\n",
           pct(g_trackKeyCounts, 0.50), pct(g_trackKeyCounts, 0.90),
           pct(g_trackKeyCounts, 1.00), g_trackKeyCounts.size());

    // ---- H1: value distribution -----------------------------------------
    printf("\n--- H1: value distribution ---\n");
    printf("predicate: fraction of keys whose extra byte == 0.\n");
    long long zeroCount = 0, negCount = 0, posCount = 0, sZeroCount = 0;
    long long hist[11] = {0};
    const int edges[12] = {0, 1, 8, 16, 32, 64, 96, 128, 160, 192, 224, 256};
    const char* labels[11] = {"0", "1-7", "8-15", "16-31", "32-63", "64-95",
                               "96-127", "128-159", "160-191", "192-223", "224-255"};
    for (double d : g_byteU) {
        const int v = static_cast<int>(d);
        if (v == 0) ++zeroCount;
        for (int bkt = 0; bkt < 11; ++bkt) if (v >= edges[bkt] && v < edges[bkt + 1]) { ++hist[bkt]; break; }
    }
    for (double d : g_byteS) {
        if (d < 0) ++negCount; else if (d > 0) ++posCount; else ++sZeroCount;
    }
    const size_t N = g_byteU.size();
    printf("byte == 0 exactly: %lld / %zu (%.2f%%)\n", zeroCount, N,
           N ? 100.0 * static_cast<double>(zeroCount) / static_cast<double>(N) : 0.0);
    printf("unsigned (0-255) reading: min %.0f  max %.0f  mean %.3f  stddev %.3f\n",
           pct(g_byteU, 0.0), pct(g_byteU, 1.0), mean(g_byteU), stddev(g_byteU));
    printf("unsigned percentiles: p50 %.0f  p90 %.0f  p99 %.0f\n",
           pct(g_byteU, 0.50), pct(g_byteU, 0.90), pct(g_byteU, 0.99));
    printf("histogram over unsigned 0-255, %zu keys:\n", N);
    for (int bkt = 0; bkt < 11; ++bkt)
        printf("  %-9s %10lld  (%.2f%%)\n", labels[bkt], hist[bkt],
               N ? 100.0 * static_cast<double>(hist[bkt]) / static_cast<double>(N) : 0.0);
    printf("signed (int8) reading: negative %lld (%.2f%%)  zero %lld (%.2f%%)"
           "  positive %lld (%.2f%%)  mean %.3f\n",
           negCount, N ? 100.0 * static_cast<double>(negCount) / static_cast<double>(N) : 0.0,
           sZeroCount, N ? 100.0 * static_cast<double>(sZeroCount) / static_cast<double>(N) : 0.0,
           posCount, N ? 100.0 * static_cast<double>(posCount) / static_cast<double>(N) : 0.0,
           mean(g_byteS));
    printf("(a signed delta with the same physical role as the other three would be\n"
           " roughly symmetric about 0; a uniform-over-0-255 index or table selector\n"
           " would not concentrate at either the low or the high end of the unsigned\n"
           " histogram. Both are falsifiable from the numbers above, not asserted.)\n");

    // ---- H2: per-run constancy --------------------------------------------
    printf("\n--- H2: constant within a run (control record's span), not per-key ---\n");
    printf("predicate: for runs with span >= 2 (span 1 is trivially \"constant\","
           " zero information), are ALL keys in the run identical?\n");
    printf("failing case this can produce: run-constancy rate no higher than an\n"
           "unaligned same-length control window's rate, which is what the byte's\n"
           "own marginal distribution alone would predict.\n");
    printf("runs with span >= 2 : %lld,  fully constant : %lld  (%.2f%%)\n",
           g_runsGE2, g_runsGE2Constant,
           g_runsGE2 ? 100.0 * static_cast<double>(g_runsGE2Constant) / static_cast<double>(g_runsGE2) : 0.0);
    printf("control windows (same track, same length, ignores run boundary): %lld,"
           " fully constant : %lld  (%.2f%%)\n",
           g_ctlWindowsGE2, g_ctlWindowsGE2Constant,
           g_ctlWindowsGE2 ? 100.0 * static_cast<double>(g_ctlWindowsGE2Constant) / static_cast<double>(g_ctlWindowsGE2) : 0.0);
    {
        const double realRate = g_runsGE2 ? static_cast<double>(g_runsGE2Constant) / static_cast<double>(g_runsGE2) : 0.0;
        const double ctlRate = g_ctlWindowsGE2 ? static_cast<double>(g_ctlWindowsGE2Constant) / static_cast<double>(g_ctlWindowsGE2) : 0.0;
        printf("separation from control (real / control, >1 favours a per-run property): %.3fx\n",
               ctlRate > 0 ? realRate / ctlRate : 0.0);
    }

    // ---- H3: index / monotonic ---------------------------------------------
    printf("\n--- H3: time/frame index within the track ---\n");
    printf("predicate: is the byte sequence weakly monotonic across the WHOLE track"
           " (translationKeys >= 2), or literally equal to the key's own index?\n");
    printf("failing case: monotonic rate no higher than the SAME test run on the\n"
           "sample delta bytes (a per-key signal known to be motion, not an index),\n"
           "and equality-to-index rate near the 1/256 chance level for a uniform byte.\n");
    printf("tracks with >=2 keys: %lld\n", g_tracksGE2);
    printf("  all keys share one byte value (makes monotonicity trivial): %lld (%.2f%%)\n",
           g_tracksAllSameByte,
           g_tracksGE2 ? 100.0 * static_cast<double>(g_tracksAllSameByte) / static_cast<double>(g_tracksGE2) : 0.0);
    printf("  weakly non-decreasing: %lld (%.2f%%)\n", g_tracksNonDecreasing,
           g_tracksGE2 ? 100.0 * static_cast<double>(g_tracksNonDecreasing) / static_cast<double>(g_tracksGE2) : 0.0);
    printf("  weakly non-increasing: %lld (%.2f%%)\n", g_tracksNonIncreasing,
           g_tracksGE2 ? 100.0 * static_cast<double>(g_tracksNonIncreasing) / static_cast<double>(g_tracksGE2) : 0.0);
    printf("  CONTROL, same test on the dx sample-delta byte (different marginal"
           " distribution - weak control): %lld (%.2f%%)\n",
           g_dxTracksNonDecreasing,
           g_tracksGE2 ? 100.0 * static_cast<double>(g_dxTracksNonDecreasing) / static_cast<double>(g_tracksGE2) : 0.0);
    printf("  CONTROL, same track's OWN extra-byte values, order destroyed by a"
           " half-length rotation (same multiset/ties - fair control): %lld (%.2f%%)\n",
           g_rotTracksNonDecreasing,
           g_tracksGE2 ? 100.0 * static_cast<double>(g_rotTracksNonDecreasing) / static_cast<double>(g_tracksGE2) : 0.0);
    printf("  median distinct byte VALUES seen per track (of translationKeys keys): %.1f\n",
           pct(g_distinctPerTrack, 0.50));
    printf("byte == key's 0-based index within its track, ALL keys: %lld / %zu (%.4f%%)\n",
           g_byteEqIdxInTrack, N,
           N ? 100.0 * static_cast<double>(g_byteEqIdxInTrack) / static_cast<double>(N) : 0.0);
    printf("  same, restricted to keys whose index is <= 255 (byte could possibly"
           " match; the unrestricted figure is diluted by indices the byte cannot"
           " reach): %lld / %lld (%.4f%%)\n",
           g_byteEqIdxInTrack_feasible, g_keysWithIdxInTrackLE255,
           g_keysWithIdxInTrackLE255 ? 100.0 * static_cast<double>(g_byteEqIdxInTrack_feasible) /
                                            static_cast<double>(g_keysWithIdxInTrackLE255)
                                      : 0.0);
    // CONTROL for the equality test above, done properly: "chance level for a
    // uniform byte is ~0.39% (1/256)" is the WRONG baseline, because H1 above
    // already shows the byte is nowhere near uniform - it is heavily skewed
    // toward small values. Small indices are also the most common indices
    // (median track has 16 keys). Two independently-skewed-small variables
    // coincide far more than 1/256 by chance alone, with no relationship
    // between them at all. The honest control pairs each key's byte with an
    // UNRELATED key's index (offset by half the population, same technique
    // probe_anim_delta_mode.cpp uses for its cross-run control): same two
    // marginal distributions, no per-key relationship by construction.
    {
        const size_t n = g_byteU.size();
        long long shufTrack = 0, shufRun = 0;
        for (size_t i = 0; i < n; ++i) {
            const size_t j = (i + n / 2) % n;
            if (g_byteU[i] == g_idxInTrackForKey[j]) ++shufTrack;
            if (g_byteU[i] == g_idxInRunForKey[j]) ++shufRun;
        }
        printf("  CONTROL - byte[i] vs an UNRELATED key's track-index (same marginals,\n"
               "  no real pairing): %lld / %zu (%.4f%%)\n",
               shufTrack, n, n ? 100.0 * static_cast<double>(shufTrack) / static_cast<double>(n) : 0.0);
        printf("  CONTROL - byte[i] vs an UNRELATED key's in-run-index: %lld / %zu (%.4f%%)\n",
               shufRun, n, n ? 100.0 * static_cast<double>(shufRun) / static_cast<double>(n) : 0.0);
    }
    printf("byte == key's 0-based index within its RUN, ALL keys: %lld / %zu (%.4f%%)\n",
           g_byteEqIdxInRun, N,
           N ? 100.0 * static_cast<double>(g_byteEqIdxInRun) / static_cast<double>(N) : 0.0);
    printf("  same, restricted to in-run index <= 255: %lld / %lld (%.4f%%)\n",
           g_byteEqIdxInRun_feasible, g_keysWithIdxInRunLE255,
           g_keysWithIdxInRunLE255 ? 100.0 * static_cast<double>(g_byteEqIdxInRun_feasible) /
                                          static_cast<double>(g_keysWithIdxInRunLE255)
                                    : 0.0);
    printf("pooled Pearson r(byte unsigned, key's 0-based index within its track) = %+.4f   n=%zu\n",
           pearson(g_byteU, g_idxInTrackForKey), g_byteU.size());
    printf("pooled Pearson r(byte unsigned, key's 0-based index within its run)   = %+.4f   n=%zu\n",
           pearson(g_byteU, g_idxInRunForKey), g_byteU.size());
    printf("(this correlation, not the monotonic pass-rate above, is the number that\n"
           " survives tie inflation: a pass rate can be high just because many values\n"
           " repeat, but a real index relationship needs a real linear trend too. Note\n"
           " its SIGN against the raw equality rate above before concluding anything.)\n");
    printf("\nA NEGATIVE correlation against an ASCENDING index does not fit that index\n"
           "hypothesis, so two REVERSE (countdown) candidates are tested as well, each\n"
           "against the same unrelated-key control as above:\n");
    {
        const size_t n = g_byteU.size();
        long long shufRevTrack = 0, shufRevRun = 0;
        for (size_t i = 0; i < n; ++i) {
            const size_t j = (i + n / 2) % n;
            if (g_byteU[i] == g_revIdxInTrackForKey[j]) ++shufRevTrack;
            if (g_byteU[i] == g_revIdxInRunForKey[j]) ++shufRevRun;
        }
        printf("byte == (translationKeys - 1 - idxInTrack)         : %lld / %zu (%.4f%%)"
               "   CONTROL: %lld / %zu (%.4f%%)\n",
               g_byteEqRevIdxInTrack, n,
               n ? 100.0 * static_cast<double>(g_byteEqRevIdxInTrack) / static_cast<double>(n) : 0.0,
               shufRevTrack, n, n ? 100.0 * static_cast<double>(shufRevTrack) / static_cast<double>(n) : 0.0);
        printf("byte == (runSpan - 1 - idxInRun)                   : %lld / %zu (%.4f%%)"
               "   CONTROL: %lld / %zu (%.4f%%)\n",
               g_byteEqRevIdxInRun, n,
               n ? 100.0 * static_cast<double>(g_byteEqRevIdxInRun) / static_cast<double>(n) : 0.0,
               shufRevRun, n, n ? 100.0 * static_cast<double>(shufRevRun) / static_cast<double>(n) : 0.0);
    }
    printf("pooled Pearson r(byte unsigned, translationKeys-1-idxInTrack)         = %+.4f   n=%zu\n",
           pearson(g_byteU, g_revIdxInTrackForKey), g_byteU.size());
    printf("pooled Pearson r(byte unsigned, runSpan-1-idxInRun)                   = %+.4f   n=%zu\n",
           pearson(g_byteU, g_revIdxInRunForKey), g_byteU.size());

    // ---- H4: correlation with delta magnitude ------------------------------
    printf("\n--- H4: correlation with the magnitude of that key's 3 axis deltas ---\n");
    printf("predicate: Pearson correlation coefficient r between the extra byte and"
           " a magnitude measure of (dx,dy,dz). Population size given each time.\n");
    printf("failing case: r near 0.\n");
    printf("r(byte unsigned, sqrt(dx^2+dy^2+dz^2))            = %+.4f   n=%zu\n",
           pearson(g_byteU, g_mag), g_byteU.size());
    printf("r(byte signed(int8), sqrt(dx^2+dy^2+dz^2))        = %+.4f   n=%zu\n",
           pearson(g_byteS, g_mag), g_byteS.size());
    printf("r(byte unsigned, max(|dx|,|dy|,|dz|))             = %+.4f   n=%zu\n",
           pearson(g_byteU, g_maxabs), g_byteU.size());
    printf("r(byte unsigned, |dx|)                            = %+.4f   n=%zu\n",
           pearson(g_byteU, g_absdx), g_byteU.size());
    printf("r(byte unsigned, |dy|)                             = %+.4f   n=%zu\n",
           pearson(g_byteU, g_absdy), g_byteU.size());
    printf("r(byte unsigned, |dz|)                             = %+.4f   n=%zu\n",
           pearson(g_byteU, g_absdz), g_byteU.size());
    printf("r(byte unsigned, this key's run span)             = %+.4f   n=%zu\n",
           pearson(g_byteU, g_runSpanForKey), g_byteU.size());
    printf("r(byte unsigned, this key's track's translationKeys) = %+.4f   n=%zu\n",
           pearson(g_byteU, g_trackKeyCountForKey), g_byteU.size());
    printf("exact-match candidates (n=%zu keys each):\n", N);
    printf("  byte == max(|dx|,|dy|,|dz|)                      : %lld (%.4f%%)\n",
           g_eqMaxAbs, N ? 100.0 * static_cast<double>(g_eqMaxAbs) / static_cast<double>(N) : 0.0);
    printf("  byte == (uint8_t)(dx+dy+dz)                      : %lld (%.4f%%)\n",
           g_eqSumMod256, N ? 100.0 * static_cast<double>(g_eqSumMod256) / static_cast<double>(N) : 0.0);
    printf("  byte == (uint8_t)(dx^dy^dz)                      : %lld (%.4f%%)\n",
           g_eqXor, N ? 100.0 * static_cast<double>(g_eqXor) / static_cast<double>(N) : 0.0);

    // ---- H5: bounded by key count ------------------------------------------
    printf("\n--- H5: does the byte ever exceed the track's key count? ---\n");
    printf("predicate: byte(unsigned) > translationKeys, restricted to tracks where\n"
           "translationKeys <= 255 (otherwise every byte is a priori <= 255 and the\n"
           "test is vacuous by construction).\n");
    printf("keys in tracks with translationKeys <= 255: %lld / %zu (%.2f%%)\n",
           g_keysWithKeyCountLE255, N,
           N ? 100.0 * static_cast<double>(g_keysWithKeyCountLE255) / static_cast<double>(N) : 0.0);
    printf("byte exceeds translationKeys, among those: %lld / %lld (%.2f%%)\n",
           g_byteExceedsKeyCount, g_keysWithKeyCountLE255,
           g_keysWithKeyCountLE255 ? 100.0 * static_cast<double>(g_byteExceedsKeyCount) /
                                          static_cast<double>(g_keysWithKeyCountLE255)
                                    : 0.0);

    printf("\n=== end of measurements. See harness comments for what each predicate can"
           " and cannot show; this program draws no conclusion of its own. ===\n");
    return 0;
}
