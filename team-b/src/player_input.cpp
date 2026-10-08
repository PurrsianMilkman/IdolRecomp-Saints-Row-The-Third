// sr3player - player runtime unit U0, the input query contract. Every rule
// below is spec-player-runtime.md Sec1 (cited per function); see
// include/sr3player/input.h for the scope and the scope cuts.
#include "sr3player/input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

#include "sr3luahost/open_state.h"

namespace sr3player {

namespace {

// Stick-direction threshold, +-0.4 (0x01117b70 / 0x01118140, Sec1.10).
// CHOSEN: "beyond +-0.4" is read as strictly beyond; the spec does not say
// which side an exactly-0.4 value falls on.
constexpr float kStickDirThreshold = 0.4f;
// Trigger hysteresis offset 0.1 (0x012a2d38, Sec1.4 step 2).
constexpr float kTriggerHysteresis = 0.1f;
// Scheme sub-arrays are copied with counts capped at 100 (Sec1.7.2 step 2).
constexpr size_t kSchemeRecordCap = 100;

uint32_t padMaskOf(const ButtonBinding& b) {
    uint32_t m = 0;
    for (int32_t v : {b.padA, b.padB}) {
        if (v >= 0 && v < 32) m |= (1u << v);
    }
    return m;
}

// Unit-circle clamp shared by the latch (Sec1.4 step 7) and 0x005bc2c0
// (Sec1.12.1). 0x00dae390 is "an approximate <= with tolerance 1e-4";
// CHOSEN reading: the length counts as <= 1 while lenSq <= 1 + 1e-4, so the
// scale-down runs only when lenSq exceeds 1 + 1e-4.
void clampToUnit(float x, float y, float& ox, float& oy) {
    float lenSq = x * x + y * y;
    if (lenSq > 1.0f + kZeroTolerance) {
        float len = std::sqrt(lenSq);
        ox = x / len;
        oy = y / len;
    } else {
        ox = x;
        oy = y;
    }
}

bool strictlyOppositeSigns(float a, float b) { return (a > 0.0f && b < 0.0f) || (a < 0.0f && b > 0.0f); }

// Response curve 0x005b9f60/0x005b9fe0 (Sec1.9.1): 0.9 v^3 +0.1 when
// positive, -0.1 when negative, 0 stays 0.
float responseCurve(float v) {
    if (v > 0.0f) return 0.9f * v * v * v + 0.1f;
    if (v < 0.0f) return 0.9f * v * v * v - 0.1f;
    return 0.0f;
}

bool ieq(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}
bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!ieq(a[i], b[i])) return false;
    return true;
}
bool icontains(const std::string& hay, const std::string& needle) {
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), ieq) != hay.end();
}

bool validKey(int32_t k) { return k >= 0 && k <= 255; }
// Mouse fields: "-1 or below 7" (Sec1.7.3/Sec1.7.4). CHOSEN: "below 7" is
// read as 0..6 (an unsigned compare) - the spec naming -1 separately implies
// other negatives are rejected.
bool validMouse(int32_t m) { return m == -1 || (m >= 0 && m < kNumMouseButtons); }

} // namespace

float ApplyDeadZone(float v, float deadZone, bool invert, bool disable) {
    float r;
    if (std::fabs(v) < deadZone) {
        r = 0.0f;
    } else {
        float sign = v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
        r = (v - sign * deadZone) / (1.0f - deadZone);
    }
    if (disable) r = 0.0f;
    if (invert) r = -r;
    return r;
}

int32_t FindSchemeIndex(const std::vector<SchemeSlot>& slots, const std::string& needle) {
    for (size_t i = 0; i < slots.size(); ++i)
        if (iequals(slots[i].name, needle)) return static_cast<int32_t>(i);
    for (size_t i = 0; i < slots.size(); ++i)
        if (icontains(slots[i].name, needle)) return static_cast<int32_t>(i);
    return -1;
}

InputSystem::InputSystem(DeviceSource* device) : device_(device ? device : &nullDevice_) {}

void InputSystem::setDevice(DeviceSource* device) { device_ = device ? device : &nullDevice_; }

// ---------------------------------------------------------------------------
// Sec1.1 - the gate 0x005be880.
// ---------------------------------------------------------------------------
void InputSystem::latchFrame() {
    if (device_->appActiveAndNotIconic()) {
        frozen_ = 0;
        // 0x005bac90 (cursor capture/clip, Sec1.12.3) is platform-only and
        // not part of the query contract - not modelled.
        latch();
    } else if (frozen_ == 0) {
        fullClear();
        frozen_ = 1;
    }
    // Steps 2c (engine mouse-byte clear) and 2d (sector tracker 0x005c2700)
    // are device-side / "not part of the contract" (Sec1.1, Sec1.12.4).
}

// ---------------------------------------------------------------------------
// Sec1.4 - the latch 0x005bcde0.
// ---------------------------------------------------------------------------
void InputSystem::latch() {
    DeviceSource& dev = *device_;
    if (!dev.padRegistered()) {
        // Step 1: zero +0x50/+0x54/+0x58/+0x60 and both stick records;
        // +0x5c is left stale (harmless, +0x60 is 0).
        ctx_.padHeld = ctx_.padRepeat = ctx_.padHeldEnable = ctx_.padPressedEnable = 0;
        ctx_.sticks = {};
    } else {
        // Step 2: pad buttons.
        uint32_t held = 0, repeat = 0, pressed = 0;
        const uint32_t now = dev.timeMs();
        for (int b = 0; b < kNumPadButtons; ++b) {
            const bool down = dev.padButtonDown(b);
            if (down) held |= (1u << b);
            if (b == kPadLT || b == kPadRT) {
                const float value = dev.padButtonValue(b);
                const float threshold = dev.padButtonThreshold(b);
                dev.setPadButtonThreshold(b, down ? std::max(value - kTriggerHysteresis, threshold)
                                                  : std::min(value + kTriggerHysteresis, threshold));
            }
            if (dev.padButtonRepeatPulse(b)) repeat |= (1u << b);
            if (dev.padButtonDownEdge(b)) {
                pressed |= (1u << b);
                ctx_.padDownTimeMs[b] = now;
            }
        }
        ctx_.padHeld = held;
        ctx_.padRepeat = repeat;
        ctx_.padHeldEnable = 0xffffffffu;
        ctx_.padPressed = pressed;
        ctx_.padPressedEnable = 0xffffffffu;

        // Steps 3-4: sticks.
        for (int s = 0; s < 2; ++s) {
            StickRecord& r = ctx_.sticks[s];
            r.prevX = r.x;
            r.prevY = r.y;
            float x = 0, y = 0;
            dev.padStickDeadZoned(s, x, y);
            // Sign-flip suppression: strictly opposite signs -> 0 this frame.
            if (strictlyOppositeSigns(r.prevX, x)) x = 0.0f;
            if (strictlyOppositeSigns(r.prevY, y)) y = 0.0f;
            r.x = x;
            r.y = y;
            dev.padStickRaw(s, r.rawX, r.rawY);
            r.pulseX = dev.padStickDirectionPulse(s, 2) ? -1 : (dev.padStickDirectionPulse(s, 3) ? 1 : 0);
            r.pulseY = dev.padStickDirectionPulse(s, 0) ? -1 : (dev.padStickDirectionPulse(s, 1) ? 1 : 0);
            r.blockLeft = r.blockRight = r.blockUp = r.blockDown = r.blockAnalogX = r.blockAnalogY = 0;
        }
    }

    // Step 5.
    ctx_.kbmActivity = 0;
    if (controlStateGetter_ && controlStateGetter_() == 5) {
        if (!state5LatchSkipByte.has_value())
            throw sr3luahost::OpenStateError("latch state-5 skip byte 0x0140e974", "spec-player-runtime.md Sec1.4 step 5");
        if (*state5LatchSkipByte) return;
    }

    // Step 6: keyboard, then mouse buttons.
    const uint32_t now = dev.timeMs();
    for (int k = 0; k < kNumKeys; ++k) ctx_.keys[k].enabled = 1;
    for (int k = 0; k < kNumKeys; ++k) {
        ButtonRecord& r = ctx_.keys[k];
        r.held = dev.keyHeld(k) ? 1 : 0;
        r.pressed = dev.keyPressed(k) ? 1 : 0;
        if (r.pressed) r.downTimeMs = now;
        r.released = dev.keyReleased(k) ? 1 : 0;
        if (r.held || r.pressed || r.released) ctx_.kbmActivity = 1;
    }
    for (int m = 0; m < kNumMouseButtons; ++m) {
        ButtonRecord& r = ctx_.mouseButtons[m];
        r.enabled = 1;
        r.held = dev.mouseHeld(m) ? 1 : 0;
        r.pressed = dev.mousePressed(m) ? 1 : 0;
        if (r.pressed) r.downTimeMs = now;
        r.released = dev.mouseReleased(m) ? 1 : 0;
        if (r.held || r.pressed || r.released) ctx_.kbmActivity = 1;
    }
    // The wheel is ignored while the middle button is held.
    if (ctx_.mouseButtons[kMouseMiddle].held && middleDisablesWheel == 1) {
        ctx_.mouseButtons[kWheelUp].enabled = 0;
        ctx_.mouseButtons[kWheelDown].enabled = 0;
    }

    // Step 7: mouse axes.
    ctx_.mouseAxes[0].enabled = 1;
    ctx_.mouseAxes[1].enabled = 1;
    float dx = 0, dy = 0;
    dev.mouseDeltas(dx, dy);
    float mx = dx * mouseSensitivityX * mouseGlobalScale;
    float my = dy * mouseSensitivityY * mouseGlobalScale;
    clampToUnit(mx, my, mx, my);
    ctx_.mouseAxes[0].value = ctx_.mouseAxes[0].valueCopy = mx;
    ctx_.mouseAxes[1].value = ctx_.mouseAxes[1].valueCopy = my;

    // Step 8.
    trackDeviceFamily();
}

// ---------------------------------------------------------------------------
// Sec1.5 - device-family tracker 0x005bc9d0.
// CHOSEN (both pairs below): the spec states each rule as a case on the
// byte's current value ("If +0x9f5 is 0: ... If +0x9f5 is 1: ..."), read
// here as mutually exclusive branches on the value at entry, so a byte set
// in the first branch is not re-tested by the second in the same frame.
// ---------------------------------------------------------------------------
void InputSystem::trackDeviceFamily() {
    if (!device_->padRegistered()) {
        ctx_.gamepadActive = 0;
        ctx_.cameraFromKbm = 1;
        return;
    }
    if (ctx_.gamepadActive == 0) {
        // 0x005ba600: +0x50 non-zero, or any dead-zoned stick axis not ~0.
        bool padActivity = ctx_.padHeld != 0;
        for (const StickRecord& s : ctx_.sticks)
            if (!WithinZero(s.x) || !WithinZero(s.y)) padActivity = true;
        if (padActivity) ctx_.gamepadActive = 1;
    } else {
        // 0x005ba5b0: either mouse-axis value not ~0.
        const bool mouseMovement = !WithinZero(ctx_.mouseAxes[0].value) || !WithinZero(ctx_.mouseAxes[1].value);
        if (ctx_.kbmActivity || mouseMovement) ctx_.gamepadActive = 0;
    }
    int rotateOut = kOutNone, elevateOut = kOutNone;
    (void)axis(kCaaCameraRotate, &rotateOut);
    (void)axis(kCaaCameraElevate, &elevateOut);
    if (ctx_.cameraFromKbm) {
        if (rotateOut == kOutAxisGamepad || elevateOut == kOutAxisGamepad) ctx_.cameraFromKbm = 0;
    } else {
        if (rotateOut == kOutAxisKbm || elevateOut == kOutAxisKbm) ctx_.cameraFromKbm = 1;
    }
}

// ---------------------------------------------------------------------------
// Sec1.11.1 - "clear latched" 0x005bbf60.
// ---------------------------------------------------------------------------
void InputSystem::clearLatched() {
    for (StickRecord& s : ctx_.sticks)
        s.blockLeft = s.blockRight = s.blockUp = s.blockDown = s.blockAnalogX = s.blockAnalogY = 1;
    ctx_.padHeldEnable = 0;
    ctx_.padPressedEnable = 0;
    for (ButtonRecord& r : ctx_.keys) r.enabled = 0;
    for (ButtonRecord& r : ctx_.mouseButtons) r.enabled = 0;
    for (MouseAxisRecord& r : ctx_.mouseAxes) r.enabled = 0;
}

// ---------------------------------------------------------------------------
// Sec1.11.3 - full clear 0x005bc830. The bound-key table and the pad
// down-edge timestamps are not in the spec's list, so they are kept.
// ---------------------------------------------------------------------------
void InputSystem::fullClear() {
    ctx_.sticks = {};
    ctx_.padHeld = ctx_.padRepeat = ctx_.padHeldEnable = ctx_.padPressed = ctx_.padPressedEnable = 0;
    ctx_.keys = {};
    ctx_.kbmActivity = 0;
    ctx_.mouseButtons = {};
    ctx_.mouseAxes = {};
    device_->resetKeyboardAndMouse();
    ctx_.gamepadActive = 0;
    ctx_.cameraFromKbm = 0;
}

// ---------------------------------------------------------------------------
// Record access helpers. Scan code 0 and mouse -1 never match (Sec1.8.3).
// ---------------------------------------------------------------------------
const ButtonRecord* InputSystem::keyRecord(int scan) const {
    return (scan > 0 && scan < kNumKeys) ? &ctx_.keys[scan] : nullptr;
}
ButtonRecord* InputSystem::keyRecord(int scan) { return (scan > 0 && scan < kNumKeys) ? &ctx_.keys[scan] : nullptr; }
const ButtonRecord* InputSystem::mouseRecord(int button) const {
    return (button >= 0 && button < kNumMouseButtons) ? &ctx_.mouseButtons[button] : nullptr;
}
ButtonRecord* InputSystem::mouseRecord(int button) {
    return (button >= 0 && button < kNumMouseButtons) ? &ctx_.mouseButtons[button] : nullptr;
}

// ---------------------------------------------------------------------------
// Sec1.10 - stick-direction primitives. d selects stick d/4, direction d%4:
// 0 up (+y), 1 down (-y), 2 right (+x), 3 left (-x).
// ---------------------------------------------------------------------------
namespace {
struct DirRef {
    int stick = -1;
    int dir = -1;
};
DirRef dirRef(int d) {
    if (d < 0 || d > 7) return {};
    return {d / 4, d % 4};
}
float component(const StickRecord& s, int dir, bool previous) {
    const float x = previous ? s.prevX : s.x;
    const float y = previous ? s.prevY : s.y;
    switch (dir) {
    case 0: return y;
    case 1: return -y;
    case 2: return x;
    default: return -x;
    }
}
uint8_t& blockByte(StickRecord& s, int dir) {
    switch (dir) {
    case 0: return s.blockUp;
    case 1: return s.blockDown;
    case 2: return s.blockRight;
    default: return s.blockLeft;
    }
}
uint8_t blockByte(const StickRecord& s, int dir) { return blockByte(const_cast<StickRecord&>(s), dir); }
} // namespace

bool InputSystem::stickDirHeld(int d) const {
    DirRef r = dirRef(d);
    if (r.stick < 0) return false;
    const StickRecord& s = ctx_.sticks[r.stick];
    return !blockByte(s, r.dir) && component(s, r.dir, false) > kStickDirThreshold;
}

bool InputSystem::stickDirPressed(int d) const {
    DirRef r = dirRef(d);
    if (r.stick < 0) return false;
    const StickRecord& s = ctx_.sticks[r.stick];
    return !blockByte(s, r.dir) && component(s, r.dir, false) > kStickDirThreshold &&
           !(component(s, r.dir, true) > kStickDirThreshold);
}

bool InputSystem::stickDirPressedConsume(int d) {
    if (!stickDirPressed(d)) return false;
    DirRef r = dirRef(d);
    blockByte(ctx_.sticks[r.stick], r.dir) = 1;
    return true;
}

bool InputSystem::stickDirPulseConsume(int d) {
    DirRef r = dirRef(d);
    if (r.stick < 0) return false;
    StickRecord& s = ctx_.sticks[r.stick];
    if (blockByte(s, r.dir)) return false;
    bool fired = false;
    switch (r.dir) {
    case 0: fired = s.pulseY < 0; if (fired) s.pulseY = 0; break;
    case 1: fired = s.pulseY > 0; if (fired) s.pulseY = 0; break;
    case 2: fired = s.pulseX > 0; if (fired) s.pulseX = 0; break;
    default: fired = s.pulseX < 0; if (fired) s.pulseX = 0; break;
    }
    if (fired) blockByte(s, r.dir) = 1;
    return fired;
}

float InputSystem::stickDirAnalog(int d) const {
    DirRef r = dirRef(d);
    if (r.stick < 0) return 0.0f;
    const StickRecord& s = ctx_.sticks[r.stick];
    const bool yAxis = (r.dir == 0 || r.dir == 1);
    if (yAxis ? s.blockAnalogY : s.blockAnalogX) return 0.0f;
    return std::max(component(s, r.dir, false), 0.0f);
}

float InputSystem::stickDirPrevAnalog(int d) const {
    DirRef r = dirRef(d);
    if (r.stick < 0) return 0.0f;
    return std::max(component(ctx_.sticks[r.stick], r.dir, true), 0.0f);
}

// ---------------------------------------------------------------------------
// Sec1.8 - button-action queries. Evaluation order (Sec1.8.3): pad, stick
// direction, key 1, key 2, mouse 1, mouse 2; first satisfied source wins.
// Every query rejects cba > 0xa3 without writing `out`. CHOSEN: a negative
// cba is rejected the same way (an unsigned compare in the real code is the
// only reading that never indexes outside the table).
// ---------------------------------------------------------------------------
namespace {
bool cbaInRange(int cba) { return cba >= 0 && cba <= kMaxCba; }
void setOut(int* out, int v) {
    if (out) *out = v;
}
} // namespace

bool InputSystem::pressed(int cba, int* out) const {
    if (!cbaInRange(cba)) return false;
    const ButtonBinding& b = buttons_[cba];
    if (padMaskOf(b) & ctx_.padPressed & ctx_.padPressedEnable) return setOut(out, kOutPadButton), true;
    if (stickDirPressed(b.stickDir)) return setOut(out, kOutStickDirection), true;
    for (int32_t k : {b.key1, b.key2})
        if (const ButtonRecord* r = keyRecord(k); r && r->pressed && r->enabled) return setOut(out, kOutKeyOrMouse), true;
    for (int32_t m : {b.mouse1, b.mouse2})
        if (const ButtonRecord* r = mouseRecord(m); r && r->pressed && r->enabled) return setOut(out, kOutKeyOrMouse), true;
    setOut(out, kOutNone);
    return false;
}

bool InputSystem::held(int cba, int* out) const {
    if (!cbaInRange(cba)) return false;
    const ButtonBinding& b = buttons_[cba];
    if (padMaskOf(b) & ctx_.padHeld & ctx_.padHeldEnable) return setOut(out, kOutPadButton), true;
    if (stickDirHeld(b.stickDir)) return setOut(out, kOutStickDirection), true;
    for (int32_t k : {b.key1, b.key2})
        if (const ButtonRecord* r = keyRecord(k); r && r->held && r->enabled) return setOut(out, kOutKeyOrMouse), true;
    for (int32_t m : {b.mouse1, b.mouse2})
        if (const ButtonRecord* r = mouseRecord(m); r && r->held && r->enabled) return setOut(out, kOutKeyOrMouse), true;
    setOut(out, kOutNone);
    return false;
}

bool InputSystem::pressedConsume(int cba, int* out) {
    if (!cbaInRange(cba)) return false;
    const ButtonBinding& b = buttons_[cba];
    const uint32_t mask = padMaskOf(b);
    if (mask & ctx_.padPressed & ctx_.padPressedEnable) {
        // "clear the mask bits from both +0x58 and +0x60" (Sec1.8.3 item 1).
        ctx_.padHeldEnable &= ~mask;
        ctx_.padPressedEnable &= ~mask;
        return setOut(out, kOutPadButton), true;
    }
    if (stickDirPressedConsume(b.stickDir)) return setOut(out, kOutStickDirection), true;
    for (int32_t k : {b.key1, b.key2}) {
        if (ButtonRecord* r = keyRecord(k); r && r->pressed && r->enabled) {
            r->enabled = 0;
            return setOut(out, kOutKeyOrMouse), true;
        }
    }
    for (int32_t m : {b.mouse1, b.mouse2}) {
        if (ButtonRecord* r = mouseRecord(m); r && r->pressed && r->enabled) {
            r->enabled = 0;
            return setOut(out, kOutKeyOrMouse), true;
        }
    }
    setOut(out, kOutNone);
    return false;
}

bool InputSystem::navigationPulseConsume(int cba, int* out) {
    if (!cbaInRange(cba)) return false;
    const ButtonBinding& b = buttons_[cba];
    const uint32_t mask = padMaskOf(b);
    if (mask & ctx_.padRepeat & ctx_.padHeldEnable) {
        ctx_.padHeldEnable &= ~mask;
        ctx_.padPressedEnable &= ~mask;
        return setOut(out, kOutPadButton), true;
    }
    if (stickDirPulseConsume(b.stickDir)) return setOut(out, kOutStickDirection), true;
    for (int32_t k : {b.key1, b.key2}) {
        if (ButtonRecord* r = keyRecord(k); r && r->held && r->enabled) {
            r->enabled = 0;
            return setOut(out, kOutKeyOrMouse), true;
        }
    }
    for (int32_t m : {b.mouse1, b.mouse2}) {
        if (ButtonRecord* r = mouseRecord(m); r && r->held && r->enabled) {
            r->enabled = 0;
            return setOut(out, kOutKeyOrMouse), true;
        }
    }
    setOut(out, kOutNone);
    return false;
}

// 0x005bbdd0: consume every bound pad button, key and mouse button; the
// stick direction is not consumed (Sec1.11.2). Pad bits are cleared from
// both enable masks (the two masks the consume helpers write, Sec1.2).
void InputSystem::consume(int cba) {
    if (!cbaInRange(cba)) return;
    const ButtonBinding& b = buttons_[cba];
    const uint32_t mask = padMaskOf(b);
    ctx_.padHeldEnable &= ~mask;
    ctx_.padPressedEnable &= ~mask;
    for (int32_t k : {b.key1, b.key2})
        if (ButtonRecord* r = keyRecord(k)) r->enabled = 0;
    for (int32_t m : {b.mouse1, b.mouse2})
        if (ButtonRecord* r = mouseRecord(m)) r->enabled = 0;
}

bool InputSystem::allSourcesDisabled(int cba) const {
    if (!cbaInRange(cba)) return false;
    const ButtonBinding& b = buttons_[cba];
    // The CONFIRMED special case (Sec1.8.3 / Sec3 OPEN 3, purpose unknown).
    if (b.padA < 0 && b.padB < 0 && b.key1 == 0 && b.key2 != 0 && b.mouse1 == -1 && b.mouse2 != -1) return false;
    if (padMaskOf(b) & ctx_.padHeldEnable) return false;
    for (int32_t k : {b.key1, b.key2})
        if (const ButtonRecord* r = keyRecord(k); r && r->enabled) return false;
    for (int32_t m : {b.mouse1, b.mouse2})
        if (const ButtonRecord* r = mouseRecord(m); r && r->enabled) return false;
    return true;
}

// 0x005bd8b0 (current) / 0x005bdb60 (previous frame), Sec1.8.3.
float InputSystem::analogImpl(int cba, int* out, bool previous) const {
    if (!cbaInRange(cba)) return 0.0f;
    if (allSourcesDisabled(cba)) {
        setOut(out, kOutNone);
        return 0.0f;
    }
    const ButtonBinding& b = buttons_[cba];
    int o = kOutNone;
    float v = 0.0f;
    for (int32_t p : {b.padA, b.padB}) {
        if (p < 0 || p >= kNumPadButtons) continue;
        // Raw engine value, not thresholded, not gated by the enable masks.
        const float pv = previous ? device_->padButtonPrevValue(p) : device_->padButtonValue(p);
        v += pv;
        if (pv != 0.0f) o = kOutPadButton;
    }
    const float sv = previous ? stickDirPrevAnalog(b.stickDir) : stickDirAnalog(b.stickDir);
    v += sv;
    if (sv != 0.0f) o = kOutStickDirection;
    if (WithinZero(v)) {
        auto down = [previous](const ButtonRecord* r) {
            if (!r || !r->enabled) return false;
            return previous ? ((r->held && !r->pressed) || r->released) : (r->held != 0);
        };
        bool found = false;
        for (int32_t k : {b.key1, b.key2})
            if (!found && down(keyRecord(k))) found = true;
        for (int32_t m : {b.mouse1, b.mouse2})
            if (!found && down(mouseRecord(m))) found = true;
        if (found) {
            v += 1.0f;
            o = kOutKeyOrMouse;
        }
    }
    if (v > 1.0f) v = 1.0f;
    setOut(out, o);
    return v;
}

float InputSystem::analog(int cba, int* out) const { return analogImpl(cba, out, false); }
float InputSystem::previousAnalog(int cba, int* out) const { return analogImpl(cba, out, true); }

bool InputSystem::fellThrough(int cba, float threshold, int* out) const {
    if (!cbaInRange(cba)) return false;
    int prevOut = kOutNone;
    const float prev = previousAnalog(cba, &prevOut);
    const float cur = analog(cba, nullptr);
    setOut(out, prevOut);
    return !allSourcesDisabled(cba) && prev >= threshold && cur < threshold;
}

int32_t InputSystem::padButton(int cba, bool alternate) const {
    if (!cbaInRange(cba)) return -1;
    return alternate ? buttons_[cba].padB : buttons_[cba].padA;
}
int32_t InputSystem::key1(int cba) const { return buttons_.at(static_cast<size_t>(cba)).key1; }
int32_t InputSystem::key2(int cba) const { return buttons_.at(static_cast<size_t>(cba)).key2; }
int32_t InputSystem::mouse1(int cba) const { return buttons_.at(static_cast<size_t>(cba)).mouse1; }
int32_t InputSystem::mouse2(int cba) const { return buttons_.at(static_cast<size_t>(cba)).mouse2; }

// ---------------------------------------------------------------------------
// Sec1.9 - axis evaluators.
// ---------------------------------------------------------------------------
float InputSystem::stickAxisValue(int code, AxisSource src) const {
    // 0/1 -> stick 1 (right) x/y; 2/3 -> stick 0 (left) x/y.
    if (code < 0 || code > 3) return 0.0f;
    const StickRecord& s = ctx_.sticks[code < 2 ? 1 : 0];
    const bool yAxis = (code % 2) == 1;
    // CHOSEN for the raw evaluator 0x005bb980: the spec says only "same, but
    // from the raw sticks"; the analog block bytes are honoured on the raw
    // path too, matching the raw accessor 0x005badb0 (Sec1.12.1).
    if (yAxis ? s.blockAnalogY : s.blockAnalogX) return 0.0f;
    if (src == AxisSource::Raw) return yAxis ? s.rawY : s.rawX;
    const float v = yAxis ? s.y : s.x;
    return src == AxisSource::Curved ? responseCurve(v) : v;
}

float InputSystem::evaluateAxis(int caa, int* out, AxisSource src) const {
    const AxisBinding& a = axes_.at(static_cast<size_t>(caa));
    // Steps 1-2: primary stick axis; the secondary only when the primary is
    // exactly zero.
    float v = stickAxisValue(a.axis, src);
    if (v != 0.0f) {
        if (a.invert) v = -v;
    } else {
        v = stickAxisValue(a.altAxis, src);
        if (a.altInvert) v = -v;
    }
    int o = kOutNone;
    if (!WithinZero(v)) {
        o = kOutAxisGamepad;  // step 3: the gamepad has priority
    } else {
        // Step 4: held keys and mouse buttons, +1 positive / -1 negative.
        auto heldKey = [this](int32_t k) {
            const ButtonRecord* r = keyRecord(k);
            return r && r->held && r->enabled;
        };
        auto heldMouse = [this](int32_t m) {
            const ButtonRecord* r = mouseRecord(m);
            return r && r->held && r->enabled;
        };
        if (heldKey(a.keyPos)) v += 1.0f;
        if (heldKey(a.keyNeg)) v -= 1.0f;
        if (heldKey(a.altKeyPos)) v += 1.0f;
        if (heldKey(a.altKeyNeg)) v -= 1.0f;
        if (heldMouse(a.mousePos)) v += 1.0f;
        if (heldMouse(a.mouseNeg)) v -= 1.0f;
        if (heldMouse(a.altMousePos)) v += 1.0f;
        if (heldMouse(a.altMouseNeg)) v -= 1.0f;
        // Step 5: the mouse axis only when the key sum is ~0.
        if (WithinZero(v) && a.mouseAxis >= 0 && a.mouseAxis < kNumMouseAxes) {
            const MouseAxisRecord& m = ctx_.mouseAxes[a.mouseAxis];
            if (m.enabled) v += (src == AxisSource::Raw) ? m.valueCopy : m.value;
        }
        // Code 2 is assigned and always overwritten by 3 (Sec1.9.3).
        if (!WithinZero(v)) o = kOutAxisKbm;
    }
    setOut(out, o);
    if (src != AxisSource::Curved) v = std::clamp(v, -1.0f, 1.0f);  // step 6
    return v;
}

float InputSystem::axis(int caa, int* out) const { return evaluateAxis(caa, out, AxisSource::DeadZoned); }
float InputSystem::axisRaw(int caa, int* out) const { return evaluateAxis(caa, out, AxisSource::Raw); }
float InputSystem::axisCurved(int caa, int* out) const { return evaluateAxis(caa, out, AxisSource::Curved); }

bool InputSystem::halfQuery(int caa, bool positive, bool pressedEdge, int* out) const {
    if (caa < 0 || caa > kMaxCaa) return false;
    const AxisBinding& a = axes_[caa];
    auto hit = [pressedEdge](const ButtonRecord* r) {
        return r && r->enabled && (pressedEdge ? r->pressed : r->held);
    };
    const bool sat = positive ? (hit(keyRecord(a.keyPos)) || hit(keyRecord(a.altKeyPos)) || hit(mouseRecord(a.mousePos)) ||
                                 hit(mouseRecord(a.altMousePos)))
                              : (hit(keyRecord(a.keyNeg)) || hit(keyRecord(a.altKeyNeg)) || hit(mouseRecord(a.mouseNeg)) ||
                                 hit(mouseRecord(a.altMouseNeg)));
    setOut(out, sat ? kOutKeyOrMouse : kOutNone);
    return sat;
}

bool InputSystem::axisPositivePressed(int caa, int* out) const { return halfQuery(caa, true, true, out); }
bool InputSystem::axisNegativePressed(int caa, int* out) const { return halfQuery(caa, false, true, out); }
bool InputSystem::axisPositiveHeld(int caa, int* out) const { return halfQuery(caa, true, false, out); }
bool InputSystem::axisNegativeHeld(int caa, int* out) const { return halfQuery(caa, false, false, out); }

int32_t InputSystem::axisStickCode(int caa, bool secondary) const {
    const AxisBinding& a = axes_.at(static_cast<size_t>(caa));
    return secondary ? a.altAxis : a.axis;
}

namespace {
bool pairOut(int32_t p, int32_t n, int32_t* pos, int32_t* neg, int32_t none) {
    if (pos) *pos = p;
    if (neg) *neg = n;
    return p != none && n != none;
}
} // namespace
bool InputSystem::keyPair1(int caa, int32_t* pos, int32_t* neg) const {
    const AxisBinding& a = axes_.at(static_cast<size_t>(caa));
    return pairOut(a.keyPos, a.keyNeg, pos, neg, 0);
}
bool InputSystem::keyPair2(int caa, int32_t* pos, int32_t* neg) const {
    const AxisBinding& a = axes_.at(static_cast<size_t>(caa));
    return pairOut(a.altKeyPos, a.altKeyNeg, pos, neg, 0);
}
bool InputSystem::mousePair1(int caa, int32_t* pos, int32_t* neg) const {
    const AxisBinding& a = axes_.at(static_cast<size_t>(caa));
    return pairOut(a.mousePos, a.mouseNeg, pos, neg, -1);
}
bool InputSystem::mousePair2(int caa, int32_t* pos, int32_t* neg) const {
    const AxisBinding& a = axes_.at(static_cast<size_t>(caa));
    return pairOut(a.altMousePos, a.altMouseNeg, pos, neg, -1);
}

// ---------------------------------------------------------------------------
// Sec1.12.1 - raw access.
// ---------------------------------------------------------------------------
void InputSystem::rawStick(int stick, float& x, float& y) const {
    const StickRecord& s = ctx_.sticks.at(static_cast<size_t>(stick));
    x = s.blockAnalogX ? 0.0f : s.rawX;
    y = s.blockAnalogY ? 0.0f : s.rawY;
}

void InputSystem::ScaleToUnit(float x, float y, float& ox, float& oy) { clampToUnit(x, y, ox, oy); }

// ---------------------------------------------------------------------------
// Sec1.7 - binding application.
// ---------------------------------------------------------------------------
void InputSystem::rebuildBindings() {
    // Step 2: initial values (the struct defaults are Sec1.6's column).
    buttons_.fill(ButtonBinding{});
    axes_.fill(AxisBinding{});
    // Step 3: option byte 0x01411b00 - bits 1-3 cleared, bit 0 set.
    optionFlagBits0to3 = static_cast<uint8_t>(0x01);
    // Step 4: the four scheme kinds, in this order, -1 mapped to 0.
    struct Step {
        SchemeKind kind;
        const char* name;
    };
    for (const Step& s : {Step{SchemeKind::General, "General Controls"}, Step{SchemeKind::Satellite, "Satellite Controls"},
                          Step{SchemeKind::OnFoot, "A"}, Step{SchemeKind::Driving, "A"}}) {
        int32_t idx = FindSchemeIndex(schemeArrays[static_cast<int>(s.kind)], s.name);
        if (idx == -1) idx = 0;
        applySchemeImpl(s.kind, idx, /*currentOpenIsNoOp=*/true);
    }
    // Step 5: every binding set's button half, then every set's axis half.
    for (const BindingSetRecords& set : bindingSets) applyBindingSetButtons(set);
    for (const BindingSetRecords& set : bindingSets) applyBindingSetAxes(set);
    // Step 6.
    rebuildBoundKeyTable();
    // Step 7 (cursor release + capture update) is platform-only.
}

void InputSystem::applyScheme(SchemeKind kind, int32_t index) { applySchemeImpl(kind, index, false); }

void InputSystem::applySchemeImpl(SchemeKind kind, int32_t index, bool currentOpenIsNoOp) {
    const int k = static_cast<int>(kind);
    const std::vector<SchemeSlot>& arr = schemeArrays[k];
    // CHOSEN: the clamp to [0, count-1] is unspecified for an empty array;
    // with nothing to apply, nothing is applied and no index is stored.
    if (arr.empty()) return;
    const int32_t newIndex = std::clamp<int32_t>(index, 0, static_cast<int32_t>(arr.size()) - 1);

    // Un-apply the currently applied slot (Sec1.7.2 step 2).
    if (currentSchemeIndex[k].has_value()) {
        const SchemeSlot& cur = arr.at(static_cast<size_t>(*currentSchemeIndex[k]));
        const size_t nc = std::min(cur.controls.size(), kSchemeRecordCap);
        for (size_t i = 0; i < nc; ++i) {
            const int32_t act = cur.controls[i].action;
            if (act < 0 || act >= kNumCba) continue;  // CHOSEN: an out-of-table action is skipped, never written
            ButtonBinding& b = buttons_[act];
            b.padA = b.padB = b.stickDir = -1;
            b.labelHandle = 0;
        }
        const size_t na = std::min(cur.axes.size(), kSchemeRecordCap);
        for (size_t i = 0; i < na; ++i) {
            const int32_t act = cur.axes[i].action;
            if (act < 0 || act >= kNumCaa) continue;
            AxisBinding& a = axes_[act];
            a.axis = a.altAxis = -1;
            a.invert = a.altInvert = 0;
        }
    } else if (!currentOpenIsNoOp) {
        throw sr3luahost::OpenStateError("current control-scheme index (0x01411b04/0x01411b08/0x01411b0c/0x01411b10)",
                                         "spec-player-runtime.md Sec1.7.2");
    }
    // else: inside a rebuild the tables were just reset to their initial
    // values, and un-apply writes only those same initial values, so
    // un-applying ANY in-range slot is a no-op - the OPEN static index does
    // not matter unless it is out of range, which the spec does not rule
    // out (flagged in the delivery report).

    currentSchemeIndex[k] = newIndex;  // step 3

    // Step 4: apply the new slot.
    const SchemeSlot& slot = arr[static_cast<size_t>(newIndex)];
    const size_t nc = std::min(slot.controls.size(), kSchemeRecordCap);
    for (size_t i = 0; i < nc; ++i) {
        const SchemeControlRecord& c = slot.controls[i];
        if (c.action < 0 || c.action >= kNumCba) continue;
        ButtonBinding& b = buttons_[c.action];
        b.padA = c.button;
        b.padB = c.altButton;
        b.stickDir = c.stickDirection;
        b.labelHandle = c.labelHandle;
    }
    const size_t na = std::min(slot.axes.size(), kSchemeRecordCap);
    for (size_t i = 0; i < na; ++i) {
        const SchemeAxisRecord& x = slot.axes[i];
        if (x.action < 0 || x.action >= kNumCaa) continue;
        AxisBinding& a = axes_[x.action];
        a.axis = x.axis;
        a.invert = x.inverted;
        a.altAxis = x.altAxis;
        a.altInvert = x.altInverted;
    }
    // 0x00846c60 notification (HYPOTHESIS: control legends) - not modelled.
}

void InputSystem::applyBindingSetButtons(const BindingSetRecords& set) {
    for (const BindingSetControlRecord& c : set.controls) {
        if (c.action == 0 && c.key1 == 0 && c.key2 == 0 && c.mouse1 == 0 && c.mouse2 == 0) continue;  // all 20 bytes zero
        if (!validKey(c.key1) || !validKey(c.key2)) continue;
        if (!validMouse(c.mouse1) || !validMouse(c.mouse2)) continue;
        if (c.action < 0 || c.action >= kNumCba) continue;  // CHOSEN, as in applySchemeImpl
        ButtonBinding& b = buttons_[c.action];
        b.key1 = c.key1;
        b.key2 = c.key2;
        b.mouse1 = c.mouse1;
        b.mouse2 = c.mouse2;
    }
}

void InputSystem::applyBindingSetAxes(const BindingSetRecords& set) {
    for (const BindingSetAxisRecord& x : set.axes) {
        if (!validKey(x.keyPos) || !validKey(x.keyNeg) || !validKey(x.altKeyPos) || !validKey(x.altKeyNeg)) continue;
        if (!validMouse(x.mousePos) || !validMouse(x.mouseNeg) || !validMouse(x.altMousePos) || !validMouse(x.altMouseNeg))
            continue;
        if (!(x.mouseAxis == -1 || (x.mouseAxis >= 0 && x.mouseAxis < kNumMouseAxes))) continue;
        if (x.action < 0 || x.action >= kNumCaa) continue;  // CHOSEN, as above
        AxisBinding& a = axes_[x.action];
        a.keyPos = x.keyPos;
        a.keyNeg = x.keyNeg;
        a.altKeyPos = x.altKeyPos;
        a.altKeyNeg = x.altKeyNeg;
        a.mousePos = x.mousePos;
        a.mouseNeg = x.mouseNeg;
        a.altMousePos = x.altMousePos;
        a.altMouseNeg = x.altMouseNeg;
        a.mouseAxis = x.mouseAxis;
    }
}

void InputSystem::rebuildBoundKeyTable() {
    ctx_.keyIsBound.fill(0);
    auto mark = [this](int32_t k) {
        if (k > 0 && k < kNumKeys) ctx_.keyIsBound[k] = 1;
    };
    for (const ButtonBinding& b : buttons_) {
        mark(b.key1);
        mark(b.key2);
    }
    for (const AxisBinding& a : axes_) {
        mark(a.keyPos);
        mark(a.keyNeg);
        mark(a.altKeyPos);
        mark(a.altKeyNeg);
    }
}

} // namespace sr3player
