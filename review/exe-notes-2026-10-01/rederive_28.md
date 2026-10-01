# Re-derivation of spec-lua-api-behaviour.md §28 against fresh dumps (2026-10-01)

Inputs: `team-a/spec-lua-api-behaviour.md` §28 (lines 11301-11637), the desk review
`scratchpad/review/adv_28.md`, and two bridge results:

- **pwgq** = `/root/crreish-bus/results/20261001T021703-team-a-pwgq/` (`func/` one file per §27/§28 entry,
  depth 1; `names/index.txt` = registrar string locations; `wrap/`).
- **jxxz** = `/root/crreish-bus/results/20261001T021707-team-a-jxxz/` (`callees/` the callees the desk review
  named, depth 1; `globals/` xrefs for the two string literals and some globals).

Both jobs lost their options, so each dump is depth 1 (the entry plus its direct callees, each with its own
listing and decompile). Everything below marked CONFIRMED — disassembly was read in those listings; the
decompile was used only to orient. Addresses are the executable's virtual addresses (image base 0x00400000).

Shared Lua-host helpers seen in every entry (identities already used throughout the document, re-read here):
0x00dfde50 = argument count (stack top minus base, 16-byte slots); 0x00dfe210 = string fetch (with optional
length out); 0x00dfe160 = number fetch; 0x00dfe1e0 = boolean fetch; 0x00dfe040 = type query (-1 when the index
is past the top, 0 for nil); 0x00dfe3a0 / 0x00dfe420 / 0x00dfe590 = push number / string / boolean;
0x00ea2596 = the round-to-integer half of the §4.1 pair. Negative indices are used throughout (`-n` = arg 1).

**Registrar check (pwgq `names/index.txt`, all 25):** every §28 name string is stored by the builder
0x00a20840 into a slot, and the function pointer stored into the next slot (+4) is exactly the address the
spec gives. All 25 name↔address pairings are CONFIRMED — disassembly. (Thirteen of the 25 pointers carry no
"function entry" tag in that listing — 0x00a4cc40, 0x00a4cab0, 0x00a4ab80, 0x00a4c520, 0x00a4c0c0,
0x00a46fd0, 0x00a47fd0, 0x00a42c20, 0x00a42c40, 0x00a42a60, 0x00a421a0, 0x00a43090, 0x00a46550 — which is
probably the set the section header means by "12 needed a function-boundary fix"; HYPOTHESIS, the header
should list them.)

Two engine idioms recur below and are named once here:

- **Host check**: 0x0087ba20 returns the session singleton (global 0x024d8534); "is host" is that object's
  +0x5c equal to its +0x58 (§8.27 already has this).
- **Record-and-replicate**: 0x0086f5f0(opcode) opens a network record; 0x00881110(…, bits) / 0x00881040(…,
  bytes) / 0x0086f4b0(id pair → 16-bit) / 0x0086f530(string) / 0x004d46e0(byte) append fields; 0x0086f110 or
  0x0086f1b0 commits; 0x0086eb20 closes. Gates: single-gate 0x008addb0(obj, n) (§6/§7 "variant 2") and the
  double gate 0x008ae480(id pair, 0) then 0x008837a0() (§3/§4 "variant 1").

---

## 28.1 `helicopter_set_dont_death_spiral` (0x00a4cc40) — CONFIRMED (one OPEN closed)

**Evidence:** pwgq `func/func_0x00a4cc40.txt` (entry, 0x00a281e0, 0x00b37740).

- Args: string via 0x00dfe210(-n, 0); boolean via 0x00dfe1e0(1-n), read unconditionally (no nil gate). The
  boolean byte is stored into a stack slot *before* the resolver call and read back from the same slot (after
  the stack is unwound) as the second argument of the dispatch — the stale-slot delivery the entry describes,
  and the decompile is numerically right. CONFIRMED — disassembly.
- Resolver 0x00a281e0: tries 0x00a280c0(name, 0); if that handle has a non-zero +0x16c0/+0x16c4 pair it
  resolves that pair through 0x004dcf00(lo, hi, 0); otherwise the name goes to 0x0062a190 on singleton
  0x02442750, then liveness 0x00853b10, then the handle's vtable+0x68 predicate, then vtable+0x70 cast.
  CONFIRMED — disassembly (matches §4.5's description as far as it goes).
- Dispatch 0x00b37740(vehicle, bool): plain 2-argument call, stack cleaned by 8. CONFIRMED — disassembly.
- Return 0 on every path. CONFIRMED.

**0x00b37740 (closes "OPEN — its own internals"):** a single-gate record-and-replicate setter. If
0x008addb0(vehicle, 0) is true: fetches the vehicle's AI record via 0x00a79470(vehicle) and sets bit 0x8 of
the byte at that record's +2 to the boolean (CONFIRMED — disassembly). If false: opens an opcode-0x42 record,
appends an 8-bit 0x14, an 8-bit 4, the vehicle's 16-bit id (0x008add70), the boolean byte, and commits it via
0x0086f110 to the target 0x008ae020(vehicle, 0, 0, 0) returns (field order CONFIRMED — disassembly; the
widths 8/8/16 are the literals handed to the bit writers, HIGH CONFIDENCE). Opcode 0x42 is the vehicle-update
opcode §9.21/§9.x already record for 0x00b374b0, a neighbour of this function.

**Spec text changes:**
- Strike "[CONFIRMED — disassembly, full chain; OPEN — 0x00b37740's own internals.]" → "[CONFIRMED —
  disassembly, full chain. 0x00b37740 is a single-gate (0x008addb0) record-and-replicate setter: gate true →
  bit 0x8 of byte +2 of the vehicle's AI record (0x00a79470); gate false → opcode-0x42 record (0x14, 4,
  16-bit vehicle id, boolean byte) committed via 0x0086f110. OPEN — the meaning of 0x00a79470's record beyond
  the fields used here.]"
- "presumably suppresses the scripted/physics death-spiral" stays HYPOTHESIS.

## 28.2 `helicopter_fly_to_set_goal_direction` (0x00a4fa80) — CORRECTED

**Evidence:** pwgq `func/func_0x00a4fa80.txt` (entry, 0x00ad31a0, 0x00da4130, 0x00a455a0, 0x00b3f6d0);
jxxz `callees/func_0x00da4130.txt`, `callees/func_0x00b3f6d0.txt`.

What the listing shows (frame: 0x34 bytes of locals under four saved registers):

1. Args: arg 1 and arg 2 are both fetched unconditionally with 0x00dfe210 (arg 2 is kept in a local). Arg 3:
   only when the count exceeds 2 and 0x00dfe040 says non-nil, 0x00dfe1e0 is read; **otherwise the mode flag
   is false**. CONFIRMED — disassembly.
2. 0x00a281e0(arg 1) then 0x00ad31a0(heli): the latter returns true iff the dword at (heli+0xbf4)+0x2c is 3
   or 4 (AI mode), false for null. CONFIRMED — disassembly.
3. The three dwords at 0x029cdb98/0x029cdb9c/0x029cdba0 are copied into a local **12-byte position buffer**.
   These globals are zero-fill (not file-backed), i.e. runtime-written, not "fixed constants". CONFIRMED —
   disassembly (the globals section says "NOT file-backed").
4. 0x00da4130 is called with the receiver register = the address of the local **36-byte buffer** (the one
   passed as 0x00a455a0's third argument two instructions later) and one stack argument = 0x01321400. Its
   body copies nine floats (0x24 bytes) from the argument to the receiver and returns with a 4-byte purge.
   So the copy destination is the 36-byte orientation buffer, not the 3-float buffer; §20.24's size reading
   was right. CONFIRMED — disassembly (both the caller's frame arithmetic and the callee body).
   0x01321400 holds 1.0 at +0, 0 at +8, 1.0 at +0x10, 0 at +0x18, 1.0 at +0x20 (file-backed values printed in
   the dump) — the diagonal of a 3×3 identity matrix; the four odd dwords were not printed, so "identity
   orientation" is HIGH CONFIDENCE. 0x00a455a0 itself copies 0x01321400..0x01321420 into its 36-byte output on
   its descriptor-bit-0x1@+0xb branch (same dump) — consistent with §20.24.
5. 0x00a455a0(arg 2, &pos12, &orient36): three stack arguments, no receiver. CONFIRMED — disassembly. Its
   first attempt is 0x00a280c0(name, 0); a null arg 2 therefore fails the lookup and nothing is dispatched.
6. Dispatch 0x00b3f6d0(heli, x, y, z): 12 bytes reserved + heli pushed, cleaned by 0x10. CONFIRMED.
   **Mode true:** the three floats pushed are the **last three floats of the 36-byte orientation record**
   (frame offsets 0x38/0x3c/0x40 = record +0x18/+0x1c/+0x20, i.e. its third 3-vector — the target's +0x64/
   +0x68/+0x6c when 0x00a455a0 copied a live object). Not "the raw resolved-target offset triple".
   **Mode false:** target position (12-byte buffer) minus heli +0x40/+0x44/+0x48, computed in double
   precision and rounded back to float. CONFIRMED — disassembly.
7. 0x00b3f6d0 body: re-checks 0x00ad31a0(heli); fetches 0x00a79470(heli); only if that record's +0x4b0 equals
   1 does it set bit 0x2 of the byte at +0x298 and store the three floats at +0x430/+0x434/+0x438. CONFIRMED
   — disassembly. Which world axis the "third basis vector" is stays OPEN.
8. Return 0 on all paths.

**Spec text changes:**
- Arguments: strike "1 mandatory string (target reference, may resolve as a named object OR be
  absent-handled via arg 3's presence)" → "1 mandatory string (target name for 0x00a455a0; when absent the
  lookup fails and nothing is applied)". Strike "1 optional boolean, standard lua_type-gated idiom (checked
  only if enough args are present) — a 'use direct offset' mode selector" → "1 optional boolean, nil-gated,
  default false — selects 'use the target's third orientation vector' instead of 'delta to the target'".
- Body: strike "initializes a local 3-float buffer from three fixed default-position constants
  (0x029cdb98/0x029cdb9c/0x029cdba0)" → "initializes a local 12-byte position buffer from three runtime
  globals 0x029cdb98/0x029cdb9c/0x029cdba0 (zero in the static image)". Strike "HIDDEN-this thiscall (this =
  the address of the local 3-float buffer just initialized)" → "a thiscall whose receiver is the local
  36-byte orientation buffer (the third argument of the following 0x00a455a0 call); 0x00da4130 copies nine
  floats from 0x01321400 (an identity 3×3 — HIGH CONFIDENCE) into it". Strike the whole "[OPEN — desk review
  …]" bracket (settled).
- Strike "either the raw resolved-target offset triple (if the boolean mode is set) or the delta…" → "either
  the last three floats of the resolved 36-byte orientation record (mode true) or the target position minus
  the helicopter's own +0x40/+0x44/+0x48 (mode false; double arithmetic, float result)".
- Side effects: strike "either as an absolute resolved-target offset" → "either as the target's third
  orientation vector". Add: "0x00b3f6d0 only stores when the helicopter's AI record (0x00a79470) has +0x4b0 ==
  1; it then sets bit 0x2 of byte +0x298 and writes the vector to +0x430..+0x438." Replace "OPEN —
  0x00b3f6d0/0x00ad31a0's own full internals" with "OPEN — the meaning of +0x4b0/+0x298 and which axis the
  third orientation row is".
- 28.26 item 8: replace "this = destination" wording with the 36-byte-buffer destination.

## 28.3 `hdr_bloom_set_multiplier` (0x00a4cab0) — CORRECTED (minor; OPEN closed)

**Evidence:** pwgq `func/func_0x00a4cab0.txt`.

The number from 0x00dfe160 is stored as a single-precision float and passed as one 4-byte argument; **the
§4.1 round-to-integer helper is not called**. 0x005dcee0 stores that float into the global 0x012ec280
(file-backed, initial 1.0f). Return 0. CONFIRMED — disassembly.

**Spec text changes:** strike "the rounded/converted float is forwarded" → "the number is narrowed to a
single-precision float (no rounding) and forwarded"; strike the "[OPEN — … round-to-int pair …]" bracket;
strike "OPEN — 0x005dcee0's own internal target" → "0x005dcee0 writes the float to global 0x012ec280 (static
initial value 1.0)".

## 28.4 `guardian_angel_enable_indicators` (0x00a4ab80) — CONFIRMED (target refined)

**Evidence:** pwgq `func/func_0x00a4ab80.txt`.

Boolean read with no nil gate, forwarded as one 4-byte argument; return 0. CONFIRMED — disassembly.
0x00626210(bool) is **not** a bare global write: it calls 0x006d2910 (through thunk 0x00614d00) and proceeds
only if that returns 3; then calls 0x00614cb0() and, if non-null, calls 0x00d34d40() and writes the boolean
byte to +0x57d of the object 0x00614cb0 returned. CONFIRMED — disassembly for the structure; the three
helpers' identities are OPEN.

**Spec text changes:** strike "a bare global 'guardian angel indicators enabled' toggle" → "a byte field
(+0x57d) on the object 0x00614cb0() returns, written only when 0x006d2910() == 3 (a mode check, OPEN)";
strike "OPEN — 0x00626210's own internal target" → "OPEN — the identities of 0x006d2910/0x00614cb0/
0x00d34d40".

## 28.5 `group_get_next_npc` (0x00a4c590) — CORRECTED (Return line)

**Evidence:** pwgq `func/func_0x00a4c590.txt` (entry, 0x005eab60, 0x00853b10, 0x005e4dd0).

- A register is zeroed at entry and returned on both failure exits: **failure returns 0 with nothing
  pushed**. The success exit pushes the string and returns 1. The Return line's "lua_gettop-derived 0/1 flag"
  is wrong. CONFIRMED — disassembly.
- 0x005eab60 and 0x005e4dd0 are both called with the receiver = 0x02442750 and one stack argument, returning
  with a 4-byte purge. Both: require singleton +0x265c > 0, look the name up via 0x004588f0 on singleton
  +0x2660, require capability bit 0x10 clear at +0x33, and require a descriptor bit in table 0x02cc9900 —
  0x20 at +0xa for 0x005eab60 (group kind), 0x4 at +0xb for 0x005e4dd0 (character kind). CONFIRMED —
  disassembly.
- 0x00853b10(obj) returns "invalid" (1) when obj is null, bit 0x4 of +0x33 is set, or the byte +0x34 is
  0xff; else 0. CONFIRMED.
- The walk reads +0x9c of the character and compares it with group +0x40, then pushes the dword at +0x18 of
  the next node as a string (0x00dfe420 pushes nil for a null pointer). §18.14's vtable+0x70 cast is **not**
  performed here; +0x18 is read on the raw node. CONFIRMED — disassembly.
- **A real gap:** a failed group resolve does not stop the function. It still resolves arg 2 and, if that
  character is alive with a non-null +0x9c, compares against the dword at address 0x40 (null group + 0x40),
  which would fault. In practice the script must pass a valid group. CONFIRMED — disassembly.

**Spec text changes:** strike the Return line and its bracket → "1 Lua value (string; nil if the next node's
+0x18 is null) with return count 1 on success; on every failure path return count 0 with nothing pushed".
Body: add "the group-resolve failure is not a short-circuit: arg 2 is still resolved, and a live character
with a next link would then dereference a null group record (+0x40) — callers must pass a valid group". Add
"§18.14's vtable+0x70 cast is not applied; +0x18 is read directly on the +0x9c node".

## 28.6 `group_get_first_npc` (0x00a4c520) — CONFIRMED

**Evidence:** pwgq `func/func_0x00a4c520.txt`.

Same resolver, same receiver 0x02442750, same liveness gate; head = group +0x40; pushes head +0x18 as a
string and returns 1; returns 0 (nothing pushed) when the string is null, the group fails, the group is
dead, or the head is null. The §18.14 "has members" bit 0x2@+0x3a is **not** tested. The extra push before
the resolver call is the callee-saved register, not an argument (the resolver pops exactly 4 bytes).
CONFIRMED — disassembly.

**Spec text changes:** replace "(… whether §18.14's 'has members' bit 0x2@+0x3a is also tested here is not
stated — OPEN.)" → "(§18.14's bit 0x2@+0x3a is not tested here.)"

## 28.7 `get_num_humans_in_trigger` (0x00a4c0c0) — CONFIRMED (callee named; details added)

**Evidence:** pwgq `func/func_0x00a4c0c0.txt` (entry, 0x005e4e30, 0x0075d4b0, 0x0093bfc0).

- Resolver 0x005e4e30 (receiver 0x02442750, descriptor bit 0x2@+7 = trigger kind). Liveness. On failure the
  function pushes 0.0 and returns 1. CONFIRMED — disassembly.
- Rendering artefacts: the decompile shows the count helper, 0x005e4e30 and 0x00853b10 with no arguments while
  the listing pushes each one's real argument; the inner liveness call is shown with a spurious second
  argument while the listing pushes one; the containment call is shown without its receiver. All CONFIRMED.
- vtable+0x58 on the trigger (receiver = trigger, two 12-byte output buffers). 0x0075d4b0 treats those two
  vectors as box corners (it forms (b−a)/2 and (a+b)/2), so they are a bounding-box min/max — HIGH CONFIDENCE.
- 0x0075d4b0(&a, &b, out, 32, 47, 0, 0): queries the spatial system (0x0161d378 → +0x58 → vtable+0x48) for
  objects in that box, keeps a candidate when 0x00778bc0(…, 47, classCode) accepts its type record's +0x1c
  code (so **47 is a category id applied through 0x00778bc0**; its meaning stays OPEN), stops at 32; if the
  category also admits code 0xc0069 it runs a second query 0x00b112d0 over the same box (up to 0x30 results)
  and appends those. Returns the count. CONFIRMED — disassembly for the structure.
- Per candidate: 0x00458230(&pair, 0x031d152c) with receiver 0x024433a8; +0x33 bit 0x10 clear; descriptor
  bit 0x1@+0xa (new on this table, as the desk review says); liveness; then **0x0093bfc0** with receiver =
  the trigger and one argument = candidate+0x40. CONFIRMED — disassembly. 0x0093bfc0 dispatches on the
  trigger's +0x8c: 0 → vtable+0x60 fills two local vectors then 0x00dbdc90(…, trigger+0x40, trigger+0x4c, …);
  1 → 0x00dbe2a0(trigger+0x40, float +0x90, point); 2 → 0x00dbe050(trigger+0x40, floats +0x58/+0x5c/+0x60,
  +0x90, +0x94, point); anything else → false. (Box / sphere / cylinder are HYPOTHESIS names.) §1.12 already
  names 0x0093bfc0 as the trigger method with a hidden receiver — this is its second sighting.
- Return: count as a number, always 1 value. CONFIRMED.

**Spec text changes:** "the final containment test … via a thiscall" → add "0x0093bfc0 (shape dispatch on
trigger +0x8c: 0/1/2, else false)". "category literal 47, plausibly 'human'" → "category id 47, tested
through 0x00778bc0 against each candidate type's +0x1c code; meaning OPEN". Add "vtable+0x58 fills the
trigger's bounding-box corners (HIGH CONFIDENCE)". Replace the §25.12/§25.25 citation as the desk review
says (the artefact here is fewer arguments shown, not more).

## 28.8 `get_char_vehicle_is_in_air` (0x00a4b160) — CONFIRMED

**Evidence:** pwgq `func/func_0x00a4b160.txt` (entry, 0x00458230, 0x00ad4040).

Resolver 0x00a281a0(name, 0) (= 0x00a280c0 with a local flag byte, falling back to 0x00a28150 when the flag
stays clear). Reads **only** +0x16c0/+0x16c4 (no +0x16d8 read anywhere in the body). 0x00458230 with
receiver 0x024433a8 and tag 0x031d152c (a hashed open-addressing lookup on the pair). +0x33 bit 0x10 clear;
descriptor bit 0x80@+6; liveness; 0x00ad4040(vehicle) → push boolean; return 1 on both paths. CONFIRMED —
disassembly. 0x00ad4040 returns true iff 0x00ad3b40(v) is false, 0x00ad3c50(v) is false, and bit 0x1 of byte
+0xc8 is clear (so "in air" = none of three ground/contact conditions; the two helpers are OPEN).

**Spec text changes:** replace "whether this function does too is not stated" → "this function reads only
the +0x16c0/+0x16c4 pair". Add the 0x00ad4040 definition above.

## 28.9 `effect_play_finisher` (0x00a48da0) — CORRECTED (argument role, return value, effect identity)

**Evidence:** pwgq `func/func_0x00a48da0.txt` (entry, 0x005bc5d0, 0x005c50b0, 0x00d21b20, 0x00a45800,
0x0052a940, 0x005cf190); jxxz `callees/func_0x0052a940.txt`, `callees/func_0x005bc5d0.txt`,
`callees/func_0x005c50b0.txt`, `callees/func_0x00a45800.txt`, `globals/xref_0x0116e190.txt`,
`globals/xref_0x0116e184.txt`.

1. **Arg 1 is a target name.** It is fetched with 0x00dfe210, parked in a local, and that local is pushed to
   0x00a45800 — which looks the name up via 0x00734e90 on 0x02442750 (descriptor bit 0x2@+6), applies the
   liveness gate, and for kinds with bit 0x8@+0xa returns the vtable+0x70 cast. CONFIRMED — disassembly.
2. Arg 2: optional number, nil-gated, default 0.0, kept as a float. Arg 3: optional number, nil-gated,
   rounded via 0x00ea2596, **default the dword 3** (a `mov dword, 3`, not a float) — the "4.2039e-45" reading
   is confirmed wrong. CONFIRMED — disassembly.
3. **The effect is fixed, not named by the script.** 0x005bc5d0 returns the byte global 0x0141250d (the
   "active input is a gamepad" flag §2.6 reads); true selects the literal at 0x0116e190 ("vfx_xbox_Icon"),
   false 0x0116e184 ("vfx_pc_Icon"). The selected pointer is pushed to 0x005c50b0 (the decompile drops it).
   0x005c50b0(name) = CRC via 0x00d9e740(name, 0) then a table lookup through 0x005c49d0 on 0x012e8b98 →
   effect index, or -1. CONFIRMED — disassembly. (This also closes §2.6's "no other reference to 0x0141250d
   found": 0x005bc5d0 is its getter, with many callers listed in the dump.)
4. **Return is not "none".** When the index is -1, when arg 1 does not resolve, or when the target's
   descriptor lacks bit 0x2@+6, the function pushes the double at 0x012a3038 (-1.0) and returns 1. On the
   success path the last call is 0x00a48af0(&handle, L) and its return value is returned unchanged — the
   exact shape §12.10/§12.12 document for the sibling effect functions. CONFIRMED — disassembly.
5. 0x00d21b20 is called (zero args) and ignored; its body returns the constant 1.
6. Position: target +0x40, +0x44 **plus arg 2**, +0x48 are written into a 12-byte local (the arg-2 offset is
   added to the middle component). CONFIRMED — disassembly.
7. 0x0052a940: receiver = a local request struct; explicit arguments in order (effect index, &position,
   target+0x4c). The third explicit argument is the **effect index**, not "the string literal's result".
   The callee stores the index at +0, 0 at +4, the position at +0x10..+0x1c, copies the orientation via
   0x004add90 into +0x20, then fills defaults (+0x54 = -1, +0x5c/+0x60 from 0x01118590/0x01118594, +0x64 =
   0xffff, +0x6c = 1, +0x70/+0x74 = 1.0, several zero bytes). Returns with a 12-byte purge. CONFIRMED.
8. After it: request +0x50 = 1; request +0x6c = arg 3 (overwriting the callee's default 1); if the target's
   descriptor has bit 0x8@+6, request +0x58/+0x5c = target's +8/+0xc id pair; request +0x60 = -1. Then
   0x005cf190(&request) — the pointer is pushed (decompile drops it) — returns a 64-bit value stored in an
   8-byte local, and 0x00a48af0(&that, L). CONFIRMED — disassembly.
9. Dropped-argument census: four explicit (count helper, 0x005c50b0's string, 0x00a45800's name,
   0x005cf190's pointer) plus one hidden receiver (0x0052a940). The desk review's count fix stands.

**Spec text changes:**
- Arguments: strike "1 mandatory string (effect name)" and the OPEN bracket → "1 mandatory string (target
  object name, resolved by 0x00a45800)". Arg 3: add "stored into the request's +0x6c (meaning OPEN)".
- Return: strike "none" → "1 Lua value (the number -1.0, constant 0x012a3038) on every failure path; on
  success 0x00a48af0's own return value, as in §12.10/§12.12".
- Body item 2: strike "(0x0116e190 or 0x0116e184 — contents not independently read this pass, OPEN)" →
  "('vfx_xbox_Icon' at 0x0116e190 when the gamepad flag 0x0141250d is set, else 'vfx_pc_Icon' at 0x0116e184
  — a fixed icon effect; 0x005bc5d0 is the flag's getter)". Strike "meaningfully refining 0x005c50b0's own
  established signature" (already in spec-tables-environment.md §10.1, as the desk review says).
- Fifth call: strike "and the string-selected literal (0x0116e190/0x0116e184's result) as its three visible
  pseudocode arguments" → "and the effect index from 0x005c50b0".
- Side effects: strike "plays a named 'finisher' effect anchored to a resolved effect-target object (via
  0x00a45800, gated on class-descriptor bit 0x2 at row-offset +6 of table 0x02cc9900 — matching the
  already-established bit/offset several other effect functions in this document use)" → "plays the fixed
  'icon' effect (PC or gamepad variant) at a named target object's position (+ arg 2 on the middle
  component), gated on the target's descriptor bit 0x2@+6 (a target-kind gate; no effect-function
  precedent)". Strike "OPEN — the contents of the two string literals" (settled).

## 28.10 `dlc3_m03_set_sprint_waning` (0x00a46fd0) — CONFIRMED (OPEN closed)

**Evidence:** pwgq `func/func_0x00a46fd0.txt`. Boolean, no nil gate, one argument, return 0. 0x009db820 stores
the byte to global 0x0263ae3a (zero-fill); readers 0x009dc020 and 0x009e1af0; 0x009e1fc0 takes its address.
CONFIRMED — disassembly.

**Spec text changes:** "OPEN — 0x009db820's own internal target" → "0x009db820 writes byte global 0x0263ae3a".

## 28.11 `debris_flow_recycle_object` (0x00a47fd0) — CORRECTED (role)

**Evidence:** pwgq `func/func_0x00a47fd0.txt` (entry, 0x00a3de90, 0x006fd8b0); jxxz
`callees/func_0x006fd8b0.txt`, `callees/func_0x006fd2b0.txt`, `callees/func_0x006fc100.txt`.

- Arg 1 number via 0x00dfe160 then 0x00ea2596 (the §4.1 pair) — CONFIRMED. Arg 2 string; 0x00a3de90 with
  receiver 0x02442750 (descriptor bit 0x8@+0xa); liveness; 0x006fd8b0(id, obj+8, obj+0xc): three stack
  arguments, no receiver, cleaned by 0xc. Return 0. CONFIRMED — disassembly.
- 0x006fd8b0: if the double-gate's first half 0x008ae480(pair, 0) is false, it proceeds **only on the host**
  and first opens an opcode-0x45 record: 8-bit sub-code 0x25, then a **4-bit code 5**, the 4-byte id, the
  16-bit object id (0x0086f4b0), committed with 0x0086f1b0(session, 0, 0). Then (gate true, or host after
  sending) it finds the row of the 3-row, 0x6f0-byte table at 0x014fde60 whose first dword equals the id
  and, if found, calls 0x006fd4f0 with receiver = that row and argument = 0x006fbc90(pair). CONFIRMED —
  disassembly.
- Siblings: 0x006fd2b0 (§22.21 add_object) uses the same 0x45/0x25 record with 4-bit code **4** and ends in
  0x006fca10(row…); 0x006fc100 (§25.22 add_avoid_object) uses code **6** and ends in 0x006fbc50. Both take a
  trailing "skip the message" flag and a 0x006fbd80 fallback when the id is not in the table; recycle has
  neither. So recycle is a third row method (0x006fd4f0), not the avoid list's remove. CONFIRMED —
  disassembly for the family; the effect of 0x006fd4f0 is OPEN ("return the object to the flow's pool" is a
  HYPOTHESIS from the name).

**Spec text changes:** strike "removes a named world object from a debris-flow instance's own avoid/tracking
list — the mirror operation of the already-documented debris_flow_add_avoid_object (§25.22), sharing its
exact resolver and position-pair convention" and its OPEN bracket → "hands the object to the debris-flow
row's third method, 0x006fd4f0 (rows: 3 × 0x6f0 bytes at 0x014fde60, keyed by the id in the first dword);
siblings §22.21 (code 4, 0x006fca10) and §25.22 (code 6, 0x006fbc50) share the opcode-0x45/0x25 record
family; on a non-owned object only the host acts and it first replicates. [OPEN — what 0x006fd4f0 does.]".
Keep the "second consumer" fix.

## 28.12 `customization_restore_player_rig` (0x00a43c30) — CONFIRMED (one wording fix)

**Evidence:** pwgq `func/func_0x00a43c30.txt` (entry, 0x009da4e0, 0x009e3400, 0x009df3d0).

Default 3; nil gate; rounding; bit 0 and bit 1 tests; the +0xa41 == 1 test; both 0x009e3400 calls with four
stack arguments (player, rig name, tag, 0); the double call of 0x009df3d0 (once for null, once to reuse) —
all CONFIRMED — disassembly. 0x009da4e0 returns global 0x0262edfc (its result is **not** null-checked before
the +0xa41 read on the bit-0 path). 0x009df3d0 returns null unless byte 0x024d4462 is set, else the first
entry of the player list (0x03171a64 +0x1f0 / +0x58) that is not the local player. The second pointer of each
pair is a **4-character tag string** — 0x011259d0 "PLYF", 0x011259bc "PLYM" — not a resource pointer.
0x009e3400(player, name, tag, 0): with the trailing 0 it first (host only) broadcasts an opcode-0x41 record
(8-bit 9, player's 16-bit id, both strings), then locally resolves the rig name through 0x004bc7d0 against
(player+0xf4)+0xd8 with a (name, -1) fallback, installs it via 0x004bca80 on (player+0xd24)+0x14, and stores
0x004b16c0(tag) at ((player+0xd24)+0x10)+0x30. CONFIRMED — disassembly.

**Spec text changes:** strike "resourcePointer" / "fixed string-literal/resource addresses" → "a 4-character
tag string ('PLYF' at 0x011259d0 for female, 'PLYM' at 0x011259bc for male)". Add "0x009e3400 replicates on
the host (opcode 0x41) before applying locally".

## 28.13 `crib_weapon_add_enable` (0x00a42c20) / 28.14 `crib_weapon_add_disable` (0x00a42c40) — CONFIRMED

**Evidence:** pwgq `func/func_0x00a42c20.txt`, `func/func_0x00a42c40.txt`. Count helper called, result
unused; one literal (1 / 0) pushed to 0x005ee6f0; return 0. 0x005ee6f0 stores the byte to global 0x012ecb34
(file-backed, initial 1 — adding is enabled by default); readers 0x005ee630 and 0x005ee6e0; another writer
0x005ee6a0 sets 1. CONFIRMED — disassembly.

**Spec text changes:** 28.13 "OPEN — 0x005ee6f0's own internal target" → "0x005ee6f0 writes byte global
0x012ecb34 (default 1)"; 28.14 add the same note.

## 28.15 `crib_unlock_strongold` (0x00a46500) — CONFIRMED (body of 0x005f1cc0 added)

**Evidence:** pwgq `func/func_0x00a46500.txt` (entry, 0x0071f0d0, 0x005f1cc0).

0x0071f0d0: receiver 0x02442750, same singleton lookup shape as the other family members, descriptor bit
0x20@+9 (stronghold kind). Liveness. 0x005f1cc0 is entered with the receiver = the stronghold handle and no
stack arguments. Return 0. CONFIRMED — disassembly.

0x005f1cc0 body (new): with a session and as host — only if bit 0x2 of the stronghold's byte +0x7a is set —
opens an opcode-0x45 record (8-bit 2, then 0x00a017c0 serialises the stronghold), commits via 0x0086f1b0,
calls 0x007161a0 with (7,0), (8,0), (9,0), (10,0), clears bit 0x2 of +0x7a, calls 0x005f1690 on the
stronghold, increments global 0x014a0f80, then 0x005f1750(this, 1); if the bit is already clear it does
nothing at all. Without a session or as a client: 0x00b98d00(&idpair, 1, (+0x154 != [0x029c9964])) then
0x005eea30(this, 0). Both branches (when they ran) end with 0x00bd2950(stronghold) and 0x005e8fb0(&idpair, 0).
CONFIRMED — disassembly for the structure; bit 0x2@+0x7a as "still locked" is HIGH CONFIDENCE; the helper
identities are OPEN.

**Spec text changes:** replace "OPEN — 0x005f1cc0's own internals" with the paragraph above (host path vs
client path, the +0x7a bit, the counter 0x014a0f80).

## 28.16 `continuous_explosion_start` (0x00a46340) — CORRECTED (semantics; the "definition vs instance" question)

**Evidence:** pwgq `func/func_0x00a46340.txt` (entry, 0x005eb390, 0x00a455a0, 0x005eb6c0).

- Both strings fetched unconditionally. 0x005eb390(arg 1): one stack argument, **no receiver set by the
  caller** — the decompile's receiver is an artefact of the callee's own local (it hashes the name with
  0x00d9e8b0 into a local dword and then scans). CONFIRMED — disassembly: the non-singleton negative holds.
- 0x005eb390 = CRC of the name (0x00d9e8b0 with receiver = local out, args (name, 0, -1)), then a linear scan
  of 0x64-byte rows at 0x014a0310 (count at 0x014a02f0) comparing the row's first dword; returns the **row
  pointer** (a definition) or null. CONFIRMED — disassembly.
- 0x00a455a0(arg 2, &pos12, &orient36): three arguments. 0x005eb6c0(row, x, y, z): 12 bytes + row, cleaned by
  0x10. Return 0. CONFIRMED.
- 0x005eb6c0: **only when the byte 0x012ec964 is 0** (no continuous explosion running): stores the row at
  0x012ec8f0, zeroes 0x012ec8f4 and 0x012ec908, writes the position (through 0x00da38e0) to the 16-byte
  global 0x012ec950, sets 0x012ec960 = -1, then sets 0x012ec964 = 1. A second start while one is active is
  ignored. CONFIRMED — disassembly. This reconciles §22.24 (stop clears 0x012ec964): there is one global
  continuous-explosion state, and the handle is a definition row, not an instance.

**Spec text changes:** strike "dispatches to 0x005eb6c0(explosionInstance, x, y, z)" → "dispatches to
0x005eb6c0(definitionRow, x, y, z)". Strike "starts a named continuous-explosion effect instance at a named
target's resolved position" and the "(Cf. §22.24 … to be reconciled.)" note → "starts the single global
continuous explosion (state byte 0x012ec964; ignored if one is already active) from the named definition
row (0x014a0310 table, matched by name CRC) at the named target's position (stored at 0x012ec950)". Replace
"OPEN — 0x005eb390/0x005eb6c0's own internals" with "OPEN — the meaning of the globals 0x012ec8f4/0x012ec908/
0x012ec960 the start resets".

## 28.17 `clear_callbacks_for_obj` (0x00a46020) — CORRECTED (getter roles) / CONFIRMED (dispatch, wrappers)

**Evidence:** pwgq `func/func_0x00a46020.txt` (entry and all 11 callees: 0x009e19b0, 0x00734e90,
0x00a33da0, 0x00a3a210, 0x00807930, 0x00a44fa0, 0x00a27100, 0x00a2d990, 0x00a29c40, 0x006fc2b0, 0x00a31210);
jxxz `callees/func_0x0087ba20.txt`.

- Dispatch structure exactly as the entry states; all seven receiver loads (`lea` into the receiver register,
  no stack argument) CONFIRMED — disassembly. 0x00a280c0(name, 0) is a plain 2-argument call (this dump also
  shows it comparing the name against the "#PLAYER#" literal). 0x00734e90: receiver 0x02442750, descriptor bit
  0x2@+6.
- **The six "clear" wrappers are one template** and do reach the family primitive: for each of N slots at the
  receiver, if the session singleton (0x0087ba20) is non-null and is host (+0x5c == +0x58) call
  0x00a1fc60(slot value) — the hook-release primitive §10.8 documents inside 0x005e4520 — then write -1 into
  the slot. N = 25 for 0x009e19b0 (+0x1f00), 26 for 0x00a33da0 (+0xf8, character), 12 for 0x00a3a210 (+0xb0,
  vehicle), 2 for 0x00a44fa0, 1 for 0x00a2d990 (+0xc0), 7 for 0x00a31210 (+0x90). CONFIRMED — disassembly.
  Family membership is therefore confirmed, and the slot counts give the array sizes.
- **The four "getters" are kind predicates, not sub-object getters.** Each returns its input handle
  unchanged when the handle's descriptor row has the bit, else null: 0x00807930 → bit 0x2@+7 (trigger kind;
  consistent with §10.5), 0x00a27100 → bit 0x80@+0xa, 0x00a29c40 → bit 0x40@+0xa, 0x006fc2b0 → bit 0x1@+0xb
  (the kind §20.24's identity-orientation branch of 0x00a455a0 handles). CONFIRMED — disassembly. So the
  hook arrays cleared on those branches are on the **resolved object itself**: trigger +0xf8 (2 slots), kind
  0x80@+0xa +0xc0 (1 slot), kind 0x40@+0xa +0x98 (2 slots), kind 0x1@+0xb +0x90 (7 slots).
- Consequences: "+0xf8 … re-confirms the character sub-record base" is only true for the character branch;
  the 0x00807930 branch's +0xf8 is a trigger's field. Whether +0xb0 here is §21.1's vehicle anchor is
  supported by the descriptor bit (0x8@+0xb = vehicle per §15); whether +0x90 here is §21.19's door base
  depends on the door kind having descriptor bit 0x1@+0xb — not checked, OPEN.

**Spec text changes:**
- Strike "tries four further per-kind sub-object getters in sequence on the SAME resolved handle … (all four
  confirmed clean, plain single-argument cdecl calls, no hidden this on the getters themselves) — and for
  whichever ONE returns non-null, clears callbacks at THAT sub-object's own +0xf8 / +0xc0 / +0x98 / +0x90" →
  "tests four kind predicates in sequence (0x00807930 = descriptor bit 0x2@+7, 0x00a27100 = 0x80@+0xa,
  0x00a29c40 = 0x40@+0xa, 0x006fc2b0 = 0x1@+0xb; each returns the same handle or null) and, for the first
  that matches, clears the hook array at the resolved object's own +0xf8 (2 slots) / +0xc0 (1) / +0x98 (2) /
  +0x90 (7)".
- Strike "note +0xc0 re-confirms the already-established base per §24.29 item 4" unless §24.29's +0xc0 is on
  the kind with bit 0x80@+0xa (not checked here).
- Replace the "[OPEN — desk review …: that these wrappers reach the family primitives …]" bracket → "the six
  wrappers are N-slot loops of the §10.8 release-when-host-then-write -1 step (0x00a1fc60); N = 25/26/12/2/1/7
  as listed".
- Add the slot counts for the first three branches: +0x1f00 (25), character +0xf8 (26), vehicle +0xb0 (12).

## 28.18 `city_zone_swap_is_active` (0x00a42a60) — CONFIRMED (lookup described)

**Evidence:** pwgq `func/func_0x00a42a60.txt` (entry, 0x00d9e8b0, 0x0084a7c0); jxxz
`callees/func_0x00d9e8b0.txt`.

0x00d9e8b0: receiver = the caller's 4-byte local, stack (name, 0, -1), returns with a 12-byte purge, writes
the table-driven CRC-32 of the lowercased name into the receiver and returns the receiver pointer (null
string → writes 0). 0x0084a7c0(&hash): linear scan of the dword array at 0x0242dc90 (count at 0x0242dc88) for
an equal value, true if found — so the "active swaps" are a list of name hashes. Push boolean; return 1.
CONFIRMED — disassembly. The desk review's "not new" fix stands (spec-tables-traffic-ai.md and §27.5 already
have the receiver/out form).

**Spec text changes:** "OPEN — 0x0084a7c0's own internals" → "0x0084a7c0 scans the active-swap hash list at
0x0242dc90 (count 0x0242dc88)". Apply the desk review's wording fixes on "genuinely new".

## 28.19 `character_set_counter_on_grabbed` (0x00a421a0) — CORRECTED (label up; family now confirmed at bit level)

**Evidence:** pwgq `func/func_0x00a421a0.txt` (entry, 0x00947720); jxxz `callees/func_0x00947720.txt`.

- Wrapper: string; boolean with no nil gate (absent → false); the boolean byte is stored to a stack slot
  before the resolver and reloaded after (stale-slot delivery) — CONFIRMED. Resolver 0x00a281a0(name, 0).
  0x00947720 entered with receiver = character and one stack argument (the boolean); callee returns with a
  4-byte purge. Return 0. CONFIRMED — disassembly.
- 0x00947720: the double-gate (variant 1) template — 0x008ae480(+8/+0xc pair, 0), then 0x008837a0(). Either
  true → bit **0x200000** of the dword at +0x1c98 is set/cleared to the boolean. Both false → if
  0x008ae3a0(pair) is non-null, opens an opcode-0x46 record, writes the hashes (0x00d9e7e0(…, 0, -1) then
  0x004def50) of "human" and "human_force_flagscounter_on_grabbed", the 16-bit id pair, the boolean byte,
  and commits via 0x0086f110(target, 0, 0, 0). CONFIRMED — disassembly. This is exactly §20.26/§20.27's
  template (their bits 0x2000000/0x4000000 sit in the same +0x1c98 dword).
- The wrapper differs from §20.26/§20.27 only in the resolver (0x00a281a0, which tries 0x00a280c0 first and
  then falls back to the same 0x00a28150 chain they use) and in the boolean being mandatory. The desk
  review's conflict is therefore real at the wrapper level and absent at the delegate level.

**Spec text changes:** strike "though 0x00947720's own internal double-gate structure and exact bit were NOT
independently decompiled this pass (budget) — flagged as HIGH CONFIDENCE for the family membership, OPEN for
the exact bit/tag-pair" and the desk-review OPEN bracket → "0x00947720 is the same double-gate (variant 1)
template as §20.26/§20.27: direct path → bit 0x200000 of +0x1c98; replicate path → opcode 0x46, tags 'human'
+ 'human_force_flagscounter_on_grabbed'. The wrapper itself resolves via 0x00a281a0 (not 0x00a28150) and
takes a mandatory boolean (absent → false), unlike §20.26/§20.27's optional default-true boolean." Label:
"[CONFIRMED — disassembly, full chain including 0x00947720.]"

## 28.20 `character_remove_child_item_by_name` (0x00a452d0) — CORRECTED (0x00853b30 semantics; rest CONFIRMED)

**Evidence:** pwgq `func/func_0x00a452d0.txt` (entry, 0x00853b30, CRT stricmp, 0x00853ea0).

- Walk: head = character +0x1c; next = node +0x20, replaced by 0 when it equals the head (so the loop stops
  after one lap); every matching node is removed (no early exit). Arg 2 null → no walk. Return 0. CONFIRMED.
- **0x00853b30(obj, n) is a descriptor-bit test, not a per-entity or liveness gate:** it returns 1 iff bit
  (n & 7) of the byte at row +6 + (n >> 3) of the 0x02cc9900 row for obj's kind is set. n = 0x1f → bit 0x80
  at +9. n = 0x1e (§4.7's literal) → bit 0x40 at +9 — the very bit this loop also tests directly (and
  §15.28's parachute bit). CONFIRMED — disassembly. So the loop requires bit 0x40@+9 set **and** bit 0x80@+9
  clear.
- Name: node +0xe0 → pointer → first dword = string; compared with CRT `stricmp` (0x00eaa178). CONFIRMED.
- 0x00853ea0(node, 0, 0): if the node's kind has bit 0x2@+6, byte 0x024d4461 == 1, 0x008addb0(node, 1) is
  false, node +0x3c is non-null and arg 2 is 0 → returns without acting (a replication-owned case,
  HYPOTHESIS); otherwise classifies the node via 0x004571e0 against tags 0x011645cb / 0x011645cc, calls
  0x008537b0(node, 1 or 2) on 0x02442750, and finally 0x00457730(node, arg 3) on 0x02442750 — the
  singleton-level removal. CONFIRMED — disassembly for the structure; "destroy/detach" stays HIGH CONFIDENCE.

**Spec text changes:** strike "AND a per-entity gate (§4.7's wording) 0x00853b30(node, 0x1f) — the SAME gate
primitive already established at §4.7 as 0x00853b30(obj, literal), there cited with literal 0x1e; this is a
fresh sighting with a DIFFERENT literal, 0x1f, extending the known literal-code set for this gate family" →
"AND descriptor bit 0x80@+9 clear, tested through 0x00853b30(node, 0x1f) — 0x00853b30(obj, n) is a generic
'descriptor bit n' test (byte +6 + n/8, mask 1<<(n%8)); §4.7's literal 0x1e is bit 0x40@+9". Side effects:
strike "new literal-code sighting on the already-established 0x00853b30 gate family" → "0x00853b30 identified
as a descriptor-bit-by-index test (corrects §4.7's 'per-entity gate')". 28.26 item 13 likewise.

## 28.21 `character_get_gender` (0x00a43090) — CONFIRMED

**Evidence:** pwgq `func/func_0x00a43090.txt`. 0x00a281a0(name, 0); byte +0xa41 zero-extended (0 when the
resolve fails) and pushed as a number; return 1. The count helper is pushed its argument in the listing
although the decompile shows none. CONFIRMED — disassembly. Apply the desk review's citation fixes.

## 28.22 `character_evacuate_from_all_vehicles` (0x00a413d0) — CONFIRMED

**Evidence:** pwgq `func/func_0x00a413d0.txt` (entry, 0x00af3e10). Optional boolean, nil-gated, default
false; 0x00af3e10(character, NOT boolean, 0) — three stack arguments; return 0. CONFIRMED — disassembly. So a
call with no second argument forwards 1. 0x00af3e10 matches §21.11: it finds the occupant record through the
seat manager 0x02845cb0 (0x00b08380(char), 0x00b08390(pair)) and dispatches on the record's state dword at
+0x6a8 + slot×0x100: 1 → 0x00afe570(rec, slot, 8, arg 2); 2 → 0x00af24d0(char, arg 2); 3 → builds a local
descriptor (arg 2 into bit 1 of byte +0x32, arg 3 into bit 1 of byte +0x30 with bit 0 set) and calls
0x00af3cd0(char, &desc). CONFIRMED — disassembly. Keep §21.11's reading; add "absent arg 2 → forwards 1".

## 28.23 `cellphone_animate_start_do` (0x00a46550) — CORRECTED (argument meaning)

**Evidence:** pwgq `func/func_0x00a46550.txt` (entry, 0x007e2b60).

The wrapper pushes the literal 0, but **0x007e2b60 never reads its argument** (no stack-argument access in
its body; its other caller 0x00a380e0 passes it the same way). The callee: local player 1 via 0x009da4e0; if
non-null and 0x00943a20(player) is false: 0x0101b460(), then 0x004bf810("Cell Phone Answer") (an animation
looked up by name), then 0x004b1b90((player+0xd24)+0x10, thatAnim, -1, 0x80, 1.0, 1.0, -1.0). CONFIRMED —
disassembly for the chain and the unused argument; "0x004b1b90 plays the animation on the player's animation
controller (the same +0xd24→+0x10 object 28.12's rig loader writes to)" is HIGH CONFIDENCE; 0x00943a20's
meaning is OPEN.

**Spec text changes:** strike "starts a cellphone-animation state machine at a fixed initial stage (literal
0)" → "plays the 'Cell Phone Answer' animation on local player 1 (the pushed literal 0 is not read by
0x007e2b60; the play is skipped when 0x00943a20(player) is true)". Strike "OPEN — 0x007e2b60's own internal
state machine" → "OPEN — 0x00943a20 and the exact meaning of 0x004b1b90's trailing arguments".

## 28.24 `boss_battle_matt_get_cheat` (0x00a40b10) — CONFIRMED (OPEN closed)

**Evidence:** pwgq `func/func_0x00a40b10.txt`. 0x005e5ad0 reads the dword at 0x012ec730 — the "matt cheat
slot" sentinel §24.1 already names (-1 when idle); converted signed to double and pushed; return 1. CONFIRMED
— disassembly. Replace "presumably reading from the same sentinel-global cluster" → "reads 0x012ec730, the
cheat-slot sentinel of the §24.1 cluster (-1.0 when idle)".

## 28.25 `boss_battle_matt_cheats_start` (0x00a40a10) — CONFIRMED (callee described)

**Evidence:** pwgq `func/func_0x00a40a10.txt` (entry, 0x005e59f0); jxxz `callees/func_0x00d9e140.txt`,
`callees/func_0x00d2f560.txt`.

All four optional arguments nil-gated (type != nil, only when enough arguments); numbers rounded with
0x00ea2596; the clamp is a conditional move keeping values > -1 and forcing -1 otherwise; arg 4 default 1;
0x005e59f0(id, n2, n3, bool) with four stack arguments; return 0. CONFIRMED — disassembly.

0x005e59f0: **id == -1** → only while the counter 0x012ec720 is below the limit 0x012ec71c (static 4): calls
0x00d2f560 (a stub returning 1), sets byte 0x012ec724 = 1, and arms the deadline at 0x012ec728 to now +
([0x012ec714] = 5000 if the boolean is true, else 0). **id ≥ 0** → stores id/n2/n3 at
0x012ec734/0x012ec738/0x012ec73c, sets 0x012ec724 = 1, arms the deadline to now + [0x012ec718] (static
3000). 0x00d9e140(receiver = deadline dword, ms) writes clock (0x01320d94) + ms, wrapped at 1,800,000,000.
So the boolean only matters when id is -1, and n2/n3 only when id ≥ 0. CONFIRMED — disassembly.

**Spec text changes:** add the two-path description above to Side effects; "setting a cheat-code id plus two
further numeric parameters and a boolean (default true)" → "id ≥ 0: records id/n2/n3 and arms a 3000 ms
deadline; id = -1: when fewer than 4 have run, arms a 5000 ms (boolean true) or immediate (false) deadline".

## 28.26 Cross-function observations — fixes

- Item 1: as the desk review (§26.23/§27.5). Both instances CONFIRMED.
- Item 2: the six wrappers reach 0x00a1fc60 under the host check — family membership confirmed; the bases
  census: +0x1f00 (25), character +0xf8 (26), vehicle +0xb0 (12), trigger +0xf8 (2), kind 0x80@+0xa +0xc0
  (1), kind 0x40@+0xa +0x98 (2), kind 0x1@+0xb +0x90 (7). "Re-confirming +0xf8 … from fresh consumers" holds
  only for the character branch.
- Item 3: four explicit drops plus one hidden receiver — confirmed as the desk review counted.
- Item 6: the dword 3 — confirmed.
- Item 7: not new (desk review stands); 0x0084a7c0's list is a hash list.
- Item 8: the destination of the 0x00da4130 copy is the 36-byte orientation buffer; 0x01321400 is an identity
  3×3 (HIGH CONFIDENCE).
- Item 10: 0x0071f0d0 confirmed (descriptor bit 0x20@+9); 0x005eb390's non-singleton negative confirmed.
- Item 13: strike — 0x00853b30 is a descriptor-bit-by-index test; 0x1e = bit 0x40@+9, 0x1f = bit 0x80@+9.
  §4.7 should be corrected accordingly.
- Item 14: desk review's fix stands.

---

## Summary

| entry | verdict | what changed |
|---|---|---|
| 28.1 | CONFIRMED | 0x00b37740 described (single-gate; bit 0x8 @ AI-record +2; opcode 0x42) |
| 28.2 | CORRECTED | copy target is the 36-byte buffer; mode-true sends the orientation's third row; arg 3 default false; globals are runtime, not constants; 0x00b3f6d0 gated on +0x4b0 == 1 |
| 28.3 | CORRECTED | no §4.1 rounding; float written to 0x012ec280 |
| 28.4 | CONFIRMED | target is byte +0x57d of an object, mode-gated; not a bare global |
| 28.5 | CORRECTED | failure returns 0, nothing pushed; group failure does not short-circuit; no vtable+0x70 cast |
| 28.6 | CONFIRMED | bit 0x2@+0x3a not tested |
| 28.7 | CONFIRMED | containment callee 0x0093bfc0 (+0x8c shape dispatch); 47 goes through 0x00778bc0; box corners |
| 28.8 | CONFIRMED | only +0x16c0/+0x16c4 read; 0x00ad4040 defined |
| 28.9 | CORRECTED | arg 1 is a target name; return -1.0 / 0x00a48af0's count; fixed icon effect chosen by gamepad flag; 0x0052a940's 3rd arg is the effect index |
| 28.10 | CONFIRMED | byte global 0x0263ae3a |
| 28.11 | CORRECTED | recycle = row method 0x006fd4f0 (opcode 0x45/0x25 code 5), not the avoid-list mirror; role OPEN |
| 28.12 | CONFIRMED | tag strings "PLYF"/"PLYM", not resource pointers; host replication opcode 0x41 |
| 28.13/14 | CONFIRMED | byte global 0x012ecb34 (default 1) |
| 28.15 | CONFIRMED | 0x005f1cc0 host/client paths; bit 0x2@+0x7a; counter 0x014a0f80 |
| 28.16 | CORRECTED | handle is a definition row; one global state (0x012ec964); second start ignored |
| 28.17 | CORRECTED | four "getters" are kind predicates returning the same handle; wrappers = N-slot host-gated 0x00a1fc60 loops (25/26/12/2/1/2/7) |
| 28.18 | CONFIRMED | active-swap hash list at 0x0242dc90 |
| 28.19 | CORRECTED | label up to CONFIRMED: same double-gate template, bit 0x200000 of +0x1c98, tag "human_force_flagscounter_on_grabbed"; wrapper differs from §20.26/27 in resolver and boolean |
| 28.20 | CORRECTED | 0x00853b30 is a descriptor-bit-by-index test (0x1f = 0x80@+9; §4.7's 0x1e = 0x40@+9); 0x00853ea0 described |
| 28.21 | CONFIRMED | — |
| 28.22 | CONFIRMED | absent arg 2 forwards 1; 0x00af3e10 matches §21.11 |
| 28.23 | CORRECTED | literal 0 unused; plays the "Cell Phone Answer" animation on local player 1 |
| 28.24 | CONFIRMED | reads 0x012ec730 (§24.1 sentinel) |
| 28.25 | CONFIRMED | two paths in 0x005e59f0 (id -1 vs id ≥ 0), deadlines 5000/0 vs 3000 ms |
| 28.26 | CORRECTED | items 2, 8, 13 as above; others per desk review |

Counts: CONFIRMED 15 (28.1, 28.4, 28.6, 28.7, 28.8, 28.10, 28.12, 28.13, 28.14, 28.15, 28.18, 28.21, 28.22,
28.24, 28.25); CORRECTED 11 (28.2, 28.3, 28.5, 28.9, 28.11, 28.16, 28.17, 28.19, 28.20, 28.23, 28.26);
OPEN 0 at the entry level. No entry's registered name or address was wrong.

Cross-section corrections surfaced here: §4.7 (0x00853b30 is a descriptor-bit test, literal 0x1e = bit
0x40@+9); §2.6 (0x005bc5d0 is 0x0141250d's getter, with many callers); §1.12 gains a second 0x0093bfc0
sighting; §22.24 reconciled with 28.16 (one global continuous-explosion state).

## Next dumps (depth 1 unless noted)

1. 0x006fd4f0 and 0x006fbc90 — settle what "recycle" does to the object (28.11, the only role left OPEN).
2. 0x00778bc0 and the category table it reads — meaning of category 47 and code 0xc0069 (28.7).
3. 0x00dbdc90, 0x00dbe2a0, 0x00dbe050 — confirm the three trigger shapes behind +0x8c (28.7; also §1.12).
4. 0x00ad3b40, 0x00ad3c50 — the two predicates inside 0x00ad4040 (28.8).
5. 0x006d2910, 0x00614cb0, 0x00d34d40 — what object owns the +0x57d indicator byte (28.4).
6. 0x00943a20, 0x004b1b90 — the skip predicate and the animation-play call (28.23).
7. 0x00b98d00, 0x005eea30, 0x005e8fb0, 0x007161a0, 0x005f1690, 0x005f1750 — stronghold unlock helpers (28.15).
8. 0x00a79470 — the vehicle AI record getter shared by 28.1 and 28.2 (+2 bit 0x8, +0x298, +0x430, +0x4b0).
9. Optional: 0x004dcf00 and 0x0062a190 (the two halves of 0x00a281e0) if §4.5 does not already have them.
