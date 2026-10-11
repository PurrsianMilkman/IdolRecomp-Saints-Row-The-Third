// lua_spec_confirmed_stubs_b04_r04_r06_08.cpp
//
// Sec31 (pause-map/stag-mode family, 5 names) + ranking tranche 04 (Sec32, 25 names) + the Sec35 teleport_coop investigation (4 names) + ranking tranches 06/07/08 (Sec38/Sec39/Sec40, 16 names).
//
// One of 7 files split 2026-10-07 from the former single 8,700+ line
// lua_spec_confirmed_stubs.cpp (a pure structural refactor, zero behaviour
// change - every stub body, comment and citation below is reproduced
// verbatim from that file). See lua_spec_confirmed_stubs_registry.cpp's own
// top comment for the full split rationale and the shared-registration
// mechanism (lua_spec_confirmed_stubs_sections.h/_common.h).
#include "lua_spec_confirmed_stubs_common.h"
#include "lua_spec_confirmed_stubs_sections.h"

namespace sr3luahost {
namespace {

// =======================================================================
// Batch 2026-10-02: spec-lua-api-behaviour.md Sec31 ("pause_map_stag_
// current_district_control" - location and "stag mode") and Sec32
// ("ranking tranche 04", 25 previously-unspecced names). 5 names from
// Sec31's own UI pause-map registrar (0x007dfae0, see Sec31.1's own
// 9-entry table) plus the 25 gameplay-registrar names of Sec32 - 30 total.
// See engine_state.h's own per-field/per-method doc comments for exactly
// which real global/mechanism each one stands for.
//
// The 0x009df3d0 correction (Sec14.31/Sec32.8): this document's §8.17/
// §14.31 previously read 0x009df3d0 as a "matching" lookup for the local
// player's own roster entry; ranking tranche 04 (§32.1-A.7) corrects this
// to "the first live-roster entry that is NOT the local player" (i.e. the
// remote co-op player), returning null only when every roster entry
// equals the local player. A grep of this project's own existing source
// for "0x009df3d0" (before this batch) found exactly one prior consumer,
// `customization_restore_player_rig` (Sec28.12,
// EngineState::PlayerRig::coopPlayerPresent) - already modeled as a plain
// presence boolean ("is there a co-op player"), never as a name-matching
// lookup, so it needed NO change for this correction. The only NEW
// consumer this batch adds, `satellite_weapon_mode_exit` below, applies
// the corrected reading directly at its own call site: bit 0x2 of its
// selector mask only ever acts on a player when
// playerRig().coopPlayerPresent is true - in single player (no co-op
// session), that bit is a no-op, never a fallback to the local player.
// =======================================================================

// ---------------------------------------------------------------------
// pause_map_stag_current_district_control (Sec31.2, 0x007de0d0). No
// arguments (the argument count is fetched and discarded - CONFIRMED).
// Return: normally 1 number (owned/total weighted fraction for the
// selected zone, or 1.0 if total <= 0 including NaN). CONFIRMED
// exception: when no zone is selected (0x0229a2ac reads 0), the function
// does NOT return early - it pushes 0 first, then falls through the SAME
// computation a second time (in practice giving 1.0, since no members are
// ever registered under zone handle 0), so it returns 2 numbers: 0, 1.0.
// A reimplementation reproduces this two-value shape faithfully.
// ---------------------------------------------------------------------
int stub_pause_map_stag_current_district_control(lua_State* L) {
    logCall(L, upLog(L), "pause_map_stag_current_district_control", upStateTag(L));
    EngineState* st = upState(L);
    uint32_t zone = st->pauseMapSelectedZone();
    int pushed = 0;
    if (zone == 0) { // CONFIRMED: no early return - pushes 0, then falls through the same computation again
        lua_pushnumber(L, 0.0);
        ++pushed;
    }
    lua_pushnumber(L, st->pauseMapZoneControlFraction(zone));
    ++pushed;
    return pushed;
}

// ---------------------------------------------------------------------
// action_sequence_end (Sec33.3, 0x00a3c270). No arguments, no return.
// CONFIRMED global teardown - see EngineState::actionSequenceEnd's own
// doc comment for the exact fields this project tracks.
// ---------------------------------------------------------------------
int stub_action_sequence_end(lua_State* L) {
    logCall(L, upLog(L), "action_sequence_end", upStateTag(L));
    upState(L)->actionSequenceEnd();
    return 0;
}

// ---------------------------------------------------------------------
// pause_map_is_stag_mode (Sec31.1 table item 4 / Sec31.5, 0x007d98b0). No
// arguments. Return: 1 boolean - the stag-mode flag 0x0229a317 (the SAME
// global game_autosave's own gate reads, Sec27.12/Sec31.5).
// ---------------------------------------------------------------------
int stub_pause_map_is_stag_mode(lua_State* L) {
    logCall(L, upLog(L), "pause_map_is_stag_mode", upStateTag(L));
    lua_pushboolean(L, upState(L)->pauseMapStagMode().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// pause_map_is_tutorial_mode (Sec31.1 table item 5 / Sec31.5, 0x007d98e0).
// No arguments. Return: 1 boolean - the tutorial-mode flag 0x0229a318
// (CONFIRMED via Sec31.5's own cross-reference naming this exact getter;
// the setter is Sec32.1's own `pause_map_tutorial_mode` below).
// ---------------------------------------------------------------------
int stub_pause_map_is_tutorial_mode(lua_State* L) {
    logCall(L, upLog(L), "pause_map_is_tutorial_mode", upStateTag(L));
    lua_pushboolean(L, upState(L)->pauseMapTutorialMode().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// pause_map_set_gps (Sec31.3, 0x007dba10). CONFIRMED (stag-mode branch
// only - see EngineState::pauseMapSetGpsStagBranch's own doc comment for
// the full gate and for why the non-stag GPS-route argument contract is
// OPEN and not read here). Return: none.
// ---------------------------------------------------------------------
int stub_pause_map_set_gps(lua_State* L) {
    logCall(L, upLog(L), "pause_map_set_gps", upStateTag(L));
    upState(L)->pauseMapSetGpsStagBranch(); // OPEN (stag-mode flag) propagates
    return 0;
}

// ---------------------------------------------------------------------
// audio_set_listener_override (Sec33.4, 0x00a3dc70) / audio_clear_
// listener_override (Sec33.4, 0x00a3c860). Set: 1 object-name string
// (general object resolver, requiring one specific descriptor bit -
// HYPOTHESIS "human/character" kind, not modeled further); clear: none.
// Both CONFIRMED: always store an id and always send a record, no
// authority gate (clear sends the same record with id 0). The per-frame
// position/orientation snapshot and listener substitution are NOT
// modeled (no position/camera data anywhere in this project).
// ---------------------------------------------------------------------
int stub_audio_set_listener_override(lua_State* L) {
    logCall(L, upLog(L), "audio_set_listener_override", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    st->objectResolves().get(name); // OPEN until set
    st->audioListenerOverrideTarget() = name;
    EngineState::replicateStateChange("audio_set_listener_override", name); // CONFIRMED: always sends an opcode-0x45 record
    return 0;
}

int stub_audio_clear_listener_override(lua_State* L) {
    logCall(L, upLog(L), "audio_clear_listener_override", upStateTag(L));
    EngineState* st = upState(L);
    st->audioListenerOverrideTarget().clear();
    EngineState::replicateStateChange("audio_clear_listener_override", ""); // CONFIRMED: sends the same record with id 0
    return 0;
}

// ---------------------------------------------------------------------
// audio_play_for_navpoint (Sec33.4, 0x00a3edf0). 1 sound/event-name
// string + 1 object-name string (third-tier by-name resolver) + 1
// optional string that is read and discarded, never used (the same
// "read but inert" pattern already documented elsewhere in this
// project). Returns 1 number: the playing instance's id, or 0 on
// failure. This project has no real Wwise event table to resolve the
// sound name against - the handle is produced the same way game_UI_
// audio_play's own stand-in does (see its doc comment), regardless of
// the sound name's own content.
// ---------------------------------------------------------------------
int stub_audio_play_for_navpoint(lua_State* L) {
    logCall(L, upLog(L), "audio_play_for_navpoint", upStateTag(L));
    (void)argString(L, 1); // sound/event name - no real Wwise table to resolve against (see doc comment)
    std::string object = argString(L, 2);
    if (lua_gettop(L) >= 3 && lua_type(L, 3) != LUA_TNIL) (void)argString(L, 3); // CONFIRMED read but inert
    EngineState* st = upState(L);
    double id = 0.0;
    if (st->objectResolves().get(object)) { // OPEN until set
        id = static_cast<double>(st->nextAudioVoiceHandle());
    }
    lua_pushnumber(L, id);
    return 1;
}

// ---------------------------------------------------------------------
// pause_map_stag_takeover_do_reward (Sec31.6, 0x007de1c0). No arguments.
// Return: none (CONFIRMED, every path). See
// EngineState::pauseMapStagTakeover's own doc comment for the full
// claim/clear-stag-mode/request-autosave mechanism and for why the real
// engine's own unchecked null-dereference (no zone selected) is refused
// safely here rather than reproduced.
// ---------------------------------------------------------------------
int stub_pause_map_stag_takeover_do_reward(lua_State* L) {
    logCall(L, upLog(L), "pause_map_stag_takeover_do_reward", upStateTag(L));
    upState(L)->pauseMapStagTakeover();
    return 0;
}

// =======================================================================
// Ranking tranche 04 (Sec32), 25 names, gameplay registrar 0x00a20840.
// =======================================================================

// ---------------------------------------------------------------------
// store_interface_is_active (Sec32.1, 0x00a5def0). No arguments. Return:
// 1 boolean - bit 0x1 of byte global 0x022cce86 (runtime-only; OPEN
// writer).
// ---------------------------------------------------------------------
int stub_store_interface_is_active(lua_State* L) {
    logCall(L, upLog(L), "store_interface_is_active", upStateTag(L));
    lua_pushboolean(L, upState(L)->storeInterfaceActive().get() ? 1 : 0); // OPEN until set
    return 1;
}

// ---------------------------------------------------------------------
// spawn_region_max_spawn_dist (Sec32.1, 0x00a5dbf0). 1 number (no nil-
// gate; a non-convertible argument reads as 0.0 via lua_tonumber's own
// real semantics). No return. CONFIRMED: narrows to float, squares it
// (double, narrowed back) into the global - no range check (negative
// squares positive, 0 makes the cap 0).
// ---------------------------------------------------------------------
int stub_spawn_region_max_spawn_dist(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_max_spawn_dist", upStateTag(L));
    double v = lua_tonumber(L, 1);
    float f = static_cast<float>(v);
    upState(L)->setSpawnRegionMaxSpawnDistSquared(f * f);
    return 0;
}

// ---------------------------------------------------------------------
// spawn_region_max_spawn_dist_reset (Sec32.1, 0x00a5dc20). No arguments,
// no return. CONFIRMED: resets the squared cap back to FLT_MAX ("no
// limit") - the sibling setter's own static default.
// ---------------------------------------------------------------------
int stub_spawn_region_max_spawn_dist_reset(lua_State* L) {
    logCall(L, upLog(L), "spawn_region_max_spawn_dist_reset", upStateTag(L));
    upState(L)->resetSpawnRegionMaxSpawnDist();
    return 0;
}

// ---------------------------------------------------------------------
// set_ped_override_density (Sec32.1, 0x00a5d1f0). 1 number, no nil-gate.
// No return. CONFIRMED: value > 0 stores it only if <= 1.0 (above 1.0
// silently ignored, prior value kept); value <= 0 stores -1.0 ("no
// override"). A NaN argument takes the >0 branch (the real test is
// structurally "<=0 ? low : high", so NaN - which fails every ordered
// comparison - falls to the high/">0" branch) then fails the <=1.0 test,
// a no-op - reproduced here via the SAME `!(v <= 0.0)` structure, not a
// literal `v > 0.0` (which would misclassify NaN).
// ---------------------------------------------------------------------
int stub_set_ped_override_density(lua_State* L) {
    logCall(L, upLog(L), "set_ped_override_density", upStateTag(L));
    double v = lua_tonumber(L, 1);
    EngineState* st = upState(L);
    if (!(v <= 0.0)) { // CONFIRMED structure: "> 0" branch, including NaN
        if (v <= 1.0) st->setPedOverrideDensity(static_cast<float>(v));
        // else: silently ignored, prior value kept (CONFIRMED)
    } else {
        st->setPedOverrideDensity(-1.0f);
    }
    return 0;
}

// =======================================================================
// Batch 2026-10-02: spec-lua-api-behaviour.md Sec38 ("ranking tranche
// 06"), Sec39 ("ranking tranche 07") and Sec40 ("ranking tranche 08") -
// 75 previously-unspecced Lua-bound names total (25 each), all low-
// traffic by Team A's own static call-count ranking and, independently,
// ZERO real hits in this session's own latest mission-drive trace
// (results/verify_sched_check/verdict_stub_hits_with_missions.tsv,
// checked directly against all 75 names - none appear there at all).
// Per the orchestrator's own explicit scope instruction, this batch
// implements: (1) EVERY mandated crash guard / faithful quirk the
// orchestrator itself flagged, regardless of call count (safety/
// correctness requirements, not optional); (2) a handful of additional
// names bundled only where they share already-built infrastructure at
// negligible marginal cost (Sec30's own "bundle cheap siblings"
// convention). 16 of the combined 75 names are implemented: 4 from
// Sec38, 7 from Sec39, 5 from Sec40 - see each sub-section's own header
// comment below for exactly which and why; the other 59 remain ordinary
// generic logging stubs this pass (not in specConfirmedStubNames()).
// =======================================================================

// -----------------------------------------------------------------------
// Sec38 ("ranking tranche 06") - 4 of its 25 names: the 2 MANDATED crash
// guards (vcust_preview_wheel_sizing, team_make_hostile), plus the 2
// MANDATED faithful quirks (vehicle_engine_check_running, store_weapon_
// purchase_ammo). The other 21 names (the vehicle-enter family and its
// shared 0x44-byte request record, vehicle_hidden/_anim_playing/
// _engine_start/_disable_weapon_physics/_forced_corpse_removal_enabled/
// _delete_all_corpses, tutorial_visible/_get_case, store_weapon_change_
// weapon/_process_post_bg_covered, store_vehicle_change_mode/_retrieve_
// car_and_exit/_selection_should_lock_controls, vcust_preview_color/
// _palette/_purchase_wheels) are zero real hits with no mandated guard/
// quirk and each would need substantial new untested engine-state
// modeling (an occupancy-slot/eligibility-predicate model, a tutorial-
// queue/case table, a store-UI mode/screen-stack model, zone/palette
// lookup tables) - left as generic stubs this pass, same judgment call
// Sec30's own batch made for its own 16 unimplemented names.
// -----------------------------------------------------------------------

// vcust_preview_wheel_sizing (Sec38.5, 0x0081f5d0) - MANDATED crash guard
// #1. 2 numbers (truncated to a byte each, CONFIRMED). No return.
// CONFIRMED real defect: "If there is no live customization target, the
// vehicle pointer is zero and the very next instruction reads an offset
// of the null pointer - an unconditional access violation." HOST-SAFETY
// DEVIATION (not a spec fact): this host checks EngineState::
// vcustPreview().targetLive (CHOSEN default false - see its own doc
// comment, engine_state.h) instead of performing the equivalent null-
// pointer-shaped read, counting the averted path via
// recordVcustPreviewWheelSizingNullTargetGuard() - "this project does not
// simulate crashes" (cf. group_get_next_npc, Sec28.5, the established
// precedent this task's own instructions cite by name). The live-target
// path's actual wheel-sizing write is OPEN (the +0x688 sub-object's exact
// fields are not given beyond the crash shape itself) - not modeled
// further either way.
int stub_vcust_preview_wheel_sizing(lua_State* L) {
    logCall(L, upLog(L), "vcust_preview_wheel_sizing", upStateTag(L));
    (void)truncateEa2596(lua_tonumber(L, 1)); // 2 numbers, truncated to a byte each (CONFIRMED) - not otherwise modeled
    (void)truncateEa2596(lua_tonumber(L, 2));
    EngineState* st = upState(L);
    if (!st->vcustPreview().targetLive) {
        st->recordVcustPreviewWheelSizingNullTargetGuard(); // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "vcust_preview_wheel_sizing:NULL_TARGET_GUARD", upStateTag(L));
        return 0;
    }
    return 0; // live target: no further confirmed effect to apply (OPEN)
}

// team_make_hostile (Sec38.2, 0x00a607a0) - MANDATED crash guard #2. 2
// strings (team names). No return. CONFIRMED: writes relation value 0
// (hostile) symmetrically with the SAME team-5-mirrors-team-6 behavior
// already established for team_make_unfriendly (Sec30.6, directly above -
// reuses that EXACT infrastructure per this task's own explicit
// instruction not to duplicate it: resolveTeamId()/
// setTeamRelationIfInBounds()/recordTeamRelationOobGuard()), then ALWAYS
// broadcasts an opcode-0x43 record with no authority gate of any kind
// (the same "no authority gate" shape team_make_unfriendly's own stub
// above already has - replicateStateChange only ever runs after a
// successful in-bounds write there too, so this is not a structural
// change for this host). Defect 1 (CONFIRMED): "a non-string team-name
// argument reaches a null-pointer dereference inside the name-hash helper
// with no guard" - already avoided by construction in this project:
// argString() (this file's own established NULL-safe lua_tolstring
// wrapper, used here) never passes a raw null C string into
// resolveTeamId(), substituting "" instead, so this specific hazard never
// arises here regardless of the Lua argument's real type - nothing
// further to guard. Defect 2 (CONFIRMED): "an unrecognized team name
// resolves to id 9, which is larger than the matrix's own row stride -
// writing past the intended row ... which then gets replicated to every
// peer." HOST-SAFETY DEVIATION (not a spec fact): both the out-of-bounds
// write AND its subsequent replication are guarded together here, exactly
// as team_make_unfriendly already does (setTeamRelationIfInBounds's own
// early-false return skips the replicateStateChange call below it, same
// shape, per this task's own "guard and log both the write and the
// replication call" instruction).
int stub_team_make_hostile(lua_State* L) {
    logCall(L, upLog(L), "team_make_hostile", upStateTag(L));
    std::string team1 = argString(L, 1);
    std::string team2 = argString(L, 2);
    EngineState* st = upState(L);
    int id1 = st->resolveTeamId(team1);
    int id2 = st->resolveTeamId(team2);
    if (!st->setTeamRelationIfInBounds(id1, id2, 0)) { // value 0 = hostile (CONFIRMED)
        st->recordTeamRelationOobGuard(); // HOST-SAFETY: shared Sec30.6 infra, see comment above
        logCall(L, upLog(L), "team_make_hostile:OOB_TEAM_INDEX_GUARD", upStateTag(L));
        return 0;
    }
    EngineState::replicateStateChange("team_make_hostile", team1 + "/" + team2); // always, no authority gate (CONFIRMED)
    return 0;
}

// vehicle_engine_check_running (Sec38.1, 0x00a63e50) - MANDATED faithful
// quirk. 1 string. CONFIRMED return-value defect: "On failure (unresolved,
// or a specific object flag clear) pushes false and returns 1 value; on
// success it calls the engine-running-bit getter, DISCARDS the result,
// and returns 0 values - so this query can never answer true." NOT
// "fixed" here into returning a boolean on success, per this task's own
// explicit instruction - implemented exactly zero-return-always-on-
// success. "A specific object flag clear" is this project's own
// VehicleState::engineCheckGatePasses stand-in (OPEN until set, exact
// real bit not given in scope - see its own doc comment).
int stub_vehicle_engine_check_running(lua_State* L) {
    logCall(L, upLog(L), "vehicle_engine_check_running", upStateTag(L));
    std::string vehicleName = argString(L, 1);
    EngineState* st = upState(L);
    bool ok = st->objectResolves().get(vehicleName) &&                            // OPEN until set
              st->getOrCreateVehicle(vehicleName).engineCheckGatePasses.get();     // OPEN until set
    if (!ok) {
        lua_pushboolean(L, 0);
        return 1;
    }
    return 0; // CONFIRMED: zero Lua values on success - this query can never answer true (Sec38.1)
}

// store_weapon_purchase_ammo (Sec38.3, 0x008165c0) - MANDATED faithful
// quirk. Up to 4 numeric arguments (slot/category, item id, amount,
// price - CONFIRMED count/order; no nil-gate given beyond "up to 4", read
// here via lua_tonumber with no gate, matching this project's own
// established "no gate, absent reads 0" idiom used throughout this file
// for similarly under-specified numeric args). No return. CONFIRMED
// quirk: "charging the price and recording the purchase even on failure:
// an empty or non-matching store-list scan still charges the player and
// records an unrelated/garbage item ... No affordability check in this
// function at all." This project has no real weapon-store item/listing
// table anywhere in scope, so the item lookup always takes the real,
// CONFIRMED failure branch here - the SAME "never-populated registry is
// the honest default" precedent as EngineState::resolveTeamId()/
// findVdoObject() - not a simplification of the charge/record contract
// itself (there is none to simplify: both happen unconditionally either
// way). `tag` matches the real notification mechanism's own literal
// string (Sec38.3).
int stub_store_weapon_purchase_ammo(lua_State* L) {
    logCall(L, upLog(L), "store_weapon_purchase_ammo", upStateTag(L));
    // arg 1 (slot/category) / arg 2 (item id) / arg 3 (amount): not otherwise
    // modeled - this host has no real store-list/inventory-slot table (see
    // comment above); only arg 4 (price) feeds an observable effect.
    double price = lua_gettop(L) >= 4 ? lua_tonumber(L, 4) : 0.0;
    EngineState* st = upState(L);
    st->playerCash() -= price; // CONFIRMED: no affordability check anywhere in this function
    st->purchaseLedger().push_back({"weapon-ammo", price, false});
    return 0;
}

// -----------------------------------------------------------------------
// Sec39 ("ranking tranche 07") - 7 of its 25 names: the 4 MANDATED new-
// defect guards (skydive_move_to_do, store_gang_show_question_marks,
// store_gallery_download_hide_list, store_common_rotate_mouse_drag) plus
// save_system_save_game/_load_game (the MANDATED off-by-one faithful
// quirk) and save_system_cancel_coop_load, bundled alongside them at
// negligible marginal cost once the shared SaveSystemUi singleton exists
// (same "bundle cheap siblings" convention as Sec30's own
// vehicle_clear_all_radio_locks addition). The other 18 names
// (rappel_enter/_exit, set_cower_variant/_unrecruitable_flag/
// _unjackable_by_ai_flag, rc_set_max_signal_range, spotlight_heli_
// laser_size, roadblock_create/_destroy, the 5 spawning/world-state
// globals, store_dlc_queue_icon/_is_offer_free, store_clothing_get_
// store_id, store_character_lineup_loaded) are zero real hits with no
// mandated guard/quirk; roadblock_destroy specifically needed its own
// investigation - see this file's own note just below the Sec39 batch
// for why it still stays a generic stub. The rest each need their own
// substantial new engine-state modeling (a rappel queue/attached-object
// model, a road-network/template table, region/world-object resolution)
// - left as generic stubs this pass, same judgment call as Sec38 above.
//
// Correction investigated and CONFIRMED ALREADY CORRECT in this codebase
// (no code change needed): Sec39.6 states "0x00ea2596 (this document's
// established truncating float-to-int cast) is now confirmed to actually
// return a full 64-bit value (EDX:EAX) - most callers only ever use the
// low half, but roadblock_destroy is a real, confirmed instance that
// needs both." This project's existing truncateEa2596() (above in this
// file, Sec4.1) was checked directly: it already returns int64_t (the
// FULL truncated 64-bit value, via std::trunc on the real double range
// [-2^63, 2^63) before a single static_cast<int64_t>), and every existing
// call site already stores that full int64_t (none narrow it to int32) -
// so this project's shared helper was never 32-bit-only and needs no
// widening. roadblock_destroy itself is NOT implemented this pass
// regardless (zero real calls, and the real per-half meaning of
// EDX/EAX - which bits of the 64-bit value the real engine's "roadblock-
// kind descriptor bit" check and "generic object-release routine" each
// read - is not given in scope beyond the crash-shaped-correction fact
// itself; implementing it would mean inventing that split rather than
// widening an existing model). roadblock_create (the paired handle
// producer) would need a road-network-manager/template-table model this
// project also does not have - both stay generic stubs, a stated gap,
// not a missed correction.
// -----------------------------------------------------------------------

// skydive_move_to_do (Sec39.1, 0x00a5f780) - MANDATED guard (a new
// original-game defect; guard/log, do NOT reproduce as a crash). 6
// arguments (character, target name, a bare "use-path" boolean, then 3
// optional: point index, 2 booleans - CONFIRMED count/order; the 3
// optional arguments have no further modeled effect in this host, see
// below). No return. CONFIRMED real defect: "With the flag false, the
// target resolves through the generic third-tier resolver with NO guard
// on failure: an absent/unresolved name leaves the target pointer at
// literal zero, and the function then unconditionally copies 12 bytes
// from address 0x40 into the character - a genuine access violation."
// HOST-SAFETY DEVIATION (not a spec fact): this host skips the equivalent
// copy on an explicitly-known-unresolved non-path target instead of
// performing it, counting the averted path via
// recordSkydiveMoveToNullTargetGuard() - "this project does not simulate
// crashes" (cf. group_get_next_npc, Sec28.5). Uses objectResolves().
// known()+get() rather than a bare get() so an EXPLICITLY-registered
// "does not resolve" name (the real shape of the confirmed defect) takes
// the guarded path directly, rather than a merely-never-registered name
// throwing OPEN_STATE first - the same "known false, not just never
// known" distinction this project's resolveTeamId() sentinel already
// relies on for an analogous guard. The use-path-true branch (a dedicated
// path-object resolver + bounds-checked 12-byte point array read) and
// the success path's own action-state-0x1f switch (0x004dd380)/move-to-
// target field copy are OPEN - not modeled either way (the 3-further-
// gate reproducibility condition Sec39.1 separately describes is about
// the REAL crash's reachability, not about whether this host's own guard
// should apply).
int stub_skydive_move_to_do(lua_State* L) {
    logCall(L, upLog(L), "skydive_move_to_do", upStateTag(L));
    std::string character = argString(L, 1);
    (void)character; // no further confirmed effect modeled in this host either way (see comment above)
    std::string targetName = argString(L, 2);
    bool usePath = lua_toboolean(L, 3) != 0; // bare boolean (CONFIRMED)
    EngineState* st = upState(L);
    if (!usePath) {
        bool targetKnownResolved = st->objectResolves().known(targetName) && st->objectResolves().get(targetName);
        if (!targetKnownResolved) {
            st->recordSkydiveMoveToNullTargetGuard(); // HOST-SAFETY, see comment above
            logCall(L, upLog(L), "skydive_move_to_do:NULL_TARGET_GUARD", upStateTag(L));
            return 0;
        }
    }
    // use-path true, or a confirmed-resolved target: no further confirmed
    // effect modeled (action-state switch / point-array read / move-to
    // field copy all OPEN, see comment above).
    return 0;
}

// store_gang_show_question_marks (Sec39.5, 0x00811170) - MANDATED guard
// (a new original-game defect; guard/log, do NOT reproduce as a crash).
// Bare boolean. No return. CONFIRMED real defect: "If the slot's own
// asset reference is unset AND the fallback 'question mark' asset lookup
// also fails ... the code takes the 'not a question mark' branch anyway
// and dereferences the resulting null pointer." HOST-SAFETY DEVIATION (not
// a spec fact): this host checks EngineState::storePreviewGuards().
// gangPreviewAssetOrFallbackResolved (CHOSEN default false - see its own
// doc comment) instead of performing the equivalent null-pointer-shaped
// read, counting the averted path via gangQuestionMarksNullGuardCount -
// "this project does not simulate crashes" (cf. group_get_next_npc,
// Sec28.5). The slot-index bounds-check hazard the spec separately flags
// as HYPOTHESIS risk (not independently confirmed reachable) is not
// modeled either way - this host's own minimal model takes no slot-index
// argument (no real fixed-size slot table in scope).
int stub_store_gang_show_question_marks(lua_State* L) {
    logCall(L, upLog(L), "store_gang_show_question_marks", upStateTag(L));
    bool wantQuestionMarks = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED)
    EngineState* st = upState(L);
    auto& guards = st->storePreviewGuards();
    if (!guards.gangPreviewAssetOrFallbackResolved) {
        ++guards.gangQuestionMarksNullGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "store_gang_show_question_marks:NULL_ASSET_GUARD", upStateTag(L));
        return 0;
    }
    guards.gangPreviewShowingQuestionMarks = wantQuestionMarks; // CHOSEN write-only toggle, this project's own stand-in
    return 0;
}

// store_gallery_download_hide_list (Sec39.5, 0x0080f1a0) - MANDATED guard
// (a new original-game defect; guard/log, do NOT reproduce as a crash).
// Bare boolean. No return. CONFIRMED real defect: "The 'true' (hide) path
// clears a large block of a global gallery-list object AND reads a field
// off the local player - neither the gallery object's own global pointer
// nor the local-player pointer is null-checked anywhere in this
// function." HOST-SAFETY DEVIATION (not a spec fact): this host checks
// EngineState::storePreviewGuards().galleryListObjectResolved (CHOSEN
// default false) AND hasLocalPlayer() (reused - the SAME "player 1
// exists" concept cellphone_animate_start_do already reuses it for,
// Sec28.23) before applying the hide path, instead of performing the
// equivalent unguarded dereferences, counting the averted path via
// galleryHideListNullGuardCount. The spec gives no detail for the "false"
// (show) path beyond the hazard being specific to "true" - not modeled
// (a plain no-op; honestly nothing confirmed to do there).
int stub_store_gallery_download_hide_list(lua_State* L) {
    logCall(L, upLog(L), "store_gallery_download_hide_list", upStateTag(L));
    bool hide = lua_toboolean(L, 1) != 0; // bare boolean (CONFIRMED)
    if (!hide) return 0; // the hazard (and all described effect) is specific to the "true" path
    EngineState* st = upState(L);
    auto& guards = st->storePreviewGuards();
    if (!guards.galleryListObjectResolved) {
        ++guards.galleryHideListNullGuardCount; // HOST-SAFETY, see comment above
        logCall(L, upLog(L), "store_gallery_download_hide_list:NULL_PTR_GUARD", upStateTag(L));
        return 0;
    }
    if (!st->hasLocalPlayer().get()) { // OPEN until set - reused, see comment above
        ++guards.galleryHideListNullGuardCount;
        logCall(L, upLog(L), "store_gallery_download_hide_list:NULL_PTR_GUARD", upStateTag(L));
        return 0;
    }
    return 0; // both resolved: no further confirmed effect to apply beyond the clear itself (OPEN detail)
}

// store_common_rotate_mouse_drag (Sec39.5, 0x0080dc10) - MANDATED CHOSEN
// host-safety stand-in. No arguments, no return. CONFIRMED: "reads mouse-
// delta values from three fixed stack slots, but only copies the real
// values in when a specific input-mode bit is set ... when clear, the
// three stack slots are never initialized at all... A faithful
// reimplementation must treat 'mouse delta unavailable' as zero, not skip
// the rotation or read whatever happens to be on the stack." CHOSEN (not
// a spec fact, per this task's own explicit instruction): this project
// has no real mouse-delta-capture layer anywhere in scope (modeled
// nowhere else in this codebase either), so BOTH the "bit clear" case
// (real uninitialized memory) and the "bit set" case (a real value this
// host has no source for) land on the same explicit, defined 0 rather
// than raw/undefined C++ memory or an invented nonzero value. The actual
// rotation target selection (gang-customization preview / store object /
// fallback local player with no null check) is not modeled - with delta
// 0 there is no further observable effect to apply either way, and this
// project does not track vehicle/player orientation at this granularity.
int stub_store_common_rotate_mouse_drag(lua_State* L) {
    logCall(L, upLog(L), "store_common_rotate_mouse_drag", upStateTag(L));
    EngineState* st = upState(L);
    auto& drag = st->storeCommonRotateMouseDrag();
    drag.lastDeltaX = 0.0; // CHOSEN, see comment above
    drag.lastDeltaY = 0.0;
    drag.lastDeltaZ = 0.0;
    ++drag.callCount;
    return 0;
}

// save_system_save_game (Sec39.4, 0x007d0dd0) - MANDATED faithful quirk.
// 1 number (slot, truncated via 0x00ea2596/truncateEa2596 - CONFIRMED).
// No return. Slot 99 is the special "new save" sentinel (CONFIRMED,
// described as save-specific - "space/slot-count checks, then commits")
// - internals OPEN, not modeled further. CONFIRMED real defect (HIGH
// CONFIDENCE, sibling-confirmed per spec): "both the save-confirm
// callback and the load path bound the slot with slot > count / slot <=
// count, accepting slot == count - one past the end of the slot table."
// This host allows that exact off-by-one (slot == count is treated the
// SAME as slot < count - both "accepted") rather than tightening the
// bound to slot < count, and logs when it fires via saveOffByOneCount.
// The confirmation-dialog/actual record-write internals are OPEN - not
// modeled.
int stub_save_system_save_game(lua_State* L) {
    logCall(L, upLog(L), "save_system_save_game", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& ui = st->saveSystemUi();
    ++ui.saveCallCount;
    if (slot == 99) return 0; // CONFIRMED "new save" sentinel - internals OPEN
    uint32_t count = ui.slotCount.get(); // OPEN until set
    if (slot == static_cast<int64_t>(count)) {
        ++ui.saveOffByOneCount; // CONFIRMED-adjacent real defect: slot==count accepted (<=, not <)
        logCall(L, upLog(L), "save_system_save_game:OFF_BY_ONE_SLOT", upStateTag(L));
    }
    // slot < count (ordinary accept) or slot == count (off-by-one, logged
    // above): both "accepted" by the real <= bound; slot > count (and
    // slot != 99) is rejected either way - no further effect to apply
    // (confirmation-dialog/record-write internals OPEN).
    return 0;
}

// save_system_load_game (Sec39.4, 0x007d0e60) - MANDATED faithful quirk,
// the load-path sibling of save_system_save_game above. 1 number (slot,
// truncated). No return. Shares the SAME confirmed off-by-one bound
// (slot == count accepted) - logged via loadOffByOneCount. The slot-99
// "new save" sentinel is NOT modeled here (its own text frames it as a
// save-specific "new save" flow, Sec39.4) - a load call with slot 99
// takes the ordinary bound check like any other value. The separate
// HYPOTHESIS "stale pending-index" deferred-load crash path (reachability
// not settled per spec) is OPEN - not modeled, a stated gap, not invented.
int stub_save_system_load_game(lua_State* L) {
    logCall(L, upLog(L), "save_system_load_game", upStateTag(L));
    int64_t slot = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    auto& ui = st->saveSystemUi();
    ++ui.loadCallCount;
    uint32_t count = ui.slotCount.get(); // OPEN until set
    if (slot == static_cast<int64_t>(count)) {
        ++ui.loadOffByOneCount; // CONFIRMED-adjacent real defect: slot==count accepted (<=, not <)
        logCall(L, upLog(L), "save_system_load_game:OFF_BY_ONE_SLOT", upStateTag(L));
    }
    return 0;
}

// save_system_cancel_coop_load (Sec39.4, 0x007d0e90) - bundled alongside
// the two above at negligible marginal cost (same shared SaveSystemUi
// singleton). No arguments, no return. CONFIRMED: "Sets the save-system's
// own state field to 5 unless already there." HYPOTHESIS: 5 = "co-op load
// cancelled" - not otherwise modeled (this project has no save-system
// state MACHINE beyond this one field, so "unless already there" and
// "set it to 5" produce the identical observable result either way).
int stub_save_system_cancel_coop_load(lua_State* L) {
    logCall(L, upLog(L), "save_system_cancel_coop_load", upStateTag(L));
    upState(L)->saveSystemUi().coopLoadState = 5; // CONFIRMED value (Sec39.4)
    return 0;
}

// -----------------------------------------------------------------------
// Sec40 ("ranking tranche 08") - 5 of its 25 names, all from the 15-entry
// PCU ("clothing store") cluster (Sec40.5/Sec40.6). That cluster's own
// write-up is EXPLICITLY condensed from a fuller interp note not included
// in this spec file ("full detail in the interp note, condensed here",
// Sec40.5), so the exact per-function Lua argument count/order for most
// of the 15 entries is not given at a level this project can implement
// without inventing beyond confirmed text - a genuine decision-fork, not
// an empty backlog, so it is documented here and deferred rather than
// guessed (per this project's own standing "no invented fixes" policy).
// This batch implements only the 5 names whose mandated guard/quirk and
// argument shape ARE given precisely enough in the condensed text itself:
// pcu_is_bra_category/pcu_is_underwear_category (1 number, 1 boolean -
// simple enough to infer with no further invention) and pcu_purchase_
// slot/pcu_purchase_outfit/pcu_wear_store_outfit (the 3 functions this
// task's own mandates name explicitly - a 2nd "price" argument for the
// two purchase entries is this project's own CHOSEN, clearly labelled
// shape, mirroring this document's own established purchase-function
// sibling pattern, see each stub's own doc comment below). The other 10
// PCU names (pcu_get_num_items_owned chief among them - its exact RETURN
// semantics beyond the bit-0-only indexing MECHANISM are not given in
// this condensed section, and inventing "returns a count of X" would
// fabricate behavior the spec text does not state) and all 10 non-PCU
// Sec40 names (the 6 sprint-override globals, radio_set_sing_along_
// allowed_during_missions/_get_station, projectile_fire_from_navpoint,
// players_in_weird_camera_mode - none mandated, zero real hits, each
// needing its own new engine-state modeling) stay ordinary generic
// logging stubs this pass.
// -----------------------------------------------------------------------

// Shared category-lookup body for pcu_is_bra_category/pcu_is_underwear_
// category (Sec40.5). 1 number (category index). Returns 1 boolean.
// CONFIRMED crash-shaped defect: "dereference a null category record when
// the index is out of range." HOST-SAFETY DEVIATION (not a spec fact):
// this project's own minimal category table (EngineState::
// pcuCategoryTable(), a test populates each known index's own "kind" tag
// directly - no real category table in scope) treats any index a test
// never populated the SAME as a real out-of-bounds index, guarding it
// instead of performing the equivalent null-pointer-shaped read.
bool pcuCategoryIsKind(lua_State* L, EngineState* st, const char* fnName, int32_t wantKind) {
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    auto& table = st->pcuCategoryTable();
    std::string key = std::to_string(idx);
    if (idx < 0 || !table.kindByIndex.known(key)) {
        ++table.categoryIndexOobGuardCount; // HOST-SAFETY, see comment above
        std::string tag = std::string(fnName) + ":CATEGORY_OOB_GUARD";
        logCall(L, upLog(L), tag.c_str(), upStateTag(L));
        return false; // CHOSEN: false on a guarded out-of-range index - no real "kind" to report
    }
    return table.kindByIndex.get(key) == wantKind;
}

int stub_pcu_is_bra_category(lua_State* L) {
    logCall(L, upLog(L), "pcu_is_bra_category", upStateTag(L));
    bool isBra = pcuCategoryIsKind(L, upState(L), "pcu_is_bra_category", EngineState::PcuCategoryTable::kBra);
    lua_pushboolean(L, isBra ? 1 : 0);
    return 1;
}

int stub_pcu_is_underwear_category(lua_State* L) {
    logCall(L, upLog(L), "pcu_is_underwear_category", upStateTag(L));
    bool isUnderwear =
        pcuCategoryIsKind(L, upState(L), "pcu_is_underwear_category", EngineState::PcuCategoryTable::kUnderwear);
    lua_pushboolean(L, isUnderwear ? 1 : 0);
    return 1;
}

// Shared catalog-outfit index resolution for pcu_wear_store_outfit/
// pcu_purchase_outfit below (Sec40.5/Sec40.6) - see EngineState::
// PcuCatalogOutfits' own doc comment (engine_state.h) for the full "two
// independent counting rules, deliberately not unified" reasoning and the
// "excluded bit read as bit 0" interpretive note. Scans test-populated
// list positions 0.. in order (this project's own minimal stand-in for
// "the end of the real catalog list" - a test marks the list's real
// extent simply by not populating past it, the SAME idiom
// readCharacterNameArray above already uses for "the first nil ends the
// array"), counting only positions whose raw flag word satisfies the
// requested rule, and returns the list POSITION of the `wantIndex`-th
// (0-based) qualifying node, or -1 if fewer than `wantIndex`+1 qualify.
int resolvePcuCatalogOutfitIndex(EngineState::PcuCatalogOutfits& catalog, int64_t wantIndex,
                                  bool countByAnyBitExcludingBit0) {
    int64_t seen = 0;
    for (int pos = 0; pos < 100000; ++pos) {
        std::string key = std::to_string(pos);
        if (!catalog.flagsByIndex.known(key)) break;
        uint32_t flags = catalog.flagsByIndex.get(key);
        bool qualifies = countByAnyBitExcludingBit0 ? ((flags & ~0x1u) != 0) : ((flags & 0x1u) != 0);
        if (qualifies) {
            if (seen == wantIndex) return pos;
            ++seen;
        }
    }
    return -1;
}

// pcu_wear_store_outfit (Sec40.5/Sec40.6) - MANDATED "implement each
// function's own rule separately": numbers catalog-outfit nodes by
// testing "any non-zero flag bit, EXCLUDING one specific bit" (read here
// as bit 0 - see resolvePcuCatalogOutfitIndex's own doc comment). 1
// number (catalog-outfit index - CONFIRMED kind of index for this entry,
// Sec40.5/Sec40.6; exact further argument count beyond this is OPEN, see
// this batch's own header comment on the condensed write-up). No return
// (an apply-style action, same shape as every other setter in this
// document). The actual apply step (the shared outfit-apply routine
// 0x00825cf0, which "mutates its source outfit record's own piece
// ordering in place") is OPEN - not modeled; this host instead records
// the resolved list position as a test-observable result
// (EngineState::pcuInventory().lastWornCatalogPosition), -1 if the index
// doesn't resolve to any qualifying node (OPEN: not confirmed whether the
// real engine has its own distinct not-found behavior here).
int stub_pcu_wear_store_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_wear_store_outfit", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    EngineState* st = upState(L);
    int pos = resolvePcuCatalogOutfitIndex(st->pcuCatalogOutfits(), idx, /*countByAnyBitExcludingBit0=*/true);
    st->pcuInventory().lastWornCatalogPosition = pos; // CHOSEN test-observable result, see comment above
    return 0;
}

// pcu_purchase_outfit (Sec40.5/Sec40.6) - MANDATED faithful quirk AND
// MANDATED "implement each function's own rule separately": numbers the
// SAME catalog-outfit list by testing "bit 0 only" - a DIFFERENT rule
// from pcu_wear_store_outfit above, deliberately not unified (the same
// raw index can therefore resolve to a DIFFERENT list position here than
// it does there - a CONFIRMED real indexing inconsistency, directly
// exercised by this pair's own tests). 1 number (catalog-outfit index) +
// an implicit price (CHOSEN 2nd-argument shape - the condensed write-up
// gives the counting rule and the charge-regardless quirk precisely but
// not this function's own full argument list, see this batch's own
// header comment; a 2nd numeric "price" argument mirrors this document's
// own established sibling shape, e.g. vcust_purchase_wheels's "1 number
// (price)", Sec38.5). CONFIRMED real logic defect (the mandated quirk):
// "ignores the inventory-append function's 'full' return value - a
// player who is already at capacity (128 outfits) is charged the full
// price and receives nothing silently." Reproduced here exactly: the
// charge always happens; the saved-outfit append is skipped (and logged)
// once at capacity, never rejected up front. The additional "drops
// individual outfit pieces past the 2048-item cap one at a time" nuance
// needs an outfit-to-pieces association this project does not have (no
// real item/outfit catalog beyond the raw flag words above) - not
// modeled, a stated gap, not invented.
int stub_pcu_purchase_outfit(lua_State* L) {
    logCall(L, upLog(L), "pcu_purchase_outfit", upStateTag(L));
    int64_t idx = truncateEa2596(lua_tonumber(L, 1));
    double price = lua_gettop(L) >= 2 ? lua_tonumber(L, 2) : 0.0; // CHOSEN 2nd-arg price, see comment above
    EngineState* st = upState(L);
    int pos = resolvePcuCatalogOutfitIndex(st->pcuCatalogOutfits(), idx, /*countByAnyBitExcludingBit0=*/false);
    auto& inv = st->pcuInventory();
    inv.lastPurchasedOutfitCatalogPosition = pos; // resolved per this function's OWN rule - see its own doc comment
                                                   // (PcuInventory, engine_state.h) for why this is a separate field
                                                   // from pcu_wear_store_outfit's lastWornCatalogPosition above
    inv.cashBalance -= price; // CONFIRMED: charged regardless of append outcome
    if (inv.savedOutfitCount >= EngineState::PcuInventory::kSavedOutfitCapacity) {
        ++inv.purchaseOutfitChargedWhileFullCount; // CONFIRMED quirk: charged even though the append is refused
        logCall(L, upLog(L), "pcu_purchase_outfit:CHARGED_WHILE_FULL", upStateTag(L));
    } else {
        ++inv.savedOutfitCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// pause_map_tutorial_mode (Sec32.1, 0x00a594e0). 1 boolean, no nil-gate
// (absent/nil -> false, lua_toboolean's own real semantics). No return.
// CONFIRMED: writes the pause-map tutorial-mode flag 0x0229a318 (the SAME
// global Sec31.1 table item 5's `pause_map_is_tutorial_mode` reads).
// ---------------------------------------------------------------------
int stub_pause_map_tutorial_mode(lua_State* L) {
    logCall(L, upLog(L), "pause_map_tutorial_mode", upStateTag(L));
    bool v = lua_toboolean(L, 1) != 0;
    upState(L)->pauseMapTutorialMode().set(v);
    return 0;
}

// ---------------------------------------------------------------------
// set_time_of_day (Sec32.1, 0x00a5e370). 2 numbers, no nil-gates (hour,
// minute; truncated via 0x00ea2596). No return. See
// EngineState::setTimeOfDay's own doc comment for the CONFIRMED forward-
// only delta arithmetic.
// ---------------------------------------------------------------------
int stub_set_time_of_day(lua_State* L) {
    logCall(L, upLog(L), "set_time_of_day", upStateTag(L));
    int64_t newHour = truncateEa2596(lua_tonumber(L, 1));
    int64_t newMinute = truncateEa2596(lua_tonumber(L, 2));
    upState(L)->setTimeOfDay(newHour, newMinute); // OPEN (current hour/minute) propagates
    return 0;
}

// ---------------------------------------------------------------------
// satellite_weapon_mode_exit (Sec32.1, 0x00a5df20). 1 optional number,
// nil-gated, default 3 - a player-selector bitmask (bit 0x1 = local
// player, bit 0x2 = remote co-op player via 0x009df3d0). No return.
// **The 0x009df3d0 correction applies directly here**: bit 0x2 only
// selects a player when playerRig().coopPlayerPresent is true (CONFIRMED,
// Sec14.31/Sec32.8 - "the first live-roster entry that is NOT the local
// player... returning 0 only when every roster entry equals the local
// player", not a "matching" lookup that could wrongly resolve to the
// local player itself in single player). With no co-op session, bit 0x2
// is therefore a correct no-op, never a fallback onto the local player.
// Local player (bit 0x1) is always resolvable (0x009da4e0, a zero-
// argument getter - no name resolution involved).
// ---------------------------------------------------------------------
int stub_satellite_weapon_mode_exit(lua_State* L) {
    logCall(L, upLog(L), "satellite_weapon_mode_exit", upStateTag(L));
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 1, 3.0));
    EngineState* st = upState(L);
    if (mask & 1) st->satelliteWeaponExit(/*remote=*/false);
    if ((mask & 2) && st->playerRig().coopPlayerPresent.get()) { // CONFIRMED false by default (applySpecInitialState item 13) - no-op in single player per the 0x009df3d0 correction above
        st->satelliteWeaponExit(/*remote=*/true);
    }
    return 0;
}

// pcu_purchase_slot (Sec40.5) - MANDATED faithful quirk. 1 number (a slot
// id, 0-23 - CONFIRMED kind of index for "several" PCU entries including
// this one, Sec40.5) + an implicit price (CHOSEN 2nd-argument shape, same
// reasoning as pcu_purchase_outfit above). CONFIRMED real logic defect
// (the mandated quirk): "ignores the inventory-append function's 'full'
// return value - a player who is already at capacity (2048 items) is
// charged the full price and receives nothing silently." The function's
// OWN separate crash-shaped defects (an unconditional dereference of a
// global pointer two lines after its own null check, an unchecked
// variant-lookup result, an unchecked store-entry variant pointer, a
// 3-slot stack-array colour copy with no clamp of its own) all need a
// store-entry/variant-lookup model this project does not have in scope -
// not modeled, a stated gap, not invented (none of them are this task's
// own mandated guards for THIS function - only the charge-while-full
// quirk is).
int stub_pcu_purchase_slot(lua_State* L) {
    logCall(L, upLog(L), "pcu_purchase_slot", upStateTag(L));
    int64_t slotId = truncateEa2596(lua_tonumber(L, 1)); // CONFIRMED: a slot id (0-23), Sec40.5
    double price = lua_gettop(L) >= 2 ? lua_tonumber(L, 2) : 0.0; // CHOSEN 2nd-arg price, see comment above
    (void)slotId; // not otherwise modeled - no real per-slot item/variant table in scope
    EngineState* st = upState(L);
    auto& inv = st->pcuInventory();
    inv.cashBalance -= price; // CONFIRMED: no affordability check (same shape as store_weapon_purchase_ammo/vcust_purchase_wheels)
    if (inv.ownedItemCount >= EngineState::PcuInventory::kOwnedItemCapacity) {
        ++inv.purchaseSlotChargedWhileFullCount; // CONFIRMED quirk: charged even though the append is refused
        logCall(L, upLog(L), "pcu_purchase_slot:CHARGED_WHILE_FULL", upStateTag(L));
    } else {
        ++inv.ownedItemCount;
    }
    return 0;
}

// ---------------------------------------------------------------------
// set_seatbelt_flag / set_trailing_aim_flag / set_never_turn_on_player
// (Sec32.2). All three: 1 mandatory character name (reuses
// objectResolves(), this project's own shared-resolver simplification), 1
// optional boolean, nil-gated, default true. No return. CONFIRMED
// record-and-replicate shape, collapsed to "always apply the write, call
// the no-op replicate stand-in" per this project's own established
// set_ignore_ai_flag precedent (replicateStateChange's own doc comment).
// ---------------------------------------------------------------------
int stub_set_seatbelt_flag(lua_State* L) {
    logCall(L, upLog(L), "set_seatbelt_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->seatbeltForceFlag()[name] = value;
        EngineState::replicateStateChange("set_seatbelt_flag", name);
    }
    return 0;
}

int stub_set_trailing_aim_flag(lua_State* L) {
    logCall(L, upLog(L), "set_trailing_aim_flag", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->trailingAimForceFlag()[name] = value;
        EngineState::replicateStateChange("set_trailing_aim_flag", name);
    }
    return 0;
}

int stub_set_never_turn_on_player(lua_State* L) {
    logCall(L, upLog(L), "set_never_turn_on_player", upStateTag(L));
    std::string name = argString(L, 1);
    bool value = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->neverTurnOnPlayerFlag()[name] = value;
        EngineState::replicateStateChange("set_never_turn_on_player", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_revive (Sec32.3, 0x00a599b0). 1 string ("#PLAYER#" accepted), no
// nil-gate, reuses objectResolves(). No return. CONFIRMED: only acts if
// the character's life-state field (+0xcc8) reads 6 ("downed" - the same
// test Sec7.13 `human_is_downed` uses); "always broadcasts first"
// regardless of authority.
// ---------------------------------------------------------------------
int stub_player_revive(lua_State* L) {
    logCall(L, upLog(L), "player_revive", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (c.lifeState.get() == 6) { // OPEN until set; CONFIRMED test value
            ++c.reviveCount;
            EngineState::replicateStateChange("player_revive", name); // CONFIRMED: always broadcasts first
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// player_warp_to_shore_disable (Sec32.3, 0x00a59a90). 1 string
// ("#PLAYER#" accepted), reuses objectResolves(). No return. CONFIRMED:
// always writes true (hardcoded, not a Lua argument) to +0x28ad bit 0x2.
// Passed WITHOUT a null check to the real setter, but that setter itself
// returns at once on a null receiver - a harmless real no-op, matching
// this project's own OPEN-refusal-on-unresolved-name convention (not a
// crash).
// ---------------------------------------------------------------------
int stub_player_warp_to_shore_disable(lua_State* L) {
    logCall(L, upLog(L), "player_warp_to_shore_disable", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->warpToShoreDisabled()[name] = true; // CONFIRMED: always true, hardcoded
        EngineState::replicateStateChange("player_warp_to_shore_disable", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// skydive_setup_tank_bailout (Sec32.3, 0x00a5e510). 1 number (stage), no
// nil-gate, truncated. No return. A mission-18 set-piece helper: no name
// resolve at all (both vehicles are hardcoded literals). CONFIRMED:
// always broadcasts first regardless of stage or vehicle existence; stage
// 1 arms, stage 2 starts the named anim; any other stage is a local no-op
// (record still sent).
// ---------------------------------------------------------------------
int stub_skydive_setup_tank_bailout(lua_State* L) {
    logCall(L, upLog(L), "skydive_setup_tank_bailout", upStateTag(L));
    int64_t stage = truncateEa2596(lua_tonumber(L, 1));
    auto& s = upState(L)->skydiveTankBailout();
    ++s.broadcastCount; // CONFIRMED: always, every stage
    if (stage == 1) ++s.armedCount;
    else if (stage == 2) ++s.animStartedCount;
    return 0;
}

// ---------------------------------------------------------------------
// qte_human_is_used (Sec32.3, 0x00a5b180). 1 string, reuses
// objectResolves(). Return: 1 boolean, always. CONFIRMED: an unresolved
// character is false (same "unresolved -> false" convention as
// get_char_vehicle_is_in_air above); else see
// EngineState::qteHumanIsUsed's own doc comment.
// ---------------------------------------------------------------------
int stub_qte_human_is_used(lua_State* L) {
    logCall(L, upLog(L), "qte_human_is_used", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    bool used = false;
    if (st->objectResolves().get(name)) { // OPEN until set
        used = st->qteHumanIsUsed(name);
    }
    lua_pushboolean(L, used ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// audio_any_conversation_playing (Sec33.4, 0x00a3c640). No arguments.
// Returns 1 boolean: true if any session member currently has a valid,
// non-0xff slot on the global "mission_conv" per-session channel
// (CONFIRMED - this settles this document's own previously-OPEN identity
// question for that channel). This project tracks no per-member array
// (see EngineState::conversationChannelActive's own doc comment) -
// collapsed to one flag per channel name, OPEN until a test sets it.
// ---------------------------------------------------------------------
int stub_audio_any_conversation_playing(lua_State* L) {
    logCall(L, upLog(L), "audio_any_conversation_playing", upStateTag(L));
    bool active = upState(L)->conversationChannelActive().get("mission_conv"); // OPEN until set
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// party_add_do (Sec32.4, 0x00a5af70). Arg 1 leader name; arg 2 table of
// follower names (length via 0x0083dff0); arg 3 boolean, no nil-gate; arg
// 4 optional boolean, nil-gated, default false. No return. CONFIRMED
// real stack-overrun hazard (6th-or-later follower) is flagged, not
// reproduced (see PartyState::overflowDetectedCount's own doc comment) -
// processing is clamped to the real 5-slot bound. A failed leader
// resolution now triggers the SAME closest-player redirect
// (resolveClosestPlayerSentinel(), lua_spec_confirmed_stubs_common.h) a
// failed leader resolve is treated as a safe no-op here,
// ai_add_enemy_target/Sec3.9 uses (2026-10-08, backlog cross-reference
// pass: this entry's own text additionally names the literal "#PLAYER#"
// as an equally-valid trigger, "picks whichever co-op player is nearest
// THAT SPECIFIC FOLLOWER" - in this project's single-player-only scope
// every such per-follower retry yields the identical result regardless of
// which follower is being processed, since no second candidate ever
// exists, so the per-follower loop collapses to one retry here, same
// reasoning as that helper's own doc comment). arg3's own capacity/force-
// override gate and arg4's leader-kind-bit AI/registration setup have no
// real data this project can reproduce and are also not modeled (read for
// correct stack shape only).
// ---------------------------------------------------------------------
int stub_party_add_do(lua_State* L) {
    logCall(L, upLog(L), "party_add_do", upStateTag(L));
    std::string leader = argString(L, 1);
    int64_t followerCount = tableLength(L, 2);
    (void)(lua_toboolean(L, 3) != 0);          // arg3: no nil-gate; capacity/force-override gate not modeled (OPEN internals)
    (void)optionalBoolDefault(L, 4, false);    // arg4: leader-kind-bit AI/registration setup not modeled (OPEN internals)
    EngineState* st = upState(L);
    if (!st->objectResolves().get(leader)) { // OPEN until set
        // CONFIRMED (Sec32.4): a failed leader resolve retries via the
        // closest-player mechanism when the leader name is
        // "#CLOSEST_PLAYER#" OR the literal "#PLAYER#" - both explicitly
        // named by this entry's own text as triggers for this fallback.
        bool triggersClosestPlayerFallback =
            caseInsensitiveEquals(leader, "#CLOSEST_PLAYER#") || caseInsensitiveEquals(leader, "#PLAYER#");
        if (!triggersClosestPlayerFallback) return 0; // any other unresolved name: CONFIRMED safe no-op
        leader = "#PLAYER1#"; // CONFIRMED redirect target - see resolveClosestPlayerSentinel()'s own doc comment
        if (!st->objectResolves().get(leader)) return 0; // OPEN until set - the retry can still fail
    }
    int64_t processed = followerCount < 0 ? 0 : std::min<int64_t>(followerCount, 5);
    if (followerCount > 5) ++st->party().overflowDetectedCount; // CONFIRMED real hazard, not reproduced
    for (int64_t i = 0; i < processed; ++i) {
        lua_rawgeti(L, 2, static_cast<int>(i + 1));
        std::string follower = argString(L, -1);
        lua_pop(L, 1);
        if (!follower.empty() && st->objectResolves().get(follower)) { // OPEN until set
            st->party().membersByLeader[leader].push_back(follower);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_is_in_party (Sec32.4, 0x00a562a0). 1 string (spec's own text: "1
// string (#FOLLOWER#/#FOLLOWER1#-#FOLLOWER3# accepted), via an inlined
// 0x00a28150-equivalent chain" - wired 2026-10-08, follower-sentinel-
// resolver pass, via resolveFollowerAwareObject()). Return: 1 boolean.
// CONFIRMED: false for an unresolved/null character; else HYPOTHESIS "has
// any party leader" (not specific to the local player's own party) - this
// project checks membership across every leader's own follower list. A
// #FOLLOWER...# literal that resolves (resolveFollowerAwareObject()
// returns true) is, by construction, already a member of SOME leader's
// list (it came from reading one) - so the membership loop below still
// runs, unchanged, over the RESOLVED name rather than the raw literal.
// ---------------------------------------------------------------------
int stub_npc_is_in_party(lua_State* L) {
    logCall(L, upLog(L), "npc_is_in_party", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    std::string resolvedName;
    bool inParty = false;
    if (resolveFollowerAwareObject(st, name, resolvedName)) { // OPEN until set - generic/#PLAYER#-then-#FOLLOWER# chain (0x00a28150); may throw OpenStateError if name is a #FOLLOWER...# literal with no index convention set
        for (const auto& kv : st->party().membersByLeader) {
            const auto& followers = kv.second;
            if (std::find(followers.begin(), followers.end(), resolvedName) != followers.end()) {
                inParty = true;
                break;
            }
        }
    }
    lua_pushboolean(L, inParty ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------
// boss_battle_matt_begin (Sec33.5, 0x00a407c0). 1 optional boolean,
// default true. No return, no replication (CONFIRMED). Resets the
// BossBattleMatt cluster's cheat-slot/id/numeric fields to their neutral
// values (this project's own modelled subset - see BossBattleMatt's own
// doc comment, Sec28.24/Sec28.25; the real "four sub-objects" this spec
// also mentions have no modelled fields here, not guessed past; the
// retry counter 0x012ec720 is NOT confirmed reset by this specific entry
// - distinct from 0x012ec71c, the static limit - left untouched), then
// arms the first deadline (5000ms true / 0 false) and marks it active.
// CONFIRMED: also looks up the character literally named "Matt" and, if
// found and alive, sets bit 0x4000 of its dword +0xec (meaning OPEN).
// Every branch-determining read happens before any write (same "no
// partial update on an OPEN refusal" convention as set_ignore_ai_flag).
// ---------------------------------------------------------------------
int stub_boss_battle_matt_begin(lua_State* L) {
    logCall(L, upLog(L), "boss_battle_matt_begin", upStateTag(L));
    bool value = true; // CONFIRMED default true
    if (lua_gettop(L) >= 1 && lua_type(L, 1) != LUA_TNIL) value = lua_toboolean(L, 1) != 0;

    EngineState* st = upState(L);
    bool mattResolved = st->objectResolves().get("Matt"); // OPEN until set - read before any write
    bool mattAlive = mattResolved && !st->getOrCreateCharacter("Matt").isDeadHighConfidence;

    auto& matt = st->bossBattleMatt();
    matt.cheatSlot.set(-1);
    matt.lastId = -1;
    matt.lastN2 = 0.0;
    matt.lastN3 = 0.0;
    matt.deadlineMs = value ? 5000 : 0; // CONFIRMED
    matt.active = true; // CONFIRMED

    if (mattAlive) {
        st->getOrCreateCharacter("Matt").flagsEc.setBits(0x4000, 0x4000); // CONFIRMED structure, meaning OPEN
    }
    return 0;
}

// ---------------------------------------------------------------------
// npc_go_idle (Sec32.4, 0x00a554b0). 1 string, reuses objectResolves()
// (no "#PLAYER#" step, per spec - this project's shared resolver is
// name-agnostic, so no code-level distinction is needed). No return.
// CONFIRMED: resets the AI-orders sub-object (not individually modeled,
// only counted) and sets the action-override state to 0x19 if in a
// vehicle (stateEnum == 3), else 0 - "the exact rule
// set_ignore_ai_flag already applies" (Sec3.4, reused directly here).
// ---------------------------------------------------------------------
int stub_npc_go_idle(lua_State* L) {
    logCall(L, upLog(L), "npc_go_idle", upStateTag(L));
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        ++c.aiOrdersResetCount;
        c.actionOverrideId.set(c.stateEnum.get() == 3 ? 0x19 : 0); // OPEN (stateEnum) until set; CONFIRMED rule
    }
    return 0;
}

// ---------------------------------------------------------------------
// auto_pickup_disable (Sec33.5, 0x00a3c8a0). No arguments, no return.
// CONFIRMED: writes a named, session-synchronized variable
// "allow_weapon_auto_pickup" off (session-wide, not per-player).
// ---------------------------------------------------------------------
int stub_auto_pickup_disable(lua_State* L) {
    logCall(L, upLog(L), "auto_pickup_disable", upStateTag(L));
    upState(L)->allowWeaponAutoPickup() = false;
    return 0;
}

// ---------------------------------------------------------------------
// waiting_for_player_dialog (Sec33.5, 0x00a68ea0). 1 boolean, no
// nil-gate. No return. CONFIRMED reference-counted show/hide (nested
// shows need an equal number of hides; a hide at counter 0 does
// nothing); showing increments the counter even when suppressed (this
// project has no "suppressing game-state check" to evaluate - stated
// simplification, see WaitingForPlayerDialog's own doc comment). On the
// final hide, host+coop-active broadcasts an opcode-0x23 record; the
// host also sends the matching show broadcast when the display actually
// appears (same gating applied by symmetry, HIGH CONFIDENCE). Open
// hazard DELIBERATELY reproduced: neither path ever clears "currently
// shown" here (CONFIRMED - only an untraced third writer, out of scope,
// does), so after one real display this stays true for the rest of the
// run.
// ---------------------------------------------------------------------
int stub_waiting_for_player_dialog(lua_State* L) {
    logCall(L, upLog(L), "waiting_for_player_dialog", upStateTag(L));
    bool show = lua_toboolean(L, 1) != 0; // CONFIRMED: no nil-gate
    EngineState* st = upState(L);
    auto& d = st->waitingForPlayerDialog();
    if (show) {
        ++d.refCount; // CONFIRMED: increments even when suppressed
        if (!d.currentlyShown) {
            d.currentlyShown = true;
            if (st->coopIsActive() && st->coopLocalIsHost()) ++d.showBroadcastCount;
        }
        // else: CONFIRMED open hazard - already shown, display branch silently skipped (deliberately reproduced)
    } else if (d.refCount > 0) { // CONFIRMED: a hide at counter 0 does nothing
        --d.refCount;
        if (d.refCount == 0 && st->coopIsActive() && st->coopLocalIsHost()) {
            ++d.hideBroadcastCount; // CONFIRMED: host, while co-op is active
        }
        // CONFIRMED open hazard: "currently shown" is NOT cleared here either.
    }
    return 0;
}

// ---------------------------------------------------------------------
// object_destroy (Sec32.5, 0x00a57630). 1 string; non-string/nil -> no-
// op (CONFIRMED). No return. Reuses objectResolves(); the liveness check,
// capability-predicate gate and per-kind destroy virtual are OPEN/not
// modeled - only that a destroy was processed is recorded.
// ---------------------------------------------------------------------
int stub_object_destroy(lua_State* L) {
    logCall(L, upLog(L), "object_destroy", upStateTag(L));
    if (lua_type(L, 1) != LUA_TSTRING) return 0; // CONFIRMED: non-string/nil -> no-op
    std::string name = argString(L, 1);
    if (upState(L)->objectResolves().get(name)) { // OPEN until set
        upState(L)->destroyedObjects().insert(name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// object_indicator_remove_do (Sec32.5, 0x00a59190). Arg 1 string; arg 2
// optional number, nil-gated, default 3 (bit 0x1 = apply locally, bit 0x2
// = send a record). No return. CONFIRMED: bit 0x1 removes EVERY indicator
// attached to the object, not just one (reuses
// CharacterState::objectIndicators, Sec10.6); bit 0x2 opens an opcode-
// 0x40 sub-tag-4 record (counted only, no networking layer modeled).
// ---------------------------------------------------------------------
int stub_object_indicator_remove_do(lua_State* L) {
    logCall(L, upLog(L), "object_indicator_remove_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        if (mask & 1) c.objectIndicators.clear(); // CONFIRMED: loop-until-none removal
        if (mask & 2) ++st->indicatorRemoveRecordCount();
    }
    return 0;
}

// ---------------------------------------------------------------------
// minimap_icon_remove_do (Sec32.5, 0x00a54430). Arg 1 string; arg 2
// optional number, nil-gated, default 3 (same bitmask convention). No
// return. CONFIRMED real crash hazard (an unranged 4-entry function-
// pointer table indexed directly by the mask byte) is flagged, not
// reproduced (Sec32.8) - this project clears every recorded minimap icon
// on the resolved object (reuses CharacterState::minimapIcons, Sec10.5)
// regardless of which real per-kind dispatch path the mask would select.
// ---------------------------------------------------------------------
int stub_minimap_icon_remove_do(lua_State* L) {
    logCall(L, upLog(L), "minimap_icon_remove_do", upStateTag(L));
    std::string name = argString(L, 1);
    int64_t mask = truncateEa2596(optionalNumberDefault(L, 2, 3.0));
    (void)mask; // the real per-kind dispatch/unranged table access (Sec32.5/Sec32.8) is not modeled
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        CharacterState& c = st->getOrCreateCharacter(name);
        c.minimapIcons.clear();
    }
    return 0;
}

// ---------------------------------------------------------------------
// shop_enable_nearest (Sec32.5, 0x00a5f650). Arg 1 optional string
// (location name, nil-gated default ""); arg 2 optional boolean, nil-
// gated, default true (enable). No return. CONFIRMED rule: the FIRST shop
// within 15 units is taken immediately, else the closest within 75 units.
// This project has no spatial/world-position system - see
// EngineState::Shop's own doc comment for the test-fixture stand-in (arg
// 1's own role in seeding the search origin is not modeled; read only for
// correct stack shape).
// ---------------------------------------------------------------------
int stub_shop_enable_nearest(lua_State* L) {
    logCall(L, upLog(L), "shop_enable_nearest", upStateTag(L));
    (void)optionalStringDefault(L, 1, ""); // location name - not modeled, see this function's own top comment
    bool enable = optionalBoolDefault(L, 2, true);
    EngineState* st = upState(L);
    auto& shops = st->shops();
    int picked = -1;
    for (size_t i = 0; i < shops.size(); ++i) {
        if (shops[i].withinFifteen) { picked = static_cast<int>(i); break; }
    }
    if (picked < 0) {
        for (size_t i = 0; i < shops.size(); ++i) {
            if (shops[i].withinSeventyFive) { picked = static_cast<int>(i); break; } // "closest" tie-breaking beyond the threshold rule itself is not modeled
        }
    }
    if (picked >= 0) {
        shops[static_cast<size_t>(picked)].disabled = !enable;
        ++st->shopEnableRecordCount();
    }
    return 0;
}

// ---------------------------------------------------------------------
// item_show (Sec32.6, 0x00a51d50). 1 string; nil -> no-op (CONFIRMED).
// No return. CONFIRMED record-and-replicate shape, collapsed per this
// project's own established convention (replicateStateChange): always
// clears the "hidden" bit (+0x3b bit 0x1) on a resolved item.
// ---------------------------------------------------------------------
int stub_item_show(lua_State* L) {
    logCall(L, upLog(L), "item_show", upStateTag(L));
    if (lua_type(L, 1) == LUA_TNIL) return 0; // CONFIRMED: nil -> no-op
    std::string name = argString(L, 1);
    EngineState* st = upState(L);
    if (st->objectResolves().get(name)) { // OPEN until set
        st->itemHidden()[name] = false; // CONFIRMED: item_show always shows (clears "hidden")
        EngineState::replicateStateChange("item_show", name);
    }
    return 0;
}

// ---------------------------------------------------------------------
// item_anim_play (Sec32.6, 0x00a51ad0). Arg 1 item name; arg 2 animation
// name (nil -> id -1); arg 3 optional boolean, nil-gated, default false;
// arg 4 optional string, nil-gated, default none. No return, no network
// record (local-only, CONFIRMED). CONFIRMED branch: if arg 4 resolves, a
// two-state blend-transition (0.3s) and arg 3 is ignored; else if arg 2
// resolves, a single-state start with flag 0x40 (arg3 true) or 0x10
// (arg3 false); neither resolving is a no-op.
// ---------------------------------------------------------------------
int stub_item_anim_play(lua_State* L) {
    logCall(L, upLog(L), "item_anim_play", upStateTag(L));
    std::string item = argString(L, 1);
    std::string animArg2 = (lua_type(L, 2) == LUA_TSTRING) ? argString(L, 2) : std::string();
    bool arg3 = optionalBoolDefault(L, 3, false);
    std::string animArg4 = optionalStringDefault(L, 4, "");
    EngineState* st = upState(L);
    if (st->objectResolves().get(item)) { // OPEN until set
        auto& rec = st->itemAnimState()[item];
        if (!animArg4.empty()) {
            rec.lastBlendTransitionAnim = animArg4; // arg3 ignored on this path, CONFIRMED
            ++rec.blendTransitionCount;
        } else if (!animArg2.empty()) {
            rec.lastSingleStateAnim = animArg2;
            rec.lastFlag = arg3 ? 0x40 : 0x10; // CONFIRMED branch values; HYPOTHESIS meaning
            ++rec.singleStateStartCount;
        }
        // neither resolving: no-op (CONFIRMED)
    }
    return 0;
}

// ---------------------------------------------------------------------
// radio_set_station (Sec32.7, 0x00a5c1c0). Arg 1 vehicle-or-character
// name (reuses objectResolves() - the vehicle resolver redirects through
// a character's cached vehicle); arg 2 number (station), no nil-gate,
// truncated to a signed byte. No return. CONFIRMED: the opcode record is
// ALWAYS sent once the vehicle resolves, even when the local 1-based
// apply (station <= 0 or above count, or itself marked refused, is
// rejected - never switches the radio off).
// ---------------------------------------------------------------------
int stub_radio_set_station(lua_State* L) {
    logCall(L, upLog(L), "radio_set_station", upStateTag(L));
    std::string target = argString(L, 1);
    int64_t stationWide = truncateEa2596(lua_tonumber(L, 2));
    int8_t station = static_cast<int8_t>(stationWide); // CONFIRMED: truncated to a signed byte
    EngineState* st = upState(L);
    if (st->objectResolves().get(target)) { // OPEN until set
        auto& radio = st->vehicleRadio(target);
        ++radio.recordSentCount; // CONFIRMED: always sent once the vehicle resolves
        if (radio.hasRadio.get() && station >= 1 && station <= radio.stationCount.get() && // OPEN until set
            !radio.stationRefused.get(std::to_string(static_cast<int>(station)))) {        // OPEN until set
            radio.currentStation = station;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// helicopter_shoot_vehicle (Sec32.7, 0x00a4d790). Arg 1 helicopter name;
// arg 2 target name (vehicle resolver); arg 3 optional boolean, nil-
// gated, default true (apply spread); arg 4 optional number, nil-gated,
// default 1.0 (spread radius); arg 5 optional boolean, nil-gated, default
// true. Return: 1 boolean - CONFIRMED false if either name fails to
// resolve or the helicopter fails its AI-drive-state gate; otherwise the
// shared dispatcher's own result, which is itself OPEN (its return value
// is invisible in the decompiled view, per spec) and refused rather than
// fabricated.
// ---------------------------------------------------------------------
int stub_helicopter_shoot_vehicle(lua_State* L) {
    logCall(L, upLog(L), "helicopter_shoot_vehicle", upStateTag(L));
    std::string heli = argString(L, 1);
    std::string target = argString(L, 2);
    (void)optionalBoolDefault(L, 3, true);       // apply spread - not modeled (spread offset has no Lua-visible return effect)
    (void)optionalNumberDefault(L, 4, 1.0);      // spread radius - not modeled
    (void)optionalBoolDefault(L, 5, true);       // arg 5 - OPEN role, not modeled
    EngineState* st = upState(L);
    if (!st->objectResolves().get(heli) || !st->objectResolves().get(target)) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!st->helicopter(heli).aiDriveStateOk.get()) { // OPEN until set
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, st->helicopterFireDispatcherResult().get() ? 1 : 0); // OPEN until set - the real dispatcher's own return value is not visible in the decompiled view (Sec32.7)
    return 1;
}

// ---------------------------------------------------------------------
// Batch 2026-10-02 (spec-lua-api-behaviour.md Sec35, the `teleport_coop`
// investigation): teleport_check_done / turn_to_check_done /
// move_to_check_done / vehicle_pathfind_check_done. The real, documented
// reason this batch exists: 3 real missions (dlc2_m01, m13, m19) sit
// suspended forever inside `teleport_coop` (game_lib.lua:2956, a script
// helper - NOT a native), because this project previously left
// teleport_check_done an unregistered generic stub (logged-nil) and a
// script's own `repeat thread_yield() until teleport_check_done(...)`
// idiom can never be satisfied by nil (host.h's own top comment already
// flags this exact real idiom). All 4 share one engine module/convention
// (Sec35.2) - see EngineState's own "teleport_check_done/..." section
// (engine_state.h) for the shared pool's full doc comment; only the
// per-function wrapper shape differs below.
// ---------------------------------------------------------------------

// teleport_check_done (Sec15.17, re-derived 2026-10-02 from the raw
// instruction stream, correcting an earlier inverted decompile-based
// reading). Arguments: 1 mandatory string - an object/character name
// (Sec15.17 itself resolves this via 0x00a281a0; this project has no real
// handle resolver - engine_state.h's own top note - so the raw name
// string IS the lookup key, same convention as CharacterState). Return: 1
// boolean, always (CONFIRMED). Body (CONFIRMED): the name and the literal
// kind 4 (teleport, Sec35.2) are looked up in the shared scripted-request
// pool; the pushed boolean is `status != 0` - true for "done" or "no
// matching request," false only while a request this host is actually
// tracking for this name is genuinely pending.
int stub_teleport_check_done(lua_State* L) {
    logCall(L, upLog(L), "teleport_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindTeleport);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// turn_to_check_done (0x00a61dd0; Sec35.2). This specific wrapper's own
// argument/return shape was NOT independently re-read this pass beyond
// "confirmed to use the SAME shared status function, kind 0" (Sec35.2's
// own closing sentence) - so only what IS confirmed is implemented here:
// a 1-mandatory-string lookup against the shared pool (kind 0 = turn-to),
// the identical shape to the sibling teleport_check_done/
// move_to_check_done. HIGH CONFIDENCE (not CONFIRMED) for the exact
// argument position/count; CONFIRMED for the shared status function and
// the done/pending polarity itself (Sec35.2).
int stub_turn_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "turn_to_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindTurnTo);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// move_to_check_done (Sec22.15, Sec35.2). The real function takes 8
// arguments (a previously-allocated request id; arg 2 the character name;
// a target/group-member selector; a movement-style code; 3 booleans; a
// group-member index) and its fuller body additionally matches a
// per-character cached request-id tag, resolves a target or group member,
// and - only once genuinely arrived (a distance-under-1-unit-plus-
// predicate test this project does not model) - dispatches an arrival
// animation before clearing its own per-character sub-record (+0xc30/
// +0xc34). None of that fuller pipeline is modeled here: deliberately out
// of this task's own narrow scope (Sec35's brief is the shared pool's
// done/pending CONVENTION fix, not a reimplementation of every wrapper's
// full body - the same "don't invent a generic entity shape" boundary
// engine_state.h's own top note already states for CharacterState).
// What IS implemented, CONFIRMED (Sec35.2/Sec22.15's own corrected status-
// code table): arg 2 (the character name) is used as a genuine per-
// request lookup key - NOT ignored - against the SAME shared pool
// teleport_check_done/turn_to_check_done/vehicle_pathfind_check_done
// consult (kind 2 = move-to/pathfind), with the corrected polarity
// (status != 0 -> true, Sec35.2).
int stub_move_to_check_done(lua_State* L) {
    logCall(L, upLog(L), "move_to_check_done", upStateTag(L));
    std::string name = argString(L, 2);
    bool done = upState(L)->scriptedRequestCheckDone(name, EngineState::kScriptedRequestKindMoveOrPathfind);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

// vehicle_pathfind_check_done (Sec9.10) - the REFERENCE reading Sec35.2's
// own cross-check cites as "already correct", i.e. this one was never
// mis-read in the first place. Arguments: 1 mandatory string - a vehicle
// reference. Return: 1 NUMBER (NOT a boolean, CONFIRMED - lua_pushnumber
// then return 1) - the raw 3-way status code itself: 0 = pending, 1 =
// done (consumed), 2 = no active request OR the vehicle failed to resolve
// OR it has no occupant/part in slot 0 (most plausibly a driver check,
// Sec9.10's own text) - both the "no vehicle" and "no active request"
// routes converge on the SAME literal fallback constant, 2.0. This
// project has no vehicle-resolution/occupant registry (deliberately, same
// boundary as every other entity this project does not model) - every
// name defaults unresolvable, itself the real, CONFIRMED fallback for an
// unmodeled vehicle, not a guess (see
// EngineState::vehiclePathfindCheckDoneCode's own doc comment).
int stub_vehicle_pathfind_check_done(lua_State* L) {
    logCall(L, upLog(L), "vehicle_pathfind_check_done", upStateTag(L));
    std::string name = argString(L, 1);
    lua_pushnumber(L, upState(L)->vehiclePathfindCheckDoneCode(name));
    return 1;
}


} // namespace

namespace stub_sections {

const std::vector<std::string>& namesB04() {
    static const std::vector<std::string> names = {
        "action_sequence_end",
        "audio_set_listener_override",
        "audio_clear_listener_override",
        "audio_play_for_navpoint",
        "audio_any_conversation_playing",
        "boss_battle_matt_begin",
        "auto_pickup_disable",
        "waiting_for_player_dialog",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec31 (pause-map
        // stag-mode/district-control family, 5 names, `ui` tag) and Sec32
        // (ranking tranche 04, 25 names, `gameplay` tag) - 30 total, both
        // tags grepped directly from tools/lua_all_registered_1490_tagged.txt.
        "pause_map_stag_current_district_control",
        "pause_map_is_stag_mode",
        "pause_map_is_tutorial_mode",
        "pause_map_set_gps",
        "pause_map_stag_takeover_do_reward",
        "store_interface_is_active",
        "spawn_region_max_spawn_dist",
        "spawn_region_max_spawn_dist_reset",
        "set_ped_override_density",
        "pause_map_tutorial_mode",
        "set_time_of_day",
        "satellite_weapon_mode_exit",
        "set_seatbelt_flag",
        "set_trailing_aim_flag",
        "set_never_turn_on_player",
        "player_revive",
        "player_warp_to_shore_disable",
        "skydive_setup_tank_bailout",
        "qte_human_is_used",
        "party_add_do",
        "npc_is_in_party",
        "npc_go_idle",
        "object_destroy",
        "object_indicator_remove_do",
        "minimap_icon_remove_do",
        "shop_enable_nearest",
        "item_show",
        "item_anim_play",
        "radio_set_station",
        "helicopter_shoot_vehicle",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec35 (the
        // `teleport_coop` investigation) - all 4 `gameplay` in
        // tools/lua_all_registered_1490_tagged.txt (grepped directly,
        // lines 523/843/858/948).
        "teleport_check_done",
        "turn_to_check_done",
        "move_to_check_done",
        "vehicle_pathfind_check_done",
        // Batch 2026-10-02: spec-lua-api-behaviour.md Sec38/Sec39/Sec40
        // ("ranking tranches 06/07/08"), 16 of their combined 75 names -
        // see this file's own per-tranche batch header comments above for
        // which and why. Real cluster tags verified directly against
        // tools/lua_all_registered_1490_tagged.txt (grepped, not assumed):
        // team_make_hostile/vehicle_engine_check_running/skydive_move_
        // to_do are `gameplay`; every other name below is `ui`.
        "vcust_preview_wheel_sizing",
        "team_make_hostile",
        "vehicle_engine_check_running",
        "store_weapon_purchase_ammo",
        "skydive_move_to_do",
        "store_gang_show_question_marks",
        "store_gallery_download_hide_list",
        "store_common_rotate_mouse_drag",
        "save_system_save_game",
        "save_system_load_game",
        "save_system_cancel_coop_load",
        "pcu_is_bra_category",
        "pcu_is_underwear_category",
        "pcu_purchase_slot",
        "pcu_purchase_outfit",
        "pcu_wear_store_outfit",
    };
    return names;
}

void registerB04(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants) {
    if (wants("action_sequence_end")) registerOne(L, state, log, stateTag, "action_sequence_end", openGuard<stub_action_sequence_end>);
    if (wants("audio_set_listener_override")) registerOne(L, state, log, stateTag, "audio_set_listener_override", openGuard<stub_audio_set_listener_override>);
    if (wants("audio_clear_listener_override")) registerOne(L, state, log, stateTag, "audio_clear_listener_override", openGuard<stub_audio_clear_listener_override>);
    if (wants("audio_play_for_navpoint")) registerOne(L, state, log, stateTag, "audio_play_for_navpoint", openGuard<stub_audio_play_for_navpoint>);
    if (wants("audio_any_conversation_playing")) registerOne(L, state, log, stateTag, "audio_any_conversation_playing", openGuard<stub_audio_any_conversation_playing>);
    if (wants("boss_battle_matt_begin")) registerOne(L, state, log, stateTag, "boss_battle_matt_begin", openGuard<stub_boss_battle_matt_begin>);
    if (wants("auto_pickup_disable")) registerOne(L, state, log, stateTag, "auto_pickup_disable", openGuard<stub_auto_pickup_disable>);
    if (wants("waiting_for_player_dialog")) registerOne(L, state, log, stateTag, "waiting_for_player_dialog", openGuard<stub_waiting_for_player_dialog>);
    if (wants("pause_map_stag_current_district_control")) registerOne(L, state, log, stateTag, "pause_map_stag_current_district_control", openGuard<stub_pause_map_stag_current_district_control>);
    if (wants("pause_map_is_stag_mode")) registerOne(L, state, log, stateTag, "pause_map_is_stag_mode", openGuard<stub_pause_map_is_stag_mode>);
    if (wants("pause_map_is_tutorial_mode")) registerOne(L, state, log, stateTag, "pause_map_is_tutorial_mode", openGuard<stub_pause_map_is_tutorial_mode>);
    if (wants("pause_map_set_gps")) registerOne(L, state, log, stateTag, "pause_map_set_gps", openGuard<stub_pause_map_set_gps>);
    if (wants("pause_map_stag_takeover_do_reward")) registerOne(L, state, log, stateTag, "pause_map_stag_takeover_do_reward", openGuard<stub_pause_map_stag_takeover_do_reward>);
    if (wants("store_interface_is_active")) registerOne(L, state, log, stateTag, "store_interface_is_active", openGuard<stub_store_interface_is_active>);
    if (wants("spawn_region_max_spawn_dist")) registerOne(L, state, log, stateTag, "spawn_region_max_spawn_dist", openGuard<stub_spawn_region_max_spawn_dist>);
    if (wants("spawn_region_max_spawn_dist_reset")) registerOne(L, state, log, stateTag, "spawn_region_max_spawn_dist_reset", openGuard<stub_spawn_region_max_spawn_dist_reset>);
    if (wants("set_ped_override_density")) registerOne(L, state, log, stateTag, "set_ped_override_density", openGuard<stub_set_ped_override_density>);
    if (wants("pause_map_tutorial_mode")) registerOne(L, state, log, stateTag, "pause_map_tutorial_mode", openGuard<stub_pause_map_tutorial_mode>);
    if (wants("set_time_of_day")) registerOne(L, state, log, stateTag, "set_time_of_day", openGuard<stub_set_time_of_day>);
    if (wants("satellite_weapon_mode_exit")) registerOne(L, state, log, stateTag, "satellite_weapon_mode_exit", openGuard<stub_satellite_weapon_mode_exit>);
    if (wants("set_seatbelt_flag")) registerOne(L, state, log, stateTag, "set_seatbelt_flag", openGuard<stub_set_seatbelt_flag>);
    if (wants("set_trailing_aim_flag")) registerOne(L, state, log, stateTag, "set_trailing_aim_flag", openGuard<stub_set_trailing_aim_flag>);
    if (wants("set_never_turn_on_player")) registerOne(L, state, log, stateTag, "set_never_turn_on_player", openGuard<stub_set_never_turn_on_player>);
    if (wants("player_revive")) registerOne(L, state, log, stateTag, "player_revive", openGuard<stub_player_revive>);
    if (wants("player_warp_to_shore_disable")) registerOne(L, state, log, stateTag, "player_warp_to_shore_disable", openGuard<stub_player_warp_to_shore_disable>);
    if (wants("skydive_setup_tank_bailout")) registerOne(L, state, log, stateTag, "skydive_setup_tank_bailout", openGuard<stub_skydive_setup_tank_bailout>);
    if (wants("qte_human_is_used")) registerOne(L, state, log, stateTag, "qte_human_is_used", openGuard<stub_qte_human_is_used>);
    if (wants("party_add_do")) registerOne(L, state, log, stateTag, "party_add_do", openGuard<stub_party_add_do>);
    if (wants("npc_is_in_party")) registerOne(L, state, log, stateTag, "npc_is_in_party", openGuard<stub_npc_is_in_party>);
    if (wants("npc_go_idle")) registerOne(L, state, log, stateTag, "npc_go_idle", openGuard<stub_npc_go_idle>);
    if (wants("object_destroy")) registerOne(L, state, log, stateTag, "object_destroy", openGuard<stub_object_destroy>);
    if (wants("object_indicator_remove_do")) registerOne(L, state, log, stateTag, "object_indicator_remove_do", openGuard<stub_object_indicator_remove_do>);
    if (wants("minimap_icon_remove_do")) registerOne(L, state, log, stateTag, "minimap_icon_remove_do", openGuard<stub_minimap_icon_remove_do>);
    if (wants("shop_enable_nearest")) registerOne(L, state, log, stateTag, "shop_enable_nearest", openGuard<stub_shop_enable_nearest>);
    if (wants("item_show")) registerOne(L, state, log, stateTag, "item_show", openGuard<stub_item_show>);
    if (wants("item_anim_play")) registerOne(L, state, log, stateTag, "item_anim_play", openGuard<stub_item_anim_play>);
    if (wants("radio_set_station")) registerOne(L, state, log, stateTag, "radio_set_station", openGuard<stub_radio_set_station>);
    if (wants("helicopter_shoot_vehicle")) registerOne(L, state, log, stateTag, "helicopter_shoot_vehicle", openGuard<stub_helicopter_shoot_vehicle>);
    if (wants("teleport_check_done")) registerOne(L, state, log, stateTag, "teleport_check_done", openGuard<stub_teleport_check_done>);
    if (wants("turn_to_check_done")) registerOne(L, state, log, stateTag, "turn_to_check_done", openGuard<stub_turn_to_check_done>);
    if (wants("move_to_check_done")) registerOne(L, state, log, stateTag, "move_to_check_done", openGuard<stub_move_to_check_done>);
    if (wants("vehicle_pathfind_check_done")) registerOne(L, state, log, stateTag, "vehicle_pathfind_check_done", openGuard<stub_vehicle_pathfind_check_done>);

    // Batch 2026-10-02: spec-lua-api-behaviour.md Sec38/Sec39/Sec40 ("ranking tranches 06/07/08"), 16 names.
    if (wants("vcust_preview_wheel_sizing")) registerOne(L, state, log, stateTag, "vcust_preview_wheel_sizing", openGuard<stub_vcust_preview_wheel_sizing>);
    if (wants("team_make_hostile")) registerOne(L, state, log, stateTag, "team_make_hostile", openGuard<stub_team_make_hostile>);
    if (wants("vehicle_engine_check_running")) registerOne(L, state, log, stateTag, "vehicle_engine_check_running", openGuard<stub_vehicle_engine_check_running>);
    if (wants("store_weapon_purchase_ammo")) registerOne(L, state, log, stateTag, "store_weapon_purchase_ammo", openGuard<stub_store_weapon_purchase_ammo>);
    if (wants("skydive_move_to_do")) registerOne(L, state, log, stateTag, "skydive_move_to_do", openGuard<stub_skydive_move_to_do>);
    if (wants("store_gang_show_question_marks")) registerOne(L, state, log, stateTag, "store_gang_show_question_marks", openGuard<stub_store_gang_show_question_marks>);
    if (wants("store_gallery_download_hide_list")) registerOne(L, state, log, stateTag, "store_gallery_download_hide_list", openGuard<stub_store_gallery_download_hide_list>);
    if (wants("store_common_rotate_mouse_drag")) registerOne(L, state, log, stateTag, "store_common_rotate_mouse_drag", openGuard<stub_store_common_rotate_mouse_drag>);
    if (wants("save_system_save_game")) registerOne(L, state, log, stateTag, "save_system_save_game", openGuard<stub_save_system_save_game>);
    if (wants("save_system_load_game")) registerOne(L, state, log, stateTag, "save_system_load_game", openGuard<stub_save_system_load_game>);
    if (wants("save_system_cancel_coop_load")) registerOne(L, state, log, stateTag, "save_system_cancel_coop_load", openGuard<stub_save_system_cancel_coop_load>);
    if (wants("pcu_is_bra_category")) registerOne(L, state, log, stateTag, "pcu_is_bra_category", openGuard<stub_pcu_is_bra_category>);
    if (wants("pcu_is_underwear_category")) registerOne(L, state, log, stateTag, "pcu_is_underwear_category", openGuard<stub_pcu_is_underwear_category>);
    if (wants("pcu_purchase_slot")) registerOne(L, state, log, stateTag, "pcu_purchase_slot", openGuard<stub_pcu_purchase_slot>);
    if (wants("pcu_purchase_outfit")) registerOne(L, state, log, stateTag, "pcu_purchase_outfit", openGuard<stub_pcu_purchase_outfit>);
    if (wants("pcu_wear_store_outfit")) registerOne(L, state, log, stateTag, "pcu_wear_store_outfit", openGuard<stub_pcu_wear_store_outfit>);
}

} // namespace stub_sections
} // namespace sr3luahost
