// lua_spec_confirmed_stubs_b18_wave2_ui_frontend_b_part2.cpp
//
// Second half of lua_spec_confirmed_stubs_b18_wave2_ui_frontend_b.cpp's stub functions, moved
// here unchanged to keep each file under ~1,500 lines (Team C refactor, 2026-10-17). The
// names/registration pair stays in that file; see its header comment for scope, sources
// and policy.

#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include <unordered_set>

namespace sr3luahost {
namespace stubs_b18 {

// ---------------------------------------------------------------------
// dialog_option_end_kbd_input (Sec45.3). Arguments: CHOSEN (option index
// number, committed text string default "") - the exact full arg shape
// is not individually detailed beyond "committed text re-encoded and
// delivered to a callback on end." No return. CONFIRMED cursor capped at
// 191 UTF-16 units - reproduced via truncation.
// ---------------------------------------------------------------------
int stub_dialog_option_end_kbd_input(lua_State* L) {
    logCall(L, upLog(L), "dialog_option_end_kbd_input", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1));
    std::string text = optionalStringDefault(L, 2, "");
    if (text.size() > 191) text.resize(191); // CONFIRMED: cursor capped at 191 UTF-16 units
    EngineState* st = upState(L);
    st->dialogUi().kbdEditing = false;
    st->dialogUi().kbdCursorPos = static_cast<int32_t>(text.size());
    st->dialogUi().kbdCommittedText = text;
    ++st->dialogUi().kbdEndCount;
    return 0;
}

// =======================================================================
// Sec46.6 vint_options_remap_reset_bindings
// =======================================================================

// ---------------------------------------------------------------------
// vint_options_remap_reset_bindings (Sec46.6, new one-name registrar
// 0x007cdc00). No arguments, no return. CONFIRMED re-applies two hard-
// coded default control-binding tables and rebuilds the key-in-use map -
// this project has no real binding-table data, so only the reset event
// itself is counted; the "unbounded action index"/"negative modifier
// accepted" data-gated shapes are not independently reachable here (no
// binding table to index into).
// ---------------------------------------------------------------------
int stub_vint_options_remap_reset_bindings(lua_State* L) {
    logCall(L, upLog(L), "vint_options_remap_reset_bindings", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->vintOptionsRemapResetCount();
    return 0;
}

// =======================================================================
// Sec51.5 pause_menu_horde_mode_new/_retry / southpaw queries
// =======================================================================

// ---------------------------------------------------------------------
// pause_menu_horde_mode_new (Sec51.5). No arguments, no return. CONFIRMED
// resets horde-mode state and broadcasts an opcode-0x44 record with no
// host gate at all (reused replicateStateChange() stand-in).
// ---------------------------------------------------------------------
int stub_pause_menu_horde_mode_new(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_horde_mode_new", upStateTag(L));
    EngineState::replicateStateChange("pause_menu_horde_mode_new", ""); // CONFIRMED: no host gate at all
    ++upState(L)->pauseMenuHorde().newCount;
    return 0;
}

// ---------------------------------------------------------------------
// pause_menu_horde_mode_retry (Sec51.5). No arguments, no return.
// CONFIRMED the same underlying mission-end routine as _new, but
// preserves 3 state dwords across the reset that _new discards (not
// individually modelled - only the distinct call path is counted).
// ---------------------------------------------------------------------
int stub_pause_menu_horde_mode_retry(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_horde_mode_retry", upStateTag(L));
    ++upState(L)->pauseMenuHorde().retryCount;
    return 0;
}

// ---------------------------------------------------------------------
// pause_menu_is_using_southpaw_control_scheme /
// _is_using_vehicle_southpaw_control_scheme (Sec51.5, the latter AGG -
// described collectively with its sibling). No arguments. Return: 1
// boolean each. CONFIRMED "simple bounds-safe input-binding-table
// queries, no crash shape" - this project tracks each as a plain
// test-settable flag (CHOSEN default false; the real underlying scheme-
// index mapping is OPEN, not guessed).
// ---------------------------------------------------------------------
int stub_pause_menu_is_using_southpaw_control_scheme(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_is_using_southpaw_control_scheme", upStateTag(L));
    lua_pushboolean(L, upState(L)->southpawControlScheme() ? 1 : 0);
    return 1;
}
int stub_pause_menu_is_using_vehicle_southpaw_control_scheme(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_is_using_vehicle_southpaw_control_scheme", upStateTag(L));
    lua_pushboolean(L, upState(L)->vehicleSouthpawControlScheme() ? 1 : 0);
    return 1;
}

// =======================================================================
// Sec51.2 playlist_play_track (CRASH - hardened) / playlist_stop_track (AGG)
// =======================================================================

// ---------------------------------------------------------------------
// playlist_play_track (Sec51.2; CRASH - hardened). Arguments: 1 number
// (track index). No return. CONFIRMED: an out-of-range index, a negative
// index, or any index while no songs are loaded makes the real song
// lookup return null, dereferenced unchecked; separately the index
// comparison only examines the low 16 bits (an index of 65536+k silently
// plays track k). CHOSEN (hardened divergence, Sec51.2): reproduces the
// 16-bit index aliasing exactly (a pure logic quirk, not itself unsafe),
// but refuses instead of dereferencing a null song lookup, counted.
// ---------------------------------------------------------------------
int stub_playlist_play_track(lua_State* L) {
    logCall(L, upLog(L), "playlist_play_track", upStateTag(L));
    int64_t raw = truncateEa2596(lua_tonumber(L, 1));
    int32_t aliased = static_cast<int32_t>(raw & 0xffff); // CONFIRMED: only the low 16 bits are examined
    EngineState* st = upState(L);
    auto& pl = st->playlist();
    bool ok = pl.loadedSongCount.known() && aliased >= 0 && aliased < static_cast<int32_t>(pl.loadedSongCount.get());
    if (!ok) {
        ++pl.nullSongGuardCount; // CHOSEN (hardened divergence): refuse instead of a null song-lookup dereference
        return 0;
    }
    pl.currentTrack = aliased;
    pl.playing = true;
    return 0;
}

// ---------------------------------------------------------------------
// playlist_stop_track (Sec51.2, AGG - described collectively with its 8
// siblings, not individually). No arguments, no return (CHOSEN - the
// natural counterpart of playlist_play_track).
// ---------------------------------------------------------------------
int stub_playlist_stop_track(lua_State* L) {
    logCall(L, upLog(L), "playlist_stop_track", upStateTag(L));
    EngineState* st = upState(L);
    st->playlist().playing = false;
    ++st->playlist().stopCount;
    return 0;
}

// =======================================================================
// Sec51.2 save_game_wrap_up / save_system_change_device /
// save_system_delete_game / save_system_set_operation
// =======================================================================

// ---------------------------------------------------------------------
// save_game_wrap_up (Sec51.2, AGG - described collectively, not
// individually). No arguments, no return.
// ---------------------------------------------------------------------
int stub_save_game_wrap_up(lua_State* L) {
    logCall(L, upLog(L), "save_game_wrap_up", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->saveGameExtra().wrapUpCount;
    return 0;
}

// ---------------------------------------------------------------------
// save_system_change_device (Sec51.2, AGG - described collectively, not
// individually). No arguments, no return.
// ---------------------------------------------------------------------
int stub_save_system_change_device(lua_State* L) {
    logCall(L, upLog(L), "save_system_change_device", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->saveGameExtra().changeDeviceCount;
    return 0;
}

// ---------------------------------------------------------------------
// save_system_delete_game (Sec51.2). Arguments: 1 number (save-slot
// index, CONFIRMED bounds-checked, no crash shape in the real engine).
// No return. CONFIRMED real silent-drop logic defect: a delete while
// another storage job is running, or with no Steam user interface
// available, is silently dropped (reproduced exactly, not a crash to
// harden).
// ---------------------------------------------------------------------
int stub_save_system_delete_game(lua_State* L) {
    logCall(L, upLog(L), "save_system_delete_game", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& extra = st->saveGameExtra();
    if (extra.jobInProgress || !extra.steamUserInterfaceAvailable.get()) { // OPEN until set
        ++extra.deleteDroppedCount; // CONFIRMED: silently dropped
        return 0;
    }
    uint32_t count = st->saveSystemUi().slotCount.get(); // OPEN until set; CONFIRMED correctly bounds-checked here
    if (slot < 0 || static_cast<uint32_t>(slot) >= count) return 0;
    ++extra.deleteAppliedCount;
    return 0;
}

// ---------------------------------------------------------------------
// save_system_set_operation (Sec51.2). Arguments: 1 number (an
// operation code). No return. CONFIRMED writes to a global nothing in
// the binary ever reads - a confirmed dead write, reproduced exactly (a
// plain, consequence-free store).
// ---------------------------------------------------------------------
int stub_save_system_set_operation(lua_State* L) {
    logCall(L, upLog(L), "save_system_set_operation", upStateTag(L));
    int64_t op = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->saveGameExtra().lastOperation = static_cast<int32_t>(op); // CONFIRMED: dead write, nothing reads it
    return 0;
}

// =======================================================================
// Sec51.2 sb_saintsbook_is_available / _select_chop_shop_vehicle /
// _select_hitman_target
// =======================================================================

// ---------------------------------------------------------------------
// sb_saintsbook_is_available (Sec51.2). No arguments. Return: 1 boolean.
// CONFIRMED its 3rd eligibility term reads a global NOTHING in the binary
// ever writes - permanently dead (confirmed by an exhaustive reference
// check), ANDed with the other 2 terms. Since a permanently-dead,
// never-written global holds its zero-filled default forever (this
// project's own standing "CONFIRMED zero-filled" convention for such
// globals, used throughout this document), the AND with that term makes
// this function's real-world result ALWAYS false, regardless of the
// other 2 (OPEN) terms - so this is reproduced as a genuine, confirmed
// hardcoded-false finding, not queried per-term.
// ---------------------------------------------------------------------
int stub_sb_saintsbook_is_available(lua_State* L) {
    logCall(L, upLog(L), "sb_saintsbook_is_available", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushboolean(L, 0); // CONFIRMED: the dead 3rd AND-term forces this always false
    return 1;
}

// ---------------------------------------------------------------------
// sb_select_chop_shop_vehicle (Sec51.2). Arguments: CHOSEN 1 number
// (vehicle index) - fully bounds-checked in the real engine (no real
// catalogue in this project to bound against). No return. CONFIRMED:
// selecting clears the hitman manager's own active contract first;
// re-selecting the CURRENT chop-shop vehicle DESELECTS it (a real,
// confirmed asymmetry with the hitman-target sibling below).
// ---------------------------------------------------------------------
int stub_sb_select_chop_shop_vehicle(lua_State* L) {
    logCall(L, upLog(L), "sb_select_chop_shop_vehicle", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& sb = st->saintsbook();
    if (idx == sb.activeChopShopVehicle) {
        sb.activeChopShopVehicle = -1; // CONFIRMED: re-selecting the current vehicle deselects it
        ++sb.chopShopDeselectCount;
        return 0;
    }
    sb.activeChopShopVehicle = static_cast<int32_t>(idx);
    sb.activeHitmanTarget = -1; // CONFIRMED: clears the other manager's active contract first
    return 0;
}

// ---------------------------------------------------------------------
// sb_select_hitman_target (Sec51.2). Arguments: CHOSEN 1 number (target
// index) - fully bounds-checked in the real engine. No return. CONFIRMED:
// selecting clears the chop-shop manager's own active contract first;
// re-selecting the CURRENT hitman target is simply a no-op (NOT a
// deselect - a confirmed asymmetry with the chop-shop sibling above).
// ---------------------------------------------------------------------
int stub_sb_select_hitman_target(lua_State* L) {
    logCall(L, upLog(L), "sb_select_hitman_target", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& sb = st->saintsbook();
    if (idx == sb.activeHitmanTarget) {
        ++sb.hitmanReselectNoopCount; // CONFIRMED: re-selecting the current target is simply a no-op
        return 0;
    }
    sb.activeHitmanTarget = static_cast<int32_t>(idx);
    sb.activeChopShopVehicle = -1; // CONFIRMED: clears the other manager's active contract first
    return 0;
}

// =======================================================================
// Sec50.1 main_menu_restart_video (CRASH - hardened) / _video_has_stopped
// =======================================================================

// ---------------------------------------------------------------------
// main_menu_restart_video (Sec50.1; CRASH - hardened). No arguments.
// CONFIRMED real null-video crash: recreates the main-menu background
// video; the creation call can return null, and the real engine stores
// that null and sets the "playing" flag anyway without checking - the
// NEXT call (to this or main_menu_video_has_stopped) then makes a virtual
// call through the null video object. CHOSEN (hardened divergence,
// Sec50.1) - the spec's OWN recommended fix: create first, only set the
// flag on success, counted when creation fails. Fires a "stop" hook
// before teardown and a "start" hook after the new video exists -
// CONFIRMED stop-then-start ordering even though both happen in one call.
// ---------------------------------------------------------------------
int stub_main_menu_restart_video(lua_State* L) {
    logCall(L, upLog(L), "main_menu_restart_video", upStateTag(L));
    EngineState* st = upState(L);
    auto& video = st->mainMenuVideo();
    ++video.stopHookCount; // CONFIRMED: stop hook fires before teardown
    if (!st->mainMenuVideoCreateSucceeds().get()) { // OPEN until set
        ++video.nullVideoGuardCount; // CHOSEN (hardened divergence): do NOT set playingFlag on a failed creation
        return 0;
    }
    video.playingFlag = true; // CONFIRMED: only set on success (the hardened fix)
    ++video.startHookCount;
    ++video.restartCount;
    return 0;
}

// ---------------------------------------------------------------------
// main_menu_video_has_stopped (Sec50.1). No arguments. Return: 1
// boolean. CONFIRMED the video-teardown half of the pair above.
// ---------------------------------------------------------------------
int stub_main_menu_video_has_stopped(lua_State* L) {
    logCall(L, upLog(L), "main_menu_video_has_stopped", upStateTag(L));
    EngineState* st = upState(L);
    bool wasPlaying = st->mainMenuVideo().playingFlag;
    st->mainMenuVideo().playingFlag = false;
    lua_pushboolean(L, wasPlaying ? 1 : 0);
    return 1;
}

// =======================================================================
// Sec50.4 msn_killbane_selected_option
// =======================================================================

// ---------------------------------------------------------------------
// msn_killbane_selected_option (Sec50.4). Arguments: CHOSEN 1 number (the
// player's chosen option). No return. CONFIRMED delivers the choice to
// whichever script function registered itself via the existing
// on_m21_player_choice global hook slot - this project does not
// implement Lua-callback invocation (established convention), so only the
// delivery event itself is counted.
// ---------------------------------------------------------------------
int stub_msn_killbane_selected_option(lua_State* L) {
    logCall(L, upLog(L), "msn_killbane_selected_option", upStateTag(L));
    int64_t option = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    st->killbaneSelectedOption() = static_cast<int32_t>(option);
    ++st->killbaneCallbackFiredCount();
    return 0;
}

// =======================================================================
// Sec50.2 object_indicator_init_lua
// =======================================================================

// ---------------------------------------------------------------------
// object_indicator_init_lua (Sec50.2). No arguments documented beyond its
// own shape. CONFIRMED a one-time-callback-plus-per-call-element-caching
// setup (the same two-phase shape Sec45.6's autil_hud_mayhem_init
// establishes for this family); 3 null-read shapes (no callback context,
// a missing document element, an exhausted callback pool), all shape-
// only with reachability HYPOTHESIS. CHOSEN simplification: all 3 fold
// into the one callbackPoolAvailable gate below (same convention as
// autilHudMayhemInit()).
// ---------------------------------------------------------------------
int stub_object_indicator_init_lua(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_init_lua", upStateTag(L));
    EngineState* st = upState(L);
    auto& oi = st->objectIndicatorInitLua();
    if (!oi.oneTimeSetupDone) {
        if (!oi.callbackPoolAvailable.get()) { // OPEN until set
            ++oi.poolExhaustedGuardCount; // CHOSEN (hardened divergence)
            return 0;
        }
        oi.oneTimeSetupDone = true;
    }
    ++oi.elementCacheCount;
    return 0;
}

// =======================================================================
// Sec50.4 options_display_pc_set_options
// =======================================================================

// ---------------------------------------------------------------------
// options_display_pc_set_options (Sec50.4). Arguments: the full 12-
// argument PC graphics-settings applier (resolution width/height, VSync,
// fullscreen, MSAA, anisotropic, lighting/shadow/scene detail,
// reflections, post-processing, ambient occlusion). No return. CONFIRMED
// reflections/post-processing/ambient-occlusion are stored with NO range
// check at all (an out-of-range value like 0 just stores -1); anisotropic
// and shadow detail are "safely bounded" instead - the real numeric
// bounds for the latter are not given by the available spec text, so
// this project stores every raw value as given rather than fabricate
// specific clamp bounds (a stated gap, not a guess).
// ---------------------------------------------------------------------
int stub_options_display_pc_set_options(lua_State* L) {
    logCall(L, upLog(L), "options_display_pc_set_options", upStateTag(L));
    EngineState* st = upState(L);
    auto& o = st->pcOptions();
    o.resolutionWidth = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 1)));
    o.resolutionHeight = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 2)));
    o.vsync = lua_toboolean(L, 3) != 0;
    o.fullscreen = lua_toboolean(L, 4) != 0;
    o.msaa = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 5)));
    o.anisotropic = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 6))); // CONFIRMED "safely bounded" - real bound not given
    o.lightingDetail = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 7)));
    o.shadowDetail = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 8))); // CONFIRMED "safely bounded" - real bound not given
    o.sceneDetail = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 9)));
    o.reflections = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 10))); // CONFIRMED stored raw, no range check
    o.postProcessing = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 11)));
    o.ambientOcclusion = static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 12)));
    ++o.applyCount;
    return 0;
}

// =======================================================================
// Sec50.3 pause_map_drag_map (CRASH - hardened)
// =======================================================================

// ---------------------------------------------------------------------
// pause_map_drag_map (Sec50.3; CRASH - hardened). No arguments, no
// return. CONFIRMED real uninitialized-local crash: the mouse-movement
// deltas are only read into locals when a specific input-flags bit is
// set; when clear, the locals are left completely uninitialized and the
// map pans by garbage. This project has no real raw-mouse-delta source
// at all - CHOSEN (hardened divergence, Sec50.3): the spec's OWN
// recommended fix ("zero the deltas unconditionally before this call") is
// exactly what this host already does by construction (no non-zero delta
// source exists here), counted for observability.
// ---------------------------------------------------------------------
int stub_pause_map_drag_map(lua_State* L) {
    logCall(L, upLog(L), "pause_map_drag_map", upStateTag(L));
    EngineState* st = upState(L);
    st->pauseMapZoomState().panX += 0.0; // CHOSEN (hardened divergence): no real delta source, always zero
    st->pauseMapZoomState().panY += 0.0;
    ++st->pauseMapDragMapCount();
    return 0;
}

// =======================================================================
// Sec50.1 pause_menu_cant_retry_because_host_in_creation /
// _cat_mouse_active / _common_top_finished_loading
// =======================================================================

int stub_pause_menu_cant_retry_because_host_in_creation(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_cant_retry_because_host_in_creation", upStateTag(L));
    lua_pushboolean(L, upState(L)->pauseMenuHostInCharacterCreation() ? 1 : 0);
    return 1;
}
int stub_pause_menu_cat_mouse_active(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_cat_mouse_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->catMouseMinigameActive() ? 1 : 0);
    return 1;
}
int stub_pause_menu_common_top_finished_loading(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_common_top_finished_loading", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->pauseMenuCommonTopFinishedLoadingCount();
    return 0;
}

// =======================================================================
// Sec54.1 main_menu_check_chat_priv/_online_privilege (NOOP, constant
// true) / _check_messages/_maybe_show_voice_dialog/_money_shot_check (AGG,
// shared no-op stub) / _check_open_nat (inert) / _continue /
// _matchmaking (CRASH - hardened)
// =======================================================================

// ---------------------------------------------------------------------
// main_menu_check_chat_priv / main_menu_check_online_privilege (Sec54.1).
// No arguments. Return: 1 boolean each. CONFIRMED both funnel through a
// shared helper (0x008703f0) that is not a platform privilege check at
// all - a plain "1 <= argument <= 8" range test with no platform call and
// no state, called with a constant argument that always satisfies it:
// always true.
// ---------------------------------------------------------------------
int stub_main_menu_check_chat_priv(lua_State* L) {
    logCall(L, upLog(L), "main_menu_check_chat_priv", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushboolean(L, 1); // CONFIRMED: hardcoded true
    return 1;
}
int stub_main_menu_check_online_privilege(lua_State* L) {
    logCall(L, upLog(L), "main_menu_check_online_privilege", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushboolean(L, 1); // CONFIRMED: hardcoded true
    return 1;
}

// ---------------------------------------------------------------------
// main_menu_check_messages / main_menu_maybe_show_voice_dialog /
// main_menu_money_shot_check (Sec54.1, AGG - "3 names are the shared
// no-op stub," not individually detailed beyond that). CONFIRMED genuine
// no-ops (shared stub 0x007c9f50): the shared prologue is made, nothing
// else happens, no push.
// ---------------------------------------------------------------------
int stub_main_menu_check_messages(lua_State* L) {
    logCall(L, upLog(L), "main_menu_check_messages", upStateTag(L));
    (void)lua_gettop(L);
    return 0;
}
int stub_main_menu_maybe_show_voice_dialog(lua_State* L) {
    logCall(L, upLog(L), "main_menu_maybe_show_voice_dialog", upStateTag(L));
    (void)lua_gettop(L);
    return 0;
}
int stub_main_menu_money_shot_check(lua_State* L) {
    logCall(L, upLog(L), "main_menu_money_shot_check", upStateTag(L));
    (void)lua_gettop(L);
    return 0;
}

// ---------------------------------------------------------------------
// main_menu_check_open_nat (Sec54.1). No arguments. Return: 1 boolean.
// CONFIRMED its own gating call is a linker-folded "return 0" body - it
// never does anything, regardless of what its name promises: always
// false.
// ---------------------------------------------------------------------
int stub_main_menu_check_open_nat(lua_State* L) {
    logCall(L, upLog(L), "main_menu_check_open_nat", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushboolean(L, 0); // CONFIRMED: linker-folded "return 0" body, always false
    return 1;
}

// ---------------------------------------------------------------------
// main_menu_continue (Sec54.1). No arguments documented beyond its own
// resolved shape. No return value mentioned - raises a continue/save-
// enumeration request handled by the main-menu's own per-frame update;
// this project has no per-frame update loop, so only the request itself
// is counted.
// ---------------------------------------------------------------------
int stub_main_menu_continue(lua_State* L) {
    logCall(L, upLog(L), "main_menu_continue", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->mainMenuContinueCount();
    return 0;
}

// ---------------------------------------------------------------------
// main_menu_matchmaking (Sec54.1; CRASH - hardened). Arguments: 1
// optional boolean. CONFIRMED real null-dialog-pool crash: each
// precondition-failure warning dialog is popped from the shared 4-slot
// dialog pool and used immediately with no null check - if all 4 slots
// are already in use, a write near address 0 results. CHOSEN (hardened
// divergence, Sec54.1): refuses instead when the shared pool
// (dialogPoolSlotsInUse(), also used by game_cancel_mission) is exhausted, counted.
// ---------------------------------------------------------------------
int stub_main_menu_matchmaking(lua_State* L) {
    logCall(L, upLog(L), "main_menu_matchmaking", upStateTag(L));
    bool arg = optionalBoolDefault(L, 1, false);
    (void)arg;
    EngineState* st = upState(L);
    ++st->mainMenuMatchmaking().startCount;
    if (st->dialogPoolSlotsInUse() >= 4) {
        ++st->mainMenuMatchmaking().nullDialogGuardCount; // CHOSEN (hardened divergence)
        return 0;
    }
    return 0;
}

// =======================================================================
// Sec57.4 cell_playlist_save_list - THIS BATCH'S SECOND OWN NAMED
// MEMORY-SAFETY FINDING (MEMSAFE)
// =======================================================================

// ---------------------------------------------------------------------
// cell_playlist_save_list (Sec57.4, 0x0059fb30-adjacent playlist
// registrar; MEMSAFE - this batch's own fix). Arguments: 1 table (track
// numbers) with an own "n" field (count). CONFIRMED the tranche's second
// headline memory-safety finding, two distinct crash shapes in the real
// engine: (1) track-number arguments used directly as indices into the
// radio song table with NO bound check (reachable anywhere within
// roughly +-640KB of the table); (2) a negative `n` field narrows to 16
// bits (so -1 becomes 65,535) and is used as an unbounded loop writing
// into a fixed 512-entry STACK array (overrunning the canary/return
// address). CHOSEN (hardened divergence, Sec57.6's own fix
// recommendation, applied verbatim): drop song ids outside the table's
// own range, and clamp a negative table count to 0 before use.
//
// Adversarial-review follow-up (2026-10-08), same fix, 2 more leftover
// issues closed: (1) the non-negative branch used to cast nRaw to
// int32_t directly via a bare static_cast - undefined behavior in C++
// for a double above INT32_MAX, infinity, or NaN (not something to rely
// on producing any particular iteration count). Now routed through this
// project's own standard Lua-number -> integer helper, truncateEa2596
// (lua_spec_confirmed_stubs_common.h, Sec4.1), the same way every other
// native in this project converts a Lua number to an integer; it refuses
// (OpenStateError, caught by openGuard) for a non-finite/out-of-int64-
// range nRaw rather than invoking undefined behavior. (2) a very large
// POSITIVE count (e.g. 2^31-1) is no longer a memory-safety issue once
// the negative-count clamp above closes the original overrun, but it was
// still an unbounded-loop hang/DoS shape - now capped at 65,535, matching
// the real engine's own 16-bit narrowing bug's accidental cap (CHOSEN:
// spec-lua-api-behaviour.md Sec57.4 does not give an exact bound for this
// field, so this is a labelled CHOSEN bound, not a CONFIRMED one).
// ---------------------------------------------------------------------
int stub_cell_playlist_save_list(lua_State* L) {
    logCall(L, upLog(L), "cell_playlist_save_list", upStateTag(L));
    EngineState* st = upState(L);
    std::vector<int32_t> survivors;
    if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TTABLE) {
        lua_getfield(L, 1, "n");
        bool hasN = lua_type(L, -1) == LUA_TNUMBER;
        double nRaw = hasN ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
        int32_t count;
        if (!hasN) {
            count = 0;
        } else if (nRaw < 0.0) {
            // CHOSEN (hardened divergence, Sec57.4/Sec57.6): clamp a negative count to 0 before use,
            // instead of narrowing it to 16 bits (the real bug: -1 -> 65,535, a 512-entry stack overrun).
            ++st->playlistSaveNegativeCountClampedCount();
            count = 0;
        } else {
            // CHOSEN (hardening, adversarial-review follow-up): truncateEa2596 instead of a bare
            // static_cast<int32_t>(nRaw) - see the function doc comment above.
            int64_t n64 = truncateEa2596(nRaw);
            if (n64 > 65535) {
                // CHOSEN (hardening, adversarial-review follow-up): bound a very large positive
                // count - see the function doc comment above.
                ++st->playlistSaveCountCappedCount();
                n64 = 65535;
            }
            count = static_cast<int32_t>(n64);
        }
        uint32_t tableCount = st->radioSongTableCount().get(); // OPEN until set
        for (int32_t i = 1; i <= count; ++i) {
            lua_pushnumber(L, i);
            lua_gettable(L, 1);
            if (lua_type(L, -1) == LUA_TNUMBER) {
                int64_t songId = truncateEa2596(lua_tonumber(L, -1));
                if (songId >= 0 && songId < static_cast<int64_t>(tableCount)) {
                    survivors.push_back(static_cast<int32_t>(songId));
                } else {
                    // CHOSEN (hardened divergence, Sec57.4/Sec57.6): drop song ids outside the table's own range,
                    // instead of indexing +-640KB around the table with no bound check.
                    ++st->playlistSaveOutOfRangeDroppedCount();
                }
            }
            lua_pop(L, 1);
        }
    }
    st->savedPlaylists().push_back(survivors);
    return 0;
}

// =======================================================================
// Sec57.4 city_load_img_load_complete / community_login_open /
// completion_call_callback
// =======================================================================

// ---------------------------------------------------------------------
// city_load_img_load_complete (Sec57.4). No arguments, no return.
// CONFIRMED sets a single gating byte with no crash shape, but a latent
// stall hazard if the calling script never fires it (this project has no
// polling loop to stall - only the gate transition is counted).
// ---------------------------------------------------------------------
int stub_city_load_img_load_complete(lua_State* L) {
    logCall(L, upLog(L), "city_load_img_load_complete", upStateTag(L));
    (void)lua_gettop(L);
    upState(L)->cityLoadImgComplete() = true;
    return 0;
}

// ---------------------------------------------------------------------
// community_login_open (Sec57.4). No arguments, no return. CONFIRMED
// resolves in full with no crash shape - a screen-open trigger, counted.
// ---------------------------------------------------------------------
int stub_community_login_open(lua_State* L) {
    logCall(L, upLog(L), "community_login_open", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->communityLoginOpenCount();
    return 0;
}

// ---------------------------------------------------------------------
// completion_call_callback (Sec57.4). Arguments: 1 optional number (a
// callback case selector). No return. CONFIRMED: calling it with NO
// argument selects callback case 0, NOT "no callback" (a real, confirmed
// quirk) - this project does not implement Lua-callback invocation
// (established convention), so only the selected case is tracked.
// ---------------------------------------------------------------------
int stub_completion_call_callback(lua_State* L) {
    logCall(L, upLog(L), "completion_call_callback", upStateTag(L));
    int32_t caseId = (lua_gettop(L) >= 1) ? static_cast<int32_t>(truncateEa2596(lua_tonumber(L, 1))) : 0; // CONFIRMED: no-arg -> case 0
    upState(L)->completionCallbackCaseSelected() = caseId;
    return 0;
}

// =======================================================================
// Sec57.1 dialog_box_create_internal - THIS BATCH'S OTHER OWN NAMED
// MEMORY-SAFETY FINDING (MEMSAFE) / dialog_pause_unpause
// =======================================================================

// ---------------------------------------------------------------------
// dialog_box_create_internal (Sec57.1; MEMSAFE - this batch's own fix).
// A 15-argument dialog builder - only the 3 hardening-relevant arguments
// (title, body, callback name) are individually modelled; the remaining
// up to 12 are not semantically given by the available spec text and are
// not read, per this project's conservative default (no invented return
// handle either - the spec text does not state a return shape). CONFIRMED
// the tranche's most severe memory-safety finding: the real engine copies
// the callback-name argument into a fixed 64-byte slot field with NO
// length limit (64+ characters overruns the field; 68+ corrupts the NEXT
// dialog slot's own ring-link fields or the first body-text pool block),
// and reads title/body/callback-name UNCONDITIONALLY with no null check.
// CHOSEN (hardened divergence, Sec57.1/Sec57.6, this batch's own fix
// recommendations, applied verbatim): bound the callback name to 63
// characters, and handle a nil title/body/callback (argString's own
// NULL-safe fallback already avoids the dereference; the counters below
// make that fact test-observable).
// ---------------------------------------------------------------------
int stub_dialog_box_create_internal(lua_State* L) {
    logCall(L, upLog(L), "dialog_box_create_internal", upStateTag(L));
    EngineState* st = upState(L);
    bool titleNil = lua_gettop(L) < 1 || lua_type(L, 1) == LUA_TNIL;
    bool bodyNil = lua_gettop(L) < 2 || lua_type(L, 2) == LUA_TNIL;
    bool callbackNil = lua_gettop(L) < 3 || lua_type(L, 3) == LUA_TNIL;
    if (titleNil || bodyNil || callbackNil) {
        ++st->dialogUi().createNilArgGuardCount; // CHOSEN (hardened divergence): handle nil instead of crashing
    }
    std::string title = argString(L, 1);    // NULL-safe: "" for nil/non-string
    std::string body = argString(L, 2);     // NULL-safe
    std::string callbackName = argString(L, 3); // NULL-safe
    if (callbackName.size() > 63) {
        callbackName.resize(63); // CHOSEN (hardened divergence): bound to 63 characters, this batch's own fix
        ++st->dialogUi().createCallbackNameTruncatedCount;
    }
    (void)body;
    st->dialogUi().lastCreateTitle = title;
    st->dialogUi().lastCreateCallbackName = callbackName;
    ++st->dialogUi().createCount;
    return 0; // CHOSEN: spec text does not state a return shape; no invented handle
}

// ---------------------------------------------------------------------
// dialog_pause_unpause (Sec57.1). Arguments: CHOSEN 1 optional boolean -
// the exact full arg shape is not individually detailed. No return.
// CONFIRMED hides a script-openable pause display (or cancels a still-
// pending deferred show) - both outcomes clear the open flag.
// ---------------------------------------------------------------------
int stub_dialog_pause_unpause(lua_State* L) {
    logCall(L, upLog(L), "dialog_pause_unpause", upStateTag(L));
    (void)optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    st->dialogUi().pauseDisplayOpen = false; // CONFIRMED: hides, or cancels a deferred show - either way, cleared
    ++st->dialogUi().pauseUnpauseCount;
    return 0;
}

// =======================================================================
// Sec59.1 cell_allow_camera (AGG) / _debug_all_enabled (NOOP) /
// _is_closing / _is_map_disabled / _machinima_is_recording (AGG) /
// Sec59.2 _music_menu_toggle_station / Sec59.3 cheat menu (CRASH - hardened)
// =======================================================================

// ---------------------------------------------------------------------
// cell_allow_camera (Sec59.1, AGG - one of "six simple boolean queries"
// described collectively, not individually). No arguments. Return: 1
// boolean. CHOSEN default true (allowed) - the real initial value is not
// individually given.
// ---------------------------------------------------------------------
int stub_cell_allow_camera(lua_State* L) {
    logCall(L, upLog(L), "cell_allow_camera", upStateTag(L));
    lua_pushboolean(L, upState(L)->cellUi().allowCamera ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// cell_debug_all_enabled (Sec59.1). No arguments. Return: 1 boolean.
// CONFIRMED the constant-false stub.
// ---------------------------------------------------------------------
int stub_cell_debug_all_enabled(lua_State* L) {
    logCall(L, upLog(L), "cell_debug_all_enabled", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushboolean(L, 0); // CONFIRMED: constant false
    return 1;
}

// ---------------------------------------------------------------------
// cell_is_closing (Sec59.1). No arguments. Return: 1 boolean. CONFIRMED
// reads false ONLY while two specific phone UI-document types are BOTH
// currently open - meaning it also reads true whenever the phone isn't
// open at all (a naming trap, reproduced exactly).
// ---------------------------------------------------------------------
int stub_cell_is_closing(lua_State* L) {
    logCall(L, upLog(L), "cell_is_closing", upStateTag(L));
    EngineState* st = upState(L);
    bool bothOpen = st->cellUi().phoneUiDocType1Open && st->cellUi().phoneUiDocType2Open;
    lua_pushboolean(L, bothOpen ? 0 : 1); // CONFIRMED: false only while both document types are open
    return 1;
}

// ---------------------------------------------------------------------
// cell_is_map_disabled (Sec59.1). No arguments. Return: 1 boolean.
// CONFIRMED true only during a hard-coded six-mission list (m01, m02,
// m16, m21, m24, dlc2_m02) AND one additional gating byte.
// ---------------------------------------------------------------------
int stub_cell_is_map_disabled(lua_State* L) {
    logCall(L, upLog(L), "cell_is_map_disabled", upStateTag(L));
    static const std::unordered_set<std::string> kDisabledMissions = {"m01", "m02", "m16", "m21", "m24", "dlc2_m02"};
    EngineState* st = upState(L);
    bool result = kDisabledMissions.count(st->currentMissionStemForTesting()) != 0 && st->cellUi().mapDisabledExtraGate;
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// cell_machinima_is_recording (Sec59.1, AGG - described collectively).
// No arguments. Return: 1 boolean. CONFIRMED reads the SAME state dword
// game_machinima_is_recording reads (0x029443bc == 1) - this project
// models both as the one shared machinima().recording flag.
// ---------------------------------------------------------------------
int stub_cell_machinima_is_recording(lua_State* L) {
    logCall(L, upLog(L), "cell_machinima_is_recording", upStateTag(L));
    lua_pushboolean(L, upState(L)->machinima().recording ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// cell_music_menu_toggle_station (Sec59.2, pointer-before-name one-name
// registrar). Arguments: 1 number (1-based station number). No return.
// CONFIRMED truncated to a SIGNED BYTE before use (257 behaves as 1, 255
// as -1); an out-of-range station number reads as "already disabled," so
// the computed toggle silently does nothing (a safe no-op, not a crash).
// Re-enabling the station the local player is currently listening to
// retunes their radio (no real audio pipeline to simulate further).
// ---------------------------------------------------------------------
int stub_cell_music_menu_toggle_station(lua_State* L) {
    logCall(L, upLog(L), "cell_music_menu_toggle_station", upStateTag(L));
    int64_t raw = truncateEa2596(lua_tonumber(L, 1));
    int8_t signedByte = static_cast<int8_t>(raw & 0xff); // CONFIRMED: truncated to a signed byte
    EngineState* st = upState(L);
    auto& disabledMap = st->cellUi().stationDisabled;
    auto it = disabledMap.find(signedByte);
    if (it == disabledMap.end()) {
        ++st->cellUi().toggleStationNoopCount; // CHOSEN: an untracked/out-of-range station reads as already-disabled
        return 0;
    }
    bool wasDisabled = it->second;
    it->second = !wasDisabled;
    if (wasDisabled && !it->second && st->cellUi().listeningStation == signedByte) {
        // CONFIRMED: re-enabling the currently-listened station retunes the radio - no audio pipeline to simulate further
    }
    return 0;
}

// ---------------------------------------------------------------------
// cell_cheats_activate_cheat (Sec59.3; CRASH - hardened). Arguments: 1
// number (cheat index). No return. CONFIRMED real null-pointer crash
// reachable from an unbounded script-supplied index - no bounds check
// anywhere on the activation path; the apply routine treats any index at
// or past the unlocked-cheat count (including negative, unsigned compare)
// as a null cheat record and dereferences it. Also: the pending index
// lives in one shared global a second call before the first dialog is
// answered silently redirects. CHOSEN (hardened divergence, Sec59.3): an
// out-of-range index refuses instead of dereferencing null, counted; the
// shared-global race is reproduced exactly (last write wins), since it is
// a logic quirk, not itself a memory-safety bug once the index is bounded.
// ---------------------------------------------------------------------
int stub_cell_cheats_activate_cheat(lua_State* L) {
    logCall(L, upLog(L), "cell_cheats_activate_cheat", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& cell = st->cellUi();
    uint32_t unlockedCount = cell.unlockedCheatCount.get(); // OPEN until set
    if (idx < 0 || static_cast<uint32_t>(idx) >= unlockedCount) {
        ++cell.activateNullRecordGuardCount; // CHOSEN (hardened divergence): refuse instead of a null cheat-record deref
        return 0;
    }
    cell.pendingCheatIndex = static_cast<int32_t>(idx); // CONFIRMED: shared global, reproduced (last call wins the race)
    return 0;
}

// ---------------------------------------------------------------------
// cell_cheats_unlock_cheat (Sec59.3). Arguments: 1 number (cheat index).
// No return. CONFIRMED logic defect: a cheat whose master record carries
// a non-default content id is NEVER unlocked through this path, even when
// that content is installed and enabled (the content gate is consulted
// only after a different, unconditional rejection test already excludes
// it) - reproduced exactly via the content-id map.
// ---------------------------------------------------------------------
int stub_cell_cheats_unlock_cheat(lua_State* L) {
    logCall(L, upLog(L), "cell_cheats_unlock_cheat", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& contentIds = st->cheatContentId();
    auto it = contentIds.find(static_cast<int32_t>(idx));
    int32_t contentId = (it != contentIds.end()) ? it->second : 0;
    if (contentId != 0) return 0; // CONFIRMED: non-default content id -> never unlocked through this path
    ++st->cellUi().unlockCount;
    return 0;
}

} // namespace stubs_b18
} // namespace sr3luahost
