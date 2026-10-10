# Combined packet-rebuild + codebook-ordered-branch fix: full 493-record oracle sweep (2026-10-09)

**Branch:** `integration/vorbis-packet-plus-codebook-fix`
**Task:** merge two independently oracle-verified Vorbis bugfixes that had
never been checked together, then check the COMBINED result against an
independent oracle (vgmstream-cli) as broadly as possible - prior checks
only ever covered 5 records.

## What was integrated

Cherry-picked onto `088abac` (oracle-refutation diagnostics baseline, itself
already on this branch):

1. `41b2b58` (`fix/vorbis-wwise-packet-rebuild`) - per-packet audio layout
   rebuild per exe-confirmed spec-audio-runtime.md Sec11.11.4a: no
   packet-type bit on the wire, hardcoded 1-bit mode selector read
   sequentially with a one-packet submission lag, long-block window flags
   supplied by the caller from already-decoded state rather than peeked from
   a neighboring packet. New `resolveWwiseAudioModeBit()` /
   `rebuildWwiseAudioPacket()`, appended after the existing
   `rebuildVorbisSetupHeader()` body (src/wwise_setup_rebuild.cpp lines
   ~696-882 post-merge).
2. `68f2d4a` (same branch) - PCM-frame tail-trim to the `vorb` chunk's own
   exact sample-count field (`vorb.sampleCountFallbackField`), closing a
   consistent 0.01-0.02s over-decode caused by un-trimmed lapped-MDCT
   final-block ring-out.
3. `028b6ae` (`diag/vorbis-488-codebook-investigation`, stacked on
   diagnosis-only `0d88c43` which was deliberately NOT cherry-picked - pure
   additive diagnostic field exposure in a different region, line ~296,
   unneeded for this fix and confirmed not required by 028b6ae's own diff
   context) - `transcodeOneCodebook()`'s ordered-branch field-width bug:
   the 3-bit length-field-width was being read unconditionally after the
   `ordered` flag; fixed to read it only when `ordered==0` (the `ordered==1`
   branch instead reads a fixed 5-bit seed length, no width field at all).
   Confirmed against the public vgmstream reference implementation and this
   project's own spec-audio-runtime.md text. Took 488/493 -> 493/493
   structural `vorbis_synthesis_init()` passes.

**Conflict check:** all three cherry-picks applied with NO manual conflict
resolution (`git cherry-pick` auto-merged `src/wwise_setup_rebuild.cpp`
cleanly). Verified this was not luck: `41b2b58`'s hunks sit at lines
696-744+ (appending new functions after the end of
`rebuildVorbisSetupHeader()`), `028b6ae`'s hunks sit at lines 168-256
(entirely inside `transcodeOneCodebook()`), and `0d88c43`'s own skipped
8-line insert sits at line 296 (between the two, but never touched by
either integrated commit) - three disjoint regions of the same file, so a
plain patch-apply merges correctly with no overlap, confirming the prior
(untested) agent's reasoning.

Final commit: `8c10d26` (HEAD after all three cherry-picks; see `git log`
on this branch for the full per-commit messages, each retained verbatim).

## Build + test

Configured fresh (`cmake -S . -B build -G "Visual Studio 17 2022" -A x64`,
no pre-existing build dir in this worktree) and built Release, all 373
targets, 0 errors. Full `ctest -C Release -j 4`: **150 tests, 100% passed**
(148 passed + 2 skipped - `real_bink_frame0_decode_test` /
`real_bink_test_vector_validation_test`, the same pre-existing, unrelated,
expected Bink skips called out in prior commit messages on this branch).
`real_vorbis_decode_test` itself: passed, 49.67s. Zero regressions from
either fix or from their combination.

## Oracle tool extension

`tools/wwise_vorbis_oracle_extract.cpp` extended (not replaced - same tool,
per the task's own instruction) with an optional 4th CLI argument: a record
filter that matches either a 0-based index into the `recordsWithVorb`
population (493 total, same population `real_vorbis_decode_test` counts) or
a substring of the record's output tag name. Default behavior (argument
omitted) is unchanged - still walks and extracts every qualifying record.
Also added an explicit `FAIL ...: diagnostic: ...` stdout line for records
where `attemptWwiseVorbisDecode()` returns `ok==false` (previously silently
skipped), and the `vorbIndex`/`tag` fields are now printed on the `OK` line
too, for reproducibility.

## Oracle sweep methodology

- Oracle decoder: official prebuilt `vgmstream-cli` r2117
  (`vgmstream/vgmstream` GitHub release, asset `vgmstream-win64.zip`),
  downloaded fresh into an isolated scratch directory (no existing copy
  found under `D:\Project Crreish\` or any sibling worktree). **SHA-256
  verified: `6c4a8a3813864fefed081bbd337dbc0ad93bf88e0b92f5db98d7ab258b22dc6c`
  - exact match** to the hash both prior tasks (`results/verify_vorbis_
  oracle_comparison_20261008.md` and `0d88c43`'s own independent
  corroboration) used. `vgmstream-cli.exe` self-reports `r2117 (May 19
  2026)`, consistent.
- Extraction: ran the extended `wwise_vorbis_oracle_extract.exe` against
  `CRREISH_GAME_CACHE_DIR/cutscene_sounds.vpp_pc` with NO filter (full
  sweep) - **493/493 records now structurally decode (`ok==true`)**, up
  from 5/493 before either fix and 488/493 before the packet-rebuild fix
  specifically (codebook fix alone already got structural success to
  493/493; this sweep adds the independent oracle check on top of the
  COMBINED fix for the first time).
- For every record: ran `vgmstream-cli -o oracle_<tag>.wav raw_<tag>.wem`
  (default PCM16 mixdown, matching this project's own `writeWavPcm16()`
  output format) - **492 decodes run (1 record, `ct_kinzie_03_media`, was
  pre-extracted during tool validation and reused), 0 vgmstream-cli
  failures**.
- Compared every pair with a zero-lag Pearson correlation (stdlib `wave` +
  numpy; no scipy available in this environment, so no FFT full-lag search
  was run this time - not needed, see results) plus an exact
  frames/rate/channels match check.

## Results: 493/493 oracle-confirmed

**All 493 records: Pearson r > 0.9999 at zero lag AND exact decoded-length
match (frames, sample rate, channel count all identical to the oracle).**

- r range: min `0.999999999319`, max `0.999999999996`, median
  `0.999999999977` - every single record, not just the original 5.
- Channel-count distribution: 398 mono, 46 stereo, 49 six-channel (5.1
  surround, e.g. `story_01_intro_media`, `story_02_intro_media`,
  `story_02_outro_media`).
- Sample-rate distribution: 460 @ 48000 Hz, 33 @ 44100 Hz (e.g.
  `story_stronghold_*_media`).
- **Zero failures, zero distinct failure kinds to report** - every record
  in the population passed both the structural check and the independent
  oracle check. This supersedes the 2026-10-08 refutation
  (`verify_vorbis_oracle_comparison_20261008.md`, which found |r| < 0.02 for
  all 5 then-claimed successes): that refutation was correct about the
  state of the code at the time (packet rebuild was still wrong), and is
  fully resolved by `41b2b58`+`68f2d4a`, with the codebook fix (`028b6ae`)
  additionally required for 488 of the 493 records to reach
  `vorbis_synthesis_init()` at all.

No further bugs found; nothing attempted beyond diagnosis, per this
project's own no-invented-fixes discipline - there was nothing to fix.

## New reference clips (`D:\Project Crreish\audio-check\`)

Four newly oracle-confirmed records (none of the original 5
`ct_kinzie_03_media`/`zscene_01_01..04_media`), chosen for structural
variety, copied as `fixed2_<name>.wav`:

- `fixed2_story_stronghold_dk_01_media.wav` - 44100 Hz mono (distinct
  sample rate from the original 5, all of which are 48000 Hz)
- `fixed2_story_stronghold_ld_01_media.wav` - 44100 Hz stereo (distinct
  rate AND channel count)
- `fixed2_story_01_intro_media.wav` - 48000 Hz, 6-channel 5.1 surround
  (distinct channel count, longest/most complex record in the whole
  population at 10,883,551 frames)
- `fixed2_ct_angel_01_media_rec5.wav` - 48000 Hz stereo, different content
  prefix (`ct_` cutscene dialogue vs. `story_`/`zscene_`)

All four: r > 0.999999999, exact length match, per `pearson_results.csv`
(scratch, not committed - raw numbers reproduced in the table logic above).

## Build artifact

Build directory: `D:\Project Crreish\TEAM B\.claude\worktrees\
agent-vorbisintegration\build`, ~2.0 GB (fresh configure+build, this
worktree had no prior build dir). Flagged per project disk-space
discipline, not deleted - awaiting move to
`G:\AI\Claude Projects\recomp\TEAM-B-worktree-builds\agent-vorbisintegration\build`.
