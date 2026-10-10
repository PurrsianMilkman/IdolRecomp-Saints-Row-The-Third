// Declares the per-section name-list/registration pair each
// lua_spec_confirmed_stubs_b0N_*.cpp defines, so
// lua_spec_confirmed_stubs_registry.cpp (the coordinator that implements
// the PUBLIC specConfirmedStubNames()/registerSpecConfirmedStubs() API from
// include/sr3luahost/spec_confirmed_stubs.h) can call all 7 in order
// without needing to see any individual stub_* function - those stay
// private (internal linkage, each section's own anonymous namespace) to
// their own .cpp, exactly as every stub_* in the original single-file
// lua_spec_confirmed_stubs.cpp was private to IT.
//
// Why a pair per section, and not just one combined call: `namesBxx()`
// needs no `wants` predicate and is occasionally useful on its own (future
// tooling), and keeping it separate from `registerBxx()` mirrors the
// original file's own two-separate-functions shape
// (specConfirmedStubNames() / registerSpecConfirmedStubs()) one level down.
//
// See lua_spec_confirmed_stubs_registry.cpp's own top comment for the full
// split rationale.
#pragma once

#include "lua_spec_confirmed_stubs_common.h"

namespace sr3luahost {
namespace stub_sections {

// b01: functions 1-27 of the original file's own numbering (coop_is_active
// through sfx_faded_out) - spec-lua-api-behaviour.md Sec2/3/4/6/7/8/9/10/14/
// 15/26 (scattered), plus vint_object_find (spec-lua-bindings.md
// Sec13.7/Sec15). See lua_spec_confirmed_stubs_b01_initial.cpp.
const std::vector<std::string>& namesB01();
void registerB01(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b02: Batch 2026-10-01, spec-lua-api-behaviour.md Sec27 (UI cluster) +
// Sec28 (gameplay), 50 names. See lua_spec_confirmed_stubs_b02_sec27_28.cpp.
const std::vector<std::string>& namesB02();
void registerB02(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b03: spec-lua-bindings.md Sec18-Sec21 ("Vint UI API", 8 names) + ranking
// tranche 03 (Sec30, 9 names) + ranking tranche 05 (Sec33, 25 names) and the
// Sec14.1 0x009df3d0 correction. See
// lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp.
const std::vector<std::string>& namesB03();
void registerB03(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b04: Sec31 (pause-map/stag-mode family) + ranking tranche 04 (Sec32) +
// the Sec35 teleport_coop investigation (4 names) + ranking tranches 06/07/
// 08 (Sec38/Sec39/Sec40, 16 names). See
// lua_spec_confirmed_stubs_b04_r04_r06_08.cpp.
const std::vector<std::string>& namesB04();
void registerB04(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b05: the Sec3.7/Sec20.1/Sec46.1 vehicle-invulnerability bit-conflict
// resolution + a curated subset of ranking tranches 12-14 (Sec44/45/46) +
// ranking tranches 15/17/20/22/23 real-hit batch + the action_play_do/
// get_dist/player_names_get_all real-hit probe-mode additions. See
// lua_spec_confirmed_stubs_b05_invuln_r15_23.cpp.
const std::vector<std::string>& namesB05();
void registerB05(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b06: the player runtime batch (player_controls_disable/enable,
// player_movement_disable) + the 2026-10-07 Section 6 full-document-sweep
// batch + the 2026-10-07 Section 7 full-document-sweep batch (which
// includes player_movement_enable, the 4th player-runtime name). See
// lua_spec_confirmed_stubs_b06_playerrt_sec6_7.cpp.
const std::vector<std::string>& namesB06();
void registerB06(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b07: the 2026-10-07 Section 9 full-document-sweep batch + the dedicated
// vehicle_chase (Sec9.4) pass + the 2026-10-07 Section 8 full-document-
// sweep batch + character_is_in_vehicle (Sec3.10) + the Team-A-cleared
// batch (commit 97a9b8d, Sec1.1/1.2/1.4/1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/
// 3.6). See lua_spec_confirmed_stubs_b07_sec8_9_teamA.cpp.
const std::vector<std::string>& namesB07();
void registerB07(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b08: wave 1 batch 1 (2026-10-08), spec-lua-api-behaviour.md §1-4
// (trigger/mission/teleport/combat-hook core) - 15 of the batch's
// originally-assigned 21 names actually cleared for implementation (6
// deferred, each still carrying its own un-superseded "NOT yet cleared
// for implementation" Review-status line - see this file's own top
// comment for the full list). See
// lua_spec_confirmed_stubs_b08_batch1_core.cpp.
const std::vector<std::string>& namesB08();
void registerB08(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b09: wave 1, Batch 2 of the 2026-10-08 7-agent parallel fan-out
// (results/lua_natives_sec10_28_fanout_scoping_20261008.md) - "Character/
// vehicle/HUD/audio residue (Sec5-9, ranked 51-150)", 25 names from
// spec-lua-api-behaviour.md Sec7/Sec8/Sec9. See
// lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp's own top comment for
// the full list and citations. Named b09 (not b08) per the orchestrator's
// own dispatch of this wave - this wave's other batches are separate
// agents/worktrees, each merging under its own bNN name; a future
// merge reconciles the exact numbering/ordering across all of them.
const std::vector<std::string>& namesB09();
void registerB09(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b14: Batch 2026-10-08, wave 1 Batch 7 of a 7-agent parallel fan-out
// (results/lua_natives_sec10_28_fanout_scoping_20261008.md) - spec-lua-
// api-behaviour.md Sec30 ("ranking tranche 03"), the 16 of its 25 names
// NOT already wired by b03 (character_dont_regenerate,
// character_clear_combat_move, boss_battle_matt_set_player_as_avatar,
// boss_battle_matt_hide_wings, audio_ambient_emitter_pause/_resume,
// ai_suggest_action, ai_do_scripted_advance, vehicle_set_2d_tank_controls,
// vehicle_rc_get_signal_strength, vehicle_stop_do, vehicle_pathfind_to_do,
// vehicle_set_malfunctioning, vehicle_component_detach, turn_to_do,
// teleport_vehicle_to_object). Numbered b14 (not b08) per the refactor's
// own reviewer guidance: a new file, never retrofitted into b01-b07. See
// lua_spec_confirmed_stubs_b14_batch7_tranche03.cpp.
const std::vector<std::string>& namesB14();
void registerB14(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b12: wave 1, Batch 5 of the 7-agent parallel fan-out (2026-10-08) -
// spec-lua-api-behaviour.md Sec15 (ranks 251-350, 25 names) + Sec16
// (ranks 251-350 UI-cluster, 16 names), 41 names total - the HUD-slot/
// customization/pause-menu cluster. A new file per the refactor's own
// reviewer guidance (not retrofit into b01-b07). See
// lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp.
const std::vector<std::string>& namesB12();
void registerB12(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b11: wave 1 batch 4 of the Sec10-28 lua-natives fan-out (2026-10-08) -
// spec-lua-api-behaviour.md Sec13 (28 names) + a 27-name subset of Sec14
// (ranks ~151-250, "flag-setters/hooks/combat cluster"), 55 names total.
// See lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp.
const std::vector<std::string>& namesB11();
void registerB11(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b10: wave 1, Batch 3 of the 7-agent parallel fan-out (results/
// lua_natives_sec10_28_fanout_scoping_20261008.md) - spec-lua-api-
// behaviour.md Sec11 (12 PCR/PCU/store/pause-menu UI-cluster names) +
// Sec12 (28 of its 29 gameplay-cluster names; the 29th,
// traffic_disable_lanes, is already in b06). 40 names total. A NEW file,
// not a retrofit into b01-b07, per that scoping report's own guidance -
// see lua_spec_confirmed_stubs_b10_batch3_pcr_vehicle.cpp.
const std::vector<std::string>& namesB10();
void registerB10(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b15: wave 2 batch W2-1 (2026-10-08) - "core dispatchers", 13 names:
// vehicle_exit_do (Sec21.11), vehicle_exit_check_done (Sec24.9),
// vehicle_exit_guardian_angel/vehicle_enter_do/vehicle_enter_guardian_angel/
// vehicle_enter_group_do/vehicle_enter_group_check_done (Sec38.1),
// vehicle_enter_check_done (Sec22.3), move_to_do (Sec21.22),
// vehicle_pathfind_navmesh_do (Sec22.1), teleport_to_object (Sec22.4),
// character_damage (Sec20.32), qte_start (Sec20.9). See
// lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp.
const std::vector<std::string>& namesB15();
void registerB15(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b16: wave 2, Batch W2-3 (2026-10-08) - spec-lua-api-behaviour.md Sec20
// ("ranks 351-450 tranche, part C", 29 of its 32 names) + Sec21 ("ranks
// 351-450 tranche, part A", 28 of its 30 names), 57 names total - "Sec20+
// Sec21 gameplay residue". A NEW file, not a retrofit into any existing
// bNN, per the standing fan-out convention (see
// lua_spec_confirmed_stubs_registry.cpp's own top comment) - see
// lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp's own top comment for the
// full name list and citations.
const std::vector<std::string>& namesB16();
void registerB16(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b22: wave 2 batch W2-4 (2026-10-08) - spec-lua-api-behaviour.md Sec22
// ("ranks 351-450 tranche, part B", 24 of its 30 names - the other 6 belong
// to sibling wave-2 batches). See
// lua_spec_confirmed_stubs_b22_wave2_sec22.cpp.
const std::vector<std::string>& namesB22();
void registerB22(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b23: wave 2 batch W2-4 (2026-10-08) - spec-lua-api-behaviour.md Sec24
// ("ranks 451-550 tranche, part A", 27 of its 28 names) + Sec25 ("ranks
// 451-550 tranche, part B", 27 of its 28 names), 54 names total. See
// lua_spec_confirmed_stubs_b23_wave2_sec24_25.cpp.
const std::vector<std::string>& namesB23();
void registerB23(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b26: wave 2, Batch W2-7 (2026-10-08) - spec-lua-api-behaviour.md Sec38-60,
// "gameplay A: vehicles/world/spawn/effects/audio/camera" (113 names total
// with b27). This file holds the vehicle/helicopter/airplane/traffic half
// (39 names). See lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp.
const std::vector<std::string>& namesB26();
void registerB26(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b27: wave 2, Batch W2-7 (2026-10-08) - the spawn/world/effects/audio/
// camera/mission-specific half (74 names). See lua_spec_confirmed_stubs_
// b27_wave2_spawn_world_audio.cpp.
const std::vector<std::string>& namesB27();
void registerB27(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b24: wave 2, Batch W2-5 (2026-10-08) - "Customization frontends" half 1 of
// 2: customization_*/pcr_*/pcu_* (spec-lua-api-behaviour.md Sec19/Sec23/
// Sec26/Sec40/Sec43/Sec60.1), 63 names. HARDENING batch (see this file's
// own doc comment on namesB25()/registerB25() below for the shared policy
// note). See lua_spec_confirmed_stubs_b24_wave2_pcu_pcr.cpp.
const std::vector<std::string>& namesB24();
void registerB24(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b25: wave 2, Batch W2-5 (2026-10-08) - "Customization frontends" half 2 of
// 2: vcust_*/store_*/garage_*/crib_*/gang_customization_*/building_purchase_
// is_crib_being_purchased (spec-lua-api-behaviour.md Sec19/Sec23/Sec26/
// Sec38/Sec39/Sec41/Sec49/Sec52/Sec53/Sec56/Sec57/Sec59), 60 names.
// HARDENING batch: owns all 4 of this wave's NAMED memory-safety findings
// (gang_customization_confirm_gang_sign/_select_gang_sign/_select_vehicle,
// vcust_purchase_component) plus 24 more crash-shaped findings shared with
// b24 (28 total across both files, the most of any wave-2 batch). See
// lua_spec_confirmed_stubs_b25_wave2_vcust_store_garage.cpp.
const std::vector<std::string>& namesB25();
void registerB25(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b17: wave 2 batch W2-6 (2026-10-08), "UI frontend (143 natives) -
// HARDENING BATCH" - split A of 2 (file b18 is the other half). 66
// names: spec-lua-api-behaviour.md Sec19/Sec23/Sec26 in full, plus every
// lobby/coop/session/steam `game_*` name from Sec41/Sec42/Sec44/Sec50/
// Sec51/Sec53/Sec54/Sec55/Sec56/Sec59 EXCEPT the 7 `game_machinima_*`
// names (which join file b18's machinima family instead), plus a
// handful of non-`game_*` "call-ranked residue" names that fit neither
// file's own named bucket. See
// lua_spec_confirmed_stubs_b17_wave2_ui_frontend_a.cpp's own top comment
// for the exact split rule and full per-name rationale.
const std::vector<std::string>& namesB17();
void registerB17(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b18: wave 2 batch W2-6 (2026-10-08) - split B of 2. 77 names: menus
// (main_menu_*/pause_menu_*/pause_map_*/options_display_pc_set_options/
// vint_options_remap_reset_bindings/object_indicator_init_lua/msn_
// killbane_selected_option/sb_*/autil_hud_mayhem_init/community_login_
// open), dialog_*, cellphone_*, cell_*, save_system_*/save_game_*, the
// WHOLE machinima family (game_machinima_*, cinema_editor_*, cell_
// machinima_is_recording - one owner, per this batch's own hardening
// directive), and completion_*/Completion_*. Owns this batch's 2 named
// memory-safety fixes (dialog_box_create_internal, cell_playlist_save_
// list). See lua_spec_confirmed_stubs_b18_wave2_ui_frontend_b.cpp's own
// top comment for the full rationale.
const std::vector<std::string>& namesB18();
void registerB18(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b19: wave 2, batch W2-8 (2026-10-08) - spec-lua-api-behaviour.md
// §38-60 "gameplay B," the "character/NPC/inventory/party" half (56 of
// its 93 names). See lua_spec_confirmed_stubs_b19_wave2_char_npc_inv.cpp's
// own top comment for the full list and citations.
const std::vector<std::string>& namesB19();
void registerB19(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b20: wave 2, batch W2-8 (2026-10-08) - spec-lua-api-behaviour.md
// §38-60 "gameplay B," the "HUD/minimap/mission-flow/hooks" half (37 of
// its 93 names), including the shared on_* hook-name pool (§42.3/§42.7/
// §47). See
// lua_spec_confirmed_stubs_b20_wave2_hud_minimap_hooks.cpp's own top
// comment for the full list and citations.
const std::vector<std::string>& namesB20();
void registerB20(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b21: wave 2, batch W2-2 of the 680-name lua-natives backlog (2026-10-08) -
// spec-lua-api-behaviour.md Sec17 (30 names, "ranks 251-350 tranche, part
// A") + Sec18 (30 names, "ranks 251-350 tranche, part C"), 60 names total -
// the highest real call volume in the entire backlog. See
// lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp.
const std::vector<std::string>& namesB21();
void registerB21(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b28: the 6-native clearance fast-follow onto wave 1 batch 1 (2026-10-08) -
// on_dismiss (Sec1.11), character_prevent_flinching (Sec3.14),
// character_allow_ragdoll (Sec3.16), npc_combat_enable (Sec3.17),
// inv_weapon_add_temporary (Sec4.10), inv_weapon_remove_temporary
// (Sec4.11) - the 6 of b08's original 21-name batch deferred at the time
// for still carrying an undated "NOT yet cleared for implementation"
// Review-status line; all 6 were independently re-derived and dated
// "cleared for implementation" later the same day. See
// lua_spec_confirmed_stubs_b28_batch1_deferred6.cpp's own top comment.
const std::vector<std::string>& namesB28();
void registerB28(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b29: Sec6 backlog close-out (2026-10-08) - the 10 of Sec6's 21 names the
// earlier Sec6 batch deliberately deferred (engine_state.h's own
// "deliberately NOT in this batch" note, since resolved): disable_
// specific_notoriety_group (Sec6.2), notoriety_set_min_and_max (Sec6.3),
// notoriety_set_max (Sec6.5), notoriety_set (Sec6.7), notoriety_force_
// no_spawn_group (Sec6.10), notoriety_set_min (Sec6.11), mission_end_
// success (Sec6.12), notoriety_reset (Sec6.16), mission_end_to_activity
// (Sec6.20), mission_set_cancel_warp_location (Sec6.21). See
// lua_spec_confirmed_stubs_b29_sec6_notoriety_mission.cpp's own top
// comment.
const std::vector<std::string>& namesB29();
void registerB29(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b30: cross-reference backlog close-out (2026-10-08) - character_kill
// (Sec3.15) and vehicle_is_destroyed (Sec4.9), both newly cleared by the
// 2026-10-08 tranche-F spec sync (commit 46f90d1), plus camera_restrict_
// set_active (Sec8.12), already CONFIRMED since 2026-09-30 but missed by
// every earlier batch. See lua_spec_confirmed_stubs_b30_tranchef_camera.cpp's
// own top comment.
const std::vector<std::string>& namesB30();
void registerB30(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

// b31: 3 natives cleared for implementation (2026-10-08) -
// continuous_spawn_start (Sec4.8), on_vehicle_enter (Sec4.12, re-resolved
// by commit e07a46a), tutorial_start (Sec6.19, 210-entry name table
// transcribed by commit 3816870). See
// lua_spec_confirmed_stubs_b31_continuous_spawn_vehicle_enter_tutorial.cpp's
// own top comment.
const std::vector<std::string>& namesB31();
void registerB31(lua_State* L, EngineState& state, HitLog& log, const std::string& stateTag,
                  const WantsPredicate& wants);

} // namespace stub_sections
} // namespace sr3luahost
