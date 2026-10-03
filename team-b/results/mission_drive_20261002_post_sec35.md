# Mission drive — post-Sec35 teleport_coop merge verification (2026-10-02)

Verified independently against a fresh build (`build_verify_s35`, commit `83a2f1b`), separate from the
implementing agent's own pre-merge build. `ctest -C Release`: 53/53 passed.

| metric | pre-Sec35 (post-Sec31/32, `results/mission_drive_20261002_post_sec3132.md`) | post-Sec35 (this run) |
|---|---|---|
| `missions_start_after_ticks` | finished:26 errored:9 still_suspended:4 | finished:27 errored:10 still_suspended:2 |
| `live_script_threads_after_missions` | 4 | 2 |

still_suspended dropped 4->2, consistent with Sec35's fix unsticking missions previously parked
forever in `teleport_coop`'s `repeat thread_yield() until teleport_check_done(...)` poll
(dlc2_m01/m13/m19). No crash, no new build errors, no unexpected regression.

Verdict: merge `83a2f1b` is sound.
