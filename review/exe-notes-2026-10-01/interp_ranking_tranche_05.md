# Ranking tranche 05 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-05.json` (names 51-75 of the 554
unspecced names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t05b`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved in that one call** (`maxfuncs` is a per-name cap, not a cap on the
name list): every name string occurs exactly once in `.rdata`, is stored exactly once by the 1,014-entry gameplay
registrar `0x00a20840`, and its function pointer is the code pointer stored in the very next slot (insn offset +1) —
CONFIRMED — disassembly (dump `index.txt`). A second, depth-0 `func` run on 29 callee addresses filled in the bodies
the depth-1 dump stopped at; it is cited below as "follow-up dump".

Call counts are Team B's (`for-team-b/team-b/tools/lua_reconciliation_called_and_registered_1181.tsv`, total call sites
/ distinct scripts). All 25 are low-traffic (2-3 calls each).

Labels as in the other notes of this folder: **CONFIRMED — disassembly** = read in the listed instruction stream or the
decompile of the dumps; **HIGH CONFIDENCE** = follows from dumped instructions but a body it relies on was not dumped,
or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled.

**Shared conventions, cited not re-described:** every entry opens with the document's established `lua_gettop`
prologue (`0x00dfde50`) and reads its arguments with negative stack indices counted from the bottom (`-N` = arg 1,
`1-N` = arg 2, …), so an argument read without a presence check is simply the corresponding slot. Primitives as in
`spec-lua-api-behaviour.md` §4.1: `0x00dfe210` `lua_tolstring`, `0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type`,
`0x00dfe160` `lua_tonumber`, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`. "Standard nil-gated idiom"
means the §-preamble shape: only consulted when enough arguments were passed, and `lua_type == 0` (nil) takes the
default. A boolean read "without nil-gate" is a bare `lua_toboolean`, so an absent argument reads as `false`.
Resolvers: `0x00a281e0` vehicle-instance resolver (§4.5/§28.1), `0x00a281a0` generic/`#PLAYER#` character chain,
`0x00a28150` character chain (§3.4), `0x00a29200` general object resolver (§2.4), `0x005e4dd0` character by name,
`0x005982e0` the third-tier by-name resolver (descriptor bit `0x2`@`+0xb`), `0x0062a190` vehicle by name (bit
`0x8`@`+0xb`), all four of the last as methods on singleton `0x02442750`; `0x00853b10` liveness ("true = dead/invalid");
id-pair resolution `0x00458230` on `0x024433a8` with tag `0x031d152c` (§24.5). The record-and-replicate shapes are the
four of §7's preamble ("double gate `0x008ae480`/`0x008837a0`, string-tagged", "single gate `0x008addb0`, numeric-tagged",
"host-gated via `0x0087ba20`", "always both").

---

## A. Vehicle cluster — 11 functions

### A.1 `vehicle_is_helicopter` (`0x00a66070`) — 2 calls / 2 scripts

**Arguments:** 1 mandatory string (vehicle name), read without a nil check; a null string pointer short-circuits to
`false`.

**Return:** exactly 1 boolean on every path.

**Body:** does **not** use the §4.5 vehicle-instance resolver. It calls the by-name vehicle lookup `0x0062a190` on
`0x02442750` directly (name registry `+0x2660`, reject `+0x33` bit `0x10`, require descriptor bit `0x8`@`+0xb`), then
the liveness check `0x00853b10`, then the object's own virtual slot `+0x70` (the usual kind cast); a null cast result
reads `false`. The answer is `0x00ad3220(vehicle)`: true only when the dword at `(vehicle+0xbf4)+0x2c` equals **3**
(null vehicle → false). CONFIRMED — disassembly.

**Side effects/subsystem:** none, pure query. Consequences worth stating for an implementer: (a) a character name, or
`#PLAYER#`, never resolves here (the §4.5 chain's character-to-vehicle hop is not used), only a vehicle's own name;
(b) the field at `+0xbf4`→`+0x2c` is the same one §12.11's `0x00ad31a0` tests for "3 or 4" and §28.2 relies on — this
function accepts only 3, and `0x00ad3200` (A.2/A.3) accepts only 4, so **a VTOL (mode 4) is not a helicopter for this
query**, while §12.11/§28.2's helicopter commands accept both. CONFIRMED — disassembly (both predicates' bodies).
HYPOTHESIS: `+0xbf4` is the vehicle's class/definition record and `+0x2c` its flying-type enum (3 = helicopter,
4 = VTOL); only the values' use is confirmed.

### A.2 `vehicle_is_vtol_hover` (`0x00a64700`) — 2 calls / 1 script

**Arguments:** 1 mandatory string (vehicle reference), via `0x00a281e0`.

**Return:** **1 or 2 Lua values.** Failure or "not hovering": one `false`. Success: `true` **followed by `false`**,
return count 2. CONFIRMED — disassembly: the success branch pushes `true` and sets a counter to 1, then falls through
into the failure push of `false` instead of returning, and the function returns counter + 1. In `if
vehicle_is_vtol_hover(v) then` only the first value matters, so scripts see the intended answer; an implementation that
wants byte-for-byte stack behaviour must return the extra `false`.

**Body:** resolve; require `0x00ad3200(vehicle)` — the dword at `(vehicle+0xbf4)+0x2c` equals **4** (null → false); then
"hovering" = the dword at vehicle `+0x5e8` is **0 or 3**. CONFIRMED — disassembly.

**Side effects/subsystem:** none. HYPOTHESIS: `+0x5e8` is the VTOL flight-mode state (0 and 3 hover-like, 1 and 2
jet-like — see A.3); the four values' real meaning is OPEN.

### A.3 `vehicle_is_vtol_jet` (`0x00a64770`) — 2 calls / 1 script

Identical shape to A.2 (same resolver, same mode-4 gate `0x00ad3200`, same "push `true`, then fall through and push
`false`, return 2" defect on success); "jet" = vehicle `+0x5e8` is **1 or 2**. CONFIRMED — disassembly. A.2 and A.3 are
exact complements on a mode-4 vehicle only if `+0x5e8` never holds other values (OPEN); on a non-VTOL both answer a single
`false`.

### A.4 `vehicle_is_ready` (`0x00a62820`) — 2 calls / 1 script

**Arguments:** 1 mandatory string (vehicle reference), via `0x00a281e0`.

**Return:** exactly 1 boolean.

**Body:** unresolved → `false`. Otherwise the answer is the negation of `0x00ad3820(vehicle)`, the "not ready" predicate
§7.24's `0x00a3bae0` already calls without having opened it. `0x00ad3820` returns **true (not ready)** if
`0x008cc4d0(vehicle)` is true, or `0x0091f600(vehicle)` is true, or the vehicle pointer is null, or bit `0x8` of byte
`+0x33` is **clear**; it returns false (ready) only when both sub-predicates are false and `+0x33` bit `0x8` is set.
CONFIRMED — disassembly. (Sub-predicate bodies: see A.4a below.)

**Side effects/subsystem:** none, pure query. Cross-link: `+0x33` bit `0x8` is the same bit A.8's kneecapper setter
tests to decide between "apply now" and "defer" — strong evidence it means "the vehicle object is fully set up"
(HIGH CONFIDENCE from the two uses; not proven from the writer side).

**A.4a sub-predicates — follow-up dump.** `0x008cc4d0(v)` = bit `0x1` of byte `+0x3b`; `0x0091f600(v)` = bit `0x1` of byte
`+0x3a`. Neither checks for null (the null test in `0x00ad3820` comes after both, so a null vehicle would fault there —
unreachable from this caller, which tests the resolver result first). So **ready = `+0x3b` bit 0 clear AND `+0x3a` bit 0
clear AND `+0x33` bit `0x8` set**. CONFIRMED — disassembly. The meanings of the two low bits are OPEN.

### A.5 `vehicle_never_flatten_tires` (`0x00a62a60`) — 2 calls / 2 scripts

**Arguments:** 1 mandatory string (vehicle reference). 1 optional boolean, standard nil-gated idiom, **default true**.

**Return:** none (0 values).

**Body:** resolve via `0x00a281e0`; on success call `0x00a7b3b0` as a thiscall (receiver = the vehicle) with the
boolean. `0x00a7b3b0` is the §7-preamble **double-gate, string-tagged** setter: if `0x008ae480(vehicle +8/+0xc pair, 0)`
or `0x008837a0()` is true it writes the boolean into **bit `0x1` of vehicle byte `+0x1d7b`**; otherwise it resolves the
peer with `0x008ae3a0(pair)` and, if found, opens an opcode-`0x46` record tagged `"vehicle"` +
`"m_force_flagsnever_flatten_tires"` (one concatenated `.rdata` string, the §20/`spec-ai-behavior-format.md` naming
pattern), appends the vehicle's 16-bit network id (`0x0086f4b0`) and the boolean byte (`0x004d46e0`) and commits with
`0x0086f110` — without writing the bit locally. CONFIRMED — disassembly.

**Side effects/subsystem:** per-vehicle "tyres cannot be flattened" force-flag, replicated. Vehicle damage. The flag's
reader was not traced (OPEN; the name is the only evidence of its effect).

### A.6 `vehicle_set_weapons_disarmed` (`0x00a63520`) — 2 calls / 2 scripts

**Arguments:** 1 mandatory string (vehicle reference). 1 boolean, read **without** nil-gate (absent → `false`).

**Return:** none.

**Body:** resolve via `0x00a281e0`; on success thiscall `0x00a7d910(vehicle, boolean)`: the same double-gate shape as
A.5, local write = **bit `0x4` of vehicle byte `+0x1d7e`**, record tags `"vehicle"` + `"m_force_flagsweapons_disabled"`.
CONFIRMED — disassembly.

**Side effects/subsystem:** per-vehicle "weapons disabled" force-flag, replicated. The registered Lua name says
"disarmed", the engine's own flag name says "disabled" — same thing. Reader not traced (OPEN).

### A.7 `vehicle_set_no_chase` (`0x00a62b80`) — 2 calls / 1 script

**Arguments:** 1 mandatory string (vehicle reference). 1 boolean, **no** nil-gate (absent → `false`).

**Return:** none.

**Body:** resolve via `0x00a281e0`; on success thiscall `0x00b234f0` with receiver = **vehicle `+0xc68`** (the
vehicle-AI sub-record of §4.5/§9.4/§9.5/§14.2) and the boolean. `0x00b234f0` is the double-gate setter keyed on the
sub-record's owner id pair at sub `+0x4e0`/`+0x4e4`: local write = **bit `0x1` of sub-record byte `+5`** (vehicle
`+0xc6d`); record tags `"vehicle_ai"` + `"vai_force_flagsno_chase"`. CONFIRMED — disassembly.

**Side effects/subsystem:** vehicle-AI "no chase" force-flag, replicated. **Relation to §14.2:** §14.2's
`vehicle_disable_chase` reaches the very same setter `0x00b234f0` through `0x00b259c0` (two negations), so the two Lua
names write the same bit; the observable differences are only the default (`vehicle_disable_chase` defaults to `true`,
this one reads an absent argument as `false`) and §14.2's extra wrapper. CONFIRMED for the shared setter (this dump);
§14.2's wrapper path is as that section states (not re-read here).

### A.8 `vehicle_set_kneecappers` (`0x00a62fe0`) — 2 calls / 1 script

**Arguments:** 1 mandatory string (vehicle reference). 1 optional boolean, standard nil-gated idiom, **default true**.

**Return:** none.

**Body:** resolve via `0x00a281e0`; on success a plain cdecl call `0x00ab8940(vehicle, boolean)` (both on the stack —
CONFIRMED). `0x00ab8940`:
- **enable:** if the pointer at vehicle `+0x1550` is already non-null, nothing. Otherwise, if vehicle `+0x33` bit `0x8`
  is **clear**, it only sets **bit `0x8` of the dword at vehicle `+0x16cc`** and returns — a deferred request. If the bit
  is set, it calls `0x00ac3270`, stores the result at `+0x1550`, and if non-null calls `0x00ab8620(vehicle)`.
- **disable:** if `+0x1550` is non-null, calls `0x00ab6d30`.

`0x00ac3270` and `0x00ab6d30` are called with **nothing pushed**; the vehicle is live in `ESI` at both call sites, so
they most likely take it as a register argument (HIGH CONFIDENCE from the listing; see A.8a for their bodies).
CONFIRMED — disassembly for the branch structure.

**Side effects/subsystem:** attaches or removes a "kneecapper" (tyre-spike / wheel-disable) controller object at vehicle
`+0x1550`, with a deferred-enable bit `+0x16cc` bit `0x8` for vehicles not yet set up (A.4 cross-link). No replication
at this level (none of the §7 gates appears in `0x00ab8940`). Whether the deferred bit is later honoured is OPEN (its
reader was not traced).

**A.8a callees — follow-up dump.** CONFIRMED — disassembly unless noted:
- `0x00ac3270()` takes **no** argument (the `PUSH ESI` in its prologue is a register save): it pops a slot index from a
  48-entry free list (`0x02828de9`, count byte `0x02828de8`, refuses at 48), returns the 0x80-byte record at
  `0x028275e8 + index*0x80` after initializing it with `0x00a7a820`; null when the pool is exhausted (then `+0x1550` stays
  null and nothing else happens).
- `0x00ab8620(vehicle)` (enable, stack argument): only when the record's `+0xc` is 0; for each wheel index below
  `0x00ad6770(vehicle)` that `0x00ab7160(vehicle, i)` accepts it builds a small box (half-extent 0.25, `0x3e800000`) at that
  wheel's transform and creates a physics object for it (`0x00c1e1d0`), storing the results in the record. HIGH
  CONFIDENCE for "one collision box per wheel"; the per-wheel acceptance test `0x00ab7160` is OPEN.
- `0x00ab6d30` (disable) really takes the vehicle in **ESI** (its first instruction reads `[ESI+0x1550]` with no load —
  CONFIRMED register argument): with no record it only clears the deferred bit `0x8` of `+0x16cc`; otherwise, for each
  wheel, releases the stored physics object (`0x00c177b0`, `0x00be6100`), destroys an object whose id pair is stored at
  record `+0x38 + 8*i` when non-zero (`0x005cfe00(pair, 1, 1)`), then returns the record to the pool (`0x00ac32b0`) and
  nulls `+0x1550`.

So "kneecappers" = per-wheel spike/collision attachments drawn from a 48-vehicle pool (HIGH CONFIDENCE). Disabling a
vehicle that only had the deferred request clears that request.

### A.9 `vehicle_set_sirenlights` (`0x00a64490`) — 2 calls / 1 script

**Arguments:** 1 mandatory string (vehicle reference). 1 boolean, **no** nil-gate (absent → `false`).

**Return:** none.

**Body:** resolve via `0x00a281e0`; on success call `0x00ad55a0(vehicle +8, vehicle +0xc, boolean)` — the vehicle is
passed as its **id pair**, not as a pointer, and re-resolved inside: `0x00458230` on `0x024433a8`/`0x031d152c`, reject
`+0x33` bit `0x10`, require descriptor bit `0x80`@`+6` (the §24.5 vehicle-kind bit), liveness, and byte `+0xbd0 == 1`
(the §28.8 "body active" byte). Then it switches on the dword at `(vehicle+0xbf4)+0x4c4` (a vehicle-class enum):
- values **3, 5, 6, 7, 8, 11**: `0x00a7c660(boolean)` then `0x00a7c4f0(boolean)` (both thiscall on the vehicle);
- value **12**: `0x00a7c4f0(boolean)` only;
- any other value: if bit `0x2` of byte `+0x50` of the record at vehicle `+0xa88` is **clear**, it forces both off
  (`0x00a7c4f0(0)`, `0x00a7c660(0)`) and **returns without the final call**; if set, `0x00a7c4f0(boolean)` and
  `0x00a7c660(boolean)`;
- every path except that forced-off one then calls `0x00559b40(id low, id high, boolean)`.

CONFIRMED — disassembly for the gates, the argument shapes and the default branch; HIGH CONFIDENCE for the exact case
list (a compiler jump table at `0x00ad56a0`/`0x00ad56ac`, recovered by the decompiler, cross-checked against the two
call targets in the listing).

**Side effects/subsystem:** switches a vehicle's siren and/or emergency lights. HYPOTHESIS from the case split: classes
3/5/6/7/8/11 have both lights and siren, class 12 only one of the two, and other classes have them only when their
`+0xa88` record says so (else the call actively turns them off). Which of `0x00a7c4f0`/`0x00a7c660` is the siren and which
the lights, and what `0x00559b40` does with the id pair: see A.9a.

**A.9a callees — follow-up dump.** CONFIRMED — disassembly:
- `0x00a7c4f0(on)` is a double-gate, string-tagged setter: local write **bit `0x8` of vehicle byte `+0x1d7c`**, tags
  `"vehicle"` + `"m_force_flagssirenlights_on"`.
- `0x00a7c660(on)` is the same shape: **bit `0x10` of `+0x1d7c`**, tags `"vehicle"` + `"m_force_flagsheadlights_flash_on"`.
- `0x00559b40(low, high, on)` is the **siren sound**: resolves the pair with `0x004dcf00(pair, !on)`; when switching on
  (and `0x005598d0()` agrees) and `0x00559980(vehicle)` is true and `+0x163c` bit `0x10` is clear, it sets bit `0x1` of
  `+0x163d` and, unless a sound is already playing at `+0x1628`, starts the audio event hash `0x762b3179` on the emitter
  `+0x159c` (through `0x00ab5070`/`0x005566d0`/`0x0045ea70`); otherwise it clears bit `0x1` of `+0x163d` and stops
  (`0x0045f1a0`) and forgets the sound at `+0x1628`.

So the class switch reads: classes 3/5/6/7/8/11 → flashing headlights **and** siren lights; class 12 → siren lights only;
other classes → both only if their `+0xa88` record allows (bit `0x2` of `+0x50`), else both forced off and no sound
change. Then the siren sound follows the boolean.

### A.10 `vehicle_set_ambient` (`0x00a62ad0`) — 2 calls / 2 scripts

**Arguments:** 1 mandatory string (vehicle reference). 1 optional number, standard nil-gated idiom (`lua_tonumber`,
kept as a single-precision float, no integer conversion), **default −1.0** (the shared `0x012a2d54` constant).

**Return:** none.

**Body:** resolve via `0x00a281e0`; unresolved → nothing. Otherwise, in this order:
1. sets **bit `0x1` of the dword at vehicle `+0x16c0`** directly (no gate, no replication);
2. calls `0x00b63ac0(vehicle+0xc68, 0, 0, 0)` — the vehicle-AI sub-record's mode setter (described below);
3. if the number is **≥ 0.0** (compare against the `0.0` constant at `0x0125e270`), calls the §9.5 speed setter
   `0x00b29ca0(vehicle+0xc68, number, 0)`: clamp to at most 1000.0, clear bit `0x10` of sub byte `+0xa`, store at sub
   `+0x9c`, then `0x00b26880(1)` on the sub-record. CONFIRMED — disassembly.

`0x00b63ac0(sub, target, flagA, flagB)` (here all three zero): reads the owner pair at sub `+0x4e0`, maps it through
`0x00ab63f0` and stops if the descriptor-bit test `0x00853b30(obj, 0x21)` is true. Not authoritative
(`0x008ae480(owner, 0)` false) → opcode-`0x42` record with 8-bit sub-code **12**, the owner id, a payload written by
`0x00b1bad0(target, …)`, sent with `0x0086f110` to the peer from `0x008ae3a0`. Authoritative → if (flagA is 0 and sub
byte `+7` bit `0x4` is set) or the global `0x02705fe0` is null or points at 0, it calls `0x00b46ca0(sub)` instead;
otherwise (after `0x00b62be0` when the sub's first byte is 6) it calls `0x00b26250(sub, 6, 0x11)`, resets three
sub-objects (`+0x534`, `+0x538`, timer `+0x450`, `+0x57c`), stores the target at `+0x274`, writes flagA/flagB into bits
0/1 of `+0x26c` and calls `0x00ad57a0(owner, 0)`. CONFIRMED — disassembly for the structure.

**Side effects/subsystem:** returns a scripted vehicle to "ambient" AI: marks the vehicle (bit `0x1`@`+0x16c0`), puts its
AI sub-record into mode 6 with sub-mode 0x11 and no target (HYPOTHESIS: mode 6 = ambient/traffic driving), and
optionally caps its speed. **Unit note:** unlike §9.5's `vehicle_speed_override`, which multiplies its argument by the
mph→m/s factor 0.44704 before calling the same `0x00b29ca0`, this function passes the Lua number **unconverted**, so its
speed argument is already in the engine's internal unit (CONFIRMED — no multiply between `lua_tonumber` and the call).
The meaning of mode 6 / sub-mode 0x11 is OPEN. The fallback `0x00b46ca0(sub)` (follow-up dump), when authoritative,
clears the AI's current order (`0x00b24e40(0, 0)`, plus `0x00ab4760` when the vehicle's `0x00ad2f80` mode is 1) and calls
`0x00b2e510(sub, 0, 0, 0)`; otherwise it sends opcode `0x42` sub-code 2. CONFIRMED for the structure; meaning OPEN.

### A.11 `vehicle_spotlight_is_target_spotted` (`0x00a65e70`) — 2 calls / 1 script

**Arguments:** 1 mandatory string (vehicle or driver reference). 1 mandatory string (target name), read only after
arg 1 resolved.

**Return:** exactly 1 boolean.

**Body:**
1. Arg 1 first goes through the character chain `0x00a281a0(name, 0)`. If that finds a character, the vehicle is the
   one cached on the character — its "active" pair `+0x16d8`/`+0x16dc`, or, when that pair is zero, `+0x16c0`/`+0x16c4`
   (the §24.5 order) — resolved via `0x00458230` with the §24.5 gates (`+0x33` bit `0x10` clear, descriptor bit
   `0x80`@`+6`, liveness). A character with no live vehicle answers `false`; it does **not** fall back to the vehicle
   resolver. Only when no character matched is arg 1 passed to the vehicle resolver `0x00a281e0`.
2. Arg 2 → general object resolver `0x00a29200` (an extra literal `1` is pushed but `0x00a29200` reads only one
   argument — a dead parameter, CONFIRMED from its body).
3. Requires vehicle byte `+0xbd0 == 1` and a non-null spotlight object at vehicle `+0xa44`; otherwise `false`.
4. **Writes the target's id pair (`+8`/`+0xc`) into the spotlight's `+0x10`/`+0x14`** — the query re-aims the light.
5. Answers `0x00aadfd0(spotlight)` (thiscall), a visibility test: builds the light's transform from
   `(spotlight+0xc)+0xb8` (orientation) and `+0xdc` (position) via `0x004aac40`; resolves the stored target pair (must
   pass `+0x33` bit `0x10` clear, descriptor bit **`0x2`@`+6`**, liveness); takes the target position minus the light
   position; **false if the distance exceeds 80.0** (`0x01133abc`); if the dot product of the light's forward axis with
   that offset is below 1.0 it takes an arc-cosine-like value (`0x00dc0d80`) and answers **false when that angle exceeds
   0.17453 rad (10°)** (`0x01154224`); finally it casts a ray (`0x0075d7e0`, filter callback `0x00aac230`, ignoring the
   vehicle's and the target's ids) from the light to the target's position raised by **1.85** on the second axis
   (`0x0130da3c`) and answers **true only when the ray is not blocked**.

CONFIRMED — disassembly for steps 1-4 and for the constants and the ray call; HIGH CONFIDENCE for the reading of step 5
as "range 80, 10° cone, unobstructed line of sight to a point 1.85 above the target's origin" (the helpers `0x004d6300`,
`0x00dc0d80`, `0x0075d7e0` were not opened; in particular whether `0x004d6300` normalizes the offset in place, which the
"< 1.0 then angle" test suggests, is HYPOTHESIS).

**Side effects/subsystem:** **not a pure query** — it sets the spotlight's target to arg 2 as a side effect, before
testing. Vehicle spotlight (police/helicopter searchlight). Descriptor bit `0x2`@`+6` as the "target kind" requirement
matches §28.9's target resolver.

### A.12 Vehicle-cluster observations

1. Two vehicle "kind" predicates read the same field `(vehicle+0xbf4)+0x2c` with different accepted values: `0x00ad3220`
   (== 3, A.1), `0x00ad3200` (== 4, A.2/A.3), and §12.11's `0x00ad31a0` (3 or 4). CONFIRMED.
2. **Two stack-discipline defects** of the same kind: A.2 and A.3 push a stray `false` after a successful `true` and return
   2. CONFIRMED — disassembly. Same family as C.3's multi-push below.
3. Three more members of the §7 double-gate, string-tagged family with their exact bits: `+0x1d7b` bit `0x1` (never
   flatten tyres), `+0x1d7e` bit `0x4` (weapons disabled), vehicle-AI sub `+5` bit `0x1` (no chase).
4. `vehicle_is_helicopter` is the only name in the cluster that bypasses `0x00a281e0`; scripts that pass a driver's name
   get `false` (A.1).

---

## B. AI cluster — 4 functions

All four resolve their first argument through the character chain `0x00a28150` (§3.4: `#FOLLOWER#`-family index first,
then `0x005e4dd0`, liveness, vtable `+0x70` cast).

### B.1 `ai_clear_priority_target` (`0x00a3c320`) — 3 calls / 2 scripts

**Arguments:** 1 mandatory string (character).

**Return:** none.

**Body:** on resolve, thiscall `0x004e7b00` with receiver = **character `+0x2b0`** (the per-character AI-data
sub-object of §3.4/§15.4) and one explicit argument, the address of the `.rdata` pair `0x0117f808`/`0x0117f80c` (both
dwords 0 — the null id pair). `0x004e7b00(newPair)` is the setter of the AI record's **forced target**:
- it reads the owner's id pair at AI `+0x610`/`+0x614` (= character `+0x8c0`/`+0x8c4`) and does nothing when that pair
  is zero or does not resolve (`0x00458230`, `+0x33` bit `0x10` clear, descriptor bit `0x8`@`+6`). The following
  liveness call `0x00853b10` has its result **discarded** (no test after it) — CONFIRMED from the listing;
- `0x008ae480(owner, 0) == 1` (authoritative) → writes the new pair into AI `+0x10`/`+0x14` (character
  `+0x2c0`/`+0x2c4`) and then, if both `0x004e55c0(0x004d2f30(owner, 0))` and `0x004e4ce0(newPair, 1)` return non-null,
  calls `0x005023f0` on the first with the second (a notification of the target change; HYPOTHESIS);
- otherwise → `0x008ae3a0(owner)` peer; opcode-`0x46` record tagged `"human_ai_data"` + `"forced_target_handle"`,
  appending the owner id and the new target id (`0x0086f4b0` twice), committed with `0x0086f110`.

CONFIRMED — disassembly. Note the gate here is `0x008ae480` **only** (shape 3 of §7's preamble, previously seen only in
the leash functions), not the two-gate shape.

**Side effects/subsystem:** clears a character's AI forced/priority target (sets it to the null pair), locally or by
replication. The `.rdata` string dumped at `0x01116418` shows three garbage bytes before `forced_target_handle`; the
record tag is the string the call actually pushes, `0x01116418`, whose readable tail is `forced_target_handle` (the
dump's string reader started early — HIGH CONFIDENCE, not material).

### B.2 `ai_set_in_scripted_cover` (`0x00a3c3a0`) — 3 calls / 1 script

**Arguments:** 1 mandatory string (character). 1 boolean, **no** nil-gate (absent → `false`).

**Return:** exactly 1 boolean: `false` when the character does not resolve, `true` otherwise. CONFIRMED.

**Body:** on resolve, thiscall `0x004e1ee0(AI = character+0x2b0, boolean)` — the double-gate, string-tagged setter:
local write = **bit `0x1` of AI byte `+0xc`** (character `+0x2bc`; the same byte §3.4's ignore-AI flag uses bit `0x2`
of), record tags `"human_ai_data"` + `"ai_force_flagsin_scripted_cover"`. When the boolean is **false** the function
additionally calls `0x004f4b90(character, 13, 0, 0, 0, 0)` (the two zero dwords come from the same null pair
`0x0117f808`), a scripted-action setter: for an action id below 70 and when character byte `+0x44c` bit `0x8` is clear
(or the fifth argument is non-zero) it sets bits `0x18` of `+0x44c`, clears bit `0x80` of `+0x4a6` if the id differs from
the current `+0x445`, re-arms timer `+0x440` with 0, re-initializes the 0x58-byte request block at `+0x4a8` from the
template `0x012e14e0`, stores id 13 at `+0x4a8`, the pair at `+0x4b0`/`+0x4b4`, and packs the fifth argument into the
top bits of `+0x4fe`; with a zero sixth argument and zero fifth argument it then consults `0x008addb0(character, 0)` and
does nothing more. CONFIRMED — disassembly for `0x004f4b90`'s body.

**Side effects/subsystem:** sets/clears the character's "in scripted cover" AI force-flag, replicated; on clear, also
queues scripted action **13** with no target (HYPOTHESIS: "leave cover" / idle). Sibling of §18.3
`ai_do_scripted_take_cover` (action 8). The meaning of action 13 is OPEN.

### B.3 `ai_pay_attention_to_position` (`0x00a3d720`) — 3 calls / 1 script

**Arguments:** 1 mandatory string (character). 1 mandatory string (object whose position is used) — via the general
object resolver `0x00a29200` (extra literal `1` pushed and ignored, as in A.11). 1 optional boolean, standard nil-gated
idiom, **default true**.

**Return:** none.

**Body:** both must resolve, else nothing. Then the single gate `0x008addb0(character, 0)` (§7 preamble shape 2):
- **true (apply locally):** copies the object's position `+0x40`/`+0x44`/`+0x48` into a 16-byte vector (helper
  `0x00da38e0`, which writes x, y, z and repeats z in the fourth lane) and calls `0x004fad60(vector, boolean, 3, 0)` as a
  thiscall on **character `+0x510`**: it stores the vector at sub `+0x270` (character `+0x780`..`+0x78c`) and then calls
  `0x004f6e10(code, 3)` on that same `+0x270` block with **code = 7 when the boolean is true, 3 when false**; if that
  call reports failure it retries with code **8**. The fourth argument is 0, so the optional `0x004f6800` step is
  skipped. CONFIRMED — disassembly.
- **false (not authoritative):** opcode-`0x43` record, 8-bit sub-code **50** (`0x32`, written by `0x00898cb0`), then the
  character's and the object's 16-bit network ids (`0x0050f790` each) and the boolean byte, committed through the
  host-session path (`0x0087ba20` → `0x0086f1b0`). CONFIRMED — disassembly.

**Side effects/subsystem:** makes an NPC attend to (look at / face) a world position taken from a named object, once;
the position is copied, not tracked. The meaning of codes 3/7/8 of `0x004f6e10` is in B.3a. Bit `0x4` of the code is
exactly the Lua boolean (7 = 3 | 4).

**B.3a `0x004f6e10(code, sub)` — follow-up dump.** A thiscall on the 0x70-odd-byte attention block (character `+0x780`;
current code at block `+0x22`, a variant byte at `+0x23`). The codes come in pairs 0/4, 1/5, 2/6, 3/7 where bit `0x4` is
passed on to the per-pair helper as a flag; 8-11 are separate modes (8 resets the block's targets and timers to a neutral
state; 9/10/11 set fixed angles such as `0x3f490fdb` = π/4 and `0x3fc90fdb` = π/2). With `sub` = 3 the variant byte
`+0x23` becomes 1 for codes 5-7 and 0 otherwise. Codes **3/7** call `0x004f6b60(block, code == 7, 0)` — the helper that
uses the position just stored at the block's start — record the code at `+0x22` and **always return 1**, so the
fallback to code 8 in `0x004fad60` is never taken from this caller. CONFIRMED — disassembly. So `ai_pay_attention_to_position`
= "attention mode 3 (position) on the copied point", with the Lua boolean selecting the bit-`0x4` variant (true → 7,
variant byte 1). What the variant changes inside `0x004f6b60` is OPEN (HYPOTHESIS: head-only vs. whole-body facing).

### B.4 `ai_do_scripted_rush` (`0x00a3e870`) — 3 calls / 3 scripts

**Arguments:** 1 mandatory string (character). 1 optional string (rush target name), standard nil-gated idiom, default
none.

**Return:** exactly 1 boolean on every path.

**Body:** character not resolved → `false`. Then:
- **no target given:** `0x00539d90(character, character+0x4a8)` decides whether a rush is possible against the
  character's current target (`0x004e4e50(character)`; dumped in the follow-up, see B.4a): it computes a destination
  near that target through the AI navigation object at character `+0x510` (`0x004fe390`), refuses when
  `0x004de3f0(character, target id pair, 500)` is true, when the target's dword `+0xe8` has bit `0x100`, or when
  `0x004e91c0(character, destination, 0)` fails; if the character's current scripted action (`+0x445`) is already
  **22** it accepts at once; otherwise it accepts only if the squared distance it measured (`0x00509ba0`) is at least the
  square of a radius taken from `(0x00941f40(character))+0x19c`→`+0x120` when present (else a value from
  `0x004dd530`), **clamped below at 10.0** (`0x01118d20`). On acceptance it stores the destination at request `+0x18`..`+0x20`
  (character `+0x4c0`..`+0x4c8`). If accepted, `0x004f4d00(character, 22, 0)` — the §7.24/§18.3 scripted-action-slot
  setter with action **22** — and the result is pushed.
- **target given:** the name is tried as a character (`0x005e4dd0`, liveness, vtable `+0x70` cast must be non-null — the
  cast is called twice) and then as a third-tier object (`0x005982e0`, liveness); the first that resolves supplies its
  position `+0x40`..`+0x48`, copied straight into character `+0x4c0`..`+0x4c8`, then `0x004f4d00(character, 22, 0)` and
  `true`. Neither resolves → `false`.

CONFIRMED — disassembly for both branches and for `0x004f4d00`; HIGH CONFIDENCE for the reading of `0x00539d90`'s helpers
(none of `0x004fe390`, `0x004de3f0`, `0x004e91c0`, `0x00509ba0`, `0x004dd530`, `0x00941f40` was opened).

**Side effects/subsystem:** starts scripted action **22** ("rush") toward either the character's current combat target
(subject to the reachability/distance checks) or a named object's/character's current position. Exact twin of §18.3
`ai_do_scripted_take_cover` (action 8, `+0x4b0`/`+0x4b4`), with a position triple at `+0x4c0` instead of an id pair. The
`0x004f4d00` body read here: action id < 70; with the third argument 0 it sets bit `0x40` of `+0x4fe` and stores
`0x006f62b0(character, 3, 0)` at `+0x4aa`; then `0x008addb0(character, 0)` false → `0x004f4830(character, 0, action,
3000)` (the request path) and return; true → applies locally (copies the request block `+0x4a8` to `+0x450`, sets
`+0x444 = 0x12`, `+0x445 = action`, re-arms two timers, and calls `0x004e4130` with a per-action byte from the table at
`0x012e1c64`, stride 24). CONFIRMED.

**B.4a `0x004e4e50(character)` — follow-up dump.** The "current target" used by the no-target branch: none if character
byte `+0x2ba` has bit `0x4`; otherwise the id pair at character `+0x1330`/`+0x1334`, resolved via `0x00458230` and
accepted only if `+0x33` bit `0x10` is clear, the descriptor has bit `0x1`@`+0xa`, it is live (`0x00853b10`), not dead
(`0x0096f4f0`), `+0x3b` bit 0 clear (`0x008cc4d0`, A.4a) and `+0x33` bit `0x8` set. CONFIRMED — disassembly. So "rush
with no target" means "rush the current combat target if it is alive and set up".

---

## C. Action cluster — 3 functions

### C.1 `action_play_synced_do` (`0x00a3fe10`) — 3 calls / 2 scripts

**Arguments:** 3 mandatory strings — actor 1, actor 2, synced-action name — read without nil checks. 1 optional string
(an anchor object name), standard nil-gated idiom, default none.

**Return:** exactly 1 number: **−1** if both actors resolved and actor 1 is dead; **1** if the final play call
succeeded; **0** otherwise. CONFIRMED.

**Body:**
1. Actors 1 and 2 through `0x00a281a0(name, 0)`; the anchor through `0x005982e0` + liveness (dead/absent → none).
2. Only the combination "both resolved and actor 1 dead (`0x0096f4f0`)" short-circuits (to −1). **An unresolved actor
   does not stop the function**: it continues with a null handle. CONFIRMED — disassembly (the null test jumps into the
   main path, not to the exit).
3. `0x0095da50(name)` turns the **synced-action name (arg 3)** into an index: it hashes the name (`0x00d9e8b0(name, 0,
   −1)`, thiscall into a local) and scans the global table at `0x02624b50` (count `0x02624b54`, entries 0x30 bytes, hash at
   entry `+0x10`), returning the matching index or `0xffff`. CONFIRMED — disassembly.
4. If the global byte `0x024d4461` is set (the same multiplayer-flag §14.1 gates on): opcode-`0x43` record, 8-bit
   sub-code **19**, the two actors' network ids (`0x0050f790`), the anchor's id pair (or the null pair `0x0117f890`)
   as a 16-bit id via `0x0086f4b0`, and the 16-bit action index, committed via the host-session path
   (`0x0087ba20` → `0x0086f1b0`). This is **in addition to**, not instead of, step 5.
5. Local play: with an anchor, `0x004aac40` builds a transform from the anchor's orientation `+0x4c` and position `+0x40`
   and the call is `0x0095fc00(actor1, actor2, index, &transform, 1)`; without, `0x0095fc00(actor1, actor2, index, 0, 1)`.
   Its true result gives the 1.

**Side effects/subsystem:** starts a two-actor synced animation, optionally placed at an anchor, and (in multiplayer)
broadcasts it. `0x0095fc00`'s body was not opened (OPEN), so what a null actor does inside it is OPEN.

**Implication for §14.1 (`action_play_synced_state`, `0x00a3d3e0`):** §14.1 describes `0x0095da50` as "matching arg 1's
identity against a global table of currently-active pairings" via a hidden argument, and calls its own arg 3 "read but
inert". The body read here shows `0x0095da50` is a **name → synced-action-definition index** lookup taking the name as
its one stack argument. See C.1a for the check of §14.1's own call site.

**C.1a §14.1's own call site — follow-up dump of `0x00a3d3e0`.** At `0x00a3d48b`/`0x00a3d48f` §14.1's function pushes the
frame slot that `0x00a3d456` filled with its **third** `lua_tolstring` result and calls `0x0095da50` with it. So in §14.1
too, arg 3 is the synced-action name and is **not** inert, and the lookup is by name, not by arg 1's identity; there is no
hidden argument. CONFIRMED — disassembly (stack-offset bookkeeping across the `0x38`-byte cleanup at `0x00a3d469`). §14.1
also differs from C.1 in one respect worth keeping: there an unresolved actor short-circuits to −1 (the null tests at
`0x00a3d470`/`0x00a3d474` jump to the −1 exit), whereas C.1 carries on with a null handle. **This is a correction to
`spec-lua-api-behaviour.md` §14.1 (Arguments and Body), to be applied separately; not applied here.**

### C.2 `action_play_directional_stumble_do` (`0x00a3f7d0`) — 3 calls / 2 scripts

**Arguments:** 1 mandatory string (character). 1 mandatory string (the object the stumble is relative to — via
`0x005982e0`). 1 optional boolean, standard nil-gated idiom, **default false** (selects between two animation-id sets).

**Return:** **a variable number of numbers, one per push; the C return value is the push count.** The function keeps a
counter, never returns early, and pushes:
- **−1.0** (constant `0x012a3038`) for each of: character unresolved; object unresolved or dead; character dead
  (`0x0096f4f0`); animation id not found on the character (`0x004b1630` returns −1);
- **0.0** when the final play call `0x00959b50` fails;
- **1.0** always, last.

So a clean success returns the single value 1.0; a dead character returns (−1.0, 1.0) **and still plays the stumble**;
an animation-lookup miss returns (−1.0, …, 1.0). CONFIRMED — disassembly (counter at frame `+0x20`, incremented after
each failure push, return = counter + 1).

**Null-pointer hazard:** because nothing returns early, an unresolved **object** leads to a read of `[0 + 0x64]` at
`0x00a3f91e`, and an unresolved **character** to reads through a null character (`0x00a3f952` reads `+0x68`, after
`0x009c07a0` and `0x00957c80` were already called with it). CONFIRMED from the listing: no null test guards those reads
(only the descriptor-bit test at `0x00a3f8f5` is guarded). In the shipped game this is an access violation; an
implementation should treat both failures as script errors rather than reproduce the crash.

**Body (normal path):**
1. `0x009c07a0(character)` — clears some attack state (sets bit `0x80` of `+0x1a10`, resets an AI field, re-arms a
   5000 ms timer, plays a fixed sound hash `0xc9c88782`; HYPOTHESIS for the reading as "interrupt current action").
2. `0x00957c80(character, 0.2, 0, 0, 0)` — blends out the character's current animation layers over 0.2 (constant
   `0x01171540`); in multiplayer (`0x024d4461`) with either flag set it would also send an opcode-`0x41` record (both
   flags are 0 here, so it does not).
3. If the character's descriptor has bit `0x2`@`+0xa` (the §7.21 "player-like" bit): thiscall `0x009e3200(character, 2,
   0)` (sets a script-mode value 2 at `+0x2888`, locally or via an opcode-`0x46` `"player"`/`"script_mode_params"`
   record) and `0x009dcf10(character)`.
4. **Direction:** the object's third orientation row (`+0x64`/`+0x68`/`+0x6c`) is flattened to the horizontal plane and
   normalized (`0x00d9ffe0`: drops the second component, divides by the length of the other two, falls back to (1, 0, 0)
   when the length is ~0). Its dot product with the character's own third row (`+0x64`..`+0x6c`) is compared against
   **0.70710677** (`0x01115258`, cos 45°).
5. **Animation id:** with the boolean **true**: `0x2a0` when the dot ≥ 0.7071, else `0x2a1`; with **false**: `0x497` /
   `0x498`. The code also contains ids `0x2a3`/`0x2a4` and `0x499`/`0x49a` (chosen by a second dot product, the
   character's first row with its own third row), but **that branch is unreachable for any non-NaN input**: it is entered
   only when the dot is both below 0.7071 and above 0.7071 (the second comparison re-tests the same constant with the
   operands swapped). CONFIRMED — disassembly (`0x00a3f9ed`..`0x00a3fa3c`). HYPOTHESIS: the source compared against
   −0.7071 there, intending front / back / left / right; as shipped only "front" and "not front" exist.
6. The id is checked on the character's animation controller (`0x004b1630((character+0xd24)+0x10, id)`, §22.29), and
   then played with `0x00959b50(character, id, 0x1da1, 0, 0, 1.0, −1.0, 1, 0)` — the action-slot id `0x1da1` also seen in
   §9 (−1.0 is the shared constant `0x012a2d54`).

**Side effects/subsystem:** interrupts the character's current action and plays a "stumble" reaction whose animation
depends on whether the reference object faces the same way as the character. Character action/animation.

### C.3 `action_sequence_end` (`0x00a3c270`) — 3 calls / 2 scripts

**Arguments:** none read (the `lua_gettop` prologue only).

**Return:** none.

**Body:** calls `0x006dc930()`, a global "end the current scripted sequence" routine:
1. if the global `0x012f2af0` is 0 it first calls `0x006dc820()`; then `0x006dc310()` on the object `0x012f2ae0`;
2. if the id pair `0x014e95e0`/`0x014e95e4` is non-zero, `0x005cfe00(pair, 0, 1)` and the pair is reset from the null
   constant `0x0113b620`;
3. for each of the two local-player accessors `0x009da4e0()` and `0x009df3d0()` that returns a player,
   `0x0094b070(player, &(−1))` (HYPOTHESIS: clears a per-player scripted-camera/target field to −1);
4. if `0x012f264c` is not −1, `0x007efbd0(value, 0)` and the global is set to −1;
5. `0x0101bac0()`;
6. **host only** (session `+0x5c` == `+0x58`, §8.27): opcode-`0x49` record with one byte, value **2**, sent through the
   host-session path.

CONFIRMED — disassembly for the sequence of calls and gates; the identities of `0x006dc820`, `0x006dc310`, `0x005cfe00`,
`0x0094b070`, `0x007efbd0` and `0x0101bac0` are OPEN.

**Side effects/subsystem:** global cutscene/action-sequence teardown, broadcast to clients by the host (opcode `0x49`,
sub-value 2). HYPOTHESIS for "sequence" = an in-engine scripted cutscene sequence; the only other caller of
`0x006dc930` is `0x006de560`, in the conversation cluster of §2.10 (`0x006deXXX`).

---

## D. Audio cluster — 4 functions

### D.1 `audio_set_listener_override` (`0x00a3dc70`) — 3 calls / 2 scripts

**Arguments:** 1 mandatory string (object name), via the general object resolver `0x00a29200` (an extra literal `1` is
pushed and ignored, as in A.11).

**Return:** none.

**Body:** resolve; require descriptor bit **`0x8`@`+6`** of the resolved object; then `0x005554b0(object +8, object
+0xc)`. `0x005554b0(low, high)` stores the id pair into the globals **`0x013bb5b0`/`0x013bb5b4`**, converts it to a 16-bit
network id (`0x008ae2b0`) stored in the word **`0x013bb634`**, and **always** sends an opcode-**`0x45`** record carrying
that 16-bit id through the host-session path (`0x0087ba20` → `0x0086f1b0`) — no authority gate, no local/remote split
(§7 preamble shape 4, "always both"). CONFIRMED — disassembly.

**Side effects/subsystem:** makes the audio listener follow the named object. Reader side, from the follow-up dump:
- `0x00555f20` (a per-frame snapshot routine): if the pair at `0x013bb5b0` resolves to a live object with bit `0x8`@`+6`,
  it copies that object's position `+0x40`..`+0x48`, its orientation rows `+0x4c`..`+0x6c` and the vector at
  `+0x8c`..`+0x94` into the block `0x013bb77c`..`0x013bb7b4`, and clears the 16-bit id; if the pair is empty but the
  16-bit id is non-zero, it rebuilds the pair from the id with `0x008adfc0` — so a peer that only received the
  network id resolves it lazily. CONFIRMED — disassembly.
- `0x005557c0` (the listener update): when the override pair resolves it takes the listener position/orientation from
  the override block (`0x013bb6bc`..`0x013bb6f8`) instead of the player/camera paths. CONFIRMED — disassembly for the
  branch; the block-to-block plumbing between the two routines (`0x005552a0`, `0x013bb700`) was not traced (HIGH
  CONFIDENCE that it is the snapshot above).

Descriptor bit `0x8`@`+6` is also the kind required of the AI owner in B.1 and of the override object by the reader
above; HYPOTHESIS: it is the "human/character" kind bit, so in practice the override accepts characters only.

### D.2 `audio_clear_listener_override` (`0x00a3c860`) — 3 calls / 2 scripts

**Arguments:** none read.

**Return:** none.

**Body:** `0x005555e0()`: copies the null id pair from `.rdata` `0x011196a8`/`0x011196ac` (both 0) into
`0x013bb5b0`/`0x013bb5b4`, zeroes the 16-bit id `0x013bb634`, and sends the same opcode-`0x45` record with id 0 through
the host-session path. CONFIRMED — disassembly. Exact inverse of D.1; the listener returns to its default source on the
next update (D.1's reader falls through to the non-override branch).

### D.3 `audio_play_for_navpoint` (`0x00a3edf0`) — 3 calls / 1 script

**Arguments:** 1 mandatory string (sound/event name), 1 mandatory string (navpoint/object name), 1 optional string
(standard nil-gated idiom) that is **read and discarded** — its value is never used (CONFIRMED: the third
`lua_tolstring` result is not stored).

**Return:** exactly 1 number: the playing instance's id, or **0** on any failure. CONFIRMED.

**Body:** arg 2 → `0x005982e0` + liveness; arg 1 → `0x00462960` (§2's Wwise name → id conversion, through `0x0046fd00`);
a zero id fails. Then a local play-request block is built — event id, the object's id pair `+8`/`+0xc`, five zero
dwords and a final byte `1` — and passed to **`0x00a2acb0`** (the shared audio-play core of §2.4/§2.5, whose callers
include `audio_object_post_event` `0x00a3cb00`): it starts the event via `0x00a2a850(request, 1)` and returns the
request's `+0x24` field (the playing id). The id is pushed as an unsigned 32-bit value converted to a double
(`2^32` added when the signed read is negative). CONFIRMED — disassembly.

**Side effects/subsystem:** plays a sound event positioned on the named navpoint/object and returns its playing id.
The discarded third argument is the same "read but inert" pattern §2.5/§9.3 record.

### D.4 `audio_any_conversation_playing` (`0x00a3c640`) — 3 calls / 3 scripts

**Arguments:** none read.

**Return:** exactly 1 boolean.

**Body:** pushes `0x006de990(−1)`. With the argument −1 (0xff as a byte) `0x006de990`:
- returns false if the global **`0x014e9ad0`** is null. The follow-up dump settles that global's identity (the OPEN item
  of §2.10/§27.4): it is written only by `0x006de8e0`, which sets it to `0x00879e60("mission_conv", session)` — a
  per-session channel object **named "mission_conv"**, created against the session object returned by `0x0087ba20`.
  CONFIRMED — disassembly.
- otherwise walks the session's member list exactly as §3.1 describes (head at (channel `+0x1c`) `+0x54`, next at node
  `+0xb28c`, stop at null or on returning to the head) and answers **true at the first member whose channel slot is
  valid and holds a value other than 0xff**: `0x00877a60(member)` = the slot's byte `+2` (valid flag) and
  `0x00877a90(member)` = the slot's byte `+0` (0xff when invalid), where the slot is the 5-byte element at index
  member byte `+0x158` of the array at channel `+0xc` (count at channel `+0x10`). CONFIRMED — disassembly.
- (with any other argument it tail-calls `0x00877dc0(id)`, which answers whether some member's valid slot equals that
  id — the "is this conversation playing" form; not used here.)

**Side effects/subsystem:** none, pure query: "is any session member currently in a mission conversation". The same
channel layout (per-member 5-byte slots indexed by member `+0x158`) is what §3.1's `0x00877a90` reads on session
`+0x50`; so `0x00877a60`/`0x00877a90`/`0x00877dc0` are generic accessors of a replicated per-member byte channel, and
`0x014e9ad0` is one such channel. HIGH CONFIDENCE for "channel" as a description; the layout is CONFIRMED. If the
session object or its member list is absent (e.g. no session), the answer is false.

---

## E. Other — 3 functions

### E.1 `boss_battle_matt_begin` (`0x00a407c0`) — 3 calls / 1 script

**Arguments:** 1 optional boolean, standard nil-gated idiom, **default true**.

**Return:** none.

**Body:** forwards the boolean to `0x005e7b30(flag)`:
1. resets the whole "boss battle Matt" state block through `0x005e5b20` (thiscall on `0x012ec720`): the counter dword
   `0x012ec720` := **1**, deadlines `0x012ec728`/`0x012ec72c` := −1, the bytes `0x012ec724`/`0x012ec725` := 0, the
   cheat slot `0x012ec730` and `0x012ec734` := −1, `0x012ec738`/`0x012ec73c` := 0, `0x012ec748` := −1,
   `0x012ec750`/`0x012ec754` := −1, two bytes at `+0x60`/`+0x61` := 0, four sub-objects re-initialized by `0x00d9fc60`,
   and `+0x7c`/`+0x80`/`+0x84`/`+0x85`/`+0xa0`/`+0xa4` cleared. CONFIRMED — disassembly.
2. then, **only while** the counter `0x012ec720` is below the limit `0x012ec71c` (static 4) — always true right after
   the reset set it to 1 — calls the stub `0x0101ba60` (a two-instruction `return 1`, result unused), sets the "active"
   byte `0x012ec724` := 1 and arms the deadline `0x012ec728` through `0x00d9e140` with **`[0x012ec714]` ms (static 5000)
   when the flag is true, 0 when false**. This is the same branch §28.25 describes for `boss_battle_matt_cheats_start`
   with id −1. CONFIRMED.
3. looks up the character literally named **"Matt"** (`0x0112592c`) with `0x005e4dd0` on `0x02442750`; if found and
   alive, casts it (vtable `+0x70`) and calls `0x00942530(matt, 1)`, which sets **bit `0x4000` of the dword at `+0xec`**.
   CONFIRMED — disassembly. The meaning of that bit is OPEN.

**Side effects/subsystem:** (re)starts the Matt Miller boss encounter's state machine with a 5-second (or immediate)
first deadline, and flags the "Matt" character. Pairs with §24.1 (`_cheats_stop`), §28.24/§28.25. No replication.

### E.2 `auto_pickup_disable` (`0x00a3c8a0`) — 3 calls / 2 scripts

**Arguments:** none read.

**Return:** none.

**Body:** `0x008fb880(0)`, a one-line setter that writes its byte argument to the global **`0x01308698`** (static
initial value 1). CONFIRMED — disassembly. The global's other users: `0x008fccd0` writes 1 (and passes its address
somewhere), `0x00903330` reads it (compare with 0). The registrar's neighbouring entry `0x00a3c880` also calls
`0x008fb880` (HIGH CONFIDENCE: that is `auto_pickup_enable`, writing 1). See E.2a for the reader.

**Side effects/subsystem:** turns off automatic item pickup globally (not per player). No replication.

**E.2a — follow-up dump.** CONFIRMED — disassembly:
- `0x00a3c880` (the registrar's neighbour) is `lua_gettop` + `0x008fb880(1)` — the enable twin (its registered name was not
  read here; HIGH CONFIDENCE it is `auto_pickup_enable`).
- `0x008fccd0` sets `0x01308698` := 1 and registers it with `0x0086d770("allow_weapon_auto_pickup", &0x01308698, 1,
  isHost, 0)`, where `isHost` is the §8.27 host test — so the byte is a **named, session-synchronized variable
  "allow_weapon_auto_pickup"** with the host as owner (HIGH CONFIDENCE for "synchronized": `0x0086d770` was not opened).
  A script call on the host therefore propagates through that variable mechanism rather than through a record of its own.
- `0x00903330` (a pickup decision routine) tests the byte before its weapon-pickup branch.

Corrected subsystem line for E.2: turns off automatic **weapon** pickup; the flag is session-wide, not per player.

### E.3 `waiting_for_player_dialog` (`0x00a68ea0`) — 2 calls / 1 script

**Arguments:** 1 boolean, **no** nil-gate (absent → `false`).

**Return:** none.

**Body:** true → `0x007b0850(0)`; false → `0x007b08a0()`. Both are gated on the global enable byte `0x012fcbfc`
(static 1) and share a nesting counter `0x02242870`:
- `0x007b0850(mode)` ("show"): increments the counter; if the byte `0x02242874` is 0 it stores the mode byte (0 here) in
  `0x02242876`, calls `0x007b4990(−1)` as a thiscall on the HUD-screen registry `0x012fced8` (already named elsewhere in this
  document), and then — unless `0x00706ab0()` or `0x00706b90()` returns 8, in which case it only sets the "pending" byte
  `0x02242875` := 1 — tail-jumps to `0x007b0810`.
- `0x007b08a0()` ("hide"): if the counter is positive, decrements it; **only when it reaches 0**: clears `0x02242875`
  and, on the **host** (session `+0x5c` == `+0x58`) while co-op is active (`0x00867830`, §3.1), calls `0x007b0740(0)`.

CONFIRMED — disassembly for both wrappers. See E.3a for the three callees.

**Side effects/subsystem:** shows/hides the co-op "waiting for the other player" dialog with reference counting — nested
shows need the same number of hides. Note the asymmetry: a "hide" with the counter already at 0 does nothing, and a
"show" increments even when `0x02242874` blocks the display.

**E.3a callees — follow-up dump.** CONFIRMED — disassembly for each:
- `0x007b4990(keep)` on the screen registry `0x012fced8`: walks the registry's screen stack (count at registry `+0x24`)
  from the top, and for every open screen except the one `0x007b3ae0(keep)` returns calls the screen's virtual slots
  `+0x20(1)` and `+0x24` (HYPOTHESIS: close, then release); the stack is then reset to contain only the kept screen (or
  nothing), a registry timer is re-armed with `[0x012fced0]`, and `0x007b3e80`/`0x007b3de0` refresh it. With −1 here:
  close all screens (unless `0x007b3ae0(−1)` maps −1 to a screen, OPEN).
- `0x007b0810()` ("display"): `0x00706e00(9)`, sets the "shown" byte **`0x02242874` := 1**, and on the host stores
  `0x00707380()` in `0x02242877` and calls `0x007b0740(1)`.
- `0x007b0740(show)`: opcode-**`0x23`** record with three bytes — `show`, `0x02242876`, `0x02242877` — sent through the
  host-session path. So the host broadcasts both the show (1) and the final hide (0).
- `0x00706b90()`: returns a cached game-state value (recomputed through `0x00706b00` when the dirty byte `0x012f4a84` is 1).
  Together with `0x00706ab0()` (§ "table-indexed accessor" elsewhere in this document) the value 8 suppresses the
  display and only marks it pending. Meaning of state 8 and of `0x00706e00(9)` OPEN.

Open point: neither wrapper clears `0x02242874` (the hide path clears only `0x02242875`), so after one display a second
show skips straight past the display branch unless some other routine resets that byte — its other writers were not
traced (OPEN).

---

## F. Cross-cutting findings and implications for existing spec sections

1. **Stack-discipline defects (three functions).** `vehicle_is_vtol_hover`/`vehicle_is_vtol_jet` return two values
   (`true`, `false`) on success (A.2/A.3); `action_play_directional_stumble_do` returns one number per failure plus a final
   1.0 and never returns early, which also makes it dereference null on an unresolved character or object (C.2). All
   CONFIRMED from the listings. An implementation should document whether it reproduces the extra values; the null
   dereferences should not be reproduced.
2. **Dead branch in shipped code:** C.2's left/right stumble ids are unreachable (the second comparison re-tests +0.7071
   with swapped operands). CONFIRMED.
3. **§14.1 correction** (C.1a): `0x0095da50` is a name → synced-action index lookup taking §14.1's arg 3 as its only
   argument; §14.1's "arg 3 inert" and "matches arg 1's identity via a hidden argument" are wrong. CONFIRMED.
4. **`0x014e9ad0` identified** (D.4): the "mission_conv" per-session channel created by `0x006de8e0` — closes the OPEN
   "object-kind identity" item §2.10/§27.4 carry. `0x00877a60`/`0x00877a90`/`0x00877dc0` are generic per-member byte-channel
   accessors (§3.1 uses `0x00877a90` on session `+0x50`). CONFIRMED.
5. **`0x00ad3820` opened** (A.4): §7.24's "not ready" predicate is `0x008cc4d0` OR `0x0091f600` OR null OR `+0x33` bit
   `0x8` clear. CONFIRMED.
6. **Flying-type enum** at `(vehicle+0xbf4)+0x2c`: 3 = helicopter (`0x00ad3220`), 4 = VTOL (`0x00ad3200`), §12.11's
   `0x00ad31a0` accepts both. CONFIRMED for the predicates; the naming is HYPOTHESIS.
7. **Unit difference** between `vehicle_set_ambient` (raw number) and §9.5 `vehicle_speed_override` (mph × 0.44704) into the
   same setter `0x00b29ca0`. CONFIRMED.
8. **`0x008ae2b0` opened** (follow-up dump): id pair → 16-bit network id = the word at `*(object+0x3c)`, only for objects
   with descriptor bit **`0x2`@`+6`** that pass `+0x33` bit `0x10` clear (its liveness call's result is discarded, the
   same quirk as B.1's `0x004e7b00`); else 0. CONFIRMED. So bit `0x2`@`+6` is at least "has a network id"; A.11's spotlight
   target requirement is that same bit.
9. **New record opcodes / sub-codes seen** (all CONFIRMED): `0x45` listener override (always sent, 16-bit id); `0x49`
   with byte 2 (sequence end, host only); `0x23` waiting dialog (three bytes, host only); `0x43` sub-codes 50
   (pay-attention request) and 19 (synced play); `0x42` sub-code 12 (vehicle-AI mode); `0x46` string-tagged for
   `m_force_flagsnever_flatten_tires`, `m_force_flagsweapons_disabled`, `vai_force_flagsno_chase`,
   `ai_force_flagsin_scripted_cover`, `forced_target_handle`.
10. **Three ignored extra arguments to `0x00a29200`** (A.11, B.3, D.1 each push a literal 1 that the resolver never reads).
    CONFIRMED from the resolver body (it reads only its first stack argument).

---

## G. Direct answer — all 25 names

All 25 resolved (one registration each in `0x00a20840`) and are written up above. None failed to resolve; the job's
arguments were used unchanged.

| # | Name | Address | Status | One line |
|---|---|---|---|---|
| 1 | `boss_battle_matt_begin` | `0x00a407c0` | written up (E.1) | resets the Matt-boss state block, arms a 5000 ms / 0 ms first deadline, sets bit `0x4000`@`+0xec` on character "Matt" |
| 2 | `auto_pickup_disable` | `0x00a3c8a0` | written up (E.2) | writes 0 to the session-synced global "allow_weapon_auto_pickup" `0x01308698` |
| 3 | `audio_set_listener_override` | `0x00a3dc70` | written up (D.1) | audio listener follows a named (bit `0x8`@`+6`) object; opcode `0x45` always sent |
| 4 | `audio_play_for_navpoint` | `0x00a3edf0` | written up (D.3) | plays an event on a named object via `0x00a2acb0`, returns playing id or 0; arg 3 discarded |
| 5 | `audio_clear_listener_override` | `0x00a3c860` | written up (D.2) | clears the override pair, sends opcode `0x45` with id 0 |
| 6 | `audio_any_conversation_playing` | `0x00a3c640` | written up (D.4) | any session member's "mission_conv" channel slot valid and ≠ 0xff |
| 7 | `ai_set_in_scripted_cover` | `0x00a3c3a0` | written up (B.2) | AI force-flag bit `0x1`@AI `+0xc`; on false also queues scripted action 13; returns resolve success |
| 8 | `ai_pay_attention_to_position` | `0x00a3d720` | written up (B.3) | copies an object's position into the attention block, mode 3 or 7; else opcode `0x43`/50 |
| 9 | `ai_do_scripted_rush` | `0x00a3e870` | written up (B.4) | scripted action 22 toward the current target (with checks) or a named position; returns success |
| 10 | `ai_clear_priority_target` | `0x00a3c320` | written up (B.1) | sets the AI forced target to the null pair (single-gate `0x008ae480` setter) |
| 11 | `action_sequence_end` | `0x00a3c270` | written up (C.3) | global sequence teardown; host broadcasts opcode `0x49` byte 2 |
| 12 | `action_play_synced_do` | `0x00a3fe10` | written up (C.1) | two-actor synced animation by name, optional anchor; returns −1/0/1; corrects §14.1 |
| 13 | `action_play_directional_stumble_do` | `0x00a3f7d0` | written up (C.2) | front / not-front stumble; multi-value return, null dereference on bad names, dead side branch |
| 14 | `waiting_for_player_dialog` | `0x00a68ea0` | written up (E.3) | reference-counted show/hide of the co-op waiting dialog; host sends opcode `0x23` |
| 15 | `vehicle_spotlight_is_target_spotted` | `0x00a65e70` | written up (A.11) | re-aims the spotlight at arg 2, then range 80 / 10° cone / line-of-sight test |
| 16 | `vehicle_set_weapons_disarmed` | `0x00a63520` | written up (A.6) | force-flag bit `0x4`@`+0x1d7e`, replicated |
| 17 | `vehicle_set_sirenlights` | `0x00a64490` | written up (A.9) | class-dependent siren lights (`+0x1d7c` bit `0x8`) / headlight flash (bit `0x10`), plus siren sound |
| 18 | `vehicle_set_no_chase` | `0x00a62b80` | written up (A.7) | vehicle-AI force-flag bit `0x1`@sub `+5` (same setter as §14.2) |
| 19 | `vehicle_set_kneecappers` | `0x00a62fe0` | written up (A.8) | attaches/removes per-wheel spike collision record `+0x1550` (pool of 48), deferred bit in `+0x16cc` |
| 20 | `vehicle_set_ambient` | `0x00a62ad0` | written up (A.10) | sets `+0x16c0` bit `0x1`, vehicle-AI mode 6/0x11, optional speed cap in raw units |
| 21 | `vehicle_never_flatten_tires` | `0x00a62a60` | written up (A.5) | force-flag bit `0x1`@`+0x1d7b`, replicated, default true |
| 22 | `vehicle_is_vtol_jet` | `0x00a64770` | written up (A.3) | flying-type 4 and `+0x5e8` ∈ {1, 2}; returns (true, false) on success |
| 23 | `vehicle_is_vtol_hover` | `0x00a64700` | written up (A.2) | flying-type 4 and `+0x5e8` ∈ {0, 3}; returns (true, false) on success |
| 24 | `vehicle_is_ready` | `0x00a62820` | written up (A.4) | `+0x3b` bit 0 clear, `+0x3a` bit 0 clear, `+0x33` bit `0x8` set |
| 25 | `vehicle_is_helicopter` | `0x00a66070` | written up (A.1) | by-name vehicle lookup only; flying-type == 3 (VTOLs answer false) |

Main OPEN items carried forward: the meaning of scripted actions 13 and 22, the attention-mode variant bit `0x4`, the VTOL
state values in `+0x5e8`, vehicle-AI mode 6/0x11, bit `0x4000`@`+0xec` set on "Matt", the writer that resets
`0x02242874`, and `0x0095fc00`'s handling of a null actor.
