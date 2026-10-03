# Mission drive — ranking tranches 15/17/20/22/23 real-hit batch (2026-10-03)

Orchestrator-directed pass over the entire remaining ranking backlog (spec-lua-api-behaviour.md §50-§59,
10 sections, tranches 18/17/15/20/19/22/21/23/25/24 — everything except tranche 16/§49, done separately):
implement ONLY names with a real, measured non-zero mission-drive call count; everything else stays an
ordinary generic logging stub.

## Method

1. Fresh build (`build_tranches`, then independently reconfirmed in a NEW `build_verify_realhits` dir),
   same commit baseline as `main` (`51456ee`).
2. Fresh real mission drive (`lua_host_run.exe` against `D:/Project Crreish/Saints Row 3 CRREISH/packfiles/pc/cache`
   with `tools/lua_all_registered_1490_tagged.txt`) **before** any code change: `results/verify_tranches5059/`.
3. Extracted every name identifiable from §50-§59's own text (208 candidates; a handful of names in a few
   subsections are never individually named in the condensed spec prose itself — see the implementing
   commit message for the exact per-section count) and cross-referenced them against that fresh
   `verdict_stub_hits_with_missions.tsv`.
4. Found 8 real hits (all `gameplay` cluster, all 1 static call site): `cutscene_play_check_done` (12121),
   `cutscene_play_do` (31), `group_create_hidden_do` (9), `group_create_do` (7), `dlc2_m02_clapboards_get` (6),
   `dlc2_m02_clapboards_reset` (1), `radio_newsbreak_clear` (1), `tutorial_lock` (1).
5. Implemented all 8 faithfully (see `src/lua_spec_confirmed_stubs.cpp`'s own batch header, right before
   `stub_tutorial_lock`, for the full per-name citation). Left everything else in all 10 sections as generic
   stubs.
6. Fresh mission drive **after** the change, same cache/tagged list, same tick budget: `results/
   verify_tranches5059_postimpl/` (dev build) and `results/verify_realhits_final/` (independent
   `build_verify_realhits` build) — **identical numbers in both**, confirming reproducibility.

## Before -> after (same commit baseline, same cache, same tick budget)

| metric | before (`verify_tranches5059`) | after (`verify_realhits_final`) |
|---|---|---|
| `missions_start_after_ticks` | still_suspended:**34** finished:**8** errored:**5** | still_suspended:**9** finished:**10** errored:**28** |
| `stop_reason_histogram` | budget_exhaustion:**35** completion:**14** | budget_exhaustion:**10** completion:**39** |
| `total_ticks_survived_across_all_missions` | 751 | 372 |
| `scheduler_passes` | own_resumes:716 own_thread_errors:5 | own_resumes:312 own_thread_errors:28 |

**25 missions moved out of permanent `still_suspended`** (34 -> 9): 2 now fully `finished`, 23 now reach a
defined `errored` state further down their own script (almost all still landing on the already-known,
already-OPEN `named-object resolution['Killbane']` item, a couple on other already-documented OPEN items —
`m19` on the game-clock hour byte, `m02` on a different named-object resolution target — none of these are
new or newly-discovered blockers, they are the SAME items this project's own baseline already names). This
is the expected, intended shape of "unstick a false-infinite-wait, reveal the real next blocker" — not a
new regression surface.

**Root cause, in one line**: `cutscene_play_do`/`cutscene_play_check_done` (tranche 23, §57.2) were generic
nil-returning stubs; `game_lib.lua`'s own `cutscene_play(name, ...)` wrapper calls `cutscene_play_do` once
then polls `cutscene_play_check_done()` in a tight `thread_yield` loop — with the stub always returning nil
(falsy), that loop never terminated, parking nearly every mission at the exact same source line
(`waiting_at=[...]:1364 (in cutscene_play)` — visible verbatim in `verify_tranches5059`'s own per-mission
detail) regardless of which mission or scene was involved.

## Verification

- `ctest -C Release` from `build_verify_realhits` (independent, NOT the dev build): **56/56** passed (55
  pre-existing + 1 new, `synthetic_luahost_tranche_realhits_test`).
- Mission-drive numbers from that same independent build match the dev-build run exactly (table above).
