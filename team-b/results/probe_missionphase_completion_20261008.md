# Real mission-phase machine + completion-flag writer, and the honest FINISH count (2026-10-08)

Implements `spec-lua-api-behaviour.md` Sec21.27a (the `0x014c7a14` phase machine) and Sec21.27b
(the real end-of-mission dispatcher / completion-flag writer, correcting Sec15.10), per the
orchestrator's own task brief. Re-runs the established mission-drive probe (`lua_host_run.exe
<real cache> tools/lua_all_registered_1490_tagged.txt <out> --tick-budget=721
--probe-unresolved-names`, byte-identical invocation to every prior probe doc's own header) and
measures, for the first time, how many of the 49 missions' own mission handles actually reach
the real completion flag (`+0x88` bit `0x4`) - not just "ran without an OPEN-state error." Raw
output: `results/verify_probe_missionphase_completion_20261008/`.

## What changed

**The phase machine (Sec21.27a).** `EngineState::MissionContext` previously modeled only a
boolean facade (`activeAndRunning`, `!= 8`) - a simplified stand-in this project's own prior
session already used to fix a 12-mission blocker. That facade is kept EXACTLY as-is (a couple
dozen existing tests `.set()` it directly) and is now backed by a real integer `phase` field
(0-8) with the real transition table Sec21.27a gives: `EngineState::bindMissionPhaseMachine()`
drives 8 -> (2, fresh+named only, arming the mission-start marker) -> 1 -> (3 or 5, keyed by the
bound mission's own TYPE field, `+0xa0`, 0-3 - CHOSEN default 0, no real per-mission population
table exists in this project for that field). `EngineState::setMissionPhase()` implements the
real general-purpose setter's own case-1 self-loop (phase 1 is "never actually observable as a
steady value," per that section's own text). `beginMissionPhaseTracking()` (the one hook
`tools/lua_host_run.cpp`'s per-mission loop already called) now also calls this real machine,
passing each mission's own name/CHOSEN type 0/`isRestart=false`.

**The completion-flag writer (Sec21.27b, correcting Sec15.10).** `EngineState::
writeMissionCompletionFlagBits()` implements the real, single, unconditional write Sec21.27b
found: `byte = (byte & 0xf7) | 0x44` on the mission handle's own `+0x88` byte. Wired from the
existing `mission_set_completed` native (`recordMissionSetCompleted`, now unconditional per the
real writer's own unconditional nature, same observable result as the old idempotent guard) and
from `EngineState::missionEndDispatch()` (the new `0x006d9f70` end-of-mission super-dispatcher)
type-1/type-2 success branches. `writeMissionUnlockedFlagBit()` is the matching bit-`0x1` writer
(`mission_unlock`'s own setter). **Fixed a real pre-existing defect this task's own brief flagged
by name:** `missionUnlocked_`, `missionCompletedMap_`, and `missionComplete_` (the OpenValueMap
`cell_is_mission_complete`/`mission_is_complete` read, Sec27.2/Sec20.14) were three separate,
never-cross-wired maps standing in for ONE real byte - exactly the invented-parallel-field
duplication Sec21.27b's own cross-validation (mission_unlock's bit `0x1` test on the SAME byte)
disproves. All three are now driven by the one real writer (`missionFlagBits_`), so a real
`mission_set_completed`/`mission_unlock`/`missionEndDispatch` call now makes `cell_is_mission_
complete`/`mission_is_complete` see it directly, with no test needing to `.set()` the facade by
hand. `missionCompletionFlagBitSet(name)` is the new direct accessor for "did this mission's own
`+0x4` bit get set" - what `tools/lua_host_run.cpp` now reads back per mission.

**The organic triggers (Sec21.27b).** Implemented the 4-way end-of-mission type switch
structurally in `missionEndDispatch()`: type 1 (completion writer + `mission_unlock`'s own
setter, gated on `successFlag`), type 2 (completion writer again; `0x006d9470` stays an OPEN,
counted stub - explicitly not decoded in the spec, not invented here), types 0/3 (a `+0x1f8`
script-hook ATTEMPT is counted; no real name table exists for that field, same gap as the
already-documented `+0x1f0`/`script_mission_start` real-name lookup). Wired the cleanest single
organic SUCCESS trigger Sec21.27b's own table names ("Success, unconditionally" for the
`0x006db7fa` "silent"-class case) as `missionSilentClass()`, a flat OPEN/test-settable resolver
(the same convention as `weaponDefinitionResolves()`/`lookupNotorietyTrackCode()`) fired from
inside `bindMissionPhaseMachine()` once phase settles - **a documented no-op for every one of the
real 49 probed missions today**, since no population table for which missions carry class bit
`0x20` exists anywhere in this project's clean-room sources. The other 4 organic call sites
Sec21.27b's own table lists (timeouts, cancel-warp sites, the caller-determined pass-through, the
fallback-path default) were left OPEN/unwired - they live inside `0x006db140`'s own per-tick
driver and `0x006da7b0`/`0x006dac44`, functions this project does not model at that level of
granularity, and inventing their trigger conditions would be exactly the kind of guess this
project's own discipline refuses.

## The headline number: 0/49, both before and after

**`missions_with_real_completion_flag_0x4_set=0/49`** (`results/verify_probe_missionphase_
completion_20261008/verdict_mission_drive_summary.txt`, line 20) - **not one of the 49 missions'
own mission handles reached the real completion flag** in this run, despite the writer now being
correctly wired end to end (confirmed directly: a synthetic unit test exercising `mission_set_
completed`/`missionEndDispatch`/`missionSilentClass` all land on the one shared `+0x88` field,
`tests/synthetic_luahost_trigger_mission_cluster_test.cpp`, all passing).

This is not a defect in the implementation - it is the SAME honest finding Sec21.27b's own text
states up front: no organic trigger is a real "objectives complete" check, and objective progress
lives entirely inside each mission's own Lua coroutine state. Checked directly against this run's
own hit log (`verdict_stub_hits_with_missions.tsv`): **`mission_set_completed` was called ZERO
times by any of the 49 missions' own scripts** within the 721-tick budget. Cross-checked against
the PRE-this-task baseline run (`results/verify_probe_blockers_ranked_20261008/verdict_stub_hits*.
tsv`, main repo): `mission_set_completed` was ALSO never called there - so the honest "before"
number for this exact metric is the same 0/49, not a regression, and not something this task's
own phase-machine/completion-writer scope could change on its own. Every one of the 49 missions,
run in this harness's own per-mission isolation, gets stuck on SOME OPEN engine-state gate (listed
per-mission in `verdict_mission_drive_summary.txt`) well before its own script logic ever reaches
a point where it would call `mission_set_completed` or trip an organic success dispatch - matching
this project's own "no invented fixes" discipline: forcing this number up would require fabricating
either a population table this project does not have, or gameplay progress the harness does not
simulate.

## What DID move: a new shared blocker now exposed, one layer deeper

Despite the FINISH count staying flat, the phase-machine fix is independently confirmed working:
**zero missions are now blocked by the old "mission context active, phase != 8" gate** (the
12-mission blocker a prior session's own simplified boolean fix already resolved, and this
task's real-phase upgrade preserves). 9 of those same missions (`dlc1_mm_01` through `dlc1_mm_06`,
`mm_p_02`, `mm_p_04`, `mm_p_05`) now progress one step further into their own `_start` script and
hit a DIFFERENT, previously-second-order OPEN gate instead: `MissionContext::currentMissionName`
(Sec1.5, `mission_end_failure`'s own identity-gate precondition) - the exact "fixing one blocker
exposes the next" pattern this project's own probe docs have repeatedly documented. This is a
genuine new finding, flagged here (not fixed - out of this task's own Sec21.27a/Sec21.27b scope)
for whoever picks up the mission-blocker ranking next. `stop_reason_histogram` also shifted
(`budget_exhaustion` 4->7, idle `completion` 45->42 vs. the pre-task baseline) - 3 missions that
previously went idle quickly are now being resumed every tick for the full budget, consistent
with (not proof of) deeper script progress from the real phase machine.

`missions_with_start_call_ok=46/49` is UNCHANGED from the pre-task baseline (the same 3
pre-existing, unrelated blockers: `dlc2_m01`/Sec28.12 second-coop-player, `mm_p_06`/Sec26.25
`zscene_prep`, `mm_m1_5`/Sec60.1 `customization_screen_is_ready`) - confirmed not a regression.
`stop_reason_histogram: error=0` is also unchanged (still zero unrecovered Lua-side crashes across
all 49 missions).

## Build/test

Fresh Release build (`cmake --build build --config Release -j 8`): 0 errors. Full `ctest
--test-dir build -C Release -j 4`: 141/141 passing (139 run + 2 pre-existing `real_bink_*`
skips, unrelated to this task - missing test fixture data). One transient failure
(`golden_scene_check`, a GPU/rendering golden-image test touching none of this task's files) on
the first `-j4` run was traced to first-run texture-cache cold-start time under parallel
contention (not a code defect): re-run in isolation passed in 111.9s, and a second full `-j4` run
with a warm cache passed 141/141 clean.

## Files

- `include/sr3luahost/engine_state.h` - the real phase machine, end-of-mission dispatcher,
  silent-class trigger, and the unified `+0x88` completion-flag writer.
- `tools/lua_host_run.cpp` - wires the real phase machine into the per-mission probe loop; adds
  the `mission_completion_flag_0x4_set` TSV column and the `missions_with_real_completion_flag_
  0x4_set` summary line (deliberately separate from the pre-existing `stop_reason=completion`
  bucket, which only means "2 consecutive identical tick signatures," not real mission
  completion).
- `tests/synthetic_luahost_trigger_mission_cluster_test.cpp` - new coverage for the phase machine,
  `missionEndDispatch()`'s 4-way type switch, the silent-class auto-success trigger, and the
  unified completion-flag field.
- `results/verify_probe_missionphase_completion_20261008/` - this run's full raw probe output.
