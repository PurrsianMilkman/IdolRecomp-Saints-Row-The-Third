// Population census of every .cefct_pc file in the game archives, built for
// Team A's effects work (spec-effects-format.md §5.6, §6.2 item 6, §6.12
// item 4): the spec's three real samples all have count 0 at root+0x40,
// root+0x70 and root+0x90, so nothing about those three arrays' elements has
// ever been seen. This harness finds out how many real files have a
// non-zero count at each of them and names examples.
//
// Usage:  validate_cefct_population [--tsv out.tsv] [archive.vpp_pc ...]
// With no archive arguments it scans every *.vpp_pc in the game's
// packfiles/pc/cache directory.
//
// DENOMINATORS (all printed):
//   D0  directory entries whose name ends in .cefct_pc, reached by walking
//       every archive (raw entries that validate as containers are opened;
//       compressed entries are decoded only when they are a .cefct_pc or a
//       container-named .str2_pc/.vpp_pc - every other compressed entry is
//       counted as "not decoded", never silently dropped)
//   D1  of D0, entries whose bytes were obtained (container status usable)
//   D2  of D1, entries that parse (material magic, 71BW at the predicted
//       root, version 42..44)
//   E   of D2, entries passing the spec's EXACT-CONSUMPTION identity (§5.3):
//       the u32 at root+0x38 equals (file size - root). The two numbers come
//       from independent sources (the file's own header vs the container's
//       declared size), so this is the check that would expose a mode-(a)/(b)
//       mis-recovery. MEASURED: it holds for 1,468 of 1,812 real occurrences
//       and FAILS on 344, every one of which has a non-zero count at
//       root+0x40 - see the END FIELD section of the output.
//   R   of D2, entries in E, or whose bytes after the declared end are
//       exactly a run of NUL-terminated strings (2-byte aligned) each equal,
//       ignoring case, to one of the material block's texture names. This
//       relaxation is DERIVED FROM THE REAL BYTES, not from the spec; it is
//       reported next to E, never in place of it.
//   B   of R, entries whose six count/pointer arrays all fit in the file, whose
//       records/P/Q layout passes (spec §6.11 structure bullet, when there
//       are records) and whose known extents do not overlap.
//   The primary tables use B; the E and D2 tables are repeated so the reader
//   can see how much the conclusion depends on the gate.
//
// CONTROLS (each perturbs a copy of the file and counts how many still PASS
// the checker; a discriminating check gives 0):
//   C1  copy truncated by 1 byte            -> strict end identity (set E)
//   C2  file re-sliced 16 bytes late        -> parse (marker/magic)
//   C3  end field compared with root+16     -> strict end identity (set E)
//   C4a sub-object count + 1                -> record/P/Q layout check
//   C4b last record's P pointer + 16        -> layout check
//   C4c a P pointer slot made misaligned    -> layout check
//   C4d a P tail byte set non-zero          -> layout check
//   C5  strides of the three populated arrays perturbed -> extent-overlap check
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "sr3effects/effects.h"
#include "vpp/container.h"

namespace fs = std::filesystem;

namespace {

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

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

uint64_t fnv1a(const uint8_t* d, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= d[i];
        h *= 1099511628211ull;
    }
    return h;
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
           s == vpp::DecodeStatus::ContentValidated ||
           s == vpp::DecodeStatus::RecoveredSharedStream ||
           s == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
}

struct Extent {
    const char* what;
    uint64_t start, end;
};

// One .cefct_pc occurrence.
struct Occ {
    std::string archive;  // top-level archive file name
    std::string path;     // nested container chain (excluding the archive)
    std::string name;     // entry name
    std::string how;      // "raw" or a DecodeStatus name
    size_t size = 0;
    uint64_t hash = 0;
    bool parsed = false;
    std::string parseFail;
    bool endOk = false;
    bool arraysFit = false;
    bool layoutApplicable = false;
    bool layoutOk = false;
    size_t root = 0;
    uint32_t version = 0;
    int32_t flagCount = 0;
    uint32_t endField = 0;
    uint32_t c28 = 0, c40 = 0, c60 = 0, c70 = 0, c80 = 0, c90 = 0;
    std::vector<uint32_t> filterClasses;
    std::vector<uint32_t> volumeClasses;
    std::vector<uint16_t> emitterTypes;
    bool namesParsed = false;
    long nameSlack = 0; // nameTableLength - bytes the string walk consumed
    bool sourceNameMatches = false; // string at root+pointer(root+0x18) == <entry name>.effectx (observation, not in spec)
    long long endSlack = 0;         // (size - root) - endField
    int tailKind = 0;               // 0 none, 1 tail = 2-aligned strings all in the block's texture names, case-insensitive, 2 other tail, 3 end beyond EOF
    size_t tailStrings = 0;
    std::string tailPreview;
    bool tailNonMember = false, tailMisaligned = false, tailUnterminated = false;
    bool texListInBlock = false;    // every root+0x28/0x30 list string is one of the block's names (spec §5.3: 3/3)
    bool texListParsed = false, texListInBlockCi = false;
    bool overlap = false;
    // Controls, evaluated at analysis time on a corrupted copy (gated files only).
    bool ctlRun = false, c13Elig = false;
    bool c1 = false, c2 = false, c3 = false; // true = the perturbed file STILL passed (control failed to fail)
    bool c4Elig = false, c4a = false, c4b = false, c4c = false, c4d = false;
    bool c5Elig = false, c5a = false, c5b = false;
};

// Reads the sub-object layout verdict on a (possibly corrupted) copy.
// Returns false when the copy no longer parses at all.
bool layoutOkOn(const std::vector<uint8_t>& copy) {
    try {
        sr3effects::EffectFile f = sr3effects::EffectFile::parse(vpp::ByteView(copy.data(), copy.size()));
        return f.checkSubObjectLayout().ok();
    } catch (const std::exception&) {
        return false;
    }
}

void putU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t getU32(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

// ---- tallies ----
size_t g_archivesScanned = 0, g_archivesFailed = 0;
size_t g_containersOpened = 0;
size_t g_cmpNotDecoded = 0;          // compressed entries skipped (not .cefct_pc / container name)
size_t g_cmpContainerFail = 0;       // container-named compressed entries that did not decode/open
std::map<std::string, size_t> g_extCensus; // extensions containing "cefct"
std::map<std::string, size_t> g_d0ByStatus;
size_t g_d0 = 0, g_d1 = 0;
std::vector<Occ> g_occ;

// Extent overlap test over the structures whose extents the spec gives
// (count * stride is CONFIRMED in the fix-up code; P/Q pitches per §6.11).
// `stride40/70/90` override the three populated arrays' strides (controls).
bool extentsOverlap(const sr3effects::EffectFile& f, uint32_t s40, uint32_t s70, uint32_t s90) {
    std::vector<Extent> ex;
    auto add = [&](const char* w, const sr3effects::ArrayRef& a, uint32_t stride) {
        if (a.count == 0 || a.isNull()) return;
        ex.push_back({w, a.pointer, static_cast<uint64_t>(a.pointer) + static_cast<uint64_t>(a.count) * stride});
    };
    add("texlist", f.textureList(), sr3effects::kTextureListStride);
    add("a40", f.array40(), s40);
    add("recs", f.subObjects(), sr3effects::kSubObjectStride);
    add("a70", f.array70(), s70);
    add("filt", f.filters(), sr3effects::kFilterStride);
    add("a90", f.array90(), s90);
    const auto n = f.subObjects().count;
    if (n > 0 && !f.subObjects().isNull()) {
        uint64_t p0 = (f.subObjects().extentEnd() + 15) / 16 * 16;
        uint64_t q0 = (p0 + static_cast<uint64_t>(n) * sr3effects::kParamBlockSize + 15) / 16 * 16;
        ex.push_back({"P", p0, p0 + static_cast<uint64_t>(n) * sr3effects::kParamBlockSize});
        ex.push_back({"Q", q0, q0 + static_cast<uint64_t>(n) * sr3effects::kStateBlockSize});
    }
    for (size_t i = 0; i < ex.size(); ++i)
        for (size_t j = i + 1; j < ex.size(); ++j)
            if (ex[i].start < ex[j].end && ex[j].start < ex[i].end) return true;
    return false;
}

// Array-stride tiling measurement. For each populated array among 0x40/0x70/0x90
// the distance from its start to the NEXT root-relative target that any known
// pointer in the file names is compared with count*stride. If the spec's
// strides are right and nothing else sits inside the array, the difference
// (slack) is small (0..15: alignment padding). Pointers with unknown meaning
// inside those three arrays' own elements are not in the target set, so a
// large slack can also mean "an element blob follows" - the histogram is an
// observation, and the wrong-stride comparison below it is the control.
struct TileStats {
    size_t files = 0;
    size_t tightReal = 0, tightX2 = 0, tightP18 = 0, tightM8 = 0; // slack in [0,16)
    std::map<std::pair<uint32_t, long long>, size_t> hist;         // (count, slack) -> files (real stride)
};
TileStats g_tile[3]; // 0x40, 0x70, 0x90

void tileMeasure(const sr3effects::EffectFile& f, vpp::ByteView view) {
    const size_t root = f.rootOffset();
    const uint64_t endRel = view.size() - root;
    std::set<uint64_t> targets;
    auto add = [&](uint32_t v) {
        if (v != sr3effects::kNullPointer && v <= endRel) targets.insert(v);
    };
    for (size_t off : {size_t(0x10), size_t(0x18), size_t(0x30), size_t(0x38), size_t(0x48), size_t(0x58), size_t(0x68),
                       size_t(0x78), size_t(0x88), size_t(0x98)})
        add(view.readU32LE(root + off));
    for (size_t i = 0; i < f.textureList().count && f.arrayFitsInFile(f.textureList()); ++i)
        add(view.readU32LE(root + f.textureList().pointer + i * 8));
    const auto recs = f.readSubObjects();
    for (size_t k = 0; k < recs.size(); ++k) {
        const size_t base = root + f.subObjects().pointer + k * sr3effects::kSubObjectStride;
        for (size_t off : {size_t(0x00), size_t(0x28), size_t(0x30)}) add(view.readU32LE(base + off));
        if (recs[k].paramPointer != sr3effects::kNullPointer &&
            static_cast<uint64_t>(recs[k].paramPointer) + sr3effects::kParamBlockSize <= endRel) {
            const size_t p = root + recs[k].paramPointer;
            add(view.readU32LE(p + 0x1E0));
            add(view.readU32LE(p + 0x1F0));
            for (size_t s = 0x220; s <= 0x480; s += 0x10) add(view.readU32LE(p + s));
        }
        if (recs[k].statePointer != sr3effects::kNullPointer &&
            static_cast<uint64_t>(recs[k].statePointer) + sr3effects::kStateBlockSize <= endRel) {
            const size_t q = root + recs[k].statePointer;
            for (size_t off : {size_t(0x08), size_t(0x20), size_t(0x30), size_t(0x38), size_t(0x48), size_t(0x60)})
                add(view.readU32LE(q + off));
        }
    }
    if (f.arrayFitsInFile(f.filters()))
        for (size_t k = 0; k < f.filters().count; ++k) {
            const size_t base = root + f.filters().pointer + k * sr3effects::kFilterStride;
            for (size_t off : {size_t(0x00), size_t(0x20), size_t(0x40), size_t(0x50), size_t(0x68)}) add(view.readU32LE(base + off));
        }
    if (f.arrayFitsInFile(f.array40()))
        for (size_t k = 0; k < f.array40().count; ++k) {
            const size_t base = root + f.array40().pointer + k * sr3effects::kArray40Stride;
            add(view.readU32LE(base + 0x00)); // element name pointer (spec §6.8)
            add(view.readU32LE(base + 0x10)); // pointer to three floats (spec §6.8)
        }
    const sr3effects::ArrayRef arrs[3] = {f.array40(), f.array70(), f.array90()};
    for (int a = 0; a < 3; ++a) {
        const sr3effects::ArrayRef& r = arrs[a];
        if (r.count == 0 || r.isNull() || !f.arrayFitsInFile(r)) continue;
        auto next = targets.upper_bound(r.pointer);
        if (next == targets.end()) continue;
        const long long gap = static_cast<long long>(*next - r.pointer);
        auto slack = [&](uint64_t stride) { return gap - static_cast<long long>(r.count * stride); };
        TileStats& t = g_tile[a];
        ++t.files;
        const long long s0 = slack(r.stride);
        ++t.hist[{r.count, s0}];
        auto tight = [](long long s) { return s >= 0 && s < 16; };
        t.tightReal += tight(s0);
        t.tightX2 += tight(slack(r.stride * 2));
        t.tightP18 += tight(slack(r.stride + 0x18));
        t.tightM8 += tight(slack(r.stride - 8));
    }
}

void analyse(Occ& o, const uint8_t* data, size_t size) {
    o.size = size;
    o.hash = fnv1a(data, size);
    vpp::ByteView view(data, size);
    try {
        sr3effects::EffectFile f = sr3effects::EffectFile::parse(view);
        o.parsed = true;
        tileMeasure(f, view);
        o.root = f.rootOffset();
        o.version = f.version();
        o.endField = f.endField();
        o.flagCount = f.flagCount();
        o.endOk = f.endFieldMatchesFileSize();
        o.c28 = f.textureList().count;
        o.c40 = f.array40().count;
        o.c60 = f.subObjects().count;
        o.c70 = f.array70().count;
        o.c80 = f.filters().count;
        o.c90 = f.array90().count;
        o.arraysFit = f.arrayFitsInFile(f.textureList()) && f.arrayFitsInFile(f.array40()) &&
                      f.arrayFitsInFile(f.subObjects()) && f.arrayFitsInFile(f.array70()) &&
                      f.arrayFitsInFile(f.filters()) && f.arrayFitsInFile(f.array90());
        auto lc = f.checkSubObjectLayout();
        o.layoutApplicable = lc.applicable;
        o.layoutOk = lc.ok();
        o.filterClasses = f.readFilterClassIds();
        for (const auto& r : f.readSubObjects()) o.volumeClasses.push_back(r.classId);
        o.emitterTypes = f.readEmitterTypes();
        o.namesParsed = f.namesParsed();
        if (f.namesParsed())
            o.nameSlack = static_cast<long>(f.nameTableLength()) - static_cast<long>(f.nameBytesConsumed());
        {
            std::string base = lower(o.name);
            const std::string ext = ".cefct_pc";
            if (endsWith(base, ext)) base.resize(base.size() - ext.size());
            std::string src;
            o.sourceNameMatches = f.stringAt(f.pointer18(), src) && lower(src) == base + ".effectx";
        }
        {
            bool resolved = false;
            std::vector<std::string> lst = f.readTextureListNames(resolved);
            o.texListParsed = resolved;
            std::set<std::string> blockNames(f.textureNames().begin(), f.textureNames().end());
            bool all = resolved, allCi = resolved;
            std::set<std::string> blockLower;
            for (const auto& n : blockNames) blockLower.insert(lower(n));
            for (const std::string& s : lst) {
                if (!blockNames.count(s)) all = false;
                if (!blockLower.count(lower(s))) allCi = false;
            }
            o.texListInBlock = all;
            o.texListInBlockCi = allCi;
        }
        o.endSlack = f.endSlack();
        if (f.endSlack() < 0) {
            o.tailKind = 3;
        } else if (f.endSlack() > 0) {
            // Bytes follow the declared end. Measured shape (see the report):
            // a run of NUL-terminated strings, each starting on a 2-byte
            // boundary of the tail, zero-padded, every string equal (ignoring
            // case) to one of the material block's texture names.
            const uint8_t* t = data + o.root + f.endField();
            const size_t tail = static_cast<size_t>(f.endSlack());
            std::set<std::string> blockNames;
            for (const auto& n : f.textureNames()) blockNames.insert(lower(n));
            size_t pos = 0;
            bool nonMember = false, misaligned = false, unterminated = false;
            while (pos < tail) {
                if (t[pos] == 0) { ++pos; continue; }
                if (pos % 2 != 0) misaligned = true;
                size_t j = pos;
                while (j < tail && t[j] != 0) ++j;
                if (j == tail) { unterminated = true; break; }
                if (!blockNames.count(lower(std::string(reinterpret_cast<const char*>(t) + pos, j - pos)))) nonMember = true;
                ++o.tailStrings;
                pos = j + 1;
            }
            o.tailNonMember = nonMember;
            o.tailMisaligned = misaligned;
            o.tailUnterminated = unterminated;
            o.tailKind = (!nonMember && !misaligned && !unterminated && o.tailStrings > 0) ? 1 : 2;
            if (o.tailKind == 2) {
                for (size_t i = 0; i < tail && i < 160; ++i) {
                    char ch = static_cast<char>(t[i]);
                    o.tailPreview += (ch >= 32 && ch < 127) ? ch : (ch == 0 ? '.' : '?');
                }
                std::string names;
                for (const auto& n : f.textureNames()) names += n + "|";
                o.tailPreview += "   <block names: " + names + ">";
            }
        }
        o.overlap = extentsOverlap(f, sr3effects::kArray40Stride, sr3effects::kArray70Stride,
                                   sr3effects::kArray90Stride);

        {
            o.ctlRun = true;
            o.c13Elig = o.endOk;
            // Every c* field below is the checker's PASS verdict on the
            // perturbed input; a discriminating check gives false.
            if (o.endOk) {   // C1: drop the last byte
                try {
                    sr3effects::EffectFile g = sr3effects::EffectFile::parse(vpp::ByteView(data, size - 1));
                    o.c1 = g.endFieldMatchesFileSize();
                } catch (const std::exception&) { o.c1 = false; }
            }
            {   // C2: start 16 bytes late
                try {
                    (void)sr3effects::EffectFile::parse(vpp::ByteView(data + 16, size - 16));
                    o.c2 = true;
                } catch (const std::exception&) { o.c2 = false; }
            }
            // C3: compare the end field with root+16 instead of root
            if (o.endOk) o.c3 = f.endField() == size - (f.rootOffset() + 16);
            if (lc.ok()) {
                o.c4Elig = true;
                const size_t root = f.rootOffset();
                const size_t recBase = root + f.subObjects().pointer;
                const uint32_t n = f.subObjects().count;
                {   std::vector<uint8_t> t(data, data + size);
                    putU32(t, root + sr3effects::kRootSubObjectCount, n + 1);
                    o.c4a = layoutOkOn(t); }
                {   std::vector<uint8_t> t(data, data + size);
                    const size_t at = recBase + static_cast<size_t>(n - 1) * sr3effects::kSubObjectStride + 0x28;
                    putU32(t, at, getU32(t, at) + 16);
                    o.c4b = layoutOkOn(t); }
                {   std::vector<uint8_t> t(data, data + size);
                    const size_t pAbs = root + getU32(t, recBase + 0x28);
                    uint32_t v = getU32(t, pAbs + 0x1E0);
                    putU32(t, pAbs + 0x1E0, v == sr3effects::kNullPointer ? 1u : v + 8);
                    o.c4c = layoutOkOn(t); }
                {   std::vector<uint8_t> t(data, data + size);
                    const size_t pAbs = root + getU32(t, recBase + 0x28);
                    t[pAbs + sr3effects::kParamBlockSize - 1] |= 1;
                    o.c4d = layoutOkOn(t); }
            }
            if ((o.c40 || o.c70 || o.c90) && !o.overlap) {
                o.c5Elig = true;
                o.c5a = !extentsOverlap(f, sr3effects::kArray40Stride * 2, sr3effects::kArray70Stride * 2,
                                        sr3effects::kArray90Stride * 2);
                o.c5b = !extentsOverlap(f, sr3effects::kArray40Stride + 0x18, sr3effects::kArray70Stride + 0x18,
                                        sr3effects::kArray90Stride + 0x18);
            }
        }
    } catch (const sr3effects::FormatError& e) {
        o.parseFail = e.what();
        o.parsed = false;
    } catch (const std::exception& e) {
        o.parseFail = std::string("exception: ") + e.what();
        o.parsed = false;
    }
}

// Debug aid: --dump <name-substring> <out-dir> writes each matching entry's
// decoded bytes (at most 6) so a failing file can be hex-inspected.
std::string g_dumpSub, g_dumpDir;
int g_dumped = 0;

void takeLeaf(const std::string& archive, const std::string& path, const std::string& name,
              const std::string& how, const uint8_t* data, size_t size) {
    if (!g_dumpSub.empty() && lower(name).find(g_dumpSub) != std::string::npos && g_dumped < 6) {
        std::string fn = g_dumpDir + "/" + std::to_string(g_dumped++) + "_" + name;
        std::ofstream o(fn, std::ios::binary);
        o.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    Occ o;
    o.archive = archive;
    o.path = path;
    o.name = name;
    o.how = how;
    analyse(o, data, size);
    g_occ.push_back(std::move(o));
    ++g_d1;
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& path);

void walkDecodedContainer(const std::vector<uint8_t>& data, const std::string& archive,
                          const std::string& path) {
    try {
        vpp::Container n{vpp::ByteView(data.data(), data.size())};
        ++g_containersOpened;
        walk(n, archive, path);
    } catch (const std::exception&) {
        ++g_cmpContainerFail;
    }
}

void walk(const vpp::Container& c, const std::string& archive, const std::string& path) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const auto& e = c.entries()[i];
        const std::string here = path + "/" + e.name;
        const bool isCefct = endsWith(lower(e.name), ".cefct_pc");
        {
            size_t dot = e.name.find_last_of('.');
            if (dot != std::string::npos) {
                std::string ext = lower(e.name.substr(dot));
                if (ext.find("cefct") != std::string::npos) ++g_extCensus[ext];
            }
        }
        if (isCefct) ++g_d0;
        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                if (!isCefct) {
                    try {
                        vpp::Container n = c.openNested(i);
                        ++g_containersOpened;
                        walk(n, archive, here);
                    } catch (const vpp::FormatError&) {
                        // ordinary leaf, not a container
                    }
                } else {
                    vpp::ByteView v = c.rawEntryBytes(i);
                    ++g_d0ByStatus["raw"];
                    takeLeaf(archive, path, e.name, "raw", v.data(), v.size());
                }
            } else {
                const std::string ln = lower(e.name);
                const bool containerNamed = endsWith(ln, ".str2_pc") || endsWith(ln, ".vpp_pc");
                if (!isCefct && !containerNamed) {
                    ++g_cmpNotDecoded;
                    continue;
                }
                vpp::DecompressResult r = c.decompressEntry(i);
                if (isCefct) {
                    ++g_d0ByStatus[statusName(r.status)];
                    if (statusUsable(r.status))
                        takeLeaf(archive, path, e.name, statusName(r.status), r.data.data(), r.data.size());
                } else if (statusUsable(r.status)) {
                    walkDecodedContainer(r.data, archive, here);
                } else {
                    ++g_cmpContainerFail;
                }
            }
        } catch (const std::exception& ex) {
            if (isCefct) ++g_d0ByStatus[std::string("exception: ") + ex.what()];
        }
    }
}

// ---------------------------------------------------------------- reporting

std::string countTuple(const Occ& o) {
    char b[96];
    snprintf(b, sizeof b, "c40=%u c70=%u c90=%u", o.c40, o.c70, o.c90);
    return b;
}

using Getter = uint32_t Occ::*;

void histogram(const char* label, const std::vector<const Occ*>& set, Getter g) {
    std::map<uint32_t, size_t> h;
    for (const Occ* o : set) ++h[(*o).*g];
    printf("  %s:", label);
    for (const auto& kv : h) printf("  %u x%zu", kv.first, kv.second);
    printf("\n");
}

void examples(const char* label, const std::vector<const Occ*>& set, Getter g) {
    // distinct content (by name+hash), largest count first, up to 10
    std::vector<const Occ*> v;
    std::set<std::pair<std::string, uint64_t>> seen;
    for (const Occ* o : set)
        if ((*o).*g != 0 && seen.insert({o->name, o->hash}).second) v.push_back(o);
    std::stable_sort(v.begin(), v.end(), [&](const Occ* a, const Occ* b) { return (*a).*g > (*b).*g; });
    printf("  examples with non-zero %s (distinct content: %zu; showing up to 10, largest count first):\n",
           label, v.size());
    for (size_t i = 0; i < v.size() && i < 10; ++i)
        printf("    %-46s count=%-3u %s%s  [%s]\n", v[i]->name.c_str(), (*v[i]).*g, v[i]->archive.c_str(),
               v[i]->path.c_str(), v[i]->how.c_str());
}

void countsTable(const char* title, const std::vector<const Occ*>& set) {
    printf("\n--- %s: %zu occurrences ---\n", title, set.size());
    std::set<std::pair<std::string, uint64_t>> distinct;
    for (const Occ* o : set) distinct.insert({o->name, o->hash});
    printf("  distinct (name, content) files: %zu\n", distinct.size());

    auto nz = [&](Getter g) {
        size_t occ = 0;
        std::set<std::pair<std::string, uint64_t>> d;
        for (const Occ* o : set)
            if ((*o).*g != 0) {
                ++occ;
                d.insert({o->name, o->hash});
            }
        return std::make_pair(occ, d.size());
    };
    auto p40 = nz(&Occ::c40), p70 = nz(&Occ::c70), p90 = nz(&Occ::c90);
    printf("  non-zero count (occurrences / distinct files):\n");
    printf("    root+0x40 (stride 0x88):  %zu / %zu\n", p40.first, p40.second);
    printf("    root+0x70 (stride 0xC8):  %zu / %zu\n", p70.first, p70.second);
    printf("    root+0x90 (stride 0x228): %zu / %zu\n", p90.first, p90.second);

    std::map<std::string, std::pair<size_t, std::set<std::pair<std::string, uint64_t>>>> combo;
    for (const Occ* o : set) {
        std::string k = std::string(o->c40 ? "40" : "--") + "," + (o->c70 ? "70" : "--") + "," + (o->c90 ? "90" : "--");
        auto& e = combo[k];
        ++e.first;
        e.second.insert({o->name, o->hash});
    }
    printf("  combinations (which of 0x40,0x70,0x90 are non-zero) occurrences / distinct:\n");
    for (const auto& kv : combo) printf("    %-10s %zu / %zu\n", kv.first.c_str(), kv.second.first, kv.second.second.size());

    printf("  count-value distribution (value xoccurrences):\n");
    histogram("root+0x40", set, &Occ::c40);
    histogram("root+0x70", set, &Occ::c70);
    histogram("root+0x90", set, &Occ::c90);
    histogram("root+0x60 (sub-objects, for context)", set, &Occ::c60);
    histogram("root+0x80 (filters, for context)", set, &Occ::c80);
    histogram("root+0x28 (texture list, for context)", set, &Occ::c28);
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    std::string tsvPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--tsv" && i + 1 < argc) tsvPath = argv[++i];
        else if (a == "--dump" && i + 2 < argc) { g_dumpSub = lower(argv[i + 1]); g_dumpDir = argv[i + 2]; i += 2; }
        else archives.push_back(a);
    }
    if (archives.empty()) {
        const fs::path dir = "D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache";
        for (const auto& de : fs::directory_iterator(dir))
            if (de.path().extension() == ".vpp_pc") archives.push_back(de.path().string());
        std::sort(archives.begin(), archives.end());
    }

    for (const std::string& a : archives) {
        std::vector<uint8_t> bytes = readFile(a);
        if (bytes.empty()) {
            printf("could not read %s\n", a.c_str());
            ++g_archivesFailed;
            continue;
        }
        const std::string base = fs::path(a).filename().string();
        try {
            vpp::Container c{vpp::ByteView(bytes.data(), bytes.size())};
            ++g_containersOpened;
            size_t before = g_d0;
            walk(c, base, "");
            printf("scanned %-28s cefct entries: %zu\n", base.c_str(), g_d0 - before);
            fflush(stdout);
            ++g_archivesScanned;
        } catch (const std::exception& ex) {
            printf("skip %s: %s\n", base.c_str(), ex.what());
            ++g_archivesFailed;
        }
    }

    printf("\n=== WALK ===\n");
    printf("archives scanned %zu (failed %zu); containers opened %zu\n", g_archivesScanned,
           g_archivesFailed, g_containersOpened);
    printf("compressed entries NOT decoded (not a .cefct_pc, not container-named): %zu\n", g_cmpNotDecoded);
    printf("container-named compressed entries that failed to decode/open: %zu\n", g_cmpContainerFail);
    printf("extension census (extensions containing \"cefct\"):");
    for (const auto& kv : g_extCensus) printf("  %s x%zu", kv.first.c_str(), kv.second);
    printf("\n");
    printf("D0 = .cefct_pc directory entries reached: %zu\n", g_d0);
    printf("   by decode status:");
    for (const auto& kv : g_d0ByStatus) printf("  %s x%zu", kv.first.c_str(), kv.second);
    printf("\n");
    printf("D1 = bytes obtained: %zu\n", g_d1);


    // Gates (definitions in the header comment; E is the spec's own §5.3
    // identity, R relaxes it by the trailing-texture-copy shape measured on
    // real files, B adds the body-structure checks).
    std::vector<const Occ*> parsed, strict, relaxed, body, notParsed;
    for (Occ& o : g_occ) {
        if (!o.parsed) {
            notParsed.push_back(&o);
            continue;
        }
        parsed.push_back(&o);
        if (o.endOk) strict.push_back(&o);
        const bool r = o.endOk || o.tailKind == 1;
        const bool bodyOk = o.arraysFit && (!o.layoutApplicable || o.layoutOk) && !o.overlap;
        if (r) relaxed.push_back(&o);
        if (r && bodyOk) body.push_back(&o);
    }
    printf("D2 = parsed (material magic, 71BW at align16(0x20+len+1), version 42..44): %zu\n", parsed.size());
    printf("E  = D2 and root+0x38 == size - root (spec §5.3 exact-consumption identity): %zu\n", strict.size());
    printf("R  = E, or every byte after the declared end is a 2-aligned NUL-terminated string equal (any case) to a block texture name: %zu\n", relaxed.size());
    printf("B  = R and all six arrays fit, record->P->Q layout ok (when records exist), no extent overlap: %zu\n", body.size());
    if (!notParsed.empty()) {
        std::map<std::string, size_t> why;
        for (const Occ* o : notParsed) ++why[o->parseFail];
        printf("not parsed (%zu):\n", notParsed.size());
        for (const auto& kv : why) printf("   %-90s x%zu\n", kv.first.c_str(), kv.second);
    }
    {
        std::map<std::string, std::array<size_t, 4>> byHow; // D2, E, R, B
        std::map<std::string, std::array<size_t, 4>> byArch;
        for (const Occ* o : parsed) {
            const bool r = o->endOk || o->tailKind == 1;
            const bool bodyOk = o->arraysFit && (!o->layoutApplicable || o->layoutOk) && !o->overlap;
            for (auto* m : {&byHow[o->how], &byArch[o->archive]}) {
                ++(*m)[0];
                if (o->endOk) ++(*m)[1];
                if (r) ++(*m)[2];
                if (r && bodyOk) ++(*m)[3];
            }
        }
        printf("by obtain-status        D2 / E / R / B:\n");
        for (const auto& kv : byHow) printf("   %-34s %zu / %zu / %zu / %zu\n", kv.first.c_str(), kv.second[0], kv.second[1], kv.second[2], kv.second[3]);
        printf("by archive              D2 / E / R / B:\n");
        for (const auto& kv : byArch) printf("   %-34s %zu / %zu / %zu / %zu\n", kv.first.c_str(), kv.second[0], kv.second[1], kv.second[2], kv.second[3]);
    }

    // ---- end-field analysis: how does the spec's §5.3 identity fail? ----
    printf("\n=== END FIELD (root+0x38) vs FILE SIZE, over D2 (%zu) ===\n", parsed.size());
    {
        size_t exact = 0, beyond = 0, tail = 0, tailNames = 0, tailOther = 0, failC40zero = 0, failC40pos = 0, okC40pos = 0, okC40zero = 0;
        size_t nonMember = 0, misal = 0, unterm = 0, noStrings = 0;
        std::map<long long, size_t> slackHist;
        for (const Occ* o : parsed) {
            if (o->endOk) {
                ++exact;
                if (o->c40) ++okC40pos; else ++okC40zero;
            } else {
                if (o->c40) ++failC40pos; else ++failC40zero;
                ++slackHist[o->endSlack];
                if (o->tailKind == 3) ++beyond;
                if (o->tailKind == 1) ++tailNames;
                if (o->tailKind == 2) {
                    ++tailOther;
                    nonMember += o->tailNonMember;
                    misal += o->tailMisaligned;
                    unterm += o->tailUnterminated;
                    if (o->tailStrings == 0) ++noStrings;
                }
                if (o->endSlack > 0) ++tail;
            }
        }
        printf("end field == size - root: %zu   (of which c40 > 0: %zu, c40 == 0: %zu)\n", exact, okC40pos, okC40zero);
        printf("end field != size - root: %zu   (of which c40 > 0: %zu, c40 == 0: %zu)\n", parsed.size() - exact, failC40pos, failC40zero);
        printf("  declared end lies BEFORE the file end (bytes follow it): %zu\n", tail);
        printf("  declared end lies beyond the file end: %zu\n", beyond);
        printf("  bytes-after-end are only NUL-terminated strings, each on a 2-byte tail boundary, each one of the block's texture names (case-insensitive): %zu\n", tailNames);
        printf("  bytes-after-end that are anything else: %zu  (contain a non-member string: %zu; misaligned string: %zu; unterminated: %zu; no string at all: %zu)\n",
               tailOther, nonMember, misal, unterm, noStrings);
        {
            size_t listIn = 0, listCi = 0, listUnres = 0;
            for (const Occ* o : parsed) {
                listIn += o->texListInBlock;
                listCi += o->texListInBlockCi;
                listUnres += !o->texListParsed;
            }
            printf("  spec §5.3 replay - every root+0x28/0x30 texture-list string is one of the block's names: exact %zu / %zu, ignoring case %zu / %zu (list unresolvable/out of file: %zu)\n",
                   listIn, parsed.size(), listCi, parsed.size(), listUnres);
        }
        {
            size_t shown = 0;
            std::set<std::string> seenPrev;
            for (const Occ* o : parsed)
                if (o->tailKind == 2 && shown < 10 && seenPrev.insert(o->tailPreview).second) {
                    printf("  example tail (kind 2) of %s (%zu bytes, size %zu, c28=%u c40=%u): %s\n", o->name.c_str(),
                           static_cast<size_t>(o->endSlack), o->size, o->c28, o->c40, o->tailPreview.c_str());
                    ++shown;
                }
        }
        printf("  bytes after declared end (bytes x files, top 12 by frequency):");
        {
            std::vector<std::pair<size_t, long long>> v;
            for (const auto& kv : slackHist) v.push_back({kv.second, kv.first});
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < 12; ++i) printf("  %lld x%zu", v[i].second, v[i].first);
        }
        printf("\n");
        size_t noTex = 0, noTexTail = 0;
        for (const Occ* o : parsed)
            if (o->c28 == 0) {
                ++noTex;
                if (o->tailKind >= 1) ++noTexTail;
            }
        printf("  files with root+0x28 (texture-list count) == 0: %zu, of which have bytes after the declared end: %zu\n", noTex, noTexTail);
    }

    // ---- main tables ----
    countsTable("PRIMARY: SET B (structure-gated)", body);
    for (auto [label, g] : {std::pair<const char*, Getter>{"root+0x40", &Occ::c40},
                            {"root+0x70", &Occ::c70},
                            {"root+0x90", &Occ::c90}})
        examples(label, body, g);
    countsTable("SET E (spec's strict end-field gate only)", strict);
    countsTable("SET D2 (parsed; no end/body gate) - dependence on the gates", parsed);

    // ---- other layout facts ----
    printf("\n=== LAYOUT FACTS OVER D2 (%zu) ===\n", parsed.size());
    {
        size_t fit = 0, app = 0, lok = 0, ovl = 0, srcMatch = 0, namesP = 0;
        std::map<long, size_t> slack;
        std::map<uint32_t, size_t> ver;
        std::map<int32_t, size_t> flag;
        for (const Occ* o : parsed) {
            if (o->arraysFit) ++fit;
            if (o->layoutApplicable) {
                ++app;
                if (o->layoutOk) ++lok;
            }
            if (o->overlap) ++ovl;
            if (o->sourceNameMatches) ++srcMatch;
            if (o->namesParsed) {
                ++namesP;
                ++slack[o->nameSlack];
            }
            ++ver[o->version];
            ++flag[o->flagCount];
        }
        printf("all six arrays fit inside the file: %zu / %zu\n", fit, parsed.size());
        printf("sub-object layout applicable (count > 0): %zu; passing record->P->Q chain + P pointer slots + P tail: %zu\n", app, lok);
        printf("known extents overlapping each other: %zu / %zu\n", ovl, parsed.size());
        printf("texture names parsed from the material block: %zu / %zu\n", namesP, parsed.size());
        printf("nameTableLength - bytes consumed by the texture-name walk:");
        for (const auto& kv : slack) printf("  %ld x%zu", kv.first, kv.second);
        printf("\nstring at root+ptr(root+0x18) == <entry name>.effectx (observation; spec §5.3 leaves this pointer's content OPEN): %zu / %zu\n",
               srcMatch, parsed.size());
        printf("version:");
        for (const auto& kv : ver) printf("  %u x%zu", kv.first, kv.second);
        printf("\nroot+0x08 flag/count:");
        for (const auto& kv : flag) printf("  %d x%zu", kv.first, kv.second);
        printf("\n");
        std::map<uint32_t, size_t> fc, vc;
        std::map<uint16_t, size_t> et;
        for (const Occ* o : body) {
            for (uint32_t x : o->filterClasses) ++fc[x];
            for (uint32_t x : o->volumeClasses) ++vc[x];
            for (uint16_t x : o->emitterTypes) ++et[x];
        }
        printf("(class-id tables below are over set B)\n");
        printf("filter record class ids (record+0x10, spec §6.10):");
        for (const auto& kv : fc) printf("  0x%08X x%zu", kv.first, kv.second);
        printf("\nsub-object class ids (record+0x10, spec §6.6):");
        for (const auto& kv : vc) printf("  0x%08X x%zu", kv.first, kv.second);
        printf("\nP+0x02 emitter types (spec §6.7):");
        for (const auto& kv : et) printf("  %u x%zu", kv.first, kv.second);
        printf("\n");
    }

    // ---- array-stride tiling ----
    printf("\n=== STRIDE TILING of the three arrays (over D2 occurrences with a populated array) ===\n");
    printf("slack = (distance to the next known pointer target) - count*stride; 'tight' = 0 <= slack < 16\n");
    {
        const char* nm[3] = {"root+0x40 stride 0x88 ", "root+0x70 stride 0xC8 ", "root+0x90 stride 0x228"};
        for (int a = 0; a < 3; ++a) {
            const TileStats& t = g_tile[a];
            printf("  %s files %zu | tight with real stride %zu | x2 %zu | +0x18 %zu | -8 %zu\n", nm[a], t.files,
                   t.tightReal, t.tightX2, t.tightP18, t.tightM8);
            std::vector<std::pair<size_t, std::pair<uint32_t, long long>>> v;
            for (const auto& kv : t.hist) v.push_back({kv.second, kv.first});
            std::sort(v.rbegin(), v.rend());
            printf("     (count, slack) x files, top 10:");
            for (size_t i = 0; i < v.size() && i < 10; ++i)
                printf("  (%u,%lld) x%zu", v[i].second.first, v[i].second.second, v[i].first);
            printf("\n");
        }
    }

    // ---- spec anchor replay: the three published samples ----
    printf("\n=== SPEC ANCHOR REPLAY (spec §5.2/§5.4 published values) ===\n");
    struct Anchor { const char* name; size_t size, root, recs, filters; };
    const Anchor anchors[] = {{"vfx_shockwave_kill_all.cefct_pc", 12480, 112, 5, 1},
                              {"vfx_whored_invulnerable.cefct_pc", 5264, 64, 2, 1},
                              {"vfx_runningman_fireworks.cefct_pc", 14416, 112, 6, 0}};
    for (const Anchor& a : anchors) {
        size_t found = 0, agree = 0;
        for (const Occ& o : g_occ)
            if (o.name == a.name) {
                ++found;
                if (o.parsed && o.size == a.size && o.root == a.root && o.c60 == a.recs && o.c80 == a.filters &&
                    o.c40 == 0 && o.c70 == 0 && o.c90 == 0 && o.endOk)
                    ++agree;
            }
        printf("  %-38s occurrences %zu, agreeing with spec (size %zu root %zu recs %zu filters %zu, c40=c70=c90=0, end identity) %zu\n",
               a.name, found, a.size, a.root, a.recs, a.filters, agree);
    }

    // ---- controls ----
    printf("\n=== CONTROLS (numbers = files on which the checker still PASSED the perturbed input; 0 = the check discriminates) ===\n");
    {
        size_t run = 0, e13 = 0, c1 = 0, c2 = 0, c3 = 0, elig = 0, a = 0, b = 0, c = 0, d = 0, e5 = 0, c5a = 0, c5b = 0;
        for (const Occ* o : parsed) {
            if (!o->ctlRun) continue;
            ++run;
            c2 += o->c2;
            if (o->c13Elig) { ++e13; c1 += o->c1; c3 += o->c3; }
            if (o->c4Elig) { ++elig; a += o->c4a; b += o->c4b; c += o->c4c; d += o->c4d; }
            if (o->c5Elig) { ++e5; c5a += o->c5a; c5b += o->c5b; }
        }
        printf("  C2 re-slice 16 bytes late, parse still succeeds (all D2):                          %zu / %zu\n", c2, run);
        printf("  C1 truncate 1 byte, strict end identity still passes (eligible = set E):           %zu / %zu\n", c1, e13);
        printf("  C3 end field compared with root+16, identity still passes (eligible = set E):      %zu / %zu\n", c3, e13);
        printf("  layout-check controls (eligible = layout check passed originally: %zu):\n", elig);
        printf("  C4a sub-object count+1, layout still ok:                        %zu / %zu\n", a, elig);
        printf("  C4b last record's P pointer +16, layout still ok:               %zu / %zu\n", b, elig);
        printf("  C4c record 0's P+0x1E0 slot made misaligned, layout still ok:   %zu / %zu\n", c, elig);
        printf("  C4d record 0's P tail byte set non-zero, layout still ok:       %zu / %zu\n", d, elig);
        printf("  extent-overlap controls (eligible = a populated 0x40/0x70/0x90 array, no overlap originally: %zu):\n", e5);
        printf("  C5a all three strides x2, still no overlap:                     %zu / %zu\n", c5a, e5);
        printf("  C5b all three strides +0x18, still no overlap:                  %zu / %zu\n", c5b, e5);
    }


    // ---- TSV of every populated file ----
    if (!tsvPath.empty()) {
        std::ofstream t(tsvPath);
        t << "archive\tpath\tname\thow\tsize\tparsed\tend_ok\troot\tversion\tflag08\tendField\tsize_minus_root\tend_slack\ttail_kind\tsrc18\tc28\tc40\tc60\tc70\tc80\tc90\thash\n";
        for (const Occ& o : g_occ) {
            t << o.archive << '\t' << o.path << '\t' << o.name << '\t' << o.how << '\t' << o.size << '\t'
              << o.parsed << '\t' << o.endOk << '\t' << o.root << '\t' << o.version << '\t' << o.flagCount << '\t'
              << o.endField << '\t' << (o.size - o.root) << '\t' << o.endSlack << '\t' << o.tailKind << '\t'
              << o.sourceNameMatches << '\t'
              << o.c28 << '\t' << o.c40 << '\t' << o.c60 << '\t' << o.c70
              << '\t' << o.c80 << '\t' << o.c90 << '\t' << std::hex << o.hash << std::dec << '\n';
        }
        printf("wrote %s (%zu rows)\n", tsvPath.c_str(), g_occ.size());
    }
    return 0;
}
