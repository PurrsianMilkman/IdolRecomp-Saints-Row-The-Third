// lua_spec_confirmed_stubs_b02_sec27_28.cpp
//
// Batch 2026-10-01: spec-lua-api-behaviour.md Sec27 (UI cluster, ranks 551-650 part D) + Sec28 (gameplay), 50 names.
//
// One of 7 files split 2026-10-07 from the former single 8,700+ line
// lua_spec_confirmed_stubs.cpp (a pure structural refactor, zero behaviour
// change - every stub body, comment and citation below is reproduced
// verbatim from that file). See lua_spec_confirmed_stubs_registry.cpp's own
// top comment for the full split rationale and the shared-registration
// mechanism (lua_spec_confirmed_stubs_sections.h/_common.h).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"
#include "sr3luahost/save_game_bridge.h"

namespace sr3luahost {
namespace {

// =======================================================================
// Batch 2026-10-01: spec-lua-api-behaviour.md Sec27 (25 UI-cluster
// functions, "ranks 551-650" tranche part D) and Sec28 (25 gameplay-
// cluster functions, part C) - jobs 20261001T021703-team-a-pwgq /
// 20261001T021707-team-a-jxxz. Every function below implements ONLY its
// own entry's CONFIRMED/CORRECTED body (the current, non-struck-through
// text); a sub-item an entry's own 2026-10-01 review-status line still
// lists as OPEN is modelled as an OpenValue/OpenValueMap read (throws
// OpenStateError, caught by openGuard below) rather than invented - see
// engine_state.h's own per-field doc comment for exactly which real
// global/gate each one stands for. Deep multi-step dispatch chains this
// project has no real data tables for (hook-slot-array bases, class-
// descriptor bit tables, trigger/spatial queries, rig-file loading) are
// NOT separately simulated - only the real, checkable gate/branch and a
// count of "this happened" is modelled, the same "record the request,
// not the full engine" convention already established for
// minimap_icon_add_do/object_indicator_add_do earlier in this file.
// =======================================================================


// ---------------------------------------------------------------------
// 28. cat_mouse_results_select (Sec27.1, 0x007bd240): 1 mandatory number
// (result-slot/winner index, rounded via the round-cast pair, no nil-
// gate). Return: none. CONFIRMED hidden-`this` chain: resolves the cat-
// and-mouse minigame singleton (0x014c2d10) and, if one exists, remaps
// the index into the object's own +0xe0 selection field (0 -> 1 if the
// object's +0x1c field is 1, else 2; 1 -> 2; any other index leaves it
// unchanged) then, when the field is non-zero, sends a message to the
// co-op session (no real networking layer - counted only, see
// EngineState::replicateStateChange's own convention). OPEN (stated in
// the spec itself): the real-world meaning of selection values 1/2.
// ---------------------------------------------------------------------
int stub_cat_mouse_results_select(lua_State* L) {
    logCall(L, upLog(L), "cat_mouse_results_select", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    auto& mg = upState(L)->catMouseMinigame();
    if (mg.present.get()) { // OPEN until set
        int32_t sel = mg.selection;
        if (idx == 0) sel = mg.fieldIc1IsOne.get() ? 1 : 2; // OPEN until set
        else if (idx == 1) sel = 2;
        mg.selection = sel;
        if (sel != 0) ++mg.sessionMessageCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 29. cell_is_mission_complete (Sec27.2, 0x00a525a0 - the SAME function
// as Sec20.14's `mission_is_complete`, registered under two names). 1
// mandatory string (mission name). Return: 1 boolean. CONFIRMED body:
// false for an empty name; else resolves through the shared singleton
// resolver family (reuses objectResolves() - this project's own one-
// map-for-all-resolvers simplification, engine_state.h's own top note);
// false unless alive; else the resolved object's own +0x88 bit 0x4.
// ---------------------------------------------------------------------
int stub_cell_is_mission_complete(lua_State* L) {
    logCall(L, upLog(L), "cell_is_mission_complete", upStateTag(L));
    std::string name = argString(L, 1);
    bool complete = false;
    if (!name.empty()) {
        if (upState(L)->objectResolves().get(name)) { // OPEN until set
            complete = upState(L)->missionIsComplete(name); // Sec20.14 port pointer: false unless host state says complete
        }
    }
    lua_pushboolean(L, complete ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 30. Completion_should_wait_for_coop (Sec27.3, 0x007bfc10). No
// arguments. Return: 1 boolean. CONFIRMED: resolves the shared
// "completion_screen" UI context; absent -> true (the stated default).
// Present -> when the flag-cluster global 0x02282282 is zero, the
// internal flag comes from a double-deref byte (>=1 true); when
// nonzero, from the per-player walk 0x00877ca0(context,1) (abstracted
// to one flag, see engine_state.h's own CompletionScreen doc comment).
// The value actually pushed is the LOGICAL NEGATION of that internal
// flag (CONFIRMED - the name describes the pushed result directly).
// ---------------------------------------------------------------------
int stub_Completion_should_wait_for_coop(lua_State* L) {
    logCall(L, upLog(L), "Completion_should_wait_for_coop", upStateTag(L));
    auto& cs = upState(L)->completionScreen();
    bool internalFlag = true; // CONFIRMED default when no context resolves
    if (cs.present.get()) { // OPEN until set
        if (!cs.flagClusterSet.get()) { // OPEN until set
            internalFlag = cs.derefByteAtLeastOne.get(); // OPEN until set
        } else {
            internalFlag = cs.everyPlayerAtThreshold1.get(); // OPEN until set
        }
    }
    lua_pushboolean(L, internalFlag ? 0 : 1); // CONFIRMED: pushed value is the negation
    return 1;
}

// ---------------------------------------------------------------------
// 31. Completion_user_is_done_viewing (Sec27.4, 0x007bdc80). No
// arguments. Return: none. CONFIRMED: resolves the SAME
// "completion_screen" context as item 30 above; if present and the
// context's own +4 record is enabled (+2 byte nonzero), stores state 1
// (always != 0x81) and sends a message (no real networking layer here -
// counted only).
// ---------------------------------------------------------------------
int stub_Completion_user_is_done_viewing(lua_State* L) {
    logCall(L, upLog(L), "Completion_user_is_done_viewing", upStateTag(L));
    auto& cs = upState(L)->completionScreen();
    if (cs.present.get()) { // OPEN until set
        if (cs.recordEnabled.get()) { // OPEN until set
            cs.lastStateWritten = 1;
            ++cs.messageSentCount;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 32. vcust_set_camera_pos (Sec27.5, 0x0081fa80). 1 mandatory string
// (camera-position preset name). Return: none. CONFIRMED: resolves the
// current vehicle-customization target (capability+class-gated, then
// liveness-gated - "proceed when alive", Sec27.5's own resolved
// polarity note); on success hashes the preset name (the real CRC-32,
// sr3save::nameHash - CONFIRMED match to 0x00d9e8b0, see this file's own
// multiply33XorHashBucket neighbour for the project's established reuse
// convention) and selects the camera preset by that hash (0x00a9b080's
// own positioning math is OPEN and not simulated - only the real,
// checkable hash is recorded), then caches whether the preset name
// case-insensitively matches "Standard" into the CONFIRMED-initial-
// value global 0x01300b88.
// ---------------------------------------------------------------------
int stub_vcust_set_camera_pos(lua_State* L) {
    logCall(L, upLog(L), "vcust_set_camera_pos", upStateTag(L));
    std::string preset = argString(L, 1);
    auto& vc = upState(L)->vcustCamera();
    if (vc.targetPresent.get()) {       // OPEN until set
        if (vc.targetValid.get()) {     // OPEN until set
            if (vc.targetAlive.get()) { // OPEN until set; true = alive (resolved polarity)
                vc.lastAppliedPresetHash = sr3save::nameHash(preset);
                vc.isStandardActive = caseInsensitiveEquals(preset, "Standard");
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 33. pause_menu_has_seen_display_cal_screen (Sec27.6, 0x007e0490). No
// arguments. Return: 1 boolean. CONFIRMED trivial body: pushes global
// 0x02297c80 (zero at start; no in-scope setter writes it).
// ---------------------------------------------------------------------
int stub_pause_menu_has_seen_display_cal_screen(lua_State* L) {
    logCall(L, upLog(L), "pause_menu_has_seen_display_cal_screen", upStateTag(L));
    lua_pushboolean(L, upState(L)->pauseMenuSeenDisplayCalScreen() ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 34. msn_text_adventure_set_screen (Sec27.7, 0x008410c0). 1 mandatory
// number (screen/state index), rounded. Return: none. CONFIRMED: pure
// global setter, no forwarding call, writes 0x01301124 directly
// (CONFIRMED initial -1).
// ---------------------------------------------------------------------
int stub_msn_text_adventure_set_screen(lua_State* L) {
    logCall(L, upLog(L), "msn_text_adventure_set_screen", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->textAdventureScreenIndex() = static_cast<int32_t>(idx);
    return 0;
}

// ---------------------------------------------------------------------
// 35. horde_results_set_end_action (Sec27.8, 0x007c6370). 1 mandatory
// number, rounded. Return: none. CONFIRMED: forwards to a bare setter of
// global 0x012f3b48 (CONFIRMED initial 2).
// ---------------------------------------------------------------------
int stub_horde_results_set_end_action(lua_State* L) {
    logCall(L, upLog(L), "horde_results_set_end_action", upStateTag(L));
    int64_t action = truncateEa2596(lua_tonumber(L, 1));
    upState(L)->hordeResultsEndAction() = static_cast<int32_t>(action);
    return 0;
}

// ---------------------------------------------------------------------
// 36. garage_preview_vehicle (Sec27.9, 0x005f8890). 1 mandatory number
// (index), rounded. Return: none. CONFIRMED: looks up a vehicle-type id
// by index in a table (0x014a1a80), and if it differs from the cached
// preview value (0x014a1d1c), updates the cache and fires a refresh
// (0x005f8670's own internals OPEN - counted only).
// ---------------------------------------------------------------------
int stub_garage_preview_vehicle(lua_State* L) {
    logCall(L, upLog(L), "garage_preview_vehicle", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    auto& gp = upState(L)->garagePreview();
    int32_t model = gp.vehicleTypeAtIndex.get(std::to_string(idx)); // OPEN until set
    int32_t cached = gp.previewCache.get();                         // OPEN until set
    if (model != cached) {
        gp.previewCache.set(model);
        ++gp.refreshCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 37. game_lobby_coop_finished (Sec27.10, 0x007ab170). No arguments.
// Return: none. CONFIRMED: sets the "co-op lobby finished" global
// (0x0224244c, zero-filled) and issues a fade-out request (duration -1,
// no callback, flag 1 - the exact Sec26.23 signature), reusing the
// existing screen-fade mechanism (Sec26.24).
// ---------------------------------------------------------------------
int stub_game_lobby_coop_finished(lua_State* L) {
    logCall(L, upLog(L), "game_lobby_coop_finished", upStateTag(L));
    upState(L)->coopLobbyFinished() = true;
    upState(L)->screenFadeRequest(true, -1, nullptr, 1); // OPEN if the fade state itself is unset
    return 0;
}

// ---------------------------------------------------------------------
// 38. dialog_box_force_close (Sec27.11, 0x007c4960). 1 mandatory number
// (dialog-type/slot id), rounded. Return: none. CONFIRMED structure
// (407-byte body, summarized per this document's own house style): the
// same self-consistency gate `dialog_box_set_result` (Sec23.18) uses
// must pass; if the slot's timer is armed and not yet expired, the
// close is deferred (counted); otherwise, if not already closing, fires
// the result callback (if any) and a named-callback dispatch (if the
// name isn't blank); finally, if not already fully removed, resets the
// slot. Only the real gates/counts are modelled - the full per-slot
// record and the 0x02282d14 ring's own role (itself still OPEN per the
// spec) are not simulated.
// ---------------------------------------------------------------------
int stub_dialog_box_force_close(lua_State* L) {
    logCall(L, upLog(L), "dialog_box_force_close", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    std::string key = std::to_string(id);
    auto& d = upState(L)->dialogForceClose();
    if (!d.slotMatchesId.get(key)) return 0; // OPEN until set; self-consistency gate failing is a real no-op
    if (d.timerArmedNotExpired.get(key)) {   // OPEN until set
        ++d.closePendingCount;
        return 0;
    }
    if (!d.alreadyClosing.get(key)) {                                    // OPEN until set
        if (d.hasResultCallback.get(key)) ++d.resultCallbackFiredCount;  // OPEN until set
        if (!d.callbackNameBlank.get(key)) ++d.namedCallbackDispatchCount; // OPEN until set
    }
    if (!d.fullyRemoved.get(key)) { // OPEN until set
        ++d.slotResetCount;
        d.fullyRemoved.set(key, true);
    }
    return 0;
}

// ---------------------------------------------------------------------
// 39. game_autosave (Sec27.12, 0x00841f50). No arguments. Return: none.
// CONFIRMED gate (all five must hold): the suppress flag 0x012fcadc
// clear (CONFIRMED file value 1 = suppressed, so a fresh process does
// nothing without a test clearing it), 0x0290ceca clear, no mission
// active, 0x006f8370 false, 0x0229a317 clear. 0x00b94ff0 (the save
// itself) is OPEN per spec as to its OWN internal gates - but
// spec-save-format.md Sec15.2 (added 2026-10-08) independently CONFIRMS,
// from the save-format side, that 0x00b94ff0 bottoms out in the real save
// entry 0x00b94a10 with the file-write branch (Sec6.3) - so once every
// gate above passes, this now ALSO performs a real write of the live
// EngineState::saveGame()/playerCash() state via
// save_game_bridge.h's writeAutosaveSnapshot(), same convention as this
// project's other real writers (never throws; a failure is recorded on
// EngineState::lastAutosaveError() instead of surfacing as a Lua error -
// the real native's own return is unconditionally "none" either way).
// ---------------------------------------------------------------------
int stub_game_autosave(lua_State* L) {
    logCall(L, upLog(L), "game_autosave", upStateTag(L));
    EngineState* st = upState(L);
    auto& a = st->autosave();
    if (!a.suppressFlag.get()) {                   // OPEN until set; CONFIRMED initial true
        if (!a.secondFlagSet.get()) {              // OPEN until set
            if (!a.missionActive.get()) {          // OPEN until set
                if (!a.thirdGateBlocks.get()) {     // OPEN until set
                    if (!a.fourthFlagNonzero.get()) { // OPEN until set
                        ++a.triggeredCount;
                        std::string path, err;
                        writeAutosaveSnapshot(*st, path, err); // Sec15.2/Sec6.3: real file write
                    }
                }
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 40. game_send_pause_menu_player_invite (Sec27.13, 0x008440e0). 1
// mandatory number (player slot), rounded. Return: 1 boolean. CONFIRMED:
// bounds-checks the slot against the shared player-slot count
// (0x02289b94) then reports whether 0x0088e690(record,0) returned zero;
// 0x00d34dd0 is a confirmed inert `return 1` stub (ignored). Out-of-
// range is a well-defined false (no further read needed).
// ---------------------------------------------------------------------
int stub_game_send_pause_menu_player_invite(lua_State* L) {
    logCall(L, upLog(L), "game_send_pause_menu_player_invite", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    bool ok = false;
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) {
        ok = ps.sendInviteOk.get(std::to_string(slot)); // OPEN until set
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 41. game_can_send_player_invite (Sec27.14, 0x00844120). Structural
// sibling of item 40: same bounds check, 0x0101b530 is a confirmed
// inert `return 1` stub (ignored), reports 0x0088dfd0's own boolean
// result.
// ---------------------------------------------------------------------
int stub_game_can_send_player_invite(lua_State* L) {
    logCall(L, upLog(L), "game_can_send_player_invite", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    bool ok = false;
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) {
        ok = ps.canSendInvite.get(std::to_string(slot)); // OPEN until set
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 42. game_set_coop_friendly_fire (Sec27.15, 0x00844510). 1 mandatory
// number (mode 0/1/2), rounded. Return: none. CONFIRMED 3-way enum
// rotation (0->1, 1->2, 2->0 internally; any other input -> 0) applied
// to the real setter 0x007031e0, which only writes when no session is
// present or the local player is the session host (reuses the existing
// coopSession() fields, Sec3.1/Sec8.27 - the same host/solo write gate
// earlier batches already established).
// ---------------------------------------------------------------------
int stub_game_set_coop_friendly_fire(lua_State* L) {
    logCall(L, upLog(L), "game_set_coop_friendly_fire", upStateTag(L));
    int64_t mode = truncateEa2596(lua_tonumber(L, 1));
    int32_t internalValue = 0; // CONFIRMED: both "mode 0" and "invalid" fall through, differing only in the literal
    if (mode == 0) internalValue = 1;
    else if (mode == 1) internalValue = 2;
    else if (mode == 2) internalValue = 0;
    EngineState* st = upState(L);
    bool present = st->coopSession().present.get();                      // OPEN until set
    bool allowed = !present || st->coopSession().localIsHost.get();      // OPEN until set, only read when present
    if (allowed) st->coopFriendlyFireRaw().set(internalValue);
    return 0;
}

// ---------------------------------------------------------------------
// 43. game_get_coop_friendly_fire (Sec27.16, 0x00844580). No arguments.
// Return: 1 number. CONFIRMED: reads the raw internal mode (0x012f4500,
// file value 1) and remaps it 0->2, 1->0, 2->1 (the exact inverse of
// item 42's own rotation); any value outside {0,1,2} pushes 0.
// ---------------------------------------------------------------------
int stub_game_get_coop_friendly_fire(lua_State* L) {
    logCall(L, upLog(L), "game_get_coop_friendly_fire", upStateTag(L));
    int32_t raw = upState(L)->coopFriendlyFireRaw().get(); // CONFIRMED initial 1 (applySpecInitialState)
    int32_t pushed = 0;
    if (raw == 0) pushed = 2;
    else if (raw == 1) pushed = 0;
    else if (raw == 2) pushed = 1;
    lua_pushnumber(L, pushed);
    return 1;
}

// ---------------------------------------------------------------------
// 44. game_send_party_invites (Sec27.17, 0x007c9f50). CONFIRMED: a
// complete, disassembly-confirmed no-op (the shared stub behind several
// bindings this document already records) - no argument read beyond the
// boilerplate, no other call at all.
// ---------------------------------------------------------------------
int stub_game_send_party_invites(lua_State* L) {
    logCall(L, upLog(L), "game_send_party_invites", upStateTag(L));
    return 0;
}

// ---------------------------------------------------------------------
// 45. game_is_connected_to_network (Sec27.18, 0x008427e0). No arguments.
// Return: 1 boolean. CONFIRMED: the call operand is a jump stub to a
// function that returns 1 unconditionally - always true on PC (the
// platform check was compiled out).
// ---------------------------------------------------------------------
int stub_game_is_connected_to_network(lua_State* L) {
    logCall(L, upLog(L), "game_is_connected_to_network", upStateTag(L));
    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------
// 46. game_is_signed_in (Sec27.19, 0x00842810). No arguments. Return: 1
// boolean. CONFIRMED: the call operand is a jump stub that returns true
// iff the Steam user interface pointer (0x0101c54c import) is non-null.
// ---------------------------------------------------------------------
int stub_game_is_signed_in(lua_State* L) {
    logCall(L, upLog(L), "game_is_signed_in", upStateTag(L));
    lua_pushboolean(L, upState(L)->steamInterfaceAvailable().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// 47. game_sign_into_network (Sec27.20, 0x00842840). 1 mandatory
// boolean (read unconditionally), 1 mandatory string (read
// unconditionally - a Lua completion-callback name). Return: none.
// CONFIRMED: caches a resolved context's own field (not separately
// modelled - nothing in this batch's scope reads it back) and the given
// callback name; the real async-callback registration is a confirmed
// empty stub on PC (never fires); unconditionally surfaces the PC
// "cannot sign in" notice. This project has no real platform SDK, so
// only the call's own raw arguments are recorded, honestly, as "a
// request" (same convention as MinimapIconRecord/ObjectIndicatorRecord
// elsewhere in this file).
// ---------------------------------------------------------------------
int stub_game_sign_into_network(lua_State* L) {
    logCall(L, upLog(L), "game_sign_into_network", upStateTag(L));
    bool want = lua_toboolean(L, 1) != 0;
    std::string cb = argString(L, 2);
    upState(L)->signInRequests().push_back({want, cb});
    ++upState(L)->signInErrorDialogCount(); // CONFIRMED unconditional every call
    return 0;
}

// ---------------------------------------------------------------------
// 48. game_show_coop_gamercard (Sec27.21, 0x00844640). 1 mandatory
// number (player slot), rounded. Return: none. CONFIRMED: 0x0101b4a0 is
// a confirmed inert `return 1` stub (ignored); bounds-checks the slot
// against the shared player-slot count (0x02289b94) and, in range,
// forwards to 0x0086fe40/0x00870810 (OPEN internals - counted only).
// ---------------------------------------------------------------------
int stub_game_show_coop_gamercard(lua_State* L) {
    logCall(L, upLog(L), "game_show_coop_gamercard", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) ++ps.gamercardShownCount;
    return 0;
}

// ---------------------------------------------------------------------
// 49. game_main_menu_join_friend_in_progress (Sec27.22, 0x00844670). 1
// mandatory number (friend slot/index), rounded. Return: 1 boolean.
// CONFIRMED: attempts to join (0x007c9410, bounds-checked against the
// same 0x02289b94 count), then calls the confirmed inert `return 1`
// stub 0x0101b4f0 (ignored, closes Sec13.24's own OPEN on it), then
// pushes the earlier-captured boolean.
// ---------------------------------------------------------------------
int stub_game_main_menu_join_friend_in_progress(lua_State* L) {
    logCall(L, upLog(L), "game_main_menu_join_friend_in_progress", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    auto& ps = upState(L)->playerSlots();
    uint32_t count = ps.count.get(); // OPEN until set
    bool joined = false;
    if (slot >= 0 && static_cast<uint64_t>(slot) < count) {
        joined = ps.joinFriendInProgressOk.get(std::to_string(slot)); // OPEN until set
    }
    lua_pushboolean(L, joined ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 50. game_coop_start_new_live (Sec27.23, 0x008425d0). 1 optional
// boolean, nil-gated, DEFAULT TRUE (a notably different default
// polarity from most optional booleans elsewhere in this document).
// Return: none. CONFIRMED: 0x00d2f540 is a confirmed inert `return 1`
// stub (ignored); forwards the boolean to 0x007027b0, which sets the
// "live session" byte (0x014ff6c1) and clears the mutually-exclusive
// "system-link session" byte (0x014ff6c2) when true.
// ---------------------------------------------------------------------
int stub_game_coop_start_new_live(lua_State* L) {
    logCall(L, upLog(L), "game_coop_start_new_live", upStateTag(L));
    bool b = optionalBoolDefault(L, 1, true); // CONFIRMED default true
    upState(L)->coopLiveSessionActive() = b;
    if (b) upState(L)->coopSyslinkSessionActive() = false;
    return 0;
}

// ---------------------------------------------------------------------
// 51. game_coop_start_new_syslink (Sec27.24, 0x00842640). Structural
// sibling of item 50 (byte-for-byte identical shape): 0x00d34d40 is a
// confirmed inert `return 1` stub (ignored); forwards to 0x007027d0,
// which sets the "system-link session" byte and clears "live session".
// ---------------------------------------------------------------------
int stub_game_coop_start_new_syslink(lua_State* L) {
    logCall(L, upLog(L), "game_coop_start_new_syslink", upStateTag(L));
    bool b = optionalBoolDefault(L, 1, true); // CONFIRMED default true
    upState(L)->coopSyslinkSessionActive() = b;
    if (b) upState(L)->coopLiveSessionActive() = false;
    return 0;
}

// ---------------------------------------------------------------------
// 52. game_get_in_progress_type (Sec27.25, 0x008447b0). No arguments.
// Return: 1 number. CONFIRMED 3-branch body: an active mission's own
// +0xa0 type field; else the cat-and-mouse minigame singleton (the SAME
// 0x014c2d10 item 28 reads) -> fixed literal 4; else a phase-6/7
// activity's own +0xa0 field; else -1.
// ---------------------------------------------------------------------
int stub_game_get_in_progress_type(lua_State* L) {
    logCall(L, upLog(L), "game_get_in_progress_type", upStateTag(L));
    EngineState* st = upState(L);
    double result = -1.0;
    if (st->inProgressType().activeMissionPresent.get()) {       // OPEN until set
        result = st->inProgressType().activeMissionType.get();  // OPEN until set
    } else if (st->catMouseMinigame().present.get()) {           // OPEN until set
        result = 4.0; // CONFIRMED fixed literal
    } else if (st->inProgressType().activityPresent.get()) {    // OPEN until set
        result = st->inProgressType().activityType.get();       // OPEN until set
    }
    lua_pushnumber(L, result);
    return 1;
}

// ---------------------------------------------------------------------
// 53. helicopter_set_dont_death_spiral (Sec28.1, 0x00a4cc40). 1
// mandatory string (vehicle/helicopter reference), 1 mandatory boolean
// (no nil-gate). Return: none. CONFIRMED: resolves via the vehicle
// resolver (reuses objectResolves()) and, on success, sets the resolved
// vehicle's own "don't death-spiral" flag (0x00b37740's own gated
// record-vs-replicate split converges on this one Lua-visible flag per
// this batch's no-networking-layer convention).
// ---------------------------------------------------------------------
int stub_helicopter_set_dont_death_spiral(lua_State* L) {
    logCall(L, upLog(L), "helicopter_set_dont_death_spiral", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        upState(L)->helicopterDontDeathSpiral()[name] = flag;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 54. helicopter_fly_to_set_goal_direction (Sec28.2, 0x00a4fa80). 1
// mandatory string (helicopter reference), 1 mandatory string (target
// name - when empty the lookup fails and nothing is applied), 1
// optional boolean, nil-gated, default false (mode: target's own third
// orientation vector vs. delta-to-target). Return: none. CONFIRMED
// structure: resolves the helicopter then gates on the helicopter-
// qualification predicate (0x00ad31a0); resolves the target by name; if
// both resolve, the actual position/orientation arithmetic (OPEN per
// this batch's own scope - nothing reads a computed vector back) is not
// reproduced - only the real branch and whether the apply gate
// (0x00b3f6d0's own AI-record check) passed is recorded, honestly (same
// "record the request" convention as object_indicator_add_do).
// ---------------------------------------------------------------------
int stub_helicopter_fly_to_set_goal_direction(lua_State* L) {
    logCall(L, upLog(L), "helicopter_fly_to_set_goal_direction", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string target = argString(L, 2);
    bool mode = optionalBoolDefault(L, 3, false);
    EngineState* st = upState(L);
    if (st->objectResolves().get(heli)) {                        // OPEN until set
        if (st->helicopterFlyTo().qualifies.get(heli)) {         // OPEN until set
            if (!target.empty() && st->objectResolves().get(target)) { // OPEN until set (only read when target non-empty)
                bool applied = st->helicopterFlyTo().applyGate.get(heli); // OPEN until set
                st->helicopterFlyTo().lastRequest[heli] = {target, mode, applied};
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 55. hdr_bloom_set_multiplier (Sec28.3, 0x00a4cab0). 1 mandatory
// number, no nil-gate, no rounding (narrowed to a single-precision float
// and forwarded directly - CONFIRMED, the Sec4.1 round-cast pair is NOT
// called here). Return: none. CONFIRMED bare global setter (0x012ec280,
// CONFIRMED initial 1.0).
// ---------------------------------------------------------------------
int stub_hdr_bloom_set_multiplier(lua_State* L) {
    logCall(L, upLog(L), "hdr_bloom_set_multiplier", upStateTag(L));
    double raw = lua_tonumber(L, 1);
    upState(L)->hdrBloomMultiplier() = static_cast<float>(raw);
    return 0;
}

// ---------------------------------------------------------------------
// 56. guardian_angel_enable_indicators (Sec28.4, 0x00a4ab80). 1
// mandatory boolean, no nil-gate, no name argument. Return: none.
// CONFIRMED structure: proceeds only when a mode check (0x006d2910)
// reads 3 (OPEN what the mode means) AND a singleton object (0x00614cb0)
// is non-null, then writes the boolean to that object's own +0x57d
// byte.
// ---------------------------------------------------------------------
int stub_guardian_angel_enable_indicators(lua_State* L) {
    logCall(L, upLog(L), "guardian_angel_enable_indicators", upStateTag(L));
    bool flag = lua_toboolean(L, 1) != 0;
    auto& ga = upState(L)->guardianAngel();
    if (ga.modeIsThree.get()) {       // OPEN until set
        if (ga.objectPresent.get()) { // OPEN until set
            ga.indicatorsEnabled = flag;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 57. group_get_next_npc (Sec28.5, 0x00a4c590). 1 mandatory string
// (script-group name), 1 mandatory string (current-NPC character name).
// Return: 1 Lua value (string) with return count 1 on success; return
// count 0 with nothing pushed on every failure path (CONFIRMED).
// CONFIRMED structure: both names resolve via the shared singleton
// (reuses objectResolves()); walks the current NPC's own +0x9c "next"
// link, stopping at the group's own +0x40 head (the SAME ordered member
// roster group_members_alive, Sec18.14, already maintains via
// registerGroupMemberForTesting - no separate membership model
// invented). This project treats an unresolved group OR character as a
// safe "return 0" rather than reproducing the real binary's own
// null-deref fault on a bad group handle with a live character (a
// deliberate, labelled simplification - this project does not simulate
// crashes). NOT the unrelated light-group +0x40 flags byte (Sec17.24) -
// a different structure, untouched here.
// ---------------------------------------------------------------------
int stub_group_get_next_npc(lua_State* L) {
    logCall(L, upLog(L), "group_get_next_npc", upStateTag(L));
    std::string group = argString(L, 1);
    std::string current = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(group)) return 0;   // OPEN until set
    if (!st->objectResolves().get(current)) return 0; // OPEN until set
    const std::string* next = st->groupNextNpcName(group, current);
    if (!next) return 0; // null +0x9c, unregistered member, or wraparound back to the group head
    lua_pushstring(L, next->c_str());
    return 1;
}

// ---------------------------------------------------------------------
// 58. group_get_first_npc (Sec28.6, 0x00a4c520). 1 mandatory string
// (script-group name). Return: 1 Lua value (string) or nothing pushed
// on failure. CONFIRMED: resolves via the SAME singleton resolver; if
// the group's own +0x40 head is non-null, pushes that member's name -
// the first entry of the SAME ordered member roster group_members_alive
// (Sec18.14) already maintains, not a separately-seeded value. An
// unregistered/empty group pushes nothing (a well-defined "no members
// known" answer, the same "collapses to 0" precedent
// groupMembersAliveCount already established for this registry), not an
// OPEN item. NOT the unrelated light-group +0x40 flags byte (Sec17.24).
// ---------------------------------------------------------------------
int stub_group_get_first_npc(lua_State* L) {
    logCall(L, upLog(L), "group_get_first_npc", upStateTag(L));
    std::string group = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(group)) return 0; // OPEN until set
    const std::string* first = st->groupFirstNpcName(group);
    if (!first) return 0;
    lua_pushstring(L, first->c_str());
    return 1;
}

// ---------------------------------------------------------------------
// 59. get_num_humans_in_trigger (Sec28.7, 0x00a4c0c0). 1 mandatory
// string (trigger name). Return: 1 number, always pushed. CONFIRMED:
// resolves the trigger via the shared singleton, then counts a
// category-filtered set of nearby objects inside it via a genuine
// hidden-`this` containment test (full chain re-derived, Sec28.7's own
// 2026-10-01 text); the category/shape-test meanings stay OPEN. An
// unresolved trigger pushes 0 (a well-defined "no matches" answer, not
// a guess).
// ---------------------------------------------------------------------
int stub_get_num_humans_in_trigger(lua_State* L) {
    logCall(L, upLog(L), "get_num_humans_in_trigger", upStateTag(L));
    std::string trigger = argString(L, 1);
    double count = 0.0;
    if (upState(L)->objectResolves().get(trigger)) { // OPEN until set
        count = upState(L)->humansInTriggerCount().get(trigger); // OPEN until set
    }
    lua_pushnumber(L, count);
    return 1;
}

// ---------------------------------------------------------------------
// 60. get_char_vehicle_is_in_air (Sec28.8, 0x00a4b160). 1 mandatory
// string (character reference). Return: 1 boolean, always exactly 1
// value. CONFIRMED: resolves the character (generic/"#PLAYER#" chain,
// reuses objectResolves()), reads the SAME cached active-vehicle id-pair
// Sec24.5 establishes, then queries a clean "in air" predicate
// (0x00ad4040). An unresolved character pushes false.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
// ---------------------------------------------------------------------
int stub_get_char_vehicle_is_in_air(lua_State* L) {
    logCall(L, upLog(L), "get_char_vehicle_is_in_air", upStateTag(L));
    std::string name = argString(L, 1);
    bool inAir = false;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        if (st->vehicleInAirByCharacter().known(resolvedName)) {
            inAir = st->vehicleInAirByCharacter().get(resolvedName); // test/override wins
        } else {
            // Sec28.8 (78fb40a, FINAL): 0x00ad4040 = no static-world contact, no wheel contact, not in water; a
            // flying helicopter is airborne. No contact physics here: a flying-class vehicle (xtbl class 2
            // airplane/3 heli/4 VTOL) counts as airborne until helicopter_land marks it landed or it is in water;
            // a character with no current vehicle, or a ground-class vehicle, reads false.
            const CharacterState& ch = st->getOrCreateCharacter(resolvedName);
            if (ch.currentVehicleName.known() && !ch.currentVehicleName.get().empty()) {
                const VehicleState& v = st->getOrCreateVehicle(ch.currentVehicleName.get());
                int32_t cls = v.flyingType.get(); // OPEN (throws) when the vehicle's class is not known
                inAir = (cls == 2 || cls == 3 || cls == 4) && !v.landed && !v.inWater;
            }
        }
    }
    lua_pushboolean(L, inAir ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 61. effect_play_finisher (Sec28.9, 0x00a48da0). 1 mandatory string
// (target object name), 1 optional number (height/Z offset, nil-gated,
// default 0.0), 1 optional number (a selector, nil-gated, default the
// plain integer 3 - the decompiler's "4.2039e-45" is a mistyped dword
// literal, CONFIRMED - rounded via the round-cast pair). Return: 1 Lua
// value - the number -1.0 on every failure path (the fixed icon
// effect's own index is -1, or the target does not resolve); on
// success, 0x00a48af0's own return value (the Sec9.1/Sec12.10/Sec12.12
// shared finalizer/apply family - not modelled further here, see
// EffectFinisher::successReturn's own doc comment). CONFIRMED: the
// played effect is always the fixed PC/gamepad "icon" effect, never a
// name derived from arg 1 (arg 1 is the TARGET, per Sec28.9's own
// correction). The exact real evaluation order between the two failure
// conditions is not stated by the spec; checking the effect index first
// (an "early exit" shape, not reproducing any specific instruction
// order) avoids an unjustified extra OPEN read when the index is
// already invalid.
// ---------------------------------------------------------------------
int stub_effect_play_finisher(lua_State* L) {
    logCall(L, upLog(L), "effect_play_finisher", upStateTag(L));
    std::string target = argString(L, 1);
    double heightOffset = optionalNumberDefault(L, 2, 0.0);
    (void)heightOffset; // stored on the request (meaning OPEN per spec); not Lua-observable in this scope
    int64_t selector = truncateEa2596(optionalNumberDefault(L, 3, 3.0));
    (void)selector; // stored on the request (+0x6c), own meaning OPEN per spec
    EngineState* st = upState(L);
    // 0x0141250d answered (spec sync bfe0542, Sec28.9 blocker sh02, FINAL): the "last active input device is a
    // gamepad" byte, 0 at load (the same byte game_is_active_input_gamepad returns), set only by the input latch
    // on gamepad activity. On PC with keyboard and mouse it is 0, so the icon is vfx_pc_Icon. A test-set
    // gamepadMode still wins.
    const bool gamepadActive = st->effectFinisher().gamepadMode.known() ? st->effectFinisher().gamepadMode.get()
                                                                         : st->activeInputIsGamepad();
    const char* iconName = gamepadActive ? "vfx_xbox_Icon" : "vfx_pc_Icon";
    // A test-set value wins; else the real effects tables' Name set (spec-tables-environment.md 10.1, 0x005c50b0) says the
    // icon effect exists -> index != -1. A name absent from the tables stays OPEN.
    const bool iconIndexValid = (!st->effectFinisher().effectIndexValid.known(iconName) && st->effectNameInTable(iconName))
                                    ? true
                                    : st->effectFinisher().effectIndexValid.get(iconName); // OPEN until set
    if (!iconIndexValid) {
        lua_pushnumber(L, -1.0); // CONFIRMED failure sentinel
        return 1;
    }
    if (target.empty() || !(st->resolveZoneObject(target) != nullptr || st->objectResolves().get(target))) { // OPEN until set when target non-empty
        lua_pushnumber(L, -1.0);
        return 1;
    }
    // Success: 0x00a48af0 pushes the created instance's packed identifier (spec 28.9 Return line and the sh02
    // note: "return the effect handle"). The packed value is not specified, so a test-set successReturn wins and
    // otherwise the same port-allocated handle effect_play (9.1) returns stands in.
    if (st->effectFinisher().successReturn.known()) {
        lua_pushnumber(L, st->effectFinisher().successReturn.get());
    } else {
        lua_pushnumber(L, static_cast<lua_Number>(st->nextVisualEffectHandle()));
    }
    return 1;
}

// ---------------------------------------------------------------------
// 62. dlc3_m03_set_sprint_waning (Sec28.10, 0x00a46fd0). 1 mandatory
// boolean, no nil-gate, no name argument. Return: none. CONFIRMED bare
// global setter (0x0263ae3a, CONFIRMED zero at start).
// ---------------------------------------------------------------------
int stub_dlc3_m03_set_sprint_waning(lua_State* L) {
    logCall(L, upLog(L), "dlc3_m03_set_sprint_waning", upStateTag(L));
    upState(L)->dlc3SprintWaning() = lua_toboolean(L, 1) != 0;
    return 0;
}

// ---------------------------------------------------------------------
// 63. debris_flow_recycle_object (Sec28.11, 0x00a47fd0). 1 mandatory
// number (debris-flow id, rounded, no nil-gate), 1 mandatory string
// (object reference name). Return: none. CONFIRMED: resolves the object
// via the shared singleton, then hands it to the debris-flow row's own
// third method (0x006fd4f0 - its role is HYPOTHESIS from the name; NOT
// the mirror of debris_flow_add_avoid_object, see Sec28.11's own
// correction). Only the gated call is counted, keyed by flow id.
// ---------------------------------------------------------------------
int stub_debris_flow_recycle_object(lua_State* L) {
    logCall(L, upLog(L), "debris_flow_recycle_object", upStateTag(L));
    int64_t flowId = truncateEa2596(lua_tonumber(L, 1));
    std::string name = argString(L, 2);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        ++upState(L)->debrisFlowRecycleCount()[flowId];
    }
    return 0;
}

// ---------------------------------------------------------------------
// 64. customization_restore_player_rig (Sec28.12, 0x00a43c30). 1
// optional number (player-selector bitmask, nil-gated, default 3 - both
// bits). Return: none. CONFIRMED: bit 0 restores local player 1's body
// rig to the gender-appropriate default file (reading the player's own
// +0xa41 byte, unconditionally - no null-check per spec); bit 1 does
// the same for the co-op player, gated on that player existing
// (0x009df3d0 non-null). Only the gated reads/counts are modelled - the
// actual rig-file load (0x009e3400) is a confirmed shared helper, not
// simulated.
// ---------------------------------------------------------------------
int stub_customization_restore_player_rig(lua_State* L) {
    logCall(L, upLog(L), "customization_restore_player_rig", upStateTag(L));
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    auto& rig = upState(L)->playerRig();
    if (mask & 1) {
        (void)rig.localPlayer1Gender.get(); // OPEN until set - "not null-checked", always read
        ++rig.restorePlayer1Count;
    }
    if (mask & 2) {
        if (rig.coopPlayerPresent.get()) { // CONFIRMED false by default (applySpecInitialState item 13, Sec28.12) - no-op in single player
            (void)rig.coopPlayerGender.get(); // OPEN until set - only reached once a test sets coopPlayerPresent true
            ++rig.restoreCoopCount;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 65. crib_weapon_add_enable (Sec28.13, 0x00a42c20). No arguments
// consumed. Return: none. CONFIRMED: the enable half of a shared setter
// (0x005ee6f0) writing global 0x012ecb34 (CONFIRMED file value 1).
// ---------------------------------------------------------------------
int stub_crib_weapon_add_enable(lua_State* L) {
    logCall(L, upLog(L), "crib_weapon_add_enable", upStateTag(L));
    upState(L)->cribWeaponAddEnabled() = true;
    return 0;
}

// ---------------------------------------------------------------------
// 66. crib_weapon_add_disable (Sec28.14, 0x00a42c40). No arguments
// consumed. Return: none. CONFIRMED: the SAME shared setter as item 65,
// called with the fixed literal 0 instead. (One of the two named
// examples the manager flagged for this batch: a shared setter with
// argument 0, read from this entry's own exact body, not assumed from
// the name alone.)
// ---------------------------------------------------------------------
int stub_crib_weapon_add_disable(lua_State* L) {
    logCall(L, upLog(L), "crib_weapon_add_disable", upStateTag(L));
    upState(L)->cribWeaponAddEnabled() = false;
    return 0;
}

// ---------------------------------------------------------------------
// 67. crib_unlock_strongold (Sec28.15, 0x00a46500). 1 mandatory string
// (stronghold/crib name - the in-binary name is "strongold", not
// "stronghold"). Return: none. CONFIRMED: resolves via a new confirmed
// member of the shared singleton resolver family, then (with a session
// and as host) unlocks only if the stronghold's own "still locked" bit
// is set; without a session or as a client, takes a different,
// internals-OPEN request path (counted only).
// ---------------------------------------------------------------------
int stub_crib_unlock_strongold(lua_State* L) {
    logCall(L, upLog(L), "crib_unlock_strongold", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name)) return 0; // OPEN until set
    bool present = st->coopSession().present.get(); // OPEN until set
    bool hostBranch = present && st->coopSession().localIsHost.get(); // OPEN until set, only read when present
    auto& sh = st->stronghold();
    if (hostBranch) {
        if (sh.stillLocked.get(name)) { // OPEN until set
            sh.stillLocked.set(name, false);
            ++sh.unlockedCount;
        }
    } else {
        ++sh.clientOrSoloRequestCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// 68. continuous_explosion_start (Sec28.16, 0x00a46340). 2 mandatory
// strings (definition name, target/position reference name). Return:
// none. CONFIRMED: resolves arg 1 via a PLAIN cdecl resolver (NOT the
// shared singleton family - a genuine negative, modelled as its own
// dedicated map); resolves arg 2 via the shared singleton; if both
// resolve and no continuous explosion is already active (the ONE global
// state this reconciles with Sec22.24's own stop function), starts it.
// ---------------------------------------------------------------------
int stub_continuous_explosion_start(lua_State* L) {
    logCall(L, upLog(L), "continuous_explosion_start", upStateTag(L));
    std::string defName = argString(L, 1);
    std::string targetName = argString(L, 2);
    EngineState* st = upState(L);
    auto& ce = st->continuousExplosion();
    if (!ce.resolve(defName)) return 0;     // Sec28.16: unknown row name is a silent no-op once the table is installed
    if (!st->objectResolves().get(targetName)) return 0;   // OPEN until set
    if (ce.active.get()) return 0; // OPEN until set; CONFIRMED "ignored if one is already active"
    ce.active.set(true);
    ce.lastDefName = defName;
    ce.lastTargetName = targetName;
    ++ce.startCount;
    return 0;
}

// ---------------------------------------------------------------------
// 69. clear_callbacks_for_obj (Sec28.17, 0x00a46020). 1 mandatory string
// (object reference name, any kind). Return: none. CONFIRMED dispatch
// structure (seven confirmed hidden-`this` findings across six wrapper
// functions, each an N-slot "release when host, write -1" loop over a
// different per-kind hook-array base) - not individually simulated here;
// only that a call cleared callbacks for a resolved object is counted,
// per name (same "record the request" convention as
// minimap_icon_add_do/object_indicator_add_do elsewhere in this file).
// ---------------------------------------------------------------------
int stub_clear_callbacks_for_obj(lua_State* L) {
    logCall(L, upLog(L), "clear_callbacks_for_obj", upStateTag(L));
    std::string name = argString(L, 1);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        ++upState(L)->callbacksClearedCount()[name];
    }
    return 0;
}

// ---------------------------------------------------------------------
// 70. city_zone_swap_is_active (Sec28.18, 0x00a42a60). 1 mandatory
// string (city-zone/pair name). Return: 1 boolean. CONFIRMED: hashes the
// name via the real, general-purpose name hash (sr3save::nameHash -
// confirmed match to 0x00d9e8b0) and checks membership in the active-
// swaps hash list (0x0242dc90) - this project's own real, empty-by-
// default set (Sec1.9's `city_zone_swap` setter is out of this batch's
// own scope; an empty set IS the honest "nothing marked active yet"
// default, not a guess).
// ---------------------------------------------------------------------
int stub_city_zone_swap_is_active(lua_State* L) {
    logCall(L, upLog(L), "city_zone_swap_is_active", upStateTag(L));
    std::string name = argString(L, 1);
    uint32_t h = sr3save::nameHash(name);
    bool active = upState(L)->activeCityZoneSwapHashes().count(h) != 0;
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// 71. character_set_counter_on_grabbed (Sec28.19, 0x00a421a0). 1
// mandatory string (character reference), 1 mandatory boolean (no nil-
// gate). Return: none. CONFIRMED: resolves via the generic/"#PLAYER#"
// chain (reuses objectResolves()); on success, a double-gate (direct
// path or replicate path, abstracted to one flag - see
// CounterOnGrabbed::gatePasses's own doc comment) determines whether
// the flag is actually applied.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject(). (Distinct
// from the unrelated character_set_counter_on_grabbed_by_player, already
// wired in an earlier pass with its own 0x00a28150 citation.)
// ---------------------------------------------------------------------
int stub_character_set_counter_on_grabbed(lua_State* L) {
    logCall(L, upLog(L), "character_set_counter_on_grabbed", upStateTag(L));
    std::string name = argString(L, 1);
    bool flag = lua_toboolean(L, 2) != 0;
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        // CONFIRMED (Sec3.7's own single-player resolution, cross-ref
        // Sec6.22, 2026-10-03): this gate shares the SAME 0x008ae480/
        // 0x008837a0 pair Sec3.7 settles, so it is seeded true on first
        // touch here, the same "seed on first reference, don't clobber a
        // later explicit test override" convention
        // EngineState::getOrCreateCharacter uses for the scalar case - an
        // OpenValueMap has no construction hook of its own to seed through.
        if (!st->counterOnGrabbed().gatePasses.known(resolvedName)) st->counterOnGrabbed().gatePasses.set(resolvedName, true);
        if (st->counterOnGrabbed().gatePasses.get(resolvedName)) { // CONFIRMED true in single player, Sec3.7/Sec28.19
            st->counterOnGrabbed().value[resolvedName] = flag;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 72. character_remove_child_item_by_name (Sec28.20, 0x00a452d0). 1
// mandatory string (character reference), 1 mandatory string (child
// item name). Return: none. CONFIRMED: resolves the character via the
// generic/"#PLAYER#" chain; removes every child item (case-insensitive
// match) from this project's own minimal children-list stand-in (see
// characterChildItems()'s own doc comment for why the two real
// descriptor-bit gates are not separately simulated).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
// ---------------------------------------------------------------------
int stub_character_remove_child_item_by_name(lua_State* L) {
    logCall(L, upLog(L), "character_remove_child_item_by_name", upStateTag(L));
    std::string name = argString(L, 1);
    std::string item = argString(L, 2);
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        auto& items = st->characterChildItems()[resolvedName];
        items.erase(std::remove_if(items.begin(), items.end(),
                                    [&](const std::string& s) { return caseInsensitiveEquals(s, item); }),
                    items.end());
    }
    return 0;
}

// ---------------------------------------------------------------------
// 73. character_get_gender (Sec28.21, 0x00a43090). 1 mandatory string
// (character reference). Return: 1 number, always pushed (0 on resolve
// failure). CONFIRMED: resolves via the generic/"#PLAYER#" chain, then
// reads the resolved character's own +0xa41 byte (independently cross-
// confirmed against item 64's own read of the same field, Sec28.12).
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
// ---------------------------------------------------------------------
int stub_character_get_gender(lua_State* L) {
    logCall(L, upLog(L), "character_get_gender", upStateTag(L));
    std::string name = argString(L, 1);
    double gender = 0.0; // CONFIRMED: 0 on resolve failure
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        gender = st->characterGender().get(resolvedName); // OPEN until set
    }
    lua_pushnumber(L, gender);
    return 1;
}

// ---------------------------------------------------------------------
// 74. character_evacuate_from_all_vehicles (Sec28.22, 0x00a413d0). 1
// mandatory string (character reference), 1 optional boolean, nil-
// gated, default false. Return: none. CONFIRMED: resolves via the
// generic/"#PLAYER#" chain, then forwards the NEGATED boolean plus a
// fixed trailing 0 to a shared occupant-state dispatcher (0x00af3e10,
// already characterized at Sec21.11; its own three-way state dispatch
// is OPEN here, not simulated) - only the gated call is counted.
//
// Resolver 0x00a281a0 - RE-CORRECTED (2026-10-08, direct disassembly of
// its own body): a two-stage #PLAYER#-then-#FOLLOWER# wrapper, not
// PLAYER-only as an earlier reading had it - wired here, follower-
// sentinel-resolver pass, via resolveFollowerAwareObject().
// ---------------------------------------------------------------------
int stub_character_evacuate_from_all_vehicles(lua_State* L) {
    logCall(L, upLog(L), "character_evacuate_from_all_vehicles", upStateTag(L));
    std::string name = argString(L, 1);
    bool arg2 = optionalBoolDefault(L, 2, false);
    (void)arg2; // forwarded in negated sense to 0x00af3e10 - its own dispatch is OPEN, not simulated
    EngineState* st = upState(L);
    std::string resolvedName;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set; may throw OpenStateError for an unresolved-index #FOLLOWER...# literal
        ++st->evacuateFromVehiclesCount()[resolvedName];
    }
    return 0;
}

// ---------------------------------------------------------------------
// 75. cellphone_animate_start_do (Sec28.23, 0x00a46550). No arguments
// consumed (the `lua_gettop` result is read but never used; the pushed
// literal 0 is likewise never read by the callee). Return: none.
// CONFIRMED: when local player 1 exists (reuses the existing
// hasLocalPlayer()) and is not suppressed (0x00943a20), plays the fixed
// "Cell Phone Answer" animation (not itself simulated - only the gated
// play is counted).
// ---------------------------------------------------------------------
int stub_cellphone_animate_start_do(lua_State* L) {
    logCall(L, upLog(L), "cellphone_animate_start_do", upStateTag(L));
    EngineState* st = upState(L);
    if (st->hasLocalPlayer().get()) { // OPEN until set
        if (!st->cellphoneAnimSuppressed().get()) { // OPEN until set
            ++st->cellphoneAnimPlayedCount();
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// 76. boss_battle_matt_get_cheat (Sec28.24, 0x00a40b10). No arguments
// consumed. Return: 1 number, always pushed. CONFIRMED: reads the
// cheat-slot sentinel global (0x012ec730) and pushes it as a double. Its
// only writer in any spec read so far is Sec24.1's `cheats_stop`, out of
// this batch's own scope - stays OPEN until a test (or that function,
// when implemented) sets it.
// ---------------------------------------------------------------------
int stub_boss_battle_matt_get_cheat(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_get_cheat", upStateTag(L));
    int32_t v = upState(L)->bossBattleMatt().cheatSlot.get(); // OPEN until set
    lua_pushnumber(L, static_cast<lua_Number>(v));
    return 1;
}

// ---------------------------------------------------------------------
// 77. boss_battle_matt_cheats_start (Sec28.25, 0x00a40a10). 1 optional
// number (cheat-code id, nil-gated, default 0, via the round-cast pair
// as elsewhere in this document, then clamped so any resolved value
// <= -1 collapses to exactly -1), 1 optional number (nil-gated, default
// 0, NOT rounded - Sec28.25's own text does not say it is), 1 further
// optional number (same), 1 optional boolean, nil-gated, default TRUE.
// Return: none. CONFIRMED: id >= 0 records id/n2/n3 and arms a 3000ms
// deadline; id == -1, gated on the retry counter (0x012ec720) being
// below the CONFIRMED static limit 4, arms a 5000ms (true) or immediate
// (false) deadline instead (the stub call 0x00d2f560 is confirmed
// inert). The shared clock reuses the existing screen-fade host clock
// (the same real global 0x01320d9c both areas cite).
// ---------------------------------------------------------------------
int stub_boss_battle_matt_cheats_start(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_cheats_start", upStateTag(L));
    int64_t id = truncateEa2596(optionalNumberDefault(L, 1, 0.0));
    if (id <= -1) id = -1; // CONFIRMED clamp
    double n2 = optionalNumberDefault(L, 2, 0.0);
    double n3 = optionalNumberDefault(L, 3, 0.0);
    bool flagArg = optionalBoolDefault(L, 4, true); // CONFIRMED default true
    EngineState* st = upState(L);
    auto& bbm = st->bossBattleMatt();
    auto wrapDeadline = [](int64_t clockMs, int64_t addMs) {
        constexpr int64_t kWrap = 1800000000;
        return (clockMs + addMs) % kWrap;
    };
    if (id >= 0) {
        bbm.lastId = static_cast<int32_t>(id);
        bbm.lastN2 = n2;
        bbm.lastN3 = n3;
        bbm.active = true;
        bbm.deadlineMs = wrapDeadline(st->screenFadeClockMs(), 3000);
    } else {
        int32_t retries = bbm.retryCounter.get(); // OPEN until set
        if (retries < EngineState::BossBattleMatt::kRetryLimit) {
            bbm.active = true;
            bbm.deadlineMs = wrapDeadline(st->screenFadeClockMs(), flagArg ? 5000 : 0);
        }
    }
    return 0;
}


} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB02() {
    static const std::vector<std::string> names = {
        // Batch 2026-10-01: spec-lua-api-behaviour.md Sec27 (25 names, all
        // `ui` in tools/lua_all_registered_1490_tagged.txt) and Sec28 (25
        // names, all `gameplay`) - grepped directly, not assumed.
        "cat_mouse_results_select",
        "cell_is_mission_complete",
        "Completion_should_wait_for_coop",
        "Completion_user_is_done_viewing",
        "vcust_set_camera_pos",
        "pause_menu_has_seen_display_cal_screen",
        "msn_text_adventure_set_screen",
        "horde_results_set_end_action",
        "garage_preview_vehicle",
        "game_lobby_coop_finished",
        "dialog_box_force_close",
        "game_autosave",
        "game_send_pause_menu_player_invite",
        "game_can_send_player_invite",
        "game_set_coop_friendly_fire",
        "game_get_coop_friendly_fire",
        "game_send_party_invites",
        "game_is_connected_to_network",
        "game_is_signed_in",
        "game_sign_into_network",
        "game_show_coop_gamercard",
        "game_main_menu_join_friend_in_progress",
        "game_coop_start_new_live",
        "game_coop_start_new_syslink",
        "game_get_in_progress_type",
        "helicopter_set_dont_death_spiral",
        "helicopter_fly_to_set_goal_direction",
        "hdr_bloom_set_multiplier",
        "guardian_angel_enable_indicators",
        "group_get_next_npc",
        "group_get_first_npc",
        "get_num_humans_in_trigger",
        "get_char_vehicle_is_in_air",
        "effect_play_finisher",
        "dlc3_m03_set_sprint_waning",
        "debris_flow_recycle_object",
        "customization_restore_player_rig",
        "crib_weapon_add_enable",
        "crib_weapon_add_disable",
        "crib_unlock_strongold",
        "continuous_explosion_start",
        "clear_callbacks_for_obj",
        "city_zone_swap_is_active",
        "character_set_counter_on_grabbed",
        "character_remove_child_item_by_name",
        "character_get_gender",
        "character_evacuate_from_all_vehicles",
        "cellphone_animate_start_do",
        "boss_battle_matt_get_cheat",
        "boss_battle_matt_cheats_start",
    };
    return names;
}

void registerB02(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {

    // Batch 2026-10-01: spec-lua-api-behaviour.md Sec27/Sec28 (50 names).
    if (wants("cat_mouse_results_select")) registerOne(L, state, log, stateTag, "cat_mouse_results_select", openGuard<stub_cat_mouse_results_select>);
    if (wants("cell_is_mission_complete")) registerOne(L, state, log, stateTag, "cell_is_mission_complete", openGuard<stub_cell_is_mission_complete>);
    if (wants("Completion_should_wait_for_coop")) registerOne(L, state, log, stateTag, "Completion_should_wait_for_coop", openGuard<stub_Completion_should_wait_for_coop>);
    if (wants("Completion_user_is_done_viewing")) registerOne(L, state, log, stateTag, "Completion_user_is_done_viewing", openGuard<stub_Completion_user_is_done_viewing>);
    if (wants("vcust_set_camera_pos")) registerOne(L, state, log, stateTag, "vcust_set_camera_pos", openGuard<stub_vcust_set_camera_pos>);
    if (wants("pause_menu_has_seen_display_cal_screen")) registerOne(L, state, log, stateTag, "pause_menu_has_seen_display_cal_screen", openGuard<stub_pause_menu_has_seen_display_cal_screen>);
    if (wants("msn_text_adventure_set_screen")) registerOne(L, state, log, stateTag, "msn_text_adventure_set_screen", openGuard<stub_msn_text_adventure_set_screen>);
    if (wants("horde_results_set_end_action")) registerOne(L, state, log, stateTag, "horde_results_set_end_action", openGuard<stub_horde_results_set_end_action>);
    if (wants("garage_preview_vehicle")) registerOne(L, state, log, stateTag, "garage_preview_vehicle", openGuard<stub_garage_preview_vehicle>);
    if (wants("game_lobby_coop_finished")) registerOne(L, state, log, stateTag, "game_lobby_coop_finished", openGuard<stub_game_lobby_coop_finished>);
    if (wants("dialog_box_force_close")) registerOne(L, state, log, stateTag, "dialog_box_force_close", openGuard<stub_dialog_box_force_close>);
    if (wants("game_autosave")) registerOne(L, state, log, stateTag, "game_autosave", openGuard<stub_game_autosave>);
    if (wants("game_send_pause_menu_player_invite")) registerOne(L, state, log, stateTag, "game_send_pause_menu_player_invite", openGuard<stub_game_send_pause_menu_player_invite>);
    if (wants("game_can_send_player_invite")) registerOne(L, state, log, stateTag, "game_can_send_player_invite", openGuard<stub_game_can_send_player_invite>);
    if (wants("game_set_coop_friendly_fire")) registerOne(L, state, log, stateTag, "game_set_coop_friendly_fire", openGuard<stub_game_set_coop_friendly_fire>);
    if (wants("game_get_coop_friendly_fire")) registerOne(L, state, log, stateTag, "game_get_coop_friendly_fire", openGuard<stub_game_get_coop_friendly_fire>);
    if (wants("game_send_party_invites")) registerOne(L, state, log, stateTag, "game_send_party_invites", openGuard<stub_game_send_party_invites>);
    if (wants("game_is_connected_to_network")) registerOne(L, state, log, stateTag, "game_is_connected_to_network", openGuard<stub_game_is_connected_to_network>);
    if (wants("game_is_signed_in")) registerOne(L, state, log, stateTag, "game_is_signed_in", openGuard<stub_game_is_signed_in>);
    if (wants("game_sign_into_network")) registerOne(L, state, log, stateTag, "game_sign_into_network", openGuard<stub_game_sign_into_network>);
    if (wants("game_show_coop_gamercard")) registerOne(L, state, log, stateTag, "game_show_coop_gamercard", openGuard<stub_game_show_coop_gamercard>);
    if (wants("game_main_menu_join_friend_in_progress")) registerOne(L, state, log, stateTag, "game_main_menu_join_friend_in_progress", openGuard<stub_game_main_menu_join_friend_in_progress>);
    if (wants("game_coop_start_new_live")) registerOne(L, state, log, stateTag, "game_coop_start_new_live", openGuard<stub_game_coop_start_new_live>);
    if (wants("game_coop_start_new_syslink")) registerOne(L, state, log, stateTag, "game_coop_start_new_syslink", openGuard<stub_game_coop_start_new_syslink>);
    if (wants("game_get_in_progress_type")) registerOne(L, state, log, stateTag, "game_get_in_progress_type", openGuard<stub_game_get_in_progress_type>);
    if (wants("helicopter_set_dont_death_spiral")) registerOne(L, state, log, stateTag, "helicopter_set_dont_death_spiral", openGuard<stub_helicopter_set_dont_death_spiral>);
    if (wants("helicopter_fly_to_set_goal_direction")) registerOne(L, state, log, stateTag, "helicopter_fly_to_set_goal_direction", openGuard<stub_helicopter_fly_to_set_goal_direction>);
    if (wants("hdr_bloom_set_multiplier")) registerOne(L, state, log, stateTag, "hdr_bloom_set_multiplier", openGuard<stub_hdr_bloom_set_multiplier>);
    if (wants("guardian_angel_enable_indicators")) registerOne(L, state, log, stateTag, "guardian_angel_enable_indicators", openGuard<stub_guardian_angel_enable_indicators>);
    if (wants("group_get_next_npc")) registerOne(L, state, log, stateTag, "group_get_next_npc", openGuard<stub_group_get_next_npc>);
    if (wants("group_get_first_npc")) registerOne(L, state, log, stateTag, "group_get_first_npc", openGuard<stub_group_get_first_npc>);
    if (wants("get_num_humans_in_trigger")) registerOne(L, state, log, stateTag, "get_num_humans_in_trigger", openGuard<stub_get_num_humans_in_trigger>);
    if (wants("get_char_vehicle_is_in_air")) registerOne(L, state, log, stateTag, "get_char_vehicle_is_in_air", openGuard<stub_get_char_vehicle_is_in_air>);
    if (wants("effect_play_finisher")) registerOne(L, state, log, stateTag, "effect_play_finisher", openGuard<stub_effect_play_finisher>);
    if (wants("dlc3_m03_set_sprint_waning")) registerOne(L, state, log, stateTag, "dlc3_m03_set_sprint_waning", openGuard<stub_dlc3_m03_set_sprint_waning>);
    if (wants("debris_flow_recycle_object")) registerOne(L, state, log, stateTag, "debris_flow_recycle_object", openGuard<stub_debris_flow_recycle_object>);
    if (wants("customization_restore_player_rig")) registerOne(L, state, log, stateTag, "customization_restore_player_rig", openGuard<stub_customization_restore_player_rig>);
    if (wants("crib_weapon_add_enable")) registerOne(L, state, log, stateTag, "crib_weapon_add_enable", openGuard<stub_crib_weapon_add_enable>);
    if (wants("crib_weapon_add_disable")) registerOne(L, state, log, stateTag, "crib_weapon_add_disable", openGuard<stub_crib_weapon_add_disable>);
    if (wants("crib_unlock_strongold")) registerOne(L, state, log, stateTag, "crib_unlock_strongold", openGuard<stub_crib_unlock_strongold>);
    if (wants("continuous_explosion_start")) registerOne(L, state, log, stateTag, "continuous_explosion_start", openGuard<stub_continuous_explosion_start>);
    if (wants("clear_callbacks_for_obj")) registerOne(L, state, log, stateTag, "clear_callbacks_for_obj", openGuard<stub_clear_callbacks_for_obj>);
    if (wants("city_zone_swap_is_active")) registerOne(L, state, log, stateTag, "city_zone_swap_is_active", openGuard<stub_city_zone_swap_is_active>);
    if (wants("character_set_counter_on_grabbed")) registerOne(L, state, log, stateTag, "character_set_counter_on_grabbed", openGuard<stub_character_set_counter_on_grabbed>);
    if (wants("character_remove_child_item_by_name")) registerOne(L, state, log, stateTag, "character_remove_child_item_by_name", openGuard<stub_character_remove_child_item_by_name>);
    if (wants("character_get_gender")) registerOne(L, state, log, stateTag, "character_get_gender", openGuard<stub_character_get_gender>);
    if (wants("character_evacuate_from_all_vehicles")) registerOne(L, state, log, stateTag, "character_evacuate_from_all_vehicles", openGuard<stub_character_evacuate_from_all_vehicles>);
    if (wants("cellphone_animate_start_do")) registerOne(L, state, log, stateTag, "cellphone_animate_start_do", openGuard<stub_cellphone_animate_start_do>);
    if (wants("boss_battle_matt_get_cheat")) registerOne(L, state, log, stateTag, "boss_battle_matt_get_cheat", openGuard<stub_boss_battle_matt_get_cheat>);
    if (wants("boss_battle_matt_cheats_start")) registerOne(L, state, log, stateTag, "boss_battle_matt_cheats_start", openGuard<stub_boss_battle_matt_cheats_start>);
}

} // namespace stub_sections
} // namespace sr3luahost
