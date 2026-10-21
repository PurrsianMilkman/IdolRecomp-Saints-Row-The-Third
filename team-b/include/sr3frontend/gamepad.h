// Gamepad menu input for the front end (XInput, first connected pad). Edge-triggered like
// sr3game::FrontendInput. CHOSEN mapping (the retail binding layer, spec-tables-ui-controls.md,
// is not reproduced): D-pad or left stick = up/down/left/right, A = accept, B = back,
// Start = pause/resume (`start`).
#pragma once

#include "sr3game/frontend.h"

namespace sr3frontend {

struct GamepadEdges {
    sr3game::FrontendInput menu;
    bool start = false;
    bool connected = false;
};

// Raw pad state for one frame (tests feed this directly).
struct PadState {
    bool connected = false;
    bool up = false, down = false, left = false, right = false;  // D-pad
    bool a = false, b = false, start = false;
    float stickX = 0.0f, stickY = 0.0f;  // left stick, -1..1, +Y up
};

class GamepadReader {
public:
    // Turns held states into edges; the stick counts as a direction past 0.5 and is released
    // below 0.3.
    GamepadEdges step(const PadState& s);
    // Reads XInput pad 0..3 (the first connected) and steps.
    GamepadEdges poll();

private:
    PadState prev_;
    bool stickUp_ = false, stickDown_ = false, stickLeft_ = false, stickRight_ = false;
};

// OR of two inputs.
sr3game::FrontendInput Merge(const sr3game::FrontendInput& a, const sr3game::FrontendInput& b);

}  // namespace sr3frontend
