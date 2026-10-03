// Synthetic tests for the 50-function batch (2026-10-01):
// spec-lua-api-behaviour.md Sec27 (25 UI-cluster functions, "ranks
// 551-650" tranche part D) and Sec28 (25 gameplay-cluster functions,
// part C) - see include/sr3luahost/engine_state.h and
// src/lua_spec_confirmed_stubs.cpp for the full per-function reasoning.
// Same convention as tests/synthetic_luahost_bare_globals_test.cpp: a
// small, hand-written fixture (no real tagged-registration-list file or
// real archive read here - that is exercised for real by
// tools/lua_host_run.cpp). Every name below is registered with its own
// real cluster tag, grepped directly from
// tools/lua_all_registered_1490_tagged.txt (all 25 Sec27 names are `ui`,
// all 25 Sec28 names are `gameplay`), matching how Host (host.h) really
// splits the tagged list.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include "sr3save/save_crc.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

// Runs `code` and expects it to succeed (asserts inside the chunk do the work).
void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "batch2728_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

// Runs `code` and expects a Lua error whose message contains `needle`.
void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "batch2728_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

bool hasGlobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

// The 50 (name, cluster) rows this batch registers - real tags grepped
// directly from tools/lua_all_registered_1490_tagged.txt.
std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"cat_mouse_results_select", "ui"},
        {"cell_is_mission_complete", "ui"},
        {"Completion_should_wait_for_coop", "ui"},
        {"Completion_user_is_done_viewing", "ui"},
        {"vcust_set_camera_pos", "ui"},
        {"pause_menu_has_seen_display_cal_screen", "ui"},
        {"msn_text_adventure_set_screen", "ui"},
        {"horde_results_set_end_action", "ui"},
        {"garage_preview_vehicle", "ui"},
        {"game_lobby_coop_finished", "ui"},
        {"dialog_box_force_close", "ui"},
        {"game_autosave", "ui"},
        {"game_send_pause_menu_player_invite", "ui"},
        {"game_can_send_player_invite", "ui"},
        {"game_set_coop_friendly_fire", "ui"},
        {"game_get_coop_friendly_fire", "ui"},
        {"game_send_party_invites", "ui"},
        {"game_is_connected_to_network", "ui"},
        {"game_is_signed_in", "ui"},
        {"game_sign_into_network", "ui"},
        {"game_show_coop_gamercard", "ui"},
        {"game_main_menu_join_friend_in_progress", "ui"},
        {"game_coop_start_new_live", "ui"},
        {"game_coop_start_new_syslink", "ui"},
        {"game_get_in_progress_type", "ui"},
        {"helicopter_set_dont_death_spiral", "gameplay"},
        {"helicopter_fly_to_set_goal_direction", "gameplay"},
        {"hdr_bloom_set_multiplier", "gameplay"},
        {"guardian_angel_enable_indicators", "gameplay"},
        {"group_get_next_npc", "gameplay"},
        {"group_get_first_npc", "gameplay"},
        {"get_num_humans_in_trigger", "gameplay"},
        {"get_char_vehicle_is_in_air", "gameplay"},
        {"effect_play_finisher", "gameplay"},
        {"dlc3_m03_set_sprint_waning", "gameplay"},
        {"debris_flow_recycle_object", "gameplay"},
        {"customization_restore_player_rig", "gameplay"},
        {"crib_weapon_add_enable", "gameplay"},
        {"crib_weapon_add_disable", "gameplay"},
        {"crib_unlock_strongold", "gameplay"},
        {"continuous_explosion_start", "gameplay"},
        {"clear_callbacks_for_obj", "gameplay"},
        {"city_zone_swap_is_active", "gameplay"},
        {"character_set_counter_on_grabbed", "gameplay"},
        {"character_remove_child_item_by_name", "gameplay"},
        {"character_get_gender", "gameplay"},
        {"character_evacuate_from_all_vehicles", "gameplay"},
        {"cellphone_animate_start_do", "gameplay"},
        {"boss_battle_matt_get_cheat", "gameplay"},
        {"boss_battle_matt_cheats_start", "gameplay"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 50 names land as real functions in their own
    // real cluster's state, and nowhere else.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.uiState(), "cat_mouse_results_select"));
        CHECK(!hasGlobal(host.gameplayState(), "cat_mouse_results_select"));
        CHECK(hasGlobal(host.gameplayState(), "helicopter_set_dont_death_spiral"));
        CHECK(!hasGlobal(host.uiState(), "helicopter_set_dont_death_spiral"));
        for (const auto& rn : batchFixtureNames()) {
            lua_State* want = rn.cluster == "ui" ? host.uiState() : host.gameplayState();
            CHECK(hasGlobal(want, rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* ui = host.uiState();
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ------------------------------------------------------------------
    // Sec27.1 cat_mouse_results_select: OPEN until the minigame singleton
    // is known; present+ic1 determine the +0xe0 remap (not Lua-observable,
    // checked directly via the C++ accessor).
    fails(host, ui, "cat_mouse_results_select(0)", "0x014c2d10");
    es.catMouseMinigame().present.set(true);
    es.catMouseMinigame().fieldIc1IsOne.set(true);
    ok(host, ui, "cat_mouse_results_select(0)");
    CHECK(es.catMouseMinigame().selection == 1); // ic1 == true -> 1
    CHECK(es.catMouseMinigame().sessionMessageCount == 1);
    ok(host, ui, "cat_mouse_results_select(1)");
    CHECK(es.catMouseMinigame().selection == 2);
    ok(host, ui, "cat_mouse_results_select(2)"); // unrecognised index: unchanged
    CHECK(es.catMouseMinigame().selection == 2);

    // ------------------------------------------------------------------
    // Sec27.2 cell_is_mission_complete: empty name -> false (no OPEN read
    // at all); a real name needs resolution then the completion bit.
    ok(host, ui, "assert(cell_is_mission_complete('') == false)");
    fails(host, ui, "cell_is_mission_complete('mission01')", "named-object resolution");
    es.objectResolves().set("mission01", true);
    fails(host, ui, "cell_is_mission_complete('mission01')", "+0x88");
    es.missionComplete().set("mission01", true);
    ok(host, ui, "assert(cell_is_mission_complete('mission01') == true)");
    es.objectResolves().set("mission02", false);
    ok(host, ui, "assert(cell_is_mission_complete('mission02') == false)");

    // ------------------------------------------------------------------
    // Sec27.3/27.4 Completion_should_wait_for_coop / _user_is_done_viewing:
    // whether the "completion_screen" context resolves at all is itself
    // OPEN (same honest-refusal convention as objectResolves() elsewhere
    // in this file) - it must be set explicitly, even to represent the
    // "resolve fails" default path. Absent context -> true (default);
    // present context exercises both internal-flag paths and the final
    // negation.
    fails(host, ui, "Completion_should_wait_for_coop()", "'completion_screen' UI context resolved");
    fails(host, ui, "Completion_user_is_done_viewing()", "'completion_screen' UI context resolved");
    es.completionScreen().present.set(false); // resolve fails: default stands
    ok(host, ui, "assert(Completion_should_wait_for_coop() == false)"); // default internal flag true -> pushed false
    ok(host, ui, "Completion_user_is_done_viewing()"); // absent context: real no-op, no OPEN read needed
    CHECK(es.completionScreen().lastStateWritten == -1); // unchanged
    es.completionScreen().present.set(true);
    es.completionScreen().flagClusterSet.set(false);
    es.completionScreen().derefByteAtLeastOne.set(false);
    ok(host, ui, "assert(Completion_should_wait_for_coop() == true)"); // internal false -> pushed true
    es.completionScreen().recordEnabled.set(true);
    ok(host, ui, "Completion_user_is_done_viewing()");
    CHECK(es.completionScreen().lastStateWritten == 1);
    CHECK(es.completionScreen().messageSentCount == 1);

    // ------------------------------------------------------------------
    // Sec27.5 vcust_set_camera_pos: gated OPEN chain; on success hashes
    // the preset name (real CRC-32) and tracks the "Standard" comparison.
    fails(host, ui, "vcust_set_camera_pos('Combat')", "0x00812e40");
    es.vcustCamera().targetPresent.set(true);
    es.vcustCamera().targetValid.set(true);
    es.vcustCamera().targetAlive.set(true);
    ok(host, ui, "vcust_set_camera_pos('standard')"); // case-insensitive match
    CHECK(es.vcustCamera().isStandardActive == true);
    CHECK(es.vcustCamera().lastAppliedPresetHash == sr3save::nameHash("standard"));
    ok(host, ui, "vcust_set_camera_pos('Combat')");
    CHECK(es.vcustCamera().isStandardActive == false);

    // ------------------------------------------------------------------
    // Sec27.6/27.7/27.8: trivial globals, CONFIRMED initial values.
    ok(host, ui, "assert(pause_menu_has_seen_display_cal_screen() == false)");
    CHECK(es.textAdventureScreenIndex() == -1);
    ok(host, ui, "msn_text_adventure_set_screen(5.9)");
    CHECK(es.textAdventureScreenIndex() == 5); // truncation toward zero
    CHECK(es.hordeResultsEndAction() == 2);
    ok(host, ui, "horde_results_set_end_action(7)");
    CHECK(es.hordeResultsEndAction() == 7);

    // ------------------------------------------------------------------
    // Sec27.9 garage_preview_vehicle: OPEN table/cache, refresh only on change.
    fails(host, ui, "garage_preview_vehicle(3)", "0x014a1a80");
    es.garagePreview().vehicleTypeAtIndex.set("3", 42);
    fails(host, ui, "garage_preview_vehicle(3)", "0x014a1d1c");
    es.garagePreview().previewCache.set(0);
    ok(host, ui, "garage_preview_vehicle(3)");
    CHECK(es.garagePreview().refreshCount == 1);
    ok(host, ui, "garage_preview_vehicle(3)"); // same model again: no refresh
    CHECK(es.garagePreview().refreshCount == 1);

    // ------------------------------------------------------------------
    // Sec27.10 game_lobby_coop_finished: sets the flag and issues a fade
    // request through the existing, already-initialised screen-fade state.
    CHECK(es.coopLobbyFinished() == false);
    ok(host, ui, "game_lobby_coop_finished()");
    CHECK(es.coopLobbyFinished() == true);

    // ------------------------------------------------------------------
    // Sec27.11 dialog_box_force_close: self-consistency gate, deferred
    // close, and the reset path.
    fails(host, ui, "dialog_box_force_close(1)", "self-consistency");
    es.dialogForceClose().slotMatchesId.set("1", false);
    ok(host, ui, "dialog_box_force_close(1)"); // gate fails: real no-op
    CHECK(es.dialogForceClose().slotResetCount == 0);
    es.dialogForceClose().slotMatchesId.set("2", true);
    es.dialogForceClose().timerArmedNotExpired.set("2", true);
    ok(host, ui, "dialog_box_force_close(2)");
    CHECK(es.dialogForceClose().closePendingCount == 1);
    es.dialogForceClose().timerArmedNotExpired.set("2", false);
    es.dialogForceClose().alreadyClosing.set("2", false);
    es.dialogForceClose().hasResultCallback.set("2", true);
    es.dialogForceClose().callbackNameBlank.set("2", false);
    es.dialogForceClose().fullyRemoved.set("2", false);
    ok(host, ui, "dialog_box_force_close(2)");
    CHECK(es.dialogForceClose().resultCallbackFiredCount == 1);
    CHECK(es.dialogForceClose().namedCallbackDispatchCount == 1);
    CHECK(es.dialogForceClose().slotResetCount == 1);

    // ------------------------------------------------------------------
    // Sec27.12 game_autosave: CONFIRMED-initial suppress flag means a
    // fresh process does nothing (no OPEN read needed at all); clearing
    // every gate lets it trigger.
    ok(host, ui, "game_autosave()"); // suppressed by the CONFIRMED initial value - no OPEN touched
    CHECK(es.autosave().triggeredCount == 0);
    es.autosave().suppressFlag.set(false);
    es.autosave().secondFlagSet.set(false);
    es.autosave().missionActive.set(false);
    es.autosave().thirdGateBlocks.set(false);
    es.autosave().fourthFlagNonzero.set(false);
    ok(host, ui, "game_autosave()");
    CHECK(es.autosave().triggeredCount == 1);

    // ------------------------------------------------------------------
    // Sec27.13/27.14/27.21/27.22: shared player-slot count, per-slot maps,
    // and the honest "out of range" false.
    fails(host, ui, "game_send_pause_menu_player_invite(0)", "0x02289b94");
    es.playerSlots().count.set(2);
    ok(host, ui, "assert(game_send_pause_menu_player_invite(5) == false)"); // out of range
    es.playerSlots().sendInviteOk.set("0", true);
    ok(host, ui, "assert(game_send_pause_menu_player_invite(0) == true)");
    es.playerSlots().canSendInvite.set("1", false);
    ok(host, ui, "assert(game_can_send_player_invite(1) == false)");
    ok(host, ui, "game_show_coop_gamercard(0)");
    CHECK(es.playerSlots().gamercardShownCount == 1);
    ok(host, ui, "game_show_coop_gamercard(9)"); // out of range: not counted
    CHECK(es.playerSlots().gamercardShownCount == 1);
    es.playerSlots().joinFriendInProgressOk.set("1", true);
    ok(host, ui, "assert(game_main_menu_join_friend_in_progress(1) == true)");

    // ------------------------------------------------------------------
    // Sec27.15/27.16: the two named examples the manager flagged - the
    // coop friendly-fire round trip (CONFIRMED initial 1 -> pushed 0).
    // get() is the exact mathematical inverse of set()'s own rotation
    // (Sec27.16's own cross-function finding), so get(set(mode)) == mode
    // for every mode - verified by composing the two branch tables
    // directly, not assumed.
    ok(host, ui, "assert(game_get_coop_friendly_fire() == 0)");
    ok(host, ui, "game_set_coop_friendly_fire(0)"); // solo (no session): write allowed
    ok(host, ui, "assert(game_get_coop_friendly_fire() == 0)");
    ok(host, ui, "game_set_coop_friendly_fire(1)");
    ok(host, ui, "assert(game_get_coop_friendly_fire() == 1)");
    ok(host, ui, "game_set_coop_friendly_fire(2)");
    ok(host, ui, "assert(game_get_coop_friendly_fire() == 2)");
    // Host/session gating.
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(false);
    ok(host, ui, "game_set_coop_friendly_fire(0)"); // client: write refused, value unchanged
    ok(host, ui, "assert(game_get_coop_friendly_fire() == 2)");
    es.coopSession().localIsHost.set(true);
    ok(host, ui, "game_set_coop_friendly_fire(0)"); // host: write allowed
    ok(host, ui, "assert(game_get_coop_friendly_fire() == 0)");
    es.coopSession().present.set(false); // restore solo for the rest of the suite

    // ------------------------------------------------------------------
    // Sec27.17/27.18/27.19: no-op, always-true, Steam-interface-gated.
    ok(host, ui, "game_send_party_invites()");
    ok(host, ui, "assert(game_is_connected_to_network() == true)");
    fails(host, ui, "game_is_signed_in()", "0x0101c54c");
    es.steamInterfaceAvailable().set(true);
    ok(host, ui, "assert(game_is_signed_in() == true)");

    // ------------------------------------------------------------------
    // Sec27.20: no OPEN state at all - always records the request and the
    // unconditional error dialog.
    ok(host, ui, "game_sign_into_network(true, 'OnSignInComplete')");
    CHECK(es.signInRequests().size() == 1);
    CHECK(es.signInRequests().back().wantSignIn == true);
    CHECK(es.signInRequests().back().callbackName == "OnSignInComplete");
    CHECK(es.signInErrorDialogCount() == 1);

    // ------------------------------------------------------------------
    // Sec27.23/27.24: mutually exclusive session-type bytes, default true.
    CHECK(es.coopLiveSessionActive() == false);
    CHECK(es.coopSyslinkSessionActive() == false);
    ok(host, ui, "game_coop_start_new_live()"); // default true
    CHECK(es.coopLiveSessionActive() == true);
    ok(host, ui, "game_coop_start_new_syslink()");
    CHECK(es.coopSyslinkSessionActive() == true);
    CHECK(es.coopLiveSessionActive() == false); // mutually exclusive

    // ------------------------------------------------------------------
    // Sec27.25: the three branches, reusing the Sec27.1 minigame singleton.
    es.catMouseMinigame().present.set(false);
    fails(host, ui, "game_get_in_progress_type()", "0x014c8460");
    es.inProgressType().activeMissionPresent.set(false);
    es.catMouseMinigame().present.set(true);
    ok(host, ui, "assert(game_get_in_progress_type() == 4)");
    es.catMouseMinigame().present.set(false);
    es.inProgressType().activityPresent.set(false);
    ok(host, ui, "assert(game_get_in_progress_type() == -1)");
    es.inProgressType().activityPresent.set(true);
    es.inProgressType().activityType.set(7);
    ok(host, ui, "assert(game_get_in_progress_type() == 7)");

    // ==================================================================
    // Sec28: gameplay cluster.
    // ==================================================================

    // Sec28.1 helicopter_set_dont_death_spiral.
    fails(host, gp, "helicopter_set_dont_death_spiral('heli01', true)", "named-object resolution");
    es.objectResolves().set("heli01", true);
    ok(host, gp, "helicopter_set_dont_death_spiral('heli01', true)");
    CHECK(es.helicopterDontDeathSpiral().at("heli01") == true);

    // Sec28.2 helicopter_fly_to_set_goal_direction.
    fails(host, gp, "helicopter_fly_to_set_goal_direction('heli01', 'target01')", "0x00ad31a0");
    es.helicopterFlyTo().qualifies.set("heli01", true);
    ok(host, gp, "helicopter_fly_to_set_goal_direction('heli01', '')"); // empty target: short-circuits, no further OPEN read
    CHECK(es.helicopterFlyTo().lastRequest.count("heli01") == 0);
    es.objectResolves().set("target01", true);
    es.helicopterFlyTo().applyGate.set("heli01", true);
    ok(host, gp, "helicopter_fly_to_set_goal_direction('heli01', 'target01', true)");
    CHECK(es.helicopterFlyTo().lastRequest.at("heli01").targetName == "target01");
    CHECK(es.helicopterFlyTo().lastRequest.at("heli01").useOrientationMode == true);
    CHECK(es.helicopterFlyTo().lastRequest.at("heli01").applied == true);

    // Sec28.3 hdr_bloom_set_multiplier: no rounding.
    CHECK(es.hdrBloomMultiplier() == 1.0f);
    ok(host, gp, "hdr_bloom_set_multiplier(2.5)");
    CHECK(es.hdrBloomMultiplier() == 2.5f);

    // Sec28.4 guardian_angel_enable_indicators.
    fails(host, gp, "guardian_angel_enable_indicators(true)", "0x006d2910");
    es.guardianAngel().modeIsThree.set(false);
    ok(host, gp, "guardian_angel_enable_indicators(true)"); // mode gate false: real no-op
    CHECK(es.guardianAngel().indicatorsEnabled == false);
    es.guardianAngel().modeIsThree.set(true);
    es.guardianAngel().objectPresent.set(true);
    ok(host, gp, "guardian_angel_enable_indicators(true)");
    CHECK(es.guardianAngel().indicatorsEnabled == true);

    // Sec28.5/28.6 group_get_next_npc / group_get_first_npc.
    fails(host, gp, "group_get_next_npc('group01', 'npc01')", "named-object resolution");
    es.objectResolves().set("group01", true);
    es.objectResolves().set("npc01", true);
    fails(host, gp, "group_get_next_npc('group01', 'npc01')", "+0x9c");
    es.groupNextNpcName().set("npc01", "");
    ok(host, gp, "local a, b = group_get_next_npc('group01', 'npc01'); assert(a == nil and b == nil)");
    es.groupNextNpcName().set("npc01", "npc02");
    ok(host, gp, "assert(group_get_next_npc('group01', 'npc01') == 'npc02')");
    fails(host, gp, "group_get_first_npc('group01')", "+0x40");
    es.groupFirstNpcName().set("group01", "npc03");
    ok(host, gp, "assert(group_get_first_npc('group01') == 'npc03')");

    // Sec28.7 get_num_humans_in_trigger: always pushes, 0 for an
    // unresolved trigger.
    es.objectResolves().set("trigger01", false);
    ok(host, gp, "assert(get_num_humans_in_trigger('trigger01') == 0)");
    es.objectResolves().set("trigger02", true);
    es.humansInTriggerCount().set("trigger02", 3);
    ok(host, gp, "assert(get_num_humans_in_trigger('trigger02') == 3)");

    // Sec28.8 get_char_vehicle_is_in_air.
    es.objectResolves().set("char01", false);
    ok(host, gp, "assert(get_char_vehicle_is_in_air('char01') == false)");
    es.objectResolves().set("char02", true);
    es.vehicleInAirByCharacter().set("char02", true);
    ok(host, gp, "assert(get_char_vehicle_is_in_air('char02') == true)");

    // Sec28.9 effect_play_finisher: failure sentinel -1.0 on an invalid
    // index or an unresolved target; success pushes 0x00a48af0's own value.
    // The icon name picked (hence which OPEN read comes first) depends on
    // the gamepad flag, read before the effect-index lookup.
    fails(host, gp, "effect_play_finisher('target03')", "0x0141250d");
    es.effectFinisher().gamepadMode.set(false);
    fails(host, gp, "effect_play_finisher('target03')", "0x005c50b0");
    es.effectFinisher().effectIndexValid.set("vfx_pc_Icon", false);
    ok(host, gp, "assert(effect_play_finisher('target03') == -1.0)");
    es.effectFinisher().effectIndexValid.set("vfx_pc_Icon", true);
    ok(host, gp, "assert(effect_play_finisher('') == -1.0)"); // empty target name
    es.objectResolves().set("target03", true);
    es.effectFinisher().successReturn.set(12.0);
    ok(host, gp, "assert(effect_play_finisher('target03', 1.5, 2) == 12.0)");

    // Sec28.10 dlc3_m03_set_sprint_waning.
    CHECK(es.dlc3SprintWaning() == false);
    ok(host, gp, "dlc3_m03_set_sprint_waning(true)");
    CHECK(es.dlc3SprintWaning() == true);

    // Sec28.11 debris_flow_recycle_object.
    es.objectResolves().set("debris01", true);
    ok(host, gp, "debris_flow_recycle_object(5, 'debris01')");
    CHECK(es.debrisFlowRecycleCount().at(5) == 1);

    // Sec28.12 customization_restore_player_rig. Bit 0's own gender read
    // and count increment happen before bit 1's OPEN read is reached, so
    // the two "fails" calls below that get as far as bit 0 succeeding
    // already bump restorePlayer1Count - the openGuard trampoline does not
    // roll back side effects that ran before the throw (same honest
    // "partial information" shape as dialog_box_force_close's own gates).
    fails(host, gp, "customization_restore_player_rig()", "0x009da4e0"); // default mask 3: bit 0 first
    CHECK(es.playerRig().restorePlayer1Count == 0); // threw before bit 0's own increment
    es.playerRig().localPlayer1Gender.set(1);
    fails(host, gp, "customization_restore_player_rig()", "0x009df3d0"); // bit 0 succeeds, bit 1 throws
    CHECK(es.playerRig().restorePlayer1Count == 1); // bit 0's own increment already happened
    es.playerRig().coopPlayerPresent.set(false);
    ok(host, gp, "customization_restore_player_rig()");
    CHECK(es.playerRig().restorePlayer1Count == 2);
    CHECK(es.playerRig().restoreCoopCount == 0);
    ok(host, gp, "customization_restore_player_rig(1)"); // bit 0 only
    CHECK(es.playerRig().restorePlayer1Count == 3);

    // Sec28.13/28.14: the second manager-flagged example - a shared
    // setter with literal 0 (disable), read from the entry's own body.
    CHECK(es.cribWeaponAddEnabled() == true); // CONFIRMED initial 1
    ok(host, gp, "crib_weapon_add_disable()");
    CHECK(es.cribWeaponAddEnabled() == false);
    ok(host, gp, "crib_weapon_add_enable()");
    CHECK(es.cribWeaponAddEnabled() == true);

    // Sec28.15 crib_unlock_strongold.
    fails(host, gp, "crib_unlock_strongold('stronghold01')", "named-object resolution");
    es.objectResolves().set("stronghold01", true);
    ok(host, gp, "crib_unlock_strongold('stronghold01')"); // solo (no session): client/solo branch
    CHECK(es.stronghold().clientOrSoloRequestCount == 1);
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    es.stronghold().stillLocked.set("stronghold01", true);
    ok(host, gp, "crib_unlock_strongold('stronghold01')");
    CHECK(es.stronghold().unlockedCount == 1);
    ok(host, gp, "crib_unlock_strongold('stronghold01')"); // already unlocked: no-op
    CHECK(es.stronghold().unlockedCount == 1);
    es.coopSession().present.set(false);

    // Sec28.16 continuous_explosion_start.
    fails(host, gp, "continuous_explosion_start('def01', 'target04')", "0x005eb390");
    es.continuousExplosion().definitionResolves.set("def01", true);
    es.objectResolves().set("target04", true);
    fails(host, gp, "continuous_explosion_start('def01', 'target04')", "0x012ec964");
    es.continuousExplosion().active.set(false);
    ok(host, gp, "continuous_explosion_start('def01', 'target04')");
    CHECK(es.continuousExplosion().startCount == 1);
    ok(host, gp, "continuous_explosion_start('def01', 'target04')"); // already active: ignored
    CHECK(es.continuousExplosion().startCount == 1);

    // Sec28.17 clear_callbacks_for_obj.
    es.objectResolves().set("hookobj01", true);
    ok(host, gp, "clear_callbacks_for_obj('hookobj01')");
    CHECK(es.callbacksClearedCount().at("hookobj01") == 1);

    // Sec28.18 city_zone_swap_is_active: empty set is the honest default.
    ok(host, gp, "assert(city_zone_swap_is_active('downtown') == false)");
    es.activeCityZoneSwapHashes().insert(sr3save::nameHash("downtown"));
    ok(host, gp, "assert(city_zone_swap_is_active('downtown') == true)");
    ok(host, gp, "assert(city_zone_swap_is_active('DOWNTOWN') == true)"); // nameHash lower-cases internally
    ok(host, gp, "assert(city_zone_swap_is_active('uptown') == false)");

    // Sec28.19 character_set_counter_on_grabbed.
    es.objectResolves().set("char03", true);
    fails(host, gp, "character_set_counter_on_grabbed('char03', true)", "0x008ae480");
    es.counterOnGrabbed().gatePasses.set("char03", true);
    ok(host, gp, "character_set_counter_on_grabbed('char03', true)");
    CHECK(es.counterOnGrabbed().value.at("char03") == true);

    // Sec28.20 character_remove_child_item_by_name.
    es.objectResolves().set("char04", true);
    es.characterChildItems()["char04"] = {"Pistol", "Shotgun"};
    ok(host, gp, "character_remove_child_item_by_name('char04', 'pistol')"); // case-insensitive
    CHECK(es.characterChildItems().at("char04").size() == 1);
    CHECK(es.characterChildItems().at("char04")[0] == "Shotgun");

    // Sec28.21 character_get_gender: cross-checked against the same field
    // Sec28.12 reads (independently keyed, see engine_state.h's own note).
    // "Resolve failure" is a known-false case here (same objectResolves()
    // convention as every other resolver in this file), not an unset one.
    es.objectResolves().set("nosuchchar", false);
    ok(host, gp, "assert(character_get_gender('nosuchchar') == 0)");
    es.objectResolves().set("char05", true);
    es.characterGender().set("char05", 1);
    ok(host, gp, "assert(character_get_gender('char05') == 1)");

    // Player gender initial state (2026-10-02, spec-lua-api-behaviour.md
    // Sec16.8/Sec16.8a, spec-save-format.md Sec10.4, spec-tables-
    // customization.md Sec16.2): applySpecInitialState() (run automatically
    // by Host's constructor, src/lua_host.cpp) pre-populates "#PLAYER1#"'s
    // gender via the fresh-game default-preset path (this host never loads
    // a real save file), matching the shipped player_presets.xtbl Default
    // row ("male_white", Gender=0). This is the exact mechanism that used to
    // leave mission m21 blocked on "character +0xa41 gender byte (1=female,
    // else male)['#PLAYER1#'] is OPEN" when the mission queried its own
    // resolved player object - no manual es.characterGender().set() call
    // here, unlike the char05 case above, because the whole point is that
    // it is already known from host construction.
    CHECK(es.objectResolves().known("#PLAYER1#") && es.objectResolves().get("#PLAYER1#"));
    CHECK(es.characterGender().known("#PLAYER1#"));
    CHECK(es.characterGender().get("#PLAYER1#") == 0);
    ok(host, gp, "assert(character_get_gender('#PLAYER1#') == 0)"); // male, Default preset row
    // "#PLAYER2#" resolves (Sec29.4's unconditional name registration) but a
    // co-op session is absent at start (item 1 of applySpecInitialState), so
    // no second player character object exists to even hold a sentinel pair
    // - its gender correctly stays OPEN, not guessed.
    CHECK(es.objectResolves().get("#PLAYER2#") == true);
    CHECK(!es.characterGender().known("#PLAYER2#"));
    fails(host, gp, "character_get_gender('#PLAYER2#')",
          "character +0xa41 gender byte (1=female, else male)['#PLAYER2#']");

    // Sec28.22 character_evacuate_from_all_vehicles.
    es.objectResolves().set("char06", true);
    ok(host, gp, "character_evacuate_from_all_vehicles('char06')");
    CHECK(es.evacuateFromVehiclesCount().at("char06") == 1);

    // Sec28.23 cellphone_animate_start_do.
    fails(host, gp, "cellphone_animate_start_do()", "0x009da4e0");
    es.hasLocalPlayer().set(true);
    fails(host, gp, "cellphone_animate_start_do()", "0x00943a20");
    es.cellphoneAnimSuppressed().set(true);
    ok(host, gp, "cellphone_animate_start_do()"); // suppressed: real no-op
    CHECK(es.cellphoneAnimPlayedCount() == 0);
    es.cellphoneAnimSuppressed().set(false);
    ok(host, gp, "cellphone_animate_start_do()");
    CHECK(es.cellphoneAnimPlayedCount() == 1);

    // Sec28.24/28.25 boss_battle_matt cluster. The default id is CONFIRMED
    // 0 (not -1) - id 0 takes the id>=0 branch with no OPEN read, so the
    // retry-counter branch needs an explicit id <= -1 argument.
    fails(host, gp, "boss_battle_matt_get_cheat()", "0x012ec730");
    ok(host, gp, "boss_battle_matt_cheats_start(5, 1, 2, false)"); // id >= 0: no OPEN read needed
    CHECK(es.bossBattleMatt().lastId == 5);
    CHECK(es.bossBattleMatt().lastN2 == 1.0);
    CHECK(es.bossBattleMatt().lastN3 == 2.0);
    CHECK(es.bossBattleMatt().active == true);
    fails(host, gp, "boss_battle_matt_cheats_start(-1)", "0x012ec720"); // id -1: retry counter OPEN
    es.bossBattleMatt().retryCounter.set(0);
    ok(host, gp, "boss_battle_matt_cheats_start(-1)"); // id -1, retries 0 < 4: proceeds
    es.bossBattleMatt().cheatSlot.set(-1);
    ok(host, gp, "assert(boss_battle_matt_get_cheat() == -1)");

    if (g_failures == 0) std::cout << "batch 2026-10-01 (Sec27/Sec28, 50 functions): all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
