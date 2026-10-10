# Scoping note — Bink Video (`binkw32.dll`): the game↔Bink boundary, and a size estimate for the codec itself

**Date:** 2026-10-07 · **Type:** scoping note only — no spec content, nothing here is cleared for
implementation. Part (a) below (the game→Bink boundary) is scoped **and**, opportunistically,
already carried to near-spec-quality detail because the real logic turned out to be small enough
to read in full in this one pass; part (b) (the Bink 1 container/codec inside the DLL) is scoped
only, per the user's own ordering, and is **not** attempted here.

**User's explicit instruction on scope (quoted verbatim, highest priority for this note):**
> "Two parts: (a) the game → Bink boundary, meaning which Bink API calls the game makes for
> cutscene and logo playback, with what arguments and frame-pump/sync semantics; (b) the Bink 1
> container plus video/audio codec as implemented in that DLL. Scope (a) first. It's small, and
> it unblocks Team B playing videos through any backend. FFmpeg's open-source Bink decoder
> (LGPL) can serve as a legitimate behavioural cross-check of the codec spec. Compare outputs;
> don't derive from its code."

**On FFmpeg, stated plainly so it is never misread later:** FFmpeg's Bink decoder is not read,
consulted, or used anywhere in this note. It is named here only because the user raised it as a
**future, output-only cross-check for part (b)** (run both decoders on the same sample, diff the
pixels/samples) — never as a source to derive the container/codec spec from. Nothing in this pass
needed it; part (b) is not attempted here.

**Question.** (a) Which exported `Bink*` functions does `binkw32.dll` actually have; which of
them does the game call, directly or indirectly, and with what arguments; which game-side
functions own cutscene playback versus the logo splash; what is the frame-pump loop's shape
(cadence relative to the game's own frame rate, blocking vs polling); how is a decoded frame
handed to the renderer. (b) Roughly how large is the codec/container implementation inside the
DLL, as a scale estimate only.

**Method.** Read-only `pefile` + `capstone` static reads against the shipped binaries only,
in the same style as `tools/harnesses/exe_va.py` and `tools/harnesses/pe_survey.py`
(no decompiler, no Ghidra used this pass — the surface turned out small enough that direct
listings were faster): (1) `binkw32.dll`'s own PE export table, read directly, not assumed;
(2) the main exe's PE import table for `binkw32.dll`; (3) a `.text`-wide byte-pattern scan for
`FF 15 <iat_va>` (`call dword ptr [IAT]`) against every one of the 23 imported slots, giving
exact call-site addresses (not just a count, extending the prior census); (4) linear
`capstone` disassembly of the address range the call sites cluster in, read by hand,
function-by-function, including backward scans past `int3`-padding to find each enclosing
function's start; (5) a direct-E8-call xref scan (own script, same idea as
`tools/harnesses/find_xrefs.py` but for call targets rather than data addresses) to find who
calls the handful of key internal entry points, including a check for *write* sites to one
decision-relevant global flag; (6) a rough size/function-count pass over `binkw32.dll`'s own
code sections for part (b)'s estimate. Also read: `D:\Project Crreish\TEAM A\HANDOFF.md` §27
(current resume state, the Havok/Wwise/Bink authorization and its FORBIDDEN-sources list),
`review/scoping-audio.md` and `review/scoping-physics-vehicle.md` (for this project's scoping-note
format and the §1.2-style call-site table), and
`scratchpad/middleware_census_2026-10-07.md` §9 (the prior whole-binary Bink census this note
extends). **No leaked Volition/RAD source of any kind was read, consulted, or used** — RE is from
the shipped `SaintsRowTheThird.exe` and `binkw32.dll` only, exactly as authorized.
`.czn_pc` zone data was not touched (unrelated, parked item; nothing here is near it).

**Confidence labels** are the project's usual ones: **CONFIRMED** (read directly in this pass),
**HIGH** (inferred from structure), **HYPOTHESIS** (plausible, not checked).

**Naming.** `Bink*` function names below are the DLL's own PE export-table names and the main
exe's own PE import-table names — shipped data, read directly, cited the same way this project
already cites Wwise's export names (`scoping-audio.md`, preamble). Where a public Bink SDK
header would give a named constant for a flag value, I have **not** asserted that name unless I
could verify it in this pass; unverified flag bits are left as plain hex and marked HYPOTHESIS.

---

## 0. Verdict up front

1. **`binkw32.dll` is classic Bink 1.x** (77 exports, read directly from its own export table,
   §1). The main exe imports **23** of them (re-confirmed exactly against the prior census).
2. **The entire game-side Bink-calling surface is one tight, self-contained module**, address
   span `0x00bdb850`–`0x00bde150` of the main exe's `.text` (**~10.5 KB, ~55 functions, ~3,389
   instructions** by a linear disassembly pass — right at the top of this project's usual
   U0 unit size, §7). There is no Bink-calling code anywhere else in the binary. This is a much
   smaller and much more tightly clustered footprint than Wwise's (25 functions spread across an
   82 KB block, `scoping-audio.md` §1.6) — consistent with the user's "it's small."
3. **The prior census's honest "3 imports show zero direct call sites, possibly indirect"
   hedge is now fully resolved, for all three, by direct reads (§2):**
   - `_BinkSetSpeakerVolumes@20` — the game loads its IAT slot into a register **once** and
     issues **5 `call reg` instructions** against it (one per speaker channel). CONFIRMED.
   - `_BinkOpenDirectSound@4` — its IAT slot's value (the function's own address) is loaded and
     **passed as the callback argument to `_BinkSetSoundSystem@8`**; Bink itself calls it
     internally, the game never does directly. CONFIRMED.
   - `_BinkDoFrame@4` — same register-hoist pattern as above, called indirectly **once** per
     synchronous-pump pass. CONFIRMED.
   Counting these, **real call sites (direct + indirect) total 44 across all 23 imports** — the
   28-site direct-only census undercounts because this module repeatedly hoists an IAT pointer
   into a register once per function and reuses it, rather than re-emitting `call dword ptr [iat]`
   each time.
4. **There are two frame-pump implementations in the binary, selected by one global flag byte —
   but only one of them ever runs.** A byte at `0x02989968` selects synchronous
   (`BinkWait`/`BinkDoFrame`) vs asynchronous (`BinkDoFrameAsync`/`BinkDoFrameAsyncWait`) pumping.
   **A write-site scan of the entire executable found zero places that write this byte** — it is
   a statically zero-initialized flag, so **the shipped game always takes the synchronous path**;
   the asynchronous path is present in the binary but, as far as a static census can show,
   unreachable (§4).
5. **The game does not use `BinkCopyToBuffer`/`BinkCopyToBufferRect` at all** — neither is among
   the 23 imports (confirmed directly from the import table). Instead it uses the lower-level
   `BinkRegisterFrameBuffers`/`BinkGetFrameBuffersInfo` pair: the game allocates its own
   16-byte-aligned heap block (tagged allocation, debug string `"bink decompression"`) sized to
   hold the Y/U/V planes, builds the plane-pointer table itself, and hands that memory to Bink to
   decode directly into. **This is plain CPU heap memory, not a locked D3D9 surface** — the
   actual GPU upload/colour-conversion step, if any, is not in this module and was not traced in
   this pass (§5). This corrects the task brief's working assumption that `BinkCopyToBuffer`
   targets a D3D9 texture directly.
6. **Logo splash and (most likely) cutscenes are two different trigger paths into the same
   underlying player**, but the exact wiring for *both* triggers is reached only indirectly
   (function-pointer/registration dispatch, not a plain call), so this is flagged honestly rather
   than forced (§6):
   - the low-level "allocate a player, open the file" entry point has **exactly one call site in
     the whole binary**, inside a function that is clearly the **logo/startup-splash gate** (two
     option/cvar checks, a resolution-by-aspect-ratio chooser, zero direct callers itself — it is
     reached some other way, most likely a boot-sequence function-pointer table, not traced
     further here);
   - a sibling constructor is registered from a **static initializer** and its logic matches a
     console command whose usage string (`"Usage: play_video <file name> (<x> <y> <width>
     <height>)…"`) is present verbatim in `.rdata` — this is the generic, filename-driven entry
     point, the best candidate for what cutscenes also use, but the exact call chain from the
     cutscene-table reader (which only *parses* the `BinkMovieFilename` field,
     `tools/harnesses/cutscene_xtbl_probe.py`) down into this module was **not found by a direct
     call-site scan** of that reader's own address range — OPEN, flagged rather than guessed.

**Rough size for a first spec unit (part a):** the pure boundary contract (open/close, audio
config, the two pump variants, seek, background-IO throttle) is **10 functions, ~1.0–1.3k
instructions**; including the handle-table plumbing, descriptor-apply, resolution chooser and
option-reading scaffolding it is the full module, **~55 functions / ~3,389 instructions** — one
right-sized U0 unit (§7).

---

## 1. The Bink API surface

### 1.1 `binkw32.dll`'s own export table (read directly — 77 exports, CONFIRMED)

The DLL exports the full classic Bink 1.x flat-C API (`__stdcall`-decorated names, byte-count
suffixes). The complete table (ordinal, RVA, name) was read directly from the PE export
directory; the functions relevant to this note are named inline below. Representative entries
(full 77-entry list available in this pass's working notes, not reproduced in full here since
most are untouched by the game — see §1.2):

`_BinkOpen@8`, `_BinkOpenWithOptions@12`, `_BinkClose@4`, `_BinkDoFrame@4`,
`_BinkDoFrameAsync@12`, `_BinkDoFrameAsyncWait@8`, `_BinkNextFrame@4`, `_BinkWait@4`,
`_BinkShouldSkip@4`, `_BinkGoto@12`, `_BinkGetKeyFrame@12`, `_BinkGetRealtime@12`,
`_BinkCopyToBuffer@28`, `_BinkCopyToBufferRect@44`, `_BinkRegisterFrameBuffers@8`,
`_BinkGetFrameBuffersInfo@8`, `_BinkGetSummary@8`, `_BinkSetSoundSystem@8`,
`_BinkOpenDirectSound@4`, `_BinkOpenWaveOut@4`, `_BinkOpenXAudio2@4`, `_BinkOpenMiles@4`,
`_BinkSetSoundTrack@8`, `_BinkSetVolume@12`, `_BinkSetSpeakerVolumes@20`,
`_BinkSetSoundOnOff@8`, `_BinkSetVideoOnOff@8`, `_BinkPause@8`, `_BinkSetWillLoop@8`,
`_BinkControlBackgroundIO@8`, `_BinkSetIOSize@4`, `_BinkGetError@0`, `_BinkSetError@4`,
`_BinkGetPlatformInfo@8`, `_BinkBufferOpen@16`/`Blit`/`Lock`/`Unlock`/… (the separate `BinkBuffer*`
display-helper family, entirely unused — §1.2), `_BinkDX9SurfaceType@4`/`_BinkDX8SurfaceType@4`/
`_BinkDDSurfaceType@4` (surface-type helpers for the `BinkBuffer*` family — also unused, see
below), `_BinkOpenTrack@8`/`_BinkGetTrackData@8`/… (the multi-track "open as data stream" family,
unused), `_BinkLogoAddress@0`, `_RADTimerRead@0`.

### 1.2 The 23 functions the game actually imports (CONFIRMED, from the exe's own import table)

```
_BinkControlBackgroundIO@8   _BinkSetSoundTrack@8        _BinkOpen@8
_BinkSetIOSize@4             _BinkClose@4                _BinkPause@8
_BinkGetRealtime@12          _BinkGoto@12                _BinkGetKeyFrame@12
_BinkDoFrameAsyncWait@8      _BinkSetSpeakerVolumes@20    _BinkOpenDirectSound@4
_BinkNextFrame@4             _BinkShouldSkip@4            _BinkSetWillLoop@8
_BinkWait@4                  _BinkDoFrameAsync@12         _BinkGetSummary@8
_BinkRegisterFrameBuffers@8  _BinkGetFrameBuffersInfo@8   _BinkSetVolume@12
_BinkSetSoundSystem@8        _BinkDoFrame@4
```

**Notably absent:** `BinkCopyToBuffer`/`BinkCopyToBufferRect` (the game decodes straight into its
own buffers instead, §5), the entire `BinkBuffer*` display-helper family (the game does its own
blit/present, not Bink's optional DirectDraw/DX8/DX9 "buffer" convenience layer),
`BinkOpenWaveOut`/`BinkOpenXAudio2`/`BinkOpenMiles` (only `BinkOpenDirectSound` is wired up — the
game's sound backend for movies is DirectSound specifically, not Wwise and not XAudio2 — matches
the census's own finding that the one `DirectSound` string in the whole binary is Bink's own,
`scratchpad/middleware_census_2026-10-07.md` §9), `BinkOpenTrack`/multi-track data-stream API,
`BinkGetPalette`, `BinkSetFrameRate`/`SetFileOffset`/`SetSimulate` (debug/tool-only Bink
functions), the async-thread-pool API (`BinkStartAsyncThread`/`BinkWaitStopAsyncThread`/
`BinkRequestStopAsyncThread` — consistent with §4's finding that the async pump path is never
actually taken).

---

## 2. The game→Bink call-site census (direct + indirect, CONFIRMED)

### 2.1 Direct `call dword ptr [IAT]` sites (`.text`-wide scan against all 23 import slots)

**28 distinct direct call sites, all inside one address range `0x00bdc270`–`0x00bddabd`**
(re-confirms the prior census's count exactly). Three imports show 0 direct sites, explained
below (§2.2):

| Game function | Direct sites | Target(s) |
|---|---|---|
| `0x00bdc270` (audio config) | `0x00bdc2d9`, `0x00bdc39f` | `SetSoundTrack`, `SetVolume` |
| `0x00bdc270` | `0x00bdc2e8` | `ControlBackgroundIO` |
| `0x00bdc3b0` (open) | `0x00bdc3d1`, `0x00bdc526`, `0x00bdc53b` | `SetIOSize`, `SetSoundTrack`, `Open` |
| `0x00bdc560` (close) | `0x00bdc5c2` | `Close` |
| `0x00bdc610` (realtime helper a) | `0x00bdc61b` | `GetRealtime` |
| `0x00bdc640` (realtime helper b) | `0x00bdc64e` | `GetRealtime` |
| `0x00bdc7a0` (seek) | `0x00bdc7c0`, `0x00bdc7da`, `0x00bdc7e5` | `DoFrameAsyncWait` (conditional), `GetKeyFrame`, `Goto` |
| `0x00bdc880` (sound-system config) | `0x00bdc888` | `SetSoundSystem` |
| `0x00bdcd70`/`0x00bdcdc0` (handle-table Pause wrappers) | `0x00bdcdff`, `0x00bdcdb0` | `Pause` |
| `0x00bdd040` (sync pump) | `0x00bdd045`, `0x00bdd068`, `0x00bdd09c`, `0x00bdd0e8`, `0x00bdd10e`, `0x00bdd169` | `Wait`, `SetWillLoop` (conditional), `ShouldSkip` ×2, `NextFrame` ×2 |
| `0x00bdd180` (async pump) | `0x00bdd19a`, `0x00bdd1a4`, `0x00bdd209`, `0x00bdd258` | `DoFrameAsync` ×3, `Wait` |
| `0x00bdd660` (open, top-level) | `0x00bdd85e`, `0x00bddaae`, `0x00bddabd` | `GetFrameBuffersInfo`, `RegisterFrameBuffers`, `GetSummary` |

### 2.2 The three "zero direct site" imports — resolved (CONFIRMED by direct reads)

| Import | How it is actually reached |
|---|---|
| `_BinkSetSpeakerVolumes@20` | `0x00bdc270` loads its IAT value into `edi` once (`0x00bdc2fe`) and issues **5** `call edi` instructions (`0x00bdc30d/32e/34f/368/381`), one per speaker-channel argument set (channel indices 0–4 with position/volume values 1–5 on the stack). |
| `_BinkOpenDirectSound@4` | `0x00bdc880` loads its IAT value into `eax` (`0x00bdc880`) and **passes it as the callback-function argument to `_BinkSetSoundSystem@8`** (`0x00bdc888`) — i.e. the game calls `BinkSetSoundSystem(BinkOpenDirectSound, 0)`; Bink calls `BinkOpenDirectSound` internally, the game never calls it itself. |
| `_BinkDoFrame@4` | `0x00bdd040` (the synchronous pump) loads its IAT value into `ebp` once (`0x00bdd06e`) and issues **1** `call ebp` (`0x00bdd089`), guarded by "is this handle playing and non-null," with a global "current movie" pointer (`0x02989960`) set around the call for re-entrant callback context. |

Adding these indirect sites, **every one of the 23 imports is now accounted for**: total real
call sites (direct + indirect) = 44 (28 direct + 5 + 1 + 1 for the three above, + 4 more indirect
`ControlBackgroundIO` sites and 2 more indirect `DoFrameAsyncWait`/`NextFrame`/`ShouldSkip` sites
inside the background-IO throttle and async pump, detailed in §3–§4).

---

## 3. The game-side module: structure

**Everything lives in one block, `0x00bdb850`–`0x00bde150`** (`.text` of the main exe). It
implements a small handle-table-based "movie player" object type:

- **Handle table**: fixed array at `0x02989984`, stride `0x128` bytes, capacity word at
  `0x0298997a`, plus a hashed free/lookup index at `0x02989988` (division-by-constant-compiled
  hash, `0xdd67c8a7` magic constant — a compiler-generated modulo, HIGH) and a
  critical-section-style container object at `0x02989970` (enter/leave through vtable slots
  `+0xc`/`+0x10`, same convention this project already documented for other subsystems' locks).
  Each slot's first dword is a generation-tagged handle value (self-check pattern also seen in
  this project's other handle tables), `+4` a refcount, `+8` the actual `BINK*` handle from
  `BinkOpen`.
- **`0x00bdc7f0` ("apply descriptor")**: copies a small movie-descriptor struct's fields into a
  new slot — two embedded strings up to 0x20 bytes each (file names — most likely the movie file
  and a companion/alt-track file; not fully traced), a loop/mode dword, and a volume float, then
  calls the audio-config helper (below) with that volume.
- **`0x00bdc270` ("configure audio")**: builds a 12-entry track-index array `[0..0xb]`, calls
  `SetSoundTrack`, calls `ControlBackgroundIO` with flag `0x80000002`, sets 5 speaker-channel
  volumes indirectly (§2.2), then calls `SetVolume` with a value derived from the descriptor's
  volume float. **CONFIRMED** this is a pure setup step, called once from `0x00bdc7f0`.
- **`0x00bdc3b0` ("open")**: resolves which sound track to use (a helper at `0x00462890` returns a
  value tested against flag bit `0x1000000` — HYPOTHESIS: a language/subtitle-track selector),
  conditionally calls `SetIOSize`, looks up a small table (`0x00849940`) to pick between two
  track-index layouts, calls `SetSoundTrack` again with the resolved layout, then calls `Open`
  with flags composed as `(0x4400 or 0x204400, optionally |0x1000000) | 0x4000` (exact bit
  meanings not asserted — plain hex, HYPOTHESIS for any public-SDK name). Stores the resulting
  handle and sets the slot's "opened" state.
- **`0x00bdc890` ("choose resolution")**: picks a target width/height by aspect ratio — if
  `screen_width/screen_height` is at or above a stored threshold, targets `0x500×0x2d0`
  (1280×720); otherwise `0x280×0x168` (640×360) and also writes a `0x3c` (60) field into the
  descriptor. **This is logo-splash-specific** (§6) — a generic file-driven cutscene would not
  need to pick its own target resolution this way.
- **`0x00bdd660` ("open, top-level")**: allocates/zeroes a slot, calls `0x00bdc3b0`, then on
  success calls `_BinkGetFrameBuffersInfo@8`, allocates and builds the plane-pointer table, and
  calls `_BinkRegisterFrameBuffers@8` + `_BinkGetSummary@8` (§5). **Exactly one call site in the
  whole binary** (§6).
- **`0x00bdc560` ("close")**: releases three render-plane helper objects (`+0x90`/`+0x8c`/`+0x94`,
  via their own vtables), calls `Close` on the handle if non-null, then frees two more sub-objects
  through a tagged allocator (debug tag string `"bink internal alloc"`).
- **`0x00bdc7a0` ("seek")**: if the module is async-enabled (always false in the shipped build,
  §4), calls `DoFrameAsyncWait` with a −1 (block) timeout to flush any in-flight async decode;
  then unconditionally calls `GetKeyFrame` (mode `2`, i.e. "nearest keyframe" — HYPOTHESIS for the
  exact mode semantics) and `Goto` with the resolved keyframe number.
- **`0x00bdcd70`/`0x00bdcdc0`/`0x00bdce10`/`0x00bdce60`/`0x00bdced1`/`0x00bdcee0`**: a small bank
  of handle-validated public wrappers (pause, get a struct field, "is playing," "is ready," seek)
  over the handle table — the likely surface a higher-level game/script object calls, though the
  higher-level caller itself was not traced in this pass.

---

## 4. Frame-pump semantics (CONFIRMED)

**Per-movie pump, two variants, selected by one global byte — only one is ever reachable:**

- **`0x02989968`** (byte): `0` → synchronous pump; nonzero → asynchronous pump. **A full-binary
  write-site scan found zero places where this byte is written.** It is a statically
  zero-initialized global, so **the shipped retail build always takes the synchronous path**; the
  asynchronous implementation exists in the binary (and is fully wired to real Bink calls) but is,
  as far as a static census can show, dead code in this build. (Caveat: a write through a
  computed/indirect address would not be caught by this immediate-operand scan; for a single
  always-false-looking flag byte checked only by plain `cmp` elsewhere, that is unlikely but not
  impossible — noted honestly, not force-resolved.)

- **Synchronous pump, `0x00bdd040`:**
  1. `BinkWait(handle)` — **non-blocking poll**, per the public Bink API's own contract: it
     returns "should the caller wait" (nonzero = not yet time for the next frame), it does not
     itself sleep. If it returns nonzero, the pump does nothing else this call.
  2. If ready: optionally `SetWillLoop(handle, 1)` if the descriptor's loop-mode field is `1`.
  3. `BinkDoFrame(handle)` — called **indirectly** (§2.2), only if the slot's refcount is `1` and
     the handle is non-null; a global "current movie" pointer is set around the call.
  4. Catch-up loop: `while (ShouldSkip(handle)) { …rebuild the current sound-track name if it
     changed…; NextFrame(handle); …re-apply loop flag if needed… }`. This is the textbook Bink
     "falling behind real time" catch-up idiom.
  5. End-of-movie check (current frame counter equals total frame count, compared at the handle's
     own struct offsets) and, if not looping, a "finished" callback (`0x00bdcc40`); otherwise one
     more `NextFrame` and clear the "seeking" flag.

- **Asynchronous pump, `0x00bdd180`** (present, unreachable in retail per the flag above): if the
  "seeking" flag is set, kicks `DoFrameAsync(handle, thread-count-global, priority-global)`; then
  `BinkWait`; if ready, `DoFrameAsyncWait(handle, timeout)` with timeout `1000` ms if seeking else
  a poll value; if that returns "not ready," the pump returns early (no block beyond the chosen
  timeout); otherwise the same `ShouldSkip`/`NextFrame` catch-up loop runs, **re-kicking
  `DoFrameAsync` on every iteration** to keep the background decode pipeline fed.

- **Cadence relative to the game's own frame rate:** the global dispatcher that walks every live
  handle and calls one of the two pumps (`0x00bdd270`) is itself called from the **main per-frame
  update dispatcher** — the same giant sequential-subsystem-call function this project's own
  `scoping-physics-vehicle.md` §1.1 already identified at `0x00702a50` (the Bink dispatcher's call
  site `0x00702ca5` sits ~0x255 bytes into that same function, HIGH). **So the pump runs once per
  real rendered game frame, not on physics's fixed 1/30 s step** — and because `BinkWait` is a
  cheap poll, most of those per-frame calls are a no-op until Bink's own internal clock says a
  frame is due. Two more call sites to the same dispatcher (`0x007ad570`, `0x007aeddb`) sit in a
  different function elsewhere — HYPOTHESIS: a second pump context for a blocking/loading-screen
  state, not confirmed further this pass. A companion cleanup pass (`0x00bdd520`, closes any movie
  whose refcount has dropped and it is marked finished) runs immediately after, from the same
  three outer call sites plus one internal use.
- **Background-IO throttle, `0x00bdc660`:** runs after every pump call for every handle (CONFIRMED
  call order inside `0x00bdd270`). Calls `GetRealtime` through the two small helpers (§3) to get a
  playback-percent-complete figure, then toggles `ControlBackgroundIO` (mode `1`/`2`, called
  indirectly through a once-loaded register) when that percent crosses `0x28` (40) or `0x50` (80)
  — buffering-ahead/behind management, separate from the decode pump itself.

**Blocking vs polling, plainly:** nothing in this module blocks the game thread waiting on Bink.
`BinkWait` and `BinkShouldSkip` are both polls; the only bounded wait is the (currently
unreachable) async path's `DoFrameAsyncWait` with an explicit timeout, and even that is only
called from the seek path with its own -1/-1 flush semantics, not from the per-frame pump.

---

## 5. How the decoded frame reaches the renderer (CONFIRMED — corrects the task brief's working assumption)

The task brief's working assumption was that `BinkCopyToBuffer` targets a D3D9 texture/surface.
**That is not what this build does — `BinkCopyToBuffer`/`BinkCopyToBufferRect` are not imported at
all** (§1.2). Instead, inside `0x00bdd660` ("open, top-level"), after a successful `Open`:

1. `BinkGetFrameBuffersInfo(handle, &info)` — fills a local structure with the number of
   frame-buffer sets Bink wants (typically double-buffered) and the per-plane pitch/dimension
   requirements.
2. The game computes 16-byte-aligned plane sizes (`(x + 0xf) & ~0xf`-style masking, repeated per
   plane) for what the Y-plane and the (half-resolution, matching planar YUV) U/V planes need,
   and allocates **one heap block** through this project's already-documented generic tagged
   allocator, with the debug tag string **`"bink decompression"`** (also seen as the heap tag for
   this module's internal handle-table allocations, `"bink internal alloc"`).
3. It fills in a `0x30`-byte-stride array of plane-pointer records (one record per frame-buffer
   set Bink asked for) pointing into that one block, with the stride/pitch values Bink reported.
4. `BinkRegisterFrameBuffers(handle, &frameBuffersStruct)` hands that **caller-owned system
   memory** to Bink; from then on `BinkDoFrame`/`BinkNextFrame` decode directly into it — no
   `BinkCopyToBuffer` call, no CPU-side YUV→RGB conversion inside this module, no D3D9 interface
   touched anywhere in this address range.
5. `BinkGetSummary(handle, &summary)` right after — almost certainly width/height/frame-rate/
   frame-count for the caller to size whatever it draws the movie into (not traced further).

**What this means for Team B / for a later unit:** the actual GPU presentation step — however the
game turns this Y/U/V system-memory buffer into an on-screen D3D9 image (a locked dynamic texture
update plus a YUV→RGB pixel shader is the most likely shape for a title of this vintage, but that
is HYPOTHESIS, not checked) — is **not in this module** and was not located in this pass. This is
the single most load-bearing OPEN item for "unblocking Team B playing videos through any
backend": the game↔Bink half of the contract is now well understood, but the Bink-buffer↔screen
half still needs its own short investigation (see §7's proposed unit).

Three small helper objects (`+0x90`/`+0x8c`/`+0x94` on the handle-table slot, generic vtable
`0x012a0dd0`, constructed via a shared non-Bink-specific helper at `0x00487fc0`) receive
per-plane rect/pitch data (`0x00bdc730`) and are released in `Close` and in the cleanup pass — most
likely thin CPU-side "buffer view" wrappers over sub-ranges of the one allocated block (one per
plane), not D3D9 objects; this was not traced further since it does not change the §5 verdict.

---

## 6. Logo splash versus cutscene playback — two paths, one unresolved wire each

Both the logo splash and (most likely) cutscenes end up at the same low-level player described in
§3, but the exact trigger wiring into it differs and neither was fully closed this pass:

- **Logo/startup splash.** A function at `0x00bddcd0` (itself the body of a larger,
  SEH-wrapped outer function at `0x00bddd50`) is gated by two checks: a "has this already run"
  byte at `0x029cdb88`, and a generic option/cvar call (`0x00d9ed50(0, 2)` — HYPOTHESIS: an
  "intro movies enabled" setting). If both pass, it builds a zeroed movie descriptor, calls
  `0x00bdc890` ("choose resolution," §3) to pick the target size by screen aspect ratio, reads
  four more numeric options (HYPOTHESIS: subtitle/caption-related, same generic option call with
  different indices) into the descriptor, allocates a handle, and calls `0x00bdc7f0` ("apply
  descriptor"). **This function is the only place in the entire binary that reaches
  `0x00bdd660` ("open, top-level")** — a direct-call-site scan found exactly one xref. **Neither
  `0x00bddcd0` nor its outer wrapper `0x00bddd50` has any direct caller of its own** (zero E8
  xrefs found) — it is reached some other way, almost certainly a stored function pointer in a
  boot/startup sequence table (a pattern this project has already seen for other subsystems'
  bring-up), not traced further in this pass. **Flagged honestly as OPEN rather than guessed.**
- **Generic / cutscene path.** A sibling object constructor at `0x00bddc00` has exactly **one**
  caller, at `0x0100c845` — an address in the `0x01000000`–`0x0101ffff` range this project has
  already identified as the static-class-initializer band (`scoping-physics-vehicle.md` §3.3).
  So `0x00bddc00` runs once, at process start, registering something rather than handling a
  per-invocation request — most likely a **console-command object**, since the literal usage
  string `"Usage: play_video <file name> (<x> <y> <width> <height>)\n If the dimensions are not
  specified it will play fullscreen"` sits in `.rdata` nearby. This confirms a developer/debug
  `play_video <file>` command exists and takes an explicit filename plus an optional screen rect
  — the generic, file-driven shape a cutscene call would also need. **The actual runtime handler
  this command (and, by inference, cutscene playback) calls was not localized in this pass**: a
  direct-call-site scan of the cutscene-table-reading code
  (`tools/harnesses/cutscene_xtbl_probe.py`'s `BinkMovieFilename`-reading reader, roughly
  `0x00736800`–`0x00738000`) found **zero** direct calls into the Bink module's address range —
  the reader only parses the field into a struct; whatever later consumes that stored filename to
  actually start playback is a different function not identified here. **This is the clearest
  concrete gap in part (a) and the natural second half of the first spec unit (§7).**

**Plain statement of what is and is not established:** the *low-level* player (open/close,
pump, seek, buffer handoff, §3–§5) is CONFIRMED and almost fully specified by this scoping pass.
*Which exact higher-level game code decides to open a logo movie versus a cutscene movie, and
feeds it a filename*, is HIGH/HYPOTHESIS at best and in the cutscene case is a genuine OPEN item.

---

## 7. Sizing and a proposed first unit for part (a)

| Scope | Functions | Instructions | Status |
|---|---|---|---|
| Pure boundary contract (open/close, audio config, both pumps, seek, IO throttle) | 10 | ~1.0–1.3k | Near-CONFIRMED this pass |
| + handle table, descriptor-apply, resolution chooser, option reads, console-cmd/logo wrappers | ~55 total | ~3,389 total | Mostly read this pass; a few fields/flags left HYPOTHESIS |
| Trigger wiring (who calls the logo gate; who calls the cutscene/console entry with a real filename) | not yet located | small (a handful of functions, HIGH) | **OPEN — the real remaining work** |
| GPU presentation of the decoded Y/U/V buffer (§5) | not yet located | unknown, likely small | **OPEN — the other real remaining work** |

**Proposed first unit, "U0 — Bink game-boundary contract":** write up §2–§5 of this note as a real
spec section (call-site table, argument values, the two pump state machines, the buffer-handoff
struct shapes) — almost all of it is already CONFIRMED here and mostly just needs the formal
write-up plus one or two re-reads to firm up the remaining HYPOTHESIS flag-value labels. Then
spend a small, separate pass resolving the two OPEN items above (trigger wiring, GPU
presentation) — each is likely a handful of functions once found, not a large search, since both
the logo gate and the console command are now pinned to exact addresses to work outward from.
Total estimated size for this whole unit including the two follow-ups: **comparable to this
note's module figure, ~3–4k instructions** — squarely inside this project's usual U0 range and,
per the user, exactly the "small" unit that unblocks Team B.

---

## 8. Part (b) — the codec/container itself: scale estimate only (not attempted)

`binkw32.dll` is 214 KB on disk (re-confirmed). Its PE section table shows the executable code
split across several named sections rather than one plain `.text`: a small `.text` (6 KB) plus
five more small, distinctly-named exec sections (`BINKY12`, `BINKY16`, `BINKP8`, `BINK16`,
`BINK32` — HYPOTHESIS: per-bit-depth/pixel-format conversion routines, from the naming alone, not
opened) totalling under 20 KB, a small `RADCODE` section (2.5 KB, HYPOTHESIS: RAD's shared
bit-reader/CRC utility code), and **one dominant section named `BINK`, 115,200 bytes raw** — by
a rough linear `capstone` disassembly pass (not function-boundary-aware, an approximate upper
bound) **this one section alone is ~36,000 instructions across a rough ~250 function-boundary
count** (`int3`-padding-run proxy, a lower bound since not every function uses that padding
style). **Total across every executable section in the DLL: ~139 KB of code, ~43,000
instructions (rough), ~310 functions (rough lower bound), against 77 exported entry points.**
For scale: that is in the same neighbourhood as this project's own vehicle-physics closure
(`scoping-physics-vehicle.md`, ~21k instructions) roughly doubled, or a little under a quarter of
Wwise's whole statically-linked block (~175k instructions, `scoping-audio.md`) — i.e. a real but
boundeded, single-vendor-codec-sized RE target, not an open-ended one. No internal structure of
this section was opened in this pass (per the user's own ordering, part (b) is scope-only this
pass); the per-bit-depth section names and the dominant `BINK` section are the natural starting
points for a future scoping pass that actually opens the codec. **FFmpeg's own Bink decoder is
not relevant to that future pass's method** — it would only ever be run, unread, as an
output-comparison oracle once this project has its own independently-derived spec to check
against (per the user's instruction, quoted above).

---

## 9. Notes for whoever takes this up

- **Tooling.** No Ghidra was needed this pass — `pefile` for both PE's own tables, and
  `capstone` for linear disassembly of the one small address range, were sufficient. The
  `int3`-padding backward-scan for function starts (also used for the part (b) size estimate)
  is a cheap, reliable heuristic in this codebase's release-build convention (functions
  consistently padded to small alignment boundaries with `int3`); it undercounts only where a
  function happens to immediately follow another with no padding.
- **The register-hoisted indirect-call pattern (§2.2) is common in this module** — any future
  direct-call-site census of a small, hot module should also do an E8/indirect pass before
  reporting "zero call sites" as unresolved; two of this module's three such cases turned out to
  be completely ordinary (a loop reusing one loaded pointer), and the third (`OpenDirectSound`)
  is the standard Bink SDK idiom for wiring up a sound backend, not a mystery.
- **Logo-vs-cutscene wiring and the GPU-presentation step (§5–§6) are genuinely open** — do not
  assume either is a quick corollary of this note; both need their own short, targeted call-site
  search outward from the exact addresses pinned here (`0x00bddcd0`/`0x00bddd50` for the logo
  side, `0x00bddc00` plus the `play_video` usage string for the console/cutscene side,
  `0x00bdd660`'s `BinkRegisterFrameBuffers` call for the render side).
- **Out of bounds:** nothing here touches `.czn_pc` zone data, Havok, or Wwise; no leaked
  Volition/RAD material of any kind was read or used.
- Every label and address above is from this scoping pass only. A spec pass must re-derive each
  claim from the executable before recording it, same as every other note in this series.

## 10. Clean-room check

- Addresses are plain hex throughout. No decompiler was used at any point in this pass, so there
  are no Ghidra auto-names, no decompiler variable names, and no pasted pseudocode — every
  instruction quoted above was read from a `capstone` linear-disassembly listing.
- Names quoted as data: `Bink*` function names are the DLL's own PE export-table names and the
  exe's own PE import-table names; the two debug heap-tag strings (`"bink decompression"`,
  `"bink internal alloc"`) and the console-command usage string are literal `.rdata` content,
  quoted verbatim as data.
- No public or leaked Bink/RAD source of any kind was read or consulted; no FFmpeg source was
  read or consulted (see the FFmpeg note in the preamble). No spec file was edited by this note.
- The project's standard clean-room regex
  (`\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`,
  plus the plain-substring backstop) was run against the finished file: **0 hits**.
