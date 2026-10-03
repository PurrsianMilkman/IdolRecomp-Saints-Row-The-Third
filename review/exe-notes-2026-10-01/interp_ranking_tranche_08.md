# Ranking tranche 08 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-08.json` (names 126-150 of the 554
unspecced names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t08`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved in that one call.** Every name string occurs exactly once in `.rdata`
and its function pointer is the code pointer stored in the slot right after the name (insn offset +1) — CONFIRMED —
disassembly (dump `index.txt`). Two follow-up runs on the same private copy: a depth-0 `func` run on 17 callee
addresses (cited as "follow-up dump") and an `xref` run on `0x00830230` and `0x0130ef98` (cited as "xref run").

**Two registrars, not one.** The 10 non-`pcu_` names are stored by the gameplay registrar `0x00a20840`. The 15 `pcu_`
names are stored by **`0x00830230`**, which is neither the gameplay registrar nor the UI registrar itself. Its only
caller is the UI registrar `0x008430f0` (call at `0x00843132`), so it is a **UI sub-registrar for the clothing store
("PCU")** — CONFIRMED — disassembly + xref run. Team B's reconciliation table tags all 15 as `ui` / family `pcu`, which
agrees.

Call counts are Team B's (`for-team-b/team-b/tools/lua_reconciliation_called_and_registered_1181.tsv`, total call sites
/ distinct scripts): every name here is 2 calls; the radio names and `players_in_weird_camera_mode` come from 1 script,
the rest from 2.

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section H).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
`spec-lua-api-behaviour.md` §4.1: `0x00dfe210` `lua_tolstring`, `0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type`,
`0x00dfe160` `lua_tonumber` (a non-number reads as 0), `0x00ea2596` truncating float-to-int, `0x00dfe590`
`lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`. Two more push primitives appear in this tranche and were dumped:
`0x00dfe380` pushes nil, and `0x00dfe420` pushes a C string (nil when the pointer is null) — CONFIRMED. The **standard
optional-boolean idiom** in this tranche means: the boolean is read only when more than one argument was passed and the
slot is not nil (`lua_type != 0`); otherwise it defaults to false. Resolvers: `0x00a281e0` dedicated vehicle resolver,
`0x005982e0` third-tier by-name resolver on singleton `0x02442750`, `0x00853b10` liveness guard (non-zero = dead);
`0x009da4e0` returns the local player (global `0x0262edfc`); `0x009df3d0` returns the co-op partner (first player-list
entry that is not the local player, null unless `0x024d4462` is set); `0x0087ba20` returns the session.

**Lua callback-call helpers** (used by three `pcu_` functions; dumped in this tranche): `0x00e0ca80(name, state)` sets up
a call to the Lua function with that name on the script state returned by `0x00e1a1b0` (global `0x02a45450`), and
returns null when it can't. `0x00e0ce40` pushes a number argument, `0x00e0ce60` pushes nil, and `0x00e0cfb0` pushes a
string (nil when the string is null). Each of the three increments the argument count at call-object `+0x1c`.
`0x00e0cd00` performs the call unless the call object's flag `+0x18` bit `0x10` is set — CONFIRMED (bodies), HIGH
CONFIDENCE (that this is a call-by-name builder; `0x00e0c720`/`0x00e0cba0` not dumped).

---

## A. Sprint overrides — 6 functions

Three independent integer overrides, each a static dword whose file-backed initial value is **-1** ("no override"):

| override | global | Lua setter | Lua clearer | engine getter |
|---|---|---|---|---|
| sprint ("use") | `0x012f2530` | `player_override_set_sprint` `0x00a5a220` → `0x006ceb30` | `player_override_clear_sprint` `0x00a597e0` → `0x006ceb50` | `0x006ceb40` |
| recharge | `0x012f2534` | `player_override_set_sprint_recharge` `0x00a5a280` → `0x006ceb60` | `player_override_clear_sprint_recharge` `0x00a59820` → `0x006ceb90` | `0x006ceb70` |
| delay | `0x012f2538` | `player_override_set_sprint_delay` `0x00a5a250` → `0x006ceba0` | `player_override_clear_sprint_delay` `0x00a59800` → `0x006cebc0` | `0x006cebb0` |

All CONFIRMED — disassembly (dumps + follow-up dump).

### A.1-A.3 `player_override_set_sprint` / `_set_sprint_recharge` / `_set_sprint_delay`

**Arguments:** 1 number (arg 1), read with no presence check. A missing or non-number argument reads as 0, so the
override becomes **0**, not "cleared". The value is truncated toward zero to a 32-bit integer by `0x00ea2596`.

**Return:** 0 values.

**Body:** a one-line store of the integer into the global in the table above, through a 1-instruction setter. There is
no player argument, no player lookup, and no range check, so negative values other than -1 are stored as-is. CONFIRMED —
disassembly.

### A.4-A.6 `player_override_clear_sprint` / `_clear_sprint_recharge` / `_clear_sprint_delay`

**Arguments:** none read (the `lua_gettop` call's result is discarded). **Return:** 0 values. **Body:** writes -1 into
the matching global. CONFIRMED — disassembly.

### A.7 What the engine does with the overrides

- **The overrides are global, not per player.** One value covers every player in a co-op session. CONFIRMED (no
  player index anywhere in the setters, clearers or getters).
- **They are mission state.** The only other writer of the three globals is `0x006d6010`, a mission-start/restart
  routine (it references "mission start", "mission restart" and "mission checkpoint"). It resets all three to -1 and
  then registers each one with `0x0086d770` under the names **"mission_sprint_use"**, **"mission_sprint_recharge"** and
  **"mission_sprint_delay"**, size 4. The same routine registers "mission_finishers", "mission_parachute" and
  "mission_streaming" the same way. CONFIRMED — follow-up dump. HYPOTHESIS: `0x0086d770` registers a variable in the
  mission's saved/replicated state block, so the overrides survive checkpoints and are reset on mission restart.
- **The getters are not symmetric.** `0x006ceb40` (sprint) and `0x006cebb0` (delay) return the stored value as-is.
  `0x006ceb70` (recharge) returns the override **only when `0x007044f0()` returns non-zero**. Otherwise it returns
  `0x006150a0()` instead, ignoring the override. CONFIRMED — follow-up dump. `0x007044f0` is the same mode query that
  gates sing-alongs in B.1; its value meanings are OPEN.
- Who calls the getters, and what unit the integers are in (milliseconds? a percentage?), was not dumped — OPEN.

---

## B. Radio — 2 functions

### B.1 `radio_set_sing_along_allowed_during_missions` (`0x00a5b610`)

**Arguments:** 1 boolean (arg 1), bare `lua_toboolean` with no nil gate; an absent argument reads as false.
**Return:** 0 values.

**Body:** calls `0x0055e370`, which stores the boolean as a byte at **`0x013c60e7`**. CONFIRMED — disassembly.

**Consumers (follow-up dump):**
- The only reader is `0x0055f8c0`, the sing-along selector. It returns no candidate when `0x007044f0()` returns **3**
  unless at least one of these holds: the 64-bit value at `0x013bbc10`/`0x013bbc14` is non-zero, or the flag at
  `0x013c60e7` is set. CONFIRMED. HYPOTHESIS: mode 3 means "a mission is running", which matches the function's name.
  The pair at `0x013bbc10` looks like a forced/scripted sing-along target (OPEN).
- The radio reset `0x005601f0` clears the flag to 0 and registers it with `0x0086d770` as **"mission_sing_alongs"**
  (size 1). That is the same mission-state mechanism as A.7. The fourth argument of that registration is 1 only when a
  session exists and its `+0x5c` equals `+0x58` (the host test used throughout §7), otherwise 0. CONFIRMED —
  follow-up dump.

**Side effects:** one global byte. Nothing is replicated by this function itself.

### B.2 `radio_get_station` (`0x00a5b760`)

**Arguments:** 1 string (arg 1), passed to the dedicated vehicle resolver `0x00a281e0`. A character name (including
`#PLAYER#`) resolves to that character's current vehicle through the resolver's `+0x16c0`/`+0x16c4` hop, as for every
other user of that resolver. CONFIRMED — dump (resolver body included).

**Return:** exactly 1 number on every path:
- vehicle not resolved → **-1** (the double at `0x012a3038`, -1.0);
- vehicle resolved but registered with no station → **-1**;
- otherwise the **1-based station index**.

**Body:** reads the 64-bit value at vehicle `+0x8`/`+0xc` and passes it to `0x0055f3e0`. That calls `0x0055ec10`, which
walks the station table (base `0x013c58cc`, count `0x013c58dc`, stride `0x690`). Each station holds a list of 24-byte
entries at `+0x5b0`, with a 16-bit count at `+0x670`, and the walk looks for an entry whose first two dwords equal the
vehicle's pair. On a match `0x0055f3e0` returns `(station − base) / 0x690 + 1`; on no match it returns 0xff. The Lua
binding sign-extends that **byte**, so "no match" arrives as -1. CONFIRMED — dump + follow-up dump. Each station is
wrapped in a `0x00d9f620`/`0x00d9f630` pair during the walk (HYPOTHESIS: a lock).
- HYPOTHESIS: the `+0x8` pair is the vehicle's own object handle, and a station's entry list is its set of listeners.
- Note for an implementer: station record 0 is special in the reset routine `0x005601f0` (it is cleared differently
  from the others), so the value **1** probably means the off/"no station" record rather than a real station
  (HYPOTHESIS). Indices above 127 would come out negative because of the byte sign-extension; whether the station count
  can reach that is OPEN (it can't in practice if the station list is the stock one).

---

## C. Projectile — 1 function

### C.1 `projectile_fire_from_navpoint` (`0x00a5abb0`)

**Arguments** (all read with no presence check):
1. string — source object name;
2. string — target object name;
3. string — weapon name;
4. number — spread in **degrees** (missing → 0). It is converted to radians by multiplying by the double at
   `0x012a30d8`, which the decompiler shows as 0.017453.

**Return:** 0 values.

**Body:** args 1 and 2 are each resolved with the third-tier by-name resolver `0x005982e0` on `0x02442750`, and each
result is rejected when the liveness guard `0x00853b10` reports it dead. If either fails, nothing happens. Otherwise it
calls `0x00923870(source+0x40, target+0x40, weaponName, spreadRadians)` — CONFIRMED — disassembly. HIGH CONFIDENCE:
object `+0x40` is the world position (it is used as an xyz triple below).

**`0x00923870` (dumped, depth 1):**
- No-op if the two positions are exactly equal component-wise, if the spread is **not below** the double at
  `0x012a3090` (the decompile shows 1.5707964, i.e. π/2 — so spreads of 90° or more do nothing), or if the weapon lookup
  fails. CONFIRMED.
- Weapon lookup `0x00b81220` (follow-up dump): **null-safe** (a null name returns null) and **case-insensitive**
  (`__stricmp`). It walks the weapon table at `0x028dc4dc` (count `0x028dc4e0`, stride `0x7ec`) and only accepts entries
  whose flag dword `+0x18` has bit `0x4`. So a nil or unknown weapon name is a silent no-op — CONFIRMED.
- Direction = target − source, normalised (`0x00da03c0`, HIGH CONFIDENCE normalise). When spread > 0 the direction is
  perturbed inside a cone. The function takes `0x00ea4980` of the spread (HYPOTHESIS: tangent), picks a uniform random
  angle in [0, 2π) from `0x00dab6a0` (its upper bound is the float `0x40c90fdb` = 2π), builds a perpendicular offset
  (`0x00da5930`/`0x00da6230`), adds it, and renormalises. CONFIRMED structure; HYPOTHESIS for the exact maths of the
  undumped helpers.
- Velocity = direction × weapon `+0x34c` (HIGH CONFIDENCE: muzzle speed).
- Spawn: `0x00922500` builds a creation record from (0, 0, weapon, source position, scratch, velocity, `0x029cdb98`, -1,
  0, 1.0, 0, 0), and `0x00456e30(6, record)` is called on `0x02442750`. CONFIRMED call shape. HYPOTHESIS: kind 6 =
  projectile, and the two leading zeros mean **no owner/shooter**, i.e. a projectile with no attacker.
- Audio: `0x0045da50(source position)` creates an emitter. Two switch-like calls follow: hash `0x50d893ff` set from
  weapon `+0xc8`, and `0x30ced6a4` = `0x3ade20a1`. Then two parameter calls: hash `0xfdf31b1a` = 0, and
  "Weapon_Upgrade_Level" = 1.0. Finally event `0xdcae324c` is posted and the emitter released (`0x0045e170`). CONFIRMED
  call sequence; HYPOTHESIS that this is the fire sound with the upgrade level pinned to 1.

**Crash shapes:** none found. Both objects are liveness-guarded, the weapon lookup is null-safe, and identical
positions are rejected before the subtraction.

---

## D. Camera — 1 function

### D.1 `players_in_weird_camera_mode` (`0x00a5a1b0`)

**Arguments:** none read. **Return:** exactly 1 boolean.

**Body:** true when **any** of these holds (CONFIRMED — disassembly):
1. the byte at `0x0130ef98` is non-zero;
2. `0x00a90690()` is true — that returns the byte at `0x0278cada`;
3. a co-op partner exists (`0x009df3d0`) **and** either the partner's dword `+0xcc8` equals **13**, or the partner's
   dword `+0x1cc4` equals **0x1a2** or **0x1a3**.

Findings:
- **The local player's own state is never inspected** — only the partner's fields and two globals. So in single player
  the answer comes from the two globals alone. CONFIRMED. HYPOTHESIS: the globals cover the local player's cases, and
  the partner test exists because the partner's camera mode isn't mirrored into them.
- `0x0130ef98` is **never written by any code**. The xref run, including a raw immediate/displacement scan, finds 9
  reads and no writes, and its file-backed value is 0. In the shipped binary it is effectively constant false. HIGH
  CONFIDENCE (a write through a data pointer table is the only way the scan could miss one).
- `0x0278cada` is set by `0x00a93070` and cleared by `0x00a93320` (follow-up dump). `0x00a93070` references the string
  **"RC Gun"**, so HYPOTHESIS: this flag means "remote-controlling a vehicle with the RC gun".
- The meanings of partner state 13 at `+0xcc8` and of values `0x1a2`/`0x1a3` at `+0x1cc4` are OPEN.

---

## E. PCU (clothing store) — 15 functions

### E.0 Data model these functions share

Everything below is CONFIRMED from the dumped bodies unless tagged otherwise. All addresses are statics.

- **Inventory is local-player-only.** Every accessor checks that the player passed in **is the local player**
  (`0x0262edfc`); for any other player it returns 0 or null (`0x009dad80`, `0x009dadf0`, `0x009dae20`, `0x009dae40`,
  `0x009dd440`, `0x009dad20`, `0x009dafd0`). A co-op partner has no inventory in this system.
- **Owned-item list:** count `0x0262edf8`, entries from `0x0262ee00`, stride `0x18`. Entry layout: `+0` item record,
  `+4` variant record, `+8` byte variant index (0xff = "not cached, search"), `+9` byte, `+0xc`/`+0x10`/`+0x14` three
  colour pointers (0 = item default). **Capacity 0x800 (2048)**, enforced by both appenders `0x009dad20` (single item)
  and `0x009dafd0` (outfit pieces). Both **silently refuse** when full.
- **Saved-outfit list:** count `0x0262e9f4`, entries from `0x0262e9f8`, stride 8 (outfit record, second dword).
  **Capacity 0x80 (128)**, enforced by `0x009dafd0`, which returns false and adds nothing when full.
- **Outfit record** (shared by saved outfits and store catalog nodes): `+0x10` piece array (stride `0x18`, piece `+0` =
  item record), `+0x14` piece count, `+0x78` byte content-pack id. Catalog nodes add `+8` price (integer), `+0x18` the
  value stored as the saved outfit's second dword, `+0x64` flag dword, `+0x70` next. The catalog is a **circular list
  headed by `0x022db9b8`**.
- **Content-pack gate** `0x0045b3a0(byte)`: 0xff (base game) always passes. Any other id passes only if the 0x108-byte
  record at `0x02cda341 + id·0x108` echoes the id and has flag bits 1 and 2 set at `+2`. HYPOTHESIS: DLC/pack ownership.
- **Item records:** table `0x022db9c8`, stride `0x7c`, count `0x022db988`. `0x00822040(index)` is a bounds-checked
  index lookup (null when out of range); `0x00822a00(id)` is a linear search on `+0xc`. Fields: `+8` name string,
  `+0xc` item id, `+0x14` string id, `+0x20` **slot id** (0..23), `+0x24` wear-option array (stride `0x28`), `+0x28`
  wear-option count, `+0x2c` variant array (stride `0x70`), `+0x30` **variant count**, `+0x34…` default colours, `+0x58`
  colour-slot count, `+0x5c`/`+0x60` (see E.12), `+0x78` content-pack id.
- **Variant records** (stride `0x70`): `+4` string id, `+0xc` name string, `+0x10` variant id, `+0x14` price (float),
  `+0x6c` byte "listed" (the filter in E.7), `+0x6d` byte set to 1 on purchase.
- **"String id or literal" pairs.** Item, variant and wear-option records carry a 32-bit string id next to a literal
  string. When the id equals the sentinel at **`0x029c9964`**, the Lua side receives **(nil, literal string)**;
  otherwise it receives **(id as an unsigned number, nil)**. Same rule everywhere in this tranche. HYPOTHESIS: the
  sentinel is the "no localisation id" value.
- **Category records:** pointer `0x022db978`, count `0x022db97c`, stride `0x44`. `0x00821fe0(index)` is a
  bounds-checked index lookup (null when out of range). Fields: `+0` name string, `+0x18` slot-id array, `+0x1c` slot
  count.
- **Store working set:** the local player's `+0x2088` points at the store object, whose `+0x14` is a store index `s`.
  Entries live at `0x022d0920 + s·0x720` with stride `0x4c` (so at most 24 by size), and the count is at
  `0x022d0918 + s·4`. Entry fields: `+0` item, `+0x10` variant, `+0x14` "player component information" record (its
  `+0x2c` is the slot id), `+0x18…` colours, `+0x3c` colour count, `+0x48` 16-bit **owned-instance index**. Lookups:
  `0x00821740(out, store, slot)` finds the entry for a slot, and `0x00821630(store, slot, flag)` removes every entry for
  a slot (compacting with `0x00821590`). Store `+0x1c` is a byte that tracks the backpack (E.10, E.13, E.15).
- **Worn/preview table:** `0x022ff5d0`, **24 entries × `0x4c`, indexed by slot id**; it ends at `0x022ffcf0`.
  `0x0082e210(slot or -1, filter)` refreshes it from the store working set. Per slot it copies the store entry
  (`0x008216a0`) or zeroes it, calls `0x00bd9830(item)` when the slot's item changed, and finally calls
  `0x0082dc40(0x022ffd40, 0x022ffd44, 1)`. HYPOTHESIS: the last two load the new item's assets and rebuild the
  character's appearance.
- **So the "id" most `pcu_` functions take is a slot id.** `0x00821740` matches it against the component record's
  `+0x2c`, and the same number indexes the 24-entry worn table directly.
- **Special slots:** slot **0x17 (23)** is the backpack. A piece in that slot sets player `+0x20f8` and store `+0x1c`,
  and the category named "Backpacks" drives the same flag. Slot **0x13 (19)** sets bit 0 of player `+0x1d97` and is
  processed first in `0x0082e2d0` and `0x00825cf0`. Slots 0–4, 21 and 22 fail the "clearable" predicate `0x0082a630`,
  and slots 0, 10 and 11 are additionally exempted from bulk clears. CONFIRMED values; their meanings are HYPOTHESIS
  (body/hair/face-type slots).
- **Local player is never null-checked** before `+0x2088` is read in any of the 15 roots or in `0x0082e210`. The only
  null checks are inside `0x00824f80` (E.12) and in E.5's wear-option tail. CONFIRMED. With no local player every one of
  these crashes.

### E.1 `pcu_is_bra_category` (`0x0082b7d0`) and E.2 `pcu_is_underwear_category` (`0x0082b740`)

**Arguments:** 1 number (category index), truncated to an integer.

**Return:**
- index ≤ -1 → **0 values**;
- valid index → 1 boolean: true when the category name equals **"Bra"** (E.1, literal at `0x0115f954`) or
  **"Underwear"** (E.2, `0x0115f948`), compared with an exact case-sensitive inline `strcmp`.

**Crash:** an index ≥ the category count makes `0x00821fe0` return null, and the root dereferences the result at once
(`0x0082b7fb` / `0x0082b76b`) — **null read, CONFIRMED — disassembly**.

### E.3 `pcu_get_num_items_owned` (`0x0082baa0`)

**Arguments:** 1 number (catalog-outfit index, or -1). **Return:** 1 number.

**Body:**
- With -1: returns the local player's owned-item count.
- Otherwise: walks the catalog list from `0x022db9b8`, numbering nodes with a 16-bit counter that starts at -1 and
  increments on every node whose flags have **bit 0 set and bit `0x10` clear**. For every node whose current number
  equals the argument it adds the node's **piece count (`+0x14`)** to the owned count, and returns the total.
  CONFIRMED — disassembly.

Meaning (HYPOTHESIS, strongly suggested by E.11): the number of owned items **after** buying that catalog outfit, i.e.
a capacity check against the 2048 limit.

Quirk: there is no first-match guard. Nodes that don't increment the counter inherit the previous node's number, so
uncounted nodes that follow the matching one are **also summed**. E.11 buys only the first matching node, so the
projection can over-count — CONFIRMED shape.

### E.4 `pcu_get_owned_item_info` (`0x0082ccd0`)

**Arguments:** 1 number = item **index** (table `0x022db9c8`); 2 number = owned-instance index, truncated to 16 bits.

**Return:**
- no owned entry found → **1 value, -1**;
- found → **33 values** in the common case. The count varies with the variant search below. CONFIRMED by tallying the
  push counter in the listing; the function returns that counter + 4.

**Body:** looks up the item record. It walks the owned list; for each entry whose item has the same item id (`+0xc`) it
compares a 16-bit instance counter against arg 2, then advances the counter. On the matching instance it pushes, in
order:
1. item `+0x20` (slot id);
2. arg 1 echoed back (as unsigned);
3. the item's (string id, literal) pair — 2 values;
4. item `+0x30` (variant count);
5. the variant index: the entry's cached byte if it isn't 0xff. Otherwise every index `i` whose variant `+0x10`
   matches the owned variant's `+0x10` (normally exactly one), or **255** when none matches;
6. the (string id, literal) pair of **variant 0 of the item — not the owned variant** — 2 values. CONFIRMED (the
   listing reads `+0x2c` and then that record's `+4`/`+0xc` with no index). HYPOTHESIS: a bug, or this reports the
   item's base name;
7. colour-slot count = min(k, 3, item `+0x58`), where k is the `+8` of the record that variant 0's `+0x1c` points at,
   or 0 when that pointer is null;
8. three colour groups of 4 values each. Within the colour count a group is (palette index, R, G, B); past it the group
   is (-1, nil, nil, nil). The palette index is (colour pointer − palette base) / `0x30`, where the slot's palette comes
   from `0x00746770(slot)` → `0x00746750`. R, G and B are the palette entry's floats `+0xc/+0x10/+0x14` converted
   **linear → sRGB** (>0.0031308: 1.055·x^(1/2.4) − 0.055, else 12.92·x), rounded to 8 bits, then divided by 255. An
   owned entry with no colour pointer uses the item's default colours (`+0x34…`). CONFIRMED;
9. the item's wear options that pass both availability checks `0x00822a60` and `0x00822a30`: their **count**, then
   the **first passing index** (or -1). This is computed only when a local player exists;
10. the first passing option's text as **(nil, string)**. Unlike the pairs above, the text is resolved host-side: a
    string id goes through `0x00849fd0` and is formatted into a 256-byte stack buffer by `0x00e22a30`. If no option
    passed, the string slot is nil;
11. the constants 0, -1, nil, nil, -1, nil, nil, nil (HYPOTHESIS: placeholder fields kept for a fixed return shape).

**Crash shapes:**
- arg 1 ≥ item count → `0x00822040` returns null, and the null record's `+0xc` is read at `0x0082cd73` as soon as any
  owned entry has an item — **null read, CONFIRMED**.
- The variant search in step 5 runs while index **≤** the variant count (`0x0082ced2`–`0x0082cedf`, `JLE`). E.7 and
  `0x008214c0` both treat `+0x30` as a count (`<`), so this reads **one `0x70` record past the end of the variant
  array** — out-of-bounds read, CONFIRMED. It can push one extra index if the stray dword happens to match (HYPOTHESIS).

### E.5 `pcu_get_wear_options` (`0x0082bdd0`)

**Arguments:** 1 number (slot id); 2 string (name of a Lua callback). **Return:** 0 values.

**Body:** finds the store entry for the slot. On failure, or if arg 2 is null, nothing is called. Then for each wear
option of the entry's item (array `+0x24`, stride `0x28`, count `+0x28`) that passes `0x00822a30` **and** `0x00822a60`,
it calls the Lua function named by arg 2 with (option index, string id or nil, nil or literal). If the callback can't
be set up, the loop stops. CONFIRMED. Both availability checks forward to `0x00821500`/`0x00821550` with a per-store
table `0x022d0910[s]` and the global `0x022db974` (bodies not dumped — OPEN).

### E.6 `pcu_report_outfits` (`0x0082ae90`)

**Arguments:** 1 string (callback name). **Return:** 0 values.

**Body:** for each saved outfit `i`, the callback is called with (i) when all of these hold: the entry and its record
are non-null, the record's content-pack byte `+0x78` passes `0x0045b3a0`, and **every piece has a non-null item**.
Outfits failing any test are skipped silently. CONFIRMED.

### E.7 `pcu_get_variants` (`0x0082bc90`)

**Arguments:** 1 number (slot id); 2 string (callback name). **Return:** 0 values.

**Body:** for each variant of the slot's store-entry item (index < `+0x30`) whose `+0x6c` byte is **1**, it calls the
callback with (variant index, string id or nil, nil or literal). If the callback can't be set up, the loop stops.
CONFIRMED.

### E.8 `pcu_is_item_needed_for_outfit` (`0x0082db40`)

**Arguments:** 1 number (slot id). **Return:** exactly 1 boolean.

**Body:** false if the slot has no store entry. Otherwise it is true when some saved outfit has a piece with the same
item id as the store entry's item, **and** `0x009dd440(player, item, store-entry variant)` returns **exactly 1**, i.e.
the player owns exactly one instance of that item/variant. CONFIRMED. HYPOTHESIS: "selling or discarding this would
break a saved outfit".

Crash shape: `0x009dd440` reads each owned entry's variant `+0x10` without checking the entry's variant pointer for
null, whenever a variant filter is passed (CONFIRMED shape; reachability HYPOTHESIS, since both appenders always fill
`+4`).

### E.9 `pcu_is_current_item_worn` (`0x0082bfa0`)

**Arguments:** 1 number (slot id); 2 number (owned-instance index, 16 bits).

**Return:** irregular:
- slot has no store entry → 1 boolean, **false**;
- worn-table entry for the slot is empty, or its item name differs from the store entry's item name (`strncmp`, limit
  0x200) → **0 values**;
- otherwise → 1 boolean: arg 2 equals the store entry's `+0x48` instance index.

CONFIRMED. Note that the instance compared is the store entry's, not the worn entry's.

Crash shape: the slot id indexes the 24-entry worn table with no bound check. It is only reached after the store lookup
proved some store entry has that slot, so this is data-gated (HYPOTHESIS low risk).

### E.10 `pcu_purchase_slot` (`0x0082ea20`)

**Arguments:**
1. number — slot id; negative → return at once;
2. optional boolean (standard idiom) — **true = skip the charge-and-add block** (HYPOTHESIS: "already owned / free");
3. optional number (standard nil-gated idiom, 16 bits) — owned-instance index to store.

**Return:** 0 values.

**Body** (CONFIRMED unless tagged):
1. Look up the store entry for the slot; stop if there is none.
2. Call `0x006b2050(0xf)`. The other branch (0x11) is dead code: its condition is `0x006948c0`, a stub that always
   returns 0. HYPOTHESIS: a UI sound/event id.
3. Re-find the item by id (`0x00822a00`). Then write the store entry's `+0x48` instance index: arg 3 when present,
   otherwise the number of instances already owned (`0x009dd440(player, item, 0)`). This means a new purchase gets the
   next instance number.
4. Unless arg 2 is true:
   - find the variant whose id matches the store entry's variant (`0x008214c0`);
   - price = `0x0080e740(variant +0x14)`, which applies the discount globals `0x022cd0ec`/`0x022cd0f0` and rounds with
     `0x00ea4e80`;
   - adjust the player's cash by −price·100 (cents, clamped ±2·10⁹ by `0x00960160`) through `0x0094d920(…, 0xb)`. That
     function builds the "cash_adjust" network message (HIGH CONFIDENCE cash change, reason 0xb);
   - `0x006cef30(price)`, then `0x00710cb0(0x46, &price)` (HYPOTHESIS: stat 0x46 = money spent on clothing);
   - build a 24-byte owned entry (item, variant, variant index or 0xff, 0, up to 3 colours copied from the store entry)
     and append it with `0x009dad20`;
   - set variant `+0x6d` = 1; if item `+0x60` is 0, clear item `+0x5c`;
   - "clothing" notification: if there is no session, or the session is not the host (`+0x5c` ≠ `+0x58`), it uses
     `0x00a295a0(2, 0x026e9568, 0x026e956c, 4)` provided `0x026e9538` ≠ -1; the host uses `0x00a1f990(0x026e9538, 0)`.
     Either way, on a non-null result it pushes the strings "clothing" and `0x00a353d0(player)` (HYPOTHESIS: an
     interrupted script thread resumed with these values; no execute call is visible — OPEN);
   - `0x00a03d40(player, price, item name, 0)` (HYPOTHESIS: purchase bookkeeping).
5. If the current category record `0x022ffd4c` is non-null: take its `+0x34` or `+0x3c` depending on player byte
   `+0xa41` (0 or 1; HYPOTHESIS: gender) and, when it isn't -1, pass it to `0x0095b930` (HYPOTHESIS: a reaction
   animation or voice line).
6. `0x009dd960` (HYPOTHESIS: inventory UI refresh), then refresh all worn slots.
7. If the current category name is "Backpacks", set player `+0x20f8` = 1.

**Crash/logic shapes:**
- **`0x022ffd4c` is null-checked at `0x0082ed01`, then dereferenced unconditionally at `0x0082ed68`/`0x0082ed6e`** for
  the "Backpacks" compare. With no current category this is a **null read** — CONFIRMED — disassembly.
- When the store entry's variant id isn't in the item's variant array, `0x008214c0` returns 0 and its `+0x14` is read
  at `0x0082eb65` — **null read**, CONFIRMED shape (reachability HYPOTHESIS). The store entry's variant pointer is also
  dereferenced unchecked at `0x0082eb56`.
- If `0x00822a00` doesn't find the item it returns null, which is then dereferenced (`+0x30`, `+0x2c`, and inside
  `0x009dd440`) — CONFIRMED shape, reachability HYPOTHESIS.
- **The colour copy is bounded only by the store entry's `+0x3c`** and writes into a 3-dword stack array
  (`ESP+0x34…0x3c`, `0x0082ec40`–`0x0082ec50`). A value above 3 would overwrite the saved registers next to it. Latent
  only: `0x00825cf0` clamps `+0x3c` to ≤ 3 when it builds entries (CONFIRMED clamp), and other entry builders weren't
  checked (OPEN).
- **The result of `0x009dad20` is ignored**: with 2048 items already owned, the player is charged and gets nothing —
  CONFIRMED (`0x0082ec58`–`0x0082ec60`, no test).
- The variant-index search uses an 8-bit counter, so a variant count over 255 would never end; data-gated, HYPOTHESIS
  irrelevant.

### E.11 `pcu_purchase_outfit` (`0x0082e820`)

**Arguments:** 1 number (catalog-outfit index; negative → nothing); 2 optional boolean (standard idiom; true = free).
**Return:** 0 values.

**Body:** walks the catalog list with the same numbering as E.3 (bit 0 set, bit `0x10` clear) and acts on the **first**
matching node only. For that node:
1. `0x009dafd0(player, node, node+0x18, 1, 0)` appends a saved outfit and, because of the 1, appends every piece as an
   owned item. Each new owned entry gets: item, the item's first variant (`+0x2c`), variant index 0, and the piece's
   three colours from `+0xc…`.
2. If any piece's item is in slot 0x17, set player `+0x20f8` = 1.
3. Refresh all worn slots.
4. Unless free: price = `0x0080ede0(node +8)` (the same discount formula as E.10, on an integer), then the same cash
   adjust, trackers, stat 0x46 and "clothing" notification as E.10. There is **no** `0x00a03d40`, `0x009dd960`, slot
   reaction or `0x006b2050` call here. CONFIRMED.

**Logic bugs:**
- `0x009dafd0`'s result is ignored. With 128 outfits saved it adds **nothing** (no outfit, no pieces), yet the player
  is **still charged**. CONFIRMED (decompile; the listing shows no test).
- Pieces past the 2048-item cap are dropped one by one, without notice, while the outfit itself is saved. CONFIRMED.

### E.12 `pcu_remove_clothing` (`0x0082ef10`)

**Arguments:** none. **Return:** 0 values.

**Body:**
1. `0x00824f80(player)` (null-checked inside) removes every store entry whose slot passes `0x0082a630`, i.e. every slot
   except 0–4, 21 and 22. It then calls `0x00820df0()` and `0x00824ca0(store, 0)` (HYPOTHESIS: rebuild).
2. The root then sets store `+0x1c` = 0 and player `+0x20f8` = 0, and refreshes all worn slots.

CONFIRMED. Unlike E.15, slots 10 and 11 are **not** exempt here.

Crash: the root reads `player+0x2088` itself after `0x00824f80` returns, so a null local player crashes even though
the helper tolerated it (CONFIRMED — listing `0x0082ef28`).

### E.13 `pcu_set_slots_empty` (`0x0082f2e0`)

**Arguments:** 1 number (category index); 2 optional boolean (standard idiom; true = refresh worn slots afterwards).
**Return:** 0 values.

**Body:** an out-of-range category means nothing happens (the null is checked). Otherwise:
1. Zero the slot bitset (a heap buffer: pointer stored at `0x022d6150`, size at `0x022d6154`) and set the bit of
   every slot in the category.
2. `0x0082b470(bitset)` — conflict resolution around the "current slot" `0x01300c6c` and the worn table (not analysed
   further — OPEN).
3. For each slot in the category: remove its store entries (`0x00821630(store, slot, 0)`). If the slot is 0x13, set
   bit 0 of player `+0x1d97`.
4. If arg 2 is true, refresh all worn slots.
5. If the category name is "Backpacks", set store `+0x1c` = 0.

CONFIRMED. The bitset writes index by raw slot id with no bound; slot ids come from data (HYPOTHESIS low risk).

### E.14 `pcu_wear_outfit` (`0x0082f520`)

**Arguments:** 1 number (saved-outfit index); 2 optional boolean (standard idiom; refresh). **Return:** 0 values.

**Body:**
- Set bit 0 of player `+0x1d97` **unconditionally**, even when the index is bad.
- If the index is below the outfit count and `0x009dae40` returns an entry (negative indices give null):
  1. zero the bitset and mark the outfit's slots (`0x0082b370`: each piece's slot, plus the slot range
     `[+0x30, +0x34]` of any piece whose component record passes `0x009f0b30`; HYPOTHESIS: multi-slot garments);
  2. `0x0082b310` marks every clearable slot (not 0, 10 or 11) that the outfit **doesn't** cover in a second bitset
     (pointer at `0x022d6140`, size at `0x022d6144`). HIGH CONFIDENCE that it tests coverage against the first bitset:
     the decompile shows that pointer arriving in a register, not as an argument;
  3. `0x009f0ce0` clears the byte `0x0263f9dd`; its argument is ignored;
  4. apply with `0x00825cf0(store, outfit, second dword, 0x0082b2b0)`;
  5. refresh if asked.

CONFIRMED.

**`0x00825cf0` (follow-up dump, 785 instructions)** applies an outfit to the store working set:
- It **mutates the outfit record in place**: the first slot-0x13 piece is swapped to position 0, together with the
  parallel array at the second dword's `+4` (CONFIRMED).
- It sets store `+0x1c` when a piece is in slot 0x17.
- It runs up to 4 passes over the pieces. A piece is skipped when any of these is true:
  - its requirement list (`+0x18`/`+0x1c` of the piece's `+8` record) has an id whose per-store unlock byte is clear;
  - the store belongs to the local player and the item's content pack fails `0x0045b3a0`;
  - no "player component information" record is found. In that case it formats "Could not find player component
    information for mesh '%s' and variant '%s'" into the global buffer `0x029f5200` and moves on.
- Otherwise it replaces or evicts conflicting store entries (`0x009f0b50` overlap test, `memmove` compaction) and writes
  a fresh `0x4c` entry. That entry's colour count is clamped to ≤ 3, and the piece's own colours are applied through
  `0x00824950`.
- The completion callback (here `0x0082b2b0`, which removes the store entries of every slot flagged in `0x022d6140`
  and then clears that bitset) is called **only if no piece was skipped** (CONFIRMED). So if one piece is locked or
  missing, the slots the outfit doesn't cover keep their old clothing.

### E.15 `pcu_wear_store_outfit` (`0x0082f600`)

**Arguments:** 1 number (catalog-outfit index); 2 optional boolean (standard idiom; refresh). **Return:** 0 values.

**Body:** sets bit 0 of player `+0x1d97`, then walks the catalog and acts on the first matching node only. For it:
1. remove the store entries of every clearable slot except 0, 10 and 11;
2. mark slots exactly as E.14 does;
3. if the outfit has a slot-0x17 piece, set store `+0x1c` = 1; otherwise rebuild worn slot 0x17 alone
   (`0x0082e2d0(0x17)`) and copy player `+0x20f8` into store `+0x1c`;
4. `0x00825cf0(store, node, node+0x18, 0x0082b2b0)`;
5. `0x00826ff0(store)` = `0x00820df0()` + `0x00824ca0(store, 0)`;
6. refresh if asked.

CONFIRMED.

**Index inconsistency:** this function numbers catalog nodes on **any non-zero flag dword** with bit `0x10` clear
(`0x0082f683` tests the whole dword). E.3 and E.11 count **bit 0** only (`0x0082bb03`, `0x0082e8a8`). If a node has a
flag set without bit 0, the same Lua index names **different outfits** in "wear" and "buy" — CONFIRMED (listings). There
is also no negative-index guard: -1 matches the head node when the head isn't counted (CONFIRMED shape).

---

## F. Cross-function observations

1. **Mission-state registration is a shared mechanism.** The sprint overrides (A) and the sing-along flag (B.1) are
   both reset and registered through `0x0086d770` under "mission_*" names, alongside "mission_finishers",
   "mission_parachute" and "mission_streaming". The spec should treat these as mission-scoped variables that are reset
   at mission start. How they are persisted or replicated is OPEN.
2. **`0x007044f0` gates both families:** mode 3 blocks sing-alongs unless they're allowed (B.1), and mode 0 disables
   the sprint-recharge override (A.7). It is one host-side mode query; its values are OPEN.
3. **Pure-global setters.** A.1-A.6 and B.1 store into a single global with no player argument and no validation.
   Omitting the number in A.1-A.3 sets the override to 0 rather than clearing it.
4. **The PCU family is local-player-only** (E.0). In co-op, a partner's clothing purchases can't go through this
   inventory. Every root reads `player+0x2088` without a null check.
5. **The PCU "id" is a slot id** (0..23) for E.5, E.7, E.8, E.9 and E.10. It is a **category index** for E.1, E.2 and
   E.13, an **item index** for E.4 arg 1, a **catalog-outfit index** for E.3, E.11 and E.15, and a **saved-outfit
   index** for E.14. A binding layer must not mix these up.
6. **Three callback-iterating functions** (E.5, E.6, E.7) return nothing; they call a Lua function named by a string
   argument once per qualifying element. The (string id, literal) pair convention is uniform across E.4, E.5 and E.7.
7. **Purchases never check that the inventory accepted the item.** E.10 and E.11 both charge first or regardless (see
   G). E.11 also omits several follow-ups that E.10 performs.
8. **Return-shape irregularities a script can observe:** E.1/E.2 return no value for a negative index; E.9 returns no
   value on its name-mismatch path; E.4 returns 1 value or about 33.
9. **`radio_get_station` and the station resolver use different "nothing" values:** both "no vehicle" and "not
   tuned" return -1, while the lowest real answer is 1 (station record 0). See B.2.

## G. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | E.1 `0x0082b7fb`, E.2 `0x0082b76b` | category index ≥ count → null record dereferenced | CONFIRMED |
| 2 | E.4 `0x0082cd73` | item index ≥ count → null record `+0xc` read (once any item is owned) | CONFIRMED |
| 3 | E.4 `0x0082ced2`–`0x0082cedf` | variant scan uses `<=` count → reads one `0x70` record past the array | CONFIRMED |
| 4 | E.10 `0x0082ed01` vs `0x0082ed68` | `0x022ffd4c` null-checked, then dereferenced unconditionally | CONFIRMED |
| 5 | E.10 `0x0082eb56`/`0x0082eb65` | store-entry variant pointer and `0x008214c0` result dereferenced unchecked | CONFIRMED shape, reachability HYPOTHESIS |
| 6 | E.10 `0x0082ec40`–`0x0082ec50` | colour copy into a 3-dword stack array bounded only by data `+0x3c` | CONFIRMED unclamped; latent (clamped by `0x00825cf0`) |
| 7 | E.10 (via `0x00822a00`) | item-by-id lookup result dereferenced unchecked | CONFIRMED shape, reachability HYPOTHESIS |
| 8 | all 15 PCU roots | local player not null-checked before `+0x2088` | CONFIRMED shape |
| 9 | `0x009dd440` (E.8, E.10) | owned entry's variant pointer dereferenced without a null check | CONFIRMED shape, reachability HYPOTHESIS |
| 10 | E.9 | slot id indexes the 24-entry worn table unbounded (data-gated) | HYPOTHESIS low risk |
| 11 | E.10 | `0x009dad20` refusal (2048 owned) ignored → charged, nothing added | CONFIRMED |
| 12 | E.11 | `0x009dafd0` refusal (128 outfits) ignored → charged, nothing added; pieces past 2048 dropped | CONFIRMED |
| 13 | E.15 vs E.3/E.11 | catalog index counts different nodes (any flag vs bit 0) | CONFIRMED |
| 14 | E.3 vs E.11 | projection sums trailing uncounted nodes; purchase takes the first only | CONFIRMED shape |
| 15 | E.4 step 6 | reports variant 0's name, not the owned variant's | CONFIRMED (intent HYPOTHESIS) |
| 16 | `0x00825cf0` | reorders the source outfit's pieces in place | CONFIRMED |

## H. OPEN

- Values of the mode query `0x007044f0` (3 in B.1, 0 in A.7) and its meaning.
- Who reads the sprint getters `0x006ceb40`/`0x006ceb70`/`0x006cebb0`, and the unit of the stored integers; what
  `0x006150a0` returns in place of the recharge override.
- `0x0086d770` (the "mission_*" registration): persistence and replication semantics.
- B.1: what the pair at `0x013bbc10`/`0x013bbc14` represents.
- B.2: whether station record 0 is "radio off", and whether the vehicle `+0x8` pair is its handle.
- D.1: meaning of partner `+0xcc8` == 13 and `+0x1cc4` ∈ {`0x1a2`, `0x1a3`}; whether `0x0130ef98` has a data-table
  writer the scan could miss; confirmation that `0x0278cada` is the RC-gun remote-control mode.
- C.1: semantics of `0x00922500`/`0x00456e30` kind 6 (projectile with no owner?), the zero-vector argument
  `0x029cdb98`, and the audio hashes.
- Lua callback helpers: whether a nil callback name (`0x00e0ca80` with a null string) is handled (`0x00e0c720` not
  dumped).
- E.10/E.11 "clothing" notification at `0x026e9538` (`0x00a1f990`/`0x00a295a0`): what receives the two pushed strings
  and when it runs.
- `0x0094d920` reason 0xb, stat 0x46 (`0x00710cb0`), `0x006cef30` trackers, `0x00a03d40`, `0x0095b930`.
- Slot semantics: names of slots 0–4, 10, 11, 19, 21, 22 (23 = backpack is HIGH CONFIDENCE); player `+0x1d97` bit 0;
  player `+0xa41`.
- `0x0082b470` (conflict resolution around `0x01300c6c`), `0x00821500`/`0x00821550` (availability checks),
  `0x00bd9830` and `0x0082dc40` (worn-slot change hooks), `0x00824ca0`/`0x00820df0` (store rebuild).
- E.10 #6: whether any store-entry builder other than `0x00825cf0` can produce a colour count above 3.

## I. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `radio_set_sing_along_allowed_during_missions` | gameplay `0x00a20840` | `0x00a5b610` | resolved — sets byte `0x013c60e7`; mission-scoped |
| 2 | `radio_get_station` | gameplay | `0x00a5b760` | resolved — 1-based station index or -1 |
| 3 | `projectile_fire_from_navpoint` | gameplay | `0x00a5abb0` | resolved — worker `0x00923870`; spawn/audio internals OPEN |
| 4 | `players_in_weird_camera_mode` | gameplay | `0x00a5a1b0` | resolved — partner state + 2 globals; state meanings OPEN |
| 5 | `player_override_set_sprint_recharge` | gameplay | `0x00a5a280` | resolved — `0x012f2534` (conditionally honoured) |
| 6 | `player_override_set_sprint_delay` | gameplay | `0x00a5a250` | resolved — `0x012f2538` |
| 7 | `player_override_set_sprint` | gameplay | `0x00a5a220` | resolved — `0x012f2530` |
| 8 | `player_override_clear_sprint_recharge` | gameplay | `0x00a59820` | resolved — writes -1 |
| 9 | `player_override_clear_sprint_delay` | gameplay | `0x00a59800` | resolved — writes -1 |
| 10 | `player_override_clear_sprint` | gameplay | `0x00a597e0` | resolved — writes -1 |
| 11 | `pcu_wear_store_outfit` | PCU `0x00830230` (under UI `0x008430f0`) | `0x0082f600` | resolved — index inconsistency (G13) |
| 12 | `pcu_wear_outfit` | PCU | `0x0082f520` | resolved — applies via `0x00825cf0` |
| 13 | `pcu_set_slots_empty` | PCU | `0x0082f2e0` | resolved — `0x0082b470` OPEN |
| 14 | `pcu_report_outfits` | PCU | `0x0082ae90` | resolved — callback per usable saved outfit |
| 15 | `pcu_remove_clothing` | PCU | `0x0082ef10` | resolved |
| 16 | `pcu_purchase_slot` | PCU | `0x0082ea20` | resolved — **null deref G4**, G5-G7, G11 |
| 17 | `pcu_purchase_outfit` | PCU | `0x0082e820` | resolved — charge-without-add G12 |
| 18 | `pcu_is_underwear_category` | PCU | `0x0082b740` | resolved — **null deref G1** |
| 19 | `pcu_is_item_needed_for_outfit` | PCU | `0x0082db40` | resolved |
| 20 | `pcu_is_current_item_worn` | PCU | `0x0082bfa0` | resolved — irregular return shape |
| 21 | `pcu_is_bra_category` | PCU | `0x0082b7d0` | resolved — **null deref G1** |
| 22 | `pcu_get_wear_options` | PCU | `0x0082bdd0` | resolved — callback iterator |
| 23 | `pcu_get_variants` | PCU | `0x0082bc90` | resolved — callback iterator |
| 24 | `pcu_get_owned_item_info` | PCU | `0x0082ccd0` | resolved — 1 or ~33 values; **null deref G2**, OOB read G3 |
| 25 | `pcu_get_num_items_owned` | PCU | `0x0082baa0` | resolved — projected owned count |
