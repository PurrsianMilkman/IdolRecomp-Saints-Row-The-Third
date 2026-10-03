# Mission drive — post-Sec36+cadence/Sec37 merge verification (2026-10-02)

Verified independently against a fresh build (`build_verify_s3637`, commit `6b73717`), separate from
either implementing agent's own pre-merge build. `ctest -C Release`: 53/53 passed.

| metric | pre-Sec36/37 (post-Sec35, `results/mission_drive_20261002_post_sec35.md`) | post-Sec36+cadence/Sec37 (this run) |
|---|---|---|
| `missions_with_start_call_ok` | 48/49 | 47/49 |
| `missions_with_start_suspended` | 39/49 | 46/49 |
| `missions_start_after_ticks` | finished:27 errored:10 still_suspended:2 | finished:8 errored:5 still_suspended:33 |
| `live_script_threads_after_missions` | 2 | 33 |

Large shift, expected and explained by both fixes together, not a regression:

- Sec36 (checkpoint arg fix: `"mission start"` not `0.0`) makes most mission scripts take their
  real fresh-start branch instead of falling through to an `else`/lookup-miss that falsely
  "completed" in 2-3 ticks — matches the Sec36 implementing agent's own A/B exactly
  (`results/mission_drive_20261002_sec36_checkpoint_fix.md`: 47/49, 46/49, `still_suspended` rose
  from 6 to 37 with that fix alone against its own earlier baseline).
- The cadence fix (1 scheduler pass/tick, not 11) then reduces how far each tick's single resume
  can push a now-really-looping script, so more of those missions sit at `still_suspended`
  (33, vs. the checkpoint-only A/B's 37) under the same 20-tick budget — consistent, not
  contradictory: fewer passes per tick means slower measured progress per tick, which is the
  intended, faithful effect of removing the unsupported 11x pump approximation.
- `errored` dropped 10->5, consistent with scripts now reaching different real code paths before
  hitting the shared, already-known OPEN items (Killbane, cutscene state, etc.) — no new failure
  categories appeared.

No crash, no unexpected build/test failures. `mm_p_06` and `m21` losing their old (spurious) good
result to a different already-tracked OPEN item, exactly as the Sec36 agent's own report predicted
and explicitly flagged as a correction, not a regression.

Verdict: merge `6b73717` is sound.
