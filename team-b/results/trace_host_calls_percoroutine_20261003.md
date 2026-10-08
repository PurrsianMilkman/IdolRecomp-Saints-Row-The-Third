# Per-coroutine trace ring + the 6-mission "architectural wait" question answered (2026-10-03)

Peer follow-up to `results/trace_host_calls_20261003.md`'s own "NOT answered by this tool as built" section:
`--trace-host-calls-before-error`'s ring was a single global `std::vector`, reset before every resume of
ANY thread. Since this mission-drive loop advances the whole scheduler every tick (thousands of
foreign-thread resumes per mission, per `scheduler_foreign_resumes` in the TSV), a mission classified
`still_suspended` at budget exhaustion could never trust the global ring to hold ITS OWN last resume's
calls rather than some other thread's.

Fixed by keying the ring per-coroutine: `g_traceRings` is now `std::unordered_map<lua_State*,
std::vector<std::string>>`. `lua_sethook` is already per-thread in Lua 5.1, so the `lua_State*` the hook
fires with IS the specific coroutine being resumed — using it directly as the map key means reading back
`g_traceRings[co]` by a mission's own `rec->co` is always that thread's own last resume, regardless of what
else resumed around it. New `printTraceRingStillSuspended()` reads this same per-coroutine ring at the
existing `still_suspended` classification point (previously only the error path printed anything), finally
answering "what does this mission's own wait poll" for a mission that never errors.

`PassEvent` (the scheduler-pass `after` callback's own argument) carries no `lua_State*` — bridged with one
local `currentResumeCo`, safe because `before`/`after` are called in strict pairs for the same record
within one `schedulerPass` call (confirmed by reading `BareThreadTable::schedulerPass`'s own single-
threaded while loop, no interleaving between records).

## Verification

Rebuilt in `build_verify_uirender`: clean. `ctest -C Release`: **61/61 passed**. Default-mode (no
diagnostic flags) mission drive confirmed byte-identical to the pre-change baseline via direct `diff`
(exit 0) — this is purely additive, stderr-only tooling.

## The 6-mission question, answered

`--tick-budget=721 --probe-unresolved-names --trace-host-calls-before-error=15`: all 6 target missions
(`m06`, `m10`, `m11`, `m13`, `m14`, `m15`) show the identical shape — **exactly 1 call in their own last
resume: `thread_yield`, nothing else**. No missing/unimplemented native is hiding inside their wait loop —
whatever condition each wrapper checks before yielding (`m06_go_to_dock`, `m10_run_get_to_the_front_door`,
`m11_go_to_first_base`, `m13_goto_a_trigger`, `m14_cellphone_call_wait`, `m15_goto_a_trigger`) is evaluated
entirely in Lua-local state, with no host call this trace mechanism can observe in that specific check.

For contrast, two OTHER still-suspended missions in the same run show a genuinely different shape:
`m04_start` → `character_is_in_vehicle character_is_in_vehicle thread_yield` (2 real host calls before
yielding) and `m01_start` → `character_is_ready thread_yield` (1 real host call). These ARE polling a host
native every tick; the 6 target missions are not.

This is consistent with (does not independently prove, but rules out the alternative of "a missing stub
call hidden in the loop") the peer's own "legitimate player-input wait, a real milestone" hypothesis: each
of the 6 appears to check its own already-known Lua-local state (a position comparison, a trigger-enter
flag, a callback result) rather than being blocked on any native this project hasn't implemented.

**Which callback-registration native each one is actually waiting on (peer follow-up) is not retrievable
from the current ring data**: `trigger_enable`/`on_trigger`/`on_vehicle_enter` all show real, non-zero
aggregate call counts this run (86/83/1 respectively, `verdict_stub_hits_with_missions.tsv`), so at least
one of these 6 missions (or another mission) almost certainly registered one before entering its wait loop
- but that TSV is a whole-run aggregate, not broken down per mission, and the per-coroutine ring only keeps
each thread's MOST RECENT resume (overwritten every resume), discarding whatever a mission called on an
earlier resume (its own setup, before the loop). Answering this specific sub-question would need a small
further change (e.g. also keeping each coroutine's first-ever resume, or a per-mission call log) - not
built here, per the peer's own explicit "don't let it delay the renderer" instruction.
