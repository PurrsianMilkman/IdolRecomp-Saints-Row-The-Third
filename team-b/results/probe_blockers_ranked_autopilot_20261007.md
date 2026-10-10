# --probe-autopilot: next blockers past still_suspended, ranked (2026-10-07)

New diagnostic flag `--probe-autopilot` added to `tools/lua_host_run.cpp` (opt-in, off by
default, same category as `--probe-unresolved-names`/`--trace-host-calls-before-error=N`).
Real mission drive, `lua_host_run <real cache> tools/lua_all_registered_1490_tagged.txt <out>
--tick-budget=721 --probe-unresolved-names --probe-autopilot`, against the real archive cache
(`D:\Project Crreish\Saints Row 3 CRREISH\packfiles\pc\cache`). Raw output:
`results/verify_probe_autopilot_mission_drive/`.

## What `--probe-autopilot` does

For every mission classified `still_suspended` at tick-budget exhaustion, it reads that
mission's own first-resume trace (`g_firstResumeTraceRings`, from the already-merged
`e777162` pass) and checks whether the mission called a CONFIRMED callback-registration
native (one that takes a callback-name string argument, to be invoked LATER) before it ever
reached its wait loop:

- **If one is found**, the tool reads the native's own real arguments (not guessed — captured
  directly off the Lua stack at the `LUA_HOOKCALL` instant the registration call itself was
  made, a new capture this task added: `g_registrationCallsThisResume`/
  `g_firstResumeRegistrations`), then dispatches the registered callback-name global exactly
  once (zero arguments — no spec section this project has read establishes what arguments a
  *fired* callback receives, only what a *registering* call's own arguments are, so no shape
  is invented there), tagged `SYNTHETIC` in `[probe-autopilot]` stderr output and in
  `verdict_mission_drive.tsv`'s new `probe_autopilot_*` columns. It then runs a bounded
  30-tick follow-up window (same per-tick shape as the main drive loop, just shorter) to see
  whether the mission's own thread advances, and if so what it hits next.
- **If none is found**, it records that explicitly as "nothing to synthesize" rather than a
  silent empty result — per this task's own framing, that null result is itself a real
  finding (the wait is most likely satisfied by polled world state, not a callback).

## Real result: all 28 still_suspended missions — nothing to synthesize

`missions_start_after_ticks=still_suspended:28 finished:10 errored:9 killed:0` this run.
**Every one of the 28 still_suspended missions' first-resume traces showed NO confirmed
callback-registration native call at all** — `probe_autopilot_synthesized_events=0`,
`probe_autopilot_nothing_to_synthesize=28`. Independently cross-checked directly against the
raw stderr first-resume rings (`results/verify_probe_autopilot_mission_drive/stderr.log`):
none of the 8 confirmed native names (`on_trigger`, `on_trigger_exit`, `on_death`,
`on_vehicle_destroyed`, `on_attack_performed`, `on_take_damage`, `on_revived`,
`on_qte_animation_trigger`) appears in any of the 28 missions' first-resume rings — so this
isn't a matching bug, it's the real, measured shape of this corpus.

This extends the already-established 6-mission finding
(`results/trace_host_calls_firstresume_20261007.md`: `m06`/`m10`/`m11`/`m13`/`m14`/`m15`,
all present below) to **every** still_suspended mission this run reached, not just those 6:
every one of them runs generic mission-start boilerplate (disable controls, check
`coop_is_active`, wait for a fade/cutscene, sometimes poll a group/scene/zscene helper) and
goes straight into its own `thread_yield` poll loop, with no callback registered first. The
"next blocker" these missions are actually waiting on is apparently a polled world-state
condition the script checks in its own loop body, not a registered callback — something this
trace mechanism (which only observes native calls, not pure-Lua condition checks) cannot see
further into without reading the scripts themselves (out of scope/clean-room boundary).

### The 28 missions (1 shared outcome: "nothing to synthesize"), with where each one waits

| mission | waiting at (own wait loop) |
|---|---|
| `dlc2_m03` | `dlc2_m03.lua`:1501 (in `dlc2_m03_first_transmitter`) |
| `dlc3_m01` | `dlc3_m01.lua`:1608 (in `dlc3_m01_search_for_tag`) |
| `dlc3_m02` | `dlc3_m02.lua`:1811 (in `dlc3_m02_wait_for_ps_trigger`) |
| `m01` | `m01.lua`:2503 (in `m01_initialize_checkpoint`) |
| `m02` | `game_lib.lua`:456 (in `group_create_check_done_loop`) |
| `m03` | `m03.lua`:1205 (in `m03_steal_a_car`) |
| `m04` | `m04.lua`:507 (in `m04_get_into_car`) |
| `m06`* | `m06.lua`:1060 (in `m06_go_to_dock`) |
| `m08` | `m08.lua`:960 (in `m08_kill_snipers`) |
| `m09` | `m09.lua`:2099 (in `m09_go_to_boat`) |
| `m10`* | `m10.lua`:1465 (in `m10_run_get_to_the_front_door`) |
| `m11`* | `m11.lua`:1239 (in `m11_go_to_first_base`) |
| `m12` | `m12.lua`:1290 (in `m12_goto_a_trigger`) |
| `m13`* | `m13.lua`:2955 (in `m13_goto_a_trigger`) |
| `m14`* | `m14.lua`:2265 (in `m14_cellphone_call_wait`) |
| `m15`* | `m15.lua`:1013 (in `m15_goto_a_trigger`) |
| `m16` | `m16.lua`:2677 (in `m16_transform_player_into_human_avatar`) |
| `m17` | `m17.lua`:1224 (in `m17_hijack_heli`) |
| `m20` | `m20.lua`:817 (in `m20_objective_take_control_of_the_car`) |
| `m22` | `m22.lua`:1593 (in `m22_process_battle_zone_sequence`) |
| `m24` | `m24.lua`:950 (in `m24_hack_terminal`) |
| `mm_p_01` | `mm_p_01.lua`:87 |
| `mm_p_03` | `mm_p_03.lua`:342 (in `mm_p_03_go_to_storefront`) |
| `mm_p_07` | `mm_p_07.lua`:116 (in `mm_p_07_run`) |
| `sh01` | `sh01.lua`:433 (in `sh01_run`) |
| `sh02` | `sh02.lua`:680 (in `sh02_run`) |
| `sh03` | `sh03.lua`:546 (in `sh03_run`) |
| `sh04` | `sh04.lua`:773 (in `sh04_wait_for_slots`) |

`*` = one of the 6 missions `results/trace_host_calls_firstresume_20261007.md` already
answered this question for — reproduced here with the same answer, now via the dedicated
`--probe-autopilot` mechanism rather than by reading the printed rings by eye.

No ranking by "shared next blocker" was possible beyond this single bucket, since nothing
advanced past any wait for any of the 28 — there is no second-order blocker to rank.

## Natives checked against this task's own example list, and excluded

This task named `trigger_enable`/`on_trigger`/`on_vehicle_enter`/`on_cellphone` as the
candidate family. Checked each directly against `spec-lua-api-behaviour.md` rather than
assumed:

- **`trigger_enable` (Sec1.1) — EXCLUDED.** No callback argument at all (string trigger name,
  bool enable, numeric flag) — a toggle on an *already-registered* trigger, not a
  registration call.
- **`on_cellphone` — EXCLUDED, does not exist.** Not in `spec-lua-api-behaviour.md`,
  `spec-lua-bindings.md`, or the 1490-name tagged registration list. The only
  cellphone-related natives are `audio_play_for_mission_cellphone`/
  `audio_remove_mission_cellphone` (immediate audio actions) and
  `cellphone_animate_start_do`/`_stop_do` (immediate actions) — none register a callback.
- **`on_vehicle_enter` (Sec4.12) — EXCLUDED.** The spec's own CONFIRMED "Body" reading
  describes an *immediate* numeric-event post (codes `0x16`/`0x17`/`7`), not a deferred
  dispatch; a separate OPEN note in the same section flags an alternative "arg 1 might
  actually be a callback name" hypothesis that the section itself calls `NEEDS-EXE` and
  explicitly unresolved. The spec contradicts itself on whether this is even a registration
  native, so it was excluded rather than guessed at.
- **`on_vehicle_enter_water` (Sec14.30) — EXCLUDED** for the same reason, by analogy only
  (same per-object hook-slot family), not independently re-derived this pass.

Also surveyed and excluded as a genuinely different mechanism shape (a stored
responder/property name, or an actual Lua closure, not a plain callback-name string):
`game_sign_into_network`, `vint_internal_dataresponder_request`, `game_peg_load_with_cb`,
`vint_set_property`'s callback-typed case.

**The 8 natives `--probe-autopilot` actually acts on** (all CONFIRMED by direct spec reading
to take a callback-name string argument, registering it for later): `on_trigger` (Sec1.2),
`on_trigger_exit` (Sec1.7), `on_death` (Sec3.2), `on_vehicle_destroyed` (Sec3.3),
`on_attack_performed` (Sec3.12), `on_take_damage` (Sec3.13), `on_revived` (Sec10.8),
`on_qte_animation_trigger` (Sec10.7, single-argument/global, no target name).

## Mechanism verification: the real corpus never exercises the dispatch path — a manual smoke test does

Since all 28 real still_suspended missions landed in the "nothing to synthesize" bucket, the
actual dispatch-and-follow-up code path (the part that fires a SYNTHETIC callback and reports
what breaks next) has zero coverage from the real-data run above. To confirm it actually works
rather than just reporting an untested null result everywhere, a throwaway synthetic cache
(NOT part of the committed test suite — see "Tests" below) was built with one mission:

```lua
function smoke_cb() smoke_flag = true end
function dlc1_mm_06_start(cp, restart)
  on_trigger('smoke_cb', 'smoke_trigger')
  while not smoke_flag do thread_yield() end
  error('SMOKETEST_PAST_WAIT_' .. tostring(smoke_flag))
end
```

Run with `--tick-budget=5 --probe-autopilot` against this throwaway cache
(reusing the real `dlc1_mm_06` stem already in `tools/mission_package_per_container.tsv` so no
TSV/CMake changes were needed), the real, measured result was:

```
probe_autopilot_synthesized_events=1 distinct mission(s) with a SYNTHETIC callback dispatch: [dlc1_mm_06(on_trigger)]
dlc1_mm_06: ... probe_autopilot=[on_trigger("smoke_cb") dispatch=[SYNTHETIC dispatch ok (no Lua error)]
  next_blocker=[SYNTHETIC-probe next error: dlc1_mm_06_start: [string "dlc1_mm_06.lua"]:7: SMOKETEST_PAST_WAIT_true]]
```

This confirms, end to end, on a real (if synthetic) run: the registration's own arguments are
correctly recovered from the trace (`"smoke_cb"`), the callback is correctly dispatched once
with zero arguments, the dispatch's own success/failure is correctly reported, the mission's
real coroutine is correctly re-resumed in the follow-up window, and the genuinely NEXT error it
hits (after its wait loop condition was flipped by the synthesized callback) is correctly
captured with file/line/message. (The smoke test's own unrelated `first_error=[attempt to call
a nil value]` in that run's baseline fields is `fireConfirmedHooks()`'s pre-existing,
already-established Group-2-unconditional-call behavior hitting an undefined lifecycle hook
name — an artifact of the smoke cache deliberately providing no `game_lib.lua`/real lib
globals, not a probe-autopilot bug; it is independent of, and does not affect, the
`probe_autopilot_*` fields.)

## Build / tests

Fresh build (`build_verify_probeautopilot`): clean, 0 errors, 0 warnings in `lua_host_run.cpp`
itself (a handful of pre-existing warnings in unrelated `tools/validation/*.cpp` files, not
touched this pass). `ctest -C Release`: **77/77 passed**, including
`lua_host_run_integration_py` (the one test that drives `lua_host_run` end to end, default
flags only, no `--probe-autopilot`) — confirms this change does not alter baseline drive
behavior. (One intermediate build attempt did fail — a declaration-order bug,
`g_registrationCallsThisResume` used in `resetTraceRing()` before its own declaration further
down the file — caught by the build itself before this report, fixed by moving the
map's declaration earlier; not a silent/escaped defect.)

No new unit test file. Same established convention this whole ring/print machinery
(`g_traceRings`, `printTraceRingIfAny`, `g_firstResumeTraceRings`, and now
`g_registrationCallsThisResume`/`g_firstResumeRegistrations`/`isCallbackRegistrationNative`)
has followed since it was introduced: every prior pass that touched it
(`results/trace_host_calls_20261003.md`, `results/trace_host_calls_percoroutine_20261003.md`,
`results/trace_host_calls_firstresume_20261007.md`) reported "no new test file — CLI/stderr-only
diagnostic addition, not a behavior change needing new test scaffolding," and the existing
`lua_host_run_integration_py` test (the one TSV/column-based integration check covering
`start_after_ticks`/`start_still_suspended_at`) does not exercise any of
`--trace-host-calls-before-error`/`--probe-unresolved-names`/`--probe-autopilot` and was not
extended, per this task's own instruction not to invent a test that doesn't match this flag
family's established convention. The manual smoke test above (not committed, scratch-only)
fills the "does the untested-by-real-data code path actually work" gap a unit test would
normally cover.

## Judgment calls

- Took the FIRST callback-registration call in a mission's first-resume trace as "that ONE
  event" when more than one might be present (none of the 28 real missions had this
  ambiguity, so it never mattered in practice this run — documented for the case it does).
- Zero-argument dispatch for the fired callback, since no spec section this project has read
  establishes a FIRED (as opposed to *registered*) trigger/character/vehicle/QTE callback's
  own argument shape — only the registration call's arguments are CONFIRMED. Calling with
  zero arguments is a CHOSEN, explicitly-flagged stand-in, not a spec claim.
- Auto-raises `--trace-host-calls-before-error` to 200 when `--probe-autopilot` is given and
  the caller didn't set it explicitly (reusing the exact ring size
  `results/trace_host_calls_firstresume_20261007.md` already measured as large enough to avoid
  truncating a mission's real first-resume sequence), printed to stdout so it's never silent.
- Follow-up window (`kProbeAutopilotFollowUpTicks = 30`, CHOSEN) is deliberately much smaller
  than the main `--tick-budget` (721 this run) — a real blocker genuinely unblocked by the
  synthesized event should show up almost immediately on the very next resume; this is "does
  it move at all," not "is it mid-way through a long legitimate wait" (that question was
  already answered by the real tick-budget loop before classification).
- Excluded `trigger_enable`/`on_cellphone`/`on_vehicle_enter`/`on_vehicle_enter_water` after
  direct verification rather than trusting this task's own example list at face value — see
  "Natives checked" above; two of the four named examples turned out not to belong in this
  family at all.
