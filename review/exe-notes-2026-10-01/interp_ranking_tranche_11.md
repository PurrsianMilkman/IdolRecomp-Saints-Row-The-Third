# Ranking tranche 11 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-11.json` (names 201-225 of the 554
unspecced names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t11`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved in that one call.** Every name string occurs exactly once in `.rdata`
and its handler is the code pointer stored in the slot right after the name (insn offset +1) — CONFIRMED — disassembly
(dump `index.txt`). Follow-up runs on the same private copy, cited by name below:

- "follow-up dump": depth-0 `func` run on 24 callee addresses (`0x007ed7d0 0x008410c0 0x00872830 0x00872760 0x007ade80
  0x005f4a80 0x007e18a0 0x008adff0 0x00b81a50 0x00ad4160 0x00a79470 0x008ccb90 0x0075af60 0x0075ae80 0x00a77e10
  0x00a77e20 0x008addb0 0x007f9b70 0x006e6610 0x007070a0 0x00b81870 0x005feaa0 0x00a1fda0 0x0097e280`);
- "second follow-up dump": depth-0 `func` run on `0x00ab1a20 0x00853b30 0x005f8710`;
- "range run": `range` mode on `0x00a21e40-0x00a21e70`, `0x007ca0f8-0x007ca130`, `0x007f98d4-0x007f98e8`;
- "xref run": `xref` mode on `0x007c9fc0 0x007e18e0 0x005fc500 0x00845aa0`, then on `0x008489e0`;
- "ptrs run": `ptrs` mode, 70 dwords at `0x01122690`.

Call counts are Team B's (`for-team-b/team-b/tools/lua_reconciliation_called_and_registered_1181.tsv`): **every name
in this tranche is 2 call sites**; distinct scripts are 2 for `mesh_mover_set_damaged_by_player_only`,
`mesh_mover_ready`, `main_menu_new_game`, `main_menu_horde_start`, `inv_item_get_weapon_upgrades`, `inv_item_add_ammo`,
`human_has_taken_non_melee_damage_from_player`, `helicopter_set_dont_use_constraints`, `get_vehicle_speed`,
`get_current_difficulty`, `get_closest_npc_to_object`, `game_steam_open_overlay`, `game_start_find_syslink_servers`,
and 1 for the other 12.

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section J).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in
`spec-lua-api-behaviour.md` §4.1: `0x00dfe210` `lua_tolstring` (a non-string, non-number slot reads as a null pointer),
`0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil), `0x00dfe160` `lua_tonumber` (a non-number reads as 0),
`0x00ea2596` truncating float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe420`
push C string. Resolvers: `0x00a281a0` generic character chain, `0x00a280c0` kind-filtered chain (tolerates a null
out-flag pointer — CONFIRMED, body dumped here), `0x00a281e0` dedicated vehicle resolver (also accepts a character name
and returns that character's vehicle through the 64-bit handle at character `+0x16c0`/`+0x16c4` — CONFIRMED, body
dumped here), `0x0062a190` per-kind vehicle resolver, `0x00853b10` liveness guard (non-zero = dead), `0x009da4e0` local
player (global `0x0262edfc`), `0x0087ba20` session.

**By-name resolver family on singleton `0x02442750`.** Six resolvers seen in this tranche share one shape: if the
singleton's count at `+0x265c` is positive, look the name up in the table at `+0x2660` (through `0x004588f0`), reject
an object whose byte `+0x33` has bit `0x10` set, then accept it only if one bit of its kind's flag record is set. The
kind's flag record is `[0x02cc9900 + 4 * (object byte +0x34)]`. CONFIRMED — disassembly, for each:

| resolver | kind-flag test | used here by |
|---|---|---|
| `0x00734e90` | byte `+6` bit `0x02` | `get_closest_npc_to_object`, inside `0x00a29200`, `0x00a455a0` |
| `0x006434a0` | byte `+0xb` bit `0x01` | `mesh_mover_set_damaged_by_player_only`, `mesh_mover_apply_impulse` |
| `0x005982e0` | byte `+0xb` bit `0x02` | `mesh_mover_apply_impulse` (target), inside `0x00a29200` |
| `0x005e4dd0` | byte `+0xb` bit `0x04` | `get_vehicle_speed` (third tier) |
| `0x005eab60` | byte `+0xa` bit `0x20` | `group_show_do`, `group_hide_do`, `group_is_loaded_internal` |
| `0x0062a190` | (per-kind vehicle resolver, established) | `get_vehicle_speed` |

Meanings of the bits are inferred from the callers' names: `+0xb` bit 1 = mesh mover, `+0xa` bit `0x20` = group —
HIGH CONFIDENCE; the rest HYPOTHESIS. Other kind-flag bits read in this tranche: `+6` bit `0x80` = vehicle
(handle-to-vehicle resolution in `0x00ad53f0` and the garage), `+0xa` bit `0x04` = human (candidate filter in
`get_closest_npc_to_object`), `+0xa` bit `0x02` = player (name lookup in `0x00a353d0`), `+6` bit `0x08` and `+6` bit
`0x20` (object-show and physics-impulse gates) — CONFIRMED tests, meanings HYPOTHESIS.

**Handle resolution.** A 64-bit object handle is turned into an object by `0x00458230` on table `0x024433a8` with key
`0x031d152c`, followed by the same `+0x33` bit-`0x10` rejection and a kind-flag test — CONFIRMED (garage, speed and
spatial-query code).

**Local-ownership test** `0x008addb0(obj, 0)` returns **true for a null object** and otherwise defers to `0x00884760`
— CONFIRMED (follow-up dump). Several setters in this tranche act locally when it is true and otherwise **serialize a
network message** instead (message ids `0x3e`, `0x41`, `0x42`, `0x43`, `0x45` seen) — CONFIRMED structure; wire
format not covered here.

---

## A. Registrars — where the 25 names live

Five different registrars store these names. CONFIRMED — `index.txt` plus the xref run:

| registrar | reached from | names in this tranche |
|---|---|---|
| gameplay `0x00a20840` | (the established gameplay registrar) | 16: the three `mesh_mover_*`, the three `inv_item_*`, `human_has_taken_non_melee_damage_from_player`, `hud_x_of_y_remove`, `hud_get_text_adventure_current_screen_index`, `helicopter_set_dont_use_constraints`, the three `group_*`, `get_vehicle_speed`, `get_closest_npc_to_object`, `get_char_skydiving_state` |
| `0x007c9fc0` (main-menu sub-registrar) | UI registrar `0x008430f0`, call at `0x0084315f` | 3: `main_menu_set_coop_menu_type`, `main_menu_new_game`, `main_menu_horde_start` |
| `0x007e18e0` (difficulty sub-registrar) | UI registrar `0x008430f0`, call at `0x0084317d` | 1: `get_current_difficulty` |
| `0x005fc500` (garage sub-registrar) | UI registrar `0x008430f0`, call at `0x00843150` | 1: `garage_repair_vehicle` |
| `0x00845aa0` | `0x008489e0` (call at `0x00848cdc`), itself called from `0x005d2400` (call at `0x005d245c`) | 4: `hud_diversion_remove_callback`, `get_get_key_names_for_axis_action`, `game_steam_open_overlay`, `game_start_find_syslink_servers` |

- `0x00845aa0` is **not** reached through the 311-entry UI registrar `0x008430f0`. It is a separate registrar on a
  different call chain, and its role is OPEN. Team B tags its four names `ui`, which fits.
- The main-menu sub-registrar builds a local array of `{name, function}` pairs and then registers 18 (`0x12`) entries in
  a loop: `lua_pushcclosure` (`0x00dfe4f0`), then `lua_setfield` into globals (`0x00dfe830`, index `-10002`). CONFIRMED —
  range run, `0x007ca102`-`0x007ca12b`.
- The second reference to `main_menu_new_game`'s handler at `0x007ca11b` is only that loop's function-pointer push, not
  a second name. Ghidra annotates the push with a possible constant value. HIGH CONFIDENCE.
- **Alias:** `get_current_difficulty`'s handler `0x00a47300` is also stored by the gameplay registrar under the name
  **`difficulty_get_level`**: string `0x0117cc8c` at `0x00a21e4e`, handler at `0x00a21e59`. The two names are the same
  function. CONFIRMED — range run.
- The second use of the `main_menu_new_game` string, at `0x00ff9460` (outside any function), passes it to the
  constructor `0x007b5ff0`. That is a static initializer for an unrelated named object, not a Lua binding. HIGH
  CONFIDENCE (`main_menu_new_game_1.txt`).

---

## B. Mesh movers — 3 functions

### B.1 `mesh_mover_set_damaged_by_player_only` → `0x00a53310`

**Arguments:** arg 1 mover name (string), arg 2 boolean (read unconditionally, so missing means false).
**Return:** **0 values on success; 1 value (`false`) on any failure.** The return count therefore depends on
whether the call worked. CONFIRMED.
**Body:** it resolves the name through `0x006434a0` (mesh-mover kind) and requires the result to be alive. It reads the
sub-object pointer at mover `+0xc0`, and if that is non-null it sets or clears **bit 26 (`0x04000000`) of the dword at
sub-object `+0x14c`** to match the boolean. Failure cases: null name, not found, dead, or null `+0xc0`. CONFIRMED —
disassembly.
**Side effects:** that one flag bit only; there is no network message. Who reads the bit is OPEN. The name suggests
"only the player's damage counts", which is a HYPOTHESIS.

### B.2 `mesh_mover_ready` → `0x00a522f0`

**Arguments:** arg 1 name.
**Return:** if the name resolves to an object: **1 value, `true`**. If not: **2 values, `false` then `true`**.
CONFIRMED (the false is pushed first, then an unconditional true, and the return count is 1 + not-found).
**Body:** it calls the generic any-object resolver `0x00a29200(name, 1)`. That resolver tries, in order: the
kind-filtered character chain `0x00a280c0`; then `0x00734e90`, dispatching by kind through virtual slot `+0x70` (or
through `0x00a27100` / `0x006fc2b0`); then `0x005982e0`; then `0x00807930` / `0x00a27120`. CONFIRMED (body dumped).
**There is no mesh-mover kind check and no readiness check**: any resolvable object name answers `true`. CONFIRMED.
The resolver's second argument (`1`) does not appear in its decompile, so its use is OPEN.

### B.3 `mesh_mover_apply_impulse` → `0x00a52d70`

**Arguments:** arg 1 mover name, arg 2 target object name, arg 3 magnitude (number).
**Return:** 0 values. CONFIRMED.
**Body:** it resolves the mover through `0x006434a0` and the target through `0x005982e0`, and requires both to be alive.
It builds a vector from the **target's three floats at `+0x64`, `+0x68`, `+0x6c`, each multiplied by the magnitude**,
and calls `0x00a325f0(&vector)` on the mover. CONFIRMED. On objects, `+0x40` is the position and `+0x4c`…`+0x6c` a 3×3
orientation (the same layout is copied out by `0x00a455a0`, see G.2). So `+0x64..+0x6c` is the orientation's third
row, and the impulse direction is the **target's forward axis** — HYPOTHESIS, supported by the layout.
**`0x00a325f0`** (dumped):
1. If global byte `0x027038c0` is 0 and the low 16 bits of mover `+0xc` are non-zero, it serializes a network message
   (`0x43`, sub-codes 7 and 8: the mover reference, then the vector) and sends it through the session.
2. Either way it then takes the mover's physics object through virtual slot `+0x70`. If that object exists, has
   kind-flag `+6` bit `0x20`, and is locally owned, it calls `0x00a06a30` and `0x00a06460(obj, 1, 0)` (HYPOTHESIS:
   wake / activate), then `0x00a0ac40(obj, vector, obj+0x40)`, which applies the impulse at the object's position.

CONFIRMED structure; callee meanings HYPOTHESIS.

---

## C. Main menu, difficulty, multiplayer front-end — 6 functions

### C.1 `main_menu_new_game` → `0x007c9d20`

**Arguments:** none read. **Return:** 0 values.
**Body:** a tail call into the **new-game routine `0x007ae950`**. CONFIRMED. That routine (dumped at depth 1):
- if multiplayer byte `0x024d4461` equals 1, calls `0x00867740` (HYPOTHESIS: leave or reset the network session);
- initialises the state object `0x014ff338` (`0x0086bbd0`), fills a 3-byte record with 10, 0, 0, and applies it
  (`0x006fe180`, `0x006fe1d0(0)`);
- sets bytes `0x022425dc` and `0x022425dd` to 1;
- calls `0x0088ba10(4, 2)` (HYPOTHESIS: game-state transition);
- **copies the world name `"sr3_city"` into the 128-byte buffer `0x02242558`**;
- calls `0x007ae0a0(1)`, `0x0059f8c0(-1, 0x007b4cb0, 1)` (registers a callback), `0x00b97270` and `0x00d2f540`.

CONFIRMED call sequence; meanings HYPOTHESIS.

### C.2 `main_menu_horde_start` → `0x007c9d60`

**Arguments:** none read. **Return:** 0 values.
**Body:** calls `0x006e6630`, which sets **byte `0x014f3d34` = 1** (a mode flag read by about 20 functions in
`0x006e44f0`…`0x006e9600`), sets **dword `0x014f3d2c` = 0**, and then **tail-jumps into the same new-game routine
`0x007ae950`**. So horde start is "set horde mode, then start a new game on `sr3_city`". CONFIRMED.
`0x006e6610` is the same pair of stores without the jump (follow-up dump); its Lua name, if any, is OPEN.

### C.3 `main_menu_set_coop_menu_type` → `0x007c9ed0`

**Arguments:** arg 1 boolean. **Return:** 0 values.
**Body:** stores the boolean as a byte in **`0x02296a60`**. CONFIRMED.
**Consumer:** the only reader is `0x007070a0` (follow-up dump). In its non-zero-argument branch, after other checks,
it returns early when the byte is 0, and otherwise calls `0x0088dc50(0xb)`. A sibling condition in the same branch
calls `0x0088dc50(10)` instead. CONFIRMED. What menu types 10 and 11 are is OPEN.

### C.4 `get_current_difficulty` (alias `difficulty_get_level`) → `0x00a47300`

**Arguments:** none read.
**Return:** 1 number, the dword **`0x012ece10`**, read through the 1-instruction getter `0x005f4aa0`. The file-backed
initial value is **1**. CONFIRMED.
**Writer:** `0x005f4a80` stores the value only if it is **below 3 as an unsigned compare** (so 0, 1 or 2; negative
input is rejected), after calling `0x0101b460`. CONFIRMED (follow-up dump). Its Lua-facing caller is `0x007e18a0`, the
handler two slots later in the same difficulty sub-registrar. It reads arg 1 as a truncated number and calls the
setter only if the value differs from the current one. CONFIRMED. The Lua name of `0x007e18a0` was not dumped (OPEN).
That the three values are the three difficulty levels is HIGH CONFIDENCE.

### C.5 `game_steam_open_overlay` → `0x008420c0`

**Arguments:** arg 1 string. A non-string reads as a null pointer and is passed on unchanged. **Return:** 0 values.
**Body:** it calls the imported **`SteamFriends`** accessor (the pointer at `0x0101c570`). If that returns non-null, it
calls it again and invokes **virtual slot `+0x4c`** on the result with the string. CONFIRMED. That the slot is the
Steam overlay-activation method is HIGH CONFIDENCE from the name. A null string can reach Steam; its handling there is
OPEN.

### C.6 `game_start_find_syslink_servers` → `0x00842560`

**Arguments:** none read. **Return:** 0 values.
**Body:**
1. Calls the constant-1 stub `0x00d34cd0` and discards the result.
2. Calls `0x007ade60`. If guard byte **`0x022425eb`** is 0, that routine:
   - runs teardown `0x00872830`: if a find-server pool exists (`0x024d683c`), releases it (`0x00db53e0`), zeroes its
     bookkeeping, calls `0x00d223d0` and `0x00867740`, and sets `0x01304378`;
   - runs setup `0x00872760`: calls `0x008675c0`, creates a pool named **`"findserver_pool"` of `0x11800` bytes**,
     allocates one `0x11800`-byte block from it, and **links 64 records of `0x460` bytes into a ring** (64 × `0x460` =
     `0x11800` exactly). It then calls `0x00d225f0` and sets `0x024d6831` and `0x01304378` to 1;
   - sets the guard to 1 in all cases.

So **repeated calls are no-ops until the guard is cleared**. CONFIRMED (dumps + follow-up dump). The clearer is
`0x007ade80` (if the guard is 1, call `0x00872820`; then guard = 0), which is HIGH CONFIDENCE the stop counterpart.
Its Lua name is OPEN.

---

## D. Inventory — 3 functions

Two item tables appear:
- **Weapon table**: base `0x028dc4dc`, count `0x028dc4e0`, stride `0x7ec`. Name pointer at `+0`, enabled flag at `+0x18`
  bit 2, class byte at `+0x54`, subtype at `+0x44`, linked definition at `+0x5c`. Looked up case-insensitively by
  `0x00b81220`, which returns null for a null name. `0x00b81a50` maps an entry back to its index, or `0xff` when the
  pointer is outside the table. CONFIRMED.
- **Item-definition table**: fixed at `0x0250adc0`, **110 entries of `0x34` bytes**, enabled flag at `+0x30` bit 0.
  `0x008dcb10` matches the ASCII name at `+0` **or** the wide display name at `+8`, both case-insensitive. For the wide
  match the input is first widened into a 1024-wchar stack buffer. CONFIRMED.

### D.1 `inv_item_add_ammo` → `0x00a50e40`

**Arguments:** arg 1 **character** name, arg 2 item name, arg 3 amount (truncated number). The order differs from
`inv_item_add_do`, which takes the item first. CONFIRMED.
**Return:** 0 values.
**Body:**
1. Looks the item up in the weapon table; if it is not found, nothing happens.
2. Reads the class with `0x008dc690`, which returns the byte at `+0x54`. Its pre-check `0x00b81870` only tests whether
   that byte is already 10, so the check is redundant.
3. **Class `0xb` → nothing happens.**
4. **Class 9 → subtype** taken from `+0x44`: 1→3, 2→2, 3→1, anything else → 0.
5. Only then resolves the character through `0x00a281a0`. If it is found, calls `0x00a26cc0(character, class, amount,
   subtype)`.

CONFIRMED.
**`0x00a26cc0`:** if the character is not locally owned, it sends message `0x43` (sub-code `0x18`). The message carries
the character's 16-bit network id, the class byte, **the amount truncated to 16 bits**, and the subtype as 16 bits.
Otherwise it picks an ammo pool: `0x005fed90(subtype)` for class 9, else `0x005fec20(class)`. If the pool is non-null it
calls **`0x005fd930(character, pool, amount, 0)`**. CONFIRMED structure. `0x005fd930` is HIGH CONFIDENCE the actual
ammo add. There is no sign or range check on the amount at this level.

### D.2 `inv_item_add_do` → `0x00a51dc0`

**Arguments:** arg 1 item name, then three optional arguments read in order. Each is consumed only if it is present and
not nil; otherwise its default applies:
- count (truncated number, default **1**);
- character name (default **`"#PLAYER1#"`**);
- boolean (default false).

CONFIRMED.
**Return:** 0 values.
**Body:** it resolves the character through `0x00a281a0`; if that fails, nothing happens. It works on the character's
**inventory at `+0x1b78`**:
- **Weapon path** (the name is in the weapon table): calls `0x00602900(inventory, weapon, count, boolean, 1)`, then
  returns.
- **Fallback path** (otherwise): finds the name in the 110-entry item-definition table. If found, calls
  `0x00601380(inventory, entry, &flags)`, where flags bit 0 is the boolean. **The count is ignored on this path.**

CONFIRMED.
**`0x00602900`** (dumped):
- If the inventory's owner (handle at inventory `+0xb0`/`+0xb4`) is not locally owned, it sends message `0x41`
  (sub-code `0x27`): owner, weapon index byte (`0x00b81a50`), **count as 16 bits**, the boolean, and the trailing 1.
- Otherwise, because the trailing argument is 1, it first gets the class pool (`0x005fec20`) and calls
  `0x00602020(pool, 0, 0)` (HYPOTHESIS: fill that ammo pool).
- It then calls `0x00601380(the weapon's linked definition at +0x5c, &flags)`.
- If that returns an object, it calls `0x004d7f30(owner, 0, object-if-kind-+9-bit-0x80-else-0, count, 0, 0, 0)` and
  passes the result to `0x009bfd70`.

CONFIRMED structure; meanings HYPOTHESIS. `0x00601380` is a large give/equip routine. Flag bits 0, 1, 2, 3 and 4 each
change its path; only bit 0 is reachable from Lua here. Their meanings are OPEN.

### D.3 `inv_item_get_weapon_upgrades` → `0x00a51000`

**Arguments:** arg 1 item name, arg 2 optional character name (read only if present and not nil; default
`"#PLAYER1#"`). Resolution uses the **kind-filtered chain `0x00a280c0`, not `0x00a281a0`**. CONFIRMED.
**Return:**
- **2 values (level, -1)** when found. The level is the first dword of the upgrade record, as a number.
- **1 value, -1**, otherwise.

The trailing -1 (the double at `0x012a3038`) is pushed in both cases. CONFIRMED.
**Body:**
1. Finds the inventory slot with `0x005fec00`: the name goes through the item-definition table, then `0x005feaa0` on
   the inventory at `+0x1b78`.
2. Requires the slot's weapon pointer at `+0x19c` to be non-null.
3. Gets the record from `0x00b76850(character, weapon)`. That function returns null if either argument is null.
   Otherwise it returns **`[0x028dc470][slot] + index * 0x7f8`**, where `slot` comes from `0x008adff0(character)` and
   `index` is `0x00b81a50(weapon)`.

CONFIRMED.
**Crash-shaped (HYPOTHESIS, flagged):**
- `0x008adff0` (follow-up dump) returns byte `+3` of the character's player record at `+0x3c`. If there is no record,
  it falls back to `0x00889e00()` and a byte at `+0x158` of a nested record. Failing that it returns **`0xff`**.
- Neither `0x00b76850` nor the handler checks the slot against the size of the pointer array at `0x028dc470`, or the
  array entry for null.
- So a character with no player slot (`0xff`), or a slot whose array entry is null, gives a garbage or near-null record
  pointer, which the handler dereferences at `0x00a51082`.

Whether a non-player character can reach this path, and the array size, are OPEN.

---

## E. HUD — 3 functions

### E.1 `hud_x_of_y_remove` → `0x00a4e9e0`

**Arguments:** arg 1 index (truncated number, no range check). **Return:** 0 values.
**Body:** calls `0x007ed9e0(index)`, which calls `0x007ed7d0(0x022b6758 + index * 0x34, 8, index)`. CONFIRMED.
**`0x007ed7d0`** (follow-up dump) works on that 0x34-byte slot:
- **Network notification:** if slot flag dword `+0x1c` has bit 1 set, it sends message `0x3e` (with 8 and the index)
  through the session. The one exception is when slot byte `+0xc` is zero **and** flag bit 0 is set.
- **Active-list removal:** if flag bit 0 is set, it removes the slot pointer from the active list at `0x022b65e0`
  (count `0x022b6600`) by shifting the rest down. When slot `+0x18` is 0 it also decrements `0x022b6604`. A state value
  of 2 at `0x022b6630` becomes 4.
- **Always,** it calls **virtual slot `+0xc` through the slot's own first dword**.

CONFIRMED.
**Crash-shaped (CONFIRMED missing check):** neither the handler, `0x007ed9e0` nor `0x007ed7d0` bounds-checks the
index. An out-of-range or negative index makes the code read flags from arbitrary memory, and then **make an indirect
call through whatever dword sits at the computed slot address** (at `0x007ed952`). This is a wild-call risk, not just
a bad read. The table's element count is OPEN.

### E.2 `hud_get_text_adventure_current_screen_index` → `0x00a4dda0`

**Arguments:** none read. **Return:** 1 number.
**Body:** calls `0x00841090`, which **reads the dword `0x01301124` and then resets it to -1**. The file-backed initial
value is -1. CONFIRMED.
- **This is a consume-once read:** a second call without a new store returns -1. CONFIRMED.
- The only other writer is `0x008410c0`, a Lua-shaped handler (`lua_gettop` prologue) that stores arg 1 truncated to an
  int. CONFIRMED (follow-up dump). Its Lua name is OPEN; it is HYPOTHESIS that the text-adventure UI pushes the chosen
  screen index this way.

### E.3 `hud_diversion_remove_callback` → `0x00844780`

**Arguments:** arg 1 diversion type (truncated number). **Return:** 0 values.
**Body:** calls `0x007f9880(type)`. For types `0x77`, `0x78`, `0x79`, `0x7a` it **decrements the matching counter
`0x022b7a94`, `0x022b7a98`, `0x022b7a9c`, `0x022b7aa0`, but only if it is above 0**. Any other type is ignored.
CONFIRMED (jump table at `0x007f98d4` read in the range run: entries `0x007f9893`, `0x007f98a3`, `0x007f98b3`,
`0x007f98c3`).
**Meaning:** the counters are slot occupancy. The admission check `0x007f9b70` (follow-up dump) refuses a new diversion
when the first three counters sum to 5 or more, or when the type's own counter has reached its cap. The caps are **4
for `0x77`, 2 for `0x78`, 1 for `0x79`, and `0x7a` requires 0**. CONFIRMED. So this function frees a diversion slot.
Excess calls are harmless because of the floor at 0.

---

## F. Groups, helicopter, skydiving, damage flag — 6 functions

### F.1 `group_show_do` → `0x00a4ca60` and F.2 `group_hide_do` → `0x00a4c9a0`

**Arguments:** arg 1 group name. **Return:** 0 values.
**Body:** they resolve the group through `0x005eab60` and require it to be alive, then call `0x00a2b3b0(group, flag)`
with **flag 0 for show and 1 for hide**. CONFIRMED.
**`0x00a2b3b0`** walks the circular member list at group `+0x3c` (next pointer at member `+0x88`). For each member
whose virtual slot `+0x68` returns true, it takes the object from virtual slot `+0x70`, keeps it only if it is non-null
**and** has kind-flag `+6` bit `0x08`, **otherwise uses null**, and calls **`0x008ccb90(object, flag, 1, 0)`
unconditionally**. CONFIRMED.
**`0x008ccb90`** (follow-up dump):
- If the object is remote, it sends message `0x45`.
- Otherwise, if bit 0 of object byte `+0x3b` differs from the flag, it stores the flag there, sets bit 1 of the word at
  `+0x74`, and calls `0x008cc4e0`.
- Unless bit 0 of byte `+0x3a` is set, it then calls virtual slot `+0x38` with `(visible = flag == 0, 1)`.
- The remote test runs first: `0x00853b30(object, 0x21)`, which returns 0 for null (second follow-up dump), then
  `0x008addb0`, which returns true for null.

**Crash-shaped (CONFIRMED path):** a null object passes both tests and **reaches the read of byte `+0x3b` with no null
check**. So if any loaded member's object is null or lacks kind-flag `+6` bit `0x08`, show and hide both dereference
address `0x3b`. Whether such members exist in shipped data is OPEN.

### F.3 `group_is_loaded_internal` → `0x00a4c9f0`

**Arguments:** arg 1 group name. **Return:** 1 boolean.
**Body:** resolves the group through `0x005eab60` and requires it to be alive, then returns **bit 1 of group byte
`+0x3a`**. A failed lookup returns false. CONFIRMED.

### F.4 `helicopter_set_dont_use_constraints` → `0x00a4cdc0`

**Arguments:** arg 1 vehicle (or character) name, arg 2 boolean. **Return:** 0 values.
**Body:** resolves through the dedicated vehicle resolver `0x00a281e0`, calls the constant-1 stub `0x00d21330` and
discards the result, then calls `0x00b37ac0(vehicle, boolean)`. **There is no helicopter-kind check** in the handler or
in `0x00b37ac0`. CONFIRMED.
**`0x00b37ac0`:**
- If the vehicle is not locally owned, it sends message `0x42` (sub-codes `0x14`, 6) with the vehicle's id and the
  boolean.
- Otherwise it gets the record at **vehicle `+0x100`** through `0x00a79470`, which lazily initialises the record
  (`0x00ad3220`/`0x00ad3200`) and never returns null (follow-up dump).
- If **bit 2 of record byte 2** differs from the boolean, it stores the boolean there.
- If **bit 0 of record byte 0** is also set, it detaches the chain at vehicle `+0x11a0` (zeroing the field). It
  allocates `record[0x160]` nodes from `0x00a16920`, stopping early if the allocator returns null. Each node is indexed
  through `0x00b34500` and linked into a ring that starts at the detached chain. Finally it calls
  `0x00b36220(vehicle, last node)`.

CONFIRMED. HYPOTHESIS: record byte 0 bit 0 means "this vehicle has rotor constraint data", so non-helicopters only
store the bit.

### F.5 `get_char_skydiving_state` → `0x00a4adc0`

**Arguments:** arg 1 character name.
**Return:**
- **2 values (state, -1)** when the character resolves (through `0x00a281a0`);
- **1 value, -1**, otherwise.

CONFIRMED.
**State** (`0x009adbd0`, dumped):
- **255** (a byte `0xff` pushed as a number, *not* -1) when character mode `+0xcc8` is not `0xe`;
- otherwise the sub-state byte at `+0xcce` is mapped:

| sub-state | state |
|---|---|
| `0x16`, `0x17`, `0x1a` | 0 |
| `0x18` | 1 |
| `0x19`, `0x1b`, `0x22` | 2 |
| `0x1f` | 3 |
| `0x20` | 4 |
| `0x21` | 5 |
| `0x1c` | 6 |
| `0x1d` | 7 |
| `0x1e` | 8 |

  Anything outside `0x16`…`0x22` maps to 255.

CONFIRMED. Scripts therefore see two different "not available" values: -1 when the character is not found, 255 when
it is not skydiving.

### F.6 `human_has_taken_non_melee_damage_from_player` → `0x00a4d2e0`

**Arguments:** arg 1 character name. **Return:** 1 boolean.
**Body:** resolves through `0x00a281a0` and returns **bit 21 (`0x00200000`) of character dword `+0xec`**. A failed
lookup returns false. CONFIRMED. Which code sets the bit is OPEN.

---

## G. Queries — 3 functions

### G.1 `get_vehicle_speed` → `0x00a4c440`

**Arguments:** arg 1 name (vehicle, character, or a third object kind).
**Return:** 1 number, an integer speed. 0 when nothing resolves. CONFIRMED.
**Body:** it finds a vehicle handle in up to three ways. It starts from the null-handle constant at
`0x0117f918`/`0x0117f91c` (HIGH CONFIDENCE that this is zero):
1. **Vehicle:** `0x0062a190` (on `0x02442750`), alive. The handle is at `+8`/`+0xc` of its virtual-`+0x70` object; if
   that object is null, the default handle is used.
2. **Character:** `0x00a280c0`. The handle is the character's current vehicle at `+0x16c0`/`+0x16c4`.
3. **Third tier:** `0x005e4dd0`, alive. The handle is read at `+0x16c0`/`+0x16c4` of its **virtual-`+0x70` result**.

The handle then goes through `0x00ad53f0`: resolve it, require vehicle kind (`+6` bit `0x80`) and alive, then call
**`0x00ad4160`**. That function returns 0 unless vehicle byte `+0xbd0` equals 1. Otherwise it returns **truncate(
|velocity| × [`0x0283b630`] + [`0x01184f40`])**, where the velocity comes from `0x00abae00`. CONFIRMED (follow-up dump).
The scale is a runtime value and the units are OPEN; HYPOTHESIS: metres per second converted to mph, with a rounding
bias.
**Crash-shaped (CONFIRMED missing check):** on tier 3, the result of virtual `+0x70` is dereferenced at `0x00a4c4e4`
with no null test, and its kind is not checked to be a character. The group code (F.1) null-checks the same virtual
slot, which suggests it can return null. Reachability is OPEN.

### G.2 `get_closest_npc_to_object` → `0x00a4b7c0`

**Arguments:** arg 1 reference object name, arg 2 radius (number).
**Return:** always **2 values**:
- **(distance, name)** for the closest match;
- **(3.4028234663852886e38, "")** otherwise. That is the float maximum, and the string at `0x0129a0e3` is empty
  (first byte 0).

CONFIRMED.
**Body:**
1. **Radius ≤ 0 → not found.**
2. The reference must resolve through **`0x00734e90`** and be alive. HYPOTHESIS: a pseudo-name like `"#PLAYER1#"`
   fails here even though the position helper handles it.
3. Its position comes from `0x00a455a0`. That helper copies the position from `+0x40` and the 3×3 orientation from
   `+0x4c`. It tries a character first (`0x00a280c0`), then other kinds through `0x00734e90`, with special cases.
4. Spatial query `0x0075d4b0` over the box centre ± radius on each axis, type mask `0x2f`, **capacity 32 handles** into
   a stack array.
5. Each handle is accepted only if it:
   - resolves to an object without the `+0x33` bit-`0x10` flag;
   - has human kind (`+0xa` bit `0x04`) and is alive;
   - passes `0x0096f4f0`: mode `+0xcc8` is not 5 and byte `+0xe3` bit 0 is clear (HYPOTHESIS: not dead or dying);
   - **is not the reference object itself**;
   - has a **non-empty script name** from `0x00a353d0`.
6. **Name rules** (`0x00a353d0`):
   - player-kind objects: the name at `+0x18` when a session condition holds, else from `0x00a1fda0`;
   - humans: the name pointed to by `+0x1da8` (`+0x18`);
   - otherwise followers get **`"#FOLLOWER#"`**.

   So **unnamed ambient NPCs are never returned**.
7. **Selection:** the first best is the radius squared, and a candidate wins only with a strictly smaller squared
   distance (`0x0043aa90`). The pushed distance is the square root of the best.

CONFIRMED.
**Buffer safety:** the 32-entry cap is enforced. The query object keeps buffer, capacity and count at `+8`, `+0xc` and
`+0x10`, and the append `0x00ab1a20` refuses to add when the count has reached the capacity (and also skips
duplicates). Both scan passes append through it (`0x0075ae80`, `0x0075af60`). CONFIRMED (follow-up dumps). There is no
overflow. **Side effect of the cap:** with more than 32 candidates in the box, the true closest NPC can be dropped —
HYPOTHESIS about the effect, CONFIRMED cap.

### G.3 `get_get_key_names_for_axis_action` → `0x00842320`

The doubled `get_` is in the registered string itself (`0x01163a08`) — CONFIRMED.
**Arguments:** arg 1 axis-action name. **Return:** always **2 strings**. CONFIRMED.
**Body:**
1. `0x005be5d0(name, wide buffer A, wide buffer B)`. Each buffer is a 32-wchar (64-byte) stack array of the handler.
2. Each buffer is then encoded by `0x00e22a30` into a 100-byte stack buffer, with output limit 99, and pushed as a C
   string.
3. The encoding is NUL-free so the string survives as a byte string. It writes a `0x80` lead byte, then 3 bytes per
   UTF-16 unit with flag bits marking zero bytes as `0xff`, and ends with `0x08 0x00`. CONFIRMED.

**`0x005be5d0`** compares the name case-insensitively against the first **34** entries of the `{index, name}` table
at `0x01122690`. The ptrs run lists them: `CAA_CAMERA_ROTATE` (index 0), `CAA_CAMERA_ELEVATE`, `CAA_WALK_*`,
`CAA_DRIVE_*`, `CAA_HELI_*`, `CAA_CRANE_*`, `CAA_VPC_TURRET_*`, `CAA_PLANE_*` and `CAA_TURRET_CAMERA_*`, mapping to
indices 0…`0x21`. The 35th entry, `STICK_DIR_UNBOUND` with index -1, is past the loop bound. On a match:
- **index 0** → both buffers get the localized `"STR_THE_MOUSE"` (`0x0084a1b0`), truncated to 31 wchars;
- any other index → the two key names come from the binding table at `0x01411428` / `0x0141142c` (stride `0x34`),
  through `0x005be0d0`. They are formatted into the buffers with the format at `0x0111fdfc`. The local defaults are
  built as `"unknown"` for the first and an emptied string for the second.

CONFIRMED (the conversion `0x005be0d0` and the format were not dumped).
**Bug (CONFIRMED):** the handler **never initialises the two wide buffers**, and `0x005be5d0` writes them only on a
match with a non-(-1) index. So **an unknown or nil action name returns two strings encoded from uninitialised stack
memory**. This is junk or residue from earlier calls, not empty strings. The encoder is bounded by its output size and
reads at most 33 wchars (66 bytes) of each 64-byte source. At worst that is 2 bytes past the end into the adjacent stack
buffer, so it is a garbage-return and information-leak defect, not a crash.

---

## H. Garage — 1 function

### H.1 `garage_repair_vehicle` → `0x005f88d0`

**Arguments:** none read. **Return:** 0 values.
**Body:**
1. Takes the **garage's current vehicle handle**, the 64-bit global `0x014a1d28`/`0x014a1d2c`. It resolves it and
   requires vehicle kind (`+6` bit `0x80`), no `+0x33` bit `0x10`, and alive. **Otherwise the vehicle is null.**
2. Computes **`cost = 0x00a9bbe0(vehicle)`**.
3. Builds a money value with `0x009601d0(-cost)`. That clamps the input to ±20,000,000, multiplies by 100, and clamps
   to ±2,000,000,000.
4. Calls **`0x0094d920` on the local player with that value and reason code `0xb`**. That routine references the
   strings `"cash_adjust"` and `"human"`, so it is HIGH CONFIDENCE a cash debit.
5. Calls **`0x00a9f980(vehicle, 1.0, -1, 1, 1)`**. That function returns immediately if the vehicle is null or the
   second argument is ≤ 0. HYPOTHESIS: repair to full health.

CONFIRMED.
**Cost formula (`0x00a9bbe0`):**
- `maximum` = vehicle `+0x155c`, `current` = vehicle `+0x1564` (follow-up dump: `0x00a77e20`, `0x00a77e10`).
- If `maximum` (as unsigned) is above 0, `percent = current × K / maximum`, with K the double at `0x012a2dd8`
  (HYPOTHESIS 100.0, consistent with the comparison against 100).
- Rate:
  - **0** if `percent ≥ 100 − trunc([0x027b34c0] × K)`;
  - else **0.01** above 74, **0.02** above 49, **0.03** otherwise;
  - 0 if `maximum` is 0.
- `cost = round(base × (1 − [0x027b34b0]) × rate)`, where `base` is the unsigned dword at `[vehicle + 0xbf4] + 0x4a4`.
- The constants 0.01, 0.02 and 0.03 are `0x012a2dd0`, `0x012a2ea8` and `0x011175e0`.

CONFIRMED. Healthier vehicles are cheaper, and a health level near full is free. HYPOTHESIS: `0x027b34b0` is a discount
(written by `0x00a9bd50`/`0x00a9bd80`) and `0x027b34c0` is the free-repair threshold.
**Crash (CONFIRMED):** `0x00a9bbe0` has **no null check**. It calls the getter on the vehicle and unconditionally reads
`[vehicle + 0xbf4]` (at `0x00a9bc95`). So **`garage_repair_vehicle` with no current garage vehicle** (handle zero,
stale, dead, or not a vehicle) **dereferences null**. The sibling garage handler `0x005f8710` (second follow-up dump;
it pushes the cost) calls `0x00a9bbe0` only after a successful resolution and pushes 0 otherwise. So the repair handler
is the one missing that guard.
**Other observations:** the player is charged **before** the repair, with no affordability check visible in the
handler. Whether `0x0094d920` clamps at zero cash is OPEN.

---

## I. Cross-function observations

1. **Return shapes that vary with the outcome:**
   - `mesh_mover_set_damaged_by_player_only` returns 0 values on success and 1 (`false`) on failure.
   - `mesh_mover_ready` returns `true`, or `false, true`.
   - `get_char_skydiving_state` and `inv_item_get_weapon_upgrades` return `(value, -1)` or `(-1)`. The trailing -1
     sentinel is pushed in both cases.
   - `get_closest_npc_to_object` always returns 2 values (float maximum and `""` when nothing is found).

   All CONFIRMED.
2. **Crash-shaped findings, most certain first:**
   - (a) `garage_repair_vehicle`: null vehicle into `0x00a9bbe0` (CONFIRMED path; reachable whenever the garage handle
     is unset or stale).
   - (b) `hud_x_of_y_remove`: an unchecked index gives a wild virtual call (CONFIRMED missing check).
   - (c) `group_show_do` / `group_hide_do`: a null member object reaches a `+0x3b` read (CONFIRMED path; reachability
     OPEN).
   - (d) `get_vehicle_speed` tier 3: a virtual result is dereferenced without a null check (CONFIRMED missing check;
     reachability OPEN).
   - (e) `inv_item_get_weapon_upgrades`: the player-slot index (up to `0xff`) and a possibly null array entry are both
     unchecked (HYPOTHESIS).

   Non-crash defect: `get_get_key_names_for_axis_action` returns uninitialised stack as two strings for unknown actions
   (CONFIRMED).
3. **No kind validation where the name promises one:**
   - `mesh_mover_ready` accepts any object.
   - `helicopter_set_dont_use_constraints` accepts any vehicle, and also a character (through `0x00a281e0`).
   - `get_vehicle_speed` accepts a character and reports the vehicle it is in.

   CONFIRMED.
4. **Networked setters:** `inv_item_add_ammo`, `inv_item_add_do`, `helicopter_set_dont_use_constraints`,
   `mesh_mover_apply_impulse`, group show/hide and `hud_x_of_y_remove` all branch on local ownership or flags and send
   a message instead of, or as well as, acting locally. In the ammo and add messages the amount or count is **truncated
   to 16 bits**. CONFIRMED.
5. **Global, not per-player, state:** the difficulty (`0x012ece10`), the co-op menu flag (`0x02296a60`), the horde
   flag (`0x014f3d34`), the text-adventure index (`0x01301124`), the diversion counters and the syslink guard
   (`0x022425eb`) are single globals. CONFIRMED.
6. **Dead or stub calls:** `0x00d34cd0` (in syslink start) and `0x00d21330` (in the helicopter setter) return the
   constant 1, and their results are discarded. CONFIRMED.
7. **Shared new-game path:** `main_menu_new_game` and `main_menu_horde_start` end in the same routine `0x007ae950`,
   which hard-codes the world name `"sr3_city"`. CONFIRMED.

---

## J. OPEN items

1. Element count of the HUD x-of-y table at `0x022b6758` (stride `0x34`), needed to state the valid index range for
   `hud_x_of_y_remove`.
2. Size and population of the per-player upgrade pointer array at `[0x028dc470]`, and whether a non-player character
   can reach `inv_item_get_weapon_upgrades` with slot `0xff` or a null entry.
3. Whether virtual slot `+0x70` can return null for `0x005e4dd0`-kind objects (the `get_vehicle_speed` crash), and
   what that kind is.
4. Whether any group member can be "loaded" with a null or non-`+6`-bit-`0x08` object (the group show/hide crash).
5. Speed units: the runtime scale `0x0283b630` and the bias `0x01184f40`.
6. Lua names of the unnamed siblings: `0x008410c0` (text-adventure index setter), `0x007e18a0` (difficulty setter),
   `0x007ade80` (syslink stop), `0x006e6610` (horde flag without new game).
7. The meaning of co-op menu types 10 and 11 (`0x0088dc50`) selected by `0x007070a0`.
8. What sets bit 21 of character `+0xec` (`human_has_taken_non_melee_damage_from_player`), and who reads bit 26 of the
   mesh-mover sub-object `+0x14c`.
9. Meaning of the `0x00601380` flag bits beyond bit 0.
10. The double at `0x012a2dd8` (assumed 100.0), and the two runtime repair globals `0x027b34b0` / `0x027b34c0`.
11. The meaning of the second argument (1) passed to `0x00a29200` by `mesh_mover_ready`.
12. Whether `0x0094d920` refuses or clamps a debit the player cannot afford.
13. Role of the separate registrar `0x00845aa0` (and of its callers `0x008489e0` / `0x005d2400`).
14. How Steam handles a null string passed to the overlay call.

---

## K. Cleanroom check

The text above uses plain hex addresses only and contains no decompiler auto-names or variable names. The forbidden
pattern from `spec-lua-api-behaviour.md`'s front matter was grepped against this file before hand-off: **0 hits**.

---

## L. Direct-answer table (all 25)

| # | name | handler | registrar | status |
|---|---|---|---|---|
| 1 | `mesh_mover_set_damaged_by_player_only` | `0x00a53310` | `0x00a20840` | CONFIRMED (B.1); reader of the bit OPEN |
| 2 | `mesh_mover_ready` | `0x00a522f0` | `0x00a20840` | CONFIRMED (B.2); no kind or ready check |
| 3 | `mesh_mover_apply_impulse` | `0x00a52d70` | `0x00a20840` | CONFIRMED (B.3); direction = target forward axis HYPOTHESIS |
| 4 | `main_menu_set_coop_menu_type` | `0x007c9ed0` | `0x007c9fc0` | CONFIRMED (C.3); menu-type meaning OPEN |
| 5 | `main_menu_new_game` | `0x007c9d20` | `0x007c9fc0` | CONFIRMED call sequence (C.1); callee meanings HYPOTHESIS |
| 6 | `main_menu_horde_start` | `0x007c9d60` | `0x007c9fc0` | CONFIRMED (C.2) |
| 7 | `inv_item_get_weapon_upgrades` | `0x00a51000` | `0x00a20840` | CONFIRMED (D.3); slot-index crash HYPOTHESIS |
| 8 | `inv_item_add_do` | `0x00a51dc0` | `0x00a20840` | CONFIRMED (D.2); give-routine flag bits OPEN |
| 9 | `inv_item_add_ammo` | `0x00a50e40` | `0x00a20840` | CONFIRMED (D.1) |
| 10 | `human_has_taken_non_melee_damage_from_player` | `0x00a4d2e0` | `0x00a20840` | CONFIRMED (F.6); bit setter OPEN |
| 11 | `hud_x_of_y_remove` | `0x00a4e9e0` | `0x00a20840` | CONFIRMED (E.1); **unchecked index → wild virtual call** |
| 12 | `hud_get_text_adventure_current_screen_index` | `0x00a4dda0` | `0x00a20840` | CONFIRMED (E.2); consume-once read |
| 13 | `hud_diversion_remove_callback` | `0x00844780` | `0x00845aa0` | CONFIRMED (E.3) |
| 14 | `helicopter_set_dont_use_constraints` | `0x00a4cdc0` | `0x00a20840` | CONFIRMED (F.4); no helicopter check |
| 15 | `group_show_do` | `0x00a4ca60` | `0x00a20840` | CONFIRMED (F.1); **null-member crash path** |
| 16 | `group_is_loaded_internal` | `0x00a4c9f0` | `0x00a20840` | CONFIRMED (F.3) |
| 17 | `group_hide_do` | `0x00a4c9a0` | `0x00a20840` | CONFIRMED (F.1); **null-member crash path** |
| 18 | `get_vehicle_speed` | `0x00a4c440` | `0x00a20840` | CONFIRMED (G.1); units OPEN; **tier-3 null deref** |
| 19 | `get_get_key_names_for_axis_action` | `0x00842320` | `0x00845aa0` | CONFIRMED (G.3); **uninitialised-buffer return** |
| 20 | `get_current_difficulty` (= `difficulty_get_level`) | `0x00a47300` | `0x007e18e0` and `0x00a20840` | CONFIRMED (C.4) |
| 21 | `get_closest_npc_to_object` | `0x00a4b7c0` | `0x00a20840` | CONFIRMED (G.2); 32-candidate cap is safe |
| 22 | `get_char_skydiving_state` | `0x00a4adc0` | `0x00a20840` | CONFIRMED (F.5) |
| 23 | `garage_repair_vehicle` | `0x005f88d0` | `0x005fc500` | CONFIRMED (H.1); **null-vehicle crash** |
| 24 | `game_steam_open_overlay` | `0x008420c0` | `0x00845aa0` | CONFIRMED (C.5); slot identity HIGH CONFIDENCE |
| 25 | `game_start_find_syslink_servers` | `0x00842560` | `0x00845aa0` | CONFIRMED (C.6) |
