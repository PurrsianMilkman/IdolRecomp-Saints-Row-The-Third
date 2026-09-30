// Is the bone record's `+0x14` triple a ROTATION, or the negated
// parent-relative offset?
//
// spec-rig-format.md §4 calls it "Rest rotation, three f32" and marks the
// convention OPEN. A peer relayed a claim from Team A that it is not a
// rotation at all but `-(own_position - parent_position)`, fully derivable
// from data already in the record. Their updated spec has NOT landed in
// either folder - both copies still read "OPEN - which convention" - so
// this verifies the claim from scratch rather than adopting it.
//
// The identity under test:
//     child bone : triple == -(pos[i] - pos[parent[i]])
//     root bone  : triple == -pos[i]
//
// WHAT ELSE COULD PRODUCE A MATCH (the §3 check, applied before trusting
// the answer rather than after):
//
//  1. TRIVIALITY. If the triples were all ~0 AND every bone sat on its
//     parent, the identity would hold vacuously. So the magnitude
//     distribution of the triples is reported alongside the match rate. A
//     match only means something if the quantities being matched are big
//     enough to have been wrong.
//  2. LOOSE TOLERANCE. Reported at 1e-5 and at 1e-3 separately, so a
//     "match" that only appears when the tolerance is opened up is visible
//     as such.
//  3. A DEGENERATE PREDICTOR. Control run predicts from a deliberately
//     WRONG parent (shifted index). If that scores near the real one, the
//     predictor is not using the parent link at all.
//
// AND THE SHARPEST TEST - the 117 exceptions. The spec records that 117 of
// 22,274 triples exceed +/-pi, all in vehicle rigs, and flags it as a
// puzzle (§7: "plausibly rotor/spin bones stored beyond one turn"). The
// two hypotheses split cleanly on these:
//   * ROTATION: values beyond pi are anomalies needing a special story.
//   * OFFSET:   they are simply long bones in large vehicles, and they
//               must satisfy the identity exactly like every other bone.
// So this reports the match rate for the >pi subset SEPARATELY. If the
// identity holds there too, the "beyond one turn" story is not needed and
// the rotation reading is dead.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3rig/rig.h"
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

struct Tally {
    long long total = 0;
    long long match1e5 = 0;
    long long match1e3 = 0;
    long long controlMatch1e3 = 0;
    double maxErr = 0.0;
};
Tally g_child, g_root, g_overPi;
long long g_rigs = 0, g_bones = 0;
std::vector<double> g_tripleMag, g_offsetMag;
long long g_tripleNearZero = 0;
int g_examples = 0;

static void checkRig(const std::string& name, const std::vector<uint8_t>& b) {
    sr3rig::Rig rig;
    try {
        rig = sr3rig::Rig::parse(vpp::ByteView(b.data(), b.size()));
    } catch (const std::exception&) { return; }
    ++g_rigs;
    const auto& bones = rig.bones();
    const size_t n = bones.size();

    for (size_t i = 0; i < n; ++i) {
        const auto& bone = bones[i];
        ++g_bones;
        const auto& t = bone.negatedParentOffset;
        const auto& p = bone.restPosition;

        double predicted[3];
        double control[3];
        bool isRoot = bone.isRoot();
        if (isRoot) {
            for (int a = 0; a < 3; ++a) predicted[a] = -static_cast<double>(p[a]);
            // control: predict a root as if parented to bone 0
            for (int a = 0; a < 3; ++a)
                control[a] = -(static_cast<double>(p[a]) - bones[0].restPosition[static_cast<size_t>(a)]);
        } else {
            const auto& pp = bones[bone.parentIndex].restPosition;
            for (int a = 0; a < 3; ++a)
                predicted[a] = -(static_cast<double>(p[a]) - pp[static_cast<size_t>(a)]);
            // control: a deliberately WRONG parent
            size_t wrong = (static_cast<size_t>(bone.parentIndex) + n / 2 + 1) % n;
            const auto& wp = bones[wrong].restPosition;
            for (int a = 0; a < 3; ++a)
                control[a] = -(static_cast<double>(p[a]) - wp[static_cast<size_t>(a)]);
        }

        double err = 0.0, cerr = 0.0, mag = 0.0, omag = 0.0;
        for (int a = 0; a < 3; ++a) {
            err = std::max(err, std::fabs(static_cast<double>(t[static_cast<size_t>(a)]) - predicted[a]));
            cerr = std::max(cerr, std::fabs(static_cast<double>(t[static_cast<size_t>(a)]) - control[a]));
            mag += static_cast<double>(t[static_cast<size_t>(a)]) * t[static_cast<size_t>(a)];
            omag += predicted[a] * predicted[a];
        }
        mag = std::sqrt(mag);
        omag = std::sqrt(omag);
        g_tripleMag.push_back(mag);
        g_offsetMag.push_back(omag);
        if (mag < 1e-6) ++g_tripleNearZero;

        Tally& tal = isRoot ? g_root : g_child;
        ++tal.total;
        if (err < 1e-5) ++tal.match1e5;
        if (err < 1e-3) ++tal.match1e3;
        if (cerr < 1e-3) ++tal.controlMatch1e3;
        tal.maxErr = std::max(tal.maxErr, err);

        // the >pi subset - the discriminating case
        bool overPi = false;
        for (int a = 0; a < 3; ++a)
            if (std::fabs(static_cast<double>(t[static_cast<size_t>(a)])) > 3.14159266) overPi = true;
        if (overPi) {
            ++g_overPi.total;
            if (err < 1e-5) ++g_overPi.match1e5;
            if (err < 1e-3) ++g_overPi.match1e3;
            if (cerr < 1e-3) ++g_overPi.controlMatch1e3;
            g_overPi.maxErr = std::max(g_overPi.maxErr, err);
            if (g_examples < 6) {
                ++g_examples;
                printf("  >pi example  %-18s %-16s triple (%9.4f %9.4f %9.4f)  predicted (%9.4f %9.4f %9.4f)  err %.2e\n",
                       name.c_str(), bone.name.c_str(),
                       static_cast<double>(t[0]), static_cast<double>(t[1]), static_cast<double>(t[2]),
                       predicted[0], predicted[1], predicted[2], err);
            }
        }
    }
}

static void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".rig_pc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b) && !b.empty()) checkRig(n, b);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

static void report(const char* label, const Tally& t) {
    if (t.total == 0) { printf("  %-26s (none)\n", label); return; }
    printf("  %-26s %lld / %lld at 1e-5 (%.2f%%)   %lld / %lld at 1e-3   max err %.3e\n",
           label, t.match1e5, t.total, 100.0 * static_cast<double>(t.match1e5) / static_cast<double>(t.total),
           t.match1e3, t.total, t.maxErr);
    printf("  %-26s   CONTROL (wrong parent): %lld / %lld at 1e-3 (%.2f%%)\n", "",
           t.controlMatch1e3, t.total,
           100.0 * static_cast<double>(t.controlMatch1e3) / static_cast<double>(t.total));
}

static double median(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("could not read %s\n", argv[i]); continue; }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
            printf("scanned %s\n", argv[i]);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("container failed %s: %s\n", argv[i], ex.what());
        }
    }

    printf("\n=== is bone +0x14 a rotation, or the negated parent-relative offset? ===\n");
    printf("rigs %lld, bones %lld\n\n", g_rigs, g_bones);
    printf("IDENTITY: triple == -(pos - parent_pos)   [roots: -pos]\n");
    report("child bones", g_child);
    report("root bones", g_root);
    printf("\nTHE DISCRIMINATING SUBSET - triples with a component beyond +/-pi.\n");
    printf("Under the ROTATION reading these need a special story (spec S7 offers\n");
    printf("\"rotor/spin bones stored beyond one turn\"). Under the OFFSET reading\n");
    printf("they are just long bones and must obey the identity like the rest.\n");
    report("|component| > pi", g_overPi);

    printf("\nNON-TRIVIALITY (a match on near-zero quantities would mean nothing)\n");
    printf("  triples that are ~zero      : %lld / %lld\n", g_tripleNearZero, g_bones);
    printf("  median |triple|             : %.4f\n", median(g_tripleMag));
    printf("  median |predicted offset|   : %.4f\n", median(g_offsetMag));
    return 0;
}
