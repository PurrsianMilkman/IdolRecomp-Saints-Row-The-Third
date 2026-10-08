# Combined merge verification: cvtf §5.1/NEEDS-DATA + U1/U2 vint-property wiring + force-flag double-gate (2026-10-03)

Verified independently against one fresh build (`build_verify_combined`), separate from any implementing
agent's own dev build. Three merges landed together: the geometry §5.1/NEEDS-DATA reader-check batch, the
U1/U2 typed `vint_get_property`/`vint_set_property` wiring, and this project's own force-flag double-gate
fix (Sec3.7/Sec6.22, implemented directly rather than via a dispatched agent).

`ctest -C Release`: **61/61 passed** (up from 56 pre-batch — `synthetic_cvtf_test`,
`synthetic_vint_property_test`, and the already-landed `synthetic_vintdoc_document_test`/
`real_vintdoc_document_test` all registered and ran, none skipped).

## Default (20-tick, no probe) mission drive: unchanged, as expected

`results/verify_combined_default_mission_drive/`: `still_suspended:9 finished:10 errored:28` — identical
to the pre-batch baseline. None of these three merges change a default-budget mission's own start/suspend/
error outcome, because at the default budget most missions are still parked upstream of the force-flag
gate (on `Killbane`/max-HP, only reachable via the probe diagnostic) and the vint-property/cvtf work isn't
on any mission-drive script's own critical path.

## Probe-mode (721-tick, `--probe-unresolved-names`) mission drive: the force-flag fix's real reach

`results/verify_combined_mission_drive/`: `still_suspended:25 finished:10 errored:12`, vs. the pre-merge
probe baseline (`results/verify_probe_post_realhits/`) `still_suspended:18 finished:10 errored:19`.

Read in isolation this looks like a regression (`still_suspended` up 7). It is not — a per-mission diff
against the pre-merge probe baseline shows **exactly the 11 missions the probe report named as blocked on
the force-flag double-gate all moved**, each to its own real next point further down its own script, never
back to where it started:

| Mission | Before | After |
|---|---|---|
| `dlc2_m03`, `dlc3_m02`, `m02`, `m17`, `m22`, `m24`, `sh04` | errored (force-flag gate OPEN) | still_suspended, parked deeper in the script (real forward progress, not a new error) |
| `dlc2_m01` | errored (force-flag gate OPEN) | errored (character state enum `+0x16d4` OPEN, Sec3.4/Sec3.10 — a genuinely different, deeper OPEN item) |
| `m08` | errored (force-flag gate OPEN) | errored (`m08.lua:910: attempt to compare nil with number` — a real script-level issue, not an OPEN-state refusal) |
| `m18` | errored (force-flag gate OPEN) | errored (character max hit points `+0x1cac` OPEN, Sec7.12/Sec7.31/Sec34.2 — already-catalogued) |
| `m23` | errored (force-flag gate OPEN) | errored (continuous-explosion definition row `['Daedalus']` OPEN, Sec28.16 — a new, specific item) |

Every one of the 11 genuinely progresses past the force-flag gate; none regress. The net `still_suspended`
increase is the same "unstick a false blocker, reveal the real next one" shape this session's prior fixes
(cutscene_play, the ranking-tranche real-hits) already established — not a defect in this fix.

## Reviewed directly before merge (not taken on any report's word)

- `src/vint_engine_state.cpp`'s tween-redirect handling (`setVintProperty`): confirmed the write always
  targets the function's own `handle` parameter (the tween), never the resolved target's handle — matches
  Sec20.1's correction exactly.
- `EngineState::setVintScreenSize`'s divide-by-zero guard: confirmed placed before the division, both axes
  checked, counted via a HOST-SAFETY counter.
- `include/sr3vehicle/cvtf.h`/`src/cvtf.cpp`'s real-byte header-size fix and the new dword-offset-array
  Region 2/Family-name reader.
- The force-flag double-gate fix's own 3 call sites (`CharacterState::forceFlagGatePasses`,
  `VehicleState::forceFlagGatePasses`, `CounterOnGrabbed::gatePasses`) — confirmed via a repo-wide grep for
  every reference to `0x008ae480` that no fourth caller was missed.

Verdict: all three merges are sound.
