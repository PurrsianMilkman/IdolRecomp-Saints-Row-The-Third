# Ranking tranche 07 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-07.json` (names 101-125 of the 554
unspecced names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t07`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved.** Every name string occurs exactly once in `.rdata` (dump `index.txt`).
Two follow-up runs, both depth 0 `func` mode, are cited below as "registrar dump" (the seven non-gameplay registrars)
and "follow-up dump" (21 callee addresses the depth-1 dump stopped at, plus the two UI handlers the `lua` mode could not
reach).

Labels as in the other notes of this folder: **CONFIRMED** = read in the listing or decompile of the dumps;
**HIGH CONFIDENCE** = follows from dumped instructions, but a body it relies on was not dumped or a meaning is inferred
from strong usage; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in §H).

**Shared conventions, cited not re-described:** every handler opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives:
`0x00dfe210` `lua_tolstring`, `0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type`, `0x00dfe160` `lua_tonumber`,
`0x00ea2596` truncating float-to-integer cast, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe590` `lua_pushboolean`,
`0x00dfe4f0` `lua_pushcclosure`, `0x00dfe830` `lua_setfield`. The **standard nil-gated idiom** means an optional argument
is consulted only when enough arguments were passed and `lua_type` is not nil (0). Otherwise the default applies and the
argument position still advances. A **bare** boolean is a plain `lua_toboolean`, so an absent argument reads as
`false`. Resolvers: `0x00a281a0` generic character chain (`#PLAYER#` etc.), `0x00a28150` character chain, `0x00a281e0`
dedicated vehicle resolver, `0x005982e0` third-tier by-name object resolver (descriptor bit `0x2`@`+0xb`), `0x00853b10`
liveness ("true = dead/invalid"), id-pair resolution `0x00458230` on `0x024433a8` with tag `0x031d152c`. All by-name
resolvers are methods on singleton `0x02442750`, use the name registry at `+0x2660` (count `+0x265c`), and reject objects
with bit `0x10`@`+0x33`; they differ only in the descriptor bit they require (descriptor = entry of `0x02cc9900` indexed
by the object's byte `+0x34`). The replicate shapes are the ones in `spec-lua-api-behaviour.md` §7's preamble. The one
used most here is **"double gate `0x008ae480`/`0x008837a0`, string-tagged"**: when either gate is true the field is
written locally. Otherwise the owner is looked up with `0x008ae3a0(handle)` and a message of kind `0x46` carrying two tag
strings, the handle and the value is sent through `0x0086f110`, and nothing is written locally.

**Registration (CONFIRMED, index + registrar dump).** 16 names are stored by the gameplay registrar `0x00a20840`,
with the function pointer in the next slot (insn offset +1). The other 9 are **not** in `0x00a20840`, and they are also
not in the UI registrar `0x008430f0`. They live in seven small, screen-specific registrars. Each registrar calls
`lua_pushcclosure(L, fn, 0)` then `lua_setfield(L, -10002 /* 0xffffd8ee, globals */, name)`, either in a loop over a
name/function pair table on the stack (name slot, then function slot) or as a single direct pair:

| Registrar | Entries | Names from this tranche |
|---|---|---|
| `0x007c5ba0` | 3 (loop) | `store_dlc_is_offer_free` → `0x007c5b00`, `store_dlc_queue_icon` → `0x007c5b60` |
| `0x007d0ec0` | 7 (loop) | `save_system_save_game` → `0x007d0dd0`, `save_system_load_game` → `0x007d0e60`, `save_system_cancel_coop_load` → `0x007d0e90` |
| `0x0080dd70` | 3 (loop) | `store_common_rotate_mouse_drag` → `0x0080dc10` |
| `0x0080fab0` | 4 (loop) | `store_gallery_download_hide_list` → `0x0080f1a0` |
| `0x008111f0` | 5 (loop) | `store_gang_show_question_marks` → `0x00811170` |
| `0x0080dbc0` | 1 (direct) | `store_clothing_get_store_id` → `0x0080db90` |
| `0x0080d4d0` | 1 (direct) | `store_character_lineup_loaded` → `0x0080d4b0` |

Tooling notes: for the two direct registrars the `lua` mode's "nearest code pointer" heuristic picked `lua_setfield`
(`0x00dfe830`), so the real handler had to be read off the registrar decompile. The second dumps for
`store_dlc_is_offer_free` and `save_system_save_game` (`*_1.txt`, root `0x00dfe830`) are artifacts of the loop's shared
`PUSH` site, not second registrations. Ghidra had no function defined at `0x0080d4b0`; the follow-up dump disassembled it
in the read-only session. Who calls the seven screen registrars was not dumped (OPEN §H.1).

---

## A. Character action cluster — 5 functions

### A.1 `skydive_move_to_do` (`0x00a5f780`)

**Arguments:** (1) character reference, string, via `0x00a281a0`. (2) target name, string, no nil check (a null string
is treated as "not found"). (3) **use-path** flag, bare boolean. (4) optional point index, standard nil-gated, truncating
cast, default 0. (5) optional boolean, nil-gated, default `false`. (6) optional boolean, nil-gated, default `false`.
CONFIRMED.

**Return:** 0 values on every path. CONFIRMED.

**Body (CONFIRMED, listing + decompile):**
- *Use-path true:* the target name is resolved with `0x0062a1f0` on `0x02442750`. This is a by-name resolver of the
  shared shape that requires descriptor bit `0x2`@`+0x8` (note: byte `+0x8`, not `+0xb` as for `0x005982e0`). Then the
  liveness check runs. The index is compared **unsigned** against the object's point count at `+0x33c` (`JNC`), so a
  negative index is rejected too. The target point is `0x00930e90(index)` = object `+0x3c + 12 × index`, a 12-byte
  vector. Any failure here returns with no effect.
- *Use-path false:* the target name is resolved with `0x005982e0` plus liveness, and the target point is the object's
  position at `+0x40`.
- The character must resolve, `0x009adb00(character)` must hold (dword `+0xcc8` == `0xe`), and `0x0095d740(character)`
  must be false. Then:
  - `0x004dd380(character, 0x1f, null)` switches the character's action-state byte `+0x501` to `0x1f`. It first calls
    the old state's exit handler from table `0x012e1540` (stride `0x24`, skipped for sentinel `0x2d` or a null entry),
    saves the old state in `+0x502`, writes `+0x500` = `0xb2` and `+0x503` = 1, and clears bits `0x3` of `+0x44c`. It
    then calls the new state's enter handler from `0x012e153c`.
  - `0x004dd2d0(character, 0x1f, 3)` (follow-up dump): if still in state `0x1f`, sets sub-state `+0x500` = 3. Its side
    calls `0x0094f720` run unless the sub-state is `0x24`, and `0x0094f890` unless it is `0x25`. If the state is no
    longer `0x1f`, it calls `0x00754410(character, 0)` instead.
  - The 12-byte target point is copied to character `+0x468..+0x470`.
  - Flag byte `+0x4a5` gets bits `0x0c` replaced: bit `0x08` = arg 5, and bit `0x04` = **NOT** arg 6.

**CRASH-SHAPED — CONFIRMED (listing `0x00a5f8ce`–`0x00a5f924`).** On the use-path-false branch, when the name is absent
or does not resolve or the object is dead, the code zeroes the object register and then forms the target pointer as
"object + `0x40`" = **`0x40`**. The following null test (`TEST EDI,EDI`) can never fire on that value. If the character
passes the three gates, the copy at `0x00a5f924` reads 12 bytes from address `0x40`, which is an access violation.
Reproducer shape: `skydive_move_to_do(<character in state 0xe>, "no_such_object", false)`, or `nil` as arg 2. The
use-path branch does return early on failure. An implementation should treat an unresolved non-path target as "no
effect".

**HYPOTHESIS:** `+0xcc8` == `0xe` is the free-fall/skydive locomotion state; state `0x1f` is "skydive move-to". Arg 5's
bit (`0x08`) and arg 6's inverted bit (`0x04`) have no confirmed meaning (OPEN §H.2).

### A.2 `rappel_enter` (`0x00a5c4a0`)

**Arguments:** (1) character, string, via `0x00a281a0` (an unresolved character returns immediately). (2) optional
**queue** flag, nil-gated, default `false`. (3) optional target object name, nil-gated, default none. (4) optional
integer, nil-gated, truncating cast, default **−1**. CONFIRMED.

**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED):**
- If arg 3 resolves (`0x005982e0` + liveness), the position is object `+0x40..+0x48` and the direction is the object's
  `+0x58` row with its Y component zeroed (`0x00d9fa80`). If that flattened vector is degenerate, the direction becomes
  the object's `+0x64` row, unflattened. "Degenerate" is `0x00d9f9d0`: all three components strictly inside ±1e-7.
- With no target, the position and direction pointers are null.
- Queue flag true → `0x009a5ea0(character, pos, dir, int, 0)`. Queue flag false → `0x009a64c0(character, pos, dir, int, 0)`.

**`0x009a5ea0` — queued rappel (CONFIRMED, depth-1 decompile):**
- Stores position, direction and the integer into one of **two** slots of the table at `0x0262d170`. The stride is
  `0x1c`: position at `+0`, direction at `+0xc`, integer at `+0x18`. The slot index is 0 when `0x0091f5e0(character)`
  is true, otherwise 1. `0x0091f5e0` is "is the local player" (follow-up dump: compares with `0x009da4e0()`). A null
  position or direction is replaced by `0x00d9fc60` on the slot field (HYPOTHESIS: zero vector).
- Next it **always** builds a kind-`0x41` message with op `0x1f`. The fields are: the character, sub-op 0, has-position
  flag + position, has-direction flag + direction, the integer, and a trailing `1` byte. It sends this to the co-op
  partner, `0x008ae020(0x009df3d0(0,0,0))`.
- Only if `0x008addb0(character, 0)` is true is the effect applied locally: `0x0094cca0(c,0,0)`, `0x0094f450(c,0,1)`,
  `0x009b0750(c)`, `+0x1c98 |= 0x81`, `0x00942110(c, 0x12, 0, -1.0)` (enter state `0x12`), and `0x00986920(c, 0xd, 0x24)`.
  `0x00986920` writes sub-state bytes `+0xccd`/`+0xcce`; follow-up dump, CONFIRMED that it writes `+0xcce`.

**`0x009a64c0` — immediate rappel (CONFIRMED, depth-1 decompile):**
- If `0x008addb0(character, 0)` is false: send kind `0x41`, op `0x1f`, sub-op 0, position/direction/integer and a
  trailing `0` byte to the owner (`0x008ae020(character)`), then return.
- Otherwise it requires all of:
  - state `+0xcc8` ≠ `0x12` (not already rappelling);
  - the character's vehicle handle pair `+0x16c0/+0x16c4` is zero (the same pair `0x00a281e0` follows for its
    character-to-vehicle hop), i.e. not in a vehicle;
  - global float `0x0262d160` lies in [0, 90] (OPEN §H.3).
- It then:
  - runs `0x0094cca0` and `0x009b0750`, sets `+0x1c98 |= 0x81`, clears bit `0x80` of `+0xe8`, calls `0x00d34dd0`;
  - enters state `0x12` with sub-state `0x23` via `0x00986920(c, 0xd, 0x23)`;
  - if the character has no attached child named "Jump_Rope" (`0x009a6390` walks the child ring at `+0x1c`/`+0x20`
    looking for descriptor bit `0x40`@`+0x9` and that name hash), creates a "ga_cable" object at the character's
    `+0x40`/`+0x4c`, registers it (`0x00456e30(0x1e, …)`) and attaches it (`0x009215b0`, `0x008fea50(c, 1)`).
- Owner-side continuation:
  - writes `+0xa50..+0xa5c` from a `0x00da38e0` result, zeroes `+0xa60` and `+0xa68`, and copies the global vector at
    `0x029cdb98` into `+0xa70..+0xa78`;
  - aims at (position, or the character's own `+0x40`) + 5 × forward via `0x009b1fe0`;
  - teleports to the position only if one was given (`0x00996b50`, or `0x009e2220` when `0x00971cf0(c)` is non-null);
  - **if the integer is `< 8` unsigned** (so the default −1 is skipped), calls `0x007f4650(int, 1, 1)` for the local
    player only and `0x00600720(int, 1, 0, 0)`;
  - finishes with `0x0094b070(0x1ac)` and `0x0062a150(c)`.

**Implementer notes:** (a) the queued path sends its message to the co-op partner before the ownership check, while the
immediate path sends to the owner only when not owner. These are deliberately different, so an implementation must keep
both. (b) Any character that is not the local player, including an NPC, shares queued slot 1, so two NPC queued
rappels overwrite each other's slot (CONFIRMED indexing; whether scripts ever do this is OPEN). (c) The integer's
meaning is OPEN (§H.3). It is bounds-checked (`< 8`) before use, so it is not crash-shaped.

### A.3 `rappel_exit` (`0x00a5c690`)

**Arguments:** (1) character via `0x00a281a0`. (2) optional boolean, nil-gated, default **`true`** (the opposite default
to `rappel_enter`'s flag). (3) optional target object name, nil-gated. CONFIRMED.

**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED):** if the target resolves (`0x005982e0` + liveness), the position is `+0x40..+0x48` and the
direction is the target's `+0x64` row. Note the asymmetry: enter prefers the flattened `+0x58` row, exit uses `+0x64`
and does no degeneracy test at this level. Calls `0x009a6b80(character, pos|null, dir|null, flag, 0)`.

**`0x009a6b80` (CONFIRMED, depth-1 decompile):**
- If not owner: send kind `0x41`, op `0x1f`, **sub-op 1**, position/direction and the flag to the owner, and return.
- Otherwise, require state `+0xcc8` == `0x12`.
- If the sub-state byte `+0xcce` is `0x24` (a **queued** rappel from A.2): leave the state (`0x00942110(c, 0, 0, -1.0)`),
  clear the character's queued slot position and direction (`0x00d9fc60` twice, slot chosen by `0x0091f5e0`), call
  `0x009a6440(c, 0)`, and stop.
- If not owner at this second check: `0x009a6440(c, 0)` plus leave the state.
- Owner, live rappel:
  - Direction: the given one, or the character's own `+0x58` row. If that is degenerate, the `+0x64` row is used.
  - The direction is then flattened (Y = 0). If it is still degenerate, the constant vector at `0x013213f0` is used.
  - The orientation is rebuilt with `0x00da5aa0(dir, 0x013213e4)` and the state is left. The fields
    `+0xa50..+0xa78` are rewritten exactly as in A.2's owner continuation. Then `0x009a6440(c, 0)` is called
    (HYPOTHESIS: cable detach).
- Landing, case `0x00971cf0(c)` is null: sub-state `0x00986920(c, 0, 0)`, then a teleport to the position if one was
  given, else orientation-only.
- Landing, case `0x00971cf0(c)` is non-null: sub-state `(1, 0)`.
  - If a position was given, teleport there via `0x009e2220`.
  - If no position was given **and the flag is true**, a probe `0x009532a0` from the character's position is tried. On a
    hit the character is placed at the hit point; otherwise orientation-only.
- Then aim at position + 5 × forward (`0x009b1fe0`), `0x0094b070(-1)`, `0x0062a170(c)`.

**HYPOTHESIS:** the flag means "find ground below when no explicit target" (only consulted in the
`0x00971cf0`-non-null, no-target case). Sub-state bytes: `0x23` = live rappel, `0x24` = queued (CONFIRMED as the values
A.2 writes and A.3 tests; names are HYPOTHESIS).

### A.4 `set_cower_variant` (`0x00a5e200`)

**Arguments:** (1) character, string, via `0x00a28150`. (2) variant, number, **no nil check** (absent reads 0),
truncating cast. CONFIRMED.

**Return:** 0 values. CONFIRMED.

**Body (CONFIRMED):**
- The variant is clamped to [0, 4], and the candidate value is **13 + variant** (13..17).
- It is validated against the character's animation set: `0x004b1630((character+0xd24)+0x10, value)`. This walks two
  sub-tables at `+0x30`/`+0x34` keyed by `+0x44` via `0x004ccf30`, and −1 means "not present". A −1 falls back to 13.
- `0x0094b490(character, &value)` then applies it under the **double gate, string-tagged** shape: the local write is
  character `+0x1cd8` = value; the remote tags are "human" / "cower_stand_variant" and the value goes out with
  `0x004def80`.

**Risk note (HYPOTHESIS):** `character+0xd24` is dereferenced without a null check. `0x0095d740` (A.1) does the same, so
it is probably always set on a resolved human.

### A.5 `set_unrecruitable_flag` (`0x00a5da40`)

**Arguments:** (1) character, string, via `0x00a28150`. (2) optional boolean, nil-gated, default **`true`**. CONFIRMED.

**Return:** 0 values.

**Body (CONFIRMED):** `0x009d6fa0(flag)` on the character, using the **double gate, string-tagged** shape with the
handle at character `+0x8/+0xc`. Local write: bit `0x10` of byte `+0x1e02` = flag. Remote tags: "npc" (string at
`0x01167294`) / "npc_force_flagsunrecruitable", with the boolean sent via `0x004d46e0`. The same tag string is also
referenced by `0x008af750` (HYPOTHESIS: the receive-side handler).

---

## B. Vehicle / world-object cluster — 5 functions

### B.1 `set_unjackable_by_ai_flag` (`0x00a5d920`)

**Arguments:** (1) vehicle, via the dedicated resolver `0x00a281e0`. (2) optional boolean, nil-gated, default
**`true`**. CONFIRMED.

**Return:** 0 values.

**Body (CONFIRMED):** `0x00b23660(flag)` with `this` = vehicle `+0xc68` (the embedded vehicle-AI component), **double
gate, string-tagged**, handle at component `+0x4e0/+0x4e4`. Local write: bit `0x02` of component `+0x5` (vehicle
`+0xc6d`). Remote tags: "vehicle_ai" / "vai_force_flagsunjackable_by_ai". The receive side is HYPOTHESIS: `0x008b08d0`,
the only other reference to the tag.

### B.2 `rc_set_max_signal_range` (`0x00a5b7d0`)

**Arguments:** 1 number, no nil check (absent reads 0), narrowed to float. CONFIRMED.

**Return:** **1 number, the previous range.** `0x00a906b0` swaps the new value into global float `0x0278cad4` and
returns the old one. CONFIRMED.

**Reader (follow-up dump, CONFIRMED):** `0x00a92430` computes `1 − horizontal_distance / range`. The distance is taken
between two positions from `0x00da38e0`, using only the X and Z components (Y ignored). Its callers are `0x00a924d0`,
`0x007f6f90`, `0x009e5dd0` and one site at `0x00a64156`.

**Edge:** `rc_set_max_signal_range(0)` (or no argument) makes the reader divide by zero. It is an SSE float divide, so
with default masked exceptions it yields ±inf/NaN, not a trap (HYPOTHESIS on the FP control word). The global is
zero-fill, and the startup default is OPEN (§H.4): there is an unexplained static reference at `0x01009a20`.

### B.3 `spotlight_heli_laser_size` (`0x00a5dec0`)

**Arguments:** 1 number, no nil check. **Return:** 0 values. **Body:** writes the narrowed float to global
`0x0130da2c` (static default **1.0**). CONFIRMED.

**Reader (follow-up dump):** `0x00aad570` (called from `0x00aae270`) is the only reader. It multiplies a distance by
`0x0130da6c` and by this size to get a half-width, which is then compared with a frame-scaled offset. HIGH CONFIDENCE
that this is the helicopter spotlight/laser beam width scale. There is no replication, no clamp, and no reset by the
host found in this dump. A negative value is accepted (OPEN whether that inverts the comparison sensibly).

### B.4 `roadblock_create` (`0x00a5cbc0`)

**Arguments:** (1) roadblock template name, string, no nil check. (2) placement object name, string. CONFIRMED.

**Return:** **exactly 1 number**: the new roadblock's 64-bit handle as a double, or **−1** on any failure. CONFIRMED.

**Body (CONFIRMED):**
- **Template lookup:** `0x0092c690` → `0x0092b760` hashes the name (`0x00d9e8b0`) and scans `[0x026171cc]` entries of
  stride `0x28` from `0x0261f588`, comparing entry `+4` to the hash. It **keeps scanning after a match and returns the
  last match**, so duplicate template names resolve to the last one. A null name gives null.
- **Placement object:** `0x005982e0` + liveness.
- `0x0092e5e0(template, object+0x40, object+0x4c, 2)` returns 0 (failure) when:
  - the template is null;
  - the road-network manager `0x02cc99b0` reports busy (virtual `+0x14`) or empty (virtual `+0xc` ≤ 0);
  - none of up to 8 candidate spots collected by `0x0092e150` within **7.0** units of the position is accepted by
    `0x0092b130(candidate, template)`.
- On success it builds the roadblock with `0x0092bf30(template, 2, candidate, orientation)`. When the candidate's lane
  count is 1..4, it records the lanes under the name pointed to by `0x01309234` ("roadblock_lanes"). It then registers
  the new object with `0x00456e30(0x2c, …)` on `0x02442750` and returns its handle pair (`+0x8/+0xc`).
- `0x00a48af0` pushes the handle as an unsigned 64-bit → double conversion.

**Precision note (HYPOTHESIS):** a handle above 2^53 does not round-trip exactly through a Lua number, and
`roadblock_destroy` (B.5) would then miss it silently. Whether real handles get that large depends on the id allocator
(OPEN §H.5).

### B.5 `roadblock_destroy` (`0x00a5c350`)

**Arguments:** 1 number (the handle from B.4). CONFIRMED.

**Return:** 0 values.

**Body (CONFIRMED):**
- The number goes through `0x00ea2596`, used here as a **64-bit** truncating cast: both `EAX` and `EDX` are kept, so this
  helper returns `EDX:EAX` and other callers simply use the low half. A zero handle is a no-op.
- Otherwise: id-pair resolution `0x00458230` on `0x024433a8`, reject bit `0x10`@`+0x33`, require **descriptor bit
  `0x10`@`+0xb`** (roadblock kind), liveness, then `0x00853ea0(object, 0, 0)`.
- `0x00853ea0`, the generic release, does nothing when all of these hold: the descriptor has bit `0x2`@`+0x6`, network
  byte `0x024d4461` == 1, `0x008addb0(object, 1)` is false, and object `+0x3c` is non-zero (CONFIRMED). Otherwise it
  probes the object with `0x004571e0` against two `.rdata` keys (`0x011645cb`, `0x011645cc`) and, on a match, calls
  `0x008537b0(object, 1 or 2)`; then it always calls `0x00457730(object, 0)` (CONFIRMED calls; HYPOTHESIS that
  `0x00457730` is the actual free and `0x008537b0` a release hook).
- A non-roadblock handle is ignored.

---

## C. Spawning and world-state globals — 5 functions

All five write process globals directly, with no object resolution. Three of the globals are also registered with the
session-synced-variable mechanism `0x0086d770(name, address, size, authoritative, change-callback)`, the same mechanism
tranche 05 noted for "allow_weapon_auto_pickup". Its 4th argument is computed identically everywhere: true when the
session `0x0087ba20()` exists and its dwords `+0x5c` == `+0x58` (HYPOTHESIS: "this machine is authoritative").

### C.1 `spawning_allow_gang_ambient_spawns` (`0x00a5dd00`)

**Arguments:** 1 optional boolean, nil-gated, default **`true`**. **Return:** 0 values.

**Body (CONFIRMED):** `0x00910830(flag)` writes the same byte to **all four** of `0x01308acc`, `0x01308acd`, `0x01308ace`,
`0x01308acf`.
- Reader `0x00910870` (called from `0x008be050`) returns true only if **all four are non-zero**.
- `0x009142a0` (from `0x00908fb0`, a reset/initialiser) sets all four to 1. File-backed statics are 1, 0, 0, 0, so
  before that reset the reader answers false.
- The same bytes also have an indexed writer `0x00910850` and an indexed reader `0x00915da0` (global xrefs).
- HYPOTHESIS: four independent gang/category gates, of which Lua can only set all at once.

### C.2 `spawn_region_enable_los` (`0x00a5fc70`)

**Arguments:** (1) spawn-region name, string, no nil check (a null name is a no-op). (2) enable, **bare** boolean. So a
call with one argument **disables**. CONFIRMED.

**Return:** 0 values.

**Body (CONFIRMED):** `0x006ed4d0` on `0x02442750` is the by-name resolver shape requiring **descriptor bit
`0x8`@`+0x8`**. Its only callers are this handler and its registrar neighbours `0x00a5fc00` / `0x00a5fcf0`. After the
liveness check it writes bit `0x10` of byte `+0x94` = **NOT enable**. HYPOTHESIS: bit set = "skip the line-of-sight test
when spawning in this region".

### C.3 `set_saints_hated` (`0x00a5d5a0`)

**Arguments:** 1 **bare** boolean. **Return:** 0 values. **Body:** `0x00702a20` writes byte `0x014ff6ba`. CONFIRMED.

The reader `0x00702a30` returns the byte and has many callers (`0x0051afe0` ×3, `0x008c14e0`, `0x008c3110`, `0x0091e2e0`,
`0x0091eb50`, …), HIGH CONFIDENCE that they are faction-relationship checks. `0x00704280` (from `0x00708330`) zeroes the
byte and registers it as synced variable **"saints_hated"** (size 1, no callback). The Lua setter writes the byte
directly; propagation is left to the sync mechanism (HYPOTHESIS: so on a non-authoritative peer the write can be
overwritten by the next sync).

### C.4 `set_stag_notoriety_area_active` (`0x00a5d670`)

**Arguments:** 1 **bare** boolean. **Return:** 0 values.

**Body (CONFIRMED):** `0x00704390(flag)`:
- writes byte `0x014ff6bb` = flag;
- takes the object name from pointer `0x012f44f8` ("LOCKDOWN_NOTORIETY") and resolves it with `0x00704150`, the by-name
  shape requiring **descriptor bit `0x40`@`+0xa`**, then checks liveness;
- if the object exists, writes object dword `+0x84` = flag (0/1) and **tail-calls** `0x005e3880(flag)`. If not, only
  the byte is written.

`0x005e3880` (follow-up dump) acts only when the session exists, `+0x5c` == `+0x58`, and the flag differs from the cached
`0x0149f208`:
- true: `0x0092d100`, `0x00d48e50`;
- false: releases every live handle pair in the table at `0x0149f6b0` (count `0x0149f20c`, stride `0x28`) via
  `0x00854410`, then `0x0101ba60`;
- then caches the flag.

`0x00704280` registers byte `0x014ff6bb` as synced variable **"stag_notoriety"** with change callback `0x00704230`.
That callback (global xrefs) also reads `0x012f44f8`, calls `0x00704150`, and reaches `0x005e3880` (call at
`0x00704275`), so a peer receiving the sync re-applies the same object update (HIGH CONFIDENCE). Neighbouring synced
variables registered in the same function: "friendly_fire" (`0x012f4500`, 4 bytes, callback `0x00702940`) and
"stag_active" (`0x014ff6b9`). Neither has a setter in this tranche.

### C.5 `set_cops_shooting_from_vehicles` (`0x00a5cf10`)

**Arguments:** 1 optional boolean, nil-gated, default **`true`**. **Return:** 0 values.

**Body (CONFIRMED):** `0x0051db30` writes byte `0x013ba5d0`. `0x0051dce0` (from `0x004dd260`) resets it to **0** and
registers it as synced variable **"passenger_cops"**. The reader `0x0051e4a0` (from `0x0051e780`) returns the byte in
one case of its switch, when `0x009431c0()` is true. HIGH CONFIDENCE: "may police passengers fire from vehicles". Note
the engine default after reset is `false`, while a bare `set_cops_shooting_from_vehicles()` sets `true`.

---

## D. Save-system UI cluster — 3 functions

All three operate on the save-system singleton at **`0x02297d88`**. Its dword `+0x84` (`0x02297e0c`) is a state and
`+0x88` (`0x02297e10`) the pending slot index. The slot records are a table of `0x60`-byte entries at pointer
`0x02297d08`, with count `0x02297d0c`. CONFIRMED: the listings load `ECX` = `0x02297d88` before the thiscall helpers, and
the absolute addresses equal singleton + offset.

### D.1 `save_system_save_game` (`0x007d0dd0`)

**Arguments:** 1 number (slot), truncating cast, no nil check. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED):** writes the pending slot (`0x02297e10`).

**Slot 99** (`0x63`) is the "new save" sentinel and goes straight to `0x007d0b70`:
- if `0x00b95340()` is false → warning "SAVELOAD_NOT_ENOUGH_SLOTS_NEW";
- else if `0x00b94880(0)` is false → warning "SAVELOAD_NOT_ENOUGH_SPACE_NEW";
- else `0x00b94a10(0x02297d28, 0, 0)` and `0x007d0930(0)`.
- Warnings use a dialog titled "MENU_TITLE_WARNING" via `0x007c3d80` with callback `0x007cfb00`, store the dialog's
  `+0x12c` in `0x02297d18`, and reset the pending slot to −1.

**Any other slot** opens a confirmation dialog through `0x007c3de0`: title key "SAVELOAD_SAVE_GAME", body key
"SAVELOAD_SAVE_GAME_CONFIRM_EXPOSITION", callback `0x007d0d60`. The dialog's `+0x118` is set to 1 and its `+0x12c` is
stored in `0x02297d1c`. The slot is **not validated here**.

Callback `0x007d0d60` (follow-up dump):
- choice 0 → `0x007d0b70` with `this` = `0x02297d88`. For a non-99 slot this does: if slot < 0 or slot > count, return
  without resetting pending. Otherwise `0x00b94750(0x02297d08 + slot × 0x60)`, `0x00b94a10(0x02297d28, 0, 0)`,
  `0x007d0930(0)`, pending = −1.
- other choice → if bit `0x8` of byte `0x02297da6` is set, invoke the Lua-side function "save_load_lock_input" via
  `0x00e0ca80`/`0x00e0cd00`, setting its `+0x14` from `0x02297dac`.
- `0x02297d1c` is cleared on both paths.

**BUG — off-by-one slot bound (comparison CONFIRMED; bug status HIGH CONFIDENCE).** `0x007d0b70` rejects only
`slot > [0x02297d0c]`, using `JG`/`count < slot`, so **slot == count is accepted**. That indexes the `0x60`-byte record
one past the end of the table. The sibling handler `0x007d07b0`, registered in the same registrar and not in this
tranche, bounds the same index with a strict `slot < [0x02297d0c]`. That establishes `0x02297d0c` as a count, so `<=` is
the defect. The same bound is in D.2. The consequence (a write via `0x00b94750` on a stray record) is HYPOTHESIS, since
`0x00b94750` was not dumped.

### D.2 `save_system_load_game` (`0x007d0e60`)

**Arguments:** 1 number (slot), truncating cast. **Return:** 0 values. CONFIRMED.

**Body (CONFIRMED):** writes the pending slot, then calls `0x007d0ca0` with `this` = `0x02297d88`:
- **Bound:** requires 0 ≤ slot ≤ `[0x02297d0c]` (`JL` / `JG`). This has the **same off-by-one as D.1**.
- If `0x00706ab0()` == 2:
  - `0x00706ab0` returns `0x01503b50[0x012f4a80]` when the index is ≥ 0, else 0; the meaning of mode 2 is OPEN (§H.6).
  - Calls `0x00b95210(record, 1)`. If true: `0x007aec20()`, and if state ≠ 3 → state = 3, pending = −1,
    `0x007d0930(6)`.
  - Pending = −1 in all cases.
- Else, if the session exists and `+0x5c` ≠ `+0x58` → `0x007d0c50(3)` directly.
- Else it is deferred: `0x0059f8c0(0, 0x007d0c50, 0)`.

`0x007d0c50` (follow-up dump) calls `0x00b95210(0x02297d08 + pending × 0x60, 0)` with **no bound check of its own**. On
success, if state ≠ 3 → state = 3, pending = −1, `0x007d0930(6)`.

**Crash-shaped (HYPOTHESIS):** on the deferred path the pending index is re-read when the callback fires. If anything
resets it to −1 in between (D.1's warning path, or D.2's own mode-2 branch, both store −1), the callback indexes record
**−1**, i.e. `0x60` bytes before the table. Whether the deferral window can overlap such a write is OPEN.

### D.3 `save_system_cancel_coop_load` (`0x007d0e90`)

**Arguments:** none read (the `lua_gettop` result is discarded). **Return:** 0 values. **Body:** if the singleton state
(`0x02297e0c`) is not 5, set it to 5. CONFIRMED. HYPOTHESIS: 5 = "co-op load cancelled", and 3 (D.2) = "load
committed".

---

## E. Store UI cluster — 7 functions

### E.1 `store_gang_show_question_marks` (`0x00811170`)

**Arguments:** 1 **bare** boolean. **Return:** 0 values.

**Body (CONFIRMED, listing; register-passed arguments reconstructed from the listing because the decompile dropped
them):**
- Writes the flag to byte `0x022cd9e5`.
- Reads the current customisation slot index from `0x022cd9d0`.
- `0x00838f60(index, &a, &b)` loads `a` = `0x023013cc[index]` and `b` = `0x023013dc[index]`. The two tables are `0x10`
  bytes apart, so at most 4 entries.
- If `a` is 0, `a` = `0x00be3360("npc_questionmark")`, a name-hash lookup into `0x029a7578`, 0 if absent.
- `0x00810060` (index in `EAX`) releases the slot's previous preview object: the handle pair at
  `0x022cdac8/0x022cdacc` + index × `0x40` goes through `0x00854410`, and slot `0x022cdac0` entry is reset to −1.
- `0x00810440` (index in `ECX`, `a` in `EDX`, stack `b`, 0) does:
  - if `a` is the "npc_questionmark" asset: mark `0x022cdadc`[index × `0x40`] = 1. Then, **only when the flag is
    false**, replace `a` by `0x00be2060(b, 0, 0)`; if that yields null, return with no preview;
  - otherwise mark 0;
  - then spawn a "gang_cust" preview through `0x00bbb4a0(…, a+0x8, 5)`, store its handle pair in the slot, and set
    object flags (`+0x1da7 |= 4`, `+0xe0 |= 0x8000000`).
- So `true` keeps the placeholder question-mark model and `false` reveals the real one (HIGH CONFIDENCE).

**CRASH-SHAPED — CONFIRMED path.** If the slot's `a` is 0 **and** the "npc_questionmark" lookup also returns 0 (asset
not loaded), `0x00810440` takes the "not the question-mark" branch, which has no null check, and reads `a+0x8` with
`a` = 0. **Also (HYPOTHESIS risk):** `0x022cd9d0` indexes the two 4-entry tables and the `0x40`-stride slot arrays with
no bound check.

### E.2 `store_gallery_download_hide_list` (`0x0080f1a0`)

**Arguments:** 1 **bare** boolean. **Return:** 0 values.

**Body (CONFIRMED):**
- **true:**
  - `0x00b990b0(*0x022cd894, 0)` clears **`0x246c` bytes** of the gallery-list object, re-initialises its list
    (`0x00b98f00(obj+0x8, obj+0x225c, 0)`), sets header bytes `+0x4` = 2 and `+0x5` = 1, and stores a checksum of
    `+0x4..` (`0x00d9e790`, length `0x2468`) at `+0`;
  - then the local player `0x009da4e0()` gets its byte `+0x20f8` = `0x009daed0(player)`, which is the byte at
    `(player+0x2088)+0x1c`, or 0 if that pointer is null;
  - then `0x00bd1480()`, which zeroes `0x02946e4c`/`0x02946e50` and releases `0x02946e48` via `0x00db53e0`.
- **false:** `0x00b97740(*0x022cd894)`, which is `0x00b97510(obj+0x8, obj+0x225c, 0)`, then `0x00bd1480()`.

**CRASH-SHAPED — CONFIRMED unchecked.**
- (a) The object pointer read from global `0x022cd894` (zero-fill runtime) is passed on with no null check: memset of
  `0x246c` bytes at null, or `null+0x8`, if the store gallery is not initialised.
- (b) On the `true` path the local player is not null-checked: `0x009daed0` reads `player+0x2088` directly. This crashes
  if the call happens with no local player, e.g. at the front-end. Whether either can occur in practice is OPEN (§H.7).

### E.3 `store_dlc_queue_icon` (`0x007c5b60`) — **no-op stub**

Reads arg 1 as a number and arg 2 as a string, discards both, returns 0 values. No side effects. CONFIRMED (whole body).

### E.4 `store_dlc_is_offer_free` (`0x007c5b00`) — **constant stub**

Reads arg 1 as a number and discards it. **Always returns one `false`.** CONFIRMED (whole body). An implementation should
return `false` regardless of input.

### E.5 `store_common_rotate_mouse_drag` (`0x0080dc10`)

**Arguments:** none read (the `lua_gettop` result is discarded). **Return:** 0 values.

**Body (CONFIRMED):**
- `0x00dcfcc0(&dx, &u, &v)` copies the mouse deltas from `0x02a381d8` / `0x02a381d4` / `0x02a381d0`, **but only when bit
  `0x10` of `0x0132abc0` is set**.
- The angle is `dx × −0.25` (the double at `0x01154b38`). Three targets, first match wins:
  1. **Current screen object `0x012fced8` non-null with `+0x30` == `0x2c`** → `0x00811800(angle)`, which rotates the
     current gang-customisation preview:
     - handle pair from the E.1 slot table at index `0x022cd9d0`, resolved by id pair;
     - requires descriptor bit `0x4`@`+0xa`, liveness, bit `0x8`@`+0x33`, and `0x004b8310((obj+0xd24)+0x10)` == 0;
     - the yaw is `0x0132a0ac` (≈ 1/30) × 180 × angle × 0.017453 rad, applied to the orientation at `+0x4c` through
       `0x00da4130`/`0x00da61b0` and virtual `+0x48`;
     - the motion fields are zeroed.
  2. Else, **if `0x008131f0()`** (store mode `0x022cdf0c` == 9, `0x00567c80()` true, and byte `0x01300b88` set) →
     `0x007b7cf0(handle pair 0x022cde00/0x022cde04, angle)`. This is skipped when `0x00707490()` is true. It requires
     descriptor bit `0x80`@`+0x6` and liveness, and rotates through virtual `+0x40`.
  3. Else → `0x007b7570(angle)`. This is skipped when byte `0x012fd0f9` is set or `0x00707490()` is true; otherwise it
     rotates **the local player** the same way.

**BUG — uninitialised input, CONFIRMED (listing `0x0080dc14`–`0x0080dc2c`).** The three output slots are fresh stack
space (`SUB ESP,0xc`) that is never initialised. When bit `0x10` of `0x0132abc0` is clear (HYPOTHESIS: mouse not the
active input device, e.g. a gamepad), `dx` is stale stack memory and the target rotates by a garbage angle. A
re-implementation should treat "no mouse delta" as 0.

**CRASH-SHAPED — CONFIRMED unchecked:** target 3 dereferences `0x009da4e0()` (the local player) with no null check.

### E.6 `store_clothing_get_store_id` (`0x0080db90`)

**Arguments:** none read. **Return:** 1 number = `0x0080d810()` (an int converted to double). CONFIRMED. The body of
`0x0080d810` was not dumped (OPEN §H.8).

### E.7 `store_character_lineup_loaded` (`0x0080d4b0`)

**Arguments:** none read. **Return:** 0 values. **Body:** sets byte `0x022ccc52` = 1. CONFIRMED (whole body). HYPOTHESIS:
a "lineup ready" latch that the store screen polls; its readers were not dumped.

---

## F. Cross-function observations

1. **Default asymmetry across boolean setters**:
   - **Bare (absent → `false`):** `spawn_region_enable_los` (C.2: absent *disables*), `set_saints_hated`,
     `set_stag_notoriety_area_active`, `store_gang_show_question_marks`, `store_gallery_download_hide_list`, and
     `skydive_move_to_do`'s use-path flag.
   - **Nil-gated, default `true`:** `spawning_allow_gang_ambient_spawns`, `set_cops_shooting_from_vehicles`,
     `set_unrecruitable_flag`, `set_unjackable_by_ai_flag`, `rappel_exit`'s flag.
   - **Nil-gated, default `false`:** `rappel_enter`'s queue flag, and `skydive_move_to_do` args 5 and 6.

   An implementation must reproduce each default individually.
2. **Three replication patterns in one tranche.**
   - Double-gate string-tagged (A.4, A.5, B.1).
   - Numeric kind `0x41` / op `0x1f` with sub-ops 0 (enter) and 1 (exit) for rappel (A.2, A.3). The queued variant
     broadcasts to the co-op partner unconditionally.
   - Session-synced global variables via `0x0086d770` ("saints_hated", "stag_notoriety", "passenger_cops"), where the Lua
     setter writes the raw byte. C.4 additionally runs a host-gated object update in `0x005e3880` and a mirror callback
     `0x00704230`.
3. **Rappel state model (CONFIRMED values):** state `+0xcc8` = `0x12` while rappelling. Sub-state `+0xcce` = `0x23`
   (live) or `0x24` (queued), written via `0x00986920(c, 0xd, …)`. Queued parameters live in a two-slot table at
   `0x0262d170`: slot 0 = local player, slot 1 = everyone else. The cable is a "ga_cable" object, re-used if a
   "Jump_Rope" child already exists.
4. **Character action-state switch** `0x004dd380(c, state, payload)`: byte `+0x501` plus the exit/enter handler tables
   at `0x012e1540` / `0x012e153c` (stride `0x24`, sentinel `0x2d`). The optional payload of `0x24` dwords is copied to
   `+0x8d0`. Skydive uses state `0x1f`. This helper has about 30 callers and is worth its own spec entry.
5. **New by-name resolver variants**: all share the `0x02442750` registry shape and differ in the required descriptor
   bit. `0x0062a1f0` (bit `0x2`@`+0x8`) resolves path-like objects with a 12-byte point array at `+0x3c` and count
   `+0x33c`. `0x006ed4d0` (bit `0x8`@`+0x8`) resolves spawn regions. `0x00704150` (bit `0x40`@`+0xa`) resolves the
   lockdown-notoriety object.
6. **`0x00ea2596` returns 64 bits** (`EDX:EAX`). `roadblock_destroy` relies on that; most callers use only `EAX`.
7. **Two UI handlers are stubs** (E.3, E.4). Seven UI-side names are registered by screen-local registrars, not by
   `0x008430f0`. The `lua` dump mode mis-resolves direct (non-table) registrations, which affected E.6 and E.7.

### Crash/defect summary (for the spec's defect list)

| # | Where | Kind | Status |
|---|---|---|---|
| 1 | `skydive_move_to_do` `0x00a5f8ce` | pointer `0x40` dereferenced when the non-path target is absent/unresolved | CONFIRMED |
| 2 | save `0x007d0b70`, load `0x007d0ca0` | slot bound `<=` count (sibling `0x007d07b0` uses `<`) → record one past the end | comparisons CONFIRMED; bug HIGH CONFIDENCE |
| 3 | load deferred callback `0x007d0c50` | no bound check, re-reads the pending slot (may be −1) | HYPOTHESIS (reachability) |
| 4 | `store_common_rotate_mouse_drag` | uninitialised stack delta when the mouse flag is clear | CONFIRMED |
| 5 | `store_common_rotate_mouse_drag` target 3, `store_gallery_download_hide_list` true path | local player not null-checked | CONFIRMED unchecked; reachability OPEN |
| 6 | `store_gallery_download_hide_list` | global object pointer `0x022cd894` not null-checked (memset `0x246c`) | CONFIRMED unchecked |
| 7 | `store_gang_show_question_marks` / `0x00810440` | `null+0x8` read when the slot asset is empty and "npc_questionmark" is not found | CONFIRMED path |
| 8 | `store_gang_show_question_marks` | unchecked slot index `0x022cd9d0` into 4-entry tables | HYPOTHESIS |
| 9 | `rc_set_max_signal_range(0)` | divide-by-zero in reader `0x00a92430` (inf/NaN, likely no trap) | CONFIRMED division; effect HYPOTHESIS |
| 10 | `roadblock_create`/`destroy` | 64-bit handle through a Lua double | HYPOTHESIS |
| 11 | `set_cower_variant` | `(character+0xd24)` unchecked | HYPOTHESIS |

---

## G. Direct answer — all 25 names

| # | Name | Address | Registrar | Status | One line |
|---|---|---|---|---|---|
| 1 | `store_gang_show_question_marks` | `0x00811170` | `0x008111f0` | written up (E.1) | bare bool → `0x022cd9e5`; rebuilds the current slot's gang preview, question mark vs real model; null-asset crash path |
| 2 | `store_gallery_download_hide_list` | `0x0080f1a0` | `0x0080fab0` | written up (E.2) | true: reset gallery list object (`0x246c` bytes) + local-player byte `+0x20f8`; false: rebuild list; unchecked pointers |
| 3 | `store_dlc_queue_icon` | `0x007c5b60` | `0x007c5ba0` | written up (E.3) | no-op stub (reads number + string, returns nothing) |
| 4 | `store_dlc_is_offer_free` | `0x007c5b00` | `0x007c5ba0` | written up (E.4) | constant stub, always returns `false` |
| 5 | `store_common_rotate_mouse_drag` | `0x0080dc10` | `0x0080dd70` | written up (E.5) | rotates gang preview / store object / local player by mouse dx × −0.25; uninitialised-delta bug |
| 6 | `store_clothing_get_store_id` | `0x0080db90` | `0x0080dbc0` (direct) | written up (E.6) | returns `0x0080d810()` as a number |
| 7 | `store_character_lineup_loaded` | `0x0080d4b0` | `0x0080d4d0` (direct) | written up (E.7) | sets latch byte `0x022ccc52` = 1 |
| 8 | `spotlight_heli_laser_size` | `0x00a5dec0` | `0x00a20840` | written up (B.3) | float → `0x0130da2c` (default 1.0), beam-width scale in `0x00aad570` |
| 9 | `spawning_allow_gang_ambient_spawns` | `0x00a5dd00` | `0x00a20840` | written up (C.1) | nil-gated bool (default true) → all four bytes `0x01308acc..acf` |
| 10 | `spawn_region_enable_los` | `0x00a5fc70` | `0x00a20840` | written up (C.2) | spawn region by name (bit `0x8`@`+0x8`); bit `0x10`@`+0x94` = NOT enable; bare bool |
| 11 | `skydive_move_to_do` | `0x00a5f780` | `0x00a20840` | written up (A.1) | state `0x1f` move-to toward a path point or object position; **pointer `0x40` crash** |
| 12 | `set_unrecruitable_flag` | `0x00a5da40` | `0x00a20840` | written up (A.5) | bit `0x10`@`+0x1e02`, double-gate "npc"/"npc_force_flagsunrecruitable", default true |
| 13 | `set_unjackable_by_ai_flag` | `0x00a5d920` | `0x00a20840` | written up (B.1) | vehicle-AI `+0x5` bit `0x02`, double-gate "vehicle_ai"/"vai_force_flagsunjackable_by_ai", default true |
| 14 | `set_stag_notoriety_area_active` | `0x00a5d670` | `0x00a20840` | written up (C.4) | synced byte "stag_notoriety" + LOCKDOWN_NOTORIETY object `+0x84` + host-gated spawn/despawn |
| 15 | `set_saints_hated` | `0x00a5d5a0` | `0x00a20840` | written up (C.3) | synced byte "saints_hated" `0x014ff6ba`, bare bool |
| 16 | `set_cower_variant` | `0x00a5e200` | `0x00a20840` | written up (A.4) | clamp 0..4, state 13+n validated in anim set, double-gate "human"/"cower_stand_variant" → `+0x1cd8` |
| 17 | `set_cops_shooting_from_vehicles` | `0x00a5cf10` | `0x00a20840` | written up (C.5) | synced byte "passenger_cops" `0x013ba5d0`, nil-gated default true (engine default false) |
| 18 | `save_system_save_game` | `0x007d0dd0` | `0x007d0ec0` | written up (D.1) | slot 99 = new save with space checks; else confirm dialog → overwrite; **off-by-one bound** |
| 19 | `save_system_load_game` | `0x007d0e60` | `0x007d0ec0` | written up (D.2) | load slot (direct or deferred `0x007d0c50`); **off-by-one bound** |
| 20 | `save_system_cancel_coop_load` | `0x007d0e90` | `0x007d0ec0` | written up (D.3) | save-system state ← 5 |
| 21 | `roadblock_destroy` | `0x00a5c350` | `0x00a20840` | written up (B.5) | 64-bit id → roadblock (bit `0x10`@`+0xb`) → generic release `0x00853ea0` |
| 22 | `roadblock_create` | `0x00a5cbc0` | `0x00a20840` | written up (B.4) | template by name (last match) placed on a road spot within 7.0 of an object; returns handle or −1 |
| 23 | `rc_set_max_signal_range` | `0x00a5b7d0` | `0x00a20840` | written up (B.2) | swaps float `0x0278cad4`, **returns previous**; reader `1 − dist/range` |
| 24 | `rappel_exit` | `0x00a5c690` | `0x00a20840` | written up (A.3) | leave state `0x12`; queued (`0x24`) just cancels; live lands/teleports; default flag true |
| 25 | `rappel_enter` | `0x00a5c4a0` | `0x00a20840` | written up (A.2) | queued (slot table) or immediate rappel with "ga_cable"; net op `0x41`/`0x1f` sub-op 0 |

---

## H. OPEN items

1. Which code calls the seven screen-local registrars (`0x007c5ba0`, `0x007d0ec0`, `0x0080d4d0`, `0x0080dbc0`,
   `0x0080dd70`, `0x0080fab0`, `0x008111f0`), and into which Lua state. They register into the globals table, but the
   call sites were not dumped.
2. `skydive_move_to_do` args 5/6: what bits `0x08` / `0x04` of `+0x4a5` mean. The meaning of state `0xe` (`+0xcc8`) and
   action state `0x1f`.
3. `rappel_enter`: the meaning of the integer argument (`< 8`, passed to `0x007f4650` / `0x00600720`) and of the global
   float `0x0262d160` that must lie in [0, 90]. `rappel_exit`: the exact semantics of `0x009532a0` (assumed ground
   probe) and `0x009a6440` (assumed cable detach).
4. Startup default of the RC range `0x0278cad4` (zero-fill; static reference at `0x01009a20`).
5. Id-allocator range for roadblock handles (does the high dword ever exceed 2^21 so the double round-trip loses bits?).
6. `0x00706ab0`'s mode value 2 (`0x01503b50[0x012f4a80]`) in load-game; what `0x00b95210`, `0x00b94750`, `0x00b94a10`
   do with a record; whether the deferred `0x007d0c50` can observe pending = −1.
7. Whether `store_gallery_download_hide_list` / `store_common_rotate_mouse_drag` can run with a null `0x022cd894` or no
   local player.
8. `0x0080d810` (the value `store_clothing_get_store_id` returns); the readers of `0x022ccc52`.
9. Whether the session-synced-variable mechanism `0x0086d770` overwrites a non-authoritative peer's direct Lua write
   (affects C.3–C.5 in co-op).
