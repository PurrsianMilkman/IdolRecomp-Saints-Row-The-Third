# Mission-drive A/B: `<stem>_start` checkpoint-argument fix (2026-10-02)

Implements `spec-lua-api-behaviour.md` §36 / `spec-lua-bindings.md` §14.11 (synced as of commit
`6ef420b`, this worktree's branch point): a mission's fresh start must call
`<stem>_start("mission start", false)` — the literal STRING `"mission start"`, never the number `0` —
as a new coroutine through the script-thread runner. `tools/lua_host_run.cpp`'s `callMissionStart()`
already used the runner/coroutine mechanism correctly (via `BareThreadTable::allocate`/`run`, not a
bare `lua_pcall`); the bug was the checkpoint argument's **type and value**: `lua_pushnumber(L, 0.0)`
instead of the confirmed string.

## Old vs new call shape

- **Before**: `funcName(0.0, false)` — `kMissionStartCheckpoint` was `constexpr double = 0.0`, pushed
  via `lua_pushnumber`.
- **After**: `funcName("mission start", false)` — `kMissionStartCheckpoint` is now
  `const std::string = "mission start"`, pushed via `lua_pushstring`. `is_restart` was already
  correctly `false` and is unchanged. No resume/retry call site exists in this harness (one
  `callMissionStart` call per mission, fresh-start only), so §36.4's resume half
  (`start_fn(checkpointName, true)` reading the current-checkpoint-name global `0x014c8354`) is
  honestly **not implemented** — there is nothing here that would ever pass `isRestart=true`.
- The `<stem>_start` name itself is unchanged (still derived from `mission_lua_stem` +
  `"_start"`) but its header comment now explicitly labels this as a **documented engineering
  choice** (HYPOTHESIS per §36.4/§14.11, the real name is a data-borne `script_mission_start`
  property this project cannot read), not a confirmed spec mechanism.

## Controlled A/B (same commit, same binary otherwise, cache = real game install)

`results/verify_mission_AB_before/` = checkpoint reverted to `0.0` (byte-for-byte the pre-fix
`tools/lua_host_run.cpp`, rebuilt and rerun to isolate exactly this change from everything else
already on `main` since the prior frozen baseline, e.g. the Vint UI API §18-21 merge). Reproduces
`results/mission_baseline_20261002_post_scheduler.md`'s numbers exactly (48/49, 39/49,
`still_suspended:6 finished:31 errored:2`, `scheduler_passes` identical) — confirming nothing else
on `main` moved these numbers, only this fix does.

`results/verify_mission_start_fix_20261002/` = this fix applied (current worktree state).

| key | before (checkpoint=0) | after (checkpoint="mission start") |
|---|---|---|
| `missions_with_start_call_ok` | 48/49 | 47/49 |
| `missions_with_start_suspended` | 39/49 | 46/49 |
| `missions_start_after_ticks` | still_suspended:6 finished:31 errored:**2** killed:0 | still_suspended:37 finished:8 errored:**1** killed:0 |
| `total_ticks_survived_across_all_missions` | 464 | 795 |
| `live_script_threads_after_missions` | 5 | 37 |
| `scheduler_passes` | own_resumes:3915 own_thread_errors:2 | own_resumes:8295 own_thread_errors:1 |

Full per-mission diff: `py tools/bridge_diff.py results/verify_mission_AB_before results/verify_mission_start_fix_20261002`.

## `dlc2_m02`: the target bug, fixed and measured, not assumed

- **Before**: `first_error=[dlc2_m02_start: [string "dlc2_m02.lua"]:1245: attempt to index local
  'cp_data' (a nil value)]` — the exact real bug this task was dispatched to fix, reproduced from a
  clean revert of only this change.
- **After**: `start_call_ok=1 start_suspended=1 ticks_survived=20
  stop_reason=[budget exhaustion (20 ticks)] first_error=[engine state named-object
  resolution['Killbane'] is OPEN ...]` — `dlc2_m02` no longer errors on its own checkpoint-table
  index. It now runs its real fresh-start logic to the full 20-tick budget and hits the *same*
  already-known, already-OPEN `Killbane` named-object-resolution gap that blocks most other
  missions (not a new bug — a pre-existing, correctly-flagged OPEN spec item, `spec-lua-api-behaviour.md`
  §3.9/§10.6/§29).

## Honest note on the rest of the delta — not a regression, a correction

Most of the 33 changed missions flipped from `stop_reason=completion` (2-3 ticks, a false
idempotence signal) to `stop_reason=budget exhaustion` (full 20 ticks). This is the **expected**
effect of giving mission scripts their real checkpoint argument: previously, `cp == 0` never
matched any checkpoint name a script's own Lua code tests for (`"mission start"`/`"1"`-`"4"`/etc.),
so most scripts fell through to whatever their checkpoint-table lookup/`else` branch does — often a
short, non-looping path — and reported a bogus quick "completion". With the correct string, scripts
now take their real fresh-start branch, which loops/polls (`thread_yield`, `cutscene_play_check_done`,
etc. — `thread_yield` calls alone: 16,792 → 171,209) and runs far longer, matching the real engine's
own behavior far more closely. Two individual missions *look* worse in isolation:

- **`mm_p_06`**: `start_call_ok` 1→0, now failing immediately on `zscene_is_loaded: ... pending
  zscene 'p_z02' ... is OPEN` (same already-OPEN `0x0153b520` cutscene-state gap `mm_p_01` already
  hit before this fix, spec-lua-api-behaviour.md §26.25) instead of running 9 ticks and hitting
  `Killbane` like before.
- **`m21`**: ticks 5→2, now hitting a different already-OPEN item (`character +0xa41 gender byte`)
  instead of `Killbane`.

Both are reassignments between pre-existing, already-documented OPEN spec gaps — not new failure
categories, and not something this fix should "work around": per this project's no-invented-fixes
policy, these stay correctly reported as OPEN. The gameplay Lua state is one persistent state shared
across the whole mission walk (this file's own long-standing caveat), so an earlier mission's script
now taking a different real code path can change what shared engine state a later mission sees —
exactly what happened here. Reported as measured, not suppressed.

## Caveats carried over, unresolved by this task

- The separate `*_check_done` status-code-inversion fix (§35) was **not** on `main` at this
  worktree's branch point (`6ef420b`) and has not been folded in here — these numbers do not include
  it.
- `main` gained two more spec-sync commits after this worktree branched (`5a02e8e`: narrows the
  §36.1/§14.11 "which carrier holds `script_mission_start`" OPEN question, no effect on the CONFIRMED
  call-shape mechanism implemented here; `002c811`: §26.25 zscene-driver text, doc-only, not yet
  implemented in any worktree's code as far as this task found) — verified by reading both diffs
  directly, neither contradicts or changes what was implemented here.
