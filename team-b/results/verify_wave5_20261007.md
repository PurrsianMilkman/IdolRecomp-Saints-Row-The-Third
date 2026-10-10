# Verification: wave-5 merge (16 Lua native implementations)

**Date**: 2026-10-07
**HEAD after merge**: `7cd9275` (chain: `357bdca` → `7cd9275`)
**Fresh build dir**: `build_verify_wave5` (not reused from the source agent's own worktree build)

## Merge covered

**`7cd9275`** — 16 Lua native implementations from a fresh mission-drive stub-hit ranking. Source branch
`worktree-agent-aa94af86bce7e939c`, commit `0acb5aa`.

## What it does

Implemented, each cited to its own spec section: `audio_object_post_event` (§2.4), `character_is_ready`
(§7.27, partial — CONFIRMED early-exits only, the AND-chain's own unresolved "else" value left as a
precisely-cited `OpenStateError`), `mesh_mover_hide`/`show` (§9.16/9.17), `character_playing_combat_move`
(§25.27), `object_spawn_pause`/`mip_streaming_pause` (§60.1), `door_lock`/`close` (§9.15/14 family),
`city_zone_swap` (§1.9), `traffic_disable_lanes` (§12.19), `party_dismiss_all` (§14.17),
`sidewalk_disable_nodes` (§15.3), two of the three `on_mover_destroyed`-sibling natives, and
`group_create_check_done` (§22.18, added after discovery mid-task — see below). `specConfirmedStubNames()`
now totals 246 (230 + 16).

**Respected the standing spec-lua-api-behaviour.md §1-5 implementation freeze**: skipped
`trigger_enable`/`on_trigger`/`door_open`/`character_is_dead`/`notoriety_force_no_spawn` despite real call
volume, since none of those sections has been individually re-cleared by Team A yet (only
`audio_object_post_event`/§2.4 and `city_zone_swap`/§1.9 are individually cleared, by §60.3's own
"re-verified, no corrections needed" text — verified this distinction holds before merging).

**`group_create_check_done`** was added after the agent found it driving mission `m02`'s own stuck poll
loop (same `action_play_do`-shaped pattern: `repeat thread_yield() until group_create_check_done(name)`
against a nil-returning generic stub). Confirmed post-merge: `m02` now completes instead of
`still_suspended`.

**A real latent bug caught and fixed**: a dual-registration race where `registerStubs()` would have
silently overwritten the new `audio_object_post_event` implementation immediately after
`registerSpecConfirmedStubs()` installed it — fixed by moving it to `specUiNames`, the same pattern
`coop_is_active` already uses.

**Companion Team-A deliverable**: `results/probe_blockers_ranked_20261007.md`, a fresh
`--tick-budget=721 --probe-unresolved-names` mission-drive blocker ranking, replacing Team A's stale
2026-10-03 version. Confirms `character_is_in_vehicle`/§3.10 (41,176 calls, 5 missions) as the new #1 —
already flagged there as being closed by a Team A agent, not duplicated.

**Excluded from this merge's commit** (see commit message on `7cd9275` for the full explanation): three
raw mission-drive verdict-TSV dumps the source branch had committed to git (~674k lines combined,
`results/verify_native_impl_mission_drive/`, `results/verify_native_impl_post_merge/`,
`results/verify_probe_20261007/`) — this project's convention keeps that kind of raw run output as
untracked working-tree scratch, never committed to git. Left in the working tree exactly as the source
branch produced them, simply not staged/committed.

## Verification performed (this pass, independent of the source agent's own run)

- Fresh build directory `build_verify_wave5`, configured via `cmake -S . -B build_verify_wave5 -G "Visual
  Studio 17 2022" -A x64` (clean configure), built Release (`cmake --build build_verify_wave5 --config
  Release -j 8`, exit 0), against the actual merge commit `7cd9275`.
- `ctest -C Release`: **100% tests passed, 79/79** (1 more than the wave-4 baseline of 78 —
  `synthetic_luahost_native_batch1007_test`), including `synthetic_luahost_refusal_stress_test` and
  `synthetic_luahost_roster_test` (confirms the dual-registration fix didn't disturb the other 7
  already-dual-registered names).
- `git status --short` confirmed only the intended files were staged before commit (the three raw-dump
  directories deliberately excluded, per above) — no spec files rode into it.

## Outcome

Merge verified clean on a fresh build. Worktree `agent-aa94af86bce7e939c` and build dir
`build_verify_wave5` are candidates for the pending cleanup batch (not deleted, per the user's current
"ask each time" policy — see HANDOFF.md).
