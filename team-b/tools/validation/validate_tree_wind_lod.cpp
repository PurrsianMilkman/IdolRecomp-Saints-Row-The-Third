// Real-data gates for the wind-state object and LOD distances of
// sr3tree::Tree, against spec-tree-format.md Sec13 (re-synced 2026-09-20).
// Walks .vpp_pc archives recursively, parses every .csrt_pc/.gsrt_pc pair,
// collapses the shipped copies to distinct trees (by stem, verifying the
// copies are byte-identical) and scores the spec's own gate table (Sec13.8):
//
//   W1  k >= 0x26 (block +0xD0..+0x207) all zero on disk       spec 11/11
//   W2  band frequency pairs (k = 7+4g, 8+4g) ascending        spec 44/44,
//       control (pairing shifted by one float)                       0/33
//   W3  gust ranges k18 <= k19 and k1A <= k1B                  spec 11/11 each
//   W4  band exponents (k = 0x20..0x23) > 0                    spec 44/44
//   W6  distinct 38-float parameter sets                       spec 5 (5,2,1,2,1)
//   L1  geometry +0xE0..+0xEC strictly ascending A<B<C<D       spec 11/11;
//       same predicate at every other 4-float window in +0xC0..+0xFC:
//                                                                    <= 3/11
//   L2  geometry +0xF0/+0xF4 zero on disk                      spec 11/11
//   EOF exact-end-of-file (a successful Tree::parse)           spec 11/11
//
// Two independent views are used where it matters: the gates read the
// reader's exposed values, and W1/L1/L2 are additionally recomputed from the
// RAW bytes (located only through the block/structure offsets the reader
// reports, each anchored by the 'TREE' magic) and required to agree with the
// reader. The Sec13.4 derived steady-state table is checked against
// windSteadyState() at the shipped level 0.1.
//
// Usage: validate_tree_wind_lod <archive.vpp_pc> [more archives...]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3tree/tree.h"
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

uint32_t rawU32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}
float rawF32(const std::vector<uint8_t>& b, size_t off) {
    float v = 0;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}

struct TreeRec {
    std::string stem;
    std::vector<uint8_t> c, g;
    sr3tree::Tree tree;
};

long long g_copies = 0, g_parsed = 0, g_failed = 0, g_copyMismatch = 0;
std::map<std::string, TreeRec> g_distinct; // stem -> first parsed copy

void check(const std::string& archive, const std::string& stem, std::vector<uint8_t>&& c,
           std::vector<uint8_t>&& g) {
    ++g_copies;
    try {
        sr3tree::Tree t = sr3tree::Tree::parse(vpp::ByteView(c.data(), c.size()),
                                               vpp::ByteView(g.data(), g.size()));
        ++g_parsed;
        auto it = g_distinct.find(stem);
        if (it == g_distinct.end()) {
            TreeRec r;
            r.stem = stem;
            r.c = std::move(c);
            r.g = std::move(g);
            r.tree = std::move(t);
            g_distinct.emplace(stem, std::move(r));
        } else if (it->second.c != c || it->second.g != g) {
            ++g_copyMismatch;
            std::printf("COPY_DIFFERS archive=%s stem=%s\n", archive.c_str(), stem.c_str());
        }
    } catch (const std::exception& e) {
        ++g_failed;
        std::printf("FAIL archive=%s stem=%s : %s\n", archive.c_str(), stem.c_str(), e.what());
    }
}

void walk(const vpp::Container& c, const std::string& archive) {
    std::map<std::string, size_t> csrt, gsrt;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".csrt_pc", stem)) csrt[stem] = i;
        else if (stemFor(n, ".gsrt_pc", stem)) gsrt[stem] = i;
    }
    for (const auto& kv : csrt) {
        std::vector<uint8_t> cb, gb;
        if (!entryBytes(c, kv.second, cb)) continue;
        auto git = gsrt.find(kv.first);
        if (git == gsrt.end()) {
            std::printf("NO_G_PAIR archive=%s stem=%s\n", archive.c_str(), kv.first.c_str());
            continue;
        }
        if (!entryBytes(c, git->second, gb)) continue;
        check(archive, kv.first, std::move(cb), std::move(gb));
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                walk(c.openNested(i), archive);
            } catch (const std::exception&) {
            }
        }
    }
}

int g_gateFails = 0;
void gate(const char* name, long long got, long long denom, long long expected, const char* note = "") {
    const bool ok = got == expected;
    if (!ok) ++g_gateFails;
    std::printf("  %-4s %-58s %3lld/%-3lld (spec %lld/%lld) %s %s\n", name, note, got, denom, expected,
                denom, ok ? "PASS" : "**MISMATCH**", "");
}

std::string fmtSet(const std::set<float>& s) {
    std::string out;
    char buf[64];
    for (float v : s) {
        std::snprintf(buf, sizeof(buf), "%s%g", out.empty() ? "" : ", ", static_cast<double>(v));
        out += buf;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    using namespace sr3tree;
    if (argc < 2) {
        std::printf("usage: %s <archive.vpp_pc> [more...]\n", argv[0]);
        return 1;
    }
    for (int a = 1; a < argc; ++a) {
        std::vector<uint8_t> bytes = readFile(argv[a]);
        if (bytes.empty()) {
            std::printf("OPEN_FAILED %s : could not read file\n", argv[a]);
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c, argv[a]);
        } catch (const std::exception& e) {
            std::printf("OPEN_FAILED %s : %s\n", argv[a], e.what());
        }
    }

    const long long N = static_cast<long long>(g_distinct.size());
    std::printf("\n=== population ===\n");
    std::printf("  shipped copies            : %lld parsed OK %lld, failed %lld, copies differing from first: %lld\n",
                g_copies, g_parsed, g_failed, g_copyMismatch);
    std::printf("  distinct trees (by stem)  : %lld\n", N);

    // ---- EOF ----
    std::printf("\n=== gates (spec Sec13.8) ===\n");
    gate("EOF", g_parsed, g_copies, g_copies, "exact-EOF, every shipped copy (28/28 in HANDOFF 9.67)");
    gate("EOF", N, 11, 11, "exact-EOF, distinct trees");

    // ---- anchor: 'TREE' magic at the reported block offset, reader == raw ----
    long long anchorOk = 0, rawVsReaderWind = 0, rawVsReaderLod = 0;
    for (const auto& kv : g_distinct) {
        const TreeRec& r = kv.second;
        const TreeBlock& tb = r.tree.treeBlock();
        const GeometrySubStructure& geo = r.tree.geometry();
        if (rawU32(r.c, tb.offset) == kTreeMagic) ++anchorOk;
        bool same = true;
        for (size_t k = 0; k < kWindFileLoadedFloatCount; ++k) {
            uint32_t rb = rawU32(r.c, tb.offset + 0x38 + 4 * k);
            uint32_t vb = 0;
            std::memcpy(&vb, &tb.windFloats[k], 4);
            if (rb != vb) same = false;
        }
        if (same) ++rawVsReaderWind;
        bool sameLod = true;
        for (size_t i = 0; i < 4; ++i) {
            uint32_t rb = rawU32(r.c, geo.offset + 0xE0 + 4 * i), vb = 0;
            std::memcpy(&vb, &geo.lodDistances[i], 4);
            if (rb != vb) sameLod = false;
        }
        for (size_t i = 0; i < 2; ++i) {
            uint32_t rb = rawU32(r.c, geo.offset + 0xF0 + 4 * i), vb = 0;
            std::memcpy(&vb, &geo.lodDerivedOnDisk[i], 4);
            if (rb != vb) sameLod = false;
        }
        if (sameLod) ++rawVsReaderLod;
    }
    gate("ANCH", anchorOk, N, N, "'TREE' magic found at reader-reported block offset");
    gate("ANCH", rawVsReaderWind, N, N, "reader windFloats[k] == raw bytes at block+0x38+4k, k=0..0x25");
    gate("ANCH", rawVsReaderLod, N, N, "reader LOD/derived fields == raw bytes at geometry+0xE0..+0xF4");

    // ---- W1 ----
    long long w1Reader = 0, w1Raw = 0, w1Agree = 0;
    long long w1LowerNonZero = 0; // sanity: the file-loaded region is NOT all zero
    for (const auto& kv : g_distinct) {
        const TreeRec& r = kv.second;
        const size_t base = r.tree.treeBlock().offset;
        size_t nz = 0;
        for (size_t at = 0xD0; at + 4 <= 0x208; at += 4) {
            if (rawU32(r.c, base + at) != 0) ++nz;
        }
        const bool rawZero = nz == 0;
        const bool rdrZero = r.tree.treeBlock().windRuntimeRegionAllZero();
        if (rawZero) ++w1Raw;
        if (rdrZero) ++w1Reader;
        if (rawZero == rdrZero) ++w1Agree;
        size_t lowNz = 0;
        for (size_t at = 0x38; at < 0xD0; at += 4) {
            if (rawU32(r.c, base + at) != 0) ++lowNz;
        }
        if (lowNz > 0) ++w1LowerNonZero;
    }
    gate("W1", w1Reader, N, 11, "k>=0x26 all zero (reader accessor)");
    gate("W1", w1Raw, N, 11, "k>=0x26 all zero (raw dword scan +0xD0..+0x207)");
    gate("W1", w1Agree, N, N, "accessor agrees with the raw scan");
    gate("W1c", w1LowerNonZero, N, N, "control: file-loaded region +0x38..+0xCF is NOT all zero");

    // ---- W2 ----
    long long w2Strict = 0, w2NonStrict = 0, w2Den = 0;
    long long ctrlA = 0, ctrlADen = 0;   // (k = 8+4g, 9+4g), g = 0..2 : within the band block
    long long ctrlB = 0, ctrlBDen = 0;   // (k = 6+4g, 7+4g), g = 0..3
    long long ctrlAn = 0, ctrlBn = 0;    // same, non-strict
    long long ampAsc = 0, ampAscNonStrict = 0, ampDen = 0;
    for (const auto& kv : g_distinct) {
        const TreeBlock& tb = kv.second.tree.treeBlock();
        for (size_t g = 0; g < 4; ++g) {
            const WindOscillatorBand& b = tb.wind.bands[g];
            ++w2Den;
            if (b.frequencyAtLevel0 < b.frequencyAtLevel1) ++w2Strict;
            if (b.frequencyAtLevel0 <= b.frequencyAtLevel1) ++w2NonStrict;
            ++ampDen;
            if (b.amplitudeAtLevel0 < b.amplitudeAtLevel1) ++ampAsc;
            if (b.amplitudeAtLevel0 <= b.amplitudeAtLevel1) ++ampAscNonStrict;
            // also confirm the named fields sit at the k the spec says
            if (b.frequencyAtLevel0 != tb.windFloats[7 + 4 * g] || b.frequencyAtLevel1 != tb.windFloats[8 + 4 * g]) {
                std::printf("  NAMING mismatch tree %s band %zu\n", kv.first.c_str(), g);
                ++g_gateFails;
            }
        }
        for (size_t g = 0; g < 3; ++g) {
            ++ctrlADen;
            if (tb.windFloats[8 + 4 * g] < tb.windFloats[9 + 4 * g]) ++ctrlA;
            if (tb.windFloats[8 + 4 * g] <= tb.windFloats[9 + 4 * g]) ++ctrlAn;
        }
        for (size_t g = 0; g < 4; ++g) {
            ++ctrlBDen;
            if (tb.windFloats[6 + 4 * g] < tb.windFloats[7 + 4 * g]) ++ctrlB;
            if (tb.windFloats[6 + 4 * g] <= tb.windFloats[7 + 4 * g]) ++ctrlBn;
        }
    }
    gate("W2", w2Strict, w2Den, 44, "freq pair f0 < f1 strictly (k=7+4g, 8+4g)");
    gate("W2", w2NonStrict, w2Den, 44, "freq pair f0 <= f1 (k=7+4g, 8+4g)");
    gate("W2c", ctrlA, ctrlADen, 0, "control A: (k=8+4g, 9+4g), g=0..2, strict <");
    std::printf("       (control A non-strict <=: %lld/%lld; control B (k=6+4g, 7+4g) g=0..3 strict: %lld/%lld,"
                " non-strict: %lld/%lld)\n",
                ctrlAn, ctrlADen, ctrlB, ctrlBDen, ctrlBn, ctrlBDen);
    std::printf("       amplitude pairs (k=5+4g, 6+4g) ascending: strict < %lld/%lld, non-strict <= %lld/%lld"
                " (spec Sec13.8: NOT ordered, 34/44 ascending; no gate claimed)\n",
                ampAsc, ampDen, ampAscNonStrict, ampDen);

    // ---- W3 / W4 ----
    long long w3a = 0, w3b = 0, w4 = 0, w4Den = 0;
    for (const auto& kv : g_distinct) {
        const WindParameters& p = kv.second.tree.treeBlock().wind;
        if (p.gustTargetAmplitudeLo <= p.gustTargetAmplitudeHi) ++w3a;
        if (p.gustPlateauDurationLo <= p.gustPlateauDurationHi) ++w3b;
        for (const auto& b : p.bands) {
            ++w4Den;
            if (b.exponent > 0.0f) ++w4;
        }
    }
    gate("W3", w3a, N, 11, "k18 <= k19 (gust target amplitude range)");
    gate("W3", w3b, N, 11, "k1A <= k1B (gust plateau duration range)");
    gate("W4", w4, w4Den, 44, "exponents e0..e3 (k=0x20..0x23) > 0");

    // ---- W6 ----
    std::vector<std::vector<std::string>> groups; // in first-appearance (stem-sorted) order
    std::vector<std::array<uint32_t, kWindFileLoadedFloatCount>> keys;
    for (const auto& kv : g_distinct) {
        std::array<uint32_t, kWindFileLoadedFloatCount> key{};
        for (size_t k = 0; k < kWindFileLoadedFloatCount; ++k) {
            std::memcpy(&key[k], &kv.second.tree.treeBlock().windFloats[k], 4);
        }
        size_t gi = keys.size();
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) {
                gi = i;
                break;
            }
        }
        if (gi == keys.size()) {
            keys.push_back(key);
            groups.emplace_back();
        }
        groups[gi].push_back(kv.first);
    }
    std::printf("  W6   distinct parameter sets: %zu (spec 5), group sizes in first-appearance order:", groups.size());
    for (const auto& gr : groups) std::printf(" %zu", gr.size());
    std::printf("  (spec 5,2,1,2,1)\n");
    if (groups.size() != 5) ++g_gateFails;
    {
        const std::vector<size_t> want = {5, 2, 1, 2, 1};
        bool ok = groups.size() == want.size();
        for (size_t i = 0; ok && i < want.size(); ++i) ok = groups[i].size() == want[i];
        if (!ok) ++g_gateFails;
        std::printf("       sizes match 5,2,1,2,1 in order: %s\n", ok ? "PASS" : "**MISMATCH**");
    }
    for (const auto& gr : groups) {
        std::printf("       group:");
        for (const auto& s : gr) std::printf(" %s", s.c_str());
        std::printf("\n");
    }

    // ---- L1 / L2 ----
    long long l1 = 0, l2 = 0, l2raw = 0, l1Raw = 0;
    std::vector<long long> otherWin(13, 0);   // step-4 windows starting +0xC0..+0xF0
    std::vector<long long> otherAligned(4, 0); // step-16 windows +0xC0,+0xD0,+0xE0,+0xF0
    for (const auto& kv : g_distinct) {
        const TreeRec& r = kv.second;
        const GeometrySubStructure& geo = r.tree.geometry();
        if (lodDistancesValid(geo.lodDistances)) ++l1;
        if (geo.lodDerivedOnDiskZero()) ++l2;
        if (rawU32(r.c, geo.offset + 0xF0) == 0 && rawU32(r.c, geo.offset + 0xF4) == 0) ++l2raw;
        for (size_t w = 0; w < 13; ++w) {
            const size_t at = geo.offset + 0xC0 + 4 * w;
            const float a = rawF32(r.c, at), b = rawF32(r.c, at + 4), c = rawF32(r.c, at + 8), d = rawF32(r.c, at + 12);
            const bool asc = a < b && b < c && c < d;
            if (w == 8 && asc) ++l1Raw; // window +0xE0
            if (asc) {
                ++otherWin[w];
                if (w % 4 == 0) ++otherAligned[w / 4];
            }
        }
    }
    gate("L1", l1, N, 11, "A<B<C<D strictly ascending at +0xE0 (reader)");
    gate("L1", l1Raw, N, 11, "A<B<C<D strictly ascending at +0xE0 (raw)");
    long long maxOther = 0;
    std::printf("  L1c  same predicate per 4-float window (spec: other windows <= 3/11):\n");
    for (size_t w = 0; w < 13; ++w) {
        const size_t off = 0xC0 + 4 * w;
        if (off == 0xE0) continue;
        maxOther = std::max(maxOther, otherWin[w]);
        std::printf("       window +0x%02zX : %lld/%lld%s\n", off, otherWin[w], N, off % 16 == 0 ? "  (16-aligned)" : "");
    }
    std::printf("       max over the 12 other windows: %lld/%lld -> %s\n", maxOther, N,
                maxOther <= 3 ? "PASS (<= 3)" : "**MISMATCH**");
    if (maxOther > 3) ++g_gateFails;
    gate("L2", l2, N, 11, "+0xF0/+0xF4 zero (reader)");
    gate("L2", l2raw, N, 11, "+0xF0/+0xF4 zero (raw)");

    // ---- LOD function on real distances ----
    long long lodShape = 0;
    for (const auto& kv : g_distinct) {
        const LodDistances& d = kv.second.tree.geometry().lodDistances;
        auto t = [&](float x) { return kv.second.tree.lodParameterAtDistance(x); };
        bool ok = t(0.0f) && *t(0.0f) == 1.0f && t(d[0]) && *t(d[0]) == 1.0f && t(d[1]) && *t(d[1]) == 0.0f &&
                  t(d[2]) && *t(d[2]) == 0.0f && t(d[3]) && *t(d[3]) == -1.0f && t(d[3] * 2) &&
                  *t(d[3] * 2) == -1.0f;
        // monotone non-increasing over a sweep 0..1.5 D
        float prev = 2.0f;
        for (int i = 0; ok && i <= 3000; ++i) {
            auto v = t(d[3] * 1.5f * static_cast<float>(i) / 3000.0f);
            if (!v || *v > prev + 1e-6f || *v < -1.0f || *v > 1.0f) ok = false;
            if (v) prev = *v;
        }
        if (ok) ++lodShape;
    }
    gate("LOD", lodShape, N, N, "t(0)=1,t(A)=1,t(B)=t(C)=0,t(D)=t(2D)=-1, monotone non-increasing, in [-1,1]");

    // ---- per-tree table ----
    std::printf("\n=== per-tree LOD distances (metres) ===\n");
    for (const auto& kv : g_distinct) {
        const LodDistances& d = kv.second.tree.geometry().lodDistances;
        auto mid = kv.second.tree.lodParameterAtDistance(0.5f * (d[0] + d[1]));
        std::printf("  %-18s A=%-8g B=%-8g C=%-8g D=%-8g  t(mid A..B)=%.4f  raw+F0/F4=(%g,%g)\n", kv.first.c_str(),
                    static_cast<double>(d[0]), static_cast<double>(d[1]), static_cast<double>(d[2]),
                    static_cast<double>(d[3]), mid ? static_cast<double>(*mid) : -99.0,
                    static_cast<double>(kv.second.tree.geometry().lodDerivedOnDisk[0]),
                    static_cast<double>(kv.second.tree.geometry().lodDerivedOnDisk[1]));
    }

    // ---- per-k population vs spec Sec13.2's "Population" column ----
    std::printf("\n=== per-k distinct values across the %lld distinct trees (spec Sec13.2 population column) ===\n", N);
    for (size_t k = 0; k < kWindFileLoadedFloatCount; ++k) {
        std::set<float> vals;
        for (const auto& kv : g_distinct) vals.insert(kv.second.tree.treeBlock().windFloats[k]);
        std::printf("  k=0x%02zX block+0x%02zX : %s\n", k, 0x38 + 4 * k, fmtSet(vals).c_str());
    }
    std::printf("\n  per-tree (k1 k2 k15 | k18 k19 | k1A k1B | k1D):\n");
    for (const auto& kv : g_distinct) {
        const WindParameters& p = kv.second.tree.treeBlock().wind;
        std::printf("    %-18s k1=%-4g k2=%-4g k15=%-4g | k18/19=%g/%g | k1A/1B=%g/%g | k1D=%g\n", kv.first.c_str(),
                    static_cast<double>(p.directionResponseTime), static_cast<double>(p.lengthLikeReciprocalToC2z),
                    static_cast<double>(p.gustOnsetRate), static_cast<double>(p.gustTargetAmplitudeLo),
                    static_cast<double>(p.gustTargetAmplitudeHi), static_cast<double>(p.gustPlateauDurationLo),
                    static_cast<double>(p.gustPlateauDurationHi), static_cast<double>(p.scaleOfLevelToC6z));
    }
    std::printf("  k1 counts:");
    {
        std::map<float, int> cnt;
        for (const auto& kv : g_distinct) ++cnt[kv.second.tree.treeBlock().windFloats[1]];
        for (const auto& c : cnt) std::printf(" %g x%d", static_cast<double>(c.first), c.second);
        std::printf("   (spec: 2.5 in 5 trees, 1.0 in 6)\n");
    }

    // ---- Sec13.4 derived steady-state table ----
    struct Row {
        std::vector<std::string> trees;
        float osc[4], freq[4], amp[4];
    };
    const std::vector<Row> spec134 = {
        {{"st_com_break", "st_com_lrg", "st_com_med", "st_com_sml", "st_shrub_lrg"},
         {0.01f, 0.01f, 0.01f, 0.01f}, {0.298f, 1.04f, 0.793f, 0.793f}, {1.99f, 3.99f, 0.0f, 0.101f}},
        {{"st_pine_full", "st_pine_xmas"},
         {0.01f, 0.001f, 0.1f, 0.178f}, {1.01f, 7.01f, 1.68f, 10.33f}, {0.015f, 0.502f, 0.0f, 0.0178f}},
        {{"st_pine_tall"},
         {0.01f, 0.001f, 0.1f, 0.178f}, {1.01f, 2.01f, 1.68f, 10.33f}, {1.515f, 0.502f, 0.0f, 0.0089f}},
        {{"st_shrub_med", "st_shrub_sml"},
         {0.01f, 0.01f, 0.1f, 0.178f}, {3.04f, 6.04f, 0.2f, 10.33f}, {0.01f, 0.02f, 0.03f, 0.0053f}},
        {{"st_shrub_nice_01"},
         {0.01f, 0.001f, 0.1f, 0.178f}, {1.01f, 2.01f, 1.68f, 5.27f}, {0.015f, 0.502f, 0.015f, 0.0178f}},
    };
    std::printf("\n=== Sec13.4 derived steady state (level 0.1) vs windSteadyState() ===\n");
    long long dOk = 0, dDen = 0;
    double worstRel = 0.0;
    for (const Row& row : spec134) {
        for (const auto& stem : row.trees) {
            auto it = g_distinct.find(stem);
            if (it == g_distinct.end()) {
                std::printf("  tree %s missing\n", stem.c_str());
                ++g_gateFails;
                continue;
            }
            auto ss = it->second.tree.windSteadyStateAtLevel();
            for (size_t g = 0; g < 4; ++g) {
                const double got[3] = {ss->at(g).response, ss->at(g).frequency, ss->at(g).amplitude};
                const double want[3] = {row.osc[g], row.freq[g], row.amp[g]};
                for (int q = 0; q < 3; ++q) {
                    ++dDen;
                    const double err = std::fabs(got[q] - want[q]);
                    const double tol = 0.006 * std::fabs(want[q]) + 0.0006; // the table is printed to ~3 significant digits
                    if (err <= tol) ++dOk;
                    else std::printf("  DEVIATION %s g%zu %s: got %.6g, spec %.6g\n", stem.c_str(), g,
                                     q == 0 ? "osc" : q == 1 ? "freq" : "amp", got[q], want[q]);
                    if (want[q] != 0.0) worstRel = std::max(worstRel, err / std::fabs(want[q]));
                }
            }
        }
    }
    gate("D", dOk, dDen, dDen, "Sec13.4 values (osc, freq, amp) within table rounding");
    std::printf("       worst relative deviation from the printed table: %.3f%%\n", worstRel * 100.0);

    std::printf("\nGATE SUMMARY: %d mismatch(es)\n", g_gateFails);
    return g_gateFails == 0 ? 0 : 2;
}
