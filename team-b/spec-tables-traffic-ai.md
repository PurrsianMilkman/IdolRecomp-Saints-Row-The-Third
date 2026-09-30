# Saints Row: The Third — Traffic, Ambient Population, Roadblock and AI Data Tables: Loader-Recovered Schemas

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process), agent AJ
**Phase:** 2, "schema-from-loader" campaign (`HANDOFF.md` §30, archived §27.2), group *traffic, ambient population, roadblocks and AI*. Sibling documents: `spec-tables-weapons-combat.md` (agent AH), `spec-tables-progression.md` (agent AI), `spec-tables-environment.md` (agent AK).
**Scope:** For each `.xtbl` table of the group, the element tree the exe's loader asks for, each element's type and destination in the runtime table (or global), defaults, required/optional behaviour, unit conversions, limits, hash keys and cross-table references — recovered **from the exe alone**. No base-game table value was read; no mode-(a) container was opened (the parked limitation, `HANDOFF.md` §27.3, is neither touched nor relied on). DLC raw tables are the only value samples (§24). *(Original pass; base-game tables were later extracted and validated, see §26.)*
**Method:** Exactly the method of `spec-vehicle-data.md` §7 (agent AF): the literal table filename in the exe → its one cross-reference → the loader → the per-row reader → the element-name literals passed to the shared XML accessors. Ghidra project copy `tools/gp_tbl3` (disposable). Tooling: `tools/scripts/AjBoth.java` (decompile + string-annotated disassembly of a function and its callees), `AjAsmFn.java`, `AjDecTree.java`, `AfStrXrefs.java`/`AfDec.java` (agent AF's); `tools/harnesses/aj_va.py` (reads constants/strings/pointer tables out of the exe image by virtual address); wrapper `tools/run_tbl3.ps1`. Dumps: `tools/aj_*.txt`. No whole-binary predicate search was used; every function was reached from a filename literal, an element-name literal or a named global.
**Cleanroom compliance:** No decompiled code is reproduced; no original internal identifiers are used (functions are cited by address only, as evidence). XML element and table names, enum strings and other data literals are game data and are listed as such. Offsets, strides, capacities and constants are stated as measured.
**Confidence key:** **CONFIRMED — disassembly** · **CONFIRMED — empirical** · **HIGH CONFIDENCE — inferred** · **HYPOTHESIS — unconfirmed** · **OPEN / UNKNOWN**.

---

## 1. Overview, coverage and the shared reader grammar

### 1.1 Coverage matrix

Every filename was first searched as an exact NUL-delimited string in the exe image (Ghidra `AfStrXrefs`, plus a raw case-insensitive byte scan). **Filename literals are matched case-insensitively by the engine's file layer, and three are stored with different case than the task list** (`PEDF_Life.xtbl`, `Life_default.xtbl`, `Escort_constants.xtbl`).

| Table | Literal in exe | Loader (entry) | Specced in |
|---|---|---|---|
| `traffic_lanes.xtbl` | yes | `0x00A69060` | §2 |
| `traffic_types.xtbl` | **NO — no literal, no loader** (only the *suffix* of `distant_vehicle_traffic_types.xtbl` matches) | — | §3 |
| `ambient_traffic_events.xtbl` | yes | `0x005E3660` | §4 |
| `roadblock_layouts.xtbl` | yes | `0x0092BDE0` | §5 |
| `roadblock_notoriety.xtbl` | yes | `0x0060A300` | §6 |
| `roadblock_stag_lockdown.xtbl` | yes | `0x005E3FD0` | §7 |
| `panic_reactions.xtbl` | yes | `0x004EE110` | §8 |
| `ai_goals.xtbl` | yes | `0x004F29B0` | §9 |
| `ai_behavior.xtbl` | yes | `0x004F2B50` (also loads `combat_actions.xtbl` and `ai_goals.xtbl`) | §10 |
| `ai_personalities.xtbl` | yes | `0x00507AC0` | §11 |
| `generic_characters.xtbl`, `generic_vehicles.xtbl` | yes | `0x009189A0`, `0x009186E0` | §12 |
| `action_node_groups.xtbl`, `action_node_notoriety.xtbl`, `action_node_npcs.xtbl` | yes | `0x008C9760`, `0x008C83B0`, `0x008C87D0` | §13.1–13.3 |
| `action_nodes.xtbl` (placement file) | yes | `0x008F08A0` | §13.4 (structure only) |
| `distant_peds`, `distant_ped_colors`, `distant_ped_spawn_parameters`, `distant_vehicles`, `distant_vehicle_colors`, `distant_vehicle_spawn_parameters`, `distant_vehicle_traffic_types` | yes (all seven) | `0x00B8FAD0`, `0x00B8FB60`, `0x00B8FD40`, `0x00B93180`, `0x00B92C40`, `0x00B92CE0`, `0x00B93000` | §14 |
| `homies.xtbl` (+ `<framework>_homies.xtbl`) | yes (2 refs) | `0x007E6E70` / `0x007E7580` → shared reader `0x007E6760` | §15 |
| `follower_heads.xtbl` | yes | `0x007F0670` | §16 |
| `driver_bailout.xtbl` | yes | `0x006A5310` | §17 |
| `vehicle_despawn.xtbl` | yes, **as a data pointer only** (no code xref) | parser `0x00905C20` via `0x00908FB0` | §18 |
| `escort_constants.xtbl` | yes, as `Escort_constants.xtbl` | `0x0067F1D0` (no Ghidra function existed) | §19 |
| `human_transition.xtbl` | yes | `0x009B8630` | §20 |
| `pedf_life.xtbl`, `life_default.xtbl` | yes, as `PEDF_Life.xtbl`, `Life_default.xtbl` | `0x008C9FD0` → `0x008C7BC0` | §21 |
| `node_graph_files.xtbl` | yes (filename only in the start-up routine) | start-up `0x005D25F0`; reader `0x004CB510` | §22 |

**All 32 tables are accounted for: 31 have a loader that was read; `traffic_types.xtbl` has none.** Depth varies: §2–§12, §14–§20 are complete element-by-element schemas; §13.4 (`action_nodes.xtbl`), §21 (life files) and §22 (`node_graph_files.xtbl`) are structural, with the interior explicitly marked OPEN.

### 1.2 The XML document model **[CONFIRMED — disassembly]**

The engine's XML tree node (as walked by every accessor in `0x00DAB9D0`–`0x00DAD000` and `0x00DC4FF0`–`0x00DC5190`) has four fields: `+0x00` element-name `char*`, `+0x04` next-sibling pointer, `+0x08` first-child pointer, `+0x0C` text `char*` (NULL for an element with no text). All name comparisons are **case-insensitive** (`_stricmp`).

**Table loader `0x00DAC9A0(filename, memory-source = 0, 1)`** builds the parse (pool label `"xml_table_parse %s"`), returns the **`Table` child of the document root** (the `<root><Table>` shape of `spec-xtbl-format.md` §2); failure prints the message *"The table file "%s" is missing or invalid - parser error: %s."* and returns 0. **Two files in this group bypass it:** `action_nodes.xtbl` (§13.4) and `node_graph_files.xtbl` (§22) are parsed by the raw-document entry `0x00DC5AC0` and their rows are taken **directly from the document root, with no `Table` wrapper**. The matching release call is `0x00DAB9D0`.

### 1.3 The shared element accessors **[CONFIRMED — disassembly]**

All are C-calling-convention functions; `node` is an XML element, `name` a child-element name (or 0 = the node's own text). *(The per-type readers were independently found by agent AF, `spec-vehicle-data.md` §7.2; the addresses below are re-verified and extended. AH's `spec-tables-weapons-combat.md` §1 is the intended home of the full grammar; this section states only what these tables need.)*

| Address | Role | Behaviour |
|---|---|---|
| `0x00DAB9E0` | `FindChild(node, name)` | first child with that name (case-insensitive), else 0 |
| `0x00DAB9F0` | `NextSibling(parent, cur, name)` | next sibling **after `cur`** with the name — the row-iteration primitive |
| `0x00DABA00` | `CountChildren(node, name)` | number of children with that name |
| `0x00DC5070` | `NthChild(node, name, n)` | the *n*-th (0-based) child with that name |
| `0x00DABA10`, `0x00DABA40` | `ChildText(node, name)` | text pointer of the child, or 0 if the child is absent or empty |
| `0x00DABA70` | `CopyText(dst, size, node, name)` | bounded copy; **leaves `dst` untouched if the child is absent**; NUL forced at `dst[size-1]`. Callers that then test `dst[0] != 0` ("required non-empty" below) usually **do not pre-clear**, so an absent element tests whatever the buffer held — a shipped table must always supply such elements |
| `0x00DABAB0` | `CopyTextOk(dst, size, node, name)` | same, returns true iff copied |
| `0x00DABC70` / `0x00DABD20` | signed 32-bit int, **always write** / **write only if present** | optional leading `-` (only these two handle a sign) |
| `0x00DABDF0` / `0x00DABE80` | unsigned 32-bit, always / if present | **no sign handling — a leading `-` parses as 0** |
| `0x00DAC1D0` | unsigned 8-bit, always write | (u32 parse, low byte stored) |
| `0x00DAC480` / `0x00DAC510` | bool, always / if present | **only `true` and `yes` (case-insensitive, whole string) give 1; every other present text gives 0** |
| `0x00DACCB0` / `0x00DACD40` | float, always / if present | the engine float grammar (below) |
| `0x00DACF20` | `vec3` | children `X`, `Y`, `Z` (literals at `0x01121CB4`/`B0`/`AC`) via the always-write float reader |
| `0x00DAC740(names[], count, node, 0)` | **flag-list bitmask** | walks all children of `node` named `Flag`; for each, case-insensitively matches its text against `names[0..count)` and ORs bit *i*; returns the mask |
| `0x00DAC7D0(node, name)` | `HasFlag` | true iff some `Flag` child of `node` has that text |
| `0x00DAC830(names[], count, node, 0)` | enum index | index of the node's text among `names[]` (case-insensitive); **−1** if the node is null or nothing matches |
| `0x00DAC880`, `0x00DAC8C0` / `0x00DAC950` | `Filename`-child text / copy | element `Filename` under a named child (`Animation` → `Filename` in §21) |

**"Always write" readers do not zero their 1-KiB scratch buffer**: when the element is absent they parse whatever the stack held (`0x00DABC70`, `0x00DACCB0` disassembled: no store between the frame set-up and the parse). This refines `spec-vehicle-data.md` §7.2's "0 if absent": the destination is *unspecified*, not 0. Every table below that needs a default therefore either pre-initialises the destination and uses the "if present" reader, or is fed a value the row validators later range-check. **[CONFIRMED — disassembly.]**

**Engine float grammar (`0x00DACB20`)** — as AF §7.2, restated with the integer path: optional `-`; a token starting `0x`/`0X` returns **0.0**; a leading `.` is accepted; the integer part is read by the engine integer parser `0x00DAB8B0` (decimal digits, or `0x…` hex, stopping at the first non-digit; no overflow check); a `.` starts the fraction (each digit weighted by successive ×0.1 in single precision), `e`/`E` starts a decimal exponent applied as ×10^n. Text that does not start like a number reads as 0. **[CONFIRMED — disassembly.]** *(Team B, 2026-09-20: because a token starting `0x` returns 0.0 before the integer parser runs, the parser's hex branch is unreachable through this reader; no real value text in the base tables starts with `0x`.)*

**Text-boolean elements** that several tables read *not* through `0x00DAC480` but by string compare (`action_node_npcs`, §13.3): the value is equal to `True` or `False` (case-insensitive); **any other text leaves the field at the value it had before the compare** — and for those tables the field is cleared/set immediately before, so an unrecognised spelling yields the pre-set default.

### 1.4 The engine hashes used as row keys **[CONFIRMED — disassembly]**

| Routine | What | Used for |
|---|---|---|
| `0x00D9E8B0(out, str, seed, maxlen)` (`__thiscall`) and `0x00D9E740(str, seed)` | table-driven reflected CRC-32 (table at `0x01320DA0`), **lower-cases each byte**, `crc = (crc >> 8) ^ T[(byte ^ crc) & 0xFF]`, **no final XOR**. Every call in this group passes seed **0** (a few *chain* the result of one string as the seed of a second: §5) | row-name keys (`Name` → a 32-bit key), `Spawn_Name`, layout names |
| `0x00DAB330(str, nbuckets)` | multiply-by-33 XOR of lower-cased bytes, `% nbuckets` | bucket index for the "Team" name registry (`% 0x20`) |
| `0x00DAB2B0(key, nbuckets)` | 32-bit integer mixer (shift/xor/×9 avalanche) `% nbuckets` (signed `IDIV`) | bucket index of the character-definition registry (500 buckets) fed with a CRC |
| Wwise `GetIDFromString` via `0x0046FD00` (called from `0x00462960`/`0x0070A2F0`) | third-party audio-middleware string→ID hash; the text `none` (any case) returns 0 | `Persona_Situation`, `Music_Emitter` (audio event ids) |

### 1.5 Fixed enum tables in the exe image that the readers match against

These are initialised `.data` arrays (readable statically). They are game data and are listed because rows refer to them by name.

- **Combat-action table** (70 entries, stride `0x18`, at `0x012E1C60`; the `0x47` = 71 value is the "not found" sentinel): `fire`, `fire sweep`, `fire suppress`, `fire chaos`, `throw grenade`, `throw weapon`, `melee`, `melee vehicle`, `cover take def`, `cover take off`, `cover advance`, `cover stay down`, `cover fire`, `cover popout`, `hold position`, `melee stand back`, `move retreat`, `move regroup`, `move advance`, `move chase`, `avatar chase`, `move surround`, `move rush`, `move rush to melee`, `move scripted`, `move to navmesh`, `move sidestep`, `move to post combat`, `move to post idle`, `move close in`, `follow make way`, `follow avoid LOF`, `follow formation`, `follow acquire`, `follow player`, `follow teleport`, `follow lemming`, `follow to car`, `follow other car`, `follow carjack`, `follow browse`, `investigate move`, `investigate look`, `investigate peek`, `reload`, `pickup weapon`, `human shield grab`, `taunt`, `vehicle passenger`, `vehicle enter scpt`, `vehicle extract`, `brute throw prop`, `brute car flip`, `brute bull rush`, `brute qte player`, `brute attack brute`, `brute gun finisher`, `avatar shockwave`, `avatar stomp`, `avatar taunt`, `avatar fireballs`, `avatar teleport stomp`, `avatar bullrush`, `quick kill`, `zombie eat`, `zombie explode`, `killbane chase`, `killbane strafe`, `roller blader change`, `idle` (ids 0…69 in that order). Entry layout: `+0x00` name `char*`, `+0x04` category id (0…0x2A), `+0x08` handler function pointer (NULL for the two `scripted` actions), `+0x0C` `u16` min-repeat-time, `+0x0E` `u16` auto-abort ms (image default 15000), `+0x10` `u16` no-interrupt ms, `+0x12` `u16` refresh-desire interval (image default 300), `+0x14` byte (image 0 except `cover stay down` = 1), `+0x15` byte precondition mask. **`combat_actions.xtbl` is read by `0x004F2650`, called from the `ai_behavior` loader; AH's `spec-tables-weapons-combat.md` owns that table's schema — the fields above are only what the AI loaders touch.**
- **AI goal table** (17 entries, stride `0x20`, at `0x012E22F0`; sentinel 17): `change mode`, `avoid brute`, `kill near player`, `survive`, `retreat`, `group`, `suppress`, `cover`, `taking_damage`, `advance`, `reload`, `kill enemy`, `flush out`, `frustrated`, `acquire target`, `investigate`, `idle`. Entry: `+0x00` name `char*`, `+0x04…+0x07` unknown, `+0x08…+0x1F` a **24-byte action-order list** filled by `ai_goals.xtbl` (§9).
- **Weapon-class names** (23, pointer array at `0x011899F0`): `pistol`, `smg`, `rifle`, `shotgun`, `launcher`, `thrown`, `knife`, `nightstick`, `stungun`, `bat`, `sword`, `pimp slap`, `video camera`, `knuckles`, `flamethrower`, `cutscene only`, `man cannon`, `minigun`, `pepper spray`, `chainsaw`, `waterspray`, `script`, `vehicle`.
- **Panic-reaction built-ins** (6, `0x012E1B68`): `On Fire`, `Pepper Spray`, `Sticky Projectile`, `Flashbang`, `Luch Grenade`, `Fart In A Jar`. **Precondition flag names** (5, `0x012E1B90`): `on foot`, `in vehicle`, `in water`, `must have target`, `can do in cover`.
- **Generic-character slot names** (21, `0x01308CD0`) and **generic-vehicle slot names** (20, `0x01308D28`) — §12.
- **Roadblock layout-flag names** (4, `0x013091F0`): `end cap`, `intersection`, `no flee`, `Can Spawn on Disabled Lanes`; **roadblock object-type names** (5, `0x01309200`): `Vehicle`, `Vehicle Group`, `NPC`, `Item`, `Action Node` — §5.
- Hard-coded 24 h-clock and capacity limits are given per table.

*(§§2–22 follow, then §23 the cross-reference map, §24 validation, §25 open items, §26 base-table validation.)*

## 2. `traffic_lanes.xtbl` — lane-speed classes

Loader `0x00A69060` (whole function read; 177 bytes). **[CONFIRMED — disassembly, exhaustive: no other element name is passed to any accessor.]**

**Element tree.** `Table` → `NewEntity` → `LaneGrid` → repeated **`LaneSpeed`** → child **`Speed`**.

| Element | Type | Destination | Notes |
|---|---|---|---|
| `LaneSpeed` (repeated) | row | array element *i* in document order | count taken with `CountChildren` first, then each row fetched with `NthChild` |
| `Speed` | float (always-write reader, §1.3) | `float[i]` at `0x02705420 + 4·i` | stored raw, **no unit conversion**; the largest value seen is kept in the float global `0x02705FEC` (reset to 0 first) |

The count is stored in `0x02705FE8`. **There is no bound check in the loader**; the array occupies `0x02705420…0x02705FE7` (`0xBC8` bytes = 754 floats) directly below the count global, so 754 is the implied ceiling (HIGH CONFIDENCE — layout adjacency, not exercised). A `LaneSpeed` without a `Speed` child would store an unspecified value (§1.3). The file's other content (the lane grid proper, etc.) is **not read by this loader** — whether other code reads the same file by another route was not searched (no second filename xref exists: the literal has exactly one reference). Consumers of the array (which lane class uses index *i*, the unit — mph vs m/s — of `Speed`) were not traced. *(Unit partially resolved 2026-09-23: m/s from editor metadata, HIGH CONFIDENCE, §25 item 11.)* **[OPEN.]**

## 3. `traffic_types.xtbl` — **no loader exists in the exe**

The literal `traffic_types.xtbl` does **not** occur in the image. The only occurrence of the text is the *tail* of `distant_vehicle_traffic_types.xtbl` (string at `0x0118BC90`; zero cross-references to that address). A raw case-insensitive byte scan for `traffic_type` finds exactly three strings — `Distant_Vehicle_Traffic_Type`, `distant_vehicle_traffic_types.xtbl` and `Traffic_Type` — all belonging to the distant-vehicle reader (§14). No `%s`-style format string producing a name ending `_types.xtbl` other than the fixed ones (`activity_types.xtbl`, `character_types.xtbl`, `distant_vehicle_traffic_types.xtbl`) exists. **Conclusion: nothing in the exe loads a file called `traffic_types.xtbl` by name; it is either unused by the retail binary or loaded through a name assembled at run time, which cannot be excluded statically.** ~~Its content, if it exists in the base game, is unreachable by this method.~~ **RESOLVED 2026-09-23** — the base-table container is now readable (`spec-vpp-container.md` §7); the file DOES exist (`misc_tables.vpp_pc` entry 256, 1,362 bytes) and its content is 8 free-text category rows (`TrafficTypes`/`Name`: Highway Only, All Roads, Surface Roads, Boat, Indoor, Offroad, Baggage, Taxi) — but having the file in hand changes nothing about the code side: still zero references to the filename anywhere in the exe. See §26. The *traffic-type* concept exists only as the `Traffic_Type` element of `distant_vehicle_traffic_types.xtbl` (§14). **[CONFIRMED — disassembly for the absence of a literal; HIGH CONFIDENCE for "not loaded".]**

## 4. `ambient_traffic_events.xtbl` — scripted ambient-traffic events (roadblock spawns on a schedule)

Loader `0x005E3660`; row reader `0x005E3430`. **[CONFIRMED — disassembly, both read in full.]**

**Element tree.** `Table` → repeated **`Ambient_traffic_event`** (row).

**Destination.** Rows are appended to an array at **`0x0149F210`, stride `0x48` (72 bytes), capacity 16**, count in `0x0149F1F8`; the loader stops at 16 rows (`>= 0x10`) and a row that fails validation is **skipped without consuming a slot**. Load returns success even if zero rows are valid.

| Element | Type | Field (row-relative) | Rule |
|---|---|---|---|
| `Name` | text (`char[0x40]`) | **not stored** | required non-empty; an empty/absent `Name` rejects the row |
| `TOD_start` | signed int | `+0x08` | must be `0…23`; **stored ×100** (an hour-of-day → `HH00` clock value) |
| `TOD_end` | signed int | `+0x0C` | must be `0…24`; stored ×100 |
| `Flags` | container of `Flag` children | byte `+0x40` **bit 0** | one name recognised: **`disabled_during_notoriety`**. The `Flags` element itself is **required** (absent ⇒ row rejected); other `Flag` text is ignored |
| `Min_life` | float | `+0x10` (`s32`) | valid range `0.1 … 240.0`; stored `int(value × 3 600 000)` (hours → milliseconds) |
| `Max_life` | float | `+0x14` | same range and scale; **must be ≥ `Min_life`** (checked after scaling) |
| `Cooldown_time` | float | `+0x18` | valid `0.0 … 240.0`; ×3 600 000 |
| `Roadblock_layout` | text (`char[0x40]`) | `+0x1C` (pointer) | required non-empty; resolved by **CRC of the name** against the roadblock-layout table (§5, lookup `0x0092C690`); a name that matches no layout stores NULL and **rejects the row** |

Bytes `+0x00…+0x07`, `+0x20…+0x3F`, `+0x41…+0x47` are not written by the reader (runtime state). Any range failure rejects the whole row. **Cross-reference:** `Roadblock_layout` → `roadblock_layouts.xtbl` `Name`. Units: *hours* (float) → milliseconds; *hour-of-day* (int) → hundredths. **[CONFIRMED — disassembly for the constants `0.1`, `240.0`, `3 600 000.0`, `100`.]**

## 5. `roadblock_layouts.xtbl` — physical roadblock compositions

Loader `0x0092BDE0` ("Reparsing roadblock tables" progress label), row reader `0x0092B7B0`, object-type matcher `0x0092AF70`, resource interner `0x0092B620`. **[CONFIRMED — disassembly, all read in full.]**

**Element tree.** `Table` → repeated **`Roadblock_layouts`** (row; the plural is the row element name) → `Name`, `Num_lanes`, `Flags`, `Objects` → repeated `Object` (`Type`, `Resource_name`, `Variant_Name`, `Lane_index`, `X_offset`, `Z_offset`, `Heading`, `Hitpoints`, `Children` → repeated `Child` (`Type`, `Resource_name`, `X_offset`, `Z_offset`, `Heading`, `Seat_Index`, `Hitpoints`)).

**Runtime containers** (three adjacent fixed arrays): layouts **`0x0261F588`, stride `0x28`, capacity 66 (`0x42`)**, count `0x026171CC` (`< 0x42`); objects **`0x02617E00`, stride `0x24`, capacity 850 (`0x352`)**, count `0x026171C8`; interned resources **`0x026172C0`, stride `0x18`, capacity 120 (`0x78`)**, count `0x026171C4`. The three counts are zeroed at the start of each parse. A layout that fails validation does not consume a slot; the loader returns true iff at least one layout loaded.

**Layout entry (`0x28` bytes)**

| Off | Field | Source / rule |
|---|---|---|
| `+0x00` | name pointer | `Name` text (`char[0x40]`, required) interned in the string pool at `0x02617260` (`0x00DB12C0`) |
| `+0x04` | name key | CRC-32 (§1.4, seed 0) of the interned name — **the key `0x0092C690` matches when other tables name a layout** (it returns the *last* entry whose `+0x04` equals the CRC) |
| `+0x08` | `Num_lanes` | signed int, valid `1…4` |
| `+0x0C` | count of type-0/1 objects | runtime, capped **6** |
| `+0x10` | count of type-2 objects | capped 16 |
| `+0x14` | count of type-3 objects | capped 16 |
| `+0x18` | count of type-4 objects | capped 6 |
| `+0x1C` | total objects | capped **32 (`0x20`)** |
| `+0x20` | first object pointer | head of a singly linked list of objects (each object's `+0x00` links to the next) |
| `+0x24` | flag byte | bits 0–3 from `Flags`/`Flag` text against the four names of §1.5 (`end cap` = bit 0, `intersection` = bit 1, `no flee` = bit 2, `Can Spawn on Disabled Lanes` = bit 3); **bit 4 (`0x10`) is set at load when an object of type 1 (`Vehicle Group`) is present** |

If bit 0 (`end cap`) is set, `Num_lanes` **must be 2** or the layout is rejected. `Objects`/`Object` may be absent (a layout with zero objects is discarded: the entry counts only if total > 0).

**Object entry (`0x24` bytes)**

| Off | Field | Source / rule |
|---|---|---|
| `+0x00` | next object | list link (top-level chain; runtime) |
| `+0x04` | first child | head of the object's child chain |
| `+0x08` | resource pointer | interned `(Type, Resource_name, Variant_Name)` — see below; NULL rejects the object |
| `+0x0C` / `+0x10` / `+0x14` | position | `X_offset` (float), Y (no element — keeps the initial value), `Z_offset` (float); the initial value is the global default vector (`0x029CDB98`, zero in the static image) |
| `+0x18` | `Heading` | float, stored **as read**. (`0x00DADEB0` — an angle-wrap into `[-π, π)` — is called on it but **its result is discarded** at both call sites, so the value is *not* normalised for top-level objects) |
| `+0x1C` | `Hitpoints` | signed int, if present (image default −1) |
| `+0x20` | `Lane_index` | byte, **top-level objects only**; must be `1…Num_lanes` |
| `+0x21` | `Seat_Index` | byte, **children only**; default `0xFF`; an unreadable/absent value gives `0xFF` |

`Type` is matched **case-insensitively** to the five names of §1.5 (index 0…4); an unknown or empty `Type` rejects the object. `Resource_name` must be non-empty. The per-type counters above are checked when an object is added: type 0 and 1 share the cap 6, type 4 caps at 6, types 2 and 3 cap at 16; exceeding a cap rejects that object (and ends the containing `Objects`/`Children` walk).

**Children.** After a top-level object is accepted, its `Children`/`Child` list is parsed with the same rules except: `Variant_Name` is ignored (variant index 0), there is no `Lane_index`, `Seat_Index` is read, and **the child's `Heading` is offset by the parent's heading and its X/Z offsets are rotated by the parent's heading** (child positions are parent-relative). Children are chained from the parent's `+0x04`; they count toward the same per-type and per-layout (32) caps.

**Resource interner (`0x0092B620`).** Key = CRC-32 of the *variant name* then, **chained with that value as seed**, the resource name (both lower-cased; variant absent = seed 0). Entry (`0x18` bytes): `+0x00` key, `+0x04` type, `+0x08` resolved value, `+0x0C` variant index (`u16`), `+0x10` −1, `+0x14` byte 0. A key already present is reused (its type must match). Resolution by type: **0 `Vehicle`** — the vehicle-info table lookup by name (`0x00AC2400`, the table of `spec-vehicle-data.md` §7.1); a non-empty `Variant_Name` is then resolved to a customisation-variant index (`0x00A95D60`), and a miss rejects the object; **1 `Vehicle Group`** — the name is resolved through the team registry (`0x0094CC60`; result 9 = unknown ⇒ rejected), stored as a byte; **2 `NPC`** — the character-definition registry lookup by name (`0x00BE3360`); **3 `Item`** — `0x00904C10` (an item lookup, not traced); **4 `Action Node`** — the action-node-group name → group index (`0x008C8500`, §13.1), stored `index + 1`. **[CONFIRMED — disassembly for the structure; HIGH CONFIDENCE for the *meaning* of each resolver (identified from the call, not read).]**

**Cross-references.** `Type=Vehicle` → vehicle table (§7.1 of `spec-vehicle-data.md`); `Type=Action Node` → `action_node_groups.xtbl` (§13.1); `Type=NPC` → the character registry (§12); `Type=Vehicle Group` → team names. Consumers: `ambient_traffic_events.xtbl` (§4), `roadblock_notoriety.xtbl` (§6), `roadblock_stag_lockdown.xtbl` (§7) each name a layout by `Name`.

## 6. `roadblock_notoriety.xtbl` — per-faction, per-notoriety-level roadblock schedule

Loader `0x0060A300`, row reader `0x0060A010`, team mapper `0x00609FC0`. **[CONFIRMED — disassembly, both read in full.]**

**Element tree.** `Table` → repeated **`Roadblock_notoriety`** (row) → `Name` (text, required non-empty; not stored), `Team` (text, required), `Levels` → repeated **`Level`** → `Notoriety_level`, `Max_active_roadblocks`, `Min_time`, `Max_time`, `Roadblock_layouts` → repeated **`Roadblock_layout`** → `Roadblock`, `Cooldown`.

**Destination.** A 5 × 5 array (team × notoriety level) of `0x44`-byte records at **`0x014A50B8`** (25 records, ending `0x014A575C`), each record's layout count zeroed before parsing. Record index = `team_slot × 5 + (Notoriety_level − 1)`.

- **`Team`** is resolved by `0x00609FC0` through the team registry (bucket hash `% 0x20`, `0x0094CC60`) to a team id; **only ids 1, 2, 3, 5, 6 are accepted and map to slots 0, 1, 2, 3, 4**; anything else (including an unknown name) rejects the whole row. (These are almost certainly the five notoriety factions; the id→name binding is built at run time and was not recovered. **HIGH CONFIDENCE for "five factions", OPEN for names.**)
- **`Notoriety_level`** signed int `1…5` else the whole row stops (a failing `Level` ends the parse of this `Roadblock_notoriety` row; levels already stored stay). A `Level` whose record already has layouts (a duplicate level for the same team) also stops the row.

| Element | Type | Record field | Rule |
|---|---|---|---|
| `Max_active_roadblocks` | s32 | `+0x00` | must be `1…3` |
| `Min_time` | s32 | `+0x04` | `0…300`, **stored ×1000** (seconds → ms) |
| `Max_time` | s32 | `+0x08` | `0…600`, ×1000 |
| `Roadblock_layouts` | container | — | **required**; up to **4** `Roadblock_layout` children are consumed |
| `Roadblock` (child of each `Roadblock_layout`) | text | layout pointer at `+0x14 + 12·k` | resolved by CRC against `roadblock_layouts.xtbl` `Name` (§5); required non-empty; unresolved (NULL) skips the entry |
| `Cooldown` | s32 | `+0x18 + 12·k` | `0…600`, ×1000; **out of range ⇒ the entry is not counted** (the pointer was written but the count is not advanced, so the next entry overwrites it) |

**Buffer-reuse quirk.** The `Team` text and every `Roadblock` text are copied into the *same* 64-byte stack buffer (`CopyText` leaves it untouched when the child is absent). A `Roadblock_layout` entry with no `Roadblock` child therefore re-resolves the **previous** text — for the first entry, the `Team` name — as a layout name. **[CONFIRMED — the buffer addresses in the assembly coincide.]**

`+0x10` = number of accepted layouts (must be ≥ 1 or the whole row stops); `+0x0C` and `+0x1C + 12·k` are not written (runtime). Record size `0x14 + 4×12 = 0x44`. **[CONFIRMED — disassembly, constants and limits read from the instructions.]**

## 7. `roadblock_stag_lockdown.xtbl` — fixed roadblock sites for the lockdown scenario

Loader `0x005E3FD0`, row reader `0x005E3F10`, row initialiser `0x005E32D0`. **[CONFIRMED — disassembly.]**

**Loaded only when the current level name begins `sr3_city`** (case-insensitive 8-character compare against the level-name global `0x013CDBA0`); in any other level the loader returns 1 without reading the file.

**Element tree.** `Table` → **`Roadblock_stag_lockdown`** → **`Roadblock_List`** → repeated **`Roadbock`** *(sic — the reader looks for this misspelt element name; a correctly-spelled `Roadblock` row would be invisible to it)* → `Roadblock_Layout`, `Spawn_Point`. Missing `Roadblock_stag_lockdown` or `Roadblock_List` ⇒ load returns 0.

**Destination.** Array at **`0x0149F690`, stride `0x28`, capacity 16**, count `0x0149F20C` (reset to 0 at load); a failing row is skipped without consuming a slot.

| Row field | Source | Rule |
|---|---|---|
| `+0x00` layout pointer | `Roadblock_Layout` text → CRC → `roadblock_layouts.xtbl` (§5) | required, must resolve |
| `+0x04…+0x0F` position `vec3` | `Spawn_Point` text = the **name of a placed world object**, looked up (case-insensitively) in the object registry at `0x02442750` (`0x005982E0`: multiply-33 bucket hash → chain compare, `0x004588F0`); position copied from object `+0x40…+0x48` | the lookup returns the object only if it exists **and** object byte `+0x33` bit `0x10` is clear **and** the category descriptor `0x02CC9900[obj+0x34]` has bit `0x02` set at its `+0x0B`; the row reader additionally requires object `+0x33` bit `0x04` clear and `+0x34 ≠ 0xFF` (`0x00853B10`) |
| `+0x10…+0x1B` vector | object `+0x64`, `+0x68`, `+0x6C` (a second `vec3`, likely a facing/forward vector) | — |
| `+0x20`, `+0x24` | initialised to two floats read from image `0x011254F8`/`FC` (both 0.0); runtime | — |

Row init (`0x005E32D0`) also clears `+0x00` and fills both vectors with the global default vector. **[CONFIRMED — disassembly for the copy pattern; OPEN: which subsystem owns the registry at `0x02442750` and the meaning of object bytes `+0x33/+0x34` — the object comes from the world/zone data, whose interior is the parked area and was not read.]**

## 8. `panic_reactions.xtbl` — reactions of bystanders to hazards

Loader `0x004EE110`, row reader `0x004EDEF0`, sub-reader `0x004EDD90`. **[CONFIRMED — disassembly, all read in full.]**

**Element tree.** `Table` → repeated **`Panic`** (row) → `Name`, `Priority`, optional `Timed` (`Min_Time_NPC`, `Max_Time_NPC`, `Min_Time_Player`, `Max_Time_Player`), optional `Movement` (`Run`, `Min_Dist`, `Max_Dist`), `Player_Animations` and `NPC_Animations` (each: `Anim_State`, `Anim_Action`, `Exit_Anim_Action`, `Enter_Anim_Action`, `NumAnimVariants`), optional `Drug_Effects` (`Booze_Factor_Max`, `Weed_Factor_Max`, `Booze_Factor_Min`, `Weed_Factor_Min`), optional `Persona_Situation`.

**Destination.** Array at **`0x0135B4A8`, stride `0x60` (96 bytes)**. The `Name` is hashed (CRC-32, seed 0). **Slots 0–5 are reserved for the six built-ins of §1.5**, in that order: a row whose name-CRC equals a built-in's fills that fixed slot; any other row is appended at index `DAT_012E1B80` (starts at **6**, incremented per appended row). **There is no capacity check on appended rows** (the array's real bound was not recovered; OPEN). After all rows, three callback pointers are written into the `+0x5C` word of slots 0, 1 and 2 (`0x0097CDD0`, `0x009BD730`, `0x004DD620`).

| Off | Field | Reader / rule |
|---|---|---|
| `+0x00` | name CRC | `Name`, seed 0 |
| `+0x04` | `Priority` | `u8` (always-write) |
| `+0x05` | flag byte | bit 0 = **no `Timed` element** (set when absent, cleared when present); bit 1 = `Movement` present; bit 2 = `Movement/Run` true; bit 3 = `Drug_Effects` present; bit 4 = `Persona_Situation` present and non-zero |
| `+0x08` / `+0x0C` | `Min_Time_NPC` / `Max_Time_NPC` | float **seconds ×1000 → ms** (`s32`), only when `Timed` present |
| `+0x10` / `+0x14` | `Min_Time_Player` / `Max_Time_Player` | likewise |
| `+0x18` / `+0x1C` | `Min_Dist` / `Max_Dist` | float, only when `Movement` present |
| `+0x20` / `+0x24` | `Weed_Factor_Max` / `Booze_Factor_Max` | float, when `Drug_Effects` present |
| `+0x28` / `+0x2C` | `Weed_Factor_Min` / `Booze_Factor_Min` | float |
| `+0x30…+0x43` | `Player_Animations` block | five dwords: `+0x30` `Anim_Action`, `+0x34` `Anim_State`, `+0x38` `Enter_Anim_Action`, `+0x3C` `Exit_Anim_Action` (each a **name → animation-name-table index**: CRC-32 of the text, then a linear scan of the table at `DAT_03171C10`, stride `0x10`, count `DAT_03171C08`, comparing the stored key then the string; **−1 if absent/unknown**), `+0x40` `NumAnimVariants` (signed) |
| `+0x44…+0x57` | `NPC_Animations` block | same five dwords |
| `+0x58` | `Persona_Situation` | text → Wwise event id (§1.4); 0 when absent, `none`, or empty |
| `+0x5C` | callback slot | 0 from the reader; slots 0–2 patched by the loader |

Sub-reader note: the animation-block reader returns immediately (leaving the block **uninitialised**) if the `Player_Animations`/`NPC_Animations` element is absent. **[CONFIRMED — disassembly.]** Cross-references: animation-name table (built by the animation-set loader, not in this group); Wwise event names.

## 9. `ai_goals.xtbl` — per-goal ordered action lists

Loader `0x004F29B0` (called first by `ai_behavior`'s loader, so it is always re-read together with it). **[CONFIRMED — disassembly, read in full.]**

**Element tree.** `Table` → repeated **`ai_goal`** (row) → `Name`, `order_of_actions` → repeated **`action_elm`** → `action`.

- `Name` is matched case-insensitively against the 17 goal names (§1.5). **An unmatched name (index 17) is an error (sets error flag `0x01361970`) and the row is skipped.**
- Each `action` text is matched case-insensitively against the 70 combat-action names (§1.5); **an unknown action sets the error flag and is dropped**; more than **24** actions sets the error flag (the 25th and later are dropped).
- Destination: the action-id bytes go to the goal entry's 24-byte list (`0x012E22F8 + goal·0x20 + n`); after the row the remainder of the 24 bytes is filled with **`0x47` (71 = "no action")**. A goal absent from the file keeps its image-initialised list (unread; the image bytes were not dumped).

The global error flag `0x01361970` is set by every soft failure in `ai_goals`, `ai_behavior` and `combat_actions`, but **the `ai_behavior` loader writes 0 to it as its last act**, so it has no persistent effect after a complete load (HIGH CONFIDENCE — its readers, if any, were not searched).

## 10. `ai_behavior.xtbl` — named combat behaviours (the AI "personality of a fight")

Loader `0x004F2B50`; it first calls `0x004F2650` (`combat_actions.xtbl`, **AH's table**) and `0x004F29B0` (§9). **[CONFIRMED — disassembly of the whole loader; offsets below are derived from its record-base arithmetic and cross-checked between the C and the assembly for the fields marked ★.]**

**Element tree.** `Table` → repeated **`Behavior`** (row) → `Name`, `Scripted_Only`, `combat_aggression`, `Human_Description` → repeated `human_elm` (`Weapon_Class`, `Team`, `Rank`), `Riotshield_Only`, `Actions` → repeated `ability_elm` (`Action`, `Repeat_min`, `Repeat_max`), `Options` (`can_fire_while_walking`, `can_back_step_fire`, `dodge_chance`), `Goals` → `Survive` (`low_health_pct`), `Suppress` (`length`), `Cover` (`take_cover_when` → `Flag`s, `abandon_cover_time`, `find_new_cover_time`, `advance_cover_time`, `stay_down_time`, `pop_up_time`, `squad_cover_pct`), `Taking_damage`, `Group` (`seperated_distance`, `seperated_distance_interior` — *sic* spellings), `Advance`, `Avoid_brute`, `Kill_near_player`, `Retreat`, `Kill_Enemy` (`seen_in_last`), `Reload` (`clip_pct`), `Flush_Out` (`seen_in_last`), `Acquire_Target` (`seen_in_last`), `Investigate` (`seen_in_last`), `goals_can_process_early`.

**Destination.** Record array at **`0x0135BFC8`, stride `0x200`, capacity 44 (`0x2C`; the 45th `Behavior` sets the error flag and stops the load)**; behaviour count in `0x013617C8`. The human-description sub-table is at **`0x0135B700`, stride `0x10`, capacity 140 (`0x8C`)**, count `0x0135BFC0`. Each record is `memset` to 0 first.

**Behaviour record (`0x200` bytes)**

| Off | Field | Rule |
|---|---|---|
| `+0x000` | record index `u32` | = position in the file |
| `+0x004` | `Name` `char[0x1E]` | bounded copy (NUL at `+0x21`) |
| `+0x022…+0x1C5` | **ability table** — 70 entries × 6 bytes, indexed by combat-action id | `Actions`/`ability_elm`: `Action` text → action id (unknown ⇒ error flag, skipped); entry `+0` `u16` `Repeat_min`, `+2` `u16` `Repeat_max` (**float seconds ×1000 → ms**, then `Repeat_min` is raised to the action table's `+0x0C` minimum and `Repeat_max` to at least `Repeat_min`), `+4` byte **enabled = 1**. **Action 14 (`hold position`) is always enabled with repeats 0** regardless of the file |
| `+0x1C6…+0x1D6` | **goal-enabled bytes**, one per goal id 0…16 (§1.5 order) | ★ set to 1 when the goal's own element (`Survive`, `Suppress`, `Cover`, `Taking_damage`, `Group`, `Advance`, `Avoid_brute`, `Kill_near_player`, `Retreat`, `Kill_Enemy`, `Reload`, `Flush_Out`, `Acquire_Target`, `Investigate`) exists under `Goals`; **`change mode` (0), `frustrated` (13) and `idle` (16) are always 1** |
| `+0x1D8` | `Survive/low_health_pct` | float |
| `+0x1DC` | `Group/seperated_distance` | float; **when `Group` is absent, `FLT_MAX` (3.4028235e38 — no separation limit)** |
| `+0x1E0` | `Group/seperated_distance_interior` | float; **defaults to the value of `seperated_distance`** when absent (the "if present" reader leaves the previous value) |
| `+0x1E4` | `Reload/clip_pct` | float |
| `+0x1E8`, `+0x1EA`, `+0x1EC`, `+0x1EE`, `+0x1F0` | `Cover/abandon_cover_time`, `find_new_cover_time`, `advance_cover_time`, `stay_down_time`, `pop_up_time` | float seconds ×1000 → `s16`. **When `Cover` is absent: 30000, 30000, 30000, 1000, 30000 ms** and `squad_cover_pct` = 100 |
| `+0x1F2` | `Suppress/length` | seconds ×1000 → `s16` |
| `+0x1F4`, `+0x1F6`, `+0x1F8`, `+0x1FA` | `seen_in_last` of `Kill_Enemy`, `Flush_Out`, `Acquire_Target`, `Investigate` | seconds ×1000 → `s16`, zeroed if the goal is absent; **then forced monotone non-decreasing** (`Flush_Out` ≥ `Kill_Enemy`, `Acquire_Target` ≥ `Flush_Out`, `Investigate` ≥ `Acquire_Target`) |
| `+0x1FC` | `Cover/squad_cover_pct` | `u8` |
| `+0x1FD` | `Options/dodge_chance` | `u8`, valid `0…100` (else error flag) |
| `+0x1FE` | `combat_aggression` | **first character of the text minus `'0'`**, valid `0…9` (else error flag); **forced to 1 when `Scripted_Only` is true** |
| `+0x1FF` | flag byte | bit 0 `Scripted_Only`; bit 1 `Riotshield_Only` (only read when the row is not scripted-only); bit 2 `goals_can_process_early`; bit 3 `Options/can_fire_while_walking`; bit 4 `Options/can_back_step_fire`; bits 5/6/7 = `take_cover_when` contains `aimed at` / `shot at` / `damaged` (`grenade noticed` is compared but sets nothing) |

A `Scripted_Only` (true) row skips `Human_Description` and `combat_aggression` entirely. **Human sub-table entry (`0x10` bytes)**, one per `human_elm` (a row with more than 140 in total sets the error flag and stops): `+0x00` weapon-class index (`Weapon_Class` text against the 23 names of §1.5 by `0x00DAC830`; **−1 if absent or unmatched**), `+0x04` byte team id (`Team` text via the team registry, default **9** = none), `+0x08` `Rank` text → pointer into the **rank/character-descriptor table at `0x029A91A0`** (stride `0x698`, count `0x029A9118`, first field a name pointer; **index 0 if not found** — which subsystem owns that table was not traced, OPEN), `+0x0C` back-pointer to this behaviour record. **[CONFIRMED — disassembly.]**

Cross-references: `Action` → `combat_actions.xtbl` names (AH's doc); goals → §9; `Team` → team registry.

## 11. `ai_personalities.xtbl` — ambient (non-combat) personality tweaks

Loader `0x00507AC0`. **[CONFIRMED — disassembly, read in full.]**

**Element tree.** `Table` → repeated **`Personality`** (row, **capacity 20**: `>= 0x14` stops the load; count in `0x013B84C8`) → `Name`, `combat_aggression`, `Flee_tweaks` (`Flee_health_pct`, `flee_events` → `Flag`s), `Ambient_tweaks` (`attack_unfriendly`, `Taunt_reaction` {`flee`, `taunt_back`, `attack`}, `MeleeReaction` {`Flee`, `Worry`, `DoNothing`, `Watch`, `Cheer`, `Attack`}, `VehicleWaitResponse` {`DoNothing`, `Honk`, `Threaten`}, `VehicleCollisionResponse` {`DoNothing`, `Honk`, `Threaten`, `Flee`, `GetOut`, `Attack`, `AttackPlayer`}, `HijackBehavior` {`JackFailureChance`, `PostResistFleeChance`, `PreJackFleeChance`}).

**Destination:** array at **`0x013B84D0`, stride `0x20`** (30 bytes used). All the `Ambient_tweaks` leaves are **`u8` always-write readers** (so an absent leaf is unspecified, §1.3; the values are percentages/weights, not clamped).

| Off | Field |
|---|---|
| `+0x00` | name CRC (seed 0) |
| `+0x04` | `combat_aggression` = first character − `'0'`, **clamped to ≤ 2** (values 0–2; a non-digit clamps to 2) |
| `+0x05` | `Flee_health_pct` |
| `+0x06` | `flee_events` bitmask, **OR-accumulated into the existing byte** (not cleared per row): `aimed at` 0x01, `melee hit` 0x02, `bullet heard` 0x04, `bullet hit` 0x08, `explosion heard` 0x10 |
| `+0x07` | `attack_unfriendly` |
| `+0x08…+0x0A` | `Taunt_reaction`: `flee`, `taunt_back`, `attack` |
| `+0x0B…+0x10` | `MeleeReaction`: `Flee`, `Worry`, `DoNothing`, `Watch`, `Cheer`, `Attack` |
| `+0x11…+0x13` | `VehicleWaitResponse`: `DoNothing`, `Honk`, `Threaten` |
| `+0x14…+0x16` | `VehicleCollisionResponse`: `DoNothing`, `Honk`, `Threaten` |
| `+0x17`, `+0x18` | `VehicleCollisionResponse`: `Flee`, `GetOut` |
| `+0x19`, `+0x1A` | `VehicleCollisionResponse`: `Attack`, `AttackPlayer` |
| `+0x1B…+0x1D` | `HijackBehavior`: `JackFailureChance`, `PostResistFleeChance`, `PreJackFleeChance` |

The `+0x06` mask is not zeroed by the reader; it relies on the static image being zero (the load runs once per process). **[CONFIRMED — disassembly.]** Cross-reference: personalities are keyed by the CRC of `Name` (`+0x00`); behaviours (§10) are stored with their name string and ordinal. Which consumers resolve them (e.g. the named-behaviour overrides documented in `spec-ai-behavior-format.md` §3) was not traced.

## 12. `generic_characters.xtbl` and `generic_vehicles.xtbl` — the generic-population slot maps

Loaders `0x009189A0` (via helper `0x009188C0`) and `0x009186E0`. **[CONFIRMED — disassembly, all read in full.]**

Both files have the identical shape: `Table` → repeated **`Generics_Table`** (row) → `Name`, `Spawn_Name`, and (vehicles only) `Variant_Name`. **The `Name` selects a fixed slot from a list compiled into the exe; the row does nothing for a `Name` outside the list.**

- **`generic_characters.xtbl`** — 21 slots (names in §1.5 order, `Young Male` … `Camera Man`). `Name` and `Spawn_Name` are both CRC-hashed (seed 0); the row's `Name` CRC is compared against the 21 slot-name CRCs (built at load); on a match the slot's word in the array **`0x02609C80`** (21 dwords) is set to the **character-definition record** found by `Spawn_Name`'s CRC through the registry lookup `0x00BE3320` (bucket hash `0x00DAB2B0(crc, 500)` then a key compare; **0 if not found**). *A later row for the same slot overwrites an earlier one.*
- **`generic_vehicles.xtbl`** — 20 slots (`Truck`, `Police Car`, `Police Helicopter`, `Attack Helicopter`, `Swat Car`, `Swat APC Police`, `Swat APC Ultor`, `Armored Truck`, `News Van`, `Taxi`, `Ambulance`, `Garbage Truck`, `Delivery Gyro`, `Delivery Freckled Bitches`, `Delivery Lik A Chick`, `Pimp`, `Ship It`, `Firetruck`, `Hazmat`, `Gang Wagon`). **`Name` is matched by a case-*sensitive* byte compare** against those 20 (unlike the character file, whose names go through the lower-casing CRC). On a match, `Spawn_Name` is CRC-hashed and looked up in the **vehicle-info table by name hash** (`0x00AC2560`, `spec-vehicle-data.md` §7.1); a hit stores the **vehicle-entry pointer** at `0x02609BE0 + 8·slot` and `Variant_Name` (text → interned string from the pool object at `0x01495410`) at `0x02609BE4 + 8·slot`. **An unresolvable `Spawn_Name` leaves the slot untouched.** **One entry loads both files:** the `generic_characters` routine `0x009189A0` hashes the 20 vehicle-slot names (result unused) and calls the `generic_vehicles` loader `0x009186E0` before reading its own file.

Cross-references: `Spawn_Name` → `vehicles.xtbl`/`_veh.xtbl` `Name` (vehicles) and the character-definition table (characters; `character_definitions.xtbl` family, **AI/AH/AK's docs — not verified here**); `Variant_Name` → the vehicle's customisation variant name (`spec-customization-data.md`).

## 13. Action-node tables (`action_node_groups`, `action_node_notoriety`, `action_node_npcs`, and the `action_nodes` placement file)

Action nodes are the world's ambient-behaviour anchors (sidewalk activities, benches, store browsing…). The three `.xtbl` *definition* tables are small registries that a placement file (`action_nodes.xtbl`, §13.4) refers to by name-CRC.

### 13.1 `action_node_groups.xtbl` (loader `0x008C9760`)

`Table` → repeated **`action_node_group`** (row). **Array at `0x024ED760`, stride `0x6C`, capacity 200**, count `0x024ED744` (the 201st row is dropped and the count clamped to 200). **[CONFIRMED — disassembly, read in full.]**

| Off | Element | Type / rule |
|---|---|---|
| `+0x00` | `Name` | `char[0x1E]` → CRC-32 (seed 0) **= the group key** used by §5 (`Action Node` object type, `0x008C8500` returns the row index) and §13.4 |
| `+0x04` | `Min_spacing` | float |
| `+0x08` / `+0x0C` | `Convert_to_ped_min_delay` / `_max_delay` | `u32`; min is clamped down to ≤ max; min defaults to 0 |
| `+0x22` (low nibble) | `Spawning` → `Flag` children | names `night`=bit 0, `day`=1, `noon`=2, `evening`=3 (through `HasFlag` `0x00DAC7D0`); upper nibble preserved |
| `+0x20` (byte) | flags | bit 0 `Capture_peds`, 1 `Outdoor_node`, 2 `Use_In_Rain`, 3 `Store_browsing`, 4 `Store_ownership_spawn`, 5 `Exclusive_Actions`, 6 **`Reference_object` present**, 7 `sequence_nodes` |
| `+0x21` (byte) | flags | bit 0 `On_Water`, 1 `Player_Allowed`, 2 `Spawn_During_High_Notoriety`, 4 `GroupDisabled`, 6 `Stag_Node`, 7 `Anti_Saints_Node` (all the bool readers of §1.3: only `true`/`yes` set) |
| `+0x24` | `Reference_object` | text → CRC (seed 0); bit 6 of `+0x20` records presence |
| `+0x28` | `Spawn_Timer` | s32; **any value < 60 becomes −1** (disabled) |
| `+0x2C` / `+0x2E` | `Instance_Cap`, `MaxSpawns` | signed → stored as a byte; default −1 (= 0xFF) |
| `+0x3C…+0x4B`, `+0x4C` | `npc_list` → repeated **`npc`** (text = an §13.3 row `Name`) | up to 4 pointers into the §13.3 table (CRC match); count byte at `+0x4C`. **The loop has no 4-entry bound check** (a 5th matching NPC would write `+0x4C` itself) |
| `+0x50` | `Notoriety_Info` | text → CRC → pointer to the matching §13.2 row (0 if none) |
| `+0x54` | `Music_Emitter` | text → **Wwise event id** (§1.4); 0 if absent |
| `+0x58` | `Min_Player_Rank` | u32, default 0 |

**[CONFIRMED — disassembly.]**

### 13.2 `action_node_notoriety.xtbl` (loader `0x008C83B0`)

`Table` → **`Action_Node_Notoriety`** (row). **Only the *first* row is ever stored** — the loader's per-row test is a straightforward comparison of the running row count against `1`, branching past the store once the count has reached it (so only the first row, while the count is still below `1`, is ever stored), and the record array at `0x024ED74C` (stride `0x14`) is followed immediately by the §13.1 array. Record: `+0x00` `Name` CRC, `+0x04` byte flags (bit 0 `Spawn_Flags/Air_Only`, bit 1 `Spawn_Flags/Gang`; bool readers), `+0x08` `Min_Notoriety` s32, `+0x0C` `Max_Notoriety` s32, `+0x10` `Weapon` text → weapon-table index/pointer (`0x008DCB10`: scan of the 110-entry weapon table at `0x0250ADC0`, stride `0x34`, matching the text against the entry's ASCII name **or** its wide-string name; only entries with bit 0 of `+0x30` set are considered; returns the scan result, **unspecified if no match**). **[CONFIRMED — disassembly. The "first row only" rule is a quirk of the shipped binary: any additional `Action_Node_Notoriety` rows in a base table are silently ignored.]** Cross-reference: `Weapon` → `weapons.xtbl` (AH's doc, `spec-tables-weapons-combat.md`).

### 13.3 `action_node_npcs.xtbl` (loader `0x008C87D0`)

`Table` → repeated **`action_node_npc`** (row). **Array at `0x024F2BC0`, stride `0xCC`, capacity 150 (`0x96`)**, count `0x024ED748`. Two passes (pass 2 wires `Follow_Nodes`). **[CONFIRMED — disassembly, read in full.]**

| Off | Element | Type / rule |
|---|---|---|
| `+0x00` | `Name` | `char[0x1E]` → CRC-32 (row key) |
| `+0x04` | `Persona_situation` | Wwise id (§1.4) |
| `+0x08`, `+0x0C`, `+0x10` | `Enter_anim`, `Exit_anim`, `Startle_anim` | animation-name index (§8; −1 if unknown) |
| `+0x14…+0x73` | `Animations` → repeated `State` (`Animation_State`, `Actions` → repeated `Animation_action`) | 6-dword records: `+0` state index, `+4…+0x10` up to **4** action indices (**unknown names dropped**), `+0x14` count. **The `State` loop has no bound check (4 states fit before `+0x74`)** |
| `+0x74` | count of `State` rows | u32 |
| `+0x78`, `+0x7C…+0x87` | `CowerAnims` → repeated `CowerAnim` | count, then up to **3** animation indices (bounded) |
| `+0x88`, `+0x8C` | `Min_anim_delay`, `Max_anim_delay` | float seconds ×1000 → `s32` |
| `+0x90` | flag byte A | **0** `Synced_Animation` · **1** `Single_Use` · **2** `Flee_On_Exit` · **3** `Get_Bag` · **4** `Reverse_Enter` · **5** `Can_Use_With_Bag` · **6** `Never_Exit` · **7** `Can_Not_Exit` (True sets bits 6 **and** 7; False clears only bit 7). `Flee_On_Exit` is **read only when `Single_Use` is `True`** |
| `+0x91` | flag byte B | **0** `Spawn_Only` · **1** `Standardize_Height` · **2** `React_To_Peds` · **3** `Required_NPC` · **4** `NPC_Dies` · **5** `Drunk_Node` · **6** `Can_Be_Bumped` · **7** `Stationary_Node` (**default set**) |
| `+0x92` | flag byte C | **0** `Spawn_Priority` (`Sidewalk (normal)` = 0, `Action Node (high)` = 1) · **1** `ExitOnItemDislodge` · **2** `SpawnInAir` · **3** `SpawnOffNavmesh` · **4** `SupportsNonHeroic` (**default set**) · **5** `SupportsHeroic` · **6** `EquipWeapon` · **7** `RequiresRifle` |
| `+0x94…+0xA3`, `+0xA4` | pass 2: `Follow_Nodes` → repeated `Node` (text = another NPC row's name) | up to 4 row pointers, count at `+0xA4` (bounded) |
| `+0xA8` | `Random_destination_percent` (pass 2) | float `= 1.0 − percent / 100` |
| `+0xAC…+0xBB`, `+0xBC` | `Model_List` → repeated `Model` | character-definition records (registry lookup `0x00BE3360`; NULL if unknown), **no bound check** (4 fit), count at `+0xBC` |
| `+0xC0` | `Num_Resource_Requests` | u32, **clamped to the model count** (0 if there are no models) |
| `+0xC4` | `Sex` | `male` → 0, `female` → 1 (case-insensitive), default **3** |
| `+0xC8` | `Team` | `Civilian` 0, `Police` 1, `Gang` 2, `Saints` 3, other/absent −1 |

The text-boolean elements of bytes A–C are compared as the strings `True`/`False`; a value that is neither leaves the byte at its pre-set default (§1.3). **[CONFIRMED — disassembly.]**

### 13.4 `action_nodes.xtbl` — the placement file (loader `0x008F08A0`)

*Structure only; not a gameplay-tuning table.* Parsed by `0x00DC5AC0` (§1.2): **`object` rows sit directly under the document root — there is no `Table` wrapper** — under a dedicated pool labelled "action node parse". Per `object`: `name` (`char[0x18]`, CRC-hashed), `num_action_nodes`, `num_spawn_nodes`, `num_vehicles`, repeated `action_node` (reader `0x008F02C0`: `group` — an §13.1 name — `npc` — an §13.3 name, resolved to a row index by `0x008C85A0` — `transform`, `player_node` bool), `spawn_node` (reader `0x008F0190`: `group`, `name`, `transform`) and `vehicle` (`group`, `name` — a vehicle-table name, resolved through the vehicle-info name hash — and `transform`) children. **`transform` is a single text element of seven whitespace-separated floats** (`strtok`/`atof` on `" \r\n\t"`), unlike the `X`/`Y`/`Z` child convention of §1.3. Objects are indexed in an open-addressing table of 1000 slots (`0x025EF148`), **capacity 500 objects** (`0x025EF140` counts), entries `0xC` bytes at `0x025F00E8`. The per-object packing (`0x008EFEF0`) and the runtime use were not traced. **[CONFIRMED — disassembly for element names, counts and the transform grammar; OPEN — record layout and consumers.]**

## 14. Distant-population tables (`distant_peds`, `distant_ped_colors`, `distant_ped_spawn_parameters`, `distant_vehicles`, `distant_vehicle_colors`, `distant_vehicle_spawn_parameters`, `distant_vehicle_traffic_types`)

The "distant" system spawns cheap far-field pedestrians and vehicles. Seven loaders, all with the standard `Table` prologue, all **[CONFIRMED — disassembly, every function read in full]**. Colour convention shared by both colour tables: a colour element has children **`R`, `G`, `B`**, each an always-write float **divided by 255** on load (`0x00DAD0A0`); a named colour child that is absent is skipped (`0x00DAD130` — the destination keeps whatever it held).

### 14.1 `distant_peds.xtbl` (loader `0x00B8FAD0`)

`Table` → repeated **`Distant_Pedestrian`** → `Name`, `Mesh`. Destination: **`0x0290ACB0`, stride `0x888`, capacity 3** (the loop stops at a count of 3; count in `0x0290AC9C`, zeroed first). `Name` → `char[0x19]` at `+0x00`; **`Mesh`** → `char[0x41]` at `+0x19`. Both are bounded copies (no other field is written by this loader).

### 14.2 `distant_ped_colors.xtbl` (loader `0x00B8FB60`)

`Table` → repeated **`Color_Set`** → `Skin_Colors` → repeated `Skin_Color`; `Hair_Colors` → repeated `Hair_Color`; `Clothing_Colors` → repeated `Clothing_Color_Set` → `Undershirt_Color`, `Overshirt_Color`, `Pants_Color`, `Shoe_Color`, `Accent_Color`. **All `Color_Set` rows are merged into three global pools** (the set boundary is lost): skin colours at **`0x0290C6A8`**, `vec3` stride `0xC`, **capacity 6** (count `0x0290AC94`); hair colours **`0x0290C648`**, `0xC`, **capacity 8** (count `0x0290AC98`); clothing sets **`0x0290C6F0`**, stride **`0x3C`** = five `vec3` (`Undershirt` `+0x00`, `Overshirt` `+0x0C`, `Pants` `+0x18`, `Shoe` `+0x24`, `Accent` `+0x30`), **capacity 32** (`0x20`; count `0x0290AC90`). Excess rows are ignored. Each `vec3` = (R, G, B)/255.

### 14.3 `distant_ped_spawn_parameters.xtbl` and `distant_vehicle_spawn_parameters.xtbl` (loaders `0x00B8FD40`, `0x00B92CE0`)

`Table` → one **`Distant_Ped_Spawn_Parameters`** / **`Distant_Vehicle_Spawn_Parameters`** element whose children are (all always-write floats): `Length_Per_Spawn` (peds only), `Base_Spawning_Dist`, `Max_Spawning_Dist`, `Double_Spawn_Height`, `Despawn_Expansion_Distance`, `Camera_Lookahead_Time`, `Spawn_Angle`, `Despawn_Angle`. Conversions: **`Length_Per_Spawn` is stored as its reciprocal** (`1 / value`, in double precision then narrowed); **`Spawn_Angle` and `Despawn_Angle` are degrees and are stored as `cos(angle · 0.01745300)`** (the constant is the single-precision π/180 = 0.0174530; the routine is the C runtime `cos`), i.e. as dot-product thresholds. Destinations (ped / vehicle): Length_Per_Spawn `0x0130F400` / — ; Base `0x0130F40C` / `0x0130F4E0`; Max `0x0130F408` / `0x0130F4DC`; Double_Spawn_Height `0x0130F404` / `0x0130F4D8`; Despawn_Expansion_Distance `0x0130F410` / `0x0130F4E4`; Camera_Lookahead_Time `0x0130F414` / `0x0130F4E8`; Spawn_Angle `0x0130F418` / `0x0130F4EC`; Despawn_Angle `0x0130F3FC` / `0x0130F4CC`. **Image values before the file loads** (usable as documented defaults *only if the element tree is absent altogether*; the always-write readers otherwise overwrite them): ped 0.04 (Length_Per_Spawn, already a reciprocal), Base 200, Max 1200, Double_Spawn_Height 100, Despawn_Expansion 25, Lookahead 0.25, Spawn_Angle 0.8, Despawn_Angle 0.6; vehicle Base 200, Max 1200, Height 100, Expansion 25, Lookahead 0.25, Spawn_Angle −0.4, Despawn_Angle −0.5. **[CONFIRMED — disassembly for stores and constants; the `cos` identification is from the callee body (`FCOS`).]**

### 14.4 `distant_vehicle_traffic_types.xtbl` (loader `0x00B93000`, takes a mode byte)

`Table` → repeated **`Distant_Vehicle_Traffic_Type`** → `Name`, `Spline_Type`, `Length_Per_Spawn`, `No_Spawn_Zone`, `Dummy_Car_Length`. **This is the only place a "traffic type" is defined** (§3). Mode 0 (initial load) allocates `count × 0x1C` bytes at `0x0290CE88` (count `0x0290CEB8`); mode ≠ 0 is a *refresh*: it returns immediately if the row count differs from the loaded one, and otherwise re-reads only `Length_Per_Spawn`, `No_Spawn_Zone` and `Dummy_Car_Length` (not `Spline_Type`) for each row whose `Name` CRC equals the type at the same position. Type record (`0x1C`): `+0x00` `Name` CRC (seed 0), `+0x04` `Spline_Type` (`u32`, unsigned always-write) **stored as `1 << n`** (a one-hot mask; `n` ≥ 32 wraps), `+0x08` `Length_Per_Spawn` **stored as `1 / value`**, `+0x0C` `No_Spawn_Zone` float, `+0x10` `Dummy_Car_Length` float, `+0x14` per-type vehicle count (runtime), `+0x18` per-type vehicle list pointer (runtime).

### 14.5 `distant_vehicle_colors.xtbl` (loader `0x00B92C40`)

`Table` → repeated **`Distant_Vehicle_Color`** → `Color` (R, G, B). One `vec3` per row in a heap array of `count × 0xC` bytes at `0x0290CE8C` (count `0x0290CEB0`).

### 14.6 `distant_vehicles.xtbl` (loader `0x00B93180`)

`Table` → repeated **`Distant_Vehicle`** → `Name`, `Mesh`, `Traffic_Type`. A heap array of `count × 0x30` at `0x0290CE90` (count `0x0290CEAC` = the **raw** row count). Entry: `+0x00` `Name` `char[0x19]`; `+0x19` byte ordinal; `+0x1C` mesh resource — the `Mesh` text is CRC-hashed and looked up in the **item/mesh registry** (`0x00904C10`: table at `0x025F5B98`, stride `0xB8`, count `0x025F5B8C`, key at `+0x04`), then resolved to a `.csmesh_pc` resource with the registry entry's name/variant (`0x007520B0`); `+0x2C` pointer to that registry entry; `+0x20` and `+0x24` **light indices** — the index of the mesh's records named `headlight` / `taillight` (record scan, stride `0x60`, name at `+0x18`; −1 if none); `+0x28` **traffic-type index** — `Traffic_Type` text CRC matched against §14.4 (−1 if none). **A row whose mesh does not resolve is not advanced over** (the write cursor only moves for resolved rows, so the tail of the array is left unwritten while the count stays the raw row count). After the pass each type's vehicle count is used to allocate a `count × 4` pointer list (type `+0x18`), the OR of all used types' `Spline_Type` masks is kept in `0x0290CEB4`, and every resolved vehicle with a valid type is appended to its type's list. **[CONFIRMED — disassembly.]** Cross-references: `Mesh` → the item/mesh registry (`items_3d.xtbl`-family, not verified); `Traffic_Type` → §14.4.

## 15. `homies.xtbl` — the crew roster (and every `<framework>_homies.xtbl`)

Loaders `0x007E6E70` (base) and `0x007E7580` (per-DLC), **one shared row reader `0x007E6760(filename, framework)`**. The base call passes `homies.xtbl` and the framework `main` and first zeroes the three counters; the DLC path builds the name **`<framework>_homies.xtbl`** (`"%s_%s"`, `0x0045BA50`; the bare `homies.xtbl` instead if bit 2 of descriptor byte `+0x103` is set or the framework name is empty) for every loaded framework whose descriptor has a valid byte at `+0x101` and bit 1 of `+0x103` set. **The DLC files are therefore the identical schema — the three raw `dlcN_homies.xtbl` samples validate this section directly (§24).** **[CONFIRMED — disassembly, the whole reader read; empirical for the three DLC files.]**

**Element tree.** `Table` → repeated **`Homie`** → `Name`, `Display_Name`, `Display_Desc`, `Humans` → repeated `Human` → `Character`; `Vehicles` → repeated **`Vehicle_Entry`** → `Vehicle`, `Vehicle_Variant`, `Weight`; `Flags` → repeated `Flag`; `Image_name`; `Audio`; `Blocked_for_Mission` → repeated `Flag`; `Framework`; `Is_DLC`. `_Editor` (present in the files) is never read.

**Row selection.** A row's `Framework` text (default `main`) must equal (case-insensitively) the framework being loaded (or the caller passes none); a row with framework `main` goes to the **base array `0x022AD780`, stride `0x180`, capacity 28 (`0x1C`)** (count `0x022AD758`); any other framework goes to the **DLC array `0x022B0180`, stride `0x180`, capacity 16** (count `0x022AD75C`); total in `0x022AD754`. A full array **ends the whole file's load** (`break`).

| Row off | Field | Reader / rule |
|---|---|---|
| `+0x00` | `Name` | `char[0x1F]`, bounded copy |
| `+0x20` | name key | CRC-32, seed 0, **case-sensitive** (`0x00D9E7E0` — the one CRC in this group that does *not* lower-case its input) |
| `+0x24` | DLC gate byte | `Is_DLC` true/yes → `0x00`, otherwise `0xFF` (the vehicle table's `+0x1C` convention, `spec-vehicle-data.md` §7.3) |
| `+0x28`, `+0x2C` | `Display_Name`, `Display_Desc` | **localisation keys**: CRC-32 (seed 0, lower-cased, `0x00D9E740`) of the text, kept only if the text table recognises that key (`0x00849950`), else 0 |
| `+0x30` | `Image_name` | CRC-32 (seed 0) of the UI-image name |
| `+0x34…+0x53`, `+0x54` | `Humans` → `Human` → `Character` | character-definition record pointers (`0x00BE3360`: CRC, bucket-hash `% 500`, registry; NULL if unknown); count at `+0x54`; **8 fit, no bound check**. **Hard-coded special case:** a `Character` beginning `npc_AlienBrutella` (compare of the first 17 characters, case-sensitive) replaces every vehicle with `truck_4dr_pickup07` variant `Saints` |
| `+0x58…+0x67`, `+0x68`, `+0x6C` | `Vehicles` → `Vehicle_Entry` | 8-byte pairs `{vehicle handle u32, Weight s32}`; handle = `Vehicle` name → vehicle-table slot (`spec-vehicle-data.md` §7.1: low 16 bits slot, high 16 bits variant index resolved from `Vehicle_Variant`, `0xFFFF` = none; an unknown vehicle gives the all-`0xFFFF` handle); `+0x68` = count, `+0x6C` = **sum of weights**. **Two pairs fit; a third overwrites the count (no bound check)** |
| `+0x70` | `Audio` | Wwise event id (§1.4) |
| `+0x78`, `+0x7C` | — | two dwords copied from an image constant (`0`) |
| `+0xBC` | — | zeroed |
| `+0xF0` | framework number | the digits after the substring `dlc` in the framework name (`atoi`), `0xFF` if the name has none |
| `+0xF1` | flag byte | from `Flags`/`Flag`, **case-sensitive exact strings**: `clear gang notoriety` 0x01, `clear police notoriety` 0x02, `form own squad` 0x04, `give random weapons` 0x08, `player selects vehicle` 0x10, `repair players vehicle` 0x20, `turn to ped after driveup` 0x40, `emergency` 0x80 |
| `+0xF2` | flag byte | `taxi` 0x01, `requires saints` 0x02 |
| `+0xFC`, list at `*(+0xF4)`, capacity `+0xF8` | `Blocked_for_Mission` | each non-empty `Flag` text is matched case-insensitively against the **33-entry mission-name array at `0x012FF868`** (`m04`, `m05`, `mm_p_01`…`mm_p_07`, `sh01`, `m06`–`m08`, `sh02`, `m09`–`m15`, `sh03`, `m16`, `mm_m16_5`, `m17`–`m20`, `sh04`, `m21`–`m24`); the matching index is appended to the row's list (max 30, and only while the count ≤ the capacity in `+0xF8`; **`+0xF4`/`+0xF8` are not set by this reader** — the list buffer is provided elsewhere, OPEN) |

The editor description block of each DLC file lists an extra flag **`is wheelman`** that **this reader never tests** (all ten tested strings are listed above; §24). **[CONFIRMED.]**

## 16. `follower_heads.xtbl` — follower head-portrait associations

Loader `0x007F0670`. `Table` → **`Heads`** → **`Associations`** → repeated **`Follower`** → `Persona`, `Charname`, `Bitmap`. Heap array `count × 0x28` at `0x022B7924` (count `0x022B7928`, from `CountChildren`). Entry: `+0x00` `Persona` → Wwise event id (0 if absent); `+0x04` `Charname` → CRC-32 (seed 0, lower-cased; 0 if absent); `+0x08` `Bitmap` → an **unbounded string copy** into the remaining 32 bytes (`+0x08…+0x27`) — no length check, and **an absent `Bitmap` element dereferences a null pointer (the element is effectively required)**. **[CONFIRMED — disassembly.]** Cross-reference: `Charname` = the character-definition name CRC (same key as §12, §15); `Persona` = audio persona event.

## 17. `driver_bailout.xtbl` — reward and penalty rules for making a driver bail out

Loader `0x006A5310`. `Table` → one **`Driver_Bailout`** whose leaves are all direct children. Every leaf is an always-write reader except the two `Record_*_Time` (write-if-present); values are post-processed as follows (**[CONFIRMED — disassembly, all constants read from the image]**):

| Element | Destination | Type / rule |
|---|---|---|
| `Max_Distance`, `Min_Distance` | `0x014C2D44`, `0x014C2D48` | float; **if `Max ≤ 0` or `Max < Min`, both are set to 0** |
| `Max_Damage`, `Min_Damage` | `0x012F006C`, `0x014C2D54` | `u32`; if `Max == 0` or `Max < Min`, both 0 |
| `Max_Damage_Percent_Reward` | `0x012F0060` | float ÷ 100, **clamped to `[0, 1]`** |
| `Min_Velocity` | `0x012F0064` | float, clamped ≥ 0, then **× 0.44704 (mph → m/s) and squared** (a speed² threshold) |
| `End_Time` | `0x014C2D50` | float seconds, clamped ≥ 0, ×1000 → ms (rounded) |
| `Record_Display_Time`, `Record_Queue_Time` | `0x014C2D58`, `0x014C2D5C` | float seconds, ≥ 0, ×1000 → ms; **default 0 when absent** |
| `Record_Threshold` | `0x014C2D4C` | float ≥ 0, ÷ 100 |
| `Show_Damage_Record` | `0x014C2D60` | bool |
| `Max_Respect`, `Max_Lifetime_Respect` | `0x014C2D64`, `0x014C2D68` | s32, clamped ≥ 0 |
| `Max_Cash` | `0x014C2D6C` | float, clamped ≥ 0 |
| `Vehicle_Damage_Multiplier` | `0x012F0068` | float (image value 1.0 before load) |

Unit note: this is the only table in the group with an explicit **mph → m/s** conversion (constant `0.44703999…`, single precision).

## 18. `vehicle_despawn.xtbl` (and `vehicle_despawn_mp.xtbl`) — ambient vehicle despawn schedule

There is **no direct code reference to the filename**: both names are entries of a two-element table of `char*` (`0x01308964` = `vehicle_despawn.xtbl`, `0x01308968` = `vehicle_despawn_mp.xtbl`), and *every element name is also held in a `char*` global* (`0x0130896C` `Table`, `0x01308970` `num_cars`, `0x01308974` `time`, `0x01308978` `abandon_despawn_info`, `0x0130897C` `in_view_despawn_info`, `0x01308980` `out_of_view_despawn_info`, `0x01308984` `safety_despawn_time`, `0x01308988` `abandon_despawn_delay`, `0x0130898C` `player_vehicle_despawn_distance`). The ambient-vehicle manager's initialiser (`0x00908FB0`, which ends by calling `0x00905C20`) hands the chosen filename to the parser **`0x00905C20`**; only the code path for `vehicle_despawn.xtbl` was followed (which mode selects the `_mp` file was not traced). **[CONFIRMED — disassembly for the parse; OPEN: file selection.]**

**Element tree as read.** `Table` → **a second element named `Table`** *(the parser descends into a child of the document's `Table` that is itself called `Table`)* → `in_view_despawn_info`, `out_of_view_despawn_info` (each holds repeated **`abandon_despawn_info`** children, each `{num_cars, time}`), `abandon_despawn_delay`, `player_vehicle_despawn_distance`. `safety_despawn_time` is named in the descriptor but **not read by `0x00905C20`** (its other users were not followed).

| Element | Destination | Rule |
|---|---|---|
| `in_view_despawn_info` / `abandon_despawn_info` | array of `{s32 num_cars, s32 time}` at **`0x026092C8`**, count at `+0x50` | `time` (seconds) **×1000 → ms**. **10 entries fit** (the next array is `0x54` bytes on); no bound check |
| `out_of_view_despawn_info` / `abandon_despawn_info` | same layout at **`0x0260931C`** | same |
| `abandon_despawn_delay` | s32 → `0x02609370` **and** `0x02609374` | the *same* element is read into both (×1000 ms each) |
| `player_vehicle_despawn_distance` | s32 → `0x02609378` | **stored squared** |

The first `abandon_despawn_info` is fetched with the find-first primitive and the remainder with the find-next primitive, so any number of siblings is consumed in document order.

## 19. `Escort_constants.xtbl` — escort-mission ("Tiger") tuning constants

Literal `Escort_constants.xtbl` (capital `E`); loader at `0x0067F1D0` — **Ghidra had not defined a function there** (the only reference to the literal, `0x0067F250`, sits in undefined code; the block was disassembled and a function created in the disposable project copy). `Table` → **`Escort_Constants`** → **`Tiger_Constants`** → `Rage` {`Desired_speed_MPS`, `Rage_Attack_Damage_HP`, `Rage_Increase_Rate`, `Rage_Decrease_Rate`}; `Penalties` → `Vehicles` {`Vehicle_Damage_Penalty_MS`, `Vehicle_Damage_Threshold`, `Vehicle_Damage_Cooldown_MS`}, `Humans` {`Human_Damage_Penalty_MS`, `Human_Damage_Threshold`}, `Movers` {`Mover_Damage_Penalty_MS`, `Mover_Mass_Threshold_KG`, `Mover_Damage_Cooldown_MS`}, `World` {`World_Damage_Penalty_MS`, `World_Damage_Threshold_HP`, `World_Damage_Cooldown_MS`}. **[CONFIRMED — disassembly, read in full.]**

**Defaults are written first, and each group's leaves are always-write readers**: the image-supplied defaults below survive **only when the whole group element (`Rage`, `Vehicles`, `Humans`, `Movers`, `World`) is absent**; a present group missing a leaf stores an unspecified value (§1.3). Destination / default: `Desired_speed_MPS` `0x014BB248` 18.0 · `Rage_Attack_Damage_HP` `0x014BB24C` 250.0 · `Rage_Increase_Rate` `0x014BB250` 1.0 · `Rage_Decrease_Rate` `0x014BB254` 1.0 · `Vehicle_Damage_Penalty_MS` `0x014BB25C` 4000 (`u32`) · `Vehicle_Damage_Threshold` `0x014BB260` 50.0 · `Vehicle_Damage_Cooldown_MS` `0x014BB264` 250 · `Human_Damage_Penalty_MS` `0x014BB268` 2000 · `Human_Damage_Threshold` `0x014BB26C` 10.0 · `Mover_Damage_Penalty_MS` `0x014BB270` 500 · `Mover_Mass_Threshold_KG` `0x014BB274` 75.0 · `Mover_Damage_Cooldown_MS` `0x014BB278` 250 · `World_Damage_Penalty_MS` `0x014BB27C` 500 · `World_Damage_Threshold_HP` `0x014BB280` 15.0 · `World_Damage_Cooldown_MS` `0x014BB284` 250. Values are stored raw (the `_MS` names are milliseconds, `_MPS` metres per second; no conversion in the loader).

## 20. `human_transition.xtbl` — human animation-state transition graph

Loader `0x009B8630` (whole function read). **Loaded only if the file parses.** It rebuilds three fixed-capacity name registries and two data arrays from scratch. **[CONFIRMED — disassembly for element names, the packed transition word and the capacities; OPEN — the runtime meaning of the states (they are keys into the animation system).]**

**Element tree.** `Table` → repeated **`Human_Transition`** (one per *state*) → `Name`; `start_state_grid` → repeated `start_state_elm` → `start_state`; `default_transition_from_stand` (bool); `Transitions` → repeated **`Transition`** → `End_State`, `Transition_Animation`, `Speed_Multiplier`, `Allow_Player`, `Allow_Player_Gang`, `Allow_Other_Gang`, `Allow_Police`, `Allow_Civilian`; `Blends` → repeated **`Blend`** → `End_State`, `Blend_Time`, `OneH_Blend_Time`, `TwoH_Blend_Time`.

**Two-pass structure.** Pass 1 registers each row's `Name` as a state (key: bucket hash `% 0x32`, so **at most 50 states**; the registry returns "already present/full" as `0xFFFF` and such a row still consumes an ordinal but no name slot), records the row's `start_state` animation-name indices into a 50-slot hash set, and for `default_transition_from_stand` registers the row ordinal in a second (`% 0x19`) set. Pass 2 walks the rows again: `End_State` texts must name a registered state; the pair *(start state ordinal, end state ordinal)* is interned in a **99-slot (`0x63`) pair registry** (`0x009B7AD0`, bucket `% 100`), and

- **transition record** — a packed dword stored at `0x0262DCB0 + 4·pairIndex`: bits 0–15 = `Transition_Animation` resolved to an **animation-name index** (§8; −1 truncates to `0xFFFF`), bits 16–23 = an 8-bit encoding of `Speed_Multiplier`, bits 24–31 = allow-flags. A `Transition` whose `Transition_Animation` is absent or whose `End_State` is unknown is skipped.
- **Speed_Multiplier encoding** (float `v`): `v ≤ 0.5` → 0; `0.5 < v ≤ 1.0` → `int((v − 0.5) × 255)` (0…127); `1.0 < v < 2.0` → `int(v × 0.5 × 255)` (127…254); `v ≥ 2.0` → 255. So 0.5→0, 1.0→127, 2.0→255. **[CONFIRMED — the branch structure and the constants 0.5, 1.0, 2.0, 255.0 read from the image.]**
- **allow-flags byte:** `Allow_Player` 0x01, `Allow_Player_Gang` 0x02, `Allow_Other_Gang` 0x04, `Allow_Police` 0x08, `Allow_Civilian` 0x10 (bool readers, §1.3).
- **blend record** — three floats `{Blend_Time, OneH_Blend_Time, TwoH_Blend_Time}` at `0x0262E168 + 0xC·pairIndex`.

## 21. `PEDF_Life.xtbl` and `Life_default.xtbl` — pedestrian "life" animation sets

Both literals (`PEDF_Life.xtbl`, `Life_default.xtbl`; the case differs from the task list) are referenced once each, from `0x008C9FD0` (a routine in the action-node module — an assertion-path string it embeds places it in the action-node code, by path only), and are parsed by the **same routine `0x008C7BC0(registry, allocator, filename, platform)`**; `Life_default.xtbl` is read first. **[CONFIRMED — disassembly of `0x008C7BC0`; HIGH CONFIDENCE for the role.]**

**Element tree.** `Table` → repeated **`Skeleton_Set`** → `Groups` → repeated **`Group`** → `States` → repeated **`State`** (`ID`, `Animation` → `Filename`), and `Actions` → repeated **`Action`** (`ID`, `Animation` → `Filename`). Each `State`/`Action` inserts one entry into a hash registry keyed by the **animation-name index of its `ID` text** (§8), with a 0x40-byte heap string built from the animation file name and the **platform-specific extension token chosen by the platform index (`anim_pc` for PC; `anim_ps2`/`anim_ps3`/`anim_xbox`/`anim_xbox2` are the others in the table)**. The consumer `0x008C9FD0` then resolves each action-node NPC's `Enter_anim`/`Exit_anim`/`Startle_anim`/`Animations`/`CowerAnims` names (§13.3) through that registry. The exact string built (`0x00DA8790`) and the meaning of `Skeleton_Set` names were not traced. **[OPEN.]**

## 22. `node_graph_files.xtbl` — the animation-network graph file list

The literal is referenced once, inside the very large start-up routine `0x005D25F0` — **which only copies the filename into a descriptor and does not read the file**. It builds a `0x1048`-byte descriptor of **16 slots of 0x100 bytes** (`+0x04 + n·0x100`) holding the animation-network table filenames, in this order of use: `anim_files`, `anim_states`, `anim_actions`, `anim_groups`, `anim_set_filenames`, `anim_triggers`, `anim_set_properties`, `anim_ik_situation`, `anim_prop_sets`, `anim_transitions`, `anim_blend_trees`, `control_parameters`, `control_filters` (slot 13, read by `0x004CAEE0`), **`node_graph_files` (slot 14)** and the base path `tables/anim/network/node_graphs/` (slot 15), plus five pointers at `+0x102C…+0x1040`; the descriptor is validated (every slot non-empty, every pointer non-null) and copied into a global block by `0x004C8780`. **[CONFIRMED — disassembly; the slot ordering for slots 13/14 is confirmed by their two readers; the other slots' order is HIGH CONFIDENCE.]**

The **reader of slot 14 is `0x004CB510`**. Like `control_filters.xtbl` and `action_nodes.xtbl` it opens the file with the raw-document entry `0x00DC5AC0` and takes its elements **directly from the document root, with no `Table` wrapper**: **`node_graph_files`** → repeated **`node_graph_file`** → `name` (text). Each `name` gets the extension **`.xtbl`** appended and names *another* XML file (state-machine / blend-tree definition) that is then opened; a graph is accepted only if its root has a **`state_machine`** child (preferred) or a **`blend_tree`** child, and that element's **`validated`** child reads true. Accepted graphs are keyed by the CRC-32 (seed 0) of their file name and instantiated as `0x30`-byte objects. The name buffer is sized for **384** graphs; no cap is enforced in the loop. **The per-graph format (`state_machine`/`blend_tree`) belongs to the animation-network subsystem and is not specified here.** **[CONFIRMED — disassembly of `0x004CB510`'s top level; OPEN — everything below the graph header.]**

## 23. Cross-table reference map

| From (table.element) | To | How the key is formed |
|---|---|---|
| `ambient_traffic_events.Roadblock_layout` (§4) | `roadblock_layouts.Name` (§5) | CRC-32 of the name vs. layout `+0x04` (last match wins) |
| `roadblock_notoriety.Roadblock` (§6) | `roadblock_layouts.Name` | same |
| `roadblock_stag_lockdown.Roadblock_Layout` (§7) | `roadblock_layouts.Name` | same |
| `roadblock_stag_lockdown.Spawn_Point` (§7) | a placed world object (by name) | object-registry lookup (`0x02442750`) |
| `roadblock_layouts.Object[Type=Vehicle].Resource_name` (§5) | vehicle table (`spec-vehicle-data.md` §7.1) | name CRC → slot; `Variant_Name` → variant index |
| `roadblock_layouts.Object[Type=Action Node].Resource_name` | `action_node_groups.Name` (§13.1) | CRC → group index (+1) |
| `roadblock_layouts.Object[Type=NPC].Resource_name` | character-definition registry | CRC → bucket (500) |
| `roadblock_layouts.Object[Type=Vehicle Group]` | team registry | name → team id |
| `roadblock_notoriety.Team`, `ai_behavior.human_elm.Team` | team registry | multiply-33 bucket hash `% 0x20`, then key compare |
| `ai_goals.action`, `ai_behavior.ability_elm.Action` | the combat-action name table (§1.5; `combat_actions.xtbl`, AH) | case-insensitive name compare |
| `ai_behavior.human_elm.Weapon_Class` | the 23 weapon-class names (§1.5) | case-insensitive |
| `panic_reactions.*_Animations.*`, `action_node_npcs.*anim*`, `human_transition.Transition_Animation` | the animation-name table (`0x03171C10`) | CRC of the lower-cased name, then string compare; −1 if absent |
| `panic_reactions.Persona_Situation`, `follower_heads.Persona`, `homies.Audio`, `action_node_groups.Music_Emitter`, `action_node_npcs.Persona_situation` | Wwise event ids | `AK::SoundEngine::GetIDFromString` |
| `generic_vehicles.Spawn_Name`, `homies.Vehicle` | vehicle table | name CRC → slot (`0x00AC2560`) |
| `generic_characters.Spawn_Name`, `homies.Character`, `action_node_npcs.Model`, `follower_heads.Charname` | character-definition registry | CRC (`Charname`: stored as the CRC itself) |
| `action_node_groups.npc_list` (§13.1) | `action_node_npcs.Name` (§13.3) | CRC match |
| `action_node_groups.Notoriety_Info` | `action_node_notoriety.Name` (§13.2) | CRC match |
| `action_node_notoriety.Weapon` | the weapon table (`0x0250ADC0`) | text vs. ASCII or wide name |
| `distant_vehicles.Traffic_Type` (§14.6) | `distant_vehicle_traffic_types.Name` (§14.4) | CRC |
| `distant_vehicles.Mesh` | the item/mesh registry (`0x025F5B98`) | CRC |
| `homies.Blocked_for_Mission` (§15) | the 33 mission names (`0x012FF868`) | case-insensitive string |
| `Life_default`/`PEDF_Life` `State.ID` (§21) | the animation-name table | as above |

**Hash-key convention summary** — CRC-32 reflected `0xEDB88320`, seed 0, no final XOR: **lower-cased** everywhere except `homies.Name` (§15); chained-seed form for the roadblock resource interner (§5).

## 24. Validation

**Ground truth available.** Of the 32 tables in this group, **only `homies.xtbl` has raw-readable samples** (`dlc1/2/3_homies.xtbl`, extracted by `tools/harnesses/tbl_dlc_list.py` into `tools/ah_dlc_xtbl/`; the three archives are stored raw, so no mode-(a) question arises). ~~Every other table of the group is base-game only and lives in a mode-(a) container — it was neither read nor decoded.~~ **SUPERSEDED 2026-09-23** — the mode-(a) container is now readable; 30 of the other 31 group tables were located and decoded from real base-game archives (`vehicle_despawn_mp.xtbl` was not found in any scanned archive), see §26. *(Count corrected 2026-09-30: §26.0 reports 32/32 found; `vehicle_despawn_mp.xtbl` is not one of the 32 tables counted in §1.1, so all 31 other group tables were located.)* The validation below is therefore complete for §15 and **structural / internal-consistency only** for the rest (§24.3).

### 24.1 The three DLC homies tables against §15 **[CONFIRMED — empirical]**

Harness `tools/harnesses/aj_homies_validate.py` (predicates stated in the script before it was run).

| Predicate | Result |
|---|---|
| every child element of every `Homie` row ∈ the reader's vocabulary (the ten read names plus the never-read `_Editor`) | **10 / 10 rows** |
| `Name` ≤ 30 characters (the `char[0x1F]` field) | 10 / 10 |
| `Humans/Human` count ≤ 8 (the pointer array `+0x34…+0x53`) | 10 / 10 (1 in seven rows, 2 in three) |
| `Vehicle_Entry` count ≤ 2 (the pair array `+0x58…+0x67`, §15) | 10 / 10 (**exactly 1 in every sampled row** — the two-entry ceiling is never approached) |
| `Vehicle_Entry` children ⊆ {`Vehicle`, `Weight`, `Vehicle_Variant`}; `Weight` a plain integer | 10 / 10; 10 / 10 |
| every `Flag` text ∈ the ten strings the reader tests | **2 / 2** (`requires saints`, `turn to ped after driveup`, both on one row) |
| `Framework` present and equal to the archive's tag (`dlc1`/`dlc2`/`dlc3`); `Is_DLC` ∈ {true, yes} | 10 / 10; 10 / 10 |
| case-sensitive name CRCs (§15, `+0x20`) pairwise distinct; total DLC rows (10) ≤ 16 | pass; pass |
| **84 predicate instances, 0 failures** | |

Rows present: `cheapyd`, `angry_tiger`, `sexy_kitten`, `sad_panda`, `tami`, `yarnball` (dlc1, 6 rows); `kwilanna`, `space_brutina` (dlc2, 2 rows); `aisha_brutella`, `johnny_tag` (dlc3, 2 rows) — 10 rows, of which `yarnball` carries the only two `Flag` elements. One of the rows (`space_brutina`) uses the character `npc_AlienBrutella`, i.e. **the hard-coded special case of §15 fires on shipped content** (its file-declared vehicle is replaced at load by `truck_4dr_pickup07`/`Saints`).

**The editor description block embedded in each DLC file corroborates the loader independently** (it is data written by the authoring tool, not read by the engine): its element list — `Name`, `Display_Name`, `Display_Desc`, `Humans` (grid of `Character`), `Vehicles` (grid of `Vehicle`, `Vehicle_Variant`, `Weight` default 1), `Audio`, `Flags`, `Image_name`, `Blocked_for_Mission`, `Is_DLC` (default `False`), `Framework` — is **exactly the eleven names the reader consumes (11 / 11)**. Its `Blocked_for_Mission` value list has **33 mission names and equals, as a set, the 33-name array in the exe at `0x012FF868` (3 / 3 files)** *(orchestrator re-derived 2026-09-20 from the three raw DLC files and the exe: same 33 names, **but in a different ORDER** — the editor's list has `mm_m16_5` last, the exe array has it right after `m16` — so the runtime bit/index for a blocked mission is the position in the EXE array, never the editor's list order; the `Flags` list is exactly the ten tested strings plus `is wheelman` in 3/3 files; the ten DLC `<Homie>` rows contain no element outside the loader's list except the editor-only `_Editor`/`Category`; every `Flag` text in a data row is one of the ten tested strings.)* — a check the loader cannot have influenced. Its `Flags` value list has **eleven** entries; the reader tests **ten** of them — **`is wheelman` is declared to the editor but never read by the engine** (the reader was read exhaustively; §15).

### 24.2 Internal consistency checks against the exe image **[CONFIRMED — disassembly]**

Capacity claims that the loaders do not bound-check were cross-checked against the *adjacency* of the runtime arrays, which the compiler lays out contiguously: the roadblock layouts / objects / resources arrays end exactly where the next begins (`0x0261F588 = 0x02617E00 + 850 × 0x24`, `0x02617E00 = 0x026172C0 + 120 × 0x18`), the base and DLC homie arrays (`0x022AD780 + 28 × 0x180 = 0x022B0180`), the `action_node_notoriety` single record and the start of the group array (`0x024ED74C + 0x14 = 0x024ED760`), the roadblock-notoriety block (`25 × 0x44` ends at `0x014A575C`), and the two vehicle-despawn arrays (`0x0260931C − 0x026092C8 = 0x54 = 10 × 8 + 4`). In each case the loader's explicit cap (where it has one) equals the array size, which supports the inferred capacity where it has none (§2 traffic lanes, §8 panic reactions are the two where **no** cap is checked and the arithmetic is the only evidence).

Two loader properties were verified by the DLC samples' *shape* rather than by values: (i) rows are matched by element name **case-insensitively** (the DLC `Vehicle_Entry`/`Weight` spellings match either way); (ii) the `Table` wrapper — every raw DLC file has `<root><Table>` (`spec-xtbl-format.md` §2).

### 24.3 What is *not* validated

⚠ **SUPERSEDED 2026-09-23 by §26** (real base tables, 32/32 found and decoded). The paragraph below describes the DLC-only state of §24.

No raw sample exists for `traffic_lanes`, `ambient_traffic_events`, `roadblock_*`, `panic_reactions`, `ai_*`, `generic_*`, `action_node*`, `distant_*`, `follower_heads`, `driver_bailout`, `vehicle_despawn`, `Escort_constants`, `human_transition`, `PEDF_Life`/`Life_default`, `node_graph_files`. Their schemas rest on disassembly alone. The claims are of the form "the reader asks for element X, converts it thus, and stores it there"; whether a shipped base table *contains* an element the reader does not ask for, or omits one the reader treats as required, cannot be checked here. Every range check quoted (e.g. `Min_life ≤ 240 hours`, `Cooldown ≤ 600 s`, `Num_lanes ∈ 1…4`) is a **rejection rule of the loader**, i.e. a bound the shipped data must satisfy — but the *values* in the shipped data are unknown.

## 25. Running open-items list

1. **`traffic_types.xtbl` has no loader** (§3); the only "traffic type" definition site is `distant_vehicle_traffic_types.xtbl` (§14.4). ~~If a base table of that name exists it is not read by name.~~ **CONFIRMED 2026-09-23** — a base table of that name DOES exist (`misc_tables.vpp_pc` entry 256) and is still not read by name (re-checked with the file in hand); see §26.
2. **Team registry** — the id↔name binding behind `roadblock_notoriety.Team` (only ids 1, 2, 3, 5, 6 accepted), `ai_behavior.human_elm.Team` and `Vehicle Group` (`0x0094CC60`, `0x005961A0`) was not recovered (**real name strings now known, §26.3**: `roadblock_notoriety` uses `Deckers`/`Morningstar`/`Police`/`Luchadores`/`STAG`; `ai_behavior.human_elm.Team` additionally uses `Neutral Gang`/`Playas`/`Civilian` — 8 distinct strings from real base data, the id↔name mapping itself still OPEN); and the **rank/character-descriptor table at `0x029A91A0`** (stride `0x698`) used by `ai_behavior.human_elm.Rank` — which loader fills it was not traced.
3. **§7** — the owner of the world-object registry at `0x02442750` and the meaning of object flags `+0x33`/`+0x34` (object data comes from zone data, whose interior is the parked area and was not read); which subsystem consumes the roadblock arrays.
4. **§8** — the real capacity of the appended panic-reaction rows (no check in the loader) and the identity of the three callbacks written into slots 0–2; the goal table's words `+0x04…+0x07`.
5. **§13.4** — `action_nodes.xtbl`'s packed record layout and consumers. ~~the `Spawn_Timer` post-processing (`0x00D9E140`).~~ **RESOLVED, 2026-09-30** — corrected attribution too: `Spawn_Timer` is a §13.1 field (`action_node_groups.xtbl`), not §13.4's. `0x00D9E140` was independently decompiled by an unrelated pass documenting the Lua API (`spec-lua-api-behaviour.md` §1.2/§11.3): it adds its integer argument to a fixed global base, wraps the sum into a fixed range (a ±1,800,000,000 correction), and writes the result through a hidden pointer argument — a deadline/timestamp computation, not a per-record post-processing step. This is consistent with §26's own observation at the `action_node_groups.xtbl` call site (arg `1`, return value discarded, no visible effect on the record) — the hidden-pointer destination there is a separate global, not this record, which is exactly why no effect on the record was ever observed. **[CONFIRMED — disassembly, `0x00D9E140`'s own body, cross-project.]**
6. **§21** — the string built by `0x00DA8790` for each life `State`/`Action`, and what `Skeleton_Set` names select.
7. **§22** — how the slot-15 base path is applied; slot order of the first thirteen descriptor names; the `state_machine`/`blend_tree` graph format (a separate subsystem).
8. **§18** — which mode selects `vehicle_despawn_mp.xtbl`; who reads `safety_despawn_time`; the nested `Table/Table` shape can only be confirmed with a sample. *(A real sample now exists, §26.1/§26.3, and `safety_despawn_time` is absent from it; whether the nesting was checked is not reported there.)*
9. **§15** — provenance of the per-row blocked-mission list buffer (`+0xF4`, capacity `+0xF8`); consumers of the `+0xF1`/`+0xF2` flag bits.
10. **§20** — runtime meaning of the human-state names; the exact capacity behaviour when a 51st state or 100th pair is offered.
11. **§2** — ~~consumers and units of the `LaneSpeed` array; the implied capacity of 754 is arithmetic only.~~ **PARTIALLY RESOLVED 2026-09-23** — the real base file's own `TableDescription` labels `Speed`'s `Display_Name` as "Lane Speed (m/s)", i.e. the unit is almost certainly **m/s** (editor metadata, not a runtime trace — HIGH CONFIDENCE, not CONFIRMED); the base file has only 3 real `LaneSpeed` rows against the 754 arithmetic ceiling. The true runtime consumer is still OPEN. See §26.2/§26.4.
12. **§10** — the full schema of `combat_actions.xtbl` is AH's (`spec-tables-weapons-combat.md`); this document lists only the fields the AI loaders touch.
13. ~~**All base-table values** remain unreadable (mode-(a), parked). Every schema here decodes a base table directly the moment one is available; none of the rules above depends on it.~~ **→ RESOLVED 2026-09-23, §26** (base tables readable; 32/32 found and decoded).
14. **Audio middleware:** the third-party Wwise engine is used for `Persona_Situation` / `Audio` / `Music_Emitter` event ids; those hashes are the middleware's (public) string-to-id function and were not re-derived.

### 25.1 Artifacts

Ghidra post-scripts: `tools/scripts/AjBoth.java` (decompile + annotated disassembly of a call tree), `AjAsmFn.java`, `AjDecTree.java`, `AjMkFn.java` (creates a function in the disposable project copy), plus agent AF's `AfStrXrefs.java`, `AfDec.java`, `AfAsm.java`, `AfRangeRefs.java`. Wrapper `tools/run_tbl3.ps1` (project copy `tools/gp_tbl3`). Harnesses: `tools/harnesses/aj_va.py` (constants / strings / pointer tables from the exe by virtual address), `aj_homies_validate.py`. Dumps: `tools/aj_fn_xrefs*.txt` (filename xrefs), `aj_d*_*.txt`, `aj_a*.txt` (loaders and readers), `aj_dp_*`, `aj_dv_*` (distant), `aj_hm_*` (homies), `aj_an*_*`, `aj_ng*_*` (action / anim network), `aj_ht*_*` (human transition), `aj_db_*`, `aj_fh_*`, `aj_esc_*`, `aj_life*_*`, `aj_vd_*`. The parked mode-(a) container question and the `.czn_pc` interior were not touched.

## 26. Validation against real base-game tables (2026-09-23, orchestrator resume — agent AJ2)

**Scope of this pass:** the container limitation that made §24 "structural / internal-consistency only" is now resolved (`spec-vpp-container.md` §7/§8). Every table this document specced was located and extracted from the real base-game archives and checked against the loader-derived schemas above. **[CONFIRMED — empirical for every fact in this section unless marked otherwise; `tools/harnesses/vpp_modea.py` + a local tolerant XML reader, `spec-xtbl-format.md` §7's rules applied throughout — no strict-parser rejection was treated as extraction failure.]**

### 26.0 Method and extraction result

`misc_tables.vpp_pc` (1,342 entries), `da_tables.vpp_pc` (110), `patch_compressed.vpp_pc` (126) and `cutscene_tables.vpp_pc` (118) were opened with `scan_meshes.parse_entries` + `vpp_modea.modea_positions`/`get_entry_bytes_modea`. **All 32 tables this document covers were searched for by exact case-insensitive filename; 32/32 were found and decoded**, every one in `misc_tables.vpp_pc` (`roadblock_stag_lockdown.xtbl`, `follower_heads.xtbl`, `driver_bailout.xtbl`, `vehicle_despawn.xtbl` and `escort_constants.xtbl` are additionally byte-identical in `da_tables.vpp_pc`/`patch_compressed.vpp_pc`), including **`traffic_types.xtbl` itself** (§3) and (separately) the misspelled-row `roadblock_stag_lockdown.xtbl`. `vehicle_despawn_mp.xtbl` (§18) was searched for in all four archives and **not found anywhere** — a new, real negative for that OPEN item (the base game may simply not ship it, or it lives under a fifth archive not scanned here). Every extracted entry's inflated length matched its directory `sz_uncomp` exactly (32/32) — a working correctness check on the mode-(a) reader itself, independent of the tolerant-parser step. Raw files kept in the agent's scratchpad for this session (`tools/harnesses/prog_common.py`'s `parse_xtbl`/`El` reused read-only, no edits to that file).

Two loader functions were re-decompiled from a fresh disposable Ghidra copy (`tools/gp_aj_val1`) to resolve real coverage questions raised by the data (§26.4): `0x00507AC0` (`ai_personalities.xtbl`) and `0x008C9760` (`action_node_groups.xtbl`), both read in full again.

### 26.1 Coverage: every real element name vs. the documented schema, table by table

Legend: ✅ = every element name in the real file is in this document's schema (aside from the universal editor scaffolding `_Editor`/`Category`/`TableTemplates`/`TableDescription`/`EntryCategories` and its own descendants, which no table's loader reads and which this document correctly never lists). ⚠ = real, game-authored elements exist that this document's schema does not name.

| Table | Result | Undocumented elements found |
|---|---|---|
| `traffic_lanes.xtbl` | ⚠ | `UID`, `Description` per `LaneSpeed` row (not read by the loader — §2's "no other element name is passed to any accessor" still holds; these are dead-to-the-loader authoring metadata) |
| `traffic_types.xtbl` | ⚠ (moot — unloaded) | row element is `TrafficTypes` (not documented since no loader exists); single child `Name` |
| `ambient_traffic_events.xtbl` | ✅ | none |
| `roadblock_layouts.xtbl` | ✅ | none (`Variant_name` case differs from the documented `Variant_Name` — case-insensitive, no effect) |
| `roadblock_notoriety.xtbl` | ✅ | none |
| `roadblock_stag_lockdown.xtbl` | ✅ | none — confirms the misspelled row tag `Roadbock` is what the real file actually uses, 15/15 rows |
| `panic_reactions.xtbl` | ✅ | none |
| `ai_goals.xtbl` | ✅ | none |
| `ai_behavior.xtbl` | ✅ | none |
| `ai_personalities.xtbl` | ⚠ (confirmed dead, §26.4) | a full **parallel top-level copy** of `AttackNonPlayer`, `VehicleCollisionResponse`, `VehicleWaitResponse`, `MeleeReaction`, `Flee_health_pct`, `HijackBehavior` (with extra sub-fields `PlayerRank`, `StopChance`, `JackSuccessChance`, `PostJackFleeChance`) sitting alongside the documented `Ambient_tweaks`-nested set — re-decompiled, confirmed **never read** |
| `generic_characters.xtbl` | ✅ | none (but see §26.4 — one row's `Name` matches no compiled slot) |
| `generic_vehicles.xtbl` | ✅ | none |
| `action_node_groups.xtbl` | ⚠ (confirmed dead, §26.4) | `Trigger_distance` (153/163 rows), `Time_of_day_start`/`Time_of_day_end` (37/163 each) — re-decompiled, confirmed **never read** |
| `action_node_notoriety.xtbl` | ✅ | none |
| `action_node_npcs.xtbl` | ✅ | none |
| `action_nodes.xtbl` (placement file) | ⚠ (structural — §13.4 already OPEN) | per-`object`: `associated_max_file`, `action_node_bounds` → `box_length`/`box_width`; per-`spawn_node`: `source_file`, `source_prop` (spec only names `group`/`name`/`transform` for `spawn_node`). Zero `vehicle` children and zero `num_vehicles` occur anywhere in this sample, so that part of the documented shape is simply unexercised, not contradicted |
| `distant_peds.xtbl` | ✅ | none |
| `distant_ped_colors.xtbl` | ✅ | none |
| `distant_ped_spawn_parameters.xtbl` | ✅ | none (real element is spelled `Length_Per_spawn`, lower-case `s` — case differs from the documented `Length_Per_Spawn`, no effect) |
| `distant_vehicles.xtbl` | ✅ | none |
| `distant_vehicle_colors.xtbl` | ✅ | none |
| `distant_vehicle_spawn_parameters.xtbl` | ✅ | none |
| `distant_vehicle_traffic_types.xtbl` | ✅ | none |
| `homies.xtbl` | ✅ | none — and see §26.5: `Framework`/`Is_DLC` are simply absent from every base row rather than present-and-default |
| `follower_heads.xtbl` | ✅ | none, but `Charname` is present in the schema and **never once populated** (§26.4) |
| `driver_bailout.xtbl` | ✅ | none |
| `vehicle_despawn.xtbl` | ✅ | none (`safety_despawn_time` confirmed absent from the real file too, consistent with it not being read) |
| `escort_constants.xtbl` | ✅ | none |
| `human_transition.xtbl` | ✅ | none |
| `pedf_life.xtbl` / `life_default.xtbl` | ⚠ (structural — §21 already OPEN) | `Parent_Skeleton` under `Skeleton_Set` (real values: `PEDM` for `PEDF_Life`, absent for `Life_default`); a lower-case `group` text child of `Group` (real value `Default` in both files) |
| `node_graph_files.xtbl` | ✅ | none — confirmed no-`Table`-wrapper shape, root → `node_graph_files` → repeated `node_graph_file` → `name` |

**Net result: 0 of 32 tables contradict a disassembly-derived reader claim.** Every element the loaders were documented to read is genuinely present with the documented name; every extra element found (ai_personalities' duplicate block, action_node_groups' three fields, follower_heads' `Charname`) was checked and is either re-confirmed unread by a fresh decompile or, for `Charname`, simply never written by any real row.

### 26.2 Row-count / capacity checks (real base count vs. this document's claimed bound)

| Table | Claimed capacity | Real base row count | Margin |
|---|---|---|---|
| `traffic_lanes.xtbl` `LaneSpeed` | 754 (arithmetic, unexercised) | **3** | far under |
| `ambient_traffic_events.xtbl` | 16 (checked) | **15** | 1 free |
| `roadblock_layouts.xtbl` layouts | 66 (checked) | **65** | 1 free |
| `roadblock_layouts.xtbl` objects | 850 (checked) | **629** | comfortable |
| `roadblock_layouts.xtbl` resources | 120 (checked) | **76 distinct** `(Type,Resource_name,Variant_Name)` keys | comfortable |
| `roadblock_notoriety.xtbl` rows (team slots) | 5×5 = 25 record grid | **5** `Roadblock_notoriety` rows, **20** `Level` children (see §26.3 — level 1 never used) | 5 team slots exactly full, 20/25 level cells |
| `roadblock_notoriety.xtbl` layouts per level | 4 (checked) | **44 total / 20 levels ≈ 2.2 avg**, no per-level breakdown taken | under |
| `roadblock_stag_lockdown.xtbl` | 16 (checked) | **15** | 1 free |
| `panic_reactions.xtbl` | 6 reserved + uncapped appended | **exactly 6**, all matching the built-ins (§26.3) | **0 appended rows in the base game** |
| `ai_goals.xtbl` | 17 fixed goal names | **17/17**, exact set match (§26.3) | full, exact |
| `ai_behavior.xtbl` `Behavior` | 44 (checked, `0x2C`) | **44** | **exactly at capacity — see §26.5** |
| `ai_behavior.xtbl` `human_elm` sub-table | 140 (checked) | not counted exactly; 159 `human_elm` occurrences total across all rows in the raw dump, consistent with being under 140 per the loader's own running total (not independently re-derived here) | plausible |
| `ai_personalities.xtbl` `Personality` | 20 (checked) | **17** | 3 free |
| `generic_characters.xtbl` slots | 21 fixed names | **22 rows**, 21 match a slot + **1 dead row** (§26.4) | over by exactly the dead row |
| `generic_vehicles.xtbl` slots | 20 fixed names | **20/20**, exact set match | full, exact |
| `action_node_groups.xtbl` | 200 (checked) | **163** | comfortable |
| `action_node_notoriety.xtbl` | "only first row stored" (loader quirk) | **1** row in the file itself — the quirk is real but not exercised by base data | moot |
| `action_node_npcs.xtbl` | 150 (checked) | **129** | comfortable |
| `distant_peds.xtbl` | 3 (checked) | **1** (`Name=Test`, §26.5) | 2 free |
| `distant_ped_colors.xtbl` skin/hair/clothing pools | 6 / 8 / 32 (checked) | **3 / 8 / 8** | hair pool exactly full |
| `distant_vehicles.xtbl` | heap, no cap | **2** rows | n/a |
| `distant_vehicle_colors.xtbl` | heap, no cap | **11** rows | n/a |
| `distant_vehicle_traffic_types.xtbl` | heap, no cap | **1** row (`Ambient`) | n/a |
| `homies.xtbl` base array | 28 (checked) | **24** | 4 free |
| `follower_heads.xtbl` | heap, no cap | **78** rows | n/a |
| `node_graph_files.xtbl` | 384 (checked) | **249** | comfortable |

**Nothing in the base game exceeds a documented bound.** Several arrays are within one row of their checked capacity (`ambient_traffic_events`, `roadblock_layouts` layouts, `roadblock_stag_lockdown`), and `ai_behavior.xtbl` is exactly saturated (§26.5) — none of this could have been seen from the DLC-only sample of §24, which contained none of these tables at all.

### 26.3 Real content sampled (`ai_behavior`/`ai_goals`/`ai_personalities`, `panic_reactions`, roadblock team names, distant tables)

- **`panic_reactions.xtbl`**: the 6 real rows are, in file order, `On Fire`, `Pepper Spray`, `Flashbang`, `Sticky Projectile`, `Luch Grenade`, `Fart In a Jar` — the same six built-ins §1.5 lists from the image array (`0x012E1B68`), confirming the fixed-slot mechanism is exercised exactly as documented, with **zero custom (appended, slot ≥ 6) reactions in the base game**. Casing differs on one: the file has `Fart In a Jar` (lower-case `a`) vs. the image string `Fart In A Jar` — harmless under the case-insensitive CRC, but added here alongside this document's other case-difference notes (§1.1).
- **`ai_goals.xtbl`**: all 17 real `Name` values are an exact set match to the 17 compiled goal names (§1.5) — 17/17, no extra, no missing. Per-goal action-list lengths range 1 (`taking_damage`) to **24** (`kill enemy` — exactly at the "more than 24 sets the error flag" ceiling, not over it).
- **`ai_behavior.xtbl`**: all `Action` texts (1,042 `ability_elm` occurrences) and all `Weapon_Class` texts (159 `human_elm` occurrences) matched the compiled 70-action / 23-weapon-class vocabularies with **zero unrecognized strings** — full vocabulary coverage. Real `Team` texts used: `Neutral Gang`, `Deckers`, `Police`, `STAG`, `Playas`, `Civilian`, `Luchadores`, `Morningstar` (8 distinct). Real `Rank` texts (19 distinct, e.g. `Homie - Oleg`, `Riot Shield`, `RollerBlader_Cyberspace`, `Decker_CyberSpace`) feed the still-untraced rank table at `0x029A91A0` (§25 item 2, unchanged).
- **`roadblock_notoriety.xtbl`**: the 5 real `Team` values are `Deckers`, `Morningstar`, `Police`, `Luchadores`, `STAG` — one per row, matching the "five factions" hypothesis (§6) exactly. **Every one of the 5 rows defines exactly 4 `Level` children, for notoriety levels 2–5 — level 1 is never configured for any faction** (0 of 25 grid cells at level 1 are populated; the other 20 are).
- **`roadblock_layouts.xtbl`**: `Num_lanes` is **only ever 2 or 4** across all 65 real layouts (33 and 32 respectively) — 1 and 3 are never used. The `Flags`/`Flag` texts used are only `No Flee` (3) and `Can Spawn on Disabled Lanes` (2); **`end cap` and `intersection` are never set by any real layout** (so the "`end cap` ⇒ `Num_lanes` must be 2" rejection rule is real but vacuous against base data). `Object`/`Type` histogram: `Vehicle` 149, `NPC` 228, `Item` 196, `Action Node` 29, `Vehicle Group` 27 (629 total, matching the object count exactly — every object's `Type` text is one of the 5 known names, 0 unrecognized). The 18 exponent-form floats `spec-xtbl-format.md` §8 found in this file are all near-zero `Heading` values (e.g. `-2.98023223877e-08`) plus one `X_offset` (`3.09944152832e-06`) — floating-point noise from the level editor around 0, not meaningful non-zero headings.
- **`generic_characters.xtbl`** / **`generic_vehicles.xtbl`**: the exe's literal 21-name (`0x01308CD0`) and 20-name (`0x01308D28`) arrays were read directly (`tools/harnesses/aj_va.py`). `generic_vehicles.xtbl`'s 20 real rows are an **exact set match** to the 20 compiled slot names. `generic_characters.xtbl`'s 22 real rows cover all 21 compiled slots **plus one, `Police FBI` (`Spawn_Name=npc_cop`), that matches none of the 21** — a real, base-game instance of the documented "row does nothing for a `Name` outside the list" dead-row behaviour (§26.4).
- **`escort_constants.xtbl`**: every leaf under `Rage`/`Penalties/{Humans,Vehicles,Movers,World}` is present with a real value (e.g. `Desired_Speed_MPS=15.0` vs. the documented image default 18.0; `Human_Damage_Threshold=10` matches its own default exactly; `Mover_Damage_Cooldown_MS=150` vs. default 250) — full coverage, no group absent, so the "defaults survive only when the whole group is absent" rule is not exercised by base data either.
- **`driver_bailout.xtbl`**: single row, all 15 documented leaves present with real values (`Min_Distance=5`, `Max_Distance=35`, `Min_Damage=50`, `Max_Damage=2000`, `Min_Velocity=0.7`, `End_Time=2.0`, `Max_Respect=100`, `Max_Lifetime_Respect=5000`, `Max_Cash=100`, `Vehicle_Damage_Multiplier=7`, …) — full coverage.
- **`vehicle_despawn.xtbl`**: real `in_view_despawn_info` has 4 `abandon_despawn_info` entries (`{5 cars,30s}…{8 cars,0s}`), `out_of_view_despawn_info` has 3 (`{5,10s}…{7,0s}`) — both far under the "10 fit" arithmetic bound; `abandon_despawn_delay=10`, `player_vehicle_despawn_distance=10000`; `safety_despawn_time` is genuinely absent from the shipped file.
- **Distant tables**: `distant_vehicles.xtbl` has exactly 2 rows (`Car`→`car_medium`, `Truck`→`truck_medium`), both `Traffic_Type=Ambient`, matching the single real `distant_vehicle_traffic_types.xtbl` row (`Ambient`: `Spline_Type=10`→mask `1<<10`, `Length_Per_Spawn=100`→stored `0.01`, `No_Spawn_Zone=10`, `Dummy_Car_Length=4`). `distant_peds.xtbl` has exactly 1 row, `Name=Test`, `Mesh=distant_ped` (§26.5). `distant_ped_colors.xtbl`'s one `Color_Set` ("Base") supplies 3 skin colours, a full 8 hair colours, and (per the recursive element count) 8 of 32 clothing sets.

### 26.4 Corrections and resolutions (decompile-verified)

- **`ai_personalities.xtbl` (§11) — schema re-confirmed exhaustive, not corrected.** The real file's `Personality` rows carry a full second, parallel copy of `AttackNonPlayer`/`VehicleCollisionResponse`/`VehicleWaitResponse`/`MeleeReaction`/`Flee_health_pct`/`HijackBehavior` at the top level (with extra fields `PlayerRank`, `StopChance`, `JackSuccessChance`, `PostJackFleeChance` not present in the nested, documented copy). Loader `0x00507AC0` was re-decompiled in full: it reads `Name`, `combat_aggression`, `Flee_tweaks/Flee_health_pct`, `Flee_tweaks/flee_events/Flag`, and `Ambient_tweaks/{attack_unfriendly, Taunt_reaction, MeleeReaction, VehicleWaitResponse, VehicleCollisionResponse, HijackBehavior}` **and nothing else** — every offset in §11's table matches the loader's own field-store instructions exactly. The top-level duplicate block is real, shipped, and **entirely dead to this loader** — most plausibly a legacy/flat authoring layout that a data-pipeline step nested into `Ambient_tweaks` (or vice versa) without ever deleting the original. `combat_aggression` is stored as descriptive text (e.g. `"1- average"`), confirming the "first character minus `'0'`" parse rule operates on real, non-bare-digit content.
- **`action_node_groups.xtbl` (§13.1) — three real elements confirmed dead.** `Trigger_distance` (153/163 rows), `Time_of_day_start`/`Time_of_day_end` (37/163 each) are genuine, common, real-game elements with no accessor call anywhere in the re-decompiled loader `0x008C9760` (1,332-byte body, read in full a second time). They join `Reference_object`'s sibling data as further evidence of an editor schema wider than the runtime reader. Also newly visible in this decompile: the unconditional call `FUN_00d9e140(1)` immediately after the `Spawn_Timer` clamp — a literal-argument call whose return value is discarded and not stored into the record, i.e. **not** part of `Spawn_Timer`'s own post-processing. **§25 item 5 resolved, 2026-09-30:** `FUN_00d9e140` is a deadline/timestamp computation (argument plus a fixed global base, wrapped into a fixed range, written through a hidden pointer argument) — the hidden pointer's destination is a separate global, not this record, which is exactly why the call has no visible effect here.
- **`generic_characters.xtbl` (§12) — the documented dead-row rule fires on real data.** `Police FBI` is a real row whose `Name` CRC matches none of the 21 compiled slot CRCs (verified against the literal array at `0x01308CD0`); per §12 it silently does nothing, so its `Spawn_Name` (`npc_cop`, already covered by the `Police Officer` and `Police Swat` rows) is simply discarded at load. This is the first observed real-game instance of that rule; the DLC-only pass of §24 could not have produced it (no DLC touches this table).
- **`follower_heads.xtbl` (§16) — `Charname` unused in the base game.** 0 of 78 real `Follower` rows populate `Charname` (only `Persona`/`Bitmap` are ever present); the documented cross-reference `Charname → character-definition registry` (§23) is schema-real but empirically dead in every shipped base row. `Bitmap` is present in 78/78 rows, consistent with the "absent `Bitmap` dereferences null" reading (never tested by real data, but never contradicted either).
- **§3 `traffic_types.xtbl`, §25 item 1, §25 item 2, §25 item 11, §24 — struck through in place above** with pointers into this section (see the diffs at each citation).

### 26.5 What the DLC-only validation (§24) could not have caught

- **Base `homies.xtbl` rows never specify `Framework` or `Is_DLC`** (0/24) — the loader's documented defaults (`main`, not-DLC) are exercised by every single base row, the opposite of every DLC row in §24.1's sample (10/10 always explicit). This asymmetry between base and DLC authoring style is invisible from DLC data alone.
- **`ai_behavior.xtbl` is exactly at its documented 44-row capacity in the base game already** — any future patch, mod or DLC `Behavior` row added through the same loader path would be the 45th and would trip the error flag and stop loading (§10). The DLC sample had zero coverage of this table.
- **`distant_peds.xtbl`'s only real row is literally named `Test`** (`Mesh=distant_ped`), suggesting this whole distant-pedestrian sub-system may ship in a vestigial/placeholder state in the retail build — a content observation no amount of loader disassembly or DLC sampling could reach.
- **Several arrays sit one row under their checked capacity** (`ambient_traffic_events` 15/16, `roadblock_layouts` 65/66, `roadblock_stag_lockdown` 15/16) purely from base content; §24's DLC sample contained none of these tables, so this is the first real test of any of these bounds.
- **`roadblock_notoriety`'s 5×5 grid is real-content-sparse in a structured way**: every one of the 5 teams skips notoriety level 1 and defines only levels 2–5 — a uniform authoring pattern, not something a schema-only pass could predict.
- **`generic_characters.xtbl`'s dead-row rule and `ai_personalities.xtbl`'s dead top-level block** (§26.4) are both real, shipped instances of "the schema is wider than what the loader reads" — the DLC sample (10 `homies.xtbl` rows) never touched either table.

*(§25, this document's own running open-items list, is unchanged in numbering; the items resolved or advanced above are struck through in place at their original citations there, not renumbered.)*

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked §24.3, the scope header and §25 item 13 superseded/resolved by §26; added pointers from §2 (unit, §25 item 11) and §25 item 8 (real sample exists); corrected the 30-of-31 table count against §26.0 (32/32); fixed 3 cross-references (§26.4→§26.3, §1→§2, roadmap now lists §26).
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline; repointed 1 `HANDOFF.md` §27.x references to the archived headings (§27.2 → §30, header); 0 left (see review).
