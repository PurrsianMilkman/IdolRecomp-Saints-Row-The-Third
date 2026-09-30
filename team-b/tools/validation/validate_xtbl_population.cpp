// Population gates for the sr3xtbl foundation (include/sr3xtbl/xtbl.h) over EVERY
// `.xtbl` / `.cte_xtbl` in the shipped archives, plus the name-hash check against real
// save files.
//
// Usage: validate_xtbl_population [--saves <dir>]... <archive.vpp_pc> [...]
//   --saves <dir>  a folder holding sr3save_NN.sr3s_pc snapshots (read-only); repeatable.
//
// Every gate prints a denominator, and each one that could pass vacuously has a control
// that can fail (marked CONTROL). Exit status is non-zero if any gate fails.
//
// Gates
//   G1  survey: every entry whose name ends in "xtbl" (any extension) found in every
//       archive, recursively (raw AND compressed entries); the extension set; container
//       status of each. Anything that contains "xtbl" but does not end in it is counted.
//   G2  the parser accepts every decoded file (zero FormatError); files accepted with zero
//       warnings vs with warnings, per warning class with the file names.
//       CONTROL: an independent STRICT oracle (written here, shares no code with the
//       parser: case-exact close tags, no spaces in names, no control bytes, attributes
//       only as name="value") must reject exactly the files the parser warned about, and
//       for the files it accepts the element count and maximum depth must agree.
//   G3  shape: root element names, `Table` child of the root (any position / first
//       child / absent), row-element-name census (children of Table).
//   G4  text statistics that bear on the engine's number readers (leading/trailing
//       whitespace in leaf text, whitespace-only leaves, '+' exponents, 0x text, ...).
//   G5  parser controls on real files: (a) truncation inside a tag must throw FormatError
//       for every sampled file; (b) truncation right after a '>' must parse with
//       UnclosedAtEof; (c) an injected mismatched close tag must give exactly one
//       MismatchedCloseTag and an identical element count.
//   G6  the row-key hash: known answers, then the real evidence - the unlockable ids
//       stored in save snapshots (spec-save-format.md 6.4/9.8/10.6, spec-tables-progression.md
//       4.1) against NameHash(Name) of the rows of unlockables.xtbl / patch_unlockables.xtbl /
//       dlc{1,2,3}_unlockables.xtbl, and the cheat ids against cheats.xtbl text.
//       CONTROLS: the same test with a hash that does not lower-case, with the standard
//       (complemented) CRC-32, and with random 32-bit values.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

namespace {

using Bytes = std::vector<uint8_t>;

int g_fail = 0;
#define GATE(ok, ...)                                        \
    do {                                                     \
        const bool ok_ = (ok);                               \
        std::printf("  [%s] ", ok_ ? "PASS" : "FAIL");       \
        std::printf(__VA_ARGS__);                            \
        std::printf("\n");                                   \
        if (!ok_) ++g_fail;                                  \
    } while (0)

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
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool endsWithCi(const std::string& s, const char* x) {
    const size_t n = std::strlen(x);
    return s.size() >= n && lower(s.substr(s.size() - n)) == x;
}
std::string extOf(const std::string& n) {
    const size_t p = n.rfind('.');
    return p == std::string::npos ? std::string("<none>") : lower(n.substr(p));
}
std::string baseName(const std::string& p) {
    const size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// ---------------------------------------------------------------------------
// G1: collect
// ---------------------------------------------------------------------------
struct Item {
    std::string archive;  // chain of containers, e.g. cutscenes.vpp_pc/07_out.str2_pc
    std::string name;
    bool compressed = false;
    int status = 0;  // 0 = decoded / readable, else DecodeStatus (or -1 raw range error)
    Bytes data;
    uint64_t fnv = 0;
};

std::vector<Item> g_items;
long long g_entriesSeen = 0, g_containers = 0;
std::map<std::string, long long> g_extEndsXtbl;
std::map<std::string, long long> g_extContainsOnly;

uint64_t fnv64(const Bytes& b) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : b) { h ^= c; h *= 1099511628211ull; }
    return h;
}

void walk(vpp::ByteView bytes, const std::string& path) {
    vpp::Container c(bytes);
    ++g_containers;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        ++g_entriesSeen;
        const std::string ln = lower(e.name);
        const bool ends = ln.size() >= 4 && ln.compare(ln.size() - 4, 4, "xtbl") == 0;
        if (!ends) {
            if (ln.find("xtbl") != std::string::npos) ++g_extContainsOnly[extOf(e.name)];
            continue;
        }
        ++g_extEndsXtbl[extOf(e.name)];
        Item it;
        it.archive = path;
        it.name = e.name;
        it.compressed = e.payload.kind == vpp::PayloadKind::Compressed;
        if (it.compressed) {
            vpp::DecompressResult r = c.decompressEntry(i);
            it.status = static_cast<int>(r.status);
            if (r.status == vpp::DecodeStatus::Ok) it.data = std::move(r.data);
        } else {
            try {
                vpp::ByteView v = c.rawEntryBytes(i);
                it.data.assign(v.data(), v.data() + v.size());
            } catch (const std::exception&) {
                it.status = -1;
            }
        }
        it.fnv = fnv64(it.data);
        g_items.push_back(std::move(it));
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        if (endsWithCi(c.entries()[i].name, "xtbl")) continue;
        try {
            walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name);
        } catch (const std::exception&) {
        }
    }
}

// ---------------------------------------------------------------------------
// G2 control: an independent STRICT well-formedness oracle
// ---------------------------------------------------------------------------
struct Oracle {
    bool ok = false;
    size_t elements = 0, maxDepth = 0;
    std::string why;
};

Oracle strictOracle(const Bytes& d) {
    Oracle r;
    auto ws = [](uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    auto fail = [&](const std::string& w) { r.ok = false; r.why = w; return r; };
    const size_t n = d.size();
    size_t i = 0;
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;
    std::vector<std::string> st;
    bool sawRoot = false;
    while (i < n) {
        if (d[i] != '<') {
            const size_t s = i;
            while (i < n && d[i] != '<') ++i;
            if (st.empty()) {
                for (size_t k = s; k < i; ++k) if (!ws(d[k])) return fail("text outside root");
                continue;
            }
            for (size_t k = s; k < i; ++k) {
                const uint8_t c = d[k];
                if (c < 0x20 && c != '\t' && c != '\r' && c != '\n') return fail("control byte in text");
                if (c == '&') {
                    size_t j = k + 1;
                    while (j < i && d[j] != ';' && j < k + 12) ++j;
                    if (j >= i || d[j] != ';') return fail("bare &");
                    std::string ent(reinterpret_cast<const char*>(&d[k + 1]), j - k - 1);
                    bool okEnt = ent == "amp" || ent == "lt" || ent == "gt" || ent == "quot" || ent == "apos";
                    if (!okEnt && ent.size() > 1 && ent[0] == '#') {
                        okEnt = true;
                        for (size_t q = (ent[1] == 'x' ? 2 : 1); q < ent.size(); ++q) okEnt = okEnt && std::isxdigit(static_cast<unsigned char>(ent[q]));
                    }
                    if (!okEnt) return fail("unknown entity &" + ent + ";");
                    k = j;
                }
            }
            continue;
        }
        if (i + 3 < n && !std::memcmp(&d[i], "<!--", 4)) {
            size_t j = i + 4;
            while (j + 2 < n && std::memcmp(&d[j], "-->", 3)) ++j;
            if (j + 2 >= n) return fail("unterminated comment");
            i = j + 3;
            continue;
        }
        if (i + 8 < n && !std::memcmp(&d[i], "<![CDATA[", 9)) {
            size_t j = i + 9;
            while (j + 2 < n && std::memcmp(&d[j], "]]>", 3)) ++j;
            if (j + 2 >= n) return fail("unterminated CDATA");
            i = j + 3;
            continue;
        }
        if (i + 1 < n && (d[i + 1] == '?' || d[i + 1] == '!')) {
            size_t j = i;
            while (j < n && d[j] != '>') ++j;
            if (j >= n) return fail("unterminated <?/<!");
            i = j + 1;
            continue;
        }
        const bool closing = i + 1 < n && d[i + 1] == '/';
        size_t j = i + (closing ? 2 : 1);
        const size_t ns = j;
        while (j < n && !ws(d[j]) && d[j] != '>' && d[j] != '/') {
            if (d[j] < 0x20) return fail("control byte in name");
            ++j;
        }
        const std::string name(reinterpret_cast<const char*>(&d[ns]), j - ns);
        if (name.empty()) return fail("empty name");
        bool selfClose = false;
        // attributes: (ws+ name ws* = ws* quoted)* ws* [/] >
        while (true) {
            while (j < n && ws(d[j])) ++j;
            if (j >= n) return fail("truncated in tag");
            if (d[j] == '>') break;
            if (d[j] == '/') {
                if (j + 1 < n && d[j + 1] == '>' && !closing) { selfClose = true; ++j; break; }
                return fail("misplaced '/'");
            }
            if (closing) return fail("garbage in close tag (space in name?)");
            const size_t as = j;
            while (j < n && !ws(d[j]) && d[j] != '=' && d[j] != '>' && d[j] != '/') ++j;
            if (j == as) return fail("bad attribute");
            while (j < n && ws(d[j])) ++j;
            if (j >= n || d[j] != '=') return fail("attribute-less garbage after name (space in name?)");
            ++j;
            while (j < n && ws(d[j])) ++j;
            if (j >= n || (d[j] != '"' && d[j] != '\'')) return fail("unquoted attribute");
            const uint8_t q = d[j++];
            while (j < n && d[j] != q) ++j;
            if (j >= n) return fail("unterminated attribute");
            ++j;
        }
        // d[j] == '>'
        if (closing) {
            if (st.empty() || st.back() != name) return fail("close tag mismatch </" + name + ">");
            st.pop_back();
        } else {
            if (st.empty()) {
                if (sawRoot) return fail("second root");
                sawRoot = true;
            }
            ++r.elements;
            if (!selfClose) {
                st.push_back(name);
                r.maxDepth = std::max(r.maxDepth, st.size());
            } else {
                r.maxDepth = std::max(r.maxDepth, st.size() + 1);
            }
        }
        i = j + 1;
    }
    if (!sawRoot) return fail("no root");
    if (!st.empty()) return fail("unclosed elements at EOF");
    r.ok = true;
    return r;
}

size_t maxDepthOf(const sr3xtbl::Node* nd, size_t depth = 1) {
    // iterative to stay safe on deep files
    size_t best = 0;
    std::vector<std::pair<const sr3xtbl::Node*, size_t>> st{{nd, depth}};
    while (!st.empty()) {
        auto [n, d] = st.back();
        st.pop_back();
        best = std::max(best, d);
        for (const sr3xtbl::Node* c : n->children()) st.push_back({c, d + 1});
    }
    return best;
}

std::string hexAround(const Bytes& d, size_t off) {
    const size_t b = off > 24 ? off - 24 : 0, e = std::min(d.size(), off + 24);
    std::string hex, txt;
    char t[8];
    for (size_t i = b; i < e; ++i) {
        std::snprintf(t, sizeof t, "%02X ", d[i]);
        hex += t;
        txt += (d[i] >= 0x20 && d[i] < 0x7F) ? static_cast<char>(d[i]) : '.';
    }
    return "bytes[" + std::to_string(b) + ".." + std::to_string(e) + ") " + hex + " |" + txt + "|";
}

// ---------------------------------------------------------------------------
// G6 helpers
// ---------------------------------------------------------------------------
uint32_t crc32Standard(const std::string& s) {  // the ordinary zlib-style CRC-32 (init ~0, final ~)
    uint32_t c = 0xFFFFFFFFu;
    const uint32_t* t = sr3xtbl::NameHashTable();
    for (unsigned char ch : s) c = (c >> 8) ^ t[(ch ^ c) & 0xFF];
    return ~c;
}

uint32_t rd32(const Bytes& b, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}

const Item* findItem(const std::string& nameCi, const std::string& archiveEndsWith = "") {
    for (const Item& it : g_items) {
        if (it.status != 0) continue;
        if (lower(it.name) != nameCi) continue;
        if (!archiveEndsWith.empty() && !endsWithCi(it.archive, archiveEndsWith.c_str())) continue;
        return &it;
    }
    return nullptr;
}

// Row-name hashes, in file order, of `Table/<rowName>/Name`.
std::vector<uint32_t> rowNameHashes(const Item& it, const char* rowName, std::vector<std::string>* names = nullptr) {
    std::vector<uint32_t> out;
    sr3xtbl::Document doc = sr3xtbl::ParseDocument(it.data.data(), it.data.size());
    const sr3xtbl::Node* table = doc.table();
    for (const sr3xtbl::Node* row = sr3xtbl::FindChild(table, rowName); row; row = sr3xtbl::NextSibling(table, row, rowName)) {
        const std::string* nm = sr3xtbl::ChildText(row, "Name");
        if (!nm) continue;
        out.push_back(sr3xtbl::NameHash(*nm));
        if (names) names->push_back(*nm);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> archives, saveDirs;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--saves") && i + 1 < argc) saveDirs.push_back(argv[++i]);
        else archives.push_back(argv[i]);
    }

    // ------------------------------------------------------------------ G1
    for (const std::string& a : archives) {
        Bytes b = readFile(a);
        if (b.empty()) { std::printf("cannot read %s\n", a.c_str()); continue; }
        try {
            walk(vpp::ByteView(b.data(), b.size()), baseName(a));
        } catch (const std::exception& e) {
            std::printf("open failed %s: %s\n", a.c_str(), e.what());
        }
    }
    std::printf("=== G1 survey ===\n");
    std::printf("archives %zu, containers walked %lld, directory entries seen %lld\n", archives.size(), g_containers, g_entriesSeen);
    long long total = static_cast<long long>(g_items.size()), comp = 0, raw = 0, decoded = 0;
    std::map<int, long long> statusCount;
    std::set<uint64_t> distinct;
    std::set<std::string> distinctNames;
    for (const Item& it : g_items) {
        (it.compressed ? comp : raw)++;
        ++statusCount[it.status];
        if (it.status == 0) { ++decoded; distinct.insert(it.fnv); }
        distinctNames.insert(lower(it.name));
    }
    std::printf("entries whose name ends in \"xtbl\": %lld (compressed %lld, raw %lld); distinct names %zu; distinct payloads %zu\n",
                total, comp, raw, distinctNames.size(), distinct.size());
    std::printf("extension set (entries ending in xtbl):");
    for (auto& kv : g_extEndsXtbl) std::printf("  %s x%lld", kv.first.c_str(), kv.second);
    std::printf("\n");
    long long containsOnly = 0;
    for (auto& kv : g_extContainsOnly) containsOnly += kv.second;
    std::printf("entries containing \"xtbl\" but NOT ending in it: %lld\n", containsOnly);
    for (auto& kv : statusCount) std::printf("  container status %d : %lld\n", kv.first, kv.second);
    GATE(total > 0 && decoded == total, "every xtbl-family entry decoded (container status Ok / raw readable): %lld / %lld", decoded, total);
    GATE(total > 1000, "CONTROL: the survey is not vacuous (%lld entries found; Team B reported 2,080 compressed + the raw DLC ones)", total);

    // ------------------------------------------------------------------ G2 / G3 / G4
    std::printf("=== G2 parse ===\n");
    long long accepted = 0, rejected = 0, zeroWarn = 0, withWarn = 0;
    std::map<std::string, long long> warnClassCount;                   // class -> occurrences
    std::map<std::string, std::set<std::string>> warnClassFiles;       // class -> file names
    std::map<std::string, long long> warnClassEntries;                 // class -> entries
    std::vector<std::string> rejectLines;
    long long oracleAgree = 0, oracleDisagree = 0, oracleShapeAgree = 0, oracleShapeDisagree = 0, oracleRejected = 0;
    std::vector<std::string> disagreeLines;
    std::map<std::string, long long> rootNames;
    long long tableAny = 0, tableFirst = 0, tableNone = 0, tableCompressedOnly = 0, tableFirstCompressedOnly = 0;
    long long compressedAccepted = 0;
    std::vector<std::string> noTableFiles;
    std::map<std::string, long long> noTableSig;                 // "child names of root" signature -> entries
    std::map<std::string, std::string> noTableSigExample;
    std::vector<std::string> upperRootFiles;
    std::map<std::string, long long> rowCensus;      // exact spelling -> rows
    std::map<std::string, std::set<std::string>> rowCensusFiles;
    long long rowsTotal = 0;
    // G4 statistics
    long long elements = 0, leaves = 0, leavesNoText = 0, leavesWsOnly = 0, leafLeadWs = 0, leafTrailWs = 0, leafNumericLeadWs = 0;
    long long mixedEntries = 0, textAfter = 0;
    long long attrElems = 0, mixed = 0, nonAscii = 0, plusExp = 0, hexText = 0, dotLead = 0, bigIntFloat = 0, expText = 0, quotedText = 0;
    size_t maxDepthAll = 0;
    std::vector<std::string> leadWsExamples, wsOnlyExamples, plusExpExamples, bigIntExamples, hexExamples, expExamples;
    std::set<std::string> filesWithLeadWsNumeric, filesWithWsOnly;

    auto looksNumeric = [](const std::string& t) {  // after trimming: -?digits[.digits]
        size_t b = 0, e = t.size();
        while (b < e && (t[b] == ' ' || t[b] == '\t' || t[b] == '\r' || t[b] == '\n')) ++b;
        while (e > b && (t[e - 1] == ' ' || t[e - 1] == '\t' || t[e - 1] == '\r' || t[e - 1] == '\n')) --e;
        if (b == e) return false;
        size_t i = b;
        if (t[i] == '-') ++i;
        size_t digits = 0, dots = 0;
        for (; i < e; ++i) {
            if (t[i] >= '0' && t[i] <= '9') ++digits;
            else if (t[i] == '.') ++dots;
            else return false;
        }
        return digits > 0 && dots <= 1;
    };

    for (const Item& it : g_items) {
        if (it.status != 0) continue;
        sr3xtbl::Document doc;
        try {
            doc = sr3xtbl::ParseDocument(it.data.data(), it.data.size());
        } catch (const sr3xtbl::FormatError& e) {
            ++rejected;
            rejectLines.push_back(it.archive + " :: " + it.name + " : " + e.what() + " @" + std::to_string(e.offset()) + "  " + hexAround(it.data, e.offset()));
            continue;
        }
        ++accepted;
        if (it.compressed) ++compressedAccepted;
        if (doc.warnings().empty() && doc.warningCount() == 0) ++zeroWarn; else ++withWarn;
        std::set<std::string> classesHere;
        for (const sr3xtbl::Warning& w : doc.warnings()) {
            const std::string k = sr3xtbl::WarningKindName(w.kind);
            ++warnClassCount[k];
            warnClassFiles[k].insert(it.name);
            classesHere.insert(k);
        }
        for (const std::string& k : classesHere) ++warnClassEntries[k];

        // independent oracle
        Oracle o = strictOracle(it.data);
        const bool parserClean = doc.warningCount() == 0;
        if (o.ok == parserClean) ++oracleAgree;
        else {
            ++oracleDisagree;
            if (disagreeLines.size() < 10) disagreeLines.push_back(it.archive + " :: " + it.name + " oracle " + (o.ok ? "accepts" : ("rejects: " + o.why)) + " but parser had " + std::to_string(doc.warningCount()) + " warnings");
        }
        if (!o.ok) ++oracleRejected;
        if (o.ok) {
            const bool same = o.elements == doc.elementCount() && o.maxDepth == maxDepthOf(doc.root());
            (same ? oracleShapeAgree : oracleShapeDisagree)++;
            if (!same && disagreeLines.size() < 10) disagreeLines.push_back(it.archive + " :: " + it.name + " elements oracle " + std::to_string(o.elements) + " vs parser " + std::to_string(doc.elementCount()));
        }

        // shape
        const sr3xtbl::Node* root = doc.root();
        ++rootNames[root->name()];
        if (root->name() != "root") upperRootFiles.push_back(it.name);
        const sr3xtbl::Node* table = doc.table();
        if (table) {
            ++tableAny;
            if (it.compressed) ++tableCompressedOnly;
            if (!root->children().empty() && root->children()[0] == table) { ++tableFirst; if (it.compressed) ++tableFirstCompressedOnly; }
            for (const sr3xtbl::Node* row : table->children()) {
                ++rowCensus[row->name()];
                rowCensusFiles[row->name()].insert(it.name);
                ++rowsTotal;
            }
        } else {
            ++tableNone;
            noTableFiles.push_back(it.archive + " :: " + it.name);
            {
                std::set<std::string> nm;
                for (const sr3xtbl::Node* c : root->children()) nm.insert(c->name());
                std::string sig = "<" + root->name() + "> children:";
                int k = 0;
                for (const std::string& s : nm) { if (k++ < 6) sig += " " + s; }
                if (nm.size() > 6) sig += " ...(" + std::to_string(nm.size()) + " distinct)";
                ++noTableSig[sig];
                noTableSigExample.emplace(sig, it.name);
            }
        }

        // G4 statistics: iterate every element
        std::vector<const sr3xtbl::Node*> st{root};
        size_t md = maxDepthOf(root);
        maxDepthAll = std::max(maxDepthAll, md);
        while (!st.empty()) {
            const sr3xtbl::Node* nd = st.back();
            st.pop_back();
            ++elements;
            if (!nd->attributes().empty()) ++attrElems;
            for (auto c : nd->children()) st.push_back(c);
            for (char ch : nd->name()) if (static_cast<unsigned char>(ch) >= 0x80) { ++nonAscii; break; }
            if (nd->text()) for (char ch : *nd->text()) if (static_cast<unsigned char>(ch) >= 0x80) { ++nonAscii; break; }
            if (!nd->children().empty()) continue;
            ++leaves;
            const std::string* t = nd->text();
            if (!t) { ++leavesNoText; continue; }
            const std::string& s = *t;
            const auto isw = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            bool allw = true;
            for (char c : s) if (!isw(c)) { allw = false; break; }
            if (allw) {
                ++leavesWsOnly;
                filesWithWsOnly.insert(it.name);
                if (wsOnlyExamples.size() < 4) wsOnlyExamples.push_back(it.name + " <" + nd->name() + "> len " + std::to_string(s.size()));
                continue;
            }
            if (isw(s.front())) {
                ++leafLeadWs;
                if (leadWsExamples.size() < 4) leadWsExamples.push_back(it.name + " <" + nd->name() + ">");
                if (looksNumeric(s)) { ++leafNumericLeadWs; filesWithLeadWsNumeric.insert(it.name); }
            }
            if (isw(s.back())) ++leafTrailWs;
            if (s.size() >= 2 && s[0] == '"') ++quotedText;
            // numeric-shape statistics (engine grammar corner cases)
            size_t p = 0;
            if (s[p] == '-') ++p;
            if (p + 1 < s.size() && s[p] == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X')) {
                ++hexText;
                if (hexExamples.size() < 4) hexExamples.push_back(it.name + " <" + nd->name() + "> " + s.substr(0, 24));
            }
            if (s[p] == '.' && p + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[p + 1]))) ++dotLead;
            // number with exponent: digits [. digits] e|E [+-] digits and nothing else
            size_t q = p, dg = 0;
            while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q]))) { ++q; ++dg; }
            if (q < s.size() && s[q] == '.') { ++q; while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q]))) { ++q; ++dg; } }
            if (dg > 0 && q < s.size() && (s[q] == 'e' || s[q] == 'E')) {
                size_t r2 = q + 1;
                bool sign = false;
                if (r2 < s.size() && (s[r2] == '+' || s[r2] == '-')) { sign = s[r2] == '+'; ++r2; }
                size_t ed = 0;
                while (r2 < s.size() && std::isdigit(static_cast<unsigned char>(s[r2]))) { ++r2; ++ed; }
                if (ed > 0 && r2 == s.size()) {
                    ++expText;
                    if (sign) ++plusExp;
                    if (expExamples.size() < 6) expExamples.push_back(it.name + " <" + nd->name() + "> " + s);
                    if (sign && plusExpExamples.size() < 4) plusExpExamples.push_back(it.name + " <" + nd->name() + "> " + s);
                }
            }
            // integer part of a decimal number >= 2^31 written with a fraction (float with big integer part)
            {
                size_t k = p, digs = 0;
                while (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) { ++k; ++digs; }
                if (digs >= 10 && k < s.size() && s[k] == '.' && looksNumeric(s)) {
                    ++bigIntFloat;
                    if (bigIntExamples.size() < 4) bigIntExamples.push_back(it.name + " <" + nd->name() + "> " + s.substr(0, 30));
                }
            }
        }
        mixed += static_cast<long long>(doc.mixedContentElements());
        textAfter += static_cast<long long>(doc.textAfterChildElements());
        if (doc.mixedContentElements()) ++mixedEntries;
    }

    std::printf("decoded files parsed: accepted %lld, rejected (FormatError) %lld, of %lld\n", accepted, rejected, decoded);
    for (const std::string& l : rejectLines) std::printf("  REJECTED %s\n", l.c_str());
    GATE(rejected == 0 && accepted == decoded, "parser accepts every decoded file: %lld / %lld (FormatError count %lld)", accepted, decoded, rejected);
    std::printf("accepted with ZERO warnings: %lld / %lld;  with warnings: %lld\n", zeroWarn, accepted, withWarn);
    for (auto& kv : warnClassCount) {
        std::printf("  warning class %-24s occurrences %-6lld entries %-3lld files:", kv.first.c_str(), kv.second, warnClassEntries[kv.first]);
        for (auto& f : warnClassFiles[kv.first]) std::printf(" %s", f.c_str());
        std::printf("\n");
    }
    {
        // which entries carry warnings (one line each) - so the documented defects can be checked by eye
        std::printf("  entries with warnings:\n");
        for (const Item& it : g_items) {
            if (it.status != 0) continue;
            sr3xtbl::Document doc = sr3xtbl::ParseDocument(it.data.data(), it.data.size());
            if (doc.warningCount() == 0) continue;
            std::printf("    %s :: %s  (%zu warnings; first: [%s] %s @%zu)\n", it.archive.c_str(), it.name.c_str(), doc.warningCount(),
                        sr3xtbl::WarningKindName(doc.warnings()[0].kind), doc.warnings()[0].detail.c_str(), doc.warnings()[0].offset);
        }
    }
    // The spec (spec-xtbl-format.md 7) names exactly these defect classes and files.
    const std::set<std::string> expectedFiles = {"template.xtbl", "07_out.cte_xtbl", "07_out-lightset.xtbl", "xbox360_text.xtbl"};
    std::set<std::string> gotFiles;
    for (auto& kv : warnClassFiles) for (auto& f : kv.second) gotFiles.insert(lower(f));
    GATE(gotFiles == expectedFiles, "warned files == the four the spec names (got %zu distinct file names)", gotFiles.size());
    GATE(warnClassCount.count("MismatchedCloseTag") && warnClassCount.count("ControlCharacterInText") && warnClassCount.count("NameContainsWhitespace") &&
             warnClassCount.size() == 3,
         "warning classes seen are exactly the three defect classes of spec-xtbl-format.md 7 (%zu classes)", warnClassCount.size());
    GATE(oracleDisagree == 0 && oracleAgree == accepted,
         "CONTROL (independent strict oracle): oracle rejects exactly the parser-warned files: agree %lld / %lld (oracle rejected %lld)", oracleAgree, accepted, oracleRejected);
    for (const std::string& l : disagreeLines) std::printf("    disagreement: %s\n", l.c_str());
    GATE(oracleShapeDisagree == 0 && oracleShapeAgree > 0,
         "CONTROL: for oracle-accepted files the parser's element count and maximum depth equal the oracle's: %lld / %lld", oracleShapeAgree, oracleShapeAgree + oracleShapeDisagree);
    GATE(oracleRejected > 0 && oracleRejected < accepted, "CONTROL: the oracle is not vacuous - it rejects some files (%lld) and accepts the rest", oracleRejected);

    std::printf("=== G3 shape ===\n");
    std::printf("root element names:");
    for (auto& kv : rootNames) std::printf("  <%s> x%lld", kv.first.c_str(), kv.second);
    std::printf("\n");
    std::printf("root -> Table child present: %lld / %lld  (Table is the FIRST child of root: %lld;  no Table: %lld)\n", tableAny, accepted, tableFirst, tableNone);
    std::printf("  compressed entries only: Table present %lld / %lld, first child %lld   (Team B reported 1,817 of 2,080)\n", tableCompressedOnly, compressedAccepted, tableFirstCompressedOnly);
    {
        std::map<std::string, long long> uniq;
        for (const std::string& f : noTableFiles) ++uniq[f];
        std::printf("  files without a Table child (%zu):\n", uniq.size());
        int shown = 0;
        for (auto& kv : uniq) if (shown++ < 40) std::printf("    %s\n", kv.first.c_str());
    }
    {
        std::printf("  shapes of the files WITHOUT a Table child (root-children signature: entries, first example):\n");
        std::vector<std::pair<long long, std::string>> v;
        for (auto& kv : noTableSig) v.push_back({kv.second, kv.first});
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < v.size() && i < 14; ++i) std::printf("    %5lld  %s   e.g. %s\n", v[i].first, v[i].second.c_str(), noTableSigExample[v[i].second].c_str());
        std::printf("  %zu distinct signatures in total\n", v.size());
        std::printf("  files whose root element is not spelled `root`:");
        for (const std::string& n : upperRootFiles) std::printf(" %s", n.c_str());
        std::printf("\n");
    }
    GATE(tableAny + tableNone == accepted, "shape census is complete: %lld + %lld == %lld", tableAny, tableNone, accepted);
    {
        // the two spec-named exceptions must be among the no-Table files
        bool an = false, ng = false;
        for (const std::string& f : noTableFiles) { an |= f.find(":: action_nodes.xtbl") != std::string::npos; ng |= f.find(":: node_graph_files.xtbl") != std::string::npos; }
        GATE(an && ng, "CONTROL: action_nodes.xtbl and node_graph_files.xtbl (traffic-ai 1.2) have no Table wrapper, as specified");
    }
    // row-element-name census
    std::printf("row-element-name census (children of Table): %zu distinct names, %lld rows\n", rowCensus.size(), rowsTotal);
    {
        std::vector<std::pair<long long, std::string>> v;
        for (auto& kv : rowCensus) v.push_back({kv.second, kv.first});
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
        for (size_t i = 0; i < v.size() && i < 45; ++i) std::printf("    %-34s rows %-7lld in %zu file names\n", v[i].second.c_str(), v[i].first, rowCensusFiles[v[i].second].size());
        // case-variant collisions
        std::map<std::string, std::vector<std::string>> byLower;
        for (auto& kv : rowCensus) byLower[lower(kv.first)].push_back(kv.first);
        for (auto& kv : byLower) if (kv.second.size() > 1) {
            std::printf("    case-variant row names (the engine treats these as one):");
            for (auto& n : kv.second) std::printf(" %s(x%lld)", n.c_str(), rowCensus[n]);
            std::printf("\n");
        }
    }

    std::printf("=== G4 text statistics ===\n");
    std::printf("elements %lld, leaf elements %lld (no text %lld, whitespace-only text %lld), max depth %zu, elements with attributes %lld\n",
                elements, leaves, leavesNoText, leavesWsOnly, maxDepthAll, attrElems);
    std::printf("mixed content (an element with child elements AND non-whitespace text of its own): %lld elements in %lld entries; of which with text AFTER a child element: %lld\n",
                mixed, mixedEntries, textAfter);
    std::printf("leaf text with LEADING whitespace %lld (of which numeric-looking %lld in %zu file names), trailing whitespace %lld\n",
                leafLeadWs, leafNumericLeadWs, filesWithLeadWsNumeric.size(), leafTrailWs);
    for (auto& s : wsOnlyExamples) std::printf("    whitespace-only leaf: %s\n", s.c_str());
    for (auto& s : leadWsExamples) std::printf("    leading-whitespace leaf: %s\n", s.c_str());
    std::printf("leaf text: starts with 0x/0X %lld, leading '.' number %lld, exponent-form number %lld (with '+' in exponent %lld), decimal number with >=10 integer digits and a fraction %lld, starts with '\"' %lld; elements with non-ASCII bytes %lld\n",
                hexText, dotLead, expText, plusExp, bigIntFloat, quotedText, nonAscii);
    for (auto& s : hexExamples) std::printf("    0x text: %s\n", s.c_str());
    for (auto& s : expExamples) std::printf("    exponent text: %s\n", s.c_str());
    for (auto& s : bigIntExamples) std::printf("    big-integer float text: %s\n", s.c_str());
    GATE(leaves > 0 && elements > 0, "tree statistics gathered over %lld elements", elements);

    // ------------------------------------------------------------------ G5
    std::printf("=== G5 parser controls on real files ===\n");
    {
        long long trials = 0, throwsOk = 0, eofOk = 0, injOk = 0, injTrials = 0, junkOk = 0, junkTrials = 0;
        size_t idx = 0;
        for (const Item& it : g_items) {
            if (it.status != 0) continue;
            if ((idx++ % 7) != 0) continue;  // every 7th file
            const Bytes& d = it.data;
            if (d.size() < 200) continue;
            // (a) cut inside a tag: find a '<' near the middle, keep 3 bytes of the tag
            size_t mid = d.size() / 2;
            size_t lt = mid;
            while (lt < d.size() && d[lt] != '<') ++lt;
            if (lt + 4 >= d.size() || d[lt + 1] == '>') continue;
            ++trials;
            {
                bool threw = false;
                // keep '<' plus ONE more byte: cutting after two name bytes can already complete a tag (`<X>`)
                try { sr3xtbl::ParseDocument(d.data(), lt + 2); } catch (const sr3xtbl::FormatError&) { threw = true; }
                throwsOk += threw;
            }
            // (b) cut right after a '>' in the middle
            size_t gt = mid;
            while (gt < d.size() && d[gt] != '>') ++gt;
            {
                bool okEof = false;
                try {
                    sr3xtbl::Document doc = sr3xtbl::ParseDocument(d.data(), gt + 1);
                    for (const auto& w : doc.warnings()) if (w.kind == sr3xtbl::WarningKind::UnclosedAtEof) okEof = true;
                } catch (const sr3xtbl::FormatError&) {}
                eofOk += okEof;
            }
            // (c) inject: rename the first close tag (</X>) to </X_> ... only where the file is warning-free
            {
                sr3xtbl::Document base = sr3xtbl::ParseDocument(d.data(), d.size());
                if (base.warningCount() == 0) {
                    size_t p = 0;
                    while (p + 2 < d.size() && !(d[p] == '<' && d[p + 1] == '/')) ++p;
                    // pick a close tag well inside the file
                    p = mid;
                    while (p + 2 < d.size() && !(d[p] == '<' && d[p + 1] == '/')) ++p;
                    if (p + 2 < d.size()) {
                        Bytes m = d;
                        m.insert(m.begin() + static_cast<std::ptrdiff_t>(p) + 2, '#');  // </#Name> : names no open element
                        ++injTrials;
                        sr3xtbl::Document md = sr3xtbl::ParseDocument(m.data(), m.size());
                        long long mm = 0;
                        for (const auto& w : md.warnings()) mm += w.kind == sr3xtbl::WarningKind::MismatchedCloseTag;
                        injOk += (mm == 1 && md.elementCount() == base.elementCount());
                    }
                }
            }
            // (d) not XML-like: same bytes without the leading '<'
            {
                ++junkTrials;
                Bytes m(d.begin() + 1, d.end());
                bool threw = false;
                try { sr3xtbl::ParseDocument(m.data(), m.size()); } catch (const sr3xtbl::FormatError&) { threw = true; }
                junkOk += threw;
            }
        }
        GATE(trials > 100 && throwsOk == trials, "(a) truncation inside a tag throws FormatError: %lld / %lld", throwsOk, trials);
        GATE(eofOk == trials, "(b) truncation right after a '>' parses with UnclosedAtEof: %lld / %lld", eofOk, trials);
        GATE(injTrials > 100 && injOk == injTrials, "(c) injected close tag naming no open element -> exactly one MismatchedCloseTag, same element count: %lld / %lld", injOk, injTrials);
        GATE(junkOk == junkTrials && junkTrials > 100, "(d) input without its leading '<' is rejected: %lld / %lld", junkOk, junkTrials);
    }

    // ------------------------------------------------------------------ G6
    std::printf("=== G6 name hash ===\n");
    {
        const uint32_t* T = sr3xtbl::NameHashTable();
        GATE(T[0] == 0 && T[1] == 0x77073096u && T[2] == 0xEE0E612Cu, "table starts 0, 0x77073096, 0xEE0E612C (spec-save-format.md 6.1)");
        const uint32_t stdCheck = ~sr3xtbl::NameHashCaseSensitive("123456789", 0xFFFFFFFFu);
        GATE(stdCheck == 0xCBF43926u, "known answer: with seed 0xFFFFFFFF and a final complement the same routine gives the standard CRC-32 check value 0xCBF43926 (got 0x%08X)", stdCheck);
    }
    // Saves: collect distinct files
    struct Save { std::string path; Bytes data; };
    std::vector<Save> saves;
    {
        std::set<uint64_t> seen;
        for (const std::string& dir : saveDirs) {
            for (int n = 0; n < 24; ++n) {
                char nm[64];
                std::snprintf(nm, sizeof nm, "\\sr3save_%02d.sr3s_pc", n);
                Bytes b = readFile(dir + nm);
                if (b.size() != 108776) continue;
                if (!seen.insert(fnv64(b)).second) continue;
                saves.push_back({dir + nm, std::move(b)});
            }
        }
    }
    std::printf("save snapshots (distinct, 108,776 bytes each): %zu\n", saves.size());

    // Names -> hash sets
    const Item* unl = findItem("unlockables.xtbl", "misc_tables.vpp_pc");
    const Item* pun = findItem("patch_unlockables.xtbl");
    const Item* d1 = findItem("dlc1_unlockables.xtbl");
    const Item* d2 = findItem("dlc2_unlockables.xtbl");
    const Item* d3 = findItem("dlc3_unlockables.xtbl");
    const Item* chs = findItem("cheats.xtbl", "misc_tables.vpp_pc");
    GATE(unl && d1 && d2 && d3, "found unlockables.xtbl (base) and dlc1/2/3_unlockables.xtbl among the decoded tables");
    if (unl && d1 && d2 && d3 && !saves.empty()) {
        std::vector<uint32_t> hBase = rowNameHashes(*unl, "Unlockable");
        std::vector<uint32_t> hPatch = pun ? rowNameHashes(*pun, "Unlockable") : std::vector<uint32_t>{};
        std::vector<uint32_t> h1 = rowNameHashes(*d1, "Unlockable"), h2 = rowNameHashes(*d2, "Unlockable"), h3 = rowNameHashes(*d3, "Unlockable");
        std::printf("Unlockable rows: base %zu, patch %zu, dlc1 %zu, dlc2 %zu, dlc3 %zu\n", hBase.size(), hPatch.size(), h1.size(), h2.size(), h3.size());
        std::vector<uint32_t> dlcRows;
        dlcRows.insert(dlcRows.end(), h1.begin(), h1.end());
        dlcRows.insert(dlcRows.end(), h2.begin(), h2.end());
        dlcRows.insert(dlcRows.end(), h3.begin(), h3.end());

        // controls: alternative hashes of the same names
        std::vector<std::string> namesBase, names1, names2, names3, namesP;
        (void)rowNameHashes(*unl, "Unlockable", &namesBase);
        if (pun) (void)rowNameHashes(*pun, "Unlockable", &namesP);
        (void)rowNameHashes(*d1, "Unlockable", &names1);
        (void)rowNameHashes(*d2, "Unlockable", &names2);
        (void)rowNameHashes(*d3, "Unlockable", &names3);
        std::unordered_set<uint32_t> setAll, setNoLower, setStdCrc;
        auto addAll = [&](const std::vector<std::string>& v) {
            for (const std::string& s : v) {
                setAll.insert(sr3xtbl::NameHash(s));
                setNoLower.insert(sr3xtbl::NameHashCaseSensitive(s));
                setStdCrc.insert(crc32Standard(lower(s)));
            }
        };
        addAll(namesBase); addAll(namesP); addAll(names1); addAll(names2); addAll(names3);
        // random control ids
        std::vector<uint32_t> randomIds;
        {
            uint32_t s = 0x1234567u;
            for (int i = 0; i < 4000; ++i) { s = s * 1664525u + 1013904223u; randomIds.push_back(s); }
        }
        long long baseSlots = 0, baseInSet = 0, baseInNoLower = 0, baseInStd = 0, baseInOrder = 0, baseIdentity = 0, prioBefore = 0, prioTotal = 0;
        // Priority hash of each base/patch row (0 = none), keyed by the row-name hash
        std::unordered_map<uint32_t, uint32_t> prioOf;
        for (const Item* src : {unl, pun}) {
            if (!src) continue;
            sr3xtbl::Document pd = sr3xtbl::ParseDocument(src->data.data(), src->data.size());
            const sr3xtbl::Node* tb = pd.table();
            for (const sr3xtbl::Node* row = sr3xtbl::FindChild(tb, "Unlockable"); row; row = sr3xtbl::NextSibling(tb, row, "Unlockable")) {
                const std::string* nm = sr3xtbl::ChildText(row, "Name");
                if (!nm) continue;
                prioOf.emplace(sr3xtbl::NameHash(*nm), sr3xtbl::NameHashOrZero(sr3xtbl::ChildText(row, "Priority")));
            }
        }
        long long dlcSlots = 0, dlcInSet = 0, dlcExact = 0, dlcExpected = 0, dlcNoLowerExact = 0;
        long long randHits = 0;
        for (uint32_t r : randomIds) randHits += setAll.count(r);
        std::printf("control: %zu random 32-bit values found in the %zu-name hash set: %lld\n", randomIds.size(), setAll.size(), randHits);
        for (const Save& sv : saves) {
            // base block 0x4428: 350 x u32; non-zero slots are the base unlockables
            std::vector<uint32_t> slots;
            for (int i = 0; i < 350; ++i) { const uint32_t v = rd32(sv.data, 0x4428 + 4 * i); if (v) slots.push_back(v); }
            {
                std::unordered_map<uint32_t, size_t> slotOf;
                for (size_t k = 0; k < slots.size(); ++k) slotOf.emplace(slots[k], k);
                for (size_t k = 0; k < slots.size(); ++k) {
                    baseIdentity += (k < hBase.size() && slots[k] == hBase[k]);
                    auto pi = prioOf.find(slots[k]);
                    if (pi == prioOf.end() || pi->second == 0) continue;
                    ++prioTotal;
                    auto ps = slotOf.find(pi->second);
                    prioBefore += (ps != slotOf.end() && ps->second < k);
                }
            }
            // patch rows follow the base rows in load order; the accepted sequence is base then patch
            std::vector<uint32_t> seq = hBase;
            seq.insert(seq.end(), hPatch.begin(), hPatch.end());
            size_t rp = 0;
            long long inSet = 0, inOrder = 0;
            for (uint32_t v : slots) {
                inSet += setAll.count(v);
                baseInNoLower += setNoLower.count(v);
                baseInStd += setStdCrc.count(v);
                size_t q = rp;
                while (q < seq.size() && seq[q] != v) ++q;
                if (q < seq.size()) { ++inOrder; rp = q + 1; }
            }
            baseSlots += static_cast<long long>(slots.size());
            baseInSet += inSet;
            baseInOrder += inOrder;
            // dlc block 0x15D90
            std::vector<uint32_t> dslots;
            for (int i = 0; i < 350; ++i) { const uint32_t v = rd32(sv.data, 0x15D90 + 4 * i); if (v) dslots.push_back(v); }
            dlcSlots += static_cast<long long>(dslots.size());
            for (uint32_t v : dslots) dlcInSet += setAll.count(v);
            // spec-tables-progression.md 4.1: slots 6..61 == the 56 DLC rows in file order (v9 snapshots)
            if (dslots.size() == 62) {
                for (size_t k = 0; k < dlcRows.size() && 6 + k < 62; ++k) {
                    ++dlcExpected;
                    dlcExact += (dslots[6 + k] == dlcRows[k]);
                }
                // control: same positions against the non-lowercasing hash of the names
                std::vector<std::string> all = names1;
                all.insert(all.end(), names2.begin(), names2.end());
                all.insert(all.end(), names3.begin(), names3.end());
                for (size_t k = 0; k < all.size() && 6 + k < 62; ++k) dlcNoLowerExact += (dslots[6 + k] == sr3xtbl::NameHashCaseSensitive(all[k]));
            }
        }
        std::printf("base unlockable block (0x4428): non-zero slots over all saves %lld; ids = NameHash(Name) of a row of unlockables/patch_unlockables: %lld / %lld; in ascending row order: %lld / %lld\n",
                    baseSlots, baseInSet, baseSlots, baseInOrder, baseSlots);
        std::printf("   controls on the same slots: non-lowercasing hash matches %lld, standard complemented CRC-32 matches %lld\n", baseInNoLower, baseInStd);
        std::printf("DLC unlockable block (0x15D90): non-zero slots %lld, ids matching a dlc row name hash %lld; slots 6..61 == rows of dlc1/2/3_unlockables in file order: %lld / %lld; non-lowercasing control: %lld\n",
                    dlcSlots, dlcInSet, dlcExact, dlcExpected, dlcNoLowerExact);
        GATE(baseSlots > 0 && baseInSet == baseSlots, "base unlockable ids reproduced from the real table rows: %lld / %lld", baseInSet, baseSlots);
        std::printf("   (informational) base slot order vs file row order: %lld / %lld slots are in ascending file-row order; slot k == row k for %lld / %lld\n", baseInOrder, baseSlots, baseIdentity, baseSlots);
        std::printf("   (informational) the row a slot's <Priority> element names sits at an EARLIER slot: %lld / %lld slots that have a Priority\n", prioBefore, prioTotal);
        GATE(dlcInSet == dlcSlots, "every DLC-block id is the hash of a dlc row name: %lld / %lld", dlcInSet, dlcSlots);
        GATE(baseInStd == 0 && randHits == 0, "CONTROL: the standard CRC-32 and random ids reproduce nothing (%lld, %lld)", baseInStd, randHits);
        GATE(baseInNoLower < baseInSet, "CONTROL: a hash that does not lower-case fails on mixed-case names (%lld of %lld still match, the all-lower-case ones)", baseInNoLower, baseInSet);
        GATE(dlcExpected > 0 && dlcExact == dlcExpected, "DLC ids in the v9 snapshots equal CRC(lower(Name)) of the dlc rows in file order: %lld / %lld (Team A: 784/784)", dlcExact, dlcExpected);
        GATE(dlcExpected == 0 || dlcNoLowerExact < dlcExact, "CONTROL: non-lowercasing hash reproduces fewer (%lld)", dlcNoLowerExact);
    }
    if (chs && !saves.empty()) {
        // cheats: ids at 0x0CC (count) / 0x0D0.. ; the ids are CRC(lower(UnlockString)) per spec-tables-progression.md
        sr3xtbl::Document doc = sr3xtbl::ParseDocument(chs->data.data(), chs->data.size());
        std::unordered_map<uint32_t, std::string> byHash;  // hash -> "row/child = text"
        std::vector<const sr3xtbl::Node*> st{doc.table()};
        while (!st.empty()) {
            const sr3xtbl::Node* n = st.back(); st.pop_back();
            if (!n) continue;
            for (auto c : n->children()) st.push_back(c);
            if (n->children().empty() && n->text() && n->text()->size() < 64) byHash.emplace(sr3xtbl::NameHash(*n->text()), n->name() + " = " + *n->text());
        }
        long long ids = 0, hit = 0, hitUnlockString = 0, randHit = 0;
        std::set<std::string> resolved;
        for (const Save& sv : saves) {
            const uint32_t cnt = rd32(sv.data, 0xCC);
            if (cnt > 200) continue;
            for (uint32_t i = 0; i < cnt; ++i) {
                const uint32_t id = rd32(sv.data, 0xD0 + 4 * i);
                ++ids;
                auto f = byHash.find(id);
                if (f != byHash.end()) {
                    ++hit;
                    if (f->second.rfind("UnlockString", 0) == 0) ++hitUnlockString;
                    resolved.insert(f->second);
                }
            }
        }
        uint32_t s = 99;
        for (int i = 0; i < 4000; ++i) { s = s * 1664525u + 1013904223u; randHit += byHash.count(s); }
        std::printf("cheat ids in saves: %lld; equal NameHash(text of a cheats.xtbl leaf): %lld (of which the leaf is <UnlockString>: %lld); distinct resolved:", ids, hit, hitUnlockString);
        for (const std::string& r : resolved) std::printf(" [%s]", r.c_str());
        std::printf("\n   control: random ids resolving against the same %zu-entry set: %lld / 4000\n", byHash.size(), randHit);
        GATE(ids > 0 && hit > 0 && hitUnlockString == hit && randHit == 0, "cheat ids reproduced from cheats.xtbl <UnlockString> text (%lld / %lld ids; random control %lld)", hit, ids, randHit);
    }

    std::printf("\n%s (%d gate failure%s)\n", g_fail ? "FAILED" : "ALL GATES PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
