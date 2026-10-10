# Verification: wave-6 merge (static Lua native-call cross-reference)

**Date**: 2026-10-07
**Merge commit verified**: `0cd18db` (chain: `5ea0de6` → `0cd18db`)
**Fresh build dir**: `build_verify_wave6` (configured/built against `0cd18db`'s tree; two doc-only commits,
`85a8986` spec-sync and `9221eb6` HANDOFF.md, landed on top while this build ran in the background - both
touch only `.md` files, not code, so they don't affect this verification)

## Merge covered

**`0cd18db`** — static Lua native-call cross-reference, report-only. Source branch
`worktree-agent-ad3d8d6dc3609b566`, commit `1e80a94`.

## What it does

Ran `luacheck` (installed from the official `lunarmodules/luacheck` v1.2.0 GitHub Release, MIT, since no
package manager with it was available) restricted to warning W113 (undefined global access) over all 804
shipped mission scripts (dumped to disk by a new standalone tool, `tools/validation/lua_source_dump.cpp`,
NOT wired into CMakeLists.txt, matching that directory's own convention) — 118,422 raw warnings, 7,165
distinct names, filtered down to 1,262 genuine static candidates after excluding names any shipped script
defines itself. Cross-referenced against the runtime stub-hit TSV and this project's full set of
already-implemented natives (caught and corrected a real methodological trap before publishing:
`thread_yield`'s 315M real calls would have ranked #1 as "unimplemented" without widening the exclusion
set past `specConfirmedStubNames()` to the hand-written `ThreadScheduler` and the 24 bare globals).

**Headline finding**: 15 of the top 20 ranked candidates already have a CONFIRMED, address-cited
spec-lua-api-behaviour.md section — they're blocked on the standing 2026-09-30 "not yet cleared for
implementation" freeze (§1-5), not on missing spec knowledge. Team A is now making lifting that freeze a
priority, in parallel batches, per the orchestrator.

Full ranked table: `results/lua_undefined_globals_ranked_20261007.md`.

## Verification performed (this pass, independent of the source agent's own run)

- Fresh build directory `build_verify_wave6`, configured via `cmake -S . -B build_verify_wave6 -G "Visual
  Studio 17 2022" -A x64` (clean configure), built Release (`cmake --build build_verify_wave6 --config
  Release -j 8`, exit 0), against merge commit `0cd18db`.
- `ctest -C Release`: **100% tests passed, 79/79** (unchanged from the wave-5 baseline — expected, since
  this merge is report-only and touches no `sr3*` library or CMakeLists.txt target).
- `git status --short` confirmed only the two intended files were staged before commit.

## Outcome

Merge verified clean (no change expected or found). Worktree `agent-ad3d8d6dc3609b566` and build dir
`build_verify_wave6` are candidates for the pending cleanup batch (not deleted, per the user's "ask each
time" policy).
