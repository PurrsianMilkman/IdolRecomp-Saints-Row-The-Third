# get_dist / player_names_get_all implementation verification (2026-10-03)

Implements `get_dist` (spec-lua-api-behaviour.md Sec4.2, cleared for implementation same day after
Team A struck its own stale "not yet cleared" review line) and `player_names_get_all` (Sec51.4, ranking
tranche 17). Both were generic stubs feeding nil directly into a script-level error, found via the new
`--trace-host-calls-before-error` diagnostic's first real use (`results/trace_host_calls_20261003.md`).

`get_dist`: 2 mandatory object names + 2 optional booleans (vertical-axis handling), CONFIRMED contract.
Each name resolves through the same shared `objectResolves()` map every other resolver in this project's
scope uses; on a confirmed-false/unresolved name the CONFIRMED FLT_MAX "could not compute" sentinel fires
(never an error, per Sec4.2's own text). On a resolved name, reads a new `worldPosition` field (added to
both `CharacterState` and `VehicleState`, OPEN by default — no spec gives a CONFIRMED spawn position for
any object, and the save format doesn't carry one either, spec-save-format.md Sec6.6, confirmed by Team A
2026-10-03) and computes the Euclidean distance, with the vertical (Y, per spec-physics-runtime.md's own
gravity constant) axis handling per the two optional booleans. Every resolved name is treated as
character-shaped — the same simplification already established elsewhere in this file for other
dual/triple-typed resolvers (`stub_on_take_damage`).

`player_names_get_all`: confirmed zero-argument real call shape (`verdict_stub_hits_with_missions.tsv`,
checked directly). This host models single player only, so it always returns a 1-based table with exactly
one entry — the local player's own name sentinel, reusing the existing `"#PLAYER1#"` literal this project
already treats as CONFIRMED-resolving (Sec29.4), rather than inventing a different display-name format.

## Verification

Fresh rebuild (`build_verify_uirender`, same directory already verifying the UI renderer merge): clean,
exit 0. `ctest -C Release`: **61/61 passed**, including the refusal-stress test (every spec stub called
with a spread of adversarial argument shapes through both protected and unprotected paths) — confirms
neither new stub can escape the established `OpenStateError`/Lua-error refusal path.

Probe-mode mission drive (`--tick-budget=721 --probe-unresolved-names`): aggregate unchanged
(`still_suspended:23 finished:10 errored:14`), but per-mission detail shows real, intended movement:

- **`player_names_get_all` fixed its target blocker**: `sh03` and `dlc3_m02` no longer hit
  `game_lib.lua:1488`'s `player_list` nil-index. Both progress further and now hit a DIFFERENT, later
  blocker — the new `get_dist` worldPosition OPEN item (expected, see below).
- **`get_dist` now fails cleanly instead of crashing on a raw nil-compare**: `m08` no longer hits
  `m08.lua:910: attempt to compare nil with number` — it now errors with a correctly labelled
  `engine state character world position (get_dist's own 0x00a455a0 lookup) is OPEN
  (spec-lua-api-behaviour.md Sec4.2)`. Per this project's own standing discipline, a clean labelled OPEN
  beats an opaque nil-compare even though the mission doesn't advance past this point yet (still classified
  `errored` — same bucket as before, just a correct diagnosis instead of an accidental one).
- **5 missions now converge on this one new OPEN item** (`dlc3_m02`, `m03`, `m08`, `m20`, `sh03`) — a
  clear, concrete next blocker once a real position-tracking/spawn system exists (Team A is separately
  scoping this; `#PLAYER1#` stays OPEN per their explicit instruction, not seeded from save data).

## Verdict

Both names implemented per their CONFIRMED contracts, faithfully, with no fabricated values. Real forward
progress for `player_names_get_all`'s target missions; a correctly-diagnosed (not yet unblocked) outcome
for `get_dist`'s, pending the separate world-position/spawn-logic work already in scope with Team A.
