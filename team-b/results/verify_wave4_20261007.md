# Verification: wave-4 merge (audio runtime boundary)

**Date**: 2026-10-07
**HEAD after merge**: `1c74029` (chain: `d54ff53` → `1c74029`)
**Fresh build dir**: `build_verify_wave4` (not reused from the source agent's own worktree build)

## Merge covered

**`1c74029`** — audio runtime boundary, new library `sr3audioruntime` (spec-audio-runtime.md). Source
branch `worktree-agent-a714d20f37cc8fa7e`, commit `06660b9`.

## What it does

The 25-function Wwise calling contract (`WwiseBackend`, one virtual method per §1.1–1.4 crossing plus one
per §3.2/§3.4 init/shutdown sub-step), the play-record (§2.1) and voice (§2.2) record layouts, and all 14
§5 behaviours reproduced faithfully behind `NullWwiseBackend`:

1. `SetMultiplePositions` orientation corruption (source orientation-z stored into destination
   orientation-x for every node — the real bug, reproduced not fixed).
2. Multi-position count capped defensively at 16 (the task's own instruction — guard, not a literal
   overrun reproduction).
3. Voice-line post has no engine-up guard; leaves `posted=true` on failure (vs. `false` for sound-effect).
4. Teardown never calls `stopPlayingID`.
5. Pause/resume are event-wide (`executeActionOnEvent` takes no playing id).
6. Smoothed global RTPC's asymmetric decay/snap (`(current-target)*k` only past the 0.05 threshold).
7. The 200-slot global RTPC cache drops sub-0.001 deltas and full-table inserts.
8. Listener only sent while the arm counter is positive.
9. Per-voice positional/obstruction updates are time-gated, not frame-gated (33ms/5s, ±7/100 slew).
10. Three one-tick latencies (switch-dirty skip, lazy-register idle tick, Duration-callback-then-tick
    before "playing").
11. Message-pool exhaustion drops and counts.
12. Bank callback reports bank id, not the cookie passed to `loadBank`.
13. Init has no rollback on a mid-sequence Wwise failure.
14. Codec 4 (Vorbis) only, voice lines addressed by file id.

A real latent bug was caught and fixed during the source agent's own self-review (not by a failing test):
the stop-wrapper's synchronous paths (stopping an already-`Failed` record, or a failed `StopPlayingID`
call) moved a record into a terminal state without marking it dead — would have leaked it forever instead
of unlinking it next tick. Fixed and covered by a new regression test (`testStopOnFailedRecordDoesNotLeak`).

§6 OPEN items left `OpenValue`/`OpenStateError`, not invented: the per-voice RTPC tiered changed-test
(item 3), the code-7 side call's purpose (item 5), the four unnamed config pointers' identity (item 6),
`k`/the RTPC targets/the listener's initial position (item 7, except the smoothed-RTPC pair's confirmed
1.0 starting value, which is a plain float not an `OpenValue`).

## Verification performed (this pass, independent of the source agent's own run)

- Fresh build directory `build_verify_wave4`, configured via `cmake -S . -B build_verify_wave4 -G "Visual
  Studio 17 2022" -A x64` (clean configure), built Release (`cmake --build build_verify_wave4 --config
  Release -j 8`, exit 0), against the actual merge commit `1c74029`.
- `ctest -C Release`: **100% tests passed, 78/78** (1 more than the wave-3 baseline of 77 —
  `synthetic_audio_runtime_test`, confirmed registered via `ctest -N | grep audio`; its 18 named behaviour
  tests run as sub-assertions inside that single ctest entry, same convention as the physics/vehicle units).
- `git status --short` confirmed only the expected files were staged before commit — no spec files rode
  into it.

## Outcome

Merge verified clean on a fresh build. Worktree `agent-a714d20f37cc8fa7e` is ready for cleanup.
`build_verify_wave4` will be deleted immediately after this doc is committed, per the project's current
disk-space discipline.
