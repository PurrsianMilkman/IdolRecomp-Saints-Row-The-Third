// Why does the six-array walk miss the Mesh sub-block on 167/372 vehicles?
//
// `GeometryBlock` COMPUTES where the Mesh sub-block should start (BlobA's
// length field, then six arrays, then a trailer) and validates by checking
// for the version-9 marker. When that check fails we learn only "the
// arithmetic is wrong", not where or by how much.
//
// There is an independent ORACLE available: the paired `.gcar_pc`'s leading
// u32 equals the Mesh sub-block's check value (spec-vehicle-geometry.md §8,
// 393/393, and 205/205 confirmed here in §9.25). So the TRUE sub-block can
// be located without using any of the array arithmetic at all - search for
// a position where
//
//     u32(+0x00) == 9                       (version)
//     u32(+0x04) == gcar leading u32        (check value, from the OTHER file)
//
// and then measure the signed distance from what the walk computed. A
// constant delta means a fixed arithmetic error; a scattered delta means
// the model is wrong rather than merely offset.
//
// Reports the AMBIGUITY COUNT too (HANDOFF §3): a two-field match is strong,
// but "found one" means nothing without knowing how many positions matched.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
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
static uint32_t u32at(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) return 0;
    return static_cast<uint32_t>(b[o] | (b[o+1] << 8) | (b[o+2] << 16) |
                                 (static_cast<uint32_t>(b[o+3]) << 24));
}

int g_total = 0, g_walkOk = 0, g_walkFailed = 0;
int g_oracleFound = 0, g_oracleNone = 0;
std::map<int, int> g_ambiguity;
std::map<long long, int> g_deltaWhenFailed;
std::map<long long, int> g_deltaWhenOk;
std::vector<std::string> g_samples;
std::map<int, int> g_tierA, g_tierB, g_tierC;
std::map<long long, int> g_residual;
std::map<int, int> g_versionPass, g_versionFail;
int g_emptyArrayTotal = 0, g_predNewOk = 0, g_predOldOk = 0, g_predRawOk = 0;
std::map<long long, int> g_residualByCounts;

void examine(const std::string& name, const std::vector<uint8_t>& cb,
             const std::vector<uint8_t>& gb) {
    sr3vehicle::Vehicle veh;
    try {
        veh = sr3vehicle::Vehicle::parse(vpp::ByteView(cb.data(), cb.size()));
    } catch (const std::exception&) { return; }
    if (gb.size() < 16) return;
    ++g_total;

    const uint32_t oracleCheck = u32at(gb, 0);

    // What does the walk compute, and does it validate?
    long long computed = -1;
    bool walkOk = false;
    try {
        sr3geometry::GeometryBlock geo = sr3geometry::GeometryBlock::parseAt(
            vpp::ByteView(cb.data(), cb.size()), veh.meshRegionOffset());
        if (geo.hasMeshSubBlock()) {
            computed = static_cast<long long>(geo.meshSubBlockOffset());
            walkOk = true;
        }
    } catch (const std::exception&) {}
    if (walkOk) ++g_walkOk; else ++g_walkFailed;

    // Is the failure sorted by geometry-block VERSION? Every failing sample
    // printed v42, but a sample of failures cannot show that - the passing
    // set has to be measured too, or this is just "the failures are what
    // they are". Split both ways.
    {
        const uint16_t ver = static_cast<uint16_t>(u32at(cb, veh.meshRegionOffset() + 0x04) & 0xFFFF);
        if (walkOk) g_versionPass[ver] += 1; else g_versionFail[ver] += 1;
    }

    // Where is it REALLY? Located with no array arithmetic at all.
    std::vector<size_t> hits;
    for (size_t at = veh.meshRegionOffset(); at + 8 <= cb.size(); at += 4) {
        if (u32at(cb, at) != 9) continue;
        if (u32at(cb, at + 4) != oracleCheck) continue;
        hits.push_back(at);
    }

    // How unique is the sub-block WITHOUT the g-file? GeometryBlock has no
    // access to the paired file, so a fallback it could actually implement
    // must rely only on the c-file. Three tiers, each measured for
    // ambiguity rather than assumed (HANDOFF §3):
    //   A: version 9 alone
    //   B: + a c-length that fits the file and is non-trivial
    //   C: + a non-zero g-length
    int tierA = 0, tierB = 0, tierC = 0;
    for (size_t at = veh.meshRegionOffset(); at + 0x10 <= cb.size(); at += 4) {
        if (u32at(cb, at) != 9) continue;
        ++tierA;
        const uint32_t cLen = u32at(cb, at + 0x08);
        const uint32_t gLen = u32at(cb, at + 0x0C);
        if (cLen < 0x80 || at + cLen > cb.size()) continue;
        ++tierB;
        if (gLen == 0) continue;
        ++tierC;
    }
    g_tierA[tierA] += 1;
    g_tierB[tierB] += 1;
    g_tierC[tierC] += 1;
    ++g_ambiguity[static_cast<int>(hits.size())];
    if (hits.empty()) { ++g_oracleNone; return; }
    ++g_oracleFound;

    const long long actual = static_cast<long long>(hits.front());

    // HYPOTHESIS from the failing samples: the structure after BlobA starts
    // at align8(blobEnd + 1) - i.e. a terminator byte follows the blob
    // content, then 8-byte alignment - rather than align16(blobEnd).
    //
    // Tested on ALL 372, passing and failing. Ten samples fitting means
    // nothing on its own: Team A had a keyframe model reproduce two files
    // exactly, including a reversal symmetry, and it was still refuted at
    // scale. A control is included - the current align16 rule - so the two
    // are scored on the same population.
    {
        const size_t g0 = veh.meshRegionOffset();
        if (g0 + 0x8C <= cb.size()) {
            const uint32_t blobALen = u32at(cb, g0 + 0x88);
            const size_t blobEnd = g0 + 0x88 + 4 + blobALen;
            // My align8(blobEnd+1) guess scored 185/372 - WORSE than the
            // current rule. Ten samples fitting it perfectly meant nothing.
            //
            // Team A's rule, verified here rather than adopted:
            //     data_start = align16(lengthField + 4) + 1
            //     end        = data_start + declaredLength
            // The length counts from data_start, and the +1 is a leading NUL
            // that is a real convention in this format family. Tested with
            // and without a final align8, because with every array empty
            // nothing else re-aligns the cursor before the Mesh block.
            const size_t lengthField = g0 + 0x88;
            const size_t dataStart = ((lengthField + 4 + 15) / 16 * 16) + 1;
            const size_t teamAEnd = dataStart + blobALen;
            const size_t predNew = (teamAEnd + 7) / 8 * 8;   // + align8
            const size_t predRaw = teamAEnd;                  // no further align
            const size_t predOld = (blobEnd + 15) / 16 * 16;  // current rule (control)
            if (static_cast<long long>(predRaw) == actual) ++g_predRawOk;
            const bool emptyArrays = u32at(cb, g0 + 0x08) == 0 && u32at(cb, g0 + 0x38) == 0;
            if (emptyArrays) {
                ++g_emptyArrayTotal;
                if (static_cast<long long>(predNew) == actual) ++g_predNewOk;
                if (static_cast<long long>(predOld) == actual) ++g_predOldOk;
            }
        }
    }
    if (walkOk) {
        g_deltaWhenOk[actual - computed] += 1;
    } else {
        // The walk threw, so recompute what it WOULD have produced by
        // repeating its arithmetic here - otherwise there is no delta to
        // report, only "it failed".
        const size_t geomOffset = veh.meshRegionOffset();
        if (geomOffset + 0x8C <= cb.size()) {
            const uint32_t blobALength = u32at(cb, geomOffset + 0x88);
            g_deltaWhenFailed[static_cast<long long>(blobALength)] += 0; // touch
        }
        g_deltaWhenFailed[actual - static_cast<long long>(veh.meshRegionOffset())] += 1;

        // Replicate the walk's arithmetic term by term and diff against the
        // oracle, so the residual says WHICH term is wrong rather than just
        // that the total is.
        const size_t g0 = veh.meshRegionOffset();
        const uint16_t count1 = static_cast<uint16_t>(u32at(cb, g0 + 0x08) & 0xFFFF);
        const uint16_t count4 = static_cast<uint16_t>((u32at(cb, g0 + 0x08) >> 16) & 0xFFFF);
        const uint16_t count2 = static_cast<uint16_t>(u32at(cb, g0 + 0x38) & 0xFFFF);
        const uint16_t count3 = static_cast<uint16_t>((u32at(cb, g0 + 0x38) >> 16) & 0xFFFF);
        const uint32_t blobALen = u32at(cb, g0 + 0x88);
        const uint16_t version = static_cast<uint16_t>(u32at(cb, g0 + 0x04) & 0xFFFF);
        // arraySpaceStart exactly as geometry_block.cpp computes it.
        const size_t arrayStart = ((g0 + 0x88 + 4 + blobALen) + 15) / 16 * 16;
        const long long residual = actual - static_cast<long long>(arrayStart);
        g_residual[residual] += 1;
        g_residualByCounts[(long long)count1 * 1000000 + (long long)count2 * 10000 +
                           (long long)count3 * 100 + count4] += 1;
        if (g_samples.size() < 10) {
            char buf[320];
            std::snprintf(buf, sizeof(buf),
                          "%s v%u c1=%u c2=%u c3=%u c4=%u blobA=%u arrayStart=0x%zX "
                          "true=0x%llX residual=%lld",
                          name.c_str(), version, count1, count2, count3, count4, blobALen,
                          arrayStart, static_cast<unsigned long long>(actual), residual);
            g_samples.push_back(buf);
        }
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".ccar_pc")) {
            std::string gn = n;
            size_t d = gn.find_last_of('.');
            if (d != std::string::npos) gn[d + 1] = 'g';
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j)
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            if (!gb.empty()) examine(n, cb, gb);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
        } catch (const std::exception&) {}
    }

    printf("\n=== six-array walk vs an independent oracle ===\n");
    printf("vehicles            : %d\n", g_total);
    printf("walk validated      : %d\n", g_walkOk);
    printf("walk failed         : %d\n", g_walkFailed);
    printf("\nORACLE (version 9 AND check value == .gcar_pc leading u32):\n");
    printf("  located           : %d\n", g_oracleFound);
    printf("  not found at all  : %d\n", g_oracleNone);
    printf("  AMBIGUITY - positions matching both fields:\n");
    for (const auto& kv : g_ambiguity)
        printf("    %d hit(s) : %d vehicle(s)%s\n", kv.first, kv.second,
               kv.first == 1 ? "   <- unique" : "");
    printf("\nWHEN THE WALK VALIDATED, oracle minus computed:\n");
    for (const auto& kv : g_deltaWhenOk)
        printf("    %+lld : %d\n", kv.first, kv.second);
    printf("\nWHEN THE WALK FAILED, true Mesh offset relative to the sub-header:\n");
    int shown = 0;
    for (const auto& kv : g_deltaWhenFailed) {
        if (kv.second == 0) continue;
        printf("    +%lld : %d vehicle(s)\n", kv.first, kv.second);
        if (++shown >= 12) { printf("    ...\n"); break; }
    }
    printf("\nC-FILE-ONLY AMBIGUITY (what GeometryBlock could actually use):\n");
    auto dump = [](const char* label, const std::map<int, int>& m) {
        printf("  %s:\n", label);
        for (const auto& kv : m)
            printf("    %d candidate(s) : %d vehicle(s)%s\n", kv.first, kv.second,
                   kv.first == 1 ? "   <- unique" : "");
    };
    dump("A: version 9 alone", g_tierA);
    dump("B: + c-length fits the file and >= 0x80", g_tierB);
    dump("C: + non-zero g-length", g_tierC);

    printf("\nHYPOTHESIS TEST, scored on the same population (passing AND failing):\n");
    printf("  TeamA rule + align8         : %d / %d\n", g_predNewOk, g_emptyArrayTotal);
    printf("  TeamA rule, no further align: %d / %d\n", g_predRawOk, g_emptyArrayTotal);
    printf("  align16(blobEnd) [current]  : %d / %d\n", g_predOldOk, g_emptyArrayTotal);

    printf("\nVERSION SPLIT (the control: failures alone prove nothing)\n");
    printf("  walk PASSED, by geometry-block version:\n");
    for (const auto& kv : g_versionPass) printf("    v%-4d : %d\n", kv.first, kv.second);
    printf("  walk FAILED, by geometry-block version:\n");
    for (const auto& kv : g_versionFail) printf("    v%-4d : %d\n", kv.first, kv.second);

    printf("\nRESIDUAL: true Mesh offset minus arraySpaceStart\n");
    printf("  (i.e. how many bytes the six arrays REALLY occupy, when the walk got it wrong)\n");
    {
        int n = 0;
        for (const auto& kv : g_residual) {
            printf("    %+lld : %d vehicle(s)\n", kv.first, kv.second);
            if (++n >= 18) { printf("    ...\n"); break; }
        }
    }

    printf("\nsamples:\n");
    for (const auto& s : g_samples) printf("    %s\n", s.c_str());
    return 0;
}
