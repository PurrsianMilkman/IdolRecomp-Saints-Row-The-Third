// runWalkDemo() split: per-frame render.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

void walkFrameRender(WalkSession& sess, WalkFrame& frame) {
    auto& tintMode = sess.tintMode;
    auto& outlines = sess.outlines;
    auto& v2 = sess.v2;
    auto& eye = sess.eye;
    auto& yaw = sess.yaw;
    auto& pitch = sess.pitch;
    auto& farZ = sess.farZ;
    auto& characterPerMaterialSrv = sess.characterPerMaterialSrv;
    auto& characterTextured = sess.characterTextured;
    auto& characterHeadPerMaterialSrv = sess.characterHeadPerMaterialSrv;
    auto& characterHeadTextured = sess.characterHeadTextured;
    auto& haveHeadMesh = sess.haveHeadMesh;
    auto& loaded = sess.loaded;
    auto& groundTiles = sess.groundTiles;
    auto& pm = *sess.pm_;
    auto& followCam = *sess.followCam_;
    auto& device = *sess.device_;
    auto& swapChain = *sess.swapChain_;
    auto& focusBox = *sess.focusBox_;
    auto& characterRenderer = *sess.characterRenderer_;
    auto& characterHeadRenderer = *sess.characterHeadRenderer_;
    auto& charWorld = frame.charWorld;
    auto& viewProjection = frame.viewProjection;
    // ---- render ---------------------------------------------------------------
    const float aspect = static_cast<float>(swapChain.width()) / static_cast<float>(swapChain.height());
    if (v2) {
        // FIX 2: the follow camera's own real COMMITTED output (Sec4.10:
        // "consumers read the committed set"), not a hand-picked
        // --eye/--yaw/--pitch.
        const sr3player::CameraTransform& cam = followCam.committed();
        const float camEye[3] = {static_cast<float>(cam.position.x), static_cast<float>(cam.position.y),
                                 static_cast<float>(cam.position.z)};
        const float camRight[3] = {static_cast<float>(cam.right.x), static_cast<float>(cam.right.y),
                                   static_cast<float>(cam.right.z)};
        const float camUp[3] = {static_cast<float>(cam.up.x), static_cast<float>(cam.up.y),
                                static_cast<float>(cam.up.z)};
        const float camForward[3] = {static_cast<float>(cam.forward.x), static_cast<float>(cam.forward.y),
                                     static_cast<float>(cam.forward.z)};
        const float fovY = static_cast<float>(cam.fov * 3.14159265 / 180.0);
        buildViewProjectionFromCamera(camEye, camRight, camUp, camForward, fovY, aspect, 0.5f, farZ,
                                      viewProjection);
    } else {
        sr3render::buildFreeViewProjection(eye, yaw, pitch, aspect, 0.5f, farZ, viewProjection);
    }
    swapChain.bind(device.context());
    ID3D11RenderTargetView* rtv = swapChain.renderTargetView();
    device.context()->OMSetRenderTargets(1, &rtv, focusBox.depthView());
    swapChain.clear(device.context(), 0.05f, 0.05f, 0.08f, 1.0f);
    focusBox.clearDepth(device.context());

    // Real ground/terrain under brad's feet (see CityGroundTile's own
    // "GROUND/TERRAIN" top comment for the root cause this fixes) -
    // drawn before the props/character, same world-space-already
    // reasoning as runCity()'s own copy of this draw call. Real
    // textures when resolved (see tryRenderZoneEntry()'s own "GROUND
    // TEXTURE RESOLUTION" doc comment) - same empty-vector regression
    // guard as runCity()'s own copy of this draw call.
    for (auto& kv : groundTiles) {
        CityGroundTile& gt = *kv.second;
        for (auto& r : gt.renderers) {
            // SR3_GROUND_TEX_DEBUG=1: which branch this frame actually
            // took - see groundTexDebugLog()'s own comment, and
            // runCity()'s own copy of this draw call.
            if (groundTexDebugLog()) {
                std::fprintf(stderr, "[groundtex] draw tile %02d%02d: perMaterialSrv.size()=%zu -> %s\n", gt.col,
                            gt.row, gt.perMaterialSrv.size(),
                            gt.perMaterialSrv.empty() ? "MaterialIdAsColour" : "drawTextured");
            }
            if (!gt.perMaterialSrv.empty())
                r->drawTextured(device.context(), viewProjection, gt.perMaterialSrv);
            else
                r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::MaterialIdAsColour, nullptr);
        }
    }

    for (auto& kv : loaded) {
        CityLoadedCell& lc = *kv.second;
        for (auto& p : lc.props)
            for (auto& r : p.renderers) {
                if (tintMode) r->draw(device.context(), viewProjection, sr3render::MeshDrawMode::FlatTint, nullptr, p.world, lc.tint);
                else r->drawTextured(device.context(), viewProjection, p.perMaterialSrv, p.world);
            }
        if (lc.outline) lc.outline->drawSkeleton(device.context(), viewProjection);
    }

    // The character - shares focusBox's depth. --v2 with a real material
    // binding (FIX 1): textured, one draw per material range. Otherwise
    // (including every pre-existing call shape) the original Checker-mode
    // rigid body, byte-for-byte unchanged.
    if (v2 && characterTextured)
        characterRenderer.drawTextured(device.context(), viewProjection, characterPerMaterialSrv, charWorld);
    else
        characterRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker, nullptr, charWorld, nullptr);

    // HEAD FIX: the head - v2-only (haveHeadMesh is only ever set true
    // inside the `if (v2)` setup block above), so v1 draws nothing
    // extra here at all - same charWorld as the body (one rigid
    // character, see the renderer-setup comment above), same
    // textured/Checker branch shape as the body just above.
    if (haveHeadMesh) {
        if (characterHeadTextured)
            characterHeadRenderer.drawTextured(device.context(), viewProjection, characterHeadPerMaterialSrv,
                                                charWorld);
        else
            characterHeadRenderer.draw(device.context(), viewProjection, sr3render::MeshDrawMode::Checker,
                                       nullptr, charWorld, nullptr);
    }

    if (outlines) {
        float fw[16];
        // FIX 3 consistency (--v2 only): the debug focus-window outline
        // tracks the SAME focus reconcile() now uses (brad's own live
        // position), not the free-fly debug eye - otherwise it would
        // visibly drift away from both the streamed cells and the
        // follow-camera's own view as brad walks. Without --v2, eye[0]/
        // eye[2], byte-for-byte as before.
        sr3render::buildWorldMatrix(v2 ? static_cast<float>(pm.st.position.x) : eye[0], 0.0f,
                                    v2 ? static_cast<float>(pm.st.position.z) : eye[2], 0.0f, 1.0f, fw);
        focusBox.drawSkeleton(device.context(), viewProjection, fw);
    }
}

}  // namespace sr3viewer
