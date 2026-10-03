# Mission-drive delta after Sec33 ("ranking tranche 05") + Sec14.1 correction (2026-10-02)

Run: `lua_host_run.exe` against the real cache `D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache`
with `tools/lua_all_registered_1490_tagged.txt`, built fresh from this worktree (`build_sec33`, Release).
Output dir `results/verify_sec33_check/`. Compared against the cited baseline,
`results/mission_baseline_20261002_post_scheduler.md`.

## Aggregate comparison

| Metric | Baseline (pre-Sec33) | This run (post-Sec33) |
|---|---|---|
| `missions_with_start_call_ok` | 48/49 | 48/49 (unchanged) |
| `missions_with_start_suspended` | 39/49 | 39/49 (unchanged) |
| `missions_start_after_ticks` | finished:31 errored:2 still_suspended:6 | **finished:30 errored:3** still_suspended:6 |
| `scheduler_passes` | own_resumes:3915 own_thread_errors:2 | own_resumes:3915 own_thread_errors:**3** |
| `live_script_threads_after_missions` | 5 | 5 (unchanged) |
| `total_ticks_survived_across_all_missions` | 464 | 464 (unchanged) |
| `stop_reason_histogram` | budget_exhaustion=8 error=0 completion=41 | unchanged |

**Exactly one mission flipped, from `finished` to `errored`: `dlc3_m02`.** Everything else is byte-for-byte
identical to the cited baseline (same ticks survived, same scheduler-resume counts, same suspension set).

## Root cause of the one flip (not a defect - reported per the task's "report honestly" instruction)

`dlc3_m02`'s resumed coroutine (same point as before: `ticks_survived=9`, `scheduler_resumes=79`, suspended
at `game_lib.lua:1412` in `fade_out_block`) calls the real, Lua-visible global `audio_any_conversation_playing()`
during a later tick. Before this batch, that name was an unimplemented generic stub (always returns `nil`,
which the calling script evidently tolerates as falsy and continues past). After implementing it per
spec-lua-api-behaviour.md Sec33.4 (CONFIRMED: true iff any session member has a valid, non-0xff slot on the
per-session "mission_conv" channel), this project has no per-member conversation-slot array anywhere to
answer that honestly - `EngineState::conversationChannelActive()` (an `OpenValueMap<bool>`) correctly refuses
with `OpenStateError` rather than inventing a value, which now surfaces as a real Lua error:

```
dlc3_m02_start: engine state per-session conversation channel active slot['mission_conv'] is OPEN
(spec-lua-api-behaviour.md Sec33.4): no spec gives its value, so it is not modelled
```

This is the same "implementing something correctly moves the blocker forward instead of papering over it"
pattern already recorded for `mm_p_01` and `m21` in the cited baseline - not a regression to fix by inventing
a default. `missions_with_start_call_ok`/`missions_with_start_suspended` (measured before any resume) are
unaffected; only the deeper resume-phase outcome for this one mission changed.

## Real call-count correction

The pre-implementation evidence used to scope this batch (`results/verdict_stub_hits_with_missions_20261002.tsv`)
showed **zero** real hits for all 25 Sec33 names - that file predates this session's resume-scheduler fix
reaching this deep into mission ticks. This fresh run shows **2 of the 25 names are real exercised call sites**
after all: `audio_any_conversation_playing` (1 real call, `dlc3_m02`, OPEN as above) and `auto_pickup_disable`
(1 real call, no error - a plain unconditional write). The other 23 names are still genuinely zero-hit in this
deeper run. Reported here so the next pass has the corrected picture rather than the stale one.
