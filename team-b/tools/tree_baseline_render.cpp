// Headless tree-mesh renderer, built ENTIRELY from two already-proven,
// independently-validated pieces of this project - no new decode logic,
// no new engine logic. Written for the golden-scene regression harness
// (tests/golden/, see tools/golden_scene_check.cpp's own header comment
// for the convention) per AI-WORKFLOW-INSTRUCTIONS.md's "end-to-end
// validation that doesn't need a human" idea and HANDOFF.md's own
// "golden-scene regression baselines (brad, a vehicle, a tree, a zone
// tile)" note.
//
// WHY THIS FILE EXISTS: no tree-specific renderer existed anywhere in
// this project before it. `sr3_viewer` (tools/sr3_viewer.cpp, untouched
// by this file - a different task is actively editing it) has commands
// for clear/list-textures/texture/view/mesh/pose/animpose/scene, none of
// which accept a .csrt_pc/.gsrt_pc pair. But the READER side already did
// all the hard work: spec-tree-format.md's own headline is "trees are an
// assembly of blocks this project already knows, plus one new fixed
// header" - sr3tree::Tree::parse() (include/sr3tree/tree.h) decodes each
// present LOD slot's embedded geometry as a plain, ordinary
// sr3mesh::MeshBlock, the EXACT SAME type sr3render::MeshRenderer already
// knows how to upload and draw headlessly (its Checker/UvAsColour/
// NormalAsColour modes need no texture resolution at all - see
// include/sr3render/mesh_renderer.h). This file is new WIRING between
// those two existing, independently real-data-validated pieces
// (sr3tree: 28/28 real shipped copies, HANDOFF.md Sec9.67; MeshRenderer:
// the whole character/vehicle rendering milestone line) - not a new
// renderer and not a new reader.
//
// Usage:
//   tree_baseline_render <archive.vpp_pc> <stem> <out.png>
//       [--mode uv|checker|normal|materialid] [--yaw D] [--pitch D]
//       [--lod N] [--size W H]
//
// Recursively searches `archive` (descending into every nested Raw
// container - the identical walk tools/validation/validate_tree.cpp
// already uses over the real population) for <stem>.csrt_pc/
// <stem>.gsrt_pc, parses with sr3tree::Tree::parse(), then draws EVERY
// PRESENT LOD slot together (channel 0, texcoord set 0, draw group 0 of
// EACH present slot's own Mesh sub-block), composited into one frame -
// or exactly one slot when --lod overrides the selection (unchanged
// single-slot behaviour, useful for isolating one part). Falls back to
// the whole index buffer as one strip if a slot's draw-group search does
// not land, exactly like every other MeshRenderer::upload() caller
// already does (that fallback is MeshRenderer's own pre-existing,
// pre-proven behaviour, not new code written for this tool).
//
// WHY "EVERY PRESENT LOD SLOT", NOT JUST GROUP 0 OF THE FIRST ONE (the
// change this task made, 2026-09-29, closing HANDOFF Sec9.120's own
// Addendum): the first version of this tool drew ONLY the lowest-index
// present LOD slot's group 0, which - measured directly, not assumed -
// turns out to be a REAL, CONCRETE FINDING about how a tree's materials
// map onto its Tree-level LOD slots, DIFFERENT from what Sec9.120's own
// Addendum guessed: st_pine_tall's 4 materials do NOT all sit inside one
// slot's own draw-group array as extra ranges. Measured on real data
// (sr3_city_0.vpp_pc :: st_pine_tall): LOD slot 0 (layout 11, 1710
// elements) has 3 draw groups, EVERY range of EVERY group materialId=0
// (Bark_01_* - trunk); LOD slot 1 (layout 12, 153 elements) has 1 group,
// 1 range, materialId=1 (Branch_01_*); LOD slot 2 (layout 11, 2728
// elements) has 3 draw groups, EVERY range materialId=2 (pineneedles_*).
// LOD slot 3 is simply NOT PRESENT in this tree's shipped data at all -
// material 3 (common_bb_* - the billboard impostor spec-tree-format.md
// Sec7 already flags as a HYPOTHESIS-tier, no-known-reader structure) has
// no decoded geometry to draw here, which is a real data absence, not a
// decode failure. So "draw all the materials this tree's data actually
// has" means drawing group 0 of EVERY PRESENT LOD SLOT (trunk+branch+
// needles), not searching harder within one slot's own group array. Each
// present slot gets its own sr3render::MeshRenderer (own GPU vertex/index
// buffer - the slots are different Mesh sub-blocks with different vertex
// counts/layout codes, so one upload() call cannot span them), uploaded
// with the SAME drawGroup=0 convention this tool always used, and all of
// them draw into ONE shared render target + depth buffer (only the FIRST
// slot's renderer owns the depth buffer; later ones reuse that same view,
// bound externally - the identical pattern tools/sr3_viewer.cpp's
// MultiChannelMultiSetRender already uses for the analogous
// multi-channel character-mesh problem, HANDOFF Sec9.105). All three
// present slots here use ALREADY-DECODED layout codes (11 and 12, both
// known=true since Sec9.120) - checked directly with decodeChannel()
// before trusting this, not assumed - so no new decode work was needed.
//
// Every number this tool prints to stdout comes directly from
// sr3tree::Tree / sr3mesh::MeshBlock / sr3render::MeshRenderer /
// sr3render::RenderDevice's own real accessors - nothing here is
// invented or estimated.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "sr3mesh/mesh_block.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3tree/tree.h"
#include "vpp/container.h"

namespace {

std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    if (n < 0) n = 0;
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

// Basename only - this tool's stdout is meant to be a byte-diffable
// golden-baseline artifact, so it deliberately never prints a caller-
// supplied absolute path (which would make the frozen summary depend on
// where/who ran the capture). See tools/golden_scene_check.cpp's own
// comment on why sr3_viewer's equivalent "wrote:" lines DO need a
// normalisation step - this tool avoids the problem at the source
// instead.
std::string baseName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
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
    const bool ok = r.status == vpp::DecodeStatus::Ok ||
                    r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                    r.status == vpp::DecodeStatus::ContentValidated ||
                    r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                    r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
    if (!ok) return false;
    out = std::move(r.data);
    return true;
}

// Same recursive-descent shape as validate_tree.cpp's walk(), narrowed to
// stop at the first archive-relative pair matching `wantStem`.
bool findTreePair(const vpp::Container& c, const std::string& wantStem,
                  std::vector<uint8_t>& cOut, std::vector<uint8_t>& gOut) {
    std::string stem;
    size_t csrtIdx = static_cast<size_t>(-1), gsrtIdx = static_cast<size_t>(-1);
    for (size_t i = 0; i < c.entries().size(); ++i) {
        const std::string& n = c.entries()[i].name;
        if (stemFor(n, ".csrt_pc", stem) && stem == wantStem) csrtIdx = i;
        else if (stemFor(n, ".gsrt_pc", stem) && stem == wantStem) gsrtIdx = i;
    }
    if (csrtIdx != static_cast<size_t>(-1) && gsrtIdx != static_cast<size_t>(-1)) {
        if (entryBytes(c, csrtIdx, cOut) && entryBytes(c, gsrtIdx, gOut)) return true;
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                if (findTreePair(c.openNested(i), wantStem, cOut, gOut)) return true;
            } catch (const std::exception&) {
            }
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf(
            "usage: %s <archive.vpp_pc> <stem> <out.png> [--mode uv|checker|normal|materialid] "
            "[--yaw D] [--pitch D] [--lod N] [--size W H]\n",
            argv[0]);
        return 1;
    }
    const std::string archivePath = argv[1];
    const std::string stem = argv[2];
    const std::string outPng = argv[3];

    sr3render::MeshDrawMode mode = sr3render::MeshDrawMode::Checker;
    float yaw = 0.6f, pitch = 0.35f;
    int lodOverride = -1;
    uint32_t width = 800, height = 600;

    for (int a = 4; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--mode" && a + 1 < argc) {
            std::string m = argv[++a];
            if (m == "uv") mode = sr3render::MeshDrawMode::UvAsColour;
            else if (m == "checker") mode = sr3render::MeshDrawMode::Checker;
            else if (m == "normal") mode = sr3render::MeshDrawMode::NormalAsColour;
            else if (m == "materialid") mode = sr3render::MeshDrawMode::MaterialIdAsColour;
            else {
                std::printf("unknown --mode '%s'\n", m.c_str());
                return 1;
            }
        } else if (arg == "--yaw" && a + 1 < argc) {
            yaw = std::strtof(argv[++a], nullptr);
        } else if (arg == "--pitch" && a + 1 < argc) {
            pitch = std::strtof(argv[++a], nullptr);
        } else if (arg == "--lod" && a + 1 < argc) {
            lodOverride = std::atoi(argv[++a]);
        } else if (arg == "--size" && a + 2 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++a]));
            height = static_cast<uint32_t>(std::atoi(argv[++a]));
        } else {
            std::printf("unknown argument '%s'\n", arg.c_str());
            return 1;
        }
    }

    std::vector<uint8_t> archiveBytes = readFile(archivePath);
    if (archiveBytes.empty()) {
        std::printf("FATAL: could not read archive '%s'\n", archivePath.c_str());
        return 1;
    }

    std::vector<uint8_t> cBytes, gBytes;
    try {
        vpp::Container root(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));
        if (!findTreePair(root, stem, cBytes, gBytes)) {
            std::printf("FATAL: '%s.csrt_pc'/'%s.gsrt_pc' not found anywhere in '%s'\n",
                       stem.c_str(), stem.c_str(), baseName(archivePath).c_str());
            return 1;
        }
    } catch (const std::exception& e) {
        std::printf("FATAL: could not open archive: %s\n", e.what());
        return 1;
    }

    sr3tree::Tree tree;
    try {
        tree = sr3tree::Tree::parse(vpp::ByteView(cBytes.data(), cBytes.size()),
                                    vpp::ByteView(gBytes.data(), gBytes.size()));
    } catch (const std::exception& e) {
        std::printf("FATAL: sr3tree::Tree::parse failed: %s\n", e.what());
        return 1;
    }

    std::printf("archive  : %s\n", baseName(archivePath).c_str());
    std::printf("stem     : %s (.csrt_pc %zu bytes + .gsrt_pc %zu bytes)\n", stem.c_str(),
               cBytes.size(), gBytes.size());
    std::printf("materials: %u decoded, %zu present, %zu material-set names\n",
               tree.materialCount(), tree.materials().size(), tree.materialSetNames().size());
    {
        std::vector<uint32_t> shaderHashes;
        for (const auto& m : tree.materials()) {
            bool seen = false;
            for (uint32_t h : shaderHashes) seen = seen || (h == m.shaderHash);
            if (!seen) shaderHashes.push_back(m.shaderHash);
        }
        std::printf("           distinct shader hash(es):");
        for (uint32_t h : shaderHashes) std::printf(" 0x%08X", h);
        std::printf("\n");
    }
    std::printf("LOD slots: %zu present of %zu total\n", tree.presentLodCount(),
               tree.lodSlots().size());
    std::printf("capsules : %zu (radius", tree.collisionCapsules().size());
    for (const auto& cap : tree.collisionCapsules()) std::printf(" %.4f", cap.radius);
    std::printf(")\n");
    if (auto wss = tree.windSteadyStateAtLevel()) {
        std::printf("wind     : steady-state @ level=0.1,");
        for (size_t i = 0; i < wss->size(); ++i) {
            std::printf(" band%zu(amp=%.4f freq=%.4f)", i, (*wss)[i].amplitude, (*wss)[i].frequency);
        }
        std::printf("\n");
    }

    // Which LOD slot(s) to draw. --lod N keeps the tool's original
    // single-slot behaviour (isolate one part - useful for inspecting the
    // trunk, branches or needles alone). The DEFAULT (no --lod) now draws
    // EVERY PRESENT slot together, ascending - see this file's own header
    // comment for why that, not "search harder inside one slot's own
    // group array", is the real fix for the leaves/needles gap: measured
    // directly on st_pine_tall, each present slot is bound to exactly one
    // material (slot 0 -> material 0 / Bark, trunk; slot 1 -> material 1 /
    // Branch; slot 2 -> material 2 / pineneedles), and material 3 (the
    // billboard impostor) has no present slot at all in this tree's data.
    std::vector<size_t> lodsToRender;
    if (lodOverride >= 0) {
        if (static_cast<size_t>(lodOverride) < tree.lodSlots().size() &&
            tree.lodSlots()[static_cast<size_t>(lodOverride)].present) {
            lodsToRender.push_back(static_cast<size_t>(lodOverride));
        } else {
            std::printf("FATAL: requested --lod %d is not present\n", lodOverride);
            return 1;
        }
    } else {
        for (size_t i = 0; i < tree.lodSlots().size(); ++i) {
            if (tree.lodSlots()[i].present) lodsToRender.push_back(i);
        }
    }
    if (lodsToRender.empty()) {
        std::printf("FATAL: tree has no present LOD slot to draw\n");
        return 1;
    }

    std::printf("drawing  : %zu LOD slot(s) rendered together (each its own group 0):",
               lodsToRender.size());
    for (size_t slot : lodsToRender) std::printf(" %zu", slot);
    std::printf("\n");
    for (size_t slot : lodsToRender) {
        const sr3mesh::MeshBlock& m = tree.lodSlots()[slot].mesh;
        std::printf("  lod slot %zu: %zu channel(s), %u index(es), index%%3==%s, "
                   "draw-groups-located=%s, group0 has %zu range(s)\n",
                   slot, m.channels().size(), m.indexCount(),
                   m.indexCountDivisibleByThree() ? "0" : "nonzero (expected for a strip)",
                   m.drawGroupsLocated() ? "yes" : "no (falling back to whole-buffer strip)",
                   (m.drawGroupsLocated() && !m.drawGroups().empty()) ? m.drawGroups()[0].size() : 0);
        if (!m.channels().empty()) {
            std::printf("             channel 0: layout %u, %u texcoord set(s), stride %zu, %u element(s)\n",
                       m.channels()[0].layoutCode, m.channels()[0].texcoordCount, m.channels()[0].stride(),
                       m.channels()[0].elementCount);
        }
    }

    std::string err;
    sr3render::RenderDevice dev;
    if (!dev.initialise(width, height, err)) {
        std::printf("FATAL: RenderDevice::initialise failed: %s\n", err.c_str());
        return 1;
    }

    // One MeshRenderer per slot - each present LOD slot is a DIFFERENT Mesh
    // sub-block (own vertex/index buffers, possibly a different layout
    // code/element count), so one upload() call cannot span more than one
    // of them. Only the FIRST slot's renderer owns the depth buffer; every
    // later slot's draw() call relies on that SAME view being bound
    // externally by the caller below - the identical pattern
    // tools/sr3_viewer.cpp's MultiChannelMultiSetRender already uses for
    // the analogous multi-channel character-mesh problem (HANDOFF
    // Sec9.105), adapted here to multiple LOD-slot Mesh sub-blocks instead
    // of multiple channels of one Mesh sub-block.
    std::vector<std::unique_ptr<sr3render::MeshRenderer>> renderers;
    float combinedMin[3] = {1e30f, 1e30f, 1e30f};
    float combinedMax[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t slot : lodsToRender) {
        const sr3mesh::MeshBlock& m = tree.lodSlots()[slot].mesh;
        auto r = std::make_unique<sr3render::MeshRenderer>();
        if (!r->initialise(dev.device(), err)) {
            std::printf("FATAL: MeshRenderer::initialise failed for LOD slot %zu: %s\n", slot, err.c_str());
            return 1;
        }
        if (!r->upload(dev.device(), m, /*channelIndex=*/0, /*texcoordSet=*/0, err, /*drawGroup=*/0)) {
            std::printf("FATAL: MeshRenderer::upload failed for LOD slot %zu: %s\n", slot, err.c_str());
            return 1;
        }
        if (renderers.empty()) {
            if (!r->createDepth(dev.device(), width, height, err)) {
                std::printf("FATAL: MeshRenderer::createDepth failed: %s\n", err.c_str());
                return 1;
            }
        }
        const float* mn = r->boundsMin();
        const float* mx = r->boundsMax();
        for (int c = 0; c < 3; ++c) {
            if (mn[c] < combinedMin[c]) combinedMin[c] = mn[c];
            if (mx[c] > combinedMax[c]) combinedMax[c] = mx[c];
        }
        renderers.push_back(std::move(r));
    }

    dev.bindRenderTargetWithDepth(renderers.front()->depthView());
    dev.clear(sr3render::Rgba8{18, 18, 24, 255});
    renderers.front()->clearDepth(dev.context()); // clears the ONE shared depth texture every renderer above targets

    float vp[16];
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float distanceScale = 1.8f; // same framing constant this project's own
                                       // prototype_real_shader_draw.cpp uses
    // Combined bounds across every rendered slot, so the camera frames the
    // WHOLE tree (trunk+branch+needles), not just whichever slot happened
    // to be uploaded first.
    sr3render::buildOrbitViewProjection(combinedMin, combinedMax, yaw, pitch, distanceScale, aspect, vp);
    for (auto& r : renderers) r->draw(dev.context(), vp, mode, nullptr);

    std::vector<uint8_t> rgba;
    if (!dev.readBack(rgba, err)) {
        std::printf("FATAL: RenderDevice::readBack failed: %s\n", err.c_str());
        return 1;
    }
    if (!sr3render::writePng(outPng, width, height, rgba, err)) {
        std::printf("FATAL: writePng failed: %s\n", err.c_str());
        return 1;
    }

    uint32_t totalIndexCount = 0, totalTriangles = 0, totalRanges = 0;
    for (size_t i = 0; i < renderers.size(); ++i) {
        const auto& r = renderers[i];
        totalIndexCount += r->indexCount();
        totalTriangles += r->trianglesEmitted();
        totalRanges += static_cast<uint32_t>(r->subDraws().size());
        std::printf("  lod slot %zu: indexCount=%u trianglesEmitted=%u subDraws=%zu range(s)", lodsToRender[i],
                   r->indexCount(), r->trianglesEmitted(), r->subDraws().size());
        for (const auto& sd : r->subDraws()) {
            std::printf(" [mat=%u idx=%u..%u]", sd.materialId, sd.startIndex, sd.startIndex + sd.indexCount);
        }
        std::printf("\n");
    }
    std::printf("indexCount      : %u total across %zu LOD slot(s)\n", totalIndexCount, renderers.size());
    std::printf("trianglesEmitted: %u total\n", totalTriangles);
    std::printf("subDraws        : %u range(s) total\n", totalRanges);
    std::printf("bounds (renderer, combined): X[%.3f %.3f] Y[%.3f %.3f] Z[%.3f %.3f]\n", combinedMin[0],
               combinedMax[0], combinedMin[1], combinedMax[1], combinedMin[2], combinedMax[2]);
    std::printf("device   : %s (%s)\n", dev.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP",
               dev.adapterName().c_str());
    std::printf("size     : %ux%u\n", width, height);
    std::printf("wrote    : %s\n", baseName(outPng).c_str());
    return 0;
}
