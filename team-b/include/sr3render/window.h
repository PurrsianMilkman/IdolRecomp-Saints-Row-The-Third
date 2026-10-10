// Minimal Win32 window for interactive viewing.
//
// The engine's primary path stays HEADLESS (see device.h): offscreen
// render, readback, PNG. That is the verification backbone and nothing
// here replaces it - a window shows a picture to a person, but it cannot
// prove anything to an automated check the way a written-out image can.
// This is an addition, not a migration.
//
// Deliberately tiny: create, pump, close, report size. No input handling
// beyond "did the user ask to quit", no menus, no DPI gymnastics. It
// exists so meshes and (later) animation can be looked at while they are
// being worked on, which is genuinely hard to do through PNG diffs alone.
//
// Window creation is allowed to FAIL and say so. A non-interactive or
// session-0 context is a normal thing to run in here, and the honest
// response is a clear error, not a silently invisible window.

#pragma once

#include <cstdint>
#include <string>

namespace sr3render {

class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates a `width` x `height` (client area) window titled `title`.
    // Returns false with `error` set if the window class or the window
    // itself could not be created.
    //
    // `visible` (default false, window-focus-stealing fix, 2026-10-08):
    // every caller in this project renders through an offscreen D3D11 swap
    // chain/render target (WARP-friendly, and this project's goldens/tests
    // already force WARP via SR3_FORCE_WARP=1) - the window itself only
    // needs a valid HWND for SwapChain::create() to present into, not an
    // on-screen presence. Leaving this false means the window is created
    // WITHOUT WS_VISIBLE, is never SW_SHOW'd, and carries
    // WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW so it can never steal focus or
    // flash on top of whatever the user is doing on their own desktop
    // (goldens/ctest run this way). Pass true only for an actual
    // interactive/--visible invocation, where a developer deliberately
    // wants to watch the render happen.
    bool create(const std::string& title, uint32_t width, uint32_t height, std::string& error,
                bool visible = false);

    // Processes all pending messages. Returns false once the window has
    // been closed (or Escape pressed), which is the caller's cue to stop
    // its loop.
    bool pumpMessages();

    // Current client-area size. Tracks resizes, so a caller can compare
    // against the swap chain's size and resize when they diverge.
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    // Native handle, as void* so this header does not drag in windows.h
    // for every consumer. Cast to HWND at the point of use.
    void* nativeHandle() const { return hwnd_; }

private:
    void* hwnd_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool shouldClose_ = false;
};

} // namespace sr3render
