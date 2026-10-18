// runWalkDemo() split: frame-loop state.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupLoopState(WalkSession& sess) {
    auto& noInput = sess.noInput;
    auto& eye = sess.eye;
    auto& yaw = sess.yaw;
    auto& pitch = sess.pitch;
    auto& freq = sess.freq;
    auto& prevTime = sess.prevTime;
    std::printf("camera   : eye (%.1f %.1f %.1f) yaw %.1f pitch %.1f%s\n", static_cast<double>(eye[0]),
                static_cast<double>(eye[1]), static_cast<double>(eye[2]), static_cast<double>(yaw * 180.0f / 3.14159265f),
                static_cast<double>(pitch * 180.0f / 3.14159265f), noInput ? " [--no-input]" : "");

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prevTime);
    return -1;
}

}  // namespace sr3viewer
