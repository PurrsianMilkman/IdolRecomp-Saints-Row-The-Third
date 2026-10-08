// Synthetic tests for the player runtime units U0 (input query contract) and
// U1 (control-state machine), spec-player-runtime.md Sec1/Sec2. Every
// expected value below is a rule quoted from that spec (cited per block);
// input comes from SyntheticDeviceSource - injected test values, not data.
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3luahost/open_state.h"
#include "sr3player/control_state.h"
#include "sr3player/input.h"

using namespace sr3player;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool feq(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// Clears all per-frame device edges (levels stay).
void clearEdges(SyntheticDeviceSource& d) {
    d.padDownEdge.fill(false);
    d.padRepeatPulse.fill(false);
    d.keyDownEdge.fill(false);
    d.keyUpEdge.fill(false);
    d.mouseDownEdge.fill(false);
    d.mouseUpEdge.fill(false);
    for (auto& s : d.stickPulse) s.fill(false);
}

constexpr int kScanA = 0x1e;
constexpr int kScanD = 0x20;
constexpr int kScanSpace = 0x39;

// ===================================================================
// U1 - control-state machine (Sec2)
// ===================================================================
void testControlStack() {
    // Static values (Sec2.2) and the empty-stack fallback (Sec2.3 step 1).
    {
        ControlStateMachine m;
        CHECK(m.currentState() == 0 && m.previousState() == 0);
        CHECK(m.stack().empty() && m.handleCounter() == 0);
        CHECK(m.desiredState() == 2);
    }
    // Handles are pre-incremented from 0: the first is 1, 0 never names an
    // entry (Sec2.5).
    {
        ControlStateMachine m;
        const uint32_t h1 = m.push(1);
        const uint32_t h2 = m.push(0);
        CHECK(h1 == 1 && h2 == 2);
        CHECK(m.stack().size() == 2 && m.stack()[0].handle == 1 && m.stack()[1].handle == 2);
        CHECK(m.stack()[0].reserved == 0 && m.stack()[1].reserved == 0);  // {state, handle, 0}
        m.pop(0);  // 0 never names an entry: nothing removed
        CHECK(m.stack().size() == 2);
    }
    // Desired state = top entry's state, else 2.
    {
        ControlStateMachine m;
        m.push(7);
        CHECK(m.desiredState() == 7);
        m.push(0);
        CHECK(m.desiredState() == 0);
    }
    // Full stack: the push is silently dropped but STILL returns a fresh
    // non-zero handle, which then matches nothing (Sec2.5, Sec2.9).
    {
        ControlStateMachine m;
        for (int i = 0; i < kControlStackCapacity; ++i) m.push(1);
        CHECK(m.stack().size() == 5);
        const uint32_t dropped = m.push(7);
        CHECK(dropped == 6);
        CHECK(m.stack().size() == 5);
        CHECK(m.desiredState() == 1);  // the dropped 7 never took effect
        m.pop(dropped);                // "the later pop is a no-op"
        CHECK(m.stack().size() == 5);
        CHECK(m.handleCounter() == 6);
    }
    // Pop by handle, top-down search, out of order, order of the rest kept
    // (0x005aa640 shifts later entries down) - Sec2.5.
    {
        ControlStateMachine m;
        const uint32_t a = m.push(0);
        const uint32_t b = m.push(1);
        const uint32_t c = m.push(7);
        m.pop(b);  // release the MIDDLE lock
        CHECK(m.stack().size() == 2);
        CHECK(m.stack()[0].handle == a && m.stack()[0].state == 0);
        CHECK(m.stack()[1].handle == c && m.stack()[1].state == 7);
        CHECK(m.desiredState() == 7);
        m.pop(c);  // the desired state becomes whatever is now on top
        CHECK(m.desiredState() == 0);
        m.pop(999);  // unknown handle: nothing
        CHECK(m.stack().size() == 1);
        m.pop(a);
        CHECK(m.stack().empty() && m.desiredState() == 2);
    }
    // A push of 2 whose handle is discarded forces normal play on top of a
    // lock (Sec2.6, push site 0x00b94c19); only a reset removes it.
    {
        ControlStateMachine m;
        const uint32_t lock = m.push(1);
        m.push(2);
        CHECK(m.desiredState() == 2);
        m.pop(lock);
        CHECK(m.desiredState() == 2 && m.stack().size() == 1);
        m.reset();
        CHECK(m.stack().empty());
    }
    // Reset (Sec2.5): current 1, previous -1, count 0; the handle counter is
    // untouched, so handles stay unique across a reset.
    {
        ControlStateMachine m;
        m.push(0);
        m.push(1);
        m.reset();
        CHECK(m.currentState() == 1 && m.previousState() == -1 && m.stack().empty());
        CHECK(m.handleCounter() == 2);
        CHECK(m.push(0) == 3);
    }
}

void testControlDispatch() {
    using T = ControlStateMachine::TraceEvent;
    auto same = [](const std::vector<T>& got, const std::vector<T>& want) {
        if (got.size() != want.size()) return false;
        for (size_t i = 0; i < got.size(); ++i)
            if (got[i].row != want[i].row || got[i].slot != want[i].slot || got[i].handler != want[i].handler) return false;
        return true;
    };

    // Static table (Sec2.1).
    CHECK(ControlStateMachine::slotHandler(0, ControlSlot::Enter) == ControlHandler::LockedEnter);
    CHECK(!ControlStateMachine::slotHandler(0, ControlSlot::Exit));
    CHECK(ControlStateMachine::slotHandler(1, ControlSlot::PerFrame) == ControlHandler::LockedPerFrame);
    CHECK(ControlStateMachine::slotHandler(2, ControlSlot::Secondary) == ControlHandler::NormalSecondary);
    for (int r = 3; r <= 6; ++r)
        for (ControlSlot s : {ControlSlot::Enter, ControlSlot::Exit, ControlSlot::PerFrame, ControlSlot::Secondary})
            CHECK(!ControlStateMachine::slotHandler(r, s));
    CHECK(ControlStateMachine::slotHandler(7, ControlSlot::Exit) == ControlHandler::RetStub);
    CHECK(ControlStateMachine::slotHandler(7, ControlSlot::PerFrame) == ControlHandler::FreeCameraPerFrame);
    CHECK(!ControlStateMachine::slotHandler(7, ControlSlot::Secondary));

    ControlStateMachine m;
    m.traceEnabled = true;
    bool latchedCleared = false;
    bool normalPerFrameSawCleared = false;
    m.setClearLatched([&] { latchedCleared = true; });
    m.setHandlerBody(ControlHandler::NormalPerFrame, [&] { normalPerFrameSawCleared = latchedCleared; });

    // Static current 0 -> desired 2: row 0 has no exit (skipped), row 2's
    // enter is the RET stub, then row 2's per-frame - one update (Sec2.3).
    m.dispatch();
    CHECK(m.previousState() == 0 && m.currentState() == 2);
    CHECK(same(m.trace, {{2, ControlSlot::Enter, ControlHandler::RetStub},
                         {2, ControlSlot::PerFrame, ControlHandler::NormalPerFrame}}));

    // No change: only the per-frame.
    m.trace.clear();
    m.dispatch();
    CHECK(same(m.trace, {{2, ControlSlot::PerFrame, ControlHandler::NormalPerFrame}}));

    // Change 2 -> 1 in ONE update: exit(2), then enter(1) = clear latched +
    // tail-jump into state 2's per-frame (run on cleared input), then the
    // new row's own per-frame (Sec2.3, Sec2.7).
    m.trace.clear();
    latchedCleared = false;
    const uint32_t lock = m.push(1);
    m.dispatch();
    CHECK(m.previousState() == 2 && m.currentState() == 1);
    CHECK(same(m.trace, {{2, ControlSlot::Exit, ControlHandler::NormalExit},
                         {1, ControlSlot::Enter, ControlHandler::LockedEnter},
                         {2, ControlSlot::PerFrame, ControlHandler::NormalPerFrame},
                         {1, ControlSlot::PerFrame, ControlHandler::LockedPerFrame}}));
    CHECK(latchedCleared && normalPerFrameSawCleared);
    CHECK(m.clearLatchedCalls() == 1);

    // Secondary dispatch: only row 2 has a +0x0c slot (Sec2.4).
    m.trace.clear();
    m.secondaryDispatch();
    CHECK(m.trace.empty());

    // Release the lock: 1 -> 2; row 1 has no exit, row 2's enter is the stub.
    m.pop(lock);
    m.trace.clear();
    m.dispatch();
    CHECK(same(m.trace, {{2, ControlSlot::Enter, ControlHandler::RetStub},
                         {2, ControlSlot::PerFrame, ControlHandler::NormalPerFrame}}));
    m.trace.clear();
    m.secondaryDispatch();
    CHECK(same(m.trace, {{2, ControlSlot::Secondary, ControlHandler::NormalSecondary}}));

    // Row 6 ("no player control at all"): row 2's exit runs, then nothing.
    const uint32_t six = m.push(6);
    m.trace.clear();
    m.dispatch();
    CHECK(m.currentState() == 6);
    CHECK(same(m.trace, {{2, ControlSlot::Exit, ControlHandler::NormalExit}}));
    m.trace.clear();
    m.dispatch();
    CHECK(m.trace.empty());
    m.pop(six);

    // Reset: current 1 -> the next dispatch is a 1 -> 2 transition calling
    // row 2's no-op enter (Sec2.5).
    m.dispatch();
    m.reset();
    m.trace.clear();
    m.dispatch();
    CHECK(m.previousState() == 1 && m.currentState() == 2);
    CHECK(same(m.trace, {{2, ControlSlot::Enter, ControlHandler::RetStub},
                         {2, ControlSlot::PerFrame, ControlHandler::NormalPerFrame}}));

    // A state outside the 8-row table refuses (CHOSEN, see the header).
    m.push(9);
    bool threw = false;
    try {
        m.dispatch();
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw && m.currentState() == 2);
}

// ===================================================================
// U0 - input query contract (Sec1)
// ===================================================================
void testHeadlessDefault() {
    // Sec1.1: no window -> first frame runs the full clear and freezes;
    // every query reads "nothing" even with bindings present.
    InputSystem in;
    auto& b = in.buttonBindings()[0x12];  // CBA_OFC_SPRINT (Sec1.8.4)
    b.padA = 0;
    b.key1 = kScanSpace;
    b.mouse1 = 0;
    in.latchFrame();
    CHECK(in.frozen());
    int out = 123;
    CHECK(!in.pressed(0x12, &out) && out == kOutNone);
    CHECK(!in.held(0x12, &out) && out == kOutNone);
    CHECK(in.allSourcesDisabled(0x12));
    CHECK(in.analog(0x12, &out) == 0.0f && out == kOutNone);
    CHECK(!in.gamepadIsActiveInput() && !in.cameraDrivenByKbm());
    in.latchFrame();
    CHECK(in.frozen() && !in.held(0x12));
}

void testFreezeOnce() {
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    in.buttonBindings()[1].key1 = kScanA;
    dev.keyDown[kScanA] = true;
    in.latchFrame();
    CHECK(!in.frozen() && in.held(1));
    dev.active = false;  // window minimised
    in.latchFrame();
    CHECK(in.frozen() && !in.held(1) && dev.resetCount == 1);
    in.latchFrame();  // only the FIRST inactive frame clears (0x01411b03)
    CHECK(dev.resetCount == 1);
    dev.active = true;
    in.latchFrame();
    CHECK(!in.frozen() && in.held(1));
}

void testPressedHeldAndOrder() {
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    const int cba = 0x19;  // CBA_OFC_CROUCH (Sec1.8.4) - any in-range value works
    ButtonBinding& b = in.buttonBindings()[cba];
    b.padA = 0;        // pad A
    b.stickDir = 0;    // LEFT_STICK_DIR_UP
    b.key1 = kScanA;
    b.mouse1 = 0;      // MOUSE LEFT

    // Held without a down-edge: held true, pressed false (+1 level vs +2 edge).
    dev.keyDown[kScanA] = true;
    in.latchFrame();
    int out = 0;
    CHECK(!in.pressed(cba, &out) && out == kOutNone);
    CHECK(in.held(cba, &out) && out == kOutKeyOrMouse);

    // Every source satisfied: the pad wins (Sec1.8.3 order pad -> stick ->
    // keys -> mouse).
    clearEdges(dev);
    dev.padDown[0] = dev.padDownEdge[0] = true;
    dev.stickDeadZoned[0] = {0.0f, 0.9f};  // crossing 0.4 from last frame's 0
    dev.keyDownEdge[kScanA] = true;
    dev.mouseDown[0] = dev.mouseDownEdge[0] = true;
    in.latchFrame();
    CHECK(in.pressed(cba, &out) && out == kOutPadButton);
    // Pad bit gone -> the stick direction is next.
    in.mutableContext().padPressed = 0;
    CHECK(in.pressed(cba, &out) && out == kOutStickDirection);
    // Stick blocked -> the key.
    in.mutableContext().sticks[0].blockUp = 1;
    CHECK(in.pressed(cba, &out) && out == kOutKeyOrMouse);
    // Key not enabled -> the mouse button (same out code 2).
    in.mutableContext().keys[kScanA].enabled = 0;
    CHECK(in.pressed(cba, &out) && out == kOutKeyOrMouse);
    in.mutableContext().mouseButtons[0].enabled = 0;
    CHECK(!in.pressed(cba, &out) && out == kOutNone);

    // Out of range: false/0 WITHOUT writing out (Sec1.8.1).
    out = 77;
    CHECK(!in.pressed(0xa4, &out) && out == 77);
    CHECK(!in.held(-1, &out) && out == 77);
    CHECK(in.analog(0xa4, &out) == 0.0f && out == 77);
    CHECK(in.padButton(0xa4, false) == -1);

    // Stick direction pressed = crossing only: held on the next frame, not pressed.
    clearEdges(dev);
    dev.padDown[0] = false;
    dev.keyDown[kScanA] = false;
    dev.mouseDown[0] = false;
    in.latchFrame();  // y stays 0.9
    CHECK(in.held(cba, &out) && out == kOutStickDirection);
    CHECK(!in.pressed(cba, &out));
}

void testConsumption() {
    // Sec1.11.2: consumption is per physical input - another action bound to
    // the same key reads false (pressed AND held) for the rest of the frame.
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    in.buttonBindings()[10].key1 = kScanSpace;
    in.buttonBindings()[11].key2 = kScanSpace;
    in.buttonBindings()[20].padA = 3;  // Y
    in.buttonBindings()[21].padB = 3;
    in.buttonBindings()[21].stickDir = 4;  // RIGHT_STICK_DIR_UP
    dev.keyDown[kScanSpace] = dev.keyDownEdge[kScanSpace] = true;
    dev.padDown[3] = dev.padDownEdge[3] = true;
    in.latchFrame();

    CHECK(in.pressed(11) && in.held(11));
    int out = 0;
    CHECK(in.pressedConsume(10, &out) && out == kOutKeyOrMouse);
    CHECK(!in.pressed(11) && !in.held(11));
    CHECK(!in.pressedConsume(10));  // already consumed

    CHECK(in.held(21));
    CHECK(in.pressedConsume(20, &out) && out == kOutPadButton);
    CHECK(!in.pressed(21) && !in.held(21));  // bit cleared from BOTH +0x58 and +0x60

    // The next latch re-arms everything.
    clearEdges(dev);
    in.latchFrame();
    CHECK(in.held(11) && in.held(21));

    // 0x005bbdd0 consumes pad + keys + mouse but NOT the stick direction.
    dev.padDown[3] = false;
    dev.stickDeadZoned[1] = {0.0f, 0.8f};
    in.latchFrame();
    in.consume(21);
    CHECK(in.held(21, &out) && out == kOutStickDirection);

    // Clear latched (Sec1.11.1): every query false for the rest of the frame,
    // underlying data untouched.
    in.clearLatched();
    CHECK(!in.held(11) && !in.held(21) && in.allSourcesDisabled(11));
    CHECK(in.context().keys[kScanSpace].held == 1);
    in.latchFrame();
    CHECK(in.held(11));
}

void testNavigationAndStickPulse() {
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    in.buttonBindings()[0x71].stickDir = 1;  // CBA_MENU_DP_DOWN -> LEFT_STICK_DIR_DOWN
    dev.stickPulse[0][1] = true;             // engine "down" pulse -> +0x1c = +1
    in.latchFrame();
    CHECK(in.context().sticks[0].pulseY == 1);
    int out = 0;
    CHECK(in.navigationPulseConsume(0x71, &out) && out == kOutStickDirection);
    CHECK(in.context().sticks[0].pulseY == 0 && in.context().sticks[0].blockDown == 1);
    CHECK(!in.navigationPulseConsume(0x71));
}

void testAllDisabledSpecialCase() {
    // Sec1.8.3 / OPEN 3: no pad, no key 1, a key 2, no mouse 1, a mouse 2 -> false.
    InputSystem in;
    in.latchFrame();  // frozen-cleared: every enable byte is 0
    ButtonBinding& b = in.buttonBindings()[5];
    b.key2 = kScanA;
    b.mouse2 = 1;
    CHECK(!in.allSourcesDisabled(5));
    b.key1 = kScanD;  // breaks the special shape -> the normal rule (all disabled)
    CHECK(in.allSourcesDisabled(5));
    CHECK(in.allSourcesDisabled(6));  // no bindings counts as disabled
}

void testAnalog() {
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    ButtonBinding& b = in.buttonBindings()[7];  // CBA_OFC_ATTACK_PRIMARY
    b.padA = kPadRT;
    b.stickDir = 2;  // LEFT_STICK_DIR_RIGHT
    b.key1 = kScanA;
    in.latchFrame();
    int out = 0;
    CHECK(in.analog(7, &out) == 0.0f && out == kOutNone);
    dev.padValue[kPadRT] = 0.3f;  // raw engine value, not thresholded
    CHECK(feq(in.analog(7, &out), 0.3f) && out == kOutPadButton);
    dev.stickDeadZoned[0] = {0.5f, 0.0f};
    in.latchFrame();
    CHECK(feq(in.analog(7, &out), 0.8f) && out == kOutStickDirection);
    dev.stickDeadZoned[0] = {0.9f, 0.0f};
    in.latchFrame();
    CHECK(feq(in.analog(7, &out), 1.0f));  // clamped <= 1
    // Key contributes 1.0 only while pad + stick are ~0.
    dev.padValue[kPadRT] = 0.0f;
    dev.stickDeadZoned[0] = {0.0f, 0.0f};
    dev.keyDown[kScanA] = true;
    in.latchFrame();
    CHECK(feq(in.analog(7, &out), 1.0f) && out == kOutKeyOrMouse);
    // Fell through 0.5: previous frame 1.0, this frame 0.
    dev.keyDown[kScanA] = false;
    dev.keyUpEdge[kScanA] = true;
    in.latchFrame();
    CHECK(in.fellThrough(7, 0.5f, &out) && out == kOutKeyOrMouse);
    clearEdges(dev);
    in.latchFrame();
    CHECK(!in.fellThrough(7, 0.5f));
}

void testAxis() {
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    const int caa = 9;  // CAA_DRIVE_STEER
    AxisBinding& a = in.axisBindings()[caa];
    a.axis = 2;  // LEFT_STICK_AXIS_X -> stick 0 x
    a.keyPos = kScanD;
    a.keyNeg = kScanA;
    a.mouseAxis = 0;  // MOUSE X
    int out = 0;

    // Pad priority (Sec1.9.2 step 3): a non-zero stick wins outright, keys ignored.
    dev.stickDeadZoned[0] = {0.5f, 0.0f};
    dev.keyDown[kScanA] = true;
    in.latchFrame();
    CHECK(feq(in.axis(caa, &out), 0.5f) && out == kOutAxisGamepad);
    a.invert = 1;
    CHECK(feq(in.axis(caa, &out), -0.5f));
    a.invert = 0;

    // Stick zero -> keys: -1 from the negative key.
    dev.stickDeadZoned[0] = {0.0f, 0.0f};
    in.latchFrame();
    CHECK(feq(in.axis(caa, &out), -1.0f) && out == kOutAxisKbm);
    // Both keys -> sum 0 -> the mouse axis is added (step 5).
    dev.keyDown[kScanD] = true;
    dev.mouseDx = 100.0f;  // 100 counts x 0.24 x 0.01 = 0.24
    in.latchFrame();
    CHECK(feq(in.context().mouseAxes[0].value, 0.24f));
    CHECK(feq(in.axis(caa, &out), 0.24f) && out == kOutAxisKbm);
    // Key sum non-zero -> mouse NOT added.
    dev.keyDown[kScanA] = false;
    in.latchFrame();
    CHECK(feq(in.axis(caa, &out), 1.0f) && out == kOutAxisKbm);
    // Two positives clamp to 1 (step 6); the curved evaluator is unclamped.
    a.altKeyPos = kScanSpace;
    dev.keyDown[kScanSpace] = true;
    in.latchFrame();
    CHECK(feq(in.axis(caa), 1.0f) && feq(in.axisCurved(caa), 2.0f));
    // Nothing -> 0, out -1.
    dev.keyDown.fill(false);
    dev.mouseDx = 0.0f;
    in.latchFrame();
    CHECK(in.axis(caa, &out) == 0.0f && out == kOutNone);

    // Secondary axis used only when the primary is exactly 0; analog block -> 0.
    a.altAxis = 1;  // RIGHT_STICK_AXIS_Y -> stick 1 y
    a.altInvert = 1;
    dev.stickDeadZoned[1] = {0.0f, 0.25f};
    in.latchFrame();
    CHECK(feq(in.axis(caa, &out), -0.25f) && out == kOutAxisGamepad);
    in.mutableContext().sticks[1].blockAnalogY = 1;
    CHECK(in.axis(caa, &out) == 0.0f && out == kOutNone);

    // Response curve: 0.9 v^3 + 0.1.
    a.altAxis = -1;
    dev.stickDeadZoned[0] = {0.5f, 0.0f};
    in.latchFrame();
    CHECK(feq(in.axisCurved(caa), 0.9f * 0.125f + 0.1f));

    // Half queries: keys/mouse only, range-checked at 0x21 without an out write.
    dev.keyDown[kScanD] = dev.keyDownEdge[kScanD] = true;
    in.latchFrame();
    CHECK(in.axisPositivePressed(caa, &out) && out == kOutKeyOrMouse);
    CHECK(!in.axisNegativeHeld(caa, &out) && out == kOutNone);
    out = 5;
    CHECK(!in.axisPositiveHeld(0x22, &out) && out == 5);
    int32_t p = 0, n = 0;
    CHECK(in.keyPair1(caa, &p, &n) && p == kScanD && n == kScanA);
    CHECK(!in.mousePair1(caa, &p, &n) && p == -1);
    // The evaluators do not range check in the real build: refused here.
    bool threw = false;
    try {
        (void)in.axis(34);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
}

void testLatchDetails() {
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    // Mouse clamp to the unit circle (Sec1.4 step 7).
    dev.mouseDx = 300.0f;  // 0.72
    dev.mouseDy = 400.0f;  // 0.96 -> length 1.2
    in.latchFrame();
    CHECK(feq(in.context().mouseAxes[0].value, 0.6f) && feq(in.context().mouseAxes[1].value, 0.8f));
    CHECK(feq(in.context().mouseAxes[1].valueCopy, 0.8f));

    // The wheel is ignored while the middle button is held (Sec1.4 step 6).
    in.buttonBindings()[3].mouse1 = kWheelUp;
    dev.mouseDown[kWheelUp] = true;
    in.latchFrame();
    CHECK(in.held(3));
    dev.mouseDown[kMouseMiddle] = true;
    in.latchFrame();
    CHECK(!in.held(3));
    in.middleDisablesWheel = 0;
    in.latchFrame();
    CHECK(in.held(3));

    // Sign-flip suppression (Sec1.4 step 3).
    dev.stickDeadZoned[0] = {0.7f, 0.0f};
    in.latchFrame();
    dev.stickDeadZoned[0] = {-0.7f, 0.0f};
    in.latchFrame();
    CHECK(in.context().sticks[0].x == 0.0f && in.context().sticks[0].prevX == 0.7f);
    in.latchFrame();
    CHECK(in.context().sticks[0].x == -0.7f);  // prev was 0 -> not opposite

    // Trigger hysteresis (Sec1.4 step 2): down -> max(v - 0.1, t); up -> min(v + 0.1, t).
    dev.padThreshold[kPadLT] = 0.5f;
    dev.padValue[kPadLT] = 0.9f;
    dev.padDown[kPadLT] = true;
    in.latchFrame();
    CHECK(feq(dev.padThreshold[kPadLT], 0.8f));
    dev.padDown[kPadLT] = false;
    dev.padValue[kPadLT] = 0.2f;
    in.latchFrame();
    CHECK(feq(dev.padThreshold[kPadLT], 0.3f));

    // No pad: masks zero, sticks zeroed, gamepad flag 0, camera flag 1.
    dev.padPresent = false;
    in.latchFrame();
    CHECK(in.context().padHeldEnable == 0 && in.context().sticks[0].prevX == 0.0f);
    CHECK(!in.gamepadIsActiveInput() && in.cameraDrivenByKbm());

    // Latch step 5: state 5 with the OPEN byte refuses; with it set, keys keep
    // last frame's values.
    int state = 5;
    in.setControlStateGetter([&] { return state; });
    bool threw = false;
    try {
        in.latchFrame();
    } catch (const sr3luahost::OpenStateError&) {
        threw = true;
    }
    CHECK(threw);
    in.state5LatchSkipByte = true;
    dev.keyDown[kScanA] = true;
    in.latchFrame();
    CHECK(in.context().keys[kScanA].held == 0);
    state = 2;
    in.latchFrame();
    CHECK(in.context().keys[kScanA].held == 1);
}

void testDeviceFamily() {
    SyntheticDeviceSource dev;
    InputSystem in(&dev);
    AxisBinding& rot = in.axisBindings()[kCaaCameraRotate];
    rot.axis = 0;       // RIGHT_STICK_AXIS_X
    rot.mouseAxis = 0;  // MOUSE X
    in.latchFrame();
    CHECK(!in.gamepadIsActiveInput() && !in.cameraDrivenByKbm());
    // Pad activity sets +0x9f5.
    dev.padDown[0] = true;
    in.latchFrame();
    CHECK(in.gamepadIsActiveInput());
    // Stays set while only the pad is used...
    in.latchFrame();
    CHECK(in.gamepadIsActiveInput());
    // ...cleared by key activity.
    dev.padDown[0] = false;
    dev.keyDown[kScanA] = true;
    in.latchFrame();
    CHECK(!in.gamepadIsActiveInput());
    dev.keyDown[kScanA] = false;
    // Camera provenance: mouse-driven camera sets +0x9f6 ...
    dev.mouseDx = 50.0f;
    in.latchFrame();
    CHECK(in.cameraDrivenByKbm());
    // ...sticky until the stick actually moves the camera.
    dev.mouseDx = 0.0f;
    in.latchFrame();
    CHECK(in.cameraDrivenByKbm());
    dev.stickDeadZoned[1] = {0.6f, 0.0f};
    in.latchFrame();
    CHECK(!in.cameraDrivenByKbm() && in.gamepadIsActiveInput());
}

void testBindingApplication() {
    InputSystem in;
    auto& gen = in.schemeArrays[static_cast<int>(SchemeKind::General)];
    auto& sat = in.schemeArrays[static_cast<int>(SchemeKind::Satellite)];
    auto& foot = in.schemeArrays[static_cast<int>(SchemeKind::OnFoot)];
    auto& drive = in.schemeArrays[static_cast<int>(SchemeKind::Driving)];
    gen = {SchemeSlot{"General Controls", {{100, 12, -1, -1, 0}, {101, 13, -1, -1, 0}}, {{0, 0, 1, 0, 1}}}};
    sat = {SchemeSlot{"GCN", {}, {}}, SchemeSlot{"Satellite Controls", {{100, 4, -1, -1, 0}}, {}}};
    foot = {SchemeSlot{"B", {{20, 1, -1, -1, 0}}, {}},
            SchemeSlot{"A", {{18, 0, -1, 2, 77}, {101, 2, 3, -1, 0}}, {{11, 3, 0, 2, 0}}}};
    drive = {SchemeSlot{"N", {}, {}}};  // no "A": falls back to index 0 (-1 -> 0)

    BindingSetRecords set1, set2;
    set1.controls = {{18, kScanA, 0, -1, -1},  // accepted
                     {19, -1, kScanD, -1, -1},  // absent key (-1): the WHOLE row is dropped
                     {0, 0, 0, 0, 0},           // all zero: skipped
                     {21, 0, kScanD, 9, -1}};   // mouse 9 invalid: dropped
    set1.axes = {{9, kScanD, kScanA, 0, 0, -1, -1, -1, -1, 0}, {10, 300, 0, 0, 0, -1, -1, -1, -1, -1}};
    set2.controls = {{18, kScanSpace, 0, 2, -1},  // later set wins
                     {22, 0, kScanD, -1, -1}};    // UNBOUND (0) key 1 is accepted
    in.bindingSets = {set1, set2};
    in.rebuildBindings();

    const auto& B = in.buttonBindings();
    const auto& A = in.axisBindings();
    CHECK(in.optionFlagBits0to3 == 0x01);
    // Layering General -> Satellite -> On_Foot -> Driving; later kinds override.
    CHECK(B[100].padA == 4);                    // Satellite overrode General
    CHECK(B[101].padA == 2 && B[101].padB == 3); // On_Foot "A" overrode General
    CHECK(B[18].padA == 0 && B[18].stickDir == 2 && B[18].labelHandle == 77);
    CHECK(B[20].padA == -1);                    // slot "B" not applied
    CHECK(*in.currentSchemeIndex[static_cast<int>(SchemeKind::OnFoot)] == 1);
    CHECK(*in.currentSchemeIndex[static_cast<int>(SchemeKind::Satellite)] == 1);
    CHECK(*in.currentSchemeIndex[static_cast<int>(SchemeKind::Driving)] == 0);
    CHECK(A[0].axis == 0 && A[0].invert == 1 && A[11].axis == 3);
    // KB/M halves never touch the pad half and vice versa.
    CHECK(B[18].key1 == kScanSpace && B[18].mouse1 == 2);  // last set wins
    CHECK(B[19].key2 == 0 && B[21].key2 == 0);            // dropped rows
    CHECK(B[22].key1 == 0 && B[22].key2 == kScanD);
    CHECK(A[9].keyPos == kScanD && A[9].keyNeg == kScanA && A[9].mouseAxis == 0);
    CHECK(A[10].keyPos == 0 && A[10].mouseAxis == -1);    // key 300 invalid: dropped
    CHECK(A[0].keyPos == 0);                               // KB/M half untouched by schemes
    // Bound-key table.
    CHECK(in.context().keyIsBound[kScanSpace] == 1 && in.context().keyIsBound[kScanD] == 1);
    CHECK(in.context().keyIsBound[kScanA] == 1 && in.context().keyIsBound[0x10] == 0);

    // Switching On_Foot A -> B un-binds every action A named, even one General
    // had also bound (Sec1.7.2 consequence).
    in.applyScheme(SchemeKind::OnFoot, 0);
    CHECK(B[101].padA == -1 && B[101].padB == -1);
    CHECK(B[18].padA == -1 && B[18].labelHandle == 0);
    CHECK(B[20].padA == 1);
    CHECK(A[11].axis == -1);
    CHECK(B[18].key1 == kScanSpace);  // KB/M half survives
    // Clamp to [0, count-1].
    in.applyScheme(SchemeKind::OnFoot, 99);
    CHECK(*in.currentSchemeIndex[static_cast<int>(SchemeKind::OnFoot)] == 1 && B[18].padA == 0);

    // A standalone apply with the current index still OPEN refuses.
    InputSystem fresh;
    fresh.schemeArrays[static_cast<int>(SchemeKind::OnFoot)] = foot;
    bool threw = false;
    try {
        fresh.applyScheme(SchemeKind::OnFoot, 0);
    } catch (const sr3luahost::OpenStateError&) {
        threw = true;
    }
    CHECK(threw);

    // Scheme lookup: exact (case-insensitive) first, else contains, else -1.
    std::vector<SchemeSlot> s = {{"General Controls", {}, {}}, {"a", {}, {}}};
    CHECK(FindSchemeIndex(s, "A") == 1);
    CHECK(FindSchemeIndex(s, "controls") == 0);
    CHECK(FindSchemeIndex(s, "zzz") == -1);
}

void testHelpers() {
    CHECK(ApplyDeadZone(0.1f, 0.2f) == 0.0f);
    CHECK(feq(ApplyDeadZone(0.6f, 0.2f), 0.5f));
    CHECK(feq(ApplyDeadZone(-0.6f, 0.2f), -0.5f));
    CHECK(feq(ApplyDeadZone(0.6f, 0.2f, true), -0.5f));
    CHECK(ApplyDeadZone(0.6f, 0.2f, false, true) == 0.0f);
    float x = 0, y = 0;
    InputSystem::ScaleToUnit(3.0f, 4.0f, x, y);
    CHECK(feq(x, 0.6f) && feq(y, 0.8f));
    InputSystem::ScaleToUnit(0.3f, 0.4f, x, y);
    CHECK(feq(x, 0.3f) && feq(y, 0.4f));
}

} // namespace

int main() {
    testControlStack();
    testControlDispatch();
    testHeadlessDefault();
    testFreezeOnce();
    testPressedHeldAndOrder();
    testConsumption();
    testNavigationAndStickPulse();
    testAllDisabledSpecialCase();
    testAnalog();
    testAxis();
    testLatchDetails();
    testDeviceFamily();
    testBindingApplication();
    testHelpers();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_player_runtime_test: all checks passed\n";
    return 0;
}
