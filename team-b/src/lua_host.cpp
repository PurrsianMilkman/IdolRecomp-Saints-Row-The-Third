#include "sr3luahost/host.h"

namespace sr3luahost {

namespace {

// Stock-library opening, spec-lua-bindings.md Sec16.1 creator step 3 /
// Sec16.4 and spec-lua-api-behaviour.md Sec26.27 (CONFIRMED - disassembly,
// jobs 20261001T021641-team-a-yduu and 20261001T114555-team-a-lgdz): the
// engine's state creator opens the base library and the `coroutine` table
// and NO other stock library - no math, string, table, io, os, debug or
// package in either state. Stock Lua 5.1's luaopen_base
// (third_party/lua51/src/lbaselib.c) installs the base functions, _G,
// _VERSION, ipairs/pairs and newproxy and then registers `coroutine`
// itself, so it is the whole opener. Run through lua_cpcall, as stock
// linit.c does, so a failure is a status code, never an error crossing the
// constructor's C++ frame.
// OPEN (spec-lua-bindings.md Sec13.1): the engine's base table at
// 0x01293410 has 23 entries against stock 5.1's 24; which stock entry is
// missing is not specced, so the stock set is kept.
int openEngineStockLibs(lua_State* L) {
    lua_pushcfunction(L, luaopen_base);
    lua_pushstring(L, "");
    lua_call(L, 1, 0);
    return 0;
}

} // namespace

Host::Host(const std::vector<RegisteredName>& allNames) {
    gameplay_ = luaL_newstate();
    ui_ = luaL_newstate();
    lua_cpcall(gameplay_, openEngineStockLibs, nullptr);
    lua_cpcall(ui_, openEngineStockLibs, nullptr);

    // thread_new/thread_yield/thread_kill/thread_check_done/thread_close -
    // real, extensively-called globals confirmed absent from BOTH the
    // tagged registration list (checked against the original 1,430-name
    // file and, same-day, the corrected 1,435-name file - still absent
    // from both) and game_lib.lua's own real text (see
    // thread_scheduler.h's own top comment). Registered into BOTH states:
    // no cluster/state assignment for these 5 names is confirmed anywhere
    // (they are not present in the tagged registration list this
    // constructor otherwise splits on), and game_lib.lua - loaded only
    // into the gameplay state - is not the only real caller (the
    // reconciliation census counting 94/113/85/33/1 distinct calling
    // scripts covers far more than one file), so registering into only
    // one state risked silently breaking scripts in the other rather than
    // letting them make forward progress. Stated plainly: which real
    // state(s) the real engine itself registers these into is UNCONFIRMED
    // - this is this project's own deliberate, documented choice, not a
    // measured fact.
    registerThreadScheduler(gameplay_, threadScheduler_, hitLog_, "gameplay");
    registerThreadScheduler(ui_, threadScheduler_, hitLog_, "ui");

    // The 13 names promoted to real, spec-confirmed behavior
    // (spec_confirmed_stubs.h/.cpp) are filtered OUT of the generic
    // tagged-list split below and registered separately, via
    // registerSpecConfirmedStubs(), into whichever single state their OWN
    // registered cluster tag names (unlike thread_scheduler.h's 5 names,
    // which are registered into BOTH states because they are absent from
    // the tagged list entirely and so have no real cluster tag to honor -
    // these 13 DO each have a real, pre-verified cluster tag in the
    // current tagged registration list, split here into two SEPARATE
    // per-state name lists - they do NOT all share one cluster - and
    // registered via two separate calls, one per state, each with only
    // that state's own real subset). This ordering (special registration
    // happens in the SAME pass as the generic split, before either
    // registerStubs() call below runs) means a plain lua_setglobal from
    // either path can never race/overwrite the other for the same name -
    // each of these 13 names is set exactly once, by exactly one of the
    // two registration functions, never both, and never into both states.
    const std::vector<std::string>& specNames = specConfirmedStubNames();
    auto isSpecConfirmed = [&specNames](const std::string& name) {
        for (const auto& n : specNames) if (n == name) return true;
        return false;
    };

    std::vector<RegisteredName> gameplayNames;
    std::vector<RegisteredName> uiNames;
    std::vector<std::string> specGameplayNames;
    std::vector<std::string> specUiNames;
    gameplayNames.reserve(allNames.size());
    uiNames.reserve(allNames.size());
    for (const auto& rn : allNames) {
        if (isSpecConfirmed(rn.name)) {
            if (rn.cluster == "ui") {
                specUiNames.push_back(rn.name);
            } else {
                specGameplayNames.push_back(rn.name);
            }
            continue;
        }
        if (rn.cluster == "ui") {
            uiNames.push_back(rn);
        } else {
            // "gameplay", or (not expected from the pre-verified file) any
            // other tag - fail safe into the larger, primary state rather
            // than silently dropping a name.
            gameplayNames.push_back(rn);
        }
    }

    // audio_object_post_event: real, CONFIRMED dual-registration (spec-lua-
    // bindings.md Sec13.5 addendum, 2026-09-30) - the SAME native function
    // (0x00a3cb00) is registered from two structurally disjoint call
    // chains, one inside the gameplay-state bring-up path, one inside the
    // UI-state bring-up path - not one function registered once and
    // miscounted twice. The tagged registration list (built on a "one name,
    // one registrar call site" assumption) only ever recorded the first one
    // found and tags it "gameplay" - real, but incomplete. Real-world
    // corroboration: real shipped vint_lib.lua (UI-preloaded, spec Sec16)
    // calls this at its own line 483 unconditionally - the confirmed cause
    // of a real population of nil-global hook-fire errors (HANDOFF.md
    // Sec9.131). Added into uiNames here, alongside the gameplay entry the
    // main split loop above already placed it in - a targeted fix for this
    // one name only, not a change to the tagged file or its own "one
    // cluster per name" format. 7 sibling names share the tagged list's own
    // "8 overlaps" note but were NOT individually re-checked by Team A -
    // deliberately NOT extended to them; each needs its own xref-exhaustive
    // check before being treated as dual-registered too.
    uiNames.push_back({"audio_object_post_event", "ui"});

    // The remaining 7 of the "8 overlaps" (spec-lua-bindings.md Sec13.5)
    // - resolved 2026-09-30, prompted by the coordinator peer relaying
    // Team A's own follow-up confirmation, checked directly against the
    // spec text myself before acting: all 7 are genuinely dual-registered
    // (real raw-array cross-check, both registrars, identical function
    // pointer each - NOT string-pooling coincidence), the exact same shape
    // already resolved for audio_object_post_event above. Each is
    // currently tagged "gameplay" in tools/lua_all_registered_1490_tagged
    // .txt (verified directly, not assumed) and so already lands in
    // gameplayNames/specGameplayNames via the split loop above; this block
    // ALSO registers each into the UI state, the same targeted-fix pattern
    // as audio_object_post_event (not an edit to the tagged file itself,
    // which keeps its own "one name, one registrar call site found"
    // convention intact).
    //
    // 6 of the 7 (game_is_active_input_gamepad, hud_display_set_element,
    // audio_stop, hud_display_create_state, hud_display_commit_state,
    // hud_display_remove_state) are generic logging stubs, not among the
    // 22 spec-confirmed names above - added to uiNames directly, same as
    // audio_object_post_event.
    uiNames.push_back({"game_is_active_input_gamepad", "ui"});
    uiNames.push_back({"hud_display_set_element", "ui"});
    uiNames.push_back({"audio_stop", "ui"});
    uiNames.push_back({"hud_display_create_state", "ui"});
    uiNames.push_back({"hud_display_commit_state", "ui"});
    uiNames.push_back({"hud_display_remove_state", "ui"});

    // coop_is_active IS one of the 22 spec-confirmed names (real behavior,
    // spec-lua-api-behaviour.md Sec3.1) - it was already filtered out of
    // the generic split loop above (isSpecConfirmed("coop_is_active") ==
    // true) and placed into specGameplayNames only, per its own tagged
    // "gameplay" cluster. Registering it into the UI state too means
    // adding it to specUiNames here (NOT uiNames - the generic path never
    // sees this name at all), so registerSpecConfirmedStubs(ui_, ...)
    // below also binds the same real stub_coop_is_active implementation
    // into the UI state (it already reads its own stateTag as an upvalue
    // per-call, so it reports "ui" correctly there, not hardcoded
    // "gameplay").
    specUiNames.push_back("coop_is_active");

    // Bare globals outside the tagged list (Sec13.2), into the state(s) the spec names.
    for (const auto& bg : specBareGlobals()) {
        if (bg.gameplay) specGameplayNames.push_back(bg.name);
        if (bg.ui) specUiNames.push_back(bg.name);
    }
    applySpecInitialState(engineState_);
    registerSpecConfirmedStubs(gameplay_, engineState_, hitLog_, "gameplay", specGameplayNames);
    registerSpecConfirmedStubs(ui_, engineState_, hitLog_, "ui", specUiNames);
    registerStubs(gameplay_, gameplayNames, hitLog_, "gameplay");
    registerStubs(ui_, uiNames, hitLog_, "ui");
    gameplayStubCount_ = gameplayNames.size() + specGameplayNames.size();
    uiStubCount_ = uiNames.size() + specUiNames.size();
}

Host::~Host() {
    if (gameplay_) lua_close(gameplay_);
    if (ui_) lua_close(ui_);
}

Host::RunResult Host::runChunk(lua_State* L, const std::string& source, const std::string& chunkName) {
    RunResult r;
    // Lua 5.1's own public API has no LUA_OK constant (that's a 5.2+
    // addition) - 0 is the documented success value for both
    // luaL_loadbuffer and lua_pcall in this version.
    int loadStatus = luaL_loadbuffer(L, source.data(), source.size(), chunkName.c_str());
    if (loadStatus != 0) {
        const char* msg = lua_tostring(L, -1);
        r.loadOk = false;
        r.loadError = msg ? msg : "(no error message)";
        lua_pop(L, 1);
        return r;
    }
    r.loadOk = true;
    r.ranPcall = true;
    int callStatus = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (callStatus != 0) {
        const char* msg = lua_tostring(L, -1);
        r.pcallOk = false;
        r.pcallError = msg ? msg : "(no error message)";
        lua_pop(L, 1);
    } else {
        r.pcallOk = true;
    }
    // Clear the stack (return values, if any) so the next chunk run
    // against this same persistent state starts clean. Globals live in
    // LUA_GLOBALSINDEX, not the stack, so this never touches anything a
    // previous script defined.
    lua_settop(L, 0);
    return r;
}

namespace {

// Real, unmodified Lua 5.1 debug-API count hook (lua_sethook +
// LUA_MASKCOUNT - see host.h's own doc comment on
// Host::fireConfirmedHooks for why this exists). Fires once after the
// given instruction count elapses inside ONE lua_pcall; luaL_error()
// inside a hook is documented, legal Lua 5.1 behavior (an error raised
// from a debug hook unwinds exactly like any other Lua error, which
// lua_pcall then reports through the ordinary error path) - not an
// invented mechanism.
constexpr int kHookWatchdogInstructionBudget = 5'000'000;

void watchdogHook(lua_State* L, lua_Debug*) {
    luaL_error(L, "sr3luahost watchdog: hook call exceeded the %d-instruction budget "
                  "(likely an infinite loop against an always-nil stub, e.g. "
                  "'repeat thread_yield() until some_stub()' where some_stub() always "
                  "returns nil/falsy) - aborted by this tool's own safety hook, not hung",
               kHookWatchdogInstructionBudget);
}

} // namespace

std::vector<Host::HookFireResult> Host::fireConfirmedHooks(lua_State* L, const std::vector<HookSpec>& hooks,
                                                            const std::string& scriptFilename) {
    std::vector<HookFireResult> out;
    out.reserve(hooks.size());

    for (const auto& spec : hooks) {
        HookFireResult r;
        r.hookName = spec.name;
        r.group = spec.group;

        // The real, confirmed existence check (spec Sec8.3): fetch the
        // global, check it is really a function. Measured for EVERY
        // group, including Group 2 (see hook_registry.h) - but only
        // Group 1/3 use it to GATE the call below.
        lua_getglobal(L, spec.name.c_str());
        r.existedAsFunction = (lua_type(L, -1) == LUA_TFUNCTION);
        lua_pop(L, 1);

        bool isUnconditionalGroup = (spec.group == HookGroup::Group2_LifecycleUnconditional);
        if (!isUnconditionalGroup && !r.existedAsFunction) {
            // Group 1/3, not defined: this IS the real engine's own
            // confirmed behavior (spec Sec8.3's silent existence check) -
            // not called, no error. Correctly matches real behavior
            // rather than being a harness artifact.
            out.push_back(std::move(r));
            continue;
        }

        // Fresh reference as the real call target (the one popped above
        // is gone). spec.args is empty for every hook except the small
        // set given a real, spec-confirmed argument contract (spec-lua-
        // bindings.md Sec14) - see HookSpec::args's own doc comment
        // (hook_registry.h). Each HookArg is pushed here, in order, using
        // its own real value: a fixed literal (number/string), or, for
        // HookArgKind::ScriptFilenameString, this call's own
        // `scriptFilename` parameter - the real per-script filename this
        // firing pass is currently running against, never a fabricated
        // placeholder.
        lua_getglobal(L, spec.name.c_str());
        r.attemptedCall = true;

        for (const auto& arg : spec.args) {
            switch (arg.kind) {
                case HookArgKind::LiteralNumber:
                    lua_pushnumber(L, arg.numberValue);
                    break;
                case HookArgKind::LiteralString:
                    lua_pushstring(L, arg.stringValue.c_str());
                    break;
                case HookArgKind::ScriptFilenameString:
                    lua_pushstring(L, scriptFilename.c_str());
                    break;
            }
        }

        lua_sethook(L, watchdogHook, LUA_MASKCOUNT, kHookWatchdogInstructionBudget);
        int callStatus = lua_pcall(L, static_cast<int>(spec.args.size()), LUA_MULTRET, 0);
        lua_sethook(L, nullptr, 0, 0); // always clear, regardless of outcome - never leaves the watchdog armed for a later, unrelated call

        if (callStatus != 0) {
            const char* msg = lua_tostring(L, -1);
            r.callOk = false;
            r.callError = msg ? msg : "(no error message)";
            lua_pop(L, 1);
        } else {
            r.callOk = true;
        }
        lua_settop(L, 0); // clear any return values, same convention as runChunk() above
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace sr3luahost
