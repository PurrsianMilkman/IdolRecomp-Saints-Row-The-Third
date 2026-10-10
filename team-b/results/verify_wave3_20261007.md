# Verification: wave-3 parallel-dispatch merges (vehicle minimum drivable slice, player<->animation coupling)

**Date**: 2026-10-07
**HEAD after both merges**: `373613e` (chain: `ba2cc7b` → `bbd627d` → `373613e`)
**Fresh build dir**: `build_verify_wave3` (not reused from either source agent's own worktree build)

## Merges covered

1. **`bbd627d`** — vehicle minimum drivable slice (U6.0/U6.1/U6.3/U6.4/U6.6), new library
   `sr3player_vehicle`. Source branch `worktree-agent-a33bbd7e85ffdd7f5`, commit `4319505`.
2. **`373613e`** — player<->animation coupling, new library `sr3playeranim`. Source branch
   `worktree-agent-ad6cff909ee924c13`, commit `3550a14`.

Both branched from `e89eede` (the already-merged camera commit), merged sequentially with `git merge
--no-commit --no-ff`, each committed promptly after confirming `git status --short` showed only the
expected files staged. `CMakeLists.txt` auto-merged cleanly for both — zero manual conflict resolution.

## What each merge does

**Vehicle minimum drivable slice**: a vehicle-keyed occupancy registry (U6.0, 8 slots/vehicle, capacity
48); the full §9.3 element-selection key-builder table plus wildcard matcher against
`sr3tables_vehicle_world::VehicleInteractionAnimationElement` (U6.1); the real instant-enter route
`0→1→(2→)4→9` plus the 800ms local-player-only stick-abort cancel that never fires the completion
callback (U6.3 — Team A synced spec-player-runtime.md §10 as `4ae8d22` mid-task; the agent read it
directly from main and replaced its `OpenStateError` stub); the seating commit writing condition row
`+0xcc8` = 8 (seat 0) / 9 (elsewhere) (U6.4); the exit request plus all 6 non-animated exit states
`0x14`–`0x19` (U6.6). **5 of 35 states confirmed unreachable** (`0x10`, `0x12`, `0x1a`, `0x1e`, `0x20`),
derived from a full state-dispatch sweep across every state `0x00`–`0x22`, not asserted — matches the
spec's own §7.0 item 2 count exactly. Unit bodies for U6.2/U6.5/U6.7/U6.8 are explicitly stubbed
(generic `StubOnEntry`, correctly routed to but not internally implemented) since they need systems
outside this slice's scope (real animation playback, inventory switching, distance-ordered entry
queues, camera hand-off).

**Player<->animation coupling**: the field→control-parameter writer (spec-anim-runtime.md §4.2's full
census, writing directly into a `GraphInstance`'s alias slots), driving the real `"player state machine"`
graph (confirmed via direct archive inspection that the internal name has a space — `findGraph` needs the
space, not an underscore) through the existing U2/U3 loader+evaluator every frame; a scoped-down post-step
(`PostOutputList()`) turning the real 32-entry output list into a T/E root-motion split using each clip's
loop bit as an explicit, documented CHOSEN proxy for the real engine's missing layer-flag `0x01`; the
playback-rate formula (spec-player-runtime.md §5.4.3 step 7, clamped [0.1, 1.5]); a camera-target hookup.
One judgment call flagged: the graph's `locomotion` control parameter (gates idle↔moving) has no writer in
§4.2's own census — written from `PlayerMovementState::locomotionState` (+0xccc) instead, same concept,
documented as a bridge-owned decision.

**Not included in this merge** (confirmed lost, not recoverable — see HANDOFF.md's 15th rewrite): the
deterministic viewer-demo/golden piece the coupling agent sub-dispatched. Its own CMakeLists.txt comment
originally claimed a `walk` viewer command existed; corrected before this merge's commit to describe
`sr3playeranim`/`sr3tables_animation` as linked-but-unused, since no code anywhere calls them yet.

## Verification performed (this pass, independent of each source agent's own run)

- Fresh build directory `build_verify_wave3`, configured via `cmake -S . -B build_verify_wave3 -G "Visual
  Studio 17 2022" -A x64` (clean configure), built Release (`cmake --build build_verify_wave3 --config
  Release -j 8`, exit 0), against the actual merge-chain HEAD `373613e`.
- `ctest -C Release`: **100% tests passed, 77/77** (2 more than the wave-2 baseline of 75 — the new
  `synthetic_player_vehicle_occupancy_test` and `synthetic_player_anim_bridge_test`).
- `git status --short` confirmed, before each commit, that only the expected files were staged per merge —
  no spec files or unrelated changes rode into either commit.

## Outcome

Both merges verified clean on a fresh build. Worktrees `agent-a33bbd7e85ffdd7f5` and
`agent-ad6cff909ee924c13` are ready for cleanup. `build_verify_wave3` itself will be deleted immediately
after this doc is committed, per the project's current disk-space discipline.
