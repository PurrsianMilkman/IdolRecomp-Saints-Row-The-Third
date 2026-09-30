// Are translation tracks RIGID BONES? spec-anim-format.md Sec6c.5 confirms
// the decode `axis = base/64 + delta/4000` for a track's translation keys.
// The physical claim riding on top of that decode is that each track is one
// bone's position relative to its PARENT, and a bone does not change length
// mid-animation. If that is true, the VECTOR MAGNITUDE |t| of a track's keys
// should stay nearly constant across time even as its direction swings
// around during the animation.
//
// This is deliberately a WITHIN-ONE-FILE test. A prior cross-format test
// (validate_anim_rig_crossformat / validate_anim_rig_stratified) compared
// these same translations against .rig_pc rest-bone offsets, but that needed
// two assumptions - a track-to-bone mapping, and pairing a clip to a rig by
// bone count - and 98.8% of its comparisons inherited at least one of them.
// Constant magnitude-within-a-track needs neither: no rig, no bone mapping,
// no cross-file pairing. It only needs the shipping decoder, applied to one
// track's own keys.
//
// PER-TRACK STATISTIC: decode every key of the track with
// Payload::translations() (the shipping decoder - not reimplemented here),
// compute |t| per key, and report the RELATIVE spread
//     relSpread = (max|t| - min|t|) / mean|t|
// (relative, because absolute spread is meaningless when bones differ
// hugely in length - a spine root and a fingertip are both "rigid" at wildly
// different scales) and the coefficient of variation cv = stddev(|t|) /
// mean|t|.
//
// "Small spread" proves nothing without a scale, so two controls run beside
// every real reading:
//
//   CROSS-TRACK control - the same NUMBER of |t| values, but one drawn from
//   each of N DIFFERENT tracks in the same clip (their first key). This is
//   what "unrelated lengths" looks like in this data; if real tracks do not
//   score dramatically lower, the claim is not supported.
//
//   SHUFFLED-KEY control - the same track's own keys, but each output key's
//   x, y, z axes are taken from three (as far as the key count allows)
//   DIFFERENT keys of that track. This preserves every per-axis marginal
//   distribution while destroying the vector relationship between axes; if
//   the real spread is not clearly lower than this, constant magnitude would
//   be an artifact of the axis distributions rather than a property of the
//   vectors.
//
// A 1-key track has zero spread BY CONSTRUCTION - it carries no information
// about constancy - so it is counted but excluded from every headline
// figure. Results are stratified by key count (1, 2-4, 5+) per HANDOFF's
// standing warning: a previous test on this data was misleading pooled and
// decisive stratified, purely because ~44% of its pairs sat in a
// zero-information stratum.
//
// Usage: probe_anim_bone_rigidity <archive.vpp_pc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "vpp/container.h"

// ---- traversal scaffolding, copied from probe_anim_delta_mode.cpp --------

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

// ---- statistics -----------------------------------------------------------

double magOf(const sr3anim::Vec3& v) {
    const double x = v.x, y = v.y, z = v.z;
    return std::sqrt(x * x + y * y + z * z);
}

// Relative spread and coefficient of variation of a population of |t|
// values. Returns false (leaving outputs untouched) when there are fewer
// than 2 values or the mean is too near zero for a relative measure to mean
// anything - both are reportable exclusions, not silent failures.
bool computeSpread(const std::vector<double>& mags, double& relSpread, double& cv) {
    if (mags.size() < 2) return false;
    double sum = 0.0;
    for (double m : mags) sum += m;
    const double mean = sum / static_cast<double>(mags.size());
    if (mean < 1e-6) return false;
    double mn = mags[0], mx = mags[0];
    for (double m : mags) { mn = std::min(mn, m); mx = std::max(mx, m); }
    double sq = 0.0;
    for (double m : mags) sq += (m - mean) * (m - mean);
    const double sd = std::sqrt(sq / static_cast<double>(mags.size()));
    relSpread = (mx - mn) / mean;
    cv = sd / mean;
    return true;
}

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
double fracBelow(const std::vector<double>& v, double thresh) {
    if (v.empty()) return 0.0;
    size_t c = 0;
    for (double x : v) if (x < thresh) ++c;
    return static_cast<double>(c) / static_cast<double>(v.size());
}

// ---- per-population accumulator (one instance per stratum) ---------------

struct Pop {
    std::vector<double> relSpread;
    std::vector<double> cv;
    void add(double r, double c) { relSpread.push_back(r); cv.push_back(c); }
    size_t n() const { return relSpread.size(); }
};

struct Strata {
    Pop s2to4, s5plus, all;
    void add(size_t keyCount, double r, double c) {
        all.add(r, c);
        if (keyCount <= 4) s2to4.add(r, c); else s5plus.add(r, c);
    }
};

Strata g_real, g_cross, g_shuf;

long long g_clips = 0;
long long g_tracksSeen = 0;
long long g_tracksZeroKeys = 0;
long long g_tracksOneKey = 0;
long long g_tracksDegenerateMean = 0; // n>=2 but mean|t| ~ 0: relSpread undefined
long long g_tracksEligible = 0;       // n>=2, mean|t| usable: the headline population
long long g_crossSkippedInsufficientPool = 0; // eligible track, but clip lacks N other tracks

// Paired, per-track comparisons (stronger than comparing separate medians,
// since a control that merely has a *higher median* could still be beaten
// by real spread only half the time - bimodality would hide in that).
long long g_realLtShuf = 0;      // real relSpread < shuffled relSpread, same track
long long g_realLtCross = 0;     // real relSpread < cross-track relSpread, same track
long long g_realLtCrossDenom = 0;

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    // Clean, oracle-confirmed population only (Sec6c.4): no unaccounted
    // trailing payload, and the walk lands exactly where the file itself
    // declares it should.
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) return;
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) return;
    ++g_clips;

    const size_t numTracks = pl.tracks().size();
    std::vector<std::vector<sr3anim::Vec3>> trackVecs(numTracks);
    std::vector<size_t> poolTrackIdx;
    std::vector<double> poolMag0; // one key (the first) per track that has >=1 key

    for (size_t t = 0; t < numTracks; ++t) {
        trackVecs[t] = pl.translations(bv, t);
        if (!trackVecs[t].empty()) {
            poolTrackIdx.push_back(t);
            poolMag0.push_back(magOf(trackVecs[t][0]));
        }
    }

    for (size_t t = 0; t < numTracks; ++t) {
        ++g_tracksSeen;
        const auto& vec = trackVecs[t];
        const size_t n = vec.size();
        if (n == 0) { ++g_tracksZeroKeys; continue; }
        if (n == 1) { ++g_tracksOneKey; continue; }

        std::vector<double> mags(n);
        for (size_t k = 0; k < n; ++k) mags[k] = magOf(vec[k]);

        double realRel = 0.0, realCv = 0.0;
        if (!computeSpread(mags, realRel, realCv)) { ++g_tracksDegenerateMean; continue; }
        ++g_tracksEligible;
        g_real.add(n, realRel, realCv);

        // SHUFFLED-KEY control: output key m takes x from key m, y from key
        // m+off1, z from key m+off2 (mod n), with off1/off2 spread roughly a
        // THIRD OF THE TRACK APART rather than merely +1/+2. This matters:
        // an adjacent-key shuffle (+1/+2) barely perturbs anything for a
        // long track, because neighbouring keys usually sit in the same
        // control record's run and therefore share the same base - it would
        // make the control artificially weak (pass almost by default) for
        // reasons having nothing to do with the physical claim. Spacing the
        // three source keys a third of the track apart deliberately crosses
        // run/base boundaries when the track has more than one run, so this
        // control gets a fair, harder-to-pass shot at destroying the vector
        // relationship. For n==2 three distinct keys cannot exist at all
        // (inherent, not a test weakness); off2 then collapses onto off1.
        {
            const size_t off1 = (n / 3 >= 1) ? n / 3 : 1;
            size_t off2 = 2 * (n / 3);
            if (off2 <= off1) off2 = off1 + 1;
            std::vector<double> shufMags(n);
            for (size_t m = 0; m < n; ++m) {
                const size_t ix = m;
                const size_t iy = (m + off1) % n;
                const size_t iz = (m + off2) % n;
                const double x = vec[ix].x, y = vec[iy].y, z = vec[iz].z;
                shufMags[m] = std::sqrt(static_cast<double>(x) * x +
                                         static_cast<double>(y) * y +
                                         static_cast<double>(z) * z);
            }
            double shufRel = 0.0, shufCv = 0.0;
            if (computeSpread(shufMags, shufRel, shufCv)) {
                g_shuf.add(n, shufRel, shufCv);
                if (realRel < shufRel) ++g_realLtShuf;
            }
        }

        // CROSS-TRACK control: exactly N values, one each from N OTHER
        // tracks in this same clip (their first key). Skipped, and counted,
        // when the clip does not have N other tracks with at least one key
        // - a weakened (padded or truncated) control would not be a fair
        // "same number of values" comparison.
        {
            std::vector<double> otherMags;
            otherMags.reserve(poolMag0.size());
            for (size_t k = 0; k < poolTrackIdx.size(); ++k) {
                if (poolTrackIdx[k] == t) continue;
                otherMags.push_back(poolMag0[k]);
            }
            if (otherMags.size() < n) {
                ++g_crossSkippedInsufficientPool;
            } else {
                otherMags.resize(n);
                double crossRel = 0.0, crossCv = 0.0;
                if (computeSpread(otherMags, crossRel, crossCv)) {
                    g_cross.add(n, crossRel, crossCv);
                    ++g_realLtCrossDenom;
                    if (realRel < crossRel) ++g_realLtCross;
                }
            }
        }
    }
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b)) doClip(b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walkArchive(c.openNested(i)); } catch (...) {}
        }
    }
}

void printPopRow(const char* label, const Pop& p) {
    printf("%-28s %8zu %10.4f %10.4f %10.4f %9.1f%% %9.1f%% %9.1f%%\n",
           label, p.n(), median(p.relSpread), pct(p.relSpread, 0.90), pct(p.relSpread, 0.99),
           100.0 * fracBelow(p.relSpread, 0.01), 100.0 * fracBelow(p.relSpread, 0.05),
           100.0 * fracBelow(p.relSpread, 0.25));
}
void printCvRow(const char* label, const Pop& p) {
    printf("%-28s %8zu %10.4f %10.4f %10.4f\n",
           label, p.n(), median(p.cv), pct(p.cv, 0.90), pct(p.cv, 0.99));
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

    printf("\n=== bone rigidity: within-track |t| constancy (spec Sec6c.5) ===\n");
    printf("PREDICATE: if a translation track is a bone's position relative to its\n");
    printf("parent, bones are rigid, so |t| should be near-constant across a track's\n");
    printf("own keys even as direction changes. A FAILING case looks like: real\n");
    printf("relSpread is NOT dramatically lower than the cross-track control, or NOT\n");
    printf("clearly lower than the shuffled-key control - either would mean 'small\n");
    printf("spread' is an artifact of scale or of axis marginals, not of the vectors.\n\n");

    printf("clips (clean population) : %lld\n", g_clips);
    printf("tracks seen              : %lld\n", g_tracksSeen);
    printf("  0 keys (excluded)       : %lld\n", g_tracksZeroKeys);
    printf("  1 key  (excluded - zero-information by construction, no spread possible)\n");
    printf("                          : %lld\n", g_tracksOneKey);
    printf("  n>=2 but mean|t|~0 (excluded, relative spread undefined)\n");
    printf("                          : %lld\n", g_tracksDegenerateMean);
    printf("  n>=2 and usable (HEADLINE population)\n");
    printf("                          : %lld\n", g_tracksEligible);
    printf("cross-track control skipped (clip lacked N other tracks) : %lld / %lld eligible\n",
           g_crossSkippedInsufficientPool, g_tracksEligible);

    printf("\n--- relSpread = (max|t|-min|t|)/mean|t|, by population and key-count stratum ---\n");
    printf("%-28s %8s %10s %10s %10s %10s %10s %10s\n",
           "", "n", "median", "p90", "p99", "<0.01", "<0.05", "<0.25");

    printf("\n[stratum: 2-4 keys]\n");
    printPopRow("real track", g_real.s2to4);
    printPopRow("cross-track control", g_cross.s2to4);
    printPopRow("shuffled-key control", g_shuf.s2to4);

    printf("\n[stratum: 5+ keys]\n");
    printPopRow("real track", g_real.s5plus);
    printPopRow("cross-track control", g_cross.s5plus);
    printPopRow("shuffled-key control", g_shuf.s5plus);

    printf("\n[ALL eligible tracks, n>=2 pooled - reported for completeness, the\n");
    printf(" stratified rows above are the ones to trust]\n");
    printPopRow("real track", g_real.all);
    printPopRow("cross-track control", g_cross.all);
    printPopRow("shuffled-key control", g_shuf.all);

    printf("\n--- coefficient of variation cv = stddev|t| / mean|t| (same populations) ---\n");
    printf("%-28s %8s %10s %10s %10s\n", "", "n", "median", "p90", "p99");
    printf("\n[stratum: 2-4 keys]\n");
    printCvRow("real track", g_real.s2to4);
    printCvRow("cross-track control", g_cross.s2to4);
    printCvRow("shuffled-key control", g_shuf.s2to4);
    printf("\n[stratum: 5+ keys]\n");
    printCvRow("real track", g_real.s5plus);
    printCvRow("cross-track control", g_cross.s5plus);
    printCvRow("shuffled-key control", g_shuf.s5plus);
    printf("\n[ALL eligible tracks, n>=2 pooled]\n");
    printCvRow("real track", g_real.all);
    printCvRow("cross-track control", g_cross.all);
    printCvRow("shuffled-key control", g_shuf.all);

    printf("\n--- paired, per-track comparison (avoids median-vs-median blind spots) ---\n");
    printf("real relSpread < shuffled-key relSpread : %lld / %lld (%.1f%%)\n",
           g_realLtShuf, g_tracksEligible,
           g_tracksEligible ? 100.0 * static_cast<double>(g_realLtShuf) / static_cast<double>(g_tracksEligible) : 0.0);
    printf("real relSpread < cross-track relSpread  : %lld / %lld (%.1f%%)\n",
           g_realLtCross, g_realLtCrossDenom,
           g_realLtCrossDenom ? 100.0 * static_cast<double>(g_realLtCross) / static_cast<double>(g_realLtCrossDenom) : 0.0);

    printf("\n--- distribution shape (deciles), real-track relSpread ---\n");
    printf("median/p90/p99 alone can hide two populations behind one number; this\n");
    printf("prints the shape so a bimodal split shows up as a jump between deciles\n");
    printf("rather than being smoothed away.\n\n");
    printf("%-16s", "stratum");
    const double qs[] = {0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 0.95, 0.99};
    for (double q : qs) printf(" p%02.0f", q * 100.0);
    printf("\n");
    auto printDeciles = [&](const char* label, const Pop& p) {
        printf("%-16s", label);
        for (double q : qs) printf(" %5.2f", pct(p.relSpread, q));
        printf("   (n=%zu)\n", p.n());
    };
    printDeciles("real 2-4 keys", g_real.s2to4);
    printDeciles("real 5+ keys", g_real.s5plus);
    printDeciles("shuf 5+ keys", g_shuf.s5plus);

    printf("\nNOTE on bimodality: if the distributions above show a large gap between\n");
    printf("the fraction below 0.01-0.05 and the fraction below 0.25 (i.e. many tracks\n");
    printf("very tight AND many tracks far looser, rather than one broad middle), that\n");
    printf("is two populations, not one weak fit - report both, not just the median.\n");
    return 0;
}
