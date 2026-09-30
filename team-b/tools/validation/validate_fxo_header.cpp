// Independent check of spec-fxo-format.md §7's header-size formula on every
// real .fxo* file the archives contain.
//
//     H = 0x80 + 0x10*c0 + 8*(c1+c2+c3+c4) + 8*(nVS+nMid+nPS) + 0x10*c8
//     blobs: running offset starts at H (unrounded); each non-empty blob starts
//     at the running offset rounded up to 16; vertex table, then middle
//     (geometry) table, then pixel table.                     (§6.4, §7.2, §7.5)
//
// The spec verified this on ONE trusted sample (entry 0 of shaders.vpp_pc,
// which is mode-(a): only its first entry is trusted under the container
// spec). The identity to test is
//
//     (running offset after the last blob) == file length
//
// It compares a number derived from the file's own header with the container's
// independently declared size (directory +0x0C), so it is an exact-consumption
// gate that ALSO serves as content validation for the non-first entries the
// container library flags as OkUnconfirmedContent - a truncated or garbled
// decode cannot satisfy it except by coincidence.
//
// APPLICABILITY CONDITION (stated, and counted separately from FAILURE): the
// formula is applied only where WrapperHeader::tryParse() succeeds - at least
// 0x80 bytes, magic 0x4B42A1EE, signed version >= 14, all nine counts
// non-negative, and all nine tables inside the file. Files outside that
// condition are counted with their reason, never dropped.
//
// Populations: `.fxo_pc` (the ask), plus `.fxo_pc_dx11` and bare `.fxo`
// reported SEPARATELY (spec §8.2 says the dx11 family shares the wrapper; it
// is not in the ask, so it is labelled supplementary).
//
// CONTROLS: seven wrong variants of the formula/alignment are scored on the
// same files (a control must be able to fail and the count of files on which
// it agrees with the spec's rule is printed so a control that cannot
// discriminate is visible).
//
// Usage: validate_fxo_header [archive.vpp_pc ...]   (default: every archive in
// the game's packfiles/pc/cache directory)
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <zlib.h>

#include "sr3fxo/d3d9_blob.h"
#include "sr3fxo/wrapper_header.h"
#include "vpp/container.h"

namespace fs = std::filesystem;
using sr3fxo::Stage;
using sr3fxo::WrapperHeader;

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
const char* statusName(vpp::DecodeStatus s) {
    switch (s) {
    case vpp::DecodeStatus::Ok: return "Ok";
    case vpp::DecodeStatus::OkUnconfirmedContent: return "OkUnconfirmedContent";
    case vpp::DecodeStatus::ContentValidated: return "ContentValidated";
    case vpp::DecodeStatus::ContentValidationFailed: return "ContentValidationFailed";
    case vpp::DecodeStatus::RecoveredSharedStream: return "RecoveredSharedStream";
    case vpp::DecodeStatus::RecoveredSharedStreamLongChain: return "RecoveredSharedStreamLongChain";
    case vpp::DecodeStatus::NoValidZlibHeaderAtOffset: return "NoValidZlibHeaderAtOffset";
    case vpp::DecodeStatus::ZlibStreamError: return "ZlibStreamError";
    case vpp::DecodeStatus::SizeMismatch: return "SizeMismatch";
    }
    return "?";
}
bool statusUsable(vpp::DecodeStatus s) {
    return s == vpp::DecodeStatus::Ok || s == vpp::DecodeStatus::OkUnconfirmedContent ||
           s == vpp::DecodeStatus::ContentValidated || s == vpp::DecodeStatus::RecoveredSharedStream ||
           s == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
}
uint64_t fnv1a(const uint8_t* d, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= d[i];
        h *= 1099511628211ull;
    }
    return h;
}

struct Occ {
    std::string archive, path, name, ext, how;
    size_t entryIndex = 0;
    size_t declaredSize = 0; // directory +0x0C
    bool sharedStreamFlag = false;
    std::vector<uint8_t> data;
    uint64_t hash = 0;
};

std::vector<Occ> g_occ;
std::map<std::string, size_t> g_census; // "ext | archive | status"
size_t g_notDecodedOther = 0, g_decodeFail = 0;

void takeEntry(const vpp::Container& c, size_t i, const std::string& archive, const std::string& path) {
    const auto& e = c.entries()[i];
    Occ o;
    o.archive = archive;
    o.path = path;
    o.name = e.name;
    o.ext = lower(e.name.substr(e.name.find_last_of('.')));
    o.entryIndex = i;
    o.declaredSize = e.payload.decompressedLength;
    o.sharedStreamFlag = c.header().isSharedStreamMode();
    if (e.payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView v = c.rawEntryBytes(i);
        o.data.assign(v.data(), v.data() + v.size());
        o.how = "raw";
    } else {
        vpp::DecompressResult r = c.decompressEntry(i);
        o.how = statusName(r.status);
        ++g_census[o.ext + " | " + archive + " | " + o.how];
        if (!statusUsable(r.status)) {
            ++g_decodeFail;
            return;
        }
        o.data = std::move(r.data);
        o.hash = fnv1a(o.data.data(), o.data.size());
        g_occ.push_back(std::move(o));
        return;
    }
    ++g_census[o.ext + " | " + archive + " | " + o.how];
    o.hash = fnv1a(o.data.data(), o.data.size());
    g_occ.push_back(std::move(o));
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const std::string ln = lower(e.name);
        const size_t dot = ln.find_last_of('.');
        const bool isFxo = dot != std::string::npos && ln.compare(dot, 4, ".fxo") == 0;
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                if (isFxo) {
                    takeEntry(c, i, archive, path);
                } else {
                    try {
                        vpp::Container n = c.openNested(i);
                        walk(n, archive, path + "/" + e.name);
                    } catch (const vpp::FormatError&) {
                    }
                }
            } else if (isFxo) {
                takeEntry(c, i, archive, path);
            } else if (endsWith(ln, ".str2_pc") || endsWith(ln, ".vpp_pc")) {
                vpp::DecompressResult r = c.decompressEntry(i);
                if (statusUsable(r.status)) {
                    try {
                        vpp::Container n{vpp::ByteView(r.data.data(), r.data.size())};
                        walk(n, archive, path + "/" + e.name);
                    } catch (const std::exception&) {
                        ++g_decodeFail;
                    }
                } else {
                    ++g_decodeFail;
                }
            } else {
                ++g_notDecodedOther;
            }
        } catch (const std::exception&) {
            ++g_decodeFail;
        }
    }
}

// ---------------------------------------------------------------- analysis

struct Verdict {
    bool applicable = false;
    std::string why;
    WrapperHeader h;
    size_t endSpec = 0, endD9 = 0;
    bool passSpec = false, passD9 = false;
    bool blobTokensOk = false;   // every blob passes its payload check (see below)
    size_t blobsChecked = 0;
    // controls: end-of-blobs == size under the wrong rule
    std::array<bool, 7> ctl{};
    std::array<bool, 7> ctlDiffers{}; // wrong rule's end differs from the spec rule's end (control had a chance to fail)
};

const char* kCtlName[7] = {"K1 alignment 8 instead of 16", "K2 no alignment at all",
                           "K3 first blob unaligned (later ones 16)", "K4 H without the 0x10*c8 term",
                           "K5 H without the 8*(c3+c4) terms", "K6 T1..T4 entries counted as 0x10 not 8",
                           "K7 H without the stage-table entries"};

size_t endUnder(const WrapperHeader& h, sr3fxo::LayoutOptions opt) {
    size_t e = 0;
    h.layoutBlobs(opt, e);
    return e;
}

// Payload check: DX9 blobs by their public tokens; dx11 blobs by "DXBC".
bool payloadOk(const std::vector<uint8_t>& d, const WrapperHeader& h, const std::string& ext, size_t& checked) {
    size_t e = 0;
    auto blobs = h.layoutBlobs(e);
    bool ok = true;
    for (const auto& b : blobs) {
        if (b.offset + b.length > d.size()) return false;
        vpp::ByteView v(d.data() + b.offset, b.length);
        ++checked;
        if (ext == ".fxo_pc") {
            if (b.stage == Stage::Middle) continue; // no D3D9 geometry stage exists
            sr3fxo::D3d9BlobInfo info = sr3fxo::inspectD3d9Blob(v);
            const bool wantVertex = b.stage == Stage::Vertex;
            if (!info.versionTokenValid || info.isVertex != wantVertex || !info.endTokenAtLastDword) ok = false;
        } else if (ext == ".fxo_pc_dx11") {
            if (b.length < 4 || std::memcmp(v.data(), "DXBC", 4) != 0) ok = false;
        }
    }
    return ok;
}

Verdict analyse(const Occ& o) {
    Verdict v;
    vpp::ByteView view(o.data.data(), o.data.size());
    v.applicable = WrapperHeader::tryParse(view, v.h, v.why);
    if (!v.applicable) return v;
    const WrapperHeader& h = v.h;
    v.endSpec = endUnder(h, {});
    sr3fxo::LayoutOptions d9;
    d9.includeMiddle = false;
    v.endD9 = endUnder(h, d9);
    v.passSpec = v.endSpec == o.data.size();
    v.passD9 = v.endD9 == o.data.size();

    const size_t H = h.headerSize();
    auto set = [&](int k, sr3fxo::LayoutOptions opt) {
        const size_t e = endUnder(h, opt);
        v.ctl[static_cast<size_t>(k)] = e == o.data.size();
        v.ctlDiffers[static_cast<size_t>(k)] = e != v.endSpec;
    };
    { sr3fxo::LayoutOptions x; x.alignment = 8; set(0, x); }
    { sr3fxo::LayoutOptions x; x.alignment = 1; set(1, x); }
    { sr3fxo::LayoutOptions x; x.alignFirstBlob = false; set(2, x); }
    { sr3fxo::LayoutOptions x; x.startOffset = H - 0x10u * h.passCount(); set(3, x); }
    { sr3fxo::LayoutOptions x; x.startOffset = H - 8u * static_cast<size_t>(h.count(3) + h.count(4)); set(4, x); }
    { sr3fxo::LayoutOptions x;
      x.startOffset = H + 8u * static_cast<size_t>(h.count(1) + h.count(2) + h.count(3) + h.count(4)); set(5, x); }
    { sr3fxo::LayoutOptions x;
      x.startOffset = H - 8u * static_cast<size_t>(h.vertexCount() + h.middleCount() + h.pixelCount()); set(6, x); }

    if (v.passSpec) v.blobTokensOk = payloadOk(o.data, h, o.ext, v.blobsChecked);
    return v;
}

// ---------------------------------------------------------------- reporting

template <class K>
void printHist(const char* label, const std::map<K, size_t>& m) {
    printf("  %s:", label);
    for (const auto& kv : m) printf("  %lld x%zu", static_cast<long long>(kv.first), kv.second);
    printf("\n");
}

void trustedSample() {
    printf("\n=== 1. THE TRUSTED SAMPLE (spec §9.1: rfg-skybox-meteors.fxo_pc, entry 0 of shaders.vpp_pc, 1,408 bytes) ===\n");
    const Occ* t = nullptr;
    for (const Occ& o : g_occ)
        if (o.name == "rfg-skybox-meteors.fxo_pc" && o.entryIndex == 0) t = &o;
    if (!t) {
        printf("  NOT FOUND as entry 0 of any container - nothing checked\n");
        return;
    }
    printf("  archive %s, entry index %zu, size %zu (directory declared %zu), status %s\n", t->archive.c_str(),
           t->entryIndex, t->data.size(), t->declaredSize, t->how.c_str());
    Verdict v = analyse(*t);
    if (!v.applicable) {
        printf("  formula not applicable: %s\n", v.why.c_str());
        return;
    }
    const WrapperHeader& h = v.h;
    struct Row { const char* what; long long got, want; };
    const Row rows[] = {
        {"version (§7.1: 14)", h.version(), 14},
        {"flags dword @0x08 (§7.1: 0x100)", h.flags(), 0x100},
        {"c0 T0 count (§7.1: 0)", h.count(0), 0},
        {"c1 T1 count (1)", h.count(1), 1},
        {"c2 T2 count (0)", h.count(2), 0},
        {"c3 T3 count (4)", h.count(3), 4},
        {"c4 T4 count (0)", h.count(4), 0},
        {"nVS (1)", h.vertexCount(), 1},
        {"nMid (0)", h.middleCount(), 0},
        {"nPS (1)", h.pixelCount(), 1},
        {"c8 T8 count (1)", static_cast<long long>(h.passCount()), 1},
        {"H header size (§7.2: 200 = 0xC8)", static_cast<long long>(h.headerSize()), 200},
        {"first blob offset (208 = 0xD0)", static_cast<long long>(h.firstBlobOffset()), 208},
        {"vertex table entry 0 length @0xA8 (732)", h.vertexTable().empty() ? -1 : static_cast<long long>(h.vertexTable()[0].length), 732},
        {"pixel table entry 0 length @0xB0 (464)", h.pixelTable().empty() ? -1 : static_cast<long long>(h.pixelTable()[0].length), 464},
        {"end of blobs == file length (1408)", static_cast<long long>(v.endSpec), 1408},
    };
    size_t good = 0;
    for (const Row& r : rows) {
        printf("  %-46s got %-8lld spec %-8lld %s\n", r.what, r.got, r.want, r.got == r.want ? "match" : "DIFFERS");
        good += r.got == r.want;
    }
    printf("  %zu / %zu spec-stated values reproduced\n", good, sizeof(rows) / sizeof(rows[0]));
    size_t e = 0;
    auto blobs = h.layoutBlobs(e);
    for (const auto& b : blobs) printf("  blob: %s table entry %zu at file offset %zu, length %zu (offset %% 16 = %zu)\n",
                                      b.stage == Stage::Vertex ? "vertex" : b.stage == Stage::Pixel ? "pixel" : "middle",
                                      b.tableIndex, b.offset, b.length, b.offset % 16);
    // role bytes / T8 / 0xB8 dword
    printf("  role bytes 0x60..0x69:");
    for (size_t k = 0; k < 10; ++k) printf(" %02X", static_cast<uint8_t>(h.roleIndexByte(k)));
    printf("  base byte 0x6A: %d\n", h.groupBase());
    if (!h.passes().empty()) {
        const auto& p = h.passes()[0];
        printf("  T8[0]: vertexIndex %d, word2 %d, pixelIndex %d, word6 %d, nameHash 0x%08X, word0C 0x%08X\n", p.vertexIndex,
               p.word2, p.pixelIndex, p.word6, p.nameHash, p.word0C);
        vpp::ByteView view(t->data.data(), t->data.size());
        printf("  dword at file offset 0xB8 (spec: first dword of T8[0], 0xFFFF0000): 0x%08X\n", view.readU32LE(0xB8));
    }
    // T1/T3 vs the bytecode's own constant table
    size_t matched = 0, total = 0;
    std::vector<sr3fxo::D3d9Constant> all;
    for (const auto& b : blobs) {
        auto info = sr3fxo::inspectD3d9Blob(vpp::ByteView(t->data.data() + b.offset, b.length));
        for (auto& c : info.constants) all.push_back(c);
    }
    auto check = [&](const char* tn, const std::vector<sr3fxo::ConstantEntry>& tbl) {
        for (const auto& c : tbl) {
            ++total;
            bool hit = false;
            std::string nm;
            for (const auto& k : all)
                if (sr3fxo::hashLowerName(k.name) == c.nameHash && k.registerIndex == c.registerIndex) {
                    hit = true;
                    nm = k.name;
                }
            matched += hit;
            printf("  %s entry: hash 0x%08X reg %u perElem %u elems %u -> %s%s\n", tn, c.nameHash, c.registerIndex,
                   c.registersPerElement, c.elementCount, hit ? "constant table name " : "NO name+register match",
                   nm.c_str());
        }
    };
    check("T1", h.constantsT1());
    check("T2", h.constantsT2());
    check("T3", h.constantsT3());
    check("T4", h.constantsT4());
    printf("  T1..T4 entries whose hash = CRC(lower(name)) AND register = the bytecode's register: %zu / %zu (spec §9.2: 5/5)\n",
           matched, total);
}

// The exact-consumption identity fails on most decoded files (see the report).
// Two readings are possible: the formula is wrong, or the decoded bytes are
// not the whole file (the container spec's known mode-(a) limitation for
// non-first entries). This separates them: if the LAYOUT is right and the
// data merely stops early, every blob whose bytes are present must be a
// perfect D3D9 blob at exactly the computed offset; if the layout were wrong
// the very first blob would already miss. A wrong-offset control (same
// check at offset+8 and +16) shows the check can fail.
void prefixConsistency(const std::vector<const Occ*>& occ, const std::vector<Verdict>& vs) {
    size_t files = 0, blobs = 0, startInside = 0, startOk = 0, fullInside = 0, fullOk = 0;
    size_t shiftStartOk8 = 0, shiftStartOk16 = 0;
    size_t failFiles = 0, failTailZero = 0, failTailNonZero = 0;
    std::map<int, size_t> ratio;           // size / end-of-blobs in tenths, failing files
    std::map<size_t, size_t> fitHist;      // (blobs fully present) of (blobs total), failing files: key "present*100+total"
    size_t firstBlobStartOk = 0, firstBlobPresent = 0;
    for (size_t i = 0; i < occ.size(); ++i) {
        const Verdict& v = vs[i];
        if (!v.applicable) continue;
        ++files;
        const auto& d = occ[i]->data;
        size_t e = 0;
        auto bl = v.h.layoutBlobs(e);
        size_t present = 0;
        bool first = true;
        for (const auto& b : bl) {
            ++blobs;
            auto tokenOkAt = [&](size_t off) {
                if (off + 8 > d.size()) return false;
                const size_t len = std::min(b.length, d.size() - off);
                sr3fxo::D3d9BlobInfo inf = sr3fxo::inspectD3d9Blob(vpp::ByteView(d.data() + off, len));
                return inf.versionTokenValid && inf.isVertex == (b.stage == Stage::Vertex);
            };
            if (b.stage != Stage::Middle) {
                if (b.offset + 8 <= d.size()) {
                    ++startInside;
                    startOk += tokenOkAt(b.offset);
                    shiftStartOk8 += tokenOkAt(b.offset + 8);
                    shiftStartOk16 += tokenOkAt(b.offset + 16);
                    if (first && !v.passSpec) { ++firstBlobPresent; firstBlobStartOk += tokenOkAt(b.offset); }
                }
                if (b.offset + b.length <= d.size()) {
                    ++fullInside;
                    ++present;
                    sr3fxo::D3d9BlobInfo inf = sr3fxo::inspectD3d9Blob(vpp::ByteView(d.data() + b.offset, b.length));
                    fullOk += (inf.versionTokenValid && inf.isVertex == (b.stage == Stage::Vertex) && inf.endTokenAtLastDword);
                }
            }
            first = false;
        }
        if (!v.passSpec) {
            ++failFiles;
            ++ratio[static_cast<int>(10.0 * static_cast<double>(d.size()) / static_cast<double>(v.endSpec))];
            bool z = true;
            for (size_t k = d.size() >= 32 ? d.size() - 32 : 0; k < d.size(); ++k)
                if (d[k]) z = false;
            (z ? failTailZero : failTailNonZero)++;
            ++fitHist[present * 100 + bl.size()];
        }
    }
    printf("PREFIX CONSISTENCY (layout right + data merely short?  non-middle blobs only, spec layout rule):\n");
    printf("  blobs laid out: %zu; blobs whose first 8 bytes are inside the decoded data: %zu, of which a right-stage D3D9 version token sits at the computed offset: %zu\n",
           blobs, startInside, startOk);
    printf("     control - the same test at computed offset +8: %zu, at +16: %zu\n", shiftStartOk8, shiftStartOk16);
    printf("  blobs entirely inside the decoded data: %zu, of which version token + end token both perfect: %zu\n", fullInside, fullOk);
    if (failFiles) {
        printf("  in the %zu files that do not consume exactly: FIRST blob's start is inside the data in %zu and carries a valid token in %zu\n",
               failFiles, firstBlobPresent, firstBlobStartOk);
        printf("     last 32 decoded bytes all zero: %zu, not all zero: %zu\n", failTailZero, failTailNonZero);
        printf("     decoded size / header-implied size, in tenths (bucket x files):");
        for (const auto& kv : ratio) printf("  %d x%zu", kv.first, kv.second);
        printf("\n");
    }
    (void)files;
}

void populationReport(const std::string& ext, const char* label, const std::vector<Occ>& src) {
    std::vector<const Occ*> occ;
    for (const Occ& o : src)
        if (o.ext == ext) occ.push_back(&o);
    printf("\n=== %s ===\n", label);
    printf("directory entries with this extension whose bytes were obtained (D1): %zu\n", occ.size());
    if (occ.empty()) return;

    std::map<std::string, size_t> byHow, firstEntry;
    std::set<uint64_t> distinct;
    for (const Occ* o : occ) {
        ++byHow[o->how];
        distinct.insert(o->hash);
        if (o->entryIndex == 0) ++firstEntry[o->name];
    }
    printf("  distinct contents: %zu\n", distinct.size());
    printf("  by obtain-status:");
    for (const auto& kv : byHow) printf("  %s x%zu", kv.first.c_str(), kv.second);
    printf("\n  entry-0-of-container occurrences:");
    for (const auto& kv : firstEntry) printf("  %s x%zu", kv.first.c_str(), kv.second);
    printf("\n");

    std::vector<Verdict> vs;
    vs.reserve(occ.size());
    for (const Occ* o : occ) vs.push_back(analyse(*o));

    size_t appl = 0;
    std::map<std::string, size_t> notWhy;
    for (const Verdict& v : vs) {
        if (v.applicable) ++appl;
        else ++notWhy[v.why];
    }
    printf("formula APPLICABLE (>=0x80 bytes, magic, version>=14, counts>=0, tables inside file): %zu / %zu\n", appl, occ.size());
    for (const auto& kv : notWhy) printf("   not applicable: %-52s x%zu\n", kv.first.c_str(), kv.second);
    if (appl < occ.size() && ext == ".fxo") {
        for (size_t i = 0; i < occ.size(); ++i)
            if (!vs[i].applicable) {
                printf("   %s (%zu bytes) begins:", occ[i]->name.c_str(), occ[i]->data.size());
                for (size_t k = 0; k < 24 && k < occ[i]->data.size(); ++k) printf(" %02X", occ[i]->data[k]);
                printf("  |");
                for (size_t k = 0; k < 24 && k < occ[i]->data.size(); ++k) {
                    const char ch = static_cast<char>(occ[i]->data[k]);
                    printf("%c", (ch >= 32 && ch < 127) ? ch : '.');
                }
                printf("|\n");
            }
    }

    size_t pass = 0, passD9 = 0, blobOk = 0, blobChecked = 0, midNonzero = 0;
    std::map<long long, size_t> deltaHist;
    std::map<std::string, std::array<size_t, 3>> byStatus; // applicable, pass, blobOk
    for (size_t i = 0; i < occ.size(); ++i) {
        const Verdict& v = vs[i];
        if (!v.applicable) continue;
        auto& b = byStatus[occ[i]->how + (occ[i]->entryIndex == 0 ? " (entry 0)" : "")];
        ++b[0];
        if (v.h.middleCount() > 0) ++midNonzero;
        if (v.passSpec) {
            ++pass;
            ++b[1];
            if (v.blobTokensOk) { ++blobOk; ++b[2]; }
            blobChecked += v.blobsChecked;
        } else {
            ++deltaHist[static_cast<long long>(v.endSpec) - static_cast<long long>(occ[i]->data.size())];
        }
        if (v.passD9) ++passD9;
    }
    printf("EXACT CONSUMPTION (running offset after last blob == file length), spec rule: %zu / %zu applicable\n", pass, appl);
    printf("  (with middle-table blobs NOT stepped over, the Direct3D 9 loader's placement: %zu / %zu; files with nMid > 0: %zu)\n",
           passD9, appl, midNonzero);
    printf("  by obtain-status  applicable / consumed exactly / blob payload check ok:\n");
    for (const auto& kv : byStatus)
        printf("     %-40s %zu / %zu / %zu\n", kv.first.c_str(), kv.second[0], kv.second[1], kv.second[2]);
    printf("  blob payload check (%s) over the exactly-consuming files: %zu / %zu files ok (%zu blobs examined)\n",
           ext == ".fxo_pc" ? "D3D9 version token of the right stage + end token 0x0000FFFF at the last dword"
                            : ext == ".fxo_pc_dx11" ? "first four bytes are \"DXBC\" - NOT in the spec, spec §8.2 marks the payload identification HIGH CONFIDENCE only"
                                                    : "none defined for this extension",
           blobOk, pass, blobChecked);
    if (!deltaHist.empty()) {
        printf("  files where the formula did NOT consume exactly: %zu; (end of blobs - file size) histogram:", appl - pass);
        for (const auto& kv : deltaHist) printf("  %+lld x%zu", kv.first, kv.second);
        printf("\n");
        size_t shown = 0;
        for (size_t i = 0; i < occ.size() && shown < 12; ++i)
            if (vs[i].applicable && !vs[i].passSpec) {
                const auto& h = vs[i].h;
                printf("     FAIL %-38s size %zu H %zu firstBlob %zu end %zu | c0..c4 %d %d %d %d %d nVS %d nMid %d nPS %d c8 %u | %s idx %zu\n",
                       occ[i]->name.c_str(), occ[i]->data.size(), h.headerSize(), h.firstBlobOffset(), vs[i].endSpec,
                       h.count(0), h.count(1), h.count(2), h.count(3), h.count(4), h.vertexCount(), h.middleCount(),
                       h.pixelCount(), h.passCount(), occ[i]->how.c_str(), occ[i]->entryIndex);
                ++shown;
            }
    }

    if (ext == ".fxo_pc") prefixConsistency(occ, vs);
    if (ext == ".fxo_pc") {
        size_t shown = 0;
        for (size_t i = 0; i < occ.size() && shown < 4; ++i) {
            if (!vs[i].applicable || vs[i].passSpec) continue;
            ++shown;
            const auto& h = vs[i].h;
            const auto& d = occ[i]->data;
            printf("DETAIL %s (entry %zu, size %zu): H=%zu firstBlob=%zu flags=0x%X\n", occ[i]->name.c_str(), occ[i]->entryIndex,
                   d.size(), h.headerSize(), h.firstBlobOffset(), h.flags());
            printf("   VS lengths:");
            for (const auto& e : h.vertexTable()) printf(" %u", e.length);
            printf("   PS lengths:");
            for (const auto& e : h.pixelTable()) printf(" %u", e.length);
            printf("\n   T8:");
            for (const auto& p : h.passes()) printf(" [v%d %d p%d w6=%d h=%08X w0C=%X]", p.vertexIndex, p.word2, p.pixelIndex, p.word6, p.nameHash, p.word0C);
            printf("\n");
            size_t e = 0;
            for (const auto& b : h.layoutBlobs(e)) {
                const bool present = b.offset + b.length <= d.size();
                uint32_t tok = b.offset + 4 <= d.size() ? vpp::ByteView(d.data(), d.size()).readU32LE(b.offset) : 0;
                uint32_t last = present ? vpp::ByteView(d.data(), d.size()).readU32LE(b.offset + b.length - 4) : 0;
                printf("   blob %s#%zu at %zu len %zu %s first dword %08X last dword %08X\n",
                       b.stage == Stage::Vertex ? "VS" : b.stage == Stage::Pixel ? "PS" : "MID", b.tableIndex, b.offset,
                       b.length, present ? "present" : (b.offset < d.size() ? "PARTIAL" : "absent "), tok, last);
            }
            printf("   first 0x80 bytes of decoded data:");
            for (size_t k = 0; k < 0x80 && k < d.size(); ++k) printf("%s%02X", k % 16 == 0 ? "\n     " : " ", d[k]);
            printf("\n");
        }
    }
    {
        printf("  files consuming exactly:");
        for (size_t i = 0; i < occ.size(); ++i)
            if (vs[i].applicable && vs[i].passSpec) printf("  %s (entry %zu, %s, %zu bytes)", occ[i]->name.c_str(), occ[i]->entryIndex, occ[i]->how.c_str(), occ[i]->data.size());
        printf("\n");
    }

    // ---- controls ----
    printf("CONTROLS (files, among the applicable, on which the WRONG rule also consumes exactly | files where the wrong rule's end differs from the spec rule's):\n");
    for (size_t k = 0; k < 7; ++k) {
        size_t agree = 0, differs = 0, bothPass = 0;
        for (const Verdict& v : vs) {
            if (!v.applicable) continue;
            agree += v.ctl[k];
            differs += v.ctlDiffers[k];
            bothPass += (v.ctl[k] && v.passSpec);
        }
        printf("   %-44s passes %4zu / %zu  (also passing spec rule: %zu) | differs from spec rule on %zu files\n",
               kCtlName[k], agree, appl, bothPass, differs);
    }

    // ---- distributions ----
    std::map<int, size_t> hc[5], hvs, hmid, hps;
    std::map<uint32_t, size_t> hc8, hver, hflags;
    std::map<size_t, size_t> hH, hpad;
    size_t roleConsistent = 0, roleTotal = 0, t8InRange = 0, t8Total = 0, resNonZero = 0, resTotal = 0;
    size_t midT8 = 0, midT8InRange = 0, midT8AbsentDespiteTable = 0;
    std::array<size_t, 4> fixedNonZero{}; // 0x19-0x1F, 0x20-0x5F, 0x6B-0x77, 0x7C-0x7F
    size_t fixedFiles = 0;
    for (size_t i = 0; i < occ.size(); ++i) {
        const Verdict& v = vs[i];
        if (!v.applicable || !v.passSpec) continue;
        const WrapperHeader& h = v.h;
        for (int k = 0; k < 5; ++k) ++hc[k][h.count(static_cast<size_t>(k))];
        ++hvs[h.vertexCount()];
        ++hmid[h.middleCount()];
        ++hps[h.pixelCount()];
        ++hc8[h.passCount()];
        ++hver[static_cast<uint32_t>(h.version())];
        ++hflags[h.flags()];
        ++hH[h.headerSize()];
        ++hpad[h.firstBlobOffset() - h.headerSize()];
        for (size_t r = 0; r < 10; ++r) {
            ++roleTotal;
            const bool flag = h.rolePresent(r);
            const bool byteSet = h.roleIndexByte(r) != -1;
            roleConsistent += (flag == byteSet);
        }
        for (const auto& p : h.passes()) {
            ++t8Total;
            const bool vOk = p.vertexIndex == -1 || (p.vertexIndex >= 0 && p.vertexIndex < h.vertexCount());
            const bool pOk = p.pixelIndex == -1 || (p.pixelIndex >= 0 && p.pixelIndex < h.pixelCount());
            t8InRange += (vOk && pOk);
            if (p.word2 != -1) {
                ++midT8;
                if (p.word2 >= 0 && p.word2 < h.middleCount()) ++midT8InRange;
            } else if (h.middleCount() > 0) {
                ++midT8AbsentDespiteTable;
            }
        }
        for (const auto* tbl : {&h.vertexTable(), &h.middleTable(), &h.pixelTable()})
            for (const auto& e : *tbl) {
                ++resTotal;
                resNonZero += e.reserved != 0;
            }
        const uint8_t* d = occ[i]->data.data();
        auto anyNz = [&](size_t a, size_t b) {
            for (size_t k = a; k < b; ++k)
                if (d[k]) return true;
            return false;
        };
        ++fixedFiles;
        fixedNonZero[0] += anyNz(0x19, 0x20);
        fixedNonZero[1] += anyNz(0x20, 0x60);
        fixedNonZero[2] += anyNz(0x6B, 0x78);
        fixedNonZero[3] += anyNz(0x7C, 0x80);
    }
    printf("DISTRIBUTIONS over the %zu exactly-consuming files:\n", pass);
    printHist("version", hver);
    for (int k = 0; k < 5; ++k) {
        char b[24];
        snprintf(b, sizeof b, "c%d", k);
        printHist(b, hc[k]);
    }
    printHist("nVS", hvs);
    printHist("nMid (geometry slot)", hmid);
    printHist("nPS", hps);
    printHist("c8 (T8 pass count)", hc8);
    {
        printf("  flags dword @0x08:");
        for (const auto& kv : hflags) printf("  0x%X x%zu", kv.first, kv.second);
        printf("\n");
    }
    printHist("H (unrounded header size)", hH);
    printHist("align16(H) - H", hpad);
    printf("  role flag bit <=> role index byte != -1, roles 0..9: %zu / %zu (spec §7.4: 11/11 on the sample)\n", roleConsistent, roleTotal);
    printf("  T8 vertex/pixel indices inside their stage tables (or -1): %zu / %zu (spec §7.3: 1/1)\n", t8InRange, t8Total);
    printf("  T8 entries with word2 (+2, HYPOTHESIS: middle index) != -1: %zu of %zu; of those, in range [0, nMid): %zu; T8 entries of files WITH a middle table that still have word2 == -1: %zu\n",
           midT8, t8Total, midT8InRange, midT8AbsentDespiteTable);
    printf("  stage-table entries whose second dword (ignored by the loader) is non-zero: %zu / %zu\n", resNonZero, resTotal);
    printf("  files with a non-zero byte in the never-read/ignored fixed ranges: 0x19-0x1F %zu, 0x20-0x5F %zu, 0x6B-0x77 %zu, 0x7C-0x7F %zu (of %zu)\n",
           fixedNonZero[0], fixedNonZero[1], fixedNonZero[2], fixedNonZero[3], fixedFiles);

    // ---- T1..T4 constant hashes against the bytecode (DX9 only) ----
    if (ext == ".fxo_pc") {
        size_t entries = 0, matched = 0, files = 0, filesAll = 0, perElemEq = 0, perElemTotal = 0;
        size_t perTable[4][3] = {{0}}; // per T1..T4: [hash+register matched, hash matched but register differs, hash matches no CTAB name]
        for (size_t i = 0; i < occ.size(); ++i) {
            const Verdict& v = vs[i];
            if (!v.applicable || !v.passSpec) continue;
            const WrapperHeader& h = v.h;
            size_t e = 0;
            auto blobs = h.layoutBlobs(e);
            std::vector<sr3fxo::D3d9Constant> all;
            for (const auto& b : blobs) {
                if (b.stage == Stage::Middle) continue;
                auto info = sr3fxo::inspectD3d9Blob(vpp::ByteView(occ[i]->data.data() + b.offset, b.length));
                for (auto& c : info.constants) all.push_back(c);
            }
            size_t fe = 0, fm = 0;
            int ti = 0;
            for (const auto* tbl : {&h.constantsT1(), &h.constantsT2(), &h.constantsT3(), &h.constantsT4()}) {
                ++ti;
                for (const auto& c : *tbl) {
                    ++fe;
                    bool hit = false, nameOnly = false;
                    for (const auto& k : all)
                        if (sr3fxo::hashLowerName(k.name) == c.nameHash) {
                            if (k.registerIndex == c.registerIndex) {
                                hit = true;
                                ++perElemTotal;
                                perElemEq += (static_cast<unsigned>(c.registersPerElement) * c.elementCount == k.registerCount);
                                break;
                            }
                            nameOnly = true;
                        }
                    fm += hit;
                    ++perTable[static_cast<size_t>(ti - 1)][hit ? 0 : nameOnly ? 1 : 2];
                }
            }
            entries += fe;
            matched += fm;
            if (fe > 0) {
                ++files;
                filesAll += (fe == fm);
            }
        }
        printf("T1..T4 CONSTANT ENTRIES vs the bytecode's own CTAB (hash = CRC32-noinvert(lower(name)) AND register = CTAB register):\n");
        printf("  entries matched: %zu / %zu; files with >=1 constant entry: %zu, of which every entry matched: %zu\n", matched,
               entries, files, filesAll);
        printf("  of the matched: (+6)*(+7) == CTAB register count on %zu / %zu (spec §7.3/§9.2: 4/5 on the sample, the miss being a declared-vs-compiled 4x4 matrix)\n",
               perElemEq, perElemTotal);
        for (int t = 0; t < 4; ++t)
            printf("  T%d entries: hash+register matched %zu | hash matched a CTAB name but the register differs from every blob's %zu | hash matches NO CTAB name in any blob of the file %zu\n",
                   t + 1, perTable[t][0], perTable[t][1], perTable[t][2]);
    }
}


// ------------------------------------------------------------------------
// STREAM-LEVEL CHECK. The library-level population above inherits the
// container's directory->data mapping, which (spec-vpp-container.md §3.2) is
// only trusted for entry 0. This pass does not: for every top-level
// compressed entry whose extension starts with .fxo it lets zlib inflate at
// the entry's directory payload offset with NO target size (until the stream
// ends or errors) and judges the OUTPUT on its own content - magic, the
// header formula, exact consumption, blob tokens. Whatever name the directory
// gives it is recorded but not trusted; the content kind (Direct3D 9 tokens
// vs "DXBC") is taken from the blob bytes, not from the name.
// ------------------------------------------------------------------------
struct StreamRow {
    std::string name, ext, archive;
    size_t idx = 0, declared = 0, clen = 0;
    int ret = 0;
    size_t total = 0, consumedIn = 0;
    bool magicOk = false;
    Verdict v;
    int kind = 0; // 0 unknown/none, 1 D3D9 token stream, 2 "DXBC"
    uint64_t hash = 0;
};
std::vector<StreamRow> g_stream;
struct DeclRef { std::string ext; size_t declared, idx; };
std::vector<DeclRef> g_decl;

void streamPass(const vpp::Container& c, const std::vector<uint8_t>& bytes, const std::string& archive) {
    static std::vector<uint8_t> out(4u << 20);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const std::string ln = lower(e.name);
        const size_t dot = ln.find_last_of('.');
        if (dot == std::string::npos || ln.compare(dot, 4, ".fxo") != 0) continue;
        StreamRow r;
        r.name = e.name;
        r.ext = ln.substr(dot);
        r.archive = archive;
        r.idx = i;
        r.declared = e.payload.decompressedLength;
        r.clen = e.payload.length;
        g_decl.push_back({r.ext, r.declared, i});
        if (e.payload.kind != vpp::PayloadKind::Compressed) { r.ret = -200; g_stream.push_back(std::move(r)); continue; }
        const size_t off = e.payload.offset;
        if (off >= bytes.size()) { r.ret = -100; g_stream.push_back(std::move(r)); continue; }
        z_stream s{};
        if (inflateInit(&s) != Z_OK) { r.ret = -101; g_stream.push_back(std::move(r)); continue; }
        s.next_in = const_cast<Bytef*>(bytes.data() + off);
        s.avail_in = static_cast<uInt>(std::min<size_t>(bytes.size() - off, 8u << 20));
        s.next_out = out.data();
        s.avail_out = static_cast<uInt>(out.size());
        int ret = Z_OK;
        while (ret == Z_OK && s.avail_out > 0) ret = inflate(&s, Z_NO_FLUSH);
        r.ret = ret;
        r.total = s.total_out;
        r.consumedIn = s.total_in;
        inflateEnd(&s);
        r.hash = fnv1a(out.data(), r.total);
        r.magicOk = r.total >= 4 && vpp::ByteView(out.data(), r.total).readU32LE(0) == sr3fxo::kWrapperMagic;
        if (r.magicOk) {
            Occ tmp;
            tmp.data.assign(out.data(), out.data() + r.total);
            tmp.ext = ".fxo_pc";
            // Content kind from the first non-empty blob's first bytes.
            WrapperHeader h;
            std::string why;
            if (WrapperHeader::tryParse(vpp::ByteView(tmp.data.data(), tmp.data.size()), h, why)) {
                size_t end = 0;
                auto bl = h.layoutBlobs(end);
                if (!bl.empty() && bl[0].offset + 4 <= tmp.data.size()) {
                    const uint32_t t = vpp::ByteView(tmp.data.data(), tmp.data.size()).readU32LE(bl[0].offset);
                    if ((t & 0xFFFE0000u) == 0xFFFE0000u) r.kind = 1;
                    else if (std::memcmp(tmp.data.data() + bl[0].offset, "DXBC", 4) == 0) r.kind = 2;
                }
            }
            tmp.ext = r.kind == 2 ? ".fxo_pc_dx11" : ".fxo_pc";
            r.v = analyse(tmp);
        }
        g_stream.push_back(std::move(r));
    }
}

void streamReport() {
    printf("\n=== 5. STREAM-LEVEL CHECK, independent of the directory->data mapping ===\n");
    printf("(each top-level compressed entry's zlib stream inflated at its directory payload offset with NO target size; output judged on its own content)\n");
    std::set<std::string> exts;
    for (const auto& r : g_stream) exts.insert(r.ext);
    for (const std::string& ext : exts) {
        std::vector<const StreamRow*> rows;
        for (const auto& r : g_stream)
            if (r.ext == ext) rows.push_back(&r);
        printf("\n-- entries named *%s : %zu --\n", ext.c_str(), rows.size());
        std::map<int, size_t> retHist;
        size_t produced = 0, magic = 0, appl = 0, exact = 0, exactD9 = 0, midNz = 0, midExactOnlyWithStep = 0;
        std::map<std::string, size_t> notWhy;
        std::map<long long, size_t> delta;
        std::map<int, size_t> kindHist;
        size_t blobsOk = 0, ident = 0, other = 0, otherUnique = 0, otherAmbig = 0, otherNone = 0;
        std::map<long long, size_t> shiftHist;
        std::set<uint64_t> distinctExact;
        std::array<size_t, 7> ctl{}, ctlDiff{};
        for (const StreamRow* r : rows) {
            ++retHist[r->ret];
            if (r->total >= 0x80) ++produced;
            if (!r->magicOk) continue;
            ++magic;
            if (!r->v.applicable) { ++notWhy[r->v.why]; continue; }
            ++appl;
            ++kindHist[r->kind];
            if (r->v.h.middleCount() > 0) {
                ++midNz;
                if (r->v.passSpec && !r->v.passD9) ++midExactOnlyWithStep;
            }
            if (r->v.passD9) ++exactD9;
            if (r->v.passSpec) {
                ++exact;
                distinctExact.insert(r->hash);
                blobsOk += r->v.blobTokensOk;
                for (size_t k = 0; k < 7; ++k) { ctl[k] += r->v.ctl[k]; ctlDiff[k] += r->v.ctlDiffers[k]; }
                if (r->total == r->declared) {
                    ++ident;
                } else {
                    ++other;
                    std::vector<size_t> hits;
                    for (const DeclRef& d : g_decl)
                        if (d.ext == ext && d.declared == r->total) hits.push_back(d.idx);
                    if (hits.empty()) ++otherNone;
                    else if (hits.size() == 1) { ++otherUnique; ++shiftHist[static_cast<long long>(hits[0]) - static_cast<long long>(r->idx)]; }
                    else ++otherAmbig;
                }
            } else {
                ++delta[static_cast<long long>(r->v.endSpec) - static_cast<long long>(r->total)];
            }
        }
        printf("  inflate return codes (zlib: 1 stream end, 0 ok, -3 data error, -5 buf error, -100 offset out of file):");
        for (const auto& kv : retHist) printf("  %d x%zu", kv.first, kv.second);
        printf("\n  output >= 0x80 bytes: %zu; output begins with the wrapper magic: %zu / %zu\n", produced, magic, rows.size());
        printf("  formula APPLICABLE to those: %zu / %zu", appl, magic);
        for (const auto& kv : notWhy) printf("  [not applicable: %s x%zu]", kv.first.c_str(), kv.second);
        printf("\n  EXACT CONSUMPTION (running offset after last blob == inflated length): %zu / %zu applicable  (distinct contents: %zu)\n",
               exact, appl, distinctExact.size());
        printf("  content kind among applicable (1 = D3D9 tokens, 2 = \"DXBC\", 0 = neither/none):");
        for (const auto& kv : kindHist) printf("  %d x%zu", kv.first, kv.second);
        printf("\n  blob payload check over the exactly-consuming streams (right-stage D3D9 tokens / \"DXBC\"): %zu / %zu\n", blobsOk, exact);
        printf("  applicable streams with nMid > 0: %zu; of the exactly-consuming, those that consume ONLY when middle blobs are stepped over: %zu\n",
               midNz, midExactOnlyWithStep);
        printf("  exact consumption if middle-table blobs are NOT stepped over (Direct3D 9 loader placement): %zu / %zu\n", exactD9, appl);
        if (!delta.empty()) {
            printf("  applicable but NOT exact: %zu; (implied - inflated) bytes histogram (top 10):", appl - exact);
            std::vector<std::pair<size_t, long long>> v;
            for (const auto& kv : delta) v.push_back({kv.second, kv.first});
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < 10; ++i) printf("  %+lld x%zu", v[i].second, v[i].first);
            printf("\n");
        }
        printf("  MAPPING: exactly-consuming streams whose length equals THIS entry's declared +0x0C: %zu; equals another entry's: %zu (unique match %zu, ambiguous %zu), matches no entry's: %zu\n",
               ident, other, otherUnique, otherAmbig, otherNone);
        if (!shiftHist.empty()) {
            printf("     (index of the uniquely-matching entry) - (this entry's index), top 10:");
            std::vector<std::pair<size_t, long long>> v;
            for (const auto& kv : shiftHist) v.push_back({kv.second, kv.first});
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < 10; ++i) printf("  %+lld x%zu", v[i].second, v[i].first);
            printf("\n");
        }
        printf("  CONTROLS on the exactly-consuming streams (wrong rule also consumes exactly | wrong rule's end differs from spec rule's):\n");
        for (size_t k = 0; k < 7; ++k)
            printf("     %-44s %4zu / %zu | differs on %zu\n", kCtlName[k], ctl[k], exact, ctlDiff[k]);
    }
}

// ------------------------------------------------------------------------
// CORRECTED-OFFSET PASS. The two passes above showed that for
// shaders.vpp_pc (flags 0x4801, mode (a)) the directory's +0x08 offsets do not
// locate each entry's own zlib stream: inflating there yields a complete,
// valid file, but of ANOTHER entry (its length equals some other entry's
// +0x0C, and the header formula consumes it exactly). The spec's §3.3
// describes a loader-side running position, in 2,048-byte blocks starting at
// the payload region, that accumulates the COMPRESSED size (+0x10) when flags
// bit 0x1 is set (this archive: 0x4801). That text is taken here as a
// hypothesis about where the streams really sit:
//
//     physical(i) = payloadStart + sum over j < i of roundup(+0x10 of j, 0x800)
//
// and it is TESTED, not assumed: an entry is accepted only if unbounded
// inflate at that position yields EXACTLY the entry's own +0x0C bytes
// ("identity"). Anything that fails the identity is reported and excluded.
// The header formula is then run on content whose name/size association has
// been independently confirmed. The rule is applied only to top-level
// containers with flags bit 0x1 set and the shared-stream bit clear.
// ------------------------------------------------------------------------
std::vector<Occ> g_occ2;
size_t g_h1Tested = 0, g_h1Identity = 0, g_h1Fail = 0;
std::map<std::string, std::pair<size_t, size_t>> g_h1ByArchive; // archive -> (tested, identity)

void correctedPass(const vpp::Container& c, const std::vector<uint8_t>& bytes, const std::string& archive) {
    const auto& H = c.header();
    if ((H.flagsRaw & 0x1u) == 0 || H.isSharedStreamMode()) return;
    static std::vector<uint8_t> out;
    size_t cum = 0;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const size_t clen = e.payload.length, declared = e.payload.decompressedLength;
        const size_t phys = H.payloadStart() + cum;
        // Raw entries occupy their (uncompressed) size in the same block units.
        cum += vpp::roundUpBlock(e.payload.kind == vpp::PayloadKind::Compressed ? clen : declared);
        const std::string ln = lower(e.name);
        const size_t dot = ln.find_last_of('.');
        if (dot == std::string::npos || ln.compare(dot, 4, ".fxo") != 0) continue;
        if (e.payload.kind != vpp::PayloadKind::Compressed) continue;
        ++g_h1Tested;
        auto& per = g_h1ByArchive[archive];
        ++per.first;
        if (phys >= bytes.size()) { ++g_h1Fail; continue; }
        const size_t cap = declared + 64;
        if (out.size() < cap) out.resize(cap);
        z_stream s{};
        if (inflateInit(&s) != Z_OK) { ++g_h1Fail; continue; }
        s.next_in = const_cast<Bytef*>(bytes.data() + phys);
        s.avail_in = static_cast<uInt>(std::min<size_t>(bytes.size() - phys, 0x7FFFFFFFu));
        s.next_out = out.data();
        s.avail_out = static_cast<uInt>(cap);
        int ret = Z_OK;
        while (ret == Z_OK && s.avail_out > 0) ret = inflate(&s, Z_NO_FLUSH);
        const size_t total = s.total_out;
        inflateEnd(&s);
        if (total != declared) { ++g_h1Fail; continue; }
        ++g_h1Identity;
        ++per.second;
        Occ o;
        o.archive = archive;
        o.name = e.name;
        o.ext = ln.substr(dot);
        o.entryIndex = i;
        o.declaredSize = declared;
        o.how = "identity at corrected offset";
        o.data.assign(out.data(), out.data() + total);
        o.hash = fnv1a(o.data.data(), o.data.size());
        g_occ2.push_back(std::move(o));
    }
}
} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        const fs::path dir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
        for (const auto& de : fs::directory_iterator(dir))
            if (de.path().extension() == ".vpp_pc") archives.push_back(de.path().string());
        std::sort(archives.begin(), archives.end());
    }
    size_t scanned = 0;
    for (const std::string& a : archives) {
        std::vector<uint8_t> bytes = readFile(a);
        if (bytes.empty()) continue;
        const std::string base = fs::path(a).filename().string();
        try {
            vpp::Container c{vpp::ByteView(bytes.data(), bytes.size())};
            walk(c, base, "");
            streamPass(c, bytes, base);
            correctedPass(c, bytes, base);
            ++scanned;
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", base.c_str(), ex.what());
        }
    }
    printf("=== 0. CENSUS: every directory entry whose extension starts with .fxo (archives scanned: %zu) ===\n", scanned);
    for (const auto& kv : g_census) printf("  %-70s x%zu\n", kv.first.c_str(), kv.second);
    printf("  compressed entries not decoded (not .fxo*, not a container name): %zu; decode failures: %zu\n",
           g_notDecodedOther, g_decodeFail);
    {
        std::map<std::string, size_t> byExt;
        for (const Occ& o : g_occ) ++byExt[o.ext];
        printf("  bytes obtained per extension:");
        for (const auto& kv : byExt) printf("  %s x%zu", kv.first.c_str(), kv.second);
        printf("\n");
    }

    trustedSample();
    populationReport(".fxo_pc", "2. LIBRARY-LEVEL POPULATION: .fxo_pc as decoded by vpp::Container (directory +0x08 offsets)", g_occ);
    streamReport();

    printf("\n=== 6. CORRECTED-OFFSET POPULATION ===\n");
    printf("top-level compressed .fxo* entries tested at physical(i) = payloadStart + sum roundup(+0x10, 0x800): %zu; identity (inflated length == the entry's own +0x0C): %zu; failed identity or offset outside the file: %zu\n",
           g_h1Tested, g_h1Identity, g_h1Fail);
    for (const auto& kv : g_h1ByArchive) printf("   %-24s tested %zu identity %zu\n", kv.first.c_str(), kv.second.first, kv.second.second);
    populationReport(".fxo_pc", "6a. .fxo_pc AT CORRECTED OFFSETS (the ask)", g_occ2);
    populationReport(".fxo_pc_dx11", "6b. .fxo_pc_dx11 AT CORRECTED OFFSETS (supplementary: spec §8.2, same wrapper, D3D11 payload)", g_occ2);
    populationReport(".fxo", "6c. bare .fxo AT CORRECTED OFFSETS (supplementary)", g_occ2);
    return 0;
}
