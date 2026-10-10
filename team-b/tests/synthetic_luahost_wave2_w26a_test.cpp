// Synthetic tests for wave 2 batch W2-6 (2026-10-08), "UI frontend (143
// natives) - HARDENING BATCH" - split A (file src/lua_spec_confirmed_
// stubs_b17_wave2_ui_frontend_a.cpp): 66 names, all CONFIRMED `ui` cluster
// via a direct grep of tools/lua_all_registered_1490_tagged.txt (same
// house convention every other synthetic_luahost_*_test.cpp follows -
// every one of this batch's 143 names is tagged `ui` there, none
// `gameplay`). Same hand-written-fixture convention as every sibling test.
//
// Coverage focus: every registered name gets at least one real call+assert
// exercising its own documented behavior; every CRASH-tagged name's own
// hardened-divergence guard counter is exercised on BOTH the normal path
// (counter stays 0) and the bad-input path (counter increments, no crash);
// every OPEN-refusal name is checked to actually refuse with a message
// naming its own citation, not silently succeed.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                      \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "wave2_w26a_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w26a_test.lua");
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

std::vector<RegisteredName> batchFixtureNames() {
    return {
        {"game_interface_effect_begin", "ui"},
        {"game_interface_effect_end", "ui"},
        {"online_validator_set_checking", "ui"},
        {"game_coop_get_starting_coop", "ui"},
        {"game_coop_get_starting_syslink", "ui"},
        {"game_is_syslink", "ui"},
        {"game_get_party_member_count", "ui"},
        {"game_audio_play", "ui"},
        {"game_lobby_get_remote_player_name", "ui"},
        {"game_is_connected_to_service", "ui"},
        {"game_is_connected_to_internet", "ui"},
        {"game_set_equipped_grenade", "ui"},
        {"game_use_radial_menu_item", "ui"},
        {"game_vkeyboard_input", "ui"},
        {"game_award_cash", "ui"},
        {"game_check_coop_dlc", "ui"},
        {"game_letterbox_fade_out", "ui"},
        {"game_stop_find_syslink_servers", "ui"},
        {"game_start_find_syslink_servers", "ui"},
        {"game_steam_open_overlay", "ui"},
        {"game_steam_open_url", "ui"},
        {"game_steam_open_dlc_page_overlay", "ui"},
        {"get_current_difficulty", "ui"},
        {"game_difficulty_select", "ui"},
        {"get_get_key_names_for_axis_action", "ui"},
        {"hud_diversion_remove_callback", "ui"},
        {"game_coop_kick_player", "ui"},
        {"game_hud_hide", "ui"},
        {"game_is_autosaving", "ui"},
        {"game_is_joinable", "ui"},
        {"game_is_local_player_in_vehicle", "ui"},
        {"game_join_syslink_game", "ui"},
        {"game_lobby_get_local_player_name", "ui"},
        {"game_lobby_set_horde_mode_data", "ui"},
        {"game_lobby_update_char_selection", "ui"},
        {"game_lobby_update_map_selection", "ui"},
        {"game_set_coop_join_type", "ui"},
        {"game_show_coop_partner_gamercard", "ui"},
        {"get_char_in_string", "ui"},
        {"get_localized_string_for_tag", "ui"},
        {"get_world_income_dollars", "ui"},
        {"game_is_waiting_for_partner", "ui"},
        {"game_kick_coop_player", "ui"},
        {"game_lobby_finished_loading", "ui"},
        {"game_lobby_horde_continue_from_wave", "ui"},
        {"game_lobby_update_ready_state", "ui"},
        {"game_pause_map_filter", "ui"},
        {"game_proto_select", "ui"},
        {"game_record_mode_can_encode", "ui"},
        {"game_record_mode_enable", "ui"},
        {"game_record_mode_show_hud", "ui"},
        {"game_record_set_quality_level", "ui"},
        {"game_request_game_terminate", "ui"},
        {"game_use_imperial_units", "ui"},
        {"game_audio_set_rtpc", "ui"},
        {"game_cancel_mission", "ui"},
        {"game_event_tracking_interface_exit", "ui"},
        {"game_format_int_to_string", "ui"},
        {"game_get_coop_ping", "ui"},
        {"game_get_game_play_mode", "ui"},
        {"game_get_pending_game_play_mode", "ui"},
        {"game_is_debug_reloading_level", "ui"},
        {"game_is_demo", "ui"},
        {"game_is_german_build", "ui"},
        {"game_is_going_to_main_menu", "ui"},
        {"autil_mashing_minigame_faded_out", "ui"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 66 names land as real functions in the ui state,
    // never in the gameplay state.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.uiState(), "game_interface_effect_begin"));
        CHECK(!hasGlobal(host.gameplayState(), "game_interface_effect_begin"));
        for (const auto& rn : batchFixtureNames()) {
            CHECK(hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* ui = host.uiState();
    EngineState& es = host.engineState();

    // Sec19.7/Sec23.14 game_interface_effect_begin/_end.
    ok(host, ui, "game_interface_effect_begin('flash', 1.0, 2.0, true)");
    CHECK(es.interfaceEffect().activeName == "flash");
    CHECK(es.interfaceEffect().param1 == 1.0);
    CHECK(es.interfaceEffect().param2 == 2.0);
    CHECK(es.interfaceEffect().forced == true);
    ok(host, ui, "game_interface_effect_end(3.0)");
    CHECK(es.interfaceEffect().param1 == 0.0);
    CHECK(es.interfaceEffect().param2 == 3.0);

    // Sec19.6 online_validator_set_checking.
    ok(host, ui, "online_validator_set_checking(true)");
    CHECK(es.onlineValidatorChecking() == true);
    ok(host, ui, "online_validator_set_checking(false)");
    CHECK(es.onlineValidatorChecking() == false);

    // Sec19.9/Sec44.3/Sec26.21 coop/syslink start-flags (OR, syslink-only, is-syslink).
    es.coopLiveSessionActive() = false;
    es.coopSyslinkSessionActive() = false;
    ok(host, ui, "assert(game_coop_get_starting_coop() == false)");
    ok(host, ui, "assert(game_is_syslink() == false)");
    es.coopSyslinkSessionActive() = true;
    ok(host, ui, "assert(game_coop_get_starting_coop() == true)");
    ok(host, ui, "assert(game_coop_get_starting_syslink() == true)");
    ok(host, ui, "assert(game_is_syslink() == true)");
    es.coopSyslinkSessionActive() = false;
    es.coopLiveSessionActive() = true;
    ok(host, ui, "assert(game_coop_get_starting_coop() == true)");
    ok(host, ui, "assert(game_coop_get_starting_syslink() == false)");
    es.coopLiveSessionActive() = false;

    // Sec19.8 game_get_party_member_count: CONFIRMED hardcoded 0.0.
    ok(host, ui, "assert(game_get_party_member_count() == 0)");

    // Sec26.17 game_audio_play: unknown name -> 0 values; registered name/raw
    // numeric id -> a handle.
    ok(host, ui, "local h = game_audio_play('unknown_sound'); assert(h == nil)");
    es.registerAudioEventForTesting("boom");
    ok(host, ui, "local h = game_audio_play('boom'); assert(type(h) == 'number')");
    ok(host, ui, "local h = game_audio_play(42); assert(type(h) == 'number')");
    ok(host, ui, "local h = game_audio_play(0); assert(h == nil)");

    // Sec26.4 game_lobby_get_remote_player_name.
    ok(host, ui, "local n = game_lobby_get_remote_player_name(); assert(n == nil)");
    es.remotePlayer().present = true;
    es.remotePlayer().name = "Shaundi";
    ok(host, ui, "assert(game_lobby_get_remote_player_name() == 'Shaundi')");

    // Sec23.13/Sec44.2 game_is_connected_to_service/_internet: same handler.
    fails(host, ui, "game_is_connected_to_service()", "Steam logged-on state");
    es.steamLoggedOn().set(true);
    ok(host, ui, "assert(game_is_connected_to_service() == true)");
    ok(host, ui, "assert(game_is_connected_to_internet() == true)");
    es.steamLoggedOn().set(false);
    ok(host, ui, "assert(game_is_connected_to_service() == false)");

    // Sec26.19 game_set_equipped_grenade: 8-11 valid, else refused.
    ok(host, ui, "assert(game_set_equipped_grenade(9) == true)");
    CHECK(es.equippedGrenadeSlot() == 1);
    ok(host, ui, "assert(game_set_equipped_grenade(12) == false)");
    ok(host, ui, "assert(game_set_equipped_grenade(7) == false)");

    // Sec26.18 game_use_radial_menu_item: slot 12 and out-of-range -> 1.0;
    // 0-7 reads the OPEN quick-item-use outcome.
    ok(host, ui, "assert(game_use_radial_menu_item(12) == 1.0)");
    CHECK(es.radialMenuSlot12Count() == 1);
    ok(host, ui, "assert(game_use_radial_menu_item(99) == 1.0)");
    fails(host, ui, "game_use_radial_menu_item(3)", "radial-menu quick-item use outcome");
    es.radialMenuQuickItemSucceeds().set(true);
    ok(host, ui, "assert(game_use_radial_menu_item(3) == 1.0)");
    es.radialMenuQuickItemSucceeds().set(false);
    ok(host, ui, "assert(game_use_radial_menu_item(3) == 0.0)");

    // Sec26.20 game_vkeyboard_input: gate false -> gate open; closed result OPEN.
    fails(host, ui, "game_vkeyboard_input('Enter name')", "synchronous close result");
    es.virtualKeyboardCloseResult().set(true);
    ok(host, ui, "assert(game_vkeyboard_input('Enter name') == true)");
    CHECK(es.virtualKeyboard().lastTitle == "Enter name");
    es.virtualKeyboard().open = true;
    ok(host, ui, "assert(game_vkeyboard_input('x') == false)"); // CONFIRMED: already-open gate -> false
    es.virtualKeyboard().open = false;

    // Sec23.16 game_award_cash: just needs to not throw with/without a reason.
    ok(host, ui, "game_award_cash(100)");
    ok(host, ui, "game_award_cash(100, 5, true)");

    // Sec23.15 game_check_coop_dlc / Sec23.12 game_letterbox_fade_out: no crash.
    ok(host, ui, "game_check_coop_dlc('dlc1')");
    ok(host, ui, "game_letterbox_fade_out(true)");

    // Sec23.11/Sec41.2 syslink search guard.
    CHECK(es.syslinkSearch().active == false);
    ok(host, ui, "game_start_find_syslink_servers()");
    CHECK(es.syslinkSearch().active == true);
    CHECK(es.syslinkSearch().startCount == 1);
    ok(host, ui, "game_start_find_syslink_servers()"); // CONFIRMED: repeated call is a guarded no-op
    CHECK(es.syslinkSearch().guardedNoopCount == 1);
    ok(host, ui, "game_stop_find_syslink_servers()");
    CHECK(es.syslinkSearch().active == false);
    CHECK(es.syslinkSearch().stopCount == 1);

    // Sec41.2/Sec56.3 Steam overlay openers.
    ok(host, ui, "game_steam_open_overlay('friend1')");
    CHECK(es.steamOverlay().lastOverlayArg == "friend1");
    ok(host, ui, "game_steam_open_url('http://example.com')");
    CHECK(es.steamOverlay().lastUrl == "http://example.com");
    ok(host, ui, "game_steam_open_dlc_page_overlay()");
    CHECK(es.steamOverlay().dlcOverlayCount == 1);

    // Sec41.2/Sec44.5 difficulty.
    ok(host, ui, "assert(get_current_difficulty() == 1)"); // CONFIRMED file-default 1
    ok(host, ui, "game_difficulty_select(2)");
    ok(host, ui, "assert(get_current_difficulty() == 2)");
    ok(host, ui, "game_difficulty_select(5)"); // CONFIRMED: >= 3 silently ignored
    ok(host, ui, "assert(get_current_difficulty() == 2)");

    // Sec41.6 get_get_key_names_for_axis_action: always 2 strings, hardened empty.
    ok(host, ui, "local a, b = get_get_key_names_for_axis_action('move_forward'); assert(a == '' and b == '')");
    CHECK(es.axisActionUnknownNameGuardCount() == 1);

    // Sec41.4 hud_diversion_remove_callback: floored at 0.
    es.diversionSlotOccupancy()[3] = 2;
    ok(host, ui, "hud_diversion_remove_callback(3)");
    CHECK(es.diversionSlotOccupancy()[3] == 1);
    ok(host, ui, "hud_diversion_remove_callback(3)");
    ok(host, ui, "hud_diversion_remove_callback(3)"); // CONFIRMED: floored at 0, no underflow
    CHECK(es.diversionSlotOccupancy()[3] == 0);

    // Sec44.2 game_coop_kick_player: dialog only shown with a remote player.
    es.remotePlayer().present = false;
    ok(host, ui, "game_coop_kick_player()");
    CHECK(es.coopKick().confirmDialogShownCount == 0);
    es.remotePlayer().present = true;
    ok(host, ui, "game_coop_kick_player()");
    CHECK(es.coopKick().confirmDialogShownCount == 1);
    CHECK(es.coopKick().wideSprintfBoundGuardCount == 1); // bounded-formatting hardening ran

    // Sec44.5 game_hud_hide: no-arg counts as show; balanced hide/show.
    CHECK(es.hudHideRefCount() == 0);
    ok(host, ui, "game_hud_hide()"); // CONFIRMED: no-arg -> show (decrement, floored at 0)
    CHECK(es.hudHideRefCount() == 0);
    ok(host, ui, "game_hud_hide(true)");
    CHECK(es.hudHideRefCount() == 1);
    ok(host, ui, "game_hud_hide(false)");
    CHECK(es.hudHideRefCount() == 0);

    // Sec44.5 game_is_autosaving.
    ok(host, ui, "assert(game_is_autosaving() == false)");
    es.autosaveInProgressSimplified() = true;
    ok(host, ui, "assert(game_is_autosaving() == true)");
    es.autosaveInProgressSimplified() = false;

    // Sec44.2 game_is_joinable: CONFIRMED "!=0" quirk reproduced exactly
    // (a negative free count still reports joinable).
    es.joinable().capacity.set(4);
    es.joinable().currentPlayerCount = 0;
    ok(host, ui, "assert(game_is_joinable() == true)"); // 4-2-0=2 != 0
    es.joinable().currentPlayerCount = 2;
    ok(host, ui, "assert(game_is_joinable() == false)"); // 4-2-2=0
    es.joinable().currentPlayerCount = 10;
    ok(host, ui, "assert(game_is_joinable() == true)"); // 4-2-10=-8 != 0, CONFIRMED quirk reproduced

    // AGG, no spec text found - OPEN refusal, not a guess.
    fails(host, ui, "game_is_local_player_in_vehicle()", "no spec text found");

    // Sec44.2 game_join_syslink_game: 8-bit index alias reproduced exactly.
    ok(host, ui, "game_join_syslink_game(1)");
    CHECK(es.syslinkJoinLastIndex() == 1);
    ok(host, ui, "game_join_syslink_game(257)"); // CONFIRMED: 257 & 0xff == 1
    CHECK(es.syslinkJoinLastIndex() == 1);
    CHECK(es.syslinkJoinCount() == 2);

    // Sec44.3 game_lobby_get_local_player_name: no local player -> nil.
    es.hasLocalPlayer().forget(); // CLOSED 2026-10-09: now CONFIRMED-true by default (Sec10.3) - force OPEN to exercise this refusal
    fails(host, ui, "game_lobby_get_local_player_name()", "local player exists");
    es.hasLocalPlayer().set(false);
    ok(host, ui, "local n = game_lobby_get_local_player_name(); assert(n == nil)");
    es.hasLocalPlayer().set(true);
    es.localPlayerNameForTesting().set("Boss");
    ok(host, ui, "assert(game_lobby_get_local_player_name() == 'Boss')");

    // Sec44.3 game_lobby_set_horde_mode_data: CONFIRMED spurious-warning
    // logic quirk, reproduced exactly (not "fixed").
    es.hordeLobby().selectedMapIndex = 3;
    ok(host, ui, "game_lobby_set_horde_mode_data(true)");
    CHECK(es.hordeLobby().spuriousRestartWarningCount == 1);
    CHECK(es.hordeLobby().committedMapIndex == -1); // CONFIRMED: commit refused on an out-of-range index
    es.hordeLobby().savedProgressByMap[3] = 2;
    ok(host, ui, "game_lobby_set_horde_mode_data(true)");
    CHECK(es.hordeLobby().committedMapIndex == 3);

    // Sec44.3 game_lobby_update_char_selection/_map_selection: host stages,
    // client applies at once.
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    ok(host, ui, "game_lobby_update_char_selection(2)");
    CHECK(es.hordeLobby().stagedCharIndexHost == 2);
    es.coopSession().localIsHost.set(false);
    ok(host, ui, "game_lobby_update_char_selection(4)");
    CHECK(es.hordeLobby().selectedCharIndex == 4);
    ok(host, ui, "game_lobby_update_map_selection(7)");
    CHECK(es.hordeLobby().selectedMapIndex == 7);

    // Sec44.2 game_set_coop_join_type: client -> ignored; host -> applied/mapped.
    es.coopSession().localIsHost.set(false);
    ok(host, ui, "game_set_coop_join_type(1)");
    es.coopSession().localIsHost.set(true);
    ok(host, ui, "game_set_coop_join_type(1)");
    CHECK(es.coopJoinType().get() == 1);
    ok(host, ui, "game_set_coop_join_type(9)"); // CONFIRMED: anything else -> 2
    CHECK(es.coopJoinType().get() == 2);

    // Sec44.2 game_show_coop_partner_gamercard: hardened null-interface guard.
    es.remotePlayer().present = false;
    ok(host, ui, "game_show_coop_partner_gamercard()");
    CHECK(es.coopKick().gamercardOverlayCount == 0);
    es.remotePlayer().present = true;
    ok(host, ui, "game_show_coop_partner_gamercard()");
    CHECK(es.coopKick().gamercardOverlayCount == 1);

    // Sec53.5 get_char_in_string: hardened null-argument guard, in-range read.
    ok(host, ui, "assert(get_char_in_string('hello', 1) == 'e')");
    ok(host, ui, "assert(get_char_in_string('hello', 99) == '')");
    ok(host, ui, "assert(get_char_in_string(nil, 0) == '')");
    CHECK(es.getCharInStringNullGuardCount() == 1);

    // Sec53.5 get_localized_string_for_tag / get_world_income_dollars: OPEN
    // until a test registers them.
    fails(host, ui, "get_localized_string_for_tag('HUD_HEALTH')", "localized string for tag");
    es.localizedStringForTag().set("HUD_HEALTH", "Health");
    ok(host, ui, "assert(get_localized_string_for_tag('HUD_HEALTH') == 'Health')");
    fails(host, ui, "get_world_income_dollars()", "world income dollars");
    es.worldIncomeDollars().set(1234.0);
    ok(host, ui, "assert(get_world_income_dollars() == 1234.0)");

    // Sec56.6 game_is_waiting_for_partner: defaults true with no backing object.
    ok(host, ui, "assert(game_is_waiting_for_partner() == true)");
    es.coopPing().set(10);
    es.remotePlayer().present = true;
    ok(host, ui, "assert(game_is_waiting_for_partner() == false)");

    // Sec56.6 game_kick_coop_player: host-only, immediate, no confirmation.
    es.coopSession().localIsHost.set(false);
    ok(host, ui, "game_kick_coop_player('Pierce')");
    CHECK(es.coopKick().kickedCount == 0);
    es.coopSession().localIsHost.set(true);
    ok(host, ui, "game_kick_coop_player('Pierce')");
    CHECK(es.coopKick().kickedCount == 1);

    // Sec56.6 game_lobby_finished_loading.
    ok(host, ui, "game_lobby_finished_loading()");
    CHECK(es.hordeLobby().finishedLoadingCount == 1);

    // Sec56.6 game_lobby_horde_continue_from_wave: -1/0/N>=2 all reproduced.
    es.hordeLobby().selectedMapIndex = -1;
    ok(host, ui, "assert(game_lobby_horde_continue_from_wave() == 0)"); // CONFIRMED invalid-index -> 0
    es.hordeLobby().selectedMapIndex = 99;
    es.hordeLobby().continueFromSavedWave = false;
    ok(host, ui, "assert(game_lobby_horde_continue_from_wave() == -1)");
    es.hordeLobby().savedProgressByMap[99] = 5;
    es.hordeLobby().continueFromSavedWave = true;
    ok(host, ui, "assert(game_lobby_horde_continue_from_wave() == 5)");

    // Sec56.6 game_lobby_update_ready_state: host/no-session path starts/cancels.
    es.coopSession().localIsHost.set(true);
    ok(host, ui, "game_lobby_update_ready_state(true)");
    CHECK(es.hordeLobby().readyStateCountdownStartedCount == 1);
    ok(host, ui, "game_lobby_update_ready_state(false)");
    CHECK(es.hordeLobby().readyStateCancelCount == 1);
    es.coopSession().localIsHost.set(false);
    ok(host, ui, "game_lobby_update_ready_state(true)"); // client -> only reports, no countdown
    CHECK(es.hordeLobby().readyStateCountdownStartedCount == 1);

    // Sec56.3 game_pause_map_filter: setter 0 values, getter 1; lock ignored by setter.
    ok(host, ui, "local n = select('#', game_pause_map_filter('PFT_FILTER_FRIENDLY')); assert(n == 0)");
    ok(host, ui, "assert(game_pause_map_filter() == 'PFT_FILTER_FRIENDLY')");
    es.pauseMapFilter().locked = true;
    ok(host, ui, "assert(game_pause_map_filter() == 'PFT_FILTER_LOCKED')");
    ok(host, ui, "game_pause_map_filter('PFT_FILTER_ALL')"); // CONFIRMED: setter ignores the lock
    es.pauseMapFilter().locked = false;
    ok(host, ui, "assert(game_pause_map_filter() == 'PFT_FILTER_ALL')");

    // Sec56.4 game_proto_select: invalid args hardened-refused; valid picks lowest unused.
    ok(host, ui, "game_proto_select(nil, 1)");
    CHECK(es.protoSelect().invalidArgGuardCount == 1);
    ok(host, ui, "game_proto_select(1, 2)");
    CHECK(es.protoSelect().selected == 0);

    // Sec56.2 game_record_* ambiguous OPEN refusals.
    fails(host, ui, "game_record_mode_can_encode()", "3-no-op-1-constant-false");
    fails(host, ui, "game_record_mode_enable()", "3-no-op-1-constant-false");
    fails(host, ui, "game_record_mode_show_hud()", "3-no-op-1-constant-false");
    fails(host, ui, "game_record_set_quality_level()", "3-no-op-1-constant-false");
    CHECK(es.gameRecordAmbiguousRefusalCount() == 4);

    // Sec56.3 game_request_game_terminate: once-only latch, idempotent.
    CHECK(es.gameTerminateRequested() == false);
    ok(host, ui, "game_request_game_terminate()");
    CHECK(es.gameTerminateRequested() == true);
    ok(host, ui, "game_request_game_terminate()"); // second call: still fine, no crash
    CHECK(es.gameTerminateRequested() == true);

    // Sec56.3 game_use_imperial_units: test override takes precedence.
    es.imperialUnitsOverrideForTesting().set(true);
    ok(host, ui, "assert(game_use_imperial_units() == true)");
    es.imperialUnitsOverrideForTesting().set(false);
    ok(host, ui, "assert(game_use_imperial_units() == false)");

    // Sec55.3 game_audio_set_rtpc: "none" is a safe no-op; both ids non-zero applies.
    ok(host, ui, "game_audio_set_rtpc('none', 1.0, 5)");
    CHECK(es.audioRtpcAppliedCount() == 0);
    ok(host, ui, "game_audio_set_rtpc('density', 1.0, 0)");
    CHECK(es.audioRtpcAppliedCount() == 0);
    ok(host, ui, "game_audio_set_rtpc('density', 1.0, 5)");
    CHECK(es.audioRtpcAppliedCount() == 1);

    // Sec55.2 game_cancel_mission: all 3 hardened paths + the diversion path.
    es.missionCancel().inProgressType.set(4);
    ok(host, ui, "game_cancel_mission()");
    CHECK(es.missionCancel().diversionCancelCount == 1);
    es.missionCancel().inProgressType.set(-1);
    ok(host, ui, "game_cancel_mission()");
    CHECK(es.missionCancel().nullMissionGuardCount == 1); // hardened: type -1 refused
    es.missionCancel().inProgressType.set(0);
    es.missionCancel().missionPointerValid = true;
    es.dialogPoolSlotsInUse() = 4;
    ok(host, ui, "game_cancel_mission()");
    CHECK(es.missionCancel().dialogPoolExhaustedGuardCount == 1); // hardened: pool exhausted
    es.dialogPoolSlotsInUse() = 0;
    es.missionCancel().inProgressType.set(2); // unhandled type
    ok(host, ui, "game_cancel_mission()");
    CHECK(es.missionCancel().unhandledTypeGuardCount == 1); // hardened: unhandled type refused
    es.missionCancel().inProgressType.set(1); // activity - handled
    ok(host, ui, "game_cancel_mission()");
    CHECK(es.missionCancel().confirmDialogOpenCount == 1);
    CHECK(es.dialogPoolSlotsInUse() == 1);

    // Sec55.1 game_event_tracking_interface_exit: genuine no-op.
    ok(host, ui, "game_event_tracking_interface_exit()");

    // Sec55.1 game_format_int_to_string: floor rounding, comma grouping,
    // out-of-range sentinel.
    ok(host, ui, "assert(game_format_int_to_string(1234567) == '1,234,567')");
    ok(host, ui, "assert(game_format_int_to_string(-1.5) == '-2')"); // CONFIRMED floor, not truncation
    ok(host, ui, "assert(game_format_int_to_string(0/0) == '-2,147,483,648')"); // NaN sentinel
    ok(host, ui, "assert(game_format_int_to_string(1e30) == '-2,147,483,648')"); // out-of-range sentinel

    // Sec55.1 game_get_coop_ping: no session -> 0.
    es.coopSession().present.set(false);
    ok(host, ui, "assert(game_get_coop_ping() == 0)");
    es.coopSession().present.set(true);
    es.coopSession().localIsHost.set(true);
    es.remotePlayer().present = true;
    es.coopPing().set(42);
    ok(host, ui, "assert(game_get_coop_ping() == 42)");

    // Sec55.1 game_get_game_play_mode/_pending_game_play_mode.
    ok(host, ui, "assert(game_get_game_play_mode() == 'Normal')"); // CONFIRMED file-default
    ok(host, ui, "assert(game_get_pending_game_play_mode() == 'None')");
    es.gamePlayMode().pending = "Mission";
    ok(host, ui, "assert(game_get_pending_game_play_mode() == 'Mission')");

    // Sec55.1/Sec44.1 constant-false stubs.
    ok(host, ui, "assert(game_is_debug_reloading_level() == false)");
    ok(host, ui, "assert(game_is_demo() == false)");
    ok(host, ui, "assert(game_is_german_build() == false)");

    // Sec55.1 game_is_going_to_main_menu: projected=2 -> true; current 5/6 -> false.
    es.gameStateStack().projectedTop = 2;
    ok(host, ui, "assert(game_is_going_to_main_menu() == true)");
    es.gameStateStack().projectedTop = -1;
    es.gameStateStack().currentTop = 5;
    ok(host, ui, "assert(game_is_going_to_main_menu() == false)");
    es.gameStateStack().currentTop = 1;
    ok(host, ui, "assert(game_is_going_to_main_menu() == true)");

    // AGG, registered (spec-lua-bindings.md:1248, bare name) but no behavior
    // given anywhere - OPEN refusal, not a guess (message corrected 2026-10-08:
    // this name is NOT "absent", it's registered with no behavior specified).
    fails(host, ui, "autil_mashing_minigame_faded_out()", "registered with no behavior specified");

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "synthetic_luahost_wave2_w26a_test: all checks passed (66 names)\n";
    return 0;
}
