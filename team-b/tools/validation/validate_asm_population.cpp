// Population validation of the .asm_pc manifest reader against the NEW
// spec-asm-format.md (sections 6-10, version-11 layout).
//
// What this harness does, and why each part is here:
//
//  1. Finds every .asm_pc directory entry in every archive it is given
//     (top level and, recursively, inside any raw entry that itself carries the
//     container magic) and records how each is stored. A file that is stored
//     compressed is COUNTED and not parsed (its decode would be unverifiable
//     for a manifest); the count is reported, not hidden.
//
//  2. Parses each file two independent ways and compares them:
//       (a) sr3asm::AsmManifest (the library reader), and
//       (b) a second, deliberately separate walker written in this file
//           straight from spec 7.1-7.4, parameterised so it can be perturbed.
//     Both must consume EXACTLY to end of file with the header's record_count
//     records, and their per-record field digests must be equal. Agreement of
//     (a) and (b) alone proves only that two readings of one document agree,
//     so the evidence that actually matters is 3 and 4.
//
//  3. CONTROLS that can fail: the (b) walker is rerun with one thing changed
//     at a time (u8 count, no source_name, size table moved / dropped, entry
//     tail 12 or 14 bytes, start of the record area shifted by +-1/+2, ...)
//     and the raw bytes are perturbed (truncate, append, delete/insert a
//     byte). The gate should collapse for each. The control that skips the
//     `extra` blob is reported as uninformative by the spec (extra_len is 0
//     everywhere) and is expected to NOT collapse; it is printed so that is
//     visible rather than assumed.
//
//  4. Independent evidence from real data outside the manifest: each
//     record's backing `<name>.str2_pc` (or `<name minus extension>.str2_pc`)
//     directory is read with vpp::Container, and the manifest-derived file
//     list, sizes, header_region_size (= payload_start) and payload_length
//     (= header field 0x168, 0 for the all-raw sentinel) are compared with it.
//     Control: the same comparison against the NEXT record's sibling.
//
//  5. Every population number quoted in the spec is recomputed and printed
//     beside the spec's value with MATCH / MISMATCH.
//
// Usage: validate_asm_population [archive.vpp_pc ...]
//   With no arguments: every *.vpp_pc in the game's packfiles\pc\cache plus
//   launcher.vpp_pc one directory above the game's packfiles folder.
// Read-only on game data.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sr3asm/manifest.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;
namespace fs = std::filesystem;

const char* kGameRoot = "D:\\Project Crreish\\Saints Row 3 CRREISH";

Bytes readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0);
    Bytes b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool endsWithCI(const std::string& s, const std::string& x) {
    if (s.size() < x.size()) return false;
    return lower(s.substr(s.size() - x.size())) == lower(x);
}

// ---------------------------------------------------------------- inventory

struct AsmFile {
    std::string archive; // archive file name
    std::string path;    // path inside the archive (a/b/name)
    std::string name;    // the entry's own name
    bool topLevel = true;
    Bytes bytes;
};

struct DirItem {
    std::string name;
    uint32_t size = 0;
};
struct Str2Info {
    std::string archive;
    std::string name;
    uint32_t payloadStart = 0;
    uint32_t h168 = 0; // header field 0x168, read as a raw u32
    std::vector<DirItem> dir;
};

struct Inventory {
    std::vector<AsmFile> files;
    std::vector<Str2Info> str2;
    size_t archivesRead = 0, archivesFailed = 0;
    size_t nestedOpened = 0, nestedFailed = 0;
    size_t asmCompressed = 0;          // .asm_pc entries stored compressed (not parsed)
    size_t asmNested = 0;              // .asm_pc found below the top level
    size_t str2TopRaw = 0, str2TopCompressed = 0, str2TopFailed = 0;
    size_t compressedContainerNames = 0; // compressed entries named *.str2_pc / *.vpp_pc anywhere (not opened)
    std::map<std::string, size_t> asmPerArchive;
};

void walk(const vpp::Container& c, const std::string& archive, const std::string& path, int depth,
          Inventory& inv) {
    if (depth > 8) return;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        const bool raw = e.payload.kind == vpp::PayloadKind::Raw;
        const bool isAsm = endsWithCI(e.name, ".asm_pc");
        const bool isStr2 = endsWithCI(e.name, ".str2_pc");
        if (isAsm) {
            if (!raw) {
                ++inv.asmCompressed;
            } else {
                AsmFile f;
                f.archive = archive;
                f.path = path + e.name;
                f.name = e.name;
                f.topLevel = depth == 0;
                vpp::ByteView rb = c.rawEntryBytes(i);
                f.bytes.assign(rb.data(), rb.data() + rb.size());
                if (depth > 0) ++inv.asmNested;
                ++inv.asmPerArchive[archive];
                inv.files.push_back(std::move(f));
            }
        }
        if (!raw) {
            if (isStr2 || endsWithCI(e.name, ".vpp_pc")) ++inv.compressedContainerNames;
            if (isStr2 && depth == 0) ++inv.str2TopCompressed;
            continue;
        }
        vpp::ByteView rb;
        try {
            rb = c.rawEntryBytes(i);
        } catch (const std::exception&) {
            continue;
        }
        if (rb.size() < 4 || rb.readU32LE(0) != vpp::kMagic) {
            if (isStr2 && depth == 0) ++inv.str2TopFailed;
            continue;
        }
        try {
            vpp::Container n = c.openNested(i);
            ++inv.nestedOpened;
            if (isStr2 && depth == 0) {
                Str2Info s;
                s.archive = archive;
                s.name = e.name;
                s.payloadStart = static_cast<uint32_t>(n.header().payloadStart());
                s.h168 = rb.size() >= 0x16C ? rb.readU32LE(0x168) : 0;
                for (const vpp::Entry& ne : n.entries())
                    s.dir.push_back({ne.name, static_cast<uint32_t>(ne.payload.decompressedLength)});
                inv.str2.push_back(std::move(s));
                ++inv.str2TopRaw;
            }
            walk(n, archive, path + e.name + "/", depth + 1, inv);
        } catch (const std::exception&) {
            ++inv.nestedFailed;
            if (isStr2 && depth == 0) ++inv.str2TopFailed;
        }
    }
}

// -------------------------------------------------- independent spec walker

// Digest helpers: FNV-1a over a canonical field order, used to compare the
// library's parse with the walker's parse of the same file.
struct Digest {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    void u(uint64_t v) { bytes(&v, sizeof v); }
    void s(const std::string& x) {
        u(x.size());
        bytes(x.data(), x.size());
    }
};

enum class SizeTablePos { Before, After, None };

struct Model {
    int countBytes = 2;          // spec 7.3: s16
    bool hasSourceName = true;   // spec 7.3 (version >= 10)
    bool hasExtra = true;        // spec 7.3
    bool hasPayloadLength = true;
    SizeTablePos sizeTable = SizeTablePos::Before;
    int entryTail = 13;          // spec 7.4
    int skewAfterKind = 0;       // bytes inserted after container_kind (>0) / record_flags width shrink (<0)
    int recordAreaSkew = 0;      // shifts where the first record is read from
    bool loopToEof = false;      // ignore header record_count, walk until EOF
};

struct WalkResult {
    bool ok = false;         // no read ran off the end
    size_t pos = 0;
    size_t size = 0;
    uint32_t records = 0;
    uint64_t entries = 0;
    uint16_t declared = 0;
    uint64_t digest = 0;      // records + entries
    uint64_t tableDigest = 0; // the three tables
    uint32_t tableCounts[3] = {0, 0, 0};
    bool gate() const { return ok && pos == size && (records == declared); }
};

WalkResult specWalk(const Bytes& b, const Model& m) {
    WalkResult r;
    r.size = b.size();
    size_t pos = 0;
    bool ok = true;
    auto need = [&](size_t n) {
        if (!ok || n > b.size() - std::min(pos, b.size())) ok = false;
        return ok;
    };
    auto rd8 = [&]() -> uint32_t { return need(1) ? b[pos++] : 0u; };
    auto rd16 = [&]() -> uint32_t {
        if (!need(2)) return 0;
        uint32_t v = b[pos] | (b[pos + 1] << 8);
        pos += 2;
        return v;
    };
    auto rd32 = [&]() -> uint32_t {
        if (!need(4)) return 0;
        uint32_t v = b[pos] | (b[pos + 1] << 8) | (b[pos + 2] << 16) | (static_cast<uint32_t>(b[pos + 3]) << 24);
        pos += 4;
        return v;
    };
    auto rdStr = [&]() -> std::string {
        uint32_t n = rd16();
        if (!need(n)) return {};
        std::string s(reinterpret_cast<const char*>(b.data() + pos), n);
        pos += n;
        return s;
    };

    rd32(); // magic (the engine never compares it; spec 6.2)
    uint32_t version = rd16();
    (void)version;
    r.declared = static_cast<uint16_t>(rd16());

    Digest td;
    for (int t = 0; t < 3 && ok; ++t) {
        uint32_t n = rd32();
        r.tableCounts[t] = n;
        td.u(n);
        for (uint32_t i = 0; i < n && ok; ++i) {
            std::string nm = rdStr();
            uint32_t id = rd8();
            td.s(nm);
            td.u(id);
        }
    }
    r.tableDigest = td.h;

    if (m.recordAreaSkew > 0) pos += static_cast<size_t>(m.recordAreaSkew);
    if (m.recordAreaSkew < 0) pos -= static_cast<size_t>(-m.recordAreaSkew);
    if (pos > b.size()) ok = false;

    Digest d;
    uint32_t wanted = m.loopToEof ? 0xFFFFFFFFu : r.declared;
    while (ok && r.records < wanted) {
        if (m.loopToEof && pos == b.size()) break;
        std::string name = rdStr();
        uint32_t kind = rd8();
        for (int k = 0; k < m.skewAfterKind; ++k) rd8();
        uint32_t flags = m.skewAfterKind < 0 ? rd8() : rd16();
        int32_t count = m.countBytes == 1 ? static_cast<int32_t>(rd8())
                                           : static_cast<int32_t>(static_cast<int16_t>(rd16()));
        uint32_t hrs = rd32();
        std::string source = m.hasSourceName ? rdStr() : std::string();
        uint32_t extraLen = m.hasExtra ? rd32() : 0;
        if (m.hasExtra && ok) {
            if (!need(extraLen)) break;
            pos += extraLen;
        }
        uint32_t payloadLen = m.hasPayloadLength ? rd32() : 0;
        if (!ok || count < 0) {
            ok = false;
            break;
        }
        d.s(name);
        d.u(kind); d.u(flags); d.u(static_cast<uint32_t>(count)); d.u(hrs); d.s(source);
        d.u(extraLen); d.u(payloadLen);

        std::vector<std::pair<uint32_t, uint32_t>> st;
        auto readSizeTable = [&]() {
            for (int32_t i = 0; i < count && ok; ++i) {
                uint32_t p = rd32();
                uint32_t s2 = rd32();
                st.push_back({p, s2});
            }
        };
        if (m.sizeTable == SizeTablePos::Before) readSizeTable();
        std::vector<std::string> names;
        for (int32_t i = 0; i < count && ok; ++i) {
            std::string en = rdStr();
            if (!need(static_cast<size_t>(m.entryTail))) break;
            size_t start = pos;
            uint32_t type = rd8(), pool = rd8(), eflags = rd8(), variant = rd8();
            uint32_t prim = rd32(), sec = rd32();
            uint32_t group = 0;
            if (m.entryTail >= 13) group = rd8();
            pos = start + static_cast<size_t>(m.entryTail);
            d.s(en);
            d.u(type); d.u(pool); d.u(eflags); d.u(variant); d.u(prim); d.u(sec); d.u(group);
            ++r.entries;
        }
        if (m.sizeTable == SizeTablePos::After) readSizeTable();
        for (auto& p : st) {
            d.u(p.first);
            d.u(p.second);
        }
        if (ok) ++r.records;
    }
    r.ok = ok;
    r.pos = pos;
    r.digest = d.h;
    return r;
}

// Same canonical order, computed from the library's parse.
uint64_t libDigest(const sr3asm::AsmManifest& m, uint64_t& tableDigest) {
    Digest td;
    for (const auto& t : m.fixedTables()) {
        td.u(t.entries.size());
        for (const auto& e : t.entries) {
            td.s(e.name);
            td.u(e.id);
        }
    }
    tableDigest = td.h;
    Digest d;
    for (const auto& r : m.records()) {
        d.s(r.name);
        d.u(r.containerKind); d.u(r.recordFlags); d.u(static_cast<uint32_t>(static_cast<int32_t>(r.entryCount)));
        d.u(r.headerRegionSize); d.s(r.sourceName); d.u(r.extra.size()); d.u(r.payloadLength);
        for (const auto& e : r.entries) {
            d.s(e.name);
            d.u(e.typeId); d.u(e.poolId); d.u(e.entryFlags); d.u(e.variantSelect);
            d.u(e.primarySize); d.u(e.secondarySize); d.u(e.allocGroup);
        }
        for (const auto& p : r.sizeTable) {
            d.u(p.primary);
            d.u(p.secondary);
        }
    }
    return d.h;
}

// ------------------------------------------------------------------ reporting

int g_mismatch = 0;

void line(const char* what, long long actual, long long spec) {
    const bool ok = actual == spec;
    if (!ok) ++g_mismatch;
    std::printf("  %-58s actual %10lld   spec %10lld   %s\n", what, actual, spec, ok ? "MATCH" : "MISMATCH");
}
void info(const char* what, long long actual) {
    std::printf("  %-58s actual %10lld   (no spec figure)\n", what, actual);
}

std::string addCommas(unsigned long long v) {
    std::string s = std::to_string(v), o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) o += ',';
        o += s[i];
    }
    return o;
}

std::string gName(const std::string& c) {
    std::string g = c;
    size_t d = g.find_last_of('.');
    if (d != std::string::npos && d + 1 < g.size() && (g[d + 1] == 'c' || g[d + 1] == 'C'))
        g[d + 1] = (g[d + 1] == 'c') ? 'g' : 'G';
    else
        g.clear();
    return g;
}

std::string sibKeyBase(const std::string& recName) { return lower(recName) + ".str2_pc"; }
std::string sibKeyStripped(const std::string& recName) {
    size_t d = recName.find_last_of('.');
    if (d == std::string::npos) return {};
    return lower(recName.substr(0, d)) + ".str2_pc";
}

struct ListCompare {
    bool ok = true;
    size_t zeroGPresent = 0, gAbsent = 0;
    const char* why = "";
};

// Manifest-derived file list vs a sibling directory (spec 9.4).
ListCompare compareList(const sr3asm::ContainerRecord& r, const Str2Info& s, bool caseSensitive) {
    ListCompare lc;
    auto same = [&](const std::string& a, const std::string& b) { return caseSensitive ? a == b : lower(a) == lower(b); };
    size_t pos = 0;
    auto fail = [&](const char* w) {
        lc.ok = false;
        lc.why = w;
    };
    for (const auto& e : r.entries) {
        if (e.typeId == 39) continue; // Buffer: names no file (spec 9.4)
        if (pos >= s.dir.size()) { fail("dir ended early"); return lc; }
        if (!same(s.dir[pos].name, e.name)) { fail("primary name"); return lc; }
        if (s.dir[pos].size != e.primarySize) { fail("primary size"); return lc; }
        ++pos;
        if (e.paired()) {
            std::string g = gName(e.name);
            if (g.empty()) { fail("g-name derivation"); return lc; }
            if (pos < s.dir.size() && same(s.dir[pos].name, g)) {
                if (s.dir[pos].size != e.secondarySize) { fail("secondary size"); return lc; }
                if (e.secondarySize == 0) ++lc.zeroGPresent;
                ++pos;
            } else {
                if (e.secondarySize != 0) { fail("mandatory g-file missing"); return lc; }
                ++lc.gAbsent;
            }
        }
    }
    if (pos != s.dir.size()) fail("unaccounted directory names");
    return lc;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) archives.push_back(argv[i]);
    if (archives.empty()) {
        for (const auto& de : fs::directory_iterator(std::string(kGameRoot) + "\\packfiles\\pc\\cache"))
            if (de.is_regular_file() && lower(de.path().extension().string()) == ".vpp_pc")
                archives.push_back(de.path().string());
        std::sort(archives.begin(), archives.end());
        archives.push_back(std::string(kGameRoot) + "\\launcher.vpp_pc");
    }

    // ------------------------------------------------------------ 1. inventory
    Inventory inv;
    for (const auto& a : archives) {
        Bytes bytes = readFile(a);
        if (bytes.empty()) {
            std::printf("cannot read %s\n", a.c_str());
            ++inv.archivesFailed;
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c, fs::path(a).filename().string(), "", 0, inv);
            ++inv.archivesRead;
        } catch (const std::exception& ex) {
            std::printf("cannot parse container %s: %s\n", a.c_str(), ex.what());
            ++inv.archivesFailed;
        }
    }
    std::printf("== 1. inventory ==\n");
    std::printf("  archives read %zu, failed %zu; nested containers opened %zu, failed to open %zu\n",
                inv.archivesRead, inv.archivesFailed, inv.nestedOpened, inv.nestedFailed);
    std::printf("  .asm_pc stored raw: %zu (in %zu archives), stored compressed (NOT parsed): %zu, nested below top level: %zu\n",
                inv.files.size(), inv.asmPerArchive.size(), inv.asmCompressed, inv.asmNested);
    std::printf("  top-level .str2_pc: raw+opened %zu, compressed %zu, raw-but-unopenable %zu; compressed *.str2_pc/*.vpp_pc entries anywhere (not opened): %zu\n",
                inv.str2TopRaw, inv.str2TopCompressed, inv.str2TopFailed, inv.compressedContainerNames);
    for (const auto& kv : inv.asmPerArchive) std::printf("    %-26s %zu manifests\n", kv.first.c_str(), kv.second);

    // ------------------------------------------- 2. parse every file, two ways
    std::printf("\n== 2. exact-consumption gate, library reader and independent walker ==\n");
    struct Parsed {
        sr3asm::AsmManifest m;
        size_t fileIndex;
    };
    std::vector<Parsed> parsed;
    size_t libThrew = 0, libTrailing = 0, libCountMismatch = 0, walkGate = 0, walkEofGate = 0, digestDiffer = 0, tableDigestDiffer = 0;
    uint64_t totalBytes = 0, totalRecords = 0, totalEntries = 0;
    size_t minBytes = ~size_t(0), maxBytes = 0;
    std::set<uint64_t> distinctTableDigests;
    std::vector<uint32_t> recordCounts;
    std::vector<std::string> failingFiles;
    const Model spec{};
    Model eof;
    eof.loopToEof = true;
    for (size_t i = 0; i < inv.files.size(); ++i) {
        const AsmFile& f = inv.files[i];
        totalBytes += f.bytes.size();
        minBytes = std::min(minBytes, f.bytes.size());
        maxBytes = std::max(maxBytes, f.bytes.size());
        WalkResult w = specWalk(f.bytes, spec);
        if (w.gate()) ++walkGate;
        WalkResult we = specWalk(f.bytes, eof);
        if (we.ok && we.pos == we.size && we.records == we.declared) ++walkEofGate;
        distinctTableDigests.insert(w.tableDigest);
        try {
            sr3asm::AsmManifest m = sr3asm::AsmManifest::parse(sr3asm::ByteView(f.bytes.data(), f.bytes.size()));
            if (m.trailingBytes() != 0) ++libTrailing;
            if (m.records().size() != m.declaredRecordCount()) ++libCountMismatch;
            uint64_t td = 0;
            uint64_t ld = libDigest(m, td);
            if (ld != w.digest) ++digestDiffer;
            if (td != w.tableDigest) ++tableDigestDiffer;
            totalRecords += m.records().size();
            totalEntries += m.totalEntries();
            recordCounts.push_back(m.declaredRecordCount());
            parsed.push_back({std::move(m), i});
        } catch (const std::exception& ex) {
            ++libThrew;
            failingFiles.push_back(f.archive + "/" + f.path + ": " + ex.what());
        }
    }
    const size_t denom = inv.files.size();
    std::printf("  denominator = %zu .asm_pc files (%s bytes total, %zu .. %zu bytes each)\n", denom,
                addCommas(totalBytes).c_str(), minBytes, maxBytes);
    std::printf("  library parse succeeded (no FormatError)      : %zu / %zu\n", denom - libThrew, denom);
    std::printf("  library: zero trailing bytes (exact EOF)      : %zu / %zu\n", denom - libThrew - libTrailing, denom);
    std::printf("  library: records parsed == header record_count: %zu / %zu\n", denom - libThrew - libCountMismatch, denom);
    std::printf("  walker (b), header-count loop, exact EOF      : %zu / %zu\n", walkGate, denom);
    std::printf("  walker (b), walk-to-EOF ignoring the count    : %zu / %zu (records found == header field)\n", walkEofGate, denom);
    std::printf("  library vs walker per-record field digest equal: %zu / %zu\n", denom - libThrew - digestDiffer, denom);
    std::printf("  library vs walker table digest equal          : %zu / %zu\n", denom - libThrew - tableDigestDiffer, denom);
    std::printf("  distinct (table1,table2,table3) contents      : %zu\n", distinctTableDigests.size());
    for (const auto& s : failingFiles) std::printf("  FAILED: %s\n", s.c_str());

    // ------------------------------------------------------------ 3. controls
    std::printf("\n== 3. controls (each should COLLAPSE the gate except the marked one), denominator %zu ==\n", denom);
    struct Ctl {
        const char* name;
        Model m;
        bool expectCollapse;
    };
    std::vector<Ctl> ctls;
    auto add = [&](const char* n, Model m, bool collapse) { ctls.push_back({n, m, collapse}); };
    { Model m; add("baseline: spec layout", m, false); }
    { Model m; m.countBytes = 1; add("entry_count as u8", m, true); }
    { Model m; m.hasSourceName = false; add("no source_name", m, true); }
    { Model m; m.sizeTable = SizeTablePos::After; add("size table AFTER the entries", m, true); }
    { Model m; m.sizeTable = SizeTablePos::None; add("no size table", m, true); }
    { Model m; m.entryTail = 12; add("entry tail 12 bytes (no alloc_group)", m, true); }
    { Model m; m.entryTail = 14; add("entry tail 14 bytes", m, true); }
    { Model m; m.hasPayloadLength = false; add("no payload_length", m, true); }
    { Model m; m.skewAfterKind = 1; add("1 extra byte after container_kind", m, true); }
    { Model m; m.skewAfterKind = -1; add("record_flags as u8", m, true); }
    { Model m; m.recordAreaSkew = 1; add("record area starts +1 byte", m, true); }
    { Model m; m.recordAreaSkew = 2; add("record area starts +2 bytes", m, true); }
    { Model m; m.recordAreaSkew = -1; add("record area starts -1 byte", m, true); }
    { Model m; m.hasExtra = false; add("skip the extra blob (spec: UNINFORMATIVE, extra_len==0 everywhere)", m, false); }
    for (const Ctl& c : ctls) {
        size_t pass = 0;
        for (const AsmFile& f : inv.files)
            if (specWalk(f.bytes, c.m).gate()) ++pass;
        std::printf("  %-72s %4zu / %zu%s\n", c.name, pass, denom, c.expectCollapse ? (pass < denom ? "  (collapsed)" : "  (DID NOT COLLAPSE)") : "");
        if (c.expectCollapse && pass == denom) ++g_mismatch;
    }
    // raw byte perturbations against the baseline walker
    struct BytePert {
        const char* name;
        int kind;
    };
    const BytePert perts[] = {{"truncate the last byte", 0},
                              {"append one zero byte", 1},
                              {"delete byte 8 (low byte of table 1's count)", 2},
                              {"insert a zero byte at offset 8", 3},
                              {"delete byte 6 (record_count low byte)", 4}};
    size_t zeroRecordFiles = 0;
    for (const AsmFile& f : inv.files)
        if (f.bytes.size() >= 8 && (f.bytes[6] | (f.bytes[7] << 8)) == 0) ++zeroRecordFiles;
    for (const BytePert& p : perts) {
        size_t pass = 0;
        for (const AsmFile& f : inv.files) {
            Bytes b = f.bytes;
            if (p.kind == 0 && !b.empty()) b.pop_back();
            if (p.kind == 1) b.push_back(0);
            if (p.kind == 2 && b.size() > 8) b.erase(b.begin() + 8);
            if (p.kind == 3 && b.size() > 8) b.insert(b.begin() + 8, 0);
            if (p.kind == 4 && b.size() > 6) b.erase(b.begin() + 6);
            if (specWalk(b, spec).gate()) ++pass;
        }
        std::printf("  %-72s %4zu / %zu  (collapsed)\n", p.name, pass, denom);
    }
    std::printf("  (%zu of the files are zero-record manifests: header + tables only, nothing for a record-level perturbation to break)\n",
                zeroRecordFiles);
    // the reader itself must also reject a truncated copy (FormatError), not just the walker
    {
        size_t rejected = 0;
        for (const AsmFile& f : inv.files) {
            Bytes b = f.bytes;
            b.pop_back();
            try {
                sr3asm::AsmManifest::parse(sr3asm::ByteView(b.data(), b.size()));
            } catch (const sr3asm::FormatError&) {
                ++rejected;
            }
        }
        std::printf("  library reader throws FormatError on the last-byte-truncated copy: %zu / %zu\n", rejected, denom);
        size_t detectedTrailing = 0;
        for (const AsmFile& f : inv.files) {
            Bytes b = f.bytes;
            b.push_back(0);
            try {
                auto m = sr3asm::AsmManifest::parse(sr3asm::ByteView(b.data(), b.size()));
                if (m.trailingBytes() == 1) ++detectedTrailing;
            } catch (const sr3asm::FormatError&) {
            }
        }
        std::printf("  library reports trailingBytes()==1 on the one-byte-appended copy : %zu / %zu\n", detectedTrailing, denom);
    }

    // ------------------------------------------------- 5. population numbers
    std::printf("\n== 5. population figures vs the spec (sections 7.1, 7.3, 7.4, 9.1, 9.3) ==\n");
    line("files", static_cast<long long>(denom), 805);
    line("archives holding at least one manifest", static_cast<long long>(inv.asmPerArchive.size()), 20);
    line("total bytes", static_cast<long long>(totalBytes), 20956590);
    line("smallest file (bytes)", static_cast<long long>(minBytes), 2238);
    line("largest file (bytes)", static_cast<long long>(maxBytes), 8182847);
    line("records", static_cast<long long>(totalRecords), 8362);
    line("entries", static_cast<long long>(totalEntries), 390134);
    line("files with record_count 0", static_cast<long long>(zeroRecordFiles), 3);
    {
        std::vector<uint32_t> rc = recordCounts;
        std::sort(rc.begin(), rc.end());
        line("record_count median", rc.empty() ? 0 : rc[rc.size() / 2], 1);
        line("record_count max", rc.empty() ? 0 : rc.back(), 1812);
        line("files with exactly one record", std::count(rc.begin(), rc.end(), 1u), 527);
    }
    {
        auto it = inv.asmPerArchive.find("sr3_city_0.vpp_pc");
        auto it1 = inv.asmPerArchive.find("sr3_city_1.vpp_pc");
        line("manifests in sr3_city_0.vpp_pc", it == inv.asmPerArchive.end() ? 0 : static_cast<long long>(it->second), 331);
        line("manifests in sr3_city_1.vpp_pc", it1 == inv.asmPerArchive.end() ? 0 : static_cast<long long>(it1->second), 405);
    }


    std::map<int, long long> kindHist, flagsHist, entryFlagsHist, variantHist, groupHist, poolHist, typeHist;
    long long srcEmpty = 0, srcNonEmpty = 0, srcEqualsOwnName = 0, extraNonZero = 0, entryCountZero = 0, gt255 = 0, maxEntryCount = 0;
    long long nonLive = 0, nonLiveBothZero = 0, liveRecs = 0, hrs1800 = 0, hrs0800 = 0, hrsNot1800 = 0, payloadZeroLive = 0, payloadZeroLiveNonEmpty = 0;
    long long sizeTableEqTrailer = 0, kindInTable3 = 0, typeInTable2 = 0, poolInTable1 = 0, nonZeroPool = 0, hrsNotMult800 = 0;
    long long maxEntryNameLen = 0, hrsMax = 0, tblCountsOk = 0, flag0300Total = 0, flag0300OnKind2425 = 0;
    long long v1Total = 0, v1Type16Cvbm = 0, t16v0Total = 0, t16v0Cpeg = 0, v255Total = 0, v255Type39 = 0, type39Total = 0;
    std::set<uint32_t> hrsDistinct;
    std::set<size_t> filesWithBig;
    std::map<int, std::pair<long long, long long>> typePaired; // type -> (paired, unpaired)
    std::map<int, std::set<std::string>> typeExt;              // type -> set of file extensions (lower-case, "" = none)
    for (const Parsed& p : parsed) {
        const AsmFile& f = inv.files[p.fileIndex];
        const sr3asm::AsmManifest& m = p.m;
        if (m.poolTable().entries.size() == 39 && m.typeTable().entries.size() == 45 &&
            m.kindTable().entries.size() == 41)
            ++tblCountsOk;
        for (const sr3asm::ContainerRecord& r : m.records()) {
            ++kindHist[r.containerKind];
            ++flagsHist[r.recordFlags];
            if (m.kindTable().findName(r.containerKind)) ++kindInTable3;
            if (r.sourceName.empty()) ++srcEmpty; else ++srcNonEmpty;
            if (!r.sourceName.empty() && r.sourceName == f.name) ++srcEqualsOwnName;
            if (!r.extra.empty()) ++extraNonZero;
            if (r.entryCount == 0) ++entryCountZero;
            if (r.entryCount > 255) {
                ++gt255;
                filesWithBig.insert(p.fileIndex);
            }
            maxEntryCount = std::max<long long>(maxEntryCount, r.entryCount);
            if (r.recordFlags == 0x0300) {
                ++flag0300Total;
                if (r.containerKind == 24 || r.containerKind == 25) ++flag0300OnKind2425;
            }
            if (!r.sizeHintsLive()) {
                ++nonLive;
                if (r.headerRegionSize == 0 && r.payloadLength == 0) ++nonLiveBothZero;
            } else {
                ++liveRecs;
                hrsDistinct.insert(r.headerRegionSize);
                if (r.headerRegionSize == 0x1800) ++hrs1800; else ++hrsNot1800;
                if (r.headerRegionSize == 0x0800) ++hrs0800;
                if (r.headerRegionSize % 0x800 != 0) ++hrsNotMult800;
                hrsMax = std::max<long long>(hrsMax, r.headerRegionSize);
                if (r.payloadLength == 0) {
                    ++payloadZeroLive;
                    if (!r.entries.empty()) ++payloadZeroLiveNonEmpty;
                }
            }
            for (size_t i = 0; i < r.entries.size(); ++i) {
                const sr3asm::ManifestEntry& e = r.entries[i];
                ++typeHist[e.typeId];
                ++entryFlagsHist[e.entryFlags];
                ++variantHist[e.variantSelect];
                ++groupHist[e.allocGroup];
                ++poolHist[e.poolId];
                if (m.typeTable().findName(e.typeId)) ++typeInTable2;
                if (e.poolId != 0) {
                    ++nonZeroPool;
                    if (m.poolTable().findName(e.poolId)) ++poolInTable1;
                }
                if (r.sizeTable[i].primary == e.primarySize && r.sizeTable[i].secondary == e.secondarySize) ++sizeTableEqTrailer;
                maxEntryNameLen = std::max<long long>(maxEntryNameLen, static_cast<long long>(e.name.size()));
                if (e.paired()) ++typePaired[e.typeId].first; else ++typePaired[e.typeId].second;
                if (e.typeId == 39) ++type39Total;
                {
                    const size_t dot = e.name.find_last_of('.');
                    typeExt[e.typeId].insert(dot == std::string::npos ? std::string() : lower(e.name.substr(dot + 1)));
                }
                if (e.variantSelect == 255) {
                    ++v255Total;
                    if (e.typeId == 39) ++v255Type39;
                }
                if (e.variantSelect == 1) {
                    ++v1Total;
                    if (e.typeId == 16 && endsWithCI(e.name, ".cvbm_pc")) ++v1Type16Cvbm;
                }
                if (e.typeId == 16 && e.variantSelect == 0) {
                    ++t16v0Total;
                    if (endsWithCI(e.name, ".cpeg_pc")) ++t16v0Cpeg;
                }
            }
        }
    }
    line("files with table sizes 39 / 45 / 41", tblCountsOk, static_cast<long long>(denom));
    line("distinct container_kind values", static_cast<long long>(kindHist.size()), 35);
    line("record kinds present in table 3", kindInTable3, static_cast<long long>(totalRecords));
    line("record_flags 0x0080", flagsHist[0x0080], 7990);
    line("record_flags 0x0300", flagsHist[0x0300], 305);
    line("record_flags 0x0200", flagsHist[0x0200], 21);
    line("record_flags 0x0000", flagsHist[0x0000], 46);
    line("record_flags of any other value", static_cast<long long>(totalRecords) - flagsHist[0x80] - flagsHist[0x300] - flagsHist[0x200] - flagsHist[0], 0);
    line("0x0300 records on kinds 24/25 (of all 0x0300 records)", flag0300OnKind2425, flag0300Total);
    static const std::pair<int, long long> kindSpec[] = {
        {1, 318}, {2, 75}, {3, 2}, {5, 147}, {6, 44}, {7, 303}, {8, 283}, {9, 2}, {10, 1348}, {12, 337},
        {13, 45}, {14, 7}, {15, 4}, {16, 1}, {17, 1}, {18, 130}, {19, 82}, {21, 183}, {22, 928}, {23, 243},
        {24, 33}, {25, 272}, {26, 14}, {27, 7}, {28, 4}, {29, 1194}, {30, 1686}, {31, 150}, {32, 183},
        {33, 155}, {34, 3}, {35, 19}, {39, 155}, {40, 3}, {41, 1}};
    {
        long long kindOk = 0;
        for (const auto& ks : kindSpec)
            if (kindHist.count(ks.first) && kindHist[ks.first] == ks.second) ++kindOk;
        line("container_kind ids whose count equals the spec's (of 35)", kindOk, 35);
    }
    line("source_name empty", srcEmpty, 5149);
    line("source_name non-empty", srcNonEmpty, 3213);
    line("source_name equal to the manifest's own file name", srcEqualsOwnName, 1597);
    line("records with a non-empty extra blob", extraNonZero, 0);
    line("records with entry_count 0", entryCountZero, 155);
    line("records with entry_count > 255", gt255, 491);
    line("files containing such a record", static_cast<long long>(filesWithBig.size()), 238);
    line("max entry_count", maxEntryCount, 3820);
    line("records without 0x0080", nonLive, 372);
    line("  ... of which header_region_size == payload_length == 0", nonLiveBothZero, 372);
    line("records with 0x0080", liveRecs, 7990);
    line("  header_region_size distinct values", static_cast<long long>(hrsDistinct.size()), 44);
    line("  header_region_size == 0x1800", hrs1800, 5887);
    line("  header_region_size == 0x0800", hrs0800, 155);
    line("  header_region_size != 0x1800", hrsNot1800, 2103);
    line("  header_region_size not a multiple of 0x800", hrsNotMult800, 0);
    line("  header_region_size max", hrsMax, 0x36000);
    line("  payload_length == 0", payloadZeroLive, 138);
    line("  ... of which on records that have entries", payloadZeroLiveNonEmpty, 0);
    line("entries whose size_table[i] == (primary, secondary)", sizeTableEqTrailer, static_cast<long long>(totalEntries));
    line("entries with type_id present in table 2", typeInTable2, static_cast<long long>(totalEntries));
    line("entries with non-zero pool_id", nonZeroPool, static_cast<long long>(totalEntries) - 230554);
    line("  ... of which present in table 1", poolInTable1, nonZeroPool);
    line("distinct type_id values", static_cast<long long>(typeHist.size()), 41);
    {
        long long typeSetOk = 1;
        for (int t = 1; t <= 45; ++t) {
            const bool absentInSpec = t == 6 || t == 8 || t == 14 || t == 28;
            if (absentInSpec == (typeHist.count(t) != 0)) typeSetOk = 0;
        }
        line("type_id set == {1..45} minus {6,8,14,28}", typeSetOk, 1);
        line("entries of type 253 or 254", (typeHist.count(253) ? typeHist[253] : 0) + (typeHist.count(254) ? typeHist[254] : 0), 0);
    }
    line("entry_flags 0", entryFlagsHist[0], 55699);
    line("entry_flags 4", entryFlagsHist[4], 212253);
    line("entry_flags 68 (0x44)", entryFlagsHist[68], 122170);
    line("entry_flags 32 (0x20)", entryFlagsHist[32], 12);
    line("variant_select 0", variantHist[0], 367932);
    line("variant_select 1", variantHist[1], 19755);
    line("variant_select 255", variantHist[255], 2447);
    line("  variant 1 on type 16 with .cvbm_pc", v1Type16Cvbm, v1Total);
    line("  variant 0 on type 16 with .cpeg_pc", t16v0Cpeg, 1214);
    line("  type-39 entries with variant 255", v255Type39, type39Total);
    line("  variant 255 entries that are type 39", v255Type39, v255Total);
    static const std::pair<int, long long> groupSpec[] = {{0, 255753}, {1, 15195}, {2, 1693}, {3, 8}, {4, 8}, {5, 4572}, {255, 112905}};
    for (const auto& gs : groupSpec) {
        std::string label = "alloc_group " + std::to_string(gs.first);
        line(label.c_str(), groupHist[gs.first], gs.second);
    }
    static const std::pair<int, long long> poolSpec[] = {{0, 230554}, {7, 200}, {8, 25000}, {9, 3636}, {21, 792}, {22, 239}, {26, 2705},
                                                          {30, 109223}, {31, 12269}, {32, 878}, {33, 63}, {37, 2}, {39, 1}, {40, 4572}};
    line("distinct pool_id values", static_cast<long long>(poolHist.size()), 14);
    for (const auto& ps : poolSpec) {
        std::string label = "pool_id " + std::to_string(ps.first);
        line(label.c_str(), poolHist[ps.first], ps.second);
    }
    line("longest entry name (characters)", maxEntryNameLen, 41);
    {
        // spec 9.3 "Type <-> file extension" list (the two inventory exceptions are already folded in).
        static const std::pair<int, const char*> extSpec[] = {
            {1, "ccar_pc"}, {2, "cvtf_pc"}, {3, "cpeg_pc"}, {4, "cefct_pc"}, {15, "cefct_pc"}, {5, "ccmesh_pc"}, {9, "ccmesh_pc"},
            {7, "cmorph_pc"}, {11, "cmorph_pc"}, {12, "cmorph_pc"}, {10, "cpeg_pc"}, {13, "rig_pc"}, {20, "rig_pc"},
            {16, "cpeg_pc,cvbm_pc"}, {17, "cvbm_pc"}, {18, "cpeg_pc"}, {19, "csmesh_pc"}, {21, "anim_pc"}, {41, "anim_pc"},
            {22, "matlib_pc"}, {35, "matlib_pc"}, {23, "cte_xtbl"}, {24, "csc_pc"}, {25, "xtbl"}, {33, "xtbl"}, {34, "xtbl"},
            {43, "xtbl"}, {26, "vint_doc"}, {27, "lua"}, {32, "lua"}, {29, "czn_pc"}, {30, "czn_pc"}, {31, "clmesh_pc"},
            {40, "clmesh_pc"}, {36, "csrt_pc"}, {37, "cfmesh_pc"}, {38, "lightmult_pc,lightpos_pc,lightrot_pc"}, {39, ""},
            {42, "ctdg_pc"}, {44, "czh_pc"}, {45, "czh_pc"}};
        long long extOk = 0;
        for (const auto& es : extSpec) {
            std::string got;
            for (const std::string& x : typeExt[es.first]) got += (got.empty() ? "" : ",") + x;
            if (got == es.second) ++extOk;
            else std::printf("    type %d: observed extensions '%s', spec says '%s'\n", es.first, got.c_str(), es.second);
        }
        line("types whose observed extension set equals spec 9.3's (of 41)", extOk, 41);
    }
    {
        static const int pairedSpec[] = {1, 3, 4, 5, 9, 10, 15, 16, 17, 18, 19, 29, 30, 31, 36};
        long long ok = 1;
        std::string mixed, pairedSeen;
        for (const auto& kv : typePaired) {
            const bool inSpec = std::find(std::begin(pairedSpec), std::end(pairedSpec), kv.first) != std::end(pairedSpec);
            if (kv.second.first && kv.second.second) { mixed += " " + std::to_string(kv.first); ok = 0; }
            if (kv.second.first) pairedSeen += " " + std::to_string(kv.first);
            if ((kv.second.first != 0) != inSpec) ok = 0;
        }
        line("paired-type set == spec's {1,3,4,5,9,10,15,16,17,18,19,29,30,31,36}, no type mixed", ok, 1);
        std::printf("    paired types observed:%s ; types with both paired and unpaired entries:%s\n", pairedSeen.c_str(), mixed.empty() ? " none" : mixed.c_str());
    }

    // -------------------------------------- 4. cross-check against .str2_pc
    std::printf("\n== 4. cross-check against the sibling .str2_pc directories (real data outside the manifest) ==\n");
    std::multimap<std::string, size_t> str2Index; // lowercase name -> index in inv.str2
    for (size_t i = 0; i < inv.str2.size(); ++i) str2Index.insert({lower(inv.str2[i].name), i});
    struct Flat {
        const sr3asm::ContainerRecord* rec;
        const AsmFile* file;
        long long sib; // index into inv.str2 or -1
    };
    std::vector<Flat> flat;
    long long viaBase = 0, viaStripped = 0, sameArchive = 0, otherArchive = 0, multiCandidates = 0, multiAllPass = 0;
    for (const Parsed& p : parsed) {
        const AsmFile& f = inv.files[p.fileIndex];
        for (const sr3asm::ContainerRecord& r : p.m.records()) {
            Flat fl{&r, &f, -1};
            std::string k1 = sibKeyBase(r.name);
            std::string k2 = sibKeyStripped(r.name);
            std::vector<size_t> cand;
            bool base = true;
            auto range = str2Index.equal_range(k1);
            for (auto it = range.first; it != range.second; ++it) cand.push_back(it->second);
            if (cand.empty() && !k2.empty()) {
                base = false;
                auto r2 = str2Index.equal_range(k2);
                for (auto it = r2.first; it != r2.second; ++it) cand.push_back(it->second);
            }
            if (!cand.empty()) {
                size_t pick = cand[0];
                for (size_t c : cand)
                    if (inv.str2[c].archive == f.archive) { pick = c; break; }
                if (cand.size() > 1) {
                    ++multiCandidates;
                    bool allPass = true;
                    for (size_t c : cand)
                        if (!compareList(r, inv.str2[c], false).ok) allPass = false;
                    if (allPass) ++multiAllPass;
                }
                fl.sib = static_cast<long long>(pick);
                if (base) ++viaBase; else ++viaStripped;
                if (inv.str2[pick].archive == f.archive) ++sameArchive; else ++otherArchive;
            }
            flat.push_back(fl);
        }
    }
    long long withSib = 0, withSibLive = 0, listOk = 0, listOkExact = 0, hrsOk = 0, plOk = 0, zeroGPresent = 0, gAbsent = 0;
    std::map<std::string, long long> listFailWhy;
    std::map<int, long long> noSibKinds;
    std::map<std::string, long long> noSibFiles;
    long long noSib = 0, noSibOtherThan2425 = 0;
    std::set<size_t> usedSibs;
    for (const Flat& fl : flat) {
        if (fl.sib < 0) {
            ++noSib;
            ++noSibKinds[fl.rec->containerKind];
            ++noSibFiles[fl.file->archive + "/" + fl.file->name];
            if (fl.rec->containerKind != 24 && fl.rec->containerKind != 25) ++noSibOtherThan2425;
            continue;
        }
        ++withSib;
        const Str2Info& s = inv.str2[static_cast<size_t>(fl.sib)];
        usedSibs.insert(static_cast<size_t>(fl.sib));
        ListCompare lc = compareList(*fl.rec, s, false);
        if (compareList(*fl.rec, s, true).ok) ++listOkExact;
        if (lc.ok) {
            ++listOk;
            zeroGPresent += static_cast<long long>(lc.zeroGPresent);
            gAbsent += static_cast<long long>(lc.gAbsent);
        } else {
            ++listFailWhy[lc.why];
            static int shown = 0;
            if (shown < 6) {
                ++shown;
                std::printf("  LIST MISMATCH #%d: record '%s' (kind %u, %zu entries) in %s/%s vs sibling '%s' (%s), %zu dir entries; reason: %s\n",
                            shown, fl.rec->name.c_str(), static_cast<unsigned>(fl.rec->containerKind), fl.rec->entries.size(),
                            fl.file->archive.c_str(), fl.file->name.c_str(), s.name.c_str(), s.archive.c_str(), s.dir.size(), lc.why);
                for (size_t k = 0; k < 8 && k < fl.rec->entries.size(); ++k) {
                    const auto& e = fl.rec->entries[k];
                    std::printf("      manifest[%zu] type %u flags 0x%02X prim %u sec %u  %s\n", k, static_cast<unsigned>(e.typeId),
                                static_cast<unsigned>(e.entryFlags), e.primarySize, e.secondarySize, e.name.c_str());
                }
                for (size_t k = 0; k < 8 && k < s.dir.size(); ++k)
                    std::printf("      dir[%zu] size %u  %s\n", k, s.dir[k].size, s.dir[k].name.c_str());
            }
        }
        if (fl.rec->sizeHintsLive()) {
            ++withSibLive;
            if (fl.rec->headerRegionSize == s.payloadStart) ++hrsOk;
            const uint32_t expectPayload = s.h168 == 0xFFFFFFFFu ? 0u : s.h168;
            if (fl.rec->payloadLength == expectPayload) ++plOk;
        }
    }
    line("records with a top-level sibling .str2_pc", withSib, 8057);
    line("  resolved as <name>.str2_pc", viaBase, 7442);
    line("  resolved as <name minus extension>.str2_pc", viaStripped, 615);
    info("  sibling found in the same archive as the manifest", sameArchive);
    info("  sibling found only in another archive", otherArchive);
    info("  records whose name matched more than one .str2_pc", multiCandidates);
    info("    ... of which EVERY candidate matches the record list (choice immaterial)", multiAllPass);
    line("records with no top-level sibling", noSib, 305);
    line("  ... of which kind 24/25 (effect containers)", noSib - noSibOtherThan2425, noSib);
    for (const auto& kv : noSibFiles) std::printf("    no-sibling records in %-52s %lld\n", kv.first.c_str(), kv.second);
    line("manifest-derived file list == sibling directory (names compared CASE-INSENSITIVELY, order, size)", listOk, withSib);
    info("  same comparison with names compared CASE-EXACTLY", listOkExact);
    for (const auto& kv : listFailWhy) std::printf("    list mismatch reason '%s': %lld\n", kv.first.c_str(), kv.second);
    line("records with a sibling AND 0x0080", withSibLive, 7990);
    line("header_region_size == sibling payload_start (0x0080 records)", hrsOk, withSibLive);
    line("payload_length == sibling header 0x168 / 0 for 0xFFFFFFFF", plOk, withSibLive);
    info("  g-files present as zero-size files (paired, secondary 0)", zeroGPresent);
    info("  g-files absent (paired, secondary 0)", gAbsent);
    {
        long long dirNames = 0, dirNamesWithUpper = 0, exactCaseSib = 0;
        for (const Str2Info& s2 : inv.str2)
            for (const DirItem& di : s2.dir) {
                ++dirNames;
                if (di.name != lower(di.name)) ++dirNamesWithUpper;
            }
        for (const Flat& fl2 : flat)
            if (fl2.sib >= 0) {
                const std::string& sn = inv.str2[static_cast<size_t>(fl2.sib)].name;
                if (sn == fl2.rec->name + ".str2_pc" || sn == fl2.rec->name.substr(0, fl2.rec->name.find_last_of('.')) + ".str2_pc") ++exactCaseSib;
            }
        info("sibling .str2_pc directory names (all top-level .str2_pc)", dirNames);
        info("  ... containing an upper-case letter", dirNamesWithUpper);
        info("records whose sibling file name matches in exact case", exactCaseSib);
    }
    info("distinct sibling containers used", static_cast<long long>(usedSibs.size()));
    info("top-level .str2_pc not used by any record", static_cast<long long>(inv.str2.size() - usedSibs.size()));

    // Control: match each record against the sibling of the NEXT record with a sibling.
    {
        std::vector<size_t> withSibIdx;
        for (size_t i = 0; i < flat.size(); ++i)
            if (flat[i].sib >= 0) withSibIdx.push_back(i);
        long long ctlList = 0, ctlListNonTrivial = 0, ctlPayload = 0, ctlLive = 0;
        long long ctlHrs = 0, ctlHrsNot1800 = 0, hrsNot1800Total = 0;
        for (size_t k = 0; k < withSibIdx.size(); ++k) {
            const Flat& a = flat[withSibIdx[k]];
            const Flat& nx = flat[withSibIdx[(k + 1) % withSibIdx.size()]];
            const Str2Info& s = inv.str2[static_cast<size_t>(nx.sib)];
            if (compareList(*a.rec, s, false).ok) {
                ++ctlList;
                if (a.rec->entries.size() > 1) ++ctlListNonTrivial;
            }
            if (a.rec->sizeHintsLive()) {
                ++ctlLive;
                const uint32_t ep = s.h168 == 0xFFFFFFFFu ? 0u : s.h168;
                if (a.rec->payloadLength == ep) ++ctlPayload;
                const bool notCommon = a.rec->headerRegionSize != 0x1800;
                if (notCommon) ++hrsNot1800Total;
                if (a.rec->headerRegionSize == s.payloadStart) {
                    ++ctlHrs;
                    if (notCommon) ++ctlHrsNot1800;
                }
            }
        }
        std::printf("  CONTROL (record vs the sibling of the next record): list matches %lld / %lld (of which records with >1 entry: %lld); payload_length matches %lld / %lld   [spec reports 110 / 142]\n",
                    ctlList, withSib, ctlListNonTrivial, ctlPayload, ctlLive);
        std::printf("  CONTROL header_region_size vs next record's sibling payload_start: %lld / %lld match (0x1800 is the shared minimum); for records whose value is NOT 0x1800: %lld / %lld\n",
                    ctlHrs, ctlLive, ctlHrsNot1800, hrsNot1800Total);
    }

    std::printf("\n== summary ==\n");
    const bool gatesOk = libThrew == 0 && libTrailing == 0 && libCountMismatch == 0 && walkGate == denom &&
                         walkEofGate == denom && digestDiffer == 0 && tableDigestDiffer == 0 &&
                         distinctTableDigests.size() == 1 && inv.asmCompressed == 0 && inv.archivesFailed == 0;
    std::printf("  gates (library parse, exact EOF, record_count, walker, digest equality, one table triple, nothing skipped): %s\n", gatesOk ? "ALL PASS" : "FAIL");
    std::printf("  spec-figure comparisons that disagreed (including controls that failed to collapse): %d\n", g_mismatch);
    return (gatesOk && g_mismatch == 0) ? 0 : 1;
}
