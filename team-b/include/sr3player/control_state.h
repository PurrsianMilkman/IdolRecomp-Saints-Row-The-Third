#pragma once

// sr3player/control_state.h - player runtime unit U1: the control-state
// machine (spec-player-runtime.md Sec2, SPEC TEAM 2026-10-03; the ONLY source
// for every rule below - addresses are cited from that text, never derived).
//
// What is reproduced exactly:
//   * the 8-row state table 0x012e6bb8 - WHICH slots of which rows are
//     non-null (Sec2.1), so the dispatcher skips exactly the null ones;
//   * the request stack 0x012e6c58: capacity 5, 12-byte {state, handle, 0}
//     entries, push 0x005aa780 / pop 0x005aa740 / remove-and-shift
//     0x005aa640 / reset 0x005aa690 / getter 0x005aa630 (Sec2.2, Sec2.5);
//   * dispatch 0x005aa6c0 and the secondary dispatch 0x005aa610 (Sec2.3,
//     Sec2.4), including "exit, then enter, then the new row's per-frame,
//     all in the same update";
//   * the rows-0/1 enter 0x005aa5f0 = "clear latched, then tail-jump into
//     state 2's per-frame 0x005b7430" (Sec2.7).
//
// What is NOT reproduced: the BODIES of the row handlers 0x005b2170,
// 0x005b7430, 0x005ac4f0, 0x005b69c0 and 0x005c2800 belong to later units
// (U2 onward, Sec2.7 records only their skeletons). Each is an injectable
// body (setHandlerBody), a no-op until a later unit provides one. The shared
// single-RET stub 0x00754410 IS a no-op in the real game (Sec2.1). The
// "return 1" inert hook 0x00d48e50 called before every enter has no effect
// and is not modelled. Reset's first call 0x005aed70 (control floats,
// grenade_disable registration) is outside U1's machinery - not modelled.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace sr3player {

inline constexpr int kNumControlStates = 8;
inline constexpr int kControlStackCapacity = 5;   // 0x012e6c5c
inline constexpr int32_t kDefaultControlState = 2; // "normal play", the empty-stack fallback
inline constexpr int32_t kLockedStateMission = 0;  // row 0
inline constexpr int32_t kLockedStateUi = 1;       // row 1 (UI, menus, player_controls_disable)
inline constexpr int32_t kNoControlState = 6;      // row 6, empty: no player-control code at all
inline constexpr int32_t kFreeCameraState = 7;     // row 7

enum class ControlSlot { Enter = 0, Exit = 1, PerFrame = 2, Secondary = 3 };

// The distinct functions the table points at (Sec2.1).
enum class ControlHandler {
    LockedEnter,          // 0x005aa5f0 (rows 0/1 enter) - composite, implemented here
    LockedPerFrame,       // 0x005b2170 (rows 0/1 per-frame) - later unit
    NormalExit,           // 0x005ac4f0 (row 2 exit) - later unit
    NormalPerFrame,       // 0x005b7430 (row 2 per-frame) - later unit
    NormalSecondary,      // 0x005b69c0 (row 2 secondary) - later unit
    FreeCameraPerFrame,   // 0x005c2800 (row 7 per-frame) - later unit
    RetStub,              // 0x00754410 (shared single-RET stub) - a real no-op
};
inline constexpr int kNumControlHandlers = 7;

// One stack entry, 12 bytes: +0 requested state, +4 handle, +8 always 0.
struct ControlRequest {
    int32_t state = 0;
    uint32_t handle = 0;
    uint32_t reserved = 0;
};

class ControlStateMachine {
public:
    // Static values (Sec2.2): current 0, previous 0, count 0, handle counter 0.
    ControlStateMachine() = default;

    // 0x005aa780: pre-increments the handle counter; appends {state, handle,
    // 0} only if count + 1 <= capacity; returns the new handle EITHER WAY (a
    // full-stack push is silently dropped). First handle is 1, so 0 never
    // names an entry.
    uint32_t push(int32_t state);
    // 0x005aa740: searches from the top down for `handle`; removes the
    // first match with 0x005aa640 (shift later entries down, order kept);
    // no match -> nothing.
    void pop(uint32_t handle);
    // 0x005aa690 (the 0x005aed70 call it makes first is not modelled):
    // current = 1, previous = -1, count = 0. The handle counter is untouched.
    void reset();

    int32_t currentState() const { return current_; }   // 0x005aa630 (0x0140e62c)
    int32_t previousState() const { return previous_; } // 0x0140e628
    // Top entry's state, or 2 when the stack is empty (Sec2.3 step 1).
    int32_t desiredState() const;
    const std::vector<ControlRequest>& stack() const { return stack_; }
    uint32_t handleCounter() const { return handleCounter_; } // 0x0140e634

    // 0x005aa6c0 (Sec2.3). Throws std::out_of_range if the current or
    // desired state is outside the 8-row table - CHOSEN: the real
    // dispatcher would index past 0x012e6bb8; every real push site pushes
    // 0, 1, 2, 6 or 7 (Sec2.6), so this never fires on real call sites.
    void dispatch();
    // 0x005aa610 (Sec2.4): current row's +0x0c slot, if non-null.
    void secondaryDispatch();

    // The static slot table (Sec2.1): nullopt = a null slot.
    static std::optional<ControlHandler> slotHandler(int row, ControlSlot slot);

    // Body for a later-unit handler (LockedPerFrame, NormalExit,
    // NormalPerFrame, NormalSecondary, FreeCameraPerFrame). Setting a body
    // for LockedEnter or RetStub is ignored: their behaviour is given here.
    void setHandlerBody(ControlHandler handler, std::function<void()> body);
    // "clear latched" 0x005bbf60, called by the rows-0/1 enter.
    void setClearLatched(std::function<void()> fn) { clearLatched_ = std::move(fn); }

    // Test observability: every handler invocation in call order, including
    // the composite enter's own tail-jump into NormalPerFrame.
    struct TraceEvent {
        int row;
        ControlSlot slot;
        ControlHandler handler;
    };
    bool traceEnabled = false;
    std::vector<TraceEvent> trace;
    uint64_t clearLatchedCalls() const { return clearLatchedCalls_; }

private:
    void invoke(int row, ControlSlot slot);
    void runHandler(int row, ControlSlot slot, ControlHandler h);

    int32_t current_ = 0;
    int32_t previous_ = 0;
    std::vector<ControlRequest> stack_;
    uint32_t handleCounter_ = 0;
    std::array<std::function<void()>, kNumControlHandlers> bodies_{};
    std::function<void()> clearLatched_;
    uint64_t clearLatchedCalls_ = 0;
};

} // namespace sr3player
