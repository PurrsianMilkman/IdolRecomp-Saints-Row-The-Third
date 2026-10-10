# Scoping note — the Bink 1 container/codec implementation *inside* `binkw32.dll` (part (b))

**Date:** 2026-10-07 · **Type:** scoping note only — no spec content, nothing here is cleared for
implementation. This is part (b) of the two-part Bink task the user laid out (quoted in full in
`review/scoping-bink.md`'s own preamble): part (a), the game↔Bink call boundary, is DONE
(`spec-bink-boundary.md`). Part (b) — "the Bink 1 container plus video/audio codec as implemented
in that DLL" — was, until this pass, scoped only at the whole-section, size-only level
(`review/scoping-bink.md` §8, explicitly "not attempted"). This note opens `binkw32.dll` for real
(function-boundary-aware disassembly, not just PE-table/section-size reads) and produces the
dispatchable-unit breakdown the task asked for.

**Question (restated from the task brief).** (1) Verify/refine `scoping-bink.md` §8's size estimate
with a real function-boundary-aware pass, not the padding-heuristic proxy it used. (2) Identify
which of the DLL's 77 exports are the real decode-frame/decode-audio entry points into the codec,
versus the thin wrapper/state-management exports part (a) already covers. (3) Map the high-level
structure — does the code separate cleanly into container-parsing / video-decode / audio-decode
phases, and where. (4) Propose sized, dispatchable RE units, priority-ordered by what the shipped
game actually uses. (5) Flag anything surprising, in particular whether any of the per-format
sections are dead code for this shipped game.

**Method.** Read-only `pefile` + `capstone` (x86, 32-bit) static reads against `binkw32.dll` only
— a small purpose-built script (not `tools/harnesses/exe_va.py`, which is hardcoded to the main
exe's own base/sections; this DLL is a separate PE with its own image base, read fresh this pass).
No Ghidra was used or needed. Concretely, five passes: (1) a full PE dump of `binkw32.dll`'s own
sections, 77-export table (with real addresses — part (a)'s own spec file lists export *names*
only, never this DLL's own RVAs, so pulling them was a necessary first step, not a re-derivation of
anything already on record), and import table; (2) a linear `capstone` disassembly of every
executable section with `skipdata` on, so any run of undecodable bytes (embedded data/jump tables)
shows up as a flagged gap instead of being silently mis-decoded; (3) a direct-call/jmp **target**
census (every `E8`/`E9` whose destination lands inside a mapped executable section) used as the
function-start signal, instead of the padding-run heuristic the prior pass used — per this
project's own `WALLS.md` ("only a named structural anchor buys selectivity" / the int3-padding-proxy
risk flagged specifically for this exact estimate); (4) a call-graph BFS from every export, and
specifically from the 23 exports the game actually imports (`spec-bink-boundary.md` §1.2), to find
real reachability into specific code regions; (5) a handful of small, targeted follow-ups (a raw
byte search for a function-pointer table, full dumps of the small sections) once the first four
passes turned up concrete leads. **No leaked Volition/RAD source of any kind was read, consulted,
or used. FFmpeg's Bink decoder was not read, consulted, or used** — nothing here needed it; it
remains reserved, per the user's standing instruction, for a future output-only comparison once
this project has its own independently-derived spec. **Clean-room note up front:** everything below
is read directly from `binkw32.dll`'s own disassembly and PE tables; mnemonics shown are literal
`capstone` output, not decompiler pseudocode (no decompiler was used).

**A scope-defining fact, stated once so it is never misread later: `binkw32.dll` is its own PE,
loaded at preferred base `0x10000000`.** Every address in this note is in **that** address space.
This project's other specs and scoping notes almost all cite addresses in the main
`SaintsRowTheThird.exe`'s own `0x00xxxxxx` space — **the two are disjoint and not comparable
without translation.** Do not mix a `0x10xxxxxx` address from this note with a `0x00xxxxxx` address
from `spec-bink-boundary.md`/`scoping-bink.md` and assume any relationship.

**Confidence labels** are the project's usual ones: **CONFIRMED** (read/measured directly this
pass), **HIGH** (inferred from strong structural evidence, not independently verified), **HYPOTHESIS**
(plausible, flagged honestly as unchecked).

---

## 0. Verdict up front

1. **The prior pass's function-count estimate for the dominant `BINK` section was a real
   undercount, for exactly the reason `WALLS.md` already warns about.** A call/jmp-target-based
   census (a structural anchor, not a byte-pattern proxy) finds **633 real candidate functions**
   in the `BINK` section, against the prior pass's **~250 by an int3-padding-run proxy** — a
   **2.5× correction**. Checked directly: only **~33%** of this section's real function starts are
   preceded by `int3` padding at all; the other **~66%** are packed with no padding byte
   whatsoever. An int3-only heuristic was therefore structurally guaranteed to miss roughly
   two-thirds of this section's real functions — not a fluke, a property of this toolchain's own
   packing convention (CONFIRMED by direct sampling, §2).
2. **Instruction and byte totals, by contrast, hold up well** — they don't depend on boundary
   detection. `BINK` section: **36,049 instructions** this pass vs. the prior rough **~36,000**;
   whole-DLL code: **139,264 bytes** this pass vs. the prior **~139 KB**; whole-DLL instructions:
   **~44,531** this pass vs. the prior **~43,000**. **Whole-DLL function count corrects from the
   prior ~310 to 766** (§2.3) — driven almost entirely by the same undercount, plus a previously
   un-surveyed `.text` (73 functions, not the handful implied by its 6 KB size alone).
3. **A structural correction to the prior pass's working model, not just its numbers: the DLL's
   entire 77-export flat-C API — both the thin state wrappers *and* the deep codec implementation —
   physically lives inside the one `BINK` section.** 76 of 77 exports resolve to a `BINK`-section
   address; only `_BinkLogoAddress@0` sits in `.text`. There is no section-level split between
   "wrapper layer" and "codec section" — the prior pass's framing (`.text` as a small wrapper layer,
   `BINK` as the codec) does not hold once the exports' own addresses are actually read (§3).
4. **The real deep-codec entry points are identified, by call-graph evidence, not by name alone:
   `_BinkDoFrame@4` and the (game-unused) `_BinkDoFramePlane@8`** are the only exports whose closure
   fans out substantially into the codec's two dense internal clusters (§4–§5). Every other export
   checked — `Open`, `Close`, `Pause`, `Goto`, `Wait`, `ShouldSkip`, `NextFrame`, the async-pump
   family, every `Set*`/`Get*` — has a shallow closure confined to state/bookkeeping/container
   code, consistent with part (a)'s own characterisation of them as the boundary contract.
5. **The container-parsing / video-decode / audio-decode phase separation the task asked about is
   real, sharply bounded, and found by direct disassembly — not assumed from Bink 1's known public
   design.** Two large, address-contiguous, densely self-contained blocks sit inside `BINK`:
   - **Block A, `0x10017470`–`0x1001ce50` (23,008 bytes, 170 functions, 7,490 instructions): the
     video decode core.** Reached from `BinkDoFrame`/`BinkDoFramePlane` (28–31 of their closure
     functions land here) and, more lightly, from `BinkGoto`'s keyframe-seek path (5 functions) —
     consistent with video-side frame-index structures sitting alongside the decode code. 227
     call/jmp edges stay inside the block; only 18 incoming edges reach it from outside, across 17
     real caller functions (§5.1).
   - **Block B, `0x1001e1d0`–`0x100260d4` (32,516 bytes, 104 functions, 8,854 instructions): the
     audio decode core.** Every externally-reached entry point inside it that this pass actually
     disassembled uses floating-point/XMM instructions (`movss`, `cvtsi2ss`, `divss`, `fmul`,
     `fld`) in transform-setup-shaped arithmetic (compute half the block size, convert to float,
     take a reciprocal — the shape of a transform/window-table setup, not pixel math), plus a
     struct-field-unpacking function reading six `word`-sized fields at fixed offsets (`+0x70`,
     `+0x20`, `+0x50`, `+0x60`, `+0x40` — plausibly channel count/block size/sample-rate-class
     fields of an audio-stream descriptor). 207 internal edges, 33 incoming from outside across 10
     real caller functions (§5.2).
   - Everything else in `BINK` (359 functions, ~19,705 instructions, spread through the rest of the
     section) is container parsing, the 76 exports' own thin-to-moderate bodies, inter-block
     dispatch glue, and the several entirely-unused API families (`BinkBuffer*`, multi-track
     `OpenTrack`/`GetTrackData`, the `DX8`/`DX9`/`DD` surface-type helpers) part (a) already found
     absent from the game's import table.
6. **`scoping-bink.md` §8's `RADCODE` hypothesis ("RAD's shared bit-reader/CRC utility") is
   REFUTED by direct string and disassembly evidence — it is RAD's shared Win32-feature-detection
   and threading shim, not codec logic.** Its own small constant strings (read directly,
   `RADCONST`, §5.3) are `IsDebuggerPresent`, `KERNEL32`, `RAD_NO_SET_THREAD_NAMES`,
   `SetThreadIdealProcessor`, `TryEnterCriticalSection`,
   `InitializeCriticalSectionAndSpinCount`, `SwitchToThread` — this is RAD's own
   dynamically-resolve-newer-kernel32-APIs-and-name-the-current-thread shim (the `0x406D1388`
   magic exception code used for debugger thread-naming appears directly in its code), gated by an
   opt-out environment variable. **Correct this HYPOTHESIS; do not re-cite the old one as fact.**
7. **A concrete, evidence-based (not merely plausible) case that the five small per-bit-depth/
   pixel-format sections (`BINKY12`/`BINKY16`/`BINKP8`/`BINK16`/`BINK32`, ~15 KB / 43 functions
   combined) are dead code for this specific shipped game — HIGH confidence, not CONFIRMED (§6).**
   Zero direct call or jump edges anywhere in the whole binary target any of their 43 functions.
   Even `_BinkCopyToBuffer@28`/`_BinkCopyToBufferRect@44`/`_BinkBufferBlit@12`/`_BinkBufferOpen@16`
   — the only public APIs whose documented purpose is exactly "convert/copy decoded video into an
   arbitrary pixel format" — show no *direct*-call path into them either, in an 8-hop closure.
   `.text` does contain five small, confirmed CPU-feature-selection stubs (one per family) that
   pick among 1–3 candidate implementation addresses based on a CPUID-derived flag and register
   the winner into a small fixed global record before calling a shared finalize helper (`capstone`-
   read directly, §6) — so some registration code for this family plausibly does run once, at
   module init. **But this pass found no code anywhere that reads a registered pointer back out and
   calls through it** — the real invocation, if any ever happens, is through an indirect call this
   static method cannot trace. Combined with part (a)'s own CONFIRMED finding that the game's
   import table altogether lacks every API that would exercise this family, the honest conclusion
   is: **very likely unreachable in practice for this shipped game, not proven unreachable.**

**Rough size, whole-DLL, this pass (CONFIRMED function-boundary-aware figures):**

| Section | Raw bytes | Functions | Instructions |
|---|---|---|---|
| `.text` | 6,144 | 73 | 2,160 |
| `BINKY12` | 512 | 3 | 227 |
| `BINKY16` | 2,560 | 14 | 1,026 |
| `BINKP8` | 2,560 | 4 | 874 |
| `BINK16` | 4,608 | 12 | 1,532 |
| `BINK32` | 5,120 | 10 | 1,692 |
| `BINK` | 115,200 | 633 | 36,049 |
| `RADCODE` | 2,560 | 17 | 971 |
| **Total (exec sections)** | **139,264** | **766** | **44,531** |

For scale: this is still in the same rough neighbourhood the prior pass placed it in relative to
this project's other closures (roughly double the vehicle-physics closure's ~21k instructions,
about a quarter of Wwise's whole statically-linked block) — the size-class verdict holds; what
changes is the function-count granularity and, now, a real internal structure to dispatch against.

---

## 1. Where the 77 exports actually live (correcting the structural picture)

`scoping-bink.md` §8 inferred a two-tier structure from section *names and sizes* alone (a small
`.text` "wrapper" layer, `BINK` as "the codec"), without reading where the exports themselves
resolve to. Reading the export directory's own addresses (CONFIRMED) shows that picture does not
hold: **76 of the 77 exports resolve to an address inside the `BINK` section itself; only
`_BinkLogoAddress@0` (trivially, since it just returns a static logo-image pointer) sits in
`.text`.** `.text` (6,144 bytes, 73 functions, 2,160 instructions) holds the DLL entry point
(`0x10002240`), that one export, and — as §6 and §9 show — a handful of small CPU-feature-dispatch
selector stubs plus a large population of tiny generic/CRT-style helper functions that `BINK`'s own
code calls into **172 separate times** (the single largest cross-section call-edge count found in
the whole binary, §9). So `.text` is better described as "the compiler/CRT-provided glue + a few
init-time selectors," not as a wrapper layer over the exported API — the exported API itself, thin
or deep, is uniformly inside `BINK`.

`BINKY12`/`BINKY16`/`BINKP8`/`BINK16`/`BINK32` and `RADCODE` export **nothing** — they are purely
internal, confirmed by the export table directly.

---

## 2. Refining the size estimate — real function-boundary-aware pass

### 2.1 The padding-heuristic failure, checked directly (not assumed)

For every one of `BINK`'s 633 candidate function starts (call/jmp-target based), this pass sampled
the 16 bytes immediately before it: **209 are preceded by one or more `int3` bytes, 5 by `nop`, and
418 (66%) have neither — the previous instruction's bytes run directly into the next function with
no padding at all.** `209/633 ≈ 33%` — almost exactly the prior pass's own reported "~250" figure,
which makes the mechanism of the prior undercount legible: an int3-padding census can only ever see
the ~33% of this section's functions that happen to be padded that way, and this toolchain (RAD's
own build, not the main exe's MSVC) only pads about a third of the time. `WALLS.md`'s own standing
caution about this exact heuristic, raised specifically for this estimate in the task brief, is
confirmed as the real cause, not a hypothetical risk.

The other exec sections show the same mixed convention, to varying degrees (sampled the same way):
`.text` 18 int3 / 1 nop / 54 none; `RADCODE` 9 / 1 / 6; `BINKY16` 0 / 2 / 11; `BINK16` 0 / 1 / 10;
`BINK32` 0 / 0 / 9; `BINKP8` 0 / 0 / 3; `BINKY12` 1 / 0 / 1. The smaller per-format sections lean
even further toward "no padding at all" — consistent with them being a tight, hand-tuned MMX kernel
library rather than ordinary compiler output.

### 2.2 Sanity check: did the call-target method itself miss or invent anything large?

Two cross-checks, both pass: (1) a full linear disassembly of every exec section with `skipdata`
enabled found **zero runs of 8+ undecodable bytes anywhere** — no embedded jump/data tables large
enough to have thrown off either this pass's or the prior pass's linear instruction count; (2) the
sum of per-function instruction counts, re-segmented at the 633 real starts, is **36,049** against
the unsegmented linear total of **36,048** — a 1-instruction rounding difference, meaning the
re-segmentation neither double-counts nor drops instructions. The function-count correction is real
and large; the instruction/byte totals were never actually wrong.

### 2.3 Whole-DLL totals, before/after

| | Prior pass (`scoping-bink.md` §8) | This pass |
|---|---|---|
| Total code size | ~139 KB | 139,264 bytes (exact) |
| Total instructions | ~43,000 (rough) | ~44,531 |
| Total functions | ~310 (rough lower bound, padding-proxy) | **766** (call/jmp-target census) |
| `BINK` section alone | ~36,000 instr / ~250 funcs | 36,049 instr / **633 funcs** |

---

## 3. Real entry points into the codec vs. thin wrapper/state exports

Using call-graph closures (BFS over direct `call`/`jmp` edges, function-boundary-segmented) rooted
at each export, two clearly different populations emerge:

**Shallow, state/bookkeeping-only closures** (≤20 functions at a generous 6–10-hop depth), matching
part (a)'s own characterisation of these as the boundary contract, not codec work:
`_BinkControlBackgroundIO@8` (1), `_BinkSetIOSize@4` (1), `_BinkGetKeyFrame@12` (1),
`_BinkOpenDirectSound@4` (1), `_BinkRegisterFrameBuffers@8` (1), `_BinkGetFrameBuffersInfo@8` (1),
`_BinkSetWillLoop@8` (1), `_BinkSetSoundTrack@8` (2), `_BinkSetVolume@12` (2),
`_BinkSetSpeakerVolumes@20` (8), `_BinkShouldSkip@4` (15), `_BinkGetRealtime@12` (16),
`_BinkDoFrameAsyncWait@8` (17), `_BinkGetSummary@8` (20), `_BinkSetSoundSystem@8` (19).

**Deep closures reaching the dense internal clusters identified in §5:**
`_BinkDoFrame@4` (closure 120 functions at 10 hops; 28 land in the video core, 29 in the audio
core) and the game-unused `_BinkDoFramePlane@8` (closure 129; 31 video-core, 36 audio-core) are the
**only** exports whose closures substantially enter both of §5's blocks. `_BinkGoto@12` (closure 83)
touches the video core lightly (5 functions — keyframe-table lookup) but not the audio core at all.
`_BinkDoFrameAsync@12` (closure 43) touches 3 small thread-signalling functions at the video core's
address range but does not itself decode (it kicks off the background thread; §5.1). `_BinkOpen@8`
(closure 44–66 depending on hop depth) and `_BinkClose@4` (closure 40–58) are almost entirely
outside both blocks — container-header parsing and teardown are, as the task's §3 question hoped,
genuinely separate from the two decode cores, not interleaved with them.

**Answer to the task's item 2, plainly:** the real decode-frame entry point is `_BinkDoFrame@4`
(and its sibling, not used by this game, `_BinkDoFramePlane@8`); there is no separate
"decode-audio" export — audio decode is reached from the same `BinkDoFrame` call, fanning out into
a structurally distinct region (§5.2) rather than through its own public entry point. Every other
export checked is a thin wrapper or state accessor over the handle/track/buffer bookkeeping that
part (a) already specified at the game-boundary level.

---

## 4. High-level structure: the phase boundaries, by disassembly

### 4.1 Overview

```
BINK section, 0x1000a000-0x10026074 (115,200 bytes; 633 functions / 36,049 instr total)
  0x1000a000-0x10017470  54,384 B / 335 funcs / ~18.1k instr   container/API/dispatch region
       - the 76 exports' own bodies (thin to moderate)
       - BinkOpen's header/track parsing, handle-table-equivalent bookkeeping
       - BinkBuffer* family, DX8/DX9/DD surface-type helpers, OpenTrack/GetTrackData
         (confirmed unused by this game, part (a); physically interleaved here, not segregated,
         and this pass did not separately measure this sub-family's own share of the 335/~18.1k)
       - small async-thread-signalling primitive cluster right before Block A
  0x10017470-0x1001ce50  BLOCK A: VIDEO DECODE CORE   23,008 B / 170 funcs / 7,490 instr
  0x1001ce50-0x1001e1d0  4,992 B / 24 funcs / ~1.5k instr   CPU-feature dispatch finalize helpers +
                                   ControlPlatformFeatures' own body (shared infra for Sec. 6's
                                   per-format family; not itself part of either decode core)
  0x1001e1d0-0x100260d4  BLOCK B: AUDIO DECODE CORE   32,516 B / 104 funcs / 8,854 instr
```

(the pre-Block-A and gap-A/B byte/function counts are from a separate, narrower re-run of the same
candidate-start method restricted to each sub-range; their instruction sum comes out ~150 lower
than `36,049 − 7,490 − 8,854 = 19,705` due to a boundary-rounding artifact in that narrower rerun,
not a real discrepancy in Block A/B's own figures, which matched exactly across both runs — treat
the two non-core regions' instruction counts as ±150 approximate, and the 359-function total
(335+24, which does exactly equal `633 − 170 − 104`) as solid)

### 4.2 Block A — video decode core (`0x10017470`–`0x1001ce50`)

Reached directly: from `_BinkDoFramePlane@8`'s own body (`0x10011280`, calling `0x1001cb00` at the
very tail of the block) and from an internal `DoFramePlane` helper (`0x1001161a`, also calling
`0x1001cb00`) — the per-plane decode's own real work plausibly sits at or near `0x1001cb00`, right
at this block's end, though this pass did not open that specific function's body (left to a future
unit, §7). Other confirmed doorways in: buffer/geometry size-and-alignment helpers
(`0x10018600`/`0x10018800`/`0x10018860`/`0x10018ba0`, called from `RegisterFrameBuffers`'s own
internal helpers, `CopyToBufferRect`, `OpenTrack`, `GetTrackData` — plane stride/size computation,
not pixel decode itself), and a small, genuinely separate cluster of ~7 tiny thread-flag
set/test/clear primitives (`0x100176e0`–`0x10017ba0`, each 8–18 instructions, operating on two
global bitmask dwords `0x1003faec`/`0x1003fcd0`) called from the `BinkStartAsyncThread`/
`RequestStopAsyncThread`/`WaitStopAsyncThread`/`DoFrameAsync`/`DoFrameAsyncWait` family — this small
sub-cluster is async-thread-pool signalling infrastructure, not video math, and happens to sit at
the very front of the block (immediately after `_BinkOpenXAudio2@4`'s own export address, which is
why a coarse "nearest preceding export" attribution would mislabel it — a real pitfall this pass
hit and corrected by re-attributing to the *actual* candidate-function-start containing each call
site, not the nearest export name).

Only **18** call/jmp edges enter the block from genuinely outside it (17 distinct real caller
functions); **227** stay inside it. That ratio (dense internal fan-out, few external doorways) is
itself structural evidence this is one cohesive subsystem, not a grab-bag of unrelated code that
happens to sit in this address range.

### 4.3 Block B — audio decode core (`0x1001e1d0`–`0x100260d4`)

Reached from internal helper functions physically clustered in the `0x10018600`–`0x1001c400` range
— i.e. immediately *after* `_BinkOpenXAudio2@4`'s own export address, the same "address-proximity,
not logical ownership" pitfall as above (these are **not** part of `OpenXAudio2`'s own body; they
are unexported internal functions the linker happened to place nearby). Every entry point into
Block B that this pass actually disassembled shows a floating-point/XMM instruction signature:
`0x1001e3e0`/`0x1001e550` compute half of an input count, convert to float (`cvtsi2ss`), and take a
reciprocal (`divss`) against a loaded constant (`movss xmm1, [0x10031318]`) — the shape of a
transform/window-table parameter setup (e.g. computing `1/(N/2)` for a cosine-table or windowing
step), not pixel arithmetic. `0x100244c0` reads six `word`-sized fields at fixed struct offsets
(`+0x70`, `+0x20`, `+0x50`, `+0x60`, `+0x40`, and one more) into locals — HIGH: unpacking an
audio-stream descriptor's fields (channel count, block size, sample-rate class, etc. — exact
semantics not asserted, this is a structural read only). One doorway function (`0x1001e2b0`, called
from the `BinkDX9SurfaceType`/`OpenTrack` address buckets) is a generic 32-byte-aligned
allocate-and-round-up helper, not obviously audio-specific — flagged as a shared utility that
happens to sit at this block's front edge, not evidence against the audio-core reading overall.

**33** edges enter from outside (10 distinct real callers); **207** stay inside. Same
dense-internal/few-external-doorways signature as Block A.

### 4.4 Container parsing is genuinely separate (confirms the task's §3 question)

`_BinkOpen@8`'s own closure barely touches either block (2 functions in each, at a 10-hop depth) —
the bulk of its ~44–66-function closure is header/track/options parsing and handle setup, physically
and structurally distinct from both decode cores. `_BinkClose@4`'s closure touches neither block at
all. This is the clean phase separation the task asked whether to expect from Bink 1's known public
design (container format, separate video codec, separate audio codec) — found here by direct
call-graph evidence, not assumed from that public description.

---

## 5. `RADCODE` — corrected role (REFUTES the prior HYPOTHESIS)

`scoping-bink.md` §8 hypothesized `RADCODE` (2,560 bytes) as "RAD's shared bit-reader/CRC utility
code," from the section name alone, not opened. Opened this pass (17 functions, 971 instructions,
full dump): it is **RAD's shared Win32-feature-detection and threading shim**, unrelated to codec
bitstream work. Direct evidence:
- its own tiny constant-string section, `RADCONST` (159 bytes, read directly in full): `IsDebuggerPresent`,
  `KERNEL32`, `RAD_NO_SET_THREAD_NAMES`, `SetThreadIdealProcessor`, `TryEnterCriticalSection`,
  `InitializeCriticalSectionAndSpinCount`, `SwitchToThread` — every one of these is a
  dynamically-resolved newer-than-baseline Kernel32 API name or an environment-variable gate, not a
  codec term;
- one `RADCODE` function (`0x10027000`) does nothing but compare a dword against `0x406D1388` and
  return whether it matches — the well-known magic exception code used by the
  `RaiseException`-based debugger thread-naming protocol;
- the init-style functions in `RADCODE` follow the textbook "cache a resolved function pointer"
  idiom: test a cached-result dword, and if unset, call `GetModuleHandleA("KERNEL32")` then
  `GetProcAddress` for one of the strings above, cache the result, then call through it — exactly
  the pattern needed to use `TryEnterCriticalSection`/`InitializeCriticalSectionAndSpinCount` (both
  added to Kernel32 after the Windows baseline some RAD code apparently still targets) without a
  hard import-table dependency.
`RADDATA` (52 bytes, read directly) is mostly zero with one `0xffffffff` marker — consistent with a
small set of cached-resolution/TLS-slot flags, not a CRC table.

`RADCODE` is called into from `BINK` 21 times (CONFIRMED census), from scattered points across the
container/export region (`SetSoundSystem`, `SetSoundTrack`, `RegisterFrameBuffers`,
`GetPlatformInfo`, `OpenXAudio2`'s own nearby address range) — consistent with "generic thread/CPU
utility used wherever the code needs a critical section or a thread property," not with being
specific to either decode core. **Correct this hypothesis going forward; do not re-cite "shared
bit-reader/CRC utility" as fact.**

---

## 6. The per-bit-depth/pixel-format family — likely dead code for this shipped game

`BINKY12` (3 funcs/227 instr), `BINKY16` (14/1,026), `BINKP8` (4/874), `BINK16` (12/1,532), `BINK32`
(10/1,692) — **43 functions, 5,351 instructions, 15,360 bytes combined**, tighter than the prior
pass's "under 20 KB" estimate. Opened directly this pass (full dumps of every candidate function):
every one of these sections' real bodies uses **MMX instructions** (`movq`, `punpcklbw`,
`punpckhbw`, `pxor`, `psllw`, `pmulhw`, `pand`, `packuswb`) over small fixed byte-shuffle patterns —
textbook hand-written SIMD pixel-format/bit-depth conversion kernels, confirming the naming
hypothesis by actual code, not just the section names.

**Reachability, checked both ways:**
- **From the game's 23 actually-imported exports** (`spec-bink-boundary.md` §1.2): a call-graph BFS
  (6-hop depth, generous given most of these closures are far smaller) finds **zero** reaching any
  of the five sections.
- **From the exports whose documented public purpose is exactly to use this family**
  (`_BinkCopyToBuffer@28`, `_BinkCopyToBufferRect@44`, `_BinkBufferBlit@12`, `_BinkBufferOpen@16` —
  none imported by the game, part (a)): also **zero**, at an 8-hop depth.
- **A raw byte search of every data section** (`BINKCONS`, `BINKDATA`, `.data`, `.rdata`, `RADDATA`,
  `RADCONST`) **and every code section** for any of the 43 functions' own addresses, stored as a
  4-byte little-endian value (i.e. looking for a function-pointer dispatch table): **zero hits in
  every data section.** The *only* hits are five in `.text`, and they are not a table — they are
  five separate small functions, one per family, each testing a CPUID-derived flag
  (`and eax, 0x70000000`) and picking between 1–3 candidate implementation addresses for that one
  family (e.g. `0x10001a82`: picks between `0x10004000` and `0x10004260` for the `BINKY16` family
  based on a bit test; `0x100016e2`/`0x100016f1`: always selects `0x10008000` for `BINK32`),
  storing the winner into a small fixed global record (`0x10030a9c`–`0x10030ab8`) and then calling
  a shared "finalize" helper (`0x1001d550`, or `0x1001d000`/`0x1001ce90` for two of the five) —
  CONFIRMED by direct disassembly, these are genuinely CPU-feature-selection stubs, the standard
  "pick the best SIMD variant for this CPU" idiom.

**So there is confirmed self-registration machinery for this family, but no confirmed consumer.**
This pass did not find what calls the five `.text` selector stubs themselves (plausibly a module
init routine run once at DLL load or at first `BinkOpen` — not traced further this pass), nor any
code that reads the registered pointer back out of `0x10030a9c`/`0x10030aa0`/`0x10030aa4` and calls
through it. The most likely real mechanism is an indirect call (`call dword ptr [reg]`, reading the
pointer from that fixed global record) at the point where `BinkCopyToBuffer`/`BinkBufferBlit` would
actually perform the pixel conversion — exactly the kind of call this project's own
register-hoisted-indirect-call pattern (already documented for a different module,
`spec-bink-boundary.md` §2.2) and `WALLS.md`'s general indirect-call caution predict a direct-E8/E9
census cannot see.

**Verdict: HIGH confidence, not CONFIRMED, that this ~15 KB/43-function family never actually runs
its pixel-conversion bodies in the shipped game.** The two independent lines of evidence — part
(a)'s CONFIRMED absence of every API that would use it from the game's import table, and this
pass's confirmed absence of any direct call edge reaching it from anywhere in the binary, including
from those same unused APIs' own closures — point the same way. What would overturn this: finding
the indirect call site that reads `0x10030a9c` et al. and dispatches through it, and showing that
call site is itself reachable from the game's real 23 imports (not just from the unused
`BinkCopyToBuffer` family) — a plausible but not yet found alternative is that `BinkDoFrame` itself
has some internal, lower-priority output-conversion path that also uses this table; this pass's
closure data for `BinkDoFrame` (§3) shows it reaching deep into Blocks A and B but did not
specifically check whether any of those 120 closure functions also touches `0x10030a9c`'s record —
worth a quick, cheap check before fully closing this out in a future pass.

---

## 7. Proposed dispatchable units, priority order

Following this project's usual pattern (Havok's `D-U0`/`W-U1`/etc., Wwise's `U-W1`–`U-W4b`), sized
from this pass's own function-boundary-aware counts. Priority is by what the shipped game actually
exercises (per part (a)'s CONFIRMED 23-import list and this pass's closure data), highest first.

| Unit | Scope | Functions | Instructions | Priority / rationale |
|---|---|---|---|---|
| **BK-U0** | Container/API/dispatch region: the 76 exports' own bodies, `BinkOpen`'s header/track parsing, handle bookkeeping, the seek/keyframe path, the catch-up/async-pump glue, PLUS the confirmed-unused `BinkBuffer*`/`DX8`/`DX9`/`DD`/multi-track families (physically interleaved, not separately measured this pass) — everything in `BINK` outside Blocks A/B | 359 (335 pre-Block-A + 24 in the small A/B gap) | ~19.7k | **Highest for the game-used subset.** Every game-used export (`Open`/`Close`/`DoFrame`/`Goto`/`Wait`/etc.) lives here or dispatches from here; this is the real "contract" layer a replacement decoder's shell must match. A future unit should split out the confirmed-unused sub-families (not sized separately this pass) before committing to RE-ing the full 359/~19.7k — most of it only needs the game's own 23-import subset documented, not the full 76 exports. |
| **BK-U1** | Video decode core, Block A (`0x10017470`–`0x1001ce50`) | 170 | 7,490 | **Highest — exercised on every decoded frame of every cutscene/logo video.** Reached directly from `BinkDoFrame`/`BinkDoFramePlane`; this is Bink 1's actual video bitstream decode. The likely per-plane decode entry (`0x1001cb00`, at the block's tail) is the natural first function to open. |
| **BK-U2** | Audio decode core, Block B (`0x1001e1d0`–`0x100260d4`) | 104 | 8,854 | **Highest — exercised on every frame of every cutscene with sound**, i.e. almost all of them (the game wires up `BinkOpenDirectSound`, part (a)). Reached from the same `BinkDoFrame` call as BK-U1. Structurally distinct (FPU/XMM-heavy, separate address range) — worth its own unit even though it shares an entry point with BK-U1. |
| **BK-U3** | `RADCODE` (shared Win32-feature/threading shim) | 17 | 971 | **Low, footnote-sized.** Not codec logic (§5); needed only as a short dependency note for BK-U0–U2 (which call into it 21 times combined). Could be folded into BK-U0's write-up as a short subsection rather than a standalone unit. |
| **BK-U4** | Per-bit-depth/pixel-format conversion family: `BINKY12`/`BINKY16`/`BINKP8`/`BINK16`/`BINK32` + their 5 `.text` CPU-selector stubs | 43 (+5 selectors, already counted in `.text`) | 5,351 | **Deprioritize.** HIGH-confidence dead code for this shipped game (§6). Only worth opening if a later unit specifically needs pixel-format-converter behaviour for some unrelated purpose (e.g. a reference/test harness), or to close the one open item in §6 (whether `BinkDoFrame`'s own closure ever touches the registration record). |
| **BK-U5** | `.text`'s remaining generic helpers (DllMain/CRT entry sequence, the ~168 non-selector functions `BINK` calls into 172 times) | ~68 | ~2,160 (minus the 5 selector stubs' own small size) | **Deprioritize, low standalone value.** Almost certainly compiler/CRT-provided primitives (memcpy/memset/float-conversion-style helpers), not Bink-specific logic; worth a short inventory pass only if a future unit needs to know exactly what each of the 172 call sites is actually calling. |

**Suggested order for a future RE pass:** BK-U1 and BK-U2 first (they are the actual codec — the
point of this whole part (b) exercise), using BK-U0 only as needed to establish the exact argument
shapes crossing into them (which this pass did not fully trace — e.g. the precise calling
convention into `0x1001cb00`); BK-U3 as a short footnote inside whichever of BK-U1/U2 needs a
critical-section/thread-naming citation; BK-U4/BK-U5 last, if ever, given §6's dead-code finding.

---

## 8. Notes for whoever takes this up

- **Tooling.** `pefile` + `capstone` with `skipdata=True` was sufficient throughout; no Ghidra was
  needed. The key methodological choice — direct call/jmp **target** addresses as the function-start
  signal, rather than any padding-byte heuristic — is directly reusable for any future pass over
  this DLL (or any other RAD/non-MSVC-toolchain binary in this project), and should be the default
  going forward per `WALLS.md`'s own standing caution.
- **A real pitfall hit and corrected in this pass, worth recording for the next one:** attributing
  a call site to "the nearest preceding export by address" is **not** the same as attributing it to
  its real containing function — in a section with hundreds of unexported internal functions
  between exports, that coarse method mislabels call sites as belonging to whichever export happens
  to sit lowest in address, even when the real caller is a completely unrelated internal function
  that merely happens to sit in that export's "address shadow." This pass caught it by re-deriving
  the *real* candidate-function-start containing each call site (bisecting into the full 633-start
  list) before describing any caller's role — any future pass attributing behaviour by address
  proximity in this DLL should do the same, not trust nearest-export bucketing for anything beyond
  a first-pass orientation.
- **Open items, honestly flagged, not guessed at:**
  - The exact function implementing per-plane video decode (most likely `0x1001cb00`, at the tail
    of Block A, reached directly from both `BinkDoFramePlane` and its internal helper) was not
    itself opened this pass — a natural first target for BK-U1.
  - Block B's exact audio-codec identity (Huffman+DCT-like per Bink 1's public design, vs. a
    different RAD-proprietary transform) was not determined beyond "float/XMM transform-setup-shaped
    code, structurally distinct from the video core" — opening a couple of its deeper functions is
    BK-U2's first real task.
  - §6's one open thread (does `BinkDoFrame`'s own 120-function closure ever touch the
    `0x10030a9c` dispatch record) should be checked before fully closing out the per-format family
    as dead.
  - What calls the five `.text` CPU-feature-selector stubs (§6) — plausibly a module-init routine,
    not traced this pass.
  - The ~5 KB zone between Block A and Block B (`0x1001ce50`–`0x1001e1d0`, holding
    `_BinkControlPlatformFeatures@8`'s own real body plus the shared registration-finalize helpers
    `0x1001d550`/`0x1001d000`/`0x1001ce90`) was identified but not opened in detail — small, low
    priority, but it is genuinely shared infrastructure for BK-U4, not part of either decode core.
- **Out of bounds, confirmed not touched:** no leaked Volition/RAD source of any kind; FFmpeg's
  Bink decoder was not read, consulted, or used anywhere in this pass (reserved, per the user's
  standing instruction, as a future output-only comparison oracle once this project has its own
  independently-derived codec spec). This note does not touch `spec-bink-boundary.md` or any other
  spec file, and is not synced to Team B, per this project's scoping-note convention.
- Every address and figure above is from this scoping pass only. A future spec pass must re-derive
  each claim from the executable before recording it, same as every other note in this series.

---

## 9. Clean-room check

- Addresses are plain hex throughout (all in `binkw32.dll`'s own `0x10xxxxxx` address space, as
  flagged in the preamble). No Ghidra was used anywhere in this pass, so there are no Ghidra
  auto-names, no decompiler variable names, and no pasted pseudocode — every instruction mnemonic
  quoted above (`and eax, 0x70000000`, `movss xmm1, [0x10031318]`, etc.) is literal `capstone`
  disassembly output, read directly, not decompiler reconstruction.
- Names quoted as data: `Bink*` export names are the DLL's own PE export-table names (already this
  project's standard citation convention, `spec-bink-boundary.md`/`scoping-bink.md`); the
  `RADCONST` strings (`IsDebuggerPresent`, `KERNEL32`, `RAD_NO_SET_THREAD_NAMES`, etc.) are literal
  `.rdata`-equivalent content, quoted verbatim as shipped data, the same way this project already
  quotes other modules' literal strings.
- No leaked Volition/RAD source of any kind was read, consulted, or used. No FFmpeg source was read
  or consulted. No spec file was edited by this note; nothing here is synced to Team B.
- The project's standard clean-room regex
  (`\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`,
  plus the plain-substring backstop) was run against the finished file: **0 hits**.
