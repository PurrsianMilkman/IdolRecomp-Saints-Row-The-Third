// runWalkDemo() split: per-frame streaming, scripted player, graph and follow camera.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

void walkFramePlayer(WalkSession& sess, WalkFrame& frame) {
    auto& v2 = sess.v2;
    auto& walkForwardFrames = sess.walkForwardFrames;
    auto& walkTurnFrames = sess.walkTurnFrames;
    auto& eye = sess.eye;
    auto& channelIndex = sess.channelIndex;
    auto& drawGroup = sess.drawGroup;
    auto& useBonePalette = sess.useBonePalette;
    auto& parentIdx = sess.parentIdx;
    auto& restRig = sess.restRig;
    auto& verts = sess.verts;
    auto& clipRegistry = sess.clipRegistry;
    auto& loopPhase = sess.loopPhase;
    auto& realPoseEnabled = sess.realPoseEnabled;
    auto& src = sess.src;
    auto& heldFacing = sess.heldFacing;
    auto& dupPtr = sess.dupPtr;
    auto& reconcile = sess.reconcile;
    auto& frames = sess.frames;
    auto& mesh = *sess.mesh_;
    auto& bridge = *sess.bridge_;
    auto& pm = *sess.pm_;
    auto& followCam = *sess.followCam_;
    auto& device = *sess.device_;
    auto& characterRenderer = *sess.characterRenderer_;
    const auto& bones = sess.rig_->bones();
    auto& animFrame = frame.animFrame;
    auto& poseResult = frame.poseResult;
    auto& headingNow = frame.headingNow;
    auto& charWorld = frame.charWorld;
    // FIX 3 (--v2 only): stream around brad's own live position, not the
    // free-fly debug eye - see reconcile()'s own comment above. Without
    // --v2 this passes eye[0]/eye[2], byte-for-byte as before.
    reconcile(frames, v2 ? static_cast<float>(pm.st.position.x) : eye[0],
             v2 ? static_cast<float>(pm.st.position.z) : eye[2]);

    // ---- drive the simulated player + graph for THIS frame ----------------
    // kForwardFrames/kTurnFrames default to 40/20 (walkForwardFrames/
    // walkTurnFrames's own defaults, see their declaration above) -
    // byte-for-byte the SAME scripted sequence every existing golden
    // froze unless --walk-forward-frames/--walk-turn-frames override
    // them (--collision's own demonstration use, see this function's
    // doc comment); "stopped" is everything after, same as before.
    const int kForwardFrames = walkForwardFrames, kTurnFrames = walkTurnFrames;
    if (frames < kForwardFrames) { src.A = 0.0f; src.B = 1.0f; }
    else if (frames < kForwardFrames + kTurnFrames) { src.A = 0.6f; src.B = 0.0f; }
    else { src.A = 0.0f; src.B = 0.0f; }

    sr3player::Vec3 facing = pm.st.facingA50;
    if (sr3player::IsZero7(facing)) facing = heldFacing; else heldFacing = facing;
    pm.world.orientation.set(sr3playeranim::OrientationFromFacing(facing));

    animFrame.nowMs = static_cast<int64_t>(frames) * 33;
    animFrame.dt = 1.0 / 30.0;
    animFrame.axisDt = 1.0 / 30.0;
    animFrame.camera.row0 = sr3player::Vec3{1, 0, 0}; // WORLD-FIXED, not the debug camera - see doc comment
    animFrame.camera.row2 = sr3player::Vec3{0, 0, 1};
    // FIX 2 (--v2 only): angles before "physics" (spec-player-runtime.md
    // Sec4.3's cadence) - the SAME fixed 1/30s step animFrame itself
    // uses, never the real wall-clock debug-camera dt, so this stays
    // deterministic.
    if (v2) followCam.preUpdate(animFrame.dt);
    bridge.stepFrame(pm, animFrame);

    // FIX 4 (--v2 only, real U4 pose compose, 2026-10-07 follow-up
    // task): bridges this frame's real GraphOutputList
    // (bridge.lastOutput()) to the EXISTING clip sampler via
    // sr3animpose, then re-skins and re-uploads brad's mesh with the
    // real composed pose, replacing JUDGEMENT CALL 2's rigid-body
    // render for the on-foot common case this demo already is. See
    // sr3animpose/clip_pose_bridge.h and pose_compose.h for the exact
    // compose algorithm/citations.
    if (realPoseEnabled) {
        const float blendedDuration =
            sr3animpose::BlendedLoopingDurationUnits(bridge.lastOutput(), clipRegistry);
        loopPhase.advance(animFrame.dt, bridge.playbackRate(), blendedDuration);
        std::vector<sr3animpose::ActiveLayer> activeLayers =
            sr3animpose::BuildActiveLayers(bridge.lastOutput(), loopPhase, clipRegistry);
        poseResult = sr3animpose::ComposeClipPose(activeLayers, clipRegistry, bones.size());

        std::vector<sr3rig::Rotation3> rotsRig(bones.size(), sr3rig::identityRotation3());
        std::vector<std::array<float, 3>> deltasRig(bones.size(), std::array<float, 3>{0.0f, 0.0f, 0.0f});
        for (size_t b = 0; b < bones.size(); ++b) {
            const sr3anim::BoneSample& s = poseResult.bones[b];
            rotsRig[b] = sr3rig::quatToRotation3(s.rotation.x, s.rotation.y, s.rotation.z, s.rotation.w);
            deltasRig[b] = s.translationDelta;
        }
        std::vector<sr3rig::Mat3x4> skinRig =
            sr3rig::computeAnimatedSkinningMatrices(parentIdx, restRig, &rotsRig, &deltasRig);
        std::vector<sr3rig::Mat3x4> skinMesh(bones.size());
        for (size_t b = 0; b < bones.size(); ++b)
            skinMesh[b] = useBonePalette ? sr3rig::conjugateToMeshSpaceInverted(skinRig[b])
                                         : sr3rig::conjugateToMeshSpace(skinRig[b]);
        std::vector<std::array<float, 3>> posedPositions = sr3rig::skinVertices(verts, skinMesh, nullptr);
        std::string reuploadError;
        if (!characterRenderer.upload(device.device(), mesh, channelIndex, 0, reuploadError, drawGroup,
                                      &posedPositions, dupPtr)) {
            std::fprintf(stderr, "frame %d: posed re-upload failed: %s\n", frames, reuploadError.c_str());
        }
    }

    // The real follow-camera target hookup, every frame (Judgement call 1).
    sr3playeranim::DriveCameraTarget(followCam, pm);
    // FIX 2 (--v2 only): position follows THIS frame's player position
    // (Sec4.3's "Consequence" paragraph) - update() commits working to
    // committed (Sec4.11 step 6) using the target DriveCameraTarget()
    // just set.
    if (v2) followCam.update(sr3player::CameraFrameTimes{animFrame.dt, animFrame.dt, animFrame.dt});

    headingNow = sr3player::HeadingFromForward(pm.world.orientation.get().forward);
    sr3render::buildWorldMatrix(static_cast<float>(pm.st.position.x), static_cast<float>(pm.st.position.y),
                                static_cast<float>(pm.st.position.z), static_cast<float>(headingNow), 1.0f,
                                charWorld);
}

}  // namespace sr3viewer
