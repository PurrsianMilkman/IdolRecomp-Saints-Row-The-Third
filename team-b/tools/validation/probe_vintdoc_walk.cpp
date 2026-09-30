// Throwaway investigation tool (this session, 2026-09-30): prototypes the
// FULL structural walk (header -> string pool -> critical resources ->
// metadata -> element tree -> property blocks) against real
// interface_startup.vpp_pc .vint_doc files, to settle two things
// spec-vint-doc-format.md leaves OPEN before committing them to the real
// sr3vintdoc library:
//
//  1. What does the main structural cursor do to get from the string-offset
//     array to the critical-resource section? probe_vintdoc_layout.cpp
//     already found strong evidence that header field +0x16 ("secondary
//     offset", spec Sec2 - explicitly NOT the string-pool base) is instead
//     an ABSOLUTE FILE OFFSET the reader jumps the cursor to, landing
//     exactly on the critical-resource section: for building_purchase.
//     vint_doc and cat_mouse_results.vint_doc (both stringCount=257), that
//     hypothesis produced perfectly in-range, plausible metadata string
//     indices (27/43/60/76/94/109...), while "no skip" and "skip to the
//     max resolved-string reach" both produced billions-range garbage.
//  2. The property block's per-resolution override lookup (spec Sec5): is
//     the baseline/override byte-offset an ABSOLUTE file offset (seek,
//     independent of the main cursor) or something else? And does the main
//     cursor resume from right after the override-lookup header (to read
//     children) regardless of where the baseline tagged-value list
//     physically sits?
//
// The decisive test for both is the same one this project always uses for
// an open structural question (see sr3clmesh::LevelMesh::landsOnEof()):
// walk the WHOLE file on this model and see whether it lands EXACTLY on
// end-of-file, across a real sample - not just the first record.
//
// Usage: probe_vintdoc_walk <interface_startup.vpp_pc> [maxFiles]

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

void walkArchive(const vpp::Container& c, const std::string& prefix,
                  std::vector<std::pair<std::string, std::vector<uint8_t>>>& out, int& remaining) {
    for (size_t i = 0; i < c.entries().size() && remaining > 0; ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".vint_doc")) {
            std::vector<uint8_t> b;
            if (entryBytes(c, i, b) && !b.empty()) {
                out.emplace_back(prefix + n, std::move(b));
                --remaining;
            }
        }
    }
    for (size_t i = 0; i < c.entries().size() && remaining > 0; ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                vpp::Container nested = c.openNested(i);
                walkArchive(nested, prefix + c.entries()[i].name + "/", out, remaining);
            } catch (const std::exception&) {
            }
        }
    }
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
uint8_t rdU8(const std::vector<uint8_t>& b, size_t off) {
    if (off + 1 > b.size()) throw std::out_of_range("rdU8");
    return b[off];
}

struct Cursor {
    const std::vector<uint8_t>& b;
    size_t pos;
    explicit Cursor(const std::vector<uint8_t>& buf, size_t start) : b(buf), pos(start) {}
    uint32_t u32() { uint32_t v = rdU32(b, pos); pos += 4; return v; }
    uint16_t u16() { uint16_t v = rdU16(b, pos); pos += 2; return v; }
    uint8_t u8() { uint8_t v = rdU8(b, pos); pos += 1; return v; }
    void skip(size_t n) { if (pos + n > b.size()) throw std::out_of_range("skip"); pos += n; }
};

// Walks one property block (spec Sec5). `afterHeaderPos` is where the main
// cursor should resume (right after the override-lookup header) regardless
// of where the baseline/override tagged-value lists physically sit -
// hypothesis 2 above. Always takes the baseline branch (no real
// "current resolution" concept - CHOSEN, per task brief). Returns the
// number of tagged values consumed (diagnostic only).
// g_propertyModel: 0 = seek to baselineOffset, restore to afterHeaderPos
// (hypothesis 2 as originally written); 1 = ignore baselineOffset entirely,
// read the tagged-value list sequentially right after the override header.
int g_propertyModel = 0;

int walkPropertyBlock(const std::vector<uint8_t>& b, Cursor& c, int depth, bool verbose) {
    size_t propStart = c.pos;
    uint32_t baselineOffset = c.u32();
    uint8_t overrideCount = c.u8();
    for (uint8_t i = 0; i < overrideCount; ++i) {
        c.u32(); // resolution-name string-pool index
        c.u32(); // override-block byte offset
    }
    size_t afterHeaderPos = c.pos;
    if (verbose) {
        printf("%*s  propBlock@0x%zx: baselineOffset=%u(0x%x) overrideCount=%u afterHeader=0x%zx filesize=0x%zx\n",
               depth * 2, "", propStart, baselineOffset, baselineOffset, overrideCount, afterHeaderPos, b.size());
    }

    int count = 0;
    if (g_propertyModel == 0) {
        Cursor bc(b, baselineOffset);
        while (true) {
            if (count > 5000) throw std::out_of_range("property count limit");
            uint8_t tag = bc.u8();
            if (tag == 0) break;
            bc.u32();
            switch (tag) {
                case 1: bc.skip(4); break;
                case 2: bc.skip(4); break;
                case 3: bc.skip(4); break;
                case 4: bc.skip(4); break;
                case 5: bc.skip(1); break;
                case 6: bc.skip(12); break;
                case 7: bc.skip(8); break;
                default: throw std::out_of_range("unknown property tag");
            }
            ++count;
        }
        c.pos = afterHeaderPos;
    } else {
        while (true) {
            if (count > 5000) throw std::out_of_range("property count limit");
            uint8_t tag = c.u8();
            if (tag == 0) break;
            c.u32();
            switch (tag) {
                case 1: c.skip(4); break;
                case 2: c.skip(4); break;
                case 3: c.skip(4); break;
                case 4: c.skip(4); break;
                case 5: c.skip(1); break;
                case 6: c.skip(12); break;
                case 7: c.skip(8); break;
                default: throw std::out_of_range("unknown property tag");
            }
            ++count;
        }
    }
    return count;
}

struct WalkStats {
    long long elementsWalked = 0;
    long long propertiesWalked = 0;
    long long maxDepth = 0;
};

void walkElement(const std::vector<uint8_t>& b, Cursor& c, int depth, WalkStats& stats, bool verbose) {
    if (depth > 64) throw std::out_of_range("depth limit");
    if (stats.elementsWalked > 20000) throw std::out_of_range("element count limit");
    size_t startPos = c.pos;
    uint32_t typeRef = c.u32();
    uint32_t nameRef = c.u32();
    uint16_t childCount = c.u16();
    uint8_t unexamined = c.u8();
    if (verbose) {
        printf("%*selement@0x%zx: typeRef=%u nameRef=%u childCount=%u unexamined=%u\n", depth * 2, "",
               startPos, typeRef, nameRef, childCount, unexamined);
    }
    stats.elementsWalked++;
    if (depth > stats.maxDepth) stats.maxDepth = depth;
    int nprops = walkPropertyBlock(b, c, depth, verbose);
    stats.propertiesWalked += nprops;
    for (uint16_t i = 0; i < childCount; ++i) {
        walkElement(b, c, depth + 1, stats, verbose);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: probe_vintdoc_walk <interface_startup.vpp_pc> [maxFiles]\n");
        return 1;
    }
    int maxFiles = argc > 2 ? atoi(argv[2]) : 30;
    std::string verboseName = argc > 3 ? argv[3] : "";

    std::vector<uint8_t> archiveBytes = readFile(argv[1]);
    if (archiveBytes.empty()) { printf("could not read %s\n", argv[1]); return 1; }

    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    int remaining = maxFiles;
    try {
        vpp::Container root(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
        walkArchive(root, "", files, remaining);
    } catch (const std::exception& ex) {
        printf("FAILED to open archive: %s\n", ex.what());
        return 1;
    }

    long long total = 0, landedEof = 0, failed = 0;
    for (auto& kv : files) {
        const std::string& name = kv.first;
        const std::vector<uint8_t>& b = kv.second;
        ++total;
        try {
            if (b.size() < 0x1E) throw std::out_of_range("too small");
            uint32_t magic = rdU32(b, 0x00);
            if (magic != 0x00003027u) throw std::out_of_range("bad magic");
            uint16_t version = rdU16(b, 0x08);
            uint32_t metaCount = rdU32(b, 0x0E);
            uint32_t critCount = rdU32(b, 0x12);
            uint32_t secOffset = rdU32(b, 0x16);
            uint16_t elemCount = rdU16(b, 0x1A);
            uint16_t animCount = rdU16(b, 0x1C);

            Cursor c(b, 0x1E);
            uint32_t strCount = c.u32();
            std::vector<uint32_t> offsets(strCount);
            for (uint32_t i = 0; i < strCount; ++i) offsets[i] = c.u32();
            // hypothesis 1: jump to header secOffset for critical resources
            c.pos = secOffset;

            for (uint32_t i = 0; i < critCount; ++i) {
                c.u8();   // selector
                c.u32();  // raw value
                if (version == 2) c.u8();
            }
            for (uint32_t i = 0; i < metaCount; ++i) {
                c.u32(); // name index
                c.u32(); // value index
            }

            bool verboseThis = verboseName.empty() ? (total <= 1) : (name.find(verboseName) != std::string::npos);
            if (verboseThis) {
                // Brute-force search: try every byte position from just after
                // the string offset array (base) through EOF as a candidate
                // element-tree start, and see if the FULL elemCount+animCount
                // recursive walk completes and lands exactly on EOF. This
                // sidesteps every assumption about what's between the string
                // pool and the element tree (critical resources, metadata,
                // string character data) - it just finds where the element
                // grammar itself is self-consistent all the way to EOF.
                size_t base2 = 0x1E + 4 + (size_t)strCount * 4;
                for (int model = 0; model <= 1; ++model) {
                    g_propertyModel = model;
                    printf("  BRUTE FORCE SEARCH model=%d for element-tree start (base=0x%zx .. EOF=0x%zx):\n",
                           model, base2, b.size());
                    int hits = 0;
                    long long bestDelta = 1LL << 60;
                    size_t bestPos = 0, bestCand = 0;
                    for (size_t cand = base2; cand < b.size(); ++cand) {
                        try {
                            Cursor tc(b, cand);
                            WalkStats st;
                            for (uint16_t i = 0; i < elemCount; ++i) walkElement(b, tc, 0, st, false);
                            for (uint16_t i = 0; i < animCount; ++i) walkElement(b, tc, 0, st, false);
                            long long delta = (long long)b.size() - (long long)tc.pos;
                            if (delta == 0 && hits < 10) {
                                printf("    HIT @0x%zx (secOffset delta=%lld, base delta=%lld): elems=%lld props=%lld maxDepth=%lld\n",
                                       cand, (long long)cand - (long long)secOffset, (long long)cand - (long long)base2,
                                       st.elementsWalked, st.propertiesWalked, st.maxDepth);
                                ++hits;
                            } else if (llabs(delta) <= 200 && hits < 30) {
                                printf("    near @0x%zx -> end 0x%zx (delta=%lld, secOffset delta=%lld): elems=%lld props=%lld\n",
                                       cand, tc.pos, delta, (long long)cand - (long long)secOffset,
                                       st.elementsWalked, st.propertiesWalked);
                            }
                            if (llabs(delta) < llabs(bestDelta)) { bestDelta = delta; bestPos = tc.pos; bestCand = cand; }
                        } catch (const std::exception&) {
                        }
                    }
                    if (hits == 0) {
                        printf("    NO EXACT HIT. Best near-miss: start@0x%zx -> ended@0x%zx, delta=%lld (secOffset delta=%lld)\n",
                               bestCand, bestPos, bestDelta, (long long)bestCand - (long long)secOffset);
                    }
                }
                g_propertyModel = 0;
            }
            if (verboseThis) {
                printf("=== %s: version=%u metaCount=%u critCount=%u secOffset=0x%x elemCount=%u animCount=%u strCount=%u\n",
                       name.c_str(), version, metaCount, critCount, secOffset, elemCount, animCount, strCount);
                printf("    cursor after critRes+metadata = 0x%zx / filesize 0x%zx\n", c.pos, b.size());
                // Scan nearby byte-aligned positions for a plausible element
                // record start: typeRef/nameRef both < strCount, childCount
                // small, unexamined byte small.
                size_t scanStart = c.pos;
                size_t scanEnd = b.size();
                for (size_t cand = scanStart; cand + 11 <= scanEnd; ++cand) {
                    uint32_t t = rdU32(b, cand);
                    uint32_t nm = rdU32(b, cand + 4);
                    uint16_t cc = rdU16(b, cand + 8);
                    uint8_t un = rdU8(b, cand + 10);
                    if (t > 0 && t < strCount && nm < strCount && un < 4) {
                        printf("    candidate element start @0x%zx (delta %lld): typeRef=%u nameRef=%u childCount=%u unexamined=%u\n",
                               cand, (long long)cand - (long long)c.pos, t, nm, cc, un);
                    }
                }
                // Also print the secOffset->base gap and the raw bytes
                // right at secOffset itself for hand inspection.
                printf("    base=0x%zx secOffset=0x%x gap(secOffset-base)=%lld\n", 0x1E + 4 + (size_t)strCount * 4,
                       secOffset, (long long)secOffset - (long long)(0x1E + 4 + (size_t)strCount * 4));
                printf("    as u32 words from secOffset to EOF (%zu words):\n", (b.size() - secOffset) / 4);
                int col = 0;
                for (size_t i = secOffset; i + 4 <= b.size(); i += 4) {
                    printf("%6u", rdU32(b, i));
                    if (++col % 12 == 0) printf("\n");
                }
                printf("\n");
                printf("    first 20 offset-array entries: ");
                for (uint32_t i = 0; i < offsets.size() && i < 20; ++i) printf("%u ", offsets[i]);
                printf("\n    last 20 offset-array entries: ");
                for (uint32_t i = (offsets.size() > 20 ? (uint32_t)offsets.size() - 20 : 0); i < offsets.size(); ++i)
                    printf("%u ", offsets[i]);
                printf("\n");
                uint32_t maxOff = 0;
                for (uint32_t v : offsets) if (v > maxOff && v < 100000) maxOff = v;
                printf("    max offset-array entry (excluding outliers>=100000) = %u\n", maxOff);
            }
            WalkStats stats;
            for (uint16_t i = 0; i < elemCount; ++i) walkElement(b, c, 0, stats, verboseThis);
            for (uint16_t i = 0; i < animCount; ++i) walkElement(b, c, 0, stats, verboseThis);

            if (c.pos == b.size()) {
                ++landedEof;
            } else {
                printf("  %-40s: walk finished at 0x%zx, file size 0x%zx (delta %lld) elems=%lld props=%lld\n",
                       name.c_str(), c.pos, b.size(), (long long)c.pos - (long long)b.size(),
                       stats.elementsWalked, stats.propertiesWalked);
            }
        } catch (const std::exception& ex) {
            ++failed;
            printf("  %-40s: FAILED - %s\n", name.c_str(), ex.what());
        }
    }

    printf("\n=== probe_vintdoc_walk summary ===\n");
    printf("total=%lld landedEof=%lld failed=%lld notLanded=%lld\n", total, landedEof, failed,
           total - landedEof - failed);
    return 0;
}
