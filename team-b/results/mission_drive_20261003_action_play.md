# `action_play_do` / `action_play_is_finished` fix - before/after (2026-10-03)

## What was found, and the investigation path

Starting point: `results/probe_blockers_ranked_20261003_post_realhits.md`'s #1 shared
blocker - `action_play_do` had 49,655 real calls in a fresh probe-mode mission drive
(the single highest call count of any name in the whole run), was NOT in
`specConfirmedStubNames()` (a pure generic logging stub), and 7 missions
(`dlc3_m03`, `m06`, `m10`, `m11`, `m13`, `m14`, `m15`) all parked at the identical
`[string "game_lib.lua"]:563 (in action_play_non_blocking)`, no `first_error` - the
same architectural shape `cutscene_play_do`/`cutscene_play_check_done` had before
Sec57.2 closed it.

The a-priori guess (same shape as the cutscene fix: a separate `_check_done`-style
completion-check native being polled in the `thread_yield` loop) does **not** hold up
against this project's own real mission-drive evidence, checked directly rather than
assumed:

- `action_play_is_finished` (spec-lua-api-behaviour.md Sec7.21, `0x00a3dcc0` - a real,
  registered `gameplay` native, grepped directly in
  `tools/lua_all_registered_1490_tagged.txt`) had **zero** measured calls across a
  fresh run of the same 49 missions (absent from the whole
  `verdict_stub_hits_with_missions.tsv`, not merely zero-count).
- `thread_new`/`thread_check_done`/`thread_yield` (this project's own already-
  implemented thread-scheduler primitives, `src/lua_thread_scheduler.cpp` -
  deliberately out of scope per `spec_confirmed_stubs.h`'s own standing note) do show
  large real call counts, but a `thread_new()`-created handle is never
  `lua_resume()`'d by anything in this project's scope, so
  `ThreadScheduler::isDone()` would report "dead" on the very first check after
  creation - the opposite of a multi-tick stall, so this shape doesn't fit either.
- `action_play_non_blocking` is itself a plain `game_lib.lua` **function**, not a
  registered native (absent from `tools/lua_all_registered_1490_tagged.txt`) - its
  traceback frame is a Lua call frame, not a C one.

A temporary, reverted-before-commit diagnostic (dumping real argument **values**, not
just types, for `action_play_do` specifically - never reading `game_lib.lua`'s own
source text, which stays off-limits per this project's clean-room boundary) found the
SAME exact call repeating every tick for a stuck mission:
`action_play_do("MS_Jimmy", "saint fan react b", "", false, true, true, nil, nil)`.

`genericStubTrampoline` (`src/lua_stub_registry.cpp`) always pushes exactly 1 value -
`nil` - for an unimplemented name. In Lua, `nil` is one of only two falsy values (with
`false`), so a wrapper of the shape `while not action_play_do(...) do thread_yield()
end` (or an equivalent "keep retrying until this stops being nil/false" idiom -
inferred from this real call/return evidence, never from reading the off-limits
script text) can never terminate against a stub that returns `nil` on every call,
regardless of whether any separate completion-check native exists at all.

**spec-lua-api-behaviour.md Sec22.29 itself describes `action_play_do` as a one-shot
dispatcher with no completion-signaling mechanism of its own** - no scripted-
request-id pool (unlike Sec35.6's teleport/move-to/pathfind family), no thread
handle, no flag it sets for a later poll to read. Its own CONFIRMED contract is
simply "always pushes exactly 1 number" (1 on full success; two explicitly named
sentinels on two named failure paths). Implementing *that* contract faithfully - a
real number on every path, never `nil` - is therefore both the full extent of what
Sec22.29 actually specifies, and, on this project's own measured evidence, the real
candidate fix for the observed stall. No separate completion-tracking mechanism was
invented, because Sec22.29 does not describe one to invent.

## What was implemented

Both `action_play_do` (Sec22.29, `0x00a3fae0`) and `action_play_is_finished` (Sec7.21,
`0x00a3dcc0`) are now implemented per their CONFIRMED spec text (see
`src/lua_spec_confirmed_stubs.cpp`'s own doc comment above `stub_action_play_do` for
the full citation, and `include/sr3luahost/engine_state.h` for the new state).

**`action_play_do`**: 8-argument shape (character, action name, variant defaulting to
the action name itself - a genuinely distinct default shape Sec22.29 flags - 3
optional booleans default false, 2 optional secondary refs). Resolves the character
via the SAME shared Sec29 named-object map (`EngineState::objectResolves()`) every
other in-scope CharacterState-based function already uses - resolve failure pushes
the fixed sentinel `-1.0` (`EngineState::kActionPlaySentinelResolveFail`), the SAME
constant `effect_play`/`effect_play_on_script_object` (Sec9.1/Sec12.10) would use for
their own failure case - **neither of those two is implemented anywhere in this
codebase yet** (grepped directly), so this is this constant's first home, not a
reused one. If resolved but dead and not forced (arg 4): a bare `-1`
(`kActionPlaySentinelDeadNotForced` - textually the same Lua number, but tracked via
its own named constant/counter to mirror Sec22.29's own "two distinct sentinels"
observation). The dead-check reads a freshly-added, separately-scoped
`CharacterState::notDeadForActionPlay` (OPEN by default, same "OPEN until set, scoped
only to this one function" convention `CharacterState::isAlive` already established
for `vehicle_exit_group_check_done` - reusing `isAlive` directly would assume a
cross-call-site sharing fact neither section states).

Otherwise (alive-or-forced): the action-id lookup (`0x004bf810`, the animation-
name-to-id table) has **zero confirmed entries anywhere in this project's spec** -
the same boundary Sec29's named-object map documents for Killbane, here for
animation names instead of object names. Every lookup therefore honestly takes the
"not found" path - the same "honest behavior of a never-populated registry, not a
simplification of the return contract" this project already established for
`team_make_unfriendly`'s team-name registry (Sec30.6). Per Sec22.29's own AND logic
("the final return code is 1 only if BOTH the action-lookup succeeded earlier AND
this dispatch itself succeeded"), a failed lookup alone already forces a non-1
return regardless of what the still-OPEN dispatch (`0x00a275e0`) would otherwise
have decided - so this project never needs to guess `0x00a275e0`'s own internal
result to answer honestly. The exact non-1 numeric encoding isn't itself given by
Sec22.29 - `kActionPlaySentinelLookupMiss` is CHOSEN as `0.0` (this document's own
common boolean-as-number "false" idiom), explicitly labeled as such, not a claimed
real value.

**`action_play_is_finished`**: Sec7.21's CONFIRMED structure is "if resolved AND a
zero-argument context check (`0x00487e80`) succeeds (result != -1) AND the object's
action-slot value is still below the threshold argument, returns false. Otherwise
returns true[, + conditional notify]." The "context check" queries whether the
character's animation controller has an active sub-state - something only
`action_play_do`'s own OPEN internal dispatch (`0x00a275e0`) could ever establish,
and this project does not model those internals. Nothing in this project's scope can
therefore ever make the context check report anything other than "no active state" -
the same "this host never allocates X, so it honestly and permanently reproduces the
'reports done prematurely' [structural branch]" reasoning already established for
`cutscenePlayCheckDone()`. Implemented as an unconditional `true` (see
`EngineState::actionPlayIsFinished()`'s own doc comment for the full reasoning,
including why neither the resolved nor not-resolved path needs to be distinguished
here).

Neither function invents a new completion-signaling mechanism - both close exactly
what Sec22.29/Sec7.21 CONFIRM and leave `0x00eaa178`'s two-flag-table role and
`0x00a275e0`'s own internal body genuinely OPEN, refused rather than fabricated when
actually reached (the dead-check on a real, non-probed name; the lookup-miss path
honestly never reaches `0x00a275e0`'s own result at all, per the AND-logic reasoning
above).

## Before / after (real mission drive, `--tick-budget=721 --probe-unresolved-names`)

Both runs against the real archive cache
(`D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache`,
`tools/lua_all_registered_1490_tagged.txt`), 49 missions. Before = my own fresh
baseline run, this session, pre-fix (`results/verify_action_play_baseline/`). After =
the SAME flags, run from the independently-built `build_verify_action_play` directory
post-fix (`results/verify_action_play_mission_drive/`) - numbers reproduced
identically against the dev-build run too.

| metric | before | after |
|---|---|---|
| `missions_start_after_ticks` | `still_suspended:18 finished:10 errored:19` | `still_suspended:11 finished:10 errored:26` |
| `stop_reason_histogram` | `budget_exhaustion=18 error=0 completion=31` | `budget_exhaustion=11 error=0 completion=38` |
| `action_play_do` real calls | 49,655 | 9 |
| `action_play_is_finished` real calls | 0 (absent from TSV) | 1 |

Exactly 7 missions moved from `still_suspended`/`budget_exhaustion` to `errored`/
`completion` - precisely the 7 the probe report named, nothing else changed:

- `dlc3_m03`: now stops at `engine state character state enum (+0x16d4) is OPEN
  (Sec3.4/Sec3.10)` - an **already-known** OPEN item (the same one `m05`/`m07`/`m21`
  already hit in the pre-fix baseline) - i.e. it progressed past the action_play gate
  into an existing, already-tracked blocker, not a new category.
- `m06`, `m10`, `m11`, `m13`, `m14`, `m15` (6 of 7): now stop at a **new**, honest
  OPEN_STATE diagnostic naming exactly what's missing: `engine state character
  alive/not-gone predicate (0x0096f4f0), action_play_do's own call site is OPEN
  (Sec22.29)`. These calls resolve a non-probed, real character name (almost
  certainly `"#PLAYER1#"`, one of Sec29's 5 pre-populated-true literals - distinct
  from `dlc3_m03`'s probed `"MS_Jimmy"` NPC case above), so the diagnostic probe
  fallback correctly does not widen this genuinely OPEN gap; this project's own
  `--probe-unresolved-names` flag only widens the named-object resolution map, by
  design, never a per-character field beyond that.

`ctest` (59/59) passes in both the dev build (`build_baseline`) and a fresh,
independently-built `build_verify_action_play` directory, including
`synthetic_luahost_refusal_stress_test` (which calls every registered spec stub,
including the 2 new ones, with a spread of malformed/edge-case argument shapes under
`pcall`).

## Left genuinely OPEN

- `CharacterState::notDeadForActionPlay` (the 0x0096f4f0 predicate at
  `action_play_do`'s own call site) - OPEN until Team A gives a real default or a
  writer; this is now the concrete, named next blocker for `m06`/`m10`/`m11`/`m13`/
  `m14`/`m15`, surfaced honestly rather than guessed.
- The action-id lookup table (`0x004bf810`)'s real contents (which animation names
  exist, their real ids) - zero confirmed entries anywhere in this project's spec,
  same boundary as Killbane's named-object map.
- `0x00eaa178`'s own two-flag-table role and `0x00a275e0`'s own internal dispatch body
  - both explicitly flagged OPEN by Sec22.29 itself, not modeled.
- `dlc3_m03`'s own next blocker (character state enum +0x16d4) is pre-existing/
  already tracked, not newly introduced by this batch.
