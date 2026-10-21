# Free-roam gap audit refresh, 2026-10-20 (Team B, read-only)

Scope: `main` @ bd5330d vs the 18 gaps in `results/freeroam_gap_audit_20261017.md`. Method: read current code in `tools/sr3_viewer/viewer_playtest_*`,
`include/sr3*/`, `src/`, and the spec-*.md files at the repo root (all spec files live at the repo root). Nothing was built, run or disassembled.
Verdicts come from code, not comments (several in-code comments are stale, listed in section 5). `vp_` = `tools/sr3_viewer/viewer_playtest_`.
Spec labels are quoted as the spec writes them; "CHOSEN" in the viewer means a stand-in, not a spec value.

## 1. Status of the 18 old gaps

| # | Old gap | Status | Evidence / what is missing |
|---|---|---|---|
| 1 | Player damage, death, respawn | DONE | `vp_frame_vitals.cpp` (regen, death phases, fade, hospital respawn, $500 penalty); spec-player-runtime §18 (2026-10-18) now covers respawn point. Mission-mode death path deliberately not run (`vp_frame_vitals.cpp:15`, §18 OPEN) |
| 2 | Jump and crouch | DONE (value CHOSEN) | `include/sr3player/jump_crouch.h`; Space/C bound (`vp_frame_input.cpp:226-228`). Height 1.0 m is CHOSEN (`jump_crouch.h:17,33`, spec gives no height argument for `0x0098bea0`) |
| 3 | Enter ambient cars / carjack | DONE | `vp_frame_input.cpp:430-484` (any live ambient car, occupied = carjack, per-car model). Enter/exit animation still absent (see 18) |
| 4 | Crime -> notoriety -> police | DONE core, PARTIAL tail | `vp_frame_traffic.cpp:1228` `notorietyReportCrime`; `vp_frame_police.cpp` (level-driven dispatch, chase cars, foot cops that shoot back, bust gates). Missing: cop-witness LOS test is CHOSEN (`vp_frame_traffic.cpp:1225-1227`); bust ragdoll gate is a CHOSEN stand-in (`vp_frame_police.cpp:357,375`); chase cars use direct steering, not the route planner (`vp_frame_police.cpp:6-10` header) |
| 5 | Ambient models + ped heading/anim + own ground Y | DONE core, PARTIAL tail | `vp_ambient_models.h`, `vp_frame_render.cpp:421-432` per-ped walk clip. Missing: vehicle `Variant`/paint not mapped (variant _0 only, header line "CHOSEN"), ped Height scaling not applied, no knockdown/death pose (dead/knocked peds freeze standing, `vp_frame_traffic.cpp:939`) |
| 6 | Vehicle camera at spec values | DONE | `vp_frame_player.cpp:759-783` `VehicleChaseCamera` (Sec4.15). Reference point still CHOSEN (`:762`, spec §4.15.5 OPEN) |
| 7 | Gun/impact/engine/horn/collision audio | PARTIAL | Footsteps real (`vp_frame_player.cpp:321`). Fire sound code exists (`:521-526`) but `sess.weaponFireClip` is declared (`vp_session.h:572`) and never assigned anywhere, so `.valid` is always false and weapons stay silent (counter `weaponFireSoundsUnresolved`). No engine, horn, tyre, impact, or ambient audio call exists in `vp_*` |
| 8 | Vehicle-vs-vehicle/ped collision and damage | PARTIAL | `vp_frame_collision.cpp` (run-over, car-car impulse, 0.0055 damage scale, knock/kill). Missing: ambient cars take no damage (`vp_frame_traffic.cpp:473-475`); no destruction, smoke, fire, explosion, or part detach (grep finds none) |
| 9 | Weather schedule | PARTIAL | `WeatherScheduler` wired (`vp_session.h:474-480`, `vp_setup_assets.cpp:272`). Missing: no rain/lightning/wind visuals or audio (no consumer of `rain.xtbl`/`lightning.xtbl` in `vp_*`) |
| 10 | Despawn schedule + fades | PARTIAL | Camera-gated despawn is live and is the despawn (`vp_frame_traffic.cpp:246-248`, `runDespawn=false` is deliberate). Missing: the 1000 ms fade is only counted, no alpha fade is drawn (`:230`) |
| 11 | Stop signs, mode gate, junction yield | PARTIAL | Signals, stop-line source (kind 4) and yield wired (`vp_frame_traffic.cpp:310-311,552-553,620`). Missing: ambient cars have no directive mode byte, so the "mode 8 + obey bit" gate is CHOSEN to "all near-field cars" (`:553`); no mode 0-0x10 behaviour table |
| 12 | NPC LOS / cover / downed facts (AI-3/7/11) | DONE | LOS assigned for hudEs (`vp_setup_audio_hud.cpp:354`), homies (`vp_frame_homies.cpp:827`), cops (`vp_frame_police.cpp:120`), session `npcLosWorld_` (`vp_session.h:807`); cover/downed counters live (`vp_frame_render.cpp:1218-1224`) |
| 13 | Walk-speed pool | DONE | `nearFieldPedWalkSpeed` per ped (`vp_frame_traffic.cpp:721,1071`); only the fallback `kWanderWalkSpeedMsChosen` remains (`vp_frame_render.cpp:431`) |
| 14 | Lane re-query/lookahead/brake/include-mask | DONE | Re-query 15 m (`traffic_lane_follower.h:773`), include mask passed (`vp_support.cpp:432`), per-car braking (commit bae7600). Residual: lookahead 15.0 is CHOSEN (`traffic_lane_follower.h:766`) |
| 15 | Melee | OPEN | No key, no code. Spec-player-runtime §15.2: trigger CONFIRMED, move selection and damage application "OPEN" (boundary `0x00985760`) |
| 16 | Vault/climb, swim, cover, ladder | OPEN | Player cover/vault/climb/swim never invoked; only `movement.h:234,336` parameters and `vp_craft.cpp` boat-on-water exist. Spec: §3.6.4 CONFIRMED trigger only; bodies (`0x009660e0`, `0x00989ed0`) not specced; §5.5 swim branch named; water volume §10.13.6 "PARTIAL" in spec-physics-runtime |
| 17 | Weapon set: reload, switch, ammo, aim assist | PARTIAL | Reload, slots 1-9, Q/Tab, ammo, auto-reload/switch done (`vp_frame_player.cpp:384-534`, spec-weapons-runtime §5.4 2026-10-18). Missing: aim assist (spec §15.1 selector CONFIRMED, steering half OPEN), scope/aim camera, grenades/explosive throw (no viewer code; spec-weapons-runtime §2a.4 "code-side consumer OPEN") |
| 18 | Vehicle enter/exit position and animation | PARTIAL | Exit still a lateral offset (`vp_frame_input.cpp:576`); no door-based placement or enter/exit clip (spec-player-runtime §6, §11 CONFIRMED, buildable) |

Tally: DONE 7 (1,2,3,6,12,13,14), DONE-core with a partial tail 2 (4,5), PARTIAL 7 (7,8,9,10,11,17,18), OPEN 2 (15,16). New since the old audit but not in its table: air/boat/freefall rig (`vp_craft.cpp`), real MOPP ground, sr3.exe front end.

## 2. Ranked remaining and new Team-B-lane gaps

Impact: H/M/L as a player would notice it in free roam. Spec: label of the governing section. "Now" = buildable now; "A" = needs a Team A answer first.

| Rank | Gap | Impact | Spec status (section, label) | Size | Anchors | Ready |
|---|---|---|---|---|---|---|
| 1 | Weapons are silent: fire clip never resolved | H | spec-weapons-runtime §2.2c/§4.4 sound event selected CONFIRMED; event name -> media mapping unresolved (`vp_session.h` comment, T6); spec-audio-runtime event/bank pipeline implemented | S | `vp_session.h:572`, `vp_frame_player.cpp:521-534`, `vp_setup_audio_hud.cpp:93-137` | Now (CHOSEN clip allowed), A for real mapping |
| 2 | Traffic drivers ignore the player (no honk/swerve/bail-out/aimed-at reactions) | H | spec-ai-runtime §18.7 "FINAL" (port recipe, directive modes), §20.6.7 horn; mode 8 blocked draw `VehicleWaitResponse` §18.x line 5399 | M | `vp_frame_traffic.cpp:553` (no mode byte), `:498` obstacle probe | Now |
| 3 | Vehicle destruction, explosion, smoke/fire | H | spec-vehicle-damage-runtime §4 explosion via `explosions.xtbl` (CONFIRMED structure), §6 smoke/fire/engine-sound "HIGH CONFIDENCE", §7 item 1: the HP-reached-zero trigger "still not located" (OPEN); spec-tables-weapons-combat §10.1 explosions CONFIRMED | M | `vp_frame_collision.cpp:202-208` (damage accumulates, no consumer), `vp_frame_traffic.cpp:473-475` | Partly now (use HP<=0 as CHOSEN trigger); A for exact trigger |
| 4 | Engine, tyre, horn, collision audio | H | spec-tables-audio-radio §8 `foley_collision` CONFIRMED, §10 `foley_engine` CONFIRMED; player horn key F (spec-tables-ui-controls:583); Wwise attenuation OPEN | M | `vp_frame_input.cpp:331-345` (radio only), `vp_frame_collision.cpp:202` | Now for events, A for media mapping |
| 5 | Dead/knocked-down peds: no pose, no ragdoll | H | spec-player-runtime §15.3a ragdoll hand-off CONFIRMED; Havok ragdoll body OPEN; ped clip set spec-anim-runtime | M | `vp_frame_traffic.cpp:939`, `vp_frame_collision.cpp:33,145`, `vp_frame_render.cpp:421` | A (which death/knock clips) |
| 6 | Hostile civilians/gang peds fight back, gang hoods | H | spec-ai-runtime §20.4 FINAL (team byte 7 gates wary vs flee), §21.9 PARTIAL (first-sight trigger OPEN), §19.7/§20.6.10 gang roadblocks FINAL; gang spawn production gap noted at ai-runtime ~9425 | L | `vp_frame_traffic.cpp:1225-1227` team CHOSEN, `vp_frame_police.cpp` (cop path to copy) | A (first-sight trigger, team assignment per spawn) |
| 7 | Rain/lightning/wind visuals and sound | M | spec-tables-environment §5.1 `rain.xtbl`, §5.2 `lightning.xtbl`, §4 `wind.xtbl` all CONFIRMED | M | `vp_session.h:474-480`, `vp_sky.cpp` | Now |
| 8 | Muzzle flash, impact FX, decals, shells, blood | M | spec-tables-environment §10.1 effects, §11.2 decals, §11.4 shells CONFIRMED; spec-effects-format; effect names only indexed for HUD (`vp_support.cpp:876`) | L | `vp_frame_player.cpp:342-355` hit path | Now (L) |
| 9 | Melee | M | spec-player-runtime §15.2 trigger CONFIRMED, resolution OPEN | - | none | A |
| 10 | Vault/climb, ladders, swim, player cover | M | §3.6.4/§5.5 triggers named, bodies not specced; water §10.13.6 PARTIAL | - | none | A |
| 11 | Grenades/explosive throw; aim assist; scope camera | M | spec-weapons-runtime §2a.1 cross-ref, §2a.4 code OPEN; spec-player-runtime §15.1 steering OPEN, §3.6.5 shoulder/scope read but OPEN 20 | M | `vp_frame_player.cpp:384` slot 7 selectable but no throw | Partly (scope cam), A (aim steering, projectile fuse) |
| 12 | Ambient car fade-out not drawn; ped despawn fade | L-M | spec-ai-runtime §20.6.1(d) FINAL (1000 ms fade at `0x00a846f0`) | S | `vp_frame_traffic.cpp:230`, `vp_frame_render.cpp:274-295` | Now |
| 13 | Vehicle exit/enter door placement + animation | L-M | spec-player-runtime §6, §11 CONFIRMED | L | `vp_frame_input.cpp:576` | Now (L) |
| 14 | Ambient cars use one handling row, kinematic, take no damage | M | `spec-vehicle-data` rows CONFIRMED; per-car braking already reads each row | M | `vp_frame_traffic.cpp:459-475,337` | Now |
| 15 | Vehicle `Variant` paint, ped Height scaling | L-M | `vp_ambient_models.h` header "not wired"; spec-vehicle-data variant semantics, spec-tables-customization 12.1 character_definitions field reader "undecompiled" | S-M | `vp_ambient_models.h` | A for ped Height; vehicle variant Now |
| 16 | Mode gate/byte for ambient cars (mode 8 obey bit) | L | spec-ai-runtime §18.7.1 FINAL (modes 0,7,8,0xf obey lights) | S-M | `vp_frame_traffic.cpp:553` | Now (folds into rank 2) |
| 17 | Gamepad | L (for keyboard players) | spec-tables-ui-controls binding layer CONFIRMED | M | `vp_setup_args.cpp:700` "NOT WIRED" | Now (Team C overlap, check lane) |
| 18 | Distant/LOD ambient human tier | L | no spec-confirmed pass (`vp_setup_args.cpp:700-710`) | - | - | A |
| 19 | Police: bust ragdoll gate, route-planner chase | L | `vp_frame_police.cpp:357-375`, header lines 6-10 | M | - | A (see questions) |

## 3. Top 5 buildable-now tasks

### T1. Make weapons audible (rank 1, S)
- Assign `sess.weaponFireClip` at setup: resolve the weapon row's sound event through the existing bank pipeline in `vp_setup_audio_hud.cpp:93-137` (same decode path the footstep clip uses); if the event cannot resolve, fall back to a CHOSEN first-playable gunshot clip and label it CHOSEN in the trace line.
- The playback site already exists (`vp_frame_player.cpp:521-526`); drop the stale "only log the selection" comment at `:516-518`.
- Add dry-fire sound (`FireSoundSelection::kDryFire`, `:508`).
- Verify headlessly: `--script-fire` trace shows `weaponFireSoundsPlayed > 0`, `weaponFireSoundsUnresolved == 0`; keep stdout goldens untouched except the one new counter.

### T2. Traffic-driver reactions to the player (rank 2 plus 16, M)
- Spec: spec-ai-runtime §18.7 FINAL: directive mode byte per car, Honk/Flee/GetOut/aimed-at kinds, obey-lights set by mode (§18.7.1).
- Add a `directiveMode` byte to the ambient car record, set via the mode setter rules; replace the CHOSEN "all cars obey" at `vp_frame_traffic.cpp:553` with the mode-derived obey bit.
- Add the player-proximity triggers (player car close/closing, player aiming at driver) producing honk/swerve/bail; bail uses the existing carjack-empty path in `vp_frame_input.cpp:468`.
- Verify: scripted `--script-enter-ambient-at` plus a driven approach; assert reaction counters and that a honk event is logged (audio plays when T4 lands).

### T3. Rain, lightning, wind from the weather stage (rank 7, M)
- Read `rain.xtbl`/`lightning.xtbl`/`wind.xtbl` (spec-tables-environment §5, §4, all CONFIRMED) keyed by the current weather stage from `sess.weatherTick`.
- Add a camera-attached rain-streak pass in `vp_sky.cpp` using the rain intensity level; fire lightning flashes by the type's interval/probability as a sky/ambient boost.
- Keep blending inside the existing TOD/weather uniform path (`vp_frame_render.cpp:167-170`) so goldens for clear weather do not change.
- Verify: `--weather=Rain` headless capture at a fixed frame, plus a unit test of the lightning interval logic.

### T4. Vehicle audio: engine, collision, horn (rank 4, M)
- Use `foley_engine` (§10) and `foley_collision` (§8) rows from spec-tables-audio-radio to pick the events per vehicle; collision foley triggers off `vp_frame_collision.cpp:202-208` with the row's `MinimumSpeed` (already in m/s per §8).
- Engine loop pitch/volume from the simulator speed/rpm already in the rig; horn on F while driving.
- Resolve events through the same path as T1; where a media mapping is missing, log "unresolved" and keep silent rather than invent a clip (see Team A question A2).
- Verify: drive script counts engine voices, collision events on a scripted ped/car hit, horn on a scripted key.

### T5. Vehicle destruction, smoke and explosion on ambient and player cars (rank 3, M)
- Give ambient cars a vehicle-info HP (`vp_frame_traffic.cpp:473-475` currently skips damage) and apply the existing 0.0055 collision damage to them; CHOSEN trigger: HP <= 0 (label it, spec §7 item 1 OPEN).
- On trigger run the `explosions.xtbl` row from the vehicle record (spec-vehicle-damage-runtime §4, spec-tables-weapons-combat §10.1): radius damage to nearby peds/cars via the existing lethal sink, one-shot smoke/fire effect (§6), occupant ejection/death via `applyGuaranteedLethalHit`.
- Wire notoriety crime report (existing `notorietyReportCrime`) for exploding an occupied car.
- Verify: scripted ram test asserts HP reaches 0, one explosion, nearby ped dies, level rises.

## 4. Team A spec questions (for the blocked or partly blocked items)

A1. Weapon fire sound: which Wwise event/media does the `Sounds` fire event in `weapons.xtbl` map to, and where is the event-to-bank lookup (init.bnk attenuation is still undecoded)? (rank 1)
A2. Vehicle audio: how are `foley_engine` / `foley_collision` events turned into bank events and RTPC values (rpm, speed)? Which event plays the horn at `0x0055a520`? (rank 4)
A3. Vehicle destruction: what is the exact HP-reached-zero decision (spec-vehicle-damage-runtime §7 item 1) and does ambient-traffic damage use the same path, including the per-vehicle `Death_Explosion` field that is "unreconciled"? (rank 3)
A4. Peds: which clips play for knock-down, death (by cause: gunshot, run-over, explosion) and get-up, and when is the Havok ragdoll body used instead of a clip? (rank 5)
A5. Hostility: what makes a civilian or gang ped fight back (§21.9 first-sight trigger OPEN), and how is a spawned ped's team byte chosen (7 = civilian per §20.4)? Are gang members spawned from the hood spawn category or only by notoriety/missions? (rank 6)
A6. Melee: which animation event applies melee damage, and how is the `MeleeMove` row picked (boundary `0x00985760`)? (rank 9)
A7. Climb/vault/ladder/swim: spec the ledge probe `0x009660e0`, climb start `0x00989ed0`, vault `0x009aaf80`, and the swim speed/stop rules including the water-volume query; also the water source for non-boat swimming (spec-physics-runtime §10.13.6 PARTIAL). (rank 10)
A8. Aim: what does the `steering` half of each `aim_assist.xtbl` profile do, and what do `0x009eb700` and the scope-camera branch (§3.6.5, OPEN 20) require? Grenade throw and projectile fuse consumer (§2a.4 code side OPEN). (rank 11)
A9. Jump: confirm the default jump height argument to `0x0098bea0` (we use 1.0 m CHOSEN). (done-but-CHOSEN)
A10. Police: which `CrimeActivity` record does each in-world action report, does the cop-witness test use LOS, what replaces the CHOSEN bust ragdoll gate, and is there a route-planner spec for chase cars? (gap 4 tail)
A11. Ped Height/Height_Class scaling and the `character_definitions` field reader (spec-tables-customization 12.1 "undecompiled"). (rank 15)
A12. LOD/distant ambient human tier: is there a confirmed pass for it? (rank 18)

## 5. Most player-visible CHOSEN stand-ins in the viewer

From `grep -n CHOSEN tools/sr3_viewer/viewer_playtest*` (about 450 hits; these are the ones a player can see or feel):
- Ambient car motion is a kinematic CHOSEN integrator: accel 3 m/s^2, friction decel 5 m/s^2, no damage (`vp_frame_traffic.cpp:337,459-475`).
- Collision shapes and response: one plan-view box per car, ped radius 0.35 m, restitution 0.3, ped knockdown 2.5 s, 80 kg ped body (`vp_frame_collision.cpp:26-37,135`).
- Vehicle exit position: lateral offset (`vp_frame_input.cpp:576`); chase camera reference point (`vp_frame_player.cpp:762`).
- Jump height 1.0 m (`include/sr3player/jump_crouch.h:33`); all keys CHOSEN (`vp_frame_input.cpp:226`).
- Despawn fade counted not drawn (`vp_frame_traffic.cpp:230`); all cars obey lights regardless of mode (`:553`).
- Police unit counts, spawn rings, car crew, direct steering, stand-down, bust ragdoll gate (`vp_frame_police.cpp:6-10,26-30,357-375`); crime team CHOSEN (`vp_frame_traffic.cpp:1225`).
- Ped wander fallback speed `kWanderWalkSpeedMsChosen` and 1.0 m arrival radius (`vp_frame_render.cpp:431`, `vp_frame_traffic.cpp:955`).
- Water plane for boats is a flat CHOSEN plane (`vp_setup_args.cpp:435`, spec §10.13.6 source OPEN); boat draft 0.3 m (`vp_craft.cpp:71`).
- Homie revive timer multiplier 1.0 and enemy range 60 m (`vp_frame_homies.cpp:82,325`).

Stale comments to refresh (no behaviour change): `vp_frame_traffic.cpp:106,774` still say hood->category "not wired" (it is, via `FindHood`); `vp_frame_render.cpp:153-154` says no weather schedule exists (it does); `vp_frame_player.cpp:516-518` says the fire sound is only logged; v0.15 NOTES says aircraft/boats "not yet" in sr3.exe but `--spawn-vehicle` exists in the viewer.
