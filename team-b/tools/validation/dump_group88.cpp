// Dumps and analyses every populated `+0x88` nested-group inner element from
// every `.clmesh_pc` entry in the archives that carry it
// (`sr3_city_0.vpp_pc`, `sr3_city_1.vpp_pc` - HANDOFF Sec9.59 measured this
// as 258 of 100,379 entries, zero in the three DLC archives).
//
// spec-physics-format.md Sec4.2's `FUN_00749c40` row leaves the 12-byte
// inner element's field layout OPEN, reading it as "consistent with (not
// proven to be) a triangle record (3 vertex/point indices) or a small
// plane/face descriptor". This tool tests both hypotheses against the
// COMPLETE population it can reach, not a sample:
//
//   * TRIANGLE/INDEX hypothesis: the 3 u32 values should be small, densely
//     packed, and bounded by some in-file count - tested here against the
//     vertex count of any embedded Mesh sub-block the SAME file's
//     `+0x48` array resolves (co-occurrence checked first, since the two
//     fields need not appear in the same file at all), and against
//     count(+0x70) as a secondary candidate.
//   * PLANE/FACE hypothesis: the 3 values read as floats should look
//     physically meaningful - a normal-vector-shaped triple near unit
//     magnitude, or small bounded position-like values.
//
// It also censuses the outer 16-byte record's two fields
// spec-physics-format.md Sec4.2 does not itemise beyond innerCount (+0x00)
// and the runtime-resolved inner pointer (+0x08): +0x04 and +0x0C.
//
// Usage:  dump_group88 <archive.vpp_pc> [more archives...] <out-dir>
// (out-dir is always the LAST argument.)
//
// Writes <out-dir>/group88_dump.csv - one row per inner 12-byte element,
// both interpretations plus raw hex - and prints population-level summary
// statistics to stdout. Only decompresses a `.glmesh_pc` pair when the
// paired `.clmesh_pc` actually populates `+0x88` AND `+0x48`, which is why
// this is much cheaper than a full `validate_clmesh` sweep of the same two
// archives.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3clmesh/level_mesh.h"
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

float bitsToFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// ------------------------------------------------------------- accumulators
long long g_clmeshSeen = 0;       // every .clmesh_pc entry touched
long long g_clmeshParsed = 0;     // header + tail parsed with LevelMesh
long long g_clmeshGroup88 = 0;    // files with group88Outer().count() > 0
long long g_clmeshNotLanded = 0;  // group88>0 but landsOnEof()==false (should be 0)

long long g_outerRecords = 0;
long long g_innerElements = 0;

// outer record padding-field census (bytes 0x04 and 0x0C; 0x08 is the
// runtime-resolved inner pointer, expected 0 on disk like +0xb8)
long long g_outerField04Zero = 0, g_outerField08Zero = 0, g_outerField0CZero = 0;
uint32_t g_outerField04Min = 0xFFFFFFFFu, g_outerField04Max = 0;
uint32_t g_outerField08Min = 0xFFFFFFFFu, g_outerField08Max = 0;
uint32_t g_outerField0CMin = 0xFFFFFFFFu, g_outerField0CMax = 0;

// per-slot u32 stats across ALL inner elements
struct U32Stat {
    uint32_t mn = 0xFFFFFFFFu, mx = 0;
    long long zero = 0;
    long long lt16 = 0, lt256 = 0, lt65536 = 0, lt16m = 0, huge = 0; // buckets
    void see(uint32_t v) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
        if (v == 0) ++zero;
        if (v < 16) ++lt16;
        else if (v < 256) ++lt256;
        else if (v < 65536) ++lt65536;
        else if (v < 16 * 1024 * 1024) ++lt16m;
        else ++huge;
    }
};
U32Stat g_slot[3];

// per-slot float stats
struct FloatStat {
    double mn = 1e300, mx = -1e300, sum = 0;
    long long n = 0, nan_ = 0, inf_ = 0, subnormalNonzero = 0;
    long long nearUnit = 0;   // |v| in [0.9, 1.1]
    long long smallPos = 0;   // |v| in [0, 2]
    long long huge_ = 0;      // |v| > 1e6
    void see(float f) {
        ++n;
        if (std::isnan(f)) { ++nan_; return; }
        if (std::isinf(f)) { ++inf_; return; }
        if (f != 0.0f && std::fabs(f) < 1.17549435e-38f) ++subnormalNonzero;
        double a = std::fabs(static_cast<double>(f));
        mn = std::min(mn, a);
        mx = std::max(mx, a);
        sum += a;
        if (a >= 0.9 && a <= 1.1) ++nearUnit;
        if (a <= 2.0) ++smallPos;
        if (a > 1e6) ++huge_;
    }
};
FloatStat g_fslot[3];

// magnitude of the (f0,f1,f2) triple, treated as a vector
double g_magMin = 1e300, g_magMax = -1e300, g_magSum = 0;
long long g_magN = 0, g_magNearUnit = 0; // |mag - 1.0| < 0.1

// co-occurrence: does the SAME file populate both +0x88 and +0x48?
long long g_group88Files = 0;
long long g_group88AndRefA = 0;      // count(+0x48) > 0 too
long long g_group88RefAAllResolved = 0; // every +0x48 reference resolved to a MeshBlock
long long g_boundCheckable = 0;      // files where we got at least one vertex count to test against
long long g_valuesInBound = 0, g_valuesTotal = 0; // per-slot-value bound test tally (all 3 slots)
uint32_t g_vertexCountMin = 0xFFFFFFFFu, g_vertexCountMax = 0;

// count(+0x70) co-occurrence (56-byte array, same count as +0x48/+0x58 per
// Sec9.59 finding - a cheap second candidate bound with no decompression
// cost since it never needs the .glmesh_pc pair)
long long g_valuesInBound70 = 0;

// locality / de-duplication signature: within one outer group's inner list,
// what fraction of the 3*innerCount slot values are DISTINCT? A real
// triangle list sharing vertices between adjacent faces reads well below
// 1.0; random/unrelated 3-tuples read close to 1.0.
double g_dedupRatioSum = 0;
long long g_dedupGroups = 0; // groups with innerCount >= 2 (ratio undefined/trivial otherwise)

std::ofstream* g_csv = nullptr;

void analyzeGroup88(const std::string& archive, const std::string& stem, vpp::ByteView content,
                    const sr3clmesh::LevelMesh& lm, uint32_t vertexBound, bool haveVertexBound,
                    uint32_t count70) {
    const auto& outer = lm.group88();
    for (size_t oi = 0; oi < outer.size(); ++oi) {
        const sr3clmesh::NestedEntry& e = outer[oi];
        ++g_outerRecords;
        uint32_t f04 = content.readU32LE(e.outerOffset + 0x04);
        uint32_t f08 = content.readU32LE(e.outerOffset + 0x08);
        uint32_t f0C = content.readU32LE(e.outerOffset + 0x0C);
        if (f04 == 0) ++g_outerField04Zero; else { g_outerField04Min = std::min(g_outerField04Min, f04); g_outerField04Max = std::max(g_outerField04Max, f04); }
        if (f08 == 0) ++g_outerField08Zero; else { g_outerField08Min = std::min(g_outerField08Min, f08); g_outerField08Max = std::max(g_outerField08Max, f08); }
        if (f0C == 0) ++g_outerField0CZero; else { g_outerField0CMin = std::min(g_outerField0CMin, f0C); g_outerField0CMax = std::max(g_outerField0CMax, f0C); }

        std::set<uint32_t> distinctVals;
        for (size_t ii = 0; ii < e.innerCount; ++ii) {
            size_t at = e.innerOffset + ii * e.innerStride;
            if (at + 12 > content.size()) break;
            uint32_t u0 = content.readU32LE(at + 0);
            uint32_t u1 = content.readU32LE(at + 4);
            uint32_t u2 = content.readU32LE(at + 8);
            float f0 = bitsToFloat(u0), f1 = bitsToFloat(u1), f2 = bitsToFloat(u2);
            ++g_innerElements;
            g_slot[0].see(u0); g_slot[1].see(u1); g_slot[2].see(u2);
            g_fslot[0].see(f0); g_fslot[1].see(f1); g_fslot[2].see(f2);
            distinctVals.insert(u0); distinctVals.insert(u1); distinctVals.insert(u2);

            double mag = std::sqrt(static_cast<double>(f0) * f0 + static_cast<double>(f1) * f1 +
                                   static_cast<double>(f2) * f2);
            if (std::isfinite(mag)) {
                g_magMin = std::min(g_magMin, mag);
                g_magMax = std::max(g_magMax, mag);
                g_magSum += mag;
                ++g_magN;
                if (std::fabs(mag - 1.0) < 0.1) ++g_magNearUnit;
            }

            if (haveVertexBound) {
                ++g_valuesTotal;
                if (u0 < vertexBound) ++g_valuesInBound;
                ++g_valuesTotal;
                if (u1 < vertexBound) ++g_valuesInBound;
                ++g_valuesTotal;
                if (u2 < vertexBound) ++g_valuesInBound;
            }
            if (count70 > 0) {
                if (u0 < count70) ++g_valuesInBound70;
                if (u1 < count70) ++g_valuesInBound70;
                if (u2 < count70) ++g_valuesInBound70;
            }

            if (g_csv) {
                char hex[25];
                uint8_t raw[12];
                std::memcpy(raw + 0, &u0, 4);
                std::memcpy(raw + 4, &u1, 4);
                std::memcpy(raw + 8, &u2, 4);
                for (int b = 0; b < 12; ++b) snprintf(hex + b * 2, 3, "%02x", raw[b]);
                (*g_csv) << archive << "," << stem << "," << oi << "," << e.outerOffset << ","
                         << e.innerCount << "," << f04 << "," << f08 << "," << f0C << "," << ii
                         << "," << at << "," << u0 << "," << u1 << "," << u2 << "," << f0 << ","
                         << f1 << "," << f2 << "," << mag << "," << hex << "\n";
            }
        }
        if (e.innerCount >= 2) {
            g_dedupRatioSum += static_cast<double>(distinctVals.size()) / (3.0 * e.innerCount);
            ++g_dedupGroups;
        }
    }
}

void handleClmesh(const std::string& archive, const std::string& stem, vpp::Container& c,
                  size_t clIdx, size_t glIdx, bool haveGl) {
    std::vector<uint8_t> cb;
    if (!entryBytes(c, clIdx, cb)) return;
    ++g_clmeshSeen;

    vpp::ByteView content(cb.data(), cb.size());
    sr3clmesh::LevelMesh lm;
    try {
        lm = sr3clmesh::LevelMesh::parse(content);
    } catch (const std::exception&) {
        return;
    }
    ++g_clmeshParsed;

    const uint32_t c88 = lm.headerField(sr3clmesh::kCount88);
    if (c88 == 0) return; // the overwhelming majority - skip, no glmesh decompress needed
    ++g_clmeshGroup88;

    if (!lm.landsOnEof()) {
        ++g_clmeshNotLanded;
        return; // HANDOFF Sec9.59: measured as 0/258 in the full population; report if it recurs
    }

    const uint32_t cRefA = lm.headerField(sr3clmesh::kCountRefA);
    const uint32_t c70 = lm.headerField(sr3clmesh::kCount70);
    ++g_group88Files;

    uint32_t vertexBound = 0;
    bool haveVertexBound = false;
    if (cRefA > 0) {
        ++g_group88AndRefA;
        if (haveGl) {
            std::vector<uint8_t> gb;
            if (entryBytes(c, glIdx, gb)) {
                vpp::ByteView gcontent(gb.data(), gb.size());
                std::vector<sr3mesh::MeshBlock> meshes = lm.resolveReferencedMeshes(content, gcontent);
                if (meshes.size() == cRefA) ++g_group88RefAAllResolved;
                for (const sr3mesh::MeshBlock& m : meshes) {
                    if (m.channels().empty()) continue;
                    uint32_t vc = m.channels()[0].elementCount;
                    if (vc == 0) continue;
                    // conservative combined bound across all resolved sub-blocks in
                    // this file, since a group-88 index does not itself say which
                    // sub-block it belongs to
                    vertexBound = std::max(vertexBound, vc);
                    haveVertexBound = true;
                    g_vertexCountMin = std::min(g_vertexCountMin, vc);
                    g_vertexCountMax = std::max(g_vertexCountMax, vc);
                }
            }
        }
    }
    if (haveVertexBound) ++g_boundCheckable;

    analyzeGroup88(archive, stem, content, lm, vertexBound, haveVertexBound, c70);
}

void walk(const std::string& archive, vpp::Container& c) {
    std::map<std::string, size_t> cl, gl;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".clmesh_pc", stem)) cl[stem] = i;
        else if (stemFor(n, ".glmesh_pc", stem)) gl[stem] = i;
    }
    for (const auto& kv : cl) {
        try {
            auto git = gl.find(kv.first);
            handleClmesh(archive, kv.first, c, kv.second, git == gl.end() ? 0 : git->second,
                        git != gl.end());
        } catch (const std::exception&) {
            continue;
        }
        if (g_clmeshSeen % 5000 == 0 && g_clmeshSeen > 0) {
            printf("  ... %lld .clmesh_pc scanned, %lld with count(+0x88)>0 so far\n", g_clmeshSeen,
                   g_clmeshGroup88);
            fflush(stdout);
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = c.openNested(i);
                walk(archive, nested);
            } catch (const std::exception&) {
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: dump_group88 <archive.vpp_pc> [more archives...] <out-dir>\n");
        return 1;
    }
    std::string outDir = argv[argc - 1];
    std::vector<std::string> archives;
    for (int i = 1; i < argc - 1; ++i) archives.push_back(argv[i]);

    std::ofstream csv(outDir + "/group88_dump.csv", std::ios::binary);
    csv << "archive,stem,outer_index,outer_offset,inner_count,outer_f04,outer_f08,outer_f0c,"
           "inner_index,inner_offset,u0,u1,u2,f0,f1,f2,magnitude,hex\n";
    g_csv = &csv;

    for (const std::string& path : archives) {
        std::vector<uint8_t> b = readFile(path);
        if (b.empty()) {
            printf("skip: %s (cannot read)\n", path.c_str());
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(path, c);
            printf("scanned %s\n", path.c_str());
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED %s: %s\n", path.c_str(), ex.what());
        }
    }
    csv.close();
    g_csv = nullptr;

    printf("\n=== .clmesh_pc scan ===\n");
    printf("  entries decompressed+parsed  : %lld / %lld\n", g_clmeshParsed, g_clmeshSeen);
    printf("  count(+0x88) > 0             : %lld\n", g_clmeshGroup88);
    printf("  ... but tail NOT located     : %lld (HANDOFF Sec9.59 measured this as 0/258)\n",
           g_clmeshNotLanded);
    printf("  group88 files analysed       : %lld\n", g_group88Files);
    printf("  outer 16-byte records        : %lld\n", g_outerRecords);
    printf("  inner 12-byte elements       : %lld\n", g_innerElements);

    printf("\n=== outer record padding fields (beyond innerCount@0x00, pointer@0x08) ===\n");
    printf("  +0x04 zero: %lld / %lld", g_outerField04Zero, g_outerRecords);
    if (g_outerField04Zero < g_outerRecords) printf("  (nonzero range %u..%u)", g_outerField04Min, g_outerField04Max);
    printf("\n");
    printf("  +0x08 zero: %lld / %lld  (expected 0 pre-resolution, like +0xb8)", g_outerField08Zero, g_outerRecords);
    if (g_outerField08Zero < g_outerRecords) printf("  (nonzero range %u..%u)", g_outerField08Min, g_outerField08Max);
    printf("\n");
    printf("  +0x0C zero: %lld / %lld", g_outerField0CZero, g_outerRecords);
    if (g_outerField0CZero < g_outerRecords) printf("  (nonzero range %u..%u)", g_outerField0CMin, g_outerField0CMax);
    printf("\n");

    printf("\n=== per-slot u32 distribution (TRIANGLE/INDEX hypothesis) ===\n");
    for (int s = 0; s < 3; ++s) {
        const U32Stat& st = g_slot[s];
        printf("  slot %d: min=%u max=%u  zero=%lld  <16:%lld <256:%lld <65536:%lld <16M:%lld huge:%lld\n",
               s, st.mn, st.mx, st.zero, st.lt16, st.lt256, st.lt65536, st.lt16m, st.huge);
    }

    printf("\n=== count(+0x88)/count(+0x48) co-occurrence and index-bound test ===\n");
    printf("  files with count(+0x88)>0 AND count(+0x48)>0 : %lld / %lld\n", g_group88AndRefA,
           g_group88Files);
    printf("  ... of those, ALL +0x48 refs resolved to MeshBlock : %lld / %lld\n",
           g_group88RefAAllResolved, g_group88AndRefA);
    printf("  files usable for the bound test (>=1 resolved vertex count) : %lld / %lld\n",
           g_boundCheckable, g_group88Files);
    if (g_boundCheckable > 0) {
        printf("  resolved vertex counts observed : %u .. %u\n", g_vertexCountMin, g_vertexCountMax);
        printf("  slot values (u0,u1,u2 combined) < max resolved vertex count in same file : %lld / %lld (%.4f%%)\n",
               g_valuesInBound, g_valuesTotal,
               g_valuesTotal ? 100.0 * g_valuesInBound / g_valuesTotal : 0.0);
    } else {
        printf("  NO files could be bound-tested this way - see co-occurrence figure above.\n");
    }
    printf("  slot values < count(+0x70) (56-byte array, same count as +0x48/+0x58 per Sec9.59) : %lld / %lld (%.4f%%)\n",
           g_valuesInBound70, g_innerElements * 3,
           g_innerElements ? 100.0 * g_valuesInBound70 / (g_innerElements * 3) : 0.0);

    printf("\n=== per-slot float distribution (PLANE/FACE hypothesis) ===\n");
    for (int s = 0; s < 3; ++s) {
        const FloatStat& st = g_fslot[s];
        printf("  slot %d: |v| min=%.6g max=%.6g mean=%.6g  NaN=%lld Inf=%lld subnormal-nonzero=%lld  near-unit[.9,1.1]=%lld small[0,2]=%lld huge(>1e6)=%lld  (n=%lld)\n",
               s, st.n > (st.nan_ + st.inf_) ? st.mn : 0.0, st.mx, st.n ? st.sum / std::max<long long>(1, st.n - st.nan_ - st.inf_) : 0.0,
               st.nan_, st.inf_, st.subnormalNonzero, st.nearUnit, st.smallPos, st.huge_, st.n);
    }
    printf("  (f0,f1,f2) vector magnitude: min=%.6g max=%.6g mean=%.6g  near-unit[|mag-1|<0.1]=%lld / %lld\n",
           g_magN ? g_magMin : 0.0, g_magN ? g_magMax : 0.0, g_magN ? g_magSum / g_magN : 0.0,
           g_magNearUnit, g_magN);

    printf("\n=== locality / vertex-sharing signature ===\n");
    printf("  mean distinct-value ratio per outer group (innerCount>=2) : %.4f  (n=%lld groups; "
           "1.0 = no sharing at all, lower = real mesh-like sharing)\n",
           g_dedupGroups ? g_dedupRatioSum / g_dedupGroups : 0.0, g_dedupGroups);

    printf("\nCSV written: %s/group88_dump.csv (%lld rows)\n", outDir.c_str(), g_innerElements);
    return 0;
}
