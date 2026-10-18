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
- Remaining (proposal for Team B, their file): point the save bridge's autosave write
  (src/lua_save_game_bridge.cpp writeAutosaveSnapshot) at PatchFreeRoamSave over the loaded image
  so 0x4A5C and the crib block survive Lua autosaves; a pause-menu Save entry (PauseMenuSave gate).
  Also open: a real player-start marker and the crib objects' own orientation in the viewer.
