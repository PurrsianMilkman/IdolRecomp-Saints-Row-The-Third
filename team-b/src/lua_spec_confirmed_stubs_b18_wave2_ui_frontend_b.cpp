// lua_spec_confirmed_stubs_b18_wave2_ui_frontend_b.cpp
//
// Wave 2 batch W2-6 (2026-10-08), "UI frontend (143 natives) - HARDENING
// BATCH" - split B of 2 (file A: lua_spec_confirmed_stubs_b17_wave2_ui_
// frontend_a.cpp). 77 names: menus (main_menu_*/pause_menu_*/pause_map_*/
// options_display_pc_set_options/vint_options_remap_reset_bindings/
// object_indicator_init_lua/msn_killbane_selected_option/sb_*/autil_hud_
// mayhem_init/community_login_open), dialog (dialog_*), cellphone
// (cellphone_*), cell (cell_*), save (save_system_*/save_game_*), machinima
// (game_machinima_*, cinema_editor_*, cell_machinima_is_recording - the
// WHOLE family, one owner, per the orchestrator's own directive), and
// completion (completion_*/Completion_*/completion_call_callback). See
// lua_spec_confirmed_stubs_b17_wave2_ui_frontend_a.cpp's own top comment
// for the exact split rule this batch's own author chose and the full
// per-name rationale.
//
// CLEAN-ROOM: every behavior below comes from spec-lua-api-behaviour.md
// (sections cited per stub) - never the game executable or any
// disassembly (Team A's exclusive domain).
//
// THIS FILE OWNS THE BATCH'S SECOND NAMED MEMORY-SAFETY FINDING,
// `cell_playlist_save_list` (the first, `dialog_box_create_internal`, is
// also in this file): drop song ids outside the table's range, and clamp
// a negative table count to 0 before use - both this batch's own §57 fix
// recommendations, applied exactly as directed.
//
// THIS FILE OWNS THE WHOLE MACHINIMA FAMILY (game_machinima_is_recording/
// _playback_enter/_record/_copy_clip/_delete_clip/_overwrite_clip/
// _clip_exists, cell_machinima_is_recording, cinema_editor_create_camera_
// zone, cinema_editor_save_clip): they share a path-builder defect (the
// real builder leaves its output buffer unwritten on all 3 failure exits,
// and no caller checks) - EngineState::buildMachinimaClipPath()
// (engine_state.h/lua_engine_state.cpp) is the ONE shared, hardened
// stand-in every one of these stubs calls, so the family fails closed as
// a whole rather than each stub reinventing its own guard.
//
// Hardening counters (`...GuardCount`) are test-observable only, not real
// engine fields - see engine_state.h's own "Wave 2 batch W2-6" section for
// each one's full citation.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

#include <unordered_set>

namespace sr3luahost {
namespace {

// =======================================================================
// Sec26.22 cellphone_choose_vehicle
// =======================================================================

// ---------------------------------------------------------------------
// cellphone_choose_vehicle (Sec26.22, 0x0083d5e0). Arguments: 1 mandatory
// number (an index into the cellphone vehicle-delivery list). No return.
// HIGH CONFIDENCE (summarized per house style, largest/most branchy of
// its own batch): selects, spawns, and announces delivery of the chosen
// vehicle. This project has no real per-entry delivery-list data (no
// readiness bit, no permission predicate) - only the index selection and
// the "if a local player exists, announce" step are modelled; the
// permission/spawn-callee internals (0x00ac1840/0x00bc1250/0x005eebd0)
// stay OPEN, not guessed.
// ---------------------------------------------------------------------
int stub_cellphone_choose_vehicle(lua_State* L) {
    logCall(L, upLog(L), "cellphone_choose_vehicle", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    st->cellphoneVehicleDelivery().lastSelectedIndex = static_cast<int32_t>(idx);
    ++st->cellphoneVehicleDelivery().selectCount;
    if (st->hasLocalPlayer().get()) { // OPEN until set; CONFIRMED: "if the current player exists..."
        ++st->cellphoneVehicleDelivery().announceCount;
    }
    return 0;
}

// =======================================================================
// Sec26.3 main_menu_check_user_content
// =======================================================================

// ---------------------------------------------------------------------
// main_menu_check_user_content (Sec26.3, 0x007c9e30). No arguments.
// Return: 1 boolean. CONFIRMED a genuine hardcoded-true stub - calls a
// range-check helper whose literal argument (4) always falls inside its
// valid range, so this always returns true unconditionally.
// ---------------------------------------------------------------------
int stub_main_menu_check_user_content(lua_State* L) {
    logCall(L, upLog(L), "main_menu_check_user_content", upStateTag(L));
    (void)lua_gettop(L);
    lua_pushboolean(L, 1); // CONFIRMED: hardcoded, always true
    return 1;
}

// =======================================================================
// Sec26.5 Completion_get_other_player_name
// =======================================================================

// ---------------------------------------------------------------------
// Completion_get_other_player_name (Sec26.5, 0x007bfc70). No arguments.
// Return: 0 values if no session/no matching member found, otherwise 1
// string. CONFIRMED reads the SAME +0xc2 name field game_lobby_get_
// remote_player_name (file A) reads via a different context object -
// this project models both as the one shared remotePlayer() record.
// ---------------------------------------------------------------------
int stub_Completion_get_other_player_name(lua_State* L) {
    logCall(L, upLog(L), "Completion_get_other_player_name", upStateTag(L));
    EngineState* st = upState(L);
    if (!st->remotePlayer().present) return 0; // CONFIRMED: no match -> no push
    lua_pushstring(L, st->remotePlayer().name.c_str());
    return 1;
}

// =======================================================================
// Sec26.6 completion_screen_finished
// =======================================================================

// ---------------------------------------------------------------------
// completion_screen_finished (Sec26.6, 0x007c03f0). No arguments, no
// return. CONFIRMED sets a "completion screen finished" flag and requests
// a screen fade - reuses the exact same screenFadeRequest()/Sec26.24
// mechanism game_lobby_coop_finished (b02) already calls with the
// identical (-1, null, 1) shape.
// ---------------------------------------------------------------------
int stub_completion_screen_finished(lua_State* L) {
    logCall(L, upLog(L), "completion_screen_finished", upStateTag(L));
    EngineState* st = upState(L);
    st->completionScreenFinishedFlag() = true;
    st->screenFadeRequest(true, -1, nullptr, 1); // OPEN if the fade state itself is unset - same shape as game_lobby_coop_finished
    return 0;
}

// =======================================================================
// Sec26.10 pause_menu_quit_game_internal
// =======================================================================

// ---------------------------------------------------------------------
// pause_menu_quit_game_internal (Sec26.10, 0x007e0470). No arguments, no
// return. CONFIRMED requests a zero-millisecond fade-out (flag 0) with a
// real completion callback that (once the fade completes) sets a
// shutdown-request flag and tail-jumps into a shared shutdown path. This
// project does not model the deferred-callback chain itself (0x00554270/
// 0x00877410/0x00707880 are OPEN) - only the flag and the fade request.
// ---------------------------------------------------------------------
int stub_pause_menu_quit_game_internal(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_quit_game_internal", upStateTag(L));
    EngineState* st = upState(L);
    st->screenFadeRequest(true, 0, nullptr, 0); // CONFIRMED: zero-ms fade-out, flag 0
    st->pauseMenuQuitRequested() = true;
    return 0;
}

// =======================================================================
// Sec23.22/Sec23.21/Sec23.20 completion screen queries
// =======================================================================

// ---------------------------------------------------------------------
// completion_camera_shake (Sec23.22, 0x007bdc50). No arguments. No
// return value mentioned beyond the side effect. CONFIRMED a single,
// fixed (non-parameterized) camera-shake trigger tied to the completion-
// screen camera-shake handle.
// ---------------------------------------------------------------------
int stub_completion_camera_shake(lua_State* L) {
    logCall(L, upLog(L), "completion_camera_shake", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->completionCameraShakeCount();
    return 0;
}

// ---------------------------------------------------------------------
// completion_credits_were_active (Sec23.21, 0x007bdd20). No arguments.
// Return: 1 boolean. CONFIRMED pure query, forwards to a zero-arg
// callee - tracked here as a plain test-settable flag (CHOSEN default
// false, same zero-filled convention this document uses throughout).
// ---------------------------------------------------------------------
int stub_completion_credits_were_active(lua_State* L) {
    logCall(L, upLog(L), "completion_credits_were_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->completionCreditsWereActive() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// completion_cutscene_was_skipped (Sec23.20, 0x007bdcb0). No arguments.
// Return: 1 boolean. CONFIRMED pure query, forwards to a zero-arg callee
// (structural sibling of the above) - CHOSEN default false.
// ---------------------------------------------------------------------
int stub_completion_cutscene_was_skipped(lua_State* L) {
    logCall(L, upLog(L), "completion_cutscene_was_skipped", upStateTag(L));
    lua_pushboolean(L, upState(L)->completionCutsceneWasSkipped() ? 1 : 0);
    return 1;
}

// =======================================================================
// Sec23.18/Sec23.17 dialog self-consistency gate / pause display
// =======================================================================

// ---------------------------------------------------------------------
// dialog_box_set_result (Sec23.18, 0x007c2d90). Arguments: arg1 mandatory
// number (a dialog-type/slot id); arg2 optional number, default 0 (an
// option index); arg3 mandatory number (always read regardless of arg2's
// presence). Body: CONFIRMED validates arg1 - non-zero, (id & 0xFFFF) < 4,
// AND the slot indexed by (id & 0xFFFF) currently shows exactly this id
// (self-consistency) - only then applies the result, pushing its boolean;
// otherwise 0 Lua values are returned (an implicit failure signal, NOT a
// crash - no pointer is dereferenced either way in this project's model).
// ---------------------------------------------------------------------
int stub_dialog_box_set_result(lua_State* L) {
    logCall(L, upLog(L), "dialog_box_set_result", upStateTag(L));
    int64_t dialogId = truncateEa2596(lua_tonumber(L, 1));
    double optionIndex = optionalNumberDefault(L, 2, 0.0);
    double resultValue = lua_tonumber(L, 3); // CONFIRMED: always read regardless of arg2's presence
    (void)optionIndex;
    (void)resultValue;
    EngineState* st = upState(L);
    int32_t slot = static_cast<int32_t>(dialogId & 0xFFFF);
    bool valid = dialogId != 0 && slot >= 0 && slot < 4 && st->dialogUi().slotShowingId[static_cast<size_t>(slot)] == dialogId;
    if (!valid) {
        ++st->dialogUi().setResultSelfConsistencyFailCount; // CONFIRMED: no push at all
        return 0;
    }
    bool result = st->dialogUi().optionApplyResult.get(); // OPEN until set - 0x007c1c80's own result
    lua_pushboolean(L, result ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// dialog_open_pause_display (Sec23.17, 0x007c2ee0). Arguments: 1 optional
// boolean, default false. No return. CONFIRMED checks the SAME small
// session/pause-state accessor dialog_box_disconnect (already
// implemented) reads; if the result isn't exactly 2, opens the pause
// dialog display. CHOSEN simplification: this project's dialog_box_
// disconnect implementation only counts calls, never derives that state
// code, and neither branch here has any OTHER Lua-visible effect (no
// return value either way) - so this host always takes the "open" path,
// documented as a stated simplification rather than a guessed engine
// mechanism.
// ---------------------------------------------------------------------
int stub_dialog_open_pause_display(lua_State* L) {
    logCall(L, upLog(L), "dialog_open_pause_display", upStateTag(L));
    bool arg1 = optionalBoolDefault(L, 1, false);
    (void)arg1;
    EngineState* st = upState(L);
    st->dialogUi().pauseDisplayOpen = true;
    ++st->dialogUi().pauseDisplayOpenCount;
    return 0;
}

// =======================================================================
// Sec23.9 main_menu_redeem_code
// =======================================================================

// ---------------------------------------------------------------------
// main_menu_redeem_code (Sec23.9, 0x007c9d80). No arguments, no return.
// CONFIRMED a single forwarding call that opens the redeem-code entry
// screen - a 2-instruction body past the shared prologue.
// ---------------------------------------------------------------------
int stub_main_menu_redeem_code(lua_State* L) {
    logCall(L, upLog(L), "main_menu_redeem_code", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->mainMenuRedeemCodeOpenCount();
    return 0;
}

// =======================================================================
// Sec42.1 pause_map_add_bookmark / pause_map_zoom /
// pause_menu_change_control_scheme (2 CRASH - hardened)
// =======================================================================

// ---------------------------------------------------------------------
// pause_map_add_bookmark (Sec42.1, 0x007df8a0; CRASH - hardened). No
// arguments, no return. CONFIRMED toggle: a currently-selected bookmark
// is removed; otherwise, with fewer than 5 bookmarks, a new one is added
// at the cursor's position. Real engine: the add path's marker-object
// creation can fail (pool exhaustion) and is dereferenced unchecked; the
// remove path can dereference an already-destroyed marker object (the
// "local ownership" test treats a null object as locally-owned). CHOSEN
// (hardened divergence, Sec42.1): both paths refuse instead of
// dereferencing a null/stale marker, each counted.
// ---------------------------------------------------------------------
int stub_pause_map_add_bookmark(lua_State* L) {
    logCall(L, upLog(L), "pause_map_add_bookmark", upStateTag(L));
    EngineState* st = upState(L);
    auto& bm = st->pauseMapBookmarks();
    if (bm.selectedBookmark >= 0) {
        bm.selectedBookmark = -1; // CONFIRMED: a selected bookmark is removed
        ++bm.removeCount;
        return 0;
    }
    if (bm.bookmarkPositions.size() >= 5) return 0; // CONFIRMED: capacity exactly 5
    if (!bm.markerCreateSucceeds.get()) { // OPEN until set
        ++bm.createFailureGuardCount; // CHOSEN (hardened divergence): refuse instead of a null marker-object dereference
        return 0;
    }
    bm.bookmarkPositions.push_back(st->pauseMapZoomState().panX); // CHOSEN stand-in position value
    bm.selectedBookmark = static_cast<int32_t>(bm.bookmarkPositions.size()) - 1;
    ++bm.addCount;
    return 0;
}

// ---------------------------------------------------------------------
// pause_map_zoom (Sec42.1, 0x007dbcc0). Arguments: 1 boolean (true=in,
// false/missing=out - CONFIRMED missing counts as out, not a no-op). No
// return. Body: CONFIRMED applies a +-20% multiplicative step (clamped to
// bounds), then recomputes pan by DIVIDING by the new zoom factor -
// HYPOTHESIS edge case (not crash-shaped, "just numerically degenerate"):
// an unset zoom factor of 0 produces inf/NaN via ordinary IEEE float
// division, reproduced naturally here with no special-case guard (this
// is NOT CRASH-tagged for this native - a degenerate float result is not
// a memory-safety bug).
// ---------------------------------------------------------------------
int stub_pause_map_zoom(lua_State* L) {
    logCall(L, upLog(L), "pause_map_zoom", upStateTag(L));
    bool zoomIn = optionalBoolDefault(L, 1, false); // CONFIRMED: missing -> out
    EngineState* st = upState(L);
    auto& z = st->pauseMapZoomState();
    double step = zoomIn ? 1.2 : (1.0 / 1.2); // CONFIRMED +-20% multiplicative step
    double newZoom = z.zoomFactor * step;
    if (newZoom < z.zoomMin) newZoom = z.zoomMin;
    if (newZoom > z.zoomMax) newZoom = z.zoomMax;
    z.zoomFactor = newZoom;
    z.panX = z.panX / z.zoomFactor; // CONFIRMED: recompute by division; IEEE semantics handle the zero case naturally
    z.panY = z.panY / z.zoomFactor;
    ++z.zoomCount;
    return 0;
}

// ---------------------------------------------------------------------
// pause_menu_change_control_scheme (Sec42.1, 0x007e10d0; CRASH -
// hardened). Arguments: 1 number (control-scheme index). No return.
// CONFIRMED selects one of two fixed scheme sets (selector 0 -> set A,
// anything else -> set B, reusing the existing pauseMenuControlScheme()
// selector pause_menu_control_scheme_init already writes), clamps the
// index to the set's own count. Real engine: an empty scheme set (count
// 0) computes clampedMax = -1 and clamps the index DOWN to -1, reading
// the fixed-size scheme record immediately BEFORE the array base. CHOSEN
// (hardened divergence, Sec42.1): an empty set refuses instead, counted.
// ---------------------------------------------------------------------
int stub_pause_menu_change_control_scheme(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_change_control_scheme", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    bool setA = (st->pauseMenuControlScheme() == 0); // CONFIRMED mapping
    auto& sets = st->pauseMenuControlSchemeSets();
    int32_t count = setA ? sets.countA.get() : sets.countB.get(); // OPEN until set
    int32_t maxIdx = count - 1;
    if (maxIdx < 0) {
        ++sets.emptySetGuardCount; // CHOSEN (hardened divergence): refuse instead of reading before the array base
        return 0;
    }
    int32_t clamped = static_cast<int32_t>(idx < 0 ? 0 : (idx > maxIdx ? maxIdx : idx));
    sets.appliedIndex = clamped;
    return 0;
}

// =======================================================================
// Sec41.2 main_menu_new_game / _horde_start / _set_coop_menu_type
// =======================================================================

// ---------------------------------------------------------------------
// main_menu_new_game (Sec41.2, 0x007c9d20) / main_menu_horde_start
// (Sec41.2, 0x007c9d60). No arguments, no return. CONFIRMED: both funnel
// into the identical new-game routine (hard-codes the world name
// "sr3_city"); horde_start additionally sets a horde-mode flag first -
// the only confirmed difference at this level.
// ---------------------------------------------------------------------
int stub_main_menu_new_game(lua_State* L) {
    logCall(L, upLog(L), "main_menu_new_game", upStateTag(L));
    (void)lua_gettop(L);
    ++upState(L)->mainMenuNewGame().newGameCount;
    return 0;
}
int stub_main_menu_horde_start(lua_State* L) {
    logCall(L, upLog(L), "main_menu_horde_start", upStateTag(L));
    (void)lua_gettop(L);
    EngineState* st = upState(L);
    st->mainMenuNewGame().hordeMode = true; // CONFIRMED: sets the horde-mode flag before falling into the same routine
    ++st->mainMenuNewGame().hordeStartCount;
    return 0;
}

// ---------------------------------------------------------------------
// main_menu_set_coop_menu_type (Sec41.2, 0x007c9ed0). Arguments: 1
// boolean. No return. CONFIRMED stores it as a byte; the sole reader
// selects between two further UI dispatch codes (10 vs 11, meanings
// OPEN) - only the raw stored value is modelled.
// ---------------------------------------------------------------------
int stub_main_menu_set_coop_menu_type(lua_State* L) {
    logCall(L, upLog(L), "main_menu_set_coop_menu_type", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0;
    upState(L)->coopMenuType() = flag ? 1 : 0;
    return 0;
}

// =======================================================================
// Sec44.4/Sec56.5 the machinima family (ALL of it, one owner - see this
// file's own top comment) - every stub below calls the shared, hardened
// EngineState::buildMachinimaClipPath() path-builder stand-in.
// =======================================================================

// ---------------------------------------------------------------------
// game_machinima_is_recording (Sec44.4, 0x00bc5860). No arguments.
// Return: 1 boolean. CONFIRMED simple state-dword read.
// ---------------------------------------------------------------------
int stub_game_machinima_is_recording(lua_State* L) {
    logCall(L, upLog(L), "game_machinima_is_recording", upStateTag(L));
    lua_pushboolean(L, upState(L)->machinima().recording ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// game_machinima_playback_enter (Sec44.4, 0x00bc67d0; CRASH - hardened).
// Arguments: 1 string, CONFIRMED compared inline against the literal
// "machinima" with NO null check in the real engine. Real engine ALSO has
// an uninitialized-buffer crash (a plain string not produced by the
// matching UI encoder leaves a 520-byte stack buffer uninitialized, fed
// to _swprintf_s as %s) and clip-loader resource-handling defects (handle
// leak, unchecked ReadFile, size underflow, unchecked allocation). CHOSEN
// (hardened divergence, Sec44.4): a non-string argument and any path-
// builder failure both fail closed via the shared buildMachinimaClipPath()
// stand-in, counted there.
// ---------------------------------------------------------------------
int stub_game_machinima_playback_enter(lua_State* L) {
    logCall(L, upLog(L), "game_machinima_playback_enter", upStateTag(L));
    EngineState* st = upState(L);
    if (lua_gettop(L) < 1 || lua_type(L, 1) != LUA_TSTRING) {
        ++st->machinima().pathBuilderFailureGuardCount; // CHOSEN (hardened divergence): non-string arg never dereferenced
        return 0;
    }
    std::string arg1 = argString(L, 1);
    if (arg1 != "machinima") return 0; // CONFIRMED literal comparison; not equal -> no-op
    auto path = st->buildMachinimaClipPath(arg1); // shared hardened path-builder
    if (!path.ok) return 0; // fail closed, counted inside buildMachinimaClipPath
    st->machinima().playbackActive = true;
    ++st->machinima().playbackEnterCount;
    return 0;
}

// ---------------------------------------------------------------------
// game_machinima_clip_exists (Sec56.5, AGG - described collectively with
// its 4 siblings, not individually). Arguments: 1 string (clip name).
// Return: 1 boolean.
// ---------------------------------------------------------------------
int stub_game_machinima_clip_exists(lua_State* L) {
    logCall(L, upLog(L), "game_machinima_clip_exists", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    auto path = st->buildMachinimaClipPath(name);
    bool exists = path.ok && st->machinima().clips.count(name) != 0;
    lua_pushboolean(L, exists ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// game_machinima_copy_clip (Sec56.5). Arguments: 2 strings (source, dest
// clip names). No return - CONFIRMED the real CopyFileW result is
// discarded, so the script cannot detect a failed copy (reproduced: no
// push either way). Both names go through the shared hardened path-
// builder (fail closed on either failure).
// ---------------------------------------------------------------------
int stub_game_machinima_copy_clip(lua_State* L) {
    logCall(L, upLog(L), "game_machinima_copy_clip", upStateTag(L));
    std::string src = argString(L, 1);
    std::string dst = argString(L, 2);
    EngineState* st = upState(L);
    auto srcPath = st->buildMachinimaClipPath(src);
    auto dstPath = st->buildMachinimaClipPath(dst);
    if (srcPath.ok && dstPath.ok && st->machinima().clips.count(src) != 0) {
        st->machinima().clips[dst] = true;
    }
    return 0; // CONFIRMED: CopyFileW result discarded, no push either way
}

// ---------------------------------------------------------------------
// game_machinima_delete_clip (Sec56.5) - THE MOST SEVERE instance in the
// family: with a plain (non-encoded) name, the real decode buffer is
// NEVER written, and a file whose name is whatever garbage was already on
// the stack gets deleted (a stale buffer holding an earlier real clip
// name would delete THAT clip - a genuine wrong-file-deletion defect).
// CHOSEN (hardened divergence, Sec56.5/Sec56.7) - the whole reason this
// family shares one hardened path-builder: fail closed on ANY path-
// builder failure, so this host can never resolve an uninitialized name
// to delete.
// ---------------------------------------------------------------------
int stub_game_machinima_delete_clip(lua_State* L) {
    logCall(L, upLog(L), "game_machinima_delete_clip", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    auto path = st->buildMachinimaClipPath(name);
    if (!path.ok) return 0; // CHOSEN (hardened divergence): never deletes an unresolved/garbage name
    st->machinima().clips.erase(name);
    return 0;
}

// ---------------------------------------------------------------------
// game_machinima_overwrite_clip (Sec56.5). Arguments: 2 strings (source,
// dest). Shares the uninitialized-source-name shape with copy_clip -
// same hardened path-builder treatment.
// ---------------------------------------------------------------------
int stub_game_machinima_overwrite_clip(lua_State* L) {
    logCall(L, upLog(L), "game_machinima_overwrite_clip", upStateTag(L));
    std::string src = argString(L, 1);
    std::string dst = argString(L, 2);
    EngineState* st = upState(L);
    auto srcPath = st->buildMachinimaClipPath(src);
    auto dstPath = st->buildMachinimaClipPath(dst);
    if (srcPath.ok && dstPath.ok) {
        st->machinima().clips[dst] = true; // CONFIRMED: overwrite semantics, result discarded
    }
    return 0;
}

// ---------------------------------------------------------------------
// game_machinima_record (Sec56.5). Arguments: CHOSEN 1 optional boolean
// (start=true/stop=false), default true - the exact arg shape is not
// individually detailed beyond its own behavior. CONFIRMED: ALWAYS resets
// first (stops/discards any in-progress recording, radio unmuted, buffers
// freed) before deciding whether to actually start; a refusal to record
// is silent, leaving the reset's side effects in place regardless.
// ---------------------------------------------------------------------
int stub_game_machinima_record(lua_State* L) {
    logCall(L, upLog(L), "game_machinima_record", upStateTag(L));
    bool wantRecord = optionalBoolDefault(L, 1, true);
    EngineState* st = upState(L);
    st->machinima().recording = false; // CONFIRMED: always resets first
    if (wantRecord) {
        auto path = st->buildMachinimaClipPath("recording"); // CHOSEN stand-in name - no clip-name argument documented
        if (path.ok) {
            st->machinima().recording = true;
            ++st->machinima().recordCount;
        }
        // else: silent refusal, CONFIRMED
    }
    return 0;
}

// =======================================================================
// Sec45.6 autil_hud_mayhem_init (CRASH - hardened)
// =======================================================================

// ---------------------------------------------------------------------
// autil_hud_mayhem_init (Sec45.6, new one-name registrar 0x0067d660). No
// arguments documented beyond its own shape. CONFIRMED: one-time native-
// callback creation plus per-call UI-element lookup/data-item
// subscription; calling it more than once re-subscribes with no
// confirmed de-duplication; an empty native-callback pool on the very
// first call reads through null. CHOSEN (hardened divergence, Sec45.6):
// an unavailable callback pool refuses instead, counted.
// ---------------------------------------------------------------------
int stub_autil_hud_mayhem_init(lua_State* L) {
    logCall(L, upLog(L), "autil_hud_mayhem_init", upStateTag(L));
    EngineState* st = upState(L);
    auto& a = st->autilHudMayhemInit();
    if (!a.oneTimeSetupDone) {
        if (!a.nativeCallbackPoolAvailable.get()) { // OPEN until set
            ++a.poolExhaustedGuardCount; // CHOSEN (hardened divergence): refuse instead of a null-pool read
            return 0;
        }
        a.oneTimeSetupDone = true;
    }
    ++a.subscribeCount; // CONFIRMED: per-call re-subscription, no confirmed de-duplication
    return 0;
}

// =======================================================================
// Sec45.4 cellphone_dial (CRASH - hardened)
// =======================================================================

// ---------------------------------------------------------------------
// cellphone_dial (Sec45.4, 0x0083d930; CRASH - hardened). Arguments: 1
// phone-book index (number) + 2 callback-name strings. CONFIRMED 3 real
// crash shapes in the real engine: no bound/sign check on the phone-book
// index (reads an arbitrary 24-byte entry); a bad lookup's null result is
// dereferenced unchecked; the callback-coroutine result is written
// through with no null check. CHOSEN (hardened divergence, Sec45.4): the
// index is bounds-checked, refusing (not dereferencing) an out-of-range
// value, counted; the callback-coroutine mechanism is not implemented in
// this project at all (established convention - every named-callback
// mention elsewhere in this document is counted, never actually invoked),
// so its own null-write shape cannot occur here either.
// ---------------------------------------------------------------------
int stub_cellphone_dial(lua_State* L) {
    logCall(L, upLog(L), "cellphone_dial", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    std::string cb1 = optionalStringDefault(L, 2, "");
    std::string cb2 = optionalStringDefault(L, 3, "");
    (void)cb1;
    (void)cb2;
    EngineState* st = upState(L);
    uint32_t count = st->cellphoneDial().phoneBookCount.get(); // OPEN until set
    if (idx < 0 || static_cast<uint32_t>(idx) >= count) {
        ++st->cellphoneDial().outOfRangeGuardCount; // CHOSEN (hardened divergence)
        return 0;
    }
    ++st->cellphoneDial().dialCount;
    return 0;
}

// =======================================================================
// Sec45.2 cinema_editor_create_camera_zone (CRASH - hardened) /
// cinema_editor_save_clip
// =======================================================================

// ---------------------------------------------------------------------
// cinema_editor_create_camera_zone (Sec45.2, 0x00bcc1d0; CRASH -
// hardened). Arguments: 1 optional number (template handle to copy
// from). Return: a new zone handle (1 number) or 0 values on failure
// (not nil - CONFIRMED). Real engine: the template handle's slot index
// (handle & 0xff) is used to read the pool BEFORE being bounded by the
// pool's actual size (an out-of-bounds read). CHOSEN (hardened
// divergence, Sec45.2): an unknown template slot refuses instead of
// reading out of bounds, counted.
// ---------------------------------------------------------------------
int stub_cinema_editor_create_camera_zone(lua_State* L) {
    logCall(L, upLog(L), "cinema_editor_create_camera_zone", upStateTag(L));
    EngineState* st = upState(L);
    auto& ce = st->cinemaEditor();
    if (lua_gettop(L) >= 1 && lua_type(L, 1) != LUA_TNIL) {
        int64_t handle = truncateEa2596(lua_tonumber(L, 1));
        uint32_t slot = static_cast<uint32_t>(handle) & 0xff; // CONFIRMED: handle & 0xff slot index
        if (ce.zoneGeneration.find(slot) == ce.zoneGeneration.end()) {
            ++ce.oobTemplateGuardCount; // CHOSEN (hardened divergence): refuse the OOB template read
            return 0;                   // CONFIRMED: 0 values on failure, not nil
        }
    }
    uint32_t newHandle = ce.nextZoneHandle++;
    ce.zoneGeneration[newHandle & 0xff] = newHandle;
    ++ce.createCount;
    lua_pushnumber(L, static_cast<lua_Number>(newHandle));
    return 1;
}

// ---------------------------------------------------------------------
// cinema_editor_save_clip (Sec57.4, developer-tool, HYPOTHESIS
// unreachable in shipped play). Arguments: CHOSEN 1 string (clip name) -
// the exact arg shape is not individually detailed. CONFIRMED leaves its
// destination path buffer uninitialized when any of 3 Win32 calls
// building it fails, then hands that buffer to CreateFileW. CHOSEN
// (hardened divergence, Sec57.4): reuses the SAME shared hardened path-
// builder as the rest of the machinima family - fails closed instead.
// ---------------------------------------------------------------------
int stub_cinema_editor_save_clip(lua_State* L) {
    logCall(L, upLog(L), "cinema_editor_save_clip", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    auto path = st->buildMachinimaClipPath(name);
    if (!path.ok) {
        ++st->cinemaEditor().saveClipPathGuardCount; // CHOSEN (hardened divergence)
        return 0;
    }
    return 0; // dev-tool; no further Lua-visible state in this project
}

// =======================================================================
// Sec45.3 dialog_box_set_current_option / _close_finished /
// _option_accept_kbd_input / _option_end_kbd_input
// =======================================================================

// ---------------------------------------------------------------------
// dialog_box_set_current_option (Sec45.3). Arguments: 1 number (option
// index). No return. CONFIRMED writes the index with NO range check
// (consumer unknown, OPEN) - just a plain store, no crash shape.
// ---------------------------------------------------------------------
int stub_dialog_box_set_current_option(lua_State* L) {
    logCall(L, upLog(L), "dialog_box_set_current_option", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->dialogUi().currentOptionIndex = static_cast<int32_t>(idx);
    return 0;
}

// ---------------------------------------------------------------------
// dialog_close_finished (Sec45.3, 0x007c1be0). No arguments, no return.
// CONFIRMED a required handshake with no timeout: clears a latch byte the
// dialog manager's own per-pass routine blocks on until this is called.
// This project does not model the manager's own freeze mechanism (no
// polling loop to unblock) - only the latch-clear transition is counted.
// ---------------------------------------------------------------------
int stub_dialog_close_finished(lua_State* L) {
    logCall(L, upLog(L), "dialog_close_finished", upStateTag(L));
    EngineState* st = upState(L);
    st->dialogUi().closeLatchSet = false;
    ++st->dialogUi().closeFinishedCount;
    return 0;
}

// ---------------------------------------------------------------------
// dialog_option_accept_kbd_input (Sec45.3). Arguments: CHOSEN 1 number
// (option index) - the exact full arg shape is not individually detailed
// beyond "start text-entry editing on a dialog option (cursor capped at
// 191 UTF-16 units, key-state table flushed on start)". No return.
// ---------------------------------------------------------------------
int stub_dialog_option_accept_kbd_input(lua_State* L) {
    logCall(L, upLog(L), "dialog_option_accept_kbd_input", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    st->dialogUi().kbdEditing = true;
    st->dialogUi().kbdCursorPos = 0; // CONFIRMED: key-state table flushed on start
    ++st->dialogUi().kbdAcceptCount;
    return 0;
}

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

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB18() {
    static const std::vector<std::string> names = {
        "cellphone_choose_vehicle",
        "main_menu_check_user_content",
        "Completion_get_other_player_name",
        "completion_screen_finished",
        "pause_menu_quit_game_internal",
        "completion_camera_shake",
        "completion_credits_were_active",
        "completion_cutscene_was_skipped",
        "dialog_box_set_result",
        "dialog_open_pause_display",
        "main_menu_redeem_code",
        "pause_map_add_bookmark",
        "pause_map_zoom",
        "pause_menu_change_control_scheme",
        "main_menu_new_game",
        "main_menu_horde_start",
        "main_menu_set_coop_menu_type",
        "game_machinima_is_recording",
        "game_machinima_playback_enter",
        "game_machinima_clip_exists",
        "game_machinima_copy_clip",
        "game_machinima_delete_clip",
        "game_machinima_overwrite_clip",
        "game_machinima_record",
        "autil_hud_mayhem_init",
        "cellphone_dial",
        "cinema_editor_create_camera_zone",
        "cinema_editor_save_clip",
        "dialog_box_set_current_option",
        "dialog_close_finished",
        "dialog_option_accept_kbd_input",
        "dialog_option_end_kbd_input",
        "vint_options_remap_reset_bindings",
        "pause_menu_horde_mode_new",
        "pause_menu_horde_mode_retry",
        "pause_menu_is_using_southpaw_control_scheme",
        "pause_menu_is_using_vehicle_southpaw_control_scheme",
        "playlist_play_track",
        "playlist_stop_track",
        "save_game_wrap_up",
        "save_system_change_device",
        "save_system_delete_game",
        "save_system_set_operation",
        "sb_saintsbook_is_available",
        "sb_select_chop_shop_vehicle",
        "sb_select_hitman_target",
        "main_menu_restart_video",
        "main_menu_video_has_stopped",
        "msn_killbane_selected_option",
        "object_indicator_init_lua",
        "options_display_pc_set_options",
        "pause_map_drag_map",
        "pause_menu_cant_retry_because_host_in_creation",
        "pause_menu_cat_mouse_active",
        "pause_menu_common_top_finished_loading",
        "main_menu_check_chat_priv",
        "main_menu_check_messages",
        "main_menu_check_online_privilege",
        "main_menu_check_open_nat",
        "main_menu_continue",
        "main_menu_matchmaking",
        "main_menu_maybe_show_voice_dialog",
        "main_menu_money_shot_check",
        "cell_playlist_save_list",
        "city_load_img_load_complete",
        "community_login_open",
        "completion_call_callback",
        "dialog_box_create_internal",
        "dialog_pause_unpause",
        "cell_allow_camera",
        "cell_cheats_activate_cheat",
        "cell_cheats_unlock_cheat",
        "cell_debug_all_enabled",
        "cell_is_closing",
        "cell_is_map_disabled",
        "cell_machinima_is_recording",
        "cell_music_menu_toggle_station",
    };
    return names;
}

void registerB18(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("cellphone_choose_vehicle")) registerOne(L, state, log, stateTag, "cellphone_choose_vehicle", openGuard<stub_cellphone_choose_vehicle>);
    if (wants("main_menu_check_user_content")) registerOne(L, state, log, stateTag, "main_menu_check_user_content", openGuard<stub_main_menu_check_user_content>);
    if (wants("Completion_get_other_player_name")) registerOne(L, state, log, stateTag, "Completion_get_other_player_name", openGuard<stub_Completion_get_other_player_name>);
    if (wants("completion_screen_finished")) registerOne(L, state, log, stateTag, "completion_screen_finished", openGuard<stub_completion_screen_finished>);
    if (wants("pause_menu_quit_game_internal")) registerOne(L, state, log, stateTag, "pause_menu_quit_game_internal", openGuard<stub_pause_menu_quit_game_internal>);
    if (wants("completion_camera_shake")) registerOne(L, state, log, stateTag, "completion_camera_shake", openGuard<stub_completion_camera_shake>);
    if (wants("completion_credits_were_active")) registerOne(L, state, log, stateTag, "completion_credits_were_active", openGuard<stub_completion_credits_were_active>);
    if (wants("completion_cutscene_was_skipped")) registerOne(L, state, log, stateTag, "completion_cutscene_was_skipped", openGuard<stub_completion_cutscene_was_skipped>);
    if (wants("dialog_box_set_result")) registerOne(L, state, log, stateTag, "dialog_box_set_result", openGuard<stub_dialog_box_set_result>);
    if (wants("dialog_open_pause_display")) registerOne(L, state, log, stateTag, "dialog_open_pause_display", openGuard<stub_dialog_open_pause_display>);
    if (wants("main_menu_redeem_code")) registerOne(L, state, log, stateTag, "main_menu_redeem_code", openGuard<stub_main_menu_redeem_code>);
    if (wants("pause_map_add_bookmark")) registerOne(L, state, log, stateTag, "pause_map_add_bookmark", openGuard<stub_pause_map_add_bookmark>);
    if (wants("pause_map_zoom")) registerOne(L, state, log, stateTag, "pause_map_zoom", openGuard<stub_pause_map_zoom>);
    if (wants("pause_menu_change_control_scheme")) registerOne(L, state, log, stateTag, "pause_menu_change_control_scheme", openGuard<stub_pause_menu_change_control_scheme>);
    if (wants("main_menu_new_game")) registerOne(L, state, log, stateTag, "main_menu_new_game", openGuard<stub_main_menu_new_game>);
    if (wants("main_menu_horde_start")) registerOne(L, state, log, stateTag, "main_menu_horde_start", openGuard<stub_main_menu_horde_start>);
    if (wants("main_menu_set_coop_menu_type")) registerOne(L, state, log, stateTag, "main_menu_set_coop_menu_type", openGuard<stub_main_menu_set_coop_menu_type>);
    if (wants("game_machinima_is_recording")) registerOne(L, state, log, stateTag, "game_machinima_is_recording", openGuard<stub_game_machinima_is_recording>);
    if (wants("game_machinima_playback_enter")) registerOne(L, state, log, stateTag, "game_machinima_playback_enter", openGuard<stub_game_machinima_playback_enter>);
    if (wants("game_machinima_clip_exists")) registerOne(L, state, log, stateTag, "game_machinima_clip_exists", openGuard<stub_game_machinima_clip_exists>);
    if (wants("game_machinima_copy_clip")) registerOne(L, state, log, stateTag, "game_machinima_copy_clip", openGuard<stub_game_machinima_copy_clip>);
    if (wants("game_machinima_delete_clip")) registerOne(L, state, log, stateTag, "game_machinima_delete_clip", openGuard<stub_game_machinima_delete_clip>);
    if (wants("game_machinima_overwrite_clip")) registerOne(L, state, log, stateTag, "game_machinima_overwrite_clip", openGuard<stub_game_machinima_overwrite_clip>);
    if (wants("game_machinima_record")) registerOne(L, state, log, stateTag, "game_machinima_record", openGuard<stub_game_machinima_record>);
    if (wants("autil_hud_mayhem_init")) registerOne(L, state, log, stateTag, "autil_hud_mayhem_init", openGuard<stub_autil_hud_mayhem_init>);
    if (wants("cellphone_dial")) registerOne(L, state, log, stateTag, "cellphone_dial", openGuard<stub_cellphone_dial>);
    if (wants("cinema_editor_create_camera_zone")) registerOne(L, state, log, stateTag, "cinema_editor_create_camera_zone", openGuard<stub_cinema_editor_create_camera_zone>);
    if (wants("cinema_editor_save_clip")) registerOne(L, state, log, stateTag, "cinema_editor_save_clip", openGuard<stub_cinema_editor_save_clip>);
    if (wants("dialog_box_set_current_option")) registerOne(L, state, log, stateTag, "dialog_box_set_current_option", openGuard<stub_dialog_box_set_current_option>);
    if (wants("dialog_close_finished")) registerOne(L, state, log, stateTag, "dialog_close_finished", openGuard<stub_dialog_close_finished>);
    if (wants("dialog_option_accept_kbd_input")) registerOne(L, state, log, stateTag, "dialog_option_accept_kbd_input", openGuard<stub_dialog_option_accept_kbd_input>);
    if (wants("dialog_option_end_kbd_input")) registerOne(L, state, log, stateTag, "dialog_option_end_kbd_input", openGuard<stub_dialog_option_end_kbd_input>);
    if (wants("vint_options_remap_reset_bindings")) registerOne(L, state, log, stateTag, "vint_options_remap_reset_bindings", openGuard<stub_vint_options_remap_reset_bindings>);
    if (wants("pause_menu_horde_mode_new")) registerOne(L, state, log, stateTag, "pause_menu_horde_mode_new", openGuard<stub_pause_menu_horde_mode_new>);
    if (wants("pause_menu_horde_mode_retry")) registerOne(L, state, log, stateTag, "pause_menu_horde_mode_retry", openGuard<stub_pause_menu_horde_mode_retry>);
    if (wants("pause_menu_is_using_southpaw_control_scheme")) registerOne(L, state, log, stateTag, "pause_menu_is_using_southpaw_control_scheme", openGuard<stub_pause_menu_is_using_southpaw_control_scheme>);
    if (wants("pause_menu_is_using_vehicle_southpaw_control_scheme")) registerOne(L, state, log, stateTag, "pause_menu_is_using_vehicle_southpaw_control_scheme", openGuard<stub_pause_menu_is_using_vehicle_southpaw_control_scheme>);
    if (wants("playlist_play_track")) registerOne(L, state, log, stateTag, "playlist_play_track", openGuard<stub_playlist_play_track>);
    if (wants("playlist_stop_track")) registerOne(L, state, log, stateTag, "playlist_stop_track", openGuard<stub_playlist_stop_track>);
    if (wants("save_game_wrap_up")) registerOne(L, state, log, stateTag, "save_game_wrap_up", openGuard<stub_save_game_wrap_up>);
    if (wants("save_system_change_device")) registerOne(L, state, log, stateTag, "save_system_change_device", openGuard<stub_save_system_change_device>);
    if (wants("save_system_delete_game")) registerOne(L, state, log, stateTag, "save_system_delete_game", openGuard<stub_save_system_delete_game>);
    if (wants("save_system_set_operation")) registerOne(L, state, log, stateTag, "save_system_set_operation", openGuard<stub_save_system_set_operation>);
    if (wants("sb_saintsbook_is_available")) registerOne(L, state, log, stateTag, "sb_saintsbook_is_available", openGuard<stub_sb_saintsbook_is_available>);
    if (wants("sb_select_chop_shop_vehicle")) registerOne(L, state, log, stateTag, "sb_select_chop_shop_vehicle", openGuard<stub_sb_select_chop_shop_vehicle>);
    if (wants("sb_select_hitman_target")) registerOne(L, state, log, stateTag, "sb_select_hitman_target", openGuard<stub_sb_select_hitman_target>);
    if (wants("main_menu_restart_video")) registerOne(L, state, log, stateTag, "main_menu_restart_video", openGuard<stub_main_menu_restart_video>);
    if (wants("main_menu_video_has_stopped")) registerOne(L, state, log, stateTag, "main_menu_video_has_stopped", openGuard<stub_main_menu_video_has_stopped>);
    if (wants("msn_killbane_selected_option")) registerOne(L, state, log, stateTag, "msn_killbane_selected_option", openGuard<stub_msn_killbane_selected_option>);
    if (wants("object_indicator_init_lua")) registerOne(L, state, log, stateTag, "object_indicator_init_lua", openGuard<stub_object_indicator_init_lua>);
    if (wants("options_display_pc_set_options")) registerOne(L, state, log, stateTag, "options_display_pc_set_options", openGuard<stub_options_display_pc_set_options>);
    if (wants("pause_map_drag_map")) registerOne(L, state, log, stateTag, "pause_map_drag_map", openGuard<stub_pause_map_drag_map>);
    if (wants("pause_menu_cant_retry_because_host_in_creation")) registerOne(L, state, log, stateTag, "pause_menu_cant_retry_because_host_in_creation", openGuard<stub_pause_menu_cant_retry_because_host_in_creation>);
    if (wants("pause_menu_cat_mouse_active")) registerOne(L, state, log, stateTag, "pause_menu_cat_mouse_active", openGuard<stub_pause_menu_cat_mouse_active>);
    if (wants("pause_menu_common_top_finished_loading")) registerOne(L, state, log, stateTag, "pause_menu_common_top_finished_loading", openGuard<stub_pause_menu_common_top_finished_loading>);
    if (wants("main_menu_check_chat_priv")) registerOne(L, state, log, stateTag, "main_menu_check_chat_priv", openGuard<stub_main_menu_check_chat_priv>);
    if (wants("main_menu_check_messages")) registerOne(L, state, log, stateTag, "main_menu_check_messages", openGuard<stub_main_menu_check_messages>);
    if (wants("main_menu_check_online_privilege")) registerOne(L, state, log, stateTag, "main_menu_check_online_privilege", openGuard<stub_main_menu_check_online_privilege>);
    if (wants("main_menu_check_open_nat")) registerOne(L, state, log, stateTag, "main_menu_check_open_nat", openGuard<stub_main_menu_check_open_nat>);
    if (wants("main_menu_continue")) registerOne(L, state, log, stateTag, "main_menu_continue", openGuard<stub_main_menu_continue>);
    if (wants("main_menu_matchmaking")) registerOne(L, state, log, stateTag, "main_menu_matchmaking", openGuard<stub_main_menu_matchmaking>);
    if (wants("main_menu_maybe_show_voice_dialog")) registerOne(L, state, log, stateTag, "main_menu_maybe_show_voice_dialog", openGuard<stub_main_menu_maybe_show_voice_dialog>);
    if (wants("main_menu_money_shot_check")) registerOne(L, state, log, stateTag, "main_menu_money_shot_check", openGuard<stub_main_menu_money_shot_check>);
    if (wants("cell_playlist_save_list")) registerOne(L, state, log, stateTag, "cell_playlist_save_list", openGuard<stub_cell_playlist_save_list>);
    if (wants("city_load_img_load_complete")) registerOne(L, state, log, stateTag, "city_load_img_load_complete", openGuard<stub_city_load_img_load_complete>);
    if (wants("community_login_open")) registerOne(L, state, log, stateTag, "community_login_open", openGuard<stub_community_login_open>);
    if (wants("completion_call_callback")) registerOne(L, state, log, stateTag, "completion_call_callback", openGuard<stub_completion_call_callback>);
    if (wants("dialog_box_create_internal")) registerOne(L, state, log, stateTag, "dialog_box_create_internal", openGuard<stub_dialog_box_create_internal>);
    if (wants("dialog_pause_unpause")) registerOne(L, state, log, stateTag, "dialog_pause_unpause", openGuard<stub_dialog_pause_unpause>);
    if (wants("cell_allow_camera")) registerOne(L, state, log, stateTag, "cell_allow_camera", openGuard<stub_cell_allow_camera>);
    if (wants("cell_cheats_activate_cheat")) registerOne(L, state, log, stateTag, "cell_cheats_activate_cheat", openGuard<stub_cell_cheats_activate_cheat>);
    if (wants("cell_cheats_unlock_cheat")) registerOne(L, state, log, stateTag, "cell_cheats_unlock_cheat", openGuard<stub_cell_cheats_unlock_cheat>);
    if (wants("cell_debug_all_enabled")) registerOne(L, state, log, stateTag, "cell_debug_all_enabled", openGuard<stub_cell_debug_all_enabled>);
    if (wants("cell_is_closing")) registerOne(L, state, log, stateTag, "cell_is_closing", openGuard<stub_cell_is_closing>);
    if (wants("cell_is_map_disabled")) registerOne(L, state, log, stateTag, "cell_is_map_disabled", openGuard<stub_cell_is_map_disabled>);
    if (wants("cell_machinima_is_recording")) registerOne(L, state, log, stateTag, "cell_machinima_is_recording", openGuard<stub_cell_machinima_is_recording>);
    if (wants("cell_music_menu_toggle_station")) registerOne(L, state, log, stateTag, "cell_music_menu_toggle_station", openGuard<stub_cell_music_menu_toggle_station>);
}

} // namespace stub_sections
} // namespace sr3luahost
