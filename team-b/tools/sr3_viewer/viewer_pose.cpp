#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_common.h"
#include "viewer_multichannel.h"
#include "viewer_skinning.h"
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
#include "sr3geometry/character_matlib.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"
#include "vpp/container.h"

using namespace sr3viewer;


int runPose(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: sr3_viewer pose <archive.vpp_pc> <name.ccmesh_pc> <name.rig_pc> "
                     "<out_prefix>\n"
                     "                       [--mode uv|checker|normal|material]\n"
                     "                       [--size W H] [--yaw D] [--pitch D]\n"
                     "                       [--channel N] [--group N] [--angle D]\n"
                     "                       [--legacy-skinning]\n"
                     "       writes <out_prefix>_bind.png and <out_prefix>_posed.png\n"
                     "       Skinning convention is AUTO-DETECTED per mesh (HANDOFF Sec9.63.9): "
                     "single-set bone\n"
                     "       palette -> palette + (-x,-y,-z) [DEFAULT]; multi-set -> per-draw-range "
                     "sets\n"
                     "       (Sec9.63.10), refusing only if that selector or its per-vertex "
                     "assignment is\n"
                     "       ambiguous; no palette declared -> REFUSED (no spec-backed rule; "
                     "vertex Sec9 / rig Sec11.15).\n"
                     "       --legacy-skinning: force the pre-promotion direct-index + (x,-y,-z) "
                     "reading even on\n"
                     "                          any mesh (RETRACTED in spec; debugging only), to reproduce Sec9.56.x's "
                     "pre-Sec9.63 numbers.\n");
        return 1;
    }
    const std::string archivePath = argv[2];
    const std::string cmeshName = argv[3];
    const std::string rigName = argv[4];
    const std::string outPrefix = argv[5];
    if (outPrefix.rfind("--", 0) == 0) {
        std::fprintf(stderr,
                     "output prefix '%s' looks like a flag - it is POSITIONAL and must come "
                     "before any --options\n", outPrefix.c_str());
        return 2;
    }
    uint32_t width = 720, height = 900;
    float yaw = 0.0f, pitch = 0.1f;
    size_t channelIndex = 0;
    size_t drawGroup = 0;
    float angleDegrees = 45.0f; // matches validate_pose.cpp's confirmed test exactly
    sr3render::MeshDrawMode mode = sr3render::MeshDrawMode::Checker;
    // HANDOFF Sec9.63.9: forces the pre-promotion direct-index + (x,-y,-z)
    // reading even on a single-set mesh, so Sec9.56.x's pre-Sec9.63 numbers
    // remain reproducible on demand. Off by default: the promoted default
    // is auto-detected per mesh (see resolveSkinningMode() above).
    bool legacySkinning = false;

    for (int i = 6; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--legacy-skinning") {
            legacySkinning = true;
        } else if (arg == "--size" && i + 2 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--yaw" && i + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && i + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--channel" && i + 1 < argc) {
            channelIndex = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--group" && i + 1 < argc) {
            drawGroup = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--angle" && i + 1 < argc) {
            angleDegrees = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string m = argv[++i];
            if (m == "uv") mode = sr3render::MeshDrawMode::UvAsColour;
            else if (m == "checker") mode = sr3render::MeshDrawMode::Checker;
            else if (m == "normal") mode = sr3render::MeshDrawMode::NormalAsColour;
            else if (m == "material") mode = sr3render::MeshDrawMode::MaterialIdAsColour;
            else if (m == "textured") {
                // Deliberately unsupported here: runMesh's textured path
                // resolves per-material bindings/textures from the archive
                // (~60 lines), and duplicating that for a command whose job
                // is checking POSITION deformation - not texturing - would
                // be a second implementation of logic already written and
                // exercised once in runMesh, for no benefit to what this
                // command is verifying.
                std::fprintf(stderr,
                             "--mode textured is not supported by 'pose' (see source comment) - "
                             "use uv|checker|normal|material\n");
                return 2;
            } else {
                std::fprintf(stderr, "unknown --mode '%s' (uv, checker, normal, material)\n",
                             m.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string gmeshName = cmeshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", archivePath.c_str());
        return 1;
    }
    std::vector<uint8_t> cb, gb, rb, unusedSibling;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cmeshName, cb, gb, gmeshName)) {
            std::fprintf(stderr, "could not find '%s'\n", cmeshName.c_str());
            return 1;
        }
        if (!findEntry(container, rigName, rb, unusedSibling, "")) {
            std::fprintf(stderr, "could not find '%s'\n", rigName.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    if (gb.empty()) {
        std::fprintf(stderr, "found '%s' but not its paired '%s'\n", cmeshName.c_str(),
                     gmeshName.c_str());
        return 1;
    }
    std::printf("mesh     : %s (%zu bytes) + %s (%zu bytes)\n", cmeshName.c_str(), cb.size(),
                gmeshName.c_str(), gb.size());
    std::printf("rig      : %s (%zu bytes)\n", rigName.c_str(), rb.size());

    sr3geometry::MaterialBlock material =
        sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry =
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    if (!geometry.hasMeshSubBlock()) {
        std::fprintf(stderr, "no Mesh sub-block in this file\n");
        return 1;
    }
    sr3mesh::MeshBlock mesh =
        sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                  geometry.meshSubBlockOffset(), vpp::ByteView(gb.data(), gb.size()));
    std::printf("mesh blk : check 0x%08X, g-length %u (file %zu), %zu channel(s), %u indices\n",
                mesh.checkValue(), mesh.gLength(), gb.size(), mesh.channels().size(),
                mesh.indexCount());
    if (channelIndex >= mesh.channels().size()) {
        std::fprintf(stderr, "channel %zu out of range\n", channelIndex);
        return 1;
    }
    if (mesh.drawGroupsLocated() && drawGroup >= mesh.drawGroups().size()) {
        std::fprintf(stderr, "group %zu does not exist (%zu available)\n", drawGroup,
                     mesh.drawGroups().size());
        return 2;
    }
    const auto& channel = mesh.channels()[channelIndex];
    std::printf("channel  : layout %u, %u texcoord set(s), stride %zu, %u vertices%s\n",
                channel.layoutCode, channel.texcoordCount, channel.stride(), channel.elementCount,
                channel.strideMatchesLaw ? " (stride matches the spec law)" : " (STRIDE LAW MISMATCH)");
    if (!sr3mesh::layoutInfoFor(channel.layoutCode).hasSkinning) {
        std::fprintf(stderr,
                     "channel %zu's layout carries no blend weights/indices - nothing to pose\n",
                     channelIndex);
        return 1;
    }

    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
    const auto& bones = rig.bones();
    std::printf("rig      : %zu bones, hash table %s\n", bones.size(),
                rig.hashTableMatchesNames() ? "OK" : "MISMATCH");

    // Same posable-bone pick as validate_pose.cpp: first name-list match
    // ("upperarm", "forearm", "thigh", "calf", "shoulder", in that order)
    // that HAS A CHILD, so the rotation is visible on a real limb rather
    // than on a leaf bone with nothing attached to swing.
    const char* wanted[] = {"upperarm", "forearm", "thigh", "calf", "shoulder"};
    int target = -1;
    for (const char* w : wanted) {
        for (size_t i = 0; i < bones.size() && target < 0; ++i) {
            std::string nm = bones[i].name;
            for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (nm.find(w) != std::string::npos) {
                bool hasChild = false;
                for (const auto& b2 : bones)
                    if (!b2.isRoot() && b2.parentIndex == i) { hasChild = true; break; }
                if (hasChild) target = static_cast<int>(i);
            }
        }
        if (target >= 0) break;
    }
    if (target < 0) {
        std::fprintf(stderr,
                     "no posable bone (upperarm/forearm/thigh/calf/shoulder with a child) found "
                     "in this rig\n");
        return 1;
    }

    std::vector<char> inSubtree(bones.size(), 0);
    inSubtree[static_cast<size_t>(target)] = 1;
    for (size_t i = 0; i < bones.size(); ++i) // parent < child, enforced by the reader
        if (!bones[i].isRoot() && inSubtree[bones[i].parentIndex]) inSubtree[i] = 1;
    int subtreeSize = 0;
    for (char c2 : inSubtree) subtreeSize += c2;

    const float angleRadians = angleDegrees * 3.14159265f / 180.0f;
    std::printf("pose     : joint [%d] \"%s\"  subtree %d/%zu bones  +%.1f deg about Z (mesh "
                "space)\n", target, bones[static_cast<size_t>(target)].name.c_str(), subtreeSize,
                bones.size(), static_cast<double>(angleDegrees));

    // HANDOFF Sec9.63.9: auto-detect palette vs. direct-index skinning from
    // the mesh's own declaration (see resolveSkinningMode() above this
    // function). A multi-set mesh refuses here, before any GPU work.
    const SkinningModeResult skinModeResult = resolveSkinningMode(mesh, legacySkinning);
    if (isSkinningRefusal(skinModeResult.mode)) {
        std::fprintf(stderr, "%s\n", skinModeResult.refusalMessage.c_str());
        return 1;
    }
    const bool useBonePalette = skinModeResult.mode == SkinningMode::kPaletteSingleSet ||
                                skinModeResult.mode == SkinningMode::kPaletteMultiSet;
    std::printf("skinning : %s\n", skinningModeLabel(skinModeResult.mode));

    // Rest positions -> MESH space (sr3rig/pose.h's header comment: this
    // module does not know about the rig<->mesh axis flip, and mesh-space
    // vertices are what get skinned - verts[i].position is already
    // mesh-space, same convention every other command in this file uses).
    std::vector<uint32_t> parentIdx(bones.size());
    std::vector<std::array<float, 3>> restMesh(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        parentIdx[i] = bones[i].parentIndex;
        restMesh[i] = useBonePalette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                     : sr3rig::rigToMeshSpace(bones[i].restPosition);
    }
    std::vector<sr3rig::Rotation3> rotations(bones.size(), sr3rig::identityRotation3());
    rotations[static_cast<size_t>(target)] =
        sr3rig::rotationFromAxisAngle({0.0f, 0.0f, 1.0f}, angleRadians);

    std::vector<sr3rig::Mat3x4> bindSkin =
        sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
    std::vector<sr3rig::Mat3x4> poseSkin =
        sr3rig::computeSkinningMatrices(parentIdx, restMesh, &rotations);

    std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(channelIndex);
    if (verts.empty()) {
        std::fprintf(stderr, "channel produced no vertices\n");
        return 1;
    }
    sr3render::VertexDuplication vertexDuplication; // HANDOFF Sec9.68; stays empty except for `reynolds`
    bool multiChannelNeeded = false; // HANDOFF <multi-channel section>: see below
    if (skinModeResult.mode == SkinningMode::kPaletteMultiSet) {
        // HANDOFF Sec9.63.10/Sec9.68: each draw range names its own palette
        // set, so the remap is per vertex via the group being drawn. If two
        // drawn ranges in that group claim one vertex for different sets,
        // duplicate it (Sec9.68) rather than refuse - `reynolds` is the one
        // shipped mesh (of 318) where this happens.
        sr3rig::BlendIndexRemapStats remap;
        sr3rig::VertexDuplicationResult dup =
            applyMultiSetSkinning(mesh, verts, bones.size(), drawGroup, remap);
        if (!dup.usable) {
            std::fprintf(stderr, "%s\n", dup.problem.c_str());
            return 1;
        }
        if (dup.duplicatesCreated != 0) {
            vertexDuplication.sourceIndex = dup.duplicateSourceIndex;
            vertexDuplication.redirectPerRange = dup.redirectPerRange;
            std::printf("dup      : %lld conflicting vertex-uses resolved by duplication "
                        "(HANDOFF Sec9.68)\n", dup.duplicatesCreated);
        }
        std::printf("palette  : %zu entries in %zu sets (rig %zu bones); group %zu: %zu vertices "
                    "total (%lld duplicated); lanes remapped=%lld dropped(no palette slot)=%lld "
                    "dropped(rig index out of range)=%lld\n",
                    mesh.bonePalette().size(), mesh.bonePaletteSets().size(), bones.size(),
                    drawGroup, verts.size(), dup.duplicatesCreated,
                    remap.lanesRemapped, remap.lanesDroppedPaletteOutOfRange,
                    remap.lanesDroppedRigOutOfRange);
        // A dropped lane means a vertex carries a blend index its range's
        // named set cannot hold - which on a correct reading never happens:
        // measured 0 dropped lanes on all 46 renderable multi-set meshes at
        // draw group 0 (HANDOFF Sec9.63.10/Sec9.68). It DOES happen on the
        // LOD groups of the two multi-channel meshes, where the range draws
        // from a channel this command did not upload (`alien_e01 --group 4`:
        // 2,213 lanes). WHICH channel is no longer open (fixed 2026-09-29,
        // HANDOFF Sec9.89/Sec9.103, rule 16): DrawRange::submeshIndex IS the
        // channel index, confirmed both on the 393-vehicle population AND
        // directly re-checked against alien_e01 itself here (28/28 real
        // draw ranges across all 8 groups, including group 4 specifically,
        // land exactly inside their own submeshIndex-selected channel's
        // real vertex count). FIXED (multi-channel section, 2026-09-29):
        // this used to be a hard refusal here - now the cause is KNOWN, so
        // instead of giving up, fall through to the real per-channel
        // multi-channel render (MultiChannelMultiSetRender, above) once
        // this block finishes printing its own (still accurate) diagnostic
        // numbers. Every SINGLE-channel mesh (single-set or multi-set)
        // still gets lanesDroppedPaletteOutOfRange == 0 here and never
        // reaches this branch at all - this is the regression guarantee,
        // structural rather than behavioural: the code above executes
        // first and unconditionally for every mesh.
        if (remap.lanesDroppedPaletteOutOfRange != 0) {
            std::printf(
                "channel  : %lld blend lane(s) in draw group %zu do not fit --channel %zu's own "
                "bone-palette set - this group's ranges span more than one vertex channel "
                "(DrawRange::submeshIndex, HANDOFF Sec9.89/Sec9.103/Sec9.104). Switching to a "
                "per-channel multi-channel render instead of refusing.\n",
                remap.lanesDroppedPaletteOutOfRange, drawGroup, channelIndex);
            multiChannelNeeded = true;
        }
    } else if (useBonePalette) {
        sr3rig::BlendIndexRemapStats remap;
        verts = sr3rig::remapBlendIndicesThroughPalette(verts, mesh.bonePalette(), bones.size(),
                                                         &remap);
        std::printf("palette  : %zu entries (rig %zu bones); lanes remapped=%lld "
                    "dropped(no palette slot)=%lld dropped(rig index out of range)=%lld\n",
                    mesh.bonePalette().size(), bones.size(), remap.lanesRemapped,
                    remap.lanesDroppedPaletteOutOfRange, remap.lanesDroppedRigOutOfRange);
    }

    if (multiChannelNeeded) {
        sr3render::RenderDevice mcDevice;
        std::string mcError;
        if (!mcDevice.initialise(width, height, mcError)) {
            std::fprintf(stderr, "device init failed: %s\n", mcError.c_str());
            return 1;
        }
        std::printf("device   : %s (%s)\n",
                    mcDevice.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                    mcDevice.adapterName().c_str());
        std::vector<MultiChannelFrame> frames;
        frames.push_back({outPrefix + "_bind.png", bindSkin});
        frames.push_back({outPrefix + "_posed.png", poseSkin});
        std::vector<std::vector<uint8_t>> framePixels;
        if (!runMultiChannelFrames(mcDevice, mesh, drawGroup, bones.size(), width, height, yaw, pitch,
                                   mode, frames, &framePixels)) {
            return 1;
        }
        if (framePixels.size() == 2 && framePixels[0].size() == framePixels[1].size() &&
            !framePixels[0].empty()) {
            size_t pixelCount = framePixels[0].size() / 4;
            size_t differing = 0;
            for (size_t i = 0; i + 3 < framePixels[0].size(); i += 4) {
                if (framePixels[0][i] != framePixels[1][i] ||
                    framePixels[0][i + 1] != framePixels[1][i + 1] ||
                    framePixels[0][i + 2] != framePixels[1][i + 2] ||
                    framePixels[0][i + 3] != framePixels[1][i + 3]) {
                    ++differing;
                }
            }
            std::printf("pixel diff (bind vs posed): %zu / %zu pixels differ (%.2f%%)\n", differing,
                        pixelCount,
                        pixelCount ? 100.0 * static_cast<double>(differing) / static_cast<double>(pixelCount)
                                   : 0.0);
        }
        return 0;
    }

    sr3rig::SkinningStats bindStats, poseStats;
    std::vector<std::array<float, 3>> bindPositions =
        sr3rig::skinVertices(verts, bindSkin, &bindStats);
    std::vector<std::array<float, 3>> posedPositions =
        sr3rig::skinVertices(verts, poseSkin, &poseStats);

    // BIND-POSE identity sanity check (same as validate_pose.cpp): every
    // Skin_i should reduce to Identity at all-identity local rotations, so
    // bindPositions should equal the original decoded positions to
    // ordinary float-summation error, not exactly zero.
    double maxBindVertDev = 0.0;
    float bindMin[3] = {1e30f, 1e30f, 1e30f}, bindMax[3] = {-1e30f, -1e30f, -1e30f};
    float poseMin[3] = {1e30f, 1e30f, 1e30f}, poseMax[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t vi = 0; vi < verts.size(); ++vi) {
        double dx = static_cast<double>(bindPositions[vi][0]) - verts[vi].position[0];
        double dy = static_cast<double>(bindPositions[vi][1]) - verts[vi].position[1];
        double dz = static_cast<double>(bindPositions[vi][2]) - verts[vi].position[2];
        double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > maxBindVertDev) maxBindVertDev = d;
        for (int c = 0; c < 3; ++c) {
            float bv = bindPositions[vi][static_cast<size_t>(c)];
            float pv = posedPositions[vi][static_cast<size_t>(c)];
            if (bv < bindMin[c]) bindMin[c] = bv;
            if (bv > bindMax[c]) bindMax[c] = bv;
            if (pv < poseMin[c]) poseMin[c] = pv;
            if (pv > poseMax[c]) poseMax[c] = pv;
        }
    }
    std::printf("bind-pose identity check: max |v' - v| = %.3e over %zu verts (tol 1e-5, matches "
                "stage 1's validate_pose.cpp figures)\n", maxBindVertDev, verts.size());
    std::printf("skinning stats: bind  lanesDropped=%lld maxOutOfRangeIdx=%lld\n",
                bindStats.lanesDropped, bindStats.maxOutOfRangeIndex);
    std::printf("                pose  lanesDropped=%lld maxOutOfRangeIdx=%lld\n",
                poseStats.lanesDropped, poseStats.maxOutOfRangeIndex);
    std::printf("bbox (CPU, mesh space):\n");
    std::printf("  bind : X[%.3f %.3f] Y[%.3f %.3f] Z[%.3f %.3f]\n", static_cast<double>(bindMin[0]),
                static_cast<double>(bindMax[0]), static_cast<double>(bindMin[1]),
                static_cast<double>(bindMax[1]), static_cast<double>(bindMin[2]),
                static_cast<double>(bindMax[2]));
    std::printf("  posed: X[%.3f %.3f] Y[%.3f %.3f] Z[%.3f %.3f]\n", static_cast<double>(poseMin[0]),
                static_cast<double>(poseMax[0]), static_cast<double>(poseMin[1]),
                static_cast<double>(poseMax[1]), static_cast<double>(poseMin[2]),
                static_cast<double>(poseMax[2]));

    sr3render::RenderDevice device;
    std::string error;
    if (!device.initialise(width, height, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());

    sr3render::MeshRenderer renderer;
    if (!renderer.initialise(device.device(), error)) {
        std::fprintf(stderr, "mesh renderer init failed: %s\n", error.c_str());
        return 1;
    }
    if (!renderer.createDepth(device.device(), width, height, error)) {
        std::fprintf(stderr, "depth buffer failed: %s\n", error.c_str());
        return 1;
    }

    // Upload the BIND pose first and build the camera from ITS bounds -
    // should match the existing unposed `mesh` command's framing (same
    // distanceScale, same yaw/pitch defaults). The SAME view-projection
    // matrix is then reused for the posed render too, so the camera does
    // not move between the two images: any silhouette growth visible
    // between them is the pose, not a reframed shot.
    const sr3render::VertexDuplication* dupPtr =
        vertexDuplication.sourceIndex.empty() ? nullptr : &vertexDuplication;
    if (!renderer.upload(device.device(), mesh, channelIndex, 0, error, drawGroup, &bindPositions,
                         dupPtr)) {
        std::fprintf(stderr, "bind-pose upload failed: %s\n", error.c_str());
        return 1;
    }
    const float* mn = renderer.boundsMin();
    const float* mx = renderer.boundsMax();
    std::printf("bounds(renderer, bind): %.3f x %.3f x %.3f\n", static_cast<double>(mx[0] - mn[0]),
                static_cast<double>(mx[1] - mn[1]), static_cast<double>(mx[2] - mn[2]));
    float viewProjection[16];
    sr3render::buildOrbitViewProjection(mn, mx, yaw, pitch, 1.9f,
                                        static_cast<float>(width) / static_cast<float>(height),
                                        viewProjection);

    auto renderOne = [&](const std::string& path, std::vector<uint8_t>& outPixels) -> bool {
        device.bindRenderTargetWithDepth(renderer.depthView());
        device.clear({28, 30, 38, 255});
        renderer.clearDepth(device.context());
        renderer.draw(device.context(), viewProjection, mode, nullptr);
        if (!device.readBack(outPixels, error)) {
            std::fprintf(stderr, "readback failed: %s\n", error.c_str());
            return false;
        }
        if (!sr3render::writePng(path, width, height, outPixels, error)) {
            std::fprintf(stderr, "png write failed: %s\n", error.c_str());
            return false;
        }
        std::printf("wrote    : %s\n", path.c_str());
        return true;
    };

    const std::string bindPath = outPrefix + "_bind.png";
    const std::string posedPath = outPrefix + "_posed.png";
    std::vector<uint8_t> bindPixels, posedPixels;

    if (!renderOne(bindPath, bindPixels)) return 1;

    if (!renderer.upload(device.device(), mesh, channelIndex, 0, error, drawGroup, &posedPositions,
                         dupPtr)) {
        std::fprintf(stderr, "posed upload failed: %s\n", error.c_str());
        return 1;
    }
    if (!renderOne(posedPath, posedPixels)) return 1;

    // Programmatic sanity check: bind vs posed pixel diff. This is NOT a
    // claim of visual correctness - only "did anything change at all",
    // reported as a number rather than eyeballed. A human still has to
    // look at both PNGs to judge whether the deformation looks like a
    // correctly bent limb.
    if (bindPixels.size() == posedPixels.size() && !bindPixels.empty()) {
        size_t pixelCount = bindPixels.size() / 4;
        size_t differing = 0;
        for (size_t i = 0; i + 3 < bindPixels.size(); i += 4) {
            if (bindPixels[i] != posedPixels[i] || bindPixels[i + 1] != posedPixels[i + 1] ||
                bindPixels[i + 2] != posedPixels[i + 2] || bindPixels[i + 3] != posedPixels[i + 3]) {
                ++differing;
            }
        }
        std::printf("pixel diff (bind vs posed): %zu / %zu pixels differ (%.2f%%)\n", differing,
                    pixelCount,
                    pixelCount ? 100.0 * static_cast<double>(differing) / static_cast<double>(pixelCount)
                               : 0.0);
    } else {
        std::printf("pixel diff (bind vs posed): SKIPPED (buffer size mismatch %zu vs %zu)\n",
                    bindPixels.size(), posedPixels.size());
    }

    return 0;
}
