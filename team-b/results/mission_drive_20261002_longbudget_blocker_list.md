# Mission drive — long-budget diagnostic: "still suspended" is real blockers, not slow waits (2026-10-02)

Per the orchestrator's request, `tools/lua_host_run.cpp` gained a `--tick-budget=` CHOSEN override
(default unchanged, 20) plus two reporting additions: `mission_simulated_seconds_budget=` in
`verdict_summary.txt` (the budget re-expressed in this host's own simulated time: ticks *
`fade_host_ms_per_tick` / 1000), and `waiting_at=[file:line (in function)]` in the per-mission console
line / `start_still_suspended_at` TSV column for any mission still suspended at the end of its budget.

## Method

Ran the real mission drive against `D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache` at
two budgets, same binary (`build_verify_s3840`, commit `14267f3`, current `main` as of this doc):

- default: 20 ticks = 6.66 simulated seconds
- `--tick-budget=721`: 721 ticks = 240.09 simulated seconds (~36x longer)

## Result: identical aggregate at both budgets

`missions_with_start_call_ok=48/49`, `missions_with_start_suspended=47/49`,
`missions_start_after_ticks=still_suspended:34 finished:8 errored:5 killed:0` — **byte-identical**
between the two runs. 36x more simulated time produced zero additional progress for any of the 34
still-suspended missions. This rules out "mid-way through a long, legitimate wait" for all 34: a
real wait (a fade, a scripted delay) would have resolved somewhere in a 240-second simulated window
if it were going to resolve at all.

## Where they're actually stuck (`waiting_at`, identical at both budgets)

| count | location | first_error at that point |
|---|---|---|
| 31 | `game_lib.lua:1364` (in `cutscene_play`) | named-object resolution `['Killbane']` is OPEN |
| 1 | `mm_p_07.lua:116` (in `mm_p_07_run`) | named-object resolution `['Killbane']` is OPEN |
| 1 | `mm_p_01.lua:87` | named-object resolution `['Killbane']` is OPEN |
| 1 | `game_lib.lua:563` (in `action_play_non_blocking`) | named-object resolution `['Killbane']` is OPEN |

All 34 share the same immediate cause: `Killbane` (spec-lua-api-behaviour.md Sec3.9/Sec10.6/Sec29) is
an unresolved named object, already a tracked OPEN item, not a newly-discovered gap. The overwhelming
majority (31/34) are parked at the exact same single call site, `game_lib.lua:1364` inside
`cutscene_play` — a real, repeatable, single blocker, not 31 unrelated problems.

## Takeaway for Team A

This is a concrete blocker list, not a timing artifact: closing the `Killbane` named-object OPEN item
(giving it a real resolved handle) is very likely to unstick the large majority of these 34 missions
in one move, since 31 of them share the identical `game_lib.lua:1364`/`cutscene_play` call site.
