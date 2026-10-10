# Independent-oracle verification of the "5/493 real Vorbis decodes" claim (2026-10-08)

**Task:** the immediately prior pass on this branch (commit `a72320a`, already
merged to `main`) reported 5/493 real records decoding all the way through
`vorbis_synthesis_init`/`vorbis_synthesis_blockin`/`vorbis_synthesis_pcmout`
to non-silent PCM, and saved one of them
(`tests/golden/wwise_vorbis_decode/ct_kinzie_03_media_first_success.wav`) as
"first real decoded game audio." The orchestrator independently measured that
WAV's own raw sample statistics (8.2% hard clipping, 32% exact-zero samples,
RMS 10559, lag-1 autocorrelation 0.74, zero-crossing rate 0.16) and found a
pattern inconsistent with correct dialogue/cutscene audio - structured but
garbled, not merely "loud." This task runs a genuinely independent oracle
decoder against the same real bytes and checks per-sample, for all 5 claimed
successes individually, whether this project's own decode is bit-correct.

**Result: REFUTED, 5/5.** None of the 5 claimed successes correlate with the
real audio content at all - essentially zero correlation (|r| < 0.02) at
every lag, for every file, searched over the ENTIRE overlap range via FFT
cross-correlation, not just at zero lag. The underlying offset fix
(`setupPacketStart = dataChunkStart + vorb[16:20)`, `firstAudioPacketStart =
dataChunkStart + vorb[20:24)`) is NOT what this result calls into question -
see "What this result does and does not say" below. What is refuted is
specifically the claim that the full decode pipeline downstream of that
correct offset (codebook/floor/residue/mapping transcoding, the real Vorbis
bitstream decode) produces correct PCM for these 5 records. It does not.

## Step 1 - oracle tool chain

Downloaded the official prebuilt Windows binary release of `vgmstream-cli`
(BSD/ISC-style licensed, open source, public GitHub releases -
`vgmstream/vgmstream` tag `r2117`, asset `vgmstream-win64.zip`, sha256
`6c4a8a3813864fefed081bbd337dbc0ad93bf88e0b92f5db98d7ab258b22dc6c`), extracted
into its own isolated scratch directory, and ran the compiled
`vgmstream-cli.exe` as a pure black box - never read or copied its source,
only invoked the binary and compared its output samples. `vgmstream-cli`
recognized every one of the 5 raw record blobs directly (renamed `.wem`) as
"Audiokinetic Wwise RIFF header" / "Custom Vorbis" with no special handling
needed - it parses the exact same container this project does, from its own
independent implementation.

No `ww2ogg`/`revorb`/`ffmpeg` fallback was needed; `vgmstream-cli` worked
directly on the raw Wwise-wrapped bytes.

## Step 2 - same real source bytes, both ways

New diagnostic-only tool `tools/wwise_vorbis_oracle_extract.cpp` (not wired
into the real decode path, not a ctest) walks `cutscene_sounds.vpp_pc` from
`CRREISH_GAME_CACHE_DIR` exactly like `tests/real_vorbis_decode_test.cpp`,
and for every record where `attemptWwiseVorbisDecode()` reports `ok==true`,
dumps (a) the raw, untouched record bytes fed into that decode (`raw_<name>.wem`)
and (b) this project's own decoded PCM via the real `writeWavPcm16()` path
(`ours_<name>.wav`). Reproduced the prior pass's exact result:

```
records with a 'vorb' chunk : 493
real decode SUCCEEDED (ok)  : 5
```

Same 5 records, same frame counts/channels/sample rates as the prior pass's
own report (`results/probe_vorb_setup_audio_dword_20261008.md`):
`ct_kinzie_03_media.bnk_pc`, `zscene_01_01_media.bnk_pc`,
`zscene_01_02_media.bnk_pc`, `zscene_01_03_media.bnk_pc`,
`zscene_01_04_media.bnk_pc` (bank record #2).

`vgmstream-cli -o` decoded each raw `.wem` to a reference WAV.

## Step 3 - per-sample comparison, all 5 individually

Python (`numpy`) script: reads both WAVs' PCM (16-bit int or 32-bit float,
mixed to mono by channel-averaging if needed), computes zero-lag Pearson
correlation and SNR, then an EXACT FFT-based normalized cross-correlation
search over the full lag range spanning the entire shorter signal (not just a
small window) to rule out a simple time-shift/offset explaining the
mismatch.

| record | ours duration | oracle duration | zero-lag r | best-lag r (lag) | SNR (oracle ref) |
|---|---|---|---|---|---|
| ct_kinzie_03_media | 26.81s | 30.77s | -0.0006 | -0.0122 (lag=+218589) | -12.82 dB |
| zscene_01_01_media | 11.56s | 27.99s | 0.0001 | -0.0106 (lag=-440818) | -4.18 dB |
| zscene_01_02_media | 14.34s | 33.97s | 0.0020 | 0.0125 (lag=-607315) | -8.27 dB |
| zscene_01_03_media | 0.34s | 21.65s | 0.0000 | -0.0000 (lag=+2010) | -inf dB (oracle's first 14912 samples are silence lead-in) |
| zscene_01_04_media | 0.67s | 17.82s | -0.0000 | -0.0049 (lag=-27917) | -41.12 dB |

Every single one: |r| < 0.02 even at the BEST lag found by an exhaustive FFT
search over the full overlap range. This is statistically indistinguishable
from uncorrelated noise - not "mostly right with some drift," not "correct
but time-shifted." **None of the 5 claimed successes are bit-correct
decodes, or even recognizably-related-but-degraded decodes, of the real
audio.**

The oracle's own stats confirm what real dialogue/cutscene audio should look
like, and sharply contrast with this project's own decode:

| record | ours clip% | oracle clip% | ours zero% | oracle zero% | ours lag1-autocorr | oracle lag1-autocorr |
|---|---|---|---|---|---|---|
| ct_kinzie_03_media | 8.22% | 0.00% | 32.00% | 9.36% | 0.738 | 0.985 |
| zscene_01_01_media | 4.38% | 0.02% | 35.43% | 11.98% | 0.564 | 0.983 |
| zscene_01_02_media | 6.83% | 0.02% | 28.28% | 20.13% | 0.546 | 0.980 |
| zscene_01_03_media | 10.96% | 0.00% | 27.59% | 91.61% | -0.178 | 0.972 |
| zscene_01_04_media | 0.00% | 0.02% | 38.63% | 24.27% | 0.694 | 0.978 |

The `ct_kinzie_03_media` row matches the orchestrator's own independently-
measured numbers from its top-level report exactly (8.2% clip, 32% zero, RMS
~10559, lag-1 autocorr 0.74) - confirming this is the same file, same
problem, now explained (see Step 4), not merely re-measured.

## Step 4 - the precise divergence, bisected packet-by-packet

New diagnostic-only tool `tools/wwise_vorbis_packet_divergence_probe.cpp`
duplicates (does not modify) `src/real_vorbis_decode.cpp`'s own
per-audio-packet `vorbis_synthesis()` loop with per-packet pass/fail logging.
Findings, consistent across every record examined:

1. **The packet-framing walk itself is self-consistent to the exact end of
   the "data" sub-chunk for every record** (`framed.consistent == true`, 0
   leftover bytes) - this is NOT a boundary/offset bug; the walk reaches
   exactly where it should.

2. **Packet index 1 (the very first real audio packet) always succeeds**
   (`vorbis_synthesis() == 0`) but produces exactly 0 output samples - this
   is EXPECTED, correct Vorbis behavior (the first audio packet in a stream
   only primes the lapped-MDCT window; a real decoder also produces 0
   samples from it). Not a bug.

3. **Packet index 2 (the first real-content audio packet) fails EVERY
   SINGLE TIME, in every record examined**, with `vorbis_synthesis()`
   returning exactly `-135` (`OV_ENOTAUDIO` in libvorbis's own
   `codec.h` - the packet's type bit or decoded mode-number failed
   `vorbis_synthesis()`'s own basic structural check, before any
   floor/residue decode math even runs). This failure is not an isolated
   glitch - it starts a LONG CONTIGUOUS RUN of failures (16 consecutive
   failures for `ct_kinzie_03_media`; 769 consecutive failures - the
   overwhelming majority of the 948-packet stream - for
   `zscene_01_03_media`).

4. **Failures continue in long contiguous bursts throughout the entire
   packet stream**, not scattered single-packet noise - e.g.
   `ct_kinzie_03_media`'s run-length pattern: `OKx1 FAILx16 OKx11 FAILx23
   OKx32 FAILx2 OKx15 FAILx1 OKx5 FAILx4 ... FAILx79 OKx11 FAILx5` (204 runs
   total over 2544 packets); `zscene_01_04_media`'s: `OKx1 FAILx523 OKx6
   FAILx38 OKx8 FAILx41 OKx3 FAILx105 OKx7 FAILx22 OKx5 FAILx36` (12 runs
   over 795 packets, one single run of 523 consecutive failures). Overall
   per-record `vorbis_synthesis()` success rates: 49.4% (kinzie), 30.4%
   (zscene_01_01), 30.8% (zscene_01_02), 1.7% (zscene_01_03), 3.8%
   (zscene_01_04).

5. **The production code's own success gate never required full-stream
   success**: `src/real_vorbis_decode.cpp`'s audio-packet loop (lines
   ~282-304) silently `continue`s past every `vorbis_synthesis()` failure -
   no gap/silence fill, no decoder reset, no abort - and
   `attemptWwiseVorbisDecode()`'s own gate (line ~314) only checks
   `decodedOk == 0` to call it a hard failure; ANY nonzero `decodedOk`,
   even 1.7% of the attempted packets, sets `result.ok = true` and the
   surviving packets' PCM is concatenated as if they were a single
   continuous recording. Splicing non-contiguous Vorbis audio packets this
   way breaks the lapped-MDCT overlap-add every subsequent "successful"
   packet depends on, so even a packet that itself decoded without error is
   stitched against the WRONG previous window state - independently
   sufficient to explain the clipping/noise-floor/zero-crossing-rate
   pattern the orchestrator flagged, on top of the fact that roughly half
   to 98% of the real audio content was silently dropped rather than
   decoded at all.

**Honest conclusion on root cause, not guessed further (no invented fixes):**
this is not a subtle floor/residue rounding bug in an otherwise-correct
decode. It is a real, severe, population-consistent (observed in every
record examined) failure of `vorbis_synthesis()` itself on the majority of
real audio packets, starting at the very first real-content packet, in long
contiguous runs rather than scattered noise - most consistent with the
rebuilt setup header's own mode table (mode count / mode-to-mapping
associations) or an un-found residue/floor bit-budget error still being
wrong for real audio-packet content, even though it is coincidentally close
enough to pass libvorbis's OWN header-time structural checks
(`vorbis_synthesis_headerin` acceptance, `vorbis_synthesis_init` success) for
these 5 particular records. This is a NEW, more precisely located residual
than the already-known, separately-tracked 488/493 codebook-count/
Kraft-inequality issue (explicitly out of scope for this task) - these 5
records' setup headers are NOT among the 488 that fail
`vorbis_synthesis_init`, so this is a different bug, further downstream,
not yet root-caused to a specific field. Not attempted further here, per
this project's own "no invented fixes / skip-and-revisit" discipline.

## What this result does and does not say

- Does NOT call into question the offset fix itself
  (`setupPacketStart`/`firstAudioPacketStart` from `vorb[16:20)`/`vorb[20:24)`)
  - that remains [CONFIRMED] by Team A's own direct disassembly
  (spec-audio-runtime.md Sec11.11.5) and is independently corroborated here:
  the packet-framing walk is self-consistent to the exact end of "data" for
  every record, and the very first real audio packet is accepted by
  `vorbis_synthesis()` as structurally valid (0 samples, as expected).
- DOES call into question, and REFUTES, the claim that this project
  achieved "5/493 real decoded game audio" or "first real decoded game
  audio." None of the 5 is a correct decode. The orchestrator's independent
  measurement that triggered this task was right to be suspicious.
- The already-merged commit `a72320a` on `main` should NOT necessarily be
  reverted (the offset/grammar fixes inside it may still be real, partial
  progress - the packet framing IS self-consistent now, which it never was
  before that commit), but its own claim of "5 real decodes" needs to be
  walked back by whoever updates the record on `main` - this is relayed to
  the orchestrator for that correction, not handled by reverting the merge
  here.

## Artifacts

- `tools/wwise_vorbis_oracle_extract.cpp` (+ CMake target): dumps raw record
  bytes and this project's own decoded WAV for every `ok==true` record.
- `tools/wwise_vorbis_packet_divergence_probe.cpp` (+ CMake target):
  per-audio-packet `vorbis_synthesis()` pass/fail trace with run-length
  pattern summary.
- Both WAVs (ours + oracle reference) for all 5 records copied to
  `D:\Project Crreish\audio-check\` (`ours_<name>.wav` /
  `oracle_<name>.wav`) for direct listening/comparison - kept out of this
  repo, same as every other game-derived golden.
- `tests/golden/wwise_vorbis_decode/STATUS.txt`: updated to record this
  REFUTED result, labelled per this task's own "do not claim success"
  requirement.
- `include/sr3audioruntime/wwise_setup_rebuild.h` and
  `tests/real_vorbis_decode_test.cpp`: dated 2026-10-09 UPDATE notes
  appended to their own existing header comments (same convention as every
  other dated UPDATE in these files), correcting the "5/493 FIRST REAL
  DECODES SUCCEEDED" claim - comment-only, no behavior/logic change. The
  production `ok==true` success gate itself
  (`attemptWwiseVorbisDecode()`/`real_vorbis_decode.cpp`) was deliberately
  NOT changed - fixing it would mean guessing at the right behavior for the
  underlying per-packet failure, which this task's own "no invented fixes"
  constraint rules out without further investigation.
