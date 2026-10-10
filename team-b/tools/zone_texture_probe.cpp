// Throwaway investigation tool (2026-10-08, road/sidewalk texture
// resolution task): empirically locates, for a real `.czn_pc`/`.gzn_pc`
// ground-tile pair, WHERE the real `.tga` texture name(s) spec-zone-
// data-format.md Sec13.2 found ("scanned that record's own declared body
// span for its embedded .tga texture-name pool") sit relative to the
// located `0x2237` Mesh sub-block(s) this project's own
// sr3zone::ZoneGeometry::locate() already finds.
//
// WHY THIS TOOL EXISTS: this project's existing zone/terrain render path
// (tools/sr3_viewer.cpp runZoneTile()/loadCityGroundTile()) draws real
// `.czn_pc` ground geometry with MeshDrawMode::MaterialIdAsColour because
// the TWO texture-binding mechanisms it already checked - the vehicle-
// style `0x424BD00D` GeometryBlock sub-header (material_binding.h) and
// `.czh_pc`'s own shared MaterialBlock name table (sr3zone::ZoneHeader::
// materialBlock()) - are both genuine, measured dead ends for `.czn_pc`
// (0/2,971 files; 71/71 non-texture names respectively). Sec13.2 is a
// THIRD, newer finding that real `.tga` names DO exist somewhere in a
// `0x2237` record's own body - but it does not state a decoded byte-level
// path to them (that scan was a standalone population survey, not a
// structural field decode), and this project's own `.czn_pc` top-level
// `{id,length}` record chain is explicitly unread anywhere in this
// codebase (sr3zone::zone_header.h's own file comment: "there is no
// reader for it here, or anywhere in this project, deliberately" - the
// chain is HYPOTHESIS-unconfirmed and a controlled negative, spec-zone-
// data-format.md Sec2/Sec3/Sec8.1). So there is no existing reader that
// can hand this project "the 0x2237 record's declared body span" directly.
//
// This tool instead empirically tests the SAME general shape this
// project's OWN other Mesh-sub-block carriers already use for their real,
// CONFIRMED per-material texture-binding record: `.clmesh_pc`/vehicles/
// characters/foliage/trees all put their per-material record (built by the
// shared primitive spec-zone-data-format.md Sec13.1 cites for 0x2237 too,
// `0xe71090`) EITHER immediately past a Mesh sub-block's own end
// (material_binding.h's own documented convention: "Each material owns a
// record past the end of the Mesh sub-block...") OR in a material-SET
// block located before it (sr3tree::Tree, sr3clmesh::LevelMesh's MIDDLE).
// Both conventions put the real name pool CLOSE to the Mesh sub-block, in
// the SAME file, not scattered arbitrarily - so a whole-file scan for
// EVERY located Mesh sub-block AND every NUL-terminated ".tga"-suffixed
// ASCII run, cross-tabulated by byte distance, should reveal which (if
// either) convention this content-kind actually follows, directly against
// real shipped bytes - no executable read, no guess.
//
// Never asserts pass/fail. Prints a table for a human (or another agent)
// to read, same convention as tools/zone_objects_population.cpp and
// tools/clmesh_probe.cpp.
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sr3geometry/material_block.h"
#include "sr3zone/zone_geometry.h"
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

bool toLowerEndsWith(const std::string& hay, const std::string& needleLower) {
    if (hay.size() < needleLower.size()) return false;
    for (size_t i = 0; i < needleLower.size(); ++i) {
        char c = hay[hay.size() - needleLower.size() + i];
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(c))) != needleLower[i]) return false;
    }
    return true;
}

struct NameHit {
    size_t offset = 0;   // absolute offset of the FIRST byte of the name
    size_t length = 0;   // not counting the NUL
    std::string text;
};

// Whole-buffer scan for NUL-terminated, printable-ASCII runs ending in
// ".tga" (case-insensitive) - the SAME shape Sec13.2's own method found
// (a real .tga name pool), applied here with no assumption about WHERE in
// the file it sits. A run must be entirely printable ASCII (0x20-0x7e) and
// at least 5 bytes (shortest real name would be "x.tga") to count as a
// candidate; this rejects stray coincidental bytes without asserting any
// particular structural boundary rule (deliberately looser than material_
// binding.h's own "lands on a name BOUNDARY" check, since this tool's own
// job is to test whether such a boundary rule is even findable here, not
// to assume one).
std::vector<NameHit> findTgaNames(const std::vector<uint8_t>& buf) {
    std::vector<NameHit> hits;
    size_t runStart = std::string::npos;
    for (size_t i = 0; i <= buf.size(); ++i) {
        bool printable = i < buf.size() && buf[i] >= 0x20 && buf[i] < 0x7f;
        if (printable) {
            if (runStart == std::string::npos) runStart = i;
        } else {
            if (runStart != std::string::npos) {
                size_t len = i - runStart;
                if (len >= 5) {
                    std::string s(reinterpret_cast<const char*>(buf.data() + runStart), len);
                    if (toLowerEndsWith(s, ".tga")) {
                        hits.push_back({runStart, len, s});
                    }
                }
                runStart = std::string::npos;
            }
        }
    }
    return hits;
}

int g_filesReported = 0;
constexpr int kMaxFilesReported = 40;
bool g_hexDump = false;

void hexDumpRegion(const std::vector<uint8_t>& buf, size_t start, size_t end) {
    start = start > 16 ? start - 16 : 0;
    end = (std::min)(end + 16, buf.size());
    for (size_t row = start - (start % 16); row < end; row += 16) {
        printf("    %6zu: ", row);
        for (size_t i = 0; i < 16; ++i) {
            size_t p = row + i;
            if (p < buf.size() && p < end) printf("%02x ", buf[p]);
            else printf("   ");
        }
        printf(" |");
        for (size_t i = 0; i < 16; ++i) {
            size_t p = row + i;
            if (p < buf.size() && p < end) {
                char c = static_cast<char>(buf[p]);
                printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
            }
        }
        printf("|\n");
    }
}

void checkPair(const std::string& label, const std::vector<uint8_t>& cb, const std::vector<uint8_t>& gb) {
    auto blocks = sr3zone::ZoneGeometry::locate(sr3zone::ByteView(cb.data(), cb.size()),
                                                 sr3zone::ByteView(gb.data(), gb.size()));
    auto names = findTgaNames(cb);
    if (blocks.empty() && names.empty()) return; // nothing here at all - not interesting
    if (g_filesReported >= kMaxFilesReported) return;
    ++g_filesReported;

    printf("\n=== %s ===\n", label.c_str());
    printf("  czn size=%zu gzn size=%zu\n", cb.size(), gb.size());

    // NEW HYPOTHESIS TEST: does `.czn_pc` embed its OWN copy of the shared
    // "material reference block" (sr3geometry::MaterialBlock, magic
    // 0x00043854 - the SAME general-purpose block .ccmesh_pc/.clmesh_pc/
    // .matlib_pc/.czh_pc already embed verbatim at THEIR OWN start) at
    // byte 0 of THIS file, separately from .czh_pc's own copy (which
    // runZoneTile()'s own doc comment already measured as 71 names, zero
    // .tga, for the file it checked)? If so, this project's EXISTING,
    // already-confirmed reader (no new parser needed) hands back exactly
    // the name list the whole-buffer .tga scan below is finding anyway.
    bool matBlockOk = false;
    sr3geometry::MaterialBlock matBlock;
    size_t matBlockOffset = 0;
    // Search the first 4KB for the magic directly (4-aligned), in case it
    // is not at byte 0 of this file (unlike .ccmesh_pc/.clmesh_pc/.matlib_pc,
    // which is where the SHARED block's own doc comment says it always
    // starts) - .czn_pc may prefix it with a small chain/record header.
    std::vector<size_t> magicOffsets;
    for (size_t off = 0; off + 4 <= cb.size() && off < 4096; off += 4) {
        uint32_t v = static_cast<uint32_t>(cb[off]) | (static_cast<uint32_t>(cb[off + 1]) << 8) |
                     (static_cast<uint32_t>(cb[off + 2]) << 16) | (static_cast<uint32_t>(cb[off + 3]) << 24);
        if (v == sr3geometry::kMaterialBlockMagic) magicOffsets.push_back(off);
    }
    printf("  magic 0x00043854 (4-aligned, first 4KB) found at offsets:");
    for (size_t o : magicOffsets) printf(" %zu", o);
    printf(" %s\n", magicOffsets.empty() ? "(none)" : "");
    try {
        matBlock = sr3geometry::MaterialBlock::parse(sr3geometry::ByteView(cb.data(), cb.size()));
        matBlockOk = true;
    } catch (const std::exception& ex) {
        printf("  czn-own leading MaterialBlock at offset 0: FAILED to parse: %s\n", ex.what());
    }
    if (!matBlockOk) {
        for (size_t o : magicOffsets) {
            try {
                matBlock = sr3geometry::MaterialBlock::parse(
                    sr3geometry::ByteView(cb.data() + o, cb.size() - o));
                matBlockOk = true;
                matBlockOffset = o;
                printf("  czn-own MaterialBlock PARSED OK at offset %zu\n", o);
                break;
            } catch (const std::exception& ex) {
                printf("  czn-own MaterialBlock at offset %zu: FAILED: %s\n", o, ex.what());
            }
        }
    }
    if (matBlockOk) {
        const size_t matBlockEnd = matBlockOffset + matBlock.totalSize;
        printf("  czn-own MaterialBlock: OK, blockOffset=%zu textureSlotCount=%u totalSize=%zu end=%zu "
               "(first Mesh block at %zu, gap=%lld)\n",
               matBlockOffset, matBlock.textureSlotCount, matBlock.totalSize, matBlockEnd,
               blocks.empty() ? size_t(0) : blocks[0].cznOffset,
               blocks.empty() ? 0LL
                              : static_cast<long long>(blocks[0].cznOffset) - static_cast<long long>(matBlockEnd));
        for (size_t i = 0; i < matBlock.textureNames.size() && i < 400; ++i) {
            printf("    name[%zu] = \"%s\"\n", i, matBlock.textureNames[i].c_str());
        }
    }
    printf("  Mesh sub-blocks located: %zu\n", blocks.size());
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& e = blocks[bi];
        size_t end = e.cznOffset + e.block.cLength();
        printf("    block[%zu]: cznOffset=%zu cLength=%u (end=%zu) fromGFile=%d gznOffset=%zu "
               "channels=%zu drawGroupsLocated=%d\n",
               bi, e.cznOffset, e.block.cLength(), end, e.fromGFile ? 1 : 0, e.gznOffset,
               e.block.channels().size(), e.block.drawGroupsLocated() ? 1 : 0);
    }
    // For every block with a layout-code-0/2 (ground-renderable) channel,
    // print that channel's own distinct draw-range materialIds and,
    // IF matBlockOk, what name sits at that exact index in the czn-own
    // leading MaterialBlock - a direct, falsifiable test of "materialId
    // indexes this block's own name table positionally", the same shape
    // of hypothesis runZoneTile()'s own doc comment already tested (and
    // refuted) against .czh_pc's OWN much smaller table - never assumed
    // true here, only reported for a human to judge semantic plausibility
    // (does a ground channel's materialId land on a road/sidewalk/asphalt-
    // sounding name, or on something unrelated like a window/roof texture?).
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& channels = blocks[bi].block.channels();
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            if (channels[ci].layoutCode != 0 && channels[ci].layoutCode != 2) continue;
            std::map<uint32_t, size_t> matIdCount;
            if (blocks[bi].block.drawGroupsLocated()) {
                for (const auto& group : blocks[bi].block.drawGroups()) {
                    for (const auto& dr : group) {
                        if (dr.submeshIndex != ci) continue;
                        matIdCount[dr.materialId]++;
                    }
                }
            }
            printf("  block[%zu] channel[%zu] (layoutCode=%u): %zu distinct materialId(s) in its own draw ranges:\n",
                   bi, ci, channels[ci].layoutCode, matIdCount.size());
            for (const auto& kv : matIdCount) {
                printf("    materialId=%u (used in %zu range(s))", kv.first, kv.second);
                if (matBlockOk && kv.first < matBlock.textureNames.size()) {
                    printf("  -> name[%u] = \"%s\"", kv.first, matBlock.textureNames[kv.first].c_str());
                } else if (matBlockOk) {
                    printf("  -> OUT OF RANGE (table has %zu names)", matBlock.textureNames.size());
                }
                printf("\n");
            }
        }
    }

    if (g_hexDump && !names.empty()) {
        printf("  hex dump from file start through the first name pool entry:\n");
        hexDumpRegion(cb, 0, (std::min)(names.front().offset + 32, cb.size()));
    }
    printf("  .tga name(s) found anywhere in .czn_pc content: %zu\n", names.size());
    for (const auto& n : names) {
        printf("    offset=%zu len=%zu text=\"%s\"", n.offset, n.length, n.text.c_str());
        // distance to the nearest located block's start/end, signed: positive
        // means the name sits AFTER that boundary.
        if (!blocks.empty()) {
            long long bestDist = 0;
            size_t bestIdx = 0;
            bool have = false;
            for (size_t bi = 0; bi < blocks.size(); ++bi) {
                size_t start = blocks[bi].cznOffset;
                size_t end = start + blocks[bi].block.cLength();
                long long dStart = static_cast<long long>(n.offset) - static_cast<long long>(start);
                long long dEnd = static_cast<long long>(n.offset) - static_cast<long long>(end);
                long long d = (std::abs)(dEnd) < (std::abs)(dStart) ? dEnd : dStart;
                if (!have || (std::abs)(d) < (std::abs)(bestDist)) {
                    bestDist = d;
                    bestIdx = bi;
                    have = true;
                }
            }
            printf("  nearest block=%zu distFromEnd=%lld", bestIdx,
                   static_cast<long long>(n.offset) -
                       static_cast<long long>(blocks[bestIdx].cznOffset + blocks[bestIdx].block.cLength()));
        }
        printf("\n");
    }
}

void walk(const vpp::Container& c, const std::string& containerLabel, const std::string& filter) {
    std::map<std::string, size_t> czn;
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        std::string stem;
        if (stemFor(n, ".czn_pc", stem)) {
            if (filter.empty() || stem.find(filter) != std::string::npos) czn[stem] = i;
        }
    }
    for (const auto& kv : czn) {
        try {
            std::vector<uint8_t> cb;
            if (!entryBytes(c, kv.second, cb) || cb.empty()) continue;
            std::string gznName = kv.first + ".gzn_pc";
            std::vector<uint8_t> gb;
            for (size_t j = 0; j < c.entries().size(); ++j) {
                if (c.entries()[j].name == gznName) {
                    entryBytes(c, j, gb);
                    break;
                }
            }
            checkPair(containerLabel + "/" + kv.first + ".czn_pc", cb, gb);
        } catch (const std::exception& ex) {
            printf("  [skip %s: %s]\n", kv.first.c_str(), ex.what());
        }
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                walk(c.openNested(i), containerLabel + "/" + c.entries()[i].name, filter);
            } catch (const std::exception&) {
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: zone_texture_probe <archive.vpp_pc> [more archives...] [--filter <substr>]\n");
        return 1;
    }
    std::string filter;
    std::vector<std::string> archives;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--filter" && i + 1 < argc) {
            filter = argv[++i];
        } else if (a == "--hexdump") {
            g_hexDump = true;
        } else {
            archives.push_back(a);
        }
    }
    for (const auto& path : archives) {
        std::vector<uint8_t> b = readFile(path);
        if (b.empty()) {
            printf("skip (empty/unreadable): %s\n", path.c_str());
            continue;
        }
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            printf("scanning %s (filter=\"%s\")\n", path.c_str(), filter.c_str());
            walk(c, path, filter);
            fflush(stdout);
        } catch (const std::exception& ex) {
            printf("FAILED to open %s: %s\n", path.c_str(), ex.what());
        }
    }
    printf("\n(files reported: %d, cap=%d)\n", g_filesReported, kMaxFilesReported);
    return 0;
}
