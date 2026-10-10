# First-resume trace ring: which native each "architectural wait" mission calls before it waits (2026-10-07)

Peer follow-up to `results/trace_host_calls_percoroutine_20261003.md`'s own explicitly-left-open question:
**which callback-registration native did each of the 6 "architectural wait" missions
(`m06`/`m10`/`m11`/`m13`/`m14`/`m15`) actually call before entering its wait loop?** That doc's per-coroutine
ring (`g_traceRings`, keyed by `lua_State*`) answers "what does this mission's own wait poll RIGHT NOW"
correctly, but is overwritten by `resetTraceRing()` on every resume, so by budget exhaustion it has long
since discarded whatever a mission called on its own earlier resumes - specifically its own `_start` call
(its one-time setup), before it ever reached the `thread_yield` loop the existing ring does show.

## What was built

Option (a) from that doc's own suggestion list: a second map, `g_firstResumeTraceRings`
(`std::unordered_map<lua_State*, std::vector<std::string>>`), populated **once** per coroutine and never
reset or overwritten afterward - purely additive to the existing ring, same opt-in,
`--trace-host-calls-before-error=N`-gated, diagnostic-only contract (N=0 keeps it unused, same as
`g_traceRings`).

Chosen over option (b) (a full per-mission call log) because `callMissionStart()` (`tools/lua_host_run.cpp`)
is, by this harness's own existing top doc comment, **only ever called once per mission's coroutine** ("this
harness only ever drives a FRESH start... there is no resume/retry call site here yet") - so the existing
`g_traceRings[co]` ring, read back at the exact instant `threads.run(L, rec, fetchError)` (that function's
one and only `lua_resume` of `co`) returns, and before the mission-drive loop's later scheduler-pass resumes
get a chance to call `resetTraceRing(co)` again, IS already, completely, that coroutine's first resume. No
new call-site instrumentation was needed anywhere else - just one line saving a copy of the ring at that
existing point, a new sibling print function (`printTraceRingFirstResume`, mirroring the existing
`printTraceRingStillSuspended`), and one new call to it alongside the existing one at the `still_suspended`
classification site.

Changes, all in `tools/lua_host_run.cpp`, no other file touched, no `sr3*` library touched:
- New global `g_firstResumeTraceRings` (declared next to `g_traceRings`, with its own doc comment explaining
  why `callMissionStart`'s single-resume contract makes this safe).
- One new line in `callMissionStart()`, right after `threads.run(...)` returns and the watchdog hook is
  cleared: `if (g_traceRingSize > 0) g_firstResumeTraceRings[co] = g_traceRings[co];`.
- New `printTraceRingFirstResume(lua_State*, const std::string&)`, called immediately after the existing
  `printTraceRingStillSuspended(rec->co, funcName)` call at the `still_suspended` classification site in the
  mission-drive loop.
- Usage text for `--trace-host-calls-before-error=N` extended to document the new paired first/last print.

## Verification

Fresh build (`build_verify_firstresume`): clean, 0 errors. `ctest -C Release`: **74/74 passed** (unchanged
count from this worktree's own pre-change baseline - no new test file, see "Tests" below for why).

Default-mode run (no diagnostic flags) against the real archive cache
(`D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache`, 38 `.vpp_pc` containers,
`results/verify_percoroutine_firstresume_default/`): **stderr is completely empty, 0 lines** - with
`g_traceRingSize == 0` (the flag's off-by-default state), both the new map-write (`if (g_traceRingSize > 0)
...`) and the new print function (`if (g_traceRingSize == 0) return;`) are no-ops, exactly the same guard
pattern the pre-existing ring code already uses. Purely additive; never changes real engine behavior.

## The 6-mission question, answered with real data

Ran the same invocation as the percoroutine doc
(`lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out> --tick-budget=721
--probe-unresolved-names --trace-host-calls-before-error=15`, `results/verify_percoroutine_firstresume_mission_drive/`),
then reran with a larger ring (`=200`, `results/verify_percoroutine_firstresume_n100/`) once the first pass
showed 3 of the 6 missions' first-resume rings hitting the 15-call cap (meaning the printed window could
have evicted an earlier call) - the `=200` rerun's rings for all 6 are comfortably under that cap (max 20
calls), so every ring below is the complete, untruncated sequence from the very start of that mission's
`_start` call to its first yield.

All 6 **both still show exactly `thread_yield` alone on their own most recent resume** (same finding as the
percoroutine doc - confirms this rerun reproduces that baseline) **and now additionally show their own
complete first-resume (`_start` call) sequence**:

| mission | wrapper (where parked) | first resume (own `_start` call, in order) |
|---|---|---|
| `m06` | `m06_go_to_dock` | `? coop_is_active cutscene_play zscene_is_loaded cutscene_in character_is_dead convert_to_table convert_to_table type cutscene_play_do cutscene_play_check_done fade_out m06_initialize mission_start_fade_out fade_out player_controls_disable coop_is_active fade_out_block fade_is_fully_faded_out thread_yield` |
| `m10` | `m10_run_get_to_the_front_door` | `? mission_start_fade_out fade_out player_controls_disable coop_is_active fade_out_block fade_is_fully_faded_out thread_yield` |
| `m11` | `m11_go_to_first_base` | `? cutscene_play zscene_is_loaded cutscene_in character_is_dead convert_to_table convert_to_table type cutscene_play_do cutscene_play_check_done fade_out m11_initialize mission_start_fade_out fade_out player_controls_disable coop_is_active fade_out_block fade_is_fully_faded_out thread_yield` |
| `m13` | `m13_goto_a_trigger` | `? coop_is_active m13_initialize mission_start_fade_out fade_out player_controls_disable coop_is_active fade_out_block fade_is_fully_faded_out thread_yield` |
| `m14` | `m14_cellphone_call_wait` | `? mission_start_fade_out fade_out player_controls_disable coop_is_active fade_out_block fade_is_fully_faded_out thread_yield` |
| `m15` | `m15_goto_a_trigger` | `? coop_is_active cutscene_play zscene_is_loaded cutscene_in character_is_dead convert_to_table convert_to_table cutscene_play_do cutscene_play_check_done m15_initialize mission_start_fade_out fade_out player_controls_disable coop_is_active fade_out_block fade_is_fully_faded_out thread_yield` |

(The leading `?` in every row is a call the hook couldn't name - `lua_getinfo`'s own `name` field came back
null for it, printed as `?` by this same convention `printTraceRingIfAny` already used before this change;
most likely the coroutine's own outermost call frame. Not itself a native, callback-registration or
otherwise.)

**Answer: none of the 6 missions calls any callback-registration native (`trigger_enable`, `on_trigger`,
`on_vehicle_enter`, or any other) before entering its wait loop.** All 6 share the identical generic
mission-start shape: disable player controls, check `coop_is_active`, wait for the opening fade-out to
finish (`fade_out`/`fade_out_block`/`fade_is_fully_faded_out`) - three of them (`m06`, `m11`, `m15`) also
play/poll an opening cutscene first (`cutscene_play`, `zscene_is_loaded`, `cutscene_in`,
`cutscene_play_do`/`check_done`) - then every one of the 6 goes straight to `thread_yield` with **no**
callback registered first. This is the harness's own real, measured answer, not an inference from the
wrapper names: each of the 6 is waiting on its own already-known Lua-local state (most plausibly a position
check or a trigger-object query done in pure Lua against data this host already populated, matching the
wrapper names like `*_goto_a_trigger`/`*_go_to_dock`), not on any native call - callback-registration
included - this trace mechanism can observe.

**Where the real 86/83/1 `trigger_enable`/`on_trigger`/`on_vehicle_enter` aggregate calls
(`verdict_stub_hits_with_missions.tsv`, reproduced exactly in this run too) actually come from**: the
`=200` rerun's full stderr log shows them inside **`dlc2_m02`**'s and **`m18`**'s own traces (both on a
*later* resume, not their first - a hook-fire call, in each case immediately before the real engine-state
OPEN item that classifies them `errored`, not `still_suspended`) - e.g. `m18`:
`...hud_set_fake_notoriety trigger_enable on_trigger marker_add_trigger...trigger_enable on_trigger
on_trigger_exit set_current_hit_points`. Neither `dlc2_m02` nor `m18` is one of the 6 target missions,
which independently corroborates the first-resume answer above: the aggregate counts were never from the 6
"architectural wait" missions at all.

## Tests

No new unit test file. This flag's own ring/print machinery (`g_traceRings`, `printTraceRingIfAny`,
`printTraceRingStillSuspended`, and now `g_firstResumeTraceRings`/`printTraceRingFirstResume`) has never had
unit test coverage in this project - every prior pass that touched it (`results/trace_host_calls_20261003.md`,
`results/trace_host_calls_percoroutine_20261003.md`) reported the same "no new test file - CLI/stderr-only
diagnostic addition, not a behavior change needing new test scaffolding" and an unchanged `ctest` count,
which this pass also preserves (74/74, same as this worktree's pre-change baseline). It is exercised purely
by direct invocation against the real mission-drive harness, same as those two prior passes; the existing
`lua_host_run_integration_py` test (the one TSV/column-based integration check that does cover
`start_after_ticks`/`start_still_suspended_at` for a synthetic fixture) does not exercise
`--trace-host-calls-before-error` at all and was not extended, per the task's own instruction not to invent
a test that doesn't match this flag's established convention.

## Judgment calls

- Captured the first-resume ring unconditionally (on error, finish, or yield alike) rather than only on the
  yield/suspend path, since it's a cheap copy of an already-bounded small vector and keeps the capture site
  a single unconditional line right after the one resume this function ever makes - simpler than special-
  casing which outcome to save for.
- Re-ran at a larger ring size (`=200`) once `=15` showed 3 of the 6 missions hitting the cap, rather than
  reporting a possibly-truncated answer - this task asked for which native each mission "actually called,"
  which a capped-and-possibly-evicted window cannot honestly answer for those 3 missions.
