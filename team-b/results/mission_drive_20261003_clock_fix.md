# Game clock (§48) fix verification (2026-10-03)

Implements spec-lua-api-behaviour.md §48: the game clock's hour/minute are now CONFIRMED known from
`applySpecInitialState()` (10:00:00, the single-player new-game value, §48.2's resolved correction),
closing the `dlc3_m01`/`m19`/+2 others blocker (`set_time_of_day`'s own forward-delta arithmetic reads
the current hour/minute first, which previously had no initial value). `set_time_of_day` (§48.4) now
forgets hour/minute afterward rather than fabricating the post-snap value, since the real engine snaps
to the nearest time-of-day key and the key list is OPEN loaded data. A new `gameClockAdvanceFrame()`
(§48.3, CONFIRMED 40x real-time mechanism) is wired into `tools/lua_host_run.cpp`'s per-tick loop.

## Verification

Fresh build (`build_measurement`), `ctest -C Release`: 54/54 passed, including 2 fixed/added test
blocks (`synthetic_luahost_batch31_32_test.cpp`'s existing `set_time_of_day` coverage updated for the
new forget()-after-call behavior; a new dedicated §48 block in `synthetic_luahost_test.cpp` covering
initial state, `gameClockAdvanceFrame`'s 40x arithmetic including a within-day wrap, and the
forget-then-no-op-advance interaction with `set_time_of_day`).

Real mission drive (`results/verify_clock_mission_drive/`): the string "game clock hour byte" no
longer appears as any mission's blocker anywhere in the trace (grepped directly, 0 hits in
`verdict_mission_drive.tsv`). `dlc3_m01` and `m19` now run the full 20-tick budget and park at the
same shared blocker as most other missions (`game_lib.lua:1364`, in `cutscene_play`, on the
already-tracked `Killbane` named-object-resolution OPEN item) instead of erroring immediately on the
clock. Aggregate numbers (`still_suspended:34 finished:8 errored:5`) are unchanged from the pre-fix
baseline - these 2 missions simply moved from their own unique blocker to the same shared one
everyone else already hits, not a further regression or additional progress past Killbane.

Verdict: the clock fix is sound and closes exactly the blocker it targeted.
