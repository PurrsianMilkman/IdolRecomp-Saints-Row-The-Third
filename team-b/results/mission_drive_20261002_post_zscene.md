# Mission drive — post-zscene-fix merge verification (2026-10-02)

Verified independently against a fresh build (`build_verify_zscene`, commit `808e12f`), separate from
the implementing agent's own pre-merge build. `ctest -C Release`: 53/53 passed.

| metric | pre-zscene-fix (post-Sec36+cadence/Sec37, `results/mission_drive_20261002_post_sec3637.md`) | post-zscene-fix (this run) |
|---|---|---|
| `missions_with_start_call_ok` | 47/49 | 48/49 |
| `missions_with_start_suspended` | 46/49 | 47/49 |
| `missions_start_after_ticks` | finished:8 errored:5 still_suspended:33 | finished:8 errored:5 still_suspended:34 |

`mm_p_01` (the fix's own target) now passes `start_call_ok=1`, `start_suspended=1`, runs the full
20-tick budget, `zscene_promotions:1` (was 0) — no longer the special zscene blocker, now hits the
same already-tracked `Killbane` OPEN item as most other missions.

`mm_p_06` still fails `start_call_ok=0`, but now on a different, unrelated gap
(`zscene_prep: engine state 0x0153b520 (cutscene state) is OPEN`, Sec26.24/Sec26.25) rather than the
old pending-zscene-promotion refusal — a separate, pre-existing OPEN item this fix was not scoped to
close, not a regression.

`m21` now reaches `character +0xa41 gender byte['#PLAYER1#'] is OPEN` (Sec28.21/Sec28.12/Sec16.8) —
confirms the player-gender/m21 fix (queued next) is not stale against current `main`.

Verdict: merge `808e12f` is sound.
