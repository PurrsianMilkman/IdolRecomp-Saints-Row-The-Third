#pragma once

// sr3player/input.h - player runtime unit U0: the input query contract.
//
// Source: spec-player-runtime.md Sec1 (SPEC TEAM, 2026-10-03), the ONLY
// source for every behaviour below. Every address/offset cited in a comment
// is copied from that spec's own prose so a reader can cross-reference the
// section; nothing here was derived from the executable.
//
// WHAT THIS IS. The spec's own Sec1.13 states the seam: a port fills the
// control context (0x01411b18, Sec1.2) once per frame from its own platform
// layer, keeps the two runtime binding tables (Sec1.6/Sec1.7), and the ~45
// query functions (Sec1.8-Sec1.10) are "pure functions of that state and can
// be ported as specified". So:
//   * ControlContext        - the Sec1.2 context, field for field.
//   * DeviceSource          - this project's OWN injectable interface
//                             standing in for the engine library's device
//                             block (Sec1.3). The latch reads ONLY through
//                             it. NullDeviceSource is the headless default.
//   * InputSystem           - the latch gate (Sec1.1), the latch (Sec1.4),
//                             the device-family tracker (Sec1.5), the
//                             binding tables + application (Sec1.6/Sec1.7),
//                             every query (Sec1.8-Sec1.10, Sec1.12.1) and
//                             the consumption model (Sec1.11).
//
// HEADLESS DEFAULT (Sec1.1, CONFIRMED). This host has no window. The spec's
// own rule for "window inactive or minimised" is: on the first such frame
// run the full clear 0x005bc830 and set the frozen byte 0x01411b03; every
// later inactive frame leaves the context frozen in that cleared state, so
// every query reads "nothing". NullDeviceSource reports "not active", so a
// default InputSystem lands in exactly that spec-given state - nothing is
// invented for the no-device case.
//
// SCOPE CUTS (each also listed in the delivery report):
//   * Binding tables are populated from ALREADY-RESOLVED INTEGER records
//     (SchemeSlot / BindingSetControlRecord / BindingSetAxisRecord below),
//     NOT from the parsed .xtbl structures in sr3tables_ui_controls. That
//     library keeps Action/Key fields as raw text because the CBA (166),
//     CAA (34) and key-name (80) vocabularies are NOT enumerated in
//     spec-tables-ui-controls.md Sec4.1-Sec4.3 (only a handful of values are
//     published; per-letter scan codes are OPEN there), and the Scheme_Type
//     bucket of each control_schemes.xtbl row is not modelled by that
//     reader either. Resolving text -> index here would mean inventing
//     those values. The full Sec1.7 application MACHINERY (initial values,
//     scheme layering order, un-apply-on-switch, clamp, KB/M validation
//     rules, last-set-wins, bound-key table) IS implemented over the
//     integer records, so real data drops in once Team A publishes the
//     vocabularies.
//   * Not implemented (no confirmed data, or HYPOTHESIS-level only): the
//     pad-descriptor byte queries 0x005bc200/0x005bc230 (descriptor table
//     0x01122008 content not given), the display-string queries
//     0x005baf30/0x005baef0, the option/look-invert bits (Sec1.12.2,
//     HYPOTHESIS), the mouse-sensitivity clamping setters (clamp constants
//     0x012a2ee0/0x01115d6c/0x012a2e80 not given), cursor capture (Sec1.12.3,
//     platform), the stick sector tracker 0x005c2700 (Sec1.12.4, "not part
//     of the contract"), and the engine-library internals below the device
//     block (dead-zone/threshold/repeat VALUES are spec OPEN 1).

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sr3player {

// --- Sizes (Sec1.2, Sec1.6) ------------------------------------------------
inline constexpr int kNumCba = 164;          // button records, CBA 0..163 (0xa3)
inline constexpr int kMaxCba = 0xa3;         // every button query rejects cba > 0xa3 (Sec1.8.1)
inline constexpr int kNumCaa = 34;           // axis records, CAA 0..33 (0x21)
inline constexpr int kMaxCaa = 0x21;         // only the half queries range-check (Sec1.9.1)
inline constexpr int kNumKeys = 256;         // Set-1 scan codes
inline constexpr int kNumMouseButtons = 7;   // MOUSE LEFT..WHEEL DOWN
inline constexpr int kNumMouseAxes = 2;      // MOUSE X, MOUSE Y
inline constexpr int kNumPadButtons = 16;    // A=0 .. RS=15

// Vocabulary values used by the contract itself (all CONFIRMED in
// spec-player-runtime.md / spec-tables-ui-controls.md Sec4.4-Sec4.8).
inline constexpr int kMouseMiddle = 1;       // MOUSE MIDDLE (Sec1.2)
inline constexpr int kWheelUp = 5;           // WHEEL UP
inline constexpr int kWheelDown = 6;         // WHEEL DOWN
inline constexpr int kPadLT = 6;             // LT - trigger hysteresis (Sec1.4 step 2)
inline constexpr int kPadRT = 7;             // RT
inline constexpr int kCaaCameraRotate = 0;   // CAA_CAMERA_ROTATE (spec-tables-ui-controls.md Sec4.2)
inline constexpr int kCaaCameraElevate = 1;  // CAA_CAMERA_ELEVATE

// Out codes (Sec1.8.2, Sec1.9.3).
inline constexpr int kOutNone = -1;
inline constexpr int kOutPadButton = 0;      // button queries
inline constexpr int kOutStickDirection = 1; // button queries
inline constexpr int kOutKeyOrMouse = 2;     // button queries
inline constexpr int kOutAxisGamepad = 1;    // axis evaluators
inline constexpr int kOutAxisKbm = 3;        // axis evaluators (code 2 is dead, Sec1.9.3)

// The 1e-4 "is zero" tolerance (Sec1.5, Sec1.9.2). CHOSEN boundary: "within
// 1e-4 of 0" is read as |v| <= 1e-4; the spec does not say which side an
// exactly-1e-4 value falls on.
inline constexpr float kZeroTolerance = 1e-4f;
inline bool WithinZero(float v) { return v <= kZeroTolerance && v >= -kZeroTolerance; }

// --- The control context 0x01411b18 (Sec1.2) -------------------------------

// Stick record, 0x28 bytes (stick 0 = left at +0x00, stick 1 = right at +0x28).
struct StickRecord {
    float x = 0, y = 0;            // +0x00/+0x04 dead-zoned, [-1,1], +y up
    float prevX = 0, prevY = 0;    // +0x08/+0x0c previous frame's +0x00/+0x04
    float rawX = 0, rawY = 0;      // +0x10/+0x14 raw (invert/disable applied, no dead zone)
    // +0x18/+0x1c digital pulses. The spec gives the values (-1/+1/0) but
    // not the storage type; int32 is this project's choice.
    int32_t pulseX = 0;            // -1 "left" pulse, +1 "right"
    int32_t pulseY = 0;            // -1 "up" pulse, +1 "down" (screen-style sign)
    uint8_t blockLeft = 0;         // +0x20 x-negative
    uint8_t blockRight = 0;        // +0x21 x-positive
    uint8_t blockUp = 0;           // +0x22 y-positive
    uint8_t blockDown = 0;         // +0x23 y-negative
    uint8_t blockAnalogX = 0;      // +0x24
    uint8_t blockAnalogY = 0;      // +0x25
};

// Keyboard / mouse-button record, 8 bytes.
struct ButtonRecord {
    uint8_t enabled = 0;   // +0 enabled this frame (re-armed by the latch, cleared by consume/clear)
    uint8_t held = 0;      // +1 held (level)
    uint8_t pressed = 0;   // +2 went down this frame
    uint8_t released = 0;  // +3 went up this frame
    uint32_t downTimeMs = 0; // +4 time of the last down-edge (write-only in the real build, Sec1.2)
};

// Mouse-axis record, 0xc bytes (MOUSE X = 0 at +0x9dc, MOUSE Y = 1 at +0x9e8).
struct MouseAxisRecord {
    float value = 0;       // +0 (read by 0x005bb530)
    float valueCopy = 0;   // +4 (read by the raw evaluator 0x005bb980)
    uint8_t enabled = 0;   // +8
};

// CHOSEN: value-initialised to all zeros. That is exactly the state the
// full clear 0x005bc830 produces (Sec1.11.3) on the first inactive frame,
// which is the only state a headless host can ever reach; the real
// pre-first-latch contents are not given by the spec.
struct ControlContext {
    std::array<StickRecord, 2> sticks{};               // +0x000 / +0x028
    uint32_t padHeld = 0;                              // +0x050
    uint32_t padRepeat = 0;                            // +0x054
    uint32_t padHeldEnable = 0;                        // +0x058
    uint32_t padPressed = 0;                           // +0x05c
    uint32_t padPressedEnable = 0;                     // +0x060
    std::array<uint32_t, kNumPadButtons> padDownTimeMs{}; // +0x064 (write-only in the real build)
    std::array<ButtonRecord, kNumKeys> keys{};         // +0x0a4
    std::array<uint8_t, kNumKeys> keyIsBound{};        // +0x8a4 (no reader found, Sec3 OPEN 4)
    std::array<ButtonRecord, kNumMouseButtons> mouseButtons{}; // +0x9a4
    std::array<MouseAxisRecord, kNumMouseAxes> mouseAxes{};    // +0x9dc
    uint8_t kbmActivity = 0;                           // +0x9f4
    uint8_t gamepadActive = 0;                         // +0x9f5 (absolute 0x0141250d)
    uint8_t cameraFromKbm = 0;                         // +0x9f6 (absolute 0x0141250e)
};

// --- The device-side contract (Sec1.3) --------------------------------------
//
// This project's OWN abstraction of what the latch reads from the engine
// library. Each method names the engine accessor whose SEMANTICS it must
// reproduce; a real platform layer implements them from OS devices, a test
// implements them from synthetic values. The pad methods all refer to the
// active pad (0x012e6ec8, -1 read as pad 0) - which pad that is, is the
// source's own concern.
class DeviceSource {
public:
    virtual ~DeviceSource() = default;
    // Sec1.1 gate: app-active byte 0x02a076e8 set AND IsIconic(main window) false.
    virtual bool appActiveAndNotIconic() const = 0;
    // 0x00dd1620: engine pad count 0x02a390e4 non-zero.
    virtual bool padRegistered() const = 0;
    // 0x00db8760: connected, value > threshold, not suppressed.
    virtual bool padButtonDown(int button) const = 0;
    virtual bool padButtonRepeatPulse(int button) const = 0;  // record +0x1d
    virtual bool padButtonDownEdge(int button) const = 0;     // record +0x1c
    virtual float padButtonValue(int button) const = 0;       // 0x00db88a0 (raw analog, not thresholded)
    virtual float padButtonPrevValue(int button) const = 0;   // 0x00db8870 (record +0x04)
    virtual float padButtonThreshold(int button) const = 0;   // 0x00db8970
    virtual void setPadButtonThreshold(int button, float t) = 0; // 0x00db8940 (trigger hysteresis)
    // Dead-zoned stick (0x00db8af0 semantics, see ApplyDeadZone) and raw stick (0x00db86a0).
    virtual void padStickDeadZoned(int stick, float& x, float& y) const = 0;
    virtual void padStickRaw(int stick, float& x, float& y) const = 0;
    // Direction pulse: dir 0 = y>0.5, 1 = -y>0.5, 2 = -x>0.5, 3 = x>0.5 (Sec1.3.3).
    virtual bool padStickDirectionPulse(int stick, int dir) const = 0;
    // Keyboard accessors 0x00dd0260/0x00dd02a0/0x00dd02e0 (Sec1.3.1, Sec1.2).
    virtual bool keyHeld(int scan) const = 0;
    virtual bool keyPressed(int scan) const = 0;
    virtual bool keyReleased(int scan) const = 0;
    // Mouse accessors 0x00dcfea0/0x00dcfed0/0x00dcff00 (Sec1.3.2).
    virtual bool mouseHeld(int button) const = 0;
    virtual bool mousePressed(int button) const = 0;
    virtual bool mouseReleased(int button) const = 0;
    // 0x00dcfcf0: per-frame cursor deltas in counts (X, Y; the wheel output is discarded).
    virtual void mouseDeltas(float& dx, float& dy) const = 0;
    // Engine time in ms (0x029f4db0).
    virtual uint32_t timeMs() const = 0;
    // Full clear's engine keyboard (0x00dd0190) and mouse (0x00dcfe20) resets (Sec1.11.3).
    virtual void resetKeyboardAndMouse() {}
};

// The headless default: no window, so the Sec1.1 gate is always false and
// no other method is ever reached by the latch. The remaining methods
// answer "nothing" for completeness (the analog query reads the engine pad
// value directly, Sec1.8.3, but only after 0x005bbff0's all-disabled check,
// which is always true in the frozen-cleared state).
class NullDeviceSource : public DeviceSource {
public:
    bool appActiveAndNotIconic() const override { return false; }
    bool padRegistered() const override { return false; }
    bool padButtonDown(int) const override { return false; }
    bool padButtonRepeatPulse(int) const override { return false; }
    bool padButtonDownEdge(int) const override { return false; }
    float padButtonValue(int) const override { return 0.0f; }
    float padButtonPrevValue(int) const override { return 0.0f; }
    float padButtonThreshold(int) const override { return 0.0f; }
    void setPadButtonThreshold(int, float) override {}
    void padStickDeadZoned(int, float& x, float& y) const override { x = y = 0.0f; }
    void padStickRaw(int, float& x, float& y) const override { x = y = 0.0f; }
    bool padStickDirectionPulse(int, int) const override { return false; }
    bool keyHeld(int) const override { return false; }
    bool keyPressed(int) const override { return false; }
    bool keyReleased(int) const override { return false; }
    bool mouseHeld(int) const override { return false; }
    bool mousePressed(int) const override { return false; }
    bool mouseReleased(int) const override { return false; }
    void mouseDeltas(float& dx, float& dy) const override { dx = dy = 0.0f; }
    uint32_t timeMs() const override { return 0; }
};

// A plain-data synthetic device: every value is a public field a test (or a
// future replay tool) sets directly. "Synthetic" = injected test input, not
// recovered data.
class SyntheticDeviceSource : public DeviceSource {
public:
    bool active = true;
    bool padPresent = true;
    std::array<bool, kNumPadButtons> padDown{};
    std::array<bool, kNumPadButtons> padRepeatPulse{};
    std::array<bool, kNumPadButtons> padDownEdge{};
    std::array<float, kNumPadButtons> padValue{};
    std::array<float, kNumPadButtons> padPrevValue{};
    std::array<float, kNumPadButtons> padThreshold{};
    std::array<std::array<float, 2>, 2> stickDeadZoned{}; // [stick][x,y]
    std::array<std::array<float, 2>, 2> stickRaw{};
    std::array<std::array<bool, 4>, 2> stickPulse{};      // [stick][dir]
    std::array<bool, kNumKeys> keyDown{}, keyDownEdge{}, keyUpEdge{};
    std::array<bool, kNumMouseButtons> mouseDown{}, mouseDownEdge{}, mouseUpEdge{};
    float mouseDx = 0, mouseDy = 0;
    uint32_t nowMs = 0;
    int resetCount = 0;

    bool appActiveAndNotIconic() const override { return active; }
    bool padRegistered() const override { return padPresent; }
    bool padButtonDown(int b) const override { return padDown[b]; }
    bool padButtonRepeatPulse(int b) const override { return padRepeatPulse[b]; }
    bool padButtonDownEdge(int b) const override { return padDownEdge[b]; }
    float padButtonValue(int b) const override { return padValue[b]; }
    float padButtonPrevValue(int b) const override { return padPrevValue[b]; }
    float padButtonThreshold(int b) const override { return padThreshold[b]; }
    void setPadButtonThreshold(int b, float t) override { padThreshold[b] = t; }
    void padStickDeadZoned(int s, float& x, float& y) const override { x = stickDeadZoned[s][0]; y = stickDeadZoned[s][1]; }
    void padStickRaw(int s, float& x, float& y) const override { x = stickRaw[s][0]; y = stickRaw[s][1]; }
    bool padStickDirectionPulse(int s, int d) const override { return stickPulse[s][d]; }
    bool keyHeld(int s) const override { return keyDown[s]; }
    bool keyPressed(int s) const override { return keyDownEdge[s]; }
    bool keyReleased(int s) const override { return keyUpEdge[s]; }
    bool mouseHeld(int b) const override { return mouseDown[b]; }
    bool mousePressed(int b) const override { return mouseDownEdge[b]; }
    bool mouseReleased(int b) const override { return mouseUpEdge[b]; }
    void mouseDeltas(float& dx, float& dy) const override { dx = mouseDx; dy = mouseDy; }
    uint32_t timeMs() const override { return nowMs; }
    void resetKeyboardAndMouse() override { ++resetCount; }
};

// The engine's per-axis dead-zone transform 0x00db8af0 (Sec1.3.3,
// CONFIRMED): |v| < dz -> 0, else (v - sign(v)*dz) / (1 - dz); then
// `disable` forces 0 and `invert` negates. The dead-zone VALUE itself is
// spec OPEN 1, so the caller must supply it - exposed for a platform layer
// building a DeviceSource, not used by the latch.
float ApplyDeadZone(float v, float deadZone, bool invert = false, bool disable = false);

// --- The runtime binding tables (Sec1.6) ------------------------------------

// Button record, 164 x 0x20 bytes at 0x0140ff98. Defaults = "initial value" column.
struct ButtonBinding {
    int32_t padA = -1;         // +0x00 pad half
    int32_t padB = -1;         // +0x04 pad half
    int32_t stickDir = -1;     // +0x08 pad half (Sec1.10 code)
    int32_t key1 = 0;          // +0x0c KB/M half (scan, 0 = none)
    int32_t key2 = 0;          // +0x10
    int32_t mouse1 = -1;       // +0x14
    int32_t mouse2 = -1;       // +0x18
    uint32_t labelHandle = 0;  // +0x1c pad half (interned Action_Name)
};

// Axis record, 34 x 0x34 bytes at 0x01411418.
struct AxisBinding {
    int32_t axis = -1;         // +0x00 primary stick-axis code (RIGHT_X=0, RIGHT_Y=1, LEFT_X=2, LEFT_Y=3)
    uint8_t invert = 0;        // +0x04
    int32_t altAxis = -1;      // +0x08
    uint8_t altInvert = 0;     // +0x0c (always equals +0x04 from real data, Sec1.6)
    int32_t keyPos = 0, keyNeg = 0;          // +0x10 / +0x14
    int32_t altKeyPos = 0, altKeyNeg = 0;    // +0x18 / +0x1c
    int32_t mousePos = -1, mouseNeg = -1;    // +0x20 / +0x24
    int32_t altMousePos = -1, altMouseNeg = -1; // +0x28 / +0x2c
    int32_t mouseAxis = -1;    // +0x30 (MOUSE X=0, MOUSE Y=1)
};

// --- Integer-resolved source records for Sec1.7 (see the scope-cut note) ---

// control_schemes.xtbl <Control>, 20 bytes (spec-tables-ui-controls.md Sec3.1).
struct SchemeControlRecord {
    int32_t action = -1;        // CBA value
    int32_t button = -1;
    int32_t altButton = -1;
    int32_t stickDirection = -1;
    uint32_t labelHandle = 0;
};
// control_schemes.xtbl <Axes><Axis>, 20 bytes (Sec3.2).
struct SchemeAxisRecord {
    int32_t action = -1;        // CAA value
    int32_t axis = -1;
    uint8_t inverted = 0;
    int32_t altAxis = -1;
    uint8_t altInverted = 0;    // the loader's second read of Inverted
};
// One 0xFC0-byte scheme slot.
struct SchemeSlot {
    std::string name;
    std::vector<SchemeControlRecord> controls;
    std::vector<SchemeAxisRecord> axes;
};
// control_binding_sets.xtbl <Control>, 20 bytes (Sec2.2). An absent key is
// stored as -1 by the loader, an authored UNBOUND as 0 (Sec1.7.3).
struct BindingSetControlRecord {
    int32_t action = 0, key1 = 0, key2 = 0, mouse1 = 0, mouse2 = 0;
};
// control_binding_sets.xtbl <Axis>, 40 bytes (Sec2.3).
struct BindingSetAxisRecord {
    int32_t action = 0;
    int32_t keyPos = 0, keyNeg = 0, altKeyPos = 0, altKeyNeg = 0;
    int32_t mousePos = -1, mouseNeg = -1, altMousePos = -1, altMouseNeg = -1;
    int32_t mouseAxis = -1;
};
struct BindingSetRecords {
    std::vector<BindingSetControlRecord> controls;
    std::vector<BindingSetAxisRecord> axes;
};

// The four gamepad scheme kinds and the order the rebuild applies them in
// (Sec1.7.1 step 4).
enum class SchemeKind { General = 0, Satellite = 1, OnFoot = 2, Driving = 3 };

// The scheme name lookup 0x005bd580/0x005bd600/0x005bd680/0x005bd700
// (Sec1.7.1): first case-insensitive exact match, else first case-insensitive
// substring match (0x00da7780), else -1.
int32_t FindSchemeIndex(const std::vector<SchemeSlot>& slots, const std::string& needle);

// --- InputSystem -------------------------------------------------------------

class InputSystem {
public:
    // `device` must outlive this object. Defaults to an internal
    // NullDeviceSource (headless) when null.
    explicit InputSystem(DeviceSource* device = nullptr);
    // Holds a pointer to its own embedded NullDeviceSource by default.
    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    void setDevice(DeviceSource* device);
    DeviceSource& device() { return *device_; }

    // Current control state getter 0x005aa630, read by the latch's step 5.
    // Unset = state is never 5 (nothing in the shipped executable pushes
    // state 5, Sec1.4 step 5 / Sec2.6). Wired to ControlStateMachine by the
    // owner.
    void setControlStateGetter(std::function<int32_t()> getter) { controlStateGetter_ = std::move(getter); }
    // The byte 0x0140e974 the step-5 skip also tests. Meaning and value are
    // not given by the spec: OPEN (read - and thrown on - only when the
    // current control state is 5, which is unreachable in shipped play).
    std::optional<bool> state5LatchSkipByte;

    // Sec1.1: the latch gate 0x005be880 (then the latch 0x005bcde0 when active).
    void latchFrame();
    // Sec1.4: the latch itself (exposed for tests; latchFrame is the real entry).
    void latch();
    // Sec1.5: the device-family tracker 0x005bc9d0 (run by the latch, step 8).
    void trackDeviceFamily();
    // Sec1.11.1: "clear latched" 0x005bbf60.
    void clearLatched();
    // Sec1.11.3: full clear 0x005bc830.
    void fullClear();

    const ControlContext& context() const { return ctx_; }
    ControlContext& mutableContext() { return ctx_; }   // tests / platform layer
    bool frozen() const { return frozen_ != 0; }         // 0x01411b03

    // Mouse globals (Sec1.2 adjacent globals, static values CONFIRMED).
    float mouseSensitivityX = 0.24f;     // 0x012e6f04
    float mouseSensitivityY = 0.24f;     // 0x012e6f08
    float mouseGlobalScale = 0.01f;      // 0x012e6f0c (no writer found)
    uint8_t middleDisablesWheel = 1;     // 0x012e6f10

    // --- Binding tables (Sec1.6) and application (Sec1.7) ---
    std::array<ButtonBinding, kNumCba>& buttonBindings() { return buttons_; }
    std::array<AxisBinding, kNumCaa>& axisBindings() { return axes_; }
    const std::array<ButtonBinding, kNumCba>& buttonBindings() const { return buttons_; }
    const std::array<AxisBinding, kNumCaa>& axisBindings() const { return axes_; }

    // The four scheme arrays and their current-index globals
    // (0x01411b04 General / 0x01411b0c Satellite / 0x01411b08 On_Foot /
    // 0x01411b10 Driving). The current-index static values are not given by
    // the spec: nullopt = OPEN until the first apply stores one.
    std::array<std::vector<SchemeSlot>, 4> schemeArrays;
    std::array<std::optional<int32_t>, 4> currentSchemeIndex;
    // Binding-set slots in file order (0x0142a010 / 0x01436ea0).
    std::vector<BindingSetRecords> bindingSets;

    // Sec1.7.1 steps 2-6 (step 1, reloading the files, is the caller's
    // job: fill schemeArrays/bindingSets first; step 7 is cursor capture,
    // platform-only). Option byte 0x01411b00: bits 1-3 cleared, bit 0 set.
    void rebuildBindings();
    // Sec1.7.2: one scheme applier (also called standalone by options UI in
    // the real game). Throws OpenStateError if the kind's current index is
    // still OPEN (only a rebuild can run with it OPEN - see the .cpp).
    void applyScheme(SchemeKind kind, int32_t index);
    // Sec1.7.3 / Sec1.7.4: one binding set's KB/M halves.
    void applyBindingSetButtons(const BindingSetRecords& set);
    void applyBindingSetAxes(const BindingSetRecords& set);
    // Sec1.7.5: bound-key table 0x005bab40.
    void rebuildBoundKeyTable();
    // 0x01411b00 bits 0-3 as written by the rebuild (other bits OPEN).
    std::optional<uint8_t> optionFlagBits0to3;

    // --- Button-action queries (Sec1.8). `out` may be null. ---
    bool pressed(int cba, int* out = nullptr) const;            // 0x005bae10
    bool held(int cba, int* out = nullptr) const;               // 0x005bd7b0
    bool pressedConsume(int cba, int* out = nullptr);           // 0x005bdfe0
    bool navigationPulseConsume(int cba, int* out = nullptr);   // 0x005bdef0
    void consume(int cba);                                       // 0x005bbdd0
    bool allSourcesDisabled(int cba) const;                      // 0x005bbff0
    float analog(int cba, int* out = nullptr) const;            // 0x005bd8b0
    float previousAnalog(int cba, int* out = nullptr) const;    // 0x005bdb60
    bool fellThrough(int cba, float threshold, int* out = nullptr) const; // 0x005bde80
    int32_t padButton(int cba, bool alternate) const;           // 0x005bc1a0
    // 0x005bc4d0/0x005bc4e0/0x005bc4f0/0x005bc500: "no range check" in the
    // real build (an out-of-range index reads adjacent memory). CHOSEN:
    // throw std::out_of_range instead of fabricating that memory.
    int32_t key1(int cba) const;
    int32_t key2(int cba) const;
    int32_t mouse1(int cba) const;
    int32_t mouse2(int cba) const;

    // --- Axis-action queries (Sec1.9). The three evaluators do not range
    // check in the real build; CHOSEN: std::out_of_range here (as above). ---
    float axis(int caa, int* out = nullptr) const;              // 0x005bb530
    float axisRaw(int caa, int* out = nullptr) const;           // 0x005bb980
    float axisCurved(int caa, int* out = nullptr) const;        // 0x005bb140 (unclamped)
    bool axisPositivePressed(int caa, int* out = nullptr) const; // 0x005bc5f0
    bool axisNegativePressed(int caa, int* out = nullptr) const; // 0x005bc680
    bool axisPositiveHeld(int caa, int* out = nullptr) const;    // 0x005bea10
    bool axisNegativeHeld(int caa, int* out = nullptr) const;    // 0x005beab0
    int32_t axisStickCode(int caa, bool secondary) const;        // 0x005bc1d0
    // 0x005bc510/0x005bc540/0x005bc570/0x005bc5a0: "both bound" pairs.
    bool keyPair1(int caa, int32_t* pos, int32_t* neg) const;
    bool keyPair2(int caa, int32_t* pos, int32_t* neg) const;
    bool mousePair1(int caa, int32_t* pos, int32_t* neg) const;
    bool mousePair2(int caa, int32_t* pos, int32_t* neg) const;

    // --- Stick-direction primitives (Sec1.10), code d in 0..7 ---
    bool stickDirHeld(int d) const;
    bool stickDirPressed(int d) const;
    bool stickDirPressedConsume(int d);
    bool stickDirPulseConsume(int d);
    float stickDirAnalog(int d) const;
    float stickDirPrevAnalog(int d) const;

    // --- Raw access (Sec1.12.1) ---
    void rawStick(int stick, float& x, float& y) const;          // 0x005badb0
    static void ScaleToUnit(float x, float y, float& ox, float& oy); // 0x005bc2c0

    // Getters 0x005bc5d0 / 0x005bc5e0 (Sec1.5).
    bool gamepadIsActiveInput() const { return ctx_.gamepadActive != 0; }
    bool cameraDrivenByKbm() const { return ctx_.cameraFromKbm != 0; }

private:
    enum class AxisSource { DeadZoned, Raw, Curved };
    float evaluateAxis(int caa, int* out, AxisSource src) const;
    float stickAxisValue(int code, AxisSource src) const;
    bool halfQuery(int caa, bool positive, bool pressedEdge, int* out) const;
    void applySchemeImpl(SchemeKind kind, int32_t index, bool currentOpenIsNoOp);
    float analogImpl(int cba, int* out, bool previous) const;
    const ButtonRecord* keyRecord(int scan) const;
    ButtonRecord* keyRecord(int scan);
    const ButtonRecord* mouseRecord(int button) const;
    ButtonRecord* mouseRecord(int button);

    NullDeviceSource nullDevice_;
    DeviceSource* device_;
    ControlContext ctx_;
    uint8_t frozen_ = 0;
    std::function<int32_t()> controlStateGetter_;
    std::array<ButtonBinding, kNumCba> buttons_{};
    std::array<AxisBinding, kNumCaa> axes_{};
};

} // namespace sr3player
