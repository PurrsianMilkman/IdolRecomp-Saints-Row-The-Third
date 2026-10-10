# Verification: wave-8 combined merge (AI runtime, character_is_in_vehicle, anim U4-U7 check)

**Date**: 2026-10-07
**HEAD at verification**: `8148c7d` (chain: `59bae2a` → `a644e9c` → `e8408f8` → `434f6df` → `22bdd39` →
`8148c7d`; the last two are HANDOFF-only, no code change)
**Fresh build dir**: `build_verify_wave8` (not reused from any source agent's own worktree build; covers
all three code merges together in one combined verification pass)

## Merges covered

1. **`a644e9c`** — AI runtime (A0-A3), new library `sr3airuntime`. Source branch
   `worktree-agent-a889e030807838dba`, commit `74bd09b`.
2. **`e8408f8`** — `character_is_in_vehicle` (spec-lua-api-behaviour.md §3.10). Source branch
   `worktree-agent-a34152079c1730f6b`, commit `4ba380c`.
3. **`434f6df`** — anim-runtime U4-U7 spec-sync check (comment/citation-only, no behavior change). Source
   branch `worktree-agent-a9882bb32dde7dad2`, commit `943c0fd`.

Each merged sequentially with `git merge --no-commit --no-ff`, committed promptly after confirming
`git status --short` showed only the expected files staged. `CMakeLists.txt` auto-merged cleanly for both
code merges — zero manual conflict resolution.

## What each merge does

**AI runtime** (full detail in its own merge commit message, `a644e9c`): A0 — the full 44-row mode table,
request/apply/transition/acceptance, event pump; 6 of 13 begin-functions real, 7 left as cited
`OpenStateError` (the spec itself gives only a call signature for those — a genuine spec gap, now
forwarded to Team A as a new "AI A5" unit). A1 — the complete gate/chooser/goal pipeline, all 17 goal
tests, all 41 force flags; a real design bug caught mid-build (a `constexpr` `pre_flags` table would have
made the chooser's accept path untestable — made it a per-state array defaulting to 0 instead, matching
the spec's own "all 70 masks are 0 in the shipped image"). A2 — confirmed 22 lists (not 17, per a spec
correction), full capacity/lifetime/eviction/expiry; two corrections applied from a mid-task sync, the
rest documented as a cited follow-up gap. A3 — 59 of 70 action routines real, including all 8
non-pure-on-refusal routines with exact side effects and both null routines (24/49) refused via
defense-in-depth; 11 left as cited `OpenStateError` (2 are the null routines by design, not a gap; 2 are
genuine spec gaps — undecoded trajectory solvers, also now part of the AI A5 forward; 7 are this
project's own "no real pathfinding/geometry" scope choice, confirmed by the orchestrator to wait on
Team A's Havok collision-query specs).

**`character_is_in_vehicle`**: now cleared for implementation; `CharacterState::stateEnum==3` checked
unconditionally, the confirmed double-return-value quirk (false + the stateEnum-only result) reproduced
when arg2 is given but fails to resolve, vehicle identity compared by name via the existing
`currentVehicleName` field rather than a new handle-pair field.

**Anim-runtime U4-U7 spec-sync check**: independently confirmed (not assumed) that Team A's new §8.10 text
settles both prior discrepancies (31-vs-30 output-list margin; 13-vs-12 float-precision ramp updates) —
no code behavior changed, only comments annotated with the resolution and its provenance. U4-U7 themselves
describe a sampled-pose/bone/rig/layer-array system this project has never modeled; documented as a real
scope boundary. Flagged (not resolved) a genuine spec-anim-runtime.md internal contradiction between
§6.6-6.8 and the new §11.7 over two layer-setter address citations — zero behavioral effect either way,
relayed to Team A.

## Verification performed (this pass, independent of each source agent's own run)

- Fresh build directory `build_verify_wave8`, configured via `cmake -S . -B build_verify_wave8 -G "Visual
  Studio 17 2022" -A x64` (clean configure), built Release (`cmake --build build_verify_wave8 --config
  Release -j 8`, exit 0), against the actual merge-chain HEAD.
- `ctest -C Release`: **100% tests passed, 81/81** (2 more than the wave-6 baseline of 79 —
  `synthetic_ai_runtime_test` and `synthetic_luahost_character_is_in_vehicle_test`, both confirmed
  registered via `ctest -N`). The anim U4-U7 merge added no new tests (comment-only change, correctly —
  the existing, untouched assertions already serve as regression evidence for the two resolved
  discrepancies).
- `git status --short` confirmed, before each of the three commits, that only the expected files were
  staged — no spec files or unrelated changes rode into any of them.

## Outcome

All three merges verified clean on a fresh combined build. Worktrees `agent-a889e030807838dba`,
`agent-a34152079c1730f6b`, and `agent-a9882bb32dde7dad2` are candidates for the pending cleanup batch (not
deleted, per the user's "ask each time" policy).
