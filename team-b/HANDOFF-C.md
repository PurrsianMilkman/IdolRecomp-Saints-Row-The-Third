# Team C handoff

Team C is the clean-side team for free-roam systems, built as self-contained libraries with
their own tests. Lane: (1) HUD and minimap, (2) save/load integration, (3) respect, level-up and
progression. Do not edit `tools/sr3_viewer.cpp` or `include/sr3luahost/engine_state.h` until Team
B's viewer split has landed on main.

Workflow: one feature branch per system off main; build, then full ctest; when green, append
`<branch> - <summary>` to `D:\IdolRecomp Saints Row The Third\TEAM B\.claude\merge-queue.txt`.
Team B merges. Build dir: `build/` (VS 2022 x64, Release), e.g.
`cmake --build build --config Release --parallel 4` then
`ctest --test-dir build -C Release -j 4`.

## 1. HUD and minimap — `feature/hud-minimap` (library `sr3hud`, test `synthetic_hud_test`)

Source: `spec-hud-runtime.md` H.1-H.4 (the 2026-10-14 "Free-roam HUD binding map and minimap
transform" section) and `spec-world-streaming.md` 10.6(g) for the tile lattice.

| File | What |
|---|---|
| `include/sr3hud/dataitem.h`, `src/hud_dataitem.cpp` | Typed dataitems with change-detected field writes and a dirty mark (H.1); the 7 core items plus the 4-item `sr2_notoriety` group, with the H.1 names and field-type lists |
| `include/sr3hud/producers.h`, `src/hud_producers.cpp` | Producers and encodings (H.2): `health_pct = trunc(1000·HP/max)/1000`, cash `cents/100`, notoriety `L + fraction/T[L+5·police]` with the fake override, respect `pct − floor(pct)` with the 22000 cap, the force-show counter, the radio-station push on record change, dual-wield ammo halving |
| `include/sr3hud/display_model.h`, `src/hud_display_model.cpp` | The HUD scripts' documented behaviour: health ring angles / vignette / retract, cash count-up and `format_cash`, notoriety icon add/remove/pulse/decay and ring expand/contract, respect meter angles and "+N", ammo / grenade / weapon-icon text, map-group alpha |
| `include/sr3hud/minimap.h`, `src/hud_minimap.cpp` | H.3: speed-zoom smoother, `Z`/`k`, rotation `θ = −(h+π)` and static mode, world→screen (cartesian and polar), blip clamp at R67 and edge factor, north marker, category-pass gates, city tile lattice, base texture selection with water/world fallbacks, UVs, base quad (exterior and the shipped interior origin, with an opt-in corrected variant) |
| `tests/synthetic_hud_test.cpp` | 369 checks; every H.4(a) and H.4(b) vector |

Wiring for Team B after the split (H.5): publish the dataitems from engine state each frame with
the producers, and either run the real `hud.lua` against them or draw with the display model.
The minimap draw replaces the viewer's box minimap: `MinimapCentre` → `MinimapSpeedZoom` →
`MinimapZoom`/`MinimapScale` → `ComputeMinimapRotation` → `SelectMinimapBaseTexture` +
`MinimapBaseQuad` for the tile, `PlaceMinimapBlip` per blip, `MinimapNorthMarker`, and the
`map_blip` glyph at `kMinimapCentreRef` rotated by `glyphRotation`. The older
`include/sr3luahost/minimap_live_transform.h` predates H.3 (wrong rotation source, no scale)
and is superseded by this library.

### CHOSEN where the spec is silent (revisit on a spec sync)

- Weapons dataitem: no type codes in the spec. Bitmap names String, counts Int, flags Bool;
  fields 5 and 12-24 are `field_N` Int placeholders (their names live in a `hud.lua` comment
  this tree does not carry). Frequent-weapon and hood field types also CHOSEN.
- A missing weapon is published as `""`; the display model shows `ui_hud_inv_fist` for it
  (the spec says Lua shows the fist for nil).
- Health level outside 0-4 is clamped for the end-angle table.
- Vignette at pct ≥ 0.5: the spec does not say what the script does; `vignetteAlpha` is empty.
- Ammo text when both continuous-fire and infinite flags are set: continuous fire wins.
- `FormatCash` of a negative value puts `-` in front.
- Speed zoom: `floor(acc)` is drained every frame, including frames where `s == T`.
- Notoriety threshold ≤ 0 returns the integer level (division guard).
- Tile boxes are half-open (lower edges belong to the tile).

### Spec-sync questions for Team A (via orchestrator)

1. `sr2_local_player_weapons`: names and type codes of fields 5 and 12-24, and type codes of
   fields 0-11; type codes of `sr2_local_player_frequent_weapon` and `sr2_local_player_hood`.
2. `hud_player_status_change`: what happens to `vignette_health` at pct ≥ 0.5 (fade to 0?).
3. Ammo-text precedence of `cont_fire` versus `inf_ammo`.
4. The world box (`mapsaintsrow.tga` bounds) values, for the no-zone fallback; currently
   caller-supplied.

## 2. Save/load integration — `feature/save-integration` (library `sr3saveflow`, test `synthetic_saveflow_test`)

Source: `spec-save-format.md` "Free-roam load and save flow (2026-10-14)" A.1-A.4, plus 6.1
(directory write), 10.5 (cribs), 9.3(c) (mission objects), 15.1/15.2 (menu save, autosave);
clock layout from `spec-lua-api-behaviour.md` 48.1. Builds on Team B's `sr3save` (reader,
`SaveSnapshot::build`, CRC, slot summary) without changing it. This branch also carries
`feature/hud-minimap` (merged in so `HANDOFF-C.md` does not conflict).

| File | What |
|---|---|
| `include/sr3saveflow/snapshot_blocks.h`, `src/saveflow_blocks.cpp` | Blocks `sr3save` does not decode: 20-byte game clock (TOD fixed point, play float), 256-bit flag set, respawn-crib handle `0x058`, respect multiplier `0x4A5C`, crib block (entry 9) with the ten known cribs (A.3 handles, names, positions), mission-object table; read and patch-in-place writers, cash clamp, hash stamp |
| `include/sr3saveflow/freeroam_flow.h`, `src/saveflow_load.cpp` | `PlanFreeRoamLoad`: steps in game order, version gate, clock verbatim, play time from `0x0A0` (full load only), placement (`"tss01"` override → respawn crib → player-start marker; host/no-session and full load only), cash clamp, respect verbatim, multiplier, cribs, region bits, post-start stream centre |
| `src/saveflow_save.cpp` | `NearestOwnedCrib`, `PatchFreeRoamSave` (writes over the loaded image so unmodelled spans survive), `PauseMenuSave` / `SlotScreenSave` / `AutosaveAllowed`, `BuildNewGameSnapshot`, `MakeSlotRecord`, `BuildSaveDirectory` |
| `tests/synthetic_saveflow_test.cpp` | 272 checks; the A.4 vector byte for byte |

Wiring for Team B: on load, run `PlanFreeRoamLoad` with a `LoadWorld` that resolves handles
against the loaded zone's crib objects (zone record handle, `spec-save-format.md` A.3) and
returns the player-start marker; apply the plan's fields in `steps` order. On save, keep the
loaded image and call `PatchFreeRoamSave` with live state, then `MakeSlotRecord` +
`BuildSaveDirectory`. The existing `include/sr3luahost/save_game_bridge.h` builds from a
zero-filled image and so drops every unmodelled block, including the `0x4A5C` multiplier (the
A.4 pitfall); patching over the loaded image avoids that.

CHOSEN: "show the warning once" is modelled as caller-held `...WarningShown` flags; the
moon counter (+8, width not stated) is kept as 4 raw bytes; the new-game clock keeps the
caller's date fields; new-game cash defaults to the spec's recommended $57.00
(`startingCashRaw = 0` gives the engine-like HYPOTHESIS).

## 3. Respect, level-up and progression — `feature/respect-progression` (library `sr3progression`, test `synthetic_progression_test`)

Source: `spec-tables-progression.md` section 5 (`respect_levels.xtbl`) and "Free-roam respect
grants and level-up flow (2026-10-14)" B.1-B.3. Links `sr3tables_progression` for the parsed
table. This branch carries `feature/save-integration` (and so `feature/hud-minimap`).

| File | What |
|---|---|
| `include/sr3progression/respect.h` | `RespectTable` (50-record limit, `cost(L)` = record L or 22000 past 50, `cumulative`, cap = sum minus last row, `FromParsed`, `ShippedCosts` from the B.2 formula); `AwardRespect` (horde/remote guards, overflow to dollars at the cap or at L >= 49, clamp to cap, level-up loop, hint 0x44 with L+1); `ApplyLevelUp` / `ApplyLevelUnlocks` (record L's unlockables, hint 0xA9, decade milestone in sr3_city); multiplier helpers; activity two-call award; diversion rule; reward-toast finalisation; kill respect by rank; store-purchase respect; shipped amounts |
| `src/progression_respect.cpp`, `src/progression_grants.cpp` | Implementation |
| `tests/synthetic_progression_test.cpp` | 177 checks: the B.3 totals table, sequences A and B, the B.2 data-check saves, A.4's 10,000/850/4 |

Wiring for Team B: after the split, replace `engine_state.h` `awardRespect` /
`applyRespectLevelUp` with `AwardRespect` over `RespectTable::FromParsed` of the real table; add
`overflowCashRaw` to the wallet; apply `levelUps[].newlyApplied` to the unlockables; post the
hints. Callers scale with `ScaleByMultiplier` where B.1.1 lists them (not kills). The HUD respect
producer (`sr3hud::RespectPct`) takes `progress = inLevel`, `need = cost(L)`.

CHOSEN: diversion "fraction ~0" uses a 1e-6 tolerance; the per-kill factor product is rounded;
a record below 50 but past the loaded count costs 0 (the loop's `c > 1` test then stops it).
Spec-sync question 5: how the per-kill float product becomes an int amount.

## 4. Refactor: split large non-viewer files — `refactor/teamc-split-large-files`

Orchestrator task 2026-10-17: split non-viewer sources over ~1,500 lines, no behaviour change, never
touching `tools/sr3_viewer/*`, `engine_state.h` or the prototype tools.

| Before | After |
|---|---|
| 8 stub batches `src/lua_spec_confirmed_stubs_{b04,b11,b15,b16,b17,b18,b21,b24}_*.cpp` (1,554-1,858) | each + `_part2.cpp` (all ≤ 1,197): second half of the stub functions under `sr3luahost::stubs_bNN`, reached from the original through declarations + using-declarations; registration untouched |
| `src/player_vehicle_occupancy.cpp` (1,805) | + `player_vehicle_occupancy_routes.cpp` (884 + 941) |
| `tools/golden_scene_check.cpp` (2,624) | + `golden_scene_check.h`, `_util.cpp`, `_scenes_vehicle.cpp`, `_scenes_world.cpp` (≤ 946); namespace `golden_scene_check`; CMake adds the sources in the validation `foreach` |
| `tests/synthetic_clmesh_test.cpp` (1,682) | `tests/synthetic_clmesh/` (common.h/.cpp, two test files, main.cpp); exe output identical before/after |
| `src/lua_engine_state.cpp` (2,515) | + `lua_engine_state_vint.cpp`, `lua_engine_state_misc.cpp` (824 / 798 / 951) |
| `include/sr3luahost/character_state.h` (2,911) | `CharacterState` derives from three data-only bases in original member order, one header each, plus `character_state_records.h` (≤ 1,072) |

Every split was checked line-for-line (each removed line present verbatim in the new files; only
namespace lines, declarations and using-declarations added). Scripts: `.claude_scratch/` (untracked).

SMALL CALLS (noted per the orchestrator's rule):
- `tools/lua_host_run.cpp` not touched: Team B's refactor step 3b is splitting it in parallel
  (`.claude/brief_lua_host_run_split_step3.md`); a second split would only conflict.
  `tests/synthetic_luahost_test.cpp` was already split on main (c021f11).
- `src/wwise_codebook_library_data.cpp` (3,635) left whole: generated data (a byte array written
  by its generator, optional at configure time with a stub fallback); splitting it would fight the
  generator and the `EXISTS` guard.
- `tests/synthetic_ai_runtime_test.cpp` (1,563) and `src/tables_progression.cpp` (1,529) left as
  they are: within "about 1,500".

## 5. Conformance review 854ae94 follow-up — `fix/teamc-review-854ae94`

Team A's review of the three Team C merges: CORRECT 21, DEVIATION 0 for Team C code, INVENTED 0.
Acted on:
- HM-7: `sr3hud::MinimapZoomSourceSpeed` (current vehicle, else last vehicle, else 0) and
  `WeaponHudInput::clipForcedZero` (H.2's unnamed single-round flag case; caller sets it).
- RP-7: `sr3progression::ApplyRespectBonusUnlockable` (type 37 adds its Modifier) and
  `ApplyPayCashForRespectUnlockable` (type 32: Cash_Cost returned, round(m x Respect_Received)
  awarded).
- SV-6: the new-game crib block is now labelled CHOSEN (data-backed, 10.5's new-game save); the
  checkpoint load kind cites 11.2/15.3/A.1.
Not mine: RP-6 (replace `engine_state.h` `awardRespect`/`applyRespectLevelUp` with
`sr3progression::AwardRespect` over the real table) is engine-state wiring, Team B's file.

## 7. Cutscene playback — `feature/cutscene-playback` (library `sr3cutsceneplay`, test `synthetic_cutsceneplay_test`)

Source: `spec-cutscene-camera-format.md` "In-game cutscene playback, end to end (2026-10-14)"
A-H, plus `spec-xtbl-format.md` 6.3/6.4 for element names. Links `sr3tables_cutscene` (kind);
the camera itself stays `sr3cutscene::CameraScript::sample(t)` (section 10), sampled at
`CutscenePlayer::time()`.

| File | What |
|---|---|
| `include/sr3cutsceneplay/scene.h`, `src/cutsceneplay_scene.cpp` | Scene model (shots, actor rows + clips, audio lines, options, characters); shot spans and duration `sum(Defined+1)/30`; FNV-1 Wwise ids; `ParseSoundtrack` (switch ids, `lower(value).bnk_pc`); Bink path `videos\<base>`; `_f` package; clip name + `f_` fallback; `#PLAYER#` -> `#PLAYER1#`; Zscene `_cs` names; Angel/Killbane mesh rules |
| `include/sr3cutsceneplay/playback.h`, `src/cutsceneplay_player.cpp` | `CutscenePlayer`: load (bank, movie opened paused, scene options), start (soundtrack switches + `Cinematic_CTE`, camera mode 7), one clock, shot begins, actor setup (HideChar), clip start/release, lipsync 0.1 s early, subtitles, options (Fullscreen Bink Play / Bink Play / Bink Stop / Show Completion Screen, pre-fire one shot ahead), pause/resume, skip gate (playing, flags, > 2 s, completion > 0.6 s left), plain and tail-hold skip paths (01_in, 17_out cases), end fades (Zscene 125 ms at 0.125 s left; others 0 ms at 0.8 s), teardown, NextScene chain or fade-in 500/125 |
| `include/sr3cutsceneplay/scene_reader.h`, `src/cutsceneplay_reader.cpp` | `.cte_xtbl` -> model for the stated element names only |
| `tests/synthetic_cutsceneplay_test.cpp` | 167 checks: F.1 05_in (shot times, 26.5 s, clip names incl. the female fallback, subtitle times, audio ids), F.2 22_choice (37.6333 s, `videos\22_choice.bik`, full-screen push at shot begin, M22_CTE id), skip/end/chain/pause rules, reader |

CHOSEN where the spec is silent: a clip is released on the first frame past its EndFrame (and
at shot change); pre-fire options of shot 0 fire at playback start; the tail-skip fade-out has no
stated duration (event `FadeOut` with `flag = true`); the tail-skip `SoundtrackStop` (flag) means
"post Stop_Cinematics", the bank unloads at teardown; `01_in`'s first-shot skip goes to state 12
and re-stamps the skip timer only (what state 12 then does is not stated).

Spec-sync question 6 (Team A): the `.cte_xtbl` element names of (a) the per-shot actor rows (the
container under `Shot`, and the row's name element) and their `Animations` parent, (b) the name
element of a `Characters/Character` row, (c) an `Audio` row's lipsync-clip element, (d) an
`Options/Option` row's name element; and which options carry the pre-fire byte. Until then
`ReadScene` leaves those parts empty and the host fills them.

Wiring (after the viewer split step 3, same rules as section 6): replace the playtest's cutscene
auto-skip with `CutscenePlayer` (H step 10), executing its events through the existing anim,
audio and Bink paths.

## 8. Activities — `feature/activity-fraud` (library `sr3activity`, test `synthetic_activity_test`)

Source: `spec-activity-runtime.md` 6.1 (shared frame, reward grant) and 6.2 (Insurance Fraud), with
`spec-tables-diversions.md` 10 (`fraud_globals.xtbl`, parsed by Team B's `sr3tables_diversions`).

| File | What |
|---|---|
| `include/sr3activity/activity.h`, `src/activity_instance.cpp` | `ReadActivityInstance` (Activity row, up to 6 Levels; awards only inside Level), `TypeFromInstanceName`, `GrantActivityRewards` (done bit per level, Reward/Reward2, Props then round(Props x (m-1)) respect, Cash_Award, hints 0x5b/0x5c(+0x13/0x33/type hint), completion counter; replay pays nothing) |
| `include/sr3activity/fraud.h`, `src/activity_fraud.cpp` | `FraudRun`: ragdoll-episode hits, crush/scrape impacts with the mass multiplier, body multipliers, money gate, scoring-area/spot/adrenaline gate, struck vehicles (<= 6, luxury), the *_Element category grid, hit value, banking with spot consumption, adrenaline meter, 3 s cooldown, on-fire bonus, win (1 s timer) / timer fail deferred by ragdoll or adrenaline, death; `FraudParamsFromTables` (+ the Mass_*/Crush/Scrape/Adrenaline scalars 6.2 names) |
| `tests/synthetic_activity_test.cpp` | 83 checks |
| `tools/sr3_viewer/viewer_playtest_activity.cpp` | `--activity _a_if_<hood>_<nn>`: real instance xtbl from `<inst>_modal.str2_pc`, real fraud_globals, HUD line, tick, death fail, reward grant into the HUD EngineState; `--fraud-test-hit-every S` test aid |

CHOSEN: adrenaline-grid gain clamped at the grid ends; the 3 s cooldown blocks arming a new hit;
mass-multiplier speed test on |component|; adrenaline mode has no specified end (`endAdrenalineMode`
exists, nothing calls it); viewer: run starts where the player stands, scoring area counted as
reached (the hood latch needs the instance hood's volume), no player ragdoll/contacts in the viewer
yet so real hits never happen (test aid only). The viewer success/reward branch was not reached
live (the cooldown caps synthetic hits at $468 per 3 s against $80,000); it is unit-tested.
OPEN (spec): the per-hit cap +0x5c writer; bonus slots +0x70/+0x74/+0x88; what 0x005e97a0 gates on;
the "all instances done" test before activity_types Unlockables; bonus-spot placement.
Snatch (6.4, `feature/activity-snatch`): `include/sr3activity/snatch.h` / `src/activity_snatch.cpp`
(`SnatchRun`: Ho_Win_Amount, Duration/Coop_Duration, drop-off 6/8, In_Range_Pickup_Dist 60, win
before timer, death; notoriety bump clamp). The shipped rows keep the per-type fields in
`Level/Snatch_Info` (checked on the real `_a_sn_ne_01.xtbl`), so the instance reader also looks
one element below Level. Viewer `--activity _a_sn_*` + `--snatch-test-deliver-every S` (test aid):
smoke deliver 4 in 320 s -> SUCCEEDED, unlock Activity_Unlock_SN, +$2,000, respect 200.
OPEN: the recapture complication's mechanism (only its parameters are specified).
Mayhem / Tank Mayhem (6.6/6.7, `feature/activity-mayhem`): `include/sr3activity/mayhem.h` /
`src/activity_mayhem.cpp` (`MayhemRun`: combo count/multiplier with the class multipliers and the
Combo_Max_Multiplier cap, immediate credit, combo timer start/extend/end with the tweak times,
Combo_Decay_Cutoffs as absolute speeds, Completion_Mult, scoring gates, HVT Value, win wrap-up
waiting for the combo to close and the HVT groups, fails: timer / Tank leash / tank destroyed /
death; `ReadMayhemParams` from the real layout `Level/{Destruction_Goal, Duration, Combo_Info/...,
Activity_Center{Center,Radius}}`; Tank rows carry no Combo_Max_Multiplier = uncapped). Literal
reading kept: when the goal-meeting event also crosses a cutoff, the speed gets Completion_Mult
there AND again at the win (6.6 states both steps). Viewer `--activity _a_mh_*|_a_tm_*` +
`--mayhem-test-kill-every S` (test aid); the Activity_Center navpoint is not resolved (no leash).
OPEN: the vehicle record's value field (xtbl name not matched by the spec); 0x006641c0's HVT test.
Next: real hooks once the viewer has them (player ragdoll/contacts for Fraud, rescue NPCs and
the SN_Ho_Dropoff navpoint for Snatch, kill/destroy events and navpoint positions for Mayhem);
All eight instanced activity types (6.2-6.9) are now in sr3activity and the viewer.
Heli Assault (6.5, `feature/activity-heli`): `include/sr3activity/heli.h` / `src/activity_heli.cpp`
(`HeliRun`: Objectives_Needed, Use_Timer-gated Time_For_Level, fails death / heli destroyed /
protected car destroyed; `ReadHeliParams` from the real layout, objective kind from the top-level
block holding the matching Level_number - CHOSEN 1-based). Viewer `--activity _a_ha_*` +
`--heli-test-objective-every S` (test aid). Smoke: _a_ha_nw_02 -> SUCCEEDED, +$4,000. 6.5's own
OPEN stays: no per-type reader located, so how objectives are counted in-game is data-only.
Genki (6.9, `feature/activity-genki`): `include/sr3activity/genki.h` / `src/activity_genki.cpp`
(`GenkiRun`: exit gate Required_cash before Time_limit, kill cash Base + listed bonuses (additive,
CHOSEN), targets, trap evades, timer, death; `ReadGenkiGlobals` from the real
`running_man_globals.xtbl` in `Running Man.str2_pc` (layout `Running_man_globals/Scoring/...`)).
Viewer `--activity _a_rm_*` + `--genki-test-kill-every S` (test aid); the viewer package reader is
now `ReadPackagedFile(package, file)`. Smoke: _a_rm_dt_01 -> SUCCEEDED, unlocks Activity_Unlock_RM
+ Homie_Pierce. Unethical cash deduction is not floored (6.9 gives none).
Spec-sync question 7 (Team A): the Genki combo rule - what counts as a consecutive score, when the
Max_combo_ticks budget refills and how Base_decay_rate / Decay_rate_table apply, what the
Multiplier_table factor multiplies (kill cash? all cash?), and what Event_Multiplier 3 scales. The
tables are read (`GenkiComboFactor`) but not applied until then.
ANSWERED by spec 6.9.1 (main 06118ca) and implemented on `feature/activity-genki-combo`:
`GenkiCombo` (kept rows, Add/Refill x4/Decay with the two truncations, one level step per add),
kills paid trunc(x M) at the old M then refill; pickups/button/trap evades trunc(cash x M), Add(0,
cash, 0) only while live; negative pickup ends the combo; run total floored at 0 (replaces the
old unfloored deduction and the additive-bonus CHOSEN). Spec vectors V1-V7 are unit tests. Still
not applied: Event_Multiplier, the beep, Intensity (6.9.1 leaves them presentation/untraced).
Trafficking (6.3, `feature/activity-trafficking`): `include/sr3activity/trafficking.h` /
`src/activity_trafficking.cpp` (`TraffickingRun`: Deal_Use_Count deals then Ending_Nav, per-deal
complication rolls capped at Max_complications, fails dealer/player dead or car destroyed;
`ReadTraffickingParams` from the real layout). CHOSEN: deals taken in Deals order; at most one
complication per deal (first hit). Viewer `--activity _a_dt_*` + `--trafficking-test-deal-every S`
(test aid). Smoke: _a_dt_dt_01 -> SUCCEEDED, unlock Activity_Unlock_DT, +$2,000.
Escort (6.8, `feature/activity-escort`): `include/sr3activity/escort.h` / `src/activity_escort.cpp`
(`EscortRun` base/Tiger/Genki: filming test, footage drain + warning, satisfaction from driving time
when unhindered, drop-off, fail order client dead / car destroyed / 10 s abandon / Genki 30 m /
footage (not Tiger) / death; Tiger slap penalty; `ReadEscortParams` from the real Level layout).
Viewer `--activity _a_es_*` + `--escort-test-drive` (test aid; the player's own vehicle stands in
for the client's car; the abandon countdown starts after the first entry - CHOSEN). Smoke:
_a_es_ne_01 -> SUCCEEDED, unlocks Activity_Unlock_ES + Homie_Zimos (real Reward2), +$2,000.
OPEN: the co-op wrong-move penalty's direction ("subtracts" from a counter that counts down);
Slap_Rage_Value's unit; the later drop-off phases (5-0x12) and phase-2 promotion are folded into
`arriveAtDropoff()`.

### 8.1 Start points, HUD bars, end-to-end harness - `feature/activity-e2e`

Every activity instance ships its own zone, `<instance>.str2_pc` (`sr3_city~f<instance>`.czn_pc`),
next to `<instance>_modal.str2_pc`. Its placed objects are real: `local_start`/`remote_start`
(= the row's `Warp_Location`/`Second_Warp_Location`), Snatch's pickup navpoints, hostage/pimp/thug
script_npcs and `SN_Ho_Dropoff`, Fraud's `_A_IF_DT_01_Start` and bonus-spot navpoints, etc.
`--activity-list-objects` prints them.

- Library: `ActivityInstance` gains the four warp names; `ActivityPos`/`Distance`;
  `AtActivityStart` (CHOSEN start point = the Warp_Location navpoint within 3 m - the in-world
  trigger is untraced, section 1, and the shipped rows have Start_Location Disabled);
  `ReadSnatchLayout` (drop-off navpoint, Vehicle_Name, Recruit_Notification_Dist, pickups with
  their Location_Navpoints and Standard_Pickup/Ho_Rescue options); `SnatchHostages` (CHOSEN join
  rule: a waiting hostage within Recruit_Notification_Dist follows; at the drop-off radius of 6.4
  all followers are delivered). `include/sr3activity/harness.h`: `StepToward` and
  `FraudStrikeHarness` (one scripted car strike per ragdoll episode: 1500 kg at 15 m/s, 1.5 s of
  slowing ground contacts, airtime 1.5 s, thrown 12 m) - test aids, not game behaviour.
- Fix: `FraudParamsFromTables` now honours `Cash_Instead` as a flag element. The shipped
  fraud_globals.xtbl marks Airtime, Linear_Dist and Windshield with an empty `<Cash_Instead/>`
  (exactly the three 6.2 lists as cash); Team B's `sr3tables_diversions` reads it with
  `ReadBoolAlways` (text) and gets false, which made e.g. a 12 m throw a x260 multiplier (one
  strike banked $1.1 M). Proposal for Team B (their file, src/tables_diversions.cpp:640): treat
  an empty Cash_Instead element as true.
- Viewer: `--activity X` now ARMS X at its start point; walking in (edge-triggered) starts it.
  `--activities` arms every shipped instance found in the archives (`*_modal.str2_pc` of the 8
  types). The HUD label shows the nearest start point (<= 250 m) while idle; while running, the
  label plus two CHOSEN bars under it (progress gold, time left blue - no activity meter is
  specified in spec-hud-runtime.md). Snatch uses the real hostages and drop-off; each pickup's
  first option at its first Location_Navpoint (CHOSEN). An instance without a resolvable start
  point still starts where the player stands (old behaviour).
- `--auto-activity` (harness, all CHOSEN): teleports 8 m from the start point, walks in at 4 m/s,
  then Snatch: straight-line travel at 20 m/s to the nearest waiting hostage, then the drop-off
  once enough follow; Fraud: FraudStrikeHarness strikes. The run ends 15 frames after the result.
  The older per-type test aids (`--snatch-test-deliver-every`, `--heli-test-objective-every`, ...)
  still work once a run is started; for the other six types use them with `--auto-activity`
  (it reaches the start point, then the aid drives the run).
- ctest `real_activity_e2e_test` (tests/real_activity_e2e_test.cpp, GPU lock, skips without the
  cache): _a_sn_ne_01 (3 + 2 hostages joined, 5 delivered, SUCCEEDED, Activity_Unlock_SN,
  +$2,000, respect 200) and _a_if_dt_01 (25 strikes x $3,252 = $81,306 of $80,000 at ~113 s of
  210, SUCCEEDED, Activity_Unlock_IF, +$2,000) - ~157 s together. Peak viewer memory 3.45 GB
  working set / 540 MB private.
- `feature/activity-e2e-all`: `--auto-activity` now also turns on the per-type test aid at a
  CHOSEN default (Mayhem/Tank kills 0.25 s, stopped once the goal is met so the combo closes;
  Escort test drive; Trafficking deal 5 s; Genki kill 1 s; Heli objective 5 s). Real-data runs,
  all SUCCEEDED with the reward: _a_mh_nw_01 ($93,241/$90,000, 411 s wall), _a_tm_dt_01
  ($125,255/$125,000), _a_es_ne_01, _a_dt_dt_01, _a_rm_dt_01 (needs sr3_city_missions.vpp_pc for
  running_man_globals), _a_ha_nw_02. _a_es_ne_01, _a_tm_dt_01 and _a_ha_nw_02 have no
  Warp_Location navpoint in their zone, so they start where the player stands. Not added to
  ctest (about 15 min together); Snatch and Fraud stay the ctest pair.
- `feature/activity-trafficking-real`: Trafficking uses its real navpoints - the current deal's
  Deal_Nav, then Ending_Nav, resolved in the instance zone; arriving within 5 m (CHOSEN, 6.3 gives
  no radius) completes the deal / the run. The autopilot drives to them; the timed test aid is
  used only when the navpoints are missing. _a_dt_dt_01: deal 003, deal01, end -> SUCCEEDED,
  +$2,000. Mayhem, Escort, Genki and Heli still need kill/vehicle/objective events the viewer
  does not have (NPC combat is Team B's lane), so they keep their test aids.
- Next: autopilot drivers for the other six types (Mayhem kills, Escort drive, Trafficking deals,
  Genki, Heli) need the same kind of real objects (their zones are loaded already); the vehicle
  ("load them, drive to the drop-off") needs a scripted seat + drive of the player car - today the
  harness carries hostages on foot.

## 9. Collectibles and challenges - `feature/collectibles-challenges` (library `sr3collect`)

Source: `spec-tables-progression.md` "Free-roam collectibles, challenges and stat tracking" C.1-C.6
(and section 9). Orchestrator order 2026-10-19: (1) this, (2) Rim Jobs, (3) pause menu/options/rebinding.

| File | What |
|---|---|
| `include/sr3collect/collectibles.h`, `src/collect_collectibles.cpp` | `CollectibleDefsFromTable` (Team B's `ParseCollectiblesTable` rows -> 4 group records, thresholds > 0, max 6), `CollectibleRegistry` (find-or-append, 20 cap, collect routine with the exact-match threshold, screen type 1/2, title, save entry 1 records + restore), `EvaluateCollectionAchievements` (C.5) |
| `include/sr3collect/challenges.h`, `src/collect_challenges.cpp` | `ReadChallenges` (real layout: Reward/Respect+Cash, HUD_Percent/HUD_Linear, Unlock_Requirement, Trigger_Kill/Destroy (+Vehicle_List)/Stat/Special; display unit = Trigger_Stat/Units else Units), `ChallengeDisplayValue`, `ChallengeTracker` (mission unlock, progress routine with clamp and HUD-step notices, stat/kill/destroy/special dispatch with "Any" and "Any Hostile But Police") |
| `tests/synthetic_collect_test.cpp` (126 checks), `tests/real_collect_tables_test.cpp` (37 checks, shipped tables; vectors 2, 5, 8-12, totals 21,001 respect / $68,001) | |
| `tools/sr3_viewer/viewer_playtest_collect.cpp` | with --hud: loads both real tables; vehicle distance -> stat "distance by vehicle"; hud_challenge banner text + collection screen text at (900,140), 3 s (CHOSEN); payment when they end (respect x multiplier via AwardRespect incl. overflow cash, cash with the 0..2e9 cap); `--collectible-test-pickup-every S`, `--challenges-unlock-all` (test aids) |

CHOSEN: rows without Unlock_Requirement start unlocked (none in the shipped table - all 41 have one);
amount <= 0 is the "below epsilon" ignore; the 80 world placements are OPEN in the spec, so the viewer
registers 20 stand-in handles per group only under the test aid; Photo Op uses the same collect path.
Smoke: 16 test pickups (screen types 1 and 2) each paid $1,000 at screen end. The driving feed was not
seen live: --script-vehicle never seats the player at the default spawn (car 25 m away); the stat path
is unit-tested. Not wired: autosave after payment, save entry 26 (challenge progress; no Team B parser),
entry 1 restore from --load-save, kill/destroy/special events (no NPC combat in the viewer yet),
the other 19 Trigger_Stat stats (the viewer tracks none of them), Collectible_Finder map filter.

## 10. FIRST PLAYABLE front end - `feature/frontend-menus` (libraries `sr3frontend`, `sr3game_frontend_teamc`)

Orchestrator 2026-10-19 (top priority, replaces collectibles follow-ups / Rim Jobs / options for now): render
and navigate the front-end menus for sr3.exe. Contract: Team B's `include/sr3game/frontend.h` on
`feature/first-playable` (3fb7423, WIP); this branch CONTAINS it (merged) and builds it.

| File | What |
|---|---|
| `include/sr3frontend/menu_model.h`, `src/frontend_menu_model.cpp` | `MainMenuModel` (Continue / New Game / Load Game / Quit Game; Continue = most recent VALID save, else the MENU_TITLE_NOTICE / MAIN_MENU_CONTINUE_UNAVAILABLE notice (spec Sec3); load list with 8-row scrolling, unreadable saves refused; quit confirm defaulting to No), `PauseMenuModel` (Resume / Save Game / Quit to Main Menu / Quit to Desktop, both quits confirmed; Back = Resume), `DefaultText` (English for the keys) |
| `include/sr3frontend/gamepad.h`, `src/frontend_gamepad.cpp` | XInput pad 0-3 -> edge-triggered menu input (D-pad / left stick with 0.5/0.3 hysteresis, A accept, B back, Start), merged with the host keyboard input |
| `src/frontend_menus.cpp` | the frontend.h functions over the models, drawn through FrontendCanvas (title band, purple highlight bar, panels, confirm boxes, save rows with date / play time / cash / level); Start closes the pause menu; logs `frontend : ...` incl. the stand-in's `main menu result = ...` line |
| `tests/synthetic_frontend_test.cpp` | 41 checks (Continue rule, load list/scroll, confirms, pause actions, pad edges) |

CMake: `sr3` links `sr3game_frontend_teamc` instead of `sr3game_frontend_default` (kept, unused).
CHOSEN: entry lists/order/wording, confirms, layout/colours, gamepad mapping, English text for the keys
(no localisation hook in the contract - proposal for Team B: add `std::function<std::string(const
std::string&)> localize` to FrontendContext, then the menus show the real strings). The retail menus are
Lua + vint_doc screens (pause opens natively: CBA_GAC_PAUSE_MENU -> "dialog_pause_show", Sec7); they are not
run - the native menus follow the spec's structure. No frontend Lua stub needed real behaviour for this.

**Aligned to spec-frontend-flow.md (main 188370f) on `feature/frontend-flow-spec`:** PC auto-accepts press
start (Sec3.2) -> top list Continue (only with a readable save, the newest) / Campaign / Quit Game; Esc on the
top list = PLT_QUIT_GAME / PLT_QUIT_GAME_WARNING confirm (Sec8.2); Campaign = New Game (MAINMENU_NEW) / Load
Game (SAVELOAD_LOAD_GAME); New Game -> difficulty (DLT_CASUAL/NORMAL/HARDCORE, cursor on Normal) -> NewGame;
load list (Sec5.1): readable newest first, unreadable last, empty -> SAVELOAD_NOTHING_TO_LOAD row, main-menu
load without confirmation (Sec5.2), unreadable -> SAVELOAD_ERROR / SAVELOAD_FILE_NOT_LOADABLE; pause
(ui-controls Sec18.1): header SYSTEM_PAUSED, Save Game / Quit (PAUSE_MENU_QUIT_TITLE / QUIT_GAME_TEXT_FULL ->
main menu) / Quit to Desktop (CHOSEN extra), Back resumes (no Resume entry). Not yet (need host/contract
support - proposals for Team B): MainMenuResult difficulty field (chosen difficulty is only logged);
PauseMenuAction LoadSave(path) for pause Load Game; a save target path for the SAVE-mode list (New save row +
overwrite confirm, Sec7.2/7.3); an autosave flag in SaveFileInfo (autosave-first order, Sec5.1); Options
(orchestrator item 3); display-calibration screen (skipped). 42 model checks.

**Options (`feature/frontend-options`, spec-tables-ui-controls Sec18.2):** `include/sr3frontend/options.h` /
`src/frontend_options.cpp`: Options entry on the main top list and the pause list -> list Mouse / Gameplay / Display
/ Audio (vector 4); pages edit the REAL profile options struct (sr3save::ProfileOptions, spec-save-format 7.5): mouse
invert Y/X (+0x4C bits 1/2), sensitivity (+0x50/+0x54), vehicle camera snap (+0x58); difficulty (applied when the
Gameplay page is left, values > 2 ignored, Sec18.4), airplane controls (+0x60 bit1), subtitles (+0x20), radio
(+0x44), minimap view (+0x21), pause on focus loss (+0x4C bit0); brightness (+0x24), gameplay vsync (+0x22); SFX
(channels 3-5 together), music (1), voice (2), 0-100 step 10; Restore Defaults per page (not Display), confirmed.
Leaving a page commits: the struct is written into `<saveDir>/sr3def_profile` (patched at 0x2000/0x2004 if the file
exists, else a new 0x4248-byte image with profile-valid 0 so default bindings stay). Defaults = Sec18.2 shipped
values. CHOSEN: labels, mouse-sensitivity UI scale (0.24 = 50), brightness UI 0..100 = 0..2, airplane bit1 = Normal.
NOT applied to the game yet - proposal for Team B: the host reads `sr3frontend::LoadOptions(saveDir)` (or a
FrontendContext hook) and applies sensitivity / invert / volumes / minimap / difficulty. Not built: display.ini
items, Controls (pad) page, Remap (rebinding, next step), the profile key-binding tables. 67 frontend checks.

**Rebinding (`feature/frontend-rebinding`, spec-tables-ui-controls Sec18.3):** `include/sr3frontend/bindings.h` /
`src/frontend_bindings.cpp`: `BindingModel::FromTable` over Team B's ParseControlBindingSetsTable (General, On Foot,
Car, Helicopter, Plane, Tank; Camera hidden; Debug_Only / Non_Release_Final rows skipped; an axis = two rows +/-;
114 rows from the shipped table), `bind()` with the conflict rule (same set, every set for the common General set;
losers cleared to UNBOUND and listed; allowed-sharing pairs skipped), input check (key scan code 1..0xFF with a name,
mouse 0..6), `resetAll`. Options -> Mouse -> Remap Controls: categories (+ confirmed Restore Defaults = the retail X),
rows with primary/secondary columns, "press any key" capture by Win32 polling in frontend_menus.cpp (scan codes via
MapVirtualKey; Esc cancels; mouse wheel not capturable this way). Tests: real_bindings_test (shipped table, vectors
7/8/9) + synthetic remap flow (91 frontend checks). CHOSEN: a cell holds one input (key XOR mouse); allowed-sharing
pair lists empty (contents not dumped - Team A question); key names past the spec's list = standard set-1 scan codes.
INTERIM: the binding table is read from %CRREISH_GAME_CACHE_DIR%/misc_tables.vpp_pc (else the build-time cache) -
proposal for Team B: a FrontendContext table-reader hook. Not done: writing the bindings to the profile tables A/B
(spec-save-format 7.6), the game using them (Team B input layer), gamepad presets.

Changes in Team B files (small, needed for the scripted flow; please keep or replace):
- `tools/sr3_game/main.cpp`: ScriptedMenuInput = Down then Accept (New Game is 2nd); ScriptedPauseInput adds
  Left + Accept for the quit confirm; the scripted run passes `--frames roamFrames+200` because `--no-input`
  alone caps the playtest at 300 frames - the very frame the script opens the pause menu (the session ended
  before the pause, with any front end).
- `tools/sr3_viewer/viewer_playtest_frame_player.cpp`: `bridge.stepFrame` is skipped when `animFrame.dt == 0`.
  A zero-dt frame (pause open; also a live >0.25 s hitch, which stepFrameBegin clamps to 0) crashed with
  0xC0000409 inside stepFrame (bisected with traces: last good point before the call, nothing after). The
  root cause inside the movement/collision step is NOT located - Team B should look (it predates the menus).

Result: `sr3.exe --script-menu=new --frames 300 --headless`: main menu -> New Game -> free roam -> pause ->
Save (sr3save_01 written) -> Quit to Desktop (confirmed) -> exit 0; captures main_menu.png / pause OK.
Not done: Options / control rebinding screens (orchestrator item 3, later), real vint_doc rendering of the
retail screens, gamepad Start to OPEN the pause menu (host opens on Esc only - proposal: host ORs pad Start).

## 11. "Looks like the game" track (orchestrator 2026-10-19: ground, sky, sun/TOD, real HUD art)

- Ground (`feature/ground-render`): the playtest drew only the FIRST layout-0/2 Mesh sub-block of a tile's zone
  (`tryRenderZoneEntry`); sr3_city~s0915 has 143 blocks, so 26 triangles drew and the sky clear colour showed under
  the buildings. New opt-in `allBlocks` on `loadCityGroundTile` (viewer_city_ground.cpp / viewer_city_support.h):
  every block's first layout-0/2 channel draws; texture ranks are assigned over the union of their material ids
  (Team B's existing CHOSEN rank mapping, so materials can be wrong - e.g. paving everywhere). Only the playtest
  (sr3.exe) passes it; city / walk / drive commands and every golden scene are byte-identical. Spawn tile: 45,692
  triangles, 226 ranges. Collision unchanged (0x2237 path). Not done: layout-4 channels, correct material mapping
  (needs the zone material table -> diffuse map rule; Team A question), more than the 2x2 streamed tiles.
- Sky (`feature/sky-gradient`, contains ground-render): `tools/sr3_viewer/viewer_playtest_sky.cpp`, a screen-space
  gradient drawn after the clear (one hook line in viewer_playtest_frame_render.cpp, HUD runs only - the quad
  pipeline exists only with --hud), coloured from the real District_Skybox colours (West1-4, East1-4, West_Zenith;
  spec-tables-environment 3.3) blended with the lighting's own 2x2 segment x stage corner blend. CHOSEN: bands at
  0/12/30/55 deg + zenith 90, West = -X / East = +X, joint scale-down when a channel > 1. Not done: the real skybox
  meshes (skybox.vpp_pc rfg_skybox / skybox_clouds .csmesh_pc + cloud peg, sun/moon bitmaps).
- Exposure (`feature/scene-exposure`, contains sky-gradient): `applySceneExposure` (viewer_playtest_sky.cpp, hooked
  after ComputeTodLightingBlended in the render step, HUD runs only) replaces the per-colour normalisation (ambient
  and sun each to 1.0 -> washed out) with ONE factor on the real blended Ambient_Color and TOD_Light_Color (colour x
  intensity): brightest channels sum to 1.25, never scaled up (night stays dim), ambient x2 (CHOSEN lifts for the
  unmodelled HDR eye adaptation / indirect light). Spawn: raw ambient 0.89/0.87/1.25, sun 11.0/6.9/6.1 -> k 0.10.
  Not done: the real tonemap / bloom (spec 3.3 floats listed, formula OPEN), sun direction (spec negative; CHOSEN arc).
- Spec sync d1f8f7a (`feature/visual-spec-sync`, contains scene-exposure + main): spec-looks-like-the-game.md.
  * Ground per spec-zone-data-format 13.5.2 recipe: every layout-0/2 channel any draw range uses, near-origin
    local-space blocks skipped (spec CHOSEN +-50 m rule; incl. the old first block #0 template). Spawn: 38,856 tris.
  * Sun direction per spec-tables-environment 19.2 (differential D-23): new lib `sr3sky` (`include/sr3sky/sun.h`,
    `TodLightDirection`, `SunTowardDirection`; 19.2 + D-23 vectors pass) drives the lighting AND the shadow map
    (2 lines in viewer_playtest_frame_render.cpp, replacing ComputeChosenSunDirection there).
  * Base TOD curves per 19.1: `include/sr3sky/tod_curves.h` (`TodBaseCurves` parses default_district.xtbl, 48-slot
    resample with the 47-span wrap quirk, per-frame slot blend, rgb x intensity); tests: synthetic (resample/read
    vectors) + real_tod_curves_test (12:00/21:00 table values). The playtest uses them for a weather corner with
    no weather_time_of_day rows (Clear Skies), else the override cells as before; corners mixed by the scheduler.
    Clear noon is now a blue sky with warm light (the old path fell back to Overcast cells).
  * Not yet from this sync: the 19.4 sky dome/cloud/mountain shading + fog, 19.3 sun/moon sprites, back-ambient,
    HUD art (H.7) - next items.
- HUD art started (`feature/hud-art`): viewer_playtest_hudart.cpp loads a named sheet sub-image (bitmap_sheets.xtbl
  -> <sheet>.xtbl rect via docOf, page <sheet>.tga via findAndUploadTexture) and the health bar fill now draws the real
  ui_hud_meter_fill (ui_bms_01 (311,1,290x12), page 768x1024 - matches H.7 vector). 1 line in frame_render.cpp.
  Next: more ui_hud_* elements (respect meter, weapon icon, notoriety), then real hud.vint_doc layout.
- (old plan) item 4, HUD art, spec-hud-runtime H.7: runVintDoc (viewer_vintdoc.cpp, Team B) already loads bitmap_sheets +
  <sheet>.xtbl sub-images and the interface-backend peg, but it is one 1,000-line function (no reusable class). Plan:
  factor a small Team C sheet loader (sheet name -> SRV, sub-image rect) in a new viewer file, then draw the real
  ui_hud_* bitmaps (health ring, respect meter fill, weapon icon) with VintQuadPipeline::quad(srv, pos, uv) in
  drawActivityHud-style hooks, keeping the current labels. Vectors: ui_hud_meter_fill -> ui_bms_01 (311,1,290,12).

## 6. PLAN (not started): wiring sr3hud / sr3saveflow / sr3progression into the viewer

Start only after Team B's refactor step 3 (engine_state.h split, lua_host_run split) is on main;
re-check every file:line below against main first (they come from a read-only survey of the tree
at 854ae94 + Team C branches, 2026-10-17). One branch per step, build + full ctest each, viewer
smoke run (playtest 60 s headless, stdout diff vs. before) for steps that touch the frame loop.

Facts the plan rests on (survey):
- HUD today: `stepRender()` in `tools/sr3_viewer/viewer_playtest_frame_render.cpp`, block
  `if (hudEnabled)` (~:393-554): HP via Lua `get_current_hit_points`, max from `sess.hudMaxHp`,
  ammo from `weaponTemporaryInventory()["player"]["pistol"]`, cash `playerCash()/100`, police
  notoriety only (`notorietyAliasTrackStates()[3]`), flat rects + `drawHudLabel`; no respect.
  Minimap ~:522-553: `updateMinimapLiveTransform`, 200x200 box top right, flat tile colour,
  `drawMinimapBackgroundQuad` / `drawMinimapPlayerMarker` (`viewer_hud_support.cpp:178/207`).
- State: HP `CharacterState::currentHitPoints/maxHitPoints`; cash `EngineState::playerCash()`
  (cents); notoriety `notorietyAliasTrackStates()` codes 0-3; respect `respectTotal()/
  respectBanked()/respectLevel()` (a second copy in `saveGame()`, never synced); weapon
  `sess.bradWeapon->roundsInClip()`; seated `sess.inVehicle`; vehicle velocity
  `vehicleRig.sim->state_.velForwardBody/velRightBody`; position `pm.st.position`; camera forward
  `followCam.committed().forward` (on foot) / local `camForward` (vehicle), not stored on the session.
- Save: the viewer never calls the save bridge; spawn is `--spawn` / defaults, final warp
  `pm.warp(...)` in `viewer_playtest_setup_world.cpp:~557`. Bridge builds from a zero-filled
  image (drops 0x4A5C). `game_autosave` / `mission_autosave` stubs write through the bridge.
- Respect: `EngineState::awardRespect` (`src/engine_state_progression.cpp:~311`) has no production
  caller; `stub_game_award_respect` only records the grant; `respectLevelTable()` is test-filled;
  the viewer does not load `respect_levels.xtbl`.
- CMake: `sr3_viewer` main link line `CMakeLists.txt:~1577`; none of the three libraries is linked.

Steps:
1. **CMake + respect table (small).** Link `sr3hud sr3saveflow sr3progression` into `sr3_viewer`
   (and `sr3luahost` for step 4). In the viewer's table setup (`sess.docOf` / `miscRead`), load
   `unlockables.xtbl` then `respect_levels.xtbl` into a `sr3progression::RespectTable` held on the
   session.
2. **HUD producers (viewer only).** Add a `HudDataItems` + `RespectProducer` +
   `RadioStationProducer` to the session; each frame in `stepRender` fill `PlayerStatusInput`
   (HP/max from the player's CharacterState), `PlayerStatusInfrequentInput` (`playerCash()`),
   `RespectInput` (total/in-level/level + `RespectTable::cost(L)` as need), notoriety (all four
   tracks via `PushNotoriety`, thresholds from `notoriety_levels.xtbl` limit(set, L+1)), weapons
   (`roundsInClip`, reserve from the inventory count). Then drive the existing labels from
   `display_model.h` (health ring angles as a bar fraction until the vint_doc HUD renders,
   `CashDisplay` count-up text, `NotorietyDisplay` icon count, `RespectDisplay` level/"+N",
   `AmmoText`). Keep the old rects behind a flag for one release so the golden HUD scenes can be
   re-baselined deliberately (they will change: new values and positions).
3. **Minimap (viewer only).** Replace the box minimap: centre `kMinimapCentreRef` scaled by the
   UI scale, `MinimapCentre` (player/vehicle), `MinimapSpeedZoom` fed by
   `MinimapZoomSourceSpeed(|vel| of the current vehicle, last vehicle)`, `ComputeMinimapRotation`
   with the camera heading `HeadingFromForward(camera forward)` (store the committed camera forward
   on the session each frame - small new session field), `SelectMinimapBaseTexture` +
   `MinimapBaseQuad` with the real `minimap_CCRR` texture (reuse `runMinimapDemo`'s `resolveTile`
   archive lookup, moved into `viewer_hud_support`), player glyph at the centre. Remove the viewer's
   use of `minimap_live_transform.h` (keep the header until its test is retired with Team B's OK).
   Golden: the minimap and HUD-live scenes need a deliberate re-baseline.
4. **Respect (engine_state - needs Team B's go-ahead, it is their file).** Make
   `EngineState::awardRespect` call `sr3progression::AwardRespect` on its three fields with the
   loaded `RespectTable` (or delete it in favour of a thin wrapper), add `overflowCashRaw` to
   `playerCash()`, apply `levelUps[].newlyApplied` through the existing unlock path, and make
   `stub_game_award_respect` actually award (it only records today). Sync `saveGame()` respect
   from the live fields at save time (or retire the copy). Review item RP-6.
5. **Save/load (viewer + bridge).** Add `--load-save <path>` and `--save-dir`: on load, run
   `PlanFreeRoamLoad` with a `LoadWorld` whose `resolveHandle` maps the 10 crib handles
   (`KnownCribIndex`) to their transforms and whose `playerStartMarker` is the zone's player-start
   marker (fallback: current `--spawn` default); apply cash/respect/multiplier/clock in plan order
   and warp at the `pm.warp` site. On save (pause menu Save, gated by `PauseMenuSave`; autosaves
   gated by `AutosaveAllowed`), keep the loaded image and call `PatchFreeRoamSave` + `MakeSlotRecord`
   + `BuildSaveDirectory`; new game uses `BuildNewGameSnapshot`. Point the bridge's autosave write
   at the patch path so `0x4A5C` survives. Bridge edits are `src/lua_save_game_bridge.cpp`
   (luahost) - coordinate with Team B.

## Status

- All earlier Team C branches (hud-minimap, save-integration, respect-progression, the refactor,
  the review fixes, cutscene-playback) are merged to main.
- `feature/viewer-wiring-step1` (section 6 steps 1-2, DONE): step 1 feeds the real
  `respect_levels.xtbl` to `EngineState::setRealRespectTable` at playtest HUD setup (Team B did
  the RP-6 engine side); step 2 drives the playtest HUD labels through sr3hud (health bar from
  health_pct, cash count-up, notoriety icons, new RESPECT label top right; ammo unchanged, no
  reserve modelled). Build 0, full ctest 221/221 incl. golden; m01 20 s smoke + capture OK. Queued
  2026-10-17.
- `feature/viewer-wiring-step3` (section 6 step 3, DONE; contains step1): the playtest's box
  minimap is replaced by the H.3 corner minimap (C=(191,566) x UI scale, speed zoom from the
  vehicle, camera-heading rotation from the camera forward now stored by stepPlayerView, the real
  minimap_CCRR.tga - note the record name needs ".tga" for findAndUploadTexture - player glyph,
  north marker). CHOSEN: square window instead of the mask stencil, white square glyph, no blips,
  rotating mode only. Build 0, full ctest 221/221 incl. golden; free-roam capture OK. Queued.
  Side note for Team B: runMinimapDemo's resolveTile passes "minimap_CCRR" without ".tga", so the
  demo never finds the real tile (endsWith match on the record name) - not changed here.
- `feature/viewer-wiring-step5` (section 6 step 5 viewer half, DONE; contains step3): playtest
  `--load-save <file>` (PlanFreeRoamLoad: respawn-crib placement x/z, cash, respect verbatim,
  multiplier, time of day) and `--save-at-exit <file.sr3s_pc>` (PatchFreeRoamSave over the loaded
  or a new-game image), in tools/sr3_viewer/viewer_playtest_saveflow.cpp. Adds
  EngineState::restoreRespectFromSave (8 lines in Team B's engine_state_progression.h). A.4-style
  smoke: loads at Crib_SaintsHQ, HUD LEVEL 5 850/2575 and $5,000; resave changes only hash, clock,
  play time. Build 0, full ctest 221/221 incl. golden. Queued.
- `feature/activity-fraud` (section 8): sr3activity + viewer --activity; build 0, full ctest
  228/228 incl. golden; queued.
- `feature/activity-snatch` (contains activity-fraud): Snatch library + viewer; build 0, full
  ctest 228/228 incl. golden; queued.
- `feature/activity-mayhem` (contains activity-snatch and activity-fraud): Mayhem / Tank Mayhem
  library + viewer; build 0, full ctest 228/228 incl. golden; queued.
- `feature/activity-escort` (contains activity-mayhem and below): Escort library + viewer;
  build 0, full ctest 228/228 incl. golden; queued.
- `feature/activity-trafficking` (contains activity-escort and below): Trafficking library +
  viewer; build 0, full ctest 228/228 incl. golden; queued.
- `feature/activity-genki` (contains activity-trafficking and below): Genki library + viewer;
  build 0, full ctest 228/228 incl. golden; queued.
- `feature/activity-heli` (contains activity-genki and below): Heli Assault library + viewer;
  build 0, full ctest 228/228 incl. golden; queued.
- `feature/activity-e2e` (contains activity-heli and below): section 8.1 - start points,
  HUD bars, --activities/--auto-activity, Cash_Instead fix, real_activity_e2e_test; build 0,
  full ctest 229/229 incl. golden and the new e2e test; queued.
- `feature/activity-genki-combo` (contains activity-e2e + main 06118ca): Genki combo per 6.9.1;
  build 0, full ctest 237/237 incl. golden; queued.
- `feature/activity-e2e-all` (contains activity-genki-combo): auto test aids for the other six
  types, all eight succeed on real data; build 0, full ctest 237/237 incl. golden; queued.
- `feature/activity-trafficking-real` (contains activity-e2e-all): Trafficking on real deal
  navpoints; build 0, full ctest 237/237 incl. golden; queued.
- `fix/activity-cash-cap` (contains activity-trafficking-real + main): activity cash grant clamps the
  wallet to 0..2,000,000,000 cents (review RP-2 cash cap). Review 2026-10-19: AE-1..4, GC-1..5 all
  CORRECT/labelled. Build 0, full ctest 237/237; queued.
- `fix/activity-respect-overflow` (contains fix/activity-cash-cap + main 545ff46): review CC-2 - the
  activity grant also pays AwardResult::overflowCashRaw (respect past the last level) into the wallet,
  each add saturating 0..2e9 cents; the logged reward is the wallet's real gain. The engine-side
  overflow drop (src/engine_state_progression.cpp:586) is Team B's file. Build 0, full ctest 240/240; queued.
- `feature/collectibles-challenges` (contains fix/activity-respect-overflow + main): section 9;
  build 0, full ctest 245/245 incl. golden; queued.
- `feature/frontend-menus` (contains feature/first-playable 3fb7423 + feature/collectibles-challenges):
  section 10; build 0, full ctest 247/247 incl. golden + real_first_playable_smoke_test; queued.
- `fix/differential-teamc` (contains feature/frontend-menus + main 188370f): Team D differential vectors for
  sr3progression as ctest `differential_progression_test` (reads spec-differential-vectors-progression.md V1-V5 and
  -cash.md at run time: 1,159 vectors, 0 mismatches). Fixes: D-1 RoundProductF32 (single-precision product, halves
  away, INT32_MIN out of range) in ScaleByMultiplier / KillRespect / ActivityCompletionAwards (unsigned Props_Award);
  D-2 diversion 0.001 threshold + floored cash; D-3 toast respect OR cash; D-4 32-bit sums in AwardRespect/level-up;
  D-22 AddCashToWallet (cash multiplier on positive local grants, wallet clamped then 32-bit add, +-2e9, floor 0 -
  the clamp-before-add is inferred from the vectors) used by the activity and collect payments (multiplier 1.0: no
  Cash_Bonus state in the engine yet). Proposal for Team B: route sr3player::AddCashSaturating through
  AddCashToWallet with a real cash multiplier. V6 (container hash) is Team B's. Build 0, full ctest 248/248; queued.
- `feature/frontend-flow-spec` (contains fix/differential-teamc): menus aligned to spec-frontend-flow.md
  (section 10); host scripted input updated; build 0, full ctest 248/248 incl. golden + first-playable smoke; queued.
- `feature/frontend-options` (contains feature/frontend-flow-spec): options pages + profile persistence
  (section 10); host pause script gets one more Down; build 0, full ctest 248/248 incl. golden + first-playable smoke; queued.
- `feature/frontend-rebinding` (contains feature/frontend-options): PC rebinding model + Remap screens
  (section 10); build 0, full ctest 249/249 incl. golden + first-playable smoke; queued.
- `feature/ground-render` (from main 97bad5c): playtest/sr3.exe draws every ground Mesh block (section 11);
  build 0, full ctest 253/253 incl. golden (unchanged); queued.
- `feature/sky-gradient` (contains feature/ground-render): TOD sky gradient (section 11); build 0, full ctest
  253/253 incl. golden; queued.
- `feature/scene-exposure` (contains feature/sky-gradient): joint scene exposure (section 11); build 0, full
  ctest 253/253 incl. golden; queued.
- `feature/visual-spec-sync` (contains feature/scene-exposure + main d1f8f7a): ground recipe, D-23 sun, base TOD
  curves (section 11); build 0, full ctest 255/255 incl. golden; queued.
- `feature/hud-art` (contains feature/visual-spec-sync): real ui_hud_meter_fill sheet art on the health bar;
  build 0, full ctest 255/255 incl. golden; queued.
- `fix/review-lg4` (contains feature/hud-art): review LG-4, sky east/west from the spec sunrise direction,
  joint scale-down labelled CHOSEN; queued. LG-6 was already fixed in 14846cd.
- `fix/review-lg8` (contains fix/review-lg4): review LG-8, HUD art cache moved from a static map keyed by the
  session pointer into PlaytestSession (hudArtMeterFill/hudArtTried); build 0, full ctest 255/255; queued.
- `feature/d24-tod-merge` (contains fix/review-lg8 + main e902321): D-24 / 19.1 "Live wiring" - every sky,
  ambient and sun value = default_district base curve at the hour, current segment's from/to weather cells
  merged per field (absent field keeps the base; other segment unused); replaces the old corner blend and
  closes LG-3. The merge with main resolved the hudart conflict with Team B's 987ea9e (sessionId-keyed static
  map) to the session-owned cache. Script-forced weather (merge off) is not modelled in the playtest.
  Build 0, full ctest 257/257; queued.
- Review e03e9db: TA-1 LG-8 FIXED, TA-3 D-24 CORRECT; all of hud-art, lg4, lg8, d24-tod-merge are on main.
  TA-3's unverified points: both cells come from one segment (br.low, the current one) in ComputeSceneColours;
  the debug override record and the script-weather flag are not modelled (no source of either in the playtest).
- `feature/hud-art-2`: police notoriety icons (ui_hud_not_police, ui_bms_01 (357,878,52x58)) at the H.2
  offsets around the minimap centre, scale 0.5, floor(level) lit; tweens not played, white, centred (CHOSEN).
  HUD art cache is now per image name on the session (hudArtImages). Seen with --debug-notoriety=3 (3 badges).
  One hook line in viewer_playtest_frame_render.cpp after the minimap. Build 0, full ctest 258/258 (6 long
  playtest tests re-run separately after the background limit killed the first run); queued.
- `feature/hud-art-2` (cont.): drawHudBitmapCircle = vint bitmap_circle (spec-vint-render 13.2; wedges as
  degenerate strip quads, count clamped 1..256) and the respect meter ring: ui_hud_respect_fill
  (ui_bms_01 (639,51,70x70)) at (1135,87) ref, end = 2.65 - 5.25 x RespectPct. CHOSEN: start 2.65, image size,
  white; respect_meter_bg and the level text not drawn. Checked with a throwaway pct 0.4 build (arc visible).
  Build 0, full ctest 258/258; queued.
- Weapon icon BLOCKED on spec: H.2 names wpn_bmp_name as the weapon definition's "+0x19C record" but no
  weapons.xtbl element name for it (spec-tables-weapons-combat has none) - spec-sync question for Team A.
  The health ring (health_bar bitmap_circle, ui_hud_wpn_health, around the weapon icon) can use
  drawHudBitmapCircle once the weapon block layout (base_weapons offset) is decided.
- `fix/levelup-vectors` (contains feature/hud-art-2): spec sync 8a10f2b, tables-progression 5.4 note (Team D
  FINAL). ApplyLevelUp already paid record L and applied record L+1 with the cap at count-1; added the
  vectors (2115 -> 1/0 + "u1", 2114, 100+2015, 5115 -> two ups, negative floors) and the in-loop
  "needed <= 1 blocks levelling" guard (was only checked before the loop). Build 0, full ctest 258/258; queued.
  The free-roam difficulty answer (set A row `difficulty`, default 1) is Team B's (host/options apply).
- Next (section 11): the weapon block (health ring + icon) and the hud.vint_doc
  layout; later 19.4 sky dome/fog (LG-5), 19.3 sun/moon sprites, the LG-7 98/45 count log.
- Remaining (proposal for Team B, their file): point the save bridge's autosave write
  (src/lua_save_game_bridge.cpp writeAutosaveSnapshot) at PatchFreeRoamSave over the loaded image
  so 0x4A5C and the crib block survive Lua autosaves; a pause-menu Save entry (PauseMenuSave gate).
  Also open: a real player-start marker and the crib objects' own orientation in the viewer.
