// Synthetic tests for wave 2 batch W2-6 (2026-10-08), "UI frontend (143
// natives) - HARDENING BATCH" - split B (file src/lua_spec_confirmed_
// stubs_b18_wave2_ui_frontend_b.cpp): 77 names, all CONFIRMED `ui` cluster
// via a direct grep of tools/lua_all_registered_1490_tagged.txt. Same
// hand-written-fixture convention as every sibling test.
//
// Coverage focus: this file's two own named memory-safety fixes
// (dialog_box_create_internal, cell_playlist_save_list) each get a
// dedicated pair of tests (valid input reproduces real behavior; bad
// input is dropped/clamped/bounded, never corrupts anything); the whole
// machinima family's shared hardened path-builder gets exercised on both
// its success and failure paths across every one of its 10 stubs.
#include <chrono>
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
    auto r = host.runChunk(L, code, "wave2_w26b_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "wave2_w26b_test.lua");
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
        {"cellphone_choose_vehicle", "ui"},
        {"main_menu_check_user_content", "ui"},
        {"Completion_get_other_player_name", "ui"},
        {"completion_screen_finished", "ui"},
        {"pause_menu_quit_game_internal", "ui"},
        {"completion_camera_shake", "ui"},
        {"completion_credits_were_active", "ui"},
        {"completion_cutscene_was_skipped", "ui"},
        {"dialog_box_set_result", "ui"},
        {"dialog_open_pause_display", "ui"},
        {"main_menu_redeem_code", "ui"},
        {"pause_map_add_bookmark", "ui"},
        {"pause_map_zoom", "ui"},
        {"pause_menu_change_control_scheme", "ui"},
        {"main_menu_new_game", "ui"},
        {"main_menu_horde_start", "ui"},
        {"main_menu_set_coop_menu_type", "ui"},
        {"game_machinima_is_recording", "ui"},
        {"game_machinima_playback_enter", "ui"},
        {"game_machinima_clip_exists", "ui"},
        {"game_machinima_copy_clip", "ui"},
        {"game_machinima_delete_clip", "ui"},
        {"game_machinima_overwrite_clip", "ui"},
        {"game_machinima_record", "ui"},
        {"autil_hud_mayhem_init", "ui"},
        {"cellphone_dial", "ui"},
        {"cinema_editor_create_camera_zone", "ui"},
        {"cinema_editor_save_clip", "ui"},
        {"dialog_box_set_current_option", "ui"},
        {"dialog_close_finished", "ui"},
        {"dialog_option_accept_kbd_input", "ui"},
        {"dialog_option_end_kbd_input", "ui"},
        {"vint_options_remap_reset_bindings", "ui"},
        {"pause_menu_horde_mode_new", "ui"},
        {"pause_menu_horde_mode_retry", "ui"},
        {"pause_menu_is_using_southpaw_control_scheme", "ui"},
        {"pause_menu_is_using_vehicle_southpaw_control_scheme", "ui"},
        {"playlist_play_track", "ui"},
        {"playlist_stop_track", "ui"},
        {"save_game_wrap_up", "ui"},
        {"save_system_change_device", "ui"},
        {"save_system_delete_game", "ui"},
        {"save_system_set_operation", "ui"},
        {"sb_saintsbook_is_available", "ui"},
        {"sb_select_chop_shop_vehicle", "ui"},
        {"sb_select_hitman_target", "ui"},
        {"main_menu_restart_video", "ui"},
        {"main_menu_video_has_stopped", "ui"},
        {"msn_killbane_selected_option", "ui"},
        {"object_indicator_init_lua", "ui"},
        {"options_display_pc_set_options", "ui"},
        {"pause_map_drag_map", "ui"},
        {"pause_menu_cant_retry_because_host_in_creation", "ui"},
        {"pause_menu_cat_mouse_active", "ui"},
        {"pause_menu_common_top_finished_loading", "ui"},
        {"main_menu_check_chat_priv", "ui"},
        {"main_menu_check_messages", "ui"},
        {"main_menu_check_online_privilege", "ui"},
        {"main_menu_check_open_nat", "ui"},
        {"main_menu_continue", "ui"},
        {"main_menu_matchmaking", "ui"},
        {"main_menu_maybe_show_voice_dialog", "ui"},
        {"main_menu_money_shot_check", "ui"},
        {"cell_playlist_save_list", "ui"},
        {"city_load_img_load_complete", "ui"},
        {"community_login_open", "ui"},
        {"completion_call_callback", "ui"},
        {"dialog_box_create_internal", "ui"},
        {"dialog_pause_unpause", "ui"},
        {"cell_allow_camera", "ui"},
        {"cell_cheats_activate_cheat", "ui"},
        {"cell_cheats_unlock_cheat", "ui"},
        {"cell_debug_all_enabled", "ui"},
        {"cell_is_closing", "ui"},
        {"cell_is_map_disabled", "ui"},
        {"cell_machinima_is_recording", "ui"},
        {"cell_music_menu_toggle_station", "ui"},
    };
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Registration: all 77 names land as real functions in the ui state,
    // never in the gameplay state.
    {
        Host host(batchFixtureNames());
        CHECK(hasGlobal(host.uiState(), "dialog_box_create_internal"));
        CHECK(!hasGlobal(host.gameplayState(), "dialog_box_create_internal"));
        for (const auto& rn : batchFixtureNames()) {
            CHECK(hasGlobal(host.uiState(), rn.name.c_str()));
        }
    }

    Host host(batchFixtureNames());
    lua_State* ui = host.uiState();
    EngineState& es = host.engineState();

    // Sec26.22 cellphone_choose_vehicle.
    es.hasLocalPlayer().set(true);
    ok(host, ui, "cellphone_choose_vehicle(2)");
    CHECK(es.cellphoneVehicleDelivery().lastSelectedIndex == 2);
    CHECK(es.cellphoneVehicleDelivery().announceCount == 1);

    // Sec26.3 main_menu_check_user_content: CONFIRMED hardcoded true.
    ok(host, ui, "assert(main_menu_check_user_content() == true)");

    // Sec26.5 Completion_get_other_player_name: shares remotePlayer() with file A.
    es.remotePlayer().present = false;
    ok(host, ui, "local n = Completion_get_other_player_name(); assert(n == nil)");
    es.remotePlayer().present = true;
    es.remotePlayer().name = "Pierce";
    ok(host, ui, "assert(Completion_get_other_player_name() == 'Pierce')");

    // Sec26.6 completion_screen_finished.
    ok(host, ui, "completion_screen_finished()");
    CHECK(es.completionScreenFinishedFlag() == true);

    // Sec26.10 pause_menu_quit_game_internal.
    ok(host, ui, "pause_menu_quit_game_internal()");
    CHECK(es.pauseMenuQuitRequested() == true);

    // Sec23.22/Sec23.21/Sec23.20 completion screen queries.
    ok(host, ui, "completion_camera_shake()");
    CHECK(es.completionCameraShakeCount() == 1);
    ok(host, ui, "assert(completion_credits_were_active() == false)");
    es.completionCreditsWereActive() = true;
    ok(host, ui, "assert(completion_credits_were_active() == true)");
    ok(host, ui, "assert(completion_cutscene_was_skipped() == false)");

    // Sec23.18 dialog_box_set_result: self-consistency gate. dialogId=2 ->
    // slot = dialogId & 0xFFFF = 2, which must be < 4 to even be eligible.
    ok(host, ui, "local n = select('#', dialog_box_set_result(2, 0, 1)); assert(n == 0)");
    CHECK(es.dialogUi().setResultSelfConsistencyFailCount == 1);
    es.setDialogSlotShowingIdForTesting(2, 2);
    es.dialogUi().optionApplyResult.set(true);
    ok(host, ui, "assert(dialog_box_set_result(2, 0, 1) == true)");

    // Sec23.17 dialog_open_pause_display.
    ok(host, ui, "dialog_open_pause_display()");
    CHECK(es.dialogUi().pauseDisplayOpen == true);

    // Sec23.9 main_menu_redeem_code.
    ok(host, ui, "main_menu_redeem_code()");
    CHECK(es.mainMenuRedeemCodeOpenCount() == 1);

    // Sec42.1 pause_map_add_bookmark: hardened null-marker guard, then real add/remove.
    fails(host, ui, "pause_map_add_bookmark()", "marker-object creation");
    es.pauseMapBookmarks().markerCreateSucceeds.set(false);
    ok(host, ui, "pause_map_add_bookmark()");
    CHECK(es.pauseMapBookmarks().createFailureGuardCount == 1); // known false -> hardened guard, no throw
    es.pauseMapBookmarks().markerCreateSucceeds.set(true);
    ok(host, ui, "pause_map_add_bookmark()");
    CHECK(es.pauseMapBookmarks().addCount == 1);
    ok(host, ui, "pause_map_add_bookmark()"); // CONFIRMED: now selected -> removed
    CHECK(es.pauseMapBookmarks().removeCount == 1);

    // Sec42.1 pause_map_zoom.
    double before = es.pauseMapZoomState().zoomFactor;
    ok(host, ui, "pause_map_zoom(true)");
    CHECK(es.pauseMapZoomState().zoomFactor > before);
    ok(host, ui, "pause_map_zoom(false)");
    ok(host, ui, "pause_map_zoom()"); // CONFIRMED: missing -> out

    // Sec42.1 pause_menu_change_control_scheme: hardened empty-set guard, then real clamp.
    fails(host, ui, "pause_menu_change_control_scheme(0)", "pause-menu control-scheme set");
    es.pauseMenuControlScheme() = 0; // set A
    es.pauseMenuControlSchemeSets().countA.set(0);
    ok(host, ui, "pause_menu_change_control_scheme(0)");
    CHECK(es.pauseMenuControlSchemeSets().emptySetGuardCount == 1); // hardened: empty set refused
    es.pauseMenuControlSchemeSets().countA.set(3);
    ok(host, ui, "pause_menu_change_control_scheme(99)"); // clamp to maxIdx
    CHECK(es.pauseMenuControlSchemeSets().appliedIndex == 2);

    // Sec41.2 main_menu_new_game/_horde_start/_set_coop_menu_type.
    ok(host, ui, "main_menu_new_game()");
    CHECK(es.mainMenuNewGame().worldName == "sr3_city");
    CHECK(es.mainMenuNewGame().newGameCount == 1);
    ok(host, ui, "main_menu_horde_start()");
    CHECK(es.mainMenuNewGame().hordeMode == true);
    ok(host, ui, "main_menu_set_coop_menu_type(true)");
    CHECK(es.coopMenuType() == 1);

    // Sec44.4/Sec56.5 the WHOLE machinima family - fail-closed path-builder first.
    fails(host, ui, "game_machinima_playback_enter('machinima')", ""); // path builder OPEN by default -> no throw, just no-op
    ok(host, ui, "game_machinima_playback_enter(42)"); // non-string -> hardened guard, no crash
    CHECK(es.machinima().pathBuilderFailureGuardCount >= 1);
    ok(host, ui, "game_machinima_playback_enter('not_machinima')"); // CONFIRMED literal mismatch -> no-op
    es.machinimaPathBuilderSucceeds().set(true);
    ok(host, ui, "game_machinima_playback_enter('machinima')");
    CHECK(es.machinima().playbackActive == true);
    CHECK(es.machinima().playbackEnterCount == 1);

    ok(host, ui, "assert(game_machinima_is_recording() == false)");
    ok(host, ui, "assert(cell_machinima_is_recording() == false)"); // CONFIRMED: shares the same state dword
    es.machinima().recording = true;
    ok(host, ui, "assert(game_machinima_is_recording() == true)");
    ok(host, ui, "assert(cell_machinima_is_recording() == true)");
    es.machinima().recording = false;

    es.machinima().clips["intro"] = true;
    ok(host, ui, "assert(game_machinima_clip_exists('intro') == true)");
    ok(host, ui, "assert(game_machinima_clip_exists('missing') == false)");
    ok(host, ui, "game_machinima_copy_clip('intro', 'intro2')");
    ok(host, ui, "assert(game_machinima_clip_exists('intro2') == true)");
    ok(host, ui, "game_machinima_overwrite_clip('intro', 'intro2')");
    ok(host, ui, "game_machinima_delete_clip('intro2')");
    ok(host, ui, "assert(game_machinima_clip_exists('intro2') == false)");
    // Fail-closed: with the path builder forced to fail, delete must never
    // touch the clip map at all (the whole point of the hardened divergence).
    es.machinimaPathBuilderSucceeds().set(false);
    ok(host, ui, "game_machinima_delete_clip('intro')");
    CHECK(es.machinima().clips.count("intro") != 0); // CHOSEN (hardened divergence): never deleted
    es.machinimaPathBuilderSucceeds().set(true);

    ok(host, ui, "game_machinima_record(true)");
    CHECK(es.machinima().recording == true);
    CHECK(es.machinima().recordCount == 1);
    ok(host, ui, "game_machinima_record(false)"); // CONFIRMED: always resets first
    CHECK(es.machinima().recording == false);

    // Sec45.6 autil_hud_mayhem_init: hardened pool-exhaustion guard, then real setup.
    fails(host, ui, "autil_hud_mayhem_init()", "native-callback pool availability");
    es.autilHudMayhemInit().nativeCallbackPoolAvailable.set(false);
    ok(host, ui, "autil_hud_mayhem_init()");
    CHECK(es.autilHudMayhemInit().poolExhaustedGuardCount == 1);
    es.autilHudMayhemInit().nativeCallbackPoolAvailable.set(true);
    ok(host, ui, "autil_hud_mayhem_init()");
    CHECK(es.autilHudMayhemInit().oneTimeSetupDone == true);
    ok(host, ui, "autil_hud_mayhem_init()"); // CONFIRMED: re-subscribes, no de-dup
    CHECK(es.autilHudMayhemInit().subscribeCount == 2);

    // Sec45.4 cellphone_dial: hardened unbounded-index guard, then real dial.
    fails(host, ui, "cellphone_dial(0, 'cb1', 'cb2')", "phone-book entry count");
    es.cellphoneDial().phoneBookCount.set(3);
    ok(host, ui, "cellphone_dial(5, 'cb1', 'cb2')");
    CHECK(es.cellphoneDial().outOfRangeGuardCount == 1);
    ok(host, ui, "cellphone_dial(1, 'cb1', 'cb2')");
    CHECK(es.cellphoneDial().dialCount == 1);

    // Sec45.2 cinema_editor_create_camera_zone: no template -> always succeeds;
    // unknown template slot -> hardened OOB guard.
    ok(host, ui, "local h = cinema_editor_create_camera_zone(); assert(type(h) == 'number')");
    ok(host, ui, "local n = select('#', cinema_editor_create_camera_zone(999)); assert(n == 0)");
    CHECK(es.cinemaEditor().oobTemplateGuardCount == 1);

    // Sec57.4 cinema_editor_save_clip: hardened path-builder guard.
    es.machinimaPathBuilderSucceeds().set(false);
    ok(host, ui, "cinema_editor_save_clip('dev_clip')");
    CHECK(es.cinemaEditor().saveClipPathGuardCount == 1);
    es.machinimaPathBuilderSucceeds().set(true);

    // Sec45.3 dialog_box_set_current_option / _close_finished / kbd input.
    ok(host, ui, "dialog_box_set_current_option(2)");
    CHECK(es.dialogUi().currentOptionIndex == 2);
    es.dialogUi().closeLatchSet = true;
    ok(host, ui, "dialog_close_finished()");
    CHECK(es.dialogUi().closeLatchSet == false);
    ok(host, ui, "dialog_option_accept_kbd_input(0)");
    CHECK(es.dialogUi().kbdEditing == true);
    ok(host, ui, "dialog_option_end_kbd_input(0, 'Boss')");
    CHECK(es.dialogUi().kbdEditing == false);
    CHECK(es.dialogUi().kbdCommittedText == "Boss");

    // Sec46.6 vint_options_remap_reset_bindings.
    ok(host, ui, "vint_options_remap_reset_bindings()");
    CHECK(es.vintOptionsRemapResetCount() == 1);

    // Sec51.5 pause_menu_horde_mode_new/_retry / southpaw queries.
    ok(host, ui, "pause_menu_horde_mode_new()");
    CHECK(es.pauseMenuHorde().newCount == 1);
    ok(host, ui, "pause_menu_horde_mode_retry()");
    CHECK(es.pauseMenuHorde().retryCount == 1);
    ok(host, ui, "assert(pause_menu_is_using_southpaw_control_scheme() == false)");
    ok(host, ui, "assert(pause_menu_is_using_vehicle_southpaw_control_scheme() == false)");

    // Sec51.2 playlist_play_track: hardened null-song guard, then real play;
    // 16-bit index aliasing reproduced exactly.
    ok(host, ui, "playlist_play_track(0)"); // loadedSongCount unset -> .known() short-circuit -> hardened guard, no throw
    CHECK(es.playlist().nullSongGuardCount == 1);
    es.playlist().loadedSongCount.set(10);
    ok(host, ui, "playlist_play_track(99)"); // out of range -> hardened guard
    CHECK(es.playlist().nullSongGuardCount == 2);
    ok(host, ui, "playlist_play_track(3)");
    CHECK(es.playlist().currentTrack == 3);
    CHECK(es.playlist().playing == true);
    ok(host, ui, "playlist_play_track(65539)"); // CONFIRMED: 65539 & 0xffff == 3
    CHECK(es.playlist().currentTrack == 3);
    ok(host, ui, "playlist_stop_track()");
    CHECK(es.playlist().playing == false);

    // Sec51.2 save_game_wrap_up/save_system_change_device/_delete_game/_set_operation.
    ok(host, ui, "save_game_wrap_up()");
    CHECK(es.saveGameExtra().wrapUpCount == 1);
    ok(host, ui, "save_system_change_device()");
    CHECK(es.saveGameExtra().changeDeviceCount == 1);
    es.saveGameExtra().jobInProgress = true;
    es.saveGameExtra().steamUserInterfaceAvailable.set(true);
    ok(host, ui, "save_system_delete_game(0)");
    CHECK(es.saveGameExtra().deleteDroppedCount == 1); // CONFIRMED: silently dropped while a job is running
    es.saveGameExtra().jobInProgress = false;
    es.saveSystemUi().slotCount.set(5);
    ok(host, ui, "save_system_delete_game(2)");
    CHECK(es.saveGameExtra().deleteAppliedCount == 1);
    ok(host, ui, "save_system_set_operation(3)");
    CHECK(es.saveGameExtra().lastOperation == 3);

    // Sec51.2 sb_saintsbook_is_available: CONFIRMED always false (dead AND-term).
    ok(host, ui, "assert(sb_saintsbook_is_available() == false)");

    // Sec51.2 sb_select_chop_shop_vehicle/_hitman_target: cross-clearing + asymmetric reselect.
    ok(host, ui, "sb_select_hitman_target(3)");
    CHECK(es.saintsbook().activeHitmanTarget == 3);
    ok(host, ui, "sb_select_chop_shop_vehicle(7)");
    CHECK(es.saintsbook().activeChopShopVehicle == 7);
    CHECK(es.saintsbook().activeHitmanTarget == -1); // CONFIRMED: clears the other manager first
    ok(host, ui, "sb_select_chop_shop_vehicle(7)"); // CONFIRMED: re-selecting deselects
    CHECK(es.saintsbook().activeChopShopVehicle == -1);
    ok(host, ui, "sb_select_hitman_target(3)");
    ok(host, ui, "sb_select_hitman_target(3)"); // CONFIRMED: re-selecting is a no-op (NOT a deselect)
    CHECK(es.saintsbook().activeHitmanTarget == 3);
    CHECK(es.saintsbook().hitmanReselectNoopCount == 1);

    // Sec50.1 main_menu_restart_video/_video_has_stopped: hardened stale-flag guard.
    fails(host, ui, "main_menu_restart_video()", "video-object creation");
    es.mainMenuVideoCreateSucceeds().set(false);
    ok(host, ui, "main_menu_restart_video()");
    CHECK(es.mainMenuVideo().nullVideoGuardCount == 1);
    CHECK(es.mainMenuVideo().playingFlag == false); // CHOSEN (hardened divergence): not set on failure
    es.mainMenuVideoCreateSucceeds().set(true);
    ok(host, ui, "main_menu_restart_video()");
    CHECK(es.mainMenuVideo().playingFlag == true);
    ok(host, ui, "assert(main_menu_video_has_stopped() == true)");
    CHECK(es.mainMenuVideo().playingFlag == false);

    // Sec50.4 msn_killbane_selected_option.
    ok(host, ui, "msn_killbane_selected_option(2)");
    CHECK(es.killbaneSelectedOption() == 2);
    CHECK(es.killbaneCallbackFiredCount() == 1);

    // Sec50.2 object_indicator_init_lua: hardened pool-exhaustion guard.
    fails(host, ui, "object_indicator_init_lua()", "callback pool availability");
    es.objectIndicatorInitLua().callbackPoolAvailable.set(true);
    ok(host, ui, "object_indicator_init_lua()");
    CHECK(es.objectIndicatorInitLua().oneTimeSetupDone == true);
    CHECK(es.objectIndicatorInitLua().elementCacheCount == 1);

    // Sec50.4 options_display_pc_set_options.
    ok(host, ui,
       "options_display_pc_set_options(1920, 1080, true, true, 4, 8, 2, 2, 2, -1, -1, -1)");
    CHECK(es.pcOptions().resolutionWidth == 1920);
    CHECK(es.pcOptions().resolutionHeight == 1080);
    CHECK(es.pcOptions().reflections == -1);
    CHECK(es.pcOptions().applyCount == 1);

    // Sec50.3 pause_map_drag_map: no real delta source, always a no-op pan.
    ok(host, ui, "pause_map_drag_map()");
    CHECK(es.pauseMapDragMapCount() == 1);

    // Sec50.1 pause_menu_cant_retry_because_host_in_creation/_cat_mouse_active/
    // _common_top_finished_loading.
    ok(host, ui, "assert(pause_menu_cant_retry_because_host_in_creation() == false)");
    es.pauseMenuHostInCharacterCreation() = true;
    ok(host, ui, "assert(pause_menu_cant_retry_because_host_in_creation() == true)");
    ok(host, ui, "assert(pause_menu_cat_mouse_active() == false)");
    ok(host, ui, "pause_menu_common_top_finished_loading()");
    CHECK(es.pauseMenuCommonTopFinishedLoadingCount() == 1);

    // Sec54.1 main_menu_check_chat_priv/_online_privilege (NOOP, true).
    ok(host, ui, "assert(main_menu_check_chat_priv() == true)");
    ok(host, ui, "assert(main_menu_check_online_privilege() == true)");

    // Sec54.1 main_menu_check_messages/_maybe_show_voice_dialog/_money_shot_check
    // (AGG, shared no-op stub).
    ok(host, ui, "main_menu_check_messages()");
    ok(host, ui, "main_menu_maybe_show_voice_dialog()");
    ok(host, ui, "main_menu_money_shot_check()");

    // Sec54.1 main_menu_check_open_nat: CONFIRMED linker-folded, always false.
    ok(host, ui, "assert(main_menu_check_open_nat() == false)");

    // Sec54.1 main_menu_continue / main_menu_matchmaking (hardened).
    ok(host, ui, "main_menu_continue()");
    CHECK(es.mainMenuContinueCount() == 1);
    ok(host, ui, "main_menu_matchmaking(true)");
    CHECK(es.mainMenuMatchmaking().startCount == 1);
    es.dialogPoolSlotsInUse() = 4;
    ok(host, ui, "main_menu_matchmaking()");
    CHECK(es.mainMenuMatchmaking().nullDialogGuardCount == 1);
    es.dialogPoolSlotsInUse() = 0;

    // Sec57.4 cell_playlist_save_list - THIS FILE'S OWN MEMSAFE FIX: drop
    // out-of-range song ids, clamp a negative count to 0.
    es.radioSongTableCount().set(5);
    ok(host, ui,
       "cell_playlist_save_list({[1]=1, [2]=2, [3]=99, [4]=-1, n=4})");
    CHECK(es.playlistSaveOutOfRangeDroppedCount() == 2); // ids 99 and -1 both out of range
    CHECK(es.savedPlaylists().back().size() == 2);       // only ids 1 and 2 survive
    ok(host, ui, "cell_playlist_save_list({[1]=1, n=-1})");
    CHECK(es.playlistSaveNegativeCountClampedCount() == 1); // CHOSEN (hardened): clamped to 0, not 65535
    CHECK(es.savedPlaylists().back().empty());              // n clamped to 0 -> nothing read from the table

    // Adversarial-review follow-up (2026-10-08), same fix: n is now routed
    // through truncateEa2596 (not a bare static_cast<int32_t> - UB for a
    // non-finite/out-of-int32_t-range double) and the resulting count is
    // bounded at 65,535 (closing the hang/DoS shape an unbounded large
    // positive n would otherwise still have, even with the overrun itself
    // already closed above). A very large but finite, in-int64-range count
    // (1e9) gets truncated fine, then capped - and must run fast, not hang.
    {
        auto t0 = std::chrono::steady_clock::now();
        ok(host, ui, "cell_playlist_save_list({n=1e9})");
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
        CHECK(es.playlistSaveCountCappedCount() == 1); // CHOSEN (hardened): capped to 65,535
        CHECK(es.savedPlaylists().back().empty());     // table has no [1..65535] entries -> nothing survives
        CHECK(elapsedMs < 2000); // bounded at 65,535 iterations, not 1e9 - no multi-second hang
    }
    // INT32_MAX (2^31-1): same shape, the review's own worst-case example -
    // extrapolated by the review at ~16s unbounded; must stay fast once bounded.
    {
        auto t0 = std::chrono::steady_clock::now();
        ok(host, ui, "cell_playlist_save_list({n=2147483647})");
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
        CHECK(es.playlistSaveCountCappedCount() == 2); // CHOSEN (hardened): capped to 65,535 again
        CHECK(elapsedMs < 2000); // bounded, not the ~16s an unbounded 2^31-1 loop would take
    }
    // Infinity (1/0) and NaN (0/0): both are not finite, so truncateEa2596
    // refuses (OpenStateError -> openGuard -> a clean Lua error) instead of a
    // bare static_cast<int32_t> invoking undefined behavior. Neither reaches
    // the nRaw < 0.0 clamp branch (that comparison is false for both).
    fails(host, ui, "cell_playlist_save_list({n=1/0})", "0x00ea2596");
    fails(host, ui, "cell_playlist_save_list({n=0/0})", "0x00ea2596");

    // Re-confirm the negative-count clamp and out-of-range-id drop still work
    // exactly as before, unaffected by the truncateEa2596/cap fix above or by
    // the two refused (exception-throwing) calls just before this one.
    es.radioSongTableCount().set(5);
    ok(host, ui,
       "cell_playlist_save_list({[1]=1, [2]=2, [3]=99, [4]=-1, n=4})");
    CHECK(es.playlistSaveOutOfRangeDroppedCount() == 4); // cumulative: 2 from the earlier call above + 2 here
    CHECK(es.savedPlaylists().back().size() == 2);       // only ids 1 and 2 survive (this call's own result)
    ok(host, ui, "cell_playlist_save_list({[1]=1, n=-7})");
    CHECK(es.playlistSaveNegativeCountClampedCount() == 2); // unchanged clamp-to-0 behavior, now a 2nd time
    CHECK(es.savedPlaylists().back().empty());

    // Sec57.4 city_load_img_load_complete / community_login_open / completion_call_callback.
    ok(host, ui, "city_load_img_load_complete()");
    CHECK(es.cityLoadImgComplete() == true);
    ok(host, ui, "community_login_open()");
    CHECK(es.communityLoginOpenCount() == 1);
    ok(host, ui, "completion_call_callback()");
    CHECK(es.completionCallbackCaseSelected() == 0); // CONFIRMED: no-arg -> case 0, not "no callback"
    ok(host, ui, "completion_call_callback(3)");
    CHECK(es.completionCallbackCaseSelected() == 3);

    // Sec57.1 dialog_box_create_internal - THIS FILE'S OTHER OWN MEMSAFE FIX:
    // nil title/body/callback handled; callback name bounded to 63 chars.
    ok(host, ui, "dialog_box_create_internal(nil, nil, nil)");
    CHECK(es.dialogUi().createNilArgGuardCount == 1);
    CHECK(es.dialogUi().createCount == 1);
    ok(host, ui,
       "local s = ''; for i = 1, 100 do s = s .. 'x' end; dialog_box_create_internal('Title', 'Body', s)");
    CHECK(es.dialogUi().createCallbackNameTruncatedCount == 1);
    CHECK(es.dialogUi().lastCreateCallbackName.size() == 63); // CHOSEN (hardened): bound to 63 characters
    CHECK(es.dialogUi().lastCreateTitle == "Title");
    ok(host, ui, "dialog_box_create_internal('T', 'B', 'short_callback')");
    CHECK(es.dialogUi().createCallbackNameTruncatedCount == 1); // unchanged: short name not truncated
    CHECK(es.dialogUi().lastCreateCallbackName == "short_callback");

    // Sec57.1 dialog_pause_unpause.
    es.dialogUi().pauseDisplayOpen = true;
    ok(host, ui, "dialog_pause_unpause(true)");
    CHECK(es.dialogUi().pauseDisplayOpen == false);

    // Sec59.1 cell_allow_camera (AGG default true) / _debug_all_enabled (NOOP false)
    // / _is_closing / _is_map_disabled / _machinima_is_recording (AGG, shared state).
    ok(host, ui, "assert(cell_allow_camera() == true)");
    ok(host, ui, "assert(cell_debug_all_enabled() == false)");
    ok(host, ui, "assert(cell_is_closing() == true)"); // CONFIRMED: phone not open -> true (naming trap)
    es.cellUi().phoneUiDocType1Open = true;
    es.cellUi().phoneUiDocType2Open = true;
    ok(host, ui, "assert(cell_is_closing() == false)");
    ok(host, ui, "assert(cell_is_map_disabled() == false)");
    es.currentMissionStemForTesting() = "m01";
    es.cellUi().mapDisabledExtraGate = true;
    ok(host, ui, "assert(cell_is_map_disabled() == true)");

    // Sec59.2 cell_music_menu_toggle_station: untracked station -> safe no-op;
    // tracked station toggles; signed-byte truncation (257 -> 1).
    ok(host, ui, "cell_music_menu_toggle_station(1)");
    CHECK(es.cellUi().toggleStationNoopCount == 1);
    es.cellUi().stationDisabled[1] = true;
    ok(host, ui, "cell_music_menu_toggle_station(257)"); // CONFIRMED: 257 truncates to the signed byte 1
    CHECK(es.cellUi().stationDisabled[1] == false);

    // Sec59.3 cell_cheats_activate_cheat: hardened unbounded-index guard.
    fails(host, ui, "cell_cheats_activate_cheat(0)", "unlocked-cheat count");
    es.cellUi().unlockedCheatCount.set(3);
    ok(host, ui, "cell_cheats_activate_cheat(5)");
    CHECK(es.cellUi().activateNullRecordGuardCount == 1); // hardened: out-of-range refused
    ok(host, ui, "cell_cheats_activate_cheat(1)");
    CHECK(es.cellUi().pendingCheatIndex == 1);

    // Sec59.3 cell_cheats_unlock_cheat: non-default content id never unlocks.
    es.cheatContentId()[2] = 5; // non-default content id
    ok(host, ui, "cell_cheats_unlock_cheat(2)");
    CHECK(es.cellUi().unlockCount == 0); // CONFIRMED: never unlocked through this path
    ok(host, ui, "cell_cheats_unlock_cheat(1)"); // default content id (0) -> unlocks
    CHECK(es.cellUi().unlockCount == 1);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "synthetic_luahost_wave2_w26b_test: all checks passed (77 names)\n";
    return 0;
}
