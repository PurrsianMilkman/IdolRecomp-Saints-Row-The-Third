// Impact-audit dump: parses every .xtbl/.cte_xtbl in the given archives with sr3xtbl and writes one line per
// element (path, text) plus, for numeric-looking texts, the float and int32 reader results, so two builds can be diffed.
// Usage: xtbl_audit_dump <out.txt> <archive.vpp_pc> [...]
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"
#include "vpp/container.h"

namespace {
using Bytes = std::vector<uint8_t>;
struct Item { std::string name; Bytes data; };
std::vector<Item> g_items;
std::vector<std::string> g_highNames;

bool endsXtbl(const std::string& n) {
    std::string l = n;
    for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l.size() >= 4 && l.compare(l.size() - 4, 4, "xtbl") == 0;
}

void walk(vpp::ByteView bytes, const std::string& path) {
    vpp::Container c(bytes);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        for (unsigned char ch : e.name) if (ch >= 0x80) { g_highNames.push_back(path + "/" + e.name); break; }
        if (!endsXtbl(e.name)) continue;
        Item it;
        it.name = path + "/" + e.name;
        if (e.payload.kind == vpp::PayloadKind::Compressed) {
            vpp::DecompressResult r = c.decompressEntry(i);
            if (r.status != vpp::DecodeStatus::Ok) continue;
            it.data = std::move(r.data);
        } else {
            try { vpp::ByteView v = c.rawEntryBytes(i); it.data.assign(v.data(), v.data() + v.size()); } catch (...) { continue; }
        }
        g_items.push_back(std::move(it));
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind != vpp::PayloadKind::Raw || endsXtbl(c.entries()[i].name)) continue;
        try { walk(c.rawEntryBytes(i), path + "/" + c.entries()[i].name); } catch (...) {}
    }
}

std::string esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c < 0x20 || c >= 0x7f || c == '\\') { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", c); o += b; }
        else o.push_back(static_cast<char>(c));
    }
    return o;
}

bool numericish(const std::string& s) {
    for (char c : s) if ((c >= '0' && c <= '9')) return true;
    return false;
}

void dump(FILE* f, const sr3xtbl::Node* n, const std::string& path) {
    const std::string p = path + "/" + n->name();
    std::fprintf(f, "E %s", esc(p).c_str());
    if (const std::string* t = n->text()) {
        std::fprintf(f, " T='%s'", esc(*t).c_str());
        if (numericish(*t) && t->size() < 40) {
            const float v = sr3xtbl::ParseFloat(*t);
            uint32_t bits; std::memcpy(&bits, &v, 4);
            std::fprintf(f, " F=%08x I=%d", bits, sr3xtbl::ParseInt32(*t));
        }
    }
    std::fprintf(f, "\n");
    for (const sr3xtbl::Node* c : n->children()) dump(f, c, p);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage\n"); return 2; }
    for (int i = 2; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary | std::ios::ate);
        if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[i]); continue; }
        Bytes b(static_cast<size_t>(in.tellg())); in.seekg(0); in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
        std::string base = argv[i];
        base = base.substr(base.find_last_of("\\/") + 1);
        try { walk(vpp::ByteView(b.data(), b.size()), base); } catch (const std::exception& e) { std::fprintf(stderr, "%s: %s\n", argv[i], e.what()); }
    }
    std::sort(g_items.begin(), g_items.end(), [](const Item& a, const Item& b) { return a.name < b.name; });
    FILE* f = std::fopen(argv[1], "wb");
    long long ok = 0, bad = 0;
    for (const Item& it : g_items) {
        std::fprintf(f, "FILE %s\n", esc(it.name).c_str());
        try {
            sr3xtbl::Document d = sr3xtbl::ParseDocument(it.data.data(), it.data.size());
            ++ok;
            dump(f, d.root(), "");
        } catch (const std::exception&) {
            ++bad;
            std::fprintf(f, "REJECTED\n");
        }
    }
    std::fclose(f);
    std::printf("entry names with bytes >= 0x80: %zu\n", g_highNames.size());
    for (const std::string& n : g_highNames) std::printf("HIGHNAME %s\n", esc(n).c_str());
    std::printf("files=%zu parsed=%lld rejected=%lld\n", g_items.size(), ok, bad);
    return 0;
}
