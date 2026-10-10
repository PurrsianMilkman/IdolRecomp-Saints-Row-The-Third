# Wwise "vorb" setup/first-audio offset-pair re-hypothesis — first real decodes (2026-10-08)

**Task:** re-test a new offset hypothesis against the immediately prior pass's
population-wide REJECTION (`results/probe_vorb_seektable_relative_offsets_20261008.md`:
`setupPacketStart = dataChunkOffset + vorb[4:8) + vorb[16:20)` / `firstAudioPacketStart
= dataChunkOffset + vorb[8:12) + vorb[16:20)`, 0/493 plausible, `firstAudioPacketStart`
landing exactly on `dataEnd` for every record). New hypothesis (dispatched by the
orchestrator, independently before Team A's own correction landed on `main`):
`vorb[16:20)` (already independently CONFIRMED as the leading seek table's own exact
byte length) might double as the setup packet's own offset with no separate field, and
the real first-audio-packet offset might be the NEXT dword, `vorb[20:24)`.

**Result: HYPOTHESIS CONFIRMED, population-wide (493/493), and real decodes followed.**
Mid-task, the orchestrator relayed Team A's own direct-disassembly correction (commit
`8871fb6` on `main`, spec-audio-runtime.md Sec11.11.5) stating the exact same formula
under the real names (`this+0x48`/`vorb[4:8)` was always the first-audio-offset
TERM, not a setup-offset term — a semantic mislabel, not a bad formula). This
project's own independent re-derivation (real-data bit-trace + explicitly-authorized
vgmstream public-source reading) and Team A's disassembly converged on the identical
formula before the two were cross-checked against each other.

## Step 1 — the new formula, tested exactly as hypothesized

```
setupPacketStart      = dataChunkOffset + vorb[16:20)
firstAudioPacketStart = dataChunkOffset + vorb[20:24)
```

Tool: `tools/wwise_vorb_setup_audio_dword_probe.cpp`, same "one-shot real-data probe"
convention as every prior probe on this branch, same `probeSetupPacketBitBudget()`
plausibility gate (sane books/floors/residues/maps/modes counts, zero out-of-range
codebook indices, <=7 leftover bits), against all 493 real `"vorb"`-bearing records.

```
setupPacketStart < firstAudioPacketStart               : 493/493
both offsets within [dataOffset, dataOffset+dataSize]  : 493/493
16-bit length prefix @ setupPacketStart in bounds      : 493/493
INVARIANT (setupPacketStart+2+declaredLen == firstAudioPacketStart), EXACT: 493/493
audio region (dataEnd - firstAudioPacketStart) > 0                        : 493/493
  min=19232 max=7267413 mean=382787 bytes
```

The INVARIANT line is the headline structural result: the setup packet's own
declared 2-byte-length-prefixed payload lands **exactly** on `firstAudioPacketStart`
for every single real record — zero slack, zero exceptions. This alone is strong,
population-wide, falsifiable evidence the offset formula is right, independent of
whether the setup packet's own CONTENT parses as plausible Vorbis yet.

**Identity cross-check against Team A's own commit `8871fb6`** (`vorb[20:24) ==
vorb[4:8) + vorb[16:20)`, stated as 981/981 on Team A's own corpus): **493/493** on
this project's own 493-record corpus. The two formulas are the same number under
different field groupings.

## Step 2 — public-source packet-header-shape check (vgmstream)

Explicitly authorized this round: read vgmstream's own real source via WebFetch, for
format understanding only, never transcribed into this repository.

- `src/meta/wwise.c`'s own `case 0x2a:` (the exact vorb-chunk-size variant this
  project's own direct measurement already pinned at 493/493, same variant vgmstream's
  own comment names "Saints Row the 3rd (X360)-v53-most") sets `data_offsets = 0x10`
  and reads `seek_size = vorb[0x10:0x14)`, `audio_offset = vorb[0x14:0x18)` (i.e.
  `vorb[16:20)`/`vorb[20:24)` exactly), then computes `setup_offset = start_offset +
  seek_size` and the first-audio start as `start_offset + audio_offset` — field-for-
  field, arithmetic-op-for-arithmetic-op the same formula tested above.
- `src/coding/vorbis_custom_utils_wwise.c`'s own `setup_version_config()`: this
  project's confirmed version, `WWVORBIS_V53` (the version `case 0x2a:` selects), maps
  to `header_type == WWV_TYPE_2` — a bare 2-byte little-endian size prefix, **no**
  granule/sample-count field, used identically for the setup packet (`is_setup=1`
  takes the same `read_packet()`/`header_type` path) and every audio packet.

**Conclusion on the orchestrator's second question**: a packet-header-SHAPE mismatch
does **not** explain the prior pass's own 19–24-byte short-read finding. The existing
2-byte-length-prefix assumption was always correct for this exact version; only the
STARTING OFFSET was wrong, exactly as this project's own real-data findings already
showed and as vgmstream's own source now corroborates independently.

## Step 3 — two further bitstream-grammar fixes found by hand bit-tracing

With the offset fixed, every record now reaches the setup packet's own CONTENT, but
`probeSetupPacketBitBudget()`'s plausibility gate was still 0/493 — `ranToEnd=0/493`
(every record aborted inside `residue 0`'s own booklist read). Hand bit-tracing one
real record (`tools/wwise_setup_packet_bit_trace_probe.cpp`, `ct_angel_01_media.bnk_pc`
rec@2048) found the codebook and floor sections both fully self-consistent (codebook
indices all in-range with a sensible dim/entries/hasLookup progression; floor1's own
postlist-entry counts matched the class_dim arithmetic EXACTLY for both floors, 4 and
27 entries respectively) — the drift began exactly at `residue 0`, where `groupbook`
came out `80` against only 32 real codebooks (invalid by construction) and every
booklist entry was likewise out of range.

Cross-checking against vgmstream's own `vorbis_custom_utils_wwise.c` (read via WebFetch,
format understanding only) found two fields this project's prior transcode skipped
entirely:

1. **`residue_type`** (2 bits), read immediately before `begin`/`end`/`grouping` —
   this project previously hardcoded it by channel count and never read it from the
   input at all. Adding the 2-bit read: `groupbook` becomes `31`/`31` (both valid,
   <32) and every booklist entry (21–30) falls cleanly inside `[0,32)`, with a
   regular, plausible cascade pattern (4,4,4,4,4,3,7 repeating).
2. **The per-submap "time submap" placeholder byte** (8 bits) — this project
   previously assumed it ABSENT from the input (only synthesized fresh on output).
   vgmstream's own gloss: "time_config (8 bits, passed through as an unused
   placeholder)" — i.e. it genuinely IS present on the wire. Reading and discarding
   it: `floorsub`/`residuesub` go from always-0 (floor 1/residue 1 — the far more
   elaborate config this project had already correctly decoded — never referenced by
   any mapping) to a real, varied `(0,0)`/`(1,1)` split (mapping 0 → floor/residue 0,
   mapping 1 → floor/residue 1); one mode's own mapping index goes from out-of-range
   (`2` against `maps=2`) to valid; the packet closes with 4 leftover bits (was 20).

Both fixes applied to production code (`src/wwise_setup_rebuild.cpp`,
`transcodeOneResidue()`/`transcodeOneMapping()`), labelled `agrees-with-public-format`
(not `[CONFIRMED]` — that label is reserved for direct disassembly, which Team B does
not do; these two are this project's own real-data discovery, independently
corroborated by vgmstream's public source, not a disassembly answer from Team A).

Population-wide after both fixes: `probeSetupPacketBitBudget()` plausibility gate
**493/493** (up from 0/493); `ranToEnd` 493/493; `outOfRangeCount==0` 493/493;
leftover bits 0 (4 records), 2 (1 record), 4 (488 records) — all within the <=7-bit
byte-alignment slack every prior pass used as the bar.

## Step 4 — wiring into the real decode path, and the first real decodes

`src/real_vorbis_decode.cpp`'s `attemptWwiseVorbisDecode()` previously called
`walkFramedPackets(riffBlob, chunks.dataOffset, chunks.dataSize)` — i.e. assumed the
packet stream begins at the very first byte of "data", with no skip for the leading
seek table. Changed to compute `setupPacketStart = chunks.dataOffset + vorb[16:20)`
and walk from there (a single continuous walk naturally produces the setup packet as
`packets[0]` and every real audio packet as `packets[1..]`, since the INVARIANT_EXACT
finding above proves the setup packet's own declared length reaches
`firstAudioPacketStart` exactly — no second starting point is needed).

Re-running `tests/real_vorbis_decode_test.cpp` against all 493 real records:

```
banks walked                                : 86
records with a 'vorb' chunk                 : 493
reconstructed id+comment header accepted    : 493
transcode failed before reaching libvorbis  : 488   (NEW failure point - see Step 5)
setup header rejected, exactly OV_EBADHEADER: 0      (was the best-ever result before this pass: 5/493)
setup header rejected, OTHER code (BAD)     : 0
real decode SUCCEEDED (ok==true)            : 5      (was 0/493 every prior pass)
```

**5/493 real records decode all the way through `vorbis_synthesis_init` /
`vorbis_synthesis_blockin` / `vorbis_synthesis_pcmout` to real, non-silent PCM:**

```
ct_kinzie_03_media.bnk_pc : 1286720 frames, ch=1, 48000 Hz, 26.8067 s, peak=32767 (full scale)
zscene_01_01_media.bnk_pc :  509952 frames, ch=1, 44100 Hz, 11.5635 s, peak=32767
zscene_01_02_media.bnk_pc :  688128 frames, ch=1, 48000 Hz, 14.3360 s, peak=32767
zscene_01_03_media.bnk_pc :   14912 frames, ch=1, 44100 Hz,  0.3381 s, peak=32767
zscene_01_04_media.bnk_pc :   29696 frames, ch=1, 44100 Hz,  0.6734 s, peak=1581
```

First real decoded game audio, saved as a playable WAV:
`tests/golden/wwise_vorbis_decode/ct_kinzie_03_media_first_success.wav`
(2,573,484 bytes — exactly 44-byte header + 1,286,720 frames x 1 channel x 2 bytes PCM,
confirming the WAV writer itself is correct, not just the decode).

## Step 5 — the new, later, narrower residual (488/493), isolated precisely

Every one of the 493 records now gets a STRUCTURALLY AND SEMANTICALLY valid rebuilt
setup header — `vorbis_synthesis_headerin` (which itself calls `res0_unpack`/
`floor1_unpack` and validates `groupbook<books`, every booklist entry `<books`, the
`partvals<=entries` bound, etc.) accepts all 493/493 (0 `OV_EBADHEADER` — every prior
pass's best result was 5/493 even reaching this call). The failure moved to
`vorbis_synthesis_init` itself.

Isolated by instrumenting the actual failure with `rebuilt.codebookCount`
(`src/real_vorbis_decode.cpp`), not guessed:

```
442 records: codebookCount=32  -> vorbis_synthesis_init FAILS
 46 records: codebookCount=34  -> vorbis_synthesis_init FAILS
  4 records: codebookCount=35  -> SUCCEEDS
  1 record : codebookCount=42  -> SUCCEEDS
```

442+46 = 488 (exact match to the failure count); 4+1 = 5 (exact match to the success
count). This points at `vorbis_book_init_decode`
(`third_party/libvorbis/lib/sharedbook.c`'s own `_make_words()` Kraft-inequality check
— "the lengths must specify an overpopulated tree") failing for at least one specific
codebook-library slot present in the 32-book and 34-book selections but absent from
the 35/42-book ones. This is a codebook-LIBRARY-DATA question (spec-audio-codebook-
library.md's own transcription), not an offset or packet-framing question, and was
**not** attempted further this pass, per "no invented fixes" — not yet root-caused to
a specific slot index.

## Outcome against this task's deliverables

- New offset hypothesis: **CONFIRMED, population-wide, 493/493** — now matching Team
  A's own direct-disassembly answer (commit `8871fb6`) exactly, re-labelled
  `[CONFIRMED]` in production code.
- vgmstream's own packet-header-shape handling: **confirmed unchanged** (bare 2-byte
  prefix, no granule field) — the prior pass's 19-24-byte short reads were never a
  header-shape bug, only a wrong starting offset, exactly as Team A's own correction
  states independently.
- Two further bitstream-grammar fixes (`residue_type` read, time-submap byte read),
  labelled `agrees-with-public-format`, applied to production code.
- Real PCM decode: **5/493 succeeded** — first real decoded game audio this project
  has ever produced, saved as a playable `.wav`.
- New, later, narrower residual (488/493, isolated to specific codebook-count buckets)
  honestly reported, not forced past.

## Artifacts kept in the tree

- `tools/wwise_vorb_setup_audio_dword_probe.cpp` (+ CMake target): population-wide
  offset-formula + identity probe.
- `tools/wwise_setup_packet_bit_trace_probe.cpp` (+ CMake target): verbose bit-by-bit
  trace tool used to find the `residue_type`/time-submap fixes.
- `src/wwise_setup_rebuild.cpp`: `residue_type` read (was hardcoded), time-submap byte
  read (was assumed absent).
- `src/real_vorbis_decode.cpp`: `setupPacketStart`/`firstAudioPacketStart` now computed
  from `vorb[16:20)`/`vorb[20:24)` instead of walking from `dataChunkOffset` with no
  skip; failure diagnostic now includes `rebuilt.codebookCount`.
- `spec-audio-runtime.md`: brought current with `main`'s commit `8871fb6` via a clean
  cherry-pick (spec-text-only, physics/vehicle-damage files from the same commit left
  untouched as out of this branch's scope).
- `tests/golden/wwise_vorbis_decode/STATUS.txt` and
  `.../ct_kinzie_03_media_first_success.wav`: updated with this finding.
