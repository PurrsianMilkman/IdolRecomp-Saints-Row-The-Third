# Wwise "vorb"-chunk explicit offset-field hypothesis — population-wide test (2026-10-08)

**Task:** test whether `spec-audio-runtime.md` Sec11.11.2's own table row -

> `+0x7c`/`+0x80` (dwords) | vorb bytes 4-7, 8-11 | two offset-shaped fields
> (vorb[4:8] small/near-constant, vorb[8:12] varies widely — consistent with
> setup-packet-relative vs. first-audio-packet-offset); consumer still
> untraced | CONFIRMED location... HIGH on exact semantic role

— describes explicit `setupPacketOffset`/`firstAudioPacketOffset` fields
that would pin down the still-OPEN question commit `58e939f` (previous pass,
same branch) left unresolved: where the real Vorbis setup packet sits inside
the WAV `"data"` sub-chunk, relative to the leading per-audio-packet
seek/size table that pass found population-wide evidence for (see
`results/probe_wwise_setup_packet_framing_20261008.md`).

**Result: HYPOTHESIS REJECTED, population-wide (493/493 real records), in
all three placements tried.** No real PCM decode was produced this pass
either — this project's "no invented fixes" discipline means a rejected
hypothesis is reported honestly, not forced or papered over with a fourth
guess. One genuine, new, EXACT (zero-variance) fact was found as a
byproduct and is kept below as real forward progress even though it does
not, by itself, resolve the open question.

## Method

New diagnostic tool `tools/wwise_vorb_offset_probe.cpp` (not a ctest, same
"one-shot real-data probe" convention as `tools/wwise_setup_budget_probe.cpp`
from the prior pass), run against all 493 real `"vorb"`-bearing records in
`cutscene_sounds.vpp_pc` (`CRREISH_GAME_CACHE_DIR`). For each record: read
the "vorb" chunk's own raw bytes `[4:8)` and `[8:12)` as little-endian `u32`
fields (`fieldA`, `fieldB`) directly off `WwiseVorbChunk::raw42` — NOT the
already-relocated `blockSizeExponent0/1` (bytes 40/41) or
`sampleCountFallbackField` (bytes 0-3) positions `wwise_vorbis_setup.h`
already empirically corrected; those are different bytes, unaffected here.
Each placement's plausibility is checked with the EXISTING, already-
validated `sr3audioruntime::probeSetupPacketBitBudget()` (from commit
`58e939f`): a candidate setup-packet byte range is "PLAUSIBLE" only if it
runs to its own declared end with `<=7` leftover bits, zero out-of-range
codebook indices, and sane `books`/`floors`/`residues`/`maps`/`modes` counts
(each in `[1,256]`/`[1,64]`/`[1,64]`/`[1,64]`/`[1,64]` respectively) — the
same falsifiable bar the prior pass's own two rejected placements were held
to.

## Public-format corroboration attempted (per this task's explicit scope)

Before testing byte offsets, this pass checked whether the generic, public
Wwise-RIFX/Vorbis container format (as documented by `ww2ogg`/`vgmstream`,
both BSD-licensed) independently corroborates that a `setupPacketOffset`/
`firstAudioPacketOffset` field PAIR really exists in the real format (as
opposed to Sec11.11.2's own guess being unfounded). `xoreos`'s C++
reimplementation of `ww2ogg`, `Sound::WwRIFFVorbisStream` — its own
doxygen-generated class reference, read via `WebFetch`/`WebSearch`, NOT its
source — lists exactly these two private members by name:
`_setupPacketOffset: size_t` and `_firstAudioPacketOffset: size_t`,
alongside `_dataOffset`/`_dataSize`/`_sampleCount`/`_blocksize0Pow`/
`_blocksize1Pow`/`_loopStart`/`_loopEnd`/`_uid` (all of which line up with
fields this project has already separately identified in the "vorb" chunk).
**This is real, independent, public corroboration that the conceptual field
pair genuinely exists** in the real format — not an invented pairing. What
it does NOT give is the byte-level layout: the doxygen page documents field
NAMES and types only, no parsing logic, no offsets. Searching further for a
standalone prose/format-notes document describing the actual byte layout
(as opposed to the converters' own source, `ww2ogg`'s `wwriff.cpp`/`.h` or
vgmstream's Wwise meta parser) found nothing — every lead terminated at
"read the source to confirm the offsets," which this task's own hard rule
explicitly fences off ("fetch descriptive documentation/README/format-notes,
not source files"). No source was read; the byte-level test below is
therefore based ONLY on Sec11.11.2's own already-in-repo table row and this
project's own real-data measurement, not on any public documentation of the
actual offsets (none could be found within bounds).

## Placement 1 — literal: setup packet = `[fieldA, fieldB)`

`fieldA` (vorb[4:8)) is genuinely small and near-constant across the real
population, matching Sec11.11.2's own wording — only 4 distinct values
across all 493 records:

```
176: 442   186: 46   188: 4   221: 1
```

`fieldB` (vorb[8:12)) genuinely "varies widely" (range `[19420, 7267589]`,
mean `382964`) — but it tracks almost exactly with each record's OWN
`dataChunkSize` (every example checked: `dataChunkSize - fieldB` is a few
hundred bytes, e.g. `528`, `532`, `540`), not with a small "shortly after
the setup packet" value. Reading `[fieldA, fieldB)` as the setup packet
therefore means asking `probeSetupPacketBitBudget()` to parse a
**hundreds-of-kilobytes** span as a single Vorbis setup packet — implausible
on its face, and empirically confirmed so: every one of the 6 worked
examples produced `leftoverBits` in the **millions** and noise-shaped
floor/residue/map/mode counts, identical in character to the prior pass's
own rejected placements.

```
fieldA <= dataChunkSize                       : 493/493
fieldB <= dataChunkSize                       : 493/493
fieldA < fieldB                               : 493/493
PARSE([fieldA, fieldB)) PLAUSIBLE             : 0/493
```

## A new, exact, population-wide fact (byproduct, not itself a fix)

Measuring `(dataChunkSize - fieldB) - cleanRunEnd` (where `cleanRunEnd` is
the prior pass's own heuristically-detected end of the leading "clean/
structured, near-round-float" region) across all 493 records:

```
delta = (dataChunkSize - fieldB) - cleanRunEnd
min=4  max=4  mean=4   (zero variance - exact, not approximate)
```

This is a genuinely new, falsifiable, EXACT match — not the 0.2%-7%-band
ratio the prior pass's own leading-table finding rested on. It independently
confirms, via a second and completely different measurement (a real vorb-
chunk field, not a byte-pattern heuristic), that there is a real boundary
inside `"data"` at `trueTableEnd = dataChunkSize - fieldB`, and that this
boundary sits exactly 4 bytes past the prior pass's own heuristic estimate
(consistent with that pass's own noted limitation: "the shortfall is
consistent with this probe's own conservative cutoff clipping the table's
last few, less suspiciously-round entries"). **This elevates the prior
pass's "leading seek/size table" finding from population-wide-ratio
evidence to an exactly-pinned boundary** — real forward progress, kept here
even though (see below) it does not by itself resolve where the setup
packet's own bytes begin.

## Placement 2 — corrected: setup packet = `[fieldA, trueTableEnd)`

Re-testing with the newly-exact boundary instead of the literal `fieldB`:

```
CORRECTED PARSE([fieldA, dataChunkSize-fieldB)) PLAUSIBLE: 0/493
```

Still rejected. The hex dump of bytes starting at `fieldA` (e.g. `00 40 ee
07 00 40 33 07 00 40 e0 06 80 41 1d 09 ...`) shows the SAME repeating,
structured 2-byte-field pattern the prior pass's own cleanliness profiler
flagged as "clean"/non-bitstream-like — i.e. `fieldA` (176-221) still lands
INSIDE the leading structured region, not on real compressed-bitstream
noise. Whatever `fieldA` marks, it is not the start of the real setup
packet's own bytes.

## Placement 3 — combining two already-confirmed facts

Rather than guessing a fourth, independent placement, this test combines
two facts each separately confirmed (Sec11.11.1's own CONFIRMED "bare
16-bit little-endian length prefix" packet framing, and this pass's own new
exact `trueTableEnd` boundary): read a `u16` length prefix AT `trueTableEnd`
itself, then parse the following declared-length bytes as the setup packet.

```
LENPREFIX@trueTableEnd PLAUSIBLE: 0/493   (out-of-bounds length prefix: 0/493)
```

Still rejected — but the closest of the three. The declared length itself
is suspiciously near-constant and plausible as a real minimal setup-packet
SIZE (174 bytes for every mono record checked, 184 for the one 2-channel
record checked), and the resulting `books`/`floors`/`residues` counts are
small and sane (`books=32`, `floors=2`, `residues=2` — not the dozens-to-
hundreds of noise seen in placements 1-2), but `maps=0`/`modes=0` and every
record hits EOF before modes are reached (`ranToEnd=false`) — not a clean
result, and therefore not accepted.

## Outcome against this task's deliverables

- Population check of the literal Sec11.11.2 hypothesis: **REJECTED, 0/493
  plausible** (placement 1).
- Corrected-boundary retest: **REJECTED, 0/493 plausible** (placement 2).
- Framing-combined retest: **REJECTED, 0/493 plausible, though closest**
  (placement 3).
- Real PCM decode / `.wav` file: **not reached**. No synthetic, silent, or
  otherwise fabricated file was produced in its place, per this project's
  standing discipline.
- New confirmed fact kept for whoever picks this up next: `trueTableEnd =
  dataChunkSize - vorb[8:12)` is an EXACT (zero-variance, 493/493) real
  boundary inside `"data"`, 4 bytes past the prior pass's own heuristic
  `cleanRunEnd` estimate. The leading region `[0, trueTableEnd)` is now
  pinned exactly; where inside it the real Vorbis setup packet's own bytes
  begin is still genuinely OPEN.

## Relay to Team A (adds to the existing Sec11.11.1 spec-sync question)

In addition to the open question `results/probe_wwise_setup_packet_framing_
20261008.md` already raised (real per-packet seek/size table layout, entry
count, and setup-packet placement), this pass adds: Sec11.11.2's own table
row calling vorb bytes `[4:8)`/`[8:12)` "consistent with setup-packet-
relative vs. first-audio-packet-offset" does not hold under direct,
population-wide testing against real bytes in either its literal or
boundary-corrected form — `fieldB` behaves as a trailing byte-count from the
END of `"data"` (exactly `dataChunkSize - trueTableEnd`) rather than an
absolute "first audio packet" offset counted from the start, and `fieldA`
lands inside the same leading structured region the prior pass already
flagged, not on real setup-packet bytes. Sec11.11.5's own caveat that the
REAL consumed values (`this+0x48`/`this+0x4c`) are a File-side COMPUTED
COMBINATION of these raw dwords plus a separate 16-bit selector — not the
raw dwords themselves — is consistent with this negative result: this
project tested the raw, untransformed fields directly, which is exactly the
thing Sec11.11.5 itself flags as not yet traced to any consumer.

## Artifacts kept in the tree

- `tools/wwise_vorb_offset_probe.cpp` (+ `CMakeLists.txt` target
  `wwise_vorb_offset_probe`, diagnostic-only, not wired into the real decode
  path): the probe used to produce every number above, with its own top
  comment kept in sync with this writeup.
- `include/sr3audioruntime/wwise_setup_rebuild.h`'s own header comment: new
  dated UPDATE block pointing here.
- `tests/golden/wwise_vorbis_decode/STATUS.txt`: updated with this finding.
