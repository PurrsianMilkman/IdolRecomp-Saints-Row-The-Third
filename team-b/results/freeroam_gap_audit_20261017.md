# Free-roam gap audit, 2026-10-17 (Team B, read-only)

Scope: what stands between `main` @ 96d9025 and a playable free roam via `sr3_viewer playtest` (live mode). Method: read code under
`tools/sr3_viewer/viewer_playtest_*.cpp`, `include/`, `src/`, the spec-*.md files, and `spec-conformance-review.md` (Team A's latest, up to
2026-10-17). Nothing was built, run or disassembled. Claims below were checked against code, not against comments. Paths are relative to the
repo root; `vp_` = `tools/sr3_viewer/viewer_playtest_`.

## 0. Headline

Today's playtest is a walk-and-drive sandbox with ambient cars and peds, not yet a game loop. The player cannot be hurt, cannot die, cannot
jump or crouch, and cannot take any vehicle except the single parked genki. Shooting a civilian has no consequence (no crime, no notoriety, no
police, no hostile peds). Ambient cars and peds are all the same model, drawn at the player's ground height. The biggest gaps are all
Team B-lane and mostly spec-ready: player damage/death, jump/crouch, ambient-vehicle entry, crime-to-notoriety-to-police, and spec-value
cameras. Stale text: the `NOT IN V0` banner at `vp_setup_args.cpp:513-516` still says ambient pedestrians are not wired (they are).

## 1. What a player can do today

### On-foot movement
- Walk/run: W/A/S/D polled via `GetAsyncKeyState` into the move seam (`vp_frame_input.cpp:163-166`); the real movement chain
  (`sr3player::PlayerMovement`) runs with a real collision hook (`vp_setup_world.cpp:673-682`).
- Sprint: Shift held (`vp_frame_input.cpp:167`), through the real sprint stance bits.
- Jump: NOT wired. The only keys read are W/A/S/D/E/T/R/comma/period/Shift/Esc/F10/LMB (`grep VK_` in `vp_frame_input.cpp`); no `VK_SPACE`.
  `character_collision.h:84-89` says the jump launch formula is known but unimplemented. `vp_setup_args.cpp:513` confirms "NOT IN V0: jump, crouch".
- Crouch, climb/vault, swim, cover, ragdoll-on-impact: not wired (no key, no code in `vp_*`; grep for crouch/climb/swim/cover finds nothing).
- Footsteps: real left/right-foot trigger frames drive a stand-in clip (`vp_frame_player.cpp:174-235`).
- Gamepad: none (keyboard and mouse only).

### On-foot combat
- Fire: LMB, edge-triggered only (`vp_frame_input.cpp:182`, `vp_frame_player.cpp:285-291`). One weapon only: the first pistol-class
  `weapons.xtbl` row (`viewer_playtest.cpp:269-272`). No reload, no weapon switch, no auto-fire, no aim/zoom, no ammo consumption visible in play.
- Hit path: real `WeaponInstance::fire()` against per-ped boxes, then the lethal sink into `EngineState::applyGuaranteedLethalHit()`
  (`vp_frame_player.cpp:342-355`). Peds die; there is no death animation or ragdoll draw (peds are drawn as static meshes, see world below).
- No gunshot, impact or reload sound: the only `playOneShot3D` caller is the footstep (`vp_frame_player.cpp:229`); `soundSelected` is
  only printed (`:344-346`).
- Melee: not wired (no key; spec §15.2 says resolution is OPEN).
- Player damage, death, respawn: not wired. Nothing in `vp_*` reduces player health (grep for hurt/damage/respawn/health finds only the HUD
  label and the `--auto-combat` harness, `vp_frame_render.cpp:405`, `vp_frame_mission.cpp:513`). Hostile mission NPCs can exist under
  `--mission=`, but a free-roam session has no hostile NPC source and no incoming damage path.

### Camera
- On-foot: real follow camera with mouse look (poll-and-recentre, `vp_frame_input.cpp:184-202`) and real swept-sphere camera collision
  (`vp_setup_world.cpp:355-357`).
- Vehicle: still the CHOSEN chase camera, rigidly locked to vehicle heading: `kVehCamDistance 9.0`, `kVehCamHeightAbovePivot 4.0`
  (`vp_session.h:109-110`), FOV `kVehCamFovY`, built at `vp_frame_player.cpp:567-587`. Spec §4.15.6 gives FINAL values (7.5 m, 0.7 m, 50-75
  degree speed FOV, follow swing) that have not been applied. Camera collision while driving: not checked (the eye is computed directly, no
  sweep at `:567-570`).
- Death camera (mode 9), shoulder/aim camera, stunt/nitrous cams: not wired.

### Vehicles
- Enter/exit: E within `kVehicleEnterRadius` of the ONE parked genki (`vp_frame_input.cpp:294-326`). Exit places the player at a lateral
  offset (CHOSEN, not door-based). No enter/exit animation, no carjack, no entering ambient or police cars.
- Drive: planar 13-sub-step simulator (`VehicleSubstepSimulator`, no vertical state, no roll/pitch), W/S/A/D, real world-collision stand-in
  (bounding sphere, `vehicle_world_collision.h`). No vehicle-vs-vehicle, vehicle-vs-ped, or vehicle damage (`vp_setup_args.cpp:523`;
  `results/freeroam_audit_vehicle_physics.md` rows 14, 23, 24).
- Only `car_4dr_genki` is ever spawned for the player (`results/freeroam_audit_vehicle_physics.md` row 25); no handbrake, horn, headlights.
- Radio: `,`/`.` cycle stations while driving (`vp_frame_input.cpp:331-345`), real station roster and playback per the audio audit and
  the radio review. No engine, horn, tyre or collision sound.

### World
- Streaming: real cell/ground-tile reconcile with LOAD/EVICT (`vp_setup_world.cpp:416-458`), spec-world-streaming level-4 window.
- Collision: static `.clmesh_pc` props via a triangle collision world, plus street surface from the ground-tile collision BVH
  (`vp_setup_world.cpp:349-357`, `:486-540`). Ground Y is sampled from heightmap/BVH for the player; there are no dynamic props, no
  destructibles, no water volumes, no swim state.
- Time of day: live TOD clock with T key skip (`vp_frame_input.cpp:283-292`) and per-frame lighting blend.
- Weather: a fixed stage, `kWeatherStageForPlaytest = "Light Rain"` (`vp_session.h:290`). No weather schedule, no precipitation FX.
- Zone interior streaming (0x2237/.czn_pc) is Team A's lane (hold lifted); not assessed.

### Roads and traffic
- Ambient cars: always on, real road-graph spawn (130 m ring, cone, segment occupancy, highway rule) and lane following
  (`vp_frame_traffic.cpp:95-214`). Cruise follows `AmbientThrottleLaw` to 10 m/s (per the 2026-10-17 re-check RG-12 PARTIAL).
- Signals: the §20.6.13 controller ticks every frame (`vp_frame_traffic.cpp:35-52`) and cars stop for lights via `SignalRuleStops`
  (`:415`). Stop signs and the mode gate (`obey_traffic_lights`, mode 8/14) are not applied: the only `obey_traffic_lights` code is the Lua stub
  (`src/lua_spec_confirmed_stubs_b26_wave2_vehicles_helicopter.cpp:198`).
- Despawn: the camera-gated distance despawn is still switched off: `RunNearFieldVehicleMaintenancePass(..., /*runDespawn=*/false, ...)`
  at `vp_frame_traffic.cpp:181` (Team A's RF-1). A separate erase pass exists just above it (`:176-178`); recycling is by that rule only.
- All ambient cars draw with the player's genki mesh, at `finalGroundY` (the player's ground height), not at their own ground height
  (`vp_frame_render.cpp:274-295`). On a slope or on a different level they float or sink.
- Police roadblocks: spawn only when R is pressed (debug gate, `vp_frame_input.cpp:177`, `:347`), not tied to notoriety.

### Pedestrians
- Ambient peds: near-field population with real sidewalk graph, A* navigator, crossing gate, wander, and gunfire flee/cower
  (`vp_frame_traffic.cpp:467-975`). A per-NPC AI loop runs for them (`:614`).
- Drawn as copies of the player's skinned character mesh at `finalGroundY` with heading 0 and no per-ped animation or facing
  (`vp_frame_render.cpp:296-326`); a walking ped slides sideways. Walk speed is a constant 1.4 m/s (`near_field_ped_population.h:809`).
- Hood-to-category is wired (`vp_setup_assets.cpp:297`, `FindHood`), but PedDensity uses the fixed Light Rain value, so the ped target is
  about half of the clear-sky spec value (RG-10).
- Peds do not react to the player's vehicle, melee, or being run over. Ped line-of-sight is unset (AI-3: `npcAiLineOfSight` is declared
  in `include/sr3luahost/engine_state.h:93` and read in `src/lua_engine_npc_ai.cpp:198`, but nothing in tools assigns it).

### Police and notoriety
- `liveNotorietyLevel` reads the alias tracks (`vp_setup_sim.cpp:37-45`); the only writer in the viewer is `--debug-notoriety=N`
  (`:32-35`). No crime source feeds it: `NotorietyDriver` (`include/sr3airuntime/notoriety.h:413`) is referenced only by that header and
  tests (grep over `tools`, `src`). So shooting, killing, or ramming a civilian never raises heat; no police respond; no pursuit (mode 4
  chase rules are library-only, `combat_ai.h:307-316`).
- Notoriety does scale population density when set via the debug flag.

### Radio and audio
- Radio: working (see above). Footsteps only for SFX. No gunfire, vehicle, ambient, or impact audio in the playtest.

### Systems outside Team B's lane (where they plug in)
- HUD/minimap: `--hud` overlay exists (health/ammo/money/notoriety, minimap); Team C owns its completion.
- Save/load, respect/progression: Team C libraries (`sr3save*`, `sr3progression`); no playtest hook needed for free roam.
- Cutscene playback: Team C (`sr3cutsceneplay`); free roam does not need it.

## 2. Ranked gap table

Ranked by player impact x spec readiness. Spec status uses the spec's own labels.

| # | Gap | Area | Impact | Spec support | Size | Notes |
|---|---|---|---|---|---|---|
| 1 | No player damage, death, or respawn | combat | high | Death trigger `0x00702810` and `respawn_delay_ms` CONFIRMED (spec-player-runtime §15.4, spec-tables-progression §10.2); generic damage/lethal path §15.3a CONFIRMED; **free-roam respawn destination OPEN** (§15.4 "whether free-roam death follows a different path") | M | Build damage intake plus death state (controls-disabled flag, death cam mode 9, `respawn_delay_ms`) now; respawn target needs Team A (see 3.1 question) |
| 2 | Jump and crouch missing | movement | high | Jump input path §3.6.4 CONFIRMED, launch formula §5.6.5 CONFIRMED (`speed = sqrt(2*g*h)`); crouch §3.6.3 CONFIRMED; **jump height arg `0x0098bea0` and crouch anim states unconfirmed** | M | Jump needs the height value; crouch needs capsule/state change. Check `spec-anim-runtime.md` §4.2 for "Jump Dive Bool" and crouch rows before starting |
| 3 | Cannot enter ambient cars (carjack, any road vehicle) | vehicles | high | Entry flow specced (spec-player-runtime §7-§14, CONFIRMED); vehicle data tables CONFIRMED | L | Needs per-vehicle model loading (gap 5) and generalising `PlaytestVehicleRig` from one genki. Enter/exit animation can stay CHOSEN-snap first |
| 4 | Crime to notoriety to police response absent | police | high | Notoriety rules §19.1-§19.8 FINAL (escalation recipe §19.8 FINAL 2026-10-12); roadblock tie-in §20.6.10 FINAL; chase §22.2 FINAL; **which in-world actions are crimes beyond the 5 confirmed activity records is partial** | L | Step 1 (S): wire attacks on peds/cars into `NotorietyDriver` using the confirmed `CrimeActivity` records. Step 2 (M): tie roadblock arm to level per §20.6.10 instead of R key. Step 3 (L): police chase AI |
| 5 | All ambient cars/peds same model, floating at player Y, peds have no heading/animation | world/NPCs | high (looks broken) | Per-ped ground height: ground query exists (`sampleRealGroundHeight`). Vehicle model choice §20.6.1(e) and ped character choice §20.6.2(e) **FINAL in spec-ai-runtime (20.6.x)**; ped locomotion clips §20.6.9(a) CONFIRMED | M | Quick win (S): sample ground per actor and set ped heading from movement. Model variety needs the vehicle/character resolver (M-L) |
| 6 | Vehicle camera not at spec values (9/4 m, fixed FOV, rigid heading) | camera | med-high | spec-player-runtime §4.15.2/§4.15.3/§4.15.6 FINAL | S-M | All constants are in a table in §4.15.6; add swing (§4.15.3) and speed FOV. Add camera sweep to the driving eye |
| 7 | No gunshot, impact, engine, horn, or collision sound | audio | med | Event/bank pipeline implemented (audio audit); `Sounds` events for weapons exist in `weapons.xtbl`; mapping to Wwise events partly **OPEN** (init.bnk attenuation not decoded) | M | Weapon fire sound selected by `fire()` is computed but never played; hook `playOneShot3D` |
| 8 | Vehicle-vs-vehicle, vehicle-vs-ped collision and vehicle damage | vehicles | high | spec-physics-runtime §10a collision CONFIRMED (response model partly); spec-vehicle-damage-runtime §1-§6 CONFIRMED structure | L | Run-over of peds is the cheapest visible piece (S-M) once ped boxes exist (already used for hitscan) |
| 9 | Weather: fixed Light Rain, no schedule | world | med | Stage machine spec-tables-environment §18 CONFIRMED structure; `Chance`/`Average_Duration`/`Variance` data CONFIRMED; blend formula CONFIRMED and already used | M | Switch default to Clear Skies (S, fixes RG-10 ped density) then add the schedule tick |
| 10 | Despawn schedule off (RF-1) and fades absent | traffic | med | §20.6.1(d) FINAL; Team A RF-1 | S | Pass `runDespawn=true` at `vp_frame_traffic.cpp:181` after verifying the camera gate; fades M |
| 11 | Stop signs, mode gate, junction yield absent | traffic | med | §20.6.13(f)/(g) FINAL | M | Needs the vehicle mode byte (ambient cars have none) and segment bit 7 |
| 12 | Ambient/hostile NPC line of sight unset (AI-3), no cover source (AI-7), downed facts never set (AI-11) | AI | med (matters once hostiles exist) | §21.1 FINAL, §21.3 FINAL, §21.10.1 FINAL | M | LOS: assign `npcAiLineOfSight` from the existing swept collision world. Cover: feed zone `cover_node` objects |
| 13 | Walk speed constant 1.4 m/s; per-ped variants | peds | low-med | §20.6.9(a) CONFIRMED (1.386 / 1.368 and variant table) | S | PN-4 |
| 14 | Car lane re-query 3 m vs spec 15 m; lookahead; brake decel fixed 8.0; include-mask not passed | traffic | low | §20.6.3(d), §20.6.7(a) FINAL | S | RG-12/RF-3/RG-3 |
| 15 | Melee | combat | med | Trigger CONFIRMED §3.6.6; damage/move selection **OPEN** (§15.2: boundary at `0x00985760`) | - | **Needs Team A**: which animation event applies melee damage, and the move-selection rule |
| 16 | Vault/climb, swim, cover, ladder | movement | med | Vault/ledge target finder `0x009660e0`, climb start `0x00989ed0` named but bodies not specced | - | **Needs Team A**: spec the ledge/vault probe and climb states. Swim needs water volume query (§20.6.8 lists `Water_nn` hood objects) |
| 17 | Weapon set: reload, switch, ammo, aim assist, scope | combat | med | Weapons runtime spec exists; aim-assist selector §15.1 CONFIRMED but steering half **OPEN** | M | Reload/switch/ammo are spec-ready in spec-weapons-runtime; aim assist partly OPEN |
| 18 | Player-vehicle exit/enter position and animation (CHOSEN offset) | vehicles | low | §6/§11 CONFIRMED | L | Defer behind gap 3 |

## 3. Top 6 recommended next tasks

### Task 1: Player damage, death and free-roam respawn skeleton
Goal: let the player take damage from NPC fire and falls, die, see the death state, and restart after `respawn_delay_ms`.
Spec: spec-player-runtime §15.3, §15.3a, §15.4; spec-tables-progression §10.2 (`Death_and_busted`); spec-ai-runtime §21.1 for NPC fire.
Files: new `PlayerVitals` helper in `include/sr3player/`; hook in `vp_frame_player.cpp` (damage intake) and `vp_frame_input.cpp` (controls-disabled flag).
Verify headlessly: `playtest --headless --no-input --script-fire` plus a new `--debug-damage-player N` flag; assert health goes to 0, controls flag set, and respawn timer fires at the table value; add a unit test for the timer.
Needs Team A (blocking only the respawn destination): "In free roam (no active mission), where does the player respawn after death: last checkpoint name, a hospital marker, or the mission-start fallback? Is there a 'busted' path for notoriety arrest?"

### Task 2: Jump and crouch
Goal: bind Space and C, implement jump launch in the character controller and crouch stance.
Spec: spec-player-runtime §3.6.3 (crouch, CONFIRMED), §3.6.4 (jump gate), §5.6.5 (launch formula `sqrt(2*|g|*h)`).
Files: `include/sr3player/character_collision.h` (the unimplemented jump at lines 84-89), `include/sr3player/movement.h`, `vp_frame_input.cpp`.
Verify headlessly: unit test the launch formula against the spec's gravity vectors; `--no-input` script frame that presses jump and asserts peak height and landing frame.
Needs Team A first (quick): "What height argument does the default jump pass to `0x0098bea0`, and what clips play for jump and crouch idle/walk?"

### Task 3: Fix actor placement and orientation (ambient cars and peds)
Goal: draw every ambient car and ped at its own ground height, with heading from its movement, and with distinct-per-id mesh later.
Spec: spec-ai-runtime §20.6.9(a) (ped clips and speeds), §20.6.1(e) and §20.6.2(e) (model choice).
Files: `vp_frame_render.cpp:274-326`, `vp_frame_traffic.cpp:743-950` (heading source), `sess.sampleRealGroundHeight`.
Verify headlessly: add a trace line with per-actor `y` and `heading`; assert `y` differs from `finalGroundY` on a sloped tile and ped heading follows velocity; compare golden stdout. Do not change golden images except via the existing golden script.

### Task 4: Spec-value vehicle camera
Goal: replace the CHOSEN 9 m / 4 m rigid chase camera with the §4.15.2/§4.15.3 camera (FD 7.5, FH 0.7, speed FOV 50-75, follow swing, pitch clamp [-35, 55]) plus a camera collision sweep.
Spec: spec-player-runtime §4.15.2, §4.15.3, §4.15.4 (test vectors), §4.15.6.
Files: `vp_session.h:109-112`, `vp_frame_player.cpp:443-590`, `src/player_follow_camera.cpp`.
Verify headlessly: unit test against §4.15.4 vectors (FOV at 30 m/s is 69.738 degrees); `--script-vehicle` run and compare eye-to-pivot distance and FOV in the trace log.

### Task 5: Crime to notoriety, then tie roadblocks to level
Goal: make attacks on peds, ramming, and killing raise heat through the confirmed `NotorietyDriver` rules; arm roadblocks from level rather than the R key.
Spec: spec-ai-runtime §19.2, §19.8 (FINAL recipe), §20.6.10 (roadblock timers, FINAL); `include/sr3airuntime/notoriety.h`.
Files: `include/sr3airuntime/notoriety.h`, `vp_setup_sim.cpp:37`, `vp_frame_player.cpp:342-420` (crime report from fire/kill), `vp_frame_input.cpp:347` (R gate), `police_roadblock_spawn.h:553`.
Verify headlessly: `--script-fire` kills a ped; assert police level rises and decays per table values; assert a roadblock slot fills only when level >= 1 and the player is driving. Step-1 only needs the five confirmed `CrimeActivity` records plus a CHOSEN "attack civilian" record, labelled as CHOSEN.
Needs Team A (for the remainder): "Which `CrimeActivity` record does shooting or killing a civilian, or hitting a pedestrian with a vehicle, report, and does the cop-witness test use line of sight?"

### Task 6: Weapon sounds, plus Clear Skies default and despawn on
Goal: three small fidelity fixes bundled as one task. (a) Play the fire sound that `fire()` already selects. (b) Default the playtest weather stage to the game-start stage, fixing the ped density (RG-10). (c) Pass `runDespawn=true` and wire the camera gate (RF-1).
Spec: spec-weapons-runtime §2.2c/§4.4 (sound event), spec-ai-runtime §20.6.0(d), §20.6.1(d), spec-tables-environment §2.
Files: `vp_frame_player.cpp:344`, `vp_session.h:290`, `vp_frame_traffic.cpp:181`.
Verify headlessly: `--script-fire` log shows an event played; population log shows Default Spawn target 32 peds at 14:00; despawn counter increases after a scripted long drive.

## 4. Observations that are not tasks yet

- The `NOT IN V0` banner (`vp_setup_args.cpp:513`) and the doc comment (`vp_support.cpp:108-121`) are stale and should be refreshed when Task 1 lands.
- Team A review items still open in code (checked 2026-10-17): RG-3 (include mask built but never passed), RG-12 (`traffic_lane_follower.h:773` re-query 3.0), RG-13/RF-2 (stop signs, mode gate), RF-1, RF-3, AI-3, AI-7, AI-11, AI-12, AI-15, AI-16, PN-4. Fixed since the review: signal controller wired (`vp_frame_traffic.cpp:35`, `:415`), hood lookup wired, ped navigator wired.
- Ambient peds and cars share one skinned vertex buffer with the player's mesh (`vp_frame_player.cpp:143-153` reuploads it each frame), so any per-ped animation needs separate buffers or a GPU-skinned path.
