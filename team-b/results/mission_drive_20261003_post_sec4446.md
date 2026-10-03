# Mission drive — post-Sec44/45/46 merge verification (2026-10-03)

Verified independently against a fresh build (`build_verify_s4446`, commit `8e7b246`), separate from
the implementing agent's own dev build. `ctest -C Release`: 55/55 passed (up from 54 - the new
`synthetic_luahost_sec44_46_tests` registered correctly).

Mission-drive numbers are **identical** to the pre-merge baseline (`results/mission_drive_20261003_clock_fix.md`):
`missions_with_start_call_ok=48/49`, `missions_with_start_suspended=47/49`,
`still_suspended:34 finished:8 errored:5`. Consistent with the implementing agent's own scope-
discipline finding (the 28 implemented names were selected by explicit mandate - crash guards,
faithful quirks, the Sec46.1 bit correction - not by measured call-count ranking) - nothing should
have moved, and nothing did.

Verdict: merge `8e7b246` is sound.
