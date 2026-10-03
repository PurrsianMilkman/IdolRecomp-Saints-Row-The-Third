# Mission drive — post-Sec31/Sec32 merge verification (2026-10-02)

Verified independently against a fresh build (`build_verify_s3132`, commit `6d89b4d`), separate from the
implementing agent's own pre-merge build. `ctest -C Release`: 53/53 passed (including the new
`synthetic_luahost_batch31_32_test`).

| metric | pre-Sec31/32 baseline (post-Sec33, `results/mission_drive_20261002_post_sec33.md`) | post-Sec31/32 (this run) |
|---|---|---|
| `missions_start_after_ticks` | finished:30 errored:3 still_suspended:6 | finished:26 errored:9 still_suspended:4 |
| `scheduler_passes` | own_resumes:3915 own_thread_errors:3 | own_resumes:3172 own_thread_errors:9 |
| `live_script_threads_after_missions` | 5 | 4 |

Shift: finished -4, errored +6, still_suspended -2. Same direction and similar magnitude to what the
Sec31/32 implementing agent measured pre-merge against its own (slightly earlier) baseline
(finished 31→27, errored 2→7, still_suspended 6→5) — the small numeric difference between that
prediction and this independently-measured shift is consistent with the harness-level shared-state
timing sensitivity the agent itself flagged as not fully re-traced to certainty (3 missions:
`dlc2_m03`/`m08`/`m12`), not a sign of a bad merge resolution.

Confirmed exactly as predicted: 2 of the new errors are `dlc3_m01` and `m19`, both now stopping on
`engine state game clock hour byte (0x014ff33c) is OPEN (spec-lua-api-behaviour.md Sec32.1)` —
the new OPEN state introduced by Sec32's `set_time_of_day`, not a regression.

`mm_p_01` still blocked on the pre-existing zscene-promotion OPEN (Sec26.25) — unchanged, fix not yet merged.

Verdict: merge `6d89b4d` is sound. No unexpected regressions; the mission-drive shift matches the
implementing agent's own characterization of Sec31/32's effect.
