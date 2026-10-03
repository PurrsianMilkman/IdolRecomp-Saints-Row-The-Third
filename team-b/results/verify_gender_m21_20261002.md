# Player-gender initial state: m21 before/after (2026-10-02)

Task: close `character +0xa41 gender byte (1=female, else male)['#PLAYER1#'] is OPEN`, the specific
resume-error blocker reported for mission `m21` in `results/mission_baseline_20261002_post_scheduler.md`
(built from `main` at `7592a42`/`17cbdfb`, already an ancestor of this worktree's branch point `e3b0f3d`).

## Mechanism implemented

`src/lua_spec_initial_state.cpp`, new item 9 in `applySpecInitialState()`:
`es.characterGender().set("#PLAYER1#", 0);` — CONFIRMED per:
- `spec-lua-api-behaviour.md` §16.8/§16.8a — producer census: a fresh character object
  (`0x00954340`) holds the sentinel pair `+0xa41:=3`/`+0xa40:=5`; no script ever observes it because
  game-start step `0x00708330` always applies one of (a) a loaded save's own character-record bytes,
  (b) the `player_presets.xtbl` row marked `Default`, or (c) a co-op guest record, before any Lua runs.
- `spec-save-format.md` §10.4 — confirms save offsets `+0x203c` (gender: 0 male/1 female) /
  `+0x203d` (race: 0 asian/1 black/2 hispanic/3 white), written from player `+0xa41`/`+0xa40`.
- `spec-tables-customization.md` §16.2 — confirms `player_presets.xtbl`'s one `Default`-marked row is
  `male_white`: `Gender`=0 (via `0x00831fc0`'s own vocabulary), `Race`=3.

This host (`tools/lua_host_run.cpp`) never loads a real `.sr3save`/`.sr3s_pc`/`.sr3d_pc` file anywhere in
the mission drive (checked directly: no reference to any save path in that file), so path (a) never
applies here and path (c) needs a co-op session, which is CONFIRMED absent at start (item 1 of the same
function). The applicable path is always (b), the fresh-game default-preset step — so `Gender := 0` is
the CONFIRMED value for `"#PLAYER1#"` in this host's configuration. `"#PLAYER2#"` is deliberately left
OPEN (no co-op session, so no second player object exists to even hold the sentinel). Race (`+0xa40`) has
no reader stub anywhere in this codebase yet, so nothing was set for it — there is no OPEN query to close.

## Test coverage added

- `tests/synthetic_luahost_test.cpp`: new block — raw `EngineState` (no initial state) still refuses
  `characterGender().get("#PLAYER1#")` (the sentinel is never directly observable); after
  `applySpecInitialState()`, `"#PLAYER1#"` is known and `== 0`, `"#PLAYER2#"` stays unknown; `Host({})`
  exposes the same value through its own engine state.
- `tests/synthetic_luahost_batch2728_test.cpp`: new assertions in the existing Sec28.21 block —
  `character_get_gender('#PLAYER1#') == 0` through the real Lua stub, and `character_get_gender('#PLAYER2#')`
  still throws the OPEN error (regression guard against over-widening).

Fresh build (`build_verify_gender`, Release) + full `ctest -C Release`: **51/51 passed**, including both
edited test binaries.

## Mission-drive re-run (`lua_host_run.exe`, same cache + tagged list as the cited baseline)

Aggregate (`verdict_mission_drive_summary.txt`) — **unchanged from the baseline** except
`scheduler_passes.own_resumes` (+1, exactly m21's one extra resume cycle):

| Metric | Before (baseline) | After (this run) |
|---|---|---|
| missions_with_start_call_ok | 48/49 | 48/49 |
| missions_with_start_suspended | 39/49 | 39/49 |
| missions_start_after_ticks | finished:31 errored:2 still_suspended:6 killed:0 | finished:31 errored:2 still_suspended:6 killed:0 |
| scheduler_passes.own_resumes | 3915 | **3916** |
| total_ticks_survived_across_all_missions | 464 | 464 |
| stop_reason_histogram | budget_exhaustion:8 error:0 completion:41 | budget_exhaustion:8 error:0 completion:41 |

**m21 itself** (`verdict_mission_drive.tsv`, column `start_after_resume_error`):

- Before: `character +0xa41 gender byte (1=female, else male)['#PLAYER1#'] is OPEN (spec-lua-api-behaviour.md Sec28.21/Sec28.12/Sec16.8)`
- After: `m21_start: engine state character ignore-AI flag (+0x2bc) is OPEN (spec-lua-api-behaviour.md Sec3.4): no spec gives its value, so it is not modelled`
- `scheduler_resumes`: 35 → 36 (one more resume pass happened before the next blocker).

**Honest verdict:** the exact blocker named in the task brief — the gender-byte query for `'#PLAYER1#'` —
is CONFIRMED fixed; m21's resume no longer stops there. m21 is **still** counted in the `errored:2`
aggregate bucket, because resuming its coroutine now advances one step further along the same path and
immediately hits a *different*, already-known, still-genuinely-OPEN item (`character ignore-AI flag
+0x2bc`, spec-lua-api-behaviour.md §3.4 — the same item flagged alongside Killbane/max-hit-points in the
prior session's own HANDOFF notes). This is not a regression and not a non-fix: it is the next link in the
same per-tick-hook chain, outside this task's scope (no §3.4 CONFIRMED initial value was in the brief).
`dlc2_m02` (the other `errored` mission, an unrelated script-level nil index) is untouched, as expected.
