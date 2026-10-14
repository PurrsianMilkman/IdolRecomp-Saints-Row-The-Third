# Saints Row: The Third — Bink Video Runtime: the Game↔Bink Boundary Contract

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** Bink Video, unit **U0** of the proposed order in `review/scoping-bink.md` §7 ("U0 — Bink
game-boundary contract"). This is part **(a)** of the user's own two-part Bink scope, quoted verbatim
in that note's preamble: *"(a) the game → Bink boundary, meaning which Bink API calls the game makes
for cutscene and logo playback, with what arguments and frame-pump/sync semantics; (b) the Bink 1
container plus video/audio codec as implemented in that DLL. Scope (a) first."* Part (b) — the
container/codec implementation inside `binkw32.dll` itself — stays **scope-only** (`review/scoping-bink.md`
§8, a rough size estimate, nothing opened) and is **not** attempted by this file.
**Scope:** The Volition-authored module that sits between the game and the DLL-imported Bink 1.x video
engine (`binkw32.dll`, loaded as `binkw32.dll` next to `SaintsRowTheThird.exe`) — the full 23-import
call surface (44 real call sites, direct and indirect), the handle-table-based movie-player object, the
frame-pump state machine and its unreachable asynchronous sibling, the CPU-side frame-buffer hand-off,
and the two entry points (logo/startup splash, generic/console `play_video`) that reach it. This is the
contract a replacement video backend must satisfy to play the game's logo and cutscene movies without
touching Bink itself. It does **not** open `binkw32.dll`'s own container/codec internals, and it does
**not** use FFmpeg's open-source Bink decoder as a source for anything here — per the user's own
instruction (quoted in `review/scoping-bink.md`'s preamble), FFmpeg is named only as a **future,
output-only cross-check for a later part-(b) pass** (run both decoders on the same sample, diff the
pixels), never read, consulted, or derived from, in that note or in this file.

**Part (b), the container/codec internals this file explicitly stays out of, is now DONE —
`spec-bink-codec.md` (2026-10-07).** It independently confirms, from the decode-core side, this file's
own §5.2 CPU-heap-buffer finding and its §1.3 async-pump-is-dead-code finding, and resolves the "GPU
upload/YUV→RGB step" item this file's §5.2 leaves open halfway (the CPU-side half is now closed; the
GPU-upload half is still open in both files).
**Method:** This unit **formalizes `review/scoping-bink.md`'s findings into spec form; it does not
re-run the disassembly.** That note's own method (its preamble) was read-only `pefile` + `capstone`
static analysis against the shipped `SaintsRowTheThird.exe` and `binkw32.dll` only — no decompiler, no
Ghidra, no leaked Volition/RAD/Bink source of any kind, nothing from `.czn_pc`. Every CONFIRMED claim
below was read directly in that pass: the DLL's own PE export table, the exe's PE import table, a
`.text`-wide `FF 15 <iat_va>` scan against all 23 imported slots, linear `capstone` disassembly of the
address range the call sites cluster in (including backward scans past `int3` padding to find each
enclosing function's start), a direct-E8 call-site/xref scan for the handful of key internal entry
points, and a full-binary write-site scan for the frame-pump flag. This write-up pass re-checked every
figure quoted below against that note for internal consistency — one minor arithmetic gap was found and
is flagged, not silently resolved, in §0.1 — and performed **no new disassembly reads** of its own.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear
anywhere below; no original Volition identifier is used as an identifier — every function, global and
struct field is cited by its plain hex address or offset. `Bink*` function names are `binkw32.dll`'s own
PE export-table names and the exe's own PE import-table names (RAD Game Tools' public API surface,
shipped data, cited the same way this project already cites Wwise's export names in
`spec-audio-runtime.md`); the two debug heap-tag strings and the console-command usage string quoted
below are literal `.rdata` content, quoted verbatim as data, same as that file's practice.
**Confidence key:** **CONFIRMED** (read directly, in `review/scoping-bink.md`'s pass); **HIGH**
(inferred from structure, not independently re-read to the same depth); **HYPOTHESIS** (plausible, not
checked) — the same three labels that note uses.
**Relationship to other work:** `review/scoping-bink.md` is the scoping note this unit formalizes; §0.1
below is the one place this pass sharpened its wording rather than just citing it. `review/scoping-physics-vehicle.md`
§1.1 and §3.3 are cited for two shared landmarks (the per-frame subsystem dispatcher at `0x00702a50`, the
static-class-initializer address band `0x01000000`–`0x0101ffff`). Part (b) — the Bink 1 container/codec
inside `binkw32.dll` — remains scope-only (`review/scoping-bink.md` §8) and is left for a future unit.

---

## 0. Verdict

1. **`binkw32.dll` is classic Bink 1.x** — 77 exports, read directly from its own PE export table
   (§1.1, CONFIRMED). The main exe imports **23** of them (§1.2, CONFIRMED).
2. **The entire game-side Bink-calling surface is one tight, self-contained module**, address span
   `0x00bdb850`–`0x00bde150` of the main exe's `.text` (**~10.5 KB, ~55 functions, ~3,389 instructions**,
   CONFIRMED by a linear disassembly pass). There is no Bink-calling code anywhere else in the binary.
3. **Every one of the 23 imports is accounted for by a real call site**, direct or indirect — including
   the three imports a prior census had left as "zero direct sites, possibly indirect": the game loads
   `_BinkSetSpeakerVolumes@20`'s and `_BinkDoFrame@4`'s IAT slots into a register once per function and
   issues `call reg` against it (register-hoisting, not absence); `_BinkOpenDirectSound@4`'s address is
   passed as a **callback argument** to `_BinkSetSoundSystem@8`, never called by the game itself (§3,
   CONFIRMED).
4. **Exactly one of the two frame-pump implementations is ever reachable in the shipped build.** A
   selector byte at `0x02989968` chooses synchronous (`BinkWait`/`BinkDoFrame`) vs. asynchronous
   (`BinkDoFrameAsync`/`BinkDoFrameAsyncWait`) pumping; a full-binary write-site scan found **zero**
   writers of that byte anywhere, so it is statically zero and **the shipped game always takes the
   synchronous, polling path** (§4, CONFIRMED). The asynchronous implementation is present and fully
   wired to real Bink calls, but is dead code as far as static analysis can show.
5. **The game does not use `BinkCopyToBuffer`/`BinkCopyToBufferRect` at all** — neither is imported. It
   uses the lower-level `BinkRegisterFrameBuffers`/`BinkGetFrameBuffersInfo` pair to decode straight into
   its own CPU heap allocation (§5, CONFIRMED). **The GPU upload/presentation step — RESOLVED
   2026-10-10 — is a dedicated three-texture draw path through the generic Vint/UI primitive renderer**
   (one D3D9 texture per decoded plane, bound simultaneously and drawn via a dedicated shader,
   `rl_prim_2d_bink`; §5.5, CONFIRMED down to the renderer's own texture-handle setter). Two small
   sub-gaps remain: the exact per-plane CPU→texture upload call, and the shader's own unread bytecode.
6. **Logo splash and (most likely) cutscenes are two different trigger paths into the same underlying
   player**, and the exact wiring into *both* triggers is reached only indirectly and was not closed by
   the scoping pass — each is flagged OPEN rather than forced (§6).

### 0.1 One wording gap found while formalizing the scoping note

`review/scoping-bink.md` §2.2 states the real call-site total as **44** (28 direct, §2.1, plus the three
previously-indirect imports resolved there) and then adds "+ 4 more indirect `ControlBackgroundIO` sites
and 2 more indirect `DoFrameAsyncWait`/`NextFrame`/`ShouldSkip` sites inside the background-IO throttle
and async pump". Summing the note's own itemized pieces — 28 (direct) + 5 (`SetSpeakerVolumes`) + 1
(`OpenDirectSound` callback pass) + 1 (`DoFrame`) + 4 + 2 — gives **41**, three short of the stated 44.
The note's descriptive text for the "+4"/"+2" sites (§3's background-IO throttle description, §4's async
catch-up loop) is internally plausible but does not give exact addresses for those six, so this pass
cannot resolve the three-site gap without new disassembly, which is out of scope for a formalization
pass (per this unit's brief: cite, don't re-derive, unless there's a real inconsistency — this is one,
and it is recorded here rather than silently repeating "44" as if it were independently re-verified).
Carried forward as OPEN item 9 (§7). Nothing else in the note's arithmetic (the 28-site direct count,
the ~55-function/~3,389-instruction module figure) failed to re-sum cleanly.

---

## 1. The Bink API surface

### 1.1 `binkw32.dll`'s own exports (77 total, read directly from its PE export table — CONFIRMED)

The DLL exports the full classic Bink 1.x flat-C API (`__stdcall`-decorated names, byte-count suffixes).
Entries relevant to this unit (the complete 77-entry table is in `review/scoping-bink.md`'s working
notes, not reproduced here since most are untouched by the game, §1.3 below):

```
_BinkOpen@8              _BinkOpenWithOptions@12     _BinkClose@4
_BinkDoFrame@4           _BinkDoFrameAsync@12        _BinkDoFrameAsyncWait@8
_BinkNextFrame@4         _BinkWait@4                 _BinkShouldSkip@4
_BinkGoto@12             _BinkGetKeyFrame@12         _BinkGetRealtime@12
_BinkCopyToBuffer@28     _BinkCopyToBufferRect@44    _BinkRegisterFrameBuffers@8
_BinkGetFrameBuffersInfo@8   _BinkGetSummary@8       _BinkSetSoundSystem@8
_BinkOpenDirectSound@4   _BinkOpenWaveOut@4          _BinkOpenXAudio2@4
_BinkOpenMiles@4         _BinkSetSoundTrack@8        _BinkSetVolume@12
_BinkSetSpeakerVolumes@20    _BinkSetSoundOnOff@8    _BinkSetVideoOnOff@8
_BinkPause@8             _BinkSetWillLoop@8          _BinkControlBackgroundIO@8
_BinkSetIOSize@4         _BinkGetError@0             _BinkSetError@4
_BinkGetPlatformInfo@8   _BinkLogoAddress@0          _RADTimerRead@0
```

Plus the entirely-unused `BinkBuffer*` display-helper family (`Open`/`Blit`/`Lock`/`Unlock`/…), the
`BinkDX9SurfaceType`/`BinkDX8SurfaceType`/`BinkDDSurfaceType` helpers that go with it, and the
`BinkOpenTrack`/`BinkGetTrackData`/… multi-track "open as data stream" family.

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

### 1.3 Notably absent — what a replacement backend does *not* need to emulate (CONFIRMED by the import list's absence)

| Not imported | What that means |
|---|---|
| `BinkCopyToBuffer`, `BinkCopyToBufferRect` | The game decodes straight into its own buffers instead (§5) — **the task brief's working assumption that these target a D3D9 surface directly does not hold for this build.** |
| The entire `BinkBuffer*` family, and `BinkDX9SurfaceType`/`BinkDX8SurfaceType`/`BinkDDSurfaceType` | The game does its own blit/present, not Bink's optional DirectDraw/DX8/DX9 "buffer" convenience layer. |
| `BinkOpenWaveOut`, `BinkOpenXAudio2`, `BinkOpenMiles` | Only `BinkOpenDirectSound` is wired up (§3.3) — the movie sound backend is DirectSound specifically, not Wwise, not XAudio2. |
| `BinkOpenTrack` / multi-track data-stream API, `BinkGetPalette` | Unused. |
| `BinkSetFrameRate`, `BinkSetFileOffset`, `BinkSetSimulate` | Debug/tool-only Bink functions; unused. |
| `BinkStartAsyncThread`, `BinkWaitStopAsyncThread`, `BinkRequestStopAsyncThread` (the async thread-pool API) | Consistent with §4's finding that the async pump path is never actually taken. |

---

## 2. The game-side module

Everything lives in one block, `0x00bdb850`–`0x00bde150` (CONFIRMED). It implements a small
handle-table-based "movie player" object type.

### 2.1 The handle table (CONFIRMED structure, HIGH field roles)

- **Fixed array** at `0x02989984`, stride **`0x128`** bytes, capacity word at `0x0298997a`.
- **Hashed free/lookup index** at `0x02989988` — a compiler-generated modulo (division-by-constant,
  magic constant `0xdd67c8a7`), the same compiler idiom this project has already documented for other
  subsystems' hash tables (HIGH).
- **Critical-section-style container object** at `0x02989970`, entered/left through its own vtable
  slots `+0xc`/`+0x10` — the same locking convention this project already documented for other
  subsystems' per-object locks (HIGH).
- **Per-slot layout:** `+0x00` a generation-tagged handle value (the same self-check pattern this
  project has already seen in other handle tables — HIGH), `+0x04` a refcount, `+0x08` the actual
  `BINK*` handle returned by `BinkOpen`. `+0x90`/`+0x8c`/`+0x94` hold the three per-plane render helper
  objects (§5.4).
- **`0x02989960`** — a global "current movie" pointer, set around the (indirect) `BinkDoFrame` call for
  re-entrant callback context (§4.2, CONFIRMED).

### 2.2 Structural functions (CONFIRMED roles unless marked)

| Function | Role |
|---|---|
| `0x00bdc7f0` | **"Apply descriptor."** Copies a small movie-descriptor struct's fields into a new handle-table slot — two embedded strings up to `0x20` bytes each (file names; most likely the movie file and a companion/alt-track file, not fully traced, HIGH), a loop/mode dword, and a volume float — then calls `0x00bdc270` with that volume. |
| `0x00bdc270` | **"Configure audio."** Builds a 12-entry track-index array `[0..0xb]`, calls `SetSoundTrack`, calls `ControlBackgroundIO` with flag `0x80000002`, sets 5 speaker-channel volumes indirectly (§3.2), then calls `SetVolume` with a value derived from the descriptor's volume float. A pure setup step, called once from `0x00bdc7f0`. |
| `0x00bdc3b0` | **"Open."** Resolves which sound track to use via a helper at `0x00462890` whose result is tested against flag bit `0x1000000` (HYPOTHESIS: a language/subtitle-track selector), conditionally calls `SetIOSize`, looks up a small table at `0x00849940` to pick between two track-index layouts, calls `SetSoundTrack` again with the resolved layout, then calls `Open` with flags composed as `(0x4400 or 0x204400, optionally |0x1000000) | 0x4000` (plain hex; exact bit meanings not asserted, HYPOTHESIS for any public-SDK name). Stores the resulting handle and sets the slot's "opened" state. |
| `0x00bdc890` | **"Choose resolution."** Picks a target width/height by aspect ratio: if `screen_width/screen_height` is at or above a stored threshold, targets `0x500×0x2d0` (1280×720); otherwise `0x280×0x168` (640×360) and also writes a `0x3c` (60) field into the descriptor. This is logo-splash-specific (§6.1) — a generic file-driven cutscene has no reason to pick its own target resolution this way. |
| `0x00bdd660` | **"Open, top-level."** Allocates/zeroes a slot, calls `0x00bdc3b0`, then on success calls `BinkGetFrameBuffersInfo`, allocates and builds the plane-pointer table, and calls `BinkRegisterFrameBuffers` + `BinkGetSummary` (§5). **Exactly one call site in the whole binary** (§6.1). |
| `0x00bdc730` | **"Update plane textures."** (newly identified, 2026-10-10, §5.5.) Per open/playing movie, once per frame: for each of the 3 per-plane helper objects, calls that object's own vtable `+0x20` with the current plane-pointer record's `(pointer, pitch)`; clears a "needs update" flag (`+0xd8`). Called via `0x00bdd520`, itself called once per frame from an outer driver (`0x007ad570`, outside this module). |
| `0x00bdc900` | **"Draw one movie's current frame."** (newly identified, 2026-10-10, §5.5.) Clears the plain UI texture handle, sets the draw colour from the movie's own `+0xa8` alpha field, sets the three-handle Bink texture binding from the helper objects' own `+4` fields (via `0x00e4af20`), computes a quad position/scale from `+0x98`/`+0x9c`/`+0xa0`/`+0xa4`, submits the quad, then immediately clears the binding again. |
| `0x00bdca80` / `0x00bdcba0` | **Draw-all-open-movies loop / draw-by-handle.** (newly identified, 2026-10-10, §5.5.) Walk the handle table for qualifying slots (or resolve one handle) and call `0x00bdc900` per slot. `0x00bdca80` is called from the same outer per-frame driver (`0x007ad570`) as `0x00bdc730`'s own caller; `0x00bdcba0` is called from `0x00842ff0` (outside this module, not traced). |
| `0x00bdc560` | **"Close."** Releases the three render-plane helper objects (slot `+0x90`/`+0x8c`/`+0x94`, via their own vtables), calls `Close` on the handle if non-null, then frees two more sub-objects through a tagged allocator (debug tag string `"bink internal alloc"`). |
| `0x00bdc7a0` | **"Seek."** If the module is async-enabled (always false in the shipped build, §4), calls `DoFrameAsyncWait` with a −1 (block) timeout to flush any in-flight async decode; then unconditionally calls `GetKeyFrame` (mode `2`, HYPOTHESIS "nearest keyframe") and `Goto` with the resolved keyframe number. |
| `0x00bdcd70` / `0x00bdcdc0` | Handle-validated public **pause** wrappers over the handle table. |
| `0x00bdce10` | Handle-validated public wrapper, **get a struct field**. |
| `0x00bdce60` | Handle-validated public wrapper, **"is playing."** |
| `0x00bdced1` | Handle-validated public wrapper, **"is ready."** |
| `0x00bdcee0` | Handle-validated public wrapper, **seek** (the per-handle entry point into `0x00bdc7a0`). |

This bank of six wrappers is the likely surface a higher-level game/script object calls, though the
higher-level caller itself was not traced in the scoping pass (§7, OPEN item 5).

---

## 3. The game→Bink call-site census

### 3.1 Direct `call dword ptr [IAT]` sites (CONFIRMED — `.text`-wide scan against all 23 import slots)

**28 distinct direct call sites, all inside `0x00bdc270`–`0x00bddabd`.**

| Game function | Direct call site(s) | Bink API reached |
|---|---|---|
| `0x00bdc270` (configure audio) | `0x00bdc2d9`, `0x00bdc39f` | `SetSoundTrack`, `SetVolume` |
| `0x00bdc270` (configure audio) | `0x00bdc2e8` | `ControlBackgroundIO` |
| `0x00bdc3b0` (open) | `0x00bdc3d1`, `0x00bdc526`, `0x00bdc53b` | `SetIOSize`, `SetSoundTrack`, `Open` |
| `0x00bdc560` (close) | `0x00bdc5c2` | `Close` |
| `0x00bdc610` (realtime helper a) | `0x00bdc61b` | `GetRealtime` |
| `0x00bdc640` (realtime helper b) | `0x00bdc64e` | `GetRealtime` |
| `0x00bdc7a0` (seek) | `0x00bdc7c0`, `0x00bdc7da`, `0x00bdc7e5` | `DoFrameAsyncWait` (conditional), `GetKeyFrame`, `Goto` |
| `0x00bdc880` (sound-system config) | `0x00bdc888` | `SetSoundSystem` |
| `0x00bdcd70` / `0x00bdcdc0` (pause wrappers) | `0x00bdcdff`, `0x00bdcdb0` | `Pause` |
| `0x00bdd040` (sync pump) | `0x00bdd045`, `0x00bdd068`, `0x00bdd09c`, `0x00bdd0e8`, `0x00bdd10e`, `0x00bdd169` | `Wait`, `SetWillLoop` (conditional), `ShouldSkip` ×2, `NextFrame` ×2 |
| `0x00bdd180` (async pump, unreachable, §4.3) | `0x00bdd19a`, `0x00bdd1a4`, `0x00bdd209`, `0x00bdd258` | `DoFrameAsync` ×3, `Wait` |
| `0x00bdd660` (open, top-level) | `0x00bdd85e`, `0x00bddaae`, `0x00bddabd` | `GetFrameBuffersInfo`, `RegisterFrameBuffers`, `GetSummary` |

### 3.2 The three "zero direct site" imports — resolved by direct reads (CONFIRMED)

A prior whole-binary census (`scratchpad/middleware_census_2026-10-07.md` §9) had honestly flagged
these three as "possibly indirect, not traced." The scoping pass resolved all three directly:

| Import | How it is actually reached |
|---|---|
| `_BinkSetSpeakerVolumes@20` | `0x00bdc270` loads its IAT value into `edi` once (`0x00bdc2fe`) and issues **5** `call edi` instructions (`0x00bdc30d`/`32e`/`34f`/`368`/`381`) — one per speaker-channel argument set (channel indices 0–4 with position/volume values 1–5 on the stack). An ordinary loop reusing one loaded IAT pointer, not a mystery. |
| `_BinkOpenDirectSound@4` | `0x00bdc880` loads its IAT value into `eax` (`0x00bdc880`) and **passes it as the callback-function argument to `_BinkSetSoundSystem@8`** (`0x00bdc888`) — i.e. the game calls `BinkSetSoundSystem(BinkOpenDirectSound, 0)`. Bink itself calls `BinkOpenDirectSound` internally; the game never calls it. This is the standard Bink SDK idiom for wiring up a sound backend. |
| `_BinkDoFrame@4` | `0x00bdd040` (the synchronous pump) loads its IAT value into `ebp` once (`0x00bdd06e`) and issues **1** `call ebp` (`0x00bdd089`), guarded by "is this slot's refcount 1 and the handle non-null," with the global "current movie" pointer `0x02989960` (§2.1) set around the call for re-entrant callback context. |

### 3.3 Totals (CONFIRMED for the 35 individually addressed; see §0.1 for a 3-site gap in the stated grand total)

Adding the three resolved imports' sites to the direct census: 28 (direct) + 5 (`SetSpeakerVolumes`) +
1 (`OpenDirectSound`, passed as a callback) + 1 (`DoFrame`) = **35 individually addressed sites**, every
one of the 23 imports now accounted for by at least one real call. `review/scoping-bink.md` §2.2 states
a grand total of **44**, attributing the remaining 9 to "4 more indirect `ControlBackgroundIO` sites"
(the background-IO throttle's two-threshold toggle, §4.5) and "2 more indirect
`DoFrameAsyncWait`/`NextFrame`/`ShouldSkip` sites" (inside the async pump's catch-up loop, §4.3) — both
plausible by the described control flow, but given without exact addresses, and the note's own numbers
sum to 41, not 44 (§0.1, OPEN item 9). **Either way, the headline fact stands and is not in question:
all 23 imports have a real, identified call path; none is reached through any mechanism this pass failed
to find.**

---

## 4. The frame-pump contract

### 4.1 The selector flag — confirmed always-synchronous in the shipped build (CONFIRMED)

**`0x02989968`** (byte): `0` → synchronous pump; nonzero → asynchronous pump. **A full-binary
write-site scan found zero places that write this byte.** It is a statically zero-initialized global,
so **the shipped retail build always takes the synchronous path**; the asynchronous implementation
exists in the binary, fully wired to real Bink calls, but is, as far as a static census can show, dead
code in this build.

> **Caveat, carried over from the scoping note, not resolved further here:** a write through a
> computed/indirect address would not be caught by this immediate-operand scan. For a single
> always-false-looking flag byte checked only by plain `cmp` elsewhere, that is unlikely but not
> impossible. Stated honestly, not force-resolved (OPEN item 6, §7).

### 4.2 Synchronous pump `0x00bdd040` — the only reachable path (CONFIRMED)

1. **`BinkWait(handle)`** — a non-blocking poll per the public Bink API's own contract: returns
   "should the caller wait" (nonzero = not yet time for the next frame); it does not itself sleep. If
   nonzero, the pump does nothing else this call.
2. If ready: optionally `SetWillLoop(handle, 1)` if the descriptor's loop-mode field is `1`.
3. **`BinkDoFrame(handle)`** — called indirectly (§3.2), only if the slot's refcount is `1` and the
   handle is non-null; the global "current movie" pointer `0x02989960` is set around the call.
4. **Catch-up loop:** `while (ShouldSkip(handle)) { …rebuild the current sound-track name if it
   changed…; NextFrame(handle); …re-apply loop flag if needed… }` — the textbook Bink
   "falling behind real time" catch-up idiom.
5. **End-of-movie check** (current frame counter equals total frame count, compared at the handle's
   own struct offsets) and, if not looping, a "finished" callback (`0x00bdcc40`); otherwise one more
   `NextFrame` and clear the "seeking" flag.

### 4.3 Asynchronous pump `0x00bdd180` — present, unreachable in retail (CONFIRMED code, HIGH reachability conclusion per §4.1)

If the "seeking" flag is set, kicks `DoFrameAsync(handle, thread-count-global, priority-global)`; then
`BinkWait`; if ready, `DoFrameAsyncWait(handle, timeout)` with timeout `1000` ms if seeking else a poll
value; if that returns "not ready," the pump returns early (no block beyond the chosen timeout);
otherwise the same `ShouldSkip`/`NextFrame` catch-up loop runs, **re-kicking `DoFrameAsync` on every
iteration** to keep the background decode pipeline fed. A replacement backend has no behavioral
obligation to this path's exact timing, since it never runs in the shipped build — but should not assume
it can never be reached by some build configuration this static census could not see (§4.1's caveat).

### 4.4 Cadence relative to the game's own frame rate (CONFIRMED call chain, HIGH for the second context)

The global dispatcher that walks every live handle and calls one of the two pumps, `0x00bdd270`, is
itself called from the **main per-frame update dispatcher** — the same giant sequential-subsystem-call
function this project already identified at `0x00702a50` (`review/scoping-physics-vehicle.md` §1.1).
The Bink dispatcher's call site, `0x00702ca5`, sits ~`0x255` bytes into that same function (HIGH). **So
the pump runs once per real rendered game frame, not on physics's fixed 1/30 s step** — and because
`BinkWait` is a cheap poll, most of those per-frame calls are a no-op until Bink's own internal clock
says a frame is due.

Two more call sites to the same dispatcher, `0x007ad570` and `0x007aeddb`, sit in a different function
elsewhere (HYPOTHESIS: a second pump context for a blocking/loading-screen state, not confirmed further
by the scoping pass — OPEN item 7, §7). A companion cleanup pass, `0x00bdd520` (closes any movie whose
refcount has dropped and it is marked finished), runs immediately after, from the same three outer call
sites plus one internal use.

### 4.5 Background-IO throttle `0x00bdc660` (CONFIRMED)

Runs after every pump call for every handle (confirmed call order inside `0x00bdd270`). Calls
`GetRealtime` through the two small helpers of §3.1 (`0x00bdc610`/`0x00bdc640`) to get a
playback-percent-complete figure, then toggles `ControlBackgroundIO` (mode `1`/`2`, called indirectly
through a once-loaded register, per the §3.2 pattern) when that percent crosses `0x28` (40) or `0x50`
(80) — buffering-ahead/behind management, separate from the decode pump itself. This is the throttle
whose exact indirect-call addresses are the "4 more" of §3.3's unresolved total (OPEN item 9).

### 4.6 Blocking vs. polling, plainly (CONFIRMED)

**Nothing in this module blocks the game thread waiting on Bink.** `BinkWait` and `BinkShouldSkip` are
both polls; the only bounded wait is the (currently unreachable) async path's `DoFrameAsyncWait` with an
explicit timeout, and even that is only called from the seek path with its own −1/−1 flush semantics
(§2.2, `0x00bdc7a0`), not from the per-frame pump. A replacement backend's own decode can run
asynchronously to the game thread as long as its game-visible polling surface (an equivalent of
`BinkWait`/`BinkShouldSkip`) never blocks on the calling thread.

---

## 5. The renderer hand-off contract

### 5.1 What the game does not do (CONFIRMED by import-table absence, §1.3)

The task brief's working assumption — that `BinkCopyToBuffer` targets a D3D9 texture/surface directly —
**does not hold for this build.** `BinkCopyToBuffer`/`BinkCopyToBufferRect` are not imported at all.

### 5.2 The actual sequence, inside `0x00bdd660` ("open, top-level") after a successful `Open` (CONFIRMED)

1. **`BinkGetFrameBuffersInfo(handle, &info)`** — fills a local structure with the number of
   frame-buffer sets Bink wants (typically double-buffered) and the per-plane pitch/dimension
   requirements.
2. The game computes 16-byte-aligned plane sizes (`(x + 0xf) & ~0xf`-style masking, repeated per plane)
   for the Y-plane and the (half-resolution, matching planar YUV) U/V planes, and allocates **one heap
   block** through this project's already-documented generic tagged allocator, with the debug tag
   string **`"bink decompression"`** (also seen as the heap tag for this module's internal
   handle-table allocations, `"bink internal alloc"`, §2.2).
3. It fills in a `0x30`-byte-stride array of plane-pointer records (one record per frame-buffer set
   Bink asked for) pointing into that one block, with the stride/pitch values Bink reported.
4. **`BinkRegisterFrameBuffers(handle, &frameBuffersStruct)`** hands that **caller-owned system
   memory** to Bink; from then on `BinkDoFrame`/`BinkNextFrame` decode directly into it — no
   `BinkCopyToBuffer` call, no CPU-side YUV→RGB conversion inside this module, no D3D9 interface
   touched anywhere in this address range.
5. **`BinkGetSummary(handle, &summary)`** right after — almost certainly width/height/frame-rate/
   frame-count for the caller to size whatever it draws the movie into (not traced further).

### 5.3 The CPU buffer, stated plainly

This is **plain CPU heap memory, not a locked D3D9 surface**. The decode target the game builds and
hands to Bink is a single tagged allocation holding planar YUV (one Y plane, two half-resolution U/V
planes, per-frame-buffer-set), addressed through the `0x30`-byte plane-pointer records. A replacement
backend reproducing this contract on the *game* side needs only to supply equivalent memory and let its
own decoder (whatever it is) write into it the same way; it does not need to supply a D3D9 texture to
satisfy this half of the contract.

### 5.4 The three per-plane helper objects (CONFIRMED existence and lifecycle, HIGH role)

Three small helper objects, at handle-table slot `+0x90`/`+0x8c`/`+0x94` (§2.1), generic vtable
`0x012a0dd0`, constructed via a shared non-Bink-specific helper at `0x00487fc0`. They receive per-plane
rect/pitch data (`0x00bdc730`) and are released in `Close` (§2.2) and in the dispatcher's cleanup pass
(§4.4).

**CORRECTED/RESOLVED 2026-10-10, §5.5:** these are **not** CPU-side "buffer view" wrappers — each
object's own `+4` field holds a real D3D9 texture handle, confirmed by disassembly: the draw step
(`0x00bdc900`) reads `[helper+4]` from all three objects and passes them directly into the renderer's
three-handle Bink-texture setter (`0x00e4af20`→`0x00e48270`→`0x00e82cb0`). Each object's own vtable
slot `+0x20` (called once per frame, `0x00bdc730`, with the Bink-decoded plane's `(pointer, pitch)`)
is therefore most likely the per-plane texture-upload step (a `LockRect`/memcpy/`UnlockRect` shape,
HIGH CONFIDENCE, not read to the instruction level — see §5.5's own narrowed OPEN item). Full detail:
§5.5.

### 5.5 GPU presentation — CONFIRMED: a dedicated three-texture draw path through the generic Vint/UI primitive renderer, found and traced to its D3D9/Bink-module boundary (2026-10-10)

**RESOLVED, superseding the original "not located" framing.** The game turns the decoded Y/U/V
system-memory buffer into an on-screen image by uploading each plane into its own D3D9 texture (one
per plane, held by the three per-plane helper objects already named in §5.4) and drawing a quad
textured by all three simultaneously, through a dedicated shader — **not** a single packed/interleaved
texture, and not a bespoke Bink-only renderer: it reuses the same generic 2-D primitive-draw system
(`rl_draw`) the Vint/UI layer's own `bitmap`/glyph-run drawing already uses (`spec-vint-render.md` §15),
via a parallel, higher-priority primitive mode the UI itself never emits.

**The call chain, traced this pass from the Bink module outward (CONFIRMED by disassembly):**

```
0x007ad570 (outer per-frame driver, outside the Bink module's own 0x00bdb850-0x00bde150 range)
  -> 0x00bdd520 -> 0x00bdc730 (per-open-movie texture-update step, inside the Bink module)
       for each of the 3 per-plane helper objects (slot +0x90/+0x8c/+0x94):
         calls that object's own vtable slot +0x20 with the plane-pointer record's
         (pointer, pitch) pair — i.e. hands each helper object its current decoded
         plane directly, once per frame. (The actual LockRect/memcpy/UnlockRect, if
         that is the shape, lives inside this call target — generic vtable 0x012a0dd0,
         built via 0x00487fc0 — not traced to the instruction level this pass; see
         the narrowed OPEN item below.)
       clears a per-movie "needs update" flag (+0xd8 = 0)
  -> 0x00bdca80 (draw-all-open-movies loop, inside the Bink module) OR 0x00bdcba0
     (draw-by-handle single entry, called from 0x00842ff0)
       walks the handle table (the same array/stride §2.1 already documents), and for
       every slot whose flags qualify (+0xd9==0, +0xd5==0, +0xd6!=0, "opened"==1) calls:
       -> 0x00bdc900 (THE DRAW STEP, CONFIRMED by disassembly, inside the Bink module):
            1. 0x00e4aef0(-1) — clears the plain UI texture handle (mutual exclusivity
               with the Bink triple, matching §15.2's own priority rule, now confirmed
               from the Bink-caller side too)
            2. 0x00e4ae90(1.0, 1.0, 1.0, movie's own +0xa8 alpha field) — sets draw colour
            3. 0x00e4af20( [helper(+0x94)+4], [helper(+0x8c)+4], [helper(+0x90)+4] )
               -> 0x00e48270 -> 0x00e82cb0: THE SETTER for the "three-handle Bink binding"
               (spec-vint-render.md §15.8's 0x00e82cb0/0x00e82ce0 pair — 0x00e82cb0 is the
               setter, 0x00e82ce0 the getter; §15.8's own text names the storage but not
               this setter). **This independently CONFIRMS §5.4/OPEN-item-3's own
               HYPOTHESIS: the three per-plane helper objects' own +4 field IS the D3D9
               texture handle bound into this triple** — read directly out of each helper
               object (not out of the Bink SDK handle or the raw CPU buffer).
               0x00e48270 also re-confirms mutual exclusivity from this side: once all
               three handles are valid, it explicitly clears the plain texture handle
               (0x00e485e0(-1)) and the material handle (0x00e48630(0xffff)).
            4. Computes a quad position/scale from the movie object's own +0x98/+0x9c/
               +0xa0/+0xa4 fields and calls 0x00e4af50(...) — the actual quad submission,
               using the global sampler/state record at 0x0132d488 (not decoded further
               this pass).
            5. 0x00e4af20(-1,-1,-1) — clears the Bink triple again immediately after
               drawing, so the binding is live only for the duration of this one draw.
```

**This closes the structural question completely** (three separate textures, one per plane, drawn
via a dedicated mode of the generic primitive renderer — not the task's original "one packed texture"
hypothesis) **and substantially narrows what remains open:**

- **CONFIRMED, this pass:** the setter/getter pair for the three-handle binding (`0x00e82cb0`/
  `0x00e82ce0`), the draw-step function (`0x00bdc900`), the per-frame draw-all/draw-by-handle entry
  points (`0x00bdca80`/`0x00bdcba0`), the per-frame texture-update hookup (`0x00bdc730`'s calls into
  each helper object's own vtable `+0x20`), and the mutual-exclusivity logic on both the setter side
  (`0x00e48270`) and the clearer side (already CONFIRMED in `spec-vint-render.md` §15.2). All of
  `0x00bdc730`/`0x00bdc900`/`0x00bdca80`/`0x00bdcba0` are newly-identified functions inside the
  already-established Bink-module address range (`0x00bdb850`–`0x00bde150`) that were not previously
  named in §2.2's function table.
- **Cross-reference, cited not re-derived:** `spec-vint-render.md` §15.1–§15.2/§15.8 independently
  establishes the render-side half — the primitive-code priority rule (Bink triple > texture >
  material), the dedicated shader `rl_prim_2d_bink` (one of exactly 5 startup-built shader materials,
  a first-class sibling of the ordinary UI quad shaders, not a hack), and that codes `0xc`/`0x13`
  (which this draw step's quad submission reaches) are "not used by the UI" — i.e. reached only by
  this Bink path.
- **Still genuinely OPEN, narrower than before:**
  1. **The exact per-plane upload mechanism inside the helper object's own vtable `+0x20`** (generic
     vtable `0x012a0dd0`, built via `0x00487fc0`) — whether it's a `LockRect`/memcpy/`UnlockRect` pair
     as hypothesized, or something else, was not traced to the instruction level this pass. This is now
     a small, well-bounded next step (one vtable slot, one object type) rather than an open-ended
     search.
  2. **`rl_prim_2d_bink`'s own compiled pixel-shader bytecode** — not read this pass. The actual
     YUV→RGB math, if that's what it does, is inferred from "three stages, vector form"
     (`spec-vint-render.md` §15.2 item 4), not confirmed by reading the shader itself. Reading
     `rl_prim_2d_bink_s.fxo_pc` from `shaders.vpp_pc` the same way `spec-vint-render.md` §15.7 already
     read its two siblings is a cheap, well-scoped next step.
  3. The global sampler/state record `0x0132d488` consumed by the quad-submission call `0x00e4af50`
     was not decoded.

Full detail: this pass's own disassembly, folded in here; no separate scratch file.

---

## 6. Logo splash versus cutscene playback — two entry points, two open gaps

Both the logo splash and (most likely) cutscenes end up at the low-level player described in §2–§5, but
the exact trigger wiring into it differs for each, and neither was fully closed by the scoping pass.

### 6.1 Logo/startup splash — the only confirmed caller of the top-level open (CONFIRMED down to the gap)

A function at `0x00bddcd0` (itself the body of a larger, SEH-wrapped outer function at `0x00bddd50`) is
gated by two checks: a "has this already run" byte at `0x029cdb88`, and a generic option/cvar call
(`0x00d9ed50(0, 2)` — HYPOTHESIS: an "intro movies enabled" setting). If both pass, it:

1. builds a zeroed movie descriptor,
2. calls `0x00bdc890` ("choose resolution," §2.2) to pick the target size by screen aspect ratio,
3. reads four more numeric options (HYPOTHESIS: subtitle/caption-related, same generic option call with
   different indices) into the descriptor,
4. allocates a handle, and
5. calls `0x00bdc7f0` ("apply descriptor," §2.2).

**This function is the only place in the entire binary that reaches `0x00bdd660` ("open, top-level") —
a direct-call-site scan found exactly one xref.**

### 6.2 OPEN — the splash gate's own caller (flagged, not invented)

**Neither `0x00bddcd0` nor its outer wrapper `0x00bddd50` has any direct caller of its own** — a direct-E8
xref scan of the whole binary found zero. It is reached some other way, almost certainly a stored
function pointer in a boot/startup sequence table (a pattern this project has already seen for other
subsystems' bring-up), not traced further by the scoping pass. Flagged honestly as OPEN rather than
guessed (OPEN item 2, §7).

### 6.3 Generic / console `play_video` — the file-driven entry point (CONFIRMED down to the gap)

A sibling object constructor at `0x00bddc00` has exactly **one** caller, at `0x0100c845` — an address in
the `0x01000000`–`0x0101ffff` range this project has already identified as the static-class-initializer
band (`review/scoping-physics-vehicle.md` §3.3). So `0x00bddc00` runs once, at process start, registering
something rather than handling a per-invocation request — most likely a **console-command object**,
since the literal usage string

```
Usage: play_video <file name> (<x> <y> <width> <height>)
 If the dimensions are not specified it will play fullscreen
```

sits in `.rdata` nearby (quoted verbatim, as data). This confirms a developer/debug `play_video <file>`
command exists and takes an explicit filename plus an optional screen rect — the generic, file-driven
shape a cutscene call would also need.

### 6.4 OPEN — the cutscene-xtbl → module chain (flagged, not invented — the clearest concrete gap in part (a))

**[CLOSED 2026-10-14, FINAL: the movie is opened at cutscene load by the state-7 handler `0x0072bff0` (`0x007201a0` → `0x00732fc0` → `0x00bddbb0` → `0x00bdd6e0` → BinkOpen), file name `"videos\" + basename(BinkMovieFilename)`; per-shot options `Bink Play` `0x00738150` / `Fullscreen Bink Play` `0x00738260` / `Bink Stop` `0x00738200` start and stop it. None of the 4 candidate handlers opens it. See spec-cutscene-camera-format.md "## In-game cutscene playback, end to end (2026-10-14)". OPEN item 4 in §7 is closed by this.]**

**The actual runtime handler this command (and, by inference, cutscene playback) calls was not
localized by the scoping pass.** A direct-call-site scan of the cutscene-table-reading code
(`tools/harnesses/cutscene_xtbl_probe.py`'s `BinkMovieFilename`-reading reader, roughly
`0x00736800`–`0x00738000`) found **zero** direct calls into this module's address range (`0x00bdb850`–
`0x00bde150`) — that reader only parses the `BinkMovieFilename` field into a struct; whatever later
consumes the stored filename to actually start playback is a different function, not identified here.
**This is the clearest concrete gap in part (a)** (OPEN item 4, §7) — the natural next step once this
unit lands, since both the logo gate (§6.2) and the console command (§6.3/§6.4) are now pinned to exact
addresses to work outward from.

**Cross-reference, 2026-10-08 — narrows the search space, does not close this item.** A sibling
investigation into cutscene triggering (`spec-lua-api-behaviour.md` §57.2a) fully traced the unified
in-engine cutscene pipeline (`cutscene_play_do`→`0x00725df0`→the 20-state state machine `0x0072d660`)
and checked two of that state machine's own handlers for a direct call into this module's address range
(`0x00bdb850`-`0x00bde150`): the `.csc_pc` camera-script load handler (`0x0072bff0`) and the "playback
start" handler (`0x00729150`) — **both confirmed negative, no Bink-module call in either.** The state
machine's remaining still-undumped handlers (`0x0072c790`, `0x00720200`, `0x0072c260`, `0x0072c980` —
per `spec-lua-api-behaviour.md` §26.25's own naming, the "6/7/8" and "10/11/12: playing" states) are now
the single most likely remaining place to find this call chain, narrower than the whole binary this
item's own original scoping left it. Also newly surfaced, data-side: every `.cte_xtbl` scene-table entry
carries both a `CutsceneType`/kind field and its own `BinkMovieFilename` field (`spec-xtbl-format.md`
§6.3), consistent with (not proof of) a cutscene being authored as either a `.csc_pc`-driven in-engine
sequence or a pre-rendered Bink movie, selected per-entry by kind. Full detail:
`scratchpad/cutscene_playback_sequencing_2026-10-08.md`.

### 6.5 Plain statement of what is and is not established

The *low-level* player (open/close, pump, seek, buffer handoff, §2–§5) is CONFIRMED and effectively fully
specified by this unit. *Which exact higher-level game code decides to open a logo movie versus a
cutscene movie, and feeds it a filename*, is HIGH/HYPOTHESIS at best, and in the cutscene case is a
genuine, unresolved OPEN item.

---

## 7. OPEN items

1. **GPU presentation of the decoded Y/U/V buffer (§5.5) — RESOLVED 2026-10-10.** A dedicated
   three-texture draw path through the generic Vint/UI primitive renderer was found and traced in
   full, from the Bink module's per-frame texture-update step through the draw call to the renderer's
   own three-handle binding setter. Two small sub-gaps remain, both narrow: the exact per-plane
   upload mechanism inside the helper objects' own vtable `+0x20` (§5.4), and `rl_prim_2d_bink`'s own
   unread shader bytecode.
2. **The logo/startup splash gate's own caller (§6.2).** Neither `0x00bddcd0` nor `0x00bddd50` has a
   direct E8 caller; almost certainly a stored function pointer in a boot-sequence table. Not traced.
3. **The three per-plane helper objects' exact role (§5.4) — RESOLVED 2026-10-10.** Each object's own
   `+4` field is a real D3D9 texture handle (CONFIRMED by disassembly, §5.5) — not a CPU-side
   buffer-view wrapper as previously hypothesized. The remaining open piece (the upload mechanism
   itself) is folded into item 1 above.
4. **The cutscene-xtbl → Bink-module call chain (§6.4).** The `BinkMovieFilename` reader
   (`0x00736800`–`0x00738000`) makes zero direct calls into this module; the real consumer of the parsed
   filename is unidentified. The clearest concrete gap in part (a).
5. **The higher-level caller of the six public handle-validated wrappers (§2.2, `0x00bdcd70`–`0x00bdcee0`).**
   Not traced — presumably a game/script-facing movie-player object, but not identified.
6. **The frame-pump selector byte `0x02989968`'s zero-writer finding (§4.1) has one standing caveat**: a
   write through a computed/indirect address would not be caught by the immediate-operand scan used.
   Judged unlikely, not re-checked by any other method.
7. **The two extra call sites to the pump dispatcher** (`0x007ad570`, `0x007aeddb`, §4.4) — HYPOTHESIS a
   second pump context for a blocking/loading-screen state. Not confirmed.
8. **Several flag/mode values are plain hex with no asserted public-SDK name** (all HYPOTHESIS, not
   CONFIRMED beyond the value itself): the `Open` flags composition `(0x4400`/`0x204400`, optionally
   `|0x1000000) | 0x4000` and the `0x1000000` track-selector bit (§2.2, `0x00bdc3b0`); `GetKeyFrame`'s
   mode argument `2` (§2.2, `0x00bdc7a0`); the track-index-layout table at `0x00849940`'s two layouts;
   the `ControlBackgroundIO` mode values `1`/`2` and the flag `0x80000002` (§2.2, §4.5).
9. **The scoping note's stated 44-site call total does not visibly sum from its own itemized breakdown**
   (§0.1, §3.3) — 41 by direct addition, 3 short of 44. The headline fact (every import has a real call
   path) is unaffected; the exact count of the background-IO throttle's and async pump's indirect sites
   is not pinned to addresses.
10. **The two embedded descriptor strings** (`0x00bdc7f0`, up to `0x20` bytes each) — most likely the
    movie file and a companion/alt-track file, not independently confirmed.

---

## 8. Clean-room check

- Addresses and offsets are plain hex throughout. No decompiler was used at any point in the scoping
  pass this unit formalizes, and none was used in this write-up pass either — there are no Ghidra
  auto-names, no decompiler variable names, and no pasted pseudocode anywhere in this file; every
  instruction-level claim traces back to a `capstone` linear-disassembly listing read in that pass.
- Names quoted as data: `Bink*` function names are `binkw32.dll`'s own PE export-table names and the
  exe's own PE import-table names; the two debug heap-tag strings (`"bink decompression"`,
  `"bink internal alloc"`) and the `play_video` console-command usage string are literal `.rdata`
  content, quoted verbatim as data.
- No public or leaked Bink/RAD source of any kind was read or consulted, in this file or in the note it
  formalizes; no FFmpeg source was read or consulted (see the Scope line's FFmpeg note above). No
  `.czn_pc` zone data was touched. No leaked Saints Row IV/Volition source of any kind was read or used.
- This is a new, strict-convention spec file (same convention as `spec-player-runtime.md`,
  `spec-ai-runtime.md`, `spec-audio-runtime.md`, `spec-anim-runtime.md`): baseline **0** hits expected.
- **Self-check run on the finished file**, using this project's standard strict regex and its
  plain-substring backstop for Ghidra/decompiler auto-names (the exact patterns are recorded once, in
  `HANDOFF.md` §27.6, and are deliberately not re-quoted here — quoting them verbatim inside a file's own
  clean-room section would make the backstop match its own documentation of itself, a trap worth naming
  so nobody repeats it). **Result: regex 0 hits, backstop 0 hits** against this file's actual content;
  see the Changelog entry for the control-string confirmation.

---

## Changelog

- **2026-10-07 — created** (SPEC TEAM). First version of this file: U0 of `review/scoping-bink.md` §7,
  formalizing that note's §1–§7 into spec form (§1 API surface, §2 the game-side module and handle table,
  §3 the 44-call-site census, §4 the frame-pump contract and its confirmed-dead async sibling, §5 the
  CPU-side renderer hand-off contract, §6 the two entry points and their two open trigger-wiring gaps).
  No new disassembly performed this pass — every CONFIRMED/HIGH/HYPOTHESIS claim is cited to
  `review/scoping-bink.md`'s own direct `pefile`/`capstone` reads. One wording gap found and flagged
  rather than silently repeated: the scoping note's stated 44-site call total does not visibly sum from
  its own itemized breakdown (41 by direct addition) — recorded as §0.1 and OPEN item 9, not resolved
  with new reads. Ten OPEN items carried forward or newly itemized in §7, including the three the task
  brief named as the genuine gaps (GPU-upload step, the splash-gate's own caller, the cutscene→module
  chain). Cleanroom self-check (strict regex + plain-substring backstop, same two checks this project's
  other runtime specs use) run on the finished file: **regex 0 hits, backstop 0 hits**; a separate
  known-positive control file built from one example of each major pattern family the regex targets
  (a decompiler-numbered local, an unnamed-register artifact, and raw address-derived function/data
  labels) matched on all of them in the same pass, confirming the zero above is real and not a vacuous
  check. (The check caught a false start on itself first: the §8 Clean-room check section originally
  quoted the regex/backstop patterns verbatim, which made the backstop match its own documentation of
  itself — fixed by describing the check in prose instead, per the note left in §8, and this entry
  deliberately avoids repeating those literal example tokens for the same reason.) Copied to
  `D:\Project Crreish\TEAM B\spec-bink-boundary.md` and verified byte-identical with `diff`.

- **2026-10-08 — §6.4/OPEN item 4 narrowed, not closed.** A sibling cutscene-triggering investigation
  checked 2 of the cutscene state machine's own handlers for a direct call into this module and found
  both negative, narrowing (not closing) this item's search space to that state machine's 4 remaining
  undumped handlers. No new disassembly in this file. Full detail:
  `scratchpad/cutscene_playback_sequencing_2026-10-08.md`. Cleanroom self-check: 0 new hits.
- **2026-10-10 — §5.5/OPEN item 1 (GPU presentation) RESOLVED; §5.4/OPEN item 3 (per-plane helper
  objects' role) RESOLVED** (SPEC TEAM, agent `aad9a14996668b228` plus independent disassembly by
  the splicing session). The agent's own pass was a synthesis (no new disassembly) that surfaced an
  uncross-referenced lead already sitting in `spec-vint-render.md` §15.1-§15.2: a dedicated
  three-texture draw mode (`rl_draw` codes `0xc`/`0x13`, shader `rl_prim_2d_bink`) that the UI layer
  itself never emits. The splicing session then independently traced the actual call chain by fresh
  disassembly via the Ghidra query server: `0x00bdc730` (per-frame texture-update, newly identified,
  inside the Bink module) feeds each of the 3 per-plane helper objects' own vtable `+0x20`; the draw
  step `0x00bdc900` (newly identified) reads each helper object's own `+4` field — CONFIRMING those
  fields are real D3D9 texture handles, not CPU buffer views as previously hypothesized — and passes
  them into the renderer's three-handle-binding setter `0x00e82cb0` (reached via `0x00e4af20`→
  `0x00e48270`), submits the quad, then clears the binding. `0x00bdca80`/`0x00bdcba0` (also newly
  identified) are the per-frame draw-all/draw-by-handle entry points. All 4 newly-identified
  functions fall inside the already-documented Bink-module address range. Two narrow sub-gaps
  remain, both flagged in place: the exact per-plane upload call inside the helper object's own
  vtable slot, and `rl_prim_2d_bink`'s own unread shader bytecode. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.
