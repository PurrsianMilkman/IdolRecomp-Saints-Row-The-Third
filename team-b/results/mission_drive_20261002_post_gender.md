# Mission drive — post-player-gender/m21-fix merge verification (2026-10-02)

Verified independently against a fresh build (`build_verify_gender`, commit `2cfac17`), separate from
the implementing agent's own pre-merge build. `ctest -C Release`: 53/53 passed.

Before this fix (post-zscene, `results/mission_drive_20261002_post_zscene.md`), `m21` stopped on
`character +0xa41 gender byte['#PLAYER1#'] is OPEN`. After this fix, `m21` no longer stops there: it
now runs 3 ticks and hits the same already-tracked `Killbane` named-object-resolution OPEN item most
other missions hit (`stop_reason=completion`).

Aggregate numbers (`missions_with_start_call_ok=48/49`, `missions_with_start_suspended=47/49`,
`still_suspended:34 finished:8 errored:5`) are unchanged from the pre-fix baseline — `m21` was already
counted in the same bucket both before and after; only its own blocking reason changed, confirming
this closes the specific OPEN item it targeted without otherwise moving the aggregate.

This also confirms the implementing agent's own report (which predicted m21 would next hit
"character ignore-AI flag +0x2bc") was measured against a stale pre-Sec36 baseline and does not
describe current `main`'s actual behavior — current `main` already closes that flag via Sec34, and
Sec36's checkpoint fix changed which code path `m21` takes entirely. Not a defect in the merged fix
itself, just a reminder that "next blocker" predictions from a worktree branched before later fixes
land need independent re-measurement, not assumption.

Verdict: merge `2cfac17` is sound.
