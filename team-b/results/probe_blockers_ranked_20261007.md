# Next shared blockers, ranked by mission count (2026-10-07)

Supersedes `results/probe_blockers_ranked_20261003_post_realhits.md` for ranking purposes — that file
stays as historical record, not overwritten. Requested by Team A (relayed through the orchestrator):
the 2026-10-03 file predates the `action_play_do`/`action_play_is_finished` fix
(`results/mission_drive_20261003_action_play.md`) and the spec-lua-api-behaviour.md Sec3.7 double-gate
resolution, and 2 of its top-3 ranked blockers are already gone.

Real mission drive, `--tick-budget=721 --probe-unresolved-names` (the same extended per-character probe
and flags the 2026-10-03 file used), run fresh against the real archive cache from a from-scratch build
of the current worktree tip (`build_verify_native_impl`, Release). Raw output:
`results/verify_probe_20261007/`.

Aggregate: `missions_start_after_ticks=still_suspended:28 finished:10 errored:9` (of the 47/49 missions
whose `_start` call suspended; one further mission, `mm_p_06`, fails at the `_start` call itself — see
item 6 below — and one, `mm_m1_5`, finishes synchronously inside `_start` with no suspend). Probe
resolved 125 distinct named objects this run (vs. 50 on 2026-10-03 — more names are now reachable
because more upstream blockers are closed), so every blocker below is genuinely past named-object
resolution, not a resolution artifact.

**Confirmed gone since 2026-10-03:** `action_play_do`/`action_play_non_blocking` (previously 7 missions
parked at the identical `game_lib.lua:563`) — none of the 28 still-suspended missions below are at that
line anymore. The clock blocker and the Sec3.7 vehicle-invulnerability double-gate are also gone as
*shared* blockers (the clock item below is a different, single-mission residual, expected per
Sec48.4's own documented behaviour).

## 1. Character state enum `(+0x16d4)` / `character_is_in_vehicle` (vehicle vtable `+0x70`) — Sec3.4/Sec3.10 — 5 missions, 41,176 real calls (the single highest call count of any name this run)

- **Missions (errored — `first_error` is the terminal state, thread never resumed again):** `dlc2_m01`,
  `dlc3_m03`, `m07`, `m05`, `m21` — all `engine state character state enum (+0x16d4) is OPEN
  (spec-lua-api-behaviour.md Sec3.4/Sec3.10)`.
- **`character_is_in_vehicle` itself logs 41,176 calls this run** (`verdict_stub_hits_with_missions.tsv`),
  by a wide margin the highest of any name — it and several already-implemented natives (the group-
  liveness and action-override checks at `src/lua_spec_confirmed_stubs.cpp:496/3103/3158/4487`) all read
  the same `CharacterState::stateEnum` (+0x16d4) OPEN field, so its resolution has broad reach beyond
  just these 5 terminal missions.
- **Already in progress, no action taken here:** per the orchestrator's relay, a Team A agent is
  currently closing lua-api Sec3.10 (`character_is_in_vehicle`, the vehicle vtable `+0x70` accessor),
  reported there as blocking 3 missions — this fresh run measures 5 (more missions now reach this point
  after the clock/action_play/double-gate fixes landed). Noted for their awareness, not duplicated here.

## 2. Character max hit points `(+0x1cac)` OPEN — Sec7.12/Sec7.31/Sec34.2 — 1 mission (`m18`)

Spec-side note: Sec60.4 already describes this as code-side CLOSED in the spec document itself ("the
value itself is confirmed to be genuinely random per spawn, pinned only by class/rank type, with a
script-NPC override still parked zone data") — but Team B's own `get_max_hit_points` implementation
(`stub_get_max_hit_points`, already in `specConfirmedStubNames()`) has not yet been updated to that
corrected, random-per-spawn/class-rank model, so it still refuses via the old OPEN value here. This is a
Team B implementation-lag item, not a missing spec — flagged for our own backlog, not Team A's.

## 3. Group `+0x40` head name `['grp_cspawn_pistol_01']` OPEN — Sec28.6 — 1 mission (`dlc2_m02`)

Same single-mission item as the 2026-10-03 report's item 4a, unchanged.

## 4. Continuous-explosion definition row resolves `(0x005eb390, NOT the 0x02442750 singleton family)['Daedalus']` OPEN — Sec28.16 — 1 mission (`m23`)

New since 2026-10-03 — `m23` previously parked earlier (behind Killbane/max-HP-class blockers); with
those probe-widened, it now reaches this deeper, previously-unmeasured blocker.

## 5. Game clock hour byte `(0x014ff33c)` OPEN — Sec32.1/Sec48.2/Sec48.4 — 1 mission (`m19`)

Same single-mission item as the 2026-10-03 report's item 4b. Expected, not new: Sec48.4's own documented
forward-delta-then-forget behaviour after a SECOND `set_time_of_day` call (the clock fix already closes
the first call; `m19` calls it a second time).

## 6. `_start` call itself fails: `zscene_prep` → cutscene state `0x0153b520` OPEN — Sec26.24/Sec26.25 — 1 mission (`mm_p_06`)

Distinct from every item above: this is the one mission (of 49) where `missions_with_start_call_ok`
itself is 0, not a post-suspend blocker. Not in the 2026-10-03 report (that run's own `_start`-call
success rate was presumably different before the intervening fixes).

## 7. The next `action_play_do`-shaped target: `group_create_check_done` — 1 mission directly (`m02`), but 18,810 real calls, and genuinely unimplemented

`m02` is parked at `game_lib.lua:456` inside `group_create_check_done_loop` — a
`repeat thread_yield() until group_create_check_done(name)` poll against a native that is a **pure
generic logging stub today** (confirmed: absent from `specConfirmedStubNames()`), the identical
architectural shape `action_play_do`/`cutscene_play_check_done` had before their own fixes. Unlike those,
only 1 mission is *terminally* stuck here, but the call count (18,810, `verdict_stub_hits_with_missions.tsv`)
is the 4th-highest generic-stub count in this entire run — most of that volume is other missions polling
it in loops that happen to resolve for other reasons before this specific gate matters to them. Spec
coverage exists (`spec-lua-api-behaviour.md` Sec22.18, CONFIRMED for the "unresolved/not-alive → true"
early-exit; OPEN only for the two-list completion walk's exact internal field offsets once a group
actually resolves) — in scope for, and picked up by, Team B's own native-implementation pass this round
(see the companion implementation report); flagged here too since it is Team A-visible mission-drive
signal of the same shape their own action_play_do tip already proved valuable.

## Remaining still_suspended missions with no shared blocker (27 of the 28, each at its own distinct script line)

`dlc2_m03`, `dlc3_m02`, `dlc3_m01`, `sh03`, `m08`, `mm_p_01`, `m17`, `m16`, `m11`, `m24`, `m10`, `m13`,
`m04`, `m12`, `sh02`, `m03`, `mm_p_03`, `sh04`, `m20`, `m06`, `sh01`, `m01`, `m22`, `mm_p_07`, `m15`,
`m09`, `m14` — no two share an exact `waiting_at` script:line this run (the one exception, `m02`, is
item 7 above). This is a real, measured change from 2026-10-03: that run found exactly one 7-mission
cluster sharing a line; this run finds none, because that cluster's native (`action_play_do`) is fixed.
No further cross-mission overlap was found by location; a deeper native-level grouping (shared helper
calls inside `game_lib.lua`, not just identical yield lines) was not attempted this pass and would need
reading each of these 27 scripts' own surrounding source, which is out of this report's scope.
