// Extracts one zone stem's `.czn_pc` / `.gzn_pc` / `.czh_pc` bytes out of a
// `.vpp_pc` archive to plain files, so a specific member of the §9.55.1
// Gap-2 fail bucket can be examined byte-by-byte without re-running a whole
// archive scan. Read-only; uses the same entry-pairing walk as
// diag_zone_gaps.cpp / diag_zone_gap52.cpp.
//
//   dump_zone_pair.exe <archive.vpp_pc> <stem-substring> <out-dir>
//
// Writes <out-dir>/<sanitized-stem>.czn, .gzn, .czh for every stem whose
// name contains <stem-substring>, and prints what it wrote.
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
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

bool getEntry(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

std::string sanitize(const std::string& s) {
    std::string o;
    for (char ch : s) o += (isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') ? ch : '_';
    return o;
}

std::string g_needle, g_outDir;

void writeOut(const std::string& stem, const char* ext, const std::vector<uint8_t>& b) {
    std::string path = g_outDir + "/" + sanitize(stem) + ext;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    printf("wrote %s (%zu bytes)\n", path.c_str(), b.size());
}

void walk(const vpp::Container& c, const std::string& path) {
    std::map<std::string, size_t> czh, czn, gzn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czh_pc", stem)) czh[stem] = i;
        else if (stemFor(n, ".czn_pc", stem)) czn[stem] = i;
        else if (stemFor(n, ".gzn_pc", stem)) gzn[stem] = i;
    }
    for (const auto& kv : czn) {
        if (kv.first.find(g_needle) == std::string::npos) continue;
        printf("MATCH in container: %s :: %s\n", path.c_str(), kv.first.c_str());
        std::vector<uint8_t> b;
        try { if (getEntry(c, kv.second, b)) writeOut(kv.first, ".czn", b); } catch (const std::exception& e) { printf("  czn error: %s\n", e.what()); }
        auto g = gzn.find(kv.first);
        if (g != gzn.end()) {
            std::vector<uint8_t> gb;
            try { if (getEntry(c, g->second, gb)) writeOut(kv.first, ".gzn", gb); } catch (const std::exception& e) { printf("  gzn error: %s\n", e.what()); }
        }
        auto h = czh.find(kv.first);
        if (h != czh.end()) {
            std::vector<uint8_t> hb;
            try { if (getEntry(c, h->second, hb)) writeOut(kv.first, ".czh", hb); } catch (const std::exception& e) { printf("  czh error: %s\n", e.what()); }
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i), path + " > " + c.entries()[i].name); } catch (const std::exception&) {}
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: dump_zone_pair.exe <archive.vpp_pc> <stem-substring> <out-dir>\n"); return 1; }
    g_needle = argv[2];
    g_outDir = argv[3];
    std::vector<uint8_t> b = readFile(argv[1]);
    if (b.empty()) { printf("cannot read %s\n", argv[1]); return 1; }
    try {
        vpp::Container c(vpp::ByteView(b.data(), b.size()));
        walk(c, argv[1]);
    } catch (const std::exception& ex) {
        printf("FAILED to open %s: %s\n", argv[1], ex.what());
        return 1;
    }
    return 0;
}
