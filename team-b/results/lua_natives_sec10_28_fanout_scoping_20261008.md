# Lua native implementation fan-out scoping — full-document pass (2026-10-08)

**Resuming a killed run.** A prior instance of this task was killed mid-run by a session-wide rate limit before writing anything; nothing is recovered from it. This is a fresh pass. It independently re-verified the prior instance's own secondhand claims rather than trusting them, per the task's own instruction — and found its headline premise (that §29-60 is "the likely biggest source of remaining gaps") understated the real problem: **§11-26 and §38-60 together hold the overwhelming majority of the gap**, not §29-60 alone.

## 0. Method

1. Verified the registered-name count by reading each `namesB0N()` vector directly out of `src/lua_spec_confirmed_stubs_b01_initial.cpp` through `_b07_sec8_9_teamA.cpp` (not the registry's own prose claim) and counting every string literal by hand. **Confirmed: 36+50+34+58+49+45+30 = 302, exactly as claimed**, with zero duplicates across files (checked via `sort | uniq -d` on the full 302-name list).
2. Mapped all 60 `## N.` headers in `spec-lua-api-behaviour.md` (15,006 lines) to their line numbers, then fanned out 7 parallel read-only sub-agents across section-aligned, slightly-overlapping ranges covering the **entire document end to end** (not just §29-60), each handed the verified 302-name list to diff against directly.
3. Each sub-agent was told the project's own "cleared for implementation" bar used throughout the existing b01-b07 stubs: the native's own top-level argument count/types and return shape must be CONFIRMED or HIGH CONFIDENCE; a deep callee-level OPEN/HYPOTHESIS detail on *one specific internal step* does not disqualify it (the existing stubs routinely throw an explicit refusal for just that one sub-detail while implementing the rest).
4. **Consolidation cross-check (this pass, not delegated):** concatenated all 7 sub-agents' claimed "new" names (859 raw lines, zero internal duplicates) and ran it through `comm -12` against the verified 302-name file. This caught **4 genuine false positives** — names two different sub-agents (correctly, independently) read as "cleared" but which are *already* implemented and registered:

   | Name | Wrongly re-flagged in | Actually registered in |
   |---|---|---|
   | `audio_conversation_load_direct` | §5-9 pass (§8.1) | `b07` |
   | `vehicle_chase` | §5-9 pass (§9.4) | `b07` |
   | `sidewalk_disable_nodes` | §10-16 pass (§15.3) | `b06` |
   | `skydive_move_to_do` | §38-60 pass (§39, tranche 07) | `b04` |

   No action needed on these four — they're already done. This leaves **855 verified, deduplicated, cleared-but-unregistered natives** as the real gap.

## 1. The real gap count: **855**, not a §29-60-only problem

| Document range | Sections | Cleared-but-unregistered natives (verified) |
|---|---|---|
| §1-4 | Trigger/platform/character/vehicle core clusters | **21** |
| §5-9 | Mission/notoriety/combat-flag/UI-audio/vehicle clusters | **25** |
| §10-16 | Runtime-frequency tranche + ranks 151-350 (UI+gameplay) | **136** |
| §17-22 | Ranks 251-450 (gameplay+UI) | **163** |
| §23-26 | Ranks 451-550 (gameplay+UI) + bare math/engine-globals cluster | **122** |
| §27-37 | Ranks 551-650 (already 100% done) + ranking tranche 03 + misc investigations | **16** |
| §38-60 | Ranking tranches 06-25 + stub hit-list | **372** |
| **Total** | | **855** |

§1-9 (the range a prior pass called "largely already implemented") in fact still has **46 real gaps** (21+25) — the "largely implemented" read only holds for the *highest-call-count* entries within those sections, not the sections as a whole. §10-26 — covering call-count ranks 151-550, written up in the same per-function depth as §1-9 — is **almost entirely unimplemented** (421 of its ~445 documented natives, after subtracting the one small already-covered cluster in §26). §27-28 (ranks 551-650) is the one large tranche-style range that's genuinely 100% done. §29-60's ranking tranches are a real gap too (roughly 330-380 natives, depending on how the ~20 "named in aggregate but not individually spelled out" stragglers from §38-60 are counted — see §5 below) but are not disproportionately larger than §10-26; both are enormous.

## 2. Why 4-6 batches doesn't fit the whole 855 — and what I'm proposing instead

The existing `b01`-`b07` split sized one native-implementation file at 30-58 natives (avg. 43) specifically *because* a bigger single file became "this project's single most heavily multi-agent-contended file" (per that refactor's own commit). Forcing all 855 into literally 4-6 batches would mean **140-215 natives per batch** — 3-5x larger than any file this project has ever used for this purpose, recreating the exact problem the b01-b07 split just fixed.

Per this project's own no-invented-fixes/skip-and-revisit policy, I'm not forcing that fit. Instead:

- **This wave: 7 agents** (5 main batches sized like b01-b07, at 21-55 natives each, **plus 2 small batches explicitly flagged as needing a dedicated owner** for shared-infrastructure reasons rather than size — which is exactly the "flag anything unusually large/complex deserving its own dedicated agent" case the task asked for). Total covered this wave: **222 natives**.
- **Backlog, clearly catalogued, not dropped: 633 natives** (§17-22, §23-26 residue, §38-60), broken out by section/tranche in §6 below for whoever scopes the next wave(s).

This wave is ordered by the project's own stated historical priority — highest real call-count first — with one deliberate exception (batches 6-7) justified on a different, explicit axis.

## 3. This wave's 7 batches

### Batch 1 — Trigger/mission/teleport/combat-hook core (§1-4) — 21 natives — **highest priority: call counts 78-424, the highest in the entire backlog**
`objective_text`(424 calls), `teleport`(181), `ai_do_scripted_move`(154), `on_attack_performed`(130), `audio_conversation_play`(129), `release_to_world`(128), `vehicle_suppress_npc_exit`(116), `game_peg_unload`(122), `pop_screen`(106), `on_trigger_exit`(104), `spawn_region_enable`(104), `door_open`(101), `on_dismiss`(100), `set_player_can_enter_exit_vehicles`(109), `continuous_spawn_stop`(109), `npc_combat_enable`(95), `character_prevent_flinching`(92), `object_is_in_trigger`(86), `inv_weapon_add_temporary`(88), `character_allow_ragdoll`(82), `inv_weapon_remove_temporary`(78)

### Batch 2 — Character/vehicle/HUD/audio residue (§5-9, ranked 51-150) — 25 natives
`npc_leash_remove`, `npc_leash_to_nav`, `set_attack_player_flag`, `set_animation_state`, `set_script_animation_state`, `ai_set_action_delay`, `human_gravity_enable`, `audio_persona_load_2d`, `audio_play_persona_line_2d`, `audio_ambient_emitter_start`, `audio_ambient_emitter_stop`, `hud_bar_off`, `hud_bar_on`, `hud_bar_set_value`, `hud_prompt_clear`, `hud_set_fake_notoriety`, `hud_timer_set`, `hud_timer_stop`, `vint_set_mouse_cursor`, `push_screen`, `explosion_create`, `vehicle_max_speed`, `vehicle_detonate`, `helicopter_set_path_orientation`, `inv_item_equip`

### Batch 3 — PCR quick-queries + vehicle/HUD/effect cluster (§11+§12, ranks 151-250) — 40 natives
§11: `pcr_is_plastic_surgeon`, `pcr_mark_changed`, `pcr_apply_morph_value`, `pcr_get_morph_value`, `pcu_is_streaming_done`, `horde_mode_is_active`, `game_audio_stop`, `get_localized_crc_for_tag`, `store_common_mouse_zoom`, `store_set_camera_pos`, `pause_menu_accept_options`, `vint_options_remap_set_key_binding`
§12 (calls 19-28 each): `hud_critical_timer_stop`, `audio_play_for_mission_cellphone`, `inv_item_remove_all`, `character_set_skydiving_state`, `character_hide`, `vehicle_set_smoke_and_fire_state`, `set_always_sees_player_flag`, `inv_weapon_disable_all_but_this_slot`, `get_char_vehicle_name`, `effect_play_on_script_object`, `helicopter_fly_to_do`, `effect_play_on_human`, `character_set_skydiving`, `vehicle_get_driver`, `on_vehicle_exit`, `inv_weapon_disable_slot`, `guardian_angel_increment_node_progress`, `vehicle_show`, `radio_block`, `vehicle_set_invulnerable`, `vehicle_prevent_explosion_fling`, `camera_restrict_target_object`, `waypoint_add`, `get_current_hit_points`, `vehicle_freeze`, `set_player_can_enter_exit_rc_vehicles`, `player_parachute_wear_backpack`, `persona_override_persona_stop`

### Batch 4 — Flag-setters/hooks/combat cluster (§13+§14, ranks ~151-250) — 55 natives
§13: `object_indicator_add_objective_text`, `notoriety_spawn_group_set_delay`, `hud_prompt`, `customization_outfit_wear`, `character_set_no_explosive_panic`, `character_ragdoll_clear_last_resort_position`, `ai_scripted_action_is_processing`, `vehicle_ignore_repulsors`, `spawn_region_toggle_filter`, `refraction_situation_set`, `npc_revive`, `hud_timer_hide`, `hud_critical_timer_set`, `character_has_specific_human_shield`, `camera_shake_play`, `camera_end_look_through`, `action_nodes_enable`, `set_team`, `set_min_hit_points`, `set_max_hit_points`, `inv_item_in_slot`, `hud_achievement_show_progress`, `helicopter_set_max_bank_angle`, `crib_disable_interface`, `character_ragdoll_set_last_valid_position`, `character_collision_enable`, `ai_set_personality`, `vehicle_suppress_flipping`
§14 (calls 16-19 each): `action_play_synced_state`, `vehicle_disable_chase`, `set_players_can_be_downed`, `set_player_can_take_human_shields`, `player_human_shield_lock`, `on_mover_enter_water`, `on_ai_do_action_complete`, `mission_set_excluded_reward`, `mesh_mover_kill`, `helicopter_shoot_navpoint`, `customization_item_wear`, `customization_item_revert`, `vehicle_set_keyframed_physics`, `vehicle_hide`, `persona_override_persona_start`, `helicopter_shoot_human`, `drug_effect_set_override_values`, `debris_flow_set_active`, `camera_restrict_set_limits`, `audio_conversation_playing`, `vehicle_speed_cancel`, `vehicle_disable_flee`, `set_cant_flee_flag`, `set_behavior_set`, `roadblocks_enable`, `player_script_controlled_clear`, `on_vehicle_enter_water`

### Batch 5 — HUD-slot/customization/pause-menu cluster (§15+§16, ranks 251-350) — 41 natives
§15 (calls 10-12 each): `character_set_damage_multiplier`, `vehicle_anim_start`, `set_always_cower_flag`, `refraction_situation_clear`, `qte_cleanup`, `persona_clear_global_situation_override`, `on_weapon_pickup`, `mission_unlock`, `mission_set_completed`, `message`, `hud_display_remove_state`, `follower_remain_in_car`, `character_is_ragdolled`, `vehicle_skydiving_start`, `vehicle_is_tank`, `skydive_set_player_dive_fall_speed`, `set_unenterable_for_player`, `set_player_undercover_team`, `player_actions_enable`, `npc_leash_to_object`, `hud_display_create_state`, `hud_display_commit_state`, `game_choice_set_state`, `dlc_achievement_counter_set`, `character_parachute_detach`
§16: `game_get_language`, `game_award_respect`, `tutorial_set_hud_help_closing`, `completion_load_lut`, `completion_cutscene_was_active`, `pcr_change_hair`, `pcr_change_composite`, `pcr_get_player_preset`, `pcr_set_identity`, `dialog_box_disconnect`, `main_menu_supress_profile_change`, `pause_menu_control_scheme_init`, `pause_menu_update_option`, `pause_menu_restore_defaults`, `store_common_bg_anim_complete`, `store_weapon_hide_weapon`

### Batch 6 — *dedicated agent, not call-volume-driven* — bare math/engine-globals/RNG/thread-scheduler (§26.27) — 24 natives
`abs`, `acos`, `cos`, `sin`, `ceil`, `debug_print`, `assert_msg`, `floor`, `get_frame_time`, `include`, `max`, `min`, `rand_float`, `rand_int`, `round`, `sizeof_table`, `sqrt`, `strstr`, `thread_check_done`, `thread_kill`, `thread_new`, `thread_yield`, `closest_point_on_line_segment`, `which_side_of_2d_line`

**Why its own agent, not folded into a size-matched batch:** these are the Lua base-library primitives every script in the game calls constantly. They share one 8192-entry RNG ring/generator object (`rand_int`/`rand_float`), a non-standard binary `max`/`min` (second-argument-wins on ties/NaN), single-precision-narrowing-everywhere semantics, and — critically — a thread/coroutine scheduler (`thread_new`/`thread_kill`/`thread_check_done`/`thread_yield`) tied into the engine's per-frame 11-slot game-state dispatch table and a pause-exempt list. Building this as 24 independent stubs would almost certainly produce 24 inconsistent re-derivations of the same RNG/scheduler state; one agent needs to own the shared data structures first, then wire the 24 thin natives on top.

### Batch 7 — *dedicated agent, not call-volume-driven* — ranking tranche 03 AI/vehicle cluster (§30) — 16 natives
`character_dont_regenerate`, `character_clear_combat_move`, `boss_battle_matt_set_player_as_avatar`, `boss_battle_matt_hide_wings`, `audio_ambient_emitter_pause`, `audio_ambient_emitter_resume`, `ai_suggest_action`, `ai_do_scripted_advance`, `vehicle_set_2d_tank_controls`, `vehicle_rc_get_signal_strength`, `vehicle_stop_do`, `vehicle_pathfind_to_do`, `vehicle_set_malfunctioning`, `vehicle_component_detach`, `turn_to_do`, `teleport_vehicle_to_object`

**Why its own agent:** this tranche is *already half-implemented* — `b03` already wired the other 9 of its 25 names (`vehicle_set_invulnerable_to_player_explosives`, `vehicle_disable_explosion_and_damage_vfx`, `vehicle_set_special_override_never_ghost`, `vehicle_clear_all_radio_locks`, `vehicle_is_vtol`, `auto_pickup_enable`, `vehicle_exit_group_do`, `vehicle_exit_group_check_done`, `team_make_unfriendly`), all sharing the same vehicle force-flag/resolver infra documented in §30's own text. The remaining 16 need the 70-entry CAE combat-action table (`0x012e1c60`) plus a per-character staged/committed action record — new shared engine-state modeling, best done by one owner who can also sanity-check against the 9 already-done siblings rather than re-deriving the shared vehicle-resolver chain a second time.

**This wave's total: 222 natives across 7 agents** (21+25+40+55+41+24+16).

## 4. Cross-batch dependency notes (same-wave coordination, not blockers)

- **Helicopter family spans Batch 3 and Batch 4:** `helicopter_fly_to_do`/`helicopter_set_max_bank_angle` (Batch 3, §12/§13) share a dispatcher with `helicopter_shoot_navpoint`/`helicopter_shoot_human` (Batch 4, §14). Not worth merging batches over, but Batch 3's and Batch 4's owners should compare notes before finalizing the shared helicopter-AI call shape.
- **Customization family spans Batch 4 and Batch 5:** `customization_outfit_wear` (Batch 4, §13), `customization_item_wear`/`customization_item_revert` (Batch 4, §14), `pcr_change_hair`/`pcr_change_composite` (Batch 5, §16) all touch the same dual local-player customization state and 63-slot item list. The *backlog* also holds a much larger PCU/PCR family (see §6) — whoever picks that up later should look at how Batch 4/5 modeled this shared state first.
- **HUD bar/timer cluster is fully co-located in Batch 2** (`hud_bar_off/on/set_value`, `hud_timer_set/stop`) — intentional, build the shared per-slot infra once.
- **HUD display-state slot family is fully co-located in Batch 5** (`hud_display_create_state/remove_state/commit_state`, `qte_cleanup`) — same reasoning.
- **Player human-shield/downed-state flags are fully co-located in Batch 4** (`set_players_can_be_downed`, `set_player_can_take_human_shields`, `player_human_shield_lock`, `player_script_controlled_clear`) — all one status-byte family (`+0x28ac`/`+0x28ad`); implement together to avoid inconsistent bit-layout stubs.

## 5. Flagged items (not-ready, excluded, or needing special care)

**Correctly excluded, not cleared (verified from the spec's own text, not assumed):**
- `camera_restrict_set_active` (§8.12) — the doc's own review-status line says "NOT yet cleared for implementation."
- `character_kill`, `continuous_spawn_start`, `vehicle_is_destroyed`, `on_vehicle_enter` (§4) — each has an internally disputed or self-contradicting top-level reading (e.g. `vehicle_is_destroyed`'s own push-polarity sentence contradicts itself) that was never resolved by a later pass in-range.
- §5.1-5.3 (emulator-derived CRC/hash verification vectors) — not Lua natives at all.
- `teleport_coop` (§35) — a plain Lua-side function in `game_lib.lua`, not an engine-registered native; not a C++ stub candidate.

**Two premises in the original task brief did not hold up under direct verification — recording the correction rather than silently following the wrong premise:**
- **§33 ("ranking tranche 05") is fully registered, 25/25** — independently confirmed both from the spec text (all 25 sub-entries present) and by grep-checking `src/lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp` directly (all 25 have real `stub_*` bodies and are registered). The brief's claim of "only 17/25 actually registered" does not apply to §33. It almost certainly described **§30 (tranche 03)** instead, which really is split 9/25 registered vs. 16/25 not — that 16 is exactly Batch 7 above.
- **§60's 27 generic-stub names are a fully distinct set from the "real-hit" tranche-15/17/20/22/23 names** the brief asked about — those real-hit names (`tutorial_lock`, `radio_newsbreak_clear`, `group_create_do`, `group_create_hidden_do`, `dlc2_m02_clapboards_get/_reset`, `cutscene_play_do/_check_done`) live in §52/53/55/57, not §60. §60 was independently verified to split as 20/27 already registered, 7/27 not (`customization_create_character`, `customization_creation_is_open`, `customization_screen_is_ready`, `player_parachute_has_backpack`, plus 3 whose full contracts live in earlier sections outside that range) — those 7 are folded into the §38-60 backlog total below, not separately batched this wave given their small count and cross-reference dependencies.

**Genuine memory-safety bugs found in the (backlog) binary that a clean-room reimplementation will need to decide how to handle — reproduce faithfully or harden — before anyone implements them:**
- `dialog_box_create_internal` (§57) — unbounded 64-byte callback-name copy, heap-adjacent corruption.
- `gang_customization_select_vehicle` (§53), `gang_customization_select_gang_sign`/`_confirm_gang_sign` (§56) — unchecked small-array indices → arbitrary-address write, one of them save-persisting.
- `vcust_purchase_component` (§52) — OOB read-and-write from a stale sentinel index.
- `cell_playlist_save_list` (§57) — two independent corruption shapes (unchecked index; negative-count stack overflow).
- `dlc2_m02_clapboards_set` (§55) — same signed-index arbitrary-R/W bug class as its two already-registered siblings `_get`/`_reset`; should eventually be implemented alongside them as one hardened unit.

**Large shared-infrastructure families spanning multiple backlog tranches (flag for whoever scopes the next wave — each wants ONE owner, not split across several agents):**
- **PCU/PCR character-customization surface** — roughly 35 functions spread across §19, §23, §26, §40, §43 (backlog) plus `pcr_change_hair`/`pcr_change_composite`/`pcr_get_player_preset`/`pcr_set_identity` already in this wave's Batch 5. Tightly coupled shared data (owned-items/outfits arrays, several different "no selection" sentinels, a 63-slot composite-item list, a 34-entry category table). Recommend one dedicated agent for the whole surface once the backlog is picked up.
- **vcust_* vehicle-customization family** — 13 functions across §38 (backlog) and §52 (backlog); shares one "requires a live customization vehicle" precondition that the original binary itself only checks in 2 of the 13 call sites.
- **Machinima recording family** — 7 functions across §44 and §56 (backlog); shares a file-path-builder defect (uninitialized buffer on failure).

**Near-duplicate names worth double-checking at implementation time (distinct functions, not duplicates, but easy to transpose):** `objective_text` (new) vs. registered `objective_text_clear`; `door_open` (new) vs. registered `door_close`/`door_lock`; `teleport` (new) vs. registered `teleport_check_done`/`teleport_vehicle`; `vehicle_hide` (Batch 4, new) vs. registered `vehicle_show`; `mesh_mover_kill` (Batch 4, new) vs. registered `mesh_mover_set_invulnerable`/`_hide`/`_show`/`_reset`; `character_ragdoll_clear_last_resort_position` and `character_ragdoll_set_last_valid_position` (both Batch 4, new) vs. registered `character_ragdoll_set_last_resort_position`.

**One FYI, no action needed:** `audio_stop` and the already-registered `audio_stop_emitting_id` are confirmed (§8.9) to be two different Lua names bound to the literal same C function pointer — a true alias. Worth knowing if `audio_stop` is ever picked up: it should be wired to the identical existing implementation, not re-derived.

## 6. Backlog (633 natives, deferred this wave — catalogued, not dropped)

| Range | Sections | Natives | Notes for next wave |
|---|---|---|---|
| Ranks 251-450 | §17-22 | **163** | Contains the project's most complex single functions found so far: `character_damage` (central damage engine, ~5,200 bytes), `move_to_do` (core movement dispatcher), `vehicle_exit_do`, `qte_start`, `teleport_to_object`, `vehicle_pathfind_navmesh_do` — each flagged by its own sub-agent as wanting a dedicated owner, not a bundled batch slot. |
| Ranks 451-550 residue | §23-25 + §26 minus its math cluster (already Batch 6) | **98** | Mostly straightforward single-subsystem setters/queries; `character_set_counter_on_grabbed_by_player` sits right next to the already-registered `character_set_counter_on_grabbed` — watch for bit-layout transposition. |
| Ranking tranches 06-25 + stub hit-list | §38-60 | **372** | The largest remaining slice. Already has several severe memory-safety bugs catalogued above (dialog_box_create_internal, gang_customization_*, vcust_purchase_component, cell_playlist_save_list, dlc2_m02_clapboards_set) that deserve a hardening-focused pass rather than a literal port. ~20 additional functions in this range are named only in aggregate by the spec (exact Lua binding string not reproduced in `spec-lua-api-behaviour.md` itself) — pull those from the matching `review/exe-notes-2026-10-01/interp_ranking_tranche_NN.md` files before finalizing a future batch count here. |

## 7. Prioritization recommendation

Dispatch in this order, matching this project's historical practice of taking high-call-volume natives first:

1. **Batch 1** (§1-4) — call counts 78-424, by far the single highest-value batch in the whole backlog.
2. **Batch 2** (§5-9, ranks 51-150).
3. **Batch 3** (§11-12, ranks 151-250, calls 19-28).
4. **Batch 4** (§13-14, ranks ~151-250, calls 16-19).
5. **Batch 5** (§15-16, ranks 251-350, calls 8-13).
6. **Batches 6 and 7** last among this wave — not because they're low-value, but because they're foundational/shared-infra builds (base-library RNG+scheduler; CAE action table) rather than ranked individually by call count; dispatching them after 1-5 means their respective shared data structures can be sanity-checked against a slightly larger body of already-implemented sibling code.
7. **Backlog** (§17-22, §23-26 residue, §38-60) — scope as a follow-up wave; §17-22 should go first within it (still has real per-function call counts given, 8-13 each, vs. the mostly-low-traffic §38-60 tranches), and the memory-safety-flagged functions within it should get a hardening-focused implementer rather than a literal-port one.

## 8. Files

- This report: `D:\Project Crreish\TEAM B\.claude\worktrees\agent-a19466b37c3ff40a7\results\lua_natives_sec10_28_fanout_scoping_20261008.md`
- Spec source read in full: `D:\Project Crreish\TEAM B\.claude\worktrees\agent-a19466b37c3ff40a7\spec-lua-api-behaviour.md`
- Registered-name source of truth (re-derived from source, not trusted from prose): `src/lua_spec_confirmed_stubs_b01_initial.cpp` through `src/lua_spec_confirmed_stubs_b07_sec8_9_teamA.cpp`
