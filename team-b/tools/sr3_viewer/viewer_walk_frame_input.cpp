// runWalkDemo() split: per-frame debug fly-camera input.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

void walkFrameInput(WalkSession& sess) {
    auto& noInput = sess.noInput;
    auto& eye = sess.eye;
    auto& yaw = sess.yaw;
    auto& pitch = sess.pitch;
    auto& moveSpeed = sess.moveSpeed;
    auto& freq = sess.freq;
    auto& prevTime = sess.prevTime;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = static_cast<float>(static_cast<double>(now.QuadPart - prevTime.QuadPart) /
                                  static_cast<double>(freq.QuadPart));
    prevTime = now;
    if (dt > 0.25f || dt < 0.0f) dt = 0.0f;
    if (!noInput) {
        // Same polled-input fly camera as runCity()/runScene() - COPIED
        // VERBATIM, debug-only: it moves the VIEWER's own eye, never the
        // simulated character (see the doc comment's "MoveFrameInput.
        // camera is WORLD-FIXED" note).
        const bool fast = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const float speed = moveSpeed * (fast ? 3.0f : 1.0f) * dt;
        const float lookSpeed = 1.6f * dt;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) yaw -= lookSpeed;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) yaw += lookSpeed;
        if (GetAsyncKeyState(VK_UP) & 0x8000) pitch += lookSpeed;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) pitch -= lookSpeed;
        float forward[3] = {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
        float right[3] = {-forward[2], 0.0f, forward[0]};
        const float rightLen = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        if (rightLen > 0.0f) { right[0] /= rightLen; right[2] /= rightLen; }
        if (GetAsyncKeyState('W') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] += forward[c] * speed;
        if (GetAsyncKeyState('S') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] -= forward[c] * speed;
        if (GetAsyncKeyState('D') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] += right[c] * speed;
        if (GetAsyncKeyState('A') & 0x8000) for (int c = 0; c < 3; ++c) eye[c] -= right[c] * speed;
        if (GetAsyncKeyState('E') & 0x8000) eye[1] += speed;
        if (GetAsyncKeyState('Q') & 0x8000) eye[1] -= speed;
    }
    const float pitchLimit = 1.56f;
    if (pitch > pitchLimit) pitch = pitchLimit;
    if (pitch < -pitchLimit) pitch = -pitchLimit;
}

}  // namespace sr3viewer
