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


// Milestone 3: render a real mesh from a shipped archive.
int runMesh(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: sr3_viewer mesh <archive.vpp_pc> <name.ccmesh_pc> <out.png>\n"
                     "                       [--mode uv|checker|normal|textured|material]\n"
                     "                       [--size W H]\n"
                     "                       [--yaw D] [--pitch D] [--channel N]\n");
        return 1;
    }
    const std::string archivePath = argv[2];
    const std::string cmeshName = argv[3];
    const std::string outPath = argv[4];
    if (outPath.rfind("--", 0) == 0) {
        std::fprintf(stderr,
                     "output path '%s' looks like a flag - it is POSITIONAL and must come "
                     "before any --options\n", outPath.c_str());
        return 2;
    }
    uint32_t width = 720, height = 900;
    float yaw = 0.0f, pitch = 0.1f;
    size_t channelIndex = 0;
    size_t drawGroup = 0;
    sr3render::MeshDrawMode mode = sr3render::MeshDrawMode::Checker;
    std::string rigName;

    for (int i = 5; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--size" && i + 2 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--yaw" && i + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && i + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++i])) * 3.14159265f / 180.0f;
        } else if (arg == "--channel" && i + 1 < argc) {
            channelIndex = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--rig" && i + 1 < argc) {
            rigName = argv[++i];
        } else if (arg == "--group" && i + 1 < argc) {
            drawGroup = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string m = argv[++i];
            if (m == "uv") mode = sr3render::MeshDrawMode::UvAsColour;
            else if (m == "checker") mode = sr3render::MeshDrawMode::Checker;
            else if (m == "normal") mode = sr3render::MeshDrawMode::NormalAsColour;
            else if (m == "textured") mode = sr3render::MeshDrawMode::Textured;
            else if (m == "material") mode = sr3render::MeshDrawMode::MaterialIdAsColour;
            else {
                std::fprintf(stderr,
                             "unknown --mode '%s' (uv, checker, normal, textured, material)\n",
                             m.c_str());
                return 2;
            }
        } else {
            // Refuse anything unrecognised instead of ignoring it. Silently
            // skipping an argument is how this tool rendered a CHECKER
            // image while being asked for material colours: the output path
            // is POSITIONAL, so `--mode material --out file.png` made
            // "--mode" the filename and left "material" as a stray token
            // that this loop quietly dropped. Exit code 0, wrong picture -
            // the same failure mode recorded in HANDOFF Sec3.
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
    std::vector<uint8_t> cb, gb;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, cmeshName, cb, gb, gmeshName)) {
            std::fprintf(stderr, "could not find '%s'\n", cmeshName.c_str());
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

    sr3geometry::MaterialBlock material =
        sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    sr3geometry::GeometryBlock geometry =
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material);
    if (!geometry.hasMeshSubBlock()) {
        std::fprintf(stderr, "no Mesh sub-block in this file\n");
        return 1;
    }
    std::printf("textures : ");
    for (const auto& n : material.textureNames) std::printf("%s ", n.c_str());
    std::printf("\n");

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
    const auto& channel = mesh.channels()[channelIndex];
    std::printf("channel  : layout %u, %u texcoord set(s), stride %zu, %u vertices%s\n",
                channel.layoutCode, channel.texcoordCount, channel.stride(), channel.elementCount,
                channel.strideMatchesLaw ? " (stride matches the spec law)" : " (STRIDE LAW MISMATCH)");

    // Topology is settled: triangle STRIP with degenerate stitching
    // (spec Sec8.1). No divisibility requirement - the ~38% of blocks that
    // fail it were the first hint these were never lists.

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
    if (!renderer.upload(device.device(), mesh, channelIndex, 0, error, drawGroup)) {
        std::fprintf(stderr, "mesh upload failed: %s\n", error.c_str());
        return 1;
    }
    if (!renderer.createDepth(device.device(), width, height, error)) {
        std::fprintf(stderr, "depth buffer failed: %s\n", error.c_str());
        return 1;
    }
    const float* mn = renderer.boundsMin();
    const float* mx = renderer.boundsMax();
    std::printf("bounds   : %.3f x %.3f x %.3f   (min Y %.3f)\n", mx[0] - mn[0], mx[1] - mn[1],
                mx[2] - mn[2], mn[1]);
    if (mesh.drawGroupsLocated()) {
        std::printf("groups   : %zu LOD group(s), ranges per group:", mesh.drawGroups().size());
        for (const auto& g : mesh.drawGroups()) std::printf(" %zu", g.size());
        std::printf("   (drawing group %zu, each range restarted)\n", drawGroup);
        if (drawGroup >= mesh.drawGroups().size()) {
            std::fprintf(stderr, "group %zu does not exist (%zu available)\n", drawGroup,
                         mesh.drawGroups().size());
            return 2;
        }
        // Index counts per group. If the groups really are LOD steps these
        // must fall away monotonically; printing them puts a number beside
        // the picture rather than relying on the eye alone.
        std::printf("lod      :");
        for (size_t gi = 0; gi < mesh.drawGroups().size(); ++gi) {
            uint32_t n = 0;
            for (const auto& r : mesh.drawGroups()[gi]) n += r.indexCount;
            std::printf(" g%zu=%u", gi, n);
        }
        std::printf("\n");
    } else {
        std::fprintf(stderr, "groups   : NOT located - falling back to one continuous strip, "
                             "which will produce bridge triangles\n");
    }
    std::printf("topology : strip of %u indices -> %u non-degenerate triangles (%.3f per vertex)\n",
                mesh.indexCount(), renderer.trianglesEmitted(),
                channel.elementCount
                    ? static_cast<double>(renderer.trianglesEmitted()) / channel.elementCount
                    : 0.0);

    // Count triangles with an edge wildly out of scale with the mesh. On a
    // correctly-decoded surface every edge is small relative to the object;
    // a long one means a triangle spanning unrelated parts of the model.
    // These are the visible "spikes" and they are NOT a decode error - see
    // the note printed below.
    {
        std::vector<sr3mesh::Vertex> verts = mesh.decodeChannel(channelIndex);
        std::vector<uint32_t> tris = mesh.drawGroupsLocated() ? mesh.triangleListForGroup(0)
                                                             : mesh.triangleListIndices();
        float diag = 0.0f;
        for (int c = 0; c < 3; ++c) {
            float d = mx[c] - mn[c];
            diag += d * d;
        }
        diag = std::sqrt(diag);
        const float threshold = diag * 0.20f;
        size_t outliers = 0;
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            const auto& a = verts[tris[t]].position;
            const auto& b = verts[tris[t + 1]].position;
            const auto& c = verts[tris[t + 2]].position;
            const std::array<const std::array<float, 3>*, 3> p = {&a, &b, &c};
            bool longEdge = false;
            for (int e = 0; e < 3 && !longEdge; ++e) {
                const auto& u = *p[static_cast<size_t>(e)];
                const auto& v = *p[static_cast<size_t>((e + 1) % 3)];
                float dx = u[0] - v[0], dy = u[1] - v[1], dz = u[2] - v[2];
                if (std::sqrt(dx * dx + dy * dy + dz * dz) > threshold) longEdge = true;
            }
            if (longEdge) ++outliers;
        }
        std::printf("bridges  : %zu triangle(s) with an edge > 20%% of the bbox diagonal, out of %u"
                    "  (%zu material texture(s))\n",
                    outliers, renderer.trianglesEmitted(), material.textureNames.size());
    }

    device.bindRenderTargetWithDepth(renderer.depthView());
    device.clear({28, 30, 38, 255});
    renderer.clearDepth(device.context());

    // Textured mode: resolve the material's first diffuse-looking texture
    // out of the archive. Preferring a "_d" name skips the normal maps,
    // which are real data but decode to olive (see HANDOFF.md Sec9.5) and
    // would make a correct render look broken.
    sr3render::UploadedTexture uploaded;
    std::vector<sr3render::UploadedTexture> materialTextures;
    std::vector<ID3D11ShaderResourceView*> perMaterialSrv;
    bool perMaterialTextured = false;

    if (mode == sr3render::MeshDrawMode::Textured) {
        // Real per-material binding (HANDOFF §9.22): each material names its
        // own textures by byte offset. Falls back to the old single-texture
        // path only when no binding exists - placeholder meshes genuinely
        // have none, and guessing one would bind a wrong-but-plausible
        // texture.
        sr3geometry::MaterialBindings bindings;
        bool haveBindings = false;
        try {
            bindings = sr3geometry::MaterialBindings::parse(
                vpp::ByteView(cb.data(), cb.size()), geometry.offset(),
                geometry.meshSubBlockOffset() + mesh.cLength());
            haveBindings = bindings.located();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "material binding: %s\n", ex.what());
        }

        if (haveBindings) {
            vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
            materialTextures.resize(bindings.materialCount());
            perMaterialSrv.assign(bindings.materialCount(), nullptr);
            std::printf("binding  : %u material(s)\n", bindings.materialCount());
            for (uint32_t m = 0; m < bindings.materialCount(); ++m) {
                const std::string* diffuse = bindings.diffuseFor(m);
                if (diffuse == nullptr) {
                    std::printf("    mat %u: (no slot-0 diffuse)\n", m);
                    continue;
                }
                std::string foundIn;
                if (findAndUploadTexture(container, *diffuse, device.device(),
                                         materialTextures[m], foundIn)) {
                    perMaterialSrv[m] = materialTextures[m].srv;
                    std::printf("    mat %u: %s  (%ux%u %s)\n", m, diffuse->c_str(),
                                materialTextures[m].width, materialTextures[m].height,
                                materialTextures[m].dxgiFormatName.c_str());
                } else {
                    // Drawn in its flat material colour, NOT in a
                    // neighbouring material's texture.
                    std::printf("    mat %u: %s  UNRESOLVED - flat colour\n", m,
                                diffuse->c_str());
                }
            }
            perMaterialTextured = true;
        } else {
            std::printf("binding  : none located (placeholder mesh) - single-texture fallback\n");
            std::string wanted;
            for (const auto& n : material.textureNames) {
                if (n.size() > 6 && n.find("_d.tga") != std::string::npos) { wanted = n; break; }
            }
            if (wanted.empty() && !material.textureNames.empty()) wanted = material.textureNames[0];
            if (!wanted.empty()) {
                vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
                std::string foundIn;
                if (findAndUploadTexture(container, wanted, device.device(), uploaded, foundIn)) {
                    std::printf("texture  : %s from %s (%ux%u, %s)\n", wanted.c_str(),
                                foundIn.c_str(), uploaded.width, uploaded.height,
                                uploaded.dxgiFormatName.c_str());
                } else {
                    std::fprintf(stderr,
                                 "could not resolve texture '%s' in this archive - falling back "
                                 "to checker so the geometry is still visible\n",
                                 wanted.c_str());
                    mode = sr3render::MeshDrawMode::Checker;
                }
            }
        }
    }

    float viewProjection[16];
    sr3render::buildOrbitViewProjection(mn, mx, yaw, pitch, 1.9f,
                                        static_cast<float>(width) / static_cast<float>(height),
                                        viewProjection);
    if (perMaterialTextured) {
        renderer.drawTextured(device.context(), viewProjection, perMaterialSrv);
    } else {
        renderer.draw(device.context(), viewProjection, mode, uploaded.srv);
    }

    // Optional skeleton overlay. Drawn from MODEL-SPACE rest positions with
    // no rotation applied - the rotation convention is OPEN, and positions
    // do not need it. If the skeleton lines up with the mesh, that confirms
    // the positions really are model-space (spec has it HIGH CONFIDENCE).
    if (!rigName.empty()) {
        std::vector<uint8_t> rb, unusedSibling;
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (findEntry(container, rigName, rb, unusedSibling, "")) {
            sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size()));
            std::vector<std::array<float, 3>> positions;
            std::vector<uint32_t> parents;
            for (const auto& bone : rig.bones()) {
                positions.push_back(bone.restPosition);
                parents.push_back(bone.parentIndex);
            }
            std::printf("rig      : %s, %zu bones, hash table %s\n", rigName.c_str(),
                        rig.bones().size(), rig.hashTableMatchesNames() ? "OK" : "MISMATCH");
            {
                float rmn[3] = {1e30f, 1e30f, 1e30f}, rmx[3] = {-1e30f, -1e30f, -1e30f};
                for (const auto& p : positions) {
                    for (int c = 0; c < 3; ++c) {
                        float v = p[static_cast<size_t>(c)];
                        if (v < rmn[c]) rmn[c] = v;
                        if (v > rmx[c]) rmx[c] = v;
                    }
                }
                std::printf("rig bbox : %.3f x %.3f x %.3f  X[%.2f %.2f] Y[%.2f %.2f] Z[%.2f %.2f]\n",
                            rmx[0] - rmn[0], rmx[1] - rmn[1], rmx[2] - rmn[2], rmn[0], rmx[0],
                            rmn[1], rmx[1], rmn[2], rmx[2]);
                std::printf("mesh bbox: %.3f x %.3f x %.3f  X[%.2f %.2f] Y[%.2f %.2f] Z[%.2f %.2f]\n",
                            mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], mn[0], mx[0], mn[1],
                            mx[1], mn[2], mx[2]);
            }
            if (renderer.uploadSkeleton(device.device(), positions, parents, error)) {
                renderer.drawSkeleton(device.context(), viewProjection);
            } else {
                std::fprintf(stderr, "skeleton upload failed: %s\n", error.c_str());
            }
        } else {
            std::fprintf(stderr, "could not find rig %s\n", rigName.c_str());
        }
    }

    std::vector<uint8_t> pixels;
    if (!device.readBack(pixels, error)) {
        std::fprintf(stderr, "readback failed: %s\n", error.c_str());
        return 1;
    }
    if (!sr3render::writePng(outPath, width, height, pixels, error)) {
        std::fprintf(stderr, "png write failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("wrote    : %s\n", outPath.c_str());
    return 0;
}



// TEMPORARY DIAGNOSTIC (brad-head matlib-mapping investigation, 2026-10-08):
// dumps a .ccmesh_pc's own CONFIRMED per-material binding
// (sr3geometry::MaterialBindings - slot/paramHash/name per material) next to
// a standalone .matlib_pc's own CONFIRMED MaterialBlock (slot count + name
// table) side by side, so the two files' own structures can be compared by
// hand. Read-only, prints to stdout only, not wired into any render path and
// not part of the default/golden-tested command set - this exists purely to
// answer "does the matlib's name list line up with the mesh's own slot/role
// structure", per this task's own data-only, bounded scope.
int runProbeMatlib(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: sr3_viewer probe-matlib <archive.vpp_pc> <mesh.ccmesh_pc> "
                     "<lib.matlib_pc>\n");
        return 1;
    }
    const std::string archivePath = argv[2];
    const std::string meshName = argv[3];
    const std::string matlibName = argv[4];

    std::vector<uint8_t> archive = readFile(archivePath);
    if (archive.empty()) {
        std::fprintf(stderr, "could not read %s\n", archivePath.c_str());
        return 1;
    }

    // --- .ccmesh_pc side: its own embedded MaterialBlock + MaterialBindings ---
    std::string gmeshName = meshName;
    size_t dot = gmeshName.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < gmeshName.size()) gmeshName[dot + 1] = 'g';
    std::vector<uint8_t> cb, gb;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, meshName, cb, gb, gmeshName)) {
            std::fprintf(stderr, "could not find '%s'\n", meshName.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    std::printf("=== %s (%zu bytes) ===\n", meshName.c_str(), cb.size());
    sr3geometry::MaterialBlock meshMaterial =
        sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size()));
    std::printf("MaterialBlock: slotCount=%u totalSize=%zu names:", meshMaterial.textureSlotCount,
                meshMaterial.totalSize);
    for (const auto& n : meshMaterial.textureNames) std::printf(" [%s]", n.c_str());
    std::printf("\n");

    sr3geometry::GeometryBlock geometry =
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), meshMaterial);
    if (!geometry.hasMeshSubBlock()) {
        std::fprintf(stderr, "no Mesh sub-block\n");
    } else {
        sr3mesh::MeshBlock mesh =
            sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()), geometry.meshSubBlockOffset(),
                                      vpp::ByteView(gb.data(), gb.size()));
        sr3geometry::MaterialBindings bindings;
        bool haveBindings = false;
        try {
            bindings = sr3geometry::MaterialBindings::parse(
                vpp::ByteView(cb.data(), cb.size()), geometry.offset(),
                geometry.meshSubBlockOffset() + mesh.cLength());
            haveBindings = bindings.located();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "material binding: %s\n", ex.what());
        }
        std::printf("MaterialBindings: materialCount=%u located=%s\n", bindings.materialCount(),
                    haveBindings ? "true" : "false");
        if (haveBindings) {
            for (uint32_t m = 0; m < bindings.materialCount(); ++m) {
                const auto& mat = bindings.materials()[m];
                std::printf("  material %u: %zu texture entr%s\n", m, mat.textures.size(),
                            mat.textures.size() == 1 ? "y" : "ies");
                for (const auto& t : mat.textures) {
                    std::printf("    slot=%u paramHash=0x%08X name=%s\n", t.slot, t.paramHash,
                                t.name.c_str());
                }
            }
        }
    }

    // --- .matlib_pc side: its own CONFIRMED MaterialBlock (header + name
    // table) only - the footer past it is NOT decoded anywhere in this
    // project's spec documentation, so it is reported as a byte count here,
    // never interpreted. ---
    std::vector<uint8_t> lb, unusedSibling;
    try {
        vpp::Container container(vpp::ByteView(archive.data(), archive.size()));
        if (!findEntry(container, matlibName, lb, unusedSibling, "")) {
            std::fprintf(stderr, "could not find '%s'\n", matlibName.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "archive error: %s\n", ex.what());
        return 1;
    }
    std::printf("=== %s (%zu bytes) ===\n", matlibName.c_str(), lb.size());
    sr3geometry::MaterialBlock lib = sr3geometry::MaterialBlock::parse(vpp::ByteView(lb.data(), lb.size()));
    std::printf("MaterialBlock: slotCount=%u totalSize=%zu names:", lib.textureSlotCount, lib.totalSize);
    for (const auto& n : lib.textureNames) std::printf(" [%s]", n.c_str());
    std::printf("\n");
    if (lib.totalSize < lb.size()) {
        std::printf("footer: %zu bytes past the MaterialBlock (UNDECODED per spec-texture-format.md "
                    "Appendix / spec-geometry-format.md Sec3.2 - not interpreted by this probe)\n",
                    lb.size() - lib.totalSize);
    }
    return 0;
}
