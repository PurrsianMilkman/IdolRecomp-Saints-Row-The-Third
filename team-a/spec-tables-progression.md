# Saints Row: The Third — Progression, Rules and World-State `.xtbl` Tables: Schemas Recovered from the Loaders

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** Schema-from-loader campaign (dispatched 2026-09-20; agent AI, group "progression, rules and world-state tables"; Ghidra project copy `tools/gp_tbl2`).
**Scope:** The XML element tree, element types, runtime destinations, defaults, unit conversions, name-hash keys, cross-table references and fixed capacities of the gameplay tables that a save-game / progression reimplementation needs: `stats`, `achievements`, `unlockables` (+ `patch_unlockables`, DLC `*_unlockables`), `respect_levels`, `notoriety*`, `difficulty_levels`, `cheats`, `collectibles`, and the smaller rule tables listed in §1.2. Value semantics are recorded where the consumers were followed; base-game *values* are out of reach (the parked mode-(a) limitation — no base-game table was read or decompressed).
**Method:** Exactly the method of `spec-vehicle-data.md` §7 (agent AF): start from the table's **exact file-name string literal** in the executable (a cross-reference to that literal is the loader), follow the loader into its row reader, and read off the element-name literals and the offsets/types they are bound to. Nothing was found by a predicate search. Shared XML accessor helpers were identified once (§1.3) and reused. The DLC archives (`dlc1/2/3.vpp_pc`, stored raw, 142 `.xtbl` files) supplied real samples of `unlockables`, `achievements`, `tweak_table` and `activity_types` (extracted by `tools/harnesses/prog_dlc_extract.py`); the 16 real save snapshots supplied the unlockable-id and stat ground truth.
**Cleanroom compliance:** No decompiled code and no original internal identifiers are reproduced. XML element and table names, stat/unlockable/achievement *names as data*, exact offsets, strides, constants and function addresses (as evidence anchors, base `0x00400000`, unpacked image) are given. The parked items of `HANDOFF.md` §27.3 (mode-(a) container decompression; `.czn_pc` interior) were not touched; no base-game table was opened.
**Confidence key:** **CONFIRMED — disassembly** (read from the code) · **CONFIRMED — empirical** (checked against real data, N/N stated) · **HIGH CONFIDENCE — inferred** · **HYPOTHESIS — unconfirmed** · **OPEN** · **UNKNOWN**.

**Review status summary (2026-09-30).** An adversarial desk review (no executable, no game data opened; Team B's full-population run cited where it exists) graded 38 units: **8 DESK-PASS**, **7 DESK-PASS, text fixes applied**, **18 NEEDS-EXE**, **4 NEEDS-DATA**, **1 VALIDATED-BY-DATA** (§4.2). A desk pass alone does not clear a unit: it means the text is internally consistent, not that it was re-derived from the executable; only VALIDATED-BY-DATA units are already backed by Team B's full-population run. **Units awaiting the executable:** §1.3, §2.3, §3.1, §4.1, §4.3, §4.5, §5, §6.1, §6.3, §10.1, §10.2, §10.4, §10.6, §10.7, §10.9, §10.11, §10.12, §11. **Units awaiting real data:** §2.1/§2.2, §3.2/§3.3, §4.6, §14. Each unit ends with its own review-status line; the open questions are marked **[OPEN — desk review 2026-09-30: …]** at the claim.

---

## 1. Overview, method, and the shared reader grammar

### 1.1 What was found, in one paragraph

Every table in the group is loaded by a small function that opens the table by literal file name, asks the XML tree for a named row element, and stores a handful of named children into a fixed global array. **All 26 requested file names were checked with a raw scan; 25 occur exactly as NUL-delimited literals — the 26th, `gameplay_nag_globals.xtbl`, occurs with different capitalisation (`Gameplay_nag_globals.xtbl`, so a case-sensitive scan misses it; element-name matching is case-insensitive throughout).** Of the group, `stats.xtbl` (§2), `achievements.xtbl` (§3) and `unlockables.xtbl` (§4) were specified to the field; the remaining tables (§5 onward) carry their element tree, destinations and defaults, with the honest gaps stated per table. The strongest results: the **stat table** has exactly **217 rows × `0x48` bytes at `0x01517228`**, each row bound to a static (name → row id) table of **217 names** (an unrecognised `Name` **ends the load of the whole table**); **unlockables** are **`0x100`-byte records at `0x01522750`, capacity 384**, whose `Type` element selects one of **61 type names** with a per-type payload — and the loader's type indices resolve several "meaning OPEN" fields of `spec-save-format.md` §10.6 (§4.6); DLC unlockables reproduce **784/784** saved DLC ids by name hash.

### 1.2 Filename census (raw scan of the whole image, exact NUL-delimited literal)

| Table | Literal at | Loader (function containing the reference) | Section |
|---|---|---|---|
| `stats.xtbl` | `0x0114268C` | `FUN_00713BB0` (also loads `achievements.xtbl`) | §2 |
| `achievements.xtbl` | `0x011425D8` | `FUN_00713BB0` (base), `FUN_00714430` (each DLC) → row reader `FUN_00713840` | §3 |
| `unlockables.xtbl` | `0x0114651C` ~~(suffix of the next)~~ *(corrected 2026-09-30: a standalone literal — the next literal starts `0x01146530 − 0x0114651C = 0x14` bytes later, while a suffix of `patch_unlockables.xtbl` would start 6 bytes into it, at `0x01146536`; `unlockables.xtbl` + NUL is 17 bytes and ends at `0x0114652D`)* | `FUN_0071FAC0` (base), `FUN_0071EF40` (each DLC) → `FUN_0071EDF0` → row reader `FUN_0071E5E0` | §4 |
| `patch_unlockables.xtbl` | `0x01146530` | `FUN_0071FAC0` (after the base load) | §4.5 |
| `respect_levels.xtbl` | `0x01129130` | `FUN_0060ED30` | §5 |
| `notoriety.xtbl` | `0x01128714` | `FUN_00604B50` → `FUN_006049A0` | §6.1 |
| `notoriety_levels.xtbl` | `0x01128804` | `FUN_00604C10` | §6.2 |
| `notoriety_spawn.xtbl` | `0x0116D49C` | `FUN_008EEC00` → `FUN_008ED480` | §6.3 |
| `difficulty_levels.xtbl` | `0x01126C04` | `FUN_005F4710` | §7 |
| `cheats.xtbl` | `0x0113E644` | `FUN_006FBB50` → `FUN_006FBAB0` → row reader `FUN_006FB8D0` | §8 |
| `collectibles.xtbl` | `0x011260A4` | `FUN_005EA0E0` | §9 |
| `store_discounts.xtbl` | `0x0115DD80` | `FUN_0080E930` | §10.1 |
| `gameplay_constants.xtbl` | `0x01127A64` | `FUN_005F5B50` | §10.2 |
| `gameplay_nags.xtbl` | `0x0113F21C` | `FUN_007050A0` | §10.3 |
| `gameplay_nag_globals.xtbl` | **not an exact literal** — `Gameplay_nag_globals.xtbl` | `FUN_007050A0` | §10.3 |
| `mission_checkpoints.xtbl` | `0x0113BD78` | `FUN_006DF8B0` | §10.4 |
| `mission_help.xtbl` | `0x01178370` | `FUN_00A1FF70` → generic per-framework loader `FUN_00604540` | §10.5 |
| `spawn_info_categories.xtbl` | `0x011909CC` | `FUN_00BE5890` → row reader `FUN_00BE5300` | §10.6 |
| `spawn_info_groups.xtbl` | `0x01190A54` | `FUN_00BE5C10` → row reader `FUN_00BE5960` | §10.6 |
| `spawn_info_ranks.xtbl` | `0x01190768` | `FUN_00BE4DA0` | §10.6 |
| `drunk_levels.xtbl` | `0x01172BD8` | `FUN_00975520` → `FUN_00975170` | §10.7 |
| `rank_reactions.xtbl` | `0x01117EDC` | `FUN_0051A520` | §10.8 |
| `default_global.xtbl` | `0x0118CF40` | `FUN_00BB70B0` | §10.9 |
| `tweak_table.xtbl` | `0x01145C48` | `FUN_0071B420` (base), `FUN_0071B450` (each DLC) → `FUN_0071B070` | §10.10 |
| `metered_sprint.xtbl` | `0x01177874` | `FUN_009FFFE0` | §10.11 |
| `activity_types.xtbl` | `0x01129BDC` | `FUN_0084E3B0` (base+mission-set enumeration), `FUN_00617E10` | §10.12 |

Every loader is **run once at start-up (base) and once per mounted DLC framework** where a DLC copy exists (`stats`, `respect_levels`, … have no DLC variant; `achievements`, `unlockables`, `tweak_table`, `activity_types`, `mission_help`, `spawn_info_*` do — §1.4).

**Review status (2026-09-30): DESK-PASS, text fixes applied (§1.2 `unlockables.xtbl` literal is standalone, not a suffix; the case-insensitive archive match of `gameplay_nag_globals.xtbl` is supported by Team B's population run, team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") — desk review (not re-derived from the executable).**

### 1.3 The shared XML reader grammar (recovered once; **cross-reference:** `spec-vehicle-data.md` §7.2 lists the same family for the vehicle reader, and `spec-tables-weapons-combat.md` §1 is the documented home of the per-type element readers)

This section records what *these* loaders exercised, so the tables below can cite it. Everything is **CONFIRMED — disassembly**.

**Table open.** `FUN_00DAC9A0(file name, …)` opens `<file>` through the engine's XML parser (the debug label "xml_table_parse %s" is the file being parsed), keeps the parsed document in a global, stores its **`Table` child element** in the global `0x029CFFA0`, and **returns that `Table` element** (every row lookup below is made on it). A file that fails to parse prints "The table file "%s" is missing or invalid" and the loader carries on with a null document (row loops then run zero times). `FUN_00DAB960` frees the document. `FUN_00DC5AC0(name, …)` is the same open without the `Table` step (used by `default_global.xtbl`, §10.9).

**Node layout (each XML element).** `+0x00` element-name `char*`, `+0x04` next sibling, `+0x08` first child, `+0x0C` **text `char*`** (NULL for an empty or absent text — `<Requirements></Requirements>` has a NULL text). Element-name matching is **case-insensitive** everywhere.

| Function | Semantics |
|---|---|
| `FUN_00DC4FF0(node, name)` | first child named `name`, else NULL |
| `FUN_00DC5030(parent, cur, name)` | next sibling of `cur` named `name`, else NULL |
| `FUN_00DC5150(node, name)` | number of children named `name` (used to size arrays: `spawn_info_*`, `notoriety_spawn`, `store_discounts`) |
| `FUN_00DC5190(node, default)` | `node`'s text, or `default` when the node or its text is NULL |
| `FUN_00DABA10(node, name)` / `FUN_00DABA40(node, name)` | text `char*` of child `name` (NULL if absent) — the two differ only in the NULL-parent guard |
| `FUN_00DABA70` / `FUN_00DABAB0` (dst, size, node, name) | bounded string copy (`FUN_00DA7930`); the second returns whether the child was present |
| `FUN_00DAC900` / `FUN_00DAC950` (dst, size, node, name) | copy of the **`Filename` child** of child `name` (the `<Image_Source><Filename>…` shape) |
| `FUN_00DABC70` / `00DABD20` | **signed int** — "always" (see the caveat below) / "write only if present" |
| `FUN_00DABDF0` / `00DABE80` | **u32** — always / only if present |
| `FUN_00DAC1D0` | **u8** — always |
| `FUN_00DAC480` / `00DAC510` | **bool** — always (false if absent) / only if present; text is compared to the four literals `true`, `yes`, `false`, `no` (case-insensitive, whole string); **anything else reads as false** **[OPEN — desk review 2026-09-30: §6.1 says `Check_Detection` accepts only `true`, which conflicts with this four-literal reader unless `Check_Detection` uses a different reader (likewise `Allow_Update_By_Server` §2.2, compared to `true`); to be settled against the executable (`FUN_006049A0`'s callee, `FUN_00DAC480`/`FUN_00DAC510`).]** |
| `FUN_00DACCB0` / `00DACD40` | **float** — "always" / only if present |
| `FUN_00DACF20` | vec3 (children `X`, `Y`, `Z`) |
| `FUN_00DAB8B0` | integer text: decimal digits, or `0x…` hex; a leading `-` is handled by the signed wrappers; parsing stops at the first non-digit |
| `FUN_00DACB20` | float text: optional `-`, integer part, optional `.digits`, optional exponent; **a leading `0x` reads as `0.0`**; a leading `.` is accepted |

**Caveat on the "always" readers (documented in `spec-tables-weapons-combat.md` §1.3, confirmed here on `FUN_00DACCB0` / `FUN_00DABC70`):** the "always" flavour parses its 1 KB stack buffer even when the child element is **absent** and never clears that buffer on the absent path, so "0 if absent" (`spec-vehicle-data.md` §7.2) is *not guaranteed* — shipped data always supplies these elements; a reimplementation should treat an absent element as 0. Statements below of the form "absent ⇒ 0" mean that recommendation, not a guarantee of the original. The "only if present" flavours are exact (they leave the destination untouched).

**Strings and hashes.** `FUN_00D9E8B0` (`__thiscall`, output pointer in ECX; arguments string, seed, max length) and `FUN_00D9E740(string, seed)` are the **engine name hash — CRC-32, reflected `0xEDB88320`, seed 0, no final XOR, input lower-cased** (table `0x01320DA0`); every "name hash" below is this. `FUN_00D9E7E0` is the same *without* lower-casing. Localisation keys: a `DisplayName`/`Description`/`Event_Text` element holds a **localisation key**, resolved through the string table by `FUN_00849DF0` / `FUN_00849FA0` / `FUN_0084A1B0` (key → CRC → string handle; `FUN_0084A1B0` falls back to showing the key when no entry exists). `FUN_00A74910(text, pool)` makes a persistent pooled copy of a string and returns its pointer; `FUN_00DAD460(len, 1, 0, 0)` is the general heap allocator used to copy names.

**Numbers in the XML.** All numeric text goes through the readers above. None of the loaders in this document apply units other than: `× 1000` for seconds → milliseconds (spawn timers, nags), `÷ 100` for percentages (`FriendlyFirePercentage`, unlockable `Amount`/`Resist_Percent`), and `× 1000` for `Player_Ram_Delay`; each is stated where used. **[OPEN — desk review 2026-09-30: the rounding of each `× 1000` conversion (round-to-nearest vs truncation; float vs double) and of `int(RechargeTime × PantPercentage)` (§10.11) is not stated; to be settled against the executable (`FUN_007050A0`, `FUN_005F4710`, `FUN_00604C10`, `FUN_008ED480`, `FUN_009FFFE0`).]**

**Review status (2026-09-30): NEEDS-EXE: boolean-literal set vs §6.1's "only `true`", and the rounding of the `× 1000` conversions (the name hash itself is VALIDATED-BY-DATA: team-b/HANDOFF.md "### 9.79 `sr3xtbl` foundation", base unlockable ids 4,992/4,992 and DLC 784/784 equal the hash of `Name`) — desk review (not re-derived from the executable).**

### 1.4 The framework / DLC multi-load pattern **[CONFIRMED — disassembly]**

Tables with a DLC form are read in two ways: the **base** load passes the framework name `main` (the literal at `0x0111D44C`); a **DLC** load is run once per mounted bundle. For DLC bundle *b* the file name is built by `FUN_0045BA50(dst, bundle, base name)` as `"<framework>_<base name>"` (the framework string is at bundle `+0x80`; e.g. `dlc1_unlockables.xtbl`), or the plain base name when bundle flag bit 2 (`+0x103 & 4`) is set or the framework name is empty; a bundle is visited only if its byte at `+0x101` is neither `0` nor `0xFF` and bit 1 of `+0x103` is set. **Every row carries an optional `Framework` child (default text `main`); a row is accepted only if `Framework` equals (case-insensitive) the framework being loaded** — for the base load that is `main` *(exceptions, stated in their own sections — added 2026-09-30: `patch_unlockables.xtbl` (§4.5) and the base `tweak_table.xtbl` call (§10.10) pass a **NULL** framework, which accepts every row; the base `achievements.xtbl` pass does filter on `main` (§3.1), which is what keeps the 30 `Framework=dlc*` rows of the 83-row copy out of the base pass)*. Rows are appended after earlier rows; nothing is overwritten (except the duplicate rule of §4.2).

**Review status (2026-09-30): DESK-PASS, text fixes applied (NULL-framework exceptions listed; the bundle-visit predicates `+0x101`/`+0x103` and the name builder `FUN_0045BA50` are disassembly-only, no data reaches them) — desk review (not re-derived from the executable).**

### 1.5 Validation harness and evidence files

`tools/harnesses/prog_validate.py` (predicates stated before running; N/N printed), `prog_common.py` (engine name hash + tolerant XML reader), `prog_unlock_tables.py` (transcribed tables), `prog_dlc_extract.py` (raw DLC extraction into `tools/ai_dlc_xtbl/`). Decompile dumps are `tools/ai_*.txt`; memory dumps of the transcribed tables are `tools/ai_mem*.txt`; the stat table is also in `tools/ai_stat_static_table.tsv`. Ghidra post-scripts: `tools/scripts/AiMem.java`, `AiDecForce.java` (plus AF's `AfDec`, `AfStrXrefs`, `AfAsm`, `AfRangeRefs`); runner `tools/run_tbl2.ps1`.

---

## 2. `stats.xtbl` — the statistics table

### 2.1 Where it lives **[CONFIRMED — disassembly]**

Loader `FUN_00713BB0` (called by `FUN_007105B0`, which then runs the handler-table initialiser `FUN_00714770`). The rows land in a fixed array: **base `0x01517228`, stride `0x48`, exactly 217 rows** (the row array ends at `0x0151AF30`, which is the **row-count global**, the loaded-row count; `0x0151AF34` is the achievement count, §3). **Row index = stat id** — not table order: a static name → id table at **`0x01141BD0`** (217 entries of `{name char*, row id}`, 8 bytes each; extracted in full to `tools/ai_stat_static_table.tsv`) turns the `Name` text into the row index. The ids are exactly the permutation 0…216 and each (name, id) equals the id → name table of `spec-save-format.md` §7.4 (217/217). The names are the strings the game shows ("time played", "total respect", "pistol hit pct", …); they are the only accepted values of `Name`.

### 2.2 Element tree

```
<Table>
  <Stat>                              ← repeated; one per stat (217 expected)
    <Name>            text            ← matched (case-insensitive) against the 217 static names
    <DisplayName>     text            ← localisation key (string-table lookup by hash)
    <Value_Type>                      ← container; the first child present (tested in this order) selects the type
        <integer/> | <float/> | <distance/> | <time/> | <money/> | <boolean/>
      | <percent> <Percentage_Of_Stat>name</Percentage_Of_Stat> </percent>
      | <complex/>
    </Value_Type>
    <Allow_Update_By_Server>  true|false
    <LivePropertyID>          int     ← optional (default 0)
    <LiveLeaderboardID>       int     ← optional (default 0)
    <_Editor>…</_Editor>              ← editor-only, not read (added 2026-09-30: present on 217/217 real rows, team-b/HANDOFF.md "### 9.81")
  </Stat>
```

| Element | Type / reader | Destination (row-relative) | Default / required | Notes |
|---|---|---|---|---|
| `Name` | text, heap-copied | `+0x00` (`char*`) | **required — an unknown name aborts the whole load** | The row is found by scanning the 217 static names with a case-insensitive compare. **If no static name matches, the loader jumps out of its row loop: every later `Stat` in the file is ignored** (it then frees the document and goes on to load `achievements.xtbl`). A duplicate name overwrites the same row and still increments the count. |
| `DisplayName` | localisation key → string handle | `+0x04` | NULL text → 0 handle | resolved by `FUN_00849DF0`. |
| `Value_Type` child | presence test in the order `integer`, `float`, `distance`, `time`, `money`, `boolean`, `percent`, `complex` | `+0x08` handler type and `+0x0C` value class | none present → handler `0x33` (51, "unset"), class 6 | table below. |
| `Percentage_Of_Stat` | text, compared **case-sensitively** against the names of the rows already loaded | `+0x3C` (denominator row id) | **required for `percent`** — a missing element dereferences NULL (HIGH CONFIDENCE: the compare is inlined and unguarded; not exercised) | the denominator must be loaded *earlier in the file* than the percent stat; if it is not found the denominator stays `-1` (`0xFFFFFFFF`) and the class is still set to 4. **[OPEN — desk review 2026-09-30: §14.3 names the 10 denominators but does not check that each denominator row precedes its percent row in file order and matches its spelling case-sensitively; to be settled against real data (`stats.xtbl`).]** |
| `Allow_Update_By_Server` | text compared to `true` (case-insensitive) | bit 1 (`0x02`) of the flags dword at `+0x10` | **effectively required** — a missing element passes NULL to the string compare (HIGH CONFIDENCE, not exercised) | `true` sets the bit; anything else clears it. |
| `LivePropertyID` | signed int, write-only-if-present | `+0x40` | 0 | platform-service property id. |
| `LiveLeaderboardID` | signed int, write-only-if-present | `+0x44` | 0 | platform-service leaderboard id. |

**Review status (2026-09-30), §2.1/§2.2: NEEDS-DATA: percent-denominator file order and case (row count 217/217 VALIDATED-BY-DATA, team-b/HANDOFF.md "### 9.81 `sr3tables_progression`"; `_Editor` added to the tree) — desk review (not re-derived from the executable).**

### 2.3 The stat row (`0x48` bytes) **[CONFIRMED — disassembly; +0x14/+0x18 from the achievement linker (§3.3), +0x38/+0x3C from the readers `FUN_00710730` / `FUN_007107B0` and the entry-27 serializer]**

| Offset | Content |
|---|---|
| `+0x00` | name `char*` (heap copy of the XML `Name`) |
| `+0x04` | `DisplayName` localisation handle |
| `+0x08` | **handler type** (0…50, `0x33` = none) — index of the 51-entry handler table at `0x0151CBF8` (stride `0x18`, six code pointers per entry, filled by `FUN_00714770`; slot 0 is the *update* handler called by `FUN_00710CB0` with (row, value-argument), slot 1 the *requirement-evaluate* handler used by `FUN_00710950`; the other four slots were not tracked — OPEN) |
| `+0x0C` | **value class**: `0` named/derived (no numeric storage — ids 43, 126, 150), `1` boolean, `2` **int32** in `+0x38`, `3` **float32** in `+0x38`, `4` **percent** (int32 numerator in `+0x38`, denominator = the value of row `+0x3C`, result ×100), `5` **set** (`+0x38` holds a pointer; only id 45 "unique vehicles owned"), `6` unset |
| `+0x10` | flags; bit 1 = `Allow_Update_By_Server` |
| `+0x14` | number of achievements that reference this stat (set to 0 by the stat loader, incremented by the achievement loader) |
| `+0x18`–`+0x37` | up to **8** pointers to achievement records that reference this stat (unbounded by the linker) |
| `+0x38` | **value** (int32 / float32 / set pointer per class) — this is the dword the entry-27 save serializer copies |
| `+0x3C` | denominator row id (percent class); `-1` otherwise-unset; this is the dword saved next to the value (the "auxiliary = denominator stat id" of `spec-save-format.md` §12.6.1) |
| `+0x40`, `+0x44` | `LivePropertyID`, `LiveLeaderboardID` |

**[OPEN — desk review 2026-09-30, §2.3: slot 1 of the handler table is called *requirement-evaluate* here but *get* in `spec-save-format.md` §7 item 4 ("slots: update, get, aux, reset, serialise, deserialise"); to be settled against the executable (`FUN_00714770`, `FUN_00710950`, `FUN_00710CB0`).]**

**Review status (2026-09-30), §2.3: NEEDS-EXE: handler-slot naming conflicts with `spec-save-format.md` §7 — desk review (not re-derived from the executable).**

### 2.4 Value type → handler type and class

| `Value_Type` child | handler `+0x08` | class `+0x0C` |
|---|---:|---:|
| `integer` | 0 | 2 |
| `float` | 1 | 3 |
| `distance` | 2 | 3 |
| `time` | 3 | 3 |
| `money` | 4 | 3 |
| `boolean` | 5 | 1 |
| `percent` | 6 | 4 |
| `complex` | by stat id (below) | by stat id |
| *(none)* | `0x33` | 6 |

This reproduces the handler-type numbering of `spec-save-format.md` §7.4 exactly (0 integer … 6 percent, float/distance/time/money → float32 payload).

**`complex` stats.** The handler is fixed **by the stat id** (the `Name`'s row id), independent of anything else in the XML. The loader's switch names **44** ids; each maps to a *distinct* handler type (`7`…`50`); an id not listed leaves handler `0x33`/class 6 and logs "Handlers not set up for complex stat %s". Class in brackets.

| stat id → handler type [class] |
|---|
| 13→8 [3] · 14→9 [3] · 15→10 [3] · 16→11 [3] · 17→12 [3] (the five "time at max … notoriety" counters — the only complex stats with a serialise slot besides id 45) · 18→15 [2] · 19→16 [2] · 20→13 [2] · 21→27 [2] · 22→17 [2] · 23→18 [2] · 24→20 [2] · 25→21 [2] · 26→22 [2] · 27→23 [2] · 28→19 [2] · 29→14 [2] · 30→29 [2] · 31→28 [2] · 32→31 [2] · 33→30 [2] · 34→33 [2] · 35→35 [2] · 36→39 [2] · 37→37 [2] · 38→32 [2] · 39→34 [2] · 40→38 [2] · 41→36 [2] · 43→40 [0] ("best weapon") · **45→42 [5]** ("unique vehicles owned"; the only class-5 row; its `+0x38` is set to the address `0x0151CBF0`) · 50→44 [2] · 51→45 [2] · 58→46 [2] · 59→47 [2] · 60→48 [2] · 61→49 [2] · 66→24 [2] · 67→50 [2] · 69→7 [3] ("cash earned per day") · 126→43 [0] ("most hated gang") · 150→41 [0] ("favorite transportation") · 163→25 [2] ("barnstorms found") · 164→26 [2] ("stunt jumps found") |

**[CONFIRMED — empirical, independent derivation.]** The 38 complex ids whose handler type has no serialise slot (all of the above except 13–17 and 45) are **exactly** the 38 record-less ids of `spec-save-format.md` §7.4 (`18`–`41`, `43`, `50`, `51`, `58`–`61`, `66`, `67`, `69`, `126`, `150`, `163`, `164`) — derived here from the loader's switch, there from the handler-type table; 38/38 (`prog_validate.py` V1.6).

**Review status (2026-09-30), §2.4: DESK-PASS (44 ids → 44 distinct handler types 7…50; 44 − 6 = 38 record-less ids; real distribution 116+44+21+16+10+9+1+0 = 217, §14.3) — desk review (not re-derived from the executable).**

### 2.5 What this settles in `spec-save-format.md` (annotated there by reference, not edited)

- **§12.6.1's "skipping rows whose dword at row `+0x14` equals 5 — which rows these are was not determined"** is a class test: the serializer (`FUN_00710BD0`, a helper Ghidra had not made a function; its wrapper `0x00B96EB0` adds `0x3340` to the buffer and passes the count `0x95` = 149) copies **`+0x38` and `+0x3C`** of rows 68…216 and skips rows whose **value class (`+0x0C`) is 5**. Only stat **id 45** has class 5 and 45 < 68, so **the skip never fires for ids 68–216**; the "20 rows that are 0 in all 16 samples" are therefore *not* skipped rows — they are stats that were simply never incremented. **[CONFIRMED — disassembly of both ends; V1.8.]**
- The row *value* at `+0x38` is an `int32` for classes 1, 2, 4 and a `float32` for class 3 (time/distance/money/float) — the int/float split of §7.4 falls out of the `Value_Type` child, which is data in `stats.xtbl`, not code.

**Review status (2026-09-30), §2.5: DESK-PASS (`spec-save-format.md` already carries the correction) — desk review (not re-derived from the executable).**

### 2.6 Open items for `stats.xtbl`

1. **Which stat is `integer` vs `float` vs `boolean` …** for the 173 non-complex ids is the `Value_Type` child of each row of the (unreadable) base table — the schema is closed, the per-stat assignment is not. *[Resolved in aggregate §14.3: the real `Value_Type` distribution over all 217 rows is recorded there, and per-id values are recoverable from the base file; the handler-type slots below are not affected.]* The handler-type slots 2–5 of the 51-entry table (`0x0151CBF8`) were not classified. **[OPEN.]**
2. Whether the base `stats.xtbl` defines all 217 stats. The array bound (`0x0151AF30` = row 217) means at most 217 rows can be stored; the row-count global is used as the loop bound by every consumer, so **a table missing any static name would leave that id's row unnamed while shrinking the visible count** — HIGH CONFIDENCE the shipped table has all 217. *[Resolved §14.3: 217/217 rows present.]*

**Review status (2026-09-30), §2.6: DESK-PASS (both items resolved by §14.3) — desk review (not re-derived from the executable).**

---

## 3. `achievements.xtbl` — and its link to `stats.xtbl`

### 3.1 Where it lives **[CONFIRMED — disassembly]** *(scope narrowed 2026-09-30: CONFIRMED covers the loaders, the row reader, the record base/stride and the count globals only; which same-named `achievements.xtbl` copy is loaded is HIGH CONFIDENCE, not CONFIRMED — §14.2 states "the exact hop from the table loader's own open call down to the dispatcher was not walked")*

Base: `FUN_00713BB0` opens `achievements.xtbl` right after the stat rows and runs the row reader **`FUN_00713840(document, "main")`**; each DLC bundle: `FUN_00714430` runs `FUN_00713840(document, framework)` on `<framework>_achievements.xtbl` (§1.4). After the base load the count `0x0151AF34` is copied to `0x0151AF38` (**base achievement count**). Records: **base `0x0151AF48`, stride `0x58`**, count `0x0151AF34`; neither loader bounds the count. The next known data (the handler table, `0x0151CBF8`) leaves room for **83 records** (`0x0151CBF0` is where the array would run into it) — ~~the retail row counts (base + 3 DLC × 10) are not knowable without the base table, so whether the array fits is **OPEN**~~ **RESOLVED for the row counts (§14.2): the real `misc_tables.vpp_pc` copy has 53 rows; a second, same-named copy in `patch_compressed.vpp_pc` has 83 rows — 53 (minus one renamed row) plus all 30 known DLC achievements, each tagged `Framework=dlc1/dlc2/dlc3` — filling the 83-record window with ZERO slots of slack. Which of the two same-named files the running game actually loads (archive mount precedence) was NOT traced — §14.2's honest gap.** **[RESOLVED 2026-09-24 (§14.2; §13.2 item 3): the `patch_compressed.vpp_pc` copy (83 rows) is the one loaded.]** *(HIGH CONFIDENCE, not CONFIRMED — see the scope note in the heading. Desk review 2026-09-30: the row counts do not discriminate between the two copies — the base pass filters `Framework` = `main` (§1.4), so it reads 53 rows from either copy and the DLC loaders add the same 30; only the mount/priority trace of §14.2 does.)* **[OPEN — desk review 2026-09-30: the hop from `FUN_00DAC9A0` (called by `FUN_00713BB0`) to the named-resource dispatcher; to be settled against the executable.]**

**Review status (2026-09-30), §3.1: NEEDS-EXE: loader-to-dispatcher hop not walked; CONFIRMED label scope narrowed in place — desk review (not re-derived from the executable).**

### 3.2 Element tree (validated on the 30 raw DLC rows — §3.5)

```
<Table>
  <Achievement>                       ← repeated
    <Name>                  text      ← the identifying key (e.g. "Dead Presidents", "BAMF!")
    <DisplayName>           text      ← localisation key; stored as its CRC-32 only if it exists in the string table (else 0)
    <Image>                 text      ← optional; heap copy
    <Requirements>                    ← container (may be empty)
      <Requirement>         repeated
        <Stat>name</Stat>             ← case-insensitive lookup among the loaded stat rows
        <Condition>at least | at most</Condition>
        <Value>number</Value>         ← int for an integer-class stat, float for a float-class stat
      </Requirement>
    </Requirements>
    <HudUpdateFrequency>    int       ← always written (0 = no HUD update)
    <HudUpdateNumSuppress>  bool      ← always written; "show the bar but not the number"
    <Avatar_award>          text      ← presence-tested only (see below)
    <PC_Only>               bool      ← authoring flag: NOT read by the loader
    <Framework>             text      ← default `main`
    <_Editor><Category>…</Category></_Editor>   ← editor-only
  </Achievement>
```

The DLC rows also carry the editor schema (`<TableDescription>` with the same element list, types and defaults — `HudUpdateFrequency` Int ≥ 0 default 0; `HudUpdateNumSuppress` True/False default False; `Condition` selection default `at least`; `Value` Float default 0.0; `Requirement.Stat` is a **`Reference` to `stats.xtbl`, type `Stat.Name`**; `DisplayName` a `Reference` to `localized_exports\HUD_text.xtbl`, type `HUD_Identifier.Name`; `Framework` a `Reference` to `dlc_frameworks.xtbl`, type `BundleInfo.Flag_String`). That authoring metadata is the independent source for the cross-table references.

### 3.3 The achievement record (`0x58` bytes, record *i* at `0x0151AF48 + 0x58·i`) **[CONFIRMED — disassembly]**

| Offset | Content |
|---|---|
| `+0x00` | name `char*` (heap copy of `Name`) |
| `+0x04` | name hash (CRC-32, §1.3) |
| `+0x08` | `DisplayName`: the key's CRC-32 if the key exists in the string table, else `0` (`FUN_00849FA0`) |
| `+0x0C` | number of requirements |
| `+0x10 + 0x0C·k` | requirement *k*: `{stat-row pointer, condition (0 = at least, 1 = at most), value (int32 or float32)}` — room for **4** before `+0x40`; unbounded |
| `+0x40` | byte, cleared at load (runtime "earned" state) |
| `+0x44` | **platform achievement id** — found by scanning the 60-name table at `0x012F5620` (`{name char*, id}`, 8 bytes; ids 1…57, three names with id 0 — "Bromance In The Row", "Crew of Two", "Partners In Crime"); a `Name` not in the table (**every DLC achievement**) gets the default `0` |
| `+0x48` | `0` — assigned from a static initialiser whether or not `Avatar_award` is present (both branches store the same value; the element's text is never used, so the **avatar award is not implemented on this PC build**) |
| `+0x4C` | `HudUpdateFrequency` |
| `+0x50` | `HudUpdateNumSuppress` (byte) |
| `+0x54` | `Image` `char*` (0 if absent) |

**Linking.** For each `Requirement` the linker finds the stat row by name (case-insensitive scan of the loaded rows); **an unknown stat name silently drops that requirement** (the count is not incremented, so the next requirement overwrites the slot). On success it stores the achievement's address into the stat row's pointer array at `+0x18 + 4·(stat.+0x14)`, increments the stat's `+0x14`, decodes `Condition` (`at least` → 0, `at most` → 1; anything else logs "Unknown requirement condition: %s" and leaves the field unset) and reads `Value` as an **int if the stat's class is 2, a float if 3**; any other class logs "Unexpected tracked stat requirement value" and leaves the value unset. A stat can therefore back at most 8 achievements before the pointer array reaches its value dword (unchecked). **[OPEN — desk review 2026-09-30: neither cap (4 requirements per achievement, 8 achievements per stat) has been measured against the 83 real rows, nor whether any real requirement names a percent-class or boolean-class stat (value left unset); to be settled against real data.]**

**Review status (2026-09-30), §3.2/§3.3: NEEDS-DATA: requirement and back-pointer caps not measured on real rows (record tiling checked: `0x10 + 4·0x0C = 0x40`, then `+0x40`…`+0x54` → `0x58`; 53 base rows and `PC_Only` 53/53 per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") — desk review (not re-derived from the executable).**

### 3.4 Consumers (evidence, not specified further)

`FUN_00710580` finds an achievement by platform id (scan of `+0x44`); `FUN_00710C50`/`FUN_00710950` evaluate a record's requirements through the handler table's slot 1 (`at least`: value ≥ target; a percent stat uses its denominator) and, when all hold, award (`FUN_007108A0`).

### 3.5 Validation against the raw DLC samples **[CONFIRMED — empirical]**

`prog_validate.py` V3: **30/30** DLC achievement rows found (10 per DLC); the only tag present but never read is **`PC_Only`** (30/30 rows carry it; the loader never asks for it — **an authoring-only flag on this build**) plus the editor-only `_Editor`; `Framework` equals the owning DLC in 30/30; every `Requirements` list is empty in 30/30 (DLC achievements are awarded by script, not by stat thresholds); `HudUpdateFrequency` is `0` and `HudUpdateNumSuppress` is `True`/`False` in 30/30; names unique 30/30. The 30 DLC names are not in the 60-name platform-id table, so their `+0x44` is `0` by the fall-through.

### 3.6 Open items

~~The retail base achievement count and whether base + DLC fit the 83-record window~~ **RESOLVED (§14.2): 53 base rows; 83 in a second same-named `patch_compressed.vpp_pc` copy that exactly merges base+DLC, filling the window exactly.** Slot-by-slot meaning of the handler table's slots 2–5; what consumes `HudUpdateFrequency` (the HUD progress bar — not traced); **NEW OPEN ITEM (§14.2): which of the two same-named `achievements.xtbl` copies the game actually mounts/reads — archive mount-order precedence across `.vpp_pc` archives was not traced this pass, and resolving it matters because it decides whether a real double-load overflow risk exists.** **[RESOLVED 2026-09-24 (§14.2; §13.2 item 3): the `patch_compressed.vpp_pc` copy (83 rows) is the one loaded.]** *(HIGH CONFIDENCE — see §3.1's scope note.)* **[OPEN.]**

**Review status (2026-09-30), §3.4–§3.6: DESK-PASS (53 + 30 = 83; `0x0151AF48 + 83·0x58 = 0x0151CBD0` < `0x0151CBF8`) — desk review (not re-derived from the executable).**

---

## 4. `unlockables.xtbl`, `patch_unlockables.xtbl` and the per-DLC `*_unlockables.xtbl`

### 4.1 Where it lives **[CONFIRMED — disassembly]**

| Item | Value |
|---|---|
| Table | records of **`0x100` bytes** at **`0x01522750`** (the runtime table of `spec-save-format.md` §10.6); record count `0x015226FC`; **base-count marker `0x015226F8`**; **capacity 384 (`0x180`)** — the row loop stops at 384 |
| Loader | `FUN_0071FAC0` (base): loads `unlockables.xtbl` with framework `main` through `FUN_0071EDF0`, then **stores the record count at `0x015226F8`** (312 in every save), then loads `patch_unlockables.xtbl` (§4.5); per DLC `FUN_0071EF40` → `FUN_0071EDF0` on `<framework>_unlockables.xtbl` |
| Row loop | `FUN_0071EDF0`: for each `<Unlockable>` accepted by the `Framework` rule (§1.4): hash `Name` (CRC-32, §1.3); **skip the row if a record with the same hash already exists** (first definition wins; the invalid-id constant `DAT_029C9964` = 0 is exempt); read the record with `FUN_0071E5E0`; **count the row only if that returns true** — it returns **false when the `<Type>` element is absent or has none of the 61 type children**, so such a row is dropped and its slot reused by the next |
| Special case | after the base load, the record whose name hashes as `M17_Vehicle_STAG_Tank_Upgrade` gets its `Is_DLC` field (`+0xF8`) forced to `2` ("Sometimes"); if the name is not defined the store is through a NULL record (HIGH CONFIDENCE; not exercised) |

**Order = save order.** *(2026-09-30: holds for the DLC block only — for the base block this heading is superseded by the CORRECTION at the end of this paragraph.)* The base records `[0, base count)` are the saved block of entry 13; records added by DLC loads (and by `patch_unlockables.xtbl`) follow in load order and are the saved block of entry 28 (`spec-save-format.md` §6.4/§10.6). **Empirical (V2.11/V2.12): in the 14 version-9 snapshots, the DLC-side id block (`0x15D90`) holds 62 ids; slots 6…61 are exactly the CRC-32 name hashes of the 56 rows of the raw `dlc1/2/3_unlockables.xtbl` in file order (`dlc1` ×25, `dlc2` ×23, `dlc3` ×8) — 784/784 — and slots 0…5 are six entries from a bundle loaded before them whose table is inside the (unreadable) base archives.** *(**RESOLVED 2026-09-20 with the now-readable base tables** (`spec-vpp-container.md` §7; verified by SPEC TEAM from the real files): slots 0…5 are the six rows of `patch_unlockables.xtbl` — `Community_Row_Shirt`, `Community_Logo_Shirt`, `Community_Fleur_Hoodie`, `Community_3rd_Street_Hoodie`, `Community_Jerseys`, `Community_Antennae` — identical in all 14 version-9 snapshots. The 312 saved BASE ids (`0x4428`) equal CRC-32(lower(`Name`)) of a row of `unlockables.xtbl` (~~403 rows~~ **312 rows — the real `<Table>` element the loader reads; 403 double-counted an editor-only `<TableTemplates>` sibling of 91 more `<Unlockable>`-named elements no loader ever visits, §14.1**) or `patch_unlockables.xtbl` (6 rows): **4,992/4,992** over 16 snapshots (Team B's claim, re-derived) — ~~so not every row is saved~~ **and in fact the saved-id SET is IDENTICAL to the real 312-row set in all 16 snapshots: every row is saved and every saved id is a real row, a full bijection (§14.1, CONFIRMED — empirical).** **CORRECTION to "Order = save order" for the BASE block:** the base slot order is NOT the file order of `unlockables.xtbl` (slot k == row k for only 10 of 312 slots; 179 of 311 consecutive slot pairs ascend, i.e. near chance); the claim holds for the DLC block (784/784). Team B measured 176/4,992 slots ascending, and that 1,392/1,424 slots' `<Priority>` target sits at an EARLIER slot (not re-derived here).)* The version-8 snapshot's 31 ids are those six + the 25 `dlc1` rows. **[OPEN — desk review 2026-09-30: what produces the saved base order is unexplained — either a reorder after the row loop of `FUN_0071EDF0` inside `FUN_0071FAC0`, or a different enumeration order in the entry-13 serializer/loader (`0x0071FD30`, selector `0x0071DBD0`); to be settled against the executable (cheap data test first: compare the saved order with `Priority`-topological, hash, and Category-then-file order).]**

**Review status (2026-09-30), §4.1: NEEDS-EXE: base-block save order mechanism (312 rows, 6 patch rows and 4,992/4,992 hashes VALIDATED-BY-DATA, team-b/HANDOFF.md "### 9.81 `sr3tables_progression`" and "### 9.79 `sr3xtbl` foundation"; `0x01522750 + 384·0x100 = 0x0153A750`, 312 + 6 + 56 = 374 ≤ 384) — desk review (not re-derived from the executable).**

### 4.2 Element tree (validated on 56 raw DLC rows)

```
<Table>
  <Unlockable>                              ← repeated
    <Name>                       text       ← key; record id = CRC-32(lower(Name))
    <Type>                                  ← REQUIRED container; exactly one child selects the type (see §4.4)
        <Vehicle>|<Homie>|…|<Crib_Ammo>  …payload…
    </Type>
    <DisplayName>                key        ← localisation key; record +0xC0 = string handle, +0xC8 = CRC of the key
    <Description>                key        ← localisation key; +0xC4 handle, +0xCC CRC
    <Image_Source><Filename>tex.tga</Filename></Image_Source>   ← extension stripped ("ui_reward…") and pooled → +0xD0
    <Detailed_Description_Text>  key        ← pooled string → +0xD8
    <Event_Text>                 key        ← pooled string → +0xDC
    <Category>                   one of 10  ← case-insensitive; +0xD4 = index (default 0 if unmatched)
    <Price>                      u32        ← +0xF0
    <Priority>                   text       ← names another unlockable (authoring reference `unlockables.xtbl`, `Unlockable.Name`); CRC-32 of the text → +0xE4 (0 if absent)
    <Auto_Unlock>                enum       ← +0xF4
    <Is_DLC>                     enum       ← +0xF8; value 1 also clears the DLC-gate byte
    <Framework>                  text       ← default `main`
    <_Editor><Category>Entries:…</Category></_Editor>      ← editor-only
  </Unlockable>
```

| Enumeration | Accepted text (case-insensitive) → stored value |
|---|---|
| `Category` (10) | `Player Abilities` 0 · `Health` 1 · `Damage` 2 · `Weapons` 3 · `Vehicles` 4 · `Homies` 5 · `Discounts` 6 · `Customization` 7 · `Strongholds` 8 · `Activities` 9 |
| `Auto_Unlock` | absent 0 · `Silently` 1 · `Loudly` 2 · `Loudly (Helipad)` 3 · `Silently (Last)` 4 |
| `Is_DLC` | `False` 0 · `True` 1 · `Sometimes` 2 (absent = 0) |

**Review status (2026-09-30), §4.2: VALIDATED-BY-DATA: 312 real rows with 0 undocumented elements (§14.1) and zero unrecognised children for `unlockables`/`patch_unlockables` in Team B's full-population run (team-b/HANDOFF.md "### 9.81 `sr3tables_progression`"); Category sum 61+53+44+36+30+24+25+19+12+8 = 312. Only `Silently` of the four `Auto_Unlock` spellings occurs in base data — desk review (not re-derived from the executable).**

### 4.3 The record (`0x100` bytes) **[CONFIRMED — disassembly]**

| Offset | Content |
|---|---|
| `+0x00` | **name hash** (id) |
| `+0x04` | **type index** 0…60 (`-1` before a `Type` child is matched) |
| `+0x08`–`+0xBF` | **type payload** (§4.4) — per-type; the fixed part is `+0x08`…`+0x1B`, list types run to `+0x2C`/`+0xBC` |
| `+0xC0` / `+0xC4` | `DisplayName` / `Description` string handles |
| `+0xC8` / `+0xCC` | CRC-32 of the `DisplayName` / `Description` keys |
| `+0xD0` | image name (pooled, extension stripped) |
| `+0xD4` | category index |
| `+0xD8` / `+0xDC` | `Detailed_Description_Text` / `Event_Text` pooled strings |
| `+0xE0` / `+0xE1` / `+0xE2` | **state bytes** — the three per-unlockable bits of `spec-save-format.md` §10.6: set A "purchased/active" (`+0xE0`), set B "unlocked/available" (`+0xE1`), set C (`+0xE2`); zeroed by the parser |
| `+0xE4` | `Priority` hash |
| `+0xE8`, `+0xEC` | zeroed by the parser (runtime) |
| `+0xF0` | `Price` |
| `+0xF4` / `+0xF8` | `Auto_Unlock` / `Is_DLC` enums |
| `+0xFC` | **DLC-gate byte**: `0xFF` by default, **`0`** if `Is_DLC` = `True` (the same `0x00`/`0xFF` convention as the vehicle entry's `+0x1C`, `spec-vehicle-data.md` §7.3) |
| `+0xFD` | zeroed |

**[OPEN — desk review 2026-09-30: bytes `+0xE3`, `+0xFE` and `+0xFF` are not described (the table above tiles `0x100` otherwise); no data covers record offsets; to be settled against the executable (`FUN_0071E5E0`).]**

**Review status (2026-09-30), §4.3: NEEDS-EXE: three undescribed bytes; record offsets have no data coverage — desk review (not re-derived from the executable).**

### 4.4 The 61 types and their payload elements **[CONFIRMED — disassembly for every type; the DLC rows exercise 7 of the 61]** *(scope narrowed 2026-09-30: CONFIRMED covers the payload columns; the "apply" descriptions are HIGH CONFIDENCE naming except the setters followed in §4.6 — see the precision note below)*

The `Type` element's children are tested **in this order; the first present wins**, its position is stored as the type index, and the sub-reader runs on that child. The **"re-apply"** column is the byte of the table at `0x012F5C98` (`1` = the effect is re-applied when a save is loaded; `0` = the effect survives only through the scalars entry 13 saves — `spec-save-format.md` §10.6); the **apply routine** is `FUN_0071F170`. "hash" = CRC-32 (§1.3); "pooled" = `FUN_00A74910` copy; "lookup" names the resolver. **Precision note:** the *payload* columns are read directly from the sub-readers; the "apply" wording is read from which callees each case of the apply switch invokes and from the type's name — only the setters listed in §4.6 were followed into their callees, so treat the other apply descriptions as HIGH CONFIDENCE naming, not as decoded behaviour.

**Nesting of the payload paths (clarified 2026-09-30).** Every payload path in the table is relative to the selected type child, which is itself a child of `<Type>`; list wrappers are real elements. Type 0 is `<Type><Vehicle><Vehicles><Vehicle><Type>name</Type><Variant>…</Variant></Vehicle>…</Vehicles></Vehicle></Type>` (§11: `Vehicle/Vehicles/Vehicle/Type`); type 7 is `<Type><Clothes><Items><Item><ItemName>…</ItemName>…</Item>…</Items></Clothes></Type>` (§11: `Clothes/Items/Item/ItemName`); type 25 is `<Type><Taunts><Actions><Animation_Action><Action>…</Action></Animation_Action>…</Actions></Taunts></Type>`.

| # | Type child | Payload elements → record offset (type) | Resolver / what the apply routine does | re-apply |
|--:|---|---|---|:-:|
| 0 | `Vehicle` | `Vehicles`→(≤5 × `Vehicle`): `Type` (vehicle name) → `+0x08+8k` u16 slot, **`Variant`** (optional, pooled) → `+0x0C+8k`; count `+0x30` | vehicle name → `FUN_00AC27A0` (name hash → vehicle-table slot, `spec-vehicle-data.md` §7.1); a name that resolves to `0xFFFF` aborts the sub-reader. Apply: spawns/gives each vehicle (default variant text "Reward") | 0 |
| 1 | `Homie` | `Name` → `+0x08` = the homie record's `+0x20` field | homie name → `FUN_007E7490` (CRC of the name → homie table `0x022AD780`, stride `0x180`) | 0 |
| 2 | `Crib_Weapon` | `Name` → `+0x08` (weapon-definition pointer) | `FUN_00B81220` (weapon table `0x028DC4DC`, stride `0x7EC`; only definitions with flag bit `0x04` at `+0x18`) | 0 |
| 3 | `Store_Weapon` | `Name` → `+0x08` | same resolver | **1** |
| 4 | `Discount` | `name`, `name_2` … `name_8` (≤8 hashes) → `+0x08+4k`, count `+0x28`; `Amount` float → `+0x2C` | the `name` values are **shop names** (authoring reference `shop_names.xtbl`, `Shop_Names.Name`), hashed; the apply routine (`FUN_0080ED20`, in the store-discount module of §10.1) applies `Amount` as a discount to each named shop | 0 |
| 5 | `Crib_Customization_Discount` | `Amount` float → `+0x08` | — | 0 |
| 6 | `Crib_Level` | `Crib_Name` (pooled) → `+0x08`, `Level` int → `+0x0C` | crib handle by name; apply raises the crib level | 0 |
| 7 | `Clothes` | `Items`→(≤9 × `Item`, stride `0x14` from `+0x08`): `ItemName`, `ItemVariant`, `Color1`/`Color2`/`Color3` (hashes); count `+0xBC` | item table `FUN_008282C0` (index → `0x022DB9C8`, stride `0x7C`) and variant `FUN_008282F0`; colours `FUN_00746D60` | 0 |
| 8 | `Sprint_Bonus` | `Amount` int → **float = Amount ÷ 100 at `+0x08`**, `Level` int → `+0x0C` | apply: `≤ 0` ⇒ unlimited sprint (meter state 3); else sets the meter scale (saved at `0x4A50/0x4A54`) | 0 |
| 9 | `Damage_Resist` | `Source` → `+0x08`, `Resist_Percent` int → **float = % ÷ 100 at `+0x0C`** | `FUN_009DB320`: `bullet` 0 · `explosion` 1 · `vehicle` 2 · `fire` 3 · `falling` 4 (`script` 5 is in the list but the apply rejects ≥ 5); apply: player damage scale[type] := 1 − amount (saved at `0x4A34…`) | 0 |
| 10 | `Notoriety` | `Score` → byte `+0x08` (**5 = unrecognised → sub-reader fails**), `Amount` int → **float = % ÷ 100 at `+0x0C`** | `FUN_00605160` (aliases: `luchadores`/`brotherhood`/`los_carnales` → 0, `deckers`/`ronin`/`vice_kings` → 1, `morningstar`/`samedi`/`rollers` → 2, **`police` → 3**); apply: notoriety multiplier[category] := 1.0 + amount (saved at `0x4A24…`) | 0 |
| 11 | `Health` | `Modifier` float → `+0x08` | apply: player `+0x2770` := value (saved `0x4A4C`) | 0 |
| 12 | `Crib` | `Name` (pooled) → `+0x08` | crib name; apply unlocks the crib | 0 |
| 13 | `Music` | *(none read)* | — | 0 |
| 14 | `Repair_Discount` | `Amount` float → `+0x08` | apply: adds to the vehicle-repair discount fraction and clamps to [0, 1] (saved `0x4A58`) | 0 |
| 15 | `Mission_Stronghold` | *(none)* | — | 0 |
| 16 | `Custom` | *(none)* | — | 0 |
| 17 | `Gang_Customization` | `Gang_Type` → hash at `+0x08` | gang-customization unlock for that gang | **1** |
| 18 | `Gang_Vehicle_Customization` | `Vehicle_Group` → hash at `+0x08` | — | **1** |
| 19 | `Melee_Damage_Bonus` | `Damage_Multiplier` float → `+0x08` | **apply: player `+0x1B70` := value** (saved `0x4A60`) | 0 |
| 20 | `Unlimited_Crib_Ammo` | `weapon_class` → `+0x08` | `FUN_00B81280`: index into a 23-name weapon-class list beginning `pistol` | **1** |
| 21 | `Ammo_Multiplier` | `inv_slot` → byte `+0x0C`, `ammo_mul` float → `+0x08` | `FUN_008DC6B0`: 11-name inventory-slot list beginning `unarmed` (255 = unknown) | **1** |
| 22 | `No_Reloading` | `inv_slot` → byte `+0x08` | same | **1** |
| 23 | `Firearm_Accuracy` | `accuracy_multiplier` float → `+0x08` | apply: player `+0x27A8` := value (saved `0x4A68`) | 0 |
| 24 | `Gang_Taunts` | `Team` → byte `+0x08` | `FUN_0094CC60` (team name → id; 9 if unknown) | **1** |
| 25 | `Taunts` | `Actions`→(≤8 × `Animation_Action`): `Action` → `+0x08+4k`, count `+0x28` | `FUN_004BF810` (animation-action name → index in the table at `0x03171C10`, stride `0x10`) | **1** |
| 26 | `Compliments` | same as `Taunts` | same | **1** |
| 27 | `Outfit` | `Outfit` → `+0x08` | `FUN_0082A6B0` (CRC of the name → outfit list `0x022DB9B8`, hash at `+4`, next at `+0x70`) | 0 |
| 28 | `Free_Driveup_Homie` | *(none)* | apply sets a homie drive-up flag | 0 |
| 29 | `Pickpocket` | *(none)* | — | **1** |
| 30 | `Dual_Wield` | `Weapon_Type`: `pistols` → 0, `SMGs` → 1 (`-1` = neither) → `+0x08` | — | **1** |
| 31 | `Weekly_Payments` | `Amount` int → `+0x08` | apply: player `+0x27AC` (u16) += amount (saved `0x4A6E`) and starts a periodic payment | 0 |
| 32 | `Pay_Cash_for_Respect` | `Cash_Cost` int → `+0x08`, `Respect_Received` int → `+0x0C` | apply: charges cash and awards respect | 0 |
| 33 | `Lump_Sum_of_Money` | `Amount` int → `+0x08` | apply: adds cash | 0 |
| 34 | `Vehicle_Customization` | *(none)* | apply sets the global "vehicle customization unlocked" byte `0x027B15F8` (saved `0x4A6C`) | 0 |
| 35 | `Character_Customization` | *(none)* | sets player flag `+0x1D99` bit 0 | **1** |
| 36 | `Gang_Customization_Unlock` | *(none)* | sets `+0x1D99` bit 1 | **1** |
| 37 | `Respect_Bonus_Modifier` | `Modifier` float → `+0x08` | apply: respect-gain multiplier (`0x012FD1B0`) += value (saved `0x4A5C`) | 0 |
| 38 | `Cash_Bonus_Modifier` | `Modifier` float → `+0x08` | apply: global `0x012F5D00` += value (starts 1.0; **not** the `0x4A60` scalar) | **1** |
| 39 | `NPC_Cash_Drop_Modifier` | `Modifier` → `+0x08` | global `0x012F5D04` += value | **1** |
| 40 | `DBNO_Extra_Time` | child **`ms`** (int) → `+0x08` | down-but-not-out extra time | **1** |
| 41 | `Revive_Hold_Time_Modifier` | `Modifier` → `+0x08` | revive-hold modifier | **1** |
| 42 | `Homie_Health_Bonus` | `Modifier` → `+0x08` | global `0x012F5D08` | **1** |
| 43 | `Special_Homie_Health_Bonus` | `Modifier` → `+0x08` | global `0x012F5D0C` | **1** |
| 44 | `Player_Health_Bonus` | `Modifier` float → `+0x08`, `Level` int → `+0x0C` | — | **1** |
| 45 | `Muscles` | `Boost` → `+0x08`, `Melee_Multiplier` → `+0x0C`, `Throw_x`/`Throw_y`/`Throw_z` → `+0x10`/`+0x14`/`+0x18` (floats) | apply: player `+0x1B70` := `Melee_Multiplier` (same setter as type 19) | **1** |
| 46 | `Weapon_Customization_Discount` | `Amount` float → `+0x08` | — | 0 |
| 47 | `Reminder` | *(none)* | — | 0 |
| 48 | `Auto_Complete_City_Takeover_District` | *(none)* | — | 0 |
| 49 | `Auto_Complete_City_Takeover_All` | *(none)* | — | 0 |
| 50 | `Explosive_No_Ragdoll` | *(none)* | — | **1** |
| 51 | `Gang_Weapons` | `Weapon_Slot` → byte `+0x08` | inventory-slot resolver | **1** |
| 52 | `Vampire` | *(none)* | — | **1** |
| 53 | `Customization_Items` | `Team_Name` → byte `+0x08` | team resolver | 0 |
| 54 | `Bloody_Mess` | *(none)* | — | **1** |
| 55 | `Reload_Speed_Bonus` | `scalar` float → `+0x08` | — | **1** |
| 56 | `Immune_To_Flat_Tires` | *(none)* | — | **1** |
| 57 | `Collectable_Finder` | *(none)* | sets `+0x1D99` bit 2 | **1** |
| 58 | `Crib_Cash_Limit_Scalar` | `scalar` float → `+0x08` | crib stash limit scalar | **1** |
| 59 | `Crib_Cash_Stronghold_Scalar` | `scalar` float → `+0x08`, `district` (hash) → `+0x0C` (default: invalid id) | stronghold cash scalar | **1** |
| 60 | `Crib_Ammo` | `percent` float → `+0x08` | crib ammo percent | **1** |

The **re-apply column matches `spec-save-format.md` §10.6's list exactly** (31 types: 3, 17, 18, 20–22, 24–26, 29, 30, 35, 36, 38–45, 50–52, 54–60 — read here from the byte table, `REAPPLY` in `prog_unlock_tables.py`). Type names in the "no elements read" rows are the ones the DLC rows use (`Reminder`, `Custom`) with only their name present.

**Real-data note.** The DLC rows use **7 types**: `Reminder` 12, `Outfit` 11, `Vehicle` 11, `Homie` 10, `Clothes` 6, `Custom` 4, `Gang_Customization` 2 (56 rows). Every row's `Type` has exactly one child that is one of the 61 names (56/56) and every child element of that type element is one the sub-reader asks for (56/56).

**Review status (2026-09-30), §4.4: DESK-PASS, text fixes applied (label scope narrowed; nesting example added; re-apply list 31/31 re-checked; element names independently confirmed 60/60 by the DLC schema, §4.7; the real-data type census is short by 4 rows, see §14.1) — desk review (not re-derived from the executable).**

### 4.5 `patch_unlockables.xtbl` **[CONFIRMED — disassembly for the load; HIGH CONFIDENCE for the flag being constant]**

`FUN_0071FAC0` loads it **after** storing the base count, **only if the global byte `0x0149365D` is non-zero**, and with a **NULL framework** (`FUN_0071EDF0`'s framework argument absent) — which makes the loader accept **every** `<Unlockable>` row regardless of its `Framework` child. The flag has exactly one writer in the image (`FUN_005D1A30`, start-up), which stores 1, so the file is read on every PC start. Rows in it obey the same duplicate-hash rule (a patch row whose `Name` already exists is dropped), and because they are appended after `0x015226F8` they belong to the **DLC-side** saved block. Its rows use the same element tree as §4.2. **[OPEN: the file's content — unreadable; presumably fixes to base rows; the duplicate rule means it can *add* unlockables but not *change* existing ones.]** **[Resolved §14.1: the file has 6 rows (named there) and an empty `<TableTemplates>` sibling.]** **[OPEN — desk review 2026-09-30: "read on every PC start" needs the store of 1 into `0x0149365D` inside `FUN_005D1A30` shown to be unconditional (`spec-world-streaming.md` leaves the same question open for the sibling flag `0x0149365C` stored by the same function); behaviourally supported, since the 6 rows are in all 14 version-9 saves (§4.1); to be settled against the executable.]**

**Review status (2026-09-30), §4.5: NEEDS-EXE: unconditional flag store not shown (6 rows, all `Clothes`, found by Team B, team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") — desk review (not re-derived from the executable).**

### 4.6 What the loader's type table resolves in `spec-save-format.md` §10.6 (annotated there by reference, not edited)

The parse loop stores the type **index** in record `+0x04`; the apply routine `FUN_0071F170` switches on the same field, so the names above are the names of §10.6's numeric types **[CONFIRMED — disassembly, both ends]**:

| Saved field (§10.6) | Type (index → name) | Resolves | Label |
|---|---|---|---|
| `0x4A24`–`0x4A33` notoriety multipliers | 10 `Notoriety`; category from `Score` | **Index 0 = luchadores, 1 = deckers, 2 = morningstar, 3 = police** (resolver `FUN_00605160`). §10.6's "index 0 = police is the natural reading" is **not** supported — police is index **3**. | **CONFIRMED** for the parse/apply numbering; **HIGH CONFIDENCE** that the runtime consumers index the array the same way (the multiplier store is written by the apply and is `1.0 + Amount/100`) |
| `0x4A34`–`0x4A47` damage scales | 9 `Damage_Resist` (`Source`: bullet, explosion, vehicle, fire, falling) | names §10.6's five types (which it took from "the executable's own 5-string type table") | CONFIRMED |
| `0x4A4C` "meaning OPEN" | 11 `Health` (`Modifier`) | a **health modifier** (player `+0x2770`) | HIGH CONFIDENCE (name + setter) |
| `0x4A58` "meaning OPEN" | 14 `Repair_Discount` (`Amount`) | the **vehicle-repair discount fraction**, clamped to [0, 1] | HIGH CONFIDENCE |
| `0x4A5C` respect-gain multiplier | 37 `Respect_Bonus_Modifier` | confirms §10.6 | CONFIRMED |
| **`0x4A60`** | **19 `Melee_Damage_Bonus` and 45 `Muscles`** — both call the setter that writes player `+0x1B70` | **This contradicts §10.6's "cash-gain multiplier (HIGH CONFIDENCE — inferred)".** The types that write the saved scalar are the two *melee* types; the cash type (38 `Cash_Bonus_Modifier`) writes a *different, unsaved* global (`0x012F5D00`) and is re-applied on load. The correlation §10.6 found (1.3 / 1.9 tracking `Cash_Bonus_*` slots 31/32) is real but is then a co-purchase pattern, or those slots' types are melee types despite their names — **the type of the `Cash_Bonus_n` unlockables is unreadable (base table)**. **Second argument (persistence rule):** §10.6 itself notes that only types whose re-apply byte is 0 need a saved scalar; type 19 has re-apply 0 (so its scalar must be saved), type 45 has 1, and the cash type 38 has 1 — a saved cash multiplier would be redundant, a saved melee multiplier is not. Recommendation: downgrade §10.6's label for `0x4A60` to **HYPOTHESIS — melee-damage multiplier (types 19/45)**. | **CONFIRMED** (setter identity, both types) / **HIGH CONFIDENCE** (melee reading) |
| `0x4A68` "meaning OPEN" | 23 `Firearm_Accuracy` (`accuracy_multiplier`) | the **firearm accuracy multiplier** | HIGH CONFIDENCE |
| `0x4A6C` byte "meaning OPEN" | 34 `Vehicle_Customization` | the **"vehicle customization unlocked" flag** (global `0x027B15F8`) | HIGH CONFIDENCE |
| `0x4A6E` weekly payments | 31 `Weekly_Payments` (`Amount`) | confirms §10.6 (player `+0x27AC`, `u16`) | CONFIRMED |
| `0x4A50`/`0x4A54` sprint | 8 `Sprint_Bonus` (`Amount`÷100; `≤ 0` ⇒ unlimited) | confirms §10.6 | CONFIRMED |

**[OPEN — desk review 2026-09-30: types 14 `Repair_Discount` (`0x4A58`) and 23 `Firearm_Accuracy` (`0x4A68`) are not in §14.1's real-table type list, which is itself 4 rows short; if neither type is used, these saved scalars never leave their defaults; to be settled against real data (the values of `0x4A58`/`0x4A68` over the 16 saves, and a full `Type` tally of the 312 rows).]**

**Review status (2026-09-30), §4.6: NEEDS-DATA: whether types 14/23 occur in real data — desk review (not re-derived from the executable).**

### 4.7 Validation **[CONFIRMED — empirical]**

`prog_validate.py` V2: 56 DLC rows found; **every top-level child is read by the row reader** (56/56, `_Editor` aside); `Type` shape and payload-child sets 56/56 each; `Category` ∈ the 10 names, `Auto_Unlock` ∈ the accepted spellings, `Is_DLC` ∈ {False, True, Sometimes}, `Framework` = owning DLC: 56/56 each; names unique and hash-collision-free 56/56; **the version-9 snapshots' DLC id block reproduces from those names: 784/784** (56 per file × 14 files), in file order in **14/14**; 22/22 of the base-block names of `spec-save-format.md` §10.6 that are checked reproduce under the same hash.

**The editor schema carried by the raw DLC files is an independent statement of the element tree, and it agrees (V2.14–V2.17).** Each raw DLC `*_unlockables.xtbl` ends with a `<TableDescription>` (the authoring tool's description of the table: element names, editor types, defaults, cross-references). Its **13 top-level elements** (`Name`, `Type`, `DisplayName`, `Description`, `Image_Source`, `Detailed_Description_Text`, `Event_Text`, `Priority`, `Category`, `Price`, `Auto_Unlock`, `Is_DLC`, `Framework`) are **exactly the row reader's set** (13/13); the `Type` element lists **63 type children** (identical in all three DLC files), of which **60 are among the loader's 61 type names, and for those 60 the schema's child-element names equal the sub-reader's element names in 60/60** — i.e. the whole payload decode of §4.4 is confirmed independently of the disassembly. The three schema-only type children are **`Killbane_Mask` (child `Mask`, referencing `customization_items.xtbl`), `Melee_Style` (child `Style`) and `Purchasable_Cribs` (no children)** — **the executable has no reader for them** (a row using one would be dropped as a type-less row, §4.1), so they are dead authoring types on this build; the loader's `Music` type (index 13) is the one type absent from the schema. The schema also gives the authoring cross-references used in §11.


### 4.8 Open items

~~The six leading DLC-block ids (slots 0–5) and the remaining ~227 of 312 base ids — names live in the parked base tables; which framework the six early ids belong to;~~ *(resolved: slots 0–5 are the six `patch_unlockables.xtbl` rows, §4.1; all 312 base ids are named, §14.1)* the exact return path of `FUN_0071E5E0` after the payload switch (returns true — HIGH CONFIDENCE — on every path that found a `Type` child); the payload semantics of the hash-only types beyond the resolvers named in §4.4. **[OPEN.]**

## 5. `respect_levels.xtbl` — the 50 respect levels

### 5.1 Where it lives **[CONFIRMED — disassembly]**

Loader `FUN_0060ED30` (no DLC variant found; the file name has one cross-reference). Storage: **50 level records of `0x64` bytes at `0x014B0338`** (a static vector constructor `0x00FF3BD0` builds 50 elements of `0x64` bytes with element constructor `FUN_0060ECF0`, which initialises the per-level unlock array to the invalid-id constant); level count `0x014B0320`; **cumulative respect total** `0x014B031C`. The loader stops storing at 50 (`< 0x32`) but keeps iterating; rows past 50 are ignored.

### 5.2 Element tree

```
<Table>
  <respect_levels>                       ← single element
    <levels>
      <respect_level>                    ← repeated, one per level (level 1 = first row)
        <Rank>          localisation key ← +0x00 (string handle)
        <Respect>       int              ← +0x04: respect points that level costs
        <Unlockables>
          <Unlockable>  text             ← an unlockable NAME (§4); repeated
        </Unlockables>
```

| Field | Reader | Destination | Notes |
|---|---|---|---|
| `Rank` | key → `FUN_0084A1B0` | record `+0x00` | name shown for the level |
| `Respect` | signed int, always-write | record `+0x04` | added into the running total |
| `Unlockable` (text) | CRC-32 (§1.3) → **looked up among the already-loaded unlockable records** (`FUN_0071CC90`); the id is kept only if the record exists | appended to the level's list: array pointer `+0x08`, capacity `+0x0C`, count `+0x10`, ids from `+0x14` | **Load order matters: `unlockables.xtbl` must have been loaded first**, else every name resolves to the invalid id and is dropped. An id is stored only while count < capacity (capacity = 20 ids per level — the element constructor lays the array at `+0x14` with 20 slots and the record ends at `+0x64`; HIGH CONFIDENCE, the capacity store itself was not read). |

**Cumulative total (`0x014B031C`)** = Σ `Respect` over all levels **minus the last level's `Respect`** (the loader subtracts the last record's value after the loop) — i.e. the respect needed to *reach* the final level. Helper accessors (`0x0060EB40`…`0x0060ECA0`): `FUN_0060EB40(n)` returns level *n*'s `Respect` and **`22000` (`0x55F0`) for `n ≥ 50`**; `FUN_0060EBB0(n)` = the sum of the first *n* levels' `Respect`; `FUN_0060EBF0(n)` re-applies level *n*'s unlockables (each id → the apply routine `FUN_0071CCF0` unless already applied) and shows the level-up message (hint id `0xA9`); `FUN_0060ECA0(id)` finds which level lists an unlockable id.

**Cross-check with a save.** The level field of `sr3save` (`0x4420`, `spec-save-format.md` §10.3) is an index into this array; saves hold levels 0…50 (`0x4420` 0 at new game, 50 maximum observed), consistent with the 50-record bound, and the respect fields `0x4418`/`0x441C` are the totals this table's `Respect` values accumulate into. **[HIGH CONFIDENCE — inferred; not exercised beyond the field identities.]**

### 5.3 Open items

Per-level `Respect` values and unlockable lists (base table unreadable) *[RESOLVED §14.5]*; the capacity constant `0x14` (read from the constructor's loop count, not its store); what `Rank`'s handle indexes beyond display. **[OPEN.]** **[OPEN — desk review 2026-09-30: §5.2 says "level 1 = first row" while saves hold levels 0…50 against 50 records and `FUN_0060EB40(n)` returns 22000 for `n ≥ 50`; whether saved level L indexes row L−1 or row L is not stated; to be settled against the executable (`FUN_0060EB40`, `FUN_0060EBB0`; capacity store in `FUN_0060ECF0`/`FUN_0060ED30`).]**

**Review status (2026-09-30), §5: NEEDS-EXE: level-to-row indexing and the capacity store (50 rows per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`"; `0x14 + 20·4 = 0x64`; Respect sum 238,875, §14.5) — desk review (not re-derived from the executable).**

## 6. Notoriety: `notoriety.xtbl`, `notoriety_levels.xtbl`, `notoriety_spawn.xtbl`

Three tables define the wanted-level system: the *points* an action is worth (`notoriety.xtbl`), the *levels'* limits and timers (`notoriety_levels.xtbl`), and *what spawns* at each level (`notoriety_spawn.xtbl`). **[CONFIRMED — disassembly for all three loaders.]**

### 6.1 `notoriety.xtbl` — points per crime (`FUN_00604B50` → `FUN_006049A0`)

```
<Table>
  <Entry>                                ← repeated
    <Name>         text                  ← one of 30 activity names (case-insensitive); others are ignored
    <Data>
      <Activity>
        <Delay>          int             ← shared by both sub-blocks
        <Gang>   <Points/> <Min_Level/> <Max_Level/> <Check_Detection>true|…</Check_Detection> </Gang>
        <Police> <Points/> <Min_Level/> <Max_Level/> <Check_Detection>true|…</Check_Detection> </Police>
      </Activity>
    </Data>
  </Entry>
```

The 30 names (table at `0x012ED0B8`, matched by `stricmp`; a name outside the list writes nothing): `armored truck assault`, `burglary`, `carjacking`, `car assault`, `civilian assault`, `civilian kill`, `civilian shooting`, `civilian squirting`, `drug use`, `firearm discharge`, `gang alert`, `gang assault`, `gang kill`, `gang shooting`, `generic gang`, `generic police`, `govt car destroy`, `govt car theft`, `govt heli theft`, `helicopter destroy`, `mover dislodge`, `public nudity`, `police alert`, `police assault`, `police kill`, `police shooting`, `robbery`, `atm extortion`, `vandalism`, `enter owned store`. Activity index *i* (0…29) owns a `0x2C`-byte record at **`0x014A4620 + 0x2C·i`**: `+0x00` gang `Points`, `+0x04` `Delay`, `+0x08` gang `Min_Level`, `+0x0C` gang `Max_Level`, `+0x10` gang `Check_Detection` (byte; `true` when the text equals `true`, case-insensitive; the *only* accepted spelling), then `+0x14` police `Points`, `+0x18` `Delay` (a second copy of the shared value), `+0x1C` police `Min_Level`, `+0x20` police `Max_Level`, `+0x24` police `Check_Detection`. All numbers are signed ints read with the "always" reader (an absent element is unspecified — §1.3 caveat; treat as 0). No unit conversion. **[HIGH CONFIDENCE for the meaning of the sub-block names — `Gang`/`Police` are the two notoriety tracks; the per-field meaning (score awarded, level window in which it applies, whether the deed must be witnessed) is by name.]** **[OPEN — desk review 2026-09-30: (a) "the *only* accepted spelling" for `Check_Detection` conflicts with the generic bool reader of §1.3 (`true`/`yes`/`false`/`no`); (b) the record fields end at `+0x24` (a byte), so `+0x25`…`+0x2B` of the `0x2C` stride are undescribed; to be settled against the executable (`FUN_006049A0`). The stride itself is consistent: `0x014A4620 + 30·0x2C = 0x014A4B48`, the brute-spawn array of §6.2.]**

**Review status (2026-09-30), §6.1: NEEDS-EXE: `Check_Detection` reader and the record tail (30/30 names matched, team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") — desk review (not re-derived from the executable).**

### 6.2 `notoriety_levels.xtbl` — level limits, decay and spawn timers (`FUN_00604C10`)

All target arrays are first set to `-1`. The file holds **two `NotorietyLevels` elements** (set 0 = first, set 1 = second; the loop is `while set < 2`), each with:

```
<NotorietyLevels>
  <NotorietyLevelsData>
    <NotorietyLevelElement>              ← repeated, indexed by NotorietyLevel
      <NotorietyLevel/>           int    ← index within the set (used unchecked, see below)
      <NotorietyLevelLimit/>      int    ← stored only when NotorietyLevel > 0
      <NotorietyDecayRate/>       int
      <NotorietyFirstDecay/>      int    ← × 1000 (seconds → ms)
      <RoadblockSpawnTimeMin/> <RoadblockSpawnTimeMax/>   int, × 1000 each
      <BruteSpawnTimeMin/>     <BruteSpawnTimeMax/>       int, × 1000 each
```

| Target | Address and index | Value |
|---|---|---|
| level limit | `0x014A45F0 + 4·(5·set + level)` | `NotorietyLevelLimit` |
| decay rate | `0x014A45C4 + 4·(6·set + level)` | `NotorietyDecayRate` |
| first-decay delay | `0x014A4594 + 4·(6·set + level)` | `NotorietyFirstDecay × 1000` |
| roadblock spawn min / max | `0x014A4BA8 + 8·(6·set + level)` / `+4` | `× 1000` each |
| brute spawn min / max | `0x014A4B48 + 8·(6·set + level)` / `+4` | `× 1000` each |

Each set has room for **6 levels (0…5)** in the decay/spawn arrays and **5 (1…5)** in the limit array; `NotorietyLevel` is used as an unchecked array index, so a value > 5 would spill into the other set. **Which set is police and which is gang is positional (first/second element) — the loader alone does not say; OPEN** (the debug overlay's police/gang decay split noted in `spec-save-format.md` §10.6 is the natural consumer). *(Note 2026-09-30: the "limit 0" shown for level 0 in §14.6's table is the XML value; since the limit is stored only for `NotorietyLevel > 0`, the stored level-0 limit stays `-1`. This is also what keeps set 1 level 0 (`0x014A45F0 + 4·5`) from overwriting set 0 level 5.)*

**Review status (2026-09-30), §6.2: DESK-PASS, text fixes applied (all five arrays tile: `0x014A4594 + 12·4 = 0x014A45C4`, `+ 12·4 = 0x014A45F0`, `0x014A4B48 + 12·8 = 0x014A4BA8`; level-0 limit note added) — desk review (not re-derived from the executable).**

### 6.3 `notoriety_spawn.xtbl` — spawn groups per wanted level (`FUN_008EEC00` → `FUN_008ED480`)

`FUN_008EEC00` fills **24 named runtime structures**; for each name it finds the table row whose `Name` equals the name (case-sensitive compare, `FUN_008EC4A0`) and parses it into that structure. The 24 names, in table order (`0x013080F8`; destination structures listed at `0x01308098`): `police`, `police_motorcycle`, `police_heli`, `police_blackhawk`, `police_attack_heli`, `police_waverunner`, `police_speedboat`, `police_riot`, `police_tank`, `police_ng`, `luchadores`, `deckers`, `morningstar`, `morningstar_heli`, `stag`, `stag_riot`, `stag_heli`, `stag_attack_heli`, `stag_tank`, `whored_one`, `whored_two`, `survival_bikers`, `survival_mascots`, `survival_bums`. **The row lookup asks the element returned by the table-open helper for children named `Table` (string at `0x0113A890`)** — for every other loader that element is the outer `<Table>` and rows are its named children, so in this file each spawn definition is a child element literally called `Table` (HIGH CONFIDENCE from the code; no sample — the base table is unreadable). *[The `Table` row element is confirmed on real data, §14.6. The real file has **25** rows, not 24: the 25th, `stag_speedboat`, matches none of these names (team-b/HANDOFF.md "### 9.81 `sr3tables_progression`": "24/25 matching the spec's list"). The pointer list `0x01308098 + 24·4 = 0x013080F8` (the name table) leaves room for exactly 24 destinations, so that row is most likely never loaded.]* **[OPEN — desk review 2026-09-30: the loop bound of `FUN_008EEC00` (24 vs 25); to be settled against the executable.]**

~~**Element tree as first drawn (flat repeated siblings):**~~ **SUPERSEDED by §14.6 (2026-09-30) — do not implement from the block below.** `level_info`, `group_info` and `group_details` are not repeated siblings of `Name`: each is a single wrapper element whose children carry the same tag name and are the records. The corrected tree follows the old block.

```
~~ SUPERSEDED (§14.6): flat shape, kept for history ~~
<Table>                                  ← row (found by <Name>)
  <Name>          group name (one of the 24)
  <level_info>                           ← repeated → one 0x30-byte level record each
     <level/> <min_spawn_time/> <max_spawn_time/> <veh_min_spawn_time/> <veh_max_spawn_time/>   int (times × 1000)
     <max_vehicle_occupants/> <max_vehicles/> <npc_cap/> <specialist_cap/> <brute_cap/>          int
  <group_info>                           ← repeated; consecutive rows with the same <level/> form that level's group list
     <level/> <chance/>(float) <tag_name/> <vehicle_name/> <variant_name/>       → 0x20-byte record
  <group_details>                        ← repeated; consecutive rows with the same <tag_name/> form one group
     <tag_name/> <seat_name/>  Default | Driver | Passenger 1 … Passenger 7     → -1, 0, 1 … 7
     <npc_name/> <melee_brute_seat/> <weapons_brute_seat/> <rollerbladers_seat/> <outside_seat/>   (bools → 4 flag bits)
  <override_group>  text                 ← optional: one of the 24 names → pointer stored at struct +0x450
  <spawn_flags>                          ← <Flag> list: Spawn on Land | Spawn on Water | Spawn vs Bikes Only |
                                            Spawn Indoors Only | Attack Helicopter | Force No Ram | Active With Stag
</Table>
```

**⚠ Corrected §14.6:** `level_info`/`group_info`/`group_details` are same-name wrapper containers (each holding repeated children of the same name), not the flat repeated siblings drawn above; and the real file has 25 groups (the 25th is `stag_speedboat`), not 24.

**Corrected element tree (2026-09-30, from §14.6; leaf names, types and conversions unchanged from the superseded block):**

```
<Table>                                  ← row (found by <Name>)
  <Name>          group name
  <level_info>                           ← ONE wrapper
     <level_info>                        ← repeated → one 0x30-byte level record each
        <level/> <min_spawn_time/> … <brute_cap/>                     (leaves as in the superseded block)
     </level_info>
  </level_info>
  <group_info>                           ← ONE wrapper
     <group_info> <level/> <chance/> <tag_name/> <vehicle_name/> <variant_name/> </group_info>   ← repeated
  </group_info>
  <group_details>                        ← ONE wrapper
     <group_details> <tag_name/> <seat_name/> <npc_name/> … <outside_seat/> </group_details>     ← repeated
  </group_details>
  <override_group>  text                 ← optional
  <spawn_flags> <Flag/>… </spawn_flags>
</Table>
```

Where the layout below says `count(level_info)`, read it as the number of inner `level_info` records (§1.3's `FUN_00DC5150` counts the children of the wrapper).

Destination layout (structure-relative): level array pointer **`+0x3E8`** (`count(level_info) × 0x30` bytes), first level number `+0x3E0`, last level `+0x3E4` (= first + count − 1); the seven flags as bytes **`+0x425…+0x42B`** in the order listed; override-group pointer `+0x450`; `+0x3D4/+0x3D8/+0x3DC` = `-1`. **Level record (`0x30`)**: `+0x00` level, `+0x04` min spawn ms, `+0x08` max spawn ms, `+0x0C` vehicle min ms, `+0x10` vehicle max ms, `+0x14` max occupants, `+0x18` max vehicles, `+0x1C` NPC cap, `+0x20` specialist cap, `+0x24` brute cap, `+0x28` pointer to the level's group array, `+0x2C` group count. **Group record (`0x20`)**: `+0x00` chance, `+0x04` vehicle-table slot (`u16`, from `vehicle_name` via the name-hash lookup, `spec-vehicle-data.md` §7.1; `0xFFFF` = unknown vehicle) and `+0x06` variant index (from `variant_name`), `+0x08` `-1`, `+0x0C` pointer to the matching detail group (by `tag_name`, case-insensitive), `+0x10/+0x14/+0x18` `vehicle_name` / `variant_name` / `tag_name` string pointers. **Detail record (`0x14`)**: `+0x00` NPC-name pointer, `+0x08` seat index, `+0x10` low nibble = the four seat-role flags. After the arrays are read, each `group_info` vehicle is resolved against the vehicle table; **if a vehicle's table entry is not live, or its DLC-ownership check (`FUN_00BC1250` on the entry's `+0x1C` gate byte) fails, the parse function returns at that point** — the remaining vehicles, the `override_group` and the `spawn_flags` of that structure are then never processed (HIGH CONFIDENCE from the control flow; not exercised).

**Level indexing.** `level` values in `level_info` must be consecutive starting at the first row's value; the loader records `+0x3E0`/`+0x3E4` as that range and treats `group_info` rows in file order (same `level` ⇒ same list). **[CONFIRMED — disassembly.]**

**Review status (2026-09-30), §6.3: NEEDS-EXE: loop bound 24 vs the 25 real rows; flat tree struck and replaced in place by §14.6's nested shape (Team B's reader follows the flat tree — reported to Team B) — desk review (not re-derived from the executable).**

### 6.4 Validation and open items

No DLC or save sample exists for these three tables (`notoriety.xtbl`, `notoriety_levels.xtbl`, `notoriety_spawn.xtbl` are base-only). **The multiplier index mapping of §4.6 (luchadores 0, deckers 1, morningstar 2, police 3) comes from the same notoriety category resolver and is independent of these files.** Open: which `NotorietyLevels` set is police vs gang; the meaning of the `Check_Detection` flag beyond its name; the 24 destination structures' remaining fields; ~~the `Table`-nesting of `notoriety_spawn.xtbl` (needs one real sample)~~ *(resolved §14.6)*. **[OPEN.]**

## 7. `difficulty_levels.xtbl` — dynamic difficulty

### 7.1 Where it lives **[CONFIRMED — disassembly]**

Loader `FUN_005F4710`. It fills **two parallel sets** (initial and second `Dynamic_Difficulty` element of the file) of **three `0x28`-byte difficulty-level records** — set A at **`0x014A1130`**, set B at **`0x014A10B8`** (each `0x78` bytes, zeroed first) — and two parallel arrays of **four `0x38`-byte "rank occurrence" records** at **`0x014A11A8`** (A) and **`0x014A1288`** (B). If the file has no `Dynamic_Difficulty` element both sets stay zero. The difficulty index saved at `sr3save` `0x2B64` (0…2, `spec-save-format.md` §6.4) selects one of the three level records.

### 7.2 Element tree

```
<Table>
  <Dynamic_Difficulty>                   ← repeated; the FIRST fills set A, the SECOND set B (max 2)
    <Difficulty_Levels>
      <Difficulty_Level>                 ← repeated, at most 3 (index = position: 0, 1, 2)
        <Damage_Received_Mult/> <Health_Regen_Wait_Mult/> <Health_Regen_Speed_Mult/> <Damage_Dealt_Mult/>
        <Notoriety_Decay_Mult/> <Homie_Revive_Timer_Mult/> <Vehicle_Damage_Received_Mult/>
        <Brute_Health_Scalar/> <Friendly_Health_Scalar/>     float, "always" reader (absent: treat as 0.0, §1.3)
        <Player_Ram_Delay/>                                  float seconds
    <Rank_Occurrence_Percentages>
      <Rank_Occurrence_Element>          ← repeated, at most 4
        <Mission_Name/>   text (≤ 31 characters)
        <Rank1/> <Rank2/> <Rank3/> <Rank4/>                  float (default 0.25 each)
```

| Field | Destination (level record `+`) | Conversion |
|---|---|---|
| `Damage_Received_Mult` | `+0x00` | — |
| `Health_Regen_Wait_Mult` | `+0x04` | — |
| `Health_Regen_Speed_Mult` | `+0x08` | — |
| `Damage_Dealt_Mult` | `+0x0C` | — |
| `Notoriety_Decay_Mult` | `+0x10` | **stored as `1.0 ÷ value`** (reciprocal) |
| `Homie_Revive_Timer_Mult` | `+0x14` | — |
| `Vehicle_Damage_Received_Mult` | `+0x18` | — |
| `Brute_Health_Scalar` | `+0x1C` | — |
| `Friendly_Health_Scalar` | `+0x20` | — |
| `Player_Ram_Delay` | `+0x24` | **× 1000, rounded to an integer (ms)** |

**Rank-occurrence record (`0x38`)**: `+0x00` `Mission_Name` (`char[0x20]`, written only if the element has text), `+0x20`/`+0x24` two dwords preset to 0, `+0x28`…`+0x34` the four `RankN` floats. The four records of each array are **pre-set to 0.25 per rank** before parsing, but the reader used for `RankN` is the always-write float reader, so **inside a `Rank_Occurrence_Element` an absent `RankN` is not guaranteed to keep the 0.25 preset** (unspecified per the §1.3 caveat; treat as 0.0); the preset is reliable only for records the file does not list. The `Difficulty_Level` loop runs at most 3 times and the rank loop at most 4; a missing second `Dynamic_Difficulty` element makes set B a byte copy of set A's *level records only* (`0x78` bytes; the rank arrays are not copied).

### 7.3 Open items

The meaning of set A vs set B (positional: first vs second `Dynamic_Difficulty`; likely single-player vs co-op — HYPOTHESIS, the consumer was not traced); the per-level values (unreadable) *[RESOLVED §14.7]*; whether `Mission_Name` is matched against mission names. **[OPEN.]**

**Review status (2026-09-30), §7: DESK-PASS (blocks chain: `0x014A10B8 + 0x78 = 0x014A1130`, `+ 0x78 = 0x014A11A8`, `+ 0xE0 = 0x014A1288`, `+ 0xE0 = 0x014A1368` = `panic_fall_velocity`, §10.2) — desk review (not re-derived from the executable).**

## 8. `cheats.xtbl` — the cheat table

### 8.1 Where it lives **[CONFIRMED — disassembly]**

Loader `FUN_006FBB50` calls the generic cheat-table reader **`FUN_006FBAB0(record array `0x014F7228`, count `0x014F71D8`, capacity 200, file name)`**, which zeroes `200 × 0x84` bytes, opens the file and parses each `<Cheats>` element with `FUN_006FB8D0`, stopping at 200. The helper takes the file name as a parameter, so DLC cheat tables (the save's cheat ids include `dlc_*` codes, `spec-save-format.md` §9.5) reuse it; only the base file name has a literal.

### 8.2 Element tree

```
<Table>
  <Cheats>                               ← repeated (row element is literally "Cheats"; ≤ 200)
    <Name>                  text ≤ 32     ← record +0x05
    <UnlockString>          text          ← the typed code; record +0x28 = its CRC-32 (§1.3)
    <DisplayName>           text ≤ 31     ← record +0x2C  (copied verbatim, not a localisation key lookup)
    <Cheat_Description>     text ≤ 31     ← record +0x4C
    <Interface_Category>    text          ← record +0x78: index (0…4) of the category whose stored hash equals CRC-32(text), else -1
    <Dont_Flag_As_Cheating> bool          ← record +0x04 (always-write)
    <Is_DLC>                bool          ← record +0x6C: 0x00 if true, else 0xFF (the DLC-gate convention of §4.3)
    <Type>                                ← ONE of the children below (tested in this order; first present wins)
        <AI>               <Action>Evil Cars | Evil Cars 2 | Ped War | Drunk</Action>
      | <Gameplay_Physics> <Action>… (27 names, below; was "25" — recounted 2026-09-30)</Action>
      | <Time>             <Action> <Set_Hour><Hour>n</Hour></Set_Hour> | <Stop_Time/> | <TOD_Cycle/> </Action>
      | <Vehicles>         <Action> <Drop><Vehicle_Name/><Vehicle_Variant/></Drop> | <Infinite_Mass/> | <Repair_Vehicle/> |
                                    <Player_Vehicle_No_Damage/> | <Player_Vehicle_Smash/> </Action>
      | <Weapon>           <Action> <Give><Weapon>name</Weapon><Ammo>n</Ammo></Give> | <Infinite_Ammo/> | <All_Weapons/> </Action>
      | <Weather>          <Action> <Clear/> | <Set_To><Conditions>text</Conditions></Set_To> | <Wrath_of_God/> </Action>
    <_Editor>…</_Editor>                  ← editor-only, not read (added 2026-09-30: present on every real row, team-b/HANDOFF.md "### 9.81")
```

**`Gameplay_Physics` actions (exact `Action` text, case-insensitive):** `Instant Cash (+$100000)`, `Instant Respect (+100000)`, `God Mode`, `Golden Gun`, `Remove Police Notoriety`, `Remove Gang Notoriety`, `Add Police Notoriety`, `Add Gang Notoriety`, `Infinite Sprint`, `Max Health`, `Player Pratfalls`, `Super Beer Muscles`, `Giant Player`, `Tiny Player`, `Bushwick Bill`, `Raining Peds`, `Never Die`, `Super Saints`, `Unlimited Clip`, `Super Explosions`, `Low Gravity`, `Elevator to Heaven`, `Hide Hud`, `Bloody Mess`, `Zombie Peds`, `Mascot Peds`, `Pimps and Hos Peds`. **An `Action` string not in the list leaves the record with no handlers** (a silent no-op cheat). The list confirms that the shipped cheat vocabulary is engine-implemented behaviours selected by string, not data.

### 8.3 The cheat record (`0x84` bytes) **[CONFIRMED — disassembly]**

| Offset | Content |
|---|---|
| `+0x00` | byte: **toggle** cheat (1 = has an *off* handler; 0 = one-shot) |
| `+0x02` | byte: 1 for cheats flagged in the reader (God Mode, Infinite Sprint, Instant Cash/Respect, Max Health, Player Pratfalls, Super Beer Muscles, Never Die, Unlimited Clip, Evil Cars…; **meaning of the flag not traced** — possibly "counts as using cheats"; OPEN) |
| `+0x03` | byte: 1 for `Elevator to Heaven`, `Hide Hud`, `Bloody Mess`, `Zombie Peds`, `Mascot Peds`, `Pimps and Hos Peds` (meaning OPEN) |
| `+0x04` | `Dont_Flag_As_Cheating` |
| `+0x05`–`+0x25` | `Name` (`char[0x21]`) |
| `+0x28` | **`UnlockString` hash** — the id the save file stores for an active cheat (`spec-save-format.md` §9.5, entry 14) |
| `+0x2C`–`+0x4B` | `DisplayName` (`char[0x20]`) |
| `+0x4C`–`+0x6B` | `Cheat_Description` (`char[0x20]`) |
| `+0x6C` | DLC-gate byte |
| `+0x70` / `+0x74` | **on-handler / off-handler** code pointers (off = 0 for one-shot cheats) |
| `+0x78` | interface category index (-1 default) |
| `+0x7C`… | per-action payload: `Set_Hour` → int `Hour` at `+0x7C`; `Drop` → vehicle slot `u16` at `+0x7C` (from `Vehicle_Name` via the vehicle name-hash lookup) and variant index `u16` at `+0x7E` (`0xFFFF` if `Vehicle_Variant` absent); `Give` → weapon-name pointer at `+0x7C`, `Ammo` int at `+0x80`; `Set_To` → conditions-text pointer at `+0x7C` |

**Interface categories.** The five category hashes are a static array at `0x014F7214` that is **zero in the image** and filled at run time (by the cheat menu code); the loader compares `CRC-32(Interface_Category)` with them — until they are filled every row gets `-1`. **[HIGH CONFIDENCE — the category *names* are therefore not recoverable from this loader.]**

**Cross-check with the save.** `spec-save-format.md` §9.5 resolved 8 of 23 cheat ids to `dlc_*` codes and `hohoho` by the same CRC: a cheat's saved id = CRC-32(lower(`UnlockString`)) — confirmed here at the loader (`+0x28`). No cheat table is raw-readable (the DLC archives carry none). *[Superseded: the base table is read in §14.4.]*

### 8.4 Open items

The flag bytes `+0x02`/`+0x03`; the category names *(names recovered §14.4; the index-to-name mapping is still open)*; ~~whether DLC cheat tables exist~~ *(RESOLVED §14.4: none; DLC cheats are 7 `Is_DLC` rows of the base table)*; the semantics of individual actions beyond their names. **[OPEN.]**

**Review status (2026-09-30), §8: DESK-PASS, text fixes applied (`_Editor` added; record tiles to `0x84`; `0x014F7214 + 5·4 = 0x014F7228`; 97 rows, 97/97 type-matched and 0 `Action` mismatches per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`"; the `Time` type and its `Set_Hour`/`Stop_Time`/`TOD_Cycle` names are unused in real data, so disassembly-only) — desk review (not re-derived from the executable).**

## 9. `collectibles.xtbl` — collectible awards

### 9.1 Where it lives **[CONFIRMED — disassembly]**

Loader `FUN_005EA0E0`. **Four collectible types**, each a `0x74`-byte record at **`0x0149FC00 + 0x74·type`**. The name → type table is at `0x01125FD0` (`{name char*, type index, statistic id}`, 12 bytes × 4): `Drug Package` 0 → stat `0x18` (24 "drug packages found") · `Money Pallet` 1 → `0x19` · `Sex Doll` 2 → `0x1A` · `Photo Op` 3 → `0x1B` — the same four groups as entry 1's `0x3FC` collectible table (4 groups × 20, stats 24–27; `spec-save-format.md` §10.3). The third column is not read by this loader (used by the award code).

### 9.2 Element tree

```
<Table>
  <collectible>                          ← repeated; row element is lower-case
    <Name>              text             ← must equal (case-insensitive) one of the four names; other rows are skipped
    <Title_Message/> <Subtitle_Message/>  ← localisation keys → handles at record +0x00 / +0x04 (only if present)
    <Respect_Award/>    int              ← +0x08 (preset 0; write-only-if-present)
    <Cash_Award/>       float            ← +0x0C (preset 0; write-only-if-present)
    <Threshold_Rewards>
      <Threshold_Reward>                 ← repeated, at most 6 kept
        <Threshold/>      int            ← record dropped unless > 0
        <Respect_Award/>  int
        <Cash_Award/>     float
        <Unlockable/>     text           ← an unlockable NAME; stored as its CRC-32 (no existence check here)
```

Threshold record *k* (0 ≤ k < 6) is at record `+0x10·(k+1)`: `+0x00` `Threshold`, `+0x04` `Respect_Award`, `+0x08` `Cash_Award`, `+0x0C` unlockable hash; the count is at record **`+0x70`** (set to 0 first). The loop tests `count > 5` *after* each row, so a seventh `Threshold_Reward` is never read. **[CONFIRMED — disassembly.]** A threshold's unlockable id is the same CRC key as the unlockable records of §4 — a threshold reward grants that unlockable when the collected count reaches `Threshold` (HIGH CONFIDENCE — the consumer was not traced).

### 9.3 Open items

~~Base values (unreadable);~~ *(RESOLVED §14.8)* the award routine's use of the threshold list. **[OPEN.]**

**Review status (2026-09-30), §9: DESK-PASS (`0x10 + 6·0x10 = 0x70`, count at `+0x70`, record `0x74`; 4 rows × 4 thresholds, §14.8; 4/4 names per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") — desk review (not re-derived from the executable).**

## 10. The smaller rule tables

The remaining tables of the group, each with the element tree, destinations and defaults read from its loader. All **[CONFIRMED — disassembly]** unless marked. Where a value or a runtime consumer is out of reach the gap is stated.

### 10.1 `store_discounts.xtbl` (`FUN_0080E930`)

```
<Table>
  <StoreDiscounts>                       ← repeated; row element = "StoreDiscounts"; one discount definition
    <Name>              text             ← record hash = CRC-32(lower(Name))
    <Discounts>
      <DiscountElement>                  ← repeated
        <DiscountName/>   text           ← CRC-32 of the text
        <Amount/>         float          ← write-only-if-present, default 0
        <RadioEvent/>     text           ← optional; CRC-32, default the invalid-id constant
        <Hours/>          u32            ← write-only-if-present, default 1
        <Triggered/>      float          ← write-only-if-present, default 0.1
    <_Editor>…</_Editor>                 ← editor-only, not read (added 2026-09-30: present on every real row, team-b/HANDOFF.md "### 9.81")
```

Discount record *i*: hash at **`0x022CD108 + 0x30·i`**, a zero dword at `+0x04` after it (`0x022CD10C`), element-count **byte** at `0x022CD111 + 0x30·i`, element-array pointer at `0x022CD114 + 0x30·i` (allocated `count × 0x18`, zeroed); discount count `0x022CD0E0`; total element count `0x022CD05C`. **Element (`0x18` bytes)**: `+0x00` `DiscountName` hash, `+0x04` `Amount`, `+0x08` `RadioEvent` hash, `+0x0C` `Hours`, `+0x10` `Triggered`, `+0x14` zero. The element count is a **byte**, so at most 255 elements per discount. `DiscountName` is a hash in the same **shop-name** namespace that the `Discount` unlockable type (§4.4 type 4) uses for its `name`/`name_2…name_8` (the authoring schema of that type references `shop_names.xtbl`, `Shop_Names.Name`; HIGH CONFIDENCE that `DiscountName` uses the same namespace — the store-discount table's own authoring schema is not in any raw sample). **Cross-check with the save (`spec-save-format.md` §12.5):** the save's discount block has **31 defined discounts** (`0xFFFFFFFF` prefix exactly 31 long, 16/16) and per-discount arrays of 40 slots — the runtime state is laid out *per discount index*, i.e. this table's row order; the "current amount" array's only non-zero entry (index 7, exactly `0.5`) is therefore discount row 7 of the base file. **[HIGH CONFIDENCE — the save block's per-index meaning is inferred from this row order; not re-derived here.]** **[OPEN — desk review 2026-09-30: the capacity of the discount-record array is not stated (the save has 40-slot arrays; 31 real rows), and the record layout (count byte at `+0x09`, pointer at `+0x0C` of a `0x30` stride) has an unexplained `+0x08` byte and tail; to be settled against the executable (`FUN_0080E930`).]**

**Review status (2026-09-30), §10.1: NEEDS-EXE: record capacity and layout (31 rows per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`" and §14.8; element record `0x18` tiles) — desk review (not re-derived from the executable).**

### 10.2 `gameplay_constants.xtbl` (`FUN_005F5B50` and 20 sub-readers)

One element, **`Gameplay_Constants`**, under `Table`, holding named sections; every leaf is written into a fixed global block **`0x014A0F50`…`0x014A1660`** (all read with the "always" readers: an absent leaf is unspecified — §1.3 caveat — and should be treated as `0`). Types: **F** = float, **I** = signed int, **U** = u32, **V3** = vec3 (`X`/`Y`/`Z`), **C** = colour (12-byte reader `FUN_00DAD0A0`).

| Section (child of `Gameplay_Constants`) | Leaves (type) → destination | Notes |
|---|---|---|
| *(top level)* | `panic_fall_velocity` F `0x014A1368`; `FriendlyFirePercentage` F `0x014A136C` (**÷ 100**) | |
| `Death_and_busted` | `respawn_delay_ms` I `0x014A1370` | |
| `Fat_bones` | `Fat_Bones_Arm` F `0x014A1378`, `Fat_Bones_Leg` F `0x014A1374` (both **× 0.1**) | |
| `Fire` | `Max_Player_Burn_Time_Ms` `0x014A137C`, `Max_npc_burn_time_ms` `1380`, `Max_corpse_burn_time_ms` `1384` (I); `MP_fire_damage_per_second` F `1388`; `MP_max_player_burn_time_ms` `138C`, `MP_max_npc_burn_time_ms` `1390`, `MP_max_corpse_burn_time_ms` `1394` (I) | |
| `Dripping_Wet` | `Max_Player_Drip_Time_ms` `1398`, `Max_NPC_Drip_Time_ms` `139C`, `Max_Corpse_Drip_Time_ms` `13A0`, `Multiplayer_Max_Player_Drip_Time_ms` `13A4`, `Multiplayer_Max_NPC_Drip_Time_ms` `13A8`, `Multiplayer_Max_Corpse_Drip_Time_ms` `13AC` (I) | |
| `sticky_fire` | `conic_spread_threshold` `13B0`, `conic_stretch_factor` `13B4`, `circular_stretch_factor` `13B8`, `conic_arc` `13BC` (F); `burn_radii` → `large` `13C0`, `small_1` `13C4`, `small_2` `13C8` (F) | |
| `player_health` | `restore_per_sec_sp` `13CC`, `restore_per_sec_vampire_sp` `13D0`, `restore_max_pct_sp` `13D4`, `restore_per_sec_mp` `13D8`, `restore_per_sec_vehicle_mp` `13DC`, `restore_max_pct_mp` `13E0` (F); `restore_wait_time_ms` `13E4`, `restore_wait_time_ms_mp` `13E8`, `full_restore_time_ms` `13EC` (I) | the health-regen parameters |
| `Object_Glow_Colors` | `Weapons` C `13F0`, `Cash` C `13FC`, `Drugs` C `1408` | |
| `Ho_Pimp_AI` | `Slap_Chance` `1414`, `Customer_Sell_Chance` `1418`, `Go_Customer_Chance` `141C` (F) | |
| `ragdoll_damage_factors` | `global` `1420`, `normal` `1424`, `sliding` `1428`, `head` `142C`, `upper_body` `1430`, `body` `1434`, `upper_arm` `1438`, `lower_arm` `143C`, `upper_leg` `1440`, `lower_leg` `1444`, `Damage_Against_Mover_Multiplier` `1448`, `Damage_Against_Mover_Minimum_HP` `144C` (F) | |
| `Melee_attack` | `Melee_attack_speeds` → repeated `Melee_attack_speed_element`: `Num_attackers` (index), `Attack_delay_min` `0x014A1448 + 8·n`, `Attack_delay_max` `0x014A144C + 8·n`, `Sword_Attack_Delay_Min` `0x014A1460 + 8·n`, `Sword_Attack_Delay_Max` `0x014A1464 + 8·n` (I; **`n` is unchecked — the layout fits exactly `n` ∈ {1, 2, 3}: `n` = 0 would overwrite `Damage_Against_Mover_*` and `n` ≥ 4 `Gun_melee_attack_delay`**); `Gun_melee_attack_delay` U `1480`; `Finisher_Camera_Chance` F `1488`; `Wieldable_Prop_Player_Damage_Mult` F `148C`; `Power_Attack_Angle` I (degrees) → `1484` = **cos(angle × 0.017453 × 0.5)** (the half-angle cosine) | |
| `Firearm_attack` | `Min_spread_multipliers` → repeated `Min_spread_multiplier_element`: `Attacker_advantage` I, `Min_spread_multiplier` F → **8-byte entries from `0x014A1490`, unbounded (room for 4 before `dual_wield_multiplier`)**; `dual_wield_multiplier` `14B0`, `dual_wield_multiplier_online` `14B4` (F) | |
| `ControlPatterns` | `OrthogonalArc` U `14B8`, `CenteredThreshold` F `14BC` | |
| `TauntReactions` | `ResponseFlee` `14C0`, `ResponseTaunt` `14C4`, `ResponseAttack` `14C8` (F) | |
| `Bust_Offsets` | `Bust_Face_Down_Left` V3 `14CC`, `Bust_Face_Down_Right` `14D8`, `Bust_Face_Up_Left` `14E4`, `Bust_Face_Up_Right` `14F0`, `Bust_Standing` `14FC` | |
| `Helicopter` | `Draft` → `height` `1508`, `radius` `150C`, `force_x` `1510`, `dislodge_damage_x` `1514`, `ped_flee_radius_start` `1518`, `ped_flee_radius_stop` `151C` (F) | |
| `Cribs` | `Money_Storage` → repeated `Money_Storage_Element`: `Number_of_Cribs` (index *n*), `Max_Stash` I → **`0x014A0F50 + 4·n`** | the crib stash cap by crib count (`spec-save-format.md` §10.5's "capped by owned-crib count") |
| `Combat_AI` | `Gun` → `Reposition_Min` `1524`, `Reposition_Max` `1528`, `Cant_Fire_Reposition_Min` `152C`, `Cant_Fire_Reposition_Max` `1530` (U); `Pepperspray` **[OPEN — desk review 2026-09-30: parent of `Pepperspray` — this row lists it beside `Gun` (a child of `Combat_AI`), §14.9 writes `Combat_AI` → `Gun` → `Pepperspray` (a child of `Gun`); neither section says it corrects the other; to be settled against the executable (`FUN_005F5B50` sub-reader) or the real file.]** → `Spray_Min` `1534` and **`Spray_Min` again into `1538`** (**the loader reads the same element name twice — a `Spray_Max` element is never read; both dwords always hold `Spray_Min`**); `PepperSprayMinUsageDelay` `153C`, `StunGunMinUsageDelay` `1540` (U); `Back_Away_Min_Dist` `1544`, `Back_Away_Max_Dist` `1548`, `Back_Away_Abs_Min_Dist` `154C` (F); `Gunfire_Evade` → `Cower_Flee_Chance` F `1558`, `Cower_Flee_Max_Rank` I `1554`; `Bust` → `Bust_HP_Pcnt` F `155C` | |
| `Vehicle_Evade_AI` | `Evade_Chance` `1560`, `Dive_Chance` `1564`, `Threat_Chance` `1568`, `Threat_Chance_After_Dive` `156C` (F) | |
| `Idle_AI` | `Crime_scene` → `Get_callers_delay` `1570`, `Dont_call_delay` `1574`, `Police_response_delay` `1578`, `Observe_interval` `157C`, `Add_cop_interval` `1580` (I), `Radius` F `1584`; `Drunk` → `drunk_knockdown_chance` `1588`, `drunk_action_delay_min` `158C`, `drunk_action_delay_max` `1590` (I), `Drunk_action_type` → `drunk_taunt_pct` `1594`, `drunk_stumble_pct` `1598`, `drunk_vomit_pct` `159C`, `drunk_stand_pct` `15A0`, `drink_more_pct` `15A4` (I); `Sidewalk` → `Player_Blocking_Complain_Ms` `15A8`, `Player_Blocking_React_Ms` `15AC` (I) | |
| `PepperSpray` | `SprayMeterReact` `15B0`, `SprayMeterMax` `15B4`, `PlayerSprayIncRate` `15B8`, `PlayerSprayDecRate` `15BC`, `SprayIncRate` `15C0`, `SprayDecRate` `15C4` (F) | |
| `Fight_Club` | `Finisher_Rates` → repeated `Finisher_Rate`: `Num_Failures` (must be < 4), `NPC` F `0x014A15C8 + 4·n`, `Player` F `0x014A15D8 + 4·n`; `Health_Weight` F `15E8`; `Max_Imbalance` I `15EC`, `Min_Imbalance` I `15F0` | |
| `Coop_Meta_Game` | `Reward_Cash` F `15F4`; `Points_Per_Player_Death` `15F8`, `_Gang_Vehicle` `15FC`, `_Homie_Revive` `1600`, `_Mission_Objective` `1604`, `_Headshot` `1608`, `_Nutshot` `160C`, `_Melee_Kill` `1610`, `_Explosion_Kill` `1614`, `_Quick_Kill` `1618` (I) | |
| `Wieldable_Prop_Throw` | `Basketball_Multiplier_Low` `161C`, `Basketball_Multiplier_High` `1620`, `H1H_Mult` `1624`, `H2H_Mult` `1628`, `H2H_High_Mult` `162C`, `H2H_Low_Mult` `1630` (F) | |
| `Watercraft_Params` | `Reduced_Speed_Pct` `1634`, `Reduced_Speed_Pct_Min` `1638`, `Reduced_Speed_Pct_Max` `163C` (F); **`Min` is clamped to ≤ `Max`** | |
| `Siren_Whoop_Params` | `Whoop_Min_Count` `1640`, `Whoop_Max_Count` `1644` (I); `Honk_Whoop_Chance` `1648`, `Ram_Whoop_Chance` `164C`, `Hit_Vehicle_Whoop_Chance` `1650`, `Hit_Ped_Whoop_Chance` `1654` (F) | |
| `Fall_Damage` | `Min_Distance` `1658`, `Percent_Damage_per_Meter` `165C`, `MP_Percent_Damage_per_Meter` `1660` (F) | |

(Offsets of the form `NNNN` are `0x014A NNNN`.) The section names are the strings the sub-readers push before their child lookup; the parent of each section is `Gameplay_Constants` (the caller's element, passed in a register). **[CONFIRMED — disassembly for every name and destination; the *values* and their consumers are out of reach (base table).]** **[OPEN — desk review 2026-09-30: the heading counts 20 sub-readers, but the table above names 26 sections (`Death_and_busted` … `Fall_Damage`) besides the top-level leaves; unless some sub-readers handle several sections, the count is a mis-tally, and §14.9's "only 20 were traced" hedge on the six top-level sections rests on it; to be settled against the executable (enumerate the callees of `FUN_005F5B50` and their name literals).]**

**Review status (2026-09-30), §10.2: NEEDS-EXE: `Pepperspray` parent (OPEN here and in §14.9) and the 20-vs-26 sub-reader count (the block `0x014A0F50`…`0x014A1660` tiles section to section) — desk review (not re-derived from the executable).**

### 10.3 `gameplay_nags.xtbl` and `Gameplay_nag_globals.xtbl` (`FUN_007050A0`)

```
gameplay_nags.xtbl:   <Table><Nag_Data> <Name/> <Increment/>(float s) <Nag_Time/>(float s) </Nag_Data>…
Gameplay_nag_globals.xtbl:  <Table><Nag_Data> <Mission_postpone_s/>(int) <Nag_display_postpone_s/>(int) </Nag_Data>
```

*(Corrected 2026-09-30 from §14.10: the real element names are `Mission_postpone_S` / `Nag_display_postpone_S` (capital `S`); matching is case-insensitive, so behaviour is unchanged.)*

`Name` is matched (case-insensitive) against the **20-row nag table at `0x012F47F0`** (rows of `0x20` bytes; names `human_shield`, `cruise_control`, `power_attacks`, `taunting`, `fine_aim`, `sprinting`, `change_clothes`, `grenades`, `grenades_pc`, `city_takeover`, `hitman`, `chop_shop`, `weapon_upgraded`, `gang_customize`, `cell_rewards`, `crib_stash`, `radio_controls`, `sprint_recharge`, `low_ammo`, `notoriety` — exactly `spec-save-format.md` §12.6.3's list). For a matching row: **`+0x14` present flag := 1**, **`+0x10` increment := `round(Increment × 1000)` (ms)**, **`+0x0C` nag time := `round(Nag_Time × 1000)` (ms)**. **An unknown `Name` ends the whole load** (same behaviour as `stats.xtbl`, §2.2). The globals file overwrites `0x012F47E8` (mission postpone) and `0x012F47EC` (display postpone): both are preset to 30 and each is set to `value × 1000` (ms). **Note:** the save spec's "30 s and 300 s defaults" are the *file's* values, not this function's presets — the function presets both to 30 before reading. The row's `+0x18` counter is what entry 11 saves. *(Rounding of `round(… × 1000)`: see the OPEN note in §1.3.)*

**Review status (2026-09-30), §10.3: DESK-PASS, text fixes applied (element case from §14.10; `20·0x20 = 0x280`; 20/20 names per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") — desk review (not re-derived from the executable).**

### 10.4 `mission_checkpoints.xtbl` (`FUN_006DF8B0`)

Loaded **only when the current world name starts with `sr3_city`** (8-character case-insensitive prefix test on the string at `0x013CDBA0`), after 16 checkpoint-icon effect handles are resolved and the pool `"mission_checkpoint_sp"` (`0x1100` bytes) is created.

```
<Table>
  <MissionCheckpoints>                   ← repeated; ≤ 166 (0xA6) rows kept
    <MissionName/>  text  <CheckpointName/> text  <Index/> int  <Debug/> bool
```

Record (`0x10` bytes, array `0x014EB4E0`, count `0x014ED040`): `+0x00` mission-name `char*`, `+0x04` checkpoint-name `char*` (both copied into the string buffer `0x014ED5E8`), `+0x08` `Index`, `+0x0C` `Debug` (byte). After loading the array is **`qsort`ed** (`FUN_006DF740`). This is the mission-restart checkpoint list ("mission_checkpoint_sp" = single-player; `spec-save-format.md` §11.2's in-memory checkpoints are a different mechanism). **[OPEN — desk review 2026-09-30: the sort key of the `qsort` comparator is not given, so the lookup order cannot be reproduced; to be settled against the executable (`FUN_006DF740`).]**

**Review status (2026-09-30), §10.4: NEEDS-EXE: `qsort` comparator (`166·0x10 = 0xA60`; the 166-row patch copy exactly fills the capacity, §14.11) — desk review (not re-derived from the executable).**

### 10.5 `mission_help.xtbl` (`FUN_00A1FF70` → `FUN_00604540`)

Loaded **once** (guard: the destination pointer `0x026E7E68` is zero), with the string pool `0x01495410`. `FUN_00604540(destination {array, count}, file name, pool)`:

```
<Table>
  <String>                               ← repeated
    <Texts>
      <Text>                             ← repeated; every <Text> of every <String> becomes one 12-byte record
        <Name/>         text             ← hash = CRC-32 WITHOUT lower-casing (FUN_00D9E7E0) → +0x00
        <English/>      key              ← preferred; falls back to <DisplayText/> when absent → localisation handle → +0x04
        <Duration/>     float            ← +0x08; a negative value is replaced by FLT_MAX (3.4028235E+38, "no timeout")
```

Total record count = Σ `Text` elements; array allocated `count × 12` and zeroed. **[CONFIRMED — disassembly; no DLC sample for this table.]** (The case-sensitive hash here differs from the engine-wide lower-cased hash — a reimplementation must not lower-case `Name` when looking these records up.)

**Review status (2026-09-30), §10.5: DESK-PASS (12-byte record; 265 real records, §14.11) — desk review (not re-derived from the executable).**

### 10.6 `spawn_info_categories.xtbl`, `spawn_info_groups.xtbl`, `spawn_info_ranks.xtbl`

The ambient-spawn definition tables. `spawn_info_categories` (`FUN_00BE5890` → `FUN_00BE5300`, framework-aware: `FUN_00BE5300(record, element, pool, first-pass)`) fills **`0x60`-byte category records** (array pointer `0x029A9114`, count `0x029A9110`); `spawn_info_groups` (`FUN_00BE5C10` → `FUN_00BE5960`) fills **`0x30`-byte group records** (`0x029A910C` / `0x029A9108`); `spawn_info_ranks` (`FUN_00BE4DA0(merge)`) fills **`0x698`-byte rank records** at `0x029A91A0` (count `0x029A9118`; the zeroed area `0x16AA8` = room for **55** records).

**Category** (`<Category>`, repeated):

```
<Category>
  <Name/>                     text   ← record +0x00 (name pointer via FUN_00DB12C0); +0x04 = CRC-32(name)
  <Groups>
    <Group>                   ← repeated; 0x28-byte entries at record[+0x0C] (count record[+0x08])
      <Name/>                 text   ← CRC-32, matched against the spawn-group table's hash; UNKNOWN name or wrong group kind ⇒ entry dropped
      (only when the matched group's kind byte is 7:)
      <DayChance/> <NightChance/>                  float
      <DayCap/> <NightCap/>                        int
      <VehicleDayCap/> <VehicleNightCap/>          u32
      <Item_Carried/>         None | Luggage | Shopping Bags   → 0 | 1 | 2
      <Carry_Percent/>        u32
      <Group_Category/>       General Ped | Special Ped | Special Vehicle   → 0 | 1 | 2
  <Flags>                     ← <Flag> list: No Spawn Outside Category | No Enemy Gang Spawning | Law Spawning Area | Lockdown Area
                                → four bytes at record +0x10…+0x13
  <CarDay/> <CarNight/> <PedDay/> <PedNight/>                      float → +0x20…+0x2C
  <Law_Spawn_Group/>          text   ← CRC-32 → group lookup → +0x14
  <LawCap/> <LawDelay/>       int    → +0x18, +0x1C
  <General_Ped_Slot_Day/> <General_Ped_Slot_Night/> <Special_Ped_Slot_Day/> <Special_Ped_Slot_Night/>
  <Special_Vehicle_Slot_Day/> <Special_Vehicle_Slot_Night/>        float → +0x30…+0x44
```

The six slot weights are **normalised per time of day** (general ped, special ped, special vehicle: each divided by their sum; if the sum is ≤ 0 the general-ped weight becomes 1.0). Five category **names are special-cased** (first pass only): `Default Spawn` → global `0x029A911C`, `Start Spawn` → `0x029A9120`, `sp_cat_Cheat_Ho` → `0x029A9130`, `sp_cat_Cheat_Mascot` → `0x029A9134`, `sp_cat_Cheat_Zombie` → `0x029A9138` (these are the categories the "Pimps and Hos / Mascot / Zombie Peds" cheats of §8.2 switch to).

**Group** (`<Group>`, repeated): `Name` (CRC-32 → record `+0x00`); `GeneralFlags` (`Flag` list: `unique` bit 0, `vehicle_only` bit 1, `has_designated_driver` bit 2 of `+0x04`); `Team` (→ byte `+0x08`, resolver `FUN_0094CC60`, 9 if unknown); `Spline_Type` (one of `Highway Only`, `Boat`, `Offroad`, `Indoor`, `Baggage`, `Taxi` → `+0x24`, `-1` if none) **[label downgraded 2026-09-30 from CONFIRMED to OPEN for the completeness of this six-name list: team-b/HANDOFF.md "### 9.81 `sr3tables_progression`" reports "`Spline_Type`'s 6-name enum is incomplete: **58/61** real rows use a value outside it, all either `"All Roads"` or `"Surface Roads"`", matching §14.12 (56 + 2 = 58 of 61). Whether those values resolve to further enum entries or fall through to `-1` is to be settled against the executable (the compare table in `FUN_00BE5960`).]**; `Characters` → repeated `Character` (text → CRC-32): state-array pointer `+0x0C` (zeroed dwords), capacity `+0x10` (= number of `Character` children), running count `+0x14`, hash-array pointer `+0x18`; `spawn_drunk_pct_day` / `spawn_drunk_pct_night` (u32 → bytes `+0x28` / `+0x29`); `Vehicles` → repeated `Vehicle` (≤ 132, the vehicle-table size of `spec-vehicle-data.md` §7.1; each read by the vehicle-name/variant helper `FUN_00ACB350(node, "Name", "Variant")` → a 4-byte **vehicle handle** (low 16 bits = vehicle-table slot, high 16 bits = variant index, `spec-vehicle-data.md` §7.1); array `+0x20`, count `+0x1C`). After each group the loader **does not advance** the output slot if the group is `vehicle_only` and has no vehicles (a dropped record); `+0x2C` is set to `-1`.

**Rank** (`<Rank>`, repeated; the merge flag re-opens the table for DLC: a rank found by name hash is extended instead of appended):

```
<Rank>
  <Name/>                    text  ← record +0x00 = name hash (FUN_00DB12C0)
  <type/>                    Civilian | Gang | Homie | Law enforcement | Stag | National Guard | Other | Specialist  → +0x04 (-1 if none)
  <rank_numeric/>            u32 → byte +0x08
  <Hit_Points/> <Bleed_Out_Hit_Points/> <Knockdown_Points/>    u32, each clamped to 0x7FFF → u16 at +0x0A / +0x0C / +0x0E
  <Melee_Damage_Modifier/>   float → +0x10
  <Pct_Dmg_to_Flinch/>       float, only if present (else 0) → +0x14
  <per_team_info>
    <per_team_elm>           ← repeated; team index = resolver(<Team/>) & 0xFF; per-team block = 0xD0 bytes at +0x18 + 0xD0·team
      <Team/>                text
      <weapon_loadout><weapon_loadout_elm>   ← ≤ 12: <chance/> u8 (a 0 entry does not advance), <melee/> <firearm/> <thrown/> weapon names
                                                → 16-byte entries {chance, melee id, firearm id, thrown id} from block +0x00; id 0xFF if absent/unknown (FUN_00B81220/FUN_00B81A50); the running sum of chances is kept at block +0xCD
      <personality_rotation><personality/> …  ← ≤ 12 personality names → bytes at block +0xC0 (FUN_00507E60); their count is the byte at block +0xCC
```

The record (`0x698` = `0x18` + **8** × `0xD0`) has room for **8 team blocks**; the team resolver returns 9 for an unknown team name and the index is not range-checked. The weapon names resolve through the weapon-definition table also used by unlockable types 2/3 (§4.4). **[CONFIRMED — disassembly; the per-team block's exact interior offsets beyond those given were not tabulated.]** **[OPEN — desk review 2026-09-30: the category record's fields end at `+0x44` (`+0x48`…`+0x5F` of the `0x60` record are undescribed) and the `0x28`-byte category-group entry has no offset table; the 55 real rank rows exactly fill the 55-record area (§14.12), so any further row would overflow unchecked; to be settled against the executable (`FUN_00BE5300`, `FUN_00BE4DA0`).]**

**Review status (2026-09-30), §10.6: NEEDS-EXE: `Spline_Type` list (CONFIRMED downgraded in place on Team B's 58/61), category record tail and group-entry layout; `_Editor` is present on every real category and group row (team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") and is not read — desk review (not re-derived from the executable).**

### 10.7 `drunk_levels.xtbl` (`FUN_00975520` → `FUN_00975170`)

```
<Table>
  <Drunk_Levels>                         ← repeated; one per drug effect
    <Drug_Type/>          drunk | weed | escort tiger        ← other values skip the row
    <Max_Booze_Points/>   float
    <Max_Time_Drunk/>     float
    <Levels>
      <Level>             ← repeated, at most 5
        <Percent_drunk/> <Camera_rotation_mult/> <Camera_pitch_mult/>                       float
        <Random_input_switch_time/>                                                          int
        <Random_input_amount_foot/> <Random_input_amount_vehicle/>                           float
        <Control_delay_on_foot/> <Control_delay_vehicle/> <Ragdoll_on_impact_time/>          int
        <Reticle_x_max_offset/> <Reticle_y_max_offset/> <Reticle_x_speed/> <Reticle_y_speed/>  float
        <Sleepy/>                                                                             bool
        (+ <Freerunning_fail_pct/> on every real Level — added 2026-09-30 from §14.13; not in the read list; whether and where it is read is OPEN)
```

Storage: **3 effect slots of `0x118` bytes from about `0x0262581C`** (the loader first resets every level record to defaults — 0, `1.0` for the camera multipliers, `2000` ms for the two delay/impact fields — through the block-initialiser `0x009755F0`), each holding the two scalars, a level count, and five `0x38`-byte level records (`0x0262587C` … `0x02625C70`). The slot index passed to the row parser is the drug-type test order (`drunk`, `weed`, `escort tiger`) — HIGH CONFIDENCE; the register carrying the index was not decoded. **[CONFIRMED — disassembly for the element vocabulary; record offsets beyond the first field of each level are in order of the reads (`+0x00`…`+0x2C` floats/ints as listed, `Sleepy` at `+0x30`); the two scalar destinations are `0x02625828`/`0x02625834` (+ slot)].** **[OPEN — desk review 2026-09-30: the slot numbers do not fit together: `5 · 0x38 = 0x118`, so a `0x118` slot holds the five level records and nothing else, leaving no room for the two scalars and the level count; the level records start at `0x0262587C − 0x0262581C = 0x60` into the block; and the stated span `0x02625C70 − 0x0262581C = 0x454` matches neither `3 · 0x118 = 0x348` nor `3 · (0x60 + 0x118) = 3 · 0x178 = 0x468`. The slot stride and the scalar offsets are to be settled against the executable (`FUN_00975520`, `FUN_00975170`, block initialiser `0x009755F0`).]**

**Review status (2026-09-30), §10.7: NEEDS-EXE: slot stride/span arithmetic does not close; `Freerunning_fail_pct` undocumented in the read list (3/3 drug-type names per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") — desk review (not re-derived from the executable).**

### 10.8 `rank_reactions.xtbl` (`FUN_0051A520`)

```
<Table>
  <NPCGroup>                             ← repeated
    <Name/>    Civilian | Civilian Saints Hated | Gang, Enemy | Gang, Friendly | Police     ← compared CASE-SENSITIVELY
    <Reactions>
      <ReactionPercentages>              ← repeated
        <PlayerRank/>   Rank 1 - Thug | Rank 2 - Killa | Rank 3 - Gangsta | Rank 4 - Kingpin   ← CASE-SENSITIVE
        <Ignore/> <Avoid/> <Compliment/> <FlipOff/> <GangSign/> <SayAmbient/> <Threaten/> <Observe/>   int (percent)
```

Each of the eight reaction values is read as an int and **stored as a float `value × 0.01`** into a **`0x20`-byte row** (eight floats) of the table at `0x013BA330`: row index = `4·group + rank`, i.e. 5 groups × 4 ranks. **A `Name` or `PlayerRank` that matches none of the names is not rejected: the search variable runs past the list (5 / 4) and the code stores through the resulting out-of-range index** (HIGH CONFIDENCE from the loop structure; not exercised). **[CONFIRMED — disassembly.]**

**Review status (2026-09-30), §10.8: DESK-PASS (`5·4·0x20 = 0x280`; 5/5 names per team-b/HANDOFF.md "### 9.81 `sr3tables_progression`"; every row sums to 100, 20/20, §14.13) — desk review (not re-derived from the executable).**

### 10.9 `default_global.xtbl` (`FUN_00BB70B0`, called from the world-load path `FUN_006E2460`)

Opened with the plain open helper (`FUN_00DC5AC0`) — so the document root is used directly, without the `Table` child. Three optional outputs, each guarded by the caller supplying a destination buffer:

| Child of the root | Reader | Default | Destination |
|---|---|---|---|
| `skybox_mesh_filename` | text (`FUN_00DC5190`) | `rfg_skybox` | caller buffer 1 |
| `cloud_mesh_filename` | text | `skybox_clouds` | caller buffer 2 |
| `orbitals` → repeated `orbital` → `map_name` | text; kept only if non-empty | — | caller array of **15** names × `0x40` bytes (count byte-limited: stops at 15) |

The two defaults are the literals in the executable. **[CONFIRMED — disassembly.]** (The skybox/cloud mesh names are the world's sky assets; `spec-tables-environment.md` covers the sky tables.) *(Added 2026-09-30 from §14.14: the real file also has five more top-level elements — `horizon_mountain_enabled`, `fog_camera_follow`, `day_begin`, `day_end`, `tod_segments` — not in the table above.)* **[OPEN — desk review 2026-09-30: which code reads those five elements; to be settled against the executable (`FUN_00BB70B0` and the other callers of `FUN_00DC5AC0`).]**

**Review status (2026-09-30), §10.9: NEEDS-EXE: five real top-level elements unexplained (plain-open shape confirmed by §14.14) — desk review (not re-derived from the executable).**

### 10.10 `tweak_table.xtbl` — the named-override table (`FUN_0071B420`/`FUN_0071B450` → `FUN_0071B070`)

```
<Table>
  <Tweak_Table_Entry>                    ← repeated
    <Name/>         text ≤ 255           ← looked up (case-insensitive) in the tweak REGISTRY; unknown names are ignored
    <Value/>        float                ← always-write reader
    <Description/>  text                 ← authoring-only ("NOT read into the game")
    <Framework/>    text                 ← default `main`; filter as §1.4 (base call passes a NULL framework = accept all)
```

**The registry** (`FUN_0071AFB0` lazily creates it: pointer array `0x015216E4`, **capacity `0x280` (640)**, count `0x015216E0`) is filled by **static registration constructors** — four of them, one per value type — each taking (entry object, name literal, destination address) and appending the entry unless the name is already registered: bool `FUN_0071B4E0` (type 0), float `FUN_0071B580` (1), int `FUN_0071B530` (2), 4-float vector `FUN_0071B5D0` (3). An entry is `{+0x00 name char*, +0x04 destination pointer, +0x08 type}`. **Applying a row** (`FUN_0071AE40`): type 0 → byte `1` unless `Value == 0.0`; type 1 → float; type 2 → `int(Value)` truncated; type 3 → all four floats := `Value`. **Every call site of the four constructors was enumerated** (`tools/scripts/AiTweakReg.java` → **`tools/ai_tweak_registry.tsv`**): **586 registered tunables, 586 distinct names (none duplicated), all 586 names recovered** — **422 float, 117 int, 46 vec4, 1 bool** (`Challenge_reset_on_mission_system_reload`) — comfortably under the 640 capacity. The TSV lists, for each, the type, name, destination address and entry-object address; it is the complete list of the names a `tweak_table.xtbl` row may use. Name families by first token: `Vehicle` 47 · `Player` 41 · `Human` 40 · `Brute` 25 · `Ai` 21 · `Satellite` 18 · `Melee` 18 · `VI` 17 · `Parachute` 14 · `Notoriety` 14 · `Camera` 13 · `Max` 12 · `Freefall` 11 · `Sprint` 10 …

**Validation against the raw DLC row.** The only raw DLC tweak file (`dlc1_tweak_table.xtbl`, one row) names `Genki_ball_bounce_timer_ms` with `Value` 3000 — that name **is** in the registry as an **int** (destination `0x0140E6C4`), so the row applies as `3000` (`prog_validate.py` V4). `Genki_mind_control_time` (int, `0x026169C0`) is the other `Genki_*` tunable.

**[CONFIRMED — disassembly for the loader and the registration mechanism; CONFIRMED — enumeration for the registry contents (586/586 names resolved from the constructors' literal arguments); the *consumers* of each tunable were not traced.]**

**Review status (2026-09-30), §10.10: DESK-PASS, text fixes applied (`422 + 117 + 46 + 1 = 586`; the 601/585/16 real-row figures of §14.15 reconciled with Team B's count there; the NULL-framework base call is now listed in §1.4) — desk review (not re-derived from the executable).**

### 10.11 `metered_sprint.xtbl` (`FUN_009FFFE0`, called with the entry name `Single Player`)

```
<Table>
  <SprintEntry>                          ← repeated; only the row whose <Name/> equals the requested entry name is read (case-SENSITIVE; the boot path asks for "Single Player")
    <UseTime/> <RechargeTime/> <DelayTime/>     int → 0x0268D29C, 0x0268D290, 0x0268D28C
    <PantPercentage/>                            float → 0x0268D288 = int(RechargeTime × PantPercentage)   (the pant threshold, ms)
    <JumpPenalty/> <StartPenalty/>               float → 0x0268D284, 0x0268D280
```

The first matching row ends the search. Its values are the sprint-meter parameters that the `Sprint_Bonus` unlockable (§4.4 type 8) and the saved sprint scalar (`spec-save-format.md` §10.6, `0x4A50`) scale. **[CONFIRMED — disassembly.]** *(Added 2026-09-30 from §14.15: the real file has a second row, `Multiplayer`; only the `Single Player` lookup was traced. Rounding of `int(RechargeTime × PantPercentage)`: see the OPEN note in §1.3.)* **[OPEN — desk review 2026-09-30: the call site(s) of `FUN_009FFFE0` that request `Multiplayer`; to be settled against the executable.]**

**Review status (2026-09-30), §10.11: NEEDS-EXE: second (`Multiplayer`) row's caller and the product's rounding — desk review (not re-derived from the executable).**

### 10.12 `activity_types.xtbl` (`FUN_00617E10` → `FUN_006174C0`; base+mission-set enumeration in `FUN_0084E3B0`)

The table is loaded once for the base game and once per mounted DLC framework (`FUN_006174C0(file, framework, pool)`); rows are matched by `Name` to one of **19 activity-type records of `0x94` bytes at `0x014B2DE8`** (count fixed at 19 = `0xAFC/0x94`; the record names are registered at run time, not in the static image, so the 19 names are not recoverable from the loader alone). **A `Name` that matches no record gives index −1 and the reader then writes through the record *before* the array (unchecked — HIGH CONFIDENCE, not exercised).**

```
<Table>
  <Activity>                             ← repeated
    <Name/>                         text  ← activity type name (must be a registered type)
    <Framework/>                    text  ← default `main`; rows for another framework are skipped
    <Unlockables><Unlockable/>…</Unlockables>   ← unlockable NAMES; each kept only if it exists in the unlockables table (§4) → id array (ptr +0x54, capacity +0x58, count +0x5C)
    <Completion_Image><Filename/></Completion_Image>   ← extension stripped, pooled → +0x08 (empty string constant if absent)
    <Activity_type_flags><Flag/>…</Activity_type_flags> ← bits in record bytes +0x41 / +0x42 (below)
    <Disable_flags><Flag/>…</Disable_flags>              ← optional; bits in record bytes +0x43 / +0x44 (below)
    <Soundbank_Name/>               text  ← +0x48 (via the string helper FUN_005540A0)
    <Type/> <Additional_Resources/> <_Editor/>   ← present in DLC rows; NOT read here
```

| Byte | Bit → flag string (case-insensitive, matched against the `Flag` children) |
|---|---|
| `+0x41` | 0 `stats doesnt count` · 1 `keep screen faded` · 2 `auto advance level` · 3 `remove noteriety spawns` · 4 `reset noteriety each level` · 5 `uses button mashing interface` · 6 `fail on death` · 7 `finisher only death` |
| `+0x42` | 0 `no vehicle eject` · 1 `do initial warp` · 2 `racing` · **3 = set when the loading framework is not `main`** (a DLC activity type) |
| `+0x43` | 0 `turn off spawning` · 1 `disable distant spawns` · 3 `disable parking spawns` · 4 `disable all stores` · 5 `disable crib` · 6 `disable HUD` · 7 `allow cops to shoot from vehicle` |
| `+0x44` | 0 `disable helicopters` · 1 `disable attack helis` · 2 `disable player swap cheats` · 3 `disable warp triggers` · 4 `disable roadblocks` |

(The spelling **`noteriety`** is the real one in both the code and the data.) `Disable_flags` is processed only if the element exists (the two bytes are zeroed first). **[CONFIRMED — disassembly.]** **[OPEN — desk review 2026-09-30: byte `+0x43` lists bits 0, 1, 3–7 with no bit 2; whether bit 2 is unused or a flag string was missed is to be settled against the executable (`FUN_006174C0`).]** *(Added 2026-09-30: one real base row, `Fraud` (1/15), also carries a direct top-level `<Unlockable>` child outside `<Unlockables>`, which this reader does not read — team-b/HANDOFF.md "### 9.81 `sr3tables_progression`", disagreement (3).)*

**Validation (raw DLC sample `dlc1_activity_types.xtbl`, `prog_validate.py` V5).** Every `Flag` string the DLC rows use is in the reader's sets, every `Unlockable` name is a `dlc1_unlockables.xtbl` row, and the tags the reader ignores are exactly `Type`, `Additional_Resources` and `_Editor`.

**Review status (2026-09-30), §10.12: NEEDS-EXE: the missing bit 2 of `+0x43` (`19·0x94 = 0xAFC`; the unread top-level `Unlockable` on `Fraud` noted) — desk review (not re-derived from the executable).**

## 11. Cross-table reference map and load order

Every cross-table reference found in the loaders of this group. **Mechanism** is what the loader does; **authoring reference** is the `<Reference>` (file, type) declared in the editor schema (`TableDescription`) carried by the raw DLC files — an independent statement, available only for `unlockables`, `achievements`, `activity_types`, `tweak_table` (the base tables' schemas are unreadable). Reference targets in other groups are documented in the sibling specs (`spec-tables-weapons-combat.md`, `spec-tables-traffic-ai.md`, `spec-tables-environment.md`) where they cover the target.

| From (table → element) | To (table.element) | Mechanism in the loader | Authoring reference |
|---|---|---|---|
| `achievements` → `Requirement/Stat` | `stats.Name` | case-insensitive scan of the loaded stat rows; unknown ⇒ requirement dropped | `stats.xtbl`, `Stat.Name` |
| `stats` → `percent/Percentage_Of_Stat` | `stats.Name` (an earlier row) | case-**sensitive** scan of already-loaded names | — |
| `achievements`, `stats`, `unlockables`, `collectibles`, `respect_levels`, `mission_help` → `DisplayName` / `Description` / `Event_Text` / `Title_Message` / `Rank` / `English` | localisation tables | CRC-32 key → string handle | `localized_exports\HUD_text.xtbl` (`HUD_Identifier.Name`) for achievements; `localized_exports\Unlock_text.xtbl` (`SR3_Unlockables_Identifier.Name`) for unlockables |
| every `Framework` child (`achievements`, `unlockables`, `tweak_table`, `activity_types`, …) | `dlc_frameworks.xtbl` (`BundleInfo.Flag_String`) | text compare with the loading framework (§1.4) | as stated |
| `unlockables` → `Priority` | `unlockables.Name` | CRC-32 of the text stored at `+0xE4` | `unlockables.xtbl`, `Unlockable.Name` |
| `unlockables` → `Auto_Unlock` | `unlockables_auto_flags.xtbl` | four spellings, §4.2 | `BundleInfo.Flag_String` |
| `unlockables` → `Vehicle/Vehicles/Vehicle/Type` | `vehicles.xtbl` `Vehicle.Name` | name hash → vehicle-table slot (`spec-vehicle-data.md` §7.1) | as stated |
| `unlockables` → `Homie/Name` | `homies.xtbl` `Homie.Name` | name hash → homie record | as stated |
| `unlockables` → `Crib_Weapon/Name`, `Store_Weapon/Name`; `spawn_info_ranks` → `melee`/`firearm`/`thrown` | `weapons.xtbl` `Weapon.Name` | `stricmp` scan of the weapon-definition table (`0x028DC4DC`, stride `0x7EC`) | as stated (unlockables) |
| `cheats` → `Weapon/Give/Weapon` | `weapons.xtbl` `Weapon.Name` | the name string is **stored** (record `+0x7C`) and resolved when the cheat runs — not traced | — |
| `unlockables` → `Unlimited_Crib_Ammo/weapon_class` | `weapon_class.xtbl` `Weapon_Class.Name` | index in a 23-name list | as stated |
| `unlockables` → `Ammo_Multiplier`/`No_Reloading`/`Gang_Weapons` `inv_slot`/`Weapon_Slot` | `weapon_inventory_slots.xtbl` `Inv_Slot.Name` | index in an 11-name list beginning `unarmed` | as stated |
| `unlockables` → `Clothes/Items/Item/ItemName`; `Killbane_Mask/Mask` (unread) | `customization_items.xtbl` `Customization_Item.Name` | item lookup by name | as stated |
| `unlockables` → `Item/Color1..3` | `character_color_pool.xtbl` `Color_Entry.Name` | colour name → id | as stated |
| `unlockables` → `Outfit/Outfit` | `customization_outfits.xtbl` `Outfit.Name` | CRC → outfit list node | as stated |
| `unlockables` → `Gang_Customization/Gang_Type` | `character_customization_categories.xtbl` `category.Name` | CRC-32 stored | as stated |
| `unlockables` → `Gang_Vehicle_Customization/Vehicle_Group` | `gang_customization.xtbl` (`GangCustomization.gang_vehicles.gang_vehicles.name`) | CRC-32 stored | as stated |
| `unlockables` → `Gang_Taunts/Team`, `Customization_Items/Team_Name`; `spawn_info_groups` → `Team`; `spawn_info_ranks` → `Team` | `teams.xtbl` `Team.Name` | team resolver `FUN_0094CC60` (9 if unknown) | as stated (unlockables) |
| `unlockables` → `Taunts`/`Compliments` `Action` | `customizable_action.xtbl` `CustomizableActions.Action` | name → action index | as stated |
| `unlockables` → `Discount/name…name_8` (and `store_discounts` → `DiscountName`, HIGH CONFIDENCE) | `shop_names.xtbl` `Shop_Names.Name` | CRC-32 stored | as stated (unlockables) |
| `unlockables` → `Crib_Cash_Stronghold_Scalar/district` | `map_districts.xtbl` `Map_district.Name` | CRC-32 stored | as stated |
| `unlockables` → `Damage_Resist/Source`, `Notoriety/Score` | fixed engine lists (5 damage types; notoriety categories) | §4.4 | — |
| `respect_levels`, `activity_types`, `collectibles` → `Unlockable` | `unlockables.Name` | CRC-32 → **existing** record (`respect_levels`, `activity_types`; unknown ⇒ dropped) or **hash only** (`collectibles`) | `unlockables.xtbl`, `Unlockable.Name` (activity_types) |
| `activity_types` → `Soundbank_Name` | `audio_banks.xtbl` `NewEntity.Name` | string helper | as stated |
| `cheats` → `Vehicles/Drop/Vehicle_Name`; `notoriety_spawn` → `vehicle_name`; `spawn_info_groups` → `Vehicle/Name` | `vehicles.xtbl` `Vehicle.Name` | name hash → vehicle slot/handle | — |
| `spawn_info_categories` → `Group/Name`, `Law_Spawn_Group` | `spawn_info_groups.Name` | CRC-32 match; **load order groups → categories → ranks** (`FUN_00BE5CD0`) | — |
| `tweak_table` → `Name` | the in-code tweak registry (586 names, §10.10) | case-insensitive scan | — |
| `store_discounts` → `RadioEvent` | radio-event ids | CRC-32 stored | — |

**Load order that matters.** The master start-up routine `FUN_005D25F0` runs these loaders in this order (call sites ascending): `tweak_table` (`0x5D2CCA`) → `collectibles` (`0x5D2E2C`) → `notoriety` + `notoriety_levels` (`0x5D2F9A`) → `gameplay_nags` + globals (`0x5D2F9F`) → `drunk_levels` (`0x5D30AB`) → **`spawn_info_groups` → `spawn_info_categories` → `spawn_info_ranks`** (`0x5D3251`) → `difficulty_levels` (`0x5D3311`) → `metered_sprint` (`0x5D34BA`) → **`stats` + `achievements`** (`0x5D356E`) → `cheats` (`0x5D3585`) → **`unlockables` (+ `patch_unlockables`)** (`0x5D3628`) → **`respect_levels`** (`0x5D362D`, immediately after, because its `Unlockable` names are resolved against the unlockables just loaded). `store_discounts`, `mission_checkpoints`, `default_global`, `activity_types` and the mission sets are loaded by the **per-world loader** `FUN_0084E3B0` (and `mission_checkpoints` only for a world whose name starts with `sr3_city`); `rank_reactions` is loaded from a UI/AI init path (`FUN_0050EFB0`). DLC copies are loaded when a DLC bundle is mounted, through function pointers (the DLC unlockable/achievement loaders have no direct callers). **[CONFIRMED — disassembly.]** **[OPEN — desk review 2026-09-30: the order above is the ascending order of the call sites; ascending addresses prove execution order only for straight-line code, not across branches or loops inside `FUN_005D25F0`; to be settled against the executable (its control flow).]**

**Review status (2026-09-30), §11: NEEDS-EXE: call-site order assumed to be execution order (dependencies stats → achievements, unlockables → respect_levels, groups → categories → ranks are consistent) — desk review (not re-derived from the executable).**

## 12. Validation against the real save snapshots and the raw DLC tables

All predicates were stated before they were run; `python tools/harnesses/prog_validate.py` prints N/N for each and lists any failure (**final run: 0 failures**). Ground truth: the 16 real snapshots (`tools/harnesses/save_region_map.load_samples`), the 142 raw DLC tables, `spec-save-format.md` (independently derived facts of agents Q/U/W), and the executable's own data tables (memory dumps `tools/ai_mem*.txt`).

| # | Predicate | Result |
|---|---|---|
| V1.1–V1.4 | the stat name/id table at `0x01141BD0` has 217 rows; ids are the permutation 0…216; every (name, id) equals agent Q's independent id → name table; names unique case-insensitively | 217/217 each |
| V1.5–V1.7 | the loader's `complex` switch names 44 ids with 44 distinct handler types | 44/44 |
| V1.6 | the complex ids whose handler type has no serialise slot are exactly the 38 record-less ids of `spec-save-format.md` §7.4 | 38/38 |
| V1.8 | only id 45 has value class 5, so the entry-27 "skip class 5" never fires in 68…216 | 1/1 |
| V1.9 | the value class of the complex ids inside the saved range (69, 126, 150, 163, 164) agrees with the snapshots' float/int decode | 5/5 |
| V2.1–V2.10 | 56 raw DLC unlockable rows: every top-level child is read; `Type` shape; per-type child sets; `Category`/`Auto_Unlock`/`Is_DLC`/`Framework` values; names unique, hash-collision-free | 56/56 each |
| V2.11 | DLC-block ids of the 14 version-9 snapshots equal CRC-32(lower(Name)) of a raw DLC row | **784/784** (56 per file × 14) |
| V2.12 | slots 6…61 of the block are the 56 rows in file order (`dlc1`, `dlc2`, `dlc3`) | 14/14 |
| V2.13 | base-block names of `spec-save-format.md` §10.6 reproduce under the hash | 22/22 |
| V2.14–V2.17 | the editor schema of the raw DLC unlockables: 13 top-level elements = the row reader's set; 63 type children, 60 shared with the loader's 61, **child-element names equal the sub-reader's for 60/60**; identical in all 3 files | 13/13 · 60/60 · 60/60 · 3/3 |
| V3.1–V3.8 | 30 raw DLC achievement rows: only `PC_Only` is unread; `Framework`; empty `Requirements`; `HudUpdateFrequency`; bool text; unique names; schema element set = reader set + `PC_Only` | 30/30 each; 9/9 |
| V4.1–V4.3 | tweak registry: 586 distinct names from 586 constructor calls; census 422 float / 117 int / 46 vec4 / 1 bool; the raw DLC tweak row's name (`Genki_ball_bounce_timer_ms`) is a registered **int** | 586/586 · 586/586 · 1/1 |
| V5.1–V5.5 | raw DLC `activity_types` rows: all 19 activity flags and 8 disable flags are strings the reader tests; ignored tags = `Type`/`Additional_Resources`/`_Editor`; unlockable names exist in `dlc1_unlockables`; `Framework` = `dlc1` | 19/19 · 8/8 · 3/3 · 3/3 · 4/4 |
| V6.1–V6.5 | the transcribed type-name table (61), category table (10), re-apply byte table (64; its 31 ones = the types of `spec-save-format.md` §10.6) and the 20 nag names equal the executable's own data | 61/61 · 10/10 · 64/64 · 31/31 · 20/20 |

**Controls.** A wrong-hash control was not needed for V2.11 (784/784 exact against 4.29-billion-value ids); the six unexplained DLC-block slots (0–5) are reported as unexplained rather than fitted *(since explained: §4.1 / §14.1)*. The type-child equality of V2.16 is checked against a source (the schema) that the loader decode never saw.

**Not validated (no sample exists):** `stats`, `respect_levels`, `notoriety*`, `difficulty_levels`, `cheats`, `collectibles`, `store_discounts`, `gameplay_constants`, `gameplay_nags`, `mission_*`, `spawn_info_*`, `drunk_levels`, `rank_reactions`, `default_global`, `metered_sprint` — base-only tables (the DLC archives carry none). Their schemas rest on the loader disassembly alone. **[Superseded by §14 (2026-09-23): all of these were validated against the real base tables.]**

## 13. Open items, notes for other documents, artifacts

### 13.1 Notes for other documents (annotated by reference; nothing in another agent's file was edited except the three minimal pointers listed)

- **`spec-save-format.md` §12.6.1** — "skipping rows whose dword at row `+0x14` equals 5, which rows these are was not determined": resolved in §2.5 (value class 5 = id 45 only, never in 68…216).
- **§10.6 `0x4A24`** — "which index is which category was not shown by code": luchadores 0, deckers 1, morningstar 2, **police 3** (§4.6).
- **§10.6 `0x4A60`** — the "cash-gain multiplier (HIGH CONFIDENCE)" label is **contradicted** by the types that write it (19 `Melee_Damage_Bonus`, 45 `Muscles`, both the setter for player `+0x1B70`); the cash type (38) writes an unsaved global. Suggested label: HYPOTHESIS — melee-damage multiplier (§4.6). Also `0x4A4C` = type 11 `Health` modifier, `0x4A58` = type 14 `Repair_Discount`, `0x4A68` = type 23 `Firearm_Accuracy`, `0x4A6C` = type 34 `Vehicle_Customization` flag.
- **§12.6.3** — the two nag-postpone globals are preset to **30 s each** by the loader (§10.3); the "300 s" figure is a value from the file, not a default of the function.

### 13.2 Consolidated open items

1. ~~**Base-game values** for every table (parked mode-(a) limitation)~~ **RESOLVED for this whole group (§14): every table below was extracted from the real base archives and its real values decoded.** Narrower value/consumer gaps that remain are re-stated per item below and in §14's own "still open" list (§14.17).
2. **`stats.xtbl`** — ~~the per-id `Value_Type` assignment (§2.6)~~ **RESOLVED in aggregate (§14.3): real distribution over 217 rows is integer 116 / complex 44 / distance 21 / money 16 / percent 10 / time 9 / float 1 / boolean 0 — `boolean` is UNUSED in the shipped table; the 10 percent stats' denominators are now named.** Handler-table slots 2–5 and the 51 handlers' semantics remain OPEN; ~~whether the shipped file defines all 217 rows~~ **CONFIRMED yes (§14.3): 217/217 present, none missing.**
3. **`achievements.xtbl`** — ~~retail base row count vs the 83-record window~~ **RESOLVED (§14.2): 53 base / 83 base+DLC-merged in a second same-named file, exact fit — see the correction at §3.1/§3.6.** `HudUpdateFrequency` consumer (still untraced); ~~**NEW: which of the two same-named `achievements.xtbl` copies is actually mounted (archive precedence, §14.2) — the single most consequential open item this pass surfaced.**~~ **RESOLVED 2026-09-24 (§14.2): the `patch_compressed.vpp_pc` copy (83 rows) is the one actually loaded, traced to a patch-exclusive archive-priority mechanism in the game's own boot/mount code.**
4. **`unlockables.xtbl`** — ~~the six leading DLC-block ids (slots 0–5) and which bundle they belong to~~ (already RESOLVED at §4.1 as the 6 `patch_unlockables.xtbl` rows, before this pass); ~~the content of `patch_unlockables.xtbl`~~ **RESOLVED (§14.1): 6 rows, names confirmed, its own `TableTemplates` sibling empty.** Apply-routine semantics beyond the setters listed in §4.6; resolver internals for the hash-only types; the unread schema types `Killbane_Mask`, `Melee_Style`, `Purchasable_Cribs`. **NEW (§14.1): the real `<Table>` has 312 rows, not the previously-cited 403 (corrected at §4.1); the 312 saved base ids are an exact bijection with the 312 real rows in all 16 snapshots (not a subset as previously stated); 44/61 types and 28/312 `Is_DLC=Sometimes` rows are now known; the `M17_Vehicle_STAG_Tank_Upgrade` special case is CONFIRMED to fire (row exists).**
5. **`respect_levels.xtbl`** — the per-level unlock capacity (20, read from the constructor's loop, not its store — still not independently re-derived; the largest real level (50) uses only 8 of the 20 slots, §14.5). ~~Per-level `Respect` values and unlockable lists~~ **RESOLVED (§14.5): all 50 real levels decoded — `Respect` rises by a constant 115/level from 2115 (L1) to 7635 (L49), L50=0; cumulative total = 238,875.**
6. **`notoriety*.xtbl`** — which `NotorietyLevels` set is police vs gang (still OPEN — real values decoded at §14.6 but not traced to a consumer); `Check_Detection` semantics (still OPEN); ~~the `Table`-nested row shape of `notoriety_spawn.xtbl`~~ **RESOLVED (§14.6): confirmed, and `level_info`/`group_info`/`group_details` are ALSO same-name-wrapped containers, not flat siblings — §6.3's ASCII art is corrected there.** The interior of the 24 (real data: 25, §14.6 — `stag_speedboat`) destination structures beyond the fields now decoded (§14.6) remains OPEN.
7. **`difficulty_levels.xtbl`** — meaning of set A vs set B (still OPEN/HYPOTHESIS — real values decoded at §14.7 show Set B is uniformly harsher at the top difficulty level, consistent with but not confirming a SP-vs-co-op split); the consumers. ~~Per-level values~~ **RESOLVED (§14.7): both sets, all 3 levels + 4 rank-occurrence rows each, decoded.**
8. **`cheats.xtbl`** — flag bytes `+0x02`/`+0x03` (still OPEN); ~~the five interface-category names (filled at run time)~~ **the NAMES are now known from the raw XML text (§14.4: `Vehicles`, `Weapons`, `Gameplay`, `World`, `Weather`) — the runtime index-to-name mapping remains OPEN, since that is filled by unrelated menu code, not this loader.** ~~DLC cheat tables~~ **RESOLVED (§14.4): no separate DLC cheat file exists; DLC cheats are 7 rows of the single base `<Table>` marked `Is_DLC=True`.** **NEW (§14.4): the previously-reported "151 UnlockString values" was `<Table>` (97, real/active) + `<TableTemplates>` (54, unread) combined — only the 97 are ever active (23/23 real saved ids check out against `<Table>` only).**
9. **`gameplay_constants.xtbl`** — consumers of each constant (destinations are given; no consumer was traced); the parent register of the sub-readers (assumed `Gameplay_Constants`). **NEW (§14.9): six top-level sections (`throwing_constants`, `Human_Fine_Collision`, plus top-level `Crime_scene`/`Drunk`/`Bust`/`Gunfire_Evade` duplicating their correctly-nested, documented-as-read twins under `Idle_AI`/`Combat_AI` with DIFFERENT values) are real but not reachable by the documented reader — most likely dead authoring leftovers, flagged not asserted; and the `Spray_Min`-read-twice bug (§10.2) is now CONFIRMED on real data (`Spray_Max=3000` is authored and silently discarded).**
10. **`drunk_levels.xtbl`** — the slot-index register (still OPEN); record offsets inside a level record beyond the read order. ~~Real values~~ **RESOLVED (§14.13): all 3 drug types (drunk/weed/escort tiger), all levels, decoded. NEW coverage gap: every real `Level` record carries an undocumented `Freerunning_fail_pct` element not in §10.7's tree.**
11. **`activity_types.xtbl`** — ~~the 19 activity-type names (registered at run time)~~ **HIGH CONFIDENCE RESOLVED (§14.16): all 19 recovered — 15 from the real base file, 4 more from the raw `dlc1_activity_types.xtbl` sample, no overlap, count matches exactly. Not CONFIRMED by tracing the runtime registration call sites.** A `Name` that matches no record indexes before the array (still not exercised).
12. **`tweak_table.xtbl`** — the consumers of the 586 tunables (the registry gives names, types and destination addresses only). **NEW (§14.15): the real base file has 601 rows; 585 match a registered tunable, 16 do not (confirmed absent from the registry, not a matching artifact) and are therefore dead data — exactly the population-scale fact the original 1-row DLC validation could not have caught.**
13. **Mixed-case file name** — `Gameplay_nag_globals.xtbl` is the only literal that differs from the requested name; a file lookup in the archives is presumably case-insensitive (not verified against a container listing because the base containers are parked).

### 13.3 Artifacts

Harnesses: `tools/harnesses/prog_validate.py`, `prog_common.py`, `prog_unlock_tables.py`, `prog_dlc_extract.py`, `prog_append.py`. Data: `tools/ai_stat_static_table.tsv` (217 stat names/ids), **`tools/ai_tweak_registry.tsv` (586 tunables: type, name, destination, entry object, call site)**, `tools/ai_dlc_xtbl/` (142 raw DLC tables + `_all_entries.txt`). Ghidra post-scripts: `tools/scripts/AiMem.java` (memory/table dump with pointer-to-string annotation), `AiDecForce.java` (force-disassemble + decompile), `AiTweakReg.java` (registry enumeration); runner `tools/run_tbl2.ps1`; project copy `tools/gp_tbl2` (disposable). Decompile/dump outputs: `tools/ai_stats1.txt`, `ai_ach.txt`, `ai_unlock*.txt`, `ai_apply.txt`, `ai_setters.txt`, `ai_grp*.txt`, `ai_gc.txt`, `ai_cheat.txt`, `ai_act.txt`, `ai_res1.txt`, `ai_mem*.txt`, `ai_asm*.txt`. The parked items of `HANDOFF.md` §27.3 (mode-(a) container decompression; `.czn_pc` interior) were not touched.

## 14. Real base-table extraction and value validation (2026-09-23)

**What changed since §1–§13 were written:** every table cited in §1.2's filename census was pulled from the real, now-readable base archives (`misc_tables.vpp_pc`, `da_tables.vpp_pc`, `patch_compressed.vpp_pc`; `spec-vpp-container.md` §7/§8, `spec-xtbl-format.md` §7) and decoded — the "base-game values are out of reach" caveat of this document's own header no longer holds for this group. Extraction harness: `tools/harnesses/prog_base_extract.py` (companion to `prog_dlc_extract.py`); every file it pulls verified byte-length-exact against its container directory entry (32/32 extractions, 0 failures). Extracted files are preserved at `tools/ai_base_xtbl/*.xtbl` for anyone who needs the full raw content beyond what is transcribed below. Parser: `tools/harnesses/prog_common.py`'s tolerant reader (case-insensitive element matching, per `spec-xtbl-format.md` §7). **Method note:** counts below are direct element-tree counts from the real files, cross-checked in three places against independent raw-regex tag counts and, for unlockables/cheats, against the real save snapshots' saved-id sets — not estimates.

### 14.1 `unlockables.xtbl` / `patch_unlockables.xtbl` — the "403 rows" correction, and a new structural fact

**Correction to §4.1/§4.7 and to `spec-save-format.md` §14 (struck through in place there): the real `unlockables.xtbl` `<Table>` element — the one the loader actually reads (§1.3: the table-open helper returns the document's `Table` child, nothing else) — has exactly `312` `<Unlockable>` rows, not 403.** The 403 figure (previously cited from a raw tag count) conflated the real `<Table>` with a **previously-undocumented fourth root-level sibling, `<TableTemplates>`**: `<root>` in the real file contains `<Table>` (312 rows), `<TableTemplates>` (91 more `<Unlockable>`-named elements — editor scaffolding, one apparent "starter" example per authoring convenience, never touched by any loader since every loader in this document opens the file through the `Table`-only accessor of §1.3), `<TableDescription>` and `<EntryCategories>`. Raw regex tag count = 312 + 91 = 403, which is where the old number came from. **[CONFIRMED — empirical: a full recursive walk found all 403 `<Unlockable>`-named nodes, 312 direct children of `<Table>` and 91 direct children of a distinct `<TableTemplates>` sibling; a from-scratch stack-based well-formedness check of the whole file found zero mismatched tags, ruling out a parser bug as the explanation.]**

**Bijection with the real saves.** Hashing (CRC-32, lower-cased, §1.3) the 312 real `<Table>` row `Name`s and comparing the SET against the 312 saved base-slot ids at `0x4428` in all 16 real snapshots: **the two sets are IDENTICAL in 16/16 snapshots** (no duplicate hashes on either side; 0 overlap with `patch_unlockables.xtbl`'s 6 hashes). **This corrects §4.1's "so not every row is saved" (and the identical claim in `spec-save-format.md` §14): every one of the real table's 312 rows is a saved slot, and every saved slot is one of the real table's 312 rows — a full one-to-one correspondence, not a subset relationship.** The base slot ORDER is still not the file order (§4.1's existing correction on that point stands, unaffected by the row-count fix).

**Capacity.** Real base + patch + all-DLC total = 312 (`unlockables.xtbl`) + 6 (`patch_unlockables.xtbl`) + 56 (all three DLC combined, per the already-confirmed V2.11/V2.12 count) = **374, against the loader's 384-record capacity — 10 slots of headroom, never approached.** **[CONFIRMED — empirical.]**

**`patch_unlockables.xtbl` itself:** confirmed 6 rows (`Community_Row_Shirt`, `Community_Logo_Shirt`, `Community_Fleur_Hoodie`, `Community_3rd_Street_Hoodie`, `Community_Jerseys`, `Community_Antennae` — byte-for-byte the already-known names); its own `<TableTemplates>` sibling is present but **empty** (0 children), unlike the main table's populated one.

**Coverage check (real `<Unlockable>` rows).** Every top-level element name observed (`Name`, `Type`, `DisplayName`, `Description`, `Image_Source`, `Event_Text`, `Category`, `Detailed_Description_Text`, `Price`, `Priority`, `Is_DLC`, `Auto_Unlock`, `_Editor`) is already documented in §4.2 — **0 undocumented elements.** `Framework` never appears (all 312 rows are implicitly `main`, consistent with §1.4's default).

**Real value distributions (312 rows), none seen in the 56-row DLC sample at anywhere near this breadth:**
- **Type distribution — 44 of the 61 types are exercised** (the DLC sample only reached 7): `Ammo_Multiplier` 28, `Homie`/`Vehicle` 21 each, `Custom` 24, `Damage_Resist` 20, `Respect_Bonus_Modifier` 17, `Crib_Weapon` 15, `Reminder` 14, `Notoriety` 13, `Gang_Customization` 11, `Outfit` 12, `Crib_Cash_Stronghold_Scalar`/`Crib_Cash_Limit_Scalar` 12 each, `Weekly_Payments`/`Sprint_Bonus`/`Cash_Bonus_Modifier`/`Crib` 5 each, `Clothes` 7, `Crib_Ammo` 8, `Health`/`Player_Health_Bonus` 4 each, `No_Reloading`/`DBNO_Extra_Time`/`Muscles`/`Melee_Damage_Bonus`/`Reload_Speed_Bonus`/`Special_Homie_Health_Bonus`/`Gang_Weapons`/`Revive_Hold_Time_Modifier`/`Pay_Cash_for_Respect`/`Discount` 3 each, `NPC_Cash_Drop_Modifier`/`Dual_Wield`/`Auto_Complete_City_Takeover_District` 2 each, `Pickpocket`/`Lump_Sum_of_Money`/`Vehicle_Customization`/`Character_Customization`/`Auto_Complete_City_Takeover_All`/`Explosive_No_Ragdoll`/`Gang_Customization_Unlock`/`Vampire`/`Collectable_Finder` 1 each. **17 of the 61 registered types are never used by the real base table** (only usable, if at all, via a DLC row or `patch_unlockables.xtbl`). *[Count check 2026-09-30: the list above names 43 types whose counts sum to 308, not 44 types / 312 rows; one type (4 rows) appears to be missing from the list — not re-extracted, so 44/17 is left as stated.]* *[Desk review 2026-09-30: the 312-row total is proven by the spec's own sums — Category 61+53+44+36+30+24+25+19+12+8 = 312 and `Is_DLC` 245+28+39 = 312 — so the type list is short by 312 − 308 = 4 rows. The numbers do not show whether those 4 rows are one more type (44 used / 17 unused) or belong to listed types (43 / 18).]* **[OPEN — desk review 2026-09-30: the per-type tally of the 312 rows; to be settled against real data.]**
- **Category:** `Strongholds` 61, `Weapons` 53, `Player Abilities` 44, `Homies` 36, `Customization` 30, `Damage` 24, `Discounts` 25, `Vehicles` 19, `Activities` 12, `Health` 8 (sums to 312).
- **`Is_DLC`:** absent (=False) 245, `Sometimes` 28, `True` 39.
- **`Auto_Unlock`:** absent (=0) 311, `Silently` 1 — the base table exercises only one of the four spellings; `Loudly`, `Loudly (Helipad)`, `Silently (Last)` are unused here.
- **The `M17_Vehicle_STAG_Tank_Upgrade` special case (§4.1) is CONFIRMED to fire for real**, upgrading its prior "HIGH CONFIDENCE — not exercised" label: the row exists in the shipped table (its authored `Is_DLC` is absent/False, forced to `2`/"Sometimes" by the special-case store).

### 14.2 `achievements.xtbl` — two different files with the same name, and the 83-slot window filled exactly

`misc_tables.vpp_pc`'s copy of `achievements.xtbl` has **53** `<Achievement>` rows. **`patch_compressed.vpp_pc` also contains a file literally named `achievements.xtbl`, with 83 rows** — a strict superset of the 53-row copy except for one renamed row (`Love/Hate Relationship` in the 53-row copy → `Love Hate Relationship` in the 83-row copy, an apparent typo fix), plus all **30** of the already-known DLC achievement names, each carrying an explicit `<Framework>dlc1|dlc2|dlc3</Framework>` (10 apiece) that the 53-row copy's rows never carry (default `main`). **This resolves §3.1/§3.6's "whether base + DLC fit the 83-record window" — it fits EXACTLY, with 53 + 30 = 83 and zero slots of slack** (the achievement record array has no bound check, §3.1, so an 84th row would overflow into the adjacent handler-table memory). **[CONFIRMED — empirical for the row counts, the superset relationship, and the Framework tagging.]**

~~**Honest gap, flagged rather than guessed at (per `HANDOFF.md` §36 (archived §27.8)): which of the two same-named `achievements.xtbl` files the running game actually loads was NOT traced this pass.**~~ **RESOLVED 2026-09-24 (see below): traced from the archive-mounting side, as this gap asked; reading (a) below is the one the code implements.** Only one `achievements.xtbl` string literal exists in the executable (§1.2) and the table-open helper resolves a filename through whatever the archive-mounting layer hands it; this pass extracted directory entries from both archives but did not trace the mount-order/precedence code that decides which physical bytes a same-named lookup returns across `misc_tables.vpp_pc` vs `patch_compressed.vpp_pc`. Two readings are both consistent with the evidence and NEITHER is asserted here: (a) the patch archive's copy wins and already contains the DLC rows pre-merged, in which case the separate per-DLC achievement loader (`FUN_00714430` on `<framework>_achievements.xtbl`) would be redundant for achievements specifically (but harmless, since those are distinctly-named files); or (b) the base archive's 53-row copy is what actually loads and the 83-row `patch_compressed.vpp_pc` copy is unused leftover content, with the 30 DLC rows arriving only via the separate per-DLC loader as §3 already documented. Resolving this needed the archive mount-order/precedence mechanism, which was `spec-vpp-container.md` territory and has now been traced (immediately below). ~~**[OPEN — do not treat this as resolved by the counts above; only the counts and the superset relationship are confirmed.]**~~ **[RESOLVED — see below; the counts, the superset relationship, and the winning copy are now confirmed/high-confidence together.]**

**RESOLVED 2026-09-24 (archive mount-order/priority, traced from the game's own boot code, not assumed from genre convention).** Checked the boot sequence directly in Ghidra (headless), starting from the executable's own list of top-level `.vpp_pc` archive names. All 35 base-game archives (no loose manifest file governs this -- checked the install directory for one; none exists outside the executable itself) are opened from a single fixed in-executable registry table (one 12-byte record per archive: a name pointer, an 8-bit flags byte, and one more field), walked by three boot-time mount passes, each gated on a different flags bit and each iterating the table forward from its first entry to its last. `misc_tables.vpp_pc` and `patch_compressed.vpp_pc`/`patch_uncompressed.vpp_pc` are all opened by the *same* pass (the flags-bit-`0x2` pass) in that same forward order, and `misc_tables.vpp_pc` sits near the start of the table while `patch_compressed.vpp_pc` and `patch_uncompressed.vpp_pc` are literally the last two entries of all 35 -- **`misc_tables.vpp_pc` is always fully mounted before `patch_compressed.vpp_pc` even opens, every boot, unconditionally. [CONFIRMED -- disassembly.]**

Mount order alone doesn't decide a same-name lookup by itself, so the actual by-name resolution path was traced too. `patch_compressed.vpp_pc` and `patch_uncompressed.vpp_pc` turn out to be the *only two* of all 35 boot-manifest archives whose registry-flags byte carries one particular bit; every other archive, including `misc_tables.vpp_pc`, lacks it. That bit, tested identically in all three boot-mount passes, triggers a dedicated post-open step -- found in none of the other 33 archives' code paths -- that re-resolves the just-opened container by name and inserts its handle into a small, fixed-size priority list. A full cross-reference sweep of the entire executable found exactly three call sites for that insertion, all three inside these boot-mount passes, all three passing the same hard-coded destination slot: this priority list is populated *exclusively* by the two patch archives, unconditionally, every boot, and by nothing else in the shipped game. A separate function performs the actual by-name lookup used when a game system asks for a resource by filename: a linear, case-insensitive search that checks this small priority list *first*, before falling back to the ordinary mounted-archive collection -- and that lookup is reached from the engine's general extension-keyed, named-resource-open dispatcher. **[CONFIRMED -- disassembly, for the mount-order fact and for the patch-exclusive priority-list mechanism's existence, construction, and search-order. HIGH CONFIDENCE, not individually re-traced per table, that `achievements.xtbl`'s own loader (`FUN_00713BB0`) reaches this same general dispatcher rather than some other, untraced path -- the dispatcher's design (a generic, extension-keyed table of location providers, reused for other extension-typed content elsewhere in the executable) makes this very likely, but the exact hop from the table loader's own open call down to the dispatcher was not walked instruction-by-instruction this pass.]**

Net effect: **`patch_compressed.vpp_pc`'s 83-row copy of `achievements.xtbl` is the one actually loaded**, not the 53-row `misc_tables.vpp_pc` copy -- resolving both readings (a)/(b) above in favor of (a). This also closes the loop with this item's own row-count evidence: 53 (misc_tables) + 30 (DLC, added at runtime by the separate per-DLC loader, §3) = 83 exactly, with zero slack against the documented capacity, is only a meaningful coincidence if the 83-row copy is genuinely what loads -- which the mount/priority mechanism above now independently confirms from the loader side, as this item asked. *(Desk review 2026-09-30: the count argument does not discriminate — with the base pass filtering `Framework` = `main` (§1.4), either copy yields 53 base rows and the DLC loaders add the same 30, so 53 + 30 = 83 holds in both readings; the conclusion rests on the mount/priority trace alone, which is HIGH CONFIDENCE for this loader per the paragraph above.)* **Generalizes, by construction:** the mechanism is archive-level (which container's entry a name resolves to), not file-specific, so it applies identically to every same-named pair between `misc_tables.vpp_pc` and `patch_compressed.vpp_pc`/`patch_uncompressed.vpp_pc` -- `mission_checkpoints.xtbl` and `spawn_info_ranks.xtbl` (§14.11/§14.12) resolve the same way (patch copy wins) by the same trace, and this also confirms the `patch_compressed.vpp_pc` copies already treated as authoritative elsewhere in this project's specs on independent (row-count/capacity) grounds -- `ammo.xtbl`/`continuous_explosions.xtbl` in `spec-tables-weapons-combat.md` §18, `camera_free.xtbl`/`bitmap_sheets.xtbl` in `spec-tables-environment.md` §17.4-§17.5 -- were the right call.

**Coverage and real values (53-row base copy).** Element names observed on real rows: `Name`, `DisplayName`, `Requirements`, `HudUpdateFrequency`, `HudUpdateNumSuppress`, `Image` (50/53), `Avatar_award` (3/53: "Dead Presidents", "Flash The Pan", "Jumped In"), `PC_Only`, `_Editor` — all already documented in §3.2, **0 undocumented elements.** **`Requirements` really is populated in real base content** (contrast §3.5's DLC-only finding that DLC rows always have empty `Requirements`): **35 of 53 base achievements carry ≥1 real `Requirement`**, referencing stat names such as "total collectibles found", "challenges completed", the four homie city-takeover counts, and the eight activity "levels completed" stats (`fraud`, `drug trafficking`, `heli assault`, `mayhem`, `escort`, `snatch`, `human torch`, `tank mayhem`) — i.e. the stat-requirement linking mechanism of §3.3 is genuinely exercised by retail content, not just schema-complete. `HudUpdateFrequency` real distribution: `0` (no HUD bar) 35, `5` → 9, `10` → 5, `100` → 2, `1` and `15` → 1 each.

### 14.3 `stats.xtbl` — the `Value_Type` open item, resolved in aggregate

217 `<Stat>` rows confirmed (matches §2.1 exactly; array fully populated, no missing rows — resolving §2.6 item 2). Real `Value_Type` distribution over all 217 rows, resolving §2.6 item 1: **`integer` 116, `complex` 44, `distance` 21, `money` 16, `percent` 10, `time` 9, `float` 1, `boolean` 0.** **`boolean` is entirely unused by the shipped table** — no stat in the real base file selects the `boolean` value type, despite it being one of the eight tested children. The 10 `percent` stats' denominators (the `Percentage_Of_Stat` text, §2.2), previously unknown: `pistol hit pct`→`pistol shots fired`, `smg hit pct`→`smg shots fired`, `shotgun hit pct`→`shotgun shots fired`, `rifle hit pct`→`rifle shots fired`, `rpg hit pct`→`rpg shots fired`, `thrown hit pct`→`thrown weapon uses`, `sniper hit pct`→`sniper shots fired`, `special hit pct`→`special ammo used`, `shot hit pct`→`shots fired`, `mission success rate`→`missions attempted`. **[CONFIRMED — empirical.]** Per-id classification for the 173 non-complex ids is now recoverable from `tools/ai_base_xtbl/stats.xtbl` directly; not transcribed row-by-row here.

### 14.4 `cheats.xtbl` — the "151" figure explained, and DLC cheats found inside the base table

**Correction to `spec-save-format.md` §14 (struck through in place there).** The real `cheats.xtbl` has the same four-sibling root shape as `unlockables.xtbl` (§14.1): `<Table>` (**97** real `<Cheats>` rows — the ones the loader's `Table`-only accessor actually reads) and `<TableTemplates>` (**54** more `<Cheats>`-named rows, disjoint hash set, never read by any loader). **97 + 54 = 151 — exactly the previously-reported "151 `UnlockString` values"**, which counted both blocks together rather than the loader's real, active set. **Empirical confirmation that only the 97 `<Table>` rows are ever actually active:** every one of the **23** distinct active-cheat ids observed across all 16 real snapshots hashes to a `<Table>` row (23/23); **zero** hash only to a `TableTemplates`-only row. **[CONFIRMED — empirical, both the count split and the behavioural check.]**

**This also resolves §8.4's "whether DLC cheat tables exist" — no separate per-DLC cheat file exists; DLC cheats are simply 7 rows of the single base `<Table>` marked `<Is_DLC>True</Is_DLC>`:** `InfiniteMass` (`dlc_car_mass`), `Super Saints` (`dlc_super_saints`), `Never Die` (`dlc_never_die`), `Michael Bay Mode` (`dlc_super_explosions`), `Player Pratfall` (`dlc_player_pratfalls`), `Unlimited Ammo` (`dlc_unlimited_ammo`), `Unlimited Clip` (`dlc_unlimited_clip`) — these are exactly the `dlc_*` `UnlockString`s `spec-save-format.md` §9.5 previously resolved only from executable string literals.

**Coverage (97 real `<Table>` rows):** elements observed — `Name`, `UnlockString`, `Type`, `DisplayName`, `Interface_Category`, `Cheat_Description`, `Dont_Flag_As_Cheating`, `Is_DLC` (7/97) — all already documented in §8.2, **0 undocumented elements.** `Dont_Flag_As_Cheating=true` count: **0** — no shipped cheat sets this flag. `Type` distribution: `Vehicles` 38, `Weapon` 36, `Gameplay_Physics` 18, `Weather` 4, `AI` 1 — **`Time` (the sixth documented `Type` child) is never used by any real base cheat.** `Gameplay_Physics` actions actually used (18 of the ~~26~~ 27 documented names, §8.2 — recounted 2026-09-30): `Elevator to Heaven`, `Add/Remove Police Notoriety`, `Add/Remove Gang Notoriety`, `Instant Cash (+$100000)`, `Instant Respect (+100000)`, `Infinite Sprint`, `Super Saints`, `Never Die`, `Super Explosions`, `Bloody Mess`, `Zombie/Mascot/Pimps and Hos Peds`, `Golden Gun`, `Player Pratfalls`, `Unlimited Clip`.

**Partial resolution of §8.4's "the five interface-category names":** the runtime *indices* are still unrecoverable (§8.3's static zero-filled array is unaffected by reading the XML), but the raw `Interface_Category` text itself gives the five author-facing category **names** for the first time: `Vehicles` (35), `Weapons` (34), `Gameplay` (13), `World` (11), `Weather` (4) — five distinct strings, matching the "five category hashes" cardinality exactly. **[CONFIRMED — empirical for the name strings; the index-to-name mapping remains OPEN as before.]**

### 14.5 `respect_levels.xtbl` — all 50 levels, resolving §5.3

50 `<respect_level>` rows confirmed (matches §5.1's array bound exactly). Real per-level data (level = row position, 1-indexed as §5.2 describes):

| Level | Respect | # Unlockables | First unlockable(s) |
|--:|--:|--:|---|
| 1 | 2115 | 0 | — |
| 2 | 2230 | 5 | Sprint_1, Damage_Bullet_1, Notoriety_Mstar_1 |
| 3 | 2345 | 5 | Upgrade_Ammo_Pool_SMG_1, Health_Regen_1, Damage_Vehicle_1 |
| 4–9 | 2460…3035 (+115/level) | 4–5 | (weapon-pool/homie/cash unlocks — see `tools/ai_base_xtbl/respect_levels.xtbl`) |
| 10–20 | 3150…4300 | 1–4 | (steadily fewer per level as content thins out) |
| 21–30 | 4415…5450 | 1–3 | |
| 31–49 | 5565…7635 | 1–2 (mostly 1 from level 32 on) | |
| 50 | **0** | **8** | Unlimited_Ammo_Explosives, Unlimited_Ammo_Grenades, Unlimited_Ammo_Pistol, … |

`Respect` increases by a constant **115** per level from level 1 through 49 (2115, 2230, 2345, … 7635 = 2115 + 115·(n−1)); **level 50's `Respect` is `0`** (the max level costs nothing further to "reach" beyond level 49 — consistent with §5.2's own note that the cumulative-total register subtracts the last level's value). Sum of all 50 `Respect` fields = **238,875**; per §5.2's formula (Σ minus the last level's value, which is 0 here) the cumulative-total register `0x014B031C` = **238,875**. Level 50 alone carries 8 unlockables (the "unlimited ammo" family across every weapon slot) — the single largest per-level grant in the table. **[CONFIRMED — empirical; resolves §5.3's "per-level `Respect` values and unlockable lists" for the base table. The capacity constant of 20 ids/level (read from the constructor, not exercised by any real level here — the largest real level uses 8) remains as previously stated, not independently re-derived.]**

### 14.6 Notoriety family — `notoriety.xtbl`, `notoriety_levels.xtbl`, `notoriety_spawn.xtbl`

**`notoriety.xtbl` (30/30 `<Entry>` rows, matching the 30-name table of §6.1 exactly).** Real per-activity values now known for all 30 (full table in `tools/ai_base_xtbl/notoriety.xtbl`); representative examples: `helicopter destroy` → police-only, 500 points, window levels 4–5; `armored truck assault` → police-only, 100 points, levels 2–3; `govt heli theft` → police-only, 250 points, levels 4–5; `atm extortion` → police 200 points levels 2–5 (gang 0 outside level 0–1); `enter owned store` → **both tracks −20 points** (the only negative-point activity — entering an owned store *reduces* notoriety); `gang alert`/`gang kill` → gang-only, 75 points; most "civilian *" activities are police-only (gang points 0). `Delay` is **500** (ms, ×1000 already applied per §1.3's unit note — i.e. the raw XML integer is milliseconds directly, not seconds; the ×1000 conversion documented for spawn timers does not apply here) for 28/30 activities, **100** for `public nudity`/`atm extortion`, and **0** for `vandalism`/`enter owned store`. `Check_Detection=true` appears on 12/30 (both `police` and/or `gang` sub-blocks): `burglary`, `carjacking` (police only), `civilian assault/shooting/kill/squirting`, `govt car theft`, `police alert`, `car assault` (both), `mover dislodge`, `vandalism`. *[Count check 2026-09-30: this list names 11 activities against the stated 12; one is missing or the count is 11 — not re-extracted.]* **[CONFIRMED — empirical; resolves the "values untraced" half of §6.1 for all 30 activities.]**

**`notoriety_levels.xtbl` (2 `NotorietyLevels` sets × 6 levels each, matching §6.2 exactly).**

| Level | Set 0 limit / decay / 1st-decay(s) | Set 1 limit / decay / 1st-decay(s) |
|--:|---|---|
| 0 | 0 / 1 / 15 | 0 / 1 / 15 |
| 1 | 75 / 5 / 15 | 75 / 6 / 15 |
| 2 | 225 / 10 / 15 | 300 / 15 / 15 |
| 3 | 500 / 20 / 15 (roadblock 15–30s) | 700 / 25 / 15 (roadblock 15–30s) |
| 4 | 1200 / 40 / 15 (roadblock 10–20s) | 1200 / 50 / 15 (roadblock 10–20s) |
| 5 | 2500 / 50 / 60 (roadblock 8–15s) | 2500 / 75 / 60 (roadblock 8–15s) |

Both sets share identical level-0/1/4/5 limits, `NotorietyFirstDecay` and roadblock windows; they diverge only in **decay rate at levels ~~1–3~~ 1–5** *(corrected 2026-09-30: the table above and the figures in this sentence differ at levels 4 and 5 too)* (set 1 decays faster: 6/15/25/50/75 vs set 0's 5/10/20/40/50) and in the **level-2/3 limits** (set 1 needs more points: 300/700 vs set 0's 225/500). **`BruteSpawnTimeMin`/`Max` are `-1` (disabled) at every level in both sets** — bruteS never get a level-gated spawn timer from this table in the shipped game, a real, confirmed-empty field rather than an extraction gap. **Which set is police vs gang remains OPEN as before** (positional only; the faster-decay/higher-threshold set (1) is *consistent with* a "police" reading — attention escalates more slowly but fades faster — but this is not traced to a consumer and is offered as an observation, not a finding.) **[CONFIRMED — empirical for all values; the police/gang labeling stays HYPOTHESIS/OPEN, per `HANDOFF.md` §36 (archived §27.8) (the "when confirmed facts run out, defer rather than invent" rule), since no consumer was traced.]**

**`notoriety_spawn.xtbl` — 25 groups, not 24, and a structural correction to §6.3.** The real file has a **25th named group beyond the 24 destination-structure names of §6.3: `stag_speedboat`** (flags `Spawn on Water`, `Active With Stag`; 4 `level_info` records for levels 2–5; 7 `group_info` entries). Whether a 25th runtime destination structure exists or this row is simply dropped (no matching name in the 24-entry lookup, §6.3) was not traced — flagged rather than guessed. **Structural correction:** `level_info`, `group_info` and `group_details` are each, like the row wrapper `Table` itself, a **container whose own tag name is repeated for every child record** — i.e. the real shape is `<level_info><level_info>…</level_info><level_info>…</level_info>…</level_info>` (and likewise for the other two), not the flat repeated-sibling shape §6.3's ASCII art suggested. This matches — and is explained by — §1.3's own accessor grammar (`FUN_00DC5150` counts children of a given name, used to size `spawn_info_*`/`notoriety_spawn`/`store_discounts` arrays): the loader finds the single wrapper child by name, then counts/iterates *its* children of the same name. **[CONFIRMED — empirical, from-scratch stack trace of the raw file around several rows.]**

Real per-group record counts (`level_info` / `group_info` / `group_details`): `police` 4/9/10, `police_ng` 4/11/10, `luchadores` 4/33/49, `deckers` 4/32/45, `morningstar` 4/30/33, `whored_one` 4/13/17, `survival_mascots`/`survival_bikers` 4/24/each, `stag` 4/10/9, smaller STAG/police variant groups (heli/tank/riot/attack_heli/blackhawk/waverunner/speedboat/motorcycle/attack_heli) 1–4/1–7/1–10, `police_heli` 1/1/3 (level 3 only). Totals across all 25 groups: **80 `level_info` + 254 `group_info` + 263 `group_details` records.** **[VALIDATED-BY-DATA 2026-10-01: Team B's reader on this §14.6 nested tree (job `20261001T003803-team-b-upsq`; `team-b/HANDOFF.md`, item "Job 07": "80 / 254 / 263 level_info / group_info / group_details records, exactly the spec's §14.6 text; 25/25 rows nested") reproduces all three totals. So the 80 stands, and the "22/25 groups cover levels 2–5" sentence below is the figure in doubt (OPEN, see the next note).]** **[OPEN — desk review 2026-09-30: these counts do not agree with the next sentence. If 22 of 25 groups cover levels 2–5 with one `level_info` per level, that is 22 × 4 = 88, plus 3 exceptions × 1 = 91, not 80. The groups listed individually above already give 10 × 4 + 3 × 1 = 43, which leaves 80 − 43 = 37 for the remaining 12 groups, fewer than 12 × 4 = 48. So either "22/25" or "80" is wrong, or "contiguous range 2–5" includes sub-ranges; the per-group counts are to be settled against real data.]** `override_group` is used by exactly 2 of 25 (`police` and `police_waverunner`, both → `police_motorcycle`). Levels covered per group are almost always the contiguous range 2–5 (22/25 groups); the three exceptions are `police_heli` (level 3 only), `stag_tank`/`police_tank` (level 5 only) — i.e. tanks and the police helicopter are reserved for the highest wanted tiers. Sample real `police` level-2 record: `min/max_spawn_time` 25/35s, `veh_min/max_spawn_time` 10/20s, `max_vehicle_occupants` 2, `max_vehicles` 1, `npc_cap` 4, `specialist_cap`/`brute_cap` 0; by level 5: 15/20s spawn window, 4 occupants, 3 vehicles, 8 NPCs, 2 specialists. **[CONFIRMED — empirical; resolves the "Table-nesting… needs one real sample" half of §6.4 and gives real per-level spawn parameters that were previously fully OPEN.]**

### 14.7 `difficulty_levels.xtbl` — both sets' real values

Both `Dynamic_Difficulty` blocks present (matching §7.1's "at most 2" exactly), each with 3 `Difficulty_Level` rows and 4 `Rank_Occurrence_Element` rows.

| Field | Set A L0/L1/L2 | Set B L0/L1/L2 |
|---|---|---|
| `Damage_Received_Mult` | 0.3 / 0.75 / 1.5 | 0.5 / 0.95 / 1.75 |
| `Damage_Dealt_Mult` | 1.1 / 1.0 / 0.75 | 1.0 / 0.9 / 0.6 |
| `Health_Regen_Speed_Mult` | 1.3 / 1.0 / 1.0 | 1.1 / 1.0 / 1.0 |
| `Notoriety_Decay_Mult` (raw; stored as 1/x per §7.2) | 1.3 / 1.0 / 0.7 | 1.3 / 1.0 / 0.7 |
| `Player_Ram_Delay` (s, ×1000→ms per §7.2) | 1.0 / 0.24 / 0.06 | 0.36 / 0.12 / 0.03 |
| `Brute_Health_Scalar` | 0.5 / 1.0 / 1.25 | 0.5 / 1.0 / 1.5 |
| `Friendly_Health_Scalar` | 2.0 / 1.0 / 1.0 | 2.0 / 1.25 / 1.0 |

Both sets follow the same L0=easy/L1=normal/L2=hard shape (damage received climbs, damage dealt and health-regen speed fall as the index rises); **Set B is uniformly harsher at L2** (1.75× damage received vs Set A's 1.5×, 0.6× damage dealt vs 0.75×) and gentler at L0 in some fields (`Damage_Received_Mult` 0.5 vs 0.3) — consistent with, but not confirmed as, a single-player-vs-co-op split (§7.3's own HYPOTHESIS is not upgraded here, no consumer traced). `Rank_Occurrence_Element` rows are identical between the two sets: an unnamed default row (`Rank1=1.0`, all others 0), then `m05` (0.2/0.6/0.2/0), `m10` (0.05/0.25/0.5/0.2), `m17` (0/0.1/0.2/0.7) — i.e. only three specific missions (`m05`, `m10`, `m17`) override the default all-rank-1 occurrence weighting, and the override shifts steadily toward higher ranks as the story progresses. **[CONFIRMED — empirical; resolves §7.3's per-level values; the set-A-vs-set-B semantic question is left as before, per `HANDOFF.md` §36 (archived §27.8) (the "when confirmed facts run out, defer rather than invent" rule).]**

### 14.8 `collectibles.xtbl` and `store_discounts.xtbl`

**`collectibles.xtbl`:** all 4 rows present (`Drug Package`, `Money Pallet`, `Sex Doll`, `Photo Op`). Every one uses the **identical** reward shape: top-level `Cash_Award=1000` (no top-level `Respect_Award`), and exactly 4 `Threshold_Reward`s at thresholds **5/10/15/20**, each `Cash_Award=1000`, `Respect_Award=0`, **no `Unlockable`** on any of the 16 threshold rows — collectible rewards in the shipped base game are pure cash, uniform across all four types, with no unlockable ever granted this way. **[CONFIRMED — empirical; resolves §9.3's "base values" gap completely for this table.]**

**`store_discounts.xtbl`:** 31 `<StoreDiscounts>` rows — **confirms `spec-save-format.md` §12.5's independently-derived "31 defined discounts" exactly (row-for-row, not just count-for-count).** 16 of the 31 stores have real `DiscountElement`s (2–3 each; the other 15 have none authored). Every real `DiscountName`/`RadioEvent` pair follows a consistent naming convention (e.g. `Impressions15`/`COM_IMPRESSIONS_SALE`, `Bling_Bling_25`/`COM_BLINGBLING_SALE`) consistent with §10.1's shop-name-namespace hypothesis, though the namespace table (`shop_names.xtbl`) itself was not read this pass. `Amount` ranges 0.15–0.70 (as a fraction, per §10.1); `Triggered` (the chance the offer fires) ranges 0.05–0.80. **[CONFIRMED — empirical.]**

### 14.9 `gameplay_constants.xtbl` — real values, a confirmed dead-field bug, and six orphaned top-level sections

**A structural coverage gap: six top-level sections exist as DIRECT children of `<Gameplay_Constants>` that no documented reader path visits.** §10.2 traces the loader walking specific named parents (`Idle_AI` → `Crime_scene`/`Drunk`/`Sidewalk`; `Combat_AI` → `Gun` → `Pepperspray` **[OPEN — desk review 2026-09-30: §10.2's table lists `Pepperspray` beside `Gun` under `Combat_AI`, not inside `Gun`; neither section says it corrects the other; to be settled against the executable or the real file.]**, and `Combat_AI` → `Gunfire_Evade`/`Bust`). The real file ALSO carries **top-level** `Crime_scene`, `Drunk`, `Bust` and `Gunfire_Evade` siblings of `Idle_AI`/`Combat_AI` (not nested under them) — each with genuinely DIFFERENT values from their correctly-nested, documented-as-read twin (e.g. top-level `Drunk.Drunk_action_type` gives `drunk_taunt_pct=45`/`stumble=10`/`vomit=10`/`stand=30`/`drink_more=5`, while `Idle_AI.Drunk.Drunk_action_type` gives `25/25/5/25/20` — two different, both internally-consistent-to-100 distributions under the same names). Plus two top-level sections never mentioned in §10.2 at all: **`throwing_constants`** (`player_on_foot_throw`, `on_foot_toss`, `in_vehicle_throw`, `npc_on_foot_throw`) and **`Human_Fine_Collision`** (eight `*_Width` floats for head/neck/torso/groin/upper-lower arm/leg). **Given the loader binds each name to one specific named parent (§10.2's own per-section table), the top-level copies are, on the documented reader alone, unread — most likely stale authoring leftovers from a restructuring that moved these blocks under `Idle_AI`/`Combat_AI` without deleting the old top-level copies. This is flagged as an honest gap, not asserted: it is possible an additional, untraced 21st/22nd sub-reader (only 20 were traced per §10.2's own heading) reads one or more of these top-level names directly.** **[CONFIRMED — empirical that these six sections exist and are distinct from their nested twins; HIGH CONFIDENCE, not CONFIRMED, that they go unread, since the "20 sub-readers" figure was not re-verified as exhaustive this pass.]**

**A confirmed authoring/code mismatch, now with real data on both sides.** §10.2 already documented, from disassembly alone, that `Combat_AI → Gun → Pepperspray` *(parent OPEN, see above)* reads the element `Spray_Min` **twice** into two different destinations (`0x1534`/`0x1538`) and never reads a `Spray_Max` element. The real file authors a **genuine, distinct `<Spray_Max>3000</Spray_Max>`** alongside `<Spray_Min>2000</Spray_Min>` — i.e. the table's author clearly intended two different values, and the shipped code silently discards the authored `3000` and uses `2000` for both. **[CONFIRMED — disassembly (§10.2) + empirical (this pass): the bug is real on both sides, not just a plausible-looking code quirk.]**

**Representative real values** (full tree of ~230 leaves in `tools/ai_base_xtbl/gameplay_constants.xtbl`): `panic_fall_velocity`=12; `FriendlyFirePercentage`=25 (→ 0.25 stored, §10.2); `player_health.restore_per_sec_sp`=125, `restore_wait_time_ms`=6000; `Fire.Max_Player_Burn_Time_Ms`=2000; `Death_and_busted.respawn_delay_ms`=4000; `Melee_attack.Power_Attack_Angle`=360 (a full circle — the half-angle-cosine store, §10.2, becomes cos(180°)=−1); `Cribs.Money_Storage` gives the crib-count → max-stash schedule directly: 0 cribs→$0, 1→$5,000, 2→$15,000, 3→$30,000, 4→$50,000, 5→$75,000, 6→$105,000, 7→$140,000, 8→$180,000, 9→$225,000, 10→$500,000 (an 11-entry table, well within the documented layout — the jump from $225k to $500k at 10 cribs is the single largest step); `Fight_Club.Finisher_Rates` gives NPC finisher chance falling 2.0→1.6→1.2→0.8 and player's rising 0.8→1.6→2.0→4.0 as `Num_Failures` climbs 0→3 (the game makes finishers progressively easier for the player and harder to trigger against as a fight drags on); `Coop_Meta_Game.Reward_Cash`=500, headshot=50 points, nutshot=30, melee/quick kill=25, explosion=10, player death=**−200**. **[CONFIRMED — empirical for every value transcribed; the consumers remain untraced as before, §10.2/§13.2 item 9.]**

### 14.10 `gameplay_nags.xtbl` / `Gameplay_nag_globals.xtbl`

All 20 documented names present (§10.3), real `Increment`/`Nag_Time` (both already in the ×1000-ms convention, i.e. `human_shield`: Increment=1→1000ms, Nag_Time=600→600,000ms=10 min): `human_shield`/`cruise_control` 1s/10min; `fine_aim` 1s/5min; `power_attacks` 2s/5min; `taunting` 1s/20min; `sprinting`/`grenades`/`weapon_upgraded`/`radio_controls`/`low_ammo`/`notoriety`/`grenades_pc` Increment=0 (count-only, no per-use time credit) at Nag_Time 5–30min; `city_takeover`/`hitman`/`chop_shop` 30min; `gang_customize`/`change_clothes` 2 hours; `crib_stash` Increment=100/Nag_Time=1s (a near-instant, dollar-denominated nag rather than a time one — *HYPOTHESIS (desk review 2026-09-30): by §10.3's loader rule `Increment` is stored × 1000 as ms, i.e. 100,000 ms; the dollar reading is an interpretation*); `sprint_recharge` Nag_Time=5s. **`Gameplay_nag_globals.xtbl` real values: `Mission_postpone_S`=30, `Nag_display_postpone_S`=300 — CONFIRMS, with real data for the first time, §13.1's inference that "the '300 s' figure is a value from the file, not a default of the function"** (the file's `Mission_postpone_S`=30 exactly matches the loader's own 30s preset — a no-op override — while `Nag_display_postpone_S`=300 genuinely overrides the function's 30s preset tenfold). Note the real element names carry a capital `S` (`Mission_postpone_S`, not `_s`); matching is case-insensitive so this doesn't change behaviour, only the transcription in §10.3. **[CONFIRMED — empirical.]**

### 14.11 `mission_checkpoints.xtbl` and `mission_help.xtbl`

**`mission_checkpoints.xtbl`:** `misc_tables.vpp_pc`'s copy has **139** rows across 29 distinct missions; **`patch_compressed.vpp_pc` carries a second copy of this filename too, with 166 rows — a strict superset of the 139 (0 removed, 27 added) — and 166 is exactly §10.4's documented capacity (`0xA6`/166 rows kept).** As with achievements (§14.2), this is the same "two same-named files, patch copy is a capacity-exact superset" pattern, and the same mount-precedence mechanism applies: the `patch_compressed.vpp_pc` 166-row copy is the one actually loaded (RESOLVED 2026-09-24, not re-argued here, see §14.2's resolution). `Debug=True` on 7/139 real rows (base copy). **`mission_help.xtbl`:** 32 `<String>` rows containing **265** `<Text>` records total; 261/265 use the `DisplayText` fallback rather than `English` (confirming §10.5's fallback path is the dominant real-world case, not the exception); 185/265 have a negative `Duration` (→ FLT_MAX "no timeout" per §10.5) — most mission-help text has no timeout. **[CONFIRMED — empirical.]**

### 14.12 `spawn_info_categories.xtbl`, `spawn_info_groups.xtbl`, `spawn_info_ranks.xtbl`

**Categories:** 31 real rows; **all five run-time-special-cased names of §10.6 are present** (`Default Spawn`, `Start Spawn`, `sp_cat_Cheat_Ho`, `sp_cat_Cheat_Mascot`, `sp_cat_Cheat_Zombie`) — **[CONFIRMED — empirical]**, plus 26 per-district categories (`sp_cat_CutterAirport`, `sp_cat_SierraPoint`, …) and one `Test_`-prefixed category named after a developer (an apparent leftover test category; name withheld 2026-09-30). **[OPEN — desk review 2026-09-30: 5 + 26 + 1 = 32, not the 31 rows stated; one of the three figures is wrong; to be settled against real data (recount `spawn_info_categories.xtbl`).]** Element coverage matches §10.6's documented tree exactly, 0 undocumented elements.

**Groups:** 61 real rows. `Team` distribution: `Civilian` 53, `Police` 3, one each of `STAG`/`Morningstar`/`Deckers`/`Playas`/`Luchadores`. **`Spline_Type` real distribution surfaces an enum-coverage gap: `All Roads` (56/61) and `Surface Roads` (2/61) are NOT among §10.6's documented six accepted spellings (`Highway Only`, `Boat`, `Offroad`, `Indoor`, `Baggage`, `Taxi`)** — only `Baggage` (1), `Offroad` (1) and `Boat` (1) of the documented six actually appear; the overwhelming majority of real rows (56/61) use `All Roads`, a spelling absent from the documented accepted-list. Given how dominant it is in real data, it is very likely the resolver DOES accept `All Roads`/`Surface Roads` and §10.6's enum list is simply incomplete (most likely read from a partial view of the compare table) — but this is **HIGH CONFIDENCE, not re-confirmed by fresh disassembly of that specific compare table**, flagged rather than silently corrected. **[CONFIRMED — empirical for the real value distribution; the enum-list completeness itself is OPEN.]** *(2026-09-30: §10.6's CONFIRMED label for the six-name list is downgraded in place; the alternative reading — `All Roads`/`Surface Roads` match nothing and store `-1` — is equally consistent with the text.)*

**Ranks:** `misc_tables.vpp_pc` has **54** rows; `patch_compressed.vpp_pc` carries a 55-row superset adding **`Homie - Kwilanna`** (the only difference — ~~a clean addition, not a capacity-exact fit this time: 55 is well under any stated cap~~ *corrected 2026-09-30: 55 rows exactly fill the 55-record zeroed area of §10.6 (`0x16AA8` / `0x698` = 55) — a capacity-exact fit*) — and per §14.2's archive-priority resolution (RESOLVED 2026-09-24), the 55-row `patch_compressed.vpp_pc` copy, not the 54-row base copy, is the one actually loaded, so `Homie - Kwilanna` IS present in the shipped game. `type` distribution across the 54 base rows matches §10.6's 8 documented type names exactly: `Homie` 14, `Specialist`/`Other` 10 each, `Civilian`/`Gang`/`Law enforcement`/`Stag`/`National Guard` 4 each (sums to 54). **[CONFIRMED — empirical; resolves the row-count/structural half of §12's "not validated" list for all three tables.]**

### 14.13 `drunk_levels.xtbl` and `rank_reactions.xtbl`

**`drunk_levels.xtbl`:** all three documented `Drug_Type`s present (`drunk` 4 levels, `weed` 3 levels, `escort tiger` 2 levels — all ≤ the 5-level cap). **Coverage gap: every real `Level` record carries a `Freerunning_fail_pct` element that §10.7's documented element tree does not mention at all** — a genuine undocumented leaf, not merely an unlisted-but-implied one (it sits alongside the documented `Percent_drunk`/`Camera_rotation_mult`/etc. at the same nesting depth). `escort tiger`'s `Max_Time_Drunk`=99999999 (an effectively-infinite value) and its 2 levels are a binary 0.0/1.0 `Percent_drunk` split, consistent with it gating a scripted state rather than a decaying meter. **[CONFIRMED — empirical; resolves §13.2 item 10's drug-type/level-count half; the undocumented `Freerunning_fail_pct` element is a new coverage finding.]**

**`rank_reactions.xtbl`:** all 5 documented `NPCGroup` names present (`Civilian`, `Civilian Saints Hated`, `Gang, Enemy`, `Gang, Friendly`, `Police`), each with all 4 `ReactionPercentages` rows. Every row's eight reaction percentages sum to 100 (a decode sanity check, 20/20 rows). Representative: `Police` reactions barely change with player rank (mostly `Ignore`+`SayAmbient`, `Threaten` only appears at rank 2+); `Gang, Friendly` never `Threaten`s and its `Compliment`/`GangSign` chances rise with rank; `Civilian Saints Hated`'s `Observe` (a fear/watching reaction) rises from 5% at rank 1 to 30% at rank 4 as `Ignore`/`Threaten` fall. **[CONFIRMED — empirical; fully resolves §10.8's per-level reaction values, previously entirely OPEN.]**

### 14.14 `default_global.xtbl` — undocumented top-level elements

Confirms §10.9's core claim exactly: the document has **no `<root><Table>` wrapper** (its children are direct children of the bare root, read by the plain-open helper `FUN_00DC5AC0`, §10.9). The three documented children (`skybox_mesh_filename`, `cloud_mesh_filename`, `orbitals`) are present, **but the real file also carries five more top-level elements §10.9 does not mention at all: `horizon_mountain_enabled`, `fog_camera_follow`, `day_begin`, `day_end`, `tod_segments`.** Whether these are read by the same `FUN_00BB70B0` (perhaps via output-buffer arguments this pass's disassembly reading did not tabulate) or by a different, untraced reader is an honest gap — flagged, not guessed at. **[CONFIRMED — empirical that these five elements exist in the real file and are absent from §10.9's documented tree; whether/how they are consumed is OPEN.]**

### 14.15 `tweak_table.xtbl` and `metered_sprint.xtbl`

**`tweak_table.xtbl`: 601 real `Tweak_Table_Entry` rows (no duplicates), against the 586-name registry of §10.10.** **585/601 real row names match a registered tunable (case-insensitive); 16 do not** — e.g. `Cell_phone_UI_delay_ms`, `DBNO_bleed_out_delta_per_second`, `DBNO_dead_screen_render_opacity`/`_red_value`, five more `DBNO_getup_*` names, `Melee_power_attack_blend_time_ms`/`_ready_time_ms`, `Player_melee_move_speed`, `Sewage_mover_base_value`/`_increment_value` — confirmed absent from `tools/ai_tweak_registry.tsv` by direct grep, not just a matching-logic artifact. Per §10.10's own documented behaviour ("unknown names are ignored"), **these 16 rows are dead data — authored tunables the shipped code no longer registers**, most plausibly a naming/removal drift between the data and a later code revision. **[CONFIRMED — empirical; this is exactly the kind of population-scale fact the 1-row DLC sample (§10.10's own validation) could not have caught.]** `Framework` is absent (→`main`) on all 601 real rows. *(Reconciliation 2026-09-30: Team B's "15 more than there are possible destinations" (team-b/HANDOFF.md "### 9.81 `sr3tables_progression`") is 601 − 586 = 15; the 16 dead rows here are 601 − 585 = 16; together they imply 586 − 585 = 1 registered tunable with no row. The figures agree.)*

**`metered_sprint.xtbl`: 2 real `SprintEntry` rows** — `Single Player` (`UseTime`=10000ms, `RechargeTime`=9000ms, `DelayTime`=2000ms, `PantPercentage`=0.25, `JumpPenalty`/`StartPenalty`=0) as documented, **plus a second, previously-unremarked `Multiplayer` entry** (`UseTime`=12000ms, `RechargeTime`=10000ms, `DelayTime`=500ms, `PantPercentage`=0.25, `JumpPenalty`=0.1, `StartPenalty`=0.025) — §10.11 only traced the boot path's `"Single Player"` lookup; a second, untraced call site presumably requests `"Multiplayer"` in a co-op/MP context. **[CONFIRMED — empirical for both rows' real values.]**

### 14.16 `activity_types.xtbl` — all 19 names recovered

§10.12/§13.2 item 11 stated the 19 activity-type names are registered at run time and "not recoverable from the loader alone." **The real base file itself supplies most of them directly, and the raw DLC1 sample supplies the rest — together, all 19, with no overlap:**

- **Base `activity_types.xtbl` (15 names, `misc_tables.vpp_pc`; byte-identical to the `da_tables.vpp_pc` copy):** `Escort`, `Fraud`, `Trafficking`, `Guardian Angel`, `Zombie`, `Heli`, `Running Man`, `Horde Mode`, `Tank Mayhem`, `Human Torch`, `Mayhem`, `Snatch`, `Escort_Tiger`, `Snatch_Kinzie`, `Human Torch Cyber`.
- **`dlc1_activity_types.xtbl` (the raw DLC1 sample already used for §10.12's V5 validation; re-read here for its `Name`s specifically) — 4 more, disjoint from the base 15:** `Human Torch Panda`, `Ball Mayhem`, `Escort_Genki`, `Running Man DLC`.

15 + 4 = **19 distinct names, exactly matching the loader's fixed 19-record array (§10.12).** **[HIGH CONFIDENCE, not CONFIRMED by tracing the runtime registration call sites — the count matching exactly across the only two available real sources is strong convergent evidence, but this pass did not decompile whatever code actually calls the registration routine, so it is offered as a very likely closure of §13.2 item 11's naming half, not an ironclad one.]** Real per-activity data (base 15): flag usage confirms all of §10.12's documented `Activity_type_flags`/`Disable_flags` strings are exercised by real rows (e.g. `Horde Mode` sets 9 of the 12 documented disable flags, the most restrictive activity in the table); `Soundbank_Name` present on 5/15 (`Escort`, `Insurance_Fraud`, `Running_Man`, `Whored_Mode`, `Trailblazing` — `Human Torch`/`Human Torch Cyber` share `Trailblazing`).

### 14.17 Method, artifacts, and what is still open after this pass

**Method.** Every table above was extracted with `tools/harnesses/prog_base_extract.py` (32 files, 0 length-mismatch failures) and decoded with `tools/harnesses/prog_common.py`'s tolerant reader; the `unlockables.xtbl`/`cheats.xtbl` `<TableTemplates>` discovery was cross-checked with an independent from-scratch stack-based tag scanner (not the tolerant reader) to rule out a parser artifact, and the unlockables bijection and cheats active-id check were run against all 16 real save snapshots via `tools/harnesses/prog_common.save_samples()`. Extracted files: `tools/ai_base_xtbl/*.xtbl` (32 files, including both `achievements.xtbl`/`mission_checkpoints.xtbl`/`spawn_info_ranks.xtbl` copies, suffixed `.patch_compressed.xtbl` where a same-named `patch_compressed.vpp_pc` copy exists and differs).

**Closed or substantially advanced by this pass (§13.2 item numbers as they stood before this section):** item 1 (base-game values — done for this whole group); item 2 (`stats.xtbl` `Value_Type` distribution); item 3 (achievements row count / 83-window fit; mount-precedence now also resolved, §14.2); item 4 (unlockables row-count correction, capacity check, `patch_unlockables.xtbl` content, `M17` special case); item 5 (respect-level values); item 6 (notoriety/notoriety_levels values; `notoriety_spawn` row shape and per-group data — police-vs-gang labeling and `Check_Detection` semantics remain OPEN); item 8 (cheats: the "151" figure explained, DLC-cheats-exist-inside-base-table resolved, category names partially recovered — flag bytes and index mapping remain OPEN); item 10 (drunk-levels values, though the slot-index register itself remains OPEN); item 11 (activity-type names, all 19, HIGH CONFIDENCE); the DLC-only-validation gaps flagged as item 5 of this task's own brief (tweak_table's 16 dead entries; unlockables' 44/61 real type distribution; cheats' 97-row real Type distribution — none of which the 1–56-row DLC samples could have shown).

**Still genuinely open after this pass (do not treat any of these as resolved):** ~~the `achievements.xtbl`/`mission_checkpoints.xtbl`/`spawn_info_ranks.xtbl` archive mount-precedence question (§14.2) — this is now the single most consequential remaining gap in this document, since it determines whether the achievement array's 83-slot window is exactly full or whether a genuine double-load overflow risk exists~~ **RESOLVED 2026-09-24, §14.2: the `patch_compressed.vpp_pc` copies win, via a patch-exclusive archive-priority mechanism traced in the game's own boot/mount code — the achievement array's 83-slot window is exactly full, not at overflow risk;** which `NotorietyLevels` set is police vs gang and what `Check_Detection` does; the interior of the 24 (or 25, §14.6) `notoriety_spawn` destination structures beyond the fields read; consumers of `gameplay_constants.xtbl`'s ~230 values (six of its top-level sections may now be genuinely dead data, not just untraced); `Spline_Type`'s true accepted-spellings list (§14.12); what reads `default_global.xtbl`'s five undocumented elements; `stats.xtbl`'s handler-table slots 2–5; cheats' flag bytes `+0x02`/`+0x03` and the interface-category index mapping; the consumers of `tweak_table.xtbl`'s (now confirmedly, mostly-still-live) 585 tunables.

**Review status (2026-09-30), §14: NEEDS-DATA: three count conflicts in the spec's own figures, each marked OPEN at the claim — §14.1 type census (43 types / 308 rows listed vs 312), §14.6 `level_info` total (22 × 4 + 3 = 91 vs 80), §14.12 categories (5 + 26 + 1 = 32 vs 31); the main sections now carry or point to each §14 correction (§6.3 tree, §10.3 element case, §10.6 `Spline_Type`, §10.7 `Freerunning_fail_pct`, §10.9 extra elements, §10.11 second row) — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked stale OPEN items resolved (§2.6 items 1–2, §3.1, §3.6, §4.5, §4.8, §5.3, §6.4, §7.3, §8.3, §8.4, §9.3, §12); added a §14.6 correction marker after the §6.3 tree; fixed counts (§8.2 25→27 names, §14.4 26→27; §14.13 capacity-exact fit 0x16AA8/0x698 = 55; §14.6 decay-rate levels 1–3→1–5); flagged unreconciled counts without changing them (§14.1 type census 43/308, §14.6 `Check_Detection` 11 vs 12); inlined the HANDOFF §27.8 rule (§14.6, §14.7).
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline; repointed 3 `HANDOFF.md` §27.x references to the archived headings (§27.8 → §36, §14.6/§14.7); 0 left (see review).
- 2026-09-30 (desk review, `review/adv_tables-progression.md`): added the review-status summary and 38 per-unit status lines; struck §6.3's flat `notoriety_spawn` tree in place and added §14.6's nested tree; marked the `Pepperspray` parent OPEN in both §10.2 and §14.9; downgraded §10.6's `Spline_Type` list from CONFIRMED to OPEN on Team B's 58/61 (team-b/HANDOFF.md "### 9.81 `sr3tables_progression`"); narrowed the scope of the CONFIRMED labels on §3.1 and §4.4; fixed §1.2's "suffix" literal (0x14 bytes apart, a standalone literal); added OPEN notes for count conflicts (§14.1 43/308 vs 312, §14.6 22×4+3 = 91 vs 80, §14.12 5+26+1 = 32 vs 31, §10.7 slot 0x118 = 5·0x38 and span 0x454, §10.2 20 vs 26 sections) and for executable/data questions; carried §14 corrections into §10.3/§10.7/§10.9/§10.11; listed `_Editor` in four trees; replaced a developer-named test category name in §14.12.
- 2026-10-01 (cloud, manager relay of Team B job `20261001T003803-team-b-upsq`): §14.6's nested `level_info` tree and its totals 80 / 254 / 263 VALIDATED-BY-DATA (25/25 rows nested); the count conflict note now points at the "22/25 groups cover levels 2–5" sentence, since the 80 total is reproduced.
