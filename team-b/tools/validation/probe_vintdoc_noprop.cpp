// Throwaway diagnostic (this session, 2026-09-30): isolates whether the
// bare element-header shape (typeRef u32, nameRef u32, childCount u16,
// unexamined u8 - spec-vint-doc-format.md Sec4, NO property block at all)
// can ever land exactly on EOF for elemCount+animCount top-level records,
// scanning every byte position. If even THIS minimal skeleton never lands,
// the bug is in elemCount/animCount/childCount itself, not in the property
// block grammar (probe_vintdoc_bruteforce.cpp already ruled out 8 property
// grammar variants with the real header shape).
//
// Usage: probe_vintdoc_noprop <archive> <filenameSubstring> [scanFromHex]

#include <cstdint>
#include <cstdio>
#include <fstream>
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
bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
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
bool findFile(const vpp::Container& c, const std::string& needle, std::vector<uint8_t>& out, std::string& foundName) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".vint_doc") && n.find(needle) != std::string::npos) {
            if (entryBytes(c, i, out)) { foundName = n; return true; }
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { vpp::Container nested = c.openNested(i); if (findFile(nested, needle, out, foundName)) return true; }
            catch (const std::exception&) {}
        }
    }
    return false;
}
uint32_t rdU32(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) throw std::out_of_range("u32");
    return b[o] | (b[o+1]<<8) | (b[o+2]<<16) | (uint32_t(b[o+3])<<24);
}
uint16_t rdU16(const std::vector<uint8_t>& b, size_t o) {
    if (o + 2 > b.size()) throw std::out_of_range("u16");
    return static_cast<uint16_t>(b[o] | (b[o+1]<<8));
}
struct Cursor {
    const std::vector<uint8_t>& b; size_t pos;
    Cursor(const std::vector<uint8_t>& buf, size_t s): b(buf), pos(s) {}
    uint32_t u32(){uint32_t v=rdU32(b,pos);pos+=4;return v;}
    uint16_t u16(){uint16_t v=rdU16(b,pos);pos+=2;return v;}
    uint8_t u8(){if(pos+1>b.size())throw std::out_of_range("u8");return b[pos++];}
};
long long g_budget;
void walkBare(const std::vector<uint8_t>& b, Cursor& c, int depth) {
    if (depth > 64) throw std::out_of_range("depth");
    if (--g_budget < 0) throw std::out_of_range("budget");
    c.u32(); c.u32();
    uint16_t cc = c.u16();
    c.u8();
    for (uint16_t i = 0; i < cc; ++i) walkBare(b, c, depth + 1);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: probe_vintdoc_noprop <archive> <filenameSubstring> [scanFromHex]\n"); return 1; }
    auto archiveBytes = readFile(argv[1]);
    std::vector<uint8_t> b; std::string foundName;
    vpp::Container root(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
    if (!findFile(root, argv[2], b, foundName)) { printf("not found\n"); return 1; }
    printf("file: %s (%zu bytes)\n", foundName.c_str(), b.size());
    uint16_t elemCount = rdU16(b, 0x1A);
    uint16_t animCount = rdU16(b, 0x1C);
    uint32_t strCount = rdU32(b, 0x1E);
    size_t base = 0x1E + 4 + (size_t)strCount*4;
    size_t scanFrom = argc > 3 ? strtoul(argv[3], nullptr, 0) : 0x1E;
    printf("elemCount=%u animCount=%u strCount=%u base=0x%zx scanFrom=0x%zx filesize=0x%zx\n",
           elemCount, animCount, strCount, base, scanFrom, b.size());
    long long hits = 0, noThrow = 0;
    long long bestDelta = 1LL << 60;
    size_t bestCand = 0, bestEnd = 0;
    for (size_t cand = scanFrom; cand < b.size(); ++cand) {
        g_budget = 20000;
        try {
            Cursor c(b, cand);
            for (uint16_t i = 0; i < elemCount; ++i) walkBare(b, c, 0);
            for (uint16_t i = 0; i < animCount; ++i) walkBare(b, c, 0);
            ++noThrow;
            long long delta = (long long)b.size() - (long long)c.pos;
            printf("  noThrow @0x%zx -> end 0x%zx delta=%lld\n", cand, c.pos, delta);
            if (llabs(delta) < llabs(bestDelta)) { bestDelta = delta; bestCand = cand; bestEnd = c.pos; }
            if (c.pos == b.size()) {
                printf("HIT bare-skeleton @0x%zx\n", cand);
                if (++hits >= 20) break;
            }
        } catch (const std::exception&) {}
    }
    printf("total bare-skeleton hits (no property block at all): %lld, noThrow=%lld\n", hits, noThrow);
    printf("best near-miss: start@0x%zx end@0x%zx delta=%lld\n", bestCand, bestEnd, bestDelta);
    return 0;
}
