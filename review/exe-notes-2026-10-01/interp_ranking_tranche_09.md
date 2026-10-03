# Ranking tranche 09 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-02)

Job definition: `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-09.json` (names 151-175 of the 554
unspecced names, Team B call-count order). Run locally, read-only, on a private copy of the Ghidra project
(`tools\gp_t09`, deleted afterwards), with the job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15
maxinsn:500 <25 names>`. **All 25 names resolved in that one call.** Every name string occurs exactly once in `.rdata`,
and the handler is the code pointer stored in the stack slot right after the name (insn offset +1) — CONFIRMED —
disassembly (dump `index.txt`). Three follow-up depth-0 `func` runs on the same private copy (41 callee addresses in
all, listed in section C; cited as "follow-up dump").

**No gameplay-registrar names this time.** All 25 are UI customization names reached only through the 311-entry UI
registrar `0x008430f0`:

| sub-registrar | family | names in this tranche | already known as |
|---|---|---|---|
| `0x00830230` | `pcu_` (clothing store / wardrobe) | 10 | tranche 08 (section E), spec §26.11-§26.15 |
| `0x008377d0` | `pcr_` (character creator / plastic surgeon) | 15 | spec §11, §16.6-§16.8a, §19.3-§19.15, §23.1-§23.8, §26.1-§26.2 |

Team B's reconciliation table (`for-team-b/team-b/tools/lua_reconciliation_called_and_registered_1181.tsv`) gives
every one of the 25 names **2 call sites in 2 distinct scripts**, tagged `ui`, family `pcu` or `pcr`; that agrees.

One index oddity, not a second binding: `pcu_bra_required` is reported with 2 "uses". The second use (`0x008305d0`,
inside the registrar's `lua_pushcclosure`/`lua_setfield` loop at `0x008305c9`/`0x008305d7`) is the loop reading the
**first** row of the name/function table, which happens to be `pcu_bra_required`; the dumper's second candidate
(`pcu_bra_required_1.txt`, rooted at the `lua_setfield` primitive `0x00dfe830`) is that loop primitive, not a second
handler. The handlers `0x0082ad10` and `0x0082ae00` each show an extra DATA reference at `0x008305c7` for the same
reason. CONFIRMED — follow-up dump of the registrar (section C).

Labels: **CONFIRMED** = read in the listing or decompile of a dump made for this note; **HIGH CONFIDENCE** = follows
from a dumped call or reference, but the callee body was not dumped or a meaning is inferred from strong usage;
**HYPOTHESIS** = plausible, not settled; **OPEN** = not settled (collected in section E).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives:
`0x00dfe210` `lua_tolstring`, `0x00dfe160` `lua_tonumber` (a non-number reads as 0), `0x00ea2596` truncating
float-to-int, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe590` `lua_pushboolean`, `0x00dfe380` push nil, `0x00dfe420` push C
string (nil when null). `0x009da4e0` returns the local player (global `0x0262edfc`); `0x0087ba20` returns the session
(global `0x024d8534`). The PCU data model (owned list, saved-outfit list, catalog list `0x022db9b8`, item / variant /
category records, store working set at `0x022d0920 + s·0x720`, worn table `0x022ff5d0`, slot ids 0..23, content-pack
gate `0x0045b3a0`, "string id or literal" pairs with sentinel `0x029c9964`) is exactly as tranche 08 section E.0 states
and is cited, not repeated. The PCR 63-slot composite list (`0x026533ac`/`0x026533b0`, per-character stride `0x270`)
and the composite-definition table (`0x026542e8`, stride `0x44`, count `0x0268d13c`, category at `+0x24`, pack byte at
`+0x40`) are as spec §16.7/§16.16/§19.16 state.

**Lua callback helpers** (all dumped this tranche; tranche 08 dumped four of them and left `0x00e0c720` OPEN). Every
callback in this tranche runs as a **new Lua coroutine**, not a plain `lua_call`:
- `0x00e0ca80(name, state)` → `0x00e0c720(name, 0, 0, 0, state)` (follow-up dump). It looks the name up in the globals
  table (`0x00dfe610(L, -10002, name)`), requires the value to be a **Lua** function (type 6 and not a C function,
  `0x00dfe080` = 0), creates a **new Lua thread** (`0x00dfde10`), and registers it in a 32-byte-stride thread table at
  `0x02a42d10` (count `0x02a44d58`, **hard cap 256**; a fresh 16-bit id from the counter `0x02a44d5c`, skipping ids in
  use). The record gets `+4` = the new thread, `+8` = the parent state, `+0xc` = 1, a heap copy of the name at
  `+0x10`, `+0x14` = 0 (no parent record), `+0x18` = 0, `+0x1c` = 0 (argument count). It returns **null** when the name
  is null, **the table already holds 256 records**, the state is null, the global is missing or not a Lua function, or
  thread creation fails. CONFIRMED — disassembly. This settles tranche 08's OPEN item: a nil callback name is handled
  (null result).
- `0x00e0c8f0(name, parentRecord, 0, 0)` — used **only** by `pcr_report_skin_colors` here. Same mechanism, but the state
  comes from the **parent thread record** (`parentRecord +8`) and the new record inherits the parent's `+0x14`; a null
  parent record gives null. CONFIRMED.
- `0x00e0cba0` (called by `0x00e0cd00`; follow-up dump) runs the coroutine **synchronously**: unless the record is
  dead or suspended (flag bits `0x4`, `0x3`, or two optional hooks at `0x02a44d60`/`0x02a44d64`), it fetches the
  function by name again, moves it below the pushed arguments, marks the record running (bit 1), pushes the record on
  a 16-deep "current thread" stack (`0x02a44d18`, depth `0x02a44d10`), and resumes the thread (`0x00e00080(L, argc)`).
  A status above 1 (an error) calls an error handler found through `0x00e0d070(record +8)`. Depending on `0x00e0c610`
  the record is either kept (returns 1) or released by `0x00e0c650` (returns 0). `0x00e0c610` (second follow-up dump)
  returns 0 only when the record's `+0xc` is clear and the thread has neither call information (`0x00e01710`) nor stack
  values, i.e. it **finished**; a **finished or errored** coroutine is released at once (`0x00e0c650` drops the thread
  reference from the parent state's stack, frees the name copy, moves the last table record into the freed slot and
  decrements `0x02a44d58`), while a **yielded** one keeps its record. CONFIRMED — disassembly. So the 256-record cap is
  only approached by callbacks that yield.
- `0x00e0cb20(L)` finds the thread-table record whose `+4` is the Lua state `L` (the calling script's own record),
  skipping dead records (flag `+0x18` bit `0x4`) and records whose thread has finished (`+0xc` clear and
  `0x00e01710`/`lua_gettop` both zero). Null when `L` is not a registered thread. CONFIRMED.
- Argument pushers on a call object (each increments the argument count at `+0x1c`): `0x00e0ce40` number, `0x00e0ce60`
  nil, `0x00e0ce00` boolean (the low byte of its one argument), `0x00e0cfb0` string (nil when null). `0x00e0cd00` runs
  the call through `0x00e0cba0` unless the object's flag `+0x18` bit `0x10` is set. CONFIRMED.

**Wide-string encoder `0x00e22a30(buffer, size, utf16)`** (dumped): writes a header byte `0x80`, then 3 bytes per UTF-16
code unit: a tag byte (1, plus 2 when the low byte is zero, plus 4 when the high byte is zero) followed by the low and
high bytes with any zero byte replaced by `0xff`; it ends with `0x08 0x00`. A null or empty source gives an empty
string; a size of 3 or less writes nothing. This is how UTF-16 text (outfit names, hair-colour names, anim names)
reaches Lua as a NUL-free C string. CONFIRMED — disassembly. HYPOTHESIS: the UI side decodes this back to UTF-16.

**Name hash `0x00d9e8b0`** (dumped; already annotated in the project database): table-driven CRC-32 over a lowercased
C string, result written through a hidden ECX output pointer. CONFIRMED.

**PRNG `0x00dab660(lo, hi)`** (dumped): takes the next dword from a 0x2000-entry table at `0x013214d4` (cursor
`0x013214d0`, wraps to 0) and returns `lo + value mod (hi − lo + 1)`. **There is no guard for an empty range:** with
`hi = lo − 1` the modulus is 0 and the `DIV` faults (integer divide by zero). CONFIRMED — disassembly. Several callers
below can reach that with an empty table.

---

## A. PCU (clothing store / wardrobe) — 10 functions, sub-registrar `0x00830230`

### A.1 `pcu_get_num_items_category` (`0x0082bb50`)

**Arguments:** 1 number = category index (truncated); 2 number = catalog-outfit index, or -1.

**Return:** exactly 1 number on every path. CONFIRMED.

**Body — arg 2 = -1:** looks up the category record (`0x00821fe0`, bounds-checked; null → returns **0**). Otherwise
returns `0x009dada0(category)`: the number of owned-list entries whose item's slot id belongs to that category. The slot
→ category map is `0x00828270(slot)`, a linear scan of the category table (`0x022db978`, count `0x022db97c`, stride
`0x44`) that returns the **first** category whose slot array (`+0x18`, count `+0x1c`) contains the slot, or null.
CONFIRMED. So a slot listed in two categories is only ever counted for the first one (CONFIRMED shape).
`0x009dada0` loads the local-player global into a register and then compares that register with the same global, so
its "local player only" test can never fail (CONFIRMED; harmless).

**Body — arg 2 ≠ -1:** walks the catalog list from `0x022db9b8` with the same numbering as tranche 08 E.3 (a counter
that starts at -1 and is incremented for nodes with flag bit 0 set and bit `0x10` clear; compared as a sign-extended
16-bit value). For every node whose current number equals arg 2 — including, as in E.3, uncounted nodes that follow
the matching one — it walks the node's pieces (`+0x10`, stride `0x18`, count `+0x14`). For each piece: category =
`0x00828270(piece item +0x20)`; **if that category's `+0x2c` is greater than the running result**, the result becomes
`0x009dada0(category)`. The result starts at 0. CONFIRMED — disassembly.
- Arg 1 is **ignored** in this mode (CONFIRMED: the category record is looked up only in the -1 branch).
- What the comparison computes is odd: it compares a category field with an owned-item count, so the result is "the
  owned count of the last piece category (in piece order) whose `+0x2c` exceeded the previous result", not a sum or a
  maximum. HYPOTHESIS: the intent was "largest owned count over the categories this outfit touches" (a per-category
  capacity check); the meaning of category `+0x2c` is OPEN (spec §26.14 reads the same offset as an item count).

**Crash shape:** `0x00828270` returns null for a slot id that is in no category, and the root then reads `+0x2c` of
that null at `0x0082bc32` — **null read, CONFIRMED shape**, reachable only with catalog data whose piece slot is in no
category (HYPOTHESIS: not in shipped data).

### A.2 `pcu_get_items_in_category` (`0x0082fa30`)

**Arguments:** 1 number = category index (truncated); 2 string = name of a Lua callback.

**Return:** 0 values. If arg 2 is null or the category index is out of range, nothing is called. CONFIRMED.

**Body (CONFIRMED unless tagged):** reads the local player and its owned count (`0x009dad80`), constructs a stack hash
object named "temp hash" (`0x00db64a0` on `0x01493a78`, then `0x0082f9a0`) and destroys it at the end (`0x00728fa0`,
`0x00db60b0`). The hash object is never otherwise used in the body (CONFIRMED; HYPOTHESIS: leftover). Then one of
three branches, each calling the callback once per reported element:

1. **Category name is exactly "Outfits"** (case-sensitive inline compare with `0x0115f84c`): walk the catalog list
   `0x022db9b8`. A node is reported when its flag dword `+0x64` is **non-zero** and bit `0x10` is clear; its number is
   a counter starting at 0. Callback arguments (12 values, or 10 — see slots 4-5):
   1. node number;
   2. nil;
   3. node name: node `+0xc` is a UTF-16 string, encoded by `0x00e22a30` into a 256-byte buffer;
   4-5. flag bit 0 set → (true, `0x0080ede0(node +8)`) — HIGH CONFIDENCE the discounted price, per tranche 08 E.11;
      else flag bit 1 set → (true, nil); **else nothing is pushed for these two slots**, so every later argument
      shifts left by two (CONFIRMED — the listing has no else-push);
   6. node `+8` (integer price) × 0.1 (the double at `0x012a2d38`) — HYPOTHESIS: a sale/resale value;
   7. nil; 8. true; 9. flag bit 5 (`0x20`);
   10. "locked": true when the node's content pack fails `0x0045b3a0`, or, if it passes, when the node has a block at
       `+0x6c` and any inline piece of that block (items at block `+0x20 + 0x18·i`, count at block `+0x240`) has a null
       item or an item whose pack fails;
   11. false; 12. node `+0x78` ≠ `0xff` (HIGH CONFIDENCE "is DLC").
   (Numbered as if slots 4-5 were present; without them the call has 10 values.)
   **Numbering mismatch:** this branch numbers nodes on *any* non-zero flag, exactly like tranche 08 E.15
   (`pcu_wear_store_outfit`), while A.1, E.3 and E.11 number on bit 0 only. Player-created outfits carry flag value 2
   (bit 1 only — see A.5), so after the first player-created node the "Outfits" list index and the purchase / count
   index disagree. CONFIRMED (listings).

2. **Other category, item flag `+0x5c` bit 1 set** ("owned instances" mode): for the category's items (circular list
   from category `+0x10`, next at item `+4`, skipping items whose `+0x5c` is 0), loop over the local player's owned
   list by index k; for each owned entry (`0x009dadf0(player, k)`) whose item id (`+0xc`) equals this item's id, call
   the callback with **18 values**:
   1. item index (`0x00822060(item)`, pushed as unsigned);
   2-3. item (string id, literal) pair;
   4. false; 5. 0; 6. nil;
   7. per-item instance number (a counter that counts matches for this item, starting at 0);
   8. `+0x5c` bit `0x10` clear; 9. `+0x5c` bit 5; 10. "locked" = item pack fails `0x0045b3a0`;
   11. the owned entry's byte `+9` (the "new" flag — see A.7);
   12. item `+0x78` ≠ `0xff`;
   13-15. nil, nil, nil;
   16. "currently in the store working set": `0x00821740(player store, owned item's slot)` found an entry whose item id
       equals this item's **and** whose 16-bit `+0x48` instance index equals the **instance number** of item 7 (not the
       owned-list index k);
   17. item `+0x18`; 18. false.
   **Hang:** when the callback object can't be built (`0x00e0ca80` returns null — e.g. the callback name is not a
   defined Lua function), the code jumps to `0x0082ff77`, which does `DEC EBX` and then falls into the loop's
   `INC EBX`, so **the owned-list index does not advance and the same entry is retried forever.** CONFIRMED —
   disassembly (`0x0082fdb0` → `0x0082ff77`/`0x0082ff78` → `0x0082ff81` back to `0x0082fd65`). **Reachability:**
   `0x00e0c720` returns null when the callback name is not a global Lua function (missing, misspelt, a C function) or
   when the thread table already holds 256 records (front matter, "Lua callback helpers"), and nothing inside the loop can change either
   condition, so with at least one owned copy of a bit-1 item in the requested category the game **hangs** in this
   loop. CONFIRMED mechanism; the shipped scripts presumably pass a valid callback (HYPOTHESIS), so the realistic
   trigger is thread-table exhaustion by many yielding coroutines. Modes 1 and 3 simply skip the element instead.

3. **Other category, bit 1 clear** ("variants" mode): only for items whose `+0x60` > 0 and variant count `+0x30` > 0.
   It first **writes the global `0x01300c6c` = item slot id** (`+0x20`) — the "currently previewed slot" global that
   spec §19.16 attributes to the PCU preview functions — as a side effect of a *query*. CONFIRMED. Then for each
   variant v (stride `0x70` from `+0x2c`) whose byte `+0x6c` is 1, it calls the callback with 18 values:
   1. item index; 2-3. item pair; 4. false; 5. nil; 6. nil; 7. **-1**; 8. `+0x5c` bit `0x10` clear; 9. `+0x5c` bit 5;
   10. locked; 11. false; 12. `+0x78` ≠ `0xff`; 13. **v**; 14-15. the variant's (string id, literal) pair when the
   item has more than one variant and item `+0x1c` bit `0x4` is clear, else (nil, nil); 16. nil; 17. item `+0x18`;
   18. false.
   With item `+0x1c` bit `0x4` set the loop stops after variant 0 (reported only if listed). CONFIRMED.

So the 18-value layout is shared by modes 2 and 3; positions 5, 7, 11, 13 and 16 tell them apart (CONFIRMED).

**Crash shape:** local player not null-checked before `+0x2088` (mode 2). Same as all PCU roots (tranche 08 G8).

### A.3 `pcu_get_item_defaults` (`0x0082c7f0`)

**Arguments:** 1 number = item index (table `0x022db9c8`).

**Return:** index out of range → **1 value, -1**. Otherwise **35 values** (CONFIRMED: the push counter starts at 9,
gains 4 per colour group and 2 + 10 + 2 afterwards; the function returns it).

**Body:** the same layout as tranche 08 E.4 (`pcu_get_owned_item_info`) but for the item's **defaults** rather than an
owned instance. In order:
1. item `+0x20` (slot id); 2. arg 1 echoed (unsigned); 3-4. item (string id, literal) pair;
5. variant count `+0x30` when item `+0x1c` bit `0x4` is set, else **1**;
6. **0** (where E.4 reports the owned variant index);
7-8. the (string id, literal) pair of **variant 0** (`+0x2c` → `+4`/`+0xc`);
9. colour-slot count c = min(k, 3, item `+0x58`), where k is the `+8` of the record that variant 0's `+0x1c` points
   at, or 0 when that pointer is null;
10-21. three colour groups: within c → (palette index, R, G, B) from the item's **default** colour pointers
   (`+0x34`, `+0x38`, `+0x3c`); past c → (-1, nil, nil, nil). The palette is `0x00746770(slot)` → `0x00746750`
   (`0x00746770` maps slots 0..23 through the table `0x01152628`, else 0; `0x00746750` returns the base
   `0x015535b8[sel]` and writes the count `0x015535a8[sel]`, **with no bound on `sel`**). The palette index is
   (pointer − base) / `0x30`. R, G and B are the entry's floats `+0xc/+0x10/+0x14` run through `0x00540330` — the
   standard linear → sRGB encode (≤ 0.0031308: ×12.92, else 1.055·x^(1/2.4) − 0.055), ×255, rounded, clamped 0..255 —
   then divided by 255. CONFIRMED;
22-23. count of the item's wear options that pass both `0x00822a60` and `0x00822a30`, then the first passing index (or
   -1). Computed only when a local player exists (the **only** player null check in this function);
24-25. that option's text as (nil, string): the literal when its id is the sentinel, otherwise `0x00849fd0(id)` encoded
   by `0x00e22a30`; (nil, nil) when none passed;
26-33. constants 0, -1, nil, nil, -1, nil, nil, nil;
34. integer price: variant 0's `+0x18` when non-zero, else (int)(variant 0's float `+0x14` × the float global
   `0x022ff5c8`) — HYPOTHESIS: a store price multiplier;
35. `0x0080e740(variant 0 +0x14)` — HIGH CONFIDENCE the discounted price, per tranche 08 E.10.

**Crash shapes:** variant 0 is read through item `+0x2c` with no check (`0x0082c90f`/`0x0082c912`); an item with a null
variant array crashes — CONFIRMED shape, data-gated (HYPOTHESIS: never in shipped data). A default colour pointer that
is null while c > 0 would give a garbage palette index and a read at `null+0xc` inside the sRGB step — CONFIRMED shape,
data-gated.

### A.4 `pcu_delete_outfit` (`0x0082dad0`)

**Arguments:** 1 number = saved-outfit index. **Return:** 0 values.

**Body:** if the index is below the saved-outfit count (`0x009dae20`) and `0x009dae40` returns the entry (it rejects
negative indices and non-local players): take the entry's (record, second dword). If the record is non-null:
1. if record `+0x6c` is non-null (a player-created outfit — see A.5), `0x00821c90(record)` returns it to the pools:
   the `+0x6c` block is unlinked from the in-use block list `0x022d60ec` and appended to the free block list
   `0x022d60f0` (links at block `+0x244`/`+0x248`), and the record itself is unlinked from the catalog list
   `0x022db9b8` and appended to the free-node list `0x022db9b4` (links at `+0x70`/`+0x74`);
2. `0x009db140(player, record, second)` removes the first saved-outfit entry with that exact pair and compacts the
   list with `memmove`, decrementing `0x0262e9f4`.
CONFIRMED — disassembly (all three bodies dumped).

**Notes:** no player null check is needed (the accessors return 0/null for a null player). A catalog (store-bought)
outfit has `+0x6c` = null, so deleting it only forgets the saved entry; the catalog node stays (CONFIRMED). Deleting a
player-created outfit **does not** touch owned items, the worn table or the store working set (CONFIRMED: no other
calls). Whether another saved entry can still point at the freed record (two saved entries sharing one record) is
OPEN — if it can, that entry is left dangling.

### A.5 `pcu_create_outfit` (`0x0082d850`)

**Arguments:** 1 string = a **key** naming the outfit text, not the text itself (see step 1).

**Return:** arg 1 null, or the key has no text → **2 values: -1, "CUSTOMIZE_INTERNAL_ERROR"**. Otherwise **1 number** =
`0x00821c60(new node)` taken as a byte: the node's index in the 130-node outfit-node pool at `0x022fb660` (stride
`0x7c`, ending at `0x022ff558`), or 255 for a pointer outside it (follow-up dump). CONFIRMED.

**Body (CONFIRMED unless tagged):**
1. The Lua string is hashed (`0x00d9e8b0`, case-insensitive CRC) and the hash is looked up by `0x00845170` → jump to
   `0x008436c0` (follow-up dump), which takes the hash through a **hidden EAX pointer**, rejects the sentinel
   `0x029c9964`, and scans a 12-byte-stride table at `0x02317d74` (count `0x02317b64`) for a matching hash, returning
   the UTF-16 string pointer stored next to it, or null. HYPOTHESIS: this is the store of virtual-keyboard results
   (`game_vkeyboard_input`, spec §26.20, sits next to it at `0x00845780`), so a script passes the keyboard-result
   name, not the typed name.
2. A 0x24c-byte outfit **block template** is built on the stack, **not zeroed first**:
   - for each slot 0..23 that passes the "clearable" predicate `0x0082a630` (i.e. **not** 0-4, 21 or 22) and has a
     store-working-set entry (`0x00821740`), a 24-byte piece is written at template `+0x20 + 0x18·n`: dword 0 = the
     entry's item, dwords 2-5 = entry `+8`, `+0x18`, `+0x1c`, `+0x20` (three colours). **Dword 1 is never written**,
     so it keeps whatever was on the stack (CONFIRMED). The entry's variant (`+0x10`) goes to a parallel array at
     template `+0x1f4 + 4·n`, and n is stored at template `+0x240`;
   - capacity: (0x1f4 − 0x20) / 0x18 = 19 pieces and 19 variants, against at most 17 clearable slots, so the copy
     cannot overflow (CONFIRMED arithmetic);
   - name at template `+0`: an empty converted name is replaced by the localized "OUTFIT_NAME_TAG" (`0x0084a1b0`);
     otherwise `0x0084a030(buf, 16, name, 1)` filters it character by character through `0x00849ae0` (only when the
     globals `0x0242dbcc`/`0x0242dbc8` are set; else a plain copy) and it is copied with `wcsncpy(…, 16)`. Character
     15 is then forced to 0, so names keep at most 15 UTF-16 units.
3. `0x00821ae0(template)` allocates: it takes the first block from the free list `0x022d60f0` and the first node from
   the free list `0x022db9b4`; **if either list is empty it returns null.** Otherwise it copies the 0x24c bytes into
   the block (restoring the block's own list links), moves the block to the in-use list `0x022d60ec`, appends the node
   to the **catalog list `0x022db9b8`**, and fills the node: `+0` = 0, `+0xc` = block (the name), `+0x10` = block
   `+0x20` (pieces), `+0x14` = piece count, `+0x18` = block `+0x1e8` (with block `+0x1e8` = 0 and `+0x1ec` = block
   `+0x1f4`, the variant array), `+0x1c` = 1, `+0x20` = 0, `+0x60` = 0, `+0x6c` = block, `+0x78` = `0xff`.
4. The root sets node `+0x64` = **2** (flag bit 1 only) and calls `0x009dafd0(player, node, node+0x18, 0, 1)` — the
   saved-outfit appender (follow-up dump): it appends (node, node+0x18) when the player is local and fewer than 128
   outfits are saved; its 4th argument 0 means **no pieces are added as owned items** (tranche 08 E.11 passes 1), and
   the 5th argument is only used for owned entries, so it has no effect here. Its result is **ignored**.

So **player-created outfits live in the same catalog list as store outfits**, distinguished by flag value 2 and by a
non-null `+0x6c` block (CONFIRMED). This is why A.2's "Outfits" branch and tranche 08 E.15 see them and E.3/E.11 do
not.

**Crash and logic shapes:**
- **Null write when the pools are empty:** `0x00821ae0`'s result is used at once — `+0x18` read at `0x0082da49` and
  `+0x64` written at `0x0082da52` — with no check. **CONFIRMED — disassembly.** `pcu_can_create_outfit` (A.9) only
  tests the block list, not the node list, so the guard a script is expected to call first does not cover this.
- With 128 outfits already saved, `0x009dafd0` refuses (tranche 08 E.0) but the node has already been taken from the
  pool and linked into the catalog list: an **orphan catalog node** that no saved entry refers to and nothing frees.
  CONFIRMED shape (result ignored).
- The uninitialized dword 1 of every piece is copied into the block (see step 2). Tranche 08 reads owned-entry `+4` as
  the variant record; whether anything reads piece dword 1 of a created outfit is OPEN.
- Local player not null-checked before `+0x2088` (`0x0082d927`). CONFIRMED.

### A.6 `pcu_clear_obscured_slots` (`0x0082e7e0`)

**Arguments:** none. **Return:** 0 values.

**Body:** `0x0082e2d0(-1)` (push the whole worn/preview table back into the store working set — see the note below),
then two queued jobs on the job queue at `0x0264b720` (guarded by the lock pair `0x00d9f620`/`0x00d9f630` on
`0x0264c940`): `0x009f3590(store)` queues **kind 6** and `0x009f35f0(store)` queues **kind 7**, each with the player's
store object as payload. The slot comes from `0x0088bc80` (follow-up dump), which pops a free-list pool (free count =
the 16-bit field at queue `+0xe`, i.e. the word at `0x0264b72e` that both callers test first), so a **full queue
silently drops the job**.
CONFIRMED — disassembly. The name suggests the jobs recompute which slots are hidden by other garments; what kinds 6
and 7 do is OPEN (their consumer was not traced). Local player read twice without a null check (CONFIRMED).

**`0x0082e2d0(slot or -1)` (dumped this tranche)** is the reverse of tranche 08's `0x0082e210`: it applies the worn
table **to** the store working set. With -1 it first copies player `+0x20f8` into store `+0x1c`. It then makes up to 10
passes; each pass visits slot 0x13 first and then slots 1..23 (slot 0 is never visited), skipping slots already done
(a 3-byte bitset) and, when a slot is given, all other slots. For a visited slot: if the worn entry's item is empty, any
store entries for the slot are removed (`0x00821630`); otherwise the item is resolved (`0x008231f0`) and applied
(`0x00825010`), with slot 1 also getting its hair colours from `0x00831a50`/`0x00831c60` and colours re-applied with
`0x00824950`. A failed slot is retried on the next pass; after 10 passes the last failed slot's worn entry is zeroed.
It ends with `0x0082dc40(0x022ffd40, 0x022ffd44, 1)`. It null-checks the local player. CONFIRMED.

### A.7 `pcu_clear_item_new` (`0x0082bf40`)

**Arguments:** 1 number = item index; 2 number = instance number. **Return:** 0 values.

**Body:** `0x009daef0(player, 0x00822040(arg 1), 0, arg 2)`. For the local player only, it walks the owned list and
counts entries whose item equals the given item (the variant filter is 0 = any), starting the counter at -1; when the
counter equals arg 2 it clears that entry's byte `+9` and stops. CONFIRMED (the body continues in the fragment
`0x009daeff`, dumped in the follow-up). So owned byte `+9` is the **"new item" flag**, the same byte A.2 reports as
argument 11.

**Logic shapes (CONFIRMED):** the "counter equals arg 2" test runs for every non-empty owned entry, not only matching
ones, so **arg 2 = -1 clears the flag on the first non-empty owned entry whatever its item**, and an out-of-range item
index (null item) with arg 2 = -1 does the same. Otherwise an unknown item or instance is a silent no-op.

### A.8 `pcu_category_nav_clear_slot` (`0x0082f440`)

**Arguments:** 1 number, truncated and then **masked to 16 bits** (`MOVZX … AX`). **Return:** 0 values.

**Body:** the value indexes the descriptor-pointer array `0x022ffcf0` (spec §19.1/§26.14/§26.15: count at `0x022ffd30`,
so at most 16 entries) **with no bound check and no null check**, and the descriptor's name (`+0`) is compared with
"Piercings" (exact, case-sensitive).
- **Match:** copy worn-table entry 5 (`0x022ff74c`, 0x4c bytes) to the backup `0x022ffd68`, zero entry 5, set the flag
  `0x022ffd64` = 1.
- **No match:** if `0x022ffd64` is set, copy the backup back into entry 5 and clear the flag.
- Both paths then call `0x0082e2d0(-1)` (apply the worn table to the store, A.6) and set `0x01300c6c` = -1.
CONFIRMED — disassembly. So while the player browses "Piercings", whatever is worn in slot 5 is taken off (HYPOTHESIS:
slot 5 is headwear that would hide piercings) and it is put back when they browse anything else.

**Crash shape:** an index ≥ 16 reads past the array into the following globals (`0x022ffd30…`) and dereferences
whatever is there; -1 becomes 0xffff and reads `0x022ffcf0 + 0x3fffc`. **Out-of-bounds read followed by a pointer
dereference, CONFIRMED shape.** A valid index whose slot is still null (fewer descriptors loaded) is a **null read**.
The other readers of this array bound-check against `0x022ffd30` (spec §19.1, §26.14); this one does not.

**Logic shape:** the backup is single-level. Navigating "Piercings" → "Piercings" overwrites the backup with the
already-zeroed entry; the original slot-5 item is then lost (CONFIRMED: the match path copies unconditionally, without
testing `0x022ffd64`).

### A.9 `pcu_can_create_outfit` (`0x0082ae00`)

**Arguments:** none read. **Return:**
- **3 values: false, "CUSTOMIZE_OUTFIT_YOURE_NAKED_TITLE", "CUSTOMIZE_OUTFIT_YOURE_NAKED_BODY"** when `0x00821300(player)`
  is true: a local player exists and **no** store-working-set entry is in a clearable slot (not 0-4, 21, 22);
- **3 values: false, "CANNOT_CREATE_OUTFIT_TITLE", "CANNOT_CREATE_OUTFIT_BODY"** when the free block list
  `0x022d60f0` is empty (`0x008212f0`);
- otherwise **1 value: true**.
CONFIRMED — disassembly.

**Gaps (CONFIRMED):** it does not check the free-node list `0x022db9b4` (A.5's allocator needs both) and does not
check the 128-entry saved-outfit cap, so true does not guarantee A.5 succeeds. With no local player the "naked" test
is false and the result depends only on the pool. A.5 itself never repeats the naked test, so an outfit with 0 pieces
can be created by skipping this guard.

### A.10 `pcu_bra_required` (`0x0082ad10`)

**Arguments:** none read. **Return:** exactly 1 boolean = local player byte `+0xa41` == 1 (`0x009db0d0`). CONFIRMED.

`+0xa41` is the gender byte (spec §16.8, 0 = male, 1 = female), so this is "the player is female". The value 3 (the
constructor's "unspecified" sentinel, spec §16.8) gives false. **No player null check** — a null local player is read
at `+0xa41` (CONFIRMED shape).

---

## B. PCR (character creator) — 15 functions, sub-registrar `0x008377d0`

### B.1 `pcr_report_skin_colors` (`0x00832570`)

**Arguments:** 1 string = callback name. **Return:** 0 values (nothing happens when arg 1 is null).

**Body:** hash "ui_menu_pcr_skin_00" (`0x00d9e8b0`) as a default; find the calling script's thread record
(`0x00e0cb20(L)`); read the current skin pointer from local player `+0x208c` (**no player null check**). For each skin
entry i (table `0x0230084c`, stride `0x38`, count `0x02300850`, unsigned compare) it creates **a new Lua thread** for
the callback with `0x00e0c8f0` (front matter, "Lua callback helpers") and, if that succeeds, passes:
1. i;
2. entry `+0xc` (an image-name hash) — or, when it equals the sentinel `0x029c9964`, the hash of
   "ui_menu_pcr_skin_00";
3-4. the name pair from entry `+8`/`+4`, using a **different sentinel, `0x029c9960`**: equal → (nil, literal `+4`),
   else (id, nil);
5. entry address == player `+0x208c` (the applied skin; compare spec §19.15, which writes `+0x208c`).
CONFIRMED — disassembly.

**Difference from every other report function in this tranche (CONFIRMED):** all of them run each callback as a new
coroutine (front matter, "Lua callback helpers"), but this one takes the Lua state from **the calling script's own thread record**
(`0x00e0cb20(L)` → `0x00e0c8f0`) instead of the global script state `0x00e1a1b0`. So it reports **nothing at all** when
it is called from a state that is not a registered script thread, and its callbacks inherit the caller's record
`+0x14`. A callback that yields keeps one of the 256 thread records until it finishes.

### B.2 `pcr_report_hair_items` (`0x00832cc0`)

**Arguments:** 1 number = hair category 0..2 (truncated); 2 string = callback name.

**Return:** 0 values.

**Body:** maps arg 1 through the 3-entry table `0x01300cf4` = {1, 2, 3} (slot ids) **with no range check** — unlike
`pcr_change_hair` (spec §16.6, checks < 3) and `pcr_get_hair_info` (§23.6). Finds the current store entry for that slot
(`0x00821740`; local player not null-checked). Then for each hair style i (array `0x02300860`, count `0x02300864`):
- skip if the item's pack fails `0x0045b3a0`;
- `0x009fd5b0(first wear option +0xc, first variant +0x10)` — a linear search of two tables of 0x54-byte records
  (`0x02677198` × 1000, then `0x02675f38` × 56) for a record whose first dword equals the **sum** of the two values;
  skip unless that record's `+0x2c` equals the mapped slot;
- `0x00db60a0(item)` must return 1 — it is a two-instruction stub that **always returns 1** (CONFIRMED);
- callback (`0x00e0ca80`, with call-object `+0x14` cleared to 0) with: (i, item string-id/literal pair, "worn" =
  store entry found and its item id equals this item's id).
CONFIRMED — disassembly.

**Crash shapes (CONFIRMED shape):** `0x009fd5b0` returns null when no record matches, and its `+0x2c` is read at once
(`0x00832d6b`) — **null read**; item `+0x24` and `+0x2c` (first wear option, first variant) are dereferenced without
checks; arg 1 outside 0..2 reads `0x01300cf4[arg]` out of bounds (a stray slot id for small values, a fault for huge
ones). Arg 2 is passed to `0x00e0ca80` without a null check; a nil name makes `0x00e0c720` return null, so nothing is called (front matter).

### B.3 `pcr_report_hair_colors` (`0x00833420`)

**Arguments:** 1 string = callback name. **Return:** 0 values.

**Body:** the "current" colour is (dword at `0x022fff48 + s·0x80` − `0x02300868`) / `0x48`, where s is the store index
(store `+0x14`). That dword is colour-1 of **entry 0** of the per-store hair-colour table written by `0x00831ad0` (B.9),
whatever slot entry 0 belongs to (CONFIRMED). For each colour i (table `0x02300868`, stride `0x48`, count
`0x0230086c`): callback (`0x00e0ca80`) with (i, entry `+0x10` as unsigned, nil, entry `+4` UTF-16 name encoded by
`0x00e22a30`, i == current, entry `+0xc` as a C string or nil). CONFIRMED.

Local player not null-checked; an empty colour table for that store gives a negative "current" and nothing is flagged
(CONFIRMED arithmetic).

### B.4 `pcr_report_eye_colors` (`0x00830da0`)

**Arguments:** 1 string = callback name. **Return:** 0 values.

**Body:** the current eye definition is the first item of the character's 63-slot composite list
(`0x026533ac`/`0x026533b0` at stride `0x270` by store index) whose definition category (`0x0265430c + def·0x44`) is
**1**; -1 if none. Then for each composite definition d (`0x026542e8`, count `0x0268d13c`) with category 1 whose pack
byte `+0x40` passes `0x0045b3a0`, callback (`0x00e0ca80`) with:
1. **d — the global definition index**, not an index among eye colours;
2. `0x00d9e740` hash of `"ui_menu_pcr_%s"` formatted with the definition name (`0x00da78d0` into a 64-byte buffer);
3-4. the definition's id `+8`: non-zero → (id, nil); **zero** → (nil, name). Note the sentinel here is **0**, not
   `0x029c9964`;
5. d == current.
CONFIRMED — disassembly. Category 1 = "eyes" per spec §16.7's 34-name table (HIGH CONFIDENCE). Local player not
null-checked.

### B.5 `pcr_report_anims` (`0x00834bf0`)

**Arguments:** 1 string = style category name; 2 string = callback name. **Return:** 0 values.

**Body:** returns at once if either string is null, if the category doesn't resolve, or if there is **no local player**
(this one null-checks). Arg 1 is passed to `0x008307f0` in **EDI with no stack push** — the same hidden-register
convention spec §19.14/§23.5 document for this callee (CONFIRMED again here at `0x00834c4e`/`0x00834c50`).
- **0 ("melee_combat_style") → no-op**, as in §19.14.
- **1 ("taunt_style"):** clear the picked-flag bytes `0x02300920[0..n)` (n = `0x023008a4`); then n times take the next
  entry from the picker `0x00833bf0(0)`; for entries whose byte `+0xc` in the 16-byte table `0x0230089c` is 0, call
  back with (name: table `+8` UTF-16 encoded by `0x00e22a30` into 128 bytes, the entry index, index == player byte
  `+0x28b4`).
- **2 ("compliment_style"):** the same with `0x023008a8`, `0x02300960`, picker `0x00833ca0`, table `0x023008a0` and
  player byte `+0x28b5`.
- **3 ("facial_expression_style"):** first one call with ("FACIAL_EXPRESSION_BLANK", **-1**, player `+0xd28` ==
  `0x102`); then clear the 15 picked bytes `0x0230090c…0x0230091a`; then 15 times take the picker `0x00833b20(0)` and
  call back with (the literal from the 15-entry table `0x01300d54` copied with `strncpy(…, 0x80)` — **plain ASCII, not
  `0x00e22a30`-encoded** — the index e, e + `0xf3` == player `+0xd28`).
CONFIRMED — disassembly.

**Pickers (CONFIRMED):** `0x00833ca0`, `0x00833b20` and `0x00833bf0` (the last one in the follow-up dump) are selection
sorts: each call returns the not-yet-picked entry whose name
compares smallest (an ordinal UTF-16 compare; for facial expressions the names are first localized with `0x0084a1b0`)
and marks it picked. So **the callbacks arrive in alphabetical order of the displayed name**, not table order, and
each list costs O(n²) string compares. Passing a non-zero argument instead resets the picked flags.

**Consistency with §19.14:** BLANK is reported with `+0xd28` value `0x102` = `0xf3` + 15, i.e. BLANK is facial index
15 and the 15 table entries are 0..14 (CONFIRMED arithmetic; §19.14's setter accepts arg 2 < `0xf`). The BLANK row
reports index -1, so a script that feeds the reported index back to `pcr_set_anim` cannot select BLANK through it
(HYPOTHESIS: the script special-cases it).

**Latent shapes (data-gated):** loop counters are bytes compared against dword counts, so counts over 255 never end;
the clear loops write `count` bytes from the flag arrays with a sign-extended byte index and no bound (the taunt flags
have 0x40 bytes before the compliment flags begin). `strncpy` with 0x80 does not terminate a 128-character name.
CONFIRMED shapes; shipped counts are small (HYPOTHESIS).

### B.6 `pcr_randomize_eye_colors` (`0x00835bc0`)

**Arguments:** none read. **Return:** 0 values.

**Body:** collect every composite definition with category 1 whose pack passes into a **600-entry stack array**, pick
one with `0x00dab660(0, n − 1)` (only when n > 0, so no divide-by-zero here), and apply it with
`0x009f9f60(s, def, &{0xff, 0xff, 0xff, 0xff}, -1)`. CONFIRMED.

`0x009f9f60(s, def, tint, variant)` (dumped): after the pack check it looks for `def` in the character's 63-slot list;
if present it overwrites the item's tint (with `*tint`, or `0xffffffff` when the pointer is null) and its variant byte;
otherwise it counts existing items of the same category against the per-category limit `0x0130b6a0[category]` and
either replaces one or appends (only while the list holds fewer than 63). CONFIRMED (partial body; the replace branch
at `0x009fa057` was not followed).

**Latent overflow:** the 600-entry array is filled with **no bound check**; more than 600 eye definitions would
overwrite the stack. The definition table can hold far more (its span before `0x0268d13c` fits about 3400 records).
CONFIRMED unchecked; data-gated (HYPOTHESIS: shipped data has a few dozen). Local player not null-checked.

### B.7 `pcr_purchase_tattoo` (`0x008333a0`)

**Arguments:** 1 number = price in dollars (no presence check; missing → 0). **Return:** 0 values.

**Body:** cents = round-half-away-from-zero(−price × 100.0) (`0x00dad900`), clamped to ±2·10⁹ (`0x00960160`); then
`0x0094d920(&cents, 0xb)` with the **local player in ECX** (a `this` call — the decompile hides it), which is the
"cash_adjust" path with reason `0xb` that tranche 08 E.10 identified; then the tracker `0x006cef30(price)` with the
original float. CONFIRMED — disassembly.

**Findings (CONFIRMED):** nothing tattoo-specific happens — no item, no composite, no stat `0x46`, no notification; it
only charges. There is no sign check (a negative price **pays the player**), no affordability check, and no player
null check before the `this` call. HYPOTHESIS: the tattoo itself is applied by the composite functions
(`pcr_change_composite`, §16.7) and this only bills it.

### B.8 `pcr_lips_randomize` (`0x00834a00`)

**Arguments:** none read. **Return:** 0 values.

**Body:**
1. Build a **64-entry stack array**: entry 0 = -1 ("no lips"), then every composite definition with category **0**
   (lips) whose pack passes — **no bound check** against 64.
2. r = `0x00dab660(0, count)` — the range includes entry 0.
3. r picked "no lips" → `0x00821630(store, 4, 1)` removes the PCU store entries of **slot 4**, and returns. This is the
   same lips = PCU slot 4 link spec §19.5/§23.3/§23.7 found (CONFIRMED again).
4. Otherwise the definition's name (`+0`) is looked up as a PCU item (`0x008282c0`: a string-table lookup
   `0x00db0c50` → item record, **null when the name is unknown**), and the item's **variant** named "Standard_2" is
   found by `0x008282f0` (a case-insensitive `_stricmp` over the variant array `+0x2c`, stride `0x70`, name at `+0xc`;
   null when absent). Both go to `0x00825010(store, item, item +0x24, variant, 0, 0)`. (Follow-up dump.)
5. Colour: palette 2 (`0x00746750(&count, 2)`), k = `0x00dab660(0, count − 1)`; if the store entry for the item's
   slot exists and k is in range, the entry's floats `+0x18/+0x1c/+0x20` × 255 go through `0x00832230` (the
   hidden-EAX colour helper of spec §16.7) and are written as material parameter "Diffuse_Color" of "Standard_2"
   (`0x009f1f40(store, entry component +0x14, hash, hash, rgb)` — follow-up dump: it finds the component in the store's
   circular component list and, when the component has a material, calls `0x007539f0(material, hash "Standard_2", hash
   "Diffuse_Color", {r, g, b, 1.0})`); the store entry's `+0x18` becomes the palette entry.
CONFIRMED — disassembly.

**Crash shapes (CONFIRMED shape):**
- **Stack overflow:** more than 63 lips definitions overrun the 64-entry array (data-gated).
- **Divide by zero:** if palette 2 is empty, `0x00dab660(0, -1)` divides by zero at `0x00dab694`. The range test that
  follows comes too late.
- **Null read:** `0x008282c0`'s result is used at once (`+0x24` at `0x00834abf`); a lips definition whose name is not a
  PCU item crashes.
- Local player not null-checked.

### B.9 `pcr_hair_randomize_all` (`0x00837450`)

**Arguments:** none read. **Return:** 0 values.

**Body:**
1. Gate: if `0x009f0aa0(store, -1)` is true — the per-store dword at `0x0263df00 + s·0x124` is > 0 — do nothing
   (HYPOTHESIS: appearance assets are still loading; meaning OPEN).
2. c1 = `0x00dab660(0, colours − 1)` and c2 = the same (colours = `0x0230086c`) — **once**, shared by all three
   categories.
3. For each hair category i = 0..2 (slot `0x01300cf4[i]`): build a 64-entry list (entry 0 = -1 "none"; **bounded at
   64** here) of hair styles whose pack passes and whose item slot equals the category's slot (plus the always-true
   `0x00db60a0`); r = `0x00dab660(0, n)`; call `0x00836b10` with **ECX = i and EAX = list[r]** (hidden registers, as
   spec §16.6 documents) and stack arguments (c1, c2); then call `0x00831ad0(store, slot, &colour[c1], &colour[c2])`.
CONFIRMED — disassembly.

`0x00831ad0(store, slot, colour1, colour2)` (dumped): ignores null store/colours; in the per-store table at
`0x022fff40 + s·0x80` (16-byte entries {store, slot, colour1, colour2}, count `0x022fff38[s]`, **capacity 8**) it
updates the entry for that slot or appends one. CONFIRMED.

**Bug (CONFIRMED read; intent HYPOTHESIS):** the slot passed to `0x00831ad0` in step 3 is `0x02300860[i] +0x20` — the
slot of **hair-style array entry i** (i = 0, 1, 2) — not the category's slot `0x01300cf4[i]` (which is still in EBP at
that point). Unless the first three styles happen to sit in slots 1, 2 and 3, colours are recorded under the wrong
slot. `0x00836b10` itself also calls `0x00831ad0` (its caller list includes `0x00836c73`), so the correct slot is
probably set there and this extra call is redundant at best.

**Crash shape:** an empty hair-colour table (`0x0230086c` = 0) makes step 2 divide by zero (CONFIRMED shape). Local
player not null-checked.

### B.10 `pcr_get_voice` (`0x00832e60`)

**Arguments:** none. **Return:** no local player → **0 values**; otherwise **1 number** = store `+0x10`, the cached
voice index that `pcr_set_voice` (spec §23.1) writes. CONFIRMED. (Store pointer itself is not checked.)

### B.11 `pcr_get_triangle_cursor_pos` (`0x008329a0`)

**Arguments:** none. **Return:** exactly **2 numbers** = local player `+0x2798` and `+0x279c`, each read as a
**signed 32-bit integer** (`CVTSI2SD`). CONFIRMED. No player null check (CONFIRMED shape).

Consistency with the setter (follow-up dump of `0x008329f0`, spec §19.3): the setter **truncates** each number to an
integer with `0x00ea2596` before storing, so the pair round-trips as integers and fractional cursor positions are lost
(CONFIRMED). §19.3 says the values are written "directly"; the truncation should be added there. The setter has no
player null check either.

### B.12 `pcr_creation_complete` (`0x008335c0`)

**Arguments:** none. **Return:** 0 values.

**Body (CONFIRMED — disassembly):** a co-op "both players finished the creator" handshake on three globals.
- **Client** (a session exists and its `+0x5c` ≠ `+0x58`): set `0x022ccc51` = 1. "Ready" if `0x022ccc50` is already
  set; otherwise ready iff `0x008b7100()` returns 0.
- **Host or no session:** set `0x022ccc50` = 1. Ready if `0x022ccc51` is already set; otherwise ready iff
  `0x008b7100()` returns 0.
- "Host must wait" = a session exists, it is the host, and `0x008b6d20()` is true.
- If not ready, or the host must wait, open a dialog with `0x007c3e60(title, body, callback, cancelText, 1)`: title
  "COOP_TITLE", body "COOP_WAITING_ON_PLAYER", callback `0x00832360`, cancel-line text from the key at `0x0113a178`
  (the listing shows "@COMPLETION_COOP_DISCONNECT"), all localized by `0x0084a1b0`. The dialog's `+0x12c` is stored in
  `0x022ccc54`.

Callees (follow-up dump, CONFIRMED):
- `0x008b7100()` returns the first player in the session's circular player list (head session `+0x54`, next at player
  `+0xb28c`) that is **not** session `+0x5c` — i.e. "a remote player exists" — or 0 with no session. So in single
  player "ready" is always true.
- `0x008b6d20()` returns the byte at `0x024e97d2` (HYPOTHESIS: "the remote player is still in the creator", set by a
  network message; writer OPEN).
- `0x007c3e60` builds the dialog through `0x007c3310(title, 3, 0, 1)`, sets the body, stores the callback at dialog
  `+0x13c`, and with its last argument set appends a "[B button or ESC] + cancel text" line and sets bit `0x200` of
  dialog `+0x120`. It **returns the dialog in EAX** (the decompile shows `void`), which the root reads at `+0x12c`
  without a null check. `0x007c3310` (third follow-up dump) takes the dialog object from the free list `0x02282d14`
  and **returns null when that list is empty**; `0x007c3e60` then calls `0x007c1aa0` with a null `this` and the root
  reads `null+0x12c` — **null dereference when the dialog pool is exhausted**, CONFIRMED shape.
- `0x00832360(dialog, choice)`: for any choice other than -1 it opens a second confirmation dialog
  ("MENU_TITLE_WARNING" / "DIALOG_PAUSE_DISCONNECT_PROMPT", callback `0x008322f0`) via `0x007c3de0`, sets its `+0x118`
  = 1 and stores its `+0x12c` in `0x022ccc54` (or 0 when it could not be created — this one **is** null-checked).

Notes: the two flags are only ever **set** here (CONFIRMED for this function; their reset path is OPEN). So in a
session the dialog appears when this side finishes first, and the host may show it even after both flags are set
while `0x024e97d2` is non-zero (CONFIRMED logic).

### B.13 `pcr_cleanup_player_voice` (`0x00830a30`)

**Arguments:** none. **Return:** 0 values. **Body:** if a local player exists, `0x0070cb80(player)`. CONFIRMED.

Callees (follow-up dumps, CONFIRMED): `0x0070cb80` and its sibling `0x0070cb60` (the call `pcr_set_voice`, spec §23.1,
uses to open the "Phone_Call" cue) both read the player's voice-slot index (`0x00941c50` = player `+0x1c70`) and
return 0 when it is 0. `0x0070cb60` → `0x0070c850(slot, cue)`, which finds the cue in a cue list and stores it at
voice-slot record `+0x18` (records at `0x01504088`, stride `0x28`, 110 slots, 1-based, only when the record's `+0x20`
byte is set). `0x0070cb80` → `0x0070c9c0(slot)`, which **clears** record `+0x18`, calls `0x0046a160(record +8, 0)`,
and, when a session exists and this machine is the host, sends a replicated message (`0x0086f5f0(0x45)` … sub-type
`0x14`, `0x0086f1b0`, `0x0086eb20`). So this is the "close" of `pcr_set_voice`'s phone-call cue override, replicated by
the host. HYPOTHESIS: the record is the character's voice emitter and `0x0046a160` sets its active cue.

### B.14 `pcr_apply_regional_preset` (`0x00830600`)

**Arguments:** 1 string; 2 string (no null checks here). **Return:** 0 values.

**Body:** r = `0x00838cc0(arg 1)`; if r ≠ -1, p = `0x00838d10(r, arg 2)`; if p ≠ -1, `0x00838850(player, r, p)`.
CONFIRMED. `0x00838cc0`/`0x00838d10` are the same resolvers spec §23.8 shows `pcr_apply_preset` using for its optional
arguments 2 and 3.

Callees (follow-up dumps, CONFIRMED):
- A "regional preset" table rooted at the pointer `0x023009c0`, valid only while the byte `0x023009c4` is set
  (otherwise both resolvers return -1): dword 0 = region count, dword 1 = region array (16-byte records: `+0` name hash,
  `+8` preset count, `+0xc` preset array). Preset records are 20 bytes: `+0` name hash, `+8` a value, `+0xc` pair count,
  `+0x10` array of 8-byte (morph, value) pairs.
- `0x00838cc0(name)` / `0x00838d10(region, name)` hash the name with the case-insensitive CRC `0x00d9e8b0` and return
  the first record with that hash, or -1. `0x00838d10` does not bound-check `region` (its only caller here passes a
  valid one).
- `0x00838850(player, region, preset)` bound-checks both indices, writes the preset's `+8` into **player `+0x2090`**
  unless it is -1, then calls `0x00834860(player, morph, value)` for every pair.
- `0x00834860(player, morph, value)` (second follow-up dump) applies the morph through `0x009f59b0(store, morph,
  value)`, calls `0x00831e90(player)` and `0x00d9e140(4000)`, and special-cases the morph named **"body gender"**: when
  value > 0.5, the toggle `0x02300848` is 1 and player `+0xa41` is 0, it removes the store entries of slot 10 and
  clears the toggle; when value ≤ 0.5 and the toggle is 0, it calls `0x00827f90(player, &flag)` and, on success, sets
  the toggle. HYPOTHESIS: this swaps the slot-10 base garment when the body-gender morph crosses 0.5.

**Crash shape:** `0x00838850` writes player `+0x2090` and `0x00834860` reads player `+0x2088` with **no null check**, so
calling this with no local player and a valid region/preset pair crashes (CONFIRMED shape). Arg strings may be nil:
the CRC helper yields 0 for a null string, so the lookup fails unless some record's hash happens to be 0 (CONFIRMED
helper behaviour). The meaning of player
`+0x2090` is OPEN.

### B.15 `pcr_add_default_clothing` (`0x008306a0`)

**Arguments:** none. **Return:** 0 values.

**Body (CONFIRMED):** `0x009f1ca0()`; `0x009f0ce0(0)` (clears byte `0x0263f9dd`, tranche 08 E.14); `0x0082e210(10, 0)`
(refresh worn slot 10 from the store); `0x00827960(player, 0)`; `0x0082e2d0(10)` (apply worn slot 10 back to the
store, A.6); `0x0082e210(-1, 0)` (refresh every worn slot); `0x009f0cf0(player store)`.

Compare `pcr_restore_clothing` (§23.2): the same frame (`0x009f1ca0`, `0x009f0ce0(0)`, …, `0x009f0cf0`), but this one
works on **slot 10** and calls `0x00827960(player, 0)` in the middle. Slot 10 is one of the three slots (0, 10, 11)
that bulk clears exempt (tranche 08 E.0).

**`0x00827960(player, flag)` (follow-up dumps; spec §16.8a only says it "refreshes the body"):**
- **flag 0 (this function):** first `0x009dae70(player)`, which — for the local player — sets the owned-item count
  `0x0262edf8` to **0**, i.e. **empties the player's whole owned-clothing inventory** (third follow-up dump, CONFIRMED:
  a compare with `0x0262edfc` and one store of 0). It then walks the default-garment table (pointer `0x022d60f4`,
  count `0x022d60f8`, stride `0x50`): entries whose gender byte (`+0x4c`; 3 = any) or ethnicity byte (`+0x4d`) don't
  match player `+0xa41`/`+0xa40` have their store entries removed when they are what the store currently holds; entries
  that match exactly (gender ≠ 3, gender and ethnicity equal) are then applied (the add path continues past the dumped
  instruction budget — HIGH CONFIDENCE that it re-adds them, given the "Could not find player component information"
  error text it formats on failure).
- **flag 1** (used by `pcr_apply_preset` §23.8 and `pcr_set_identity` §16.8a): instead of emptying the inventory it
  calls `0x00822930(player)`, which removes every store entry whose slot passes `0x00828420` (null-checks the player).
  The slot predicate also gates the two table loops, in opposite senses: with flag 0 a default garment whose slot
  **passes** `0x00828420` is skipped; with flag 1 it is processed only if it passes, except slots 0, 10, 11, 21, 22 and
  23 (the remove loop) or 10, 11, 21, 22, 23 (the add loop), which bypass the test and are always processed.
CONFIRMED structure; the meaning of `0x00828420` is OPEN.

**So `pcr_add_default_clothing` resets the wardrobe:** every owned garment is forgotten and only the default garments
for the player's gender/ethnicity are put back. In the character creator for a new character this is the intent; if a
script calls it on an established character (for example from the plastic surgeon, which shares this `pcr_` table), the
owned clothing is lost. CONFIRMED mechanism; whether any shipped script calls it outside new-character creation is OPEN
(2 call sites in 2 scripts per Team B).

**Crash shape:** the final `player +0x2088` read has no null check (`0x008306e9`); `0x009dae70` and the first loop of
`0x00827960` also read the player unguarded. CONFIRMED.

---

## C. Method notes for the follow-up dumps

Three depth-0 `func` runs on the private copy (cited above as "follow-up dump"; the second and third as "second /
third follow-up dump"): 32 callees (`0x00e0c720`, `0x00e0cba0`, `0x009daeff`, `0x00821c60`, `0x009dafd0`, `0x009dadf0`,
`0x00822060`, `0x0080ede0`, `0x0080e740`, `0x00849fd0`, `0x008436c0`, `0x0088bc80`, `0x00830230`, `0x008329f0`,
`0x00833bf0`, `0x0070cb80`, `0x0070cb60`, `0x00838cc0`, `0x00838d10`, `0x00838850`, `0x008b7100`, `0x008b6d20`,
`0x00832360`, `0x007c3e60`, `0x00827960`, `0x009f1ca0`, `0x009f0cf0`, `0x00d9e740`, `0x008282c0`, `0x008282f0`,
`0x009f1f40`, `0x00821630` — the last produced an empty file and was not needed, tranche 08 covers it); then
`0x00e0c610`, `0x00e0c650`, `0x00834860`, `0x0070c9c0`, `0x0070c850`, `0x00941c50`; then `0x009dae70`, `0x00822930`,
`0x007c3310`.

Facts from these dumps that the sections above rely on but don't spell out (all CONFIRMED):
- **Registrar `0x00830230`:** builds a 44-row (name, function) table on the stack and loops 44 times
  (`0x008305bd` `MOV EBX,0x2c`) over `lua_pushcclosure(L, fn, 0)` (`0x00dfe4f0`) then `lua_setfield(L, globals,
  name)` (`0x00dfe830`). The "second use" of `pcu_bra_required` and the extra DATA references at `0x008305c7` are the
  dumper annotating that loop's first iterations. So the PCU family has **44** Lua names.
- **Discount formula** used for catalog prices (`0x0080ede0`, integer input) and variant prices (`0x0080e740`, float
  input), identical bodies: with d1 = float `0x022cd0ec` and d2 = float `0x022cd0f0`: if d1 > 1.0 the price becomes
  (1 − d2)·(price − d1); otherwise f = clamp(d1 + d2, 0, 1) and the price becomes (1 − f)·price; the result is rounded
  by `0x00ea4e80`. So d1 doubles as a flat amount (when above 1) or a fraction. Tranche 08 named the globals without
  the formula.
- `0x00849fd0(hash)` returns the localized UTF-16 string for a hash, or the literal L"!!Unrecognized hash value".
- `0x00d9e740(string, seed)` is the same case-insensitive CRC as `0x00d9e8b0` but returns the value directly (0 for a
  null string).
- `0x009f0cf0(store, flag)` sets store `+0x1d` = flag and the 24 per-slot dwords at `0x0263e028 + s·0x60`: all 0 when
  the flag is 0; when non-zero, all 1, then several reset to 0 and every entry in the store's component list whose slot
  is not 0, 1, 2, 4, 10, 11, 21 or 22 gets its byte `+4` set. HYPOTHESIS: per-slot "stripped / hidden" flags; flag 0
  restores visibility. (`pcr_add_default_clothing` and `pcr_restore_clothing` both pass 0.)
- `0x009f1ca0()` walks 24 circular object lists (heads every 0x14 bytes from `0x02668258`); for objects with bit `0x10`
  of `+0x78` it calls `0x0074e670(obj +0x58)` and clears the bit, zeroes `+0x24…+0x43` of every object, and calls
  `0x009f6c30()` if any bit was cleared. HYPOTHESIS: drops temporary preview overrides.

---

## D. Cross-function observations

1. **Two sub-registrars, no gameplay names.** All 25 names are UI customization functions. A spec table that groups by
   registrar must list `0x00830230` (PCU, 44 names) and `0x008377d0` (PCR) under the UI registrar `0x008430f0`.
2. **Every Lua callback in the customization UI is a coroutine.** `0x00e0ca80`/`0x00e0c720` and `0x00e0c8f0` both create
   a new Lua thread per callback, run it synchronously through `0x00e0cba0`, free it when it finishes, and keep it when
   it yields. The thread table has 256 records. When the table is full, or the callback name isn't a Lua function,
   every report function silently skips the element, except `pcu_get_items_in_category` mode 2, which **hangs** (A.2).
   This is the same script-thread table spec §26.27 documents for the `thread_*` names ("The script-thread table").
   Two of that entry's HIGH CONFIDENCE items become CONFIRMED here: the allocators `0x00e0c720`/`0x00e0c8f0` do compare
   the count with 256, and the release `0x00e0c650` does null the caller's record pointer. The record field §26.27 calls
   the `+0x08` "key" is, in both allocators, the Lua state the function is looked up in (passed in, or the parent
   record's `+0x08`). The earlier spec wording that these UI calls "open event records" (§19.5, §23.3, §23.4) should be
   read as "spawn a script coroutine".
3. **Player-created outfits share the store catalog.** `pcu_create_outfit` takes a node from a 130-node pool and links
   it into the same circular list `0x022db9b8` that holds store outfits. Store outfits are numbered by "bit 0" in A.1,
   tranche 08 E.3 and E.11, and created ones (flag 2) by "any flag" in A.2 mode 1 and E.15. As soon as one outfit has
   been created, the "Outfits" list index and the purchase/count index **refer to different outfits** for every node
   after it (CONFIRMED by the flag value 2 written in A.5).
4. **Outfit pools and guards don't match.** The allocator needs a free block (`0x022d60f0`) **and** a free node
   (`0x022db9b4`); `pcu_can_create_outfit` checks only the block list, never the node list or the 128 saved-outfit cap.
   `pcu_create_outfit` writes through the allocator result without a null check. Saving fails silently at 128 and
   leaves an orphan node in the catalog. `pcu_delete_outfit` returns both block and node to their free lists.
5. **"New item" flag.** Owned entry byte `+9` is reported by A.2 (argument 11) and cleared by A.7. The outfit appender
   `0x009dafd0` writes it from its 5th argument when it adds pieces (HIGH CONFIDENCE from the decompile's packed
   store; E.11 passes 0 there).
5a. **Wardrobe reset.** `pcr_add_default_clothing` empties the whole owned list through `0x00827960(player, 0)` →
   `0x009dae70` (B.15). It is the only function in this tranche (and, as far as tranches 08-09 show, the only Lua entry)
   that clears the owned inventory wholesale.
6. **Hidden-register conventions confirmed again:** `0x008307f0` takes its string in EDI (B.5, third sighting);
   `0x00836b10` takes ECX = hair category and EAX = style (B.9, second sighting after §16.6); `0x008436c0` takes its hash
   pointer in EAX (A.5, new); `0x0094d920` is a `this` call with the player in ECX (B.7, new for this callee);
   `0x007c3e60` returns its dialog in EAX although the decompile shows `void` (B.12, new).
7. **Three different "no string id" sentinels** in the same family: `0x029c9964` (items, variants, hair styles, A.2/A.3/
   B.2), `0x029c9960` (skin names, B.1) and **0** (eye definitions, B.4). A binding layer must not assume one value.
8. **Empty-range PRNG faults.** `0x00dab660(lo, hi)` divides by `hi − lo + 1`, so an empty table makes it divide by zero.
   `pcr_lips_randomize` (palette 2) and `pcr_hair_randomize_all` (hair colours) call it without checking the count;
   `pcr_randomize_eye_colors` checks first.
9. **Local-player null checks are rare.** Only `pcr_report_anims`, `pcr_get_voice`, `pcr_cleanup_player_voice`,
   `pcu_get_item_defaults` (for its wear-option step only) and `pcu_can_create_outfit` (inside `0x00821300`) check the
   local player. `pcu_delete_outfit` and `pcu_clear_item_new` are safe because their accessors compare against the
   local-player global. Every other root reads the player or its `+0x2088` store unguarded.
10. **Index spaces per argument** (for a binding layer): A.1 arg 1 = category index, arg 2 = catalog-outfit index;
    A.2 arg 1 = category index; A.3 / A.7 arg 1 = **item index** (table `0x022db9c8`), A.7 arg 2 = per-item instance
    number; A.4 = saved-outfit index; A.8 = descriptor index into `0x022ffcf0`; B.2 = hair category 0..2; B.4 reports
    **global composite-definition indices**, not eye-colour indices; B.5 reports table indices in alphabetical order.
11. **Reported shapes a script can observe:** A.2 mode 1 sends 10 or 12 values depending on the node's flags; A.3 sends
    1 or 35; A.5 sends 1 or 2; A.9 sends 1 or 3; B.10 sends 0 or 1.

## E. OPEN

- `0x00e0ca80` threads: whether shipped scripts' customization callbacks yield (which decides how close the 256-record
  cap can get); `0x00e0d070`'s error handler.
- A.1: meaning of category `+0x2c` (an item count per §26.14?) and the intent of the "greater than running result"
  comparison.
- A.2: purpose of the unused "temp hash" object; meaning of item `+0x5c` bits 1, 4, 5 and item `+0x18`; why mode 3
  writes `0x01300c6c`.
- A.3: the float global `0x022ff5c8` (price multiplier?) and variant `+0x18` (fixed price?).
- A.4: whether two saved entries can share one player-created record (dangling entry after delete).
- A.5: what the virtual-keyboard table `0x02317d74` really is and who fills it; whether anything reads the
  uninitialized piece dword 1 of a created outfit; how many free blocks/nodes the pools start with (`0x0082a470`
  initializes the node pool).
- A.6: what queued job kinds 6 and 7 do (consumer of `0x0264b720`).
- A.8: whether the slot-5 backup can survive leaving the store screen (nothing in A.8 restores it except the next
  non-Piercings navigation).
- B.1: the image-hash field `+0xc` and the second sentinel `0x029c9960`.
- B.3: whether entry 0 of the per-store hair-colour table is always the hair slot.
- B.6: the replace branch of `0x009f9f60` at `0x009fa057`.
- B.9: what the store-busy counter `0x0263df00 + s·0x124` counts; whether the wrong-slot `0x00831ad0` call has any visible
  effect after `0x00836b10` has set the right one.
- B.12: the writer and meaning of `0x024e97d2`; where `0x022ccc50`/`0x022ccc51` are reset; whether `0x007c3310` can
  return null; what `0x008322f0` does on confirm.
- B.13: `0x0046a160`'s exact effect on the voice record.
- B.14: player `+0x2090`; `0x009f59b0`, `0x00831e90`, `0x00827f90`, `0x02300848`.
- B.15: the slot predicate `0x00828420`; the rest of `0x00827960`'s add path (past the dumped instruction budget);
  whether any shipped script calls `pcr_add_default_clothing` on an established character.

## F. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `pcu_get_num_items_category` | PCU `0x00830230` (under UI `0x008430f0`) | `0x0082bb50` | resolved — two modes; arg 1 ignored in mode 2; null read on unmapped slot (shape) |
| 2 | `pcu_get_items_in_category` | PCU | `0x0082fa30` | resolved — 3 callback modes; **infinite loop in mode 2 when the callback can't be built**; numbering mismatch; writes `0x01300c6c` |
| 3 | `pcu_get_item_defaults` | PCU | `0x0082c7f0` | resolved — 1 or 35 values; unchecked variant-0 read (data-gated) |
| 4 | `pcu_delete_outfit` | PCU | `0x0082dad0` | resolved — guarded; returns created-outfit block and node to the pools |
| 5 | `pcu_create_outfit` | PCU | `0x0082d850` | resolved — arg is a text key; **null write when pools are empty**; orphan node at 128 outfits |
| 6 | `pcu_clear_obscured_slots` | PCU | `0x0082e7e0` | resolved — re-sync + jobs 6/7 (job meaning OPEN) |
| 7 | `pcu_clear_item_new` | PCU | `0x0082bf40` | resolved — clears owned byte `+9`; arg 2 = -1 hits the first owned entry |
| 8 | `pcu_category_nav_clear_slot` | PCU | `0x0082f440` | resolved — "Piercings" hides slot 5; **unbounded index → out-of-bounds read + deref** |
| 9 | `pcu_can_create_outfit` | PCU | `0x0082ae00` | resolved — 1 or 3 values; guard incomplete (node pool, 128 cap) |
| 10 | `pcu_bra_required` | PCU | `0x0082ad10` | resolved — player is female (`+0xa41` == 1); no null check |
| 11 | `pcr_report_skin_colors` | PCR `0x008377d0` (under UI `0x008430f0`) | `0x00832570` | resolved — per-skin coroutine from the caller's thread; second sentinel |
| 12 | `pcr_report_hair_items` | PCR | `0x00832cc0` | resolved — **unbounded category index; null read on component miss** |
| 13 | `pcr_report_hair_colors` | PCR | `0x00833420` | resolved — "current" = entry 0 of per-store colour table |
| 14 | `pcr_report_eye_colors` | PCR | `0x00830da0` | resolved — reports global definition indices; sentinel 0 |
| 15 | `pcr_report_anims` | PCR | `0x00834bf0` | resolved — alphabetical order via selection sort; BLANK = -1 / `0x102` |
| 16 | `pcr_randomize_eye_colors` | PCR | `0x00835bc0` | resolved — unbounded 600-entry stack array (data-gated) |
| 17 | `pcr_purchase_tattoo` | PCR | `0x008333a0` | resolved — cash only, reason `0xb`; negative price pays the player |
| 18 | `pcr_lips_randomize` | PCR | `0x00834a00` | resolved — **divide by zero on empty palette; null read on unknown item;** unbounded 64-entry array |
| 19 | `pcr_hair_randomize_all` | PCR | `0x00837450` | resolved — **divide by zero on empty colour table;** wrong-slot colour record (bug) |
| 20 | `pcr_get_voice` | PCR | `0x00832e60` | resolved — 0 or 1 values; store `+0x10` |
| 21 | `pcr_get_triangle_cursor_pos` | PCR | `0x008329a0` | resolved — 2 integers; setter truncates |
| 22 | `pcr_creation_complete` | PCR | `0x008335c0` | resolved — co-op handshake + waiting dialog |
| 23 | `pcr_cleanup_player_voice` | PCR | `0x00830a30` | resolved — clears the voice-slot cue, host-replicated |
| 24 | `pcr_apply_regional_preset` | PCR | `0x00830600` | resolved — region/preset table, morph pairs, player `+0x2090`; no null check |
| 25 | `pcr_add_default_clothing` | PCR | `0x008306a0` | resolved — **empties the owned-clothing list**, re-applies gender/ethnicity defaults on slot 10 |

## G. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | A.2 `0x0082fdb0`→`0x0082ff77` | callback setup failure in mode 2 retries the same owned entry forever (**hang**) | CONFIRMED |
| 2 | A.5 `0x0082da49`/`0x0082da52` | allocator result (null when a pool is empty) read and written unchecked | CONFIRMED |
| 3 | A.5 | `0x009dafd0` refusal at 128 ignored → orphan catalog node | CONFIRMED shape |
| 4 | A.8 `0x0082f463` | 16-bit index into the 16-entry descriptor array, no bound or null check, then dereferenced | CONFIRMED |
| 5 | A.8 | single-level slot-5 backup overwritten by a repeated "Piercings" navigation | CONFIRMED |
| 6 | A.9 vs A.5 | guard checks the block pool only, not the node pool or the 128 cap | CONFIRMED |
| 7 | A.2 mode 1 vs A.1 / E.3 / E.11 | catalog numbering differs once a created outfit (flag 2) exists | CONFIRMED |
| 8 | A.1 `0x0082bc32` | null category from `0x00828270` dereferenced | CONFIRMED shape, data-gated |
| 9 | A.3 `0x0082c912` | variant 0 read through an unchecked `+0x2c` | CONFIRMED shape, data-gated |
| 10 | A.7 | arg 2 = -1 clears the "new" flag of an unrelated owned entry | CONFIRMED |
| 11 | B.2 `0x00832d00`, `0x00832d6b` | unbounded index into `0x01300cf4`; null result of `0x009fd5b0` dereferenced | CONFIRMED |
| 12 | B.8 `0x00834af6`→`0x00dab694` | empty palette → divide by zero | CONFIRMED shape |
| 13 | B.8 `0x00834abf` | unknown lips item → null read | CONFIRMED shape |
| 14 | B.8 / B.6 | 64- and 600-entry stack arrays filled without bounds | CONFIRMED unchecked, data-gated |
| 15 | B.9 `0x0083748f` | empty hair-colour table → divide by zero | CONFIRMED shape |
| 16 | B.9 `0x00837582` | colours recorded under hair-style entry i's slot, not category i's slot | CONFIRMED read, intent HYPOTHESIS |
| 17 | B.7 | negative price credits cash; no affordability check | CONFIRMED |
| 18 | B.14 | player written/read without a null check inside `0x00838850`/`0x00834860` | CONFIRMED shape |
| 19 | most roots | local player / store not null-checked (see D.9) | CONFIRMED shape |
| 20 | B.5 | byte loop counters vs dword counts; unbounded flag-array clears; unterminated `strncpy` | CONFIRMED shapes, data-gated |
| 21 | B.12 (`0x007c3310` → `0x007c3e60` → root) | empty dialog pool → null `this` and null `+0x12c` read | CONFIRMED shape |
| 22 | B.15 (`0x00827960` → `0x009dae70`) | owned-clothing inventory wiped before defaults are re-applied | CONFIRMED (intent HYPOTHESIS: new-character only) |
| 23 | A.2 mode 1 | 10 or 12 callback values depending on node flags (positional shift) | CONFIRMED |

## H. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`memmove`, `wcsncpy`, `strncpy`, `_stricmp`, `tolower`), as earlier tranches do.
- Final self-check run on this file with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits**.
- No spec file was edited. The private Ghidra copy `tools\gp_t09` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
