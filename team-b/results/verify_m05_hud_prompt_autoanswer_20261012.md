# HUD-prompt auto-answer harness flag + m05 re-run (2026-10-12)

Branch `feature/hud-prompt-autoanswer-harness`, worktree `.claude/worktrees/agent-hudpromptautoanswer`.

## Task

Add a harness option to auto-answer HUD prompts during a scripted/automated (`--no-input`-style)
run, so a mission that genuinely stalls on a real player-input-gated HUD-prompt wait (confirmed real
behaviour, not an engine bug: m05.lua:1141's `m05_parachute_in` waits for the `M05_JUMP` prompt's own
registered callback, per Team A spec sync `dc5b4ce` and the already-merged
`feature/m05-jump-hud-prompt-test` investigation, commit `917ce08`) can be driven past that wait and
surface its own NEXT real stall point.

## Existing infrastructure found and reused

- `EngineState::hudPromptRequests()`/`recordHudPrompt()` (`include/sr3luahost/engine_state.h`) - the
  real `hud_prompt` native's (spec-lua-api-behaviour.md §13.3) own request-recording sink, already on
  `main`.
- `tools/sr3_viewer.cpp`'s own `--script-hud-prompt-response` flag (already merged, commit `917ce08`):
  each tick, answers any newly-recorded `hudPromptRequests()` entry **immediately** (no delay) by
  spawning a coroutine for its own `arg3` via `BareThreadTable::allocate()` - the same "spawn now, let
  the next scheduler pass resume it" idiom §1.14's confirmed `on_trigger`/`on_death`/etc dispatch
  already uses, and the same idiom `tools/lua_host_run.cpp`'s own `--probe-autopilot` post-hoc
  diagnostic block uses for that family.
- The shop/crib press-E entry pattern (`registerShopCribTrigger()`/`pressShopCribInteract()`) was
  checked but is a **separate, dedicated** trigger-volume mechanism, not what governs a generic
  `hud_prompt` call - `hud_prompt`'s own callback dispatch (as this task's own new measurement below
  shows) is NOT clearly the same mechanism the prior merged task assumed either; see "Open question"
  below.

## What this task added

1. **`tools/sr3_viewer.cpp`**: new `--auto-answer-hud-prompts [N]` flag (default N=15), a generalization
   of `--script-hud-prompt-response` with a configurable per-request frame delay (tracked from each
   request's own first-seen `frames` value) instead of an immediate answer. `--script-hud-prompt-
   response` is unchanged (delay 0); both share the same underlying dispatch block and bool. Generic
   over every currently-showing prompt, not hardcoded to `M05_JUMP`.
2. **`tools/lua_host_run.cpp`** (the actual tool `results/mission_drive_*.md`'s own entries are produced
   by - the real "mission-drive" harness, not `sr3_viewer`): new `--auto-answer-hud-prompts[=N]` flag
   (default N=15 ticks), firing **live** during the normal per-mission tick loop (unlike
   `--probe-autopilot`, which is a post-hoc, one-shot diagnostic fired only after a mission is already
   classified `still_suspended` at budget exhaustion) - so a mission blocked only on a HUD prompt can
   keep advancing within its own `--tick-budget=` instead of tick-budget-stalling on it forever.
   Persistent, run-wide tracking (`hudPromptRespondedCount`/`hudPromptFirstSeenGlobalTick`/`globalTick`),
   matching the existing `schedulerForeignErrorsTotal`-style "persists across the whole mission loop"
   convention already used in that file.
3. **Test**: `tests/synthetic_hud_prompt_autoanswer_test.cpp` (registered in `CMakeLists.txt`) - exercises
   the real `EngineState`/`BareThreadTable` primitives both production sites use, with the identical
   delay-math: (A) no auto-answer at all - a recorded `hud_prompt` request never resolves on its own
   across 50 scheduler passes; (B) with a 15-tick delay - the request does NOT fire for the first 14
   ticks after being first seen, still does not fire on tick 15 (`tick - firstSeen == 14`, boundary
   check), fires (spawns) on tick 16 (`tick - firstSeen == 15`), and its callback body actually runs
   (sets a flag) only on the FOLLOWING scheduler pass, never synchronously; (C) a second, independent
   prompt is delay-gated from its own first-seen tick, proving the mechanism is generic, not a
   one-shot special case.

## m05 re-run result

Ran `lua_host_run.exe` against the real game cache (`tools/lua_all_registered_1490_tagged.txt`,
`--tick-budget=200`), once without the new flag and once with `--auto-answer-hud-prompts=15`:

```
BEFORE (no flag):
  m05: ... waiting_at=[[string "m05.lua"]:1141 (in m05_parachute_in)]

AFTER (--auto-answer-hud-prompts=15):
  m05: ... waiting_at=[[string "m05.lua"]:1141 (in m05_parachute_in)]   <-- SAME

[auto-answer-hud-prompts] hud_prompt("#PLAYER1#", "M05_JUMP", "m05_jump_local") seen 15 tick(s) ago -
  spawning a coroutine for callback 'm05_jump_local'
```

Full before/after `verdict_*` outputs: `results/verify_m05_hud_prompt_autoanswer_20261012/{before,after}/`.

**The flag demonstrably works as designed** - it correctly waited 15 ticks after the `M05_JUMP` prompt
was first recorded, then spawned a coroutine for `m05_jump_local` exactly once (confirmed by the one log
line above; not re-fired on later ticks). **m05 itself did not advance past line 1141** even with 185
further ticks (and scheduler passes) for that spawned coroutine to run and resume - the wait condition
`m05_parachute_in` polls never cleared.

## Open question (NOT this task's to resolve - "no invented fixes" per standing project policy)

Re-reading spec-lua-api-behaviour.md §13.3 directly (the real `hud_prompt` native's own spec entry)
while writing this up: it describes `hud_prompt`'s arguments as **"Arg 1 - a target/actor name. Arg 2,
Arg 3 - two further strings (prompt text/localization keys)"**, and marks "**OPEN - ... the exact
real-world roles of arg 2 vs. arg 3**" - it does **not** itself claim arg 3 is a callback name to spawn.
The already-merged `--script-hud-prompt-response` mechanism (and this task's own `--auto-answer-hud-
prompts`, which deliberately reuses that same mechanism per this task's own "reuse existing machinery,
don't invent a parallel path" instruction) both assume `req.arg3` IS a callback name, matching the
string `m05.lua` itself passes (`"m05_jump_local"`) closely enough to look plausible, and the spawn/
dispatch mechanics themselves are proven correct (the synthetic test above, and the log line confirming
the spawn fired exactly once at the right tick). But this run's own new measurement - the spawn
genuinely fires, is given 185 further ticks' worth of scheduler passes to run and resume, and m05 STILL
never leaves line 1141 - is at minimum consistent with, and does not rule out, that assumption being
incomplete or wrong: `arg3` may really be (as §13.3 suggests) a **localization key**, not a callback
name, and the real callback-to-press association for a `hud_prompt` may be a separate, not-yet-
identified mechanism (e.g. the real button-press handler §13.3 itself never traces past `0x00a370c0`,
marked OPEN). Determining which is correct is real-game-logic reverse engineering (tracing `0x00a370c0`
and/or the real button-press dispatch path), which is explicitly **Team A's territory, not Team B's**
(per the standing "Team B never disassembles" rule) - flagging this as a spec-sync question for Team A
rather than guessing further or inventing a fix here.

## Build/test

- `cmake --build build --config Release`: exit 0 (full build, worktree `agent-hudpromptautoanswer`).
- `ctest -C Release`: see HANDOFF/commit message for the exact pass count; new test
  `synthetic_hud_prompt_autoanswer_test` added and passing, no regressions against the existing
  baseline.
