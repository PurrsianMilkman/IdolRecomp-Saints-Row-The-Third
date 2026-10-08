# Saints Row: The Third — Audio Runtime: the Wwise Adapter Contract

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** Audio runtime, unit A0 of the proposed order in `review/scoping-audio.md` §5.
**Scope:** The Volition-authored adapter that sits between the game and the statically-linked Wwise
sound engine — the 25 functions that call into Wwise's public API, the two runtime record layouts those
calls operate on (the play record and the voice), init/shutdown order, the four memory hooks, the custom
I/O-hook/file-resolver object, the bank callbacks, and the full feedback (status-code) path back into
gameplay-visible state. This file is the **contract our side must honour** to replace the Wwise engine
underneath it without changing observable game behavior. It does not open Wwise's own internals, the
bank `HIRC` format, or anything inside the statically-linked Wwise code range — those stay out of scope,
same as `spec-audio-format.md`'s own boundary.
**Method:** direct Ghidra disassembly, headless, read-only, against a private disposable project copy
(deleted on completion), `func`/`ptrs`/`xref` dump modes. Every function in §1's tables was read in full
from its own listing. The dump tool's decompiler view was used only as a cross-check of the listing
reading and nothing from it is reproduced here.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear
anywhere below; no original Volition identifiers are used as identifiers — every function and field is
cited by its plain hex address or offset. Wwise API names are the binary's own PE export-table names
(Audiokinetic's public API, third-party and out of scope in themselves); the mapping of a pushed value
to a named parameter follows the public signature encoded in each export's mangled name (HIGH); named
enum values from the public SDK are HYPOTHESIS unless the code itself shows the meaning.
**Confidence key:** **CONFIRMED** (read directly in a listing this pass); **HIGH CONFIDENCE** (inferred
from structure, not independently read to the same depth); **HYPOTHESIS** (plausible, not checked).
**Relationship to other specs:** `spec-audio-format.md` documents the file-side boundary (the `.vpp_pc`
archive layout, the plain vs. `_media.bnk_pc` wrapper format, the third-party/in-scope split) — this
file documents the **runtime call contract** layered on top of that boundary. `spec-audio-format.md`
§6–§8 (the I/O hook and bank items) are refined directly by this file's §3. `review/scoping-audio.md`
is the scoping note this unit refines; corrections to it are given in §0.

---

## 0. Headline corrections to the scoping note

1. **There are two records, not one.** What the scoping note called "the voice record" fields `+0x04`
   cookie, `+0x08` event id, `+0x0c` playing id, `+0x14` seek, `+0x18` flags, `+0x1c`/`+0x1e` state
   bytes do **not** live in the `0xa8`-stride voice. They belong to a separate **32-byte "play
   request" record** (the *play record*), several of which hang off one voice in a singly-linked list
   at voice `+0x24`. The voice (stride `0xa8`) is the Wwise **game object**; the play record is one
   **posted event** on it. CONFIRMED: `0x0045ce70` takes the play record in `EAX` and indexes a
   per-record state array by `(record − pool base) >> 5`, and takes the voice as its stack argument,
   which it forwards as the game-object argument to `ExecuteActionOnEvent` and to both `PostEvent`
   wrappers; `0x0045d1e0` calls it once per node of the list at voice `+0x24` with the voice pushed
   (§2).
2. **The voice-line `PostEvent` does not post the play record's `+0x08` as the event id.** `+0x08` is
   first mapped through `0x00469100` to a pair (external-source cookie, play-event id); the second is
   the event id posted (§1.2, row `0x00470500`).
3. **Only the global RTPC path is cached.** `0x00470960` and `0x0045f2d0` consult the value cache
   `0x00470830` only when the target is the global object; per-object RTPCs bypass it (§1.3).
4. **The status codes come back to the play record, not the voice**, keyed by a handle (the cookie),
   through one queue drained by the "Audio callback thread" under the audio lock (§4). The same queue
   also carries bank completions (codes 0–3, keyed by **bank id**) and the I/O hook's read completions
   (code 10), so Wwise's stream manager is told about finished reads on the game's callback thread
   (§3.6).
5. **The per-voice tick and the frame tick (`RenderAudio`) run on every second pass of the Main Audio
   Thread**, about every 33 ms, not every pass (§4.5).

---

## 1. The 25 calling functions — exact arguments at every crossing

**Reading conventions for this section.**
- *Voice* = the `0xa8` record (§2.2), always the Wwise game object. *Play record* = the 32-byte record
  (§2.1). Register-passed arguments are named by register because the wrappers are not standard calls.
- The argument list is given in **Wwise parameter order** (first parameter first), not push order.
- "Guard" lists the early-outs before the Wwise call and the value returned. Every game-side error code
  is negative: `−0x12e` = sound engine not up (byte `0x031728c6` clear); `−0x64`/`−0x67`/`−0x65` =
  missing voice / missing play record or event / voice handle 0; `−0x63` = generic failure of the
  Wwise call; others per row.
- Enum names in brackets are the public SDK names for the pushed value (HYPOTHESIS for the name,
  CONFIRMED for the value). The SDK generation is pre-2012: the export table names
  `SetGameObjectEnvironmentsValues` (not the later aux-send name), and `AkInitSettings` has the assert
  hook as its first member (§3.2), both consistent with the 2011 SDK line.

### 1.1 Per-voice parameter and position path (audio thread, from the voice tick `0x0045d1e0`)

| Caller (insns) | Wwise API (site) | Arguments, in parameter order | Fields read / written | Guard → return |
|---|---|---|---|---|
| `0x00470610` (49) | `RegisterGameObj` (`0x00470699`) | (object = voice address, name = 128-byte stack buffer holding voice `+0x00` printed with `"%u"` from `0x0129fb94`). Only two arguments are pushed. | reads voice `+0x00` | engine down → `−0x12e`; voice null → `−0x64`; handle `+0x00` = 0 → `−0x65`; Wwise result ≠ 1 → `−0x132`; else 0. Called from voice create `0x0045da50` (synchronous registration) and from the tick when voice flag `0x40` is set (lazy registration; flag cleared on success, retried next tick on failure). |
| `0x0045cd80` (79) | `SetGameObjectEnvironmentsValues` (`0x0045ce56`) | (object = voice, array = 16 zeroed bytes on the stack, **count = 0**) — i.e. *clear all aux sends of this object* | none | only when its byte argument is non-zero, the engine is up and the voice is non-null. The tick passes global byte `0x03172884` and clears that byte after the pass over all voices (`0x0045d485`), so a set byte means "clear every voice's sends once". |
| `0x0045cd80` (cont.) | — (calls the wrappers below) | Body, when the voice's **update timer** `+0x1c` has expired: if `+0x9f` ≠ 0, recompute positions (`0x004685c0`, `0x004682d0`, game-side, not read), then `0x00470280` (position), `0x00470450` (obstruction); if `+0xa6` ≠ 0 and `0x0045c160` says the value changed, `0x00470960` with RTPC id **`0x85c3e987`** and value `+0x0c`, then copy `+0x0c` → `+0x10`. Then, if global byte **`0x03172883`** (aux sends enabled; copied from the init config, §3.1) is set, `0x004709b0`. Finally re-arm the timer: **33 ms** (`0x21`) if the voice has play records (`+0xa0` ≠ 0), **5000 ms** otherwise. | `+0x0c`, `+0x10`, `+0x1c`, `+0x9f`, `+0xa0`, `+0xa6` | — |
| `0x00470280` (159) | `SetPosition` (`0x004702ef`) | (object = voice in `EDI`, position = 6 floats copied from voice `+0x28`…`+0x3c` as (position x, y, z, orientation x, y, z), listener index = **`−1`** [all listeners]) | `+0x28`…`+0x3c`, `+0x9f` | engine down → `−0x12e`; voice null → `−0x64`. Used when `+0x9f` = 1. |
| `0x00470280` (cont.) | `SetMultiplePositions` (`0x00470432`) | (object = voice, array of `+0x9f` 24-byte records on the stack, count = `+0x9f` (zero-extended byte), type = **`2`** [`MultiDirections`]) | `+0x28` (first node), `+0x44` and each node's `+0x1c` (next), `+0x9f` | used when `+0x9f` ≥ 2; nothing is sent when it is 0. Two defects, see §5: the orientation copy is wrong, and there is no bound on the count (the stack array holds 16 records). |
| `0x00470450` (61) | `SetObjectObstructionAndOcclusion` (`0x004704de`) | (object = voice in `ESI`, listener = **0**, obstruction = `+0xa1` / 100.0, occlusion = **0.0**) | reads `+0xa2` (target), writes `+0xa1` (sent value) | engine down → `−0x12e`; voice null → `−0x64`; **no call at all when `+0xa1` = `+0xa2`**; Wwise result ≠ 1 → `−0x63`. The new `+0xa1` is the target clamped into `[max(cur − 7, 0), min(cur + 7, 100)]`: a game-side slew of at most 7 points (0.07) per update, i.e. per 33 ms while playing. The divisor is the double `100.0` at `0x012a2dd8`. |
| `0x00470960` (30) | `SetRTPCValue` (`0x00470997`) | (RTPC id in `ESI`, value = stack float, object = `EAX`, or **`−1`** when `EAX` = 0) | — | engine down → `−0x12e`; Wwise result ≠ 1 → `−0x140`. **Only the global form (`EAX` = 0) consults the value cache `0x00470830`**, and skips the call (returns 0) when the cache says "unchanged". The per-object form always calls. Callers: `0x0045cd80` (per voice, id `0x85c3e987`) and `0x0045ccd0`. |
| `0x004709b0` (137) | `SetGameObjectEnvironmentsValues` ×4 (`0x00470a7e`, `0x00470a95`, `0x00470b19`, `0x00470b30`) | (object = voice in `ECX`, array = up to 3 `{environment id, float level}` pairs in a zeroed 4-pair stack buffer, count = number of pairs whose level is **> 0.01**) — the four sites are the same call on four control-flow paths | `+0x98` bit 0; `+0x60`/`+0x64`, `+0x68`/`+0x6c`, `+0x70`/`+0x74` | engine down → `−0x12e`; voice null → `−0x64`. Slot 0 is the voice's own `+0x60`/`+0x64` when `+0x98` bit 0 is set, otherwise the **global** send `0x03172950`/`0x03172954`; slots 1 and 2 are `+0x68`/`+0x6c` and `+0x70`/`+0x74`. A slot is considered only when its id is non-zero; one whose level is ≤ 0.01 (float `0x012a2dd0`) is written but not counted, so the next slot overwrites it. The call is made on every positional update, changed or not. |
| `0x0045d1e0` (215) | `SetRTPCValue` (`0x0045d353`) | (RTPC id = node `+0x04`, value = node `+0x08` (float), object = voice) for each node of the voice's RTPC list (first node embedded at voice `+0x88`, next at node `+0x00`) whose dirty byte node `+0x0c` is set | voice `+0x88`…`+0x97` and the chained nodes; clears each node's dirty byte (even when the engine is down) and then voice flag `0x10` | only when voice flag `0x10` is set; per node, the call is skipped (but the dirty byte still cleared) when the engine is down; the result is ignored. No value cache. |
| `0x0045d1e0` (cont.) | `SetSwitch` (`0x0045d3a0`) | (switch group = node `+0x04`, switch state = node `+0x08`, object = voice) for each dirty node of the switch list (first node at voice `+0x78`) | voice `+0x78`…`+0x87` and chained nodes; clears dirty bytes and voice flag `0x20` | only when voice flag `0x20` is set. **After flushing switches the tick moves on to the next voice**, so the play records of that voice are not processed until the next tick (one-tick latency between a switch change and a post on the same voice). |
| `0x0045cc10` (73) | `UnregisterGameObj` (`0x0045cca5`) | (object = voice in `EAX`) | walks and frees every play record (`+0x24`, `+0xa0`): releases a stream slot (`+0x1c` ≠ `0xff` → `0x0046a700`), tells `0x00461210` the cookie (`+0x04`), returns the record to its pool (`0x00461000`). Reads `+0x00`. | voice null → `−0x64`. Unregisters only when the engine is up and `+0x00` ≠ 0. **No `StopPlayingID` is issued for records that are still posted** (§5). Then frees the voice (`0x0045cbc0`, `0x0045c680`, `0x0045c440`, `0x00461520(+0x00)`, `0x00460430`). Called from the tick (voice flag `0x8000`) and from voice create `0x0045da50`. |

### 1.2 Play-record state machine `0x0045ce70` and its wrappers (audio thread)

`0x0045ce70` takes the play record in `EAX` and the voice as its one stack argument. It reads the
record's state byte (§2.1) and request word `+0x18`, and calls exactly one Wwise-facing operation per
tick at most. The full transition table is reproduced in §2.3; only what decides each crossing is given
here.

| Caller (insns) | Wwise API (site) | Arguments, in parameter order | Fields | Guard → return / follow-up |
|---|---|---|---|---|
| `0x00470720` (36) | `PostEvent` (`0x0047074e`) — **sound-effect path** | (event = play `+0x08`, object = voice in `EDX`, flags = **`0x1000d`** [`EndOfEvent` 0x1 \| `Marker` 0x4 \| `Duration` 0x8 \| `EnableGetSourcePlayPosition` 0x10000], callback = **`0x004706c0`**, cookie = play `+0x04`, external-source count = 0, external-source array = null) | writes `+0x0c` (playing id) and `+0x1e` = 1; on a zero playing id, clears `+0x1e` again | engine down → `−0x12e`; record null or `+0x08` = 0 → `−0x67`. Zero playing id → status **8** with the cookie (§4) and `−0x137`; the state machine then moves the record to state 7. Chosen when play `+0x18` bit 24 (`0x01000000`) is clear and the record is in state 1 with request bit 0 set. |
| `0x00470500` (87) | `PostEvent` (`0x004705c4`) — **voice-line (external-source) path** | (event = **second dword returned by `0x00469100`**, object = voice (stack argument), flags = **`9`** [`EndOfEvent` \| `Duration`], callback = **`0x00469ea0`**, cookie = play `+0x04`, external-source count = **1**, array = one 24-byte record on the stack: {external-source cookie = first dword from `0x00469100`, codec = **4** [Vorbis], file name = null, in-memory pointer = null, in-memory size = 0, file id = result of `0x00468ae0`}) | reads play `+0x08` (the **line key**), voice `+0x14` and `+0x18` (persona keys); writes play `+0x0c`, `+0x1e` = 1 (**left at 1 on failure**), voice `+0xa3` += 1 on success | voice (stack) or record null → `−0x63`. **No engine-up test** (unlike every other wrapper). File id: `0x00468ae0(voice +0x14, line key)`, falling back to `0x00468ae0(voice +0x18, line key)`; both 0 → `−0x17`, nothing posted. `0x00469100(line key)` looks the line up (`0x004694c0`) and on a miss returns the defaults hashed at init from **`Default_Ext_Src_external_source`** (cookie, `0x03172874`) and **`voc_Default_Ext_Src_play`** (event, `0x03172878`) (CONFIRMED at `0x00462637`–`0x00462655`). Zero playing id → status **8**, `−0x137`. Chosen when play `+0x18` bit 24 is set. |
| `0x004707e0` (25) | `StopPlayingID` (`0x004707ff`) | (playing id = play `+0x0c`, fade = **0** ms, curve = **4** [Linear]) | reads `+0x0c`; then `+0x1e` = 0, `+0x0c` = 0 | engine down → `−0x12e`; record null or `+0x0c` = 0 → `−0x67`. The Wwise result is **ignored**. Always posts status **7** with the cookie afterwards. Used for request bit 3 (stop) in states 1 (only if `+0x1e` is set), 2, 3, 4; success → state 5, failure → state 6. |
| `0x0045ce70` (199) | `ExecuteActionOnEvent` (`0x0045d003`) — **pause** | (event = play `+0x08`, action = **1** [Pause], object = voice, fade = **10** ms, curve = **4** [Linear]) | reads `+0x08`, `+0x0c` | only for request bit 2 (pause) without bit 0, in state 2, with the engine up and `+0x0c` ≠ 0. Result = 1 → status **6** with the cookie, state 2 → 3. Note the action targets **the event on the object**, not the playing id: every instance of that event on that voice is paused. |
| `0x0045ce70` | `ExecuteActionOnEvent` (`0x0045d05d`) — **resume** | (event = play `+0x08`, action = **2** [Resume], object = voice, fade = **10** ms, curve = **4**) | reads `+0x08`, `+0x0c`; clears request bit 0 on success | only for request bit 0 in state 4, engine up, `+0x0c` ≠ 0. Result = 1 → status **4**. Same event-wide scope as pause. |
| `0x00470790` (30) | `SeekOnEvent` (`0x004707bc`) — millisecond overload | (event = play `+0x08`, object = voice in `ECX`, position = play `+0x14` (ms), seek-to-nearest-marker = **false**) | reads `+0x14`; on success writes `+0x14` = `−1` | engine down → `−0x12e`; voice or record null → `−0x67`; `+0x14` < 0 → `−0x64`; Wwise result ≠ 1 → `−0x13c` (request bit 1 stays set, so it is retried next tick). Only for request bit 1 in state 2; bit 1 is cleared on success. |

### 1.3 Global (non-object) calls

| Caller (insns) | Wwise API (site) | Arguments, in parameter order | Notes |
|---|---|---|---|
| `0x0045f2d0` (124) — the voice API's RTPC setter, 51+ callers | `SetRTPCValue` (`0x0045f43a`) | (RTPC id = arg 1, value = arg 2 (float), object = **`−1`**) | Signature (id, float value, voice handle, float arg 4). Guard: voice-API byte `0x031728c2` clear → `−1`. **Handle 0** → global: engine down → `−0x12e`; value cache `0x00470830` says unchanged → 0, no call; Wwise result ≠ 1 → `−0x140`. **Handle ≠ 0** → no Wwise call here: under the audio lock `0x031f0260` the handle is resolved (`0x0045c720`) and the value stored in the voice's RTPC list by `0x0045c230` (which also takes arg 4, HIGH: an interpolation time — not passed to Wwise anywhere read); if it changed, voice flag `0x10` is set and the tick sends it (§1.1). Unknown handle → `−0x64`. |
| `0x0045f690` (239) — frame tick, end of every Main Audio Thread pass | `SetListenerPosition` (`0x0045f733`) | (listener position = 9 floats on the stack: front = `0x031728f0`/`f4`/`f8`, top = `0x031728fc`/`0x03172900`/`04`, position = `0x031728e4`/`e8`/`ec` [the SDK's `AkListenerPosition` order front, top, position — HIGH], listener index = **0**) | Sent only while the counter **`0x03172908` > 0**, which is then decremented: a setter arms it for N frames rather than sending every frame. The counter is decremented even when the engine is down. |
| `0x0045f690` | `SetRTPCValue` (`0x0045f7bb`) | (id **`0xdb2d51a3`**, value = `0x031728e0` × 100.0, object = `−1`) | A game-side smoothed global. Target `0x031728dc`, current `0x031728e0` (both 1.0 at init). If current > target by more than **0.05** (double `0x012a2ee0`): current := (current − target) × `0x0317297c`. Otherwise, if different, current := target. Sent (through the cache) only on a change. The update formula is not "approach the target" unless the target is 0 (§5). |
| `0x0045f690` | `SetRTPCValue` ×3 (`0x0045f841`, `0x0045f8b9`, `0x0045f9b4`) | (id = `0x03171a70[i]`, value, object = `−1`) for **7 mix RTPCs**, `i` = 0…6 | Ids in `0x03171a70[7]`, filled lazily from the static table `0x013520ec[7]` when zero. Values `0x03172918[7]` (init: 0.8 ×6, 1.0 for the seventh), dirty bytes `0x0317290e[7]`. Mode byte **`0x031728c0`** (set to 1 at the end of init, cleared first thing at shutdown): when **0**, all seven are sent as **0.0** every tick (through the cache) and "sent" byte `0x0351f908` is cleared; when **1**, all seven are sent once with their values (`0x0351f908` := 1), then only dirty ones. Three call sites = the three branches. |
| `0x0045f690` | `SetRTPCValue` (`0x0045f91d`) | (id **`0x9e3669da`**, value = (float) of the integer `0x031728d8`, object = `−1`) | Sent when `0x031728d8` differs from its shadow `0x03521184`; the shadow is updated even when the engine is down. |
| `0x0045f690` | `RenderAudio` (`0x0045f945`, tail jump) | none | Last action of the frame tick, only when the engine is up. Whole tick skipped when byte `0x031728c5` or `0x031728c6` is clear, or when the counter `0x03171ac0` is odd (the main loop increments it twice per pass, `0x00470e4f`/`0x00470f9c`). |
| `0x00462970` (21) | `SetState` (`0x00462999`) | (state group = arg 1, state = arg 2) | Guards: `0x031728c2` clear → `−1`; engine down → `−0x12e`; result ≠ 1 → `−0x63`. Called from one site, `0x00a3c7b6` (inside the Lua audio bindings block, not in a defined function). Synchronous — runs on the caller's thread, no queue. |
| `0x0046fd00` (38) | `GetIDFromString` (wide, `0x0046fd61`) | (UTF-16 copy of the name, made by `0x0046fc50` into a 128-character stack buffer) | Null, empty or `"none"` (case-insensitive, string `0x0129b26c`) → **0** without calling Wwise. Already specified (FNV-1, scoping note §2.1). |

### 1.4 Banks, streaming, audio input

| Caller (insns) | Wwise API (site) | Arguments, in parameter order | Notes |
|---|---|---|---|
| `0x00465cc0` (374) — "Soundbank Process" task | `LoadBank` (`0x00465e34`) — asynchronous id overload | (bank id = bank record `+0x40`, callback = **`0x00470220`**, cookie = bank record address, memory pool = bank record `+0x50`) | Only when byte `0x031728c5` is set; otherwise (and when the result ≠ 1) the record gets error bit 1 of `+0x5b`. Either way the record moves to bank state 3 (`0x004673c0`). Bank record = the `0x5c`-stride record of `spec-audio-format.md` §7.5. |
| `0x00465cc0` | `UnloadBank` (`0x00466072`, `0x0046615c`) — asynchronous id overload | (bank id = `+0x40`, callback = **`0x00470250`**, cookie = bank record) | Two sites in two list passes. Skipped when `0x031728c5` is clear; the record moves to bank state 2 regardless. The result is not checked. |
| `0x00466850` (63) — bank-system init | `SetBankLoadIOSettings` (`0x0046687b`) | (throughput = float at `0x01295104`, priority = **100** (`0x64`) [`AK_MAX_PRIORITY`]) | First action after the bank registry is reset (`0x00467490`); result ignored. Throughput value: §3.3. |
| `0x00470110` (33) / `0x00470160` (31) | `MemoryMgr::CreatePool` + `SetPoolName` | (memory = block from the game allocator object, size = n + `0x2000`, block size = n + `0x2000`, attributes = **9**, block alignment = 0); then (pool id, name) | The block comes from the audio-library allocator (object `0x0317285c`, or the `ECX` object for `0x00470110`) through its vtable slot `+0x38` (size, alignment **`0x800`**, 0, 0). One block the size of the whole pool: a fixed single-block pool per bank (HIGH; attribute 9 = `AkMalloc | AkFixedSizeBlocksMode` in public terms, HYPOTHESIS). `0x00470160` stores the pool id at bank record `+0x50` and names it with the record's own name string; `0x00470110` (from `0x00463420`) returns the id and names it from its stack argument. Allocator failure: `0x00470110` → `−1`, `0x00470160` leaves `+0x50` untouched. `CreatePool` failure: the block is not returned to the allocator. |
| `0x0046b850` (68) — stream device setup, called by init | `StreamMgr::GetFileLocationResolver`, `SetFileLocationResolver`, `CreateDevice` (`0x0046b939`/`b947`/`b955`) | resolver: set to the game object **`0x032358f0`** only if none is set yet; `CreateDevice`(device settings from init (§3.2), I/O hook = **`0x032358f4`**) | Refuses (returns false) unless the device settings' scheduler flag (`+0x14`) is **2** [deferred, lined-up]. Before creating the device it initialises the hook object's request pool (`0x004723d0`, entries `0x0323dd70`…`0x0323e5b0`, stride `0x58`) and starts the **"Audiolib Stream Manager"** thread (entry `0x00471000` → `0x0046b820`, stack `0xc000`). Device id → `0x0323e5f0` (`−1` = failed). The two objects are four bytes apart (`0x032358f0` resolver vtable slot, `0x032358f4` hook vtable slot): one C++ object with two interfaces (§3.6). |
| `0x0046f820` (162) — the audio-input execute callback | `StopSourcePlugin` (`0x0046f910`) | (plug-in id = **200**, company = **0**, playing id = slot `+0x08`) | See §3.6. |

---

## 2. Record layouts (CONFIRMED offsets unless marked; built up from every function in §1)

### 2.1 The play record (32 bytes; one posted event on a voice)

Pool: base pointer `0x031d4cd0`, a parallel **state byte array** at `0x031d4cd8` indexed by
`(record − base) >> 5`, capacity `0x031d4ce8` (set to `0x41a` = 1050 by `0x00460ee0`, CONFIRMED write),
pool object `0x031d4cf0` (passed to the state-setter `0x004610b0` and the free routine `0x00461000`).

| Offset | Size | Meaning | Evidence |
|---|---|---|---|
| `+0x00` | dword | next play record on the same voice (list head at voice `+0x24`) | `0x0045d1e0`, `0x0045cc10` unlink loops |
| `+0x04` | dword | **cookie** — handed to `PostEvent` as the callback cookie, to the status queue `0x004719b0` with every status code, and to `0x00461210` when the record is freed | `0x00470500`, `0x00470720`, `0x004707e0`, `0x0045ce70`, `0x0045cc10` |
| `+0x08` | dword | **event id** (sound-effect path) / **line key** mapped to (cookie, event) by `0x00469100` (voice-line path) | `0x00470720`, `0x00470500`, `ExecuteActionOnEvent` and `SeekOnEvent` sites |
| `+0x0c` | dword | **playing id** returned by `PostEvent`; 0 = not posted. Cleared by the stop wrapper. Pause/resume are skipped while it is 0 | `0x004705cc`, `0x00470756`, `0x0047080e`, `0x0045cfea`, `0x0045d048` |
| `+0x10` | dword | **start-delay deadline** (the one-dword game timer described under voice `+0x1c`); checked only in state 1; when it fires, request bit 0 (play) is raised and the deadline set to `−1` (disarmed) | `0x0045cec9`–`0x0045ceda` |
| `+0x14` | int32 | **seek target in ms**; `< 0` = none; reset to `−1` after a successful seek | `0x00470790` |
| `+0x18` | dword | **request/flag word** (bit table in §2.3) | `0x0045ce70` |
| `+0x1c` | byte | **streamed-line slot**, `0xff` = none. Released through `0x0046a700(slot)`, which finds the matching entry among the three `0x58`-byte entries at `0x0322d828` (match byte at entry `+0x50`) and frees it with `0x0046a950`, under the audio lock. HIGH: the persona-line (`.lm_pc`) streaming slots of `review/scoping-audio.md` §2.2. | `0x0045cf83`, `0x0045cc58` |
| `+0x1e` | byte | **posted** flag: set to 1 right after `PostEvent` (left at 1 on the voice-line failure path, cleared on the sound-effect failure path — §5), cleared by the stop wrapper and by status codes 7 and 8 | `0x004705cf`, `0x00470759`/`0x00470761`, `0x0047080a`, `0x0045cb0d`, `0x0045cb2c` |
| `+0x1f` | byte | **has started**: set to 1 by status code 4 (§4.4); read by the code-7 handler | `0x0045ca5a`, `0x0045cb18` |

**The cookie `+0x04` is a handle, not a pointer.** Status messages carry it, and the consumer resolves
it with `0x004613a0`: an integer hash of the handle, modulo `0x41a` (1050), looked up in the table
`0x031e3d20` (`0x004620f0`), whose hit gives the record pointer from `0x031e5df8[index]`.
`0x00461210(cookie)` removes the entry when the record is freed. So a late Wwise callback for a freed
record is dropped harmlessly (CONFIRMED mechanism; HIGH that it is the purpose).

The **state byte** (separate array, not in the record; changing it moves the record between per-state
lists, `0x004610b0`) takes values 1–8. Names below are HIGH, from the transitions in `0x0045ce70` (§1.2)
combined with the status consumer `0x0045c9d0` (§4.4):

| State | Meaning | Left by |
|---|---|---|
| 1 | created, not yet audible: waiting for its delay, or posted and waiting for the first `Duration` callback | play request → post (stays 1 on success); post failure → 7; status 4 (request `0x20000`) → 2; stop → 5/6 |
| 2 | playing | pause → 3; stop → 5/6; seek happens only here |
| 3 | pause sent, not yet confirmed | status 6 (request `0x40000`) → 4; stop → 5/6 |
| 4 | paused | resume sent (stays 4); status 4 → 2; stop → 5/6 |
| 5 | stop sent, waiting for the end | status 7 (request `0x80000`) → 6 |
| 6 | ended (terminal) | marked dead |
| 7 | failed (terminal) | marked dead |
| 8 | killed (terminal; request `0x800000`, not raised by the status consumer — set by the voice API) | marked dead |

States 6, 7, 8 on entry set flag bit 31 and return; the voice tick then unlinks and frees the record.

### 2.2 The voice (stride `0xa8`; the Wwise game object)

Pool: array base `0x031f02c0`, 16-bit index links at `0x031f02c4` (4 bytes per voice; `+2` = next index),
head index `0x031f02bc`, live count `0x031f02be`, list-valid byte `0x031f02b4`; all under the audio lock
`0x031f0260`. The game object id handed to Wwise is the voice's address (CONFIRMED at every
object-taking call site).

| Offset | Size | Meaning | Evidence |
|---|---|---|---|
| `+0x00` | dword | **voice handle** (non-zero when live). Printed with `"%u"` as the Wwise registration name; registration and unregistration are skipped when 0; passed to `0x00461520` on release | `0x00470610`, `0x0045cc9f`, `0x0045ccb7` |
| `+0x0c` | float | value of the per-voice RTPC `0x85c3e987` (current) | `0x0045cde5` |
| `+0x10` | float | the same, last value sent | `0x0045cdfb` |
| `+0x14` | dword | primary persona/line-table key for the external-source file lookup | `0x00470519` |
| `+0x18` | dword | fallback key for the same lookup | `0x00470556` |
| `+0x1c` | dword | **positional-update deadline** (re-armed 33 ms while the voice has play records, 5000 ms otherwise). The game's timer type is one dword: a deadline on the millisecond counter `0x01320d94`, which wraps at 1.8·10⁹; negative = disarmed; `0x00d9e1e0` = "expired?", `0x00d9e140(ms)` = arm | `0x0045cd8e`, `0x0045ce11`–`0x0045ce25` |
| `+0x20` | dword | not touched by the adapter | — |
| `+0x24` | ptr | **head of the play-record list** (§2.1) | `0x0045d3cc`, `0x0045cc20` |
| `+0x28`…`+0x3c` | 6 floats | first emitter position (x, y, z) and orientation (x, y, z) — Wwise `AkSoundPosition` order | `0x004702bd`–`0x004702e8` |
| `+0x40` | dword | (not read by the adapter) — the sixth dword of the embedded first position node | — |
| `+0x44` | ptr | next position node (32-byte nodes: 6 floats, one dword, next at `+0x1c`) | `0x00470346` |
| `+0x60`/`+0x64` | id, float | aux-bus send 0 (environment id, level) — used instead of the global send when flag bit 0 of `+0x98` is set | `0x00470a0d` |
| `+0x68`/`+0x6c` | id, float | aux-bus send 1 | `0x00470a2f` |
| `+0x70`/`+0x74` | id, float | aux-bus send 2 | `0x00470a53` |
| `+0x78`…`+0x87` | node | **switch list**, first node embedded: `+0x78` next, `+0x7c` switch-group id, `+0x80` switch-state id, `+0x84` dirty byte | `0x0045d381`–`0x0045d3a8` |
| `+0x88`…`+0x97` | node | **RTPC list**, first node embedded: `+0x88` next, `+0x8c` RTPC id, `+0x90` float value, `+0x94` dirty byte | `0x0045d326`–`0x0045d35b` |
| `+0x98` | word | **voice flags** (bit table in §2.3) | `0x0045d1e0`, `0x004709dc`, `0x0045f3c1` |
| `+0x9f` | byte | number of emitter positions (0 = none sent, 1 = `SetPosition`, ≥2 = `SetMultiplePositions`) | `0x004702b2` |
| `+0xa0` | byte | number of play records on the voice | `0x0045d43d`, `0x0045cc82`, `0x0045ce11` |
| `+0xa1` | byte | obstruction sent (0–100) | `0x00470450` |
| `+0xa2` | byte | obstruction target (0–100) | `0x00470450` |
| `+0xa3` | byte | count of successful voice-line posts on this voice | `0x004705f0` |
| `+0xa6` | byte | per-voice RTPC `0x85c3e987` enabled | `0x0045cdd1` |

Voice byte `+0x9e` bit 0 enables the per-voice **marker callback**: a function pointer at voice `+0x04`
with user data at `+0x08` (§4.4). So voice `+0x04`/`+0x08` are a callback/user-data pair, CONFIRMED by
the consumer.

### 2.3 Flag words

**Play record `+0x18` (request word), as consumed by `0x0045ce70` — CONFIRMED bits, HIGH names.** The
five "event" bits are tested in this priority order and only the first one found acts per tick; each
also rewrites the word as shown.

| Bit | Raised by | Effect in `0x0045ce70` |
|---|---|---|
| `0x800000` | voice API | → state 8; word := (word & `0x81880000`) \| `0x880000` |
| `0x200000` | status 8 (post failed) | → state 7; word := (word & `0x81200000`) \| `0x200000` |
| `0x80000` | status 7 (ended/stopped) | → state 6; word := (word & `0x81a80000`) \| `0x80000` |
| `0x40000` | status 6 (paused) | → state 4; clears `0x40000` and bit 2 |
| `0x20000` | status 4 (duration known / resumed) | → state 2 (unless already 2); clears `0x20000` and bit 0 |

Then one action, again first match wins:

| Bit | Name | Action |
|---|---|---|
| bit 3 (`0x8`) | stop | release stream slot `+0x1c`; `StopPlayingID` in states 1 (only if posted), 2, 3, 4 → state 5 (6 on wrapper error); state 7 → 6. The bit is not cleared. |
| bit 2 (`0x4`) | pause | if bit 0 is also set, treated as bit 0 instead; else pause in state 2 → 3 |
| bit 0 (`0x1`) | play / resume | state 1 → post (bit 24 picks the path), bit cleared, failure → 7; state 4 → resume, bit cleared on success |
| bit 1 (`0x2`) | seek | state 2 only; cleared on success |
| bit 24 (`0x01000000`) | voice line | selects `0x00470500` instead of `0x00470720` |
| bit 31 (`0x80000000`) | dead | set on entry in states 6/7/8; the voice tick frees the record |

The play-request bit 0 is also raised by the state machine itself when the start-delay deadline `+0x10`
fires in state 1.

**Voice `+0x98` (word) — CONFIRMED bits, HIGH names.**

| Bit | Meaning | Where |
|---|---|---|
| `0x0001` | use the voice's own aux send 0 (`+0x60`/`+0x64`) instead of the global one | `0x004709dc` |
| `0x0010` | RTPC list dirty | set `0x0045f3c1`, cleared `0x0045d369` |
| `0x0020` | switch list dirty | cleared `0x0045d3b6` |
| `0x0040` | needs `RegisterGameObj` | tested `0x0045d2d5`, cleared `0x0045d2f4` |
| `0x4000` | release when the play list becomes empty (sets `0x8000`) | `0x0045d3d3` |
| `0x8000` | release now (teardown `0x0045cc10` at the end of this voice's tick) | `0x0045d44d` |

### 2.4 Voice tick order (`0x0045d1e0`, Playlist Process task) — CONFIRMED

Once per active voice, under the audio lock `0x031f0260`, and only on even passes of the main loop
(§4.5):
1. flag `0x40` → `RegisterGameObj`; on success clear the flag; **either way go to the next voice**;
2. positional update `0x0045cd80` (positions, obstruction, per-voice RTPC `0x85c3e987`, aux sends —
   gated by the voice's own deadline `+0x1c`);
3. flag `0x10` → flush the RTPC list;
4. flag `0x20` → flush the switch list and **go to the next voice**;
5. run `0x0045ce70` on every play record; unlink and free records marked dead (bit 31), decrementing
   `+0xa0`;
6. list empty and flag `0x4000` → set `0x8000`; flag `0x8000` → teardown `0x0045cc10`.

---

## 3. Init and shutdown, the memory hooks, the I/O hook object, the bank callbacks

### 3.1 The audio-library init `0x00462460(config)` — CONFIRMED order

Called once from `0x00554d6f` (gameplay audio module). One argument, a configuration block; every
field below is copied to a global before anything starts. Returns 0, or `−1` on any failure (with no
unwinding of the steps already done).

1. First call only (byte `0x031728c1`): channel count `0x031728d4` := 2, `0x031728d0` := 0, two bytes
   cleared, `0x031728c1` := 1.
2. Seven mix-RTPC values `0x03172918[0..5]` := **0.8** (float `0x01173df8`), `[6]` := **1.0**; their
   seven dirty bytes `0x0317290e…` := 1 (§1.3).
3. Config copy: three path strings at config `+0x00`/`+0x04`/`+0x08` (up to 1 KiB each, into
   `0x03171c3c`/`0x0317203c`/`0x0317243c`); `+0x10` → **allocator object** `0x0317285c` (used for bank
   pools, §1.4, and called through vtable slots `+0x1c`, `+0x38`, `+0x40`); `+0x14` → `0x03172860`;
   `+0x18` → **stream-device I/O memory size** `0x03172864`; `+0x1c` → **sound-engine default pool
   size** `0x03172868`; `+0x20` → **lower-engine pool size** `0x0317286c`; `+0x24` → `0x03172870`;
   `+0x28`…`+0x38` → `0x03172888`…`0x03172898`; byte `+0x60` → `0x03172934`; `+0x3c`, `+0x40`, `+0x44`,
   `+0x48` → `0x0317289c`…`0x031728a8` (**each must be non-zero or init fails**); `+0x4c`…`+0x5c` →
   `0x031728ac`…`0x031728bc`; byte `+0x68` → `0x03172882`; byte `+0x69` → **`0x03172883` (aux sends
   on/off, §1.1)**; `+0x64` → `0x0317287c`; `+0x6c` → **`0x03172970`, the window handle** passed to
   Wwise (§3.2). The smoothed RTPC pair `0x031728dc`/`0x031728e0` := 1.0.
4. Default external source: `0x03172874` := hash(`"Default_Ext_Src_external_source"`), `0x03172878` :=
   hash(`"voc_Default_Ext_Src_play"`) (§1.2).
5. `0x00462c70`; `0x004628a0(config +0x0c)` (failure → `−1`).
6. **Start the "Audio callback thread"** `0x00471c70` (§4.3) — before Wwise exists, so it is ready for
   the first callback.
7. **Wwise init `0x0046fdd0`** (§3.2), only if `0x031728c5` is clear; success sets **`0x031728c5` and
   `0x031728c6`** to 1.
8. Bank system `0x00466850`: reset the bank registry (`0x00467490`), `SetBankLoadIOSettings(1048.576,
   100)` (§1.4), clear the 11 + 5 dword tables at `0x02cea950`/`0x02cea93c`, read `audio_banks.xtbl`
   (`0x00464a70(−1)`; **failure clears `0x031728c6`/`0x031728c5` and runs the Wwise shutdown
   `0x004701b0`**), `0x00465410(−1)`, register the **"Soundbank Process"** task (`0x00465cc0`), set
   `0x03171a6e`.
9. Voice system `0x0045d650`: under the audio lock, build the pools (`0x004614d0`, `0x004611c0`,
   `0x004604b0`, play pool `0x031d4cf0` via `0x00461160`, `0x00460ea0`, `0x0045d610`), load
   `audio_info.vad_pc` (`0x0045d4c0(−1, 1)`), register the **"Playlist Process"** task (`0x0045d1e0`),
   set `0x03171a57`.
10. `0x00467fc0` once (byte `0x03171a92`).
11. **Start the "Main Audio Thread"** `0x00471030` (§4.5); `0x00469460`.
12. Allocator slot `+0x1c` (×3, interleaved with an optional client callback `0x031728b8(allocator)`
    and `0x00464f30`), `0x00471d70`.
13. `0x031728c2` := 1 (voice API up), `0x031728c0` := 1 (mix RTPCs live).
14. **Default voice** `0x02cea938` := `0x0045da50(0, "2D" (`0x0129ed40`), 1)`; then RTPC `0x85c3e987` :=
    0.0 on that voice and then globally, both with the fourth argument 0.009 (float `0x012a2ff4`),
    through `0x0045f2d0`.

### 3.2 Wwise init `0x0046fdd0` — CONFIRMED order and values

Returns 1 on success, 0 on the first failure, **with no rollback** of the parts already initialised.
Struct field names are the public SDK's, matched by offset and value shape (HIGH); the offsets and
values themselves are CONFIRMED.

| Step | Call | Settings the game changes (everything else = the SDK default from the matching `GetDefault…` call) |
|---|---|---|
| 1 | `MemoryMgr` init (unexported `0x00f32960`) | memory settings `+0x00` (max number of pools) = **21** (`0x15`). Result must be 1. |
| 2 | `StreamMgr::GetDefaultSettings` → `StreamMgr::Create` | stream-manager settings `+0x00` (its memory size) = **`0x10000`** (64 KiB). Null → fail. |
| 3 | `StreamMgr::GetDefaultDeviceSettings` → `0x0046b850` (§1.4) | device settings: `+0x04` I/O memory size = config value `0x03172864`; `+0x10` granularity = **`0x6800`** (26 KiB); `+0x14` scheduler type = **2** (deferred lined-up — required by `0x0046b850`); `+0x20` I/O thread stack = **`0x10000`**; `+0x28` max concurrent I/O = **18** (`0x12`). |
| 4 | `SoundEngine::GetDefaultInitSettings`, `GetDefaultPlatformInitSettings` → `SoundEngine::Init` | init settings: `+0x0c` default pool size = config `0x03172868`; `+0x10` pool ratio threshold = **0.95**. Platform settings: `+0x00` window handle = config `0x03172970`; `+0x04` lower-engine thread priority = **2**; `+0x0c` its stack = **`0x8000`**; `+0x18` bank-manager thread stack = **`0x8000`**; `+0x1c` lower-engine pool size = config `0x0317286c`; `+0x20` lower-engine pool ratio threshold = **0.85**; `+0x24` (16-bit) refills per voice = **5**. Result must be 1. |
| 5 | `MusicEngine::GetDefaultInitSettings` → `MusicEngine::Init` | defaults unchanged. Result must be 1. |
| 6 | `RegisterPlugin` ×18, `RegisterCodec` ×1, one unexported call | table below; results ignored |
| 7 | `MotionEngine::SetPlayerListener(player 0, listener 0)` | |
| 8 | `SetListenerPipeline(listener 0, audio = true, motion = true)` | |
| 9 | `0x0066ac20(3, 0x0046fd80)` | the linker-folded no-op that also stands for `AK::Monitor::PostCode`/`SetLocalOutput` (`review/scoping-audio.md` §1.1). By its arguments (error level 3, an output callback) the source called **`SetLocalOutput`**; in this build it does nothing. The callback `0x0046fd80` narrows a wide message into a 2 KiB stack buffer and discards it. |
| 10 | `GetSpeakerConfiguration` | **`0x031728d4` := 6 if the result is `0x3f` [5.1], else 2** (channel count used by the game). |

There is **no communication (`AK::Comm`) init and no bank load** in this function: banks are loaded
later by the Soundbank Process task.

**Plug-in registrations, in call order** (type, company, plug-in id, then the two factory pointers, all
inside the Wwise block). Type values: 2 = source, 3 = effect, 5 = motion source (public `AkPluginType`,
HYPOTHESIS for the names). The plug-in names are the public SDK ids for those numbers (HYPOTHESIS; the
RTTI class list in `review/scoping-audio.md` §1.1 contains the matching `CAk…` classes for most of
them).

| # | Type | Company | Id | Public id name (HYPOTHESIS) | Create / params factory |
|---|---|---|---|---|---|
| 1 | 3 | 0 | `0x73` (115) | Matrix reverb | `0x00f32670` / `0x00f32940` |
| 2 | 3 | 0 | `0x69` (105) | Parametric EQ | `0x00f41910` / `0x00f40d90` |
| 3 | 3 | 0 | `0x6e` (110) | Peak limiter | `0x00f42c70` / `0x00f42ee0` |
| 4 | 3 | 0 | `0x74` (116) | SoundSeed Impact | `0x00f91b00` / `0x00f92540` |
| 5 | 2 | 0 | `0x77` (119) | SoundSeed Wind | `0x00f92d10` / `0x00f934a0` |
| 6 | 2 | 0 | `0x78` (120) | SoundSeed Woosh | `0x00f967c0` / `0x00f95e60` |
| 7 | 3 | 0 | `0x6c` (108) | Compressor | `0x00f22dd0` / `0x00f22380` |
| 8 | 3 | 0 | `0x6d` (109) | Expander | `0x00f24170` / `0x00f23840` |
| 9 | 3 | 0 | `0x6a` (106) | Delay | `0x00f23690` / `0x00f23040` |
| 10 | 5 | 0 | `0x195` (405) | Motion generator | `0x00f34020` / `0x00f34090` |
| 11 | 2 | 0 | `0x64` (100) | Sine | `0x00f4a9f0` / `0x00f4ab70` |
| 12 | 2 | 0 | `0x65` (101) | Silence | `0x00f4a670` / `0x00f4a650` |
| 13 | 2 | 0 | `0x66` (102) | Tone generator | `0x00fa11a0` / `0x00fa06e0` |
| 14 | 3 | **`0x100`** | `0x67` (103) | McDSP FutzBox | `0x00fbc000` / `0x00fbc5e0` |
| 15 | 3 | **`0x100`** | `0x6e` (110) | McDSP ML1 | `0x00faa0b0` / `0x00fa96c0` |
| — | — | — | — | unexported `0x00fa8780(0x0046f820, 0x0046f800, 0)` — HIGH: the audio-input plug-in's "set callbacks" (execute, get-format, get-gain = none), §3.6 | |
| 16 | 2 | 0 | `0xc8` (200) | Audio input | `0x00fa8d10` / `0x00fa8e10` |
| — | codec | 0 | **4** | Vorbis — `RegisterCodec(company 0, codec 4, file-source factory = CreateVorbisFilePlugin 0x00fa44a0, bank-source factory 0x00fa33e0)` | |
| 17 | 3 | 0 | `0x76` (118) | RoomVerb | `0x00f46d90` / `0x00f439d0` |
| 18 | 3 | 0 | `0x7e` (126) | Guitar distortion | `0x00f24780` / `0x00f24f70` |

A replacement engine must accept bank content that references any of these ids (company 0 or 256).
Which ones the shipped banks actually use is a `HIRC` question and is not opened here.

### 3.3 Constants used by the adapter (CONFIRMED raw values)

| Address | Raw | Value | Used as |
|---|---|---|---|
| `0x01295104` | `0x4483126f` | 1048.576 (float) | `SetBankLoadIOSettings` throughput (public unit: bytes per ms, i.e. 1 MiB/s, HIGH) |
| `0x012a2dd0` | `0x3c23d70a` | 0.01 (float) | aux-send "is non-zero" threshold |
| `0x012a2dd8` | `0x4059000000000000` | 100.0 (double) | obstruction divisor; ×100 for RTPC `0xdb2d51a3` |
| `0x012a2ee0` | `0x3fa99999a0000000` | 0.05 (double) | RTPC `0xdb2d51a3` smoothing threshold |
| `0x01351020` | `0x3a83126f` | 0.001 (float) | value-cache "changed" epsilon (in `.data`, not `.rdata`) |
| `0x012a30fc` / `0x012a3100` | `0x3f733333` / `0x3f59999a` | 0.95 / 0.85 | pool ratio thresholds |
| `0x01173df8` | `0x3f4ccccd` | 0.8 | initial mix-RTPC values |
| `0x012a2ff4` | `0x3c1374bc` | 0.009 | fourth argument of the two init-time RTPC calls |
| `0x013520ec` | — | `0x10138752`, `0xe7f119bb`, `0xdb1f38cc`, `0xd0133f1b`, `0xdafafc77`, `0x293d1ed3`, `0x8d7f8461` | default ids of the seven mix RTPCs |

### 3.4 Shutdown

**Audio-library shutdown `0x00462780`** (called from `0x00553cde`), CONFIRMED order:
1. `0x031728c0` := 0 (mix RTPCs off);
2. `0x0045dca0`, `0x0045d740` (voice system down; `0x0045d740` clears the Playlist gate `0x03171a57`);
3. `0x031728c8` := 1, `0x03171a92` := 0; bank system down `0x00466fb0`;
4. if the Main Audio Thread was started (`0x03171abc`) and its handle `0x013537a0` ≠ `0xffff`: set its
   quit byte `0x013537b0`, wait for / close it (`0x00db65e0`), handle := `0xffff`;
5. **`0x031728c6` := 0, `0x031728c5` := 0, then Wwise shutdown `0x004701b0`** — so every wrapper already
   refuses before Wwise goes;
6. `0x03171aa2` := 1; stop the callback thread `0x00471d10` (§4.3);
7. `0x031728c2` := 0, `0x031728c1` := 0; allocator slot `+0x40(0, 0)`.

**Wwise shutdown `0x004701b0`** (28 insns; also the failure path of the bank init), CONFIRMED order —
the exact reverse of §3.2:
1. `MusicEngine::Term`;
2. if `SoundEngine::IsInitialized`: `StopAll(−1)` [all objects], `ClearPreparedEvents`,
   `UnregisterAllGameObj`, `ClearBanks`, `SoundEngine::Term`;
3. if the resolver currently installed is the game's object `0x032358f0`,
   `SetFileLocationResolver(null)`;
4. `StreamMgr::DestroyDevice(0x0323e5f0)` (also when the device was never created: id `−1`);
5. if the exported `IAkStreamMgr::m_pStreamMgr` (`0x02cc5f2c`) is non-null, call its vtable slot 1
   (`+0x04`) — HIGH: the stream manager's `Destroy`;
6. tail-jump to `MemoryMgr::Term`.

No `UnregisterPlugin`, no wait for pending bank callbacks. Plug-in callbacks registered with the
audio-input plug-in are not cleared.

### 3.5 The four memory hooks — CONFIRMED bodies, HIGH roles

The SDK requires the game to define these four functions; Wwise's memory manager calls them directly.

| Hook | Game body | Called from (Wwise side) | Behaviour |
|---|---|---|---|
| allocate (size) | `0x0046fc90` (6 insns) | `CreatePool` `0x00f32aba`, `0x00f32d10` | `0x00ea3362(size, 0x800)` — the C-runtime aligned allocation with **2 KiB alignment** (HIGH: its free partner below has the aligned-free shape) |
| free (pointer) | `0x0046fcb0` (1 insn) | `MemoryMgr::Term` `0x00f33045`, `0x00f32990` | jumps to `0x00ea3348`, which frees the block whose real start is stored in the dword just below the 4-byte-rounded pointer — the C-runtime aligned free |
| virtual allocate (address, size, type, protection) | `0x0046fcc0` | `CreatePool` `0x00f32aa4` | forwards all four arguments to Win32 `VirtualAlloc` (import `0x0101c1a0`) |
| virtual free (address, size, free type) | `0x0046fce0` | `0x00f329a4` | forwards all three to Win32 `VirtualFree` (import `0x0101c328`) |

The game's own bank pools (§1.4) pass their own memory to `CreatePool`, so these hooks serve only the
pools Wwise creates for itself.

### 3.6 The I/O hook object and the audio-input callbacks

**The object** lives at `0x032358f0` and is built by a static constructor `0x0046b690` (run from the
C-runtime initialiser list, `0x00fea8d0`). It has two vtable pointers — `+0x00` = **`0x0129f13c`**
(file-location resolver, 3 slots), `+0x04` = **`0x0129f14c`** (deferred low-level I/O hook, 8 slots) —
and, behind them, two 16 KiB buffers (`+0x08`, `+0x4008`), a 3-entry × `0x78` table at `0x0323d8f8`,
24 request entries × `0x58` at `0x0323dd70` with a free list (`0x0323e5b0`) and a queue (`0x0323e5c8`),
a lock `0x0323e5f4`, the device id `+0x8d00` (`0x0323e5f0`, `−1` until created) and an open-file count
`+0x8d20`. The constructor first writes the abstract-base vtables (`0x0129f12c`, `0x0129f108`) and then
the final ones, which is the usual C++ construction order.

| Vtable slot | Function | Public method (HIGH) | Behaviour (CONFIRMED) |
|---|---|---|---|
| resolver 0 | `0x0046b660` | destructor | scalar-deleting destructor |
| resolver 1 | `0x0046b970` | `Open(AkFileID, …)` | sets the caller's "synchronous open" flag to true; allocates a file entry (`0x00467850`), opens by id through `0x00465830`; failure → frees the entry, returns 2 [`AK_Fail`]; success → file descriptor: size from the entry, sector 0, no custom parameter, **`+0x14` = the entry**, `+0x18` = the device id; open count +1; returns 1 |
| resolver 2 | `0x0046ba10` | `Open(const char*, …)` | **always returns 2** — opening by name is not supported |
| hook 0 | `0x0046c700` | destructor | adjusts `this` by −4 and jumps to the resolver's destructor (one object, two interfaces) |
| hook 1 | `0x0046ba20` | `Close` | if the entry has a bank link (`+0x58`), decrements the linked bank's 16-bit reference at its `+0x58` under the bank lock `0x0320e4e4`; frees the entry; open count −1; returns 1 |
| hook 2 | `0x0046ba80` | `GetBlockSize` | returns **`0x800`** (2048) |
| hook 3, 4 | `0x00444690`, `0x00444dc0` | `GetDeviceDesc`, `GetDeviceData` | linker-folded generic stubs (`review/scoping-audio.md` §1.5) |
| hook 5 | `0x0046ba90` | `Read` | `0x0046bcb0(this, descriptor +0x14 (the entry), transfer info, heuristics)`; true → 1, false → 2 |
| hook 6 | `0x0046bac0` | `Write` | always returns 2 |
| hook 7 | `0x0046bad0` | `Cancel` | not read in full |

The abstract bases right before them (`0x0129f0f0`: 5 slots, `0x0129f108`: 8 slots, `0x0129f12c`: 3
slots; every non-destructor slot = the pure-call stub `0x00ea2778`) are as `review/scoping-audio.md`
describes. **Deferred reads complete on the callback thread**: the read path (`0x0046bcb0`,
`0x0046c040`) posts message code **10** through `0x00471ae0(transfer info, result)`, and the callback
thread calls the function pointer at transfer-info `+0x14` with (transfer info, result). The read path
checks exactly that field before posting (`0x0046bd9c`, `0x0046bde0`) and posts result 1
[`AK_Success`]. HIGH that it is Wwise's own I/O-completion callback: the public asynchronous
transfer-info layout (64-bit file position at `+0x00`, two 32-bit sizes, buffer pointer at `+0x10`)
puts the callback at `+0x14`, and the read path uses `+0x00` as the file position, `+0x0c` as the size
and `+0x10` as the buffer. So Wwise's stream manager is told about finished reads from the game's
"Audio callback thread", not from the "Audiolib Stream Manager" thread.

**Audio-input callbacks** (registered at §3.2 through the unexported `0x00fa8780`; plug-in id 200):
- **format** `0x0046f800(playing id, format*)`: writes sample rate **11025** (`0x2b11`) and the packed
  word **`0x02400004`** = channel mask 4 (front centre, i.e. mono), 16 bits per sample, block align 2,
  integer, interleaved (decoded with the 18/6/5/2/1-bit field widths of the header helper `0x0046f7b0`,
  `review/scoping-audio.md` §1.4; HIGH).
- **execute** `0x0046f820(playing id, buffer*)`: under lock `0x032350d8`, scans three source slots of
  `0x2728` bytes (5000 16-bit samples of ring buffer at `0x0322db60 + slot·0x2728`, then six control
  dwords at `0x03230270 + slot·0x2728`: `+0x00`, `+0x04` (not read here), `+0x08` play handle, `+0x0c`
  voice handle, `+0x10` read index, `+0x14` write index). A slot matches when its play handle resolves
  (under the audio lock) to a play record whose playing id `+0x0c` equals the callback's playing id. If
  the ring is empty: valid frames := 0, **`StopSourcePlugin(200, 0, slot +0x08)`**, release the voice
  (`0x0045dbc0(slot +0x0c)`), zero the six control dwords. Otherwise copy min(available, buffer max
  frames) samples into the buffer (handling the wrap at 5000), set valid frames, advance the read
  index. The buffer state is set to **`0x2d`** (45, public `AK_DataReady`, HIGH) in both cases. The
  buffer fields used are `+0x00` data, `+0x08` state, `+0x0c` max frames (16-bit), `+0x0e` valid frames
  (16-bit).
- **Known defect (HIGH):** `StopSourcePlugin` is given the slot's play *handle* (`+0x08`), while the
  match two steps earlier compared the play record's *playing id* with the callback's playing id.
  Unless the two coincide, the stop names the wrong id. A replacement should decide deliberately
  whether to reproduce this or fix it; either choice is observationally bounded to the audio-input path
  alone.

### 3.7 The bank callbacks — CONFIRMED (range listing; the two bodies are not inside Ghidra functions)

Both take the public bank-callback arguments (bank id, result, pool id, cookie) and use only the first
two; the **bank id, not the cookie, becomes the message cookie**.
- **Load done `0x00470220`**: result 1 [`AK_Success`] or **`0x45`** (69; HYPOTHESIS
  `AK_BankAlreadyLoaded`) → status **1**; anything else → status **0**.
- **Unload done `0x00470250`**: result 1 → status **2**; anything else → status **3**.

The callback thread passes codes 0–3 to `0x00466730`, which finds the bank record by id (hash modulo
`0x223` = 547 in the table `0x0321b4b0`, records from `0x0321c5d0`) under the bank lock `0x0320e4e4`
and edits the bank record's word `+0x54`:
- 1 (loaded): clear bit 0, set bit 3;
- 0 (load failed): **only if bit 2 is set** — set error bit 1 of byte `+0x5b`, clear bit 3, set bit 0;
  otherwise nothing (the state machine cleared bit 2 just before `LoadBank`, so a failure with no new
  request pending is not recorded here — HIGH, to be confirmed against `0x004673c0`);
- 2 or 3 (unloaded / unload failed): clear bit 3, set bit 0.

HIGH: bit 3 = resident, bit 0 = idle. The rest of the bank state machine is `spec-audio-format.md`
§7.5/§8 material.

---

## 4. The callback / feedback path

### 4.1 Wwise callback type → status code — CONFIRMED

Both `PostEvent` callbacks receive (callback type, callback-info pointer); info `+0x00` is the cookie
(= play record `+0x04`, a handle), and for a marker, info `+0x18` is the label string (public
`AkMarkerCallbackInfo` layout: cookie, game object, playing id, event id, marker id, position, label —
HIGH).

| Callback | Type test | Status code(s) posted | Extra |
|---|---|---|---|
| sound effect `0x004706c0` | type **= 1** [`EndOfEvent`] | **7** | |
| | type **= 4** [`Marker`] | **5** | via `0x00471a30(cookie)` with the label in `EDI` |
| | type **= 8** [`Duration`] | **4** | |
| | anything else | none | |
| voice line `0x00469ea0` | type **bit 0x1** set | **9**, then **7** | two messages, same cookie |
| | else type **bit 0x8** set | **4** | |

The sound-effect callback compares for equality and the voice-line one tests bits; with one type per
call (public behaviour) the two are equivalent. The voice-line path never asks for markers (flags `9`).

**Codes posted by the game itself** (not by a Wwise callback): 8 after a failed `PostEvent` (both
paths), 7 after every `StopPlayingID`, 6 after a successful pause, 4 after a successful resume (§1.2),
and 0–3 from the bank callbacks (§3.7). Code 10 is the I/O completion (§3.6). So **4 means "audible /
resumed", 6 "paused", 7 "ended or stopped", 8 "post failed", 9 "voice line ended"**; 5 is a marker.

### 4.2 The message and the queue — CONFIRMED

`0x004719b0(code, cookie)` (38 insns), and its two siblings `0x00471a30` (marker, code 5) and
`0x00471ae0` (code 10), all do the same:
1. take an item from the message pool `0x013537b8` (pool object; its slot `+0x08` is the take,
   `0x00452120`); none → return 0, **the status is dropped**;
2. decrement the free counter `0x013537c8` (interlocked);
3. zero the `0x54`-byte body at item `+0x08` and fill it:

| Body offset | Content |
|---|---|
| `+0x00` | time stamp, `0x00dad740(1000)` (HIGH: milliseconds) |
| `+0x04` | status code |
| `+0x08` | cookie (play handle; bank id for 0–3; transfer-info pointer for 10) |
| `+0x0c`, `+0x10` | zero (read by the marker consumer, §4.4) |
| `+0x14` | code 5: the marker label, copied only if shorter than 64 characters (otherwise left empty); code 10: the result value |

4. append the item to the queue `0x013537d0` (queue object; slot `+0x04` = push `0x00468870`);
5. signal the event `0x03245408` (`0x00dd80f0`); return 1.

**The pool**: 1000 items of `0x60` bytes at `0x02ceadb8`…`0x02d024b8` (8-byte link header, `0x54`-byte
body, 4 bytes spare), filled into the pool by the callback-thread starter `0x00471c70`. There is no
back-pressure: with all 1000 items queued, further statuses are lost.

**Which thread posts.** The two `PostEvent` callbacks run on whichever Wwise thread delivers callbacks
(out of scope); the game-originated codes 4/6/7/8 are posted from the Main Audio Thread (the state
machine); codes 0–3 from Wwise's bank thread; code 10 from the I/O path. The queue is the only
crossing.

### 4.3 The "Audio callback thread" — CONFIRMED

Started by `0x00471c70` (from init step 6): fills the pool, names the descriptor `0x013537f0` "Audio
callback thread", sets its entry to **`0x00471b60`**, stack `0x2040`, and creates the thread through
the shared thread trampoline `0x00471000` (which calls descriptor `+0x20` with the descriptor). Handle
→ `0x01353814`.

Loop `0x00471b60`, until the quit byte `0x01353824` is set:
1. wait on the event `0x03245408` with no timeout (`0x00dd8110(−1)`);
2. pop messages until the queue is empty; for each, copy the `0x54`-byte body to the stack, return the
   item to the pool, increment the free counter, then dispatch on the code:
   - **0–3** → `0x00466730` (bank record, §3.7);
   - **4–8** → `0x0045c9d0` (play record, §4.4);
   - **9** → `0x00469ef0(cookie)` (voice line, §4.4);
   - **10** → if the object at body `+0x08` is non-null and its `+0x14` function pointer is non-null,
     call it with (object, body `+0x14`);
   - anything else ignored;
3. `0x00dc4440(1)` (HIGH: a 1 ms sleep), then loop.

**Stop** `0x00471d10` (shutdown step 6): sets the quit byte, signals the event, then drains the queue
**without dispatching** — every pending status is thrown away. It does not wait for the thread to exit.

### 4.4 What each status does to gameplay-visible state — CONFIRMED

`0x0045c9d0` runs on the callback thread **under the audio lock `0x031f0260`**. It resolves the cookie
to a play record (§2.1); unknown handle → nothing.

| Code | Effect on the play record | Next tick of the state machine |
|---|---|---|
| 4 | request `\|= 0x20000`; `+0x1f` := 1 (has started) | → state 2 (playing) |
| 5 | none on the record. Finds the owning voice (`0x0045c870`); if voice `+0x9e` bit 0 is set and voice `+0x04` (a function pointer) is non-null, calls it with a pointer to a 7-dword block: {the function itself, body `+0x10` byte, play record `+0x0c` (playing id), voice `+0x08` (user data), body `+0x00` (time stamp), body `+0x0c`, pointer to the label at body `+0x14`}. **This call happens on the callback thread with the audio lock held.** | — |
| 6 | request `\|= 0x40000` | → state 4 (paused) |
| 7 | request `\|= 0x80000`; `+0x1e` := 0. If byte `0x03171a5a` is set and the record never started (`+0x1f` = 0), calls `0x0045c870(record)` and ignores the result (OPEN — purpose not settled) | → state 6 (ended) |
| 8 | request `\|= 0x200000`; `+0x1e` := 0 | → state 7 (failed) |

`0x00469ef0` (code 9), also under the audio lock: resolves the play record, finds its voice,
**decrements voice `+0xa3`** (the count raised by each successful voice-line post, §1.2), then
`0x00469e00` with the voice (voice-line bookkeeping, a later unit).

So the **only gameplay-visible effects** of Wwise feedback are: the play record's state (read back by
the voice API's "is it playing" queries, e.g. `0x0045ed50`, `review/scoping-audio.md` §2.4), the
voice-line counter `+0xa3`, the bank record's resident/idle bits, and the optional per-voice marker
callback. The Wwise playing id is never handed out: it stays in play record `+0x0c`. This answers the
scoping note's id-space question from the adapter side: **game code above the voice API can only hold
voice handles and play handles; no Wwise playing id escapes the audio library** (HIGH; the voice API
itself is a later unit).

### 4.5 Thread map of the crossings — CONFIRMED except where marked

| Thread | Wwise calls made from it |
|---|---|
| **Main Audio Thread** (`0x00471030` → loop `0x00470e20`, descriptor `0x0135377c`, stack `0xc000`) | everything in §1.1, §1.2, the frame tick of §1.3 and the bank calls of §1.4. Per pass: increment `0x03171ac0`, run the registered tasks under the task lock `0x0323e620` (Playlist Process, Soundbank Process; per task it records the elapsed time and drops a task that returns true, `0x00471410`), run the frame tick `0x0045f690`, sleep **16 ms** (`0x00dc4440(0x10)`, HIGH), increment `0x03171ac0` again. **Because the voice tick and the frame tick both skip odd values of that counter, and it is incremented once per pass after the first, both run on every second pass only** — about every 33 ms, which matches the 33 ms positional deadline; the Soundbank task runs every pass. |
| caller's thread (game thread or a script thread) | global `SetRTPCValue` through `0x0045f2d0` with handle 0, `SetState` (`0x00462970`), `GetIDFromString`, voice-create-time `RegisterGameObj`/`UnregisterGameObj`/position update (`0x0045da50` calls `0x00470610`, `0x0045cc10`, `0x0045cd80`; locking not independently confirmed here) |
| Wwise's own threads (out of scope) | the `PostEvent` callbacks, the bank callbacks, the audio-input callbacks, the memory hooks, the I/O hook's `Open`/`Read`/`Close` |
| Audio callback thread | no Wwise API call; it calls back **into** Wwise only through the I/O-completion pointer (code 10) |
| "Audiolib Stream Manager" thread (`0x0046b850`, entry `0x0046b820`) | none read here |

---

## 5. Behaviour a replacement must reproduce or knowingly fix

1. **`SetMultiplePositions` gets corrupted orientations** (CONFIRMED, `0x0047031a`–`0x0047041d`). For
   every record the copy loop stores the source's orientation x, y and z **all into the record's
   orientation-x slot** (three stores to the same address), so orientation x ends up = the source's
   orientation z, and orientation y/z are left as whatever was on the stack. Positions are copied
   correctly. With type 2 (multi-directions) the orientations matter only for cone attenuation (public
   behaviour, HYPOTHESIS for impact).
2. **No bound on the multi-position count.** `+0x9f` (a byte, up to 255) records are written into a
   stack array with room for 16 (frame `0x1a0` bytes); more would overrun the frame. Whether any writer
   allows more than 16 is unresolved.
3. **The voice-line post has no "engine up" guard** and **leaves `+0x1e` = 1 when the post fails**
   (CONFIRMED). The sound-effect path clears it. Harmless in practice because the record goes to state
   7 and status 8 clears `+0x1e` again on the callback thread.
4. **Voice teardown does not stop its events** (CONFIRMED, `0x0045cc10`): records are freed and the
   object unregistered, with no `StopPlayingID`. Under the public API, unregistering an object does not
   stop its sounds; they play to their end with frozen parameters (HYPOTHESIS for the audible effect).
   Their end callbacks then arrive with a handle that no longer resolves and are ignored (§2.1).
5. **Pause and resume are event-wide on the object**: `ExecuteActionOnEvent` targets (event id, voice),
   not the playing id, so two play records of the same event on one voice pause and resume together,
   while only the record that asked changes state (CONFIRMED arguments; consequence HIGH).
6. **The smoothed global RTPC `0xdb2d51a3` uses current := (current − target) × k** when current
   exceeds target by more than 0.05 (CONFIRMED, `0x0045f756`–`0x0045f77b`). That is a smooth decay only
   when the target is 0. For a non-zero target the first step already drops the value to
   (current − target) × k, which can be below the target; the next tick then takes the "below target"
   branch and snaps it to the target. Rises (current < target) are applied at once. Value sent =
   current × 100. `k` is `0x0317297c` (runtime, not read here).
7. **The global RTPC cache can silently drop changes.** `0x00470830` keeps up to 200 (id, value) pairs
   (hash on id mod 200, `0x00470d70`/`0x00470d20`, lock `0x032350f4`); a change smaller than 0.001 is
   not sent; and when the table is full (insert returns `0xff`) the function returns "unchanged", so
   **the RTPC is not sent at all**. Every global RTPC path goes through it; per-object RTPCs do not.
8. **The listener is not sent every frame**: only while the arm counter `0x03172908` is positive
   (§1.3). A replacement must not assume a per-frame listener update.
9. **Every per-voice positional update is time-gated**, not frame-gated: 33 ms while the voice has play
   records, 5 s when idle; the aux-send call is repeated on each update even when unchanged;
   obstruction moves at most 7/100 per update.
10. **One-tick latencies** (CONFIRMED structure): a voice whose switches changed processes no play
    records that tick; a lazily registered voice does nothing else in the tick it registers; a newly
    posted record becomes "playing" only after the `Duration` callback has crossed the queue.
11. **Statuses can be lost** when the 1000-item message pool is exhausted, and are discarded at
    shutdown (§4.2, §4.3). A record waiting in state 5 for a lost code 7 would never be freed (HIGH).
12. **Bank callbacks report the bank id, not the cookie** passed to `LoadBank` (§3.7): the cookie (the
    record address) is ignored by the game's callback.
13. **Init has no rollback** (§3.1, §3.2): a failure part-way leaves earlier subsystems running; only
    the bank-registry failure path calls the Wwise shutdown.
14. **Codec**: the only codec registered is 4 (Vorbis), for both streamed files and in-bank media;
    voice lines are streamed external sources addressed by **file id** through the resolver's by-id
    `Open` (by-name `Open` always fails). This ties `spec-audio-format.md` §7 (resolver) to the
    external-source file ids chosen by `0x00468ae0`.

---

## 6. OPEN items

1. **What sets each voice field from the game side** (positions `+0x28…`, `+0x9f`, obstruction target
   `+0xa2`, aux sends `+0x60…+0x74`, per-voice RTPC value `+0x0c` and enable `+0xa6`, persona keys
   `+0x14`/`+0x18`, marker callback `+0x04`/`+0x08`/`+0x9e`) — the voice API is a later unit; this file
   only reads them.
2. **Request bit `0x800000`** (→ state 8) and the writers of request bits 0–3 — the voice API.
3. **`0x0045c160`** (the per-voice RTPC "changed" test before `0x00470960`): a tiered threshold that
   depends on the magnitude of `+0x0c` (floats `0x012a2f80`, `0x012a2f7c`, `0x012a2f78`, `0x01351c3c`,
   and 1.0); values not read.
4. **`0x0045c230`'s fourth argument** (0.009 at init): stored with the per-voice RTPC but never passed
   to Wwise in anything read; HYPOTHESIS an interpolation time the 2011 `SetRTPCValue` signature has no
   room for.
5. **Code-7 side call** `0x0045c870(record)` when `0x03171a5a` is set and the record never started —
   result unused; purpose unknown (diagnostic?).
6. **Identity of the four required config pointers** `+0x3c`…`+0x48` and the other unnamed config
   fields (§3.1) — whoever builds the config block at `0x00554c90`.
7. **The value of `k` (`0x0317297c`)** and who writes the RTPC targets `0x031728dc`, `0x031728d8`, the
   seven mix values `0x03172918[]`, the listener globals and the arm counter `0x03172908`.
8. **Names of the RTPCs**: `0x85c3e987` (per voice), `0xdb2d51a3`, `0x9e3669da` and the seven mix ids at
   `0x013520ec` are FNV-1 hashes whose source strings were not searched for here.
9. **`0x0046bad0` (I/O `Cancel`)** and the I/O request lifecycle.
10. **Bank load failure with no re-request pending** (§3.7) — whether `0x004673c0` covers it.
11. **Exact public names** of the SDK structs and enums used above (generation: 2011 line, from the
    export names); only the offsets and values are CONFIRMED.

---

## 7. Clean-room check

- Plain hex addresses and own words throughout; no Ghidra auto-names, no decompiler variable names, no
  pasted pseudocode or listing lines. Wwise API names are from the binary's PE export table
  (Audiokinetic's public API); public SDK struct/enum names are given only as labelled interpretations
  of CONFIRMED offsets and values. Thread, task, table and file names are the binary's own strings.
- Nothing touches `.czn_pc` zone data or named-object resolution. Nothing inside the Wwise block or the
  bank `HIRC` format was opened: the plug-in factory addresses are cited only as the pointers the game
  passes.
- The private Ghidra copy used while deriving this material was deleted after the dumps.
