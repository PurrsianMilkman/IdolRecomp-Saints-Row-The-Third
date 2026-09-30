// Throwaway investigation tool (this session, 2026-09-30): systematically
// tries combinations of element/property-record grammar variants against a
// single real .vint_doc file, searching EVERY byte position from right
// after the string-offset array through EOF as a candidate element-tree
// start, to find one where the documented elemCount+animCount recursive
// walk completes and lands EXACTLY on end-of-file (spec-vint-doc-format.md
// Sec4/Sec5). probe_vintdoc_walk.cpp's own brute force (element record
// exactly as spec Sec4/Sec5 states, in both property-offset interpretations)
// found ZERO hits and ZERO near-misses (within 200 bytes of EOF) anywhere
// in building_purchase.vint_doc - strong evidence the bug is in the
// element/property GRAMMAR itself, not just the starting offset. This tool
// widens the search across the shape variants spec's own prose leaves room
// for:
//   - child count as u16 (spec-stated) or u32
//   - per-property record as [tag][hash][value] (spec's table order) or
//     [hash][tag][value]
//   - property baseline offset as an independent seek (restored afterward)
//     or read sequentially in place (baseline offset field simply skipped)
//
// Usage: probe_vintdoc_bruteforce <interface_startup.vpp_pc> <filenameSubstring>

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
bool endsWith(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
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
            try {
                vpp::Container nested = c.openNested(i);
                if (findFile(nested, needle, out, foundName)) return true;
            } catch (const std::exception&) {}
        }
    }
    return false;
}

uint32_t rdU32(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) throw std::out_of_range("rdU32");
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}
uint16_t rdU16(const std::vector<uint8_t>& b, size_t off) {
    if (off + 2 > b.size()) throw std::out_of_range("rdU16");
    return static_cast<uint16_t>(static_cast<uint16_t>(b[off]) | (static_cast<uint16_t>(b[off + 1]) << 8));
}

struct Cursor {
    const std::vector<uint8_t>& b;
    size_t pos;
    explicit Cursor(const std::vector<uint8_t>& buf, size_t start) : b(buf), pos(start) {}
    uint32_t u32() { uint32_t v = rdU32(b, pos); pos += 4; return v; }
    uint16_t u16() { uint16_t v = rdU16(b, pos); pos += 2; return v; }
    uint8_t u8() { if (pos + 1 > b.size()) throw std::out_of_range("u8"); uint8_t v = b[pos]; pos += 1; return v; }
    void skip(size_t n) { if (pos + n > b.size()) throw std::out_of_range("skip"); pos += n; }
};

struct Variant {
    bool childCountU32;      // false = u16 (spec), true = u32
    bool hashBeforeTag;      // false = tag then hash (spec table order), true = hash then tag
    bool propertySequential; // false = seek to baselineOffset + restore, true = ignore offset, read in place
    long long elementBudget; // remaining element-record budget (safety)
};

void walkProps(const std::vector<uint8_t>& b, Cursor& c, Variant& v) {
    size_t afterHeaderStart = c.pos;
    uint32_t baselineOffset = c.u32();
    uint8_t overrideCount = c.u8();
    for (uint8_t i = 0; i < overrideCount; ++i) { c.u32(); c.u32(); }
    size_t afterHeaderPos = c.pos;
    (void)afterHeaderStart;

    Cursor* pc = &c;
    Cursor seeked(b, baselineOffset);
    if (!v.propertySequential) pc = &seeked;

    int count = 0;
    while (true) {
        if (++count > 3000) throw std::out_of_range("prop limit");
        uint8_t tag; uint32_t hash;
        if (!v.hashBeforeTag) {
            tag = pc->u8();
            if (tag == 0) break;
            hash = pc->u32();
        } else {
            hash = pc->u32();
            tag = pc->u8();
            if (tag == 0) break;
        }
        (void)hash;
        switch (tag) {
            case 1: pc->skip(4); break;
            case 2: pc->skip(4); break;
            case 3: pc->skip(4); break;
            case 4: pc->skip(4); break;
            case 5: pc->skip(1); break;
            case 6: pc->skip(12); break;
            case 7: pc->skip(8); break;
            default: throw std::out_of_range("bad tag");
        }
    }
    if (!v.propertySequential) c.pos = afterHeaderPos;
}

void walkElement(const std::vector<uint8_t>& b, Cursor& c, int depth, Variant& v) {
    if (depth > 64) throw std::out_of_range("depth");
    if (--v.elementBudget < 0) throw std::out_of_range("budget");
    c.u32(); // typeRef
    c.u32(); // nameRef
    uint32_t childCount;
    if (v.childCountU32) { childCount = c.u32(); }
    else { childCount = c.u16(); c.u8(); /* unexamined byte, only in u16 variant */ }
    walkProps(b, c, v);
    for (uint32_t i = 0; i < childCount; ++i) walkElement(b, c, depth + 1, v);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: probe_vintdoc_bruteforce <archive> <filenameSubstring>\n"); return 1; }
    std::vector<uint8_t> archiveBytes = readFile(argv[1]);
    if (archiveBytes.empty()) { printf("could not read %s\n", argv[1]); return 1; }
    std::vector<uint8_t> b;
    std::string foundName;
    try {
        vpp::Container root(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
        if (!findFile(root, argv[2], b, foundName)) { printf("not found: %s\n", argv[2]); return 1; }
    } catch (const std::exception& ex) { printf("error: %s\n", ex.what()); return 1; }

    printf("file: %s (%zu bytes)\n", foundName.c_str(), b.size());
    if (b.size() < 0x1E) { printf("too small\n"); return 1; }
    uint32_t magic = rdU32(b, 0);
    if (magic != 0x00003027u) { printf("bad magic\n"); return 1; }
    uint16_t elemCount = rdU16(b, 0x1A);
    uint16_t animCount = rdU16(b, 0x1C);
    uint32_t strCount = rdU32(b, 0x1E);
    size_t base = 0x1E + 4 + (size_t)strCount * 4;
    printf("elemCount=%u animCount=%u strCount=%u base=0x%zx filesize=0x%zx\n", elemCount, animCount, strCount,
           base, b.size());
    size_t scanFrom = (argc > 3) ? (size_t)strtoul(argv[3], nullptr, 0) : base;
    if (scanFrom > b.size()) { printf("scanFrom past EOF, aborting\n"); return 1; }
    printf("scanning from 0x%zx\n", scanFrom);

    for (int cc = 0; cc <= 1; ++cc) {
        for (int ho = 0; ho <= 1; ++ho) {
            for (int ps = 0; ps <= 1; ++ps) {
                long long hits = 0;
                size_t firstHit = 0;
                for (size_t cand = scanFrom; cand < b.size(); ++cand) {
                    Variant v{cc != 0, ho != 0, ps != 0, 20000};
                    try {
                        Cursor c(b, cand);
                        for (uint16_t i = 0; i < elemCount; ++i) walkElement(b, c, 0, v);
                        for (uint16_t i = 0; i < animCount; ++i) walkElement(b, c, 0, v);
                        if (c.pos == b.size()) {
                            if (hits == 0) firstHit = cand;
                            ++hits;
                        }
                    } catch (const std::exception&) {
                    }
                }
                printf("childCountU32=%d hashBeforeTag=%d propertySequential=%d -> hits=%lld%s\n", cc, ho, ps, hits,
                       hits > 0 ? (std::string("  first@0x") + std::to_string(firstHit)).c_str() : "");
            }
        }
    }
    return 0;
}
