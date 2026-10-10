# Wwise setup-packet out-of-range codebook index — root-cause narrowing (2026-10-08)

**Task:** diagnose and, if possible, fix the out-of-range (>=599) outer
codebook-library-index bug blocking the real 493-record Vorbis setup-header
sweep (`tests/real_vorbis_decode_test.cpp`, `cutscene_sounds.vpp_pc`), then
push through to a real decoded `.wav`.

**Result: not fixed. Root cause narrowed, with population-wide real-data
evidence, to a container/packet-framing bug — NOT codebook-library
completeness.** No real PCM decode was produced this pass. Relayed to Team A
as a spec-sync question (see bottom). Full narrative:
`include/sr3audioruntime/wwise_setup_rebuild.h`'s own header comment,
"UPDATE 2026-10-08" block; `tests/golden/wwise_vorbis_decode/STATUS.txt`.

## What was already known (commit `c0dd9df`, prior pass)

- Codebook BODY grammar (dim/entries/ordered/width/sparse/has-lookup/lookup
  table) is proven airtight: 599/599 real library codebooks parse with zero
  errors, zero mismatches (`tests/wwise_codebook_library_crosscheck_test.cpp`).
- On the real sweep: 5/493 records reach `vorbis_synthesis_headerin` (still
  rejected with `OV_EBADHEADER`); 488/493 fail earlier because one of the
  main packet's own per-codebook 10-bit library-index reads resolves to a
  value >= 599 (out of the real 599-entry codebook library).
- Two live hypotheses were identified and left open ("Team B never
  disassembles"): (a) the real codebook library is bigger than the 599
  entries transcribed, or (b) some other wrinkle exists in the main
  packet's own per-codebook-slot list encoding.

## What this pass found

Built a diagnostic (`sr3audioruntime::probeSetupPacketBitBudget()`,
`include/sr3audioruntime/wwise_setup_rebuild.h` /
`src/wwise_setup_rebuild.cpp`, diagnostic-only, never called from the real
decode path) plus a standalone tool (`tools/wwise_setup_budget_probe.cpp`,
registered in `CMakeLists.txt`, not a ctest) to walk a real setup packet
past an out-of-range codebook index instead of aborting — safe, because a
codebook's BODY is always read from a separate, isolated buffer sliced from
the static library; it never touches the main packet's own bitstream
cursor, whether the index resolves or not.

**Finding 1 — the bug is not confined to the codebook-index list.** Even the
5/493 records where every codebook index IS in range produce implausible
`floors`/`residues`/`maps`/`modes` counts once the walk continues past the
codebook list (e.g. `floors=59`, `modes=64`, `maps=62` for a record that
should need a handful of each). This is noise, not real encoder output —
meaning `rebuildVorbisSetupHeader()`'s reads were never landing on genuine
Vorbis setup-header content for ANY of the 493 real records, not just the
488 that visibly fail with an out-of-range index.

**Finding 2 — the WAV "data" sub-chunk does not start with a bare,
length-prefixed Vorbis setup packet the way `wwise_vorbis_setup.h`'s
`walkFramedPackets()` (spec-audio-runtime.md Sec11.11.1, "no lacing table")
assumes.** Its first several hundred bytes are a clearly structured,
non-bitstream-like region: successive 4-byte groups whose second half, read
as a little-endian float32, lands within 1% of a round number (32.01,
64.02, 4.00, 2.00, 8.00, 16.00, ...) for hundreds of consecutive bytes, then
**sharply** (not gradually) transitions to real high-entropy noise — see
the 32-byte-block cleanliness profile in the full tool output
(`100% -> 0%` within one 32-byte window, consistently, across every record
tested).

**Finding 3 — population-wide confirmation this is a real per-packet
seek/size table, not coincidence.** Reading that leading region as a
sequence of `(sampleCount: u16 LE, byteSize: u16 LE)` 4-byte entries and
summing every entry's FIRST field, up to where the "clean" pattern ends,
reproduces the `"vorb"` chunk's own INDEPENDENT total-sample-count field
(`sampleCountFallbackField`, Sec11.11.2/11.11.5) to within 0.2%-7%, for
**all 493/493 real records tested** — not a couple of hand-picked examples:

```
records with a usable clean run: 493/493
mean ratio=0.984787  min=0.930131  max=0.99807
ratio in [0.9,1.0): 493   (every other bucket from 0.5 to 1.6: 0)
```

The small shortfall (1.5%-7%, worst case) is consistent with this probe's
own conservative "within 1% of a round number" cutoff clipping the table's
last few, less suspiciously-round entries — not with the match being
coincidental. This is strong, falsifiable, real-data evidence that
Sec11.11.1's "no lacing table" claim does not hold for the real shipped
format: there IS a leading, per-audio-packet seek/size table, roughly one
4-byte entry per real audio packet, before the packet payload pool.
`walkFramedPackets()`'s current `packets[0]` is slicing into the MIDDLE of
this table, not a real setup packet — which is exactly why the main
cursor's subsequent reads (codebook count, 10-bit indices, floor/residue/
mapping/mode counts) come out as noise for literally every record.

## What is still genuinely open (not solved this pass)

Where the real stripped Vorbis setup packet actually lives relative to this
newly-found table was **not** pinned down. Two concrete placements were
tried and **both empirically rejected** against real bytes (neither
invented nor assumed):

1. **Immediately after the table's last detected entry** — the following
   bytes decode as `books=1` and the same absurd floor/residue/mapping/mode
   noise signature as before.
2. **A trailing region at the very end of "data"**, sized `(bytes left
   after the table) - (sum of the table's own byteSize fields)` — its
   leading bytes DO look like genuine high-entropy content (unlike #1), but
   the computed region's own size is wildly inconsistent across records
   with no consistent scaling (2939 / 1468 bytes for two channel=1 records
   vs. 15991 / 17131 bytes for two channel=6 records) and its parse still
   produces an implausible `books` count (209, 117, 80, 30) and dozens of
   out-of-range codebook indices, or an outright EOF — not a clean result
   either way.

## Relay to Team A (spec-sync question)

Sec11.11.1's packet-framing contract itself appears wrong for the real
shipped format (not the codebook library, which this pass has no reason to
doubt is correctly transcribed). Concretely: what is the real byte layout
of a per-audio-packet seek/size table entry in the WAV "data" sub-chunk,
how many entries does it really have, and where exactly does the actual
Vorbis setup packet sit relative to it (before the table, inside it, or
after the audio-packet payload pool)? This is not answerable from real-data
measurement alone without crossing into disassembly, which Team B does not
do.

## Outcome against this task's deliverables

- Index-valid count (0-598 for every codebook in a record) out of 493: **5**
  (unchanged from the prior pass — not improved, since the real bug is
  upstream of the codebook-index list itself).
- `vorbis_synthesis_headerin` ok==true count out of 493: **0** (unchanged).
- Real PCM decode / `.wav` file: **not reached**. No synthetic, silent, or
  otherwise fabricated file was produced in its place.

## Artifacts kept in the tree

- `include/sr3audioruntime/wwise_setup_rebuild.h` /
  `src/wwise_setup_rebuild.cpp`: added `probeSetupPacketBitBudget()`
  (diagnostic-only, additive, never called from the real decode path —
  `rebuildVorbisSetupHeader()` itself is byte-for-byte unchanged).
- `tools/wwise_setup_budget_probe.cpp` (+ `CMakeLists.txt` target
  `wwise_setup_budget_probe`): the standalone real-data probe used to
  produce every number above. Re-run it against
  `CRREISH_GAME_CACHE_DIR/cutscene_sounds.vpp_pc` for anyone picking this up
  next; its own top comment explains the method.
- `tests/golden/wwise_vorbis_decode/STATUS.txt`: updated with this finding.
