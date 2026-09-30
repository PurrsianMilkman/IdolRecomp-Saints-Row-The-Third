// Population validation of the .cpeg_pc/.cvbm_pc + .gpeg_pc/.gvbm_pc layout
// (spec-texture-format.md Sec10-Sec12) against every shipped archive.
//
// WHAT IS MEASURED (the gates come from spec Sec12.1 / Sec12.3, restated
// here, not from the reader's source):
//   c-side, per file: sr3texture::TexturePair::parse accepts it (magic,
//     version 13, own size == file length, exact name-table consumption);
//     AND, from RAW bytes read directly by this harness: 0x10 == 0x14,
//     0x12 == 0, 0x16 == 16, platform (0x06) == 0, every record's group
//     count (+0x14) == 1, so names and records are 1:1.
//   pair-level, per c/g pair whose g side is a reliable entry: header 0x0C
//     == g entry length; first record offset == 0; offset[i+1] ==
//     align16(offset[i] + size[i]); align16(last offset + last size) ==
//     g length.
//   record-level: +0x24 == sum of the level sizes. Computed TWICE, by two
//     different formulations: (a) this harness's own block-count/pixel-count
//     reference (refChainBytes, written from the Sec10.5 prose and the
//     Sec11.1 table, deliberately not the row-bytes form the library uses),
//     and (b) the library's sr3texture::computeLevelLayout, which follows the
//     spec's literal row-bytes formulas. Both must equal the file's size
//     field, and equal each other.
//
// NEGATIVE CONTROLS (each is a deliberate break; the count that still
// passes is printed and must be far below the real result, otherwise the
// gate could not fail): level count +/-1; a wrong bytes-per-unit for each
// format; the size field read as 16 bits (the OLD reader's bug); the record
// stride shifted to 64/68/70/74/76/80; 8-byte instead of 16-byte region
// alignment; an unclamped literal halving; header 0x0C compared against a
// neighbouring pair's g length.
//
// RELIABILITY (spec Sec12.2): an entry is "at risk" (excluded, the spec's
// PARKED mode-(a) limitation) when it is Compressed, its container has
// entryCount > 1, is not in shared-stream mode, and its index > 0 - the
// exact condition under which vpp::Container reports
// DecodeStatus::OkUnconfirmedContent. At-risk c-files are tallied and parsed
// for information only and are NOT part of any headline number.
//
// Usage:
//   validate_peg_population [--verify-g N] [--out stats.txt] <archive.vpp_pc>...
//   validate_peg_population --merge stats1.txt stats2.txt ...
//   validate_peg_population --list-fmt CODE COUNT <archive.vpp_pc>...
//   validate_peg_population --px403 [...]   (also decode every 403 record's pixels and
//       score image-likeness against two wrong-layout controls)
// --verify-g N additionally decompresses every Nth reliable pair's g entry and
// checks its real length against the directory's declared length.
//
// Build (Windows; build_one.bat's object list does not include
// texture_pair.obj, so link by hand): compile src/byte_view.cpp format.cpp
// hash.cpp payload_locator.cpp container.cpp content_validation.cpp
// texture_pair.cpp with /c, then
//   cl /std:c++17 /EHsc /W4 /O2 /I include /I third_party/zlib
//      tools/validation/validate_peg_population.cpp <those .obj>
//      <build_verify/zlib/*.obj>
// One process per archive group can be run in parallel with --out, then the
// partial results combined with --merge.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "sr3texture/texture_pair.h"
#include "vpp/container.h"

namespace {

// ---------------------------------------------------------------------
// statistics store (merge-able across processes)
// ---------------------------------------------------------------------
std::map<std::string, long long> S;
std::vector<std::string> EX;
constexpr size_t kMaxExamples = 80;

void inc(const std::string& k, long long by = 1) { S[k] += by; }
void example(const std::string& s) {
    if (EX.size() < kMaxExamples) EX.push_back(s);
}

// ---------------------------------------------------------------------
// memory-mapped input (archives reach 2.6 GB)
// ---------------------------------------------------------------------
struct Mapped {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE map = nullptr;
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool open(const std::string& path) {
        file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(file, &sz) || sz.QuadPart <= 0) return false;
        size = static_cast<size_t>(sz.QuadPart);
        map = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (map == nullptr) return false;
        data = static_cast<const uint8_t*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
        return data != nullptr;
    }
    ~Mapped() {
        if (data) UnmapViewOfFile(data);
        if (map) CloseHandle(map);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
};

// ---------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------
std::string lower(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}
bool endsWithLower(const std::string& name, const char* ext) {
    std::string l = lower(name);
    size_t n = std::strlen(ext);
    return l.size() >= n && l.compare(l.size() - n, n, ext) == 0;
}
std::string stemOf(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string f = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = f.find_last_of('.');
    return dot == std::string::npos ? f : f.substr(0, dot);
}
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint64_t align(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

// The container layer's own "at risk" predicate (see container.h,
// DecodeStatus::OkUnconfirmedContent).
bool atRisk(const vpp::Container& c, size_t i) {
    const auto& e = c.entries()[i];
    return e.payload.kind == vpp::PayloadKind::Compressed && c.header().entryCount > 1 &&
           !c.header().isSharedStreamMode() && i > 0;
}

bool usable(vpp::DecodeStatus s) {
    return s == vpp::DecodeStatus::Ok || s == vpp::DecodeStatus::OkUnconfirmedContent ||
           s == vpp::DecodeStatus::ContentValidated || s == vpp::DecodeStatus::RecoveredSharedStream ||
           s == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
}

// ---------------------------------------------------------------------
// the harness's OWN level-size reference (formulation (a) in the header
// comment): count blocks / pixels rather than use the row-bytes forms.
// `blockBytesOverride` / `bppOverride` (0 = none) drive the format controls.
// `clamp == false` halves unconditionally down to 1 (control), evaluated
// with the spec's literal per-level formula.
// ---------------------------------------------------------------------
struct RefResult {
    bool ok = false;
    uint64_t bytes = 0;
};

RefResult refChainBytes(uint32_t fmt, uint32_t w, uint32_t h, uint32_t levels, bool cube,
                        uint32_t blockBytesOverride = 0, uint32_t bppOverride = 0) {
    RefResult r;
    bool dxt = false;
    uint32_t blockBytes = 0, bpp = 0;
    switch (fmt) {
        case 400: dxt = true; blockBytes = 8; break;
        case 401: case 402: case 701: dxt = true; blockBytes = 16; break;
        case 403: case 404: case 405: case 408: case 409: bpp = 2; break;
        case 406: bpp = 3; break;
        case 407: bpp = 4; break;
        case 410: bpp = 1; break;
        default: return r;
    }
    if (blockBytesOverride && dxt) blockBytes = blockBytesOverride;
    if (bppOverride && !dxt) bpp = bppOverride;
    uint32_t faces = 1;
    if (cube) {
        if (h % 6 != 0) return r;
        h /= 6;
        faces = 6;
    }
    uint64_t total = 0;
    const uint32_t minDim = dxt ? 8u : 2u; // halve only while the halved value stays >= block edge (4 / 1)
    for (uint32_t l = 0; l < levels; ++l) {
        if (dxt) total += static_cast<uint64_t>((w + 3) / 4) * ((h + 3) / 4) * blockBytes;
        else total += static_cast<uint64_t>(w) * h * bpp;
        if (w >= minDim) w /= 2;
        if (h >= minDim) h /= 2;
    }
    r.ok = true;
    r.bytes = total * faces;
    return r;
}

// Control: the spec's literal formula, halving without the clamp.
RefResult literalUnclamped(uint32_t fmt, uint32_t w, uint32_t h, uint32_t levels, bool cube) {
    RefResult r;
    uint32_t edge = 1;
    switch (fmt) {
        case 400: case 401: case 402: case 701: edge = 4; break;
        default: break;
    }
    uint32_t faces = 1;
    if (cube) {
        if (h % 6 != 0) return r;
        h /= 6;
        faces = 6;
    }
    uint64_t total = 0;
    for (uint32_t l = 0; l < levels; ++l) {
        uint64_t rb = sr3texture::sourceRowBytes(fmt, w);
        if (rb == 0) return r;
        total += static_cast<uint64_t>(h) * rb / edge;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    r.ok = true;
    r.bytes = total * faces;
    return r;
}

// ---------------------------------------------------------------------
// per-file processing
// ---------------------------------------------------------------------
struct RawRecord {
    uint32_t offset, width, height, fmt, group, flags, levels, size;
};

struct Options {
    long long verifyG = 0;
    int listFmt = 0;
    long long listCount = 0;
    long long listed = 0;
    bool px403 = false; // --px403: image-likeness statistics over every 403 (R5G6B5) record
};

// Mean absolute horizontal neighbour difference over R,G,B (0..255 scale) of a
// w x h RGBA8 image. Natural images / UI art score low; channel noise scores
// high. A statistic, not a proof - it exists so "looks like an image" is
// measured on all 709 textures instead of eyeballed on three.
double meanAbsDx(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    if (w < 2 || h == 0) return 0;
    uint64_t sum = 0;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = rgba.data() + static_cast<size_t>(y) * w * 4;
        for (uint32_t x = 0; x + 1 < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                int d = static_cast<int>(row[x * 4 + c]) - static_cast<int>(row[(x + 1) * 4 + c]);
                sum += static_cast<uint64_t>(d < 0 ? -d : d);
            }
        }
    }
    return static_cast<double>(sum) / (static_cast<double>(h) * (w - 1) * 3);
}

// Decodes `px` 16-bit words as R5G6B5 (correct, from the spec) and, as
// CONTROLS, (a) the same words with the two bytes of each swapped (wrong
// endianness) and (b) A4R4G4B4 (wrong layout). Returns the three smoothness
// scores and the count of distinct correct-decode pixel values.
void score403(const uint8_t* src, uint32_t w, uint32_t h, double& sCorrect, double& sSwapped, double& sA4,
              size_t& distinct) {
    const size_t px = static_cast<size_t>(w) * h;
    std::vector<uint8_t> img(px * 4), sw(px * 4), a4(px * 4);
    sr3texture::expandR5G6B5ToRgba8(src, px, img.data());
    std::vector<uint8_t> swappedBytes(px * 2);
    for (size_t i = 0; i < px; ++i) {
        swappedBytes[i * 2] = src[i * 2 + 1];
        swappedBytes[i * 2 + 1] = src[i * 2];
    }
    sr3texture::expandR5G6B5ToRgba8(swappedBytes.data(), px, sw.data());
    for (size_t i = 0; i < px; ++i) {
        uint32_t v = src[i * 2] | (src[i * 2 + 1] << 8);
        a4[i * 4 + 0] = static_cast<uint8_t>(((v >> 8) & 15) * 17);
        a4[i * 4 + 1] = static_cast<uint8_t>(((v >> 4) & 15) * 17);
        a4[i * 4 + 2] = static_cast<uint8_t>((v & 15) * 17);
        a4[i * 4 + 3] = 255;
    }
    sCorrect = meanAbsDx(img, w, h);
    sSwapped = meanAbsDx(sw, w, h);
    sA4 = meanAbsDx(a4, w, h);
    std::set<uint16_t> seen;
    for (size_t i = 0; i < px && seen.size() < 4; ++i) seen.insert(static_cast<uint16_t>(src[i * 2] | (src[i * 2 + 1] << 8)));
    distinct = seen.size();
}
Options g_opt;
long long g_pairSeq = 0;
std::vector<std::pair<uint32_t, uint32_t>> g_pairHeaderVsG; // (header 0x0C, g length) for the shuffled-g control, per archive

void processCFile(const std::string& archive, const std::string& path, const vpp::Container& cont,
                  size_t ci, const std::map<std::string, size_t>& gByName, bool atRiskC) {
    const vpp::Entry& ce = cont.entries()[ci];
    const std::string pre = atRiskC ? "risk." : "";

    std::vector<uint8_t> owned;
    vpp::ByteView cv;
    if (ce.payload.kind == vpp::PayloadKind::Raw) {
        cv = cont.rawEntryBytes(ci);
    } else {
        vpp::DecompressResult d = cont.decompressEntry(ci);
        if (!usable(d.status) || d.data.empty()) {
            inc(pre + "c.decode_fail");
            example("C-DECODE-FAIL " + path + " :: " + ce.name);
            return;
        }
        owned = std::move(d.data);
        cv = vpp::ByteView(owned.data(), owned.size());
    }

    // ---- parse via the library ----
    sr3texture::TexturePair t;
    try {
        t = sr3texture::TexturePair::parse(cv);
    } catch (const std::exception& ex) {
        inc(pre + "c.parse_fail");
        example("C-PARSE-FAIL " + path + " :: " + ce.name + " : " + ex.what());
        return;
    }
    inc(pre + "c.parsed");
    inc(pre + "c.by_archive." + archive);
    if (atRiskC) return; // information only

    // ---- gates read straight from RAW bytes ----
    const uint8_t* b = cv.data();
    const uint32_t count = rd16(b + 0x14);
    bool hdrOk = rd16(b + 0x10) == count && rd16(b + 0x12) == 0 && rd16(b + 0x16) == 16 && rd16(b + 0x06) == 0 &&
                 rd16(b + 0x04) == 13 && rd32(b + 0x08) == cv.size();
    inc("gate.header_fields.total");
    if (hdrOk) inc("gate.header_fields.pass");
    else example("HEADER-FIELDS " + path + " :: " + ce.name);
    inc("dist.recs_per_file." + std::to_string(count));
    if (count == 0) inc("c.zero_texture");

    std::vector<RawRecord> recs;
    bool groupsAllOne = true;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = b + 0x18 + 72 * i;
        RawRecord r;
        r.offset = rd32(p + 0x00);
        r.width = rd16(p + 0x08);
        r.height = rd16(p + 0x0A);
        r.fmt = rd32(p + 0x0C) & 0xFFFF;
        r.group = rd16(p + 0x14);
        r.flags = rd16(p + 0x16);
        r.levels = p[0x23];
        r.size = rd32(p + 0x24);
        if (r.group != 1) groupsAllOne = false;
        recs.push_back(r);
    }
    inc("gate.groups_all_one.total");
    if (groupsAllOne) inc("gate.groups_all_one.pass");

    // library vs raw agreement on what was read (catches a mis-offset in the reader)
    bool libAgrees = t.records().size() == recs.size();
    for (size_t i = 0; libAgrees && i < recs.size(); ++i) {
        const auto& lr = t.records()[i];
        libAgrees = lr.gpegOffset == recs[i].offset && lr.width == recs[i].width && lr.height == recs[i].height &&
                    lr.pixelFormat == recs[i].fmt && lr.groupCount == recs[i].group && lr.flags == recs[i].flags &&
                    lr.levelCount == recs[i].levels && lr.compressedSize == recs[i].size;
    }
    inc("gate.lib_matches_raw_fields.total");
    if (libAgrees) inc("gate.lib_matches_raw_fields.pass");
    else example("LIB-RAW-MISMATCH " + path + " :: " + ce.name);

    // ---- pair lookup ----
    std::string gname = lower(ce.name);
    size_t dot = gname.find_last_of('.');
    bool haveG = false;
    size_t gi = 0;
    if (dot != std::string::npos) {
        gname[dot + 1] = 'g';
        auto it = gByName.find(gname);
        if (it != gByName.end()) {
            haveG = true;
            gi = it->second;
        }
    }
    bool gReliable = haveG && !atRisk(cont, gi);
    uint64_t gLen = 0;
    if (haveG) {
        const auto& ge = cont.entries()[gi];
        gLen = ge.payload.kind == vpp::PayloadKind::Raw ? ge.payload.length : ge.payload.decompressedLength;
    }
    inc(haveG ? (gReliable ? "pair.g_reliable" : "pair.g_at_risk") : "pair.no_g_entry");
    if (!haveG && count == 0) inc("pair.no_g_entry_zero_texture");
    if (!haveG && count != 0) {
        inc("pair.no_g_entry_nonempty");
        example("NONEMPTY-C-NO-G " + path + " :: " + ce.name);
    }

    // ---- lazily fetched g bytes, only for --px403 ----
    std::vector<uint8_t> gOwned;
    vpp::ByteView gView;
    bool gFetched = false, gFetchOk = false;

    // ---- record-level size gate (needs no g) ----
    for (size_t i = 0; i < recs.size(); ++i) {
        const RawRecord& r = recs[i];
        const bool cube = (r.flags & sr3texture::kFlagCubeMap) != 0;
        inc("records.total");
        if (gReliable) inc("records.paired");
        inc("dist.fmt." + std::to_string(r.fmt));
        inc("dist.levels." + std::to_string(r.levels));
        {
            char fb[16];
            std::snprintf(fb, sizeof fb, "0x%03X", r.flags);
            inc(std::string("dist.flags.") + fb);
        }
        if (cube) inc("dist.cube_records");
        if (r.size > 65535) inc("records.size_over_65535");
        if (r.offset == sr3texture::kNoDataOffset) inc("records.no_data_sentinel");

        if (g_opt.listFmt != 0 && static_cast<int>(r.fmt) == g_opt.listFmt && g_opt.listed < g_opt.listCount) {
            ++g_opt.listed;
            std::printf("LIST fmt=%u  %ux%u levels=%u size=%u flags=0x%X  index=%zu  name=%s\n    c-file: %s :: %s\n",
                        r.fmt, r.width, r.height, r.levels, r.size, r.flags, i, t.records()[i].name.c_str(),
                        path.c_str(), ce.name.c_str());
        }

        // --px403: image-likeness of the decoded pixels
        if (g_opt.px403 && gReliable && r.fmt == 403 && r.size == static_cast<uint64_t>(r.width) * r.height * 2) {
            if (!gFetched) {
                gFetched = true;
                const auto& ge = cont.entries()[gi];
                if (ge.payload.kind == vpp::PayloadKind::Raw) {
                    gView = cont.rawEntryBytes(gi);
                    gFetchOk = true;
                } else {
                    vpp::DecompressResult d = cont.decompressEntry(gi);
                    if (usable(d.status) && !d.data.empty()) {
                        gOwned = std::move(d.data);
                        gView = vpp::ByteView(gOwned.data(), gOwned.size());
                        gFetchOk = true;
                    }
                }
            }
            if (!gFetchOk || static_cast<uint64_t>(r.offset) + r.size > gView.size()) {
                inc("px403.g_unavailable");
            } else {
                double sc, ss, sa;
                size_t distinct;
                score403(gView.data() + r.offset, r.width, r.height, sc, ss, sa, distinct);
                inc("px403.records");
                if (distinct <= 1) {
                    inc("px403.flat_single_colour");
                } else {
                    inc("px403.nonflat");
                    if (sc < ss) inc("px403.correct_smoother_than_byteswapped");
                    if (sc < sa) inc("px403.correct_smoother_than_a4r4g4b4");
                    if (sc < ss && sc < sa) inc("px403.correct_smoother_than_both");
                    inc("px403.sum_correct_x1000", static_cast<long long>(sc * 1000));
                    inc("px403.sum_swapped_x1000", static_cast<long long>(ss * 1000));
                    inc("px403.sum_a4_x1000", static_cast<long long>(sa * 1000));
                    if (sc >= ss || sc >= sa) {
                        example("PX403-NOT-SMOOTHEST " + path + " :: " + ce.name + " rec " + std::to_string(i) + " " +
                                std::to_string(r.width) + "x" + std::to_string(r.height) + " correct=" + std::to_string(sc) +
                                " swapped=" + std::to_string(ss) + " a4=" + std::to_string(sa));
                    }
                }
            }
        }

        // (a) harness reference
        RefResult ref = refChainBytes(r.fmt, r.width, r.height, r.levels, cube);
        // (b) library
        sr3texture::LevelLayout ll;
        std::string why;
        bool libOk = sr3texture::computeLevelLayout(r.fmt, r.width, r.height, r.levels, cube, ll, why);

        const bool refPass = ref.ok && ref.bytes == r.size;
        const bool libPass = libOk && ll.totalBytes == r.size;
        inc("gate.size_ref.total");
        if (refPass) inc("gate.size_ref.pass");
        inc("gate.size_lib.total");
        if (libPass) inc("gate.size_lib.pass");
        inc("gate.ref_equals_lib.total");
        if (ref.ok == libOk && (!libOk || ref.bytes == ll.totalBytes)) inc("gate.ref_equals_lib.pass");
        if (!refPass || !libPass) {
            example("SIZE-GATE " + path + " :: " + ce.name + " rec " + std::to_string(i) + " fmt=" +
                    std::to_string(r.fmt) + " " + std::to_string(r.width) + "x" + std::to_string(r.height) + " lv=" +
                    std::to_string(r.levels) + " size=" + std::to_string(r.size) + " ref=" +
                    (ref.ok ? std::to_string(ref.bytes) : std::string("n/a")) + " lib=" +
                    (libOk ? std::to_string(ll.totalBytes) : why));
        }
        if (gReliable) {
            inc("gate.size_paired.total");
            if (refPass && libPass) inc("gate.size_paired.pass");
        }

        // ---- controls, evaluated on every record ----
        inc("ctl.records");
        auto passes = [&](const RefResult& x) { return x.ok && x.bytes == r.size; };
        if (passes(refChainBytes(r.fmt, r.width, r.height, r.levels + 1, cube))) inc("ctl.levels_plus1.pass");
        if (r.levels >= 1 && passes(refChainBytes(r.fmt, r.width, r.height, r.levels - 1, cube)))
            inc("ctl.levels_minus1.pass");
        // wrong bytes-per-unit
        {
            uint32_t wrongBlock = 0, wrongBpp = 0;
            const char* tag = nullptr;
            switch (r.fmt) {
                case 400: wrongBlock = 16; tag = "fmt400_as_16Bblock"; break;
                case 401: wrongBlock = 8; tag = "fmt401_as_8Bblock"; break;
                case 402: wrongBlock = 8; tag = "fmt402_as_8Bblock"; break;
                case 403: wrongBpp = 4; tag = "fmt403_as_4Bpp"; break;
                case 407: wrongBpp = 2; tag = "fmt407_as_2Bpp"; break;
                case 410: wrongBpp = 2; tag = "fmt410_as_2Bpp"; break;
                default: break;
            }
            if (tag) {
                inc(std::string("ctl.") + tag + ".total");
                if (passes(refChainBytes(r.fmt, r.width, r.height, r.levels, cube, wrongBlock, wrongBpp)))
                    inc(std::string("ctl.") + tag + ".pass");
            }
        }
        // 16-bit size field (the old reader)
        inc("ctl.size_u16.total");
        if (ref.ok && (ref.bytes & 0xFFFF) == (r.size & 0xFFFF) && (r.size & 0xFFFF) == r.size)
            inc("ctl.size_u16.pass_equal_to_real_size");
        if (ref.ok && ref.bytes == (r.size & 0xFFFF)) inc("ctl.size_u16.pass");
        // unclamped literal halving
        {
            RefResult u = literalUnclamped(r.fmt, r.width, r.height, r.levels, cube);
            const bool dxtF = r.fmt == 400 || r.fmt == 401 || r.fmt == 402 || r.fmt == 701;
            if (dxtF) {
                inc("ctl.unclamped_literal.dxt_total");
                if (u.ok && u.bytes == r.size) inc("ctl.unclamped_literal.dxt_pass");
                if (!(u.ok && u.bytes == r.size)) inc("ctl.unclamped_literal.dxt_records_where_clamp_matters");
            }
        }
    }

    // ---- offset-chain gates (raw offsets/sizes) ----
    if (count > 0) {
        bool first0 = recs[0].offset == 0;
        bool chain16 = true, chain8 = true, chain1 = true;
        for (size_t i = 0; i + 1 < recs.size(); ++i) {
            uint64_t end = static_cast<uint64_t>(recs[i].offset) + recs[i].size;
            if (recs[i + 1].offset != align(end, 16)) chain16 = false;
            if (recs[i + 1].offset != align(end, 8)) chain8 = false;
            if (recs[i + 1].offset != end) chain1 = false;
        }
        const uint64_t lastEnd = static_cast<uint64_t>(recs.back().offset) + recs.back().size;
        if (gReliable) {
            inc("gate.first_offset_zero.total");
            if (first0) inc("gate.first_offset_zero.pass");
            inc("gate.offset_chain_align16.total");
            if (chain16) inc("gate.offset_chain_align16.pass");
            else example("CHAIN16 " + path + " :: " + ce.name);
            if (chain8) inc("ctl.offset_chain_align8.pass");
            if (chain1) inc("ctl.offset_chain_noalign.pass");
            if (recs.size() > 1) {
                inc("gate.offset_chain_align16.multi_record_files");
                if (chain16) inc("gate.offset_chain_align16.multi_record_pass");
                if (chain8) inc("ctl.offset_chain_align8.multi_record_pass");
                if (chain1) inc("ctl.offset_chain_noalign.multi_record_pass");
            }
            inc("gate.tail_align16_equals_glen.total");
            if (align(lastEnd, 16) == gLen) inc("gate.tail_align16_equals_glen.pass");
            else example("TAIL16 " + path + " :: " + ce.name + " lastEnd=" + std::to_string(lastEnd) + " glen=" + std::to_string(gLen));
            if (align(lastEnd, 8) == gLen) inc("ctl.tail_align8.pass");
            if (lastEnd == gLen) inc("ctl.tail_unaligned.pass");
            if (recs.size() == 1) {
                // For single-record files the last-end == first-size; unaligned holds only when size is a multiple of 16
            }
        }
    }

    // ---- header 0x0C vs g length ----
    if (gReliable) {
        inc("pair.total");
        if (count == 0) inc("pair.zero_texture_pairs");
        const uint32_t hdrG = rd32(b + 0x0C);
        inc("gate.hdr_glen_equals_g_entry.total");
        if (hdrG == gLen) inc("gate.hdr_glen_equals_g_entry.pass");
        else example("HDR-GLEN " + path + " :: " + ce.name + " hdr=" + std::to_string(hdrG) + " g=" + std::to_string(gLen));
        // shuffled control: compare against the previous pair's g length
        g_pairHeaderVsG.emplace_back(hdrG, static_cast<uint32_t>(gLen));

        // stride control on multi-record files
        if (count >= 2) {
            inc("ctl.stride.files");
            static const uint32_t strides[] = {64, 68, 70, 72, 74, 76, 80};
            for (uint32_t st : strides) {
                bool allOk = true;
                for (uint32_t i = 0; i < count && allOk; ++i) {
                    size_t base = 0x18 + static_cast<size_t>(st) * i;
                    if (base + 0x28 > cv.size()) { allOk = false; break; }
                    const uint8_t* p = b + base;
                    uint32_t w = rd16(p + 0x08), h = rd16(p + 0x0A), f = rd32(p + 0x0C) & 0xFFFF, lv = p[0x23],
                             sz = rd32(p + 0x24), fl = rd16(p + 0x16);
                    RefResult x = refChainBytes(f, w, h, lv, (fl & 0x8) != 0);
                    if (!(x.ok && x.bytes == sz)) allOk = false;
                }
                if (allOk) inc("ctl.stride." + std::to_string(st) + ".files_pass");
            }
        }

        // optional real decompression of the g entry
        if (g_opt.verifyG > 0 && (++g_pairSeq % g_opt.verifyG) == 0) {
            const auto& ge = cont.entries()[gi];
            if (ge.payload.kind == vpp::PayloadKind::Raw) {
                inc("verify_g.raw_skipped");
            } else {
                vpp::DecompressResult d = cont.decompressEntry(gi);
                inc("verify_g.tested");
                if (usable(d.status) && d.data.size() == gLen) inc("verify_g.length_matches_directory");
                else example("VERIFY-G " + path + " :: " + ce.name + " status/len mismatch, got " + std::to_string(d.data.size()) +
                             " want " + std::to_string(gLen));
                if (usable(d.status) && d.data.size() == hdrG) inc("verify_g.length_matches_header");
            }
        }
    }
}

void walk(const vpp::Container& cont, const std::string& archive, const std::string& path, int depth) {
    if (depth > 8) return;
    std::map<std::string, size_t> gByName;
    for (size_t i = 0; i < cont.entries().size(); ++i) {
        const std::string& n = cont.entries()[i].name;
        if (endsWithLower(n, ".gpeg_pc") || endsWithLower(n, ".gvbm_pc")) {
            std::string k = lower(n);
            if (gByName.count(k)) inc("dup.g_names");
            else gByName[k] = i;
        }
    }

    static const char* kExts[] = {".cvbm_pc", ".gvbm_pc", ".cpeg_pc", ".gpeg_pc", ".cvbl_pc", ".gvbl_pc", ".cvbh_pc", ".gvbh_pc"};
    for (size_t i = 0; i < cont.entries().size(); ++i) {
        const auto& e = cont.entries()[i];
        inc("entries.visited");
        for (const char* x : kExts) {
            if (endsWithLower(e.name, x)) {
                const std::string ext = x + 1;
                inc("ext." + ext);
                const bool risk = atRisk(cont, i);
                const char* kind = e.payload.kind == vpp::PayloadKind::Raw ? "raw" : (risk ? "atrisk" : "shared_or_first");
                inc("class." + ext + "." + kind);
                break;
            }
        }
    }

    for (size_t i = 0; i < cont.entries().size(); ++i) {
        const auto& e = cont.entries()[i];
        const bool isC = endsWithLower(e.name, ".cpeg_pc") || endsWithLower(e.name, ".cvbm_pc");
        if (isC) {
            try {
                processCFile(archive, path, cont, i, gByName, atRisk(cont, i));
            } catch (const std::exception& ex) {
                inc("c.exception");
                example("C-EXCEPTION " + path + " :: " + e.name + " : " + ex.what());
            }
        }
    }

    // recurse
    for (size_t i = 0; i < cont.entries().size(); ++i) {
        const auto& e = cont.entries()[i];
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = cont.openNested(i);
                inc("nested.raw_opened");
                walk(nested, archive, path + " > " + e.name, depth + 1);
            } catch (const std::exception&) {
            }
        } else if (endsWithLower(e.name, ".str2_pc") || endsWithLower(e.name, ".vpp_pc")) {
            inc("nested.compressed_container_entries");
            vpp::DecompressResult d = cont.decompressEntry(i);
            if (usable(d.status) && !d.data.empty()) {
                try {
                    vpp::Container nested{vpp::ByteView(d.data.data(), d.data.size())};
                    inc("nested.compressed_opened");
                    walk(nested, archive, path + " > " + e.name + " (z)", depth + 1);
                } catch (const std::exception&) {
                    inc("nested.compressed_not_a_container");
                }
            } else {
                inc("nested.compressed_undecodable");
                example("NESTED-UNDECODABLE " + path + " :: " + e.name);
            }
        }
    }
}

// ---------------------------------------------------------------------
// reporting
// ---------------------------------------------------------------------
long long get(const char* k) {
    auto it = S.find(k);
    return it == S.end() ? 0 : it->second;
}
long long get(const std::string& k) { return get(k.c_str()); }

void gateLine(const char* label, const char* key) {
    long long t = get(std::string("gate.") + key + ".total");
    long long p = get(std::string("gate.") + key + ".pass");
    std::printf("  %-52s %lld / %lld%s\n", label, p, t, p == t ? "" : "   <-- NOT ALL");
}

void printPrefixed(const char* title, const char* prefix, size_t cut) {
    std::printf("%s\n", title);
    for (const auto& kv : S) {
        if (kv.first.compare(0, std::strlen(prefix), prefix) == 0) {
            std::printf("  %-28s %lld\n", kv.first.c_str() + cut, kv.second);
        }
    }
}

void report() {
    std::printf("\n================ PEG POPULATION REPORT ================\n");
    std::printf("entries visited (all containers, recursed)      : %lld\n", get("entries.visited"));
    std::printf("nested: raw containers opened %lld ; compressed container entries %lld (opened %lld, not-a-container %lld, undecodable %lld)\n",
                get("nested.raw_opened"), get("nested.compressed_container_entries"), get("nested.compressed_opened"),
                get("nested.compressed_not_a_container"), get("nested.compressed_undecodable"));
    std::printf("\n-- extension census (spec Sec12.2: cvbm 67,710 gvbm 67,710 cpeg 2,890 gpeg 2,838; no cvbl/gvbl/cvbh/gvbh) --\n");
    for (const char* e : {"cvbm_pc", "gvbm_pc", "cpeg_pc", "gpeg_pc", "cvbl_pc", "gvbl_pc", "cvbh_pc", "gvbh_pc"}) {
        std::printf("  .%-8s total %6lld   raw %6lld   compressed-reliable %6lld   at-risk(mode-a, index>0) %6lld\n", e,
                    get(std::string("ext.") + e), get(std::string("class.") + e + ".raw"),
                    get(std::string("class.") + e + ".shared_or_first"), get(std::string("class.") + e + ".atrisk"));
    }
    std::printf("\n-- c-side files (RELIABLE entries only; spec expects 70,524 = cvbm 67,665 + cpeg 2,859) --\n");
    std::printf("  parsed OK by the library : %lld\n", get("c.parsed"));
    std::printf("  parse FAILED             : %lld\n", get("c.parse_fail"));
    std::printf("  decode failed / exception: %lld / %lld\n", get("c.decode_fail"), get("c.exception"));
    std::printf("  zero-texture (24-byte) files : %lld   (spec: 55)\n", get("c.zero_texture"));
    printPrefixed("  parsed per archive (spec: city_1 36,851 city_0 21,224 cutscenes 4,720 dlc2 1,715 ...):", "c.by_archive.", 13);
    std::printf("\n-- at-risk c-files (mode-(a), index>0; EXCLUDED, informational; spec: 76 = cvbm 45 + cpeg 31) --\n");
    std::printf("  parse OK %lld ; parse FAILED %lld ; decode failed %lld\n", get("risk.c.parsed"), get("risk.c.parse_fail"), get("risk.c.decode_fail"));
    std::printf("\n-- pairs --\n");
    std::printf("  c-files with a reliable g entry (pairs) : %lld   (spec: 70,471)\n", get("pair.total"));
    std::printf("    of which zero-texture pairs            : %lld   (spec: 3)\n", get("pair.zero_texture_pairs"));
    std::printf("  c-files whose g partner is at-risk       : %lld   (spec: 1, startup.cpeg_pc)\n", get("pair.g_at_risk"));
    std::printf("  c-files with no g entry at all           : %lld   (of which zero-texture %lld; spec: 52, all zero-texture)\n",
                get("pair.no_g_entry"), get("pair.no_g_entry_zero_texture"));
    std::printf("  non-empty c-file with no g entry         : %lld\n", get("pair.no_g_entry_nonempty"));
    std::printf("  duplicate g names within a container     : %lld\n", get("dup.g_names"));
    std::printf("\n-- records --\n");
    std::printf("  records in reliable c-files : %lld   (spec: 76,651)\n", get("records.total"));
    std::printf("  records in reliable pairs   : %lld   (spec: 76,650)\n", get("records.paired"));
    std::printf("  records with size > 65,535  : %lld   (spec: 23,749)\n", get("records.size_over_65535"));
    std::printf("  records using the 0xFFFFFFFF no-data offset : %lld (spec: 0)\n", get("records.no_data_sentinel"));
    std::printf("\n-- GATES (pass / total) --\n");
    gateLine("header: 0x10==0x14, 0x12==0, 0x16==16, plat 0, ver 13, own size", "header_fields");
    gateLine("every record group count (+0x14) == 1 (names 1:1)", "groups_all_one");
    gateLine("library fields == raw-byte fields (all records, all files)", "lib_matches_raw_fields");
    gateLine("size == sum of levels, harness reference (records)", "size_ref");
    gateLine("size == sum of levels, library layout (records)", "size_lib");
    gateLine("harness reference == library layout (records)", "ref_equals_lib");
    gateLine("size == sum of levels, both, records in PAIRS", "size_paired");
    gateLine("header 0x0C == g entry length (pairs)", "hdr_glen_equals_g_entry");
    gateLine("first record offset == 0 (non-empty pairs)", "first_offset_zero");
    gateLine("offset[i+1] == align16(offset[i]+size[i]) (non-empty pairs)", "offset_chain_align16");
    gateLine("align16(last offset + last size) == g length (non-empty pairs)", "tail_align16_equals_glen");
    std::printf("\n-- CONTROLS: how many still pass when the rule is deliberately broken (must be << the gate above) --\n");
    long long n = get("ctl.records");
    std::printf("  level count +1                        : %lld / %lld\n", get("ctl.levels_plus1.pass"), n);
    std::printf("  level count -1                        : %lld / %lld\n", get("ctl.levels_minus1.pass"), n);
    for (const char* tag : {"fmt400_as_16Bblock", "fmt401_as_8Bblock", "fmt402_as_8Bblock", "fmt403_as_4Bpp", "fmt407_as_2Bpp", "fmt410_as_2Bpp"}) {
        std::printf("  %-38s: %lld / %lld\n", tag, get(std::string("ctl.") + tag + ".pass"), get(std::string("ctl.") + tag + ".total"));
    }
    std::printf("  size field read as 16 bits (OLD reader): %lld / %lld pass  (records whose real size fits in 16 bits: %lld)\n",
                get("ctl.size_u16.pass"), get("ctl.size_u16.total"), get("ctl.size_u16.total") - get("records.size_over_65535"));
    std::printf("  DXT records: unclamped-literal halving  : %lld / %lld pass  (=> clamp changes the result on %lld DXT records)\n",
                get("ctl.unclamped_literal.dxt_pass"), get("ctl.unclamped_literal.dxt_total"),
                get("ctl.unclamped_literal.dxt_records_where_clamp_matters"));
    std::printf("  offset chain with align 8 (all / multi-record files): %lld / %lld ; %lld / %lld\n", get("ctl.offset_chain_align8.pass"),
                get("gate.offset_chain_align16.total"), get("ctl.offset_chain_align8.multi_record_pass"), get("gate.offset_chain_align16.multi_record_files"));
    std::printf("  offset chain with NO alignment (all / multi-record) : %lld / %lld ; %lld / %lld\n", get("ctl.offset_chain_noalign.pass"),
                get("gate.offset_chain_align16.total"), get("ctl.offset_chain_noalign.multi_record_pass"), get("gate.offset_chain_align16.multi_record_files"));
    std::printf("  offset chain with align 16 (multi-record files, the real gate) : %lld / %lld\n", get("gate.offset_chain_align16.multi_record_pass"),
                get("gate.offset_chain_align16.multi_record_files"));
    std::printf("  tail: align8(last end) == g length    : %lld / %lld ; unaligned last end == g length: %lld / %lld\n", get("ctl.tail_align8.pass"),
                get("gate.tail_align16_equals_glen.total"), get("ctl.tail_unaligned.pass"), get("gate.tail_align16_equals_glen.total"));
    long long sf = get("ctl.stride.files");
    std::printf("  record stride on multi-record pairs (files where EVERY record passes format+size gate), of %lld files:\n", sf);
    for (int st : {64, 68, 70, 72, 74, 76, 80}) {
        std::printf("      stride %2d : %lld / %lld\n", st, get("ctl.stride." + std::to_string(st) + ".files_pass"), sf);
    }
    if (get("shuffle.total") > 0) {
        std::printf("  header 0x0C vs the NEXT pair's g length (shuffled) : %lld / %lld pass (real: %lld / %lld)\n", get("shuffle.pass"),
                    get("shuffle.total"), get("gate.hdr_glen_equals_g_entry.pass"), get("gate.hdr_glen_equals_g_entry.total"));
    }
    if (get("verify_g.tested") > 0) {
        std::printf("\n-- --verify-g: real decompression of sampled g entries --\n");
        std::printf("  tested %lld ; decompressed length == directory length %lld ; == header 0x0C %lld ; raw entries skipped %lld\n",
                    get("verify_g.tested"), get("verify_g.length_matches_directory"), get("verify_g.length_matches_header"),
                    get("verify_g.raw_skipped"));
    }
    if (get("px403.records") + get("px403.g_unavailable") > 0) {
        long long nf = get("px403.nonflat");
        std::printf("\n-- --px403: image-likeness of every 403 (R5G6B5) record (mean |horizontal neighbour delta|, 0..255) --\n");
        std::printf("  403 records with usable g data: %lld (g unavailable %lld) ; single-colour images: %lld ; non-flat: %lld\n",
                    get("px403.records"), get("px403.g_unavailable"), get("px403.flat_single_colour"), nf);
        std::printf("  correct R5G6B5 smoother than byte-swapped control : %lld / %lld\n", get("px403.correct_smoother_than_byteswapped"), nf);
        std::printf("  correct R5G6B5 smoother than A4R4G4B4 control     : %lld / %lld\n", get("px403.correct_smoother_than_a4r4g4b4"), nf);
        std::printf("  correct smoother than BOTH controls              : %lld / %lld\n", get("px403.correct_smoother_than_both"), nf);
        if (nf > 0) {
            std::printf("  mean score: correct %.2f ; byte-swapped %.2f ; A4R4G4B4 %.2f  (lower = smoother; both controls are the same bytes read wrongly)\n",
                        get("px403.sum_correct_x1000") / 1000.0 / nf, get("px403.sum_swapped_x1000") / 1000.0 / nf,
                        get("px403.sum_a4_x1000") / 1000.0 / nf);
        }
    }
    std::printf("\n-- distributions (reliable c-files) --\n");
    printPrefixed("format codes:", "dist.fmt.", 9);
    printPrefixed("level counts (+0x23):", "dist.levels.", 12);
    printPrefixed("flags (+0x16):", "dist.flags.", 11);
    std::printf("cube-flagged records: %lld (spec: 465)\n", get("dist.cube_records"));
    {
        std::printf("records per file:");
        for (const auto& kv : S) {
            if (kv.first.compare(0, 19, "dist.recs_per_file.") == 0) std::printf(" %s x%lld;", kv.first.c_str() + 19, kv.second);
        }
        std::printf("\n");
    }
    std::printf("\n-- first %zu exception/failure examples --\n", EX.size());
    for (const auto& s : EX) std::printf("  %s\n", s.c_str());
}

void writeStats(const std::string& path) {
    std::ofstream f(path);
    for (const auto& kv : S) f << "K\t" << kv.first << "\t" << kv.second << "\n";
    for (const auto& e : EX) f << "X\t" << e << "\n";
}

void mergeStats(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() > 2 && line[0] == 'K') {
            size_t t2 = line.find('\t', 2);
            if (t2 == std::string::npos) continue;
            S[line.substr(2, t2 - 2)] += std::stoll(line.substr(t2 + 1));
        } else if (line.size() > 2 && line[0] == 'X') {
            example(line.substr(2));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string outPath;
    std::vector<std::string> archives;
    bool merge = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--verify-g" && i + 1 < argc) g_opt.verifyG = std::atoll(argv[++i]);
        else if (a == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--merge") merge = true;
        else if (a == "--px403") g_opt.px403 = true;
        else if (a == "--list-fmt" && i + 2 < argc) {
            g_opt.listFmt = std::atoi(argv[++i]);
            g_opt.listCount = std::atoll(argv[++i]);
        } else archives.push_back(a);
    }
    if (merge) {
        for (const auto& p : archives) mergeStats(p);
        report();
        return 0;
    }
    for (const auto& path : archives) {
        Mapped m;
        if (!m.open(path)) {
            std::fprintf(stderr, "cannot map %s\n", path.c_str());
            continue;
        }
        try {
            g_pairHeaderVsG.clear();
            vpp::Container c(vpp::ByteView(m.data, m.size));
            walk(c, stemOf(path), path, 0);
            // shuffled-g control over this archive's pairs: header 0x0C against the NEXT pair's g length
            for (size_t i = 0; i + 1 < g_pairHeaderVsG.size(); ++i) {
                inc("shuffle.total");
                if (g_pairHeaderVsG[i].first == g_pairHeaderVsG[i + 1].second) inc("shuffle.pass");
            }
            std::fprintf(stderr, "scanned %s (c parsed so far %lld)\n", path.c_str(), get("c.parsed"));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "FAILED to open %s: %s\n", path.c_str(), ex.what());
            inc("archive.open_failed");
        }
    }
    if (!outPath.empty()) writeStats(outPath);
    if (g_opt.listFmt == 0) report();
    return 0;
}
