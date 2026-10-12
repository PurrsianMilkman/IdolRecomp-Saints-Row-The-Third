# HUD-prompt direct-callback CHOSEN stand-in + m05 re-run (2026-10-12)

Branch `fix/hud-prompt-direct-callback-standin`, worktree
`.claude/worktrees/agent-hudpromptautoanswer` (reused from the prior
`feature/hud-prompt-autoanswer-harness` task, rebased onto `main` at `7489e2d`).

## Task

Per Team A's 2026-10-12 spec-sync (spec-lua-api-behaviour.md Sec13.3, orchestrator-routed): `hud_prompt`'s
arg 3 IS confirmed to be the real callback name the engine genuinely calls (e.g. `"m05_jump_local"` in
`m05.lua`'s own `hud_prompt(LOCAL_PLAYER, "M05_JUMP", "m05_jump_local")` - `M05_jumped` is only ever set
true inside that function, and the shipped mission completes in the real game), but the real dispatch path
that resolves it to a call is NOT inside `hud_prompt`'s own native chain (`0x00a4cff0`/`0x00a37210`/
`0x00a370c0`, all checked) - genuinely not yet traced. Orchestrator + Team A signed off on a pragmatic fix:
have the auto-answer harness directly look up and CALL the named global Lua function when it fires, labeled
as a CHOSEN stand-in (not modeling the real unknown dispatch path).

## What changed

- **`include/sr3luahost/host.h` / `src/lua_host.cpp`**: new `Host::callGlobalFunctionByName(lua_State* L,
  const std::string& name, std::string& error)`. First version (bare `lua_getglobal` + `lua_pcall`,
  matching `Host::fireConfirmedHooks()`'s own existing idiom) was tried first and measured to FAIL for
  `m05_jump_local` specifically: `"attempt to yield across metamethod/C-call boundary"` - the real callback
  body itself yields (confirmed by this measurement, not guessed), so it cannot run inside an ordinary
  `lua_pcall` C-call frame. CORRECTED in the same pass to use the ALREADY-EXISTING
  `BareThreadTable::startThread()` (`bare_globals.h`/`lua_bare_globals.cpp`) instead - this project's own
  "allocate a root record for global `name` and run it AT ONCE" helper (same primitive the mission-`_start`
  root entry uses), which calls the named global via `lua_resume` on a fresh coroutine - tolerates a yield
  inside it exactly like any other script thread (left alive in the thread table, resumed again by the
  normal `runScriptThreadSchedulerPass()` each tick if it doesn't finish). No second call-by-name mechanism
  invented: this reuses the SAME coroutine-table/scheduler machinery `BareThreadTable::allocate()` already
  provided, just calling `run()` immediately instead of deferring to the next scheduler pass.
- **`tools/lua_host_run.cpp`** / **`tools/sr3_viewer.cpp`**: both `--auto-answer-hud-prompts[=N]` /
  `--script-hud-prompt-response` dispatch blocks now call `Host::callGlobalFunctionByName()` instead of
  `threads().allocate()` (deferred spawn). Each call site carries an explicit "CHOSEN stand-in" comment
  citing spec-lua-api-behaviour.md Sec13.3 and this measurement.
- **`tests/synthetic_hud_prompt_autoanswer_test.cpp`**: added block (D), mirroring blocks (B)/(C) but for
  the new direct-call dispatch - confirms the callback's side effect (a flag global) is observable
  immediately once due (no extra scheduler pass needed, unlike the old deferred-spawn shape), that no
  coroutine is left behind for a callback that doesn't yield, and that a request naming a non-existent
  global is a silent no-op (no crash, no Lua error), matching `fireConfirmedHooks()`'s own existence-check
  convention. Existing blocks (A)/(B)/(C) kept unchanged - `BareThreadTable::allocate()` itself is still a
  real, separately-used primitive (`thread_new`, trigger-volume dispatch, etc.), just no longer what THIS
  dispatch path uses.

## m05 re-run result

Same invocation style as `results/verify_m05_hud_prompt_autoanswer_20261012/` (`lua_host_run.exe` against
the real game cache `D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache`,
`tools/lua_all_registered_1490_tagged.txt`, `--tick-budget=200`), before/after this fix:

```
BEFORE (no flag):
  m05: ... waiting_at=[[string "m05.lua"]:1141 (in m05_parachute_in)] scheduler_resumes=200

AFTER (--auto-answer-hud-prompts=15, direct-call stand-in):
  m05: ... waiting_at=[[string "m05.lua"]:1141 (in m05_parachute_in)] scheduler_resumes=369   <-- SAME LINE, +169 extra resumes

[auto-answer-hud-prompts] hud_prompt("#PLAYER1#", "M05_JUMP", "m05_jump_local") seen 15 tick(s) ago -
  CHOSEN stand-in: calling global 'm05_jump_local' directly
```

(No "not called"/error line this run - the earlier bare-`lua_pcall` version of this same stand-in DID log
`attempt to yield across metamethod/C-call boundary` for this exact call; switching to
`BareThreadTable::startThread()` eliminated that error entirely.)

**m05 still does not advance past line 1141** - but the measurement is now qualitatively different and
more precise than the prior task's: `m05_jump_local` genuinely IS invoked this time (no call-by-name
failure, no Lua error of any kind), and its own thread stays alive and is resumed 169 additional times
across the remaining budget (`scheduler_resumes` 200->369, `scheduler_thread_errors` stays 0) without ever
completing - i.e. it is yielding repeatedly inside its own body, not erroring out and not failing to be
found. Reading `m05_jump_local`'s real source (`m05.lua:1730-1734`, extracted via the project's own
clean-room `lua_source_dump` tool from the real game cache, not exe-derived):

```lua
function m05_jump_local()
	hud_prompt_clear(LOCAL_PLAYER)
	vehicle_exit(LOCAL_PLAYER)
	M05_jumped = true
end
```

`M05_jumped = true` is the LAST statement - if the thread never reaches it, something earlier in this
3-line body (`hud_prompt_clear` or, far more likely given this project's own already-documented
`vehicle_exit`/`+0x16d4` state-machine history, HANDOFF.md's "Vehicle enter/exit `+0x16d4` synchronous-write
fix merged" entry) is itself gated on a real seated/vehicle-occupancy precondition this synthetic harness
run never independently establishes (no real "player actually sitting in the M05 heli" state exists in this
tick-budget drive the way it would in an actual played mission) - so `vehicle_exit`'s own real completion
gate just never closes here, independent of the hud_prompt dispatch question this task was scoped to.

## Open question (NOT this task's to resolve - "no invented fixes" per standing project policy)

Whether `vehicle_exit(LOCAL_PLAYER)` inside `m05_jump_local` is waiting on a real seated/vehicle-state
precondition that a bare `lua_host_run` tick-budget drive (no real player-vehicle setup) can never satisfy,
or on some other real engine condition, is real-game-logic reverse engineering territory, not this
harness-flag task's own scope (and, per the standing "Team B never disassembles" rule, not something to
trace further here) - flagging as a spec-sync question for Team A / a follow-up mission-blocker task:
**"does `vehicle_exit` on a player with no live vehicle-occupancy state ever complete, or does its own
real completion gate require actually being seated first (something m05's own earlier mission flow would
have set up in a real playthrough, but this synthetic tick-budget drive starting mid-mission does not)?"**

## Build/test

- `cmake --build build --config Release`: exit 0 (full build, worktree `agent-hudpromptautoanswer`).
- `ctest -C Release`: **185/185 passed** (same as the pre-existing baseline - this task only extends the
  existing `synthetic_hud_prompt_autoanswer_test` binary with one more test block, no new test target, so
  the total registered-test COUNT is unchanged at 185).
