// Throwaway diagnostic (this session, 2026-09-30): sanity-checks that the
// bytes this project's own vpp::Container/decompressEntry hands back for a
// given .vint_doc entry are NOT corrupted (spec-vint-doc-format.md Sec1.1
// and the container library's own documented history of a now-fixed mode-
// (a) non-first-entry corruption bug, HANDOFF Sec9.78) before spending more
// effort on structural hypotheses. Does a literal byte-substring search for
// a string spec-vint-doc-format.md Sec3.1 explicitly quotes as resolving
// correctly in ITS OWN independently-written parser (e.g. "vdo_bitmap_viewer"
// for vdo_bitmap_viewer.vint_doc) - if that substring is nowhere in the raw
// bytes at all, the problem is extraction/decompression, not this reader's
// structural model. Also prints payload kind (Raw/Compressed) and decode
// status/diagnostic.
//
// Usage: probe_vintdoc_sanity <archive> <filenameSubstring> <needleString>

#include <algorithm>
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

void search(const vpp::Container& c, const std::string& prefix, const std::string& needleName,
            const std::string& needleStr, int depth) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        if (endsWith(e.name, ".vint_doc") && e.name.find(needleName) != std::string::npos) {
            printf("=== %s%s  kind=%s ===\n", prefix.c_str(), e.name.c_str(),
                   e.payload.kind == vpp::PayloadKind::Raw ? "Raw" : "Compressed");
            std::vector<uint8_t> b;
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                vpp::ByteView r = c.rawEntryBytes(i);
                b.assign(r.data(), r.data() + r.size());
                printf("  raw bytes: %zu\n", b.size());
            } else {
                auto r = c.decompressEntry(i);
                printf("  decompress status=%d diagnostic=%s bytes=%zu\n", (int)r.status,
                       r.diagnostic.c_str(), r.data.size());
                b = std::move(r.data);
            }
            if (!b.empty()) {
                // literal substring search
                auto it = std::search(b.begin(), b.end(), needleStr.begin(), needleStr.end());
                if (it != b.end()) {
                    size_t off = static_cast<size_t>(it - b.begin());
                    printf("  FOUND \"%s\" at byte offset 0x%zx (%zu)\n", needleStr.c_str(), off, off);
                } else {
                    printf("  NOT FOUND: \"%s\" does not appear anywhere in %zu bytes\n", needleStr.c_str(), b.size());
                }
                // dump first 64 bytes hex
                printf("  first 64 bytes:\n   ");
                for (size_t k = 0; k < 64 && k < b.size(); ++k) {
                    printf(" %02X", b[k]);
                    if (k % 16 == 15) printf("\n   ");
                }
                printf("\n");

                // General search: for EVERY plausible "count field" position
                // P from 0x1E through EOF, treat b[P..P+4) as N, compute
                // base=P+4+N*4, and check whether ANY of the N offset-array
                // entries starting at P+4 equals (needleOff-base) - i.e.
                // does base+offsetArray[i] land exactly on the ground-truth
                // needle string spec-vint-doc-format.md Sec3.1 vouches for.
                // This is independent of which header field we THINK is the
                // count.
                auto it2 = std::search(b.begin(), b.end(), needleStr.begin(), needleStr.end());
                if (it2 != b.end()) {
                    size_t needleOff = static_cast<size_t>(it2 - b.begin());
                    printf("  needle at file offset 0x%zx - searching ALL candidate count positions (base=after-array)...\n", needleOff);
                    int totalHits = 0;
                    for (size_t P = 0x1E; P + 4 <= b.size() && totalHits < 20; ++P) {
                        uint32_t N = b[P] | (b[P+1]<<8) | (b[P+2]<<16) | (uint32_t(b[P+3])<<24);
                        if (N == 0 || N > 100000) continue;
                        size_t base = P + 4 + (size_t)N * 4;
                        if (base > b.size() || P + 4 + (size_t)N * 4 > b.size()) continue;
                        if (needleOff < base) continue;
                        long long want = (long long)needleOff - (long long)base;
                        for (uint32_t i = 0; i < N; ++i) {
                            size_t p = P + 4 + (size_t)i * 4;
                            uint32_t v = b[p] | (b[p+1]<<8) | (b[p+2]<<16) | (uint32_t(b[p+3])<<24);
                            if ((long long)v == want) {
                                printf("    P=0x%zx N=%u base=0x%zx offsetArray[%u]=%u MATCH\n", P, N, base, i, v);
                                ++totalHits;
                                break;
                            }
                        }
                    }
                    printf("  total candidate count-positions producing a match (base=after-array): %d\n", totalHits);

                    // Alternative: offsets are ABSOLUTE file offsets
                    // directly (no base addition at all).
                    printf("  scanning for ANY 4-aligned u32 == needleOff (0x%zx) directly (absolute-offset hypothesis)...\n", needleOff);
                    int absHits = 0;
                    for (size_t p = 0x1E; p + 4 <= b.size() && absHits < 20; p += 1) {
                        uint32_t v = b[p] | (b[p+1]<<8) | (b[p+2]<<16) | (uint32_t(b[p+3])<<24);
                        if (v == needleOff) {
                            printf("    abs match @0x%zx == %u\n", p, v);
                            ++absHits;
                        }
                    }
                    printf("  total absolute-offset matches: %d\n", absHits);

                    // Alternative: base = right after the COUNT field only
                    // (i.e. base = P+4, offsets index from the START of the
                    // array itself, not after it).
                    printf("  searching with base=P+4 (start of array) hypothesis...\n");
                    int startHits = 0;
                    for (size_t P = 0x1E; P + 4 <= b.size() && startHits < 20; ++P) {
                        uint32_t N = b[P] | (b[P+1]<<8) | (b[P+2]<<16) | (uint32_t(b[P+3])<<24);
                        if (N == 0 || N > 100000) continue;
                        size_t arrEnd = P + 4 + (size_t)N * 4;
                        if (arrEnd > b.size()) continue;
                        size_t base2 = P + 4;
                        if (needleOff < base2) continue;
                        long long want2 = (long long)needleOff - (long long)base2;
                        for (uint32_t i = 0; i < N; ++i) {
                            size_t p = P + 4 + (size_t)i * 4;
                            uint32_t v = b[p] | (b[p+1]<<8) | (b[p+2]<<16) | (uint32_t(b[p+3])<<24);
                            if ((long long)v == want2) {
                                printf("    P=0x%zx N=%u base2=0x%zx offsetArray[%u]=%u MATCH\n", P, N, base2, i, v);
                                ++startHits;
                                break;
                            }
                        }
                    }
                    printf("  total base=start-of-array matches: %d\n", startHits);
                }
            }
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = c.openNested(i);
                search(nested, prefix + c.entries()[i].name + "/", needleName, needleStr, depth + 1);
            } catch (const std::exception&) {}
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: probe_vintdoc_sanity <archive> <filenameSubstring> <needleString>\n"); return 1; }
    auto archiveBytes = readFile(argv[1]);
    if (archiveBytes.empty()) { printf("could not read archive\n"); return 1; }
    vpp::Container root(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
    search(root, "", argv[2], argv[3], 0);
    return 0;
}
