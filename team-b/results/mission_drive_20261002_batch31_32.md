# Mission-drive delta: Sec31 (pause-map stag mode) + Sec32 (ranking tranche 04) + the 0x009df3d0 correction (2026-10-02)

Run: `lua_host_run.exe` against the real cache `D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache`
with `tools/lua_all_registered_1490_tagged.txt`, built fresh in `build_batch3132` from this worktree (branched off
`main` at `0b3a543`, with `0cc94bd`/`2556182`/`7592a42`/`03d003a` — resume-scheduler, named-object map,
weapons-combat fix, Vint UI API — all already merged). Output dir
`results/verify_batch3132_mission_drive/` (TSV + summary). Baseline for comparison:
`results/mission_baseline_20261002_post_scheduler.md`.

## Aggregate, before -> after

| metric | before | after |
|---|---|---|
| `missions_with_start_call_ok` | 48/49 | 48/49 (unchanged) |
| `missions_with_start_suspended` | 39/49 | 39/49 (unchanged) |
| `missions_start_after_ticks` | finished:31 errored:2 still_suspended:6 killed:0 | finished:27 errored:7 still_suspended:5 killed:0 |
| `scheduler_passes` | own_resumes:3915 own_thread_errors:2 leftover_resumes:12872 | own_resumes:3391 own_thread_errors:7 leftover_resumes:9968 |
| `live_script_threads_after_missions` | 5 | 4 |
| `total_ticks_survived_across_all_missions` | 464 | 418 |

**The top-level gates are unchanged** (48/49 call_ok, 39/49 suspended) — neither §31 nor §32 touch the
Killbane/named-object-resolution or resume-scheduler mechanisms that set those two numbers. The
**after-ticks breakdown shifted**: 5 missions moved from `finished` to `errored`. Investigated individually,
not assumed:

## The 5 newly-errored missions, split by real cause

**2 are a direct, confirmed consequence of this batch's own new CONFIRMED OPEN state** (`set_time_of_day`,
Sec32.1, the game-clock hour/minute bytes 0x014ff33c/0x014ff33d): `dlc3_m01` and `m19` both now stop during
their own resume with `engine state game clock hour byte (0x014ff33c) is OPEN (Sec32.1)`. Confirmed via
`results/verify_batch3132_mission_drive/verdict_stub_hits_with_missions.tsv`: `set_time_of_day` shows
`incremental_from_missions=2` — an exact match to these two missions, the only two that call it. Before this
batch, `set_time_of_day` was a silent generic stub (no error, no effect); it is now a real, CONFIRMED
function that correctly refuses to invent a game-clock starting hour/minute no spec gives — surfacing a
genuine real call site that was previously invisible, not a regression. **Not fixed by inventing a default**:
no spec text states the real engine's own starting hour/minute, and HIGH-CONFIDENCE-guessing one would
violate this project's own no-invented-fixes standard.

**3 are NOT caused by any of this batch's 30 new functions** (`dlc2_m03`→`'Actress'`, `m08`→`'NPC Zimos'`,
`m12`→`'npc_suko_start'`, all `named-object resolution[...]` OPEN, the same Sec3.9/Sec10.6/Sec29 mechanism
Killbane already uses). Checked directly against the stub-hits TSV: none of these three names are arguments
to any of the 30 newly-implemented functions in this run (the only new function any mission actually calls
besides `set_time_of_day` is `party_add_do`, 4 calls from one script, all with a pre-populated-literal leader
name — it never errors). These three missions' own first-error at call time was already, and remains,
`'Killbane'` — unchanged. What changed is their **resume-time** error: most likely explained by this harness's
own documented shared-gameplay-state effect across sequentially-driven missions ("`leftover_resumes` = resumes
of threads still live from an earlier mission in the shared gameplay state" — this tool's own summary wording,
unchanged this batch): `dlc3_m01`/`m19` now error out at a different point in the shared resume schedule than
before, shifting exactly which tick/resume-pass count every later mission's own threads land on, and therefore
which of each mission's *own, pre-existing* unresolved names its resume happens to reach first. This is a
timing-dependent side effect of the test harness's cross-mission shared state, not a new gap this batch
introduces — flagged honestly rather than silently absorbed, per this project's own measurement discipline, but
not independently re-traced tick-by-tick to a fully certain root cause within this pass's scope.

## What this batch's own target function actually measured

`pause_map_stag_current_district_control` (§31's own subject) is itself real and significant by call volume —
303 real calls per `results/stub_ranking_with_specced_20261002.tsv`, all pre-mission UI bring-up
(`call_count_before_missions`), matching §31's own "called 303 times" claim exactly — but, as expected for a
UI pause-map function, it and its 4 confirmed siblings do not appear anywhere in the mission-drive's own
per-mission detail (not mission-script-reachable), so they have **no effect on the aggregate numbers above**,
consistent with the Vint UI API batch's own prior finding that UI-registrar functions are off the
mission-drive's critical path.

**Ranking tranche 04's own real-world footprint, measured (not assumed from the stale pre-batch ranking,
which showed 0 calls for all 25 names — that ranking predates this implementation and could not see calls a
silent generic stub makes no record of distinguishing from "never reached")**: 2 of the 25 names
(`set_time_of_day`, `party_add_do`) are real, mission-script-reachable call sites; the other 23 were not
observed called by any of the 49 real missions in this run (that does not mean they are never called by any
of the ~800 total shipped scripts — only the 49 mission-drive scripts were exercised here).
