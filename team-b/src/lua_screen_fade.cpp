// Screen-fade state machine (spec-lua-api-behaviour.md Sec26.24, with Sec2.9,
// Sec8.13, Sec26.9; batch 2026-10-01). Declarations and the global table are
// in engine_state.h (EngineState::ScreenFade).
//
// What is engine behaviour here (CONFIRMED - disassembly, Sec26.24): the two
// request helpers 0x0059f8c0 / 0x0059fc40, the completion native
// Screen_fade_transition_complete 0x005a0110, the per-frame routine
// 0x0059fe70, init 0x0059fa30 and the two broadcast helpers. What is not:
//  - the host clock (the engine's 0x01320d9c): it starts at 0 and only
//    screenFadeHostFrame moves it;
//  - the completion timer in screenFadeHostFrame, the HOST-SIDE SUBSTITUTE
//    Sec26.24 allows for a host whose UI script never completes a fade; it is
//    a fallback only, counted separately from the real path;
//  - a stamp comparison at exact equality ("reached" / "not passed yet"),
//    whose result the spec does not state: refused as OPEN;
//  - the audio-id posts (HYPOTHESIS in the spec): not modelled.
//
// Calls into the UI Lua state run inside a lua_pcall on that state, through
// a trampoline with no C++ objects in its frame, so no Lua error ever leaves
// it (the frame rule in lua_spec_confirmed_stubs.cpp).
#include "sr3luahost/engine_state.h"

namespace sr3luahost {

namespace {

struct UiCall {
    const char* name;
    int nargs;
    double args[3];
};

// Runs under lua_pcall on the UI state. Returns true when the global was a
// function and was called; a Lua error raised here or by the callee lands in
// that lua_pcall.
int uiCallTrampoline(lua_State* L) {
    const UiCall* c = static_cast<const UiCall*>(lua_touserdata(L, 1));
    lua_getglobal(L, c->name);
    if (lua_type(L, -1) != LUA_TFUNCTION) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (int i = 0; i < c->nargs; ++i) lua_pushnumber(L, c->args[i]);
    lua_call(L, c->nargs, 0);
    lua_pushboolean(L, 1);
    return 1;
}

// Same budget as the host's hook watchdog (lua_host.cpp): a UI script that
// never returns is aborted instead of hanging the host.
constexpr int kUiCallInstructionBudget = 5'000'000;

void uiCallWatchdog(lua_State* L, lua_Debug*) {
    luaL_error(L, "sr3luahost watchdog: screen-fade UI call exceeded the %d-instruction budget",
               kUiCallInstructionBudget);
}

// Stamp comparisons of the per-frame routine and the fade-in hold test. The
// spec gives "reached" / "not passed yet" but not the result at equality.
[[noreturn]] void throwStampEquality(const char* global) {
    throw OpenStateError(std::string(global) + " compared with the clock at exact equality",
                         "spec-lua-api-behaviour.md Sec26.24");
}

bool stampReached(int64_t now, int64_t stamp, const char* global) {
    if (now == stamp) throwStampEquality(global);
    return now > stamp;
}

} // namespace

bool EngineState::screenFadeCallUi(const char* name, int nargs, double a0, double a1, double a2, bool* errored) {
    if (errored) *errored = false;
    lua_State* ui = screenFadeUi_;
    if (!ui) return false;
    UiCall call{name, nargs, {a0, a1, a2}};
    const int top = lua_gettop(ui);
    lua_Hook oldHook = lua_gethook(ui);
    const int oldMask = lua_gethookmask(ui);
    const int oldCount = lua_gethookcount(ui);
    lua_sethook(ui, uiCallWatchdog, LUA_MASKCOUNT, kUiCallInstructionBudget);
    lua_pushcfunction(ui, uiCallTrampoline);
    lua_pushlightuserdata(ui, &call);
    const int status = lua_pcall(ui, 1, 1, 0);
    lua_sethook(ui, oldHook, oldMask, oldCount);
    bool called;
    if (status != 0) {
        // The lookup or the script raised an error. Host choice (the spec
        // does not say what the engine's call builder does with one): it is
        // swallowed and counted; the call counts as made.
        const char* m = lua_tostring(ui, -1);
        if (std::string(name) == "screen_fade_do") {
            ++screenFadeCounters_.screenFadeDoErrors;
            screenFadeCounters_.lastScreenFadeDoError = m ? m : "(no message)";
        }
        if (errored) *errored = true;
        called = true;
    } else {
        called = lua_toboolean(ui, -1) != 0;
    }
    lua_settop(ui, top);
    return called;
}

void EngineState::screenFadeInit(bool documentFound) {
    // 0x0059fa30 (CONFIRMED): loads the "screen_fade" document (its id goes
    // to 0x012e6aa0, -1 on failure) and, if the document is found, keeps its
    // handle and sets state := 2, target := 2, flag := 1.
    if (!documentFound) return;
    screenFade_.documentLoaded.set(true);
    screenFade_.state.set(2);
    screenFade_.target.set(2);
    screenFade_.flag.set(1);
}

void EngineState::screenFadeRequest(bool out, int32_t durationMs, FadeCallback cb, uint32_t flag) {
    ScreenFade& f = screenFade_;
    const uint32_t settled = out ? 3u : 2u;         // the end this request moves toward
    const uint32_t running = out ? 1u : 0u;         // state while it runs
    const uint32_t oppositeRunning = out ? 0u : 1u; // the other direction running
    const int64_t now = screenFadeClockMs_;

    // Step 1 (both helpers): an audio-id post, HYPOTHESIS - not modelled.
    const uint32_t state = f.state.get();
    const uint32_t target = f.target.get();
    // Step 2: already settled at this end -> callback(settled), stop.
    if (state == settled && target == settled) {
        if (cb) cb(*this, settled);
        return;
    }
    // Step 4's deferral test and step 5's document test are read here, before
    // step 3 writes, so an OPEN read leaves nothing half done.
    bool defer = state == oppositeRunning;
    if (!out && !defer) {
        // Fade-in only: a hold stamp set (>= 0) that the clock has not passed.
        const int64_t holdLogo = f.holdLogoUntil.get();
        const int64_t holdImages = f.holdImagesUntil.get();
        const bool logoHolds = holdLogo >= 0 && !stampReached(now, holdLogo, f.holdLogoUntil.global());
        const bool imagesHold = holdImages >= 0 && !stampReached(now, holdImages, f.holdImagesUntil.global());
        defer = logoHolds || imagesHold;
    }
    const bool documentLoaded = defer ? false : f.documentLoaded.get();

    // Step 3.
    f.target.set(settled);
    f.flag.set(flag);
    // Step 4: park the callback; state untouched, no Lua call.
    if (defer) {
        f.deferred = cb;
        return;
    }
    // Fade-in step 5: reset the logo / images stamps.
    if (!out) {
        f.logoAt.set(-1);
        f.imagesAt.set(-1);
    }
    // Step 5: screen_fade_do(flag, alpha, durationMs) in the UI state, only
    // when the document id is not -1 and the global is a function.
    bool called = false;
    if (documentLoaded) {
        called = screenFadeCallUi("screen_fade_do", 3, static_cast<double>(flag), out ? 1.0 : 0.0,
                                  static_cast<double>(durationMs), nullptr);
        if (called) ++screenFadeCounters_.screenFadeDoCalls;
    }
    // Step 6: direction and in-flight callback (fade-in also posts an audio
    // id, HYPOTHESIS - not modelled).
    f.state.set(running);
    f.inFlight = cb;
    // HOST-SIDE SUBSTITUTE timer for this transition (see screenFadeHostFrame).
    screenFadeFallback_.armed = true;
    screenFadeFallback_.dueMs = now + durationMs;
    screenFadeFallback_.reason = called ? FadeFallbackReason::NoCallback : FadeFallbackReason::Undefined;
}

bool EngineState::screenFadeCompletionBody() {
    // 0x005a0110 (CONFIRMED, full listing). Takes nothing, returns nothing.
    ScreenFade& f = screenFade_;
    const int64_t now = screenFadeClockMs_;
    const uint32_t state = f.state.get();
    const uint32_t target = f.target.get();
    // 1. Flip by the current state; 2 and 3 are left alone; the target is not consulted.
    bool flipped = false;
    if (state == 0) {
        f.state.set(2);
        flipped = true;
    } else if (state == 1) {
        f.state.set(3);
        f.logoAt.set(now + 1000);
        f.holdLogoUntil.set(-1);
        f.imagesAt.set(-1);
        f.holdImagesUntil.set(-1);
        flipped = true;
    }
    // The transition the substitute timer was armed for has ended.
    if (flipped) screenFadeFallback_.armed = false;
    // 2. The in-flight callback with the target.
    if (f.inFlight) f.inFlight(*this, target);
    // 3. Settled at the target with a deferred callback: the same function as
    //    the in-flight one -> both cleared, return; otherwise call it with the
    //    target and clear it. (Slots and globals re-read after step 2.)
    if (f.deferred && f.state.get() == f.target.get()) {
        if (f.deferred == f.inFlight) {
            f.deferred = nullptr;
            f.inFlight = nullptr;
            return flipped;
        }
        FadeCallback d = f.deferred;
        d(*this, f.target.get());
        f.deferred = nullptr;
    }
    // 4.
    f.inFlight = nullptr;
    return flipped;
}

void EngineState::screenFadeCompletionNative() {
    if (screenFadeCompletionBody()) ++screenFadeCounters_.realCompletions;
}

void EngineState::screenFadeBroadcast(bool out) {
    // 0x005a0270 / 0x005a0400 (CONFIRMED): nothing unless the session exists
    // and +0x5c == +0x58. The opcode-0x53 record (direction bit, the overlay
    // colour for a fade-out, 16 bits of durationMs, 8 bits of 0) goes to the
    // peers; no networking here, so it is counted.
    if (!coopLocalIsHost()) return;
    ++screenFadeOpcode53Count_;
    screenFade_.lastBroadcastWasOut.set(out);
    replicateStateChange(out ? "fade_out_opcode_0x53" : "fade_in_opcode_0x53", "");
}

void EngineState::screenFadeFrameRoutine() {
    // 0x0059fe70 (CONFIRMED, full listing), in order.
    ScreenFade& f = screenFade_;
    const int64_t now = screenFadeClockMs_;

    // Auto-save indicator.
    const int32_t counter = f.autoSaveCounter.get();
    const int64_t saveStamp = f.autoSaveStamp.get();
    if (counter > 0 && saveStamp < 0) {
        f.autoSaveStamp.set(now + 3000);
        if (screenFadeCallUi("screen_fade_auto_save_show", 0, 0, 0, 0, nullptr)) ++screenFadeCounters_.uiScriptCalls;
    } else if (counter <= 0 && saveStamp >= 0 && stampReached(now, saveStamp, f.autoSaveStamp.global())) {
        if (screenFadeCallUi("screen_fade_auto_save_hide", 0, 0, 0, 0, nullptr)) ++screenFadeCounters_.uiScriptCalls;
        f.autoSaveStamp.set(-1);
    }

    // Mode gate (mode 4: meaning OPEN).
    if (f.modeStackTop.get() == 4) {
        f.logoAt.set(-1);
        f.holdLogoUntil.set(-1);
        f.imagesAt.set(-1);
        f.holdImagesUntil.set(-1);
        return;
    }

    // Loading logo.
    const int64_t logoAt = f.logoAt.get();
    if (logoAt >= 0 && stampReached(now, logoAt, f.logoAt.global())) {
        const int32_t cutscene = f.cutsceneState.get();
        if (cutscene < 10 || cutscene > 13) {
            const bool images = f.useLoadImages.get();
            f.logoAt.set(-1);
            f.holdLogoUntil.set(now + 1000);
            if (images) f.imagesAt.set(now + 6000);
            f.holdImagesUntil.set(-1);
            // Unless ids 0x35-0x37 are active (0x007b3ba0), an audio-id post
            // (HYPOTHESIS) - not modelled.
            if (screenFadeCallUi("screen_fade_logo_show", 0, 0, 0, 0, nullptr)) ++screenFadeCounters_.uiScriptCalls;
            return;
        }
    }

    // Holds.
    const int64_t holdLogo = f.holdLogoUntil.get();
    if (holdLogo >= 0 && !stampReached(now, holdLogo, f.holdLogoUntil.global())) return;
    const int64_t imagesAt = f.imagesAt.get();
    if (imagesAt >= 0 && stampReached(now, imagesAt, f.imagesAt.global())) {
        f.imagesAt.set(-1);
        f.holdImagesUntil.set(now + 1500);
        if (screenFadeCallUi("screen_fade_images_show", 0, 0, 0, 0, nullptr)) ++screenFadeCounters_.uiScriptCalls;
        return;
    }
    const int64_t holdImages = f.holdImagesUntil.get();
    if (holdImages >= 0 && !stampReached(now, holdImages, f.holdImagesUntil.global())) return;

    // Replay a parked request with a fixed 250 ms once the state has settled
    // at the opposite end.
    const uint32_t target = f.target.get();
    const uint32_t state = f.state.get();
    if (target == 2 && state == 3) {
        screenFadeRequest(false, 250, f.deferred, f.flag.get());
    } else if (target == 3 && state == 2) {
        screenFadeRequest(true, 250, f.deferred, f.flag.get());
    }
}

void EngineState::screenFadeHostFrame(int64_t elapsedMs) {
    screenFadeClockMs_ += elapsedMs;
    ++screenFadeCounters_.framesRun;
    // 1. HOST-SIDE SUBSTITUTE (not engine behaviour): complete a transition
    //    the UI script has not completed durationMs after it started.
    if (screenFadeFallback_.armed && screenFadeClockMs_ >= screenFadeFallback_.dueMs) {
        const FadeFallbackReason reason = screenFadeFallback_.reason;
        screenFadeFallback_.armed = false;
        try {
            if (screenFadeCompletionBody()) {
                if (reason == FadeFallbackReason::Undefined) ++screenFadeCounters_.fallbackUndefined;
                else ++screenFadeCounters_.fallbackNoCallback;
            }
        } catch (const OpenStateError& e) {
            ++screenFadeCounters_.framesBlockedOnOpen;
            screenFadeCounters_.lastFrameBlocker = e.global();
        }
    }
    // 2. The engine's per-frame routine.
    try {
        screenFadeFrameRoutine();
    } catch (const OpenStateError& e) {
        ++screenFadeCounters_.framesBlockedOnOpen;
        screenFadeCounters_.lastFrameBlocker = e.global();
    }
}

std::string EngineState::screenFadeCompletionPathSummary() const {
    const ScreenFadeCounters& c = screenFadeCounters_;
    return "real:" + std::to_string(c.realCompletions) + " fallback_undefined:" + std::to_string(c.fallbackUndefined) +
           " fallback_no_callback:" + std::to_string(c.fallbackNoCallback);
}

} // namespace sr3luahost
