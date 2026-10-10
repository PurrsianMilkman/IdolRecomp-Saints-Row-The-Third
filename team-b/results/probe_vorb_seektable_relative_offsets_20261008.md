# Wwise "vorb" seek-table-relative setup/first-audio offset formula — population-wide test (2026-10-08)

**Task:** re-test the disassembly-CONFIRMED formula spec-audio-runtime.md
Sec11.11.5 now states (commit `64f705a` on `main`, "Spec sync from Team A
(orchestrator): Wwise vorb: setup/first-audio offsets = vorb field +
seek-table length (vorb[16:20]) + data start (11.11.5); 11.11.2 downgrade;
11.11.6 fix" — verified directly against the real commit object, then
cherry-picked onto this branch as commit `c1f586e`, before trusting the
paraphrase it was relayed through) against all 493 real records, superseding
every earlier placement this project tried against vorb bytes [4:8)/[8:12)
taken literally without the seek-table term
(`results/probe_vorb_explicit_offset_fields_20261008.md`, 3 placements,
0/493 each).

**Result: FORMULA REJECTED, population-wide (493/493), as literally stated
and under 2 motivated alternate readings.** One EXACT, zero-variance,
493/493 falsification was found: the formula's own `firstAudioPacketStart`
computes to **exactly** the end of the `"data"` sub-chunk for every single
real record — leaving **zero bytes** for any audio packet, in a corpus
where every record demonstrably holds hundreds of kilobytes of real
compressed audio. No real PCM decode was produced this pass — per this
project's "no invented fixes" discipline, a population-wide-falsified
formula is reported honestly, not forced past with a fourth guess.

## What was independently verified before testing

The orchestrator's relay of Team A's formula was verified against the real
commit object before use (`git cat-file`, `git show 64f705a --
spec-audio-runtime.md`), per this project's standing "relayed technical
claims get verified, not dismissed or blindly trusted" practice — this
caught one small inaccuracy in the relay's own paraphrase: the rare
`selector==1` fallback's `setup_packet_start` term is `this+0xe0` (a live
decode-state field, not file/vorb-derived at all), not "`vorb[0xe0]`" as the
relay's prose stated — moot for this task since every real record here hits
the common/default path per the spec's own text, but worth noting since
catching exactly this kind of relay slip is the documented reason to verify
rather than skip straight to re-testing.

The commit was clean-cherry-picked onto this branch (`c1f586e`, spec-only,
no code) so this branch's own `spec-audio-runtime.md` reads the same
Sec11.11.2/11.11.5/11.11.6 text `main` does.

A parallel, now-superseded investigation into `ww2ogg`/`vgmstream`/`xoreos`'s
own public source (explicitly authorized for this round, read for format
understanding only, not transcribed) had independently converged on the
same structural shape — `vgmstream`'s `src/meta/wwise.c` names Saints Row:
The Third directly in its own vorb-size-variant switch (`case 0x2a: ...
Saints Row the 3rd (X360)-v53-most`, `data_offsets = 0x10`, i.e. the setup/
seek-table-size field pair at vorb bytes 16-23) and `ww2ogg`'s `wwriff.cpp`
places the same pair at the identical offset for its own `case 0x2A:` vorb
size. This cross-check is why vorb[16:20)'s role as the seek table's own
byte length is treated as solid below, independent of the disassembly
source — two separate public tools and Team A's own disassembly all agree
on that one field. **Stopped once the orchestrator's redirect landed**, per
instruction, in favor of re-testing the disassembly-confirmed formula
directly; kept here only as corroboration for the one sub-claim (vorb[16:20)
= seek-table byte length) that both sides' evidence agrees on.

## Method

New diagnostic tool `tools/wwise_vorb_seektable_offset_probe.cpp` (same
"one-shot real-data probe, not a ctest" convention as this branch's own
prior two probes), run against all 493 real `"vorb"`-bearing records in
`cutscene_sounds.vpp_pc` (`CRREISH_GAME_CACHE_DIR`). Plausibility uses the
SAME already-validated `sr3audioruntime::probeSetupPacketBitBudget()` every
prior pass on this branch used (sane books/floors/residues/maps/modes
counts, zero out-of-range codebook indices, ≤7 leftover bits).

## Step 1 — measure the real "vorb" chunk size directly (not assumed)

`findRiffSubChunk()`'s own returned size (the file's own declared RIFF
sub-chunk size field, not a hardcoded constant) across all 493 real records:

```
size=42 (0x2a): 493/493 records
```

Confirms this project's files are the `0x2a` (42-byte) Wwise vorb-chunk size
variant — the same variant both `ww2ogg`'s `wwriff.cpp` (`case 0x2A:`) and
`vgmstream`'s `wwise.c` (`case 0x2a: ... Saints Row the 3rd (X360)-v53-most`)
document by name, and the same variant spec-audio-runtime.md's own
commit-`64f705a` "chunk-size/layout question, answered" note states is the
only size seen across Team A's own 981-record sample. Three-way agreement
(Team B's own measurement, Team A's disassembly, two independent public
tools) — this part of the picture is solid.

## Step 2 — the formula as literally stated

```
setupPacketStart      = dataChunkOffset + vorb[4:8)  + vorb[16:20)
firstAudioPacketStart = dataChunkOffset + vorb[8:12) + vorb[16:20)
```

`dataChunkOffset` uses the exact same `chunkDataOffset = offset + 8`
convention `vorbis_container.h`'s `parseRiffWaveContainer()` already uses
for every other chunk (verified by direct read, not assumed) — i.e. the
`"data"` sub-chunk's own payload start, right after its own tag+size
dwords, matching the spec's own definition of `dataChunkStart` word for
word. No off-by-8 (header-vs-payload) mismatch is possible here.

```
seekTableLen (vorb[16:20)) % 4 == 0                    : 493/493
setupPacketStart < firstAudioPacketStart               : 493/493
both offsets within [dataOffset, dataOffset+dataSize]  : 493/493
16-bit length prefix @ setupPacketStart in bounds      : 493/493
INVARIANT (setupPacketStart+2+declaredLen == firstAudioPacketStart), EXACT: 0/493
PARSE([payloadOffset, payloadOffset+declaredLen)) PLAUSIBLE as a setup packet: 0/493
```

Every structural precondition holds (in-bounds, correctly ordered, a legal
16-bit length prefix at the computed position) — but the length prefix read
at `setupPacketStart` is implausibly tiny (19–24 bytes across every worked
example) for a packet that must hold a `books` codebook-index list alone
(the same worked examples read `books=87` off the first few bits — 87 × 10
bits = 870 bits ≈ 109 bytes, already exceeding the whole declared packet by
4-5×, before a single floor/residue/mapping/mode is even reached). Every
one of the 8 printed worked examples aborts immediately
(`ranToEnd=false`, multiple out-of-range codebook indices).

## Step 3 — the sharp, EXACT falsification (the formula's OTHER half)

Independent of whether `setupPacketStart` parses, `firstAudioPacketStart`
can be checked entirely on its own terms — how much room does it leave for
ANY audio packet?

```
dataEnd - firstAudioPacketStart == 0 (zero bytes left for ANY audio packet): 493/493
min=0 max=0 mean=0   (zero variance - exact, not approximate)
```

**For every one of the 493 real records, `firstAudioPacketStart` lands
exactly on the end of the `"data"` sub-chunk — zero bytes remain for any
audio packet.** Algebraically this traces to `vorb[8:12) + vorb[16:20) ==
dataChunkSize` holding exactly for every record (confirmed directly: e.g.
record `ct_angel_01_media.bnk_pc`@2048, `dataChunkSize=304757`,
`vorb[8:12)=304229`, `vorb[16:20)=528`, `304229+528=304757` exactly) — i.e.
`vorb[8:12)` behaves as "`dataChunkSize − seekTableLen`" by construction
(consistent with `vgmstream`'s own research-comment gloss for this same
byte position, `LoopInfo.dwLoopEndPacketOffset "(data end...)"`, when no
real loop is present — true of every record in this corpus). Whatever
`this+0x4c` is really consumed for by the Seek-shaped virtual call Team A's
disassembly found, it cannot be "the byte position where packet-stream
walking should resume to read real audio" in the sense this project needs —
every real file in this corpus holds the overwhelming majority of its
`dataChunkSize` as genuine compressed audio (the leading
seek-table-plus-profile-constant region is only ~530–750 bytes out of
~300,000+), so a "first audio packet" position at the very last byte of
`"data"` is not merely implausible, it is a hard, reproducible, 493/493
contradiction.

## Step 4 — two specific, motivated alternate readings (not blind guesses)

Before stopping, two of the most immediately-motivated alternatives to the
literal formula were tested directly, population-wide, against the same
plausibility bar:

```
(a) drop the vorb[4:8) term - setup = dataChunkOffset + seekTableLen           : 0/493
    (equivalent to ww2ogg's own simpler "setup_offset = start_offset +
    seek_size", and to the prior pass's own already-tried
    "LENPREFIX@trueTableEnd" placement, now using the EXACT seekTableLen
    field instead of that pass's own approximate cleanRunEnd landmark -
    same rejection, no new information, since trueTableEnd and
    seekTableLen are numerically identical on this data)
(b) subtract instead of add - setup = dataChunkOffset + seekTableLen - vorb[4:8) : 0/493
```

A bonus local-neighborhood scan (every byte offset from `setupPacketStart -
64` to `setupPacketStart + 256`, the same plausibility bar) found **zero**
positions where a 16-bit length prefix plus `probeSetupPacketBitBudget()`
parse is plausible, for any of the 8 worked examples — i.e. this is not a
small off-by-a-few-bytes slip fixable by local search; whatever is wrong is
structural, not a rounding error.

## Outcome against this task's deliverables

- Measured vorb chunk size: **42 bytes (0x2a), 493/493, confirmed directly**
  — matches the exact size variant both Team A's disassembly and two
  independent public converters (`ww2ogg`, `vgmstream`) document for this
  game.
- Re-test of the disassembly-CONFIRMED Sec11.11.5 formula: **REJECTED,
  population-wide, 0/493 plausible**, as literally stated.
- Sharpest finding: **`firstAudioPacketStart` computes to exactly the end
  of `"data"` for 493/493 records (zero variance)** — algebraically because
  `vorb[8:12) + vorb[16:20) == dataChunkSize` holds exactly for every
  unlooped record in this corpus, leaving no room for the audio these files
  demonstrably contain. This is offered back as a specific, falsifiable
  data point for Team A to check against their own trace (e.g. whether
  `this+0x4c` is genuinely consumed by the Seek call as a resume-reading
  position, versus as an end-of-valid-range bound for a Seek(position,
  length-or-bound) style API that would never need to double as a packet
  boundary) — not asserted as a disassembly correction, which Team B does
  not do.
- Two motivated alternate readings of the setup-packet half: **also
  REJECTED, 0/493 each.** A ±64/+256-byte local neighborhood scan around the
  formula's own computed position found no nearby fix either.
- Real PCM decode / `.wav` file: **not reached.** No synthetic, silent, or
  otherwise fabricated file was produced in its place.
- Production code (`wwise_setup_rebuild.h`/`.cpp`, `real_vorbis_decode.cpp`)
  was **not modified** — per this task's own instructions, a rejected
  hypothesis is not wired into the real decode path.

## Relay to Team A

This adds a third round to the existing Sec11.11.1/11.11.2/11.11.5
spec-sync thread: the newly-CONFIRMED formula, tested exactly as given
against this project's own 493-record corpus, produces one hard, exact,
population-wide contradiction (`firstAudioPacketStart` landing on the very
last byte of `"data"` for every record) plus a uniformly-implausible setup
packet placement (19–24-byte declared length where ≥109 bytes are needed
just for the codebook-index list). vorb[16:20)'s own role as the seek
table's exact byte length is NOT in question — it is corroborated three
ways (Team A's disassembly, this project's own measurement, and two
independent public converters' source, both naming this exact game). What
remains genuinely open is specifically what `this+0x48`/`this+0x4c` are
consumed FOR by the Seek-shaped virtual call once computed — Team B has no
way to settle this without disassembly, which Team B does not do.

## Artifacts kept in the tree

- `tools/wwise_vorb_seektable_offset_probe.cpp` (+ `CMakeLists.txt` target
  `wwise_vorb_seektable_offset_probe`, diagnostic-only, not wired into the
  real decode path): the probe used to produce every number above.
- `spec-audio-runtime.md`: brought current with `main`'s commit `64f705a`
  via a clean cherry-pick (`c1f586e`), spec-text-only, no code.
- `include/sr3audioruntime/wwise_setup_rebuild.h`'s own header comment: new
  dated UPDATE block pointing here.
- `tests/golden/wwise_vorbis_decode/STATUS.txt`: updated with this finding.
