// Synthetic tests for sr3luahost (include/sr3luahost/{lua_c_api,
// stub_registry, host}.h). Every fixture below is small and hand-written -
// no real shipped game data or real registration-list file is read here
// (that is exercised for real by tools/lua_host_run.cpp against the
// current real tagged registration list - originally
// tools/lua_all_registered_1430_tagged.txt, then
// tools/lua_all_registered_1435_tagged.txt, superseded again 2026-09-29
// (same day, later pass) by tools/lua_all_registered_1490_tagged.txt
// (the 55-name vint_* registrar, spec-lua-bindings.md Sec13.7) - and the
// real archives; this suite only proves the scaffold's own mechanics:
// parsing the tagged format, registering stubs into the right state, the
// stub's own logging contract, real luaL_loadbuffer/lua_pcall outcomes for
// both a clean script and a genuinely broken one, and the 22 names
// promoted from a generic logged-nil stub to real, spec-confirmed
// behavior (13 from the original pass + 9 from a same-shape follow-up
// pass, spec-lua-api-behaviour.md Sec10.1-Sec10.9) - see
// spec_confirmed_stubs.h/engine_state.h).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"         \
                      << __LINE__ << "\n";                                  \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

using sr3luahost::confirmedHooks;
using sr3luahost::EngineState;
using sr3luahost::HitLog;
using sr3luahost::HookGroup;
using sr3luahost::HookSpec;
using sr3luahost::Host;
using sr3luahost::RegisteredName;
using sr3luahost::loadTaggedRegistrationList;
using sr3luahost::registerStubs;

// One real (name, cluster) row per one of the 22 names spec_confirmed_stubs.cpp
// implements, mirroring the exact tags this project's own
// tools/lua_all_registered_1490_tagged.txt carries (grepped directly, not
// guessed) - see this file's own new test section below for why each
// fixture needs its target name present with its OWN real cluster tag,
// exactly like the real tagged list, to exercise Host's real per-state
// split.
std::vector<RegisteredName> specConfirmedFixtureNames() {
    return {
        {"coop_is_active", "gameplay"},
        {"game_get_key_name", "ui"},
        {"game_UI_audio_play", "ui"},
        {"game_audio_get_audio_id", "ui"},
        {"game_get_key_name_for_action", "ui"},
        {"game_peg_load_with_cb", "ui"},
        {"ai_add_enemy_target", "gameplay"},
        {"on_take_damage", "gameplay"},
        {"set_ignore_ai_flag", "gameplay"},
        {"get_max_hit_points", "gameplay"},
        {"set_current_hit_points", "gameplay"},
        {"ai_clear_scripted_action", "gameplay"},
        {"vint_object_find", "ui"},
        // The 9-function follow-up pass (spec-lua-api-behaviour.md
        // Sec10.1-Sec10.9) - real cluster tags grepped directly from
        // tools/lua_all_registered_1490_tagged.txt (lines 494/580/619/
        // 622/862/1035/1272/1344/1387).
        {"store_vehicle_get_state", "ui"},
        {"Completion_is_client", "ui"},
        {"game_hud_update_inventory", "ui"},
        {"tutorial_advance", "gameplay"},
        {"minimap_icon_add_do", "gameplay"},
        {"object_indicator_add_do", "gameplay"},
        {"on_qte_animation_trigger", "gameplay"},
        {"on_revived", "gameplay"},
        {"game_get_coop_join_type", "ui"},
    };
}

} // namespace

int main() {
    // --- loadTaggedRegistrationList: the real tab-separated format -------
    {
        std::string path = "synthetic_luahost_test_taglist.tmp.txt";
        {
            std::ofstream f(path);
            f << "waypoint_add\tgameplay\n";
            f << "garage_remove_vehicle\tui\n";
            f << "\n";                 // blank line - must be skipped
            f << "malformed_no_tab\n"; // no tab - must be skipped, not throw
            f << "trailing_cr\tgameplay\r\n";
        }
        auto names = loadTaggedRegistrationList(path);
        std::remove(path.c_str());
        CHECK(names.size() == 3);
        CHECK(names[0].name == "waypoint_add" && names[0].cluster == "gameplay");
        CHECK(names[1].name == "garage_remove_vehicle" && names[1].cluster == "ui");
        CHECK(names[2].name == "trailing_cr" && names[2].cluster == "gameplay"); // CRLF stripped
    }

    // --- Host: gameplay/UI split, and the stub's own logging contract ----
    {
        std::vector<RegisteredName> names = {
            {"waypoint_add", "gameplay"},
            {"garage_remove_vehicle", "ui"},
        };
        Host host(names);
        CHECK(host.gameplayStubCount() == 1);
        // 1 (garage_remove_vehicle) + 8 real, unconditionally dual-
        // registered names (spec-lua-bindings.md Sec13.5's own "8
        // overlaps": audio_object_post_event, game_is_active_input_gamepad,
        // hud_display_set_element, audio_stop, hud_display_create_state,
        // hud_display_commit_state, hud_display_remove_state - all 7 via
        // uiNames - plus coop_is_active via specUiNames) - lua_host.cpp's
        // own Host constructor registers these into BOTH states
        // unconditionally, independent of whether this minimal fixture
        // even mentions them, because the real engine itself does (this is
        // a real, disassembly-confirmed fact about the binary, not a
        // property of any particular input list) = 9.
        CHECK(host.uiStubCount() == 9);

        // A gameplay-tagged name is a real Lua function in the gameplay
        // state, and simply absent (nil) in the UI state - proving the
        // split is real, not just a shared roster registered twice.
        {
            auto r = host.runChunk(host.gameplayState(), "return type(waypoint_add)", "chunk1");
            CHECK(r.loadOk && r.pcallOk);
        }
        {
            auto r = host.runChunk(host.uiState(), "assert(waypoint_add == nil)", "chunk2");
            CHECK(r.loadOk && r.pcallOk);
        }
        {
            auto r = host.runChunk(host.gameplayState(), "assert(garage_remove_vehicle == nil)", "chunk3");
            CHECK(r.loadOk && r.pcallOk);
        }

        // Calling the stub: real argc, real arg types (via lua_type), and
        // a real nil return - the task's exact specified stub contract,
        // checked from the Lua side of the real C API round-trip.
        {
            auto r = host.runChunk(host.gameplayState(),
                                    "local a, b, c = waypoint_add(1, 'x', true)\n"
                                    "assert(a == nil and b == nil and c == nil)",
                                    "chunk4");
            CHECK(r.loadOk && r.pcallOk);
        }
        CHECK(host.hitLog().hits().count("waypoint_add") == 1);
        CHECK(host.hitLog().hits().at("waypoint_add").callCount == 1);
        CHECK(host.hitLog().hits().at("waypoint_add").lastArgc == 3);
        CHECK(host.hitLog().hits().at("waypoint_add").lastArgTypes == "number,string,boolean");
        // Real per-state tracking (added same-day follow-up pass): a call
        // made against the gameplay state must be tallied under
        // gameplayCallCount, not uiCallCount - measured directly at the
        // real call site, never inferred from waypoint_add's own
        // registered cluster tag ("gameplay" here, but that's incidental
        // to this check).
        CHECK(host.hitLog().hits().at("waypoint_add").gameplayCallCount == 1);
        CHECK(host.hitLog().hits().at("waypoint_add").uiCallCount == 0);

        // Calling it again accumulates the hit count (a real, whole-run
        // aggregate, not a per-call snapshot).
        {
            auto r = host.runChunk(host.gameplayState(), "waypoint_add()", "chunk5");
            CHECK(r.loadOk && r.pcallOk);
        }
        CHECK(host.hitLog().hits().at("waypoint_add").callCount == 2);
        CHECK(host.hitLog().hits().at("waypoint_add").lastArgc == 0);
        CHECK(host.hitLog().hits().at("waypoint_add").lastArgTypes.empty());
    }

    // --- runChunk: a real syntax error and a real runtime error -----------
    {
        std::vector<RegisteredName> names; // no stubs needed for this fixture
        Host host(names);

        // Real Lua 5.1 syntax error - luaL_loadbuffer must fail, with a
        // real message, and lua_pcall must never even be attempted.
        {
            auto r = host.runChunk(host.gameplayState(), "function broken(", "bad_syntax.lua");
            CHECK(!r.loadOk);
            CHECK(!r.loadError.empty());
            CHECK(!r.ranPcall);
        }

        // Loads fine, but errors at runtime (calling a nil global) - a
        // real, distinct lua_pcall failure, with the loaded chunk's own
        // name/line real in the message.
        {
            auto r = host.runChunk(host.gameplayState(), "this_name_is_not_defined_anywhere()", "bad_runtime.lua");
            CHECK(r.loadOk);
            CHECK(r.ranPcall);
            CHECK(!r.pcallOk);
            CHECK(!r.pcallError.empty());
        }

        // A persistent state really does persist: a global defined by one
        // chunk is visible to the next chunk run against the SAME state -
        // the real engine's own "load game_lib.lua once, then run mission
        // scripts against the same state" model this scaffold follows.
        {
            auto r1 = host.runChunk(host.gameplayState(), "GLOBAL_FROM_EARLIER_SCRIPT = 42", "first.lua");
            CHECK(r1.loadOk && r1.pcallOk);
            auto r2 = host.runChunk(host.gameplayState(), "assert(GLOBAL_FROM_EARLIER_SCRIPT == 42)", "second.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }
    }

    // --- fireConfirmedHooks: existence-gated Group 1/3 behavior ----------
    {
        std::vector<RegisteredName> names; // no stubs needed
        Host host(names);
        lua_State* L = host.gameplayState();

        // Not defined at all -> not called, no error (matches spec
        // Sec8.3's silent existence check).
        {
            std::vector<HookSpec> hooks = {{"not_defined_hook_xyz", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results.size() == 1);
            CHECK(!results[0].existedAsFunction);
            CHECK(!results[0].attemptedCall);
        }

        // Defined, runs clean.
        {
            auto r = host.runChunk(L, "function my_ok_hook() GLOBAL_HOOK_RAN = true end", "def1.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"my_ok_hook", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results.size() == 1);
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
            auto check = host.runChunk(L, "assert(GLOBAL_HOOK_RAN == true)", "check1.lua");
            CHECK(check.loadOk && check.pcallOk);
        }

        // Defined, errors at runtime - a real, distinct failure.
        {
            auto r = host.runChunk(L, "function my_bad_hook() error('deliberate test error') end", "def2.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"my_bad_hook", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(!results[0].callOk);
            CHECK(results[0].callError.find("deliberate test error") != std::string::npos);
        }

        // Defined as a non-function global (e.g. a number) - must NOT be
        // treated as existing (real lua_isfunction check), so Group 1/3
        // correctly skips it rather than erroring trying to call a number.
        {
            auto r = host.runChunk(L, "my_non_function_hook = 42", "def3.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"my_non_function_hook", HookGroup::Group1_GeneralNamed, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(!results[0].existedAsFunction);
            CHECK(!results[0].attemptedCall);
        }
    }

    // --- fireConfirmedHooks: Group 2's real UNCONDITIONAL call (spec
    // Sec12.4) - existence is measured, but the call is attempted
    // regardless, unlike Group 1/3. -------------------------------------
    {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();

        // Not defined: existence check false, but the call is still
        // ATTEMPTED (real engine's own unconditional-call behavior, spec
        // Sec12.4) and fails with a real Lua error - not silently skipped
        // like Group 1/3 would.
        {
            std::vector<HookSpec> hooks = {{"_NotDefinedLifecycleHook", HookGroup::Group2_LifecycleUnconditional, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results.size() == 1);
            CHECK(!results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(!results[0].callOk);
            CHECK(!results[0].callError.empty());
        }

        // Defined: existence true, call attempted and succeeds.
        {
            auto r = host.runChunk(L, "function _DefinedLifecycleHook() end", "def4.lua");
            CHECK(r.loadOk && r.pcallOk);
            std::vector<HookSpec> hooks = {{"_DefinedLifecycleHook", HookGroup::Group2_LifecycleUnconditional, {}}};
            auto results = host.fireConfirmedHooks(L, hooks);
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
        }
    }

    // --- fireConfirmedHooks: the instruction-count watchdog really
    // aborts a runaway hook instead of hanging the caller (this project's
    // own real, found risk - real game_lib.lua text has patterns like
    // `repeat thread_yield() until <always-nil-returning stub>`, and
    // thread_yield is a genuine no-op on the main thread, so such a loop
    // would otherwise spin forever). --------------------------------
    {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();
        auto r = host.runChunk(L, "function my_infinite_hook() while true do end end", "def5.lua");
        CHECK(r.loadOk && r.pcallOk);
        std::vector<HookSpec> hooks = {{"my_infinite_hook", HookGroup::Group1_GeneralNamed, {}}};
        auto results = host.fireConfirmedHooks(L, hooks);
        CHECK(results.size() == 1);
        CHECK(results[0].existedAsFunction);
        CHECK(results[0].attemptedCall);
        CHECK(!results[0].callOk); // the watchdog must have aborted it, not hung this test
        CHECK(results[0].callError.find("watchdog") != std::string::npos);

        // The state must still be perfectly usable afterward - the
        // watchdog's own error must not leave anything corrupted (its
        // lua_sethook is always cleared, the stack is always settled).
        auto after = host.runChunk(L, "assert(1 + 1 == 2)", "after_watchdog.lua");
        CHECK(after.loadOk && after.pcallOk);
    }

    // --- thread_* scheduler: real Lua 5.1 coroutine primitives, minimal
    // documented scope (thread_scheduler.h). -----------------------------
    {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();

        // thread_new returns a real Lua number (matches real game_lib.lua
        // call-site usage: `type(threads) == "number"`, grepped directly).
        auto r1 = host.runChunk(L,
            "H = thread_new('some_engine_routine', 'arg1')\n"
            "assert(type(H) == 'number')",
            "thread1.lua");
        CHECK(r1.loadOk && r1.pcallOk);

        // thread_check_done: this pass's own minimal thread_new() never
        // attaches a body and nothing ever resumes it, so by the real Lua
        // 5.1 status/stack/top idiom this reads as done immediately - see
        // ThreadScheduler::isDone()'s own doc comment.
        auto r2 = host.runChunk(L, "assert(thread_check_done(H) == true)", "thread2.lua");
        CHECK(r2.loadOk && r2.pcallOk);

        // thread_kill/thread_close: real calls, no error, idempotent, and
        // silently no-op on an unknown handle.
        auto r3 = host.runChunk(L,
            "thread_kill(H)\n"
            "thread_close(H)\n"        // identical op (see header note) - must not error when called again
            "thread_kill(99999999)",   // unknown handle - silent no-op, must not error
            "thread3.lua");
        CHECK(r3.loadOk && r3.pcallOk);

        // thread_yield: real no-op on the main thread (no active
        // coroutine) - must not error or hang.
        auto r4 = host.runChunk(L, "thread_yield()", "thread4.lua");
        CHECK(r4.loadOk && r4.pcallOk);

        // All 5 thread_* calls above must fold into the SAME HitLog
        // ranking every other generic stub uses (host.h/stub_registry.h).
        CHECK(host.hitLog().hits().count("thread_new") == 1);
        CHECK(host.hitLog().hits().count("thread_check_done") == 1);
        CHECK(host.hitLog().hits().count("thread_kill") == 1);
        CHECK(host.hitLog().hits().count("thread_close") == 1);
        CHECK(host.hitLog().hits().count("thread_yield") == 1);
        CHECK(host.hitLog().hits().at("thread_kill").callCount == 2); // thread_kill(H) + thread_kill(99999999)
    }

    // --- confirmedHooks(): the real 138-entry static table (133 original
    // + 5 more Group-1 names, spec Sec14.6, same-day follow-up), sanity-
    // checked structurally (not re-deriving the spec's own text here -
    // just proving the shipped table is well-formed: the right total, the
    // right per-group split, every name non-empty, unique, and every
    // args list EMPTY except the small, explicitly-enumerated set spec
    // Sec14 gives a real argument contract for). -------------------------
    {
        const auto& hooks = confirmedHooks();
        CHECK(hooks.size() == 138);
        // Real, spec-confirmed (Sec14) per-hook argument counts - every
        // OTHER hook not listed here must have an EMPTY args list.
        std::unordered_map<std::string, size_t> expectedArgCount = {
            {"_PrepareForDynamicGlobals", 1},
            {"cmp_common_screen_start", 1},
            {"hud_running_man_event_update", 1},
        };
        size_t g1 = 0, g2 = 0, g3 = 0;
        std::vector<std::string> allNames;
        for (auto& h : hooks) {
            CHECK(!h.name.empty());
            auto it = expectedArgCount.find(h.name);
            if (it != expectedArgCount.end()) {
                CHECK(h.args.size() == it->second);
            } else {
                CHECK(h.args.empty());
            }
            allNames.push_back(h.name);
            if (h.group == HookGroup::Group1_GeneralNamed) ++g1;
            else if (h.group == HookGroup::Group2_LifecycleUnconditional) ++g2;
            else if (h.group == HookGroup::Group3_MissionNumberedPatternBased) ++g3;
        }
        CHECK(g1 == 78); // 73 original + 5 new (spec Sec14.6)
        CHECK(g2 == 2);
        CHECK(g3 == 58);
        std::sort(allNames.begin(), allNames.end());
        CHECK(std::adjacent_find(allNames.begin(), allNames.end()) == allNames.end()); // no duplicates

        // The 5 new Sec14.6 names are really present and really Group 1.
        for (const char* name : {"store_weapon_uncover_weapon", "store_gallery_upload_complete",
                                  "store_gallery_download_list_complete",
                                  "screen_capture_open_preview_dialog", "dialog_build"}) {
            CHECK(std::find(allNames.begin(), allNames.end(), std::string(name)) != allNames.end());
        }
    }

    // --- fireConfirmedHooks: real argument pushing (spec Sec14) - a
    // literal number, a literal string, and the real per-call
    // scriptFilename context value for HookArgKind::ScriptFilenameString.
    // ------------------------------------------------------------------
    {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();

        // A literal number arg really arrives as a real Lua number.
        {
            auto r = host.runChunk(L, "function my_number_hook(x) assert(x == 0.0) end", "defn.lua");
            CHECK(r.loadOk && r.pcallOk);
            HookSpec spec;
            spec.name = "my_number_hook";
            spec.group = HookGroup::Group1_GeneralNamed;
            spec.args = {sr3luahost::HookArg::number(0.0)};
            auto results = host.fireConfirmedHooks(L, {spec});
            CHECK(results[0].existedAsFunction);
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
        }

        // ScriptFilenameString really pushes the real scriptFilename this
        // call was given - not a placeholder, not empty (when a real one
        // is supplied).
        {
            auto r = host.runChunk(L, "function my_filename_hook(name) assert(name == 'real_script.lua') end", "defs.lua");
            CHECK(r.loadOk && r.pcallOk);
            HookSpec spec;
            spec.name = "my_filename_hook";
            spec.group = HookGroup::Group2_LifecycleUnconditional;
            spec.args = {sr3luahost::HookArg::scriptFilename()};
            auto results = host.fireConfirmedHooks(L, {spec}, "real_script.lua");
            CHECK(results[0].attemptedCall);
            CHECK(results[0].callOk);
        }
    }

    // --- EngineState::multiply33XorHashBucket(): the real, CONFIRMED,
    // empirically vector-validated "multiply-33/XOR" hash
    // (spec-texture-format.md Sec8.2), cross-confirmed by
    // spec-lua-api-behaviour.md Sec5.3's own 5 real I/O vectors -
    // reproduced here exactly, byte-for-byte against those documented
    // vectors, not just "some hash." -----------------------------------
    {
        CHECK(EngineState::multiply33XorHashBucket("Tint_color", 0x20) == 0x5);
        CHECK(EngineState::multiply33XorHashBucket("continuous_spawn_start", 0x20) == 0x16);
        CHECK(EngineState::multiply33XorHashBucket("", 0x20) == 0x0);
        CHECK(EngineState::multiply33XorHashBucket("SAINTS_ROW_THE_THIRD", 0x100) == 0x7d);
        CHECK(EngineState::multiply33XorHashBucket("a", 0x20) == 0x1);
    }

    // --- The 12 spec-confirmed names promoted from a generic logged-nil
    // stub to real, spec-confirmed behavior (spec_confirmed_stubs.h/.cpp),
    // plus vint_object_find (folded in mid-task, spec-lua-bindings.md
    // Sec13.7/Sec15), plus 9 more (functions 14-22, spec-lua-api-
    // behaviour.md Sec10.1-Sec10.9, a same-shape follow-up pass) - one
    // real test per function, each derived from that function's own spec
    // section, exercised through the real Lua-visible global (never by
    // calling the C++ trampoline directly) so this is a genuine
    // round-trip through the real Lua 5.1 C API. Host is built with
    // specConfirmedFixtureNames() standing in for the real tagged
    // registration list's own 22 rows (same real cluster tags) - Host's
    // own real per-name-cluster split (lua_host.cpp) is exercised for
    // real, not bypassed. -------------------------------------------
    {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();

        // Real per-state placement: 21 of the 22 names are a real function
        // in EXACTLY the state their own cluster tag names, nil in the
        // other - proves the per-name-cluster-aware special registration
        // path (lua_host.cpp) actually works, not just "some state got
        // it." coop_is_active is the one confirmed exception (spec-lua-
        // bindings.md Sec13.5, resolved 2026-09-30): real, unconditional
        // dual-registration into BOTH states - checked as a real function
        // in both below, not nil in either.
        {
            auto r1 = host.runChunk(gp, "assert(type(coop_is_active) == 'function')", "spec1.lua");
            CHECK(r1.loadOk && r1.pcallOk);
            auto r2 = host.runChunk(ui, "assert(type(coop_is_active) == 'function')", "spec2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            auto r3 = host.runChunk(ui, "assert(type(game_get_key_name) == 'function')", "spec3.lua");
            CHECK(r3.loadOk && r3.pcallOk);
            auto r4 = host.runChunk(gp, "assert(game_get_key_name == nil)", "spec4.lua");
            CHECK(r4.loadOk && r4.pcallOk);
        }

        // 1. coop_is_active (Sec3.1): default false; EngineState::setCoopActive()
        // is a C++-only test entry point (no Lua setter among these 13).
        {
            auto r = host.runChunk(gp, "assert(coop_is_active() == false)", "coop1.lua");
            CHECK(r.loadOk && r.pcallOk);
            es.setCoopActive(true);
            auto r2 = host.runChunk(gp, "assert(coop_is_active() == true)", "coop2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }

        // 2. game_get_key_name (Sec2.3): CONFIRMED sentinel -1 -> "PC_UNBOUND_KEY".
        // General path: a real GetKeyNameTextW call - only asserted non-empty
        // (real OS/locale-dependent content, not asserted byte-exact).
        {
            auto r = host.runChunk(ui, "assert(game_get_key_name(-1) == 'PC_UNBOUND_KEY')", "keyname1.lua");
            CHECK(r.loadOk && r.pcallOk);
            // 0x1E is the real DirectInput scan code for 'A'
            // (spec-tables-ui-controls.md Sec4.3) - a real, ordinary key,
            // not the sentinel.
            auto r2 = host.runChunk(ui,
                "local n = game_get_key_name(0x1E)\n"
                "assert(type(n) == 'string' and #n > 0)",
                "keyname2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }

        // 3. game_UI_audio_play (Sec2.2): a handle is ALWAYS returned,
        // strictly increasing across calls (this project's own chosen
        // stand-in - see EngineState::nextAudioVoiceHandle's own doc
        // comment), regardless of whether a sound argument was given.
        {
            auto r = host.runChunk(ui,
                "local h1 = game_UI_audio_play('some_sound')\n"
                "local h2 = game_UI_audio_play()\n"
                "assert(type(h1) == 'number' and type(h2) == 'number' and h2 > h1)",
                "audioplay1.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // 4. game_audio_get_audio_id (Sec8.22): CONFIRMED number path
        // (round-then-mask-to-16-bits), CONFIRMED absent->0, CONFIRMED
        // "none"/"" sentinel ->0.
        {
            auto r = host.runChunk(ui,
                "assert(game_audio_get_audio_id(70000.4) == (70000 % 65536))\n" // round-to-nearest then mask
                "assert(game_audio_get_audio_id() == 0)\n"
                "assert(game_audio_get_audio_id('none') == 0)\n"
                "assert(game_audio_get_audio_id('') == 0)\n"
                "assert(game_audio_get_audio_id('NoNe') == 0)", // case-insensitive
                "audioid1.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // 5. game_get_key_name_for_action (Sec8.23): CONFIRMED literal
        // CAA_CAMERA_ROTATE -> STR_THE_MOUSE (case-insensitive match, real
        // __stricmp-equivalent contract); CONFIRMED "no match" -> "".
        {
            auto r = host.runChunk(ui,
                "assert(game_get_key_name_for_action('CAA_CAMERA_ROTATE') == 'STR_THE_MOUSE')\n"
                "assert(game_get_key_name_for_action('caa_camera_rotate') == 'STR_THE_MOUSE')\n"
                "assert(game_get_key_name_for_action('CBA_SOME_UNKNOWN_ACTION') == '')",
                "actionname1.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // 6. game_peg_load_with_cb (Sec8.24): CONFIRMED arg-reading
        // contract (name bounds-checked, count clamped 1..6, filenames
        // read while present) plus this task's own real, CONFIRMED-hash
        // bucket-index cross-check (bucket_count=9000, same routine
        // tested directly above).
        {
            auto r = host.runChunk(ui, "game_peg_load_with_cb('req1', 2, 'tex_a.cvbm_pc', 'tex_b.cvbm_pc')", "peg1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.pegLoadRequests().size() == 1);
            const auto& req = es.pegLoadRequests().back();
            CHECK(req.requestName == "req1");
            CHECK(req.filenames.size() == 2);
            CHECK(req.filenames[0].name == "tex_a.cvbm_pc");
            CHECK(req.filenames[0].bucketIndex == EngineState::multiply33XorHashBucket("tex_a.cvbm_pc", 9000));
            CHECK(req.filenames[1].name == "tex_b.cvbm_pc");

            // Count out of the confirmed 1..6 range -> no filenames read,
            // even though more string args were supplied.
            auto r2 = host.runChunk(ui, "game_peg_load_with_cb('req2', 0, 'should_not_be_read.cvbm_pc')", "peg2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.pegLoadRequests().size() == 2);
            CHECK(es.pegLoadRequests().back().filenames.empty());
        }

        // 7. ai_add_enemy_target (Sec3.9): the #CLOSEST_PLAYER# sentinel
        // bit, arg4's bit, banker's-rounded priority, and the always-true
        // resolve-success return (this project's own minimal registry -
        // see EnemyTargetRecord's own doc comment).
        {
            auto r = host.runChunk(gp,
                "local ok = ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 5, true)\n"
                "assert(ok == true)",
                "enemy1.lua");
            CHECK(r.loadOk && r.pcallOk);
            auto& actor = es.getOrCreateCharacter("villain01");
            CHECK(actor.enemyTargets.count("5") == 1);
            const auto& rec = actor.enemyTargets.at("5");
            CHECK(rec.priorityOrId == 5);
            CHECK(rec.arg4Flag == true);
            CHECK(rec.sentinelMatchFlag == true);
        }

        // 8. on_take_damage (Sec3.13): the callback name lands on the
        // resolved target's own field.
        {
            auto r = host.runChunk(gp, "on_take_damage('my_callback_fn', 'target_npc')", "takedmg1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.getOrCreateCharacter("target_npc").onTakeDamageCallback == "my_callback_fn");
        }

        // 9. set_ignore_ai_flag (Sec3.4): CONFIRMED default TRUE when arg 2
        // omitted/nil (the one function in this batch defaulting a
        // nil-gated bool to true); explicit false; and the HIGH-
        // CONFIDENCE-tagged conditional action-override side effect when
        // newly enabling while stateEnum==3 ("in a vehicle").
        {
            auto r = host.runChunk(gp, "set_ignore_ai_flag('npc_a')", "ignoreai1.lua"); // no 2nd arg -> default true
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI == true);

            auto r2 = host.runChunk(gp, "set_ignore_ai_flag('npc_a', false)", "ignoreai2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI == false);

            auto& npcB = es.getOrCreateCharacter("npc_b");
            npcB.stateEnum = 3; // this project's own test-only setup - "in a vehicle" (Sec3.4/Sec3.10)
            CHECK(npcB.actionOverrideId == -1); // default "no override"
            auto r3 = host.runChunk(gp, "set_ignore_ai_flag('npc_b', true)", "ignoreai3.lua"); // false->true: "newly enabling"
            CHECK(r3.loadOk && r3.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_b").actionOverrideId == 0x19); // HIGH CONFIDENCE meaning, CONFIRMED structure
        }

        // 10./11. get_max_hit_points (Sec7.12) / set_current_hit_points
        // (Sec7.31): the round-trip this task's own brief specifically
        // asked for. Default max=100 (this project's own chosen default);
        // set_current_hit_points clamps into [0, max], leaves max
        // untouched (CONFIRMED cross-check, same field); un-gated numeric
        // arg 2 omission resolves to 0 (CONFIRMED); clamped<=0 sets the
        // HIGH-CONFIDENCE-only isDeadHighConfidence flag.
        {
            auto r = host.runChunk(gp, "assert(get_max_hit_points('hero') == 100.0)", "hp1.lua"); // default
            CHECK(r.loadOk && r.pcallOk);

            auto r2 = host.runChunk(gp, "set_current_hit_points('hero', 50)", "hp2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints == 50);
            auto r3 = host.runChunk(gp, "assert(get_max_hit_points('hero') == 100.0)", "hp3.lua"); // untouched
            CHECK(r3.loadOk && r3.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").isDeadHighConfidence == false);

            auto r4 = host.runChunk(gp, "set_current_hit_points('hero', 999)", "hp4.lua"); // clamp to cap
            CHECK(r4.loadOk && r4.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints == 100);

            auto r5 = host.runChunk(gp, "set_current_hit_points('hero', -50)", "hp5.lua"); // clamp to 0
            CHECK(r5.loadOk && r5.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints == 0);
            CHECK(es.getOrCreateCharacter("hero").isDeadHighConfidence == true);

            auto r6 = host.runChunk(gp, "set_current_hit_points('freshvictim')", "hp6.lua"); // omitted arg2 -> 0 (CONFIRMED nil-handling)
            CHECK(r6.loadOk && r6.pcallOk);
            CHECK(es.getOrCreateCharacter("freshvictim").currentHitPoints == 0);
        }

        // 12. ai_clear_scripted_action (Sec7.24): the confirmed resolve
        // step happens (touches the registry entry into existence) and
        // the call itself never errors, without inventing what internal
        // field 0x004f4c90 actually clears (OPEN, not even HIGH
        // CONFIDENCE, per the spec's own text).
        {
            CHECK(!es.hasCharacter("#PLAYER#"));
            auto r = host.runChunk(gp, "ai_clear_scripted_action('#PLAYER#')", "clearaction1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.hasCharacter("#PLAYER#"));
        }

        // 13. vint_object_find (spec-lua-bindings.md Sec13.7/Sec15) - the
        // 43%-of-all-errors fix. CONFIRMED: always exactly 1 number,
        // 0.0 on every failure path, never nil. This project's registry
        // is never populated by any Lua-visible call in this pass's scope
        // (see engine_state.h's own VdoObject doc comment), so a bare
        // "not found" call is itself the real, honest, CONFIRMED
        // behavior for a script's very first call - exercised first,
        // then the full parent/document-scoped mechanism is exercised
        // directly via EngineState::registerVdoObjectForTesting().
        {
            auto r = host.runChunk(ui,
                "local h = vint_object_find('never_registered_widget')\n"
                "assert(type(h) == 'number' and h == 0.0)",
                "vintfind1.lua");
            CHECK(r.loadOk && r.pcallOk);

            // Document-wide find (no parent/doc args -> current default
            // document, 0 by this project's own default).
            uint32_t rootHandle = es.registerVdoObjectForTesting("root_widget", /*parentHandle=*/0, /*docHandle=*/0);
            CHECK(rootHandle != 0);
            auto r2 = host.runChunk(ui,
                "local h = vint_object_find('root_widget')\n"
                "assert(h == " + std::to_string(rootHandle) + ")",
                "vintfind2.lua");
            CHECK(r2.loadOk && r2.pcallOk);

            // Parent-scoped find: a child registered under rootHandle is
            // found when searching WITH that parent handle, but NOT found
            // when searching a document that doesn't contain it as a root
            // (parent absent, falls back to the default document, which
            // does not have this child as a direct entry keyed to it).
            uint32_t childHandle = es.registerVdoObjectForTesting("child_widget", rootHandle, /*docHandle=*/0);
            CHECK(childHandle != 0);
            auto r3 = host.runChunk(ui,
                "local h = vint_object_find('child_widget', " + std::to_string(rootHandle) + ")\n"
                "assert(h == " + std::to_string(childHandle) + ")",
                "vintfind3.lua");
            CHECK(r3.loadOk && r3.pcallOk);

            // A given-but-unresolvable (bad/stale) parent handle falls
            // back to a document-wide search (CONFIRMED per Sec15's own
            // "or document-wide if no parent was resolved" text) - finds
            // root_widget (a doc-0 root) even though the supplied parent
            // handle (999999) does not exist.
            auto r4 = host.runChunk(ui,
                "local h = vint_object_find('root_widget', 999999)\n"
                "assert(h == " + std::to_string(rootHandle) + ")",
                "vintfind4.lua");
            CHECK(r4.loadOk && r4.pcallOk);

            // A real 3-argument call (name, parent_handle, doc_handle) -
            // doc_handle is only consulted when no parent resolves; here
            // the parent DOES resolve, so doc_handle is irrelevant to the
            // outcome (still finds the child).
            auto r5 = host.runChunk(ui,
                "local h = vint_object_find('child_widget', " + std::to_string(rootHandle) + ", 12345)\n"
                "assert(h == " + std::to_string(childHandle) + ")",
                "vintfind5.lua");
            CHECK(r5.loadOk && r5.pcallOk);
        }

        // 14. store_vehicle_get_state (Sec10.1): default 0.0 ("not in
        // vehicle-store UI mode"); real number (not boolean) return;
        // EngineState::setVehicleStoreActiveForTesting() is a C++-only
        // test entry point (no Lua setter among these 9, same precedent
        // as coop_is_active/Sec3.1 above).
        {
            auto r = host.runChunk(ui,
                "local v = store_vehicle_get_state()\n"
                "assert(type(v) == 'number' and v == 0.0)",
                "vehiclestore1.lua");
            CHECK(r.loadOk && r.pcallOk);
            es.setVehicleStoreActiveForTesting(true);
            auto r2 = host.runChunk(ui, "assert(store_vehicle_get_state() == 1.0)", "vehiclestore2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }

        // 15. Completion_is_client (Sec10.2): CONFIRMED formula
        // isCoopActive() && !isHost(); false with no session at all (NOT
        // the literal complement of the out-of-scope game_get_is_host);
        // isHost() defaults true (this project's own single-instance
        // default).
        {
            es.setCoopActive(false);
            auto r = host.runChunk(ui, "assert(Completion_is_client() == false)", "isclient1.lua"); // no session
            CHECK(r.loadOk && r.pcallOk);
            es.setCoopActive(true);
            auto r2 = host.runChunk(ui, "assert(Completion_is_client() == false)", "isclient2.lua"); // session, but host (default)
            CHECK(r2.loadOk && r2.pcallOk);
            es.setHostForTesting(false);
            auto r3 = host.runChunk(ui, "assert(Completion_is_client() == true)", "isclient3.lua"); // session, not host
            CHECK(r3.loadOk && r3.pcallOk);
            es.setHostForTesting(true); // restore for later fixtures in this same Host instance
            es.setCoopActive(false);
        }

        // 16. game_hud_update_inventory (Sec10.3): default hasLocalPlayer
        // true -> triggers a refresh (counter increments); false ->
        // no-op. No Lua return value.
        {
            CHECK(es.hudInventoryRefreshCount() == 0);
            auto r = host.runChunk(ui, "game_hud_update_inventory()", "hudinv1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.hudInventoryRefreshCount() == 1);
            es.setHasLocalPlayerForTesting(false);
            auto r2 = host.runChunk(ui, "game_hud_update_inventory()", "hudinv2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.hudInventoryRefreshCount() == 1); // unchanged - no local player
            es.setHasLocalPlayerForTesting(true);
        }

        // 17. tutorial_advance (Sec10.4): this project's own stated
        // simplification (no real 210-entry table in this codebase) -
        // any non-empty id resolves (true) and bumps its own counter;
        // empty id does not resolve (false), no counter bump.
        {
            CHECK(es.tutorialAdvanceCount("hint_grab_weapon") == 0);
            auto r = host.runChunk(gp, "assert(tutorial_advance('hint_grab_weapon') == true)", "tutadv1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.tutorialAdvanceCount("hint_grab_weapon") == 1);
            auto r2 = host.runChunk(gp, "assert(tutorial_advance('hint_grab_weapon') == true)", "tutadv2.lua"); // real, cumulative counter
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.tutorialAdvanceCount("hint_grab_weapon") == 2);
            auto r3 = host.runChunk(gp, "assert(tutorial_advance('') == false)", "tutadv3.lua");
            CHECK(r3.loadOk && r3.pcallOk);
        }

        // 18. minimap_icon_add_do (Sec10.5): CONFIRMED nil-gated defaults
        // (arg3 "", arg4 0.0, arg5 3); resolved via getOrCreateCharacter,
        // recorded into that object's own minimapIcons vector - no
        // dispatch-branch modeling, no Lua return value.
        {
            auto r = host.runChunk(gp, "minimap_icon_add_do('waypoint_obj', 'ICON_MISSION')", "minimap1.lua"); // args 3-5 omitted -> defaults
            CHECK(r.loadOk && r.pcallOk);
            auto& obj = es.getOrCreateCharacter("waypoint_obj");
            CHECK(obj.minimapIcons.size() == 1);
            CHECK(obj.minimapIcons[0].iconType == "ICON_MISSION");
            CHECK(obj.minimapIcons[0].group.empty());
            CHECK(obj.minimapIcons[0].param4 == 0.0);
            CHECK(obj.minimapIcons[0].flag5 == 3);

            auto r2 = host.runChunk(gp, "minimap_icon_add_do('waypoint_obj', 'ICON_ALLY', 'group_a', 1.5, 7)", "minimap2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(obj.minimapIcons.size() == 2);
            CHECK(obj.minimapIcons[1].iconType == "ICON_ALLY");
            CHECK(obj.minimapIcons[1].group == "group_a");
            CHECK(obj.minimapIcons[1].param4 == 1.5);
            CHECK(obj.minimapIcons[1].flag5 == 7);
        }

        // 19. object_indicator_add_do (Sec10.6): args 2/3 CONFIRMED
        // mandatory with NO nil gate (real API: omitted -> 0 via
        // lua_tonumber's own nil-handling); args 4/5 CONFIRMED optional,
        // defaults 3/100.0; always returns true through this minimal
        // registry's own resolver (see this function's own doc comment,
        // lua_spec_confirmed_stubs.cpp, for why).
        {
            auto r = host.runChunk(gp,
                "local ok = object_indicator_add_do('indicator_obj', 2, 5)\n"
                "assert(ok == true)",
                "objind1.lua");
            CHECK(r.loadOk && r.pcallOk);
            auto& obj = es.getOrCreateCharacter("indicator_obj");
            CHECK(obj.objectIndicators.size() == 1);
            CHECK(obj.objectIndicators[0].arg2 == 2);
            CHECK(obj.objectIndicators[0].arg3 == 5);
            CHECK(obj.objectIndicators[0].arg4 == 3);   // default
            CHECK(obj.objectIndicators[0].arg5 == 100.0); // default

            auto r2 = host.runChunk(gp,
                "local ok2 = object_indicator_add_do('indicator_obj', 2, 5, 1, 50.0)\n"
                "assert(ok2 == true)",
                "objind2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(obj.objectIndicators.size() == 2);
            CHECK(obj.objectIndicators[1].arg4 == 1);
            CHECK(obj.objectIndicators[1].arg5 == 50.0);
        }

        // 20. on_qte_animation_trigger (Sec10.7): a SINGLE GLOBAL slot
        // (EngineState-level, not per-character) - non-empty sets it,
        // empty clears it. No Lua return value.
        {
            CHECK(es.qteAnimationTriggerCallback().empty());
            auto r = host.runChunk(gp, "on_qte_animation_trigger('my_qte_cb')", "qte1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.qteAnimationTriggerCallback() == "my_qte_cb");
            auto r2 = host.runChunk(gp, "on_qte_animation_trigger('')", "qte2.lua"); // empty -> clears
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.qteAnimationTriggerCallback().empty());
        }

        // 21. on_revived (Sec10.8): the target character's own dedicated
        // onRevivedCallback field (slot "2" in the real engine's own
        // generic hook-slot mechanism) - non-empty sets it, empty clears
        // it. No Lua return value.
        {
            auto r = host.runChunk(gp, "on_revived('my_revive_cb', 'hero_npc')", "revived1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.getOrCreateCharacter("hero_npc").onRevivedCallback == "my_revive_cb");
            auto r2 = host.runChunk(gp, "on_revived('', 'hero_npc')", "revived2.lua"); // empty -> clears
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("hero_npc").onRevivedCallback.empty());
        }

        // 22. game_get_coop_join_type (Sec10.9): default 0; real number
        // return; EngineState::setCoopJoinTypeForTesting() is a C++-only
        // test entry point (no real setter in this task's 9-function
        // scope - the real writer is a different, out-of-scope function).
        {
            auto r = host.runChunk(ui, "assert(game_get_coop_join_type() == 0)", "jointype1.lua");
            CHECK(r.loadOk && r.pcallOk);
            es.setCoopJoinTypeForTesting(2);
            auto r2 = host.runChunk(ui, "assert(game_get_coop_join_type() == 2)", "jointype2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }

        // Every one of the 22 calls above must fold into the SAME HitLog
        // ranking every other stub uses (stub_registry.h/thread_scheduler.h).
        for (const char* name : {"coop_is_active", "game_get_key_name", "game_UI_audio_play",
                                  "game_audio_get_audio_id", "game_get_key_name_for_action",
                                  "game_peg_load_with_cb", "ai_add_enemy_target", "on_take_damage",
                                  "set_ignore_ai_flag", "get_max_hit_points", "set_current_hit_points",
                                  "ai_clear_scripted_action", "vint_object_find",
                                  "store_vehicle_get_state", "Completion_is_client",
                                  "game_hud_update_inventory", "tutorial_advance",
                                  "minimap_icon_add_do", "object_indicator_add_do",
                                  "on_qte_animation_trigger", "on_revived", "game_get_coop_join_type"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

    if (g_failures == 0) {
        std::cout << "ALL sr3luahost SYNTHETIC TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
