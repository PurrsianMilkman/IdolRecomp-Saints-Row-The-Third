# Saints Row: The Third — Weapons and Combat Data Tables: XML Schemas Recovered from the Loaders

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process), agent AH
**Phase:** Schema-from-loader campaign (`HANDOFF.md` §30, archived §27.2) — group "weapons and combat"
**Scope:** For each `.xtbl` gameplay table of the weapons/combat group whose literal filename appears in the executable: the loader, the element tree its reader accepts, each element's type and destination offset in the runtime record, defaults and required-vs-optional behaviour, unit conversions, name-hash keys, cross-table references and fixed capacities. Base-game *values* are out of scope (the compressed base-game containers are not read here — see §1.1). *[Superseded 2026-09-23: the base tables have since been read and validated, §18.]*
**Method:** Exact filename literals were located by a raw scan of the executable and followed by string cross-reference in Ghidra 12.1.3 (project copy `tools/gp_tbl1`) to each table's loader; the per-row reader and its callees were decompiled and read to the end; the shared XML accessor helpers were read once and are documented in §1. The three raw-stored DLC archives (`dlc1/2/3.vpp_pc`, 142 `.xtbl` files) supplied real rows to validate against (§16). No whole-binary predicate search was used; negative claims ("the reader never reads element X") rest on a complete read of the reader body and its callees plus a case-insensitive whole-image literal scan (harness `tools/harnesses/tbl_exe_lit.py`).
**Cleanroom compliance:** No decompiled code is reproduced and no original internal identifiers are used. XML table/element names, enum literals and flag literals are *data* and are listed; offsets, sizes, strides, constants and function addresses are evidence anchors. Function addresses use the form `FUN_00XXXXXX` for the image address only.
**Confidence key:** CONFIRMED — disassembly (read directly from the reader's code) / CONFIRMED — empirical (checked against real shipped rows) / HIGH CONFIDENCE — inferred / HYPOTHESIS — unconfirmed / OPEN / UNKNOWN.

---

## 1. Overview, method, and the shared reader grammar

### 1.1 What this document is, and its boundary

Every table in this group is an ordinary `<root><Table><Row>…` document (`spec-xtbl-format.md` §2–§3). Each is read at start-up by a small dedicated loader that opens the file by name, walks the rows and fills a runtime array. This document recovers those loaders' *vocabulary*. The base-game `.xtbl` files themselves are inside compressed containers this project cannot decode past entry 0 and nothing here attempts to; the only raw-readable rows are the DLC ones, used for validation (§16). *[Superseded 2026-09-23: the base tables have since been read and validated, §18.]* **[CONFIRMED — disassembly for every reader claim below unless marked.]**

All 22 tables assigned to this group have their exact filename literal in the executable, each with exactly the number of code cross-references listed in §15 — none had to be skipped. The DLC archives carry **no** raw `weapon_upgrades`, `ammo`, `melee`, `aim_*`, `combat_*` etc. tables; the only weapon-group DLC files are `dlc2_weapons.xtbl` (6 rows), `dlc3_weapons.xtbl` (3 rows), `dlc2_weapon_tracers.xtbl` and `dlc1/2/3_explosions.xtbl`.

### 1.2 The XML node model **[CONFIRMED — disassembly]**

The parsed document is a tree of nodes with this layout: **`+0x00` name pointer, `+0x04` next-sibling pointer, `+0x08` first-child pointer, `+0x0C` text pointer** (NULL when the element has no text). Every lookup is a linear walk and every name comparison is **case-insensitive** (`_stricmp`). The document is opened by `FUN_00DAC9A0(filename, pool, 1)`, which parses the file through `FUN_00DC5AC0` and returns the **`Table`** child of the root (on failure it prints a formatted "table file missing or invalid" message carrying the file name and the parser error); the in-memory twin is `FUN_00DACA90` (`spec-xtbl-format.md` §6.2). The document is released with `FUN_00DAB960` (thunk `FUN_00DAB9D0`). The sibling metadata blocks `TableTemplates`, `TableDescription`, `EntryCategories` and per-row `_Editor` are never visited by row readers (one loader, weapon upgrades, does look for an `_Editor` child — §4).

### 1.3 The shared accessor family (documented once; siblings cross-reference this section) **[CONFIRMED — disassembly]**

All helpers take `(destination, parent-node, child-name)`; **passing a NULL child name means "use the parent's own text"**. Two flavours exist for every scalar type:

| Type | "always" reader (parses whatever text it finds) | "if present" reader (returns a bool; writes only when the child exists and has text) |
|---|---|---|
| `s32` | `FUN_00DABC70` | `FUN_00DABD20` |
| `u32` | `FUN_00DABDF0` | `FUN_00DABE80` |
| `u16` | `FUN_00DABF20` | `FUN_00DABFB0` |
| `s16` | `FUN_00DAC050` | `FUN_00DAC100` |
| `u8`  | `FUN_00DAC1D0` | `FUN_00DAC260` |
| `s8`  | `FUN_00DAC300` | `FUN_00DAC3B0` |
| `bool`| `FUN_00DAC480` | `FUN_00DAC510` |
| `f32` | `FUN_00DACCB0` | `FUN_00DACD40` |

- **The "always" flavour does not initialise its 1 KB scratch buffer on the absent path** — when the child is missing it parses whatever was on the stack. (`spec-vehicle-data.md` §7.2 describes this flavour as "always write, 0 if absent"; the disassembly of `FUN_00DACCB0` shows no clearing of the buffer, so "0 if absent" is not guaranteed. Shipped data always supplies these elements. **[CONFIRMED — disassembly of the entry sequence; behaviour on absent path is therefore unspecified; a re-implementation should treat absent as 0.]**)
- Integer text parser `FUN_00DAB8B0`: `0x`/`0X` prefix → hexadecimal, else decimal digits, stopping at the first non-digit; the signed variants strip one leading `-` first. `u8`/`u16` variants parse to 32 bits and **truncate**. Boolean parser `FUN_00DAB850`: case-insensitive whole-string match, `true`/`yes` → 1, everything else (including `false`, `no`, unrecognised text) → 0.
- Float grammar `FUN_00DACB20` (`spec-vehicle-data.md` §7.2): optional `-`, integer part, optional `.digits`, optional exponent; a leading `.` is accepted; **a leading `0x` yields exactly 0.0**.
- Text getters `FUN_00DABA10`, `FUN_00DABA40` → pointer to a child's text or NULL; bounded string copies `FUN_00DABA70` (void) and `FUN_00DABAB0` (returns present); heap-duplicating readers `FUN_00DABAF0`/`FUN_00DABBB0`.
- Child navigation: first child by name `FUN_00DC4FF0` (thunk `FUN_00DAB9E0`), next sibling of the same name `FUN_00DC5030` (thunk `FUN_00DAB9F0`), count of children of a name `FUN_00DC5150` (thunk `FUN_00DABA00`).
- **Enum reader `FUN_00DAC830(table, count, node, 0)`**: index of the first case-insensitive match of the node's text in a table of literal pointers; **−1 if the node is NULL or nothing matches** (a NULL table slot also ends the search with −1).
- **Flag-list readers**: `FUN_00DAC740(table, count, parent)` returns a bitmask (bit *i* = table entry *i*) over the parent's `Flag` children; `FUN_00DAC7D0(parent, text)` tests whether any `Flag` child equals a string. Many readers instead compare each `Flag` child against inline literals (an if/else chain) — those chains are tabulated per table.
- Lists: `FUN_00DAC5B0` (array of `Element` children copied as strings), `FUN_00DAC620` (integer list of `Element` children, a leading `"` selecting a quoted-string callback), `FUN_00DACE80` (float list of `Element` children), `FUN_00DACE40` (one float replicated ×4).
- **vec3**: `FUN_00DACF20`, `FUN_00DACF60`, `FUN_00DACFB0` — children named `X`, `Y`, `Z` (all "always" floats).
- **`Filename` child getters**: `FUN_00DAC880`, `FUN_00DAC8C0` (text), `FUN_00DAC900`, `FUN_00DAC950` (copy).

### 1.4 Name hashing **[CONFIRMED — disassembly]**

`FUN_00D9E8B0(out, string, seed, maxlen)` and `FUN_00D9E740(string, seed)` are the engine's table-driven CRC-32 (reflected, table at `0x01320DA0`, input lower-cased, **no final XOR**). Every weapon-group caller passes **seed 0** (the vehicle spec, `spec-vehicle-data.md` §7.1, reports the same; `spec-conversation-format.md` §6 reports seed `0xFFFFFFFF` for another caller — the seed is a parameter). A NULL name hashes to 0. The multiply-by-33 bucket hash `FUN_00DAB330(name, 0x80)` is used for camera-shake names. Sound-event names go to the audio middleware's own string-to-id function through `FUN_00462960` (the literal `none` and an empty string give 0).

### 1.5 Loader idioms common to the whole group **[CONFIRMED — disassembly]**

1. **Two-pass row loading with a `Framework` filter.** A base-game pass counts rows whose `Framework` child is absent or equals `main` (case-insensitive), allocates the array, and reads only those rows; rows carrying another framework name (DLC rows) are skipped by that pass. `Is_DLC` (bool) becomes a per-record **gate byte: `0x00` if true, else `0xFF`** (same convention as the vehicle entry, `spec-vehicle-data.md` §7.3).
2. **First-load vs refresh flag.** Loaders are called with a byte flag: 0 = first load (allocate, zero, duplicate name strings); non-zero = **refresh in place** (row matched to its existing record by name; strings not re-allocated; fewer defaults re-applied). Row readers therefore contain many "`if not refresh`" default initialisers. Documented here once; per-table sections state only what matters.
3. **Record flags at record `+0x18`, bit 2 = "loaded"** (weapons; and other tables use analogous live bits).
4. **`Name`** (`0x0129EE6C`) is the row key. Records are found by linear scan with `_stricmp` (weapons, ammo, aim-drift profiles, melee-attack sets) or by CRC (effects, explosions, tracers, `items_3d`).

### 1.6 Cross-table name resolvers used by this group (addresses are evidence anchors) **[CONFIRMED — disassembly]**

| Resolver | Target | How the key is matched |
|---|---|---|
| `FUN_00B81220` | weapons array (`0x028DC4DC`, count `0x028DC4E0` *(per §2.1 this is the capacity, base rows + 12; loaded count `0x028DC4E4`)*, stride `0x7EC`) | `_stricmp` on record `+0x00`, only records with `+0x18` bit 2 |
| `FUN_00B6E4E0` | ammo records (`DAT_028CDC4C`, count `DAT_028CDC48`, stride `0x30`) | `_stricmp` on record `+0x00` → record address (0 if none) |
| `FUN_00B6C320` | aim-drift profiles (`0x028CD2B8`, count `0x028CDBD4`, stride `0x48`, max 24) | `_stricmp` on `+0x00` |
| `FUN_004CCE70` | animation-group name array (`DAT_03519838`, count `DAT_03171C1C`) | `_stricmp`; returns the index, **−1 if none; not NULL-safe** |
| `FUN_009561B0` | strafe-angle sets (`0x02624390`, stride `0x64`, count `0x02624388`) | `_stricmp` on the inline name; index, −1 if none |
| `FUN_005C50B0` | effects hash map (object `0x0143CB38`, record stride `0x98`, hash at `+0x44`) | CRC(name), linear-probe on `hash mod capacity`; returns the record's first dword (the effect index), **`0xFFFFFFFF` if none** |
| `FUN_00590D20` → `FUN_00590CE0` | explosion records (`0x013CF5C0`, stride `0xEC`, count `0x013CF504`) | CRC(name) compared with the record dword at `+0x20`; 0 if none |
| `FUN_005A2FD0` | tracer records (`0x014033E0`, stride `0x2C`, count `0x014033D4`) | CRC(name) compared with the dword at `+0x04`; 0 if none |
| `FUN_00904C10` | `items_3d` records (`0x025F5B98`, stride `0xB8`, count `0x025F5B8C`) | CRC(name) at `+0x04` |
| `FUN_008DCB10` | `items_inventory` records (`0x0250ADC0`, stride `0x34`, 110 slots, live bit in byte `+0x30` **[Conflict: `spec-save-format.md` §10.3 (entry 1 weapon-inventory row) says bit 0 of the byte at `+0x2C` = valid; not re-checked which is right — see the matching marker there]**) | `_stricmp` on `+0x00` |
| `FUN_0058BF30` | brass records (`0x013CF220`, stride `0x0C`, count `0x013CF280`) | `_stricmp`; stores the record address |
| `FUN_0057BEE0` | camera-shake table (`0x012E4620`, indexed) | multiply-33 hash → index via `FUN_0057BC70`; 0 if none |
| `FUN_00561370` | foley-collision records (`0x013C85B0`, stride 20 bytes, count = `u16` at `0x013C85B4`) | Wwise-style id compared with the record's first dword |
| `FUN_006F76F0` | 33 physical-material names (`0x0113DF70`) | `_stricmp`; **index 31 (`0x1F`) if no match** |
| `FUN_009828F0` | melee attack table | CRC(name) → `FUN_00982780`; **`0xFFFF` if NULL/none** |

---

## 2. `weapons.xtbl` — the weapon record

**Loader chain [CONFIRMED — disassembly].** `FUN_005D25F0` (game init) → `FUN_00B85400` (the weapon subsystem initialiser: reads `weapon_categories.xtbl` (§3), then calls) → **`FUN_00B834C0(refresh)`**, which: loads `aim_drift.xtbl` (§8) and `weapon_melee_attacks.xtbl` (§6) first; opens **`weapons.xtbl`** (literal `0x01184E58`, three code xrefs: the loader's row-count pass, its call that hands the name to the row walker at `0x00B835BC`, and the preload pass `FUN_00AD0180`); counts the `<Weapon>` rows whose `Framework` is absent/`main`; sets the capacity to **count + 12** (`0x028DC4E0`) and allocates `capacity × 0x7EC` bytes at **`0x028DC4DC`**; clears the loaded bit of every slot; then `FUN_00B83390` walks the rows and calls the per-row reader **`FUN_00B82200(record, ctx, row, refresh)`** for each base-game row, ORing `4` into record `+0x18` and counting them at `0x028DC4E4`. A final pass resolves `Base_Version` names to pointers (§2.2). The 12 spare slots are for DLC rows, filled by the DLC handler below.

**DLC rows [CONFIRMED — disassembly].** The `weapon` entry of the DLC content registry (§15.2) is **`FUN_00B835E0(packages)`** (its unload twin is `FUN_00B837E0`). Ghidra had no function at either address (the code is reached only through the registry's function-pointer words, so string cross-references from it were invisible to the first xref pass; both were disassembled by hand for this pass). For each DLC package it: (1) builds `<framework>_items_inventory.xtbl` (`FUN_0045BA50`, §10.1), finds the first `Inventory_Item` row of that framework, reads its **`Info_Slot_Index`** (`u32`, default 0) and hands both to the `items_inventory` DLC loader `FUN_008DC8C0`; (2) builds **`<framework>_weapons.xtbl`**, finds the **first `Weapon` row whose `Framework` equals the package's framework name and reads *its* `Info_Slot_Index` (`u32`, default 0)**; (3) calls the row walker `FUN_00B83390` with that value as the **starting slot index** and the package's framework string, so the framework's `Weapon` rows are read into consecutive slots **from `Info_Slot_Index`** (stopping at the capacity); (4) runs the post-pass that resolves `Base_Version` names. The two raw DLC files agree: `dlc2_weapons.xtbl`'s first row carries `Info_Slot_Index` **82** (six rows → slots 82–87) and `dlc3_weapons.xtbl`'s **89** (three rows → 89–91); with capacity = base count + 12 this is consistent with ~~a base-game count of 82 weapons and DLC slots 82–93 (HIGH CONFIDENCE — the base file is not readable; 82 + 12 = 94 = `0x5E`, the first DLC `items_inventory` slot the unload routine clears, which supports the reading)~~ **a base-game count of 82 weapons and DLC slots 82–93 — CONFIRMED — empirical, 2026-09-23: the real base `weapons.xtbl` (`misc_tables.vpp_pc` entry 344) has exactly 82 `<Weapon>` rows, every one with `Framework` absent; capacity 94 = 82 + 12 is therefore exact, not inferred (§18.4).** The unload routine `FUN_00B837E0` clears the loaded bit of the last 12 slots (decrementing `0x028DC4E4`) and unloads `items_inventory` slots `0x5E…0x6D`. `Info_Slot_Index` is therefore **consumed by the DLC handler, never by the row reader** (`FUN_00B82200` ignores it, as do rows after the first of a framework).

**A second reader of the same file:** `FUN_00AD0180` re-opens `weapons.xtbl` and, for each row that has a `Vehicle_Weapon` element and a `Name`, gathers up to 16 effect handles for preloading: the effects named by `Explosion`, `Underwater_Explosion` and `Penetrating_End_Point_Explosion` (each resolved through the explosion table to that explosion's effect) and the `Effect` of every child of `Effect_Situations`; the list is registered against the weapon name. (Preload bookkeeping only; nothing is stored in the weapon record.)

### 2.1 Storage

| Item | Value |
|---|---|
| Array / stride / capacity | pointer at `0x028DC4DC`; **stride `0x7EC` (2028 bytes)**; capacity `0x028DC4E0` = base-game `<Weapon>` rows + **12**; loaded count `0x028DC4E4` |
| Record key | `Name` (heap string at record `+0x00`); lookup `FUN_00B81220` |
| Row element | `<Weapon>` (case-insensitive) directly under `<Table>` |
| Loaded bit | record `+0x18` bit 2 |
| Consistency check | the last field read ends at `+0x7EC` exactly (`Blood_Decal_Delay`, §2.3), matching the stride |

### 2.2 Record layout — top level **[CONFIRMED — disassembly; offsets are record-relative]**

"Rd" = reader family from §1.3 (a/i = always/if-present). "Init" = value when the element is absent on first load (the record is zero-filled first, so unlisted defaults are 0). Effects are `s32` handles (−1 = none); explosions/ammo/tracers/brass are record **pointers** (0 = none).

| Offset | Element (XML) | Type / reader | Default / behaviour | Notes |
|---|---|---|---|---|
| `+0x00` | `Name` | heap `char*` | required | duplicated on first load only; refresh: mismatch ⇒ row skipped |
| `+0x04` / `+0x08` | `Base_Version` | heap `char*` / resolved pointer | absent ⇒ `+0x04`=0; post-pass: `+0x08` = the named weapon's record, or the record itself if absent/unresolved | names the base weapon this record is a variant of |
| `+0x0C` | `Is_DLC` | `u8` gate | `0x00` if true else `0xFF` | §1.5 |
| `+0x10` / `+0x14` / `+0x18` | `Flags`/`Flag` | flag words A / B / C | zeroed at the start of each read | §2.4. Bit `0x8000000` of A is also set by `Waterspray_info`. `+0x18` bit 2 = loaded |
| `+0x1C` | `Special_Case_Type` | `s32` id | absent/unmatched ⇒ **−1** | id column of the 14-name table, §2.5 |
| `+0x20` | `Melee_Damage_Overrides` | `u8` present | 1 if the element exists | children below |
| `+0x24` / `+0x28` / `+0x2C` | `NPC` / `Player` / `Online` (under `Melee_Damage_Overrides`) | `f32` (a) | — | |
| `+0x30` | `Animation_Group` | `s32` (resolver `FUN_004CCE70`) | **required** (resolver is not NULL-safe) | |
| `+0x34` | `Fine_Aim_Animation_Group` | `s32` | defaults to `+0x30` | |
| `+0x38` | `Reload_Animation_Group` | `s32` | defaults to `+0x30` | |
| `+0x3C` | `Reload_override_time_sec` | `f32` (i) | 0 | |
| `+0x40` / `+0x42` | `Warmup_Delay` / `Cooldown_Delay` | `u16` (i) | 0 | row level only — see §16 for an `Audio/Warmup_Delay` in DLC data |
| `+0x44` | `Grenade_Type` | `s32` enum | −1 if absent/unmatched | §2.5 |
| `+0x48` | `Strafe_Angles` | `s32` (resolver `FUN_009561B0`) | written only if present; else 0 | index into `strafe_angles.xtbl` (§14) |
| `+0x4C` | `Weapon_Class` | `s32` enum | −1 if absent | 23 names, §2.5 |
| `+0x50` | `Category` | `s32` enum | written only if present; else 0 | 7 `WPNCAT_*` names, §2.5 |
| `+0x54` | `Inv_Slot` | `u8` enum | −1 → `0xFF` if absent | 11 names, §2.5 |
| `+0x58` / `+0x5C` | *(weapon `Name`)* | `items_3d` / `items_inventory` record pointers | first load only. **Missing entry ⇒ the diagnostic "Weapon parse can't find matching entry ITEMS_3D.XTBL / ITEMS_INVENTORY.XTBL for '…'" is printed and global byte `0x028DC4F1` is set** | so every weapon must have a same-named row in `items_3d.xtbl` and `items_inventory.xtbl` |
| `+0x60` | `Muzzle_Flash` | — | element is fetched and discarded; field forced to −1 | legacy element, no effect |
| `+0x64` / `+0x68` / `+0x6C` | `Muzzle_Effect` / `Alt_Muzzle_Effect` / `Melee_Effect` | effect handle | −1 if absent | resolver `FUN_005C50B0` |
| `+0x70` | `Fire_Cone_Impact_Effect` | effect handle | −1 if absent | |
| `+0x74` | `Offhand_Weapon_Mesh` | `u32` CRC of the name | absent ⇒ `DAT_029C9964` (0) | |
| `+0x78` / `+0x7C` / `+0x80` / `+0x84` | `Tracer_Info` → `Tracer` / `Tracer_NPC` / `Alt_Tracer` / `Alt_Tracer_NPC` | tracer record pointers | all four 0 if `Tracer_Info` absent | resolver `FUN_005A2FD0` |
| `+0x88` | `Tracer_Info` → `Tracer_Frequency` | `u8` (i) | 0 | |
| `+0x8C`…`+0xBB` | `Constant_Effects` → repeated `Constant_Effect` | **array of 12-byte records**: `+0` effect handle (`Effect`), `+4` heap `char*` (`Weapon_Prop_Point`, first load only, 0 if absent), `+8` condition enum | count at **`+0xBC`** | **capacity 4 records; the loop is not bounded** — a fifth row would overwrite `+0xBC` and beyond. `Condition` text (case-insensitive): `during melee swing` = 1, `fine aim` = 2, `weapon raised` = 3; **anything else, including absent, = 0** (DLC3 uses the unrecognised text `always on`, which therefore means 0) |
| `+0xC0` | `Brass` | brass record pointer | 0 | resolver `FUN_0058BF30` |
| `+0xC4` | `Diversion_Kill_Multiplier` | `f32` (i) | 1.0 | |
| `+0xC8` | `Audio` → `Weapon_Model` | `u32` Wwise id of the name | — | block written only if `Audio` exists |
| `+0xCC` | `Audio` → `Soundbank_Name` | `u32` id | absent ⇒ id of `Wep_<Weapon_Model>` | |
| `+0xD0` / `+0xD4` | `Audio` → `Stop_Override_Event` / `Alt_Fire_Stop_Override_Event` | `u32` id | 0 | |
| `+0xD8` | `Audio` → `Sound_Radius` | `f32` (i) | 0 on first load | |
| `+0xDC` / `+0xDD` | `Audio` → `looping` / `alt_looping` | `u8` bool | `alt_looping` default 0 | |
| `+0xE0`…`+0x104` | `Target_Lockon` | see §2.3 | `+0xE0` = 0 if absent | |
| `+0x108` / `+0x10C` | `Trigger_Type` / `Alt_Trigger_Type` | `s32` enum | −1 if absent | `single` 0, `burst` 1, `automatic` 2, `charge release` 3 |
| `+0x110` / `+0x114` | `Ammo` / `Alt_Ammo` | ammo record pointer | 0 | resolver `FUN_00B6E4E0` → §5 |
| `+0x118` / `+0x11A` | `Magazine_Size` / `Ammo_per_Shot` | `u16` (a) | — | |
| `+0x11C` / `+0x120` / `+0x124` / `+0x128` | `Ammo_Regeneration` / `Range_Max` / `AI_Ideal_Range_Min` / `AI_Ideal_Range_Max` | `f32` (a) | — | |
| `+0x12C` | `NPC_Aim_Drift` | profile pointer | absent/unmatched ⇒ the profile named `Default` | resolver `FUN_00B6C320` → §8 |
| `+0x130`…`+0x13C` | `Time_Management` | §2.3 | | |
| `+0x13E`…`+0x14A` | `Alt_Time_Management` | same layout | all zero if absent | |
| `+0x14C` / `+0x150` | `Damage_Max` → `NPC_Damage` / `Player_Damage` | `f32` (a) | | |
| `+0x154` | *(copy of `+0x150`)* | | | |
| `+0x158` / `+0x15C` / `+0x160` | `Damage_Min` → `NPC_Damage` / `Player_Damage` / *(copy of `+0x15C`)* | `f32` (a) | **`Damage_Min` absent ⇒ the three words copy `+0x14C`/`+0x150`/`+0x154`** | |
| `+0x164` | `Riot_Shield_Damage_Multiplier` | `f32` (a) | | |
| `+0x168` | `Explosion` | explosion pointer | 0 | resolver `FUN_00590D20` → §10 |
| `+0x16C` | `Alt_Explosion` | | defaults to `+0x168` | |
| `+0x170` | `NPC_Explosion` | | defaults to `+0x168` | |
| `+0x174` | `NPC_Alt_Explosion` | | defaults to the (possibly overridden) `+0x170` | the value read for `Alt_Explosion` is therefore not what NPCs use when `NPC_Explosion` is absent |
| `+0x178` | `Underwater_Explosion` | | 0 | |
| `+0x17C` / `+0x180` | `Damage_Max_Dist` / `Damage_Min_Dist` | `f32` (i) | 0 / `Range_Max` (`+0x120`) | |
| `+0x184` | `Operator_Damage_Multiplier` | `f32` (i) | 1.0 | |
| `+0x188` | `Wieldable_Prop_Death_VFX` | effect handle | **read only when `Special_Case_Type` = 8 (`Wieldable Prop Weapon`)**; −1 if absent; otherwise 0 | |
| `+0x18C` | `Wieldable_Prop_Hits_Allowed` | `u16` (i) | same condition | |
| `+0x190`…`+0x1A0` | `Flat_Spread_Metrics` | §2.3 | `+0x190` = 0 if absent | |
| `+0x1A4` | `Fire_Cone_Angle` (degrees) | `f32` | default 1.0; if present the stored value is **cos(½ × angle)** (HIGH CONFIDENCE that the transcendental is cosine) | |
| `+0x1A8` | `Fire_Cone_Length` | `f32` (i) | 0 | |
| `+0x1AC`…`+0x208` | `Fire_Cone_Metrics` | §2.3 | | |
| `+0x20C` | `Shots_Per_Round` | `u8` (i) | 1 | |
| `+0x210`…`+0x24B` | `PlayerWeaponSpread` | spread block (§2.3) | | |
| `+0x24C`…`+0x287` | `PlayerAltWeaponSpread` | spread block | | |
| `+0x288`…`+0x2D3` | `NPCWeaponSpread` | NPC spread block (§2.3) | | |
| `+0x2D4`…`+0x31F` | `NPCAltWeaponSpread` | NPC spread block | | |
| `+0x320` | `Ragdoll_Force_Shoot` | `f32` (i) | 1.0 | |
| `+0x324` | `Object_Bullet_Hit_Impulse_Magnitude` | `f32` (i) | 10.0 | |
| `+0x328`…`+0x344` | `Ragdoll_Info` | §2.3 | zero if absent | |
| `+0x348`…`+0x3AC` | `Projectile_Info` | §2.3 | | |
| `+0x3B0`…`+0x433` | `melee_material_effects` → repeated `material_effect` (`material`, `effect`) | **33 `s32` effect handles indexed by material** | all −1 | material index from `FUN_006F76F0`; unknown material name ⇒ index 31 |
| `+0x434` | `melee_damage_to_anchored_scaler` | `f32` (i) | 0.2 | |
| `+0x438` / `+0x43C` | `vehicle_damage_scale` / `player_vehicle_damage_scale` | `f32` (i) | 1.0 / defaults to `vehicle_damage_scale` | |
| `+0x440` | `Melee_Attack_Info` | pointer to a `weapon_melee_attacks` set | absent/unmatched ⇒ the built-in default set at `0x0130F04C` | §6 |
| `+0x444`…`+0x45C` | `Waterspray_info` | §2.3 | `+0x45C` = `DAT_029C9964` if absent | |
| `+0x460`…`+0x484` | `Charge_Release_Info` | §2.3 | | |
| `+0x488`…`+0x70F` | `Vehicle_Weapon` | §2.3 | counts 0 if absent | |
| `+0x710`…`+0x774` | `Camera_Info` | §2.3 | | |
| `+0x774` / `+0x778` | `Burst_Fire_Info` → `Shots` / `Burst_Delay_ms` | `s32` (a) | 0 / 0 | |
| `+0x77C` | `NPC_Desired_Burst_Size` | `s32` (i) | **only read when `Trigger_Type` is `automatic` (2), default 3; otherwise fixed 1** | |
| `+0x780` / `+0x784` / `+0x788` / `+0x78C` | `Override_Bullet_Impact_Effect` / `Alt_…` / `…_NPC` / `Alt_…_NPC` | effect handle | −1 | |
| `+0x790` / `+0x794` | `Penetrating_End_Point_Explosion` / `Alt_…` | explosion pointer | 0 | |
| `+0x798`…`+0x7AC` | `Overheat_Info` | §2.3 | zero if absent | |
| `+0x7B0`…`+0x7CC` | `Effect_Situations` | §2.3 | all −1 | |
| `+0x7D0` | `Max_Melee_Impacts` | `s32` (i) | −1 | |
| `+0x7D4` … `+0x7E0` | *(initialised, not element-driven by this reader)* | | `+0x7D4`=0, `+0x7D8`=0.0 (constant), `+0x7DC`=0, `+0x7E0`=−1 | display-name text pointer / its handle / description / bitmap **override slots** — written only by `weapon_upgrades` patches (`Display_Name_Override`, `Description_Override`, `Bitmap_Override`, §4.3); HIGH CONFIDENCE |
| `+0x7E4` | `Blood_Decal_Scale` | `f32` (i) | 1.0 | |
| `+0x7E8` | `Blood_Decal_Delay` | `s32` (i) | −1 | record ends at `+0x7EC` |

### 2.3 Sub-blocks **[CONFIRMED — disassembly]**

**`Time_Management` / `Alt_Time_Management`** (reader `FUN_00B7E7B0`; block at `+0x130` and `+0x13E`, seven `u16` slots + a `u8`):

| Block offset | Element | Notes |
|---|---|---|
| `+0x00` | `Refire_Delay` | read as `u32`, stored `u16` (milliseconds) |
| `+0x02` | `npc_refire_delay` → `min` | default 400 |
| `+0x04` | `npc_refire_delay` → `max` | absent ⇒ `min + 500`; then **raised to at least `min`** (so a `min` larger than `max` in the data — the DLC rows have `min` 6000, `max` 3000 — produces max = min) |
| `+0x06` | `Post_Detonate_Delay` | 0 if absent (first load) |
| `+0x08` | `Pre_Detonate_Delay` | 0 if absent (first load) |
| `+0x0A` | `Firecone_Ramp_In_Time` (seconds) | stored as `u16` **milliseconds** (×1000); 0 if absent |
| `+0x0C` | `npc_refire_delay` → `npc_refire_type` | `u8` = (first character of the text) − `'1'`, so `1. Random Range` → 0 |

The `min`/`max` children placed **directly** under `Time_Management` in shipped rows are not read (only those inside `npc_refire_delay`).

**Player/NPC spread blocks** (`FUN_00B7FE20` for `PlayerWeaponSpread`/`PlayerAltWeaponSpread`, `FUN_00B80140` for `NPCWeaponSpread`/`NPCAltWeaponSpread`; the row-level wrapper name selects primary vs alt). All multipliers default 1.0, all others 0; angles are degrees converted to radians (×π/180 — the constant `0.01745299994945526`):

| Player block (15 words, at `+0x210` and `+0x24C`) | | NPC block (19 words, at `+0x288` and `+0x2D4`) | |
|---|---|---|---|
| `+0x00` `SpreadMinMax`/`Player_Spread_Min` (rad) | | `+0x00` `SpreadMinMax`/`NPC_Spread_Min` (rad) | |
| `+0x04` `SpreadMinMax`/`Player_Spread_Max` (rad) | | `+0x04` `SpreadMinMax`/`NPC_Spread_Max` (rad) | |
| `+0x08…+0x1C` `SpreadMovementMultipliers`/`Movement_Multiplier_` `Crouch`, `Walk`, `Run`, `Sprint`, `Vehicle`, `Fine_Aim` | | `+0x08…+0x18` same wrapper: `Crouch`, `Walk`, `Run`, `Sprint`, `Vehicle` (no `Fine_Aim`) | |
| `+0x20` (`u8`) `SpreadDynamics`/`Player_To_Spread_Max` | | `+0x20` (`u8`) `SpreadDynamics`/`NPC_To_Spread_Max` | |
| `+0x22` (`u16`) `SpreadDynamics`/`Player_To_Spread_Min` | | `+0x22` (`u16`) `SpreadDynamics`/`NPC_To_Spread_Min` | |
| `+0x24…+0x38` `SpreadDynamics`/`SpreadDynamicMultipliers`/`Movement_Multiplier_` `Crouch`…`Fine_Aim` | | `+0x24…+0x34` same wrapper: `Crouch`…`Vehicle` | |
| | | `+0x3C…+0x48` `SpreadMinMax`/`SpreadTargetMovementMultipliers`/`Target_Movement_Multiplier_` `Walk`, `Run`, `Sprint`, `Vehicle` | |

Every multiplier is an "if present" float, so a value placed one level too shallow (as in the DLC row for `bike_jet03_w`, which puts `Movement_Multiplier_Vehicle` directly under `SpreadMinMax` and directly under `PlayerWeaponSpread`) is **not read** and the default 1.0 stands.

**`Target_Lockon`** (`+0xE0` = 1 when present): `Lockon_Time_MS` `+0xE4` (`s32`), `Locking_Size_Multiplier` `+0xE8`, `Locked_Size_Multiplier` `+0xEC`, `Beep_Timing_Slowest` `+0xF0`, `Beep_Timing_Fastest` `+0xF4`, `Angle_From_Reticle` `+0xF8`, `Angle_From_Reticle_Lose_Target` `+0xFC`, `Locking_Rotation_Furthest_Angle` `+0x100` (degrees → radians), `flags`/`flag` `+0x104` `u8` (bit 0 = `enemy aircraft only`). All floats "always".

**`Flat_Spread_Metrics`** (`+0x190` = 1 when present; angles ×π/180): `Flat_Spread_Width_Angle` `+0x194`, `Flat_Spread_Height_Angle` `+0x198`, `Flat_Spread_Rotation` `+0x19C`, `Flat_Spread_Rotation_Per_Shot` `+0x1A0`.

**`Fire_Cone_Metrics`** (`FUN_00B7FC20`; children `Metric_Type` → either `Angle` → `Angle` or `Min_Max` → `Near_Radius`/`Far_Radius`; `+0x1AC`/`+0x1B0` zero on first load). `Angle` form: `+0x1A4` = cos(½ × angle rad) (HIGH CONFIDENCE, cosine). `Min_Max` form: `+0x1AC` Near, `+0x1B0` Far; `+0x1A4` = `Fire_Cone_Length ÷ √(length² + (Far−Near)²)`; `+0x208` = 7; `+0x1B4…+0x1BC` = 0; six ring points at 60° steps (`vec3` each, 12 bytes, from `+0x1C0`) with radius `(Far − Near)` and z = `Fire_Cone_Length`. The reader returns whether `Fire_Cone_Metrics` existed.

**`Ragdoll_Info`** (all "always" `f32`; zero if absent): `Chance` `+0x328`, `Death_Velocity_Horizontal` `+0x32C`, `Death_Velocity_Vertical` `+0x330`, `Death_Point_Velocity` `+0x334`, `Death_Angular_Velocity_Horizontal` `+0x338`, `Death_Angular_Velocity_Vertical` `+0x33C`, `Death_Range_Min` `+0x340`, `Death_Range_Max` `+0x344`.

**`Projectile_Info`** (`FUN_00B804D0`; the whole block is skipped if absent; returns failure — and the weapon row fails — **if `Model` is present but not found in `items_3d.xtbl`**):

| Offset | Element | Notes |
|---|---|---|
| `+0x348` | `Model` | `items_3d` record pointer |
| `+0x34C` / `+0x350` | `Speed` / `Speed_NPC` | `f32`; NPC defaults to `Speed` |
| `+0x354` / `+0x358` | `Post_Ignition_Speed` / `Post_Ignition_Speed_NPC` | NPC defaults to the former |
| `+0x35C` | `Gravity` | default **−9.8** |
| `+0x360` | `Launch_pitch_change_deg` | degrees → radians |
| `+0x364` | `Attached_Effect` | effect handle, −1 if absent (first load) |
| `+0x368` | `Creation_Effect` | effect handle, −1 |
| `+0x36C` | `Attached_Effect_Prop_Point` | heap string, first load only |
| `+0x370` / `+0x372` | `Fuse_Time` / `NPC_Fuse_Time` | `u16`; presence of `Fuse_Time` sets flag bit `0x10000`; NPC defaults to the plain fuse |
| `+0x374` | `Fade_Out_Time` (seconds) | `u16` ms (×1000); 0 if absent or negative |
| `+0x376` | `Projectile_Ignition_Delay_MS` | `s16` (i) |
| `+0x378` | `AI_Can_Guide` | `u16` (i), default 0 |
| `+0x37C` | `Mass` | `f32` (a) |
| `+0x380` / `+0x384` | `Linear_Damp` / `Angular_Damp` | `f32` (i), default 0 |
| `+0x388` / `+0x38C` | `Restitution` / `Friction` | `f32` (i), default 0.4 |
| `+0x390` | `Angular_Velocity` → `X`,`Y`,`Z` | vec3 (§1.3); **a wrapper named `Angular_Velocity` is required** |
| `+0x39C` | `Blow_Tire_Radius` | `f32` (i) |
| `+0x3A0` | `Ignition_Effect` | effect handle, −1 |
| `+0x3A4` | `Sound` | Wwise id, 0 |
| `+0x3A8` | `FoleyCollision` | foley record value (`FUN_00561370`), 0 |
| `+0x3AC` | `Projectile_Flags` → `Flag` | bit set, §2.4 |

The DLC rows place `X`/`Y`/`Z` and a `Foley_Name` **directly** under `Projectile_Info`; the reader asks for a wrapper `Angular_Velocity` and a child `FoleyCollision`, so both are not read (§16). The `items_3d` record's own field at `+0x40` is overwritten with 1.0 on first load.

**`Waterspray_info`** (`+0x444`…): `Water_stream_force` `+0x444`, `Refill_rate_per_Second` `+0x448`, `Radius_Expansion_Rate` `+0x44C` (all "always"), `Waterspray_Effect` `+0x45C` (CRC of name), `Pressure_Increase_Rate` `+0x450` (if present: sets flag-A bit `0x8000000` and reads `Pressure_Decrease_Rate` `+0x454`, `Pressure_Restore_Time` `+0x458`; if absent the bit is cleared).

**`Charge_Release_Info`** (`FUN_00B80D40`; zero if absent): `Charge_Time_sec` → `+0x460` = **1 ÷ seconds**; `Min_Charge_Percent` `+0x464` (default 1.0), `Charge_Base` `+0x468`, `Auto_Release` `+0x46C` (bool), `Min_Range` `+0x470` (default −1.0), `Pre_Charge_Delay` `+0x474`, `Charge_Cooldown_Time` → `+0x478` = 1 ÷ seconds (and flag word `+0x484` bit 1 set), `Charging_Camera_Shake` `+0x47C`, `Charged_Camera_Shake` `+0x480`, `Charge_Flags`/`Flag` = `Show HUD on charge` → `+0x484` bit 0.

**`Vehicle_Weapon`** (`FUN_00B7E670`; block base `+0x488`; children `Primary_Weapons` and `Alt_Weapons`, each a list of `Weapon`; **at most 4 each**). Primary components at `+0x488 + 0x50·i`, count at **`+0x5C8`**; alt components at `+0x5CC + 0x50·j`, count at **`+0x70C`**. **Component record (`FUN_00B7E3A0`, 0x50 bytes):** `+0x00` `UID` (`u32` "always"), `+0x04` `Weapon_Class` (23-name enum), `+0x08` flags (`Target reticule` = 1, `Enable physics` = 2), `+0x0C` `Muzzle_Explosion` (explosion pointer), then two 32-byte turret parts — `Middle_Component` at `+0x10`, `Top_Component` at `+0x30` — each: `+0x00` flags (`Don't reset angle when unmanned` = 1), `+0x04` `Min_Angle`, `+0x08` `Max_Angle`, `+0x0C` `Max_Speed`, `+0x10` `Damp_Angle`, `+0x14` `Unmanned_Speed` (all degrees → radians), `+0x18` `Firing_Angle_Speed` (degrees → radians; **defaults to `Max_Speed`**), `+0x1C` `Max_Force` (**× 4.448221683502197**, a pound-force → newton factor).

**`Camera_Info`** (`FUN_00B809A0`; intensities default 1.0; the names below in order): `Primary_Fire_Camera_Shake` `+0x710` (+ `_Intensity` `+0x714`), `Primary_Fine_Aim_Camera_Shake` `+0x718` (+ `Primary_Fire_Fine_Aim_Camera_Shake_Intensity` `+0x71C`), `Secondary_Fire_Camera_Shake` `+0x720` (+`_Intensity` `+0x724`), `Secondary_Fire_Fine_Aim_Camera_Shake` `+0x728` (+`…_Intensity` `+0x72C`), `Melee_Hard_Camera_Shake` `+0x730` (+`_Intensity` `+0x734`), `Melee_Soft_Camera_Shake` `+0x738` (+`_Intensity` `+0x73C`), `Player_Hit_Camera_Shake` `+0x740` (+`_Intensity` `+0x744`) — shake names resolve through `FUN_0057BEE0`; `Primary_Recoil_Multiplier` `+0x748`, `Primary_Fine_Aim_Recoil_Multiplier` `+0x74C`, `Primary_Recoil_Delay_ms` `+0x750` (`s32`), `Primary_Recoil_Ramped` `+0x754` (bool, default true), `Secondary_Recoil_Multiplier` `+0x758`, `Secondary_Fine_Aim_Recoil_Multiplier` `+0x75C`, `Zoom_Type` `+0x760` (`progressive` 0, `non-progressive` 1; written only if present), `Minimum_FOV` `+0x764`, `Maximum_FOV` `+0x768`, `Zoom_Steps` `+0x76C`, `FOV_Rate` `+0x770` (all "if present" floats).

**`Overheat_Info`** (zero if absent): `Percent_Increase_Per_Shot` `+0x798`, `Percent_Decrease_Per_Second` `+0x79C`, `Percent_Decrease_Per_Reload` `+0x7A0`, `Percent_Decrease_Per_Second_Overheated` `+0x7A4`, `Percent_Decrease_Per_Reload_Overheated` `+0x7A8`; `Overheat_Flags`/`Flag` → byte `+0x7AC`: `applies to primary` 1, `applies to alt fire` 2, `play reload anim` 4.

**`Effect_Situations`** (`FUN_00B7E300`; **positional**): the first four *children*, whatever their tag (the shipped rows use `Situation`, and the parent's tag is not checked), each read as `Situation` (enum, 25 names, §2.5) → `+0x7B0 + 8·i` and `Effect` (effect handle) → `+0x7B4 + 8·i`; all eight words start at −1. A fifth and later child is ignored.

### 2.4 The `Flags` vocabularies **[CONFIRMED — disassembly; machine-readable list `tools/ah_flags_w.txt`]**

Top-level `<Flags>`/`<Flag>` text is matched case-insensitively against **63 literals**, each setting one bit; unrecognised text is ignored. In addition, a weapon whose `Name` is `sp_gat01_w` gets flag-A bit `0x8` (unlimited ammo) by name. The record holds three words (`+0x10` A, `+0x14` B, `+0x18` C).

| Word A (`+0x10`) | | | |
|---|---|---|---|
| `0x1` alt unlimited ammo | `0x2` infinite magazine capacity | `0x4` heavy weapon move speed | `0x8` unlimited ammo |
| `0x10` other hand ik during attack only | `0x20` no combat ready | `0x40` no vehicle combat ready | `0x80` use block flinch anims |
| `0x100` on selection | `0x200` on trigger | `0x400` lethal melee | `0x800` bullets damage tanks |
| `0x1000` explosions damage tanks | `0x2000` melee can dislodge movers | `0x4000` one shot shatter | `0x8000` allow offhand grenade |
| `0x10000` attaches | `0x20000` brass during reload only | `0x40000` dual wieldable | `0x80000` can zoom |
| `0x100000` disallow forward throw in vehicle | `0x200000` has alt fire | `0x400000` melee continue on world collide | `0x800000` use modified bullet direction |
| `0x1000000` manually detonates | `0x2000000` revives humans | `0x4000000` secondary trigger fires grenades | `0x8000000` *(set by `Waterspray_info`, not a `Flag`)* |
| `0x10000000` secondary weapon | `0x20000000` use box shape for melee casts | `0x40000000` use underslung fine aim | `0x80000000` *(runtime "unlocked" bit, set/cleared by `crib_weapons.xtbl`, §13.1 — not a `Flag`)* |

| Word B (`+0x14`) | | | |
|---|---|---|---|
| `0x1` cutscene only | `0x2` disallowed in demos | `0x4` not allowed in vehicle | `0x8` not allowed with human shield |
| `0x10` unlockable | `0x20` disallow jumping | `0x40` disallow crouching | `0x80` disallow reload |
| `0x100` apply force to live ragdolls | `0x200` instant ragdoll | `0x400` causes convulsions | `0x800` left hand |
| `0x1000` always wear on back | `0x2000` do not hide when sprinting | `0x8000` always play melee vfx | `0x10000` constant vfx on combat ready only |
| `0x20000` constant vfx only at night | `0x40000` looping muzzle flash | `0x80000` use mission srid for effects | `0x100000` bullets can hit multiple humans |
| `0x200000` no blood splat | `0x400000` no random give | `0x800000` incendiary shots | `0x1000000` armor piercing override |
| `0x2000000` gibs victims | `0x4000000` zoom allows fine aim | `0x8000000` attach to forearm | `0x10000000` show reserve in hud |
| `0x20000000` drops with full reserve | `0x40000000` player instant reload | `0x80000000` melee always dislodge | |

Word C (`+0x18`): `0x1` no bullet decal, `0x2` manned turret, `0x4` = record loaded (set by the loader).

**`Projectile_Flags`** (24 literals → `+0x3AC`): `has light attached` `0x1`, `rocket flight` `0x2`, `sticky` `0x4`, `harpoon` `0x8`, `satchel charge` `0x10`, `guided` `0x20`, `guided on fine aim` `0x40`, `attach effect after ignition` `0x80`, `detonate on vehicle collision` `0x100`, `detonate on human collision` `0x200`, `use bullet collision quality` `0x400`, `orient projectile to velocity` `0x800`, `dont detonate from explosion` `0x1000`, `seek to target pos` `0x2000`, `seek to target pos (npc only)` `0x4000`, `does not fire from muzzle` `0x8000`, *(`0x10000` set by `Fuse_Time` presence)*, `swarm` `0x20000`, `vehicle rc` `0x40000`, `teleport to target` `0x80000`, `rc self destruct` `0x100000`, `rc military allowed` `0x200000`, `play attach sound on vehicles only` `0x400000`, `show hud indicator` `0x800000`, `genki` `0x1000000`.

### 2.5 Enumerations **[CONFIRMED — disassembly]**

- **`Weapon_Class`** (23 literals, index = stored value): `pistol` 0, `smg` 1, `rifle` 2, `shotgun` 3, `launcher` 4, `thrown` 5, `knife` 6, `nightstick` 7, `stungun` 8, `bat` 9, `sword` 10, `pimp slap` 11, `video camera` 12, `knuckles` 13, `flamethrower` 14, `cutscene only` 15, `man cannon` 16, `minigun` 17, `pepper spray` 18, `chainsaw` 19, `waterspray` 20, `script` 21, `vehicle` 22. (The same table is used by the vehicle-weapon components.)
- **`Inv_Slot`** (11): `unarmed` 0, `melee` 1, `pistol` 2, `smg` 3, `shotgun` 4, `rifle` 5, `explosive` 6, `special` 7, `vehicle` 8, `grenade` 9, `single_use` 10.
- **`Category`** (7): `WPNCAT_MELEE` 0, `WPNCAT_PISTOL` 1, `WPNCAT_SUB_MACHINE_GUN` 2, `WPNCAT_SHOTGUN` 3, `WPNCAT_RIFLE` 4, `WPNCAT_THROWN` 5, `WPNCAT_SPECIAL` 6.
- **`Trigger_Type`/`Alt_Trigger_Type`** (4): `single`, `burst`, `automatic`, `charge release`.
- **`Grenade_Type`** (4): `standard`, `stun`, `flashbang`, `molotov`.
- **`Special_Case_Type`** — a (value, name) table of 14 pairs; the stored value is the pair's first word (equal to its position): `RC Gun` 0, `Air Strike` 1, `Sonic Wave Gun` 2, `Riot Shield` 3, `Needler Prototype` 4, `Flak Cannon Prototype` 5, `Multi Launcher Prototype` 6, `Satellite Drone Prototype` 7, `Wieldable Prop Weapon` 8, `Avatar Sword` 9, `Heli Spotlight` 10, `Cyber Cannon` 11, `Flamethrower` 12, `Chum` 13; no match ⇒ −1.
- **`Effect_Situations`/`Situation`** (25): `muzzle flash` 0, `alt muzzle flash` 1, `tracer` 2, `alt tracer` 3, `bullet impact override` 4, `alt bullet impact override` 5, `overheat` 6, `player flashlight` 7, `npc flashlight` 8, `charge release charging muzzle` 9, `charge release charging ribbon` 10, `charge release charging target` 11, `laser cutter ribbon` 12, `laser cutter target near` 13, `laser cutter target far` 14, `laser guide ribbon` 15, `laser guide target` 16, `projectile` 17, `projectile ignite` 18, `projectile post ignition` 19, `projectile create` 20, `sonic hit effect` 21, `sonic scan charging` 22, `sonic scan charged` 23, `genki fire` 24.
- **`Constant_Effect`/`Condition`**: see §2.2.

### 2.6 Elements the reader never asks for, in real rows **[CONFIRMED — disassembly + case-insensitive whole-image scan]**

Of **199** distinct element tags in the 9 DLC rows, 7 are not among the reader's literals (§16): `_Editor` (read only by the weapon-upgrade loader, §4, never here), `Info_Slot_Index` (consumed by the DLC handler, §2), `Effect_Situation` (the per-situation *row tag*; the positional read makes its name irrelevant), `Projectile_Flags` (a false alarm of the automatic check: the literal sits in the helper `FUN_00B7DB30`, string at `0x01189D9C`), and **three with no literal anywhere in the image** (case-insensitive raw scan, exact and embedded): `Hit_Wall_Sound` and `Spinning_Snd_Pitch_End` (both under `Audio`) and `Foley_Name` (under `Projectile_Info`; the reader asks for `FoleyCollision`). The `Condition` text `always on` (DLC3) is a *value*, not an element (§2.2). **Independent cross-check:** each raw DLC weapons file carries its own `TableDescription` block declaring **376 elements**; only **five** of those names are not accounted for by the row reader — `Info_Slot_Index` (DLC handler), `Projectile_Flags` and `Effect_Situation` (false alarms above), and two that are **read nowhere** (no literal in the image, case-insensitive): **`Fire_Damage_Per_Second`** and **`Alt_Time_Management` → `npc_burst_time`**. The `Audio` children `Hit_Wall_Sound`/`Spinning_Snd_Pitch_End` are not even declared by that description — stale row data.

## 3. weapon_categories.xtbl

**Loader [CONFIRMED — disassembly].** `weapon_categories.xtbl` (literal `0x0118B2AC`, one xref at `0x00B85408`) is read by the first part of the weapon-subsystem initialiser `FUN_00B85400` (the same routine that then loads the weapons, §2). The row element is **`Categories`** (plural — the tag is literally that, one row per category). The reader accepts exactly one child, **`Name`**, which it reads twice:

1. as an **enum** against the seven `WPNCAT_*` literals of §2.5 (`FUN_00DAC830`) — the result *k* is the destination slot;
2. as **text**, handed to the localisation helper `FUN_0084A1B0(name, empty)`, which hashes the name (CRC, seed 0), checks the string table and, if the name is not there, registers a placeholder text entry; the returned handle is stored.

| Item | Value |
|---|---|
| Storage | array of `u32` at `0x028DC4E8`, allocated `4 × (number of Categories rows)`; row count at `0x028DC484` |
| Destination | `array[k]` where *k* is the enum index (0 `WPNCAT_MELEE` … 6 `WPNCAT_SPECIAL`) |
| Stored value | the category's display-name text handle (HIGH CONFIDENCE that it is the CRC of the row's `Name`; the helper returns it in a register the decompile does not show) |
| Validation | **none** — a `Name` that matches no `WPNCAT_*` literal gives *k* = −1 and the store lands one slot before the array; the loader trusts the data |

Nothing else in the row is read; the shipped table is therefore a seven-row list of category names whose only runtime effect is to attach a display string to each category. **[CONFIRMED — the whole loader body was read.]**

After the categories, `FUN_00B85400` also (a) calls the weapon loader (§2), (b) zeroes an `0x54`-byte block at `0x028DC488` and sets `0x028DC55C` to point at it with two constants (`0x028DC560` = 7, `0x028DC564` = 12 — i.e. 7 slots of 12 bytes, one per category by size; the block's use was not traced), (c) initialises a small list object at `0x0130F078`, and (d) calls the weapon-upgrade initialiser `FUN_00B7C5A0` (§4). No DLC archive carries a `weapon_categories.xtbl`, so this section has no empirical cross-check beyond the DLC weapons' `Category` values, which are all in the seven-name set (§16). *(Since validated against the real base rows: §18.5, 7/7.)*

## 4. weapon_upgrades.xtbl — per-weapon upgrade patches

**Loader [CONFIRMED — disassembly].** `weapon_upgrades.xtbl` (literal `0x011896B0`; xrefs `0x00B7C660` in the reload routine `FUN_00B7C680` and `0x00B7C689`, and the initialiser `FUN_00B7C5A0`) is read by **`FUN_00B7C380(filename, reload)`**, called at start-up from `FUN_00B85400` (§3) through `FUN_00B7C5A0` and again by the reload routine. The file is a list of **`Weapon_Upgrade`** rows; **each row is one upgrade of one weapon, expressed as a set of *patches* against that weapon's `weapons.xtbl` record** (§2). The row reader is `FUN_00B76BC0` (a 22 KB function; it is a long repetition of one pattern, described once below).

### 4.1 Row structure

| Element | Meaning / behaviour |
|---|---|
| `Name` | heap copy → upgrade record `+0x00` |
| `_Editor` → `Category` | **required.** The text after the *last* `:` is the **name of the weapon** the upgrade applies to (`Entries:Vehicle`-style editor category; here the tail is a weapon name); resolved with `FUN_00B81220`. A row whose weapon is not found is skipped. There is no fallback if `_Editor` is missing (the reader passes a NULL text to `strrchr`) |
| `upgrade_description` | text handle (`FUN_00849FA0`: CRC of the name if it exists in the string table, else 0) → upgrade record `+0x0C` |
| `upgrade_price` | `u32` "if present" → upgrade record `+0x10` |
| *override elements* | **any element name of the `weapons.xtbl` reader (§2), each optionally accompanied by a sibling `<name>_OM`** (list in §4.3) |

**Upgrade record** (pool at `0x028DC46C`, `0x14` bytes per row): `+0x00` heap name, `+0x04` pointer to a heap array of patch pointers, `+0x08` patch count, `+0x0C` description handle, `+0x10` price. **Per-weapon table** (`0x028DC474`, `0x2C` bytes per weapon, indexed like the weapons array): up to **10** upgrade-record pointers at `+0x00…+0x27`, count at **`+0x28`** (the loader does not bound the count — an eleventh upgrade for one weapon would overwrite it). The patch-pointer scratch area of one row is 1 KB, i.e. at most 256 patches per row.

**Upgrade order = row order** for a given weapon; a weapon's upgrade *level* *n* is bit *n* of the mask below.

### 4.2 The patch mechanism **[CONFIRMED — disassembly]**

For each override element the reader starts from the **base weapon record** (a scratch copy), parses the row's element with the *same* readers §2 documents (the "if present" flavours and the sub-block readers, so units are converted exactly as in §2), and — **only when the element is present** — builds a **patch record (0x14 bytes)** with helpers `FUN_00B76A40` / `FUN_00B769D0` (4-byte and 2-byte aligned copies):

| Patch offset | Field |
|---|---|
| `+0x00` `u16` | **destination offset inside the weapon record** (§2.2) |
| `+0x04` pointer | heap copy of the new bytes |
| `+0x08` `u16` | number of bytes |
| `+0x0C` `s32` | **operation mode** from the sibling `<name>_OM` (`FUN_00DAC830`-style match, case-insensitive) — table at `0x011888BC`: `OM_REPLACE` **−1**, `OM_ADDITIVE` **0**, `OM_MULTIPLICATIVE` **1**, `OM_REMOVAL` **2**; absent or unmatched ⇒ **−1** (replace) |
| `+0x10` `s32` | **value type** from a fixed per-field choice, table at `0x011888DC`: `ubyte` 0, `ushort` 1, `int` 2, `float` 3 (composite blocks and handles use −1 / their own size) |

Applying a patch (`FUN_00B76B20` with helpers `FUN_00B76600` / `FUN_00B76690`) to a weapon instance: mode 0 **adds** the value to the field, mode 1 **multiplies**, any other mode (−1, 2, composite types) **copies the bytes over** the destination; the additive/multiplicative forms are defined for `ubyte`/`ushort`/`int`/`float` fields only. (`OM_REMOVAL` has no arithmetic path of its own — it is treated as a byte copy of the value carried by the patch; **whether the shipped data uses it to mean "clear the field" was not determinable without the base rows** — OPEN; the 64 real base rows are now available (§18.5) but their `_OM` values were not counted there.)

The `Flags` element is handled specially: the three flag words (`+0x10`) of the scratch record are compared with the base's and, **if any differs**, one 12-byte patch at offset `0x10` carrying all three words is created (replace semantics).

**Weapon instances** (`0x028DC470`): a two-entry pointer table; each entry points to an array of `capacity × 0x7F8` bytes — the per-weapon *instance* used while playing, one array per player slot (index from `FUN_008ADFF0`, a byte value 0/1 — HYPOTHESIS: the two sets are the two co-op players; the function was not read). An instance is `{ u32 applied-upgrade bitmask ; 0x7EC-byte weapon-record copy ; 2 words }`. The current level of a weapon is the highest set bit (`FUN_00B76950`). The reload routine `FUN_00B7C680` re-parses the file, refreshes each instance's record copy from the base record, and re-applies every upgrade whose bit was set.

### 4.3 Override vocabulary and destination offsets **[CONFIRMED — disassembly; machine-readable `tools/ah_upg_offsets.txt` from `tools/harnesses/tbl_upgrade_offsets.py`]**

Every name below is accepted in a `Weapon_Upgrade` row together with its `_OM` companion. The destination offsets are **independent of the base-weapon reader** — they were taken from the patch constructors — and they **agree with every `weapons.xtbl` offset of §2.2 they overlap** (block sizes included: e.g. `Constant_Effects` 0x8C/0x34 = 4 records of 12 + the count; `Tracer_Info` 0x78/0x14; `Time_Management` 0x130/0x0E and `Alt_Time_Management` 0x13E/0x0E; `Fire_Cone_Metrics` 0x1AC/0x60; `Projectile_Info` 0x348/0x68; `Camera_Info` 0x710/0x64; `Charge_Release_Info` 0x460/0x28; `Overheat_Info` 0x798/0x18; `Target_Lockon` 0xE0/0x28; spread blocks 0x3C and 0x4C). That is an independent confirmation of the §2.2 layout. **[CONFIRMED — empirical/structural cross-check.]**

| Override element(s) → destination offset / size |
|---|
| `Display_Name_Override` → `+0x7D4` (text pointer) and `+0x7D8` (its handle) · `Description_Override` → `+0x7DC` · `Bitmap_Override` → `+0x7E0` · `Object_Item_Override` → `+0x58` |
| `Animation_Group` `+0x30` · `Fine_Aim_Animation_Group` `+0x34` · `Reload_Animation_Group` `+0x38` · `Strafe_Angles` `+0x48` · `Muzzle_Flash` `+0x60` · `Muzzle_Effect` `+0x64` · `Alt_Muzzle_Effect` `+0x68` · `Melee_Effect` `+0x6C` · `Fire_Cone_Impact_Effect` `+0x70` |
| `Tracer_Info` `+0x78` (0x14 bytes) · `Constant_Effects` `+0x8C` (0x34) · `Brass` `+0xC0` · `Sound_Radius` `+0xD8` · `Target_Lockon` `+0xE0` (0x28) |
| `Trigger_Type` `+0x108` · `Alt_Trigger_Type` `+0x10C` · `Ammo` `+0x110` · `Alt_Ammo` `+0x114` · `Magazine_Size` `+0x118` (`u16`) · `Range_Max` `+0x120` · `NPC_Aim_Drift` `+0x12C` |
| `Time_Management` `+0x130` (0x0E) · `Alt_Time_Management` `+0x13E` (0x0E) · `Damage_Max` `+0x14C` (0x0C) · `Damage_Min` `+0x158` (0x0C) · `Explosion` `+0x168` · `Underwater_Explosion` `+0x178` · `Damage_Max_Dist` `+0x17C` · `Damage_Min_Dist` `+0x180` |
| `Flat_Spread_Metrics` `+0x190` (0x14) · `Fire_Cone_Angle` `+0x1A4` · `Fire_Cone_Length` `+0x1A8` · `Fire_Cone_Metrics` `+0x1AC` (0x60) · `Shots_Per_Round` `+0x20C` (`u8`) |
| `PlayerWeaponSpread` `+0x210` (0x3C) · `PlayerAltWeaponSpread` `+0x24C` (0x3C) · `NPCWeaponSpread` `+0x288` (0x4C) · `NPCAltWeaponSpread` `+0x2D4` (0x4C) |
| `Ragdoll_Force_Shoot` `+0x320` · `Object_Bullet_Hit_Impulse_Magnitude` `+0x324` · `Ragdoll_Info` `+0x328` (0x20) · `Projectile_Info` `+0x348` (0x68) · `melee_material_effects` `+0x3B0` (0x84, or single entries at `+0x3B0 + 4·material`) |
| `melee_damage_to_anchored_scaler` `+0x434` · `vehicle_damage_scale` `+0x438` · `player_vehicle_damage_scale` `+0x43C` · `Melee_Attack_Info` `+0x440` · `Charge_Release_Info` `+0x460` (0x28) |
| `Camera_Info` `+0x710` (0x64) · `Burst_Fire_Info` `+0x774` (8) · `Override_Bullet_Impact_Effect` `+0x780` · `Alt_Override_Bullet_Impact_Effect` `+0x784` · `Penetrating_End_Point_Explosion` `+0x790` · `Alt_Penetrating_End_Point_Explosion` `+0x794` · `Overheat_Info` `+0x798` (0x18) · `Effect_Situations` one 8-byte patch per situation at `+0x7B0 + 8·i` |
| `Melee_Damage_Overrides` `+0x20` (0x10) · `Flags` `+0x10` (0x0C, all three words) |

Elements of §2 that are **not in this reader's vocabulary** (so cannot be patched): `Weapon_Class`, `Category`, `Inv_Slot`, `Grenade_Type`, `Special_Case_Type`, `Base_Version`, `Warmup_Delay`, `Cooldown_Delay`, `Reload_override_time_sec`, `Ammo_per_Shot`, `Ammo_Regeneration`, `AI_Ideal_Range_Min`/`Max`, `Offhand_Weapon_Mesh`, the `Audio` block (except `Sound_Radius`), `Alt_Explosion`, `NPC_Explosion`, `NPC_Alt_Explosion`, the `…_NPC` bullet-impact overrides, `Riot_Shield_Damage_Multiplier`, `Operator_Damage_Multiplier`, `Diversion_Kill_Multiplier`, `Waterspray_info`, `Vehicle_Weapon`, `Max_Melee_Impacts`, `Blood_Decal_Scale`/`Blood_Decal_Delay`, `NPC_Desired_Burst_Size`, `Wieldable_Prop_*`. **[CONFIRMED for "no such literal in the reader" — the reader body was read completely and its literal list extracted in full; a name assembled at run time cannot be excluded but none is built.]** No DLC archive carries a `weapon_upgrades.xtbl`, so §4 has no empirical row cross-check; the structural cross-check above stands in for it. *(Since validated against 64 real base rows: §18.5.)*

## 5. ammo.xtbl

**Loader [CONFIRMED — disassembly].** `ammo.xtbl` (literal `0x01187FA4`, one xref at `0x00B6F173`) is read by `FUN_00B6F170`, which calls the shared table-loader `FUN_00B6F0A0(&records, &count, "ammo.xtbl")`; that opens the file, counts the **`Ammo`** rows (literal `0x0113A374`), allocates `count × 0x30` bytes (record pointer `DAT_028CDC4C`, count `DAT_028CDC48`) and calls the per-row reader **`FUN_00B6E530`** for each. (A byte flag in the loader selects first-load vs refresh, as in §1.5: on refresh the row count must equal the loaded count or the reload is abandoned.) `FUN_00B6F170` then builds **two runtime ammo-state arrays** (`FUN_00B6E3B0`, one per iteration of a 2-pass loop, each entry 16 bytes: `{ pointer to the ammo record, 0, 0, 1.0 }`, the two arrays chained in a circular list at `DAT_028CDC44`); HIGH CONFIDENCE that they are per-player reserve/clip state — their fields were not traced. Resolver for the weapon table's `Ammo`/`Alt_Ammo` is `FUN_00B6E4E0` (§1.6).

### 5.1 The 0x30-byte ammo record **[CONFIRMED — disassembly]**

| Offset | Element | Type / reader | Default / notes |
|---|---|---|---|
| `+0x00` | `Name` | heap `char*` | first load only; the key for `FUN_00B6E4E0` (`_stricmp`) |
| `+0x04` | *(CRC of `Name`)* | `u32`, seed 0 | |
| `+0x08` | `Flags` → `Flag` | bit set | see below; word starts at 0 |
| `+0x0C` | `Max_in_Reserve` | `u32` "always" | |
| `+0x10` | `Inv_Slot` | `u8`, by `FUN_008DC6B0` against the same 11 names as the weapon `Inv_Slot` (§2.5) | **`0xFF` if absent or unmatched** |
| `+0x14` | *(zeroed)* | | not element-driven |
| `+0x18` | `Upgradable_Ammo` → `Weapon` | `u32` CRC of the weapon name | 0 if `Upgradable_Ammo` absent |
| `+0x1C` / `+0x20` / `+0x24` | `Upgradable_Ammo` → `Level_2_Ammo` / `Level_3_Ammo` / `Level_4_Ammo` | `u32` "always" | 0 if `Upgradable_Ammo` absent |
| `+0x28` | `store` → `cost` | `u32` "always" | left as allocated (not written) if `store` is absent |
| `+0x2C` | `store` → `clip_size` | `u32` "always" | same |

**`Flag` literals** (case-insensitive, first match wins; unrecognised text ignored): `drops with weapon` `0x1`, `thrown` `0x2`, `lethal` `0x4`, `bullet` `0x8`, `projectile` `0x10`, `fire` `0x20`, `water` `0x40`, `sewage` `0x80`, `incendiary` `0x100`, `penetrating` `0x200`, `laser` `0x400`, `armor piercing` `0x800`. The DLC weapons name ammo types such as `Bullet Rifle` (§16) — those rows live in the base `ammo.xtbl`, which has no raw copy in any DLC archive.

The `Upgradable_Ammo` triple is how ammo capacity grows with weapon upgrades (level 2/3/4 amounts for the named weapon — HIGH CONFIDENCE from the element names; the consuming code was not read). No DLC archive carries `ammo.xtbl`; §5 has no empirical row cross-check. *(Since validated against the 35-row patch copy: §18.5.)*

## 6. weapon_melee_attacks.xtbl

**Loader [CONFIRMED — disassembly].** `weapon_melee_attacks.xtbl` (literal `0x01189B8C`, one xref at `0x00B7D87A`) is read by **`FUN_00B7D870(refresh)`**, called first thing by the weapon loader (§2), before `weapons.xtbl` is opened. Rows are **`Melee_Attack_Set`** elements. First load: the row count sets `DAT_0130F040`, the array (`DAT_028DC4EC`) is `count × 0x24` bytes; refresh: sets are matched to existing records by name and re-read in place.

**Record (`0x24` bytes) [CONFIRMED — disassembly].** Every attack slot is the **index of a `MeleeMove` in `melee.xtbl`** (§7), obtained by name through `FUN_009828F0` (CRC, seed 0, then a linear scan of the move table by that CRC): **`0xFFFF` if the element is absent or names no move.**

| Offset | Element | Type |
|---|---|---|
| `+0x00` | `Name` | heap `char*` (the key the weapon's `Melee_Attack_Info` matches, §2.2) |
| `+0x04` | `StandingPrimary` | `u16` move index |
| `+0x06` | `StandingSecondary` | `u16` |
| `+0x08` | `MovingPrimary` | `u16` |
| `+0x0A` | `MovingSecondary` | `u16` |
| `+0x0C` | `Crouching` | `u16` |
| `+0x0E` | `CrouchMoving` | `u16` |
| `+0x10` | `ProneAttackPrimary` | `u16` |
| `+0x12` | `ProneAttackSecondary` | `u16` |
| `+0x14` | `Hard_Primary` | `u16` |
| `+0x16` | `Hard_Secondary` | `u16` |
| `+0x18` | `Non_Fancy_Primary` | `u16` |
| `+0x1A` | `Non_Fancy_Secondary` | `u16` |
| `+0x1C` | `StandingSynced` | `u16` |
| `+0x1E` | `MovingSynced` | `u16` |
| `+0x20` | `Target_Search_Range` | `f32` "always" |

A weapon whose `Melee_Attack_Info` is absent or names no set uses the built-in default record at `0x0130F04C` (§2.2); the same lookup is what `weapon_upgrades` patches at weapon offset `+0x440` (§4). The reader touches exactly these 15 element names besides the row's `Name`; the whole body was read. No DLC archive carries this table.

## 7. melee.xtbl and melee_transition_states.xtbl

Two tables, both loaded from `FUN_00983490` / `FUN_00981DE0`, describe melee attacks and the states that chain them. `weapon_melee_attacks.xtbl` (§6) points into `melee.xtbl`.

### 7.1 `melee.xtbl` — the `MeleeMove` record

**Loader [CONFIRMED — disassembly].** `melee.xtbl` (literal `0x0117390C`, one xref at `0x009834AD`) is read by **`FUN_00983490(refresh)`**. It first calls `FUN_0095DE40(refresh)`, which loads a sibling table, `anim_synced.xtbl` (rows `SyncedMove`, `0x30`-byte records at `DAT_02624B50`, CRC of `Name` at `+0x10`, row reader `FUN_0095DAA0` — *outside this group, not decoded here*), then opens `melee.xtbl`. Rows are **`MeleeMove`**. First load: the row count sets `DAT_0262BA3C`; the array (`DAT_0262BA38`) is `count × 0x11C` bytes; each record's `Name` is copied inline and its CRC (seed 0) stored at `+0x20` in a first pass, then the row reader **`FUN_00982920(record, row)`** fills the rest. Two per-animation-group tables are also allocated: `DAT_0262BA48` (a `u16` counter per animation group, cleared on every load) and `DAT_0262BA44` (`0x28` `u16` slots = `0x50` bytes per animation group). Refresh: rows are matched to records by CRC. Move lookup by name is `FUN_009828F0` → `FUN_00982780` (linear scan on the CRC at `+0x20`; `0xFFFF` if none).

**Record (`0x11C` bytes) [CONFIRMED — disassembly].**

| Offset | Element | Type / notes |
|---|---|---|
| `+0x00` | `Name` | inline string, then CRC at `+0x20` (`u32`) |
| `+0x24` | `AttackAnim` | animation-state index (`FUN_004BF810`: pointer-and-`_stricmp` scan of the animation-state table `DAT_03171C10`, stride `0x10`, count `DAT_03171C08`; **−1 if absent/unmatched**) |
| `+0x28` | `SyncedMove` | `u16` index into `anim_synced.xtbl` by CRC (`FUN_0095DA50`); `0xFFFF` if absent/unmatched (unmatched names are counted at `0x0262BA40`) |
| `+0x30` / `+0x34` | `Impact` → `ImpactFX` / `Impact_Human_Effect` | effect handles, −1 if absent |
| `+0x38` | *(hard-coded)* | the effect named `Melee_blood`, always looked up |
| `+0x3C…+0x47` | `Impact` → `ImpactDir` → `X`,`Y`,`Z` | vec3, **normalised on load** (a near-zero vector becomes (1,0,0)) |
| `+0x48` | `Impact` → `ImpactForce` | `f32` "always" |
| `+0x4C` / `+0x50` / `+0x54` | `DefaultDamage` → `NPCDamage` / `PlayerDamage` / `OnlineDamage` | `f32` "always" |
| `+0x58` | `Ragdoll_Getup_Time_ms` | `s32` (if present), default 0 |
| `+0x5C` | `Explosion` | `u32` CRC of the name; absent ⇒ `DAT_029C9964` (0) — **a hash, not a resolved pointer** (unlike the weapon table) |
| `+0x60…+0xEF` | `Active_Attack_Infos` → children (positional, **max 6**) | 6 entries × `0x18` bytes, initialised to `{CRC("") , 0, −1, −1, 0, 0}`. Entry: `+0x00` `QTE_HUD` (CRC of the text), `+0x04` `Success_Extra_Damage` (`f32`), `+0x08` `Success_Effect`, `+0x0C` `Fail_Effect` (effect handles, −1), `+0x10` `Effect_Tag` (interned through a string pool of capacity 200 named for melee effect tags), `+0x14` `Effect_on_Victim` (`u8` bool). If at least one child exists the flag bit `0x2000000` is set |
| `+0xF0` / `+0xF4` | `Impact` → `AttackLimb` / `AttackLimbNPC` | enum, 6 names: `Right hand` 0, `Left hand` 1, `Right foot` 2, `Left foot` 3, `Both hands` 4, `Weapon` 5; NPC defaults to the player value; **−1 if a present element is unmatched; 0 if absent** |
| `+0xF8` | flags | below |
| `+0xFC` | `Impact` → `Impact_reaction_time` | `s32` (if present), else 0 |
| `+0x100` / `+0x104` / `+0x108` | `Impact` → `Impact_reaction_reverse_speed` / `Impact_reaction_speed_ramp` / `Impact_reaction_blend_out_time` | `f32` (if present), else 0 |
| `+0x10C` | `Combo` → `End_Pose` | index of a transition state (§7.2, by `_stricmp` on its name); −1 if `End_Pose` is absent. **Only assigned when `Combo` exists** (see below) |
| `+0x110` / `+0x114` | `Blood_Effect` → `Effect` / `Repeat_Cooldown` | effect handle (−1) / `f32` (default −1.0) |

**Flag word `+0xF8`** (case-insensitive literals):
- `Processing_flags`/`Flag`: `anim state attack` `0x1`, `two-handed attack` `0x20`, `dont_play_weapon_sound` `0x40`, `unblockable` `0x80`, `allow synced incapacitated victim` `0x400000`, `attacker should not flinch` `0x1000000`. The element `Testicular_Assault` (bool, row level) sets `0x800000`.
- `Attack_is_for`/`Flag`: `Hard hit (LT)` `0x2`, `Victim is Blocking` `0x4`, `Victim is Crouched` `0x8`, `Victim is Prone/Ragdolled` `0x10`, `Attacker is Player Only` `0x1000`, `Attacker is Homie Only` `0x2000`, `Attacker is Brute` `0x4000`, `Attacker is Killbane` `0x8000`, `Attacker is Avatar` `0x10000`, `Attacker is Running` `0x20000`, `Attacker is Walking` `0x40000`, `Attacker is Sprinting` `0x80000`, `Requires Previous Move Hit` `0x100000`, `Victim is Not Ally` `0x200000`.
- `Attack_Results`/`Flag`: `Kill victim` `0x100`, `Ragdoll Victim` `0x200`, `Ragdoll Victim from behind` `0x400`, `Disarm Victim` `0x800`.
- `0x2000000` also set by `Active_Attack_Infos` having children (above).

**`Combo`** (optional row child): `Anim_group_grid` → repeated `Anim_group_ref` — each ref's own text names an animation group; the move's index is appended to that group's slot list in `DAT_0262BA44` (the guard allows one slot too many: `< 0x29` against a 0x28-slot row — HIGH CONFIDENCE off-by-one, benign only while data stays under 40). `Start_Pose` names a transition state (§7.2) into whose list of *entry moves* this move's index is appended (state record `+0x34…`, count at `+0x84`, max 0x28). `End_Pose` → record `+0x10C`. If `Combo` is absent `+0x10C` is left as it was (zero on a fresh array — HIGH CONFIDENCE the array is zero-filled; the allocation flags were not decoded).

### 7.2 `melee_transition_states.xtbl` — the `Melee_Transition_State` record

**Loader [CONFIRMED — disassembly].** Literal `0x01173480`, one xref at `0x00981DE9`, read by **`FUN_00981DE0(refresh)`**: rows **`Melee_Transition_State`**; count `DAT_0262BA50`, array `DAT_0262BA4C`, **stride `0x88`**. On refresh a table with more rows than the loaded count is refused. Fields **[CONFIRMED — disassembly, whole reader body read]**:

| Offset | Element | Type |
|---|---|---|
| `+0x00` | `Name` | inline string, 0x20 bytes copied (bounded) |
| `+0x20` | `Animation_State` | animation-state index (`FUN_004BF810`, −1 if unmatched) |
| `+0x24` | `Return_Action` | animation-state index; −1 if absent |
| `+0x28` | `Player_Combo_Time_ms` | `s32` "always" |
| `+0x2C` / `+0x30` | `NPC_Combo_Min_Time_ms` / `NPC_Combo_Max_Time_ms` | `s32` "always" |
| `+0x34…+0x83` | *(filled by `melee.xtbl`)* | up to 0x28 `u16` move indices whose `Combo/Start_Pose` names this state |
| `+0x84` | *(count of the above, zeroed by this loader)* | |

The table is cross-referenced from §7.1 (`Start_Pose`/`End_Pose`); the loader order (transition states must exist before `melee.xtbl` registers into them) is respected by the caller, which was not traced. No DLC archive carries either table.

## 8. aim_drift.xtbl

**Loader [CONFIRMED — disassembly].** `aim_drift.xtbl` (literal `0x01187E88`, one xref at `0x00B6C5D5`) is read by **`FUN_00B6C5D0`**, the first thing the weapon loader does (§2). The table is a list of **`Profile`** rows; each profile is the NPC aim-error model that a weapon selects with its `NPC_Aim_Drift` element (§2.2, resolver `FUN_00B6C320`, name compared with `_stricmp`; a weapon without a match uses the profile named `Default`).

| Item | Value |
|---|---|
| Storage | `0x028CD2B8`, **24 profiles × `0x48` bytes** (`0x6C0` bytes, zeroed at load); count at `0x028CDBD4`; the loader stops after the 24th accepted row |
| Name | interned in a 600-byte string pool named for aim-drift profile names; the pointer is stored at `+0x00` |

**Record (`0x48` bytes) — row reader `FUN_00B6C370` [CONFIRMED — disassembly]. Every float is an "always" read.**

| Offset | Element | Notes |
|---|---|---|
| `+0x00` | `Name` | pooled string pointer |
| `+0x04` | `turn_speed` | row level |
| `+0x08` | `Bullet_miss` → `Aiming` → `Close_range` | |
| `+0x0C` | … `Close_accuracy` | |
| `+0x10` | … `Far_range` | |
| `+0x14` | … `Far_accuracy` | |
| `+0x18` | … `Bounces_per_sec` | |
| `+0x1C` | … `Speed_multipler` | **spelled so** in the reader (the misspelling is the element name) |
| `+0x20` | `Bullet_miss` → `Recovery` → `recover_penalty` | default 1.0 if `Recovery` is absent |
| `+0x24` | … `recover_time` | default **500.0** |
| `+0x28` | … `bullets_to_unsteady` | default **−1.0** |
| `+0x2C` | `Bullet_miss` → `Firing` → `start_burst_box_pct` | |
| `+0x30` | `Explosive_Miss` → `explosive_z_offset` | |
| `+0x34` | … `explosive_y_offset` | |
| `+0x38` | … `lead_pct` | **÷ 100** (percentage → fraction) |
| `+0x3C` | … `explosive_error_radius_min` | |
| `+0x40` | … `explosive_error_radius_max` | |
| `+0x44` | … `explosive_error_flat` | `u8` bool "always" |

The parents `Bullet_miss`, `Aiming`, `Firing` and `Explosive_Miss` are **not** null-checked (an "always" read under a missing parent parses stack garbage, §1.3), so a well-formed profile must carry all four. No DLC archive carries this table (the DLC weapons name profiles such as `VehicleMGTurret`, which live in the base file).

## 9. aim_assist.xtbl

**Loader [CONFIRMED — disassembly].** `aim_assist.xtbl` (literal `0x01176794`, one xref at `0x009EB23E`) is read by the initialiser `FUN_009EB000`. It first writes defaults into **five global blocks**, then reads the table: rows are **`Aiming`**, matched by their `Name` (case-insensitive) to a block; **rows with any other name are skipped**, and a row is applied to its block by `FUN_009EACE0`.

| `Name` text | Block address |
|---|---|
| `Normal` | `0x0130B100` |
| `Combat Ready` | `0x0130B1A0` |
| `Fine Aim` | `0x0130B240` |
| `Tank Skydiving` | `0x0130B2E0` |
| `Zoomed` | `0x0130B380` |

**Block layout (`0xA0` bytes) [CONFIRMED — disassembly].** Child names are case-insensitive; every float is an "always" read except where noted, so an absent element keeps the default written at start-up (zero unless stated).

| Offset | Element | Default |
|---|---|---|
| `+0x00` | `Name` (32 bytes copied) | |
| `+0x20` | `steering` → `steer_amount_x` | 0 |
| `+0x24` | … `steer_amount_y` | 0 |
| `+0x28` | … `max_steer_amount_x` | 0 |
| `+0x2C` | … `max_steer_amount_y` | 0 |
| `+0x30…+0x3F` | `steering` → `dist` → `near_dist` — **one float replicated into four words** (`FUN_00DACE40`) | 0 |
| `+0x40` / `+0x44` | … `fade_in_rate` / `fade_out_rate` | 0 |
| `+0x48` / `+0x4C` | … `Capsule_inner_multiplier` / `Capsule_outer_multiplier` | 1.0 |
| `+0x50` | … `Use_Cylinder` (bool) | true |
| `+0x60` / `+0x64` | `slowing` → `slow_mult_x` / `slow_mult_y` | 1.0 |
| `+0x70…+0x7F` | `slowing` → `dist` → `near_dist` (×4) | 0 |
| `+0x80` / `+0x84` | … `fade_in_rate` / `fade_out_rate` | 0 |
| `+0x88` / `+0x8C` | … `Capsule_inner_multiplier` / `Capsule_outer_multiplier` | 1.0 |
| `+0x90` | … `Use_Cylinder` (bool) | true |

A block has two independent parts — a *steering* pull towards the target and a *slowing* of the reticle near it — each with a capsule-shaped acquisition distance model (`dist`). The reader's whole body (`FUN_009EACE0`, `FUN_009EAC80`, `FUN_009EAC20`) was read; those are all the elements it asks for. No DLC archive carries this table.

## 10. explosions.xtbl and continuous_explosions.xtbl

### 10.1 `explosions.xtbl`

**Loader [CONFIRMED — disassembly].** The literal `explosions.xtbl` (`0x0111D46C`) has three code xrefs: the base initialiser `FUN_00590B50` (which calls the table loader with `EAX` = the literal and the framework name `main`), the DLC handler `FUN_00590DA0`, and a preload pass `FUN_00ACF740` (see below). The table loader is **`FUN_00590630(filename, framework, first)`**: opens the file, and for each **`Explosion`** row whose `Framework` (default `main`) matches `framework` (`_stricmp`) calls the row reader **`FUN_00590050(record, row)`** and increments the count at `0x013CF504`; `first` zeroes the count. Records are stored at `0x013CF5C0`, **stride `0xEC`**, keyed by CRC (seed 0) of the name **stored at record `+0x20`** (resolver `FUN_00590D20` → `FUN_00590CE0`, §1.6). No capacity check exists in the loader (the backing region's extent was not determined — OPEN).

**DLC handler [CONFIRMED — disassembly].** `FUN_00590DA0(packages)` is registered as the `explosions` handler of the DLC content registry (§15.2). For each package whose flags say it carries DLC data (byte `+0x101` non-zero and not `0xFF`, and bit 1 of byte `+0x103`) it builds the file name with `FUN_0045BA50` — **`<framework>_explosions.xtbl`** (`"%s_%s"`, 0x40-character buffer; the plain name when package flag bit 2 is set or the framework is empty) — and calls `FUN_00590630(name, package+0x80, 0)`, i.e. the same row reader with the package's framework string. This is how `dlcN_explosions.xtbl` rows are appended. **The same three-step pattern (registry entry → `FUN_0045BA50` → the table's row loader with the package framework) is used by the fifteen DLC handlers that call `FUN_0045BA50` (§15.2); the weapon handler adds a slot-index step (§2).**

**Preload pass.** `FUN_00ACF740` re-opens `explosions.xtbl` and, for every `Explosion` row that has an `Effect` child, registers `(Name, heap copy of the Effect text)` with a resource-preload list — nothing is stored in the explosion record.

**Record (`0xEC` bytes) [CONFIRMED — disassembly; "always" = §1.3 always-reader, "if" = if-present reader]:**

| Offset | Element | Type / reader | Default / behaviour |
|---|---|---|---|
| `+0x00` | `Name` | bounded copy, 0x20 bytes | |
| `+0x20` | *(CRC of `Name`)* | `u32` | |
| `+0x24` | `Panic_Reaction` | id by `FUN_004EEE40` (name → id, table not decoded) | |
| `+0x28` | `Radius` | `f32` always | |
| `+0x2C` | `Decal_Radius_Override` | `f32` if | defaults to `Radius` |
| `+0x30` | `Cone_Angle` | `f32` if, degrees → radians | default **−1.0** (no cone) |
| `+0x34` | `FireRadius` | `f32` if | 0 |
| `+0x38` | `AI_Sound_Radius` | `f32` always | |
| `+0x3C` / `+0x40` | `Damage_Min` / `Damage_Max` | `u32` always | |
| `+0x44` / `+0x48` | `Damage_Min_Player` / `Damage_Max_Player` | `u32` always | |
| `+0x4C` | `Player_Vehicle_Damage_Scalar` | `f32` always | |
| `+0x50` | `Impulse` | `f32` always | |
| `+0x54` | `Redundant_Effect_Distance` | `f32` if — **stored squared**; `+0xDE` = 1 when present | 0 |
| `+0x58` | `Effect` | effect handle (`FUN_005C50B0`) from a 0x40-byte bounded copy | −1 if absent |
| `+0x5C` / `+0x60` / `+0x64` | `Sticky_Fire` → `Cone_Spread_Effect` / `Circular_Spread_Effect` / `Large_Sticky_Effect` | effect handles | −1 (all −1 if `Sticky_Fire` absent) |
| `+0x68` | *(count)* | number of `Small_Sticky_Effects` → `Effect` children read | 0; **the loop is not bounded** but the array holds 4 |
| `+0x6C…+0x9B` | `Sticky_Fire` → `Small_Sticky_Effects` → repeated `Effect` | **array of 12-byte records** `{ +0 effect handle from `Sticky_Effect`, +4 `Min` (u32 always), +8 `Max` (u32 always) }` — capacity 4 | |
| `+0x9C` | `Sticky_Fire` → `Always_Produce` | `u8` = text equals `yes` (case-insensitive; **only `yes`, not `true`**) | 0 |
| `+0xA0` | `Groundfire` | handle from `FUN_005FD830` (name lookup, table not decoded) | 0 if absent |
| `+0xA4…+0xB8` | `Screen_Effects` → `Delay_Time`, `Ramp_Up_Time`, `Full_Strength_Duration_Time`, `Decay_Time`, `Atten_Start`, `Atten_End` | `f32` always (six consecutive words) | only if `Screen_Effects` present |
| `+0xBC…` | `Screen_Effects` → `Tone` → `Tint` | colour read by `FUN_00DAD160` (12-byte region up to `+0xC7`; the shipped rows write `R`,`G`,`B` integers 0–255; component semantics not decoded); its return sets `+0xD0` bit 0 | |
| `+0xC8` | `Screen_Effects` → `Tone` → `Tint_scale` | `f32` if; sets `+0xD0` bit 1 | |
| `+0xCC` | `Screen_Effects` → `Tone` → `Saturation` | `f32` if; sets `+0xD0` bit 2 | |
| `+0xD0` | — | flag byte: bit 0 `Tint`, bit 1 `Tint_scale`, bit 2 `Saturation`, bit 3 `Locked_Attenuation` (bool under `Screen_Effects`) | |
| `+0xD4` | `Refraction_Screen_Effect` | `u32` CRC of the name | |
| `+0xD8` | `Flags` → `Causes Electric Ragdoll` | `u8` | 0 |
| `+0xD9` | *(present)* | 1 if `Screen_Effects` exists | 0 |
| `+0xDA` / `+0xDB` | `Flags` → `Causes Player Tinnitus` / `Causes Vomit` | `u8` | 0 |
| `+0xDC` / `+0xDD` | `Flags` → `Penetrates World` / `No Ragdoll` | `u8` | 0 |
| `+0xDE` | *(Redundant_Effect_Distance present)* | `u8` | 0 |
| `+0xE0` | `Camera_Shake_Info` → `Camera_Shake` | shake-table entry (`FUN_0057BEE0`) | 0 if the block is absent |
| `+0xE4` / `+0xE8` | `Camera_Shake_Info` → `Camera_Shake_Maximum_Intensity` / `Camera_Shake_Radius` | `f32` always | 0 if the block is absent |

Flag literals (case-insensitive, first match wins): `Causes Player Tinnitus`, `Causes Electric Ragdoll`, `Causes Vomit`, `Penetrates World`, `No Ragdoll`. Before the flag loop the reader zeroes `+0xD8…+0xDB` (one dword), `+0xDC…+0xDD` (one word) and `+0xDE`, so all five flag bytes start at 0 on every (re)load.

### 10.2 `continuous_explosions.xtbl` **[CONFIRMED — disassembly]**

The literal (`0x011260E4`) is referenced by the loader `FUN_005EB2F0` (through a one-word pointer stored at `0x012EC8E0`) — it is the *first* entry of a small static table: `0x012EC8E0` file-name pointer, then the three **target-type** names `default` (`0x012EC8E4`), `in_car`, `in_aircraft`. Rows are **`Continuous_explosion`**, **at most 3 accepted**, stored at `0x014A0310`, **stride `0x64`**, count at `0x014A02F0`. The row reader `FUN_005EAE80` **rejects the whole row (returns failure, so it is not counted)** on any failed range check below.

| Offset | Element | Rule |
|---|---|---|
| `+0x00` | `Name` | CRC (seed 0), the name text is not kept; a row without `Name` is rejected |
| `+0x10` | `Explosion_Data` → `Explosion` | explosion pointer (§10.1 resolver); **must resolve** |
| `+0x04` | … `Explosion_Num` | `s32`, **1…3** |
| `+0x08` / `+0x0C` | … `Spread_Min` / `Spread_Max` | integer degrees, each **0…90**, `Min ≤ Max`; stored in radians |
| `+0x18` / `+0x1C` | … `Cooldown_min` / `Cooldown_max` | seconds (`f32`), each ≥ 0, `min ≤ max`; stored as **milliseconds** (×1000, integer) |
| `+0x28` / `+0x2C` | `Approach_Info` → `Approach_Angle_Min` / `Approach_Angle_Max` | integer degrees 0…90, `Min ≤ Max`; radians |
| `+0x24` | … `Approach_Dist` | `f32` in **[0, 150]** |
| `+0x30` | … `Launch_Dist_Min` | `f32` ≥ 0, **stored squared** |
| `+0x34…+0x63` (+ flags at `+0x40/+0x50/+0x60`) | `Target_Info` → children (positional): `Target_Type` ∈ {`default` 0, `in_car` 1, `in_aircraft` 2}, `Radius_Min`, `Radius_Max`, `Target_FOV` | per target type *t* (16-byte slots): used-flag byte `+0x40 + 16t` (set to 1), `Radius_Min` `+0x34 + 16t`, `Radius_Max` `+0x38 + 16t`, `Target_FOV` `+0x3C + 16t` (integer degrees ≤ 360, stored as **half-angle radians**); each radius in [0, 500], `Radius_Max ≥ Radius_Min`; **a duplicate or unknown `Target_Type` rejects the row**; the `default` type must be present |
| `+0x14` | `Audio_Info` → `Whiz_Sound` | Wwise id, 0 if absent; **`Audio_Info`, `Approach_Info`, `Explosion_Data` and `Target_Info` (with a `default` target) are effectively required** (the row is rejected without them) |
| `+0x20` | … `Avg_Length` | `f32` if, 0 |

The table drives a scripted "continuous explosion" effect (an explosion spawned repeatedly around a target, e.g. the `Whiz` sound and approach geometry suggest a passing-shell barrage — HYPOTHESIS about purpose; every number above is from the reader). No DLC archive carries this table.

## 11. combat_actions.xtbl and combat_tricks.xtbl

Two data-driven tuning tables for the AI/combat layer. Both are read once at start-up into fixed static structures.

### 11.1 `combat_actions.xtbl`

**Loader [CONFIRMED — disassembly].** `combat_actions.xtbl` (literal `0x01116AA8`, one xref at `0x004F2659`) is read by **`FUN_004F2650`**. Rows are **`actions`** elements (that is the literal row tag). The engine keeps a **built-in table of 70 combat actions** (plus a 71st, `change mode`, with a different layout and not matchable) at **`0x012E1C60`, 24 bytes per entry**; a row's `Name` is matched case-insensitively against the entry names and **tunes that entry in place**; a name that matches nothing sets a global "bad action name" flag (`0x01361970`) and is otherwise ignored. So the table adds no actions — it only overrides four timings and a precondition mask per built-in action.

**Built-in entry layout (`0x18` bytes) [CONFIRMED — disassembly + static image]:** `+0x00` name pointer, `+0x04` action category id (`u32`, code-side, in the table below), `+0x08` handler function pointer, `+0x0C` `u16` `Min_repeat_time` (static default **0**), `+0x0E` `u16` `Auto_abort_ms` (**15000**), `+0x10` `u16` `No_interrupt_ms` (**0**), `+0x12` `u16` `refresh_desire_interval` (**300**), `+0x14` `u8` static (0, except 1 for `cover stay down`), `+0x15` `u8` precondition mask (static 0). Every one of the 70 matchable entries carries the same four static defaults, listed here so an absent element is understood.

| Row element | Destination | Notes |
|---|---|---|
| `Name` | match key | |
| `Min_repeat_time` | `+0x0C` `u16` | `s32` "always" read, truncated to 16 bits |
| `Auto_abort_ms` | `+0x0E` `u16` | |
| `No_interrupt_ms` | `+0x10` `u16` | |
| `refresh_desire_interval` | `+0x12` `u16` | **−1 is stored as `0xFFFF`** (never refresh) |
| `precondition` → `pre_flags` → `Flag` | `+0x15` `u8` | bitmask against five literals (`FUN_00DAC740`): `on foot` `0x01`, `in vehicle` `0x02`, `in water` `0x04`, `must have target` `0x08`, `can do in cover` `0x10` |

**The 70 built-in action names** (grouped by their category id; the category id and the handler pointers are static data of the engine, not read from the XML):

| Category id | Action names (in the built-in table) |
|---|---|
| 0 | `idle` |
| 3 | `brute throw prop`, `brute car flip`, `brute bull rush`, `brute qte player`, `brute attack brute`, `brute gun finisher`, `avatar shockwave`, `avatar stomp`, `avatar fireballs`, `avatar teleport stomp`, `avatar bullrush` |
| 5 | `hold position`, `melee stand back`, `move retreat`, `move regroup`, `move advance`, `move chase`, `avatar chase`, `move surround`, `move rush`, `move rush to melee`, `move scripted`, `move to navmesh`, `move sidestep`, `move to post combat`, `move to post idle`, `move close in`, `killbane chase`, `killbane strafe` |
| 6 | `quick kill` |
| 7 | `cover popout` |
| 8 | `cover take def`, `cover take off`, `cover advance`, `cover fire` |
| 8 | `cover stay down` *(cover stay down carries static byte +0x14 = 1)* |
| 12 | `fire`, `fire sweep`, `fire suppress`, `fire chaos` |
| 14 | `follow make way`, `follow avoid LOF`, `follow formation`, `follow acquire`, `follow player`, `follow teleport`, `follow lemming`, `follow to car`, `follow other car`, `follow browse` |
| 15 | `throw grenade` |
| 18 | `follow carjack`, `vehicle enter scpt`, `vehicle extract` |
| 21 | `human shield grab` |
| 22 | `investigate move`, `investigate look`, `investigate peek` |
| 23 | `melee`, `melee vehicle` |
| 25 | `vehicle passenger` |
| 26 | `pickup weapon` |
| 27 | `reload` |
| 30 | `roller blader change` |
| 35 | `taunt`, `avatar taunt` |
| 36 | `throw weapon` |
| 42 | `zombie eat`, `zombie explode` |

No DLC archive carries this table.

### 11.2 `combat_tricks.xtbl`

**Loader [CONFIRMED — disassembly].** `combat_tricks.xtbl` (literal `0x0113675C`, one xref at `0x006A1742`) is read by **`FUN_006A1730`**, a method of the combat-tricks manager object. The file has a single **`Combat_Tricks`** row (the first one is used). Time values are **seconds in the file; the engine stores milliseconds by multiplying by 1000 and truncating toward zero** (the conversion runs with the FPU in truncate mode — the disassembly sets the round-toward-zero control bits, although the decompile prints `ROUND`).

| Row element | Destination (global) | Behaviour |
|---|---|---|
| `Default_Duration` | local default | `f32` always; used only if > 0, else **5000 ms** |
| `Record_Display_Time` | `0x014C28A0` | `f32` if; clamped ≥ 0; ms; default 0 |
| `Record_Queue_Time` | `0x014C28A4` | `f32` if; clamped ≥ 0; ms; default 0 |
| `Record_Threshold` | `0x014C28A8` | `f32` always; clamped ≥ 0; **÷ 100** |

Then **one child per trick type, 18 in this fixed order** (the reader looks the child up by name; a missing child leaves that trick unconfigured): `Gang_Kill`, `Gang_Vehicle_Kill`, `One_Hit_Kill`, `Human_Shield_Kill`, `Head_Shot_Kill`, `Nut_Shot_Kill`, `Throwing`, `Multi_Kill`, `Specialist_Kill`, `Brute_Beat_Kill`, `Brute_Kill`, `STAG_Kill`, `Explosive_Kill`, `Testicular_Assault`, `Sprint_Attack`, `STAG_Vehicle_Kill`, `Heli_Or_Vtol_Kill`, `Tank_Kill` (trick id = index 0…17). Each child yields a **7-field descriptor** handed to that trick's handler object through a virtual call: trick id; `Duration` (`f32` if, seconds → ms; **default = `Default_Duration`**); `Max_Respect` (`s32` always); `Max_Lifetime_Respect` (`s32` always); `Max_Cash` (`f32` always); `Min_Value`, `Max_Value` (`f32` always); and, **for `Multi_Kill` only**, `Kill_Duration` (seconds → ms). Each trick also has a fixed localisation key in the engine (`DIVERSION_COMBAT_TRICKS_<NAME>`, e.g. `..._GANG_KILL`, `..._TESTICLE_KILL` for `Testicular_Assault`, `..._HELI_KILL` for `Heli_Or_Vtol_Kill`) and a fixed numeric id in the static table at `0x01136540` (five dwords per trick: element name, key, id, two spare `s32`). The meaning of `Min_Value`/`Max_Value` was not traced (the handlers were not read). No DLC archive carries this table.

## 12. weapon_tracers.xtbl and weapon_tracer_materials.xtbl

### 12.1 `weapon_tracers.xtbl`

**Loader [CONFIRMED — disassembly].** The literal `weapon_tracers.xtbl` (`0x0111E6D4`) has two code xrefs: the base initialiser `FUN_005A2E30` and the DLC handler `FUN_005A2F10` (registry entry `tracers`, §15.2 — it builds `<framework>_weapon_tracers.xtbl` with `FUN_0045BA50`, exactly as §10.1). Both call **`FUN_005A2BC0(filename, framework, ctx, refresh)`**. Rows are **`Weapon_Tracers`**, framework-filtered like §1.5; the loaded count is `0x014033D4`; **capacity 20 records** (`< 0x14`, checked — extra rows are dropped), stored at **`0x014033E0`, stride `0x2C`**. On refresh (`refresh` non-zero) rows are matched to records by CRC of the name and the loader aborts with the diagnostic *"refresh failed, you either added, removed, or renamed an entry"* if a name is not found.

**Record (`0x2C` bytes) [CONFIRMED — disassembly]:**

| Offset | Element | Type / reader | Default |
|---|---|---|---|
| `+0x00` | `Name` | heap `char*` (first load) | |
| `+0x04` | *(CRC of `Name`, seed 0)* | `u32` — the key used by the resolver `FUN_005A2FD0` (§1.6) | |
| `+0x08` | `Effect` | effect handle (`FUN_005C50B0`); a row without `Effect` passes NULL to the resolver | |
| `+0x0C` | `Emitter_Effect` | effect handle | −1 if absent |
| `+0x10` | `Max_Particles` | `s32` always | |
| `+0x14` | `Lifetime` | `f32` always | |
| `+0x18` | `Ricochet` → `Chance` | `f32` if | 0 (zeroed first, so all three ricochet fields are 0 without a `Ricochet` element) |
| `+0x1C` | `Ricochet` → `Distance` | `f32` if | 0 |
| `+0x20` | `Velocity_Scale` | `f32` always | |
| `+0x24` | `Ricochet` → `Ricochet_velocity_scale` | `f32` always (inside `Ricochet`) | 0 |
| `+0x28` | `Hot_Length_Size` | `f32` if | **−1.0** |

A weapon selects a tracer with `Tracer_Info` → `Tracer` / `Tracer_NPC` / `Alt_Tracer` / `Alt_Tracer_NPC` (§2.2) and the tracer's `Tracer_Frequency` (`+0x88`) sets how often one is drawn. **Real-data note:** two of the four DLC rows write `<Chance>` **directly under the row**, not inside a `Ricochet` wrapper; the reader only looks inside `Ricochet`, so that value is ignored and the field stays 0 (the shipped value is `0.0`, so nothing changes). Empirically validated against the DLC archive's four rows and its `TableDescription` (12 declared elements, all accounted for — §16).

### 12.2 `weapon_tracer_materials.xtbl`

**Loader [CONFIRMED — disassembly].** Literal `0x0111E5EC`, one xref at `0x005A2806`, read by **`FUN_005A2800`** from the tracer initialiser (§12.1). A table of **33 `f32` dampeners at `0x01403348`** — one per physical-material name (the same 33-name table the weapon `melee_material_effects` uses, `FUN_006F76F0`, §1.6) — is first filled with 1.0. Rows are **`Tracer_Material`**: `Name` (physical-material name; **an unmatched name maps to index 31**, so it silently overwrites that material's slot) and `Tracer_Dampener` (`f32`, "always" — the code presets 1.0 in the local variable, but the always-reader overwrites it with the parse of an empty scratch buffer when the child is missing, §1.3; supply the element). The result is stored at `0x01403348 + 4·index`. No DLC archive carries this table.

## 13. crib_weapons.xtbl, store_weapons.xtbl, store_weapon_lightset.xtbl

### 13.1 `crib_weapons.xtbl` **[CONFIRMED — disassembly]**

Literal `0x011264BC`, one xref at `0x005EE598`, read by **`FUN_005EE590`**. Structure: `Crib_Weapons` → `Weapons_List` → repeated **`Entry`**. Each entry reads `Weapon` (a weapon name, resolved with `FUN_00B81220`; **the result is used without a NULL test — an unknown name would fault**) and `Unlocked` (bool "always"). The effect is to **set or clear bit 31 (`0x80000000`) of the weapon's flag word A (weapon record `+0x10`, §2.2)**, i.e. the runtime "unlocked" bit — the crib's starting weapon inventory. This bit is not one of the 63 `Flag` literals of §2.4; among the loaders read in this pass it is set only from here. No DLC archive carries this table.

### 13.2 `store_weapons.xtbl` **[CONFIRMED — disassembly]**

The literal sits in a small static descriptor table at `0x01300A2C` (a code xref at `0x00817FA0` reads it, and the descriptor table itself is a data reference): the descriptor holds the file name and the element names the loader uses, in order — `Store_Weapons`, `Weapons_List`, `Entry`, `Mission`, `Num_Hoods`, `Weapon`, `Angle`, `Distance`, `Offset`, `Unlocked`, followed by ammo names (`Bullet Pistol`, `Bullet SMG`, `Bullet Shotgun`, …). The loader **`FUN_00817F90`** reads **`Store_Weapons` → `Weapons_List` → repeated `Entry`** and uses only three children of each entry:

| Child | Stored where | Behaviour |
|---|---|---|
| `Mission` | entry record (16 bytes at `0x022CE91C`, count `0x022CE924`): two dwords | if the mission name resolves (`FUN_004588F0`), the game has any missions loaded, and the mission's gating flags allow (byte `+0x33` bit 4 clear; the mission-type table entry's flag bit 2 set; `FUN_00853B10` false), the pair `(mission +0x08, +0x0C)` is stored; otherwise the pair from two static words at `0x0115E758` |
| `Weapon` | same record `+0x08` | weapon record pointer (`FUN_00B81220`; NULL if unmatched) |
| `Unlocked` | second array (8 bytes per entry at `0x022CE920`): `+0x00` CRC of the weapon name, `+0x04` byte bit 0 = `Unlocked` (bool "always") | |

`Num_Hoods`, `Angle`, `Distance` and `Offset` are in the same descriptor table but **are not read by this loader** (their consumers, presumably the store's display code, were not traced — OPEN). The descriptor pattern is the same one `continuous_explosions.xtbl` uses (§10.2). No DLC archive carries this table.

### 13.3 `store_weapon_lightset.xtbl` — not a weapon-schema table **[CONFIRMED — disassembly]**

Literal `0x0115E7D8`, one xref at `0x0081630A`, in **`FUN_008162F0`**. This routine does **not** parse the file itself: it asks the *lightset cache* (`FUN_005990C0`: CRC of the name, then a scan of the 16-byte-per-entry lightset cache at `0x013DE2C8`, falling back to the lightset loader `FUN_005984C0`) for the lightset called `store_weapon_lightset.xtbl`. **The document schema is therefore the shared lightset schema** (the same one the `*-lightset.xtbl` files in the DLC archives use — e.g. `dlc1_gb_in-lightset.xtbl`), whose reader belongs to the environment/lighting group and is not decoded here. `FUN_008162F0(store)` then **overwrites each loaded light** (`0x70`-byte records; count at lightset `+0x08`, records at lightset `+0x04`): the light's position vector (record `+0x30`, 16 bytes) is recomputed from the store object's position (or from the entity referenced at store `+0x2098`/`+0x209C`), with a fixed offset added when neither is present, and the 48 bytes at record `+0x40` are set from static constants at `0x012490C0`…`0x012490EC`. Practical meaning: the file supplies the *number and types* of lights; the game places them relative to the weapon display at run time. **[The lightset schema itself: OPEN here — belongs to another group.]**

## 14. strafe_angles, taunting, hostage, windshield_cannon

Four small tables, each read by one short loader into fixed globals or a small static array. Conventions as in §11.2: seconds in the file → milliseconds in the engine (×1000, truncated toward zero); percentages ÷ 100; degrees → radians ×π/180.

### 14.1 `strafe_angles.xtbl` **[CONFIRMED — disassembly]**

Literal `0x01171610`, one xref at `0x00955F07`, read by **`FUN_00955F00`**. Rows are **`Strafe_Angles`**; records at **`0x02624390`, stride `0x64`**, count at `0x02624388` (**no capacity check** — the extent of the region was not determined, OPEN). A row named `Default` (case-insensitive) stores its index at `0x01309860` — the set used when a weapon names none. A weapon selects a set through its `Strafe_Angles` element (§2.2, resolver `FUN_009561B0`, `_stricmp` on the inline name).

| Offset | Element | Notes |
|---|---|---|
| `+0x00` | `Name` | bounded copy, 0x40 bytes |
| `+0x40` | `Forward` | `f32` always, degrees → radians |
| `+0x44` | `Right` | |
| `+0x48` | `Backward_Right` | |
| `+0x4C` | `Backward` | |
| `+0x50` | `Backward_Left` | |
| `+0x54` | `Left` | |
| `+0x58` | `Use_Turn_Limits` present | dword 1/0 |
| `+0x5C` | `Use_Turn_Limits` → `Left_Limit` | stored as **−(360 − value)°** in radians; 0 if `Use_Turn_Limits` is absent |
| `+0x60` | `Use_Turn_Limits` → `Right_Limit` | degrees → radians; 0 if absent |

### 14.2 `taunting.xtbl` **[CONFIRMED — disassembly]**

Literal `0x0113988C`, one xref at `0x006C3070`, read by **`FUN_006C3060`**: a single **`Taunting`** row into globals starting at `0x014C76D0`.

| Element | Global | Rule |
|---|---|---|
| `Max_Taunts` / `Min_Taunts` | `0x014C76D0` / `0x014C76D4` | `s32` always; if `Max < 1` or `Max < Min`, **both become 0** |
| `Aggro_Taunt_Value` | `0x014C76D8` | `s32`, at least 1 |
| `Death_Taunt_Value` | `0x014C76DC` | `s32`, at least 1 |
| `Death_Taunt_Delay` | `0x014C76E0` | seconds, ≥ 0 → ms |
| `Max_Time_Between_Taunts` | `0x014C76E4` | seconds, ≥ 0 → ms |
| `Death_Taunt_Distance` | `0x014C76E8` | `f32`, ≥ 0, **stored squared** |
| `Taunt_Complet_Percent` (**sic**) | `0x014C76EC` | percentage clamped to [0, 100], ÷ 100 (a value above 100 becomes 100 %) |
| `Award_Multiplier` | `0x014C76F0` | `f32`, at least 1.0 |
| `First_Taunt_Cash` | `0x014C76F4` | `f32`, ≥ 0 |
| `First_Taunt_Respect` | `0x014C76F8` | `s32`, ≥ 0 |
| `Max_Lifetime_Respect` | `0x014C76FC` | `s32`, ≥ 0 |

### 14.3 `hostage.xtbl` **[CONFIRMED — disassembly]**

Literal `0x01138020`, one xref at `0x006B6321`, read by **`FUN_006B6310`**: a single **`Hostage`** row. `Min_Notoriety` (`s32` → `0x012F03E0`; **a value outside 0…5 becomes 3**), `Max_Lifetime_Respect` (`s32` ≥ 0 → `0x014C7190`), then `Vehicle_Classes` → repeated **`Vehicle_Class`**: `Class_Name` ∈ {`Compact` 0, `Sedan` 1, `Luxury` 2, `Exotic` 3, `SUV` 4, `Truck` 5} (`FUN_006B6250`; the same six names as the vehicle's `Hostage_Vehicle_Class`, `spec-vehicle-data.md` §7.3; an unmatched or absent name skips the class) → `Difficulty_Levels` → repeated **`Difficulty_Level`**:

| Element | Stored |
|---|---|
| `Num_Hostages` | **must be 1, 2 or 3** (else the level is ignored); selects the slot; **the first definition of a slot wins** |
| `Min_Evasion_Time` | seconds ≥ 0 → ms |
| `Max_Evasion_Time` | seconds, raised to at least `Min_Evasion_Time` → ms |
| `Notoriety_Per_Sec` | `s32`, negative → 0 |
| `Respect` | `s32`, negative → 0 |
| `Cash` | `f32`, negative → 0 |

Storage: `0x014C71C0`, **18 slots × `0x18` bytes**, slot index `(Num_Hostages − 1) + 3 × class`; slot: `+0x00` `Num_Hostages` (0 = unused), `+0x04` Min evasion ms, `+0x08` Max evasion ms, `+0x0C` notoriety/second, `+0x10` respect, `+0x14` cash. **A level with `Respect` < 1 and `Cash` ≤ 0 is erased back to "unused"** (a hostage grab that pays nothing is not a defined reward).

### 14.4 `windshield_cannon.xtbl` **[CONFIRMED — disassembly]**

Literal `0x01139C20`, one xref at `0x006C605B`, read by **`FUN_006C6050`**: a single **`Windshield_Cannon`** row into globals at `0x014C7760`. `Max_Distance` `0x014C7760` and `Min_Distance` `0x014C7764` (`f32` always; **both forced to 0 if `Max ≤ 0` or `Max < Min`**); `Record_Threshold` `0x014C7768` (`f32` always, ≥ 0, ÷ 100); `Record_Display_Time` `0x014C776C` and `Record_Queue_Time` `0x014C7770` (`f32` if present, ≥ 0, seconds → ms, default 0); `Max_Respect` `0x014C7774` and `Max_Lifetime_Respect` `0x014C7778` (`s32`, ≥ 0); `Max_Cash` `0x014C777C` (`f32`, ≥ 0). The three `Record_*` elements are the same trio the combat-tricks table uses (§11.2): the on-screen "record" of the stunt. No DLC archive carries these four tables.

## 15. Literal-and-loader index, DLC content registry, and cross-table reference map

### 15.1 The 22 filename literals and their loaders **[CONFIRMED — disassembly + raw scan]**

Every literal below was verified by a raw scan of the executable as an exact NUL-delimited string, and its cross-references were listed with Ghidra (`tools/ah_fn_xrefs.txt`). "Xrefs" counts code cross-references visible to the analysis; **code that the analysis had not disassembled (function-pointer targets such as the DLC handlers) does not appear**, which is why a DLC handler's use of a literal can be missing from the counts (see §2). Section numbers refer to this document.

| Table | Literal VA | Xrefs (from function) | Loader | § |
|---|---|---|---|---|
| `weapons.xtbl` | `0x01184E58` | 3 (`FUN_00B834C0`, `FUN_00AD0180`, `FUN_00B834C0`) | `FUN_00B834C0` → `FUN_00B83390` → `FUN_00B82200`; DLC `FUN_00B835E0` | 2 |
| `weapon_categories.xtbl` | `0x0118B2AC` | 1 (`FUN_00B85400`) | `FUN_00B85400` | 3 |
| `weapon_upgrades.xtbl` | `0x011896B0` | 2 (`FUN_00B7C5A0`, `FUN_00B7C680`) | `FUN_00B7C380` → `FUN_00B76BC0` | 4 |
| `weapon_melee_attacks.xtbl` | `0x01189B8C` | 1 (`FUN_00B7D870`) | `FUN_00B7D870` | 6 |
| `melee.xtbl` | `0x0117390C` | 1 (`FUN_00983490`) | `FUN_00983490` → `FUN_00982920` | 7 |
| `melee_transition_states.xtbl` | `0x01173480` | 1 (`FUN_00981DE0`) | `FUN_00981DE0` | 7 |
| `ammo.xtbl` | `0x01187FA4` | 1 (`FUN_00B6F170`) | `FUN_00B6F170` → `FUN_00B6F0A0` → `FUN_00B6E530` | 5 |
| `combat_actions.xtbl` | `0x01116AA8` | 1 (`FUN_004F2650`) | `FUN_004F2650` | 11 |
| `combat_tricks.xtbl` | `0x0113675C` | 1 (`FUN_006A1730`) | `FUN_006A1730` | 11 |
| `aim_assist.xtbl` | `0x01176794` | 1 (`FUN_009EB000`) | `FUN_009EB000` → `FUN_009EACE0` | 9 |
| `aim_drift.xtbl` | `0x01187E88` | 1 (`FUN_00B6C5D0`) | `FUN_00B6C5D0` → `FUN_00B6C370` | 8 |
| `weapon_tracers.xtbl` | `0x0111E6D4` | 2 (`FUN_005A2E30`, `FUN_005A2F10`) | `FUN_005A2BC0`; DLC `FUN_005A2F10` | 12 |
| `weapon_tracer_materials.xtbl` | `0x0111E5EC` | 1 (`FUN_005A2800`) | `FUN_005A2800` | 12 |
| `store_weapons.xtbl` | `0x0115E740` | 1 code (`FUN_00817F90`) + 1 data (`0x01300A2C`) | `FUN_00817F90` | 13 |
| `store_weapon_lightset.xtbl` | `0x0115E7D8` | 1 (`FUN_008162F0`) | lightset loader (shared), `FUN_005990C0` | 13 |
| `crib_weapons.xtbl` | `0x011264BC` | 1 (`FUN_005EE590`) | `FUN_005EE590` | 13 |
| `explosions.xtbl` | `0x0111D46C` | 3 (`FUN_00590B50`, `FUN_00590DA0`, `FUN_00ACF740`) | `FUN_00590630` → `FUN_00590050`; DLC `FUN_00590DA0` | 10 |
| `continuous_explosions.xtbl` | `0x011260E4` | 1 code (`FUN_005EB2F0`) + 1 data (`0x012EC8E0`) | `FUN_005EB2F0` → `FUN_005EAE80` | 10 |
| `strafe_angles.xtbl` | `0x01171610` | 1 (`FUN_00955F00`) | `FUN_00955F00` | 14 |
| `taunting.xtbl` | `0x0113988C` | 1 (`FUN_006C3060`) | `FUN_006C3060` | 14 |
| `hostage.xtbl` | `0x01138020` | 1 (`FUN_006B6310`) | `FUN_006B6310` | 14 |
| `windshield_cannon.xtbl` | `0x01139C20` | 1 (`FUN_006C6050`) | `FUN_006C6050` | 14 |

### 15.2 The DLC content registry **[CONFIRMED — disassembly + static data; new finding, useful to every table group]**

The DLC handlers that append `dlcN_<table>.xtbl` rows are **not called directly**: they are function-pointer words in a static registry of **21 entries, 5 dwords each, from `0x0118D1F8`** — `{ handler name (string pointer), 0, load function, unload function, 0 }` — which is why the DLC handlers have no callers in a cross-reference listing. A handler receives an object holding an array of *package* pointers (array pointer at `+0x00`, count at `+0x08`). A package record supplies: the **framework name string at `+0x80`** (`dlc1`, `dlc2`, `dlc3`); byte `+0x101` (non-zero and not `0xFF` ⇒ the package is present); byte `+0x103` — **bit 1 (`0x02`) "carries DLC table data"**, **bit 2 (`0x04`) "use unprefixed file names"**. The file name is built by `FUN_0045BA50` as **`<framework>_<table>.xtbl`** (unless bit 2 is set or the framework string is empty). Registry contents (handler name → load / unload):

| Handler | Load | Unload | | Handler | Load | Unload |
|---|---|---|---|---|---|---|
| `localization` | `0x0084A0F0` | `0x0084A170` | | `characters` | `0x00BE0030` | `0x00BDE980` |
| `effect` | `0x005C5160` | `0x005C5380` | | `level_objects` | `0x008E84C0` | `0x0000006A` *(not a code address)* |
| `items` | `0x008FD120` | `0x008FD340` | | `missions` | `0x006CFBD0` | `0x006CFC60` |
| `ui` | `0x007B10C0` | `0x007B0A00` | | `homies` | `0x007E7580` | `0x007E6430` |
| `tracers` | `0x005A2F10` | `0x005A2BB0` | | `cutscene` | `0x00721AC0` | `0x00721B40` |
| `explosions` | `0x00590DA0` | `0x00590E20` | | `unlockables` | `0x0071EF40` | `0x0071DD00` |
| **`weapon`** | **`0x00B835E0`** | **`0x00B837E0`** | | `contacts` | `0x0083B940` | `0x0083B9D0` |
| `tweak` | `0x0071B450` | `0x00754410` | | `achievements` | `0x00714430` | `0x007144C0` |
| `refraction_sitations` *(sic)* | `0x0059F7B0` | `0x0059F830` | | `audio` | `0x00554770` | `0x00554340` |
| `vehicle` | `0x00ACE890` | `0x00AC98A0` | | `cloth_sim` | `0x007414D0` | `0x00740140` |
| `customize_item` | `0x00822540` | `0x008226E0` | | | | |

A second group of registry entries follows (`weapon_key`, `vehicle_key`, `customize_composite_key`, `customize_item_key`, `cheat_key`, `homies_key`, `mission_key`, `customize_category_key`, `unlockable_edit`), each with three function pointers — presumably per-family key/lookup helpers; not decoded. Fifteen of the load handlers call `FUN_0045BA50` with the literal filename of the table they extend (`refraction_situations`, `achievements`, `tweak_table`, `unlockables`, `ui_images`, `homies`, `customization_items`, `customization_outfits`, `contacts_sr3`, `level_objects`, `items_3d`, `character_definitions`/`character`/`char_cust_cats`, `explosions`, `weapon_tracers`); the vehicle handler is `FUN_00ACE890` (`spec-vehicle-data.md` §7.1); the weapon handler is described in §2.

### 15.3 Cross-table reference map (which table's field names which other table's rows)

| From (table.field) | To | How resolved |
|---|---|---|
| `weapons.Animation_Group`, `Fine_Aim_Animation_Group`, `Reload_Animation_Group`; `melee.Combo/Anim_group_ref` | animation-group name array (`DAT_03519838`, count `DAT_03171C1C`) | `_stricmp`, index (`FUN_004CCE70`) |
| `weapons.Strafe_Angles` | `strafe_angles.xtbl` (§14.1) | `_stricmp`, index (`FUN_009561B0`) |
| `weapons.NPC_Aim_Drift` | `aim_drift.xtbl` profiles (§8) | `_stricmp` (`FUN_00B6C320`) |
| `weapons.Ammo`, `Alt_Ammo`; `ammo.Upgradable_Ammo/Weapon` | `ammo.xtbl` (§5); `weapons` (by CRC) | `_stricmp` (`FUN_00B6E4E0`); CRC |
| `weapons.Melee_Attack_Info` | `weapon_melee_attacks.xtbl` sets (§6) | `_stricmp` |
| `weapon_melee_attacks.*` (14 slots) | `melee.xtbl` `MeleeMove` (§7.1) | CRC of name → move index (`FUN_009828F0`) |
| `melee.Combo/Start_Pose`, `End_Pose` | `melee_transition_states.xtbl` (§7.2) | `_stricmp` (`FUN_00981D90`) |
| `melee.AttackAnim`, `melee_transition_states.Animation_State`, `Return_Action` | animation-state table (`DAT_03171C10`) | pointer-and-`_stricmp` (`FUN_004BF810`) |
| `melee.SyncedMove` | `anim_synced.xtbl` | CRC (`FUN_0095DA50`) |
| `weapons.Explosion`, `Alt_Explosion`, `NPC_*`, `Underwater_Explosion`, `Penetrating_End_Point_Explosion`, `Vehicle_Weapon/…/Muzzle_Explosion`; `continuous_explosions.Explosion_Data/Explosion` | `explosions.xtbl` (§10.1) | CRC (`FUN_00590D20`) |
| `melee.Explosion` | `explosions.xtbl` | **CRC only, stored unresolved** |
| `weapons.Tracer_Info/*` | `weapon_tracers.xtbl` (§12.1) | CRC (`FUN_005A2FD0`) |
| `weapon_tracer_materials`, `weapons.melee_material_effects` | the 33 physical-material names | `_stricmp` (`FUN_006F76F0`) |
| `weapons.*_Effect`, `Constant_Effect/Effect`, `Effect_Situations/*/Effect`, `Projectile_Info/*_Effect`, `melee.*Effect*`, `explosions.Effect`, `weapon_tracers.Effect` | `effects.xtbl` (other group) | CRC hash map (`FUN_005C50B0`) |
| `weapons.Name` | `items_3d.xtbl`, `items_inventory.xtbl` (same name) | CRC (`FUN_00904C10`), `_stricmp` (`FUN_008DCB10`) |
| `weapons.Projectile_Info/Model` | `items_3d.xtbl` | CRC |
| `weapons.Camera_Info/*_Camera_Shake`, `Charge_Release_Info/*_Camera_Shake`; `explosions.Camera_Shake_Info` | camera-shake table | multiply-33 hash (`FUN_0057BEE0`) |
| `weapons.Brass` | brass records (`0x013CF220`) | `_stricmp` (`FUN_0058BF30`) |
| `weapons.Audio/*`, `Projectile_Info/Sound`, `Whiz_Sound` | audio middleware event / bank names | string → id (`FUN_00462960`) |
| `weapon_upgrades._Editor/Category` | `weapons.xtbl` `Name` | text after the last `:` |
| `weapon_categories.Name`, `weapons.Category` | the seven `WPNCAT_*` names | enum |
| `crib_weapons.Entry/Weapon`, `store_weapons.Entry/Weapon` | `weapons.xtbl` | `_stricmp` (`FUN_00B81220`) |
| vehicle entry `Weapon_Link` (`spec-vehicle-data.md` §7.3, `+0xAD0`) | `weapons.xtbl` | `FUN_00B81220` (callers in the vehicle code, e.g. `FUN_00AC9060`) |
| `hostage.Vehicle_Class/Class_Name` | the vehicle `Hostage_Vehicle_Class` enum | six literals |
| save-file weapon inventory ids (`spec-save-format.md` §10.3) | `items_inventory` record table (`0x0250ADC0`, 52-byte stride) | CRC (seed 0) of the item name — §16 |

## 16. Validation against real DLC rows and independent cross-checks

**Sources.** The DLC archives (`dlc1/2/3.vpp_pc`) store their entries raw, so their tables are trustworthy and touch nothing of the parked container question. The weapon-group tables present are: `dlc2_weapons.xtbl` (6 rows), `dlc3_weapons.xtbl` (3 rows), `dlc2_weapon_tracers.xtbl` (4 rows), `dlc1/2/3_explosions.xtbl` (7 rows in total). All 142 DLC `.xtbl` files were extracted with `tools/harnesses/tbl_dlc_list.py` into `tools/ah_dlc_xtbl/`; the weapon-group ones are the only ones used. Harnesses: `tbl_weapons_validate.py` (output kept in `tools/ah_validate_weapons.txt`), `tbl_expl_validate.py`, `tbl_desc_compare.py`, `tbl_exe_lit.py`, `tbl_save_weapon_ids.py`, `tbl_upgrade_offsets.py`. The predicates were written down before the first run; two predicates (`Projectile_Flags` path, `Effect_Situations` child tag) were **harness mistakes** — they looked for tags the real data does not use — and were corrected and re-run, as noted.

### 16.1 `weapons.xtbl` — 9 real rows **[CONFIRMED — empirical]**

| Predicate | Result |
|---|---|
| `Weapon_Class` ∈ the 23 literals · `Inv_Slot` ∈ the 11 · `Category` ∈ the 7 `WPNCAT_*` · `Trigger_Type`, `Alt_Trigger_Type` ∈ the 4 · `Grenade_Type` ∈ the 4 | **9/9 · 9/9 · 9/9 · 9/9 and 9/9 · 1/1** |
| every top-level `<Flags>/<Flag>` text ∈ the 63 flag literals | **51 / 51** |
| every `Projectile_Info/Projectile_Flags/Flag` ∈ the 24 projectile literals *(first run looked under a wrong path and matched nothing; corrected)* | **18 / 18** |
| every `Effect_Situations` child's `Situation` ∈ the 25 literals; ≤ 4 children *(first run used the wrong child tag; corrected)* | **10 / 10; 9 / 9** |
| `Constant_Effect` count ≤ 4 · `Primary_Weapons` ≤ 4 · `Alt_Weapons` ≤ 4 (the capacities of §2.2/§2.3) | **9/9 · 9/9 · 9/9** |
| `Constant_Effect/Condition` recognised | **0 / 1** — the one real value is `always on`, which the reader maps to condition 0 (the "anything else" path, §2.2) |
| vehicle-weapon components: `Weapon_Class` ∈ 23 literals; component flags ∈ {`Target reticule`, `Enable physics`}; `Min_Angle ≤ Max_Angle` | **3/3 · 3/3 · 6/6** |
| float-typed scalars parse under the engine float grammar (12 element names) | **all** (`Range_Max`, `AI_Ideal_Range_*`, `Ammo_Regeneration`, `Riot_Shield_Damage_Multiplier`, `Ragdoll_Force_Shoot` 9/9 each; `Object_Bullet_Hit_Impulse_Magnitude` 5/5; `vehicle_damage_scale` 4/4; `Damage_Max_Dist` 1/1 …) |
| `Magazine_Size`, `Ammo_per_Shot` are plain integers; `Magazine_Size` fits `u16` | **9/9, 9/9; 9/9** |
| `Is_DLC` ∈ {`True`, `False`}; `Framework` present and `dlcN` | **9/9; 9/9** |
| element vocabulary: distinct element tags in the 9 rows | **199**, of which **192** are reader literals and 7 are not (§2.6) |
| the files' own `TableDescription` (376 declared elements) vs the reader | **371 accounted for**; the 5 others explained in §2.6 (`Info_Slot_Index` DLC handler; `Projectile_Flags`, `Effect_Situation` false alarms; `Fire_Damage_Per_Second` and `Alt_Time_Management/npc_burst_time` read nowhere) |
| first-row `Info_Slot_Index` of each DLC file vs the DLC handler (§2) | **82** (6 rows) and **89** (3 rows), both within a 12-slot DLC region above a base count of 82 |

**Elements present in the real rows at a level the reader never inspects** (so their values are dead data — a re-implementation reproducing the shipped game must *not* honour them): `Movement_Multiplier_*` outside a `Spread…Multipliers` wrapper (**4 of 23**); `X`/`Y`/`Z` directly under `Projectile_Info` instead of under `Angular_Velocity` (**6**, i.e. 2 rows × 3); `min`/`max` directly under `Time_Management` (**14**); `Audio/Hit_Wall_Sound`, `Audio/Warmup_Delay`, `Audio/Spinning_Snd_Pitch_End`; `Projectile_Info/Foley_Name`.

### 16.2 `explosions.xtbl` and `weapon_tracers.xtbl` **[CONFIRMED — empirical]**

- **Explosions, 7 real rows (dlc1/2/3):** every `Flags/Flag` text is one of the five literals (**2/2**); every numeric field (`Radius`, `Damage_*`, `Impulse`, 35 values) parses (**35/35**). Element vocabulary: 41 distinct tags, all reader literals except the editor block (`_Editor`, `Category`) and the colour components `R`/`G`/`B` (read inside `Tint` by `FUN_00DAD160`, whose literals the decompile does not show). Each file's `TableDescription` declares **45 elements — 44 accounted for; the one exception, `Screen_Effects/Blur_Radius`, belongs to a different table** (`dof_situations.xtbl`, `FUN_0058EC80`), the only reader of that literal.
- **Tracers, 4 real rows (dlc2):** the vocabulary is exactly the reader's (`Name`, `Effect`, `Max_Particles`, `Lifetime`, `Velocity_Scale`, `Hot_Length_Size`, `Chance`, `Framework`, plus the editor block); the `TableDescription` declares **12 elements, all accounted for**. **Two rows put `Chance` at row level; the reader wants it inside `Ricochet`** (§12.1) — dead data, harmless because the value is 0.0.

### 16.3 Name-hash convention — save-file cross-check **[CONFIRMED — empirical]**

The weapon-definition ids in the 16 real save samples (19 distinct `u32`s, `spec-save-format.md` §10.3) are matched against the CRC-32 of executable strings (`tbl_save_weapon_ids.py`, `tools/ah_idmatch.py`): with **seed 0, input lower-cased, no final XOR — the convention of §1.4** — `dildo_bat` = `0xF9890F00`, `Grenade` = `0xF78C3CCF`, `molotov` = `0x24BF8E5E`, `Flashbang` = `0x519A6525`, `grenade_electric` = `0xDECCDDE8` and `satchel` = `0x21A320A2` are all present among the saved ids (six exact matches of the 19; the earlier save-format pass reports 16 of 19 using a wider string set). That confirms both the hash and that a weapon's save id is the hash of its `Name` — i.e. the key of `weapons.xtbl`/`items_inventory.xtbl`.

### 16.4 Structural cross-check of the weapon record **[CONFIRMED — disassembly, independent code path]**

The weapon-upgrade reader (§4) was written by different code and builds patches with its own destination offsets. **Every offset and block size it uses for a name also present in the weapons reader agrees with §2.2** (about 55 name/offset/size pairs, `tools/ah_upg_offsets.txt`), and the last field the weapons reader writes ends exactly at the record stride `0x7EC`. The two implementations cannot have shared an error.

### 16.5 What was *not* validated

~~No DLC archive carries `weapon_categories`, `weapon_upgrades`, `ammo`, `weapon_melee_attacks`, `melee`, `melee_transition_states`, `aim_drift`, `aim_assist`, `combat_actions`, `combat_tricks`, `weapon_tracer_materials`, `store_weapons`, `crib_weapons`, `continuous_explosions`, `strafe_angles`, `taunting`, `hostage` or `windshield_cannon`; their schemas rest on complete reads of the loader bodies (and, for §4, the structural cross-check above), not on real rows.~~ **RESOLVED 2026-09-23 — all of these now have real base rows, validated in §18 (the DLC-only limitation this sentence described no longer applies).** No consumer of any table was read: field *names* are the authors' labels and the semantic glosses in this document are marked HIGH CONFIDENCE or HYPOTHESIS accordingly.

## 17. Open items, artifacts, and status

Status at checkpoint (2026-09-20): **all 22 assigned tables have a loader-derived schema; `weapons.xtbl` is complete to the element level; none was skipped.** What is *not* done is listed here. Running list, kept current as work proceeded.

1. ~~**Base-game values** — unchanged: the base `.xtbl` containers are the parked mode-(a) limitation; every schema here decodes a recovered base file directly. Not touched.~~ **RESOLVED 2026-09-23 — the mode-(a) container limitation was lifted project-wide 2026-09-20 (`spec-vpp-container.md` §7); base-game values for all 22 tables in this group were extracted and validated against the schemas above — see §18.**
2. **No consumer was read.** For every table the loader vocabulary (element names, types, offsets, units, defaults, limits) is confirmed; what the game *does* with the numbers (e.g. the meaning of `Min_Value`/`Max_Value` in `combat_tricks`, the `Fire_Cone` cosine test, `Effect_Situations` semantics, per-category block at `0x028DC488`) is HIGH CONFIDENCE from names only.
3. **Weapon record leftovers.** (a) The four words `+0x7D4…+0x7E0` are initialised (0, 0.0, 0, −1) but never element-driven by the weapons reader; the upgrade reader (§4.3) writes display-name text (`+0x7D4`), its handle (`+0x7D8`), description (`+0x7DC`) and bitmap (`+0x7E0`) there — the *base* rows leave them at those defaults (HIGH CONFIDENCE). (b) `Muzzle_Flash` is read and discarded. (c) The precise transcendental in `Fire_Cone_Angle`/`Fire_Cone_Metrics` (cosine assumed). (d) `FUN_008ADFF0`, the selector of the two weapon-instance arrays (co-op player HYPOTHESIS). (e) `OM_REMOVAL` semantics in upgrades *(base rows now available, §18.5; not checked there)*.
4. **Unbounded loops** worth a re-implementation guard: `Constant_Effect` > 4 rows (§2.2), `Small_Sticky_Effects` > 4 (§10.1), > 10 upgrades per weapon (§4.1), `Strafe_Angles` and `explosions` row counts (no capacity check found), `Categories` `Name` not in the seven-name set (§3).
5. **The "always" accessors and absent elements** (§1.3): the disassembly shows no clearing of the scratch buffer; `spec-vehicle-data.md` §7.2 describes the same helper as "0 if absent". Not edited there (not this pass's file); a reader relying on that wording should treat it as unverified. A run-time experiment (or the stack layout at the call sites) would settle what an absent element actually yields.
6. **Tables adjacent to this group found but not specced** (outside the assignment): `anim_synced.xtbl` (`FUN_0095DE40`/`FUN_0095DAA0`, loaded by the melee loader), `dof_situations.xtbl` (`FUN_0058EC80`; contains `Blur_Radius`), the lightset schema behind `store_weapon_lightset.xtbl`, the brass table (`0x013CF220`), the animation-group and animation-state name tables, the panic-reaction and groundfire tables `explosions.xtbl` names (`FUN_004EEE40`, `FUN_005FD830`), `items_3d`/`items_inventory` (`FUN_00904C10`, `FUN_008DCB10` resolvers only), and the nine `*_key` registry entries of §15.2.
7. **Store table:** `Num_Hoods`, `Angle`, `Distance`, `Offset` are in the descriptor table but not read by `FUN_00817F90` (§13.2) — their consumer was not found.
8. **Explosion `Tint` colour** component semantics (`FUN_00DAD160`) and `Panic_Reaction`'s target table.
9. **Registry package record** (§15.2): only the bytes the handlers touch (`+0x80`, `+0x101`, `+0x103`) are known.
10. **Explosion/strafe/tracer storage extents** other than the tracer capacity (20) and aim-drift capacity (24).
11. ~~**DLC weapon slot arithmetic** assumes a base count of 82 (§2) — consistent with two DLC first-row values and the unload range, not directly observable until the base `weapons.xtbl` is readable.~~ **RESOLVED 2026-09-23 — the base `weapons.xtbl` is now readable and has exactly 82 `<Weapon>` rows (§18.4); the slot arithmetic is CONFIRMED — empirical, not inferred.**

**Artifacts.** Ghidra post-scripts (`tools/scripts/`): `AhTab.java` (dump pointer/string tables), `AhAsm.java` (disassembly with string annotation), `AhDecMk.java` (disassemble + create function + decompile — needed for the DLC handlers, which are reached only through function-pointer words); reused `AfDec.java`, `AfDecRange.java`, `AfMem.java`, `AfStrXrefs.java`, `AfCallers.java`, `AfAsm.java`. Runner: `tools/run_tbl.ps1` (project copy `tools/gp_tbl1`, disposable). Dumps: `tools/ah_w*.txt` (weapon reader), `ah_u*.txt` (upgrades/ammo), `ah_me*.txt` (melee), `ah_o*.txt`, `ah_x*.txt` (explosions/tracers), `ah_s*.txt` (store/crib), `ah_dl*.txt` (DLC handlers), `ah_t*.txt` (static tables), `ah_flags_w.txt`, `ah_upg_offsets.txt`, `ah_validate_weapons.txt`, `ah_fn_xrefs.txt`. Harnesses (`tools/harnesses/`): `tbl_dlc_list.py`, `tbl_dec_compact.py`, `tbl_flagpairs.py`, `tbl_upgrade_offsets.py`, `tbl_weapons_validate.py`, `tbl_expl_validate.py`, `tbl_desc_compare.py`, `tbl_exe_lit.py`, `tbl_findptr.py`, `tbl_save_weapon_ids.py`, `tbl_append.py`. The parked mode-(a) decompression question and the `.czn_pc` interior were not touched.

## 18. Validation against real base-game tables (2026-09-23)

**Prepared by:** SPEC TEAM, follow-up validation pass, 2026-09-23 (`HANDOFF.md` §31, archived §27.2 queue item 0). This section validates §1–§17 (written from loader disassembly + 9 DLC rows, §16) against the now-readable real base-game tables, per `spec-vpp-container.md` §7/§8 and `spec-xtbl-format.md` §7.

### 18.1 Method and sources

All 22 tables assigned to this group were located and extracted from the base archives with `tools/harnesses/vpp_modea.py` (the `open_archive`/`get_table` pattern of this campaign's brief): `misc_tables.vpp_pc`, `da_tables.vpp_pc` and `patch_compressed.vpp_pc`. Every one of the 22 filenames is present (several also appear a second time, byte-identical, in `da_tables.vpp_pc`, which mirrors a subset of `misc_tables.vpp_pc`); two — `ammo.xtbl` and `continuous_explosions.xtbl` — additionally have a **larger** copy in `patch_compressed.vpp_pc` (35 vs 34 rows; 3 vs 1 row) that was used in preference for those two tables' value/structural checks, being the more complete data set. Every extraction's inflated length matched its declared uncompressed size exactly (24/24). All 22 files parsed cleanly with a strict XML parser (`xml.etree.ElementTree`) with no fallback needed — none of `spec-xtbl-format.md` §7's four known shipped-data quirks (mismatched close tag, control character `0x1F`, space in an element name, and the fourth being the combination of these across the same handful of files) occurs in this group. **[CONFIRMED — empirical.]**

Coverage/value-range checks for `weapons.xtbl` extend `tools/harnesses/tbl_weapons_validate.py`'s predicates (previously run against 9 DLC rows only, §16.1) to the real 82-row base file, and `tools/harnesses/tbl_desc_compare.py`'s `TableDescription` cross-check likewise. For the other 21 tables (none of which any DLC archive carries, §16.5), coverage is checked directly against this document's own already-published per-table element list (§3–§14), which was derived from a complete read of each loader; any element name found in the real file that is not in this document's list is flagged as a candidate undocumented element. Every flagged name was then checked with a case-insensitive whole-executable literal scan (`tools/harnesses/tbl_exe_lit.py`, exact NUL-delimited hits) before being called "never read" — this project's standing bar for a negative claim (`HANDOFF.md` §5) — rather than inferred from a known-field comparison alone.

### 18.2 All 22 tables are present in the base archives; row counts

| Table | Archive : entry | Bytes (uncompressed) | Row element : count |
|---|---|---|---|
| `weapons.xtbl` | `misc_tables.vpp_pc` : 344 | 526,229 | `Weapon` : **82** |
| `weapon_categories.xtbl` | `misc_tables.vpp_pc` : 345 | 1,305 | `Categories` : 7 |
| `weapon_upgrades.xtbl` | `misc_tables.vpp_pc` : 351 | 173,162 | `Weapon_Upgrade` : 64 |
| `weapon_melee_attacks.xtbl` | `misc_tables.vpp_pc` : 348 | 18,746 | `Melee_Attack_Set` : 25 |
| `ammo.xtbl` | `misc_tables.vpp_pc` : 13 / `patch_compressed.vpp_pc` : 52 | 18,718 / 19,045 | `Ammo` : 34 / **35** |
| `melee.xtbl` | `misc_tables.vpp_pc` : 180 | 440,994 | `MeleeMove` : 336 |
| `melee_transition_states.xtbl` | `misc_tables.vpp_pc` : 181 | 8,353 | `Melee_Transition_State` : 19 |
| `aim_drift.xtbl` | `misc_tables.vpp_pc` : 10 | 46,650 | `Profile` : 20 |
| `aim_assist.xtbl` | `misc_tables.vpp_pc` : 9 | 9,746 | `Aiming` : 5 |
| `explosions.xtbl` | `misc_tables.vpp_pc` : 128 | 127,991 | `Explosion` : 93 |
| `continuous_explosions.xtbl` | `misc_tables.vpp_pc` : 83 / `patch_compressed.vpp_pc` : 48 | 7,752 / 9,440 | `Continuous_explosion` : 1 / **3** |
| `combat_actions.xtbl` | `misc_tables.vpp_pc` : 387 | 35,041 | `actions` : 67 |
| `combat_tricks.xtbl` | `misc_tables.vpp_pc` : 550 (= `da_tables.vpp_pc` : 4) | 49,051 | `Combat_Tricks` : 1 |
| `weapon_tracers.xtbl` | `misc_tables.vpp_pc` : 349 | 5,948 | `Weapon_Tracers` : 13 |
| `weapon_tracer_materials.xtbl` | `misc_tables.vpp_pc` : 350 | 5,012 | `Tracer_Material` : 25 |
| `crib_weapons.xtbl` | `misc_tables.vpp_pc` : 92 | 2,552 | `Crib_Weapons` : 1 (7 `Entry`) |
| `store_weapons.xtbl` | `misc_tables.vpp_pc` : 248 (= `da_tables.vpp_pc` : 107) | 4,859 | `Store_Weapons` : 1 (34 `Entry`) |
| `store_weapon_lightset.xtbl` | `misc_tables.vpp_pc` : 531 | 12,714 | `LightSet` : 1 (not a weapon-schema table, §13.3) |
| `strafe_angles.xtbl` | `misc_tables.vpp_pc` : 249 | 4,360 | `Strafe_Angles` : 8 |
| `taunting.xtbl` | `misc_tables.vpp_pc` : 567 (= `da_tables.vpp_pc` : 21) | 5,580 | `Taunting` : 1 |
| `hostage.xtbl` | `misc_tables.vpp_pc` : 553 (= `da_tables.vpp_pc` : 7) | 11,795 | `Hostage` : 1 (6 `Vehicle_Class` × 3 `Difficulty_Level`) |
| `windshield_cannon.xtbl` | `misc_tables.vpp_pc` : 569 (= `da_tables.vpp_pc` : 23) | 4,176 | `Windshield_Cannon` : 1 |

**22/22 tables assigned to this group are present and real-row-readable.** §16.5's "no DLC archive carries…" caveat is resolved for all of them (see the strikethrough there). **[CONFIRMED — empirical.]**

### 18.3 `weapons.xtbl` coverage check — 82 real base rows

Extending §16.1's 9-DLC-row predicates: **240 distinct element tags** occur across the 82 real rows (vs. 199 in the 9 DLC rows). Of those 240, **8 are not reader literals** *(nine names are listed below because `Info_Slot_Index` is included for completeness; it occurs 0 times, so it is not one of the 8)* — and they are **exactly** the set §2.6 already named from the DLC sample plus its `TableDescription` cross-check, with **zero new surprises**: `_Editor` (82), `Effect_Situation` (32, the per-situation row tag), `Projectile_Flags` (31, the false-alarm literal that actually belongs to helper `FUN_00B7DB30`), `Info_Slot_Index` (0 — never appears in the base file at all, consistent with it being DLC-only, §18.4), `Hit_Wall_Sound` (56), `Spinning_Snd_Pitch_End` (81), `Foley_Name` (13), `Fire_Damage_Per_Second` (4) and `Alt_Time_Management/npc_burst_time` (1). **[CONFIRMED — empirical, real base data; the DLC-only inference in §2.6 is fully reproduced at 9× the sample size with no additions or contradictions.]**

### 18.4 `weapons.xtbl` value-range and structural check — 82 real base rows

| Predicate | Result (82 real base rows) |
|---|---|
| `Weapon_Class` ∈ 23-name set · `Inv_Slot` ∈ 11 · `Category` ∈ 7 `WPNCAT_*` · `Trigger_Type`, `Alt_Trigger_Type` ∈ 4 | **82/82** each |
| `Special_Case_Type` ∈ 14-name set · `Grenade_Type` ∈ 4-name set | **18/18 · 4/4** |
| top-level `<Flags>/<Flag>` ∈ 63-name set | **472/472** |
| `Projectile_Info/Projectile_Flags/Flag` ∈ 24-name set | **133/133** |
| `Effect_Situations` child `Situation` ∈ 25-name set; ≤ 4 per weapon | **32/32; 82/82** |
| `Constant_Effect` count ≤ 4 · `Constant_Effect/Condition` recognised (3 names) | **82/82 · 7/9** (the 2 unrecognised are both `always on`, exactly the DLC3 value, mapped to condition 0 as documented) |
| `Primary_Weapons` ≤ 4 · `Alt_Weapons` ≤ 4 | **82/82 · 82/82** |
| float grammar (12 element names) · `Magazine_Size`/`Ammo_per_Shot` integer, `Magazine_Size` fits `u16` | **all pass** |
| `Is_DLC` ∈ {True, False} · `Framework` | **82/82 bool-valid; `Framework` absent on all 82** |

**Enum coverage in the shipped base game:** `Inv_Slot` uses **all 11** of its names; `Category` uses **all 7**; `Grenade_Type` uses **all 4**; `Weapon_Class` uses 14 of its 23; `Special_Case_Type` uses 11 of its 14 — the three never shipped are `Needler Prototype`, `Flak Cannon Prototype` and `Multi Launcher Prototype` (cut/prototype-only content, per the names). No value outside any documented set was found anywhere in the 82 rows.

**Structural facts newly observable:**
- The base `<Weapon>` row count is **exactly 82**, confirming §2's DLC-slot arithmetic (struck through above) directly rather than by inference; capacity is `82 + 12 = 94`.
- `Info_Slot_Index` occurs in **0/82** base rows — confirmed DLC-only, as documented.
- `Base_Version` occurs in **0/82** base rows — the "variant of a base weapon" mechanism the reader supports (§2.2) is unused by the shipped base game.
- `Is_DLC` is `True` on **5/82** base rows despite every one of the 82 having `Framework` absent — i.e. `Is_DLC` (the record's own gate byte) and the `Framework`-based load-pass filter are independent gates in practice, not just in the documented mechanism; a row can be `Is_DLC=True` and still load in the "main" pass.
- No row anywhere exceeded any of the capacities this document flags as "not bounded" (`Constant_Effect` > 4, `Primary_Weapons`/`Alt_Weapons` > 4, `Effect_Situations` > 4) — the unbounded loops are real code properties, not currently exercised by any shipped row, base or DLC.

**[CONFIRMED — empirical for all rows above; the 82-count and its consequences for §2/§17 item 11 are now CONFIRMED rather than HIGH CONFIDENCE.]**

### 18.5 Coverage / value / structural checks — the other 21 tables

Every table below was checked against this document's own already-published element list for that table (§3–§14); "coverage: clean" means every element tag in the real file is already in this document's list (only previously-documented "not read" elements, if any, appear as extras — see §18.6 for the ones that are new).

| Table | Real rows | Coverage vs. §3–§14 | Value-range check | Structural check |
|---|---|---|---|---|
| `weapon_categories.xtbl` | 7 | clean (only `Name` read, as documented) | all 7 `Name` values are exact `WPNCAT_*` matches | **7/7 — the "seven-row list" claim is now an exact count, not an assumption** |
| `weapon_upgrades.xtbl` | 64 (22 distinct weapons) | clean except 2 authoring typos, §18.6 | override elements all within the §4.3 vocabulary | max upgrades for any one weapon = **3** (cap is 10, far from being hit) |
| `weapon_melee_attacks.xtbl` | 25 | clean (only the 15 documented slot names + `Name`) | — | — |
| `ammo.xtbl` (35-row patch copy) | 35 | clean | `Inv_Slot` **23/23** in the 11-set; `Flag` **all** in the 12-set; `Upgradable_Ammo/Weapon` values are plausible weapon names | — |
| `melee.xtbl` | 336 | clean except dead elements, §18.6 | `AttackLimb`/`AttackLimbNPC` **all** in the 6-name set (only 5 of 6 values actually used; `Weapon` never appears) | `Combo` present on **336/336** rows (100%); `Explosion` (CRC-only) on 6 |
| `melee_transition_states.xtbl` | 19 | clean | — | `Return_Action` authored in **0/19** real rows (always defaults to −1 in the shipped game) |
| `aim_drift.xtbl` | 20 | clean except dead elements, §18.6 | — | 20 profiles, well under the 24-profile cap; see §18.6 for the `Recovery`-wrapper finding |
| `aim_assist.xtbl` | 5 | clean | `Name` values are **exactly** `{Normal, Combat Ready, Fine Aim, Tank Skydiving, Zoomed}` — 5/5, no extra/unrecognised rows | — |
| `explosions.xtbl` | 93 | clean except `Blur_Radius` (§18.6, already flagged by §16.2) | `Flags/Flag` in the 5-name set; numeric fields parse | — |
| `continuous_explosions.xtbl` (3-row patch copy) | 3 | clean | `Target_Type` values seen: `default`, `in_aircraft` — both in the 3-name set, no duplicates/unknowns | row count **3 = the loader's own cap exactly** |
| `combat_actions.xtbl` | 67 | clean except dead elements, §18.6 | **67/67** row `Name`s match one of the 70 built-in names exactly; 0 unmatched, 0 duplicate re-tunings of the same built-in | — |
| `combat_tricks.xtbl` | 1 | clean except 1 dead extra child, §18.6 | all 18 fixed trick names present | row count **1 = exactly the documented single-row read** |
| `weapon_tracers.xtbl` | 13 | clean | — | 13, well under the 20-record cap; see §18.6 for the `Ricochet`-wrapper finding |
| `weapon_tracer_materials.xtbl` | 25 | clean | all 25 `Name` values are recognisable physical-material names | 25 of the 33-slot table populated from data |
| `crib_weapons.xtbl` | 7 entries | clean except dead elements, §18.6 | `Unlocked` = `false` on all 7 (every crib weapon ships locked) | — |
| `store_weapons.xtbl` | 34 entries | clean (`Num_Hoods`/`Angle`/`Distance`/`Offset` still absent from real data too — §13.2's OPEN item gets no new information either way) | — | — |
| `strafe_angles.xtbl` | 8 | clean | — | one row is named **`Default`**, exactly as required |
| `taunting.xtbl` | 1 | clean | — | — |
| `hostage.xtbl` | 1 (6×3 slots) | clean except 1 dead element per slot, §18.6 | 6 `Vehicle_Class/Class_Name` values are **exactly** the six-name set | **18 = 6 classes × 3 difficulty levels, exactly the documented "18 slots" storage claim** |
| `windshield_cannon.xtbl` | 1 | clean | — | — |

**[CONFIRMED — empirical for every row above.]**

### 18.6 Findings only visible with real base data (the DLC-only pass could not have made these)

Each dead-element claim below is an exhaustive case-insensitive whole-executable literal scan (`tools/harnesses/tbl_exe_lit.py`), not a comparison against a known-field list — the project's standing bar for a negative claim.

- **`hostage.xtbl`:** every one of the 18 real `Difficulty_Level` rows carries a sibling element `Vehicle_Classs` (note the extra "s") whose value exactly mirrors that slot's own `Num_Hostages` (1/2/3). `Vehicle_Classs` has **zero** exact literal hits anywhere in the executable; the correctly-spelled `Vehicle_Class` (the wrapper the reader actually uses, one level up in the tree) **is** an exact literal — a positive control confirming the scan is discriminating, not just silent.
- **`aim_drift.xtbl`:** every one of the 20 real `Profile` rows carries row-level `MinTime`/`MaxTime` children directly under `Profile` (e.g. `2000`/`4000` on the `Default` profile) — neither name is documented anywhere in §8, and neither is an exact literal anywhere in the executable. Separately, every profile's `Aiming` block carries a dead duplicate `Bounces_per_secxxx` beside the real `Bounces_per_sec` (also confirmed dead). Most significantly: **the `Recovery` wrapper this document's §8 says the reader requires around `recover_penalty`/`recover_time`/`bullets_to_unsteady` (defaulting to 1.0/500.0/−1.0 if absent) has zero occurrences anywhere in the real 20-profile file** — every real base aim-drift profile authors these three fields as direct children of `Aiming` instead, so every real profile silently gets the reader's hardcoded defaults for all three, never its own authored values. No DLC archive carries `aim_drift.xtbl` at all, so nothing short of the real base file could have shown this.
- **`melee.xtbl`:** three per-move elements that read like an alternate combo-chaining scheme — `NextPrimary` (57/336 rows), `NextSecondary` (55), `NextSpecial` (71), each wrapping a `<MeleeMove>` child that names another move by name — and three per-move gating elements — `victim_pre_condition`, `victim_post_condition`, `req_prev_hits` (273 each) — have **zero** exact literal hits anywhere in the executable. The reader's actual, documented chaining mechanism (`Combo`/`Anim_group_grid` + `Start_Pose`/`End_Pose`, §7.1) is present on all 336 rows, but at least one real row (`Avatar-P_Attack_A`) has an **empty** `<Combo></Combo>` while its only authored follow-up information is the dead `NextPrimary`/`NextSecondary` pair (naming `punch1L`) — that move's intended chain-out is expressed exclusively through a mechanism the row reader never touches. Whether some other, non-table-driven system (not read in this pass — no consumer of any table was read, §16.5) resolves the chain another way is **OPEN**.
- **`combat_actions.xtbl`:** 42/67 rows carry a free-text `Notes` child (e.g. `"get out of the leaders way"`) and 8/67 carry an (empty, in every real instance) `Prerequis` child; neither is an exact literal anywhere in the executable.
- **`crib_weapons.xtbl`:** every one of the 7 real `Entry` rows carries a `Num_Cribs` element (values 1, 1, 3, 4, 4, 5, 5 — reads like a progression-tier hint) and the single `Crib_Weapons` row carries a `MagazineScalar` element; neither is an exact literal anywhere in the executable.
- **`combat_tricks.xtbl`:** the one real row carries, alongside the correctly-named `Brute_Beat_Kill` (confirmed an exact reader literal, 1 hit), a second, differently-spelled child `Beat_Down_Kill` — **confirmed 2026-09-23 (re-checked by the orchestrator directly against the executable, prompted by Team B's independent validation flagging this as "a 19th trick"): `Beat_Down_Kill` has ZERO exact literal occurrences anywhere in the executable (vs. `Brute_Beat_Kill`'s 1), so it is never read by name — this part of the original finding stands.** ~~though it appears embedded in a static localisation-key string `DIVERSION_COMBAT_TRICKS_BEAT_DOWN_KILL`, suggesting the trick was renamed at some point and the old element name survives as inert data.~~ **The "renamed, old element survives" story is downgraded from stated-as-fact to HYPOTHESIS — it was never confirmed by tracing which trick's id the localisation key resolves to.** What IS confirmed directly from the real data: `Beat_Down_Kill` is not a stub or an empty leftover — it carries its own genuinely distinct 5-field record (`Max_Value=9, Min_Value=0, Max_Respect=90, Max_Lifetime_Respect=10000, Max_Cash=180`, no `Duration`), different in shape and every value from `Brute_Beat_Kill`'s own record (`Duration=2.0, Max_Value=1, Min_Value=0, Max_Respect=100, Max_Lifetime_Respect=3000, Max_Cash=150`) — so whatever `Beat_Down_Kill` originally was, it was authored as a real, separate trick definition, not a copy-paste duplicate of `Brute_Beat_Kill`. It is simply never reached by any exact-literal reader lookup found in this pass.
- **`weapon_tracers.xtbl`** (generalising a DLC-only observation, §16.2, to the full population): **0 of the 13 real base rows** wrap `Chance` in a `Ricochet` element — every real base row, like 2 of the 4 DLC rows, puts `Chance` directly under `Weapon_Tracers`, where the reader never looks. The `Ricochet` wrapper §12.1 says the reader requires is, across the entire shipped game (base + DLC), **never actually used**; the field always evaluates to the reader's default (0).
- **`explosions.xtbl`:** `Screen_Effects/Blur_Radius` — already identified by the DLC `TableDescription` cross-check (§16.2) as belonging to `dof_situations.xtbl`'s reader, not this one — is confirmed present in **6 of the 93** real base rows, not just a DLC edge case.
- **`weapons.xtbl`:** the 8 ~~dead elements~~ non-literal tags already named in §18.3 *(only 5 of them are genuinely unread — `Hit_Wall_Sound`, `Spinning_Snd_Pitch_End`, `Foley_Name`, `Fire_Damage_Per_Second`, `npc_burst_time`; `_Editor`, `Effect_Situation` and `Projectile_Flags` are handled or false alarms, §2.6)* are all confirmed at 9×-larger sample size with the same, unchanged membership — no table in this group turned up a *newly*-dead top-level `weapons.xtbl` element beyond what §2.6 already found from 9 DLC rows.

### 18.7 What remains open after this pass

- The consequence, if any, of `melee.xtbl`'s dead `NextPrimary`/`NextSecondary`/`NextSpecial`/`victim_*_condition`/`req_prev_hits` fields for a row whose `Combo` is empty (§18.6) — would need a consumer trace, out of this pass's scope (§16.5's "no consumer of any table was read" caveat still holds).
- `store_weapons.xtbl`'s `Num_Hoods`/`Angle`/`Distance`/`Offset` consumer (§13.2) — still not found; real data confirms these are absent from the loader's own file too (0 occurrences), so this pass adds no new information either way.
- `store_weapon_lightset.xtbl`'s own schema (§13.3, the shared lightset reader) remains out of this group's scope, unaffected by this pass.
- Exact storage extents for `explosions`/`strafe_angles`/tracer-adjacent tables beyond the already-documented caps (§17 item 10) — the real row counts (93, 8, 13) are comfortably under any plausible extent and provide no new pressure to determine it.
- Whether `ammo.xtbl`'s `Upgradable_Ammo/Weapon` values (`Special-Drone`, `Special-Airstrike`, `Special-RCVehicleGun`, `satchel`) resolve against real `weapons.xtbl` `Name` values was spot-checked for plausibility only, not exhaustively cross-joined against all 82 real weapon names.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the "base tables unreadable" and "no empirical cross-check" statements superseded by §18 (header, §1.1, §3, §4.3, §5); noted §18.5 availability for the `OM_REMOVAL` question (§4.2, §17 item 3e); aligned §1.6 weapons-array "count" with §2.1 capacity; added a conflict marker for the `items_inventory` live-bit offset (§1.6, vs `spec-save-format.md` §10.3); fixed tag counts in §18.3 (9 listed / 8 occurring) and §18.6 (5 genuinely unread of 8).
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline; repointed 2 `HANDOFF.md` §27.x references to the archived headings (§27.2 → §30 header, §27.2 queue item 0 → §31, §18); 0 left (see review).
- 2026-09-30 (cloud): §1: the table loader's failure message is paraphrased instead of quoted verbatim (manager clean-room line).
