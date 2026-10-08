// sr3player - player runtime unit U1, the control-state machine
// (spec-player-runtime.md Sec2). See include/sr3player/control_state.h.
#include "sr3player/control_state.h"

#include <stdexcept>

namespace sr3player {

namespace {
void checkRow(int32_t state) {
    if (state < 0 || state >= kNumControlStates)
        throw std::out_of_range("control state outside the 8-row table 0x012e6bb8 (spec-player-runtime.md Sec2.1)");
}
} // namespace

uint32_t ControlStateMachine::push(int32_t state) {
    const uint32_t handle = ++handleCounter_;
    if (stack_.size() + 1 <= static_cast<size_t>(kControlStackCapacity)) stack_.push_back({state, handle, 0});
    return handle;
}

void ControlStateMachine::pop(uint32_t handle) {
    for (size_t i = stack_.size(); i-- > 0;) {
        if (stack_[i].handle == handle) {
            stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(i));  // 0x005aa640: shift down, order kept
            return;
        }
    }
}

void ControlStateMachine::reset() {
    current_ = 1;
    previous_ = -1;
    stack_.clear();
}

int32_t ControlStateMachine::desiredState() const {
    return stack_.empty() ? kDefaultControlState : stack_.back().state;
}

// Sec2.1 table, CONFIRMED by direct read in the spec.
std::optional<ControlHandler> ControlStateMachine::slotHandler(int row, ControlSlot slot) {
    switch (row) {
    case 0:
    case 1:
        if (slot == ControlSlot::Enter) return ControlHandler::LockedEnter;
        if (slot == ControlSlot::PerFrame) return ControlHandler::LockedPerFrame;
        return std::nullopt;
    case 2:
        switch (slot) {
        case ControlSlot::Enter: return ControlHandler::RetStub;
        case ControlSlot::Exit: return ControlHandler::NormalExit;
        case ControlSlot::PerFrame: return ControlHandler::NormalPerFrame;
        case ControlSlot::Secondary: return ControlHandler::NormalSecondary;
        }
        return std::nullopt;
    case 7:
        if (slot == ControlSlot::Enter || slot == ControlSlot::Exit) return ControlHandler::RetStub;
        if (slot == ControlSlot::PerFrame) return ControlHandler::FreeCameraPerFrame;
        return std::nullopt;
    default:
        return std::nullopt;  // rows 3-6: every slot null
    }
}

void ControlStateMachine::setHandlerBody(ControlHandler handler, std::function<void()> body) {
    if (handler == ControlHandler::LockedEnter || handler == ControlHandler::RetStub) return;
    bodies_[static_cast<int>(handler)] = std::move(body);
}

void ControlStateMachine::runHandler(int row, ControlSlot slot, ControlHandler h) {
    if (traceEnabled) trace.push_back({row, slot, h});
    switch (h) {
    case ControlHandler::RetStub:
        return;
    case ControlHandler::LockedEnter:
        // 0x005aa5f0: clear latched, then tail-jump into 0x005b7430.
        ++clearLatchedCalls_;
        if (clearLatched_) clearLatched_();
        runHandler(2, ControlSlot::PerFrame, ControlHandler::NormalPerFrame);
        return;
    default:
        if (const auto& body = bodies_[static_cast<int>(h)]) body();
        return;
    }
}

void ControlStateMachine::invoke(int row, ControlSlot slot) {
    if (auto h = slotHandler(row, slot)) runHandler(row, slot, *h);
}

void ControlStateMachine::dispatch() {
    const int32_t desired = desiredState();
    if (desired != current_) {
        checkRow(current_);
        checkRow(desired);
        invoke(current_, ControlSlot::Exit);
        previous_ = current_;
        current_ = desired;
        invoke(current_, ControlSlot::Enter);  // (after the inert 0x00d48e50 hook)
    }
    checkRow(current_);
    invoke(current_, ControlSlot::PerFrame);
}

void ControlStateMachine::secondaryDispatch() {
    checkRow(current_);
    invoke(current_, ControlSlot::Secondary);
}

} // namespace sr3player
