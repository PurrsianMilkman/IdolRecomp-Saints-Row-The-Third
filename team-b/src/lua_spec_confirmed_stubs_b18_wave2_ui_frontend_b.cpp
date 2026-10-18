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

// Defined in lua_spec_confirmed_stubs_b18_wave2_ui_frontend_b_part2.cpp (split out of this file, no behaviour change).
} // namespace
namespace stubs_b18 {
int stub_dialog_option_end_kbd_input(lua_State* L);
int stub_vint_options_remap_reset_bindings(lua_State* L);
int stub_pause_menu_horde_mode_new(lua_State* L);
int stub_pause_menu_horde_mode_retry(lua_State* L);
int stub_pause_menu_is_using_southpaw_control_scheme(lua_State* L);
int stub_pause_menu_is_using_vehicle_southpaw_control_scheme(lua_State* L);
int stub_playlist_play_track(lua_State* L);
int stub_playlist_stop_track(lua_State* L);
int stub_save_game_wrap_up(lua_State* L);
int stub_save_system_change_device(lua_State* L);
int stub_save_system_delete_game(lua_State* L);
int stub_save_system_set_operation(lua_State* L);
int stub_sb_saintsbook_is_available(lua_State* L);
int stub_sb_select_chop_shop_vehicle(lua_State* L);
int stub_sb_select_hitman_target(lua_State* L);
int stub_main_menu_restart_video(lua_State* L);
int stub_main_menu_video_has_stopped(lua_State* L);
int stub_msn_killbane_selected_option(lua_State* L);
int stub_object_indicator_init_lua(lua_State* L);
int stub_options_display_pc_set_options(lua_State* L);
int stub_pause_map_drag_map(lua_State* L);
int stub_pause_menu_cant_retry_because_host_in_creation(lua_State* L);
int stub_pause_menu_cat_mouse_active(lua_State* L);
int stub_pause_menu_common_top_finished_loading(lua_State* L);
int stub_main_menu_check_chat_priv(lua_State* L);
int stub_main_menu_check_online_privilege(lua_State* L);
int stub_main_menu_check_messages(lua_State* L);
int stub_main_menu_maybe_show_voice_dialog(lua_State* L);
int stub_main_menu_money_shot_check(lua_State* L);
int stub_main_menu_check_open_nat(lua_State* L);
int stub_main_menu_continue(lua_State* L);
int stub_main_menu_matchmaking(lua_State* L);
int stub_cell_playlist_save_list(lua_State* L);
int stub_city_load_img_load_complete(lua_State* L);
int stub_community_login_open(lua_State* L);
int stub_completion_call_callback(lua_State* L);
int stub_dialog_box_create_internal(lua_State* L);
int stub_dialog_pause_unpause(lua_State* L);
int stub_cell_allow_camera(lua_State* L);
int stub_cell_debug_all_enabled(lua_State* L);
int stub_cell_is_closing(lua_State* L);
int stub_cell_is_map_disabled(lua_State* L);
int stub_cell_machinima_is_recording(lua_State* L);
int stub_cell_music_menu_toggle_station(lua_State* L);
int stub_cell_cheats_activate_cheat(lua_State* L);
int stub_cell_cheats_unlock_cheat(lua_State* L);
} // namespace stubs_b18
namespace {
using stubs_b18::stub_dialog_option_end_kbd_input;
using stubs_b18::stub_vint_options_remap_reset_bindings;
using stubs_b18::stub_pause_menu_horde_mode_new;
using stubs_b18::stub_pause_menu_horde_mode_retry;
using stubs_b18::stub_pause_menu_is_using_southpaw_control_scheme;
using stubs_b18::stub_pause_menu_is_using_vehicle_southpaw_control_scheme;
using stubs_b18::stub_playlist_play_track;
using stubs_b18::stub_playlist_stop_track;
using stubs_b18::stub_save_game_wrap_up;
using stubs_b18::stub_save_system_change_device;
using stubs_b18::stub_save_system_delete_game;
using stubs_b18::stub_save_system_set_operation;
using stubs_b18::stub_sb_saintsbook_is_available;
using stubs_b18::stub_sb_select_chop_shop_vehicle;
using stubs_b18::stub_sb_select_hitman_target;
using stubs_b18::stub_main_menu_restart_video;
using stubs_b18::stub_main_menu_video_has_stopped;
using stubs_b18::stub_msn_killbane_selected_option;
using stubs_b18::stub_object_indicator_init_lua;
using stubs_b18::stub_options_display_pc_set_options;
using stubs_b18::stub_pause_map_drag_map;
using stubs_b18::stub_pause_menu_cant_retry_because_host_in_creation;
using stubs_b18::stub_pause_menu_cat_mouse_active;
using stubs_b18::stub_pause_menu_common_top_finished_loading;
using stubs_b18::stub_main_menu_check_chat_priv;
using stubs_b18::stub_main_menu_check_online_privilege;
using stubs_b18::stub_main_menu_check_messages;
using stubs_b18::stub_main_menu_maybe_show_voice_dialog;
using stubs_b18::stub_main_menu_money_shot_check;
using stubs_b18::stub_main_menu_check_open_nat;
using stubs_b18::stub_main_menu_continue;
using stubs_b18::stub_main_menu_matchmaking;
using stubs_b18::stub_cell_playlist_save_list;
using stubs_b18::stub_city_load_img_load_complete;
using stubs_b18::stub_community_login_open;
using stubs_b18::stub_completion_call_callback;
using stubs_b18::stub_dialog_box_create_internal;
using stubs_b18::stub_dialog_pause_unpause;
using stubs_b18::stub_cell_allow_camera;
using stubs_b18::stub_cell_debug_all_enabled;
using stubs_b18::stub_cell_is_closing;
using stubs_b18::stub_cell_is_map_disabled;
using stubs_b18::stub_cell_machinima_is_recording;
using stubs_b18::stub_cell_music_menu_toggle_station;
using stubs_b18::stub_cell_cheats_activate_cheat;
using stubs_b18::stub_cell_cheats_unlock_cheat;

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
