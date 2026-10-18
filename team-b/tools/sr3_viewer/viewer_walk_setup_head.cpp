// runWalkDemo() split: brad's head mesh, matlib attach/bind and head textures.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupCharacterHead(WalkSession& sess) {
    auto& v2 = sess.v2;
    auto& headMatlibAttach = sess.headMatlibAttach;
    auto& headMatlibBind = sess.headMatlibBind;
    auto& charArchiveBytes = sess.charArchiveBytes;
    auto& headCb = sess.headCb;
    auto& headGb = sess.headGb;
    auto& parentIdx = sess.parentIdx;
    auto& error = sess.error;
    auto& characterHeadMaterialTextures = sess.characterHeadMaterialTextures;
    auto& characterHeadPerMaterialSrv = sess.characterHeadPerMaterialSrv;
    auto& characterHeadTextured = sess.characterHeadTextured;
    auto& haveHeadMesh = sess.haveHeadMesh;
    auto& device = *sess.device_;
    auto& characterHeadRenderer = *sess.characterHeadRenderer_;
    const auto& bones = sess.rig_->bones();
    // HEAD FIX - everything about the head is gated behind `v2`, matching
    // this task's own scope rule that walk_demo v1 (no --v2) must stay
    // byte-for-byte unchanged: no parse, no skin, no renderer upload, no
    // draw call and no stdout line runs unless v2 is set. See the long
    // comment above the `brad_head_lod.ccmesh_pc` findEntry() call (this
    // function, above) for the full root-cause citation: a SEPARATE
    // top-level entry in the SAME characters.vpp_pc archive brad.ccmesh_pc
    // already comes from, not a customization/morph asset - 311 vertices,
    // channel 0 layout code 3 (same skinned layout as the body), sharing
    // brad.rig_pc (already loaded above for the body - no second rig_pc
    // exists for this mesh). Parsed/skinned EXACTLY like the body block
    // above (same free functions: resolveSkinningMode(),
    // remapBlendIndicesThroughPalette(), skinVertices()), against its own
    // independently-resolved bone palette and skinning mode - not assumed
    // equal to the body's, though in practice it measures the same
    // (kPaletteSingleSet). Textured exactly like FIX 1 just above
    // (MaterialBindings::parse() + findAndUploadTexture()).
    if (v2) {
        sr3geometry::MaterialBlock headMaterial =
            sr3geometry::MaterialBlock::parse(vpp::ByteView(headCb.data(), headCb.size()));
        sr3geometry::GeometryBlock headGeometry =
            sr3geometry::GeometryBlock::parse(vpp::ByteView(headCb.data(), headCb.size()), headMaterial);
        if (!headGeometry.hasMeshSubBlock()) {
            std::fprintf(stderr, "brad_head_lod.ccmesh_pc has no Mesh sub-block\n");
            return 1;
        }
        sr3mesh::MeshBlock headMesh = sr3mesh::MeshBlock::parse(vpp::ByteView(headCb.data(), headCb.size()),
                                                                headGeometry.meshSubBlockOffset(),
                                                                vpp::ByteView(headGb.data(), headGb.size()));
        const size_t headChannelIndex = 0, headDrawGroup = 0; // §9.21: 1 group/1 range anyway
        if (headChannelIndex >= headMesh.channels().size()) {
            std::fprintf(stderr, "brad_head_lod: no channel 0\n");
            return 1;
        }
        if (!sr3mesh::layoutInfoFor(headMesh.channels()[headChannelIndex].layoutCode).hasSkinning) {
            std::fprintf(stderr, "brad_head_lod channel 0 carries no blend weights/indices\n");
            return 1;
        }
        std::printf("character: brad_head_lod, %zu bones (shared rig, channel 0, group 0)\n", bones.size());

        const SkinningModeResult headSkinModeResult = resolveSkinningMode(headMesh, /*legacyForced=*/false);
        if (isSkinningRefusal(headSkinModeResult.mode)) {
            std::fprintf(stderr, "%s\n", headSkinModeResult.refusalMessage.c_str());
            return 1;
        }
        const bool headUseBonePalette = headSkinModeResult.mode == SkinningMode::kPaletteSingleSet ||
                                        headSkinModeResult.mode == SkinningMode::kPaletteMultiSet;

        // Recomputed (not reused from the body's own bindSkin) because it
        // is keyed off headUseBonePalette's rigToMeshSpace[Inverted]
        // choice, a property of THIS mesh's own skinning-mode resolution -
        // not assumed equal to the body's even though it measures the same
        // in practice. Cost is trivial (bones.size() ~ 68 matrices).
        std::vector<std::array<float, 3>> headRestMesh(bones.size());
        for (size_t i = 0; i < bones.size(); ++i) {
            headRestMesh[i] = headUseBonePalette ? sr3rig::rigToMeshSpaceInverted(bones[i].restPosition)
                                                 : sr3rig::rigToMeshSpace(bones[i].restPosition);
        }
        std::vector<sr3rig::Mat3x4> headBindSkin = sr3rig::computeSkinningMatrices(parentIdx, headRestMesh, nullptr);
        std::vector<sr3mesh::Vertex> headVerts = headMesh.decodeChannel(headChannelIndex);
        if (headVerts.empty()) {
            std::fprintf(stderr, "brad_head_lod channel 0 produced no vertices\n");
            return 1;
        }
        sr3render::VertexDuplication headVertexDuplication;
        if (headSkinModeResult.mode == SkinningMode::kPaletteMultiSet) {
            sr3rig::BlendIndexRemapStats headRemap;
            sr3rig::VertexDuplicationResult headDup =
                applyMultiSetSkinning(headMesh, headVerts, bones.size(), headDrawGroup, headRemap);
            if (!headDup.usable) {
                std::fprintf(stderr, "%s\n", headDup.problem.c_str());
                return 1;
            }
            if (headDup.duplicatesCreated != 0) {
                headVertexDuplication.sourceIndex = headDup.duplicateSourceIndex;
                headVertexDuplication.redirectPerRange = headDup.redirectPerRange;
            }
        } else if (headUseBonePalette) {
            sr3rig::BlendIndexRemapStats headRemap;
            headVerts =
                sr3rig::remapBlendIndicesThroughPalette(headVerts, headMesh.bonePalette(), bones.size(), &headRemap);
        }
        sr3rig::SkinningStats headBindStats;
        std::vector<std::array<float, 3>> headBindPositions =
            sr3rig::skinVertices(headVerts, headBindSkin, &headBindStats);

        if (!characterHeadRenderer.initialise(device.device(), error)) {
            std::fprintf(stderr, "character head renderer init failed: %s\n", error.c_str());
            return 1;
        }
        const sr3render::VertexDuplication* headDupPtr =
            headVertexDuplication.sourceIndex.empty() ? nullptr : &headVertexDuplication;
        if (!characterHeadRenderer.upload(device.device(), headMesh, headChannelIndex, 0, error, headDrawGroup,
                                          &headBindPositions, headDupPtr)) {
            std::fprintf(stderr, "character head upload failed: %s\n", error.c_str());
            return 1;
        }
        haveHeadMesh = true;

        // The head's own real material binding. Probed directly this
        // session: brad_head_lod.ccmesh_pc has exactly 1 material, whose
        // slot-0 diffuse is literally named "missing.tga" in the shipped
        // data (not a bug in this reader - the probe's own `mesh` command
        // reports the identical name against the real archive) and does
        // not resolve in this archive, so it renders flat-coloured via the
        // SAME "UNRESOLVED - flat colour" fallback the body's own loop
        // already uses for any unresolved material - not a new behaviour,
        // not a stand-in texture invented for this task. Shipping this LOD
        // with a placeholder diffuse (rather than no diffuse at all) is
        // consistent with it being a distance LOD where facial detail
        // would not be legible anyway (HANDOFF.md §9.9/§9.21's own
        // "1 group/1 range" LOD framing for this exact file).
        sr3geometry::MaterialBindings headBindings;
        bool haveHeadBindings = false;
        try {
            headBindings = sr3geometry::MaterialBindings::parse(
                vpp::ByteView(headCb.data(), headCb.size()), headGeometry.offset(),
                headGeometry.meshSubBlockOffset() + headMesh.cLength());
            haveHeadBindings = headBindings.located();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "brad_head_lod material binding: %s\n", ex.what());
        }
        // --head-matlib-attach (2026-10-08, spec-tables-customization.md
        // Sec12.1a/12.1b follow-up, supersedes the prior
        // --experimental-head-matlib heuristic below this comment's history):
        // brad_head_lod.ccmesh_pc's own per-material record above is REAL,
        // not a reader gap - probed directly in an earlier session, it has 5
        // texture-binding entries, not 1. Two of the five use parameter
        // hashes ALREADY CONFIRMED, population-wide (not specific to this
        // mesh), to mean a fixed role regardless of which character they
        // appear on - material_binding.h's own kParamHashDiffuseB
        // (0x69B48F91) and kParamHashNormalMap (0x2808EB90): slot 0
        // (0x69B48F91, this mesh's diffuse role) is bound in the shipped
        // data to the generic placeholder "missing.tga"; slot 1
        // (0x2808EB90, normal-map role) to the generic placeholder
        // "flat-normalmap_n.tga". The other three entries (slots 2/3/4) use
        // THREE parameter hashes (0xF02C95C4, 0x6925C47E, 0x8D91AC6E) that
        // do not appear anywhere else in this project's confirmed data -
        // not brad's own body's kParamHashSurfaceMaterial (0xDFE71DA8), not
        // cataloged under any other name - all three currently resolving to
        // the same generic placeholder "shd_whiteopaque.tga".
        //
        // spec-tables-customization.md Sec12.1b now traces the REAL matlib-
        // consumption mechanism end to end, from the executable, and
        // REFUTES the prior reading of the problem above: the matlib is NOT
        // a source of per-texture-slot replacements for this mesh's own
        // material record. It is resolved (filename compose -> exists-
        // check) and ATTACHED as its own independent, name-keyed cache
        // entry under the identity's own Name, type-tagged 0x16 (confirmed
        // at all 4 real call sites) - the SAME per-resource-TYPE attachment
        // pattern used for mesh (type 5/7), texture peg (type 0x10) and
        // morph (type 7). Nowhere in the traced chain does code reach into
        // an already-loaded mesh's material table and patch individual
        // slots - there is no "swap this one texture name" step in the real
        // engine at all. This supersedes the prior
        // `--experimental-head-matlib` flag's hand-derived texture-NAME-swap
        // heuristic (brad_sm_head_d.tga keyed off the SAME 0x69B48F91 hash
        // above), which assumed exactly the per-slot-splice model the spec
        // has since refuted.
        //
        // sr3geometry::ResolveCharacterMatlibAttachment() (include/
        // sr3geometry/character_matlib.h) models the RESOLUTION/ATTACHMENT
        // step only: compose "brad_head" + the platform extension ->
        // "brad_head.matlib_pc", check it exists in this same
        // characters.vpp_pc archive, and if so parse+attach it (via the
        // already-CONFIRMED general-purpose MaterialBlock reader,
        // spec-geometry-format.md Sec3.1 - not a new parser) as brad's own
        // type-0x16 resource. --head-matlib-attach (alone) stops there: it
        // logs the attachment (found, its type tag, its own texture list)
        // and binds NOTHING, exactly as before.
        //
        // --head-matlib-bind (2026-10-08, spec-tables-customization.md
        // Sec12.1f real-bind follow-up): Sec12.1f CLOSES the GPU skin-
        // composite pipeline as scoped to a 2-slot PLAYER co-op record pool,
        // never bound to any character mesh, and confirms NPCs instead use
        // individually-named, pre-baked, shipped per-part textures named
        // directly by the character's own matlib "via the same generic
        // by-name mechanism Sec12.1c already confirmed." BUT - checked
        // explicitly before writing this, per the orchestrator's own review
        // note on this task - neither Sec12.1c nor Sec12.1f states a
        // CONFIRMED rule for which matlib entry replaces which mesh slot.
        // Sec12.1c traces the matlib's own constructor (0x0074d9a0) in
        // full and finds its texture-name table (what MaterialBlock below
        // parses) is "explicitly skipped and never retained" by the real
        // engine at runtime - no traced instruction matches an entry to a
        // mesh slot by name, hash, or position; the render-time consumer
        // past 0x00dd35e0/0x00db3c80->0x00db3b40 was searched for and
        // not found (Sec12.1c), and Sec12.1f's own Q2 closes "is there a
        // bind instruction" as a confirmed NEGATIVE (the composite material
        // is scoped to one internal draw call, never attached to any
        // character/mesh/render-entity).
        //
        // So this flag does NOT re-derive a traced engine mechanism. It
        // applies sr3geometry::PickMatlibTextureByRoleSuffix() - a CHOSEN,
        // clearly-labelled-as-such heuristic (full citation in that
        // function's own doc comment, character_matlib.h) built ONLY from
        // facts this project has already separately confirmed: (1)
        // material_binding.h's own measured `_d`/`_dp` (diffuse) / `_n`
        // (normal) filename-suffix convention, population-wide; (2) a
        // required substring ("head") to disambiguate brad_head.matlib_pc's
        // own real 6 entries - confirmed directly against the real archive
        // (HANDOFF.md 2026-10-08): brad_sm_head_d.tga, brad_sm_head_n.tga,
        // skin_ca_02_sb.tga, shd_whiteopaque.tga, eye_reflect_01_sb.tga,
        // f_eyelashes_d.tga - both brad_sm_head_d.tga AND f_eyelashes_d.tga
        // end in "_d", so suffix alone picks the wrong one half the time
        // without the "head" substring requirement.
        //
        // It then REPLACES (headBindings.materialsMutable(), material_
        // binding.h) the mesh's OWN placeholder TextureRef.name for whichever
        // slot(s) material_binding.h's EXISTING by-param-hash role
        // identification (kParamHashDiffuseA/B, kParamHashNormalMap) already
        // flags as diffuse/normal-map - extending the existing dispatch
        // (diffuseFor()/the findAndUploadTexture loop below run completely
        // unchanged after this, same as every other mesh), not a parallel
        // one-off splice mechanism. OFF BY DEFAULT; CHANGES rendered output
        // (the head's diffuse texture) only when passed; no existing golden
        // baseline passes this flag, so none is affected; deliberately NOT
        // promoted to default and NOT frozen as a new golden here - a CHOSEN
        // heuristic is not confirmed ground truth, so freezing one is this
        // flag's owning session's call to make after visual sign-off and/or
        // a Team-A spec-sync answer on the real rule (if one exists at all,
        // given Sec12.1f Q2's own negative).
        sr3geometry::CharacterMatlibAttachment headMatlibAttachment;
        if (headMatlibAttach) {
            vpp::Container matlibContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
            auto loadByName = [&](const std::string& composed, std::vector<uint8_t>& out) {
                std::vector<uint8_t> unusedSibling;
                return findEntry(matlibContainer, composed, out, unusedSibling, "");
            };
            try {
                headMatlibAttachment =
                    sr3geometry::ResolveCharacterMatlibAttachment("brad", "brad_head", loadByName);
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "brad_head.matlib_pc attachment: %s\n", ex.what());
            }
            if (headMatlibAttachment.attached()) {
                std::printf(
                    "character: --head-matlib-attach: identity=%s type=0x%x (matlib) file=%s - "
                    "ATTACHED, %u texture name(s)%s\n",
                    headMatlibAttachment.identityName.c_str(),
                    static_cast<unsigned>(headMatlibAttachment.type),
                    headMatlibAttachment.composedFileName.c_str(),
                    headMatlibAttachment.resource.textureSlotCount,
                    headMatlibBind
                        ? " (--head-matlib-bind: binding below uses a CHOSEN role-suffix heuristic, "
                          "NOT a spec-confirmed slot rule - see this flag's own doc comment)"
                        : " (resolution/attachment only - render-time binding stays OPEN, not "
                          "applied; pass --head-matlib-bind for the CHOSEN-heuristic real bind)");
            } else {
                std::printf(
                    "character: --head-matlib-attach: identity=%s file=%s - NOT attached (not "
                    "found, or not MaterialBlock-shaped)\n",
                    headMatlibAttachment.identityName.c_str(),
                    headMatlibAttachment.composedFileName.c_str());
            }
        }

        // --head-matlib-bind: REPLACE the head mesh's own placeholder
        // diffuse/normal-map TextureRef names with the matlib's resolved
        // real names, using a CHOSEN (not spec-confirmed) role-suffix
        // heuristic - see the long doc comment above this block and
        // sr3geometry::PickMatlibTextureByRoleSuffix()'s own doc comment
        // (character_matlib.h) for the full citation. Mutates
        // headBindings.materialsMutable() in place so the EXISTING
        // diffuseFor()/findAndUploadTexture dispatch just below (unchanged)
        // naturally picks up the replaced name - no parallel bind path.
        if (headMatlibBind && headMatlibAttachment.attached() && haveHeadBindings) {
            const std::string partHint = "head"; // derived from the matlib's own base name "brad_head"
            const std::string* realDiffuse = sr3geometry::PickMatlibTextureByRoleSuffix(
                headMatlibAttachment.resource.textureNames, partHint, {"_d", "_dp"});
            const std::string* realNormal = sr3geometry::PickMatlibTextureByRoleSuffix(
                headMatlibAttachment.resource.textureNames, partHint, {"_n"});
            int replacedDiffuse = 0, replacedNormal = 0;
            for (sr3geometry::MaterialBinding& mb : headBindings.materialsMutable()) {
                for (sr3geometry::TextureRef& tref : mb.textures) {
                    if (realDiffuse != nullptr &&
                        (tref.paramHash == sr3geometry::kParamHashDiffuseA ||
                         tref.paramHash == sr3geometry::kParamHashDiffuseB)) {
                        tref.name = *realDiffuse;
                        ++replacedDiffuse;
                    } else if (realNormal != nullptr && tref.paramHash == sr3geometry::kParamHashNormalMap) {
                        tref.name = *realNormal;
                        ++replacedNormal;
                    }
                }
            }
            std::printf(
                "character: --head-matlib-bind: CHOSEN role-suffix heuristic (NOT spec-confirmed - "
                "spec-tables-customization.md Sec12.1c/Sec12.1f) replaced %d diffuse-role / %d "
                "normal-role TextureRef name(s) with the matlib's own resolved name(s)\n",
                replacedDiffuse, replacedNormal);
        }

        if (haveHeadBindings) {
            vpp::Container headTexContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
            characterHeadMaterialTextures.resize(headBindings.materialCount());
            characterHeadPerMaterialSrv.assign(headBindings.materialCount(), nullptr);
            std::printf("character: head %u real material(s), resolving real diffuse textures\n",
                        headBindings.materialCount());
            for (uint32_t m = 0; m < headBindings.materialCount(); ++m) {
                const std::string* diffuse = headBindings.diffuseFor(m);
                if (diffuse == nullptr) {
                    std::printf("    head mat %u: (no slot-0 diffuse)\n", m);
                    continue;
                }
                // Sec12.1b: the matlib is a separate, identity-keyed
                // resource attachment, not a per-slot splice into this
                // mesh's own material record - so with --head-matlib-bind
                // OFF (or absent), this loop binds the mesh's OWN real
                // per-material diffuse completely unchanged, identically to
                // every build before this task. With --head-matlib-bind ON,
                // `diffuse` here is whatever headBindings.diffuseFor(m)
                // returns AFTER the CHOSEN-heuristic replacement block above
                // already overwrote the matching TextureRef(s) in place -
                // this loop itself is untouched, dispatching exactly as it
                // always has onto whatever name is currently in the parsed
                // binding (the "extend the existing dispatch" requirement).
                std::string foundIn;
                if (findAndUploadTexture(headTexContainer, *diffuse, device.device(),
                                         characterHeadMaterialTextures[m], foundIn)) {
                    characterHeadPerMaterialSrv[m] = characterHeadMaterialTextures[m].srv;
                    std::printf("    head mat %u: %s  (%ux%u %s)\n", m, diffuse->c_str(),
                                characterHeadMaterialTextures[m].width, characterHeadMaterialTextures[m].height,
                                characterHeadMaterialTextures[m].dxgiFormatName.c_str());
                } else {
                    std::printf("    head mat %u: %s  UNRESOLVED - flat colour\n", m, diffuse->c_str());
                }
            }
            characterHeadTextured = true;
        } else {
            std::printf("character: head - no real material binding located - staying Checker\n");
        }
    }
    return -1;
}

}  // namespace sr3viewer
