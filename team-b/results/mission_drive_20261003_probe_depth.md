# Probe-diagnostic depth check: how far past Killbane do missions get? (2026-10-03)

Extends `--probe-unresolved-names`: per-character/per-vehicle OPEN fields that cannot have a real
value for a SYNTHETIC (probed) name anyway (e.g. max hit points depends on the character's own
preset, Sec34.2 - a probe object has no preset) now also fall back to a labelled placeholder via a
new `EngineState::probedFieldOr()`, gated the same way (off by default, real names never affected).
Wired into the two fields the orchestrator named: `character.ignoreAI` (fallback `false`) and
`character.maxHitPoints` (fallback `100`, both CHOSEN diagnostic placeholders, not spec facts).

## Result: no further OPEN-state blocker surfaces

Ran `--tick-budget=721 --probe-unresolved-names` again (`build_measurement`, current `main`).
`probed_field_reads=53508` confirms the new fallback fires extensively. The same missions that
previously stopped on `character max hit points (+0x1cac) is OPEN` now record **no first_error at
all** (verified directly against the raw TSV row, not just the console summary - the
`first_error`/`start_after_resume_error` columns are genuinely empty).

Their own yield point moved further: `start_suspended_at` (before any ticks) is still
`fade_out_block` (`game_lib.lua:1409`), but `start_still_suspended_at` (after the full 721-tick
budget) is `cutscene_play` (`game_lib.lua:1364`) - real forward progress through the script, not a
stall at the same spot. They now simply keep yielding there with no recorded error, for the full
budget, at any budget size.

## Reading this result

This is not "nothing left to find" - it means the remaining suspension in `cutscene_play` is not
caused by a missing engine value this project could set via `OpenValue`/`OpenValueMap`. The real
engine's own corresponding logic there most likely depends on functional cutscene-system state
(e.g. `zscene_is_loaded`/`cutscene_play_check_done`-style progress, Sec26.25) that needs an actual
state-machine advance to satisfy, not a default value - the same category of fix the zscene
per-frame-driver work and the resume-scheduler cadence fix already made elsewhere this session, not
a new OPEN spec gap. Diagnosing exactly which mechanism would require tracing `cutscene_play`'s own
logic past line 1364, which is out of this diagnostic flag's scope (and would mean reading the
shipped Lua script's own text, outside this project's clean-room sourcing boundary - the `waiting_at`
file:line/function the host's own `lua_getinfo` reports is the clean signal available here).

Verdict: the probe-diagnostic approach has reached its practical limit for this call site - further
depth needs a different investigation (how the real `cutscene_play` logic is structured), not a
wider probe.
