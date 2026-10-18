// runWalkDemo() split: window, device, swap chain and the character renderer upload.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupWindowDevice(WalkSession& sess) {
    auto& winWidth = sess.winWidth;
    auto& winHeight = sess.winHeight;
    auto& visible = sess.visible;
    auto& channelIndex = sess.channelIndex;
    auto& drawGroup = sess.drawGroup;
    auto& vertexDuplication = sess.vertexDuplication;
    auto& bindPositions = sess.bindPositions;
    auto& error = sess.error;
    auto& depthWidth = sess.depthWidth;
    auto& depthHeight = sess.depthHeight;
    auto& dupPtr = sess.dupPtr;
    auto& mesh = *sess.mesh_;
    // ---- window/device/swapchain/shared depth (same shape as runCity()) ------
    sess.window_.emplace();
    auto& window = *sess.window_;
    if (!window.create("SR3 Viewer - walk (player<->animation demo)", winWidth, winHeight, error,
                       visible)) {
        std::fprintf(stderr, "window creation failed: %s\n", error.c_str());
        return 1;
    }
    sess.device_.emplace();
    auto& device = *sess.device_;
    if (!device.initialise(16, 16, error)) {
        std::fprintf(stderr, "device init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("device   : %s (%s)\n",
                device.kind() == sr3render::DeviceKind::Hardware ? "HARDWARE" : "WARP (software)",
                device.adapterName().c_str());
    sess.swapChain_.emplace();
    auto& swapChain = *sess.swapChain_;
    if (!swapChain.create(device.device(), window.nativeHandle(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "swap chain creation failed: %s\n", error.c_str());
        return 1;
    }
    // A persistent renderer that owns the shared depth buffer (same "only
    // the first renderer owns the depth buffer" pattern already established
    // in this file - runZoneTile(), every cell's prop renderers in
    // runCity() above) and draws the focus-window outline.
    sess.focusBox_.emplace();
    auto& focusBox = *sess.focusBox_;
    if (!uploadCityOutline(focusBox, device.device(), 0.0f, 0.0f, kLevel4WindowX * 0.5f, kLevel4WindowZ * 0.5f,
                           false, error) ||
        !focusBox.createDepth(device.device(), winWidth, winHeight, error)) {
        std::fprintf(stderr, "focus/depth renderer failed: %s\n", error.c_str());
        return 1;
    }
    depthWidth = winWidth;
    depthHeight = winHeight;

    // The character renderer: uploaded ONCE (bind pose, see above), NEVER
    // calls createDepth() - it shares focusBox's depth, same pattern.
    sess.characterRenderer_.emplace();
    auto& characterRenderer = *sess.characterRenderer_;
    if (!characterRenderer.initialise(device.device(), error)) {
        std::fprintf(stderr, "character renderer init failed: %s\n", error.c_str());
        return 1;
    }
    dupPtr = vertexDuplication.sourceIndex.empty() ? nullptr : &vertexDuplication;
    if (!characterRenderer.upload(device.device(), mesh, channelIndex, 0, error, drawGroup, &bindPositions, dupPtr)) {
        std::fprintf(stderr, "character upload failed: %s\n", error.c_str());
        return 1;
    }
    return -1;
}

}  // namespace sr3viewer
