// Does a translation key's delta measure FROM the record's base, or
// ACCUMULATE across the keys of a run?
//
// spec-anim-format.md Sec6c.6 item 1, stated as still open: "Both fit the
// arithmetic; distinguishing them needs a decoded sequence checked for
// drift, which has not been run." This is that test.
//
//   reading A (from base):  pos_k = base/64 + delta_k / 4000
//   reading B (accumulate): pos_k = base/64 + (sum of delta_0..k) / 4000
//
// THE DISCRIMINATOR: cross-run continuity. A control record covers a run of
// keys; the next record starts the next run. An animation does not teleport
// between two consecutive keys just because a record boundary falls there,
// so the CORRECT reading should make the last position of run i and the
// first position of run i+1 nearly equal, and the wrong one should not.
//
// Both readings are scored on the same pairs, so this cannot favour one by
// construction. And because "small" means nothing without a scale, the same
// statistic is computed for a CONTROL: the gap between a run's end and the
// start of an UNRELATED run elsewhere in the same track. If a reading's
// real gap is not clearly below its own control gap, the test has not
// discriminated and neither reading is supported - which is a reportable
// outcome, not a failure to find one.
//
// Deliberately NOT used as evidence: "which reading produces more plausible
// magnitudes". Plausibility arguments are what made three vacuous rotation
// checks look like confirmation (HANDOFF Sec9.37.1); a test needs a failing
// case that is physically reachable, and continuity has one.
//
// Usage: probe_anim_delta_mode <archive.vpp_pc>
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

std::vector<double> g_gapA, g_gapB, g_ctlA, g_ctlB;
std::vector<double> g_shortA, g_shortB, g_longA, g_longB, g_longCtlA, g_longCtlB;
long long g_pairsLen1 = 0;
long long g_pairs = 0, g_runs = 0;
int g_clips = 0, g_tracks = 0;
long long g_skippedShortTrack = 0;

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
    ++g_tracks;

    // Decode each run under both readings, keeping only what the continuity
    // test needs: the first and last position of every run.
    struct Run { V3 firstA, lastA, firstB, lastB; uint32_t span = 0; };
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
        double ax = 0, ay = 0, az = 0; // running sums for reading B
        bool first = true;
        for (uint32_t s = 0; s < span && key < blk.translationKeys; ++s, ++key) {
            const size_t sa = blk.translationSampleOffset + key * 3;
            if (sa + 3 > bv.size()) return;
            const double dx = static_cast<int8_t>(bv.at(sa + 0));
            const double dy = static_cast<int8_t>(bv.at(sa + 1));
            const double dz = static_cast<int8_t>(bv.at(sa + 2));

            V3 a, b;
            a.x = bx + dx / 4000.0; a.y = by + dy / 4000.0; a.z = bz + dz / 4000.0;
            ax += dx; ay += dy; az += dz;
            b.x = bx + ax / 4000.0; b.y = by + ay / 4000.0; b.z = bz + az / 4000.0;

            if (first) { run.firstA = a; run.firstB = b; first = false; }
            run.lastA = a; run.lastB = b;
        }
        run.span = span;
        runs.push_back(run);
        ++g_runs;
    }

    for (size_t i = 0; i + 1 < runs.size(); ++i) {
        ++g_pairs;
        if (g_gapA.size() < 400000) {
            g_gapA.push_back(dist(runs[i].lastA, runs[i + 1].firstA));
            g_gapB.push_back(dist(runs[i].lastB, runs[i + 1].firstB));
        }
        // STRATIFY BY RUN LENGTH - this is where the test has any power at
        // all. For a run of ONE key the two readings are IDENTICAL by
        // construction (a sum of one term is that term), so those pairs
        // carry zero information and only dilute. Divergence grows with run
        // length, so the long-run stratum is the real test and the pooled
        // figure understates it.
        const uint32_t len = runs[i].span;
        if (len <= 1) {
            ++g_pairsLen1;
        } else if (len <= 4) {
            g_shortA.push_back(dist(runs[i].lastA, runs[i + 1].firstA));
            g_shortB.push_back(dist(runs[i].lastB, runs[i + 1].firstB));
        } else {
            g_longA.push_back(dist(runs[i].lastA, runs[i + 1].firstA));
            g_longB.push_back(dist(runs[i].lastB, runs[i + 1].firstB));
            g_longCtlA.push_back(dist(runs[i].lastA, runs[(i + runs.size() / 2) % runs.size()].firstA));
            g_longCtlB.push_back(dist(runs[i].lastB, runs[(i + runs.size() / 2) % runs.size()].firstB));
        }
        // CONTROL: the same statistic against an UNRELATED run in the same
        // track. Same distributions, same decode, no adjacency - so it says
        // what "no continuity" looks like for this data.
        const size_t j = (i + runs.size() / 2) % runs.size();
        if (j != i && j != i + 1 && g_ctlA.size() < 400000) {
            g_ctlA.push_back(dist(runs[i].lastA, runs[j].firstA));
            g_ctlB.push_back(dist(runs[i].lastB, runs[j].firstB));
        }
    }
}

void doClip(const std::vector<uint8_t>& raw) {
    vpp::ByteView bv(raw.data(), raw.size());
    sr3anim::Animation a;
    try { a = sr3anim::Animation::parse(bv); } catch (...) { return; }
    // Only the clean, oracle-confirmed population: clips without flags 0x40
    // whose walk lands on the declared end. Mixing in clips whose payload is
    // not fully accounted for would contaminate every figure below.
    if ((a.flags() & sr3anim::kExtraPayloadFlag) != 0) return;
    sr3anim::Payload pl = sr3anim::Payload::walk(bv, a);
    if (!pl.walkComplete() || !pl.landedOnDeclaredEnd()) return;
    ++g_clips;
    for (const auto& blk : pl.tracks()) doTrack(bv, blk);
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

    printf("\n=== delta mode: FROM BASE vs ACCUMULATE (spec Sec6c.6 item 1) ===\n");
    printf("clips %d, tracks with >=2 runs %d, runs %lld, adjacent pairs %lld\n",
           g_clips, g_tracks, g_runs, g_pairs);
    printf("tracks skipped (fewer than 2 runs): %lld\n", g_skippedShortTrack);

    printf("\nCROSS-RUN GAP - distance between run i's last key and run i+1's first.\n");
    printf("The correct reading should be small here and NOT small in its control.\n\n");
    printf("%-34s %10s %10s %10s\n", "", "median", "p90", "p99");
    struct Row { const char* n; std::vector<double>* v; } rows[] = {
        {"reading A (from base)", &g_gapA},
        {"  control, unrelated run", &g_ctlA},
        {"reading B (accumulate)", &g_gapB},
        {"  control, unrelated run", &g_ctlB},
    };
    for (auto& r : rows)
        printf("%-34s %10.4f %10.4f %10.4f\n", r.n, median(*r.v), pct(*r.v, 0.90),
               pct(*r.v, 0.99));

    const double a = median(g_gapA), ca = median(g_ctlA);
    const double b = median(g_gapB), cb = median(g_ctlB);
    printf("\nseparation from own control (higher = more discriminating):\n");
    printf("  reading A : control/real = %.2fx\n", a > 0 ? ca / a : 0.0);
    printf("  reading B : control/real = %.2fx\n", b > 0 ? cb / b : 0.0);
    printf("\nSTRATIFIED BY RUN LENGTH - where the test actually has power.\n");
    printf("A run of ONE key makes both readings identical by construction,\n");
    printf("so those pairs carry no information; divergence grows with length.\n\n");
    printf("pairs whose run length is 1 (zero information) : %lld\n", g_pairsLen1);
    printf("%-34s %10s %10s %10s %8s\n", "", "median", "p90", "p99", "n");
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "len 2-4   reading A", median(g_shortA),
           pct(g_shortA, 0.90), pct(g_shortA, 0.99), g_shortA.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "len 2-4   reading B", median(g_shortB),
           pct(g_shortB, 0.90), pct(g_shortB, 0.99), g_shortB.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "len 5+    reading A", median(g_longA),
           pct(g_longA, 0.90), pct(g_longA, 0.99), g_longA.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "len 5+    reading B", median(g_longB),
           pct(g_longB, 0.90), pct(g_longB, 0.99), g_longB.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "len 5+    control A", median(g_longCtlA),
           pct(g_longCtlA, 0.90), pct(g_longCtlA, 0.99), g_longCtlA.size());
    printf("%-34s %10.4f %10.4f %10.4f %8zu\n", "len 5+    control B", median(g_longCtlB),
           pct(g_longCtlB, 0.90), pct(g_longCtlB, 0.99), g_longCtlB.size());

    const double la = median(g_longA), lb = median(g_longB);
    const double lca = median(g_longCtlA), lcb = median(g_longCtlB);
    printf("\nlong runs, separation from own control:\n");
    printf("  reading A : %.2fx      reading B : %.2fx\n", la > 0 ? lca / la : 0.0,
           lb > 0 ? lcb / lb : 0.0);
    printf("  A/B gap ratio (lower A = A is better): %.2fx\n", la > 0 ? lb / la : 0.0);

    printf("\nNOTE: if neither reading separates from its control, this test\n");
    printf("has not discriminated and NEITHER reading is supported by it.\n");
    return 0;
}
