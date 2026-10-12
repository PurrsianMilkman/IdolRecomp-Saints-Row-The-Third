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
| `0x0045cd80` (cont.) | — (calls the wrappers below) | Body, when the voice's **update timer** `+0x1c` has expired: if `+0x9f` ≠ 0, recompute positions (`0x004685c0` closest-sample distance → RTPC, `0x004682d0` zone/AABB aux-send override — both now read, §11a.3), then `0x00470280` (position), `0x00470450` (obstruction); if `+0xa6` ≠ 0 and `0x0045c160` says the value changed, `0x00470960` with RTPC id **`0x85c3e987`** and value `+0x0c`, then copy `+0x0c` → `+0x10`. Then, if global byte **`0x03172883`** (aux sends enabled; copied from the init config, §3.1) is set, `0x004709b0`. Finally re-arm the timer: **33 ms** (`0x21`) if the voice has play records (`+0xa0` ≠ 0), **5000 ms** otherwise. | `+0x0c`, `+0x10`, `+0x1c`, `+0x9f`, `+0xa0`, `+0xa6` | — |
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

### 1.2a `voice_control.xtbl`'s runtime, the line-key table, and the A7 gap's real resolution (2026-10-08)

**Closes (with a major correction, not a clean closure) this project's own long-standing "A7" gap.**
`0x00469140` — the line-key field `+0x08` above maps through — is CONFIRMED, fully, as
`voice_control.xtbl`'s own runtime loader/consumer, not a per-call dispatch point: it runs once at
table-load time (plus once per installed DLC via the driver `0x00469460`), walks every `Entry` node, and
packs `Local_cooldown`/`Global_cooldown`/`Min_delay`/`Max_delay`/`Play_percent`/`Priority` into one
32-bit record keyed by `Voiceline_id`:

| Field (xtbl) | Quantization | Packed bits |
|---|---|---|
| `Local_cooldown` (ms) | `/1000`, clamp `[0,0x3f]` | bits 0-5 |
| `Global_cooldown` (ms) | `/1000`, clamp `[0,0xf]` | bits 6-9 |
| `Min_delay` (ms) | `/0x46` (70), clamp `[0,0x1f]` | bits 10-14 |
| `Max_delay − Min_delay` (ms) | `/0x50` (80), clamp `[0,0x3f]` | bits 15-20 |
| `Play_percent` (0-100) | 4-bucket ladder (<26→0, 26-50→1, 51-75→2, ≥76→3) | bits 21-22 |
| `Priority` (int, default 10) | none | bits 23-27 |

`External_source`/`Play_event` are read through a third string/hash accessor and inserted as a 3-dword
record via `0x00467950`, looked up by `0x0046b140`. **The unpacker (`0x00469100`→`0x004694c0`) is now
bit-exact round-trip-verified against this exact layout** — every one of the six fields' inverse
reconstruction formulas matches the loader's own quantization formula field-for-field, closing the
record format completely. `0x00469100` is confirmed NOT Lua-reachable; its only caller is inside the
voice-line `PostEvent` wrapper (`0x00470500` above), reached only from this state machine. The real
Lua-facing entry points are `audio_play_persona_line` (ad-hoc single line) and `audio_conversation_play`
(scripted multi-turn `.ctdg_pc` conversation), both already documented in `spec-lua-api-behaviour.md`.

**Correction to this project's own prior Changelog entry: the four functions previously labeled "ambient-
emitter `.lm_pc`-streaming functions" (`0x0046ca50`/`0x0046cdd0`/`0x0046d250`/`0x0046db00`) are NOT
streaming functions and have nothing to do with `.lm_pc` or voice lines at all.** Full disassembly of all
four shows pure floating-point 3D geometry — sphere/capsule/box/compound attenuation-shape distance-and-
falloff evaluators for ambient-emitter *regions* (wind, traffic, crowd beds), each returning a `[0,1]`
fraction from a listener position and a shape descriptor; zero file I/O, zero Wwise API calls, zero
connection to dialogue. (`0x0046db00` calls `0x0046cdd0` directly, confirming they're one evaluator
family, not independent.) The real `.lm_pc`-adjacent code this working name was pointing at lives
elsewhere in the same `0x0046c700`-`0x0046fcff` neighborhood, not at these four addresses.

**The "three `0x58`-byte streaming slots" at `0x0322d828`, labeled above (`+0x1c`'s row) as "HIGH: the
persona-line (`.lm_pc`) streaming slots," are re-identified: they are the `.ctdg_pc` active-conversation
pool already deeply traced in `spec-conversation-format.md` §4-§6 — see that document for the full
allocate (`0x0046a510`) → populate (`0x0046ac60`, validates the resident `.ctdg_pc` block's magic/
version/count, up to 30 turns, every speaker's live voice) → release (`0x0046a700`/`0x0046a950`, which
force-stops any live play record still pointing at the slot before freeing it) cluster, and the Lua chain
`audio_conversation_play`→`0x006dec90`→`0x00469f90`→`0x0046a510`. This pass also closes
`spec-conversation-format.md`'s own flagged-OPEN "how many slots" question: **exactly 3**, base
`0x0322d828`, stride `0x58`, confirmed by five independent loop bounds (`0x322d980 − 0x322d828 = 0x158 =
3×0x58`).

**`.lm_pc` itself is reframed, HIGH CONFIDENCE: per-soundbank "line situation"/alternate-take metadata,
not raw streamed audio.** The external-source file-id resolver `0x00468ae0` (called by the voice-line
`PostEvent` path above to produce the file id in its external-source array) consults the same registry
object, `0x01353760`, that `spec-tables-audio-radio.md` §17 already found `voc_sb_line_sit.xtbl`'s loader
populates by opening each boot-loaded soundbank's own `<name>.lm_pc` file. `0x00468ae0`'s body is a
weighted/randomized pick-one-of-several-alternate-takes selector with an 80-entry anti-repeat exclusion
bitmap — consistent with `.lm_pc` supplying "which alternate recordings exist for this line and how to
weight them," not the recordings themselves (those live in the persona's own voice soundbank media,
`spec-audio-format.md`'s territory, addressed by the chosen file id through the already-documented by-id
resolver). **OPEN, not byte-verified:** the exact `.lm_pc` `DMLV`-entry-to-in-memory-record byte mapping,
and whether `voc_sb_line_sit.xtbl`'s own boot-load gate (already flagged OPEN, `spec-tables-audio-radio.md`
§17) ever actually fires in the shipped game — if it never does, `0x01353760`'s table stays permanently
empty and every `0x00468ae0` call falls through to its 5-persona-database fallback instead.

**[CONFIRMED — disassembly: `voice_control.xtbl`'s full loader/unpacker round-trip; the four geometry
functions' real content; the `.ctdg_pc` pool's identity, exact size, and full member cluster; the Lua-to-
pool call chain; `0x00469100`'s non-Lua-reachability. HIGH CONFIDENCE: the `.lm_pc`-as-metadata reframing
(call-graph CONFIRMED, byte-level record shape not verified). OPEN: the `.lm_pc` byte-level mapping;
whether `voc_sb_line_sit.xtbl`'s boot-load gate ever fires in the shipped game.]** Full detail:
`scratchpad/wwise_voiceline_streaming_lm_pc_2026-10-08.md`.

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
| `+0x1c` | byte | ~~**streamed-line slot**, `0xff` = none...HIGH: the persona-line (`.lm_pc`) streaming slots of `review/scoping-audio.md` §2.2.~~ **CORRECTED 2026-10-08 (§1.2a): a play record's back-reference to which of (at most 3) concurrently-active scripted `.ctdg_pc` conversations posted it — NOT a `.lm_pc` streaming slot.** Released through `0x0046a700(slot)`, which finds the matching entry among the three `0x58`-byte entries at `0x0322d828` (the base of the `.ctdg_pc` active-conversation pool, `spec-conversation-format.md` §4-§6; match byte at entry `+0x50`) and frees it with `0x0046a950`, under the audio lock. The mechanism described here (release by matching byte) is unchanged and still accurate — only the *subject* ("what is a slot") was mislabeled. | `0x0045cf83`, `0x0045cc58` |
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

## 6. The "Audiolib Stream Manager" thread — the real IO-thread scheduling loop (unit U-W5)

**Task:** resolve `review/scoping-audio.md` §6.4 U-W5's explicit OPEN item — "the actual per-block I/O
scheduling loop... was not located by any of this pass's roots." Dispatched because the Wwise-internals
scoping pass searched Wwise's own code range (`0x00f22000`–`0x00fbffff`) and the `AK::IOThread` string-scan
candidates there, and found only small, leaf-like accessors.
**Status: RESOLVED.** Confidence: CONFIRMED for the control flow and every address/offset below (full
function reads, direct disassembly); HIGH/HYPOTHESIS/OPEN called out explicitly where the read stops short
of certainty. Not yet folded into `spec-audio-runtime.md` — this section is written close to that file's own
style so it can be spliced in directly (extends §1.4/§3.1/§4.5's existing "Audiolib Stream Manager"/
`0x0046b850` material, and §3.6's I/O-hook-object layout table) by whoever takes the next direct-edit pass.
Per this task's file-collision brief, `spec-audio-runtime.md` and `review/scoping-audio.md`'s §6.1–§6.7 are left
untouched by me; only `review/scoping-audio.md` §6.8 (the scoping-only section, explicitly editable) is
corrected in place, pointing here for detail.
**Method:** read-only capstone disassembly directly against the shipped `SaintsRowTheThird.exe` (GOG, DX9),
via a throwaway scratch script following the existing `tools/harnesses/exe_va.py`/`disasm_window.py`
pattern (not committed to the project). No Ghidra project was opened. No decompiler output anywhere below;
every function cited was read as a plain listing (mnemonics/operands), and this writeup is prose over that
listing, not a pasted dump.

---

#### 6.0 Headline finding

The scheduling loop does **not live inside Wwise's own code range at all.** It is entirely **game-side
(Volition-authored) code that `spec-audio-runtime.md` already names but explicitly had not read** — the
**"Audiolib Stream Manager" thread**, entry `0x0046b820`, started by the stream-device setup function
`0x0046b850` (already in §1.4/§3.1) only when `AK::StreamMgr::CreateDevice`'s device-settings scheduler-type
field is **2**. The public SDK name for value 2 is `AK_SCHEDULER_DEFERRED_LINED_UP` (HYPOTHESIS for the
name; CONFIRMED that `0x0046b850` refuses to create the device for any other value, per the existing spec).

This is architecturally expected, not a lucky guess: in deferred/lined-up mode, Wwise's own `StreamMgr`
internals are not responsible for actually performing device I/O — the low-level I/O *client* (the game)
is. That is exactly why the Wwise-internal string-scan/export-table search (`review/scoping-audio.md`
§6.1–§6.3) turned up only leaf-like accessors (~400 instructions / ~20 functions): there is no Wwise-internal
scheduling loop to find in this build, and that estimate does not need revising upward. The real loop was
always going to be on the game side of the boundary, at the one place the existing spec had already named
the thread but flagged its body as unread (`spec-audio-runtime.md` §4.5: *`"Audiolib Stream Manager" thread
(0x0046b850, entry 0x0046b820) | none read here`*).

---

#### 6.1 The outer loop — `0x0046b820` (CONFIRMED, full function, 12 real instructions)

Reached through the shared thread trampoline `0x00471000` (already named in §4.3/§4.5 for the Audio callback
thread and Main Audio Thread). Body:

- Checks byte `0x03171aa2`; if already set, skips the loop body entirely and returns (thread exits
  immediately without ever calling the dispatch function).
- Otherwise, loops: call `0x0046c040` (the per-tick dispatch, §6.2) → sleep **20 ms** (`push 0x14` then call
  the same sleep helper `0x00dc4440` that `spec-audio-runtime.md` §4.3 already uses with argument 1 for "a 1
  ms sleep", HIGH) → re-check byte `0x03171aa2` → loop while still clear.

**Correction/refinement to §3.4/§4.5's framing:** `0x03171aa2` is not a thread-private quit flag analogous to
the Audio callback thread's own descriptor-embedded byte (`0x01353824`, a field of the thread descriptor at
`0x013537f0`, tested in §4.3). It is the **same shared global** `spec-audio-runtime.md` §3.4 step 6 already
sets during shutdown ("`0x03171aa2` := 1; stop the callback thread `0x00471d10`"). CONFIRMED by direct
address match, read independently both places. So one write, during shutdown, stops **both** the Audio
callback thread's dispatch loop *and* this thread's scheduling loop — but unlike the callback thread, this
thread has no dedicated stop/wait routine of its own (no equivalent of `0x00471d10`): shutdown sets the byte
and moves on, it does not wait for this thread to actually exit. This is a genuine (minor) shutdown-ordering
hazard worth recording if a replacement reproduces the exact sequencing, not something this pass is scoped
to fix.

The thread polls every ~20 ms — a materially coarser cadence than the Main Audio Thread's 16 ms frame tick
(§4.5) or the Audio callback thread's 1 ms message-queue poll (§4.3). All three "sleep and re-check" loops
now share one documented idiom across the file.

#### 6.2 The per-tick dispatch — `0x0046c040` (CONFIRMED, full function read)

SEH-wrapped (standard MSVC `/EHsc` exception frame at entry/exit — stack-cookie check against `0x01344754`,
the `fs:[0]` chain link, etc. — this is compiler scaffolding, not business logic, and is skipped over below).
In order, all CONFIRMED by direct listing read:

1. **Acquires the I/O-hook object's lock**, object `0x032358f0` field `+0x8d04` = **`0x0323e5f4`** — the
   exact lock `spec-audio-runtime.md` §3.6 already names ("a lock `0x0323e5f4`"). This directly proves the
   Wwise-facing hook methods (§3.6's vtable slots) and this scheduling thread serialize against each other on
   the identical lock object — there is one lock guarding both directions of the crossing, not two.
2. **Services the 3-entry "medium" table** at **`0x0323d8f8`** via `0x00472470`: a straight 3-iteration pass
   over the table (stride `0x78`, matching `review/scoping-audio.md` §6.3's own figure exactly), each entry
   polled through a shared helper (`0x00d9e400`) and reset to idle (entry field `+0x00` := 0) on completion.
   **New: this pins the table to an exact object offset** — `0x0323d8f8` = hook object `0x032358f0` **+
   `0x8008`**, i.e. immediately after its two already-documented 16 KiB streaming buffers (`+0x08` and
   `+0x4008`, each exactly `0x4000` bytes: `0x08 + 0x4000 + 0x4000 = 0x8008`). §6.3 only had the table's raw
   address, not its place in the object layout.
3. **Pumps the hook object's two persistent 16 KiB streaming buffers** via `0x0046beb0` (full function read,
   CONFIRMED), called with the hook object itself as argument. For each of the two buffer slots — tracked by
   a pair of in-flight transfer-descriptor pointers at hook `+0x8cf8`/`+0x8cfc`, **a pair not previously
   catalogued in §3.6's object-layout table**, sitting immediately before the device id at `+0x8d00`:
   - if a transfer is in flight for that slot, poll it through a **game-supplied callback at global
     `0x031728a8`** (= audio-config field `+0x48` — one of the four pointers `spec-audio-runtime.md` §3.1
     step 3 already lists as "each must be non-zero or init fails", with no stated purpose until now);
   - on completion, hand the filled buffer to a voice-side consumer, `0x0045dfe0` (already in the documented
     voice/audiolib address range), and notify through a second game-supplied callback at **`0x031728a4`**
     (config `+0x44`);
   - zero the 16 KiB buffer (`0x00ea23b0`, a CRT-style block-fill, i.e. functionally a `memset`) and return
     the finished descriptor to the scheduler's own free list, hook `+0x8cc0` = **`0x0323e5b0`** — an exact
     match to §3.6's "free list (`0x0323e5b0`)";
   - pop the next pending node from the scheduler's queue, hook `+0x8cd8` = **`0x0323e5c8`** — an exact
     match to §3.6's "queue (`0x0323e5c8`)"; if one was waiting, start a new transfer into the now-free
     buffer through a **third** game-supplied callback at **`0x0317289c`** (config `+0x3c`).
4. **Checks the scheduler's own pending-count field** (16-bit, `0x0323daae`); if zero, skips straight to
   step 6 below.
5. Otherwise, **under a second, separate Lock/Unlock pair** — virtual slots `+0xc` (Lock) and `+0x10`
   (Unlock) on a C++ object embedded inside the hook object at `+0x8180` = **`0x0323da70`** (CONFIRMED by the
   call pattern: `this` is the object's own address, not a dereferenced pointer to it elsewhere — i.e. it is
   a true sub-object, not a separate global, and not previously catalogued in §3.6 at all) — **walks a
   32-byte-stride singly linked list of pending transfer nodes**: head index at scheduler `+0x3c`
   (`0x0323daac`), node-array base pointer at scheduler `+0x40` (`0x0323dab0`), and a parallel "next-index"
   array base at scheduler `+0x44` (`0x0323dab4`) — the classic array-index-as-pointer linked-list idiom.
   For each node whose "ready" flag (node `+0x1e`) is set:
   - dispatches it through `0x00472560`: if the node's medium-index byte (node `+0x1d`) names one of the 3
     built-in table entries (value in `[0, 3)`), it computes the matching entry in the `0x0323d8f8` table
     (same `0x78` stride as step 2) and calls `0x004721f0`, which drives the transfer through the **same two
     game-supplied callbacks as step 3** (`0x031728a8` to poll, `0x031728a4` to finish) plus a direct
     block-copy (`0x00ea27b0`, CRT-style `memcpy`) for an already-resident case; if the medium-index byte is
     the sentinel **`0xff`** ("generic"/no table entry), it calls the **same two callbacks directly**,
     bypassing the table;
   - either way, once the node's outcome is known, **posts status code 10 through
     `0x00471ae0(transfer_info, result)`** — an exact, CONFIRMED match to §3.6's already-documented "the
     read path... posts message code 10 through `0x00471ae0(transfer info, result)`", now traced all the
     way back to its true origin: this per-tick dispatch, on this thread;
   - clears the node's fields, marks it free (`+0x1d` := `0xff`), returns it to the scheduler's free list via
     `0x0046c4c0` (itself CONFIRMED to re-acquire the *same* scheduler Lock/Unlock pair around a call to
     `0x0046c5c0`, the actual free-list push), then advances to the next node and repeats from the top of
     this step.
6. Releases the hook object's lock (`0x0323e5f4`, same as step 1) and returns.

#### 6.3 How a request actually gets onto that queue — `0x0046bcb0` (CONFIRMED, full function read)

This is the body behind the Wwise-facing hook's `Read` vtable slot (`0x0046ba90`, already named in §3.6) —
the other end of the same crossing. Read in full, the "deferred" hook turns out not to be purely deferred in
practice:

1. It tries **up to three synchronous fast paths**, in order, each of which — if it succeeds — posts
   completion status **1** immediately via the **same** `0x00471ae0` and returns to Wwise synchronously,
   without ever touching the queue or waking the scheduler thread: **(a)** a bank-resident direct serve,
   `0x0046e1f0`; **(b)** a second resident/cache check, `0x00471ea0`, run under the hook object's own lock
   (`0x0323e5f4` again); **(c)** an in-memory-package direct copy, gated by `0x00472430` and performed by
   `0x00471ff0` — itself a lookup-and-`memcpy` against a resident package-index table rooted near globals
   `0x00320e460`/`0x00320e464`/`0x00320e470`/`0x00320e474`/`0x00320e47c`/`0x00320e480` (not expanded further
   in this pass — see open items). **Note:** `0x00471ff0` is the *same* function the per-tick dispatch (§6.2)
   also calls for the medium-index `[0,3)` case via `0x004721f0` — i.e. the resident-package fast path and
   the normal queued path share this one lookup routine.
2. **Only if all three fail** does it allocate a 32-byte node from the scheduler's free list (`0x0046c450` —
   the allocation-side mirror of §6.2's `0x0046c4c0`/free, confirmed to address the *same* embedded scheduler
   object at hook `+0x8180`), fill in the caller's transfer-info pointer, the file descriptor, a size/offset
   pair, a heuristics byte, and a medium-index byte (assigned by `0x00472520`), set the node's "ready" flag
   (`+0x1e`), and return success to Wwise **without posting any completion** — the request is now genuinely
   queued, and only the Audiolib Stream Manager thread's next tick (§6.2) will finish it and post status 10.
3. Both the fast paths and the enqueue path run under the same hook-object lock (`0x0323e5f4`) the scheduling
   thread's per-tick dispatch also takes, so "Wwise asks for a read" and "the scheduler thread is mid-tick"
   cannot race each other.

---

#### 6.4 What this resolves

- **`review/scoping-audio.md` §6.4 U-W5's OPEN item is closed.** The real per-block I/O scheduling loop is
  `0x0046b820` (outer sleep/dispatch, §6.1) together with its per-tick body `0x0046c040` (lock → poll medium
  table → pump buffers → drain the pending-request queue → dispatch each ready node → post completions →
  unlock, §6.2), both entirely game-side. The reason the Wwise-internal search never found it is architectural:
  with the device's scheduler type forced to 2, Wwise's own internal `StreamMgr`/`AK::IOThread` code has no
  scheduling loop of its own to contain in this build.
- **`spec-audio-runtime.md` §4.5's "Audiolib Stream Manager" thread row ("none read here") is now fully
  specified.** It makes no direct Wwise *API* calls (consistent with the existing row's framing), but it
  *does* call back into Wwise's I/O-completion machinery via the exact same `0x00471ae0`/status-10 path the
  callback thread already documents (§4.2/§3.6). §4.2's "code 10 from the I/O path" can now be stated
  precisely: code 10 is posted either **(a)** synchronously, from `0x0046bcb0` itself, on whichever thread
  calls Wwise's `Read` hook (Wwise's own thread, out of scope), when one of the three fast paths succeeds; or
  **(b)** from this thread's per-tick dispatch, when a genuinely queued transfer completes.
- **Three of the four previously-uninterpreted mandatory config callback pointers are now explained.** §3.1
  step 3 lists config fields `+0x3c`/`+0x40`/`+0x44`/`+0x48` → globals `0x0317289c`/`0x031728a0`/
  `0x031728a4`/`0x031728a8` only as "each must be non-zero or init fails," with no stated purpose. By direct
  call-site read: `0x0317289c` (`+0x3c`) **starts** a new transfer into a free buffer; `0x031728a4`
  (`+0x44`) is called **on completion**; `0x031728a8` (`+0x48`) **polls** an in-flight transfer. These three
  are the actual low-level transfer primitives both the scheduling loop (§2.3/§2.5) and the hook's own fast
  path (§3.1.a, via `0x004721f0`) dispatch through. **This is the game's own file-I/O layer underneath its
  Wwise adapter** — a different, and arguably more fundamental, crossing than the public
  `IAkFileLocationResolver`/`IAkLowLevelIOHook` vtable pair §3.6 documents, which is merely the interface
  Wwise itself calls; these four pointers are what *that* interface's implementation calls in turn, to
  actually move bytes. `0x031728a0` (config `+0x40`) was **not** observed called anywhere this pass's reach
  touched — left OPEN, see below. Which concrete game subsystem supplies these four pointers (almost
  certainly the `.vpp_pc` archive reader, given `spec-audio-format.md`'s own streaming-content framing, but
  not confirmed) was not traced — that needs following forward from the config-filling caller of
  `0x00462460` (named already in §3.1 as "called once from `0x00554d6f`"), a separate, bounded follow-up.

---

#### 6.5 New/refined object-layout facts (hook object `0x032358f0`, extends `spec-audio-runtime.md` §3.6's table)

| Offset | Field | Status |
|---|---|---|
| `+0x08` | buffer 0 (16 KiB) | already in §3.6 |
| `+0x4008` | buffer 1 (16 KiB) | already in §3.6 |
| `+0x8008` (`0x0323d8f8`) | 3-entry × `0x78` medium table | located by §6.3; **this pass pins it to this exact object offset** |
| `+0x8180` (`0x0323da70`) | **new** — the embedded "scheduler" sub-object (own vtable; slot `+0xc` = Lock, slot `+0x10` = Unlock, both CONFIRMED by call pattern, not by a public-SDK name) | new |
| scheduler `+0x34` | byte, a "queue non-empty"-style gate read before the list walk | new |
| scheduler `+0x3c` (`0x0323daac`) | 16-bit head index into the pending-node array | new |
| scheduler `+0x3e` (`0x0323daae`) | 16-bit pending count | new |
| scheduler `+0x40` (`0x0323dab0`) | pending-node array base pointer (32-byte stride) | new |
| scheduler `+0x44` (`0x0323dab4`) | parallel "next-index" array base pointer | new |
| `+0x8cc0` (`0x0323e5b0`) | free list | already in §3.6 |
| `+0x8cd8` (`0x0323e5c8`) | queue | already in §3.6 |
| `+0x8cf8`/`+0x8cfc` | **new** — two in-flight transfer-descriptor pointers, one per 16 KiB buffer slot | new |
| `+0x8d00` (`0x0323e5f0`) | device id | already in §3.6 |
| `+0x8d04` (`0x0323e5f4`) | lock | already in §3.6 |
| `+0x8d20` (`0x0323e610`) | open-file count | already in §3.6 |

Pending-node fields (32 bytes total; CONFIRMED by the read/write sites in §6.2/§6.3 above, exact semantics of a
couple of fields still soft — see open items): `+0x00` transfer-info pointer; `+0x04` file descriptor;
`+0x08`/`+0x0c` a size/offset-shaped pair; `+0x10`/`+0x14` a second size/user-data-shaped pair; `+0x18`
bytes-remaining; `+0x1c` a heuristics/priority byte; `+0x1d` medium-index byte (`0`..`2`, or `0xff` =
generic/no table entry); `+0x1e` "ready" flag (gates whether the per-tick dispatch processes the node at
all); `+0x1f` a secondary status/error flag.

**Note, to avoid confusion with an existing figure:** this 32-byte-stride pending-node array is a
**different** pool from the "24 request entries × `0x58` at `0x0323dd70`" that `spec-audio-runtime.md` §1.4
already names (initialised by `0x004723d0`, part of the resolver's `Open`/`Close`-side open-file-descriptor
bookkeeping). The two coexist in the same object: one tracks *open files*, the other tracks *pending
transfers*. This pass did not need to, and did not, verify how many entries the pending-node array actually
has (its base pointer is read from a field, not re-derived from a fixed table size the way the open-file
pool's size was already known).

---

#### 6.6 Confidence summary

- **CONFIRMED** (full function read, direct disassembly, this pass): `0x0046b820`, `0x0046c040`,
  `0x0046bcb0`, `0x0046beb0`, `0x00472470`, `0x004721f0`, `0x00472560`, `0x0046c4c0` — every control-flow
  edge, address, and offset attributed to these in §6.1–§6.3 above.
- **HIGH** (role inferred from call/structure shape, not independently cross-checked against a public-SDK
  layout the way §3.6 did for the hook vtables): the scheduler sub-object's two virtual slots really being
  Lock/Unlock; the exact parameter order into the three config callbacks; "medium" as the right word for the
  3-entry table's role; `0x00dc4440`'s argument being milliseconds (inherited HIGH from §4.3, reused here).
- **HYPOTHESIS**: `AK_SCHEDULER_DEFERRED_LINED_UP` as the public name for scheduler-type value 2 (already
  HYPOTHESIS in the existing spec at §3.2, unchanged by this pass); that the four config callbacks ultimately
  bottom out in the `.vpp_pc` archive reader (plausible given the content involved, not traced).
- **OPEN, not resolved this pass** — see the companion open-items file for the actionable list.

---

#### 6.7 Clean-room check

- Every address/offset is plain hex; no Ghidra project was opened anywhere in this task (capstone-only, via
  a throwaway scratch script following the project's own `tools/harnesses/exe_va.py` pattern, deleted after
  use); no decompiler output appears anywhere above, and the write-up is prose over listing reads, not a
  pasted disassembly dump.
- No name beyond what the existing specs already use (`spec-audio-runtime.md`, `review/scoping-audio.md`) or
  the public Wwise SDK's own documented scheduler-type constant (named as HYPOTHESIS, consistent with the
  existing file's own convention for SDK names) is introduced; no leaked Audiokinetic source or other leaked
  material was read or used.
- No spec file was edited by this pass (`spec-audio-runtime.md` untouched, per the file-collision brief);
  `review/scoping-audio.md` §6.8 only (the scoping-only section) was corrected directly, pointing here.

---

## 7. Container traversal + voice instantiation (unit U-W2b)

**Scratchpad only — not folded into `spec-audio-runtime.md` or `review/scoping-audio.md`.**
Unit per `review/scoping-audio.md` §7.8.4/§7.8.5 U-W2b: "the deeper polymorphic-dispatch cluster
(`0x00f52320`, `0x00f51cc0`, `0x00f51bd0`, `0x00f51eb0`, `0x00f52070`, `0x00f53c40`, `0x00f4fc30`,
`0x00f4fd80`, `0x00f76580`, `0x00f76710`, `0x00f75a30`) plus the `CAkRegisteredObj`/`CAkRegistryMgr`
cluster (`0x00f6e550`, `0x00f555e0` and its 20 callees) — ≈2.5–3k instructions."

**Method:** read-only capstone disassembly (x86, 32-bit) directly against the shipped
`SaintsRowTheThird.exe`, via a purpose-written linear-sweep function reader
(`func_disasm.py`, same heuristic as the scoping pass's own method note: forward sweep,
track furthest conditional-branch target seen, accept first `ret`/`retn` at or beyond it).
A second purpose-written script (`rtti_scan.py`) performs a direct MSVC RTTI
Complete-Object-Locator → TypeDescriptor byte-walk, restricted to `.?AVCAk` name strings
(same technique `scoping-audio.md` §7.1.1/§7.8.2 already used for the Vorbis classes). A third
script (`string_scan.py`) scans for plain `CAk`-prefixed ASCII strings outside RTTI. No
Ghidra project was opened. No decompiler output anywhere below. All scratch scripts and raw
listings live in this session's scratchpad (`func_disasm.py`, `rtti_scan.py`,
`string_scan.py`, `f_*.txt`, `rtti_out.txt`, `string_out.txt`), not committed to the project.

**Confidence key:** **CONFIRMED** = read directly in a listing this pass (bytes verified).
**HIGH** = structure/role inferred from the confirmed bytes, not independently cross-checked.
**HYPOTHESIS** = plausible reading, flagged as such, not otherwise supported.

**Upstream dependency (U-W2a):** not landed as of this pass (`spec-audio-runtime.md` has no
§7.8 yet; `review/scoping-audio.md` §7.8.4 still lists U-W2a as proposed-not-dispatched-complete).
Per the brief, I did not re-derive the event/action-list side. My own starting assumption,
stated explicitly: **the action-list walk resolves to a specific container/sound HIRC node
object and hands this cluster a pointer to it** (my "esi" below) **plus a stable per-action
context object** (my "ebp" below). This assumption is not independently verified against
U-W2a's own output and should be re-checked once U-W2a lands.

---

#### 7.1 The container-type dispatch root — `0x00f52320` (CONFIRMED, 203 instructions)

Byte-for-byte confirms the scoping note's count exactly: **203 instructions, 9 indirect
calls, 7 direct calls** (full listing in `f52320.txt`). Calling convention: two stack
arguments at entry, call them **arg_ebp** (loaded into `ebp`) and **arg_esi** (loaded into
`esi`); no `ecx`-as-this on entry (so `0x00f52320` itself is not a plain virtual-thiscall
method — more likely a free function or a static dispatcher called with explicit stack
args from further up the chain, consistent with it sitting at the EventManager-table /
action-list boundary described in §7.8.3/§7.8.4).

##### 7.1.1 Structure (CONFIRMED mechanics, HIGH-confidence role labels)

1. **`0x00f51bd0`** (direct call) — computes a clamped float (HIGH: a duration/eligibility
   value) into a caller-supplied buffer, using `arg_ebp`-relative data and a tunable constant
   at `0x0125e270`/`0x00111645c`. Returns a pointer to the computed value.
2. **`0x00f52070`** (direct call) — a boolean "proceed?" gate (§7.3 below). If false, jumps to
   a tail block (line ~182 of the listing) that composes a small integer code from
   `esi+0x81` and calls `0x00f52900` **directly** (not virtual) — see §7.4, this call is a
   **no-op in the shipped retail build**.
3. If the gate passes: loads `eax = *ebp` (vtable pointer) and calls **vtable slot `+0x7c`**
   (CONFIRMED: `mov edx,[eax+0x7c]; ...; call edx`) on `ebp`, passing
   `(this=ebp, float=<from step 1>, id=*(esi+8), mode=3, 0, 0)` by the push order read
   directly from the listing. This is the **first of three virtual calls on the same `ebp`
   object** (role, HIGH: a per-action-context "can this proceed" precondition test,
   analogous in shape — not claimed identical — to the already-documented game-boundary
   gate idioms in `spec-lua-api-behaviour.md` §7.3.7, i.e. a boolean precondition-then-act
   pattern, which is a generic idiom this codebase uses in multiple unrelated places, not
   evidence the two are the same mechanism).
4. If that call returns false: tail block at `0x00f524b1` — reads `*(esi+8)`, indexes
   `[that+0x50]`, composes a code, calls `0x00f52900` (again the no-op stub) **then** calls
   **vtable slot `+0x80`** on `ebp` (same object) with `(this=ebp, 0, 3, *(esi+8))` — i.e.
   even on this failure path, the object's "notify/finalize" virtual method still runs.
5. If that call returns true: re-reads `eax = arg_esi` (same value already in `esi`) and
   tests `[eax+0x1c] & 0xffff0000 == 0x00080000` (CONFIRMED byte-exact — matches the scoping
   note's "flags field... masked field equals the literal 0x80000" exactly).
   - **If equal:** direct call to **`0x00f51cc0`** (`ecx = &arg4-on-stack, this(register-wise
     implicit) = esi`) — see §2. This is the **fast-path for one specific concrete
     container/node kind** that is known statically (no virtual indirection needed).
   - **If not equal:** loads `ecx = *(esi+4)` (a **different object**, pointed to by a field
     of the node, not the node itself) and calls **vtable slot `+0x10`** on *that* object
     (CONFIRMED: `mov eax,[ecx]; mov eax,[eax+0x10]; ...; call eax`), passing
     `(this=*(esi+4), &a-16-byte-buffer, esi, ebp)`. This is the **generic polymorphic path**
     — the one actually implementing "get the next/selected child" differently per concrete
     container subclass (HIGH: this, not the fast path, is where the true
     switch/random/sequence/blend container-kind polymorphism A7's histogram implies lives).
6. **Both paths converge on `edi` = "the chosen child/leaf object"** (from `0x00f51cc0`'s
   return, or from the `+0x10` virtual call's return). `edi == 0` → no eligible child
   (tail block `0x00f52485`, calls vtable `+0x80` on `ebp` directly, then falls into the
   same composed-code tail as step 2's failure path, i.e. the request is abandoned cleanly).
7. **`edi != 0` (a child was chosen):** branches again on `*esi == 0`:
   - If true: calls **`edi`'s own vtable slot `+8`** (`mov edx,[edi]; mov edx,[edx+8]; call
     edx`) with `(this=edi, &buffer, 0, 0)`.
   - If false: reads `ecx = *(esi+0x78)`, `edx = [ecx+8]`, and calls **`edi`'s own vtable
     slot `+8`** again, same offset, different argument source. Either way the result is
     compared to `1` (`sete al`). (HIGH: this looks like a second, smaller "is this child
     actually eligible" re-check specific to the chosen child's own type, distinct from the
     `ebp`-level gate in step 3.)
   - **If that re-check is true:** writes a float (`arg_esi`'s own stack float, i.e. the
     `float` argument originally passed into `0x00f52320`) into `edi+0x104`, copies
     `esi+0x74`→`edi+0xf8`, and calls **`0x00f51e50`**(`edi`, `esi+0xc`, `esi+0x70`) —
     **this is the hand-off into voice instantiation, §7.3 below.** On return, `0x00f52320`
     itself returns `1` (success) — the function's *own* job ends here; everything past
     this point (actual slot allocation, priority, etc.) happens inside `0x00f51e50`'s own
     callees.
   - **If the re-check is false:** calls `edi`'s vtable slots `+0xc` then `+4` (two more
     virtual calls on the CHOSEN CHILD, not `ebp`), then **`0x00f32e70`**
     (CONFIRMED = `Free@MemoryMgr@AK`, the exported function) to free `edi` — i.e. the
     chosen child is **discarded without ever reaching voice instantiation** (HIGH: a
     "this concrete child turned out ineligible after a second look, free it and report
     failure" path) — then falls into the same composed-code tail as the other failure
     paths, ending in a **direct (non-virtual) call to `0x00f52900`** (the no-op stub).

##### 7.1.2 New direct callees found this pass, not individually named in the scoping note's
cluster list (all CONFIRMED by direct byte read; none contradict the scoping note, they are
simply one level deeper than its own "11 cluster addresses" list):
`0x00f51e50`, `0x00f52900`, `0x00f32e70` (already export-named as `Free@MemoryMgr@AK`).

---

#### 7.2 `0x00f51cc0` — the fast-path concrete handler (CONFIRMED, 152 instructions)

**Correction to the scoping note's own heuristic size** (`review/scoping-audio.md` §7.8.4 says
"direct call to `0x00f51cc0` (119 insns)"): a full direct read gives **152 instructions**,
ending cleanly at `0x00f51e44` (`ret`) with a small alignment gap before the next named
function (`0x00f51e50` at `0x00f51e50`). The function genuinely has **four separate early
`ret`s**, each with its own complete epilogue (not a single shared tail) — a normal MSVC
duplicate-epilogue pattern, not evidence of two overlapping functions. This is a HIGH
confidence refinement of the earlier HIGH-confidence heuristic estimate, not a contradiction
of it (scoping §7.8.0's own method caveat already flags its sizes as HIGH not CONFIRMED).

**Structure (CONFIRMED mechanics):**
- `ebx = *(eax+0x1c)` (a count, from the "arg4-on-stack" pointer passed in). If zero: early
  return (a trivial "no entries" case, value `2`).
- Otherwise allocates (`0x00f60640`, direct call — unexpanded, HIGH: a constructor/ctor-like
  helper for a per-call working object) and, if that fails, frees and returns `2`.
- A loop over `ebx`'s own small record array (18 (`0x12`→ wait, stride is `0x18`=24 bytes
  per the `add esi,0x18` seen in the listing) testing four fields per record
  (`+8`,`+0xc`,`+0x10`,`+0x14` against zero) to find the first record satisfying one of
  several conditions, dispatching by which condition matched to one of **three leaf
  handlers**: `0x00f604c0`, `0x00f605b0`, `0x00f60570` — each takes a combination of
  `edi+4`/`edi+0x14` bit-tested fields (`shr ecx,8; and cl,1` — a single bit extracted from
  byte 1 of a flags dword) and, in two of the three cases, a call to `0x00f51930` first
  (unexpanded, HIGH: builds a small 3-field request struct, reused identically in both call
  sites).
- **HIGH interpretation, not CONFIRMED by any name/string:** the bit tested via
  `shr ecx,8; and cl,1` (byte 1, bit 0, of a flags field at `edi+0x14`/`edi+4`) is
  structurally exactly where Wwise's publicly-documented Random/Sequence container
  "Shuffle"/"Standard" mode switch and "loop"/"avoid-repeat" bits would live (this is public
  Wwise **authoring-tool** terminology — found in Audiokinetic's own public user
  documentation and in `wwiser`'s public, open parsing of `CAkRanSeqCntr` params — cited here
  only as a naming reference, not derived from any leaked source; nothing in this binary
  confirms the real internal field name). This is the strongest circumstantial evidence that
  **`0x00f51cc0`'s fast path specifically implements the Sequence/Random container kind**,
  while the generic `+0x10` virtual path (§7.1.5) handles the other container kinds via normal
  polymorphism. **Flagged OPEN, not resolved** — see open items.
- No RTTI or plain-string evidence ties any class name to this function or its vtables
  (§7.5 below).

---

#### 7.3 Voice instantiation — `0x00f51e50` → `0x00f4fc30`/`0x00f4fd80` → `0x00f53c40` (CONFIRMED)

This is the chain that answers the task's core question: **where Wwise decides a specific
chosen child actually starts playing and allocates the record that will be handed to the
audio-source/codec layer.**

##### 7.3.1 `0x00f51e50` (CONFIRMED, 26 instructions) — register the chosen child as an active instance

- Takes the chosen child (`edi` from §7.1, here loaded as `esi`) as its first argument.
- **Appends `esi` onto a global doubly-linked list**: head `0x02cc5430` ("current/last"),
  tail-ish `0x02cc542c` ("first"), count `0x02cc5434` (incremented). Each node links via its
  own `+0xc` field. **This is a newly-identified Wwise-internal global — the list of
  currently-resolving/active play instances** — distinct from the already-documented
  game-side `0xa8`-stride voice pool (`spec-audio-runtime.md` §2.2), which lives one layer
  up at the Volition-adapter boundary. CONFIRMED mechanism (append-to-list bytes read
  directly); HIGH that "active instance list" is the correct role (supported by §7.3.3 below,
  which walks the *same* list to re-trigger continuation logic on existing entries).
- Calls `0x00f4f550` (`this = esi`, unexpanded — HIGH: an init/"OnAdded" callback on the
  child itself).
- Computes a boolean from `arg3 == 1` and calls **`0x00f4fc30`**(`this=esi`, `0`,
  `bool`, `arg2`) — the actual instantiation trigger, §3.2.

##### 7.3.2 `0x00f4fc30` / `0x00f4fd80` (CONFIRMED, 110 / 113 instructions) — build the instantiation request

Near-identical twins (same shape, different flag constant: `0x01000000` vs `0x02000000`),
both:
- Build a small packed request descriptor on the stack:
  `{+0x00: &(esi+8) sub-object, +0x04: mode flag (0x1000000 or 0x2000000),
    +0x08: tunable float constant, +0x0c: a second float (0.0, or a computed
    cross-fade-style value derived from `esi+0x90` in the `0xf4fd80` twin),
    +0x10: a cookie/pointer carried from the caller, +0x18: byte flag 1}`.
- Call **`ecx = *0x02cc5324`** (CONFIRMED: this global is a pointer VARIABLE — the object it
  points to is the actual allocator, §7.3.3) **`.call 0x00f53c40`** with that descriptor.
- If `0x00f53c40` returns non-null (`ebp`): **stores it into the child's own `esi+0x50`
  field** — i.e. **`esi+0x50` is the child/node's own "my instantiated playback-slot handle"
  field** — then calls **the child's own vtable slot `+0x44`**
  (`mov edx,[esi]; mov eax,[edx+0x44]; ...; call eax`), passing `(this=esi, cookie=edi, 0xe)`.
  **This virtual call is the clean hand-off point into whatever comes next (U-W4's domain
  — audio-source/codec setup) — I stopped here deliberately, per the brief.**
- If `0x00f53c40` returns null (pool exhausted, no stealable victim — §7.3.3): calls instead
  the child's **vtable slot `0`** (`mov edx,[ebx]; mov edx,[edx]; call edx` where
  `ebx = esi+8`) with `(this=esi+8, cookie, 1)` — a distinct failure/"can't instantiate"
  notification, on the **sub-object** at `esi+8`, not on `esi` itself.
- `0x00f4fd80` additionally gates on `esi+0xe4` bit `0x01` and `esi+0x54`/`esi+0x58` bit `0x02`
  (early-outs to a shared virtual-call-slot-0 failure path at `0x00f4fe8f`), and on success
  also sets `esi+0x58` bit `0x01` (vs. `0xf4fc30`'s `esi+0x58` bit `0xfe`-masked clear) —
  HIGH: these look like "already instantiating" / "already playing" guard bits that make
  the two twins mutually exclusive re-entry guards for the same child object, consistent
  with one being the **first-time instantiation** trigger (`0xf4fc30`, called from
  `0x00f51e50`, i.e. straight off the container-dispatch path) and the other being a
  **re-trigger for an already-active instance** (`0xf4fd80`, called from `0x00f51eb0`, §7.3.4).

##### 7.3.3 `0x00f53c40` (CONFIRMED, 100 instructions) — the voice/playback-slot pool allocator

This is the actual **"decide which specific slot starts playing" primitive**:

- `this = *0x02cc5324` (an engine-singleton object — **no RTTI, no plain-string name found**,
  see §7.5; this is a *third* singleton slot, sitting exactly between the export-confirmed
  `g_pRegistryMgr` (`0x02cc531c`) and `g_pRTPCMgr` (`0x02cc532c`) in the same contiguous
  global-pointer table — i.e. almost certainly a sibling Wwise-internal manager singleton,
  unnamed in the export table).
- Selects one of **two parallel sub-pools** on that singleton (`this+0x10` or `this+4`,
  picked by the caller's `mode` argument) — each a `{count-limit; begin; end; capacity}`
  pointer-vector.
- **Capacity check:** `(end-begin)>>2` (element count, 4-byte stride) compared against a
  hard limit at `this+0`. **If at/over capacity:** scans the vector for the entry with the
  **lowest priority** (float at `entry+0x1c`) and, if one is found, calls
  **`0x00f76580`** (recompute/"consume" its priority) then **`0x00f766b0`** then
  **`0x00f53a00`**(the vector, that entry) on it — **HIGH: this is Wwise's publicly-known
  "voice virtualization / lowest-priority instance is evicted to make room" behavior**
  (public Wwise *concept* name, cited for orientation only — the real internal
  method/field names are not recoverable here, see §7.5).
- **If under capacity:** allocates a fixed **`0x38`-byte** block via
  **`0x00f32da0`** (CONFIRMED export = `Malloc@MemoryMgr@AK`, pool id
  `g_DefaultPoolId` = `0x01346ed8`), then calls **`0x00f76550`** (construct/init the new
  0x38-byte record) to get `esi` = the new instance. If that fails too, falls into the same
  "find lowest-priority victim" path as the over-capacity case.
- Either way, once an instance `esi` is obtained: calls **`0x00f76580`**
  (CONFIRMED, 97 instructions — computes a priority/fade ramp from the instance's own
  `+0x8`/`+0x14`/`+0x18`/`+0x1c` fields and a per-manager tunable at global
  `0x02cc53a0`, storing the result back at `entry+0x1c` — **the exact field just compared
  during eviction above**, confirming the round-trip), then **`0x00f766b0`** (unexpanded),
  then **`0x00f53a00`** (push `esi` into the vector).
- After that: reads **a fourth global, `0x02cc5314+0x84`** and calls
  **`0x00f76710`** (CONFIRMED, 117 instructions — reads the original request descriptor's
  `+4`/`+8`/`+0xc`/`+0x14`/`+0x18`/`+0x10` fields, computes min/avg/max volume-ish floats
  into the new instance's `+4`/`+8`/`+0xc`, a loop-count at `+0`, a start-time via
  `fild`+epoch-add into `+0x14`, and a **quantized start position** via integer division by
  a global at `0x01346f04` into `+0x18` — HIGH: that global divisor is very plausibly "audio
  frame/buffer length in samples," i.e. this rounds the start offset up to the next audio
  frame boundary — then records the chosen child's identity into a small per-instance
  dedup set at `entry+0x24..+0x35`, growing it via a generic array-capacity helper
  `0x00f54010` — **HIGH: this is the publicly-documented "avoid repeating the last N plays"
  bookkeeping** for Random-type containers, a public Wwise authoring concept cited for
  orientation only). If `0x00f76710`'s result is `2`: the whole allocation is rolled back
  (`0x00f766d0` teardown-notify, then the generic folded stub `0x00754410` — **the same
  shared stub §7.8.4/§7.8.6 of `review/scoping-audio.md` already found reused across
  `CAkSrcBankVorbis`/`CAkSrcFileVorbis`'s vtables**, confirming it really is a
  broadly-shared default/trivial virtual-method body, not specific to the Vorbis classes —
  then `0x00f32e70` = `Free@MemoryMgr@AK` to free the block, returning `0`).
- On full success: pushed into a second tracking vector (bounds-checked the same way), a flag
  (`entry+0x30 = 1`) set, and **`esi` (the new/stolen instance pointer) is returned** — this
  return value is exactly what §7.3.2 stores into the child's `+0x50` field.

**This is the voice-instantiation decision point the task asked for.** Everything downstream
of the `+0x44` virtual call made on the child in §7.3.2 is explicitly out of scope (U-W4).

##### 7.3.4 `0x00f51eb0` (CONFIRMED, 52 instructions) — re-evaluate already-active instances

A second entry point, **not reached from `0x00f52320` in this pass's trace** (no caller
found for it among the functions read), that walks the **same global active-instance list**
(`0x02cc542c` head, `+0xc` next-link) filtering by three key fields
(`entry+0x60`/`+0x68`/`+0x48` matched against caller-supplied filters) and, for matches,
dispatches on a 3-way mode argument to one of: `0x00f4fd80` (mode 0 — the
"already-active, re-trigger" twin from §7.3.2), `0x00f4ff80` (mode 1, unexpanded), or
`0x00f50520` (mode 2, unexpanded). **HIGH: this is the broadcast mechanism for propagating a
later change (e.g. a switch/state change, or a stop/pause action targeting the whole node)
to children that were already instantiated earlier** — i.e. it is the maintenance
counterpart to §7.3.1–§7.3.3's first-time instantiation path, likely called from the
RTPC/switch/state side (U-W3) or from a stop/pause action handler upstream, not from the
`0x00f52320` container-dispatch path itself. Flagged as **present in the cluster but not
directly wired to my traced root** — see open items.

---

#### 7.4 `0x00f52900` — a retail no-op (CONFIRMED)

**`0x00f52900` is exactly 3 bytes: `c2 10 00` = `ret 0x10`.** Every call site in
`0x00f52320` passes it 4 stack dwords (`0x10` bytes) which it discards unread, returning
immediately. **CONFIRMED by direct byte read** (the call targets are literal `E8` relative
calls, so whatever sits at that exact address is what executes — there is no ambiguity about
function-start alignment here). **HIGH: this is the same already-documented pattern as the
folded `AK::Monitor` stubs (`0x006b89a0`/`0x0066ac20`) and the "pure-call" stub
(`0x00ea2778`) noted in `review/scoping-audio.md` §7.1.1/§7.1.4/§7.1.5/§7.8.3/§7.8.6** — a
profiling/telemetry call left in the retail code path but reduced to nothing because this
build has no profiling instrumentation. This extends that already-established pattern to a
fourth address and a new call site family (the container-dispatch failure/abandon paths).

---

#### 7.5 `CAkRegisteredObj` / `CAkRegistryMgr` cluster (CONFIRMED)

##### 7.5.1 `CAkRegistryMgr::GetObjAndAddref` — `0x00f6e550` (CONFIRMED, 27 instructions)

Full mechanism read directly (minor count correction to the scoping note's "23
instructions" — my direct read gets 27; same trivial, no-further-calls shape either way,
not a substantive disagreement, just the usual heuristic-vs-exact difference already
flagged by scoping §7.8.0's own caveat):

- `this = ecx` (the `CAkRegistryMgr`), `esi = AkGameObjectID` (the one stack argument).
- Computes `esi mod 193` via the standard MSVC reciprocal-multiply idiom
  (`eax = esi * 0x15390949`; `edx = (high32) >> 4`; `edx = edx * 0xc1`; `eax = esi - edx`) —
  **CONFIRMED: a 193-bucket hash table**, base at `this+0x24`.
- Walks the bucket's singly-linked chain (`node+4` = stored key, compared to `esi`; `node+0`
  = next) until a match or the end.
- **On match:** `eax = node+8` (one more level of indirection: `node+8` holds a pointer to
  the actual object) → `eax = *(node+8)` → **`++*(eax+0x54)`** (increments the found
  object's own refcount field, confirming `CAkRegisteredObj+0x54` = refcount) → returns that
  object pointer.
- **On miss:** returns `0`.

This is an exact match to the exported signature
`GetObjAndAddref(AkGameObjectID) -> CAkRegisteredObj*` — a classic refcounted chained hash
map keyed by the 32-bit game-object id. **CONFIRMED mechanism; CONFIRMED the refcount field
is at offset `+0x54` of the returned object.**

##### 7.5.2 `~CAkRegisteredObj` — `0x00f555e0` (CONFIRMED, 44 instructions)

Short, trivial-looking destructor body:
- Calls `0x00f55590` first (unexpanded — HIGH: a base-class/member sub-object destructor,
  given it runs before any of this function's own field frees).
- If `*this != 0`: calls `0x00f55510` (unexpanded — HIGH: unlinks this object from the
  registry hash table found in §7.5.1, since that is the only other place a `CAkRegisteredObj`
  needs to be found/removed by), then, if `*this` is still non-null afterward, frees it via
  `0x00f32e70` (`Free@MemoryMgr@AK`, pool id `g_DefaultPoolId`) and zeroes the field.
- Frees `+0x2c` the same way if non-null (and zeroes `+0x2c`/`+0x30`/`+0x34`).
- Frees `+4` the same way if non-null (zeroes `+4`, clears a word at `+8`).

**This function's own body is far smaller than the scoping note's "909 instructions,
21-function closure" for the whole `~CAkRegisteredObj` cluster** — consistent with that
figure describing the *closure* (the destructor plus everything reachable from
`0x00f55590`/`0x00f55510`/etc., most of which I did not expand this pass), not this single
function. `0x00f55590` and `0x00f55510` were **not expanded this pass** — flagged as open
follow-up if a future pass wants the full closure.

##### 7.5.3 `0x00f70fa0` (CONFIRMED, 147 instructions, 11 direct calls)

Matches the scoping note's "147 instructions, 10 callees" closely (11 vs 10 — my count
includes one call target reached from two different sites, which the note may have
deduplicated differently; not a substantive disagreement). Confirmed role, refined from the
scoping note's HIGH-confidence guess:

- `this = ebp` (**not** `CAkRegisteredObj` itself — `ebp`'s own fields are accessed at
  offsets `+0x614` and `+0x308`, far larger than `~CAkRegisteredObj`'s own tiny footprint in
  §7.5.2, so this is a **different, larger manager-type object** reached *from* the
  registered-object teardown path, most plausibly the RTPC manager given the already-
  exported `CAkRTPCMgr::SetRTPCInternal` takes a `CAkRegisteredObj*` argument — consistent
  but not proven; no RTTI/string ties the name here either).
- `esi` = the one stack argument (the game-object id being torn down).
- **Walks and erases matching entries from TWO embedded hash/tree structures**: one at
  `this+0x614` (erase helpers `0x00f6ec40`/`0x00f6f690`/`0x00f6fac0`/`0x00f6ecd0`, matching
  entries by `entry+0xc == esi`; this map's bucket count is also **193**
  (`cmp eax,0xc1` at `0x00f71139`), the same modulus as §7.5.1's registry table — HIGH that
  this is a sibling per-game-object value map using the identical hashing scheme), and a
  second at `this+0x308` (helpers `0x00f6e3f0`/`0x00f6f5a0`/`0x00f6ed20`/`0x00f6ec00`/
  `0x00f6ee50`, matching entries by `entry+8 == esi`, with an explicit `memmove`-style
  compaction (`rep movsd`) after an erase — a sorted/contiguous array rather than a hash
  chain).
- **CONFIRMED**: both structures are purged of every entry referencing the torn-down game
  object. **HIGH**: these are per-registered-object override-value stores (RTPC values /
  switch or state values), matching the scoping note's own "unregister this object from the
  RTPC/switch-value maps on teardown" hypothesis — now confirmed as a real, specific
  two-structure purge rather than a single generic cleanup call.

---

#### 7.6 RTTI / string check on the three unresolved vtable offsets — negative result (CONFIRMED)

Per the scoping note's own suggestion ("the same RTTI-walk technique that worked for the
Vorbis classes... should be tried first on this address range"), I ran a full binary-wide
RTTI scan (`rtti_scan.py`) for every `.?AVCAk...@@` mangled TypeDescriptor name, and a
separate plain-ASCII scan (`string_scan.py`) for any `CAk`-prefixed string outside RTTI
mangling.

- **RTTI scan: 30 classes found, all of them plugin/effect/codec/source-layer classes**
  (`CAkCompressorFX(Params)`, `CAkDelayFX(Params)`, `CAkExpanderFX(Params)`,
  `CAkGuitarDistortionFX(Params)`, `CAkFDNReverbFX(Params)`, `CAkParametricEQFX`/
  `CAkParameterEQFXParams`, `CAkPeakLimiterFX(Params)`, `CAkRoomVerbFX(Params)`,
  `CAkFXSrcSine`/`CAkFxSrcSineParams`, `CAkModalSynth(Params)`, `CAkToneGen(Params)`,
  `CAkSrcBankVorbis`, `CAkSrcBaseEx`, `CAkVPLSrcNode`, `CAkVPLNode`, `CAkSrcFileVorbis`,
  `CAkSrcFileBase`, `CAkFXSrcAudioInput(Params)`). **Zero container/parameter-node classes**
  (no `CAkSwitchCntr`/`CAkRanSeqCntr`/`CAkLayerCntr`/`CAkBlendCntr`/`CAkActorMixer`/
  `CAkSound`/`CAkParameterNode*`/any Music-Engine class) appear anywhere in this binary's
  RTTI.
- **Plain-string scan: only the 5 already export-confirmed names** (`CAkRegisteredObj`,
  `CAkRegistryMgr`, `CAkRTPCMgr` and their two mangled method signatures) — no other `CAk`
  substring exists anywhere in the file outside the export table's own mangled names.

**CONFIRMED, exhausted result: the vtable offsets `+0x7c`/`+0x80` (on `ebp`) and `+0x10`
(on `*(esi+4)`) from §7.1, and the fast-path-vs-generic-path split at `0x00f51cc0`, cannot be
tied to a real class name by any text-based method available in this binary.** This extends
(does not merely repeat) the already-established §7.8.2/§7.8.3 finding that Wwise manager
classes lack RTTI in this build — this pass shows the **node/container/parameter-node
hierarchy also lacks RTTI**, not just the managers; only the bottom-layer plugin/effect/
source/codec classes carry it. This is a genuine, confirmed negative, not an unattempted gap.

---

#### 7.7 New globals identified this pass (CONFIRMED addresses, HIGH roles)

| Address | Role |
|---|---|
| `0x02cc5430` | head ("current/last") of the global active-instance list |
| `0x02cc542c` | head ("first") of the same list, walked by `0x00f51eb0` |
| `0x02cc5434` | live count of that list |
| `0x02cc5324` | pointer to the voice/playback-slot pool-allocator singleton (`this` for `0x00f53c40`) — sits exactly between export-confirmed `g_pRegistryMgr` (`0x02cc531c`) and `g_pRTPCMgr` (`0x02cc532c`) |
| `0x02cc5314` | pointer to a fourth, distinct manager consulted by `0x00f76710` (via its own `+0x84` field) |
| `0x02cc53a0` | `this`-like context passed into `0x00f4eed0`/`0x00f4f450`/`0x00f4f3b0`/`0x00f4f130` from `0x00f76580`/`0x00f76710` (a shared tunables/curve-evaluation object) |

None of these have export names or RTTI; addresses and read/write sites are CONFIRMED by
direct byte read, roles are HIGH.

---

#### 7.8 Summary of the end-to-end chain (plain hex, no decompiler, no auto-names)

```
(U-W2a, not re-derived) action resolves a container/sound HIRC node  ──►  arg_esi, arg_ebp
0x00f52320  container-type dispatch root (CONFIRMED, 203 insns, 9 indirect calls)
  ├─ 0x00f51bd0   duration/eligibility float (CONFIRMED, 78 insns)
  ├─ 0x00f52070   voice-limit/threshold gate (CONFIRMED, 80 insns) ──fail──► 0x00f52900 (no-op stub)
  ├─ vtable[ebp+0x7c]   "may this node proceed" precondition (vtable owner UNRESOLVED, §7.6)
  ├─ flags test  *(esi+0x1c) & 0xffff0000 == 0x80000 ?
  │    ├─ yes → 0x00f51cc0   fast-path concrete handler, HIGH = Sequence/Random container (CONFIRMED, 152 insns)
  │    └─ no  → vtable[*(esi+4)+0x10]   generic "pick child" (vtable owner UNRESOLVED, §7.6)
  ├─ child re-check  vtable[edi+8]  ──fail──► free edi (0x00f32e70), abandon
  └─ success → 0x00f51e50   register chosen child as active instance (CONFIRMED, 26 insns)
                 ├─ append to global active-instance list (0x02cc5430/2c/34)
                 ├─ 0x00f4f550   child init/OnAdded (not expanded)
                 └─ 0x00f4fc30   build request descriptor (CONFIRMED, 110 insns)
                        └─ 0x00f53c40   ***VOICE-SLOT POOL ALLOCATOR*** (CONFIRMED, 100 insns)
                               ├─ capacity check vs. a hard count limit
                               ├─ over limit → evict lowest-priority instance (0x00f76580/0x00f766b0/0x00f53a00)
                               ├─ under limit → Malloc@MemoryMgr (0x00f32da0, CONFIRMED export), init (0x00f76550)
                               ├─ 0x00f76580   compute/store priority (CONFIRMED, 97 insns)
                               ├─ 0x00f76710   compute volume/loop/start-time/dedup-history (CONFIRMED, 117 insns)
                               └─ returns the instance pointer, or 0 (rolls back, Free@MemoryMgr 0x00f32e70)
                 └─ store returned pointer into child+0x50; call child's vtable[+0x44]
                      ══════════ HAND-OFF TO U-W4 (audio-source/codec) — STOPPED HERE ══════════

(separately, not reached from 0x00f52320 in this trace)
0x00f51eb0  re-evaluate already-active instances (CONFIRMED, 52 insns) — walks the SAME
            global list, dispatches by mode to 0x00f4fd80 / 0x00f4ff80 / 0x00f50520

CAkRegisteredObj / CAkRegistryMgr cluster (export-confirmed classes):
0x00f6e550  CAkRegistryMgr::GetObjAndAddref — 193-bucket hash, refcount at +0x54 (CONFIRMED, 27 insns)
0x00f555e0  ~CAkRegisteredObj — frees 3 sub-allocations, calls 0x00f55590/0x00f55510 (CONFIRMED, 44 insns)
0x00f70fa0  purges 2 per-object value maps (+0x614 193-bucket hash; +0x308 compacted array) (CONFIRMED, 147 insns)
```

---

## 8. HIRC load: the bank-chunk reader and the per-kind construction dispatch (unit U-W1)

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process), Wwise-internals workstream,
U-W1 ("bank/HIRC load") per `HANDOFF.md` §27.H-1 item 2 and `review/scoping-audio.md` §8.4's
U-W1 scoping. This section resolves the specific open question §8.4 flagged: the per-HIRC-kind
factory dispatch inside the 65-instruction function `0x00f5acb0` that makes up to 7 indirect
calls a direct-call BFS could not follow.
**Scope:** how `LoadBank` turns raw bank bytes into Wwise's in-memory HIRC object graph — the
top-level bank-chunk reader, the real per-HIRC-kind construction dispatch (a different function
from the one the scoping note flagged), and what `0x00f5acb0` actually is. Does not open the
per-kind property-parsing bodies themselves (flagged OPEN, see the companion open-items file).
**Relationship to other specs:** `spec-audio-runtime.md` §3.1/§1.4 already specifies the
game-side wrapper (the "Soundbank Process" task, `LoadBank`/`UnloadBank` call shapes, the bank
record, the bank callbacks) — cited here, not re-derived. `review/scoping-audio.md` §8
(particularly §8.1 entry points, §8.2 export-confirmed classes, §8.4 U-W1) is the scoping note
this section refines; a direct correction has been made to §8.4 U-W1 in place (see that file's
own text) because this section's finding supersedes its "per-HIRC-object-kind dispatch" reading
of `0x00f5acb0`.
**Method:** read-only `pefile` + `capstone` disassembly against the shipped
`SaintsRowTheThird.exe` (GOG build), using the project's `tools\harnesses\exe_va.py` pattern
extended with purpose-written scratch scripts (not committed). No Ghidra project was opened. No
decompiler output anywhere below. Function boundaries were found by locating `ret` instructions
followed by a fresh prologue (and, where present, `int3` padding), not by a fixed byte-window
heuristic. Three independent whole-block scans were run over the entire statically-linked Wwise
code range (`0x00f22000`–`0x00fc0000`, 40,657 instructions total) looking for a flat
kind-indexed dispatch table: (1) `call`/`jmp` through a scale-4/8 SIB memory operand, (2) a
cascade of `cmp reg, imm` / conditional-jump pairs on the same register (both an 80- and a
500-instruction window), (3) `imul reg, reg, N` struct-stride indexing followed by an indirect
call. (1) and (2) returned zero genuine hits; (3) is what found the real registry table
described in §A.4 below (which turned out not to be the kind dispatch). The real kind dispatch
(§A.2) was found by a different route: locating the literal `HIRC` FourCC (`0x43524948`) as a
`cmp` immediate in `.text` and reading forward/backward from that one hit.
**Confidence key:** **CONFIRMED** (read directly in a listing this pass, address and bytes
checked); **HIGH** (one or two structural inferences from a CONFIRMED read, not independently
re-verified); **HYPOTHESIS** (plausible, flagged explicitly, not independently checked).

---

### 8.1 Headline finding

**`0x00f5acb0` is not the per-HIRC-kind dispatch.** It is a generic, kind-agnostic
reference-release routine shared, verbatim, by at least 22 different classes' virtual tables
(confirmed by a literal scan of `.rdata`: the dword `0x00f5acb0` occurs 22 times, at addresses
scattered from `0x0126d9e8` to `0x0127d858`, each one inside a different contiguous run of
code-pointer-shaped dwords — i.e. 22 distinct vtables plug this exact function in at whichever
slot their class's "release" method occupies). Its 7 indirect calls are not a kind switch at
all: 4 are a critical-section lock/unlock pair, entered twice (recursively) because two
different pieces of logic — a refcount check and an index-removal step — were each written
(or inlined) with their own lock guard around the *same* critical section; the other 3 are
ordinary C++ virtual calls (vtable offset `+0x60` on two sub-objects, and vtable slot 0 on the
object itself) whose polymorphism was already resolved when each object was *constructed* —
this function works identically no matter which concrete HIRC-node class it is called on.

**The real per-kind dispatch is a different, separate function: `0x00f588c0`.** It is a dense
19-entry MSVC switch-jump-table (not a function-pointer array, not a vtable) keyed directly by
the HIRC item's 1-byte kind field, located at jump-table VA `0x00f58b50`. It sits inside the
bank's top-level chunk reader, reached from the `HIRC` chunk case of a FourCC switch that also
recognises `STMG`/`DATA`/`STID` (and, by the same pattern, almost certainly `BKHD`/`DIDX`,
not read this pass). This dispatch, and the chunk reader that calls it, were never found by the
scoping note's direct-call BFS because **nothing in the entire binary calls them, or stores
their address anywhere, by any means a static byte/instruction scan can see** (§A.3) — they are
reached only through a genuinely indirect, runtime-computed call, almost certainly a
polymorphic "bank job" object's own method running on Wwise's internal Bank Manager thread.

### 8.2 The real dispatch: `0x00f588c0`, the per-item HIRC-kind switch — CONFIRMED

**Where it's called from.** A FourCC-comparison block (part of a larger chunk-list-walking
function; the clearest clean function boundary found around it starts at `0x00f59416` and ends
at `0x00f59ac9`, though — see §A.3 — this function itself has no discoverable caller) reads a
chunk tag at `0x00f59900` and compares it against four literal FourCC dwords in ascending order:

| Compare (VA) | Literal (LE dword) | ASCII | Branch |
|---|---|---|---|
| `0x00f59904` | `0x474d5453` | `STMG` | `ja`/`je` to `0x00f59a0b`/`0x00f599f5` |
| `0x00f59915` | `0x41544144` | `DATA` | `je 0x00f59957` |
| `0x00f5991c` | `0x43524948` | `HIRC` | `je 0x00f59941` |
| `0x00f59923` | `0x44495453` | `STID` | `jne 0x00f59a19`, else falls into the `STID` handler |

CONFIRMED by direct byte read; these are the public, well-documented Wwise `.bnk` top-level
chunk tags (not secret — the chunk-tag bytes are literal ASCII visible in every shipped bank
file; naming them is not a derivation from any leaked source). The `HIRC` case, at
`0x00f59941`, loads the chunk's item count/size and the chunk's own `this`-context, then:

```
0x00f59941: eax = [esp+0x10]         (a count/size value from the chunk header)
0x00f59945: ecx = [ebx]               (the chunk's raw data pointer)
0x00f59949: ecx(this) = edi           (the bank-load context)
0x00f5994b: call 0x00f588c0           (the per-item HIRC dispatcher, below)
```

CONFIRMED by direct listing. Note this is a different part of the pipeline from the "DATA"
case at `0x00f59957`, which turned out to hold its own, separate, 4-entry dense jump table
(`jmp dword ptr [ebp*4 + 0x00f59acc]`) for a DATA-chunk sub-mode — a second, smaller confirmation
that this build's compiler *does* use MSVC jump tables when it wants to, which makes the
earlier whole-block scan's zero hits for the *kind* dispatch (§A.3) a real, meaningful negative
and not a tooling blind spot — the HIRC dispatch's own jump table (§A.2.1) uses the exact same
`jmp dword ptr [reg*4 + literal]` shape and the same scan found it immediately once pointed at
the right address.

#### 8.2.1 Inside `0x00f588c0` — the kind switch itself

`0x00f588c0` (223 instructions, clean `sub esp,0x10 / push ebx,ebp,esi,edi` prologue and
matching epilogue, `ret 8`) reads one HIRC item per call. After an item-count/bound check and a
5-byte read (`call 0x00f55a20`, a generic stream-read helper) into a local buffer, the kind byte
sits at buffer offset `+0`:

```
0x00f58939: al  = byte [esp+0x18]      ; the HIRC item's kind byte, just read from the stream
0x00f5893d: ecx = zero_extend(al)
0x00f58940: ecx = ecx - 1               ; kind values are 1-based
0x00f58941: cmp ecx, 0x12                ; 0x12 = 18  -> valid kind range is [1,19]
0x00f58944: ja  0x00f58acb               ; out of range -> shared fallback
0x00f5894a: jmp dword ptr [ecx*4 + 0x00f58b50]   ; <-- the dispatch
```

CONFIRMED by direct listing and by reading the 19 raw dwords at the jump-table VA
`0x00f58b50` directly (not inferred from the branch targets alone):

| Kind (1-based) | Table index | Target VA | Construction helper called | Extra arg (sub-manager) |
|---|---|---|---|---|
| 1 | 0 | `0x00f589f9` | `0x00f58470` | none |
| 2 | 1 | `0x00f58979` | `0x00f57c10` | `ebx` (caller-supplied) |
| 3 | 2 | `0x00f58951` | `0x00f58610` | none |
| 4 | 3 | `0x00f58965` | `0x00f58700` | none |
| 5 | 4 | `0x00f5898e` | `0x00f57d20` | `[0x2cc5310]` |
| 6 | 5 | `0x00f589a9` | `0x00f57dd0` | `[0x2cc5310]` |
| 7 | 6 | `0x00f589de` | `0x00f57f30` | `[0x2cc5310]` |
| 8 | 7 | `0x00f58a0d` | `0x00f58530` | none |
| 9 | 8 | `0x00f589c4` | `0x00f57e80` | `[0x2cc5310]` |
| 10 | 9 | `0x00f58acb` | *(shared fallback — see §A.2.2)* | — |
| 11 | 10 | `0x00f58acb` | *(shared fallback)* | — |
| 12 | 11 | `0x00f58acb` | *(shared fallback)* | — |
| 13 | 12 | `0x00f58acb` | *(shared fallback)* | — |
| 14 | 13 | `0x00f58a21` | `0x00f57fe0` | `[0x2cc5310] + 0xfa0` |
| 15 | 14 | `0x00f58a40` | `0x00f58090` | `[0x2cc5310] + 0x15e0` |
| 16 | 15 | `0x00f58a61` | `0x00f58140` | `[0x2cc5310]` |
| 17 | 16 | `0x00f58a7c` | `0x00f58200` | `ebx` (caller-supplied) |
| 18 | 17 | `0x00f58a91` | `0x00f58310` | `[0x2cc5310] + 0x1900` |
| 19 | 18 | `0x00f58aaf` | `0x00f583c0` | `[0x2cc5310] + 0x1c20` |

Every row is CONFIRMED by direct listing of the case body and the jump-table dword. The
"extra arg" column is CONFIRMED to be *passed* at each call site; whether it is literally a
pointer to a distinct sub-manager object, versus a region inside one larger aggregate, is
addressed in §A.2.3.

**Kinds 10–13 are deliberately routed to the same fallback as an out-of-range value** (the
table was never auto-generated per-case; this is 4 explicit, identical jump-table entries, not
a gap). This is a striking structural match — found completely independently of it — to the
A7 wwiser pilot's own content finding, already in `HANDOFF.md` §27.H-2: *"only 12 of Wwise's
~20+ node kinds are used at all — no Music Segment/Track/Switch/Playlist"*. Four consecutive,
never-implemented kind slots is exactly what "no Music Segment/Track/Switch/Playlist" (four
kinds) predicts if those four kinds occupy kind-values 10–13 in this SDK's own numbering —
**HIGH**, not CONFIRMED, since naming which kind-number is literally "Music Segment" versus
"Music Track" etc. relies on the public/community Wwise HIRC-kind numbering (the same
`wwiser`-style taxonomy A7 itself used, per `HANDOFF.md` §27.H-2's own routing: Team-A-internal
use of community docs of unknown provenance is permitted, never cited to Team B) rather than
anything this binary names directly — this binary carries no string or RTTI name for any
individual kind value. The **structural** fact (exactly 4 consecutive kind-values share the
"unregistered" fallback, nothing else does) is CONFIRMED; the claim that those 4 values are
specifically 10/11/12/13 is CONFIRMED (read directly from the table); the claim that they
*correspond to* Music Segment/Track/Switch-Container/Playlist-Container specifically is HIGH,
resting on the numeric coincidence with public taxonomy plus A7's independent content count
agreeing on "exactly four, and no others, missing." The table size (19, not ~26) is also
consistent with — not proof of — `review/scoping-audio.md` §8.6's own flagged HYPOTHESIS that
this SDK generation predates Wwise's Modulator object kind (which public release notes place
in a materially later SDK generation): a 19-entry table has no slot at all for Modulators,
which would explain their absence as an SDK-generation fact rather than a content choice,
exactly as §8.6 already guessed without this evidence.

#### 8.2.2 The shared fallback, `0x00f58acb` — CONFIRMED

```
0x00f58acb: ecx = [0x02cc536c]          ; a global, independent of [0x2cc5310]
0x00f58ad1: test ecx; je 0x00f58aea
0x00f58ad5: push ebx; push ebp; push &stackbuf; call ecx    ; an optional registered callback
0x00f58ade: esi = eax (status)
...
0x00f58aea: cmp byte [esp+0x24], 0
0x00f58aef: jne 0x00f58b06
0x00f58af1: cmp al, 0x0a ; cmp al,0x0b ; cmp al,0x0c ; cmp al,0x0d  (al = the ORIGINAL kind byte)
            je 0x00f58b01 for each
0x00f58b01: byte[esp+0x24] = 1
0x00f58b06: ... (advance past the item's declared size and continue the loop)
```

If a non-null function pointer is present at the global `0x00f58acb` reads
(`[0x02cc536c]`), it is called with the same `(ebx, ebp, &stackbuf)` argument shape as every
other kind's construction helper — **HIGH**: this is a registrable extension point for
otherwise-unimplemented kinds (a generic "unknown item" callback), not specific to kinds
10–13; it is simply the only place that path is taken. If no callback is registered (the
observed case in this build — nothing was found writing a non-null value to `[0x02cc536c]`
anywhere in the scanned Wwise range, though that write was not hunted for exhaustively outside
it), the function falls back to a second, independent check: the original (non-decremented)
kind byte is compared against the literal bytes `0x0a`/`0x0b`/`0x0c`/`0x0d` (10/11/12/13 — the
same four values, read a second, unrelated way) to set a local flag, after which the loop
simply advances past the item without constructing anything. CONFIRMED by listing; the
*purpose* of the flag at `[esp+0x24]` was not traced further (OPEN).

#### 8.2.3 The global `[0x2cc5310]` and the shared "find-or-create" helper `0x00f579f0`

Every non-fallback kind's construction helper (6 of the 15 read in full: kinds 1, 2, 3, 4, 17,
19 — `0x00f58470`, `0x00f57c10`, `0x00f58610`, `0x00f58700`, `0x00f58200`, `0x00f583c0`) has
the **identical shape**, CONFIRMED for those 6 and HIGH (same compiled pattern, not
individually re-verified) for the remaining 9:

```
id  = item_header[+1]                       ; the HIRC item's own declared object id
slot = call 0x00f55a20(id)                   ; register/allocate a slot keyed by id
if (slot == 0) return (duplicate-id or allocation-failure path)
mgr = (the per-kind sub-manager: [0x2cc5310], [0x2cc5310]+fixed-offset, or a caller-supplied
       value already holding one of those — see the "extra arg" column in §A.2.1)
key = slot[+0]
obj = call 0x00f579f0(mgr, key)              ; find-or-create the actual HIRC object
if (obj == 0):                                ; first time this id has been seen
    lock a global critical section ([0x2cc537c], seen at every sampled site)
    obj = call <kind-specific "allocate new object" function>(...)   ; e.g. 0x00f76a40 (kind 1),
                                                                       ; 0x00f7c870 (kind 4),
                                                                       ; 0x00f7c1d0 (kind 19)
    unlock
else:                                          ; already registered — re-applying/updating
    call <kind-specific "apply fields" function>(obj, ...)           ; e.g. 0x00f768f0 (kind 1)
release the slot (0x00f55840)
return status
```

`0x00f579f0` is CONFIRMED (by call-site reading, not by reading its own body this pass) to be
the *second* layer of this architecture — a single shared "find or create the indexable object
for this manager+key" helper used identically by every kind — and it is **the same address
`review/scoping-audio.md` §8.4 U-W2 already lists as one of the shared helpers where the
public-API (`PostEvent`) route and the `AK::EventManager`-table route converge** (`0x00f52320`'s
cluster). That is an independent cross-check that `0x00f579f0` is a genuinely central,
kind-agnostic Wwise utility (most plausibly something like "get or construct the indexable
object for this id", reused for HIRC construction and for event-time object lookup alike) —
**HIGH**, not re-derived from any outside source, just the project's own two independent BFS
passes landing on the same address for related-but-different reasons.

`[0x2cc5310]` is a global pointer to what is, by the evidence here, one large aggregate: three
different kinds' construction helpers add three different large fixed offsets to it before
calling `0x00f579f0` (`+0xfa0` for kind 14, `+0x15e0` for kind 15, `+0x1900` for kind 18,
`+0x1c20` for kind 19 (used via a stack temporary, see §A.2.1's table) — i.e. **one global
object of at least ~0x1c24 bytes containing several embedded per-kind-category sub-managers at
fixed offsets**, rather than an array of separately-heap-allocated manager objects — CONFIRMED
shape (the offsets are added directly to the loaded pointer value, not used to index an array
of pointers); HIGH that it is "one Wwise engine-singleton blob with embedded sub-managers"
specifically, rather than some other aggregate. `[0x2cc5310]` also appears, independently, as
the object that `0x00f5acb0`'s *teardown* path locks and whose `+0x18` member is the 193-bucket
ID hash table a released object is removed from (§A.4) — consistent with it being (or being
very close to, structurally adjacent to) the same registry the project's already-confirmed
`g_pRegistryMgr` global (`0x02cc531c`, `review/scoping-audio.md` §8.2) belongs to: the two
addresses are only `0xc` bytes apart. **HIGH**, not proven identical — this pass did not open
either global's full layout far enough to settle whether `[0x2cc5310]` *is* `g_pRegistryMgr`'s
own object, a sibling singleton in the same static block, or something else again adjacent to
it.

### 8.3 Why the scoping note's BFS never found any of this — CONFIRMED (as a negative result)

Three independent checks, all against the complete statically-linked Wwise code range
(`0x00f22000`–`0x00fc0000`, 40,657 instructions, the full extent already established by this
project's own middleware census):

1. **No literal reference anywhere.** The raw bytes of the dword `0x00f59416` (the chunk-reader
   function's own clean entry point, by `ret`+prologue boundary) do not occur anywhere in the
   entire file — not in `.text`, not in `.rdata`, not in `.data`, not anywhere at all, aligned
   or not. The same is true of every other candidate "outer" address tried.
2. **No direct call or jump anywhere in the Wwise code range targets it.** A full
   capstone disassembly of all 40,657 instructions, filtered for `call`/`jmp` with an immediate
   operand equal to this address, found zero.
3. **The jump-table *mechanism itself* is not hidden from static analysis** — once pointed at
   the right address (via the `HIRC` FourCC literal), both this dispatch's own 19-entry table
   and the neighbouring `DATA`-case's 4-entry table were trivially readable as plain
   `jmp dword ptr [reg*4 + literal]` instructions. The earlier whole-block scans for this exact
   instruction shape (§ method notes above) found zero *other* occurrences anywhere else in the
   Wwise range, which is why they did not surface this one blind — they were looking in the
   right way, just without the FourCC-literal anchor to tell them where to start reading.

**Conclusion, stated plainly:** the entire bank-chunk-reading pipeline (the FourCC switch and
the HIRC per-item dispatch inside it) is reached *only* through a genuinely indirect,
runtime-computed call that leaves no static trace this pass's methods (or, by the completeness
of check 2, *any* purely static byte/instruction scan of this code range) can find. The most
plausible mechanism, consistent with `spec-audio-runtime.md` §1.4/§3.1's own description of
`LoadBank` as an asynchronous, callback-driven operation handled by Wwise's own Bank Manager
thread, is a polymorphic "bank processing job" object whose vtable is built at *runtime*
(e.g. placement-constructed into a per-request buffer) rather than installed as a static
vtable literal anywhere in `.rdata` — which would explain why no vtable slot anywhere in the
binary's static data was found holding this address either. This was **not confirmed** this
pass; resolving it would need either locating that job object's construction site (which the
original U-W1 BFS's "9-function/282-instruction wrapper closure, bottoming out at `0x00f57710`"
is the natural place to look next — not yet attempted at this depth) or dynamic
analysis (single-stepping a real `LoadBank` call), which was not attempted. Flagged OPEN, not
guessed at further.

### 8.4 What `0x00f5acb0` and its callees actually are, in full — CONFIRMED

For completeness, since the scoping note's language ("the signature of a per-HIRC-object-kind
dispatch") pointed straight at this function, here is its full resolved behaviour, read
directly from the listing (`ecx` on entry is the HIRC-ish object being released; its refcount
lives at object `+0x4`, an id/key field at `+0xc`, two sub-object pointers at `+0x14`/`+0x18`):

1. Lock `[0x2cc5310]` (indirect call through the global thunk at `[0x101c1c4]`, the same
   lock thunk used throughout this part of Wwise's code, e.g. also inside `0x00f57710`).
2. Decrement `this+0x4`. If still non-zero (another reference survives), skip straight to step
   8 (unlock and return) — this is an ordinary refcounted release, not a kind-specific step.
3. (Last reference reached.) Lock `[0x2cc5310]` again — the *same* critical section, entered
   recursively; this, together with step 1's separate lock, reads as two independently-written
   RAII lock scopes that got inlined together, not anything exotic.
4. Call `0x00f5ab30(&this_plus_0xc_copy, [0x2cc5310]+0x18)` — removes this object's id from a
   193-bucket singly-linked-list hash table living at `[0x2cc5310]+0x18` (bucket index via a
   Knuth multiplicative hash mod 193; live count stored immediately after the 193×4-byte bucket
   array, at `+0x304` from the table's own base — all CONFIRMED by listing). This is the global
   object/ID registry every indexable Wwise object is tracked in, independent of its kind.
5. Unlock `[0x2cc5310]` (matches step 3).
6. If `this+0x14` is non-null: call *its* vtable slot `+0x60`, passing `this+0xc`, as a normal
   virtual call (`mov edx,[eax]; call [edx+0x60]`) — likewise for `this+0x18` if non-null. These
   are two ordinary polymorphic calls on two sub-objects; which concrete function actually runs
   depends on whichever vtable those sub-objects already carry, fixed at their own construction,
   not decided here.
7. Call the released object's own vtable slot 0 with `this` in `ECX` and a stack argument of
   `0` (shape of a scalar-deleting-destructor call with a "do not free the block" flag) — then
   call `0x00f32e70(idx, this)` where `idx` is read fresh from the global `[0x1346ed8]`.
   `0x00f32e70` locks a *different* global table — a fixed-capacity array of `0x4c`-byte records
   at `[0x2cc5288]` (capacity `[0x2cc5280]`, live count `[0x2cc5284]`), validated by a small
   classifier `0x00f32ce0(idx)` that bounds-checks `idx` and tests that `table[idx]` is
   populated — locks that record's own embedded critical section (`+0x24`), subtracts an
   allocation size (`0x00f333e0(this)+8`) from a running total at `+0x44`, and records the
   release via `0x00f335d0`. **This is memory/allocation-category bookkeeping (the kind of
   per-pool byte accounting Wwise's own memory-manager profiler keeps), not a kind-construction
   table** — CONFIRMED by the shape of the arithmetic (subtracting `size+8` from a running
   total under a lock, guarded by a capacity/occupancy classifier identical in style to a
   profiler bucket table), HIGH that this specific interpretation ("memory category") is the
   right english name for it, since no string or RTTI name was found for `[0x2cc5288]`'s table
   either.
8. Unlock `[0x2cc5310]` (matches step 1); return.

So the "up to 7 indirect calls" the scoping note flagged are, exactly: 2 lock calls (steps 1,
3) + 1 unlock (step 5) + 2 ordinary virtual calls (step 6) + 1 virtual call (step 7's
destructor-shaped call) + 1 unlock (step 8) = 7. None of the seven encodes a kind switch.

### 8.6 Real population histogram — settles "kinds 10-13 unused" with data, not inference (2026-10-08)

Scanned every real plain `.bnk_pc` entry install-wide (all 39 top-level `.vpp_pc` archives plus one bounded level of `.str2_pc` recursion): **1,026 real entries found, 721 parsed fully cleanly, 41,037 real HIRC items tallied.** Each item's own kind byte and chunk were walked using the public, documented Wwise chunk/item framing (no cleanroom RE owed for the generic container shape — `spec-audio-format.md` §3's own existing public-format boundary).

- **Kinds 10-13: CONFIRMED zero** — 0 items, 0 banks, across the entire population. Matches §8.2's dispatch-table finding exactly (these 4 share the "unregistered" fallback slot with no construction code at all), now with a population-level negative to back it, not just a structural one.
- **CONFIRMED no kind value outside 1-19 anywhere** — no format-version mismatch in any real shipped file.
- **New finding, beyond the original question: kinds 1, 15, 16 and 17 are ALSO zero in every real shipped bank**, despite each having its own real, non-fallback construction-helper pair in the dispatch table (unlike 10-13, which have no construction code at all) — i.e. real but unused code, a different flavor of "zero" from 10-13's "no code path exists." **Only 11 of the 19 dispatch kinds have any real shipped content.**
- **Real distribution** (dominated by two kinds): kind 2 — 17,952 items across 717/721 banks (near-universal); kind 3 — 10,597 items across 668 banks; together ~70% of all items. Kind 14 next (1,100 items/515 banks). Kind 8 is a striking outlier: 770 items concentrated in just 2 banks (the "init" bank, shipped in two separate archives) — a shared global-bus definition, not per-content-bank data.

**Explicit synthesis, 2026-10-08, joining this section's own data-side negative with the export-census code-side negative that otherwise sit in two separate documents without a connecting sentence: Wwise's Interactive Music Engine is confirmed completely unused in this shipped game, on both sides of the boundary.** Kinds 10-13 above (the Music Segment/Track/Switch-Container/Playlist-Container kinds, per public Wwise HIRC taxonomy) are zero in every real shipped bank, with no construction code path at all (this section). On the code side, `review/scoping-audio.md` §1.2's own call-site census of the Wwise export table finds only `MusicEngine::Init`/`Term`/`GetDefaultInitSettings` ever called — bare lifecycle boilerplate — and explicitly names `GetPlayingSegmentInfo` as never called. Two independent methods (real bank content; real game-code call sites) agree: the Music Engine is initialized because the SDK requires it, but never fed content and never queried — the game's actual radio/music system is Volition's own code, not Wwise's.

**Two bounded gaps, honestly scoped, not blocking the above:** (1) 7 banks in `sounds.vpp_pc` hit a reproducible item-0 parse anomaly (kind byte decodes fine, the following size field doesn't under the standard framing or two alternates tried; byte-identical across files with different content, so a shared template artifact, not corruption) — 161 items excluded from the histogram as a result. (2) 275 real `.bnk_pc` entries in `sound_turbo.vpp_pc` (one per voice character) could not be extracted — blocked by the already-documented mode-(a) non-first-entry decoding limitation (`spec-vpp-container.md` §3.3-§3.8); pre-existing, not newly broken, not solved here.

**Two corrections to `spec-audio-format.md` surfaced by this population scan:** `dlc1.vpp_pc`/`dlc2.vpp_pc`/`dlc3.vpp_pc` also carry their own `.bnk_pc` entries (15+17+16 = 48 total), beyond that document's "six dedicated audio archives" framing — included in the histogram above. `sound_turbo.vpp_pc` DOES contain `.bnk_pc` entries (275 of them) — contradicting that document's "not `.bnk_pc` at all" claim, which was likely reached by sampling only the directory's first ~20 entries (all `.lm_pc`).

Full per-kind counts table, the reproducible scan script, and the raw JSON result are in `scratchpad/hirc_kind_histogram_2026-10-08.md` and `scratchpad/hirc_census/`.

### 8.7 What this section leaves OPEN

See the companion open-items file for the full list; headline items:
- The runtime-computed entry point into the chunk reader / HIRC dispatch (§A.3) — not located.
- The 15 non-fallback kinds' individual "allocate new object" / "apply fields" function pairs
  (e.g. `0x00f76a40`/`0x00f768f0` for kind 1) were not opened except for kind 1's pair, read
  only far enough to confirm the lock/construct-or-apply/unlock shape in §A.2.3 — the actual
  per-kind property parsing inside each of those ~15×2 functions is entirely unopened.
- Specific kind-number-to-Wwise-name mapping beyond the structural kinds-10–13 finding is
  HYPOTHESIS, sourced to public/community taxonomy only, not independently confirmed per-kind
  against this binary's own behaviour (e.g. by checking that kind 3's handler's deeper code
  shows an 11-way verb branch matching A7's "Action breaks into 11 verbs").
- Whether `[0x2cc5310]` is literally the same object as `g_pRegistryMgr` (`0x02cc531c`) or a
  structurally-adjacent sibling singleton.
- The purpose of the `[esp+0x24]` flag set only for kinds `0xa`-`0xd` in the fallback path
  (§A.2.2).

---

## 9. Event lookup and the action-list walk — PostEvent's internal path (unit U-W2a)

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process), unit U-W2a.
**Scope (per the dispatch brief and `review/scoping-audio.md` §8.4):** trace `AK::SoundEngine::PostEvent`'s
real internal path from the numeric event id, through Wwise's internal event-object lookup, to the
resolved Action object(s) it triggers, and the verb-level shape of how those Actions get applied. Rooted
at the `0x00f768c0` vtable cluster as named in the scoping note, reached from the game-boundary `PostEvent`
(`0x00f4eb90`). **Does not cover** container traversal or voice instantiation — that is U-W2b's separate
unit, rooted at `0x00f52320`/`0x00f51cc0` + the `CAkRegisteredObj` cluster; this file stops at the exact
hand-off point into that code (§5.2 below) and goes no further.
**Relationship to other specs:** `spec-audio-runtime.md` §1.2/§1.3 already specify the **game-side**
wrapper that calls into this code (`0x00470500`/`0x00470720` → the exported `PostEvent` overload at
`0x00f4eb90`) — that boundary is cited here, not re-derived. This file is the **Wwise-internal**
continuation past that boundary, newly authorized per `HANDOFF.md` §27.H-1.
**Method:** read-only `pefile` + `capstone` disassembly directly against the shipped
`SaintsRowTheThird.exe`, in the style of `tools/harnesses/exe_va.py`/`disasm_window.py`/`find_xrefs.py`,
extended with purpose-written scratch scripts (function-boundary sweep by furthest-forward-branch
heuristic, direct-call closure, and a padding-seeded whole-block caller finder — not committed to the
project). No Ghidra project was opened; no decompiler output appears anywhere below. Every address cited
was read directly from a listing produced this way, or (for vtable/jump-table contents) by a direct
4-byte-aligned read of `.rdata`.
**Confidence key (same as `spec-audio-runtime.md`):** **CONFIRMED** (read directly in a listing this
pass); **HIGH** (inferred from structure, not read to the same depth); **HYPOTHESIS** (plausible, not
checked).
**Naming convention used below:** `G1`, `G2`, `G3` are *this document's own convenience labels* for three
nearby global pointer slots (`0x02cc5310`, `0x02cc5314`, `0x02cc5334`) — not Audiokinetic names, not
carried over from any outside source. Every other identifier is either a plain hex address/offset or a
Wwise public API name already used by this project's other specs (`spec-audio-runtime.md`,
`review/scoping-audio.md`).

---

### 9.1 Overview

`PostEvent`'s real internal path, as confirmed this pass, is **two steps, not a single synchronous
walk**:

1. **`0x00f4eb90` → `0x00f4e120`**: validate, look up the Event object by numeric id (§2), and **enqueue a
   small deferred "post job" record** (§3) — `0x00f4e120` does **not** itself walk the Event's action
   list or touch any container/voice code.
2. **A separate, not-fully-located drain step** (one real lead found and reported honestly as unconfirmed,
   §6) is what actually walks the Event's list of Action references and applies them, ending — for at
   least one concrete Action subtype — in a direct hand-off into U-W2b's own named root (§5.2).

This matches Wwise's well-known public-facing contract that `PostEvent` is **non-blocking and deferred to
the next audio render pass** rather than executed inline on the calling (game) thread — a structural
property, not a guess: the enqueue step (§3) is read directly, and nothing in `0x00f4e120`'s own
CONFIRMED closure touches an Action object, a container, or a voice.

A second major finding, independent of the above: the scoping note's `0x00f768c0` cluster (`§8.3`,
previously HYPOTHESIS: "the trailing dwords… [may be] a true vtable… not resolved") is **CONFIRMED by
direct byte read to be a real, contiguous pair of MSVC vtables** for an Action-object class family (§4),
including a **CONFIRMED 5-way verb-dispatch jump table** (§4.3) and a **CONFIRMED direct hand-off into
U-W2b's own named root** (§4.2) — this is the exact "U-W2a/U-W2b seam" the scoping note flagged as open.

---

### 9.2 Event ID → Event-object lookup

#### 9.2.1 The real `PostEvent` core, `0x00f4e120` — CONFIRMED

`0x00f4eb90` (the exported overload the game calls, matching `review/scoping-audio.md` §8.1's table
exactly) is a thin wrapper: when `cExternals != 0` it builds an external-source array object via
`0x00f4d6e0(cExternals, pExternalSrcInfo)`; on failure it returns `0` immediately (no further call). It
then calls:

**`0x00f4e120(eventID, gameObjectID, uFlags, pfnCallback, pCookie, pExternalArrayOrNull)`** — 6 cdecl
stack parameters, CONFIRMED by reading the argument offsets directly:

1. Copies the external-source info (4 dwords from the array, or zero if none) into a local scratch
   struct — call its fields `S0..S3`.
2. **Event lookup:** `ecx = G1 (= *0x02cc5310) + 0x640`; `call 0x00f579f0(this=ecx, eventID)` (§2.2).
   On a miss (`eax==0`), returns `0` (`AK_INVALID_PLAYING_ID`) — no further Wwise work at all.
3. On a hit (`pEvt`): acquires a lock — `call dword ptr [0x00101c300]` with argument `0x02cc5378` (a
   lock/sync primitive distinct from the `0x00101c1c4`/`0x00101c1c8` pair used throughout §2.2/§3/§4;
   its exact identity is HYPOTHESIS, not pinned down this pass) — then reads `pEvt+0xc`.
   **Correction to an earlier working assumption in this same investigation:** `pEvt+0xc` is **not** an
   action-list pointer. `0x00f579f0`'s own hit test (§2.2) compares `node+0xc` against the search key, so
   `pEvt+0xc` is simply **the Event's own numeric id, re-read from the object** — CONFIRMED by that
   cross-check, not a separate field.
4. Calls **`0x00f52c80(this = G2 (= *0x02cc5334), &paramStruct{gameObjectID, scratch, S0, S1, S2, S3},
   pfnCallback, pCookie, uFlags, pEvt_id)`** — this **enqueues a deferred post job** (§3); it does not
   walk any action list.
5. On enqueue failure (`eax != 1`): releases `pEvt` — a virtual call through `pEvt`'s own vtable, slot
   index 2 (`*(*pEvt)+8`) — and returns `0`. (§4.4 cross-validates that vtable slot 2 = `Release` for
   Wwise's indexable-object convention generally, independent of this one call site.)
6. On success: assigns a fresh Playing ID — `call 0x00894810` (an address **outside** the Wwise
   `0x00f22000`–`0x00fbffff` block; a generic/shared runtime helper, HIGH: an interlocked 16-bit rolling
   counter, by the way its result is immediately truncated to 16 bits) combined via
   `0x00f6ab30(this = G3 (= *0x02cc5314), &outStruct, subCounter)`; the function returns `outStruct+0xc`,
   the real assigned Playing ID. `0x00f4eb90` returns this value unchanged (freeing the external-source
   array object via `0x00f4c2c0` first if step 6 was never reached and `cExternals != 0`).

**Game-object identity / argument correspondence to `spec-audio-runtime.md` §1.3:** the `gameObjectID`,
`pfnCallback`, `pCookie`, `uFlags`, and external-source array arguments passed into `0x00f4e120` are
exactly the ones `spec-audio-runtime.md` §1.2's rows for `0x00470500`/`0x00470720` already document the
game library as building before calling `0x00f4eb90` — no new game-side behavior is implied by anything
in this section.

#### 9.2.2 The lookup primitive, `0x00f579f0` — CONFIRMED, and project-wide generic

```
0x00f579f0: thiscall(this, AkUniqueID id) -> object* or 0   [ret 4]
  lock via call dword ptr [0x00101c1c4](this)
  hash = id * 0x15390949 (mulhi); hash >>= 4; hash = (hash * 0xc1) ; hash = id - hash   ; id mod 193
  node = this[hash*4 + 0x18]                                        ; 193-bucket array at this+0x18
  walk node -> node+8 (next) until node+0xc == id, or list end
  on hit:  inc dword ptr [node+4]  (AddRef)         ; refcount at +4
  unlock via call dword ptr [0x00101c1c8](this)
  return node (0 on miss)
```

All of the above is CONFIRMED by direct listing read (divisor 193 = `0xc1` is a literal immediate in an
`imul`, not inferred). **This is a generic, shared primitive, not Event-specific**: a function-start-seeded
direct-call census across the entire Wwise block (`0x00f22000`–`0x00fbffff`) found **213 distinct call
sites** to `0x00f579f0`, with `this` computed as `G1+0`, `G1+0x320`, and `G1+0x640` at different sites
(plus many sites in the `0x00f36000`–`0x00f3f300` range that are almost certainly bank-load-time reference
resolution — U-W1's territory, not expanded here). `0x00f4e120` (PostEvent's core, §2.1),
`0x00f4e320`/`0x00f4e3e0`/`0x00f4e490` (`ExecuteActionOnEvent`/`SeekOnEvent`'s two overloads, already
named in `spec-audio-runtime.md`/`review/scoping-audio.md` §8.1) all use `this=G1+0x640` — confirming they
all share this one Event registry.

**Structural finding (CONFIRMED by two independent constructions, §2.3 and §4.1): `G1` (`*0x02cc5310`) is
a flat array of fixed `0x320`-byte hash-table structures, one per registered HIRC-object kind.** Index 2
(`G1+0x640`) is the Event registry (established above by the literal `eventID` argument reaching it
directly from `PostEvent`'s own first parameter). Index 1 (`G1+0x320`) is the Action registry (§4.1).
Index 0 (`G1+0`, no offset) is used by at least one further Action-adjacent helper (`0x00f52bb0`, §4.5);
which HIRC kind it indexes was **not identified this pass (OPEN)**.

---

### 9.3 Enqueueing the deferred post job, `0x00f52c80` — CONFIRMED

```
0x00f52c80: thiscall(this, &paramStruct, pfnCallback, pCookie, uFlags, eventId) -> 1 (ok) | 2 (fail)  [ret 0x14]
  lock this+0x80 via call dword ptr [0x00101c1c4]
  node = alloc(pool = *0x01346ed8, size = 0x38) via 0x00f32da0
  if node == 0: unlock; return 2
  node+0x00 = 0           (next-pointer, singly-linked hash bucket — see below)
  node+0x04 = 1           (refcount)
  node+0x08 = eventId     (the Event's own numeric id, passed straight through)
  node+0x0c = gameObjectID            (paramStruct field 0)
  node+0x20 = scratch     (paramStruct field 1 — the 0x00101c300-returned value from §2.1 step 3)
  node+0x10 = S0, node+0x14 = S1, node+0x18 = S2     (paramStruct fields 2..4, external-source info)
  node+0x1c = S3, with an AddRef (inc dword ptr [S3]) on a non-null S3   (external-source object ref)
  node+0x28 = pfnCallback
  node+0x2c = pCookie
  node+0x30 = uFlags, masked to 0xffff0004 when pfnCallback == 0 (suppresses most notification bits
              when nobody can receive them — exact bit semantics not re-derived here, see
              spec-audio-runtime.md's own flag names)
  hash = scratch mod 31 (same multiply-shift idiom as §2.2, divisor 31 confirmed via the magic
         constant 0x8421085 and the immediately-following "quotient*32 - quotient" correction)
  node+0x34 = this[hash*4]            ; old bucket head (this is the hash-table's own bucket array,
  this[hash*4] = node                 ;   base "this" itself, not a sub-object)
  inc dword ptr [this+0x7c]            ; live job count
  unlock this+0x80
  return 1
```

**This is the complete, CONFIRMED job-enqueue operation.** It allocates a 0x38-byte record from the
shared Wwise memory pool (the same pool global, `*0x01346ed8`, and the same generic allocator,
`0x00f32da0`, that `review/scoping-audio.md` §8.4 already names as a shared helper reached by both the
public-API route and the `AK::EventManager`-table route), fills it from the caller's arguments and the
Event's own id, hash-inserts it into a 31-bucket table on `G2` keyed by the scratch/sequence value from
§2.1 step 3, and returns success/failure. **It does not read or touch the Event's action list, any
container, or any voice.** What (if anything) later walks this 31-bucket table to do the real per-frame
work is the subject of §6.

---

### 9.4 The Action-object family — `0x00f768c0` cluster, CONFIRMED

#### 9.4.1 The vtable pair — CONFIRMED by direct `.rdata` read

`review/scoping-audio.md` §8.3 found this cluster as the trailing dwords after the `AK::EventManager`
string and left its nature as HYPOTHESIS ("a true vtable… or something else, not resolved"). **Reading
the 12 dwords starting at VA `0x0127ca54` directly settles this: it is a real, contiguous pair of MSVC
vtables.**

| Slot (from `0x0127ca54`) | Address | Role |
|---|---|---|
| 0 | `0x00f768c0` | scalar-deleting destructor (sets vtable = `0x0127ca54`, calls base dtor `0x00f602e0`, conditionally frees via `0x005d7a10` on a passed flag bit) |
| 1 | `0x00f76930` | **AddRef** — `inc dword ptr [this+4]` |
| 2 | `0x00f76960` | **Release** — `dec [this+4]`; at 0, removes self from the hash table at `G1+0x320` (keyed by `this+0xc`, same 193-bucket scheme as §2.2) via `0x00f5ab30`, calls the dtor with "don't self-free" (`push 0`), then frees via the shared deallocator `0x00f32e70` |
| 3 | `0x006b89a0` | already-known folded no-op stub (`spec-audio-format.md`/`review/scoping-audio.md` §1.1/§8.6) |
| 4 | `0x00000000` | **NULL slot** — OPEN, not resolved this pass |
| 5 = vt_ca68 slot 0 | `0x00f76db0` | a **second** class's scalar-deleting dtor (vtable = `0x0127ca68`; additionally tears down a sub-object at `this+0x68` via `0x00f52880` before the base dtor `0x00f604b0`) |
| 6 = vt_ca68 slot 1 | `0x00f5ab00` | AddRef (shared slot value with the `AK::BankManager`-table candidates `review/scoping-audio.md` §8.3 already flagged) |
| 7 = vt_ca68 slot 2 | `0x00f5acb0` | Release |
| 8 = vt_ca68 slot 3 | `0x0066ac20` | folded stub |
| 9 = vt_ca68 slot 4 | `0x00f5afe0` | not examined this pass |
| 10 = vt_ca68 slot 5 | `0x006147f0` | not examined this pass |
| 11 = vt_ca68 slot 6 | `0x00f76d70` | **forwards directly to U-W2b** — §4.2 |

`0x0127ca68 = 0x0127ca54 + 0x14` exactly — i.e. **slots 5–10 above are literally `vt_ca68`'s own slots
0–5**, continuing with 6 further slots of its own (table below). Whether this reflects single
inheritance (a derived class's vtable placed immediately after its base's in `.rdata`) or two unrelated
classes placed contiguously was **not independently established**; CONFIRMED is only the raw byte
contiguity and the two constructor sites (`0x00f768c5`, `0x00f76a68` write `0x0127ca54`; `0x00f76d3b`,
`0x00f76db8` write `0x0127ca68`).

`vt_ca54`'s own real constructor, `0x00f76a40` (CONFIRMED): allocates `0x24` (36) bytes from the shared
pool (`0x00f32da0`), calls a base ctor `0x00f602c0(this, param)`, sets the vtable to `0x0127ca54`, zeroes
4 floats at `+0x10..+0x1c` and a dword at `+0x20`, then calls **`0x00f769f0`** to insert itself into
**`G1+0x320`** (the SAME 193-bucket hash scheme as §2.2, keyed by `this+0xc`) — on insert failure, calls
`Release` on itself (slot 2) and returns 0; on success returns the new object. **This is the second,
independent confirmation that `G1+0x320` is a distinct per-kind registry, parallel to the Event registry
at `G1+0x640`**, and that these 36-byte, 4-float objects are the registered content — consistent with
Wwise's "Action" HIRC kind (small, numeric-id-keyed, carrying a handful of float parameters such as a
value/fade-time/curve set) far more than with "Event" (which, per Wwise's public documentation, is simply
a list of Action references and carries no float parameters of its own).

#### 9.4.2 The U-W2a/U-W2b hand-off — CONFIRMED

```
0x00f76d70: thiscall(this, oneStackArg) -> ?   [ret 4]
  eax = [esp+4]
  push eax
  push &(this+0x68)
  push this
  call 0x00f52320          ; U-W2b's own named root, review/scoping-audio.md §8.4
  ret 4
```

This is the exact seam the scoping note left open ("resolve the three vtable offsets `+0x7c`/`+0x10`/
`+0x80`" against `0x00f52320`). **What is now CONFIRMED instead**: one of this Action-family class's own
virtual methods is nothing but a direct, unconditional forward into `0x00f52320`, passing `this` itself
and a pointer to its own embedded sub-object at `+0x68` (the same field `0x00f76db0`'s destructor tears
down via `0x00f52880`, and the same field `0x00f52bb0`, §4.5, filters candidate nodes by). **Resolving a
concrete Action object and invoking this method is, operationally, "applying" the action** — what
`0x00f52320` then does with the container reference at `+0x68` is explicitly U-W2b's own territory and is
not traced further here.

#### 9.4.3 Verb-level dispatch — CONFIRMED, two sibling 5-way jump tables

`0x00f77040` and `0x00f77140` (`vt_ca68` slots 7 and 8) are near-identical methods, each ending in a
classic compiler-generated bounded switch:

```
cmp eax, 4        ; eax = dword ptr [param_block + 0]   (param_block = this function's own stack arg)
ja <default>
jmp dword ptr [eax*4 + <table>]
```

Jump-table contents, read directly from `.rdata`:

| case | `0x00f77040`'s table (`0x00f7712c`) | `0x00f77140`'s table (`0x00f771fc`) |
|---|---|---|
| 0 | `0x00f7709c` → calls `0x00f52540(this, param_block+4, &(param_block+0xc), param_block+8)` | `0x00f77190` → calls `0x00f52540(this, 0, &(param_block+0xc), param_block+8)` |
| 1 | `0x00f770b9` → calls `0x00f52570` (same arg shape as case 0) | `0x00f771ab` → calls `0x00f52570` (literal `0` in place of `param_block+4`) |
| 2 | `0x00f770d6` → calls `0x00f525d0` (same, plus an extra byte arg `param_block+0x16`) | `0x00f771c6` → calls `0x00f525d0` (byte arg `param_block+0x15` instead) |
| 3 | `0x00f770f8` → **virtual call** through `(*this)+0x28`, args `param_block+4`, `param_block+0x18`, `param_block+8` | `0x00f771f2` → the function's own common epilogue (**no-op** for this dispatcher) |
| 4 / default | `0x00f77115` → calls `0x00f76ea0(this, param_block, param_block+4)` | `0x00f771e6` → calls `0x00f76f70(this, param_block+8, param_block)` |

**CONFIRMED as a bounded, verb-shaped dispatch** keyed on a small field of a caller-supplied parameter
block, exactly the level the task asked for ("which Action verb, with what parameters"). **Not resolved
this pass:** the exact meaning of the selector or a mapping onto A7's public 11-verb histogram
(`HANDOFF.md` §27.H-2: Stop/SetVolume/Play/…). The shape of cases 0–2 (three near-identical calls with an
increasing parameter count: 3 args, 3 args, 4 args) reads more like Wwise's public "set a named property"
family (`AkPropID`-style: Volume, Pitch, LPF, …, which share one small set of value/fade/curve parameters)
than like the Play/Stop/Pause action-type axis — **HYPOTHESIS, flagged rather than asserted**, per the
task's own instruction not to assume Wwise's full public action-type space without checking. This is the
single most promising concrete lead for whoever next maps these verbs precisely.

#### 9.4.4 Cross-validation: dtor/AddRef/Release is a project-wide convention, not special to this class

`0x00f59ec0` (found independently while searching for other `G1+0x640` consumers) walks the **same**
193-bucket Event array, and for each node whose `+0x14` field is set, calls the object's own **vtable
slot 1** (AddRef shape: `mov edx,[esi]; mov eax,[edx+4]; call eax`) then, after a second list pass, calls
**vtable slot 2** (Release shape: `mov edx,[esi]; mov eax,[edx+8]; call eax`) on every node — i.e. a bulk
unregister/teardown routine over genuine `+0x640` Event objects, using the identical slot-index convention
already used in §2.1 step 5 and §4.1. **This independently confirms that "slot 1 = AddRef, slot 2 =
Release" is a general Wwise-indexable-object convention** (shared by whatever class the real `+0x640`
Event objects use, which is a **different, not-yet-located vtable** from `vt_ca54`/`vt_ca68` — see §6's
framing: `vt_ca54`/`vt_ca68` objects live in the `+0x320` Action registry, not `+0x640`).

#### 9.4.5 Two further `vt_ca68` methods, partially resolved

- **`0x00f5b340`** (slot 9): walks a list at `this+0x14` (next-pointer at node+0x14), testing each node via
  `0x00f5ad60(this, node+0xc, param+4)`; on the first `true` result, returns `1` immediately. If the list
  is empty or exhausted without a match, falls through to a **different** virtual call (vtable slot
  `+0x20`) with the original parameter. HIGH: shape matches Wwise's public "Exceptions" list concept on
  container/randomizer-style actions (a short list of per-object overrides checked before falling back to
  default handling) — not independently confirmed against public documentation this pass.
- **`0x00f52bb0`** (slot 10, and independently the exact address `review/scoping-audio.md` §8.4 names as
  part of the U-W2a cluster): walks a list at `this+0x10`, filtering candidate nodes by two optional keys
  against `node+0x68` and `node+0x48`; only on the first successful match attempt (gated by a
  found-flag that starts at 0) does it fall back to a lookup against **`G1` itself, no offset added**
  (`0x00f579f0(this=G1, key)`) — the third, independent sighting of the generic lookup primitive against
  a *different* table index from Action (`+0x320`) or Event (`+0x640`). Full semantics OPEN (§7); its
  value here is the independent confirmation that `G1` holds further per-kind tables beyond the two this
  pass identified.

---

### 9.5 Summary call graph (CONFIRMED edges only)

```
0x00f4eb90  PostEvent (game-facing export, = spec-audio-runtime.md §1.2's target)
  -> 0x00f4d6e0   (build external-source array, when cExternals != 0)
  -> 0x00f4e120   (the real post-event core)
       -> 0x00f579f0   (Event-id lookup, G1+0x640)
       -> 0x00f52c80   (enqueue deferred job, G2 31-bucket table)
            -> 0x00f32da0  (shared pool allocator)
       -> [vtable slot 2 = Release]   (on enqueue failure only)
       -> 0x00894810, 0x00f6ab30   (Playing-ID assignment, G3)
  -> 0x00f4c2c0   (free external-source array, on failure)
```

```
(not directly reached from the above — see §6)
vt_ca54 / vt_ca68 Action-object family, G1+0x320 registry
  ctor 0x00f76a40 -> 0x00f602c0 (base ctor), 0x00f769f0 (hash-insert, G1+0x320)
  dtor 0x00f768c0 / 0x00f76db0 -> 0x00f52880 (teardown this+0x68), 0x005d7a10 (free)
  AddRef 0x00f76930 / 0x00f5ab00 ;  Release 0x00f76960 / 0x00f5acb0 -> 0x00f5ab30, 0x00f32e70
  0x00f76d70  -> 0x00f52320  *** hand-off into U-W2b ***
  0x00f77040 / 0x00f77140  -> 5-way jump table -> 0x00f52540 / 0x00f52570 / 0x00f525d0 /
                                                    [vtable+0x28] / 0x00f76ea0 / 0x00f76f70
  0x00f5b340  -> 0x00f5ad60, [vtable+0x20]
  0x00f52bb0  -> 0x00f579f0 (G1+0, third table index)
```

---

### 9.6 The missing link: how an Event's action references reach an Action object — OPEN, with one real lead

**What is CONFIRMED:** `PostEvent` enqueues a deferred job (§3) and does not walk any list. Separately,
a fully-formed Action-object class family exists, registered by numeric id in `G1+0x320`, with a verb
dispatch (§4.3) and a direct hand-off into U-W2b (§4.2). **What is NOT confirmed this pass** is the exact
function that (a) drains `G2`'s 31-bucket deferred-job table (or otherwise picks up a posted event on a
later frame), (b) reads the Event object's own list of Action references, and (c) for each one, resolves
it via the `G1+0x320` registry (presumably reusing `0x00f579f0` exactly as `0x00f52bb0` reuses it against
`G1+0`) and invokes one of the `vt_ca54`/`vt_ca68` methods from §4.

**The one concrete lead found and run down this pass**, reported honestly as unconfirmed rather than
asserted:

- **`0x00f5a3e0`**: takes either a single event id or a count + array of event ids (CONFIRMED by its own
  branch on an `== 1` stack argument), and for each one calls **`0x00f59f90`**; if any call in a multi-id
  batch fails, it **rolls back** the already-processed prefix by re-looking-up each one (`0x00f579f0`,
  `G1+0x640` — genuine Event objects) and releasing it (vtable slot 2) — a transactional, batch-capable
  wrapper.
- **`0x00f59f90`**: looks up an Event by id (`0x00f579f0`, `G1+0x640`); if the Event's own `+0x14` field is
  already non-zero, just increments it and returns (skip re-walking); otherwise walks a list at
  `Event+0x10` (next-pointer at node+0x10), and **for nodes whose packed 24-bit tag at `node+0x18` equals
  `0x4011`** (other tag values are silently skipped — no handling coded for them in this function),
  copies a 36-byte (9-dword) block and calls **`0x00f59de0`** with it plus `node+0x20` as a target id.
  (36 bytes exactly matches `vt_ca54`'s own allocation size, §4.1 — suggestive, not proven, that this
  block is one of that same Action-record shape.)
- **`0x00f59de0`**: resolves the target id against a table at `Manager+0x390` via `0x00f76c60`
  (structurally parallel to `CAkRegistryMgr::GetObjAndAddref`'s role for game objects, per
  `review/scoping-audio.md` §8.2 — HYPOTHESIS, not confirmed to be the same table); a `mode==2` argument
  (the literal value `0x00f59f90` passes) short-circuits to "just count/register interest, don't apply"
  (locks, increments a counter at the resolved target `+0x48`, unlocks, returns success) without calling
  further; otherwise it forwards a fresh 36-byte copy into **`0x00f59730`** (not examined this pass) for
  what is presumably the real per-action application step.

**Why this is flagged OPEN rather than folded into the CONFIRMED chain above:** an exhaustive
function-start-seeded direct-call search across the entire Wwise block (`0x00f22000`–`0x00fbffff`, 3,216
candidate function starts) found **no caller of `0x00f5a3e0`** within direct-call reach, and none of
`0x00f4e120`'s own CONFIRMED callees (§2.1/§3) reach `0x00f59f90`/`0x00f5a3e0` either. Three explanations
remain open and roughly equally plausible from what this pass could see: (a) it is the real per-frame
drain of `G2`'s deferred-job table, reached only via an indirect/virtual call this pass's method cannot
follow; (b) it is the internal implementation shared by one of the exported-but-game-unused families
`review/scoping-audio.md` §1.1 already lists (`PrepareEvent`/`PrepareGameSyncs`/`DynamicSequence::*`) —
its transactional, rollback-capable, 1-or-N-id shape is a good structural match for `PrepareEvent`'s
public contract; or (c) something this pass did not consider. **This is the single best next step for
whoever continues this unit or picks up U-W1's bank-load side.**

---

### 9.7 The real kind-to-registry mapping, corrected Action/Sound class identities (2026-10-09)

Closes `purrsian-04`'s wave 6 item 6 (event→bank→media lookup for one-shot SFX), the second half after
§11a.7's own positioning/classification answer. Reading the real per-HIRC-kind construction-dispatch
helpers directly settles `G1`'s array fully, closing §9.2.2's own explicit OPEN item for index 0:

| HIRC kind | Offset added to `[0x2cc5310]` | `G1` index | Allocate-new fn | Apply-fields fn | Real population (§8.6 census) |
|---|---|---|---|---|---|
| 2 | `+0x0` | 0 — **NEWLY IDENTIFIED** | `0x00f76d10` | `0x00f76de0` | 17,952 items/717 banks (dominant) |
| 1 | `+0x320` | 1 (already known, `vt_ca54`) | `0x00f76a40` | `0x00f768f0` | **0 — zero real content** |
| 4 | `+0x640` | 2 (already known, Event) | `0x00f7c870` | `0x00f7ca40` | present (Event) |
| 3 | `+0x960` | 3 — **NEWLY IDENTIFIED** | `0x00f7c2c0` | `0x00f7c7d0` | 10,597 items/668 banks (second-dominant) |

Kind 4's own construction helper is a byte-for-byte structural match to `PostEvent`'s own Event lookup
(§9.2.1 step 2) — CONFIRMED kind 4 = Event.

**Correction to §9.4: `vt_ca54` and `vt_ca68` are two unrelated classes, not base/derived (§9.4.1's own
flagged uncertainty) — and `vt_ca68` is the Sound object, not an Action.** Kind 2's real allocate-new
function (`0x00f76d10`) allocates 0x88 bytes and writes `dword[this] = 0x0127ca68` — the exact literal
§9.4.1 already read as `vt_ca68`'s base. Kind 1's allocate function (`0x00f76a40`, already read by
§9.4.1) allocates 0x24 bytes and writes `0x0127ca54`. Two separate classes, separate registries, and
(per the table above) wildly different real population — `vt_ca54` (kind 1) has **zero** real content
anywhere in the shipped corpus. **`vt_ca68` is the real, dominant Sound/SFX HIRC-object class** (kind 2);
§9.4.2's own "Action object forwards into container dispatch" finding is re-attributed: it is the SOUND
object forwarding itself into container/voice dispatch — Wwise internally treats a Sound object with
more than one candidate source exactly like a tiny one-slot container, which is why a leaf Sound node
reaches the same machinery §7 (unit U-W2b) documents for genuine Random/Sequence containers. The
genuinely populated Action-bearing kind is **kind 3** (10,597 items), a third, separate class.

**Kind 3's own construction helper (`0x00f58610`) tests a caller-supplied value against the literal
`0x4011`** — the same literal §9.6's own unconfirmed lead (`0x00f59f90`) tests against a list-node tag
at `node+0x18`. Not byte-identical in role (one compares a stack value, the other a field read from a
node) — reported as a **structural echo, HIGH not CONFIRMED** to be the exact same mechanism, but a
concrete, checkable next step for whoever closes §9.6's own remaining link (confirming kind-3 items are
exactly what an Event's own action-reference list contains).

### 9.8 Kind-2 Sound object: real on-disk source-descriptor layout, and the voice-line convergence
question (2026-10-09)

Kind 2's apply-fields function (`0x00f76de0`) reads the raw item bytes (after skipping 4 bytes past the
item's own id) and branches on whether it declares more than one candidate source: `count==0` (the
ordinary one-shot-SFX case) calls `0x00f76cd0`→`0x00f60570`, copying a 20-byte record into the Sound
object's own embedded sub-object at `+0x68`; `count!=0` calls `0x00f60600` instead (same sub-object, a
loop variant not opened further — OPEN).

**Both paths are fed by the same shared, kind-agnostic byte-stream parser, `0x00f56ba0`**, CONFIRMED by
direct listing read:

| Read order | Size | Output offset | CONFIRMED mechanics | HIGH-confidence label (not named by the binary) |
|---|---|---|---|---|
| R1 | dword | `+0x04` | unconditional | plugin/codec-type id (low nibble selects branch) |
| R2 | dword | register only | unconditional, drives every branch | stream-type selector — structurally exactly 3-valued |
| R3 | dword | `+0x08` | unconditional | **source/media id** |
| R4 | dword | `+0x0c` | unconditional | size-or-companion-id |
| R5/R6 | dword/dword | `+0x10`/`+0x14` | only if R2 != 1 | extra size/offset fields |
| B1 | byte | packed `+0x18` | bits 0/1 + `(R2==2)` | per-source flag byte |
| R8/R9 | dword/dword | `+0x20`/`+0x1c` | only if `(R1&0xF)` is 2 or 5 | embedded-parameter-blob size/companion (cursor advances past the blob) |

Branch structure (CONFIRMED, literal immediates): `(R1&0xF)==0` → no source plugin, empty slot;
`(R1&0xF)==1` → **the plain single-id codec/source case, the shape a one-shot SFX's embedded Vorbis
source takes** (R2 then checked to be one of `{0,1,2}`); `(R1&0xF)` is 2 or 5 → an embedded
parameter-blob generator source (`CAkFXSrcSine`/`CAkToneGen`/`CAkModalSynth`/`CAkFXSrcAudioInput`, §7.6's
own RTTI scan); anything else → rejected. **R2's own 3-valued branch is the real "bank-resident vs.
streamed" split**: `R2==1` skips the extra size pair (plain streaming, no prefetch); `R2==0`/`R2==2` both
read it (resident-needs-allocation-size / streamed-with-prefetch-buffer respectively) — read directly
from the branch structure, not asserted from any outside document.

**Past §7.3.2's own declared stopping point (vtable slot `+0x44`):** `vt_ca68`'s vtable slot 17
(byte offset `+0x44`) holds `0x00f529a0` — exact arithmetic match, CONFIRMED by direct dword read.
`0x00f529a0` walks a candidate list and calls `0x00f50d50`, which gathers into a 12-byte-stride weighted
array (`0x00f4fb50`) and recomputes running min/max weighted-probability sums (`0x00f501b0`) — a weighted
multi-candidate-source selection layer, a close cousin of the already-documented Random/Sequence
container weighting (§7.3.3's `0x00f76710`), sitting between "the Sound object was chosen" and "a
concrete `CAkSrcBankVorbis`/`CAkSrcFileVorbis` gets constructed." **This narrows, but does not close, the
literal call into `RegisterCodec`'s factory table** (`0x00fa44a0`/`0x00fa33e0`, §11.3.1) — OPEN.

**Worked example against real shipped data, closing `spec-audio-format.md` §4.2's own long-standing
`tag`-field OPEN item as a side effect.** Real bank pair `sh04.bnk_pc`/`sh04_media.bnk_pc` (cross-
reference id `0xd7fbe8e8`, exact match). 5 of 6 kind-2 Sound items' decoded `R3` field is an exact
32-bit match to a real `sh04_media.bnk_pc` record's own `tag` field (e.g. Sound item `0x297d8b3b` →
R3 `0x3920cac1` → media record idx 3, tag `0x3920cac1`) — far beyond chance for 32-bit values. **The
plain bank's kind-2 Sound-item source-id field (R3) and the companion `_media.bnk_pc`'s per-record `tag`
field are the same number** — resolving `spec-audio-format.md` §4.2's own flagged "role not determined"
`tag` field as exactly the identifier Wwise's own streaming-source lookup resolves against. The one
exception (`0x3a0aba68`, no matching record) is reported honestly, not forced — may reference a
different bank, or be a case this single-sample check doesn't cover.

**Convergence with the voice-line external-source mechanism (`0x00468ae0`, §1.2a/§11a.7): genuinely
separate at the id-sourcing front end, convergent only at the codec/decode back end.** Voice lines use
`PostEvent`'s own external-source override (a caller-supplied codec id plus a game-computed file id,
built BEFORE `PostEvent` is called) — they never touch a Sound HIRC item's own embedded descriptor.
Ordinary one-shot SFX carry no external source (`cExternals==0`) — their media id comes entirely from
the HIRC-embedded descriptor this section decodes, reached via Event→Action(kind 3)→Sound(kind 2,
`vt_ca68`). **Two genuinely separate id-sourcing mechanisms, not one shared resolver with a varying
parameter** — they converge only at the very back end already documented in §11 (both construct either
a `CAkSrcBankVorbis` or `CAkSrcFileVorbis` via the same two `RegisterCodec` factories and decode through
the same shared Vorbis machinery, §11.9's `0x00fa49e0`).

**Still open:** the literal `RegisterCodec` factory-dispatch call itself; the multi-source loop variant
`0x00f60600`; §9.6's own Event-action-list-walk lead remains formally unconfirmed (narrowed by the
`0x4011` echo above, not closed); the one real-world exception in the worked example; kind 1's real-world
identity (zero population, dead/legacy or simply unexercised, not determined). Full detail:
`scratchpad/wwise_sfx_event_media_lookup_2026-10-09.md`.

---

## 10. RTPC/Switch/State internal application (unit U-W3)

**Status:** scratchpad findings for the agent's own task (U-W3 of `review/scoping-audio.md` §8). Not yet
folded into any spec file by design (the dispatching session asked for scratch-only; `spec-audio-runtime.md`
and `review/scoping-audio.md` §8 content sections were left untouched except for a light-touch correction
box added to §8.4/§8.5, noted at the end of this file).
**Date:** 2026-10-07. **Method:** read-only PE export-table reads, raw `pefile`+`capstone` disassembly via a
purpose-written scratch module (`wre.py`, this session's scratchpad, not committed), direct-call-site
scanning by byte-pattern search for `E8 <rel32>` (robust against disassembly misalignment — ordinary linear
disassembly of `.text` loses sync in places and silently drops real call instructions; the byte-pattern scan
does not have this problem and is how every "who calls X" claim below was produced). **No Ghidra project
was opened. No decompiler output anywhere.** Every address below was read directly from a listing produced
this way; nothing is inferred from a tool's summary without a quoted instruction sequence backing it.
**Confidence key:** **CONFIRMED** (read directly in a listing this pass, mechanics and operand values both
verified); **HIGH** (the structural shape is directly read and consistent across multiple independent call
sites, but one detail — usually an exact field *name*, not its *behavior* — is not nailed down); **HYPOTHESIS**
(plausible, not independently checked, always flagged as such inline).
**Primary roots, per the dispatch brief:** `CAkRTPCMgr::SetRTPCInternal` (`0x00f70b10`, export-confirmed,
§8.2 of the scoping note) for the RTPC half; the game-boundary `SetSwitch`/`SetState` exports (`0x00f4b030`,
`0x00f4c640`) for the switch/state half, traced inward by call-graph search as the brief asked.

---

### 10.1 RTPC application — `CAkRTPCMgr::SetRTPCInternal` (`0x00f70b10`), fully traced

#### 10.1.1 Top-level structure — CONFIRMED by listing

`SetRTPCInternal(AkRtpcID in_id, AkRtpcValue in_value, CAkRegisteredObj* in_pGameObj)` (signature per the
export's own demangled name, §8.2 of the scoping note) does the following, read directly:

1. **Hashes `in_id` into a 193-bucket table at `CAkRTPCMgr`+0x00** (the very first member of the object —
   CONFIRMED: `mov eax,0x15390949; mul ecx; shr edx,4; imul edx,edx,0xc1; sub eax,edx` — a multiplicative
   hash, `edx*0xc1`-reduced, i.e. exactly 193 buckets). **This is the identical hash formula used by
   `CAkRegistryMgr::GetObjAndAddref`** (`0x00f6e550`, already read in the scoping pass) for its own
   193-bucket game-object table — i.e. CAkRTPCMgr and CAkRegistryMgr both instantiate the same generic
   193-bucket hash-table shape, independently confirmed twice now.
2. **Walks the bucket's chain comparing node+0x00 == `in_id`** to find (or fail to find) the **per-RTPC-ID
   record** for this RTPC id. CONFIRMED.
3. If no record exists yet, **allocates a new 36-byte (`0x24`) record** through the generic allocator
   reached via global `0x01346ed8` and helper `0x00f32da0`, zero-initialises it, and links it into the
   bucket. CONFIRMED (the exact zeroing sequence: six dwords from `+0xc` to `+0x20`, matching the layout
   below).
4. Looks up (or, if absent, inserts) **this specific game object's current value** in a **sorted array of
   `{GameObjPtr:4, value:4}` 8-byte pairs** that lives inside the per-RTPC-ID record (begin/end/reserved at
   record `+0xc`/`+0x10`/`+0x14`). The lookup is a genuine **binary search** (`0x00f6ee50` — classic
   lower-bound binary search over a sorted array, CONFIRMED by listing: midpoint computed as
   `edi + (esi-edi)/2`, loop while `edi<=esi`), not a hash — i.e. **per-RTPC-ID, the set of game objects
   with their own override value is a small sorted vector, binary-searched**, while the RTPC-ID space
   itself is a hash table. Two different container shapes for two different access patterns, both CONFIRMED.
5. **If the object's new value equals its stored value exactly** (`ucomiss`+`lahf`/`test ah,0x44`/`jnp` —
   the standard compiler idiom for float equality, same pattern used elsewhere in this binary), **the
   function returns success immediately without touching any subscriber** — a no-op-on-unchanged-value
   short-circuit *inside* the Wwise-internal manager itself. CONFIRMED. (This is a different, lower-level
   cache than the game adapter's own per-object/global value cache already documented in
   `spec-audio-runtime.md` §0 item 3 / §1.1 row `0x00470960` — that cache sits in the Volition wrapper code
   outside Wwise; this one is inside `CAkRTPCMgr` itself and applies regardless of who calls it.)
6. Otherwise calls the real "apply" routine, `0x00f6ff40` (below), then commits the new value into the
   per-object sorted array (inserting a new `{GameObjPtr, value}` pair via a generic "find-or-insert into a
   sorted dynamic array" helper, `0x00f6f880`, if this object had no prior entry).

**Per-RTPC-ID record layout (36 bytes), offsets CONFIRMED by the zeroing/allocation code and by every read
inside `0x00f6ff40`:**

| Offset | Size | Meaning |
|---|---|---|
| `+0x00` | dword | RTPC ID (hash-chain key) |
| `+0x04` | dword | hash-chain next pointer |
| `+0x08` | float | zeroed at creation; **not read anywhere in this function's own closure** — purpose not resolved by this pass, flagged open below |
| `+0x0c`/`+0x10`/`+0x14` | ptr/ptr/dword | sorted array #1: `{GameObjPtr, current value}` pairs, one per game object with its own override for this RTPC id (begin/end/reserved-count — the `AkArray`-style triple seen throughout this binary) |
| `+0x18`/`+0x1c`/`+0x20` | ptr/ptr/dword | sorted/dynamic array #2: **pointers to "subscriber" records** — the actual listening targets, see §1.2 |

#### 10.1.2 The apply routine, `0x00f6ff40` (300 instructions, CONFIRMED by full listing)

Called as `Apply(this=CAkRTPCMgr*, per_id_record, old_value_slot_or_null, new_value, in_pGameObj)`. Iterates
**array #2 of the per-ID record** (the subscriber-pointer array from §1.1), skipping adjacent-duplicate
pointers (a dedup guard against the same subscriber appearing in consecutive slots). For each subscriber
`S` (a pointer, dereferenced as a record with the fields below — **CONFIRMED by every read in this
function's listing**):

| Subscriber field | Meaning |
|---|---|
| `S+0x00` | pointer to the subscriber's "owner" object (dereferenced only in the polymorphic/default kind, below) |
| `S+0x04` | a secondary selector, read once as a dword and compared against `0x20` in the "default" kind branch (see below) |
| `S+0x0c` | **scope**: the specific game-object this subscriber is bound to, or `0` for "applies globally" |
| `S+0x10` | **kind** — a small integer selecting how this subscriber's value is produced/applied (0, 1, 2, 4, or "other") |
| `S+0x14` | a curve-descriptor pointer (kind 0) **or** the begin of a 0x14-byte-stride sub-list (kinds 4 and "other") |
| `S+0x18` | end of the same sub-list (kinds 4/"other" only) |

**Scope filter (CONFIRMED):** a subscriber is skipped unless `in_pGameObj == S+0x0c` (exact match), OR
`in_pGameObj == NULL` (this is a *global* set) **and `S+0x0c` has no per-object override recorded in array
#1** (precedence rule, CONFIRMED by listing: a global set explicitly looks up `S+0x0c`'s own game object in
array #1 via `0x00f6f3e0`→`0x00f6ee50` and skips the subscriber if that lookup succeeds — **i.e. an
object-specific RTPC value always wins over a global one, enforced right here in the propagation loop, not
merely by lookup order**).

**Per-kind handling, all CONFIRMED by listing:**

- **Kind 0** — the simple case: the subscriber has one curve (`S+0x14`), evaluated by the curve evaluator
  (§1.3) at the new RTPC value, and the result applied directly through a virtual call
  (`[[S+0x00]][+4]`, i.e. vtable slot 1 on the subscriber's owner), with the raw RTPC value and id also
  forwarded. Scope must match *exactly* here (no "subscriber scope == 0" fallback, unlike the generic filter
  above — read directly: `cmp ebx,ecx; je apply; test ebx,ebx; jne skip`).
- **Kind 1 / 2** — share one handler. If the owner's own secondary field (`[owner+0x10]` dereferenced one
  level, a 16-bit word) is non-zero, OR (kind==1 and a further small range check on `S+0x04∈{0,1,0x18..0x1c}`)
  passes, the code sums the curve evaluations of **every entry in a 0x14-byte-stride sub-list on the
  subscriber** whose key (`entry+4`) is unused as a filter here (summed unconditionally) — then, for a
  global set that hasn't yet happened this call, first snapshots (once, lazily, via `0x00f6fc20`) the list
  of game objects that already have their own per-object override for this RTPC id, to reuse as an
  exclusion list across every subscriber in the same call (an amortised version of the same precedence rule
  as the scope filter). Final apply is a call to `0x00f7e8f0`, which **re-enters the RTPC system** through
  `0x00f703d0` (read below, §1.4) to resolve a *second, related* RTPC id's current value before combining
  it with this one — i.e. genuinely **layered/combined RTPC support**, not just one curve per subscriber.
- **Kind 4** — same sub-list shape as kind 1/2, but this time the sub-list **is** filtered: only entries
  whose key (`entry+4`) equals the RTPC id currently being set are summed. Otherwise the same
  precedence/exclusion-list handling and the same `0x00f7e8f0` re-entrant apply.
- **Kind "other" (anything not 0/1/2/4)** — the generic/polymorphic path: two guard predicates are checked
  via virtual-ish calls (`0x00f39890`, `0x00f7e290`, the second itself walking a list on the owner and
  testing a chain of three more fields before calling), then **all** entries in the subscriber's own
  0x14-stride sub-list are summed (no id filter this time), and finally dispatched either to
  `0x00f7e380` (when the owner's secondary selector `S+0x04 == 0x20`: walks a *different* list on the owner
  and, for each entry passing the same guard-predicate chain, calls a virtual function at **vtable slot
  0x4c** with a literal callback pointer `0x00f7e330` — read as **propagation to a list of child/bound
  objects**, each of which re-queries its own current value through the supplied callback rather than
  being pushed a precomputed number) or to `0x00f7e8f0` (the same re-entrant combine-with-another-RTPC path
  as kinds 1/2/4, for every other `S+0x04` value).

**Reading, in plain terms:** "kind" distinguishes *how a target's value is produced* — a single curve
(0), a value with range-clamp/combine semantics (1/2), a value filtered to one specific RTPC id out of a
shared multi-RTPC binding (4), or a fully generic/virtual target that may itself be a node with children
(the propagation case) or may need another RTPC's value folded in. All four meet back at the *same*
curve-evaluation primitive (§1.3) and the *same* exclusion-list precedence rule; the "kind" only changes
*which* curve(s)/sub-list to read and *how many* to sum before applying.

**Notification tail (end of `0x00f6ff40`, object-specific sets only — `in_pGameObj != NULL`):**
reads a list head at `in_pGameObj+0x93c` and walks it; for each entry whose `entry+8` matches a snapshot of
`in_pGameObj`'s previous value (checked via the *integer* curve-evaluator overload, §1.3), walks a further
inner list at `entry+0x18` and fires a virtual call at vtable slot 1 with (the evaluated value, the RTPC id).
**Read as a "notify watchers that this specific object's resolved RTPC value changed" mechanism**, separate
from and in addition to the subscriber-apply loop above — CONFIRMED to exist and to be structurally
distinct (it is driven by a list on the *game object*, not by the per-RTPC-ID subscriber array), HIGH, not
CONFIRMED, for exactly what consumes it (the shape — object-scoped watcher list, vtable dispatch — matches
a user-visible "RTPC value changed" callback or a motion/feedback listener; no string or export ties a name
to it).

#### 10.1.3 Curve evaluation — CONFIRMED by listing, two overloads

A curve is a dynamic array of **12-byte points**: `{x: float, y: float, shape: dword}`, read via a function
taking (curve-array-view, input x) and returning the evaluated y. Two overloads exist, same point format,
same control flow, different output type:

- `0x00f6ef20` — returns a **float**.
- `0x00f6f0e0` — returns an **integer** (rounds via an FPU control-word round-to-nearest-then-`fistp`
  sequence) — used wherever the bound parameter is integer-typed (e.g. the notification-tail consumer in
  §1.2).

Both: 0 points → pass the input through unchanged (identity). 1 point → return that point's `y` regardless
of `x` (constant). ≥2 points → **linear scan** (not binary search, despite the array being sorted — CONFIRMED
by listing) for the bracketing pair; clamp to the first/last point's `y` outside the curve's domain.
**Segment shape, read directly from the point's own `shape` dword (3rd field of the 12-byte point, i.e. the
*left* point of each bracket carries the shape for that segment):**

- **shape `4` → linear interpolation**, formula read directly: `y0 + ((x-x0)/(x1-x0)) * (y1-y0)`. CONFIRMED.
- **shape `9` → constant/hold**: returns the left point's `y0` with no interpolation at all. CONFIRMED.
- **any other shape value** → dispatched to a shared helper, `0x00f4f130`, which is itself a **9-entry
  jump table** (VA `0x00f4f384`, indices 0–8) selecting a shape-specific blend-factor computation, then a
  shared `y0 + factor*(y1-y0)` blend (same final formula, CONFIRMED; the factor itself is shape-dependent
  and NOT individually decoded this pass — flagged open below). Public Wwise's `AkCurveInterpolation` enum
  for this SDK generation has one more named shape than fits this picture if 4=Linear/9=Constant are taken
  as given (there are ~10 public shapes total, 0–9, with 9 shapes going through the dispatcher's 9 entries
  0–8 and the 10th being Constant, handled outside the dispatcher) — **the exact id→name mapping beyond
  4=Linear/9=Constant is HYPOTHESIS, not re-derived from any string/table in this binary; those two are
  CONFIRMED by their literal formulas.**
- Out-of-range shape values (>8, reaching the dispatcher) fall back to a blend factor of 0.0 (i.e. clamp to
  `y0`) rather than crashing. CONFIRMED.

#### 10.1.4 The re-entrant query path, `0x00f703d0` — HIGH confidence read

Called from the kind-1/2/4/other "combine with another RTPC" sites (§1.2) via `0x00f7e8f0`. Internally it
re-runs the **same 193-bucket hash lookup and the same per-object sorted-array lookup** used by
`SetRTPCInternal` itself (literally the same multiplicative-hash instruction sequence, CONFIRMED), but this
time to **read** a resolved value rather than write one, honouring an **exclusion list** (checked via
`0x00f6fcc0`, a membership test) so that an object already counted by the caller isn't double-counted, and
also evaluates the target's own curve (`0x00f6ef20`) before returning. Read as **"get the fully-resolved
value of a (possibly different) RTPC id for a given object, as CAkRTPCMgr itself would compute it"** — i.e.
the read-side counterpart of `SetRTPCInternal`, used internally for layered-RTPC combination. HIGH, not
CONFIRMED, for this exact framing (the mechanics are fully read; the "why" — layered/combined RTPC support
— is the single most natural explanation for re-using the identical hash+array lookup machinery from inside
the apply path, but no comment/string confirms the design intent).

#### 10.1.5 Internal (non-game) callers of `SetRTPCInternal` — all three found, CONFIRMED

A byte-pattern scan of the whole `.text` section for `call 0x00f70b10` (robust against disassembly
misalignment, see header) finds **exactly three internal call sites**, none of them from the public
`SetRTPCValue` wrapper layer documented in `spec-audio-runtime.md`:

1. **`0x00f6a13e`** and **`0x00f6a170`** — two case-arms (global-object and specific-object variants) of a
   **generic internal command-queue consumer**, fully traced in §2 below — this is how the public
   `SetRTPCValue` API actually reaches `SetRTPCInternal` (through a queue, not a direct call).
2. **`0x00f86a89`** — a tiny, standalone 31-byte thunk (`0x00f86a70`–`0x00f86a8e`, isolated by `int3` padding
   on both sides, i.e. genuinely its own function, not inlined into anything) that reads `{RTPC id: dword
   @this+0x20, value: float @this+0x24}` from a small record and a game-object pointer from one level of
   indirection through its single stack argument, then calls `SetRTPCInternal(this+0x20, this+0x24,
   *(arg+0x34))` via `g_pRTPCMgr`. **No direct caller was found for this thunk** (a second byte-pattern scan
   for `call 0x00f86a70` returns zero hits) — it is reached only indirectly, almost certainly through a
   function-pointer slot on a per-HIRC-node "RTPC binding" record (consistent with the `{id, value}` pair
   being deserialised straight from bank data — see §2.4, where the structurally identical pattern for
   Switch/State *is* directly observed). **HIGH**, not CONFIRMED, that this is specifically a bank-load-time
   "push this node's initial RTPC binding" callback; the mechanism (what it does, and that it is reached
   only indirectly) is CONFIRMED, the "when/why" is not.

---

### 10.2 Switch and State — found, NOT folded into one answer: two genuinely different mechanisms

**Headline finding, CONFIRMED by call-graph tracing from the public `SetSwitch`/`SetState` exports all the
way to a real internal apply routine for each:** Switch and State do **not** share one mechanism, and
neither of them works anything like the RTPC curve machinery in §1. Tracing a *different* half of the
scoping note's own wording turned out to be exactly right on one side and exactly wrong on the other:

- **Switch genuinely *is* folded into `CAkRTPCMgr` itself**, under an **un-exported sibling method** at
  `0x00f6f810` — confirmed by direct trace, not guessed.
- **State is *not* in `CAkRTPCMgr`** — it lives in a **separate internal manager** reached through its own
  global singleton, with its own dedicated transition/ramp engine.

#### 10.2.1 How the trace was done — the generic internal command queue

The public wrapper for `SetSwitch` (`0x00f4b030`), `SetState` (`0x00f4c640`), and the id-overload of
`SetRTPCValue` (`0x00f4afe0`) are **structurally identical**, CONFIRMED by listing: each builds a small
stack-local "descriptor" with a **16-bit tag word at offset `+2`** (`2` = RTPC, `3` = State, `4` = Switch —
read directly from each wrapper's own `mov eax,N; mov word[esp+2],ax`), fills a handful of further fields
from its own stack arguments, calls `0x00f674f0` (computes something — not traced further, out of scope),
then pushes the result and calls `0x00f6ab30` through a global singleton **`0x02cc5314`**. A byte-pattern
scan finds **51 call sites** to `0x00f6ab30`, essentially every "fire into the audio thread" public API
function in the `0x00f4a000`–`0x00f4e000` wrapper range — i.e. **this is the shared generic
"post a command onto Wwise's internal queue" primitive**, used by RTPC/Switch/State and others alike;
`0x02cc5314` is a distinct singleton from both `g_pRegistryMgr` (`0x02cc531c`) and `g_pRTPCMgr`
(`0x02cc532c`), 8 and 0x18 bytes away from them respectively in what looks like a contiguous table of
Wwise-internal manager-singleton pointers (`0x02cc5314` through at least `0x02cc532c`, 7 consecutive dword
slots) — HIGH, not CONFIRMED, that it really is one contiguous singleton table (the slots are individually
read as independent globals throughout the code, which is all that's needed for every finding below; the
"it's one table" framing is a structural observation, not load-bearing).

**The consumer** was located by finding the jump table that dispatches on the queued record's own tag word:
a byte-pattern scan for an indirect `jmp dword ptr [eax*4+...]` within the same code region finds, at
`0x00f69c60`–`0x00f69c71`:

```
movzx eax, word ptr [esi+2]      ; esi = the queued command record; +2 = the SAME tag word the wrappers set
cmp eax, 0x26                     ; bounds check: valid tags are 0..0x26 (38)
ja  <skip>
jmp dword ptr [eax*4 + 0x00f6a8c4]  ; jump table, read directly, 39 entries
```

**CONFIRMED, read directly from the table at `0x00f6a8c4`:**

| Tag | Case address | Calls | Via singleton |
|---|---|---|---|
| 2 (RTPC) | `0x00f6a123` | `SetRTPCInternal` (`0x00f70b10`) | `g_pRTPCMgr` (`0x02cc532c`) |
| 3 (**State**) | `0x00f6a19e` | `0x00f547a0` | `0x02cc5318` |
| 4 (**Switch**) | `0x00f6a1c0` | `0x00f6f810` | `g_pRTPCMgr` (`0x02cc532c`) |

(Every other tag value present in the 0–38 range routes to other command kinds — bank/event/registration
work, not retraced here, out of scope for this unit.)

The **RTPC case** (`0x00f6a123`, with a second, object-specific variant at `0x00f6a148`/call-site
`0x00f6a170`) is a clean confirmation of the existing `spec-audio-runtime.md`-adjacent picture: reads
`{RTPC id @record+8, value @record+0xc, target game-object-id @record+0x10 (−1 = global)}`; when not
global, resolves the raw id to a `CAkRegisteredObj*` via `CAkRegistryMgr::GetObjAndAddref` (`0x00f6e550`,
already export-confirmed in §8.2 of the scoping note) before calling `SetRTPCInternal`. **This fully
confirms §1's read of `SetRTPCInternal`'s own closure is the real, live application path, not a dead/unused
export.**

#### 10.2.2 Switch — `CAkRTPCMgr::SetSwitchInternal` (un-exported, `0x00f6f810`) — CONFIRMED

Called as `(this=g_pRTPCMgr, SwitchGroupID, SwitchStateID, CAkRegisteredObj* resolved-object)`. Full listing
read (38 instructions):

1. **Looks up `(resolved-object, SwitchGroupID)` in a *second*, completely separate 193-bucket hash table
   living at `CAkRTPCMgr`+**`0x308`** (not the RTPC-ID table at +0x00) — same hash-table shape as §1.1,
   this time via an explicit `div` by `0xc1` (193) rather than the multiplicative trick, in a dedicated
   find-or-insert helper (`0x00f6f520`). CONFIRMED.
2. **Stores the new `SwitchStateID` into the found/created slot.** CONFIRMED. No curve, no interpolation,
   no property application of any kind happens here — this call is pure bookkeeping.
3. **Walks a notification list at `CAkRTPCMgr`+**`0x91c`**, filtering entries whose `entry+8` equals the
   `SwitchGroupID`, and fires a virtual call (vtable slot 1) with `(resolved-object, new SwitchStateID)` for
   each match.** CONFIRMED (read directly, `0x00f6f847`–`0x00f6f868`).
4. A sibling function on the same singleton, `0x00f542c0` (29 instructions, CONFIRMED by full listing), does
   a narrower version of step 3 alone — walks a list at `this+0x2c`, filtered by `(group, object)` matching
   two stack arguments, firing a virtual call at vtable slot 0 — used from two *other* dispatcher case arms
   (`0x00f6a1ef`/`0x00f6a20d`, tag values not re-derived this pass) as a standalone "just notify" primitive.

**Reading, in plain terms:** `CAkRTPCMgr`'s job for Switch is to remember *which state each (object, group)
pair is currently in* and tell a short list of watchers that it changed — **nothing here evaluates a
curve or writes a property.** The actual consequence of a switch change (which child a Switch Container
plays, any switch-bound property set) is not performed by the *set* path at all. **HIGH, not CONFIRMED**:
the natural reading is that switch-aware consumers (switch containers choosing a child, property sets bound
to a switch) *pull* the current value lazily, at decision/playback time, via a `Get`-style counterpart on
the same `+0x308` table, rather than having a value pushed to them — this pass did not locate and read that
`Get`-side consumer (flagged as the clear next step in the open-items file), so the "pull at decision time"
framing is inference from the shape of what *doesn't* happen in the set path, not a directly-read mechanism.

#### 10.2.3 State — a separate manager, `0x00f547a0` on singleton `0x02cc5318` — CONFIRMED structure

Called as `(this=0x02cc5318, StateGroupID, StateID, 0, 0)` from the runtime dispatcher (the trailing two
args are the SetState wrapper's reserved/always-zero fields — see §2.4). Full listing read (403
instructions, by a wide margin the largest function read this pass). Structure, CONFIRMED:

1. **Linear scan** (not hashed — a plain array, `this+0x00`/`this+0x04` begin/end, 8-byte elements) for the
   `StateGroupID` to find the **group record**.
2. **Two nested lookups** resolve the group's **previous** value-set and its **new** value-set: a
   stride-8-array scan (`0x00f5f560`) to find a per-value entry inside the group, whose result is itself
   searched again (`0x00f5f760`, stride-0xc) by a second key. The exact identity of each key (old-state-id
   vs new-state-id vs something else) was not pinned down to the bit this pass — **HIGH**, not CONFIRMED,
   that the two parallel lookup chains correspond to "the group's previously-active value" and "the newly
   selected value" respectively (the overall shape — two near-identical lookup chains each feeding one side
   of an old/new comparison — is CONFIRMED; which chain is "old" and which is "new" is inferred from
   control flow, not independently verified against a known state transition).
3. **Up to four numeric properties are read off each resolved value-set**, via four distinct one-line
   accessor functions, CONFIRMED trivial by listing: `fld [ecx+0x10]` (`0x00f76890`), `fld [ecx+0x14]`
   (`0x0067f6d0`), `fld [ecx+0x18]` (`0x00f768a0`), `fld [ecx+0x1c]` (`0x00f768b0`). For each of the four,
   old vs. new is compared for exact equality (same `ucomiss`/`lahf` idiom as §1.1); if different, either:
   - a **transition is active or required** for that property index (checked via `0x00f5ef40`, called with
     the property index 0–3) → a **ramping transition is registered** through **yet another singleton,
     `0x02cc5324`**, via `0x00f537f0` (start) / `0x00f53c40` (build a transition descriptor) /
     `0x00f5ef80` (register it) — bit flags `0x10000`/`0x20000`/`0x40000`/`0x80000` (one per property index)
     are passed through, read as "which property is ramping" flags; or
   - the **new value is written immediately** through one of four dedicated setters, one per property:
     `0x00f5efe0`, `0x00f5f040`, `0x00f5f010`, `0x00f5f070` (CONFIRMED distinct call targets, one per
     property index, by listing — their own bodies were not individually read this pass).
4. A user/profiler hook is consulted partway through (`mov eax,[0x02cc5368]; test eax,eax; ...; call eax`,
   guarded by one of the function's two boolean arguments) — not traced further, flagged in open items.

**The four numeric properties being ramped/set are HYPOTHESIS for their names** (public Wwise documents
State-bound properties as Volume/Pitch/Low-Pass-Filter/High-Pass-Filter for this SDK generation — the count
(four) matches exactly, which is why this is flagged as plausible, but nothing in this binary's own strings
or exports names these four fields; the structural finding — four float properties, old-vs-new compared,
ramped-or-set — is CONFIRMED independent of what they're called).

**`0x02cc5324` (the transition/ramp singleton) is CONFIRMED structurally distinct from `g_pRTPCMgr`**
(different absolute global slot, read independently throughout `0x00f547a0`) — this is the mechanism that
makes State transitions smooth over time; **RTPC has no equivalent of its own** (RTPC curves are evaluated
fresh on every `SetRTPCInternal` call from the *caller-supplied* value, with no time-based ramping inside
`CAkRTPCMgr` itself — any smoothing for RTPC is done by the *caller*, e.g. the game adapter's own
"approach the target" smoothing already documented for RTPC `0xdb2d51a3` in `spec-audio-runtime.md` §1.3).
This is a genuine, confirmed architectural asymmetry between the three mechanisms, not just a naming
difference: **RTPC = stateless curve evaluation on every call; State = stateful old→new transition with a
dedicated ramp manager; Switch = stateless bookkeeping + notify, with no value application in the set path
at all.**

#### 10.2.4 Independent corroboration: the identical split is reachable from HIRC bank-load code too

A byte-pattern scan for callers of `0x00f6f810` (Switch) and `0x00f547a0` (State) each turns up **one
further caller beyond the runtime dispatcher**, both in the `0x00f86xxx` neighbourhood (the same
neighbourhood as the unexplained RTPC thunk in §1.5) — and the notify function `0x00f542c0` has a *third*
caller there too. Reading this region directly (CONFIRMED by listing) finds **two small, structurally
parallel object kinds**, each with its own vtable, constructor, bank-data deserialiser, and "apply" method:

| Vtable VA | Deserialises (from bank bytes) | Apply calls | Via |
|---|---|---|---|
| `0x0127da58` | `{id: dword @+0x20, id: dword @+0x24}` | `0x00f547a0` (State engine) with `(rec+0x20, rec+0x24, 0, 0)` | `0x02cc5318` |
| `0x0127da80` | (ctor only read; same field shape assumed by its apply) | `0x00f6f810` (Switch) with `(rec+0x20, rec+0x24, [stream+0x34])` | `g_pRTPCMgr` |

**This independently corroborates §2.1–2.3's split from a completely different code path** (HIRC
record construction at bank load, rather than the runtime `SetSwitch`/`SetState` command queue): a
load-time "State binding" record and a load-time "Switch binding" record exist as genuinely separate C++
types, and each feeds its id/value pair into exactly the same internal manager the corresponding runtime
API call does. (The apparent `movss`/`mov` mismatch between the deserialiser, which moves the `+0x24` field
through an XMM register, and the apply code, which moves it through a GPR, is cosmetic — both are plain
4-byte copies of an opaque id, not floating-point arithmetic; checked directly, no numeric conversion
happens either place.)

**Minor independent cross-check, found along the way:** `SetState`'s own wrapper (`0x00f4c640`) folds its
second argument to 0 when it equals the literal constant `0x2ca33bdb` before building the queue record
(`cmp eax,0x2ca33bdb; cmove eax,ecx` with `ecx=0`) — consistent with this project's own already-established
FNV-1 hash of the string `"None"`/empty-string sentinel convention (cited in `HANDOFF.md` §27.H-2's A7 note
and `spec-audio-runtime.md` §1.3's `GetIDFromString` treatment) being folded to the id-0 "no state" sentinel
at the API boundary, one level before this pass's own tracing began. Not independently re-derived this pass
(no string/hash computation was redone) — noted only because it fell out of reading the wrapper and matches
an existing project fact exactly.

---

### 10.3 Summary table (plain hex, all addresses this pass read directly)

| Role | Address | Confidence |
|---|---|---|
| `CAkRTPCMgr::SetRTPCInternal` | `0x00f70b10` | CONFIRMED (export) |
| RTPC apply core | `0x00f6ff40` | CONFIRMED |
| RTPC curve evaluator (float / int) | `0x00f6ef20` / `0x00f6f0e0` | CONFIRMED |
| RTPC curve shared-shape dispatcher (9-entry jump table) | `0x00f4f130` @ table `0x00f4f384` | CONFIRMED (shape 4/9), HYPOTHESIS (others' names) |
| RTPC re-entrant query (combine-with-another-RTPC) | `0x00f703d0` | HIGH |
| Internal (non-game) callers of `SetRTPCInternal` | `0x00f6a13e`, `0x00f6a170`, `0x00f86a89` | CONFIRMED (all 3, exhaustive scan) |
| Generic internal command queue post | `0x00f6ab30` via `0x02cc5314` | CONFIRMED (51 call sites) |
| Command-queue consumer / jump table | dispatch at `0x00f69c60`, table `0x00f6a8c4` (39 entries) | CONFIRMED |
| **Switch internal apply** (`CAkRTPCMgr::SetSwitchInternal`, un-exported) | `0x00f6f810`, table at `CAkRTPCMgr+0x308`, notify list at `+0x91c` | CONFIRMED |
| Switch notify-only sibling | `0x00f542c0` | CONFIRMED |
| **State internal apply** (separate manager) | `0x00f547a0` via singleton `0x02cc5318` | CONFIRMED structure, HIGH on old/new identification |
| State transition/ramp manager | singleton `0x02cc5324`, via `0x00f537f0`/`0x00f53c40`/`0x00f5ef80` | CONFIRMED distinct from `g_pRTPCMgr` |
| HIRC load-time State binding record | vtable `0x0127da58` | CONFIRMED |
| HIRC load-time Switch binding record | vtable `0x0127da80` | CONFIRMED |

---

---

## 11. CAkSrcBankVorbis / CAkSrcFileVorbis per-class wrapper glue (unit U-W4a)

**Status:** complete scratch draft for the assigned unit **U-W4a** (`review/scoping-audio.md` §8.4/§8.5).
Not yet spliced into `spec-audio-runtime.md` — per dispatch instructions this file is scratch-only;
whoever integrates should pick the next real section number in `spec-audio-runtime.md` fresh.
**Unit boundary:** everything on `CAkSrcBankVorbis` (vtable `0x0127e954`) and `CAkSrcFileVorbis` (vtable
`0x0127e99c`) *except* whatever U-W4b's root turns out to be. The dispatch brief for this unit named
`0x00f8fa70` as that root ("the shared Vorbis bitstream/variant decode core"); **this has since turned
out to be wrong, corrected independently by this unit and by the parallel U-W4b agent (§0 item 3) —
`0x00f8fa70` is a small this-adjustor thunk into a generic array-teardown helper, not a decode function.**
This file still documents every glue-side dispatch into that address (it is real, exact, and was the
literal thing asked for), but does not claim it is "reaching the decode core," and does not attempt to
locate the real decode core itself (still open, see §10).
**Method:** read-only, Ghidra-free. Raw `pefile` section/RVA mapping + `capstone` listing-mode
disassembly against the shipped `SaintsRowTheThird.exe` (GOG, DX9), using the project's
`tools\harnesses\exe_va.py`/`disasm_window.py`/`find_xrefs.py` pattern, extended with purpose-written
scratch scripts (not committed). A direct MSVC-RTTI Complete-Object-Locator → TypeDescriptor →
ClassHierarchyDescriptor → BaseClassArray walk was used for the inheritance-chain finding in §1. An
exhaustive raw byte scan for `E8`/`E9` (`call`/`jmp` rel32) opcodes whose relative target resolves to
`0x00f8fa70` was run across the entire statically-linked Wwise block (`0x00f22000`–`0x00fbffff`, per
`review/scoping-audio.md` §1.1) to find every static dispatch site into that address — this is
exhaustive, not a heuristic sweep, for that one specific question (what `0x00f8fa70` itself turned out
to be is a separate finding, §0 item 3). No decompiler output was used anywhere. No Ghidra project was
opened. No leaked Audiokinetic/Volition/Havok/RAD material was read or used.
**Confidence key:** **CONFIRMED** (read directly from a listing or a raw byte match this pass); **HIGH**
(inferred from structure, not independently read to the same depth); **HYPOTHESIS** (plausible, not
checked).
**Naming:** `CAkSrcBankVorbis`, `CAkSrcFileVorbis`, `CAkSrcFileBase`, `CAkSrcBaseEx`, `CAkVPLSrcNode`,
`IAkSoftwareCodec`, `CAkVPLNode` are the binary's own RTTI mangled-name strings (`.?AV<Class>@@`),
CONFIRMED read directly from `.rdata` — not derived from any leaked source. Any public Wwise SDK method
name used below (`GetBuffer`, `StartStream`, `StopStream`, `SeekStream`, `ReleaseBuffer`, `GetDuration`,
etc.) is a HYPOTHESIS label for a CONFIRMED byte-level behaviour, offered only because the project's
own convention (`spec-audio-runtime.md` §"Method") treats public SDK names this way; every such label
is flagged, and the underlying field offsets/call targets are CONFIRMED regardless of whether the label
is right.

---

### 11.0 Headline corrections / additions to `review/scoping-audio.md` §8.4 (U-W4)

1. **The inheritance chain is now CONFIRMED, not HYPOTHESIS.** §8.4 said the two classes "plausibly"
   share a common base, "plausibly `IAkSoftwareCodec`". A direct RTTI ClassHierarchyDescriptor walk
   (not a guess) gives the exact chain for both (§1): `CAkSrcBankVorbis : CAkSrcBaseEx : CAkVPLSrcNode :
   IAkSoftwareCodec : CAkVPLNode` (single, non-virtual inheritance) and `CAkSrcFileVorbis : CAkSrcFileBase
   : CAkSrcBaseEx : CAkVPLSrcNode : IAkSoftwareCodec : CAkVPLNode`. `IAkSoftwareCodec` is confirmed a real
   ancestor, not a guess.
2. **Exact slot counts, not "≈".** `CAkSrcBankVorbis`'s vtable is exactly **17 slots** (indices 0–16);
   `CAkSrcFileVorbis`'s is exactly **20 slots** (indices 0–19). Both bounds are fixed by the next
   object's own Complete Object Locator pointer landing exactly at the end of the table (§2).
3. **`0x00f8fa70` is NOT the decode core — already corrected in `review/scoping-audio.md` §8.4 by the
   parallel U-W4b agent while this unit was in progress, and independently re-verified byte-for-byte by
   this unit (CONFIRMED by direct disassembly, not taken on trust).** `0x00f8fa70` disassembles to
   exactly 2 instructions, `add ecx,0x10` / `jmp 0x00f90c10`, followed immediately by `int3` padding — a
   this-adjustor thunk, not a 1,809-instruction decode function. `0x00f90c10` (37 insns) was read in full
   by this unit: it walks an array of `dword[this+0x10]` 12-byte records based at `dword[this+0x14]`,
   frees any non-null element via the shared Wwise pool-free primitive `0x00f32e70`, then frees the array
   itself the same way and zeroes the count — **a resource-array teardown helper, not decode logic**.
   This forces a correction to this unit's own original understanding of slot 9 (§2, §5): it is **not**
   `GetBuffer`, it is part of a **termination/resource-release cascade** (HIGH: Wwise's own `Term()`
   lifecycle method, called once before the C++ destructor, consistent with Wwise's documented two-phase
   object teardown — not CONFIRMED by name, only by shape). **Which vtable slot (if any) is the real
   `GetBuffer`/decode-trigger for either class was not identified by this unit** and is left open (§10;
   also still open per the U-W4b correction's own closing line, "finding it requires BFS-rooting the
   other vtable slots... individually, not yet attempted").
4. **The termination thunk is not Vorbis-exclusive at the call-site level.** Two of the 6 static dispatch
   sites into `0x00f8fa70` belong to functions that construct with **other, unidentified vtables**
   (`0x0127dd40` and `0x0127dde0`/`0x0127dd88` — no RTTI found for either; §6). So this same
   array-teardown thunk is reached by at least one or two sibling source-node classes beyond the two
   Vorbis ones — consistent with it being a generic `CAkVPLSrcNode`/`IAkSoftwareCodec`-level helper
   rather than anything Vorbis-specific, which fits its now-corrected role (generic buffer-array
   cleanup) far better than it fit "decode core."
5. **A new, CONFIRMED structural finding, independently corroborating the U-W4b correction's own
   `0x00f8fcb0` finding: a RIFF/WAVE(+Wwise-proprietary `"vorb"`) chunk walker shared by both classes'
   `StartStream`-shaped method.** §7 below. This unit found the `"RIFF"`/`"WAVE"`/`"fmt "`/`"cue "`/
   `"data"`/`"LIST"` literals independently, by its own direct byte read, before reading the U-W4b
   correction's note that the same function also recognises `"labl"`, `"smpl"` and a Wwise-proprietary
   `"vorb"` chunk — cited here as their finding, not re-derived, since the chunk's own contents are
   squarely the decode core's territory.

No further text edit to `review/scoping-audio.md` §8.4 is proposed by this unit — the U-W4b agent
already corrected the relevant passage directly (see the "⚠→✅ CORRECTED 2026-10-07" block under the
U-W4b bullet) while this unit was independently re-deriving the same fact from the glue side. The two
corrections agree on every checkable point (§0 item 3 above; §5/§6 below).

---

### 11.1 RTTI identity and inheritance (CONFIRMED)

Read directly via the standard MSVC Complete Object Locator (`vtable_va - 4`) → TypeDescriptor →
ClassHierarchyDescriptor → BaseClassArray walk — the same technique `review/scoping-audio.md` §1.1 and
§8.4 already used to *locate* the two vtables, extended here one step further to read the actual base
list instead of guessing it.

| Class | COL | TypeDescriptor name (RTTI string) | ClassHierarchyDescriptor | numBaseClasses |
|---|---|---|---|---|
| `CAkSrcBankVorbis` | `0x012c0fbc` | `.?AVCAkSrcBankVorbis@@` | `0x012c0fd0` | 5 |
| `CAkSrcFileVorbis` | `0x012c10fc` | `.?AVCAkSrcFileVorbis@@` | `0x012c1110` | 6 |

Base-class arrays (all entries: `mdisp=0, pdisp=-1, vdisp=0` — plain single, non-virtual inheritance,
no multiple/virtual inheritance anywhere in the chain):

- **`CAkSrcBankVorbis`**: `CAkSrcBankVorbis` → `CAkSrcBaseEx` → `CAkVPLSrcNode` → `IAkSoftwareCodec` →
  `CAkVPLNode`.
- **`CAkSrcFileVorbis`**: `CAkSrcFileVorbis` → `CAkSrcFileBase` → `CAkSrcBaseEx` → `CAkVPLSrcNode` →
  `IAkSoftwareCodec` → `CAkVPLNode`.

So the only structural difference between the two chains is the extra `CAkSrcFileBase` layer that
`CAkSrcFileVorbis` inserts between itself and the shared `CAkSrcBaseEx`/`CAkVPLSrcNode`/
`IAkSoftwareCodec`/`CAkVPLNode` tail — exactly the layer responsible for "this source reads from an
external stream instead of resident bank memory" (confirmed by what that layer's own code does, §4/§7).

---

### 11.2 Vtable layout (CONFIRMED addresses; slot-by-slot)

Both tables were dumped as consecutive dwords from the known vtable VA until the *next* class's COL
pointer was found sitting exactly where the table would otherwise continue — this is what fixes the
exact slot count (17 for Bank, 20 for File), not a guess or an "until it stops looking like code" cutoff.

**Key empirical fact used throughout this file:** indices 0–16 are laid out **identically** between the
two classes — i.e. **no slot reordering** happens when `CAkSrcFileBase` is inserted into the chain; its
new virtuals (and any new ones `CAkSrcFileVorbis` itself adds) are strictly appended as indices 17–19.
This is CONFIRMED empirically, not assumed: at indices 1, 6, 7, 13 and 14 — scattered through the whole
range — Bank and File hold the *exact same* code address (the two classes don't override those slots, so
whatever sits there is the shared base implementation; if the layouts didn't line up, these coincidences
would be essentially impossible).

| Idx | `CAkSrcBankVorbis` addr | `CAkSrcFileVorbis` addr | Same in both? | Role (confidence) |
|---|---|---|---|---|
| 0 | `0x00fa3410` | `0x00fa44d0` | override (own per-class) | **destructor** (scalar deleting) — CONFIRMED structure, §5 |
| 1 | `0x00c0dea0` | `0x00c0dea0` | **shared** | 1-insn `ret 4` stub: a no-op 1-argument setter, HYPOTHESIS name |
| 2 | `0x00fa2d20` | `0x00fa3840` | override | **ReleaseBuffer**-shaped: frees the held output-buffer block via `0x00f64be0`; also called directly from slot 0 (destructor) — HIGH |
| 3 | `0x00fa31f0` | `0x00f90130` | override | advances/consumes the decoded-output read cursor by a caller-supplied amount, calls `0x00f8fb00` — HIGH "consume decoded data" |
| 4 | `0x00c0dea0` | `0x00fa41f0` | **Bank: shared stub; File: real override** | Bank: no-op `ret 4`. File: releases its extra file-handle-shaped field `+0x10c` via `0x00f32e70` (the same Wwise-internal free helper already cited in `spec-audio-runtime.md` as a shared Wwise helper) — HIGH "release the open-file resource" |
| 5 | `0x00fa3600` | `0x00fa38b0` | override | HIGH **StartStream**-shaped: touches `+0xdc`/`+0x18`/sample-rate-ish fields `+0xaa`/`+0xb6` (Bank) or `+0xda`/`+0xe6` (File), calls `0x00fa2de0`/`0x00fa2e70`/`0x00f908f0` then the common tail `0x00fa45d0` |
| 6 | `0x00f80ff0` | `0x00f80ff0` | **shared** | tests flag bit `0x2` of `[this+8]+0xe5`; true → tail-jumps through *this object's own vtable slot* `+0x3c` (self-redispatch); false → returns `1`. HIGH "is-this-supported"-style trampoline |
| 7 | `0x00c0dea0` | `0x00c0dea0` | **shared** | same no-op stub as slot 1 |
| 8 | `0x00fa3450` (135 insns) | `0x00fa4510` (60 insns) | override | **StartStream/Init**-shaped — the "acquire and parse the Vorbis container header" entry point, §7 |
| 9 | `0x00fa3170` | `0x00fa4140` | override | **NOT `GetBuffer`** (corrected, §5) — a termination/resource-release cascade: releases the held output buffer, self-virtual-dispatches to slot 2, frees a per-instance resource, then tail-dispatches (via a this-adjustor thunk) into a shared array-teardown helper. HIGH "`Term()`"-shaped |
| 10 | `0x00fa3000` (95 insns) | `0x00fa3e90` (195 insns) | override | **the real decode-trigger (`GetBuffer`-equivalent), confirmed §11.9 (unit U-W4b)** — two-part body: grows the compressed-data lookahead region if needed (§7, still accurate as far as it goes), then calls the shared decode-loop worker `0x00fa49e0` and fills the caller's output struct with the decoded buffer pointer and sample count |
| 11 | `0x00fa2d70` | `0x00f90230` | override | pure-FPU, no calls: HIGH **GetDuration**-shaped (samples ÷ rate, reads `this+0xc8`/`+0xe0` Bank, `[this+8]+0x70`/`this+0x20`,`0x1c` File) |
| 12 | `0x00fa31c0` | `0x00fa3890` | override | Bank: 9-insn trivial field copy + return `1`. File: guarded, tail-jumps to the shared `0x00f90080` helper. HIGH a cheap **Reset**/state-transition |
| 13 | `0x006b89a0` | `0x006b89a0` | **shared** | the project's already-known folded 2-insn stub (`xor eax,eax; ret`) — a no-op boolean query defaulting to false |
| 14 | `0x00754410` | `0x00754410` | **shared** | the project's already-known folded bare-`ret` no-op (cited in `spec-physics-runtime.md` §1 as the same address, 27+ other vtables) |
| 15 | `0x00fa2ef0` | `0x00fa3d60` | override | HIGH **StopStream**-shaped: releases `+0xdc`/`+0x2c` via a virtual self-call to slot-like `0x00fa45d0`, sets a state byte (`+0x2c`=3 Bank, `+0x5c`=3 File) to a terminal value |
| 16 | `0x00fa2f60` | `0x00f902a0` | override | pure-FPU, no calls: HIGH a second **GetDuration**-style query (different fields than slot 11 — HYPOTHESIS "remaining" vs "total") |
| 17 | — (Bank has no slot 17) | `0x00fa3980` (146 insns) | File-only, new virtual | File's own **StartStream**-shaped header-parse, including real stream-object vtable reads (I/O), §7 |
| 18 | — | `0x00aab3b0` | File-only, new virtual — but itself a **shared folded stub** (`fld1; ret 4` — always returns the float constant `1.0`, ignoring its one argument) | HIGH default "speed/pitch multiplier = 1.0" |
| 19 | — | `0x00fa3b40` | File-only, new virtual | HIGH **SeekStream**-shaped: calls `0x00fa36b0` with the decode-state block (`this+0x54`) and two caller args, returns success/fail, §8 |

Slots 1, 7, 13, 14 are the four identical-across-both "shared, not overridden" entries beyond slot 6 —
consistent with §8.4's own earlier observation that the two tables "share several identical slot values
at the same slot index," now enumerated exactly instead of approximately, with slot 6 (a real, non-stub
3-instruction trampoline, not a folded stub) added to that list.

---

### 11.3 Construction (CONFIRMED)

#### 11.3.1 Factories (reached from `RegisterCodec`, `spec-audio-runtime.md` §3.2 row "Codec")

| Role | Address | Body |
|---|---|---|
| File-source factory (`CreateVorbisFilePlugin`, the export already named in `review/scoping-audio.md` §8.1) | `0x00fa44a0` | allocates **0x124** (292) bytes via the Wwise allocator `0x00f32da0` (global context `[0x01346f48]`), then calls the `CAkSrcFileVorbis` constructor `0x00fa3e10(this, param)` |
| Bank-source factory | `0x00fa33e0` | allocates **0xf4** (244) bytes via the same allocator, then calls the `CAkSrcBankVorbis` constructor `0x00fa2fa0(this, param)` |

Both factories are 14-instruction allocate-then-construct shims, identical in shape to each other.

#### 11.3.2 The two classes' own constructors

**`CAkSrcFileVorbis::CAkSrcFileVorbis`, `0x00fa3e10`** (28 insns):
1. calls the base-ctor chain `0x00f8ff40(this, param)` (§3.3);
2. writes own vtable pointer `dword[this] = 0x0127e99c`;
3. zeroes a cluster of fields unique to this class: `+0x10c` (dword), `+0x110`/`+0x112`/`+0x114`/`+0x116`
   (four words) — a small per-instance resource-handle/flags block, released by slot 4 and slot 9 (§2);
4. copies global `[0x01346f48]` into global `0x0134d2c0` unconditionally (role not pursued — flagged
   OPEN, §10);
5. **`memset(this+0x54, 0, 0xb8)`** via the shared helper `0x00ea23b0` — zeroes **184 bytes starting at
   offset `0x54`**.

**`CAkSrcBankVorbis::CAkSrcBankVorbis`, `0x00fa2fa0`** (26 insns):
1. calls the base-ctor chain `0x00f8fc40(this, param)` directly (no `CAkSrcFileBase` layer, §3.3);
2. writes own vtable pointer `dword[this] = 0x0127e954`;
3. zeroes `+0xdc`, `+0xe0`, `+0xe4`, `+0xe8`, `+0xec`, `+0xf0` (six dwords — a smaller, Bank-specific
   cluster: no separate file-handle resource to track);
4. same global copy as File;
5. **`memset(this+0x24, 0, 0xb8)`** via the same `0x00ea23b0` — the **same 184-byte size**, just at a
   different offset (`+0x24` instead of `+0x54` — the exact difference matches `CAkSrcFileBase`'s own
   footprint, since File's chain has one extra base layer in front of this block).

**This 0xb8-byte zeroed region is each class's own per-instance decode-state block** — the most likely
home for whatever running state the real (still-unidentified, §5/§10) decode/`GetBuffer` logic needs
across calls. It sits at a fixed, class-specific offset (`+0x24` Bank / `+0x54` File), is
zero-initialised at construction, and is what slot 19's seek call operates on (`this+0x54` for File,
§8). CONFIRMED: existence, offset, and size (184 bytes, both classes). HYPOTHESIS: that it is literally
a Vorbis-bitstream decode context (reasonable given its size and the seek slot's use of it, but not
independently verified against any decode-shaped code in this pass, since — per the correction in §0 —
`0x00f8fa70` turned out not to be that code, and the real decode entry point was not located here).

#### 11.3.3 The shared base-constructor chain (CONFIRMED, but genuinely shared — not Vorbis-specific)

- `0x00f8fc40` — **`CAkSrcBaseEx`'s own constructor.** Writes vtable `0x0127dfb0`; calls a deeper base
  ctor `0x00f81040` (which itself writes vtable `0x0127d640` — `CAkVPLSrcNode`'s own level, zeroes a
  flags byte at `+0xc`, zeroes `+4`, stores the constructor's `this`-chain argument at `+8`) and a small
  member ctor `0x00f90bb0` (zeroes an embedded 8-byte member); zeroes `+0x1c`/`+0x20`; copies a 16-bit
  field from `[[this+8]+0xe0]` into `this+0x18`. **Called directly by both classes' own constructors** —
  confirmed by reading both call sites — so this level is genuinely common infrastructure, not
  duplicated per class.
- `0x00f8ff40` — **`CAkSrcFileBase`'s own constructor**, File-only. Calls `0x00f8fc40` (above) as its own
  base, then writes vtable `0x0127dff8`, zeroes 11 dwords (`+0x24`…`+0x4c`) and clears 2 bits of byte
  `+0x52`, zeroes word `+0x50`. (File's *own* ctor then runs its further steps as in §3.2 on top of this.)

None of the three vtables written here (`0x0127dfb0`, `0x0127d640`, `0x0127dff8`) carry an RTTI COL —
consistent with `review/scoping-audio.md` §8.2's finding that Wwise's own intermediate/manager classes
often lack RTTI; here the pattern is simpler: **only the two concrete, `new`-able leaf classes
(`CAkSrcBankVorbis`, `CAkSrcFileVorbis`) carry RTTI at all** — the abstract/intermediate rungs of the
chain do not, because nothing ever constructs one as a complete object of exactly that type.

---

### 11.4 Destruction (CONFIRMED)

Both classes' slot 0 is a standard MSVC **scalar deleting destructor**, same shape in both:

1. reset `dword[this]` to the class's **own** vtable (defensive — guards any virtual call during
   unwind against seeing an already-more-derived vtable that no longer applies);
2. call the "release buffer" method directly (slot 2's own address — `0x00fa2d20` Bank / `0x00fa3840`
   File — a non-virtual direct call to the same code slot 2 points at, not a virtual dispatch);
3. **conditionally detach from a global list**: if a per-instance flag (`+0x44` Bank / `+0x74` File) is
   set, call `0x00fa4bb0(this+0x24 or this+0x54, &out)` with `ecx` = global `0x02cc5f40`. Read in full
   (§4.1) — this is a reference-counted-resource detach, not a plain list unlink.
4. call the base-destructor chain: `0x00f8fb30` (Bank) / `0x00f90330` (File) — see §4.2, this is where
   File's path into the array-teardown thunk `0x00f8fa70` lives (§0 item 3, §5);
5. test bit 0 of the one-byte "should-delete" parameter (standard scalar-deleting-destructor ABI); if
   set, call `operator delete` (`0x005d7a10(this)`);
6. return `this`.

#### 11.4.1 The global-list detach helper, `0x00fa4bb0` (CONFIRMED mechanics)

Looks up an entry keyed by a hash read from `[this+8]+0x9c` (via `0x00fa4b10`), and if found,
**decrements a reference count at the found entry's `+0x18`**; only when that count reaches zero or
below does it proceed to real cleanup (`0x00f56e30`, then the Wwise free helper `0x00f32e70` twice).
HIGH: this is detaching the source instance from a **reference-counted shared resource keyed by a name
hash** — plausibly the loaded media/cue entry this instance was playing from. The global `0x02cc5f40`
itself is not identified (OPEN, §10) — it sits 0x14 bytes past the already-known, exported
`IAkStreamMgr::m_pStreamMgr` global `0x02cc5f2c` (`spec-audio-runtime.md` §3.4 step 5), which may or may
not be meaningful; both read as `0` on disk (runtime-initialised, as expected for a `.data` global).

#### 11.4.2 The base-destructor chain — **this is where the asymmetry with Bank lives**

- **Bank's chain, `0x00f8fb30`** (8 insns, `CAkSrcBaseEx` level): writes vtable `0x0127dfb0`, calls the
  no-op stub `0x00754410` on an 8-byte-offset sub-object (a trivial/empty member destructor), writes a
  further vtable `0x0127c7f4` (the next level down — `CAkVPLSrcNode`'s own, inlined with no explicit call
  since there is nothing to clean up at that level), returns. **No call to `0x00f8fa70` anywhere in this
  chain** — CONFIRMED (it is one of the functions checked against the exhaustive scan, §5).
- **File's chain, `0x00f90330`** (`CAkSrcFileBase` level, 12 insns to its own tail-jump): writes vtable
  `0x0127dff8`; if `this+0x24` (the attached stream-descriptor object, §7) is non-null, virtual-calls its
  slot `+4` (a `Release`/`Close`-shaped call) and clears `this+0x24`; **then calls `0x00f8fa70` directly
  (non-tail, `call` at `0x00f90350`)**; then tail-jumps (`jmp`, not `call`) to `0x00f8fb30` — i.e. it
  continues down into the *same* `CAkSrcBaseEx`-level code Bank uses, after first doing its own
  File-specific cleanup **and the one extra call to the array-teardown thunk Bank never makes**.

So: **only `CAkSrcFileVorbis` reaches the array-teardown thunk during teardown; `CAkSrcBankVorbis` never
does** (via this path — Bank reaches the same thunk from its own slot 9 instead, §5). HIGH explanation,
now that `0x00f8fa70`'s real role is known (§0 item 3): `CAkSrcFileBase`'s own destructor is freeing a
12-byte-record array that only a *file-backed* instance owns (consistent with File's extra `+0x10c`
resource cluster, §3.2, and the fact that Bank's compressed data never requires a separate stream
handle, §7) — i.e. this is an ordinary "release my own array of records" call, not evidence of anything
decode-related. Not pursued further here (identifying exactly which array/what the 12-byte records are
is adjacent to, but not required by, this unit's own scope).

A second, structurally near-identical function **`0x00f90570`** also calls `0x00f8fa70` (`call` at
`0x00f90590`) and is **not** reached from `CAkSrcFileVorbis`'s own slot 0 — it is a separate compiler-
generated destructor variant (this one *does* include the scalar-deleting-destructor's own
operator-delete check inline, where `0x00f90330` does not), reached by at least one other, unidentified
caller. Mentioned for completeness; out of this unit's two-class scope to chase further.

---

### 11.5 The headline finding: exact dispatch into the shared array-teardown thunk (`0x00f8fa70`)

**Correction up front:** this unit originally framed `0x00f8fa70` as "the shared Vorbis decode core,"
following `review/scoping-audio.md` §8.4's own original characterization, and treated the question as
"where does each class dispatch into decode." While this unit was independently re-verifying that
characterization by disassembling `0x00f8fa70` itself (something the original scoping pass's heuristic
sizing method never actually did correctly, per its own §8.0 caveat), the parallel U-W4b agent landed a
direct correction to `review/scoping-audio.md` §8.4 finding the same thing: `0x00f8fa70` is a 2-instruction
this-adjustor thunk into a small array-teardown helper, not a 1,809-instruction decode function. **Both
findings agree exactly** (§0 item 3). What follows is the exact dispatch-site evidence — still fully
valid and useful as "where do these two classes' own methods reach this address," just correctly
labelled now as a termination-path call, not a decode call.

**Method, stated precisely:** every byte in the statically-linked Wwise block (`0x00f22000`–`0x00fbffff`,
the exact range `review/scoping-audio.md` §1.1 already establishes) was scanned for the opcodes `0xE8`
(`call rel32`) and `0xE9` (`jmp rel32`), and for every match the relative displacement was decoded and
added to compute the real target address; only matches whose target is **exactly** `0x00f8fa70` were
kept. This is exhaustive over static, immediate-operand call/jmp instructions in that whole range — not
a sample, not a function-local sweep — so "this is the complete list of static dispatch sites" is
CONFIRMED (indirect/register-operand calls that might reach the same address at runtime are a separate,
acknowledged blind spot, called out explicitly below).

**Result: exactly 6 static sites.**

| Address | Kind | Containing function | Belongs to |
|---|---|---|---|
| `0x00fa31af` | `jmp` | `0x00fa3170` (`CAkSrcBankVorbis` **slot 9**, now understood as a `Term()`-shaped method, §0/§2) | **Bank — direct, single-hop tail dispatch** |
| `0x00fa41e1` → `0x00f90060` → `0x00f9007b` | `jmp`, then `jmp` | `0x00fa4140` (`CAkSrcFileVorbis` **slot 9**, same corrected role) tail-jumps to the shared helper `0x00f90060`, which itself tail-jumps to the thunk | **File — two-hop tail dispatch, same slot-9 role as Bank** |
| `0x00f90350` | `call` | `0x00f90330` (`CAkSrcFileBase`-level destructor, reached from File's slot 0 via `0x00fa44f4`) | **File — non-tail call, during destruction (a second, independent path, §4.2)** |
| `0x00f90590` | `call` | `0x00f90570` (a second, structurally similar `CAkSrcFileBase`-level destructor variant) | File-adjacent, not reached from `CAkSrcFileVorbis`'s own slot 0 (§4.2) |
| `0x00f88b27` | `jmp` | a 2-instruction function at `0x00f88b20`, constructed with vtable `0x0127dd40` | **unrelated sibling class** (no RTTI found for that vtable) — out of scope |
| `0x00f89544` | `jmp` | a function at `0x00f89530`, constructed with vtable `0x0127dde0`/`0x0127dd88` | **unrelated sibling class** (no RTTI found) — out of scope |

#### 11.5.1 Bank's exact dispatch, in full (CONFIRMED, `0x00fa3170`, 43 instructions)

```
0x00fa3170: push esi
0x00fa3171: mov  esi, ecx                      ; this
0x00fa3173: lea  eax, [esi+0x34]
0x00fa3176: push eax
0x00fa3177: call 0x00fa46c0                    ; release any previously-held output buffer (§5.3)
0x00fa317c: mov  edx, [esi]
0x00fa317e: mov  eax, [edx+8]                  ; own vtable slot 2 (the "release/flush" slot)
0x00fa3181: add  esp, 4
0x00fa3184: mov  ecx, esi
0x00fa3186: call eax                           ; virtual self-call to slot 2
0x00fa3188: mov  eax, [esi+0xcc]
0x00fa318e: test eax, eax
0x00fa3190: je   0x00fa31ac
0x00fa3192: mov  ecx, [0x0134d2c0]
0x00fa3198: push eax
0x00fa3199: push ecx
0x00fa319a: call 0x00f32e70                    ; free a held +0xcc resource
0x00fa319f: add  esp, 8
0x00fa31a2: mov  dword ptr [esi+0xcc], 0
0x00fa31ac: mov  ecx, esi
0x00fa31ae: pop  esi
0x00fa31af: jmp  0x00f8fa70                    ; <-- the dispatch, into the array-teardown thunk (§0)
```
(Padding `int3`×12 immediately follows, confirming this is the true end of the function.)

#### 11.5.2 File's exact dispatch, in full (CONFIRMED, two functions)

`0x00fa4140` (`CAkSrcFileVorbis` slot 9, 84 insns) — same opening shape as Bank's (release prior buffer
via `0x00fa46c0` on `this+0x64`; virtual self-call to slot 2), then releases **two extra File-only
resources** Bank doesn't have (`this+0xfc` and `this+0x10c`, each via `0x00f32e70`), zeroing their
fields, before ending:
```
0x00fa41d8: mov  ecx, esi
0x00fa41de: pop  esi
0x00fa41df: ...
0x00fa41e1: jmp  0x00f90060                    ; <-- first hop (shared CAkSrcFileBase-level helper)
```
`0x00f90060` (shared helper, 28 insns to its own tail) — releases the stream-descriptor object at
`this+0x24` through its own vtable slot `+4` if present, then:
```
0x00f90060: push esi
0x00f90061: mov  esi, ecx
0x00f90063: mov  ecx, [esi+0x24]
0x00f90066: test ecx, ecx
0x00f90068: je   0x00f90078
0x00f9006a: mov  eax, [ecx]
0x00f9006c: mov  edx, [eax+4]
0x00f9006f: call edx                           ; virtual Release/Close on the stream object
0x00f90071: mov  dword ptr [esi+0x24], 0
0x00f90078: mov  ecx, esi
0x00f9007a: pop  esi
0x00f9007b: jmp  0x00f8fa70                    ; <-- second hop, reaches the same array-teardown thunk
```

Both slot-9 bodies, then, are a clean, self-contained "release everything this instance is holding"
method — buffer, a virtual flush, one or two private resources, then (via the thunk) a 12-byte-record
array — not a per-tick decode trigger. Nothing about either body reads a destination sample buffer,
touches a sample-rate/position field in a way that looks like producing output, or returns a
decoded-byte count; the shape is uniformly release-and-forward, reinforcing the §0 correction.

#### 11.5.3 The shared "release prior buffer" helper both slot-9s call first, `0x00fa46c0` (13 insns)

```
0x00fa46c0: push esi
0x00fa46c1: mov  esi, [esp+8]                  ; the field address passed in (this+0x34 Bank, this+0x64 File)
0x00fa46c5: mov  eax, [esi+0x14]
0x00fa46c8: test eax, eax
0x00fa46ca: je   0x00fa46e2
0x00fa46cc: push eax
0x00fa46cd: mov  eax, [0x0134d2c0]
0x00fa46d2: push eax
0x00fa46d3: call 0x00f32e70                    ; free the outstanding buffer object
0x00fa46d8: add  esp, 8
...
```
HIGH: "free whatever buffer-shaped object this instance was still holding onto, as part of releasing
everything before the array-teardown thunk runs" — called by both slot 9's (§5.1/§5.2), which this unit
originally took for `GetBuffer`'s own double-buffering housekeeping; now understood as part of the
termination cascade instead (§0 item 3), the same "free a held pointer-if-non-null" shape still applies,
just in a teardown context rather than a steady-state one.

---

### 11.6 What `0x00fa3e90` and `0x00fa3980` (File slots 10 and 17) actually call — negative result confirmed

Both are large, real per-class functions (195 and 146 instructions) and both were fully disassembled;
neither contains a direct call/jmp to `0x00f8fa70` (confirmed both by direct listing and by the
exhaustive scan in §5 finding no hit inside either address range) — this negative result holds regardless
of the §0 correction, and independently matches the U-W4b correction's own statement that "the other
citation (`0x00fa3e90`/`0x00fa3980` calling this directly) does not hold up." Each of the two functions
has a small number of *indirect* calls (through a register, not an immediate operand), and every one of
them was traced by hand: 

- `0x00fa3e90`'s one indirect call (at `0x00fa3f6e`): `mov ecx,[esi+0x24]; mov eax,[ecx]; mov
  edx,[eax+0x3c]; call edx` — a virtual call through **slot `0x3c`** of the stream-descriptor object at
  `this+0x24`.
- `0x00fa3980`'s three indirect calls (`0x00fa3acd`, `0x00fa3b14`, `0x00fa3b34`): each is `mov
  ecx,[this+0x24]; mov eax,[ecx]; mov eax,[eax+N]; call eax` for `N` = `0xc`, `0x10`, `0x14` in turn —
  three more virtual calls through the **same** stream-descriptor object, at three different slots.

These are reads through the per-instance **stream-descriptor object** (almost certainly the public
`AK::IAkAutoStream`-shaped interface already touched at the boundary level in `spec-audio-runtime.md`
§3.6's I/O-hook material, though this is the *per-source-instance* handle, not that global singleton) —
i.e. real streaming-I/O calls (read more compressed bytes, query status/position), nothing to do with
`0x00f8fa70` either way. So `0x00fa3e90` and `0x00fa3980` are squarely **"how File gets its compressed
data"** functions (§7 below) — real, substantial, decode-*adjacent* per-class logic, just not callers of
the (now-corrected) array-teardown thunk.

---

### 11.7 Bank-vs-File: how each gets its compressed data

#### 11.7.1 A shared, CONFIRMED finding: a RIFF/WAVE chunk walker in front of both classes' header step

Slot 8 (Bank, `0x00fa3450`) and slot 17 (File, `0x00fa3980`) — each class's own `StartStream`-shaped
"acquire and parse the header" method — both call the **same shared helper, `0x00f8fcb0`** (208
instructions), passing it pointers into their own per-instance state. Reading `0x00f8fcb0` directly
finds literal 32-bit immediate compares against the following little-endian values:

| Immediate (hex) | ASCII | Where |
|---|---|---|
| `0x46464952` | `"RIFF"` | `0x00f8fcfb` |
| `0x45564157` | `"WAVE"` | `0x00f8fd03` |
| `0x61746164` | `"data"` | `0x00f8fd4b`, `0x00f8fd57` |
| `0x20657563` | `"cue "` | `0x00f8fd69` |
| `0x20746d66` | `"fmt "` | `0x00f8fd71` |
| `0x5453494c` | `"LIST"` | `0x00f8fd79` |

**CONFIRMED by direct byte read of the immediate operands** — this is not a guess or a name pulled from
any outside tool. `0x00f8fcb0` walks a `"RIFF"`/`"WAVE"` container and recognises the `"fmt "`, `"cue "`,
`"data"` and `"LIST"` sub-chunks, rejecting anything that doesn't start with a valid `"RIFF"`/`"WAVE"`
header (early-exit at `0x00f8fd25` returning result `7`). This independently corroborates (with a direct
read, not a citation of outside material) the shape `review/scoping-audio.md` §8.4's own "divergence
from stock Vorbis" paragraph gestured at via public `ww2ogg`/`revorb` documentation: Wwise's Vorbis
payload here is **wrapped in a RIFF/WAVE container with `fmt `/`cue `/`data`/`LIST` chunks**, parsed by
this shared, per-class-agnostic helper before any Vorbis-specific bitstream work begins. Nothing about
the chunk *contents* (the real `fmt ` body, or what later decodes it) is opened here — that is squarely
U-W4b's/the real decode entry point's own territory, wherever it turns out to live (still open, §0/§10),
and is not re-derived in this unit.

The two call sites pass different pointers (Bank: `lea ecx,[ebx+0xe8]` / `lea edx,[ebx+0x20]` /
`lea ebp,[ebx+0x1c]` / `lea ecx,[ebx+0x10]`; File: `lea eax,[ebx+0x34]` / `lea ecx,[ebx+0x30]` /
`lea edx,[ebx+0x20]` / `lea ebp,[ebx+0x1c]` / `lea eax,[ebx+0x10]`) — consistent with each class landing
the parsed chunk offsets into its own, differently-laid-out decode-state block (§3.2), not with the
helper itself differing per class.

#### 11.7.2 Where the raw bytes come from: resident memory (Bank) vs. a stream object (File)

- **`CAkSrcBankVorbis`** reads its compressed Vorbis data straight out of the loaded bank's own resident
  memory block: slot 8's setup reads `[[this+8]+0xfc]`/`[[this+8]+0x100]` — a pointer/length pair off a
  parent record (the bank's media entry, `spec-audio-format.md`/`spec-audio-runtime.md` §1.4 territory) —
  and passes that pointer and size straight into the RIFF/WAVE walker above. **No stream object, no I/O
  calls anywhere in Bank's own vtable closure.**
- **`CAkSrcFileVorbis`** has no such resident block; instead it holds a **stream-descriptor object
  pointer at `this+0x24`**, through whose vtable it makes real virtual I/O-shaped calls: slot 17's own
  setup (3 indirect calls, slots `0xc`/`0x10`/`0x14`, §6) and slot 10's lookahead-fill logic (1 indirect
  call, slot `0x3c`, §6) both read through this object rather than touching resident memory directly.
  Slot 10 (`0x00fa3e90`, 195 insns) also **grows its lookahead buffer on demand** by calling the same
  Wwise-internal allocator (`0x00f32da0`) construction uses, and copies data with the CRT helper
  `0x00ea27b0` (called twice, consistent with a wrap-around ring-buffer copy) — i.e. it is the "read more
  compressed bytes from the stream, reallocating the lookahead region if it's currently too small" step.
  Bank's analogous slot (10, `0x00fa3000`, 95 insns) instead reallocates a fixed-shape buffer via
  `0x00f64b90` with no stream object involved at all, consistent with Bank never needing to "wait for
  more bytes to arrive" the way a streamed source does.

This is the concrete answer to "how each one gets its compressed data before handing off to whatever
decodes it": **Bank works from a pointer directly into the resident bank blob; File pulls data on demand,
in growable chunks, through a per-instance stream-descriptor object's own small vtable** — the same
bank-vs-stream split `spec-audio-format.md`/`spec-audio-runtime.md` already document at the
*file-format*/*adapter* level, now confirmed to exist again, independently, at this per-source-instance
level inside Wwise's own statically-linked code. (Which function actually consumes this data to produce
decoded samples was not identified in this pass, §0/§10 — this section answers "how the bytes get
there," not "what happens to them next.")

---

### 11.8 Seek handling (File-only; CONFIRMED structure)

**`CAkSrcFileVorbis` slot 19, `0x00fa3b40`** (24 insns) — this slot does not exist on
`CAkSrcBankVorbis` at all (Bank's vtable stops at index 16; this is one of File's three new, appended
virtuals, §2). Full body:
```
0x00fa3b40: mov  eax, [esp+8]          ; 3rd stack arg
0x00fa3b44: push esi
0x00fa3b45: push edi
0x00fa3b46: mov  edi, [esp+0x14]       ; 2nd stack arg (an output pointer)
0x00fa3b4a: push edi
0x00fa3b4b: mov  esi, ecx             ; this
0x00fa3b4d: mov  ecx, [esp+0x10]      ; 1st stack arg
0x00fa3b51: push eax
0x00fa3b52: push ecx
0x00fa3b53: lea  edx, [esi+0x54]      ; the decode-state block (§3.2)
0x00fa3b56: push edx
0x00fa3b57: call 0x00fa36b0           ; the real seek work
0x00fa3b5c: add  esp, 0x10
0x00fa3b5f: cmp  eax, 2
0x00fa3b62: jne  0x00fa3b69
0x00fa3b64: pop  edi
0x00fa3b65: pop  esi
0x00fa3b66: ret  0xc                  ; early return, result == 2 (HYPOTHESIS AK_Fail) passed straight through
0x00fa3b69: mov  eax, [esi+0x34]
0x00fa3b6c: add  dword ptr [edi], eax ; add a correction term into the caller's output parameter
0x00fa3b6e: pop  edi
0x00fa3b6f: mov  eax, 1
0x00fa3b74: pop  esi
0x00fa3b75: ret  0xc                  ; success
```
Three stack arguments (`ret 0xc` = 12 bytes), one of which (`edi`, the 2nd) is an **output** pointer the
function adds a correction term into on success — HIGH shape of the public `SeekStream(desired position,
&actual/skipped position)`-style signature (exact argument split into the three dwords — e.g. a 64-bit
position split across two dwords plus the output pointer, vs. three independent 32-bit values — not
determined here). The real positioning work is delegated to `0x00fa36b0` (not traced further — operates
on the same `this+0x54` decode-state block the constructor zeroes, consistent with seek needing to
reset/re-synchronise that same state; nothing about `0x00f8fa70`/slot 9 is involved in this path).

**Why File needs its own seek slot and Bank doesn't (HIGH):** Bank's compressed data is already fully
resident (§7.2), so "seeking" for Bank is plausibly just a cheap pointer/offset recompute absorbed into
the shared base-chain slots (e.g. slot 3's cursor-advance logic) rather than a dedicated virtual; File's
compressed data arrives through an asynchronous stream, so repositioning genuinely needs its own
stream-aware step (flush lookahead, reissue a read at the new offset) — which is exactly the kind of new
virtual `CAkSrcFileBase` would need to add beyond what `CAkSrcBaseEx`/`CAkVPLSrcNode` already declare.
Not settled by tracing `0x00fa36b0` itself in this pass — flagged, not re-derived, since that function is
arguably decode-state-adjacent (U-W4b-ish) rather than per-class glue.

---

### 11.9 The real `GetBuffer`/decode-trigger slot — found: vtable slot 10 (unit U-W4b)

**Status:** complete, read-only disassembly finding, answering the single open question left by
`spec-audio-runtime.md` §11 (unit U-W4a) and its companion `scratchpad/uw4a_open_items.md` item 0:
*which vtable slot, if any, is the real `GetBuffer`-equivalent (decode-trigger) for `CAkSrcBankVorbis`
(vtable `0x0127e954`, 17 slots) and/or `CAkSrcFileVorbis` (vtable `0x0127e99c`, 20 slots)?*

**Answer, stated up front: vtable slot 10 on both classes** (`0x00fa3000` for `CAkSrcBankVorbis`,
`0x00fa3e90` for `CAkSrcFileVorbis`). §11.2's own table had already catalogued slot 10 under
"buffer-capacity/lookahead management — fills/grows the compressed-data lookahead region," and that
description is not wrong, just incomplete: lookahead-buffer growth is the *first half* of the slot's own
body. The second half — not previously read — calls a shared decode worker, fills a caller-supplied
output-state structure with a real buffer pointer and a decoded-sample count, and advances the
per-instance sample-accurate playback position. This is CONFIRMED by direct disassembly (not inferred),
and CONFIRMED exhaustive for "is there any other vtable-reachable path to the same decode worker"
(one static-call-site scan, §11.9.3, finds exactly 3 hits, all three inside this one slot's two per-class
bodies).

**Method, same discipline as U-W4a and per `WALLS.md`'s own standing warning about this exact binary
region:** every function below was disassembled with a **strict control-flow-aware walker**, not a
byte-range/heuristic sizer — the tool follows only confirmed edges (both arms of a conditional branch,
straight-line fallthrough) as *part of* the current function, and treats every unconditional `jmp` or
`call` to an immediate target as a **boundary**, recording the target as a separate edge rather than
inlining whatever sits at or after it. This is the fix `WALLS.md` calls for after the `0x00f8fa70`
mis-sizing incident, implemented fresh for this pass (`tools`-style `pefile`+`capstone`, read-only, no
Ghidra). **Self-check:** run against `0x00f8fa70` first, it reproduces the already-corrected 2-instruction
result exactly (`add ecx,0x10` / `jmp 0x00f90c10`) before being trusted on anything new. The exhaustive
`E8`/`E9`-target byte scan used in §11.9.3 is the same method (and, run against `0x00f8fa70` as a second
self-check, reproduces the prior unit's already-published 6-site result exactly).
**Confidence key:** **CONFIRMED** (read directly from a listing or an exhaustive scan this pass); **HIGH**
(structural inference, not independently chased to the same depth); **HYPOTHESIS** (plausible, not
checked). Any public Wwise SDK name (`GetBuffer`, `ReleaseBuffer`, `AkVPLState`, etc.) is a HYPOTHESIS
label offered only for a CONFIRMED byte-level behaviour, per this project's standing convention — no
leaked Audiokinetic/Volition/Havok/RAD material was read or used anywhere in this pass.

---

#### 11.9.1 Slot 10, read in full (Bank: `0x00fa3000`, 95 instructions; File: `0x00fa3e90`, 195 instructions)

Both bodies have the same two-part shape; sizes match the prior unit's own count exactly (reassuring —
same tool, same input, same result), confirming the earlier "95 insns" / "195 insns" figures were already
real sizes, just not read past their first half.

##### 11.9.1.1 Part one (already known): grow/allocate the lookahead buffer if needed

Bank, `0x00fa3000`–`0x00fa303e`: if `[this+0x8c]` (the lookahead/output buffer pointer) is already
non-null, skip straight to part two. Otherwise allocate `[this+0x40] << 0xb` bytes (count × 2048) via the
Wwise-internal allocator `0x00f64b90`, store the result at `[this+0x8c]`, zero `[this+0x24]` (the first
field of the per-instance decode-state block, §11.3.2's own 184-byte zeroed region), and on allocation
failure write status `2` into the caller's output struct and return early.

File, `0x00fa3e90`–`0x00fa4024`: equivalent shape, but File also keeps its lookahead buffer topped up from
its stream object first (calls `0x00f88f70`/`0x00f90670`, copies bytes via the CRT helper `0x00ea27b0` —
this is the already-documented, per-instance stream-I/O side, §11's own §7.2) before reaching the same
`0x00f64b90` allocation call at `0x00fa3ffd` and the same `[this+0x54] = 0` decode-state-zero.

##### 11.9.1.2 Part two (NEW this pass): call the shared decode worker and fill the caller's output struct

**Bank, `0x00fa303e`–`0x00fa316d` (the function's real second half):**
```
lea   ebp, [esi+0x24]                  ; &decode-state block (the 184-byte region, §11.3.2)
push  ebp
push  <word [esi+0xb4]>
push  <[esi+0xe4]>
push  <[esi+0x8c]>                     ; the lookahead/output buffer pointer from part 1
call  0x00fa49e0                       ; the shared decode worker (§11.9.2)
mov   edx, [esi+0x8c]
mov   eax, [esi+0x90]
movzx ecx, word ptr [ebp]              ; decode-state+0x0 -- the sample count 0xfa49e0 just produced
mov   [edi], edx                       ; caller's output struct +0x0  = buffer pointer
mov   word [edi+0xe], cx               ; caller's output struct +0xe = decoded sample count
mov   word [edi+0xc], 0x400            ; caller's output struct +0xc = a fixed format/status word
mov   [edi+4], eax                     ; caller's output struct +0x4 = buffer capacity
...
mov   eax, [ebp]
add   [esi+0xdc], eax                  ; advance the sample-accurate playback position by the decoded count
...
mov   ecx, [esi+0x28]
mov   [edi+0x340], ecx                 ; final status into the caller's output struct
ret   4
```
(`edi` here is the function's own single stack argument, i.e. the one caller-supplied pointer this
one-argument `thiscall` method takes — both classes' slot 10 end every exit path with `ret 4`, a true
one-explicit-argument signature, exactly matching the shape of the public Wwise SDK's single-`AkVPLState&`
`GetBuffer`/`ReleaseBuffer` pair.)

**File, `0x00fa4025`–`0x00fa4133`:** same shape, parameterised for File's own field offsets and its two
exit paths (one when the lookahead buffer was already fully primed, one when it needed draining first —
both converge on the same decode call): both call `0x00fa49e0` (at `0x00fa40a3` and `0x00fa4118`), both
`lea ecx, [esi+0x54]` immediately before the call (File's own decode-state-block offset, §11.3.2), and
both finish by calling a small shared helper `0x00fa37b0` that writes `[caller+0x340] = <status>` — the
exact same output field Bank writes inline. `0x00fa37b0` (44 insns, read in full) also handles end-of-file
status codes (`0x11`, `0x2e`) and calls `0x00f904e0`/`0x00fa3770` for loop/EOF bookkeeping — the File-side
analogue of Bank's own inline `0x00fa2e30`/`0x00fa2ce0` looping calls at the tail of its own slot 10.

**This resolves §11's own open question about the caller-supplied output struct's field layout** (it was
never previously read at all): offset `0x0` = decoded-buffer pointer, `0x4` = buffer capacity, `0xc`/`0xe`
= two word-sized format/count fields, and (on both classes, well past what looks like the small part of
the struct) `0x340` = a result/status code. The large gap between `0xe` and `0x340` is not explained by
this pass — either the real struct is simply this large (plausible if it's a bigger pipeline/voice-level
object, not a small per-call `AkVPLState`), or `0x340` belongs to a different, adjacent object reachable
through the same pointer. Flagged as an open item, not resolved here (§ open-items file, item 1).

---

#### 11.9.2 The shared decode worker, `0x00fa49e0` (88 instructions, read in full)

Takes 4 explicit arguments (standard `cdecl`-shaped: 4 pushes, no `ecx` setup before the `call`):
the decode-state-block pointer, a 16-bit format/channel word, a remaining/target sample count, and the
output buffer pointer — in that order, matching the 4 values both classes' slot 10 push immediately
before this call (§11.9.1.2).

**Body is a genuine decode loop**, not a one-shot call:
```
loop:
  push edi ; push ebx ; push ebp        ; (remaining-count, output-ptr, &decode-state-sub-block)
  call 0x00fa4740                        ; "drain already-decoded, not-yet-copied-out samples, if any"
  test eax, eax
  jne  <advance-and-loop>                ; got samples directly -> advance output ptr, loop
  ; else: nothing pending -- decode a fresh unit
  call 0x00fa4950                        ; decode one compressed unit (result code in eax)
  cmp  eax, 2        ; je <end: status=2, "no more data">
  cmp  eax, 0x2b     ; je <end: finalize>
  call 0x00fa4810                        ; copy/convert the newly-decoded unit into the caller's buffer
advance-and-loop:
  ecx = [decode-state+0x1c] * eax        ; samples just produced, scaled
  ebx += ecx*2                           ; ADVANCE THE OUTPUT BUFFER POINTER BY samples*2 BYTES
  edi -= eax                             ; decrement remaining-requested count
  test edi, edi
  jne  loop                              ; keep going until the requested count is satisfied
```
The `ecx*2`-byte advance on the output pointer is the concrete "16-bit PCM" tell: each produced sample
advances the destination pointer by exactly 2 bytes. On exit, `[decode-state+0x0] = <bytes decoded>`,
`[decode-state+0x4] = <a status code>`, `[decode-state+0xc] = <a position/offset>` — this is where the
sample count slot 10 later reads back out of `[ebp]` (`decode-state+0x0`) comes from.

**Exhaustive confirmation this is the only vtable-reachable path to it:** an exhaustive raw-byte scan for
`E8`/`E9` (`call rel32`/`jmp rel32`) opcodes across the entire statically-linked Wwise block
(`0x00f22000`–`0x00fbffff`, the same range and method the prior unit used for `0x00f8fa70`) whose decoded
target is exactly `0x00fa49e0` finds **exactly 3 static sites, all three inside slot 10's own two
per-class bodies**:

| Address | Containing function |
|---|---|
| `0x00fa3090` | `CAkSrcBankVorbis` slot 10 (`0x00fa3000`) |
| `0x00fa40a3` | `CAkSrcFileVorbis` slot 10 (`0x00fa3e90`), first exit path |
| `0x00fa4118` | `CAkSrcFileVorbis` slot 10 (`0x00fa3e90`), second exit path |

No other slot, on either class, reaches this function by any static direct call or jump. (Indirect/
register-operand calls that might reach it at runtime through some other path are the same acknowledged
blind spot the prior unit flagged for its own equivalent scan — not ruled out by this method, but no
positive indirect-call evidence toward it was found either; see §11.9.3 below for the slots that were checked
for indirect decode-shaped calls and came back negative.)

---

#### 11.9.3 Every other slot checked and ruled out, with the specific evidence

Per the brief's own BFS list, every remaining unexamined/under-examined slot was read with the same strict
disassembler. None reaches `0x00fa49e0`, none reads/writes a destination sample buffer, and none advances
a position field the way slot 10 does.

- **Slot 1/7 (`0x00c0dea0`):** **CONFIRMED a true 1-instruction stub** — `ret 4`, nothing else. Not it.
- **Slot 6 (`0x00f80ff0`, 8 instructions):** confirmed matches §11.2's own description exactly — tests
  flag bit `0x2` of `[[this+8]+0xe5]`; if clear, returns `1`; if set, tail-jumps through *this object's
  own vtable* at offset `0x3c`. **New this pass: `0x3c / 4 = slot 15`** — i.e. slot 6's self-redispatch
  target is confirmed to be the object's own slot 15 (the StopStream-shaped slot), not an unidentified
  slot. So slot 6 is, at most, a conditional "treat this call as StopStream" forwarder — resolves the
  prior open question about what sits at `+0x3c`, and rules slot 6 out as the decode trigger.
- **Slot 3 (Bank `0x00fa31f0`, 47 insns; File `0x00f90130`, 44 insns; common tail `0x00f8fb00`, 14 insns):**
  read in full. This is genuinely "consume decoded output": it advances the same `this+0xdc` cursor slot
  10 also advances, bounds-checks it against the decode-state block's own end-of-data/loop-point fields,
  and on a loop-point crossing calls a small looping helper (`0x00fa2e30`) shared with slot 10's own tail.
  The common callee `0x00f8fb00` pushes the float constant `1.0` and calls `0x00f90fa0`, which itself calls
  `0x00f90ef0` then conditionally `0x00f6d1b0` passing a float and two pointer-sized values read from the
  *caller's* struct, not from any sample buffer — this is a notification/marker broadcast (consistent with
  Wwise's documented marker-callback mechanism), not decode. **No FPU/SSE instruction anywhere in this
  closure, no loop writing to a buffer.** Ruled out.
- **Slot 10's own Bank-side growth helper, `0x00f64b90`, and the shared allocator path (`0x00f32da0`):**
  already understood as allocation, not decode; not re-chased further since §11.9.1–§11.9.2 above now explain what
  slot 10 does with the buffer *after* allocating it.
- **Slot 11/16 (Bank `0x00fa2d70`/`0x00fa2f60`; File `0x00f90230`/`0x00f902a0`):** read in full. **Zero
  calls, zero branches beyond the function's own body** — straight-line FPU arithmetic only
  (`fldz`/`fild`/`fadd`/`fimul`/`faddp`/`fmul`/`fdivp`, a single samples-over-rate-shaped division).
  No loop is structurally possible here (no backward branch exists at all), so no buffer-write loop can
  be hiding inside. Confirms the existing GetDuration-shaped HIGH label; ruled out definitively.
- **Slot 12 (Bank `0x00fa31c0`, 9 insns, 0 calls — trivial field copy + return; File `0x00fa3890`, tail-
  jumps to `0x00f90080`):** `0x00f90080` (25 insns, 2 indirect calls, read in full) is a short guarded
  dispatcher with no loop and no call anywhere near `0x00fa49e0`. Ruled out.
- **Slot 15 (Bank `0x00fa2ef0`, 30 insns; File `0x00fa3d60`, 47 insns):** calls `0x00fa2e70`/`0x00fa45d0`
  (Bank) or `0x00f907b0`/`0x00f32e70`/`0x00fa45d0` (File) — all four read in full (`0x00fa45d0`: 10 insns,
  trivial; `0x00fa2e70`: 47 insns, calls only `0x00f8fb50`/`0x00fa2de0`/`0x00fa2e30`; `0x00f907b0`: 47
  insns, calls only `0x00f8fb50`/`0x00f90700`). None contains a call to `0x00fa49e0` or any FPU/SSE
  content. Ruled out — consistent with the existing StopStream-shaped HIGH label.
- **Slot 8/17 (the header-parse `StartStream`-shaped slots, Bank `0x00fa3450`, File `0x00fa4510`/
  `0x00fa3980`) and the shared RIFF/WAVE walker `0x00f8fcb0`:** not re-read line-by-line this pass (already
  well-characterized by the prior unit as header/chunk parsing), but **conclusively ruled out by the §11.9.2
  exhaustive scan alone** — if any of these called `0x00fa49e0`, directly or through any intermediate
  function containing a direct call to it, the scan would have found a 4th site; it found exactly 3, all
  inside slot 10.

---

#### 11.9.4 Independent corroboration: `ReleaseBuffer` (slot 2) touches the exact same fields

`CAkSrcBankVorbis` slot 2, `0x00fa2d20` (25 insns, read in full): a genuine **0-argument** `thiscall`
(plain `ret`, no stack cleanup) — frees `[this+0x8c]` (the identical lookahead/output buffer pointer slot
10 allocates and hands out) via `0x00f64be0`, then **zeroes `[this+0x24]`** — the identical first field of
the decode-state block that slot 10 reads as the just-decoded sample count (`movzx ecx, word ptr [ebp]`,
§11.9.1.2) — and zeroes `[this+0x8c]` itself. This is exactly the release-side counterpart of what slot 10
produces: free the buffer slot 10 handed out, and clear the pending-sample-count field so the next
`GetBuffer`-equivalent call starts clean. Two independently-read functions (`GetBuffer`-shaped and
`ReleaseBuffer`-shaped) agreeing on the same two field identities, from opposite directions, is strong
structural confirmation this is the right pair, not a coincidence.

---

#### 11.9.5 What's deliberately NOT claimed here

This pass stops at confirming **slot 10 is the vtable-level decode trigger** and that **`0x00fa49e0` is
the shared per-call decode-loop driver** both classes' slot 10 call into. It does **not** claim to have
identified the Vorbis-bitstream-specific internals (codebook/IMDCT/windowing) by name. Three further
functions in `0x00fa49e0`'s own closure were glanced at only far enough to confirm they are real,
substantial, floating-point-bearing code (not further elimination targets, but not characterized in
depth either) — left as leads for a dedicated decode-core-internals pass, not claimed as understood here:

- `0x00fa4950` (55 insns, decode-one-unit, tail-jumps to `0x00fa49b1` — not followed further).
- `0x00fa4810` (115 insns, the per-decoded-unit copy/convert step) calls `0x00fa6340` (19 insns, itself
  calling `0x00fa62a0`) and, per channel in a loop (`0x00fa4860`–`0x00fa487b`, confirmed a real loop, not
  a heuristic artifact), `0x00fa5970` (19 insns, **read in full — a genuine per-sample copy loop**:
  `dest[i] = src[i*2]`-shaped, writing consecutive output values into the caller-visible buffer; this is
  the single most concrete "write into a caller-supplied output buffer in a loop" instruction sequence
  found in this whole pass) and `0x00fa5fa0` (193 insns, calls `0x00ea2ca0` ×5, `0x00fa6dd0`, `0x00ea23b0`,
  `0x00fa72b0`, `0x00fa4da0` — not characterized).
- `0x00fa5fa0`'s own closure reaches `0x00fa59a0` (268 insns, **CONFIRMED real floating-point work**:
  `movss`/`mulss`/`subss`/`cvttss2si` — a float-to-fixed/windowing-shaped inner routine, not traced to a
  specific Vorbis algorithm step in this pass).

None of this changes the headline finding (slot 10 is the trigger; the loop and the output write are
already directly confirmed at §11.9.1–§11.9.2 without needing these deeper functions), but naming these addresses
now saves a future pass from re-finding them from scratch.

#### 11.9.6 The Vorbis decode-core internals below `0x00fa49e0`, opened (2026-10-08) — a complete, textbook Vorbis I pipeline

A follow-up unit opened every lead §11.9.5 left unnamed, plus their own further callees, and found a
complete decode pipeline. **All instruction counts below are exact, directly disassembled counts — a
few differ slightly from §11.9.5's own earlier estimates for the same addresses (`0x00fa5fa0`: 193→276
insns; `0x00fa59a0`: 268→304 insns); not reconciled, most likely a recount/boundary difference between
passes, flagged rather than silently overwritten.** Role labels below (bit-reader, floor/residue decode,
channel coupling, inverse MDCT) are HIGH-confidence structural/behavioral readings matching the
publicly-documented Vorbis I reference algorithm's own shape — not name-confirmed by any string/RTTI
evidence, and no Vorbis/Wwise source of any kind was read to produce them.

**Bit-reader (CONFIRMED):** `0x00fa62a0` (49 insn, 0 calls) extracts up to 32 bits via a classic
`(1<<n)-1`-style mask table at `0x0134f340`; `0x00fa6340` (19 insn, 1 call) advances the bit position by
N. This pair is the single most-referenced primitive in the whole area — its caller list includes
`0x00fa63c0` (§11.11.3a's codebook parse), `0x00fa5d80`, `0x00fa67f0`, `0x00fa6dd0`, and others, i.e. it
backs every codebook/floor/residue helper, not just the top-level decode loop.

**Packet framing and dispatch (CONFIRMED):** `0x00fa4950` (55 insn, 0 calls) pulls one 16-bit-length-
prefixed compressed unit from the lookahead buffer. `0x00fa4810` (115 insn, 3 calls) is the real
per-packet dispatcher: reads 1 bit (the mode/window-type flag), updates lapping-boundary fields on the
decode-state block (`+0x44`/`+0x48`/`+0x54`), calls `0x00fa5970` (19 insn — a strided gather copy
retaining half of the previous block's output for overlap), and only THEN conditionally calls the real
spectral decode when window-boundary bookkeeping calls for it (not every call necessarily transforms a
fresh block).

**Spectral decode orchestrator, `0x00fa5fa0` (276 insn, 6 calls) — floor → couple-flags → residue →
uncouple → floor-apply → IMDCT, in order, textbook Vorbis I shape:**
1. Per-channel floor decode via `0x00fa6dd0` (not opened in depth — HIGH by call shape), setting a
   per-channel "silent" flag and zeroing that channel's spectral buffer when set.
2. Channel do-not-decode flags: a residue vector is marked skip-eligible only when BOTH channels of a
   coupled pair were flagged silent in step 1 — the textbook Vorbis `do_not_decode_flag` shape.
3. Per-submap residue decode via `0x00fa72b0` (not opened in depth — HIGH by call shape and its
   `0x1c`-byte residue-config argument).
4. Channel uncoupling, inline: a branchy sign-dependent add/subtract loop over channel-pairs — the exact
   shape of the public Vorbis I spec's magnitude/angle channel-decoupling reconstruction (four distinct
   sign-combination branches matching the spec's own case analysis, not a generic clamp).
5. Per-channel floor-curve application via `0x00fa7060` (not opened in depth — HIGH by call shape).
6. Per-channel inverse MDCT via `0x00fa4da0` (below).

**The inverse MDCT, `0x00fa4da0`: body `0x00fa4da0`-`0x00fa5969`, exactly 812 instructions, ZERO calls**
— a single self-contained SSE/FPU function. Structure: a bit-reversal permutation (nibble lookups into a
16-byte table at `0x0134f330`), a long sequence of hand-unrolled SSE radix-4/8 butterfly kernels reading
fixed twiddle tables (`0x0134f2e0`/`2f0`/`300`/`310`/`320`), then recursive-doubling complex-butterfly
stages (a second twiddle-table family, `0x0134d2d0`+offsets, `0x0134e2d8`, `0x0134e6d0`) doubling the
stage size each pass. **HIGH, structural match only, not name-confirmed:** this shape — bit-reversal via
nibble-lookup, fixed small-radix SIMD butterfly base case, recursive-doubling complex butterflies with
twiddle tables, zero calls — is structurally indistinguishable from the well-known, publicly-documented
split-radix FFT-based inverse MDCT algorithm (the same public algorithm the Vorbis I reference decoder
uses). No string/RTTI evidence ties this address to any particular public function name.

**Windowing, overlap-add, and the saturating PCM write (CONFIRMED):** `0x00fa46f0` (23 insn, 0 calls) is
a block-size-keyed window-table selector — 5-way, not 2-way (block sizes 256/512/1024/2048/4096, tables
`0x013494c0`/`0x013496c0`/`0x01349ac0`/`0x0134a2c0`/`0x0134b2c0`), consistent with this being a
generalized/modified decoder core, matching this document's own already-established finding that this
Wwise Vorbis variant wraps stock bitstream content with a proprietary layer. `0x00fa4740` (78 insn, 2
calls) runs at the TOP of `0x00fa49e0`'s own loop — before a fresh packet read is even attempted — to
flush samples left over from a prior call: selects both the current and previous block's window tables,
then calls `0x00fa59a0` once per channel. **`0x00fa59a0` (304 insn, 0 calls) is the real windowed
overlap-add and 16-bit PCM writer** — per channel: a cross-fade region combining the previous block's
tail with the current block's head, weighted by both window tables (with a sign-flipped variant past the
window's symmetry midpoint); outside the overlap region, a straight scale-and-truncate; throughout, every
output value is scaled by a fixed constant `0.001953125` (`2^-9`) and written through a textbook
saturating int32→int16 clamp directly into the caller-visible PCM buffer. This is the genuine final
output-production step — everything upstream produces floating samples, this is where they become
playable PCM.

**Summary table, all counts CONFIRMED by direct disassembly this pass:**

| Address | Insns | Calls | Role |
|---|---|---|---|
| `0x00fa62a0` | 49 | 0 | bit-reader: extract up to 32 bits, masked |
| `0x00fa6340` | 19 | 1 | bit-reader: advance bit position by N |
| `0x00fa4950` | 55 | 0 | packet framing: pull next length-prefixed compressed unit |
| `0x00fa4810` | 115 | 3 | per-packet dispatch: reads exactly 1 hardcoded (not computed) mode bit — no packet-type bit precedes it — then derives long-block window flags from running state, never from the bitstream; full instruction-level detail in §11.11.4a |
| `0x00fa5970` | 19 | 0 | strided copy: retain previous block's second half for next overlap |
| `0x00fa5fa0` | 276 | 6 | spectral decode orchestrator: floor → couple-flags → residue → uncouple → floor-apply → IMDCT |
| `0x00fa6dd0` | not opened | — | HIGH: per-channel floor decode |
| `0x00fa72b0` | not opened | — | HIGH: per-submap residue/VQ decode |
| `0x00fa7060` | not opened | — | HIGH: per-channel floor-curve multiply |
| `0x00fa4da0` | **812** | **0** | the inverse MDCT: bit-reversal + SIMD butterfly cascade |
| `0x00fa4740` | 78 | 2 | per-channel windowing dispatch: selects current+previous window tables |
| `0x00fa46f0` | 23 | 0 | window-table-by-blocksize selector (5-way; tables `0x013494c0`..`0x0134b2c0`) |
| `0x00fa59a0` | 304 | 0 | windowed overlap-add + saturating float→int16 PCM write |

**OPEN, for whoever continues:** `0x00fa6dd0` (floor decode), `0x00fa72b0` (residue decode), `0x00fa7060`
(floor-curve apply) identified by call position/argument shape only, not disassembled in full; the
sibling family visible only in `0x00fa6340`'s own caller list (`0x00fa63c0`, `0x00fa5d80`, `0x00fa67f0`,
others) almost certainly holds the actual Huffman/VQ codebook lookup logic, not traced; the decode-state
block's fuller field map is incomplete; whether the 5 window tables match the Vorbis I spec's closed-form
window formula was not checked (only their selection mechanism); `0x00fa4da0`'s "IMDCT" label is
structural-match only, not name-confirmed. Full detail: `scratchpad/havok_du2_wwise_uw4b_2026-10-08.md`.

**Review status (2026-10-08): CONFIRMED for the pipeline structure, all addresses, and instruction
counts; HIGH CONFIDENCE for the Vorbis-algorithm role labels (structural match, not name-confirmed) —
independently re-derived from the executable.**

---

---

### 11.10 No further edit proposed to `review/scoping-audio.md` §8.4

The dispatch brief permits direct correction of `review/scoping-audio.md`, but by the time this unit
reached the point of writing one up, the parallel **U-W4b agent had already landed a correction to the
same passage** (the "⚠→✅ CORRECTED 2026-10-07 (U-W4b re-investigation)" block under the U-W4b bullet),
independently reaching the same core fact this unit found by its own disassembly: `0x00f8fa70` is a
2-instruction this-adjustor thunk, not the 1,809-instruction decode function the original note
described, and neither `0x00fa3e90` nor `0x00fa3980` calls it. Rather than write a second, overlapping
edit to the same sentence, this unit's correction is recorded here (§0 item 3, §5) and left for the
integrator to fold alongside the U-W4b agent's own note when `spec-audio-runtime.md` is eventually
written — the two independent findings cross-check each other exactly, which is worth preserving in the
integration, not collapsing into a single unattributed claim.

One thing this unit found that the landed U-W4b correction's own text (as read in `review/scoping-audio.md`)
does **not** mention: the **exact two-hop path for `CAkSrcFileVorbis`'s slot 9** (`0x00fa4140` →
`0x00f90060` → `0x00f8fa70`, §5.2), and the **two destructor-path call sites** (`0x00f90330`/`0x00f90590`,
§4.2/§5), as distinct from slot 9's own tail-chain. The U-W4b correction speaks of "6 real sites" in
aggregate and reads `0x00fa3170` (Bank's slot 9) in full, but this unit's own §5 table is the more
complete per-class, per-site breakdown — worth keeping both when integrating.

---

### 11.11 The Wwise-specific Vorbis wrapping layer: packet framing, header rebuild, codebook storage/selection, seek/loop metadata (unit U-W4c)

Scoped explicitly to the Wwise-specific wrapper around Vorbis, not the generic Vorbis bitstream math
(codebook Huffman decode, floor/residue reconstruction, IMDCT, windowing/overlap-add — public, standard
Ogg Vorbis, out of scope; libvorbis, BSD-licensed, covers that for Team B). Answers the four questions
§11.7.1/§11.0 item 5 left open about the unopened `"vorb"` chunk. Ghidra-free (a purpose-built strict
control-flow-aware disassembler and a frame-relative stack-offset tracker, both self-checked against the
already-published `0x00f8fa70` 2-instruction result, and the stack tracker additionally cross-checked
against an independent by-hand derivation of `0x00f8fcb0`'s own 10-argument convention, 9/9 reads
agreeing after one sign-convention fix). No Ghidra project opened; no leaked Audiokinetic/Volition/
Havok/RAD material read or used.

**Confidence key, as elsewhere in this file:** CONFIRMED (read directly or exhaustively scanned this
pass); HIGH (structural inference, not chased to the same depth); HYPOTHESIS (plausible, not checked).

#### 11.11.0 The four questions, answered up front

1. **Packet framing — CONFIRMED.** A fixed 16-bit little-endian length prefix precedes each compressed
   packet inline in the `"data"` chunk's byte stream — no Ogg page headers, no CRC, no lacing table. The
   identical primitive is reused at setup time to read the stripped setup packet (§11.11.2). Sample-
   accurate position is not stored in the stream at all: it is a running per-instance counter
   (decode-state `+0x0`, aka the already-published `this+0xdc`, §11.9.1.2) the engine advances by the
   decoded-sample-count after every `GetBuffer`-equivalent call — there is no granule-position field to
   read back.
2. **Stripped/rebuilt setup headers — CONFIRMED mechanism / HIGH on individual field identities.** No
   literal 3-packet Ogg Vorbis header sequence is stored per file. A proprietary 42-byte `"vorb"` RIFF
   chunk carries block-size exponents, a resource-identity hash, two offset-shaped fields, and a sample
   count, seeding a rebuilt in-memory setup object (mode table + mapping table) built from one further
   small, 16-bit-size-prefixed "setup packet" read from the compressed stream using the same framing as
   item 1 (§11.11.2–§11.11.3).
3. **Codebook storage/selection — CONFIRMED structural negative + CONFIRMED positive mechanism (Bank
   only).** No large static codebook table exists anywhere in this executable reachable from this
   closure (exhaustive ~1,282-instruction scan for any `0x01xxxxxx`/`0x02xxxxxx`-range literal: zero
   hits). Instead, a per-session, reference-counted runtime cache — hash-keyed off vorb-derived
   decode-state bytes, built fresh from each stream's own stripped setup packet on a miss — is shared by
   any two streams whose hash matches (§11.11.4). CONFIRMED end to end for `CAkSrcBankVorbis`; **File's
   own path to this cache was not located** (open item).
4. **Seek/loop metadata — CONFIRMED.** Loop points come from the standard, public WAV `"smpl"` chunk's
   first `SampleLoop` record; when absent, both classes fall back identically to
   `[0, vorb.firstDword − 1]` (that dword is deliberately excluded from the main 38-byte vorb copy and
   read separately). Named markers/cue points come from the standard, public WAV `"cue "` + `"labl"`
   chunk pair (§11.11.5).

#### 11.11.1 Packet framing: a fixed 16-bit size prefix, no Ogg pages (CONFIRMED)

`0x00fa4950` (55 instructions, "decode one compressed unit," called from the shared decode-loop worker
`0x00fa49e0`, §11.9.2) reads a small decode-state sub-block (`+0x0` compressed-buffer base, `+0x4` read
cursor, `+0x8` valid-byte count) and does exactly this: if fewer than 2 bytes remain, return `0x2b`
(need more data); otherwise read a 16-bit length field directly at the cursor, reject it with status `2`
if it exceeds a caller-supplied max, otherwise advance the cursor by `2+size`, hand back a data pointer
just past the 2-byte prefix plus the size, and return `0x2d` (success) — these three status codes match
what the decode loop (`0x00fa49e0`) already checks for. **No Ogg page headers, no CRC, no lacing/segment
table exist anywhere in THIS framing** — a bare 16-bit prefix is the entire primitive. The identical
`movzx`-word-then-advance-by-`2+len` shape reappears, unchanged, inside the per-instance setup-finalize
step (§11.11.2) to pull the stripped setup packet out of the same stream before any audio packet is read.

**Precision, 2026-10-08 (not a correction — this describes framing *within* one packet, which stands
exactly as written): for `CAkSrcFileVorbis`'s default path, a separate seek/index table precedes the
whole packet stream and is skipped by an explicit seek before the first packet-length read —
`CAkSrcBankVorbis` has no such table.** See §11.11.5's rewritten seek-table material for the full,
exe-confirmed mechanism; §11.11.2 for the table's own byte-length field.

#### 11.11.2 The `"vorb"` chunk's own 42 bytes (CONFIRMED copy mechanics; HIGH on several field identities)

The shared RIFF/WAVE chunk walker, `0x00f8fcb0` (re-confirmed at exactly 208 instructions with a
merge-aware walker, after an initial pass under-counted at 51 by refusing to inline an unpadded, nearby
jump that is in fact a same-function continuation), does not interpret `"vorb"` at all — on first seeing
the tag it copies exactly 42 bytes (10 dwords + 1 word) raw, via `rep movsd`/`movsw`, to a caller-supplied
10th argument. (This function takes exactly 10 stack arguments, cross-validated twice — by independent
address arithmetic and by a frame-tracking tool, 9/9 reads agreeing: RIFF-blob pointer+length, `"fmt "`
copy destination+capacity, a cue-point-array object, loop-start/loop-end output pointers, data-chunk
length/offset output pointers, and the 42-byte vorb destination.)

Both classes' header-parse step (Bank `0x00fa3450`; File `0x00fa3980`) then copy **38 of those 42 bytes**
(skipping the first dword) into the already-established 184-byte decode-state block (§11.3.2) at a fixed
relative `+0x7c` on both classes (`this+0xa0` Bank-absolute, `this+0xd0` File-absolute), confirmed with a
branch-aware re-trace at both copy sites. The skipped first dword is read back separately and used as the
loop-point fallback (§11.11.5). **Mapping, re-confirmed 2026-10-08: decode-state offset = `0x7c +
(vorbByte − 4)` — the formula itself was never wrong; what was wrong was which function reads the
exponent bytes and from where.** `0x00fa6370` (26 instructions, re-read in full) does **not** read
decode-state at all — it takes the two exponents as plain integer *arguments* and only touches a
caller-supplied setup-resource buffer. Its caller, `0x00fa4c30` (the setup-cache acquire function),
reads the real exponent bytes directly off decode-state at **`+0xA0`/`+0xA1`** (`movzx edx,[eax+0xa1];
movzx eax,[eax+0xa0]`) and passes them in — **not** `+0x7c`/`+0x7d` as previously claimed. Via the
formula, `+0xA0`/`+0xA1` correspond to vorb bytes **40/41**, independently reproducing Team B's own
empirical finding (and a fresh extraction this pass of 9 more real vorb chunks from the same file: all 9
give vorb[40]/[41]=`0x08`/`0x0b` → blocksize 256/2048, legal; vorb[4]/[5]=`0xb0`/`0xba` → illegal as an
exponent).

| decode-state offset | vorb-chunk byte(s) | Role | Confidence |
|---|---|---|---|
| (read separately) | 0–3 | loop-point fallback, sample-count-shaped | CONFIRMED mechanism; HIGH "total sample count" |
| `+0x7c` (dword) | 4–7 | a small, near-constant (4 distinct values across 981 real records observed) per-profile term, summed into `this+0x48`'s offset computation (§11.11.5) — ties to the same setup/codebook profile the shared setup-resource cache keys on (`+0x9c`/vorb[36:40], §11.11.4), not an independent packet offset | CONFIRMED location and consuming formula (§11.11.5); role as "profile-tied constant" HIGH, not independently named |
| `+0x80` (dword) | 8–11 | **downgraded 2026-10-08** (was: HIGH "first-audio-packet-offset") — ranges 19,420–7,267,589 across 981 real records, far too large to be a packet offset a few hundred bytes past the header; a bitrate sanity check is consistent with "total compressed audio payload size." **Falsified as a direct packet-offset reading a second, independent way: Team B tested this gloss against all 493 of their own real extracted files under 3 candidate placements and found 0/493 plausible.** Summed into `this+0x4c`'s offset computation regardless of its exact name (§11.11.5 has the exe-confirmed formula) | CONFIRMED location and consuming formula; semantic name HYPOTHESIS only ("total compressed payload size," not confirmed by any direct read) |
| `+0x88` (dword) | 16–19 | byte-length of a per-file seek/packet-index table that precedes the setup+audio packet stream (entries are `(u16 sampleCount, u16 byteSize)`, so **entryCount = vorb[16:20]/4**) — CONFIRMED statistically: always an exact multiple of 4 across 981/981 real records. Also summed into both `this+0x48`/`this+0x4c` (§11.11.5) as "skip past this table" | CONFIRMED (statistical, 981/981 zero exceptions) for the byte-length/divisibility fact; CONFIRMED (disassembly) for its role as a summed offset term; the exact entry-count formula is HIGH, not independently walked by any code (no loop reads this table — the runtime jumps past it, never parses it, §11.11.5) |
| `+0x94` (dword) | 28–31 | byte-size of a freshly-built shared setup resource, used directly as the pool-allocator size argument | CONFIRMED — direct read in `0x00fa4c30` |
| `+0x9c` (dword) | 36–39 | hash key into the shared setup-resource cache (bucket = `hashKey % 31`, re-derived from the real reciprocal-multiply sequence — **corrected from the previously-stated 37**) | CONFIRMED — independently re-verified this pass |
| **`+0xA0`/`+0xA1`** (bytes) | **40 / 41** | Vorbis `blocksize_0`/`blocksize_1` exponent bytes, read directly by `0x00fa4c30` and passed as arguments to `0x00fa6370` | **CONFIRMED — corrected 2026-10-08 from the retracted `+0x7c`/`+0x7d`; matches Team B's and this pass's own independent empirical bytes** |

Bytes vorb[12..15] (decode-state `+0x84..+0x87`) and vorb[20..35] (`+0x8c..+0x9b`) remain unattributed to
any confirmed read — an honest gap, not a retraction. (vorb[16..19] is no longer in this unattributed
range — see the `+0x88` row above.)

**Team B cross-reference, 2026-10-08 (their own empirical finding, not independently re-derived on this
side — labeled separately per standing convention):** across their own 493-file corpus, `dataChunkSize −
vorb[8:12) − cleanRunEnd == 4` holds exactly, with zero variance. This is consistent with, and pins down
more precisely than this pass's own statistics alone, the "leading-table boundary" — where the
seek-table-plus-profile-constant region (vorb[4:8]+vorb[16:20], §11.11.5) ends relative to the whole
data-chunk's own declared size. Not independently confirmed by disassembly on this side; reported as a
Team-B-sourced cross-check consistent with the exe-confirmed formulas above, not as its own CONFIRMED
claim.

**Chunk-size/layout question, answered:** `0x00f8fcb0`'s own `"vorb"` handler copies a **fixed** 42
bytes via `rep movsd`/`movsw` — the copy count is not driven by the chunk's own on-disk declared size
field. **The 42-byte header layout does not vary with chunk size** — CONFIRMED by disassembly, the same
read already cited above. If a real file ever declares a `"vorb"` chunk size other than `0x2a` (42,
confirmed as the only size seen across 981 real records, zero exceptions), the standard RIFF chunk
walker still advances its own cursor by that declared size for walking purposes, but this copy itself
reads exactly 42 bytes regardless.

`0x00fa6370` (26 instructions) reconstructs `blocksize0 = 1<<exp0`, `blocksize1 = 1<<exp1` from its two
exponent arguments and rejects them unless `64 ≤ blocksize0 ≤ blocksize1 ≤ 8192` — the exact legal range
the public Vorbis I spec gives for its identification header's block-size pair.

#### 11.11.3 The stripped setup packet and rebuilt mode/mapping tables (CONFIRMED mechanism; HIGH field roles)

Bank's per-instance "finalize setup" step, `0x00fa3290` (79 instructions), reads one more 16-bit-prefixed
packet off the same compressed stream (the identical framing as §11.11.1) and hands it straight to
`0x00fa4c30(ecx=0x02cc5f40, &decode-state, parentBankRecord, &{dataPtr,packetLen}, flag=0)` — the stripped
per-file Vorbis setup packet, confirmed by its framing and destination.

`0x00fa4c30` (136 instructions, read in full) is the **acquire** half of the exact same hash-bucketed
table whose **release** half, `0x00fa4bb0`, §11.4.1 already identified from the teardown side: hash key =
decode-state `+0x9c` (vorb-derived); bucket = `hashKey % 31` **[corrected 2026-10-08 from a previously
stated 37 — re-derived from the real reciprocal-multiply instruction sequence and checked numerically
against real hash values]**; on a chain hit, increment `entry+0x18`'s
refcount (the SAME field `0x00fa4bb0` decrements) and return; on a miss, allocate a `0x1c`-byte node plus
a vorb-sized (`+0x94`) data buffer via the shared Wwise pool allocator (`0x00f32da0`), reserve the first
`0x30` bytes unconditionally (purpose not chased), validate the block-size exponents (§11.11.2's
`0x00fa6370`, fed exponent bytes read here from decode-state `+0xA0`/`+0xA1`), parse the setup packet
itself (`0x00fa63c0`, opened in full 2026-10-08, see §11.11.3a below), set the new node's refcount to 1,
and return it.

##### 11.11.3a `0x00fa63c0`: the stripped Vorbis setup-packet parse (CONFIRMED structure, 2026-10-08)

Section order confirmed matching standard Vorbis I exactly (codebooks → floors → residues → mappings →
modes, each a 6-or-8-bit-plus-1 count → record-size compute → bump-allocate → per-entry builder, all
sharing one bitstream cursor via the generic bit-reader `0x00fa6340`/`0x00fa62a0`) — **except the
vestigial time-domain-transform placeholder section is dropped entirely.**

~~Codebooks are confirmed **inline per-stream, not ID-referenced** — `0x00fa7da0` reads dimension (4 bits,
shrunk from the standard 16), entries (14 bits, shrunk from 24), an ordered flag, and (new field, not in
standard Vorbis at all) a 3-bit "length field width" byte, all directly off the bitstream — strengthening
§11.11.4's "no static codebook library" conclusion one level deeper. floor_type/residue_type/
mapping_type/mode windowtype+transformtype are all never read and hardcoded to their only-ever-legal
values; channel count is not read from the packet either — it's supplied by the caller as
`popcount(decode-state+0x6c)`, passed in as an explicit argument. One OPEN item: a 10-bit per-codebook
field indexes a 1024-entry table at `0x134f3d8` before each `0x00fa7da0` call; its consuming read inside
`0x00fa7da0` wasn't found — more likely an allocator size-class hint (given its proximity to the
confirmed size-class table `0x134d2c0` used throughout this code) than a codebook-ID library, but not
confirmed either way.~~

**MAJOR CORRECTION (2026-10-08, closes the OPEN item above and reverses this section's own
"inline per-stream, not ID-referenced" framing — direct disassembly, `0x00fa6439`/`0x00fa643a`/
`0x00fa643f`, plus a full static dump of the table and every real codebook body):** the main
setup-packet bitstream does **not** read a codebook's fields inline at all. For each codebook slot it
reads a fixed, bare **10-bit index** and nothing else at that position. That index selects a pointer
out of the table at `0x134f3d8` — which, despite being sized for 1024 pointer slots, holds only
**599 real codebook entries (indices 0-598)**: slot 599 is a NULL terminator, and indices 600-1023 read
off the end of the real table into an unrelated `.data` float array (confirmed spurious — several
different "garbage" slot numbers resolve to the identical address, which a genuine distinct table entry
cannot do). The resolved pointer becomes the data source of a **brand-new, independent bitstream
cursor**, local to that one codebook; `0x00fa7da0` then reads dimension/entries/ordered-flag/
length-field-width/lookup-table — the exact field grammar already described above — from **that
separate static buffer**, never returning to the main packet's own bitstream position until the next
codebook's 10-bit index is read. The field grammar itself is unchanged and correct; what was wrong was
*where* it reads from — not the per-stream packet directly, but each codebook's own static body,
reached via the index. **The table is a real, static, compiled-in codebook library, not an allocator
size-class hint.** floor_type/residue_type/mapping_type/mode windowtype+transformtype are still never
read and hardcoded (unaffected by this correction); channel count is still supplied by the caller
rather than read from the packet (also unaffected).

**Validation status, 2026-10-08 UPDATE — ~~libvorbis acceptance PENDING~~ now RUN, real acceptance
achieved.** All 599 real codebook slots parse cleanly with zero errors under this field grammar; zero
byte-range overlaps across all 599 consecutive codebook bodies (every gap is exactly 0 or 1 byte —
alignment slack, nothing else); of the 437 codebooks that carry a VQ lookup table, zero violate the
standard Vorbis feasibility bound `quantvals^dim <= entries < (quantvals+1)^dim`, and two hit it as an
exact equality (slot 0: dim=2, entries=121=11²; slot 1: dim=4, entries=81=3⁴). **A complete, minimal
but legal Vorbis I setup header, built from 3 real codebooks (library slots 7/61/5) transcoded via the
exact grammar below plus a trivial legal floor1/res0/mapping0/mode, was fed to
`vorbis_synthesis_headerin` against Team B's own already-built `vorbisstatic.lib`/`oggstatic.lib` — id,
comment, and setup headers all returned `rc=0 ACCEPTED`.** Kraft's inequality (`Σ 2^-length <= 1`,
independent of sparse/dense/ordered shape) additionally checked across all 599 real codebooks under
this grammar: zero violations, 598/599 produce an exactly-complete code (the one exception, slot 598,
is an **ordered** codebook, unrelated to the dense-case grammar below — flagged separately, non-
blocking). **This mechanism is now libvorbis-validated, not merely disassembly-matched.** Also
resolved in the same pass: there is no separate "explicit" VQ lookup-table bitstream shape — three
internal storage-layout branches (stack-vs-arena allocation, byte-vs-word element size) were
previously misread as a second format; there is only one shape, always exactly `quantvals` entries of
`value_bits` bits each. Standard Vorbis's `lookup_type==2` does not exist in this implementation at
all, consistent with this document's broader pattern of dropped/unused standard fields. The "has VQ
lookup table" flag itself is a single **1 bit**, not a 2-bit or 4-bit maptype field — directly checked
against the disassembly; an empirical "maptype is 2 bits" reading does not hold once codebook data is
correctly sourced from the table-selected buffer (above) rather than the main packet stream.

**The non-ordered (`ordered==0`) codeword-length encoding, pinned exactly (2026-10-08, closes a real
implementation bug in a prior Team B transcoder attempt, `TEAM B/src/wwise_setup_rebuild.cpp` ~line
200, `transcodeOneCodebook`).** After `entries` and the ordered flag, for `ordered==0`: **width** = 3
bits, raw value 0-7, used directly as the per-entry length field's own bit count — **NOT itself
+1-biased** (only the resulting per-entry length VALUE carries the +1 bias, below; easy to misstate
the other way). Immediately after width, **unconditionally for every `ordered==0` codebook, regardless
of whether it turns out sparse or dense: a sparse flag, 1 bit** (`0x00fa7dfa`) — **this is the exact
field a prior transcoder attempt omitted**, going straight into the dense per-entry loop instead,
which shifts every subsequent bit read in that codebook left by one and corrupts the length list, the
has-lookup flag, and (if set) the lookup table fields for the rest of that codebook. Then, per entry
(`entries` times): **if sparse≠0** — read present (1 bit); if present, read length (`width` bits),
store `length+1`; if not present, store `0` (unused-entry sentinel, no length bits consumed). **If
sparse==0 (the dense case proper)** — read length (`width` bits) unconditionally, every entry, no
present flag; store `length+1`. Verified character-by-character against 3 real codebooks (library
slots 7/61/5: dim/entries/width/sparse = `1/10/2/1`, `1/18/3/1`, and `2/25/3/0` respectively — the last
one the dense case proper, 25 unconditional 3-bit reads, no present bit at all), each closing its exact
on-disk byte length (6/12/23 bytes) with zero slack. Full worked examples and the libvorbis test
harness: `scratchpad/wwise_vorbis_setup_header_bitexact_2026-10-08.md` Part 5.

**Actual codebook data, for implementers who cannot read the executable directly:** the full byte
contents of all 599 real codebooks are transcribed as data in **`spec-audio-codebook-library.md`**
(the index table plus the complete raw blob as offset-prefixed hex, with a SHA-256 of the blob for
byte-exact verification — synced like any other spec file, data only, no code). That file is the
canonical, Team-B-reachable copy; the original scratch working files
(`scratchpad/wwise_codebook_library_index_2026-10-08.csv`/`_blob_2026-10-08.bin`/`_blob_2026-10-08.hex.txt`)
are kept only as the source this was generated from. Full methodology:
`scratchpad/wwise_vorbis_setup_header_bitexact_2026-10-08.md`.

Full bit-width-by-field comparison table against standard Vorbis I, and the
floor/residue/mapping internals beyond their leading count/type fields, are in
`scratchpad/uw4c_vorb_contradiction_fix_and_setup_parse_2026-10-08.md`.

The resolved object (decode-state `+0x20`, aka `this+0x44`) is read a second way, through a separate
pointer at decode-state `+0x10` (populated by `0x00fa4600`, called right after `0x00fa4c30` but not
itself disassembled this pass — open item), inside the per-unit decode/copy closure (`0x00fa4810`/
`0x00fa5fa0`, already on record as real FPU/SSE Vorbis math, correctly left uncharacterized): a 2-byte
mode record (block-flag byte + mapping-number byte) is read per index, then used to index a 4-byte-stride
block-size table and a 20-byte-stride mapping table off the same base pointer — structurally exactly the
public Vorbis setup header's modes-feeding-mappings shape (CONFIRMED that the two tables exist and are
indexed this way; HIGH, not verified field-by-field, that the 20-byte mapping record matches the public
spec one-to-one).

The generic bit-reader beneath all of this, `0x00fa62a0` (49 instructions), assembles up to 5 bytes and
masks against a 32/33-entry `(1<<n)-1`-style table at `0x0134f340` — a universal Huffman/bit-unpacking
helper used by codebook decode, floor/residue decode, *and* the setup-packet parse alike. **This is the
generic bit-reader every Vorbis decoder needs, not a codebook table and not evidence of a shared codebook
library** — flagged only so a future pass doesn't mistake it for one.

#### 11.11.4 Codebook storage and selection: a per-session VORB-reuse cache, PLUS a real static per-codebook-body library (CORRECTED 2026-10-08 — title was "no static table in the exe," now wrong)

**This section's original title and framing ("no static table in the exe," "the complete picture, not
two competing hypotheses") are superseded by §11.11.3a's 2026-10-08 correction below — there IS a
real static codebook-body library, 599 real entries at `0x134f3d8`. The two mechanisms described here
coexist at different levels, they are not alternatives:** a genuine shared, reference-counted
per-session cache (§11.11.3) lets two STREAMS with an identical setup hash reuse one in-memory parsed
VORB copy, built on a miss by parsing that stream's own stripped setup packet — this part is unaffected
by the correction below and still CONFIRMED. But parsing that setup packet itself, for every codebook
slot, means reading a bare 10-bit index and resolving it against the SEPARATE static per-codebook-body
library §11.11.3a now documents — a second, lower-level sharing mechanism this section's own exhaustive
negative (the ~1,282-instruction closure, zero `0x01xxxxxx`/`0x02xxxxxx`-range literal hits) did not
rule out, because that closure was scoped to the VORB-cache's own allocation/lookup code, not to
`0x00fa7da0`'s internal behavior. The selection key/size living inside the copied vorb bytes (§11.11.2)
is still CONFIRMED by direct disassembly; that the VORB-cache hash genuinely identifies a per-file
setup/codebook profile is still HIGH, not cross-checked against two real files sharing a hash.

~~**Strengthening note (2026-10-08, does not retract the above):** opening the setup-packet parse itself
(`0x00fa63c0`, §11.11.3a) confirms this one level deeper. `0x00fa7da0`'s per-codebook reader (dimension,
entry count, ordered flag, length-field width) takes every one of those values straight off the
bitstream for each codebook in the packet, in order — there is no codebook-ID field read anywhere in the
section that could index into a shared table. The cache described above really is the only sharing
mechanism in the exe; "inline per-stream" and "hash-keyed reuse cache" are the complete picture, not two
competing hypotheses.~~ **RETRACTED 2026-10-08 — see §11.11.3a's own correction above: this
"strengthening note" is exactly the claim that turned out wrong. There IS a codebook-ID field (a bare
10-bit index, one per codebook slot) and it DOES index into a real shared static table — the opposite
of what this note asserted. Kept here, struck, as a record of the error rather than silently deleted.**

**The codebook library RULED OUT as the cause of Team B's 488/493 `vorbis_synthesis_headerin` failures
(2026-10-09, independently reproduced from real bytes).** Team B reported that, after the setup-packet
offset fix above, 488 of 493 real records fail at synthesis init reporting a codebook count of 32 or
34, while the 5 that succeed report 35 or 42 — raising the question of whether the codebook list or
library itself was the problem. A from-scratch extraction pipeline (never referencing Team B's own
code) sampling this project's own `cutscene_sounds.vpp_pc` archive independently reproduces **exactly**
Team B's split — 488/493 = count 32-or-34, 5/493 = count 35-or-42 — strong evidence this project's
grammar is applied identically on both sides. **Zero of the 15,890 codebook-index reads across all 493
records fall outside the valid 0-598 range; every one of the 143 distinct referenced codebook bodies
re-parses byte-exact and Kraft-complete straight from the already-published 599-slot library; the
"low" (32/34) and "high" (35/42) profiles even share an identical 15-codebook contiguous subsequence
verbatim** (`82, 284, 528, 354, 197, 142, 582, 461, 115, 496, 210, 73, 151, 360, 57`, at different list
positions in each) — ordinary shared-sub-library encoder behavior, not an anomaly. Zero bit-budget
overruns and a statistically identical codebook-section share of the setup packet (23.6% low vs. 24.1%
high) across 7,983 real records sampled from 6 archives (base game, cutscenes, all 3 DLCs) rule out an
index-width/bias/remap error specific to low counts. **No second, per-version codebook library exists**:
an exhaustive `.data`-section pointer-run scan and a whole-file byte search both find exactly one
599-slot table in the main exe; the only other shipped binary in this project's working set,
`SaintsRowTheThird_DX11.exe` (a real DX9/DX11 twin build, linked 4 minutes apart from the main exe on
the same day), carries the same table, 99.99% byte-identical (the only difference is in slot 598's own
already-flagged alignment-slack bytes). A real `BKHD` bank-header version field does vary (48/52/53
across real banks) but cannot be the explanation, since the library lives in the executable, not the
bank, and only one exe-resident copy exists. **[CONFIRMED — empirical, independent re-derivation from
real bytes, 7,983-record sample across 6 archives.]** **Net: the codebook list and library are not the
cause of the 488/493 failures — the real cause lies downstream of the codebook section** (most plausibly
in floor/residue/mapping/mode reconstruction, still only HIGH-confidence per the note above, not
independently re-verified field-by-field the way codebooks now are; or in header-assembly on Team B's own
side). This is directly consistent with, not contradicted by, a separate, more precise bisection Team B
ran in parallel pointing even further downstream, to the real audio-packet bitstream layout rather than
the setup-header's own field grammar — see §11.11.4a below, which confirms that bisection directly from
this exe's own decoder. A handful
(12 of 7,986 attempted, 0.15%) of unrelated mis-parses in specific weapon/vehicle streaming banks were
found, flagged, and excluded — they match the shape of this section's own already-documented rare
`selector==1` offset-formula fallback (not independently re-confirmed this pass) and do not touch the
32/34/35/42 clusters. Full detail, the 4 real index-list profiles, and the complete methodology:
`scratchpad/vorbis_codebook_library_488_493_2026-10-09.md`.

**Slot 598 itself, named above only as "already-flagged alignment-slack bytes," is now fully
resolved (2026-10-09, bounded low-priority check): it is not a real, intentionally-authored
codebook.** Its pointer-table entry (`0x134fd30` = `0x1290290`) is real and distinct, not NULL and
not the already-known indices-600+ garbage, but the 10 bytes it points at (`0x1290290`-`0x1290299`,
raw `CD CC CC 3D 9C 11 2C 01 40 A6`, confirmed byte-identical from both the existing blob and an
independent fresh Ghidra read) decode under the library's own standard grammar to `dim=13` — the
sole dim=13 in the whole 599-slot library, every other slot being 1/2/4/8 — because the leading 4
bytes are simply the IEEE-754 encoding of the float literal `0.1f` (`0x3DCCCCCD`), not an authored
dimension value. Continuing the decode produces 9 ordered codeword-length runs covering exactly the
declared 3276 entries (lengths 26-34 bits) but a Kraft sum of only `405319/8589934592 ≈ 4.72e-5` —
four orders of magnitude below complete, versus the library's other 6 ordered codebooks (slots 16,
160, 161, 183, 200, 317), which are all exactly Kraft-complete. Three of its runs (8+6+2 = 16
entries) use codeword lengths of 32-34 bits, exceeding Vorbis I's own hard 32-bit legal maximum —
out of range for the format entirely, not merely incomplete. No code-side special-casing of index
598 was found (pointer-table read only, not an exhaustive bounds-check search). Net: slot 598 is
pointer-table spillover into adjacent unrelated `.rdata` constant data (most likely whatever
followed the last real codebook, slot 597, with no 599th real codebook to point to instead), not a
bug in the published decode grammar and not a deliberate reserved/placeholder entry; it is
consistent with, not contradicted by, the DX9/DX11 twin-build comparison above, since genuinely
authored codebook bodies match byte-for-byte across both builds the way all 597 real ones do, and
only this slot's tail bytes differ. **[CONFIRMED — empirical/disassembly, every byte-level claim;
HIGH CONFIDENCE — inferred, the "spillover not authored content" root-cause framing, since the
original codebook-compiler tool was not traced.]** Full detail and methodology:
`scratchpad/codebook_slot598_check_2026-10-09.md`.

#### 11.11.4a Audio-packet bit layout: no packet-type bit, a hardcoded 1-bit mode selector, derived
window flags (2026-10-09)

Direct follow-up to a further Team B bisection: their own oracle check (vgmstream, used as a black box)
shows that even this project's "successful" decodes are pure noise (correlation below 0.02, durations far
too short) — traced to every record's first real AUDIO packet (not the setup packet, which decodes
correctly) failing standard `vorbis_synthesis` with `OV_ENOTAUDIO`. Team B's theory, from public
knowledge of how Wwise's Vorbis integration is known to modify the standard bitstream: audio packets omit
the packet-type bit and, for long blocks, the previous/next-window-flag bits, reconstructing the window
shape some other way instead of reading it fresh. This was checked directly against this exe's own
per-packet dispatcher, `0x00fa4810` (115 instructions, 3 calls — already named in §11.9's own summary
table, previously only characterized at summary level as "reads 1 bit"), opened to the instruction this
pass, along with its three callees.

**Labels used below, per the task's own instruction: CONFIRMED (from this exe's own disassembly)** for
anything read directly this pass, versus **agrees with the public Vorbis I format** wherever the exe's
confirmed behavior is only being compared against the publicly-documented standard shape (never the
source of a claim).

- **No packet-type bit exists anywhere in this path. [CONFIRMED.]** The packet-framing function
  (`0x00fa4950`) hands back a raw `(dataPointer, byteLength, endFlag)` triple without reading a single bit
  of the payload; `0x00fa4810` then resets its own bit-reader state to point at that payload's first byte
  and its very next instruction is its *only* bit-read call (`0x00fa6340`, requesting exactly 1 bit,
  `0x00fa482e`/`0x00fa4834`) — used immediately, with no intervening check, as a mode-table index. There
  is no earlier bit-read, discarded bit, or "is this an audio packet" branch anywhere in the chain.
  **Agrees with the public Vorbis I format** only in that the public spec's own grammar reads a 1-bit
  packet-type flag (required 0) immediately before the mode-number field — this exe has no equivalent
  read at all; a stock decoder's packet-type check actually consumes what this exe treats as the real
  mode-selector bit.
- **The "mode/window-type flag" is exactly 1 literal, hardcoded bit — not a computed `ilog(numModes-1)`
  field. [CONFIRMED.]** The bit count pushed before the only bit-read call is the plain immediate `1`
  (`push 1` at `0x00fa482e`), not a value loaded from any mode-count field. That raw 0/1 value directly
  indexes (`bitValue*2`, `bitValue*2+1`) a 2-byte-stride mode-record table inside the rebuilt setup
  object — there is no separate mode-number-to-blocksize indirection; this dispatcher structurally can
  never address more than 2 live mode-table entries, regardless of how many modes the stream's own setup
  packet (§11.11.3a) might declare. **Agrees with the public Vorbis I format** only for the common
  real-world case of exactly 2 declared modes, where `ilog(2-1)` also comes out to 1 bit — the bit
  *width* alone would not distinguish this exe from a stock decoder handling a 2-mode stream, but the
  *mechanism* (a hardcoded literal, not a computed field) is confirmed non-standard.
- **For long blocks, the previous/next-window-flag values are derived from running decode-state across
  calls, never read as bits. [CONFIRMED.]** After the single mode-bit read, zero further bit-reader calls
  occur anywhere in the rest of the 115-instruction function. Every subsequent instruction is pure integer
  arithmetic over: the just-read mode's block-flag value (decode-state `+0x60`), the *previous* call's
  retained block-flag value (`+0x5c`, stashed before being overwritten — "yesterday's mode, carried
  forward across the call boundary"), two per-mode block-size values pulled from the setup object's own
  inline array, and two running decode-state counters (`+0x58`/`+0x64`/`+0x66`, with a `-1` sentinel at
  `+0x54` meaning "no previous packet decoded yet"). This computes exactly the quantities a stock decoder
  would get by *reading* the window-flag bits, but every input is either the current mode bit (already
  used for mode selection) or state already in memory from the previous call. **This directly confirms
  Team B's theory for this question, from this exe's own disassembly, not by analogy.**
- **Is this one mechanism sufficient to explain `OV_ENOTAUDIO` on every record? Reasoned inference, not
  corpus-verified this pass.** A stock decoder's first action on any packet is to read 1 bit and require
  it to be `0`; whenever this exe's real mode-selector bit happens to be `1`, a stock decoder fails
  immediately with `OV_ENOTAUDIO` exactly as reported — and whenever it's `0` instead, the stock decoder
  instead misreads its own general-case mode field one bit short, corrupting every later field (consistent
  with the separately-reported pure-noise/too-short-duration symptom even on packets that don't trip the
  specific error). **This is mechanistically sufficient to explain both reported symptoms without a second
  root cause** — the one open empirical question (whether real files' first packets deterministically
  carry mode-bit value `1`, which would make Team B's "every record" framing exact rather than
  probabilistic) needs real packet bytes from an actual extracted file, not a code read, and was not
  checked this pass.

**Resolves prior OPEN item 48** (§11.11.3's own flagged need to confirm the per-stream setup-pointer
consumer): the consumer-side shape — a 2-byte mode record indexing a 4-byte-stride block-size array and a
20-byte-stride mapping table off the same setup-object base, itself reached via decode-state `+0x20` — is
now CONFIRMED exactly, instruction-for-instruction (upgraded from HIGH). The populator, `0x00fa4600`,
remains undisassembled — still open. Full instruction listing, the decode-state field map, and every
address: `scratchpad/wwise_audio_packet_bit_layout_2026-10-09.md`.

#### 11.11.5 Seek-table and loop-point metadata (CONFIRMED)

**Cue points/markers:** the chunk walker's `"cue "` handler reads the standard WAV cue-chunk's
`dwCuePoints` count and that many 24-byte records (`dwName`, `dwPosition`) into a caller-allocated output
array; the `"labl"` handler, gated on `"cue "` already seen, reads a standard WAV label chunk and attaches
its text to the matching cue entry by name. (New finding: the walker's `"LIST"` handler advances only 4
bytes past the list-type tag rather than skipping the whole chunk by size, so it falls straight into
LIST's own sub-chunks — where `"labl"` normally lives, inside a standard `"adtl"` associated-data list —
without needing any LIST-aware recursion.) All standard, public WAV chunk convention, zero
Wwise-proprietary involvement.

**Loop points:** the `"smpl"` handler reads the standard WAV `cNumSampleLoops` count and the first
`SampleLoop` record's `start`/`end` fields when present. Both Bank and File then run byte-identical
fallback logic: if no `"smpl"` loop was found (`start==0 && end==0`), `loopEnd = vorbChunk.firstDword − 1`
— the dword §11.11.2 noted was excluded from the main 38-byte copy and read back separately (HIGH gloss:
"total sample count").

**The seek-table skip, `this+0x48`/`this+0x4c` — CONFIRMED and closed, 2026-10-08 (was: flagged not
confirmed).** File's header-parse step (`0x00fa3980`, vtable slot 17) computes two output fields via a
3-term sum and consumes them through an actual Seek-shaped virtual call, firing *before* any
packet-length-prefix read — i.e. the file cursor jumps past the seek table (§11.11.1's own table/row)
before the bare-16-bit-prefix primitive (§11.11.1) is ever invoked:

```
selector = <16-bit value read off a parent-chain walk — see below, NOT File's own incoming argument>
if (selector == 1):                         // rare fallback path
    this+0x48 = this+0xe0 + dataChunkStart
    this+0x4c = dataChunkLength + dataChunkStart        (i.e. end of the "data" sub-chunk)
else (selector == 0 or > 1, the common/default path — every real standalone WAV hits this):
    this+0x48 = vorb[4:8]  + vorb[16:20] + dataChunkStart
    this+0x4c = vorb[8:12] + vorb[16:20] + dataChunkStart
```

`dataChunkStart` (`this+0x34`) is **not** vorb-derived — it is the chunk walker's (`0x00f8fcb0`) own
output argument, the absolute byte offset of the `"data"` sub-chunk's payload (right after its own
`tag+size` dwords) relative to the RIFF/WAV blob start. The selector itself is **not** read from File's
own incoming stack argument (a prior guess, now corrected) — it is read off a parent-chain walk two
levels removed from `this` (`*(this+8)` → `+0x60` → walk `+0x14` until flag bit `+0x39 & 2` is set →
read the word at `+0x5e`), structurally resembling a "resolve to the owning container"/I-O-backend
property rather than anything Vorbis- or vorb-chunk-specific (HIGH, not independently named).

Immediately after computing them, `0x00fa3980` calls `0x00f901f0(selector, &localOut)` — which copies
`this+0x48`→`localOut+4` and `this+0x4c`→`localOut+8` (or zeroes both when `selector==1`) — then passes
`&localOut` to a virtual call on the File's own stream-descriptor sub-object, vtable slot `+0x10`: **the
actual seek.** `CAkSrcBankVorbis`'s own finalize-setup (`0x00fa3290`) has no equivalent of any of this —
it reads its packet cursor directly off `this+0xe4` with zero offset computation, confirming the whole
mechanism is `CAkSrcFileVorbis`-only; §11.11.3's "reads one more 16-bit-prefixed packet immediately
after the chunk walk" framing is still exactly correct for Bank and for File's own rare `selector==1`
fallback — it is only wrong for File's default/common path, which is what every real standalone WAV
file hits. **[CONFIRMED — raw-instruction-level read of `0x00fa3980`/`0x00f901f0`/`0x00f4f7d0`/
`0x00f5b3b0`, not decompiler output.]**

**Correction, 2026-10-08 (semantic mislabel, not a bad formula — Team B independently falsified the
formula's implied reading on all 493 real records, which led to this fix): `this+0x48` is NOT the
setup-packet offset.** It is the **first-audio-packet absolute offset** — the arithmetic above was
never wrong, but what it computes was mislabeled. **The real setup-packet offset is
`dataChunkStart + vorb[16:20]` alone — no `vorb[4:8]` term.** Confirmed by reading the real
setup-packet reader directly (`0x00fa4250`, File's own finalize-setup state machine — the previously-
missing 2nd of exactly two static callers of `0x00fa4c30`, closing
`wwise_vorbis_wrapper_open_items_2026-10-07.md` item 1 too): it gates the real setup-packet read by
buffering exactly `vorb[16:20]` bytes from the sequential read stream into `this+0xfc` before calling
`0x00fa3bd0` to read the next 2-byte-prefixed packet — **it never references `this+0x48` at all.**
The real seek-table walker, `0x00fa36b0` (reached via `SeekStream` slot 19, `0x00fa3b40` — code whose
mere existence had only been inferred before, now read directly), confirms `entryCount =
vorb[16:20]>>2` by direct loop bound (not just statistically) and returns `vorb[20:24]` directly for
"seek to sample 0" — i.e. **`vorb[20:24]` IS the first-audio-packet offset.** A fresh population
check (981 real `"vorb"` records) found the exact identity `vorb[20:24] = vorb[4:8] + vorb[16:20]`
(981/981, zero exceptions) — combined with Team B's own exact `vorb[8:12]+vorb[16:20]==dataChunkSize`
finding, this proves `this+0x48` (the formula above) and `vorb[20:24]+dataChunkStart` (the real
first-audio-packet offset) are the same value: the formula was always computing the right NUMBER,
under the wrong NAME. `vorb[4:8]` resolves as the stripped setup packet's own small wire size (2-byte
prefix + payload), tied to the shared setup-resource cache's hash/profile grouping — explaining why
it only ever takes 4 distinct values across the whole population. **Per-packet header size,
re-confirmed unchanged**: still exactly a bare 2-byte length prefix (`0x00fa3bd0`, File's own real
incremental packet reader, functionally equivalent to Bank's simpler cursor read) — Team B's
"19-24 byte length" reading was never a malformed header; it was a real, small, plausible value
because the READ POSITION itself was wrong (landing on/past the first audio packet, not the setup
packet). **What `this+0x48`/`this+0x4c` are actually consumed for, traced past the Seek call**: no
other File-class function (`GetBuffer`, the real setup-packet reader, the real `SeekStream` handler)
reads them again — they are a one-shot argument to the single vtable `+0x10` call, now correctly
understood as `(first-audio-packet offset, end-of-data-chunk)`, a bound around the audio payload
deliberately excluding both the seek table and the setup packet (which use the separate mechanism
above). New OPEN items: the vtable `+0x10` call's own internal target function (`this+0x24`'s class
is zeroed by the shared base constructor `0x00f8ff40` and populated outside the Vorbis classes, not
traced); exactly where the sequential read window's first byte-position is seeded (assumed
`dataChunkStart`, consistent with everything else, not independently witnessed at its own seed
instruction). Full detail (original report + this correction, "PART 2 — Correction" section):
`scratchpad/wwise_vorbis_seektable_setup_offset_2026-10-08.md`.

#### 11.11.6 Summary

| Question | Answer | Confidence |
|---|---|---|
| Packet framing | fixed 16-bit length prefix, inline, no Ogg pages; reused for the setup packet; for File's default path, preceded by an explicit seek past a per-file seek/index table (§11.11.2/§11.11.5) | CONFIRMED |
| Granule/sample-position tracking | no stream field; a running per-instance counter the engine advances itself | CONFIRMED |
| Identification header rebuild | block-size exponents (**vorb bytes 40/41** — corrected 2026-10-08 from a stale "bytes 4/5," which this document's own §11.11.2 retracted in favor of `+0xA0`/`+0xA1`) validated against exact public Vorbis legal bounds | CONFIRMED |
| Setup header rebuild | one extra 16-bit-prefixed "setup packet," parsed into an in-memory mode+mapping table structure | CONFIRMED mechanism / HIGH field detail |
| Codebook storage | no static table in the exe; built from each file's own setup packet | CONFIRMED (exhaustive negative + positive mechanism) |
| Codebook selection | hash-keyed, refcounted runtime cache | CONFIRMED (Bank); not independently confirmed for File |
| Loop points | primary: standard WAV `"smpl"` chunk; fallback: vorb's own first dword − 1 | CONFIRMED |
| Seek/marker table (WAV-level cue/label) | standard WAV `"cue "` + `"labl"` chunk pair | CONFIRMED |
| Seek table (Vorbis packet-index, File-only) | per-file table before the packet stream, byte-length = vorb[16:20], entryCount = vorb[16:20]/4; skipped by an explicit seek (`this+0x48`/`this+0x4c`), never parsed by any loop | CONFIRMED mechanism; entry-count formula HIGH |

---

## 11a. 3D positioning, listener, and per-tick attenuation producers — the voice-API/object-sound-core unit (2026-10-08)

**Scope.** Direct follow-up, dispatched by `purrsian-62`, closing the "voice API is a later unit" gap
§12 item 1 below flags: what writes each voice field from the game side, how a game entity maps to a
Wwise "game object id," and what's traceable vs. black-box for attenuation. Method: the persistent
Ghidra server (`func`/`xref` modes, decompiler used only as a reading aid — cross-checked against the
raw instruction listing throughout, since the decompiler's own parameter typing was wrong in at least
one case noted below). No leaked Volition/Audiokinetic/Havok/RAD material read; public Wwise SDK
naming used for orientation only.

### 11a.1 Emitter position producer — `0x0045da50`, the voice-API "create" call

Writes voice `+0x28`/`+0x2c`/`+0x30` (position x/y/z) from a caller-supplied 3-float struct (or skips
the whole block if that struct is null, leaving `+0x9f`=0, a non-positional "2D" voice) — **only 3
floats; orientation `+0x34`/`+0x38`/`+0x3c` is never written by this path.** Also writes `+0x9f`=1 (one
position present), `+0x98` bit 0 (use this voice's own aux-send 0, not the global one), and `+0xa6`
(per-voice RTPC `0x85c3e987` enable, §11a.3) straight from its own 3rd argument — **every voice checked
this pass passes `1` here, including the default non-positional 2D voice** (consistent with §11a.3's
own distance report of 0 for that case). Forces an immediate positional-update pass via
`CALL 0x0045cd80` at creation time. **No dedicated "update an existing voice's position after
creation" setter was found** among the ten functions `review/scoping-audio.md` names as the voice-API
family — either one exists elsewhere in the still-unopened remainder of the Voice core address band,
or continuously-moving emitters are served by create/release churn through the pool in §11a.4 instead
(a fresh voice at the entity's current position, often enough that staleness isn't perceptible); left
genuinely OPEN, not assumed either way.

Two label corrections to the voice-API family, found while reading all ten functions directly:
`0x0045f5b0` is the voice-API's `SetSwitch` wrapper (writes switch-list node `+0x7c`/`+0x80`/`+0x84`,
sets voice flag `0x20`) — not "bind/param" as a prior scoping note's placeholder name implied; and
`0x0045f470` is the per-voice RTPC setter, which resolves a play-record cookie via
`0x004613a0`→`0x0045c870` then calls `0x0045c230` with the literal float `0.009` (`0x012a2ff4`) as its
4th argument — **this closes item 4 below's "fourth argument" mystery**: it is passed here, HIGH an
interpolation/transition time, though `0x0045c230`'s own body is still unopened.

Real voice category-name strings, read directly from `.rdata`: **`"LUA 2D"`** (`0x0117e8c8`) and
**`"LUA 3D"`** (`0x0117e8d0`) — these pass through unmodified to the registration-name helper
(`0x0045c830`, feeding `RegisterGameObj`'s debug name). This **corrects** a prior scoping note's own
HYPOTHESIS guess of `"UI"`/`"Emitter"` for the two voice categories.

### 11a.2 Listener position producer/consumer — `0x00468130`/`0x00468050`

**`0x00468130(newPos, viewStruct)`** is the one function anywhere in the binary that writes the
listener position globals `0x031728e4`/`e8`/`ec` and arms the "send to Wwise" counter `0x03172908`
(§1.3's own already-documented `SetListenerPosition` row). Gated on an "audio system ready" byte
(`0x03171a92`). Does a 0.01-epsilon change-detection against the stored position **per axis**; the
"arm counter" is incremented only if any axis moved more than that epsilon, but the position itself is
written unconditionally. If a second argument (a view/orientation struct) is supplied, it reads 6 more
floats from it (two 3-vectors — front, top) into `0x031728f0..` and `0x031728fc..`, and arms the counter
**unconditionally** whenever that struct is given (independent of the position epsilon test). Two
previously-undocumented one-byte "valid" flags are also written: `0x0317290c` (position valid),
`0x0317290d` (orientation valid) — read back by the sibling query `0x00468050`
(`GetListenerPosition`-shaped, 8 real gameplay callers in the `0x00553000-0x005644ff` gameplay-audio-
module band; defaults front to `(0,1,0)` if orientation was never set). **This is the actual mechanism
behind the already-documented "`SetListenerPosition` sent only while the counter is positive, armed
for N frames rather than every frame."**

**Caller: exactly one site, `0x005546c0`.** Sits inside the gameplay-audio-module's own address band —
HIGH a small per-frame "sync the audio listener to the current camera" helper, consistent with audio
owning its own copy of listener state rather than reaching into the camera/view system directly. The
camera/view-matrix source feeding into `0x005546c0` was not traced this pass (OPEN).

### 11a.3 The two per-tick game-side attenuation computations (both called from `0x0045cd80`, immediately before `SetPosition`/obstruction are sent — §1.1's own row for `0x0045cd80` previously flagged these "game-side, not read")

**`0x004685c0(voice, outPos)` — closest-of-N-position-samples distance to the listener, fed to Wwise as
a custom RTPC.** For a voice with multiple position samples (the already-documented
`SetMultiplePositions` path, §1.1), walks every sample node and keeps the **minimum** true 3D distance
to the listener (via a shared double-precision distance helper, `0x00da1290`, against the same
listener-position global §11a.2 writes), storing the winning distance at voice `+0xc` and the winning
node's position into the caller's output. For a voice with no position (`+0x9f`=0, the 2D case), it
early-outs and writes a literal `0` instead. That `+0xc` value is then — per §1.1's own already-
documented row — sent to Wwise as per-voice RTPC id **`0x85c3e987`**, but only when voice `+0xa6` is
set, which `0x0045da50` sets from its own 3rd argument (§11a.1) — **passed as `1` at every creation
site seen this pass, including the 2D voice**, so 2D/non-positional sounds correctly report "0
distance" to this RTPC (full volume/no falloff via whatever curve it drives) while real emitters report
a true closest-sample distance, not merely a single-point distance. **CONFIRMED mechanism; the RTPC's
downstream purpose in authored bank data (which curve `0x85c3e987` drives) was not opened this pass**
— an FNV-1 brute-force against ~40 plausible English words found no match, it stays unnamed.

**`0x004682d0(chosenPosition)` — zone/AABB-gated environment (reverb) aux-send override.** Writes the
three already-documented aux-bus send pairs (voice `+0x60`/`+0x64`, `+0x68`/`+0x6c`, `+0x70`/`+0x74`,
§2.2) — previously unattributed to a writer. Slots 1/2 are always set from global defaults
(`0x03172958..`/`0x03172960..`); slot 0 defaults to "none" (`0x60`=0) **unless** a configured override
environment id is present (`0x03172950` ≠ 0) **and** the voice has a position, in which case it tests
the chosen position against a configured AABB (`0x00dbf450`, SSE `CMPLEPS` on all 3 axes) and, if
inside, overrides slot 0 with the configured environment id/level. **This is literally a game-side
per-zone reverb override layered on top of Wwise's own aux-bus system** — not something Wwise computes
from position alone. **OPEN, honestly:** the writer of the configuration globals this reads
(`0x03172938`..`0x03172964`/`0x03172950`) was not traced — whether this represents one single fixed
region for the whole game or a separate, unlocated zone-manager rewrites it per area is genuinely
unresolved; the override mechanism itself is CONFIRMED, "general multi-zone system" is HIGH at best.

### 11a.4 Game objects → Wwise "game object id" — the real mapping, and the object-sound core

**The Wwise "game object id" is literally the voice record's own memory address** (already established
at §2.2; re-confirmed from the producer side) — not a stable per-entity integer. Because voices are
created and destroyed dynamically (below), **this id is NOT stable across a sound's lifetime** the way
the public Wwise model's "register once, reuse the id" usually implies — the game mints a brand-new
address every time it wants a new sound.

**The real, persistent identity layer sits one level above Wwise, in a 32-slot "object-sound" pool**
(`0x026ea498`, 24-byte stride, `0x00a2a850` manages it): scans for a free slot (`+0x14`==0), else steals
the oldest slot whose occupant already finished playing (`0x00a2a560`); on success, mints a fresh
**global monotonic serial, `0x026ea798`** (wrap-guarded to skip 0), written into the slot's own `+0x08`.
**This directly resolves this file's own flagged-OPEN "id-space question (engine serial `0x026ea798`
vs. Wwise playing id)": it's neither — a third, independent bookkeeping serial for this 32-slot pool
only, never passed to Wwise.** The serial is minted fresh on every request that asks for one, not
cached persistently on the calling entity.

**Entity resolution and voice creation — `0x00a2ad00`/`0x00a2a950`.** `0x00a2ad00` builds a request
descriptor copying a 64-bit **engine object handle** (not a pointer) from its caller, gets a pool slot
+ serial (above), then calls `0x00a2a950`, which: (1) resolves the 64-bit handle to a real object via
the engine's own **generic** object-handle resolver `0x00458230` against the shared class-capability
table `0x02cc9900` (indexed by a per-class byte on the resolved object's own `+0x34`, tried up to three
times with three different capability-bit tests) — `0x00458230` has 26+ unrelated callers elsewhere in
the binary, **confirming audio has no entity-id scheme of its own; it borrows the engine's general
one**; (2) if no handle was given at all, falls back to a position-only resolver, `0x004e5830` (same
`0x02cc9900` machinery, a different capability bit); (3) reads the resolved object's position directly
off its own struct at a fixed `+0x40`/`+0x44`/`+0x48` — **there is no id→position indirection table
anywhere in this chain**, the game always has a real pointer in hand; (4) creates a brand-new voice at
that position via `0x0045da50("LUA 3D", 1)` or, for the non-positional fallback, `0x0045da50("LUA 2D",
1)` — **the literal mechanism of "game object → Wwise game-object-id": resolve the entity (or fall back
to position-only) once, snapshot its current position, mint a disposable voice whose own address
becomes the Wwise id. There is no stable 1:1 table and no reuse — a fresh id is minted per request.**

**Honest scope boundary.** This fully traces ONE major producer — the one-shot "object-sound"/ambient
pool serving `"LUA 2D"`/`"LUA 3D"` sounds (impacts, footsteps, UI pings, one-off world sounds).
**Continuously-tracked looping emitters** (vehicle engines, wind, player foley loops, radio) are, per
this project's own layer breakdown, handled by a separate "gameplay audio module" region
(`0x00553000-0x00564fff`) this pass did not open — whether they reuse this exact create-once-and-forget
model or have their own persistent position-update path is OPEN, not assumed either way.

### 11a.5 Attenuation — summary verdict

| Layer | Traceable? | Evidence |
|---|---|---|
| Distance/cone attenuation curve *evaluation* (volume/LPF/HPF vs. distance, cone rolloff) | **No — entirely inside Wwise's own statically-linked code**, a deliberate, re-confirmed project-level scoping call, not re-opened | `review/scoping-audio.md` §1 |
| Closest-of-N-samples distance, fed to Wwise as a custom RTPC | **Yes — CONFIRMED mechanism** (bank-data purpose not opened) | §11a.3, `0x004685c0` |
| Zone/AABB-gated environment (reverb) aux-send override | **Yes — CONFIRMED mechanism** (zone-config writer not traced) | §11a.3, `0x004682d0` |
| Obstruction target (`+0xa2`) computation | **OPEN — searched for across all 10 voice-API + all 7 object-sound-core functions (~700 instructions), genuinely not found** | — |
| Occlusion computation | **No — confirmed negative, always sent as literal 0.0** (already in §1.1/§2.2) | — |

### 11a.6 Clean-room check

Plain hex addresses throughout; the persistent Ghidra server's decompiler output was used only as a
reading aid and was explicitly bypassed in favor of the raw instruction listing wherever its own
parameter typing was unreliable (one case noted inline, §11a.3). Public Wwise SDK naming
(`SetPosition`, `SetListenerPosition`, the front/top/position `AkSoundPosition` ordering) used for
orientation only, never as the source of an address or struct offset. One FNV-1 brute-force (this
project's own already-established hash convention) run offline against a short candidate-word list to
try naming two RTPC ids — flagged HIGH not CONFIRMED (a single-word match cannot rule out collision).
No leaked Volition/Audiokinetic/Havok/RAD material read. Self-scan with this project's strict
cleanroom pattern: 0 hits. Full detail, every address, every raw listing:
`scratchpad/wwise_3d_positioning_attenuation_2026-10-08.md`.

### 11a.7 SFX call sites and the SFX/dialogue fork — CONFIRMED same mechanism (2026-10-08)

**There is no separate "SFX positioning path."** This unit's own object-sound-core/voice-API
mechanism (§11a.1-§11a.4) **is** the SFX mechanism — confirmed by finding three independent, real,
unrelated gameplay call sites that all converge on the same object-sound-core entry point,
`0x00a2acb0` (18 real call sites across 13 functions reach it; `0x00a2ad00`, §11a.4, is the sibling
entry that does full engine-handle resolution first before calling the same machinery):

1. **Mission-script ambient/state-machine cues** — a cluster of functions (`0x005e47a0`/
   `0x005e5bb0`/`0x005e6600`/`0x005e68b0`/`0x005e7bb0`) each hash a literal event-name string through
   the already-specified name-hash path (`0x00462960`→`0x0046fd00`→Wwise `GetIDFromString`) and call
   `0x00a2acb0`. Real event names read directly from `.rdata`: `"m21_crowd_cheer_mid"`,
   `"m16_boss_invert_stop"`/`"m16_invert_stop"`/`"m16_shrink_stop"`/`"m16_shrink"`, several more
   `"m16_*"` boss-fight state cues.
2. **A player/boss special-ability impact effect** — `0x005e7bb0` plays `"m16_shockwave_impact"`,
   next to the literals `"VFX_Avatar_SwordShock"`/`"Local_Boss_Start_Navpoint"` — a direct real
   example of an impact/explosion-shaped SFX event using the same mechanism.
3. **A vehicle minigame event** — `0x00664bc0` plays `"Tank_Mayhem_Repair"`, with the request
   struct's position fields copied directly from the target vehicle's own `+0x8`/`+0xc` fields
   (positional on that vehicle).

**The SFX-vs-dialogue fork is one flag bit at the final `PostEvent` call, not a structurally
different pipeline.** Both sites are reached from the same per-voice state machine (`0x0045ce70`):
ordinary SFX → `0x0047074e` inside `0x00470720` (flags `0x1000d`, no external sources, chosen
whenever voice flag `0x01000000` "voice line" is clear); dialogue/voice line → `0x004705c4` inside
`0x00470500` (flags `9`, one external source built on the stack — a Vorbis codec id plus a file id
from `0x00468ae0`, the mechanism that plays per-line streamed `.lm_pc` files). Everything upstream
of this fork — the object-sound core, the voice API, the per-tick positional/attenuation update at
`0x0045cd80` — is shared, unduplicated code; SFX and dialogue diverge only here.

**Mixer/bus structure is almost entirely data-side.** A CONFIRMED negative: no bus id of any kind
crosses the game→Wwise call boundary anywhere in the full ~19-operation adapter contract (`PostEvent`,
`SetPosition`, `SetRTPCValue`, `SetSwitch`, `SetState`, bank load/unload, etc.) — the only bus-shaped
values that cross at all are the three aux-send (reverb) slots in `SetGameObjectEnvironmentsValues`
(§11a.3's `0x004682d0`), driven by a game-side environment/zone id, not a literal bus choice. Bus
assignment is decided entirely by data authored in Wwise and baked into the bank. On the data side, a
real Bus-shaped HIRC population exists: kind 8 (770 items, concentrated in just 2 banks — both
copies of the same global "init" bank — shape-consistent with one shared master-Bus tree) and kind 19
(463 items across 63 banks — per-content, independently corroborated by this section's own aux-send
override mechanism as plausibly Auxiliary Bus). **HIGH, not CONFIRMED** (this binary carries no
string/RTTI name for any HIRC kind value, same standing caveat as the kinds-10-13 Music finding,
§8.2.1) — genuinely OPEN: how many distinct Bus objects exist inside kind 8's own population, and
whether any are specifically named/structured as SFX/Music/Dialogue/Ambience sub-buses (next step:
open the kind-8 construction helper, `0x00f58530`, named but not opened by any pass).

**Concurrent voice/SFX counts — three different numbers at three layers, no single clean answer:**
the object-sound-core's own 32-slot request pool (`0x026ea498`/`0x00a2a850`, CONFIRMED, specific to
this one-shot "play SFX positioned on an object" entry point); the game-wide 1,050-entry play-record
pool (`0x031d4cd0`/`0x031d4ce8`, §2.1, CONFIRMED but NOT SFX-exclusive — shared by every posted
Wwise action of any type); and Wwise's own internal voice-instantiation pool (`0x00f53c40`, §7.3.3,
already landed) with a genuine hard capacity + lowest-priority eviction — generic, applies to SFX
without further tracing, but its literal numeric cap was never read by any pass (OPEN). The
practical firefight-scale ceiling is most likely this third, still-unmeasured number, not 32 or
1,050.

**Distance attenuation and occlusion for SFX are identical to the already-documented general case**
— a direct consequence of the convergence above, not a new mechanism: the closest-of-N-samples RTPC
(§11a.3's `0x004685c0`) and the zone/AABB aux-send override (`0x004682d0`) both apply unchanged to a
one-shot SFX voice (a single position sample degenerates to the simple case); occlusion remains
always literal `0.0` — no wall-blocking/line-of-sight occlusion system exists anywhere in this path
for SFX any more than for the general case (§11a.5).

**OPEN, honestly flagged, not guessed:** no literal weapon-fire ("gunshot") call site was found this
pass — three other distinct subsystems confirmed the mechanism instead; the most efficient next step
is piggybacking on whoever resolves the still-separately-OPEN hitscan-fire-dispatch item
(`spec-weapons-runtime.md` §2.2b) rather than re-deriving independently. Also OPEN: the 32-slot
pool's behavior when genuinely full with nothing finished; whether continuously-tracked looping SFX
(vehicle engines, wind) reuse this exact one-shot model or have a separate persistent path (the one
new vehicle call site found this pass, `"Tank_Mayhem_Repair"`, is a one-shot cue, not a continuous
engine loop, so it doesn't settle this). Full detail, complete address index:
`scratchpad/audio_mixer_3d_sfx_positioning_2026-10-08.md`.

### 11a.8 Footstep audio — CONFIRMED event name/switches (data); real runtime posting mechanism RESOLVED, `0x00999020` posts `FOOTSTEP` with a runtime-selected `MATERIAL_TYPE` switch (2026-10-10, resolved 2026-10-11)

**Routed from Team B.** Question: the real Wwise HIRC event/id for player/NPC footsteps, whether it's
anim-event-driven or a movement-code cadence timer, and whether/how it varies by ground material.

**The event name is the literal string `FOOTSTEP`** — not a `.text` literal (a `str`-mode scan of the
whole `.exe` for `footstep`/`Footstep`/`FOOTSTEP`/`foot_plant`/`FootPlant` finds **zero** occurrences;
this project's name-hash path is `0x00462960`→`0x0046fd00`→Wwise `GetIDFromString`, as already
documented in §1.3/§11a.7, but `FOOTSTEP` is never passed to it from `.text`). It comes from **game
data**: `misc_tables.vpp_pc`'s `anim_files.xtbl`, specifically the `Sounds/Sound` child element this
project's own `spec-tables-animation.md` §3.2 already reverse-engineered the loader for (`0x004ae7a0`
→ `0x004ae1e0`, pool at `0x02e99688`, stride `0x1C`, each record's `Audio_Event` text resolved to a
Wwise id via `0x0046fd00` **directly** — not through the `0x00462960` forwarder, which is exactly why a
scan for `0x00462960` callers never surfaces this path). **CONFIRMED (data)** — independently re-verified
against the real decoded `anim_files.xtbl` (`tools/an_xtbl/anim_xtbl/anim_files.xtbl`, 5,968 `<Anim_file>`
rows): 144 rows carry at least one `Sound` whose `Audio_Event` is the literal text `FOOTSTEP` (matches
exactly). `MATERIAL_TYPE` has exactly 3 shipped values (`CONCRETE`, `METAL_SOLID`, `FLESH`, independently
re-grepped, matches exactly).

**The mechanism splits into two tiers, and this matters for where material variation lives:**

1. **Fixed-context clips (baked per-clip switch values) — CONFIRMED (data).** A `Sound` element carries
   `Frame`, `Audio_Switch_0`, `Audio_Switch_1` (each `"GROUP:VALUE"`, split on `:`, both halves resolved
   via `0x0046fd00`) and `Audio_Event`. All 144 `FOOTSTEP`-bearing rows in the base game are **vehicle
   entry/exit clips** (`apc_entrl_trvs.animx`, `auto_entrl_bigtrck_ridr.animx`, …, stepping on a vehicle's
   metal floor/running-board) and the **Brute NPC's own locomotion/kit** (`brute_hvy_wlk.animx`,
   `brute_mc_run.animx`, `brute_hs_run_a.animx`, …). Real shipped switch values (exhaustive for the base
   game, independently re-grepped):
   - `MATERIAL_TYPE` group: `CONCRETE`, `METAL_SOLID`, `FLESH` — only 3 values shipped, and `MATERIAL_TYPE`
     is used **only** on `FOOTSTEP`-event `Sound` rows (never on any other `Audio_Event`).
   - `SHOE_TYPE` group: `BAREFOOT`, `COMBAT_BOOTS`, `SNEAKER`.
   - `MOVEMENT_TYPE` group (paired with `MATERIAL_TYPE` on some rows instead of `SHOE_TYPE`): `WALK`,
     `LAND`, `LAND_HARD`.
   Each `FOOTSTEP` `Sound` row sets **both** a material switch and a shoe/movement switch, then posts the
   one event — i.e. **one event, two orthogonal Wwise switch groups**, with the actual per-combination
   media choice made inside the Wwise bank (data-side, same scoping boundary already established for all
   SFX in §11a.5/§11a.7 — not re-opened here). **This is the material→event/switch table Team B asked
   for**, to the extent the base game's authored data goes:

   | Audio_Event | Audio_Switch_0 or _1 (material) | paired switch | example clip |
   |---|---|---|---|
   | `FOOTSTEP` | `MATERIAL_TYPE:CONCRETE` | `SHOE_TYPE:{BAREFOOT,COMBAT_BOOTS,SNEAKER}` or `MOVEMENT_TYPE:{WALK,LAND,LAND_HARD}` | sidewalk/ground-context clips |
   | `FOOTSTEP` | `MATERIAL_TYPE:METAL_SOLID` | `SHOE_TYPE:COMBAT_BOOTS` or `MOVEMENT_TYPE:LAND` | vehicle floor entry/exit (`apc_*`, `auto_*entr*`) |
   | `FOOTSTEP` | `MATERIAL_TYPE:FLESH` | (shipped value, exact pairing not captured in this pass's sample) | — |

   These values are **author-baked per clip at content-creation time**, not computed from a runtime
   ground query — there is no code path in this chain that samples a collision material and selects which
   `MATERIAL_TYPE` string to use; the animator/sound-designer picked the right constant for the context
   the clip was built for (e.g. "this is a metal vehicle floor, always"). **CONFIRMED (data + loader
   disassembly already on record)**; the inference that it's author time, not runtime, is **HIGH
   CONFIDENCE** (no contrary mechanism found, see next point).

2. **Ordinary player/NPC locomotion clips — no embedded audio event at all; CONFIRMED (data), and this
   closes an open item in `spec-anim-runtime.md`.** The actual walk/run/sprint cycles driven by the
   on-foot movement state machine (`spec-player-runtime.md` §3.6, `0x005b2ca0`/`0x005ab930`) —
   `plym_*_wlk.animx`/`plym_*_sprint.animx` (player male), `plyf_*_wlk.animx` (player female),
   `ped_*_run.animx`/`gang_*_wlk*.animx` (NPCs) — have an **empty `<Sounds></Sounds>` element**. Instead
   every one of them carries the **generic** `<Trigger><Name>left-foot</Name><Frame>N</Frame></Trigger>`
   / `right-foot` markers (from the `anim_triggers.xtbl` registry, §9 of `spec-tables-animation.md`,
   hashed with the narrow rotate/XOR hash `0x00da7890`, not this group's usual CRC-32) at the actual
   foot-plant frame. Independently re-verified count over the whole base-game table: **1,170 `left-foot`
   and 1,149 `right-foot` trigger occurrences**, essentially universal across every locomotion clip in
   the game (player and NPC alike), vs. only 144 rows with a baked `FOOTSTEP` `Sound`. **This is almost
   certainly the real foot-plant anim event** `spec-anim-runtime.md`'s own `Left Foot Bool`/`Right Foot
   Bool` graph parameters are meant to be driven by — see that file's own update.

**Runtime dispatch — genuinely OPEN, confirmed-negative by two independent checks:**
- The `Sound` pool (`0x02e99688`) has **no reader anywhere in `.text` outside its own loader**
  (`0x004ae1e0`) — independently re-verified by xref census against the Ghidra query server:
  exactly 3 uses, all inside that one function. Nothing else in the binary names this address.
- The per-clip trigger-occurrence table the loader builds from each `Trigger`'s `Frame`
  (`0x004ca2d0`→`0x004cad90`, storing `(trigger-index, frame·0x12c0/0x1e)` pairs into a global, capped
  array at `0x030e2200`/`0x030e2204`) has the same result: independently re-verified, exactly 2 uses,
  both inside `0x004cad90` (the loader itself) — zero other references to either global.
- No caller of the hash/`GetIDFromString` path (`0x00462960`, 281 sites/137 functions, full census this
  pass) sits inside the on-foot movement band or inside the animation-controller band
  (`0x004b0000`–`0x004ca000`, already fully read by this project's U1 pass for other purposes) either.

**Caveat on that negative, stated plainly so it isn't overclaimed:** a direct-xref scan only catches
code that names a global's absolute address. A consumer that reaches either pool exclusively through a
per-record pointer (the `Anim_file` record's own `+0x3C` `Sounds`-array field, or the secondary-cache
record's `+0x18` trigger-list field that `0x004cad90` fills) would **not** show up in this scan at all,
because it would never mention `0x02e99688`/`0x030e2200` literally. So this is a confirmed negative for
"no code hard-codes these pool addresses outside the loader," **not** a proof that no runtime consumer
exists. Finding (or ruling out) an indirect, pointer-chasing consumer is the natural next step — would
need a scan for code that reads an `Anim_file`/secondary-cache record's own `+0x14`/`+0x18`/`+0x3C`
fields by the record's own pointer, across every function that holds one — large search space, not
attempted under this pass's budget.

**The pointer-chasing consumer, done 2026-10-11 (SPEC TEAM, agent `a3c067c7758abaea8`, independently
re-verified by direct xref/decompile before splicing): the consumer DOES exist and is real,
callable code — but it is not an audio consumer for the generic locomotion tier, closing this
caveat as a much more precise negative.** The `Anim_file` record's own `+0x3c` tier (the 144 baked
`Sound`-pool `FOOTSTEP` rows) checks out clean — its persistent runtime array (`0x02ef2178`,
stride `0x44`) is read by 31 real functions, individually inspected, none of which reads `+0x3c`
off the record pointer; confirms that tier genuinely has no runtime consumer at all, even through
the per-record pointer. The generic foot-plant-trigger tier is different: its own persistent
record (`0x030f0c68`, stride `0x24`, built by `0x004ca2d0`→`0x004cad90`) IS read at runtime, by a
real, generic `GetTriggerFired(animState, triggerHash, occurrenceIndex)` facility
(`0x004b27b0`/`0x004bdec0`/`0x004c7090`/`0x004b4320`) — re-verified by direct decompile. The
engine's trigger-name registration table (`0x0073d550`) resolves the literal strings
`"left-foot"`/`"left-foot2"`/`"right-foot"`/`"right-foot2"` to 4 cached hash globals
(`0x0153be40`/`0x0153be3c`/`0x0153be38`/`0x0153be34`); **re-verified by direct xref, every one of
the 4 real readers of these globals was found and opened** (`0x0050c130`/`0x00552070` — vehicle
entry/exit animation-state gating, matches the already-established vehicle-entry baked-clip
finding; `0x00999020` — weapon aim-stance foot-plant IK blending, plus a Brute-NPC-only
camera-shake call; `0x009a8d30` — melee-impact camera-shake).

**CORRECTION, 2026-10-11 (SPEC TEAM, agent `ad14b91d5da2a1555`, independently re-verified by
direct query before splicing): the "none of these 4 consumers reaches the Wwise event-post path"
claim above was WRONG for `0x00999020` specifically.** The prior pass checked that function's
camera-shake call (`0x0057bfd0`, confirmed to have no Wwise call inside it — true) but never read
`0x00999020`'s own tail, past where that earlier pass stopped. **`0x00999020` IS the real
footstep-posting mechanism for ordinary player/NPC locomotion**, found in its own later body:

1. **Gating (same trigger facility already documented above, re-confirmed):** bails at
   `0x00999724` if no `left-foot2`/`right-foot2` (or `left-foot`/`right-foot`) trigger fired this
   call — so a real foot-plant edge is still required, just not AS the direct Wwise callback;
   `0x00999020` itself is the real consumer, and §11a.8's own "which trigger consumer posts audio"
   question was mis-answered by checking the wrong downstream call.
2. **Distance cull, closes a separate open item:** `audio_constants.xtbl`'s `OnFootSettings/
   FootstepRange` element (`tools/ao_xtbl/audio_constants.xtbl` lines 189-201, shipped value
   `5.0`) is squared and stored at global `0x013bb470` by `0x00553580`/`0x005536a0` —
   **re-verified by direct xref: exactly 2 uses project-wide, the one write (`0x005536cb`) and
   one read, inside `0x00999020` itself (`0x00999109`)** — confirming this global exists
   specifically to gate this function's own footstep logic, nothing else. Beyond 5m, the
   footstep is skipped unless `0x0052a230()` (role not identified this pass, plausibly an
   "is local player" or audibility check) overrides it.
3. **Once gated through, unconditionally (not Brute-specific — the Brute-only block at
   `0x009994fd`-`0x00999558` is a SEPARATE, additional camera-shake side effect on a different
   entity flag, `+0x1ca8`):** lazily creates and caches (at entity `+0xce0`) a persistent
   "Human"-category voice via the project's own already-documented emitter-create wrapper
   (`0x0094c9e0`→`0x0045da50(&pos,"Human",0)`, §11a.1) — i.e. footsteps use a per-entity
   persistent voice, not the one-shot pool. Sets an RTPC (`0x0045f2d0`, id `0xfdf31b1a`) and 3
   Wwise switches via the confirmed `SetSwitch` wrapper `0x0045f5b0`, then posts the event via
   `0x0045ea70`.

**Re-verified by direct query and independently recomputed FNV-1 (project's own standing Wwise
hash scheme), every one an EXACT bit-for-bit match, not taken on the agent's word:**
`0x00999716`/`0x0099971c` push `0x6f394b77` into `0x0045ea70` — **`FNV1("FOOTSTEP") =
0x6f394b77` exactly** — the identical literal `FOOTSTEP` event the 144 baked `anim_files.xtbl`
rows already use, now confirmed posted directly from runtime locomotion code too, bypassing the
Sound-xtbl pool entirely. `0x00999020` sets **three** switch groups (a 4th, `"Aim"`, is
gameplay-state-driven, not material-related — selects `"Normal"` or `"Fine_Aim"` by an
aim-stance check, hashed live via the confirmed `GetIDFromString` path): `0x40dc0461` =
`FNV1("MOVEMENT_TYPE")` (value `0x7db16dbe`=`FNV1("WALK")` or `0x2a72b9a8`=`FNV1("RUN")`);
`0x41654531` = `FNV1("MATERIAL_TYPE")`, the SAME group the 144 baked rows use; and a THIRD,
previously-unnoticed group, `0xc3e810da` (plaintext not yet identified).

**CORRECTION, 2026-10-11 (SPEC TEAM, agent `aa249198674bf2f1c`, independently re-verified by
direct query before splicing — the original framing above, written the same day, conflated two
separate mechanisms sharing one input index): `MATERIAL_TYPE`'s real runtime value is NOT the
3 hashes originally reported, and is NOT produced by a 31-way jump table at all.** Both feeder
functions are trivial and non-Havok, re-verified exactly: `0x009420c0(entity)` is a 3-instruction
leaf returning `*(entity+0x1a74)` (a cached locomotion-state index, role not further pinned);
`0x005539c0(index)` is a flat, bounds-checked (0..0x20) **33-entry static lookup table** at
`0x012e2b18` (default `0x322a1bec` out of range) — re-verified, all 33 real values dumped
directly, exact match. **This table's output IS the real `MATERIAL_TYPE` value** (the decompile's
own 2nd local, pushed straight into `0x0045f5b0(0x41654531, <that local>, ...)` — re-verified
exactly). The 3 previously-"unidentified MATERIAL_TYPE values" (`0x25954450`/`0x8b4347f0`/
`0x38aff350`) are real, but belong to the THIRD group, `0xc3e810da` — re-verified exactly in the
decompile (a separate local defaults to `0x25954450`, reassigned to the other two by a SEPARATE real 31-way
jump table at `0x00999744`/pointer pair `0x0099973c` — re-verified, `0x0099973c`'s own 2 entries
match exactly — gated on a height-delta/fall comparison against `1.2`, not a material test).
**`entity+0x1a74`'s own index feeds BOTH the 33-entry table (→ real `MATERIAL_TYPE`) and the
31-way jump table (→ group `0xc3e810da`) as two independent consumers of the same index — not
one mechanism producing one group's value, as the original same-day framing said.**
`0xc3e810da`'s own candidate semantic (HYPOTHESIS only, from the height/fall-comparison gating
and the Brute-NPC-only branch at `+0x1ca8`): plausibly a landing-hardness/fall-height classifier,
not a surface material at all. **Cross-corroborated from the shipped Wwise bank's own real
bytes, independent of the code (agent-reported, structurally consistent with the code-side
findings — not independently re-extracted byte-for-byte by this verification pass):** the
FOOTSTEP event's real target Switch Container object (`0x31c0d5b0`, found by walking the real
`.bnk_pc` HIRC chunk) contains literal `0xc3e810da` immediately followed by switch-value
`0x25954450`, with `0x8b4347f0`/`0x38aff350` also present verbatim elsewhere in the same
payload — and a SEPARATE, different kind-6 object in the same bank carries the literal
`0x41654531` (`MATERIAL_TYPE`) followed by the exact same default constant `0x322a1bec` the
33-entry table also uses at index 0/30/31 — the default value matching across two independently-
derived sources (static table vs. real bank bytes) is strong corroboration both halves of this
correction are right.

**The real next step for `MATERIAL_TYPE` plaintext recovery: FNV-1 brute-force the 33-entry
table's own values** (`0x322a1bec`, `0x494c975d`, `0x8fcd7364`, ... — full list in
`scratchpad/footstep_switch/ptrs_0x012e2b18.txt`), not the 3 `0xc3e810da`-group hashes a prior
same-day pass incorrectly chased — those 3 were never `MATERIAL_TYPE` candidates to begin with.

**Net: ordinary player/NPC footsteps ARE driven by the SAME `left-foot`/`right-foot` family of
anim triggers after all — the error was in which of the 4 real trigger-consumers to keep reading,
not in the trigger-vs-other-mechanism framing.** `0x00999020` both blends foot-plant IK AND posts
the real `FOOTSTEP` Wwise event, gated by distance, keyed by a runtime-selected `MATERIAL_TYPE`
value (from the 33-entry table) plus a separate, newly-found `0xc3e810da` switch group. This
closes the open item; the `MATERIAL_TYPE` plaintext-value and `0xc3e810da`-group-name questions
are new, narrower OPEN items, not blocking Team B's implementation (the group hashes/mechanism
are confirmed either way).

**Genuine negative, same pass, worth keeping on record (ruled out, not the footstep mechanism):**
`foley_collision.xtbl`/`foley_touch.xtbl` (loader `0x00561450`/`0x00561ab0`, consumers
`0x00561370`/`0x005613b0`/`0x00561410`, reached from Havok contact-response
`0x00762770`/`0x00763860`/`0x00929f60`/`0x009286a0`) is a real, velocity-gated generic
collision-foley system (51 prop-impact rows + 8 pickup-touch rows, confirmed by reading the real
decoded xtbl content) — genuinely useful for OTHER ambient-impact sounds, but confirmed to have
zero footstep-related rows.

**[CONFIRMED — disassembly for all addresses cited; independently re-verified by direct xref
(`0x0153be40`'s exact 4-use/4-caller list; `0x013bb470`'s exact 2-use list) and direct decompile
(`0x00999020`'s own call to `0x0045ea70` with literal `0x6f394b77`, and the 2 switch-group pushes)
before splicing, not taken on the agent's word — including independently recomputing all 5 FNV-1
hashes (`FOOTSTEP`/`MOVEMENT_TYPE`/`MATERIAL_TYPE`/`WALK`/`RUN`), all exact matches. OPEN: the 3
unidentified runtime `MATERIAL_TYPE` value hashes and the 31-way selector's real meaning; identity
of `0x0052a230` (distance-cull override) and `0x009420c0`/`0x005539c0` (selector inputs); which
point the compared distance is measured from (entity position vs. listener/camera, not
independently confirmed); whether `0x00999020`'s own single caller (`0x0099ebe0`, a per-frame loop
over the active-character list `0x03171a64+0x1ec/0x1e4/0x58`) runs for both player and NPCs or
NPCs only.]** Addresses: `0x004ae1e0`, `0x004ae7a0` (`0x02ef2178`), `0x004ca2d0`, `0x004cad90`,
`0x004aded0` (`0x030f0c68`), `0x004b27b0`, `0x004bdec0`, `0x004c7090`, `0x004b4320`, `0x0073d550`,
`0x0050c130`, `0x00552070`, `0x005246a0`, `0x00999020`, `0x009a8d30`, `0x0057bfd0`, `0x013bb470`,
`0x0094c9e0`, `0x0045da50`, `0x0045f2d0`, `0x0045f5b0`, `0x0045ea70`, `0x0052a230`. Full detail:
`scratchpad/footstep_rt/`, `scratchpad/footstep_wwise/`, independently re-verified in
`scratchpad/verify_footstep/`, `scratchpad/verify_footstep_wwise/`.

**Answering Team B's three questions directly (UPDATED 2026-10-11, superseding the "OPEN"
answers below — the runtime consumer this section originally failed to find is `0x00999020`,
see the correction above):**
1. **Fired from an anim event — CONFIRMED, not OPEN.** `0x00999020` gates on exactly the same
   `left-foot2`/`right-foot2` (or `left-foot`/`right-foot`) trigger facility this section already
   documents; no separate cadence timer exists. The earlier "which function consumes the trigger
   at runtime" question is answered: this same function both blends foot-plant IK AND posts the
   Wwise event, which is why the first pass's check of its camera-shake callee alone missed it.
2. **Yes, it varies by ground material, for BOTH tiers — the generic tier is NOT baked, it's
   runtime-selected. CORRECTED same day:** the selection is NOT a 31-way jump table — it's a flat
   33-entry static lookup table (`0x012e2b18`) indexed by a per-entity cached locomotion-state
   field (`entity+0x1a74`, read by the 3-instruction leaf `0x009420c0`), via `0x005539c0`. Fixed-
   context clip tier: unchanged, static `MATERIAL_TYPE` (`CONCRETE`/`METAL_SOLID`/`FLESH`) baked
   at content-authoring time. Generic open-world locomotion tier: real runtime value = this
   table's own entry for the current locomotion-state index (33 real values, not yet matched to
   plaintext — a fresh FNV-1 brute-force should target THESE 33, not the 3 hashes originally
   misattributed here, which belong to a separate, newly-found switch group `0xc3e810da`
   instead). Not a ground raycast — the index is a cached per-entity field, not a physics query;
   whether that field is itself ever set from a real ground/material source upstream was not
   traced this pass (OPEN), but the footstep-side selection mechanism itself is now fully
   characterized.
3. **Fixed-context table unchanged (above). Generic tier: the switch GROUP and the real selection
   mechanism (33-entry table) are now known; the table's 33 VALUES are not yet named** — see OPEN
   item above.

**Cross-reference, CHECKED AND REFUTED, 2026-10-11 (agent `aa249198674bf2f1c`, independently
re-verified):** the plausible structural analogy below (a Havok-body-property tag, same
convention as vehicle-wheel friction) does **NOT** hold for footstep material selection — both
`0x009420c0` and `0x005539c0` were opened in full and confirmed to touch no Havok structure at
all; the real source is a cached per-entity locomotion-state index into a flat static table, not
a physics/contact query. Left below for the historical record of what was checked.

**Cross-reference, not re-opened:** `spec-physics-runtime.md`'s already-confirmed finding that
per-wheel vehicle-ground friction comes from **a custom Havok body property tagged on the struck body**
(key 5, fallback key 7) **rather than a materials-table lookup** is a plausible structural analogy for
how a runtime surface-audio-material tag could work for characters too (a property on the ground shape,
not a mesh-vertex or per-zone table) — but this pass found no character/footstep-side code reading any
such property, so this is offered as a lead for a future pass, not a finding.

**Honest scope note.** This pass read the base game's `anim_files.xtbl` only; DLC archives were not
checked and could add more `MATERIAL_TYPE`/`SHOE_TYPE` values. Full detail: this pass's own hand-off,
folded in here; no separate scratch file. Cleanroom self-check: strict regex 0 hits, C-cast-dereference
check 0 hits.

---

## 12. OPEN items

1. ~~**What sets each voice field from the game side** (positions `+0x28…`, `+0x9f`, obstruction target
   `+0xa2`, aux sends `+0x60…+0x74`, per-voice RTPC value `+0x0c` and enable `+0xa6`, persona keys
   `+0x14`/`+0x18`, marker callback `+0x04`/`+0x08`/`+0x9e`) — the voice API is a later unit; this file
   only reads them.~~ **PARTLY CLOSED 2026-10-08 (§11a).** Position (`+0x28`/`+0x2c`/`+0x30`, 3 floats
   only — orientation `+0x34..+0x3c` is never written in the traced path) and `+0x9f`/`+0x98` bit 0/
   `+0xa6`: `0x0045da50` (§11a.1). Aux sends `+0x60..+0x74`: `0x004682d0` (§11a.3). Per-voice RTPC value
   `+0x0c`: `0x004685c0` (§11a.3). **Still OPEN, searched for and genuinely not found**: the obstruction
   target `+0xa2` writer (searched across all 10 voice-API + all 7 object-sound-core functions, ~700
   instructions, zero hits — §11a.5); persona keys `+0x14`/`+0x18`; marker callback `+0x04`/`+0x08`/
   `+0x9e`; whether an existing voice's position is ever updated post-creation (§11a.1) or only
   refreshed by create/release churn.
2. **Request bit `0x800000`** (→ state 8) and the writers of request bits 0–3 — the voice API.
3. **`0x0045c160`** (the per-voice RTPC "changed" test before `0x00470960`): a tiered threshold that
   depends on the magnitude of `+0x0c` (floats `0x012a2f80`, `0x012a2f7c`, `0x012a2f78`, `0x01351c3c`,
   and 1.0); values not read.
4. ~~**`0x0045c230`'s fourth argument** (0.009 at init): stored with the per-voice RTPC but never passed
   to Wwise in anything read; HYPOTHESIS an interpolation time the 2011 `SetRTPCValue` signature has no
   room for.~~ **CLOSED 2026-10-08 (§11a.1):** it is passed, from `0x0045f470` (the voice-API's per-voice
   RTPC setter, previously mislabeled), with the literal `0.009` from `0x012a2ff4` — HIGH an
   interpolation/transition time; `0x0045c230`'s own body is still unopened (new narrower open item).
   **New OPEN items from this pass, 2026-10-08:** the writer of the zone/AABB-override config globals
   `0x03172938`..`0x03172964`/`0x03172950` (§11a.3, one static box vs. a real multi-zone system is
   unresolved); the camera/view-matrix source feeding `0x005546c0`, the sole caller of the listener
   producer (§11a.2); whether continuously-tracked looping emitters (vehicle engines, wind, foley)
   share the one-shot object-sound pool's create-once model or have their own persistent path (§11a.4).
5. ~~**Code-7 side call** `0x0045c870(record)` when `0x03171a5a` is set and the record never started —
   result unused; purpose unknown (diagnostic?).~~ **CLOSED 2026-10-10.** `0x0045c870` (body
   `0x0045c870`-`0x0045c9ce`) is CONFIRMED a locked linear lookup, not a logging/telemetry/counter
   call: it takes a lock via the vtable call at `[0x031f0280]+0xc`, walks the global `0xa8`-byte
   group-record table (`0x031f02c0`/`0x031f02bc`/`0x031f02be`/`0x031f02c4`), and within each active
   group's linked list (hung off `+0x24`) looks for a node pointer equal to the `record` argument,
   returning the owning group pointer (or 0) after releasing the lock (vtable `+0x10`) — no side
   effects beyond the lock/unlock when its result goes unused. It has 6 callers total (not just the
   code-5/code-7 dispatcher — also `0x0045f1a0`/`0x0045ee30`/`0x0045f470`/`0x00469ef0`, not
   individually checked this pass). The same call is CONFIRMED meaningful in the code-dispatcher
   `0x0045c9d0`: in the **code-5** case the identical call's return value is used — if non-zero and
   group flag bit `+0x9e` bit 0 is set and a callback pointer at `+0x4` is non-null, the dispatcher
   assembles a parameter block and invokes that callback. The **code-7** site
   (`0x0045cb06`-`0x0045cb1e`) performs the identical lookup but drops the return value immediately
   with no branch or use of it. **CONFIRMED `0x03171a5a` is read exactly once in the entire binary**
   (that code-7 gate, `0x0045cb10`, independently re-verified via xref — `total uses 1 in 1
   functions`) **and has zero write sites anywhere** — it is a zero-fill static, so it is
   permanently 0 in the shipped executable; nothing in `SaintsRowTheThird.exe`'s own code ever sets
   it. HIGH CONFIDENCE this is vestigial/dead debug instrumentation (most plausibly an abandoned
   "re-verify `record` is still registered" sanity check mirroring code-5's real lookup-and-callback
   pattern) that never executes in the retail build regardless of build config — not a true
   compiled-out no-op (the instructions are present and would run if the flag were ever set), but a
   no-op in effect since nothing sets the gating flag.
6. **Identity of the four required config pointers** `+0x3c`…`+0x48` and the other unnamed config
   fields (§3.1) — whoever builds the config block at `0x00554c90`. **[Partly resolved, §6.4 (U-W5):
   `+0x3c`/`+0x44`/`+0x48` are the game's own file-I/O start/finish/poll primitives, called by both the
   scheduling loop and the hook's own fast path — `+0x40` was not observed called anywhere this pass
   reached, and which game subsystem supplies all four (most plausibly the `.vpp_pc` archive reader) is
   still not traced.]**
7. **The value of `k` (`0x0317297c`)** and who writes the RTPC targets `0x031728dc`, `0x031728d8`, the
   seven mix values `0x03172918[]`, the listener globals and the arm counter `0x03172908`.
8. **Names of the RTPCs**: `0x85c3e987` (per voice), `0xdb2d51a3`, `0x9e3669da` and the seven mix ids at
   `0x013520ec` are FNV-1 hashes whose source strings were not searched for here.
9. **`0x0046bad0` (I/O `Cancel`)** and the I/O request lifecycle.
10. **Bank load failure with no re-request pending** (§3.7) — whether `0x004673c0` covers it.
11. **Exact public names** of the SDK structs and enums used above (generation: 2011 line, from the
    export names); only the offsets and values are CONFIRMED.
12. **The scheduler sub-object's own vtable owner** (hook `+0x8180`, §6.5) — Lock/Unlock slots confirmed
    by call pattern, not tied to a public SDK class name (U-W5).
13. **A soft-named pair of pending-node fields** (`+0x1c` heuristics/priority byte, `+0x1f` secondary
    status/error flag, §6.2) — read/write sites confirmed, exact semantics not pinned down (U-W5).
14. **The three in-memory-package fast-path globals** (`0x00320e460`/`+4`/`+0x10`/`+0x14`/`+0x1c`/`+0x20`,
    §6.3) were not expanded beyond confirming the lookup-and-copy call shape (U-W5).
15. **`0x031728a0` (config `+0x40`)** was not observed called anywhere this pass's reach touched, unlike
    its three siblings `+0x3c`/`+0x44`/`+0x48` (§6.4) — genuinely open, not just unnamed (U-W5).
16. **The container-dispatch root's two indirect vtable owners** (`0x00f52320`'s own `ebp` object, slots
    `+0x7c`/`+0x80`; and `*(esi+4)`'s object, slot `+0x10`, §7.1) — exhausted by RTTI and plain-string
    scan (§7.6): no container/parameter-node class carries RTTI or a plain string anywhere in this
    binary, only the bottom-layer plugin/effect/source/codec classes do. Genuinely unnamed, not an
    unattempted gap (U-W2b).
17. **Whether `0x00f51cc0`'s fast path specifically implements the Sequence/Random container kind**
    (§7.2) — HIGH from a bit-position match to Wwise's public authoring-tool "Shuffle/Standard"
    terminology (cited for orientation only, not derived from any leaked source), not independently
    confirmed by any name or string in this binary (U-W2b).
18. **`0x00f51eb0`'s own caller** (the "re-evaluate already-active instances" broadcast path, §7.3.4) —
    not reached from the traced container-dispatch root in this pass; HIGH it is wired from the
    RTPC/switch/state side (unit U-W3) or a stop/pause action handler, not independently confirmed
    (U-W2b).
19. **`~CAkRegisteredObj`'s own closure** (`0x00f55590`/`0x00f55510`, §7.5) — sized by the scoping note
    as a 21-function/909-instruction closure; this pass read only the top-level destructor body (44
    instructions) and did not expand either callee (U-W2b).
20. **A fourth, unnamed Wwise-internal manager singleton** at `0x02cc5324` (the voice/playback-slot pool
    allocator's own `this`, §7.3.3) and a fifth at `0x02cc5314` (§7.7) — both sit in the same contiguous
    global-pointer table as the export-confirmed `g_pRegistryMgr`/`g_pRTPCMgr` but carry no export name
    or RTTI of their own (U-W2b).
21. **The runtime-computed caller of the §8 chunk reader/HIRC dispatcher was not located.** Three
    independent whole-block static scans found zero direct calls and zero stored literal references to
    either address anywhere; most plausible explanation is a polymorphic "bank processing job" object
    whose vtable is constructed at runtime rather than existing as a static `.rdata` literal. Next step:
    open the game-facing wrapper's own likely bank-job submission point `0x00f57710` and its direct
    callees to see whether a job object is constructed there (U-W1).
22. **The 15 non-fallback HIRC kinds' own "allocate"/"apply" function pairs are almost entirely
    unopened** (§8.2.3) — only kind 1's pair was read far enough to confirm the lock/construct-or-apply/
    unlock shape, not its own property layout. This is where the real per-kind HIRC property parsing
    lives — the natural next unit after U-W1 (U-W1).
23. **Kind-number-to-Wwise-name mapping beyond the structural kinds-10–13 finding is HYPOTHESIS** (§8.2.1).
    A concrete checkable next step: if kind 3's handler chain shows an 11-way branch matching A7's "Action
    breaks into 11 verbs," that would independently confirm kind 3 = Action (U-W1).
24. **`[0x02cc536c]`, the optional "unregistered kind" callback pointer, was never seen written** anywhere
    in the scanned Wwise range (§8.2.2) — either written from outside that range or permanently dead in
    this shipped build (U-W1).
25. **The `[esp+0x24]` flag in the HIRC fallback path** (§8.2.2), set only for kind bytes `0xa`-`0xd`, was
    read but its downstream use was not traced (U-W1).
26. **Whether `[0x2cc5310]` is literally `g_pRegistryMgr` (`0x02cc531c`) or a separate-but-adjacent
    singleton was not settled** (§8.2.3) — the two addresses are `0xc` bytes apart; re-reading
    `CAkRegistryMgr::GetObjAndAddref` (`0x00f6e550`) with this pass's hash-table knowledge in hand would
    likely settle it quickly (U-W1).
27. **The large global aggregate at `[0x2cc5310]` was only sampled at 4 of its offsets** (§8.2.3) — its
    full layout (how many sub-managers, their sizes/vtables) was not mapped (U-W1).
28. **Locate the real drain of `G2`'s 31-bucket deferred-job table** (§9.3/§9.6) — `0x00f52c80` enqueues a
    job but nothing in PostEvent's own CONFIRMED closure reads it back. The single biggest remaining gap
    for "how the action list is walked" (U-W2a).
29. **Determine what `0x00f5a3e0`/`0x00f59f90`/`0x00f59de0`/`0x00f59730` actually belong to** (§9.6) — a
    transactional 1-or-N event-id poster with no found caller; may be PostEvent's real deferred drain
    (reached only indirectly), or the internal implementation of `PrepareEvent`/`PrepareGameSyncs`/
    `DynamicSequence::*` (its shape fits `PrepareEvent` well), or something else (U-W2a).
30. **Locate the real Event-object class's own vtable** (§9.2.1/§9.4.4) — CONFIRMED to exist and to follow
    the dtor/AddRef/Release convention (cross-validated via `0x00f59ec0`'s bulk-unregister routine), but
    its vtable address was not located; explicitly NOT `vt_ca54`/`vt_ca68` (U-W2a).
31. **Map `0x00f77040`/`0x00f77140`'s 5-way jump-table verb cases to Wwise's public taxonomy** (§9.4.3) —
    the 3-arg/3-arg/4-arg shape of cases 0-2 reads more like a "set a named property" family
    (Volume/Pitch/LPF-style) than the Play/Stop/Pause action-type axis; HYPOTHESIS only (U-W2a).
32. **Identify the HIRC kind at `G1+0` (table index 0)** used by `0x00f52bb0`'s fallback lookup (§9.4.5);
    not otherwise characterized. Also unexamined: `vt_ca54` slot 4 (`0x00000000`, genuinely NULL or
    unresolved?) and `vt_ca68` slots 4/5/11 (U-W2a).
33. **Per-RTPC-ID record field `+0x08`** (float, zeroed at creation, §10.1.1) is never read anywhere
    inside `SetRTPCInternal`'s own closure — purpose not resolved; candidate is a default/designer-
    authored base value before any object override, not checked (U-W3).
34. **Switch's "Get"-side consumer was not located** (§10.2.2) — `SetSwitchInternal` only stores
    `(object,group)->state` and fires a narrow notify list; it does not evaluate any property or pick a
    container child. The natural counterpart (something reading the `+0x308` table at switch-container
    decision time) was not searched for — the single most valuable next step for fully closing "how does
    a switch value actually change what plays" (U-W3).
35. **State's old/new lookup-chain identity was inferred from control-flow position, not independently
    verified** (§10.2.3), and **the four State properties' real names are HYPOTHESIS** (Volume/Pitch/LPF/
    HPF matches the public SDK's well-known set and the count matches exactly, but no string/export in
    this binary ties a name to any of the four) (U-W3).
36. **The 9-entry curve-shape jump table's individual entries were not decoded** beyond shapes 4 (Linear)
    and 9 (Constant), which are CONFIRMED by their own literal formulas (§10.1.3) (U-W3).
37. **The RTPC load-time thunk's caller (`0x00f86a70`, §10.1.5) was not found** — reached only through a
    function pointer, almost certainly on a per-HIRC-node "RTPC binding" record by analogy with the
    independently-confirmed Switch/State binding records (§10.2.4), but that analogy was not verified by
    locating the actual RTPC binding record's own vtable (U-W3).
38. **Which vtable slot, if any, is the real `GetBuffer`/decode-trigger for `CAkSrcBankVorbis`/
    `CAkSrcFileVorbis` was not identified** (§11.0/§11.5) — no slot examined reads a destination sample
    buffer, advances playback position in a way that looks like producing output, or returns a decoded-
    sample count. The strongest remaining candidates by elimination are slots 8/17 (header parse) and
    slot 10 (lookahead management), but neither was confirmed to produce PCM output. The single most
    important gap left by Wwise-internals work to date (U-W4a).
39. **Identity of the two sibling vtables that also reach the array-teardown thunk `0x00f8fa70`**
    (`0x0127dd40`, `0x0127dde0`/`0x0127dd88`, §11.5) — no RTTI COL found for either; worth cross-checking
    against the project-wide `scratchpad/rtti_classes.json` census (HANDOFF.md §27.H-2 "A4") rather than
    re-walking RTTI by hand (U-W4a).
40. **Several Vorbis-glue globals/helpers not fully traced:** `0x02cc5f40` (detach-helper context,
    §11.4.1, sits `0x14` bytes past the known `IAkStreamMgr::m_pStreamMgr`); the reference-counted
    resource `0x00fa4bb0` detaches from (HIGH: loaded media/cue entry, not confirmed); global `0x0134d2c0`
    (cached allocator/arena handle, HIGH); `0x00fa36b0` (real seek-position work) and `0x00fa45d0` (common
    stop/reset tail) — both decode-state-adjacent, not traced internally; the RIFF/WAVE chunk walker
    `0x00f8fcb0`'s own per-chunk field extraction (§11.7.1, borderline glue/core); exact public-SDK names
    for every HYPOTHESIS-labelled vtable slot in §11.2 (U-W4a).
41. **The caller-supplied output struct's own identity and full layout** (§11.9.1.2) — slot 10 writes
    fields at offsets `0x0` (buffer pointer), `0x4` (capacity), `0xc`/`0xe` (two word fields), and `0x340`
    (a status/result code), off its one stack argument. The large gap between `0xe` and `0x340` is
    unexplained — either this really is one large struct (a bigger pipeline/voice-level object, not a
    small per-call `AkVPLState`-equivalent), or `0x340` belongs to a different, adjacent object reached
    through the same pointer. Next step: find the call site(s) that invoke slot 10 virtually (the real
    per-tick pipeline code) to identify both the struct and who drives the per-tick decode (U-W4b).
42. ~~**The Vorbis-bitstream-specific internals below the shared decode worker `0x00fa49e0` are named by
    address only, not by function** (§11.9.5): `0x00fa4740`, `0x00fa4950`, `0x00fa4810`, `0x00fa6340`/
    `0x00fa62a0`, `0x00fa5fa0`, `0x00fa59a0` (confirmed real FPU/SSE work — closest thing found to an
    actual transform/windowing step). None read past confirming they're real, substantial code; a
    dedicated "decode-core-internals" pass could try to match one of these to Vorbis codebook decode,
    IMDCT, or windowing/overlap-add by structural shape (U-W4b).~~ **CLOSED 2026-10-08, §11.9.6: all six
    structurally classified (bit-reader, packet framing/dispatch, spectral-decode orchestrator, the
    inverse MDCT, windowing/overlap-add), plus their own further callees opened one level deeper
    (floor/residue/floor-apply identified by call shape, not yet opened in full — see §11.9.6's own
    OPEN list for what remains) (U-W4b).
43. **`0x00fa49e0`'s 4-argument convention was inferred from its only call site, not cross-checked
    against a second, differently-shaped caller** (§11.9.2) — the exhaustive scan found only the 3 sites
    inside slot 10 itself, so there is no independent site to verify the argument order against. Low
    risk (the stack arithmetic inside the function itself is internally consistent and was read
    directly) but flagged per WALLS.md's own caution about single-instance-only inferences (U-W4b).
44. **The 184-byte decode-state block's field map is still mostly unnamed** (§11.9.1.2/§11.9.2) — this
    unit adds `+0x0` (decoded sample count) and, relative to the block's own anchor, `+0x70`/`+0x74`/
    `+0x79`/`+0x1c` and a sub-pointer at `+0x10`, none named beyond "read/written during decode." A full
    field map would be reasonable scope for the same future decode-core-internals unit as item 42 (U-W4b).
45. **Slot 8/17 and the RIFF/WAVE walker `0x00f8fcb0` were ruled out as decode-trigger candidates only by
    the exhaustive-scan negative** (§11.9.3), not by an independent line-by-line re-read this pass —
    sound evidence, but a future pass wanting independent confirmation could re-read those two slots in
    full; not expected to change the answer (U-W4b).
46. **The two sibling vtables reaching the array-teardown thunk `0x00f8fa70`** (item 39 above) and the
    Vorbis decode-trigger finding (§11.9) were found by two different, equally rigorous methods in the
    same pass — worth noting that the strict control-flow-aware walker built for this unit (self-checked
    against the already-published `0x00f8fa70` 2-instruction result before being trusted on slot 10) is
    now the project's reference implementation for avoiding the WALLS.md byte-range-heuristic trap in
    this binary region; a future unit touching Havok/Wwise function sizing should reuse it rather than
    re-deriving a sizer from scratch (U-W4b).
47. **`CAkSrcFileVorbis`'s own path to the shared setup-resource cache (`0x00fa4c30`) was not found**
    (§11.11.4) — File's structurally equivalent call, `0x00fa3930` (25 instructions, read in full), only
    performs the lookahead-buffer-growth half of Bank's `0x00fa3290`; two further **virtual** calls
    through File's own stream-descriptor object (vtable slots `+0xc`/`+0x14`, indirect, not statically
    resolved) are the most likely place File actually reaches the cache. Until found, "codebook
    storage/selection is a shared hash-keyed cache" is CONFIRMED for Bank only, HIGH-by-analogy for File
    (U-W4c).
48. **`0x00fa4600`** (called from `0x00fa3290` immediately after `0x00fa4c30`, with `&decode-state+0x10`
    and a popcount-of-sample-rate value) **was named but not disassembled** (§11.11.3) — the single most
    likely candidate for what actually writes the real per-stream setup pointer the decode closure later
    dereferences at decode-state `+0x10`; reading it would upgrade §11.11.3's mode/mapping-table claim
    from HIGH to CONFIRMED and might explain the popcount-of-sample-rate computation (purpose unresolved)
    (U-W4c).
49. **Roughly half of the `"vorb"` chunk's 42 bytes remain unattributed to any confirmed read** (§11.11.2
    table) — a dedicated byte-by-byte pass against real extracted `"vorb"` chunks (per
    `spec-audio-format.md` §4's real-data corpus) correlated against every read of decode-state
    `+0x7c..+0xa2` across the whole Vorbis-class closure would close this (U-W4c).
50. **`this+0x48`/`this+0x4c` (File, §11.11.5) — the closest thing to a literal seek-table entry found,
    and the one thing this pass did not close** — not traced to any consumer. The small 16-bit selector
    that picks which field combination feeds them is itself File's own first incoming stack argument,
    re-read and reinterpreted as a word; its ultimate origin (whatever calls slot 17 in the first place)
    was not traced either (U-W4c).
51. **No cross-check yet against real extracted `.wem`/bank-embedded Vorbis stream bytes** for any
    finding in §11.11 — everything there comes from static code-path disassembly alone. A follow-up with
    real bytes in hand (confirming block-size exponents decode to sane values, or that two files sharing
    an authored encoding profile really do collide on the hash key) would convert several HIGH items to
    CONFIRMED (U-W4c).

---

## 13. Clean-room check

- Plain hex addresses and own words throughout; no Ghidra auto-names, no decompiler variable names, no
  pasted pseudocode or listing lines. Wwise API names are from the binary's PE export table
  (Audiokinetic's public API); public SDK struct/enum names are given only as labelled interpretations
  of CONFIRMED offsets and values. Thread, task, table and file names are the binary's own strings.
- Nothing touches `.czn_pc` zone data or named-object resolution. Nothing inside the Wwise block or the
  bank `HIRC` format was opened: the plug-in factory addresses are cited only as the pointers the game
  passes.
- The private Ghidra copy used while deriving this material was deleted after the dumps.
- **§6 (U-W5, the IO scheduling loop) and §7 (U-W2b, container traversal + voice instantiation)
  additionally quote:** nothing beyond plain hex addresses and the binary's own export names
  (`Malloc@MemoryMgr@AK`, `Free@MemoryMgr@AK`); the Wwise public authoring-tool terms "Shuffle"/
  "Standard"/"avoid-repeat" (§7.2) and the scheduler-type constant name (§6, HYPOTHESIS) are cited
  only for orientation, never derived from any leaked source. Both units were Ghidra-free (capstone
  linear disassembly plus, for §7, a project-standard RTTI/plain-string exhaustion scan); no Ghidra
  project was opened by either; no `.czn_pc` data was touched.
- **§8 (U-W1, HIRC load dispatch), §9 (U-W2a, event/action walk), §10 (U-W3, RTPC/Switch/State), and §11
  (U-W4a, Vorbis wrapper glue) additionally quote:** the four `.bnk` top-level chunk FourCC tags
  (`STMG`/`DATA`/`HIRC`/`STID`, §8.2) and the RIFF/WAVE/`fmt `/`cue `/`data`/`LIST` chunk tags (§11.7.1)
  are literal ASCII bytes read directly from this binary's own `cmp` immediates, not asserted from any
  outside source; RTTI mangled-name strings (`CAkSrcBankVorbis`, `CAkSrcFileVorbis`, `CAkSrcFileBase`,
  `CAkSrcBaseEx`, `CAkVPLSrcNode`, `IAkSoftwareCodec`, `CAkVPLNode`, §11.1) are the binary's own `.rdata`
  strings; `G1`/`G2`/`G3` (§9) and `vt_ca54`/`vt_ca68` (§9.4) are this project's own convenience labels
  for unnamed globals/vtables, explicitly flagged as such; kind-name and verb-name HYPOTHESES (§8.2.1,
  §9.4.3) are flagged Team-A-internal public/community taxonomy per `HANDOFF.md` §27.H-2's routing, never
  cited to Team B as fact. All four units were Ghidra-free (capstone/pefile disassembly, one unit also
  using an exhaustive whole-block byte-pattern `E8`/`E9` opcode scan and one an MSVC RTTI
  ClassHierarchyDescriptor walk); no Ghidra project was opened by any of the four; no `.czn_pc` data was
  touched; no leaked Audiokinetic/Volition/Havok/RAD material was read or used by any of them.
- **§11.9 (U-W4b, the real Vorbis decode-trigger slot) additionally quotes:** nothing beyond plain hex
  addresses already established elsewhere in this file or §11.0-§11.8 (U-W4a); no new strings, no
  public SDK names beyond the already-flagged HYPOTHESIS labels. Ghidra-free (a purpose-built strict
  control-flow-aware disassembler, explicitly self-checked against the already-published `0x00f8fa70`
  2-instruction result before being trusted on new code, plus two exhaustive whole-`.text` scans —
  relative `E8`/`E9` call/jmp targets, and a separate absolute-dword-load scan — for every static and
  immediate-load-then-indirect path to the shared decode worker `0x00fa49e0`); no Ghidra project was
  opened; no `.czn_pc` data was touched; no leaked Audiokinetic/Volition/Havok/RAD material was read or
  used.
- **§11.11 (U-W4c, the Wwise-specific Vorbis wrapping layer) additionally quotes:** the standard, public
  WAV/RIFF chunk tags and field names already cited for §11.7.1 (`"smpl"`, `"cue "`, `"labl"`, `"LIST"`,
  `cNumSampleLoops`, `dwCuePoints`, `dwName`, `dwPosition`) plus the public Vorbis I specification's
  `blocksize_0`/`blocksize_1` legal-range values (64/8192) and its modes/mappings setup-header shape —
  cited only as labels for CONFIRMED byte-level behaviour, per this file's own front-matter rule; the
  bare 16-bit packet-length-prefix mechanism, the hash-bucketed resource cache, and the vorb-chunk byte
  offsets were all independently derived from this binary's own disassembly first, never from any public
  Wwise vorb-chunk community documentation (ww2ogg/revorb-style tools), which is cited only as a
  cross-check pointer for semantic glosses, never as the source of a structural claim. Ghidra-free (a
  purpose-built strict control-flow-aware disassembler and frame-relative stack tracker, both
  self-checked against the already-published `0x00f8fa70` result); no Ghidra project was opened; no
  `.czn_pc` data was touched; no leaked Audiokinetic/Volition/Havok/RAD material was read or used.

---

## 14. Vehicle radio: station switching, input binding, and the OFF state (2026-10-09)

**Scope.** The live radio/music system — runtime station table re-use, station switching, the input
binding that drives it, and the OFF state. `spec-tables-audio-radio.md` already specs the DATA side
(station/persona/playlist tables) in full; this section is the native code that reads/writes those
structures at play time. Everything here is CONFIRMED by direct disassembly unless marked otherwise.
Runtime objects re-used as given, not re-derived (`spec-tables-audio-radio.md` §7/§11/§12/§14/§16):
station array (`0x013C58CC`, stride `0x690`, count `0x013C58DC`, fields `+0x40` name/`+0x100`
Genre/`+0x140` Wwise hash/`+0x689`-`+0x68A` flag bytes/`+0x5B0` 8-slot listener array), the shared
song/content table (`0x013BBC18`, stride 20), and the persona-preference byte (`+0x24`, station
index+1). Real data: 11 `<Info>` rows — 9 `Selectable`, 1 `News_Station`, 1 with neither flag (the
`Genre=Off` row).

### 14.1 The `+0x689` bit-7 flag, resolved — closes an existing OPEN item in `spec-save-format.md` §12.8.3

**`0x00560820`** is the native backing `cell_music_menu_toggle_station` (already identified from the
Lua-binding side, `spec-lua-api-behaviour.md` §59.2). Its body: `station+0x689 = (station+0x689 &
0x7f) | (hide<<7)` — **bit 7 is a per-station "hidden from the player" flag**, settable
independently. If hiding the station the local player is currently tuned to, it immediately forces a
move to the next selectable station via the same next/prev machinery as §14.3. This also explains
why the switch function (§14.2) refuses to tune into any station with bit 7 set: a hidden station is
simply unreachable by the normal switch path. **Correction for `spec-save-format.md` §12.8.3**:
narrow "the bit's meaning (unlocked? favourite? discovered?) is OPEN" to "hidden/disabled by the
player via the music menu, CONFIRMED." **OPEN, not resolved:** the enable/disable polarity in
§59.2's own English description didn't obviously reconcile with this native setter's own
hide/unhide-argument branch on a first read — the Lua wrapper itself (which bit value it passes for
"enable" vs. "disable") was not dumped, only this native.

### 14.2 Station switching: `0x005602e0`, the one real tune function

**`0x005602e0(vehicleHandle, newStationNumber)`** is the one function in the whole subsystem that
changes what a vehicle is tuned to — confirmed by 5 independent call sites (save-load, the
cell-phone toggle above, a hard-coded "force KLASSIC" cheat path, an AI/mission vehicle-behavior
state machine, and the two D-pad paths below). Body:

1. Resolves the vehicle object (`0x004dcf00`).
2. **Gates on the vehicle's own definition record: `*(def+0x870) & 0x04000000` (bit 26).** If clear,
   silent no-op. **This is exactly the `RadioTuner` flag `spec-vehicle-data.md` §7.6 already names at
   `+0x870` bit 26 — the first traced runtime consumer of that bit.**
3. Finds the vehicle's current registration, if any, via a linear scan of every station's listener
   array (`0x0055ec10`) — returns the old station and its stored float triple (or global defaults if
   unregistered).
4. Rejects the target if hidden (bit 7, §14.1) — returns failure.
5. No-op if already tuned there.
6. Calls `0x0055ed20(handle, oldFloats, targetStation)` — the real registration worker: refuses
   outright if the target's bit 5 is clear or its listener array is full (8/8). **Bit 5 is set only
   together with bit 6 by the `Selectable` xtbl flag — a vehicle can only ever be successfully tuned
   into a `Selectable` station** (or, for station 0 specifically, one with non-empty mix-tape
   content). On success, writes the new listener slot and **carries the old station's float triple
   into the new registration** rather than resetting to a default — **HIGH CONFIDENCE a
   crossfade/continuity parameter carry-over**, exact consuming Wwise RTPC/bus not independently
   traced.
7. On success: writes **the vehicle object's own `+0x1630` byte to the new station number** (the
   current-station field — confirmed per-vehicle-instance, not per-player/global, by three
   independent writers); unregisters the old listener slot; branches on `0x0055dc60` (tests whether
   old/new station's name is literally `"RADIO_OFF"`, §14.4) to pick stop/start vs. plain-switch
   Wwise cues; if the local player is in this vehicle, posts a final "radio changed" Wwise event
   (`0x00bda1c0`).

### 14.3 Input binding: full chain from D-pad press to the tune call

`spec-player-runtime.md` §3.3 already documents the D-pad latch (`0x005ad830`) producing
consume-pressed bits for `CBA_VDC_PREV_RADIO`/`CBA_VDC_NEXT_RADIO` while in a vehicle with the
"radio" permission bit (`+0x207c` bit `0x100`). This section traces those bits forward: the
in-vehicle action dispatcher (`0x005b5c50`, reached via `0x005b73c0`) calls **`0x005aebb0`**, which
checks the permission bit, tests the two consume-pressed bits, and calls **`0x00560580(handle,
direction)`** — the next/prev-station algorithm: finds the current index (`0x0055ec10`), then
**walks the raw station array circularly, one index at a time, calling `0x005602e0` for each
candidate until one succeeds** (not the UI's `Selectable`-filtered display list — the filtering
happens implicitly inside `0x005602e0`/`0x0055ed20`'s own acceptance gate). Practical consequence:
cycling with the D-pad silently skips station 0 ("Mix Tape") whenever it has no mix-tape content, and
the `RADIO_OFF` station whenever it lacks `Selectable` — both are ordinary array entries to this
loop, not special-cased by index. **Full chain, every link read in full:** `CBA_VDC_NEXT/PREV_RADIO`
→ `0x005ad830` (latch) → `0x005b73c0`/`0x005b5c50` (dispatch) → `0x005aebb0` (consume) →
`0x00560580` (search) → `0x005602e0` (tune).

A separate hookup, **`0x0055a930`**, fires on entering/changing vehicles (when global "radio sync"
statics `0x013bbae8`/`0x013bbaec` disagree with the player's current vehicle) and re-applies
whatever station the *entering* vehicle already carries at its own `+0x1630` — its own trigger site
not independently re-traced, consistent with the already-documented vehicle entry/exit handoff.

### 14.4 The OFF state: no dedicated flag, just absent registration

**`0x0055dc60`** is a one-line helper: true iff a station's `+0x40` name is, case-insensitively,
exactly `"RADIO_OFF"` — called inside `0x005602e0`'s post-switch branch (§14.2 item 7) to pick the
right Wwise stop/start cue pair. Combined with the `Selectable`-only tuning gate (§14.2 item 6,
`RADIO_OFF`'s row lacks `Selectable`): a vehicle whose persona prefers `OFF` can never be
successfully registered on any station by the normal tune path — either the spawn-time code calls
`0x005602e0` with the `OFF` index and it silently fails at the gate, or that code recognizes `OFF` by
name and skips the call entirely. **Either way the observable result is identical: no radio music,
ambient/foley audio only.** **HIGH CONFIDENCE** (both mechanisms individually CONFIRMED; their
combination for the persona-spawn case not traced end to end — the exact spawn-time function that
first applies a persona's `+0x24` preference was not located). **For Team B: "radio off" needs no
dedicated state variable** — "this vehicle has no station registration" reproduces it exactly,
provided a reimplemented `RADIO_OFF` entry is built without the `Selectable` flag.

### 14.5 OPEN: track selection within a station

Not resolved this pass. Each station's playlist-slot array (`+0x190`/`+0x194`/`+0x5A4`, already
specced, `spec-tables-audio-radio.md` §12.2) indexes into the shared song table, and each of a
station's listener slots carries its own parallel float-triple (the crossfade parameters, §14.2 item
6) — but whether that record (or another field not reached here) also stores a "currently playing
track index," and whether track position persists across vehicle enter/exit, remain **OPEN**, now
narrowed to: it is *not* `0x005602e0`, `0x0055ed20`, `0x00560580`, or any of the D-pad/cell-phone/
save-load paths — none of them select or advance a track, only a station. The actual track-advance
logic lives in an unreached per-station "song manager" update. Full detail:
`scratchpad/vehicle_radio_music_2026-10-09.md`.

---

## Changelog

- **2026-10-11 — §11a.8's `MATERIAL_TYPE` runtime-value claim CORRECTED (same day, same section):
  it's a flat 33-entry static table, not a 31-way jump table; the 3 hashes originally attributed
  to `MATERIAL_TYPE` actually belong to a separate, newly-found switch group `0xc3e810da`** (SPEC
  TEAM, agent `aa249198674bf2f1c`, independently re-verified by direct query before splicing —
  routed from orchestrator `purrsian-f5`). `0x009420c0` (3-instruction leaf, `*(entity+0x1a74)`)
  and `0x005539c0` (bounds-checked lookup into a 33-entry table at `0x012e2b18`, all 33 values
  re-verified exact) feed the real `MATERIAL_TYPE` value; the real 31-way jump table
  (`0x00999744`/`0x0099973c`, re-verified) is a SEPARATE consumer of the same index, producing
  group `0xc3e810da`'s value instead — the 3 "unidentified" hashes belong there, cross-corroborated
  from the real shipped `.bnk_pc`'s own Switch Container bytes (agent-reported, structurally
  consistent, not independently re-extracted byte-for-byte). Also checked and REFUTED the
  Havok-body-property structural analogy from vehicle-wheel friction — neither feeder function
  touches Havok at all. Real next step for `MATERIAL_TYPE` plaintext: brute-force the 33 table
  values, not the 3 `0xc3e810da` hashes. Cleanroom self-check: strict regex 0 hits, C-cast-
  dereference check 0 hits.

- **2026-10-11 — §11a.8's footstep posting mechanism RESOLVED: `0x00999020` posts the real
  `FOOTSTEP` Wwise event, with a runtime-selected `MATERIAL_TYPE` switch — corrects a wrong
  negative from the SAME prior pass** (SPEC TEAM, agent `ad14b91d5da2a1555`, independently
  re-verified by direct xref/decompile and independently recomputed FNV-1 hashes before
  splicing; routed from orchestrator `purrsian-f5`, starting from Wwise event hashes as
  suggested). `0x00999020` — the SAME function already opened as "foot-plant IK blending, plus a
  Brute-NPC-only camera-shake call" — also, later in its own body past where the prior pass
  stopped reading, gates on a distance cull (`audio_constants.xtbl`'s `FootstepRange`, squared
  into global `0x013bb470`, re-verified 2-use xref), creates/reuses a persistent per-entity
  "Human" voice, sets an RTPC and 3 Wwise switches, and posts event `0x6f394b77` =
  `FNV1("FOOTSTEP")` exactly. Switch groups `0x40dc0461`=`FNV1("MOVEMENT_TYPE")` and
  `0x41654531`=`FNV1("MATERIAL_TYPE")` both exact matches; the `MATERIAL_TYPE` VALUE is selected
  at runtime (31-way jump table), unlike the 144 baked-clip rows' author-time constant — 3 value
  hashes remain unidentified (OPEN). Genuine negative also recorded: `foley_collision.xtbl`/
  `foley_touch.xtbl` (prop-impact/pickup-touch foley) checked and confirmed to have zero
  footstep-related rows — not the mechanism, but real and documented for future use. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §11a.8's footstep runtime-dispatch item REFINED: foot-plant triggers CONFIRMED
  NOT the posting mechanism for ordinary locomotion; open item relabeled to "find the real
  mechanism" per orchestrator correction (the shipped game does audibly play ordinary footsteps —
  initial framing wrongly concluded "no audio exists")** (SPEC TEAM, agent `a3c067c7758abaea8`,
  independently re-verified by direct xref/decompile before splicing; self-picked, gameplay-
  feedback relevant; wording corrected same day per `purrsian-f5`). Closed the prior pass's own
  named next step (the per-record-pointer caveat): found the real `GetTriggerFired` facility that
  genuinely consumes the `"left-foot"`/`"right-foot"` anim triggers at runtime, opened all 4 of
  its real callers, and confirmed none reaches the Wwise event-post path — the signal is fully
  consumed by non-audio systems (vehicle-entry animation sync, foot-plant IK blending, camera
  shake). Net: the 144 baked vehicle-entry/Brute-NPC clips and the foot-plant triggers are
  confirmed NOT how ordinary footsteps get posted; the real mechanism (foley/surface-material
  system on locomotion speed/gait phase, a vtable-dispatched caller, or Wwise-side) remains OPEN.
  One structurally unclosable gap flagged (virtual/vtable-dispatched callers can't be xref'd).
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §11.11.4 gains the resolution of codebook slot 598** (SPEC TEAM, agent
  `a83f8946bc105956e`, bounded low-priority check per `purrsian-5b`'s item (b)). Slot 598's real,
  distinct pointer (`0x134fd30` = `0x1290290`) does not point at a real authored codebook: the 10
  bytes there decode to the sole dim=13 in the 599-slot library only because the leading 4 bytes are
  the IEEE-754 encoding of the float literal `0.1f`, producing a Kraft sum of ~4.72e-5 (vs. exactly
  1.0 for the library's other 6 ordered codebooks) and 16 entries with codeword lengths of 32-34
  bits, exceeding Vorbis I's own 32-bit legal maximum. Verdict: pointer-table spillover into adjacent
  `.rdata` constant data, not a bug in the published grammar and not a deliberate reserved slot.
  CONFIRMED for every byte-level claim (independently re-derived from the live exe via two Ghidra
  query modes, byte-identical to the existing blob); HIGH CONFIDENCE (inferred) for the root-cause
  framing. No `spec-*.md` file other than this one touched. Full detail:
  `scratchpad/codebook_slot598_check_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — new §9.7/§9.8: the real Event→Action→Sound HIRC kind mapping, corrected Action/Sound
  class identities, and the real on-disk source-descriptor layout for one-shot SFX** (SPEC TEAM, agent
  `ab470784c828689c1`, wave 6 item 6). Confirmed NOT already covered by §11a.7 (positioning/classification
  only). Settled `G1`'s registry array fully (kind 2=Sound at index 0, kind 3=the real 10,597-item
  Action-bearing kind at index 3 — both previously OPEN/mis-attributed); corrected §9.4's `vt_ca54`/
  `vt_ca68` framing (two unrelated classes, not base/derived; `vt_ca68` is the Sound object, not an
  Action — `vt_ca54` has zero real population anywhere in the shipped corpus). Decoded kind 2's real
  on-disk source-descriptor field layout via the shared parser `0x00f56ba0`, traced one level past §7.3.2's
  own declared stopping point (vtable slot `+0x44`) into a weighted multi-candidate-source selection
  layer. **Worked example against real shipped data** (`sh04.bnk_pc`/`sh04_media.bnk_pc`) empirically
  confirms the field layout and resolves `spec-audio-format.md` §4.2's own long-standing `tag`-field OPEN
  item as a side effect. Confirmed genuinely separate from the voice-line external-source mechanism
  (`0x00468ae0`) at the id-sourcing front end, convergent only at the shared codec/decode back end (§11).
  Full detail: `scratchpad/wwise_sfx_event_media_lookup_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — new §11.11.4a: the real audio-packet bit layout confirmed — no packet-type bit, a
  hardcoded 1-bit mode selector, derived (not read) long-block window flags** (SPEC TEAM, agent
  `aa014c1055b072ff4`, addition to wave 6 item 44). Direct response to Team B's further bisection:
  `vorbis_synthesis` fails with `OV_ENOTAUDIO` on every record's first real audio packet even after the
  setup-packet fix. Opened the per-packet dispatcher `0x00fa4810` (previously only summary-characterized
  as "reads 1 bit") to the instruction, plus its 3 callees. CONFIRMED from this exe's own disassembly,
  separate from any public-format analogy: no packet-type bit is ever read; the mode selector is a
  hardcoded `push 1` literal, not a computed `ilog(numModes-1)` field, and can only ever address 2 live
  modes; long-block window flags are derived from running decode-state carried across calls, never read
  from the bitstream — directly confirming Team B's own theory. Resolves prior OPEN item 48's consumer
  side (HIGH→CONFIRMED); the setup-pointer populator `0x00fa4600` remains open. Full detail:
  `scratchpad/wwise_audio_packet_bit_layout_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — §11.11.4 gains a conclusive clean negative: the codebook library is ruled out as
  the cause of Team B's reported 488/493 `vorbis_synthesis_headerin` failures** (SPEC TEAM, agent
  `aacf85bea09e86c11`, dispatched wave 6 item 44). A from-scratch extraction pipeline independently
  reproduced Team B's exact 488-vs-5, 32/34-vs-35/42 split from `cutscene_sounds.vpp_pc`. Every one of
  15,890 codebook-index reads across all 493 records is in-range and every referenced body is
  byte-exact, Kraft-complete; the low- and high-count profiles even share an identical 15-codebook
  subsequence verbatim. No bit-budget overruns across 7,983 real records sampled from 6 archives; no
  second/per-version codebook library found anywhere in this exe or the sibling DX11 build (99.99%
  byte-identical). Net: the real cause is downstream of the codebook section (floor/residue/mapping/
  mode reconstruction, or Team B's own header assembly) — consistent with a separate, more precise
  bisection pointing even further downstream, to the audio-packet level. `spec-audio-codebook-library.md`
  gains a short provenance pointer to this finding. Full detail:
  `scratchpad/vorbis_codebook_library_488_493_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-08 — §11.11.5 correction: `this+0x48` is the first-audio-packet offset, not the
  setup-packet offset — a semantic mislabel, not a bad formula** (SPEC TEAM, agent
  `a552853f71aca8358`, resumed after Team B falsified the implied reading on all 493 real records).
  The real setup-packet offset is `dataChunkStart + vorb[16:20]` alone (no `vorb[4:8]` term) —
  confirmed by reading the real setup-packet reader directly (`0x00fa4250`, the previously-missing
  2nd caller of `0x00fa4c30`) and the real seek-table walker (`0x00fa36b0`), which returns
  `vorb[20:24]` directly as the first-audio-packet offset. A population-wide identity
  (`vorb[20:24] = vorb[4:8] + vorb[16:20]`, 981/981) proves the old formula and the real
  first-audio-offset are the same NUMBER, just the wrong NAME. Per-packet header size re-confirmed
  unchanged (bare 2-byte prefix) — Team B's small 19-24-byte reads weren't malformed, they were
  reading the wrong position. Full detail:
  `scratchpad/wwise_vorbis_seektable_setup_offset_2026-10-08.md`. Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §11a.7: SFX call sites and the SFX/dialogue fork, CONFIRMED same mechanism**
  (SPEC TEAM, agent `a1b74666d0edd6ea5`, dispatched by `purrsian-04`). Three new, independent, real
  call sites (mission-script ambient/boss cues, a player/boss ability impact effect, a vehicle
  minigame event) all converge on the same object-sound-core entry point §11a already opened —
  confirming ordinary SFX of every gameplay flavor uses the identical mechanism already documented
  for the general emitter/listener case, not a separate path. SFX-vs-dialogue is one flag bit at the
  final `PostEvent` call, not a structurally different pipeline. New CONFIRMED negative: no bus id
  ever crosses the game→Wwise call boundary (bus/mixer routing is 100% data-authored); a HIGH-
  confidence cross-reference between the HIRC kind-histogram's kind-8/kind-19 population shapes and
  candidate Bus/Auxiliary-Bus roles. Three concurrency numbers at three layers tabulated (32/1,050/an
  unmeasured Wwise-internal cap). Distance attenuation/occlusion confirmed identical to the general
  case by direct consequence, not re-investigated. Full detail:
  `scratchpad/audio_mixer_3d_sfx_positioning_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — §11.11.1/§11.11.2/§11.11.5 closed: the Vorbis seek/packet-index table, and the
  `this+0x48`/`this+0x4c` consumer, both fully traced** (SPEC TEAM, agent `a552853f71aca8358`, routed
  via a Team B relay through `purrsian-04`, then refined with Team B's own 493-file corpus statistics).
  **§11.11.5**: the "two further vorb-derived fields, flagged not confirmed" item is CLOSED — full
  3-term-sum formula for both fields plus the 16-bit selector's real origin (a parent-chain walk, not
  File's own incoming argument as previously guessed), and the actual Seek-shaped virtual-call consumer,
  all traced to the instruction. **§11.11.2**: added vorb[16:20]'s role as the seek table's own
  byte-length (entryCount = vorb[16:20]/4, CONFIRMED statistically on 981 real records, zero
  exceptions); **downgraded** vorb[8:12]'s "first-audio-packet-offset" gloss from HIGH to a named
  HYPOTHESIS — the magnitude doesn't fit (19K–7.2M bytes, more consistent with "total compressed
  payload size"), independently falsified a second way by Team B testing the original gloss against
  all 493 of their own real files under 3 placements, 0/493 plausible. Incorporated Team B's own
  cross-checked byproduct (`dataChunkSize − vorb[8:12) − cleanRunEnd == 4`, exact on all 493, labeled
  separately as their own finding). Answered the chunk-size-variant question (the 42-byte vorb copy is
  fixed, not driven by the chunk's own declared size). **§11.11.6**: fixed a stale "block-size
  exponents = vorb bytes 4/5" summary row left inconsistent with this same file's own §11.11.2
  correction to `+0xA0`/`+0xA1` (vorb bytes 40/41); added a seek-table summary row. Full detail:
  `scratchpad/wwise_vorbis_seektable_setup_offset_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — §11.11.2/§11.11.3/§11.11.4 Vorbis setup-packet contradiction resolved + setup-packet
  parse opened** (SPEC TEAM, Wwise unit `a06c01f9e594d72f2`, dispatched after Team B's real-byte check
  of §11.11.2's prior table contradicted its own anchor row). **Root cause: `0x00fa6370` never reads
  decode-state at all — it takes the blocksize exponents as plain arguments. Its caller `0x00fa4c30`
  reads them from decode-state `+0xA0`/`+0xA1`, not `+0x7c`/`+0x7d`.** The copy-loop mapping
  (`decode-state = 0x7c + (vorbByte-4)`) was never wrong; combined with the real `+0xA0`/`+0xA1` read it
  correctly predicts vorb[40]/[41] as the legal exponents, re-verified against 9 fresh real vorb chunks
  (consistent with Team B's own 800+-sample finding that vorb[4]/[5] give an illegal value). §11.11.2's
  table rewritten (formula restated as never-wrong; new row marks `+0xA0`/`+0xA1`↔vorb[40]/[41] as the
  corrected exponent fields). §11.11.3's hash-bucket modulus corrected from 37 to 31 (re-derived from the
  real reciprocal-multiply sequence, checked against real hash values). New §11.11.3a opens
  `0x00fa63c0` (the setup-packet parse itself, previously flagged out-of-scope) in full: section order
  matches standard Vorbis I exactly except the time-domain-transform placeholder is dropped; codebooks
  confirmed inline-per-stream via `0x00fa7da0` (dimension/entries/ordered-flag/length-field-width all
  read directly off the bitstream, shrunk bit widths vs. standard Vorbis); floor/residue/mapping/mode
  type fields never read, hardcoded; channel count supplied by the caller, not read from the packet. One
  OPEN item: a 10-bit per-codebook table index into `0x134f3d8` whose consumer wasn't found (more likely
  an allocator size-class hint than a codebook library, not confirmed). §11.11.4 gets a strengthening
  note (not a retraction): the newly-opened parse confirms no codebook-ID field exists anywhere that
  could index a shared table — the hash-keyed cache is the only sharing mechanism, full stop. Full
  bit-width comparison table and floor/residue/mapping internals in
  `scratchpad/uw4c_vorb_contradiction_fix_and_setup_parse_2026-10-08.md`. Cleanroom self-check: **regex 0
  hits, backstop 0 hits** (unchanged strict baseline).

- **2026-10-08 — §8.6 gets an explicit cross-document synthesis: Wwise Music Engine confirmed unused on
  both data and code sides** (SPEC TEAM, agent `a9906e91027286ec8`, secondary task, verification/
  synthesis of two already-landed findings rather than new RE). Joins this section's own kinds-10-13
  population negative with `review/scoping-audio.md` §1.2's export-census negative (only bare
  `MusicEngine::Init`/`Term`/`GetDefaultInitSettings` ever called) into one explicit verdict — the two
  previously sat in separate documents with no connecting sentence. Also independently re-confirmed the
  real remaining gap in A7 (voice-line/`.lm_pc` streaming): `voice_control.xtbl`'s own runtime
  (`0x00469140`) and ~~the four ambient-emitter `.lm_pc`-streaming functions~~ **the four functions named
  here (`0x0046ca50`/`0x0046cdd0`/`0x0046d250`/`0x0046db00`) — CORRECTED 2026-10-08, §1.2a: full
  disassembly shows these are pure 3D attenuation-shape geometry evaluators, unconnected to `.lm_pc` or
  voice lines at all; the "ambient-emitter `.lm_pc`-streaming" label itself was wrong, not merely
  incomplete** remain genuinely unlanded, confirmed by exhaustive grep against every audio
  spec file, not a rediscovery of something already closed. Full detail:
  `scratchpad/havok_mopp_wwise_music_2026-10-08.md`. Cleanroom self-check: strict regex 0 hits,
  unchanged.

- **2026-10-08 — new §11.9.6 opens the Vorbis decode-core internals below the decode-trigger worker,
  closing OPEN item 42** (SPEC TEAM, agent `a55498f95a34c9d10`, secondary/stretch-goal task that went
  substantially further than scoped). Opened every lead §11.9.5 left unnamed plus their own further
  callees and found a complete, textbook Vorbis I decode pipeline: bit-reader (`0x00fa62a0`/
  `0x00fa6340`), packet framing/dispatch (`0x00fa4950`/`0x00fa4810`), a 276-insn spectral-decode
  orchestrator (`0x00fa5fa0`: floor decode → channel coupling-flags → residue decode → inline
  channel-uncoupling → floor-curve apply → IMDCT), the inverse MDCT itself (`0x00fa4da0`, 812
  instructions, zero calls — bit-reversal + SSE radix-4/8 butterflies + recursive-doubling stages, a
  structural match to the public split-radix FFT-based IMDCT algorithm, not name-confirmed), and the
  real windowed overlap-add + saturating PCM writer (`0x00fa46f0`/`0x00fa4740`/`0x00fa59a0`, a 5-way
  block-size-keyed window selector — not just short/long — consistent with this being a modified
  decoder core). All role labels (floor/residue/coupling/IMDCT) are HIGH-confidence structural matches
  to the public Vorbis I algorithm, not name-confirmed by string/RTTI evidence. Floor decode
  (`0x00fa6dd0`), residue decode (`0x00fa72b0`), and floor-curve apply (`0x00fa7060`) remain OPEN,
  identified by call shape only. Full detail: `scratchpad/havok_du2_wwise_uw4b_2026-10-08.md`.
  Cleanroom self-check: strict regex 0 hits, unchanged.

- **2026-10-08 — MAJOR CORRECTION: the "OPEN item"/"no codebook-ID field" conclusion above was wrong —
  codebooks ARE ID-referenced, via a real static 599-entry library** (SPEC TEAM, Wwise unit
  `a3da36707ff3fbf2d`, two passes — the first found the mechanism, the second, after a mid-task
  disk-space-emergency pause, dumped the actual data). §11.11.3a's "inline per-stream, not
  ID-referenced" framing and its own "OPEN item" are struck and replaced: each codebook slot in the
  main setup packet is a bare 10-bit index (`0x00fa6439`/`0x00fa643a`/`0x00fa643f`) into the pointer
  table at `0x134f3d8`, which despite its 1024-slot size holds only 599 real entries (600-1023 run off
  the end into an unrelated float array, confirmed spurious via duplicate resolved addresses); the
  resolved pointer becomes a fresh, independent bitstream cursor that `0x00fa7da0` reads the already-
  documented field grammar from. §11.11.4's title and its own "Strengthening note" (which had
  asserted the opposite — no codebook-ID field exists) are corrected/struck in place; the per-session
  VORB-reuse cache (§11.11.3) is unaffected and still real, just no longer "the only sharing
  mechanism" — it coexists with this separate, lower-level per-codebook-body library. Validation:
  all 599 slots parse with zero errors, zero byte-range overlaps, and all 437 VQ-lookup codebooks
  satisfy (2 as an exact equality) the standard feasibility bound `quantvals^dim <= entries <
  (quantvals+1)^dim` — strong static/structural validation, explicitly marked "matches disassembly,
  libvorbis acceptance PENDING" since no packet has actually been run through libvorbis yet (blocked
  on toolchain setup, not a negative result). Also resolved: no separate "explicit" VQ lookup-table
  shape exists (three storage-layout branches were misread as a second format; standard Vorbis's
  `lookup_type==2` isn't implemented at all here). The full byte contents of all 599 real codebooks
  are transcribed as data (not just described) — originally as scratch working files, now published
  as **new file `spec-audio-codebook-library.md`** (index table + full offset-prefixed hex blob +
  SHA-256 for byte-exact verification; data only, no code, synced like any other spec) since
  `scratchpad/` isn't carried by the Team-B sync and the raw working files weren't reachable there —
  so an implementer who cannot read the executable directly can still reproduce every real codebook
  exactly. Full methodology: `scratchpad/wwise_vorbis_setup_header_bitexact_2026-10-08.md`.
  Cleanroom self-check: regex 0 hits, unchanged strict baseline.

- **2026-10-07 — U-W5 and U-W2b added** (SPEC TEAM, two parallel Wwise-internals units,
  user-authorized full RE of Wwise). New §6 "The 'Audiolib Stream Manager' thread" (the real
  per-block I/O scheduling loop the prior scoping pass couldn't locate — it's entirely game-side,
  not inside Wwise's own code range, because the device's scheduler type forces Wwise into
  "deferred/lined-up" mode where the low-level I/O client, not Wwise, owns the loop; resolves 3 of
  4 previously-uninterpreted mandatory config callbacks from §3.1). New §7 "Container traversal +
  voice instantiation" (the full chain from container-type dispatch through the voice/playback-slot
  pool allocator, confirmed including priority-based eviction when the pool is full; the two
  internal vtable owners stay unnamed after an exhausted RTTI/string scan — no container/
  parameter-node class carries RTTI in this binary, only the plugin/effect/source/codec layer
  does). Former §6 OPEN items and §7 Clean-room check renumbered to §8/§9; OPEN items 12-20
  appended; item 6 annotated with U-W5's partial resolution. No other spec file edited by these
  two units; `review/scoping-audio.md` corrected directly by each (not raced). Ghidra-free
  throughout; no Ghidra project opened; no `.czn_pc` data touched; no leaked Audiokinetic source or
  other forbidden material read. Cleanroom self-check (strict regex + plain-substring backstop) on
  the entire finished file: **regex 0 hits, backstop 0 hits.**

- **2026-10-07 — U-W1, U-W2a, U-W3, U-W4a added** (SPEC TEAM, four parallel Wwise-internals units,
  completing the first wave of Wwise RE alongside U-W5/U-W2b). New §8 "HIRC load: the bank-chunk reader
  and the per-kind construction dispatch" (finds the real per-kind factory dispatch the scoping note's
  own `0x00f5acb0` citation turned out not to be — that address is a generic, kind-agnostic release
  routine shared by 22+ vtables; the real dispatch is a 19-entry MSVC jump table at `0x00f588c0`/
  `0x00f58b50`, reached only through a runtime-computed call no static scan can find; kinds 10-13 share
  the "unregistered" fallback, matching A7's "no Music Segment/Track/Switch/Playlist" content finding).
  New §9 "Event lookup and the action-list walk" (PostEvent is two steps, not one synchronous walk:
  `0x00f4e120` validates and enqueues a deferred job without touching any action list; a separate,
  not-fully-located drain step does the real walk; CONFIRMS the scoping note's `0x00f768c0` cluster as a
  real contiguous MSVC vtable pair for an Action-object class family, including a 5-way verb-dispatch
  jump table and a direct hand-off into U-W2b's own root). New §10 "RTPC/Switch/State internal
  application" (a genuine architectural asymmetry, CONFIRMED: RTPC is stateless curve evaluation on every
  call; Switch is folded into `CAkRTPCMgr` itself as an unexported sibling method, pure bookkeeping +
  notify with no value application; State is a separate manager with its own dedicated transition/ramp
  singleton — RTPC has no time-based smoothing of its own at all). New §11 "CAkSrcBankVorbis /
  CAkSrcFileVorbis per-class wrapper glue" (corrects the scoping note's dispatch-brief premise: `0x00f8fa70`
  is a termination/resource-release thunk, not the Vorbis decode core — cross-checked exactly against an
  independent same-pass U-W4b correction; CONFIRMS the exact RTTI inheritance chain and vtable slot counts
  for both classes, 17 and 20 respectively; the real decode-trigger slot for either class remains
  unidentified, the single biggest open item from this batch). Former §8 OPEN items and §9 Clean-room
  check renumbered to §12/§13; OPEN items 21-40 appended. `review/scoping-audio.md` corrected directly by
  the U-W1/U-W3/U-W4b units (not raced, light-touch strikethrough+correction per this project's own
  file-collision discipline); not edited by this changelog entry's own splice. Ghidra-free throughout
  (capstone/pefile disassembly; U-W1 and U-W4a each also used an exhaustive whole-block byte-pattern scan
  for their respective target addresses; U-W4a additionally used an MSVC RTTI ClassHierarchyDescriptor
  walk); no Ghidra project was opened by any of the four units; no `.czn_pc` data was touched; no leaked
  Audiokinetic/Volition/Havok/RAD material was read or used. Cleanroom self-check (strict regex +
  plain-substring backstop) on the entire finished file: **regex 0 hits, backstop 0 hits.**

- **2026-10-07 — U-W4b added** (SPEC TEAM, finishing the Vorbis-decode investigation U-W4a left open).
  New §11.9 "The real `GetBuffer`/decode-trigger slot — found: vtable slot 10" — answers the single
  biggest open question from U-W4a: **vtable slot 10, on both `CAkSrcBankVorbis` and
  `CAkSrcFileVorbis`, is the real decode trigger.** §11.2's existing table had already catalogued slot
  10 under "buffer-capacity/lookahead management" — correct but incomplete: that's only the first half
  of the function. The second half (never previously read) calls a shared decode-loop worker
  (`0x00fa49e0`), which decodes compressed units in a loop, writes PCM samples into the caller's output
  buffer, and fills a caller-supplied output struct with the buffer pointer and decoded-sample count;
  slot 10 then advances the per-instance sample-accurate playback position from that count. Confirmed
  exhaustive (two independent whole-`.text` scans — relative-call and absolute-dword-load — find no
  other static or immediate-load path to the decode worker, both self-checked against the
  already-published `0x00f8fa70` result first) and independently corroborated by `ReleaseBuffer` (slot
  2) freeing/clearing the exact same two fields slot 10 allocates/produces. §11.2's slot-10 table row
  corrected in place. OPEN items 41-46 appended (the caller-supplied output struct's own identity, the
  Vorbis-bitstream-specific internals below the decode worker, the decode-state block's full field map).
  `review/scoping-audio.md` §8.4/§8.5 corrected directly by the same pass (light-touch, not re-edited
  here). Ghidra-free (a purpose-built strict control-flow-aware disassembler, built specifically to avoid
  the WALLS.md byte-range-heuristic trap that caused the original `0x00f8fa70` mis-sizing, self-checked
  against that already-corrected result before being trusted on new code); no Ghidra project was opened;
  no `.czn_pc` data was touched; no leaked Audiokinetic/Volition/Havok/RAD material was read or used.
  Cleanroom self-check (strict regex + plain-substring backstop) on the entire finished file: **regex 0
  hits, backstop 0 hits.**

- **2026-10-07 — U-W4c added** (SPEC TEAM, answering the four Wwise-specific-wrapper-layer questions
  explicitly scoped to exclude generic Vorbis bitstream math, per `purrsian-a4`'s own instruction: a codec
  with an open public spec and a permissively-licensed real decoder library available gets only its
  proprietary wrapper RE'd, not the open algorithm re-derived). New §11.11 "The Wwise-specific Vorbis
  wrapping layer" — answers all four questions the unopened `"vorb"` chunk left open since §11.7.1: packet
  framing is a bare 16-bit length prefix with no Ogg pages (granule position is a running per-instance
  counter, not a stream field); the stripped setup/identification headers are rebuilt from the vorb
  chunk's own block-size-exponent bytes (validated against exact public Vorbis I legal bounds) plus one
  further 16-bit-prefixed setup packet; codebook storage is CONFIRMED to be a per-session, hash-keyed,
  reference-counted runtime cache (not a static table in the exe) for `CAkSrcBankVorbis`, with `File`'s
  own path to the same cache not yet located; loop points come from the standard WAV `"smpl"` chunk
  (fallback: the vorb chunk's own excluded first dword), and markers from the standard WAV `"cue "`/
  `"labl"` pair. OPEN items 47-51 appended. A cosmetic duplicate `---` separator left over from an earlier
  splice (end of §11.10) was also removed in this pass. Ghidra-free (a purpose-built strict
  control-flow-aware disassembler plus a frame-relative stack-offset tracker, both self-checked against
  the already-published `0x00f8fa70` result, the tracker additionally cross-checked against an independent
  by-hand derivation of `0x00f8fcb0`'s own calling convention); no Ghidra project was opened; no `.czn_pc`
  data was touched; no leaked Audiokinetic/Volition/Havok/RAD material was read or used. Cleanroom
  self-check (strict regex + plain-substring backstop) on the entire finished file: **regex 0 hits,
  backstop 0 hits.**
- **2026-10-08 — real HIRC kind population histogram (§8.6, new, renumbers old §8.5 to §8.7).** Full
  install-wide scan (1,026 real `.bnk_pc` entries, 721 parsed cleanly, 41,037 real HIRC items) confirms
  kinds 10-13 are genuinely zero population-wide (not just structurally fallback-routed), and finds kinds
  1/15/16/17 are ALSO zero despite having real construction code — only 11 of 19 dispatch kinds have any
  shipped content. Real distribution dominated by kinds 2 (17,952 items) and 3 (10,597 items). Surfaces two
  corrections to `spec-audio-format.md` (DLC archives also carry `.bnk_pc`; `sound_turbo.vpp_pc` does too,
  275 entries, contrary to its "not `.bnk_pc` at all" claim) — flagged here for whoever next edits that
  file. No Ghidra; reused the project's existing `.vpp_pc` container scripts rather than re-deriving them;
  public Wwise chunk framing only, no cleanroom RE owed. Cleanroom self-check: 0 hits, standard regex.
  Synced to Team B.

- **2026-10-08 — §11.11.3a: the non-ordered codeword-length grammar pinned exactly, real libvorbis
  acceptance achieved, the "has-lookup" flag confirmed 1 bit not 2** (SPEC TEAM, agent
  `a3da36707ff3fbf2d`, resumed — "the last Wwise blocker before Team B gets audible sound"). Closes a
  real bug in a prior Team B `transcodeOneCodebook` implementation attempt: for every non-ordered
  (`ordered==0`) codebook, there is a mandatory **1-bit sparse/dense selector** immediately after the
  3-bit width field (`0x00fa7dfa`) — read unconditionally regardless of whether the codebook turns out
  sparse or dense — that the prior attempt skipped, shifting every later bit read in that codebook left
  by one. Field order, dense case: `width(3,raw)` → `sparse(1)` → if sparse: per-entry
  `{present(1), if present: length(width)+1}` → if dense: per-entry unconditional `length(width)+1`.
  Width is used directly as bit-count, never itself +1-biased. Verified character-by-character against
  3 real codebooks (library slots 7/61/5, one genuinely dense with `sparse=0`), each closing its exact
  on-disk byte length with zero slack. **Separately, a complete minimal-but-legal Vorbis I setup header
  built from those same 3 real codebooks was fed to `vorbis_synthesis_headerin` against Team B's own
  vendored `vorbisstatic.lib` — id/comment/setup headers all `rc=0 ACCEPTED`**, upgrading this whole
  mechanism (including the earlier-landed static codebook-library/10-bit-index correction) from
  "matches disassembly" to "libvorbis-validated." Kraft's inequality checked across all 599 real
  codebooks under this grammar: zero violations, 598/599 exactly complete (the 1 exception is an
  ordered codebook, unrelated, flagged separately). Also newly stated explicitly: the "has VQ lookup
  table" flag is 1 bit, not 2 or 4 — an empirical "maptype is 2 bits" reading doesn't hold once
  codebook data is correctly sourced from the table-selected buffer. The exact `transcodeOneCodebook`
  fix was relayed to `team-b-cd` directly as their own code bug, not just a spec gap. Full worked
  examples and the libvorbis test harness: `scratchpad/wwise_vorbis_setup_header_bitexact_2026-10-08.md`
  Part 5. Cleanroom self-check: 0 hits, unchanged. Synced to Team B.

- **2026-10-08 — new §11a: 3D positioning, listener, and per-tick attenuation producers; partial
  closure of §12 items 1 and 4** (SPEC TEAM, agent `a561b9986b35bb682`, dispatched by `purrsian-62` for
  emitter/listener update contracts, game-object-id mapping, and attenuation traceability). Found the
  real producers §1.1's own row for `0x0045cd80` had flagged as "game-side, not read":
  `0x004685c0` (closest-of-N-position-samples distance to the listener, fed to Wwise as per-voice RTPC
  `0x85c3e987`) and `0x004682d0` (a genuine zone/AABB-gated environment/reverb aux-send override — the
  exact "per-zone reverb override on top of Wwise" the dispatch asked about by name). Found the
  emitter-position producer (`0x0045da50` — position only, no orientation) and the listener producer/
  consumer pair (`0x00468130`/`0x00468050`, with two previously-undocumented "valid" flags). **The big
  finding**: Wwise's "game object id" is confirmed to be the voice's own transient memory address, not
  a stable handle; the REAL persistent identity layer is a 32-slot "object-sound" pool keyed by an
  independent monotonic serial (`0x026ea798`) — directly resolving this file's own flagged-OPEN
  "engine serial vs. Wwise playing id" question (it's neither, a third independent id space). Entities
  resolve through the engine's generic 64-bit object-handle mechanism (`0x00458230`, 26+ unrelated
  callers elsewhere) — audio has no entity-id scheme of its own. Corrected two voice-API label
  mismatches (`0x0045f5b0`=`SetSwitch` not "bind/param"; `0x0045f470`=the per-voice RTPC setter,
  closing item 4's "fourth argument" mystery) and the real voice category strings (`"LUA 2D"`/`"LUA
  3D"`, not a prior guess of `"UI"`/`"Emitter"`). Attenuation curve evaluation itself re-confirmed
  entirely inside Wwise's own black-box code (deliberate, not re-opened); obstruction-target writer
  searched for across ~700 instructions and genuinely not found (honest negative, new OPEN item).
  Full detail: `scratchpad/wwise_3d_positioning_attenuation_2026-10-08.md`. Cleanroom self-check: 0
  hits, unchanged. Synced to Team B.

- **2026-10-08 — new §1.2a closes the A7 voice-line/`.lm_pc` gap, with a major correction, not a clean
  closure** (SPEC TEAM, agent `af5b77c554dcf7ae0`). `voice_control.xtbl`'s runtime (`0x00469140`) fully
  opened and bit-exactly round-tripped against its own unpacker (`0x00469100`→`0x004694c0`), closing the
  packed-record format completely. **Correction: the four functions this project's own prior Changelog
  entry labeled "ambient-emitter `.lm_pc`-streaming functions" (`0x0046ca50`/`0x0046cdd0`/`0x0046d250`/
  `0x0046db00`) are CONFIRMED by full disassembly to be pure 3D attenuation-shape geometry evaluators,
  unconnected to `.lm_pc` or dialogue of any kind** — that prior label was wrong, not incomplete. §2.1's
  `+0x1c` play-record field, previously glossed "HIGH: the persona-line (`.lm_pc`) streaming slots," is
  re-identified as a back-reference to the `.ctdg_pc` active-conversation pool already deeply traced in
  `spec-conversation-format.md` §4-§6 (not a `.lm_pc` buffer) — this pass also closes that document's own
  flagged-OPEN slot-count question (exactly 3, base `0x0322d828`) and traces the full allocate/populate/
  release cluster plus the Lua `audio_conversation_play`-to-pool call chain. `.lm_pc` itself is reframed,
  HIGH CONFIDENCE: per-soundbank line-variant/weight metadata consumed at play time by the external-
  source file-id resolver `0x00468ae0`, not raw streamed audio — tying together `spec-tables-audio-radio.md`
  §17's loader-side finding and this pass's own play-side finding via the shared registry object
  `0x01353760`. Full detail: `scratchpad/wwise_voiceline_streaming_lm_pc_2026-10-08.md`. Cleanroom
  self-check: no Wwise-internal code opened; 0 new hits. Synced to Team B.

- **2026-10-09 — new §14: vehicle radio runtime — station switching, input binding, OFF state**
  (SPEC TEAM, agent `af47387a794697d20`, WAVE 11 item 3). `0x005602e0` is the one real tune
  function, gated on the `RadioTuner` flag (`spec-vehicle-data.md` §7.6's `+0x870` bit 26 — first
  traced runtime consumer), only `Selectable` stations tunable, current-station field confirmed
  per-vehicle (`+0x1630`). Full D-pad input chain traced end to end
  (`CBA_VDC_NEXT/PREV_RADIO`→`0x005ad830`→`0x005b5c50`→`0x005aebb0`→`0x00560580`→`0x005602e0`).
  OFF state needs no dedicated flag — just absent station registration. Resolved the `+0x689` bit-7
  flag as "hidden/disabled by player," narrowing `spec-save-format.md` §12.8.3's own OPEN item (not
  edited there this pass — apply that correction too). Track selection within a station remains
  OPEN. Full detail: `scratchpad/vehicle_radio_music_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — new §11a.8: footstep audio event/switches resolved (data), runtime dispatch left
  OPEN — Team-B-routed question** (SPEC TEAM, agent `a1921856e97195898`). The real event is the
  literal Wwise event `FOOTSTEP`, sourced from `anim_files.xtbl`'s `Sounds` element, not any
  `.text` literal. Two tiers: 144 fixed-context clips (vehicle floor entry/exit, the Brute NPC kit)
  carry a baked `Sound` record posting `FOOTSTEP` with `MATERIAL_TYPE`/`SHOE_TYPE`/`MOVEMENT_TYPE`
  switch values (3/3/3 shipped values respectively) — author-time constants, not a runtime ground
  query; ordinary player/NPC locomotion clips carry no embedded audio event at all, only the
  generic `left-foot`/`right-foot` anim triggers (1,170/1,149 occurrences, essentially universal).
  **Runtime dispatch confirmed-negative by independent xref re-verification:** neither the `Sound`
  pool (`0x02e99688`) nor the trigger-occurrence table (`0x030e2200`/`0x030e2204`) has any reader
  outside its own loader anywhere in `.text` — the function that turns a fired foot-plant trigger
  into a live `PostEvent` was not found (caveat: an indirect, pointer-chasing consumer would be
  invisible to this xref method, so this is not an exhaustive negative). Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.
- **2026-10-10 — §12 item 5 CLOSED: the code-7 side call is a dead debug sanity-check, not
  telemetry** (SPEC TEAM, agent `a47d5f06c1256de01`). `0x0045c870` is a locked linear lookup
  (membership query against a global group-record table), productively used by the code-5 case to
  find a callback; the code-7 site performs the identical lookup but drops the result. The gating
  flag `0x03171a5a` is read exactly once in the whole binary and never written anywhere — a
  zero-fill static permanently 0 in the shipped build. Independently re-verified via direct
  disassembly and an xref scan before splicing (both matched; also caught and corrected the
  agent's "only caller" framing — the function has 6 callers total, not one). Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
