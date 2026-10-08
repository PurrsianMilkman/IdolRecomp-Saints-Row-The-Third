# Player-input lock natives flip-on merge verification (2026-10-03)

Merges worktree `agent-a31ef2c3d2ddf97d9`'s follow-up commit `54d199a` (on top of its already-
reviewed `f891618`/`1173679`): `player_controls_disable`/`player_controls_enable`/
`player_movement_disable` (spec-lua-api-behaviour.md Sec7.22/Sec7.7/Sec7.25) are registered in
default mode again, now that Team A's answers to the two open construction-time questions landed
as spec-player-runtime.md Sec2.10 (commit `1a954e8` on main).

## Background

The player-input-layer worktree (U0/U1: query contract, device adapter, control-state stack) had
already been merged earlier this session with these 3 natives held back, registered only under
`--probe-unresolved-names`, because their direct-apply branch reads two values no spec gave at the
time: the control-lock handle global `0x0263ae3c` and a character's `+0x1d99` bit 0x08. Registering
them with strict OPEN-refusal in default mode caused 38 of 49 missions in the 721-tick drive to stop
at tick 2 inside their own start function (`first_error_message` shifting from whatever each mission's
real next blocker was to `character byte +0x1d99 bit 0x08 (controls disabled) is OPEN`) - a real
regression from the then-current baseline, not a neutral change, because a void-returning native's
nil return is invisible to the calling script, so "correctly refuse on OPEN" became the new first
blocker pre-empting whatever blocker was there before.

Team A has now answered both questions (spec-player-runtime.md Sec2.10):
1. The lock handle is CONFIRMED null (0) at session or mission start - zero at image load, and a
   session-start reset forces it to 0. It's self-balancing: its only creator is
   `player_controls_disable`'s own direct-apply push, its only destroyer is `player_controls_enable`'s
   own matching pop, so no other code path ever needs to re-seed it mid-run.
2. A character's `+0x1d99` byte is HIGH CONFIDENCE all-clear (enabled) at spawn: no constructor
   touches it and no initialiser exists, so it rests on the pool allocation being zero-filled - the
   same caveat already accepted for lifeState/killFinalizedFlagE3 (Sec34.5).
3. (Already answered, used earlier): the co-op gate always takes the local-apply branch in
   single-player (Sec3.7).

## Changes

- `CharacterState::controlsDisabled`/`movementDisabled` (`include/sr3luahost/engine_state.h`) and
  `EngineState::playerControlsLockHandle_`: doc comments updated to cite Sec2.10 and the
  zero-filled-allocation reasoning, replacing the earlier "OPEN, refuses" framing.
- `EngineState::getOrCreateCharacter()` (`src/lua_engine_state.cpp`): now seeds
  `c.controlsDisabled.set(false)`/`c.movementDisabled.set(false)` at construction.
- `applySpecInitialState()` (`src/lua_spec_initial_state.cpp`): new item 11 seeds
  `es.playerControlsLockHandle().set(0)`.
- `src/lua_spec_confirmed_stubs.cpp`: `specConfirmedStubNames()`/`registerSpecConfirmedStubs()` now
  register all 3 names unconditionally (not probe-gated). `applyControlsDisabledDirect()` dropped its
  unused `name` parameter and both internal reads are now plain `.get()` instead of the removed
  `controlLockOr()` diagnostic fallback; both call sites in `stub_player_controls_disable()`/
  `stub_player_controls_enable()` updated to match. `stub_player_movement_disable()` needed no change
  (it only ever wrote its own bit, never read either of these two fields).
- Removed as fully dead (confirmed via grep: zero remaining call sites anywhere in the tree):
  `EngineState::controlLockOr()`/`enableControlLockProbe()`/`controlLockProbeEnabled()`/
  `controlLockProbeReadCount()` and the two backing private fields (`engine_state.h`); the
  `enableControlLockProbe()` call and the `probed_control_lock_reads=` summary line in
  `tools/lua_host_run.cpp`.
- `tests/synthetic_luahost_player_controls_test.cpp`: rewrote the assertions that depended on the old
  OPEN-refusal behavior - the fresh-character block now asserts the CONFIRMED-known defaults and runs
  the real disable/enable branch directly instead of expecting a refusal; the gate-false block's
  "stays unknown" check became "stays at its construction-seeded default"; the probe-mode block now
  asserts a synthetic name's disable/enable succeeds outright (both lock fields are seeded regardless
  of whether the owner name itself was probed); the now-dead "control-lock probe" block (which called
  the removed `enableControlLockProbe()`/`controlLockProbeReadCount()`) was deleted outright.
- `tests/synthetic_luahost_test.cpp`: the `openSlotInventory()` "player" area check updated from
  `knownPerArea["player"] == 0` (OPEN) to `== 1` (CONFIRMED, citing Sec2.10 and `applySpecInitialState()`'s
  item 11) - this is the one other place in the suite that observed the lock handle's OPEN/known state
  independently of the natives themselves.

## Verification

Fresh build (`build_verify_player_input_merge`, separate from the worktree's own `build_verify_input`):
clean, exit 0. `ctest -C Release`: **65/65 passed** (63 in the worktree's own build + the 2
font tests from the already-merged text-rendering work, not part of this branch).

True default-mode mission drive (`lua_host_run.exe`, no flags, `--tick-budget=721`, same 49-mission
tagged list) diffed directly against `results/verify_preplayerinput_default/verdict_mission_drive.tsv`
(the true pre-player-input-layer baseline, captured before any of this work existed): **byte-identical**
(`diff` on both files sorted: zero lines differ). Aggregate: `still_suspended:4 finished:10 errored:33
killed:0`, `total_ticks_survived=3174`, `total_stub_calls=60748` - all match the baseline exactly. The
38-mission tick-2 regression is fully gone; this run was repeated both inside the worktree
(`results/verify_player_input_final_mission_drive`) and against the actual merge build
(`results/verify_player_input_merge_mission_drive`), with identical results both times.

`--probe-unresolved-names` mode (same tick budget) diffed against
`results/verify_player_input_after2_mission_drive/verdict_mission_drive.tsv` (the worktree's own
already-probe-fixed baseline from commit `f891618`, itself byte-identical to the true pre-change
probe baseline `verify_player_input_before_mission_drive`): **byte-identical** (sorted diff, zero
lines differ; aggregate `still_suspended:28 finished:10 errored:9`, `total_ticks=21052`,
`total_stub_calls=744195`, all matching). The outcome is genuinely byte-identical output, not rows
that moved forward - the 3 natives were already being called at real volume in the mission-drive
population (37/30 calls for disable/enable per `verdict_stub_hits_with_missions.tsv`), but their
effect (control-state-stack push/pop, input-lock bits) does not change any other engine state that
these missions' subsequent code paths gate on within the tested window.

No golden-scene re-check: this change touches nothing in `tools/sr3_viewer.cpp` or any shared
rendering path.

## Known gaps, carried forward honestly (not closed here)

Per the original worktree's own report (unchanged by this flip-on): binding tables feed integer
records, not real XTBL data (spec-tables-ui-controls.md doesn't publish the needed CBA/CAA/key-name
values); pad-descriptor byte queries, display-string queries, option/look-invert bits, clamping
mouse-sensitivity setters, cursor capture, stick sector tracker, dead-zone/threshold/repeat values are
not implemented. spec-player-runtime.md Sec3 (U2, movement/intent - the closed-form move vector) is
explicit backlog, left for a follow-up.
