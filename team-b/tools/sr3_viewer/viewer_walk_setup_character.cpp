// runWalkDemo() split: brad's v2 per-material body textures (and the head renderer declaration).
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupCharacterTextures(WalkSession& sess) {
    auto& v2 = sess.v2;
    auto& charArchiveBytes = sess.charArchiveBytes;
    auto& cb = sess.cb;
    auto& characterMaterialTextures = sess.characterMaterialTextures;
    auto& characterPerMaterialSrv = sess.characterPerMaterialSrv;
    auto& characterTextured = sess.characterTextured;
    auto& geometry = *sess.geometry_;
    auto& mesh = *sess.mesh_;
    auto& device = *sess.device_;
    // HEAD FIX: brad's head renderer - a SECOND MeshRenderer, same "upload
    // once, bind pose, share focusBox's depth, never call createDepth()"
    // pattern as characterRenderer just above, rendered as its own
    // separate draw call every frame (same per-frame charWorld as the
    // body - the two pieces are one rigid character, see Judgement call 2
    // above: no independent head motion is invented, same as no fake leg
    // motion is). Declared here at function scope (needed by the per-frame
    // draw loop far below) but only ever initialise()'d/upload()'d inside
    // the `if (v2)` block below, alongside the mesh/skinning work it needs
    // - v1 (no --v2) must stay BYTE-FOR-BYTE unchanged (this task's own
    // scope rule), so nothing about the head - no parse, no skin, no
    // upload, no draw call, no stdout line - may run outside v2. haveHead
    // (declared with the other v2-only character-render flags below) gates
    // the draw call; an un-upload()'d renderer is never drawn.
    sess.characterHeadRenderer_.emplace();

    // FIX 1 (--v2 only, see runWalkDemo()'s own "V2" doc comment): brad's
    // real per-material diffuse textures, resolved through the SAME
    // sr3geometry::MaterialBindings::parse() + findAndUploadTexture() path
    // runMesh()'s own --mode textured implementation already uses (copied,
    // not re-derived) - against brad's OWN cb/geometry/mesh, already loaded
    // above exactly like runPose() loads them. Kept at function scope (not
    // the v2 block below) because drawTextured() needs perMaterialSrv alive
    // for every frame's draw call, the same "caller owns; Release() when
    // done" lifetime runMesh() itself relies on for materialTextures.
    if (v2) {
        sr3geometry::MaterialBindings bindings;
        bool haveBindings = false;
        try {
            bindings = sr3geometry::MaterialBindings::parse(
                vpp::ByteView(cb.data(), cb.size()), geometry.offset(),
                geometry.meshSubBlockOffset() + mesh.cLength());
            haveBindings = bindings.located();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "brad material binding: %s\n", ex.what());
        }
        if (haveBindings) {
            vpp::Container texContainer(vpp::ByteView(charArchiveBytes.data(), charArchiveBytes.size()));
            characterMaterialTextures.resize(bindings.materialCount());
            characterPerMaterialSrv.assign(bindings.materialCount(), nullptr);
            // NOT charactersPath.c_str() here - an argv-supplied archive
            // path's separator style (backslash/forward-slash) is caller-
            // chosen and varies between a manually-typed invocation and a
            // CMake-registered ctest one (CMAKE_SOURCE_DIR-derived paths use
            // forward slashes) - echoing it into stdout would make an
            // otherwise byte-identical capture a false mismatch against a
            // baseline frozen from a differently-styled invocation of the
            // SAME archive (confirmed directly: this exact mismatch, nothing
            // else, is what the ctest-registered golden_scene_check target
            // hit before this fix). Every pre-existing echoed-path line in
            // this file is either an OUTPUT path (--capture, a convention
            // every scene's own COMMAND.txt already keeps consistent) or an
            // stderr-only failure message (never part of a successful run's
            // frozen stdout) - this is the one place that broke that rule.
            std::printf("character: %u real material(s), resolving real diffuse textures\n",
                        bindings.materialCount());
            for (uint32_t m = 0; m < bindings.materialCount(); ++m) {
                const std::string* diffuse = bindings.diffuseFor(m);
                if (diffuse == nullptr) {
                    std::printf("    mat %u: (no slot-0 diffuse)\n", m);
                    continue;
                }
                std::string foundIn;
                if (findAndUploadTexture(texContainer, *diffuse, device.device(), characterMaterialTextures[m],
                                         foundIn)) {
                    characterPerMaterialSrv[m] = characterMaterialTextures[m].srv;
                    std::printf("    mat %u: %s  (%ux%u %s)\n", m, diffuse->c_str(),
                                characterMaterialTextures[m].width, characterMaterialTextures[m].height,
                                characterMaterialTextures[m].dxgiFormatName.c_str());
                } else {
                    std::printf("    mat %u: %s  UNRESOLVED - flat colour\n", m, diffuse->c_str());
                }
            }
            characterTextured = true;
        } else {
            std::printf("character: no real material binding located - staying Checker\n");
        }
    }
    return -1;
}

}  // namespace sr3viewer
