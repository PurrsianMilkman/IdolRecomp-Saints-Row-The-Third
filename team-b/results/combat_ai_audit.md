# Combat and NPC AI audit (spec syncs 7591413, 5cf5540, 4c6ecc3, ec289ae, a166141, 09f8306)

Branch `feature/combat-ai`, 2026-10-15. Sources: `spec-ai-runtime.md` section 21 (combat and NPC behaviour, with the
2026-10-15 "closed" notes) and section 22 (mission-driving AI), `spec-conformance-review.md` (re-review of eefbbd9 and
review of e11f57f). Method: read the spec, grep and read the code; nothing was disassembled.

Key structural fact: the AI runtime libraries (`include/sr3airuntime/decision.h`, `perception.h`, `mode_table.h`,
`actions.h`, `cover_node_selector.h`, `line_of_sight.h`, `notoriety.h`) are complete, unit-tested modules
(`tests/synthetic_ai_runtime_test.cpp`, `synthetic_cover_node_selector_test.cpp`, `synthetic_notoriety_test.cpp`) but
**none of them is instantiated by the host or by `tools/sr3_viewer.cpp`**. The viewer wires only the population side
(`traffic_lane_follower.h`, `near_field_vehicle_population.h`, `near_field_ped_population.h`, `freeroam_population.h`,
`police_roadblock_spawn.h`). No NPC runs a decision loop in a playtest. Mission-visible NPC behaviour therefore comes
only from the Lua natives (`src/lua_spec_confirmed_stubs_*.cpp`) plus `EngineState` per-human update
(`src/lua_engine_callbacks.cpp`). Status words below: implemented (behaves per spec in the running host), partial
(library or counter exists, or only part is live), missing.

New in this branch: `include/sr3airuntime/combat_ai.h` (pure rules with the spec test vectors, `tests/combat_ai_test.cpp`)
and the host wiring in `src/lua_engine_callbacks.cpp` (`tickScriptedOrders`), `include/sr3luahost/character_state.h`
(`scriptedOrderTicket`).

### Finding that changed the stall picture: `thread_new` never started a mission function

`thread_new(name)` resolves `name` through system_lib.lua's `_GetAnyGlobalSilent`, which reads `rawget(_UGGlobals, k)`.
The engine rebinds `_UGGlobals = getfenv()` (spec-lua-bindings.md Sec14, the 6 `luaL_loadstring` sites) before any
script; this host never did, so `_UGGlobals` stayed an empty table and every `thread_new` of a mission-defined function
(for example `sh01_shaundi_hack_thread`, the wave threads of m13/m15) returned 65535 and no worker thread ever ran. Found
by dumping `sh01.lua` from the game archive (game data, allowed) and `SR3_STALL_EVAL`. Fixed by
`Host::applyDynamicGlobalsBootstrap` (a CHOSEN non-strict equivalent: `_GetAnyGlobalSilent` also returns a raw global),
applied in the playtest after the system_lib preload. Result: sh01 finishes cleanly in 86 s (was a 180 s stall at
sh01.lua:578). The engine's strict-globals metatable is deliberately not installed (it turns every unmodelled global read
into an error). `tools/lua_host_run.cpp` (the standalone drive) is unchanged.

## 1. NPC decision loop (spec 21.1)

| Behaviour | Status | Where |
|---|---|---|
| Per-frame decision step, gate, chooser, 17 goal tests, action table | partial: library only, never run in the host | `include/sr3airuntime/decision.h`, `src/ai_decision.cpp` |
| Mode table, request/apply/transition, finish routine | partial: library only | `include/sr3airuntime/mode_table.h`, `src/ai_mode_table.cpp` |
| 70 action routines | partial: library only | `include/sr3airuntime/actions.h`, `src/ai_actions.cpp` |
| Perception (lists, sense pass, thresholds, LOS cadence) | partial: library only | `include/sr3airuntime/perception.h`, `line_of_sight.h` |
| Advance step 6 weapon band (0x004e68e0 / 0x004df080) | partial: pure rule, new, not wired | `combat_ai.h` `WeaponBand`, `AdvanceBand` |
| Notoriety is not an AI input (negative) | implemented by omission: no AI code reads notoriety | n/a |

## 2. Target selection (21.2)

| Behaviour | Status | Where |
|---|---|---|
| No scoring chooser exists (spec negative) | implemented by omission | n/a |
| Target writers: damage/notice path, follower copy (2000 ms), human shield, scripted orders | missing in the host (no per-NPC target field is driven) | `CharacterState::enemyTargets` records `ai_add_enemy_target` only (`src/lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp`) |
| First-sight target writer (unprovoked NPC) | missing; the spec itself says OPEN (needs a live hook). Not invented | spec 21.2/21.7 |
| Headless stand-in: nearer-of-two-players target, 2000 ms cadence (21.9 recipe, CHOSEN) | partial: pure rule `NearerPlayerIndex`, `TargetCopyDue` only; no NPC consumes a target | `combat_ai.h` |
| Mission group spawns start with an empty target | implemented (nothing writes a target at `group_create`) | n/a |

## 3. Attack, cover, weapon use, accuracy

| Behaviour | Status | Where |
|---|---|---|
| Cover search (box, evaluator, scoring) | partial: library implements the 15.7 search; the 2026-10-15 closed evaluator/threat term is new in `combat_ai.h` (`CoverNodeInSearchBox`, `EvaluateCoverThreats`, `CoverThreatCost`), not wired | `include/sr3airuntime/cover_node_selector.h`, `combat_ai.h` |
| Cover timers (stay-down, find-new, pop-up, advance, x U(0.8,1.25)) | partial: pure rule, not wired | `combat_ai.h` `CoverTimerMs`, `CoverPopUpWindowMs`, `CoverAdvanceEligible` |
| `ai_do_scripted_take_cover` gate (0x00540b90) | implemented (counter, gate false only inside a stay-down window, never set by the host) | `src/lua_spec_confirmed_stubs_b21_wave2_sec17_18.cpp` |
| `ai_do_scripted_advance/rush/fire`, `ai_suggest_action`, `ai_set_in_scripted_cover` | partial: dispatch counters, no ticket, no behaviour | `src/lua_spec_confirmed_stubs_b03*.cpp`, `b14*.cpp`, `b21*.cpp` |
| NPC weapon use (firing, reload, grenades) | missing; no NPC fires. The spec gives no accuracy numbers (accuracy is not specified in 21) | none |
| Damage to NPCs (damage sink, `on_take_damage`, death chain) | implemented for script/harness-driven hits | `src/lua_engine_callbacks.cpp` `applyDamageModelHit`, `src/lua_engine_state.cpp` |
| Kill-all completion (on_death hooks, no group-dead event) | implemented (per-member hook thread, ordering per Lua spec B) | `src/lua_engine_callbacks.cpp` `tickCharacterDeaths` |
| Headless "enemies die" stand-in for kill waits | implemented as a harness assist (CHOSEN, labelled) | `tools/sr3_viewer.cpp` "engine callback waits" under `--auto-objective` |

## 4. Flee, cower, surrender, hostage, downed

| Behaviour | Status | Where |
|---|---|---|
| Flee-rather-than-fight (0x004ead80, percent vs fraction) | partial: pure rule, new, not wired | `combat_ai.h` `FleeRatherThanFight` |
| Flee/cower chooser (0.8 / 0.5, cant_flee, cant_cower, always_cower) | partial: pure rule, new, not wired | `combat_ai.h` `ChooseFleeOrCower` |
| Flee stop rule (900 squared) | partial: pure rule, not wired | `combat_ai.h` `FleeFromStillActive` |
| `flee` native (script order) | partial: target resolved, order is a counter only (review B-1) | `src/lua_spec_confirmed_stubs_b27_wave2_spawn_world_audio.cpp` |
| Surrender / Holdup (mugging reaction, mugging_diversion.xtbl) | missing (spec FINAL; large, needs the player aim input and a cash path) | none |
| Vehicle hostage (mode 20) release rule | partial: pure rule `HostageEnter`/`HostageTick`, not wired | `combat_ai.h` |
| Human shield (grab, throw) | partial: state natives; the grab itself is a harness assist | `src/lua_spec_confirmed_stubs_b11*.cpp`, `tools/sr3_viewer.cpp` |
| Downed (Dbno) deadline revive (type 0), heal 0.25 / 1.0 | partial: pure rule `DownedDeadlineMs`, `DownedStandsUp`, `DownedReviveHeal`, not wired; nothing in the host downs a character (`lifeState` 6 is never written) | `combat_ai.h` |

## 5. Hostility, notoriety response

| Behaviour | Status | Where |
|---|---|---|
| Team hostility matrix (0x009b7100) and force flags | partial: `teamNameForId` names; `get_team` returns "Playas" only for `#PLAYER...` (review) ; no per-character team id, no matrix | `include/sr3luahost/engine_state.h` `teamNameForId`, `b16` |
| Notoriety tracking, crimes to points, decay, hostility flip | partial: library implemented and tested; natives set flags; the viewer holds notoriety at 0 (review F-7) | `include/sr3airuntime/notoriety.h`, `src/lua_spec_confirmed_stubs_b29*.cpp` |
| Notoriety drives spawn counts (police, roadblocks, free-roam density) | partial: formulas implemented (`freeroam_population.h`, `police_roadblock_spawn.h`); runtime input hardcoded 0 | `tools/sr3_viewer.cpp` ~23077 |

## 6. Mission natives driving NPCs (21.5, 22.11)

| Behaviour | Status | Where |
|---|---|---|
| Kind-3 ticket for `ai_do_scripted_move`, completion by the mode finish | **implemented in this branch** (was missing: review L-5 / `+0x4aa`) | `include/sr3airuntime/combat_ai.h` `ScriptedOrderTicket`, `CharacterState::scriptedOrderTicket`, `src/lua_spec_confirmed_stubs_b08_batch1_core.cpp` |
| Move arrival test A (1.414 m box test), no timeout, no failure code | **implemented in this branch** | `EngineState::tickScriptedOrders` (`src/lua_engine_callbacks.cpp`); the walk itself is a CHOSEN straight-line stand-in (5 m/s run, 1.5 m/s walk), no navmesh |
| `ai_scripted_action_is_complete`, `character_is_ready`, `ai_scripted_action_is_processing` read the ticket | implemented in this branch | `b06`, `b11` stubs, `CharacterState::scriptedLookupNonNull` |
| `ai_clear_scripted_action` frees the ticket and stops the move | implemented in this branch | `src/lua_spec_confirmed_stubs_b01_initial.cpp` |
| Tickets for advance / rush / take_cover / fire / human shield | missing: completion is by each mode's finish, no timing is specified | none |
| Mission worker threads started with `thread_new` run (hack thread, wave threads) | **implemented in this branch** (was missing for every mission function) | `include/sr3luahost/host.h` `applyDynamicGlobalsBootstrap`, `tools/sr3_viewer.cpp` |
| Failure modes (reactive mode replaces the order; give-up end leaves the ticket pending) | partial: `giveUp` modelled in the pure ticket; a dead character drops its order (CHOSEN) | `combat_ai.h`, `tickScriptedOrders` |

## 7. Mission-driving AI (spec 22)

| Behaviour | Status | Where |
|---|---|---|
| Chase (mode 4): planner, speed law, stand-off 4.0, 10 m hand-off, first-ram delays, end on target death | partial: `vehicle_chase` records the directive; the pure rules `ChaseHandsOffToOccupants`, `FirstRamDelay*`, `ChaseClassFromTeam` are new; no driving sim | `src/lua_spec_confirmed_stubs_b07_sec8_9_teamA.cpp`, `combat_ai.h` |
| Drive-to (`vehicle_pathfind_to_do`): arrival 3.0 m, stop-at-goal braking | partial: request + arrival test (`vehiclePathfindArrived`), completes immediately when positions are unknown; no vehicle motion except ambient lanes | `EngineState::vehiclePathfindCheckDoneCode`, `combat_ai.h` `PathfindArrived` |
| Waypoint directive (`vehicle_pathfind_navmesh_do`, mode 7): result 1.0 / 0.0 / 2.0 | implemented (result per a166141, commit bb92cfe); no motion | `src/lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp`, `combat_ai.h` `Waypoint*` |
| Follow (no native; stand-off law, lane car-following) | partial: lane car-following exists for ambient traffic only | `traffic_lane_follower.h` |
| Off-road / stuck give-up | missing (no mission-vehicle motion) | none |
| NPC enter/exit (`vehicle_enter_do`, seat record claim/pending/seated) | partial: instant commit with the enter/exit callbacks; `seat0Claim`/`seat0Pending` have no writer because the commit is instant (review B-3 not observable) | `commitVehicleSeat`, `commitVehicleExit` |
| Helicopter fly-to and follow (22.8, PARTIAL in spec) | missing | none |

## 8. Conformance-review findings addressed to our code

| Finding | Status |
|---|---|
| N-7 player on_death timer armed at the finalizer | **fixed** in this branch (`armPlayerDeathTimer`; test `combat_ai_host_test`) |
| N-9 on_take_damage skipped when max HP is 0 | **fixed** (all three sinks; test) |
| N-8 grab/melee/explosion callers do not pass 0x1/0x8/0x4000 | no engine-side caller exists yet (only Lua `character_damage` and `applyDamageModelHit`, both pass real flags); nothing to fix until a melee/explosion sink is added |
| N-10 | info, harmless |
| L-5 `+0x4aa` scripted ticket | **fixed for `ai_do_scripted_move`** (other orders: see section 6) |
| B-3 seat-0 claim / pending writers | not fixable meaningfully: the host commits seats instantly, so claim and pending are never observable between steps |
| L-2 `on_detection` no fire site | still open (needs the perception loop, not wired) |
| L-4 `character_is_ready` remaining AI-mode terms | still open (no AI modes tracked) |
| get_team per-character team id | still open |
| Freeroam F-1..F-10 (e11f57f) | not addressed: they need the real road/sidewalk graph (`sr3zone/traffic_graph.h`, spec 13.2a) in place of synthetic radial chords; F-9 is a spec ambiguity already clarified |
| Radio re-review (b6afc89) | not combat AI; nothing addressed to this task |

## 9. Counts

Behaviours audited (sections 1-7): 44 rows. implemented 13 (5 of them added in this branch), partial 24, missing 7.
