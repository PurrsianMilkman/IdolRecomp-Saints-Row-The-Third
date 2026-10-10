# Independent-oracle re-verification after the Wwise audio-packet rebuild fix (2026-10-09)

**Task:** `results/verify_vorbis_oracle_comparison_20261008.md` (same branch's own prior
pass) REFUTED the "5/493 real Vorbis decodes" claim and bisected the cause to every real
audio packet failing `vorbis_synthesis()` with `OV_ENOTAUDIO` (packet index 2 onward),
because raw Wwise audio packets are not standard Vorbis I audio packets. Team A's
spec-audio-runtime.md Sec11.11.4a (2026-10-09, exe-confirmed from direct disassembly of the
real per-packet dispatcher `0x00fa4810`) gives the exact real bit layout: no packet-type bit
at all, a hardcoded 1-bit mode selector (not a computed `ilog(modeCount-1)` field), and
long-block previous/next window-flag bits that are never present on the wire - derived from
running decode state carried sequentially across packets instead. This task implements a
packet-rebuild step against that exact layout, drops the prior "splice survivors" behavior
(a single failed packet now fails the whole decode), and re-runs the SAME independent-oracle
method against the SAME 5 records to check whether the fix actually produces correct audio.

## What changed

- `include/sr3audioruntime/wwise_setup_rebuild.h` / `src/wwise_setup_rebuild.cpp`:
  `resolveWwiseAudioModeBit()` (new) reads ONLY a packet's own single hardcoded mode-selector
  bit and resolves it against the setup header's own already-rebuilt `modeBlockflags`.
  `rebuildWwiseAudioPacket()` inserts the standard packet-type bit (0) and mode-number field
  libvorbis's own `vorbis_synthesis()` requires, then - only for a long-block mode - the
  previous/next window-flag bits, now taken as plain caller-supplied booleans
  (`hasPreviousPacket`/`previousPacketIsLong`/`hasNextPacket`/`nextPacketIsLong`) rather than
  reading a neighboring packet's raw bytes itself (see "Correction" below).
- `src/real_vorbis_decode.cpp`: `attemptWwiseVorbisDecode()`'s per-packet loop now runs a
  strictly sequential, one-packet SUBMISSION-LAG pipeline - packet i's own mode bit is read at
  packet i's own turn; packet i itself is only rebuilt and handed to `vorbis_synthesis()` once
  the NEXT iteration has read packet i+1's own mode bit (supplying packet i's nextWindowFlag),
  or, for the last packet, once the loop ends. ANY failure (rebuild or `vorbis_synthesis()`)
  now fails the WHOLE decode (`DecodeAttemptResult::firstFailedAudioPacketIndex` names exactly
  which packet) - no more silent skip-and-splice.

### Correction made during this task (not present in the final committed code)

An uncommitted, mid-flight attempt on this same branch (interrupted by an infrastructure rate
limit before it could be verified or committed) had already implemented the no-packet-type-bit
/ hardcoded-1-bit-mode-selector / derived-window-flags mechanism, but resolved the derived
window flags by handing `rebuildWwiseAudioPacket()` a neighboring packet's own raw bytes
(`prevPacket`/`nextPacket` parameters) to peek at directly - mathematically the same final flag
VALUES, but exactly the mechanism Sec11.11.4a's own text rules out ("never read from the
bitstream at all, not even by peeking at neighboring packets' own raw bytes"), and in fact the
classic public ww2ogg-style technique this task's own original (superseded) brief had described.
Reworked to the sequential, one-packet-lag design above before any build/test/commit, so no
packet's bytes are ever touched outside its own turn in the loop - see the two functions' own
header comments for the full before/after story.

## Step 1-2 - oracle tool chain and real-bytes extraction (reused, not re-derived)

Same `vgmstream-cli` (BSD/ISC, `vgmstream/vgmstream` `r2117`) black-box binary and the same
`tools/wwise_vorbis_oracle_extract.cpp` diagnostic tool as the 2026-10-08 pass - unchanged.
The 5 oracle reference WAVs decoded by `vgmstream-cli` in that prior pass
(`oracle_<name>.wav`) are REUSED here unmodified (the raw record bytes are static shipped game
data; only this project's own decode changed) rather than re-invoking vgmstream. Re-ran
`wwise_vorbis_oracle_extract` against the SAME `cutscene_sounds.vpp_pc` to produce fresh
`ours_<name>.wav` from the now-fixed decode path - same 5 records, same bank-record indices.

## Step 3 - per-sample comparison, all 5 individually, exact (not rounded) numbers

Same method as 2026-10-08: zero-lag Pearson correlation over the overlapping prefix, PLUS a
full FFT-based normalized cross-correlation search over the entire overlap range (not just
zero lag), using the same `oracle_compare.py` script.

| record | ours duration | oracle duration | zero-lag r (exact) | best-lag r | best lag |
|---|---|---|---|---|---|
| ct_kinzie_03_media | 30.7773s | 30.7667s | 0.9999999999766274 | 1.0000 | 0 |
| zscene_01_01_media | 27.9989s | 27.9946s | 0.9999999999890619 | 1.0000 | 0 |
| zscene_01_02_media | 33.9747s | 33.9667s | 0.9999999999911575 | 1.0000 | 0 |
| zscene_01_03_media | 21.6744s | 21.6550s | 0.9999999999905416 | 1.0000 | 0 |
| zscene_01_04_media | 17.8373s | 17.8178s | 0.9999999999888927 | 1.0000 | 0 |

**Every one of the 5: zero-lag correlation > 0.999999999976 (not merely ">0.99" - essentially
1.0, with the residual below float round-trip noise), best lag = 0 samples (no time-shift at
all, unlike the 2026-10-08 refutation's own huge best-lags of -607315..+218589), and duration
matching to within 0.0107-0.0195 seconds (480-960 samples out of 786k-1.63M total frames,
0.03%-0.1% relative).** This is the diametrically opposite result from the refutation this
task started from (`|r| < 0.02` at every lag, for every file).

Supporting per-sample stats (same `oracle_compare.py` output), confirming the match is not an
artifact of the correlation metric alone:

| record | ours clip% | oracle clip% | ours zero% | oracle zero% | ours lag1-autocorr | oracle lag1-autocorr |
|---|---|---|---|---|---|---|
| ct_kinzie_03_media | 0.00 | 0.00 | 9.40 | 9.36 | 0.985 | 0.985 |
| zscene_01_01_media | 0.01 | 0.01 | 11.99 | 11.98 | 0.983 | 0.983 |
| zscene_01_02_media | 0.02 | 0.02 | 20.15 | 20.13 | 0.980 | 0.980 |
| zscene_01_03_media | 0.00 | 0.00 | 91.62 | 91.61 | 0.972 | 0.972 |
| zscene_01_04_media | 0.02 | 0.02 | 24.34 | 24.27 | 0.978 | 0.978 |

Every column matches ours-vs-oracle to within measurement noise - a sharp contrast with the
2026-10-08 refutation's own table (e.g. `ct_kinzie_03_media`: 8.22% clip vs oracle's 0.00%,
32.00% zero vs oracle's 9.36%, lag1-autocorr 0.738 vs oracle's 0.985).

## The one remaining residual: a small, consistent trailing-duration excess (not re-investigated further here)

In all 5 records, this project's own decode is LONGER than the oracle's by 0.01-0.02 seconds,
and the oracle's own full sample sequence is an exact prefix of this project's own output
(`n_overlap_samples` in the precise-correlation check equals the oracle's own frame count
exactly, every time). Consistent with this project's own decode including the lapped-MDCT's
final block's natural "ring-out"/padding samples that a real encoder's Ogg granule-position
trim (or an equivalent real exact-sample-count field this project does not yet consult) would
normally discard. **Not invented a fix for this here** (per this project's own "no invented
fixes" discipline) - it is a separate, much smaller, non-blocking residual from the packet-
rebuild bug this task targets (which is fully resolved: zero-lag r > 0.999999999976 for the
ENTIRE overlapping region, for all 5), flagged for whoever next touches trailing-sample/
granule-position handling in this decode path.

## Artifacts

- Fresh `ours_<name>.wav` (post-fix): written by this task's own rerun of
  `tools/wwise_vorbis_oracle_extract.cpp`, scratch location (not committed, same discipline as
  every other game-derived golden in this project).
- Oracle `oracle_<name>.wav` (reused, unchanged from 2026-10-08):
  `D:\Project Crreish\audio-check\`.
- `tests/real_vorbis_decode_test.cpp`'s own ctest run (same invocation as always) independently
  confirms the identical 5/493 successes and the identical per-record duration/frame/sample-rate
  values reported by `attemptWwiseVorbisDecode()` itself, not just this diagnostic tool.
