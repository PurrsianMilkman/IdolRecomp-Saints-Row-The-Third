# --trace-host-calls-before-error=N diagnostic: implementation + first real use (2026-10-03)

Completes the opt-in `--trace-host-calls-before-error=N` flag in `tools/lua_host_run.cpp` (hook/ring logic
was already in place from an earlier pass this session; only the CLI argv parsing was missing). On any
mission-thread Lua error — the initial `_start` call or a later scheduler-pass resume — it prints to stderr
the last N global call names (`LUA_HOOKCALL`) made during that specific resume before it errored. The ring
resets fresh immediately before every individual resume and is only printed if that resume's own call ends
in a Lua error, so it always reflects what led to one specific error, never a stale trace from an unrelated
resume. Diagnostic only: never alters real engine behavior, only stderr output.

## Verification

`cmake --build build_verify_s345 --config Release --target lua_host_run`: clean build, no errors.
`ctest -C Release` in `build_verify_s345`: **61/61 passed**, unchanged count (no new test file — this is a
CLI/stderr-only diagnostic addition, not a behavior change needing new test scaffolding).

## First real use: both of the peer's outstanding questions, answered

Ran the full probe-mode drive with `--tick-budget=721 --probe-unresolved-names
--trace-host-calls-before-error=12` (`results/verify_m08_trace2/`).

**m08's nil-compare** (`m08.lua:910: attempt to compare nil with number`): the trace now reaches the real
error site — unlike an earlier, already-reverted partial version of this diagnostic that only covered the
initial `_start` call and produced empty output (the real error happens well into a later scheduler-pass
resume). The last call before the error is `get_dist`. **Root cause: `get_dist` is a generic (unimplemented)
stub** — confirmed via `grep` (no `get_dist` implementation anywhere in `src/`, only a roster entry tagged
`gameplay`) — which always returns exactly one value, `nil` (`genericStubTrampoline`'s own documented
contract, `src/lua_stub_registry.cpp`). A nil return compared against a number at `m08.lua:910` is the
direct, mechanical cause. Same category as every other "needs a real implementation" blocker this session,
not a host bug.

**sh03/dlc3_m02's `player_list` nil-index** (`game_lib.lua:1488: attempt to index local 'player_list' (a
nil value)`): both traces end identically — `... get_dist_closest_player_to_object player_names_get_all
sizeof_table` immediately before the error. **Root cause: `player_names_get_all` is also a generic
(unimplemented) stub** — confirmed the same way (no implementation in `src/`, roster-only, tagged
`gameplay`). `player_list` is populated from its (nil) return, then indexed — mechanically identical shape
to the m08 case.

**Verdict for the peer's player_list question specifically**: not an under-returning engine call vs. the
real engine (the originally-suspected shape) — it is simply not implemented yet at all. Once a real body
exists for `player_names_get_all` (needs its own spec coverage — not yet checked whether one exists), this
blocker closes the same way every prior "stub nil vs. real value" fix has this session.

## The 6-mission "deeper architectural wait" question — NOT answered by this tool as built, and why

`m06`/`m10`/`m11`/`m13`/`m14`/`m15` never error — they yield indefinitely (`thread_yield`-shaped), so there
is no error to trigger `printTraceRingIfAny`. The existing `--tick-budget=`'s own `start_still_suspended_at`
report already shows WHERE each is parked (`m06_go_to_dock`, `m10_run_get_to_the_front_door`,
`m11_go_to_first_base`, `m13_goto_a_trigger`, `m14_cellphone_call_wait`, `m15_goto_a_trigger` — the same
names already known), but not WHAT native call each one's own wait polls.

The current ring (`g_traceRing`, a single global `std::vector`) is reset fresh before every scheduler-pass
resume **of any thread**, and this mission-drive loop advances the *whole* scheduler every tick (see
`scheduler_foreign_resumes` in the TSV — thousands per mission), not just the one mission's own thread. So
by the time a mission is classified `still_suspended` at budget exhaustion, the global ring most likely
holds trace data from whichever OTHER thread was resumed last in that tick's scheduler pass, not this
mission's own. Printing it at that point would be misleading, not merely incomplete.

A trustworthy answer needs the ring keyed per-coroutine (`std::map<lua_State*, std::vector<std::string>>`
instead of one global vector), so the specific mission's own thread's own last resume can be read back by
its own `lua_State*` at classification time. That's a real, scoped follow-up — not built today. Per the
peer's own framing, this was already the lowest-priority item in the queue (after the UI renderer and this
diagnostic); recording it here rather than forcing an answer from infrastructure not built for the question.
