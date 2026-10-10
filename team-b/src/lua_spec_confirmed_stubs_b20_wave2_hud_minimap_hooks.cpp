// lua_spec_confirmed_stubs_b20_wave2_hud_minimap_hooks.cpp
//
// Wave 2, batch W2-8 ("§38-60 gameplay B: character/NPC/inventory/party/
// HUD/minimap/mission-flow/hooks", 93 names) - this is the "HUD/minimap/
// mission-flow/hooks" half (37 names); the "character/NPC/inventory/
// party" half (56 names) is
// lua_spec_confirmed_stubs_b19_wave2_char_npc_inv.cpp - see that file's
// own top comment for the full batch-level rationale (shared EngineState
// additions, hardening policy, AGG-name handling) - not repeated here.
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// ---------------------------------------------------------------------
// tutorial_get_case (Sec38.2, 0x00a60390). 1 string. Returns a number
// (found) or false (not found) - CONFIRMED resolves the name through the
// EXISTING 210-entry tutorialLookup() table and returns that entry's own
// "+0x10 field" (a case number this spec gives no actual data for - an
// OpenValueMap read, refuses honestly via OpenStateError until a test
// sets it, rather than fabricate a number). CONFIRMED crash-shaped in the
// real engine (null name reaches the CRT strcmp family with no check) -
// this project's argString() is already NULL-safe (common.h), so there is
// no structural equivalent to harden here.
// ---------------------------------------------------------------------
int stub_tutorial_get_case(lua_State* L) {
    logCall(L, upLog(L), "tutorial_get_case", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    EngineState::TutorialLookup lk = EngineState::tutorialLookup(name);
    if (lk.index < 0) {
        lua_pushboolean(L, 0); // CONFIRMED: not found -> false
        return 1;
    }
    double caseNumber = st->tutorialCaseNumber().get(EngineState::tutorialStateKey(lk.index)); // OPEN until a test sets it
    lua_pushnumber(L, caseNumber);
    return 1;
}

// ---------------------------------------------------------------------
// tutorial_visible (Sec38.2, 0x00a602e0) - not a pure query. No
// arguments. Returns 1 boolean, gated on "~11 further conditions, not
// individually traced" (CONFIRMED text, genuinely OPEN beyond that) - an
// OpenValue read, refuses honestly until a test sets it. The documented
// side effect (unlinking/re-inserting a tutorial-prompt-queue entry) has
// no backing queue model in this project (OPEN, same gap as
// tutorial_stop_current below).
// ---------------------------------------------------------------------
int stub_tutorial_visible(lua_State* L) {
    logCall(L, upLog(L), "tutorial_visible", upStateTag(L));
    bool visible = upState(L)->tutorialVisible().get(); // OPEN until a test sets it
    lua_pushboolean(L, visible ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// tutorial_stop_current (Sec52.3, tranche 15). CONFIRMED "resolved in
// full; correctly handles all list-unlinking cases and replicates to the
// session host only under specific descriptor conditions, with a
// confirmed client-side gap." This project has no tutorial-PROMPT-QUEUE
// model (a separate, runtime-only mechanism from the 210-entry name
// table tutorial_get_case/_visible share) - call-count only, same
// "deferred infra" convention this codebase already uses elsewhere.
// ---------------------------------------------------------------------
int stub_tutorial_stop_current(lua_State* L) {
    logCall(L, upLog(L), "tutorial_stop_current", upStateTag(L));
    upState(L)->recordTutorialStopCurrent();
    return 0;
}

// ---------------------------------------------------------------------
// minimap_icon_add_script_group_do (Sec42.4, 0x00a54220) CRASH. Group
// name + icon name (no null check in the real engine) + optional strings/
// numbers. No return. HARDENED (CHOSEN divergence): refuses (no-op,
// counted) on an unresolved/never-created group OR a non-string icon
// name - both avoid the real unguarded dereference / unbounded 4-entry
// function-pointer-table index (only flags in {1,2,3} are real-engine
// safe, CONFIRMED). This project has no group-membership roster (see
// EngineState::w28GroupVisibility's own doc comment) so there are no
// per-member icons to actually add either way - the guarded path is a
// genuine no-op beyond the counter.
// ---------------------------------------------------------------------
int stub_minimap_icon_add_script_group_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_add_script_group_do", upStateTag(L));
    std::string group = argString(L, 1);
    EngineState* st = upState(L);
    bool groupOk = st->groupAlreadyCreated(group);
    bool iconNameOk = lua_type(L, 2) == LUA_TSTRING;
    int64_t flags = lua_gettop(L) >= 5 ? truncateEa2596(optionalNumberDefault(L, 5, 3.0)) : 3;
    bool flagsOk = (flags >= 1 && flags <= 3);
    if (!groupOk || !iconNameOk || !flagsOk) {
        st->recordMinimapIconAddScriptGroupGuard();
    }
    return 0;
}

// ---------------------------------------------------------------------
// minimap_icon_remove_script_group_do (Sec42.4, 0x00a544f0) CRASH. Group
// name + optional flags. No return. CONFIRMED correctly guarded against a
// bad group in the real engine, but shares the SAME unbounded 4-entry
// function-pointer-table-by-flags hazard as the add sibling above
// (flags∈{1,2,3} only safe) - HARDENED (CHOSEN divergence) the same way.
// ---------------------------------------------------------------------
int stub_minimap_icon_remove_script_group_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_remove_script_group_do", upStateTag(L));
    std::string group = argString(L, 1);
    EngineState* st = upState(L);
    int64_t flags = lua_gettop(L) >= 2 ? truncateEa2596(optionalNumberDefault(L, 2, 3.0)) : 3;
    bool flagsOk = (flags >= 1 && flags <= 3);
    if (!st->groupAlreadyCreated(group) || !flagsOk) {
        st->recordMinimapIconRemoveScriptGroupGuard();
    }
    return 0;
}

// ---------------------------------------------------------------------
// mission_collectible_collected (Sec42.5, 0x00a523b0). No arguments.
// Returns 1 boolean. CONFIRMED scans a fixed 4-group collectible table
// (20 entries each) and returns the first qualifying entry's own stored
// byte, OR true if nothing qualifies at all. This project has no
// collectible-table data (OPEN) - "nothing qualifies" is therefore the
// ONLY reachable path, so this stub always returns the CONFIRMED
// fallback value true, not a guess about real collectible state.
// ---------------------------------------------------------------------
int stub_mission_collectible_collected(lua_State* L) {
    logCall(L, upLog(L), "mission_collectible_collected", upStateTag(L));
    lua_pushboolean(L, 1); // CONFIRMED fallback: "no blocker found" -> true
    return 1;
}

// ---------------------------------------------------------------------
// mission_help_clear (Sec42.5, 0x00a52ab0). 1 optional number (flags,
// default 3). No return. CONFIRMED bit 1 clears locally; bit 2, only when
// host, broadcasts - single player never hosts a real session
// (Sec8.27/Sec26.28), so only the local-clear half can ever fire here.
// No mission-help display state exists in this project (OPEN, no data) -
// call-count only via logCall.
// ---------------------------------------------------------------------
int stub_mission_help_clear(lua_State* L) {
    logCall(L, upLog(L), "mission_help_clear", upStateTag(L));
    (void)optionalNumberDefault(L, 1, 3.0);
    return 0;
}

// ---------------------------------------------------------------------
// mission_help_table_do (Sec42.5, 0x00a53ba0). 10 arguments (text key, 2
// booleans, 2 substitution strings, 3 numeric flags/values, 2 more
// booleans). No return. No mission-help-text data table exists in this
// project (OPEN) - every argument is read only for correct stack shape
// (matching this project's own established "record the call, not the
// real dispatch" convention, cf. MinimapIconRecord's own doc comment),
// no further side effect modelled.
// ---------------------------------------------------------------------
int stub_mission_help_table_do(lua_State* L) {
    logCall(L, upLog(L), "mission_help_table_do", upStateTag(L));
    (void)argString(L, 1);
    (void)optionalBoolDefault(L, 2, false);
    (void)optionalBoolDefault(L, 3, false);
    (void)optionalStringDefault(L, 4, "");
    (void)optionalStringDefault(L, 5, "");
    (void)optionalNumberDefault(L, 6, 0.0);
    (void)optionalNumberDefault(L, 7, 0.0);
    (void)optionalNumberDefault(L, 8, 0.0);
    (void)optionalBoolDefault(L, 9, false);
    (void)optionalBoolDefault(L, 10, false);
    return 0;
}

// ---------------------------------------------------------------------
// object_indicator_add_script_group_do (Sec42.4, 0x00a59080) CRASH. Group
// name + 2 numbers + optional flags (default 3). No return (the single-
// object sibling returns a boolean; this one doesn't, CONFIRMED).
// HARDENED (CHOSEN divergence): refuses (no-op, counted) on an
// unresolved/never-created group name, avoiding the real unconditional
// null dereference. No member roster exists in this project (see
// w28GroupVisibility's own doc comment), so the guarded path is a
// genuine no-op beyond the counter.
// ---------------------------------------------------------------------
int stub_object_indicator_add_script_group_do(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_add_script_group_do", upStateTag(L));
    std::string group = argString(L, 1);
    (void)truncateEa2596(lua_tonumber(L, 2));
    (void)truncateEa2596(lua_tonumber(L, 3));
    (void)truncateEa2596(optionalNumberDefault(L, 4, 3.0));
    EngineState* st = upState(L);
    if (!st->groupAlreadyCreated(group)) {
        st->recordObjectIndicatorAddScriptGroupGuard();
    }
    return 0;
}

// ---------------------------------------------------------------------
// object_indicator_remove_script_group_do (Sec42.4, 0x00a59220). Group
// name + optional flags (default 3). No return. CONFIRMED correctly
// guarded against a bad group; CONFIRMED over-broad side effect ("removes
// EVERY indicator on each qualifying member, not just the ones this
// script group added") has no member roster to apply to in this project
// (OPEN, see w28GroupVisibility's own doc comment) - safe no-op.
// ---------------------------------------------------------------------
int stub_object_indicator_remove_script_group_do(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_remove_script_group_do", upStateTag(L));
    (void)argString(L, 1);
    (void)optionalNumberDefault(L, 2, 3.0);
    return 0;
}

// ---------------------------------------------------------------------
// on_dismount (Sec42.3, character hook slot 0x13). Callback name + object
// name. No return. CONFIRMED register-only (never fires an event itself);
// empty/nil callback clears the slot. Reuses EngineState::
// onHookNamePool().intern() for realism, stores the raw name directly
// (single-dedicated-field convention, see onDismountCallbacks()'s own
// doc comment).
// ---------------------------------------------------------------------
int stub_on_dismount(lua_State* L) {
    logCall(L, upLog(L), "on_dismount", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string object = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(object)) return 0; // OPEN until set
    if (!callback.empty()) st->onHookNamePool().intern(callback); // CONFIRMED: interned into the shared 350-slot pool
    st->onDismountCallbacks()[object] = callback; // CONFIRMED: empty clears
    return 0;
}

// ---------------------------------------------------------------------
// on_hit_ped (Sec42.3, character slot 0x15 OR vehicle slot 5, depending on
// which resolver matches). Callback name + object name. No return. CHOSEN
// tie-break (this project's flat objectResolves() cannot structurally
// distinguish "is this a vehicle or a character" the way the real two-
// tier resolver does - the SAME tie-break EngineState::radio_block's own
// stub already established, Sec12.20): an already-materialized
// VehicleState record is read as "vehicle"; otherwise "character."
// ---------------------------------------------------------------------
int stub_on_hit_ped(lua_State* L) {
    logCall(L, upLog(L), "on_hit_ped", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string object = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(object)) return 0; // OPEN until set
    if (!callback.empty()) st->onHookNamePool().intern(callback);
    if (st->hasVehicle(object) && !st->hasCharacter(object)) { // CHOSEN tie-break, see doc comment
        st->onHitPedVehicleCallbacks()[object] = callback;
    } else {
        st->onHitPedCharacterCallbacks()[object] = callback;
    }
    return 0;
}

// ---------------------------------------------------------------------
// on_m21_player_choice (Sec42.3). Callback name (opaque). No return.
// CONFIRMED a hand-inlined registration against a GLOBAL hook record (not
// an object), HIGH CONFIDENCE a general player-level/UI hook slot.
// ---------------------------------------------------------------------
int stub_on_m21_player_choice(lua_State* L) {
    logCall(L, upLog(L), "on_m21_player_choice", upStateTag(L));
    std::string callback = argString(L, 1);
    EngineState* st = upState(L);
    if (!callback.empty()) st->onHookNamePool().intern(callback);
    st->onM21PlayerChoiceCallback() = callback; // CONFIRMED: empty clears (family-wide convention)
    return 0;
}

// ---------------------------------------------------------------------
// on_mount (Sec42.3, 0x00a580f0) - real inverted-probe quirk. Callback
// name + object name. No return. CONFIRMED: first checks whether the
// target resolves through the GENERIC/"#PLAYER#" chain and registers the
// hook ONLY IF THAT CHECK FAILS - so on_mount(cb, "#PLAYER#") is a
// confirmed, silent no-op. CHOSEN (HIGH CONFIDENCE by direct structural
// analogy to this project's own already-implemented on_ai_do_action_
// complete, Sec14.7, which shares the IDENTICAL two-tier shape): the
// plain-character fallback path registers the hook; the real hook-slot
// NUMBER is OPEN (not given anywhere in this spec).
// ---------------------------------------------------------------------
int stub_on_mount(lua_State* L) {
    logCall(L, upLog(L), "on_mount", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string object = argString(L, 2);
    EngineState* st = upState(L);
    if (st->playerClassChainResolves(object)) { // CHOSEN default now applies when no test .set() it - CONFIRMED: generic/#PLAYER# match => do nothing
        return 0;
    }
    if (st->objectResolves().get(object)) { // OPEN until set - plain fallback resolve
        if (!callback.empty()) st->onHookNamePool().intern(callback);
        st->onMountCallbacks()[object] = callback;
    }
    return 0;
}

// ---------------------------------------------------------------------
// on_vehicle_debris_flow_recycled (Sec42.3, vehicle hook slot 0xb).
// Callback name + vehicle name. No return.
// ---------------------------------------------------------------------
int stub_on_vehicle_debris_flow_recycled(lua_State* L) {
    logCall(L, upLog(L), "on_vehicle_debris_flow_recycled", upStateTag(L));
    std::string callback = argString(L, 1);
    std::string vehicle = argString(L, 2);
    EngineState* st = upState(L);
    if (!st->objectResolves().get(vehicle)) return 0; // OPEN until set
    if (!callback.empty()) st->onHookNamePool().intern(callback);
    st->onVehicleDebrisFlowRecycledCallbacks()[vehicle] = callback;
    return 0;
}

// ---------------------------------------------------------------------
// open_vint_dialog_check_done (Sec42.6, corrected Sec50.3). No arguments.
// Returns 1 number: file-default -2 ("pending"); CORRECTED (Sec50.3): the
// completion callback only ever writes the literal -1 (never an option
// index) - so this can only ever return -2 or -1.
// ---------------------------------------------------------------------
int stub_open_vint_dialog_check_done(lua_State* L) {
    logCall(L, upLog(L), "open_vint_dialog_check_done", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->vintDialogResult()));
    return 1;
}

// ---------------------------------------------------------------------
// hud_get_text_adventure_current_screen_index (Sec41.4, 0x00a4dda0). No
// arguments. Returns 1 number. CONFIRMED a consume-once read: reads a
// global dword and immediately resets it to -1.
// ---------------------------------------------------------------------
int stub_hud_get_text_adventure_current_screen_index(lua_State* L) {
    logCall(L, upLog(L), "hud_get_text_adventure_current_screen_index", upStateTag(L));
    EngineState* st = upState(L);
    int32_t value = st->hudTextAdventureScreenIndex();
    st->hudTextAdventureScreenIndex() = -1; // CONFIRMED: consume-once
    lua_pushnumber(L, static_cast<lua_Number>(value));
    return 1;
}

// ---------------------------------------------------------------------
// hud_x_of_y_remove (Sec41.4, 0x00a4e9e0) CRASH - unbounded index reaches
// a wild indirect call. 1 number (index). No return. HARDENED (CHOSEN
// divergence): bound-checked to the CONFIRMED real 0-3 slot range (from
// hud_x_of_y_add's own "correctly rejects indices outside 0-3") - refused
// and counted outside that range, instead of the real wild indirect call.
// ---------------------------------------------------------------------
int stub_hud_x_of_y_remove(lua_State* L) {
    logCall(L, upLog(L), "hud_x_of_y_remove", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    if (slot < 0 || slot >= static_cast<int64_t>(EngineState::kHudXOfYSlotCount)) {
        st->recordHudXOfYRemoveOobGuard(); // HARDENED (CHOSEN divergence): avoid the real wild indirect call
        return 0;
    }
    st->hudXOfYSlots()[static_cast<size_t>(slot)] = EngineState::HudXOfYSlot{};
    return 0;
}

// ---------------------------------------------------------------------
// hud_x_of_y_add (Sec54.4, aggregate cross-reference only - "correctly
// rejects indices outside 0-3"). CHOSEN argument shape: slot + current +
// total (the natural "X of Y" triple; no individuated text gives this
// native's own exact argument list, only its sibling comparison).
// ---------------------------------------------------------------------
int stub_hud_x_of_y_add(lua_State* L) {
    logCall(L, upLog(L), "hud_x_of_y_add", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    double current = optionalNumberDefault(L, 2, 0.0);
    double total = optionalNumberDefault(L, 3, 0.0);
    EngineState* st = upState(L);
    if (slot < 0 || slot >= static_cast<int64_t>(EngineState::kHudXOfYSlotCount)) {
        st->recordHudXOfYAddOobGuard(); // CONFIRMED: the real engine itself rejects this range
        return 0;
    }
    auto& s = st->hudXOfYSlots()[static_cast<size_t>(slot)];
    s.active = true;
    s.current = current;
    s.total = total;
    return 0;
}

// ---------------------------------------------------------------------
// hud_x_of_y_update (Sec54.3, tranche 19) CRASH - real script-controlled
// out-of-bounds write, worse than hud_x_of_y_remove (that one only
// reads). 2 numbers (slot index, new count). CONFIRMED the real engine
// applies ZERO bound/sign checking at all. HARDENED (CHOSEN divergence):
// bound-checked to the SAME 0-3 range hud_x_of_y_add already enforces,
// refused and counted outside it.
// ---------------------------------------------------------------------
int stub_hud_x_of_y_update(lua_State* L) {
    logCall(L, upLog(L), "hud_x_of_y_update", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    double newCount = lua_tonumber(L, 2);
    EngineState* st = upState(L);
    if (slot < 0 || slot >= static_cast<int64_t>(EngineState::kHudXOfYSlotCount)) {
        st->recordHudXOfYUpdateOobGuard(); // HARDENED (CHOSEN divergence): avoid the real OOB write
        return 0;
    }
    st->hudXOfYSlots()[static_cast<size_t>(slot)].current = newCount;
    return 0;
}

// ---------------------------------------------------------------------
// flashpoint_mission_status (Sec45.2, 0x00a49b00). No arguments (none
// given in this spec text). Returns 1 number: combines a local status
// dword and a peer status dword (4=complete, 1=idle/reset, 2=started,
// 3=HYPOTHESIS failed/aborted) with a documented combine rule "favoring
// in-progress when the two sides disagree" - CHOSEN literal reading: the
// two statuses agree -> that value; disagree -> 2 (started/in progress).
// Single player never has a real peer (Sec8.27/Sec26.28), so peerStatus
// is simply kept equal to localStatus by flashpoint_start_mission below -
// this combine rule is still implemented in full for when a test sets
// the two fields independently.
// ---------------------------------------------------------------------
int stub_flashpoint_mission_status(lua_State* L) {
    logCall(L, upLog(L), "flashpoint_mission_status", upStateTag(L));
    auto& fp = upState(L)->flashpointState();
    int32_t result = (fp.localStatus == fp.peerStatus) ? fp.localStatus : 2; // CHOSEN literal reading, see doc comment
    lua_pushnumber(L, static_cast<lua_Number>(result));
    return 1;
}

// ---------------------------------------------------------------------
// flashpoint_start_mission (Sec55.5). 1 id (CHOSEN number, "starts a
// flashpoint by id"). No return given. CONFIRMED sets two state globals
// (locally AND remotely, single player taking only the local path per
// Sec8.27/Sec26.28) and suspends every other active flashpoint (OPEN - no
// world-flashpoint-list model in this project). The id is stored raw
// rather than resolved against a real list (CHOSEN simplification, OPEN
// no list exists).
// ---------------------------------------------------------------------
int stub_flashpoint_start_mission(lua_State* L) {
    logCall(L, upLog(L), "flashpoint_start_mission", upStateTag(L));
    int64_t id = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& fp = st->flashpointState();
    fp.localStatus = 2; // CONFIRMED: "started" (HIGH CONFIDENCE)
    fp.peerStatus = fp.localStatus; // CHOSEN: no real peer in single player, see doc comment
    fp.activeId = std::to_string(id);
    return 0;
}

// ---------------------------------------------------------------------
// action_sequence_setup (Sec46.2). Optional vehicle name. No return.
// CONFIRMED: with no vehicle name, clears the sequence in single player
// (this project's whole scope); CONFIRMED leaks one control-lock slot on
// every repeated non-clearing call (reproduced via the monotonic
// controlLockLeakCount, never decremented - no release function is in
// this batch's own 93 names).
// ---------------------------------------------------------------------
int stub_action_sequence_setup(lua_State* L) {
    logCall(L, upLog(L), "action_sequence_setup", upStateTag(L));
    std::string vehicle = optionalStringDefault(L, 1, "");
    auto& seq = upState(L)->actionSequenceScript();
    if (vehicle.empty()) {
        seq.active = false; // CONFIRMED: single-player clears on no vehicle name
        seq.vehicleName.clear();
    } else {
        seq.active = true;
        seq.vehicleName = vehicle;
        ++seq.controlLockLeakCount; // CONFIRMED: leaks a slot, never released in this batch's own scope
    }
    return 0;
}

// ---------------------------------------------------------------------
// action_sequence_is_playing (Sec46.2) CRASH - real null-write crash.
// No arguments. Returns 1 boolean. CONFIRMED a hidden side effect: always
// resets the local player's script mode, reaching a null+0x2888 write
// with no local player. HARDENED (CHOSEN divergence): the side effect is
// only performed (as a no-op-shaped, test-observable counter) when
// hasLocalPlayer() is known true; otherwise refused, counted.
// ---------------------------------------------------------------------
int stub_action_sequence_is_playing(lua_State* L) {
    logCall(L, upLog(L), "action_sequence_is_playing", upStateTag(L));
    EngineState* st = upState(L);
    if (st->hasLocalPlayer().known() && st->hasLocalPlayer().get()) {
        // CONFIRMED side effect: resets the local player's own script mode -
        // this project tracks no "script mode" field to actually clear, so
        // reaching this branch (the real engine's own safe case) is a no-op
        // beyond having avoided the HARDENED refusal below.
    } else {
        st->recordActionSequenceIsPlayingNullPlayerGuard(); // HARDENED (CHOSEN divergence): avoid the real null write
    }
    lua_pushboolean(L, upState(L)->actionSequenceScript().active ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// action_sequence_advance (Sec46.2). No arguments documented beyond the
// shared state block. No return. CONFIRMED never waits for its own
// animations to finish - a script must poll action_sequence_is_playing
// itself. No real animation/step-table model exists in this project
// (OPEN, hard-coded per-mission data) - call-count only.
// ---------------------------------------------------------------------
int stub_action_sequence_advance(lua_State* L) {
    logCall(L, upLog(L), "action_sequence_advance", upStateTag(L));
    ++upState(L)->actionSequenceScript().advanceCount;
    return 0;
}

// ---------------------------------------------------------------------
// qte_start_m02_skyqte_01 (Sec51.1, tranche 17) CRASH - the clearest
// same-function sibling-asymmetry crash in the series. 1 character + 1
// callback name + 2 optional names (partner, point). No return given.
// CONFIRMED the underlying worker checks its optional partner argument on
// the remote (forwarded) branch but NOT on the local branch - reachable
// only for a locally-owned character, which this project's single-player
// scope always is. Sec51.1's own text: "a nil, misspelled, despawned, or
// dead partner argument crashes" - all 4 named shapes. CORRECTED
// 2026-10-08 (review of this batch): a prior version of this guard only
// refused a partner that was PRESENT but failed to resolve (misspelled/
// despawned), silently falling through to recordQteSkyqte01Start() for an
// omitted/nil partner (this argument IS optional, so this was the most
// common real case, not an edge case) and never checking a resolved-but-
// dead partner at all - missing 2 of the 4 named crash shapes. HARDENED
// (CHOSEN divergence): an omitted/nil partner OR a partner that fails to
// resolve is refused via the SAME qteSkyqte01NullPartnerGuardCount_
// counter (both are the identical real null-dereference shape - this
// project doesn't split "nil" from "unresolved name" into separate
// counters, same convention as every other nil-gated optional-name
// argument elsewhere in this codebase); a partner that resolves but is
// dead is refused via its own qteSkyqte01DeadPartnerGuardCount_ counter
// (a genuinely distinct code path - reads CharacterState rather than just
// objectResolves()), reusing CharacterState::notDeadForActionPlay() - see
// EngineState::qteSkyqte01DeadPartnerGuardCount()'s own doc comment for
// why that predicate, not isDeadHighConfidence, is the right reuse here.
// ---------------------------------------------------------------------
int stub_qte_start_m02_skyqte_01(lua_State* L) {
    logCall(L, upLog(L), "qte_start_m02_skyqte_01", upStateTag(L));
    std::string character = argString(L, 1);
    (void)argString(L, 2); // callback name, opaque
    std::string partner = optionalStringDefault(L, 3, "");
    (void)optionalStringDefault(L, 4, ""); // point, opaque
    EngineState* st = upState(L);
    if (!st->objectResolves().get(character)) return 0; // OPEN until set
    if (partner.empty() || !st->objectResolves().get(partner)) { // HARDENED: nil/omitted OR unresolved
        st->recordQteSkyqte01NullPartnerGuard();
        return 0;
    }
    if (!st->getOrCreateCharacter(partner).notDeadForActionPlay()) { // HARDENED: resolved but dead
        st->recordQteSkyqte01DeadPartnerGuard();
        return 0;
    }
    st->recordQteSkyqte01Start();
    return 0;
}

// ---------------------------------------------------------------------
// message_remove (Sec50.2, tranche 18). 1 number (a message id, as
// returned by the already-registered message() native). No return.
// CONFIRMED "fully bounds- and generation-checked, no crash shape" - the
// existing message() stub models its own id as a bare monotonic handle
// with no backing slot array (confirmed by research), so there is
// nothing for this native to actually mutate; implemented as a safe call
// counter, consistent with that existing simplification.
// ---------------------------------------------------------------------
int stub_message_remove(lua_State* L) {
    logCall(L, upLog(L), "message_remove", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1));
    upState(L)->recordMessageRemove();
    return 0;
}

// ---------------------------------------------------------------------
// minimap_icon_add_radius_do (Sec50.2, tranche 18) CRASH - real
// null-object-read crash. 1 object name + optional radius/selector
// (CHOSEN default 3, matching the sibling minimap_icon_remove_do's own
// CONFIRMED default). HARDENED (CHOSEN divergence): refuses (no-op,
// counted) on an unresolved object name OR a selector outside {1,2,3} -
// both avoid the real crash shapes (a null handle read at address 8; an
// unranged 4-entry function-pointer-table index). Reuses the EXISTING
// CharacterState::minimapIcons field, same minimal "record the request"
// convention as the already-registered minimap_icon_add_do.
// ---------------------------------------------------------------------
int stub_minimap_icon_add_radius_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_add_radius_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t selector = lua_gettop(L) >= 2 ? truncateEa2596(optionalNumberDefault(L, 2, 3.0)) : 3;
    EngineState* st = upState(L);
    if (!st->objectResolves().get(name) || selector < 1 || selector > 3) { // HARDENED (CHOSEN divergence)
        st->recordMinimapIconAddRadiusGuard();
        return 0;
    }
    MinimapIconRecord rec;
    rec.iconType = "radius";
    rec.flag5 = selector;
    st->getOrCreateCharacter(name).minimapIcons.push_back(std::move(rec));
    return 0;
}

// ---------------------------------------------------------------------
// minimap_icon_remove_radius_do (Sec50 intro) - CONFIRMED byte-for-byte
// the ALREADY-REGISTERED minimap_icon_remove_do's own handler
// (lua_spec_confirmed_stubs_b04_r04_r06_08.cpp's stub_minimap_icon_
// remove_do: "the registrar slot is the ground truth, the name is
// misleading"). Reproduced verbatim here (3-line body) rather than
// cross-TU-aliased: that existing stub has internal linkage (anonymous
// namespace) in its own translation unit, per this project's established
// split architecture (lua_spec_confirmed_stubs_registry.cpp's own top
// comment), so it cannot be named from this file - this is NOT a separate
// implementation decision, only the same logic copied to satisfy the
// single-TU-visibility constraint.
// ---------------------------------------------------------------------
int stub_minimap_icon_remove_radius_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_remove_radius_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    (void)mask; // the real per-kind dispatch/unranged table access is not modeled - identical to minimap_icon_remove_do
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->getOrCreateCharacter(name).minimapIcons.clear();
    }
    return 0;
}

// ---------------------------------------------------------------------
// mission_maybe_uncomplete_m22 (Sec50.6, tranche 18). No arguments.
// CONFIRMED gated on an XOR over two specific follow-on missions' own
// progress (this project has no mission-progression model, OPEN) and
// only does so on the host (never true in single player, Sec8.27/
// Sec26.28) - but broadcasts from every machine regardless of host
// status, reproduced via EngineState::replicateStateChange.
// ---------------------------------------------------------------------
int stub_mission_maybe_uncomplete_m22(lua_State* L) {
    logCall(L, upLog(L), "mission_maybe_uncomplete_m22", upStateTag(L));
    EngineState* st = upState(L);
    st->recordMissionMaybeUncompleteM22();
    EngineState::replicateStateChange("mission_maybe_uncomplete_m22", ""); // CONFIRMED: unconditional broadcast
    return 0;
}

// ---------------------------------------------------------------------
// open_vint_dialog_do (Sec50.2, tranche 18) CRASH. CHOSEN argument shape:
// 1 optional dialog-identifier string (no individuated argument list is
// given). CONFIRMED shares the completion mechanism open_vint_dialog_
// check_done reads (resets the pending state to -2). CONFIRMED a real gap
// (help-text table pointer never null-checked) has no structural
// equivalent in this project - no help-text table exists here to
// dereference either way (trivially satisfied, not a guessed fix).
// ---------------------------------------------------------------------
int stub_open_vint_dialog_do(lua_State* L) {
    logCall(L, upLog(L), "open_vint_dialog_do", upStateTag(L));
    (void)optionalStringDefault(L, 1, "");
    EngineState* st = upState(L);
    st->vintDialogResult() = -2; // CONFIRMED: a fresh dialog open starts "pending" again
    st->recordOpenVintDialogDo();
    return 0;
}

// ---------------------------------------------------------------------
// homie_mission_unlock (Sec54.4, tranche 19). CHOSEN argument shape: 1
// string id (this project's majority naming convention). CONFIRMED only
// "an activity-record override that, paired with its lock sibling
// (homie_mission_lock below), has no way to restore the record's
// original default state through either binding" - no reset/clear
// accessor is provided, matching that exactly.
// ---------------------------------------------------------------------
int stub_homie_mission_unlock(lua_State* L) {
    logCall(L, upLog(L), "homie_mission_unlock", upStateTag(L));
    std::string id = argString(L, 1);
    upState(L)->homieMissionOverride()[id] = 2; // 2 = unlocked
    return 0;
}

// ---------------------------------------------------------------------
// homie_mission_lock (Sec53, AGG). NO individuated spec text exists for
// this specific name anywhere in spec-lua-api-behaviour.md - the ONLY
// available text is homie_mission_unlock's own aggregate mention (Sec54.4,
// above), which names this as its "lock sibling." Implemented as the
// inverse override, same "no restore" shape, per that one sentence - the
// fullest reading this spec text supports for an AGG name (not a guess
// about this native's own internals beyond what its sibling's text gives).
// ---------------------------------------------------------------------
int stub_homie_mission_lock(lua_State* L) {
    logCall(L, upLog(L), "homie_mission_lock", upStateTag(L));
    std::string id = argString(L, 1);
    upState(L)->homieMissionOverride()[id] = 1; // 1 = locked
    return 0;
}

// ---------------------------------------------------------------------
// hud_get_proto_selection (Sec54.4, tranche 19). No arguments. CONFIRMED
// "a sticky poll result" (does NOT consume-once, unlike
// hud_get_text_adventure_current_screen_index above).
// ---------------------------------------------------------------------
int stub_hud_get_proto_selection(lua_State* L) {
    logCall(L, upLog(L), "hud_get_proto_selection", upStateTag(L));
    lua_pushnumber(L, static_cast<lua_Number>(upState(L)->hudProtoSelection()));
    return 1;
}

// ---------------------------------------------------------------------
// hud_pop_screen (Sec54.4, tranche 19). 1 screen name. No return.
// CONFIRMED only actually pops when the named screen is on top of the
// HUD stack, but broadcasts the "pop" notification regardless of whether
// the local pop happened.
// ---------------------------------------------------------------------
int stub_hud_pop_screen(lua_State* L) {
    logCall(L, upLog(L), "hud_pop_screen", upStateTag(L));
    std::string screen = argString(L, 1);
    EngineState* st = upState(L);
    auto& stack = st->hudScreenStack();
    if (!stack.empty() && stack.back() == screen) {
        stack.pop_back(); // CONFIRMED: only pops when on top
    }
    EngineState::replicateStateChange("hud_pop_screen", screen); // CONFIRMED: unconditional broadcast regardless
    return 0;
}

// ---------------------------------------------------------------------
// add_object_indicator_to_closest_npc (Sec58, tranche 25) - naming trap,
// returns a name, not a boolean. Group name + reference character name.
// Returns 1 string: the closest living group member's own name, or the
// empty string "" (never nil, never false) when nothing qualifies
// (CONFIRMED). This project has no script-group membership roster at all
// (see EngineState::w28GroupVisibility's own doc comment) - the search
// therefore always finds zero qualifying members, so this stub always
// returns the CONFIRMED real "nothing qualifies" fallback value, not an
// invented member name.
// ---------------------------------------------------------------------
int stub_add_object_indicator_to_closest_npc(lua_State* L) {
    logCall(L, upLog(L), "add_object_indicator_to_closest_npc", upStateTag(L));
    (void)argString(L, 1); // group name - no membership roster in scope, see doc comment
    (void)argString(L, 2); // reference character
    lua_pushstring(L, ""); // CONFIRMED real fallback for "nothing qualifies"
    return 1;
}

// ---------------------------------------------------------------------
// on_mission_item_drop (Sec47, 0x00a57000) - resolved despite zero
// observed call sites in Team B's own corpus. 1 string argument (a Lua
// callback name); no return. CONFIRMED: if present and non-empty, it is
// interned into the SAME global, 350-slot name-hash pool already shared
// by the 5 on_* hooks above (EngineState::onHookNamePool()) - the
// resulting persistent global is CONFIRMED NOT reset when the argument is
// nil/empty (a no-argument call simply reuses whatever name the previous
// real call registered) - so, unlike every on_* sibling above, an empty
// argument here must NOT clear the field. CONFIRMED every call,
// regardless of whether a new name was supplied, unconditionally
// broadcasts via the standard commit trio - reproduced via
// EngineState::replicateStateChange.
// ---------------------------------------------------------------------
int stub_on_mission_item_drop(lua_State* L) {
    logCall(L, upLog(L), "on_mission_item_drop", upStateTag(L));
    std::string callback = optionalStringDefault(L, 1, "");
    EngineState* st = upState(L);
    if (!callback.empty()) {
        st->onHookNamePool().intern(callback); // CONFIRMED: interned only when present and non-empty
        st->onMissionItemDropCallback() = callback; // CONFIRMED: NOT reset on empty/nil, see doc comment
    }
    EngineState::replicateStateChange("on_mission_item_drop", ""); // CONFIRMED: unconditional broadcast, every call
    return 0;
}

} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB20() {
    static const std::vector<std::string> names = {
        "tutorial_get_case",
        "tutorial_visible",
        "minimap_icon_add_script_group_do",
        "minimap_icon_remove_script_group_do",
        "mission_collectible_collected",
        "mission_help_clear",
        "mission_help_table_do",
        "object_indicator_add_script_group_do",
        "object_indicator_remove_script_group_do",
        "on_dismount",
        "on_hit_ped",
        "on_m21_player_choice",
        "on_mount",
        "on_vehicle_debris_flow_recycled",
        "open_vint_dialog_check_done",
        "hud_get_text_adventure_current_screen_index",
        "hud_x_of_y_remove",
        "flashpoint_mission_status",
        "action_sequence_advance",
        "action_sequence_is_playing",
        "action_sequence_setup",
        "tutorial_stop_current",
        "qte_start_m02_skyqte_01",
        "message_remove",
        "minimap_icon_add_radius_do",
        "minimap_icon_remove_radius_do",
        "mission_maybe_uncomplete_m22",
        "open_vint_dialog_do",
        "homie_mission_unlock",
        "hud_get_proto_selection",
        "hud_pop_screen",
        "hud_x_of_y_add",
        "hud_x_of_y_update",
        "homie_mission_lock",
        "flashpoint_start_mission",
        "add_object_indicator_to_closest_npc",
        "on_mission_item_drop",
    };
    return names;
}

void registerB20(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("tutorial_get_case")) registerOne(L, state, log, stateTag, "tutorial_get_case", openGuard<stub_tutorial_get_case>);
    if (wants("tutorial_visible")) registerOne(L, state, log, stateTag, "tutorial_visible", openGuard<stub_tutorial_visible>);
    if (wants("minimap_icon_add_script_group_do")) registerOne(L, state, log, stateTag, "minimap_icon_add_script_group_do", openGuard<stub_minimap_icon_add_script_group_do>);
    if (wants("minimap_icon_remove_script_group_do")) registerOne(L, state, log, stateTag, "minimap_icon_remove_script_group_do", openGuard<stub_minimap_icon_remove_script_group_do>);
    if (wants("mission_collectible_collected")) registerOne(L, state, log, stateTag, "mission_collectible_collected", openGuard<stub_mission_collectible_collected>);
    if (wants("mission_help_clear")) registerOne(L, state, log, stateTag, "mission_help_clear", openGuard<stub_mission_help_clear>);
    if (wants("mission_help_table_do")) registerOne(L, state, log, stateTag, "mission_help_table_do", openGuard<stub_mission_help_table_do>);
    if (wants("object_indicator_add_script_group_do")) registerOne(L, state, log, stateTag, "object_indicator_add_script_group_do", openGuard<stub_object_indicator_add_script_group_do>);
    if (wants("object_indicator_remove_script_group_do")) registerOne(L, state, log, stateTag, "object_indicator_remove_script_group_do", openGuard<stub_object_indicator_remove_script_group_do>);
    if (wants("on_dismount")) registerOne(L, state, log, stateTag, "on_dismount", openGuard<stub_on_dismount>);
    if (wants("on_hit_ped")) registerOne(L, state, log, stateTag, "on_hit_ped", openGuard<stub_on_hit_ped>);
    if (wants("on_m21_player_choice")) registerOne(L, state, log, stateTag, "on_m21_player_choice", openGuard<stub_on_m21_player_choice>);
    if (wants("on_mount")) registerOne(L, state, log, stateTag, "on_mount", openGuard<stub_on_mount>);
    if (wants("on_vehicle_debris_flow_recycled")) registerOne(L, state, log, stateTag, "on_vehicle_debris_flow_recycled", openGuard<stub_on_vehicle_debris_flow_recycled>);
    if (wants("open_vint_dialog_check_done")) registerOne(L, state, log, stateTag, "open_vint_dialog_check_done", openGuard<stub_open_vint_dialog_check_done>);
    if (wants("hud_get_text_adventure_current_screen_index")) registerOne(L, state, log, stateTag, "hud_get_text_adventure_current_screen_index", openGuard<stub_hud_get_text_adventure_current_screen_index>);
    if (wants("hud_x_of_y_remove")) registerOne(L, state, log, stateTag, "hud_x_of_y_remove", openGuard<stub_hud_x_of_y_remove>);
    if (wants("flashpoint_mission_status")) registerOne(L, state, log, stateTag, "flashpoint_mission_status", openGuard<stub_flashpoint_mission_status>);
    if (wants("action_sequence_advance")) registerOne(L, state, log, stateTag, "action_sequence_advance", openGuard<stub_action_sequence_advance>);
    if (wants("action_sequence_is_playing")) registerOne(L, state, log, stateTag, "action_sequence_is_playing", openGuard<stub_action_sequence_is_playing>);
    if (wants("action_sequence_setup")) registerOne(L, state, log, stateTag, "action_sequence_setup", openGuard<stub_action_sequence_setup>);
    if (wants("tutorial_stop_current")) registerOne(L, state, log, stateTag, "tutorial_stop_current", openGuard<stub_tutorial_stop_current>);
    if (wants("qte_start_m02_skyqte_01")) registerOne(L, state, log, stateTag, "qte_start_m02_skyqte_01", openGuard<stub_qte_start_m02_skyqte_01>);
    if (wants("message_remove")) registerOne(L, state, log, stateTag, "message_remove", openGuard<stub_message_remove>);
    if (wants("minimap_icon_add_radius_do")) registerOne(L, state, log, stateTag, "minimap_icon_add_radius_do", openGuard<stub_minimap_icon_add_radius_do>);
    if (wants("minimap_icon_remove_radius_do")) registerOne(L, state, log, stateTag, "minimap_icon_remove_radius_do", openGuard<stub_minimap_icon_remove_radius_do>);
    if (wants("mission_maybe_uncomplete_m22")) registerOne(L, state, log, stateTag, "mission_maybe_uncomplete_m22", openGuard<stub_mission_maybe_uncomplete_m22>);
    if (wants("open_vint_dialog_do")) registerOne(L, state, log, stateTag, "open_vint_dialog_do", openGuard<stub_open_vint_dialog_do>);
    if (wants("homie_mission_unlock")) registerOne(L, state, log, stateTag, "homie_mission_unlock", openGuard<stub_homie_mission_unlock>);
    if (wants("hud_get_proto_selection")) registerOne(L, state, log, stateTag, "hud_get_proto_selection", openGuard<stub_hud_get_proto_selection>);
    if (wants("hud_pop_screen")) registerOne(L, state, log, stateTag, "hud_pop_screen", openGuard<stub_hud_pop_screen>);
    if (wants("hud_x_of_y_add")) registerOne(L, state, log, stateTag, "hud_x_of_y_add", openGuard<stub_hud_x_of_y_add>);
    if (wants("hud_x_of_y_update")) registerOne(L, state, log, stateTag, "hud_x_of_y_update", openGuard<stub_hud_x_of_y_update>);
    if (wants("homie_mission_lock")) registerOne(L, state, log, stateTag, "homie_mission_lock", openGuard<stub_homie_mission_lock>);
    if (wants("flashpoint_start_mission")) registerOne(L, state, log, stateTag, "flashpoint_start_mission", openGuard<stub_flashpoint_start_mission>);
    if (wants("add_object_indicator_to_closest_npc")) registerOne(L, state, log, stateTag, "add_object_indicator_to_closest_npc", openGuard<stub_add_object_indicator_to_closest_npc>);
    if (wants("on_mission_item_drop")) registerOne(L, state, log, stateTag, "on_mission_item_drop", openGuard<stub_on_mission_item_drop>);
}

} // namespace stub_sections
} // namespace sr3luahost
