# Interpretation of bridge dumps `20261001T114101-team-a-mnao` (fade / zscene / UI follow-up)

Team A, 2026-10-01. Source: CrreishDump output for `team-a/ghidra/jobs/nzxf-followup.json`
(steps `fade`, `fadexref`, `fadelua`, `zscene`, `zxref`, `ui`, `uixref`). Follows
`interp_nzxf.md`; its §8 open list is worked through here. Every function named in the job was
present as a defined function (including 0x005a0110, which Ghidra had not defined at the time of
the nzxf job).

Labels: **CONFIRMED — disassembly** = read in these dumps' listings; **HIGH CONFIDENCE** = follows
from a dumped instruction, reference list or string but the deciding body was not dumped;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled, see §7.

Shared primitives cited by address, as in the spec front matter: `lua_gettop` 0x00dfde50,
`lua_pushboolean` 0x00dfe590, `lua_pushcclosure` 0x00dfe4f0, `lua_setfield` 0x00dfe830 (the
pseudo-index 0xffffd8ee is the globals table), CRC-32 name hash 0x00d9e8b0, UI Lua state getter
0x00e1a1b0 (returns 0x02a45450), call builder 0x00e0ca80, call dispatch 0x00e0cd00 → 0x00e0cba0,
millisecond clock 0x01320d9c with its three helpers: 0x00d9e4c0 "stamp is set" (value ≥ 0),
0x00d9e3c0 "stamp := now + n ms" (wraps at 1,800,000,000), 0x00d9e400 "stamp has been reached"
(900,000,000 wrap tolerance).

---

## 1. Fade: the completion native — `Screen_fade_transition_complete` (0x005a0110)

### 1.1 Registration (answers nzxf §8 item 1)

**CONFIRMED — disassembly.** The UI wrapper 0x005a01d0 (called from 0x008430f0) registers four
globals in the state it is handed, each as a C closure with no upvalues:

| Name | Native | Body |
|---|---|---|
| `Screen_fade_transition_complete` | 0x005a0110 | §1.2 |
| `sfx_faded_out` | 0x0059fb30 | pushes (state 0x012e6aa4 == 3) |
| `sfx_faded_in` | 0x0059fb60 | pushes (state == 2); **CONFIRMED** (nzxf had it HIGH CONFIDENCE) |
| `sfx_use_load_images` | 0x0059fb90 | pushes the byte 0x0149365c as a boolean |

All four call `lua_gettop` and ignore the count; the three queries return 1 value, the completion
native returns 0.

### 1.2 Body of 0x005a0110 — **CONFIRMED — disassembly**, full listing

The native takes no arguments and returns nothing. In order:

1. **Flip the state by its current value, not by the target.** If the state (0x012e6aa4) is 0
   (fading in) it becomes 2. If it is 1 (fading out) it becomes 3 **and** the "logo due" stamp
   0x012e6aac is set to now + 1000 ms while the other three stamps 0x012e6ab0, 0x012e6ab4,
   0x012e6ab8 are reset to -1. If the state is already 2 or 3 nothing changes in this step (a
   spurious call from the script is harmless).
2. **Fire the in-flight callback.** If 0x013effcc is non-null it is called with one argument: the
   current **target** 0x012e6aa8 (not the new state). The slot is then re-read, because the
   callback may itself have issued a request that stored a new callback.
3. **Deferred callback.** If the state now equals the target **and** the deferred slot 0x013effd0
   is non-null: when the deferred pointer is the same function as the (re-read) in-flight pointer,
   both slots are cleared and the native returns — the function has already been called in step 2
   and is not called twice. Otherwise the deferred function is called with the target as its
   argument and the deferred slot is cleared.
4. **Clear the in-flight slot.** If the in-flight and deferred pointers are equal (which after
   step 3 means both null, or both the same not-yet-satisfied function) the deferred slot is
   cleared too. Then 0x013effcc := 0.

Consequences the listing settles:
- The native **never starts a transition**. When the opposite direction was requested during the
  transition (target ≠ new state), the deferred callback is simply left in its slot and the state
  stays at the settled value; the re-issue is done by the per-frame routine (§2.3).
- Callbacks receive the target value (2 or 3), so a callback parked by a fade-in request that was
  overtaken by a fade-out request is called with 3.
- The nzxf reading "sets the state to the requested target ... and starts that deferred
  transition" is corrected on both points: the state is flipped from its running value, and the
  deferred transition is started elsewhere.

### 1.3 What completes a fade — settled

**CONFIRMED — disassembly:** the only two stores of 2 and 3 into the state outside init/shutdown
are in this native, and the native is reachable only as a Lua global of the state that 0x005a01d0
is registered into (its two references are the registrar's table entry and the push of that
entry). So a fade completes exactly when the UI script calls
`Screen_fade_transition_complete()`; the executable contains no timer that completes it.
**HIGH CONFIDENCE** (the script is not in these dumps): the shipped `screen_fade` document's Lua
calls it when its `screen_fade_do` tween ends.

---

## 2. Fade: the per-frame routine 0x0059fe70, init 0x0059fa30, shutdown 0x0059faa0

### 2.1 Init 0x0059fa30 (one caller: 0x005d2400) — **CONFIRMED — disassembly**

1. 0x012e6aa0 := 0x007b1cb0("screen_fade", 1). That routine (dumped at depth 1) finds or creates
   a named record, copies the name into it (64 bytes), stores the mode 1 at +0x12c, resolves the
   resource pair of that name through 0x007b64d0 (+0x490/+0x494) into the record (+0x120/+0x124),
   and — when both resources report ready through 0x00daff10 — looks the UI document up by name
   (0x00e1f2f0) and sets its byte +0x59b. It returns the record's +0x40 field, or -1 when the
   name has no record or the resources are not ready. **HIGH CONFIDENCE** label: 0x012e6aa0 is the
   **id of the loaded `screen_fade` UI document record**; -1 means "not loaded", which is why the
   request helpers skip the Lua call while it is -1.
2. 0x013effc0 := 0. Then the UI document named "screen_fade" is looked up with 0x00e1f2f0 (hash of
   the name via 0x00d9e740, linear walk of the document ring at 0x02a4d17c comparing +0x57c). If
   found, 0x013effc0 := that document's **+0x580 handle** (0x00e1f330 shows the handle encoding:
   low 16 bits < 0x40 index a 0x5a8-byte array at 0x02a4d170, and the entry's +0x580 must equal the
   handle). 0x00754410 is the known no-op stub. Then state := 2, target := 2, flag 0x013effc8 := 1.
   If the document is not found none of the last three stores happen.

So 0x013effc0 (stored into every call builder's +0x14 before `screen_fade_do`,
`screen_fade_logo_show`, etc.) is the **handle of the `screen_fade` UI document**: the calls run
in that document's context. **CONFIRMED** for the data path, **HIGH CONFIDENCE** for the label.

### 2.2 Shutdown 0x0059faa0 (reached through the thunk 0x007c69b0) — **CONFIRMED — disassembly**

Resolves 0x013effc0 back to the document (0x00e1f330); if present, destroys it (0x00e20f60:
unregister 0x00e1a1c0, optional hook at 0x02a5a020, 0x00e20a80, 0x00e1ee20). Clears 0x013effc0,
unloads the named record 0x012e6aa0 through 0x007b1420 (which queues the record's +0x40 id on the
list 0x012fcc68 when +0x48 is set, else releases +0x120), then target := 2 and state := 2.
0x012e6aa0 itself is not reset (it keeps the stale id).

### 2.3 Per-frame 0x0059fe70 (callers 0x005d14b0 and 0x007a82c0, not dumped) — **CONFIRMED — disassembly**, full listing

Takes no arguments. Five blocks in order:

**A. Auto-save indicator.** The counter 0x013effd4 (incremented/decremented/zeroed by 0x0059faf0,
read by 0x0059fb20 — neither dumped) drives a fifth stamp 0x012e6abc:
- counter > 0 and the stamp not set: stamp := now + 3000 ms and call
  `screen_fade_auto_save_show()` (UI state, document context 0x013effc0, no arguments).
- counter ≤ 0 and the stamp set and reached: call `screen_fade_auto_save_hide()` and reset the
  stamp to -1.
So the auto-save icon stays up at least 3 s. **HYPOTHESIS** for the counter's meaning (it is only
named by what it does here).

**B. Mode gate.** 0x00706ab0 returns the top of the mode stack 0x01503b50 indexed by 0x012f4a80
(0 when the index is -1). If it is **4**, all four fade stamps (0x012e6aac/ab0/ab4/ab8) are reset
to -1 and the routine returns. **OPEN**: what mode 4 is (0x00706be0 pushes/pops the stack).

**C. Loading-logo step.** If the "logo due" stamp 0x012e6aac is set and reached, and the cutscene
state 0x0153b520 is **not** in 10..13 (0x007205d0):
- 0x012e6aac := -1; the fade-in hold 0x012e6ab0 := now + 1000 ms; if the load-images byte
  0x0149365c is set, the "images due" stamp 0x012e6ab4 := now + 6000 ms; 0x012e6ab8 := -1.
- Unless one of the ids 0x35, 0x36, 0x37 is active in the set object 0x012fced8 (0x007b3ba0 is a
  membership test: find the element whose +0x30 equals the id, then check that element is in the
  active list at +4), post the id 0x4c0db2a9 through 0x0045d990 (**HYPOTHESIS**: a sound event,
  as in nzxf).
- Call `screen_fade_logo_show()` and return.

**D. Holds and the load-images step.** Otherwise: if 0x012e6ab0 is set and not reached → return.
If 0x012e6ab4 is set and reached → 0x012e6ab4 := -1, the second hold 0x012e6ab8 := now + 1500 ms,
call `screen_fade_images_show()` and return. If 0x012e6ab8 is set and not reached → return.

**E. Re-issue of a deferred request.** If the target differs from the state and the state is
settled at the opposite end: target 2 with state 3 → call the fade-in helper
0x0059fc40(**250**, deferred callback 0x013effd0, flag 0x013effc8); target 3 with state 2 → the
fade-out helper 0x0059f8c0(250, deferred, flag). While a transition is running (state 0 or 1)
nothing is re-issued.

Points the listing settles:
- A **deferred request is replayed with a fixed 250 ms duration**, not the duration originally
  asked for, and with the parked callback as its completion callback. (After that replay the
  in-flight and deferred slots hold the same function; §1.2 step 3 is what keeps it from being
  called twice.)
- The four stamps mean: 0x012e6aac "show the loading logo at" (set on fade-out completion,
  1 s later); 0x012e6ab0 "hold fade-in until" (1 s after the logo); 0x012e6ab4 "show the load
  images at" (6 s after the logo, only when `sfx_use_load_images` is true); 0x012e6ab8 "hold
  fade-in until" (1.5 s after the images). A fade-in request clears 0x012e6aac and 0x012e6ab4
  when it starts (nzxf §2.3), so a fade-in that is requested within 1 s of a fade-out completing
  starts at once and no logo is shown.
- 0x0149365c is written by 0x005d1a30 (not dumped) and read by many loading-screen routines; it is
  the `sfx_use_load_images` value. **HIGH CONFIDENCE**: a "show load-screen images" preference.
- 0x013effc5 (opcode-0x53 direction byte) is cleared by 0x0059fa20, a one-instruction helper; no
  caller was dumped.

### 2.4 Siblings — **CONFIRMED — disassembly**

- 0x0059f9c0: returns 0.0 when the state is 2, else 1.0 (float; a "current overlay alpha" query).
- 0x0059f9f0: returns the target 0x012e6aa8.
- 0x00e0cba0: the call dispatcher behind 0x00e0cd00 (a builder whose +0x18 bit 0x10 is set is
  skipped; hooks at 0x02a44d60/0x02a44d64 can veto; the builder's function is run through
  0x00e00080). Nothing fade-specific.

### 2.5 Host summary (replaces nzxf §2.7 where they differ)

- Completion: `Screen_fade_transition_complete()` registered in the UI state, no arguments, no
  results, body as §1.2.
- Per frame, in this order: mode gate; logo/images schedule (only matters when the UI scripts
  exist); and the re-issue rule of block E with 250 ms.
- A host that does not run the UI scripts should still call the §1.2 logic itself after the
  requested `durationMs`, as nzxf's host-side substitute says; but the deferred direction is then
  started by the per-frame rule with 250 ms, not by the completion step.

---

## 3. zscene: where the 0xf8-stride table comes from

**CONFIRMED — disassembly** (listings of 0x00723d60, 0x00723e40, 0x007231e0, 0x0072d330 and the
reference lists of 0x0153b294/0x0153b29c):

- **0x00723d60(capacity)** allocates the table: capacity is capped at 200, the block (4 + 200×0xf8
  bytes at most) comes from the heap object 0x01493a28 through 0x00dad360, each entry is run
  through the element constructor 0x007233c0, and then count 0x0153b29c := 0, capacity
  0x0153b298 := capacity, base 0x0153b294 := block + 4. It does nothing if a table already exists.
- **0x00723e40(name)** appends one entry: refuses (returns null) when count + 1 > capacity; else
  the new entry's +0 is a heap copy of the name (0x00a74910 on the same heap) and its +0x4 the
  CRC-32 of the name (0x00d9e8b0 with seed 0, no length cap); returns the entry. So +0x0 is the
  **name string** and +0x4 the hash nzxf already described.
- Both are called **only by 0x0073bfb0**, and the cutscene init 0x0072d330 (one caller,
  0x0084e3b0) calls 0x0073bfb0 with the literal **"cutscene.xtbl"**, the container name
  "cutscene_containers" and the element name "main", after mounting **"cutscene_tables.vpp"** and
  creating the "cutscene string pool" (0x64000 bytes). A second call at 0x0072d51e handles per-
  container patch tables (names formed with "%s_%s" from the list at 0x0153ad90).
- **0x007231e0 is the table's destructor, not a writer of entries**: it tears the current scene
  down (0x00721c20(1,0,0)), clears current and state, releases every entry's +0xc and +0x10
  handles (0x00dafad0 when live, then 0x00db37c0), zeroes count, capacity and base, and also
  clears the cutscene manager 0x0153b528, its state 0x0153b520 and the object 0x0153b53c (through
  0x008798e0). Its caller 0x00707170 was not dumped (**HYPOTHESIS**: world/level unload).

So the scene table is the parsed **`cutscene.xtbl`** (from `cutscene_tables.vpp`), one 0xf8-byte
entry per element, and "zscene" entries are cutscene-table entries. **OPEN**: the per-field parse
inside 0x0073bfb0 (which element gives kind +0x8 = 1, the handles +0xc/+0x10, the lightset
+0xac/+0xad and the stream handle +0xf4).

**0x0153b556 is the "skip_all_cutscenes" option.** **CONFIRMED — disassembly**: 0x0072d330
registers the address 0x0153b556 under the literal "skip_all_cutscenes" through 0x0086d770; the
command handler 0x0072df50 (reached from the table at 0x0088f230) writes it before calling
0x00725df0. nzxf's "bypass toggle" HYPOTHESIS closes: prep refuses, `zscene_is_loaded` reports
true, and 0x00725df0 does nothing, while cutscenes are skipped.

---

## 4. zscene: promotion, completion, drivers

### 4.1 Promotion 0x00720410 (pending → current, state := 1) — **CONFIRMED — disassembly**

Only caller: **0x007258a0** (not dumped). Body:
1. Returns if nothing is pending (0x0153b538 == 0) or if 0x007316a0(1) is false. That helper is
   true when no transition stream handle 0x0153b71c exists, or when the stream's status (0x00463db0
   on the pool 0x0153b730) is 100, in which case the handle is cleared.
2. Selects the pending entry's live handle (0x0071fee0: +0xc when live, else +0x10 when live or
   when the local player's byte +0xa41 is 1, else +0xc) and hands it to **0x00dafea0**, the handle
   manager's "start loading" routine: on the owning thread (0x01329ea4) only, and not for handles
   with flag 0x40 at +0x1e, it locks (0x00db1940) and either resumes (flag 0x10 → 0x00db1760) or
   runs 0x00dafc50 when +0x34 is set, 0x00daf040 when flag 4 is set, and 0x00daf120 when neither
   flag 1 nor flag 2 is set. **HIGH CONFIDENCE** for the label "start/resume the resource load".
3. 0x007315a0(entry +0xf4, 1): when that handle exists and 0x004664a0 says it is not already
   running, it becomes the transition stream 0x0153b71c, is started on the pool 0x0153b730
   (0x004639a0) and a 1000-unit timer 0x0153b724 is taken (0x00dad740). Otherwise 0x0153b71c := 0.
   **HYPOTHESIS**: a transition sound/video stream attached to the entry.
4. The pending entry's +0x18/+0x1c receive 0x0153b568/0x0153b56c (what prep stored); current :=
   pending; pending := 0; 0x0153b568/0x0153b56c := the two `.rdata` constants at
   0x01146600/0x01146604 (0 in the file); **state := 1**.

### 4.2 Completion 0x007285c0 (state 1 → 2) — **CONFIRMED — disassembly**

Only caller: **0x0072d660** (twice; §4.3). Body:
1. With no current scene: if the transition stream is not playing (0x00731780 → 0x0045ed50 on
   0x0153b720) and 0x00731630(1) is true, release it (0x007317a0(1)).
   0x00731630 is true when there is no stream, or its timer 0x0153b724 has run ≥ 5000 ms
   (0x00dad810), or its status is 0x66.
2. State 1: classify the current entry's selected handle with 0x00dafb60. The classes, from the
   flag word at +0x1e: null handle → 1; bit 0x40 → 5; bit 0x10 → 2 (with bit 0x20) or 4; bit 4 →
   0; bit 1 → 3; bit 2 → 2; none → 1.
   - **class 3 and 0x00731630(1) true → state := 2**, and 0x0153b534 := the result of slot +0x5c of
     the service-9 object (0x00dd6450(9) → 0x00db56f0 → virtual +0x5c). **HIGH CONFIDENCE**: class 3
     is "resident"; **HYPOTHESIS**: 0x0153b534 is a world-side object created for the scene.
   - **class 5 → teardown** 0x00721c20(1, 0, 0) (the load failed).
   - any other class: keep waiting.
3. State 0: 0x00728440 (§4.4). State 2: nothing.

So "1 = loading, 2 = loaded" is now **CONFIRMED — disassembly**, and loading completes when the
resource handle reports resident **and** the transition stream (if any) has ended or 5 s passed.

### 4.3 The per-frame driver is the cutscene state machine 0x0072d660

**CONFIRMED — disassembly:** 0x0072d660 (callers 0x00702a50 and an undefined site 0x00bdbc54)
first handles the paused case (0x00707490 true and 0x00721a70 true, cutscene state 10 or 11), then
switches on the cutscene state 0x0153b520 (0..0x13, table at 0x0072defc, contents not in the
dump). Two cases run the zscene step:
- one case tail-jumps to 0x007285c0 and does nothing else;
- another calls 0x007285c0 and, once state == 2, calls 0x00725df0 with the current entry's hash
  (+0x4), the bytes 0x0153b580/0x0153b581 and the block 0x0153b588, then optional loaders
  0x00723ea0/0x007235c0 gated on 0x0153b5c8/0x0153b610.
Other cases write the states 3 ("CS_STATE_FADING_OUT"), 4, 5, 0xf ("CS_STATE_STOP"), 0x11, 0x12
("CS_STATE_STOPPED"), 0x13 ("CS_STATE_FINAL_STREAMING") and back to 0 through 0x008788e0; the
state-0x13 case requests a **fade-in** 0x0059fc40(500 ms or the value at 0x012f5d28, 0, 0/1)
gated on 0x0153b6b4/0x0153b6b5, and frees "cutscene_virtual_pool".
**HIGH CONFIDENCE**: 0x0072d660 is called once per frame (it is a state machine stepper and reads
the clock-driven helpers), and the tail-jump case is the idle (no cutscene) state, so the zscene
load completes while no cutscene is playing. **OPEN**: the jump-table contents.

**0x00725df0 is the cutscene start ("cutscene_play").** **CONFIRMED — disassembly** for what was
read: callers 0x0072df50 (the skip/play command), 0x00728870, 0x0072d660, 0x006db140. It returns
early when the local player's timer at +0x1ef4 is set, or `skip_all_cutscenes` is set, or the hash
resolves to no entry (0x00721be0). For a kind-1 entry it asks 0x007203e0 whether that entry's
selected handle is already the current, loaded scene; if not it writes cutscene state := 2, tears
the current scene down (0x00721c20(1,0,0)) and runs the **prep gate 0x007232e0(entry, 0x01146600,
0x01146604)** — the same two constants `zscene_prep` passes. Later it requests a **fade-out**
0x0059f8c0(500 ms or 0x012f5d28, 0, flag), posts "Mute_During_Zscene", and starts "cutscene_play".
0x007203e0(handle) is true iff the state is ≥ 2 and the current entry's selected handle equals
`handle`.

**Who promotes.** The only promoter caller 0x007258a0 was not dumped; its reference lines show it
calls 0x00720320 twice, 0x007203e0, 0x00722f10, 0x0071fee0, 0x00721ba0 four times and the state
setter 0x008788e0, reads 0x0153b530/0x0153b53c/0x0153b543/0x0153b52c/0x0153b518 and names
"cutscene_virtual_pool". **HIGH CONFIDENCE**: it is the cutscene machine's load step, run from one
of the 0x0072d660 cases or from 0x00725df0's successor. **OPEN**: its body and callers, hence
whether a bare Lua `zscene_prep` (pending set, no cutscene) ever gets promoted without the
cutscene machine. The three nzxf candidates resolve as: 0x00725df0 = cutscene start (calls the
gate, the teardown and the fade-out); 0x00737780 = auto-prep from a world object (§4.4);
0x0072d660 = the stepper that calls completion.

### 4.4 Idle driver 0x00728440 and auto-prep 0x00737780 — **CONFIRMED — disassembly**

When the state is 0 and the byte **0x0153b541** is set, and neither 0x006cecb0() nor 0x00831910()
is true: find the scene-bearing world object nearest to the reference position (0x009dfa90 of
each object's +0x40), starting from the current entry's own object (0x005980f0 on +0x18/+0x1c),
over the list at 0x03171a64 (count +0x2f4, index words +0x2ec, pointers +0x58, filter 0x00737500),
and call **0x00737780** on the winner. That routine, when the object's +0x84 == 1 and the entry at
its +0x7c has kind 1, runs the prep gate 0x007232e0(entry, obj +0x8, obj +0xc) — so the two values
prep stores (0x0153b568/0x0153b56c) are per-object parameters, and from Lua they are the two zero
constants. When 0x0153b541 is clear (or a blocker is true) it runs **0x00720320** instead: if the
current entry's selected handle has class 1 (no handle), 0x0153b541 := (0x0153b542 ≠ 0), state :=
0, the current entry is moved back to pending when 0x0153b542 is set, and current := 0; with no
current entry it sets 0x0153b541 := 1. So 0x0153b541 is an **"auto-select the nearest scene"
enable** (**HIGH CONFIDENCE** label) and 0x0153b542 a "re-queue on reset" flag.

0x00722f10 (callers 0x00725670 and 0x007258a0; only partially read): when the state is 1 it picks
the current entry's live handle, clears 0x0153b541, state := 0 and current := 0 — a **cancel of an
in-flight load** (**HIGH CONFIDENCE**); the string "npc_basehead" appears in its body.

### 4.5 Lifecycle as now settled

1. Startup: 0x0072d330 mounts `cutscene_tables.vpp`, parses `cutscene.xtbl` into the 0xf8 table,
   registers `skip_all_cutscenes` at 0x0153b556.
2. `zscene_prep(name)` or a cutscene start: previous scene torn down (state 0), entry pending.
3. 0x007258a0 (cutscene load step, body OPEN) → 0x00720410: resource load started, transition
   stream started, entry current, **state 1**.
4. Each frame 0x0072d660 → 0x007285c0: handle resident and stream done → **state 2** (+
   0x0153b534); handle failed → teardown.
5. `zscene_is_loaded(name)` true (§14.23 truth table unchanged).
6. Table destroyed by 0x007231e0 on unload.

---

## 5. UI resolution: the display mode 0x0132bd80 and 0x00e236f0

### 5.1 0x00e23000(a, b) — the writer of 0x0132bd80 — **CONFIRMED — disassembly**

Two integer arguments; the quotient a / b is formed in x87 (single precision). The mode starts at
-1 and is raised through a ladder of five strict "greater than" tests, each overriding the last:

| Quotient | Mode |
|---|---|
| ≤ 2.48 | -1 |
| > 2.48 | 0 |
| > 3.18 | 1 |
| > 3.72 | 2 |
| > 4.77 | 3 |
| > 5.58 | 4 |

2.48 is the single at 0x01256908 (bit pattern 0x401eb852, **CONFIRMED**); 3.18, 3.72, 4.77 and
5.58 are the doubles at 0x01256900, 0x012568f8, 0x012568f0, 0x012568e8 as the decompiler renders
them (the listing prints only their low dwords: **HIGH CONFIDENCE** for the exact values). Callers
0x00e23a20 and 0x00e23910 were not dumped; 0x00e230a0 (not dumped) is the only reader besides
`vint_is_std_res`.

**Meaning (HYPOTHESIS, consistent with every threshold):** the quotient is the full render
width/height, and the ladder classifies **multi-monitor spans**: a single display of any aspect
(up to 21:9 = 2.37) stays at -1; two 4:3 panels (2.67) give 0; two 16:10 or 16:9 panels (3.2,
3.56) give 1; three 4:3 or 5:4 panels (4.0, 3.75) give 2; three 16:10 or 16:9 panels (4.8, 5.33)
give 3; anything wider gives 4. Under this reading `vint_is_std_res`'s override "true when the
mode is 2" means "each of the three panels is 4:3, so use the standard-resolution layout" — the
one case where the overall aspect is wide but the per-panel aspect is not. The spec should present
the ladder as data and this meaning as a hypothesis.

### 5.2 0x00e236f0 — what `vint_is_std_res` reads — **CONFIRMED — disassembly**

Returns a per-thread 12-byte record: the thread context (TLS index 0x02cc4700 through FS:[0x2c])
holds a pointer at +0x670; when it is null, one 12-byte element is taken from the pool object
0x02a5a090 under its lock (virtual +0xc/+0x10 around 0x00e23640, which pops an index from a
free-list of 0xc-byte elements at +0x40), zeroed, and stored there. 0x00e237e0 (nzxf) returns
this record + 4, so the two integers `vint_is_std_res` divides are the record's **second and third
dwords**. **OPEN**: who writes them (the accessors 0x00e23770, 0x00e237b0, 0x00e237c0,
0x00e237f0 and the mode-setting callers 0x00e23a20/0x00e23910 were not dumped). **HIGH
CONFIDENCE** (unchanged from nzxf): they are the current UI width and height, and 0x00e23a20 —
which calls both 0x00e236f0 and 0x00e23000 — is the routine that stores a new resolution and
re-classifies the mode.

### 5.3 The safe-frame constants 0x0115ba60 / 0x0116dfc0 — still **OPEN**

The `xref` mode again printed only the low dword of each 8-byte constant (0x40000000 and
0xa0000000). Both are shared literals (0x0115ba60 is also used by 0x007ef720, 0x0116dfc0 by
0x008f4fd0 and 0x00903140), so they are generic values, not fade- or UI-specific. The low dwords
are those of single-precision literals widened to double: 0xa0000000 matches 0.1f (and also 0.05f,
0.925f); 0x40000000 matches 0.85f, 0.15f and 0.075f among the plausible safe-frame fractions. No
value can be chosen from this dump; the script needs a raw qword read (§7).

---

## 6. Comparison with the current spec text

| Section | Verdict | Detail |
|---|---|---|
| §26.24 globals table | confirms; extends | All rows hold. New: 0x012e6abc (auto-save stamp, 0x0059fe70 only), 0x013effd4 (auto-save counter, 0x0059faf0/0x0059fb20), 0x0149365c (`sfx_use_load_images` byte). Role of 0x012e6aa0 and 0x013effc0 now labelled (document id / document handle). |
| §26.24 "What completes a fade" | confirms; corrects | CONFIRMED now, name `Screen_fade_transition_complete`. Corrections: the native flips by current state (0→2, 1→3) rather than "sets the state to the target"; callbacks get the target; it never starts the deferred transition — 0x0059fe70 replays it with 250 ms. |
| §26.24 HYPOTHESIS on 0x0059fe70 | confirms with detail | It is the per-frame update; the replay happens only when the state is settled at the opposite end and both holds have passed; it also runs the loading-logo/load-images/auto-save script calls. |
| §26.24 host substitute | keep; amend | The substitute completes the transition; the deferred direction must then be started by the per-frame rule (250 ms), not inside the completion step. |
| §26.25 globals | confirms; relabels | 0x0153b556 = `skip_all_cutscenes`; 0x0153b541 = auto-select enable; 0x0153b534 = service-9 object made at completion; 0x0153b568/0x0153b56c = per-object parameters (zero from Lua); table entry +0x0 = name string; 0x0153b298 = capacity. 0x007231e0 is the destructor, not a filler. |
| §26.25 state codes | confirms | 1 = loading and 0 = idle move to CONFIRMED. |
| §26.25 lifecycle step 3 | answers; narrows | Promotion caller: 0x007258a0 (body OPEN). Completion caller: the cutscene stepper 0x0072d660. Candidate roles resolved. |
| §26.25 lifecycle step 5 | answers | Table = parsed `cutscene.xtbl` from `cutscene_tables.vpp` via 0x0073bfb0; field parse OPEN. |
| §26.26 `vint_is_std_res` | confirms; extends | Mode ladder of 0x00e23000 added; 0x00e236f0 record described; width/height writers OPEN. |
| §26.26 `vint_get_safe_frame` | unchanged | Constants still OPEN; dump mode limitation noted. |
| §8.21 / §14.23 | unchanged | Nothing here contradicts the nzxf corrections. |
| `interp_nzxf.md` §2.5 | confirms | `sfx_faded_in` = 0x0059fb60. |

---

## 7. Next dump (addresses; nothing above depends on them)

- **0x007258a0** (`func`, depth 1): the only promoter caller; settles who promotes a bare Lua
  `zscene_prep`. Also `xref 0x007258a0` for its callers.
- **0x0073bfb0** (`func`, depth 1, maxinsn high): the `cutscene.xtbl` parser; gives the element
  names behind kind +0x8, handles +0xc/+0x10, lightset +0xac/+0xad, stream +0xf4.
- **0x0072d660 jump table**: the 20 dwords at 0x0072defc (a raw data read; `xref` does not print
  them) to name the two cases that run 0x007285c0.
- **Fade callers**: `func 0x005d14b0 0x007a82c0` (per-frame callers), `func 0x005d2400 0x005d1a30`
  (init caller; writer of 0x0149365c), `func 0x0059faf0 0x0059fb20` (auto-save counter),
  `func 0x00706be0` (mode stack; what mode 4 is).
- **UI**: `func 0x00e23a20 0x00e23910 0x00e230a0 0x00e23770 0x00e237b0 0x00e237c0 0x00e237f0`
  (resolution writers/readers behind 0x0132bd80 and the per-thread record).
- **Safe-frame constants**: the script needs a data-read mode (8 bytes at 0x0115ba60 and
  0x0116dfc0, and the doubles at 0x01256900/0x012568f8/0x012568f0/0x012568e8); `xref` prints
  only the first dword of each.
- Unchanged from nzxf: `func 0x0045d990 0x0045ea70` (audio-id posts) and 0x00707170 (caller of the
  table destructor).

---

## 8. Spec changes (exact text for Team A to apply; wording is final)

### 8.1 §26.24 Screen fade state machine

**Source line, replace** "Source: bridge job `20261001T020200-team-a-nzxf` (" **with**
"Source: bridge jobs `20261001T020200-team-a-nzxf` and `20261001T114101-team-a-mnao` (".

**Globals table, replace the row** for 0x012e6aa0 **with**:
`| 0x012e6aa0 | -1 | id of the loaded "screen_fade" UI document record (0x007b1cb0); while it is -1 no Lua call is made | 0x0059fa30 only |`

**Replace the row** for 0x013effc0 **with**:
`| 0x013effc0 | 0 | handle of the "screen_fade" UI document (its +0x580, found by 0x00e1f2f0); placed in the Lua call builder so the calls run in that document | 0x0059fa30 (set), 0x0059faa0 (cleared) |`

**Replace the row** for 0x012e6ab0, 0x012e6ab8 **with**:
`| 0x012e6ab0, 0x012e6ab8 | -1, -1 | "hold fade-in until" stamps: now + 1000 ms when the loading logo is shown, now + 1500 ms when the load images are shown | 0x0059fe70, completion native (reset) |`

**Replace the row** for 0x012e6aac, 0x012e6ab4 **with**:
`| 0x012e6aac, 0x012e6ab4 | -1, -1 | "show the loading logo at" (fade-out completion + 1000 ms) and "show the load images at" (logo + 6000 ms, only when 0x0149365c is set); both reset to -1 when a fade-in starts | 0x0059fc40, 0x0059fe70, completion native |`

**Add three rows** after the 0x013effc5 row:
`| 0x012e6abc | -1 | auto-save indicator stamp: now + 3000 ms when the indicator is shown | 0x0059fe70 |`
`| 0x013effd4 | 0 | auto-save counter; > 0 shows the indicator | 0x0059faf0 (inc/dec/zero), read by 0x0059fb20 |`
`| 0x0149365c | 0 | byte: show load-screen images (`sfx_use_load_images`) | 0x005d1a30 |`

**Queries paragraph, replace** "Queries: `fade_is_fully_faded_out` and `sfx_faded_out` are true
when the state is 3; `fade_is_fully_faded_in` when it is 2 (§26.9). **CONFIRMED.**" **with**:
"Queries: `fade_is_fully_faded_out` and `sfx_faded_out` are true when the state is 3;
`fade_is_fully_faded_in` and `sfx_faded_in` (0x0059fb60) when it is 2 (§26.9). `sfx_use_load_images`
(0x0059fb90) returns the byte 0x0149365c. All four UI-state natives are registered by 0x005a01d0.
**CONFIRMED — disassembly.**"

**Replace the whole "What completes a fade." block** (from "**What completes a fade.**" to the end
of the HYPOTHESIS bullet about 0x0059fe70) **with**:

"**What completes a fade: `Screen_fade_transition_complete` (0x005a0110).** **CONFIRMED —
disassembly, full listing.** The UI wrapper 0x005a01d0 registers this native in the UI Lua state,
next to `sfx_faded_out`, `sfx_faded_in` and `sfx_use_load_images`. It takes no arguments and
returns nothing. Apart from init/shutdown it holds the only stores of 2 and 3 into the state, so a
fade completes exactly when the UI script calls it (HIGH CONFIDENCE: when `screen_fade_do`'s
tween ends). Its body:
1. If the state is 0 it becomes 2. If the state is 1 it becomes 3, the logo stamp 0x012e6aac is set
   to now + 1000 ms and 0x012e6ab0/0x012e6ab4/0x012e6ab8 are reset to -1. A state of 2 or 3 is
   left alone. The target is not consulted here.
2. If the in-flight callback 0x013effcc is set it is called with the **target** (2 or 3) as its
   only argument; the slot is re-read afterwards.
3. If the state now equals the target and the deferred slot 0x013effd0 is set: when it holds the
   same function as the in-flight slot, both are cleared (the function has just been called) and
   the native returns; otherwise the deferred function is called with the target and the slot is
   cleared.
4. The in-flight slot is cleared (and the deferred slot too when the two were equal).
The native never starts a transition. A request that was parked (target differs from the settled
state) is replayed by the per-frame routine below.

**Per-frame routine 0x0059fe70** (callers 0x005d14b0 and 0x007a82c0). **CONFIRMED — disassembly,
full listing.** Each frame, in this order:
- Auto-save indicator: counter 0x013effd4 > 0 and stamp 0x012e6abc unset → stamp := now + 3000 ms
  and call `screen_fade_auto_save_show()`; counter ≤ 0 and stamp reached → call
  `screen_fade_auto_save_hide()` and reset the stamp.
- Mode gate: if the top of the mode stack (0x00706ab0: 0x01503b50 indexed by 0x012f4a80) is 4,
  reset all four fade stamps and stop (OPEN: what mode 4 is).
- Loading logo: if 0x012e6aac is set and reached and the cutscene state 0x0153b520 is not 10..13:
  reset 0x012e6aac, 0x012e6ab0 := now + 1000 ms, 0x012e6ab4 := now + 6000 ms when 0x0149365c is
  set, 0x012e6ab8 := -1; unless one of the ids 0x35/0x36/0x37 is active in the set at 0x012fced8
  (0x007b3ba0), post the id 0x4c0db2a9 through 0x0045d990 (HYPOTHESIS: a sound); call
  `screen_fade_logo_show()`; stop.
- Holds: if 0x012e6ab0 is set and not reached, stop. If 0x012e6ab4 is set and reached: reset it,
  0x012e6ab8 := now + 1500 ms, call `screen_fade_images_show()`, stop. If 0x012e6ab8 is set and
  not reached, stop.
- Replay: if the target is 2 and the state is 3, call the fade-in helper 0x0059fc40(250,
  deferred callback, flag 0x013effc8); if the target is 3 and the state is 2, call the fade-out
  helper 0x0059f8c0(250, deferred callback, flag). A parked request is therefore replayed with a
  fixed 250 ms duration, and only once the state has settled at the opposite end and the holds
  have passed.
All script calls go to the UI Lua state in the context of the `screen_fade` document (0x013effc0)
and take no arguments.

**Init 0x0059fa30 / shutdown 0x0059faa0.** **CONFIRMED — disassembly.** Init loads the named UI
document "screen_fade" (0x007b1cb0, mode 1; its id goes to 0x012e6aa0, -1 on failure), looks the
document up (0x00e1f2f0) and, if found, stores its handle in 0x013effc0 and sets state := 2,
target := 2, flag := 1. Shutdown destroys the document (0x00e1f330 → 0x00e20f60), clears
0x013effc0, unloads the record (0x007b1420) and sets state and target to 2."

**Host summary, replace the bullet** "A request only flips the state to 1 (out) or 0 (in) and
calls `screen_fade_do(flag, alpha, ms)` in the UI Lua state, and only if that global is a
function. If it is not defined, the real engine never completes the fade either, so a script that
busy-polls `fade_is_fully_faded_out()` never returns." **with**:
"A request only flips the state to 1 (out) or 0 (in) and calls `screen_fade_do(flag, alpha, ms)`
in the UI Lua state, and only if that global is a function. Completion is the script's call of
`Screen_fade_transition_complete()`. If `screen_fade_do` is not defined, the real engine never
completes the fade either, so a script that busy-polls `fade_is_fully_faded_out()` never returns.
A request parked while the opposite transition ran is replayed by the per-frame routine with
250 ms once the state has settled."

**HOST-SIDE SUBSTITUTE paragraph, replace** "It sets the state to the target, calls and clears the
in-flight callback, and then issues the deferred request if the deferred slot is set and the
target differs from the new state. The engine does none of this in C; this only stands in for the
UI script's call to the native at 0x005a0110." **with**:
"It then runs the body of `Screen_fade_transition_complete` (flip 0→2 or 1→3, call the in-flight
callback with the target, handle the deferred slot as described) and, on a later frame, the
per-frame replay rule (250 ms toward the target once the state is settled). Only the timer is a
substitute; the two bodies are engine behaviour."

**OPEN — next dump block, replace** its three bullets **with**:
"- `func 0x005d14b0 0x007a82c0 0x005d2400 0x005d1a30 0x0059faf0 0x0059fb20 0x00706be0` (callers
  of the per-frame routine and init; the load-images byte; the auto-save counter; the mode stack).
- Audio-id posts (HYPOTHESIS check): `func 0x0045d990 0x0045ea70`."

**Review status, replace** the whole paragraph **with**:
"**Review status (2026-10-01): re-derived from the executable (jobs `20261001T020200-team-a-nzxf`,
`20261001T114101-team-a-mnao`): CONFIRMED parts — globals and initial values, state encoding, both
request helpers, both broadcast helpers, the four UI natives including
`Screen_fade_transition_complete`, the completion body, the per-frame routine, init/shutdown;
HIGH CONFIDENCE — the document-id/handle labels of 0x012e6aa0/0x013effc0, the co-op purpose of
0x53, the script calling the completion native; HYPOTHESIS — audio-id posts, the auto-save
counter's meaning; OPEN — mode 4 of the mode stack, the callers of the per-frame routine.**"

### 8.2 §26.25 zscene lifecycle

**Source line, replace** "Source: bridge job `20261001T020200-team-a-nzxf` (" **with**
"Source: bridge jobs `20261001T020200-team-a-nzxf` and `20261001T114101-team-a-mnao` (".

**Globals table, replace the row** for 0x0153b294 / 0x0153b29c **with**:
`| 0x0153b294 / 0x0153b29c / 0x0153b298 | scene table base / count / capacity (≤ 200); entries 0xf8 bytes, parsed from cutscene.xtbl | 0x00723d60 (allocate), 0x00723e40 (append), 0x007231e0 (destroy) |`

**Replace the row** for 0x0153b556 **with**:
`| 0x0153b556 | byte: the "skip_all_cutscenes" option (registered by 0x0072d330 through 0x0086d770); when set, prep refuses, zscene_is_loaded reports true and cutscene starts do nothing | 0x0072df50 (the skip/play command) |`

**Replace the row** for 0x0153b568 / 0x0153b56c **with**:
`| 0x0153b568 / 0x0153b56c | per-object parameters passed to prep (object +0x8/+0xc by the auto-prep path, the zero constants 0x01146600/0x01146604 from Lua and cutscene starts); copied into the entry's +0x18/+0x1c at promotion | 0x007232e0; 0x00720410 |`

**Replace the row** for 0x0153b534 **with**:
`| 0x0153b534 | object obtained from slot +0x5c of the service-9 object when the load completes (HYPOTHESIS: a world-side object for the scene); released on teardown | 0x007285c0 (set), 0x00721c20 (cleared) |`

**Replace the row** for 0x0153b541 / 0x0153b542 **with**:
`| 0x0153b541 / 0x0153b542 | bytes: "auto-select the nearest scene when idle" enable, and "re-queue the current entry on reset" | 0x00721c20, 0x00720320, 0x00722f10 |`

**Add a row** at the end of the table:
`| 0x0153b71c / 0x0153b720 / 0x0153b724 | transition stream started at promotion from the entry's +0xf4 handle, its secondary handle, and its 1000-unit timer; completion waits until it ends (status 0x66) or 5 s pass | 0x007315a0, 0x007317a0, 0x007316a0 |`

**State-code paragraph, replace** "State code: 2 = loaded is **CONFIRMED** (the value tested).
1 = loading and 0 = idle are **HIGH CONFIDENCE**: they are read from which routines write each
value, but the bodies of 0x00720410 and 0x007285c0 were not dumped." **with**:
"State code: 0 = idle, 1 = loading (written by the promotion 0x00720410 when it starts the resource
load), 2 = loaded (written by 0x007285c0 when the handle is resident). **CONFIRMED —
disassembly.**"

**Scene entry list, add as the first bullet**: "- `+0x0`: pointer to the entry's name (a heap copy
made by 0x00723e40)." **and add after the `+0xad` bullet**: "- `+0xf4`: handle of a transition
stream started at promotion (HYPOTHESIS: a sound or video played over the load)."
"- `+0x18` / `+0x1c`: the two per-object parameters copied in at promotion."

**Lifecycle, replace step 3** **with**:
"3. Promotion from pending to current is 0x00720410 (**CONFIRMED — disassembly**): it waits
   until any previous transition stream has finished (0x007316a0), selects the pending entry's
   live handle (+0xc, else +0x10 by the variant rule) and starts its load through the handle
   manager's 0x00dafea0, starts the entry's +0xf4 stream (0x007315a0), copies
   0x0153b568/0x0153b56c into the entry's +0x18/+0x1c, makes the entry current, clears the
   pending slot and writes state := 1. Its only caller is 0x007258a0 (not dumped; HIGH
   CONFIDENCE: the cutscene machine's load step; OPEN: its body and callers).
   The load completes in 0x007285c0 (**CONFIRMED — disassembly**), called only by the cutscene
   state machine 0x0072d660 (per frame, in two of its state cases): when the state is 1 it
   classifies the current entry's selected handle (0x00dafb60); class 3 (resident) together with a
   finished or 5-second-old transition stream gives state := 2 and 0x0153b534; class 5 (failed)
   runs the teardown 0x00721c20(1, 0, 0); other classes wait. When the state is 0 it runs the idle
   driver 0x00728440: with 0x0153b541 set it finds the nearest scene-bearing world object (list
   at 0x03171a64) and preps that object's entry through 0x00737780 → 0x007232e0(entry, obj +0x8,
   obj +0xc); otherwise it runs the reset 0x00720320. Cutscene starts (0x00725df0, "cutscene_play")
   use the same prep gate with the zero constants and then request a 500 ms fade-out; the
   cutscene end state requests a 500 ms fade-in."

**Replace step 5** **with**:
"5. The scene table is the parsed **`cutscene.xtbl`** (inside `cutscene_tables.vpp`): the cutscene
   init 0x0072d330 hands that file, the container name "cutscene_containers" and the element name
   "main" to the parser 0x0073bfb0, which is the only caller of the allocator 0x00723d60 (capacity
   capped at 200) and the appender 0x00723e40 (name copy at +0x0, CRC at +0x4). **CONFIRMED —
   disassembly** for the call chain; **OPEN**: the per-field parse (kind, handles, lightset,
   stream). 0x007231e0 is the table's destructor (releases every entry's handles and clears the
   cutscene manager), called from 0x00707170 (not dumped)."

**Host summary, add a bullet**:
"- A host that loads scenes itself may promote a pending entry at once (state 1) and mark it loaded
  (state 2) when its resources are in; the engine does this only from the cutscene state machine,
  whose load step (0x007258a0) is not yet read."

**OPEN — next dump block, replace** its two bullets **with**:
"- `func 0x007258a0 0x0073bfb0 0x00707170`; `xref 0x007258a0`; the 20 jump-table dwords at
  0x0072defc (raw data read)."

**Review status, replace** the whole paragraph **with**:
"**Review status (2026-10-01): re-derived from the executable (jobs `20261001T020200-team-a-nzxf`,
`20261001T114101-team-a-mnao`): CONFIRMED parts — prep gate, stub, teardown, pending slot, entry
offsets, `zscene_is_loaded` truth table, all three state codes, promotion and completion bodies,
the `skip_all_cutscenes` byte, the table's allocation/append/destroy and its source file
`cutscene.xtbl`; HIGH CONFIDENCE — the labels of 0x0153b541/0x0153b542, the resident/failed handle
classes, the cutscene machine as the per-frame driver; HYPOTHESIS — gender variant, cutscene
guard, the transition stream's nature, 0x0153b534's nature; OPEN — the promoter's caller
0x007258a0, the xtbl field parse.**"

### 8.3 §26.26 UI resolution queries

**Source sentence, replace** "this unit adds the behaviour from bridge job
`20261001T020200-team-a-nzxf`." **with** "this unit adds the behaviour from bridge jobs
`20261001T020200-team-a-nzxf` and `20261001T114101-team-a-mnao`."

**`vint_is_std_res` bullets, replace** "- 0x0132bd80 starts at -1 in the file. Its only writer is
0x00e23000, which stores -1, 0, 1, 2, 3 or 4." **with**:
"- 0x0132bd80 starts at -1 in the file. Its only writer is 0x00e23000(a, b) (**CONFIRMED —
  disassembly**), which forms a / b in single precision and sets the mode to -1, then raises it
  through strict tests: > 2.48 → 0, > 3.18 → 1, > 3.72 → 2, > 4.77 → 3, > 5.58 → 4 (2.48 is the
  single at 0x01256908; the four doubles at 0x01256900/0x012568f8/0x012568f0/0x012568e8 are
  HIGH CONFIDENCE as rendered). Its callers 0x00e23a20 and 0x00e23910 were not dumped.
- 0x00e236f0 returns a per-thread 12-byte record (thread context +0x670, allocated on first use
  from the pool at 0x02a5a090 and zeroed); the two integers are the record's second and third
  dwords (**CONFIRMED — disassembly**). Their writers were not dumped."

**Reading paragraph, replace** "Display mode 2 at 0x0132bd80 forces the standard layout whatever
the aspect ratio. Host: true when `width / height < 1.5` or the display mode is 2; with no
display-mode concept, just the aspect test." **with**:
"**HYPOTHESIS** for the mode: the thresholds classify multi-monitor spans of the full render
surface (a single display of any aspect stays at -1; two 4:3 panels → 0; two 16:10/16:9 panels →
1; three 4:3 or 5:4 panels → 2; three 16:10/16:9 panels → 3; wider → 4), so mode 2 — three 4:3
panels — is the one wide span whose per-panel aspect is standard, which is why it forces the
standard layout. Host: true when `width / height < 1.5` or the display mode is 2; with no
display-mode concept, just the aspect test."

**`vint_get_safe_frame` paragraph, replace** "**OPEN**: the two constants; only their low dwords
appear in the dump, and 0x0116dfc0's matches a widened 0.1f." **with**: "**OPEN**: the two
constants; both dumps print only their low dwords (0x40000000 and 0xa0000000, the patterns of
single-precision literals widened to double — 0xa0000000 fits 0.1f, 0x40000000 fits 0.85f, 0.15f or
0.075f), and both are shared literals used by unrelated routines, so a raw 8-byte read is needed."

**OPEN — next dump block, replace** its two bullets **with**:
"- `func 0x00e23a20 0x00e23910 0x00e230a0 0x00e23770 0x00e237b0 0x00e237c0 0x00e237f0` (who writes
  the width/height record and sets the mode).
- A raw 8-byte read of 0x0115ba60 and 0x0116dfc0 (the `xref` mode prints only the first dword)."

**Review status, replace** the whole paragraph **with**:
"**Review status (2026-10-01): re-derived from the executable (jobs `20261001T020200-team-a-nzxf`,
`20261001T114101-team-a-mnao`): CONFIRMED parts — both argument/return shapes, the
`vint_is_std_res` decision rule, the mode ladder of 0x00e23000, the per-thread record of 0x00e236f0,
the `vint_get_safe_frame` data path; HIGH CONFIDENCE — width/height reading, output order, the
four double thresholds; HYPOTHESIS — multi-monitor meaning of the mode, safe-frame edge meaning;
OPEN — the record's writers, the two scale constants.**"
