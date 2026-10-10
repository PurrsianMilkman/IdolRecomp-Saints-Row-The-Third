# Mission-drive before/after: Team-A-cleared 12-function batch (commit 97a9b8d), 2026-10-07/08

Implements spec-lua-api-behaviour.md Sec1.1 `trigger_enable`, Sec1.2 `on_trigger`, Sec1.4
`objective_text_clear`, Sec1.5 `mission_end_failure`, Sec1.6 `mission_set_checkpoint`, Sec2.1
`game_get_platform`, Sec2.5 `audio_play_persona_line`, Sec2.6 `game_is_active_input_gamepad`,
Sec3.2 `on_death`, Sec3.3 `on_vehicle_destroyed`, Sec3.5 `character_is_dead`, Sec3.6
`notoriety_force_no_spawn` (code `src/lua_spec_confirmed_stubs.cpp`/`include/sr3luahost/engine_state.h`,
commit `4683933`; test `tests/synthetic_luahost_trigger_mission_cluster_test.cpp`).

## Method

Real mission drive, `lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out>
--tick-budget=721 --probe-unresolved-names`, against the real archive cache
(`D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache`), matching this project's own
established invocation (`results/probe_blockers_ranked_autopilot_20261007.md`).

- **Before**: commit `5e37c6d` (this worktree's tip immediately prior to this batch), built fresh
  in a separate temporary worktree (`D:/tmp_wt_before_97a9b8d`, build dir `build_before`, Release).
  Raw output: `D:/tmp_wt_before_97a9b8d/results/verify_trigger_mission_cluster_before/`.
- **After**: commit `4683933` (this batch), built in `build_verify_trigger_mission_cluster`
  (Release). Raw output: `results/verify_trigger_mission_cluster_after/`.

## A regression found and fixed before this comparison is meaningful

The first `after` run (before the fix below) showed `missions_with_start_call_ok` collapse from
47/49 to 29/49 — a real regression, not noise. Root cause: `on_vehicle_destroyed`'s own generic/
`#PLAYER#`-chain gate (`EngineState::playerClassChainResolves()`, a NEW `OpenValueMap<bool>` this
batch added) was never pre-populated, so most missions' `_start`-time
`on_vehicle_destroyed(callback, '#PLAYER1#')` hook registration (registering "notify when the
player's own vehicle is destroyed," Sec3.3's own reading) now hard-refused on a brand-new OPEN map
where the existing `objectResolves()` map already answers this exact question (Sec29.4 pre-populates
`"#PLAYER1#"`/`"#PLAYER2#"` as known-true for the identical reason: these are the real, closed-set
player objects). Fixed in `src/lua_spec_initial_state.cpp` by applying the same pre-population to
`playerClassChainResolves()`. After the fix: `missions_with_start_call_ok` is 48/49 (one BETTER than
the 47/49 before-baseline — see `mm_p_01` below) and every other test (84/84, including
`synthetic_luahost_refusal_stress_test`) still passes.

## Aggregate numbers

| metric | before | after |
|---|---|---|
| `missions_with_start_call_ok` | 47/49 | **48/49** |
| `missions_with_start_suspended` | 46/49 | **47/49** |
| `missions_start_after_ticks` | `still_suspended:22 finished:10 errored:14` | `still_suspended:17 finished:10 errored:20` |
| `stop_reason_histogram` | `budget_exhaustion=23 error=0 completion=26` | `budget_exhaustion=18 error=0 completion=31` |
| `probed_unresolved_names` (diagnostic) | 508 distinct | 139 distinct |

`still_suspended` dropping (22→17) while `errored` rises (14→20) is the intended direction for
this project's own methodology: a mission stuck in `still_suspended` at budget exhaustion carries
**no diagnostic signal at all** (a silent forever-poll); a mission that now `errored` carries a
named, specific, actionable `first_error` (exactly the open_state.h design goal — "blocked on OPEN
state X," not a made-up default). The drop in `probed_unresolved_names` (508→139) is consistent
with this: fewer missions need to probe deep into named-object space because more of them now stop
earlier at a specific, real blocker instead of silently continuing on a no-op stub.

## Named per-mission diff — exactly which missions moved and how

Of 49 missions, 40 are byte-for-byte unchanged (same `stop_reason`/`first_error`/`ticks_survived`,
modulo one harmless ±1 tick on `dlc2_m02` from calling newly-real, no-op-outcome natives along the
way — same blocker, `grp_cspawn_pistol_01` OPEN, Sec28.6, not in this batch's scope). 9 moved:

**6 missions converted from an opaque, silent `still_suspended` stall (721 ticks, budget
exhaustion, no error at all) into a fast, specific, actionable OPEN-state error:**

- **`dlc2_m03`**: was `still_suspended` at `dlc2_m03.lua:1501` (`dlc2_m03_first_transmitter`) → now
  errors at tick 5: `playerClassChainResolves()['Player_starting_vehicle<001>']` OPEN (Sec3.3/3.5).
- **`m02`**: was `still_suspended` at `game_lib.lua:3410` (`process_enemy_set`) → now errors at
  tick 3: `playerClassChainResolves()['debris_vehicle 001']` OPEN (Sec3.3/3.5).
- **`m08`**: was `still_suspended` at `m08.lua:960` (`m08_kill_snipers`) → now errors at tick 244:
  `MissionContext::activeAndRunning` (`mission context active, phase != 8`) OPEN (Sec1.5/1.6) — the
  new `mission_set_checkpoint`/`mission_end_failure` gate, directly this batch's own code.
- **`m14`**: was `still_suspended` at `m14.lua:2265` (`m14_cellphone_call_wait`) → now errors at
  tick 4: `playerClassChainResolves()['Veh CP SJ Spotlight Heli']` OPEN (Sec3.3/3.5).
- **`m20`**: was `still_suspended` at `m20.lua:817` (`m20_objective_take_control_of_the_car`) → now
  errors at tick 4: `playerClassChainResolves()['Veh Angel Heli MS']` OPEN (Sec3.3/3.5).
- **`sh01`**: was `still_suspended` at `sh01.lua:433` (`sh01_run`) → now errors at tick 12:
  `MissionContext::activeAndRunning` OPEN (Sec1.5/1.6) — same new gate as `m08`.

**2 missions whose recorded blocker moved to an earlier, more specific point in the real control
flow** (same high-level outcome — completion via error — but the NEW gate now fires before the
previously-recorded one, meaning this batch's natives sit earlier in these missions' own `_start`
sequence than the already-implemented `character_is_in_vehicle`/Sec3.10 gate did):

- **`dlc3_m02`**: was tick 13, `character state enum (+0x16d4)` OPEN (Sec3.4/3.10) → now tick 4,
  `playerClassChainResolves()['start_truck']` OPEN (Sec3.3/3.5).
- **`sh03`**: was tick 18, `character state enum (+0x16d4)` OPEN (Sec3.4/3.10) → now tick 12,
  `MissionContext::activeAndRunning` OPEN (Sec1.5/1.6).

**1 mission whose own `_start` call now succeeds where it previously failed outright:**

- **`mm_p_01`**: before, `start_call_ok=0` (the `_start` call itself errored, on the same
  `zscene_prep`/cutscene-state-`0x0153b520` OPEN gate `mm_p_06` still hits, unrelated to this
  batch) → after, `start_call_ok=1`, reaches a legitimate suspend (`still_suspended`, budget
  exhaustion, `mm_p_01.lua:87`). The exact causal link to a specific one of these 12 natives was
  not traced (most likely: an earlier native in `mm_p_01`'s own `_start` sequence — plausibly
  `trigger_enable`/`on_trigger`/`on_death` — no longer being a generic nil-returning stub changes
  which branch the script takes before it would have reached the `zscene_prep` call at all) —
  stated as an observed, measured effect, not a guessed mechanism.

No mission regressed: every mission that was `finished` or `errored` before stays `finished` or
`errored` after, with the same or an earlier/more-specific blocker.

## A residual tooling gap, flagged not fixed (scope discipline)

`playerClassChainResolves()` is a NEW OpenValueMap this batch added; `--probe-unresolved-names`
(`tools/lua_host_run.cpp`) only widens the pre-existing `objectResolves()` map, so the 6 missions
above that now stop at a `playerClassChainResolves()` OPEN item cannot currently be probed past it
to see what blocks them NEXT, unlike every other named-object gate in this project. Extending the
probe to cover this second map would be a small, mechanical follow-on — not done here, since it
touches `tools/lua_host_run.cpp`'s diagnostic machinery, outside this task's 12-section scope.

## Build artifacts (for the cleanup batch — not deleted per standing policy)

- `build_verify_trigger_mission_cluster/` (this worktree, Release) — 367M. Full build + `ctest -C
  Release` both clean (84/84 tests, including `synthetic_luahost_refusal_stress_test`).
- `D:/tmp_wt_before_97a9b8d/` — a temporary `git worktree` at commit `5e37c6d` (pre-batch),
  created solely to build an unmodified `lua_host_run` for the "before" run. Its own `build_before/`
  subdir — 332M.
- `C:/Users/Purrsian/AppData/Local/Temp/claude/d--Project-Crreish-TEAM-B/3d387bd4-f4dd-4367-a32c-
  5b50d9fb4a4d/scratchpad/before_worktree/` — 231M. A FIRST attempt at the same temporary worktree,
  abandoned after `cmake` configure failed there (Windows path-length limits under the deeply
  nested scratchpad path) — left in place per the same standing policy; superseded by
  `D:/tmp_wt_before_97a9b8d/` above, never built.
- `results/verify_trigger_mission_cluster_after/` (this worktree) — 34M raw harness output (TSVs +
  summary) backing the "after" numbers above.
- `D:/tmp_wt_before_97a9b8d/results/verify_trigger_mission_cluster_before/` — 34M, the "before"
  counterpart (lives inside the temporary worktree above).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
