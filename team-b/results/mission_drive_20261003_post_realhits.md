# Mission drive — ranking tranches 15/17/20/22/23 merge verification (2026-10-03)

Verified independently against a fresh build (`build_verify_realhits_merge`), separate from the
implementing agent's own dev/verify builds. Merge into `main` (4e0c51d) had 3 conflicts (`CMakeLists.txt`,
`include/sr3luahost/engine_state.h`, `src/lua_spec_confirmed_stubs.cpp`) — `src/lua_engine_state.cpp`
auto-merged cleanly. Every block was pure concatenation EXCEPT 2 genuine brace-loss instances in
`lua_spec_confirmed_stubs.cpp` (the same bug class seen repeatedly this session): `stub_squad_enable`'s
closing `}`/`return 0;`/`}` had landed outside the markers as the other side's own trailing content
(restored from `git show` of each side's real original), and the same for
`stub_screen_capture_preview_should_upload`. Both restored and cross-checked (each of the 11 functions in
that stretch confirmed present exactly once by name, in order) before staging.

`ctest -C Release`: **59/59 passed** (up from 58 — the new `synthetic_luahost_tranche_realhits_test`
registered correctly).

Real mission drive (`results/verify_realhits_merge_mission_drive/`): **`still_suspended:9 finished:10
errored:28`** — an exact match to the implementing agent's own reported before/after table
(`results/mission_drive_20261003_tranches5059_realhits.md`), independently reproduced from a separate
fresh build against the real archive cache. Confirms the headline finding: `cutscene_play_do`/
`cutscene_play_check_done` (previously generic nil-returning stubs that never let `game_lib.lua`'s own
`cutscene_play()` wrapper's `thread_yield` poll loop terminate) were the dominant blocker parking nearly
every mission at the same source line regardless of scene — 25 missions move out of permanent
`still_suspended` (34→9), the rest reach their own real next blocker further down the script (almost all
the already-documented `named-object resolution['Killbane']` OPEN item), not a new or regressed surface.

Verdict: merge `4e0c51d` is sound, and is the single largest mission-drive improvement of the session.
