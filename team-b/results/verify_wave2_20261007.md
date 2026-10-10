# Verification: wave-2 parallel-dispatch merges (insert_values, first-resume trace, physics boundary)

**Date**: 2026-10-07
**HEAD after all three merges**: `e47e2b6` (chain: `c3268e2` → `e777162` → `fa3a35c` → `e47e2b6`)
**Fresh build dir**: `build_verify_wave2` (not reused from any source agent's own worktree build)

## Merges covered

1. **`e777162`** — first-resume trace ring (spec-adjacent diagnostic tool, `tools/lua_host_run.cpp`).
   Source branch `worktree-agent-ab1bcb6c274ef47b3`, commit `9fa4975`.
2. **`fa3a35c`** — `insert_values` localisation wiring (`src/vint_text.cpp`'s `formatInsertions`).
   Source branch `worktree-agent-ab938aec543f3eda3`, commit `3a14adf`.
3. **`e47e2b6`** — physics runtime boundary, new `sr3physics` library (spec-physics-runtime.md).
   Source branch `worktree-agent-a21e16958708d7ec0`, commit `0514ba1`.

All three branched from the same point (`c3268e2`), merged sequentially with `git merge --no-commit
--no-ff`, each committed promptly after confirming `git status --short` showed only the expected files
staged (spec files Team A had dirty — `spec-anim-runtime.md`, `spec-lua-api-behaviour.md`,
`spec-player-runtime.md` — were never touched). `CMakeLists.txt` auto-merged cleanly for the physics
branch with zero manual conflict resolution.

## What each merge does

**First-resume trace ring**: adds `g_firstResumeTraceRings`, captured once per coroutine alongside the
existing (overwritten-every-resume) `g_traceRings`, plus a new `printTraceRingFirstResume()` print.
Purely additive/opt-in — confirmed a default-mode run (no diagnostic flags) against the real archive cache
produces zero stderr output, identical to before. **Real-data finding**: none of the 6 "architectural
wait" missions (`m06`, `m10`, `m11`, `m13`, `m14`, `m15`) register any callback-registration native before
their wait loop — all 6 show identical generic mission-start boilerplate (disable controls, check
`coop_is_active`, wait for the opening fade, three also play an opening cutscene) then go straight to
`thread_yield`. The real `trigger_enable`/`on_trigger`/`on_vehicle_enter` aggregate (86/83/1 calls) traces
to `dlc2_m02` and `m18` instead — neither a target mission. Full write-up:
`results/trace_host_calls_firstresume_20261007.md`.

**`insert_values` localisation wiring**: `{N}` with a string value now resolves via
`LeStringsLookup::lookupByKey` (slot `+0x00` semantics — hash the string, look it up, fall back to the
plain string on a miss). `{N:text_tag_crc}` now resolves via `LeStringsLookup::lookup(hash)` directly
(slot `+0x04` semantics — never re-hashed as a string). Both per spec-vint-render.md §14.2 cross-referenced
to spec-localization-format.md §6/§9. `.le_strings`'s existing API already exposed both shapes needed —
no extension required. The `0x00e23f40` number/boolean-to-text branch stays `OpenStateError`/`r.open`,
unchanged — no spec section anywhere in this repo covers its formatting rules.

**Physics runtime boundary** (new library `sr3physics`): the variable-step frame contract (Δt clamped to
[1/120, 1/20]s), the physics-window mutation discipline (`ActivationQueue`, keyed `BodyStateQueue<T>`,
generic `DroppedSetter` for the 3 silently-dropped setters), the up-to-10-pass `CharacterProxyStepper`
lock-step batching, a structurally queue-only `CharacterProxyListener` (callbacks receive only an
`EnqueueOnly<T>&`, no getter, no world handle), and `PhysicsLifecycle`'s full 8-step shutdown order. No
real Havok, no real collision/solver, no real multithreaded scheduler — `NullPhysicsBackend`/
`InlineGameJobScheduler` preserve only the observable call shape. A genuine spec ambiguity was flagged
(not resolved by guessing): §4.3's numbered step list places the two queues' replay *inside* the window
(step 5, before the step-6 decrement), while a separate paragraph says they're replayed *after* the window
closes — resolved by giving the replay path direct-apply callbacks that never consult `window.isOpen()`
(only the public request wrappers do), matching the literal step order without risking a livelock; flagged
in the source's own doc comments for reviewer attention.

## Verification performed (this pass, independent of each source agent's own run)

- Fresh build directory `build_verify_wave2`, configured via `cmake -S . -B build_verify_wave2 -G "Visual
  Studio 17 2022" -A x64` (clean configure), built Release (`cmake --build build_verify_wave2 --config
  Release -j 8`, exit 0), against the actual merge-chain HEAD `e47e2b6`.
- `ctest -C Release`: **100% tests passed, 75/75** (1 more than the camera-merge baseline of 74 — the new
  `synthetic_physics_runtime_test`, confirmed present via `ctest -N | grep physics`).
- `git status --short` confirmed, before each commit, that only the expected files were staged per merge
  (verified file lists match each source branch's own diffstat against its `c3268e2` branch point exactly)
  — no spec files or unrelated changes rode into any of the three commits.

## Outcome

All three merges verified clean on a fresh build. Worktrees `agent-ab1bcb6c274ef47b3`,
`agent-ab938aec543f3eda3`, `agent-a21e16958708d7ec0` are ready for cleanup.
