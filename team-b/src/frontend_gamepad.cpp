// Gamepad menu input. See include/sr3frontend/gamepad.h.

#include "sr3frontend/gamepad.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#endif

namespace sr3frontend {

namespace {

bool Hyst(bool held, float v, bool positive) {
    const float x = positive ? v : -v;
    return held ? x > 0.3f : x > 0.5f;
}

}  // namespace

GamepadEdges GamepadReader::step(const PadState& s) {
    GamepadEdges e;
    e.connected = s.connected;
    if (!s.connected) {
        prev_ = PadState{};
        stickUp_ = stickDown_ = stickLeft_ = stickRight_ = false;
        return e;
    }
    const bool su = Hyst(stickUp_, s.stickY, true), sd = Hyst(stickDown_, s.stickY, false);
    const bool sl = Hyst(stickLeft_, s.stickX, false), sr = Hyst(stickRight_, s.stickX, true);
    e.menu.up = (s.up && !prev_.up) || (su && !stickUp_);
    e.menu.down = (s.down && !prev_.down) || (sd && !stickDown_);
    e.menu.left = (s.left && !prev_.left) || (sl && !stickLeft_);
    e.menu.right = (s.right && !prev_.right) || (sr && !stickRight_);
    e.menu.accept = s.a && !prev_.a;
    e.menu.back = s.b && !prev_.b;
    e.start = s.start && !prev_.start;
    stickUp_ = su;
    stickDown_ = sd;
    stickLeft_ = sl;
    stickRight_ = sr;
    prev_ = s;
    return e;
}

GamepadEdges GamepadReader::poll() {
    PadState s;
#ifdef _WIN32
    for (DWORD i = 0; i < XUSER_MAX_COUNT && !s.connected; ++i) {
        XINPUT_STATE st{};
        if (XInputGetState(i, &st) != ERROR_SUCCESS) continue;
        const WORD b = st.Gamepad.wButtons;
        s.connected = true;
        s.up = (b & XINPUT_GAMEPAD_DPAD_UP) != 0;
        s.down = (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        s.left = (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        s.right = (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        s.a = (b & XINPUT_GAMEPAD_A) != 0;
        s.b = (b & XINPUT_GAMEPAD_B) != 0;
        s.start = (b & XINPUT_GAMEPAD_START) != 0;
        s.stickX = static_cast<float>(st.Gamepad.sThumbLX) / 32767.0f;
        s.stickY = static_cast<float>(st.Gamepad.sThumbLY) / 32767.0f;
    }
#endif
    return step(s);
}

sr3game::FrontendInput Merge(const sr3game::FrontendInput& a, const sr3game::FrontendInput& b) {
    sr3game::FrontendInput r;
    r.up = a.up || b.up;
    r.down = a.down || b.down;
    r.left = a.left || b.left;
    r.right = a.right || b.right;
    r.accept = a.accept || b.accept;
    r.back = a.back || b.back;
    return r;
}

}  // namespace sr3frontend
