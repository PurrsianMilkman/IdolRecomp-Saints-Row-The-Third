# Mission drive — post-Sec49 merge verification (2026-10-03)

Verified independently against a fresh build (`build_verify_s49`, commit `ee0400a`), separate from the
implementing agent's own dev build/worktree (`worktree-agent-a1914028d40e8caf0`, commit `15aebec`).

Merge had 5 conflicts (`CMakeLists.txt`, `include/sr3luahost/engine_state.h` (2 blocks),
`src/lua_engine_state.cpp`, `src/lua_spec_confirmed_stubs.cpp` (4 blocks, one with a genuine brace-loss
instance — `stub_vehicle_set_tire_damage_multiplier`'s closing `}`/`return 0;`/`}` had landed outside the
conflict markers as Sec49's own trailing content; restored from `git show 51456ee:...`), and
`tests/synthetic_luahost_test.cpp` (1 block, a real formula merge — the combined inventory-size `CHECK`
needed both `batch444546` and `ranking16` terms added together, not one replacing the other)). Every
other block was pure concatenation (self-closed on both sides), confirmed by reading full block content
before deleting markers.

`ctest -C Release`: **56/56 passed** (up from 55 — the new `synthetic_luahost_tranche16_test` registered
correctly, and the merged `synthetic_luahost_sec44_46_test`'s updated inventory-size formula also passes).

Real mission drive (`results/verify_s49_mission_drive/`): aggregate numbers are **identical** to the
pre-merge baseline (`results/mission_drive_20261003_post_sec4446.md`): `missions_with_start_call_ok=48/49`,
`missions_with_start_suspended=47/49`, `still_suspended:34 finished:8 errored:5`. Consistent with the
implementing agent's own scope-discipline finding (9 of Sec49's 25 names implemented — 8 explicitly
mandated crash guards/faithful quirks plus `spawning_boats`, the only name with a measured real hit;
`spawning_boats` registers 1 real call in this run's own `verdict_stub_hits_with_missions.tsv`, confirming
it was actually exercised) — nothing mission-blocking should have moved, and nothing did.

Verdict: merge `ee0400a` is sound.
