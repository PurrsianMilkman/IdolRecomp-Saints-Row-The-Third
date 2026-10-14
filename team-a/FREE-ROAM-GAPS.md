# Free-roam gaps: what Team A must spec next (2026-10-14)

Scope: a playable free roam. That means driving around the city with traffic and peds, HUD + minimap, radio, cutscenes, save/load and respect/level-up.
Source: the CHOSEN markers in Team B's primary checkout (`build/` and worktrees skipped), the subsystem-existence greps, and the spec-*.md coverage greps.
Counts are grep line counts of the word CHOSEN. Many are prose lines inside comment blocks, so treat them as relative weight, not exact guess counts.
Paths are relative to `TEAM B\`.

## Status 2026-10-14: every group below now has a FINAL spec section (Team B: start here)

| Group | Spec section (all with test vectors + a "Team B wiring" table) | Still OPEN (small) |
|---|---|---|
| B road graph | spec-zone-data-format §13.2a: one city graph in `sr3_city~fawld.czn_pc`, scale 0.0625, shape points 6 B relative to node A | some segment flag bit names |
| B population | spec-ai-runtime §20.6 (+ §20.6.7–20.6.12: follow law, hoods → sp_cat, ped speeds, roadblocks, stop line); §20.6.6 = wiring table | lane-change occupancy check, flee state choice |
| A vehicle physics | spec-physics-runtime §10.11 (7 Havok slots, suspension/sag, inertia, friction curve, steering); wiring §10.11.12 | rim choice, Maximum_Speed limiting, 0-60/braking need live capture |
| C HUD + minimap | spec-hud-runtime "Free-roam HUD binding map and minimap transform"; wiring H.5 | interior minimap placement, mask size |
| D radio | spec-audio-runtime §14.6; wiring §14.6.5 | ducking (init.bnk) |
| H camera | spec-player-runtime §4.15 | swing reference vector, stunt-cam ramps |
| F save/load | spec-save-format "Free-roam load and save flow" | starting cash, `tss01`, crib handle → name |
| G respect | spec-tables-progression "Free-roam respect grants and level-up flow" | exploration/flashpoint delivery |
| E cutscenes | spec-cutscene-camera-format "In-game cutscene playback, end to end" | in-scene (non-fullscreen) movie draw |
| (fix) weapons | weapon +0x1A4 = clip, +0x1AC = reserve (spec-weapons-runtime §2.2c corrected) | — |

The ranked table below is the original 2026-10-14 gap analysis, kept for reference.

## Ranked summary

| Rank | Group | CHOSEN lines | Does the system exist in Team B? | Spec status | Why it blocks free roam |
|---|---|---|---|---|---|
| 1 | B. Ped + traffic population | ~136 (ai headers 116 + viewer ~20) | Yes, but it spawns into a fake road network: the playtest has no real Traffic/Sidewalks graph, so it uses a synthetic annulus and fixture graph | **Thin**: road-graph point pool, quantisation constant, Sidewalks record, spawn-rate formula and model choice are all OPEN | Streets are not populated the way the game populates them; traffic cannot follow real roads |
| 2 | A. Driving feel / vehicle physics | ~184 (physics 162 + viewer ~22) | Yes: it drives, and engine, gears and grip come from the xtbl | **Mixed**: steering speed threshold and inertia specced but not wired; suspension, wheel geometry, tire curve and steering rates thin | Car handling is mostly guessed: suspension, tire law, wheel geometry |
| 3 | C. HUD + minimap | ~52 (stub files 40, mostly safety "HARDENED" guards; viewer 12) | Partly: real stat data feeds, but a synthetic text overlay instead of the real `hud.vint_doc`; minimap scale and layout guessed | **Thin**: which HUD element and dataitem carries each stat, and the minimap world-to-screen transform | No real HUD on screen; minimap scale/rotation is invented |
| 4 | D. Radio / music | 2 (audio stubs) | **No.** Only the station tables are parsed; no radio playback; the music engine backend is a null stub | Station switching specced (spec-audio-runtime §14); track selection OPEN (§14.5); song-to-Wwise playback path thin | Missing system, but free roam is playable without it |
| 5 | H. Camera (driving) | ~47 | On foot: the real follow camera. In a vehicle: a guessed chase cam (9 m back, 4 m up) | **Partly specced**: spec-player-runtime §4.12 exists but Team B has not wired it; follow distance/height sets OPEN (item 28) | Driving feel depends on it |
| 6 | F. Save / load | 0 | Snapshot parse + build, savedir and a Lua bridge exist; **not wired into the playtest** | **Spec exists** (format, triggers §15, write/read symmetry §16) | Integration work for Team B; one spec gap: where the player spawns after a load |
| 7 | G. Respect / level-up | 0 | `awardRespect` and the level-up exist in the Lua host, but the table is test-supplied (real `respect_levels.xtbl` not loaded); no free-roam source grants respect | **Mostly specced** (tables-progression §5.4); free-roam grant paths (stunts, mugging, exploration) OPEN | Progression is invisible in free roam |
| 8 | E. Cutscene playback | ~9 | Lifecycle state machine + scene table + `.csc_pc` camera sampler exist; playtest auto-skips cutscenes; Bink decoder only in tools, `bink_play` is a stub | Lifecycle and camera specced; actor animation, soundtrack sync and Bink audio thin | Not needed to roam |
| 9 | I. Other (TOD lighting, notoriety, roadblocks) | ~54 | Yes | Mixed | Cosmetic or secondary |

## (B) Ped + traffic density/spawning: ~136 CHOSEN

The biggest finding: the playtest has no real road or sidewalk graph. A Traffic (0x2236) record reader exists (`include\sr3zone\traffic_graph.h`), but the point-pool element shape is guessed and the playtest feeds it a synthetic fixture. No Sidewalks (0x2238) reader exists at all.

| Where | What was guessed | Value | Spec |
|---|---|---|---|
| `include\sr3zone\traffic_graph.h:167` | Point-pool element stride | 12 (float3) | spec-zone-data-format §13.2: **OPEN** (thin) |
| `include\sr3zone\traffic_graph.h:174` | Quantisation scale (constant at 0x0115a220) | 1.0 placeholder | §13.2 says "value not decoded": **thin** |
| `tools\sr3_viewer.cpp:18170-18172` | Near-field vehicle spawn candidates (annulus around the player, heading-biased) instead of the real graph | 25-90 m, ±70° | spec-ai-runtime §17.4c: candidate generation **OPEN** |
| `tools\sr3_viewer.cpp:18241-18243` | Ped spawn ring and stand-in sidewalk segments | 90.5-160 m, 6 m half-segment | §20.1: far reject has **no literal** (thin) |
| `include\sr3airuntime\near_field_vehicle_population.h:125` | Spawn rate | 1 attempt per tick | §17.4c: formula at 0x00910940 (view fraction + 2 clamps) **not given** |
| `include\sr3airuntime\near_field_ped_population.h:190-222` | Difficulty gate, far reject, ground check, sideways offset | pass / off / pass / 0.0 | §20.1 names them, gives no values: **thin** |
| `include\sr3airuntime\near_field_ped_population.h:250` | Visibility test (FOV cone) | 50° half-angle | §20.1: frustum params OPEN |
| `include\sr3airuntime\near_field_ped_population.h:787,797` | Flee / wander walk speed | 5.0 / 1.4 m/s | Not specced (likely animation-driven): **thin** |
| `include\sr3airuntime\ambient_population.h:63-110` | Ped type-slot weight formula, segment pick, slot sort key, despawn OR rule, vehicle type round-robin | see items 2-8 | §17.3/§17.4 give the shapes only: **thin** |
| `include\sr3airuntime\traffic_lane_follower.h:743-756` | Arrival radius, lookahead, re-query threshold (constant at 0x01117a60), cruise throttle | 1.5 / 15 / 3.0 / 0.5 | §18.1/§18.5/§18.6: mechanism CONFIRMED, **values not published** |

Spec exists but Team B has not wired it:
- Density targets 75/75 and refill caps 25/20 (§17.2).
- The near-field cap of 48.
- The `distant_*_spawn_parameters` fields (tables-traffic-ai §14.3).
- The `generic_characters`/`generic_vehicles` slot maps (tables-traffic-ai §12).
- The playtest's spawned car is always `car_4dr_genki_0.ccar_pc` (the run-playtest code near `tools\sr3_viewer.cpp:19414`).

## (A) Driving feel / vehicle physics: ~184 CHOSEN

| Where | What was guessed | Value | Spec |
|---|---|---|---|
| `include\sr3physics\vehicle_substeps.h:176-180` | Wheelbase, track, CG height, yaw inertia, wheel radius | 2.6 m, 1.6 m, 0.5 m, 2200 kg·m², 0.32 m | Wheel positions should come from rig bones `PD_wheel_*` (spec-vehicle-geometry), but no spec states how. `Yaw_Unit_Inertia` → inertia tensor (physics-runtime §10.10) is "box shape", formula not given: **thin** |
| `include\sr3physics\vehicle_substeps.h:185` | Steering speed-falloff threshold | 8.0 m/s | **Spec exists**: `Max_Speed_Steering` (+0x5C4, mph→m/s) → steering +0xc (§10.10). **Team B has not wired it** |
| `include\sr3physics\vehicle_substeps.h:183-184` | Steering turn-in and centring rates | 3.0 / 6.0 rad/s | §10.1 confirms the two rate pairs; the source fields (`Steering_Wheel_Max_Speed`/`_Max_Return_Speed`?) are not mapped: **thin** |
| `include\sr3physics\vehicle_driving_slice.h:296-300` | Suspension spring, rebound damper, compression damper, rest length, load factor | 30000, 2500, 3500, 0.3, 1.0 | §10.4 gives the force law. The axle fields exist (`Wheel_Spring_Length/Strength/Max_Force`, `Wheel_Damp_Comp/Exp`, vehicle-data §7.3 +0x20..+0x30), but **no xtbl → suspension component construction mapping**: **thin** |
| `include\sr3physics\vehicle_substeps.h:277`, `src\physics_vehicle_substeps.cpp:61` | Tire force law (linear) and cornering stiffness | 9000 N/rad | §10.8 D-U1 slip solve is still partly HYPOTHESIS: **thin** |
| `include\sr3physics\vehicle_driving_slice.h:620-622` | 16-entry friction-slip curve, boundary, multiplier | all 1.0 / 0 / 1 | §10.8 OPEN: **thin** |
| `include\sr3physics\vehicle_driving_slice.h:235-237` | Shift duration, up/down cooldown | 0.3 / 0.5 / 0.5 s | `Clutch_Delay` is wired; cooldowns are not specced: thin |
| `include\sr3physics\vehicle_substeps.h:253` | Downshift margin when `Downshift_RPMs` is absent | 2000 rpm | §10.10: HIGH-confidence copy to +0x34 |
| `src\physics_vehicle_substeps.cpp:751-754` | Clutch-slip escape hacks | 50 rpm, 0.3 m/s², 8000 rpm/s | Harness only; should disappear once the friction curve is real |
| `include\sr3physics\vehicle_substeps.h:112-118` | Planar chassis integration (no pitch/roll) | n/a | `Roll/Pitch_Torque_Factor` fields exist; consumer not specced |

The engine torque curve, gear ratios, `Upshift_RPM`, `Clutch_Delay`, `Maximum_RPM_Rate`, `Wheel_Friction`/`Max_Friction`, braking torque, drag and mass are already wired from the spec (§10.2/§10.3/§10.10).

## (C) HUD + minimap: ~52 CHOSEN

| Where | What was guessed | Value | Spec |
|---|---|---|---|
| `tools\sr3_viewer.cpp:16968-16986` | The whole HUD is a hand-built document (4 text objects + 2 rects), not `hud.vint_doc` | layout invented | spec-hud-runtime §1-§5: the stat sources are confirmed, but **which hud element and dataitem shows each stat is not traced**: thin |
| `tools\sr3_viewer.cpp:17669-17670` | Minimap pixels per world unit | 0.12 × zoom | §6/§6.4: transform **not given** |
| `tools\sr3_viewer.cpp:17673-17676` | Minimap screen box | (1050,10)-(1250,210) | Should come from the `hud.vint_doc` element: not specced |
| `tools\sr3_viewer.cpp:17650` | Zoom default | 1.0 | sr2_map `zoom` property confirmed; default not given |
| `tools\sr3_viewer.cpp:17770` | Base-map tile binding (flat-colour fallback) | n/a | §6.3: runtime bind **OPEN** |
| `tools\sr3_viewer.cpp:17420` | Player glyph draw | stand-in | §6.4: final draw call OPEN |
| `src\lua_spec_confirmed_stubs_b20_*.cpp:75,174,344,388,573` and `src\lua_spec_confirmed_stubs_b09_*.cpp:454-645` | Out-of-bounds guards ("HARDENED") | n/a | Deliberate safety divergences, **not gaps** |

Spec exists but Team B has not wired it: the dataitem/datagroup Lua natives (spec-lua-bindings §18-§21), the pause-map subscription chain (cellphone-menu-runtime), the live minimap transform (Team B already has `minimap_live_transform.h`), and the cash/HP/notoriety/clip sources (hud-runtime §7 closures).

## (D) Radio / audio playback: 2 CHOSEN, system absent

- `src\lua_spec_confirmed_stubs_b09_batch2_hud_audio.cpp:396,414`: the persona-line and ambient-emitter natives always return 0, because no producer ever fills their tables.
- `src\wwise_backend.cpp:59`: the music engine is a null backend. There are zero radio references in the playtest. Real audio output exists (footsteps).

Spec status:
- Data tables: complete (tables-audio-radio §11/§12).
- Station switching, input, OFF state and save entries: complete (audio-runtime §14, save §12.8).
- **Thin**: which song plays and how it advances (song manager, §14.5 OPEN).
- **Thin**: how a station/track becomes a Wwise event or stream (streamed `.wem`/bank, the event ID per track) and the crossfade float triple (§14.2 item 6).
- **Thin**: DJ/commercial interleave (save entry 16 meaning HIGH only).

## (E) Cutscene playback: ~9 CHOSEN

- `tools\sr3_viewer.cpp:19373`: the playtest auto-skips via the real `skip_all_cutscenes` option.
- `src\lua_cutscene.cpp:19`: frame-order choice. The soundtrack end is an OPEN test value.

Spec status:
- Specced and implemented: the 20-state machine (lua-api-behaviour §26.25/§57.2a) and the `.csc_pc` camera sampler (cutscene-camera-format §10).
- **Thin**: how a scene's `.cte_xtbl` binds actors to animation clips, the cutscene soundtrack event, and lighting sets.
- **Thin**: in-game Bink playback. The video codec is fully specced but used only by tools; Bink audio and the frame-pump-to-screen path (bink-boundary §4) are not wired; `bink_play` is a stub.

## (F) Save/load: 0 CHOSEN, spec exists, Team B has not wired it

- Team B has: `src\save_snapshot.cpp` (parse + build), `save_directory.cpp`, `lua_save_game_bridge.cpp` and the `tools\save_browser.cpp` tool. The playtest never loads or writes a save.
- Spec: format, CRC, region map, triggers (§15: menu, autosave, checkpoint), write/read symmetry (§16).
- **One thin item**: the player is not stored in the save (§6.6 note). The spawn point after a load (crib? district default?) is not traced.

## (G) Respect / level-up: 0 CHOSEN, partly wired

- `include\sr3luahost\engine_state.h:7067` `awardRespect` + `applyRespectLevelUp` follow tables-progression §5.4.
- But (`:104`) the subsystem "never reads a real respect_levels.xtbl". The table is test-supplied, although `sr3tables_progression` already parses it.
- No free-roam respect source exists in the playtest.
- **Thin**: grant paths for stunt-jump, mugging and exploration (activity-runtime §4/§452-463 OPEN), and the HUD "+N Respect" pop element.

## (H) Camera: ~47 CHOSEN

| Where | What was guessed | Value | Spec |
|---|---|---|---|
| `tools\sr3_viewer.cpp:14579-14582` (reused by the playtest, `:17951`) | Driving chase cam distance, height, pivot, FOV | 9 / 4 / 1 world units, 0.8 rad | player-runtime §4.12: submodes 3-11, swing/slerp/FOV fields **specced, Team B has not wired them**; distance/height sets (+0x628/+0x648) **OPEN 28** |
| `include\sr3player\camera.h:79` | Rotation axis order | convention | not given |
| `src\player_follow_camera.cpp:61,165,382,412` | Small readings of reset order and blend | n/a | §4.7/§4.8 minor ambiguities |

## (I) Other: ~54 CHOSEN

| Where | What was guessed | Value | Spec |
|---|---|---|---|
| `include\sr3render\tod_lighting.h:187,218` | Sun direction (day arc) | (0.3,0.5,-0.8) default | tables-environment: no sun-direction field; **thin** |
| `include\sr3render\tod_lighting.h:215-217` | Fog → clear colour, TOD light → sun colour mapping | n/a | thin |
| `include\sr3airuntime\notoriety.h:270-271` | `Decay_Rate` as per-tick amount; `First_Decay` reused as the recurring interval | n/a | ai-runtime §19.3 ambiguous |
| `include\sr3airuntime\notoriety.h:327-346` | Gang/police windows for crimes quoted on one side only | [0,5] | §14.6 partial |
| `include\sr3airuntime\police_roadblock_spawn.h:308,332-333` | Roadblock forward offset, generic car, case-4 prop | 50 m, `police_roadblock_car`, `roadblock_barrier` | §19.7: row identity OPEN |

## Top-3 spec deliverables

### 1. Populated streets (group B)

**B-1. Road and sidewalk graph, byte-complete** (extend spec-zone-data-format §13.2). Cover:
- Traffic 0x2236: the point-pool element layout and stride, and the value of the quantisation constant at 0x0115a220.
- Per-segment fields: lanes, direction, speed class, `Spline_Type` mask.
- How segments join at junctions (real adjacency, not geometric).
- The Sidewalks 0x2238 node and segment records, including the 24-byte segment and the Traffic cross-link.

*Test vector*, for 2 named city `.czn_pc` files (one of them the default-spawn tile):
- segment count, point count, and the decoded world endpoints of segments 0-2;
- total segment length;
- sidewalk node count and the first 3 node positions;
- the neighbour list of one junction.

Team B's reader must reproduce these exactly.

**B-2. Spawn and drive constants table**, one row per constant, with value + units:
- near-field ped far-reject distance, sideways offset and difficulty gate;
- the camera visibility test parameters;
- the near-field vehicle spawn-rate formula (0x00910940) with its 2 clamps;
- near-field candidate generation;
- lane-follow lookahead, arrival radius, re-query threshold (0x01117a60) and the 3.5 throttle literal with its units;
- the ped walk/flee speed source.

*Test vector*: player standing still vs. driving at 20 m/s on a known tile. Give the expected live near-field vehicle count after 30 s (toward 48), distant counts (75/75), spawn attempts per second, and the minimum spawn distance observed.

**B-3. Ambient model selection.** How a spawned ped or car picks its character or vehicle definition: the ped type-slot weight formula, the vehicle type choice before `Spline_Type` gating, and any district or time-of-day input.

*Test vector*: the real `distant_peds`/`generic_*` rows give expected per-slot weights. For district X at 14:00, give the expected name distribution over 1000 seeded picks, or the deterministic weight table.

### 2. Driving (group A)

**A-1. Suspension construction.** Map `Wheel_Spring_Length/Strength/Max_Force`, `Wheel_Damp_Comp/Exp`, `Ramp_Damping_Comp` and `Wheel_Min/Max_Limit` to the per-wheel {spring, damperRelaxation, damperCompression, restLength, loadFactor}. Include unit conversions and the 1/X divisor (chassis +0xcc).

*Test vector*: the real `car_4dr_genki` axle values → the expected component values, and the static sag (m) at rest.

**A-2. Wheel geometry + inertia + steering rates.** Cover:
- where wheel ray start/end and wheel radius come from (rig `PD_wheel_*` bones or `.ccar_pc`);
- the `Roll/Pitch/Yaw_Unit_Inertia` → inertia-tensor formula;
- the source fields of the 2 steering rate pairs (§10.1).

*Test vector*: for genki, give the expected wheelbase, track, radius and inertia diagonal. Give the steering angle vs. time for a full-lock step at 0 and 20 m/s.

**A-3. Tire friction curve.** The 16-entry D-U1 slip table values, plus the boundary/multiplier roles (§10.8).

*End-to-end acceptance vector* (needs a live-capture item; user approval required): log chassis velocity per frame for 3 cars (genki, a truck, a sports car). Measure top speed, 0-60 mph time, 50-0 braking distance and turning circle at 10 m/s, then compare against Team B's sim fed the same `_veh.xtbl` rows.

Also: send Team B a note that `Max_Speed_Steering` → steering +0xc is already specced (§10.10), and so is the vehicle camera (§4.12).

### 3. HUD + minimap (group C)

**C-1. HUD binding map.** For each free-roam HUD stat (health, cash, respect + level bar, notoriety, weapon/clip/reserve, "+N Respect" pops), give:
- the dataitem or datagroup name;
- the field index → meaning, as returned by `vint_dataitem_get`;
- the C producer and its update cadence;
- the `hud.vint_doc` element and the property the HUD Lua script writes.

*Test vector*: player state {HP 150/200, cash $1234, respect level 3 at 40%, police notoriety 2.5} → the exact dataitem arrays and the resulting element property values.

**C-2. Minimap transform.** Cover:
- the world-to-minimap scale per `zoom`;
- the rotation rule (already confirmed, §6.4);
- the screen rect from `hud.vint_doc`;
- tile-texture selection and UV mapping (`minimap_CCRR`, the §6.3 bind);
- blip placement and clamping at the edge.

*Test vector*: player at world (x,z), heading 90°, zoom 1 → the expected tile id and UV centre, and the on-screen pixel position of a blip 100 m north.

## Quick wins for Team B (spec already exists, no new spec needed)

1. Wire `Max_Speed_Steering`/`AI_Max_Speed_Steering` into the steering threshold (physics-runtime §10.10).
2. Replace the chase cam with the mode-0 vehicle submodes (player-runtime §4.12).
3. Load the real `respect_levels.xtbl` into `respectLevelTable()` (tables-progression §5).
4. Wire save load/save into the playtest (save-format §15/§16; the reader and builder already exist).
5. Use the generic slot maps and density constants (tables-traffic-ai §12/§14, ai-runtime §17.2).
