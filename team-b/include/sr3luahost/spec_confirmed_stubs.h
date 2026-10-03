// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// The 22 Lua-visible names this task promotes from stub_registry.h's
// generic "log and return nil" behavior to real, spec-confirmed behavior,
// backed by engine_state.h (13 as of the original pass below, +9 more from
// a same-shape follow-up pass, see further down this comment). The first
// 12 of the original 13 were selected by the orchestrator
// as the intersection of (a) the top-22 real runtime stub-hit ranking
// (tools/lua_runtime_stub_ranking.tsv) and (b) already fully adversarially-
// reviewed sections of spec-lua-api-behaviour.md Sec1-9:
//   coop_is_active (Sec3.1), game_get_key_name (Sec2.3),
//   game_UI_audio_play (Sec2.2), game_audio_get_audio_id (Sec8.22),
//   game_get_key_name_for_action (Sec8.23), game_peg_load_with_cb
//   (Sec8.24), ai_add_enemy_target (Sec3.9), on_take_damage (Sec3.13),
//   set_ignore_ai_flag (Sec3.4), get_max_hit_points (Sec7.12),
//   set_current_hit_points (Sec7.31), ai_clear_scripted_action (Sec7.24).
//
// The 13th, vint_object_find (spec-lua-bindings.md Sec13.7/Sec15), was
// folded in mid-task per the orchestrator's relay of a same-day Team A
// finding: this global was simply UNREGISTERED (a nil global, not merely a
// "wrong stub behavior"), accounting for 43% of all hook-fire errors -
// the single highest-impact real gap this project had measured. Verified
// directly from spec-lua-bindings.md Sec13.7/Sec15 (read in full, not
// taken on the relay's word alone) before implementing.
//
// A same-shape follow-up pass (2026-09-30) added 9 more names, all from
// spec-lua-api-behaviour.md Sec10.1-Sec10.9: store_vehicle_get_state
// (Sec10.1), Completion_is_client (Sec10.2), game_hud_update_inventory
// (Sec10.3), tutorial_advance (Sec10.4), minimap_icon_add_do (Sec10.5),
// object_indicator_add_do (Sec10.6), on_qte_animation_trigger (Sec10.7),
// on_revived (Sec10.8), game_get_coop_join_type (Sec10.9) - see
// lua_spec_confirmed_stubs.cpp's own functions 14-22 for each one's full
// citation/reasoning, and engine_state.h for the new fields/structs
// (CharacterState::onRevivedCallback/minimapIcons/objectIndicators;
// EngineState::vehicleStoreActive_/hasLocalPlayer_/
// hudInventoryRefreshCount_/tutorialAdvanceCounts_/
// qteAnimationTriggerCallback_/coopJoinType_) backing them.
//
// Cloud phase (2026-09-30): +1, zscene_is_loaded (spec-lua-api-behaviour.md
// Sec14.23), one of the HANDOFF Sec9.143 mission-driving blockers. Its OPEN
// per-record branch is a labelled stub - see its own doc comment. The other
// Sec9.143 blockers (fade_is_fully_faded_out/_in, vint_is_std_res) have no
// behaviour spec yet and stay generic stubs (requested from Team A).
//
// Batch 2026-10-01: +9 from spec-lua-api-behaviour.md Sec26.24-Sec26.28 -
// fade_in, fade_is_fully_faded_out, fade_is_fully_faded_in, sfx_faded_in,
// Screen_fade_transition_complete (screen fade, Sec26.24), zscene_prep
// (Sec26.25), game_get_is_host (Sec8.27/Sec26.28), vint_is_std_res and
// vint_get_safe_frame (Sec26.26); zscene_is_loaded, fade_out, sfx_faded_out,
// coop_is_active, Completion_is_client, tutorial_advance and
// store_vehicle_get_state follow the corrected specs. Registering
// Screen_fade_transition_complete in a state also makes that state the
// screen_fade UI state and runs the fade init there (see the .cpp).
//
// Batch 2026-10-02: +8 from spec-lua-bindings.md Sec18-Sec21 ("Vint UI
// API") - vint_object_first_child/vint_object_clone (Sec18),
// vint_get_time_index/vint_dataitem_get (Sec19), vint_set_property/
// vint_get_property (Sec20), vint_dataresponder_finished/
// vint_internal_dataresponder_request (Sec21). Unlike the historical
// 2026-09-30 pause described below (which was specifically about
// spec-lua-api-behaviour.md §1-§5), Sec18-Sec21 were committed as part of
// the SAME sync (`0f33ef7`) the orchestrator's own commit message marks
// "final, byte-identical to Team A" - the per-section "DESK-PASS ... NOT
// yet cleared for implementation" review-status line each of Sec18-Sec21
// (and in fact nearly every section of this spec file, including Sec15's
// own vint_object_find above) carries is this document's own standing
// internal-QA tracking convention (whether a SECOND independent reviewer
// re-derived a section from the executable), not a per-feature
// implementation gate - verified directly before acting by checking that
// vint_object_find (Sec13.7/Sec15), the very first function this file ever
// implemented, carries the identical boilerplate. See
// lua_spec_confirmed_stubs.cpp's own file-header comment above functions
// 23+ onward for each of the 8 names' full citation/reasoning, and
// engine_state.h for the new state (VdoObject::firstChildHandle,
// VintTaggedValue/VintDataItem/VintDataResponderRecord,
// EngineState::vdoObjectFirstChild/cloneVdoObject/vintGetTimeIndex/
// registerVintDataItemForTesting/setVintProperty/findVintProperty/
// registerDataResponderForTesting/dataResponderFinished/
// dataResponderRequest).
//
// Batch 2026-10-02: +9 from spec-lua-api-behaviour.md Sec30 ("ranking
// tranche 03", 25 previously-unspecced names, final/committed) - vehicle_
// set_invulnerable_to_player_explosives, vehicle_disable_explosion_and_
// damage_vfx, vehicle_set_special_override_never_ghost, vehicle_clear_all_
// radio_locks, vehicle_is_vtol (Sec30.5, one shared VehicleState/force-flag
// infrastructure), auto_pickup_enable (Sec30.3), vehicle_exit_group_do /
// vehicle_exit_group_check_done (Sec30.5, including an explicit, labelled
// HOST-SAFETY guard on a CONFIRMED null-`this` crash-shaped edge), and
// team_make_unfriendly (Sec30.6, including an explicit, labelled HOST-
// SAFETY guard on a CONFIRMED out-of-bounds relation-matrix crash-shaped
// edge). Selected as the intersection of (a) real non-zero measured call
// counts in this session's own latest trace (results/verify_sched_check/
// verdict_stub_hits_with_missions.tsv: vehicle_disable_explosion_and_
// damage_vfx 28 calls, auto_pickup_enable 1 call) and (b) the task's own
// explicit, mandatory crash-guard requirement for the other two; the
// remaining 16 of this tranche's 25 names had zero measured real hits AND
// would each need substantial further new engine-state modeling, so they
// stay ordinary generic logging stubs this pass (see lua_spec_confirmed_
// stubs.cpp's own Sec30 batch header comment for the full list and
// reasoning). See engine_state.h for the new state (CharacterState::
// currentVehicleName/isAlive; VehicleState and EngineState::vehicles()/
// getOrCreateVehicle(); EngineState::autoPickupEnabled()/
// vehicleExitGroupNullThisGuardCount()/registerTeamIdForTesting()/
// resolveTeamId()/setTeamRelationIfInBounds()/teamRelationForTesting()/
// teamRelationOobGuardCount()).
// Batch 2026-10-02 (second, same day): +30 from spec-lua-api-behaviour.md
// Sec31 (`pause_map_stag_current_district_control` and its 4 fully-
// confirmed pause-map siblings - `pause_map_is_stag_mode`,
// `pause_map_is_tutorial_mode`, `pause_map_set_gps`,
// `pause_map_stag_takeover_do_reward` - all 5 members of the SAME 9-entry
// UI pause-map registrar, FUN_007dfae0, Sec31.1's own table; the other 4
// table entries - get_world_income_dollars, pause_map_add_bookmark,
// pause_map_drag_map, pause_map_zoom - have no CONFIRMED body anywhere in
// Sec31 and are NOT implemented, still ordinary generic stubs) and Sec32
// ("ranking tranche 04", 25 previously-unspecced gameplay-registrar
// names: store_interface_is_active, spawn_region_max_spawn_dist(_reset),
// set_ped_override_density, pause_map_tutorial_mode, set_time_of_day,
// satellite_weapon_mode_exit (Sec32.1); set_seatbelt_flag,
// set_trailing_aim_flag, set_never_turn_on_player (Sec32.2);
// player_revive, player_warp_to_shore_disable,
// skydive_setup_tank_bailout, qte_human_is_used (Sec32.3); party_add_do,
// npc_is_in_party, npc_go_idle (Sec32.4); object_destroy,
// object_indicator_remove_do, minimap_icon_remove_do,
// shop_enable_nearest (Sec32.5); item_show, item_anim_play (Sec32.6);
// radio_set_station, helicopter_shoot_vehicle (Sec32.7)). Real call-count
// evidence (results/stub_ranking_with_specced_20261002.tsv, the most
// recent mission-drive verdict TSV as of this batch): the Sec31 target
// itself is real and significant (303 real calls, all pre-mission UI
// bring-up, matching Sec31's own "called 303 times" claim exactly); NONE
// of Sec32's 25 tranche-04 names appear in that same real mission-drive
// trace at all (0 real calls measured) - an honest, measured finding, not
// an assumption, so no further internal-depth prioritization was applied
// within the tranche (breadth across all 25, each at the depth its own
// CONFIRMED text supports, rather than concentrating depth on an
// arbitrary subset real evidence doesn't actually favor).
//
// The 0x009df3d0 correction (Sec14.31/Sec32.8, folded in as part of this
// same batch): this document's own §8.17/§14.31 previously read 0x009df3d0
// as a "matching" lookup for the local player's own roster entry; ranking
// tranche 04 (§32.1-A.7) corrects this to "the first live-roster entry
// that is NOT the local player" (the remote co-op player), returning null
// only when every roster entry equals the local player - NOT a fallback
// that could wrongly resolve to the local player itself with no co-op
// session. A direct grep of this project's own source for "0x009df3d0"
// before this batch found exactly one prior consumer,
// `customization_restore_player_rig` (Sec28.12,
// EngineState::PlayerRig::coopPlayerPresent) - already modeled as a plain
// presence boolean, never as a name-matching lookup, so it needed NO
// change. The only NEW consumer this batch adds,
// `satellite_weapon_mode_exit`, applies the corrected reading directly:
// its bit-0x2 selector only acts on a player when
// playerRig().coopPlayerPresent is true. See
// lua_spec_confirmed_stubs.cpp's own batch header comment (above the 30
// functions) and engine_state.h's own per-field/per-method doc comments
// for the full per-function reasoning.
//
// Batch 2026-10-02 (spec-lua-api-behaviour.md Sec35, the `teleport_coop`
// investigation): +4 - teleport_check_done (Sec15.17), turn_to_check_done,
// move_to_check_done (Sec22.15), vehicle_pathfind_check_done (Sec9.10). All
// 4 were plain generic logging stubs (returning nil) before this batch -
// the real, measured reason 3 missions (dlc2_m01, m13, m19) sat suspended
// forever inside `teleport_coop` (a game_lib.lua script helper, not a
// native): a `repeat thread_yield() until teleport_check_done(...)` idiom
// can never be satisfied by nil. All 4 share one engine module, the
// 200-slot scripted-request completion pool (Sec35.2); see
// engine_state.h's own "teleport_check_done/..." section for the shared
// mechanism and lua_spec_confirmed_stubs.cpp for each wrapper's own
// citation/scope note (move_to_check_done and vehicle_pathfind_check_done
// each implement only part of their real, fuller body - see their own doc
// comments for exactly what is NOT modeled and why).
//
// Batch 2026-10-02 (resumed session): +28. First, the long-standing Sec3.7/
// Sec20.1 vehicle-invulnerability bit conflict - settled by ranking tranche
// 14 (Sec46.1: bit 0x01 of vehicle +0x1d7a; Sec3.7 was right, Sec20.1's own
// "bit 0x8" was the error) - is now actually IMPLEMENTED (previously only
// corrected in the spec text): turn_invulnerable (Sec3.7), turn_vulnerable
// (Sec3.8), vehicle_set_vulnerable (Sec20.1), vehicle_is_invulnerable
// (Sec46.5). Second, a curated subset of ranking tranches 12-14 (Sec44/
// Sec45/Sec46, 75 previously-unspecced names total): character_fake_
// revival_start/_end, character_take_human_shield_check_done, vehicle_
// turret_base_to_do, vehicle_lights_on, vehicle_tire_indicators_alive
// (Sec45.1/Sec46.4/Sec46.5 - each supporting this same bit-conflict
// correction or its own already-scaffolded engine-state); game_is_pc_dx11/
// game_get_ps3_button_swap/game_record_mode_is_supported/game_record_
// mode_is_active/game_show_party_ui/game_show_community_sessions_ui
// (Sec44.1, 6 trivial constant-false/no-op names sharing 2 already-known
// native handlers); flee_to_navpoint (Sec44.5/Sec44.6); character_hidden,
// ambient_gang_spawn_enable, cellphone_animate_stop_do, cell_camera_enable/
// cell_camera_is_enabled (Sec45.1/Sec45.4/Sec45.6); ambient_cop_spawn_
// enable, action_nodes_shouldnt_flee, action_nodes_restrict_spawning,
// whored_countdown_finished, vehicle_set_tire_durability, vehicle_set_
// tire_damage_multiplier (Sec46.3/Sec46.5/Sec46.6). Selected (same
// reasoning Sec30/Sec33 above already precedent) because a real call-count
// check (results/stub_ranking_with_specced_20261002.tsv) finds ZERO of
// these 3 tranches' 75 names with any measured real call at all - no
// signal to prioritize BY, so breadth was applied instead, bounded to
// names implementable without inventing a new subsystem (no networking,
// Steam, path/node graph, cellphone activity-record machine, or key-
// binding table - see lua_spec_confirmed_stubs.cpp's own batch header
// comment, right before stub_turn_invulnerable, for exactly which tranche-
// 12/13/14 names were deliberately left as ordinary generic stubs this
// pass, and why). See engine_state.h for the new state (CharacterState::
// hiddenFlag/fleeToNavpointRequestCount, VehicleState::tireDurability/
// tireDamageMultiplier, plus the already-scaffolded forceFlags1c98/
// forceFlags1d7a/flagsE4/humanShieldHostageName/lightsForceFlags/
// tireIndicatorObjectDisabled/seat0Occupied; EngineState::
// ambientGangSpawnEnabled()/cellCameraEnabled()/ambientCopSpawnEnabled()/
// actionNodesShouldntFlee()/actionNodesRestrictSpawning()).
//
// IMPLEMENTED PRE-REVIEW, PENDING EXE RE-CLEARANCE (2026-09-30): the desk
// review now marks spec-lua-api-behaviour.md §1-§5 "NOT yet cleared for
// implementation". Six functions here come from those sections and were
// implemented pre-pause, when they counted as reviewed:
//   game_UI_audio_play (§2.2, NEEDS-EXE), game_get_key_name (§2.3,
//   NEEDS-EXE), coop_is_active (§3.1, NEEDS-EXE), set_ignore_ai_flag
//   (§3.4, NEEDS-EXE: 0x004e2050's `this` and the read bit's mask),
//   ai_add_enemy_target (§3.9, DESK-PASS), on_take_damage (§3.13,
//   DESK-PASS).
// They are kept working (manager decision pending; option (a) = label, no
// behaviour change) and nothing new is implemented from §1-§5 until Team A's
// executable re-derivation clears it.
//
// Deliberately NOT touched (still out of scope):
// thread_check_done (this project's own scaffold mechanism, thread_scheduler.h).
// The 8 vint_object_* lifecycle siblings Sec15 originally flagged as
// "likely equally high-impact if also missing" (create/destroy/
// first_child/next_sibling/parent/set_parent/add_child/
// get_name_from_handle) are, as of the 2026-10-02 Sec18-Sec21 batch above,
// down to 6 still NOT implemented (create/destroy/next_sibling/parent/
// set_parent/get_name_from_handle - first_child is now
// vint_object_first_child, Sec18.1; vint_object_clone, Sec18.2, was also
// traced this same pass though Sec15 did not originally name it a
// sibling) - still not individually behavior-traced beyond the shared
// handle resolver. The remaining 6, and the other 38 names in the same
// 55-name registrar not covered by Sec18-Sec21 either (vint_object_clone_rename,
// vint_set_property_typed among them - Sec18's own front matter scopes
// this pass to exactly 8 names), are registered as ordinary generic
// logging stubs by simply being present in the current tagged registration
// list (tools/lua_all_registered_1490_tagged.txt) and NOT in this file's
// specConfirmedStubNames() - see host.h/lua_host.cpp's own filtering.
//
// Each function below implements ONLY what its own spec (sub)section
// states at CONFIRMED tier; a HIGH-CONFIDENCE-tier sub-detail is either
// implemented with an explicit "HIGH CONFIDENCE, not CONFIRMED" comment at
// its own call site (engine_state.h documents which per-field) or left as
// a stated, explicit gap (a TODO comment) where implementing it would
// require guessing further - never silently upgraded. See each function's
// own doc comment in lua_spec_confirmed_stubs.cpp for the specific
// per-function reasoning.
//
// Registration shape mirrors thread_scheduler.h's own established
// convention exactly (registerOne(): 3 upvalues - EngineState*, HitLog*,
// stateTag - per real Lua CFunction, each one already a real C function
// implementing its own name, not a shared name-parameterized trampoline
// like stub_registry.h's generic path, since these 12 each have genuinely
// distinct behavior). Every call still logs into the SAME HitLog every
// other stub/hook uses (stub_registry.h/thread_scheduler.h), so these 12
// names keep appearing in the exact same whole-run stub-hit ranking
// tools/lua_host_run.cpp already reports - nothing about the MEASUREMENT
// path changes, only what happens on a real call.
#pragma once

#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/stub_registry.h"

namespace sr3luahost {

// Registers exactly the names in `namesToRegister` (a subset of
// specConfirmedStubNames() - see that function's own doc comment) into
// `L`, backed by `state` and logging into `log` under `stateTag`
// ("gameplay"/"ui" - see host.h's own top comment). A name not present in
// `namesToRegister` is left untouched by this call. Host (host.h) calls
// this once per state, passing each state's own real subset (these 22
// names do NOT all share one cluster tag - see lua_host.cpp for how the
// split is computed from the actual tagged registration list, never
// hardcoded here), AFTER filtering all 22 names out of the generic
// registerStubs() call for both states (so a plain-global lua_setglobal
// from either path can never race/overwrite the other for the same name).
void registerSpecConfirmedStubs(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                                 const std::vector<std::string>& namesToRegister);

// The literal set of 22 names this file registers - exposed so Host (or a
// test) can filter them out of the generic tagged-registration-list split
// without duplicating the literal list a second time.
const std::vector<std::string>& specConfirmedStubNames();

// Bare globals registered outside the tagged registration list
// (spec-lua-bindings.md Sec13.2/Sec16.3/Sec16.4: the 24 names registered by
// 0x00e0f900). Answered 2026-10-01 (jobs 20261001T020218-team-a-bgcx,
// 20261001T114555-team-a-lgdz): all 24, into BOTH states, before any preload.
// Each row is also in specConfirmedStubNames(); Host registers the rows
// through registerBareGlobals() (bare_globals.h) into the states they name,
// before every other registration.
struct SpecBareGlobal {
    std::string name;
    bool gameplay = false;
    bool ui = false;
    std::string spec; // section that confirms the state(s)
};
const std::vector<SpecBareGlobal>& specBareGlobals();

} // namespace sr3luahost
