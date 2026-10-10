# Verification: Player runtime U4 — camera mode machine + gameplay follow camera

**Date**: 2026-10-07
**Merge commit**: `e89eede` (parents `a93d68e` + `3a1eb03`)
**Source branch**: `worktree-agent-afc77adb8295df634` (worktree `.claude/worktrees/agent-afc77adb8295df634`)
**Spec**: spec-player-runtime.md §4 (camera mode machine, gameplay follow camera)

## Provenance

This work was produced by a background agent that was killed by the 2026-10-04 00:28
weekly rate limit (not a genuine failure — exempt from the escalation ladder, see
`agent-escalation-tooling.md`). On resume, the worktree was found to hold complete,
uncommitted work, already self-verified by the agent's own pre-kill build/test run
(`build_verify_cam_ctest.log`, 71/71 passed, read directly). The work was reviewed
directly (full read of `camera.h`'s doc header and `player_follow_camera.cpp`'s header)
before being committed as `3a1eb03` on its branch and merged into `main` as `e89eede`.

## What was built

- `include/sr3player/camera.h` (568 lines) — camera mode-stack machine: move-to-top
  push, by-value pop, deferred set-base, no exit callbacks.
- `include/sr3player/camera_free_seed.h` (149 lines).
- `src/player_camera.cpp` (415 lines) — mode machine implementation.
- `src/player_follow_camera.cpp` (521 lines) — mode 0 (gameplay follow camera): look-input
  yaw/pitch integration, the angle block, the 4-kind submode blend (snap / hold / linear /
  symmetric-ease, including the 15-unit-squared instant-snap rule), sway, orientation
  construction, placement with a 0.12-radius collision sphere cast, shoulder offset, and the
  working/committed/previous output-set commit (consumers read only the committed set).
- `tests/synthetic_player_camera_test.cpp` (1120 lines).
- `include/sr3player/movement.h` — cross-reference note: the look slot `0x005ab6a0` is now
  implemented via the camera.

Scope: mode 0 (gameplay follow camera) only, built for real. Every other mode (debug,
death, satellite, cutscene, cinema-editor, store, showcase) is left as a
structurally-present-but-unimplemented row — this project has none of those subsystems.
Vehicle/combat/weapon-dependent pan-rate dampening and submode selection are gated
behind injectable `OpenValue` defaults, matching the idiom already established in
`player_movement.cpp`.

## Judgment calls (flagged by the source agent, reviewed and accepted)

- **Spec conflict, resolved in favor of the CONFIRMED-by-listing sections**:
  spec-player-runtime.md §4.3's "Consequence" paragraph states camera-relative movement
  reads the *previous* frame's committed rows, but §2.7, §4.5 step 1, and §3.5.1 step 3
  together imply the move slot reads the *working* rows rebuilt this frame. The
  CONFIRMED listing sections were followed; the conflict is documented in the source
  files' own doc comments.
- **CHOSEN rotation convention** (not spec-confirmed, explicitly labeled): Y-up,
  `R(pitch, roll, heading)`, forward = `(sin h cos p, -sin p, cos h cos p)`, positive
  pitch looks down — chosen so a positive `y_dist` places the camera above the pivot
  per §4.7 step 13.

## Verification performed (this pass, independent of the source agent's own run)

- Fresh build directory `build_verify_camera` (not the agent's own `build_verify_cam`),
  configured via `cmake -S . -B build_verify_camera -G "Visual Studio 17 2022" -A x64`,
  built Release (`cmake --build build_verify_camera --config Release -j 8`), against the
  actual merge commit `e89eede` (confirmed via `git log -1 --format="%H %P" HEAD` before
  and after the build — no concurrent commit landed during the build).
- `ctest -C Release` against `build_verify_camera`: **100% tests passed, 74/74**
  (3 more than the source agent's own pre-merge 71/71 count — the extra 3,
  `real_anim_node_graph_test` among them, come from `main` tests gained between the
  agent's branch point and the merge, not from this change).
- `git status --short` confirmed only the 7 expected files were staged pre-merge
  (`CMakeLists.txt` M; `include/sr3player/camera.h`, `camera_free_seed.h` A;
  `include/sr3player/movement.h` M; `src/player_camera.cpp`,
  `src/player_follow_camera.cpp` A; `tests/synthetic_player_camera_test.cpp` A) — no
  spec files or unrelated changes rode into the merge.

## Outcome

Merge verified clean. Worktree `.claude/worktrees/agent-afc77adb8295df634` is ready for
cleanup.
