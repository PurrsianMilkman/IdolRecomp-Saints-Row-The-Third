// Real-data gates for the .cfmesh_pc LOD/fade table and the material-handle
// table, as spec-foliage-format.md now states them (Sec4, Sec7, Sec11.2,
// Sec11.3; re-synced 2026-09-20).
//
// Two independent passes over every file:
//   RAW  - reads the bytes directly from the spec's own field offsets with no
//          use of sr3foliage (finds the outer block by scanning for its magic),
//          so the reader is not the thing that certifies itself;
//   READ - runs sr3foliage::FoliageMesh::parse() and checks that its decoded
//          records equal the RAW decode, byte for byte / bit for bit.
//
// Accepts archives (.vpp_pc, walked recursively) and loose .cfmesh_pc files.
//
// Gates reproduced (Team A's numbers in brackets):
//   tail from B+i32(B+0x38) to EOF == count x 24            [19/19]
//   the same tail measured from B+0x38 itself == count x 24 [wrong reading: expect 0/19]
//   controls: tail == count x {16,20,28,32}                 [0/19 each]
//   fadeInStart<=fadeInEnd, fadeOutStart<=fadeOutEnd        [every record]
//   billboardFlag in {0,1}                                  [19/19]
//   fadeIn(i+1)==fadeOut(i), both ends                      [15 of 18 consecutive pairs;
//                fol_weed_a01 and both pairs of oas_fol_grass_a are the exceptions]
//   drawGroupIndex values                                   [0/1/2]
//   material-handle table identity 0..n-1                   [19/19]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3foliage/foliage_mesh.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size < 0 ? 0 : size));
    if (size > 0) f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

uint32_t u32(const std::vector<uint8_t>& b, size_t o) {
    return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
           (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
}

float f32(const std::vector<uint8_t>& b, size_t o) {
    uint32_t raw = u32(b, o);
    float v;
    std::memcpy(&v, &raw, sizeof(v));
    return v;
}

struct Rec {
    float in0, in1, out0, out1;
    uint32_t group, bill;
};

struct FileResult {
    std::string name;
    size_t size = 0;
    size_t base = 0;
    int magicHits = 0;
    uint32_t count = 0, w34 = 0;
    int32_t rel = 0;
    std::vector<Rec> recs;
};

std::vector<FileResult> g_files;
std::set<std::string> g_seenNames; // de-duplicate the same file seen twice in nested archives

int g_readerOk = 0, g_readerFail = 0;
int g_readerMatchesRaw = 0;       // reader.lodRecords() == RAW decode, every field bit-exact
int g_readerBaseMatches = 0;      // reader.outerBlockOffset() == scanned magic position
int g_readerCountMatches = 0;     // reader.lodRecordCount() == RAW +0x30
int g_readerOffsetMatches = 0;    // reader.lodTableOffset() == B + i32(B+0x38)
int g_handleTablePresent = 0, g_handleIdentity = 0, g_handleHeaderCount = 0, g_handleHeaderWord1Zero = 0;
int g_handleAlign8 = 0;
std::map<uint32_t, int> g_handleWord0, g_handleWord3;

// Observations BEYOND what the spec states (each is a byte-level fact checked
// against the real files; where it contradicts the spec's wording the printout
// says so). All measured from the raw bytes plus the reader's material offsets.
int g_obsMeshEndAtField10 = 0;    // u32(B+0x10) == meshRel + u32(B+meshRel+0x08)  (Mesh block end)
int g_obsHandleAfterMesh = 0;     // handle table start == align8(B + u32(B+0x10))
int g_obsSlotsAllZero = 0;        // runtime slot array entirely zero (spec Sec3: "zero-filled")
int g_obsSlotWord0IsRecord = 0;   // slot[i].word0 == (material record i offset - B), for all i
int g_obsSlotWord1Zero = 0;       // slot[i].word1 == 0 for all i
int g_obsSlotFiles = 0;
int g_obsZeroWords = 0;           // +0x0C,+0x14,+0x1C,+0x2C,+0x3C all zero (the runtime/pad words)
std::map<uint32_t, int> g_obsField24;
std::vector<std::string> g_readerFailNames;

void checkOne(const std::string& name, const uint8_t* data, size_t size) {
    if (!g_seenNames.insert(name).second) return; // already counted (same name seen again)
    std::vector<uint8_t> b(data, data + size);
    FileResult r;
    r.name = name;
    r.size = size;

    // RAW: locate the outer block by its magic 0x0FF1C1A1 followed by version 5.
    for (size_t o = 0; o + 0x40 <= b.size(); ++o) {
        if (u32(b, o) == 0x0FF1C1A1u && u32(b, o + 4) == 5) {
            r.base = o;
            ++r.magicHits;
        }
    }
    if (r.magicHits == 1) {
        r.count = u32(b, r.base + 0x30);
        r.w34 = u32(b, r.base + 0x34);
        r.rel = static_cast<int32_t>(u32(b, r.base + 0x38));
        size_t start = r.base + static_cast<size_t>(r.rel);
        if (r.rel >= 0 && start <= b.size()) {
            size_t tail = b.size() - start;
            for (size_t i = 0; i + 24 <= tail && i / 24 < 64; i += 24) {
                Rec c;
                c.in0 = f32(b, start + i + 0);
                c.in1 = f32(b, start + i + 4);
                c.out0 = f32(b, start + i + 8);
                c.out1 = f32(b, start + i + 12);
                c.group = u32(b, start + i + 16);
                c.bill = u32(b, start + i + 20);
                r.recs.push_back(c);
            }
        }
    }

    // READ: the reader, compared against RAW.
    try {
        sr3foliage::FoliageMesh f =
            sr3foliage::FoliageMesh::parse(sr3foliage::ByteView(b.data(), b.size()));
        ++g_readerOk;
        if (r.magicHits == 1 && f.outerBlockOffset() == r.base) ++g_readerBaseMatches;
        if (f.lodRecordCount() == r.count) ++g_readerCountMatches;
        if (f.hasLodTable() && f.lodTableOffset() == r.base + static_cast<size_t>(r.rel))
            ++g_readerOffsetMatches;
        bool same = f.lodRecords().size() == r.count && r.recs.size() == r.count;
        if (same) {
            for (size_t i = 0; i < r.recs.size(); ++i) {
                const auto& a = f.lodRecords()[i];
                const Rec& c = r.recs[i];
                uint32_t ab[4], cb[4];
                float av[4] = {a.fadeInStart, a.fadeInEnd, a.fadeOutStart, a.fadeOutEnd};
                float cv[4] = {c.in0, c.in1, c.out0, c.out1};
                std::memcpy(ab, av, sizeof(av));
                std::memcpy(cb, cv, sizeof(cv));
                for (int k = 0; k < 4; ++k) same = same && ab[k] == cb[k];
                same = same && a.drawGroupIndex == c.group && a.billboardFlag == c.bill;
            }
        }
        if (same) ++g_readerMatchesRaw;

        if (r.magicHits == 1) {
            const size_t B = r.base;
            const uint32_t meshRel = u32(b, B + 0x08);
            const uint32_t f10 = u32(b, B + 0x10);
            if (meshRel != 0xFFFFFFFFu && f10 == meshRel + u32(b, B + meshRel + 0x08)) ++g_obsMeshEndAtField10;
            if (f.hasMaterialHandleTable() &&
                f.materialHandleTable().offset == (B + f10 + 7) / 8 * 8)
                ++g_obsHandleAfterMesh;
            ++g_obsSlotFiles;
            const size_t slots = B + u32(b, B + 0x18);
            bool allZero = true, w0 = true, w1 = true;
            for (size_t i = 0; i < f.materials().size(); ++i) {
                uint32_t a = u32(b, slots + i * 8), c = u32(b, slots + i * 8 + 4);
                if (a != 0 || c != 0) allZero = false;
                if (a != f.materials()[i].offset - B) w0 = false;
                if (c != 0) w1 = false;
            }
            g_obsSlotsAllZero += allZero;
            g_obsSlotWord0IsRecord += w0;
            g_obsSlotWord1Zero += w1;
            g_obsZeroWords += u32(b, B + 0x0C) == 0 && u32(b, B + 0x14) == 0 && u32(b, B + 0x1C) == 0 &&
                              u32(b, B + 0x2C) == 0 && u32(b, B + 0x3C) == 0;
            ++g_obsField24[u32(b, B + 0x24)];
        }

        if (f.hasMaterialHandleTable()) {
            ++g_handleTablePresent;
            const auto& t = f.materialHandleTable();
            if (t.isIdentity() && f.materialHandlesAreIdentity()) ++g_handleIdentity;
            if (t.headerCount == f.materialCount()) ++g_handleHeaderCount;
            if (t.headerWord1 == 0) ++g_handleHeaderWord1Zero;
            if (t.offset % 8 == 0) ++g_handleAlign8;
            ++g_handleWord0[t.headerWord0];
            ++g_handleWord3[t.headerWord3];
        }
    } catch (const std::exception& ex) {
        ++g_readerFail;
        g_readerFailNames.push_back(name + ": " + ex.what());
    }
    g_files.push_back(std::move(r));
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                try {
                    vpp::Container nested = c.openNested(i);
                    walk(nested);
                    continue;
                } catch (const vpp::FormatError&) {
                }
                if (endsWith(e.name, ".cfmesh_pc")) {
                    vpp::ByteView r = c.rawEntryBytes(i);
                    checkOne(e.name, r.data(), r.size());
                }
            } else {
                // Only inflate the entries we want (this walk is dominated by
                // decompression of a ~1.5 GB archive otherwise).
                if (!endsWith(e.name, ".cfmesh_pc")) continue;
                auto r = c.decompressEntry(i);
                bool usable = r.status == vpp::DecodeStatus::Ok ||
                              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                              r.status == vpp::DecodeStatus::ContentValidated ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
                if (usable && endsWith(e.name, ".cfmesh_pc")) {
                    checkOne(e.name, r.data.data(), r.data.size());
                }
            }
        } catch (const std::exception&) {
            continue;
        }
    }
}

std::string baseName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::vector<uint8_t> bytes = readFile(arg);
        if (bytes.empty()) {
            printf("could not read %s\n", arg.c_str());
            continue;
        }
        if (endsWith(arg, ".cfmesh_pc")) {
            checkOne(baseName(arg), bytes.data(), bytes.size());
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c);
            printf("scanned %s\n", arg.c_str());
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", arg.c_str(), ex.what());
        }
    }

    const int n = static_cast<int>(g_files.size());
    printf("\n=== .cfmesh_pc LOD/fade table + handle table gates (spec Sec11.2 / Sec11.3) ===\n");
    printf("files found: %d (spec: 19)\n", n);
    if (n == 0) return 1;

    // ---- per-file listing ----
    printf("\n-- per file: base B, count(+0x30), +0x34, rel(+0x38), tail, table --\n");
    for (const auto& r : g_files) {
        size_t start = r.base + static_cast<size_t>(r.rel);
        printf("%-28s size=%6zu B=%4zu count=%u w34=%3u rel=%5d tail=%3zu start%%4=%zu\n",
               r.name.c_str(), r.size, r.base, r.count, r.w34, r.rel,
               r.rel >= 0 && start <= r.size ? r.size - start : static_cast<size_t>(0),
               start % 4);
        for (size_t i = 0; i < r.recs.size(); ++i) {
            const Rec& c = r.recs[i];
            printf("    [%zu] in=(%g,%g) out=(%g,%g) group=%u billboard=%u\n", i, c.in0, c.in1,
                   c.out0, c.out1, c.group, c.bill);
        }
    }

    // ---- gate 0: the outer block is found exactly once ----
    int oneHit = 0;
    for (const auto& r : g_files) oneHit += r.magicHits == 1;
    printf("\nG0 outer magic 0x0FF1C1A1+version 5 found exactly once (raw scan): %d/%d\n", oneHit, n);

    // ---- gate 1: exact tail; the wrong reading; the stride controls ----
    int exact = 0, wrongReading = 0;
    int ctl[4] = {0, 0, 0, 0};
    const size_t strides[4] = {16, 20, 28, 32};
    for (const auto& r : g_files) {
        if (r.magicHits != 1) continue;
        long long start = static_cast<long long>(r.base) + r.rel;
        long long tail = static_cast<long long>(r.size) - start;
        exact += tail == static_cast<long long>(r.count) * 24;
        long long naive = static_cast<long long>(r.size) - static_cast<long long>(r.base + 0x38);
        wrongReading += naive == static_cast<long long>(r.count) * 24;
        for (int k = 0; k < 4; ++k) ctl[k] += tail == static_cast<long long>(r.count * strides[k]);
    }
    printf("G1 tail from B+i32(B+0x38) to EOF == count x 24 exactly : %d/%d   [expect 19/19]\n", exact, n);
    printf("G1' tail measured from B+0x38 ITSELF == count x 24        : %d/%d   [wrong reading; expect 0/19]\n",
           wrongReading, n);
    for (int k = 0; k < 4; ++k)
        printf("G1 control stride %2zu: tail == count x %2zu                : %d/%d   [expect 0/19]\n",
               strides[k], strides[k], ctl[k], n);

    // ---- gate 2/3/4: ordering, billboard domain, group values ----
    int totalRecs = 0, inOk = 0, outOk = 0, billOk = 0, filesBillOk = 0;
    std::map<uint32_t, int> groupHist, billHist;
    std::map<uint32_t, std::set<std::string>> groupFiles;
    std::map<uint32_t, int> countHist, w34Hist;
    for (const auto& r : g_files) {
        bool fileBillOk = true;
        ++countHist[r.count];
        ++w34Hist[r.w34];
        for (const Rec& c : r.recs) {
            ++totalRecs;
            inOk += c.in0 <= c.in1;
            outOk += c.out0 <= c.out1;
            bool bok = c.bill == 0 || c.bill == 1;
            billOk += bok;
            fileBillOk = fileBillOk && bok;
            ++groupHist[c.group];
            groupFiles[c.group].insert(r.name);
            ++billHist[c.bill];
        }
        filesBillOk += fileBillOk;
    }
    printf("G2 fadeInStart <= fadeInEnd   : %d/%d records   [expect all]\n", inOk, totalRecs);
    printf("G2 fadeOutStart <= fadeOutEnd : %d/%d records   [expect all]\n", outOk, totalRecs);
    printf("G3 billboardFlag in {0,1}     : %d/%d files (%d/%d records)   [expect 19/19]\n", filesBillOk, n,
           billOk, totalRecs);
    printf("   billboardFlag histogram: ");
    for (const auto& kv : billHist) printf("%u:%d ", kv.first, kv.second);
    printf("\nG4 drawGroupIndex histogram (records): ");
    for (const auto& kv : groupHist) printf("%u:%d ", kv.first, kv.second);
    printf("  [expect values 0/1/2 only]\n");
    for (const auto& kv : groupFiles) {
        if (kv.first == 0) continue;
        printf("   drawGroupIndex %u appears in:", kv.first);
        for (const auto& nme : kv.second) printf(" %s", nme.c_str());
        printf("\n");
    }
    printf("   records per file (outer +0x30) histogram: ");
    for (const auto& kv : countHist) printf("%u:%d ", kv.first, kv.second);
    printf("  [expect 1-3]\n");
    printf("   outer +0x34 values (OPEN): ");
    for (const auto& kv : w34Hist) printf("%u(x%d) ", kv.first, kv.second);
    printf("\n");

    // ---- gate 5: cross-fade continuity ----
    int pairs = 0, matches = 0;
    std::vector<std::string> exceptions;
    for (const auto& r : g_files) {
        for (size_t i = 0; i + 1 < r.recs.size(); ++i) {
            ++pairs;
            const Rec& a = r.recs[i];
            const Rec& c = r.recs[i + 1];
            if (c.in0 == a.out0 && c.in1 == a.out1) {
                ++matches;
            } else {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s pair %zu->%zu: fadeIn(next)=(%g,%g) vs fadeOut(this)=(%g,%g)",
                         r.name.c_str(), i, i + 1, c.in0, c.in1, a.out0, a.out1);
                exceptions.push_back(buf);
            }
        }
    }
    printf("G5 consecutive LODs cross-fade over the same interval: %d/%d pairs   [expect 15/18]\n", matches,
           pairs);
    for (const auto& e : exceptions) printf("   exception: %s\n", e.c_str());

    // ---- gate 6: material-handle table ----
    printf("G6 material-handle table located (backed up from the runtime slot array): %d/%d\n",
           g_handleTablePresent, n);
    printf("G6 handle u32s are the identity 0..n-1                 : %d/%d   [expect 19/19]\n", g_handleIdentity, n);
    printf("G6 table header count == outer +0x20 material count    : %d/%d   [spec Sec3: 19/19]\n",
           g_handleHeaderCount, n);
    printf("G6 table header word1 == 0                             : %d/%d\n", g_handleHeaderWord1Zero, n);
    printf("G6 table start 8-aligned                               : %d/%d   [spec Sec3: 8-aligned]\n",
           g_handleAlign8, n);
    printf("   header word0 (ptr-slot) values: ");
    for (const auto& kv : g_handleWord0) printf("0x%X(x%d) ", kv.first, kv.second);
    printf("\n   header word3 ('?') values: ");
    for (const auto& kv : g_handleWord3) printf("0x%X(x%d) ", kv.first, kv.second);
    printf("\n");

    // ---- observations beyond the spec's text ----
    printf("\n-- observations beyond the spec text (raw bytes; NOT gates) --\n");
    printf("O1 outer +0x10 (nonzero on disk) == Mesh-block end (meshRel + u32 at mesh+0x08)  : %d/%d\n",
           g_obsMeshEndAtField10, n);
    printf("O2 handle-table start == align8(that Mesh-block end), no gap                       : %d/%d\n",
           g_obsHandleAfterMesh, n);
    printf("O3 runtime slot array entirely zero (spec Sec3 says 'zero-filled')                 : %d/%d\n",
           g_obsSlotsAllZero, g_obsSlotFiles);
    printf("O3 slot[i] first u32 == block-relative offset of material record i, all i           : %d/%d\n",
           g_obsSlotWord0IsRecord, g_obsSlotFiles);
    printf("O3 slot[i] second u32 == 0, all i                                                   : %d/%d\n",
           g_obsSlotWord1Zero, g_obsSlotFiles);
    printf("O4 outer +0x0C,+0x14,+0x1C,+0x2C,+0x3C all zero                                     : %d/%d\n",
           g_obsZeroWords, g_obsSlotFiles);
    printf("O5 outer +0x24 (not in spec Sec4): %zu distinct values across %d files, values:",
           g_obsField24.size(), g_obsSlotFiles);
    for (const auto& kv : g_obsField24) printf(" 0x%08X", kv.first);
    printf("\n");

    // ---- the reader vs RAW ----
    printf("\n-- reader (sr3foliage::FoliageMesh::parse) vs the RAW decode above --\n");
    printf("parse() OK                                : %d/%d\n", g_readerOk, n);
    printf("outerBlockOffset() == scanned magic       : %d/%d\n", g_readerBaseMatches, n);
    printf("lodRecordCount() == RAW +0x30             : %d/%d\n", g_readerCountMatches, n);
    printf("lodTableOffset()  == B + i32(B+0x38)      : %d/%d\n", g_readerOffsetMatches, n);
    printf("lodRecords() bit-identical to RAW decode  : %d/%d\n", g_readerMatchesRaw, n);
    for (const auto& s : g_readerFailNames) printf("   PARSE FAIL %s\n", s.c_str());

    bool pass = n == 19 && oneHit == n && exact == n && wrongReading == 0 && ctl[0] == 0 && ctl[1] == 0 &&
                ctl[2] == 0 && ctl[3] == 0 && inOk == totalRecs && outOk == totalRecs && filesBillOk == n &&
                pairs == 18 && matches == 15 && g_handleIdentity == n && g_readerOk == n &&
                g_readerMatchesRaw == n && g_readerBaseMatches == n && g_readerCountMatches == n &&
                g_readerOffsetMatches == n;
    printf("\nOVERALL: %s\n", pass ? "ALL GATES MATCH THE SPEC'S NUMBERS" : "DISAGREEMENT - see above");
    return pass ? 0 : 1;
}
