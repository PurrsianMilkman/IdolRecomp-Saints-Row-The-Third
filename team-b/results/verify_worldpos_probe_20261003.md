# get_dist world-position probe extension (2026-10-03)

Peer follow-up once `get_dist`'s new `worldPosition` OPEN gate became the shared next blocker for 5
missions (`dlc3_m02`, `m03`, `m08`, `m20`, `sh03` — see `results/verify_getdist_playernames_20261003.md`).
Extends `--probe-unresolved-names` (same single switch, no new flag) to also widen that specific gate: an
unset `worldPosition` returns a fixed synthetic `(0,0,0)` instead of throwing, logged as probe via a new
`worldPositionProbeReadCount()` counter and a `probed_world_position_reads=` summary line — never a real
baseline measurement, same discipline as every other probe extension this project uses.

Deliberately NOT built on `probedFieldOr()`'s existing per-name-probe gate: a legitimately-resolved name
(one of the 5 CONFIRMED literals, e.g. `"#PLAYER1#"`) can reach `get_dist` exactly as often as a
synthetically-resolved one, and its position is equally unknown either way — no spec gives a real spawn
position for ANY object. Gating on "was the owner name itself probed" would have silently failed to widen
the gate for calls using `"#PLAYER1#"` specifically (confirmed this matters: these 5 missions' own calls
turned out to need exactly this broader gate). New `EngineState::worldPositionOr()` probes on "is the
position itself OPEN", a deliberately separate, slightly broader mechanism, same off-by-default discipline.

## Verification

Rebuilt `lua_host_run` in `build_verify_uirender`: clean. `ctest -C Release`: **61/61 passed**.

Probe-mode mission drive (`--tick-budget=721 --probe-unresolved-names`): `probed_world_position_reads=22`.
Aggregate moved from `still_suspended:23 finished:10 errored:14` to `still_suspended:28 finished:10
errored:9` — the familiar "looks like a regression in aggregate, is actually forward progress" shape (per-
mission diff required, not just the aggregate, per this project's own standing discipline).

**All 5 target missions progress past the worldPosition gate into real, plausible gameplay waits — not a
new error**:
- `dlc3_m02` → `dlc3_m02.lua:1811 (in dlc3_m02_wait_for_ps_trigger)`
- `sh03` → `sh03.lua:546 (in sh03_run)`
- `m03` → `m03.lua:1205 (in m03_steal_a_car)`
- `m08` → `m08.lua:960 (in m08_kill_snipers)`
- `m20` → `m20.lua:817 (in m20_objective_take_control_of_the_car)`

`dlc3_m02_wait_for_ps_trigger`'s name in particular lines up directly with the peer's own earlier
hypothesis for the separate 6-mission cluster (m06/m10/m11/m13/m14/m15): a headless host reaching a real,
named "wait for trigger/objective" checkpoint rather than an engine gap — worth noting as the same
category, not a new finding requiring independent investigation.

Confirmed inert when the flag is off (the only path to `enableWorldPositionProbe()` is inside the existing
`if (probeUnresolvedNames)` block in `lua_host_run.cpp`; `worldPositionOr()`'s own logic reduces to plain
`.get()` semantics whenever its probe flag is false) - this is a direct code-inspection proof, not
re-verified via a redundant empirical run (an earlier attempt to do so compared a probe-mode run against a
non-probe run by mistake and was discarded once the real cause was found).
