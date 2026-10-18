// runWalkDemo() split: load brad's rigged mesh, rig and bind-pose skin.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupBrad(WalkSession& sess) {
    auto& charactersPath = sess.charactersPath;
    auto& charArchiveBytes = sess.charArchiveBytes;
    auto& cb = sess.cb;
    auto& gb = sess.gb;
    auto& rb = sess.rb;
    auto& unusedSibling = sess.unusedSibling;
    auto& headCb = sess.headCb;
    auto& headGb = sess.headGb;
    auto& channelIndex = sess.channelIndex;
    auto& drawGroup = sess.drawGroup;
    auto& useBonePalette = sess.useBonePalette;
    auto& parentIdx = sess.parentIdx;
    auto& restMesh = sess.restMesh;
    auto& restRig = sess.restRig;
    auto& bindSkin = sess.bindSkin;
    auto& verts = sess.verts;
    auto& vertexDuplication = sess.vertexDuplication;
    auto& bindStats = sess.bindStats;
    auto& bindPositions = sess.bindPositions;
    // ---- brad (rigged character), loaded exactly like runPose() does, bind
    // pose ONLY (Judgement call 2 above - no real per-clip bone rotation
    // data exists anywhere in this project). ----------------------------------
    charArchiveBytes = readFileLargeC(charactersPath);
    if (charArchiveBytes.empty()) {
        std::fprintf(stderr, "could not read %s\n", charactersPath.c_str());
        return 1;
    }
    // HEAD FIX (walk_demo v3): brad.ccmesh_pc/gcmesh_pc is the BODY only -
    // §9.19/§9.20's own material-colour render (HANDOFF.md) already shows
    // its 5 materials land on jacket+arms/trousers/gloves/shoes/hair, never
    // skin - there is no face material in that file at all, which is why
    // v2 renders headless. The real head is a SEPARATE top-level entry in
    // this SAME characters.vpp_pc archive (confirmed directly this
    // session, not guessed): `brad_head_lod.ccmesh_pc`/`.gcmesh_pc`, the
    // same mesh HANDOFF.md §9.9/§9.21 already decoded standalone and found
    // to render "a flawless head with eyes, nose, brow and jaw" - not a
    // customization/morph asset (spec-morph-format.md's `cm_body`/
    // `cm_pc_fac` family is the player-creator's OWN separate system, keyed
    // off `customize_item.vpp_pc`/`player_morph.vpp_pc`, never touched
    // here). Re-probed this session with the existing `mesh` command
    // (reusing the exact loader, not a new one): 311 vertices, channel 0
    // layout code 3 (the SAME skinned layout brad's own body channel 0
    // uses), 1 LOD group/1 range, mesh-space Y range [1.52, 1.82] -
    // directly overlapping the top of the body's own Y range
    // ([-0.01, 1.72], measured the same way) at the neck/collar, with NO
    // transform applied by either probe. Same archive, same rig
    // (`brad.rig_pc`, 68 bones, hash table OK against this mesh's own
    // palette), same shared mesh-space origin - so it is loaded and skinned
    // here exactly like the body is, a few lines down, through the SAME
    // resolveSkinningMode()/remapBlendIndicesThroughPalette()/
    // skinVertices() free functions, not a new path.
    try {
        vpp::Container charContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
        if (!findEntry(charContainer, "brad.ccmesh_pc", cb, gb, "brad.gcmesh_pc")) {
            std::fprintf(stderr, "could not find brad.ccmesh_pc in %s\n", charactersPath.c_str());
            return 1;
        }
        if (!findEntry(charContainer, "brad.rig_pc", rb, unusedSibling, "")) {
            std::fprintf(stderr, "could not find brad.rig_pc in %s\n", charactersPath.c_str());
            return 1;
        }
        if (!findEntry(charContainer, "brad_head_lod.ccmesh_pc", headCb, headGb, "brad_head_lod.gcmesh_pc")) {
            std::fprintf(stderr, "could not find brad_head_lod.ccmesh_pc in %s\n", charactersPath.c_str());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "characters archive error: %s\n", ex.what());
        return 1;
    }
    if (gb.empty()) {
        std::fprintf(stderr, "found brad.ccmesh_pc but not brad.gcmesh_pc\n");
        return 1;
    }
    if (headGb.empty()) {
        std::fprintf(stderr, "found brad_head_lod.ccmesh_pc but not brad_head_lod.gcmesh_pc\n");
        return 1;
    }

    sess.material_.emplace(sr3geometry::MaterialBlock::parse(vpp::ByteView(cb.data(), cb.size())));
    auto& material = *sess.material_;
    sess.geometry_.emplace(
        sr3geometry::GeometryBlock::parse(vpp::ByteView(cb.data(), cb.size()), material));
    auto& geometry = *sess.geometry_;
    if (!geometry.hasMeshSubBlock()) {
        std::fprintf(stderr, "brad.ccmesh_pc has no Mesh sub-block\n");
        return 1;
    }
    sess.mesh_.emplace(sr3mesh::MeshBlock::parse(vpp::ByteView(cb.data(), cb.size()),
                                                        geometry.meshSubBlockOffset(), vpp::ByteView(gb.data(), gb.size())));
    auto& mesh = *sess.mesh_;
    // channelIndex = 0, drawGroup = 0 (WalkSession member initialisers) - brad's own golden (tests/golden/brad) uses these defaults
    if (channelIndex >= mesh.channels().size()) {
        std::fprintf(stderr, "brad: no channel 0\n");
        return 1;
    }
    if (!sr3mesh::layoutInfoFor(mesh.channels()[channelIndex].layoutCode).hasSkinning) {
        std::fprintf(stderr, "brad channel 0 carries no blend weights/indices\n");
        return 1;
    }
    sess.rig_.emplace(sr3rig::Rig::parse(vpp::ByteView(rb.data(), rb.size())));
    auto& rig = *sess.rig_;
    const auto& bones = rig.bones();
    std::printf("character: brad, %zu bones (channel 0, group 0)\n", bones.size());

    // resolveSkinningMode()/applyMultiSetSkinning() are the SAME free
    // functions runPose() already calls - reused verbatim, not duplicated,
    // in case brad ever turns out multi-set (not expected - brad's own
    // golden uses the plain single-set default).
    sess.skinModeResult_.emplace(resolveSkinningMode(mesh, /*legacyForced=*/false));
    const auto& skinModeResult = *sess.skinModeResult_;
    if (isSkinningRefusal(skinModeResult.mode)) {
        std::fprintf(stderr, "%s\n", skinModeResult.refusalMessage.c_str());
        return 1;
    }
    useBonePalette = skinModeResult.mode == SkinningMode::kPaletteSingleSet ||
                                skinModeResult.mode == SkinningMode::kPaletteMultiSet;

    parentIdx.resize(bones.size());
    restMesh.resize(bones.size());
    // FIX 4 (--v2 only): RAW rig-space rest positions, kept alongside
    // restMesh - sr3rig/animated_pose.h's own documented requirement for
    // real per-bone rotation ("do the ENTIRE pose computation in rig
    // space... convert only the FINAL per-bone Skin_i matrix to mesh space
    // ONCE"), the SAME pattern the `pose`/`animpose` command's own real
    // clip-driven render already uses (restRig there).
    restRig.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        parentIdx[i] = bones[i].parentIndex;
        restMesh[i] = useBonePalette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                     : sr3rig::rigToMeshSpace(bones[i].restPosition);
        restRig[i] = bones[i].restPosition;
    }
    bindSkin = sr3rig::computeSkinningMatrices(parentIdx, restMesh, nullptr);
    verts = mesh.decodeChannel(channelIndex);
    if (verts.empty()) {
        std::fprintf(stderr, "brad channel 0 produced no vertices\n");
        return 1;
    }
    if (skinModeResult.mode == SkinningMode::kPaletteMultiSet) {
        sr3rig::BlendIndexRemapStats remap;
        sr3rig::VertexDuplicationResult dup = applyMultiSetSkinning(mesh, verts, bones.size(), drawGroup, remap);
        if (!dup.usable) {
            std::fprintf(stderr, "%s\n", dup.problem.c_str());
            return 1;
        }
        if (dup.duplicatesCreated != 0) {
            vertexDuplication.sourceIndex = dup.duplicateSourceIndex;
            vertexDuplication.redirectPerRange = dup.redirectPerRange;
        }
    } else if (useBonePalette) {
        sr3rig::BlendIndexRemapStats remap;
        verts = sr3rig::remapBlendIndicesThroughPalette(verts, mesh.bonePalette(), bones.size(), &remap);
    }
    bindPositions = sr3rig::skinVertices(verts, bindSkin, &bindStats);
    return -1;
}

}  // namespace sr3viewer
