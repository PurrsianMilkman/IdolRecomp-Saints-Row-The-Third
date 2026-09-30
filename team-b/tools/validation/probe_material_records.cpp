// Probe the per-material binding region, before implementing anything.
//
// §9.22.1 requires that whoever builds texture binding re-derives the
// run-location heuristic AND its guard against the shipped archives, and
// reports the AMBIGUITY COUNT alongside the hit rate rather than instead
// of it. This is that step.
//
// Structure per §9.22 (relayed, being verified here):
//   subheader      = GeometryBlock::offset()        (0x424BD00D, verified)
//   materialCount  = u16(subheader + 0x0C)          (verified 549/549)
//   name blob      = subheader + 0x88, length-prefixed, MIXED case
//   per-material records: past the end of the Mesh sub-block
//   within a record: a run of 12-byte entries
//       +0x00 u32 byte offset into the name blob
//       +0x04 u32 parameter-name hash
//       +0x08 u32 slot index, sequential from 0
//
// This prints raw structure for a few meshes so the layout can be seen
// before any decode is committed to.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3mesh/mesh_block.h"
#include "vpp/container.h"

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    bool ok = r.status == vpp::DecodeStatus::Ok ||
              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
              r.status == vpp::DecodeStatus::ContentValidated ||
              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}
static uint32_t u32at(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) return 0;
    return static_cast<uint32_t>(b[o] | (b[o+1] << 8) | (b[o+2] << 16) |
                                 (static_cast<uint32_t>(b[o+3]) << 24));
}
static uint16_t u16at(const std::vector<uint8_t>& b, size_t o) {
    if (o + 2 > b.size()) return 0;
    return static_cast<uint16_t>(b[o] | (b[o+1] << 8));
}

int g_done = 0;

void probe(const std::string& name, const std::vector<uint8_t>& cb,
           const std::vector<uint8_t>& gb) {
    if (g_done >= 3) return;
    try {
        sr3geometry::MaterialBlock mat =
            sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
        sr3geometry::GeometryBlock geo =
            sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), mat);
        if (!geo.hasMeshSubBlock()) return;
        sr3mesh::MeshBlock mesh = sr3mesh::MeshBlock::parse(
            vpp::ByteView(cb.data(), cb.size()), geo.meshSubBlockOffset(),
            vpp::ByteView(gb.data(), gb.size()));
        if (!mesh.drawGroupsLocated()) return;
        ++g_done;

        const size_t sub = geo.offset();
        const uint16_t materialCount = u16at(cb, sub + 0x0C);
        const size_t meshAt = geo.meshSubBlockOffset();
        const size_t meshEnd = meshAt + mesh.cLength();

        printf("\n=== %s ===\n", name.c_str());
        printf("  file %zu bytes, subheader 0x%zX, materialCount %u\n", cb.size(), sub,
               materialCount);
        printf("  mesh block 0x%zX .. 0x%zX (cLength %u), tail after mesh = %zd bytes\n",
               meshAt, meshEnd, mesh.cLength(),
               static_cast<long long>(cb.size()) - static_cast<long long>(meshEnd));

        // Name blob at subheader + 0x88. Print what is actually there.
        const size_t blobAt = sub + 0x88;
        printf("  blob at 0x%zX: first u32 = %u\n", blobAt, u32at(cb, blobAt));
        for (int row = 0; row < 4; ++row) {
            const size_t at = blobAt + static_cast<size_t>(row) * 16;
            if (at + 16 > cb.size()) break;
            printf("    blob+%02zX  ", at - blobAt);
            for (int i = 0; i < 16; ++i) printf("%02X ", cb[at + static_cast<size_t>(i)]);
            printf(" ");
            for (int i = 0; i < 16; ++i) {
                uint8_t c = cb[at + static_cast<size_t>(i)];
                printf("%c", (c >= 32 && c < 127) ? static_cast<char>(c) : '.');
            }
            printf("\n");
        }
        // Entry offset 0 must resolve to the FIRST name. Find where that
        // string actually starts, so the blob base is measured rather than
        // assumed to be blobAt + some guessed constant.
        for (size_t k = 0; k < 32 && blobAt + k < cb.size(); ++k) {
            uint8_t c = cb[blobAt + k];
            if (c >= 32 && c < 127) {
                printf("    -> first printable at blob+%zu; name offsets are relative to "
                       "blob+%zu if entry offset 0 names it\n", k, k);
                break;
            }
        }

        // Dump the start of the region past the Mesh block, where the
        // per-material records are said to live.
        printf("  tail head (hex+ascii, 4 rows of 16 from 0x%zX):\n", meshEnd);
        for (int row = 0; row < 4; ++row) {
            const size_t at = meshEnd + static_cast<size_t>(row) * 16;
            if (at + 16 > cb.size()) break;
            printf("    %04zX  ", at - meshEnd);
            for (int i = 0; i < 16; ++i) printf("%02X ", cb[at + static_cast<size_t>(i)]);
            printf(" ");
            for (int i = 0; i < 16; ++i) {
                uint8_t c = cb[at + static_cast<size_t>(i)];
                printf("%c", (c >= 32 && c < 127) ? static_cast<char>(c) : '.');
            }
            printf("\n");
        }

        // Scan the whole tail for 12-byte entries whose +0x08 reads 0,1,2...
        // consecutively - the run shape - and report EVERY position that
        // qualifies, not the first.
        printf("  candidate runs in the tail (slot indices consecutive from 0):\n");
        int found = 0;
        for (size_t at = meshEnd; at + 12 <= cb.size() && found < 24; at += 4) {
            if (u32at(cb, at + 0x08) != 0) continue;
            size_t len = 1;
            while (at + (len + 1) * 12 <= cb.size() &&
                   u32at(cb, at + len * 12 + 0x08) == static_cast<uint32_t>(len))
                ++len;
            if (len < 2) continue;
            ++found;
            printf("    at +0x%04zX  len %zu  entries:", at - meshEnd, len);
            for (size_t e = 0; e < len && e < 4; ++e) {
                printf("  [off %u hash %08X slot %u]", u32at(cb, at + e * 12 + 0x00),
                       u32at(cb, at + e * 12 + 0x04), u32at(cb, at + e * 12 + 0x08));
            }
            printf("\n");
        }
        if (found == 0) printf("    (none)\n");
    } catch (const std::exception& ex) {
        printf("  %s: %s\n", name.c_str(), ex.what());
    }
}

void walk(const vpp::Container& c) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (endsWith(n, ".ccmesh_pc")) {
            std::string gn = n;
            size_t d = gn.find_last_of('.');
            if (d != std::string::npos) gn[d + 1] = 'g';
            std::vector<uint8_t> cb, gb;
            if (!entryBytes(c, i, cb)) continue;
            for (size_t j = 0; j < c.entries().size(); ++j)
                if (c.entries()[j].name == gn) { entryBytes(c, j, gb); break; }
            if (!gb.empty()) probe(n, cb, gb);
        }
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try { walk(c.openNested(i)); } catch (const std::exception&) {}
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::vector<uint8_t> b = readFile(argv[i]);
        if (b.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(b.data(), b.size()));
            walk(c);
        } catch (const std::exception&) {}
    }
    return 0;
}
