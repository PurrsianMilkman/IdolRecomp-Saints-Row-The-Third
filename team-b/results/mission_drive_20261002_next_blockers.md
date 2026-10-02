# Mission-drive next-blocker picture (2026-10-02)

Real mission drive (49 missions) on `main` at `c525f20` (items 1-5 of the wrap-up list, through the
request-11 + `0x0153b556` fix and the tables-progression fix), re-run after both landed. Raw tool output:
`verdict_mission_drive_20261002.tsv` (per-mission detail), `verdict_stub_hits_with_missions_20261002.tsv`
(unmodified tool output), `stub_ranking_with_specced_20261002.tsv` (same, with a `specced` column added -
see method below). Archive cache: `D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache`.

Headline: `missions_with_start_call_ok=48/49`, `missions_with_start_suspended=39/49` (yielded, not resumed -
resume cadence is OPEN, not modelled), yield-across-C-call-boundary error `0` (was 39).

## 1. Top first errors/refusals across the 49 missions

| First error/refusal | Missions hitting it (of 49) |
|---|---|
| `named-object resolution['Killbane']` is OPEN (`spec-lua-api-behaviour.md` Sec3.9/Sec10.6) | **48** |
| `zscene_prep`: `0x0153b530` (current scene entry) is OPEN (`spec-lua-api-behaviour.md` Sec26.25) | **1** (`mm_p_01`) |

Essentially one blocker now stands in front of 48/49 missions: **`named-object resolution['Killbane']`** -
the host has no spec'd way to resolve that name to whatever the engine expects (an object id/handle, most
likely a story-cutscene character reference given the name - see `spec-tables-vehicle-world.md` §6.3's own
`Killbane`/`IsAngel` fixed-name list for a plausible nearby precedent, not confirmed as the same mechanism).
The 49th (`mm_p_01`) is one step behind everyone else on an unrelated, genuinely unspecced zscene global.

## 2. New runtime stub ranking (name, calls, specced y/n)

Full table: `stub_ranking_with_specced_20261002.tsv` (69 rows, every name the hit log saw this run).
`specced` method: `y` = a dedicated, non-generic implementation exists (`src/lua_vint_resolution.cpp`,
`lua_cutscene.cpp`, `lua_screen_fade.cpp`, `lua_bare_globals.cpp`, `lua_tutorial_names.cpp`, or a
`registerOne(...)` entry in `lua_spec_confirmed_stubs.cpp`); `specced-OPEN` = a dedicated implementation
exists AND it's explicitly asking for one named engine value the spec doesn't give (an `OPEN_STATE:` row -
these are the direct candidates for "what to spec next", not generic gaps); `n(generic-stub)` = falls
through to the project's generic always-stub, no dedicated behavior modelled at all.

**The three `OPEN_STATE:` rows actually newly hit by mission driving this run** (`incremental_from_missions
> 0` - i.e. genuinely new from this pass, not already-known UI-tick noise):

| Name | Total calls | Incremental (this run's missions) | What's needed |
|---|---|---|---|
| `OPEN_STATE:named-object resolution['Killbane']` | 140 | 98 | the value(s) `named-object resolution` returns for the name `'Killbane'` |
| `OPEN_STATE:character max hit points (+0x1cac)` | 122 | 98 | the character-record field at `+0x1cac` |
| `OPEN_STATE:character ignore-AI flag (+0x2bc)` | 122 | 98 | the character-record field/flag at `+0x2bc` |

All three fire 98 times each (same call shape repeatedly across the same set of missions) - consistent with
one per-tick hook body that reads all three in sequence and gets stopped by whichever is checked first
(`Killbane`, by the first-error table above). The other `OPEN_STATE:` rows in the full TSV (safe-frame
source, Wwise id lookups, key bindings, co-op join type) are pre-existing/UI-side, not newly surfaced by
this mission-drive pass (`incremental_from_missions = 0` for all of them).

## 3. What the 39 suspended missions are waiting on

New host capability this pass: `verdict_mission_drive.tsv`'s `start_suspended_at` column captures the
*real, measured* Lua source location (script name, line number, enclosing function name only - **no source
text**, this reader never embeds script bodies) where each suspended thread's coroutine is actually parked,
via `lua_getstack`/`lua_getinfo` on the still-suspended coroutine.

All 39 land in exactly two functions of `game_lib.lua`:

| Where suspended | Missions |
|---|---|
| `game_lib.lua:1412` (in `fade_out_block`) | 28 |
| `game_lib.lua:1409` (in `fade_out_block`) | 9 |
| `game_lib.lua:1399` (in `fade_in_block`) | 2 |

All 39 are parked inside a fade-wait helper, almost certainly a `while not fade_is_fully_faded_<x>() do
thread_yield() end`-shaped poll (consistent with this run's own
`fade_completion_path=real:0 fallback_undefined:0 fallback_no_callback:3` and
`screen_fade_do_errors:5` lines - the host's fade-completion modelling doesn't reach a state these polls
accept as "done", so they never exit). This is a **separate** blocker from the resume-schedule question
(request 11 left "how often the engine resumes a yielded thread" OPEN, not invented) - even with a resume
loop, these 39 would keep re-yielding at the same fade-wait until the fade-completion modelling itself is
addressed.

## Method notes

- Fresh build from current `main` tip, no reused artifacts.
- `start_suspended_at` is read directly from the coroutine's own debug info after it's confirmed suspended
  (`lua_status(co) == LUA_YIELD`), not inferred or guessed.
- `specced` in the stub ranking is my own cross-reference against this codebase's dedicated-implementation
  files, not a field the tool itself tags - flag if a different definition would be more useful.
