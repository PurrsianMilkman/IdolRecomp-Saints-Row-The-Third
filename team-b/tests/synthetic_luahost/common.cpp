// Shared helpers/fixtures for the synthetic sr3luahost test suite (see common.h).
#include "synthetic_luahost/common.h"

int g_failures = 0;

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
