# Next shared blockers past Killbane/max-HP, ranked by mission count (2026-10-03)

Real mission drive, `--tick-budget=721 --probe-unresolved-names` (the extended per-character probe,
commit `02b746f`), against the real archive cache, run fresh after the `cutscene_play_do`/
`cutscene_play_check_done` merge (`4e0c51d`). Raw output: `results/verify_probe_post_realhits/`.

Aggregate: `missions_start_after_ticks=still_suspended:18 finished:10 errored:19`. Probe resolved 50
distinct named objects (Killbane and max-HP among them — `probed_field_reads=26776`), so every blocker
below is genuinely PAST those two, not a repeat.

## 1. `action_play_do` / `action_play_non_blocking` — 7 missions, 49,655 real calls, no real implementation

- **Missions (still_suspended at 721-tick budget exhaustion):** `dlc3_m03`, `m06`, `m10`, `m11`, `m13`,
  `m14`, `m15` — all parked at the identical line `[string "game_lib.lua"]:563 (in
  action_play_non_blocking)`.
- **No `first_error` recorded for any of them** — same architectural shape `cutscene_play_check_done` had
  before its fix: a `thread_yield` poll loop against a native with no real behavior.
- **`action_play_do` has 49,655 incremental calls this run** (`verdict_stub_hits_with_missions.tsv`),
  the single highest call count of any name in this run by a wide margin, and **is not in
  `specConfirmedStubNames()`** (`src/lua_spec_confirmed_stubs.cpp`) — a pure generic logging stub, same
  state `cutscene_play_do`/`cutscene_play_check_done` were in before Sec57.2 closed them.
- **Most valuable single next target** — the same shape/signal that found the cutscene fix.

## 2. Character force-flag double-gate `(0x008ae480/0x008837a0)` OPEN — spec-lua-api-behaviour.md Sec3.7 — 10 missions

Missions: `dlc2_m01`, `dlc2_m03`, `dlc3_m02`, `m08`, `m17`, `m18`, `m22`, `m23`, `m24`, `sh04`.
A sibling vehicle-object form of the SAME gate also appears once: `m02` —
`engine state vehicle force-flag double-gate (0x008ae480/0x008837a0) is OPEN`
(spec-lua-api-behaviour.md Sec30.5).

## 3. Character state enum `(+0x16d4)` OPEN — spec-lua-api-behaviour.md Sec3.4/Sec3.10 — 3 missions

Missions: `m05`, `m07`, `m21`.

## 4. Single-mission items (1 mission each)

- `dlc2_m02` — `engine state group +0x40 head name['grp_cspawn_pistol_01'] is OPEN`
  (spec-lua-api-behaviour.md Sec28.6).
- `m19` — `engine state game clock hour byte (0x014ff33c) is OPEN`
  (spec-lua-api-behaviour.md Sec32.1/Sec48.2/Sec48.4) — expected, not new: Sec48.4's own documented
  forward-delta-then-forget behavior after a SECOND `set_time_of_day` call (the clock fix already closes
  the first call; this is `m19` calling it a second time).

## 5. Genuine Lua-runtime errors, not OPEN-state items (3 missions) — likely downstream of a different nil-returning native

- `m20` — `[string "m20.lua"]:694: attempt to compare nil with number`
- `m03` — `[string "m03.lua"]:1162: attempt to compare nil with number`
- `sh03` — `[string "game_lib.lua"]:1488: attempt to index local 'player_list' (a nil value)`

No `first_unimplemented_stub` is recorded for any mission this run (checked directly across the whole
TSV) — every blocker above is either an OPEN engine-state item or one of these three real script-level
nil errors, not a missing-stub refusal.

## Remaining still_suspended missions with no shared blocker (1 mission each, not ranked further)

`sh01`, `sh02`, `mm_p_01`, `mm_p_03`, `mm_p_07`, `m01`, `m04`, `m09`, `m12`, `m16`, `dlc3_m01` — each
parked at its own distinct script line, no cross-mission overlap found.
