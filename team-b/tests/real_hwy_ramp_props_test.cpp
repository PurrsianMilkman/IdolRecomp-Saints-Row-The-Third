// Real-data regression for the HEAD -> MIDDLE g-cursor handoff fix
// (sr3clmesh::LevelMesh::gCursorAfterHeadChain(), include/sr3clmesh/
// level_mesh.h note 9, 2026-10-08).
//
// Reads 5 real, FIXED, NAMED `.clmesh_pc`/`.glmesh_pc` street/ramp-surface
// props from CRREISH_GAME_CACHE_DIR/sr3_city_0.vpp_pc - hwy_yellow_st_r,
// hwy_sw, hwy_sw_l, hwy_sw_r, hwy_yellow_ap_r - and exits 77 (ctest SKIPPED,
// never a pass) when that archive is not present, matching every other
// real_*_test's own convention exactly (see real_zone_objects_test.cpp).
//
// BACKGROUND: these 5 props were found, during an earlier unrelated task,
// to be silently SKIPPED by every caller of LevelMesh::resolveReferencedMeshes()
// (tools/sr3_viewer.cpp's runClmesh()/runClmeshTile()/runClmeshBatch() and
// the `--rendergroup` path) with "segment does not open with the block's
// check value" when parsing their own first render group's Mesh sub-block.
//
// ROOT CAUSE (confirmed via raw byte inspection, not guessed): every caller
// computed the render group's g-segment offset (`gSegmentOffset`, the
// `gCursor` handed to sr3mesh::MeshBlock::parse()) as the bare, un-rounded
// additive sum of the HEAD reference chain's own resolved blocks' gLength()
// - correct WITHIN the head chain itself (note 6, no rounding there,
// confirmed independently), but wrong at the ONE-TIME handoff from the end
// of that chain to the MIDDLE region's first render group, which rounds
// that sum UP to the next 16-byte boundary. All 5 of these props' real
// head-chain g-cursor sums land at 4 (mod 16); the real segment starts 12
// zero-padded bytes later, at the next 16-byte boundary. Measured over the
// whole real population sharing a str2 container with one of these 5 (602
// real `.clmesh_pc` instances across 9 containers in sr3_city_0.vpp_pc):
// success/failure under the OLD, un-rounded formula is explained exactly
// by whether that sum already happened to be 16-aligned (568/568 that are,
// parse fine either way; every other 34 - these 5 named props among them -
// fail under the old formula and are fixed by rounding, 34/34, 0 exceptions).
//
// This test locks down the FIX (gCursorAfterHeadChain()), not just "no
// crash": for each of the 5 named props, it re-parses renderGroups[0]'s own
// Mesh sub-block using the corrected gCursor and checks that real, non-
// trivial geometry comes back - a located draw-group table, a non-zero
// index count, and at least one channel whose declared element count is
// strictly positive and whose decoded vertex count matches it exactly (the
// same channel-decode path sr3_viewer.cpp's own render commands use) -
// not a placeholder/stub result.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "sr3clmesh/level_mesh.h"
#include "sr3mesh/mesh_block.h"
#include "vpp/container.h"
#include "vppcache/archive_cache.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool fileExists(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return f.good();
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

// Same recursive-descent findEntry() tools/sr3_viewer.cpp uses for its real
// `clmesh`/`clmesh-tile`/`clmesh-batch` commands (copied, not linked -
// sr3_viewer.cpp is a tool, not a library).
bool findEntry(const vpp::Container& container, const std::string& name,
               std::vector<uint8_t>& out, std::vector<uint8_t>& siblingOut,
               const std::string& siblingName) {
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].name == name) {
            if (!entryBytes(container, i, out)) return false;
            for (size_t j = 0; j < container.entries().size(); ++j) {
                if (container.entries()[j].name == siblingName) {
                    entryBytes(container, j, siblingOut);
                    break;
                }
            }
            return true;
        }
    }
    for (size_t i = 0; i < container.entries().size(); ++i) {
        if (container.entries()[i].payload.kind != vpp::PayloadKind::Raw) continue;
        try {
            vpp::Container nested = container.openNested(i);
            if (findEntry(nested, name, out, siblingOut, siblingName)) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

} // namespace

int main() {
#ifndef CRREISH_GAME_CACHE_DIR
    std::cout << "CRREISH_GAME_CACHE_DIR not configured - SKIPPED\n";
    return 77;
#else
    const std::string cacheDir = CRREISH_GAME_CACHE_DIR;
    const std::string archivePath = cacheDir + "/sr3_city_0.vpp_pc";
    if (!fileExists(archivePath)) {
        std::cout << "real-data archive not found (" << archivePath << ") - SKIPPED\n";
        return 77;
    }

    static const char* kNames[] = {"hwy_yellow_st_r", "hwy_sw", "hwy_sw_l", "hwy_sw_r",
                                    "hwy_yellow_ap_r"};
    int foundCount = 0, parsedOkCount = 0;

    try {
        auto opened = vppcache::openArchive(archivePath);
        const vpp::Container& topContainer = opened->container;

        for (const char* stem : kNames) {
            const std::string clName = std::string(stem) + ".clmesh_pc";
            const std::string glName = std::string(stem) + ".glmesh_pc";

            std::vector<uint8_t> clBytes, glBytes;
            bool found = findEntry(topContainer, clName, clBytes, glBytes, glName);
            if (!found || clBytes.empty() || glBytes.empty()) {
                // Not every build has the full retail archive set under
                // CRREISH_GAME_CACHE_DIR - treat an individual missing prop
                // as informational, not a failure; the population-level
                // CHECKs below still require all 5 to be found when the
                // archive itself is present (it is the SAME archive this
                // bug was found in).
                std::cout << "  " << stem << ": not found in " << archivePath << "\n";
                continue;
            }
            ++foundCount;

            vpp::ByteView clView(clBytes.data(), clBytes.size());
            vpp::ByteView glView(glBytes.data(), glBytes.size());

            sr3clmesh::LevelMesh lm;
            try {
                lm = sr3clmesh::LevelMesh::parse(clView);
            } catch (const std::exception& ex) {
                std::cerr << "  " << stem << ": LevelMesh parse failed: " << ex.what() << "\n";
                CHECK(false);
                continue;
            }
            CHECK(lm.walkComplete());
            CHECK(lm.middle().renderGroupCount > 0);
            if (!lm.walkComplete() || lm.middle().renderGroupCount == 0) continue;

            std::vector<sr3mesh::MeshBlock> headBlocks = lm.resolveReferencedMeshes(clView, glView);
            // The HEAD chain itself must fully resolve for this test's own
            // claim to mean anything (it is testing the HEAD->MIDDLE
            // handoff specifically, not a HANDOFF Sec9.66-class partial
            // head-chain break).
            CHECK(headBlocks.size() == lm.referenceArrayA().count);

            size_t gCursor = sr3clmesh::LevelMesh::gCursorAfterHeadChain(headBlocks);

            const auto& rg = lm.middle().renderGroups[0];
            const size_t headerDisplacement = ((rg.mesh.offset + 16 + 7) / 8 * 8) - rg.mesh.offset;

            sr3mesh::MeshBlock chosenMesh;
            bool parsedOk = false;
            try {
                chosenMesh = sr3mesh::MeshBlock::parse(clView, rg.mesh.offset, glView, gCursor,
                                                       headerDisplacement);
                parsedOk = true;
            } catch (const std::exception& ex) {
                std::cerr << "  " << stem << ": renderGroups[0] Mesh sub-block parse FAILED: " << ex.what()
                          << "\n";
            }
            CHECK(parsedOk);
            if (!parsedOk) continue;
            ++parsedOkCount;

            // Real geometry, not a placeholder: a located draw-group table,
            // a non-zero index count, and at least one channel whose
            // decoded vertex count matches its own declared element count
            // exactly (sr3_viewer.cpp's own render path calls exactly this
            // same decodeChannel()).
            CHECK(chosenMesh.drawGroupsLocated());
            CHECK(!chosenMesh.drawGroups().empty());
            CHECK(chosenMesh.indexCount() > 0);
            CHECK(!chosenMesh.channels().empty());

            bool sawRealDecodedChannel = false;
            for (size_t ci = 0; ci < chosenMesh.channels().size(); ++ci) {
                const sr3mesh::Channel& ch = chosenMesh.channels()[ci];
                if (ch.elementCount == 0) continue;
                std::vector<sr3mesh::Vertex> verts;
                try {
                    verts = chosenMesh.decodeChannel(ci);
                } catch (const std::exception&) {
                    continue;
                }
                if (verts.size() == ch.elementCount) {
                    sawRealDecodedChannel = true;
                    break;
                }
            }
            CHECK(sawRealDecodedChannel);

            std::cout << "  " << stem << ": PARSED OK - checkValue=0x" << std::hex
                      << chosenMesh.checkValue() << std::dec << " indexCount=" << chosenMesh.indexCount()
                      << " channels=" << chosenMesh.channels().size()
                      << " drawGroups=" << chosenMesh.drawGroups().size() << "\n";
        }
    } catch (const std::exception& ex) {
        std::cout << "archive error - SKIPPED (" << ex.what() << ")\n";
        return 77;
    }

    if (foundCount == 0) {
        std::cout << "none of the 5 named props were found in " << archivePath << " - SKIPPED\n";
        return 77;
    }

    std::cout << foundCount << "/5 named props found, " << parsedOkCount << "/" << foundCount
              << " renderGroups[0] parsed with real geometry\n";

    // This is the SAME archive the bug was found and measured against -
    // all 5 must be present and must all now parse.
    CHECK(foundCount == 5);
    CHECK(parsedOkCount == foundCount);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All real-data hwy-ramp-props checks passed.\n";
    return 0;
#endif
}
