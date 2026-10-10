# Scoping note — audio playback (and the exact Wwise boundary)

**Date:** 2026-10-03 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.
**Question:** what turns a gameplay/script "play this sound" into audible output; how big the game's own part of that is; where exactly the third-party Wwise SDK begins; what is already specified and what is not; in what order to specify the game-owned part.
**Method:** read-only Ghidra headless runs against a private copy of the project (`tools\gp_audioscope`, deleted afterwards): PE export-table enumeration, reference-manager call-site census at the Wwise export boundary, function-size census by address range, listing-only dumps (no decompiler output used) of every game function that calls into Wwise. I also read `spec-lua-api-behaviour.md` (§2, §8, §11.7, §26.17, §55.3 and the tranche notes folded into it), `spec-audio-format.md` §3–§8, and `review/exe-notes-2026-10-01/interp_ranking_tranche_24.md` §I and `interp_stub_hitlist_20261002.md` §E.4.

**Confidence labels** are the project's usual ones: **CONFIRMED** (read directly in this pass), **HIGH** (inferred from structure), **HYPOTHESIS** (plausible, not checked).

**Naming.** Wwise API names below are the binary's own export-table names (fully demangled C++ names are present in the PE export table — `spec-audio-format.md` §6.1), cited the same way the project already cites `AK::SoundEngine::GetIDFromString`. They are shipped data, and they are Audiokinetic's public API surface.

---

## 0. Verdict up front

**The boundary is unusually clean, and almost all of the game's audio code is above it.**

1. **Wwise is one contiguous statically-linked block** (`0x00f22000`–`0x00fbffff`, 2,868 functions, ~175k instructions). It does all decoding (Ogg Vorbis), mixing, effects, 3D panning/attenuation, streaming scheduling and hardware output. None of that is game code, and none of it needs reverse-engineering.
2. **The game calls into Wwise from exactly 25 functions, all inside one ~82 KiB block of its own audio library** (`0x0045cc10`–`0x00470b30`), through **48 distinct exported entry points**, of which only **~19 are used at runtime** (post / stop / pause-resume / seek an event; set RTPC, switch, state; register/unregister a game object; set position(s), obstruction, aux-bus sends; listener; render; load/unload bank by id; hash a name). The rest is init/shutdown. No gameplay, script or UI function touches Wwise directly. Wwise calls back into the game through four memory hooks, two I/O-hook vtables, two `PostEvent` callbacks, a bank-completion callback and (HYPOTHESIS) an audio-input plugin callback — that is the whole reverse direction.
3. **The game's own audio code is ~52k instructions in ~900 functions**, in four layers:
   - a middleware-facing **audio library** (Volition's "audiolib": voice pool, its own threads, per-voice state machine, bank/media/streaming management, voice-line control) — 469 functions / ~26.7k;
   - a **gameplay audio module** (the binary names its source file `game\audio\gameaudio.cpp`: foley, vehicle engines, wind, ambient emitters, radio, commercials) — 334 functions / ~19.3k;
   - the **script-facing play cores** already partly documented (event core, object-sound core, tracking lists) — ~25 functions / ~1.3k;
   - the **Lua/UI bindings**, largely already specified — ~5–6k.
   Of that, **only ~2 % (~1.1k instructions) is genuine pass-through** to Wwise; another ~3 % is real logic that happens to contain Wwise calls; **~95 % never touches Wwise at all**.
4. **The decision-relevant catch** is not in the code but in the data: the game posts only *numeric ids*. What an event actually does — which sounds, which random/sequence/switch containers, RTPC curves, attenuation, bus routing — lives in the shipped `.bnk_pc` banks (Wwise's own `HIRC` chunk format) and is interpreted entirely by Wwise. Replacing Wwise therefore means either licensing it, or re-implementing a sufficient subset of the Wwise bank interpreter and its Vorbis variant (§1.7). The game-side contract to satisfy is small; the content-side contract is not.

**Rough size of the game-owned part that needs real RE work** (excluding Wwise, excluding already-specified bindings and file formats): **~40–45k instructions**, i.e. roughly 4× the UI-render core — but layered so that a useful first slice (sound effects on objects, no radio/voice lines) is ~10–12k (§5).

---

## 1. The Wwise boundary (the decision-critical part)

### 1.1 Where Wwise sits in the binary

**Wwise is statically linked as one contiguous block, roughly `0x00f22000`–`0x00fbffff` of `.text`** (HIGH for the exact edges, CONFIRMED for the bulk):
- The exported `AK::` API runs from `0x00f329e0` (`AK::MemoryMgr::CreatePool`) to `0x00fa44a0` (`CreateVorbisFilePlugin`). The Wwise effect-plugin classes found by RTTI (`CAkCompressorFX`, `CAkDelayFX`, `CAkRoomVerbFX`, `CAkParametricEQFX`, `CAkPeakLimiterFX`, `CAkGuitarDistortionFX`, `CAkFDNReverbFX`, `CAkModalSynth`, `CAkToneGen`, `CAkFXSrcSine`, `CAkFXSrcAudioInput`, `CAkSrcBankVorbis`, `CAkSrcFileVorbis` and others — 31 Wwise-family classes) have their method bodies from `0x00f22260` upward. Plugin factory functions reached from the game's init sit up to `0x00fbc5e0`.
- Below `0x00f22000` the code (`0x00f18000`–`0x00f21fff`) is a separate SIMD-heavy library that calls into `0x00f20000`–`0x00f21fff` but is never reached from audio code; above `0x00fc0000` the engine's own `rl_*` renderer resumes (e.g. `bitmap_writer` vtable target `0x00fc0c00`).
- **Size of the Wwise block: 2,868 functions, 175,104 instructions** (CONFIRMED census of `0x00f22000`–`0x00fbffff`; edges ±1 %). For comparison, that is about 17× the UI-render core in `scoping-ui-render.md`, and about 3.4× all of the game's own audio code put together (§3).
- Inside the block: the sound engine, music engine, motion engine, memory manager, stream manager, the Vorbis decoder (`CAkSrcBankVorbis`/`CAkSrcFileVorbis`, registered as codec `4`), roughly twenty effect/source plugins, and the platform output. **Decode, mixing, effects, 3D panning/attenuation and the hardware output all live inside it** — the game supplies none of them.
- **Three exported Wwise names resolve to game-side 2-instruction stubs**, not to Wwise code: `0x0066ac20` (`AK::Monitor::PostCode`/`PostString`/`SetLocalOutput`, `StartOutputCapture`/`StopOutputCapture`), `0x006b89a0` (`AK::Monitor::GetTimeStamp`, `AK::MemoryMgr::GetPoolName`) and `0x00682c50` (`AK::MemoryMgr::SetMonitoring`/`SetPoolName` wide). These are the shipping-build no-op versions, folded by the linker with unrelated identical functions. They carry no behaviour.
- **The export table is a convenience, not the interface used.** Of the 162 `AK::`-related exports, the game's code calls **48 distinct exported entry points** (plus 3 unexported Wwise functions and the plugin-factory pointers). The game never calls the string-name overloads (`PostEvent(const char*)`, `SetRTPCValue(const char*)`, `LoadBank(const char*)`, …): it hashes names itself (`0x0046fd00` → wide `GetIDFromString`) and uses only the numeric-id overloads. Unused families include `PrepareEvent`/`PrepareBank`/`PrepareGameSyncs`, `DynamicSequence::*`, `DynamicDialogue::ResolveDialogueEvent`, `PostTrigger`, `SetActiveListeners`, `SetAttenuationScalingFactor`, `GetSourcePlayPosition`, `PlaySourcePlugin`, `MusicEngine::GetPlayingSegmentInfo`, `SetEnvironmentVolume(s)`, `SetEffect`, `MotionEngine::Add/RemovePlayerMotionDevice`. (CONFIRMED by the reference census, §1.2.)

### 1.2 Every game-to-Wwise call site (reference-manager census)

Census method: every function inside the Wwise address range that has a code reference from outside that range, with the calling function. 91 reference sites in 30 calling functions, reaching 55 distinct Wwise-side targets (CONFIRMED). Five of the 30 "callers" are not real crossings (they are linker-folded 2–4-instruction stubs shared with unrelated code; §1.4).

| Game function (insns) | Call site(s) | Wwise API reached |
|---|---|---|
| `0x0045cc10` (73) | `0x0045cca5` | `AK::SoundEngine::UnregisterGameObj` |
| `0x0045cd80` (79) | `0x0045ce56` | `AK::SoundEngine::SetGameObjectEnvironmentsValues` |
| `0x0045ce70` (199) | `0x0045d003`, `0x0045d05d` | `AK::SoundEngine::ExecuteActionOnEvent` (numeric-id overload) |
| `0x0045d1e0` (215) | `0x0045d353` / `0x0045d3a0` | `AK::SoundEngine::SetRTPCValue` (id) / `AK::SoundEngine::SetSwitch` (id) |
| `0x0045f2d0` (124) | `0x0045f43a` | `AK::SoundEngine::SetRTPCValue` (id) |
| `0x0045f690` (239) | `0x0045f733`; `0x0045f7bb`/`f841`/`f8b9`/`f91d`/`f9b4`; `0x0045f945` | `SetListenerPosition`; `SetRTPCValue` ×5; `RenderAudio` |
| `0x00462970` (21) | `0x00462999` | `AK::SoundEngine::SetState` (id) |
| `0x00465cc0` (374) | `0x00465e34`; `0x00466072`, `0x0046615c` | `LoadBank` (id + callback overload); `UnloadBank` (id + callback overload) ×2 |
| `0x00466850` (63) | `0x0046687b` | `AK::SoundEngine::SetBankLoadIOSettings` |
| `0x0046b850` (68) | `0x0046b939` / `b947` / `b955` | `AK::StreamMgr::GetFileLocationResolver` / `SetFileLocationResolver` / `CreateDevice` |
| `0x0046f820` (162) | `0x0046f910` | `AK::SoundEngine::StopSourcePlugin` |
| `0x0046fd00` (38) | `0x0046fd61` | `AK::SoundEngine::GetIDFromString` (wide-string overload, `0x00f4c350`) |
| `0x0046fdd0` (223) | 37 sites | init: `MemoryMgr` init (unexported `0x00f32960`), `StreamMgr::GetDefaultSettings`/`Create`/`GetDefaultDeviceSettings`, `SoundEngine::GetDefaultInitSettings`/`GetDefaultPlatformInitSettings`/`Init`, `MusicEngine::GetDefaultInitSettings`/`Init`, `RegisterPlugin` ×18, `RegisterCodec` ×1 (with `CreateVorbisFilePlugin`), `MotionEngine::SetPlayerListener`, `SetListenerPipeline`, `GetSpeakerConfiguration` |
| `0x00470110` (33), `0x00470160` (31) | `0x0047013a`/`014e`, `0x0047018f`/`01a1` | `AK::MemoryMgr::CreatePool` + `SetPoolName` (two pools) |
| `0x004701b0` (28) | 11 sites | shutdown: `MusicEngine::Term`, `SoundEngine::IsInitialized`/`StopAll`/`ClearPreparedEvents`/`UnregisterAllGameObj`/`ClearBanks`/`Term`, `StreamMgr::GetFileLocationResolver`/`SetFileLocationResolver`/`DestroyDevice`, `MemoryMgr::Term` |
| `0x00470280` (159) | `0x004702ef` / `0x00470432` | `AK::SoundEngine::SetPosition` / `SetMultiplePositions` |
| `0x00470450` (61) | `0x004704de` | `AK::SoundEngine::SetObjectObstructionAndOcclusion` |
| `0x00470500` (87) | `0x004705c4` | `AK::SoundEngine::PostEvent` (numeric-id overload, `0x00f4eb90`) |
| `0x00470610` (49) | `0x00470699` | `AK::SoundEngine::RegisterGameObj` |
| `0x00470720` (36) | `0x0047074e` | `AK::SoundEngine::PostEvent` (numeric-id overload) |
| `0x00470790` (30) | `0x004707bc` | `AK::SoundEngine::SeekOnEvent` (id, millisecond overload) |
| `0x004707e0` (25) | `0x004707ff` | `AK::SoundEngine::StopPlayingID` |
| `0x00470960` (30) | `0x00470997` | `AK::SoundEngine::SetRTPCValue` (id) |
| `0x004709b0` (137) | 4 sites | `AK::SoundEngine::SetGameObjectEnvironmentsValues` ×4 |

**Every call-in is in one ~82 KiB block, `0x0045cc10`–`0x00470b30`.** No gameplay, script-binding or UI function calls a Wwise entry point directly (CONFIRMED by the census: the only callers outside that block are the five folded stubs in §1.4).

### 1.3 What crosses the boundary (argument shapes at real call sites)

All read from listings of the calling function in this pass (CONFIRMED for the pushed values; the parameter *meaning* is taken from the public Wwise signature encoded in the export's own mangled name — e.g. `?PostEvent@SoundEngine@AK@@YAKKIKP6AX…` = `(AkUniqueID, AkGameObjectID, AkUInt32 flags, callback, cookie, AkUInt32 cExternals, AkExternalSourceInfo*)` — so the mapping of value to parameter is HIGH, and named enum values are HYPOTHESIS from the public SDK).

**Common guard.** Every thin wrapper first tests a byte at **`0x031728c6`** ("sound engine is up"; written only by the init path `0x00462460`, `0x00462780` and `0x00466850`) and returns `-0x12e` (`0xfffffed2`) when it is clear; a null voice returns `-0x64`/`-0x67`. This is a second "audio initialised" byte, distinct from the event core's `0x031728c2` already in `spec-lua-api-behaviour.md` (tranche 24 §I).

**Game-object identity.** The `AkGameObjectID` the game hands Wwise is **the address of its own voice record** (CONFIRMED: `0x00470610` passes the record pointer as the id to `RegisterGameObj`, and the same pointer is the object argument at every later `SetPosition`/`PostEvent`/`SetRTPCValue` site). The registration name is the record's first dword printed with a two-character format string at `0x0129fb94` (bytes `25 75` = `"%u"`). Global (non-object) calls pass `-1` (the SDK's "invalid/global object").

| Wwise API (call site) | Arguments the game passes |
|---|---|
| `PostEvent` (`0x004705c4`, in `0x00470500`) | event id from voice `+0x08`; object = voice; **flags `9`** (HYPOTHESIS `EndOfEvent | Duration`); callback **`0x00469ea0`** (game code); cookie = voice `+0x04`; **one external source** (`cExternals = 1`) built on the stack with the value `4` in it (HYPOTHESIS the Vorbis codec id, matching the `RegisterCodec(…, 4, …)` at init) and a file id looked up by `0x00468ae0`. **This is the dialogue/voice-line path** (external sources are how Wwise plays per-line streamed files; HIGH). Result (playing id) → voice `+0x0c`, "playing" byte → voice `+0x1e`. Chosen when voice `+0x18` bit `0x01000000` is set. |
| `PostEvent` (`0x0047074e`, in `0x00470720`) | event id from voice `+0x08`; object passed in a register; **flags `0x1000d`** (HYPOTHESIS `EndOfEvent | Marker | Duration | EnableGetSourcePlayPosition`); callback **`0x004706c0`** (game code); cookie = voice `+0x04`; no external sources. The ordinary sound-effect path. |
| `StopPlayingID` (`0x004707ff`) | playing id from voice `+0x0c`; fade `0` ms; curve `4` (HYPOTHESIS linear). Clears `+0x0c`/`+0x1e`. |
| `ExecuteActionOnEvent` (`0x0045d003`, `0x0045d05d`) | event id from voice `+0x08`; action **`1`** / **`2`** (HYPOTHESIS Pause / Resume); object; fade **`10`** ms; curve `4`. |
| `SeekOnEvent` (`0x004707bc`) | event id (voice `+0x08`); object; position from voice `+0x14` (milliseconds overload; reset to `-1` after); "seek to nearest marker" = 0. |
| `SetPosition` (`0x004702ef`) | object; a 6-float position+orientation copied from voice `+0x28…+0x3c`; listener index `-1`. Used when the voice's position count (`+0x9f`) is 1. |
| `SetMultiplePositions` (`0x00470432`) | object; up to N 24-byte position records gathered from a linked list (next pointer at `+0x1c`); count = `+0x9f`; type `2` (HYPOTHESIS "multi-directions"). |
| `SetObjectObstructionAndOcclusion` (`0x004704de`) | object; listener `0`; obstruction = byte `+0xa1` / a double constant at `0x012a2dd8` (HYPOTHESIS 100.0); occlusion = `0.0`. The byte is slewed toward a target at `+0xa2` by at most 7 per call, clamped to 100 — **game-side smoothing**, not Wwise's. |
| `SetRTPCValue` (`0x00470997`; `0x0045f43a`; `0x0045d353`; five sites in `0x0045f690`) | RTPC id, float value, object (a voice, or `-1` for global). The two generic setters first ask `0x00470830` whether the value changed (HIGH: a value cache that suppresses redundant calls). |
| `SetSwitch` (`0x0045d3a0`) | switch-group id, switch-state id (from a per-voice list at voice `+0x78`), object. |
| `SetState` (`0x00462999`) | state-group id, state id (global). |
| `SetGameObjectEnvironmentsValues` (`0x0045ce56`; ×4 in `0x004709b0`) | object; pointer to environment (aux-bus) value array; count. Gated by a byte at `0x03172883`. |
| `SetListenerPosition` (`0x0045f733`) | listener record, index 0 — once per frame from `0x0045f690`. |
| `RenderAudio` (`0x0045f945`) | none — once per frame, end of `0x0045f690`. |
| `LoadBank` / `UnloadBank` (`0x00465e34`, `0x00466072`, `0x0046615c`) | numeric bank id + completion callback + cookie (already in `spec-audio-format.md` §7.5). |
| `GetIDFromString` (`0x0046fd61`) | a UTF-16 copy of the name (already in `spec-lua-api-behaviour.md` / stub hit-list E.4: FNV-1 over the lower-cased name). |

**What comes back across the boundary.**
1. **Return values:** `PostEvent`'s playing id (stored in the voice), `AKRESULT` codes (compared against `1` = success at most sites, then mapped to game-side negative error codes such as `-0x137`, `-0x13c`, `-0x140`).
2. **Callbacks into game code (Wwise → game):** listed in §1.5.
3. **Status reporting on the game side.** The wrappers themselves report state changes to a game routine `0x004719b0` with a small code and the voice cookie (`+0x04`): codes `4` and `6` after a successful resume/pause, `7` after a stop, `8` after a failed `PostEvent` (CONFIRMED at those sites). Whether the two `PostEvent` callbacks report "finished" through the same routine is OPEN (§1.5).
4. **No shared memory.** The game never reads Wwise's own globals, with one exception: the shutdown path reads the exported `IAkStreamMgr::m_pStreamMgr` (`0x02cc5f2c`, at `0x004701fd`) to destroy the stream manager through its interface.

### 1.4 Census entries that are not real crossings

Five "callers" in the census are linker artefacts (identical-code folding), not game code using Wwise: `0x005c3130`→`0x00f3cf10` (2 insns), `0x007347d0`/`0x0074a9f0`→`0x00f66290` (4), `0x00da8d90`→`0x00f4f9f0` (2), `0x0101b050`→`0x00f351a0` (4). The targets are tiny generic bodies that happen to be placed inside the Wwise block and are shared by unrelated classes (the earlier RTTI pass shows dozens of non-audio vtables — activities, Havok agents, `rl_*` renderers — pointing at `0x00f52900`, `0x00f4f9f0`, `0x00f823e0` the same way). CONFIRMED by size; nothing to specify. The same applies in the other direction to `0x0046f7b0` (22 insns, called once from Wwise's `0x00f80b30`): it packs six bitfields (18/6/5/2/1-bit widths) into a two-dword record — HIGH: an inline helper from the public SDK headers (an audio-format "set all fields" helper) that the linker kept in the game's copy. It is Wwise-header code, not game logic.

### 1.5 Wwise → game: every place Wwise calls back into the game

| Mechanism | Game code | What it is |
|---|---|---|
| Direct `CALL` from Wwise's memory manager (`0x00f329e0`, `0x00f32990`, `0x00f32d10`, `0x00f32ff0`) | `0x0046fc90` (6 insns), `0x0046fcb0` (1), `0x0046fcc0` (10), `0x0046fce0` (8) | **The four memory hooks the SDK requires the game to define** (HIGH, by shape): allocate (forwards to a C-runtime allocation routine `0x00ea3362` with a constant `0x800`), free (jumps to `0x00ea3348`), virtual-allocate (Win32 `VirtualAlloc`), virtual-free (Win32 `VirtualFree`). CONFIRMED as code; roles HIGH. |
| Virtual calls on a game-owned object registered with `StreamMgr` (`spec-audio-format.md` §7) | resolver vtable **`0x0129f13c`** (3 slots: `0x0046b660`, `0x0046b970`, `0x0046ba10`); deferred-I/O-hook vtable **`0x0129f14c`** (8 slots: `0x0046c700`, `0x0046ba20`, `0x0046ba80`, `0x00444690`, `0x00444dc0`, `0x0046ba90`, `0x0046bac0`, `0x0046bad0`) | The file-location resolver + low-level I/O hook. The abstract base vtables immediately before them (`0x0129f0f0` 5 slots, `0x0129f108` 8 slots, `0x0129f12c` 3 slots, every non-destructor slot = the pure-call stub `0x00ea2778`) match the public `IAkLowLevelIOHook` / `IAkIOHookDeferred` / `IAkFileLocationResolver` shapes (CONFIRMED read; mapping HIGH). **Side finding:** this resolves the ⚠ CONFLICT marker in `spec-audio-format.md` §7.3 — that section's "slots 1, 5, 11" count straight across both tables starting at `0x0129f13c`; in per-table terms they are resolver slot 1 (`0x0046b970`), hook slot 1 (`0x0046ba20`, "Close" in the public order) and hook slot 7 (`0x0046bad0`, "Cancel"). Two hook slots (`0x00444690`, `0x00444dc0`) are folded generic stubs. |
| `PostEvent` callback pointers | `0x00469ea0` (voice-line path), `0x004706c0` (sound-effect path) | End-of-event / marker / duration notifications back into the voice system (HIGH from the flags). |
| `LoadBank`/`UnloadBank` completion callback | passed at `0x00465e34` (`spec-audio-format.md` §7.5) | Bank-load completion into the bank state machine. |
| Audio-input source plugin callback | `0x0046f820` (162 insns), registered at init (`0x00470066`, next to the unexported Wwise call `0x00fa8780`) | HYPOTHESIS: the `AkAudioInput` source plugin's sample/format callbacks (Wwise plugin id 200, `CAkFXSrcAudioInput` is linked in) — the game feeding PCM into Wwise from its own decoder, most likely video/cutscene audio. Its only Wwise call is `StopSourcePlugin(200, …)`. Not checked further. |

### 1.6 Game code versus middleware — the size split

| Part | Functions | Instructions | Share of game-owned audio |
|---|---|---|---|
| **Wwise SDK (out of scope)** — `0x00f22000`–`0x00fbffff` | 2,868 | 175,104 | — |
| Pure pass-through wrappers: `0x0046fd00`–`0x00470b5f` (name hash, init, pools, shutdown, and the eleven one-call wrappers) + `0x00462970` | 16 | ~1,080 | **~2 %** |
| Game functions that contain Wwise calls but are mostly game logic: per-voice tick `0x0045d1e0`, state machine `0x0045ce70`, positional update `0x0045cd80`, release `0x0045cc10`, frame tick `0x0045f690`, global RTPC setter `0x0045f2d0`, bank state machine `0x00465cc0`, bank-I/O setup `0x00466850`, stream device setup `0x0046b850`, audio-input callback `0x0046f820` | 10 | ~1,600 | ~3 % |
| Everything else game-owned (never calls Wwise) | ~870 | ~49k | ~95 % |
| **All game-owned audio code** (§3) | **~900** | **~52k** | 100 % |

So by instruction count, **~77 % of all audio code in the binary is Wwise and ~23 % is the game's**; and of the game's part, **only the ~2.7k instructions in the 25 calling functions ever touch the boundary**. The call-site contract for those 25 functions is the entire thing a replacement audio engine must satisfy on the code side.

### 1.7 What this means for the middleware decision (scoping observations, not a recommendation)

- **License Wwise (same SDK generation).** The game-side work is then only the game-owned layers (§3), with §1.2–§1.5 as the adapter contract. The exported names give the exact SDK API generation (e.g. `PostEvent` with external-source arguments, `IAkIOHookDeferred`, `DynamicSequence`/`DynamicDialogue` present, `MotionEngine` present); matching bank-format compatibility to that generation is the main risk (HYPOTHESIS: a newer SDK will not read these banks).
- **Open-source or minimal engine.** The code-side contract is small (~19 runtime operations, numeric ids only, one listener, one codec). The **content-side contract is the cost**: event → action → container → sound resolution, switch/state/RTPC semantics and curves, attenuation, aux-bus sends, voice limits and markers are all inside the banks' `HIRC` data and Wwise's interpreter. `spec-audio-format.md` §3 deliberately declares `HIRC` out of scope today. A "minimal" engine would have to bring that back into scope (at least a subset), plus decode Wwise's Vorbis variant (HYPOTHESIS from public knowledge, not checked: not a plain Ogg stream). External-source voice lines (`.lm_pc`/`DMLV`, `spec-tables-audio-radio.md` §17) and the audio-input path (§1.5) are additional, smaller items.
- **Either way the game-owned layers are the same work.** None of §3's game code depends on Wwise internals; it only depends on the ~19 runtime operations behaving as the public API documents.

---

## 2. The pipeline: from "play this sound" to audible output

```
gameplay / script (hundreds of call sites)
  ├─ name → id: 0x00462960 → 0x0046fd00 → AK::SoundEngine::GetIDFromString (FNV-1, already specified)
  ├─ one-shot on the default 2D voice: event core 0x00a26640 → 0x0045d990
  ├─ sound on a world object: object-sound core 0x00a2acb0 / 0x00a2ad00 / 0x00a2ada0 (32 emitter slots)
  ├─ ambient emitters 0x00563100/0x005631f0, foley, vehicle engines, wind, radio … (gameplay audio module 0x00553000–0x00564fff)
  └─ voice lines / personas / conversations (voice_control, line situations, external sources)
        │
        ▼  game "voice" API (audio library), all under one lock (0x031f0260):
        │    create 0x0045da50 · bind/param 0x0045f5b0 · play/commit 0x0045ea70 · release 0x0045e170
        │    other per-voice setters/queries 0x0045f1a0, 0x0045ed50, 0x0045dbc0; global RTPC 0x0045f2d0
        │    → voice records (stride 0xa8), queued; nothing reaches Wwise yet
        ▼
"Main Audio Thread" (0x00471030 starts it; loop 0x00470e20, ~16 ms sleep — HIGH)
  ├─ "Playlist Process" task: per-voice tick 0x0045d1e0
  │     ├─ lazy RegisterGameObj (0x00470610), positions/obstruction/RTPC/env (0x0045cd80 → 0x00470280/0x00470450/0x00470960/0x004709b0)
  │     ├─ per-voice RTPC and switch lists → SetRTPCValue / SetSwitch
  │     ├─ state machine 0x0045ce70 → PostEvent (0x00470720 sfx, 0x00470500 voice line) / StopPlayingID / Pause / Resume / Seek
  │     └─ teardown 0x0045cc10 → UnregisterGameObj
  ├─ "Soundbank Process" task: bank state machine 0x00465cc0 → LoadBank / UnloadBank (by id)
  └─ frame tick 0x0045f690: SetListenerPosition, five global RTPCs, RenderAudio
        │
        ▼
   Wwise (0x00f22000–0x00fbffff): event resolution from bank HIRC data, Vorbis decode, mix, effects, output
        │   reads data through the game's I/O hook (vtables 0x0129f13c / 0x0129f14c) → .bnk_pc / _media.bnk_pc / .lm_pc
        ▼
feedback: PostEvent callbacks 0x004706c0 / 0x00469ea0 → status message 0x004719b0 (code + voice cookie, timestamped, queued, event signalled)
        → "Audio callback thread" (0x00471c70) → gameplay; playing id kept in voice +0x0c
```

### 2.1 Gameplay and script posting (game-owned)

- **Name hashing** — `0x00462960` (5 insns, **281 references**) → `0x0046fd00` (38) → `0x0046fc50` (26, widen) → wide `GetIDFromString`. Fully specified already (FNV-1, 751/751 banks). CONFIRMED.
- **Event core** `0x00a26640` (88) → `0x0045d990` (81, 56 callers): make a temporary non-positional voice, play, release; falls back to the voice pointed to by `0x02cea938` (HIGH: a default 2D voice). Records results in the 16-slot list `0x02703ce0` via `0x00a37ca0` (35). Already specified (`spec-lua-api-behaviour.md`, tranche 24 §I). The sibling `0x00a267b0` (122, reached via `0x00a269f0`) does the same through the voice API directly (create / bind / play / release) — CONFIRMED callees, role HIGH.
- **Object-sound core** `0x00a2acb0` (27, 18 refs), `0x00a2ad00` (47, characters), `0x00a2ada0` (47, 10 refs) → slot allocator `0x00a2a850` (78) over 32 emitter slots `0x026ea498`, handle re-resolution `0x00a2a780` (72), emitter choice `0x00a2a950` (200, creates/binds/plays/releases a voice), "finished?" `0x00a2a560` (38, asks the voice layer via `0x0045ed50`). Mostly specified; the id-space question (engine serial `0x026ea798` vs Wwise playing id) is OPEN there.
- **Gameplay audio module** `0x00553000`–`0x00564fff` (334 functions, ~19.3k; source-file string `game\audio\gameaudio.cpp`). By its strings (CONFIRMED) it holds: `audio_constants.xtbl` play timers (brass, glass, bullet impacts, debris, vehicle impact/scrape), sound-bank group names (`PAK%d_ALWAYS_LOADED`, `PAK%d_VEHICLE_ENGINE`, `…_IGNITION`, `…_ENGINE_STOP`, `…_TIRES`, `…_HORN`), vehicle engine foley (`foley_engine.xtbl`), collision and touch foley (`foley_collision.xtbl`, `foley_touch.xtbl` — Wwise switch per foley set), wind, ambient emitters (`audio_emitter`, `"Emitter"` category), and the whole radio system (`radio_stations.xtbl`, `commercials.xtbl`, `commercial_events.xtbl`, `radio_events.xtbl`, mix tape, sing-alongs, news breaks, police/FBI stations). 123 of its 334 functions call the voice API directly. The table *schemas* are specified (`spec-tables-audio-radio.md`); the runtime behaviour is not.
- **Other gameplay clients.** The voice API's entry points are referenced from all over the binary (e.g. `0x0045ea70` 185 refs, `0x0045f5b0` 161, `0x0045f1a0` 130, `0x0045ed50` 76, `0x0045da50` 75, `0x0045e170` 63, `0x0045f2d0` 51 — weapons, vehicles, UI, missions, cutscenes). These are clients; they need only the voice-API contract.

### 2.2 The game's audio library ("audiolib", `0x0045bfc0`–`0x0047262f`, game-owned)

469 functions / 26,675 instructions (CONFIRMED census). Sub-areas by address and strings (HIGH):

| Address band | Funcs / insns | Content |
|---|---|---|
| `0x0045bfc0`–`0x0045f9df` | 57 / 4,352 | **Voice core**: voice API (create/bind/play/release/setters/queries), per-voice tick, state machine, positional update, release, frame tick, global RTPC; `audio_info.vad_pc` loader `0x0045d4c0` |
| `0x0045f9e0`–`0x004623ff` | 81 / 2,815 | Object pools and intrusive lists backing the voice core (string `object pool`; many 15–40-insn template bodies) |
| `0x00462400`–`0x00462bff` | 19 / 554 | Init `0x00462460` (214; default external source `Default_Ext_Src_external_source` / `voc_Default_Ext_Src_play`), shutdown, `SetState` wrapper, name-hash entry |
| `0x00462c00`–`0x004673ff` | 83 / 5,561 | **Banks**: `audio_banks.xtbl` loader `0x00464a70` (383), media-bank names and slot allocator (`Bump Slot_allocator_media_banks!`, `Bump AUDIOLIB_MAX_SOUNDBANKS!`), `_media.bnk_pc` / `.mbnk_pc` lookup, bank state machine `0x00465cc0` (374), "Soundbank Process" task setup `0x00466850` |
| `0x00467400`–`0x0046a3ff` | 69 / 3,624 | `audio_settings.xtbl` (speed of sound, doppler), `voc_sb_line_sit.xtbl` `0x00468ed0`, **`voice_control.xtbl` `0x00469140`** (voice-line cooldowns, delays, play percent, priority, external source, play event), external-source file lookup `0x00468ae0` (161) |
| `0x0046a400`–`0x0046c6ff` | 49 / 2,614 | I/O hook object and file-location resolver (`spec-audio-format.md` §7), stream device setup `0x0046b850` |
| `0x0046c700`–`0x0046fcff` | 51 / 4,143 | Deferred I/O read/cancel, persona-line streaming (`.lm_`, "started streaming persona line data"), four large functions `0x0046ca50`/`0x0046cdd0`/`0x0046d250`/`0x0046db00` (281–633 insns, called only from the ambient-emitter area of the gameplay module — role OPEN), `microphone_play` `0x0046fa70`, audio-input callback `0x0046f820`, memory hooks |
| `0x0046fd00`–`0x00470b5f` | 15 / 1,057 | Name hash, Wwise init `0x0046fdd0`, pools, shutdown, the eleven thin wrappers |
| `0x00470b60`–`0x0047262f` | 45 / 1,955 | RTPC value cache `0x00470830`, "Main Audio Thread" `0x00471030`/`0x00470e20`, status-message queue `0x004719b0`/`0x00471a30`, "Audio callback thread" `0x00471c70` |

**Two "initialised" bytes**: `0x031728c2` (voice-API level; the event core's guard) and `0x031728c6` (Wwise is up; guards every wrapper). A third, `0x03171a57`, gates the per-voice tick and the playlist task. CONFIRMED reads/writes; roles HIGH.

### 2.3 Middleware (Wwise) — see §1. Decode, mix, output.

### 2.4 Feedback to gameplay (game-owned)

- `PostEvent`'s return (playing id) is stored at voice `+0x0c`; a zero return triggers status code `8` (CONFIRMED).
- Callback `0x004706c0` (sound-effect path): Wwise callback type `1` → status `7`, type `4` → marker handler `0x00471a30`, type `8` → status `4`. Callback `0x00469ea0` (voice-line path): bit `0x1` → status `9` then `7`; bit `0x8` → status `4`. CONFIRMED listing; meaning of the type values HIGH (public `AkCallbackType`: end-of-event, marker, duration).
- `0x004719b0` (38): takes a message record from a pool, stamps it with a millisecond time, stores code + cookie, appends it to a queue under a critical section and signals an event (`0x03245408`); the **"Audio callback thread"** drains it. So **every completion reaches gameplay asynchronously through one queue**. Status codes seen: `4` playing/duration known, `6` paused, `7` ended/stopped, `8` post failed, `9` voice-line ended (HIGH from the sites).
- Script-side "is it still playing" checks go back through the voice layer (`0x00a2a560` → `0x0045ed50`), not to Wwise (CONFIRMED call chain).

---

## 3. Main functions and sizes

| Stage | Key addresses | Insns (approx.) | What it does |
|---|---|---|---|
| Name → id | `0x00462960`, `0x0046fd00`, `0x0046fc50` | 5 + 38 + 26 | FNV-1 via Wwise (specified) |
| Script play cores | `0x00a26640`, `0x00a267b0`, `0x00a26980`, `0x00a269f0`; `0x00a2a200`–`0x00a2ae36` (18 funcs); `0x00a37ca0`, `0x00a37550` | 282 + 898 + ~60 | event core, object-sound core, 32 emitter slots, tracking list |
| Voice API | `0x0045da50` (93), `0x0045f5b0` (63), `0x0045ea70` (126), `0x0045e170` (54), `0x0045f1a0` (93), `0x0045ed50` (57), `0x0045dbc0` (57), `0x0045f2d0` (124), `0x0045d990` (81), `0x0045f470` (92) | ~840 | the game's audio-object interface used by every client |
| Voice update (audio thread) | `0x0045d1e0` (215), `0x0045ce70` (199), `0x0045cd80` (79), `0x0045cc10` (73), `0x0045c230` (160), RTPC cache `0x00470830` (90) | ~820 | per-voice registration, parameters, play state machine, teardown |
| Thread / frame | `0x00471030` (26), `0x00470e20` (136), `0x0045d650` (57), `0x0045f690` (239) | ~460 | Main Audio Thread, tasks, listener + global RTPC + `RenderAudio` |
| Feedback | `0x004706c0`, `0x00469ea0`, `0x004719b0` (38), `0x00471a30` (61), `0x00471c70` (42) | ~250 | callbacks → status queue → callback thread |
| Thin Wwise wrappers + init/term | `0x00470110`–`0x004709b0`, `0x0046fdd0` (223), `0x004701b0`, `0x00462970`, `0x00462460` (214) | ~1,300 | §1.2 |
| Banks / media / I/O | `0x00462c00`–`0x004673ff`, `0x0046a400`–`0x0046fcff` (part) | ~10k | bank registry, slot allocators, state machine, I/O hook, streaming |
| Voice lines / settings | `0x00467400`–`0x0046a3ff` | ~3.6k | voice_control, line situations, settings, external sources |
| Pools / containers | `0x0045f9e0`–`0x004623ff` | ~2.8k | object pools for voices and lists |
| Gameplay audio module | `0x00553000`–`0x00564fff` | ~19.3k | foley, vehicles, wind, emitters (~12.9k incl. `audio_constants`/banks); radio/commercials (~6.4k, `0x0055bd30`–`0x00561450`) |
| Lua / UI bindings | `0x00a3c000`–`0x00a40fff` (audio part), `0x008437e0`–`0x00843af0` | ~5–6k | already largely specified |

**Data structures** (scoping identifications only; no layouts claimed):
- the voice record (stride `0xa8`; fields seen: `+0x04` cookie, `+0x08` event id, `+0x0c` playing id, `+0x14` seek position, `+0x18` flags incl. bit `0x01000000` "voice line", `+0x1c`/`+0x1e` state bytes, `+0x28`–`+0x3c` position/orientation, `+0x78` switch list, `+0x98` flags, `+0x9f` position count, `+0xa1`/`+0xa2` obstruction current/target, `+0xa6`);
- the voice pool and lists (`0x031d4cf0`, `0x031de510`, `0x031e3d20`) under lock `0x031f0260`;
- the 32 emitter slots (`0x026ea498`) and serial (`0x026ea798`); the 16-slot post list (`0x02703ce0`);
- the ambient-emitter list (`0x03171a64`);
- the bank record (`0x5c` stride, `spec-audio-format.md` §7.5; `spec-tables-audio-radio.md` §2.3);
- the status-message records (`0x54` bytes) and queue;
- the I/O hook object (vtables `0x0129f13c`/`0x0129f14c`).

**Totals.**

| Scope | Functions | Instructions |
|---|---|---|
| Audio library ("audiolib") | 469 | 26.7k |
| Gameplay audio module | 334 | 19.3k |
| Script play cores + tracking | ~25 | ~1.3k |
| Lua/UI audio bindings (mostly specified) | ~60–70 | ~5–6k |
| **All game-owned audio** | **~890–900** | **~52k** |
| — of which still needing RE (excl. bindings, file formats, specified cores) | ~800 | **~40–45k** |
| **Core playback slice** (name → voice API → audio thread → Wwise contract → feedback; no banks internals, radio, voice lines or foley content) | **~110** | **~8–10k** |
| Wwise (not to be reversed) | 2,868 | 175.1k |

---

## 4. Specified versus missing

### 4.1 What is specified already

- **Name hashing** — complete and validated against data (FNV-1, lower-cased, 751/751 bank headers; crash shape for 128+ character names).
- **Container formats** — `.bnk_pc` (as a Wwise bank, out of scope), `_media.bnk_pc` wrapper record table (536/536 files), the cross-reference to bank ids (`spec-audio-format.md` §4–§5).
- **Wwise SDK surface and integration outline** — exports, codec, I/O hook and bank state machine outlines (`spec-audio-format.md` §6–§7, several items NEEDS-EXE).
- **Audio data tables** — schemas and record layouts for `audio_banks`, `audio_constants`, `audio_settings`, `audio_line_tags`, `audio_personas`, foley (collision/touch/engine), radio stations, playlists, radio events, commercials, `voc_sb_line_sit` (`spec-tables-audio-radio.md`).
- **Conversations** — `.ctdg_pc` format and load/playback path (`spec-conversation-format.md`).
- **Script surface** — ~30 audio Lua bindings with argument/return shapes and several defects (`spec-lua-api-behaviour.md` §2, §8, §11.7, §12.2, §14.22, §26.17, §55.3 and tranches 24/stub hit-list), including the event core, the object-sound core's 32 emitter slots and serial counter, the 16-slot post list, the RTPC-on-instance path, ambient emitter start/stop.

### 4.2 What is missing, and whether it needs RE

| Gap | Needs RE? | Notes |
|---|---|---|
| **Wwise adapter contract** (the 25 calling functions: argument values, init settings — pools, stream device, plugin list, listener pipeline — and shutdown order) | **Yes, small** | §1.2–§1.5 already lists sites and most values; a spec pass makes it exact. Decision-relevant. |
| **Voice record and voice API semantics** (create/bind/play/release; what `0x0045f5b0`, `0x0045f1a0`, `0x0045ed50`, `0x0045dbc0` set or answer; the two voice categories `"UI"` / `"Emitter"`; default voice `0x02cea938`) | **Yes** | The interface every client uses; resolves the two OPEN role conflicts already flagged in `spec-lua-api-behaviour.md` §2.2/§26.17. |
| **Audio thread model** (Main Audio Thread loop, Playlist and Soundbank tasks, frame tick, lock discipline) | **Yes, small** | Ordering and latency of requests; ~0.5k |
| **Per-voice state machine** (play / stop / pause / resume / seek; game-object register/unregister lifetime; position vs multi-position; obstruction slew; RTPC cache; per-voice switch/RTPC lists) | **Yes** | ~0.8k; the heart of "does it play correctly" |
| **Feedback path** (callback types → status codes → queue → callback thread → gameplay; the two id spaces) | **Yes, small** | Closes the OPEN "which stop accepts which id" item |
| **Five global RTPCs and the listener** set each frame in `0x0045f690` (two hard-coded ids `0xdb2d51a3`, `0x9e3669da`, three from variables) | **Yes, small** | Which game values drive mix parameters |
| **Bank lifecycle triggers** (`spec-audio-format.md` §8 OPEN; `audio_banks.xtbl` flags `load_at_boot`/`streaming`/`cacheable`; `PAK%d_*` groups) | **Yes** | Already queued as NEEDS-EXE items there |
| I/O hook vtable conflict (`spec-audio-format.md` §7.3) | **Resolved here as a side finding** (§1.5) | Two tables: 3-slot resolver, 8-slot deferred I/O hook |
| **Voice lines** (`voice_control.xtbl` runtime: cooldowns, priority, play percent; line situations; external-source `PostEvent`; `.lm_pc` streaming) | **Yes** | ~4–6k; joins `spec-conversation-format.md` |
| **Foley / vehicle engine / wind / impacts** runtime (gameplay audio module) | **Yes** | ~8–10k; table schemas exist, behaviour does not |
| **Radio** (station scheduling, songs, intros/outros, commercials, news, mix tape, sing-alongs) | **Yes, large** | ~6.4k; recommend its own spec |
| Four large unknown functions `0x0046ca50`–`0x0046db00` | **Yes (identify)** | ~1.7k, called only from the emitter area |
| `microphone_play`, audio-input plugin path | Probably park | Likely video/voice-chat audio |
| Decode / mix / effects / output | **No** — Wwise | Unless the middleware decision says otherwise (§1.7) |
| `HIRC` bank content semantics | **No today** (declared out of scope) | **Becomes yes** if Wwise is not licensed (§1.7) |

**Plain answer.** Roughly **one sixth** of the game-owned audio (formats, tables, script surface, name hashing) is specified. The core playback path between the script surface and Wwise — voice API, audio thread, per-voice state machine, adapter contract, feedback — is **entirely unspecified**, but it is small (~8–10k) and very well bounded.

---

## 5. Proposed order of specification

Dependency first, then by how much audible output each unit unlocks.

1. **A0 — Wwise adapter contract** (§1.2–§1.5 made exact: the 25 calling functions, init settings, shutdown order, memory hooks, callback signatures; ~2.7k insns).
   - *Why first:* it is the decision input for the middleware question, it is small, and every later unit ends in one of these calls.
2. **A1 — Close `spec-audio-format.md`'s NEEDS-EXE items on the I/O hook and bank state machine, plus the bank lifecycle triggers** (§7.3–§8 there; `audio_banks.xtbl` flags).
   - *Why here:* nothing is audible until the right banks are resident. Partly queued already.
3. **A2 — Voice record + voice API** (`0x0045da50`, `0x0045f5b0`, `0x0045ea70`, `0x0045e170`, `0x0045f1a0`, `0x0045ed50`, `0x0045dbc0`, `0x0045f2d0`, `0x0045d990`; pools only as far as behaviour needs; ~1–1.5k + pool behaviour).
   - *Why here:* it is the single interface between ~hundreds of gameplay call sites and the audio thread.
4. **A3 — Audio thread + per-voice tick + state machine + frame tick** (`0x00470e20`, `0x0045d650`, `0x0045d1e0`, `0x0045ce70`, `0x0045cd80`, `0x0045cc10`, `0x00470830`, `0x0045f690`; ~1.5k).
   - *Validation:* every resulting Wwise call is already enumerated in A0, so the spec can be checked call by call.
5. **A4 — Feedback path** (`0x004706c0`, `0x00469ea0`, `0x004719b0`, `0x00471a30`, callback thread `0x00471c70` and its consumer; ~0.5k).
   - *Why here:* closes the id-space OPEN items in the Lua spec; looping/one-shot lifetimes depend on it.
6. **A5 — Script play cores, closed out** (event core, object-sound core, ambient emitters: finish the OPEN items using A2–A4).
   - *Milestone:* at this point scripted and object sounds play end-to-end — the "first useful slice" (~8–10k total).
7. **A6 — Gameplay audio content systems**: impacts/`audio_constants` play timers, foley collision/touch, vehicle engine audio, wind (~8–10k).
8. **A7 — Voice lines and personas** (`voice_control`, line situations, external sources, `.lm_pc` streaming; ~4–6k), joined with `spec-conversation-format.md`.
9. **A8 — Radio** (~6.4k), as its own spec.

**Recommended to park:** `microphone_play` / audio-input plugin path; the four unidentified large functions until A6 identifies their caller's purpose; `MotionEngine` (only `SetPlayerListener` is called).

**Not needed:** anything inside `0x00f22000`–`0x00fbffff`, unless the middleware decision is "replace Wwise", in which case the new work item is a bank-content (`HIRC`) interpreter subset, which is a separate and much larger scoping question than anything in this note.

**Rough weight against the other candidates.** Core playback (A0–A5, ~8–10k) is about the size of the UI-render core, with an equally clean seam. The whole game-owned audio system (~40–45k still needing RE) is about 4× that, but its outer layers (content systems, voice lines, radio) are independent of each other and can be scheduled or parked separately.

---

## 6. Notes for whoever takes it up

- **Tooling.** The PE export table carries fully demangled Wwise names, so the boundary census is a pure reference-manager query on the export addresses; it does not need RTTI or heuristics. Five census hits are folded stubs (§1.4) — filter by callee size.
- **Calling conventions.** The thin wrappers pass the voice pointer in a register (`ESI`, sometimes `EDI`/`ECX`/`EDX`) rather than on the stack; Ghidra shows them as "unknown" convention. Read the listings, not a decompile.
- **Threads.** Four named threads/tasks are visible by string: "Main Audio Thread", "Playlist Process", "Soundbank Process", "Audio callback thread". Anything reached from them runs off the game thread — behaviour specs should state which side each field is written from.
- **Folded code.** Several Wwise exports (`AK::Monitor::*`, `AK::MemoryMgr::SetMonitoring`/`GetPoolName`) resolve to 2-instruction game stubs, and several game vtables point into the Wwise block; both are linker folding, not real coupling.
- **Out of bounds:** nothing here touches `.czn_pc` zone data or named-object resolution. The object-sound core resolves object handles through the already-documented resolvers only; that was not opened further.
- Every label above is from this scoping pass only. A spec pass must re-derive each claim from the executable before recording it.

## 7. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names and no pasted pseudocode. Only listing-mode dumps were read for this note (no decompiler output was used).
- Names quoted as data:
  - Wwise API names come from the binary's own PE export table (Audiokinetic's public API).
  - Class names come from the RTTI type-descriptor strings (`CAk…`, `IAk…`).
  - Thread, table and file names come from string literals in the binary.
- The project's standard regex was run on the finished file with `grep -cP`: **0 hits**.
- No spec file was edited. The private Ghidra copy `tools\gp_audioscope` was deleted after the dumps.

---

## 8. Wwise-internals scoping (middleware RE newly authorized 2026-10-07) — bank/HIRC load, event→action→container→voice, RTPC/switch/state, Vorbis-variant decode, streaming/IO

**Date:** 2026-10-07 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.
**Why this exists:** the user directly authorized full RE of Wwise's internals (`HANDOFF.md` §27.H-1), lifting the prior "boundary only" hold. This section proposes how to split that work into U0–U4-sized sub-units, in the user's own stated order (bank/HIRC load → event→action→container→voice → RTPC/switch/state → Vorbis variant decode → streaming/IO thread), using the already-closed game↔Wwise boundary (§1–§7 above) and the A7 `wwiser` bank-content pilot (cited in `HANDOFF.md` §27.H-2: 267 banks, 23,135 HIRC nodes, only 12 of Wwise's ~20+ node kinds ever used, Action breaks into 11 verbs) as scoping inputs. **FORBIDDEN list obeyed exactly as stated in the dispatch brief: no leaked SR4/Re-Elected source, no leaked Havok source, no other leaked Volition/Havok/Audiokinetic material was read or used anywhere in this pass.** `.czn_pc` zone data was not touched (unrelated, still parked).

**Method:** read-only PE export-table enumeration and raw `pefile`+`capstone` disassembly (the project's `tools\harnesses\exe_va.py`/`disasm_window.py`/`find_xrefs.py` pattern, extended with purpose-written scratch scripts, not committed to the project) directly against the shipped `SaintsRowTheThird.exe` — no Ghidra project was opened for this pass (the questions below were answerable from the export table, a direct MSVC-RTTI Complete-Object-Locator walk, and listing-mode disassembly alone). No decompiler output was used anywhere in this section.

**A method caveat that applies to every instruction-count number below:** sizes were produced by a purpose-written heuristic (linear sweep from each candidate entry, tracking the furthest forward branch target seen so far, accepting the first `ret`/`retn` at or beyond that target as the function's end; direct `CALL`s with an immediate operand are followed as callees, indirect calls — register- or memory-indirect — are counted but **not** followed). This is not Ghidra's function-boundary analysis and has no ground truth to check against in this pass; treat every instruction-count/function-count figure below as **HIGH, not CONFIRMED**, even though each individual disassembly listing quoted is a direct, CONFIRMED read of real bytes. Indirect calls are flagged explicitly wherever they appear, because they are exactly where this method's reach stops — and, as shown below, that is usually exactly where the real HIRC-object-kind/action-verb dispatch lives.

### 8.1 Entry points (CONFIRMED, PE export table)

Re-read directly from the export table (same table §1.1 already characterizes), narrowed to the exact overloads the game calls per §1.2/§1.3 above:

| Role | Address | Export name (as shipped — demangled by the linker itself) |
|---|---|---|
| `LoadBank` (id + callback, the one the game calls) | `0x00f4bb90` | `LoadBank@SoundEngine@AK` `(AkBankID, callback, cookie)` |
| `LoadBank` (plain id) | `0x00f4ba70` | `LoadBank@SoundEngine@AK` `(AkBankID, &out)` |
| `UnloadBank` (plain id, the one the game calls) | `0x00f4bc40` | `UnloadBank@SoundEngine@AK` `(AkBankID, &out)` |
| `UnloadBank` (id + callback) | `0x00f4bcf0` | `UnloadBank@SoundEngine@AK` |
| `PostEvent` (id + callback + external sources, the one the game calls) | `0x00f4eb90` | matches §1.2's `0x00f4eb90` exactly |
| `ExecuteActionOnEvent` (id overload) | `0x00f4e320` | the Pause/Resume target from §1.3 |
| `StopPlayingID` | `0x00f4c130` | matches §1.2's target |
| `SeekOnEvent` (id, ms overload) | `0x00f4e3e0` | matches §1.3's target |
| `SetRTPCValue` (id) | `0x00f4afe0` | matches §1.2's target |
| `SetSwitch` (id) | `0x00f4b030` | matches §1.2's target |
| `SetState` (id) | `0x00f4c640` | matches §1.2's target |
| `StreamMgr::Create` | `0x00f99c40` | |
| `StreamMgr::CreateDevice` | `0x00f99ef0` | matches §1.2's target |
| `StreamMgr::GetFileLocationResolver` / `SetFileLocationResolver` | `0x00f993f0` / `0x00f99400` | matches §1.2's target |
| `RegisterCodec` | `0x00f4ac90` | the codec-`4` registration site (game init, §1.2) |
| `CreateVorbisFilePlugin` | `0x00fa44a0` | matches §1.1's upper-bound landmark exactly |

All CONFIRMED by direct export-table read; cross-checked against the existing call-site table in §1.2/§1.3 and found consistent (same addresses, same overloads).

### 8.2 Three internal Wwise classes named directly in the export table — the strongest anchor found this pass

A full re-read of this project's own pre-existing export-table dump (`tools\audio_wwise_exports.txt` — 217 total exports / 162 AK-matching, the exact figures already cited in §1.1, confirming it is the same census this note's §1 is built from) turns up **three Wwise-internal C++ class names that the linker happened to export in demangled form, alongside the public `AK::` API** — not RTTI, not a heuristic table-walk, read directly off the export table's own demangled names (**CONFIRMED**):

| Export | Address | Demangled signature |
|---|---|---|
| `GetObjAndAddref` | `0x00f6e550` | `CAkRegistryMgr::GetObjAndAddref(AkGameObjectID) -> CAkRegisteredObj*` |
| `SetRTPCInternal` | `0x00f70b10` | `CAkRTPCMgr::SetRTPCInternal(AkRtpcID, float, CAkRegisteredObj*) -> AKRESULT` |
| `~CAkRegisteredObj` | `0x00f555e0` | `CAkRegisteredObj`'s own destructor |
| `g_pRegistryMgr` | `0x02cc531c` | global `CAkRegistryMgr*` |
| `g_pRTPCMgr` | `0x02cc532c` | global `CAkRTPCMgr*` |

**`CAkRegisteredObj` is Wwise's own internal record for a registered game object** — the direct internal counterpart of the game-side `RegisterGameObj`/`UnregisterGameObj` boundary calls already in §1.2/§1.3 — and **`CAkRegistryMgr`/`CAkRTPCMgr` are real, CONFIRMED internal manager class names**, not guesses. A follow-up RTTI check (same method as §8.5/U-W4 below) found **zero hits** for all three as RTTI type descriptors (`CAkRegisteredObj`, `CAkRegistryMgr`, `CAkRTPCMgr` — consistent with the same negative result already found for the "named manager" candidates in §8.3 below: Wwise's internal manager/registry classes do not appear to carry MSVC RTTI at all, only the plugin/effect/codec classes do). These three names and five addresses are used as primary roots for U-W2 and U-W3 below, ahead of the weaker heuristic anchors in §8.3.

### 8.3 Three internal "named manager" anchors from a string scan — supplementary, weaker evidence

A direct string scan of `.rdata` (same method as the already-cited internal debug strings in the middleware census, `scratchpad/middleware_census_2026-10-07.md` §2: *"3 internal `AK::BankManager` / `AK::EventManager` / `AK::IOThread` debug strings"*) finds each string exactly once, and in each case the bytes immediately after the terminating NUL (4-byte aligned) are a short run of dwords that fall inside the Wwise `0x00f22000`–`0x00fbffff` block, interleaved with addresses this project already knows are linker-folded generic stubs — **CONFIRMED by direct byte read**:

| String | String VA | Trailing dwords (first few, Wwise-range only) | Already-known folded stubs re-sighted here |
|---|---|---|---|
| `AK::BankManager` | `0x0127bf94` | `0x00f5af70, 0x00f5ab00, 0x00f5acb0, 0x00f5afe0, 0x00f5a900, 0x00f5aaf0` | `0x006b89a0` (§1.1's folded `AK::Monitor` stub), `0x00ea2778` ×6 (§1.5's "pure-call" stub) |
| `AK::EventManager` | `0x0127ca40` | `0x00f768c0, 0x00f76930, 0x00f76960, 0x00f76db0, 0x00f76d70, 0x00f77040, 0x00f77140, 0x00f5b340, 0x00f52bb0` | `0x006b89a0`, `0x0066ac20` (§1.1's other folded `AK::Monitor` stub) |
| `AK::IOThread` | `0x0127e8d0` | `0x00fa0140, 0x00f328e0, 0x00fa0550, 0x00fa06a0, 0x00fa0420, 0x00f95900, 0x00fa04b0, 0x00fa07b0, 0x00fa0700` | none observed |

The `AK::BankManager` and `AK::EventManager` tables share several identical values at different slot positions (`0x00f5ab00`, `0x00f5acb0`, `0x00f5afe0` appear in both), which is the same shape as a shared base-class interface with a few linker-folded default methods — consistent with, but not proof of, these being real C++ vtables. **A targeted RTTI check (same Complete-Object-Locator/TypeDescriptor walk used successfully for the Vorbis classes in U-W4 below) found *zero* hits for every plausible real class name tried: `CAkBankMgr`, `CAkEventMgr`, `CAkAudioMgr`, `CAkLEngine`, `CAkStreamMgr`, `CAkIOThread`, `CAkFilePackageLowLevelIOBlocking`.** So: the three strings quoted above (`AK::BankManager`, `AK::EventManager`, `AK::IOThread`) are the only class-identifying text CONFIRMED present for these three internal managers — no other name is asserted, invented, or carried over from any outside source. Whether the trailing dwords are a true vtable, a separate internal named-object/profiling registration table, or something else is **HYPOTHESIS**, not resolved by this pass; they are used below only as empirically-reachable Wwise-internal code addresses, which is independently true regardless of what the table itself is — **treat this section's addresses as supplementary to §8.2's export-confirmed classes, not as strong on their own.**

> **➡ PARTIALLY RESOLVED by U-W2a, 2026-10-07** (`scratchpad/uw2a_event_action_walk_2026-10-07.md` §4.1): for the `AK::EventManager` row specifically, a direct read of the 12 dwords starting at VA `0x0127ca54` (4 bytes before the first listed trailing dword's own address, i.e. the table itself, not just its contents) **CONFIRMS it is a real, contiguous pair of MSVC vtables** (dtor/AddRef/Release slot convention at each half, constructors located and read). It is **not** the `AK::EventManager` class's own vtable, though — the located constructor (`0x00f76a40`) registers new instances into `G1+0x320` (a separate, Action-sized/Action-shaped 193-bucket registry, independently confirmed by that unit), not `G1+0x640` (the real Event registry, confirmed by `PostEvent`'s own literal event-id argument reaching it directly). So the string's own trailing-dword table is real vtable data, but most likely belongs to Wwise's **Action**-object class family, reachable from an Event only indirectly — not confirmed this pass to be "EventManager" itself. The `AK::BankManager` row's nature (shares 3 of the same slot values: `0x00f5ab00`/`0x00f5acb0`/`0x00f5afe0`) was not independently re-checked by U-W2a and remains as this section originally left it.

### 8.4 Unit-by-unit scoping, in the user's stated order

#### U-W1 — Bank/HIRC load (from `LoadBank` inward)

- **Game-facing wrapper closure:** 9 functions / 282 instructions (CONFIRMED by disassembly), rooted at `0x00f4bb90`/`0x00f4bc40`, bottoming out at `0x00f57710` (44 insns, 3 indirect calls) — the likely bank-job submission point.
- **`AK::BankManager`-table closure (§8.3, supplementary):** 17 functions / 539 instructions (CONFIRMED by disassembly, genuinely exhausted at depth 3–4, not capped).
- One member of that closure, `0x00f5acb0` (65 instructions, 7 indirect calls — the highest indirect-call density found anywhere in this pass), is a reference-counted teardown routine, read directly from the listing: lock (`call` through a global function-pointer slot `0x101c1c4`) → decrement a refcount field at object `+0x4` → on last reference, call a virtual method at **vtable offset `+0x60`** on two sub-objects (`esi+0x14`, `esi+0x18`) → call **vtable slot 0** (the object's own destructor slot) on the object itself → call the shared helper `0x00f32e70` → unlock (`call dword ptr [0x101c1c8]`). This is a plausible "release one loaded bank's object graph" routine — the natural mirror of "build the object graph," which is what `LoadBank` must do. CONFIRMED by listing; the *role* ("teardown of a bank's HIRC object graph") is HIGH, not proven by cross-reference to a load-time counterpart.
- **Interpretation:** the up-to-7 indirect calls inside one small function, concentrated right where bank teardown happens, are the signature of a per-HIRC-object-kind dispatch (a factory/table indexed by node-kind id) that a register-only disassembly cannot enumerate. Resolving it — by an RTTI vtable walk over whichever classes implement the relevant node-kind interface (same technique as U-W4 below), or by locating the literal kind-id switch/jump table in `.rdata` — is the real first step of this unit, not something this scoping pass can shortcut.
- **⚠→✅ CORRECTED 2026-10-07 (same-day follow-up pass dispatched on this exact question):** this interpretation is refuted. `0x00f5acb0`'s 7 indirect calls are NOT a kind dispatch — read in full, they resolve to 2 critical-section lock calls + 1 unlock (3 of the 7) wrapping an ordinary refcounted release (decrement `+0x4`, on last reference remove the object's id from a 193-bucket hash table at `[0x2cc5310]+0x18`), plus 2 ordinary C++ virtual calls at vtable offset `+0x60` on two sub-objects and 1 virtual call on the object's own vtable slot 0 (destructor-shaped, called with a "don't free" flag), plus a final unlock. All three "virtual" calls are standard polymorphism already resolved at each object's own construction — this function is generic and is confirmed (by a 22-hit literal `.rdata` scan) to be shared verbatim across at least 22 different classes' vtables. The shared helper `0x00f32e70` is memory/allocation-category bookkeeping (a `0x4c`-stride capacity-bounded table at `[0x2cc5288]`), not kind-related either. **The real per-HIRC-kind dispatch is a separate function, `0x00f588c0`** — a dense 19-entry MSVC jump table (VA `0x00f58b50`) keyed directly by the HIRC item's kind byte, reached from the bank's top-level chunk reader's `HIRC`-tag case (itself found via the literal `HIRC` FourCC `0x43524948`), not from anything in this unit's `AK::BankManager`-string-adjacent closure. Kinds 10–13 share one fallback slot, matching A7's "no Music Segment/Track/Switch/Playlist" finding structurally. Full writeup, confidence-labelled: `scratchpad/u1_wwise_hirc_dispatch_section.md` + `scratchpad/u1_wwise_hirc_dispatch_open_items.md` (this session's scratchpad; not yet integrated into `spec-audio-runtime.md` as of this correction). Also note: neither `0x00f588c0` nor the chunk-reader function that calls it has any discoverable direct call or static literal reference anywhere in the binary (checked three independent ways) — both are reached only through a genuinely runtime-computed indirect call, still unresolved; see that writeup's own open items.
- **Size estimate:** ~800–900 instructions / ~26 functions CONFIRMED reachable by direct calls; realistically a larger unit once the per-kind construction fan-out is included. **Propose scoping it as one U2-sized unit, ~2–3k instructions, generously** — don't commit to the small confirmed number.
- **Node-kind naming (for whoever specs this):** the 12 live HIRC kinds are a scoping input from A7 (`HANDOFF.md` §27.H-2), which names only Action's 11 verbs explicitly and says "no Music Segment/Track/Switch/Playlist, no Environment, no Modulators." **A7's own raw histogram file was not present anywhere in this project's shared `scratchpad/` at the time of this pass** (only the `HANDOFF.md` summary was readable) — re-pull or re-run A7 before naming the other ~8 kinds precisely; any name used for them beyond Event/Action should cite the public Wwise SDK/`wwiser` taxonomy explicitly as the source (public-docs naming, not code derivation), per the task's own instruction.

#### U-W2 — Event → action → container → voice (from `PostEvent` inward)

- **Game-facing wrapper closure:** 23 functions / 738 instructions (CONFIRMED), rooted at `0x00f4eb90`/`0x00f4e320`/`0x00f4c130`/`0x00f4e3e0`.
- **`AK::EventManager`-table closure (§8.3, supplementary):** 82 functions / 3,128 instructions (CONFIRMED by disassembly, genuinely exhausted at depth 6, not capped). The two closures overlap at several shared helpers (`0x00f32e70`, `0x00f32da0`, `0x00f333e0`, `0x00f335d0`, `0x00f330c0`, `0x00f33140`, `0x00f579f0`), i.e. the public-API route and the manager-table route independently converge on the same underlying code — a useful cross-check that the table in §8.3 is a real part of the live call graph, whatever its exact nature.
- **`CAkRegisteredObj`/`CAkRegistryMgr` (§8.2, export-confirmed) — the per-game-object record that voice instantiation operates on:** `CAkRegistryMgr::GetObjAndAddref` (`0x00f6e550`) is a tiny 23-instruction lookup+addref with no further calls (CONFIRMED, trivial). `~CAkRegisteredObj` (`0x00f555e0`)'s own closure is **21 functions / 909 instructions (CONFIRMED, genuinely exhausted at depth 4)**, and reaches `0x00f70fa0` (147 instructions, 10 callees) — a plausible "unregister this object from the RTPC/switch-value maps on teardown" routine, given it sits in the same address neighborhood as `CAkRTPCMgr::SetRTPCInternal` below and is only reached from the destructor path. This is the concrete internal-class anchor for "voice instantiation" the task asked for, independent of the weaker `AK::EventManager`-table evidence.
- **A concrete container/action-dispatch finding, read directly from the listing:** `0x00f52320` (203 instructions, 9 indirect calls) reads a flags field, calls a virtual method at **vtable offset `+0x7c`** on a `this`-like object, then branches on whether a masked field equals the literal `0x80000` — taking either a direct call to `0x00f51cc0` (119 insns) or a second virtual call at **vtable offset `+0x10`**, and elsewhere in the same function a third virtual call at **vtable offset `+0x80`**. This is the concrete shape the task asked for: a function that does not know in advance which kind of HIRC node it is holding, and dispatches by testing a type/flags field and then calling through a small set of fixed vtable slots. CONFIRMED by listing that this pattern exists; HIGH (not CONFIRMED) that it is specifically the container-traversal or action-verb-dispatch step, since no decompiling was done to recover which concrete classes sit behind those vtable slots. The 9-way indirect fan-out is also the right order of magnitude to match A7's "Action breaks into 11 verbs" data point, for whichever of the five listed verbs funnel through this function.
- **Size estimate:** combined, de-duplicated closure (public-API wrappers + `EventManager` table + the `CAkRegisteredObj` destructor cluster) is on the order of **4–4.5k instructions / ~115 functions CONFIRMED reachable by direct calls** — already a full-sized unit on its own. **Recommend splitting at the natural seam:**
  - **U-W2a — event lookup + action-list walk**: the public-API wrappers plus the `EventManager` table's own top-level dispatch (`0x00f768c0`…`0x00f77140`, `0x00f5b340`, `0x00f52bb0` and their shallow callees) — **≈1.5k instructions**.
  - **U-W2b — container traversal + voice instantiation**: the deeper polymorphic-dispatch cluster (`0x00f52320`, `0x00f51cc0`, `0x00f51bd0`, `0x00f51eb0`, `0x00f52070`, `0x00f53c40`, `0x00f4fc30`, `0x00f4fd80`, `0x00f76580`, `0x00f76710`, `0x00f75a30`) plus the `CAkRegisteredObj`/`CAkRegistryMgr` cluster (`0x00f6e550`, `0x00f555e0` and its 20 callees) — **≈2.5–3k instructions**.
  Both fit the project's usual 0.6k–5k bracket individually; the combined 4–4.5k does not need to be forced into one unit.
- **OPEN for whoever picks this up:** resolve the three vtable offsets (`+0x7c`, `+0x10`, `+0x80`) against whichever classes implement them — the same RTTI-walk technique that worked for the Vorbis classes (U-W4 below) should be tried first on this address range before falling back to anything heavier; not attempted in this pass (scope question, not a finding).

#### U-W3 — RTPC/switch/state (from `SetRTPCValue`/`SetSwitch`/`SetState` inward)

- **Primary root (§8.2, export-confirmed): `CAkRTPCMgr::SetRTPCInternal`, `0x00f70b10`.** This is the real internal RTPC-application entry point, named directly by the export table, not a heuristic guess. Its closure is **28 functions / 1,423 instructions (CONFIRMED by disassembly, genuinely exhausted at depth 4, not capped)** — substantially bigger and better-anchored than the public-API wrapper closure below, and the right root to actually scope this unit from.
- **Game-facing wrapper closure (`SetRTPCValue`/`SetSwitch`/`SetState`, all three id overloads):** 16 functions / 454 instructions (CONFIRMED), genuinely exhausted at depth 6 (not capped) — this is the thin public-API shim layer sitting in front of `CAkRTPCMgr`; it overlaps `CAkRTPCMgr::SetRTPCInternal`'s own closure at a handful of shared helpers (`0x00f32da0`, `0x00f32e70`, `0x00f330c0`/`0x00f33140`/`0x00f333e0`/`0x00f335d0`).
- **No equivalent exported "Internal" entry point was found for Switch or State** (only RTPC's got one) — `CAkRTPCMgr::SetRTPCInternal`'s own closure may still cover switch/state-adjacent logic (not confirmed either way; a function named "RTPC" manager could plausibly also own switch/state application in Wwise's real class design, or there could be a separate, un-exported `CAkSwitchMgr`/un-exported state-handling class — genuinely open, flagged rather than guessed).
- A handful of indirect calls (inside `0x00f6ff40`: 3, and single indirect calls at `0x00f7e380`/`0x00f4f130`/`0x00f7e250`) mark where this pass's reach stopped; not expanded further.

> **✅ RESOLVED 2026-10-07 (U-W3 dispatch), both halves of the "genuinely open" question above — found by call-graph tracing, not guessed.** Full writeup (addresses, listings, confidence labels): scratchpad `uw3_findings.md` + `uw3_open_items.md` (this session's scratchpad; not yet folded into `spec-audio-runtime.md`). Headline, both CONFIRMED: **(1) Switch *is* folded into `CAkRTPCMgr`** — an un-exported sibling method, `0x00f6f810` (`CAkRTPCMgr::SetSwitchInternal`, name HYPOTHESIS), on the *same* `g_pRTPCMgr` singleton, using a *second* 193-bucket hash table at `CAkRTPCMgr+0x308` (keyed by `(resolved object, SwitchGroupID)`) plus a notify list at `+0x91c` — but this path is pure bookkeeping-and-notify, it does **not** itself evaluate any curve or write any property (the actual consequence of a switch change — which container child plays — is inferred to be a lazy pull at decision time, not independently confirmed; flagged in open items). **(2) State is a genuinely separate manager**, not `CAkRTPCMgr` — reached via its own global singleton `0x02cc5318` and apply routine `0x00f547a0` (403 instructions, the largest function read this pass), which resolves old/new value-sets and ramps-or-sets up to 4 numeric properties through yet another dedicated singleton, `0x02cc5324` (a transition/ramp manager with no RTPC equivalent — RTPC curves are stateless, evaluated fresh on every call). Both findings independently corroborated a second way, from HIRC bank-load-time binding-record code (vtables `0x0127da58`/`0x0127da80`) feeding the identical two internal entry points. The command-queue plumbing that both the public `SetSwitch`/`SetState`/`SetRTPCValue` wrappers and these internal appliers share (tag word 2=RTPC/3=State/4=Switch, dispatcher jump table at `0x00f6a8c4`) is also now fully traced. 9 open refinement items remain (none blocking the headline finding) — see `uw3_open_items.md`.
- **Size estimate:** ~1.4–1.9k instructions CONFIRMED-reachable (the `CAkRTPCMgr` closure plus the non-overlapping part of the wrapper closure). **Propose a single U1/U2-sized unit, ~1.5–2k**, using `0x00f70b10` as the primary root rather than the public API wrappers.

#### U-W4 — Vorbis-variant decode (codec `4`, `CAkSrcBankVorbis`/`CAkSrcFileVorbis`, already named in §1.1)

Both classes' vtables were located directly by the standard MSVC RTTI walk (Complete Object Locator → TypeDescriptor → mangled name string `.?AV<Class>@@`; the same technique §1.1 already used to find the 31 Wwise-family RTTI classes) — **CONFIRMED by direct byte read**:

- `CAkSrcBankVorbis` vtable at VA `0x0127e954`.
- `CAkSrcFileVorbis` vtable at VA `0x0127e99c`.
- ~~The two tables run into each other in a raw consecutive dump (no sentinel marks a vtable's true end), so exact slot counts are **HIGH, not exact-CONFIRMED**: `CAkSrcBankVorbis` ≈17 slots, `CAkSrcFileVorbis` ≈18–20 slots. They share several identical slot values at the same slot index — two of them are the already-known folded 2-instruction game stub `0x006b89a0` and a further shared stub `0x00754410` — consistent with (not proof of) both classes implementing one common base interface (plausibly `IAkSoftwareCodec`, the name already used in §1.1) with a few trivial/default methods linker-folded identically.~~ **CORRECTED (U-W4a pass):** a direct RTTI ClassHierarchyDescriptor/BaseClassArray walk (not a raw consecutive-dump guess) gives exact slot counts and the real base chain, both CONFIRMED: `CAkSrcBankVorbis` has exactly **17 slots** (0–16), chain `CAkSrcBankVorbis : CAkSrcBaseEx : CAkVPLSrcNode : IAkSoftwareCodec : CAkVPLNode`; `CAkSrcFileVorbis` has exactly **20 slots** (0–19), chain `CAkSrcFileVorbis : CAkSrcFileBase : CAkSrcBaseEx : CAkVPLSrcNode : IAkSoftwareCodec : CAkVPLNode` — `IAkSoftwareCodec` is a confirmed real ancestor for both, not a guess. The two folded stubs (`0x006b89a0`, `0x00754410`) are correctly identified; indices 1, 6, 7, 13, 14 are also identical across both (6 is a real 3-instruction trampoline, not a stub). Full vtable-slot table and per-slot role notes: `scratchpad/uw4a_findings.md` §1–§2.

**Proposed split (U-W4's combined closure is ~7.4k instructions, well above this project's usual 5k ceiling — do not take it as one unit):**

- **U-W4a — per-class wrapper methods**: the shallow vtable-method bodies and their non-decode-core glue. **56 functions / ≈4,270 instructions** (CONFIRMED by disassembly: the `CAkSrcFileVorbis`-vtable-rooted closure of 93 functions/7,393 instructions, minus the isolated decode-core subtree below). At the top of the usual bracket; a real spec pass may be able to trim it further by separating genuinely per-class logic from shared buffer-management helpers it also reaches.
- ~~**U-W4b — shared Vorbis bitstream/variant decode core**: rooted at `0x00f8fa70` — **a single 1,809-instruction function, by far the largest function found anywhere in this pass** (25 indirect calls, 29 direct callees). **CONFIRMED reached two independent ways**: as the direct tail-call target of a `CAkSrcBankVorbis` vtable slot (`0x00fa3170`), and as a regular (non-tail) callee from inside `CAkSrcFileVorbis`'s own decode-path methods (`0x00fa3e90`, `0x00fa3980`) — i.e. **CONFIRMED shared between both source classes**, not duplicated per class. Its own closure (itself plus its 29 direct callees, recursively) is **37 functions / 3,125 instructions (CONFIRMED by disassembly)** — a clean, self-contained unit, and the right root for the real "is this stock Vorbis or a modified container" question.**~~
  > **⚠→✅ CORRECTED 2026-10-07 (U-W4b re-investigation):** this entry is wrong. `0x00f8fa70` disassembles to exactly **2 instructions** (`add ecx,0x10` / `jmp 0x00f90c10`), immediately followed by `int3` alignment padding — an ordinary small this-adjustor thunk, not a 1,809-instruction function. The "1,809 instructions / 25 indirect calls" figure was reproduced exactly from this same address using this note's own §8.0 heuristic, and the mechanism of the error is now understood: the function's own 2nd instruction is an unconditional `jmp` landing almost at the far end of a ~4.6KB run of `.text` that is actually ~40 separate, individually 16-byte-aligned, `int3`-padded real functions — the heuristic's "furthest forward branch" tracker got pushed out by that one early jump and then walked straight through ~40 real functions' own internal `ret`s without stopping. **A strict call/jmp-graph BFS (following only verified direct edges, not address-range proximity) shows `0x00f8fa70`'s real closure is 9 functions / 231 instructions, 0 indirect calls, and it is a virtual-destructor teardown helper** (frees a dynamically-allocated array of 12-byte records via Wwise's generic pool-free primitive `0x00f32e70`, itself independently cross-confirmed against this same note's own §8.4 U-W1 finding at `0x00f5acb0`). An exhaustive whole-`.text` scan for real `call`/`jmp rel32` instructions targeting `0x00f8fa70` found only 6 real sites; the cited `0x00fa3170` tail-jump is confirmed and, read in full, is itself a destructor cascade (frees sub-objects at `this+0x34`, a second vtable slot's cleanup, `this+0xcc`, then this thunk's `this+0x10` array) — **not a decode path**. The other citation (`0x00fa3e90`/`0x00fa3980` calling this directly) does not hold up: neither contains a direct call to `0x00f8fa70` per the same exhaustive scan. **The real Vorbis decode core was NOT found at this root** — finding it requires BFS-rooting the *other* vtable slots of `CAkSrcBankVorbis`(`0x0127e954`)/`CAkSrcFileVorbis`(`0x0127e99c`) individually, not yet attempted. One genuinely on-topic, CONFIRMED-by-disassembly finding did come out of this re-investigation, physically adjacent but NOT in this closure: `0x00f8fcb0` (called from `0x00fa3980`, i.e. real U-W4a territory) is a RIFF/WAVE chunk-walker recognizing `"RIFF"`/`"WAVE"`/`"fmt "`/`"cue "`/`"LIST"`/`"labl"`/`"smpl"`/`"data"` and a Wwise-proprietary **`"vorb"`** chunk — direct-from-the-binary confirmation that this build's Vorbis variant is RIFF/WAVE-wrapped rather than Ogg-paged. Full evidence trail, scripts, and a ~40-function sibling-family disassembly (also physically adjacent, also NOT confirmed to be in any particular closure) are in Team A's scratchpad (`w4b_findings.md`/`w4b_open_items.md`, this pass's session). **U-W4b as scoped here should be considered UNSOLVED, not done** — re-open rather than build a spec from the numbers above.
  > **⚠→✅ RESOLVED 2026-10-07 (U-W4b decode-trigger follow-up, same day):** the real decode-trigger slot the paragraph above calls for is **found: vtable slot 10 on both classes** (`0x00fa3000` Bank / `0x00fa3e90` File — the same slot `spec-audio-runtime.md` §11.2 had already characterized as "buffer-capacity/lookahead management," which turns out to be only its first half). Both classes' slot 10 call a **shared worker, `0x00fa49e0`**, passing it the per-instance decode-state block (`this+0x24` Bank / `this+0x54` File) plus the output-buffer pointer and a requested-sample count; an exhaustive whole-`.text` `E8`/`E9`-target scan (same method as the thunk re-check just above) finds **exactly 3 static call sites to `0x00fa49e0` anywhere in the binary, all three inside these two slot-10 bodies** — i.e. this is the only vtable-reachable path to it. `0x00fa49e0` runs a genuine per-call decode loop (decode a packet, copy samples out, advance an output pointer by `samples×2` bytes, repeat while the requested count is unmet), bottoming out in a per-channel copy loop (`0x00fa5970`) that writes decoded values directly into the caller-supplied buffer, and in real floating-point work (`0x00fa59a0`: `movss`/`mulss`/`subss`/`cvttss2si`) further down that same call tree. Back in slot 10 itself, the decoded sample count (read from the decode-state block slot 10 just passed in) is used to advance the sample-accurate position field `this+0xdc`, and is written into the caller's output-state struct alongside the buffer pointer — independently corroborated by `ReleaseBuffer` (slot 2, `0x00fa2d20`) freeing that exact same buffer-pointer field and clearing that exact same decode-state count field. Full writeup: `scratchpad/uw4b_decode_core_2026-10-07.md`; open items (the decode-state field layout, and the unidentified deeper Vorbis-bitstream/IMDCT-shaped functions this calls into): `scratchpad/uw4b_decode_core_open_items_2026-10-07.md`.
  > **⚠→✅ OPENED 2026-10-07 (separate pass, same day, scoped to the Wwise-specific wrapping layer only — packet framing, header rebuild, codebook storage, seek/loop metadata):** the `"vorb"` chunk this entry first flagged (line above) has now been opened directly — its 42-byte payload (`0x00f8fcb0`'s own `rep movsd`×10 + `movsw`, read in full) is copied RAW into the per-instance decode-state block at a fixed relative offset (`+0x7c` on both classes); its own first dword feeds a loop-end fallback (`vorb.sampleCount-1`) when no WAV `"smpl"` loop chunk is present; two of its bytes (chunk-relative offsets 4/5) are validated as Vorbis `blocksize_0`/`blocksize_1` exponents against the exact public Vorbis I legality bounds; and a further vorb-derived dword (relative decode-state `+0x9c`) is used as a hash key into a shared, reference-counted, hash-bucketed runtime cache (global `0x02cc5f40` — the SAME global this entry's own `0x00fa4bb0` citation already named on the teardown side) that holds the parsed Vorbis mode/mapping/codebook-setup structure, built fresh from each stream's own small stripped setup packet (same 16-bit size-prefix framing as steady-state audio packets) on a cache miss, or simply refcount-bumped and reused on a hit — confirmed for `CAkSrcBankVorbis` end to end; **not independently confirmed for `CAkSrcFileVorbis`**, whose own analogous call (`0x00fa3930`) only performs the lookahead-buffer-growth half, not the cache-resolve half (open item). No large static/shared codebook table was found baked into the executable anywhere in this closure (exhaustive negative, ~1,280 instructions checked for any `0x01xxxxxx`/`0x02xxxxxx`-range literal). Full writeup: `scratchpad/wwise_vorbis_wrapper_2026-10-07.md`; open items: `scratchpad/wwise_vorbis_wrapper_open_items_2026-10-07.md`.
- **Divergence from stock Ogg Vorbis:** **not established by this pass** — that needs real decode-level comparison against the public Vorbis I spec, out of scope for a scoping note, and the task explicitly says not to guess here. The only lead worth recording: public, non-Audiokinetic modding-tool documentation (e.g. the open-source `ww2ogg`/`revorb` utilities' own published descriptions of Wwise's header-stripped, externally-keyed-codebook Vorbis variant) is genuinely public information, not leaked Audiokinetic material, and would be the right starting reference for that comparison — cited here **only as a pointer, with its provenance explicitly flagged as "public tool documentation, not verified against this binary in this pass."** Nothing about the actual divergence is asserted.
- **Combined U-W4 size:** ~7.4k instructions / 93 functions total CONFIRMED-reachable by direct calls from the two vtables. **Recommend the U-W4a/U-W4b split above rather than one unit.**

#### U-W5 — Streaming/IO thread (`AK::StreamMgr`/IO-hook vtable path)

- This is the slice HANDOFF already calls "already identified" — correctly. The game-side half (the two abstract vtables `0x0129f13c`/`0x0129f14c`, their resolver/hook implementations, and the 25 calling functions) is **fully done already, in §1.5 of this note; nothing new needed there.**
- **New this pass, Wwise-internal side:** `StreamMgr::Create`/`CreateDevice`/`GetFileLocationResolver`/`SetFileLocationResolver` direct closure is **13 functions / 226 instructions (CONFIRMED, genuinely exhausted)**. The third named-manager anchor, `AK::IOThread` (§8.3), adds 9 further candidates, **176 instructions total, all leaf-like (no further calls)** — these read like small accessors (buffer-size/priority getters, by shape), not the thread's own scheduling loop.
- ~~**OPEN:** the actual per-block I/O scheduling loop (the real device/thread body that reads through the hook and hands buffers onward) was **not** located by any of this pass's roots — flag this as the first concrete task for the real U-W5 unit, not solved here.~~ **RESOLVED 2026-10-07, dedicated follow-up pass.** The loop is not Wwise-internal at all — it was never going to be found by searching `0x00f22000`–`0x00fbffff`, because the device's scheduler type is forced to **2** (deferred/lined-up), the mode where Wwise's own `StreamMgr` does not run its own scheduling loop; the low-level I/O *client* (the game) does. It is the already-named-but-unread **"Audiolib Stream Manager" thread** itself: outer sleep/dispatch loop `0x0046b820` (12 instructions, ~20 ms poll), calling the real per-tick body `0x0046c040` (lock the hook object's `0x0323e5f4` → poll the 3-entry medium table at `0x0323d8f8` → pump the two 16 KiB streaming buffers through three newly-identified game-supplied callback pointers at `0x0317289c`/`0x031728a4`/`0x031728a8` (three of the four audio-config fields `spec-audio-runtime.md` §3.1 already lists as mandatory but previously unexplained) → drain a 32-byte-stride pending-request list on a previously-uncatalogued embedded "scheduler" sub-object at hook `+0x8180` (`0x0323da70`) → post completions through the exact same `0x00471ae0`/status-10 path §1.5/§3.6 already document → unlock. Full mechanism, object-layout extensions, and confidence labels: `scratchpad/audio_iothread_scheduling_loop_2026-10-07.md`; remaining loose ends (a fourth unidentified config callback, the caller that supplies all four, a resident-package fast-path cache not fully traced): `scratchpad/audio_iothread_open_items_2026-10-07.md`. Neither `spec-audio-runtime.md` nor this note's §1–§7 were edited by that pass (file-collision discipline — scratch-only); fold-in is still pending.
- **Size estimate:** ~~400 instructions / ~20 functions CONFIRMED-reachable so far (the prior figure).~~ The resolved scheduling loop itself adds roughly another **9 functions / ~550 instructions CONFIRMED-reachable** (the two loop functions plus the buffer-pump, medium-table-tick, per-node dispatch, per-device-entry worker, and the two node alloc/free helpers — see the scratchpad file for the exact list), on top of the ~400/~20 already counted here — **propose ~1–1.5k for the real U-W5 unit**, revised up slightly from the prior ~0.6–1.2k estimate now that the loop is in hand; the fourth config-callback identity and its caller (open item 1–2 in the companion file) are small enough not to need their own unit.

### 8.5 Proposed sub-unit summary

| Unit | Root(s) | CONFIRMED direct-call closure | Proposed unit size | Notes |
|---|---|---|---|---|
| U-W1 | `0x00f4bb90`/`0x00f4bc40` + `AK::BankManager` table (`0x00f5af70` etc.) | 26 fn / ~820 insn | ~2–3k (U2) | per-kind factory dispatch unresolved (indirect) |
| U-W2a | `0x00f4eb90` + `EventManager` table top level | subset of below | ~1.5k (U1/U2) | event lookup + action-list walk |
| U-W2b | `0x00f52320`/`0x00f51cc0` cluster + `CAkRegisteredObj`/`CAkRegistryMgr` | subset of below | ~2.5–3k (U2) | container traversal + voice instantiation, now with export-confirmed class anchors |
| U-W2 (combined, if not split) | `0x00f4eb90`/`0x00f4e320`/`0x00f4c130`/`0x00f4e3e0` + `EventManager` table + `0x00f6e550`/`0x00f555e0` | ~115 fn / ~4–4.5k insn | split recommended | best-anchored unit in this pass |
| U-W3 | `CAkRTPCMgr::SetRTPCInternal` (`0x00f70b10`, export-confirmed) + `SetRTPCValue`/`SetSwitch`/`SetState` wrappers | 28+16 fn (overlapping) / ~1.4–1.9k insn | ~1.5–2k (U1/U2) | real internal root now identified, not just the public wrapper. ~~Switch/State internal path unresolved.~~ **RESOLVED 2026-10-07**: Switch is a sibling un-exported `CAkRTPCMgr` method (`0x00f6f810`, bookkeeping+notify only, no property application); State is a separate manager (singleton `0x02cc5318`, apply `0x00f547a0`, 403 insn, with its own transition-manager singleton `0x02cc5324` — no RTPC equivalent). Found by call-graph trace through the generic internal command queue (dispatcher jump table `0x00f6a8c4`), independently corroborated from HIRC bank-load binding-record code. Full writeup: `scratchpad/uw3_findings.md` + `uw3_open_items.md` (9 refinement items, none blocking). Add ~450 insn / 6 fn (CONFIRMED-read this pass: `0x00f6f810`, `0x00f542c0`, `0x00f547a0`, plus the dispatcher/queue plumbing) to this unit's real size — propose bumping to ~2–2.5k (still U2). |
| U-W4a | `CAkSrcFileVorbis`/`CAkSrcBankVorbis` vtable methods minus decode core | 56 fn / ~4.27k insn | ~4–4.5k (U3/U4) | per-class glue |
| U-W4b | ~~`0x00f8fa70` (shared decode core)~~ `0x00fa49e0` (real decode-trigger worker, called only from vtable slot 10 on both classes) | ~~37 fn / 3.125k insn~~ | ~~~3k (U3)~~ | **RETRACTED 2026-10-07**: `0x00f8fa70` is a 2-insn this-adjustor thunk feeding a virtual-destructor teardown cascade (real BFS-validated closure: 9 fn / 231 insn, 0 indirect calls), not the decode core — see corrected §8.4 entry + `scratchpad/w4b_findings.md`. **RESOLVED same day, decode-trigger follow-up**: the real trigger is vtable slot 10 (`0x00fa3000` Bank / `0x00fa3e90` File) → shared worker `0x00fa49e0` (exhaustively confirmed the only static caller); see §8.4's second resolved callout + `scratchpad/uw4b_decode_core_2026-10-07.md`. **RESOLVED 2026-10-08**: the deeper Vorbis-bitstream/IMDCT-shaped internals below `0x00fa49e0` are now structurally classified — bit-reader, packet framing/dispatch, spectral-decode orchestrator (floor→couple→residue→uncouple→floor-apply→IMDCT), the inverse MDCT itself (812 insn, structural match to the public split-radix FFT-based algorithm), and the windowed overlap-add/PCM writer. See `spec-audio-runtime.md` §11.9.6 and `scratchpad/havok_du2_wwise_uw4b_2026-10-08.md`. Floor/residue/floor-apply decode remain unopened in full (identified by call shape only). |
| U-W5 | `StreamMgr::Create`/`CreateDevice`/`...FileLocationResolver` + `AK::IOThread` table + the resolved game-side scheduling loop (`0x0046b820`/`0x0046c040`, §8.4) | 20 fn / ~400 insn (Wwise-internal) + 9 fn / ~550 insn (game-side loop, CONFIRMED 2026-10-07) | ~1–1.5k (U1) | scheduling loop RESOLVED — see `scratchpad/audio_iothread_scheduling_loop_2026-10-07.md` |

All sizes are HIGH confidence per the §8.0 method caveat (heuristic function-sizing, no ground truth), not CONFIRMED; the addresses and listed disassembly fragments they're built from are CONFIRMED, including the §8.2 export-table names (strongest) and the §8.3 string-table candidates (supplementary, weaker).

### 8.6 What this adds to / corrects in §1–§7 above

- **Extends §1.1's "31 Wwise-family RTTI classes"** with exact vtable VAs for the two Vorbis classes it already named but didn't locate: `CAkSrcBankVorbis` → `0x0127e954`, `CAkSrcFileVorbis` → `0x0127e99c`.
- **Adds three real internal class names this note's §1–§7 never had**, read directly from this project's own existing export-table dump (`tools\audio_wwise_exports.txt`): `CAkRegisteredObj` (the internal counterpart of the game-side `RegisterGameObj`/`UnregisterGameObj` calls in §1.2/§1.3), `CAkRegistryMgr`, `CAkRTPCMgr` — plus their global singleton pointers `g_pRegistryMgr` (`0x02cc531c`) and `g_pRTPCMgr` (`0x02cc532c`), in the same style as the already-documented `IAkStreamMgr::m_pStreamMgr` global in §1.3. None of these three classes carry RTTI (checked directly, zero hits), matching the same pattern already found for the §8.3 "named manager" candidates — Wwise's internal manager/registry classes appear not to use RTTI at all in this build, only the plugin/effect/codec classes do.
- **Extends §1.4/§1.5's folded-stub inventory**: `0x006b89a0`, `0x0066ac20` (§1.1's folded `AK::Monitor` stubs) and `0x00ea2778` (§1.5's "pure-call" stub) all reappear as apparent virtual-slot values inside the `AK::BankManager`/`AK::EventManager` candidate tables in §8.3 — i.e. these three generic linker-folded bodies are reused as internal Wwise vtable/table entries too, not only at the game↔Wwise boundary. Strengthens §1.4's framing that they are generic folded bodies with no meaningful behaviour of their own, wherever they appear.
- **Sharpens `HANDOFF.md` §27.H-2's framing of A7's "no Modulators" finding, as a HYPOTHESIS worth checking, not asserted here as fact:** Wwise's LFO/Envelope/Time **Modulator** object kind is publicly documented to have been added in a materially later Wwise SDK generation than the ~2010.x generation this binary links (per the API-shape evidence already in `scratchpad/middleware_census_2026-10-07.md` §2 and this note's own §1.1/§6). If so, "no Modulators" in the shipped banks would be an **expected consequence of the SDK generation**, not an independent content-usage finding — unlike "no Music Segment/Track/Switch/Playlist," since Interactive Music genuinely existed in 2010-era Wwise, so its absence is a real, separate finding. That absence is also consistent with this note's own §2.1 conclusion that the game's radio/music system is Volition's own code, not Wwise's Interactive Music Engine, even though `MusicEngine::Init`/`Term` are called at the boundary (§1.2). **This should be confirmed against public Wwise release notes (public-docs provenance) before a real spec states it as fact — flagged, not settled, here.**
- **Flags a gap, not a correction:** A7's own raw `wwiser` histogram output (named in `HANDOFF.md` §27.H-2) was not found anywhere in this project's shared `scratchpad/` as of this pass — only the `HANDOFF.md` summary was available to read. Re-pull or re-run it before committing to exact names for the 12 live HIRC node kinds in any real spec (only Event/Action and Action's 11 verbs are named with confidence right now).

### 8.7 Clean-room check (for this addition)

- Addresses are plain hex throughout; no Ghidra project was opened for this section (export-table reads, raw `pefile`/`capstone` disassembly, and a direct MSVC RTTI Complete-Object-Locator/TypeDescriptor byte-walk only); no decompiler output anywhere.
- Names quoted as data: Wwise API names and the `CAkRegisteredObj`/`CAkRegistryMgr`/`CAkRTPCMgr` internal class names from the PE export table (§1, §8.2 — this project's own pre-existing `tools\audio_wwise_exports.txt` dump, re-read, not re-derived); `CAkSrcBankVorbis`/`CAkSrcFileVorbis` from RTTI type-descriptor strings (as in §1.1); `AK::BankManager`/`AK::EventManager`/`AK::IOThread` from plain ASCII strings found directly in `.rdata` (§8.3) — no class name beyond these was invented; the negative RTTI results in §8.2/§8.3 are reported honestly rather than papered over with a guessed name.
- The project's standard regex was run on the appended text with `grep -cP`: **0 hits**.
- No spec file was edited; no `.czn_pc` file was touched; no leaked source of any kind (SR4/Re-Elected, Havok, or any other leaked Volition/Havok/Audiokinetic material) was read, consulted, or used anywhere in this pass. The scratch Python scripts used to produce the addresses/sizes above are temporary session files, not part of this project's committed tooling.
