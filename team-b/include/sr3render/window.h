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

    // Creates and shows a `width` x `height` (client area) window titled
    // `title`. Returns false with `error` set if the window class or the
    // window itself could not be created.
    bool create(const std::string& title, uint32_t width, uint32_t height, std::string& error);

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
