// runWalkDemo() split: per-frame capture.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

void walkFrameCapture(WalkSession& sess, WalkFrame& frame) {
    auto& captureFrame = sess.captureFrame;
    auto& capturePath = sess.capturePath;
    auto& captureFrames = sess.captureFrames;
    auto& realPoseEnabled = sess.realPoseEnabled;
    auto& error = sess.error;
    auto& frames = sess.frames;
    const auto& stateActions = *sess.stateActions_;
    auto& bridge = *sess.bridge_;
    auto& pm = *sess.pm_;
    auto& followCam = *sess.followCam_;
    auto& device = *sess.device_;
    auto& swapChain = *sess.swapChain_;
    const auto& bones = sess.rig_->bones();
    auto& poseResult = frame.poseResult;
    auto& headingNow = frame.headingNow;
    // captureFrames (plural, --capture-frames): capture several frames in
    // one run, each named '<capturePath>_frameN.png'. Empty (the default,
    // every pre-existing call shape) keeps the ORIGINAL single
    // --capture/--capture-frame behaviour below, byte-for-byte unchanged.
    if (!capturePath.empty() && !captureFrames.empty()) {
        for (int wantedFrame : captureFrames) {
            if (frames != wantedFrame) continue;
            LogWalkDemoAnimDiagnostics(frames, bridge, stateActions, realPoseEnabled ? &poseResult : nullptr,
                                       realPoseEnabled ? &bones : nullptr);
            const std::string path = capturePath + "_frame" + std::to_string(wantedFrame) + ".png";
            std::vector<uint8_t> pixels;
            if (swapChain.capture(device.context(), pixels, error) &&
                sr3render::writePng(path, swapChain.width(), swapChain.height(), pixels, error)) {
                std::printf("captured : %s (frame %d, player pos (%.2f %.2f %.2f) heading %.1f deg, "
                            "locomotion %d, playback rate %.3f, follow-cam target (%.2f %.2f %.2f) heading "
                            "%.1f deg, follow-cam committed eye (%.2f %.2f %.2f) fov %.2f deg)\n",
                            path.c_str(), frames, pm.st.position.x, pm.st.position.y, pm.st.position.z,
                            headingNow * 180.0 / 3.14159265, pm.st.locomotionState, bridge.playbackRate(),
                            followCam.target.referencePoint.x, followCam.target.referencePoint.y,
                            followCam.target.referencePoint.z, followCam.target.heading * 180.0 / 3.14159265,
                            followCam.committed().position.x, followCam.committed().position.y,
                            followCam.committed().position.z, followCam.committed().fov);
            } else {
                std::fprintf(stderr, "capture failed: %s\n", error.c_str());
            }
        }
    } else if (!capturePath.empty() && frames == captureFrame) {
        LogWalkDemoAnimDiagnostics(frames, bridge, stateActions, realPoseEnabled ? &poseResult : nullptr,
                                   realPoseEnabled ? &bones : nullptr);
        std::vector<uint8_t> pixels;
        if (swapChain.capture(device.context(), pixels, error) &&
            sr3render::writePng(capturePath, swapChain.width(), swapChain.height(), pixels, error)) {
            std::printf("captured : %s (frame %d, player pos (%.2f %.2f %.2f) heading %.1f deg, "
                        "locomotion %d, playback rate %.3f, follow-cam target (%.2f %.2f %.2f) heading %.1f deg)\n",
                        capturePath.c_str(), frames, pm.st.position.x, pm.st.position.y, pm.st.position.z,
                        headingNow * 180.0 / 3.14159265, pm.st.locomotionState, bridge.playbackRate(),
                        followCam.target.referencePoint.x, followCam.target.referencePoint.y,
                        followCam.target.referencePoint.z, followCam.target.heading * 180.0 / 3.14159265);
        } else {
            std::fprintf(stderr, "capture failed: %s\n", error.c_str());
        }
    }
}

}  // namespace sr3viewer
