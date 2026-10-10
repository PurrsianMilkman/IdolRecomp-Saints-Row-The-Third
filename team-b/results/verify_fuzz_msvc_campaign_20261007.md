# MSVC-native libFuzzer+ASan campaign, round 2 (2026-10-07)

Resumes the campaign started earlier the same day (`fuzz/msvc/campaign_20261007/`,
build dir `build_verify_fuzz_campaign`) after a session-wide rate-limit kill (not a
task failure) interrupted it between the `xtbl` and `d3d9bc` targets. No commits
existed yet in this worktree for the campaign beyond the already-merged fuzzing
infra (`4029515`), so this resumes the on-disk campaign state rather than
restarting from zero.

## Resume analysis (judgment call)

Before running anything, read all 3 existing logs under
`fuzz/msvc/campaign_20261007/logs/` end-to-end:

- `le_strings.log`: `Done 748424 runs in 3601 second(s)` + full stat block + a
  clean recommended-dictionary dump — the normal libFuzzer end-of-run banner,
  only printed on a clean `-max_total_time` exit, never on a crash.
- `vpp.log`: same shape, `Done 111712228 runs in 3601 second(s)`.
- `xtbl.log`: same shape, `Done 67676 runs in 3601 second(s)`.

All three logs are the **full, clean 3601-second (~1h) run**, not a partial one —
each hit its complete `-max_total_time=3600` budget and exited normally. No
`crash-*`/`oom-*`/`timeout-*` file exists anywhere in the worktree, and none of
the three logs contain `ERROR`/`AddressSanitizer`/`SUMMARY`/`crash` text.
`fuzz/msvc/campaign_20261007/artifacts/{vpp,xtbl,le_strings}/` are empty, confirming
the same thing from the other side.

`d3d9bc` had no log file at all, and its `corpus/d3d9bc/` (40 files) was
byte-for-byte the `seed_gen_msvc` output with **identical mtimes** to
`seeds/d3d9bc/` — i.e. it never ran. The kill landed in the gap right after
`xtbl` finished (its log's mtime) and before `d3d9bc` could start.

**Decision: do not re-run le_strings/vpp/xtbl.** They already have their full,
genuine hour each with a clean exit and zero findings; re-running would only
burn another 3 hours for no new coverage decision. Only `d3d9bc` needed a fresh
full hour, since it had never run at all.

The campaign build (`build_verify_fuzz_campaign/RelWithDebInfo/`) still had all
5 binaries (`fuzz_vpp_msvc`, `fuzz_xtbl_msvc`, `fuzz_le_strings_msvc`,
`fuzz_d3d9bc_msvc`, `seed_gen_msvc`) built from the exact current HEAD (no
source commits happened between that build and this resume), so no rebuild was
needed either.

## Run: `fuzz_d3d9bc_msvc`

```
build_verify_fuzz_campaign/RelWithDebInfo/fuzz_d3d9bc_msvc.exe \
    -max_total_time=3600 -artifact_prefix=fuzz/msvc/campaign_20261007/artifacts/d3d9bc/ \
    fuzz/msvc/campaign_20261007/corpus/d3d9bc
```

Ran the full 3601s, exited 0. `459409` executions, corpus grew `40 -> 999`
files. No `crash-*`/`oom-*`/`timeout-*` file, no `ERROR`/`AddressSanitizer`/
`SUMMARY`/`crash`/`leak` text anywhere in `logs/d3d9bc.log`. (This run's log
doesn't print the trailing `stat::` block the other three have after `Done ...
runs` — a cosmetic libFuzzer output difference, not a sign of early
termination: the process's own exit code was 0 and the `Done 459409 runs in
3601 second(s)` line, plus the full final-corpus dictionary dump right before
it, are the same clean-exit markers the other three logs end with.)

## Campaign totals (all 4 targets, this round + the earlier one combined)

| Target | Wall time | Executions | Exec/s (reported) | Corpus (seed -> final) | Crashes |
|---|---|---|---|---|---|
| `fuzz_vpp_msvc` (`vpp::Container`) | 3601s | 111,712,228 | 31,022 | 2 -> 135 | 0 |
| `fuzz_xtbl_msvc` (`sr3xtbl::ParseDocument`) | 3601s | 67,676 | 18 | 40 -> 488 | 0 |
| `fuzz_le_strings_msvc` (`sr3locstr::parseLeStrings`) | 3601s | 748,424 | 207 | 16 -> 180 | 0 |
| `fuzz_d3d9bc_msvc` (disassembler/CTAB/HLSL) | 3601s | 459,409 | ~128 (computed) | 40 -> 999 | 0 |
| **Total** | **~4h01m combined** | **112,987,737** | | | **0** |

Zero crashes, zero sanitizer reports, zero hangs across all 4 targets and all
~113M executions. `le_strings` in particular — called out in the task brief as
"not fuzzed anywhere before" and documented as claiming to bounds-check every
read the real loader doesn't — held up clean under a full hour and ~750K execs.

No reproducers to minimize, no new regression tests, no reader-code fixes this
round: `fuzz/msvc/README.md`'s regressions table stays `(none yet)`.

## Normal-suite confirmation

Separate default-options build, `build_verify_fuzz_default` (already configured
and built from the same unchanged HEAD as the campaign build — no source edits
happened this round, so no rebuild was needed). Fresh `ctest -C Release` run
after the campaign finished:

```
100% tests passed out of 81
Total Test time (real) =  57.75 sec
```

## Disk usage (for the cleanup batch — not deleted, per standing policy)

| Path | Size | Why |
|---|---|---|
| `fuzz/msvc/campaign_20261007/` | ~71 MB (corpus 65 MB, seeds 5.3 MB, logs 576 KB, artifacts/minimized empty) | Raw campaign corpus/logs; untracked, gitignored this round (see below) — same "stays local, never committed" convention `fuzz/README.md`'s own Clang-side `fuzz-work` directory already uses. |
| `build_verify_fuzz_campaign/` | ~110 MB | `CRREISH_FUZZ_MSVC=ON` sanitizer build, selected fuzz targets only. Already gitignored (`build_*/`). |
| `build_verify_fuzz_default/` | ~345 MB | Default-options build used for the normal-suite `ctest` confirmation. Already gitignored (`build_*/`). |

## This round's commit

Added `fuzz/msvc/campaign_*/` to `.gitignore` (the campaign output dir was
untracked but un-ignored, unlike the analogous Clang-fuzzer `fuzz-work` dir,
which is simply never placed inside the repo tree) and this verification doc.
No reader-code changes.
