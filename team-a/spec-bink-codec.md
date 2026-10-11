# Saints Row: The Third — Bink 1 Container and Codec Internals (`binkw32.dll`)

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** Bink Video, part **(b)** of the user's own two-part Bink scope (quoted in
`spec-bink-boundary.md`'s own preamble): *"(b) the Bink 1 container plus video/audio codec as
implemented in that DLL."* Part (a) — the game↔Bink boundary contract — is `spec-bink-boundary.md`,
already DONE; this file is its sibling, covering `binkw32.dll`'s own internals rather than the game's
call surface into it.
**Scope:** `binkw32.dll` only, this DLL's own PE address space (preferred image base `0x10000000`,
read directly from its PE header). **Every address in this file is in that `0x10xxxxxx` space —
never to be confused with the main exe's disjoint `0x00xxxxxx` space** that `spec-bink-boundary.md`
and every other spec in this project cites. Where this file cites an address in the main exe's own
space (none currently), it will say so explicitly.
**Method:** fresh, purpose-built `pefile` + `capstone` (x86, 32-bit) static disassembly across three
coordinated units — BK-U0 (container/API/dispatch, the 359-function remainder of the `BINK` section
outside the two codec cores), BK-U1 (video decode core, `0x10017470`–`0x1001ce50`), BK-U2 (audio
decode core, `0x1001e1d0`–`0x100260d4`) — each built on and cross-checking `review/scoping-bink-codec.md`'s
own whole-DLL structural map (766 functions/44,531 instructions/139,264 bytes total; the `BINK`
section split confirmed, independently, by two of the three units' own fresh re-census: 633
functions in `BINK`, with the BK-U0/video-core/audio-core split reproducing **exactly** at
359/170/104 functions). **No Ghidra was used by any of the three units.** No leaked
Volition/RAD/Bink/Havok source of any kind was read or used. **FFmpeg's own Bink video/audio decoder
was not read, consulted, or used anywhere in any of the three units** — it remains reserved, per the
user's own standing instruction, as a future output-only comparison oracle (run both decoders on the
same sample, diff the output) once this project has its own independently-derived spec, never as a
source for anything in this file.
**Confidence key:** **CONFIRMED** (read/measured directly); **HIGH** (strong structural evidence,
not independently cross-checked a second way); **HYPOTHESIS** (plausible, explicitly flagged as
unchecked).
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear
anywhere below — every function is cited by its plain hex address (this binary's own, read directly);
no Ghidra auto-names or `FUN_`/`DAT_`/`iVar`-style tokens anywhere. Quoted ASCII strings (the
container's own error messages, §1.6) are literal `BINKCONS`-section content, quoted verbatim as
shipped data, the same convention `spec-bink-boundary.md` already uses for its own heap-tag strings.

---

## 1. The container parser

### 1.1 `BinkOpen`/`BinkOpenWithOptions` are both trampolines to one shared worker

`_BinkOpen@8` (`0x10010690`) loads a small set of global defaults and tail-calls a single shared
worker, `0x1000f310`; `_BinkOpenWithOptions@12` (`0x10010730`) calls the identical worker directly.
**All header-parsing, validation, and handle-construction logic lives in this one function.**
**[CONFIRMED — disassembly, both export bodies.]**

### 1.2 The fixed 44-byte header: layout and validation

Inside `0x1000f310`, after setting up an IO-provider shim (erroring with the literal string **"No
file IO provider installed."** if the caller supplied none, and **"Error opening file."** if the
IO object's own open callback fails), the worker issues exactly one **indirect call through the
caller-supplied IO object's own read callback** (not a hardcoded file-handle read) requesting
exactly **44 (`0x2c`) bytes** into a local buffer — the fixed Bink container header, 11 little-endian
dwords:

| Field | Offset | Validation (CONFIRMED by disassembly) | Role |
|---|---|---|---|
| D0 | 0x00 | must equal `BIKf`/`BIKg`/`BIKh`/`BIKi` (ASCII, little-endian dwords `0x664b4942`/`0x674b4942`/`0x684b4942`/`0x694b4942`); any other value — **including the older `BIKb`/`BIKd`, explicitly NOT accepted by this binary's comparison chain** — errors **"Not a Bink file."** | Signature (CONFIRMED) |
| D1 | 0x04 | must be `>= (D10+1) * D4 * 4`; D3 must be `<= D1` | Total-size-shaped bound (HIGH) |
| D2 | 0x08 | must be nonzero, else errors **"The file doesn't contain any compressed frames yet."** | NumFrames (CONFIRMED by the error text) |
| D3 | 0x0c | must be `<= D1` | size-shaped (HIGH) |
| D4 | 0x10 | must be `>= D2` and `<= 1,000,000` | bounded count (structural only) |
| D5 | 0x14 | must be `<= 32767`; 16-pixel macroblock rounding (`(D5+0xf)>>4`, rounded up to a multiple of 16) applied immediately after | **Width** (CONFIRMED by the macroblock rounding and its direct copy into the handle) |
| D6 | 0x18 | must be `<= 32767`; same macroblock-row math used for the total macroblock count | **Height** (CONFIRMED, same evidence) |
| D7 | 0x1c | must be nonzero | **FrameRate numerator** (CONFIRMED by the rounded-FPS division, below) |
| D8 | 0x20 | must be nonzero; divisor in `round((D7 + D8/2) / D8)` | **FrameRate denominator** (CONFIRMED, same division) |
| D9 | 0x24 | raw value copied into the handle; bits `0x20000`/`0x100000` folded into the handle's own internal flags dword | attributes/flags (HIGH) |
| D10 | 0x28 | must be `<= 256`; multiplies into D1's lower bound | small bounded count (structural only — plausibly a track count, not independently confirmed by an error string) |

A separate, later error path — **"This file has bad frame size data."** — fires from a helper call
(`0x1000e560`) ~~not opened in depth~~ **[OPENED 2026-10-08, fully CONFIRMED — see §1.5a. It is not
"table-like structures sized from D2 and the rounded integer FPS" being built; it's a one-time
whole-frame-table monotonicity validation + worst-case 1-second buffer-size scan, run once at
`BinkOpen` time over the already-built frame-offset table (§1.6). `−1` return is the direct, sole
trigger of this exact error string.]**; **"Error reading Bink header."** is the outer IO-open-callback
failure message, distinct from the signature/field-validation failures above.

### 1.3 Per-OPEN scale-mode selection (distinct from the per-file revision selector, §1.4)

A 3-bit field at bits 28–30 of a resolved internal flags dword — combining the caller's own
`BinkOpen` `flags` argument with the header's own D9 field (D9's own bits 28–30 are used only when
the caller passes the literal sentinel `0x70000000`, "defer to the file") — selects, via a literal
`lea reg,[reg+reg]` doubling in each "doubled" case:

```
0b000                → no change
0b001 or 0b010        → Height doubled
0b011                 → Width doubled
0b100 or 0b101        → both Width and Height doubled
0b110 or 0b111        → no change
```

This is a **per-`BinkOpen`-call** (not per-frame) scaled-output-size selection, changing what the
handle reports as Width/Height to every later consumer. **[CONFIRMED — disassembly.]**

### 1.4 Per-file bitstream-revision selection, from the signature's own 4th byte (CONFIRMED)

Immediately after scale-mode resolution, the worker re-reads D0 a second time and runs a second
comparison against the same three FourCCs, setting two bits of the resolved flags dword:

```
signature == 'BIKh'          → sets bit 0x8000 only
signature == 'BIKf'/'BIKg'   → sets both 0x8000 and 0x10000
signature == 'BIKi'          → neither bit forced
```

This is the real per-file bitstream-revision/compression-level selector: **the container
signature's own 4th byte**, resolved once at `BinkOpen` time into two flag bits carried in the
handle — not traced into the video core's own consumption of these bits (open item).

### 1.5 Confirmed handle-struct fields (construction AND independent consumers agree)

The handle is a single 1,264-byte (`0x4f0`) allocation, built field-by-field in a stack-resident
staging area then copied in one `rep movsd` (confirmed exact: `0x13c` dwords = `0x4f0` bytes).
Fields below are cross-checked from construction AND from `BinkGetKeyFrame`/`BinkGoto`/
`BinkNextFrame` independently reading the same offsets:

| Offset | Field |
|---|---|
| `+0x00`/`+0x04` | Width / Height (post-scale-doubling) |
| `+0x08` | total frame count exposed to the public API (`BinkGoto` clamps against it directly) |
| `+0x14` | NumFrames again, used in `BinkGoto`'s per-second-bucket division |
| `+0x18` | an FPS-shaped divisor used in the same division (HIGH: "frames per seek-bucket") |
| `+0xf0`/`+0xf4` | macroblock column count / total macroblock count (`cols × rows`) |
| `+0x100`/`+0x104` | "async-decode-busy" flags — `BinkGoto`/`BinkNextFrame` early-out unless both zero |
| `+0x108`/`+0x110` | additional `BinkNextFrame`-checked state (not pinned down further) |
| `+0x124` | "has more than one second of frames" gate, controls `BinkGoto`'s seek-bucket shortcut |
| `+0x154` | pointer to a per-(audio)-track decode-state array, **384-byte (`0x180`) stride** per track |
| `+0x158` | pointer to the frame-offset table (§1.6) |
| `+0x1dc` | "seeking in progress" flag — the DLL-internal mirror of `spec-bink-boundary.md` §2.1's wrapper-side seeking flag |
| `+0x450` | rounded integer FPS (`round((D7+D8/2)/D8)`, min 1), sizes three further per-"second" buckets at Open time |
| `+0x4bc`/`+0x4c0` | pending-action counters; a nonzero `+0x4bc` triggers an internal flush helper (`0x10010750`) before clearing |
| `+0xc8` | **new, 2026-10-08**: raw return value of `0x1000e560` (§1.5a) — the worst-case "bytes needed to buffer any one-second window" of the stream, `max_i(table[i+FPS]-table[i])`; also the default for `+0x458` unless overridden by `BinkOpenWithOptions`'s own options struct |
| `+0xcc` | **new, 2026-10-08**: the frame index at which that worst-case delta occurred (§1.5a) |
| `+0x4d4` | **new, 2026-10-08**: logical AND of the keyframe bit (§1.6) across every frame `0x1000e560` validated (§1.5a) — no consumer found elsewhere in `BINK`, OPEN |

### 1.5a `0x1000e560`: frame-table validate + worst-case 1-second buffer-size scan (CONFIRMED, closes the §1.2 "not opened in depth" flag)

Called exactly once in the whole DLL, from `BinkOpen`'s shared worker (`0x1000f310`) immediately before
`BinkOpen` can succeed — confirmed by an exhaustive call/jmp-literal scan across all 8 executable PE
sections plus a whole-file raw-byte scan ruling out any function-pointer-table staging. Signature is a
genuine hybrid: 4 caller-cleaned (cdecl-discipline) stack arguments **plus one hidden register input in
`EDI`**, set by the caller immediately before the call and never itself saved/restored by the callee —
`EDI = handle+0x158` (the frame-offset table, §1.6), stack args = `handle+8` (NumFrames), `handle+0x450`
(rounded FPS), `&handle+0xcc` (out), `&handle+0x4d4` (out); return value stored by the caller into
`handle+0xc8`.

Over the whole frame-offset table, split at index `max(0, NumFrames−FPS)` (the start of "the last
~one second" of the stream): walks every index checking `table[i+1] > table[i]` (unsigned) — the first
failure returns `EAX = 0xFFFFFFFF`, the direct and sole trigger of `BinkOpen`'s "This file has bad
frame size data." error (§1.2, §1.7) — while tracking `maxDelta = max_i(table[i+FPS]−table[i])` and the
index where it occurred (confined to the pre-split range only; the post-split tail continues the
monotonicity/keyframe checks but does not extend `maxDelta`), and AND-accumulating the keyframe bit
(§1.6) across every index visited. **Every successfully-opened `.bik` file in this game has therefore
already passed this exact monotonicity check.** On the degenerate edge case where `maxDelta` comes out
exactly zero, an extra `(table[NumFrames]-table[0])*FPS/NumFrames` quotient is computed and returned
instead — no consumer for this specific quotient was found (OPEN).

`handle+0xc8`/`handle+0xcc` are not purely internal: both feed the public, SDK-facing
`_BinkGetSummary@8` export (`0x10012500`, confirmed from the DLL's own export table), which
`spec-bink-boundary.md` already confirms the game calls exactly once, right after `BinkOpen` — copied
verbatim into that function's own output struct at `+0x74` (`handle+0xc8`) and `+0x78`
(`handle+0xcc`, incremented by 1 first). `handle+0x4d4` (`allKeyframes`) was not found read anywhere
else in the `BINK` section (OPEN — may be genuinely unused in this build, or read by a path this static
scan can't see). Whether this validate/scan ever re-runs on a seek (`BinkGoto`) rather than purely at
`BinkOpen` time was not checked (OPEN). Full disassembly, both epilogues, and the complete call-site
listing are in `scratchpad/bink_frameoffset_helper_0x1000e560_2026-10-08.md`.

### 1.6 The frame-offset table and its keyframe bit (CONFIRMED, cross-checked four independent ways)

`handle+0x158` points to an array of 32-bit entries, one per frame. `BinkGetKeyFrame@12`
(`0x100118e0`) implements four distinct walks over this array ("is frame N itself a keyframe";
nearest keyframe at-or-before N; nearest at-or-after N; a stricter-lower-bound backward variant),
**every one of which tests bit 0 of the relevant 32-bit entry as the keyframe flag**, via the
identical `and dl,1`/`test byte ptr [...],1` idiom. The real byte-offset value is presumably the
same dword with bit 0 masked off — structurally obvious from the flag convention, but no masked
dereference was directly observed this pass (HIGH, not CONFIRMED).

**On-disk layout and real frame-blob boundaries, CONFIRMED 2026-10-08 (extends this section to the file
format, not just the in-memory table `handle+0x158` is loaded from):** for files with `D10=0` (no audio
tracks — `main_menu_bg.bik`, `01_crawl.bik`, both confirmed), the `(NumFrames+1)`-entry frame-offset
table sits literally at file byte offset 44 (immediately after the fixed header), file-absolute, and
**frame 0's own blob begins exactly where the table ends, byte for byte, no gap** —
`table_end = 44 + (NumFrames+1)*4` matches the table's own first entry exactly for both test files.
Frame N's raw blob is therefore `file_bytes[offs[N] : offs[N+1]]`. Files with `D10=1`+ (one or more
audio tracks, e.g. `logo_thq_and_voli_hd.bik`) insert extra per-track header bytes before the table,
breaking this exact formula — not chased further since the two `D10=0` files already suffice for a
pure-video trace. `main_menu_bg.bik` frame 0 = file bytes `0x290c`-`0x35a94` (209,288 bytes, confirmed a
keyframe) is on record as a strong real-sample candidate for future worked-example attempts, since a
keyframe structurally excludes every motion-compensated/previous-frame-copy case (0/2/4/7/9), leaving
only 1(sub)/3/5/6/8. Full detail: `scratchpad/bink_video_worked_example_block_2026-10-08-v2.md` §1.

**`D10>=1` file-level prefix size, and frame-0 interior layout, both CLOSED 2026-10-09 — confirmed two
independent ways on two independent real samples (structural byte-math AND live execution of the real,
unmodified decoder).** The per-track header before the frame-offset table is exactly **12 bytes (three
4-byte fields) per audio track** — confirmed on `logo_thq_and_voli_hd.bik` (D10=1, 1280x720) and
`logo_amd_hd.bik` (D10=1, 1920x1080) by the frame-offset table's own existing no-gap self-consistency
check (`table[0] == table_start + table_len`), which holds exactly if and only if `12*D10` bytes are
assumed; 0/4/8 bytes per track all fail it. The 12-byte block's own three dwords are byte-identical
across both samples despite differing width/height/duration (`00 3a 02 00  80 bb 00 70  00 00 00 00`),
consistent with fixed per-track encoder parameters rather than size-dependent fields — the second
dword's low 16 bits (`0xbb80` = 48000) matches a 48 kHz sample rate; the rest is OPEN, not required by
this question.

**Frame N's own blob, for `D10>=1`, does NOT begin with the video span field.** It begins with, per
audio track in order, a plain 4-byte little-endian chunk-size field followed by that many bytes of
audio payload; the video chunk — byte-for-byte the same shape already confirmed for `D10=0` files
(leading 4-byte Y-plane span field, etc.) — begins immediately after the last audio chunk, no gap, no
padding. Worked example, `logo_thq_and_voli_hd.bik` frame 0 (keyframe, one audio track): blob starts at
absolute file offset `0x5dc` (29,336 bytes); the leading dword there is the audio chunk size, `28952`
(`0x7118`); the video chunk begins at blob offset `4+28952 = 0x711c` (absolute file offset `0x76f8`),
whose own leading dword is Y's span field, `220` (`0xdc`) — the same convention `D10=0` files use. The
16 bytes at the Y sub-chunk's own start, absolute file offset `0x76fc`, read directly from the real
shipped file: `57 f1 7e 65 ef 10 11 00 00 00 00 00 00 00 11 00` (corrected 2026-10-09 — the original
transcription dropped one byte from the zero run and appended a trailing `e0` with no basis in the
real file; independently re-read three ways this pass — `Seek`+`Read`, `ReadAllBytes`+array-slice,
both against the file at `D:\Project Crreish\Saints Row 3 CRREISH\videos\logo_thq_and_voli_hd.bik` —
all three agree on the corrected bytes above; every surrounding checkpoint this paragraph cites
(`FileSize`/`NumFrames`/`Width`/`Height` header fields, the audio chunk size `28952`, the Y-span
dword `220`) was independently re-verified byte-for-byte against the same real file and matches
exactly, confirming this is the same file and offset, not a different sample). Cross-sample confirmation on
`logo_amd_hd.bik` (1920x1080): blob at `0x314` (27,420 bytes), audio chunk size `25600` (`0x6400`),
video chunk at absolute `0x6718` with Y-span `448` (`0x1c0`) — same pattern, independently.

**Live execution against the real decoder settles it decisively, not just structurally.** Instrumenting
the entropy pre-stage's own LEN-vs-CAP poison check (§5.3g) on `logo_thq_and_voli_hd.bik`'s first
descriptor-A refill: the naive `D10=0`-style reading (no audio skip) gives `LEN=673` against `CAP=672`
— poison, triggering the stall path on the very first real bitstream read of the frame; the
audio-prefix-corrected reading gives `LEN=560` against the same `CAP=672` — valid, and the decode
proceeds cleanly through all 3 planes. On the second sample, the naive reading produces 6 poison events
including wild overshoots (e.g. `LEN=833` vs `CAP=640`), while the corrected reading produces only 3,
all the mild, already-documented `LEN=0` quirk (not a desync signal) — the same qualitative signature
`D10=0`'s own baseline already shows. **This is an exact, mechanically-identified confirmation of the
desync this item's own task brief described empirically** — not a vague crash, but the documented
poison branch firing on the very first row, off by exactly one byte of capacity. ChromaA/chromaB's own
further internal chaining past Y remains equally OPEN for `D10=0` and `D10>=1` alike (out of scope
here, unchanged from the existing `D10=0` finding). Full detail:
`scratchpad/bink_d10_and_bit1757_trace_2026-10-09.md` Part 1.

### 1.7 Error strings, quoted verbatim as shipped `BINKCONS`-section data

```
"No file IO provider installed."                          (0x1003751c)
"Error opening file."                                      (0x1003753c)
"Not a Bink file."                                          (0x10037550)
"The file doesn't contain any compressed frames yet."       (0x10037564)
"Error reading Bink header."                                (0x100375b8)
"This file has bad frame size data."                        (0x100375d4)
"Bink global IO mutex"                                       (0x10037504)
```

The last string is the literal name tag passed to a critical-section constructor inside the open
worker — **Bink's IO layer serializes through one shared, named mutex** across however many `Bink*`
handles are open in the process, consistent with `RADCODE`'s own threading-shim role (§4) being the
natural source of the underlying primitive.

---

## 2. Real dispatch: `BinkDoFrame` into the two decode cores

### 2.1 `BinkDoFrame` is a 2-instruction trampoline into `BinkDoFramePlane`

`_BinkDoFrame@4` (`0x100116f0`): `push 0x303; push [handle]; call 0x10011280; ret 4`. There is **no
separate audio-decode export** — `BinkDoFrame` is simply a fixed-argument call into
`BinkDoFramePlane`'s own implementation (`0x10011280`). **[CONFIRMED — disassembly of the trampoline's
own two real instructions.]**

### 2.2 The video path: one unconditional direct call into the video core

Inside `0x10011280`, after a timing snapshot, a debug-hook call, and a mute/unmute-around-
discontinuity cleanup call (none decode-relevant), the function makes **one unconditional direct
call to `0x1001cb00`** — squarely inside the video decode core (§5). This call's own 51-function
closure contains zero addresses inside the audio core. **The video path never touches audio-core
code.**

### 2.3 The audio path: a generic per-track chunk pipeline, not a second direct call

Separately, `0x10011280` runs a per-track loop:

```
0x10011280 (DoFramePlane body)
  -> 0x100111f0   scans a small per-track "ready" array for an active audio-track slot
       -> 0x10010fe0   per-track ring-buffer chunk fetch (wraparound-aware; indexes handle+0x154
                        by track * 0x180)
            -> 0x10018600   per-track gain-application dispatcher (§6.4)
                 -> 0x1001e320   fixed-gain decode entry [audio core]
                 -> 0x1001e810 / 0x10024150   ramped-gain decode entry [audio core]
```

**39 distinct audio-core addresses are reachable, every one through this exact chain** — there is no
second, independent direct call into the audio core anywhere in `BinkDoFramePlane`'s own body.
**[CONFIRMED — exhaustive BFS from `_BinkDoFrame@4`.]**

### 2.4 `0x10018600` is an audio dispatcher, not video geometry code (correction to the scoping note)

The scoping note tentatively attributed `0x10018600` (and neighbours `0x10018800`/`0x10018860`/
`0x10018ba0`) to "buffer/geometry size-and-alignment helpers," reasoning from its callers
(`RegisterFrameBuffers`/`CopyToBufferRect`/`OpenTrack`/`GetTrackData`) alone. Its own body (opened in
full) is a two-way branch on one bit of its 3rd argument, both branches terminating inside the audio
core (§6.4) — there is no video-geometry code inside it at all. Its address merely sits inside the
byte range the scoping note's own heuristic labelled "Block A" — the exact "address-proximity ≠
logical ownership" pitfall that note's own methodology section already flagged for a different
function and warned a future pass to re-check for.

### 2.5 The async pump's own near-duplicate decode body is dead code (closing a loose end)

A second, structurally near-identical function, `0x100114e0` (same shape as `0x10011280`: timing
snapshot, debug hook, `0x100111f0`, call into `0x1001cb00`, but with one extra branch testing a
2-bit async sub-selector), sits immediately after `0x10011280`'s own `int3` padding. Its one caller
is inside the `_BinkDoFrameAsync@12`/`BinkWaitStopAsyncThread` family, which manipulates the exact
async-thread-pool signalling globals `review/scoping-bink-codec.md` §4.2 already identified. Since
`spec-bink-boundary.md` §4.1 already CONFIRMED (full-binary write-site scan) that the frame-pump
async-selector byte `0x02989968` is never written in the shipped game, **this entire second body —
and its own call into the video core — is dead code in the retail build**, consistent with
everything else the async family does.

---

## 3. The CPU-feature-selector record — settled, in both directions at once

### 3.1 The record IS read back and called through (contra the scoping note's literal finding)

The scoping note found five CPU-feature-selection stubs (one per `BINKY12`/`BINKY16`/`BINKP8`/
`BINK16`/`BINK32` pixel-format family) writing a chosen implementation address into a small global
record, `0x10030a9c`–`0x10030ab8`, and reported finding **no** code anywhere that reads a registered
pointer back out and calls through it. A whole-section literal-address search this pass finds **ten
`call dword ptr [0x10030aXX]`** indirect-call instructions, inside exactly three functions
(`0x1001d170`, `0x1001da56`, `0x1001df06`), plus two structurally identical sibling helpers
(`0x1001d460`, `0x1001d4a0`). **All five sit inside `0x1001ce50`–`0x1001e1d0`** — the small gap
region between the video and audio cores that the scoping note explicitly flagged as "identified but
not opened in detail." The scoping note's literal "no code reads it back" claim does not hold — this
pass found exactly the read-back it predicted might exist.

### 3.2 But reachability is exhaustively confirmed to be confined to the unused `BinkCopyToBuffer` family

None of the three containing functions appear anywhere in `_BinkDoFrame@4`'s own 120-function
closure (12-hop BFS). Walking **upward** (ancestors, 8 call-levels) from all three reaches exactly 32
functions total in the whole binary; the **only two exported functions among all 32 ancestors are
`_BinkCopyToBuffer@28` (`0x10010890`) and `_BinkCopyToBufferRect@44` (`0x100108d0`)** — the exact two
pixel-format-conversion exports `spec-bink-boundary.md` §1.3 already CONFIRMED are **absent from the
shipped game's import table**. **The registration record's call-through machinery exists and is
real, but is wired exclusively into a call tree already proven, independently, to be unreachable by
the game's own import surface.** `BinkDoFrame`'s real decode path has zero static connection to this
record. **[CONFIRMED — exhaustive call-graph proof, settling the scoping note's own flagged open
question in both directions at once.]**

### 3.3 `BinkControlPlatformFeatures` is unrelated machinery, same neighbourhood, different global

`_BinkControlPlatformFeatures@8` (`0x1001ce50`) reads a cached CPUID-derived feature-bits dword from
`0x10036af8` (computed once, cached thereafter), folds it with the caller's own (set-mask,
clear-mask) argument pair, and stores the result into a **different** global, `0x10036afc` — a
generic feature-flag accumulator, consistent with the public Bink SDK's documented purpose for this
call. It has no code-level connection to the `0x10030a9c` record (its own 3-function closure does
not reach any of §3.1's call-through sites) — the two sit in the same small address neighbourhood
but are structurally unrelated.

**Honest loose end:** the same literal-address search also finds the record's `0x10030a9c`/
`0x10030a98` slots overwritten elsewhere in the `BINK` section with three generic
`BinkCopyToBuffer`-internal helper addresses (not one of the five pixel-format kernels), gated by a
runtime check on one of the record's own slots — the record is reused for more than one purpose
across the unreachable call tree; the exact selection condition was not fully traced (open item).
This does not change §3.2's reachability verdict.

---

## 4. `RADCODE`'s integration point

A full census of every direct call/jmp edge targeting `RADCODE`'s address range
(`0x10027000`–`0x100279ff`) finds 31 total edges:

| Containing-function region | Call sites |
|---|---|
| BK-U0 (container/API/dispatch) | 17, across 11 distinct functions |
| Video decode core | 4, all inside the already-identified async-thread-signalling cluster at the core's own front edge — not real video-codec work |
| `RADCODE` itself (internal chaining) | 10 |
| Audio decode core | 0 |

The 11 BK-U0 callers cluster around `_BinkClose@4`'s own error/cleanup helpers, sound-system/
sound-track configuration code, `RegisterFrameBuffers`-area code, and `_BinkGetPlatformInfo@8`'s own
body. **`RADCODE` is never called from either real codec core, and its only video-range callers are
signalling primitives already known to be unrelated to actual video math** — confirming
`review/scoping-bink-codec.md` §5's own characterization of `RADCODE` as a generic Win32-feature/
threading shim (dynamically resolving `TryEnterCriticalSection` etc., honoring env var
`RAD_NO_SET_THREAD_NAMES`), never used inside either codec's own bitstream/transform math.

---

## 5. The video decode core (`0x10017470`–`0x1001ce50`)

### 5.1 Honesty ledger: what was fully read vs. structurally mapped

**Fully read, full control flow traced by hand (23 functions):** `0x1001cb00`, `0x1001a530` (setup
portion), `0x1001a917` (setup + dispatch-loop top, type-0 case inline), `0x1001ace5`, `0x1001b3b5`,
`0x1001be76`, `0x1001be70`, `0x1001acba`, `0x10018c20`, `0x100192dd`, the SSE2 transform trio
(`0x10017f80`/`e20`/`dc0`), the MMX transform kernel (`0x10017e90`), the dispatcher pair
(`0x1001878a`/`0x100186fa`), `0x10018600` (note: this address is reused in audio-core territory per
BK-U0's own disambiguation — the video-side `0x10018600` cited here by BK-U1 is a distinct function
from the identically-numbered mnemonic in some scratch notes; both units independently confirmed
their own addresses directly, no conflict in the actual binary), `0x1001a490`, the small 5-way
dispatch (`0x100189cb`/`0x10018a34`), `0x10017470` (`_BinkOpenXAudio2@4`, confirmed irrelevant to
video — an audio-backend registration stub), plus the two indirectly-reached VLC bit-window refill
routines `0x100195b0`/`0x10019850` (found only by manual byte-tracing, invisible to any direct
call/jmp-target census).

**Structurally mapped only (~145 functions):** the three giant pure-integer bitstream engines
(`0x1001b5a3` 583 instructions, `0x1001c3d7` 456, `0x1001af5e` 276 — confirmed zero SIMD/FPU content,
confirmed external call targets, exact bit-level entropy math not traced), seven per-context setup
helpers, the inferred x87 fallback tier, roughly a dozen unread jump-table case bodies.

### 5.2 The real call chain from `BinkDoFramePlane` down (closing the scoping note's top open item)

It is **not** `0x1001cb00` doing the actual block-by-block decode math — `0x1001cb00` (848 bytes,
306 instructions, read in full) is an **orchestrator**:

1. Stack-cookie setup (standard security idiom; `0x10033000` is a cookie-randomization seed, not any
   kind of buffer pointer).
2. **Nine `_alloca` scratch-buffer allocations**, sizes read from nine consecutive dword fields of a
   struct — the SAME 9-field layout a geometry-sizing function, `0x1001a490`, computes once at
   `BinkRegisterFrameBuffers` time (§5.5).
3. **Double-buffer slot selection** (CONFIRMED, directly answers "which buffer does decode write
   into"): reads a buffer-set index, computes the *other* slot (`XOR 1`), and checks a flag field of
   that other slot's own record in a **`0x30`-byte-stride array — the exact stride
   `spec-bink-boundary.md` §5.2 already documented from the game side** for its own plane-pointer-
   record array — swapping to it if its flag is set. Textbook ping-pong double-buffering, operating
   on the **same caller-owned record array the game already registered**, not a Bink-internal
   buffer.
4. **Lazy CPU-feature-flag cache init**: on the first call only, caches a feature-detection result
   (with a high sentinel bit marking "already initialized") into two globals, `0x10036af8` and
   `0x10036afc` — read by the transform-dispatch functions (§5.4) to choose SSE2/MMX/x87.
5. **Calls the real per-row-range decode workhorse, `0x1001a530`, one to three times**, once per
   row-range needed this frame (consistent with Bink's known support for partial/dirty-rectangle
   updates — the exact semantics of the selecting flag bits were not resolved, open item).
6. MMX cleanup (`emms`, only if the cached flags say MMX was actually used).
7. Returns a decode-margin percentage (`(total_blocks − min_decoded_margin) × 100 / total_blocks`).

`0x1001a530` (entered via a genuine `call`, own stack frame, ~1,188 bytes of locals) is the **real
workhorse — but its own compiled body does not end at its reported byte span**. The many "candidate
starts" a call/jmp-target census finds across `0x1001a530`–`0x1001ca9e` (`0x1001a917`, `0x1001ace5`,
`0x1001b3b5`, `0x1001be76`, `0x1001be70`, `0x1001acba`, the three giant bitstream engines, etc.) are
**internal labels/case bodies of this one function**, sharing the identical `ebp`-relative frame —
not separate callees. This is confirmed by direct read of the shared frame on both sides of the
boundary, though the exact byte gap between `0x1001a650` and `0x1001a8b0` was not individually
walked instruction-by-instruction (open item).

**A quantitative methodological finding worth carrying forward:** of the video core's 170 raw
candidate-function-starts, only **35 (21%) are genuinely independent callable subroutines** (reached
by a real `call`, an external caller, or the block's own export); the other 135 (79%) are reached
only by an internal `jmp` — basic-block labels/switch-case continuations inside the
`0x1001a530`/`0x1001a917` mega-function, not independently meaningful units. **This is not a defect
in the call/jmp-target census method** — every one of the 135 is a real control-flow boundary,
exactly what this project's own `WALLS.md` recommends anchoring on — it is a caution about what
"function count" means for a jump-table-dispatch-heavy, highly optimized codec core specifically.

**A second, concrete methodological finding — a real, found (not hypothetical) blind spot in the
direct call/jmp-target census:** `0x1001a530` selects one of two bit-window-refill routines
(`0x100195b0`/`0x10019850`) by storing a literal address into a stack-local slot, later called
*indirectly* (`call dword ptr [ebp-0x234]`) from inside the per-row loop. Neither target address is
the direct target of any `call`/`jmp` instruction anywhere in the binary — found only by reading the
bytes around `0x1001a530` by hand, then confirmed bounded (for this unit specifically) by a
defensive whole-block byte rescan that found exactly this pair plus the jump-table case targets
below, and nothing else.

### 5.3 Per-block decode dispatch: a real, confirmed jump-table switch

Inside the shared per-row loop: a byte is read from the bitstream, range-checked (`0`–`9`), and
dispatched via `jmp dword ptr [edx*4 + 0x1001cab8]` — a genuine 10-way jump table. **Value 1's case
body reads a second byte, subtracts 3, range-checks (`0`–`6`), and dispatches via a *second* jump
table, `0x1001cae0` — which is exactly `0x1001cab8 + 0x28`: table 2's 7 entries are bit-identical to
table 1's own entries 10–16.** One physical table, read by two different index computations from two
different byte values — the real shape behind what a rough scan might first read as "17 cases": it
is 10 primary cases, one of which (value 1) carries a second, 7-valued sub-type field.

Case bodies read in full and confirmed:

| Primary | Sub | Role (CONFIRMED by reading the body) |
|---|---|---|
| 0 | — | **Block copy** — an unrolled 16-dword copy from a row-strided source to destination, zero entropy decode. Matches the public "skip = copy"/predicted-copy category. |
| 1 | 1 or 4 | **Mark skip flag, no pixel write** — writes a literal `1` into a per-macroblock flag array, advances the write position, falls through to the secondary dispatch or returns. |
| 1 | 3 | **Flat/DC fill** — reads one literal byte, broadcasts it, stores the replicated dword into ~40 consecutive locations spanning a whole macroblock. |
| 1 | 5 | **Two-literal pattern entry** — reads two literal bytes, each broadcast and stored, then tail-jumps into the largest bitstream engine (`0x1001b5a3`, 583 instructions) — structurally a two-colour/two-level PATTERN block type. **Full payload grammar CONFIRMED by live execution, 2026-10-09 — see the extended paragraph below.** |
| 4 | — | Reaches the second-largest bitstream engine (`0x1001c3d7`, 456 instructions) via a helper that also calls two audio-core-range addresses (§7). |

**TwoLiteralPattern (primary=1/secondary=5) payload grammar, CONFIRMED by live execution against a
real block, 2026-10-09 — closes the "structurally a two-colour/two-level PATTERN block type" row
above to a full byte-level grammar.** After the 2 literal bytes (already documented above) are
broadcast to 32-bit replication, `0x1001b5a3` consumes **exactly 8 raw control bytes**, each an
8-bit value split into **4 two-bit fields**; each field indexes the already-published
`0x1003c130`/`0x1003c200` blend-mask table pair (`TABLE1[idx] & literal2_broadcast` OR'd with
`TABLE2[idx] & literal1_broadcast`) and writes the resulting dword to **two** destination rows
(current row + one stride below) simultaneously — 4 fields × 4 pixels/field = 16 columns, 8 control
bytes × 2 rows/byte = 16 rows, producing a **fixed 16×16-pixel output block** from 10 total raw
bytes (2 literals + 8 control). The outer repeat count (2) and the 4-field-group inner unrolling are
**hardcoded immediates in the code**, not block-size-dependent. Traced against a real occurrence in
`main_menu_bg.bik` frame 0's Y-plane (literal1=`0x9b`, literal2=`0x9c`): all 64 real written dwords
(256 pixels) decompose exactly into the 4 table-predicted patterns with zero exceptions, and 4 of
the 8 reconstructed control bytes were independently cross-checked against directly-captured live
register values (exact 4-for-4 match). **This 16×16 output size does NOT match the `Width/8`
macroblock-column convention used elsewhere in this project's own harness conventions — it is
block-type-specific, not an instance of whatever smaller unit governs the general per-block
dispatch loop** (the game's own real transform/macroblock block size remains a separate, still-open
question, §8 item 5a). **Correction to the task that requested this trace:** the specific claim that
`logo_amd_hd.bik` frame 0 block (0,0) dispatches into secondary=5 does **not** hold under live
execution — that exact block dispatches into secondary=3 (Flat/DC-fill) instead, and a full-frame
live scan found zero secondary=5 occurrences anywhere in that frame's 3 planes (methodology
independently validated via a control test on `main_menu_bg.bik`, which correctly reproduces 183
real secondary=5 occurrences there). The grammar above was therefore confirmed against a real
secondary=5 occurrence found in `main_menu_bg.bik` instead, not the file originally named. **[CONFIRMED
— live execution (Unicorn, real unmodified `binkw32.dll`), one full worked example, cross-validated
multiple independent ways; HIGH CONFIDENCE — the block size/control-byte-count invariance across
all occurrences rests on the counts being hardcoded in a single shared code path, not on a second
live sample at a different size.]** Full detail:
`scratchpad/bink_twoliteralpattern_0x1001b5a3_2026-10-09.md` (Part A).

Several other case bodies were not individually read this pass (open item). Three functions dominate
the core's instruction budget with a clean, consistent fingerprint — very large, almost pure integer
bit-shift/mask/compare code, **zero SIMD, zero FPU**: `0x1001b5a3` (2,253 bytes/583 instructions),
`0x1001c3d7` (1,735/456, calls three audio-core-range addresses), `0x1001af5e` (1,111/276, calls one
audio-core-range address). Together these three account for 17.6% of the core's instructions; their
reachability is confirmed, their exact bit-level entropy-coding math (Huffman lookup, run-length
expansion) was **not** traced — a deliberate structural-over-bit-level priority, not an oversight.

### 5.3a The entropy coding itself: fixed-width + table-driven, NOT Huffman, for the three named giants (2026-10-08, closes former OPEN items 2/3/8)

**Bit-reading mechanism (CONFIRMED):** a shared `eax`(value)/`esi`(bits-left)/`edi`(source-ptr)
accumulator, refilled a full 32-bit dword at a time, LSB-first — but drawn at four different fixed
widths depending on context (1-bit, 3-bit, 4-bit, 7-bit), plus several byte-aligned (non-bit-packed)
reads for pattern-control bytes. Not one uniform scheme.

**`0x1001af5e` (real entry `0x1001ada6`):** run-length literal/replicate expansion, written through
a 4-bit-nibble-selected 64-byte scan-order permutation table, then immediately 2× pixel-doubled/
upsampled in the same engine — two distinct jobs inside one "giant," not previously separated at
this detail.

**`0x1001b5a3`:** a two-literal "PATTERN" block — raw control bytes (no bit-packing), 2-bit fields
select one of 4 fixed blend masks between two literal colors via a dumped complementary-bitmask
table pair (`0x1003c130`/`0x1003c200`).

**`0x1001c3d7` is itself only a 7-instruction DISPATCHER, not a 456-instruction engine** — the
456-instruction count belongs to six sibling case bodies (primary dispatch cases 4-9) sharing its
stack frame, individually traced: two motion-compensated-copy variants (signed dx/dy, bounds-checked,
matching Bink's public "MOTION" block shape), a DC-fill, a finer 4-bit/16-entry version of the same
PATTERN mechanism (table dumped: each entry is literally the 4 bits of the index broadcast one-per-byte),
and a plain block-copy variant. Three of these six cases make the confirmed direct calls into the
audio-core's own address range this document's §7 already notes (`0x100244c0`, `0x10024b50`,
`0x10024eb0`, `0x10025e00`) — sharpening that cross-block finding with exact per-case attribution.

**Static tables found and dumped in full:** a 16×64-byte scan-order permutation table
(`0x100378c0`); two blend-mask table pairs, at 2-bit (`0x1003c130`/`0x1003c200`) and 4-bit
(`0x1003c180`/`0x1003c1c0`) granularity; and a 16×256-byte table (`0x10037cc0`) whose consumption by
the audio dequantizer it feeds was left OPEN.

**Byte contents, transcribed 2026-10-08 (previously only described as "dumped," never transcribed
into the spec itself):**

*`0x100378c0` — 16×64-byte scan-order permutation table, selected by a 4-bit nibble
(`row = 0x100378c0 + nibble*64`) in giant1 (`0x1001ada6`, secondary=0) and primary case 3
(`0x1001bfcb`). Every row verified programmatically to be an exact permutation of 0-63:*

```
row  0: 00 08 10 18 20 28 30 38 39 31 29 21 19 11 09 01 02 0a 12 1a 22 2a 32 3a 3b 33 2b 23 1b 13 0b 03 04 0c 14 1c 24 2c 34 3c 3d 35 2d 25 1d 15 0d 05 06 0e 16 1e 26 2e 36 3e 3f 37 2f 27 1f 17 0f 07
row  1: 3b 3a 39 38 30 31 32 33 2b 2a 29 28 20 21 22 23 1b 1a 19 18 10 11 12 13 0b 0a 09 08 00 01 02 03 04 05 06 07 0f 0e 0d 0c 14 15 16 17 1f 1e 1d 1c 24 25 26 27 2f 2e 2d 2c 34 35 36 37 3f 3e 3d 3c
row  2: 19 11 12 1a 1b 13 0b 03 02 0a 09 01 00 08 10 18 20 28 30 38 39 31 29 2a 32 3a 3b 33 2b 23 22 21 1d 15 16 1e 1f 17 0f 07 06 0e 0d 05 04 0c 14 1c 24 2c 34 3c 3d 35 2d 2e 36 3e 3f 37 2f 27 26 25
row  3: 03 0b 02 0a 01 09 00 08 10 18 11 19 12 1a 13 1b 23 2b 22 2a 21 29 20 28 30 38 31 39 32 3a 33 3b 3c 34 3d 35 3e 36 3f 37 2f 27 2e 26 2d 25 2c 24 1c 14 1d 15 1e 16 1f 17 0f 07 0e 06 0d 05 0c 04
row  4: 18 19 10 11 08 09 00 01 02 03 0a 0b 12 13 1a 1b 1c 1d 14 15 0c 0d 04 05 06 07 0e 0f 16 17 1e 1f 27 26 2f 2e 37 36 3f 3e 3d 3c 35 34 2d 2c 25 24 23 22 2b 2a 33 32 3b 3a 39 38 31 30 29 28 21 20
row  5: 00 01 02 03 08 09 0a 0b 10 11 12 13 18 19 1a 1b 20 21 22 23 28 29 2a 2b 30 31 32 33 38 39 3a 3b 04 05 06 07 0c 0d 0e 0f 14 15 16 17 1c 1d 1e 1f 24 25 26 27 2c 2d 2e 2f 34 35 36 37 3c 3d 3e 3f
row  6: 06 07 0f 0e 0d 05 0c 04 03 0b 02 0a 09 01 00 08 10 18 11 19 12 1a 13 1b 14 1c 15 1d 16 1e 17 1f 27 2f 26 2e 25 2d 24 2c 23 2b 22 2a 21 29 20 28 31 30 38 39 3a 32 3b 33 3c 34 3d 35 36 37 3f 3e
row  7: 00 01 02 03 04 05 06 07 0f 0e 0d 0c 0b 0a 09 08 10 11 12 13 14 15 16 17 1f 1e 1d 1c 1b 1a 19 18 20 21 22 23 24 25 26 27 2f 2e 2d 2c 2b 2a 29 28 30 31 32 33 34 35 36 37 3f 3e 3d 3c 3b 3a 39 38
row  8: 00 08 09 01 02 03 0b 0a 12 13 1b 1a 19 11 10 18 20 28 29 21 22 23 2b 2a 32 31 30 38 39 3a 3b 33 34 3c 3d 3e 3f 37 36 35 2d 2c 24 25 26 2e 2f 27 1f 17 16 1e 1d 1c 14 15 0d 0c 04 05 06 0e 0f 07
row  9: 18 19 10 11 08 09 00 01 02 03 0a 0b 12 13 1a 1b 1c 1d 14 15 0c 0d 04 05 06 07 0e 0f 16 17 1e 1f 26 27 2e 2f 36 37 3e 3f 3c 3d 34 35 2c 2d 24 25 22 23 2a 2b 32 33 3a 3b 38 39 30 31 28 29 20 21
row 10: 00 08 01 09 02 0a 03 0b 13 1b 12 1a 11 19 10 18 20 28 21 29 22 2a 23 2b 33 3b 32 3a 31 39 30 38 3c 34 3d 35 3e 36 3f 37 2f 27 2e 26 2d 25 2c 24 1f 17 1e 16 1d 15 1c 14 0c 04 0d 05 0e 06 0f 07
row 11: 00 08 10 18 19 1a 1b 13 0b 03 02 01 09 11 12 0a 04 0c 14 1c 1d 1e 1f 17 0f 07 06 05 0d 15 16 0e 24 2c 34 3c 3d 3e 3f 37 2f 27 26 25 2d 35 36 2e 20 28 30 38 39 3a 3b 33 2b 23 22 21 29 31 32 2a
row 12: 00 08 09 01 02 03 0b 0a 13 1b 1a 12 11 10 18 19 21 20 28 29 2a 22 23 2b 33 3b 3a 32 31 39 38 30 34 3c 3d 35 36 3e 3f 37 2f 27 26 2e 2d 2c 24 25 1d 1c 14 15 16 1e 1f 17 0e 0f 07 06 05 0d 0c 04
row 13: 18 10 08 00 01 02 03 0b 13 1b 1a 19 11 0a 09 12 1c 14 0c 04 05 06 07 0f 17 1f 1e 1d 15 0e 0d 16 3c 34 2c 24 25 26 27 2f 37 3f 3e 3d 35 2e 2d 36 38 30 28 20 21 22 23 2b 33 3b 3a 39 31 2a 29 32
row 14: 00 08 09 01 02 0a 12 11 10 18 19 1a 1b 13 0b 03 07 06 0e 0f 17 16 15 0d 05 04 0c 14 1c 1d 1e 1f 3f 3e 36 37 2f 2e 2d 35 3d 3c 34 2c 24 25 26 27 38 30 31 39 3a 32 2a 29 28 20 21 22 23 2b 33 3b
row 15: 00 01 08 09 10 11 18 19 20 21 28 29 30 31 38 39 3a 3b 32 33 2a 2b 22 23 1a 1b 12 13 0a 0b 02 03 04 05 0c 0d 14 15 1c 1d 24 25 2c 2d 34 35 3c 3d 3e 3f 36 37 2e 2f 26 27 1e 1f 16 17 0e 0f 06 07
```

*`0x1003c130`/`0x1003c200` — 2-bit blend-mask pair (4 entries each), selected by
`idx = (control_byte >> shift) & 3` in giant2 (`0x1001b5a3`, secondary=5). `TABLE2[i] = ~TABLE1[i]`
exactly, every entry:*

| idx | `0x1003c130` (TABLE1) | `0x1003c200` (TABLE2) |
|---|---|---|
| 0 | `0x00000000` | `0xFFFFFFFF` |
| 1 | `0x0000FFFF` | `0xFFFF0000` |
| 2 | `0xFFFF0000` | `0x0000FFFF` |
| 3 | `0xFFFFFFFF` | `0x00000000` |

*`0x1003c180`/`0x1003c1c0` — 4-bit blend-mask pair (16 entries each), selected by a 4-bit nibble in
primary case 8 (`0x1001c608`). `TABLE2 = ~TABLE1` throughout; each `TABLE1[i]` is exactly the 4 bits
of `i` broadcast one-per-byte-lane (bit n → byte n, `0x00` if clear / `0xFF` if set):*

| i | `0x1003c180` (TABLE1) | `0x1003c1c0` (TABLE2) |
|---|---|---|
| 0 | `0x00000000` | `0xFFFFFFFF` |
| 1 | `0x000000FF` | `0xFFFFFF00` |
| 2 | `0x0000FF00` | `0xFFFF00FF` |
| 3 | `0x0000FFFF` | `0xFFFF0000` |
| 4 | `0x00FF0000` | `0xFF00FFFF` |
| 5 | `0x00FF00FF` | `0xFF00FF00` |
| 6 | `0x00FFFF00` | `0xFF0000FF` |
| 7 | `0x00FFFFFF` | `0xFF000000` |
| 8 | `0xFF000000` | `0x00FFFFFF` |
| 9 | `0xFF0000FF` | `0x00FFFF00` |
| 10 | `0xFF00FF00` | `0x00FF00FF` |
| 11 | `0xFF00FFFF` | `0x00FF0000` |
| 12 | `0xFFFF0000` | `0x0000FFFF` |
| 13 | `0xFFFF00FF` | `0x0000FF00` |
| 14 | `0xFFFFFF00` | `0x000000FF` |
| 15 | `0xFFFFFFFF` | `0x00000000` |

Full re-derivation/cross-check methodology: `scratchpad/bink_case1_sub6_and_tables_2026-10-08.md`.

**The "~15-function VLC/dequant family" (former OPEN item 8, `0x10025810`-`0x10025d65`) is CONFIRMED
to be ONE function, not 15** — the naive start address was mid-instruction; the real boundary is
`0x100258d0`, and a clean 428-instruction disassembly lands exactly on the next real function
(`0x10025e00`) with zero desyncs. It implements a genuine breadth-first, worklist-driven **canonical
Huffman code-length-tree builder** (3-bit depth + 1-bit-per-node subdivide/terminate decisions, a
dumped 4-entry dispatch table) — this is the one real Huffman-tree-construction mechanism found
anywhere in this codec's video-adjacent code, but it builds a *tree*, it does not itself decode
symbols the way the three named giants above do (which are fixed-width/table-driven, not tree-walked).
This is the SAME "mega-function miscounted as many functions" pattern already documented for the
video core's own case-dispatch mega-function (§5.1). **One honest gap left OPEN: its own caller was
not found this pass** (checked all three giants and four audio-range siblings directly — none calls
it).

**[CONFIRMED — disassembly for the bit-reading mechanism, all three named engines' real structure,
the dispatcher/sibling-case split for `0x1001c3d7`, and the VLC family's true single-function
boundary and Huffman-tree-builder role.]** ~~OPEN — the `0x10037cc0` table's audio-side consumer; the
VLC tree-builder's own caller.~~ **Both CLOSED 2026-10-08, §5.3b: `0x10037cc0` is a real IDCT
dequantization matrix (not an audio-side table at all in this calling context); the tree-builder's
caller is `0x10025e00`, itself called from video primary case 4.** Full detail:
`scratchpad/bink_video_entropy_coding_2026-10-08.md`.

### 5.3b Primary cases 2/3, and the real video-side DCT residual family — CLOSES former OPEN item 5a (2026-10-08)

A follow-up unit opened primary dispatch cases 2 and 3 (never read by any prior unit) and followed
the three "external call" targets further than the entropy unit did, directly answering `spec-bink-codec.md`'s
own standing question: **does this codec have a real residual/transform block type at all?** Answer:
**yes — two distinct families carry genuinely new per-pixel content, one of them a real DCT.**

**Primary case 2 (`0x1001bebe`) — a third motion-copy variant (CONFIRMED).** Signed dy/dx motion
vector, dual bounds-checked, straight 8×8 block copy from the computed reference offset — zero
entropy calls, zero residual. Real and fully traced, not the answer to the standing question.

**Primary case 3 (`0x1001bfcb`) — a genuine intra texture block, giant1's full-resolution twin
(CONFIRMED).** The exact same run-length/literal-replicate engine as `0x1001af5e` ("giant1," §5.3) —
identical 64-byte scan-order permutation table (`0x100378c0`), identical 64-coefficient budget,
identical literal/replicate semantics — but **without** giant1's final 2× pixel-doubling upsample:
it writes 64 brand-new literal bytes straight into an 8×8 block with zero reference to any other
frame. Not a transform (no multiply, no cosine constant), but genuinely new bitstream content —
giant1 is this block's half-resolution+upsampled sibling.

**Primary case 4's "external call" (`0x10025e00`) is a non-DCT residual-add, and resolves the VLC
tree-builder's own open caller gap.** `0x10025e00` calls `0x100258d0` (§5.3a's Huffman-tree builder) —
**its caller was the one thing §5.3a left OPEN; it is this function, reached only from video primary
case 4.** The tree's decoded output (64 small values, one 8×8 block's worth) is added, position by
position, onto a motion-compensated reference array: `dest[i] = src[i] + delta[i]` — a textbook
residual-add, confirmed by full call-site register/stack provenance from case 4's own body (the
tree's "bitstream context pointer" argument is the SAME shared bit-reader state slot every other
case's nibble/byte reads use — this residual is read in-line from the block's own bitstream
position, not a side channel into unrelated data).

**`0x10024eb0`/`0x100244c0`/`0x10024b50` are a genuine fixed-point two-stage separable 8-point
inverse DCT pair, fed by a sibling AC-coefficient tree-decoder — the real video-side transform
§8's former OPEN item 5a asked about (CONFIRMED).**

- `0x10024eb0` is a second, sibling instance of the same breadth-first worklist-driven compact-tree
  mechanism `0x100258d0` implements (shared seed bytes, same cache-then-refill bit read, a private
  4-entry dispatch table) — but decodes **signed 16-bit coefficient values**, not code lengths. It
  writes ~30 dword/word fields into a shared `[ebp-0x310]` output struct — the AC coefficients of an
  8×8 block, one DC value having already been read directly by the caller.
- `0x100244c0` (intra) and `0x10024b50` (inter/residual) are near-byte-identical: both read the same
  64-coefficient struct, dequantize each coefficient via a per-row entry from a 16-row×256-byte
  `int32` quantization matrix (`0x10037cc0` intra, `0x10038cc0` inter — adjacent, same-size sibling
  tables, both byte-exact dumped in `spec-bink-static-tables.md`). **Correction, 2026-10-08: the
  quantizer-strength progression across rows is monotonic but NOT strictly linear** — the per-row
  ratio of column k=1 to row 0's own k=1 value steps as `1.00, 1.33, 1.67, 2.00, 2.67, 3.50, 4.00,
  5.00, 6.00, 8.00, 12.00, 17.00, 22.00, 28.00, 34.00, 44.00`, an increasing-step-size ladder, not a
  constant-ratio one (the earlier "clean linear progression" phrasing is retracted). **New finding:
  the coefficient-0 (DC) column is byte-identical between the intra and inter matrices, row for
  row** (`65536, 87381, 109227, 131072...` in both) — only the AC columns (k≥1) differ between the two
  tables. Both findings are independently re-checkable against the raw bytes in
  `spec-bink-static-tables.md`. Run a
  confirmed two-stage "row-pass then column-pass" separable-transform butterfly with a universal
  **DC-only fast path** (all-AC-zero → broadcast the scaled DC value, the standard real-world IDCT
  optimization, present independently in both functions). Four recurring integer constants in the
  butterfly stage are exact fixed-point encodings (to rounding) of classic 8-point DCT-II-derived
  rotation factors — √2, 2·cos(π/8), 2·(√2·cos(3π/8)), and their product — independently computed for
  comparison, not read from any external source. The one difference between the two: `0x100244c0`
  stores the inverse-transformed result directly (intra); `0x10024b50` adds it onto an existing
  motion-compensated reference byte (inter/residual) — the classic "predict, then add an
  inverse-transformed residual" P-block shape.
- **Call-site wiring, both cases traced end to end:** case 5 (`0x1001c321`) reads a 16-bit DC literal,
  calls `0x10024eb0` for the AC coefficients, reads a 4-bit nibble selecting the quantizer row, then
  (via the `0x1001c3d7` dispatcher, §5.3a) calls `0x100244c0` — an **intra DCT block**. Case 7
  (`0x1001c4b6`) does the same DC/AC read but first computes a motion-compensated reference pointer
  (same mechanism as case 2/4), then calls `0x10024b50` directly — a **motion-compensated DCT
  *residual* block**.

**This closes §8's former OPEN item 5a** ("the real video-side per-block transform, if this codec
revision has one at all, remains unidentified... or this revision may simply not use a classic DCT
residual scheme") — it does, reached via cases 5 and 7, in a previously-unopened function pair that
happened to sit in the same nominal "audio-range" address band `0x10018600` (§6.5a, confirmed audio)
occupied — physical proximity, not functional membership, exactly the trap this document's own §7
already warns about.

**Full case-by-case map, all 10 primary cases plus the secondary 7-way sub-dispatch (CONFIRMED):**

| Case | Mechanism | New content? |
|---|---|---|
| 0, 9 | Plain block copy | No — previous frame |
| 1 sub 1/4 | Skip-flag mark only | No |
| 1 sub 2 | Third DC+AC dequant/IDCT engine (direct-store), sibling of case 5/7's `0x100244c0`/`0x10024b50` IDCT pair — reached only from this sub-dispatch slot | New content, real DCT |
| 1 sub 3 | DC/flat fill | New literal, no transform |
| 1 sub 6 | Literal/raw copy, dual-row duplication (byte- or word-granular, alignment-selected) | New literal (bitstream bytes copied verbatim), no transform |
| 1 sub 5 (giant2), 8 | 2/4-bit PATTERN blend | New literals, blended |
| 1 sub 0 (giant1) | RLE+permutation, half-res, 2× upsampled | New content, no transform |
| 2 | Motion copy, variant 3 | No — motion reference |
| **3** | **RLE+permutation, full-res, no upsample** | **New content, no transform** |
| **4** | **Motion compensation + Huffman-tree-coded delta-add** | **Motion reference + new residual, no transform** |
| **5** | **Intra 2-stage fixed-point 8-point IDCT** | **New content, real DCT** |
| **7** | **Motion compensation + 2-stage fixed-point 8-point IDCT residual** | **Motion reference + IDCT residual, real DCT** |

**[CONFIRMED — full disassembly of every function in this chain, complete call-site register/stack
provenance, both quantization tables dumped and their progression verified by direct ratio check.
Primary case 1's own secondary=2 and secondary=6 sub-cases, directly disassembled 2026-10-08 (closes
the former OPEN item — both are real and fully traceable, and neither is DC/flat fill): secondary=2
(`0x1001b2ff`-`0x1001b3d2`) is a third DC+AC dequantize/IDCT engine — DC literal into the shared
`[ebp-0x310]` struct, `call 0x10024eb0` (`0x1001b348`) for the AC coefficients, an inline 4-bit nibble
read selecting a row of the same `0x10037cc0` quantization matrix `0x1001c3d7` uses, then
`call 0x100247d0` (`0x1001b3c8`) — a third, direct-store sibling of the `0x100244c0`/`0x10024b50`
two-stage fixed-point 8-point IDCT pair (same DC-only fast path, same `0xb50`/`0x8a9`/`0xec8`/
`0xffffeb18` rotation constants, `0x100247d0` read in full, `0x100247d0`-`0x10024b4f`). Secondary=6
(`0x1001ba40`-`0x1001be6a`) is a literal/raw copy-with-dual-row-duplication engine, not DC/flat fill —
an alignment test (`test al,3` at `0x1001ba6f`) selects a byte-granular "unaligned" copy loop
(`0x1001ba90`-`0x1001bb67`) or a word-granular "aligned" copy loop (`0x1001bb72`-`0x1001be6a`), each
repacking literal source bytes/words into a dword written to two destination rows at once, with zero
bitstream bit-reads, table lookups or external calls — a sibling of the plain block-copy cases
(primary 0/9), structurally unrelated to secondary=3's one-byte-broadcast DC fill. (The former table
row grouping "sub 3, 6" together as DC/flat fill was a transcription slip pulling sub-6 into sub-3's
row; sub-3 alone is DC/flat fill, confirmed unchanged.)
~~OPEN — the precise byte-offset-to-coefficient-index mapping inside `0x10024eb0`'s output struct
(zig-zag/row-major/other, not individually walked field-by-field)~~ **CLOSED, 2026-10-08: ROW-MAJOR,
not zig-zag — no re-sorting needed.** For coefficient index `k = 0..63`, the AC-coefficient struct's
16-bit-word byte offset is `struct_byte_offset(k) = 2*k`, and the quantization matrix's own per-row
4-byte-dword offset is `quant_byte_offset(k) = 4*k` — the same linear index `k` drives both, confirmed
directly against the dumped matrix bytes in `spec-bink-static-tables.md`. OPEN — a full bit-exact
hand-decode of one real case-5/7 block checked pixel-for-pixel against FFmpeg's own output (structurally
confirmed, not yet verified against one concrete worked example — real, bounded, un-done work, not a
structural unknown).]** Full
detail: `scratchpad/bink_video_residual_texture_block_2026-10-08.md`; secondary=2/6 ground truth:
`scratchpad/bink_case1_sub6_and_tables_2026-10-08.md`.

### 5.3c A previously-undocumented entropy pre-stage feeds §5.3's own primary case-selector byte stream — new OPEN item, the real blocker on the still-open worked-example (2026-10-08)

A fresh attempt at the bit-exact hand-decode-vs-FFmpeg worked example (§8 item 5a's own standing OPEN
item) did not reach a real case-5/7 block instance, but found something genuinely new on the way: **the
primary dispatch's own case-selector byte (§5.3's "a byte is read from the bitstream, range-checked
0-9") is NOT read from the frame's raw bytes.** `[ebp-0x170]`/`[ebp-0x16c]` (the cursor and bound that
byte is actually read from, `0x1001abd7`-`0x1001abe9`) are zero-initialized on entry to `0x1001a530` and
written only once, by `call 0x10019360` — a length-prefixed decompression stage, not a cursor-range
setup helper. Read in full (`0x10019360`-`0x100195a5`, ~130 instructions, idempotent-guarded): it reads a
bit-packed length from the shared bit-reader context, at a bit-width computed from the row's own
geometry (`N = bit_length(Width/8 + 0x1ff)` — for a 1920-wide frame, a concrete, re-checkable 10-bit
field), bounds-checks it, points `[ebp-0x170]`/`[ebp-0x16c]` at a fresh range inside one of the nine
`0x1001a490`-sized internal scratch buffers (§5.5, not raw file memory), then **fills that scratch range**
via a two-level table-driven expansion (literal-byte vs. run-length/table-lookup, selected per symbol
against the constant `0xc`, through a static table at `0x1003bd48`). **Narrated here only at a high
level — superseded 2026-10-08 by §5.3g's full instruction-level algorithm, including the correction
that the `0xc` comparison is on the post-translation output value `bl`, not the raw Huffman symbol.**

**That expansion's own lookup tables are themselves built, per call, by `call 0x10018ca0`** (read in
full, ~230 instructions) — reads a 4-bit selector nibble, indexes three small static tables
(`0x1003bd44`: 16 bytes, dumped exactly as `04 05 05 05 05 05 05 06 06 06 06 06 06 07 07 07`; an offset
table at `0x1003c140`, 16 dwords, address confirmed; a third region at `0x1003bd60`/`0x1003bd48`, address
confirmed), takes a trivial "flat/default" fast path on nibble 0 (fills a 16-byte local with the literal
identity sequence `0..0xF`), and otherwise calls a further helper, `0x10018c20`, four times to build the
real table. **This is structurally a THIRD sibling of this codec's own "breadth-first worklist-driven
Huffman-style tree builder" family** (`0x100258d0`, §5.3a's code-length-tree builder; `0x10024eb0`,
§5.3b's AC-DCT-coefficient tree builder) — the same underlying construction idiom now confirmed in three
independently-discovered places (block-type control bytes, residual deltas, AC-DCT coefficients), not
just the two already on record. A genuinely new structural finding, not previously suspected.

**Also newly found on the way: a previously-undocumented per-frame sub-chunk structure.** Each of the (up
to) four `0x1001a530` calls per frame (§5.1's own already-documented count) consumes its own independent,
4-byte-length-prefixed sub-chunk, addressed via a stable base pointer (`handle+0x11c`, almost certainly
`BinkNextFrame`'s own internal read buffer) plus a running byte offset (`handle+0xe8`) — traced directly
in `BinkDoFramePlane` (`0x10011280`) and `0x1001cb00`'s own body (`0x1001cc68`-`0x1001ccbb`). Not
previously identified by any prior unit, which had stopped at "`0x1001cb00` calls `0x1001a530` up to four
times" without tracing where each call's own input bytes come from. One directly re-checkable
consequence (HIGH, not independently confirmed — would need `BinkNextFrame` itself opened): if
`handle+0xe8` starts at 0 for a frame's first call, the Y-plane sub-chunk's length prefix sits at exactly
`offs[N]+0` (§1.6's own frame-blob start) and the real per-row bitstream begins at `offs[N]+4`.

**Both `0x1001cb00` and `0x1001a530` take a hidden register argument, the same "hidden EDI" pattern §1.5a
already found for `0x1000e560`** — now confirmed twice more inside the video core itself (`0x1001cb00`'s
hidden `ecx`/`edi` and `0x1001a530`'s hidden `ecx`, the latter being exactly the bitstream-cursor start
this section's own sub-chunk tracing resolves). Directly reusable plumbing for any future emulation
attempt.

**Consequence for §8 item 5a: still OPEN, materially narrower and better-understood than before, not
closed by this pass.** No case-5/7 block was reached and no coefficient/pixel value is reported anywhere
— the format in front of the primary dispatch is deeper than previously mapped, not that no suitable
file/frame exists (`main_menu_bg.bik` frame 0, §1.6, remains the recommended real-sample target). **Two
concrete, scoped continuation options, both detailed in the scratch file:** (A) finish hand-disassembling
`0x1001a160`/`0x10019f00`/`0x10019af0`/`0x10018c20` and cleanly re-dump the remaining static tables; (B,
likely faster) a Unicorn-based surgical real-code-emulation harness — `unicorn` 2.1.4 is already
installed in this environment alongside `capstone`/`pefile` (no download needed), `binkw32.dll`'s import
table is small and tractable on the pure decode-one-frame path (no DirectSound/COM dependency), and this
pass's own §3 argument-convention findings give the exact recipe to call `0x1001cb00` directly and let
the real DLL finish the entropy decode a hand-trace couldn't complete in time.

**Update, 2026-10-08 (follow-up pass): `0x1003bd44`, the run-length sub-table at `0x1003bd54`,
`0x1003c140`, and `0x1003bd60` are now all byte-exact dumped and independently re-verified — the prior
"not transcribed with full confidence" caveat is CLOSED.** Raw bytes for all four regions (plus both
quantization matrices, §5.3b) are published in `spec-bink-static-tables.md`, captured by direct byte
read (not decompiler pseudocode) and re-dumped/SHA-256'd a second time by the SPEC TEAM editor directly
from the agent's saved `.bin` captures, so there is no retyping-error risk in the published blob. The
16-byte symbol-class table at `0x1003bd44` reads `04 05 05 05 05 05 05 06 06 06 06 06 06 07 07 07`,
confirming the earlier partial dump exactly.

**[CONFIRMED — disassembly, every address and instruction sequence cited above, `0x1003bd44`'s 16 bytes
dumped exactly. HIGH CONFIDENCE, not independently confirmed: `handle+0xe8` starting at 0 per frame; the
exact advance-between-calls arithmetic; whether other already-documented cases' own "flat literal" byte
reads are also behind a similar pre-stage (not checked — flagged so it isn't assumed either way without
checking). OPEN: ~~`0x1001a160`/`0x10019f00`/`0x10019af0`/`0x10018c20` not opened~~ **`0x1001a160` OPENED,
2026-10-08, §5.3d — it is unrelated to this pre-stage; see there.** `0x10019f00`/`0x10019af0` remain not
opened. ~~`0x1003c140`/`0x1003bd48`/`0x1003bd60`'s exact per-entry contents not transcribed with full
confidence~~ **CLOSED, see the update above and `spec-bink-static-tables.md`.** OPEN: whether the other
per-row sub-streams (motion vectors, DC literals, nibble selects) sit behind a `0x10019360`-style stage
too.]** Full detail: `scratchpad/bink_video_worked_example_block_2026-10-08-v2.md`.

**Extended 2026-10-09 (Team B follow-up, routed via the orchestrator): the primary case-1's own
SECONDARY (sub-type) selector byte is confirmed NOT to share this pre-stage with the primary byte.**
§5.3c above establishes that the primary case-selector byte comes from descriptor A's own
`0x10019360`-filled scratch range, not raw bitstream bits. **The secondary/sub-type byte (§5.3's
"a byte is read from the bitstream... subtracts 3, range-checks 0-6") instead comes from
descriptor B's own, separately-filled scratch buffer** — the sibling `0x10019360(desc_B, ...)` call
§5.3g documents firing unconditionally once per row. Live-traced on both of Team B's own named
D10≥1 files (`logo_amd_hd.bik`, `logo_thq_and_voli_hd.bik`), block (0,0): the secondary byte's real
source address is, on both files, EXACTLY descriptor B's own cursor start value right after its
refill returns — i.e. it is literally the first byte descriptor B's own pre-stage decoded for that
row. Primary and secondary byte reads happen at the **identical live bit position** on both files
(388 and 362 respectively — zero raw bitstream bits consumed between the two reads), and this pass's
own measured descriptor-A refill-return bit positions (227, 209) exactly match Team B's own
independently-reported pre-stage consumption numbers, confirming both sides are looking at the same
real event. **Consequence: a raw `readBits(4)` approach reads the wrong SOURCE entirely, not just
the wrong bit offset — no bitstream-offset correction can fix it, because the real value never comes
from the bit-reader for this field.** The fix is architectural: descriptor B needs its own
`0x10019360`-style pre-decode stage (already known to exist and fire unconditionally, §5.3g) feeding
its own output byte array via an independent byte cursor, exactly mirroring how descriptor A already
feeds the primary case-selector byte. **[CONFIRMED — live execution (Unicorn, real unmodified DLL),
both named files, block (0,0). OPEN: whether this exact desc_A→primary / desc_B→secondary mapping
holds for primary values other than 1, or for the chromaA/chromaB planes — only Y-plane, primary=1,
block (0,0) was traced for this question.]** Full detail:
`scratchpad/bink_twoliteralpattern_0x1001b5a3_2026-10-09.md` (Part B).

### 5.3d Unicorn harness: real `binkw32.dll` execution, the DC-bug root cause, and the chromaB gap (2026-10-08)

**A Unicorn CPU-emulation harness (Option B of §5.3c's two continuation options) was built and ran
real `binkw32.dll` code end to end** — `0x1001cb00` called directly with the exact argument layout,
mapped at its native base, no Ghidra/decompiler involvement. It produced a real decoded Y/chroma-A
plane for both a keyframe (frame 0) and a real inter-predicted P-frame (frame 1) of `main_menu_bg.bik`
(§1.6's own real-sample target), cross-checked byte-for-byte against FFmpeg's own decode of the same
file. **Raw stage buffers (case-selector scratch, 14 real DC+AC coefficient instances, one
independently-verified post-IDCT block, and final plane crops for both frames) are published as data
in `spec-bink-video-test-vectors.md` — read that alongside this section.**

**Result: NOT bit-exact, but root-caused, not just observed.** Overall byte match: 50.4% (frame 0),
51.8% (frame 1). Per-8x8-block analysis (32,400 blocks, frame 0): 16,328 match FFmpeg exactly; 16,045
differ on ALL 64 bytes by a uniform, constant **+128 (mod 256)** at every pixel; only 27 are partial
(boundary artifacts) — a clean, binary, content-dependent split, not noise or desync. **Critically,
even in the mismatching TEXTURED blocks, the AC/texture pattern is bit-exact — only the DC/base level
is off, by exactly +128.** This proves the AC-coefficient tree decoder (`0x10024eb0`), the
dequantization math, and the IDCT butterfly itself (§5.3b) are all working correctly end-to-end on
real compressed data — **the bug is isolated to the DC value specifically.**

**Root cause: `0x1001a160`, a previously-unopened helper, is a sign-flippable DPCM/delta-accumulator
feeding case 5/7's own 16-bit DC literal read from a dedicated, independent, length-prefixed
sub-stream — not previously documented at all, separate from both §5.3c's case-selector pre-stage and
the main per-row bitstream.** Its core loop (disassembled, real addresses): reads a small signed delta
from a THIRD independent bit-reader context (its own two arguments — distinct from both the main
per-row context and §5.3c's pre-stage context), conditionally negates it (`test cl,1` / `neg eax`),
accumulates it into a running predictor (`ecx = predictor + delta`), writes the accumulated value as
the current block's DC word, and persists the predictor for the next iteration — plus a separate
`rep stosd`/`rep stosw` fast path for runs where the predictor doesn't change. **This exactly explains
the observed symptom**: a +128 error in the initial seed, or in how the third bit-reader context's own
base/bound get set up, would propagate through every subsequent DELTA-mode block but self-correct
whenever the bitstream emits an ABSOLUTE reset — producing exactly the observed "runs of uniformly
wrong, then correct, content-dependent" pattern. **Not fixed this pass** — doing so needs
`0x1001a160`'s own `[ebp+0xc]` argument (a 3-field descriptor) traced back to wherever `0x1001a530`
builds it, not reached in the time available. This is now a much narrower target than the prior
"finish hand-disassembling the three giants" framing: one function, with its exact bug mechanism (a
wrong seed or wrong sub-stream base/bound for a specific DPCM-accumulator context) already identified.

**Separate, still-unresolved: chromaB never decodes in this harness.** `0x1001cb00`'s 4th call into
`0x1001a530` (chromaB, §5.6's call 4) consistently runs to completion (272 internal per-row iterations,
matching chromaA's own count exactly) but never writes a single real pixel byte — stays at its
sentinel fill throughout, in both frames tested. ChromaA (call 3) works and produces plausible, partly
FFmpeg-matching output (same +128 DC issue as Y, nothing additional found). **Correction, 2026-10-08:
this project's "chromaA" buffer (`record+0x28`/`+0x2c`) matches FFmpeg's own V (Cr) channel, not U
(Cb) as previously assumed** — measured directly by comparing chromaA's raw bytes against both FFmpeg
reference channels (chromaA vs. FFmpeg-U: 0.2% byte match; chromaA vs. FFmpeg-V: 40.5%, matching the
same +128 DC-bug pattern already documented for Y). Any future consumer of this harness's chroma output
should label it V, not U. Exact cause of chromaB's non-decode not isolated —
plausible, NOT confirmed candidates: a subtly wrong chroma pitch/dimension for this specific call's own
argument slot, or an issue in how its `record+0x34` pointer (reused as both source and destination in
this single-slot harness setup) gets read by whichever case type chromaB's control-byte stream selects.

**A correction to a prior worked-example note's own argument map for `0x1001cb00`** (not previously
spliced into this spec file, so recorded here directly): `[ebp+0x10]` is the sub-chunk cursor (confirmed
by its direct use as the hidden-ECX bitstream-cursor argument into `0x1001a530`'s first call) and
`[ebp+0x14]` is the whole-buffer-end bound (confirmed by its use in the `jae`-to-fault bounds check) —
the reverse of how a prior scratch note had them. Also newly confirmed: `[ebp+8]` (the in/out scratch
pointer) must be passed non-null to avoid a side effect where the function also forces `[ebp+0xc]`
(macroblock column count) to zero.

**Call 2's `record+0x20` role (§5.6's own OPEN item 6a) — partially resolved, a concrete negative.**
Tracing `0x1001cb00`'s own argument setup for call 2 shows its `[ebx+8]` slot (the same stack slot
calls 1/3/4 use as a real plane pointer) instead receives **`record[target-slot]+0x1c`'s raw VALUE**
(the ready/dirty flag, 0 or 1) — not a pointer at all. **This confirms call 2 is NOT a 4th color plane**
(a real pixel-plane write needs a `(pointer, pitch)` pair; this call supplies a 0/1 flag value at the
pointer-shaped slot instead) — the "drives the per-macroblock skip/change-flag array" half of §5.6's
own hypothesis now has a concrete mechanism, though the array's own consumer-side semantics remain
OPEN.

**`0x100244c0` (the intra IDCT, §5.3b) does NOT write its result back into the input coefficient
struct, and recursively re-enters its own start address once per block.** Confirmed structurally: the
coefficient struct is byte-identical before and after the call — the transformed pixel output goes
through a separate destination. ~~whose own write-site/argument was not pinned down this pass
(OPEN)~~ **CLOSED, see §5.3e below: `[ebp+8]` is the destination pointer, `[ebp+0xc]` is the pitch.**
The self-re-entry (same struct pointer, same block, a bogus non-return-address value briefly visible on
the stack at the inner call) is consistent with a two-pass row-then-column butterfly implemented as one
explicit self-call rather than two inlined loops — not previously documented at this level of detail.

**[CONFIRMED — real Unicorn-emulated execution of unmodified `binkw32.dll` code, cross-checked against
FFmpeg's own decode byte-for-byte; the root-cause mechanism for the DC bug (a real DPCM accumulator with
a wrong seed/base, not yet pinned to its own setup site); call 2's non-pointer argument; the IDCT's
no-writeback-to-input-struct behavior and self-re-entry; `0x100244c0`'s real pixel-output destination
(§5.3e). OPEN: the DC-bug's TRUE root cause (the construction site is now traced, §5.3e, but two
concrete empirical fix hypotheses were tried and both REFUTED — see §5.3e); chromaB's root cause.]**
Full detail: `scratchpad/bink_unicorn_harness_2026-10-08/bink_unicorn_harness_findings_2026-10-08.md`;
raw data: `spec-bink-video-test-vectors.md`.

### 5.3e Follow-up Unicorn pass: DC-descriptor construction site traced, pixel-output destination closed, two fix hypotheses tried and REFUTED (2026-10-08)

A second Unicorn-harness pass picked up §5.3d's own "not reached in the time available" thread —
tracing `0x1001a160`'s `[ebp+0xc]` 3-field descriptor back to its construction site — and ran the
harness again with fresh instrumentation. **Net result: real progress on provenance, but the DC bug's
true root cause is still OPEN** — both concrete hypotheses this pass could actually test turned out to
be wrong, and that is reported honestly rather than papered over.

**`0x1001a160` re-characterized: NOT a pure delta accumulator.** Full re-read of its body
(`scratchpad/.../fn_a160_full.txt`) found no hardcoded `128`/`0x80` bias constant anywhere — exhaustively
checked, none exists. Its actual behavior per batch: **one absolute magnitude+sign seed value, then N-1
deltas** — ~~the seed's own bit-width is sourced dynamically from a third pointer argument (`param3`,
fields at `+8`/`+0xc`/`+0x2c`/`+0x30`), not a fixed constant.~~ **CORRECTED 2026-10-08 (follow-up pass,
§5.3f): those fields are on `param1` (`[ebp+8]`), not `param3` — a mislabeling in this paragraph. See
§5.3f for the corrected addressing and the full batch structure.** §5.3d's "sign-flippable DPCM
accumulator" framing understates this: the first value in every batch is a seed, not a delta off a
carried-over predictor from the previous batch.

**The descriptor's construction site is now traced.** `0x1001a530` builds the persistent 3-field
cursor/base/bound descriptor exactly once per plane, at `0x1001a5ae`-`0x1001a5f3`: FIELD1 (BASE) =
the raw incoming hidden-`ecx` argument to `0x1001a530` itself, FIELD2 (BOUND) = 0, FIELD0 (CURSOR) = 0.
This is the first time this construction site has been traced to source.

**Two concrete empirical fix hypotheses were tested directly against the harness — both REFUTED, not
confirmed:**

1. *"Y's base pointer is wrongly unskipped past its own length prefix."* Refuted — Y (call 1) is
   correctly already positioned at `FRAME_BUF_BASE+4` (i.e. past its own 4-byte length prefix); there
   is no off-by-one-skip bug on the Y path.
2. *"ChromaA/chromaB's base lands exactly ON their own would-be length-prefix dword, one read short of
   where it should start."* This IS a real, measured discrepancy — chromaA's `CTX_ECX` at
   `0x40020f74` vs. Y's `0x40000004`, a difference of `0x20f70` (135024), which matches the already-
   documented Y sub-chunk length (135028) minus 4. It looked like the obvious fix. **Patching in a `+4`
   skip for calls 2+ made chromaA's FFmpeg match rate WORSE, not better (40.5% → 0.1%).** This "obvious
   fix" is empirically wrong. The true root cause of the base-pointer discrepancy remains unexplained.

~~**Most promising untraced lead, not yet chased:** `param3` itself — ... resolves ... to a fixed
address `0x4003318c` ...~~ **CLOSED as a dead end, 2026-10-08 (§5.3f): `param3` is referenced exactly
once in the whole function, as a plain end-of-buffer bounds check — it is `0x1001cb00`'s own
`[ebp+0x14]` whole-buffer-end argument threaded down, identical across every plane for the mundane
reason that there is only one frame blob. It supplies no bit-width, no seed data. Not a lead.** The
"2 calls per plane, descriptor initialized once" anomaly remains unexplained, not chased further.

**`0x100244c0`'s real pixel-output destination, closed as a side effect of this pass's tracing:**
`[ebp+8]` is the destination pointer, `[ebp+0xc]` is the pitch (stride) — closes the OPEN item §5.3d
left on this question.

**Re-run byte-match numbers (NOT bit-exact — honestly reported, not rounded up):** Y 50.4%
(1,045,020/2,073,600 bytes; 16,328/32,400 blocks exact; 15,733/32,400 uniformly +128; the remainder
partial/boundary). ChromaA(=FFmpeg V, see correction above) 40.5% (210,088/518,400 bytes; 3,255/8,040
blocks exact). ChromaB: still 0.0%, pure `0xEE` sentinel fill throughout — completely unresolved, same
as §5.3d.

**[CONFIRMED — the `0x1001a160` no-bias-constant finding (exhaustive body re-read) and the descriptor
construction site (`0x1001a530` at `0x1001a5ae`-`0x1001a5f3`); both refuted hypotheses, including the
exact measured offset for hypothesis 2 and the measured match-rate regression from patching it; the
`0x100244c0` destination/pitch argument closure; the re-run byte-match numbers. ~~HIGH, not independently
confirmed: the `param3`/`0x4003318c` lead...~~ **CORRECTED, §5.3f: mislabeled, actually a dead end.**
OPEN, unchanged: the DC bug's true root cause; chromaB's root cause (now CONFIRMED-but-unfixed, §5.3f).]**
Full detail: `scratchpad/bink_entropy_tables_and_dc_fix_2026-10-08.md`.

### 5.3f Second follow-up pass: corrects the `param3` lead, fully characterizes the DC batch structure, and confirms chromaB's root cause by direct instrumentation (2026-10-08)

A third Unicorn-harness pass re-read §5.3e's own saved disassembly line by line and ran new targeted
instrumentation. **Net result: the DC +128 bug is still OPEN (one lead closed as a dead end, one new
fix hypothesis tried and refuted), but chromaB's "never writes a pixel" symptom now has a CONFIRMED
mechanical root cause** (not fixed).

**Correction to §5.3e: the `param3`/`0x4003318c` lead was mislabeled — it's really `param1`, and it's a
dead end.** Re-reading `0x1001a160`'s own saved disassembly shows `ecx` is loaded from `param1`
(`[ebp+8]`) and never reloaded from `param3` before any of the offsets `+8`/`+0xc`/`+0x28`/`+0x2c`/
`+0x30` are read — **all five belong to `param1`, not `param3`.** `param3` (`[ebp+0x10]`) is referenced
exactly once in the whole function, as a plain end-of-buffer bounds check (`cmp ebx, [ebp+0x10]`) —
no bit-width, no seed data. `0x4003318c` is simply `0x1001cb00`'s own whole-buffer-end argument
(`FRAME_BUF_BASE + frame_blob_size`) threaded straight down; it's identical across every plane because
there is only one frame blob, not a bit-width table. **Closed, not a live lead.**

**New structural ground: `0x1001a160`'s real batch/seed/delta shape, CONFIRMED and live-cross-checked.**
Corrected addressing (all on `param1`) gives a clean two-level scheme: a batch starts by reading an
`M`-bit field (bit-width = `param1+0x28`, constant `10`) giving the batch's own item COUNT (not a DC
value — confirmed by its reuse as a decrementing loop counter). The batch's first word is an **absolute
magnitude+sign SEED**: an `N`-bit magnitude (`N` = `param1+8`, constant `11` everywhere) read via one of
two branches selected by a per-call-site (not per-plane) flag at `param1+0xc`; one branch separately
reads a sign bit and conditionally negates, the other's extraction falls straight through with no visible
separate sign-bit read [HIGH — disassembly-read, not independently bit-probed]. The remaining `M-1` words
are **DELTAS**, processed in chunks of up to 8, each chunk keyed by a 4-bit "class" nibble — class 0 is a
`rep stosd`/`stosw` fast-fill (predictor repeats unchanged), class≠0 decodes up to 8 individual signed
deltas at a chunk-local bit-width equal to the class nibble, via the already-documented
test-cl-1/neg-eax/predictor-accumulate idiom. Consistent with §5.3a's "fixed-width + table-driven, not
Huffman" framing for this codec's other entropy stages.

**Negative result: every field this structure depends on is plane-independent — live-captured, ruling out
a per-plane misconfiguration.** `N` (11) and the `M`-width (10) are identical across Y/chromaA/chromaB at
both of `0x1001a530`'s call sites; the sanity max-magnitude bound differs luma-vs-chroma (1504 vs. 1264)
but is **identical between chromaA and chromaB** — the two planes with different outcomes. Whatever
causes the +128 symptom must be in the actual bit *values* decoded for specific batches (a
positional/alignment issue), not a fixed per-call configuration constant.

**One new, broader fix hypothesis tried — REFUTED again, same direction as §5.3e's narrower one.**
§5.3e patched only the DC descriptor's own copy by +4; this pass patched the raw hidden-`ecx` argument
itself at `0x1001a530`'s entry, before any internal copy derives from it. Result: Y unaffected; chromaA
regresses identically (40.5%→0.1%); **chromaB, for the first time in this harness's history, stops being
pure sentinel fill and starts writing real bytes — but the content is 0.0% match against FFmpeg (pure
noise), not a fix.** The "+4 skip" family of patches is wrong at a more fundamental level than which
internal copy it's applied to — but it does demonstrate something in this exact neighborhood is capable
of unsticking chromaB's write path at all.

**ChromaB's root cause: CONFIRMED by direct instrumentation, not fixed.** Hooking the primary 10-way
case dispatch (`0x1001abfb`, a `jmp [edx*4+0x1001cab8]` gated by a case-selector-scratch
cursor/bound-exhaustion check at `0x1001abdd`) directly, tagged by plane: Y reaches the real dispatch
23,914/23,914 attempts; chromaA 6,042/6,202; **chromaB reaches it 0/8,160 times — every single attempt,
across all 272 rows, is short-circuited by the exhaustion check before any case body (copy/fill/RLE/
either DCT path) ever runs.** This directly refutes §5.3d's own two standing candidate explanations for
chromaB (a chroma pitch/dimension mismatch; an issue in how `record+0x34`'s pointer is read by whichever
case is selected) — **no case is ever selected, so no destination pointer of any kind is ever
dereferenced.** Tracing one level further: the entropy pre-stage `0x10019360` (§5.3c) that fills that
scratch buffer gets permanently stuck exactly **-4 bytes past its own bound starting from its SECOND
invocation for chromaB specifically**, and never recovers for the remaining 67 calls — while the same
function cycles through healthy oscillating positive refills for Y and chromaA throughout. This lands on
the same "+4/own-length-prefix" quantum already documented for the DC descriptor's base pointer (§5.3e) —
circumstantial evidence the DC bug and chromaB's stall share an underlying sub-chunk-boundary computation,
even though neither concrete "+4 skip" patch tried so far (§5.3e's narrow one, this pass's broad one) is
the correct fix. The broad fix test above independently corroborates this: it unsticks chromaB's write
path (real bytes, not sentinel) without correcting its content — consistent with the stall, not the
content, being what that patch touches. **Open, not chased further:** why chromaA's own analogous
base-pointer discrepancy does not also permanently stall its `0x10019360` cycle the same way — chromaA
recovers to healthy values immediately after its first call, chromaB does not, despite both reportedly
sharing the same starting condition.

**[CONFIRMED — the `param3`→`param1` correction and `0x4003318c`'s closure as a dead end; the full
batch/seed/delta structure (live-cross-checked field values for both call sites, all three planes); the
plane-independence negative result; the broader "+4" fix test's refutation and its chromaB side effect;
chromaB's root cause (0/8,160 real dispatches reached, direct instrumentation) and its mechanical cause
(the `0x10019360` permanent -4 stall from its 2nd call onward). OPEN, unchanged: the DC bug's true
arithmetic root cause (the batch/seed/delta structure is now fully mapped — a future pass could
hand-decode one affected batch bit-by-bit against its neighbors to find the exact desync point, rather
than guessing pointer patches); chromaB's exact one-line fix (the stall's own cause inside `0x10019360`'s
refill logic); why chromaA's analogous discrepancy doesn't also stall.]** Full detail:
`scratchpad/bink_dc_bug_rootcause_chromaB_2026-10-08.md`.

### 5.3g The entropy pre-stage's full instruction-level algorithm: `0x10019360`/`0x10018ca0`/`0x10018c20`, call granularity, real bit position, and the chromaB stall mechanism (2026-10-08, closes all 4 of Team B's named blockers)

A dedicated pass fully disassembled the three functions §5.3c's own high-level narration left
un-bit-traced, cross-checked by live Unicorn-execution of unmodified `binkw32.dll` code (the same
harness §5.3d/e/f already used). **All 4 of Team B's own named blockers are CLOSED** (instruction-level
algorithm, table-build mechanics, call granularity, and real starting bit position), and a bonus
finding closes §5.3f's own named next-step on the chromaB stall mechanism.

**`0x10019360`'s full body (130 instructions, `0x10019360`-`0x100195a5`), the real symbol-expansion
loop.** Signature: `0x10019360(Desc* desc, BitReader* br, uint8_t* stream_end)`. `Desc` fields (all
relative to the descriptor §5.3c already named, e.g. `&[0x1001a530's ebp-0x170]` for Y's first call):
`+0x00`/`+0x04` cursor/bound (the same pair §5.3c and §5.6 already track), `+0x10`..`+0x1f` a 16-byte
symbol-translate/permutation table (built by `0x10018ca0`, below), `+0x20` = `A`, the max Huffman code
length for this call's codebook, `+0x24` = a pointer to a `2^A`-entry byte LUT (each byte packed as
`code_len<<4 | raw_symbol`), `+0x28` = `N`, the bit-width of the LEN field (`bit_length(Width/8+0x1ff)`,
10 bits for 1920-wide — matching §5.3c's own formula exactly), `+0x2c` = the LEN capacity, `+0x30` = the
fixed scratch backing buffer. `BitReader` (3 fields, 12 bytes: `+0x0 accum`, `+0x4 base_ptr`, `+0x8
bits_avail`) is **CONFIRMED by exact address cross-reference to be the identical struct, at the
identical address (`&[ebp_a530-0x204]`), as §5.3e/f's own "DC descriptor"** — one shared sequential bit
cursor feeds both the DC-delta decoder (`0x1001a160`) and the entire entropy pre-stage chain.

**Bit convention, CONFIRMED from the refill code (`0x10019388`-`0x100193d1`): LSB-first within each
32-bit little-endian dword** — bit 0 of the stream is the LSB of the first byte, bit 8 the LSB of the
second byte, etc., never MSB-first.

```c
// 0x10019360(desc, br, stream_end)
void entropy_prestage(Desc* desc, BitReader* br, uint8_t* stream_end) {
    if (desc->cursor != desc->bound) return;        // idempotency guard (0x1001936c/0x1001936f):
                                                      // prior range not yet fully drained -- no-op.
    uint32_t LEN = read_bits(br, desc->N);           // 0x10019382-0x100193d1, N=desc+0x28
    if (LEN == 0 || LEN > desc->CAP) {               // 0x100193d4-0x100193df
        desc->cursor = desc->SCRATCH_BASE + 4;        // 0x10019586-0x1001958c: POISON STATE
        desc->bound  = desc->SCRATCH_BASE;            // avail = bound-cursor = -4, forever
        write_back_bitreader(br); return;             // (this IS the chromaB "-4 stall" -- see below)
    }
    uint8_t* base = desc->SCRATCH_BASE;               // 0x100193e5-0x100193f0
    desc->cursor = base; desc->bound = base + LEN;
    uint8_t* out = base; uint32_t remaining = LEN;

    uint32_t mode_bit = read_bits(br, 1);             // 0x100193f3-0x10019417 (ONE bit, ONCE per
                                                        // CALL, not per symbol -- a whole-call selector)
    if (mode_bit == 1) {                              // 0x10019537-0x10019584
        uint32_t fill_value = read_bits(br, 4);         // a fresh, independent 4-bit field
        memset(out, fill_value, remaining);              // literal call to 0x10001eb0
        write_back_bitreader(br); return;
    }

    // mode_bit == 0: table-driven canonical-Huffman expansion, 0x1001941d-0x1001952f
    uint8_t last_literal = 0;
    while (remaining > 0) {
        uint32_t idx = peek_bits(br, desc->A);          // PEEK desc->A bits, do NOT consume yet
        uint8_t packed = desc->LUT_A[idx];                // 0x10019451/0x1001948f
        uint32_t code_len   = packed >> 4;                  // 0x10019457/0x1001949f
        uint32_t raw_symbol = packed & 0xF;                 // 0x1001945a/0x10019496
        consume_bits(br, code_len);                         // consume ONLY code_len, not A
        uint8_t bl = desc->table16[raw_symbol];               // 0x1001945d/0x1001949b -- the
                                                                // per-row translate table (0x10018ca0)
        if (bl < 0xC) {                                      // 0x100194c4/0x100194c7 -- on the
                                                                // TRANSLATED value bl, not raw_symbol
            *out++ = bl; remaining--; last_literal = bl;        // 0x1001951f-0x10019525
        } else {                                              // bl in {0xC,0xD,0xE,0xF}
            uint32_t run = RUNLEN[bl];        // 0x1003bd54..57 for bl=C..F == {4,8,12,32}
            if (run > remaining) { write_back_bitreader(br); return; }     // 0x100194ec: truncate
            memset(out, last_literal, run);   // 0x100194f8-0x10019518
            out += run; remaining -= run;
        }
    }
    write_back_bitreader(br);   // 0x10019591-0x100195a0
}
```

**Correction to §5.3c's own framing: the `0xc` comparison (`cmp bl,0xc` at `0x100194c4`) is on the
*translated* output value `bl` (post-permutation-table lookup), not the raw Huffman symbol** — the
whole-call `mode_bit` (flat memset-fill vs. real Huffman expansion) and the per-symbol literal/
run-length dispatch are two separate, different-level decisions; conflating them produces wrong code.

**Correction (2026-10-08, fresh live re-verification, agent `afbe23adb9f56a453` — supersedes the
original framing of this cross-check): the real LEN for Y-plane frame 0's first block-row (descriptor A)
is 528 (0x210), not 350.** Confirmed three independent ways: live EDX capture at `0x100193d4`
immediately after the LEN extraction at `0x100193c0`-`0x100193d1`; independent hand-reconstruction from
the real sub-chunk bytes at bit position 588 (sub-chunk-relative bytes 73/74, LSB-first bit assembly,
confirmed against the live capture — the opposite MSB-first assembly gives 33 and is empirically wrong);
and the descriptor's own `bound - SCRATCH_BASE` after the call (`SCRATCH_BASE = 0x303fdc90` for this
invocation). `A=6` (max Huffman code length), `N=10` (LEN field width), `CAP=752` (LEN capacity,
528<752 so the poison/"-4 stall" path does not fire here). The real 528-byte decoded output has
SHA-256 `34263c04fe17f62ff38111e6a00bbdc4b8ce28a278802407124a8589ffcdf0b3`.

The published `case_selector_scratch_frame0.bin` (350 bytes, SHA-256 `9839d7171e81c0d2b0e671679fe51c25ed2f24460330244a265635580b7b6eef`) is **not** this call's byte-0 output — it is byte-for-byte identical to `real[178:528]`, a 350-byte suffix (confirmed by exact-offset search over the real 528-byte buffer: exactly one match, at offset 178; 528-178=350). Root cause: the capture hook in `capture_stages.py` sat at address `0x1001aab0`, which is not the real call's return address (the real `call 0x10019360` is at `0x1001aab2`, real return `0x1001aab7`); that wrong hook address actually fires 271 times over the full decode (once per entry into `0x1001a530`, i.e. once per block-row per plane) and the one firing that passed the script's own `base != 0` filter was row 1 of Y observing row 0's own already-178-bytes-drained leftover state in the reused `[ebp-0x170]/[ebp-0x16c]` stack slots — hence `bound-base == 350` for that (wrong) capture.

The vector itself is still legitimately-captured real DLL output and remains usable as a test vector — downstream reimplementations should compare their own decode's bytes 178-527 against it (not bytes 0-349), or recapture a fresh byte-0-anchored 528-byte vector by fixing the hook address. **The separately-reported "23 vs 25 calls" question is NOT an error**: a dedicated fresh re-trace confirms 23 is correct call-for-call (seq1→seq2→entry→16 internal loop iterations→1 trailing internal call→4 direct calls, landing the LEN read at bit 588); a downstream team's "25" count was most likely an off-by-2 slip in where counting started relative to an intermediate checkpoint, not a real extra consumption. A separately-reported "divergence at byte 12" in downstream decodes is also fully explained as a consequence of this same offset error (comparing against the wrong 0-anchored reference window), not a decode bug — per-symbol tracing at the true byte 11/12 boundary shows nothing structurally special there.

**`0x10018c20` (`0x10018c20`-`0x10018c94`), the bit-selected 2-stream merge primitive used throughout
`0x10018ca0`'s table construction:**

```c
// merges N bytes from Q and N bytes from P into OUT, one bit of V per output item (LSB-first via
// shr), 1=take from P, 0=take from Q; once either stream exhausts, the rest of the OTHER stream is
// appended verbatim without consuming more of V. Returns bits of V actually consumed.
uint32_t merge(uint32_t V, uint8_t* Q, uint8_t* P, uint8_t* OUT, uint32_t N) {
    uint32_t p_left = N, q_left = N, bits_consumed = 0;
    while (p_left && q_left) {
        bits_consumed++;
        if (V & 1) { *OUT++ = *P++; p_left--; } else { *OUT++ = *Q++; q_left--; }
        V >>= 1;
    }
    while (q_left) { *OUT++ = *Q++; q_left--; }   // 0x10018c60 drain
    while (p_left) { *OUT++ = *P++; p_left--; }   // 0x10018c80 drain
    return bits_consumed;
}
```

**`0x10018ca0` (`0x10018ca0`-`0x100192f8`, confirmed by its own stack-cookie prologue/epilogue), the
table-build dispatcher — prior units had only reached ~230 of its real ~1600+ instructions. 5 distinct
construction strategies, selected by a 4-bit nibble plus a 1-bit flag plus a 2-bit mode:**

```c
void build_table(BitReader* br, uint8_t* table16, uint32_t* LUT_A_ptr_out, uint8_t* classbyte_out) {
    uint32_t nibble = read_bits(br, 4);                      // 0x10018ccd-0x10018cfa
    *LUT_A_ptr_out = 0x1003bd60 + OFFSET[nibble];              // 0x1003c140[nibble], dword table
    *classbyte_out = CLASSBYTE[nibble];                         // 0x1003bd44[nibble]
    if (nibble == 0) { for (i=0;i<16;i++) table16[i]=i; return; }   // 0x10018d1b: trivial identity
    uint32_t flag = read_bits(br, 1);                          // 0x10018d30-0x10018d4b
    if (flag == 1) { build_explicit_prefix(br, table16); return; }  // 0x1001922e, strategy 5, below
    uint32_t mode = read_bits(br, 2);                          // 0x10018d51-0x10018d86
    uint8_t temp[16];
    for (i = 0; i < 16; i += 2) {                                // 0x10018d88-0x10018de5: ONE bit
        uint32_t bit = read_bits(br, 1);                          // per adjacent pair
        if (bit==0) { X(i)=i; X(i+1)=i+1; } else { X(i)=i+1; X(i+1)=i; }
        // X = table16 directly if mode==0, else X = temp (16-byte scratch)
    }
    if (mode == 0) return;
    uint8_t level1[4][4];
    for (g = 0; g < 4; g++) {                                   // 0x10018e39/e84/ed2/f20, each a
        uint32_t V3 = read_bits(br, 3);                           // fresh 3-bit field per group
        merge(V3, &temp[4*g], &temp[4*g+2], (mode==1 ? &table16[4*g] : level1[g]), /*N=*/2);
    }
    if (mode == 1) return;
    uint8_t level2[2][8];
    uint32_t V7a = read_bits(br, 7);
    merge(V7a, level1[0], level1[1], (mode==2 ? &table16[0] : level2[0]), /*N=*/4);  // 0x100190d1
    uint32_t V7b = read_bits(br, 7);
    merge(V7b, level1[2], level1[3], (mode==2 ? &table16[8] : level2[1]), /*N=*/4);  // 0x1001911f
    if (mode == 2) return;
    uint32_t V15 = read_bits(br, 15);                            // 0x7fff-masked, confirmed 15 bits
    merge(V15, level2[0], level2[1], table16, /*N=*/8);           // 0x10019205, mov edx,8 before call
}
```

A clean 4-level **bit-reversal-style merge network**: level `k` (0-indexed) consumes a `(2^(k+1)-1)`-bit
selector (1/3/7/15 bits); `mode` picks how many levels deep to go. **`classbyte == A` (blocker-1's own
max-code-length field), CONFIRMED by exact address cross-reference, not naming inference**: `0x1001a530`'s
first `0x10018ca0` call (`0x1001a9db`) passes `table16_out=&[ebp-0x160]`, `classbyte_out=&[ebp-0x150]`,
`LUT_A_ptr_out=&[ebp-0x14c]` — and the same row's entropy-prestage `desc=&[ebp-0x170]` has `desc+0x10 =
[ebp-0x160]`, `desc+0x20 = [ebp-0x150]`, `desc+0x24 = [ebp-0x14c]`, an exact match on all three fields.
This confirms `0x1003bd44`'s 16 published bytes (`04 05 05 05 05 05 05 06 06 06 06 06 06 07 07 07`,
§5.3c) literally **are** "max Huffman code length per nibble value, in bits."

**5th construction strategy, `build_explicit_prefix` (the `flag==1` branch, `0x1001922e`-`0x100192dd`),
reached BEFORE the 2-bit `mode` field is even read — a genuinely separate strategy, not a variant of
modes 0-3:**

```c
void build_explicit_prefix(BitReader* br, uint8_t* table16) {
    uint32_t shift = read_bits(br, 3);                  // 0x1001922e-0x1001925e
    uint16_t used_mask = 0xFFFF;
    for (i = 0; i <= shift; i++) {                        // runs (shift+1) times, 1..8
        uint32_t value = read_bits(br, 4);
        table16[i] = value;                                 // 0x100192b8: sequential position i
        used_mask &= ~(1 << value);
    }
    uint32_t pos = shift + 1, candidate = 0;              // 0x100192c3-0x100192db
    while (true) {
        if (used_mask & 1) { table16[pos++] = candidate; if (pos > 0xF) break; }
        candidate++; used_mask >>= 1;
    }
}
```
An explicit prefix (1-8 positions, each an arbitrary 4-bit symbol value), then the unused symbol values
filled in ascending order — cheaper than the full merge network for skewed permutations.

**New, previously-unnamed function `0x10019300`-`0x10019359`:** loops 16× over `0x10018ca0` (building a
16-entry sub-table array), plus one more call; called once per block-row from `0x1001a530`
(`0x1001aa11`), interleaved between its 2nd and 3rd direct `0x10018ca0` calls — downstream consumer not
traced this pass, flagged OPEN below.

**Call granularity, CONFIRMED empirically (live Unicorn execution, de-duplicated against a Unicorn
TB-cache double-firing artifact — see below): `0x10019360` fires exactly once per 8-pixel block-row, per
descriptor** (`0x1001a530` has exactly 2 real call sites, `0x1001aab2`/`0x1001aacf`, against 2 sibling
output descriptors — "A" = §5.3c's own `[ebp-0x170]`, "B" = a second, previously-undocumented sibling
stream at `[ebp-0x1a4]`, built/expanded identically, downstream consumer not traced, flagged OPEN):

| plane | height | height/8 | A total entries | A genuine refills | B total entries | B genuine refills |
|---|---|---|---|---|---|---|
| Y | 1080 | 135 | 135 | 41 | 135 | 9 |
| chromaA | 540 | 68 | 68 | 12 | 68 | 3 |
| chromaB | 540 | 68 | 68 | 1 | 68 | 1 |

`total_entries` matches `height/8` (rounded up) exactly for every plane, both descriptors — confirming
"once per 8-pixel block-row" as the real granularity. The idempotency guard means most calls are no-ops;
a genuine refill happens roughly every 3-4 rows for Y's descriptor A, consistent with §5.3f's own
oscillating-cycle-of-3 `avail` telemetry. **This fully reconciles §5.3f's own raw call-count figures**
(Y=540, chromaA=272, chromaB=272): those equal `block_rows × 2 descriptors × 2` — the same Unicorn
TB-cache double-firing artifact this pass's own probe had to filter out (a 1-byte-wide `UC_HOOK_CODE`
range at `0x1001aab0`, not a real instruction boundary, fires twice per real call; this pass's clean
recount discards any firing whose `dword[esp]` doesn't exactly equal one of the 2 real return addresses,
`0x1001aab7`/`0x1001aad4`) — §5.3f's own numbers were not wrong, just counted at 4× this granularity.

**Real starting bit position, measured directly (refutes the "skip earlier rows" hypothesis with a
decisive row-0 argument):** for each plane's row 0, the first genuine `0x10019360` call's LEN read
begins (counting from each plane's own sub-chunk data-start, i.e. past its own 4-byte length prefix) at
Y=588 bits, chromaA=262 bits, chromaB=701 bits. **Since this gap exists at row 0 — by definition with
zero earlier rows to skip — "skip-only rows need replaying first" is refuted directly, not just deemed
unlikely.** The real mechanism (CONFIRMED to the instruction): before ever calling `0x10019360` for a
row, `0x1001a530` executes a fixed sequence of **23 calls to `0x10018ca0`** (2 direct + 1 call to
`0x10019300`'s own 16-loop + 1 more + 4 more direct calls) building tables for other, sibling per-row
sub-streams (motion-vector-adjacent and/or other alphabets, not traced further — OPEN) — **all 23 share
the identical sequential bit-reader cursor** the case-selector pre-stage itself reads from. Live-traced
bit position climbs monotonically 0→32→64→(16 sub-calls)→528→...→588, exactly 23 calls' worth of
content-dependent consumption, landing exactly at the measured mark. **This is why the position differs
per plane (588/262/701) rather than being a fixed protocol constant: each of the 23 calls' own bit cost
depends on a randomly-rolled nibble/mode value** (identity=4 bits, pairwise-swap-only≈15 bits, up to the
full merge network≈39 bits) — itself a function of that plane's real compressed content. **There is no
universal "LEN starts at bit N" constant to scan for; it is a derived quantity, obtainable only by
actually decoding everything that precedes it in program order from bit 0 of the sub-chunk's own data
start.** Flagged as a second, independently testable candidate for a zero-hit scanner: this pass's own
LSB-first bit-order finding (above) — an MSB-first-assuming scanner would never decode a correct value
at any position, independent of the row-skip question entirely.

**Full call-by-call re-trace, 2026-10-08, in response to Team B's third independent negative at this
exact location (their own reimplementation measured 580 bits before LEN, not 588, and read LEN as 0,
not 528 — on the identical file, identical offset, identical 16-byte vector, with their own bit reader
independently confirmed cold two ways including the unmodified production class).** A completely fresh,
instruction-level Unicorn trace (not a re-read of any prior pass's own output) re-executed all 23 calls
individually and measured every one of their entry/exit bit positions and every internal field read.
**Result: this project's own existing 588-bit/LEN=528 figure reproduces exactly, call for call** — the
23 calls' own measured costs (32, 32, 4, 24, 24, 46, 46, 32, 32, 40, 40, 16, 32, 32, 32, 32, 32, 4, 4, 40,
4, 4, 4) sum to exactly 588 with zero gaps or overlaps, independently cross-checked against the raw file
bytes for call #1's own first three fields (nibble=12, flag=1, shift=5, hand-derived directly from the
16-byte test vector above using only the already-agreed LSB-first convention). Nothing outside the 23
calls contributes to the 588-bit total: bit 0 is confirmed the bit reader's own cold start with no
leading alignment/pre-read, and call #23's own exit lands exactly on the real `0x10019360` LEN read's
own entry, with zero gap, confirmed via two independent hooks.

**A genuine, previously-under-specified subtlety was found in the merge-network strategy — the one
place in all 23 calls where a reimplementation could plausibly drift.** Calls #6 and #7 (both
full-depth merge-network, `mode==3`) are the *only* two of the 23 whose bit cost is not a fixed function
of {nibble, flag, shift} — the other 21 (6 identity, 15 explicit-prefix) have a bit cost fully determined
by their own small field set. Each merge group's selector field is only a **maximum peek width** (3 bits
for the 4 level-1 groups, 7 bits for the 2 level-2 groups, 15 bits for the 1 level-3 group); the function's
own merge primitive (`0x10018c20`) returns the *actual*, data-dependent number of bits it consumed from
that peek, which can be smaller, and the caller advances the bit reader by exactly that returned amount —
not by the nominal field width. In this exact trace, call #6's two level-2 merges consumed 7 and 5 bits
(not the nominal maximum 7+7=14); call #7's consumed 7 and 5 as well (numerically identical by
coincidence of the data, not a cached value — the underlying selector values, 120 vs 116, differ). An
"always consume exactly the nominal maximum width" OR an "always consume exactly the per-side item count"
(4 for a level-2 group) reading of these same two groups would silently drift by a fixed amount on every
file, concentrated entirely in calls #6/#7.

**This directly explains both of Team B's reported numbers as one coherent symptom, not two.** The real
file bytes at raw bit positions 576-591 (sub-chunk-relative bytes 72-73) are a 16-bit all-zero run,
confirmed directly from the shipped bytes. Re-extracting the real 10-bit LEN field starting at bit 580
(Team B's own stopping point) decodes to exactly **0** on these exact bytes; starting at the true bit 588
decodes to **528**. **Any table-build trace that comes up a few bits to 8 bits short by the time it
reaches the real LEN read lands inside this same zero run and reads LEN=0** — this is a plain arithmetic
fact about the shared file bytes, not a theory. The "always consume the per-side item count" reading of
calls #6/#7's own level-2 groups would give 4+4=8 per call instead of the real 7+5=12, a 4-bit shortfall
per call — 8 bits across exactly those 2 calls, matching Team B's own reported shortfall (588−580=8)
exactly in both magnitude and the fact that only 2 of the 23 calls can even produce this kind of error.
**Offered as the best-supported, most concrete hypothesis for where Team B's own 8-bit shortfall
originates — not confirmable from this side of the clean-room boundary, since this project does not read
Team B's own code.** If Team B's own merge-group bookkeeping already reflects the real returned
consumed-bit count (not a fixed nominal/minimum width) specifically for calls #6/#7, that rules this
hypothesis out and the remaining search has nowhere else to look within this specific 23-call sequence —
no other data-dependent branch point exists in it.

**Part (d), closed: exactly 1 of the 23 calls (call #1, site `0x1001a9db`) feeds descriptor A** —
confirmed by matching its own output-pointer arguments (table16-out/classbyte-out/LUT-pointer-out)
exactly against descriptor A's own `+0x10`/`+0x20`/`+0x24` fields. Call #2 feeds descriptor B; calls
#3-18 (the 16-loop body) feed a separate 16-entry sub-table array; call #19 (the loop's own "+1 more"
call) feeds a further distinct structure; calls #20-23 each feed one of 4 more mutually-distinct,
single-call scratch structures. None of the other 22 touch descriptor A — consistent with, not a
reopening of, this section's own standing "23 sibling calls... downstream consumers not traced" item
for the other 5 structures. Full per-call field-by-field detail (every nibble/flag/mode/pairwise-bit/
explicit-value/merge-group-selector-and-real-consumed-count, in order, for all 23 calls) and the raw/JSON
trace output for direct diffing: `scratchpad/bink_5_3g_call_by_call_trace_2026-10-08.md`,
`scratchpad/bink_call_by_call_trace_2026-10-08/`.

**Implementer note, 2026-10-08 (for anyone reproducing this against a byte-exact `case_selector_
scratch_frame0.bin` match and landing at chance-level agreement despite a correct algorithm):** the
588/262/701-bit figures above are counted from each plane's own sub-chunk data START — i.e. already
past that sub-chunk's own 4-byte length prefix — not from the overall frame blob's own start, and not
from any other reference point. A reimplementation whose own "sub-chunk start" candidate is anchored to
a different point in the file (e.g. the frame-blob start rather than this specific sub-chunk's own
post-length-prefix start) will be off by a few hundred bits even with an otherwise bit-exact algorithm
and the correct LSB-first convention — worth checking as the first, cheapest diagnostic before
suspecting the algorithm itself.

**The absolute number, pinned down 2026-10-08 (fresh live Unicorn trace, two independent register/
memory read paths agreeing exactly): for `main_menu_bg.bik`'s own frame 0, Y-plane's sub-chunk data
start (the "588" reference point above) sits at byte offset 4 relative to frame 0's own raw blob —
absolute file offset `0x2910` (10512 decimal), i.e. frame-0-blob start (`0x290c`, §1.6) plus 4.** What
precedes it is exactly one 4-byte little-endian field (blob bytes 0-3 = `74 0f 02 00` = 135028
decimal) — not a separate plane header, not another plane's own data, not alignment padding. HIGH
CONFIDENCE (triangulated via a second live measurement at chromaA's own first dispatch landing at
blob offset 135028 exactly = 135024 [Y's own sub-chunk span] + 4 [this same prefix]; the field's
actual consumer instruction was not traced): this leading 4-byte field is Y's own stored sub-chunk
span / skip-to-next-plane offset. The bit-reader's cursor is exactly byte- and word-aligned at this
exact point, with zero bits pre-cached (`accum=0`, `bits_avail=0`) — a clean cold start. **16-byte
test vector at this exact offset** (`main_menu_bg.bik` absolute file offset `0x2910`, real shipped
data, confirmed identical whether read from the in-memory blob or fresh from the real file on disk):
`bc 15 86 9c bc 65 98 dc 80 17 e2 83 27 31 a4 0e`. Nothing else precedes Y's own sub-chunk in the
frame-0 blob — Y is the first of the four `0x1001a530` calls (§5.6), so there is no earlier plane's
data to consume first. Full detail: `scratchpad/bink_y_plane_subchunk_start_offset_2026-10-08.md`.

**Bonus, closes §5.3f's own named next-step: the chromaB "-4 permanent stall" mechanism, now identified
to the instruction.** §5.3f measured `avail` going to exactly `-4` on chromaB's 2nd `0x10019360` call and
never recovering. That exact path is the LEN==0-or-over-capacity branch above:

```
0x10019586: mov eax, [ebx+0x30]      ; eax = SCRATCH_BASE
0x10019589: lea edx, [eax+4]
0x1001958c: mov [ebx], edx            ; desc->cursor = SCRATCH_BASE + 4
0x1001958e: mov [ebx+4], eax          ; desc->bound  = SCRATCH_BASE   (avail = bound-cursor = -4)
```
reached whenever decoded `LEN` is 0 or exceeds capacity, **and also** whenever a refill inside the main
expansion loop hits `stream_end` (`0x1001946a`-`0x1001946d`, same target). Once set, the next call's own
idempotency guard sees `cursor != bound` permanently and takes the no-op path forever — exactly
reproducing the "-4, -4, -4, ... forever" telemetry. **What remains OPEN, named precisely: why chromaB's
real decoded `LEN` evaluates to 0/over-capacity starting from its 2nd call specifically** — a
content/upstream question (the actual bit values chromaB's row produces at that point), not re-chased
this pass, likely connected to the same sub-chunk-boundary computation §5.3f already flagged as the
probable shared root with the DC +128 bug.

**[CONFIRMED — every bit consumed, table lookup, and byte write in `0x10019360` traced to the
instruction and live-cross-checked against the published 350-byte test vector (now known to be
`real[178:528]` of a 528-byte call output, not a byte-0-anchored 350-byte output — see the
2026-10-08 correction above); `0x10018c20`'s merge
primitive fully specified; all 5 of `0x10018ca0`'s construction strategies traced to the instruction,
`classbyte==A` confirmed by exact address cross-reference; call granularity confirmed empirically
(height/8, both descriptors, all 3 planes); real bit position measured per-plane and the row-skip
hypothesis refuted by direct row-0 measurement; the chromaB stall mechanism identified to the
instruction. OPEN: descriptor B's own downstream consumer; the 23 sibling calls' own downstream
consumers; whether the bit-order finding is the (or a) cause of a scanner's own zero-hit result (cannot
confirm without seeing that scanner's code); why chromaB's real bit values specifically produce an
invalid LEN from its 2nd call onward.]** Full detail:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08.md`.

### 5.3h Descriptor B's own downstream consumer, traced live — closes the OPEN item above, and finds
the real block-(0,0) dispatch sits 7 setup stages and 10,716 bits further downstream than either
descriptor's own refill call

**Scope.** Direct follow-up, live-execution trace (Unicorn emulation of the real, unmodified
`binkw32.dll` against the real, unmodified `main_menu_bg.bik`, the same method §5.3g itself used), at
Y-plane row 0, from the exact instant descriptor A's refill returns (cumulative bit position 1757,
same counting convention as this section's own "588" figure).

**Descriptor B's refill fires unconditionally at bit 1757 — confirmed directly, not inferred.** Exactly
7 instructions, all pure register/stack argument marshaling for `0x10019360(desc_B, bitreader,
stream_end)`, sit between A's refill returning (`0x1001aab7`) and B's refill being called
(`0x1001aacf`) — zero conditional branches anywhere in between. This extends this section's own
23-sibling-table-build "fires unconditionally regardless of consumption" finding to descriptor B's
refill call too.

**Descriptor B's own LEN-field bit-width is NOT computed by the literal same formula as descriptor
A's — confirmed by tracing both to their own setup-code instructions, not assumed.** `N_A =
bit_length(Width/8 + 0x1ff)` (as already published above); `N_B = bit_length(Width/16 + 0x1ff)`, built
from `Width>>1>>3` rather than `Width>>3` again. For `main_menu_bg.bik`'s `Width=1920`, A's own input
is 751 and B's is 631 — different numbers that happen to both fall in the `[512,1024)` power-of-two
bracket, so `bit_length` gives 10 for both — **the same final N from a genuinely different formula,
not the same formula**. A narrower or differently-proportioned file could plausibly see `N_A != N_B`.
(`CAP_A = round_down16(Width/8+0x20f) = 752`; `CAP_B = round_down16(Width/16+0x20f) = 640` — both
independently re-confirmed live.) Live-traced for this exact row: `LEN_B = 530` (`0x212`),
`mode_bit_B = 0` (real Huffman table-driven expansion, not the flat-fill path), decoding 530 real
bytes into descriptor B's own SCRATCH buffer; B's refill call spans bit 1757 (entry) to bit 2871
(exit).

**Neither of two plausible raw-bit checkpoints at bit 1757/2871 is actually a sub-dispatch read — both
are artifacts of unrelated fields landing at those exact bit positions, confirmed by checking the real
file bytes directly.** The first 4 bits consumed starting at bit 1757 are simply the low 4 bits of
descriptor B's own 10-bit LEN field (`LEN_B = 530 = 0b10_0001_0010`, LSB-first first 4 bits = value 2)
— not a sub-dispatch decision; nothing resembling one happens at bit 1757. The first 4 bits starting
at bit 2871 (the instant B's refill call *returns*, not the instant block (0,0) is dispatched) belong
to a wholly separate, unrelated per-row decoder reached immediately afterward — their value (10) is an
artifact of that routine's own unrelated algorithm, not a sub-dispatch value either.

**Between B's refill returning and block (0,0)'s own real case-body bit read, 7 further per-row setup
stages consume 10,716 more raw bitstream bits, none of them case-selector- or sub-dispatch-specific:**
one indirect-call per-row bit-window refill (one of two interchangeable routines selected once per
row), then 6 further named/semi-named calls — including a large (4,307-bit) DC-delta pre-decode pass
consistent with bulk-decoding DC values for every block column in the row up front, not per block.
Every one of these 7 stages' bit costs and call targets are pinned; their own downstream semantic
roles are a new, bounded OPEN item (not chased further this pass).

**The real block-(0,0) dispatch, fully traced:** the primary case-selector byte (confirmed value 1)
and the secondary/sub-dispatch byte (confirmed value 5, `-3` → 2, matching the already-documented
"byte minus 3" formula) are each a zero-bit-cost memory read from descriptor A's and descriptor B's
own pre-decoded SCRATCH buffers respectively — never a fresh bitstream read — extending this section's
own "decoded once per row, consumed per block with no further bit cost" finding (previously shown only
for the primary case-selector) to the secondary/sub-dispatch field too. Case 1 → secondary 2 jumps to
`0x1001b2ff`, already named elsewhere in this document as the secondary=2 DC+AC/IDCT engine — directly
confirmed here, live, as the real destination for this specific block. The DC literal (118) is a
further zero-bit-cost read from a pre-decoded per-row DC array; the first genuinely new raw-bitstream
read belonging to block (0,0)'s own content is the AC-coefficient tree decode at `0x10024eb0` (17
bits), followed by an inline 4-bit quantizer-select nibble and a pure-arithmetic IDCT call (zero
further bitstream reads).

**Net:** descriptor B's refill call belongs exactly where hypothesized; the bit a naive "4 fresh raw
bits right after B's refill" assumption would read next does not exist as a sub-dispatch value — the
real dispatch is reached 7 setup stages and 10,716 bits later, through zero-bit-cost SCRATCH-buffer
reads from both descriptors, not a fresh bitstream read at that point in the sequence at all.

**[CONFIRMED — live execution, this pass: B's refill timing/LEN/mode-bit/exit bit position, the 7
per-row setup stages and their bit costs, the real primary/secondary dispatch values and jump target,
the DC literal and AC-tree bit cost. CONFIRMED — disassembly, this pass: the N_A/N_B/CAP_A/CAP_B
formulas, cross-checked live. OPEN: the 4 newly-pinned per-row setup functions' own semantic roles;
why N_A and N_B use a different halving factor at all.]** Full detail:
`scratchpad/bink_d10_and_bit1757_trace_2026-10-09.md` Part 2.

### 5.3i Primary case 1's full 7-way secondary payload grammar, all values 0-6, closing Team B's own named blocker (2026-10-10)

**Scope and why this is its own subsection, not folded into 5.3c.** 5.3c/5.3d/5.3g/5.3h establish the
entropy *pre-stage* that supplies the primary and secondary case-selector bytes themselves (descriptors
A and B) and, in 5.3h's own closing paragraph, trace ONE real block (`main_menu_bg.bik` frame 0, Y-plane,
block (0,0)) as far as "a pure-arithmetic IDCT call (zero further bitstream reads)" without opening that
call's own argument/output grammar. This subsection is the direct continuation Team B asked for: a
byte-level payload grammar, in the same style as 5.3's own TwoLiteralPattern (secondary=5) entry, for
EVERY one of the 7 secondary values — reusing the already-built Unicorn harness
(`scratchpad/bink_unicorn_harness_2026-10-08/bink_harness.py`, `decode_frame0.py`) and the
TwoLiteralPattern-era scan/trace scripts as a direct template. New scripts for this pass live in
`scratchpad/bink_sub012346_grammars_2026-10-10/`.

**A methodology correction, found and fixed before any of the results below were trusted.** The
TwoLiteralPattern-era scan script (`scan_for_sub5_mmbg.py`) and this pass's own first two scan attempts
(copied from it) pass the top-level `0x1001cb00` call's `[ebp+8]` in/out-scratch argument as literal `0`
— which `decode_frame0.py`'s own comment already documents as forcing `[ebp+0xc]` (macroblock column
count) to zero as a side effect, silently making the Y-plane call (call 1) process **zero** columns
every time. The buggy scripts' own distribution counts are consequently accidental chromaA-only data,
not Y+chromaA as assumed. Every live result below was re-run (`scan_v2_sub0_sub2.py`) against the
correctly-configured harness setup (`decode_frame0.py`'s own known-good `call_bink_cb`, non-null
`SCRATCH_DWORD`) before being reported as CONFIRMED. One live cross-check this correction itself
produced: the corrected Y-plane dispatch-reach count for `main_menu_bg.bik` frame 0 (23,914 real
dispatches) and chromaA's (6,042) **independently reproduce §5.3h's own already-published figures
exactly** (§5.3h: "23,914/23,914 attempts; chromaA 6,042/6,202"), confirming the fix is right, not just
plausible.

**A new structural finding, confirmed by live register inspection, underlying several of the grammars
below: a THIRD independent per-row byte cursor, distinct from descriptors A (primary selector) and B
(secondary selector).** Every literal-byte read traced this pass and in the original TwoLiteralPattern
pass shares the identical cursor/bound pair, `[ebp-0x210]` (cursor) / `[ebx+0x10]` (bound), checked by
the idiom `ecx = [ebx+0x10] - [ebp-0x210]; cmp ecx,0x10; jb <bail>` immediately on entry to the
secondary=2, secondary=3, and (per the original 2026-10-09 trace) secondary=5 case bodies alike. This is
a SEPARATE descriptor from both A (`[ebp-0x170]`/`[ebp-0x16c]`, 5.3c) and B (`[ebp-0x1a4]`/`[ebp-0x1a0]`,
5.3c's "Extended 2026-10-09" paragraph) — call it **descriptor L** ("literal") pending a real name.
**CONFIRMED: descriptor L feeds the DC literal word (secondary=2), the flat-fill byte (secondary=3), and
both pattern-literal bytes (secondary=5) — i.e. every "raw literal value" read by any primary=1 case
body comes from this one shared byte stream.** OPEN, not chased this pass: whether descriptor L is
itself filled by its own `0x10019360`-style pre-stage call (by analogy to A and B) or reads raw
un-decoded bitstream bytes directly — not traced upstream of its own cursor/bound pair this pass.

**A second new structural finding, also confirmed by live register trace: primary=1's "mark skip, no
pixel write" behavior is reached by an early flag-bit short-circuit, BEFORE the secondary byte is ever
read — not via the secondary byte's own value 1 or 4 as the dispatch table's literal shape might
suggest.** Traced live (dispatch index 178 of `main_menu_bg.bik` frame 0's Y-plane; see secondary=1/4
below) — this corrects the framing in §5.3's own case table, which lists this behavior under "secondary
1 or 4" as if it were one of the 7 sub-dispatch targets. It is not reached that way in this real sample;
see below for the actual confirmed mechanism and an open question about whether raw secondary bytes 4/7
(→ sub 1/4) are ever produced by this encoder at all.

**A third, unifying finding across three independently-traced case bodies: secondary=2, secondary=3,
and secondary=5 (TwoLiteralPattern, already published) all write a physical 16-column x 16-row (256
pixel) output block, not an 8x8 block.** This was previously flagged for secondary=5 alone as "does NOT
match the Width/8 macroblock-column convention... block-type-specific." Live evidence this pass shows
the SAME 16x16 footprint for secondary=2 (confirmed by exact write-address enumeration, below) and
secondary=3 (confirmed likewise). This is now a block-type-FAMILY-wide convention for primary=1's own
new-content sub-cases specifically, distinct from the real 8x8 IDCT blocks used by primary cases 5/7
(5.3b).

#### secondary=2 — third DC+AC dequant/IDCT engine (`0x1001b2ff`-`0x1001b3d2`), CONFIRMED by live execution

**Occurrence rate, corrected scan:** abundant and easy to find — 2,457 real occurrences in
`main_menu_bg.bik` frame 0 alone (corrected Y+chromaA scan), including the very FIRST dispatch of the
entire frame (global dispatch index 0 = Y-plane block (0,0)) — independently corroborated by §5.3h's own
unrelated trace of the identical block, which stopped short of opening this call's own grammar.

**Byte-level read/write sequence, CONFIRMED live (`main_menu_bg.bik` frame 0, Y-plane block (0,0)):**

1. `0x1001b2ff`-`0x1001b30b`: bound check on **descriptor L** (`[ebx+0x10]`(bound) `-`
   `[ebp-0x210]`(cursor) `>= 0x10`) — bails to the generic end-of-row handler (`0x1001acc9`) if fewer
   than 16 bytes remain in the shared literal stream.
2. `0x1001b311`-`0x1001b33b`: reads a **16-bit little-endian DC literal word** directly from descriptor
   L's cursor (no bit-packing — a plain byte-pointer read/advance, `cx = word[eax]; eax += 2`), writes
   it into the shared per-block coefficient struct at `[ebp-0x310]+0` (the same struct §5.3b already
   names for cases 5/7's own DC+AC engine). **Live value: `0x0076` (118)** — exact match to §5.3h's own
   independently-traced DC value for this identical block, cross-confirming both passes.
3. `0x1001b348`: `call 0x10024eb0(hidden-ecx=bitreader-context, stack-arg=&[ebp-0x310])` — the
   already-documented (§5.3b) AC-coefficient breadth-first tree decoder, filling the struct's remaining
   63 coefficients (row-major, `struct_byte_offset(k)=2k`, per §5.3b's own closed finding) from the
   MAIN per-row bitstream (a real, bit-costly read — 17 bits for this specific block, per §5.3h).
4. `0x1001b34d`-`0x1001b3b5`: an **inline 4-bit nibble read** from the main bit-reader's own shared
   accumulator (two converging code paths depending on whether the current accumulator holds >=4
   unconsumed bits or needs a refill first — both paths land on the identical `edx = nibble (0-15)` at
   `0x1001b3b5`). **Live value: `2`.** This selects a row of the SAME 16-row x 256-byte quantization
   matrix (`0x10037cc0`) that primary cases 5/7 use (§5.3b): `quant_row_ptr = 0x10037cc0 + nibble*256`.
   **Live cross-check: `quant_row_ptr - 0x10037cc0 = 0x200 = 512 = 2*256` — an exact arithmetic match to
   the live nibble value, confirmed by direct register read, not computed after the fact.**
5. `0x1001b3c8`: `call 0x100247d0(hidden-ecx=PITCH, hidden-edx=quant_row_ptr, [ebp+8]=dest_ptr,
   [ebp+0xc]=&[ebp-0x310] struct)` — the third, "direct-store" sibling of the `0x100244c0`/`0x10024b50`
   two-stage fixed-point 8-point IDCT pair (§5.3b). **Live register values: hidden-ecx = `1920` (exactly
   `Width` for this file — an independent confirmation this really is the plane pitch, not a block-local
   stride); `[ebp+8]` = `0x42001000` = this harness's own `Y_BUF` exactly (block (0,0)'s own dest base,
   zero offset, consistent with it being block index 0).** **This is a NEW, directly-register-confirmed
   finding, not previously resolved: `0x100247d0`'s own calling convention is `(pitch, quant_row_ptr |
   dest_ptr, coeff_struct_ptr)` — DIFFERENT from `0x100244c0`'s already-closed `([ebp+8]=dest,
   [ebp+0xc]=pitch)` convention (§5.3e). The two "sibling" IDCT entry points do not share one calling
   convention; each must be implemented to its own observed signature.**

**Output, CONFIRMED by exhaustive live write-address enumeration (916,416 raw Y-plane write events were
captured for the rest of the frame after this call; the FIRST 64 belong to this one block and were
isolated by address order):** exactly **64 dword writes** (4 per row x 16 rows), covering **columns
0-15, rows 0-15** of the Y-plane relative to the block's own base — a **16x16 pixel output block**, not
8x8. For this specific real block, the IDCT output is NOT a single flat DC broadcast (which would mean
all-zero AC): rows 0-3 are uniformly `0x18` (24) and rows 4-15 are uniformly `0x19` (25), a genuine
two-band vertical pattern with zero horizontal variation — direct, real-world evidence that the AC tree
decoder produced at least one nonzero (low-frequency, vertical-only) coefficient for this block, not
just a convenient all-zero test case.

**[CONFIRMED — live execution (Unicorn, real unmodified `binkw32.dll`), one full worked example at a
real block boundary (frame 0, Y-plane, block (0,0) of `main_menu_bg.bik`), cross-validated against
§5.3h's own independently-traced DC value (exact match) and against the nibble-to-quant-row-offset
arithmetic (exact match). Every register value quoted above was read live, not inferred. OPEN: the
exact `0x100247d0` dequantization math itself (whether it reuses the same butterfly constants as
`0x100244c0`/`0x10024b50` — structurally very likely given the shared `0x10037cc0` table and "DC-only
fast path" family resemblance already established in §5.3b, but not independently re-derived this pass);
a second worked example at a block with genuinely nonzero AC in BOTH axes (this block's own AC content,
while real, happens to be vertical-only).]** Full detail/scripts:
`scratchpad/bink_sub012346_grammars_2026-10-10/trace_sub2_block0.py`,
`disasm_sub2.py`, `mem_writes_sub2_block0.txt`.

#### secondary=0 — "giant1" RLE+permutation engine (`0x1001ada6`/`0x1001af5e`) — structural grammar carried over, but a genuine, honestly-reported NEGATIVE live-occurrence result

**What is already CONFIRMED (prior units, re-cited, not re-derived here):** `0x1001af5e` (real entry
`0x1001ada6`) is a run-length literal/replicate expansion engine, written through a 4-bit-nibble-selected
row of the already-dumped 64-byte scan-order permutation table (`0x100378c0`, full bytes published in
§5.3a above), then 2x pixel-doubled/upsampled in the same engine (§5.3a). **Primary case 3 (`0x1001bfcb`)
is this engine's own already-CONFIRMED, already-live-traced full-resolution twin** (§5.3b) — identical
64-byte permutation table, identical 64-coefficient budget, identical literal/replicate semantics, minus
the final upsample step. This gives a HIGH-CONFIDENCE (not this-pass-reverified) basis for secondary=0's
own grammar by direct analogy: read a 4-bit nibble selecting a permutation-table row, then decode a
run-length/literal-replicate stream (identical mechanism to case 3's own confirmed engine) into a
half-resolution 8x8 (or similarly-sized) coefficient buffer permuted by that row, then upsample 2x
(duplicate each decoded pixel into a 2x2 output cell) to produce the final block.

**This pass's own contribution: an exhaustive, honestly-reported search for a real secondary=0
occurrence — zero found.** A corrected (Y+chromaA, properly-configured harness) full-frame dispatch scan
was run across **main_menu_bg.bik frames 0, 1, and 2** (3 full frames, one keyframe + two P-frames,
29,956 / 38,398 / 46,420 total dispatch events respectively) plus an earlier (chromaA-only, due to the
`[ebp+8]=0` bug caught above — still a valid negative for that plane) scan across **17 further
frame/file samples**: `main_menu_bg.bik` frames 50, 300, 700, 1200, 1800, 2400, and 11 early frames
(0-11); `logo_amd_hd.bik` frames 0, 1, 30, 90, 150; `logo_thq_and_voli_hd.bik` frames 0, 1, 30, 100, 200,
300 — Team B's own two named files. **Secondary=0 occurred in NONE of these 20 total frame instances,
across both a high-bitrate background loop and two logo/bumper videos, keyframes and P-frames alike.**
This is a real, reproducible negative result, not an absence of looking — the identical methodology
found secondary=2 (2,457x), secondary=3 (1,284x), secondary=5 (183x, original pass), and secondary=6
(258x) without difficulty in the very same frames. **Honest interpretation: secondary=0 may be a real
but rare code path in this game's actual shipped content (its own full-resolution twin, primary case 3,
was similarly never observed by this pass's own scans either, despite being independently confirmed
live-traced by an EARLIER unit — meaning it does occur somewhere, just not in any of these 20 samples),
or it may be dead/legacy-only code this specific encoder configuration never emits. Team B should treat
this case as structurally well-understood and safe to implement by analogy to case 3's confirmed engine,
but should NOT expect to find abundant real-world test material for it the way secondary=2/3/5/6 offer.**

**[HIGH CONFIDENCE, not independently live-reverified this pass: the byte-level grammar, by direct
structural analogy to primary case 3's own already-CONFIRMED, already-live-traced identical engine
(§5.3b) minus the 2x upsample. CONFIRMED, this pass: the negative occurrence result across 20 real
frame/file samples (listed above), via a corrected, properly-configured harness. OPEN: a genuine live
worked example for secondary=0 specifically (not found this pass, not fabricated); whether it occurs
anywhere at all in this game's real shipped `.bik` corpus (bounded, not exhaustive — only 3 files and 20
frames checked); the exact half-resolution block output dimensions before the 2x upsample (not measured
live for THIS entry point — case 3's own confirmed 8x8-no-upsample result is the best available proxy).]**
Full detail/scripts: `scratchpad/bink_sub012346_grammars_2026-10-10/scan_v2_sub0_sub2.py`,
`scan_multi_frame_sub0.py`, `scan_multi_file_sub0.py`.

#### secondary=1/4 — "mark skip flag, no pixel write" — CONFIRMED live, with a correction to the dispatch mechanism itself

**Live-traced mechanism (dispatch index 178, `main_menu_bg.bik` frame 0's Y-plane; properly-configured
harness):** primary=1's case body, on entry at `0x1001abfb`, does **NOT** unconditionally read the
secondary byte. It first runs a short, generic bound check (`cmp [ebp-0x218],0x10; jb <bail>` at
`0x1001ad5d`), then tests **bit 3 (`0x8`) of a per-call flags byte at `[ebp-0x210]`** — note this is the
SAME stack address descriptor L's own cursor lives at (`test byte ptr [ebp-0x210],8` here vs. the dword
read `sub ecx,[ebp-0x210]` in secondary=2/3's own bound checks), confirmed by direct address match: the
compiler is reusing this one stack slot's low byte as a flag in this early check and its full dword as
descriptor L's byte-cursor value in the case bodies that actually consume literal bytes. Not yet
resolved whether this is a real shared field (i.e. whether the flag bit is literally encoded into
descriptor L's own cursor value) or two unrelated variables that merely happen to share a stack offset
(OPEN, flagged honestly rather than guessed). **If that bit is set, execution jumps straight to a shared
epilogue at `0x1001be76` WITHOUT EVER READING THE SECONDARY BYTE AT ALL** — i.e., this is reached
BEFORE the 7-way secondary dispatch, not as one of its 7 targets. At `0x1001be76`: writes a literal `1`
byte into a per-macroblock flag array at `[per-row-flag-cursor + (stride>>1)]`, advances the flag cursor
by `[ebx+0x24]` (a per-plane macroblock-row stride), and repeats this mark-and-advance exactly **twice**
per invocation — covering what the surrounding `add edi,0x10` (16 pixels) confirms is one 16-pixel-wide
block-column, matching this subsection's own 16-wide block-family convention.

**Correction to §5.3's own case table:** the table lists this behavior under "secondary 1 or 4," framing
it as one of the 7 sub-dispatch jump-table targets reached via a raw secondary byte value of 4 or 7
(byte-3=1 or 4). Live evidence for this real sample shows the identical skip/no-write behavior reached
by an EARLIER flag-bit short-circuit that bypasses the secondary byte read entirely. **OPEN, not
resolved this pass: whether raw secondary byte values 4/7 (true sub=1/4, reached via the normal 7-way
jump table at `0x1001cae0`) are EVER actually produced by this encoder, or whether the flag-bit
short-circuit is the ONLY real path to this behavior in practice** — every "secondary=None" dispatch
event observed in this pass's own scans (5,222 of them in frame 0 alone) took the flag-bit path, and
none was observed reaching the jump-table's own sub=1/4 targets directly. Implementers should treat the
flag-bit short-circuit as the PRIMARY mechanism to implement, and the literal sub=1/4 jump-table entries
as a secondary, possibly-unreachable-in-practice fallback — not the reverse.

**[CONFIRMED — live execution, this pass, including the exact bit tested, the shared epilogue address,
the two-writes-per-16-pixel-column pattern, and the flag-array stride source (`[ebx+0x24]`). OPEN: the
literal jump-table sub=1/4 targets' own existence/reachability in real content (not observed, not ruled
out); the flag array's own downstream consumer semantics (pre-existing OPEN item, §5.6).]** Full
detail/script: `scratchpad/bink_sub012346_grammars_2026-10-10/trace_none178.py`.

#### secondary=3 — DC/flat fill (`0x1001b3d8`-...) — CONFIRMED live, with a precise correction to the output footprint

**Byte-level read/write sequence, CONFIRMED live (`main_menu_bg.bik` frame 0, Y-plane, dispatch index 1
= block (0,1)):**

1. `0x1001b3d8`-`0x1001b3e4`: the identical descriptor-L bound check used by secondary=2 (`[ebx+0x10] -
   [ebp-0x210] >= 0x10`) — direct confirmation secondary=2 and secondary=3 share the same literal-byte
   source stream.
2. `0x1001b3f8`: reads **one literal byte** from descriptor L's cursor (`movzx ecx, byte ptr [eax]`).
   **Live value: `0x99` (153).**
3. `0x1001b3fb`-`0x1001b407`: broadcasts that one byte to a full 32-bit dword by successive
   shift-and-or (`ecx |= ecx<<8; ecx |= ecx<<0x10`), confirmed live: `0x99` -> `0x9999` -> `0x99999999`.
4. `0x1001b409`-...: stores the replicated dword to **4 consecutive dwords per row** (16 bytes = 16
   pixels), then advances to the row one stride (`esi`=pitch) below and repeats — **writing BOTH the
   current row and the row directly below it on each loop iteration** (the identical "two destination
   rows at once" idiom already confirmed for secondary=5's engine), for a total of **16 rows**.

**Output, CONFIRMED by live address enumeration: exactly 64 dword writes (4/row x 16 rows) = a 16x16
pixel block, uniformly filled with the single broadcast byte value (`0x99` for this real occurrence) —
not the "~40 consecutive locations spanning a whole macroblock" the prior high-level description
estimated.** This is a precise correction, not a contradiction: the real count is exactly 64 dwords (256
bytes / 256 pixels), matching the same 16x16 footprint now confirmed for secondary=2 and secondary=5
alike.

**[CONFIRMED — live execution (Unicorn, real unmodified `binkw32.dll`), one full worked example
(`main_menu_bg.bik` frame 0, Y-plane block (0,1), fill byte `0x99`), every register value read live.
Correction, this pass: output footprint is exactly 16x16/64 dwords, not an approximate "~40" count.]**
Full detail/script: `scratchpad/bink_sub012346_grammars_2026-10-10/trace_sub3_idx1.py`.

#### secondary=5 — TwoLiteralPattern — already DONE, cited only

No new work this pass. Full grammar, worked example, and register cross-checks already published above
(§5.3, "TwoLiteralPattern... payload grammar, CONFIRMED by live execution... 2026-10-09") and in
`scratchpad/bink_twoliteralpattern_0x1001b5a3_2026-10-09/`. This pass's own secondary=2/3 traces
independently confirm that secondary=5's shared descriptor-L literal-byte source and 16x16 output
footprint are not special cases but a family-wide convention across primary=1's "new content" sub-cases
— see the unifying finding at the top of this subsection.

#### secondary=6 — literal/raw copy, dual-row duplication (`0x1001ba40`-`0x1001be6a`) — structural grammar carried over, not independently re-confirmed this pass

**What is already CONFIRMED (prior unit, §5.3b, re-cited, not re-derived here):** an alignment test
(`test al,3` at `0x1001ba6f`) selects a byte-granular ("unaligned") copy loop (`0x1001ba90`-`0x1001bb67`)
or a word-granular ("aligned") copy loop (`0x1001bb72`-`0x1001be6a`); each repacks literal source
bytes/words into a dword written to two destination rows at once; zero bitstream bit-reads, table
lookups, or external calls — a sibling of the plain block-copy cases (primary 0/9), not of secondary=3's
DC/flat fill. **This pass's own scans confirm it is real and abundant** (258 occurrences in
`main_menu_bg.bik` frame 0's corrected Y+chromaA scan — comparable in frequency to secondary=5), but
budget this pass went to the two genuinely-unworked cases (0 and 2) per the task's own stated priority,
plus the two "quick-confirm" cases (1/4, 3) that turned up the flag-bit-short-circuit and footprint
corrections above. **Not independently live-traced this pass — the existing structural analysis stands
uncorrected, and (unlike secondary=0) real occurrences are plentiful and easy to re-locate with this
pass's own scan scripts whenever budget allows a live confirmation pass.**

**[HIGH CONFIDENCE, carried over from §5.3b's own disassembly-based confirmation, not downgraded or
re-verified. CONFIRMED, this pass, as a byproduct of the dispatch-distribution scans: real occurrence
count and frequency. OPEN: a live worked example with real source/destination bytes and a register-level
cross-check, to bring this entry to the same standard as secondary=0/2/3/5 above.]**

#### Updated summary table (supersedes/extends §5.3's own case-by-case map for primary=1's secondary values only)

| Secondary | Mechanism | Output footprint | Live worked example this pass? |
|---|---|---|---|
| 0 | RLE+permutation (giant1), half-res, 2x upsampled | Unconfirmed this pass (by analogy to case 3: 8x8 pre-upsample) | No — 0 real occurrences found in 20 sampled frames across 3 files (negative result, see above) |
| 1 or 4 | Flag-bit short-circuit (`[ebp-0x210]`&8) -> shared epilogue `0x1001be76`; mark skip flag x2, no pixel write | N/A (flag array only), 16px-wide column granularity | Yes — dispatch idx 178, frame 0 Y-plane |
| 2 | Third DC+AC dequant/direct-store-IDCT engine | **16x16 pixels (64 dwords), CONFIRMED** | Yes — dispatch idx 0 (block (0,0)), frame 0 Y-plane; DC=118, nibble=2 |
| 3 | DC/flat fill | **16x16 pixels (64 dwords), CONFIRMED (corrects prior "~40" estimate)** | Yes — dispatch idx 1 (block (0,1)), frame 0 Y-plane; fill byte=0x99 |
| 5 | TwoLiteralPattern (giant2) | 16x16 pixels (64 dwords) | Already done, 2026-10-09 (cited only) |
| 6 | Literal/raw copy, dual-row duplication | Not measured this pass | No — structural only, carried over from §5.3b |

**[Overall confidence for this subsection: CONFIRMED for secondary=2/3's full grammars and secondary=1/4's
real dispatch mechanism (all three live-executed against real unmodified `binkw32.dll` code, real
`main_menu_bg.bik` bytes, with register-level cross-checks); CONFIRMED for the "descriptor L" shared
literal-byte-cursor finding and the 16x16-output-footprint-family finding (both cross-case, live-derived);
HIGH CONFIDENCE but not this-pass-reverified for secondary=0 and secondary=6 (both carried over from
earlier units' own disassembly-based confirmations); a genuine, bounded, honestly-reported OPEN item that
secondary=0 produced zero live occurrences in every real sample checked this pass, despite a serious,
corrected-methodology search across 20 frame instances in 3 files.]**

### 5.3j The IDCT butterfly's exact fixed-point rounding sequence, and `0x10024eb0`'s full tree-decode algorithm — instruction-level, §5.3g-style, one bit-exact worked-example verification (2026-10-10)

**Scope.** Direct continuation of §5.3b/§5.3i, routed from Team B as their literal current blocker on
getting Bink video on screen. §5.3b already established the *structure* of `0x100244c0`/`0x10024b50`
(two-stage separable butterfly, DC-only fast path, four rotation constants, row-major
`struct_byte_offset(k)=2k`) and of `0x10024eb0` (sibling of the breadth-first worklist tree builder
`0x100258d0`). This subsection raises both to §5.3g's own instruction-cited pseudocode standard. Full
fresh disassembly (capstone/pefile, static, no decompiler) of `0x100244c0`-`0x100247ce`, `0x100247d0`-
`0x10024b4f` (the third, direct-store sibling named in §5.3i), `0x10024b50`-`0x10024e9e`, and
`0x10024eb0`-`0x100258a8` (the tree decoder's real, full extent — one function, 14,840 bytes, confirmed
by the single `ret` at `0x100258a8` and no other `ret`/function-prologue in the whole range). Scripts and
raw captures: `scratchpad/bink_unicorn_harness_2026-10-08/{dump_idct.py (via the session scratchpad
copy), capture_idct_col80.py, verify_idct_math_v2.py, idct_col80_coeffs.bin, idct_col80_meta.txt,
real_output_col80_8x8.bin}`.

#### Part 1 — the IDCT rounding sequence, CONFIRMED and bit-exact-verified

**The butterfly is one shared 8-point network, run twice (column pass then row pass) with the exact
same multiply/shift sequence — CONFIRMED by literal address-for-address identity between the two
passes' own code.** Both `0x100244c0` (intra) and `0x10024b50` (inter/residual) use it unchanged; a
third sibling, `0x100247d0` (§5.3i's secondary=2 engine), also uses it unchanged, differing only in its
calling convention and in packing 4 output bytes per dword before the store (confirmed by direct
disassembly, not by analogy).

**Column pass (dequantize + first butterfly), `0x100244c0` body, general (AC-nonzero) path:**

```c
// 0x100244e0-0x1002469d, once per column c=0..7 (eax+=2 per iteration, i.e. struct_byte_offset(c)=2c):
// row[r] = sign-extended 16-bit word at struct offset r*16 + c*2 (row-major, struct_byte_offset(k)=2k,
// k=r*8+c -- already CONFIRMED §5.3b); quant[r] = dword at quant_row_ptr + r*0x20 + c*4
// (quant_byte_offset(k)=4k, same k -- already CONFIRMED §5.3b; raw bytes NOT re-dumped here, see
// spec-bink-static-tables.md).
//
// AC-nonzero test (0x1002450b-0x1002451e): or's together rows 1..7's raw words (row 0/DC excluded) --
// if ALL of rows 1-7 are zero across the ENTIRE 8x8 block (tested once per column, effectively ANDed
// across all 8 columns by the loop), the whole function instead takes the DC-only fast path:
//   dc = (row0_raw * quant[r=0]) >> 11      // 0x10024526-0x10024529, PLAIN sar, NO rounding add
//   out[row] = dc  for all 8 rows            // 0x1002452c-0x10024546/0x10024685, straight broadcast
//   (jmp 0x1002454c -> 0x10024685)
//
// General path, each of the 8 per-row dequantize products is shifted >>11 INDIVIDUALLY, before any
// combination -- and every one of these 8 shifts is a PLAIN `sar reg,0xb` with NO rounding bias added
// first (confirmed: no `add`/`lea ...+N` of any kind precedes any of these 8 sar sites --
// 0x10024571/0x10024577/0x1002457a/0x1002457d (t6,t2,t4,t0) and 0x100245dd/0x100245e3/0x100245e6/
// 0x100245f1 (t7,t3,t5,t1)):
t0 = sar(row0 * quant0, 0xb);  t1 = sar(row1 * quant1, 0xb);  // 0x10024551/0x1002457d; 0x100245ae/f1
t2 = sar(row2 * quant2, 0xb);  t3 = sar(row3 * quant3, 0xb);  // 0x1002455f/77;          0x100245c7/e3
t4 = sar(row4 * quant4, 0xb);  t5 = sar(row5 * quant5, 0xb);  // 0x10024554/7a;          0x100245d2/e6
t6 = sar(row6 * quant6, 0xb);  t7 = sar(row7 * quant7, 0xb);  // 0x10024567/71;          0x100245b9/dd

// even half -- 0x10024580-0x100245ac
b0 = t0 + t4;  d0 = t0 - t4;  s1 = t2 + t6;  d1 = t2 - t6;      // 0x10024580-8a
e0 = sar(d1 * 0xb50/*sqrt2, 2896*/, 0xb) - s1;                    // 0x10024590-0x10024599, PLAIN sar
A = s1 + b0;  B = b0 - s1;  C = e0 + d0;  D = d0 - e0;           // 0x1002459e-0x100245ac

// odd half -- 0x100245ae-0x10024634
P = t3 + t5;  M = t5 - t3;  Q = t1 + t7;  R = t1 - t7;           // 0x100245e9-0x100245fe
U = P + Q;  V = Q - P;  E2 = R + M;                                // 0x100245fc-0x10024604
H  = sar(E2 * 0xec8 /*3784*/,        0xb);                        // 0x10024613/0x1002461f  PLAIN sar
F  = sar(M  * 0xffffeb18/*-5352*/,   0xb);  F2 = F - U;  F3 = F2+H;// 0x10024607/0x10024622/25/2b
G  = sar(R  * 0x8a9  /*2217*/,       0xb);  G2 = G - H;             // 0x1002460d/0x10024628/2d
I  = sar(V  * 0xb50  /*sqrt2, 2896*/, 0xb);  I2 = I - F3;  G3=G2+I2; // 0x10024619/0x1002462f/32/34

// combine, 8 dword outputs (struct_byte_offset(k)=2k style stride, 0x20/row, in temp buffer) --
// 0x1002463f-0x10024685
out[0]=U+A; out[1]=F3+C; out[2]=I2+D; out[3]=B-G3;
out[4]=G3+B; out[5]=D-I2; out[6]=C-F3; out[7]=A-U;
```

**Row pass (final butterfly + descale to 8-bit pixels), `0x100244c0` body, `0x100246b4`-`0x100247be`,
every output row:**

```c
// Identical network, run on the column pass's own 8 dword outputs directly (col[0..7] = the row's
// 8 intermediate values) -- NO further table/quant multiply here, confirmed (no `imul ...,[edx+...]`
// anywhere in this loop, only the 4 fixed rotation-constant multiplies, same four values as above):
b0=col0+col4; d0=col0-col4; s1=col2+col6; d1=col2-col6;
e0 = sar(d1*0xb50, 0xb) - s1;           // 0x100246cd-0x100246d6, PLAIN sar, same as column pass
A=s1+b0; B=b0-s1; C=e0+d0; D=d0-e0;
P=col3+col5; M=col5-col3; Q=col1+col7; R=col1-col7;
U=P+Q; V=Q-P; E2=R+M;
H  = sar(E2*0xec8,        0xb);                                    // 0x10024721/0x1002472d
F  = sar(M *0xffffeb18,   0xb); F2=F-U; F3=F2+H;                    // 0x10024715/33/36/3c
G  = sar(R *0x8a9,        0xb); G2=G-H;                              // 0x1002471b/39/3e
I  = sar(V *0xb50,        0xb); I2=I-F3; G3=G2+I2;                   // 0x10024727/43/46/48

// THE ONLY ROUNDING-BIASED SHIFT IN THE WHOLE FUNCTION -- one per final pixel, 8 per row (64 total):
// (sum + 0x7f) >> 8, i.e. floor((sum+127)/256) -- NOT +0x80/128. Because 127 is one less than half of
// 256, an exact tie (sum of the form N*256+128) rounds DOWN, not up -- confirmed, not a generic
// "standard rounding" claim (every one of the 8 sites below uses the literal immediate 0x7f, never
// 0x80):
pixel[0] = sar(U+A +0x7f, 8);     // 0x10024750-0x10024757  (dest byte 0, ecx-1)
pixel[7] = sar(A-U +0x7f, 8);     // 0x1002475a-0x10024768  (dest byte 7, ecx+6)
pixel[1] = sar(F3+C+0x7f, 8);     // 0x1002476b-0x10024772  (dest byte 1, ecx)
pixel[6] = sar(C-F3+0x7f, 8);     // 0x10024774-0x10024784  (dest byte 6, ecx+5)
pixel[2] = sar(I2+D+0x7f, 8);     // 0x10024787-0x1002478e  (dest byte 2, ecx+1)
pixel[5] = sar(D-I2+0x7f, 8);     // 0x10024791-0x1002479e  (dest byte 5, ecx+4)
pixel[4] = sar(G3+B+0x7f, 8);     // 0x100247a1-0x100247a8  (dest byte 4, ecx+3)
pixel[3] = sar(B-G3+0x7f, 8);     // 0x100247ab-0x100247b5  (dest byte 3, ecx+2)
```

**Summary of the rounding rule, CONFIRMED by exhaustive address enumeration (every `sar` in
`0x100244c0` was checked for an immediately-or-nearby-preceding rounding `add`):** all 13 `>>0xb`
(`/2048`) shifts in the column pass, and all 9 in the row pass (8 per-term dequant shifts + 4 rotation
shifts, minus overlap correctly counted: 8 dequant + 4 rotation = 12 per pass, two passes = 24 total,
plus 1 DC-fast-path shift = 25 occurrences of `sar ...,0xb` in the whole function, every one of them a
PLAIN truncating/floor arithmetic shift with zero rounding bias. The ONLY place a rounding constant
(`0x7f`) is added before a shift anywhere in this function is the 64 final pixel-descale sites
(`>>8`), once per output byte.

**`0x10024b50` (inter/residual) and `0x100247d0` (secondary=2's direct-store sibling), CONFIRMED by
direct disassembly to use the IDENTICAL rounding rule** — same four rotation constants at the same
relative code positions, same "plain `sar`,0xb" pattern with zero exceptions, same final `+0x7f; sar
,8` pixel descale. The only differences, both already cross-checked this pass by address, not inferred:

- `0x10024b50` ADDS the descaled residual onto an existing motion-compensated reference byte, strictly
  AFTER the `+0x7f`/`>>8` step, not before it and not fused into it — e.g.
  `0x10024e03-0x10024e0d: edx = sar(row_sum+0x7f, 8); edx += byte[esi-1] (reference pixel);
  byte[ecx-1] = edx` (one of 8 such sites, `0x10024e03`-`0x10024e88`). This confirms §5.3b's "predict,
  then add a residual" framing down to the exact instruction order: the rounding is applied to the
  *residual alone*, and the reference byte is summed in afterward as a plain 8-bit add (silent
  wraparound on overflow, no clamp instruction present at any of the 8 sites).
- `0x100247d0` packs 4 horizontally-adjacent descaled pixels into one dword via `movzx`/`shl 0x10`/`or`
  before the single dword store (e.g. `0x10024a6e-0x10024a92`), instead of 8 individual byte stores —
  a packing-detail difference only, not a rounding-rule difference (same `+0x7f`/`sar ,8` values feed
  the pack).

**Bit-exact verification against a real, independently FFmpeg-verified block — CONFIRMED, not merely
attempted.** A live Unicorn hook was placed at `0x100244c0`'s own entry across a full real
`main_menu_bg.bik` frame-0 decode (reusing the existing harness, `capture_idct_col80.py`), filtered to
the one call whose `dest` stack argument equals `Y_BUF+80` — i.e. the exact block
(`postidct_block_frame0_row0_col80_VERIFIED.bin`, §3.5 of the harness findings note, already
independently verified byte-for-byte against FFmpeg's own decode). That call's real live register state
was captured: quantizer nibble `6` (`edx-0x10037cc0 = 0x600 = 6*256`), and the real 64-coefficient
struct (DC=51 `0x0033`, and nonzero AC at k=1,2,8,9,10,17: `-3,-1,-3,1,1,1`, all other 58 coefficients
zero — `idct_col80_coeffs.bin`). The pseudocode above was then transliterated instruction-for-
instruction into pure Python (`verify_idct_math_v2.py`, `butterfly8()` shared by both passes exactly as
derived here) and run on these real inputs against the real intra quantization table row 6 (bytes read
directly from the DLL for this check only — not reproduced in this document, see
`spec-bink-static-tables.md` for the published table). **Result: bit-exact, all 64 bytes, zero
divergence**, against both the harness's own freshly-re-captured real output at that address (also
independently re-confirmed `== postidct_block_frame0_row0_col80_VERIFIED.bin` byte-for-byte this pass)
and therefore transitively against FFmpeg's own decode of the same block. This is a genuine, from-first-
principles re-derivation producing the identical answer, not a copy of the DLL's own arithmetic dressed
up as independent confirmation — the Python port never calls into the DLL or Unicorn for the actual math,
only for sourcing the real test-vector inputs.

**[CONFIRMED — full disassembly of the butterfly stage in all three sibling IDCT entry points; every
shift's rounding behavior (or lack of it) individually checked by address; one full bit-exact
verification against a real, independently-FFmpeg-matched block, by independent from-scratch Python
re-derivation, not DLL re-execution. HIGH CONFIDENCE, not independently re-verified this pass: that
`0x10024b50`'s own reference-byte-add path is bit-exact end-to-end against a real motion-compensated
P-frame block (the rounding/shift portion of its code is address-confirmed identical to the
already-verified intra path; only the post-add step was not separately run through a second live
P-frame worked example this pass).]**

#### Part 2 — `0x10024eb0`'s full tree-decode algorithm

**Function extent and signature, CONFIRMED.** `0x10024eb0`-`0x100258a8` (one function, `ret 4`,
stack-cookie prologue/epilogue at `0x10024eb9`/`0x1002589a`-`0x100258a0`, matching the project's own
established stack-cookie idiom). Calling convention, CONFIRMED by direct disassembly and cross-matching
against §5.3i's already-live-confirmed call site (`0x1001b348`): hidden `ecx` = bit-reader context
pointer (3 fields, identical struct to §5.3g's `{+0x0 accum, +0x4 cursor, +0x8 bits_avail}` — confirmed
by the field order read at entry, `0x10024ef4`-`0x10024efc`: `eax=[esi+8]`(bits_avail),
`edi=[esi+4]`(cursor), `edx=[esi]`(accum)); one stack arg `[ebp+8]` = the caller's output coefficient
struct pointer (cached to `[ebp-0xc8]` at entry `0x10024ec9`, reloaded at `0x10025777` for the final
write-out below — matches §5.3i's documented `&[ebp-0x310]` argument exactly).

**Bit-read primitive used throughout this function is DIFFERENT from §5.3g's `read_bits(br,N)`: it is a
single-bit-at-a-time walk, LSB-first, with a 31-bit countdown refill — CONFIRMED, a genuinely distinct
idiom from the N-bits-at-once `0x10019360` family.** The recurring pattern (first instance
`0x10024fdb`-`0x10024ffa`, repeated verbatim at a dozen+ sites throughout):
```c
if (bits_left == 0) { accum = *cursor++; bits_left = 0x1f; }   // refill: read a fresh dword, 31 MORE
                                                                   // bits usable after this one
else { bits_left--; }
bit = accum & 1; accum >>= 1;                                     // consume exactly 1 bit, LSB-first
                                                                    // (same convention as §5.3g)
```
A second, genuinely different multi-bit variant is used only for the final magnitude/sign extraction of
"many extra bits" leaves (`0x100253cb`-`0x10025439`, and its unrolled twins): it reads up to a whole
remaining-bits count via `shr`/`shl`/`or` bit-reversal-style accumulation across a dword boundary,
refilling mid-read if the current dword runs out — structurally the same "peek more than you have,
refill, retry" idiom as `0x10018c20`'s merge primitive (§5.3g), but consuming a data-dependent bit count
rather than a fixed field width.

**The algorithm is a genuine two-phase, level-by-level breadth-first worklist build — CONFIRMED,
raising §5.3b's own "sibling of `0x100258d0`" structural claim to the instruction level for the first
time.**

*Phase 0 — seed and level-loop setup (`0x10024ecf`-`0x10024f92`):*
```c
memset(&[ebp-0x144], 0, 0x78);                           // 0x10024ed5-0x10024eef, scratch zeroed
uint32_t A = read_bits_4(br);                              // 0x10024f05-0x10024f49, SAME refill idiom
                                                              // family as 0x10018ca0's own nibble read
                                                              // (§5.3g) -- the tree's max depth/level
queue[0..5] = {0x10, 0x60, 0xb0, 7, 0xb, 0xf};            // 0x10024f68-0x10024f88, 6 literal SEED bytes
bias_mask = 1 << (A - 1);                                  // 0x10024f5b-0x10024f70
level = A;                                                  // 0x10024f76, the outer level counter
```

*Phase 1 — per level, drain the worklist (`0x10024fa0`-`0x10025495`), a genuine front/tail two-pointer
BFS queue over the SAME scratch buffer used for the seed:*
```c
// front = &[ebp-0xa4] (next node to expand), tail = &[ebp-0xa0] (next free queue slot) -- confirmed by
// cross-referencing every store site against these two cached pointers.
while (front < tail) {                                     // 0x10025489-0x1002548f
    uint8_t tag = *front;
    if (tag == 0) { front++; continue; }                    // 0x10024fd3-0x10025483: already-resolved
                                                               // leaf, just advance (no new bit read)
    uint32_t sel = tag & 3;                                 // 0x10025003-0x10025011
    switch (sel) {
      case 0:  // 0x10025018-0x10025036 -- ONE new child pushed (tail++), tag' = (tag>>2)*4 + 0x11
      case 1:  // 0x10025038-0x10025082 -- node resolved in place (tag'=(tag>>2)*4+2) AND exactly
               // THREE new children pushed (tags (tag>>2)*4+0x12/+0x22/+0x32) -- a 4-way split
      case 2:  // 0x10025087-0x1002509a -- node resolved to a TERMINAL LEAF (byte 0 written in place,
               // front++, NO children); (tag>>2) is cached as this leaf's own value-category for
               // the magnitude/sign read below
      case 3:  // 0x100253cb-0x10025439 -- the magnitude+sign read happens HERE, immediately, for
               // this node (not deferred to Phase 2) -- see "sign recovery" below; result written
               // directly into the SAME scratch buffer, reinterpreted as a SIGNED 16-BIT WORD ARRAY
               // via `word ptr [ebp + idx*2 - 0x14c]` (idx = running coefficient-count, `[ebp-0x9c]`)
    }
    // then: read the NEXT node's own 1-bit "is-leaf" flag and (if needed) the next 2-bit `sel` field,
    // using the SAME single-bit refill idiom, before looping back to `front`'s successor
  }
  bias_mask >>= 1; level--;                                 // 0x10025495-0x1002549b
} while (level > 1);                                        // 0x1002549c-0x1002549f
```
The scratch buffer at `[ebp-0x144]`/`[ebp-0x14c]` is deliberately reused as BOTH the growable
byte-tagged worklist (during tree construction) and, in place, as the final signed-16-bit coefficient
value array once a slot resolves — confirmed by the overlapping stack-offset ranges (`-0x144..-0xcc`,
120 bytes, memset at entry; `-0x14c..-0xd0`, 124/62-words, written by every `mov word [ebp+idx*2-0x14c]`
site) and by `case 3` writing a signed WORD directly into a slot the SAME loop had just read as a BYTE
tag. This is the concrete mechanism behind §5.3b's "writes ~30 dword/word fields" phrasing: the AC
coefficient array itself IS this scratch buffer, not a separate structure built elsewhere.

*Phase 2 — second pass over any categories Phase 1 deferred (`0x100254ab`-`0x10025762`, only entered if
`level`'s own cached pre-value, `[ebp-0xc0]`, was nonzero):* walks `front2 = [ebp-0x98]` up to
`tail = [ebp-0xa0]` (the SAME tail pointer Phase 1 left behind — this is the real termination bound,
confirmed, see below), re-dispatching each still-byte-tagged entry through a SECOND 4-case jump table
at `0x100258bc` (targets `0x10025512`/`0x10025522`/`0x1002556a`/`0x10025716`) that is structurally
parallel to Phase 1's own table but reads the actual extra bits and writes the final signed word —
i.e. Phase 1 builds the tree'S SHAPE and some leaves' values eagerly (case 3 above); Phase 2 resolves
any leaves Phase 1 left as pure shape markers.

**Sign recovery — two distinct, both CONFIRMED, address-cited mechanisms, not one:**

1. **General multi-bit case** (`0x10025439`-`0x10025466`, and four more byte-identical unrolled copies
   earlier in Phase 1's own body, e.g. `0x100250d2`-`0x100250fb`): the raw extra-bits field is first
   masked to its own bit-width and OR'd with the level's `bias_mask` (`0x10025440`-`0x10025448`,
   `(raw & (bias_mask-1)) | bias_mask`), THEN the sign is recovered by testing that SAME masked value
   against the level's own bias mask again (`0x10025454`: `test maskedvalue, bias_mask`) — if the test
   bit is clear, the value is used as-is (positive); if set, it is negated (`0x10025464: neg ecx`)
   before the final `mov word [ebp+idx*2-0x14c], cx` store. The sign is encoded as a specific BIT
   POSITION within the raw extra-bits field itself (position given by the level's own bit-width), not
   as a separate trailing sign bit appended after the magnitude.
2. **Single-extra-bit shortcut** (`0x100255c7`-`0x100255cf`, and 4 further identical copies later in
   Phase 2's own body, e.g. `0x10025630`/`0x10025691`/`0x10025706`/`0x1002573a`): for the smallest
   nonzero-magnitude category, the raw bit is converted directly via `and esi,1; neg esi; or esi,1` —
   bit=0 → `+1`, bit=1 → `-1`. A compact "magnitude is always exactly 1, this single bit is purely the
   sign" case, distinct from mechanism 1's "magnitude and sign share one masked field."

**Output-struct write-out, CONFIRMED by direct address trace (`0x10025762`-`0x100258a8`), in THREE
distinct groups, in this exact order — not a single clean memcpy:**

1. **Bit-reader write-back** (`0x10025762`-`0x10025774`): `ecx = [ebp-0xc4]` (the ORIGINAL hidden-`ecx`
   bit-reader-context pointer, cached at entry `0x10024edd`) — `[ecx+0]=edx` (accum), `[ecx+4]=edi`
   (cursor), `[ecx+8]=eax` (bits_avail), using the SAME 3-field layout read at entry and the SAME
   `write_back_bitreader`-style idiom already established for `0x10019360` in §5.3g — this is the real
   confirmation that this tree decoder shares one bit-reader context with every other decode on the
   same per-row bitstream, exactly as §5.3b's narrative already assumed.
2. **One explicitly-traced coefficient word** (`0x1002576a`/`0x10025795`): `dx = word[ebp-0x14a]`
   (k=1's own slot in the scratch/output word array) then `word[eax+2] = dx` (`eax` reloaded from
   `[ebp-0xc8]`, the caller's output struct pointer) — a direct, address-confirmed instance of
   `struct_byte_offset(k)=2k` (k=1 → byte offset 2) being honored on the WRITE-OUT side, not just
   assumed by extrapolation from the read side (§5.3b/§5.3i's own prior confirmations were both on
   READS). **OPEN, not closed this pass**: the remaining AC coefficients (k=2..63) are near-certainly
   copied by an analogous per-word store this one instance belongs to a larger (not fully enumerated)
   copy sequence — the function's sheer size (14,840 bytes) meant this pass traced the write-out
   mechanism's FIRST instance and the general struct-field block immediately following it (next item),
   not every one of the remaining ~60 word-copy sites individually. Treat `struct_byte_offset(k)=2k` as
   HIGH CONFIDENCE (not newly re-derived per-k) for k≥2, carried over from §5.3b/§5.3i's own already-
   CONFIRMED finding on the read side, which this pass's one k=1 write-side instance is consistent with.
3. **A block of 24 auxiliary dword fields**, struct offsets `0x04` through `0x7c` in a non-sequential
   order (`0x10025777`-`0x1002589c`: pairs of `mov ecx/edx,[ebp-N]; mov [eax+M],ecx/edx` for 24 distinct
   `(N,M)` pairs, e.g. `[ebp-0x144]->[eax+4]`, `[ebp-0x140]->[eax+0x14]`, ... `[ebp-0xd8]->[eax+0x78]`).
   These are DWORD-sized, not word-sized, and their source stack slots are NOT the `-0x14c` coefficient
   array — they are a separate set of per-run/per-level bookkeeping locals this function maintains
   internally. **OPEN, not resolved this pass**: their individual semantic roles and whether any
   downstream consumer (the IDCT functions above, or something else) actually reads them — not chased,
   given the time budget, beyond confirming their existence, exact addresses, and that they are real,
   address-distinct struct fields rather than padding or dead stores.

**Termination condition, CONFIRMED precisely — NOT a fixed count of 63/64:** Phase 1's own outer loop
terminates when `front` (`[ebp-0xa4]`) reaches `tail` (`[ebp-0xa0]`) at the CURRENT level AND the level
counter itself reaches 1 (`0x10024f95`/`0x1002549c`-`0x1002549f`). The number of AC coefficients
actually produced is exactly however many terminal (`case 2`/`case 3`-style) leaves the tree's own BFS
expansion naturally produces by the time every level has been drained — bounded above by the struct's
own fixed 63-AC-coefficient capacity (consistent with the already-known 64-coefficient struct size,
1 DC + 63 AC), but not independently re-derived as an explicit "stop at 63" check anywhere in this
function's own code; the natural tree-exhaustion condition (`front==tail`, `level==1`) is the real
mechanism, and a conformant reimplementation should drive its own worklist to the same
front-catches-tail condition rather than hard-coding a 63-symbol loop bound.

**[CONFIRMED — function extent, calling convention (cross-matched against §5.3i's own live call-site
trace), the single-bit LSB-first refill primitive, the front/tail two-pointer BFS worklist mechanism and
its 4-case dispatch table (both tables' 4 target addresses read directly from the binary), both sign-
recovery mechanisms with addresses, the bit-reader write-back block, one explicit word-level write-out
instance (k=1) confirming `struct_byte_offset(k)=2k` on the write side, and the real front==tail
termination condition. HIGH CONFIDENCE, not independently re-derived this pass: `struct_byte_offset(k)=2k`
for k=2..63 specifically on the write-out side (carried over from the already-CONFIRMED read-side
finding). OPEN: the full case-0/case-1 tag-arithmetic meaning (what each of the `+0x11/+0x12/+0x22/+0x32`
family concretely encodes about code length/category, beyond "expands the tree by 1 or 4 nodes"); the
semantic role of the 24 auxiliary dword struct fields at `0x04`-`0x7c`; whether Phase 2 is reachable with
real content in this specific file (not tested live this pass, only disassembled).]**

---

### 5.4 ~~The transform stage: a confirmed three-CPU-tier dequantize/butterfly dispatch~~ RETRACTED 2026-10-08 — this entire section is audio-side, not video

~~`0x10018600` (the video-side dispatcher, read in full; called both externally and internally)
branches on a flag bit: set → loops over a count, gate-checking each iteration (`0x10018160`, an SSE
scalar-float threshold test) and tail-jumping into the CPU-tiered transform chain below; clear → a
float accumulator step plus two calls into audio-core-range addresses (§7, not a video-pixel path).~~

~~**SSE2 tier** (`0x10017dc0`/`e20`/`f80`, read in full): builds a broadcast scale vector
(`unpcklps`), loops reading 128-bit vectors, and the most elaborate of the three
(`0x10017f80`, 123 instructions) shows an explicit **difference** step (`psubw` — subtract a
reference/previous value) followed by `pmulhw`/`pmullw` (scale), a variable-count `psrad` shift, and
`paddd`/`packssdw` — i.e. **`(a−b) × scale ≫ shift`, rounded, repacked to int16, 8 coefficients at a
time.** The expected shape for an integer dequantize-with-rounding step, plausibly fused with a
predict/delta step given the leading subtraction. **HYPOTHESIS** on the exact coefficient-domain
meaning (DCT butterfly vs. simple dequantization vs. both fused) — no inverse-DCT-specific constant
pattern was matched against any public reference.~~

~~**MMX tier** (`0x10017e90`, 81 instructions, read in full): instruction-for-instruction the same
shape at half the vector width (64-bit `mm` registers, 4 words/iteration), ending with `emms`.~~

~~**x87/fallback tier** (`0x10018110`, `0x10017c10`, `0x10017ce0`): the "else" targets of the same
CPU-flag-gated branches (confirmed by reading the dispatcher bodies `0x1001878a`/`0x100186fa` in
full — each tests the cached feature byte for bit 3 or bit 1 and falls through to one of these three
if neither is set); confirmed FPU-heavy, not individually read line-by-line.~~

~~This three-tier dispatch is **live and reachable from the real per-frame decode path** — distinct
from, and not to be confused with, the unrelated per-pixel-format-conversion CPU-dispatch family
(`BINKY12`/`16`/`P8`/`BINK16`/`32`) `review/scoping-bink-codec.md` already found and flagged as
HIGH-confidence dead code (a completely different set of functions and a completely different
global record).~~

**RETRACTED, 2026-10-08.** A dedicated follow-up unit opened all three CPU tiers plus the
x87/fallback tier line-by-line (including the two SSE2 functions and the two x87 functions this
section's own earlier pass had not read past their entry) and found this entire family —
`0x10018600` and everything it dispatches to — is **audio-side PCM gain/cross-fade
post-processing, not a video transform, and not a DCT either way.** Concretely: `0x10018600` is
**the exact same function §6.5 already documents** as the audio gain-ramp mode selector (identical
`0x1001e320`/`0x1001e810`/`0x10024150` calls, identical `0x1003c214`=0.5 ramp constant) — this
section and §6.5 were independently describing the same address and reached opposite conclusions
about which core it belongs to. A whole-section call/jmp census plus a whole-binary literal-address
byte scan found **exactly two callers of `0x10018600` in the entire binary, both audio**
(`0x10010fe0`, confirmed via the known `track×384 + handle+0x154` audio per-track indexing; and
`0x10018ba0`, reached from a handle-indexed command-queue walk, `0x10013700`) — **zero reachability
from the real video mega-function or any of its case bodies**, confirmed both ways. The three
"tiers" are three distinct operations, none transform-shaped: a plain `out[i]=round(a[i]×scale)`
scale-only step; the same scale applied independently to two separate source arrays then
interleaved (no cross-term); and `out[i] = b[i] + ((a[i]-b[i])×i)>>shift` where `i` is literally the
running element-position index (0..7) — a positional linear ramp/cross-fade, not a DCT: no cosine
constants, no recursive even/odd split, no bit-reversal permutation found anywhere in any tier, and
no literal float constant anywhere in these functions matches any classic 8-point DCT-II basis value
(0.707107, 0.980785, 0.831470, 0.555570, 0.195090, 0.541196, 1.306563, √2 — all checked, no match).
What IS present (a 32-entry power-of-two mantissa/exponent table, a 16-entry VLC code-length table, a
96-entry geometric-magnitude table) is consistent with bitstream/entropy-decode plumbing, matching
§6's own audio-core character, not a transform. The "gate" `0x10018160` (previously "an SSE
threshold test, not traced further") is actually a 375-instruction real bitstream-driven
floating-point value decoder feeding this family. This whole block of functions is moved to audio —
see new §6.5a. **No replacement content for "the video decode core's transform stage" exists yet —
see §8 OPEN item 5's replacement text: the real video-side per-block transform, if this codec
revision has one at all, remains unidentified.** Full detail:
`scratchpad/bink_video_transform_semantics_2026-10-08.md`.

### 5.5 Output: confirming decode writes into the game-registered buffer

`0x1001a490` (51 instructions, pure integer arithmetic, no calls) takes a width input and computes
nine dword fields `0x1001cb00` later `_alloca`s against — reached by exactly one caller, inside
`_BinkRegisterFrameBuffers@8`'s own body. **Correction, 2026-10-08: these nine fields are
Bink-internal per-row *scratch*-buffer sizes (five distinct `round_up(...)+padding` formulas, three
slots sharing one formula and two sharing another), NOT the output-plane width/height/stride/offset
fields** — a different structure from the game-side plane-pointer record below. Do not read "the
actual buffer-registration fields" (above) as describing the destination buffer's geometry.

`0x1001cb00`'s own double-buffer slot-selection code indexes the **same `0x30`-byte-stride record
array `spec-bink-boundary.md` §5.2 already documented from the game side**, closing that spec's own
open item halfway (the CPU-side half; the GPU-upload half remains a separate, still-open item):
**decoded pixel data is written through records in the same caller-owned array the game registered
— not a Bink-internal default buffer** — consistent with that spec's finding that
`BinkCopyToBuffer` is never imported (no intermediate copy step exists). **The exact final
pixel-write instructions are now CLOSED (2026-10-08) — see §5.6.**

### 5.6 The literal pixel-write instructions and the per-slot plane-pointer field map (CLOSES former OPEN item 6, 2026-10-08)

Hand-traced register/stack-argument provenance: `0x1001cb00` → `0x1001a530` → the type-0 "block
copy" case body (`0x1001ac02`–`0x1001ad09`). The destination pointer (`edi`, loaded from stack slot
`[ebp-0x1f4]` at `0x1001ac18`) traces back through `0x1001a530`'s own prologue
(`0x1001a5a5`→`0x1001a5db`) to a caller-pushed stack argument that is concretely
**`*(handle+0xd0) + target_slot*0x30 + 0x40`** — the `+0x40` field of the same `0x30`-byte-stride
record `spec-bink-boundary.md` §5.2 documents from the game side. The literal store instructions
(repeated per 2-row group, an unrolled 8×8-pixel/1-byte-per-pixel copy):

```
0x1001ac33: mov dword ptr [edi], eax
0x1001ac35: mov dword ptr [edi + 4], ebx
0x1001ac38: mov dword ptr [edi + ebp], ecx
0x1001ac3b: mov dword ptr [edi + ebp + 4], edx
```

The copy *source* (`esi`, stashed via `[ebp-0x21c]`) resolves to the SAME `+0x40` field but of the
*previous*-frame slot — confirming textbook "skip block = copy from last frame" semantics, both
endpoints landing in the caller-registered buffer, zero Bink-internal scratch involvement. `0x1001cb00`
calls `0x1001a530` four times (not "1 to 3" as a prior pass's structural estimate had it), one call
per plane-or-sub-case; three of the four cleanly resolve to one plane each:

| Call | Address | Record fields referenced | Plane |
|---|---|---|---|
| 1 | `0x1001cc92` | `record+0x40`/`+0x44` (ptr/pitch) | Luma (Y) |
| 2 | `0x1001cd1b` | `record+0x1c` (flag), `record+0x20` (role not pinned) — no `+0x40`/`+0x44` reference | **CONFIRMED NOT a plane, 2026-10-08 (§5.3d, Unicorn harness):** the pointer-shaped argument slot receives `record[target-slot]+0x1c`'s raw 0/1 VALUE, not a pointer — rules out a 4th color plane with a concrete mechanism, consistent with driving the per-macroblock skip/change-flag array (§5.3); that array's own consumer-side semantics remain OPEN |
| 3 | `0x1001cda7` | `record+0x28`/`+0x2c` (ptr/pitch) | Chroma-A |
| 4 | `0x1001cdfb` | `record+0x34`/`+0x38` (ptr/pitch) | Chroma-B |

**Full per-slot record field map, this `0x30`-byte-stride array (CONFIRMED):**

| Offset | Role |
|---|---|
| `+0x1c` | per-slot ready/dirty flag (double-buffer selection, already known) |
| `+0x20` | referenced only by call 2 above — role OPEN (call 2 itself CONFIRMED not a plane, §5.3d) |
| `+0x28` / `+0x2c` | chroma-A plane pointer / pitch |
| `+0x34` / `+0x38` | chroma-B plane pointer / pitch |
| `+0x40` / `+0x44` | luma (Y) plane pointer / pitch |

`+0x00`–`+0x18` (7 dwords) not individually resolved — OPEN, plausibly leading base-pointer/
byte-size bookkeeping the game's own allocation math uses, not touched by the decode core's hot path.

**This is a planar, not packed/interleaved, layout** — three independent `(pointer, pitch)` pairs
(Y, chroma-A, chroma-B), no fourth pair anywhere in the record or in any of the four calls' argument
setups, and **no alpha plane**. Independently corroborated by a black-box FFmpeg oracle run against
this game's own `videos/logo_thq_and_voli_hd.bik` (360-frame, 1280×720 file): `ffprobe` reports
`pix_fmt=yuv420p` (not `yuva420p`) for this game's real shipped content, and `ffmpeg` frame-0
extraction produced exactly 1,382,400 bytes = 1280×720×1.5, an exact match to the yuv420p
plane-size arithmetic (Y=921,600 + U=230,400 + V=230,400) implied by this record layout. Frame 0
(a solid-black fade-in) decoded to uniform `Y=16`/`U=V=128` — the canonical **limited-range ("TV
range")** black/neutral point, matching `ffprobe`'s own `color_range=tv` — a concrete, actionable
detail for any YUV→RGB conversion step (`RGB = matrix × (Y-16, U-128, V-128)`-style, not a naive
full-range matrix). FFmpeg was used strictly as a black-box CLI (official gyan.dev build, downloaded
into this project's own `tools/ffmpeg_dl/`, run hidden, output values only — its source was never
read).

**No YUV→RGB colorspace conversion was found anywhere in this DLL's traced call chain** (HIGH
CONFIDENCE, not exhaustively proven since the three giant un-bit-traced bitstream engines, §5.3,
were not individually read line-by-line). No instruction sequence reads from more than one plane
pointer while writing a single destination, and the SSE2/MMX transform stage (§5.4) is a
per-coefficient operation, not a cross-channel matrix multiply. This confirms, one level deeper,
`spec-bink-boundary.md` §5.1/§5.2's finding that the game receives raw planar YUV with no CPU-side
conversion inside `binkw32.dll` — if a YUV→RGB conversion exists at all, it happens entirely
game-side (most plausibly a pixel shader at upload time), which remains that spec's own open
"GPU-upload gap" item, unaffected by this finding.

**[CONFIRMED — disassembly, full provenance chain for the literal store instructions, the plane
field map (calls 1/3/4), and the planar/no-alpha layout; independently corroborated by the FFmpeg
black-box oracle. HIGH CONFIDENCE — no colorspace conversion inside this DLL. OPEN — call 2's
`record+0x20` role; the unresolved `record+0x00`-`+0x18` range.]** Full detail:
`scratchpad/bink_video_pixelwrite_colorspace_2026-10-08.md`.

---

## 6. The audio decode core (`0x1001e1d0`–`0x100260d4`)

### 6.1 This game's shipped build DOES use Bink's own audio path — confirmed, not dead code

`spec-bink-boundary.md` §1.2/§3.2 already CONFIRMED the game imports and calls
`_BinkSetSoundTrack@8`/`_BinkSetVolume@12`/`_BinkSetSpeakerVolumes@20`, wires
`BinkSetSoundSystem(BinkOpenDirectSound, 0)`, and forces the per-frame pump onto the always-taken
synchronous path. Combined with §2's confirmed chain from `BinkDoFrame` into this core, **the audio
core runs on every decoded frame of every Bink movie that carries a sound track** — essentially
every cutscene. `spec-audio-runtime.md` (this project's Wwise runtime spec) was checked directly and
contains **zero** references to Bink/`binkw32` — **the two audio pipelines are confirmed, both
structurally and textually, to be entirely separate**: Wwise handles all non-movie game audio;
Bink's own internal DirectSound path (not Wwise, not XAudio2, not WaveOut — all three confirmed
unimported) handles movie audio exclusively.

### 6.2 Setup-time doorways vs. per-frame doorways

A call/jmp-target census restricted to edges crossing into the audio core finds 33 edges, 12
distinct doorway addresses, from 10 distinct caller functions — matching
`review/scoping-bink-codec.md`'s own count exactly. A BFS from `BinkOpen`'s real worker
(`0x1000f310`) reaches 4 of the 10 callers directly, confirming those are **setup-time** doorways
(not per-frame):

- `0x10018938` → `0x1001e3e0`/`0x1001e550` — the "half-block-size → float → reciprocal" pattern the
  scoping note flagged, confirmed here for the first time to be genuinely `BinkOpen`-time setup
  (computing a fixed `1/(N/2)`-shaped normalization constant once per stream, not once per block).
- `0x1000f740` → `0x1001e260`, called **14 times from one caller** — `0x1001e260` (read in full) is
  a generic 32-byte-aligned ring/arena **allocator "push"** primitive (record a `(size, pointer)`
  pair as the next of 32 slots in a circular queue). Reads as "carve the audio decoder's working
  memory into ~14–15 named sub-regions once, at open time."
- `0x1000d0d0`/`0x10013559` → `0x1001e2b0` — a generic 32-byte-aligned allocate-and-round-up helper.

**The remaining 5 callers were NOT found in either the `BinkOpen` or `BinkDoFrame` BFS closures**
(20-hop limit). A literal-address search found no absolute-address references to any of the five
anywhere in the mapped image; their own callers trace a short chain into the address band between
`_BinkSetSpeakerVolumes@20` and `_BinkSetSoundOnOff@8`. **HIGH, not CONFIRMED**: plausibly reached
from a volume/mute-change-triggered re-setup path (recomputing a gain-ramp target when
`BinkSetVolume`/`BinkSetSpeakerVolumes` is called mid-playback) — not confirmed by a full trace
(open item).

### 6.3 Bitstream setup: a confirmed entropy-coded scale-factor reader

A tight cluster of small functions (`0x100250a0`–`0x10025202`) implements a sliding 32-bit
bit-buffer refill (shift the accumulator, test the vacated bit, refill from a word-aligned source
array when dry) feeding a **unary/Golomb-style growth code** (`len = len*4+3` per "continue" bit) —
one logical routine unrolled seven times, one copy per scale-factor/exponent-delta band.

`0x100244c0` (read in full) reads **seven** word-sized band-delta fields at fixed offsets and tests
whether all seven are zero:

- **Fast path (all zero):** one Q11 fixed-point value (divide by 2048, `sar ...,0xb`) is replicated
  into seven of eight output band-scale slots.
- **General path (any nonzero):** a fully fixed-point (shift-right-11 throughout) **inverse
  butterfly** combines the seven deltas pairwise using four literal integer multiply constants —
  `0xb50` (2896 ≈ `2048·√2`), `0x8a9` (2217), `0xec8` (3784), `-5352` — to derive all eight band-scale
  outputs. **The `0xb50 ≈ 2048·√2` constant is a direct, literal signature of a DCT/Hadamard-shaped
  small transform applied to the scale-factor envelope itself**, not to audio samples — the envelope
  is entropy-coded compactly before being expanded to 8 explicit per-band gains. (It is publicly
  documented, and cited here only for orientation/naming, that Bink Audio codes its quantizer
  envelope compactly rather than transmitting one scale per band outright — consistent with, not the
  source of, this independently-derived finding.)

A cluster of ~15 small, highly repetitive functions (`0x10025810`–`0x10025d65`) was identified as
structurally consistent with further VLC/dequant plumbing but not individually opened (open item).

### 6.4 The transform stage: a confirmed recursive radix-4/radix-2 FFT/DCT

Two functions, `0x1001e5d0` and `0x1001e810`, implement **byte-for-byte identical recursive
control flow** on a block-size argument `N`:

```
N > 32:    recurse on quarter-sized sub-blocks; N > 512 splits into FOUR quarters, 32 < N <= 512
           uses a mid-size path, else a small-mid path — each tail-jumps to a combine epilogue
N == 32:   a dedicated, fully-unrolled 448-instruction kernel (0x10022890)
8 < N < 32: a smaller generic kernel (0x10023a80, 170 instructions) plus an explicit 8-float
           permutation fixup
N == 8:    hand-unrolled radix-2 butterfly, INLINE (8 movss/addss/subss pairs over 8 consecutive
           floats, read directly — confirmed literal instruction sequence, not inferred)
N == 4:    hand-unrolled radix-2 butterfly, INLINE (4 movss/addss/subss pairs, same pattern)
```

**This is a textbook recursive Cooley-Tukey-style mixed-radix decomposition** — split into quarters
for large sizes, bottom out to hand-unrolled small kernels for 4 and 8, a dedicated efficient kernel
for the common size 32 — confirmed by direct disassembly, not inferred from FPU-opcode density
alone. Nine further functions are confirmed **zero-FPU pure control-flow orchestrators** selecting
among a shared pool of ~6 FPU-heavy leaf kernels (82 to 580 instructions each); one leaf kernel
(`0x10020500`, 736 instructions, spot-disassembled) confirms operation on **interleaved complex
(real/imaginary) pairs** with an explicit twiddle-factor-shaped constant load and sign-flip — a
genuine complex-FFT combine stage.

**Not fully resolved:** four more giant, near-pure-FPU functions structurally identical in shape to
the confirmed kernels have **no direct call/jmp edge from anywhere inside the audio core targeting
them** — HIGH, not confirmed, that these are reached via an indirect (e.g. size-class jump table)
dispatch this method cannot trace statically, or are an unused/alternate size class (open item).
Directly confirmed supported sizes, from the branch structure alone: 4, 8, and 32 each get a
dedicated path; sizes in `(8,32)` share one generic kernel; sizes `>32` recurse by quarters. The
actual block size(s) this game's shipped `.bik` files use, and mono/stereo channel handling, were
not independently determined (open items — would require reading per-stream descriptor fields
against real file samples).

### 6.5 A refinement of BK-U0's tentative framing: gain-ramp mode, not compressed-vs-raw

§2.3 noted `0x1001e320` and `0x1001e810`/`0x10024150` as the two audio-core entry points, initially
guessed (by the container/dispatch-level unit, from branch structure alone, explicitly flagged as
unopened) to be "compressed-Bink-audio" vs. "raw/PCM." **Having opened both fully: there is no
raw/uncompressed-PCM passthrough.** `0x1001e810` **is itself** the recursive FFT/DCT dispatcher of
§6.4 (`0x1001e5d0`'s own sibling, byte-for-byte identical control flow, differing only in which
pre-stage/kernel functions it calls); `0x1001e320` also reaches the same transform machinery (it
calls `0x1001e5d0` directly, twice). **The real distinction is a gain-application mode**:

```
0x10018600(trackRecord, sampleCount, flags, ...):
    call 0x10018160(...)              # clamp/check how many samples can be produced this call
    if ok:
        if flags & 1:
            call 0x1001e320(...)      # fixed/already-settled gain — no ramp math
        else:
            # one-pole exponential volume-ramp smoothing, in float (constant at 0x1003c214):
            #   newGain = curGain - (curGain - targetDelta) * RAMP_RATE
            if sampleCount > 4:  call 0x10024150(...); call 0x1001e810(...)
            elif sampleCount == 4: call 0x1001e810(4, ...)
    else:
        call 0x10001eb0(...)          # underrun/silence signal
```

Both branches terminate in the SAME recursive FFT/DCT dispatcher family — confirming this is a
gain-ramp mode selector, not a compressed/raw split, offered as a direct-evidence correction of an
explicitly-flagged-as-tentative guess, not a contradiction of it.

### 6.5a The real identity of `0x10018600` and its three "tiers": PCM gain/cross-fade post-processing, moved here from the former §5.4 (2026-10-08)

**This is the full body behind this section's own `0x10018600` reference above** — a dedicated
follow-up confirmed, by exhaustive call-census (whole-section call/jmp scan plus a whole-binary
literal-address byte scan), that `0x10018600` has **exactly two callers in the entire binary, both
audio**: `0x10010fe0` (indexing the known `track×384 + handle+0x154` per-track audio array) and
`0x10018ba0` (reached from a handle-indexed command-queue walk, `0x10013700` — consistent with a
volume/mute-change re-setup path, partially closing §8's former OPEN item 7). **Zero reachability
from the real video mega-function or any of its case bodies**, confirmed both directions.

The "gate," `0x10018160`, is a real 375-instruction bitstream-driven floating-point value decoder
(not merely "an SSE threshold test"), feeding three distinct per-sample operations, none
transform-shaped — each implemented three times (SSE2/MMX/x87 tiers, confirmed identical shape at
each width):

- **OP_A** (`0x10017dc0`/x87 `0x10017c10`): `out[i] = round(a[i]×scale)` — a single source array,
  one shared scalar scale, no subtraction.
- **OP_B** (`0x10017e20`/x87 `0x10017ce0`): the same scalar-scale multiply applied independently to
  **two separate source arrays**, then interleaved — no cross-term, so not an FFT/DCT combine
  either.
- **OP_C** (`0x10017f80`/MMX `0x10017e90`/x87 `0x10018110`): `out[i] = b[i] + ((a[i]-b[i])×i)>>shift`,
  where `i` is literally the running element-position index (built from integer literals 0..7,
  incremented each iteration) — a **positional linear ramp/cross-fade**, not a DCT butterfly: no
  cosine constants, no recursive even/odd split, no bit-reversal permutation anywhere in any tier.
  (This is the operation the retracted §5.4 described as "`(a−b) × scale ≫ shift`" and read as a
  plausible dequantize/predict step — the `scale` term there is this positional index `i`, not a
  quantizer value.)

**No DCT constants found anywhere in these functions.** Every literal float constant referenced was
checked against the classic 8-point DCT-II basis values (0.707107, 0.980785, 0.831470, 0.555570,
0.195090, 0.541196, 1.306563, √2) — no match. What IS present: trivial `0.0`/`1.0`/`0.5` constants, a
sign-flip mask (`-0.0`/`0x80000000`, the already-known FFT complex-conjugate negation, §6.4), a
32-entry power-of-two mantissa/exponent table, a 16-entry VLC code-length table, and a 96-entry
geometric magnitude table (ratio≈1.165) — all consistent with bitstream/entropy-decode plumbing,
matching this section's own audio-core character, not a transform.

**[CONFIRMED — disassembly, full call-census both directions, all three op-tiers plus the x87
fallback read in full, exhaustive DCT-constant check.]** Full detail:
`scratchpad/bink_video_transform_semantics_2026-10-08.md`.

### 6.6 Output: a DirectSound buffer internal to `binkw32.dll`, not a game-visible buffer

Unlike the video path (CPU-heap frame buffer, game-registered and game-visible), **no evidence of a
game-visible audio output buffer was found.** The per-track ring-buffer structures (`handle+0x154`,
384-byte stride) are allocated and consumed entirely inside `binkw32.dll` (§6.2's `0x1001e260`
allocator, called from `BinkOpen`-reached setup code). Combined with `spec-bink-boundary.md`'s own
confirmed finding that the game wires `BinkSetSoundSystem(BinkOpenDirectSound, 0)` and never itself
calls `BinkOpenWaveOut`/`BinkOpenXAudio2`/`BinkOpenMiles`, the natural reading is: **Bink owns a
DirectSound secondary buffer internally and the decode chain writes PCM straight into it**, with the
game never touching the samples directly. **HIGH, not CONFIRMED** — the actual
`IDirectSoundBuffer::Lock`/`Unlock`-style call site itself (which would live inside
`_BinkOpenDirectSound@4`'s own body, `0x10015150`, outside both cores) was not opened by any of the
three units (open item — the one piece that would make this CONFIRMED).

---

## 7. Cross-block calls: shared primitives, not a clean phase boundary

The video and audio decode cores are overwhelmingly internally self-contained (the overwhelming
majority of each core's own edges stay internal), but **video decode calls at least seven addresses
physically inside the address range this file's own structural map assigns to the audio core**:
`0x100244c0`, `0x10024b50`, `0x10024eb0`, `0x100247d0`, `0x1001e810`, `0x10024150`, `0x1001e320`.
This nuances, but does not overturn, the two-cores framing — it means a handful of functions
physically inside the "audio" address range are **shared entropy/transform primitives reachable
from the video path too**, not audio-exclusive. Concretely: `0x1001c305` (a video-side dispatch
helper) calls two of these addresses directly. **Correction, 2026-10-08: `0x10018600` itself is NOT
one of these shared/video-reachable cases — §5.4's former claim that it was "the video-side
dispatcher" calling `0x10024150`/`0x1001e810` is retracted; `0x10018600` has exactly two callers in
the whole binary, both audio (§6.5a).** The real cross-block sharing is limited to the three giant
video entropy engines' own direct calls into the audio-range addresses listed above (confirmed by
the video entropy-coding follow-up, §5.3) — not the transform/gain-ramp family. **Further
correction, 2026-10-08 (§5.3b): `0x100244c0`/`0x10024b50`/`0x10024eb0` specifically are NOT shared
primitives at all in any meaningful sense — they are a genuinely video-only DCT-residual mechanism
(AC-coefficient tree-decoder + intra/inter IDCT pair) reached only from video primary cases 5/7,
physically co-located in the "audio-range" address band for the same proximity reason `0x10018600`
was, but functionally entirely video's own.** `0x100247d0` was not re-examined by that pass and
keeps its prior "shared/video-reachable" characterization unchanged. Whether the remaining handful
means video and audio genuinely share one entropy-primitive pool, or the address-range split itself
is better understood as "codec-core utility region" rather than a strict video/audio partition, is
**not resolved** — flagged honestly rather than forced into either framing (open item).

---

## 8. OPEN items

1. Which internal bitstream-path the two per-file revision flag bits (§1.4, set from the signature's
   4th byte) actually select inside the video decode core — not traced into the video core itself.
2. ~~The exact byte-level entropy-coding math inside the three giant bitstream engines
   (`0x1001b5a3`/`0x1001c3d7`/`0x1001af5e`) — confirmed reachable and zero-SIMD/FPU, not traced
   bit-for-bit (Huffman lookup, run-length expansion).~~ **CLOSED 2026-10-08 (§5.3a): fixed-width +
   table-driven, not Huffman — a 4-bit-selected 64-byte permutation table, a 2-bit/4-bit blend-mask
   mechanism, and motion-compensated-copy/DC-fill/block-copy case bodies.**
3. ~~Several jump-table case bodies in the video core's per-block dispatch were not individually read
   (listed in BK-U1's own scratch write-up).~~ **Substantially narrowed 2026-10-08 (§5.3a): the six
   sibling case bodies sharing `0x1001c3d7`'s dispatcher frame are now individually traced.**
4. The exact byte gap between `0x1001a650` and `0x1001a8b0` inside the video core's mega-function was
   not individually walked instruction-by-instruction (the shared-frame conclusion rests on
   consistent `ebp`-relative offsets on both sides, not a full walk).
5. ~~The exact DCT-vs-dequantization-only semantic meaning of the SSE2/MMX transform stage's
   `(a−b)×scale≫shift` butterfly (§5.4) — no inverse-DCT-specific constant pattern was matched
   against any public reference.~~ **CLOSED 2026-10-08, by dissolution, not by answering as framed
   (§6.5a): the whole family is audio-side PCM gain/cross-fade post-processing, reachable only from
   audio, not a video transform or a DCT either way — exactly two callers, both audio, confirmed.**
   5a. ~~New item replacing it: the real video-side per-block transform, if this codec revision
       uses a classic block-residual transform at all, remains unidentified...~~ **CLOSED 2026-10-08
       (§5.3b): it does. `0x10024eb0`/`0x100244c0`/`0x10024b50` are a real fixed-point two-stage
       separable 8-point inverse DCT pair (intra/inter), reached via primary cases 5/7, with their
       own dedicated 16-row quantization tables (`0x10037cc0`/`0x10038cc0`) — found in a
       previously-unopened function pair that happened to sit in the same nominal "audio-range"
       address band §6.5a's retraction occupied, by physical proximity only, not function. New
       narrower open item: a full bit-exact hand-decode of one real case-5/7 block checked
       pixel-for-pixel against FFmpeg's own output — structurally confirmed, not yet verified
       against one concrete worked example. **Attempted 2026-10-08, STILL OPEN (§5.3c): a fresh
       attempt found the primary case-selector byte itself is fed by a previously-undocumented
       entropy pre-stage (`0x10019360`/`0x10018ca0`/`0x10018c20`, a third sibling of this codec's
       own tree-builder family) that was not opened to completion — no block was reached, honestly
       reported rather than faked. Two scoped continuation options on record, §5.3c.**
       **Attempted again 2026-10-08 via a Unicorn real-code-emulation harness (§5.3d, Option B of
       those two continuations) — real blocks WERE reached this time, AC/texture is bit-exact vs.
       FFmpeg, but a uniform +128 DC-level bug (root-caused to a specific DPCM-accumulator function,
       `0x1001a160`, not yet fixed) keeps this item open rather than closing it. Narrower than
       before: one function, one identified bug mechanism, not an open-ended decode question. Raw
       test vectors: `spec-bink-video-test-vectors.md`.**
       **Follow-up pass, 2026-10-08 (§5.3e): the descriptor's construction site is now traced
       (`0x1001a530` at `0x1001a5ae`-`0x1001a5f3`) and `0x1001a160` is re-characterized (seed+deltas,
       no hardcoded bias constant exists), but the true root cause is STILL OPEN — two concrete fix
       hypotheses were tried against the harness and both were empirically REFUTED, including the one
       that looked most obvious (a +4 sub-chunk-header skip for chromaA/chromaB, which made the match
       rate worse, not better). The AC-coefficient byte-offset-to-index mapping this item's own
       worked-example needs is now CLOSED (§5.3b: row-major, `struct_byte_offset(k)=2*k`,
       `quant_byte_offset(k)=4*k`), and both quantization matrices plus the three entropy pre-stage
       tables are now byte-exact published in `spec-bink-static-tables.md` — the worked example itself
       remains blocked only on the DC-bug fix, not on missing data.**
       **Third pass, 2026-10-08 (§5.3f): the `param3`/`0x4003318c` lead is CLOSED as a dead end
       (mislabeled — those fields are on `param1`); `0x1001a160`'s full batch/seed/delta structure is
       now mapped and confirmed plane-independent (ruling out a per-plane misconfiguration); a new,
       broader "+4" fix hypothesis was tried and REFUTED again. Still STILL OPEN: the true arithmetic
       root cause (now a much narrower, well-scoped target — hand-decode one affected batch bit-by-bit
       against its neighbors). chromaB's own root cause is separately CONFIRMED this same pass (not
       fixed) — see item 6a below.**
       **Fourth pass, 2026-10-08 (§5.3g): the entropy pre-stage question this item's own "Attempted
       2026-10-08" entry left open (full instruction-level algorithm for `0x10019360`/`0x10018ca0`/
       `0x10018c20`) is now CLOSED** — all 5 table-build strategies, the merge primitive, call
       granularity (once per 8-pixel block-row/descriptor), and the real starting bit position
       (measured per-plane, Team B's own row-skip hypothesis refuted by direct row-0 measurement) are
       traced to the instruction. **chromaB's own stall mechanism (flagged above as "see item 6a
       below," CONFIRMED-not-fixed in §5.3f) is now identified to the exact instructions**
       (`0x10019586`-`0x1001958e`) — the LEN==0/over-capacity poison path. This item's own remaining
       scope (the case-5/7 worked example's DC +128 bug) is UNCHANGED by this pass — still STILL OPEN,
       the entropy layer feeding it is now fully mapped but the DC bug itself was not re-attempted.
       New narrower OPEN items surfaced instead, not chased this pass: a second, previously-
       undocumented sibling entropy-pre-stage stream ("descriptor B") whose downstream consumer was
       not traced; the 23 sibling `0x10018ca0` calls per block-row (building tables for other,
       untraced sub-streams) whose own downstream consumers were not traced; whether an MSB-first bit-
       order assumption (vs. the now-confirmed real LSB-first convention) could independently explain
       a bit-offset scanner's own zero-hit result (flagged as testable, not confirmed one way or the
       other).
6. ~~The exact final pixel-write instruction(s) inside each video case handler were not individually
   located (§5.5) — the destination is structurally certain without pinpointing the literal store.~~
   **CLOSED 2026-10-08 (§5.6):** literal store instructions found and traced to the game-registered
   buffer record, full plane-pointer field map recovered, planar/no-alpha layout confirmed,
   independently corroborated by an FFmpeg black-box oracle run. Two narrower items surfaced instead:
   6a. ~~Call 2 into `0x1001a530` (`0x1001cd1b`) references `record+0x1c`/`+0x20` but no plane
       pointer — whether it writes a plane at all (vs. driving the per-macroblock skip-flag array,
       §5.3) is OPEN.~~ **CONFIRMED NOT a plane, 2026-10-08 (§5.3d):** its pointer-shaped argument
       slot receives the `+0x1c` ready/dirty flag's raw 0/1 value, not a pointer. Narrower open item:
       the per-macroblock skip/change-flag array's own consumer-side semantics.
   6b. The per-slot record's own `+0x00`–`+0x18` range (7 dwords) was not individually resolved —
       plausibly leading base-pointer/byte-size bookkeeping for the game's own allocation math, not
       touched by the decode core's hot path.
7. The 5 audio-core setup doorways not reached by either the `BinkOpen` or `BinkDoFrame` BFS closures
   (§6.2) — plausibly a volume/mute-change re-setup path, not confirmed by a full trace.
8. ~~The ~15-function repeated VLC/dequant utility family (`0x10025810`–`0x10025d65`) — not opened.~~
   **CLOSED 2026-10-08 (§5.3a): it is ONE function (`0x100258d0`, 428 instructions), a canonical
   Huffman code-length-tree builder, not 15 separate functions. New narrower item: its own caller
   was not found.**
9. ~~The four orphan FPU transform kernels with no confirmed caller inside the audio core (§6.4) — may
   be reached via an indirect/size-class dispatch, or may be an unused size class.~~ **CLOSED
   2026-10-08: not actually orphaned — all four (`0x1001f040`, `0x1001f9e0`, `0x10021ca3`,
   `0x10022240`) are internal `jmp`-target labels mis-segmented as independent functions, confirmed
   by tracing unbroken `ebp`-relative stack frames back to already-known, already-reachable parent
   functions (`0x1001ef80`, `0x1001f870`, and two unnamed-but-live siblings) — the same
   "jmp-only label miscounted as a function" artifact already flagged elsewhere in this project's
   own function-boundary methodology, now found recurring in the audio-core census too.**
10. The actual transform block size(s) this game's shipped `.bik` files use, and mono/stereo channel
    handling inside the audio core — neither independently determined; would need real file samples.
11. The `IDirectSoundBuffer::Lock`/`Unlock`-style call site itself, inside `_BinkOpenDirectSound@4`
    (`0x10015150`) — not opened by any of the three units; the one piece that would upgrade §6.6 from
    HIGH to CONFIRMED.
12. The exact selection condition for which of several purposes the CPU-feature-selector record
    (§3.1/§3.4) holds at any given time, across the unreachable `BinkCopyToBuffer` call tree.
13. The cross-block sharing noted in §7 — whether video and audio genuinely share one
    **entropy-primitive** pool (narrowed 2026-10-08: `0x10018600`'s transform/gain-ramp family is
    confirmed NOT shared, §6.5a/§7 — the remaining open sharing is limited to the three giant video
    entropy engines' own direct calls into audio-range addresses, §5.3), or the "two cores" framing
    needs a third, shared-utility region added to it.
14. The 384-byte per-track struct (`handle+0x154`) is only partially mapped — needed to settle
    mono/stereo handling (item 10) and several of the audio-core's own per-track ring semantics.
15. **New, 2026-10-08 (§1.5a):** the discarded-looking `(table[NumFrames]-table[0])*FPS/NumFrames`
    quotient on `0x1000e560`'s zero-max-delta edge case — no consumer found. `handle+0x4d4`
    (`allKeyframes`)'s own consumer — not found anywhere in `BINK`. Whether `0x1000e560`'s
    validate/scan ever re-runs on a seek (`BinkGoto`) rather than purely at `BinkOpen` time — not
    checked.

---

## 9. Clean-room check

- Every address above is plain hex in `binkw32.dll`'s own `0x10xxxxxx` space, explicitly distinguished
  from the main exe's disjoint `0x00xxxxxx` space throughout. No Ghidra was used by any of the three
  contributing units — no Ghidra auto-names, no decompiler pseudocode, no `FUN_`/`DAT_`/`iVar`-style
  tokens anywhere in this file; every instruction-level claim is literal `capstone` mnemonic/operand
  output, read directly.
- Quoted strings (the seven container error/mutex-tag strings, §1.7) are literal `BINKCONS`-section
  ASCII content, quoted verbatim as shipped data — the same convention `spec-bink-boundary.md`
  already uses for its own heap-tag strings.
- No leaked Volition/RAD/Bink/Havok source of any kind was read, consulted, or used by any of the
  three contributing units. **FFmpeg's own Bink video/audio decoder was not read, consulted, or used
  anywhere** — every finding in this file traces to this project's own direct `capstone` disassembly
  of `binkw32.dll`, read fresh this session; the one public-orientation citation (§6.3's "Bink Audio
  codes its envelope compactly") is explicitly flagged by provenance and used for naming only, never
  as a source for any address, offset, or instruction sequence.

---

## Changelog

- **2026-10-09 — TwoLiteralPattern (§5.3) payload grammar CONFIRMED by live execution, and §5.3c
  extended with the case-1 secondary selector byte's real source** (SPEC TEAM, agent
  `a9edc5da8ab49aa09`, routed item (b) + a Team B follow-up folded into the same pass). **Part A:**
  the task's own premise (`logo_amd_hd.bik` frame 0 block (0,0) dispatches secondary=5) does NOT
  hold under live execution — that block is secondary=3; zero secondary=5 occurrences exist
  anywhere in that frame. Pivoted to a real occurrence in `main_menu_bg.bik` and traced the full
  grammar to completion: 2 literal bytes + exactly 8 control bytes (4×2-bit fields each) produce a
  fixed 16×16-pixel block via the already-published `0x1003c130`/`0x1003c200` blend tables, all 64
  real output bytes verified. **Part B (Team B follow-up, §5.3c):** the case-1 secondary/sub-type
  selector byte is confirmed to come from descriptor B's own separate scratch buffer (zero
  bitstream-bit cost), not the same buffer as the primary byte and not a raw bit-reader read at
  all — explains why Team B's `readBits(4)` approach was only coincidentally right on one file.
  Full detail: `scratchpad/bink_twoliteralpattern_0x1001b5a3_2026-10-09.md`. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §1.6 hex-dump transcription fix**, flagged by `purrsian-5b`/Team B. The illustrative
  16-byte dump at absolute file offset `0x76fc` (`logo_thq_and_voli_hd.bik`) was wrong past byte 7 —
  the zero run was one byte short and a trailing `e0` had no basis in the real file. Independently
  re-read three ways against the real shipped file this pass (`Seek`+`Read`, `ReadAllBytes`+slice);
  every surrounding checkpoint in the same paragraph (header fields, audio chunk size, Y-span dword)
  was re-verified byte-for-byte and matches exactly, confirming the file/offset are otherwise correct
  — only the dump itself was mistyped. No load-bearing offset or size was affected. Corrected dump:
  `57 f1 7e 65 ef 10 11 00 00 00 00 00 00 00 11 00`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-08 — §5.3g gains a full call-by-call re-trace of the 23 table-build calls, in response to
  Team B's third independent negative at this location (580 bits measured, LEN=0, vs. this project's
  588/528)** (SPEC TEAM, routed via `purrsian-04`). A completely fresh Unicorn trace reproduces this
  project's own 588-bit/LEN=528 figure exactly, call for call, and identifies the one genuinely
  data-dependent mechanism in the whole sequence: the merge-network strategy's own real, *returned*
  bit-consumption count (calls #6/#7 only) rather than its nominal maximum field width. Directly ties
  Team B's two reported symptoms into one root cause: bit positions 576-591 are a 16-bit all-zero run
  in the real file, so any trace landing a few bits to 8 bits short of the true bit 588 reads LEN=0 —
  a plain arithmetic fact about the shared bytes. Offers a concrete, checkable hypothesis (an
  "always-nominal-width" or "always-per-side-item-count" reading of calls #6/#7's merge groups, which
  would drop exactly 8 bits across exactly 2 calls) for Team B to check against their own code, without
  claiming to see it. Closes part (d): exactly 1 of the 23 calls (call #1) feeds descriptor A. Full
  detail: `scratchpad/bink_5_3g_call_by_call_trace_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — §5.3g's "Implementer note" gains the absolute byte offset it had left
  unquantified** (SPEC TEAM, agent `a36c0368463f26313`, routed via a Team B relay through
  `purrsian-04`). Team B's own reimplementation of the §5.3g algorithm, starting from byte 0 or
  byte+4 of their own `frame0_raw.bin`, reproduced neither bit 588 nor LEN 528 — the algorithm was
  confirmed fine on both sides, so the open question was purely the sub-chunk's own absolute start
  offset. A fresh live Unicorn trace (two independent register/memory read paths) pinned it down:
  frame-0-blob byte 4, `main_menu_bg.bik` absolute file offset `0x2910`, immediately after one 4-byte
  field (HIGH CONFIDENCE a stored sub-chunk-span/skip-to-next-plane offset, triangulated against a
  second live measurement at chromaA's own dispatch). 16-byte test vector supplied. Full detail:
  `scratchpad/bink_y_plane_subchunk_start_offset_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — §5.3g correction: the real LEN for Y-plane frame 0 row 0 (descriptor A) is 528, not
  350** (SPEC TEAM, verifying agent `afbe23adb9f56a453`, routed via a Team B question through
  `purrsian-04`). The published `case_selector_scratch_frame0.bin` (350 bytes) is `real[178:528]` of
  the true 528-byte call output, not a byte-0-anchored 350-byte output — root cause: a 2-byte hook
  address error in `capture_stages.py` (`0x1001aab0` instead of the real call's return address
  `0x1001aab7`) that fired on an unrelated later event (row 1 of Y reusing row 0's own drained stack
  state) and happened to pass the capture script's own sanity filter. Confirmed 3 independent ways
  (live EDX register capture, raw-bit hand-reconstruction, descriptor bound arithmetic); full detail
  and the corrected §5.3g text above. **Also resolves a separately-reported "23 vs 25 calls"
  question: 23 is CONFIRMED CORRECT** (fresh call-by-call re-trace); the downstream "25" and a
  "diverges at byte 12" report are both explained as consequences of the same offset error, not
  real bugs — the downstream reimplementation's own algorithm is correct. Full detail:
  `scratchpad/bink_prestage_25call_discrepancy_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §5.3g: the entropy pre-stage's full instruction-level algorithm, closing all 4 of
  Team B's named blockers** (SPEC TEAM, agent `a34b0616291a584e2`). Full traced bodies for
  `0x10019360` (the symbol-expansion loop: N-bit LEN read, idempotency guard, a whole-call mode bit
  selecting flat-memset-fill vs. canonical-Huffman table-driven expansion, `cmp bl,0xc` on the
  *translated* value selecting literal-byte vs. run-length-replicate using run lengths `{4,8,12,32}`
  at `0x1003bd54-57`), `0x10018c20` (a bit-selected 2-source merge primitive), and `0x10018ca0`'s full
  ~1600-instruction body (prior units had only reached ~230 of it) — 5 distinct table-build strategies
  (identity, pairwise-swap, and a 4-level bit-reversal merge network built from chained `0x10018c20`
  calls, plus a 5th "explicit-prefix+ascending-fill" strategy), confirmed `classbyte==A` (max Huffman
  code length per nibble) by exact address cross-reference. New, previously-unnamed function
  `0x10019300` found (loops 16× over `0x10018ca0`). Call granularity CONFIRMED empirically: once per
  8-pixel block-row, per descriptor (live counts match `height/8` exactly on both of 2 sibling
  descriptors, across all 3 planes) — fully reconciles §5.3f's own raw call-count figures once a
  Unicorn TB-cache double-firing artifact is accounted for. Real starting bit position measured
  per-plane (Y=588/chromaA=262/chromaB=701 bits); **Team B's own row-skip hypothesis REFUTED by direct
  row-0 measurement** (the gap exists at row 0, where nothing could have been skipped) — real
  mechanism: 23 sibling `0x10018ca0` calls (building tables for other, untraced sub-streams) share one
  sequential bit cursor and must execute first in fixed, content-dependent-cost program order. Bit
  convention CONFIRMED LSB-first within each little-endian 32-bit dword, flagged as a testable
  alternate explanation for a bit-offset scanner's own zero-hit result. **Bonus: closes §5.3f's own
  named next-step** — chromaB's permanent "-4 stall" mechanism identified to the exact instructions
  (`0x10019586`-`0x1001958e`, the LEN==0/over-capacity poison path), though *why* chromaB's real bit
  values trigger it remains open. §5.3c's own high-level narration of the `0xc` dispatch annotated as
  superseded; §8 item 5a's own "Attempted 2026-10-08" entry closed with a new "Fourth pass" note; new
  narrower OPEN items surfaced (descriptor B's downstream consumer, the 23 sibling calls' own
  downstream consumers). Full detail: `scratchpad/bink_entropy_prestage_algorithm_2026-10-08.md`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §5.3c: a previously-undocumented entropy pre-stage found feeding §5.3's own
  case-selector byte; §1.6 extended with on-disk frame-table/frame-blob layout; §8 item 5a still OPEN,
  materially narrower** (SPEC TEAM, agent `ace5748db778659dc`, fresh worked-example attempt after the
  interrupted wave-9 unit `aa73105192eb4debc` wrote 0 bytes). Did NOT reach the goal (a bit-exact
  hand-decode of a real case-5/7 block vs. FFmpeg), reported honestly rather than faked — but found
  real new structure on the way. **The primary dispatch's own case-selector byte (§5.3) is not read
  from raw frame bytes** — it comes from an internal scratch buffer filled by `0x10019360` (a
  length-prefixed decompression stage, length bit-width = `bit_length(Width/8 + 0x1ff)`, concretely
  10 bits for a 1920-wide frame), whose own lookup tables are built by `0x10018ca0`/`0x10018c20` — **a
  third, independently-discovered sibling of this codec's breadth-first tree-builder family**
  (alongside `0x100258d0` and `0x10024eb0`), not previously suspected. Also found: a previously-
  undocumented per-frame sub-chunk structure (`handle+0x11c`+`handle+0xe8`, one 4-byte-length-prefixed
  sub-chunk per `0x1001a530` call); the on-disk frame-offset-table layout for `D10=0` files (table at
  file byte 44, frame 0's blob starts exactly where it ends — `main_menu_bg.bik` frame 0 pinned at
  `0x290c`-`0x35a94`, a strong keyframe-only real-sample candidate); and two more confirmed instances
  of the §1.5a "hidden EDI" argument convention. Two scoped continuation options recorded (finish
  hand-disassembling the remaining helpers, or a Unicorn-based real-code emulation harness — confirmed
  already installed, no download needed). Full detail:
  `scratchpad/bink_video_worked_example_block_2026-10-08-v2.md`. Cleanroom self-check: strict regex 0
  hits, unchanged.

- **2026-10-08 — §5.3b's case-1 sub-2/sub-6 contradiction resolved; §5.3a's 3 static tables transcribed
  in full** (SPEC TEAM, agent `a809e839654a0eefd`, resolving a peer-flagged inconsistency between the
  case-by-case table and its own closing bracket). Ground truth, direct `binkw32.dll` disassembly:
  **both the table row and the bracket were wrong, in different ways.** Secondary=2 (`0x1001b2ff`) is
  real and CONFIRMED — a third DC+AC dequantize/IDCT engine, a direct-store sibling of
  `0x100244c0`/`0x10024b50` via its own back-end `0x100247d0` (same DC-fast-path and rotation
  constants) — so the bracket's "never opened" was wrong for sub-2. Secondary=6 (`0x1001ba40`) is also
  real and CONFIRMED, but it's a literal/raw copy engine with alignment-selected byte/word dual-row
  duplication — zero bitstream reads, zero transform — so the table's "sub 3, 6 → DC/flat fill" row
  was wrong for sub-6 specifically (a transcription slip conflating it with sub-3, which genuinely is
  DC/flat fill, re-verified unchanged). Table row split into 3 correct rows; closing bracket rewritten
  to state both sub-cases' real mechanisms and addresses. Separately, transcribed the actual byte
  contents of all 3 static tables §5.3a had only described as "dumped in full" — the 16×64-byte
  scan-order permutation table at `0x100378c0` (new, never transcribed before; all 16 rows verified
  programmatically as exact permutations of 0-63) and the 2-bit/4-bit blend-mask pairs at
  `0x1003c130`/`0x1003c200` and `0x1003c180`/`0x1003c1c0` (re-dumped, cross-checked exactly against
  the prior entropy-coding unit's values). Full detail: `scratchpad/bink_case1_sub6_and_tables_2026-10-08.md`.
  Cleanroom self-check: strict regex 0 hits, unchanged.

- **2026-10-08 — new §5.3b finds the real video-side DCT, closes former OPEN item 5a** (SPEC TEAM,
  agent `acd6015d3c4de2fd5`). Opened primary dispatch cases 2/3 (never read before) and the three
  "external call" targets further than the entropy unit did. Case 2 = a third motion-copy variant
  (no residual). Case 3 = a genuine intra texture block — giant1's full-resolution twin (same
  RLE/permutation engine, no upsample). Case 4's external call (`0x10025e00`) resolves §5.3a's own
  "VLC tree-builder caller not found" gap and is a non-DCT Huffman-tree-coded residual-add. **The
  central find: `0x10024eb0`/`0x100244c0`/`0x10024b50` are a genuine fixed-point two-stage separable
  8-point inverse DCT pair (intra/inter), fed by a sibling AC-coefficient tree-decoder, each with its
  own 16-row quantization table, reached via primary cases 5/7** — closing the open "does this codec
  have a residual/transform block at all" question with a clear yes, found in a previously-unopened
  function pair that happened to share the same nominal "audio-range" address band §6.5a's audio
  retraction occupied (physical proximity, not function — the exact trap §7 already warns about).
  Weak FFmpeg-based corroboration across all 17 real `.bik` files: several (`main_menu_bg.bik`,
  `01_crawl.bik`, `GenkiTutorial_v1.bik`) show sustained non-trivial frame variation consistent with
  needing motion+DCT-residual encoding. New open item: a full bit-exact hand-decode of one real
  case-5/7 block vs. FFmpeg's own output — structurally confirmed, not yet verified against one
  concrete worked example. Full detail: `scratchpad/bink_video_residual_texture_block_2026-10-08.md`.
  Cleanroom self-check: **regex 0 hits, backstop 0 hits**, unchanged.

- **2026-10-08 — §5.4 RETRACTED, moved to new §6.5a; §5.3 extended with new §5.3a** (SPEC TEAM, agent
  `adc1fbc7bda5addd3` for the transform retraction, agent `ab1ba0da5a6822789` for the entropy
  extension). **Major correction**: the entire "video transform stage" §5.4 described
  (`0x10018600` and its three CPU-tiered op-families) is audio-side PCM gain/cross-fade
  post-processing, not video and not a DCT — exactly two callers in the whole binary, both audio;
  zero reachability from the video mega-function. `0x10018600` is the exact same function §6.5
  already independently documented as the audio gain-ramp selector; the two sections had reached
  opposite conclusions about the same address. No DCT constants found anywhere in the family (all 8
  classic DCT-II basis values checked, no match). Moved to new §6.5a; §5.4 struck through, not
  deleted, with a retraction note. §7's `0x10018600` cross-block example corrected. §8 items 5/9/13
  closed/narrowed accordingly; new item 5a added (the real video-side transform, if one exists at
  all, remains unidentified). Separately, new §5.3a closes former OPEN items 2/3/8: the three giant
  bitstream engines use fixed-width + table-driven entropy coding, NOT Huffman (a 64-byte permutation
  table, 2-bit/4-bit blend-mask mechanisms); `0x1001c3d7` is itself a 7-instruction dispatcher over 6
  individually-traced sibling case bodies (MOTION/DC-fill/PATTERN/block-copy); the "~15-function VLC
  family" is confirmed to be ONE 428-instruction function, a genuine canonical Huffman
  code-length-tree builder (new narrower open item: its own caller not found). Ghidra-free throughout
  (pefile+capstone, this DLL's own `0x10000000` base); no leaked material read; no FFmpeg source
  read (the entropy unit skipped the oracle entirely — FFmpeg not installed and not fetched without
  flagging first, per its own task instruction). Full detail:
  `scratchpad/bink_video_transform_semantics_2026-10-08.md`,
  `scratchpad/bink_video_entropy_coding_2026-10-08.md`. Cleanroom self-check: **regex 0 hits,
  backstop 0 hits**, unchanged.

- **2026-10-08 — §5.5 corrected, new §5.6 closes former OPEN item 6** (SPEC TEAM, agent
  `afe2e2c0048ec7084`). §5.5's "nine output dword fields" corrected: they size `0x1001cb00`'s own
  per-row scratch buffers, not the output-plane geometry. New §5.6: literal pixel-store instructions
  traced to the game-registered `0x30`-byte-stride buffer record (full field map: Y/chroma-A/
  chroma-B pointer+pitch pairs); planar, not packed; no alpha plane; no YUV→RGB conversion found
  inside `binkw32.dll` (HIGH confidence). Independently corroborated via a black-box FFmpeg oracle
  (official gyan.dev build, downloaded into `tools/ffmpeg_dl/`, run hidden, output values only, source
  never read): `ffprobe`/`ffmpeg` on this game's own `logo_thq_and_voli_hd.bik` confirm `pix_fmt=yuv420p`
  and an exact byte-count match to the derived plane-size arithmetic; frame 0's uniform `Y=16`/`UV=128`
  pins limited-range (TV) colorspace, a concrete detail for any future YUV→RGB shader work. §8 item 6
  closed; two narrower items added (call 2's `record+0x20` role; the unresolved `record+0x00`-`+0x18`
  range). Full detail: `scratchpad/bink_video_pixelwrite_colorspace_2026-10-08.md`. Cleanroom
  self-check: **regex 0 hits, backstop 0 hits**, unchanged.

- 2026-10-07: initial file, combining three parallel units dispatched from `review/scoping-bink-codec.md`'s
  own structural map — BK-U0 (container/API/dispatch glue, the 359-function remainder of the `BINK`
  section), BK-U1 (video decode core, `0x10017470`–`0x1001ce50`, 170 functions/7,490 instructions),
  BK-U2 (audio decode core, `0x1001e1d0`–`0x100260d4`, 104 functions/8,854 instructions). Closes the
  scoping note's own top open items: the exact container header layout and field validation (§1); the
  real `BinkDoFrame`→video/audio dispatch (§2); the CPU-feature-selector record's reachability,
  settled in both directions (§3); `RADCODE`'s integration point (§4); the per-plane decode workhorse
  and its 10-way/7-way block-type jump-table dispatch (§5); the three-CPU-tier dequantize/transform
  stage (§5.4); confirmation that decode writes into the game-registered buffer, not a Bink-internal
  one (§5.5); confirmation that Bink's own audio path is live, not dead code (§6.1); a confirmed
  recursive radix-4/radix-2 FFT/DCT transform (§6.4); and a correction of the container/dispatch
  unit's own tentative "compressed vs. raw" framing of the two audio entry points to "gain-ramp mode,
  not compressed-vs-raw" (§6.5). One genuinely new cross-cutting finding not anticipated by the
  scoping note: video decode calls several addresses physically inside the audio core's own address
  range, meaning the two-cores framing is not a clean phase boundary (§7). 14 open items recorded.
  Ghidra-free throughout all three contributing units; no leaked material read or used; FFmpeg not
  consulted. Cleanroom self-check: 0 `FUN_`/`DAT_`/`iVar`-style hits (plain hex throughout).
- **2026-10-08 — `0x1000e560` opened in full (new §1.5a), closing §1.2's own "not opened in depth"
  flag.** REFUTES the working "per-frame offset resolver" name: it is a one-time, whole-table
  monotonicity validation (the direct, sole trigger of "This file has bad frame size data.") plus a
  worst-case 1-second buffer-size scan, run once at `BinkOpen` time, not a per-frame lookup. Hybrid
  calling convention confirmed (4 cdecl-cleaned stack args plus a hidden `EDI` register input never
  saved/restored by the callee). 3 new handle fields added to §1.5 (`+0xc8`/`+0xcc`/`+0x4d4`); two of
  them confirmed to feed the public `_BinkGetSummary@8` export the game calls exactly once, per
  `spec-bink-boundary.md`. Also corrects an upstream address-arithmetic error in how this function's
  address was originally handed off (`0x1000e560` is a plain VA in this DLL's own `0x10xxxxxx` space,
  not a bare RVA needing the image base added again — `0x1001e560` is NOT this function, it lands
  mid-instruction inside the audio core's own transform cluster, §6.4). 3 new OPEN items (§8 item 15).
  Ghidra-free (purpose-built `pefile`+`capstone` against this DLL's own PE); no leaked material read.
  Cleanroom self-check: 0 hits, standard regex. Synced to Team B.

- **2026-10-08 — new §5.3d: Unicorn harness runs real `binkw32.dll` code end to end, root-causes the
  DC-bug, new `spec-bink-video-test-vectors.md` data file** (SPEC TEAM, agent `a8857fae7f7fc9a04`,
  Option B of §5.3c's two continuation options). Real code executed via CPU emulation (not Ghidra, not
  a decompiler) decoded a real keyframe + P-frame of `main_menu_bg.bik`. NOT bit-exact vs. FFmpeg
  (50.4%/51.8% overall byte match) but root-caused, not merely observed: AC/texture is bit-exact on
  every block checked; the entire mismatch is a uniform +128 DC-level offset on roughly half of all
  blocks, traced to a previously-unopened helper, `0x1001a160` — a sign-flippable DPCM/delta-
  accumulator reading from a third, independent bit-reader context, feeding case 5/7's own dedicated
  DC sub-stream. Not fixed (the accumulator's own seed/context-setup call site wasn't reached in time)
  but narrowed to one function with one identified bug mechanism. Also: confirms call 2's
  `record+0x20` (§5.6 item 6a) is NOT a plane write (receives a 0/1 flag value at the pointer-shaped
  slot); confirms `0x100244c0` doesn't write its IDCT result back into the input struct and
  self-re-enters once per block (two-pass row/column butterfly via explicit recursion); corrects a
  prior scratch note's `0x1001cb00` argument map (`[ebp+0x10]`/`[ebp+0x14]` were swapped). ChromaB
  (the 4th of 4 `0x1001a530` calls) never produces real pixel output in this harness — separate,
  unresolved bug. **New data file `spec-bink-video-test-vectors.md`** publishes all 22 real captured
  stage buffers (case-selector scratch, 14 DC+AC coefficient instances, 1 FFmpeg-verified post-IDCT
  block, final Y/chromaA/chromaB plane crops for both frames — chromaB flagged KNOWN INCOMPLETE, not
  presented as valid), hashes independently re-verified by the orchestrating session. Full detail:
  `scratchpad/bink_unicorn_harness_2026-10-08/bink_unicorn_harness_findings_2026-10-08.md`. Cleanroom
  self-check: 0 hits, standard regex, unchanged. Synced to Team B.

- **2026-10-08 — new §5.3e: entropy pre-stage tables + both quant matrices byte-exact published in new
  `spec-bink-static-tables.md`; AC-coefficient mapping CLOSED; DC-bug descriptor construction site
  traced; two fix hypotheses tried and REFUTED** (SPEC TEAM, agent on the Bink entropy-tables/DC-fix
  pass; data re-dumped/SHA-256'd a second time by the editor directly from the agent's saved `.bin`
  captures before publishing, to avoid transcription error). **Closes** the "not transcribed with full
  confidence" caveat on `0x1003bd44`/`0x1003bd54`/`0x1003c140`/`0x1003bd60` (§5.3c) and the AC-coefficient
  byte-offset-to-index-mapping OPEN item (§5.3b: row-major, `struct_byte_offset(k)=2*k`,
  `quant_byte_offset(k)=4*k`, same linear index for both the coefficient struct and the quant matrix).
  **Corrects** §5.3b's "clean linear quantizer-strength progression" claim to "monotonic, not strictly
  linear" (measured per-row ratio ladder given in full); adds a new finding that the DC (k=0) quantizer
  column is byte-identical between the intra and inter matrices, only AC columns differ. **Corrects**
  §5.3d's chromaA channel identity: it matches FFmpeg's V (Cr), not U (Cb) as previously assumed
  (measured: 0.2% match vs. FFmpeg-U, 40.5% vs. FFmpeg-V). **New, not previously documented:** the DC
  DPCM descriptor's construction site (`0x1001a530` at `0x1001a5ae`-`0x1001a5f3`); `0x1001a160`
  re-characterized as one magnitude+sign seed then N-1 deltas per batch, not a pure delta accumulator,
  with no hardcoded bias constant anywhere in its body (checked exhaustively); two concrete empirical
  DC-bug fix hypotheses tested against the harness and both REFUTED (Y's base pointer is already
  correct; patching a `+4` sub-chunk-header skip for chromaA/chromaB made the match rate worse, 40.5%→
  0.1%, despite looking like the obvious fix from a real measured offset coincidence); an untraced lead
  (`param3`'s own `[ebx+0x18]` field resolves to a fixed address `0x4003318c` across all 6 observed
  `0x1001a530` entries, alongside a separate unexplained "fires twice per plane" anomaly, not chased).
  Closes `0x100244c0`'s real pixel-output destination (`[ebp+8]`=dest, `[ebp+0xc]`=pitch). **The DC bug's
  true root cause remains OPEN** — reported honestly, not claimed fixed. **New data file
  `spec-bink-static-tables.md`** publishes all five static table/matrix regions byte-exact
  (hex+SHA-256, Provenance/Index/raw-blob/Changelog convention matching
  `spec-audio-codebook-library.md`). Full detail: `scratchpad/bink_entropy_tables_and_dc_fix_2026-10-08.md`.
  Cleanroom self-check: 0 hits (standard regex + the C-cast-dereference check), both files. Synced to
  Team B.

- **2026-10-08 — new §5.3f: corrects §5.3e's `param3` misattribution (really `param1`, and a dead end),
  fully characterizes `0x1001a160`'s batch/seed/delta structure, and CONFIRMS chromaB's root cause by
  direct instrumentation** (follow-up Unicorn-harness pass, reusing the existing harness and the
  predecessor's own saved disassembly without re-deriving either). **Correction:** §5.3e's "most
  promising untraced lead" (`param3`/`0x4003318c`) was mislabeled — those fields are on `param1`;
  `param3` is referenced once, as a plain bounds check; `0x4003318c` is just the whole-buffer-end bound,
  identical across planes because there's only one frame blob. Closed as a dead end, not chased further.
  **New:** the full two-level batch structure (one N=11-bit absolute magnitude+sign seed via one of two
  per-call-site branches, then M-1 deltas in 8-item RLE chunks); live-verified every dependent field is
  plane-independent, ruling out a per-plane misconfiguration; a new, broader "+4 to the raw hidden-ecx"
  fix hypothesis tried and REFUTED (same chromaA regression as §5.3e's narrower patch). **ChromaB:
  root cause now CONFIRMED, not fixed** — direct instrumentation of the primary case dispatch shows it
  is reached 0/8,160 times for chromaB (vs. 23,914/23,914 Y, 6,042/6,202 chromaA); traced one level
  further to the entropy pre-stage (`0x10019360`) permanently stalling -4 bytes past its own bound from
  its 2nd call onward for chromaB specifically — directly refuting §5.3d's two standing guesses (chroma
  pitch mismatch, `record+0x34` pointer reuse), since no case is ever selected at all. The DC bug's true
  root cause remains OPEN, now a much narrower target (hand-decode one affected batch bit-by-bit against
  its neighbors). Full detail: `scratchpad/bink_dc_bug_rootcause_chromaB_2026-10-08.md`. Cleanroom
  self-check: 0 hits. Synced to Team B.

- **2026-10-09 — §1.6 extended: `D10>=1` per-track header size and frame-0 interior layout, both
  CLOSED; new §5.3h closes §5.3g's own "descriptor B's own downstream consumer" OPEN item** (SPEC
  TEAM, agent `a41f8ff3b44a70765`, routed Bink pair, both via live Unicorn execution of the real
  unmodified decoder — not static reasoning alone). §1.6: the `D10>=1` per-track header is exactly 12
  bytes/track (confirmed on 2 independent real samples by the frame-offset table's own no-gap
  self-consistency check); frame N's own blob does not start with the video span field for `D10>=1` —
  it starts with D10 size-prefixed audio chunks, with the otherwise-unchanged video chunk beginning
  immediately after the last one. Confirmed decisively by live execution: forcing the `D10=0`-style
  reading onto a real `D10=1` file produces the documented LEN>CAP poison branch on the very first
  bitstream read (exactly the desync this item's own task brief described empirically); the corrected
  reading decodes cleanly. New §5.3h: descriptor B's refill call fires unconditionally at bit 1757
  (as hypothesized), but its own LEN-field bit-width formula is genuinely different from descriptor
  A's (`Width/16` vs `Width/8`), only coincidentally producing the same N for this file's width; and
  neither raw-bit checkpoint at bit 1757/2871 is actually a sub-dispatch read — the real block-(0,0)
  dispatch sits 7 more per-row setup stages and 10,716 bits further downstream, reached via
  zero-bit-cost SCRATCH-buffer reads from both descriptors, not a fresh bitstream read. Full detail:
  `scratchpad/bink_d10_and_bit1757_trace_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.
- **2026-10-10 — new §5.3i: full 7-way secondary payload grammar for primary case 1 (secondary 0-6),
  closing Team B's own named decoder blocker** (SPEC TEAM, agent `a0e7757df0cf66e27`). Live-confirmed
  byte-level grammars for secondary=2 (DC+AC dequant/IDCT, DC=118/nibble=2/16x16-output, plus a new
  `0x100247d0` calling-convention finding independently cross-checked against §5.3h's own published
  DC value — exact match) and secondary=3 (flat fill, corrects the prior "~40 locations" estimate to
  an exact 16x16/64-dword count). Corrected the dispatch mechanism for secondary=1/4 (reached via an
  earlier flag-bit short-circuit, not the literal jump-table targets, per live trace). Found and fixed
  a real methodology bug in the inherited scan scripts (`[ebp+8]=0` silently zeroed the Y-plane column
  count), validated by independently reproducing §5.3h's own exact dispatch-count figures after the
  fix. New structural finding: a third shared literal-byte cursor ("descriptor L") feeds every raw
  literal value across secondary=2/3/5, and all three write a uniform 16x16 output footprint — a
  block-type-family-wide convention, not special to any one sub-case. **Honest negative result:**
  secondary=0 was searched for across 20 real frame/file samples (3 files) and found zero times —
  reported as a real negative, not fabricated; its grammar is given at HIGH CONFIDENCE only, by
  structural analogy to its already-confirmed full-resolution twin (primary case 3). Secondary=6 not
  independently re-traced this pass (real occurrences confirmed abundant, carried over at HIGH
  CONFIDENCE). Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — new §5.3j: the IDCT's exact fixed-point rounding sequence, and `0x10024eb0`'s full
  tree-decode algorithm, both at instruction-level precision — resolves Team B's own named
  video-on-screen blocker** (SPEC TEAM, agent `aa6b481ce76a5a113`). Rounding rule CONFIRMED and
  bit-exact-verified: every intermediate dequant/rotation shift (`>>0xb`, 25 sites) is a plain
  truncating shift with zero rounding bias; the ONLY rounding-biased shift anywhere is the final
  per-pixel descale, `(sum+0x7f)>>8` (not `+0x80` — exact ties round down). Verified by an
  independent, from-first-principles Python re-derivation of the full butterfly, run on a real
  captured coefficient set from a block already independently matched against FFmpeg's own decode
  — bit-exact, all 64 bytes, zero divergence; independently re-run by the splicing session itself
  before trusting it, same result. `0x10024eb0` fully characterized: a two-phase breadth-first
  worklist tree build over a scratch buffer reused in place as the final signed-16-bit coefficient
  array, two distinct sign-recovery mechanisms (address-cited), and a real front==tail/level==1
  termination condition (not a hard-coded 63-count) — a reimplementation should drive the same
  worklist condition, not a fixed loop bound. No static table bytes included (cited
  `spec-bink-static-tables.md` by section throughout, per the public-repo exclusion). Open:
  `0x10024b50`'s own reference-add path not independently re-verified against a live P-frame
  example this pass (HIGH CONFIDENCE only); the AC write-out confirmed for k=1 only, k=2..63 carried
  over from the already-confirmed read-side finding; the 24 auxiliary dword fields' semantic roles
  not chased. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
