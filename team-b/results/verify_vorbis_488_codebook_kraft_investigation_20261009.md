# 488/493 `vorbis_synthesis_init()` failure: root-caused to library slot 160 (Kraft-inequality), diagnosis only (2026-10-09)

**Task:** diagnose, not fix, the 488/493 `vorbis_synthesis_init()` failure
`wwise_setup_rebuild.h`'s own "STILL genuinely OPEN" note already pointed at
`vorbis_book_init_decode()`'s own Kraft-inequality check
(`third_party/libvorbis/lib/sharedbook.c`'s `_make_words()`), with
`rebuilt.codebookCount` exactly 32 (442 records) or 34 (46 records) for every
failure, vs. 35 (4) or 42 (1) for the 5 successes - but never located which
specific library slot(s), or confirmed the hypothesis against real records.
Report the exact failing field precisely; fix only if the root cause is
simple and certain (per this task's own "no invented fixes" scope).

**Result: root-caused to an exact slot, with zero exceptions across the
whole real population - not a probabilistic pattern, a deterministic one.**
Library slot 160 alone accounts for literally all 488 failures in this
corpus. The defect is in this project's own compiled-in codebook-library
DATA (`kWwiseCodebookLibraryBlob`), not a bitstream-grammar/field-width bug
(hand bit-traced, airtight), and not a property of the real game data
(vgmstream decodes the same bytes fine). **Not fixed** - no clean-room way
to recover the TRUE correct bytes for the bad slot(s) without disassembly,
which Team B does not do.

## Step 1 - does vgmstream decode a codebookCount=32 and a codebookCount=34 record?

New diagnostic tool `tools/wwise_vorbis_codebook_kraft_probe.cpp` (`record`
mode) walks `cutscene_sounds.vpp_pc` exactly like
`tests/real_vorbis_decode_test.cpp`, buckets every real record by its own
`rebuilt.codebookCount`, and dumps the picked record's raw, untouched bytes
(same "duplicate, don't modify the real decode path" convention every other
probe on this branch already uses).

Picked `ct_angel_01_media.bnk_pc` record #0 (codebookCount=32) and
`ct_angel_01_media.bnk_pc` record #5 (codebookCount=34), plus one passing
record from each bucket for a baseline (`ct_kinzie_03_media.bnk_pc` #0,
codebookCount=35; `zscene_01_02_media.bnk_pc` #0, codebookCount=42). Ran the
official prebuilt `vgmstream-cli` (`vgmstream/vgmstream` tag `r2117`, asset
`vgmstream-win64.zip`, SHA-256
`6c4a8a3813864fefed081bbd337dbc0ad93bf88e0b92f5db98d7ab258b22dc6c` - same
binary, same hash, as the prior independent-oracle task - downloaded fresh
into its own isolated scratch directory, run as a pure black box, never read
as source) against the SAME raw bytes:

| record | codebookCount | vgmstream recognizes? | vgmstream decodes? | duration | clip% | lag-1 autocorr |
|---|---|---|---|---|---|---|
| ct_angel_01_media #0 | 32 | yes (Custom Vorbis / Wwise RIFF) | yes, full length | 46.319s | 0.00% | 0.923 |
| ct_angel_01_media #5 | 34 | yes | yes, full length | 46.319s | 0.00% | 0.986 |
| ct_kinzie_03_media #0 | 35 (passing) | yes | yes, full length | 30.767s | 0.00% | 0.985 |
| zscene_01_02_media #0 | 42 (passing) | yes | yes, full length | 33.967s | 0.02% | 0.980 |

All four decode cleanly, full length (WAV byte counts match the reported
sample counts/channels exactly), with real-audio-shaped statistics (near-zero
clipping, high lag-1 autocorrelation, consistent with the oracle reference
numbers the prior independent-oracle task already established). **This rules
out "the record is malformed" or "uses a format vgmstream also can't
handle" for both failing buckets** - these are ordinary, valid, decodable
records; this project's own rebuild is what is wrong, not the data.

## Step 2 - instrumenting `vorbis_synthesis_init()`'s own failure

Read `third_party/libvorbis/lib/block.c` (`vorbis_synthesis_init` ->
`_vds_shared_init`, decode branch, line ~234-246) and
`third_party/libvorbis/lib/sharedbook.c` (`vorbis_book_init_decode`, line
327; `_make_words`, line 79) directly - this project's own vendored,
unmodified copy of the real open-source decoder library, same footing
`wwise_setup_rebuild.h`'s own header comment already claims for `info.c`
elsewhere on this branch. Confirmed the exact call chain and the ONLY
failure path:

```
vorbis_synthesis_init()
  -> _vds_shared_init(..., encp=0)
       for i in 0..ci->books-1:
         if (vorbis_book_init_decode(ci->fullbooks+i, ci->book_param[i]))
           goto abort_books;   // <- the ONLY failure path, all-or-nothing
       -> vorbis_book_init_decode()
            codes = _make_words(s->lengthlist, s->entries, c->used_entries);
            if (codes == NULL) goto err_out;   // <- Kraft-inequality check
```

`_make_words()`'s own comment states the check directly: "error condition;
the lengths must specify an overpopulated tree" (line 96-100, the
`entry>>length` test) and the underpopulated-tree rejection (line 134-144).
Both are pure functions of one codebook's own `entries`/`lengthlist` -
nothing else, no cross-codebook state, no maptype dependency (`maptype`
only gates `_book_unquantize()`, a separate, later step).

**(1) Standalone 599-slot sweep** (`wwise_vorbis_codebook_kraft_probe
sweep`): for each of the 599 real library slots, parsed it with this
project's own already-599/599-cross-checked `parseOneCodebookForCrossCheck()`
(now additionally exposing the resolved `lengths` list -
`CodebookParseInfo::lengths`, a new, purely-additive diagnostic field, no
behavior change), built a minimal local `static_codebook` from
dim/entries/lengths alone, and called the REAL, linked, unmodified
`vorbis_book_init_decode()` on it directly - no record, no setup-header
context at all. Exactly 7/599 slots fail:

```
slot  16: dim=1 entries=49 ordered=1 maptype=1 kraftSum=2.0 (overpopulated)
slot 160: dim=1 entries= 4 ordered=1 maptype=0 kraftSum=2.0 (overpopulated)
slot 161: dim=1 entries= 4 ordered=1 maptype=0 kraftSum=2.0 (overpopulated)
slot 183: dim=1 entries=49 ordered=1 maptype=1 kraftSum=2.0 (overpopulated)
slot 200: dim=1 entries=49 ordered=1 maptype=1 kraftSum=2.0 (overpopulated)
slot 317: dim=1 entries=49 ordered=1 maptype=1 kraftSum=2.0 (overpopulated)
slot 598: dim=13 entries=3276 ordered=1 maptype=0 kraftSum=197.909667969 (overpopulated)
```

592/599 pass cleanly. Because the Kraft check is a pure per-slot property,
this result is unconditional: ANY record selecting one of these 7 slots will
fail `vorbis_book_init_decode` for that slot, full stop, regardless of which
record or which other codebooks it also selects.

**(2) Population cross-reference, exhaustive, not sampled**: re-ran all 442
real codebookCount=32 records and all 46 real codebookCount=34 records
(every one, not a sample) through the full header-accept pipeline, then
reached into each record's own already-libvorbis-unpacked
`vorbis_info::codec_setup` (via a small C-linkage bridge,
`tools/wwise_vorbis_codec_setup_bridge.c` - needed because
`codec_internal.h` cannot be `#include`d from C++ in this project: it
transitively pulls in `backends.h`, whose own `vorbis_func_residue` struct
uses `class` as an ordinary field name, legal C, a reserved word in C++) and
called `vorbis_book_init_decode()` per codebook POSITION directly, using
libvorbis's OWN already-parsed `static_codebook` for that position (not this
project's transcode struct). Also independently re-derived, position by
position, which LIBRARY SLOT each position resolves to, by duplicating (not
modifying) `rebuildVorbisSetupHeader()`'s own bare-10-bit-index outer-loop
read.

```
442/442 codebookCount=32 records: fail at position 1, library slot 160, and ONLY there
 46/46  codebookCount=34 records: fail at position 1, library slot 160, and ONLY there
  4/4   codebookCount=35 records (passing): zero failing positions
  1/1   codebookCount=42 record  (passing): zero failing positions
```

Zero exceptions in either direction. In this corpus, slot 160 alone explains
100% of the 488/493 split - not "one of 7 possible slots depending on the
record," literally always this one slot, at always the same list position.

**(3) Slot 160 hand bit-traced to certainty.** Its real library body is 4
bytes: `41 00 0c 04` (hex), `blobOffset=18862`, `byteLen=4` - matching
`spec-audio-codebook-library.md`'s own CSV row (`160,0xe823e6,0x12833e6,
18862,4,1,4,1,0,,`) exactly. Reading LSb-first per byte (oggpack's own bit
order), concatenated bit stream:
`1,0,0,0,0,0,1,0, 0,0,0,0,0,0,0,0, 0,0,1,1,0,0,0,0, 0,0,1,0,0,0,0,0`.

| field | width | bits (0-indexed) | value |
|---|---|---|---|
| dim | 4 | [0:4) = 1,0,0,0 | 1 |
| entries | 14 | [4:18) | 4 |
| ordered | 1 | [18:19) | 1 |
| length-field-width (raw) | 3 | [19:22) = 1,0,0 | 1 (width=2) |
| seed length (ordered branch, +1 biased) | 2 | [22:24) = 0,0 | 0 (+1=1) |
| run count (`ilog(entries-0)=ilog(4)=3`) | 3 | [24:27) = 0,0,1 | 4 |
| has-lookup flag | 1 | [27:28) = 0 | 0 (maptype=0) |

28 bits consumed of 32 (4 leftover bits - ordinary byte-alignment slack,
same bar every other pass on this branch uses). Every field reads
unambiguously; there is no alternate parse. The run count (4) consumes all 4
entries at `curLen=1`, giving `lengths=[1,1,1,1]`.

**Four codewords of length 1 is mathematically impossible** - only 2
distinct 1-bit codewords exist (`0`, `1`). Kraft sum = `4 * 2^-1 = 2.0`,
exactly double the legal maximum of `1.0`. This is not a rounding/edge case;
it is a hard, unambiguous violation of the Vorbis I codebook-validity rule
(confirmed directly against the vendored `_make_words()` source, not
inferred).

**Slot 161 is byte-IDENTICAL to slot 160** (same 4 raw bytes, same parse,
same failure) - worth flagging on its own as a specific, checkable
duplication.

**The pattern in slots 16/160/161/183/200/317 is exact, not approximate**:
all 6 sum to Kraft = `2.0` at full double precision (verified independently
in Python at full precision, not just `std::cout`'s default 6-digit
rounding), and shifting every one of their lengths by exactly `+1` makes
every one of those 6 slots sum to EXACTLY `1.0` - a perfectly complete, valid
Huffman tree. **Slot 598 does NOT fit this pattern** - its Kraft sum is
`197.909667969`, a qualitatively different, much larger overpopulation
(entries=3276, the single largest codebook in the whole library, sitting
immediately before the library's own NULL terminator at slot 599).

## Step 3 - diff against the public Wwise-to-Vorbis conversion approach

The OUTER mechanism (a per-codebook bare index into a shared, external
codebook-body library, rather than inline per-codebook fields) already
matches the publicly-documented `WWVORBIS_V53`/"external codebooks" shape -
established and cited in an earlier pass on this branch (`vgmstream`'s own
`src/coding/vorbis_custom_utils_wwise.c`, read via WebFetch for format
understanding only, per this project's `agrees-with-public-format`
convention; not re-fetched this pass since nothing about THIS finding
changes that established field-width/order picture). This pass's own slot-160
hand trace (Step 2.3) independently confirms the per-codebook BITSTREAM
GRAMMAR for this exact slot reads unambiguously under that already-pinned
grammar - no field-width or ordering divergence found here.

**The real divergence is not a format-reading convention at all - it is 7
specific slots' worth of raw DATA CONTENT inside this project's own locally
compiled-in copy of the shared table** (`kWwiseCodebookLibraryBlob` /
`src/wwise_codebook_library_data.cpp`, mechanically generated from
`spec-audio-codebook-library.md`). A "public-format" diff does not apply to
a static data table the same way it applies to a bitstream grammar; the
closest analog is: whatever copy of this external-codebooks table the public
converters use for this exact game/version, does ITS slot 160 contain the
same `41 00 0c 04`? That cannot be answered from this project's own clean
sources (own real-data measurement + public SOURCE for conceptual grammar
understanding only, never the public tools' own embedded data tables, which
would cross into reading data this project has no license/need to copy).

## Discrepancy with spec-audio-codebook-library.md's own 2026-10-09 note - RELAYED, not resolved

`spec-audio-codebook-library.md`'s own Provenance section carries a
**2026-10-09** "Re-validated" note: *"this library is ruled OUT as the
cause - every referenced index in-range, every referenced body byte-exact
and Kraft-complete... Full finding: spec-audio-runtime.md §11.11.4."*
§11.11.4's own matching entry states it reproduced "Team B's exact 488-vs-5,
32/34-vs-35/42 split" but names the failing call as
`vorbis_synthesis_headerin`.

This project's own branch (confirmed before this pass, and reconfirmed by
this pass's own full population run) has `vorbis_synthesis_headerin`
ACCEPTING all 493/493 records (0 `OV_EBADHEADER`) - that function's own
`_vorbis_unpack_books` (`info.c`) never calls
`vorbis_book_init_decode`/`_make_words` at all; the Kraft-inequality check
this pass found is strictly inside the LATER, separate
`vorbis_synthesis_init` call. Whether the cited note tested a genuinely
different check (no real conflict - two validators answering two different
questions) or the same one under a mislabeled call name (a direct
disagreement on the same bytes) is **not resolved here** - Team B cannot
disassemble to adjudicate it. RELAYED to Team A as a spec-sync question
(not decided unilaterally): did that validation pass ever call the real
`vorbis_book_init_decode()` (or faithfully reimplement `_make_words`'s exact
Kraft-inequality logic) on library slots 16/160/161/183/200/317/598 in
isolation, and if so what dim/entries/lengths did it compute for slot 160?
This pass's own tool (`wwise_vorbis_codebook_kraft_probe sweep`) is fully
reproducible, and the slot-160 hand trace above is laid out bit-by-bit for
an independent re-check.

## Why this is NOT fixed this pass

The "+1 to every length" pattern (Step 2.3) is an exact, mathematically
clean clue for 6 of the 7 bad slots, but this project has no clean-room way
to confirm it is the TRUE correct data - vs., for example, a wrong
entries/dim count, or a mistranscribed length-field-width specific to just
these slots - without crossing into disassembly, which Team B does not do.
Patching `lengths` to force a passing Kraft check would be exactly the kind
of unconfirmed, non-authentic fix this project's standing "no invented
fixes" discipline forbids, especially this close to real shipped audio
content. Slot 598's failure does not even fit that same clean pattern, so a
single uniform "patch" would be two different guesses, not one confirmed
fix. Reported precisely instead, per this task's own explicit instruction.

## Artifacts

- `tools/wwise_vorbis_codebook_kraft_probe.cpp` (+ CMake target, new): two
  modes - `sweep` (standalone 599-slot Kraft check) and `record` (per-record,
  per-position isolation + raw-byte dump for independent oracle testing) -
  plus a `dumpslot` mode used for the Step 2.3 hand trace.
- `tools/wwise_vorbis_codec_setup_bridge.c` (new, compiled as C): the one
  small reach-in to `vorbis_info::codec_setup`'s real internal layout
  (`codec_internal.h`), isolated to its own C translation unit because that
  header cannot be included from C++ in this project (see its own header
  comment).
- `include/sr3audioruntime/wwise_setup_rebuild.h` /
  `src/wwise_setup_rebuild.cpp`: `CodebookParseInfo::lengths` added - pure,
  additive diagnostic data exposure (the resolved per-entry length list),
  no behavior change to `rebuildVorbisSetupHeader()`'s own output. Dated
  2026-10-09 UPDATE note appended to `wwise_setup_rebuild.h`'s own header
  comment with the full finding (this file's own narrative, condensed).
- No production decode-path behavior changed. Full Release build (all
  existing targets) and full `ctest` re-run clean (see this task's own
  commit message for the exact pass count at commit time).
