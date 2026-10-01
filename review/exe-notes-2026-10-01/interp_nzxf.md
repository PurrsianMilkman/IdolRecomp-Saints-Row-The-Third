# Interpretation of bridge dumps `20261001T020200-team-a-nzxf` (mission blockers)

Team A, 2026-10-01. Source: CrreishDump output for `team-a/ghidra/jobs/mission-blockers.json`
(modes `lua`, `func`, `xref`, `str`; the `lua` step ran at callee depth 1, not the requested 2, because
the `depth=2`/`maxfuncs=30` options were taken as name strings; nothing below needed depth 2).
Answers Team B's HANDOFF "Requests to Team A" items 1 (screen fade), 2 (zscene lifecycle, §14.23
inversion) and 4 (`vint_is_std_res`); `vint_get_safe_frame` (item 5) is covered briefly at the end.

Labels: **CONFIRMED** = read in these dumps' listings; **HIGH CONFIDENCE** = follows from a dumped
instruction or reference list but the surrounding body was not dumped; **HYPOTHESIS** = plausible
reading, not settled; **OPEN** = not settled, see the next-dump list (§8).

Shared Lua primitives cited by address, already established by the spec's front matter and used
unchanged here: `lua_gettop` 0x00dfde50, `lua_type` 0x00dfe040, `lua_tolstring` 0x00dfe210,
`lua_tonumber` 0x00dfe160, round-to-int 0x00ea2596, `lua_pushnumber` 0x00dfe3a0, `lua_gettable`
0x00dfe5e0, `lua_settop` 0x00dfde60, `lua_pushboolean` 0x00dfe590, CRC-32 name hash 0x00d9e8b0.

---

## 1. Screen fade: the globals and the state encoding

All six globals Team B asked about, plus five more the dumps show in the same cluster. Initial
values are the file-backed `.data` values (what the executable starts with). **CONFIRMED** for every
row (the `xref` dumps list every reader and writer in the binary).

| Address | Initial | Meaning | Writers |
|---|---|---|---|
| 0x012e6aa4 | 2 | **fade state** (see encoding below) | 0x0059f8c0 (:=1), 0x0059fc40 (:=0), 0x0059fa30 (register value), 0x0059faa0 (register value), undefined code at 0x005a0136 (:=3) and 0x005a0159 (:=2) |
| 0x012e6aa8 | 2 | **requested target** state: 3 = out, 2 = in | 0x0059f8c0 (:=3), 0x0059fc40 (:=2), 0x0059fa30, 0x0059faa0 |
| 0x012e6aa0 | -1 | a handle established at init; while it is -1 no Lua callback is dispatched | 0x0059fa30 only |
| 0x013effc8 | 0 (init writes 1) | the third argument of the last request (a flag passed through to Lua, see §2.3) | 0x0059f8c0, 0x0059fc40, 0x0059fa30 (:=1) |
| 0x013effcc | 0 | completion callback of the transition in flight | 0x0059f8c0, 0x0059fc40, undefined code 0x005a01bd |
| 0x013effd0 | 0 | **deferred** callback: a request that could not start yet | 0x0059f8c0, 0x0059fc40, undefined code 0x005a01a8/0x005a01b1 (:=0) |
| 0x013effc0 | 0 | object handle stored into the Lua call builder (+0x14) before `screen_fade_do` is called | 0x0059fa30 (set), 0x0059faa0 (cleared) |
| 0x012e6ab0, 0x012e6ab8 | -1, -1 | two "hold until" timestamps that block a fade-in request | 0x0059fe70, undefined code 0x005a014d/0x005a0152 |
| 0x012e6aac, 0x012e6ab4 | -1, -1 | two companion timestamps, reset to -1 when a fade-in starts | 0x0059fc40, 0x0059fe70, undefined code 0x005a0148 |
| 0x013effc5 | 0 | byte: last opcode-0x53 broadcast was a fade-out (1) or fade-in (0) | 0x005a0270 (:=1), 0x005a0400 (:=0), 0x0059fa20 (:=0) |

**State encoding of 0x012e6aa4** — **CONFIRMED** from the four readers/writers in the dumps:

| Value | Meaning | Evidence |
|---|---|---|
| 0 | fading **in** (transition toward transparent is running) | 0x0059fc40 writes 0 right after dispatching the fade-to-0.0 call; 0x0059f8c0 treats 0 as "cannot start a fade-out yet" |
| 1 | fading **out** (transition toward opaque is running) | 0x0059f8c0 writes 1 after dispatching the fade-to-1.0 call; 0x0059fc40 treats 1 as "cannot start a fade-in yet" |
| 2 | **fully faded in** (screen visible; the start-up state) | file-backed initial value; `fade_is_fully_faded_in` and 0x0059fb60 test == 2; 0x0059fc40 treats 2 with target 2 as "already done" |
| 3 | **fully faded out** (screen covered) | `fade_is_fully_faded_out` and `sfx_faded_out` test == 3; 0x0059f8c0 treats 3 with target 3 as "already done" |

---

## 2. Screen fade: the functions

### 2.1 `fade_out` — 0x00a49dc0 (gameplay registrar 0x00a20840)

**Arguments** (**CONFIRMED**): arg 1 number, read unconditionally via `lua_tonumber(-n)` (duration in
seconds, kept as a float). Arg 2 optional table: present when `n >= 2` and `lua_type` of arg 2 is not
nil; keys `1`, `2`, `3` are fetched with pushnumber/gettable, each converted with `lua_tonumber` if
non-nil, else 0.0; absent table means 0, 0, 0. Arg 3 optional number: present when `n >= 3` and not
nil, rounded with 0x00ea2596; default 3. **Return** 0 values.

**Body** (**CONFIRMED**): the three components are truncated to integers and passed with alpha 255
to 0x005d9120, which scales all four by 1/255 (constant at 0x012a2e48) and hands the four floats to
0x00e4ae90 on the object at 0x0351f91c (the fade overlay colour). Then: bit 1 of the flags calls
the **fade-out request helper 0x0059f8c0** with `(int(duration * 1000.0), 0, 0)`; bit 2 calls the
**broadcast helper 0x005a0270** with `(int(duration * 1000.0), 0, 0)` (a fourth float 1.0 is also
pushed, but 0x005a0270's body never reads it). The 1000.0 is the double at 0x012a2d90.

### 2.2 `fade_in` — 0x00a49a50 (same registrar)

**Arguments** (**CONFIRMED**): arg 1 number (seconds), unconditional; arg 2 optional flags number,
default 3. **Return** 0. **Body**: bit 1 calls the **fade-in request helper 0x0059fc40** with
`(int(duration * 1000.0), 0, 0)`; bit 2 calls the broadcast helper 0x005a0400 with the same three
arguments (plus an unread 0.0 float). No colour handling.

### 2.3 The request helpers: 0x0059f8c0 (fade out) and 0x0059fc40 (fade in)

Both take three stack arguments, read off the raw listing (the decompiler mislabels them):
**`(durationMs, completionCallback, flag)`** — **CONFIRMED**. Arg 1 is the integer millisecond
duration, arg 2 a C function pointer called with the new state when the request is already satisfied,
arg 3 an integer stored in 0x013effc8 and passed to Lua as the first parameter. Both Lua wrappers
pass 0 for the callback and 0 for the flag. (§26.23 had the order as "(fadeMode, callback,
duration)"; the millisecond duration is the first argument, not the third — see §7.)

**0x0059f8c0, fade-out request** (**CONFIRMED**, full body):
1. Calls 0x005542f0, which posts the 32-bit id 0x5c0b1b1a through 0x0045d990 (a routine gated on
   byte 0x031728c2 that forwards the id and a default object 0x02cea938 to 0x0045ea70).
   **HYPOTHESIS**: a sound-event post (the sibling query names carry the `sfx_` prefix).
2. If state == 3 **and** target == 3 (already fully out): call the callback with 3 if one was given;
   return. Nothing else changes.
3. Otherwise set target := 3 and 0x013effc8 := flag.
4. If state == 0 (a fade-in is running): store the callback in the **deferred** slot 0x013effd0 and
   return. No Lua call, state unchanged. The fade-out is picked up later (see §2.6).
5. Otherwise, if 0x012e6aa0 != -1: look up the global `screen_fade_do` in the Lua state returned by
   0x00e1a1b0 (the interface/UI state at 0x02a45450, per `spec-lua-bindings.md` §13.7) using the
   named-hook check 0x00e0cef0 (true only if the global is a function). If so, open a call to it
   with 0x00e0ca80, store 0x013effc0 into the builder's +0x14, push three numbers in this order:
   **`flag` (as float), `1.0`, `durationMs` (as float)**, and dispatch with 0x00e0cd00.
6. Set state := 1 and 0x013effcc := callback. Return.

**0x0059fc40, fade-in request** (**CONFIRMED**, full body):
1. Calls 0x00554310: when 0x00707490() is false and 0x007b3ba0(0x38) is false, posts id 0xf0493dc8
   through 0x0045d990 (same **HYPOTHESIS** as above).
2. If state == 2 **and** target == 2 (already fully in): callback(2) if given; return.
3. Set target := 2 and 0x013effc8 := flag.
4. If state == 1 (a fade-out is running): deferred slot 0x013effd0 := callback; return.
5. If either hold timestamp 0x012e6ab0 or 0x012e6ab8 is set (value >= 0, test 0x00d9e4c0) and not
   yet reached (test 0x00d9e400: the millisecond clock at 0x01320d9c has passed the stamp, with a
   900,000,000 wrap tolerance): deferred slot := callback; return.
6. Set 0x012e6aac := -1 and 0x012e6ab4 := -1. If 0x012e6aa0 != -1: the same `screen_fade_do`
   dispatch as above, with the numbers **`flag`, `0.0`, `durationMs`**.
7. Set state := 0 and 0x013effcc := callback, then tail-call 0x0045d990 with id 0xa577ee9c.

So the C side never animates anything. It sets the direction (state 0 or 1), tells the UI Lua
script `screen_fade_do(flag, targetAlpha, durationMs)` to run the transition, and parks a callback.

### 2.4 The broadcast helpers: 0x005a0270 (out) and 0x005a0400 (in)

**CONFIRMED**, full bodies. Both do nothing unless 0x0087ba20 (the session object at 0x024d8534)
exists and its +0x5c equals its +0x58 (the host gate §8.13 already names). Then each opens an
opcode-0x53 record (0x0086f5f0) and writes with the bit writer 0x00881110/0x00881040:
- 0x005a0270: a 1-bit flag = 1, then the **current overlay colour as 4 bytes** (read back through
  0x00a722e0 → 0x005d9010), then 16 bits of `durationMs`, then 8 bits of the helper's arg 2 (0 from
  Lua); sets byte 0x013effc5 := 1.
- 0x005a0400: a 1-bit flag = 0, then 16 bits of `durationMs`, then 8 bits of arg 2; sets
  0x013effc5 := 0.
Each record is sent through 0x0086f1b0 to the session's peer list and closed with 0x0086eb20.
The 1-bit direction flag and the colour bytes are not in §2.9/§8.13's description (see §7).

### 2.5 The three queries

| Lua name | Address | Args | Return | Body (**CONFIRMED**) |
|---|---|---|---|---|
| `fade_is_fully_faded_out` | 0x00a49790 | none (gettop called, result discarded) | 1 boolean | pushes `0x0059fa10()` = (state at 0x012e6aa4 == 3) |
| `fade_is_fully_faded_in` | 0x00a49760 | none | 1 boolean | pushes `0x0059fa00()` = (state == 2) |
| `sfx_faded_out` | 0x0059fb30 (UI wrapper 0x005a01d0) | none | 1 boolean | pushes (state == 3), the comparison inlined |

**Answer to Team B item 1, "is it the same test as sfx_faded_out":** yes. `fade_is_fully_faded_out`
and `sfx_faded_out` read the same dword 0x012e6aa4 and compare it with 3; `fade_is_fully_faded_in`
compares it with 2. **CONFIRMED.** Neither takes an argument. The sibling at 0x0059fb60 in the same UI
wrapper compares the state with 2; it sits in the slot after `sfx_faded_out`, so it is almost
certainly `sfx_faded_in` (**HIGH CONFIDENCE**; its registered name was not dumped — next dump §8).

### 2.6 What completes a fade (Team B item 1)

Facts the dumps settle (**CONFIRMED**):
- The only instructions in the whole binary that store 3 or 2 into 0x012e6aa4, apart from the
  register stores in the init/shutdown pair 0x0059fa30/0x0059faa0, are at **0x005a0136 (:= 3)** and
  **0x005a0159 (:= 2)**, inside code Ghidra has not defined as a function (its references run from
  0x005a011a to 0x005a01bd). That code also reads the target 0x012e6aa8 (0x005a016d, 0x005a017f),
  reads the in-flight callback 0x013effcc three times, reads the deferred callback 0x013effd0,
  writes the deferred slot to 0 (0x005a01b1) and rewrites 0x013effcc (0x005a01bd), and writes the
  hold timestamps 0x012e6ab0/0x012e6ab8/0x012e6ab4 (0x005a0148–0x005a0152).
- The UI-wrapper registrar 0x005a01d0 stores the code pointer **0x005a0110** in the slot immediately
  before the `sfx_faded_out` pair (name at [ESP+0x14], function 0x0059fb30 at [ESP+0x18]; the
  function pointer 0x005a0110 is at [ESP+0x10], so its name string is at [ESP+0xc]). 0x005a0110 is
  therefore a **Lua-registered native in the interface state**, and the completion code above is its
  body.
- No C function in the dumps advances the fade per frame, and no timer compares elapsed time
  against 0x013effc8. The duration is only ever handed to Lua.

Conclusion (**HIGH CONFIDENCE**; the body at 0x005a0110 is in the dumps only as scattered xref
lines, not as a listing): a fade completes when the **UI Lua side calls the native registered at
0x005a0110** after `screen_fade_do` has finished its transition. That native sets the state to the
requested target (3 after a fade-out, 2 after a fade-in), fires the parked completion callback, and,
if the opposite direction was requested meanwhile (deferred slot 0x013effd0 non-zero, target
differing from the new state), starts that deferred transition. A 2026-09-30 reading that the fade is
"queued on a UI command queue" is not what the code does: it is a direct call of a Lua global.

What drives the per-frame animation and the exact duration semantics is in the shipped UI Lua that
defines `screen_fade_do` (the interface state loads it; Team B can read that script through the
bridge as data, not spec). **OPEN** from the executable side: the registered name of 0x005a0110,
and the body of the per-frame C helper 0x0059fe70, which is the only caller of both request helpers
and the only setter of the hold timestamps (it also reads state, target, 0x013effc8 and 0x013effd0).
**HYPOTHESIS**: 0x0059fe70 is the per-frame fade update that re-issues deferred requests once the
hold timestamps expire.

### 2.7 What a host must do (summary for Team B)

- Keep one dword state with the encoding of §1; start at 2.
- `fade_out(d, colour, flags)`: set the overlay colour; if flags & 1, run the request of §2.3 (ms =
  trunc(d*1000)); if flags & 2 and the host is the co-op host, emit the 0x53 record (ignorable in
  single player). `fade_in(d, flags)` likewise.
- The request only flips state to 1 (out) / 0 (in) and calls the Lua global `screen_fade_do(flag,
  alpha, ms)` in the **UI state** if that global is a function; if it is not defined, the real engine
  never completes the fade either (this is exactly why the busy-poll never ends in the host).
- Completion comes back through a native (the one at 0x005a0110) that the UI script calls; a host that
  does not run the UI scripts should complete the transition itself after `ms` milliseconds and set
  state := target, then run the deferred request if any. The latter is a host-side substitute, not
  engine behaviour; mark it as such.
- `fade_is_fully_faded_out()`/`sfx_faded_out()` ⇒ state == 3; `fade_is_fully_faded_in()` ⇒ state == 2.

---

## 3. zscene: globals and the table entry

**CONFIRMED** (xref dumps) unless marked.

| Address | Role | Writers (from the xref lists) |
|---|---|---|
| 0x0153b294 / 0x0153b29c | scene table base / count, entries 0xf8 bytes | 0x007231e0, 0x00723d60, 0x00723e40 |
| 0x0153b530 | pointer to the **current** scene entry | cleared by 0x00720320 and 0x00721c20 (one path); set by 0x00720410 (from 0x0153b538), 0x00722f10, 0x007231e0, 0x00728440 |
| 0x0153b538 | pointer to the **pending/requested** entry | set by 0x007232e0 (prep); cleared by 0x00720410; 0x00720320 copies current into it |
| 0x0153b51c | **load state code**: 0 idle, 1 loading, 2 loaded | := 0 by 0x00720320, 0x00721c20; := 1 by 0x00720410; := 2 by 0x007285c0; register stores in 0x00722f10, 0x007231e0 |
| 0x0153b556 | byte; when set, prep refuses and `zscene_is_loaded` reports true | 0x0072df50; its **address** is passed to something by 0x0072d330 |
| 0x0153b568 / 0x0153b56c | the two extra values prep stores (always 0 from Lua, see §4.1) | 0x007232e0; read and re-stored by 0x00720410 |
| 0x0153b534 | a secondary handle released on unload through service 9 | set by 0x007285c0, cleared by 0x00721c20 |
| 0x0153b541 / 0x0153b542 | bytes set on unload (1 / arg 2) | 0x00721c20, 0x00720320, 0x00722f10 |
| 0x0153b528 / 0x0153b520 | the cutscene manager object (per `spec-xtbl-format.md`) and its playback state | not written by anything dumped here |

Meaning of the state code: "2 = loaded" is **CONFIRMED** as the value `zscene_is_loaded` tests; that
1 is written by the function that promotes pending to current (0x00720410: it copies 0x0153b538 into
0x0153b530, clears 0x0153b538 and stores 1) and 0 by both teardown paths is visible in the xref
lines, so "1 = loading, 0 = idle" is **HIGH CONFIDENCE**; the bodies of 0x00720410 and 0x007285c0
(the := 2 writer) are not in the dumps.

**Scene entry layout** (stride 0xf8, from the three functions that touch it; **CONFIRMED** for the
offsets, roles as labelled):
- +0x4: CRC-32 of the lower-cased name (0x00d9e8b0 with seed 0, no length cap) — the lookup key.
- +0x8: **kind**; a value of 1 marks an entry that can be prepared/loaded. Tested == 1 by prep's
  gate (0x007232e0), by the named fast path (0x00723d20) and by tier 2 (0x00721db0). Nothing dumped
  writes it (**OPEN**: who fills the table — see §8).
- +0xc: primary resource handle; +0x10: alternate handle, chosen when the primary is not live, or when
  the local player (0x009da4e0 → 0x0262edfc) has byte +0xa41 == 1 (**HYPOTHESIS**: a variant
  selector such as gender). "Live" = the handle query 0x00dafbb0 (→ 0x00db19d0) returns > 0.
- +0xac: byte, "has a lightset"; +0xad: a lightset file name, looked up by CRC in the lightset cache
  0x013de2c8/0x013de2bc (`spec-tables-vehicle-world.md`) and removed from it on unload.

---

## 4. zscene: the functions and the lifecycle

### 4.1 `zscene_prep` — 0x00a68ee0

**Arguments** (**CONFIRMED**): 1 string, read unconditionally with `lua_tolstring(-n)`. **Return** 0.

**Body** (**CONFIRMED**): hash the name; find the entry with 0x00721be0 (linear scan of the table
comparing +4 with the hash; 0 if absent); call the gate 0x007232e0 with `(entry, 0, 0)` — the two
zeros are the `.rdata` dwords at 0x01180120 and 0x01180124, both 0 in the file (§8.21 called them
"caller-relayed values"; they are constants).

**0x007232e0 (prep gate)**, **CONFIRMED**: returns 0 if byte 0x0153b556 is set, or the entry is
null, or its kind (+8) != 1. If the entry is already the current scene (0x0153b530), returns 1 with
no change. Otherwise: calls 0x0101b530, which is a **stub returning 1** (two instructions; 15
callers; it does nothing); calls **0x00721c20(1, 0, 0)**; stores the entry into the pending slot
0x0153b538 and the two zeros into 0x0153b568/0x0153b56c; returns 1.

**0x00721c20(a, b, c)** is a **teardown of the current scene**, not a load start (**CONFIRMED** for
the body; the labels "release" for 0x00dafad0 and "state classifier" for 0x00dafb60 are the spec's
own from §2.7):
1. Returns at once if the cutscene manager object (*0x0153b528) exists, its +8 == 1, and the
   cutscene state 0x0153b520 is between 7 and 13 inclusive (**HYPOTHESIS**: never unload under a
   playing cutscene). Returns if there is no current scene.
2. Picks the live handle of the current entry (+0xc, else +0x10 by the rule in §3).
3. If that handle is not live: classify the selected handle with 0x00dafb60; if the class is 1 and
   `c` != 0, release 0x0153b71c/0x0153b720 via 0x007317a0, clear 0x0153b530 and the state, set byte
   0x0153b541 := 1. Prep passes `c` = 0, so from prep this path only returns.
4. If the handle is live: if 0x0153b534 is set, call slot +0x60 of the service-9 object
   (0x00dd6450(9) → +0x48) with `(0x0153b534, 0, 0)` and clear it; if +0xac is set, find the lightset
   named at +0xad (0x00596fa0) and drop it from the lightset cache (0x005970c0 with flag 0, i.e.
   without the 0x00dad5d0 release); release the selected handle with 0x00dafad0 (via 0x00720240); if
   `a` != 0, 0x007317a0(1); then 0x0153b541 := 0, 0x0153b542 := `b`, **state := 0**. The current
   pointer 0x0153b530 is left as it was on this path.

So after `zscene_prep(name)`: the previous scene's resources are released, the state code is 0, and
the new entry sits in the **pending** slot. Nothing dumped starts the load. The promotion pending →
current with state := 1 is done by 0x00720410, and state := 2 by 0x007285c0, neither of which is in
these dumps (**OPEN**: their callers; §8). §8.21's "kicks off a load/prepare sequence via
0x0101b530/0x00721c20" is therefore wrong on both counts (see §7).

### 4.2 `zscene_is_loaded` — 0x00a68f30

**Arguments** (**CONFIRMED**): 1 optional value; when `n >= 1` and arg 1 is not nil it is converted
with `lua_tolstring`; a non-string/non-number gives a null name. **Return** 1 boolean.

**Body** (**CONFIRMED**, read off the raw branch at 0x00a68f8f):
- Tier 1 (named only): `0x00723d20(name)` = "the name hashes to an entry **and** its kind (+8) == 1".
  **If this is false, the function pushes true immediately.** If it is true, it falls through to
  tier 2 with the name.
- Tier 2, `0x00721db0(name or null)`: true if byte 0x0153b556 is set; else, if a name was given and
  resolves to an entry that is **not** the current scene (0x0153b530), returns (kind != 1) — which,
  for an entry that passed tier 1, is **false**; else (no name, no entry, or the entry is the current
  scene) returns (state 0x0153b51c == 2).

**Resulting semantics** (**CONFIRMED**):
- `zscene_is_loaded()` ⇒ `0x0153b556 || state == 2`.
- `zscene_is_loaded(name)`: if `name` is not in the table, or is a non-kind-1 entry ⇒ **true**
  (nothing to load). If it is a kind-1 entry: `0x0153b556` ⇒ true; entry is not the current scene ⇒
  **false**; entry is the current scene ⇒ `state == 2`.

**Team B item 2, the §14.23 "sense inversion":** there is no inversion in the engine. §14.23
misread the tier-1 branch: the Lua function returns true when the fast path reports **false**, and
only consults the broader check when the fast path reports **true**. Both tiers agree that +8 == 1
means "a real zscene entry" (a kind, not a load state); the load state is the separate global
0x0153b51c, and 2 means loaded. With the branch read correctly the two tiers compose into the table
above.

### 4.3 Lifecycle as the dumps settle it

1. `zscene_prep(name)` → previous scene torn down (state 0), new entry pending (0x0153b538).
2. Something (not dumped) promotes pending → current and writes state 1 (0x00720410), later state 2
   (0x007285c0, which also sets 0x0153b534). **OPEN**: which per-frame or streaming routine calls
   these two; candidates from the reference lists are 0x00725df0 (calls the prep gate, the teardown
   and 0x0059f8c0, i.e. it also requests a screen fade), 0x00737780 (calls the gate) and 0x0072d660
   (tests state == 2).
3. `zscene_is_loaded(name)` is true once the entry is current and the state is 2.
4. Where the entries of the 0xf8-stride table come from is **OPEN**: the only writers of the base and
   count are 0x007231e0 (which also sets current and state) and 0x00723d60/0x00723e40; none was
   dumped. Team B's "which data file" question cannot be answered from this job.

---

## 5. `vint_is_std_res` — 0x00e1a150 (UI registrar 0x00e1dfb0, `spec-lua-bindings.md` §13.7)

**Arguments**: none (gettop called, discarded). **Return**: 1 boolean. **CONFIRMED**.

**Body** (**CONFIRMED**): takes the pointer returned by 0x00e237e0 (= 0x00e236f0() + 4) and reads
the two signed 32-bit integers at that pointer (+0 and +4), computing `first / second` in double
precision. If the quotient is **less than 1.5** (the double at 0x012a2d30), the result is **true**.
Otherwise the result is true only if the dword at **0x0132bd80 equals 2**, else false. 0x0132bd80 is
file-backed -1 and is written only by 0x00e23000, with the values -1, 0, 1, 2, 3, 4.

Reading (**HIGH CONFIDENCE**; the struct is unlabeled in the dumps): the two integers are the screen
width and height, so the function reports "standard (non-wide) resolution" when width/height < 1.5
(4:3 = 1.33, 5:4 = 1.25 qualify; 16:10 = 1.6 and 16:9 = 1.78 do not), with an override: display mode
2 at 0x0132bd80 forces the standard-resolution layout regardless of aspect. **OPEN**: what the five
mode values of 0x0132bd80 mean (0x00e23000 next dump), and what 0x00e236f0 returns (next dump).

For a host: `vint_is_std_res()` = `(width / height) < 1.5 || displayMode == 2`; with no display mode
concept, `(w / h) < 1.5`. Return it in the UI state only (§13.7).

---

## 6. `vint_get_safe_frame` — 0x00e1b570 (brief; Team B item 5)

**Arguments**: none. **Return**: 4 numbers. **CONFIRMED** for the shape: it fetches a per-thread
context (TLS slot 0x02cc4700, then +0x674), takes the object at its +0x14, reads the integers at that
object's +0x8 and +0xc, forms four integers by multiplying each by one of two double constants
(0x0115ba60 and 0x0116dfc0) and rounding, wraps each in a numeric variant and pushes it with
0x00e1a420, then destroys the variants. The pairing is (c1·a, c1·b, c2·a, c2·b) with a = +0x8,
b = +0xc (**HIGH CONFIDENCE**; the x87 operand order is ambiguous in the listing). The two constants
are not readable from the dump (only their low dwords are listed: 0x40000000 and 0xa0000000, which are
the low halves of single-precision literals widened to double; 0x0116dfc0's half matches 0.1f) —
**OPEN**, next dump. **HYPOTHESIS**: the four values are the left/top and right/bottom safe-frame
edges in pixels.

---

## 7. Comparison with the current spec text

| Section | Verdict | Detail |
|---|---|---|
| §2.9 `fade_out` | confirms; corrects | Argument reading, colour setter, both flag bits: CONFIRMED as written. Corrections: the helper's real signature is `(durationMs, callback, flag)`; the Lua call is `screen_fade_do(flag=0, 1.0, durationMs)` in that parameter order; the opcode-0x53 record also carries a direction bit (1) and the 4-byte overlay colour before the 16-bit duration; the "pure state machine" reading is incomplete: the transition is not run by C at all (§2.6). The "(ms) seconds→milliseconds" claim is confirmed (double 1000.0 at 0x012a2d90). |
| §8.13 `fade_in` | confirms; corrects | Mirror shape, 0.0 vs 1.0, host gate and opcode 0x53: CONFIRMED. Corrections: "when not already idle/faded" is really "unless a fade-out is running (state 1) or a hold timestamp (0x012e6ab0/0x012e6ab8) has not expired", in which case the request is deferred into 0x013effd0; the record's 8-bit value is the helper's arg 2 (always 0 from Lua), preceded by a direction bit (0); the helper also posts id 0xa577ee9c through 0x0045d990 on success. |
| §8.21 `zscene_prep` | confirms; contradicts | Hash, lookup, gate: CONFIRMED. Contradicted: 0x0101b530 does nothing (returns 1); 0x00721c20(1,0,0) is a teardown of the current scene, not a load start; the "two caller-relayed values" are the constants 0 at 0x01180120/0x01180124; the entry's +8 is a kind (1 = loadable), matching §8.21's own wording and refuting §14.23's "state". The "busy flag" label for 0x0153b556 is unsupported: it behaves as a bypass toggle whose address is handed out by 0x0072d330 (HYPOTHESIS). |
| §14.23 `zscene_is_loaded` | contradicts | The fast path's polarity is inverted in the spec: the function pushes true when the named entry is absent or not kind 1, and falls to the broader check otherwise. With that fixed, the two tiers agree; the OPEN "sense inversion" item closes. State codes: 2 = loaded (tested), 1 = loading, 0 = idle (HIGH CONFIDENCE). |
| §26.9 `sfx_faded_out` | confirms; adds | == 3 on 0x012e6aa4: CONFIRMED; identical to `fade_is_fully_faded_out`. The sibling 0x0059fb60 (== 2) is the fade-in query. |
| §26.10 | confirms with signature fix | The call `0x0059f8c0(0, callback, 0)` is a zero-millisecond fade-out with a completion callback, which then runs the quit path. |
| §26.23 | corrects; confirms | Signature order corrected to `(durationMs, callback, flag)`. Globals list confirmed and extended (0x013effc0, 0x012e6aac/ab0/ab4/ab8, 0x013effc5). 0x00e1a1b0 returns the UI Lua state 0x02a45450, as `spec-lua-bindings.md` §14.1 says; the "builder singleton" is what 0x00e0ca80 returns, not 0x00e1a1b0 — this settles the conflict the bindings spec flags between §26.23 and §14.1. |
| `spec-lua-bindings.md` §13.7 | confirms | `vint_is_std_res` at 0x00e1a150 registered by 0x00e1dfb0; the fade dispatch targets the same interface state, consistent with Team B request 8's answer. |

---

## 8. Next dump (addresses; nothing above these lines depends on them)

Job-ready items for `CrreishDump` (`func` depth=1 unless noted):
- **Fade completion**: `func 0x005a0110` (if Ghidra has no function there, the script will report
  "no function"; then run `xref 0x005a0110` and `func 0x005a01d0` — the wrapper's listing gives the
  registered name string at [ESP+0xc], after which `lua <that name>` dumps the body). Also
  `lua sfx_faded_in` to confirm 0x0059fb60.
- **Fade per-frame / init**: `func 0x0059fe70 0x0059fa30 0x0059faa0 0x0059fa20 0x0059f9c0
  0x0059f9f0 0x0059fb90 0x00e0cba0`; `xref 0x0059fe70 0x0059fa30 0x013effc0 0x012e6ab0`.
- **zscene load start / completion / table source**: `func 0x00720410 0x007285c0 0x00720320
  0x00722f10 0x007231e0 0x00723d60 0x00723e40 0x00725df0 0x00737780 0x0072d660 0x007203e0
  0x0072df50 0x0072d330`; `xref 0x0153b534 0x0153b541 0x01180120`.
- **UI resolution**: `func 0x00e23000 0x00e236f0`; `xref 0x0132bd80`.
- **Safe frame constants**: the 8 bytes at 0x0115ba60 and 0x0116dfc0 (an `xref` of each prints the
  full data if the script is extended to show qword values; otherwise a one-line Ghidra read).
- Audio-id posts (HYPOTHESIS check): `func 0x0045d990 0x0045ea70`; `str` cannot help (ids are hashes).
