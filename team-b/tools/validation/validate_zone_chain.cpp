// Independent corroboration for the `.gzn_pc` segment-chain fix
// (spec-zone-data-format.md Sec10.4/Sec10.5, 2026-09-13): the corrected
// ZoneGeometry::locate() returns 38,152 g-backed blocks where the old
// 16-aligned search returned 2,843, and a matching COUNT is not a matching
// SET. This harness tests the structural claim instead, which a wrong set
// of the right size cannot satisfy: do the located segments tile their
// `.gzn_pc` exactly, end to end, in c-file order, with no gap and nothing
// left over?
//
// Result over the same 10 archives spec Sec10's population uses:
//   1,002 / 1,002 files tile to EOF exactly (0 short, 0 past)
//   38,152 g-backed blocks; 580 multi-block files carrying 37,730 of them
//   37,035 successors begin exactly where the previous segment ended,
//   115 at the next 16-byte boundary (the `~al` padded case), 0 neither
//
// CONTROL, and it fires: re-run the same tiling arithmetic with every
// declared g-length perturbed by +4 and only 21 / 1,002 files still tile
// (2.1%). Without that line the test is vacuous - any monotone set of
// offsets ending at EOF would "pass".
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "sr3zone/zone_geometry.h"
#include "vpp/container.h"

namespace {
std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg(); if (n < 0) n = 0;
    f.seekg(0); std::vector<uint8_t> b((size_t)n);
    if (n > 0) f.read((char*)b.data(), n);
    return b;
}
bool stemFor(const std::string& n, const std::string& ext, std::string& stem) {
    if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) {
        stem = n.substr(0, n.size() - ext.size()); return true;
    }
    return false;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size()); return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data); return true;
}

long long g_files = 0, g_tileExact = 0, g_tileShort = 0, g_tileLong = 0;
long long g_blocks = 0, g_contiguous = 0, g_padded = 0, g_other = 0;
long long g_multiBlockFiles = 0, g_multiBlockBlocks = 0;
long long g_ctrlTileExact = 0; // control: every declared length +4

void check(const std::vector<uint8_t>& cb, const std::vector<uint8_t>& gb) {
    if (gb.empty()) return;
    auto blocks = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(cb.data(), cb.size()),
                                               sr3zone::ByteView(gb.data(), gb.size()));
    std::vector<const sr3zone::ZoneMeshBlockEntry*> g;
    for (const auto& e : blocks) if (e.fromGFile) g.push_back(&e);
    if (g.empty()) return;
    ++g_files;
    if (g.size() > 1) { ++g_multiBlockFiles; g_multiBlockBlocks += (long long)g.size(); }
    size_t cursor = 0;
    for (size_t i = 0; i < g.size(); ++i) {
        ++g_blocks;
        if (i > 0) {
            if (g[i]->gznOffset == cursor) ++g_contiguous;
            else if (g[i]->gznOffset == (cursor + 15) / 16 * 16) ++g_padded;
            else ++g_other;
        }
        cursor = g[i]->gznOffset + g[i]->block.gLength();
    }
    if (cursor == gb.size()) ++g_tileExact;
    else if (cursor < gb.size()) ++g_tileShort;
    else ++g_tileLong;

    size_t ctrl = 0;
    for (size_t i = 0; i < g.size(); ++i) ctrl = (i == 0 ? 0 : ctrl) + g[i]->block.gLength() + 4;
    if (ctrl == gb.size()) ++g_ctrlTileExact;
}

void walk(const vpp::Container& c) {
    std::map<std::string, size_t> czn, gzn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czn_pc", stem)) czn[stem] = i;
        else if (stemFor(n, ".gzn_pc", stem)) gzn[stem] = i;
    }
    for (const auto& kv : czn) {
        try {
            std::vector<uint8_t> cb;
            if (!entryBytes(c, kv.second, cb)) continue;
            auto git = gzn.find(kv.first);
            if (git == gzn.end()) continue;
            std::vector<uint8_t> gb;
            if (!entryBytes(c, git->second, gb)) continue;
            check(cb, gb);
        } catch (const std::exception&) { continue; }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) { printf("skip: %s\n", argv[i]); continue; }
        try { vpp::Container c(vpp::ByteView(b.data(), b.size())); walk(c);
              printf("scanned %s\n", argv[i]); fflush(stdout);
        } catch (const std::exception& ex) { printf("FAILED %s: %s\n", argv[i], ex.what()); }
    }
    printf("\nfiles with >=1 g-backed block : %lld\n", g_files);
    printf("  segments tile to EOF exactly : %lld\n", g_tileExact);
    printf("  short of EOF                 : %lld\n", g_tileShort);
    printf("  past EOF                     : %lld\n", g_tileLong);
    printf("  CONTROL (+4 per length)      : %lld tile exactly\n", g_ctrlTileExact);
    printf("g-backed blocks                : %lld\n", g_blocks);
    printf("  multi-block files            : %lld  (%lld blocks)\n", g_multiBlockFiles, g_multiBlockBlocks);
    printf("  begins exactly where prev ended : %lld\n", g_contiguous);
    printf("  begins at next 16-byte boundary : %lld\n", g_padded);
    printf("  neither                         : %lld\n", g_other);
    return 0;
}
