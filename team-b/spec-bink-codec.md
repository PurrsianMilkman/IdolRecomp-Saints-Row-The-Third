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
#3-18 (the 16-loop body) feed a separate 16-entry sub-table array ~~; call #19 (the loop's own "+1 more"
call) feeds a further distinct structure; calls #20-23 each feed one of 4 more mutually-distinct,
single-call scratch structures. None of the other 22 touch descriptor A — consistent with, not a
reopening of, this section's own standing "23 sibling calls... downstream consumers not traced" item
for the other 5 structures.~~ **DOWNSTREAM CONSUMER FOUND, 2026-10-11, orchestrator-routed (SPEC
TEAM, agent `a1972571f737cc99e`, independently re-verified by direct query before splicing — the
live `arg3` pointer match and the exact `A_array`/`LUT_ptr_array` contents both matched the raw
capture exactly): calls #3-18's 16-entry sub-table array IS `0x100195b0`'s own two LUT tables,
`arg3+0x100` (16 dwords, max Huffman code length per class — the "A" array) and `arg3+0x140` (16
dwords, each a pointer into the static per-nibble packed-byte LUT bank at `0x1003bd60`+) — the
exact structure §5.3s's own "two LUT-table tree lookup" left unspecified. Confirmed by a live
pointer match: the writer loop, `0x10019300` (called once per block-row from `0x1001a530`'s call
site `0x1001aa11`, itself part of this same 23-call sequence — NOT a separate mechanism), and the
`0x1001aaf0`-dispatched `0x100195b0` call both operate on the identical `arg3` address in the
same live run. `0x10019300`'s own loop body (16 iterations) calls `0x10018ca0` exactly as this
section's own `build_table` algorithm already specifies — each call unconditionally sets
`classbyte_out = CLASSBYTE[nibble]` (→ `arg3+0x100[i]`) and `LUT_ptr_out = 0x1003bd60 +
OFFSET[nibble]` (→ `arg3+0x140[i]`) from its own 4-bit nibble, then optionally reads more bits to
build an accompanying `table16_out` permutation table via the same 5 construction strategies
already fully specified above — no new bit-reading algorithm, this is the SAME `build_table` call
shape, just a previously-untraced consumer of its output. **Net answer: these arrays are rebuilt
fresh from the bitstream every block-row (NOT static DLL data) — only the packed-byte LUT
*contents* each pointer resolves to (the `0x1003bd60` bank) are static, already-published
material; the per-row *selection* of which pointer/class-length goes where is bitstream-derived.**
A persistent class index, `arg3+0x180`, is reset to 0 by the same `0x10019300` loop and carried
across `0x100195b0` calls within the row (matching §5.3s's own "running value… carried into the
next call" note). **Implementer takeaway for a general decoder: run the 16-call
`0x10018ca0` table-builder exactly as `build_table` specifies, writing 3 parallel banks
(`table16[16][16]`, `A[16]`, `LUT_ptr[16]`) into row-local scratch; maintain `cur_class` across
`0x100195b0` calls, reset to 0 at each row's table rebuild; the tree-decode loop indexes all
three banks by `cur_class`.** Remaining OPEN: call #19's and calls #20-23's own downstream
consumers (still not traced); whether `0x10018ca0`'s construction strategies ever hit
`nibble==0`/explicit-prefix for these specific 16 calls on rows other than Y row 0 (only that one
instance was traced); the semantic meaning of "class"/why 16 classes, and `table16`'s own role in
`0x100195b0`'s third tree level (traced to the instruction, not semantically interpreted).

- **Evidence:** live Unicorn capture against the unmodified `binkw32.dll`/`main_menu_bg.bik`
  proving the identical `arg3` address (`0x303f4c08`) is written by `0x10019300` and read by the
  `0x1001aaf0`-dispatched `0x100195b0` call; full static disassembly of both functions
  cross-checked instruction-by-instruction against the live register/memory values.
  Independently re-verified before splicing — the raw capture output's `arg3` match and array
  contents (`A_array=(4,6,6,6,6,5,5,5,5,5,5,5,5,5,6,4)`, `LUT_ptr_array` resolving exactly to
  `0x1003bd60`-based addresses) matched the agent's claims exactly.
- **Confidence:** CONFIRMED that the arrays are per-row/bitstream-built (not static), that the
  writer is `0x10019300`→16×`0x10018ca0`, and that the exact same `arg3` address is shared
  between writer and `0x100195b0` (live pointer match, not inference). CONFIRMED the array
  shapes from live-captured contents matching the disassembled access pattern. PLAUSIBLE
  (inherited from this section's own already-CONFIRMED `build_table` pseudocode, not
  independently re-walked this pass) that no construction strategy ever overrides
  `classbyte_out`/`LUT_A_ptr_out` after the initial nibble-driven assignment.
- **Unknowns:** the 2-of-16 nibble values missed by this pass's own probe (cosmetic — the
  resulting array contents were still captured correctly post-loop); whether chroma planes or
  later rows ever hit different construction strategies for these specific 16 calls (only Y row
  0 traced); call #19's and calls #20-23's own downstream consumers.

Full per-call field-by-field detail (every nibble/flag/mode/pairwise-bit/
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
~~**CONFIRMED: descriptor L feeds the DC literal word (secondary=2), the flat-fill byte
(secondary=3), and both pattern-literal bytes (secondary=5) — i.e. every "raw literal value" read
by any primary=1 case body comes from this one shared byte stream.**~~ **CORRECTED 2026-10-11,
§5.3y (SPEC TEAM, agent `a40f050aea077ad93`, independently re-verified by live query): descriptor
L's BOUND-check variable (`[ebx+0x10]`/`[ebp-0x210]`) genuinely is shared and byte-identical
across secondary=2/secondary=3 — that half of the claim stands. But the actual literal-byte READ
cursor is NOT one shared pointer: secondary=2's DC-word read (`0x1001b328`) uses a cursor cached
at `[ebp-0x13c]`; secondary=3's fill-byte read (`0x1001b3f8`) uses a cursor cached against
`[ebp-0x34]` — two physically different addresses, ~5,008 bytes apart in one live capture, not
adjacent sub-ranges of one obviously-shared small buffer. "One shared byte stream" overclaimed
the sharing: the bound/limit tracking is shared, the actual byte source is NOT (at least between
secondary=2 and secondary=3 — secondary=5's own pattern-literal cursor was not re-checked this
pass). A reimplementation that modeled DC-literal and fill-byte reads as pulling from one shared
cursor/array would diverge from the real DLL.** OPEN, not chased this pass: whether descriptor L
is itself filled by its own `0x10019360`-style pre-stage call (by analogy to A and B) or reads raw
un-decoded bitstream bytes directly — not traced upstream of its own cursor/bound pair this pass;
also now open, per the above correction: what sits further upstream of secondary=2's and
secondary=3's own separate literal-cursor refill-base pointers, and whether secondary=5's cursor
is a third separate address or genuinely shares with one of the other two.

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

**Upsample rule PINNED DOWN, 2026-10-12 [orchestrator-routed, for Team B's block-grid task]: plain
2x2 nearest-neighbor block replication, NOT interpolated.** `dest[2r+i][2c+j] = core[r][c]` for
`i,j ∈ {0,1}` — both horizontal AND vertical doubling of each source pixel into a 2x2 destination
block. Verified against real ffmpeg-decoded pixels for idx3 (secondary=2, DC=199): published core
row0 `[24,25,24,23,24,25,24,24]` matches real dest row 0 `[24,24,25,25,24,24,23,23,24,24,25,25,
24,24,24,24]` exactly (`core[i]` at `dest[2i]`/`dest[2i+1]`); dest row 1 is identical to row 0
(confirms vertical doubling, not previously nailed down); dest rows 2/3 are identical to each
other but differ from rows 0/1 (consistent with core row 1 feeding them the same way).

**Full footprint table, all primary/secondary cases (new column; mechanism/source-res columns
reproduce already-published facts, not re-derived):**

| Primary | Secondary | Mechanism | Source res | Write footprint | Upsampled? |
|---|---|---|---|---|---|
| 1 | 0 (giant1) | RLE+permutation | half-res 8x8 (by analogy — 0 real occurrences found in any sample, never independently measured) | 16x16 | Yes, assumed same 2x2 NN rule, not independently confirmed |
| 1 | 1 or 4 | Flag-bit short-circuit, mark skip | N/A — no pixel write | N/A | N/A |
| 1 | 2 | DC+AC dequant/IDCT | 8x8 (real DCT) | 16x16 | **Yes — CONFIRMED, 2x2 NN, exact (this pass)** |
| 1 | 3 | DC/flat fill | N/A (direct broadcast) | 16x16 | No — native 16x16, nothing to upsample |
| 1 | 5 (giant2) | TwoLiteralPattern | N/A (direct pattern write) | 16x16 | No — native 16x16, same as secondary=3 |
| 1 | 6 | Literal/raw copy, dual-row duplication | raw bitstream bytes, 8x8-equivalent | **16x16, CONFIRMED same 2x2 NN as secondary=2/3/5 (this pass)** | **Yes — CONFIRMED, 2x2 NN, exact (this pass, see addendum below)** |
| 5 / 7 | — | Real 8x8 IDCT (`0x100244c0` intra / `0x10024b50` inter), distinct from primary=1's whole family | 8x8 | **8x8, no upsample** — different block-type tier entirely | No |

Sent to `team-b-fd`.

**secondary=6 footprint/bias CLOSED, 2026-10-12 [orchestrator-routed follow-up to the table
above]: confirmed live, real occurrence #1 of 61 in `main_menu_bg.bik` frame0, Y-plane block
(row=0, col=224).** Same 2x2 nearest-neighbor upsample as secondary=2/3/5 (rows pair up exactly —
`row0==row1`, `row2==row3`, etc. — and values within each row pair up too, confirming both axes
doubled from an 8x8-equivalent source). **Also needs the same `(int8_t)+128` bias as secondary=3,
NOT secondary=2's "no bias" rule** — makes sense structurally since secondary=6 copies/repacks raw
literal bytes with zero dequant/IDCT in between, same as secondary=3's flat fill. Checked
numerically against real ffmpeg pixels at the same location: raw byte 153→signed −103→+128=25
matches exactly; 154→26, 158→30, 159→31 all match too, across the full 16x16 block, no exceptions.
Rule: `pixel = (int8_t)raw_byte + 128`. secondary=0 remains a confirmed negative — scanned ~110
frames across 5 files total (`main_menu_bg.bik`, `logo_amd_hd.bik`, `logo_thq_and_voli_hd.bik`,
`kinzie_typing.bik`, `01_crawl.bik`), zero `(primary=1,secondary=0)` dispatch events anywhere.
Script: `probe_sub6_live.py` in `scratchpad/bink_entropy_prestage_algorithm_2026-10-08/`.

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

**ADDENDUM, 2026-10-12 [orchestrator-routed, after flat-fill promotion: idx1/idx2's fill byte
`0x99` (153) doesn't match the real ffmpeg-decoded pixel value there (~24-25)]: the written byte
is NOT the raw pixel value — the native read (`movzx`, unsigned) must be reinterpreted as a
SIGNED int8 and biased by +128 before storing.** `pixel = (int8_t)fill + 128`. Confirmed exact:
`0x99` as signed int8 = `-103`; `-103 + 128 = 25`. Checked against the real ffmpeg-decoded frame0
Y plane at both idx1 `(0,16)` and idx2 `(0,32)`: both blocks are uniformly **25** across all 256
pixels, exact match, no rounding slop. The disassembly's own `movzx` at `0x1001b3f8` only widens
the byte unsigned — the sign-reinterpretation + bias is a separate step a reimplementation must
add explicitly, likely the same residual-around-neutral-gray convention the secondary=2 DC/AC path
already needs for its own DC value (not independently re-checked against secondary=2's own write
site this pass, but a strong, structurally-motivated lead). Sent to `team-b-31`.

**ANSWERED, 2026-10-12, same thread: secondary=2's own DC path needs NO such bias — it is already
fully correct.** Re-verified against the real ffmpeg-decoded ground truth directly: idx0 `(0,0)`
is uniformly 24, idx3 `(0,48)` and idx4 `(0,64)` both match their already-published core IDCT rows
exactly after the documented 2× horizontal upsample (e.g. idx4's core row `[24,22,25,29,29,28,
29,24]` upsamples to `[24,24,22,22,25,25,29,29,29,29,28,28,29,29,24,24]`, byte-for-byte identical
to the real pixels) — matching §5.3v's own independent finding (DC=118, AC k=8=-1/k=16=-1 →
dequant 6293/-102/-118 → IDCT → pixel(0,0)=24, both the real DLL and an independent Python
re-derivation agree). No `+128` or any bias constant appears anywhere in the dequant
(`sar(raw*quant,0xb)`) or final IDCT/rounding (`sar(sum+0x7f,8)`) chain (already exhaustively
enumerated, §5.3j). **The two mechanisms are genuinely different, not two instances of one shared
bias convention:** secondary=2's DC is used directly, unsigned, as input to the full dequant+IDCT
chain, whose own arithmetic naturally produces correctly-scaled output with no further step;
secondary=3's flat-fill byte bypasses dequant/IDCT entirely and writes straight to the pixel
buffer, which is why IT needed the signed-reinterpret+128 step — there's no transform in between
to absorb the level-shift on that path. If idx0/3/4 are still wrong in a given reimplementation,
the bug is not a missing bias here.

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
`0x10024eb0`-`0x100258a8` (the tree decoder's real, full extent — one function, **CORRECTED 2026-10-11**:
2,552 bytes (`0x100258a8-0x10024eb0=0x9f8`), not 14,840 as this sentence originally said — the 14,840
figure was a documentation error, caught by a fresh independent `capstone` disassembly of the exact
same address range (`scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/`, 741 real instructions decoded
cleanly start-to-end with zero desync, averaging ~3.4 bytes/instruction, consistent with normal x86
code density and inconsistent with the old 14,840-byte claim), confirmed by the single `ret` at
`0x100258a8` and no other `ret`/function-prologue in the whole range. Scripts and
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
    // CORRECTED 2026-10-11, this line was imprecise (see below): there are TWO distinct
    // single-bit gates, not one -- Gate A (every iteration, every nonzero-tag node, decides
    // whether the node does anything at all) and Gate B (reached only from case 0/case 2, up
    // to 4 unrolled copies, resolves a coefficient now or defers it).
  }
  bias_mask >>= 1; level--;                                 // 0x10025495-0x1002549b
} while (level > 1);                                        // 0x1002549c-0x1002549f
```

**CORRECTION, 2026-10-11 (orchestrator/Team B-routed, top video-decode blocker — see the new
worked example below for full detail): the "read the next node's is-leaf flag" line above was
imprecise about both sequencing and destination, and that imprecision was the direct root of a
real Team B decode bug (94 bits consumed instead of 17, 14 spurious coefficients, tree shape a
pure function of the seed bytes with zero bitstream influence).** The real mechanism has two
distinct single-bit gates:

- **Gate A** — read every main-loop iteration, for the CURRENT node at `front` (not a "next"
  node), BEFORE `sel` is even computed (`0x10024fdb`-`0x10024ffd` in Phase 1; the structurally
  identical Phase-2 copy at `0x100254db`-`0x100254f7` is the one actually exercised for small
  trees, see below). Exactly 1 bit, unconditional for any node whose tag is nonzero. It writes
  nothing — it only gates control flow: bit clear → skip the `sel` switch entirely for this node,
  advance past it, continue; bit set → compute `sel = tag & 3` and dispatch via the jump table.
  **This is the actual entropy-coding bit a port must implement — a port that gets case 0/1's tag
  arithmetic right but never implements Gate A will produce the exact seed-byte-determined tree
  shape Team B measured.**
- **Gate B** — a chained "resolve now vs. defer" test, reached only from case 0 and case 2 (NOT
  case 1, which returns straight to the loop bottom without consuming any bits at all — confirmed,
  `0x10025038`-`0x10025082` is pure arithmetic: `tag' = (tag>>2)*4+2` written in place, then 3
  children pushed at `tag'' = (tag>>2)*4+0x12/+0x22/+0x32`, then `jmp 0x10025489` straight to the
  loop bottom). Gate B's shared continuation entry is `0x100250a0` (Phase 1) / `0x10025577` (the
  Phase-2 sibling); up to 4 unrolled 1-bit copies run per visit. Each copy: bit set → defer
  (decrement a SEPARATE backward-growing deferred-worklist pointer at `[ebp-0x98]`, distinct from
  `tail`/`[ebp-0xa0]`, and write a synthetic `sel==3`-tagged byte there) and loop to the next
  unrolled copy; bit clear → resolve immediately via a second mechanism (one more bit, the sign)
  and write the final signed coefficient into the scratch word-array at index `cat = tag>>2`
  cached before the chain started — never into `*front` and never into any "next" node. Case 0
  itself (`0x10025018`-`0x10025036`) is also pure arithmetic and consumes zero bits in that address
  range — it rewrites its OWN slot's tag in place (`tag' = (tag>>2)*4+0x11`, written to `*front`,
  not a new `tail` slot) and falls straight into Gate B's chain in the SAME visit (`front` is never
  advanced by case 0 itself), unlike case 2 which explicitly advances `front` before falling into
  the same shared continuation.

**Also corrected: Phase 1's BFS loop is conditionally SKIPPED entirely for small trees, not merely
"drained."** For a tree whose Phase-0 level field is `A≤1`, `0x10024f92`'s own `cmp esi,1; jbe
0x100254ab` jumps straight past the whole `0x10024fa0`-`0x10025495` Phase-1 loop into what Part 2
above labeled "Phase 2" — which is a structurally near-identical sibling loop (same Gate-A idiom,
same case 0/1/2/3 shapes, its own jump table at `0x100258bc`) that runs directly on the 6 literal
seed bytes. This is why block (0,0)'s own real decode (see the worked example below) never visits
the literal `0x10025018`/`0x10025038` addresses at all — the actual case-0/1 arithmetic for small
trees runs through Phase 2's own mirror arms (`0x10025512`/`0x10025522`/`0x1002556a`/
`0x10025716`), which are the SAME shapes at different addresses, not a different mechanism.
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
   `(raw & (bias_mask-1)) | bias_mask`), THEN the sign is recovered ~~by testing that SAME masked value
   against the level's own bias mask again~~ **[2026-10-10, CORRECTED — see Part 3 below: the raw,
   pre-mask value is snapshotted to its own stack slot at `0x10025440` BEFORE the mask/OR runs, and the
   sign test at `0x10025454` reads THAT snapshot, not the post-mask/OR'd value — testing the masked
   value, as this sentence originally claimed, would be tautological (its bias-mask bit is always set
   by the preceding OR), which is exactly the bug a prior version of this prose had.]** — if the test
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
semantic role of the 24 auxiliary dword struct fields at `0x04`-`0x7c`; ~~whether Phase 2 is reachable with
real content in this specific file (not tested live this pass, only disassembled).~~ **RESOLVED
2026-10-11: YES — Phase 2 is not just reachable, it's the ONLY path exercised for any tree with
level≤1 (level=1 for block (0,0)'s own real decode, confirmed live) — see the new worked example
below.**]**

### 5.3w Golden-trace worked example: the AC-tree's Gate A/Gate B bit mechanism, full 17-bit trace for frame 0/Y-plane/block (0,0) — resolves Team B's top video-decode blocker (2026-10-11)

**Scope.** Direct follow-up to the Part 2 correction above. Team B independently confirmed dequant
and IDCT bit-exact against §5.3v's own published values; the remaining bug was entirely inside
the AC-tree decode, and Team B's own measurement (child tags for case 0/1 nodes coming out as a
pure, bitstream-independent function of the 6 seed bytes) pointed squarely at the never-modeled
Gate A mechanism documented above. Live Unicorn capture against the unmodified DLL, the exact same
block §5.3v already covers — entry/exit bit-reader state confirmed byte-identical to §5.3v's own
published numbers (entry: accum=`0x000007a1`, bits_avail=13; exit: accum=`0x07364522`,
bits_avail=28), confirming this is the same 17-bit call.

**Full bit-by-bit trace** (bit numbers are within this one call; bits 1-4 are the Phase-0 level
field):

- **Bits 1-4** — level field, value **A=1** (no refill needed, bits_avail=13≥4 at entry). Since
  A≤1, Phase 1's BFS loop is skipped entirely (per the Part 2 correction above) — every remaining
  bit in this call runs through Phase 2's own mirror arms, operating directly on the 6 literal
  seed bytes (`0x10`, `0x60`, `0xb0`, `0x07`, `0x0b`, `0x0f`).
- **Bit 5** — Gate A, seed slot 0 (tag=`0x10`, category=4) → **0** → inert, skip.
- **Bit 6** — Gate A, seed slot 1 (tag=`0x60`, category=24) → **1** → dispatch, `sel=0` (case 0):
  rewrites slot 1's own tag in place to `category*4+0x11 = 0x71` (pure arithmetic, no bit), falls
  straight into the Gate B chain in the same visit.
- **Bit 7** — Gate B copy #1 → **0** → resolve now via the sign mechanism.
- **Bit 8** — sign bit → **1** → **write coefficient-array slot [category=24] = −1**.
- **Bit 9** — Gate B copy #2 → **1** → defer: push synthetic category-25 entry onto the backward
  deferred list (never read again this call — dead work, harmless).
- **Bit 10** — Gate B copy #3 → **1** → defer: push synthetic category-26 entry (also never read
  again).
- **Bit 11** — Gate B copy #4 → **1** → defer: push synthetic category-27 entry (also never read
  again). Control returns to slot 1's own address; `front` was never advanced by case 0 itself.
- **Bit 12** — Gate A, re-visiting slot 1 (now tag=`0x71`, category=28) → **0** → inert, slot 1
  finally retired.
- **Bit 13** — Gate A, seed slot 2 (tag=`0xb0`, category=44) → **0** → inert, skip.
- **Bit 14** — Gate A, seed slot 3 (tag=`0x07`, category=1) → **0** → inert, skip. (First dword
  refill of this call happens here — `bits_avail` hits 0, a fresh dword is pulled from the
  cursor.)
- **Bit 15** — Gate A, seed slot 4 (tag=`0x0b`, category=2) → **1** → dispatch, `sel=3` (case 3,
  the unconditional sign-read case): advances past this slot as part of resolving.
- **Bit 16** — case 3's own sign bit → **1** → **write coefficient-array slot [category=2] = −1**.
- **Bit 17** — Gate A, seed slot 5 (tag=`0x0f`, category=3) → **0** → inert, skip. Worklist front
  now equals tail — the seed walk ends, bit-reader state is written back, matching §5.3v's exit
  state exactly.

**Net result: exactly 2 nonzero leaves, both magnitude 1, both negative** — matching §5.3v's
ground truth of 2 nonzero AC coefficients (k=8, k=16, both −1) at the end of this 17-bit call.

**The category-index→raster-k remap — RESOLVED 2026-10-11, same day follow-up (SPEC TEAM, agent
`a57fd1bde57d6f0e6`, independently re-verified by direct query before splicing — all 3 cited
`mov` pairs, and the index arithmetic connecting them to the already-established scratch-array
formula `word[ebp+idx*2-0x14c]`, matched exactly; the live Unicorn cross-check's category
2→k=8/category 24→k=16 result independently re-derived from the static offsets alone, not just
taken on trust).** The remap is NOT a runtime table-lookup instruction — there is no indexed load
anywhere in this 14,840-byte function outside the two already-documented 4-entry dispatch jump
tables (`0x100258ac`/`0x100258bc`). It is a fixed, compiler-unrolled sequence of ~32 `mov` pairs
in the write-out block (`0x10025777`-`0x1002589c`), each copying two adjacent scratch-array words
(category `N`, `N+1`) to two adjacent output-struct words (raster `k`, `k+1`) at literal immediate
offsets baked in at compile time — e.g. `0x1002577d`/`0x10025783`:
`ecx=word2[ebp-0x144]; [eax+4]=ecx` (categories 4,5 → k=2,3); `0x100257ab`/`0x100257ba`:
`edx=word2[ebp-0x148]; [eax+0x10]=edx` (**categories 2,3 → k=8,9** — confirms this block's own
category-2 write lands at k=8, independently re-derivable from the scratch-array index formula
alone: `ebp-0x148` = `idx*2-0x14c` solved for `idx=2`, and `eax+0x10` = byte offset 16 = `k=8`);
`0x100257cf`/`0x100257de`: `edx=word2[ebp-0x11c]; [eax+0x20]=edx` (**categories 24,25 → k=16,17**
— `ebp-0x11c` solves to `idx=24`, `eax+0x20` = byte offset 32 = `k=16`). One further pair handles
category 1→k=1 as an explicit 16-bit copy. Because this is a fixed fact about the DLL's own
compiled code (literal immediate offsets), not a DLL data table, it belongs in this codec spec
cited by address — NOT in `spec-bink-static-tables.md` (which holds only raw byte-read data
tables; this has no bytes to cite, only instruction addresses).

**Full reconstructed category(1-63)→raster-k(1-63) permutation**, derived mechanically from all
31 dword pairs + the 1 word pair, independently confirmed bijective (domain and range both exactly
`{1..63}`, no collisions, no gaps):

```
cat: 1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
 k : 1  8  9  2  3 10 11  4  5 12 13  6  7 14 15 20 21 28 29 22 23 30 31 16 17 24 25 32 33 40 41
cat:32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63
 k :34 35 42 43 48 49 56 57 50 51 58 59 18 19 26 27 36 37 44 45 38 39 46 47 52 53 60 61 54 55 62 63
```

Converting `k` to `(row,col)` shows this is a clean, recursive quad-tree/Morton-style traversal
over the 8×8 block's quadrants — consistent with (not coincidental to) the tree's own
`category' = category*4 + 0x11/0x12/0x22/0x32` quad-split arithmetic §5.3j Part 2 already
documents; each 4-way split's children land in a consistent quadrant pattern of the final block.

**[CONFIRMED — live Unicorn capture against the unmodified DLL (full 336-instruction trace,
bounded to this one call via the project's own established call/return-boundary hook pattern),
cross-checked against independent static disassembly of the case 0/1/2/3 handlers (exact match),
and the category-index→k remap independently re-verified by direct query (3 of 32 `mov` pairs
spot-checked, plus the full index arithmetic re-derived from the already-established scratch-array
formula) before splicing.]**

#### Part 3 — resolving Team B's two sign/width judgment calls, instruction-level (2026-10-10)

**Scope.** Direct response to Team B's reported `main_menu_bg.bik` frame-0 desync (5 blocks decode,
6th diverges). Re-opens the exact byte ranges Part 2's own "Sign recovery" subsection already cited,
at the instruction level, to settle two questions that subsection's prose left implicit (and, as it
turns out, got wrong on one point — see the strikethrough correction in Part 2 above).

**Re-disassembly of the Phase-1 case-3 immediate-resolution site, `0x10025439`-`0x10025466`
(capstone/pefile, static, `binkw32.dll`, no decompiler) — CONFIRMED, independently re-verified
against the raw bytes:**
```c
// 0x10025439: ecx = [ebp-0xb8] (bias_mask); 0x1002543f: ecx--      (ecx = bias_mask-1, magnitude mask)
// 0x10025440: [ebp-0xb4] = ebx                 <-- RAW pre-mask value SNAPSHOTTED HERE, into its OWN
//                                                   stack slot, BEFORE any masking/OR touches ebx
// 0x10025446: ebx &= ecx                       (mask ebx to its low (level-1) bits)
// 0x10025448: ebx |= [ebp-0xbc] (bias_mask)    (force ebx's top bit on -> candidate magnitude; this
//                                                   ebx is NOT the operand used for the sign test)
// 0x1002544e: ecx = [ebp-0xb8] (bias_mask, reloaded)
// 0x10025454: test [ebp-0xb4], ecx             <-- tests the RAW SNAPSHOT from 0x10025440, NOT the
//                                                   masked/OR'd ebx -- two different stack locations,
//                                                   read at different times
// 0x1002545a: [ebp-0xa8] = ebx (store final magnitude)
// 0x10025460: ecx = ebx
// 0x10025462: je 0x10025466 (bit clear -> positive, ecx unchanged)
// 0x10025464: neg ecx       (bit set -> negative)
```
Byte-identical sequence re-confirmed at the Phase-1 case-2-chained instance, `0x100250d2`-`0x100250f9`
(reached immediately after `0x10025087`'s leaf-marking code, NOT deferred to Phase 2 despite Part 2's
own "cached... for the magnitude/sign read below" phrasing — "below" means a few instructions further
down in the SAME Phase-1 code path, not literally Phase 2).

**Judgment call 2 (extra-bits width + the apparent tautology), RESOLVED — CONFIRMED, supersedes the
imprecise wording Part 2 originally had:** the sign test's real operand is the **pre-mask raw
extra-bits snapshot** (`[ebp-0xb4]` and its sibling slot in the unrolled copy), captured in its own
stack slot *before* the `and`/`or` sequence runs — not the post-mask/post-OR value that ends up in
`ebx`. Testing the post-OR value against its own just-forced-on bit would indeed be the tautology
Team B correctly flagged (always "set" → always negate); that is NOT what the real code does. The
actual mechanism tests the ORIGINAL, unmodified bit at position `level-1` of the raw field — exactly
the bit the subsequent `and`/`or` sequence separately forces to 1 in the magnitude copy. **Width,
CONFIRMED in both re-disassembled instances: exactly `level` raw bits** (the outer level counter's
current value at the moment this leaf resolves — the same count defining `bias_mask = 1 <<
(level-1)`), with no category- or tag-dependent variation found in either instance. It is literally
the same raw field read once and inspected twice: once with the top bit forced on (magnitude), once
in its untouched original form (sign).

**Re-disassembly of the Phase-2 `sel==2` dispatch arm, `0x1002556a`-`0x10025637`
(capstone/pefile, static) — CONFIRMED, locates mechanism 2's real home:**
```c
// 0x1002556a: shr ecx,2; byte[esi]=0; esi++        -- marks leaf (same shape as Phase-1 case 2)
// 0x10025577-0x10025590: read one new bit (single-bit refill idiom, Part 2)
// 0x10025593: je 0x100255ae   -- bit CLEAR: push a +3-tagged node onto front2/[ebp-0x98]
//                                 (0x10025595-0x100255aa) -- NOT a leaf resolution, a further
//                                 deferred sub-expansion
// 0x100255ae-0x100255c6: bit SET: read ONE MORE bit (same refill idiom)
// 0x100255c7: and esi,1; neg esi; or esi,1          -- MECHANISM 2 fires HERE: that second bit IS
//                                 the sign, magnitude is always exactly 1
// 0x100255cf: mov word[ebp+ecx*2-0x14c], si          -- store the final signed coefficient
```
Pattern repeats identically for the next worklist slot at `0x100255f1`-`0x10025630`.

**Judgment call 1 (sign-mechanism selection), RESOLVED — CONFIRMED, corrects Team B's case-2→
mechanism-1/case-3→mechanism-2 mapping:** mechanism selection is **not** a direct function of which
Phase-1 case (2 vs 3) a leaf came from. Concretely:
- Phase-1 `case 2` (`sel==2`, leaf marked, falls straight into the inline continuation at
  `0x100250d2`) uses **mechanism 1**, not mechanism 2 — Team B's "deferred vs inline" guess happened
  to land on the right case label for the wrong reason (it is not deferred at all).
- Phase-1 `case 3` (`sel==3`, `0x100253cb`) **also** uses mechanism 1 — Team B's mapping of case 3 to
  mechanism 2 is **wrong**; case 3 never reaches `0x100255c7`-style code.
- **Mechanism 2 belongs to Phase 2's own `sel==2` re-dispatch arm specifically**, and only on the
  sub-branch reached after a *second*, runtime bit-driven categorization test (`0x10025593`'s `je`)
  comes up "bit set" — the "bit clear" sibling branch instead re-queues the node with a `+3` tag
  rather than resolving it via either sign mechanism.

**The real selection rule: mechanism 1 for every Phase-1 immediate resolution (both case 2 and case
3); mechanism 2 only for Phase-2 `sel==2` leaves whose own extra categorization bit selects the
"magnitude is exactly 1" sub-case.** A conformant reimplementation must replicate this categorization
bit, not branch on case-2-vs-case-3 alone.

**Phase-2 `sel==1` and `sel==3` arms, instruction-level, CONFIRMED (2026-10-10 follow-up) — resolves
the item previously left OPEN above.** Both independently re-disassembled (`disasm_util.dump_range`,
matched exactly):

- **`sel==1` (target `0x10025522`-`0x1002555f`) does NO sign recovery of either kind — it is a pure
  structural requeue**, the same shape as Phase 1's own `case 1` ("4-way split"), just running inside
  Phase 2's own walk: `0x10025522`-`0x1002552e` writes `tag' = category*4+2` in place at the current
  node slot (note `(category*4+2) & 3 == 2`, so the node is simply re-tagged with a `sel==2` shape for
  later processing, not resolved to a value); `0x10025530`-`0x1002555f` pushes exactly THREE new
  children onto `tail`, tags `category*4+0x12`/`+0x22`/`+0x32` (all also `&3==2`). No bit-refill idiom,
  no `test`, no `and`/`neg`/`or` appears anywhere in this arm — pure tag arithmetic, falling through
  into the shared front/tail loop-control tail (`0x1002575a`-`0x1002575c`) that `sel==3` below also
  falls into.
- **`sel==3` (target `0x10025716`-`0x1002575c`) resolves UNCONDITIONALLY via mechanism 2 — no gating
  categorization bit at all, unlike `sel==2`'s own arm:** `0x10025716`: `ecx = tag>>2` (cached as the
  coefficient-array index); `0x10025719`-`0x10025731`: one single-bit read via the standard refill
  idiom; `0x10025732`-`0x10025737`: `and esi,1; neg esi; or esi,1` — mechanism 2's exact pattern. This
  occurrence at `0x1002573a` is one of the four further copies Part 2's own "Sign recovery" subsection
  already enumerated by address — this pass confirms which dispatch arm that already-cited address
  belongs to, it is not a newly-discovered site. `0x1002573a` stores the result (magnitude always 1,
  sign from the one bit) into the coefficient word array, then falls through to the same shared
  front/tail advance-and-loop-check code the `sel==1` arm jumps into above.

**Updated selection rule, no longer OPEN:** mechanism 1 is used ONLY by Phase 1's immediate
resolutions (`case 2`/`case 3`). Mechanism 2 has two homes, both in Phase 2, both address-confirmed:
the `sel==2` arm's gated sub-branch (bit-set case, `0x100255c7`) and the `sel==3` arm unconditionally
(`0x1002573a`). The `sel==1` arm reaches neither mechanism — it only expands the tree further (3 new
`sel==2`-tagged children plus one `sel==2`-retagged resolution of the current node), deferring actual
value/sign resolution entirely to whatever later visits those freshly-tagged `sel==2` nodes.

**Numeric cross-check against the already-FFmpeg-verified block (consistency check, not a live
bit-trace).** The block at `Y_BUF+80` (frame 0, row 0, col 80), already independently verified
byte-for-byte against FFmpeg in Part 1 above, has DC=51 and nonzero AC at k=1,2,8,9,10,17 =
-3,-1,-3,1,1,1 (`idct_col80_coeffs.bin`). Every nonzero AC value has magnitude 1 or 3 — consistent
with either a magnitude-1 leaf resolved via mechanism 2 (values -1,1,1,1) or a small multi-bit leaf
resolved via mechanism 1 with `level` small (values -3,-3). **OPEN, not closed this pass:** a clean
dynamic bit-trace tying this specific block's own `0x10024eb0` call to the rule above end-to-end — an
attempt extending `scratchpad/bink_unicorn_harness_2026-10-08/decode_frame0.py` with an entry/exit
hook on `0x10024eb0` was made, but the capture was unreliable (apparent self-re-entrant call behavior
at this function's own entry, the same shape as `0x100244c0`'s already-documented self-recursion,
confused the hook's stack bookkeeping on the second "entry"). The phase/sub-branch rule itself is
CONFIRMED independent of that gap, from direct static disassembly of both code paths above, not from
this numeric cross-check.

**[CONFIRMED by direct re-disassembly, all questions: (1) mechanism 1 handles every Phase-1
immediate leaf resolution (case 2 and case 3 alike); mechanism 2 has two homes, both in Phase 2 —
`sel==2`'s gated "magnitude-exactly-1" runtime sub-branch, and `sel==3` unconditionally; `sel==1`
reaches neither mechanism, it only expands the tree further (pure tag requeue, deferring resolution
to freshly-tagged children). (2) mechanism 1's raw field width is exactly `level` bits in both
instances checked, and its sign test's real operand is the pre-mask raw snapshot, not the
post-mask/OR'd magnitude value. OPEN: a clean live bit-trace tying one specific real block's leaves
to this rule end-to-end (blocked on the same self-re-entrancy capture issue noted above).]**

### 5.3k Per-row DC array capture confirms the +128 split is a decode-side (batch/delta) pattern, not a uniform post-array bias — extends `0x1001a160`'s "called per row" finding to the whole frame (2026-10-11)

**Scope.** Direct follow-up to §5.3h/§5.3d, live Unicorn execution (unmodified `binkw32.dll`,
unmodified `main_menu_bg.bik`, same harness family), hooking `0x1001a160`'s entry and the DC-literal
read site `0x1001b311` (`mov cx,[eax]`) across the ENTIRE frame-0 decode via `call_bink_cb`, not a
single sampled row.

**`0x1001a160` is called 1,084 times across the full frame — CONFIRMED by live hook count, extending
§5.3h's "twice per row" finding (shown there for one sampled row) in scale to the whole frame**, not
contradicting it (order-of-magnitude consistent with ~2 calls/row across Y + chromaA + chromaB row
counts).

**The DC-literal read site (`0x1001b311`) was hit 2,453 times across the full frame — CONFIRMED live.**
Address pattern: a 16-bit (stride-2) array; repeated reads at an identical address always return an
identical value (no observed read corruption); addresses advance in contiguous +2 runs, consistent
with one contiguous per-row array per plane per row (matches §5.3h's "pre-decoded PER-ROW DC array"
description). **New, not previously noted: the SAME address is sometimes read multiple consecutive
times before the next address is read** — plausibly one DC entry being shared across a macroblock's
multiple luma sub-blocks, or a luma/chroma dispatch pairing; not resolved this pass.

**Central diagnostic question (does the DC array differ from ground truth uniformly, or only
partially/inconsistently): answered using §5.3d/§5.3f's own existing FFmpeg block-level comparison,
not a new column diff this pass.** That comparison already shows a binary, content-dependent split:
~16,328/32,400 blocks exact, ~16,000 uniformly +128 on every pixel, a small remainder partial/boundary
— described there as runs that self-correct on each bitstream ABSOLUTE reset. A binary per-block run
pattern, not a uniform whole-array offset, is direct evidence against a downstream
(`0x1001b2ff`/`0x1001b311`-region) post-array bias and FOR an arithmetic fault inside `0x1001a160`'s
own batch/seed/delta decode (most likely in its DELTA-mode class-nibble chunk accumulation or sign
handling, per §5.3f's already-mapped structure) that is latched per DPCM run and reset by each
ABSOLUTE seed.

**[CONFIRMED — live execution this pass: `0x1001a160` call count (1,084) and DC-array read-site hit
count (2,453) across the full frame-0 decode; the array's stride-2/contiguous-run/repeated-address
access pattern. HIGH CONFIDENCE — reusing §5.3d/§5.3f's own confirmed block-level ground-truth split:
the +128 pattern is binary/per-block-run, not uniform, pointing inside `0x1001a160`. OPEN, unchanged
from §5.3f: the exact instruction/arithmetic step inside `0x1001a160`'s DELTA-mode decode responsible;
also newly OPEN: why the same DC-array address is sometimes read multiple consecutive times before
advancing; a narrower single-row capture driver to do a fresh independent column-by-column
ground-truth diff of one row's own DC array (not completed this pass — the full-frame capture
interleaves multiple planes/rows in one stream).]**

### 5.3l Row-0 column-grain ground-truth diff: the +128 pattern is finer-grained than block-run, pointing at per-chunk (not per-batch) delta corruption; the §5.3k-planned raw-array dump method is unreliable (2026-10-11)

**Scope.** Direct follow-up to §5.3k's own named next step. Live Unicorn execution (unmodified
`binkw32.dll`, unmodified `main_menu_bg.bik`), reusing `decode_frame0.py`'s harness plus
§5.3f/§5.3d's own FFmpeg block-classification method (`probe_fix_test2_global_ecx.py`'s
`compare()`), applied at single-block-column grain (240 columns) for Y-plane block-row 0 only,
instead of whole-plane.

**CONFIRMED, live: row 0's EXACT/+128 run pattern is single-digit-column grain, not the long
block-run pattern the whole-frame-aggregate figures in §5.3d/f/k implied.** Representative run
summary (block-column ranges, row 0): `0-1:EXACT, 2-5:+128, 6-15:EXACT, 16-18:+128, 19-20:EXACT,
21-24:+128, 25-27:EXACT, 28-34:+128, ...` (full 240-column table captured). Runs are typically
1-13 columns long, never the "whole multi-row stretch" shape a coarser per-block aggregate
statistic could suggest. **This narrows the standing hypothesis**: given §5.3f's own confirmed
DELTA-mode structure (chunks of up to 8 deltas, keyed by a 4-bit class nibble), run lengths this
short are consistent with **the fault toggling within individual delta chunks**, not only at
whole-batch ABSOLUTE-seed boundaries as previously framed — HIGH CONFIDENCE that the corruption
lives in the per-chunk class-nibble delta-accumulation loop itself (test-cl-1/neg-eax/predictor-
accumulate idiom, §5.3f), affecting individual deltas within a chunk, not a whole-chunk or
whole-batch toggle.

**OPEN, refuted plan: `0x1001a160`'s `param1+0x30` field ("writeptr" per
`probe_param1_fields.py`) is NOT a reliably-dumpable finished per-row DC array.** Dumping 240
contiguous int16 entries from it after Y row 0's own 2 `0x1001a160` calls return gives mostly
implausible magnitudes (thousands to tens-of-thousands) for a field whose real values are small
(§5.3f's own max-magnitude bound: 1504 for luma; real captured DC literals via the
`0x100244c0`-entry hook are in the 25-53 range). §5.3k's own recommended "narrower harness driver
that dumps one row's own clean DC-value array" via this field does NOT work as planned — either
`+0x30` is a transient/scratch pointer rather than the committed array base, the two per-row
calls' arrays overwrite/interleave by the time both have returned, or the array is not
contiguous-stride-2 the way §5.3k's full-frame read-site evidence assumed. **Concrete correction
for the next pass:** capture DC values via the `0x1001b311` zero-cost read site live (as §5.3k's
full-frame pass already did), scoped to row 0's dispatch window only, and correlate each read to
its consuming block's column through the case-dispatch call stack — not by bulk-reading the
backing array after both `0x1001a160` calls finish.

**[CONFIRMED — live execution, this pass: the single-column-grain run pattern for Y-plane row 0,
240/240 columns classified against FFmpeg ground truth. CONFIRMED — refuted: the `param1+0x30`
bulk-array-dump method does not yield a usable ground-truth DC array. HIGH CONFIDENCE: the fault
is in per-chunk delta accumulation, not whole-batch/whole-chunk toggling, based on run-length
granularity. OPEN, unchanged from §5.3f/k: the exact faulting instruction inside the per-chunk
class-nibble delta loop — this pass did not reach a live instruction-by-instruction trace of a
specific bad chunk; a future pass should use the corrected `0x1001b311`-based per-row capture
above to first isolate ONE short bad run's exact bit-position window, then step that window's
`0x1001a160` chunk loop live register-by-register.]**

### 5.3m Full chunk-loop disassembly pins the exact commit mechanism; corrects §5.3l's capture method but the column correlation and final instruction-level pin remain OPEN (2026-10-11)

**Scope.** Direct follow-up to §5.3l's own named next step. Live Unicorn execution (unmodified
`binkw32.dll`, unmodified `main_menu_bg.bik`) plus full static disassembly of `0x1001a160`'s
per-chunk delta loop via `disasm_util.py`.

**CONFIRMED, live+static: the full per-chunk delta-accumulation loop, address-exact** (independently
re-disassembled against the real `binkw32.dll` before splicing — matched exactly). The 4-bit class
nibble is read at `0x1001a363`/`0x1001a375`; `nibble==0` takes a `rep stosd`/`rep stosw` flat-fill
path (`0x1001a421`-`0x1001a44d`, chunk filled with the previous accumulator value unchanged, no
further bitstream consumption beyond the nibble). For `nibble!=0`, each of up to 8 values in the
chunk: extracts `nibble`-bit magnitude into `eax` (mask built at `0x1001a394`-`0x1001a3a0`); if
`eax==0` after extraction, the sign bit is skipped entirely (`je 0x1001a405` from `0x1001a3de`);
otherwise consumes exactly one more bit (`test cl,1` at `0x1001a3fe`, `cl` = pre-shift low bit of
bit-buffer register `esi`) and `neg eax` (`0x1001a403`) if set. The signed delta is then added to
the previous committed value (`0x1001a405`/`0x1001a408`, predictor-accumulate), truncated to 16
bits and written to `writeptr` (`0x1001a40a`-`0x1001a410`, advancing by 2 bytes per value); the new
accumulator becomes `prev` for the next value in the same chunk (`0x1001a417`). Loop continues
while chunk values remain (`0x1001a41d`), then falls through to the outer per-chunk-count decrement
(`0x1001a450`-`0x1001a456`).

**CONFIRMED, live: §5.3l's refuted bulk-dump method is now explained, and fixed.** `param1+0x30`
(`writeptr`) is a **transient scratch pointer re-based to the same address on every call** —
observed identically (`0x303f8622`) as the writeptr-range start for 6 sampled calls out of 1,084
total across the frame. Dumping this field after the fact (§5.3l's method) only ever recovers
whichever call most recently wrote it, never the specific call intended — this fully explains
§5.3l's "implausible garbage" result. **The fix: capture at the commit instruction (`0x1001a40a`)
live, per-call, during execution instead.** This produces plausible small values matching §5.3f's
expected magnitude range (e.g. call #2's first 20 commits: `199, 212, 51, 25, 118, 119, 26, 51, 36,
37, 53, 38, 38, 36, 56, 50, 43, 53, 20, 170`), unlike the bulk-dump garbage (thousands to
tens-of-thousands).

**OPEN, unresolved this pass — the task's own named deliverable (the exact faulting instruction)
was not reached.** (1) The `0x1001b311` zero-cost DC-literal read site, hooked live in this pass's
own harness run, reproduced §5.3k's exact hit count (2,453) but NOT its value range: captured
values (tens of thousands) are ~1000x larger than §5.3l's own stated real-DC-literal range (25-53)
and do not resemble plausible DC values — the sequential-read-order-equals-column assumption is
UNCONFIRMED. (2) Each `0x1001a160` call's commit-log entry count (518-542, sampled across 6 calls)
does not divide cleanly into the expected 240-column single-row structure — **the mapping from "one
`0x1001a160` call" to "one row's worth of columns" needs re-derivation** before any column can be
correlated to a specific commit-log entry. (3) Consequently: the first EXACT→+128 transition was
not re-confirmed against live commit data, no specific chunk/iteration was isolated for a bad
column, and no register-by-register divergence trace between a correct and a faulty chunk was
performed.

**Recommended next step.** Use the now-proven `0x1001a40a` commit-capture method (not
`0x1001b311`, not the `param1+0x30` bulk dump) as ground truth. Before attempting any column
correlation, first resolve what one `0x1001a160` call's ~520-540 commits actually represent (more
than one row? DC+other interleaved metadata?) by directly correlating `new_accum` sequences
against known-good per-row reconstructed values, rather than assuming 1 call = 1 240-column row.
The hook infrastructure (`0x1001a40a` commit, `0x1001a3fe` sign test, `0x1001a3de` zero-magnitude
bypass) is built and working and can drive the eventual divergence comparison once that mapping is
fixed.

**[CONFIRMED — live+static: full chunk-loop disassembly, address-exact, through the commit
instruction, independently re-verified. CONFIRMED — live: §5.3l's bulk-dump method explained and
replaced with a working per-commit live-capture method. OPEN, unchanged from §5.3f/k/l: the exact
faulting instruction/condition producing +128 — this pass did not reach a column-correlated live
trace, let alone a register-by-register divergence comparison between a correct and faulty chunk.
A future pass must first fix the call-to-row/column mapping (not assumed 1:1) before repeating
this task's own steps.]**

### 5.3n Corrects §5.3m's own commit-hook off-by-one; call-to-row mapping still unresolved but narrowed to 25 candidate calls out of 1,084 (2026-10-11)

**Scope.** Direct follow-up to §5.3m's own named next step. Live Unicorn execution (unmodified
`binkw32.dll`, unmodified `main_menu_bg.bik`), reusing `decode_frame0.py`'s setup unchanged.

**CONFIRMED, live+static: §5.3m's own commit-capture method had an off-by-one hook placement, now
fixed.** `0x1001a40a` (`mov eax, dword ptr [ebp-0xc]`) was hooked directly, but a Unicorn code hook
fires *before* that instruction executes — so the `eax` §5.3m captured there was stale (leftover
from the `neg eax` at `0x1001a403`/the zero-magnitude test at `0x1001a3de`), not the write address.
Independently re-verified against the live disassembly before splicing (confirmed:
`0x1001a40a: mov eax,[ebp-0xc]` then `0x1001a40d: mov word ptr [eax],cx` — `eax` is only the real
write pointer from `0x1001a40d` onward). **This is why §5.3m's own "addresses" were tiny/wraparound
garbage. Fix: hook `0x1001a40d` instead** — one instruction later. `ecx` (the committed value) was
already correct in §5.3m, since it is computed earlier, at `0x1001a408`.

**CONFIRMED, live, with the corrected hook:**
1. A single `0x1001a160` call's commits form **exactly one contiguous run**, never multiple
   sub-runs at different base addresses (sampled call #2: 518 commits, `0x303f8622` to
   `0x303f8a2c`, step-2 contiguous throughout; same pattern on 7 further sampled calls). This
   falsifies the "multiple planes/bundles interleaved in one call" hypothesis — a call's own writes
   never jump base address mid-call.
2. The write base address (e.g. `0x303f8622`) does not fall inside any of the harness's own mapped
   regions (`FRAME_BUF_BASE=0x40000000`, `PLANES_BASE=0x42000000` — containing `Y_BUF`/`CA_BUF`/
   `CB_BUF` — or `SCRATCH9_BASE=0x44000000`). It is a DLL-internal heap allocation. This
   independently reconfirms §5.3m's "transient re-based scratch pointer" framing from the
   address-mapping side: **the per-chunk delta loop writes into a private intermediate DC-delta
   scratch array inside the DLL's own heap, not directly into the final pixel planes.** Correlating
   a commit to a known-good FFmpeg pixel column requires first finding the downstream code that
   later copies/transforms this scratch array into `Y_BUF` et al. — not yet located.
3. **Of 1,084 total `0x1001a160` calls in the frame, only 25 ever reach the per-chunk
   delta-accumulation loop body at all** — calls #2, 34, 66, 98, 126, 154, 182, 210, 242, 274, 310,
   350, 374, 390, 406, 426, 446, 478, 506, 542, 654, 730, 770, 814, 816 (roughly periodic ~28-32
   calls apart through call ~540, then sparse/irregular after). The other ~1,059 calls produced
   zero commits AND zero flat-fill events on the instrumented `rep stosd`/`rep stosw` sites
   (`0x1001a437`/`0x1001a43e`). This means most of the 1,084 calls take some other internal branch
   of `0x1001a160` entirely — **the function is evidently a shared/generic nibble-array decoder
   invoked for purposes beyond per-row DC decode**, not a DC-row-only routine, which is the real
   reason §5.3m's "1 call = 1 row" assumption never divided cleanly: most calls aren't DC-row calls
   at all. Commit-run lengths among the 25 real calls range 336-607 (not 240), consistent with
   §5.3f's already-documented multi-row refill cadence (batches of several rows per call) rather
   than a single-row-per-call model.

**OPEN, unresolved this pass.** (1) Which of the 25 real calls maps to which Y-row-group, and why
lengths are 336-607 rather than a clean multiple of 240 — not yet cross-checked against the call's
own declared chunk-count stack args. (2) The downstream consumer that copies the scratch array
into the real plane buffers — unidentified; this is the correct anchor point for any future
column-exact divergence trace, not the commit loop itself. (3) Consequently, no divergence trace
was attempted this pass — the mapping blocker was not cleared far enough to make that step
meaningful yet.

**Recommended next step, concretely.** (a) Re-baseline all future commit captures on the fixed
hook site `0x1001a40d`, not `0x1001a40a`. (b) For each of the 25 real-commit calls, also capture
the caller-supplied element-count/chunk-count stack args directly, rather than inferring length
from commit count, to settle whether 336-607 is "N × 240" for some small N or unrelated to the Y
row width. (c) Add a Unicorn memory-read hook watching the scratch addresses after each of the 25
calls returns, to find the code that reads this scratch array back out and locate the copy-to-plane
step — that is the real correlation point against FFmpeg's pixel columns, not this commit loop.

**[CONFIRMED — live: §5.3m's own commit-hook placement was off-by-one and is now fixed (hook
`0x1001a40d`, not `0x1001a40a`). CONFIRMED — live: a call's commits are always one contiguous run
into a private DLL-heap scratch array, never multiple interleaved sub-runs, and only 25 of 1,084
total calls ever reach the delta-accumulation loop body at all. OPEN, narrowed from §5.3m: the
call-to-row mapping among those 25 calls, and the still-unidentified scratch-to-plane copy step
that any future divergence trace must anchor on instead of the commit loop.]**

**First call's exact count PINNED, same day follow-up, orchestrator-routed as the likely final
fix for the remaining Bink gap (SPEC TEAM, agent `a06b1037922a541b3`, independently re-verified
by direct query before splicing — the raw capture's leading-count and live write-count both
matched exactly at 519, and the exit-bit measurement matched exactly at 4307).** For
`0x1001a160`'s FIRST call in frame 0's Y-plane/row 0 (the one feeding block (0,0), already
independently confirmed DC=118 elsewhere): exact word count = **519**, verified two independent
ways that agree exactly — a leading decoded magnitude read at call entry (`0x1001a1be`-
`0x1001a1db`, saved to `[ebp-0x10]` at `0x1001a1de`, a SEPARATE decode from value[0]'s own data)
and a live instruction-level write count to the call's own natural `ret` (1 + 64×8 + 6 = 519,
exact match; no flat-fill/bulk-repeat branch fired on any of this call's 65 chunks). **Mechanism:
the identical leading-count idiom §5.3s documents for `0x100195b0`'s 650 and §5.3t for
`0x10019d10`'s 528** — the count pins the call's own output END address into the context struct
(`ctx[0]`/`ctx[4]`, written right after value[0]) BEFORE the chunk-decode loop begins; the chunk
loop (`0x1001a330`-`0x1001a456`) terminates purely on the remaining count reaching zero — no
bitstream sentinel, no fixed caller argument, no other terminator exists in the function body.
The count is a word count directly, no byte→word translation needed. 519 falls inside §5.3n's
own already-established 336-607-word range for the 25 real calls — strong corroboration this is
the right call and mechanism. Exit bit position (entry 9256, per §5.3q, reproduced unchanged):
consumes exactly **4307 bits**, exiting at absolute bit **13563** — measured honestly, NOT force-
fit against the orchestrator's own ~13619 estimate. **CLOSED 2026-10-11, see the gap-attribution
paragraph immediately below: the gap is real (24 bits of genuine bitstream cost from two more
calls) but a separate ~32-bit cross-script convention mismatch means 13563+24=13587, not 13619 —
not a missed consumer.**

- **Evidence:** live Unicorn capture against the unmodified DLL/`.bik`, run to the call's own
  natural `ret` (no budget cutoff); leading-count value and live write-instruction count
  independently cross-validated to an exact match; value[0]=118 reproduced as an internal sanity
  check. Independently re-verified before splicing — the raw capture file's leading-count,
  write-count, and exit-bits lines all matched exactly.
- **Confidence:** CONFIRMED for the word count (519) and the count-driven mechanism (two
  independent live measurements agree exactly; static disassembly shows no other terminator).
- **Unknowns:** whether the 9256/13619 baseline figures from prior passes used an identical
  bit-counting convention to this pass's — ANSWERED below: no, they do not; see the gap-
  attribution paragraph for the ~32-bit systematic offset this uncovered.

**Gap attributed call-by-call, closes the item above (same day, SPEC TEAM, agent
`ac836bd1af667b8a2`, independently re-verified by me via fresh static `capstone` disassembly of
`0x1001a530`-`0x1001ac10` before trusting — the exact call sequence and absence of any
`0x10019f00` call in this span both confirmed directly).** The orchestrator's own "two
`0x10019f00` calls" hypothesis is **disconfirmed as stated**: `0x10019f00` is called twice, but
both calls sit at `0x1001ab25`/`0x1001ab42`, BEFORE the first `0x1001a160` call (`0x1001ab74`) —
part of an earlier pre-stage gap (§5.3r), not this one. The real sequence immediately after the
first `0x1001a160` call's `ret` (`0x1001ab79`) is: a SECOND `0x1001a160` call (`0x1001ab91`) that
decodes a leading count of 0 and takes its own zero-count idle/bail exit (`0x1001a471`) — a no-op
commit, but the 0-valued leading magnitude itself still costs **10 bits**; then `0x10019af0`
(`0x1001abae`, a function named-but-never-opened until now) whose own idle-check passes, decodes a
leading magnitude of 0, and takes its own zero-magnitude bail (`test eax,eax; je 0x10019ce7`) —
also a no-op commit, costing **14 bits**; then a zero-bit-cost per-column dispatch byte read
(`0x1001abe9`) leads straight into the real DC-literal read (`0x1001b328`, already documented in
§5.3o/§5.3p/§5.3z). Sum: 10+14=**24 bits**, landing the real dispatch-read at absolute bit
**13587** — not 56 bits from 13563, and not landing at 13619. The context address captured at
this point (`0x303f4e9c`) and the DC value/cursor (118/`0x303f8620`) both exactly match the
independently-published figures for this same dispatch in §5.3y/§5.3z, confirming this is the
right instance. The residual 32-bit (one accumulator word) gap against §5.3y's own published
13619 is consistent with a systematic bit-counting **convention** difference between that pass's
hook-based method (at the AC-tree call boundary) and this pass's/§5.3n's memory-struct-read
method — not a newly-discovered bitstream consumer — but the root cause of that convention
difference was not traced to a formula-level diff (§5.3y's own script was not available for
comparison).

- **Evidence:** live Unicorn capture against the unmodified DLL/`.bik`, reusing
  `decode_frame0.py`'s bit-position convention, independently re-validated against three already-
  published values (9256 entry, 13563 exit, DC value 118/cursor `0x303f8620`) before trusting any
  new number. Independently re-verified by me (not the dispatching session) via fresh static
  `capstone` disassembly of the orchestrator function (`0x1001a530`-`0x1001ac10`,
  `scratchpad` verification script against `binkw32.dll` directly) — confirms the exact call
  sequence (`0x1001a160`×2 then `0x10019af0`) and the complete absence of any `0x10019f00` call in
  this span, exactly as the agent reported.
- **Confidence:** CONFIRMED for the call sequence, the 10+14=24-bit attribution, both calls taking
  a zero-valued no-op/bail path, and that the captured instance is the real block (0,0) dispatch.
  PLAUSIBLE, not diagnosed to root cause, that the remaining 32-bit residual against §5.3y's 13619
  is a cross-script bit-counting convention difference rather than a real missed consumer.
- **Unknowns:** the exact source of the 32-bit (one accumulator word) convention mismatch against
  §5.3y's 13619 — not traced to a formula-level diff; why `0x10019af0`'s zero-magnitude decode
  costs 14 bits specifically (its own `ctx+0x28`/`+0x2c` fields weren't read this pass); whether
  the second `0x1001a160` call's context (`0x303f4ec8`) and `0x10019af0`'s context (`0x303f4f98`)
  are per-plane descriptors or sub-fields of one combined row-context block — not resolved, not
  essential to this question.

### 5.3o Resolves an urgent orchestrator question: block (0,0)'s real DC does NOT come from `0x1001a160`; it is a direct 16-bit byte-cursor read, re-confirming the existing secondary=2 trace a third independent time and reconciling against Team B's reported 16709 (2026-10-11)

**Scope.** Direct response to an urgent question (Team B reported pixel (0,0) of block (0,0),
Y-plane, frame 0 as 236 vs. FFmpeg's real 24, with their own DC literal reading as `0x4145`=16709
— implausible for a near-black region). Reused and independently re-ran
`scratchpad/bink_sub012346_grammars_2026-10-10/trace_sub2_block0.py` live (Unicorn, unmodified
`binkw32.dll`/`main_menu_bg.bik`, path-corrected only, zero logic changes) against the existing
§5.3 "secondary=2" worked example above, which it reproduces exactly.

**CONFIRMED, live, freshly reproduced this pass — exact match to the existing worked example
above AND to §5.3h's own independently-traced value, a third independent confirmation:** block
(0,0)'s DC literal is **not** read from `0x1001a160`'s per-chunk delta-accumulation output (the
model this question's own framing, and §5.3h's original wording, assumed). It is read at
`0x1001b311`-`0x1001b33b` as a plain byte-aligned 16-bit little-endian word from descriptor L's own
byte cursor — **zero-bit-cost, word-granular, not bit-granular** — value `0x0076` (118), exactly as
already documented above. The resulting decoded pixel (0,0) is `0x18`=24, an EXACT match to
FFmpeg's ground truth — **this specific pixel is already correctly decoded by the real,
unmodified `binkw32.dll`** traced here.

**Retracts, for this specific block/dispatch case: the framing that this DC literal's "zero-bit-
cost read" traces back to `0x1001a160`'s per-row array.** It traces to descriptor L's own
independent byte cursor instead — a separate mechanism from the `0x1001a160` delta-array work
§5.3f/h/k/l/m/n have been pursuing. `0x1001a160`'s own role (per §5.3n, only 25/1,084 calls per
frame even reach its delta loop) remains real and unresolved, but it is NOT what determines block
(0,0)'s own DC value.

**Reconciling Team B's reported 16709 against the real value 118 — HIGH CONFIDENCE, not verified
against their own source (no access):** 16709 falls squarely in the "tens of thousands" magnitude
range §5.3m independently measured for mis-captured `0x1001a160` scratch-array reads (before that
pass's own off-by-one hook fix, §5.3n). Given this pass confirms block (0,0)'s DC does NOT come
from `0x1001a160` at all, the most likely explanation is that Team B's own decoder reads this
block's DC from the `0x1001a160` delta array (the superseded §5.3h framing) rather than from
descriptor L's direct byte-cursor read — a structurally-motivated explanation for the bug's
magnitude, not a confirmed code-level diagnosis.

**New, OPEN, not previously flagged:** the DC-read address (`0x303f8620` in this run) sits exactly
2 bytes before the base address of §5.3n's own sampled `0x1001a160` commit run #2
(`0x303f8622`-`0x303f8a2c`) — same DLL-heap region, immediately adjacent. Not yet determined
whether this is a real allocation/handoff relationship between descriptor L's buffer and
`0x1001a160`'s own scratch array, or coincidental sequential-allocator adjacency. Flag for whoever
next picks up §5.3n's own "unidentified scratch-to-plane copy step" item — check this adjacency
first.

**[CONFIRMED — live, freshly reproduced, exact 3-way agreement with the existing secondary=2 trace
and §5.3h's own independently-traced value: block (0,0)'s real DC literal is 118, read as a
zero-bit-cost 16-bit word from descriptor L's byte cursor, NOT from `0x1001a160`'s delta array,
and the resulting decoded pixel (24) exactly matches FFmpeg. OPEN: the suspicious 2-byte heap
adjacency between descriptor L's cursor and §5.3n's own sampled `0x1001a160` commit-run base.
OPEN, carried over unchanged: §5.3n's own named next steps — not addressed this pass, since they
don't determine block (0,0)'s own DC value.]**

### 5.3p Descriptor L's buffer SOURCE and cursor START, for block (0,0) of frame 0 — resolves §5.3o's own "suspicious 2-byte adjacency" OPEN item (2026-10-11)

**Scope.** Direct follow-up to a precise orchestrator-routed question: what writes descriptor L's
buffer (decompressed output, or a pre-stage call?), from which input bytes, and where its cursor
starts relative to that buffer, for block (0,0), frame 0. Answered by a new Unicorn write-watch
(`scratchpad/bink_sub012346_grammars_2026-10-10/trace_descriptorL_source.py`,
`scratchpad/descriptorL_source_2026-10-11/identify_ctx.py`): recorded every memory write before
the first primary-dispatch of the frame, then cross-referenced the exact address descriptor L's
cursor reads for block (0,0) (`0x303f8620`, confirmed again this pass, exact match to §5.3o)
against that write log.

**CONFIRMED, live, direct address+value correlation — descriptor L's buffer is NOT a separate
pre-stage output; it is written live, in place, by `0x1001a160` itself** — the SAME function
§5.3f/h/k/l/m/n's own "per-chunk delta-accumulation" work has been investigating, and the same
function Team B has already implemented as a separate cursor (commit `b33c13e`, per the
orchestrator). The exact write that produced the DC literal byte read for block (0,0) was
captured directly: address `0x303f8620`, size 2, value `0x0076` — an EXACT match, same address
and same value, to the read `0x1001b311`-`0x1001b33b` already documents — written by the
instruction at `0x1001a307`, inside `0x1001a160`'s own body (confirmed by direct disassembly of
`binkw32.dll`, not inferred: `0x1001a307`/`mov word ptr [eax],cx` sits between `0x1001a160`'s
prologue and its `ret`, no other function boundary between them). The immediately following
words (`0x303f8622`, `0x303f8624`, ... sequential, 2 bytes apart) are written by a SECOND,
sibling word-write site in the same function, `0x1001a40d` (identical `mov word ptr [eax],cx; add
eax,2` idiom) — i.e. `0x1001a160` has two write points for the same output stream: one for the
first symbol of a call (`0x1001a307`), one for subsequent symbols within the same call
(`0x1001a40d`), both advancing the same pointer by 2 bytes each.

**Input bytes: the main per-row compressed bitstream, decoded live by a context-adaptive
sign/magnitude bit-predictor — NOT a separately pre-decompressed buffer.** The code immediately
before the `0x1001a307` write (`0x1001a1fa`-`0x1001a2fd`) reads the SAME bit-accumulator state
(`ebx`/`edx`/`esi`, loaded at function entry from the function's own 2nd argument's `+0`/`+4`/`+8`
fields — the already-known pointer/bit-position/accumulator triple for a row's main bitstream) and
runs a magnitude-then-sign decode (shift-based probability extraction, then a `neg` applied
conditionally on a parity test) to produce the 16-bit value written out. This is an ordinary
step of the row's own primary entropy decode, not a call into any separate pre-stage routine
(contrast with descriptors A/B, which §5.3c found ARE filled by dedicated `0x10019360`-style
pre-stage calls — descriptor L is structurally different: it's a side-effect output of the main
per-row decode loop itself, interleaved symbol-by-symbol with whatever other output
`0x1001a160` also produces).

**Cursor start, for block (0,0): literal offset 0 of the buffer — confirmed by direct register
read, not inferred.** `0x1001a160`'s own first argument (a per-plane/per-row context struct,
confirmed live at runtime address `0x303f4f64` for the Y-plane this frame — a real heap
allocation, not one of the harness's own fixed scratch regions) holds the buffer's persistent base
pointer at context offset **`+0x30`**. Read directly, BEFORE `0x1001a160`'s very first call this
frame for this context: `ctx+0x30 = 0x303f8620` — already exactly equal to the address block
(0,0)'s DC read consumes. Since this is the first call into `0x1001a160` for this context this
frame, and the first write it performs (`0x1001a307`) lands at exactly this same address, **the
cursor for block (0,0) starts at the buffer's own base, offset 0** — the very first word this
mechanism produces for the frame's Y-plane is the exact word block (0,0) reads as its DC literal.
`ctx+0x30` itself does not appear to advance across calls (read again, unchanged, on a later call
with the same context) — the live write-cursor used, during a call, lives in `0x1001a160`'s own
locals (`[ebp-0xc]`, reseeded from `ctx+0x30` each call); what persists cross-call progress is a
SEPARATE pair, `ctx+0`/`ctx+4`, which were observed to change between consecutive calls on the
same context (`0`/`0` on the first call, `0x303f86b6`/`0x303f8a2e` on a later one) — consistent
with a commit-style bookkeeping pair, not yet fully characterized (OPEN, see below).

**Resolves §5.3o's own "suspicious 2-byte adjacency" OPEN item: it is not adjacency between two
separate mechanisms, it is the SAME buffer.** §5.3n's own sampled `0x1001a160` "commit run #2"
base (`0x303f8622`) is exactly the SECOND word this mechanism writes (via `0x1001a40d`) — one word
(2 bytes) after the FIRST word (`0x303f8620`, via `0x1001a307`), which is precisely the DC value
block (0,0) consumes. §5.3n's own sampling window simply started one word late, missing the very
first write. The "delta-accumulation output" §5.3f/h/k/l/m/n have been tracking and "descriptor
L's byte cursor" are the SAME output buffer, not two heap-adjacent but independent mechanisms.

**[CONFIRMED — live, direct address+value correlation between a captured pre-dispatch write
(`0x303f8620`, value `0x0076`, pc `0x1001a307`) and the already-documented DC-literal read at the
identical address/value: descriptor L's buffer is written in-place by `0x1001a160` from the main
per-row bitstream via an inline context-adaptive bit-predictor decode, not by a separate pre-stage
call; for block (0,0) specifically, the cursor starts at literal offset 0 of the buffer (the very
first word `0x1001a160` produces this frame for the Y-plane context). CONFIRMED: this resolves
§5.3o's 2-byte-adjacency OPEN item as the same buffer, not coincidental adjacency. OPEN: the exact
condition selecting the `0x1001a307` vs. `0x1001a40d` write site on a given call (first-symbol vs.
subsequent-symbol, by position in the disassembly, not independently confirmed by branch-condition
trace); the exact role/update rule for the `ctx+0`/`ctx+4` commit-style pair (changes across calls,
mechanism not traced); whether `ctx+0x30` itself is ever advanced by anything (not observed to
change across the 3 calls sampled, so it may be a fixed per-context buffer-start pointer rather
than a live cursor, with the real cross-call position tracked by the `ctx+0`/`ctx+4` pair instead
— not settled this pass).]** Full detail/scripts:
`scratchpad/bink_sub012346_grammars_2026-10-10/trace_descriptorL_source.py`,
`scratchpad/descriptorL_source_2026-10-11/identify_ctx.py`, `dump_a160_full.py`.

### 5.3q `0x1001a160`'s first call, frame 0/Y-plane/row 0: absolute bit position, ctx state at entry, first 8 decoded values, and position relative to the 23-call pre-stage (2026-10-11)

**Scope.** Precise, orchestrator-routed question for Team B's own implementation diff. Reuses
`probe_prestage_bitpos.py`'s own bit-position helper and call-order hooks (`scratchpad/
bink_entropy_prestage_algorithm_2026-10-08/`, unmodified) extended with `0x1001a160`-specific
instrumentation: `scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_a160_first_call_2026-10-11.py`.

**(1) Absolute bit offset at entry: 9256 bits** from the Y sub-chunk's own data start — measured
live, at the instruction right after `0x1001a160`'s own `mov ebp,esp` (hooking the function's
literal first byte reads the CALLER's `ebp`, not this function's own — a mistake caught and fixed
before trusting the result, same class of error as `§5.3p`'s own `identify_ctx.py` fix).

**(2) CORRECTED 2026-10-11, see new §5.3u: the "no evolving predictor seed" finding below is
real but was WRONGLY GENERALIZED to the whole stream — it only describes VALUE[0] of each call.**
The magnitude/sign decode immediately before `0x1001a160`'s FIRST write
(`0x1001a200`-`0x1001a2fd`, already disassembled in §5.3p's own investigation) is driven entirely
by the MAIN shared bit-reader's own live accumulator/position/pointer plus exactly **two FIXED
values read from the context struct**, confirmed unchanged across the 3 calls sampled in §5.3p:
`ctx+0x28 = 10` (a bound/width constant used directly in the shift-based magnitude extraction)
and `ctx+0x2c = 1504` (an overflow-sanity threshold, halved to 752 before the `ja`-bail
comparison). Neither field is written anywhere in `0x1001a160`'s own body — they read as **fixed
per-row/per-plane configuration, not a carried "previous value" or adaptive state, for THIS ONE
VALUE ONLY.** Full `ctx` dump at entry (16 dwords from `ctx+0x00`):
`0x0,0x0,0xb,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xa,0x5e0,0x303f8620,0x0,0x0,0x4` — i.e. `ctx+0x08=0xb`
(11, a third persistent small constant, role not traced this pass), `ctx+0x30=0x303f8620`
(descriptor L's own buffer base, already confirmed §5.3p). **§5.3u found that every value AFTER
the first one in the same call is genuinely predictor-accumulated (`new = prev + signed_delta`)
via a completely different mechanism (§5.3m's own chunk loop) — so "no seed" is correct for
value[0], but values[1+] are explicitly seeded by the running accumulator, which this item's
own wording obscured by not stating its scope.**

**(3) CORRECTED 2026-10-11, see new §5.3u: only VALUE[0] goes through the fixed-bound path below
— values[1-7] do NOT, and this item's own framing ("first 8 raw 16-bit values written") wrongly
implied a single uniform mechanism.** First 8 raw 16-bit values written, in order (addr, value):
`0x303f8620=118`, `0x303f8622=199`, `0x303f8624=212`, `0x303f8626=51`, `0x303f8628=25`,
`0x303f862a=118`, `0x303f862c=119`, `0x303f862e=26` — exact reproduction of §5.3p's own
independently-captured write-log for the same addresses (cross-confirms both passes measured the
same real execution). The first (`118`) is already confirmed (§5.3o) as block (0,0)'s own real DC
literal, and is the ONLY one of the 8 produced by the `0x1001a200`-`0x1001a2fd` fixed-bound
mechanism this item describes. **§5.3u's own per-symbol trace shows values[1-7] are each `prev +
signed_delta` from a genuinely separate mechanism (§5.3m's chunk loop, 0x1001a330-0x1001a456) with
its own real 4-bit "class nibble"** — the "no separate class nibble was identified" sentence
originally here was true only for value[0]'s own narrow code path and should not have been read
as a finding about the whole stream; see §5.3u for the real nibble, confirmed present and
load-bearing for every subsequent value.

**(4) Position relative to the 23-call pre-stage: strictly AFTER all of it, with a large
additional bit-consuming gap not yet attributed.** Measured call order, same run: 23 calls to
`0x10018ca0` (bits climbing 0→588, exactly reproducing §5.3g's own figures) → both real
`0x10019360` calls (descriptor A build at bit 588; descriptor B build at bit 1757 — the gap
between A's own entry and B's own entry, 588→1757, is A's own expansion/payload read, not newly
measured this pass) → two `0x10019d10` marker hits (bit position not instrumented this pass) →
the `0x1001a160` call site itself (`0x1001ab74`, confirmed by direct stack read: arg0
`0x303f4f64`=ctx, matching §5.3p's own identified Y-plane context exactly) → entry at bit 9256.
**The 1757→9256 gap (7,499 bits) is real consumption this pass did NOT individually attribute**
(likely descriptor B's own expansion/payload read, by analogy to A's, plus the indirect call via
`[ebp-0x234]` noted in `probe_prestage_bitpos.py`'s own header — neither was instrumented this
pass). **Net answer for Team B: `0x1001a160` is the first real payload decode step after the
ENTIRE pre-stage (table-builds + both descriptor real-builds + their own expansions + the indirect
dispatch) completes for this row — it does not interleave with or precede any part of the
pre-stage.** If Team B's own bit position at the point they invoke their own DC-decoder reproduction
is anywhere other than 9256 for this exact file/frame/row, the discrepancy is upstream of
`0x1001a160` itself (somewhere in the 0→9256 range), not inside the DC decode's own magnitude/sign
logic.

**[CONFIRMED — live Unicorn execution, real unmodified `binkw32.dll`/`main_menu_bg.bik`, every
value quoted read directly from registers/memory, cross-confirmed against §5.3o/§5.3p's
independently-captured values for the same addresses. CORRECTED 2026-10-11 (§5.3u): items (2)/(3)
above are only true for value[0] of the stream — they were wrongly generalized to all 8 sampled
values, which is the exact contradiction a routed Team B report caught against §5.3m. OPEN: the
exact bit-consuming content of the 1757→9256 gap (descriptor B's own expansion, the indirect-call
step); the role of `ctx+0x08=0xb`.]** Full detail/script:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_a160_first_call_2026-10-11.py`,
`probe_a160_first_call_out.txt`.

### 5.3r The 1757→9256 bit gap, attributed call by call (2026-10-11, closes §5.3q's own named OPEN item)

**Scope.** Direct follow-up, same orchestrator-routed thread: §5.3q left the 1757→9256 bit span
(7,499 bits, between descriptor B's real build and `0x1001a160`'s first call) unattributed.
Direct disassembly of `0x1001a530` (the per-row orchestrator) between the two call sites found
exactly **4 real calls** in program order, with zero other candidates: an indirect call via
`[ebp-0x234]` (call site `0x1001aaf0`), `0x10019d10` (`0x1001ab08`), and `0x10019f00` **twice**
(`0x1001ab25`/`0x1001ab42`) — then straight into `0x1001a160` (`0x1001ab74`).

**Measured live (same Unicorn harness, extended hooks; absolute bit positions, baseline-corrected
for the non-zero `bits_avail` at the measurement point — a bug caught and fixed before trusting
the numbers, since the naive version was off by exactly the initial `bits_avail`, 20 bits):**

| Step | Absolute bit position at entry | Bits consumed by this step |
|---|---|---|
| Descriptor B's own `0x10019360` real build (entry) | 1757 | — |
| Indirect call site `0x1001aaf0` (target resolved live: `0x100195b0`) | 2871 | **1114** (desc B's own expansion/payload read) |
| `0x10019d10` entry | 6255 | **3384** (the indirect call, `0x100195b0`) |
| `0x10019f00` entry (1st of 2) | 9236 | **2981** (`0x10019d10`) |
| `0x10019f00` entry (2nd) | 9246 | **10** (1st `0x10019f00` call) |
| `0x1001a160` call site `0x1001ab74` | 9256 | **10** (2nd `0x10019f00` call) |

**Sum: 1114+3384+2981+10+10 = 7,499 — exactly matches the gap, zero bits unaccounted for.**
Absolute bit 9256 at the `0x1001a160` call site exactly reproduces §5.3q's own independently-
measured entry value (confirms both passes agree, not a coincidence of baseline arithmetic).

**Structural characterization of the 3 named functions (brief, not a full algorithm trace —
out of scope for "attribute the gap," which only needed the bit costs above):** all 3
(`0x100195b0`, `0x10019d10`, `0x10019f00`) open with the IDENTICAL "idle-check" prologue
(`cmp [[param1]], [[param1]+4]; jne <bail>`) and read the SAME `+0x28`/`+0x2c` bound pair via the
SAME shift-based magnitude-extraction idiom already documented for `0x1001a160` (§5.3p) — i.e.
these are siblings in the SAME descriptor-fill function family, each filling a different scratch
structure, not a qualitatively different mechanism. `0x100195b0` additionally does a genuine
tree-lookup (two tables indexed via `ebx*4+0x100`/`+0x140`) after the magnitude step — a shape
closer to `0x10024eb0`'s already-documented AC-tree decoder (§5.3j) than to `0x1001a160`'s own
simple magnitude+sign. This is consistent with, not a reopening of, §5.3g's own standing "23
sibling calls... downstream consumers not traced" finding — these 3 are further siblings in that
same general family, now at least bit-cost-attributed even though their own full output grammar
remains untraced.

**[CONFIRMED — live Unicorn execution, exact numeric reconciliation (sum of parts == the gap,
to the bit). HIGH CONFIDENCE, not independently decoded: the full symbol/tree-decode algorithm
inside any of `0x100195b0`/`0x10019d10`/`0x10019f00` — structurally characterized by their shared
prologue/bound-field shape only, not traced line by line. OPEN: what each of these 3 functions'
own OUTPUT is actually used for later in the frame (same open question §5.3g already carries for
sibling pre-stage structures).]** Full detail/scripts:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_gap_1757_9256_2026-10-11.py`,
`dump_gap_region.py`, `dump_gap_functions.py`.

### 5.3s `0x100195b0`'s own output grammar, worked example: frame 0/Y-plane/row 0's first call (2026-10-11)

**Scope.** Direct follow-up, same thread: §5.3r identified `0x100195b0` (consuming 1,114 bits for
this call) as one of 3 sibling descriptor-fill functions in the 1757→9256 gap, but didn't trace
its own output. Full disassembly (`dump_195b0_full.py`, 231 lines, prologue through `ret`) plus
live Unicorn capture (`probe_195b0_worked_example_2026-10-11.py`).

**What it decodes: a signed byte stream, tree-coded (two LUT tables, `arg3+0x100`/`arg3+0x140`,
indexed by a running bit-accumulator), with an initial "overall magnitude" value that gates a
fast-fill shortcut for long runs.** Structurally the same family as `0x1001a160`
(§5.3p) and `0x10019d10`/`0x10019f00` (§5.3r) — identical "idle-check" prologue
(`cmp [arg1],[arg1+4]`), identical `+0x28`/`+0x2c` bound-field shift-extraction for the leading
magnitude — but with a genuinely different, byte-granular output mechanism and (unlike
`0x1001a160`) real persistent adaptive state: a running value is written back to `arg3+0x180` at
every return, carried into the next call on the same `arg3` struct — the first function in this
family found to actually carry state across calls, not just a fixed bound/config.

**Output buffer base: idle-check struct's own `+0x30` field** (live value this call: `0x303f9bb0`)
— the SAME convention `0x1001a160` uses for descriptor L's own buffer (§5.3p), confirmed
project-wide now across a second, independent function.

**Worked example, live-captured (frame 0, Y-plane, row 0, first call):**
- Initial decoded magnitude (the leading value, formed at `0x1001961a`): **650**.
- The long-run fast-fill helper (`0x10001eb0`) was observed firing during this same call —
  confirms the function uses both a per-symbol tree-decode loop AND a bulk-repeat shortcut within
  one invocation, not exclusively one or the other.
- First 64 real output bytes (sequential, `0x303f9bb0`-`0x303f9beF`), as signed values:
  `-103,-103,-103,-103,-103,-103,-103,-103,-103,-102,-103,-103,-102,-102,-102,-103,-103,-98,-102,
  -103,-103,-103,-102,-104,-103,-97,-102,-105,-103,-103,-103,-105,-100,-97,-98,-98,-98,-101,-103,
  -103,-97,-97,-97,-100,-103,-103,-103,-102,-100,-97,-98,-104,-104,-103,-102,-97,-99,-97,-97,-100,
  -103,-102,-102,-97` — a tight, low-variance band (`-105..-97`), consistent with a residual/delta
  array rather than literal pixel or coefficient data.

**Loop-termination question SETTLED, same day, by letting the call run to its own `ret` rather
than guessing from the stack-slot-reuse ambiguity above: this is count-driven, exactly.** Live
run of the same call to completion: exactly **650 bytes** written (`0x303f9bb0`-`0x303f9e39`,
contiguous, zero gaps — address span matches the byte count exactly), and the bit position at
`ret` is exactly **3384 bits** past call-entry — an EXACT match to §5.3r's own independently-
measured cost for this call, cross-confirming both measurements. First 8 bytes
(`-103,-103,-103,-103,-103,-103,-103,-103`) and last 8 (`-102,-99,-98,-98,-99,-102,-103,-103`)
both read as plausible real residual data, not truncation artifacts. **The leading magnitude
really is the literal symbol count — "decode exactly N signed bytes," full stop — not a
self-terminating stream.**

**[CONFIRMED — live Unicorn execution, every value read directly from registers/memory, including
the exact byte-count/bit-cost cross-check against §5.3r's own independent measurement. OPEN: what
this function's own output (the signed byte array at `arg1+0x30`) is used for downstream later in
the frame.]** Full detail/scripts:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_195b0_worked_example_2026-10-11.py`,
`probe_195b0_full_run_2026-10-11.py`, `dump_195b0_full.py`.

### 5.3t `0x10019d10`'s own output grammar, worked example: frame 0/Y-plane/row 0's first call (2026-10-11)

**Scope.** Same thread, the other sibling named in §5.3r (2,981-bit cost). Full disassembly
(`dump_19d10_full.py`, 183 lines) plus live Unicorn capture, same method as §5.3s.

**What it decodes: nibble-packed bytes — two separate 4-bit tree-lookups per output byte (high
nibble then low nibble, `shl dl,4; or dl,<2nd nibble>`), via an EXPLICIT decrement-to-zero loop
(`dec edi` every iteration, exits on `cmp [ebp-8],0`) — genuinely count-driven by construction,
not ambiguous the way `0x100195b0`'s stack-slot reuse was.** Same family prologue/bound-extraction
shape as `0x1001a160`/`0x100195b0` for the leading magnitude; output buffer base is the same
`arg1+0x30` convention, now confirmed a third time.

**Worked example, live-captured, run to completion (frame 0, Y-plane, row 0, first call):**
- Leading decoded magnitude: **528**. Output buffer base: `0x303f9210`.
- Bit position at `ret`, measured from call entry: exactly **2981 bits** — an EXACT match to
  §5.3r's own independent measurement for this call.
- Total bytes written: **528** — exactly the leading magnitude, confirming count-driven decode
  (contiguous `0x303f9210`-`0x303f941f`, zero gaps).
- First 8 bytes (hex): `0f,01,00,00,00,00,80,ff`. Last 8: `00,00,01,01,01,01,03,4f`. Mostly small
  values (`0x00`/`0x01` dominant) with occasional larger ones (`0x0f`, `0x80`, `0xff`, `0x4f`) —
  a sparser, more indicator/flag-shaped distribution than `0x100195b0`'s own tight residual band,
  consistent with these two sibling functions filling structurally DIFFERENT kinds of scratch
  data, not interchangeable copies of one mechanism.
- **Numeric coincidence, flagged honestly, not claimed as a mechanism link:** this call's own
  leading magnitude (528) is numerically identical to the already-documented real LEN value for
  this exact row (§5.3g: "starting at the true bit 588 decodes to **528**"). Not yet determined
  whether this is the SAME quantity reused in a second context, or a coincidence of this
  particular file/row's real content — flagged for whoever next investigates what this function's
  output feeds.

**[CONFIRMED — live Unicorn execution to natural completion, every value read directly from
registers/memory, including the exact byte-count/bit-cost cross-check against §5.3r's own
independent measurement. OPEN: what this function's own output is used for downstream; whether
the 528/LEN numeric match is meaningful.]** Full detail/scripts:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_19d10_full_run_2026-10-11.py`,
`dump_19d10_full.py`.

### 5.3u The apparent §5.3m vs §5.3q contradiction, RESOLVED: both sections are correct, each for a different value in the stream — exact per-symbol trace, first 8 values (2026-10-11)

**Scope, urgent, orchestrator-routed (Team B report, agent 2e44928): Team B now reproduces this
project's own bit position (9256) and byte counts (650/528) exactly up to `0x1001a160`'s own
entry, but their per-symbol DC decode still diverges — their value[0] gives 249 against the real
118. The orchestrator flagged that §5.3m (a 4-bit class nibble driving a variable 0-15-bit
magnitude) and §5.3q (no class field, fixed width via `ctx+0x28=10`) directly contradict each
other, and asked for a side-by-side re-read plus a live per-symbol trace.**

**Resolution: there is no contradiction — §5.3m and §5.3q each correctly describe a REAL
mechanism, but for DIFFERENT values in the same stream. §5.3q's own wording (now corrected above)
wrongly generalized its finding (true only for value[0]) to describe "the first 8 values" as a
whole, which is the exact bug.** Confirmed by a live per-symbol trace of `0x1001a160`'s first
call, frame 0/Y-plane/row 0, hooking the fixed-bound path's own write (`0x1001a307`), the
chunk-loop's class-nibble test (`0x1001a37e`), its masked-magnitude formation (`0x1001a3de`), its
sign test (`0x1001a3fe`), and its commit write (`0x1001a40d`) — with bit position computed from
LIVE REGISTERS (`ebx`/`edx`, the running bit-reader state kept in registers for the whole
function), not from the external bit-reader struct in memory, which is confirmed NOT updated
until the function returns (re-read mid-function would give stale values — caught before
trusting any number below):

| value | bit pos at commit | mechanism | nibble | raw magnitude (pre-sign) | sign | formula | result |
|---|---|---|---|---|---|---|---|
| [0] | 21 | fixed-bound (`0x1001a1fa`-`0x1001a2fd`, §5.3p/q) | — (none read) | — | — | direct | **118** |
| [1] | 34 | chunk loop (§5.3m) | 8 | 81 | + | 118+81 | **199** |
| [2] | 43 | chunk loop | 8 | 13 | + | 199+13 | **212** |
| [3] | 52 | chunk loop | 8 | 161 | − | 212−161 | **51** |
| [4] | 61 | chunk loop | 8 | 26 | − | 51−26 | **25** |
| [5] | 70 | chunk loop | 8 | 93 | + | 25+93 | **118** |
| [6] | 79 | chunk loop | 8 | 1 | + | 118+1 | **119** |
| [7] | 88 | chunk loop | 8 | 93 | − | 119−93 | **26** |

**Every single formula reconciles exactly to the real observed value — zero discrepancies.** The
real mechanism, stated precisely for Team B's own implementation:

1. **`0x1001a160` call entry: value[0] is decoded by the FIXED-bound path only** (§5.3p/§5.3q,
   unchanged, now correctly scoped) — `ctx+0x28`/`ctx+0x2c` bounds, no class nibble read, no
   predictor accumulation (there is no "previous value" yet). Bit cost for this one value: 21
   bits in this run (includes the shift-chain overflow-sanity check, not just a flat read).
2. **Every value AFTER value[0], within the SAME call, is decoded by §5.3m's chunk loop
   (`0x1001a330`-`0x1001a456`), confirmed address-exact and now cross-validated arithmetically for
   the first time:** a chunk of up to 8 values shares ONE 4-bit class nibble (here: `8` for all 7
   of values[1-7], confirming they're all in the first chunk), read once at chunk start
   (`0x1001a363`/`0x1001a375`, 4 bits: 21→25 in this trace). For each value in the chunk: extract
   an `nibble`-bit (here 8-bit) magnitude (8 bits consumed per value); if the magnitude is nonzero,
   consume exactly 1 more bit for sign (skipped entirely when magnitude==0, per §5.3m, not
   observed in this sample since all 7 magnitudes were nonzero); `new_value = prev_value +
   (sign ? -magnitude : +magnitude)`, truncated to 16 bits and written. The nibble value sets the
   magnitude width for the WHOLE chunk, not per-value — confirmed by all 7 values in this chunk
   sharing the identical `nibble=8`.
3. **value[0] seeds the predictor for the chunk loop's own first `prev_value`** — i.e. the two
   mechanisms are not independent, they're sequential stages of ONE stream: one fixed-bound seed
   value, then runs of delta-accumulated values width-coded by a per-chunk nibble, exactly as a
   classic DPCM/delta-coding scheme would be structured.

**Diagnosis for Team B's own 249-vs-118 divergence (HYPOTHESIS, not independently checked against
their code — this project has no access to it): the two most likely causes, given the shape of
this contradiction, are (a) applying the chunk-loop's class-nibble-driven variable-width read to
value[0] too (reading a phantom nibble before it, consuming 4 bits that don't exist in the real
stream at that point, desyncing everything downstream), or (b) applying `ctx+0x28`'s fixed 10-bit
width to values[1+] instead of the real per-chunk nibble width (8 bits here, but genuinely
variable 0-15 per chunk) — either produces a wrong bit-width exactly at the value[0]/value[1]
boundary, which is where the documented confusion lived.**

**[CONFIRMED — live Unicorn execution, real unmodified `binkw32.dll`/`main_menu_bg.bik`, every
value in the table above read directly from live registers (not memory, confirmed necessary — the
external bit-reader struct is proven stale mid-function), and every formula independently
verified to reconcile exactly. This resolves the §5.3m/§5.3q contradiction completely for this
sample; OPEN: whether the nibble is ever 0 (flat-fill path, `0x1001a421`-`0x1001a44d`, not
observed in this specific 8-value sample) or spans a chunk boundary within the first 8 values of
some OTHER real block (not checked).]** Full detail/script:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_first8_dc_symbols_2026-10-11.py`,
`dump_a363_a456.py`, `probe_first8_dc_symbols_out.txt`.

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

### 5.3v Golden-trace worked example: AC-decode → dequant → IDCT, full stage-by-stage capture for frame 0/Y-plane/block (0,0), orchestrator-routed to localize Team B's 141-vs-24 pixel divergence (2026-10-11)

**Scope.** Team B fixed the DC boundary for this block (DC now exactly 118, matching the 8
ground-truth ordinates), but the final decoded pixel still came out 141 against a reference value
of 24 — meaning a remaining bug somewhere in AC decode, dequantization, or the IDCT. This section
captures all four stages for this exact block, live, from the real `binkw32.dll`, independently
re-verified by direct query before splicing (raw coefficient/quant/dequant binaries re-checked
byte-for-byte; both the from-scratch Python re-derivation and the real DLL's own output binary
re-checked against the claimed pixel pattern). Block (0,0), dispatch index 0, is the secondary=2
DC+AC dequant/direct-store-IDCT engine (`0x1001b2ff`-`0x1001b3d2`, §5.3f/§5.3i), quantizer-select
nibble confirmed = 2.

**Method.** Hooked the caller's own call/return boundary around the AC-tree decoder `0x10024eb0`
(entry at `0x1001b348`, the call site itself; exit at `0x1001b34d`, the return site) rather than
the callee's own entry/exit, avoiding a self-re-entrancy hook-confusion problem noted in §5.3j
Part 3. Confirmed by live disassembly that the AC-tree's bit-reader-context pointer for this call
frame is literally `&[ebp-0x204]` inside `0x1001b2ff`'s own stack frame (`lea ecx,[ebp-0x204]` at
`0x1001b33c`), and confirmed nothing else touches that stack slot between the call and the
capture point. The dequant/IDCT math for this specific engine's own dequant/IDCT routine,
`0x100247d0`, was independently re-disassembled fresh (not assumed from the sibling `0x100244c0`
engine already bit-verified in §5.3j) — confirms the identical per-coefficient
`sar(raw[k]*quant[k], 0xb)` dequant and the same shared 8-point butterfly constants
(`0xb50`/`0x8a9`/`0xec8`/`0xffffeb18`, same final `+0x7f`/`>>8` descale) §5.3j Part 1 already
bit-exact-verified for `0x100244c0`. Also confirmed live, by write-address enumeration, that
`0x100247d0`'s 16×16 output is a straight 2×-vertical + 2×-horizontal nearest-neighbor upsample of
one internal 8×8 IDCT grid — a 100% match against the real captured 16×16 output.

**Stage 1 — bit position at AC-tree entry/exit.** Entry: accum=`0x000007a1`, cursor=
`0x400006a8`, bits_avail=13. Exit: accum=`0x07364522`, cursor=`0x400006ac`, bits_avail=28. Net
bits consumed by this one AC-tree call = 17 — an exact match to §5.3h's independently-published
"17 bits for this specific block," an independent cross-confirmation of both sections.

**Stage 2 — raw (pre-dequant) coefficient list, k = row×8+col, row-major, byte offset = 2k.**
Only 3 of the 64 coefficients are nonzero (re-verified byte-for-byte against the raw capture
binary):
- k=0 (DC, row 0 col 0): raw = 118
- k=8 (row 1, col 0): raw = -1
- k=16 (row 2, col 0): raw = -1

All other 61 coefficients are exactly zero. This confirms, with the exact raw values, the
vertical-only AC content §5.3i had already inferred from the output pattern alone.

**Stage 3 — dequantized values** (`sar(raw[k] × quant[k], 0xb)`, quantizer row = nibble 2's row
of the already-documented 16-row×256-byte dequant matrix, base `0x10037cc0`+512 =
`0x10037ec0`). Re-verified by independent recomputation from the raw capture binaries — exact
match:
- k=0: quant=109227 → dequant = sar(118×109227, 0xb) = 6293
- k=8: quant=208315 → dequant = sar(-1×208315, 0xb) = -102
- k=16: quant=240826 → dequant = sar(-1×240826, 0xb) = -118

**Stage 4 — all 64 IDCT outputs** (core 8×8 grid, before the engine's own 2×/2× upsample),
re-verified against the raw output binary — exact match:

```
row 0: 24 24 24 24 24 24 24 24
row 1: 24 24 24 24 24 24 24 24
row 2: 25 25 25 25 25 25 25 25
row 3: 25 25 25 25 25 25 25 25
row 4: 25 25 25 25 25 25 25 25
row 5: 25 25 25 25 25 25 25 25
row 6: 25 25 25 25 25 25 25 25
row 7: 25 25 25 25 25 25 25 25
```

After the confirmed 2×/2× upsample this becomes the real 16×16 Y-plane output — rows 0-3 = 24,
rows 4-15 = 25 uniformly (re-verified against the real captured output binary: exactly 64 bytes
of 24 followed by 192 bytes of 25, byte-for-byte) — an exact match to the real, live DLL's own
actual output for this block, captured independently in the same run.

**Conclusion: this stage chain is NOT the source of the 141-vs-24 divergence.** An independent,
from-scratch re-derivation of AC-decode → dequant → IDCT, run on these exact captured
intermediate values, produces pixel (row 0, col 0) = 24. The real `binkw32.dll`, live-executed in
the same run, also produces 24 at that exact pixel. Both agree with the target/reference value of
24. This means AC coefficient decode, dequantization, and the IDCT/rounding math — as already
documented in §5.3j and reconfirmed here for this specific block's real numbers — are all
correct and already produce the right answer. **The 141-vs-24 divergence is in Team B's own
reimplementation, not in the algorithm as specified.** Team B should diff their own intermediate
values directly against the four stages captured above, in order; whichever stage first disagrees
with these numbers is where the bug actually is. Given the real numbers — only 2 nonzero AC
coefficients, both magnitude 1 (`-1` at k=8 and k=16) — a ±1-coefficient perturbation of this
block's real math cannot plausibly reach 141; the likely culprits on Team B's side are a wrong
AC-tree coefficient (index, sign, or magnitude), a dequant-table indexing error (wrong nibble or
row stride), or a sign/rounding error compounding across the two-pass butterfly. The exact root
cause is inside Team B's own code and is outside this capture's scope.

**[CONFIRMED — live Unicorn capture against the unmodified DLL, cross-checked against an
independent from-scratch Python re-derivation of the same math, both independently re-verified
by direct query before splicing (raw/quant/dequant binaries recomputed and diffed exactly; output
binaries byte-counted exactly). Full capture script, raw binary dumps, and disassembly used for
re-verification: `scratchpad/bink_block00_ac_dequant_idct_capture_2026-10-11/`.]**

---

### 5.3x Compact worked examples, first block-row: Y dispatch indices 1-4 plus chroma block (0,0) — validates Team B's fix beyond a single block (2026-10-11)

**Scope.** Orchestrator-routed follow-up so video decode doesn't stall on another round-trip:
compact (not full §5.3v/§5.3w-depth) captures for the 4 Y-plane blocks immediately following
§5.3v/§5.3w's own block (0,0) in dispatch order, plus chroma block (0,0) of both planes, same
frame 0 of `main_menu_bg.bik`. All captures are live Unicorn executions against the unmodified
DLL (same harness family as §5.3v/§5.3w).

**Dispatch order, confirmed live (not assumed):** dispatch index IS simple left-to-right raster
for this part of the frame — idx0=(row0,col0) [§5.3v/w], idx1=(row0,col16), idx2=(row0,col32),
idx3=(row0,col48), idx4=(row0,col64), all in 16-pixel block-column units. **Not every block uses
the same engine** — idx1/idx2 dispatch to secondary=3 (flat/DC fill), not secondary=2.

| idx | block | engine | DC | nibble | AC bits | nonzero AC (raster k) | core IDCT row 0 |
|---|---|---|---|---|---|---|---|
| 1 | (0,16) | secondary=3 (flat fill) | fill=0x99 (153) | — | — (no AC-tree call) | none | `[153]×8` |
| 2 | (0,32) | secondary=3 (flat fill) | fill=0x99 (153) | — | — | none | `[153]×8` |
| 3 | (0,48) | secondary=2 (DC+AC/IDCT) | 199 | 0 | 83 | k=1:-2,2:3,3:-2,4:-1,8:-3,9:3,10:-2,11:1,13:-1,16:1,17:-2,18:1,20:-1,25:-1,26:1,27:-1,32:-1,33:1,34:-1,43:1 | `[24,25,24,23,24,25,24,24]` |
| 4 | (0,64) | secondary=2 (DC+AC/IDCT) | 212 | 0 | 136 | k=1:-5,2:-5,3:3,5:3,6:-2,8:-2,9:-5,10:-3,11:1,12:1,13:1,14:1,16:1,17:2,18:-1,19:-1,21:1,23:1,25:1,26:2,27:1,28:-1,32:-1,33:-1,34:-1,35:1,36:1,38:-1,42:-1,45:1 | `[24,22,25,29,29,28,29,24]` |

(k-indices already translated through §5.3w's confirmed category→k permutation table, not raw
category indices.) For idx3/idx4, the real DLL's actual first-8-bytes-at-dest (post 2×-horizontal-
upsample) is the core row's first 4 values pairwise-doubled (e.g. idx3: `[24,24,25,25,24,24,23,23]`
from core row `[24,25,24,23,...]`) — confirms the already-documented 2× upsample, not a mismatch;
Team B should diff against the CORE row above, not the upsampled bytes, unless also applying that
same upsample step. Bit-reader continuity sanity check: idx3's own entry accum
(`0x00736452`) is exactly idx0's own published exit accum (`0x07364522`) right-shifted 4 bits,
bits_avail dropping 28→24 — consistent with one continuous shared main bit-reader across blocks.

**Chroma — block (0,0) of both planes, same frame.** ChromaA's own block (0,0) (global dispatch
idx=23914) dispatches to secondary=5 (TwoLiteralPattern), not secondary=2 — fill value 124,
16 pixels wide. This single instance does not by itself answer the chroma-dequant question (Y's
own idx1/idx2 above show engine choice is content-dependent there too), so a SECOND, targeted
capture was taken: the first real secondary=2 occurrence inside chromaA, dispatch idx=23917
(chromaA row=0,col=48) —

- Quantizer table base/arithmetic: `quant_row_ptr = 0x10037cc0 + nibble×256`, nibble=0 — **the
  identical base address and identical nibble×256 row-selection arithmetic already confirmed for
  luma in §5.3v.**
- Dequant formula: the same `sar(raw[k]×quant[k], 0xb)` per-term math, through the literal SAME
  function (`0x100247d0`), same butterfly constants — shared code, not plane-specific.
- DC=1056, nibble=0, 33 AC-tree bits, 5 nonzero coefficients (k=1:-1, k=8:-1, k=13:1, k=17:-1,
  k=24:-1), core IDCT row 0 = `[131,131,131,132,131,132,132,132]`.
- **No evidence of a chroma-specific dequant matrix, a disjoint quantizer-nibble range, or any
  different scaling for chroma's secondary=2 path** — it is literally the same function, same
  table base, same arithmetic as luma.

**ChromaB's own block (0,0) — NOT decodable this pass, reported honestly rather than fabricated.**
Confirmed via a full-frame dispatch survey: chromaB never reaches a real case body anywhere in
this frame (0 real dispatches, vs. 23,914 for Y and 6,042 for chromaA) — reproducing the
already-documented chromaB entropy-pre-stage stall elsewhere in this document (`0x10019360`
permanently stalling 4 bytes past its own bound from its 2nd chromaB call onward). ChromaB's own
`(0,0)` 16×16 region is still pure `0xEE` sentinel.

**CORRECTED, 2026-10-12 [orchestrator-routed, self-picked after the `ctx+0x2c` formula closure]:
the "poisoned after its 2nd call" framing above is likely wrong — the real finding is ZERO calls,
not one-then-stall.** Captured every completed call to `0x10019360` across the whole frame0 decode,
grouped by descriptor pointer (constant per plane's whole run, same method as `0x100195b0`'s own
multicall data): exactly 2 descriptor groups, 271 calls each (Y: N=10/CAP=752; chromaA:
N=10/CAP=640). **No third descriptor ever appears — not a single call, not even an
idempotency-guarded no-op** — contradicting the "1 real call then permanent poison from the 2nd
call" framing. `0x1001a530` (the per-row orchestrator) does get entered with what looks like a
3rd, distinct bitstream-cursor value separate from the Y/chromaA pair, so something runs for
chromaB at the row level, but it never reaches the entropy-pre-stage call at all. Two live
possibilities, NOT distinguished this pass: (a) this video's chromaB plane is genuinely fully
skip-coded for frame 0 (no real content, by design, not a bug); (b) the test harness's own
`call_bink_cb` setup (`FLAGS18`/`FLAGS1C`/`MB_COLS` args) doesn't correctly request/trigger
3-plane decode — a harness limitation, not a real codec behavior. Flagged OPEN rather than guessed.
Scripts: `probe_chromaB_stall_2026-10-12.py`, `probe_a530_detail2.py` in
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/`.

**SETTLED, 2026-10-12 [orchestrator's own cheap ground-truth check]: verdict is (a), legitimate
skip-coding, not a harness gap.** Decoded frame0 with the project's own ffmpeg build
(`tools/ffmpeg_dl`) to raw `yuv420p` and inspected the real V (chroma-B) plane: the `(0,0)` 16×16
block is pixel values 132-133 throughout (a 1-unit range across all 256 pixels) — effectively
flat. The whole V plane is a slowly-varying near-uniform hue (range 123-187, mean ~135, local
stdev ~1.6), typical of a menu-background video, and this block sits in a locally-flat region.
This is consistent with the real encoder genuinely skip/flat-coding this block/plane rather than a
`FLAGS18`/`FLAGS1C`/`MB_COLS` harness misconfiguration — closes the open question above in favor
of (a). Not yet checked: whether OTHER chromaB blocks later in the frame carry real varying
content (only `(0,0)` and the aggregate plane stats were checked this pass).

- **Evidence:** live Unicorn capture against the unmodified DLL and `main_menu_bg.bik`, same
  harness family as §5.3v/§5.3w; dispatch-index→(plane,row,col) mapping independently confirmed
  by write-address enumeration (a full-frame dispatch survey script), not assumed from raster
  order alone; bit-reader entry/exit state cross-checked the same way as §5.3v/§5.3w for every
  block captured.
- **Confidence:** CONFIRMED for idx1-4's engine/DC/bit-count/coefficient/IDCT-row values and for
  chromaB's non-decodability this frame. CONFIRMED (one confirming instance, not an exhaustive
  sweep) for the chroma-shares-luma-dequant finding — only one chroma secondary=2 occurrence
  (nibble=0) was checked, not all 16 possible nibble rows for chroma specifically.
- **Unknowns:** whether chroma's dequant matches luma's at OTHER nibble values (0 was the only one
  checked); ~~the small 4-bit gap between idx0's exit and idx3's entry (noted, not traced)~~
  **CLOSED 2026-10-11, §5.3y below: it is the per-block quantizer-nibble read `0x1001b3b5`, exactly
  4 bits, confirmed live;** which physical plane (Cb/Cr) chromaA/chromaB actually correspond to
  (this project's own existing `CA_BUF`/`CB_BUF` naming was used as-is, not independently re-derived
  this pass). Next step if picked up again: a chroma sec2 sweep across more nibble values.

---

### 5.3y Bit-position/stream-source trace, Y row 0 dispatch idx 0-4 — orchestrator-routed to test the "wrong bitstream source" hypothesis for the still-wrong end-to-end decode (2026-10-11)

**Scope.** Team B has every individual decode stage proven correct in isolation (AC-tree
Gate A/Gate B, dequant, IDCT, flat-fill, chroma dequant) yet their end-to-end decode is still
wrong for all 5 of frame 0's Y-plane row-0 blocks. Working hypothesis: the decoder reads from the
wrong bitstream/cursor source somewhere. Live Unicorn capture (unmodified DLL, unmodified
`main_menu_bg.bik`) at 3 points per block — the block-type dispatch read, the AC-tree entry, and
the AC-tree exit — for dispatch idx 0-4 (idx0/3/4 = secondary=2 DC+AC/IDCT; idx1/2 = secondary=3
flat-fill, no AC-tree call):

| idx | engine | dispatch bitpos | dispatch ctx | AC-entry bitpos | AC-entry ctx | AC-exit bitpos | AC-exit ctx |
|---|---|---|---|---|---|---|---|
| 0 | sec2 | 13619 | `0x303f4e9c` | 13619 | `0x303f4e9c` | 13636 | `0x303f4e9c` |
| 1 | sec3 | 13640 | `0x303f4e9c` | — | — | — | — |
| 2 | sec3 | 13640 | `0x303f4e9c` | — | — | — | — |
| 3 | sec2 | 13640 | `0x303f4e9c` | 13640 | `0x303f4e9c` | 13723 | `0x303f4e9c` |
| 4 | sec2 | 13727 | `0x303f4e9c` | 13727 | `0x303f4e9c` | 13863 | `0x303f4e9c` |

**Finding 1 — no anomaly in the main-bitstream/AC-tree sharing for this 5-block span.** Every bit
position is strictly monotonic across the row; the AC-tree's own bit-reader context address
(`0x303f4e9c`) is byte-identical at entry and exit for every block that calls it, and identical
to the dispatch-read context used by every block including the flat-fill ones — confirming live,
across 3 AC-tree calls in sequence, that the main per-row bit-reader is one single, coherently-
advancing struct shared by the entropy pre-stage, the per-block nibble read, and the AC-tree
decoder, exactly as §5.3g/§5.3v already claim. The per-row stack frame (`ebp`) is also identical
across all 5 blocks, confirming "one `0x1001a530` call per row, every block's case body sharing
one stack frame" directly, live. **The orchestrator's hypothesized wrong-source bug is NOT
located in this specific 5-block span's main-bitstream wiring.** Bonus: this closes §5.3x's own
flagged 4-bit gap between idx0's exit and idx3's entry — it is the per-block quantizer-nibble
read at `0x1001b3b5`, confirmed to consume exactly 4 bits from the main bit-reader (idx0:
nibble=2 exactly matching §5.3v; idx3: nibble=0 exactly matching §5.3x) — not a bug, the
already-documented nibble read, now pinned to the instruction and exact bit cost.

**Finding 2 — a real correction to §5.3i's "descriptor L" framing: the literal-byte READ cursor
is NOT one shared pointer between secondary=2 and secondary=3, even though their bound-check
variable is genuinely shared.** §5.3i states descriptor L is "a SEPARATE descriptor... feed[ing]
the DC literal word (secondary=2), the flat-fill byte (secondary=3)... from this ONE shared byte
stream," naming cursor `[ebp-0x210]`/bound `[ebx+0x10]`. Re-confirmed live: the BOUND-check
variable genuinely is shared and byte-identical across all 5 blocks (`[ebp-0x210]`=`0x0`,
bound=`0x438`). **But the actual literal-byte read pointer differs by engine:** secondary=2's DC
word read (`0x1001b328`) uses a cursor cached at `[ebp-0x13c]` (live: `0x303f8620`→`0x303f8622`→
`0x303f8624`, advancing 2 bytes per DC word); secondary=3's fill-byte read (`0x1001b3f8`) uses a
cursor cached at a slot compared against `[ebp-0x34]` (live: `0x303f9bb0`→`0x303f9bb1`, advancing
1 byte per fill byte). These two cursor regions are ~5,008 bytes apart (`0x303f9bb0 − 0x303f8620
= 0x1390`) — not the same pointer, not adjacent sub-ranges of one obviously-shared small buffer.
Consistent with §5.3h's own separately-flagged large per-row DC-delta bulk-decode pass being a
genuinely distinct array from whatever backs the flat-fill byte stream. **If any reimplementation
modeled DC-literal and fill-byte reads as pulling from one shared cursor/array (per §5.3i's
literal wording), that would be a real, concrete mismatch against the live DLL** — a second,
independently-surfaced "wrong source" candidate, in the literal-byte pipeline rather than the
main bit-reader.

- **Evidence:** live Unicorn execution against the unmodified DLL/`.bik` bytes, hooks at the
  established call/return-boundary pattern (`0x1001b348`/`0x1001b34d` for the AC-tree, avoiding
  the self-re-entrancy hook-confusion issue §5.3j Part 3 flags), plus fresh static disassembly of
  `0x1001b2ff`-`0x1001b350`/`0x1001b3d8`-`0x1001b420` to locate the real literal-cursor
  instructions. Independently re-verified by direct query before splicing — the raw capture
  report's main table and literal-cursor addresses matched the agent's claims exactly.
- **Confidence:** CONFIRMED for the main table (bit positions, context addresses, monotonicity,
  no-anomaly result) and the nibble-read gap closure — live-captured, cross-checked against
  already-published §5.3v/§5.3x numbers, which reproduce exactly. CONFIRMED (address-level) that
  the sec=2/sec=3 literal-byte cursors are physically different addresses. PLAUSIBLE, not
  confirmed, that this reflects two genuinely separate per-row bulk-decoded arrays rather than one
  shared allocation with internal structure.
- **Unknowns:** whether the literal-cursor mismatch (Finding 2) is itself live in any current
  reimplementation bug, or just a spec-wording imprecision with no downstream consequence; what
  sits further upstream of the two cursors' own refill-base pointers (not traced); whether the
  same clean monotonic sharing holds beyond idx4 (chroma planes, later rows) — out of this
  capture's scope. The orchestrator's end-to-end-decode-still-wrong bug is NOT in this 5-block
  span's main-bitstream wiring, so it must be elsewhere — either the literal-byte cursor
  distinction above, further into the row/frame, or a stage not yet captured.

---

### 5.3z Both literal-byte cursors confirmed CORRECT at the source — closes §5.3y's own "what sits upstream" open item with a clean negative (2026-10-11)

**Scope.** Direct follow-up, orchestrator-routed as the top video blocker: after Team B merged
the cursor-separation fix from §5.3y, their flat-fill reads gave 87/8 instead of the ground-truth
153, and block (0,0) gave 41 instead of 24 — even with every individual decode stage
independently verified. Working hypothesis: both cursors read from the wrong buffer, or the right
buffer at the wrong starting offset. Live Unicorn capture against the unmodified DLL/`.bik`,
hooking each cursor's first real read for frame 0, Y-plane.

**Finding — disconfirmed. Both cursors already point at their ground-truth byte, at offset 0, in
the real DLL.** DC-literal cursor (dispatch idx0): buffer `0x303f8620`, first word `0x0076` (118)
at offset 0 — exact match, reconfirming §5.3o/§5.3p's own prior finding independently. Written by
`0x1001a160`'s own DC-word write site (`0x1001a307`) — a stack-local scratch array. Flat-fill
cursor (dispatch idx1): buffer `0x303f9bb0`, first byte `0x99` (153) at offset 0 — exact match.
Written by a DIFFERENT function, `0x100195b0` (write site `0x100197dc`) — a structural sibling of
`0x1001a160`, not the same function and not a `0x10019360`-style descriptor-A/B pre-stage call;
also a stack-local scratch array, independently confirmed ~0x1390 bytes from the DC buffer
(matching §5.3y's own distance measurement exactly).

**New confirmation: `0x100195b0`'s own bit-reader argument is the SAME shared main-bitstream
context address (`0x303f4e9c`) already pinned for the AC-tree and the DC-word decoder** — flat-
fill bytes are decoded from the same underlying per-row bitstream as everything else, just
through a sibling routine into its own separate output array. Its own bitstream cursor advances
through raw frame bytes (`FRAME_BUF_BASE`-relative) across successive calls, confirming real
compressed input, not a pre-decoded intermediate buffer.

**Net: buffer identity and starting offset are NOT the bug for either cursor, in either of the
two instances traced.**

**Critical cross-reference, same day, caught by Team B reading this section against §5.3s
(SPEC TEAM, self, direct check of already-published text — no new capture needed): `0x303f9bb0`
is not just "a buffer `0x100195b0` happens to write" — it is the EXACT SAME output array §5.3s
already fully documents as that function's own real tree-coded decode output, a persistent
650-byte signed-residual stream (`0x303f9bb0`-`0x303f9e39`, first byte there published as `-103`
signed = `0x99`/153 unsigned — the identical byte, identical address, as this section's own flat-
fill first-read value).** This means flat-fill blocks are NOT triggering any separate per-block
decode at the call site traced here — they are reading sequentially further into the SAME
650-byte array `0x100195b0` already fully decoded as part of the shared entropy pre-stage,
exactly the same "descriptor L" convention (`0x1001a160`'s own buffer, §5.3p) already established
for DC words. **This resolves the real remaining bug for Team B: their earlier modeling of
`0x100195b0` as a "measured-total skip" (pay the right bit cost, discard the actual decoded
content, since nothing consumed its real output at the time it was first characterized) is now
known to be wrong — flat-fill blocks DO consume that real output, sequentially, the same way DC
blocks consume `0x1001a160`'s. The fix is to actually implement `0x100195b0`'s real tree-coded
decode (§5.3s's own already-published algorithm: leading magnitude as literal count, a two-LUT
tree lookup, a fast-fill shortcut, persistent state across calls) instead of skip-paying it, and
wire flat-fill reads to pull sequentially from that real decoded array instead of a separate
`FlatFillByteCursor` abstraction.**

- **Evidence:** live Unicorn capture against the unmodified `binkw32.dll`/`main_menu_bg.bik`;
  both cursor addresses/first-read values and both writer PCs captured directly, not inferred;
  the writer `0x100195b0` located via fresh static disassembly then confirmed live via a second,
  independent hook reading its own arguments (bit-reader address, advancing frame-relative
  bitstream cursor). Independently re-verified before relaying to Team B — the raw capture dump
  (`scratchpad/bink_literal_cursor_source_2026-10-11/all_writes_count.txt`) matched the agent's
  claimed addresses exactly. The §5.3s cross-reference (same buffer, same first byte) is a direct
  textual check against this document's own already-published, independently-landed worked
  example — no new capture needed, exact address and byte-value match (`-103` signed =
  `0x99`/153 unsigned, same byte).
- **Confidence:** CONFIRMED for both cursors' buffer addresses, first-read values (0x76/118 and
  0x99/153), and exact writer instructions, all live and reproduced. CONFIRMED that the flat-fill
  writer shares the same main-bitstream context address as the AC-tree/DC-writer. CONFIRMED that
  `0x303f9bb0` is the identical buffer §5.3s already documents as `0x100195b0`'s own real
  650-byte tree-coded decode output (exact address + byte match, not inference). PLAUSIBLE that
  flat-fill blocks generally (beyond idx1) consume this same array sequentially rather than
  triggering separate decodes — strongly suggested by the shared-buffer finding but not traced
  block-by-block across the whole row this pass.
- **Unknowns:** whether the same "right buffer, right offset" result holds for later blocks/rows/
  chroma planes (only the first occurrence of each engine was checked); the precise nature of Team
  B's own remaining bug — outside this capture's scope, their own code was not inspected. **CLOSED
  2026-10-11, see new §5.3aa: the exact byte-level decode math inside `0x100195b0` itself** —
  instruction-level tracing plus a live per-symbol capture across the FULL 650-symbol call found
  the two-nibble-tree structure and the exact `cur_class` update rule, zero exceptions.

### 5.3aa `0x100195b0`'s own per-symbol tree-decode body, instruction-exact: two nibble-trees per byte, and the exact `cur_class` update rule — closes §5.3z's own named Unknown, routed directly by both the orchestrator and Team B after Team B's own 4 structural guesses all failed (2026-10-11)

**Scope.** Direct response to a precise, converging ask from both live peers: Team B's own
`0x100195b0`-builder stopgap now reproduces the two per-class LUT banks exactly (§5.3g's own
`A_array`, confirmed byte-for-byte, Team B commit `803855a`), but 4 independent structural guesses
at the per-symbol decode BODY itself all failed against the real 650-byte/3384-bit golden fixture
(frame 0/Y-plane/row 0, first call) — closest was a "nibble-pair-per-byte, translated-value
`cur_class`" model at +2.7% bits and still wrong byte values from the first byte. Full static
disassembly of `0x100195b0`'s own body (`capstone`, direct from `binkw32.dll`, 231 instructions —
exact instruction count match to §5.3s's own prior full-body dump, cross-confirming this is the
same function/build) plus a new live Unicorn per-symbol trace
(`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_195b0_curclass_trace_2026-10-11.py`),
hooking every stage of the loop body across the FULL real 650-symbol call, not just a handful of
samples.

**Finding — CONFIRMED, zero exceptions across all 650 symbols of the real call: each output byte
is built from TWO separate 4-bit tree-lookups, one per-class (driven by `cur_class`) and one
global (the same single descriptor `0x10019360` itself uses), and `cur_class` for symbol i+1 is
set to exactly symbol i's own per-class nibble output — not the raw symbol index, not the combined
byte, not a fixed/periodic update.**

```c
// 0x100195b0's per-symbol loop body, instruction-exact (0x1001968d-0x100197df)
// entered once per symbol, `remaining` times (remaining = leading magnitude, 650 here, §5.3s)
uint8_t cur_class = ctx->class_state;           // ctx+0x180, loaded ONCE before the loop starts
for (; remaining > 0; remaining--) {
    // --- nibble 1: PER-CLASS tree, indexed by cur_class ---
    uint32_t A1 = arg3->A_per_class[cur_class];        // arg3 + cur_class*4 + 0x100 (S5.3g's A_array)
    uint8_t* lut1 = arg3->lut_ptr_per_class[cur_class]; // arg3 + cur_class*4 + 0x140
    uint32_t idx1 = peek_bits(br, A1);
    uint8_t packed1 = lut1[idx1];                       // code_len1<<4 | raw_symbol1
    consume_bits(br, packed1 >> 4);                     // code_len1
    uint8_t nib1 = ctx->translate256[cur_class][packed1 & 0xF]; // CORRECTED 2026-10-11 (Team B's
                                                          // own implementation caught this): the
                                                          // table is at ctx + cur_class*16 +
                                                          // raw_symbol1 -- on arg1/ctx (the
                                                          // idle-check/output-descriptor struct,
                                                          // the SAME struct whose +0x30 is
                                                          // SCRATCH_BASE and +0x180 is cur_class),
                                                          // NOT on arg3 as originally published.
                                                          // A 16-class x 16-symbol byte table at
                                                          // ctx+0x00..0xFF; arg3's own +0x100/
                                                          // +0x140 per-class LUT arrays are a
                                                          // separate struct, confirmed unrelated.

    // --- nibble 2: CORRECTED 2026-10-12 (Team B's own per-symbol diff caught a real bug here --
    // live capture confirmed ctx+0x20/+0x24 are NOT separate scalar fields at all, they're just
    // more bytes of the SAME ctx+0x00..0xFF translate table nib1 reads (confirmed: the dword at
    // ctx+0x20 decodes to the exact permutation-table byte values at that offset, not a sane
    // max-code-length). The REAL per-class A_array/LUT-pointer arrays live at ctx+0x100 (16
    // dwords) and ctx+0x140 (16 dwords) -- confirmed live, exact match to the already-published
    // §5.3g A_array (4,6,6,6,6,5,5,5,5,5,5,5,5,5,6,4). FINAL, Team B-confirmed 2026-10-12 (full
    // 650-byte output byte-for-byte match, bit cost exactly 3384): nib2 is NOT cur_class-indexed
    // at all -- it always reads through class 0's own slot of these same arrays, unconditionally,
    // regardless of the current cur_class. This is the function's own real "global" (class-
    // independent) nature: physically co-located with the per-class array's class-0 entry, not a
    // separate descriptor and not cur_class-indexed like nib1 is.
    uint32_t A2 = ctx->A_array[0];                        // ctx + 0x100, ALWAYS class 0
    uint8_t* lut2 = ctx->lut_ptr_array[0];                // ctx + 0x140, ALWAYS class 0
    uint32_t idx2 = peek_bits(br, A2);
    uint8_t packed2 = lut2[idx2];
    consume_bits(br, packed2 >> 4);                      // code_len2
    uint8_t nib2 = ctx->translate16[0][packed2 & 0xF];    // ctx + 0 + raw_symbol2, ALWAYS class 0
                                                            // (the per-class 16x16 table's class-0
                                                            // row, same table nib1 reads but
                                                            // unconditionally fixed to class 0)

    uint8_t combo = (nib1 << 4) | nib2;                   // 0x100197c0-0x100197c3
    int8_t out_byte;                                      // sign/bias transform, 0x100197c5-0x100197d9
    if (combo & 0x80) out_byte = -(int8_t)(combo & 0x7F); // top bit set: negate low 7 bits
    else               out_byte = (int8_t)(combo & 0x7F); // top bit clear: keep low 7 bits
    out_byte -= 0x80;                                      // then bias by -128, both paths
    *out++ = (uint8_t)out_byte;

    cur_class = nib1;                                      // <<< THE UPDATE RULE: next symbol's
                                                              // per-class tree selector IS this
                                                              // symbol's own nibble-1 output, every
                                                              // single time, confirmed 650/650
}
ctx->class_state = cur_class;                              // written back ONCE, at the function's
                                                              // own single `ret` (0x1001980d)
```

**CORRECTION, 2026-10-11, caught by Team B's own careful implementation and diagnostic — the
translate-table base address above was published wrong, now fixed in place in the code block
above.** Team B implemented this section literally, field-for-field, and it did NOT reproduce the
golden fixture (first byte 134 vs. the real 153; total bit cost 3561 vs. the real 3384). Their own
bounded diagnostic (bit-offset scan, swapping which tree is per-class vs. global, trying
descriptor B instead of A for the global tree, trying `cur_class` sourced from nib2 or from the
raw symbol instead of nib1, reversing the nibble order in the combo byte) reproduced NONE of
these — but independently reconfirmed the per-class `classByte` sequence
(`4,6,6,6,6,5,5,5,5,5,5,5,5,5,6,4`) matches §5.3g's own `A_array` exactly, correctly narrowing the
bug to the translate step specifically rather than the per-class banks. A fresh live capture
(`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_195b0_translate_addr_2026-10-11.py`)
confirms the real read address for nibble 1's translate lookup is **`ctx + cur_class*16 +
raw_symbol1`** — on `arg1`/`ctx` (the idle-check/output-descriptor struct), **not** `arg3` as
originally written above. Confirmed two independent ways in the live trace: for symbol 0
(`cur_class=0`, `raw_symbol1=1`) the address is exactly `ctx+1`; for every steady-state symbol
(`cur_class=1`, `raw_symbol1=0`) it is exactly `ctx+0x10` — both matched against a byte-snapshot
of `ctx+0x00..0xFF` taken at the exact moment of the read (an end-of-emulation snapshot gave
garbage/pointer-looking data the first time this was attempted, because this region is a reused
per-row scratch buffer overwritten later in the same run — a trap worth remembering for any future
capture of this struct). The real table, captured live for this exact row (frame 0/Y/row 0, first
call):

```
ctx+0x00: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f   (class 0)
ctx+0x10: 01 02 0e 03 00 04 05 06 07 08 09 0a 0b 0c 0d 0f   (class 1)
ctx+0x20: 02 01 03 04 00 05 06 07 08 09 0a 0b 0c 0d 0e 0f   (class 2)
ctx+0x30: 02 03 01 04 05 06 07 08 09 0e 00 0a 0b 0f 0c 0d   (class 3)
ctx+0x40: 02 03 04 01 05 06 07 08 09 0e 00 0a 0b 0f 0c 0d   (class 4)
ctx+0x50: 02 05 03 04 01 06 00 07 08 09 0a 0b 0c 0d 0e 0f   (class 5)
ctx+0x60: 02 06 01 07 05 03 00 04 08 09 0a 0b 0c 0d 0e 0f   (class 6)
ctx+0x70: 02 01 07 06 05 03 08 04 00 09 0a 0b 0c 0d 0e 0f   (class 7)
ctx+0x80: 02 08 01 07 06 03 0e 09 00 04 05 0a 0b 0c 0d 0f   (class 8)
ctx+0x90: 02 01 00 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f   (class 9)
ctx+0xa0: 01 02 09 03 0e 07 00 04 05 06 08 0a 0b 0c 0d 0f   (class 10)
ctx+0xb0: 01 0e 02 0b 07 08 00 03 04 05 06 09 0a 0c 0d 0f   (class 11)
ctx+0xc0: 01 0e 0d 0c 02 0b 00 03 04 05 06 07 08 09 0a 0f   (class 12)
ctx+0xd0: 0d 0e 01 02 0c 09 00 03 04 05 06 07 08 0a 0b 0f   (class 13)
ctx+0xe0: 0e 01 02 0d 03 0c 00 04 05 06 07 08 09 0a 0b 0f   (class 14)
ctx+0xf0: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f   (class 15)
```
Every row is itself close to a permutation of 0-15 (not confirmed exhaustively bijective this
pass), which is consistent with a genuine translate/remap table rather than arbitrary data. The
rest of this section's own findings (the two-nibble structure, the global tree on `ctx`, the
`cur_class = nib1` update rule, the sign/bias transform, the one-time sign-probe bit) are
unaffected by this correction — only the translate table's own base struct was wrong.

- **Evidence:** live Unicorn re-capture, reusing the exact same call-site identification already
  validated in this section (confirmed the resolved indirect-call target really is `0x100195b0`
  before trusting the address, not assumed); the address formula cross-checked against two
  independent symbols (class 0/symbol 1 and the class 1/symbol 0 steady state) and the resulting
  table values matched the already-independently-known `nib1` outputs exactly for both.
- **Confidence:** CONFIRMED — live capture, two independent address/value cross-checks, against
  the unmodified DLL.
- **Unknowns:** whether every row of the real table is an exact bijection of 0-15 (not checked
  row-by-row); whether `ctx`'s own `+0x00..0xFF` region has this same role in `0x100195b0`'s OTHER
  calls (only this one row/call was captured).

**CORRECTION, 2026-10-12, a second real bug caught by Team B's own per-symbol diff against their
own implementation — nib2's claimed "global tree at ctx+0x20/+0x24" was also wrong, same root
cause (confusing a flat offset with a region that's actually something else).** Team B's own
debug diff found their symbol-0 `packed2` had the WRONG CODE LENGTH (2 bits, not the published
4) — not just a wrong translated value, meaning the tree itself was wrong, not just the lookup
result. A fresh live capture (`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/
probe_195b0_ctx_tree_2026-10-12.py`) confirms `ctx+0x20`/`ctx+0x24` are NOT separate scalar fields
at all — they're simply more bytes of the SAME `ctx+0x00..0xFF` translate table documented above
(the dword at `ctx+0x20` decodes to exactly the surrounding permutation-table bytes, not a
plausible max-code-length value). **The real per-class arrays live at `ctx+0x100` (16 dwords,
max-code-length per class) and `ctx+0x140` (16 dwords, LUT pointer per class)** — confirmed live,
and the 16-value sequence read from `ctx+0x100` EXACTLY matches the already-published §5.3g
`A_array` (`4,6,6,6,6,5,5,5,5,5,5,5,5,5,6,4`); class 0's own entry is `4`, matching Team B's own
EXPECTED code length exactly, strongly confirming the fix. **Most likely correct formula for
nib2 (not independently re-verified bit-for-bit against a full symbol trace this pass, but
consistent with every piece of evidence gathered): class-indexed exactly like nib1, not a flat
global field** — `A2 = ctx+0x100+cur_class*4`, `lut2 = *(ctx+0x140+cur_class*4)`, and the final
translate step uses the SAME per-class 16×16 table nib1 reads (`ctx+cur_class*16+raw_symbol2`),
not a fixed `ctx+0x10` row. Code block above corrected in place.

- **Evidence:** live Unicorn capture of the real frame0/Y/row0 call, dumping `ctx+0x00..0x14F` in
  full; the `ctx+0x100` sequence cross-checked byte-for-byte against §5.3g's independently
  published `A_array`; `ctx+0x140` onward resolves to real pointers in the expected `0x1003bxxx`
  LUT-table address range (matching the shape of descriptor A/B's own LUT pointers, though not
  identical values — confirmed a DIFFERENT, dedicated array, not an alias of desc A/B).
- **Confidence:** CONFIRMED for `ctx+0x20/+0x24` being garbage/mislabeled, and for the real
  arrays' location and class-0 value matching `A_array`. PLAUSIBLE, not independently re-verified
  this pass, for the exact class-indexed formula proposed above — Team B should re-test against
  the full symbol trace before promoting to production.
- **Unknowns:** none remaining — see the CLOSED note below, Team B's full byte-for-byte match
  settles this.

**ALGORITHM CLOSED, 2026-10-12 [Team B, independently confirmed]: full 650-byte §5.3aa output now
byte-for-byte identical to the real fixture, bit cost exactly 3384.** Two fixes landed together:
(1) the `ctx+0x28`-is-14-not-10 pre-loop width fix (above), which alone fixed byte 0 and the exact
bit total; (2) **nib2 is NOT class-indexed at all — it always reads class 0's own slot,
unconditionally** (code block corrected in place above). Found via symbol 1 (`cur_class`=1, own
`A_array[1]`=6): nib2's own `code_len` there was still 4 (class 0's `A_array[0]` value), not 6 —
proving nib2 ignores `cur_class` entirely and always targets slot 0. This is the function's real
"global" nature: physically co-located with the per-class arrays' own class-0 entry, not a
separate descriptor and not cur_class-indexed like nib1. With both fixes, Team B's full 650-byte
decode matches the fixture exactly (first 64 bytes, last 8 bytes, total size all verified).

**CONFIRMED, 2026-10-12, byte-for-byte: the class-indexed fix above is structurally correct —
Team B's remaining mismatch is a bit-alignment/index issue, not a table-identity one.** Dumped
the actual LUT content at `ctx+0x140[0]` (class 0's own LUT pointer target) live: a simple linear
byte sequence, `0x40, 0x41, 0x42, ...` (index *i* → packed value `0x40+i`). Both Team B's own
post-fix read AND the already-published trace's own read are each exactly one entry of this SAME
table — just at different indices (Team B's `packed1`=entry 0, the published trace's
`packed1`=entry 1; Team B's `packed2`=entry 1, the published trace's `packed2`=entry 9). Since
every value in dispute, on both sides, is a genuine entry of the identical table, the per-class
table identity/structure is settled — the remaining gap is that Team B's own `idx1`/`idx2 =
peek_bits(br, A2)` computation is landing on a different index than the real decode, almost
certainly from a bit-count/alignment discrepancy earlier in their own pipeline (not a wrong tree).

**One additional one-time cost, NOT part of the per-symbol loop:** immediately after the leading
magnitude is decoded and validated (§5.3s), and before the first loop iteration, the function reads
exactly 1 extra bit (`0x10019656`-`0x10019689`) that, if set, transforms the magnitude itself via
`magnitude = -20 - magnitude` (an edge-case path for a negative/sentinel magnitude, never observed
firing in this real call — the bit was 0 here, magnitude stayed +650). This 1 bit is real bitstream
cost (included automatically in the live-measured 3384-bit total) but is easy to miss or
mis-model as part of the per-symbol loop if reconstructing from static reading alone — flagging it
explicitly since Team B's own guesses may have folded it into (or omitted it from) the wrong place.

**CORRECTED, 2026-10-12 [CONFIRMED, live measurement + independent fixture test]: the pre-loop
cost is 15 bits total, not 10+1=11 — a real, previously-missing 4 bits, found while chasing
Team B's idx1/idx2 per-symbol misalignment.** Direct live register-state capture (hooking the
function's own entry and the first per-symbol loop-top at `0x10019690`, reading `edx`/`esi`/`edi`
and the `[ebp-4]` cursor spill directly — the shared bitreader struct in memory is stale through
this whole pre-loop section too, same trap as the per-symbol loop) measured the cursor advancing
exactly 32 bits (one refill) between function entry and the loop's first iteration, with a
bits_avail delta putting the real pre-loop cost at **15 bits**, not 11. Team B independently
confirmed by extracting the fixture's own raw bits at the corrected position (entry `2871` + 15 =
**2886**): `idx1=1`/`idx2=9` at `[2886,2890)`/`[2890,2894)` — an EXACT match to this section's own
published symbol-0 trace. **The already-published 10-bit leading count and 1-bit sign probe
(§5.3s above) are confirmed correct in isolation — there is a separate, as-yet-unidentified
4-bit field consumed somewhere in the pre-loop sequence that this document has not yet located
or characterized.** Not chased to the instruction level this pass (the live bit-count measurement
settled the practical question — where symbol 0 really starts — without needing to pin down
exactly which 4 bits they are or what they encode). Flagged as a genuine OPEN item for whoever
wants the full pre-loop sequence named precisely.

**CLOSED, 2026-10-12 [CONFIRMED via disassembly + live capture]: the missing 4 bits are fully
explained — the leading magnitude is NOT a flat 10-bit field at all.** It's read through the
exact same `ctx+0x28`/`ctx+0x2c` fixed-bound shift-chain magnitude-extraction idiom already
documented for `0x1001a160`'s own value[0] (§5.3p/§5.3q) — `ctx+0x28` holds the real bit-width to
read (loaded into `ebx` at `0x100195cb`, feeding `mov ecx,0x20; sub ecx,ebx; shr eax,cl`),
`ctx+0x2c` is the upper-bound validation (`cmp eax,[ecx+0x2c]` at `0x10019625`). Live-captured for
this exact call: **`ctx+0x28` = 14** (not 10), `ctx+0x2c` = 15872 (upper bound), decoded magnitude
= 650 (matches the already-published value exactly). So the real pre-loop cost is **14-bit
leading magnitude + 1-bit sign probe = 15 bits**, matching the live-measured total exactly — the
original "10-bit" figure was simply the wrong assumed field width, not a missing field. §5.3s's
own prose above should be read as "a WIDTH READ FROM `ctx+0x28`, 14 in this instance, not a fixed
10" wherever it says "10-bit leading count."

**CORRECTED, 2026-10-12 [orchestrator-routed, Team B's promotion attempt crashed on mm_p_03 —
"bad allocation"]: `ctx+0x28`=14 is NOT a fixed constant for this call site — it is derived
PER-PLANE from `ctx+0x2c` (the bound) and changes within a single video.** Live-captured all 75
calls to `0x100195b0` across frame0 of `main_menu_bg.bik`: calls 0-57 (one plane) all have
`ctx+0x28`=14 / `ctx+0x2c`=15872; calls 58-74 (the next plane) all have `ctx+0x28`=13 /
`ctx+0x2c`=8192 — same function, same frame, different plane, different width. Across every one
of the 75 calls, `preloop_bits_consumed` is exactly `ctx+0x28`+1 with no exceptions (the 1-bit sign
probe is a real, constant, separate cost, never folded into the width). **A reimplementation that
hardcodes width=14 (or any other flat value) will desync on any plane whose own bound differs from
15872 — this is almost certainly why Team B's promoted decoder corrupted mm_p_03's video (different
resolution/plane sizes → different bound → different real width), not a bounds-check gap in
`cur_class`.** Both `ctx+0x28` and `ctx+0x2c` must be read live per call, never assumed. Script:
`probe_195b0_multicall_2026-10-12.py` in `scratchpad/bink_entropy_prestage_algorithm_2026-10-08/`.

**Live per-symbol trace, first 20 of 650 symbols (full table of all 650 in
`probe_195b0_curclass_trace_out.txt`), reproduces §5.3s's own published first-20-byte sequence
exactly (`-103,-103,-103,-103,-103,-103,-103,-103,-103,-102,-103,-103,-102,-102,-102,-103,-103,
-98,-102,-103`):**

| symbol i | cur_class_in | packed1 (code_len1,raw1) | nib1 (=next cur_class) | packed2 (code_len2,raw2) | nib2 | combo | final signed byte |
|---|---|---|---|---|---|---|---|
| 0 | 0 | 0x41 (4,1) | 1 | 0x49 (4,9) | 9 | 0x19 | -103 |
| 1 | 1 | 0x10 (1,0) | 1 | 0x49 (4,9) | 9 | 0x19 | -103 |
| 2-8 | 1 | 0x10 (1,0) | 1 | 0x49 (4,9) | 9 | 0x19 | -103 |
| 9 | 1 | 0x10 (1,0) | 1 | 0x4a (4,10) | 10 | 0x1a | -102 |
| 17 | 1 | 0x10 (1,0) | 1 | 0x4e (4,14) | 14 | 0x1e | -98 |

Explains the "long flat run" shape Team B flagged as the tell their 4 guesses all missed: once
`cur_class` locks at 1 (which it does from symbol 1 onward through this entire sampled window and
most of the call — class 1's own code for raw-symbol 0 is a cheap 1-bit code, `0x10`, producing a
STABLE `nib1=1`, which immediately re-selects class 1 again next symbol — a genuine fixed point),
the output's high nibble stays locked at 1 and all visible variation comes from nibble 2's own
global tree alone — a small, low-cost code producing the tight `-96..-104` band actually observed.
`cur_class` does change later in the same call (first at symbol 76, confirmed in the full 650-entry
trace), and every single transition — all 650 symbols, zero exceptions — obeys `cur_class[i+1] =
nib1[i]` exactly, with no other rule needed.

**Why Team B's closest guess (attempt 1: "nibble-pair-per-byte, translated-value `cur_class`")
still failed, diagnosed from this trace, not from reading their own code (no access):** the
UPDATE RULE direction (translated value → next class) was right, but getting the WRONG bytes from
the very first byte and the wrong total bit cost (3474 vs 3384, +2.7%) means at least one
structural detail differed from the mechanism above — most likely candidates, in order of
plausibility given the shape of the error: using the COMBINED byte or nibble 2 (not nibble 1
specifically) as the class-update value; swapping which tree is "per-class" vs "global" (i.e.
reading nibble 2 from the class-indexed bank and nibble 1 from the global one); or omitting/
mis-placing the one-time 1-bit magnitude sign-probe described above. Not independently verified
against Team B's own reverted code (unavailable).

- **Evidence:** fresh static `capstone` disassembly of `0x100195b0`'s full body (231 instructions,
  exact count match to §5.3s's own independent dump); live Unicorn per-symbol trace against the
  unmodified `binkw32.dll`/`main_menu_bg.bik`, hooking every stage of both nibble-lookups (packed
  byte, translated nibble, fast/slow refill variants) across the ENTIRE real 650-symbol call, not
  a sample — the `cur_class[i+1] == nib1[i]` rule holds with 0 mismatches out of 650; the reproduced
  byte sequence (first 20 values) matches §5.3s's own independently-published first-20-byte dump
  exactly, and the total bit cost (3384) matches §5.3r/§5.3s's own independent measurement exactly.
- **Confidence:** CONFIRMED for the two-nibble-tree structure, the `cur_class` update rule (0/650
  exceptions, both fast- and slow-refill code paths sampled), the sign/bias transform formula (byte
  for byte against 20+ live values), and the one-time 1-bit magnitude sign-probe's existence and
  bit cost. PLAUSIBLE, not independently verified, for the specific diagnosis of which detail broke
  Team B's own attempt 1 (no access to their reverted code).
- **Unknowns:** whether `cur_class`'s valid range is exactly 0-15 (16 classes, matching the
  LUT-builder's 16-dword arrays) or whether the translate table can ever emit a nibble 1 value
  outside that range for some other block/file (not stress-tested beyond this one 650-symbol call);
  the exact semantics of the rare negative-magnitude sign-probe path (never observed firing, bit
  was 0 in every call checked this pass).

Full detail/scripts:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_195b0_curclass_trace_2026-10-11.py`,
`probe_195b0_curclass_trace_out.txt`.

### 5.3ab `0x10019d10` and `0x10019f00`'s own consumers and per-symbol structure — closes the "measured-total skip" trap for two more of the 23-call pre-stage siblings, orchestrator-routed after `0x100195b0`'s own version of this trap (§5.3z/§5.3aa) (2026-10-11)

**Scope.** Direct follow-up, same thread as §5.3aa: Team B still pays `0x10019d10` (528 bytes,
2981 bits, §5.3t) as a bit-cost-only skip — the exact trap `0x100195b0` was in until §5.3z found
its real output feeds flat-fill. Two questions, both answered by live Unicorn capture (memory-read
watches over each function's own output buffer, same method as §5.3p/§5.3z's buffer-source
tracing) plus fresh static `capstone` disassembly of both function bodies in full (`0x10019d10`:
183 instructions, exact count match to §5.3t's own prior dump, confirming same build; `0x10019f00`:
218 instructions, full body read for the first time this pass).

**ADDENDUM, 2026-10-12 [orchestrator-routed, Team B reported a 12-bit overrun in their own
`0x10019d10` reimplementation: 2993 measured vs 2981 published]:** confirmed `0x10019d10` uses the
identical `ctx+0x28`/`ctx+0x2c` fixed-bound shift-chain magnitude-extraction mechanism as
`0x100195b0` (§5.3aa) — same instruction pattern at `0x10019d2b`/`0x10019d91` (`mov edx,[ebx+0x28]`
… `cmp edi,[ebx+0x2c]`). Live-captured for this exact call: `ctx+0x28` = 12, `ctx+0x2c` = 2432,
decoded magnitude = 528 (exact match to the word count this section already documents). Unlike
`0x100195b0`, found NO extra 1-bit sign-probe anywhere in this function's own pre-loop sequence —
it goes straight from the magnitude decode to the upper-bound check to the output-cursor pin, so
the real pre-loop cost here is just the 12-bit width, no +1. Also checked: `ctx+0x20`
(=6)/`ctx+0x24` (=0x1003beb0) read sensible, real values here — this function has its own
dedicated global tree from the start (already established below: no class-carry, single shared
tree for both nibbles), so the §5.3aa "nib2 reads garbage/wrong-class" bug class does not apply —
there is no per-class aliasing to go wrong. Team B's reported 12-bit overrun numerically matches
this exact width value; flagged to them as worth checking whether their implementation hardcodes
the width instead of reading it live from `ctx+0x28`, or consumes this field twice. Not yet
independently confirmed which of those it is (no access to Team B's own code this pass).

**CLOSED, 2026-10-12 [orchestrator-routed]: the `ctx+0x28`/`ctx+0x2c` formula is now fully derived,
no bitstream read involved.** A live memory-write watchpoint on the ctx struct's own `+0x2c` dword
(static search of the obvious call chain had missed it) found the real writer at
`0x1001a742`-`0x1001a757`, in the per-plane/per-table ctx-setup block (runs once before that
plane's run of pre-stage calls, well before `0x100195b0`/`0x10019d10` are ever invoked):
```
mov edx, [ebp-0x218]      ; dim = this table's own axis count (pixel width for Y/chroma, MB-column
                           ; count for 0x10019d10-style per-MB tables)
shl edx, 6                 ; dim * 64
shr edx, 3                 ; -> dim * 8
add edx, 0x20f             ; + 527
and edx, 0xfffffff0        ; round DOWN to a multiple of 16
                            ; -> this value becomes ctx+0x2c
```
For even `dim` (always true for every case seen), this reduces exactly to **`bound = 8*dim + 512`**.
Verified against all 3 known data points: `dim`=1920 (Y pixel width) → 15872; `dim`=960 (chroma
pixel width) → 8192; `dim`=240 (`0x10019d10`'s MB-column count, `WIDTH/8`) → 2432 — all exact. A
separate, earlier block in the same setup function (`0x1001a6fe`-`0x1001a73b`) independently
computes the `ctx+0x28` width field via a direct `bsr`/bit-length operation on a sibling dim slot,
but since **`width = bound.bit_length()` holds exactly for all 3 points**, a reimplementation only
needs the one `bound` formula above and can derive `width` from it directly — no separate
computation or bitstream read needed for either field.

**`0x10019d10` — CONFIRMED, two real consumers, both already-documented engines.** A live
memory-read watch armed on the buffer immediately after its first real call commits
(`0x303f9210`-`0x303f941f`, 528 bytes) found exactly two distinct reader groups for the rest of
the frame's decode, both landing inside already-named case bodies from §5.3a:

- **Giant2 / secondary=5 (TwoLiteralPattern), `0x1001b5a3`-`0x1001b90e`:** 4 distinct reader
  instructions, each firing once per 4-byte stride (byte offsets `+0x10,+0x14`, `+0x48,+0x4c`,
  `+0x60,+0x64`, ... — one 4-byte group per TwoLiteralPattern block). This is EXACTLY §5.3a's own
  already-documented `control_byte` — the value giant2 uses to compute `idx = (control_byte>>shift)
  & 3` and select the 2-bit blend mask from `0x1003c130`/`0x1003c200` (§5.3a, lines ~578-587).
  **`0x10019d10`'s 528-byte decoded array IS secondary=5's own control-byte stream** — not a
  separate, unmodeled input, but the literal source of a quantity this doc already fully
  characterizes downstream.
- **Primary case 8, `0x1001c608`-`0x1001c960`:** 8 distinct reader instructions, each firing once
  per 8-byte stride — the 4-bit nibble selector §5.3a already documents case 8 using to index the
  4-bit blend-mask pair (`0x1003c180`/`0x1003c1c0`, §5.3a lines ~589-610). Same relationship: the
  nibble case 8 reads comes directly from this same decoded array, one byte supplying one nibble
  selector per 8-pixel group (only the low nibble is used per §5.3a's own table; which half of each
  byte case 8 vs. giant2 actually consumes was not disambiguated further this pass).

**Per-symbol structure, CONFIRMED by full static read (no live per-symbol table given — the
mechanism has no class-carry ambiguity to resolve live, unlike `0x100195b0`): the SAME FIELD
LAYOUT CONVENTION as `0x10019360`'s own descriptor and `0x100195b0`'s own nibble-2 lookup —
`ctx+0x20`(max code length)/`ctx+0x24`(LUT pointer)/`ctx+0x10`(16-byte translate table) — read
TWICE per output byte (low nibble first at `0x10019dcf`/`0x10019e13`

**Correction, 2026-10-11 [CONFIRMED via disassembly, Team B's own diagnostic prompted this
re-check]: this is NOT the same tree INSTANCE as descriptor A or B — the wording above
("a SINGLE shared global tree") overclaimed.** Traced the per-row orchestrator's (`0x1001a530`)
own call-site argument marshaling for this exact call (`0x1001ab08`): the `ctx` pointer passed is
`&[ebp-0x6c]`, a small local the orchestrator itself zero/8-initializes a few fields of
(`ebp-0x6c`/`-0x68`=0, `-0x64`=8, `-0x60`=0) immediately before the call — a completely different
stack address from descriptor A's `&[ebp-0x170]` or descriptor B's `&[ebp-0x1a4]` (both filled by
`0x10019360` itself). This `ctx`'s own tree fields (`ctx+0x20`/`ctx+0x24` = `ebp-0x4c`/`ebp-0x48`)
are populated by their OWN dedicated call to the same generic per-row table-builder,
`0x10018ca0` (`0x1001aa24`-`0x1001aa2a`: `lea edx,[ebp-0x4c]; lea ecx,[ebp-0x48]; call
0x10018ca0`) — one of the 23-call pre-stage siblings §5.3g already names, but a SEPARATE
invocation building a tree specific to this call, not a copy of or reference to descriptor A/B's
tree. `ctx+0x30` (output-buffer base, `ebp-0x3c`) is likewise filled independently, from a
different source object's `+0xc` field (`0x1001a7c1`), not from descriptor A/B either. **Answers
Team B's own question directly: `0x10019d10`'s tree is a distinct, freshly-built instance per
call, not literally the same memory `0x10019360`'s descriptor or `0x100195b0`'s nibble-2 lookup
reads — only the field-offset convention (and the `0x10018ca0`-family build mechanism) is shared.**
A reimplementation that points this call at descriptor A/B's own already-built tree (rather than
building its own fresh one via its own `0x10018ca0` call) would read the wrong tree content and
very plausibly explain a bit-cost mismatch of exactly the kind Team B is chasing (2993 measured vs
2981 expected, a 12-bit overrun) — not independently verified against Team B's own fixture this
pass, but a strong, structurally-motivated lead to check first.
fast/slow, high nibble second at `0x10019e51`/`0x10019e7f` fast/slow), combined as
`(hi<<4)|lo`, with an explicit `dec`-to-zero loop counter (`0x10019db4`/`0x1001a0b2`-style,
confirmed count-driven, matching §5.3t's own prior characterization). **No `+0x180`-style
persistent state is ever written anywhere in this function's 183 instructions — confirmed by
reading the full body, not inferred: `0x10019d10` has NO class-carry at all, unlike
`0x100195b0`.** Live reproduction of the first 8 output bytes (`0f,01,00,00,00,00,80,ff`) matches
§5.3t's own published sequence exactly, confirming this is the same call/mechanism. A per-byte
packed/code-length attribution table (mirroring §5.3aa's) was attempted but the quick capture used
this pass had an unresolved fast/slow-path bookkeeping ambiguity past the first few bytes — not
force-fit; flagged as a loose end below rather than reported as fact.

**`0x10019f00` — a THIRD, genuinely distinct mechanism, not a copy of either sibling above.**
Full body read for the first time (218 instructions, `0x10019f00`-`0x1001a13b`). Same idle-check/
leading-magnitude/bound-check prologue as all siblings. Per output byte, reads exactly 1 bit first
(`0x10019fbc`, the standard refill-aware single-bit idiom) that selects between two entirely
different sub-mechanisms — **this is a genuine per-byte mode selector, not a whole-call one like
`0x10019360`'s `mode_bit` (§5.3g):**
- **Bit clear** (`0x10019fc5` onward): one single lookup against the SAME global tree
  (`ctx+0x20`/`+0x24`/`+0x10`) produces a magnitude directly (not nibble-paired); a SEPARATE
  trailing 1-bit read (`0x1001a07e`-`0x1001a0a3`, same refill-aware idiom) then conditionally
  negates it (`neg eax` if that bit is set) — sign is a dedicated bit AFTER the magnitude, not
  encoded inside it. Writes one byte, decrements the outer count, loops.
- **Bit set** (`0x1001a0c2` onward): reads a 4-bit nibble instead (fast/slow refill variants); if
  that nibble is nonzero, reads one more bit for sign; then calls the SAME fast-fill helper
  `0x10001eb0` §5.3s/§5.3aa already document, filling **every remaining byte of the whole call in
  one shot** with this single signed value, and returns immediately — a per-byte-chosen, not
  per-call-chosen, bulk-fill-and-bail shortcut, structurally closer to the RLE escape in
  `0x10019360`'s own case-0xC-0xF run-length table (§5.3g) than to anything in `0x100195b0`/
  `0x10019d10`.

**`0x10019f00`'s own consumer — checked the same way, HONEST NEGATIVE this pass.** A memory-read
watch armed on the first call across the whole frame that actually commits a nonzero-length
buffer (every earlier call had leading magnitude 0 and bailed via the idle/zero-magnitude path —
confirmed live, not assumed; the first real commit was call #204 of 271, buffer
`0x303f8f10`-`0x303f9112`, 514 bytes) found **zero reads from that buffer anywhere in the rest of
the frame's decode.** Only one instance was checked (not call #1, which had zero magnitude in this
clip); this does not prove `0x10019f00`'s output is unconsumed in general, but for this specific
real instance in `main_menu_bg.bik` frame 0, nothing reads it back before the capture's own
natural end. Reported as a genuine gap, not forced into either "consumed" or "dead code."

- **Evidence:** fresh full-body static `capstone` disassembly of both functions (183/218
  instructions respectively, directly from `binkw32.dll`); live Unicorn memory-read watches over
  each function's own real output buffer address across the rest of frame 0's decode (not a static
  guess); the giant2/case-8 consumer identification is a direct textual cross-reference against
  this document's own already-published §5.3a addresses and table semantics, re-confirmed by
  address match, not re-derived from scratch; the first-8-byte live reproduction for `0x10019d10`
  matches §5.3t's own independently-published sequence exactly.
- **Confidence:** CONFIRMED for `0x10019d10`'s two consumers (giant2's control-byte stream,
  case 8's nibble-selector stream) and its own no-class-carry single-shared-tree mechanism
  (absence of any `+0x180`-style write confirmed by reading the complete function body, not
  inferred from a sample). CONFIRMED for `0x10019f00`'s own distinct per-byte mode-selector
  structure (bit-decides single-lookup-plus-sign vs. nibble-plus-bulk-fill-and-bail), from full
  static disassembly. PLAUSIBLE, not proven in general, for `0x10019f00`'s output being unconsumed
  — only one real (nonzero-magnitude) instance was checked.
- **Unknowns:** `0x10019d10`'s own exact per-byte bit-cost/packed-value attribution beyond the
  first few bytes (a fast/slow-path bookkeeping ambiguity in this pass's own quick capture, not
  resolved — the TOP-LEVEL mechanism and consumer findings above do not depend on resolving this);
  whether case 8 and giant2 read disjoint halves of each shared byte or genuinely independent
  stride ranges (not disambiguated); `0x10019f00`'s real consumer, if any, for an instance where
  its magnitude is actually nonzero on an EARLIER call than #204, or in a different frame/file;
  whether the remaining uncharacterized members of the 23-call pre-stage group (beyond these three
  named siblings plus the already-closed `0x1001a160`/calls-#3-18-LUT-builder from §5.3g) have
  their own distinct mechanisms — not surveyed this pass.

Full detail/scripts:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/find_19d10_consumer_2026-10-11.py`,
`find_19d10_consumer_out.txt`, `find_19f00_consumer_2026-10-11.py`, `find_19f00_consumer_out.txt`,
`probe_19d10_persymbol_2026-10-11.py`, `probe_19d10_persymbol_out.txt`.

---

### 5.3ac Full Gate A/Gate B/Phase-1-BFS/Gate-B-defer bit trace for two Phase-1-exercising blocks, dispatch idx3 and idx4 — both bit counts and both full coefficient lists reproduce the published ground truth EXACTLY (2026-10-11)

**Scope.** Direct follow-up to §5.3j Part 2 and §5.3w. §5.3w's own 17-bit worked example for block
(0,0) never left Phase 2's small-tree mirror (`A=1`) — Team B's reported idx3/idx4 divergence
(125/140 decoded coefficients vs. the real ~24-25) means their port's bug is specifically in the
real Phase-1 BFS loop and/or Gate B's defer mechanism, neither of which block (0,0) ever exercised.
This section traces dispatch idx3 (`main_menu_bg.bik` frame 0, Y-plane, row 0, col 48 — ground
truth: 83 AC bits, DC=199, 20 nonzero AC) and idx4 (row 0, col 64 — ground truth: 136 AC bits,
DC=212, 30 nonzero AC), both already published in §5.3x, end to end.

**Call-site identity, verified live, not assumed.** §5.3x's own call-count hypothesis (idx3/idx4 are
the 2nd/3rd real `0x10024eb0` call, since idx1/idx2 are secondary=3 flat-fill with no AC-tree call)
was checked directly: a counter hook on the call site `0x1001b348` plus an entry/exit bit-reader
snapshot (reading the hidden-`ecx` context struct's `{accum, cursor, bits_avail}` fields, same
layout as §5.3j Part 2) shows call #1's exit accum is `0x07364522` and call #2's entry accum is
`0x00736452` — exactly call #1's exit accum right-shifted 4 bits, the SAME cross-check §5.3x itself
cites. **Confirmed: call #1=idx0, call #2=idx3, call #3=idx4**, live, not by assumption.

**Bit-count reconciliation — two independent methods, both EXACT.**

1. *Cursor-arithmetic shortcut* (same method as §5.3w's own entry/exit check): `bits_consumed =
   8*(cursor_exit-cursor_entry) - bits_avail_exit + bits_avail_entry`. Call #2 (idx3): cursor
   `0x400006ac`→`0x400006b4` (8 bytes), bits_avail 24→5: `64-5+24 = 83`. Call #3 (idx4): cursor
   `0x400006b4`→`0x400006c8` (20 bytes), bits_avail 1→25: `160-25+1 = 136`. **Both exactly match
   §5.3x's published 83/136.**
2. *Full decision-by-decision reconciliation* (the real deliverable below): a address-classified
   event log of every Gate A test, every Gate B copy (both the resolve-now and the defer branch),
   every case 0/1/2/3 dispatch, and every Phase-2 `sel==0/1/2/3` arm actually executed, each
   correctly weighted by its own real bit cost (1 bit for a gate test; `level` bits for a mechanism-1
   magnitude+sign field; 1 extra bit for mechanism-2's "read one more bit" step; 0 bits for case 1
   and Phase-2's `sel==1`, which are pure tag arithmetic) — tallies to **83 for idx3 and 136 for
   idx4**, independently, with no fudge factor.

**Live output-struct coefficients — EXACT match, both blocks, all 20 and all 30 nonzero values.**
Reading the caller's own output coefficient struct (the stack arg cached at the call site, same
struct §5.3j Part 2's write-out section documents) immediately after each call returns:

- idx3: DC=199, nonzero AC (raster k) `1:-2, 2:3, 3:-2, 4:-1, 8:-3, 9:3, 10:-2, 11:1, 13:-1, 16:1,
  17:-2, 18:1, 20:-1, 25:-1, 26:1, 27:-1, 32:-1, 33:1, 34:-1, 43:1` — byte-for-byte identical to
  §5.3x's published list, 20/20.
- idx4: DC=212, nonzero AC (raster k) `1:-5, 2:-5, 3:3, 5:3, 6:-2, 8:-2, 9:-5, 10:-3, 11:1, 12:1,
  13:1, 14:1, 16:1, 17:2, 18:-1, 19:-1, 21:1, 23:1, 25:1, 26:2, 27:1, 28:-1, 32:-1, 33:-1, 34:-1,
  35:1, 36:1, 38:-1, 42:-1, 45:1` — byte-for-byte identical to §5.3x's published list, 30/30.

**Method — the hooking strategy actually used.** Per the task's own suggestion, rather than hand-
placing one hook per unrolled copy, a single `UC_HOOK_CODE` was installed across the ENTIRE
`0x10024eb0`-`0x100258a8` body (now independently re-measured at 2,552 bytes — see the correction
to §5.3j Part 2's preamble above), logging `{eax,ebx,ecx,edx,esi,edi}` at every executed instruction
for only the two target calls (toggled on/off by the call-site/return-site hooks). The resulting
raw instruction traces (1,293 instructions for idx3, 2,175 for idx4) were then classified offline
against a literal address map, built by a fresh static `capstone` disassembly of the same byte
range (`full_func_disasm.txt`, 741 real instructions, zero desync) — NOT against decompiler output.
The address map (every address below independently re-derived from this disassembly, not carried
over from §5.3j Part 2's prose):

- **Gate A** test: `0x10024ffa` (Phase 1), `0x100254f4` (Phase 2) — bit = the pre-shift copy of the
  accum register, tested via `test bl,1`.
- **Gate B's 4 unrolled copies** (Phase 1, reached from case 0/case 2): decision sites
  `0x100250b8`/`0x1002523d` (copy 1, two code paths for the same logical decision — refill vs.
  no-refill), `0x10025125`/`0x100252a0` (copy 2), `0x10025186`/`0x10025304` (copy 3),
  `0x100251ee`/`0x10025368` (copy 4) — all `test dl,1`. Resolve-now writes land at `0x100250ff`
  (copy 1, index register `ebx`), `0x1002515a`/`0x100251bb`/`0x10025229` (copies 2-4, index register
  `edi` — **copies 2-4 use a different index register than copy 1, confirmed by direct disassembly,
  not previously documented**). Defer writes land at `0x10025298`/`0x100252fc`/`0x10025360`/
  `0x100253be`.
- **Case dispatch** (Phase 1, jump table at `0x100258ac`): entries `0x10025018` (case 0),
  `0x10025038` (case 1), `0x10025087` (case 2), `0x100253cb` (case 3, resolve write `0x1002546c`).
  Phase 2's own table at `0x100258bc`: `0x10025512` (sel 0), `0x10025522` (sel 1), `0x1002556a`
  (sel 2), `0x10025716` (sel 3, resolve write `0x1002573a`).
- **Phase-2 `sel==2`'s own internal chain** — a previously-uncharacterized-by-address 4-slot
  "requeue-vs-resolve" structure distinct from Phase 1's Gate B, confirmed by disassembly: decision
  sites `0x10025590`/`0x100255f1`/`0x10025652`/`0x100256b3` (`test bl,1`); bit CLEAR → requeue a
  `cat*4+3`-tagged entry (writes at `0x100255aa`/`0x1002560b`/`0x1002566c`/`0x100256d2`); bit SET →
  read one more bit and resolve via mechanism 2 (writes at `0x100255cf`/`0x10025630`/`0x10025691`/
  `0x10025706`).

**Representative trace excerpt, idx3, first 19 bits (full 120-event log:
`scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/events_idx3.txt`):**

- **Bits 1-4** — Phase-0 level field, `A=2`. Since `A>1`, Phase 1's real BFS loop runs (unlike
  §5.3w's block (0,0), where `A=1` skipped it entirely) — this is the first live trace of Phase 1's
  own loop body with real bitstream content.
- **Bit 5** — Gate A, first worklist node → **1** → dispatch, `sel=0` (case 0): rewrites the
  node's own tag in place, falls into Gate B's chain.
- **Bits 6-8** — Gate B copy 1 → bit **6** (resolve-now, value 0) + a 2-bit (`level=2`)
  magnitude/sign field (bits 7-8) → **write category 4 = +3** (raster k=2, matching §5.3x's
  published `k=2:3` exactly).
- **Bits 9-11** — Gate B copy 2 → bit **9** (resolve-now) + 2-bit field (bits 10-11) → **write
  category 5 = -2** (k=3, matching `k=3:-2`).
- **Bits 12-14** — Gate B copy 3 → bit **12** (resolve-now) + 2-bit field (bits 13-14) → **write
  category 6 = -2** (k=10, matching `k=10:-2`).
- **Bit 15** — Gate B copy 4 → bit **1** → defer (synthetic category entry pushed to the back
  of the deferred list `[ebp-0x98]`, picked up again in Phase 2 below).
- **Bit 16** — Gate A, next worklist node → **0** → skip.
- **Bit 17** — Gate A → **1** → dispatch, `sel=0` (case 0 again), falls into Gate B.
- **Bit 18** — Gate B copy 1 → **1** → defer.
- **Bits 19-21** — Gate B copy 2 → bit **19** (resolve-now) + 2-bit field (bits 20-21) → **write
  category 25 = -2** (k=17, matching `k=17:-2`) — this is the FIRST live-captured instance of a
  Gate B copy resolving a category that was NOT the chain's own starting category (`cat`
  increments once per copy regardless of resolve-vs-defer, exactly as §5.3j Part 2's prose
  describes but §5.3w's trivial example never actually exercised).
- **Bit 22** — Gate B copy 3 → **1** → defer.
- **Bit 23** — Gate B copy 4 → **1** → defer.

**Correction, 2026-10-11 [Team B flagged a real tension in the bit-range labels above; checked
against the already-captured raw event log, `scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/
events_idx3.txt` — no new live capture needed, the original run already had the right numbers].**
The bit-range labels originally published here (`Bits 6-7`, `Bits 8-9`, `Bits 10-11`, `Bit 12`,
...) undercounted: each resolve-now Gate B copy spends 1 gate bit + a `level`-bit field (3 bits
total when `level=2`, as it is throughout this excerpt), not 2 — e.g. copy 1 genuinely spans bits
6-8 (cumulative bit count 8 after it finishes, confirmed directly in the raw event log's own
`bitnum` accumulator, which DOES correctly add `1 + width` per copy — the bug was only in this
prose's own compressed range labels, never in the underlying capture or in the final totals: the
full-trace bit count (83) and all 20 resolved values were independently reproduced correctly
throughout, unaffected). Labels above are now corrected to match the raw log exactly. **This
settles Team B's own flagged tension: there was no real 1-gate-bit+level-field budget violation in
the engine or the capture, only an arithmetic slip in how this specific prose excerpt labeled
cumulative bit ranges.**

**Case 2's own pre-Gate-B setup, independently confirmed live (resolves Team B's other flagged
tension — this was a judgment call by analogy to case 0 when first published, now directly
checked): idx4's own trace (`events_idx4.txt`, bit~33-34) shows `sel=2` dispatched at bit 33,
falling into Gate B's chain at bit 34 — via its own distinct Gate B entry address (`0x100250b8`),
confirmed DIFFERENT from case 0's own Gate B entry (`0x1002523d`, seen elsewhere in the same
trace at bit~41). So case 2 does reach the same Gate B copy-1..4 chain structure as case 0 (same
resolve/defer shape, same field-width behavior), but through its own separate entry point — not
literally the same code case 0 falls into, consistent with (and now live-confirmed rather than
merely inferred from) §5.3j Part 2's own claim that case 2 explicitly advances `front` before
entering Gate B while case 0 does not.**

...continuing through bit 26, Phase 1's loop dispatches `sel=3` (case 3) three times in a row
(categories 1, 2, 3; a 2-bit field each, resolving to raster k=1,8,9 = `-2,-3,3`, all matching
§5.3x exactly) before `front` catches `tail` at the current level and the loop transitions into
Phase 2 (bit 35 onward) to drain the deferred entries from Gate B copy 4's three defers above —
Phase 2 resolves them via its own `sel==3` arm (mechanism 2, magnitude always 1) and `sel==2` arm's
4-slot requeue/resolve chain, eventually bottoming out with every deferred entry either resolved or
re-deferred-and-reresolved, until the worklist is exhausted at bit 83. **Every single resolved
value in the full 120-event log maps, through §5.3w's own published category→k permutation table,
to the exact §5.3x ground-truth coefficient at that k** — spot-checked above for 7 of the 20
nonzero values, true for all 20 by direct inspection of the raw event log.

**idx4 — structurally richer (case 1 is actually exercised, unlike idx3), summarized (full 213-event
log: `scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/events_idx4.txt`).** Phase-0 level `A=3` (one
level deeper than idx3). The first loop pass dispatches case 0 (categories 4, resolved `-5`→k=2,
matching `k=2:-5`; copies 2-4 all defer), then two case-3 resolutions (category 1 →`-5`→k=1 matching
`k=1:-5`; category 3→`-5`→k=9 matching `k=9:-5`) each through a **3-bit** magnitude/sign field
(level=3, one level deeper than idx3's fields, confirmed live — the field width tracks the CURRENT
outer level, not a fixed constant, exactly as §5.3j Part 2 claims and as idx3 alone could not show
since every one of idx3's fields happened to be measured at `level=2`). Later in the same pass,
Phase 1 dispatches **case 1** for the first time in either trace (bit 32, sel=1, pure tag/children
arithmetic, zero bits, immediately followed by case 2 at bit 33) — the first live confirmation that
case 1's "4-way split, resolves itself, pushes 3 children, zero bits, never touches Gate B" claim
(§5.3j Part 2) holds for real bitstream content, not just by static disassembly. Every resolved
value in the 213-event log maps to its exact §5.3x ground-truth k — spot-checked for 7 of the 30
nonzero values above (categories 4,1,3,6,5,9,25 → k=2,1,9,10,3,5,17, values `-5,-5,-5,-3,3,3,2`, all
matching), true for all 30 by direct inspection.

**Scripts and raw output**, all in `scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/`:
`step1_callcount.py` (call-site/entry-exit verification), `step2_fulltrace.py` (the per-instruction
Unicorn capture, `raw_trace_call2.txt`/`raw_trace_call3.txt`, plus `out_struct_dump.txt` for the
coefficient cross-check), `step3_classify.py` (the address-classifier producing `events_idx3.txt`/
`events_idx4.txt`). `full_func_disasm.txt` (the fresh static disassembly the classifier's address
map was built from) lives alongside them.

- **Evidence:** live Unicorn capture against the unmodified `binkw32.dll` and unmodified
  `main_menu_bg.bik`, same harness family as §5.3v/§5.3w/§5.3x; call-site identity independently
  re-verified live (not assumed) by the same accum-shift cross-check §5.3x itself names; bit counts
  independently reproduced by two different methods (cursor arithmetic and full decision tally);
  every one of the 20 (idx3) and 30 (idx4) published nonzero coefficients reproduced byte-for-byte
  from the live output struct; the address map the classifier uses was built from a fresh
  independent static disassembly of the exact byte range, not copied from §5.3j Part 2's prose.
- **Confidence:** CONFIRMED for both blocks' bit counts, both blocks' full coefficient lists, the
  call-site/call-count identity, the Gate B copy-1-vs-copies-2–4 index-register difference (`ebx`
  vs. `edi`, newly found this pass), the Phase-2 `sel==2` 4-slot chain's address map, and case 1's
  real-content behavior (first live instance in either block's trace). The function's real size
  (2,552 bytes, correcting §5.3j Part 2's "14,840 bytes") is also CONFIRMED, by direct, exhaustive
  (zero-desync) disassembly of the whole cited range.
- **Unknowns:** the classifier's event log groups each Gate B/Phase-2-chain "copy" as one decision
  rather than hand-narrating every single one of the 83/136 bits in full §5.3w-style prose for
  every event (a deliberate scope choice under this pass's time budget, not a gap in the underlying
  data — the full event logs in `scratchpad/` give every single classified decision, address, bit
  value, and resolved value in execution order, for independent re-narration if a reader wants more
  prose detail than the representative excerpts above); whether this same classifier/address map
  generalizes cleanly to trees with `level>3` or to Phase 1 visiting MORE than 4 Gate B copies in a
  single chain (not exercised by either of these two blocks, whose chains both bottomed out at
  exactly 4).

---

### 5.3ad TEST VECTOR — `0x10019d10` full 528-byte output, frame 0/Y-plane/row 0's first call (2026-10-12, FINAL)

**Source:** real `binkw32.dll` under the Unicorn harness (isolated DLL emulation, no game process).
Read at the call's own return `0x10019ef6`. It must be read there and not at frame end, because the
scratch buffer is reused by later calls. Script:
`scratchpad/bink_entropy_prestage_algorithm_2026-10-08/probe_19d10_full_2026-10-12.py`, output saved to
`probe_19d10_full_out.txt` next to it. The first 8 bytes match the sequence already published in §5.3t.

**Inputs at entry:** `ctx=0x303f5034`, `A=6` (so the LUT has 2^6 = 64 entries), `lut_ptr=0x1003beb0`,
`out_base=0x303f9210`. Return `eax=0x64`.
- `translate16` (`ctx+0x10`): `[0, 15, 8, 1, 3, 12, 14, 7, 2, 4, 5, 6, 9, 10, 11, 13]`
- LUT bytes (all 64): `[16, 49, 16, 84, 16, 66, 16, 104, 16, 49, 16, 86, 16, 67, 16, 108, 16, 49, 16, 85, 16, 66, 16, 106, 16, 49, 16, 87, 16, 67, 16, 110, 16, 49, 16, 84, 16, 66, 16, 105, 16, 49, 16, 86, 16, 67, 16, 109, 16, 49, 16, 85, 16, 66, 16, 107, 16, 49, 16, 87, 16, 67, 16, 111]`

**Output, 528 bytes (offset: bytes):**
```
0000: 0f 01 00 00 00 00 80 ff f3 e3 e3 f1 f0 80 00 00 00 80 80 81 50 c3 b8 fa 00 00 00 00 00 3f 7f ff
0020: 10 00 80 e0 e0 e1 c1 c0 ff 00 00 00 00 bf cf e7 00 00 00 00 18 3c 7c fc 00 00 00 e0 f8 f8 fc fc
0040: f1 f1 f9 f1 c0 f0 30 04 e0 c0 00 40 c0 86 f3 f8 c0 c0 c0 c0 c0 e0 b0 30 70 30 30 70 e0 00 00 00
0060: 00 10 01 03 82 68 ee fc 00 00 00 00 03 c0 fe ff e0 c0 c0 80 83 c7 c7 e7 01 01 03 03 03 03 cf 7f
0080: 00 00 c0 18 8c 13 01 88 00 00 00 10 00 00 00 31 ff 07 03 00 00 00 00 00 fd ff 0f 9f 00 c0 80 00
00a0: 03 01 00 00 00 00 00 00 89 c9 03 80 90 32 62 42 f1 01 01 01 00 8e fe fe 01 81 7f 00 00 00 00 00
00c0: 00 00 00 00 04 00 c0 ff ff ff e3 c0 c0 c0 60 30 1f 0f 07 03 03 01 01 c1 f8 fc fe ff 03 00 00 00
00e0: 1f 0f 0f 07 07 07 03 1e 00 00 18 3f 3f 03 01 01 3f 1f 0c 00 00 00 00 00 30 60 40 00 00 00 3e 7f
0100: ff c3 01 00 00 00 01 00 c2 e0 e4 44 0c 18 30 f0 00 00 00 00 00 07 0f 0f f8 fc 9c 01 03 02 8e fe
0120: c0 00 00 80 20 3c db f3 be 9f 3f 10 07 00 00 00 3d 3c 38 38 30 00 00 00 f2 f8 fc f0 f8 90 80 00
0140: 00 00 10 18 1c 0f 0f 03 1f 9e 8e 8e c7 c3 61 30 fb f3 f1 f1 f0 f0 80 00 00 01 21 1f 0f 0f 07 00
0160: ff ff c0 00 00 00 00 c0 80 80 c0 c0 c0 8f 3f ff f8 00 00 c0 f0 f0 e0 e0 00 00 f8 fe ff ff 07 01
0180: 10 08 ff ff 7f 7f 00 00 13 0e 00 00 00 00 00 00 df 10 ff 11 f0 ec 84 00 cd 06 23 b0 e6 93 07 7f
01a0: 00 00 00 00 f0 f8 fc fc 86 8c 98 b8 b1 33 07 23 0f 07 07 07 06 1f c7 ec 38 38 30 10 00 00 00 01
01c0: 00 00 00 00 00 00 10 19 00 00 00 00 00 00 01 01 78 38 18 18 30 e0 00 00 00 00 40 5c 0e 0b 7f 3f
01e0: 60 40 00 00 00 00 00 00 20 64 64 c4 84 84 86 8e 18 3d 3f 3f 3e 00 00 00 f8 68 60 40 c0 80 00 00
0200: 01 04 fc fc fc f0 00 00 00 00 01 01 01 01 03 4f
```
Single-line hex (for direct diffing):
```
0f010000000080fff3e3e3f1f08000000080808150c3b8fa00000000003f7fff100080e0e0e1c1c0ff00000000bfcfe700000000183c7cfc000000e0f8f8fcfcf1f1f9f1c0f03004e0c00040c086f3f8c0c0c0c0c0e0b03070303070e0000000001001038268eefc0000000003c0feffe0c0c08083c7c7e7010103030303cf7f0000c0188c1301880000001000000031ff07030000000000fdff0f9f00c08000030100000000000089c9038090326242f1010101008efefe01817f0000000000000000000400c0ffffffe3c0c0c060301f0f0703030101c1f8fcfeff030000001f0f0f070707031e0000183f3f0301013f1f0c00000000003060400000003e7fffc3010000000100c2e0e4440c1830f00000000000070f0ff8fc9c0103028efec0000080203cdbf3be9f3f10070000003d3c383830000000f2f8fcf0f8908000000010181c0f0f031f9e8e8ec7c36130fbf3f1f1f0f080000001211f0f0f0700ffffc000000000c08080c0c0c08f3ffff80000c0f0f0e0e00000f8feffff07011008ffff7f7f0000130e000000000000df10ff11f0ec8400cd0623b0e693077f00000000f0f8fcfc868c98b8b13307230f070707061fc7ec3838301000000001000000000000101900000000000001017838181830e000000000405c0e0b7f3f6040000000000000206464c48484868e183d3f3f3e000000f8686040c08000000104fcfcfcf00000000001010101034f
```
**Use:** byte-diff a reimplementation of `0x10019d10` against this. The first differing offset
localizes the divergence. Nobody has diffed it against Team B's implementation yet.

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

       **Fifth pass, 2026-10-11 (§5.3k):** a live per-row DC-array capture reused §5.3d/§5.3f's own
       existing FFmpeg block-level ground truth (binary per-block-run split: exact / uniform-+128 /
       partial) to answer this item's own standing question — the error is a decode-side, per-run
       pattern, not a uniform bias applied after the DC array is built. This narrows future work to
       `0x1001a160`'s own DELTA-mode class-nibble/sign-accumulation arithmetic (structure fully
       mapped since §5.3f) and rules out the `0x1001b2ff`/`0x1001b311` consumer region as the fault
       site. Still OPEN: the exact instruction. A fresh single-row DC-array-vs-ground-truth column
       diff (distinct from reusing the existing block comparison) was attempted but not completed —
       the full-frame capture interleaves multiple planes/rows in one stream; a narrower single-row
       harness driver is needed to isolate one row cleanly, then hand-decode that row's raw bitstream
       batch bit-by-bit to find the exact faulting instruction.

       **Sixth pass, 2026-10-11 (§5.3l):** the single-row column diff is done (240/240 Y-plane row-0
       block-columns classified against FFmpeg), and it REFINES this item's own framing: runs are
       1-13 columns long, not the long multi-row stretches a coarser aggregate suggested — HIGH
       CONFIDENCE the fault is in the per-chunk class-nibble delta-accumulation loop itself, not
       confined to whole-batch ABSOLUTE-seed boundaries. The planned `param1+0x30` bulk-array-dump
       method to get a clean one-row ground-truth array is REFUTED (implausible magnitudes,
       thousands to tens-of-thousands, for a field whose real values are 25-1504) — a corrected
       recipe (capture via the `0x1001b311` zero-cost read site, scoped to one row, not a bulk dump
       of the backing array) is named for the next pass. Still OPEN: the exact instruction — this
       pass did not reach a live register-by-register trace of one bad chunk.

       **Seventh pass, 2026-10-11 (§5.3m):** full chunk-loop disassembly of `0x1001a160`'s commit
       mechanism, address-exact (CONFIRMED). §5.3l's bulk-dump refutation is now explained (the
       field is a transient scratch pointer re-based identically on every call) and fixed via a
       working live per-commit capture method (CONFIRMED). The column-correlation and final
       instruction-level +128 pin remain OPEN — blocked on an unresolved call-to-row/column mapping
       discrepancy (each call's commit count, 518-542, does not match the expected 240-column
       single row) and an unreproduced `0x1001b311` value range (hit count matched but values are
       ~1000x too large). Next pass should resolve the call-to-row mapping first, using the
       `0x1001a40a` commit-capture method, before re-attempting the divergence trace.

       **Eighth pass, 2026-10-11 (§5.3n):** found and fixed a real off-by-one in the seventh pass's
       own commit-hook placement (`0x1001a40a` hooks before `eax` is loaded — the fix is
       `0x1001a40d`). With the corrected hook: a call's commits are always one contiguous run into
       a private DLL-heap scratch array, never multiple interleaved sub-runs (falsifying the
       orchestrator's own "multiple planes combined per call" hypothesis in its literal form); and
       only 25 of the 1,084 total `0x1001a160` calls per frame ever reach the delta-accumulation
       loop at all — the function is a shared generic nibble-array decoder reused well beyond DC
       decode, which is the real reason commit counts never divided cleanly by 240. **OPEN,
       narrowed:** which of the 25 real calls maps to which Y-row-group (336-607 commits each, not
       a clean multiple of 240); and the still-unidentified scratch-to-plane copy step a future
       divergence trace must anchor on instead of the commit loop itself. Per the skip-and-defer
       policy, this sub-thread is parked here with the precise recipe above (re-baseline on the
       fixed hook; capture each of the 25 calls' own declared chunk-count args directly; watch for
       reads out of the scratch range to find the real copy-to-plane consumer) rather than ground
       on further without resolving it.
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

- **2026-10-12 — §5.3ab/§5.3aa CLOSED: `ctx+0x28`/`ctx+0x2c` formula fully derived, no bitstream
  read** (SPEC TEAM, orchestrator-routed). Live memory-write watchpoint on ctx+0x2c found the real
  writer at `0x1001a742`-`0x1001a757`: `bound = 8*dim + 512` where `dim` is the relevant axis count
  (pixel width for Y/chroma planes, MB-column count for `0x10019d10`-style tables), verified exact
  against all 3 known data points (1920→15872, 960→8192, 240→2432). `width(ctx+0x28)` =
  `bound.bit_length()` exactly in all 3 cases, so no separate computation is needed. Sent to Team B.
- **2026-10-12 — §5.3aa CORRECTED a fourth time: `ctx+0x28` width is per-plane, not a fixed 14**
  (SPEC TEAM, orchestrator-routed after Team B's production promotion crashed on mm_p_03 with "bad
  allocation"). Captured all 75 calls to `0x100195b0` in one frame0 decode: width/bound are
  constant within a plane (14/15872 for one plane, 13/8192 for the next) but change plane-to-plane
  in the same video; `preloop_bits_consumed` = width+1 always, no exceptions. A hardcoded width=14
  will desync on any plane whose bound differs from 15872, which is the likely root cause of the
  mm_p_03 crash, not a `cur_class` bounds-check gap. Both fields must be read live per call.
- **2026-10-12 — §5.3ab: `0x10019d10` confirmed to use the same `ctx+0x28`/`ctx+0x2c` bound-field
  mechanism as `0x100195b0`, live-captured for this call: `ctx+0x28`=12, `ctx+0x2c`=2432, decoded
  magnitude=528 (exact match to the already-published word count). Unlike `0x100195b0`, no extra
  1-bit sign-probe found in this function's own pre-loop sequence — pre-loop cost here is just the
  12-bit width, no +1. `ctx+0x20`/`ctx+0x24` for this function are confirmed genuine, dedicated
  tree fields (not aliased with any translate table) — this function has no class-carry at all
  (already established, §5.3ab), so the §5.3aa nib2-always-class-0 bug class does not apply here.
- **2026-10-12 — §5.3aa ALGORITHM FULLY CLOSED: Team B's full 650-byte decode now byte-for-byte
  matches the real fixture, bit cost exactly 3384** (SPEC TEAM + Team B, joint). Final fix: nib2
  is not cur_class-indexed at all, it always reads class 0's own slot unconditionally — found by
  Team B via symbol-1 cross-check (cur_class=1's own A_array value is 6, but nib2's code_len was
  still 4, the class-0 value). Combined with the already-fixed 14-bit pre-loop width, this closes
  the multi-session §5.3aa bug chain (translate table, nib2 tree location, pre-loop width, nib2
  class-indexing) entirely. Code block corrected in place. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-12 — §5.3aa CORRECTED a third time: pre-loop cost is 15 bits not 11, a 4-bit field
  previously missed** (SPEC TEAM, self, direct harness work, no agent). Live register-state
  capture at function entry vs. the per-symbol loop-top measured 15 bits, not 10(count)+1(sign-
  probe)=11; Team B independently confirmed by testing bit 2886 (not 2882) against their fixture
  — exact match. The missing 4-bit field itself not yet identified/characterized, flagged OPEN.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-12 — §5.3aa CORRECTED a second time: nib2's "global tree at ctx+0x20/+0x24" was also
  wrong — found by Team B's own per-symbol diff against their implementation, closed by a live
  capture** (SPEC TEAM, self, direct harness work, no agent). `ctx+0x20/+0x24` turned out to be
  more bytes of the SAME translate table already corrected once (§5.3aa's first correction), not
  separate scalar fields — the real per-class arrays live at `ctx+0x100`/`ctx+0x140`, confirmed
  live and matching §5.3g's own `A_array` exactly (class 0 = 4, matching Team B's own expected
  code length). Proposed class-indexed formula for nib2, mirroring nib1 — PLAUSIBLE, not
  independently re-verified against a full symbol trace this pass, flagged for Team B to confirm
  before production. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §5.3ac CORRECTED: the idx3 worked-example's own "Bits X-Y" range labels for Gate
  B's first node undercounted by not fully crediting the field width; case 2's pre-Gate-B setup
  independently confirmed live** (SPEC TEAM, self, direct read of the already-captured raw event
  log, no new capture needed, orchestrator-routed in response to Team B's own 2 flagged tensions).
  Tension (b): the original labels (`Bits 6-7`, `8-9`, `10-11`, `Bit 12`, ...) implied each Gate B
  copy spent only 2 bits total; the raw `events_idx3.txt` log's own `bitnum` accumulator (which was
  always correct — `+1` for the gate bit, `+width` for the field) shows each resolve-now copy
  actually spans 3 bits (1 gate + 2-bit field at `level=2`) — a labeling slip in this prose only,
  not in the capture or in the published totals (83 bits, 20 values, both unaffected and still
  correct). Relabeled the whole excerpt to match the raw log exactly. Tension (a): case 2's
  pre-Gate-B setup was a judgment call by analogy to case 0 when first published — now directly
  confirmed live in idx4's own trace (`sel=2` dispatched bit 33, falls into Gate B at bit 34 via
  its own distinct entry address `0x100250b8`, confirmed different from case 0's `0x1002523d`).
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §5.3ab CORRECTED: `0x10019d10`'s tree is NOT the same instance as descriptor
  A/B's tree, overclaim found and fixed on Team B's own prompt** (SPEC TEAM, self, direct static
  disassembly of the per-row orchestrator's call-site argument marshaling, no agent). The original
  text said `0x10019d10` uses "a SINGLE shared global tree — the exact same ctx+0x20/+0x24/+0x10
  fields 0x10019360's own descriptor... use" — true only of the field-offset CONVENTION, not the
  actual instance. Traced the real call-site args at `0x1001ab08`: `ctx=&[ebp-0x6c]`, a stack local
  distinct from descriptor A (`&[ebp-0x170]`) or B (`&[ebp-0x1a4]`), whose own tree fields
  (`ctx+0x20`/`+0x24`) are filled by their own dedicated `0x10018ca0` call (`0x1001aa24`-
  `0x1001aa2a`), not copied from or aliased with A/B. Flagged as a strong, structurally-motivated
  lead for Team B's own 12-bit (2993 vs 2981) overrun: pointing this call at descriptor A/B's tree
  instead of building its own would read wrong tree content. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3ac: full Gate A/Gate B/Phase-1-BFS bit trace for dispatch idx3/idx4 (83
  and 136 AC bits), both the Phase-1-real-BFS and Phase-2-defer-drain blocks Team B's own
  idx3/idx4 divergence (125/140 decoded vs. real ~24-25) was traced to** (SPEC TEAM, self, live
  Unicorn full-instruction capture of both target `0x10024eb0` calls, classified offline against a
  fresh independent `capstone` disassembly of the same 2,552-byte range). Both published bit
  counts (83/136) and both full published coefficient lists (20 and 30 nonzero values) reproduce
  EXACTLY, by two independent bit-count methods and direct output-struct read-back. New finding:
  Gate B's copy 1 resolve-write uses index register `ebx`, copies 2-4 use `edi` — not previously
  address-documented. Also corrects §5.3j Part 2's own preamble: `0x10024eb0`'s real extent is
  2,552 bytes, not 14,840 (a documentation error, caught by exhaustive zero-desync disassembly of
  the cited range). Scripts/raw logs in `scratchpad/bink_idx3_idx4_gate_trace_2026-10-11/`.
  Cleanroom self-check: no decompiler tokens, no Ghidra auto-names, all addresses literal capstone
  output.

- **2026-10-11 — new §5.3ab: `0x10019d10`'s two real consumers found (giant2/secondary=5's
  control-byte stream and primary case 8's nibble-selector stream, both already-documented
  engines in §5.3a), plus `0x10019f00`'s own distinct per-byte mode-selector mechanism** (SPEC
  TEAM, self, direct harness query — live memory-read watches over each function's own output
  buffer, fresh full-body static disassembly of both functions). Closes `0x10019d10`'s own
  "measured-total skip" trap the same way §5.3z/§5.3aa closed it for `0x100195b0`: no class-carry,
  single shared global tree, two nibble reads per byte. `0x10019f00` is a genuinely third
  mechanism (per-byte 1-bit mode selector: single-lookup-plus-trailing-sign-bit, or
  nibble-plus-bulk-fill-and-bail) with an honest negative on its own consumer (checked one real
  instance, zero reads found). Cleanroom self-check: strict regex 0 hits, C-cast-dereference check
  0 hits.

- **2026-10-11 — new §5.3aa: `0x100195b0`'s own per-symbol tree-decode body closed, instruction-
  exact, after Team B's 4 structural guesses all failed** (SPEC TEAM, self, fresh static
  `capstone` disassembly + a new live Unicorn per-symbol trace across the FULL real 650-symbol
  call before trusting anything, not a sample). Two nibble-trees per output byte (one per-class,
  one global, same single descriptor `0x10019360` itself uses); `cur_class[i+1] = nib1[i]` exactly,
  confirmed 0 mismatches across all 650 symbols; a previously-unnamed 16-class x 16-symbol
  translate table found at `ctx+0x00..0xFF` (CORRECTED same section, 2026-10-11 — originally
  published as `arg3+0x00..0xFF`, wrong; see the correction note below); a one-time 1-bit
  magnitude sign-probe before the loop starts, not observed firing. Reproduces §5.3s's own
  published first-20-byte sequence and 3384-bit total exactly. Closes §5.3z's own named Unknown
  (edited that line in place). Relayed to both live peers. Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §5.3aa CORRECTED (same day, caught by Team B's own implementation): the
  translate-table base address was wrong** (SPEC TEAM, self, direct harness query, no agent
  dispatched). Team B implemented §5.3aa literally and it did not reproduce the golden fixture
  (first byte 134 vs. 153, bit cost 3561 vs. 3384). Their own bounded diagnostic eliminated 5
  other hypotheses and reconfirmed the per-class bank identity, narrowing the bug to the translate
  step specifically. A fresh live capture confirms the real address is `ctx + cur_class*16 +
  raw_symbol1` — on `arg1`/`ctx` (the idle-check/output-descriptor struct), not `arg3`. Confirmed
  two independent ways (symbol 0 and the steady-state symbol both matched their already-known
  `nib1` output against a byte-snapshot taken at the exact read moment — an end-of-run snapshot
  attempt first gave garbage, since this scratch region is reused/overwritten later in the same
  call). Live byte table for this row spliced into §5.3aa in place. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §5.3n: the 56-bit gap CLOSED, call-by-call** (SPEC TEAM, agent
  `ac836bd1af667b8a2`, independently re-verified by fresh static `capstone` disassembly of
  `0x1001a530`-`0x1001ac10` in `binkw32.dll` directly before trusting — confirmed the exact call
  sequence and the absence of any `0x10019f00` call in this span). Orchestrator's "two
  `0x10019f00` calls" hypothesis disconfirmed as stated (those calls are earlier, in §5.3r's
  pre-stage gap); real sequence is a second `0x1001a160` call (10 bits, zero-count bail) then
  `0x10019af0` (14 bits, zero-magnitude bail), landing the real dispatch-read at bit 13587, not
  13619 — the remaining 32-bit residual against §5.3y's published 13619 is PLAUSIBLY a cross-
  script bit-counting convention mismatch, not a missed consumer. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §5.3n: `0x1001a160`'s first-call exact commit count PINNED (519 words),
  orchestrator-routed as the likely final fix for the remaining Bink gap** (SPEC TEAM, agent
  `a06b1037922a541b3`, independently re-verified by direct query before splicing — the raw
  capture's leading-count/write-count/exit-bits all matched exactly). Confirms the identical
  leading-count-pins-output-length idiom §5.3s/§5.3t already document for `0x100195b0`/
  `0x10019d10`; falls inside the already-established 336-607 range. Honest, not force-fit:
  measured exit bit (13563) is 56 bits short of the orchestrator's ~13619 estimate — flagged
  OPEN rather than papered over. Relayed to Team B directly. Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §5.3g's own standing OPEN item (calls #3-18's downstream consumer) RESOLVED,
  turning Team B's flat-fill fix from a hardcoded stopgap into a real general decoder**
  (SPEC TEAM, agent `a1972571f737cc99e`, independently re-verified by direct query before
  splicing — the live `arg3` pointer match and exact array contents matched the raw capture
  exactly). `0x100195b0`'s two LUT tables (`arg3+0x100`/`arg3+0x140`, left unspecified by §5.3s)
  are the exact structure calls #3-18 of the 23-call table-build pre-stage already write — same
  `build_table` algorithm, no new bit-reading mechanic, just a previously-untraced consumer.
  Confirmed rebuilt fresh per block-row from the bitstream (NOT static DLL data) — only the
  packed-byte LUT contents each pointer resolves to (`0x1003bd60` bank) are static/already-
  published. Gives Team B a complete implementer recipe: run the 16-call table-builder, write 3
  parallel banks into row-local scratch, maintain the persistent class index across
  `0x100195b0` calls within the row. Relayed to Team B directly. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3z: both literal-byte cursors confirmed correct at the source, AND
  identified as the real bug's root cause via a cross-reference Team B caught against §5.3s —
  flat-fill blocks consume `0x100195b0`'s own real tree-coded decode output sequentially, which
  a prior pass had modeled only as a bit-cost skip** (SPEC TEAM, agent `a30f1bd9c73cd27ef` plus
  self for the §5.3s cross-reference, independently re-verified — the raw capture dump matched
  the agent's addresses exactly, and the §5.3s buffer-address/byte-value match was checked
  directly against already-published text). Both DC-literal (idx0) and flat-fill (idx1) cursors
  point at their ground-truth byte, at offset 0, disconfirming the orchestrator's wrong-
  buffer/wrong-offset hypothesis. But `0x303f9bb0` — flat-fill's own buffer — turns out to be the
  EXACT SAME array §5.3s already documents as `0x100195b0`'s own 650-byte persistent tree-coded
  residual stream. Team B had modeled `0x100195b0` as "pay the bit cost, discard the content"
  since nothing consumed its real output when first characterized; flat-fill blocks now confirmed
  to need that real decoded content, consumed sequentially the same way `0x1001a160`'s DC-word
  buffer is consumed. Relayed directly to Team B with the fix direction (implement the real
  decode per §5.3s's algorithm, wire flat-fill to pull from it sequentially instead of a separate
  cursor abstraction). Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3y: bit-position/stream-source trace for Y row 0, testing the "wrong
  bitstream source" hypothesis for the still-wrong end-to-end decode — clean negative for this
  5-block span, plus a real correction to §5.3i's "descriptor L" sharing claim** (SPEC TEAM,
  agent `a40f050aea077ad93`, independently re-verified by direct query before splicing — the raw
  capture report's main table and literal-cursor addresses matched exactly). No anomaly found in
  the main-bitstream/AC-tree context sharing for dispatch idx 0-4 — fully monotonic, one
  coherent shared bit-reader context across pre-stage/nibble-read/AC-tree, confirmed live across
  3 AC-tree calls. Closes §5.3x's own flagged 4-bit gap (the per-block quantizer-nibble read,
  `0x1001b3b5`). Separately found that §5.3i's "descriptor L, one shared literal-byte cursor for
  DC words and flat-fill bytes" claim needs correcting: the BOUND-check variable is genuinely
  shared, but the actual literal-byte read cursors for secondary=2 (DC words) and secondary=3
  (fill bytes) are physically different addresses ~5,008 bytes apart — a real, concrete mismatch
  if any reimplementation modeled them as one shared array. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3x: compact worked examples for Y dispatch indices 1-4 and chroma block
  (0,0), validating the AC-tree fix beyond block (0,0) without another round-trip** (SPEC TEAM,
  agent `ad531bcaf38074493`, independently re-verified by direct query before splicing — the
  dispatch-survey's engine assignment per index and the idx3/idx4 bit-position/DC values both
  matched the report exactly). Confirms dispatch order is simple left-to-right raster for this
  part of the frame, but not every block uses the same engine (idx1/2 are secondary=3 flat-fill,
  not secondary=2). Gives full compact captures for idx3/idx4 (secondary=2, same engine as block
  (0,0)). Chroma: no evidence of a chroma-specific dequant matrix or scaling — one confirmed
  secondary=2 chroma occurrence uses the identical quantizer-table base/arithmetic and the
  literal same dequant/IDCT function as luma. ChromaB's own block (0,0) is confirmed
  NOT decodable this frame (reproduces the already-documented entropy-pre-stage stall), reported
  honestly rather than fabricated. Cleanroom self-check: strict regex 0 hits, C-cast-dereference
  check 0 hits.

- **2026-10-11 — new §5.3w + §5.3j Part 2 corrected in place: the AC-tree's real per-node bit
  mechanism (Gate A/Gate B) pinned, resolving Team B's top video-decode blocker** (SPEC TEAM,
  agent `ac53bf24007fb5c1f`, independently re-verified by direct query before splicing — case 0/1
  disassembly re-run and matched exactly; live trace's entry/exit bit-reader state matched
  §5.3v's own published numbers byte-for-byte). Team B had measured that case0/case1 child tags
  come out as a pure function of the 6 seed bytes, zero bitstream influence — structurally
  impossible for a real VLC, and traced to Part 2's own flagged-OPEN "read next node's bit before
  looping" line being imprecise. Real mechanism: TWO distinct 1-bit gates, not one — Gate A (every
  iteration, every nonzero-tag node, decides if it does anything at all; this is the actual
  entropy-coding bit a port must implement) and Gate B (reached only from case 0/2, up to 4
  unrolled copies, resolve-now-vs-defer). Case 1 consumes zero bits ever (pure arithmetic, returns
  straight to the loop bottom); case 0 also consumes zero bits in its own handler but falls
  straight into Gate B's chain in the same visit without advancing `front`. Also corrected: Phase
  1's BFS loop is conditionally SKIPPED (not "drained") for any tree with level≤1 — block (0,0)'s
  own real decode (level=1) runs entirely through Phase 2's structurally-identical mirror arms,
  closing Part 2's own "is Phase 2 reachable with real content" open item. New §5.3w gives the
  full 17-bit worked-example trace for block (0,0), ending at the correct 2 nonzero coefficients
  (both magnitude 1, both negative), matching §5.3v's ground truth exactly. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — category-index→raster-k remap RESOLVED, same day, closing §5.3w's one flagged
  gap (orchestrator-routed)** (SPEC TEAM, agent `a57fd1bde57d6f0e6`, independently re-verified by
  direct query before splicing — 3 of the ~32 `mov` pairs spot-checked exactly, plus the index
  arithmetic independently re-derived). Not a runtime table lookup — no indexed load exists
  anywhere in the function outside the two already-documented dispatch jump tables. It's a fixed,
  compiler-unrolled sequence of ~32 `mov` pairs in the write-out block (`0x10025777`-`0x1002589c`)
  at literal immediate offsets, confirmed to be a clean bijective category(1-63)→k(1-63)
  permutation (full table now in §5.3w), structurally a recursive quad-tree/Morton-style
  traversal consistent with the tree's own quad-split arithmetic. Because it's a fact about
  compiled code, not DLL data bytes, it stays in `spec-bink-codec.md` cited by address —
  `spec-bink-static-tables.md` needs no change. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3v: golden-trace worked example, AC-decode→dequant→IDCT for frame
  0/Y-plane/block (0,0) — localizes Team B's 141-vs-24 pixel bug to Team B's own code, not the
  documented algorithm** (SPEC TEAM, agent `ad46ad95517e9d385`, independently re-verified by
  direct query before splicing — raw/quant/dequant coefficient binaries recomputed exactly, both
  output binaries byte-counted exactly, matching the report in full). Orchestrator-routed
  follow-up after Team B fixed the DC boundary for this block (DC now exactly 118) but still saw
  pixel (0,0) = 141 against a reference of 24. Captured all 4 stages live against the unmodified
  DLL: bit position at AC-tree entry/exit (17 bits consumed, cross-confirming §5.3h's published
  figure independently); the raw coefficient list (only 3 nonzero: DC=118, k=8=-1, k=16=-1);
  dequantized values (6293/-102/-118, quantizer nibble 2); and all 64 IDCT outputs (rows 0-1=24,
  rows 2-7=25 uniformly, matching the vertical-only AC content exactly). Both an independent
  from-scratch Python re-derivation AND the real live DLL itself produce pixel (0,0)=24 from
  these exact captured numbers — proving AC decode, dequant, and the IDCT/rounding math (already
  bit-verified in §5.3j) are all correct for this block. The divergence is therefore entirely in
  Team B's own reimplementation; recommends they diff their own intermediates against these 4
  stages in order to localize it. Cleanroom self-check: strict regex 0 hits, C-cast-dereference
  check 0 hits.

- **2026-10-11 — new §5.3u: the §5.3m/§5.3q "contradiction" RESOLVED — both correct, each for a
  different value in the stream; §5.3q's own text corrected (its findings only applied to
  value[0], wrongly generalized to all 8)** (SPEC TEAM, self, direct Unicorn instrumentation,
  urgent routed item from orchestrator `purrsian-f5`/Team B report). Live per-symbol trace,
  first 8 values of `0x1001a160`'s first call: value[0] uses the fixed-bound path (§5.3p/q,
  unchanged); values[1-7] use §5.3m's chunk loop (one 4-bit class nibble per up-to-8-value
  chunk, here nibble=8, predictor-accumulated `new=prev±magnitude`). Every one of the 8 formulas
  reconciles exactly to the real observed value (118,199,212,51,25,118,119,26), zero
  discrepancies. Bit-position tracking required live register reads, not memory — the external
  bit-reader struct is proven stale mid-function (caught before trusting any number). Offered a
  hypothesis for Team B's own divergence: applying the wrong mechanism exactly at the
  value[0]/value[1] boundary. Cleanroom self-check: strict regex 0 hits, C-cast-dereference
  check 0 hits.

- **2026-10-11 — new §5.3t: `0x10019d10`'s own output grammar worked example, run to completion
  (frame 0/Y/row 0, first call)** (SPEC TEAM, self, direct disassembly + Unicorn capture, routed
  from orchestrator `purrsian-f5`). Nibble-packed bytes via an explicit decrement-to-zero loop —
  genuinely count-driven, not ambiguous. 528 bytes written, exactly matching the leading
  magnitude; bit cost at `ret` exactly 2981, an exact match to §5.3r's own independent
  measurement. Flagged a numeric coincidence (528 == this row's own real LEN value, §5.3g) for
  future investigation, not claimed as a mechanism. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-11 — §5.3s settled: `0x100195b0`'s loop-termination question resolved by running the
  call to completion (not static guessing)** (SPEC TEAM, self, direct Unicorn execution, routed
  from `purrsian-f5`). Exactly 650 bytes written, bit cost at `ret` exactly 3384 — an exact match
  to §5.3r's own independent measurement — confirming straightforward count-driven decode.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3s: `0x100195b0`'s own output grammar worked example (frame 0/Y/row 0,
  first call)** (SPEC TEAM, self, direct disassembly + Unicorn capture, routed from orchestrator
  `purrsian-f5` for Team B's own implementation). A signed byte stream, tree-coded, with a leading
  magnitude (650) gating a fast-fill shortcut for long runs; output buffer base is the same
  idle-check-struct `+0x30` convention `0x1001a160` uses. First function in this family found to
  carry real persistent adaptive state across calls (`arg3+0x180`). First 64 real output bytes
  captured, tight low-variance band (-105..-97). **Loop-termination settled same day** by running
  the call to completion: exactly 650 bytes written, bit cost at `ret` exactly 3384 — an exact
  match to §5.3r's own independent measurement — confirming this is straightforwardly
  count-driven. OPEN: what the output feeds downstream. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3r: the 1757→9256 bit gap (7,499 bits) attributed call by call, closing
  §5.3q's own named OPEN item** (SPEC TEAM, self, direct Unicorn instrumentation, routed from
  orchestrator `purrsian-f5` for Team B's own implementation diff). Found exactly 4 real calls in
  the gap by direct disassembly of `0x1001a530`; measured each one's bit cost live — sum exactly
  reconciles to the gap, zero bits unaccounted for. Caught and fixed a baseline bug (non-zero
  `bits_avail` at the measurement point) before trusting the numbers. All 3 named functions
  structurally characterized as siblings of `0x1001a160`'s own descriptor-fill family. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3q: `0x1001a160`'s first call (frame 0/Y/row 0) — absolute bit offset
  9256, no evolving predictor seed found (correction to the question's premise), first 8 decoded
  values, exact position after the full entropy pre-stage** (SPEC TEAM, self, direct Unicorn
  instrumentation, routed from orchestrator `purrsian-f5`, for Team B's own implementation diff).
  Reused `probe_prestage_bitpos.py` extended with `0x1001a160`-specific hooks. Caught and fixed
  the same function-entry-vs-prologue mistake `§5.3p` already flagged (hooking before `mov
  ebp,esp` reads the caller's frame) before trusting the result. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §5.3p: descriptor L's buffer SOURCE and cursor START identified for block
  (0,0), resolving §5.3o's own "suspicious 2-byte adjacency" OPEN item** (SPEC TEAM, self, direct
  Unicorn write-watch, routed from orchestrator `purrsian-f5`). A new write-watch hook (recording
  every memory write before the frame's first primary-dispatch) found the EXACT write that
  produced the already-documented DC-literal read (`0x303f8620`, value `0x0076`) — written by
  `0x1001a160` itself (`0x1001a307`), not by any separate pre-stage call. Descriptor L's buffer is
  filled live, in place, by this function's own per-row decode loop, from the main compressed
  bitstream via an inline context-adaptive sign/magnitude bit-predictor — the same function
  §5.3f/h/k/l/m/n have been investigating as a separate "delta-accumulation" mechanism, and the
  same function Team B implemented as a separate cursor (commit `b33c13e`). For block (0,0)
  specifically, the cursor starts at literal offset 0 of the buffer (the very first word
  `0x1001a160` writes this frame for the Y-plane context, read directly from the context struct's
  `+0x30` field before the function's first call). Resolves §5.3o's 2-byte-adjacency OPEN item:
  it's the SAME buffer, not two heap-adjacent mechanisms — §5.3n's own "commit run #2" sample
  simply started one word (2 bytes) late. Cleanroom self-check: strict regex 0 hits, C-cast-
  dereference check 0 hits.

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
- 2026-10-10 (SPEC TEAM, agent `ab7edf30d0fb20702`, Team-B-routed priority request via orchestrator):
  **new §5.3j Part 3 — resolves Team B's exact two interpretive judgment calls on the AC-tree sign
  recovery, which had caused a real desync on `main_menu_bg.bik` frame 0.** (1) The sign-mechanism
  selection rule is NOT case-2-vs-case-3 as Team B guessed: mechanism 1 handles BOTH Phase-1 case 2
  and case 3's immediate leaf resolutions; mechanism 2 is exclusive to Phase-2's `sel==2` arm's own
  runtime "magnitude-exactly-1" sub-branch. (2) Mechanism 1's extra-bits width is confirmed exactly
  `level` bits, and — correcting a genuine bug in this section's own prior prose (struck through in
  Part 2 above) — the sign test's real operand is a pre-mask RAW snapshot taken before the
  masking/OR runs, not the post-mask/OR'd value; testing the post-OR value, as the prior wording
  literally said, would have been tautological, exactly as Team B suspected. Independently
  re-disassembled both cited address ranges (`0x10025439`-`0x10025466`/`0x100250d2`-`0x100250f9` and
  `0x1002556a`-`0x10025637`) myself via `disasm_util.py` before splicing — matched the agent's
  report exactly, instruction for instruction. Left OPEN: Phase-2's `sel==1`/`sel==3` arms' own
  eventual sign-mechanism usage; a clean live bit-trace tying one specific real block's leaves to
  the rule end-to-end (a dynamic-trace attempt hit the same self-re-entrancy issue already
  documented for `0x100244c0`). Cleanroom self-check: strict regex 0 hits, C-cast-dereference check
  0 hits.
- 2026-10-10 (SPEC TEAM, agent `a0940c9e5ac3e0e2f`): **§5.3j Part 3's remaining OPEN item CLOSED —
  Phase-2's `sel==1`/`sel==3` arms traced to their own terminus.** `sel==1` (`0x10025522`-
  `0x1002555f`) does no sign recovery at all — a pure tag-arithmetic requeue, structurally identical
  to Phase 1's own `case 1`, that defers resolution entirely to three freshly `sel==2`-tagged
  children. `sel==3` (`0x10025716`-`0x1002575c`) resolves UNCONDITIONALLY via mechanism 2 — no
  gating bit, unlike `sel==2`'s own arm — confirming which dispatch arm one of Part 2's already-cited
  mechanism-2 addresses (`0x1002573a`) belongs to, not a new site. Updated selection rule: mechanism
  1 is Phase-1-only; mechanism 2 has two Phase-2 homes (`sel==2`'s gated sub-branch, `sel==3`
  unconditionally); `sel==1` reaches neither. Independently re-disassembled both address ranges
  myself via `disasm_util.py` before splicing (matched exactly). Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.
- 2026-10-11 (SPEC TEAM, agent `a9e935714b42ee777`, self-picked): new §5.3k, §8 item 5a updated.
  Live full-frame hooks on `0x1001a160` (1,084 calls) and the DC-literal read site `0x1001b311`
  (2,453 hits) confirm the per-row DC-array mechanism's scale and access pattern. Reusing
  §5.3d/§5.3f's own existing FFmpeg block-level ground truth (binary exact/+128/partial split, not
  a uniform array-wide offset) answers the DC+128 bug's standing diagnostic question: the fault is
  decode-side, inside `0x1001a160`'s own DELTA-mode arithmetic, not in the downstream consumer.
  Independently re-verified the pre-existing block-comparison figures this pass cites are actually
  present in the spec before trusting the agent's reuse of them. Still OPEN: the exact instruction;
  a fresh single-row column diff (attempted, not completed — flagged honestly by the agent rather
  than papered over). Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
- 2026-10-11 (SPEC TEAM, agent `a0f63c36485d467bd`): new §5.3l, §8 item 5a sixth-pass update.
  Completed the single-row column diff §5.3k left open: 240/240 Y-plane row-0 columns classified
  against FFmpeg, finding 1-13-column run lengths — much finer-grained than the whole-frame
  aggregate implied, narrowing the fault to the per-chunk class-nibble delta loop itself rather
  than whole-batch seed/reset boundaries. Also found and reported a clean negative: the planned
  `param1+0x30` bulk-array-dump method doesn't work (implausible magnitudes), with a corrected
  recipe named for the next pass. Independently re-verified the agent's cited pre-existing facts
  (the 1504 max-magnitude bound, the class-nibble/test-cl-1/neg-eax idiom) are actually present in
  the spec before trusting its framing. Did not reach the single-instruction pin — still OPEN,
  honestly reported as incomplete rather than forced. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.
- 2026-10-11 (SPEC TEAM, agent `a843d8239648ca893`): new §5.3m, §8 item 5a seventh-pass update.
  Full disassembly of `0x1001a160`'s per-chunk commit mechanism, address-exact — independently
  re-disassembled against the real `binkw32.dll` before splicing, matched exactly. Explains and
  fixes §5.3l's refuted bulk-dump method (the field is a transient scratch pointer re-based
  identically on every call; a live per-commit capture works instead). The task's own named
  deliverable (the exact faulting instruction/condition) was NOT reached — honestly reported,
  blocked on an unresolved call-to-row/column mapping discrepancy and an unreproduced read-site
  value range, both named precisely for the next pass. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.
- 2026-10-11 (SPEC TEAM, agent `aa676ccd8fe0ddb00`): new §5.3n, §8 item 5a eighth-pass update.
  Found and fixed a real off-by-one in the prior pass's own commit-hook placement (independently
  re-verified against the live disassembly before splicing — the fixed hook site matched exactly).
  With the fix: falsified the orchestrator's own "multiple planes per call" hypothesis (one call =
  one contiguous scratch run, never interleaved); found only 25 of 1,084 calls per frame actually
  reach the DC delta loop, explaining why counts never divided by 240. Per the skip-and-defer
  policy, parked this sub-thread with a precise recipe rather than grinding further without
  resolving the row mapping. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0
  hits.
- 2026-10-11 (SPEC TEAM, agent `ad6a67acc10a18f93`, urgent Team-B-routed priority): new §5.3o.
  Resolves an urgent question on Team B's reported 236-vs-24 pixel mismatch: block (0,0)'s real DC
  literal does NOT come from `0x1001a160`'s delta array at all — it's a zero-bit-cost 16-bit
  byte-cursor read from a separate mechanism (descriptor L), value 118, decoding to pixel 24 —
  already an EXACT match to FFmpeg, independently re-confirmed a third time against two
  pre-existing passes. Retracts the "zero-bit-cost read traces to `0x1001a160`" framing for this
  specific block. Reconciles Team B's implausible 16709 DC literal as most likely their own
  decoder reading from the wrong array (the `0x1001a160` scratch array, not descriptor L).
  Independently re-verified the exact address/mechanism this pass cites is already published,
  pre-existing CONFIRMED text before trusting the agent's cross-reference. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.
