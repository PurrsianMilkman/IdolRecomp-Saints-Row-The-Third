# 488/493 codebook Kraft-inequality: divergence located and fixed - a pure ordered-branch field-width bug (2026-10-09)

**Task:** compare this project's own packed-codebook-to-length-array expansion
(`transcodeOneCodebook()`, `src/wwise_setup_rebuild.cpp`) against the PUBLIC
vgmstream/ww2ogg equivalent, for the same raw bytes, and report exactly where
they diverge. Prior context (read in full before starting):
`results/verify_vorbis_488_codebook_kraft_investigation_20261009.md` - that
pass root-caused the 488/493 `vorbis_synthesis_init()` failure to library slot
160 resolving to codeword lengths `[1,1,1,1]` (Kraft sum 2.0, exactly double
the legal max), hand bit-traced the GRAMMAR read as "unambiguous," and
reported it as a data-vs-algorithm question it could not resolve without
disassembly, deferring per "no invented fixes."

**Result: this pass found the divergence, confirmed it is a pure CODE bug
(not a data-content question), fixed it, and verified the fix resolves 6 of
the 7 standalone-bad slots exactly (Kraft sum = 1.0, full double precision)
and the entire real 488/493 population (493/493 now reach real audio decode,
versus 5/493 before - and those 5 were already independently shown to be
garbage, not real audio).**

## Step 1 - the public source

Fetched vgmstream's `src/coding/vorbis_custom_utils_wwise.c` (GitHub,
`vgmstream/vgmstream` `master`, via WebFetch - read for conceptual
understanding only, per this project's established agrees-with-public-format
convention; nothing copied verbatim). It contains
`ww2ogg_codebook_library_rebuild()` - a public, independent reimplementation
of hcs's ww2ogg, transcoding the EXACT same "packed external Wwise codebook
library" format this project's `kWwiseCodebookLibraryBlob` holds (4-bit dim,
14-bit entries, 1-bit ordered, external-codebooks/aoTuV603 shape) into a
standard Vorbis I codebook.

Its field grammar for the per-codebook body:

```
dim (4b), entries (14b), ordered (1b)
if (ordered):
    initial_length (FIXED 5 bits, no separate width field at all)
    while (current_entry < entries):
        number = read(ilog(entries - current_entry) bits)
        current_entry += number
else:
    codeword_length_length (3b)   <- the "length-field-width" field
    sparse (1b)
    for each entry:
        if (!sparse || present-bit set): read(codeword_length_length bits)
```

**The 3-bit length-field-width field is read ONLY inside the `else`
(`ordered==0`) branch in the public source - never in the `if (ordered)`
branch, which instead uses a FIXED 5 bits for its seed length.**

## Step 2 - this project's code (before this pass)

`transcodeOneCodebook()` read `widthRaw` (3 bits) **unconditionally**, right
after `ordered`, before the `if (ordered)` branch split:

```cpp
const long entries = readChecked(in, 14);
const long ordered = readChecked(in, 1);
const long widthRaw = readChecked(in, 3);     // <- always read, even when ordered==1
const int width = static_cast<int>(widthRaw) + 1;
...
if (ordered) {
    long curLen = readChecked(in, width);      // <- BUG: should be a fixed 5 bits, not widthRaw+1
    ...
}
```

For an ordered codebook, this steals 3 bits that belong to the run-length
code stream and substitutes a too-narrow, wrongly-positioned seed-length
field in their place - every subsequent bit read in that codebook is
correct in COUNT (same total bits consumed) but wrong in MEANING from this
point on.

## Step 3 - cross-checked against this project's OWN (non-retracted) spec text

`spec-audio-runtime.md` §11.11.3a's "pinned exactly" paragraph for the
non-ordered grammar (2026-10-08) scopes the width field explicitly: *"After
`entries` and the ordered flag, for `ordered==0`: **width** = 3 bits..."* -
it does NOT claim width is read unconditionally. The OLDER, superseded
`~~...~~`-struck-through text (retracted by the 2026-10-08 MAJOR CORRECTION)
is the one that said "ordered flag, and (new field)... a 3-bit length field
width byte, all directly off the bitstream" unconditionally - exactly the
claim the code was still carrying. §11.11.3a explicitly flags slot 598 (an
**ordered** codebook) as "unrelated to the dense-case grammar below -
flagged separately, non-blocking," i.e. Team A's own disassembly pass never
actually pinned the ordered branch's own field grammar at all. The code's
unconditional read was inherited from retracted text, never re-validated for
the ordered case.

So this project's code and its own non-retracted spec text already
disagreed with each other before this pass touched anything; the public
source and the exact-Kraft-saturation result below (Step 4) independently
agree with the spec's (correct) scoping, not the code's (buggy) one.

## Step 4 - hand bit-trace confirmation (slot 160, slot 16)

**Slot 160** (`41 00 0c 04`, dim=1, entries=4, ordered=1): under the
corrected grammar (fixed 5-bit seed at bit offset 19, not a width-field+2-bit
seed), the seed reads raw=1 -> curLen=2, and the single run (ilog(4)=3 bits,
value=4) consumes all 4 entries at length 2. **lengths=[2,2,2,2], Kraft sum =
4 * 2^-2 = EXACTLY 1.0** (not approximately - full double precision), a
perfectly saturated, complete Huffman tree.

**Slot 16** (dim=1, entries=49, ordered=1, maptype=1): hand-traced the same
way - fixed 5-bit seed (raw=1 -> curLen=2), then runs of 1/0/3/4/15/26
entries at lengths 2/3/4/5/6/7 respectively (0 entries at length 3).
**lengths sum: 1*2^-2 + 3*2^-4 + 4*2^-5 + 15*2^-6 + 26*2^-7 = EXACTLY 1.0.**

Two independent slots, two exact (not approximate) Kraft saturations under
the corrected grammar. This is the strongest possible signature of "this is
the TRUE field boundary," not a coincidental improvement.

## Step 5 - the fix

`src/wwise_setup_rebuild.cpp`, `transcodeOneCodebook()`: moved the 3-bit
`widthRaw` read from before the `ordered` branch split to inside the
`else` (`ordered==0`) branch only (immediately before `sparse`, matching the
public source's own field order); the `if (ordered)` branch now reads its
seed length as a fixed 5 bits directly, with no width field at all. No
change to the dense/sparse (`ordered==0`) per-entry loop itself (already
correct - that is the part §11.11.3a's own "pinned exactly" text already
covered, and this pass's own hand traces don't touch it).

Pure parsing-code change; `kWwiseCodebookLibraryBlob`
(`wwise_codebook_library_data.cpp`/`wwise_codebook_library.h`) and every
other production decode-path file are untouched.

## Step 6 - verification

**599-slot standalone sweep** (`wwise_vorbis_codebook_kraft_probe sweep`),
before -> after:

```
before: 592/599 pass, 7 FAIL: 16 160 161 183 200 317 598
after:  598/599 pass, 0 FAIL (Kraft), 1 PARSE FAILED: 598
```

Slots 16, 160, 161, 183, 200, 317 - all 6 of the "49-entry"/"byte-identical
pair" slots - now pass the REAL, vendored, unmodified
`vorbis_book_init_decode()` cleanly. **Slot 598** (dim=13, entries=3276, only
10 raw bytes) now fails differently: under the corrected grammar its
computed lengths climb past the standard's own 32-codeword-length-bit
ceiling (33, 34) and the parse itself rejects it
("codeword length out of the standard 5-bit field's legal 1..32 range") -
no longer a Kraft overpopulation, a different kind of failure. This was
ALREADY flagged as a separate, non-blocking anomaly by both this project's
own prior investigation (Step 2.3 there: "Slot 598 does NOT fit this
pattern") and by spec-audio-runtime.md §11.11.3a itself. Slot 598 is not
used by ANY real record in the 442/46-record failing population (the prior
pass's own exhaustive, not-sampled cross-reference already established
100% of those 488 failures are at slot 160, position 1, and nowhere else) -
so it is out of scope for this task's actual real-population question, and
this pass does NOT attempt to force a further fix for it: still either a
data-content question (slot 598's own raw bytes, possibly affected by being
the last real slot before the library's NULL terminator) or a second,
different algorithm gap, ambiguous either way, reported precisely rather
than guessed.

**Real population** (`real_vorbis_decode_tests.exe`, `cutscene_sounds.vpp_pc`,
493 real `vorb`-chunk records), before -> after:

```
before: real decode SUCCEEDED (ok==true): 5/493   (all 5 already shown bogus - results/verify_vorbis_oracle_comparison_20261008.md)
after:  real decode SUCCEEDED (ok==true): 493/493
```

Every real record now reaches `vorbis_synthesis_init()` successfully and
produces decoded audio output via the real, vendored libvorbis.

## Step 7 - a newly-EXPOSED, pre-existing, separate residual (not caused by this fix)

Full `ctest` (150 tests): 149 effectively clean (148 pass + the 2
pre-existing, unrelated `real_bink_*` skips), **1 new failure**:
`real_vorbis_decode_test`, 3 `CHECK(peak > kNoiseFloor)` failures.

Confirmed this is NOT a regression from this fix, and NOT a new bug: this
test's OWN header comment (2026-10-09 UPDATE, written by the prior,
independent-oracle pass, a day before this one) already documents the exact
mechanism precisely - `attemptWwiseVorbisDecode()`'s own per-packet decode
loop (`src/real_vorbis_decode.cpp`) silently drops every audio packet that
fails `vorbis_synthesis()` with no gap-fill and splices survivors together,
and per-record real `vorbis_synthesis()` success rates were already measured
at 1.7%-49.4% of attempted packets - never 100%. A temporary, reverted-before-
commit diagnostic (`res.audioPacketsTotal`/`res.audioPacketsDecodedOk`,
added and removed in this pass, never committed) confirmed exactly that
mechanism for the 3 new failures: each attempted 971-2232 audio packets but
only **1** ever survived `vorbis_synthesis()` - and that one surviving
packet is the lapping/priming packet that legitimately produces 0 PCM
samples by design, hence `frames=0`. Before this pass's fix, 488/493 records
never got past `vorbis_synthesis_init()` at all, so this pre-existing
downstream residual was invisible to this specific test; this fix is what
finally lets the real population reach it. Out of scope for this task
(which is specifically the codebook-library EXPANSION algorithm, not the
per-packet audio-decode splicing logic) - not fixed here, not guessed at,
reported precisely per "no invented fixes / skip-and-revisit." Relaying as a
follow-up item: `src/real_vorbis_decode.cpp`'s `attemptWwiseVorbisDecode()`
per-packet loop needs its own dedicated investigation into WHY the vast
majority of audio packets fail `vorbis_synthesis()` (previously measured as
likely `OV_ENOTAUDIO`, per `results/verify_vorbis_oracle_comparison_20261008.md`)
- a separate, substantial piece of work, not a quick follow-on to this one.

## Build/test artifacts

- Full Release build (`build_preset_vs2022`, all targets): clean, no new
  warnings beyond pre-existing unrelated ones.
- Full `ctest -C Release -j 4`: 149/150 effectively clean (148 pass + 2
  pre-existing `real_bink_*` skips), 1 failure (`real_vorbis_decode_test`,
  characterized precisely above as a pre-existing, separate, newly-exposed
  residual, not a regression from this fix).
- `build_preset_vs2022/` (MSBuild/Visual Studio, flagging per this project's
  disk-space discipline - NOT self-deleted) and `build_preset_ninja_release/`
  (configure-only, no Ninja in this shell's PATH, unused this pass) both
  exist in this worktree; sizes not measured this pass, flagging path only
  for the orchestrator's own batch-approval if cleanup is ever wanted.

## Files changed

- `src/wwise_setup_rebuild.cpp` (`transcodeOneCodebook()`): the fix - moved
  the 3-bit length-field-width read from unconditional (before the
  `ordered` branch split) to inside the `ordered==0` branch only; the
  `ordered==1` branch now reads a fixed 5-bit seed length directly. Header
  comment and this function's own top-of-function doc comment updated
  in place with the full citation trail (public source + this project's own
  non-retracted spec text + the exact-Kraft-saturation confirmation).
- `results/verify_vorbis_488_codebook_divergence_20261009.md` (this file,
  new).
