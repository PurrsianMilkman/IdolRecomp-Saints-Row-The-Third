// Does the runtime BLEND a translation key's base between its own control
// record and the NEXT one, or does it just use its own record's base
// (NOBLEND)?
//
// spec-anim-format.md Sec6c.6 item 3, stated as HYPOTHESIS - unconfirmed:
// "The decoder loads 16 bytes where a record is 8, which is consistent with
// fetching two consecutive records to blend between them, but the SSE
// shuffle sequence was not traced far enough to assert it."
//
// This does NOT retest Sec6c.6 item 1 (from-base vs accumulate): that is
// now CONFIRMED - the delta is measured from the base and does not
// accumulate - so both readings below build on that confirmed arithmetic
// and differ only in what "the base" is for a key partway through a run.
//
//   reading NOBLEND (current):  pos_k = B0/64 + delta_k/4000
//   reading BLEND:              pos_k = ((1-f)*B0 + f*B1)/64 + delta_k/4000
//                                f = k / span   (k = 0-indexed key position
//                                within the run; B1 = the NEXT control
//                                record's base)
//
// Note f = 0 for a run's FIRST key under either reading (k=0), so the two
// readings are IDENTICAL there by construction - only a run's LAST key
// (k = span-1, f = (span-1)/span) can differ, and only when span > 1. A
// run of span 1 has its only key at f=0, so NOBLEND and BLEND coincide
// exactly and that pair carries zero information. This is the same
// zero-information trap as probe_anim_delta_mode.cpp, so it is stratified
// away here too rather than left to dilute the pooled figure.
//
// THE DISCRIMINATOR: cross-run continuity, same oracle as the delta-mode
// probe. An animation does not teleport between two consecutive keys just
// because a record boundary falls there, so the correct reading should
// make the LAST position of run i (computed under that reading) close to
// the FIRST position of run i+1 (identical under both readings), and the
// wrong reading should not. Both readings are scored on the exact same
// pairs, so this cannot favour one by construction.
//
// CONTROL: because "small" means nothing without a scale, the same gap
// statistic is also computed against an UNRELATED run elsewhere in the
// same track (same construction as probe_anim_delta_mode.cpp: index
// i + runs.size()/2, wrapped). If a reading's real gap is not clearly
// below its own control gap, the test has not discriminated and NEITHER
// reading is supported by it - that is a reportable outcome, not a
// failure to find one.
//
// Usage: probe_anim_blend <archive.vpp_pc>
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

struct V3 { double x = 0, y = 0, z = 0; };
double dist(const V3& a, const V3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Pooled (all run lengths) gap samples.
std::vector<double> g_gapNoblend, g_gapBlend, g_ctlNoblend, g_ctlBlend;
// Stratified by the length (span) of run i, the run whose LAST key is the
// one under test - this is what determines how much weight f gives to the
// next record's base, and therefore how much the two readings can differ.
std::vector<double> g_shortNoblend, g_shortBlend, g_shortCtlNoblend, g_shortCtlBlend;
std::vector<double> g_longNoblend, g_longBlend, g_longCtlNoblend, g_longCtlBlend;
// How far apart the two readings' OWN predictions are for run i's last key
// (dist(lastBlend[i], lastNoblend[i])) - NOT a gap-to-neighbour statistic.
// This checks a precondition the verdict depends on: that a stratum said to
// carry "more information" actually has the two hypotheses disagreeing more
// there, rather than the apparent winner-swap being noise in a stratum where
// both readings already predict nearly the same thing.
std::vector<double> g_shortDivergence, g_longDivergence;
long long g_pairsLen1 = 0;

// SUPPLEMENTARY, finer than the required 1 / 2-4 / 5+ buckets: exact span
// 2, 3, 4, then 5-9 and 10+. Added because the required buckets alone
// disagreed (2-4 favours BLEND, 5+ favours NOBLEND) and this checks whether
// that is a smooth crossover with span or a sharp, single-point flip.
// Buckets: [0]=span2 [1]=span3 [2]=span4 [3]=span5-9 [4]=span10+
std::vector<double> g_fineNoblend[5], g_fineBlend[5];
// Raw base-to-base distance, dist(B0_i, B1_i) BEFORE any delta or blend is
// applied - answers "are consecutive records' bases already close, purely
// as a property of the data, independent of which reading is correct?"
// Needed to interpret WHY NOBLEND's continuity tightens at high span: if
// bases already converge there, that tightening is a data property that
// would make NOBLEND look good whether or not it is the true decode.
std::vector<double> g_fineBaseDiff[5];
long long g_pairs = 0, g_runs = 0;
int g_clips = 0, g_tracks = 0;

// Denominators for every skip, per this project's measurement discipline.
long long g_animEntries = 0;
long long g_parseFailed = 0;
long long g_flagSkipped = 0;      // kExtraPayloadFlag set - walk not trusted
long long g_walkNotClean = 0;     // walk incomplete or missed declared end
long long g_skippedShortTrack = 0; // fewer than 2 control records
long long g_skippedWideStride = 0; // stride != 8 (the 0x20 variant; unexercised
                                    // in shipped data per spec Sec6c.1, kept as
                                    // an explicit, counted skip rather than a
                                    // silent misread)

double median(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double pct(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1))];
}

void doTrack(vpp::ByteView bv, const sr3anim::TrackBlock& blk) {
    if (blk.translationControlCount < 2 || blk.translationKeys == 0) {
        ++g_skippedShortTrack;
        return;
    }
    if (blk.translationControlStride != 8) {
        ++g_skippedWideStride;
        return;
    }
    ++g_tracks;

    // One entry per control record (= one run of `span` keys). We only need
    // each run's own base, its first key's position (identical under both
    // readings) and its last key's delta (to finish either reading's last
    // position once we know which base(s) apply).
    struct Run {
        double bx = 0, by = 0, bz = 0; // this record's base, already /64
        V3 firstPos;                   // base + firstDelta/4000 (both readings agree)
        double ldx = 0, ldy = 0, ldz = 0; // LAST key's raw signed byte deltas
        uint32_t span = 0;
    };
    std::vector<Run> runs;

    size_t key = 0;
    for (size_t r = 0; r < blk.translationControlCount && key < blk.translationKeys; ++r) {
        const size_t at = blk.translationControlOffset + r * blk.translationControlStride;
        if (at + 8 > bv.size()) return;
        const uint32_t span = bv.readU16LE(at);
        const double bx = static_cast<int16_t>(bv.readU16LE(at + 2)) / 64.0;
        const double by = static_cast<int16_t>(bv.readU16LE(at + 4)) / 64.0;
        const double bz = static_cast<int16_t>(bv.readU16LE(at + 6)) / 64.0;
        if (span == 0) return;

        Run run;
        run.bx = bx; run.by = by; run.bz = bz;
        run.span = span;
        bool first = true;
        for (uint32_t s = 0; s < span && key < blk.translationKeys; ++s, ++key) {
            const size_t sa = blk.translationSampleOffset + key * 3;
            if (sa + 3 > bv.size()) return;
            const double dx = static_cast<int8_t>(bv.at(sa + 0));
            const double dy = static_cast<int8_t>(bv.at(sa + 1));
            const double dz = static_cast<int8_t>(bv.at(sa + 2));

            if (first) {
                // f = 0/span = 0 here under BOTH readings - this is the
                // run's anchor key, not part of what is under test.
                run.firstPos.x = bx + dx / 4000.0;
                run.firstPos.y = by + dy / 4000.0;
                run.firstPos.z = bz + dz / 4000.0;
                first = false;
            }
            // Keep overwriting so after the loop these are the LAST key's
            // deltas - the only key whose position depends on which
            // reading is correct.
            run.ldx = dx; run.ldy = dy; run.ldz = dz;
        }
        runs.push_back(run);
        ++g_runs;
    }

    if (runs.size() < 2) return; // no adjacent pair possible; already
                                  // counted above via translationControlCount,
                                  // this is just the (rare) case where the
                                  // per-key walk produced fewer runs than
                                  // control records claimed.

    // Second pass: for every run that HAS a next run, compute its last
    // position under NOBLEND (own base only) and under BLEND (own base
    // interpolated toward the next run's base, weighted by how far through
    // the run the last key sits).
    std::vector<V3> lastNoblend(runs.size()), lastBlend(runs.size());
    std::vector<bool> hasLast(runs.size(), false);
    for (size_t i = 0; i + 1 < runs.size(); ++i) {
        const Run& r0 = runs[i];
        const Run& r1 = runs[i + 1];
        lastNoblend[i].x = r0.bx + r0.ldx / 4000.0;
        lastNoblend[i].y = r0.by + r0.ldy / 4000.0;
        lastNoblend[i].z = r0.bz + r0.ldz / 4000.0;

        const double f = (r0.span > 0)
                              ? static_cast<double>(r0.span - 1) / static_cast<double>(r0.span)
                              : 0.0;
        const double ix = (1.0 - f) * r0.bx + f * r1.bx;
        const double iy = (1.0 - f) * r0.by + f * r1.by;
        const double iz = (1.0 - f) * r0.bz + f * r1.bz;
        lastBlend[i].x = ix + r0.ldx / 4000.0;
        lastBlend[i].y = iy + r0.ldy / 4000.0;
        lastBlend[i].z = iz + r0.ldz / 4000.0;
        hasLast[i] = true;
    }

    for (size_t i = 0; i + 1 < runs.size(); ++i) {
        if (!hasLast[i]) continue;
        ++g_pairs;
        const double gNoblend = dist(lastNoblend[i], runs[i + 1].firstPos);
        const double gBlend = dist(lastBlend[i], runs[i + 1].firstPos);
        if (g_gapNoblend.size() < 400000) {
            g_gapNoblend.push_back(gNoblend);
            g_gapBlend.push_back(gBlend);
        }

        // STRATIFY BY THE LENGTH OF RUN i - this is where the test has any
        // power at all. Span 1 makes f = 0, so BLEND == NOBLEND exactly and
        // the pair carries zero information; divergence grows with span.
        const uint32_t len = runs[i].span;
        if (len <= 1) {
            ++g_pairsLen1;
        } else if (len <= 4) {
            g_shortNoblend.push_back(gNoblend);
            g_shortBlend.push_back(gBlend);
            g_shortDivergence.push_back(dist(lastNoblend[i], lastBlend[i]));
        } else {
            g_longNoblend.push_back(gNoblend);
            g_longBlend.push_back(gBlend);
            g_longDivergence.push_back(dist(lastNoblend[i], lastBlend[i]));
        }
        {
            int bucket = -1;
            if (len == 2) bucket = 0;
            else if (len == 3) bucket = 1;
            else if (len == 4) bucket = 2;
            else if (len >= 5 && len <= 9) bucket = 3;
            else if (len >= 10) bucket = 4;
            if (bucket >= 0) {
                g_fineNoblend[bucket].push_back(gNoblend);
                g_fineBlend[bucket].push_back(gBlend);
                V3 base0{runs[i].bx, runs[i].by, runs[i].bz};
                V3 base1{runs[i + 1].bx, runs[i + 1].by, runs[i + 1].bz};
                g_fineBaseDiff[bucket].push_back(dist(base0, base1));
            }
        }

        // CONTROL: same statistic, against an unrelated run's first key
        // elsewhere in the same track. lastNoblend[i]/lastBlend[i] do not
        // depend on j, so this reuses the exact same values computed above -
        // only the "first" endpoint changes.
        const size_t j = (i + runs.size() / 2) % runs.size();
        if (j != i && j != i + 1) {
            const double cNoblend = dist(lastNoblend[i], runs[j].firstPos);
            const double cBlend = dist(lastBlend[i], runs[j].firstPos);
            if (g_ctlNoblend.size() < 400000) {
                g_ctlNoblend.push_back(cNoblend);
                g_ctlBlend.push_back(cBlend);
            }
            if (len >= 2 && len <= 4) {
                g_shortCtlNoblend.push_back(cNoblend);
                g_shortCtlBlend.push_back(cBlend);
            } else if (len >= 5) {
                g_longCtlNoblend.push_back(cNoblend);
                g_longCtlBlend.push_back(cBlend);
            }
        }
    }
}

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { ++g_parseFailed; return; }
    // Only the clean, oracle-confirmed population: clips without flags 0x40
    // whose walk lands on the declared end. Mixing in clips whose payload is
    // not fully accounted for would contaminate every figure below.
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) { ++g_flagSkipped; return; }
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) { ++g_walkNotClean; return; }
    ++g_clips;
    for (const auto& blk : pl.tracks()) doTrack(bv, blk);
}

void walkArchive(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (endsWith(c.entries()[i].name, ".anim_pc")) {
            ++g_animEntries;
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

    printf("\n=== translation BLEND vs NOBLEND across record boundaries ");
    printf("(spec Sec6c.6 item 3) ===\n");
    printf("PREDICATE: under the correct reading, run i's last position ");
    printf("(computed\n");
    printf("under that reading) should sit close to run i+1's first ");
    printf("position; under\n");
    printf("the wrong reading it should not, and neither should beat an ");
    printf("unrelated-run\n");
    printf("control by more than the correct reading does.\n\n");

    printf(".anim_pc entries seen: %lld\n", g_animEntries);
    printf("  parse failed:                    %lld / %lld\n", g_parseFailed, g_animEntries);
    printf("  skipped, flags 0x40 (extra payload, walk untrusted): %lld / %lld\n",
           g_flagSkipped, g_animEntries);
    printf("  skipped, walk incomplete or missed +0x30:            %lld / %lld\n",
           g_walkNotClean, g_animEntries);
    printf("  clean clips used:                                    %d / %lld\n",
           g_clips, g_animEntries);
    printf("tracks skipped, <2 control records or 0 keys: %lld\n", g_skippedShortTrack);
    printf("tracks skipped, control stride != 8 (0x20 variant, unexercised here): %lld\n",
           g_skippedWideStride);
    printf("tracks used (>=2 runs, stride 8): %d\n", g_tracks);
    printf("runs %lld, adjacent pairs (run i / run i+1) %lld\n", g_runs, g_pairs);

    printf("\nCROSS-RUN GAP, pooled over all run lengths - distance between ");
    printf("run i's\n");
    printf("last position and run i+1's first position.\n\n");
    printf("%-34s %10s %10s %10s %8s\n", "", "median", "p90", "p99", "n");
    struct Row { const char* n; std::vector<double>* v; } rows[] = {
        {"NOBLEND (own base only)", &g_gapNoblend},
        {"  control, unrelated run", &g_ctlNoblend},
        {"BLEND (interp toward next base)", &g_gapBlend},
        {"  control, unrelated run", &g_ctlBlend},
    };
    for (auto& r : rows)
        printf("%-34s %10.4f %10.4f %10.4f %8zu\n", r.n, median(*r.v), pct(*r.v, 0.90),
               pct(*r.v, 0.99), r.v->size());

    const double a = median(g_gapNoblend), ca = median(g_ctlNoblend);
    const double b = median(g_gapBlend), cb = median(g_ctlBlend);
    printf("\npooled separation from own control (higher = more discriminating):\n");
    printf("  NOBLEND : control/real = %.2fx\n", a > 0 ? ca / a : 0.0);
    printf("  BLEND   : control/real = %.2fx\n", b > 0 ? cb / b : 0.0);

    printf("\nSTRATIFIED BY THE LENGTH (span) OF RUN i - where the test ");
    printf("actually has\n");
    printf("power. A run of span 1 makes f = 0, so BLEND and NOBLEND are ");
    printf("IDENTICAL\n");
    printf("by construction and that pair carries zero information; ");
    printf("divergence between\n");
    printf("the two readings can only grow with span.\n\n");
    const long long stratTotal = g_pairsLen1 +
        static_cast<long long>(g_shortNoblend.size()) +
        static_cast<long long>(g_longNoblend.size());
    printf("pairs with span(run i) == 1 (zero information): %lld / %lld (%.1f%%)\n",
           g_pairsLen1, stratTotal,
           stratTotal > 0 ? 100.0 * static_cast<double>(g_pairsLen1) / static_cast<double>(stratTotal) : 0.0);
    printf("pairs with span(run i) in 2-4:                  %zu / %lld (%.1f%%)\n",
           g_shortNoblend.size(), stratTotal,
           stratTotal > 0 ? 100.0 * static_cast<double>(g_shortNoblend.size()) / static_cast<double>(stratTotal) : 0.0);
    printf("pairs with span(run i) >= 5:                    %zu / %lld (%.1f%%)\n\n",
           g_longNoblend.size(), stratTotal,
           stratTotal > 0 ? 100.0 * static_cast<double>(g_longNoblend.size()) / static_cast<double>(stratTotal) : 0.0);

    printf("%-34s %10s %10s %10s %8s\n", "", "median", "p90", "p99", "n");
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 2-4   NOBLEND", median(g_shortNoblend),
           pct(g_shortNoblend, 0.90), pct(g_shortNoblend, 0.99), g_shortNoblend.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 2-4   BLEND", median(g_shortBlend),
           pct(g_shortBlend, 0.90), pct(g_shortBlend, 0.99), g_shortBlend.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 2-4   control NOBLEND",
           median(g_shortCtlNoblend), pct(g_shortCtlNoblend, 0.90), pct(g_shortCtlNoblend, 0.99),
           g_shortCtlNoblend.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 2-4   control BLEND",
           median(g_shortCtlBlend), pct(g_shortCtlBlend, 0.90), pct(g_shortCtlBlend, 0.99),
           g_shortCtlBlend.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 5+    NOBLEND", median(g_longNoblend),
           pct(g_longNoblend, 0.90), pct(g_longNoblend, 0.99), g_longNoblend.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 5+    BLEND", median(g_longBlend),
           pct(g_longBlend, 0.90), pct(g_longBlend, 0.99), g_longBlend.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 5+    control NOBLEND",
           median(g_longCtlNoblend), pct(g_longCtlNoblend, 0.90), pct(g_longCtlNoblend, 0.99),
           g_longCtlNoblend.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 5+    control BLEND",
           median(g_longCtlBlend), pct(g_longCtlBlend, 0.90), pct(g_longCtlBlend, 0.99),
           g_longCtlBlend.size());

    const double sa = median(g_shortNoblend), sca = median(g_shortCtlNoblend);
    const double sb = median(g_shortBlend), scb = median(g_shortCtlBlend);
    const double la = median(g_longNoblend), lca = median(g_longCtlNoblend);
    const double lb = median(g_longBlend), lcb = median(g_longCtlBlend);
    printf("\nspan 2-4, separation from own control:\n");
    printf("  NOBLEND : %.2fx      BLEND : %.2fx\n", sa > 0 ? sca / sa : 0.0,
           sb > 0 ? scb / sb : 0.0);
    printf("span 5+, separation from own control:\n");
    printf("  NOBLEND : %.2fx      BLEND : %.2fx\n", la > 0 ? lca / la : 0.0,
           lb > 0 ? lcb / lb : 0.0);

    printf("\nMODEL DIVERGENCE - dist(lastBlend[i], lastNoblend[i]), i.e. how far\n");
    printf("apart the two READINGS' OWN predictions are for run i's last key.\n");
    printf("This is NOT a gap-to-neighbour statistic; it checks the precondition\n");
    printf("the verdict depends on: that 'more informative stratum' means the two\n");
    printf("hypotheses actually disagree more there, not that a winner-swap is\n");
    printf("noise in a stratum where both readings already predict almost the\n");
    printf("same thing. f = (span-1)/span grows with span, so this should too.\n\n");
    printf("%-34s %10s %10s %10s %8s\n", "", "median", "p90", "p99", "n");
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 2-4   |BLEND-NOBLEND|",
           median(g_shortDivergence), pct(g_shortDivergence, 0.90),
           pct(g_shortDivergence, 0.99), g_shortDivergence.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "span 5+    |BLEND-NOBLEND|",
           median(g_longDivergence), pct(g_longDivergence, 0.90),
           pct(g_longDivergence, 0.99), g_longDivergence.size());

    printf("\nSUPPLEMENTARY - exact span buckets, finer than the required 1 / ");
    printf("2-4 / 5+\n");
    printf("split, run because those two non-trivial strata DISAGREED: this ");
    printf("checks\n");
    printf("whether NOBLEND overtaking BLEND at span 5+ is a smooth crossover ");
    printf("or a\n");
    printf("sharp flip. gap median only (control omitted for space; same ");
    printf("construction\n");
    printf("as above).\n\n");
    const char* fineNames[5] = {"span 2", "span 3", "span 4", "span 5-9", "span 10+"};
    printf("%-12s %12s %12s %12s %8s\n", "", "NOBLEND med", "BLEND med", "|B1-B0| med", "n");
    for (int k = 0; k < 5; ++k) {
        printf("%-12s %12.4f %12.4f %12.4f %8zu\n", fineNames[k], median(g_fineNoblend[k]),
               median(g_fineBlend[k]), median(g_fineBaseDiff[k]), g_fineNoblend[k].size());
    }
    printf("\n|B1-B0| is the raw base-to-base distance BEFORE any delta or blend -\n");
    printf("if it shrinks with span, NOBLEND's tightening at high span is partly\n");
    printf("a property of the data (long runs = slowly-moving segments whose\n");
    printf("neighbouring bases are already close) and not purely a decode result.\n");

    printf("\nNOTE: if neither reading separates clearly from its own control in\n");
    printf("the span 5+ stratum, this test has not discriminated and NEITHER\n");
    printf("reading (BLEND or NOBLEND) is supported by it. A verdict should be\n");
    printf("read off the span 5+ stratum, not the pooled figures above it, for\n");
    printf("the same reason probe_anim_delta_mode.cpp's pooled figures understated\n");
    printf("its own result: span-1 pairs are zero-information by construction.\n");
    return 0;
}
