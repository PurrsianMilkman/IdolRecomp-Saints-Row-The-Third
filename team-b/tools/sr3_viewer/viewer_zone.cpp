#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_common.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3mesh/mesh_block.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3zone/hood_volumes.h"
#include "sr3zone/road_graph.h"
#include "sr3zone/zone_heightmap.h"
#include "vpp/container.h"

using namespace sr3viewer;


// -----------------------------------------------------------------------
// `zone` command - the 4th golden-scene target (HANDOFF §9's golden-scene
// infrastructure, extending §9.110/§9.112's brad/vehicle_genki/
// vehicle_standard convention to real zone-tile geometry).
//
// Renders a real `.czn_pc`/`.gzn_pc` zone tile's embedded Mesh sub-block
// using vertex layout code 0 - the REAL, UV-bearing render geometry
// (position + normal + texcoord), NOT vertex layout code 24.
//
// CHANGED 2026-09-30 (HANDOFF.md §9.126/§9.127): layout code 24 (the
// original target of this command) was decisively established to be a real
// UV-less COMPANION stream (texcoordCount==0 at full population, 535/535
// real code-24 channels; spec-vertex-format.md §12.13 item 4) that sits
// ALONGSIDE other, real UV-bearing channels in the SAME Mesh sub-block for
// the SAME geometry.
//
// UPDATED 2026-09-30 (zone-composite investigation, runZoneTileComposite()
// below): the earlier "shadow/occlusion/collision-proxy pass" reading was
// the best-supported guess at the time but NOT visually checked - a direct
// composite render (both channels, same camera, same frame - see
// runZoneTileComposite()'s own doc comment) shows layout code 0's UV-
// bearing window/facade quads consistently sitting ON layout code 24's
// shell faces (grid patterns bounded within each shell face's silhouette,
// confirmed from two independent camera angles; a face viewed edge-on
// compresses to a thin sliver that hugs the shell's own silhouette edge
// rather than floating past it). This is the "same building, two visual
// layers" reading, not independent proxy geometry - code 24's real ROLE is
// therefore OPEN again (UV-less shell surface, not proven to be shadow/
// occlusion/collision), not "probable proxy". Kept labelled a "companion"
// stream here (still accurate - it co-occurs with code 0 in the same
// block) with its ROLE stated as open rather than guessed. This command
// still draws the real layout-code-0 channel by default - that choice is
// unaffected by this update, since code-0 remains the far larger, UV-
// bearing, materially-textured-in-principle channel either way.
//
// This command decodes and draws the real layout-code-0 channel - position (+0, FLOAT3), normal
// (+12, UBYTE4N xyz), texcoord (+16, SHORT2/1024) - confirmed via
// sr3mesh::layoutInfoFor(0) (base=16, hasNormal=true, hasTangent=false;
// see src/mesh_block.cpp), NOT assumed to match code 24's own field layout.
// The frozen zone_tile baseline's own channel measures exactly
// base(16) + 4*texcoordCount(1) == stride(20), strideMatchesLaw=1 - the
// spec's own stride law holds exactly, and a direct real-byte UV decode
// (kTexcoordScale==1024, spec-vertex-format.md §6.5) produces sane,
// tiling-overflowing [-1.21..2.72]x[-1.10..1.72] values, not denormals or
// garbage - the same universal texcoord convention used elsewhere in this
// codebase (see kTexcoordScale's own usages, e.g. vehicle rendering).
//
// MATERIAL/TEXTURE BINDING - investigated, genuine dead end, NOT invented:
// the vehicle-style `0x424BD00D` GeometryBlock sub-header
// (include/sr3geometry/material_binding.h) that resolves a draw-range
// materialId to a texture name NEVER occurs anywhere in real `.czn_pc` data
// (0/2,971 files, re-confirmed directly against this exact file's own real
// bytes, not just cited from the earlier population scan). `.czh_pc`'s own
// alternate name table (sr3zone::ZoneHeader::materialBlock()) doesn't help
// either: this file's own 71 names are 100% non-texture instance/effect
// references (zero `.tga`). A peer-relayed hypothesis (a draw range's
// materialId might index a `cc:0`-group texture STEM FAMILY rather than a
// GeometryBlock record) was tested directly against real bytes, population-
// wide (2,971 real .czh_pc files): stem-count-vs-(max materialId+1) matches
// only 1.0% of the time per Mesh block (381/37,147) and 11.5% per whole
// `.czn_pc` file (53/460, almost entirely degenerate 1-stem/1-material
// cases) - a clean statistical negative, not a working rule. So real
// per-draw-range materials/textures ARE decoded and drawn with (one
// indexed DrawIndexed call per real draw range, using the range's own real
// materialId - the SAME per-range structure `vehicle`/`pose --mode
// material` already draw), just coloured by materialId
// (MeshDrawMode::MaterialIdAsColour) rather than textured, because no
// confirmed mechanism resolves a zone materialId to a real texture or
// shader - the same honest "flat colour, never a neighbour's texture"
// fallback this codebase already uses in drawTextured() when a material's
// texture fails to resolve for any other carrier.
//
// A draw group's ranges span MULTIPLE vertex channels (this frozen tile's
// own group 0: 9 ranges, submeshIndex values {0,1,2} - i.e. code-24 AND
// code-0 AND code-2 ranges interleaved in the SAME group), so this command
// builds a `rangeMask` restricting upload() to only the ranges whose own
// `submeshIndex` equals the resolved layout-code-0 channel's index - the
// same "one channel, masked to its own ranges" pattern
// MultiChannelMultiSetRender already established for multi-channel
// character meshes, not a new mechanism.
//
// A SECOND, NEW finding for THIS SAME reason - measured directly, not
// assumed from the vehicle/character case: for this channel, group 0's own
// submeshIndex==1 ranges cover only 52 of the channel's 4,580 real
// (channel-1) indices (3 tiny ranges, materialId 2/3/4, vertex spans
// [296..310]/[1259..1261]/[2038..2053]) while group 1's own submeshIndex==1
// ranges cover the remaining 4,528 (materialId 9/10/11/3/2/12/13, vertex
// spans [0..295]/[311..1258]/[1262..2037]/[2054..3061]). Laid end to end
// these two groups' channel-1 ranges tile [0..3061] EXACTLY ONCE with ZERO
// overlap and ZERO gap (materialId 2 and 3 each appear in BOTH groups, on
// directly adjacent vertex spans) - the signature of ONE seamless surface's
// draw calls split across two submitted batches, NOT two independent
// alternative LOD levels of it (an actual coarser LOD would show fewer
// triangles over a SIMILAR vertex range, i.e. overlap, not a disjoint
// partition). So `drawGroups()`'s own "CONFIRMED to be LOD levels" finding
// (validated by rendering on single-channel vehicle/character meshes) does
// NOT generalise to this multi-channel zone case as-is - drawing group 0
// alone renders 14 triangles of a 2,510-triangle real surface. This command
// therefore unions a channel's own ranges across EVERY located group by
// default (auto mode, no `--group` given) - reusing the exact "one
// MeshRenderer per uploaded unit, shared depth buffer, N draw() calls
// composited into one frame" pattern tools/tree_baseline_render.cpp already
// established for its own analogous multi-LOD-slot compositing, not a new
// mechanism. An explicit `--group N` still renders exactly that one group's
// own channel-owned ranges, unchanged, for single-group inspection.
//
// Locating the Mesh sub-block(s) reuses sr3zone::ZoneGeometry::locate() -
// the same already-proven mechanism tools/tree_baseline_render.cpp (§9.110)
// already wires into this exact MeshRenderer/RenderDevice pair for trees -
// nothing new is invented for that step.
int runZoneTile(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: sr3_viewer zone <archive.vpp_pc> <name.czn_pc>\n"
                     "                       [--block N] [--channel N] [--group N]\n"
                     "                       [--size W H] [--frames N] [--capture <out.png>]\n"
                     "                       [--capture-frame N] [--yaw D] [--pitch D] "
                     "[--distance F] [--visible]\n"
                     "       Real zone-tile geometry render: locates the .czn_pc/.gzn_pc pair's\n"
                     "       embedded Mesh sub-block(s) via sr3zone::ZoneGeometry::locate() (same\n"
                     "       mechanism tools/tree_baseline_render.cpp already uses) and draws ONE\n"
                     "       vertex-layout-code-0 channel - the real UV-bearing render geometry\n"
                     "       (position+normal+texcoord), NOT the UV-less layout-code-24 companion\n"
                     "       stream this command drew before (see this function's own doc comment).\n"
                     "       No real material/texture binding exists for zone data (genuine, checked\n"
                     "       dead end - 0x424BD00D absent from all real .czn_pc, see doc comment), so\n"
                     "       each real draw range is still drawn separately with its own real\n"
                     "       materialId, coloured rather than textured (MeshDrawMode::\n"
                     "       MaterialIdAsColour). --block/--channel default to the first located\n"
                     "       block/channel that actually carries layout code 0; use them to inspect\n"
                     "       a different one (including layout code 24 again, for comparison).\n"
                     "       --group defaults to auto: the resolved channel's own ranges are UNIONED\n"
                     "       across every located LOD group (see doc comment - one group alone can\n"
                     "       silently omit real geometry on this multi-channel data); an explicit\n"
                     "       --group N renders exactly that one group's own channel-owned ranges.\n"
                     "       <archive.vpp_pc> is whichever top-level archive holds the tile -\n"
                     "       findEntry() below already recurses into nested .str2_pc containers.\n");
        return 1;
    }
    const std::string archivePath = argv[2];
    const std::string cznName = argv[3];

    int blockArg = -1;   // -1 = auto: first located block with a layoutCode==0 channel
    int channelArg = -1; // -1 = auto: first channel in that block with layoutCode==0
    // -1 = auto: union the resolved channel's own ranges across EVERY
    // located group (see this function's own doc comment for why, on this
    // multi-channel data, one group alone can silently omit real geometry).
    // An explicit --group N renders exactly that one group, unchanged.
    int groupArg = -1;
    uint32_t winWidth = 1100, winHeight = 700;
    int maxFrames = -1;
    int captureFrame = 0;
    std::string capturePath;
    // Camera framing: a CHOSEN default (not a recovered fact), matching the
    // same values the `vehicle` command already uses for its own real
    // render (tools/prototype_real_shader_draw_multishader.cpp's own).
    float yaw = 0.6f, pitch = 0.35f, distanceScale = 1.8f;
    bool visible = false; // off by default - see sr3render/window.h's own doc comment

    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--block" && i + 1 < argc) {
            blockArg = std::atoi(argv[++i]);
        } else if (arg == "--channel" && i + 1 < argc) {
            channelArg = std::atoi(argv[++i]);
        } else if (arg == "--group" && i + 1 < argc) {
            groupArg = std::atoi(argv[++i]);
        } else if (arg == "--size" && i + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++i]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        } else if (arg == "--capture" && i + 1 < argc) {
            capturePath = argv[++i];
        } else if (arg == "--capture-frame" && i + 1 < argc) {
            captureFrame = std::atoi(argv[++i]);
        } else if (arg == "--yaw" && i + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && i + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--distance" && i + 1 < argc) {
            distanceScale = static_cast<float>(std::atof(argv[++i]));
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string gznName = cznName;
    {
        size_t dot = gznName.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < gznName.size()) gznName[dot + 1] = 'g';
    }

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", archivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> cznBytes, gznBytes;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cznName, cznBytes, gznBytes, gznName)) {
            std::fprintf(stderr, "could not find '%s' in %s\n", cznName.c_str(), archivePath.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    std::printf("[zone] %s: czn=%zu bytes gzn=%zu bytes\n", cznName.c_str(), cznBytes.size(),
                gznBytes.size());

    std::vector<sr3zone::ZoneMeshBlockEntry> blocks;
    try {
        blocks = sr3zone::ZoneGeometry::locate(
            vpp::ByteView(cznBytes.data(), cznBytes.size()),
            vpp::ByteView(gznBytes.data(), gznBytes.size()));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "zone geometry locate failed: %s\n", ex.what());
        return 1;
    }
    std::printf("[zone] %zu Mesh sub-block(s) located\n", blocks.size());
    if (blocks.empty()) {
        std::fprintf(stderr, "no Mesh sub-block located in this zone tile - nothing to render\n");
        return 1;
    }

    // Auto-select the first (block, channel) pair carrying vertex layout
    // code 0 - the real UV-bearing render geometry (see this function's own
    // doc comment for why code 0, not code 24, is the target now). An
    // explicit --block/--channel is honoured as given, WITHOUT the code-0
    // filter, so a caller can inspect what a different channel (including
    // code 24 again) looks like - it will fail loudly in decodeChannel()/
    // upload() rather than guess, same as every other command here.
    int resolvedBlock = blockArg;
    int resolvedChannel = channelArg;
    if (resolvedBlock < 0) {
        for (size_t bi = 0; bi < blocks.size() && resolvedBlock < 0; ++bi) {
            const auto& channels = blocks[bi].block.channels();
            for (size_t ci = 0; ci < channels.size(); ++ci) {
                if (channels[ci].layoutCode == 0) {
                    resolvedBlock = static_cast<int>(bi);
                    if (resolvedChannel < 0) resolvedChannel = static_cast<int>(ci);
                    break;
                }
            }
        }
    }
    if (resolvedBlock < 0) {
        std::fprintf(stderr,
                     "no block in this zone tile carries a vertex-layout-code-0 channel - "
                     "nothing this command can render\n");
        return 1;
    }
    if (resolvedBlock >= static_cast<int>(blocks.size())) {
        std::fprintf(stderr, "--block %d out of range (%zu located)\n", resolvedBlock, blocks.size());
        return 2;
    }
    const sr3zone::ZoneMeshBlockEntry& entry = blocks[static_cast<size_t>(resolvedBlock)];
    const sr3mesh::MeshBlock& mesh = entry.block;
    if (resolvedChannel < 0) {
        for (size_t ci = 0; ci < mesh.channels().size(); ++ci) {
            if (mesh.channels()[ci].layoutCode == 0) { resolvedChannel = static_cast<int>(ci); break; }
        }
    }
    if (resolvedChannel < 0 || resolvedChannel >= static_cast<int>(mesh.channels().size())) {
        std::fprintf(stderr, "block %d has no vertex-layout-code-0 channel (--channel %d)\n",
                     resolvedBlock, resolvedChannel);
        return 1;
    }
    const size_t channelIndex = static_cast<size_t>(resolvedChannel);
    const auto& channel = mesh.channels()[channelIndex];
    std::printf("[block %d] fromGFile=%d indexCount=%u drawGroupsLocated=%d\n", resolvedBlock,
                entry.fromGFile ? 1 : 0, mesh.indexCount(), mesh.drawGroupsLocated() ? 1 : 0);
    std::printf("[channel %d] layoutCode=%u elementCount=%u stride=%zu strideMatchesLaw=%d "
                "texcoordCount=%u\n",
                resolvedChannel, channel.layoutCode, channel.elementCount, channel.stride(),
                channel.strideMatchesLaw ? 1 : 0, channel.texcoordCount);
    if (channel.layoutCode == 0) {
        // Real per-layout-code field layout, from the real API - NOT
        // assumed to match code 24's own layout (this function's own doc
        // comment). base=16 (position 12 + normal 4) + 4*texcoordCount(1)
        // == stride(20) is the spec's own stride law, checked here rather
        // than just cited.
        sr3mesh::LayoutInfo info = sr3mesh::layoutInfoFor(channel.layoutCode);
        std::printf("[channel %d] layout code 0 (sr3mesh::layoutInfoFor(0): known=%d base=%u "
                    "hasNormal=%d hasTangent=%d hasSkinning=%d): decoding POSITION(+0,FLOAT3) + "
                    "NORMAL(+12,UBYTE4N) + TEXCOORD0(+16,SHORT2/%.0f) - the real UV-bearing render "
                    "geometry, not the UV-less layout-code-24 companion stream this command drew "
                    "before\n",
                    resolvedChannel, info.known ? 1 : 0, info.base, info.hasNormal ? 1 : 0,
                    info.hasTangent ? 1 : 0, info.hasSkinning ? 1 : 0,
                    static_cast<double>(sr3mesh::kTexcoordScale));
    }
    if (groupArg >= 0 && !mesh.drawGroupsLocated()) {
        std::fprintf(stderr, "--group given but draw groups are not located in this mesh\n");
        return 2;
    }
    if (groupArg >= 0 && static_cast<size_t>(groupArg) >= mesh.drawGroups().size()) {
        std::fprintf(stderr, "--group %d does not exist (%zu available)\n", groupArg,
                     mesh.drawGroups().size());
        return 2;
    }

    // Which (group index, rangeMask) pairs to actually render. An explicit
    // --group renders exactly that one group's own channel-owned ranges
    // (legacy single-group inspection). Auto mode (no --group) UNIONS the
    // resolved channel's own ranges across EVERY located group - see this
    // function's own doc comment for the real, measured finding (a channel's
    // ranges can be split, gap-free and non-overlapping, across more than
    // one "group") that makes this the correct default rather than group 0
    // alone. Also where each real draw range's real materialId is reported,
    // and where the real material/texture binding investigation's honest
    // result is stated.
    struct GroupTarget {
        size_t groupIndex = 0;
        std::vector<bool> rangeMask;
        size_t rangesForThisChannel = 0;
    };
    std::vector<GroupTarget> targets;
    if (mesh.drawGroupsLocated()) {
        std::set<uint32_t> materialIds;
        const size_t firstGroup = groupArg >= 0 ? static_cast<size_t>(groupArg) : 0;
        const size_t lastGroupExclusive =
            groupArg >= 0 ? static_cast<size_t>(groupArg) + 1 : mesh.drawGroups().size();
        for (size_t gi = firstGroup; gi < lastGroupExclusive; ++gi) {
            const auto& group = mesh.drawGroups()[gi];
            GroupTarget t;
            t.groupIndex = gi;
            t.rangeMask.assign(group.size(), false);
            for (size_t r = 0; r < group.size(); ++r) {
                if (group[r].submeshIndex != channelIndex) continue;
                t.rangeMask[r] = true;
                ++t.rangesForThisChannel;
                materialIds.insert(group[r].materialId);
                std::printf("  [group %zu range %zu] materialId=%u submeshIndex=%u startIndex=%u "
                            "indexCount=%u minVertex=%u maxVertex=%u\n",
                            gi, r, group[r].materialId, group[r].submeshIndex, group[r].startIndex,
                            group[r].indexCount, group[r].minVertex, group[r].maxVertex);
            }
            if (t.rangesForThisChannel > 0) targets.push_back(std::move(t));
        }
        if (targets.empty()) {
            std::fprintf(stderr, "channel %d has no draw ranges in %s - nothing to render\n",
                         resolvedChannel,
                         groupArg >= 0 ? ("group " + std::to_string(groupArg)).c_str() : "any located group");
            return 1;
        }
        std::printf("[groups] %zu LOD group(s) located - rendering channel %d's own ranges from %zu of "
                    "them (%s)\n",
                    mesh.drawGroups().size(), resolvedChannel, targets.size(),
                    groupArg >= 0 ? "explicit --group, single-group inspection mode"
                                  : "auto: union across every located group, see doc comment");
        size_t totalRangesForChannel = 0;
        for (const auto& t : targets) totalRangesForChannel += t.rangesForThisChannel;
        std::printf("[material] %zu draw range(s) across %zu group(s) belong to channel %d (this "
                    "channel's own submeshIndex filter), %zu distinct real materialId(s)\n",
                    totalRangesForChannel, targets.size(), resolvedChannel, materialIds.size());
        std::printf(
            "[material] NO confirmed materialId->texture/shader binding for zone data (genuine, "
            "checked dead end, not invented): the vehicle-style 0x424BD00D GeometryBlock sub-header "
            "never occurs in any real .czn_pc (0/2,971 files); .czh_pc's own alternate name table "
            "(ZoneHeader::materialBlock()) is 100%% non-texture for this file (71/71 names); a "
            "peer-relayed cc:0-texture-stem-family hypothesis was tested population-wide and matches "
            "only 1.0%% of real Mesh blocks (381/37,147) and 11.5%% of real .czn_pc files (53/460, "
            "mostly degenerate 1-vs-1 cases) - a clean negative. Each real draw range is still drawn "
            "separately with its own real materialId (MeshDrawMode::MaterialIdAsColour - flat colour "
            "per material, never a neighbour's texture), not textured.\n");
    } else {
        // Draw groups not located at all - fall back to one "target" with no
        // mask, exactly the pre-existing behaviour for this (rare, not hit
        // by the frozen baseline file) case: upload() itself falls back to
        // the whole index buffer as one strip when drawGroupsLocated() is
        // false.
        GroupTarget t;
        t.groupIndex = 0;
        targets.push_back(std::move(t));
    }

    std::string error;
    sr3render::Window window;
    if (!window.create("SR3 Viewer - zone " + cznName, winWidth, winHeight, error, visible)) {
        std::fprintf(stderr, "window creation failed: %s\n", error.c_str());
        return 1;
    }
    sr3render::RenderDevice device;
    if (!device.initialise(16, 16, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());
    sr3render::SwapChain swapChain;
    if (!swapChain.create(device.device(), window.nativeHandle(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "swap chain creation failed: %s\n", error.c_str());
        return 1;
    }

    // One MeshRenderer per target (group, rangeMask) pair - the exact same
    // "shared depth buffer, N upload()+draw() calls composited into one
    // frame" pattern tools/tree_baseline_render.cpp already established for
    // its own multi-LOD-slot case (see this function's own doc comment for
    // why more than one target is needed here by default). Only the FIRST
    // renderer owns the depth buffer; every later one's draw() call relies
    // on that SAME view being bound externally below, same convention.
    std::vector<std::unique_ptr<sr3render::MeshRenderer>> renderers;
    float mn[3] = {1e30f, 1e30f, 1e30f};
    float mx[3] = {-1e30f, -1e30f, -1e30f};
    uint32_t totalIndexCount = 0, totalTriangles = 0;
    for (const auto& t : targets) {
        auto r = std::make_unique<sr3render::MeshRenderer>();
        if (!r->initialise(device.device(), error)) {
            std::fprintf(stderr, "mesh renderer init failed (group %zu): %s\n", t.groupIndex, error.c_str());
            return 1;
        }
        const std::vector<bool>* maskPtr = t.rangeMask.empty() ? nullptr : &t.rangeMask;
        if (!r->upload(device.device(), mesh, channelIndex, 0, error, t.groupIndex, nullptr, nullptr,
                       maskPtr)) {
            std::fprintf(stderr, "mesh upload failed (group %zu): %s\n", t.groupIndex, error.c_str());
            return 1;
        }
        if (renderers.empty()) {
            if (!r->createDepth(device.device(), winWidth, winHeight, error)) {
                std::fprintf(stderr, "depth buffer failed: %s\n", error.c_str());
                return 1;
            }
        }
        const float* rmn = r->boundsMin();
        const float* rmx = r->boundsMax();
        for (int c = 0; c < 3; ++c) {
            if (rmn[c] < mn[c]) mn[c] = rmn[c];
            if (rmx[c] > mx[c]) mx[c] = rmx[c];
        }
        totalIndexCount += r->indexCount();
        totalTriangles += r->trianglesEmitted();
        renderers.push_back(std::move(r));
    }
    uint32_t depthWidth = winWidth, depthHeight = winHeight;

    std::printf("bounds   : %.3f x %.3f x %.3f   (min Y %.3f)\n", mx[0] - mn[0], mx[1] - mn[1],
                mx[2] - mn[2], mn[1]);
    std::printf("topology : %zu group(s) rendered, %u indices -> %u non-degenerate triangles total\n",
                renderers.size(), totalIndexCount, totalTriangles);
    std::printf("camera   : orbit yaw=%.1fdeg pitch=%.1fdeg distanceScale=%.2f (CHOSEN framing, "
                "not recovered data)\n",
                static_cast<double>(yaw * 180.0f / 3.14159265f),
                static_cast<double>(pitch * 180.0f / 3.14159265f),
                static_cast<double>(distanceScale));

    int frames = 0;
    while (window.pumpMessages()) {
        if (!swapChain.resize(window.width(), window.height(), error)) {
            std::fprintf(stderr, "swap chain resize failed: %s\n", error.c_str());
            break;
        }
        if (swapChain.width() != depthWidth || swapChain.height() != depthHeight) {
            if (renderers.front()->createDepth(device.device(), swapChain.width(), swapChain.height(),
                                               error)) {
                depthWidth = swapChain.width();
                depthHeight = swapChain.height();
            }
        }

        float viewProjection[16];
        sr3render::buildOrbitViewProjection(
            mn, mx, yaw, pitch, distanceScale,
            static_cast<float>(swapChain.width()) / static_cast<float>(swapChain.height()),
            viewProjection);

        swapChain.bind(device.context());
        ID3D11RenderTargetView* rtv = swapChain.renderTargetView();
        device.context()->OMSetRenderTargets(1, &rtv, renderers.front()->depthView());
        swapChain.clear(device.context(), 0.05f, 0.05f, 0.08f, 1.0f);
        renderers.front()->clearDepth(device.context()); // the ONE shared depth texture every renderer targets

        // No real texture/shader is resolvable for zone materials (see this
        // function's own doc comment + the [material] stdout lines above) -
        // MaterialIdAsColour draws each real draw range separately
        // (one DrawIndexed per range, via subDraws_) coloured by its own
        // real materialId, the same "flat colour, never invented" fallback
        // drawTextured() already uses elsewhere in this codebase when a
        // material's texture fails to resolve.
        for (auto& r : renderers) {
            r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::MaterialIdAsColour, nullptr);
        }

        if (!capturePath.empty() && frames == captureFrame) {
            std::vector<uint8_t> pixels;
            if (swapChain.capture(device.context(), pixels, error)) {
                if (sr3render::writePng(capturePath, swapChain.width(), swapChain.height(), pixels,
                                        error)) {
                    std::printf("captured : %s (frame %d)\n", capturePath.c_str(), frames);
                } else {
                    std::fprintf(stderr, "capture png failed: %s\n", error.c_str());
                }
            } else {
                std::fprintf(stderr, "capture failed: %s\n", error.c_str());
            }
        }

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) break;
    }

    std::printf("frames   : %d presented\n", frames);
    return 0;
}


// ---------------------------------------------------------------------
// zone-composite: INVESTIGATION-ONLY command, NOT a golden-baseline
// target. Renders vertex-layout-code-24 (this tile's OLD baseline,
// tests/golden/zone_tile/*_code24_proxy.*) and vertex-layout-code-0 (the
// CURRENT baseline, tests/golden/zone_tile/COMMAND.txt) TOGETHER, into ONE
// shared frame buffer, from the SAME real Mesh sub-block - to empirically
// test whether code-0's UV-bearing facade/window quads sit ON code-24's
// UV-less shell surfaces (same building, two visual layers) or are
// offset/unrelated (independent geometry, keeping the then-standing
// "probable proxy" reading plausible).
//
// RESULT (2026-09-30, this command's own first real run - see
// tests/golden/zone_tile/_investigation_composite.png and
// _investigation_composite_angle2.png, two independent camera angles):
// they ALIGN. Code-0's per-material colour mosaic (window/facade quad
// grids) sits bounded within code-24's flat-red shell faces at both camera
// angles - never floating in empty space away from a red volume, and a
// code-0 face viewed edge-on compresses to a thin sliver that hugs the red
// shell's own silhouette edge rather than overshooting it (only possible
// if the two are very nearly coplanar). This supports "same building, two
// visual layers", not independent/offset proxy geometry - see this
// function's own investigation report for the full account. Code-24's real
// ROLE is therefore back OPEN (a real, UV-less SHELL surface - not proven
// to be shadow/occlusion/collision, not disproven either) rather than
// "probable proxy". This command renders the evidence; the read above is
// this session's own honest visual assessment of it, not a certainty -
// coverage is partial (code-0 has 3,062 elements vs code-24's 4,642, so
// several code-24 faces - e.g. the cylindrical tanks, roof structures -
// have no code-0 counterpart at all in this tile) and only one tile was
// checked.
//
// Deliberately a SEPARATE function/command from runZoneTile() above:
// runZoneTile() itself is UNTOUCHED by this addition, so the frozen
// zone_tile golden baseline (tests/golden/zone_tile/COMMAND.txt, checked
// by tools/golden_scene_check.cpp's checkZoneTileScene()) cannot regress
// from this command's existence. Reuses the exact same real decode/upload
// mechanism runZoneTile() already uses per channel
// (sr3zone::ZoneGeometry::locate(), sr3mesh::MeshBlock::channels(),
// MeshRenderer::upload(), the same "union this channel's own draw ranges
// across every located LOD group" resolution runZoneTile()'s own doc
// comment explains at length) - nothing about how either channel is
// DECODED is reinvented here; only that both channels' already-working
// draw paths are composited into one frame instead of one being picked.
//
// Colour scheme (diagnostic, not cosmetic, matching MeshDrawMode's own doc
// comment convention): layout code 24 draws as one flat SOLID colour
// (MeshDrawMode::FlatTint, bright red) across its whole surface regardless
// of material id, so it reads as one continuous "shell" a human can
// visually trace. Layout code 0 keeps its own established
// MeshDrawMode::MaterialIdAsColour (byte-for-byte the same mode/colours
// runZoneTile()'s current baseline already uses) - a mosaic of per-
// material hues. If code-0's mosaic sits ON the red shell's outer faces,
// that supports the "same building, two layers" reading; if it floats
// separate from or offset from the red shell, that keeps the "unrelated
// companion stream" reading plausible.
int runZoneTileComposite(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: sr3_viewer zone-composite <archive.vpp_pc> <name.czn_pc>\n"
                     "                       [--size W H] [--frames N] [--capture <out.png>]\n"
                     "                       [--capture-frame N] [--yaw D] [--pitch D] "
                     "[--distance F] [--visible]\n"
                     "       INVESTIGATION-ONLY: draws layout-code-24 (flat red, MeshDrawMode::\n"
                     "       FlatTint) and layout-code-0 (its own established MeshDrawMode::\n"
                     "       MaterialIdAsColour) together in ONE frame, from the SAME real Mesh\n"
                     "       sub-block, to test whether they are the same building's shell +\n"
                     "       facade or unrelated geometry. NOT a golden-baseline command - see\n"
                     "       runZoneTile()'s own `zone` command for that (unchanged by this one).\n");
        return 1;
    }
    const std::string archivePath = argv[2];
    const std::string cznName = argv[3];

    uint32_t winWidth = 1100, winHeight = 700;
    int maxFrames = -1;
    int captureFrame = 0;
    std::string capturePath;
    // Same CHOSEN default camera framing as runZoneTile()'s own `zone`
    // command, so the two are visually comparable.
    float yaw = 0.6f, pitch = 0.35f, distanceScale = 1.8f;
    bool visible = false; // off by default - see sr3render/window.h's own doc comment

    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--size" && i + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++i]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        } else if (arg == "--capture" && i + 1 < argc) {
            capturePath = argv[++i];
        } else if (arg == "--capture-frame" && i + 1 < argc) {
            captureFrame = std::atoi(argv[++i]);
        } else if (arg == "--yaw" && i + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && i + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--distance" && i + 1 < argc) {
            distanceScale = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--visible") {
            visible = true;
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string gznName = cznName;
    {
        size_t dot = gznName.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < gznName.size()) gznName[dot + 1] = 'g';
    }

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", archivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> cznBytes, gznBytes;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cznName, cznBytes, gznBytes, gznName)) {
            std::fprintf(stderr, "could not find '%s' in %s\n", cznName.c_str(), archivePath.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    std::printf("[zone-composite] %s: czn=%zu bytes gzn=%zu bytes\n", cznName.c_str(), cznBytes.size(),
                gznBytes.size());

    std::vector<sr3zone::ZoneMeshBlockEntry> blocks;
    try {
        blocks = sr3zone::ZoneGeometry::locate(
            vpp::ByteView(cznBytes.data(), cznBytes.size()),
            vpp::ByteView(gznBytes.data(), gznBytes.size()));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "zone geometry locate failed: %s\n", ex.what());
        return 1;
    }
    std::printf("[zone-composite] %zu Mesh sub-block(s) located\n", blocks.size());

    // Find the first block carrying BOTH a layoutCode==24 channel and a
    // layoutCode==0 channel - this investigation is only meaningful when
    // both live in the SAME Mesh sub-block (same real geometry group), the
    // situation already established for this tile's own block (HANDOFF
    // Sec9.127/9.129: block at cznOffset=16324 has exactly 3 channels,
    // layoutCode 24/0/2).
    int resolvedBlock = -1;
    size_t channel24 = 0, channel0 = 0;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& channels = blocks[bi].block.channels();
        int c24 = -1, c0 = -1;
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            if (channels[ci].layoutCode == 24 && c24 < 0) c24 = static_cast<int>(ci);
            if (channels[ci].layoutCode == 0 && c0 < 0) c0 = static_cast<int>(ci);
        }
        if (c24 >= 0 && c0 >= 0) {
            resolvedBlock = static_cast<int>(bi);
            channel24 = static_cast<size_t>(c24);
            channel0 = static_cast<size_t>(c0);
            break;
        }
    }
    if (resolvedBlock < 0) {
        std::fprintf(stderr,
                     "no block in this zone tile carries BOTH a layoutCode-24 and a "
                     "layoutCode-0 channel - nothing this investigation command can compare\n");
        return 1;
    }
    const sr3zone::ZoneMeshBlockEntry& entry = blocks[static_cast<size_t>(resolvedBlock)];
    const sr3mesh::MeshBlock& mesh = entry.block;
    std::printf("[zone-composite] block %d: channel %zu is layoutCode=24 (elementCount=%u), "
                "channel %zu is layoutCode=0 (elementCount=%u)\n",
                resolvedBlock, channel24, mesh.channels()[channel24].elementCount, channel0,
                mesh.channels()[channel0].elementCount);

    // Same "union this channel's own draw ranges across every located LOD
    // group" resolution runZoneTile() already documents at length (its own
    // default, --group<0 path) - duplicated here rather than shared, so
    // that function's own tested code path stays completely untouched.
    // Both channels need it independently.
    struct GroupTarget {
        size_t groupIndex = 0;
        std::vector<bool> rangeMask;
        size_t rangesForThisChannel = 0;
    };
    auto resolveTargets = [&](size_t channelIndex) {
        std::vector<GroupTarget> targets;
        if (mesh.drawGroupsLocated()) {
            for (size_t gi = 0; gi < mesh.drawGroups().size(); ++gi) {
                const auto& group = mesh.drawGroups()[gi];
                GroupTarget t;
                t.groupIndex = gi;
                t.rangeMask.assign(group.size(), false);
                for (size_t r = 0; r < group.size(); ++r) {
                    if (group[r].submeshIndex != channelIndex) continue;
                    t.rangeMask[r] = true;
                    ++t.rangesForThisChannel;
                }
                if (t.rangesForThisChannel > 0) targets.push_back(std::move(t));
            }
        } else {
            GroupTarget t;
            t.groupIndex = 0;
            targets.push_back(std::move(t));
        }
        return targets;
    };
    std::vector<GroupTarget> targets24 = resolveTargets(channel24);
    std::vector<GroupTarget> targets0 = resolveTargets(channel0);
    std::printf("[zone-composite] channel %zu (code24): %zu group-target(s)\n", channel24,
                targets24.size());
    std::printf("[zone-composite] channel %zu (code0): %zu group-target(s)\n", channel0,
                targets0.size());
    if (targets24.empty() || targets0.empty()) {
        std::fprintf(stderr,
                     "one of the two channels has no draw ranges in any located group - "
                     "nothing to composite\n");
        return 1;
    }

    std::string error;
    sr3render::Window window;
    if (!window.create("SR3 Viewer - zone-composite " + cznName, winWidth, winHeight, error,
                       visible)) {
        std::fprintf(stderr, "window creation failed: %s\n", error.c_str());
        return 1;
    }
    sr3render::RenderDevice device;
    if (!device.initialise(16, 16, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());
    sr3render::SwapChain swapChain;
    if (!swapChain.create(device.device(), window.nativeHandle(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "swap chain creation failed: %s\n", error.c_str());
        return 1;
    }

    // One MeshRenderer per (channel, group-target) pair - the same shared-
    // depth-buffer / N-draw-calls-composited-into-one-frame pattern
    // runZoneTile() and tools/tree_baseline_render.cpp already use for
    // their own multi-target case. code24Renderers draw FlatTint red;
    // code0Renderers keep MaterialIdAsColour, unchanged.
    std::vector<std::unique_ptr<sr3render::MeshRenderer>> code24Renderers, code0Renderers;
    float mn[3] = {1e30f, 1e30f, 1e30f};
    float mx[3] = {-1e30f, -1e30f, -1e30f};
    bool haveDepth = false;
    auto uploadTargets = [&](size_t channelIndex, const std::vector<GroupTarget>& tgts,
                             std::vector<std::unique_ptr<sr3render::MeshRenderer>>& out) -> bool {
        for (const auto& t : tgts) {
            auto r = std::make_unique<sr3render::MeshRenderer>();
            if (!r->initialise(device.device(), error)) {
                std::fprintf(stderr, "mesh renderer init failed (channel %zu group %zu): %s\n",
                             channelIndex, t.groupIndex, error.c_str());
                return false;
            }
            const std::vector<bool>* maskPtr = t.rangeMask.empty() ? nullptr : &t.rangeMask;
            if (!r->upload(device.device(), mesh, channelIndex, 0, error, t.groupIndex, nullptr,
                           nullptr, maskPtr)) {
                std::fprintf(stderr, "mesh upload failed (channel %zu group %zu): %s\n", channelIndex,
                             t.groupIndex, error.c_str());
                return false;
            }
            if (!haveDepth) {
                if (!r->createDepth(device.device(), winWidth, winHeight, error)) {
                    std::fprintf(stderr, "depth buffer failed: %s\n", error.c_str());
                    return false;
                }
                haveDepth = true;
            }
            const float* rmn = r->boundsMin();
            const float* rmx = r->boundsMax();
            for (int c = 0; c < 3; ++c) {
                if (rmn[c] < mn[c]) mn[c] = rmn[c];
                if (rmx[c] > mx[c]) mx[c] = rmx[c];
            }
            out.push_back(std::move(r));
        }
        return true;
    };
    if (!uploadTargets(channel24, targets24, code24Renderers)) return 1;
    if (!uploadTargets(channel0, targets0, code0Renderers)) return 1;

    uint32_t depthWidth = winWidth, depthHeight = winHeight;
    sr3render::MeshRenderer* depthOwner = code24Renderers.front().get();

    std::printf("bounds   : %.3f x %.3f x %.3f   (min Y %.3f) [combined, both channels]\n",
                mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], mn[1]);
    std::printf("camera   : orbit yaw=%.1fdeg pitch=%.1fdeg distanceScale=%.2f (CHOSEN framing, "
                "same defaults as runZoneTile()'s own `zone` command)\n",
                static_cast<double>(yaw * 180.0f / 3.14159265f),
                static_cast<double>(pitch * 180.0f / 3.14159265f),
                static_cast<double>(distanceScale));
    std::printf("colour   : code24 (channel %zu) = FLAT RED (0.95,0.12,0.12); code0 (channel %zu) "
                "= MaterialIdAsColour (unchanged per-material hues)\n",
                channel24, channel0);

    const float code24Tint[4] = {0.95f, 0.12f, 0.12f, 1.0f};

    int frames = 0;
    while (window.pumpMessages()) {
        if (!swapChain.resize(window.width(), window.height(), error)) {
            std::fprintf(stderr, "swap chain resize failed: %s\n", error.c_str());
            break;
        }
        if (swapChain.width() != depthWidth || swapChain.height() != depthHeight) {
            if (depthOwner->createDepth(device.device(), swapChain.width(), swapChain.height(),
                                        error)) {
                depthWidth = swapChain.width();
                depthHeight = swapChain.height();
            }
        }

        float viewProjection[16];
        sr3render::buildOrbitViewProjection(
            mn, mx, yaw, pitch, distanceScale,
            static_cast<float>(swapChain.width()) / static_cast<float>(swapChain.height()),
            viewProjection);

        swapChain.bind(device.context());
        ID3D11RenderTargetView* rtv = swapChain.renderTargetView();
        device.context()->OMSetRenderTargets(1, &rtv, depthOwner->depthView());
        swapChain.clear(device.context(), 0.05f, 0.05f, 0.08f, 1.0f);
        depthOwner->clearDepth(device.context()); // the ONE shared depth texture every renderer targets

        // code24 first (flat red shell), then code0 (per-material mosaic)
        // on top - draw order does not affect the result (depth test
        // decides visibility either way), kept this way only because it
        // reads naturally as "shell, then detail layer".
        for (auto& r : code24Renderers) {
            r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::FlatTint, nullptr,
                   nullptr, code24Tint);
        }
        for (auto& r : code0Renderers) {
            r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::MaterialIdAsColour,
                   nullptr);
        }

        if (!capturePath.empty() && frames == captureFrame) {
            std::vector<uint8_t> pixels;
            if (swapChain.capture(device.context(), pixels, error)) {
                if (sr3render::writePng(capturePath, swapChain.width(), swapChain.height(), pixels,
                                        error)) {
                    std::printf("captured : %s (frame %d)\n", capturePath.c_str(), frames);
                } else {
                    std::fprintf(stderr, "capture png failed: %s\n", error.c_str());
                }
            } else {
                std::fprintf(stderr, "capture failed: %s\n", error.c_str());
            }
        }

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) break;
    }

    std::printf("frames   : %d presented\n", frames);
    return 0;
}
