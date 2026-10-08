# notDeadForActionPlay (Sec34.5) fix verification (2026-10-03)

Implements spec-lua-api-behaviour.md §34.5 (CONFIRMED real body of the `0x0096f4f0` alive/not-gone
predicate at `action_play_do`'s own call site, Sec22.29): dead if `lifeState == 5` (kill-finalized) or the
new `killFinalizedFlagE3` (+0xe3 bit 0x01) is set; neither holds at spawn (CONFIRMED `lifeState` zeroed by
the constructor; HIGH CONFIDENCE `+0xe3` bit clear from a zero-filled allocation), so a freshly-created
character is alive by default. `CharacterState::notDeadForActionPlay` converted from an independently-set
OpenValue field to a method computed live from the two real underlying fields, both now seeded to their
CONFIRMED spawn defaults in `EngineState::getOrCreateCharacter` — applies uniformly to real and probed
names alike, so the stub's own probe-fallback special-case is no longer needed and was removed.

`lifeState` was already a CONFIRMED field (Sec7.13/Sec32.3, "6 = downed") but had never actually been
seeded at construction before this fix — only documented as having a CHOSEN default in a comment that was
never backed by real seeding code. This fix makes that seed real for the first time, closing a second,
previously-silent OPEN gap at the same character construction site.

## Verification

Fresh build (`build_verify_s345`): `ctest -C Release` **61/61 passed**, unchanged count (no new test file
this pass — the existing `synthetic_luahost_tranche_realhits_test`/refusal-stress coverage already
exercises `action_play_do`'s call shape; the behavior change is in default initial state, not a new
code path needing new test scaffolding).

Probe-mode mission drive (`results/verify_s345_mission_drive/`): `still_suspended:23 finished:10
errored:14`, vs. the immediately-prior `action_play_do`-merge baseline `still_suspended:17 finished:10
errored:20`. Per-mission diff confirms **all 7 missions that previously hit the
`notDeadForActionPlay`/Sec22.29 OPEN item now progress past it**, none regress:

- `m06`, `m10`, `m11`, `m13`, `m14`, `m15` (6 of 7): move from `errored` to `still_suspended`, each with
  no further recorded error — real forward progress into a deeper architectural wait, same shape as every
  prior "unstick a false blocker" fix this session.
- `dlc3_m02` (the 7th): moves to a different, already-recognized real script error
  (`game_lib.lua:1488: attempt to index local 'player_list' (a nil value)`, the same one `sh03` already
  hits) — not a new category of blocker.
- `dlc3_m03` (hit a DIFFERENT OPEN item, the already-known character-state-enum `+0x16d4`, not this one):
  unaffected, as expected.

Verdict: the fix is sound and closes exactly the blocker it targeted, for all 7 affected missions.
