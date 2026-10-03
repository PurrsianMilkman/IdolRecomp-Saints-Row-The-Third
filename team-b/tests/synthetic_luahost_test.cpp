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
using sr3luahost::VintTaggedValue;

// A string global's value ("" when absent or not a string) - used where a test
// would otherwise need the `string` library, which host states do not have.
std::string globalString(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    const char* v = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
    std::string out = v ? v : "";
    lua_pop(L, 1);
    return out;
}

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
        // Cloud phase (2026-09-30): Sec14.23, `gameplay` (tagged list line 1014).
        {"zscene_is_loaded", "gameplay"},
        // Cloud phase batch 1 (tagged list lines 776/295/505).
        {"set_mission_author", "gameplay"},
        {"fade_out", "gameplay"},
        {"mission_end_silently", "gameplay"},
        {"sfx_faded_out", "ui"},
        // Batch 2026-10-01 (tagged list: fade_in 294, fade_is_fully_faded_out
        // 293, fade_is_fully_faded_in 292, zscene_prep 1013 `gameplay`;
        // sfx_faded_in 1252, Screen_fade_transition_complete 1250,
        // game_get_is_host 1378, vint_get_safe_frame 1456, vint_is_std_res
        // 1488 `ui`).
        {"fade_in", "gameplay"},
        {"fade_is_fully_faded_out", "gameplay"},
        {"fade_is_fully_faded_in", "gameplay"},
        {"zscene_prep", "gameplay"},
        {"sfx_faded_in", "ui"},
        {"Screen_fade_transition_complete", "ui"},
        {"game_get_is_host", "ui"},
        {"vint_get_safe_frame", "ui"},
        {"vint_is_std_res", "ui"},
        // Batch 2026-10-02 (spec-lua-bindings.md Sec18-Sec21, "Vint UI
        // API"), all `ui` in tools/lua_all_registered_1490_tagged.txt
        // (grepped directly, lines 1443/1446/1455/1458/1462/1464/1469/1475).
        {"vint_object_first_child", "ui"},
        {"vint_object_clone", "ui"},
        {"vint_get_time_index", "ui"},
        {"vint_dataitem_get", "ui"},
        {"vint_set_property", "ui"},
        {"vint_get_property", "ui"},
        {"vint_dataresponder_finished", "ui"},
        {"vint_internal_dataresponder_request", "ui"},
        // Batch 2026-10-02 (spec-lua-api-behaviour.md Sec33, "ranking
        // tranche 05"), all 25 `gameplay` in tools/lua_all_registered_
        // 1490_tagged.txt (grepped directly, lines 935/1000/1001/937/945/
        // 984/947/965/972/946/994/25/35/34/31/7/5/17/74/44/67/43/92/83/
        // 1003).
        {"vehicle_is_helicopter", "gameplay"},
        {"vehicle_is_vtol_hover", "gameplay"},
        {"vehicle_is_vtol_jet", "gameplay"},
        {"vehicle_is_ready", "gameplay"},
        {"vehicle_never_flatten_tires", "gameplay"},
        {"vehicle_set_weapons_disarmed", "gameplay"},
        {"vehicle_set_no_chase", "gameplay"},
        {"vehicle_set_kneecappers", "gameplay"},
        {"vehicle_set_sirenlights", "gameplay"},
        {"vehicle_set_ambient", "gameplay"},
        {"vehicle_spotlight_is_target_spotted", "gameplay"},
        {"ai_clear_priority_target", "gameplay"},
        {"ai_set_in_scripted_cover", "gameplay"},
        {"ai_pay_attention_to_position", "gameplay"},
        {"ai_do_scripted_rush", "gameplay"},
        {"action_play_synced_do", "gameplay"},
        {"action_play_directional_stumble_do", "gameplay"},
        {"action_sequence_end", "gameplay"},
        {"audio_set_listener_override", "gameplay"},
        {"audio_clear_listener_override", "gameplay"},
        {"audio_play_for_navpoint", "gameplay"},
        {"audio_any_conversation_playing", "gameplay"},
        {"boss_battle_matt_begin", "gameplay"},
        {"auto_pickup_disable", "gameplay"},
        {"waiting_for_player_dialog", "gameplay"},
        // Batch 2026-10-02 (spec-lua-api-behaviour.md Sec35, the
        // `teleport_coop` investigation), all `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly,
        // lines 523/843/858/948).
        {"teleport_check_done", "gameplay"},
        {"turn_to_check_done", "gameplay"},
        {"move_to_check_done", "gameplay"},
        {"vehicle_pathfind_check_done", "gameplay"},
    };
}

// Completion callbacks for the C++-level fade tests (EngineState::FadeCallback).
std::vector<std::pair<char, uint32_t>> g_fadeCalls;
void fadeCbA(EngineState&, uint32_t t) { g_fadeCalls.push_back({'A', t}); }
void fadeCbB(EngineState&, uint32_t t) { g_fadeCalls.push_back({'B', t}); }
void fadeCbC(EngineState&, uint32_t t) { g_fadeCalls.push_back({'C', t}); }

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

    // --- thread_* (2026-10-01): thread_new/thread_yield/thread_kill/
    // thread_check_done are now bare globals of 0x00e0f900 with the
    // CONFIRMED Sec26.27 behaviour (bare_globals.h; full tests in
    // tests/synthetic_luahost_bare_globals_test.cpp). thread_close stays the
    // ThreadScheduler scaffold's (not in the engine roster; OPEN). ---------
    {
        std::vector<RegisteredName> names;
        Host host(names);
        lua_State* L = host.gameplayState();
        auto r1 = host.runChunk(L,
            "thread_close(5)\n"                         // scaffold no-op
            "assert(select('#', thread_kill(65535)) == 0)\n" // no thread: nothing happens, no result
            "assert(thread_check_done(1) == true)",      // no such record -> done
            "thread1.lua");
        CHECK(r1.loadOk && r1.pcallOk);
        // thread_yield outside any coroutine: stock Lua's yield error (Sec26.27,
        // HIGH CONFIDENCE), no longer the scaffold's silent no-op.
        auto r4 = host.runChunk(L, "thread_yield()", "thread4.lua");
        CHECK(r4.loadOk && !r4.pcallOk);
        CHECK(r4.pcallError.find("attempt to yield across metamethod/C-call boundary") != std::string::npos);
        // All of them fold into the same HitLog ranking.
        for (const char* n : {"thread_close", "thread_kill", "thread_check_done", "thread_yield"})
            CHECK(host.hitLog().hits().count(n) == 1);
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
        // Sec8.2 (2026-09-30): modulo and char signedness are OPEN, so a
        // bucket is only unambiguous for ASCII names, and for a
        // non-power-of-two count only when the hash is below 2^31.
        CHECK(EngineState::multiply33XorHashBucketUnambiguous("tex_a.cvbm_pc", 1024));  // power of two
        CHECK(!EngineState::multiply33XorHashBucketUnambiguous("tex_a.cvbm_pc", 9000)); // 0xd80fcd8f
        CHECK(EngineState::multiply33XorHashBucketUnambiguous("tex_b.cvbm_pc", 9000));  // 0x4010c64c
        CHECK(EngineState::multiply33XorHashBucketUnambiguous("", 9000));
        CHECK(!EngineState::multiply33XorHashBucketUnambiguous("a\xe9", 0x20));        // byte >= 0x80
        CHECK(!EngineState::multiply33XorHashBucketUnambiguous("a\x80", 9000));
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
        // A call reading OPEN engine state (open_state.h) must fail with a
        // Lua error naming that state, until a test sets it.
        auto refusesOpen = [&](lua_State* st, const char* chunk, const char* needle) {
            auto r = host.runChunk(st, chunk, "open_refusal.lua");
            CHECK(r.loadOk && !r.pcallOk);
            CHECK(r.pcallError.find("is OPEN") != std::string::npos);
            CHECK(r.pcallError.find(needle) != std::string::npos);
        };
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

        // 1. coop_is_active (Sec3.1, Sec26.28): no session at start-up
        // (CONFIRMED) -> false. With a session: host test first, else the
        // client gate; >= 2 members (unsigned); every other member passes
        // the slot check. Per-session fields are OPEN until set.
        {
            auto ok = [&](const char* chunk) {
                auto r = host.runChunk(gp, chunk, "coop.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            ok("assert(coop_is_active() == false)");
            auto& cs = es.coopSession();
            cs.present.set(true);
            refusesOpen(gp, "coop_is_active()", "+0x5c == +0x58");
            cs.localIsHost.set(true);
            refusesOpen(gp, "coop_is_active()", "+0x60"); // host: client gate not read
            cs.memberCount.set(1);
            ok("assert(coop_is_active() == false)");    // one member: never active
            cs.memberCount.set(0);
            ok("assert(coop_is_active() == false)");
            cs.memberCount.set(2);
            refusesOpen(gp, "coop_is_active()", "0x00877a90");
            cs.otherMembersPassSlotCheck.set(true);
            ok("assert(coop_is_active() == true)");
            cs.otherMembersPassSlotCheck.set(false);
            ok("assert(coop_is_active() == false)");
            cs.localIsHost.set(false);                  // client
            refusesOpen(gp, "coop_is_active()", "client gate");
            cs.clientGate.set(false);
            ok("assert(coop_is_active() == false)");
            cs.clientGate.set(true);
            cs.otherMembersPassSlotCheck.set(true);
            cs.memberCount.set(3);
            ok("assert(coop_is_active() == true)");
            auto r2 = host.runChunk(ui, "assert(coop_is_active() == true)", "coop_ui.lua"); // dual-registered
            CHECK(r2.loadOk && r2.pcallOk);
            // Back to the start-up state; the session fields return to OPEN.
            cs.present.set(false);
            cs.localIsHost.forget();
            cs.clientGate.forget();
            cs.memberCount.forget();
            cs.otherMembersPassSlotCheck.forget();
            ok("assert(coop_is_active() == false)");
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
            // Non-empty only where the real OS lookup exists (Windows); the
            // portable build has no key-name source and returns "" - the
            // CONFIRMED "1 Lua string" return contract holds on both.
#ifdef _WIN32
            auto r2 = host.runChunk(ui,
                "local n = game_get_key_name(0x1E)\n"
                "assert(type(n) == 'string' and #n > 0)",
                "keyname2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
#else
            // No OS key-name source off Windows: OPEN, not an invented "".
            refusesOpen(ui, "game_get_key_name(0x1E)", "GetKeyNameTextW");
#endif
            // Out-of-int32-range / NaN key codes: truncation unspecified -> OPEN (was UB).
            refusesOpen(ui, "game_get_key_name(1e300)", "key code truncation");
            refusesOpen(ui, "game_get_key_name(0/0)", "key code truncation");
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
                "assert(game_audio_get_audio_id(70000.4) == (70000 % 65536))\n"
                // 0x00ea2596 truncates toward zero (Sec4.1, settled 2026-10-01):
                // 70000.9 -> 70000 (not 70001), -1.5 -> -1 -> 0xFFFF (not -2 -> 0xFFFE),
                // 2.5 -> 2 and 3.5 -> 3 (not round-half-even's 2 and 4).
                "assert(game_audio_get_audio_id(70000.9) == (70000 % 65536))\n"
                "assert(game_audio_get_audio_id(-1.5) == 65535)\n"
                "assert(game_audio_get_audio_id(-0.9) == 0)\n"
                "assert(game_audio_get_audio_id(2.5) == 2 and game_audio_get_audio_id(3.5) == 3)\n"
                "assert(game_audio_get_audio_id() == 0)\n"
                "assert(game_audio_get_audio_id('none') == 0)\n"
                "assert(game_audio_get_audio_id('') == 0)\n"
                "assert(game_audio_get_audio_id('NoNe') == 0)", // case-insensitive
                "audioid1.lua");
            CHECK(r.loadOk && r.pcallOk);
            // Any other string needs the Wwise resolver: OPEN, not an invented 0.
            refusesOpen(ui, "game_audio_get_audio_id('some_event')", "Wwise id");
        }

        // 5. game_get_key_name_for_action (Sec8.23): CONFIRMED literal
        // CAA_CAMERA_ROTATE -> STR_THE_MOUSE (case-insensitive match, real
        // __stricmp-equivalent contract); CONFIRMED "no match" -> "".
        // Re-checked 2026-10-02 against spec-tables-ui-controls.md Sec4.1/
        // Sec4.2 (CONFIRMED, not held): the CBA sentinel BUTTON_UNBOUND
        // (value -1, so it never satisfies the CBA "bound" branch) and 6
        // CAA representative entries at axis indices other than 0/2/3 are
        // now individually named in spec text and resolve to "" too.
        {
            auto r = host.runChunk(ui,
                "assert(game_get_key_name_for_action('CAA_CAMERA_ROTATE') == 'STR_THE_MOUSE')\n"
                "assert(game_get_key_name_for_action('caa_camera_rotate') == 'STR_THE_MOUSE')\n"
                "assert(game_get_key_name_for_action('BUTTON_UNBOUND') == '')\n"
                "assert(game_get_key_name_for_action('button_unbound') == '')\n"
                "assert(game_get_key_name_for_action('CAA_CAMERA_ELEVATE') == '')\n"
                "assert(game_get_key_name_for_action('CAA_DRIVE_STEER') == '')\n"
                "assert(game_get_key_name_for_action('CAA_TANK_DRIVE_FORWARD_BACKWARD') == '')\n"
                "assert(game_get_key_name_for_action('CAA_VPC_TURRET_RIGHT') == '')\n"
                "assert(game_get_key_name_for_action('CAA_TURRET_CAMERA_ELEVATE') == '')\n"
                "assert(game_get_key_name_for_action('CAA_PLANE_ROLL_LEFT_RIGHT') == '')",
                "actionname1.lua");
            CHECK(r.loadOk && r.pcallOk);
            // Everything else goes through the CBA/CAA tables + live bindings: OPEN.
            refusesOpen(ui, "game_get_key_name_for_action('CBA_SOME_UNKNOWN_ACTION')", "key binding");
            // The two-key movement axes (CAA axis 2/3) stay OPEN too - their
            // own sub-action resolvers are still unread (Sec8.23's own
            // closing OPEN item), unaffected by the ui-controls table sync.
            refusesOpen(ui, "game_get_key_name_for_action('CAA_WALK_FORWARD_BACKWARD')", "key binding");
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
            // tex_a hashes to 0xd80fcd8f (>= 2^31): with 9000 buckets signed and
            // unsigned modulo disagree, both OPEN in Sec8.2 -> index stays OPEN.
            CHECK(!req.filenames[0].bucketIndex.known());
            CHECK(req.filenames[1].name == "tex_b.cvbm_pc");
            // tex_b hashes to 0x4010c64c (< 2^31): every reading agrees.
            CHECK(req.filenames[1].bucketIndex.known());
            CHECK(req.filenames[1].bucketIndex.get() == 0x4010c64cu % 9000u);

            // Count out of the confirmed 1..6 range -> no filenames read,
            // even though more string args were supplied.
            auto r2 = host.runChunk(ui, "game_peg_load_with_cb('req2', 0, 'should_not_be_read.cvbm_pc')", "peg2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.pegLoadRequests().size() == 2);
            CHECK(es.pegLoadRequests().back().filenames.empty());
        }

        // 7. ai_add_enemy_target (Sec3.9): the #CLOSEST_PLAYER# sentinel
        // bit, arg4's bit, the priority (truncated toward zero by 0x00ea2596,
        // Sec4.1 settled 2026-10-01: 7.9 keys as "7"), and the always-true
        // resolve-success return (this project's own minimal registry -
        // see EnemyTargetRecord's own doc comment).
        {
            // Name resolution is OPEN until set; unresolved -> false, no record.
            refusesOpen(gp, "ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 5, true)", "named-object resolution");
            es.objectResolves().set("villain01", true);
            es.objectResolves().set("#CLOSEST_PLAYER#", false);
            auto r0 = host.runChunk(gp, "assert(ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 5, true) == false)", "enemy0.lua");
            CHECK(r0.loadOk && r0.pcallOk);
            CHECK(es.getOrCreateCharacter("villain01").enemyTargets.empty());
            es.objectResolves().set("#CLOSEST_PLAYER#", true);
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
            auto rt = host.runChunk(gp, "assert(ai_add_enemy_target('villain01', '#CLOSEST_PLAYER#', 7.9) == true)", "enemy_trunc.lua");
            CHECK(rt.loadOk && rt.pcallOk);
            CHECK(actor.enemyTargets.count("7") == 1 && actor.enemyTargets.count("8") == 0);
            if (actor.enemyTargets.count("7")) CHECK(actor.enemyTargets.at("7").priorityOrId == 7);
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
        // newly enabling while stateEnum==3 ("in a vehicle"). Character
        // spawn state (Sec34.1/Sec34.3, 2026-10-02): a freshly touched
        // name's ignore-AI is now a CONFIRMED default (off), applied by
        // EngineState::getOrCreateCharacter() itself - not OPEN - except
        // for the one path (a bound script NPC, Sec34.4) that correctly
        // stays OPEN.
        {
            // CONFIRMED default (Sec34.1/Sec34.3): known immediately, off,
            // no .set() needed first.
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.known());
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == false);
            // Default arg2 (true) against the off default is "newly
            // enabling", which reads stateEnum next - still OPEN until test
            // setup, so THAT's what refuses now: the ignore-AI default-path
            // fix moved this call's blocker forward by one field, it didn't
            // remove the need for state-enum test setup on the enabling path.
            refusesOpen(gp, "set_ignore_ai_flag('npc_a')", "state enum");
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == false); // unchanged: refused before any write

            es.getOrCreateCharacter("npc_a").ignoreAI.set(true); // test setup: simulate already-on
            auto r = host.runChunk(gp, "set_ignore_ai_flag('npc_a')", "ignoreai1.lua"); // no 2nd arg -> default true; already true -> no enable branch, stateEnum never read
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == true);

            auto r2 = host.runChunk(gp, "set_ignore_ai_flag('npc_a', false)", "ignoreai2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == false);
            // Newly enabling with the state enum OPEN: refused, flag unchanged.
            refusesOpen(gp, "set_ignore_ai_flag('npc_a', true)", "state enum");
            CHECK(es.getOrCreateCharacter("npc_a").ignoreAI.get() == false);

            auto& npcB = es.getOrCreateCharacter("npc_b");
            npcB.stateEnum.set(3); // test setup - "in a vehicle" (Sec3.4/Sec3.10); ignoreAI already off via the CONFIRMED default
            CHECK(!npcB.actionOverrideId.known());
            auto r3 = host.runChunk(gp, "set_ignore_ai_flag('npc_b', true)", "ignoreai3.lua"); // false->true: "newly enabling"
            CHECK(r3.loadOk && r3.pcallOk);
            CHECK(es.getOrCreateCharacter("npc_b").actionOverrideId.get() == 0x19); // HIGH CONFIDENCE meaning, CONFIRMED structure
            // Not in a vehicle, nonzero threat -> override cleared to 0.
            auto& npcC = es.getOrCreateCharacter("npc_c");
            npcC.stateEnum.set(0);
            npcC.attackerThreatRef.set(7);
            auto r4 = host.runChunk(gp, "set_ignore_ai_flag('npc_c', true)", "ignoreai4.lua");
            CHECK(r4.loadOk && r4.pcallOk);
            CHECK(npcC.actionOverrideId.get() == 0);

            // Sec34.4: a character explicitly bound to a script NPC - the
            // override question (script_npc_flags containing "ignore_ai")
            // is zone data this project does not have (the .czn_pc hold),
            // so it correctly stays OPEN even though an ordinary fresh name
            // would not be.
            es.markScriptNpcBoundForTesting("npc_scriptnpc");
            CHECK(!es.getOrCreateCharacter("npc_scriptnpc").ignoreAI.known());
            refusesOpen(gp, "set_ignore_ai_flag('npc_scriptnpc', false)", "ignore-AI flag");
        }

        // 10./11. get_max_hit_points (Sec7.12) / set_current_hit_points
        // (Sec7.31): the round-trip this task's own brief specifically
        // asked for. Default max=100 (this project's own chosen default);
        // set_current_hit_points clamps into [0, max], leaves max
        // untouched (CONFIRMED cross-check, same field); un-gated numeric
        // arg 2 omission resolves to 0 (CONFIRMED); clamped<=0 sets the
        // HIGH-CONFIDENCE-only isDeadHighConfidence flag.
        {
            // Max hit points are OPEN until set (no invented 100).
            refusesOpen(gp, "get_max_hit_points('hero')", "max hit points");
            refusesOpen(gp, "set_current_hit_points('hero', 50)", "max hit points");
            es.getOrCreateCharacter("hero").maxHitPoints.set(100);
            auto r = host.runChunk(gp, "assert(get_max_hit_points('hero') == 100.0)", "hp1.lua");
            CHECK(r.loadOk && r.pcallOk);

            auto r2 = host.runChunk(gp, "set_current_hit_points('hero', 50)", "hp2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 50);
            auto r3 = host.runChunk(gp, "assert(get_max_hit_points('hero') == 100.0)", "hp3.lua"); // untouched
            CHECK(r3.loadOk && r3.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").isDeadHighConfidence == false);

            auto rt = host.runChunk(gp, "set_current_hit_points('hero', 50.9)", "hp_trunc.lua"); // truncation (Sec4.1)
            CHECK(rt.loadOk && rt.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 50);

            auto r4 = host.runChunk(gp, "set_current_hit_points('hero', 999)", "hp4.lua"); // clamp to cap
            CHECK(r4.loadOk && r4.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 100);

            auto r5 = host.runChunk(gp, "set_current_hit_points('hero', -50)", "hp5.lua"); // clamp to 0
            CHECK(r5.loadOk && r5.pcallOk);
            CHECK(es.getOrCreateCharacter("hero").currentHitPoints.get() == 0);
            CHECK(es.getOrCreateCharacter("hero").isDeadHighConfidence == true);

            es.getOrCreateCharacter("freshvictim").maxHitPoints.set(80);
            auto r6 = host.runChunk(gp, "set_current_hit_points('freshvictim')", "hp6.lua"); // omitted arg2 -> 0 (CONFIRMED nil-handling)
            CHECK(r6.loadOk && r6.pcallOk);
            CHECK(es.getOrCreateCharacter("freshvictim").currentHitPoints.get() == 0);

            // Character spawn state (Sec34.1/Sec34.2, 2026-10-02): the
            // CONFIRMED default-path formula, round(M * Hit_Points) with M
            // defaulting to 1.0 for ordinary classes - applied via
            // EngineState::applyCharacterSpawnDefaults() once a caller
            // supplies a resolved Hit_Points value (this project has no way
            // to attach one to a bare name on its own - see
            // CharacterState::maxHitPoints's own doc comment - so a fresh
            // name with no such call still stays OPEN, same as 'hero' above;
            // not a regression).
            refusesOpen(gp, "get_max_hit_points('grunt')", "max hit points");
            es.applyCharacterSpawnDefaults("grunt", 150); // M=1.0: round(1.0*150) = 150
            CHECK(es.getOrCreateCharacter("grunt").maxHitPoints.get() == 150);
            CHECK(es.getOrCreateCharacter("grunt").currentHitPoints.get() == 150); // Sec34.1 step 2: spawns at full health
            CHECK(es.getOrCreateCharacter("grunt").ignoreAI.get() == false);
            auto rg = host.runChunk(gp, "assert(get_max_hit_points('grunt') == 150.0)", "hp_spawn1.lua");
            CHECK(rg.loadOk && rg.pcallOk);
            // Explicit multiplier, half-away-from-zero rounding (this
            // project's one CONFIRMED round() convention, Sec26.27, reused
            // here per Sec34.2's own "round(...)" text - see
            // RoundHalfAwayFromZero's own doc comment, lua_engine_state.cpp).
            es.applyCharacterSpawnDefaults("elite", 75, 1.5); // round(1.5*75) = round(112.5) = 113
            CHECK(es.getOrCreateCharacter("elite").maxHitPoints.get() == 113);

            // Sec34.4: a character explicitly bound to a script NPC - the
            // override question (script_npc_hp) is zone data this project
            // does not have, so even after the spawn-default formula ran,
            // marking it bound forgets the result back to OPEN rather than
            // guess whether an override would have applied instead.
            es.applyCharacterSpawnDefaults("maybe_scripted", 200);
            CHECK(es.getOrCreateCharacter("maybe_scripted").maxHitPoints.known());
            es.markScriptNpcBoundForTesting("maybe_scripted");
            refusesOpen(gp, "get_max_hit_points('maybe_scripted')", "max hit points");
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

            // Document-wide find (no parent/doc args -> the current default
            // document). With objects registered, the answer depends on that
            // document, which is OPEN state until set.
            uint32_t rootHandle = es.registerVdoObjectForTesting("root_widget", /*parentHandle=*/0, /*docHandle=*/0);
            CHECK(rootHandle != 0);
            refusesOpen(ui, "vint_object_find('root_widget')", "current default vint document");
            es.currentDefaultDocHandle().set(0);
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

        // 14. store_vehicle_get_state (Sec10.1, Sec26.28): the flag is 0 at
        // load (CONFIRMED) -> 0.0; a real number, not a boolean.
        {
            auto r = host.runChunk(ui,
                "local v = store_vehicle_get_state()\n"
                "assert(type(v) == 'number' and v == 0.0)",
                "vehiclestore1.lua");
            CHECK(r.loadOk && r.pcallOk);
            es.vehicleStoreActive().set(true);
            auto r2 = host.runChunk(ui, "assert(store_vehicle_get_state() == 1.0)", "vehiclestore2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            es.vehicleStoreActive().set(false);
        }

        // 15. Completion_is_client (Sec10.2) and game_get_is_host (Sec8.27):
        // no session (CONFIRMED start-up, and CONFIRMED - not merely
        // default - for single player as of job "fvzk" 2026-10-01: the
        // session installer is proven statically unreachable there) -> both
        // false; the "with a session" branch below stays a real test of the
        // function's own logic (host pair +0x5c == +0x58, OPEN until set)
        // even though it no longer models anything single player can reach.
        {
            auto ok = [&](const char* chunk) {
                auto r = host.runChunk(ui, chunk, "host.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            ok("assert(Completion_is_client() == false)");
            ok("assert(game_get_is_host() == false)");
            ok("assert(select('#', game_get_is_host()) == 1)");
            auto r0 = host.runChunk(gp, "assert(game_get_is_host == nil)", "host_gp.lua"); // `ui` only
            CHECK(r0.loadOk && r0.pcallOk);
            es.coopSession().present.set(true);
            refusesOpen(ui, "Completion_is_client()", "host check");
            refusesOpen(ui, "game_get_is_host()", "host check");
            es.coopSession().localIsHost.set(true);
            ok("assert(Completion_is_client() == false and game_get_is_host() == true)");
            es.coopSession().localIsHost.set(false);
            ok("assert(Completion_is_client() == true and game_get_is_host() == false)");
            es.coopSession().present.set(false);
            es.coopSession().localIsHost.forget();
            ok("assert(Completion_is_client() == false and game_get_is_host() == false)");
        }

        // 16. game_hud_update_inventory (Sec10.3): default hasLocalPlayer
        // true -> triggers a refresh (counter increments); false ->
        // no-op. No Lua return value.
        {
            CHECK(es.hudInventoryRefreshCount() == 0);
            refusesOpen(ui, "game_hud_update_inventory()", "local player");
            CHECK(es.hudInventoryRefreshCount() == 0);
            es.hasLocalPlayer().set(true);
            auto r = host.runChunk(ui, "game_hud_update_inventory()", "hudinv1.lua");
            CHECK(r.loadOk && r.pcallOk);
            CHECK(es.hudInventoryRefreshCount() == 1);
            es.hasLocalPlayer().set(false);
            auto r2 = host.runChunk(ui, "game_hud_update_inventory()", "hudinv2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
            CHECK(es.hudInventoryRefreshCount() == 1); // unchanged - no local player
            es.hasLocalPlayer().set(true);
        }

        // 17. tutorial_advance (Sec10.4, Sec6.19, Sec26.28): the 210-name
        // resolver (case-insensitive, first match), then entry state == 3.
        // At start entries 0-188 are in state 0 and 189-209 in state 1, so
        // every name is false. The state is not changed by the call.
        {
            auto ok = [&](const char* chunk) {
                auto r = host.runChunk(gp, chunk, "tut.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            CHECK(std::string(EngineState::tutorialName(0)) == "save");
            CHECK(std::string(EngineState::tutorialName(1)) == "autosave");
            CHECK(std::string(EngineState::tutorialName(122)) == "vehicle entry");
            CHECK(std::string(EngineState::tutorialName(189)) == "dlc1_act_genki_escort_into");
            CHECK(std::string(EngineState::tutorialName(209)) == "dlc3_win");
            CHECK(EngineState::tutorialName(176) == nullptr && EngineState::tutorialName(210) == nullptr);
            int named = 0;
            for (int i = 0; i < EngineState::kTutorialEntryCount; ++i) named += EngineState::tutorialName(i) ? 1 : 0;
            CHECK(named == 209);
            CHECK(EngineState::tutorialLookup("AutoSave").index == 1);
            CHECK(EngineState::tutorialLookup("escort_minigame").index == 36); // also at 129: first wins
            CHECK(EngineState::tutorialLookup("TUT_SIXAXIS_BOAT").index == 185);
            CHECK(EngineState::tutorialLookup("hint_grab_weapon").index == -1);
            CHECK(!EngineState::tutorialLookup("hint_grab_weapon").couldBeIndex176);
            CHECK(EngineState::tutorialLookup("ab").couldBeIndex176);                // < 4 characters
            CHECK(EngineState::tutorialLookup("s\xe9ve_x").couldBeIndex176);         // non-ASCII
            CHECK(!EngineState::tutorialLookup("save").couldBeIndex176);              // matched before 176
            CHECK(es.tutorialState().get("0") == 0 && es.tutorialState().get("188") == 0);
            CHECK(es.tutorialState().get("189") == 1 && es.tutorialState().get("209") == 1);

            ok("assert(tutorial_advance('save') == false)");
            ok("assert(tutorial_advance('dlc3_win') == false)");               // state 1
            ok("assert(tutorial_advance('hint_grab_weapon') == false)");       // not in the table
            ok("assert(tutorial_advance('') == false)");                       // could be 176, state 1 != 3
            ok("assert(tutorial_advance() == false)");
            CHECK(es.tutorialAdvanceCount(0) == 0);
            es.tutorialState().set("0", 3);
            ok("assert(tutorial_advance('SAVE') == true)");
            ok("assert(select('#', tutorial_advance('save')) == 1)");
            CHECK(es.tutorialAdvanceCount(0) == 2);
            CHECK(es.tutorialState().get("0") == 3); // unchanged by tutorial_advance
            for (int st : {0, 1, 2, 4}) {
                es.tutorialState().set("0", st);
                ok("assert(tutorial_advance('save') == false)");
            }
            es.tutorialState().set("0", 0);
            es.tutorialState().set("129", 3);
            ok("assert(tutorial_advance('escort_minigame') == false)"); // resolves to 36
            es.tutorialState().set("36", 3);
            ok("assert(tutorial_advance('escort_minigame') == true)");
            es.tutorialState().set("36", 0);
            es.tutorialState().set("129", 0);
            // Entry 176's name is OPEN: refused only when the answer depends on it.
            es.tutorialState().set("176", 3);
            refusesOpen(gp, "tutorial_advance('ab')", "entry 176");
            refusesOpen(gp, "tutorial_advance('s\xe9ve_x')", "entry 176");
            ok("assert(tutorial_advance('hint_grab_weapon') == false)");     // 4+ ASCII: cannot be 176
            es.tutorialState().set("176", 1);
            ok("assert(tutorial_advance('ab') == false)");
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
            // Resolution of arg 1 is OPEN until set (the old always-true is gone).
            refusesOpen(gp, "object_indicator_add_do('indicator_obj', 2, 5)", "named-object resolution");
            es.objectResolves().set("unresolved_obj", false);
            auto r0 = host.runChunk(gp, "assert(object_indicator_add_do('unresolved_obj', 2, 5) == false)", "objind0.lua");
            CHECK(r0.loadOk && r0.pcallOk);
            CHECK(es.getOrCreateCharacter("unresolved_obj").objectIndicators.empty());
            es.objectResolves().set("indicator_obj", true);
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
            refusesOpen(ui, "game_get_coop_join_type()", "0x012f44fc");
            es.coopJoinType().set(0);
            auto r = host.runChunk(ui, "assert(game_get_coop_join_type() == 0)", "jointype1.lua");
            CHECK(r.loadOk && r.pcallOk);
            es.coopJoinType().set(2);
            auto r2 = host.runChunk(ui, "assert(game_get_coop_join_type() == 2)", "jointype2.lua");
            CHECK(r2.loadOk && r2.pcallOk);
        }

        // 23. zscene_is_loaded (Sec14.23 corrected, Sec26.25) and zscene_prep
        // (Sec8.21, Sec26.25). Only the skip byte 0x0153b556 has a specced
        // start-up value (false, Sec26.25 Globals, 2026-10-01); every other
        // zscene first read is OPEN.
        {
            auto check = [&](const char* chunk, const char* tag) {
                auto r = host.runChunk(gp, chunk, tag);
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            auto refuses = [&](const char* chunk, const char* needle) {
                auto r = host.runChunk(gp, chunk, "zsref.lua");
                CHECK(r.loadOk && !r.pcallOk);
                CHECK(r.pcallError.find(needle) != std::string::npos);
                if (r.pcallError.find(needle) == std::string::npos) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            check("assert(type(zscene_is_loaded) == 'function' and type(zscene_prep) == 'function')", "zs0a.lua");
            {
                auto r = host.runChunk(ui, "assert(zscene_is_loaded == nil and zscene_prep == nil)", "zs0b.lua");
                CHECK(r.loadOk && r.pcallOk);
            }
            CHECK(es.zsceneSkipAllCutscenes().known() && es.zsceneSkipAllCutscenes().get() == false);
            refuses("zscene_is_loaded()", "0x0153b51c");          // no name: skip byte (false), then the load state
            refuses("zscene_is_loaded('Scene_A')", "0x00723d20"); // name: table / kind first
            refuses("zscene_is_loaded('Scene_A')", "scene_a");    // keyed lower-case
            CHECK(host.hitLog().hits().count("zscene_is_loaded:OPEN_STATE") == 1);
            // Not a kind-1 entry (or missing): true at once - no inversion.
            es.zsceneLoadable().set("scene_a", false);
            check("assert(zscene_is_loaded('SCENE_A') == true)", "zs1.lua");
            check("assert(select('#', zscene_is_loaded('scene_a')) == 1)", "zs1b.lua");
            // A kind-1 entry: skip byte next (false at start), then the current
            // entry - null at start (Sec26.25 Globals, RESOLVED 2026-10-02,
            // CONFIRMED), so not current -> false.
            es.zsceneLoadable().set("scene_b", true);
            CHECK(es.zsceneCurrent().known() && es.zsceneCurrent().get().empty());
            check("assert(zscene_is_loaded('scene_b') == false)", "zs1c.lua");
            es.zsceneSkipAllCutscenes().set(true);
            check("assert(zscene_is_loaded('scene_b') == true)", "zs2.lua");
            check("assert(zscene_is_loaded() == true)", "zs3.lua");
            es.zsceneSkipAllCutscenes().set(false);
            // An unknown current entry (as after 0x00722f10, whose value is
            // not specced) is refused, naming it.
            es.zsceneCurrent().forget();
            refuses("zscene_is_loaded('scene_b')", "0x0153b530");
            es.zsceneCurrent().set("");                             // no current scene
            check("assert(zscene_is_loaded('scene_b') == false)", "zs4.lua"); // not current -> false
            es.zsceneCurrent().set("scene_b");
            refuses("zscene_is_loaded('scene_b')", "0x0153b51c");
            refuses("zscene_is_loaded()", "0x0153b51c");
            for (int code : {0, 1, 3}) {
                es.zsceneStateCode().set(code);
                check("assert(zscene_is_loaded('scene_b') == false and zscene_is_loaded() == false)", "zs5.lua");
            }
            es.zsceneStateCode().set(2);                            // 2 = loaded
            check("assert(zscene_is_loaded('Scene_B') == true and zscene_is_loaded() == true)", "zs6.lua");
            check("assert(zscene_is_loaded(nil) == true and zscene_is_loaded(true) == true)", "zs7.lua"); // no name
            refuses("zscene_is_loaded(12)", "'12'");                // a number is a name
            es.zsceneLoadable().set("scene_c", true);
            check("assert(zscene_is_loaded('scene_c') == false)", "zs8.lua"); // loaded scene is another one

            // zscene_prep: gate, teardown, pending.
            check("assert(select('#', zscene_prep('scene_a')) == 0)", "zp1.lua"); // not kind 1: nothing
            CHECK(!es.zscenePending().known());
            refuses("zscene_prep('unknown_scene')", "unknown_scene");
            es.zsceneSkipAllCutscenes().set(true);
            check("zscene_prep('scene_c')", "zp2.lua");               // skip byte: refused
            CHECK(!es.zscenePending().known());
            es.zsceneSkipAllCutscenes().set(false);
            check("zscene_prep('scene_b')", "zp3.lua");               // already current: no change
            CHECK(!es.zscenePending().known() && es.zsceneStateCode().get() == 2);
            // Teardown of the current scene reads the cutscene guard (cutscene
            // state 0x0153b520 in 7..13 with a manager whose +8 == 1), then
            // the handle class; refused with nothing written.
            refuses("zscene_prep('scene_c')", "0x0153b520");
            CHECK(!es.zscenePending().known());
            es.cutsceneState().set(8);
            es.cutsceneManager().present.set(true);
            es.cutsceneManager().field8.set(1);                       // under a cutscene: no teardown
            check("zscene_prep('SCENE_C')", "zp4.lua");
            CHECK(es.zscenePending().get() == "scene_c" && es.zsceneStateCode().get() == 2);
            es.zscenePending().forget();
            es.cutsceneState().set(0);                                // guard off
            refuses("zscene_prep('scene_c')", "handle class");
            CHECK(!es.zscenePending().known());
            es.zsceneHandleClass().set("scene_b", 1);                 // not live, c == 0: nothing
            check("zscene_prep('scene_c')", "zp5.lua");
            CHECK(es.zscenePending().get() == "scene_c" && es.zsceneStateCode().get() == 2);
            es.zsceneHandleClass().set("scene_b", 3);                 // live: state := 0
            check("zscene_prep('scene_c')", "zp6.lua");
            CHECK(es.zscenePending().get() == "scene_c" && es.zsceneStateCode().get() == 0);
            CHECK(es.zsceneCurrent().get() == "scene_b");            // current left as it was
            CHECK(!es.zsceneHandleClass().known("scene_b"));          // released handle's class: OPEN
            CHECK(es.zsceneAutoSelectNearest().get() == false && es.zsceneRequeueOnReset().get() == false);
            // The pending entry: false until the per-frame driver promotes it;
            // refused only while that driver is stopped on an OPEN read.
            // 2026-10-02 correction (spec-lua-api-behaviour.md Sec26.25's
            // corrected Host summary, the `mm_p_01` zscene-promotion-driver
            // investigation): completion and promotion each run every frame
            // independently of the cutscene state, which is no longer what
            // stops this frame - both steps instead hit the 'scene_b' handle
            // class this test already released (OPEN since line ~1241 above)
            // while resetting the still-current, not-pending 'scene_b' entry.
            check("assert(zscene_is_loaded('scene_c') == false)", "zs8b.lua");
            es.cutsceneState().forget();
            es.cutsceneHostFrame();                                   // stops on 'scene_b' handle class
            CHECK(es.zscenePendingBlockedRefusals() == 0);
            refuses("zscene_is_loaded('scene_c')", "0x00dafb60");
            CHECK(es.zscenePendingBlockedRefusals() == 1);
            CHECK(!es.cutsceneState().known()); // still never claimed to be anything
            es.cutsceneState().set(0);
            check("assert(zscene_is_loaded('scene_b') == false)", "zs9.lua"); // current, state 0
            check("assert(zscene_is_loaded() == false)", "zs10.lua");
            // No current scene: prep tears nothing down.
            es.zsceneCurrent().set("");
            es.zsceneStateCode().set(1);
            check("zscene_prep('scene_b')", "zp7.lua");
            CHECK(es.zscenePending().get() == "scene_b" && es.zsceneStateCode().get() == 1);
            check("assert(pcall(zscene_is_loaded, 'unset_scene') == false)", "zs16.lua");
        }

        // 24-26. Cloud phase batch 1.
        {
            auto check = [&](const char* chunk, const char* tag) {
                auto r = host.runChunk(gp, chunk, tag);
                CHECK(r.loadOk && r.pcallOk);
            };
            // set_mission_author (Sec6.1): inert, 0 results, any arguments.
            check("assert(select('#', set_mission_author()) == 0)", "sma1.lua");
            check("assert(select('#', set_mission_author('x', 1, true)) == 0)", "sma2.lua");

            // fade_out (Sec2.9, Sec26.24). Before any call: no colour, no
            // requests, state 2 (start-up) with the init run at registration.
            CHECK(!es.hasScreenFadeColour());
            CHECK(es.screenFadeRequests().empty());
            CHECK(es.screenFade().state.get() == 2 && es.screenFade().flag.get() == 1);
            check("assert(select('#', fade_out(2.5)) == 0)", "fo1.lua");
            CHECK(es.hasScreenFadeColour());
            CHECK(es.screenFadeColour().r == 0.0f && es.screenFadeColour().g == 0.0f &&
                  es.screenFadeColour().b == 0.0f && es.screenFadeColour().a == 1.0f);
            CHECK(es.screenFadeRequests().size() == 1);          // default flags 3: bit 0x1
            CHECK(es.screenFadeRequests()[0].durationMs == 2500.0);
            CHECK(es.screenFadeRequests()[0].targetAlpha == 1.0f);
            CHECK(es.screenFade().state.get() == 1 && es.screenFade().target.get() == 3); // fading out
            CHECK(es.screenFade().flag.get() == 0);
            CHECK(es.screenFadeOpcode53Count() == 0);             // bit 0x2, but no session: host gate fails
            // Colour table, one nil component -> 0; flags 1 only.
            check("fade_out(0.5, {255, nil, 51}, 1)", "fo2.lua");
            CHECK(es.screenFadeColour().r == 1.0f && es.screenFadeColour().g == 0.0f &&
                  es.screenFadeColour().b == 51.0f / 255.0f);
            CHECK(es.screenFadeRequests().size() == 2 && es.screenFadeRequests()[1].durationMs == 500.0);
            CHECK(es.screenFadeOpcode53Count() == 0);
            // Flags 2 only: no request; the broadcast is host-gated (Sec26.24).
            check("fade_out(1, nil, 2)", "fo3.lua");
            CHECK(es.screenFadeRequests().size() == 2 && es.screenFadeOpcode53Count() == 0);
            es.coopSession().present.set(true);
            es.coopSession().localIsHost.set(true);
            check("fade_out(1, nil, 2)", "fo3b.lua");
            CHECK(es.screenFadeOpcode53Count() == 1 && es.screenFade().lastBroadcastWasOut.get());
            es.coopSession().localIsHost.set(false);               // client: no record
            check("fade_out(1, nil, 2)", "fo3c.lua");
            CHECK(es.screenFadeOpcode53Count() == 1);
            es.coopSession().localIsHost.forget();
            refusesOpen(gp, "fade_out(1, nil, 2)", "host check");   // gate read before any write
            es.coopSession().present.set(false);
            check("fade_out(1, nil, nil)", "fo4.lua");
            CHECK(es.screenFadeRequests().size() == 3 && es.screenFadeOpcode53Count() == 1);
            // Flags 0: colour still set, nothing requested.
            check("fade_out(1, {10, 20, 30}, 0)", "fo5.lua");
            CHECK(es.screenFadeColour().g == 20.0f / 255.0f);
            CHECK(es.screenFadeRequests().size() == 3 && es.screenFadeOpcode53Count() == 1);
            // trunc(seconds x 1000): 0.0015 s -> 1 ms; a non-finite duration is
            // refused (only when bit 0x1 needs it).
            check("fade_out(0.0015, nil, 1)", "fo5b.lua");
            CHECK(es.screenFadeRequests().back().durationMs == 1.0);
            {
                auto r = host.runChunk(gp, "fade_out(1/0, nil, 1)", "fo5c.lua");
                CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("fade duration") != std::string::npos);
            }
            check("fade_out(0/0, nil, 0)", "fo5d.lua");
            // Absent duration reads as 0 (lua_tonumber, no gate).
            check("fade_out()", "fo6.lua");
            CHECK(es.screenFadeRequests().size() == 5 && es.screenFadeRequests()[4].durationMs == 0.0);
            // A non-table colour argument is indexed like the real call: Lua error.
            {
                size_t before = es.screenFadeRequests().size();
                auto r = host.runChunk(gp, "fade_out(1, 5)", "fo7.lua");
                CHECK(r.loadOk && !r.pcallOk);
                CHECK(r.pcallError.find("attempt to index") != std::string::npos);
                CHECK(es.screenFadeRequests().size() == before); // nothing applied
            }
            // lua_gettable semantics kept (it now runs under fade_out's own
            // lua_pcall, fix for bridge job jklk): __index metamethods are
            // honoured, and an error inside one is an ordinary Lua error.
            check("fade_out(1, setmetatable({}, {__index = function(t, k) return k * 10 end}), 0)", "fo8.lua");
            // Out-of-range / non-finite numbers through the 0x00ea2596 conversion
            // (fade_out's flags argument) are refused as OPEN, not cast (UB).
            {
                auto r = host.runChunk(gp, "fade_out(1, nil, 1e300)", "fo10.lua");
                CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("0x00ea2596") != std::string::npos);
                auto r2 = host.runChunk(gp, "fade_out(1, nil, 0/0)", "fo11.lua");
                CHECK(r2.loadOk && !r2.pcallOk && r2.pcallError.find("0x00ea2596") != std::string::npos);
            }
            CHECK(es.screenFadeColour().r == 10.0f / 255.0f && es.screenFadeColour().b == 30.0f / 255.0f);
            {
                auto r = host.runChunk(gp, "fade_out(1, setmetatable({}, {__index = function() error('boom') end}), 0)", "fo9.lua");
                CHECK(r.loadOk && !r.pcallOk);
                CHECK(r.pcallError.find("boom") != std::string::npos);
            }
            // Refusals raised repeatedly through openGuard, with a heap-allocated
            // (long) scene name, keep their exact message (jklk regression).
            {
                auto r = host.runChunk(gp,
                    // No `string` library in a host state (spec-lua-bindings.md
                    // Sec16.4): every message must equal the first, whose prefix
                    // is checked from C++ below.
                    "for i = 1, 200 do\n"
                    "  local ok, m = pcall(zscene_is_loaded, 'a_scene_name_well_past_the_small_string_buffer')\n"
                    "  assert(not ok and type(m) == 'string')\n"
                    "  GUARD_MSG = GUARD_MSG or m\n"
                    "  assert(m == GUARD_MSG)\n"
                    "  ok = pcall(fade_out, 1, 5)\n"
                    "  assert(not ok)\n"
                    "end", "guard_loop.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << r.pcallError << "\n";
                CHECK(globalString(gp, "GUARD_MSG").rfind("zscene_is_loaded: engine state ", 0) == 0);
            }

            // mission_end_silently (Sec15.23): bit 0x4 always; 0x10 mirrors
            // arg. The word starts fully OPEN; only those two bits become known.
            CHECK(es.missionFlagsWord().knownMask() == 0);
            check("mission_end_silently()", "mes1.lua");
            CHECK(es.missionFlagsWord().knownMask() == 0x14);
            CHECK(es.missionFlagsWord().get(0x14) == 0x4);
            check("mission_end_silently(true)", "mes2.lua");
            CHECK(es.missionFlagsWord().get(0x14) == 0x14);
            check("mission_end_silently(nil)", "mes3.lua");
            CHECK(es.missionFlagsWord().get(0x14) == 0x4);
            check("assert(select('#', mission_end_silently(1)) == 0)", "mes5.lua"); // 1 is truthy
            CHECK(es.missionFlagsWord().get(0x10) == 0x10);
            {
                bool refused = false;
                try {
                    (void)es.missionFlagsWord().get(0x1);
                } catch (const sr3luahost::OpenStateError& e) {
                    refused = std::string(e.what()).find("0x014c848c") != std::string::npos;
                }
                CHECK(refused); // bits no confirmed writer touched stay OPEN
            }
            // Wrong state: gameplay-tagged only.
            auto r = host.runChunk(ui, "assert(fade_out == nil and mission_end_silently == nil)", "b1ui.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // 27. sfx_faded_out / sfx_faded_in (UI) and fade_is_fully_faded_out /
        // fade_is_fully_faded_in (gameplay) (Sec26.9, Sec26.24): state == 3 /
        // state == 2, no arguments, one boolean.
        {
            auto both = [&](lua_State* st, const char* chunk) {
                auto r = host.runChunk(st, chunk, "fq.lua");
                CHECK(r.loadOk && r.pcallOk);
                if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
            };
            for (uint32_t st : {0u, 1u, 2u, 3u}) {
                es.screenFade().state.set(st);
                const std::string out = st == 3 ? "true" : "false";
                const std::string in = st == 2 ? "true" : "false";
                both(ui, ("assert(sfx_faded_out() == " + out + " and sfx_faded_in() == " + in + ")").c_str());
                both(gp, ("assert(fade_is_fully_faded_out() == " + out + " and fade_is_fully_faded_in() == " + in +
                          ")").c_str());
            }
            both(ui, "assert(select('#', sfx_faded_out()) == 1)");
            both(gp, "assert(sfx_faded_out == nil and sfx_faded_in == nil and Screen_fade_transition_complete == nil)");
            both(ui, "assert(fade_is_fully_faded_out == nil and fade_in == nil)");
            es.screenFade().state.forget();
            refusesOpen(ui, "sfx_faded_out()", "0x012e6aa4");
            refusesOpen(gp, "fade_is_fully_faded_in()", "0x012e6aa4");
            auto rl = host.runChunk(ui,
                "for i = 1, 200 do local ok, m = pcall(sfx_faded_out)\n"
                "  assert(not ok and type(m) == 'string'); SFO_MSG = SFO_MSG or m\n"
                "  assert(m == SFO_MSG) end", "sfo_loop.lua");
            CHECK(rl.loadOk && rl.pcallOk);
            CHECK(globalString(ui, "SFO_MSG").rfind("sfx_faded_out: engine state ", 0) == 0);
            es.screenFade().state.set(2);
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
                                  "on_qte_animation_trigger", "on_revived", "game_get_coop_join_type",
                                  "zscene_is_loaded", "set_mission_author", "fade_out",
                                  "mission_end_silently", "sfx_faded_out", "game_get_is_host",
                                  "zscene_prep", "sfx_faded_in", "fade_is_fully_faded_out",
                                  "fade_is_fully_faded_in"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

    // --- Screen fade state machine (spec-lua-api-behaviour.md Sec26.24;
    // batch 2026-10-01), through Lua: the real completion path (the UI
    // script calls Screen_fade_transition_complete), the two labelled
    // host-substitute fallbacks, and the per-frame routine.
    {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();
        auto& f = es.screenFade();
        auto run = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "fade.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        const auto& c = es.screenFadeCounters();

        // Start-up: state 2 (fully in), target 2; init ran at registration
        // (document found, flag 1); the UI state is the screen_fade state.
        CHECK(f.state.get() == 2 && f.target.get() == 2 && f.flag.get() == 1 && f.documentLoaded.get());
        CHECK(es.screenFadeUiState() == ui);
        run(gp, "assert(fade_is_fully_faded_in() == true and fade_is_fully_faded_out() == false)");
        run(ui, "assert(sfx_faded_in() == true and sfx_faded_out() == false)");
        CHECK(es.screenFadeCompletionPathSummary() == "real:0 fallback_undefined:0 fallback_no_callback:0");

        // 1. screen_fade_do undefined -> fallback_undefined after durationMs.
        run(gp, "fade_out(1.0)");
        CHECK(f.state.get() == 1 && f.target.get() == 3 && f.flag.get() == 0);
        CHECK(c.screenFadeDoCalls == 0);
        es.screenFadeHostFrame(999);
        CHECK(f.state.get() == 1 && c.fallbackUndefined == 0);
        es.screenFadeHostFrame(1); // clock 1000 = request + 1000 ms
        CHECK(f.state.get() == 3 && c.fallbackUndefined == 1);
        CHECK(f.logoAt.get() == 2000 && f.holdLogoUntil.get() == -1 && f.imagesAt.get() == -1);
        run(gp, "assert(fade_is_fully_faded_out() == true and fade_is_fully_faded_in() == false)");
        run(ui, "assert(sfx_faded_out() == true)");
        // The per-frame routine stops at the OPEN mode-stack top.
        CHECK(c.framesBlockedOnOpen == 2 && c.lastFrameBlocker.find("mode stack") != std::string::npos);
        // Already fully out: a fade-out request changes nothing.
        run(gp, "fade_out(0.5)");
        CHECK(f.state.get() == 3 && f.target.get() == 3);
        es.screenFadeHostFrame(1000);
        CHECK(c.fallbackUndefined == 1);

        // 2. The real path: screen_fade_do(flag, alpha, ms) runs in the UI
        // state; the script's later call of Screen_fade_transition_complete
        // completes the fade.
        run(ui, "fade_log = {}\n"
                "function screen_fade_do(flag, alpha, ms) fade_log[#fade_log + 1] = {flag, alpha, ms} end");
        run(gp, "fade_in(0.25)");
        CHECK(f.state.get() == 0 && f.target.get() == 2 && c.screenFadeDoCalls == 1);
        CHECK(f.logoAt.get() == -1 && f.imagesAt.get() == -1);  // reset by the fade-in request
        run(ui, "assert(#fade_log == 1 and fade_log[1][1] == 0 and fade_log[1][2] == 0.0 and fade_log[1][3] == 250)");
        run(gp, "assert(fade_is_fully_faded_in() == false)");
        run(ui, "assert(select('#', Screen_fade_transition_complete()) == 0)");
        CHECK(f.state.get() == 2 && c.realCompletions == 1);
        run(gp, "assert(fade_is_fully_faded_in() == true)");
        es.screenFadeHostFrame(1000);                        // the fallback was disarmed
        CHECK(c.fallbackNoCallback == 0 && c.fallbackUndefined == 1);
        run(gp, "fade_out(2, {0, 0, 0}, 1)");
        run(ui, "assert(fade_log[2][1] == 0 and fade_log[2][2] == 1.0 and fade_log[2][3] == 2000)");
        CHECK(f.state.get() == 1);
        run(ui, "Screen_fade_transition_complete()");
        CHECK(f.state.get() == 3 && c.realCompletions == 2);
        // Called again with nothing running: a settled state is left alone.
        run(ui, "Screen_fade_transition_complete()");
        CHECK(f.state.get() == 3 && c.realCompletions == 2);
        run(gp, "assert(Screen_fade_transition_complete == nil)"); // UI state only

        // 3. A synchronous call from inside screen_fade_do runs before the
        // request sets the direction, so it completes nothing; the fade is
        // then completed by fallback_no_callback.
        run(ui, "function screen_fade_do(flag, alpha, ms) Screen_fade_transition_complete() end");
        run(gp, "fade_in(0.5)");
        CHECK(f.state.get() == 0 && c.realCompletions == 2);
        es.screenFadeHostFrame(500);
        CHECK(f.state.get() == 2 && c.fallbackNoCallback == 1);

        // 4. An error inside screen_fade_do stays in the UI state.
        run(ui, "function screen_fade_do() error('fade script boom') end");
        run(gp, "fade_out(0.1)");
        CHECK(c.screenFadeDoErrors == 1 && c.lastScreenFadeDoError.find("fade script boom") != std::string::npos);
        CHECK(f.state.get() == 1);
        es.screenFadeHostFrame(100);
        CHECK(f.state.get() == 3 && c.fallbackNoCallback == 2);
        // A screen_fade_do that never returns is stopped by the UI-call watchdog.
        run(ui, "function screen_fade_do() while true do end end");
        run(gp, "fade_in(0.1)");
        CHECK(c.screenFadeDoErrors == 2 && c.lastScreenFadeDoError.find("watchdog") != std::string::npos);
        es.screenFadeHostFrame(100);
        CHECK(f.state.get() == 2 && c.fallbackNoCallback == 3);
        run(ui, "screen_fade_do = nil");

        // 5. A request parked during the opposite transition is replayed by
        // the per-frame routine with 250 ms, once the state has settled and
        // only when the routine is not stopped by OPEN state.
        run(gp, "fade_out(1)");                               // state 1, target 3
        run(gp, "fade_in(3)");                                // fade-out running: parked
        CHECK(f.state.get() == 1 && f.target.get() == 2);
        CHECK(es.screenFadeRequests().back().durationMs == 3000.0);
        es.screenFadeHostFrame(1000);                         // fade-out completes (fallback)
        CHECK(f.state.get() == 3 && f.target.get() == 2);
        es.screenFadeHostFrame(333);                          // routine blocked: mode stack OPEN
        CHECK(f.state.get() == 3);
        f.modeStackTop.set(0);
        f.cutsceneState.set(0);
        es.screenFadeHostFrame(333);                          // logo stamp not reached: replay
        CHECK(f.state.get() == 0 && f.target.get() == 2 && f.logoAt.get() == -1);
        const int64_t replayAt = es.screenFadeClockMs();
        es.screenFadeHostFrame(249);
        CHECK(f.state.get() == 0);
        es.screenFadeHostFrame(1);                            // 250 ms, not the 3000 asked for
        CHECK(f.state.get() == 2 && es.screenFadeClockMs() == replayAt + 250);

        // 6. Mode 4 resets the four stamps and stops the routine.
        run(gp, "fade_out(0)");
        es.screenFadeHostFrame(1);                            // completes at once (0 ms)
        CHECK(f.state.get() == 3 && f.logoAt.get() == es.screenFadeClockMs() + 1000);
        f.modeStackTop.set(4);
        es.screenFadeHostFrame(1);
        CHECK(f.logoAt.get() == -1 && f.holdLogoUntil.get() == -1);
        f.modeStackTop.set(0);

        // 7. Loading logo, holds and load images, with the script hooks
        // defined in the UI state.
        run(ui, "shown = {}\n"
                "function screen_fade_logo_show() shown[#shown + 1] = 'logo' end\n"
                "function screen_fade_images_show() shown[#shown + 1] = 'images' end");
        f.useLoadImages.set(true);
        run(gp, "fade_in(0)");                                 // 3 -> fading in
        es.screenFadeHostFrame(1);
        CHECK(f.state.get() == 2);
        run(gp, "fade_out(0)");
        es.screenFadeHostFrame(3);
        const int64_t outDone = es.screenFadeClockMs();
        CHECK(f.state.get() == 3 && f.logoAt.get() == outDone + 1000);
        es.screenFadeHostFrame(1001);                          // logo stamp passed
        const int64_t logoShown = es.screenFadeClockMs();
        run(ui, "assert(#shown == 1 and shown[1] == 'logo')");
        CHECK(f.logoAt.get() == -1 && f.holdLogoUntil.get() == logoShown + 1000);
        CHECK(f.imagesAt.get() == logoShown + 6000 && f.holdImagesUntil.get() == -1);
        CHECK(c.uiScriptCalls == 1);
        // A fade-in now is parked by the logo hold (state left at 3).
        run(gp, "fade_in(1)");
        CHECK(f.state.get() == 3 && f.target.get() == 2);
        es.screenFadeHostFrame(999);                           // hold not passed
        CHECK(f.state.get() == 3);
        es.screenFadeHostFrame(3);                             // hold passed, images not due: replay
        CHECK(f.state.get() == 0 && f.imagesAt.get() == -1);   // the fade-in resets the images stamp
        es.screenFadeHostFrame(250);
        CHECK(f.state.get() == 2);
        // Images path: set the stamps directly (values only the routine writes).
        f.imagesAt.set(es.screenFadeClockMs() - 1);
        es.screenFadeHostFrame(1);
        run(ui, "assert(#shown == 2 and shown[2] == 'images')");
        CHECK(f.imagesAt.get() == -1 && f.holdImagesUntil.get() == es.screenFadeClockMs() + 1500);
        run(gp, "fade_out(0)");                                // fade-out is not held
        CHECK(f.state.get() == 1);
        es.screenFadeHostFrame(1);
        CHECK(f.state.get() == 3);
        CHECK(f.holdImagesUntil.get() == -1);                  // the fade-out completion cleared the holds
        f.holdImagesUntil.set(es.screenFadeClockMs() + 500);
        run(gp, "fade_in(0)");                                 // held by the images stamp
        CHECK(f.state.get() == 3 && f.target.get() == 2);
        // A stamp met by the clock exactly has no specified result: OPEN.
        f.holdImagesUntil.set(es.screenFadeClockMs());
        {
            auto r = host.runChunk(gp, "fade_in(0)", "eq.lua");
            CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("exact equality") != std::string::npos);
        }
        f.holdImagesUntil.set(-1);
        f.logoAt.set(-1);

        // 8. Durations: trunc(seconds x 1000); non-finite refused before any write.
        const auto before = es.screenFadeRequests().size();
        {
            auto r = host.runChunk(gp, "fade_in(1e300)", "dur.lua");
            CHECK(r.loadOk && !r.pcallOk && r.pcallError.find("fade duration") != std::string::npos);
        }
        CHECK(es.screenFadeRequests().size() == before);
        run(gp, "fade_in(0/0, 2)");                            // bit 0x1 clear: no conversion
        run(gp, "assert(select('#', fade_in(0.0019, 1)) == 0)");
        CHECK(es.screenFadeRequests().back().durationMs == 1.0 && es.screenFadeRequests().back().targetAlpha == 0.0f);
        // fade_in's broadcast: host-gated like fade_out's.
        CHECK(es.screenFadeOpcode53Count() == 0);
        es.coopSession().present.set(true);
        es.coopSession().localIsHost.set(true);
        run(gp, "fade_in(1, 2)");
        CHECK(es.screenFadeOpcode53Count() == 1 && !f.lastBroadcastWasOut.get());
        es.coopSession().present.set(false);

        CHECK(es.screenFadeCompletionPathSummary() ==
              "real:" + std::to_string(c.realCompletions) + " fallback_undefined:" +
                  std::to_string(c.fallbackUndefined) + " fallback_no_callback:" + std::to_string(c.fallbackNoCallback));
        CHECK(c.realCompletions == 2);
    }

    // --- Screen fade callbacks (C++ level, Sec26.24): the completion body
    // calls the in-flight callback with the TARGET, handles the deferred slot,
    // and a settled request calls back at once. No UI state attached here,
    // so screen_fade_do counts as undefined.
    {
        EngineState es;
        sr3luahost::applySpecInitialState(es);
        auto& f = es.screenFade();
        f.modeStackTop.set(0);
        f.cutsceneState.set(0);
        CHECK(!f.documentLoaded.get());
        g_fadeCalls.clear();
        es.screenFadeRequest(false, 0, fadeCbC, 7);             // already fully in
        CHECK(g_fadeCalls.size() == 1 && g_fadeCalls[0] == std::make_pair('C', 2u));
        CHECK(f.flag.get() == 0);                               // stopped before step 3
        g_fadeCalls.clear();
        es.screenFadeRequest(true, 100, fadeCbA, 5);            // out, in flight A
        CHECK(f.state.get() == 1 && f.inFlight == fadeCbA && f.flag.get() == 5);
        es.screenFadeRequest(false, 100, fadeCbB, 6);           // parked: deferred B
        CHECK(f.state.get() == 1 && f.target.get() == 2 && f.deferred == fadeCbB && f.inFlight == fadeCbA);
        CHECK(es.screenFadeCompletionBody());                   // 1 -> 3; A(target 2)
        CHECK(f.state.get() == 3 && g_fadeCalls.size() == 1 && g_fadeCalls[0] == std::make_pair('A', 2u));
        CHECK(f.inFlight == nullptr && f.deferred == fadeCbB);  // 3 != target 2: deferred kept
        es.screenFadeHostFrame(1);                              // replay with B, 250 ms, flag 6
        CHECK(f.state.get() == 0 && f.inFlight == fadeCbB && f.flag.get() == 6);
        CHECK(es.screenFadeCompletionBody());                   // 0 -> 2; B(2); deferred == in-flight: cleared
        CHECK(g_fadeCalls.size() == 2 && g_fadeCalls[1] == std::make_pair('B', 2u));
        CHECK(f.inFlight == nullptr && f.deferred == nullptr);
        CHECK(!es.screenFadeCompletionBody());                  // settled: no flip, nothing called
        CHECK(g_fadeCalls.size() == 2);
        // Deferred different from in-flight, settled at the target: both called.
        g_fadeCalls.clear();
        es.screenFadeRequest(true, 100, fadeCbA, 0);            // out, A
        es.screenFadeRequest(false, 100, fadeCbB, 0);           // parked B, target 2
        es.screenFadeRequest(true, 100, fadeCbC, 0);            // target back to 3; state 1: restarts, C in flight
        CHECK(f.state.get() == 1 && f.target.get() == 3 && f.inFlight == fadeCbC && f.deferred == fadeCbB);
        CHECK(es.screenFadeCompletionBody());
        CHECK(g_fadeCalls.size() == 2 && g_fadeCalls[0] == std::make_pair('C', 3u) &&
              g_fadeCalls[1] == std::make_pair('B', 3u));
        CHECK(f.inFlight == nullptr && f.deferred == nullptr && f.state.get() == 3);
        // The fallback timer only completes the transition it was armed for.
        // Every flip above came from the body directly: no timer fired.
        CHECK(es.screenFadeCounters().fallbackUndefined == 0 && es.screenFadeCounters().realCompletions == 0);
    }

    // --- UI resolution queries (Sec26.26) through Lua.
    {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();
        auto run = [&](const char* chunk) {
            auto r = host.runChunk(ui, chunk, "vint.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        auto refuses = [&](const char* chunk, const char* needle) {
            auto r = host.runChunk(ui, chunk, "vint_ref.lua");
            CHECK(r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos);
            if (r.pcallError.find(needle) == std::string::npos) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        {
            auto r = host.runChunk(gp, "assert(vint_is_std_res == nil and vint_get_safe_frame == nil)", "v0.lua");
            CHECK(r.loadOk && r.pcallOk);
        }
        refuses("vint_is_std_res()", "0x00e236f0");
        es.vintRecordFirst().set(1024);
        es.vintRecordSecond().set(768);                       // 1.333 < 1.5: mode not read
        run("assert(vint_is_std_res() == true and select('#', vint_is_std_res(1, 2)) == 1)");
        es.vintRecordFirst().set(1280);
        es.vintRecordSecond().set(1024);                      // 5:4
        run("assert(vint_is_std_res() == true)");
        es.vintRecordFirst().set(1920);
        es.vintRecordSecond().set(1080);
        refuses("vint_is_std_res()", "0x0132bd80");
        es.vintDisplayMode().set(-1);
        run("assert(vint_is_std_res() == false)");
        es.vintDisplayMode().set(2);
        run("assert(vint_is_std_res() == true)");
        for (int mode : {0, 1, 3, 4}) {
            es.vintDisplayMode().set(mode);
            run("assert(vint_is_std_res() == false)");
        }
        es.vintRecordFirst().set(1500);
        es.vintRecordSecond().set(1000);                      // exactly 1.5: not below
        run("assert(vint_is_std_res() == false)");
        es.vintRecordFirst().set(0);
        es.vintRecordSecond().set(0);                         // 0/0 = NaN: not below
        run("assert(vint_is_std_res() == false)");
        es.vintRecordFirst().set(-5);
        es.vintRecordSecond().set(0);                         // -inf: below
        run("assert(vint_is_std_res() == true)");

        refuses("vint_get_safe_frame()", "+0x8");
        es.vintSafeFrameA().set(1280);
        es.vintSafeFrameB().set(720);
        // The constants are CONFIRMED (job nnlt) and set at start; Sec26.26's
        // worked value for 1280 x 720. Fuller coverage:
        // tests/synthetic_luahost_cutscene_test.cpp.
        run("local a, b, c, d = vint_get_safe_frame()\n"
            "assert(a == 96 and b == 54 and c == 1184 and d == 666)\n"
            "assert(select('#', vint_get_safe_frame()) == 4)");
        es.vintSafeFrameScale1().set(0.0625);
        es.vintSafeFrameScale2().set(0.9375);
        run("local a, b, c, d = vint_get_safe_frame()\n"
            "assert(a == 80 and b == 45 and c == 1200 and d == 675)");
        es.vintSafeFrameScale2().set(1.0 / 3.0);              // 426.67 / 240: round to nearest
        run("local a, b, c, d = vint_get_safe_frame()\nassert(c == 427 and d == 240)");
        es.vintSafeFrameScale1().set(0.5);
        es.vintSafeFrameA().set(3);                            // 1.5: an exact tie, mode not stated
        refuses("vint_get_safe_frame()", "tie");
    }

    // --- Hook evidence labels (spec-lua-bindings.md §4 desk review,
    // 2026-09-30): nothing removed, every Group 1 name labelled.
    {
        int sec8 = 0, pending = 0, refuted = 0, groupLevel = 0;
        for (const auto& h : sr3luahost::confirmedHooks()) {
            switch (h.evidence) {
                case sr3luahost::HookEvidence::Sec8Confirmed: ++sec8; break;
                case sr3luahost::HookEvidence::Sec4ScanPendingRecheck: ++pending; break;
                case sr3luahost::HookEvidence::RefutedSec14_3: ++refuted; break;
                case sr3luahost::HookEvidence::GroupLevel: ++groupLevel; break;
            }
            if (h.group != sr3luahost::HookGroup::Group1_GeneralNamed) CHECK(h.evidence == sr3luahost::HookEvidence::GroupLevel);
        }
        CHECK(refuted == 5);
        CHECK(pending == 42);
        CHECK(sec8 + pending + refuted == 78); // every Group 1 name labelled
        CHECK(groupLevel == static_cast<int>(sr3luahost::confirmedHooks().size()) - 78);
        for (const auto& h : sr3luahost::confirmedHooks()) {
            if (h.name == "screen_fade_do" || h.name == "hud_running_man_event_update")
                CHECK(h.evidence == sr3luahost::HookEvidence::Sec8Confirmed);
            if (h.name == "garage_populate") CHECK(h.evidence == sr3luahost::HookEvidence::RefutedSec14_3);
            if (h.name == "newsticker_populate") CHECK(h.evidence == sr3luahost::HookEvidence::Sec4ScanPendingRecheck);
        }
    }

    // --- Engine-state slot inventory and applySpecInitialState (batch
    // 2026-10-01): the CONFIRMED start-up values are set, everything else
    // stays OPEN.
    {
        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        const auto inv = es.openSlotInventory();
        std::unordered_map<std::string, int> perArea, knownPerArea;
        for (const auto& r : inv) {
            ++perArea[r.area];
            if (r.known || r.knownKeys > 0) ++knownPerArea[r.area];
            CHECK(!r.global.empty() && r.spec.rfind("spec-", 0) == 0);
        }
        // 60 = the 40 of the first batch + 6 zscene (Sec26.25 lifecycle
        // driver and scene table, nnlt) + 10 cutscene machine + 3 vint (width,
        // height, layout index) + 1 vint (vintTimeIndexByDoc_, Sec19.1 batch
        // 2026-10-02). The Sec27/Sec28 batch's own area ("batch2728"), the
        // Sec31/Sec32 batch's own area ("batch3132", 2026-10-02), the
        // Sec38/Sec39/Sec40 batch's own area ("ranking0608", 2026-10-02 -
        // saveSystemUi_.slotCount, pcuCategoryTable_.kindByIndex,
        // pcuCatalogOutfits_.flagsByIndex; see lua_spec_confirmed_stubs.cpp's
        // own Sec38/Sec39/Sec40 batch header - most of that batch's own new
        // fields are CHOSEN plain bools/per-entity maps, not global
        // OpenValue/OpenValueMap singletons, so only these 3 land here), and
        // the resumed-session Sec44/Sec45/Sec46 batch's own area
        // ("batch444546", 2026-10-02 - 5 rows: ambientGangSpawnEnabled_/
        // cellCameraEnabled_/ambientCopSpawnEnabled_/actionNodesShouldntFlee_/
        // actionNodesRestrictSpawning_), and the Sec49 batch's own area
        // ("ranking16", 2026-10-03 - spawnOverride_.categoryNameResolves;
        // see lua_spec_confirmed_stubs.cpp's own Sec49 batch header - the
        // rest of that batch's own new fields are likewise CHOSEN plain
        // bools/per-entity maps, so only this 1 lands here) are each
        // counted separately (dynamically, via perArea) so every batch's
        // own check stays independent of the others' exact row counts.
        CHECK(inv.size() == 60u + static_cast<size_t>(perArea["batch2728"]) +
                             static_cast<size_t>(perArea["batch3132"]) +
                             static_cast<size_t>(perArea["ranking0608"]) +
                             static_cast<size_t>(perArea["batch444546"]) +
                             static_cast<size_t>(perArea["ranking16"]));
        CHECK(perArea["co-op"] == 6 && perArea["tutorial"] == 1 && perArea["vehicle-store"] == 1);
        CHECK(perArea["zscene"] == 13 && perArea["cutscene"] == 10 && perArea["fade"] == 14 &&
              perArea["vint"] == 11 && perArea["other"] == 4);
        CHECK(perArea["ranking0608"] == 3 && knownPerArea["ranking0608"] == 0); // all 3 start OPEN/empty
        CHECK(perArea["ranking16"] == 1 && knownPerArea["ranking16"] == 0);     // starts OPEN/empty
        CHECK(knownPerArea["co-op"] == 1 && knownPerArea["tutorial"] == 1 && knownPerArea["vehicle-store"] == 1);
        // vint: the two safe-frame constants (CONFIRMED, nnlt) are set at start;
        // vintTimeIndexByDoc_ (a per-name map) starts empty (OPEN, Sec19.1), so
        // it doesn't add to the known count.
        // zscene: the skip byte 0x0153b556 (false, Sec26.25 Globals, 2026-10-01)
        // and the current entry 0x0153b530 (null, Sec26.25 Globals, 2026-10-02).
        // other: objectResolves_ now carries 5 known entries (Sec29's closed
        // literal set - "homies"/"shopkeepers"/"-- Cutscene Script Group --"/
        // "#PLAYER1#"/"#PLAYER2#"), so its one inventory record counts as known.
        CHECK(knownPerArea["zscene"] == 2 && knownPerArea["cutscene"] == 0 && knownPerArea["fade"] == 12 &&
              knownPerArea["vint"] == 2 && knownPerArea["other"] == 1);
        for (const auto& r : inv) {
            if (r.global.find("0x024d8534") != std::string::npos) CHECK(r.known);
            if (r.global.find("0x0151d600") != std::string::npos) CHECK(r.knownKeys == 210);
            if (r.global.find("0x022cdf08") != std::string::npos) CHECK(r.known);
            if (r.global.find("mode stack") != std::string::npos) CHECK(!r.known);
            if (r.global.find("0x0153b520") != std::string::npos) CHECK(!r.known);
            if (r.global.find("0x012f44fc") != std::string::npos) CHECK(!r.known);
        }
        // Values (spec text): co-op none; store flag 0; fade globals at their
        // file values; tutorial states after the fill.
        CHECK(es.coopSession().present.get() == false);
        CHECK(!es.coopIsActive() && !es.coopLocalIsHost() && !es.coopLocalIsClient());
        CHECK(es.vehicleStoreActive().get() == false);
        const auto& f = es.screenFade();
        CHECK(f.state.get() == 2 && f.target.get() == 2 && f.flag.get() == 0 && !f.documentLoaded.get());
        CHECK(f.logoAt.get() == -1 && f.holdLogoUntil.get() == -1 && f.imagesAt.get() == -1 &&
              f.holdImagesUntil.get() == -1 && f.autoSaveStamp.get() == -1);
        // 0x0149365c: set to 1 by the start-up 0x005d1a30 (Sec26.24, nnlt).
        CHECK(f.autoSaveCounter.get() == 0 && f.useLoadImages.get() && !f.lastBroadcastWasOut.get());
        CHECK(f.inFlight == nullptr && f.deferred == nullptr);
        for (int i = 0; i < EngineState::kTutorialEntryCount; ++i)
            CHECK(es.tutorialState().get(EngineState::tutorialStateKey(i)) == (i <= 188 ? 0 : 1));
        CHECK(es.zsceneSkipAllCutscenes().known() && es.zsceneSkipAllCutscenes().get() == false);
        CHECK(es.zsceneCurrent().known() && es.zsceneCurrent().get().empty()); // 0x0153b530 null at start
        CHECK(!es.zsceneStateCode().known() && !es.zscenePending().known());
        CHECK(!es.vintDisplayMode().known() && !es.vintRecordFirst().known());
        CHECK(es.vintSafeFrameScale1().get() == static_cast<double>(0.075f) &&
              es.vintSafeFrameScale2().get() == static_cast<double>(0.925f));
        // The inventory follows writes: a value, a per-name key, and bits.
        es.zsceneStateCode().set(2);
        es.zsceneLoadable().set("scene_a", true);
        es.missionFlagsWord().setBits(0x14, 0x4);
        for (const auto& r : es.openSlotInventory()) {
            if (r.global.find("0x0153b51c") != std::string::npos) CHECK(r.known && r.knownKeys == 1);
            if (r.global.find("0x00723d20") != std::string::npos) CHECK(!r.known && r.knownKeys == 1);
            if (r.global == "0x014c848c") CHECK(!r.known && r.knownKeys == 2);
        }
        // Bare-global table (Sec13.2/Sec16.4, answered 2026-10-01): all 24
        // names, each into BOTH states, each also a spec-confirmed name.
        for (const auto& bg : sr3luahost::specBareGlobals()) {
            CHECK(bg.gameplay || bg.ui);
            const auto& n = sr3luahost::specConfirmedStubNames();
            CHECK(std::find(n.begin(), n.end(), bg.name) != n.end());
            CHECK(bg.spec.rfind("spec-", 0) == 0);
        }
        CHECK(sr3luahost::specBareGlobals().size() == 24);
        for (const auto& bg : sr3luahost::specBareGlobals()) CHECK(bg.gameplay && bg.ui);
        // Host applies the same initial state. With no UI state holding
        // Screen_fade_transition_complete, the fade init does not run.
        sr3luahost::Host h({});
        CHECK(h.engineState().vehicleStoreActive().get() == false);
        CHECK(h.engineState().coopSession().present.get() == false);
        CHECK(!h.engineState().screenFade().documentLoaded.get());
        CHECK(h.engineState().screenFadeUiState() == nullptr);
    }

    // --- Named-object resolution's closed literal set (Sec29, 2026-10-02):
    // applySpecInitialState() pre-populates exactly the 5 names Sec29.4
    // confirms engine code registers unconditionally - everything else,
    // including a mission character's own fixed name, must stay OPEN. This
    // is a regression guard against ever accidentally widening the set.
    {
        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        auto& resolves = es.objectResolves();
        for (const std::string& name :
             {"homies", "shopkeepers", "-- Cutscene Script Group --", "#PLAYER1#", "#PLAYER2#"}) {
            CHECK(resolves.known(name));
            CHECK(resolves.get(name) == true);
        }
        CHECK(resolves.knownCount() == 5);
        // An arbitrary name NOT in the closed set - including the actual
        // mission-blocker name - stays OPEN: producer (3) (173 in-memory
        // call sites, never individually traced) could register essentially
        // any name, so this project does not know and must not guess.
        for (const std::string& name : {"Killbane", "homie", "Homies", "#PLAYER3#", "some_other_npc"}) {
            CHECK(!resolves.known(name));
            bool threw = false;
            try {
                (void)resolves.get(name);
            } catch (const sr3luahost::OpenStateError& e) {
                threw = true;
                CHECK(std::string(e.what()).find("named-object resolution['" + name + "']") != std::string::npos);
            }
            CHECK(threw);
        }
    }

    // --- Player gender initial state (2026-10-02, spec-lua-api-behaviour.md
    // Sec16.8/Sec16.8a, spec-save-format.md Sec10.4, spec-tables-
    // customization.md Sec16.2): the raw, un-initialised engine never
    // observes the engine's own fresh-object sentinel (+0xa41 := 3) -
    // characterGender() stays fully OPEN for every name, "#PLAYER1#"
    // included, until applySpecInitialState() runs. Once it does, the
    // fresh-game default-preset path (this host never loads a real save -
    // tools/lua_host_run.cpp has no save-file reference anywhere) resolves
    // "#PLAYER1#" to the shipped player_presets.xtbl Default row
    // ("male_white", Gender=0) - the mechanism that resolves mission m21's
    // "character +0xa41 gender byte (1=female, else male)['#PLAYER1#'] is
    // OPEN" blocker.
    {
        sr3luahost::EngineState raw; // no applySpecInitialState: the sentinel is never directly observable
        CHECK(!raw.characterGender().known("#PLAYER1#"));
        bool threw = false;
        try {
            (void)raw.characterGender().get("#PLAYER1#");
        } catch (const sr3luahost::OpenStateError& e) {
            threw = true;
            CHECK(std::string(e.what()).find("gender byte (1=female, else male)['#PLAYER1#']") != std::string::npos);
        }
        CHECK(threw);

        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        CHECK(es.characterGender().known("#PLAYER1#"));
        CHECK(es.characterGender().get("#PLAYER1#") == 0); // male, Default preset row
        // "#PLAYER2#" resolves by name (Sec29.4) but no co-op session is
        // active at start (item 1), so no second player object exists to
        // even hold a sentinel pair - its gender correctly stays OPEN.
        CHECK(!es.characterGender().known("#PLAYER2#"));
        // Race (+0xa40) has no modelled reader anywhere in this codebase
        // yet, so it is intentionally not set here - nothing to query means
        // nothing to leave correctly-OPEN or wrongly-guessed.

        Host host({});
        CHECK(host.engineState().characterGender().get("#PLAYER1#") == 0);
    }

    // --- Game clock initial state + per-frame advance (2026-10-03,
    // spec-lua-api-behaviour.md Sec48, corrects Sec32.1): the raw engine
    // never observes the clock's hour/minute bytes - OPEN until set, same
    // as every other OpenValue. applySpecInitialState() now sets them to
    // 10:00:00 (the CONFIRMED single-player new-game value, Sec48.2) -
    // this is the actual mechanism that closes the dlc3_m01/m19/etc.
    // blocker, since set_time_of_day's own forward-delta arithmetic reads
    // the current hour/minute before anything else.
    {
        sr3luahost::EngineState raw;
        CHECK(!raw.gameClock().hour.known() && !raw.gameClock().minute.known());

        sr3luahost::EngineState es;
        sr3luahost::applySpecInitialState(es);
        CHECK(es.gameClock().hour.get() == 10 && es.gameClock().minute.get() == 0);

        Host host({});
        CHECK(host.engineState().gameClock().hour.get() == 10);
        CHECK(host.engineState().gameClock().minute.get() == 0);

        // gameClockAdvanceFrame (Sec48.3, CONFIRMED mechanism): 40x real
        // time. 60 real seconds -> 2400 game seconds = 40 game minutes:
        // 10:00:00 -> 10:40:00.
        es.gameClockAdvanceFrame(60.0);
        CHECK(es.gameClock().hour.get() == 10 && es.gameClock().minute.get() == 40);
        // Accumulates across multiple calls (this host's own per-tick
        // cadence, tools/lua_host_run.cpp): 3 more real minutes (180s) ->
        // 7200 game seconds = 2 game hours: 10:40 -> 12:40.
        for (int i = 0; i < 3; ++i) es.gameClockAdvanceFrame(60.0);
        CHECK(es.gameClock().hour.get() == 12 && es.gameClock().minute.get() == 40);
        // Day wrap (STATED SIMPLIFICATION, GameClock's own doc comment):
        // 12:40 + 1200 real seconds (48000 game seconds = 13h20m) wraps
        // past 24:00 within the same tracked day: 12:40 + 13:20 = 26:00 ->
        // 02:00 the "same" modelled day (date fields not advanced).
        es.gameClockAdvanceFrame(1200.0);
        CHECK(es.gameClock().hour.get() == 2 && es.gameClock().minute.get() == 0);

        // set_time_of_day (Sec48.4): forgets hour/minute rather than
        // fabricating the post-snap value - advancing after that is
        // correctly a no-op (no known base to add a delta to). Called
        // directly (the Lua stub itself is exercised end-to-end in
        // synthetic_luahost_batch31_32_test.cpp) since this host wasn't
        // constructed with set_time_of_day in its registration list.
        host.engineState().setTimeOfDay(5, 0);
        CHECK(!host.engineState().gameClock().hour.known());
        double before = host.engineState().gameClock().frameAdvanceSecondsAccumulated;
        host.engineState().gameClockAdvanceFrame(60.0);
        CHECK(!host.engineState().gameClock().hour.known()); // still unknown, not fabricated
        CHECK(host.engineState().gameClock().frameAdvanceSecondsAccumulated == before + 60.0); // still tracked
    }

    // --- Batch 2026-10-02: spec-lua-bindings.md Sec18-Sec21 ("Vint UI
    // API"), 8 names - vint_object_first_child/vint_object_clone (Sec18),
    // vint_get_time_index/vint_dataitem_get (Sec19), vint_set_property/
    // vint_get_property (Sec20), vint_dataresponder_finished/
    // vint_internal_dataresponder_request (Sec21). Same "exercised through
    // the real Lua-visible global, a genuine round-trip through the real
    // Lua 5.1 C API" convention as every other spec-confirmed test above.
    {
        Host host(specConfirmedFixtureNames());
        lua_State* ui = host.uiState();
        lua_State* gp = host.gameplayState();
        sr3luahost::EngineState& es = host.engineState();
        auto refusesOpen = [&](lua_State* st, const std::string& chunk, const char* needle) {
            auto r = host.runChunk(st, chunk, "vint1821_open.lua");
            CHECK(r.loadOk && !r.pcallOk);
            CHECK(r.pcallError.find("is OPEN") != std::string::npos);
            CHECK(r.pcallError.find(needle) != std::string::npos);
        };
        auto ok = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "vint1821.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };
        auto globalNumber = [&](lua_State* st, const char* name) -> double {
            lua_getglobal(st, name);
            double v = lua_tonumber(st, -1);
            lua_pop(st, 1);
            return v;
        };

        // All 8 are `ui`-tagged (CONFIRMED, tools/lua_all_registered_1490_
        // tagged.txt) - nil in gameplay.
        ok(gp,
           "assert(vint_object_first_child == nil and vint_object_clone == nil and "
           "vint_get_time_index == nil and vint_dataitem_get == nil and "
           "vint_set_property == nil and vint_get_property == nil and "
           "vint_dataresponder_finished == nil and vint_internal_dataresponder_request == nil)");

        // 1. vint_object_first_child (Sec18.1): CONFIRMED - zero Lua values
        // (not even 0.0) on either a bad handle or a resolved object with
        // no first child; exactly one number (the child's handle) when a
        // real first child exists.
        {
            ok(ui, "assert(select('#', vint_object_first_child(999999)) == 0)"); // bad handle
            uint32_t root = es.registerVdoObjectForTesting("root", 0, 0);
            ok(ui, "assert(select('#', vint_object_first_child(" + std::to_string(root) + ")) == 0)"); // no child yet
            uint32_t child = es.registerVdoObjectForTesting("child", root, 0);
            es.setVdoObjectFirstChildForTesting(root, child);
            ok(ui, "assert(vint_object_first_child(" + std::to_string(root) + ") == " + std::to_string(child) + ")");
        }

        // 2. vint_object_clone (Sec18.2): CONFIRMED - always exactly 1
        // number, 0.0 on every failure path (no current document - OPEN;
        // an unresolvable orig_handle); the new clone's real handle on
        // success, scoped to the resolved parent (explicit arg, or the
        // original's own parent as fallback - including a given-but-
        // unresolvable parent handle) and the resolved CURRENT document
        // (not the original's own document).
        {
            refusesOpen(ui, "vint_object_clone(1)", "current default vint document"); // OPEN: no current document yet
            es.currentDefaultDocHandle().set(42);
            ok(ui, "assert(vint_object_clone(999999) == 0.0)"); // bad orig_handle -> 0.0, logged (not Lua-visible), not 0 Lua values

            uint32_t parentA = es.registerVdoObjectForTesting("parentA", 0, 7);
            uint32_t origWithParent = es.registerVdoObjectForTesting("orig_with_parent", parentA, 7);

            // No parent arg -> falls back to the ORIGINAL's own parent
            // (parentA); the clone is scoped to the CURRENT document (42),
            // not the original's own document (7).
            ok(ui, "CLONE1 = vint_object_clone(" + std::to_string(origWithParent) + ")");
            uint32_t clone1 = static_cast<uint32_t>(globalNumber(ui, "CLONE1"));
            CHECK(clone1 != 0 && clone1 != origWithParent);
            const auto* c1obj = es.vdoObjectForTesting(clone1);
            CHECK(c1obj != nullptr);
            if (c1obj) CHECK(c1obj->parentHandle == parentA && c1obj->docHandle == 42);

            // An explicit, resolvable parent arg overrides the fallback.
            uint32_t parentB = es.registerVdoObjectForTesting("parentB", 0, 7);
            ok(ui, "CLONE2 = vint_object_clone(" + std::to_string(origWithParent) + ", " + std::to_string(parentB) + ")");
            uint32_t clone2 = static_cast<uint32_t>(globalNumber(ui, "CLONE2"));
            CHECK(clone2 != 0 && clone2 != clone1);
            const auto* c2obj = es.vdoObjectForTesting(clone2);
            CHECK(c2obj != nullptr);
            if (c2obj) CHECK(c2obj->parentHandle == parentB && c2obj->docHandle == 42);

            // A given-but-unresolvable parent handle ALSO falls back to the
            // original's own parent (CONFIRMED: "falls back ... if that
            // resolution yields nothing").
            ok(ui, "CLONE3 = vint_object_clone(" + std::to_string(origWithParent) + ", 888888)");
            uint32_t clone3 = static_cast<uint32_t>(globalNumber(ui, "CLONE3"));
            const auto* c3obj = es.vdoObjectForTesting(clone3);
            CHECK(c3obj != nullptr);
            if (c3obj) CHECK(c3obj->parentHandle == parentA);
        }

        // 3. vint_get_time_index (Sec19.1): CONFIRMED - absent, nil, OR the
        // literal number 0 all take the SAME current-document fallback
        // path (a genuine difference from every other optional numeric-
        // handle argument elsewhere in this document). This host cannot
        // separately reach the real "resolution failed" zero-push path
        // (see EngineState::vintGetTimeIndex's own doc comment), so every
        // call either returns a real, test-set value or refuses as OPEN.
        {
            refusesOpen(ui, "vint_get_time_index(5)", "vint document time index"); // explicit nonzero doc, never set
            es.vintTimeIndexByDoc().set("5", 12.5);
            ok(ui, "assert(vint_get_time_index(5) == 12.5)");

            // currentDefaultDocHandle_ is 42 (set above, in test 2) - the
            // fallback path, not yet set in vintTimeIndexByDoc_.
            refusesOpen(ui, "vint_get_time_index()", "vint document time index");
            refusesOpen(ui, "vint_get_time_index(0)", "vint document time index"); // explicit 0 -> SAME fallback, not "explicit"
            es.vintTimeIndexByDoc().set("42", 99.0);
            ok(ui, "assert(vint_get_time_index() == 99.0)");
            ok(ui, "assert(vint_get_time_index(0) == 99.0)");
            ok(ui, "assert(vint_get_time_index(5) == 12.5)"); // explicit nonzero stays independent of the fallback
        }

        // 4. vint_dataitem_get (Sec19.2): CONFIRMED - zero Lua values on a
        // bad handle; every populated field of the resolved data item, in
        // order, otherwise; bounded at the real 32-slot cap.
        {
            ok(ui, "assert(select('#', vint_dataitem_get(999999)) == 0)"); // bad handle

            VintTaggedValue n1;
            n1.kind = VintTaggedValue::Kind::Number;
            n1.number = 3.5;
            VintTaggedValue b1;
            b1.kind = VintTaggedValue::Kind::Boolean;
            b1.boolean = true;
            VintTaggedValue s1;
            s1.kind = VintTaggedValue::Kind::String;
            s1.text = "hello";
            uint32_t di = es.registerVintDataItemForTesting({n1, b1, s1});
            ok(ui,
               "local a, b, c = vint_dataitem_get(" + std::to_string(di) + ")\n"
               "assert(a == 3.5 and b == true and c == 'hello')");

            std::vector<VintTaggedValue> many;
            for (int i = 0; i < 40; ++i) {
                VintTaggedValue v;
                v.kind = VintTaggedValue::Kind::Number;
                v.number = i;
                many.push_back(v);
            }
            uint32_t di2 = es.registerVintDataItemForTesting(many); // CONFIRMED cap: truncated to 32
            ok(ui, "assert(select('#', vint_dataitem_get(" + std::to_string(di2) + ")) == 32)");
        }

        // 5./6. vint_set_property / vint_get_property (Sec20): CONFIRMED -
        // vint_set_property always returns zero Lua values, every path
        // (including a missing/non-string property_name, and an
        // unresolvable handle - both silent no-ops); vint_get_property
        // returns zero Lua values (not even nil) on a bad handle or a
        // property-name miss, else echoes back exactly what was last set
        // (the real per-type dispatch/tween-redirect/callback-claim
        // mechanisms are NOT modeled - see EngineState::setVintProperty's
        // own doc comment).
        {
            uint32_t obj = es.registerVdoObjectForTesting("widget", 0, 0);
            ok(ui, "assert(select('#', vint_get_property(999999, 'scale')) == 0)"); // bad handle
            ok(ui, "assert(select('#', vint_get_property(" + std::to_string(obj) + ", 'scale')) == 0)"); // never set

            ok(ui, "assert(select('#', vint_set_property(" + std::to_string(obj) + ", 'is_paused', false)) == 0)");
            ok(ui, "local a = vint_get_property(" + std::to_string(obj) + ", 'is_paused'); assert(a == false)");

            // Multi-value (vec2-shaped) round-trip.
            ok(ui, "vint_set_property(" + std::to_string(obj) + ", 'scale', 1.5, 2.5)");
            ok(ui, "local x, y = vint_get_property(" + std::to_string(obj) + ", 'scale'); assert(x == 1.5 and y == 2.5)");

            // CONFIRMED: missing/non-string property_name -> silent no-op.
            ok(ui, "assert(select('#', vint_set_property(" + std::to_string(obj) + ")) == 0)"); // no property_name at all
            ok(ui, "vint_set_property(" + std::to_string(obj) + ", 123, 'x')"); // non-string name -> no-op
            ok(ui, "assert(select('#', vint_get_property(" + std::to_string(obj) + ", 123)) == 0)"); // confirms nothing stored

            // An unresolvable handle silently no-ops (CONFIRMED: "an
            // unresolvable handle simply fails to resolve later").
            ok(ui, "assert(select('#', vint_set_property(999999, 'scale', 1, 2)) == 0)");
            ok(ui, "assert(select('#', vint_get_property(999999, 'scale')) == 0)");

            // Case-insensitive property-name hash match (CONFIRMED, Sec9.2:
            // the engine's own lower-cased string hash).
            ok(ui, "vint_set_property(" + std::to_string(obj) + ", 'Visible', true)");
            ok(ui, "assert(vint_get_property(" + std::to_string(obj) + ", 'VISIBLE') == true)");
        }

        // 7./8. vint_dataresponder_finished / vint_internal_dataresponder_
        // request (Sec21): CONFIRMED - no record at all -> "finished"
        // (true), the real, honestly-flagged quirk; the request native
        // never creates a record and always returns zero Lua values, even
        // for a name with no record (a complete, silent no-op) or wrong
        // argument types.
        {
            ok(ui, "assert(vint_dataresponder_finished('never_registered') == true)");
            es.registerDataResponderForTesting("my_responder", false);
            ok(ui, "assert(vint_dataresponder_finished('my_responder') == false)");

            ok(ui, "assert(select('#', vint_internal_dataresponder_request('never_registered', 'cb', 10)) == 0)");
            ok(ui, "assert(vint_dataresponder_finished('never_registered') == true)"); // still no record - unaffected
            CHECK(es.dataResponderDispatchAttempts("never_registered") == 0);

            ok(ui, "vint_internal_dataresponder_request('my_responder', 'cb', 10)");
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);
            ok(ui, "assert(vint_dataresponder_finished('my_responder') == false)"); // this native never flips it (CONFIRMED)

            // CONFIRMED: wrong/missing arg type -> silent no-op, no dispatch attempt.
            ok(ui, "vint_internal_dataresponder_request('my_responder', 123, 10)");    // callback_name not a string
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);
            ok(ui, "vint_internal_dataresponder_request('my_responder', 'cb', 'ten')"); // max_records not a number
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);
            ok(ui, "vint_internal_dataresponder_request('my_responder')");              // missing both
            CHECK(es.dataResponderDispatchAttempts("my_responder") == 1);

            es.registerDataResponderForTesting("my_responder", true);
            ok(ui, "assert(vint_dataresponder_finished('my_responder') == true)");
        }

        // Every one of the 8 calls above must fold into the SAME HitLog
        // ranking every other stub/hook in this project uses.
        for (const char* name : {"vint_object_first_child", "vint_object_clone", "vint_get_time_index",
                                  "vint_dataitem_get", "vint_set_property", "vint_get_property",
                                  "vint_dataresponder_finished", "vint_internal_dataresponder_request"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

    // --- Batch 2026-10-02: spec-lua-api-behaviour.md Sec33 ("ranking
    // tranche 05", 25 names) + the Sec14.1 0x0095da50 correction. Same
    // "exercised through the real Lua-visible global" convention as every
    // other spec-confirmed test above.
    {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        sr3luahost::EngineState& es = host.engineState();
        auto refusesOpen = [&](lua_State* st, const std::string& chunk, const char* needle) {
            auto r = host.runChunk(st, chunk, "sec33_open.lua");
            CHECK(r.loadOk && !r.pcallOk);
            CHECK(r.pcallError.find("is OPEN") != std::string::npos);
            CHECK(r.pcallError.find(needle) != std::string::npos);
        };
        auto ok = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "sec33.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };

        // All 25 are `gameplay`-tagged - nil in ui.
        {
            auto r = host.runChunk(host.uiState(),
                "assert(vehicle_is_helicopter == nil and ai_clear_priority_target == nil and "
                "action_play_synced_do == nil and waiting_for_player_dialog == nil)",
                "sec33_ui_nil.lua");
            CHECK(r.loadOk && r.pcallOk);
        }

        // vehicle_is_helicopter (Sec33.1): class 3 -> true, 4 (VTOL) -> false, unresolved -> false.
        {
            refusesOpen(gp, "vehicle_is_helicopter('heli1')", "named-object resolution");
            es.objectResolves().set("heli1", true);
            refusesOpen(gp, "vehicle_is_helicopter('heli1')", "flying-type enum");
            es.getOrCreateVehicle("heli1").flyingType.set(3);
            ok(gp, "assert(vehicle_is_helicopter('heli1') == true)");
            es.getOrCreateVehicle("heli1").flyingType.set(4); // VTOL answers false here
            ok(gp, "assert(vehicle_is_helicopter('heli1') == false)");
            es.objectResolves().set("not_a_vehicle", false);
            ok(gp, "assert(vehicle_is_helicopter('not_a_vehicle') == false)");
        }

        // vehicle_is_vtol_hover / vehicle_is_vtol_jet (Sec33.1): the
        // CONFIRMED stack-discipline defect - a true result pushes 2
        // values (true, false); every other path pushes exactly 1 (false).
        {
            es.objectResolves().set("vtol1", true);
            auto& v = es.getOrCreateVehicle("vtol1");
            v.flyingType.set(4);
            v.vtolState.set(0); // hover
            ok(gp, "local a, b = vehicle_is_vtol_hover('vtol1'); assert(a == true and b == false)");
            ok(gp, "assert(select('#', vehicle_is_vtol_hover('vtol1')) == 2)");
            ok(gp, "local a, b = vehicle_is_vtol_jet('vtol1'); assert(a == false and b == nil and select('#', vehicle_is_vtol_jet('vtol1')) == 1)");
            v.vtolState.set(2); // jet
            ok(gp, "local a, b = vehicle_is_vtol_jet('vtol1'); assert(a == true and b == false)");
            v.flyingType.set(3); // not a VTOL at all -> single false either way
            ok(gp, "assert(select('#', vehicle_is_vtol_hover('vtol1')) == 1 and vehicle_is_vtol_hover('vtol1') == false)");
        }

        // vehicle_is_ready (Sec33.1): all three gates.
        {
            es.objectResolves().set("car1", true);
            auto& v = es.getOrCreateVehicle("car1");
            refusesOpen(gp, "vehicle_is_ready('car1')", "not-ready flag (+0x3b");
            v.notReadyBit3b.set(false);
            v.notReadyBit3a.set(false);
            v.fullySetUp.set(true);
            ok(gp, "assert(vehicle_is_ready('car1') == true)");
            v.notReadyBit3b.set(true);
            ok(gp, "assert(vehicle_is_ready('car1') == false)");
            v.notReadyBit3b.set(false);
            v.fullySetUp.set(false);
            ok(gp, "assert(vehicle_is_ready('car1') == false)");
        }

        // vehicle_never_flatten_tires / vehicle_set_weapons_disarmed /
        // vehicle_set_no_chase (Sec33.1): double-gate bit setters, always
        // applied locally (this project's own convention).
        {
            ok(gp, "vehicle_never_flatten_tires('tank1')"); // default true
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7b.get(0x1) == 0x1);
            ok(gp, "vehicle_never_flatten_tires('tank1', false)");
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7b.get(0x1) == 0x0);

            ok(gp, "vehicle_set_weapons_disarmed('tank1', true)");
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7e.get(0x4) == 0x4);
            ok(gp, "vehicle_set_weapons_disarmed('tank1')"); // absent -> false
            CHECK(es.getOrCreateVehicle("tank1").forceFlags1d7e.get(0x4) == 0x0);

            ok(gp, "vehicle_set_no_chase('tank1', true)");
            CHECK(es.getOrCreateVehicle("tank1").vehicleAiForceFlags.get(0x1) == 0x1);
        }

        // vehicle_set_kneecappers (Sec33.1): deferred-vs-enabled per the
        // "fully set up" gate.
        {
            auto& v = es.getOrCreateVehicle("kneecar1");
            v.fullySetUp.set(false);
            ok(gp, "vehicle_set_kneecappers('kneecar1')"); // default true, not set up -> deferred only
            CHECK(v.kneecappersDeferred == true && v.kneecappersEnabled == false);
            v.fullySetUp.set(true);
            ok(gp, "vehicle_set_kneecappers('kneecar1')");
            CHECK(v.kneecappersEnabled == true);
            ok(gp, "vehicle_set_kneecappers('kneecar1', false)");
            CHECK(v.kneecappersEnabled == false && v.kneecappersDeferred == false);
        }

        // vehicle_set_sirenlights (Sec33.1): class 3 -> both bits; class 12
        // -> siren only; an unrecognized class -> OPEN (no per-class table).
        {
            es.objectResolves().set("cruiser1", true);
            auto& v = es.getOrCreateVehicle("cruiser1");
            refusesOpen(gp, "vehicle_set_sirenlights('cruiser1', true)", "body-active flag");
            v.bodyActive.set(true);
            refusesOpen(gp, "vehicle_set_sirenlights('cruiser1', true)", "class enum");
            v.vehicleClass.set(3);
            ok(gp, "vehicle_set_sirenlights('cruiser1', true)");
            CHECK(v.forceFlags1d7c.get(0x18) == 0x18);
            ok(gp, "vehicle_set_sirenlights('cruiser1', false)");
            CHECK(v.forceFlags1d7c.get(0x18) == 0x0);
            v.vehicleClass.set(12);
            ok(gp, "vehicle_set_sirenlights('cruiser1', true)");
            CHECK(v.forceFlags1d7c.get(0x8) == 0x8);
            v.vehicleClass.set(99);
            refusesOpen(gp, "vehicle_set_sirenlights('cruiser1', true)", "per-class siren/headlight allow flag");
        }

        // vehicle_set_ambient (Sec33.1): mode/sub-mode and the unconverted speed cap.
        {
            ok(gp, "vehicle_set_ambient('ambient1')"); // default -1.0 -> no speed cap
            auto& v = es.getOrCreateVehicle("ambient1");
            CHECK(v.aiMode == 6 && v.aiSubMode == 0x11 && v.ambientFlags.get(0x1) == 0x1);
            CHECK(v.hasSpeedCap == false);
            ok(gp, "vehicle_set_ambient('ambient1', 2500.0)");
            CHECK(v.hasSpeedCap == true && v.speedCapRaw == 1000.0); // clamped, UNCONVERTED (no mph factor)
            ok(gp, "vehicle_set_ambient('ambient1', 30.0)");
            CHECK(v.speedCapRaw == 30.0);
        }

        // vehicle_spotlight_is_target_spotted (Sec33.1): side effect
        // happens, actual geometry test is OPEN.
        {
            es.objectResolves().set("spotcar1", true);
            es.objectResolves().set("spottarget1", true);
            refusesOpen(gp, "vehicle_spotlight_is_target_spotted('spotcar1', 'spottarget1')", "raycast test");
            CHECK(es.getOrCreateVehicle("spotcar1").spotlightTargetName == "spottarget1");
            es.objectResolves().set("spottarget2", false);
            ok(gp, "assert(vehicle_spotlight_is_target_spotted('spotcar1', 'spottarget2') == false)");
        }

        // ai_clear_priority_target (Sec33.2): single-gate null-clear.
        {
            es.objectResolves().set("npc_rush", true);
            es.getOrCreateCharacter("npc_rush").forcedTargetHandle = "some_prior_target";
            ok(gp, "ai_clear_priority_target('npc_rush')");
            CHECK(es.getOrCreateCharacter("npc_rush").forcedTargetHandle.empty());
        }

        // ai_set_in_scripted_cover (Sec33.2): false only if unresolved; disabling queues action 13.
        {
            es.objectResolves().set("cover_unresolved", false);
            ok(gp, "assert(ai_set_in_scripted_cover('cover_unresolved', true) == false)");
            es.objectResolves().set("npc_cover", true);
            ok(gp, "assert(ai_set_in_scripted_cover('npc_cover', true) == true)");
            CHECK(es.getOrCreateCharacter("npc_cover").inScriptedCover.get() == true);
            ok(gp, "assert(ai_set_in_scripted_cover('npc_cover', false) == true)");
            CHECK(es.getOrCreateCharacter("npc_cover").inScriptedCover.get() == false);
            CHECK(es.getOrCreateCharacter("npc_cover").scriptedAction.get() == 13);
        }

        // ai_pay_attention_to_position (Sec33.2): mode 7/3 mapping.
        {
            es.objectResolves().set("npc_attend", true);
            es.objectResolves().set("attend_obj", true);
            ok(gp, "ai_pay_attention_to_position('npc_attend', 'attend_obj')"); // default true -> mode 7
            auto& c = es.getOrCreateCharacter("npc_attend");
            CHECK(c.attentionMode == 7 && c.attentionSourceObjectName == "attend_obj");
            ok(gp, "ai_pay_attention_to_position('npc_attend', 'attend_obj', false)");
            CHECK(c.attentionMode == 3);
        }

        // ai_do_scripted_rush (Sec33.2): with-target always succeeds;
        // without a target, OPEN unless already rushing (action 22).
        {
            es.objectResolves().set("npc_rush2", true);
            es.objectResolves().set("rush_target", true);
            ok(gp, "assert(ai_do_scripted_rush('npc_rush2', 'rush_target') == true)");
            CHECK(es.getOrCreateCharacter("npc_rush2").scriptedAction.get() == 22);
            CHECK(es.getOrCreateCharacter("npc_rush2").scriptedRushTargetName == "rush_target");

            es.objectResolves().set("npc_rush3", true);
            refusesOpen(gp, "ai_do_scripted_rush('npc_rush3')", "reachability/distance/melee checks");
            es.getOrCreateCharacter("npc_rush3").scriptedAction.set(22); // already rushing -> accepted immediately
            ok(gp, "assert(ai_do_scripted_rush('npc_rush3') == true)");
        }

        // action_play_synced_do (Sec33.3) + the Sec14.1 0x0095da50
        // correction: a dead actor 1 -> -1; else 1, regardless of whether
        // the synced-action name resolves to a known index (this project's
        // never-populated table honestly answers "not found" either way).
        {
            es.objectResolves().set("performer1", true);
            es.objectResolves().set("partner1", true);
            ok(gp, "assert(action_play_synced_do('performer1', 'partner1', 'wave_action') == 1)");
            CHECK(es.lookupSyncedActionIndex("wave_action") == EngineState::kSyncedActionNotFound);
            es.registerSyncedActionForTesting("wave_action", 7);
            CHECK(es.lookupSyncedActionIndex("wave_action") == 7); // found path also plays successfully (not cross-referenced further, see doc comment)
            ok(gp, "assert(action_play_synced_do('performer1', 'partner1', 'wave_action') == 1)");
            es.getOrCreateCharacter("performer1").isDeadHighConfidence = true;
            ok(gp, "assert(action_play_synced_do('performer1', 'partner1', 'wave_action') == -1)");
        }

        // action_play_directional_stumble_do (Sec33.3): push-count
        // semantics - clean success, dead character (still plays), and the
        // "would crash" unresolved paths treated as safe failures instead.
        {
            es.objectResolves().set("stumbler1", true);
            es.objectResolves().set("stumble_ref1", true);
            ok(gp, "assert(select('#', action_play_directional_stumble_do('stumbler1', 'stumble_ref1')) == 1)");
            ok(gp, "assert(action_play_directional_stumble_do('stumbler1', 'stumble_ref1') == 1.0)");

            es.getOrCreateCharacter("stumbler1").isDeadHighConfidence = true;
            ok(gp, "local a, b = action_play_directional_stumble_do('stumbler1', 'stumble_ref1'); "
                   "assert(a == -1.0 and b == 1.0)");

            // Fresh, non-dead character name here - stumbler1 above was set
            // dead and stays dead, which would add its own extra -1.0 push
            // and change the expected values below.
            es.objectResolves().set("stumbler_fresh", true);
            es.objectResolves().set("stumble_unresolved", false);
            ok(gp, "local a, b = action_play_directional_stumble_do('stumbler_fresh', 'stumble_unresolved'); "
                   "assert(a == -1.0 and b == 1.0)"); // object unresolved -> one -1.0, then the unconditional final 1.0

            es.objectResolves().set("stumbler_unresolved", false);
            ok(gp, "assert(select('#', action_play_directional_stumble_do('stumbler_unresolved', 'stumble_ref1')) == 2)");
        }

        // action_sequence_end (Sec33.3): global teardown, host-only broadcast.
        {
            auto& seq = es.actionSequence();
            seq.active = true;
            seq.localScriptedCameraTarget = "cam1";
            int before = seq.hostBroadcastCount;
            ok(gp, "action_sequence_end()");
            CHECK(seq.active == false && seq.localScriptedCameraTarget.empty());
            CHECK(seq.trackedHandleReleaseCount == 2);
            CHECK(seq.hostBroadcastCount == before); // single player: coopLocalIsHost() == false (no session)
        }

        // audio_set_listener_override / audio_clear_listener_override (Sec33.4).
        {
            es.objectResolves().set("listener_obj", true);
            ok(gp, "audio_set_listener_override('listener_obj')");
            CHECK(es.audioListenerOverrideTarget() == "listener_obj");
            ok(gp, "audio_clear_listener_override()");
            CHECK(es.audioListenerOverrideTarget().empty());
        }

        // audio_play_for_navpoint (Sec33.4): id on resolve, 0 on failure, arg 3 inert.
        {
            es.objectResolves().set("navpoint_obj", true);
            ok(gp, "NAV_ID = audio_play_for_navpoint('some_sound', 'navpoint_obj', 'ignored_arg')");
            double navId = 0.0;
            { lua_getglobal(gp, "NAV_ID"); navId = lua_tonumber(gp, -1); lua_pop(gp, 1); }
            CHECK(navId > 0.0);
            es.objectResolves().set("navpoint_unresolved", false);
            ok(gp, "assert(audio_play_for_navpoint('some_sound', 'navpoint_unresolved') == 0)");
        }

        // audio_any_conversation_playing (Sec33.4): settles the "mission_conv" identity question.
        {
            refusesOpen(gp, "audio_any_conversation_playing()", "mission_conv");
            es.conversationChannelActive().set("mission_conv", true);
            ok(gp, "assert(audio_any_conversation_playing() == true)");
            es.conversationChannelActive().set("mission_conv", false);
            ok(gp, "assert(audio_any_conversation_playing() == false)");
        }

        // boss_battle_matt_begin (Sec33.5): full reset + deadline arm +
        // the "Matt" character bit, gated by resolve-before-write.
        {
            auto& matt = es.bossBattleMatt();
            matt.cheatSlot.set(42);
            matt.lastId = 99;
            matt.active = false;
            refusesOpen(gp, "boss_battle_matt_begin()", "named-object resolution");
            CHECK(matt.cheatSlot.get() == 42); // refused BEFORE any write - no partial update
            es.objectResolves().set("Matt", true);
            ok(gp, "boss_battle_matt_begin()"); // default true -> 5000ms deadline
            CHECK(matt.cheatSlot.get() == -1 && matt.lastId == -1 && matt.active == true);
            CHECK(matt.deadlineMs == 5000);
            CHECK(es.getOrCreateCharacter("Matt").flagsEc.get(0x4000) == 0x4000);
            ok(gp, "boss_battle_matt_begin(false)");
            CHECK(matt.deadlineMs == 0);
        }

        // auto_pickup_disable (Sec33.5): plain session-wide off switch.
        {
            CHECK(es.allowWeaponAutoPickup() == true);
            ok(gp, "auto_pickup_disable()");
            CHECK(es.allowWeaponAutoPickup() == false);
        }

        // waiting_for_player_dialog (Sec33.5): reference-counted show/hide,
        // and the deliberately-reproduced "currently shown never clears" hazard.
        {
            auto& d = es.waitingForPlayerDialog();
            ok(gp, "waiting_for_player_dialog(false)"); // hide at 0 -> no-op
            CHECK(d.refCount == 0);
            ok(gp, "waiting_for_player_dialog(true)");
            CHECK(d.refCount == 1 && d.currentlyShown == true);
            ok(gp, "waiting_for_player_dialog(true)"); // nested show
            CHECK(d.refCount == 2);
            ok(gp, "waiting_for_player_dialog(false)");
            CHECK(d.refCount == 1 && d.currentlyShown == true); // not the final hide yet
            ok(gp, "waiting_for_player_dialog(false)"); // final hide
            CHECK(d.refCount == 0);
            CHECK(d.currentlyShown == true); // CONFIRMED hazard: never cleared by this path
            // A second show cycle: display branch is silently skipped (currentlyShown already true).
            int showsBefore = d.showBroadcastCount;
            ok(gp, "waiting_for_player_dialog(true)");
            CHECK(d.showBroadcastCount == showsBefore); // no new display attempt
            ok(gp, "waiting_for_player_dialog(false)");
        }

        // Every one of the 25 calls above must fold into the SAME HitLog
        // ranking every other stub/hook in this project uses.
        for (const char* name :
             {"vehicle_is_helicopter", "vehicle_is_vtol_hover", "vehicle_is_vtol_jet", "vehicle_is_ready",
              "vehicle_never_flatten_tires", "vehicle_set_weapons_disarmed", "vehicle_set_no_chase",
              "vehicle_set_kneecappers", "vehicle_set_sirenlights", "vehicle_set_ambient",
              "vehicle_spotlight_is_target_spotted", "ai_clear_priority_target", "ai_set_in_scripted_cover",
              "ai_pay_attention_to_position", "ai_do_scripted_rush", "action_play_synced_do",
              "action_play_directional_stumble_do", "action_sequence_end", "audio_set_listener_override",
              "audio_clear_listener_override", "audio_play_for_navpoint", "audio_any_conversation_playing",
              "boss_battle_matt_begin", "auto_pickup_disable", "waiting_for_player_dialog"}) {
            CHECK(host.hitLog().hits().count(name) == 1);
            CHECK(host.hitLog().hits().at(name).callCount >= 1);
        }
    }

    // --- Batch 2026-10-02: spec-lua-api-behaviour.md Sec35 (the
    // `teleport_coop` investigation) - teleport_check_done/
    // turn_to_check_done/move_to_check_done/vehicle_pathfind_check_done.
    // A REGRESSION GUARD: Sec15.17's original desk reading had this exact
    // done/pending polarity BACKWARDS (an earlier draft read "true
    // whenever any teleport is tracked"; the corrected, now-CONFIRMED
    // convention, re-derived from the raw instruction stream, is the
    // opposite emphasis - true for DONE or NO REQUEST, false only while
    // GENUINELY PENDING) - this block pins the corrected polarity
    // directly so that mistake can never silently return.
    {
        Host host(specConfirmedFixtureNames());
        lua_State* gp = host.gameplayState();
        sr3luahost::EngineState& es = host.engineState();
        auto ok = [&](lua_State* st, const std::string& chunk) {
            auto r = host.runChunk(st, chunk, "teleport_coop.lua");
            CHECK(r.loadOk && r.pcallOk);
            if (!r.pcallOk) std::cerr << chunk << ": " << r.pcallError << "\n";
        };

        // All 4 are `gameplay`-tagged (CONFIRMED, tools/lua_all_registered_
        // 1490_tagged.txt) - nil in the UI state.
        ok(host.uiState(),
           "assert(teleport_check_done == nil and turn_to_check_done == nil and "
           "move_to_check_done == nil and vehicle_pathfind_check_done == nil)");

        // 1./2. teleport_check_done (Sec15.17) / turn_to_check_done: no
        // request tracked for this name at all -> true (CONFIRMED, Sec35.2:
        // "done OR no request" -> true) - this is also the REAL state
        // every genuine call reaches in this host (no in-scope native
        // allocates a request), and is exactly the fix that unsticks
        // `teleport_coop`'s own `repeat thread_yield() until
        // teleport_check_done(LOCAL_PLAYER)` poll (Sec35's own "why" note).
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == true)");
        ok(gp, "assert(turn_to_check_done('#PLAYER1#') == true)");

        // A request registered PENDING -> false (the corrected polarity's
        // other half - must NOT be true while genuinely pending).
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindTeleport, false);
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == false)");
        // A DIFFERENT name is unaffected (the name argument is genuinely
        // used as a per-request lookup key, not ignored).
        ok(gp, "assert(teleport_check_done('#PLAYER2#') == true)");
        // Once marked done, a read both answers true AND releases the
        // record (CONFIRMED, Sec35.2: a "done" read consumes it) - a
        // second read with no new request sees "no request" -> true, same
        // answer, for a different confirmed reason.
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindTeleport, true);
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == true)");
        CHECK(es.scriptedRequestStatusCode("#PLAYER1#", EngineState::kScriptedRequestKindTeleport) == 2); // released already

        // turn_to_check_done consults a SEPARATE kind (0, not 4) - a
        // teleport-kind pending record for the same name must not leak
        // into the turn-to query.
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindTeleport, false);
        ok(gp, "assert(turn_to_check_done('#PLAYER1#') == true)"); // kind 0, no record -> true
        ok(gp, "assert(teleport_check_done('#PLAYER1#') == false)"); // kind 4, still pending

        // 3. move_to_check_done (Sec22.15): arg 2 (not arg 1) is the
        // character name this host looks up; same corrected polarity,
        // kind 2 (move-to/pathfind).
        ok(gp, "assert(move_to_check_done(1, '#PLAYER1#', 'anchor', 1, false, false, false, 0) == true)");
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindMoveOrPathfind, false);
        ok(gp, "assert(move_to_check_done(1, '#PLAYER1#', 'anchor', 1, false, false, false, 0) == false)");
        es.registerScriptedRequestForTesting("#PLAYER1#", EngineState::kScriptedRequestKindMoveOrPathfind, true);
        ok(gp, "assert(move_to_check_done(1, '#PLAYER1#', 'anchor', 1, false, false, false, 0) == true)");

        // 4. vehicle_pathfind_check_done (Sec9.10): pushes a NUMBER, not a
        // boolean - 2 (not a boolean "true") when this host's own vehicle-
        // resolution gate is closed, CONFIRMED to be the SAME literal
        // fallback the real engine uses when the vehicle fails to resolve.
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 2)");
        // Opening the gate exposes the SAME shared-pool 0/1/2 polarity
        // teleport_check_done's own boolean is built on, kind 2 (shared
        // with move_to_check_done, Sec22.15's own "a THIRD confirmed use
        // of kind 2" text) - a DIFFERENT name from move_to_check_done's own
        // kind-2 record above, so the two cannot collide.
        es.setVehiclePathfindResolvableForTesting("some_vehicle", true);
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 2)"); // gate open, still no record -> 2 (no request)
        es.registerScriptedRequestForTesting("some_vehicle", EngineState::kScriptedRequestKindMoveOrPathfind, false);
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 0)"); // pending
        es.registerScriptedRequestForTesting("some_vehicle", EngineState::kScriptedRequestKindMoveOrPathfind, true);
        ok(gp, "assert(vehicle_pathfind_check_done('some_vehicle') == 1)"); // done

        for (const char* name :
             {"teleport_check_done", "turn_to_check_done", "move_to_check_done", "vehicle_pathfind_check_done"}) {
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
