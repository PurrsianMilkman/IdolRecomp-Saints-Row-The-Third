# Ranking tranche 03 — 25 previously-unspecced Lua-bound names (2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-03.json` (Team B
call-count order, names 1-25 of the 554 unspecced). Investigation only — nothing here has been
transcribed into `spec-lua-api-behaviour.md`.

**Method.** One local read-only headless Ghidra run of `CrreishDump.java` (`lua depth:1 maxfuncs:15
maxinsn:500`, all 25 names in one call) against a job-private copy of `tools\ghidra_projects`
(`tools\gp_t03r`, deleted afterwards). Raw dumps: session scratchpad `t03r\a\lua\*.txt`. All 25 names
resolved to exactly one string occurrence in `.rdata`, each referenced once inside the 1,014-entry
gameplay registrar `0x00a20840`, and each name's paired code pointer (insn offset +1) is the dumped
root. `maxfuncs:15` is a per-name callee cap, not a per-call name cap, so one call was enough.
`vehicle_is_vtol` produced a second candidate (`0x00c1e650`); it is not the binding, see §T3.4.4.

**Shared primitives** are cited by this document family's established identities (spec §3 preamble,
§4.1): `0x00dfde50` `lua_gettop`, `0x00dfe210` `lua_tolstring` (always `len` = 0), `0x00dfe1e0`
`lua_toboolean`, `0x00dfe040` `lua_type`, `0x00dfe160` `lua_tonumber`, `0x00ea2596` the truncating
float-to-int cast, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe590` `lua_pushboolean`. Argument indices:
every function here locates argument k by the negative index `k − 1 − gettop`, so "arg 1" is the
bottom of the frame whatever the argument count. An absent argument read through `lua_toboolean`
reads as false (the none-slot sentinel `0x01251b00` has tag 0). "Nil-gated" means the function calls
`lua_type` first and substitutes a default when the type is 0 (nil) or the argument is missing.
Resolvers: `0x00a281a0` generic character chain, `0x00a280c0` generic kind-filtered chain,
`0x00a281e0` dedicated vehicle resolver, `0x0062a190` per-kind vehicle resolver, `0x00853b10` the
handle-liveness guard (non-zero = dead). Session accessor `0x0087ba20` (global `0x024d8534`); its
`+0x5c` == `+0x58` test means "this machine is the host" (spec §3.1). Local-player accessor
`0x009da4e0`.

---

## T3.1 Character cluster — 2 functions

### T3.1.1 `character_dont_regenerate` (`0x00a41370`)

**Arguments:** 2. Arg 1 mandatory string (character name). Arg 2 boolean via `lua_toboolean`, no
nil-gate — absent or nil reads as false.

**Return:** none (returns 0, no push, on every path).

**Body:** arg 1 resolves through the generic character chain `0x00a281a0(name, NULL)`. If it
resolves, calls `0x0094ce30` (thiscall, `this` = character) with the boolean. `0x0094ce30`:

1. Liveness guard `0x00853b10`; a dead handle does nothing.
2. Single-gate authority check `0x008addb0(character, 0)`. When it returns 1 (local authority):
   writes the boolean into **bit `0x2` of the dword at character `+0x1c98`** (set when true, clear
   when false). Then, when true, stores `-1` into the dword at `+0x19c0`; when false, calls the
   deadline helper `0x00d9e140(0)` with `+0x19c0` as its hidden destination, i.e. restamps `+0x19c0`
   to "now".
3. Otherwise (no local authority): opens a dispatch record with opcode `0x46`, appends the hashed
   tags `"human"` (`0x01169464`) and `"human_force_flagsdont_regen_health"` (`0x011684a4`), the
   character's network id (`0x0050f790`) and the boolean (`0x004d46e0`), and commits it through the
   multi-target `0x0086f110` (sync id from `0x008ae020`). The local flag is NOT written on this path.

**Side effects/subsystem:** per-character "do not regenerate health" force flag. Bit `0x2` of
`+0x1c98` is a new entry in the already-catalogued `+0x1c98` force-flag word. The `+0x19c0` dword
reads as the regeneration restart time: disabled (`-1`) while the flag is on, reset to "now" when the
flag is cleared. Shape note: this is a hybrid of the spec §7 front-matter idioms — the single gate
`0x008addb0` of variant 2, but with the string-tagged opcode `0x46` record of variant 1. **[CONFIRMED — disassembly of `0x00a41370` and `0x0094ce30`, including the
hidden-`this` (`MOV ECX,EAX` at `0x00a413ba`) and the `+0x19c0` destination (`LEA ECX,[ESI+0x19c0]`
at `0x0094ceba`). HYPOTHESIS — `+0x19c0` is the regen-restart deadline (role inferred from the tag
string and the -1/now pattern). The tag string `"human_force_flagsdont_regen_health"` has exactly one
other user, `0x008ae650`, plausibly the receiving side of the record — OPEN, not dumped.]**

### T3.1.2 `character_clear_combat_move` (`0x00a410e0`)

**Arguments:** 1 mandatory string (character name). Nothing else is read.

**Return:** none.

**Body:** resolves via `0x00a281a0(name, NULL)`; on success calls `0x009682d0(character, 1)` — the
second argument is a hardcoded literal true, not a Lua argument. `0x009682d0` does nothing for a null
character or one whose word at `+0x1358` is `0xffff`. Otherwise it asks the predicate `0x0095d720`;
if that is true it hands off to `0x0095f250(character, true)` and returns. If false it calls
`0x00967f60` (no stack arguments) and then, because the flag is true, issues three identical calls
`0x00957ee0(character, id, 0.2f, 0, -1, 0)` with ids `0x1d53`, `0x1d54` and `0x1d8c` (the 0.2 is the
float constant at `0x01171540`).

**Side effects/subsystem:** clears a character's current combat movement; the three
`0x00957ee0` calls look like requests keyed by numeric action/animation ids with a 0.2 s blend.
**[CONFIRMED — disassembly of both bodies and the hardcoded `PUSH 0x1` at `0x00a41106`. OPEN — the
identities of `0x0095d720`, `0x0095f250`, `0x00967f60` and `0x00957ee0`, and what ids
`0x1d53`/`0x1d54`/`0x1d8c` name; not dumped at depth 1. HYPOTHESIS — `0x00967f60` receives the
character in `ESI` (it is called with no stack argument while `ESI` still holds the character and
`ECX` is not set on that path), and `0x00957ee0` is an action/animation request with a blend time.
`+0x1358 == 0xffff` reads as "no combat move/state assigned" — HYPOTHESIS.]**

---

## T3.2 `boss_battle_matt_*` cluster — 2 functions

Both resolve the boss through the same hardcoded literal: neither takes a character name from Lua.

### T3.2.1 `boss_battle_matt_set_player_as_avatar` (`0x00a40930`)

**Arguments:** 1 boolean (arg 1) via `lua_toboolean`, no nil-gate — absent reads as false.

**Return:** exactly 1 boolean on every path (`lua_pushboolean` at `0x00a4097e`, function returns 1).
The pushed value is the result of the worker `0x005e5eb0` (below), not the input.

**Body:** calls `0x005e5eb0(flag)`, then sets (`0x00a007d0(0)`) or clears (`0x00a007f0(0)`) **bit 0
of the global flag word `0x0268d2d0`** according to the flag, then pushes the worker's result.

`0x005e5eb0(flag)` reads the local player (`0x009da4e0`) and a second player (`0x009df3d0`, null when
absent). No local player → returns false and does nothing.

- **flag true (become the avatar):** `0x009a1e10(player, 0)`, then `0x00945d40(1)` and
  `0x00949df0(1)` on the player (thiscall); if `0x00941f40` returns an object whose `+0x19c` record
  has kind `0xb` at `+0x1c`, `0x009d1540(player, thatObject)`. Looks up `"npc_bahamut"`
  (`0x00bdea40(name, 1)`); not found → returns false. Builds a customization item `"Bahamut Suit"` /
  `"avatar.cmeshx"` / `"Bahamut"` (`0x008282c0`/`0x00828390`/`0x008282f0`), applies it to the
  player's `+0x2088` customization object (`0x00825010`), and resolves a model (`0x00754530` over the
  bahamut entry's `+0x8`/`+0xd8`); failure → returns false. On success: binds the model into the
  player's `+0xd24` render object (`0x004bcac0`), stores the bahamut entry at player `+0xf4`, swaps
  the rig to `"avatar.rig"`/`"Avatar"` (`0x009e3400`), `0x00942110(player, 0x10, 0, -1.0f)` and
  `0x00986920(player, 1, 0)`, then `0x00d34dd0()`. The second player, if present, gets the same
  treatment (minus `0x009a1e10`/`0x00d34dd0`). If the session exists and this machine is the host,
  writes 1 to the byte global `0x012ec740`. Returns true.
- **flag false (restore):** if the session exists and this machine is the host, `0x00b99e50(3)` and
  clears bit 0 of `0x0268d2d0`. Looks up `"StyleTest_PC"`; **not found → returns true** without
  restoring anything. Found: `0x00945d40(0)`/`0x00949df0(0)`, `0x00821630(+0x2088, 0x13, 1)`,
  `0x00891c70(0x008b7100(player, 1))`, stores the entry at `+0xf4`, swaps the rig back to
  `"cf_body.rig"`/`"PLYF"` when the byte at `+0xa41` is 1, else `"cm_body.rig"`/`"PLYM"`, re-binds the
  render object, `0x004bc9d0(global 0x012ec7d0)`, and if the dword at `+0xcc8` equals `0x10` calls
  `0x00942110(player, 0, 0, -1.0f)`. Same for the second player. Host → writes 0 to `0x012ec740`.
  **Returns false after a successful restore.**

**Side effects/subsystem:** swaps the local (and co-op) player's body/rig to the "Avatar"/Bahamut
model for the boss fight and back; host-side bookkeeping in `0x012ec740` and bit 0 of `0x0268d2d0`.
**[CONFIRMED — disassembly of `0x00a40930`, `0x005e5eb0`, `0x00a007d0`, `0x00a007f0`, including the
return-value paths (the restore path ends at `0x005e625d XOR AL,AL`, the "StyleTest_PC missing" exit
jumps to `0x005e6092`, which loads 1). So the Lua return is NOT a plain success flag: true = avatar
applied, or restore skipped; false = no player, avatar failed, or restore done. HYPOTHESIS —
`+0xa41 == 1` means a female player body (`cf_`/`PLYF` versus `cm_`/`PLYM`); `0x009df3d0` is the
co-op player. OPEN — the callees not dumped (`0x009a1e10`, `0x00945d40`, `0x00949df0`, `0x00942110`,
`0x00986920`, `0x00b99e50` and the rest), and the role of bit 0 of `0x0268d2d0`; that bit is also
set/cleared by many other sites (`0x006f89xx`, `0x00a43970`, `0x0061a900` among others).]**

### T3.2.2 `boss_battle_matt_hide_wings` (`0x00a40850`)

**Arguments:** 1 optional boolean (arg 1), nil-gated, **default true** (missing arg or nil → true).

**Return:** none.

**Body:** calls the always-returns-1 stub `0x00d221a0` (result discarded — inert), then resolves the
boss through `0x005e5ae0`, which is `0x00a281a0("Matt", NULL)` (literal at `0x0112592c`). If found,
`0x005e7590(matt, flag)`: walks Matt's circular attached-object list (head at `+0x1c`, next at
`+0x20`); for each entry whose kind descriptor (`0x02cc9900[entry+0x34]`) has bit `0x8` at row `+6`
and bit `0x40` at row `+9`, and whose `+0xe0` record's `+4` equals the hash of `"Avatar_wings"`
(`0x00d9e8b0`), calls `0x008ccb90(entry, flag, 1, 0)`. Afterwards, if the session exists, this
machine is the host and the flag is true, stores `0x008add70(matt)` — a 16-bit value; the same
helper supplies the 16-bit object id in the vehicle records of §T3.5.9/§T3.5.11 — into the word
global `0x012ec742`.

**Side effects/subsystem:** hides (true) or shows (false) the "Avatar_wings" attachment on the Matt
character; host remembers Matt's id while hidden. **[CONFIRMED — disassembly of `0x00a40850`,
`0x005e5ae0`, `0x005e7590`. HYPOTHESIS — `0x008ccb90`'s second argument is "hidden" (consistent with
the function name; spec §7 also sees `0x008ccb90(object, 0, 1, 0)` and leaves its role OPEN).
`0x012ec742` is read at `0x005e7826` and used by `0x005e7850` — OPEN, not dumped. Note: the
`0x012ec742` write happens only when hiding, so showing does not clear it.]** The sibling setter
`boss_battle_matt_set_matt_flying` is already spec §18.2.

---

## T3.3 Global toggles and audio emitters — 3 functions

### T3.3.1 `auto_pickup_enable` (`0x00a3c880`)

**Arguments:** none read. `lua_gettop` is called and its result discarded.

**Return:** none.

**Body:** `0x008fb880(1)`, which writes its byte argument to the global `0x01308698`. The literal 1
is hardcoded: this entry point can only enable.

**Side effects/subsystem:** global auto-pickup enable byte. Its file-backed initial value is 1
(enabled). Other users: `0x008fccd0` also writes 1 (a reset path), `0x00903330` reads it (the
consumer). The unnamed code at `0x00a3c8a0` (the registrar slot just before this name) also calls
`0x008fb880` — most plausibly the matching `auto_pickup_disable`, not dumped. **[CONFIRMED —
disassembly, full body and the global's xrefs. HYPOTHESIS — the `0x00a3c8a0` pairing.]**

### T3.3.2 `audio_ambient_emitter_pause` (`0x00a3c800`) / T3.3.3 `audio_ambient_emitter_resume` (`0x00a3c830`)

Identical shape, written up together.

**Arguments:** 1 mandatory string (emitter name).

**Return:** none.

**Body:** passes the name to `0x00563270` (pause) or `0x005632e0` (resume). Both convert the name to
a Wwise id through `0x00462960` (the established `AK::SoundEngine::GetIDFromString` path, spec §2),
then scan the ambient-emitter list of the world object manager (global `0x03171a64`, the same
manager whose vehicle list `vehicle_clear_all_radio_locks` walks, §T3.5.4): count at `+0x2b8`, `u16`
indices at `+0x2b0`, into the object-pointer array at `+0x58`. The first emitter whose `+0x84` equals the id gets its
`+0x90` handle passed to `0x0045f000` (pause) or `0x0045f0d0` (resume), followed by
`0x00562ff0(2)` (pause) or `0x00562ff0(3)` (resume). No match → nothing.

**Side effects/subsystem:** pauses/resumes one named ambient emitter; the trailing
`0x00562ff0(2|3)` is plausibly a state/notification code. **[CONFIRMED — disassembly of all four
bodies. OPEN — `0x0045f000`, `0x0045f0d0`, `0x00562ff0` not dumped; only the first match is acted
on.]**

---

## T3.4 AI cluster — 2 functions

Both use `0x00a28150`, the character-only tail of the generic chain (follower sentinels via
`0x00a26010`/`0x00a26110`, else the per-kind character resolver `0x005e4dd0` on registry
`0x02442750`, liveness guard, then the object's own virtual slot `+0x70`). Both drive the same
per-character "combat action" record keyed by an index into a fixed 70-entry action table.

**The action table (`0x012e1c60`, 70 rows of `0x18` bytes, file-backed).** Row `+0` is a name
pointer, `+4` a category byte, `+8` a handler code pointer, `+0x15` a flag byte; the dword pair at
`+0xc`/`+0x10` is `15000`/`300` in every row. `0x004f2610` maps a name to its index
(case-insensitive linear scan with `__stricmp`; 71 = `0x47` when not found) and `0x004f2840` maps an
index back to its name (`"Invalid CAE action"` for 70 and above — "CAE" is the engine's own name for
this system). Names read straight out of the executable: 0 `fire`, 1 `fire sweep`, 2 `fire suppress`,
3 `fire chaos`, 4 `throw grenade`, 5 `throw weapon`, 6 `melee`, 7 `melee vehicle`, 8 `cover take def`,
9 `cover take off`, 10 `cover advance`, 11 `cover stay down`, 12 `cover fire`, 13 `cover popout`,
14 `hold position`, 15 `melee stand back`, 16 `move retreat`, 17 `move regroup`, **18 `move advance`**,
19 `move chase`, 20 `avatar chase`, 21 `move surround`, 22 `move rush`, 23 `move rush to melee`,
24 `move scripted`, 25 `move to navmesh`, 26 `move sidestep`, 27 `move to post combat`,
28 `move to post idle`, 29 `move close in`, 30-40 the `follow ...` family (`make way`, `avoid LOF`,
`formation`, `acquire`, `player`, `teleport`, `lemming`, `to car`, `other car`, `carjack`, `browse`),
41-43 `investigate move/look/peek`, 44 `reload`, 45 `pickup weapon`, 46 `human shield grab`,
47 `taunt`, 48 `vehicle passenger`, 49 `vehicle enter scpt`, 50 `vehicle extract`, 51-56 `brute ...`
(`throw prop`, `car flip`, `bull rush`, `qte player`, `attack brute`, `gun finisher`), 57-62
`avatar ...` (`shockwave`, `stomp`, `taunt`, `fireballs`, `teleport stomp`, `bullrush`),
63 `quick kill`, 64 `zombie eat`, 65 `zombie explode`, 66 `killbane chase`, 67 `killbane strafe`,
68 `roller blader change`, 69 `idle`. **[CONFIRMED — read from the file image; the index/name mapping
is the two helpers' own bodies.]** Rows 24 (`move scripted`) and 49 (`vehicle enter scpt`) have a
null handler pointer, and row 11 (`cover stay down`) is the only row with `+0x15` = 1.

**Per-character action record (character `+0x438`..`+0x4ff`).** From the two writers below:
`+0x4a8` is a staged ("pending") 0x58-byte action block — byte `+0x4a8` the action index, word
`+0x4aa` a value from `0x006f62b0(character, 3, 0)`, dwords `+0x4b0`/`+0x4b4` a target handle pair,
`+0x4c0..+0x4c8` a 12-byte target position; `+0x450` is the committed copy of the same block;
`+0x44c` a flag byte; `+0x445` the current action index; `+0x444` a byte; `+0x438` and `+0x440` two
deadline stamps (written through `0x00d9e140(0)`, i.e. "now"); `+0x4fe` bits 6-7 flags.
**[CONFIRMED — offsets from disassembly. HYPOTHESIS — the pending/committed reading, from the 0x58
byte copy `+0x4a8` → `+0x450` in `0x004f4d00`.]**

### T3.4.1 `ai_suggest_action` (`0x00a3eb00`)

**Arguments:** 3. Arg 1 mandatory string (character name). Arg 2 mandatory string (action name, one
of the 70 above, or `"NONE"`). Arg 3 optional string (target name), nil-gated, default none.

**Return:** none on every path.

**Body:** resolves arg 1 via `0x00a28150`; unresolved → nothing. Maps arg 2 to an index via
`0x004f2610`.

- **Unknown name, other than `"NONE"`** (case-insensitive, `__stricmp` against `0x0116c1c8`): prints
  every valid action name, one per line (`printf` with `"\t%s\n"` over indices 0-69), and returns.
  Nothing else happens.
- **`"NONE"`**: index 71 is passed on and the worker rejects it (≥ 70) — a silent no-op.
- **Known name:** arg 3, if given, resolves through `0x004584e0` on registry `0x02442750` (kind bit
  `0x1` at descriptor row `+6`, objects with bit `0x10` at `+0x33` excluded), then the liveness guard.
  The live target's dword pair at `+8`/`+0xc` becomes the target handle; otherwise the all-zero pair at
  `0x0117f808`. Calls `0x004f4b90(character, index, handleLo, handleHi, 1, 0)` — the trailing `1`
  ("force") and `0` are hardcoded.

`0x004f4b90` (index < 70 only; with force = 1 it never stops on the `+0x44c` bit `0x8` busy test):
sets bits `0x18` of `+0x44c`; if the index differs from the current `+0x445`, clears bit `0x80` of
`+0x4a6`; restamps `+0x440`; clears bit `0x4` of `+0x44c`; resets the staged block from the
0x58-byte template at `0x012e14e0`; writes the index to `+0x4a8` and the handle pair to
`+0x4b0`/`+0x4b4`; sets both top bits of `+0x4fe`; stores `0x006f62b0(character, 3, 0)` in `+0x4aa`.
Then, if the character is NOT under local authority (`0x008addb0(character, 0)` false), sends
`0x004f4830(character, 1, index, 3000)`.

**Side effects/subsystem:** stages (does not commit) a suggested combat action with an optional
target on a character; remote-authority characters get the request forwarded with what looks like a
3000 ms lifetime. **[CONFIRMED — disassembly of `0x00a3eb00`, `0x004f2610`, `0x004f2840`,
`0x004584e0`, `0x004f4b90`. HYPOTHESIS — `0x004f4830` is the network forward and 3000 a
milliseconds timeout; the AI later commits or discards the staged suggestion (not traced). Note: the
printed list goes through the C runtime `printf`, which has no visible console in a retail build.]**

### T3.4.2 `ai_do_scripted_advance` (`0x00a3e370`)

**Arguments:** 1 mandatory string (character name) + 1 optional string (target name), nil-gated,
default none.

**Return:** exactly 1 boolean on every path (`lua_pushboolean`, function returns 1).

**Body:** resolves arg 1 via `0x00a28150`; unresolved → pushes false.

- **No target:** `0x0053a520(character, character + 0x4a8)` tries to compute an advance destination
  (it bails out early when `0x00853b30(0x0097e7b0(character, 0), 0x21)` is true; otherwise it queries
  the character's `+0x510` sub-object and `+0x40` position and, on success, writes a 12-byte position
  into the staged block at `+0x4c0`). On true → `0x004f4d00(character, 18, 0)`. Pushes that boolean.
- **Target given:** first tries `0x00a3df50` on registry `0x02442750` (kind bit `0x1` at descriptor
  row `+0xa`), then falls back to `0x005982e0` (kind bit `0x2` at row `+0xb` — the "5th per-kind
  resolver" spec §1.10 already cites). The first live match has its position (`+0x40..+0x48`) copied
  into `+0x4c0..+0x4c8`, then `0x004f4d00(character, 18, 0)` and pushes true. Neither resolves →
  pushes false.

`0x004f4d00(character, index, 0)` (index < 70): writes the index to `+0x4a8`; because its third
argument is 0, sets bit `0x40` of `+0x4fe` and stores `0x006f62b0(character, 3, 0)` in `+0x4aa`.
Not under local authority → `0x004f4830(character, 0, index, 3000)` and return. Local authority →
consults `0x004f25c0` with the row's `+0x15` byte (when that fails it calls `0x0096f4f0(character)`,
but that callee is a pure "is dead" predicate — see §T3.5.8 — and its result is discarded, so the call
is inert), copies the staged 0x58-byte block to `+0x450`, sets `+0x44c` = (old & `0xea`) | `0x8`, restamps
`+0x440`, writes literal `0x12` to `+0x444` and the index to `+0x445`, restamps `+0x438`, and calls
`0x004e4130(character, row +4 category byte)`.

**Side effects/subsystem:** forces combat action 18, `move advance`, on a character — toward a
named object's position, or toward a destination the engine computes itself. Unlike
`ai_suggest_action` it commits the action immediately on the authority machine. **[CONFIRMED —
disassembly of `0x00a3e370`, `0x00a28150`, `0x00a3df50`, `0x005982e0`, `0x004f4d00`, `0x0053a520`;
index 18 = `move advance` read from the table. OPEN — what object kinds bits `0x1`@`+0xa` and
`0x2`@`+0xb` denote (navpoint-like markers are plausible), and the internals of `0x0053a520`'s
callees. Note that `+0x444` gets literal `0x12` whatever the index; for this caller the index is also
18, so the two coincide.]** Compare spec §18.3 `ai_do_scripted_take_cover`, which drives the same
`0x004f4d00` from the same registrar block.

---

## T3.5 Vehicle cluster — 13 functions

**Shared vehicle facts used below.**

- **Two vehicle-resolution styles.** Most entries use the dedicated resolver `0x00a281e0`: it first
  tries the generic chain `0x00a280c0`; if that yields an object whose 64-bit handle at
  `+0x16c0/+0x16c4` is non-zero (a character sitting in a vehicle), it returns that vehicle through
  `0x004dcf00`. Otherwise it uses the per-kind vehicle resolver `0x0062a190` (registry `0x02442750`,
  descriptor bit `0x8` at row `+0xb`), the liveness guard, the virtual slot `+0x68` as a gate, and
  the virtual slot `+0x70` for the result. Two entries (`vehicle_set_invulnerable_to_player_explosives`,
  `vehicle_is_vtol`) instead inline `0x0062a190` + liveness + virtual `+0x70` with no `+0x68` gate and
  no character redirect — **a character name does not reach its vehicle through those two**.
  **[CONFIRMED — listings of `0x00a281e0`, `0x00a66270`, `0x00a66170`.]**
- **Vehicle class.** `0x00ad3200(vehicle)` is true when the dword at `+0x2c` of the vehicle's
  `+0xbf4` record equals 4 (false for a null vehicle). By its use in `vehicle_is_vtol`, class 4 is
  VTOL. Spec §20.3 already tests the same `+0xbf4`/`+0x2c` field for zero (car alarm), so this is a
  vehicle-class enum. **[CONFIRMED — body. HIGH CONFIDENCE — "class enum", from the two uses.]**
- **New force-flag bits.** Four record-and-replicate setters below all have the double-gate
  (variant 1) shape (`0x008ae480` then `0x008837a0`; both false → when `0x008ae3a0` yields a target,
  open an opcode `0x46` record with the hashed tags `"vehicle"` + a specific tag, the vehicle's handle
  pair `+8/+0xc` (`0x0086f4b0`) and the boolean, committed to that target through the multi-target
  `0x0086f110`; either gate true → write the bit directly, no record). Bits: **`+0x1d7c` bit `0x2`**
  `invulnerable_to_player_explosives`, **`+0x1d7c` bit `0x4`** `radio_controls_locked`,
  **`+0x1d7d` bit `0x20`** `disable_exp_and_damage_vfx`, **`+0x1d7d` bit `0x40`**
  `special_override_never_ghost`. None of them collides with the `+0x1d7a` bits spec §20.1/§21.30
  dispute. **[CONFIRMED — disassembly of the four setters.]**
- **Seat count / seat 0.** `0x00ab4af0(vehicle)` = 8 unless the byte at `+0xbd0` is 1, then the dword
  at `+0x824`. `0x00ab5070(vehicle, i)` returns the occupant of seat `i` from the handle at
  `+0x1358 + 0x30·i` (through `0x004d7f30(handle, 1)`). **[CONFIRMED — bodies. HIGH CONFIDENCE — seat
  0 is the driver.]**
- **Script request ids.** `0x006f62b0(object, channel, x)` first cancels any tracked request for the
  same object and channel (list at `0x014f4be0`, cancel `0x006f6040`), then allocates a 12-bit
  sequence id (counter `0x014f6b28`, wrapping from `0xfff` to 1) OR-ed with `0x008677f0()` shifted
  left 12, registers it (`0x006f61b0`) and returns it, or 0 on failure. Channel 0 is used by
  `turn_to_do`, 2 by `vehicle_pathfind_to_do`, 3 by the AI action writers (§T3.4). **[CONFIRMED —
  body. HYPOTHESIS — the high nibble is the local machine's index.]**

### T3.5.1 `vehicle_set_invulnerable_to_player_explosives` (`0x00a66270`)

**Arguments:** 1 mandatory string (vehicle name) + 1 boolean via `lua_toboolean`, no nil-gate (absent
→ false).

**Return:** none.

**Body:** a NULL name does nothing. Resolves through the inline `0x0062a190` path (above — no
character redirect), then calls `0x00a7c210` (thiscall, `this` = vehicle) with the boolean: the
variant-1 setter for **`+0x1d7c` bit `0x2`**, tag `"m_force_flagsinvulnerable_to_player_explosives"`
(`0x01168acc`).

**Side effects/subsystem:** per-vehicle "ignore explosive damage from players" flag; how damage code
reads the bit is OPEN. **[CONFIRMED — disassembly.]**

### T3.5.2 `vehicle_disable_explosion_and_damage_vfx` (`0x00a621f0`)

**Arguments:** 1 mandatory string (vehicle name, through `0x00a281e0`) + 1 boolean, no nil-gate.

**Return:** none.

**Body:** on success, thiscall `0x00a7d1e0(flag)`: variant-1 setter for **`+0x1d7d` bit `0x20`**, tag
`"m_force_flagsdisable_exp_and_damage_vfx"` (`0x01168950`).

**Side effects/subsystem:** suppresses the vehicle's explosion and damage visual effects. **[CONFIRMED
— disassembly; the consumer of the bit is OPEN.]**

### T3.5.3 `vehicle_set_special_override_never_ghost` (`0x00a63300`)

**Arguments:** 1 mandatory string (vehicle name, through `0x00a281e0`) + 1 boolean, no nil-gate.

**Return:** none.

**Body:** on success, thiscall `0x00a7d350(flag)`: variant-1 setter for **`+0x1d7d` bit `0x40`**, tag
`"m_force_flagsspecial_override_never_ghost"` (`0x01168924`). `0x00a7d350` has two engine callers
too (`0x008b02bd`, `0x009b0581`).

**Side effects/subsystem:** "never ghost" override on the vehicle. **[CONFIRMED — disassembly.
HYPOTHESIS — "ghost" is the engine's non-colliding/faded vehicle state; not traced.]**

### T3.5.4 `vehicle_clear_all_radio_locks` (`0x00a647e0`)

**Arguments:** none read (`lua_gettop` result discarded).

**Return:** none.

**Body:** walks every vehicle in the world object manager (global `0x03171a64`: the vehicle index list
is `u16` indices at `+0xb8` with count `+0xc0`, into the object-pointer array at `+0x58`) and calls
`0x00a7c380(false)` on each (thiscall). `0x00a7c380` is the variant-1 setter for **`+0x1d7c` bit
`0x4`**, tag `"m_force_flagsradio_controls_locked"` (`0x01168aa8`). The literal false is hardcoded.

**Side effects/subsystem:** clears the "radio controls locked" flag on all vehicles; on a networked
machine without authority over a given vehicle this emits one opcode `0x46` record per such vehicle
rather than writing the bit. `0x00a7c380` has two other callers (`0x008b004d` and the unnamed code at
`0x00a631ba`, plausibly the per-vehicle Lua setter). **[CONFIRMED — disassembly. HYPOTHESIS — the
`0x00a631ba` pairing.]**

### T3.5.5 `vehicle_set_2d_tank_controls` (`0x00a62d00`)

**Arguments:** 1 boolean (arg 1), no nil-gate. No vehicle name.

**Return:** none.

**Body:** `0x00a91df0(flag)`. If a session exists and this machine is the host, it first sends an
opcode `0x42` record (8-bit sub-header `0x1f`, then the boolean) through the `0x0086f1b0` commit
with the session pointer. On every path it then stores the boolean in the global byte `0x0278cad8`.

**Side effects/subsystem:** one global control-mode switch, mirrored host → clients.
**[CONFIRMED — disassembly. HYPOTHESIS — it belongs to the RC-vehicle controls: the adjacent global
`0x0278cad4` is the RC signal range used by §T3.5.6.]**

### T3.5.6 `vehicle_rc_get_signal_strength` (`0x00a64100`)

**Arguments:** 2 mandatory strings: arg 1 vehicle name (`0x00a281e0`), arg 2 character name
(`0x00a281a0`).

**Return:** exactly 1 number (`lua_pushnumber`, function returns 1). 0.0 when either name fails to
resolve.

**Body:** `0x00a92430(vehicle, character)` returns `1 − d / R`, where `d` is the **horizontal**
distance between the two objects' positions (`+0x40` vectors; the code squares and sums the X and Z
differences only — the Y difference is dropped by the shuffle at `0x00a9247f`) and `R` is the float
global `0x0278cad4`, which `0x00a906b0` writes and reads.

**Side effects/subsystem:** pure query. The value is not clamped: 1 at the controller, 0 at range
`R`, **negative beyond it**. **[CONFIRMED — disassembly, including the X/Z-only distance. OPEN —
where `R` is set from (runtime global, only `0x00a906b0` writes it).]**

### T3.5.7 `vehicle_is_vtol` (`0x00a66170`)

**Arguments:** 1 string (vehicle name). A NULL/non-string arg reads as no vehicle.

**Return:** exactly 1 boolean on every path.

**Body:** inline `0x0062a190` resolution (no character redirect); pushes true only when
`0x00ad3200(vehicle)` is true (class 4); every failure pushes false.

**Side effects/subsystem:** pure query. **[CONFIRMED — disassembly.]** The second Ghidra candidate,
`0x00c1e650`, is not a binding: its flagged "use" at `0x00c1e6d0` is a `CALL 0x00c2fe60` with no
reference to the name string — a spurious analysis reference; the function is unrelated code.

### T3.5.8 `vehicle_exit_group_do` (`0x00a67a00`) / `vehicle_exit_group_check_done` (`0x00a66b00`)

Both take a Lua table of character names and walk it the same way: `0x0083dff0(L, k, 0)` returns the
table's `n` field (string `"n"` at `0x0129d9cc`) if it is a number, otherwise counts entries with
`lua_next`; it returns −1 when the slot is missing or nil. Each element `t[i]`, `i` = 1..count, is
read with `lua_pushnumber` + `lua_gettable` (`0x00dfe5e0`) + `lua_tostring`, popped with
`lua_settop` (`0x00dfde60`), and resolved through `0x00a281a0`. **[CONFIRMED — bodies.]**

Character vehicle state `+0x16d4`: `0x009b9160` tests `== 3`, `0x009b9230` tests `== 2`.
`0x0096f4f0` is an "is dead / gone" predicate: true for null, for `+0xcc8 == 5`, or for bit `0x1` of
`+0xe3`. **[CONFIRMED — bodies. HYPOTHESIS — 3 = seated in a vehicle, 2 = entering/exiting.]**

**`vehicle_exit_group_do` — Arguments:** 4. Arg 1 boolean, no nil-gate. Arg 2 optional boolean,
nil-gated, default false. Arg 3 optional boolean, nil-gated, default false. Arg 4 the table of names.
The table must be the 4th argument: with fewer arguments the counter sees no table (−1) and nothing
happens.

**Return:** exactly 1 boolean: true when at least one character was given an exit request.

**Body:** for each name that resolves to a character in state 3, builds an exit request on the stack
(handles −1, zero vector, a "now" stamp from `0x00d9e140(0)`, flag byte = arg 1 in bit `0x1` and arg 2
in bit `0x10`, a second flag byte with bit `0x2` set and bits `0x1`/`0x4` cleared) and hands it to
`0x00af3cd0(character, &request)`. The vehicle handle (`+0x16c0/+0x16c4`, via `0x009b9130`) of the
first successful character is remembered. Afterwards that handle is looked up in the handle table
`0x024433a8` (`0x00458230`; kind bit `0x80` at descriptor row `+6`, alive) and `0x00a7bdc0(arg 3)` is
called with that vehicle as `this` — **or with `this` = NULL when no vehicle was found** (`XOR ECX,ECX`
at `0x00a67c43`).

**Side effects/subsystem:** orders a group of characters out of their vehicle(s). **[CONFIRMED —
disassembly of the root, `0x0083dff0`, `0x009b9160`, `0x009b9130`, `0x00458230`. OPEN — `0x00af3cd0`
and `0x00a7bdc0` (not dumped at depth 1); whether `0x00a7bdc0` tolerates a NULL `this`; the meaning of
the arg 1 / arg 2 flags.]**

**`vehicle_exit_group_check_done` — Arguments:** 1 table of names (arg 1).

**Return:** exactly 1 boolean: false if any listed character resolves, is not dead/gone, and is in
vehicle state 3 or 2; true otherwise — including for an empty or missing table.

**Side effects/subsystem:** pure query, the polling partner of the call above. **[CONFIRMED —
disassembly.]**

### T3.5.9 `vehicle_stop_do` (`0x00a67c80`)

**Arguments:** 1 mandatory string: a character name (its current vehicle is used) or a vehicle name.

**Return:** none on every path.

**Body:**

- **Name resolves through `0x00a280c0`** (e.g. a character): its vehicle handle `+0x16c0/+0x16c4`
  must be non-zero and must find a live vehicle in the handle table `0x024433a8` — if the name
  resolves here but the object has no vehicle, the call does nothing (the vehicle path below is not
  tried). Authority is checked
  on **the character** (`0x008addb0(character, 0)`): true → `0x00abe510(vehicleHandle, 1)`; false →
  `0x00a66d30`.
- **Otherwise** through `0x00a281e0` (vehicle). Authority checked on the vehicle: false →
  `0x00a66d30`. True → if the byte at vehicle `+0xc68` is 1, `0x00abe510(vehicleHandle, 1)`; else
  `0x00b46ca0(vehicle + 0xc68)` then `0x00a90c70(vehicle, 1)`.

`0x00a66d30` takes the vehicle in `ESI` (register-passed; it is called with no stack arguments while
`ESI` holds the vehicle on both paths): it sends an opcode `0x43` record (8-bit sub-header `0x22`, the
vehicle's 16-bit id from `0x008add70`) through the multi-target `0x0086f110` (sync id
`0x008ae020(vehicle)`) — a stop request forwarded to whoever owns the vehicle.

`0x00abe510(handle, flag)` (also used by `teleport_vehicle_to_object`): looks the vehicle up; without
authority it sends an opcode `0x42` record (sub-header `0x18`, handle, flag) through `0x0086f110`; with
authority, and only when `+0xbd0` is 1 and `+0x81c` is non-null: when the flag is set, clears a
16-byte value (`0x00524870`/`0x00524890`) and sets bit `0x2000000` of `+0x16c8`; if the vehicle is the
local player's (`+0x2880/+0x2884` of the player) calls `0x009eccb0()`; unless `0x00ad31a0(vehicle)`,
stops the `+0x808` sub-object (`0x00a90550(0)`, `0x00ae8fd0(0)`, `0x00ae5710()`) and, if
`0x00ab7000(vehicle)`, `0x00ab7920(vehicle, 0, 0)`.

`0x00a90c70(vehicle, 1)`: when `+0xbd0` is 1 and the class from `0x00ad2f80` is 0-2, or is 4 with
`0x00ad3700` false, calls `0x00a90630(1)` on the `+0x808` sub-object.

**Side effects/subsystem:** halts a vehicle's AI/scripted driving. **[CONFIRMED — disassembly of the
root, `0x00a66d30`, `0x00abe510`, `0x00b46ca0`, `0x00a90c70`. OPEN — the inner calls named above;
HYPOTHESIS — `+0xc68` is a scripted-path follower record and `+0x808` the AI driving controller.]**

### T3.5.10 `vehicle_pathfind_to_do` (`0x00a680c0`)

**Arguments:** 4, read positionally with no nil-gates. Arg 1 vehicle name (`0x00a281e0`). Arg 2 target
name (string). Arg 3 boolean, arg 4 boolean (absent → false).

**Return:** exactly 1 value, but **its type depends on the path**: boolean false on every failure;
on the main path a **number**, the result of `0x00b2b140`. A 0 result is pushed as the number 0,
which Lua treats as true.

**Body:** needs a vehicle with a driver (`0x00ab5070(vehicle, 0)` non-null); else pushes false.
Then measures arg 2 with an inlined `strlen` — **before** the NULL test at `0x00a68192`, so a
non-string arg 2 (NULL from `lua_tostring`) is dereferenced. Then:

- **empty string:** no target object; the point builder `0x00a3b860` gets a null path and returns 0 →
  pushes false.
- **path object:** `0x0062a1f0` on registry `0x02442750` (descriptor bit `0x2` at row `+8`), alive →
  `0x00a3b860(L, …, pointsOut, vehicle position, path, 0, 1, 0)` copies the vehicle's own position
  plus the path's nodes (`+0x33c` count, `0x00930e90` per node), capped at 25 points. Fewer than 2 →
  pushes false.
- **else the `0x005982e0` kind** (bit `0x2` at row `+0xb`), alive → 2 points: the vehicle's position,
  then the target's.
- **neither** → pushes false (via the push wrapper `0x0059f8a0`).

With ≥ 2 points: takes a request id from `0x006f62b0(vehicle, 2, 0)` and calls
`0x00b2b140(vehicle, points, count, arg 3, 0, arg 4, -1.0f, id)`; when that returns 0 it releases
the id (`0x006f6360(vehicle, 2)`); either way it pushes the returned integer as a number.

**Side effects/subsystem:** starts AI pathfinding of a vehicle along a named path or to a named
point. **[CONFIRMED — disassembly of the root, `0x00ab5070`, `0x0062a1f0`, `0x00a3b860`'s entry and
null-path exit, `0x0059f8a0`. OPEN — `0x00b2b140`'s semantics and its return value's meaning, and
what arg 3 / arg 4 select. The NULL-arg-2 dereference is CONFIRMED from the listing order.]**

### T3.5.11 `vehicle_set_malfunctioning` (`0x00a65540`)

**Arguments:** 1 mandatory string (vehicle name, `0x00a281e0`) + 1 optional boolean, nil-gated,
**default true**.

**Return:** none.

**Body:** **does nothing unless the vehicle is class 4 (VTOL)** — `0x00ad3200` is the only gate.

- **true:** adds 100.0 (`0x0111645c`) to the float at **local player** (`0x009da4e0`) `+0x1650`,
  clamped to [0, the runtime float `0x02625834`] (`0x00975f00`); sets the global byte `0x02625852` to 1
  (`0x00975f80`); on the vehicle, sets bit `0x1` of `+0xf8` when the dword at `+0x5e8` is 0 or 3, and
  always sets bit `0x2` of `+0xf8`; then selects the entry whose hash matches `"cyber_m16_glitch"`
  (`0x0117e898`, hashed by `0x00d9e8b0`) out of the 64-byte-entry table at `0x013ef5c0` (count
  `0x013ef58c`) into slot 0 of `0x013ef578` (`0x0059f760`; 0 when not found).
- **false:** zeroes the four floats at local player `+0x1650..+0x165c` (`0x00975720`); sets
  `0x02625852` to 0; clears slot 0 of `0x013ef578` (`0x0059eef0`); clears bit `0x2` of vehicle `+0xf8`
  (bit `0x1` is left alone).
- **Both:** sends an opcode `0x43` record (8-bit sub-header `0x20`, the vehicle's 16-bit id from
  `0x008add70`, the boolean) through the `0x0086f1b0` commit with the session pointer. There is no
  authority or host gate — every machine that runs the call sends it.

**Side effects/subsystem:** a VTOL "malfunction" mode with a screen-glitch effect applied to the
**local player** whoever is flying. **[CONFIRMED — disassembly of the root and all of `0x009da4e0`,
`0x00975f00`, `0x00975f80`, `0x00975720`, `0x0059f760`, `0x0059eef0`, `0x00d9e8b0`. HYPOTHESIS —
`+0x1650` is a screen-distortion intensity and the `0x013ef578` slots are active post-effect presets.
OPEN — what `+0xf8` bits `0x1`/`0x2` do for the vehicle, and `0x00bc5610` (called after
`0x008add70`, result unused here).]**

### T3.5.12 `vehicle_component_detach` (`0x00a63870`)

**Arguments:** 2 mandatory strings + 4 optional, all nil-gated. Arg 1 vehicle name (`0x00a281e0`).
Arg 2 component name. Arg 3 boolean, default true. Arg 4 boolean, default true. Arg 5 number
(`lua_tonumber` + truncating cast), default 0. Arg 6 boolean, default false.

**Return:** none.

**Body:** needs the vehicle's byte `+0xbd0` = 1. `0x00a89120` finds the component by name: a
case-insensitive scan of the component array (`+0x68c`, count `+0x688`) comparing each component's
`(+0x80)+0x40` name string; −1 → nothing. Then
`0x00a8eab0(component, vehicle, arg 4, arg 5, arg 3, arg 6, 0, 0)` — note the argument order.

`0x00a8eab0` (recursive): when the component is valid, its "spawn detached piece" step
`0x00a8e980(component, vehicle, arg 4, arg 5)` runs only if arg 3 is true **and** none of these
vetoes hold: vehicle `+0x16c8` bit `0x1` set with a component kind (`(+0x80)+0x44`) other than 2/3 and
a computed vector entirely below 0.125 (`0x012a2fb4`); component kind `0x12` or `0x17`; component
flag `0x80000` with `0x00b12d60` failing; `0x00bc5610()` true; vehicle `+0x16d0` bit `0x4`. It then
recurses into the component's children (`(+0x80)+0x50` count, `+0x54` index list), skipping children
flagged `0x400000` or already `0x200000`; children flagged `0x40` are instead handed to `0x00a8cd50`,
and only when arg 6 is true. For component kinds 2/3, when the dword `0x180` bytes into the
component-array block is non-null, `0x00ae7060(slot, 0)` is also called (slot = the component's
position in the 8-entry list at `(+0x7fc)+0x24`, or −1). Finally — on every
path — it marks the component detached (**bit `0x200000` of `+0xf0`**) and calls `0x00a89360`,
`0x00754410(component)`, `0x00aa4750(vehicle, index)`, `0x00aaf1e0(vehicle, index)` and
`0x00abf810(vehicle, component, 0)`.

**Side effects/subsystem:** detaches a named part (and its sub-parts) from a vehicle, optionally
spawning it as a loose piece. **[CONFIRMED — disassembly of the root, `0x00a89120`, `0x00a8eab0`'s
control flow and argument routing. OPEN — the inner calls; what arg 4 / arg 5 / arg 6 mean
physically (HYPOTHESIS: arg 3 = spawn debris, arg 6 = also handle special `0x40` children).]**

---

## T3.6 Character turn, vehicle teleport, team relation — 3 functions

### T3.6.1 `turn_to_do` (`0x00a61e10`)

**Arguments:** 2 mandatory strings + 1 optional boolean. Arg 1 character name (`0x00a281a0`). Arg 2
target object name. Arg 3 boolean, nil-gated, default false.

**Return:** exactly 1 boolean on every path.

**Body:** the target's position and 3×3 orientation come from the established position resolver
`0x00a455a0` (spec §20.24/§21.24), seeded with the runtime vector `0x029cdb98` and the identity matrix
at `0x01321400` (`0x00da4130`). If the character is missing or the target does not resolve → pushes
**true**. With arg 3 false, if the character already stands exactly on the target position (exact
float compare of `+0x40/+0x44/+0x48`) → pushes **true**. Otherwise it takes a request id from `0x006f62b0(character, 0, 0)`,
and the facing point is the target position (arg 3 false) or **the character's own position plus the
target's third orientation row** (arg 3 true — i.e. "face the way the target faces"). Pushes the
result of `0x00987410(character, point, 0, id, 0)`.

`0x00987410` returns: true for a null character; true when `0x009433d0(character)` refuses (it then
clears `+0xa50..+0xa5c` and releases the id via `0x006f66d0`); **false** without local authority
(after sending an opcode `0x41` record — sub-header value 6 written through `0x00898cb0`, then the
network id `0x0050f790`, the point `0x004d2d40`, the zero third argument and the 16-bit id — through
`0x0086f110` with sync id `0x008ae020`; the record is only sent because this caller passes 0 as the
fifth argument); false when byte `+0xa98` is non-zero; otherwise the result
of `0x00987200(character)`. On the authority path it saves the old direction (`+0xa50` → `+0xa8c`),
swaps the stored request id at `+0xa88` (releasing the old one via `0x006f6680`), stores the new
direction at `+0xa50` with **its Y component zeroed** and normalised (`0x004d6210`) unless the vector
is near zero (all components ≤ 1e-5). For the local player in some states (`0x00941da0`, byte `+0xccc`
zero, `0x00943a20`, `0x009ba7e0`/`0x009baaa0`, and `0x005649c0() != 1`) the direction is taken from
the global vector `0x013c87b0` instead of from the point.

**Side effects/subsystem:** asks a character to turn toward (or align with) an object; the return
reads as "already done" — true when there is nothing to do or the turn finished at once, false while
it is pending. **[CONFIRMED — disassembly of the root and `0x00987410`'s return paths (`XOR AL,AL` at
`0x00987510`, `MOV AL,0x1` at `0x00987584`). HIGH CONFIDENCE — the "already done" reading. HYPOTHESIS —
`0x013c87b0` is a camera direction. OPEN — `0x00987200`, `0x009433d0`.]**

### T3.6.2 `teleport_vehicle_to_object` (`0x00a60ae0`)

**Arguments:** 2 mandatory strings + 5 optional, all nil-gated. Arg 1 vehicle name **or** a character
name (its current vehicle). Arg 2 target object name (`0x00a29200(name, 1)`, the generic game-object
chain). Args 3, 4, 5 numbers, default 0.0 — offsets along the target's orientation rows 0, 1, 2. Arg
6 number, default 0.0 — a heading in degrees (×0.017453). Arg 7 boolean, default true.

**Return:** none.

**Body:** target unresolved → nothing. Arg 1 through `0x00a280c0`: if it resolves, its
`+0x16c0/+0x16c4` vehicle handle is used; otherwise `0x00a281e0`, and the vehicle is **refused when
the dword at `(+0xbf8)+0xa8` is ≥ 0** (`0x00d9e4c0`). `0x004dcf00(handle)` must return the vehicle.
Then: `0x00abe510(handle, 1)` (the same stop routine as `vehicle_stop_do`); the always-1 stub
`0x00d1de80` (inert); for each seat `i` below `0x00ab4af0`, `0x00aa1f20(vehicle,
0x00aa2080(vehicle, i, 0.0))` (`0x00aa2080` maps a seat to a component index through `+0x1e00`,
−1 when missing or detached); builds the target's 4×4 matrix (`0x004aac40` from `+0x4c` orientation
and `+0x40` position), offsets the position by arg 3·row0 + arg 4·row1 + arg 5·row2, rotates the
orientation by arg 6 (`0x00520330`, `0x004f00f0`), and calls `0x00a874f0(vehicle, x, y, z,
&orientation, arg 7, 0)`.

**Side effects/subsystem:** stops and teleports a vehicle to an object-relative pose. **[CONFIRMED —
disassembly of the root and the small helpers. OPEN — `0x00a874f0`, `0x00aa1f20`, what arg 7 selects,
and the meaning of the `(+0xbf8)+0xa8` refusal; it applies only to the vehicle-name path, not the
character path.]**

### T3.6.3 `team_make_unfriendly` (`0x00a60940`)

**Arguments:** 2 mandatory strings (team names).

**Return:** none.

**Body:** the exact sibling of spec §20.6 `team_make_allies` with relation value **1** instead of 4.
Both names go through `0x0094cc60` (hash `0x00dab330`, lookup `0x005961a0` on `0x02623a80`, then the
byte table `0x02623b48`; unknown name → 9). `0x009b7130(id1, id2, 1, true)` writes 1 to the relation
matrix `0x0262d940` (8 bytes per row) at `[id1][id2]` and, because of the trailing true, at
`[id2][id1]`. Then a local-only opcode `0x43` record (8-bit sub-header `0x30`, `id1`, `id2`, literal 1)
through `0x0086f1b0` with the session pointer.

**Refinement to spec §20.6's "team 5" note** (the same `0x009b7130`): the two "auxiliary arrays" are
the same matrix. `0x0262d946 + id1·8` is `[id1][6]` and `0x0262d970 + id2` is `[6][id2]`. So
whenever team 5 is one side, **team 6 gets the same relation** — team 6 mirrors team 5.
**[CONFIRMED — address arithmetic from the listing (`MOV [EAX+0x262d946]`, `MOV [ESI+0x262d970]`).]**

**Side effects/subsystem:** sets two named teams mutually unfriendly. **[CONFIRMED — disassembly.
OPEN — the matrix's row count. An unknown team name gives id 9, and `[9][x]` lies past an 8×8 block;
whether that overruns depends on the block size `0x009b70e0` initialises, not read here.]**

---

## T3.7 Cross-function observations

1. **The CAE combat-action table is now fully named** (§T3.4): 70 names read from the file image,
   plus the name↔index helpers. Any spec entry that hands a literal index to `0x004f4d00` or
   `0x004f4b90` can now name the action — e.g. `ai_do_scripted_advance` = 18 `move advance`.
2. **`0x006f62b0` is a shared script-request-id allocator keyed by object + channel** (channel 0 turn,
   2 vehicle pathfind, 3 AI action), and it cancels any earlier request on the same channel. This
   explains why the AI writers store its result in `+0x4aa` and why `vehicle_pathfind_to_do` releases
   it on failure.
3. **Four new vehicle force-flag bits** at `+0x1d7c`/`+0x1d7d` (§T3.5 shared facts), all with the
   variant-1 double gate; plus one new character bit, `+0x1c98` bit `0x2` (`dont_regen_health`),
   through a hybrid single-gate / string-tag shape.
4. **Inconsistent vehicle resolution:** two vehicle entry points skip the character→vehicle redirect
   that `0x00a281e0` gives every other one (§T3.5.1, §T3.5.7).
5. **Return-value quirks a reimplementation must copy:** `vehicle_pathfind_to_do` returns a boolean on
   failure but a number on its main path (0 is truthy in Lua); `turn_to_do` returns true when it
   cannot act; `boss_battle_matt_set_player_as_avatar` returns false after a successful restore;
   `vehicle_rc_get_signal_strength` goes negative out of range; `vehicle_exit_group_check_done` returns
   true for an empty table.
6. **Crash-shaped edges in the original:** `vehicle_pathfind_to_do` takes `strlen` of arg 2 before its
   NULL test; `vehicle_exit_group_do` may call `0x00a7bdc0` with a NULL `this`; `team_make_unfriendly`
   (and so §20.6) indexes the relation matrix with id 9 for unknown team names. All three are flagged,
   not resolved.
7. **Team 5 ↔ team 6 mirroring** in `0x009b7130` refines spec §20.6.
8. **`vehicle_set_malfunctioning` only works on VTOLs**, and its glitch effect lands on the local
   player whoever is flying, with an ungated network record.
9. **Inert calls:** `0x00d221a0` (always 1, `boss_battle_matt_hide_wings`), `0x00d1de80` (always 1,
   `teleport_vehicle_to_object`), and the discarded `0x0096f4f0` predicate inside `0x004f4d00`.

## T3.8 Direct answer — all 25 names

All 25 resolved (one `.rdata` string each, one registrar slot each in `0x00a20840`) and are written
up above. None failed.

| # | Name | Root | Status |
|---|---|---|---|
| 1 | `character_dont_regenerate` | `0x00a41370` | resolved, written up §T3.1.1 |
| 2 | `character_clear_combat_move` | `0x00a410e0` | resolved, written up §T3.1.2 (inner callees OPEN) |
| 3 | `boss_battle_matt_set_player_as_avatar` | `0x00a40930` | resolved, written up §T3.2.1 |
| 4 | `boss_battle_matt_hide_wings` | `0x00a40850` | resolved, written up §T3.2.2 |
| 5 | `auto_pickup_enable` | `0x00a3c880` | resolved, written up §T3.3.1 (trivial global setter) |
| 6 | `audio_ambient_emitter_resume` | `0x00a3c830` | resolved, written up §T3.3.2-3 |
| 7 | `audio_ambient_emitter_pause` | `0x00a3c800` | resolved, written up §T3.3.2-3 |
| 8 | `ai_suggest_action` | `0x00a3eb00` | resolved, written up §T3.4.1 |
| 9 | `ai_do_scripted_advance` | `0x00a3e370` | resolved, written up §T3.4.2 |
| 10 | `vehicle_stop_do` | `0x00a67c80` | resolved, written up §T3.5.9 |
| 11 | `vehicle_set_special_override_never_ghost` | `0x00a63300` | resolved, written up §T3.5.3 |
| 12 | `vehicle_set_malfunctioning` | `0x00a65540` | resolved, written up §T3.5.11 |
| 13 | `vehicle_set_invulnerable_to_player_explosives` | `0x00a66270` | resolved, written up §T3.5.1 |
| 14 | `vehicle_set_2d_tank_controls` | `0x00a62d00` | resolved, written up §T3.5.5 |
| 15 | `vehicle_rc_get_signal_strength` | `0x00a64100` | resolved, written up §T3.5.6 |
| 16 | `vehicle_pathfind_to_do` | `0x00a680c0` | resolved, written up §T3.5.10 (`0x00b2b140` OPEN) |
| 17 | `vehicle_is_vtol` | `0x00a66170` | resolved, written up §T3.5.7 (2nd candidate `0x00c1e650` spurious) |
| 18 | `vehicle_exit_group_do` | `0x00a67a00` | resolved, written up §T3.5.8 (`0x00af3cd0`/`0x00a7bdc0` OPEN) |
| 19 | `vehicle_exit_group_check_done` | `0x00a66b00` | resolved, written up §T3.5.8 |
| 20 | `vehicle_disable_explosion_and_damage_vfx` | `0x00a621f0` | resolved, written up §T3.5.2 |
| 21 | `vehicle_component_detach` | `0x00a63870` | resolved, written up §T3.5.12 |
| 22 | `vehicle_clear_all_radio_locks` | `0x00a647e0` | resolved, written up §T3.5.4 |
| 23 | `turn_to_do` | `0x00a61e10` | resolved, written up §T3.6.1 |
| 24 | `teleport_vehicle_to_object` | `0x00a60ae0` | resolved, written up §T3.6.2 (`0x00a874f0` OPEN) |
| 25 | `team_make_unfriendly` | `0x00a60940` | resolved, written up §T3.6.3 |

Cleanroom self-check: the project's identifier-pattern grep over this file returns 0 hits (run before
hand-back).
