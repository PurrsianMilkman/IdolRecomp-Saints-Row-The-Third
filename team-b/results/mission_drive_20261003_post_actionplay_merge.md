# action_play_do/action_play_is_finished merge verification (2026-10-03)

Verified independently against a fresh build (`build_verify_actionplay`), separate from the implementing
agent's own dev/verify builds. Clean auto-merge into `main` (no commit SHA captured in this doc on
purpose — see `git log` for the merge commit directly preceding this one), touching
`include/sr3luahost/engine_state.h` and `src/lua_spec_confirmed_stubs.cpp`, both resolved automatically
with no conflicts.

`ctest -C Release`: **61/61 passed** (unchanged count from the prior combined merge — this batch added no
new test file, consistent with the agent's own report of extending the existing refusal-stress test
coverage in place).

Probe-mode mission drive (`--tick-budget=721 --probe-unresolved-names`,
`results/verify_actionplay_mission_drive/`): `still_suspended:17 finished:10 errored:20`, vs. the
immediately-prior combined-merge baseline (`results/verify_combined_mission_drive/`)
`still_suspended:25 finished:10 errored:12`.

Per-mission diff confirms **exactly 8 missions moved, all from `still_suspended` to `errored`, none the
reverse**: `dlc3_m02`, `dlc3_m03`, `m06`, `m10`, `m11`, `m13`, `m14`, `m15`. 7 of these (`dlc3_m03` excepted)
were exactly the set the implementing agent's own report named; `dlc3_m02` is an additional one, explained
by this project's own prior force-flag-gate fix (landed in the same combined-merge batch) already having
moved `dlc3_m02` from `errored` to `still_suspended` on an earlier pass, so this fix finds it waiting
one step further down the same script.

Checked each new error message directly: 7 of the 8 land on the SAME new, precisely-named OPEN item —
`character alive/not-gone predicate (0x0096f4f0), action_play_do's own call site is OPEN
(spec-lua-api-behaviour.md Sec22.29)` — exactly the `CharacterState::notDeadForActionPlay` field the
implementing agent's own report flagged as "the concrete next blocker... now precisely named for Team A
rather than guessed." The 8th (`dlc3_m03`) lands on the already-known character-state-enum `+0x16d4` OPEN
item (same one `m05`/`m07`/`m21` already hit before this batch). No regression, no surprise.

Verdict: merge is sound. `notDeadForActionPlay` is now a real, named, 7-mission-shared next blocker worth
surfacing to Team A alongside the still-open force-flag-gate routing.
