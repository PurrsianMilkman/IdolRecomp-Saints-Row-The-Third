# Saints Row: The Third — Vehicle Interaction, World Objects and Items Data Tables: XML Schemas Recovered from the Loaders

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process), agent AS
**Phase:** Schema-from-loader campaign (`HANDOFF.md` §31, archived §27.2) — group "vehicle interaction, world objects and items"
**Scope:** For each `.xtbl` gameplay table of this group whose literal filename appears in the executable: the loader, the element tree its reader accepts, each element's type and destination offset in the runtime record, defaults and required-vs-optional behaviour, unit conversions, name-hash keys, cross-table references and fixed capacities, then validation against the real base-game rows (now readable, `spec-vpp-container.md` §7). Base-game *values* are not out of scope here — they were extracted and used throughout (§22).
**Method:** Exact filename literals were located by a raw scan of the executable (`tools/harnesses/tbl_exe_lit.py`) and followed by string cross-reference in Ghidra 12.1.3 (project copy `tools/gp_as1`) to each table's loader; the per-row reader and its callees were decompiled and read to the end. The shared XML accessor helpers, name-hash convention and loader idioms were established once by agent AH (`spec-tables-weapons-combat.md` §1) and are reused/cited here, not re-derived. Real base rows for all 24 tables were extracted from `misc_tables.vpp_pc` with `tools/harnesses/vpp_modea.py` (the mode-a offset fix, `spec-vpp-container.md` §7) and validated against the schemas below (§22). No whole-binary predicate search was used.
**Cleanroom compliance:** No decompiled code is reproduced and no original internal identifiers are used. XML table/element names, enum literals and flag literals are *data* and are listed; offsets, sizes, strides, constants and function addresses are evidence anchors. Function addresses use the form `FUN_00XXXXXX` for the image address only.
**Confidence key:** CONFIRMED — disassembly (read directly from the reader's code) / CONFIRMED — empirical (checked against real shipped rows) / HIGH CONFIDENCE — inferred / HYPOTHESIS — unconfirmed / OPEN.

**Review status summary (2026-10-01):** a targeted executable re-derivation (`review/exe-notes-2026-10-01/rederive_tables-vehicle-world.md`, jobs `20261001T123123-team-a-ytgi`/`20261001T123128-team-a-dvje`) cleared **all 9 units** the 2026-09-30 adversarial desk review (`review/adv_tables-vehicle-world.md`) had left NEEDS-EXE (§3, §5, §6, §7, §9.1, §11, §12, §14, §16) and additionally re-derived **9 further units** that had previously only received desk-level text fixes (§2, §4, §8.2, §10, §13, §15, §17, §18, §19) — **18 of the 23 originally reviewed units now carry a 2026-10-01 CONFIRMED — disassembly Review status line** (several also CORRECTED from the 2026-09-30 text: §3, §7, §8.2, §9.1, §10, §11, §13, §14, and §12's `Death_Money` finding). **0 units remain NEEDS-EXE.** 5 units/groupings were out of scope for this pass and keep their 2026-09-30 DESK-PASS status unchanged: §1, §8.1, §9.2 (one new OPEN item added to §23: which hash variant `FUN_00a952e0`'s colour-set lookup uses against the pool's `FUN_00d9e7e0`), §20–§21 (the §5/§7/§8.2 cross-reference rows were updated in place without a new status line), §22–§23 (open-items list updated: items 1, 2 and part of 3/5 closed; items 10–13 added). **0 units are VALIDATED-BY-DATA**, for the same reason as before — Team B's full-population run (`team-b/HANDOFF.md` section "9.93 `sr3tables_vehicle_world`": "`validate_tables_vehicle_world_population.exe` **ALL GATES PASSED**") checks file location, row counts and parse success only; its scalar readers return 0/absent on a wrong element name, so per-element names, enums, nesting and runtime record offsets are still not data-checkable by it. The row counts quoted in this document remain corroborated by that run; the 52-row `patch_compressed.vpp_pc` copy of `vehicle_interaction_info.xtbl` (§3.3) is unchanged and still not re-extracted. What were `DAT_`-only constants are now largely resolved from the image by constant folding or xref static values (`DAT_012a2d90` = 1000.0, `DAT_012a2dd8` = 100.0, `DAT_0126d2cc` = 0.5, `DAT_01115d6c` = 0.05, `DAT_012a2dd0` = 0.01, `DAT_01117a4c` = 1.0; the high dwords of the two doubles remain unread, HIGH CONFIDENCE only — see §10/§12). Remaining OPEN items are now scoped to a single sub-field or literal within an otherwise-cleared unit, not a whole unit (full detail in each section's own 2026-10-01 Review status line and in §23 items 1–13): §2 token-table entries 2–82; §3 the 6 `Capsule_Shape` indirection values, 12 of 16 seat strings, and which vehicle element `FUN_00ac9060`/`FUN_00ac9910` serve; §5 rim-name data check only; §6 the 6 weapon-category literals beyond `WPNCAT_MELEE`; §7 the `Component` record internals (`FUN_00a96a80`); §11 the third `Bloom` child literal; §12 whether `FUN_00dacfb0` falls back to `Death_Money`'s own `X`/`Y`/`Z`; §13 activity literals 2–13; §14 what else consumes the slot −1 scratch memory; §16 the 27 `Item_Flags` literals, `FUN_008fbfd0`, `FUN_009052e0` and the contained-items element name; §18 `FUN_00614d70`'s activity list and `FUN_0070a2f0`'s unknown-name value; §19 the CRT routine `0x00ea3f4a`'s identity.

---

## 1. Overview, method, and pointers to shared machinery

### 1.1 Assignment and coverage

24 tables were assigned to this group (the brief's own count of "23" appears to be a miscount of its own list — the literal enumeration has 24 names). **All 24 have their exact filename literal in the executable, exactly once each** (`tools/harnesses/tbl_exe_lit.py`; none had to be skipped) — see §20 for the full literal/loader index. Every table reached its loader and was read to at least the element-vocabulary level; most were read to a full byte-offset schema and then validated against real rows. Coverage summary:

| Depth reached | Tables |
|---|---|
| **Full byte-offset schema, CONFIRMED — disassembly, and CONFIRMED — empirical against real base rows** | `vi_enter.xtbl`, `vi_exit.xtbl`, `vi_ride.xtbl` (§2), `vehicle_interaction_info.xtbl` (§3), `vehicle_interaction_point_sets.xtbl` (§4), `vehicle_wheel_groups.xtbl` (§5), `vehicle_animation_modifiers.xtbl` (§6 — *qualified 2026-09-30: real rows extracted, structural predicates not re-run, §6.2*), `externalized_vehicle_components.xtbl` (§7 — *qualified 2026-09-30: slot header only; the `Component` record's own fields are OPEN, §7.1/§23*), `vehicle_cust_slots.xtbl`, `vehicle_cust_interface.xtbl` (§8), `vehicle_cust_color_sets.xtbl` (§9.2), `vehicle_surfing_style_two.xtbl` (§10), `level_objects.xtbl` (§12), `props.xtbl` (§13), `triggers.xtbl` (§14), `items_inventory.xtbl` (§15), `contacts_sr3.xtbl` (§17), `activity_player_persona_replacement.xtbl` (§18), `airplane_takeoff_land_curves.xtbl` (§19) |
| **Header/mechanism CONFIRMED — disassembly; full byte offsets of one nested sub-block left OPEN** | `vehicle_cust_color_pool.xtbl` (§9.1, the per-`Color` shader-value block) |
| **Element vocabulary CONFIRMED — empirical from real rows; runtime record only partially traced from code** | `Vehicle-Customization-Lightset.xtbl`, `store_gang_lightset.xtbl`, `test_shop_light_01.xtbl` (§11, the shared "LightSet/Light" schema) |
| **Array/resolver/row-tag CONFIRMED — disassembly and empirical; full per-item byte offsets OPEN** | `items_3d.xtbl` (§16) |

### 1.2 Shared reader grammar — not re-derived, cited

The node model (name/next-sibling/first-child/text pointers, case-insensitive lookup), the scalar accessor family (`always` vs `if-present` flavours for every integer/float/bool type), the float/integer text grammars, the vec3 readers, the `Flag`-list helpers, the name-hash convention (table-driven CRC-32, reflected, table `0x01320DA0`, seed passed per call site, no final XOR, input lower-cased), and the `Framework`/`Is_DLC` two-pass loading idiom are all documented once in `spec-tables-weapons-combat.md` §1 and reused verbatim here; **[Desk review 2026-09-30: unless a section says otherwise, every `vec3` in this document uses that convention — three always-float children `X`, `Y`, `Z` (`spec-tables-weapons-combat.md` §1.3); the LightSet files of §11 are the stated exception (inline space-separated text).]** this document only names the specific helper addresses where they matter for a particular field. Sections 1.6 of that document ("cross-table name resolvers") already covers two of *this* group's tables at the resolver level — `items_3d` (`FUN_00904C10`, stride `0xB8`, count `0x025F5B8C`, key CRC at `+0x04`) and `items_inventory` (`FUN_008DCB10`, stride `0x34`, 110 slots, live bit `+0x30`) — those anchors are the starting point for §15–§16 below, not re-derived.

### 1.3 Group-wide literal constants recovered this pass

**Retitled 2026-09-28 (Team B): this subsection's original title promised "a naming correction that recurs across this group," but its body is a plain literal-constant dump — the three actual, one-off naming corrections this document made during validation (the `Seat`-vs-`Name` child tag in §3, the `name`-vs-own-text child in §8.2, and the `items_preload_containers`/`items_containers` label-vs-section correction in §16) are recorded where they were found and summarised in §22's closing sentence, not here.**

`&DAT_0129ee6c` = the literal `"Name"` (the near-universal row key, confirmed in `spec-tables-weapons-combat.md` §1.5). Three more group-wide literal constants recovered this pass and reused across multiple tables below: `&DAT_0115f3b0` = `"Slot"`, `&DAT_0129ea94` = `"Flag"`, `&DAT_0111d44c` = `"main"` (the default framework name). Dumped with `tools/scripts/AsPtrStrs.java` / `AfMem.java` against the literal-pointer/plain-string addresses named at each call site (raw dumps: `tools/as_ptrstrs1.txt`, `tools/as_strs1.txt`, `tools/as_strs2.txt`).

### 1.4 Extraction and validation setup

Base rows for all 24 tables were pulled from `misc_tables.vpp_pc` (1,342 entries) with the standard pattern:

```python
sys.path.insert(0, r'tools/harnesses')  # repository-relative; developer machine path removed 2026-09-30 (desk review)
import mmap, os
import scan_meshes as sm, vehgeo_bulk as vb, vpp_modea as vm
# open_archive()/get_table() as in spec-vehicle-data.md §7 / HANDOFF §31 (archived §27.2); extraction rule: spec-vpp-container.md §7, tolerant XML parse: spec-xtbl-format.md §7
```

Harness: `tools/harnesses/as_extract.py` (all 24/24 found, byte-length checks all pass — `tools/as_base_xtbl/`). All 24 raw files parsed cleanly with `xml.etree.ElementTree` (none of them is one of the four known-quirky base files from `spec-xtbl-format.md` §7 — mismatched tag, control character `0x1F`, space in an element name — those are in unrelated tables). Validation harness: `tools/harnesses/as_validate.py`; results folded into each table's section and summarised in §22.

**Review status (2026-09-30): DESK-PASS, text fixes applied (developer machine path in §1.4 replaced with the repository-relative `tools/harnesses`; vec3 convention stated once in §1.2); the 24-table count tiles (19 + 1 + 3 + 1 in §1.1) and matches §20's 24 rows — desk review (not re-derived from the executable).**

---

## 2. `vi_enter.xtbl`, `vi_exit.xtbl`, `vi_ride.xtbl` — the shared vehicle-interaction animation-set schema

**These three tables share one loader and one per-row reader — confirmed, not just "likely" as the brief guessed.** `FUN_00b07c40` (called once at start-up) runs, in order: `vehicle_interaction_point_sets.xtbl` (§4), then the shared reader `FUN_00b07920(alloc_ctx, "vi_enter.xtbl", &DAT_02845c90)`, `FUN_00b07920(alloc_ctx, "vi_exit.xtbl", &DAT_02845c88)`, `FUN_00b07920(alloc_ctx, "vi_ride.xtbl", &DAT_02845c80)`, then `vehicle_interaction_info.xtbl` (§3), which cross-references all three result arrays (§3). **[CONFIRMED — disassembly.]**

### 2.1 Element tree and record layout

`Table` → repeated **`Vehicle_Interaction_Animation_Set`** (one array per file, base stored at `DAT_02845c90`/`88`/`80`, count in the adjacent global, stride `0xC` = 3 dwords):

| Offset | Field |
|---|---|
| `+0x00` | `Name` hash `u32` (row key; CRC of the element's own `Name` text, seed 0) |
| `+0x04` | count of `Element` children under `Animation_Grid` |
| `+0x08` | pointer to the `Element` array, stride `0x24` (36 bytes) |

`Animation_Grid` → repeated **`Element`** (`0x24`-byte record):

| Offset | Field |
|---|---|
| `+0x00`–`+0x14` | **`Parameter1`, `Parameter2`, …** — read sequentially (names built with `"Parameter%d"`) until one is absent; each value is matched (`_stricmp`) against an **83-entry** literal token table (`0x0130e638`; **re-derived 2026-10-01 (job 20261001T123123-team-a-ytgi): 83 entries, indices 0–82 (compare loop bound 0x53 read in `FUN_00b07920`); entry 0 is `none` and entry 1 is `any` (first two pointers read from `0x0130e638`/`0x0130e63c`); the remaining 81 pointers in index order are OPEN until the data range `0x0130e638`–`0x0130e784` is dumped. No-match stores 0, the same value as `none`.** Values are stored as the matched index into consecutive dwords starting at the record's own base). **No bound check exists (re-read 2026-10-01: the loop exits only when `ParameterN` is absent)** — a 7th `ParameterN` would corrupt the `Animation` field at `+0x18`; the real base data never reaches that (§22 confirms ≤ 6 for all three files). |
| `+0x18` | `Animation` — text resolved through the animation-state table (`FUN_004BF810`, the same resolver `spec-tables-weapons-combat.md` §15.3 cites for `melee.AttackAnim`); `−1` if absent |
| `+0x1C` | pointer to a `Camera_Pos` `vec3` array (from `Animated_Camera_Tests`) |
| `+0x20` | count of `Camera_Pos` children |

**[CONFIRMED — disassembly; the loop, the enum table, the field offsets and the unbounded-ParameterN hazard were all read directly from `FUN_00b07920`.]**

### 2.2 Validation — real base rows

`vi_enter.xtbl` 106 rows / 657 `Element`s, `vi_exit.xtbl` 99 rows / 813 `Element`s, `vi_ride.xtbl` 69 rows / 454 `Element`s. **657/657, 813/813, 454/454 `Element`s have both a `Name` and an `Animation` present, and ≤ 6 sequential `ParameterN` (never hits the unbounded-write hazard).** `Camera_Pos` is rare and file-specific: 21 total in `vi_enter.xtbl`, 0 in `vi_exit.xtbl`/`vi_ride.xtbl`. **[CONFIRMED — empirical, `tools/harnesses/as_validate.py`.]** **[Desk review 2026-09-30: the element path is `Element` → `Animated_Camera_Tests` → repeated `Camera_Pos`, each a §1.2 `X`/`Y`/`Z` vec3 (12-byte array stride implied, not stated); the `Element` counts 657/813/454 are Team A figures — Team B's run reproduced only the row counts 106/99/69.]**

**Review status (2026-10-01): CONFIRMED — disassembly for the 83-entry bound, the no-cap loop, entries 0/1 of the token table and the record offsets (FUN_00b07920 re-read); OPEN: token-table entries 2–82 in index order (data dump 0x0130e638–0x0130e784 pending); row counts 106/99/69 corroborated by Team B.** (re-derived from the executable 2026-10-01, job `20261001T123123-team-a-ytgi`)

---

## 3. `vehicle_interaction_info.xtbl` — per-seat vehicle-interaction behaviour

Loader `FUN_00b07330`, called last in the `FUN_00b07c40` sequence (§2), after `vi_enter/exit/ride` and `vehicle_interaction_point_sets.xtbl` are already loaded — its own rows cross-reference both.

### 3.1 Record layout

`Table` → repeated **`Vehicle_Interaction_Info`**. Record `0x164` (356) bytes; count = number of `Vehicle_Interaction_Info` elements; array allocated through the standard vtable-`+0x38` allocator.

| Offset | Field |
|---|---|
| `+0x00`–`+0x3F` | `Name` (`char[0x40]`, bounded copy) |
| `+0x40` | `Name` hash `u32` |
| `+0x44`–`+0x163` | eight **Seat_Info** sub-records, `0x24` (36) bytes each — see §3.2 |

`Seat_Info` → repeated **`Element`**, each keyed by its own `<Seat>` text child (confirmed from real data — see §3.3) through the **same 8-entry seat resolver** used by the vehicle-info table's `ProhibitedGunfireSeats`/`Seat_Specific_Data` (`spec-vehicle-data.md` §7.3): `FUN_00AC1BA0`, which accepts either the generic names `driver`/`passenger 1..7` or the vehicle-specific names `front driver`/`front passenger`/`rear driver`/`rear passenger`/`extra 1..4` for the same 8 indices (dumped at `0x0130dfa4`/`0x0130df84`). This is the exact 8-seat convention `spec-vehicle-data.md` documents — confirmed shared, not merely similar. **[Re-derived 2026-10-01: `FUN_00ac1ba0` read in full — two parallel 8-pointer tables, generic (`0x0130dfa4`: entry 0 `driver`, entry 1 `passenger 1`) tried before specific (`0x0130df84`: entry 0 `front driver`, entry 1 `front passenger`), return −1 after 8 misses; CONFIRMED — disassembly. Its other callers are `FUN_00ac9060` and `FUN_00ac9910`, both of which read a `Seat` child in the vehicle-info code; which vehicle element they serve, and the 12 unread strings, remain OPEN. `spec-vehicle-data.md` §7.3's seat labels (`Front Driver Side`…) are descriptions, not this resolver's literals.]**

### 3.2 Per-seat sub-record (`0x24` bytes, base = `record + 0x44 + seatIndex*0x24`)

| Offset | Field |
|---|---|
| `+0x00` | `Interaction_Point_Set` — text hashed and matched against `vehicle_interaction_point_sets.xtbl`'s own loaded array (`DAT_02845ca0`, §4); pointer to that row, or NULL |
| `+0x04` | `Capsule_Shape` — enum, 6 literals (`0x0130e8b8`): `stand`=0, `vehicle`=1, `motorcycle`=2, `boat`=3, `boat stand`=4, `crouch`=5; each matched index is translated through a 6-dword table at `0x011861d0` (read in `FUN_00b07330`, 2026-10-01; the six values are not yet read from the image — HYPOTHESIS: `0x0, 0xF, 0x10, 0x11, 0x12, 0xB`); the field is −1 when the child is absent or matches nothing. `0x0116bb20` is §15's table only. |
| `+0x08` | flags byte 1: bit1 `Mirror Interaction Points`, bit2 `Melee Brute Seat`, bit3 `Weapons Brute Seat`, bit4 `Rollerblader Seat`, bit5 `Riot Shield Seat`, bit6 `Entry Only`, bit7 `Check Point Projection for Usability`; **bit0 is set after every `Element` whose `Seat` resolved** (re-derived 2026-10-01; a "populated" marker, preserved across a refresh reload rather than reset) |
| `+0x09` | flags byte 2: bit0 `No Door Required`, bit1 `Ragdoll On Death`, bit2 `Quick Despawn On Death`, bit3 `Ignore Distance Checks`, bit4 `Ignore Team Disposition`, bit5 `Snap Entry Animation`, bit6 `Hide Occupant`, bit7 `Ignore Human Type Check` |
| `+0x0A` | flags byte 3: bit0 `No AI Exit`, bit1 `Special Entry Only`, bit2 `No Freefall at Altitude`, bit3 `Hold Weapon in Left Hand`, bit4 `No Door Close Anim`, bit5 `Should Hide Backpack`, bit6 `Should Scrunch Hair`, bit7 `Should Hide Big Hats` |
| `+0x0B` | flags byte 4: bit0 `Equip Rifle on Exit` (the only flag in this byte) |
| `+0x0C` | `Queue` — enum, 8 literals `Queue 1`…`Queue 8` (`0x0130e834`) → 0…7; `−1` if absent/no match |
| `+0x10` | `Primary_Access_Seat` — text resolved through the same 8-seat resolver |
| `+0x14` | `Secondary_Access_Seat` — same resolver |
| `+0x18` | `Enter_Animations` — text hashed, matched against `vi_enter.xtbl`'s loaded array (`DAT_02845c90`); pointer or NULL |
| `+0x1C` | `Exit_Animations` — same, against `vi_exit.xtbl`'s array (`DAT_02845c88`) |
| `+0x20` | `Ride_Animations` — same, against `vi_ride.xtbl`'s array (`DAT_02845c80`) |

**Re-derived 2026-10-01 (job `20261001T123123-team-a-ytgi`), `FUN_00b07330` read at 0x00b073df–0x00b07412:** before a row's `Seat_Info` is parsed all eight sub-records are reset: pointers 0, `Capsule_Shape`/`Queue`/both access seats −1, flags bytes 2–4 cleared, flags byte 1 reduced to its bit0 (the populated marker, the only value that survives a refresh). Each flag bit is then assigned from the `Flags` child when present (absent `Flags` leaves all bits clear); byte 1 bit0 is set for every `Element` whose `Seat` resolves. An `Element` whose `Seat` does not resolve is skipped entirely. **[CONFIRMED — disassembly; the full reader `FUN_00b07330` was read to its end.]**

### 3.3 Validation — real base rows

51 `Vehicle_Interaction_Info` rows (base copy, `misc_tables.vpp_pc`) — **a real content difference, not a duplicate, found 2026-09-28 by Team B and re-verified: `patch_compressed.vpp_pc` carries a separate top-level copy with 52 rows, one row more than base, unlike this group's other three multi-location tables (`vehicle_surfing_style_two.xtbl`, `items_3d.xtbl`, `contacts_sr3.xtbl`), which are byte-identical duplicates across their own two locations. Per this project's own established archive-precedence finding (`spec-tables-progression.md` §14.2: `patch_compressed.vpp_pc` wins over `misc_tables.vpp_pc` for same-named entries at boot), the 52-row patch copy is very likely the one actually loaded at runtime — not re-extracted or re-validated against the 52-row copy this pass, flagged as a concrete follow-up.** 209 `Seat_Info`/`Element` children in real base data (from the 51-row base copy; not recounted against the 52-row patch copy). The child tag actually used for the seat identifier is **`<Seat>`** (confirmed from the raw file, e.g. `<Seat>Driver</Seat>`, `<Seat>Passenger 1</Seat>`) — matched case-insensitively by `FUN_00AC1BA0`. `Queue` values seen: all 8 (`Queue 1`…`Queue 8`). `Capsule_Shape` values seen in the sample: `stand` (the only one exercised by real rows so far checked). **[CONFIRMED — empirical.]** **[Desk review 2026-09-30: label scope — the `Capsule_Shape` statement rests on an unquantified sample, not on all 209 elements; the 209/209 seat figure is for the 51-row base copy only.]** **[OPEN — desk review 2026-09-30: re-run the seat, `Queue` and `Capsule_Shape` predicates on the 52-row `patch_compressed.vpp_pc` copy and identify its extra row; to be settled against real data.]**

**Review status (2026-10-01): CONFIRMED — disassembly for the record/sub-record layout, all 24 flag bits, the reset rules, the `Queue`/point-set/animation cross-references and the two-table seat resolver (FUN_00b07330 and FUN_00ac1ba0 read in full); CORRECTED: `Capsule_Shape` indirection table is at `0x011861d0` (`0x0116bb20` belongs to §15); OPEN: the 6 indirection values, 12 of the 16 seat strings, and which vehicle element `FUN_00ac9060`/`FUN_00ac9910` serve; NEEDS-DATA: 52-row patch copy unchanged (job `20261001T123123-team-a-ytgi`).**

---

## 4. `vehicle_interaction_point_sets.xtbl` — named entry/exit point geometry

Loader `FUN_00b07090`, loaded first in the `FUN_00b07c40` chain (§2).

### 4.1 Record layout

`Table` → repeated **`Interaction_Point_Set`**. Array `DAT_02845ca0`, stride `0xC` (3 dwords), count `DAT_02845ca4`:

| Offset | Field |
|---|---|
| `+0x00` | `Name` hash `u32` (the row's `Name` text is hashed into a temporary buffer and discarded — only the hash is kept, unlike §3's records) |
| `+0x04` | count of `Interaction_Point_Set_Element` children |
| `+0x08` | pointer to the element array, stride `0x1C` (28 bytes) |

`Interaction_Point_Set_Elements` → repeated **`Interaction_Point_Set_Element`** (`0x1C`-byte record):

| Offset | Field |
|---|---|
| `+0x00` | `Interaction_Point_Type` — enum, **18 literals** (`0x0130e858`): `None`→`−1`, `Enter Start`→0, `Enter Start Convertible`→1, `Enter Start Lift`→2, `Enter Start Pull`→3, `Enter Start Extract`→4, `Enter Start Extract Convertible`→5, `Enter Traverse End`→6, `Enter Traverse End Extract`→7, `Exit End`→8, `Exit End Convertible`→9, `Exit End Fast Outward`→10, `Exit End Fast Forward`→11, `Exit End Fast Backward`→12, `Exit End At Speed Cruise`→13, `Exit End At Speed Fast`→14, `Exit End At Altitude`→15, `Exit Traverse End`→16; an unmatched `Interaction_Point_Type` leaves the field unwritten; the child is required (its text is used without a null check) (re-derived 2026-10-01, job `20261001T123123-team-a-ytgi`) |
| `+0x04`–`+0x0F` | `Seat_Offset` (`vec3`) |
| `+0x10` | `Heading` (`f32`, always) |
| `+0x14` | `Direction` — enum, 7 literals (`0x0130e8a0`): `Left`=0, `Right`=1, `Front`=2, `Front Left`=3, `Front Right`=4, `Back`=5, `stand`=6 (the 7th literal is anomalous — likely table-tail bleed from an adjacent constant, not exercised by real data, see §4.2); `−1` if absent/no match **[Desk review 2026-09-30, from this document's own addresses: the `Interaction_Point_Type` table `0x0130e858` + 18 × 4 = `0x0130e8a0` (this table's start); 6 pointers end at `0x0130e8a0` + 6 × 4 = `0x0130e8b8`, which is §3.2's `Capsule_Shape` table, whose entry 0 is `stand`. So the authored `Direction` vocabulary is 6 literals and the 7th is `Capsule_Shape[0]` read past the end, as guessed above.]** **[Re-derived 2026-10-01: the compare loop in `FUN_00b07090` runs for indices 0–6, so `stand` is accepted and stores 6 — the bleed is live. CONFIRMED — disassembly. A present but unmatched `Direction` leaves the field unwritten; absent → −1.]** |
| `+0x18` | `Orient_To_Seat` (bool) |
| `+0x19` | `Project_Onto_World` (bool) |
| `+0x1A` | `Orient_To_World` (bool) |
| `+0x1B` | `Ignore_When_In_Chassis` (bool) |

**[CONFIRMED — disassembly; `FUN_00b07090` was read to its end.]**

### 4.2 Validation — real base rows

130 `Interaction_Point_Set` rows, 843 `Interaction_Point_Set_Element` children. **843/843 have an `Interaction_Point_Type` that matches one of the 18 literals** (`tools/harnesses/as_validate.py`). `Direction` values actually seen: `Back`, `Front`, `Front Left`, `Front Right`, `Left`, `Right` — the anomalous 7th literal (`stand`) is **never used in real data**, consistent with it being a table-layout artefact rather than an authored option. **[CONFIRMED — empirical.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00b07090 re-read: 18-entry type loop storing index−1, 7-iteration `Direction` loop that accepts `stand` = 6, all element offsets); 130-row count corroborated by Team B; no open exe item.** (job `20261001T123123-team-a-ytgi`)

---

## 5. `vehicle_wheel_groups.xtbl` — rim and spinner customization catalogue

Loader `FUN_00a973e0`. **Cross-references `spec-vehicle-data.md` §7's already-resolved vehicle schema only loosely** — wheel *groups* here are a customization catalogue (rims/spinners a player can buy), distinct from the physical axle/wheel-physics fields at vehicle entry `+0x4F4` (`spec-vehicle-data.md` §7.3); the two are not the same table and don't cross-reference each other directly.

### 5.1 Record layout

`Table` → repeated **`Wheel_Group`**. Record `0x24` (36 bytes = 9 dwords); count = number of `Wheel_Group` elements.

| Offset | Field |
|---|---|
| `+0x00` | `Name` hash `u32` |
| `+0x04` | `Display_Name` — localized-string handle (`FUN_0084A1B0`) |
| `+0x08` | `Price` (`s32`, if-present, default 0) |
| `+0x0C` | pointer to a parallel array of rim `Display_Name` handles (one per `Rim_Element`) |
| `+0x10` | pointer to a parallel array of pointers to `Component` records resolved from `Front_Rim` (`Front_Rim` → `Component` `+0x00` hash, pointer result — §7, read 2026-10-01) |
| `+0x14` | pointer to a parallel array of pointers to `Component` records resolved from `Rear_Rim` (`Rear_Rim` → `Component` `+0x00` hash, pointer result — §7, read 2026-10-01) |
| `+0x18` | rim count (`u32`) |
| `+0x1C` | pointer to the `S_Element` (spinner) array, stride `0x10` (16 bytes) |
| `+0x20` | spinner count (`u32`) |

`Rims_Grid` → repeated **`Rim_Element`**: `Display_Name` (localized handle), `Front_Rim`/`Rear_Rim` (text, each resolved via `FUN_00A96040` against `externalized_vehicle_components.xtbl`'s loaded array — §7 — confirmed cross-reference). **[Re-derived 2026-10-01, `FUN_00a96040` read in full: the name is hashed and compared with the `+0x00` hash of every `Component` (stride 0x3C) of every `Slot` (slot `+0x08` array, slot `+0x0C` count byte); the stored value is a pointer to the matching `Component` record, or 0. CONFIRMED — disassembly. The `Slot` name is never compared.]** **These three fields are stored as three separate parallel `u32[]` arrays, not one interleaved struct** — a structure-of-arrays layout, unlike almost every other table in this group.

`Spinners_Grid` → repeated **`S_Element`** (`0x10`-byte record): `+0x00` `Display_Name` (localized handle), `+0x04` `Price` (`s32`, if-present, default 0), `+0x08` `Front_Spinner` (text, resolved via `FUN_00A96040`, stored as a pointer to the matching `Component` record; both spinner fields default to 0 if `Front_Spinner` is absent), `+0x0C` `Rear_Spinner` (same resolver, read only if `Front_Spinner` was present).

**[CONFIRMED — disassembly.]**

### 5.2 Validation — real base rows

7 `Wheel_Group` rows, 51 `Rim_Element` total, **0 `S_Element` (spinner) rows** — the base game ships no spinner customization data (real absence, not a parsing gap). **[CONFIRMED — empirical.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00a973e0 and FUN_00a96040 read in full): record 0x24, parallel arrays, spinner element, and the resolver returns a pointer to the matching `Component` record by `Name` hash; NEEDS-DATA (unchanged, now a pure check): all 51 `Front_Rim`/`Rear_Rim` values should exist as `Component` names; 7-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 6. `vehicle_animation_modifiers.xtbl` — hierarchical seat/weapon animation-offset overrides

**Loaded from inside the same top-level function that loads `vehicles.xtbl` itself** (`FUN_00ace640`, the loader chain `spec-vehicle-data.md` §7.1 already documents), as the **last** step, after every `_veh.xtbl` and `vehicle_cameras.xtbl` have been parsed — confirming this table is a **patch applied to already-loaded vehicle-info entries** (`spec-vehicle-data.md` §7's `0xB80`-stride table), not a table with its own array. Reader: `FUN_00ac92d0`.

### 6.1 Element tree and override hierarchy

`Vehicle_Anim_Modifiers` → `Human_Seat_Offset` (`vec3`) — a **single global default** at `DAT_027C2ECC` (in the same manager-block region `spec-vehicle-data.md` §7.1 places the vehicle-info live-count globals). → `Vehicles` → repeated **`Vehicle`** → `Name` (resolved through the vehicle-name→entry lookup `FUN_00AC2400`; unmatched names are silently skipped):

Per matched vehicle entry (offsets relative to the vehicle-info entry base, `spec-vehicle-data.md` §7's `0xB80`-stride table). **Re-derived 2026-10-01 (job `20261001T123123-team-a-ytgi`), `FUN_00ac92d0` read in full:** `Vehicle` → `Name` (bounded copy of 0x18 bytes, hashed for `FUN_00AC2400`, which scans the 132-entry vehicle table at `0x027c87b0` by the hash at `+0x18` of live entries), then `Human_Scale`, `Human_Seat_Offset`, and a **`Seats`** wrapper of repeated **`Seat`** elements, each identified by a **`Name`** child:

| Offset | Field |
|---|---|
| `+0x484` | `Human_Scale` (`f32`, if-present) |
| `+0x488`–`+0x493` | `Human_Seat_Offset` (`vec3`) — per-vehicle override; **zeroed (not defaulted to the global) if absent** |
| `+0x90C` + `seatIndex*0x14` | a per-seat sub-record, `0x14` (20) bytes, for each `<Seat>`/`<Name>` matched against exactly **4 named seats**: `driver`=0, `front passenger`=1, `back left passenger`=2, `back right passenger`=3 (a *different*, smaller seat convention than §3's 8-seat one) **[Re-derived 2026-10-01: `FUN_00ac92d0` compares the `Seat`/`Name` text inline against exactly these four literals and skips any other seat; CONFIRMED — disassembly. Seat slots 4–7 are untouched by this reader.]** |

Per-seat sub-record (`0x14` bytes):

| Offset | Field |
|---|---|
| `+0x00`–`+0x0B` | `Human_Seat_Offset` (`vec3`) — per-seat override, zeroed if absent |
| `+0x0C` | running count of "weapon animation group" sub-entries |
| `+0x10` | pointer to the sub-entry array, stride `0x14` (20 bytes) |

Each `Weapon_Animation_Groups` → `Weapon_Animation_Group` produces **one base sub-entry**, plus **one more per nested `Animation_States/Animation_State`** (`Weapon_Animation_Group` is identified by a **`Name`** child; `Animation_States` → `Animation_State` is likewise identified by a **`Name`** child; a group or state without `Name` is skipped — re-derived 2026-10-01):

| Offset | Field |
|---|---|
| `+0x00` | weapon-category index, via a 7-entry weapon-class table (`FUN_00B81900()` returns `0x011899d4`, entry 0 = `WPNCAT_MELEE`, CONFIRMED — disassembly; entries 1–6 HIGH CONFIDENCE) — the same seven `WPNCAT_*` categories `spec-tables-weapons-combat.md` §15.3 names for `weapon_categories.xtbl` |
| `+0x04` | `Animation_State` index (`FUN_004BF810`, the animation-state table); `0xFFFFFFFF` for the group-level sub-entry itself (meaning "default state for this weapon category") |
| `+0x08` | `Human_Seat_Offset` (`vec3`) — per-(weapon-category, animation-state) override, zeroed if absent |

So the override chain is **global → per-vehicle → per-seat → per-weapon-category → per-animation-state**, each level independently overridable — a drive-by/ride animation seat-offset tuning table. **[CONFIRMED — disassembly; `FUN_00ac92d0` was read to its end.]** **[Desk review 2026-09-30: label scope — the 7 weapon-category literals are HIGH CONFIDENCE only (§23 item 5), not covered by this CONFIRMED label; and since per-vehicle/per-seat offsets are zeroed rather than defaulted, what consumes the global `DAT_027C2ECC` default is not stated.]** **[Re-derived 2026-10-01: answered by `FUN_00ac92d0` read in full — `Seat` has a `Seats` wrapper, the seat name is a `Name` child (not the `Seat` element's own text), and `Weapon_Animation_Group`/`Animation_State` names are likewise `Name` children, as stated in the element tree above. CONFIRMED — disassembly.]**

### 6.2 Validation — real base rows

5,759 bytes of real base data exist for this table (extracted and byte-length-verified, `tools/harnesses/as_extract.py`); structural predicates were not separately re-run against it this pass (small table, already exhaustively read from code) — flagged in §23 as a light gap, not a blocker.

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00ac92d0, FUN_00ac2400 and FUN_00b81900 read in full): element tree incl. `Seats`/`Seat`/`Name`, four inline seat literals, all offsets and the allocation rule; the 7 category literals beyond `WPNCAT_MELEE` are HIGH CONFIDENCE (data range 0x011899d4–0x011899f0 not dumped); no NEEDS-DATA item remains.** (job `20261001T123123-team-a-ytgi`)

---

## 7. `externalized_vehicle_components.xtbl` — shared vehicle-customization component catalogue

Loader `FUN_00a971f0`. This is the table `vehicle_wheel_groups.xtbl` (§5) resolves `Front_Rim`/`Rear_Rim`/`Front_Spinner`/`Rear_Spinner` names against.

### 7.1 Element tree and layout

`Custom_vehicle_properties` → `Component_Slots` → repeated **`Slot`** (the child literal is `"Slot"`, `&DAT_0115f3b0`; count → `DAT_027b0bc4`, array `DAT_027b15f0`, stride `0x10` = 4 dwords):

| Offset | Field |
|---|---|
| `+0x00` | **pointer to the `vehicle_cust_slots` row** (§8.1) whose `Name` hash (`+0x04` of that `0x10`-byte record, array `DAT_027b15e4`, count `DAT_027b15dc`) equals the hash of this `Slot`'s `Name`; 0 if none — not a hash itself, and not matched by `FUN_00A96040` (re-derived 2026-10-01; CORRECTED from the 2026-09-30 reading) |
| `+0x04` | `Camera_Info` hash when present, else the dword at `DAT_029C9964` **[desk review 2026-09-30: `spec-tables-progression.md` §4.1 reads `DAT_029C9964` as the invalid-id constant = 0]** |
| `+0x08` | pointer to the `Component` array, count×`0x3C` (60) bytes |
| `+0x0C` | **`u8`** count of `Components/Component` children (stored through an 8-bit register — a slot with 256 or more components would wrap, re-derived 2026-10-01) |
| `+0x0D` | `u8` = 1, written unconditionally (the "marker" the 2026-09-30 text placed at `+0x08` bit3) |
| `+0x0E`–`+0x0F` | not written |

Each `Component` is filled by `FUN_00a96a80(doc, slot, &local, node)` (not dumped) and then its byte at `+0x38` gets bit3 OR-ed in. The `Slot` → `vehicle_cust_slots` link at `+0x00` is a second cross-reference, by `Name` hash, distinct from `FUN_00A96040`'s `Component`-name scan (§5) — read 2026-10-01.

**[CONFIRMED — disassembly for the `Slot` record (`FUN_00a971f0` read in full, stride `0x10`) — CORRECTED from the 2026-09-30 reading; the `Component` record's own internal fields beyond the one flag byte are OPEN — see §23.]**

### 7.2 Validation — real base rows

133,707 bytes of real base data extracted and byte-length-verified. Not independently structurally validated this pass beyond the byte-length check (§23).

**Review status (2026-10-01): CONFIRMED — disassembly for the `Slot` record (FUN_00a971f0 read in full; stride 0x10, fields `+0x00` cust_slots row pointer, `+0x04` camera hash, `+0x08` array, `+0x0C` u8 count, `+0x0D` marker byte) — CORRECTED from the 2026-09-30 text; OPEN: `Component` record internals (`FUN_00a96a80` not dumped).** (job `20261001T123123-team-a-ytgi`)

---

## 8. `vehicle_cust_slots.xtbl` and `vehicle_cust_interface.xtbl` — customization-menu slot catalogue and UI categories

### 8.1 `vehicle_cust_slots.xtbl` (loader `FUN_00a949f0`)

`Table` → repeated **`Vehicle_slot`**. Record `0x10` (16 bytes); array `DAT_027b15e4`, count `DAT_027b15dc`/`DAT_027b15e0`, zero-filled before parsing (`memset`).

| Offset | Field |
|---|---|
| `+0x00` | `DisplayName` — localized-string handle (`FUN_0084A1B0`) |
| `+0x04` | `Name` hash `u32` |
| `+0x08` | `SlotType` — enum, **5 literals** (`0x01181060`): `body mod`=0, `color`=1, `performance`=2, `wheels`=3, `chassis`=4 |
| `+0x0C` | `Vehicle_Component_Type` — enum, **6 literals** (`0x01181074`): `front wheels`=0, `rear wheels`=1, `front spinners`=2, `rear spinners`=3, `front rims`=4, `rear rims`=5; `0xFFFFFFFF` if absent |

**[CONFIRMED — disassembly.]** **Validation:** 144 real `Vehicle_slot` rows; **144/144 `SlotType` values and 6/6 present `Vehicle_Component_Type` values** match the literal sets above. **[CONFIRMED — empirical.]** **[Desk review 2026-09-30: the literal tables tile — `0x01181060` + 5 × 4 = `0x01181074`, the `Vehicle_Component_Type` table; 4 dwords = `0x10`. "6/6" means 6 of the 144 rows carry `Vehicle_Component_Type`. The roles of the two count globals `DAT_027b15dc`/`DAT_027b15e0` are not distinguished.]**

**Review status (2026-09-30): DESK-PASS (144-row count corroborated by Team B `team-b/HANDOFF.md` section "9.93 `sr3tables_vehicle_world`"; enum value checks are Team A only) — desk review (not re-derived from the executable).**

### 8.2 `vehicle_cust_interface.xtbl` (loader `FUN_00818630`) — the customization-menu category list

`Table` → repeated **`vehicle_cust_interface`** (lowercase row tag; matching is case-insensitive throughout). Record `0xA4` (164 bytes), allocated as a C++-style vector with a per-element constructor call.

| Offset | Field |
|---|---|
| `+0x00` | `Display_Name` — raw NUL-terminated text copied in place (unbounded copy loop, no length helper; the next field is at `+0x80`, so a name of 128 or more bytes overruns into the hash — desk review 2026-09-30) |
| `+0x80` | `Name` hash `u32` |
| `+0x84` | `parent_category` hash `u32` (0 if absent) |
| `+0x88`–`+0x97` | not written by the reader; initialised by the element constructor `FUN_00818180` (not dumped) |
| `+0x98` | count of `slots/slots` children |
| `+0x9C` | pointer to a `u32[]` array of pointers to the resolved `vehicle_cust_slots` rows (0 for an unresolved name) |
| `+0xA0` | flags byte: bit0 `is_color_menu`, bit1 `is_wheel_menu` (set when the child element is present, regardless of its text), bit3 `is_perf_menu` (bit2 not described; `+0xA1`–`+0xA3` padding to `0xA4`) |

`slots` (wrapper) → repeated **`slots`** (same tag name reused for the wrapper *and* each item — an odd doubly-named idiom) → **`name`** (lowercase child, holding the slot-type text) — resolved via `FUN_00A96130` **[Re-derived 2026-10-01: `FUN_00a96130` compares the hash of `name` with the `Name` hash at `+0x04` of each `vehicle_cust_slots` row and returns the row pointer; `SlotType` is not involved. CONFIRMED — disassembly.]** directly against `vehicle_cust_slots.xtbl`'s own loaded array (`DAT_027b15e4`, §8.1) — a **confirmed cross-reference**, correcting an earlier guess in this same investigation that the wrapper's own text was used (it is not; the string dump of `&DAT_012a2558` = `"name"` settled it). **[CONFIRMED — disassembly.]**

**Validation:** 4 real `vehicle_cust_interface` rows (this is a short, fixed menu-category list — ~~Color/Wheels/Performance/Body-Mod-ish, matching the 4 flags~~ **[struck 2026-09-30, desk review: the record names only 3 flag bits (bit0, bit1, bit3), not 4; the category-to-row mapping is a guess, not evidence]**), all with `Display_Name` present; 77 `slots/slots/name` children total across the 4 rows. **[CONFIRMED — empirical.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00818630 and FUN_00a96130 read in full): `name` resolves against `vehicle_cust_slots` `Name` hashes to a row pointer; flags bit0/bit3 by value, bit1 by presence; OPEN only: contents of `+0x88`–`+0x97` set by constructor `FUN_00818180`; 4-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 9. `vehicle_cust_color_pool.xtbl` and `vehicle_cust_color_sets.xtbl` — paint colour catalogue and per-vehicle colour sets

### 9.1 `vehicle_cust_color_pool.xtbl` (loader `FUN_00a94d10`)

`Table` → repeated **`Color`**. Record `0x24` (36 bytes = 9 dwords).

| Offset | Field |
|---|---|
| `+0x00` | interned `Name` string pointer (`FUN_00A74910`) — CONFIRMED by the verify-mode `_stricmp` (the verify pass `_stricmp`s the XML name against it); not a material handle |
| `+0x04` | `Name` hash, computed with `FUN_00d9e7e0` (not the `FUN_00d9e8b0` used by every other loader here — a different hash variant; which one `vehicle_cust_color_sets` uses for its lookup must match, see §9.2) |
| `+0x08` | pointer to the float pool: Σ over numeric shader values of 3 (`Vector_Element` or special kinds 0/1), 1 (`Float_Element`) or 4 (special kind 2, `Glass_Color`) floats |
| `+0x0C` | pointer to `u32[numericCount]` — per numeric shader value, its name hash (written by `FUN_00a94c60(node)`; HIGH CONFIDENCE that it hashes the `Shader_Values` entry's name — callee not dumped) or, for the three special names, the pre-hashed special |
| `+0x10` | pointer to `float*[numericCount]` — per numeric shader value, where its floats start in the pool |
| `+0x14` | pointer to the string-value buffer: one `0x40`-byte allocation (not count×`0x40`); 0 when there are no string values |
| `+0x18` | pointer to `u32[stringCount]` name hashes of string-valued entries; 0 if none |
| `+0x1C` | pointer to `char*[stringCount]`, each pointing `0x40` bytes further into the buffer at `+0x14`; 0 if none |
| `+0x20` | `u8` count of numeric shader values |
| `+0x21` | `u8` count of string shader values |
| `+0x22`–`+0x23` | not written |

**Element tree (re-derived 2026-10-01; CORRECTED):** `Color` → `Name`, then **`Grid`** → repeated **`Shader_Values`** → `Value` → either `String` (text non-empty ⇒ string-valued entry), or `Vector_Element` → `Vector` (vec3 reader; each component multiplied by 1/255 = 0.003921569 before storing, i.e. colours are authored 0–255) and/or `Float_Element` → `Float`. `FUN_00a93990(node)` (not dumped) classifies the entry: kind −1 = generic (floats appended to the pool in document order), kind 0/1/2 = the three special names in the order hashed above (3 floats reserved for 0/1, 4 for 2; a `Float_Element` for a special entry is stored at the slot index that function also returns). Only the first entry of each special kind gets a slot; later duplicates fall through to the generic path. `FUN_00a94b70()` runs after each row (no arguments, not dumped). Hazard: a `Color` with two or more string-valued shader entries writes past the `0x40`-byte buffer at `+0x14` (each string is copied bounded to `0x40` and the destination advances by `0x40` per string).

**[CONFIRMED — disassembly for the full `0x24`-byte record map, allocation sizes and the `Grid`/`Shader_Values`/`Value` tree (`FUN_00a94d10` read in full, re-derived 2026-10-01); HIGH CONFIDENCE for the roles of `FUN_00a93990` (kind/slot classifier) and `FUN_00a94c60` (name-hash writer), not dumped.]** (694 real `Color` rows exist — §9.3.)

**Review status (2026-10-01): CONFIRMED — disassembly for the full 0x24 record map, allocation sizes, `Grid`/`Shader_Values`/`Value` tree and the 1/255 colour scaling (FUN_00a94d10 read in full); HIGH CONFIDENCE for the roles of `FUN_00a93990` (kind/slot classifier) and `FUN_00a94c60` (name hash writer), not dumped; 694-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

### 9.2 `vehicle_cust_color_sets.xtbl` (loader `FUN_00a952e0`)

**Loads `vehicle_cust_color_pool.xtbl` as a dependency first** (calls `FUN_00a94d10` before opening its own file). `Table` → repeated **`Color_Set`**. Record `0x14` (20 bytes = 5 dwords).

| Offset | Field |
|---|---|
| `+0x00` | `Display_Name` — localized-string handle |
| `+0x04` | `Name` hash `u32` |
| `+0x08` | `Price` (`s32`, always) |
| `+0x0C` | count of `Color_Grid/Color_Element` children |
| `+0x10` | pointer to a `u32[]` array of resolved colour-pool pointers |

`Color_Grid` → repeated **`Color_Element`** → `Color` (text, hashed and linear-scanned against `vehicle_cust_color_pool.xtbl`'s loaded array, `+0x04` key). **An unresolved colour name is not fatal** — the engine formats a "colour not found" diagnostic naming the colour into a global buffer and stores 0 (shipped message text paraphrased 2026-09-30, desk review). **[CONFIRMED — disassembly.]**

### 9.3 Validation — real base rows

694 `Color` rows in `vehicle_cust_color_pool.xtbl`; 29 `Color_Set` rows / 874 `Color_Element` children in `vehicle_cust_color_sets.xtbl`. **874/874 `Color_Element/Color` names resolve to a real row in the colour pool** (case-insensitive match against the 694 pool names) — the cross-table reference is exercised, not dead. **[CONFIRMED — empirical.]** **[Desk review 2026-09-30: the engine matches the CRC of the lower-cased name, the check here used case-insensitive text equality; equal except for CRC collisions. Team B corroborates the 694 and 29 row counts only, not 874.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (copied diagnostic text paraphrased; validation method qualified) for §9.2; §9.3 counts as stated — desk review (not re-derived from the executable).**

---

## 10. `vehicle_surfing_style_two.xtbl` — global surfing/handstanding tuning

Loader `FUN_006c3eb0`. Unlike almost every other table in this group, this is a **single-row global settings table**: it reads exactly one `Vehicle_Surfing` element and writes into a compact, non-indexed global block — there is no per-vehicle or per-row array.

| Element | Destination | Conversion |
|---|---|---|
| `Max_Surfing_Time` / `Min_Surfing_Time` (f32, always) | `DAT_014c770c` / `DAT_014c7710` | both forced to 0/0 if `Max ≤ 0` or `Max < Min`; else × a seconds→ticks constant (`DAT_012a2d90`, = **1000.0** double per `spec-cutscene-camera-format.md` §11.2, read from the image — so "ticks" are milliseconds), truncated toward zero (FISTP with RC = 11) |
| `Max_Handstand_Time` / `Min_Handstand_Time` (f32, always) | `DAT_014c7714` / `DAT_014c7718` | same rule and conversion |
| `Max_Handstand_Percent_Reward` (f32, always) | `DAT_014c7724` | ÷ a percent→fraction constant (`DAT_012a2dd8` = 100.0 double — HIGH CONFIDENCE from constant folding; low dword only in the xref), then clamped to `[0, DAT_01117a4c]` (`DAT_01117a4c` = 1.0, byte-confirmed in `spec-lua-api-behaviour.md`) |
| `Surfing_Min_Speed` (f32, always) | `DAT_014c7728` | clamped ≥ 0, × `0.44704` (mph→m/s, `spec-vehicle-data.md` §7.2's constant), **squared** (stored as a squared-speed threshold; no upper cap stated) |
| `Surfing_Timeout` (f32, always) | `DAT_014c7700` | clamped ≥ 0, × ticks constant, truncated toward zero (FISTP with RC = 11) |
| `Start_Time` (f32, always) | `DAT_014c7704` | same |
| `Max_Speed_Delay` (f32, always) | `DAT_014c7708` | same |
| `Record_Display_Time` (f32, **if-present**) | `DAT_014c771c` | 0 if absent; else clamped ≥ 0, × ticks constant |
| `Record_Queue_Time` (f32, if-present) | `DAT_014c7720` | 0 if absent; else same |
| `Record_Threshold` (f32, always) | `DAT_014c7730` | clamped ≥ 0, ÷ percent constant |
| `Handstanding_Sensitivity_Multiplier` (f32, always) | `DAT_014c772c` | clamped to a floor `DAT_01117a4c` (= 1.0, as above) |
| `Max_Respect` (s32, always) | `DAT_014c7734` | clamped ≥ 0 |
| `Max_Lifetime_Respect` (s32, always) | `DAT_014c7738` | clamped ≥ 0 |
| `Max_Cash` (f32, always) | `DAT_014c773c` | clamped ≥ 0 |

**The `Record_Display_Time`/`Record_Queue_Time`/`Record_Threshold`/`Max_Respect`/`Max_Lifetime_Respect`/`Max_Cash` sextet is the exact same "on-screen stunt record" field group `spec-tables-weapons-combat.md` §14.4 (`windshield_cannon.xtbl`) and §11.2 (`combat_tricks.xtbl`) already document** — confirmed shared idiom, third table using it. A trailing call `FUN_006a4ad0(vehicleSurfingNode)` reads a **nested `Balance_Bar_Params`** block, in offset order into the caller's 22-dword block at `+0x14` (re-derived 2026-10-01, `FUN_006a4ad0` read in full): `+0x00` `Balanced_Region_Size`, `+0x04` `Balanced_Region_Size_Change`, `+0x08` `Balanced_Region_Min_Size`, `+0x0C` `Balanced_Region_Acceleration`, `+0x10` `Balanced_Acceleration_Change`, `+0x14` `Balanced_Acceleration_Max`, `+0x18` `Unbalanced_Region_Acceleration`, `+0x1C` `Unbalanced_Acceleration_Change`, `+0x20` `Unbalanced_Acceleration_Max`, `+0x24` `Balancing_Acceleration`, `+0x28` `Balancing_Acceleration_Change`, `+0x2C` `Balancing_Acceleration_Max` — 12 XML floats (always-float helper) + 10 runtime dwords listed in `review/exe-notes-2026-10-01/rederive_tables-vehicle-world.md` §10 (`+0x30` = 1.0, `+0x34`/`+0x38` = 0, `+0x3C`/`+0x40`/`+0x44`/`+0x48` copies of `+0x00`/`+0x0C`/`+0x18`/`+0x24`, `+0x4C` initialised by `FUN_00d9e140`, `+0x50` = 0, `+0x54` a field of the `@@sr2_balance_meter` asset or 0 when missing) — into a 22-dword block at the surfing manager's `+0x14`, then does an SR2-legacy-asset lookup (`"sr2_balance_meter"`) unrelated to the XML. CONFIRMED — disassembly; Team B's expanded names were right. **[Desk review 2026-09-30: the 16 destinations `0x014c7700`–`0x014c773c` tile with no gap or overlap; the placement of `Vehicle_Surfing` (under `Table` or at the root) is not stated.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_006c3eb0 and FUN_006a4ad0 read in full): all 16 destinations, clamps, truncating seconds→ms conversion, the 12 `Balance_Bar_Params` names and the 10 runtime dwords; constants 1000.0 / 100.0 / 0.44704 HIGH CONFIDENCE (decompiler constant folding; high dwords not in the xref listing); 1.0f CONFIRMED.** (job `20261001T123123-team-a-ytgi`)

---

## 11. The shared "LightSet" table — `Vehicle-Customization-Lightset.xtbl`, `store_gang_lightset.xtbl`, `test_shop_light_01.xtbl`

**These three tables (plus `spec-tables-weapons-combat.md` §17 item 6's still-open `store_weapon_lightset.xtbl`) all share one generic lightset loader**, resolving a previously-undocumented shared schema. Confirmed by code for two of the three consumers here **[desk review 2026-09-30: the list that follows names consumer functions for all three files; which one lacks code confirmation is not stated]** (`FUN_00819210`/`FUN_00810330` for the vehicle-customization file, `FUN_00810280` for `store_gang_lightset.xtbl`, `FUN_008367c0` for `test_shop_light_01.xtbl`), all calling the same pair: `FUN_005990c0(filename)` (resolve-or-create a lightset object by filename hash — registry array `DAT_013de2c8`, stride `0x10`, count `DAT_013de2bc`) then `FUN_00598160(handle, mode, extra)` (per-row activation pass).

### 11.1 What the disassembly confirms — the *runtime activation* record, not the XML schema

Each lightset's row array has stride `0x70` (112 bytes); `FUN_00598160` reads/writes, per row: a mode byte at `+0x14`; a **`Light` count** at `+0x10` (re-derived 2026-10-01: CORRECTED — not a resource pointer); an **`Ambient_Grid` count** at `+0x1C` (re-derived 2026-10-01: CORRECTED — its non-zero test on unload clears `DAT_013de2c0`/`DAT_013de2c1`); an `Exposure` float at `+0x20` (if-present, default −1.0) and a `RampExposure` bool byte at `+0x24` — fade-in runs when `RampExposure` is false and `Exposure` ≥ 0.0 (threshold a literal 0.0 compare, re-derived 2026-10-01, CORRECTED from `DAT_0125e270`); and **a 16-byte quad at `+0x30` (zeroed at load) and a 48-byte block of three quads at `+0x40` (initialised from the constant block at `0x012490c0`)** (re-derived 2026-10-01: CORRECTED — this resolves the 2026-09-30 inconsistency between "two quads" and the four 16-byte quads at `+0x30`/`+0x40`/`+0x50`/`+0x60`), either populated live by an SH-probe capture (`FUN_00DA38E0` + `FUN_004ADD90`) or — for the three consumer functions in *this* group — **immediately overwritten with fixed, per-consumer colour constants** (each consumer supplies its own `0x30`/`0x40`/`0x50`/`0x60` quad of hard-coded globals; `test_shop_light_01.xtbl`'s consumer additionally supports a runtime "gang colour" clone via `FUN_00596ff0`). **This is a real, useful finding, but it is *not* the table's own XML-sourced content** — those `+0x00`–`+0x0F` bytes are where the real per-row XML fields must live. **[CONFIRMED — disassembly for everything stated; the XML→struct mapping itself is given in §11.3.]**

### 11.2 What the real base rows confirm — the actual XML vocabulary

All three files were pulled from `misc_tables.vpp_pc` and read raw (they are small, one row each — `Table` → one **`LightSet`** element): `Name`, `StartTime`, `EndTime`, `Exposure`, `RampExposure` (bool), then **`Lights` → repeated `Light`**, each with: `Name`, `Type` (seen: `omni`, `circular spotlight`), `Category_0`..`Category_3` (bool), `CastShadows` (bool), `Color` (three 0–1 floats, space-separated — an inline vec3, *not* `X`/`Y`/`Z` children, unlike almost every other vec3 in this project), `Multiplier` (f32), `Template` (bool), `Position` (inline vec3), `Orientation` (two floats — azimuth/elevation), `Indoor`/`Outdoor` (int flags), `Hotspot` (two floats — spotlight cone angles), `Attenuation` (two floats — near/far falloff), `Slate_Name`, `LightCharacter`, `ShadowCharacter`, `LightLevel`, `ShadowLevel`. **[CONFIRMED — empirical; element vocabulary only.]** **Not read by `FUN_005984c0` (re-derived 2026-10-01): `Template`, `Indoor`, `Outdoor`, `LightCharacter`, `ShadowCharacter`, `LightLevel`, `ShadowLevel` — dead elements in this reader (same family as §12.3's finding); `Negative` is read by the loader but was not seen in these three files.** Real row counts: 1 `LightSet` in each of the three files (`tools/harnesses/as_validate.py`).

### 11.3 XML→struct mapping (re-derived 2026-10-01, job `20261001T123123-team-a-ytgi`)

Dumps: `FUN_005984c0` (loader, read in full) and `FUN_00598160` (activation pass, §11.1). The "two of the three consumers" sentence above stays OPEN (not in scope of these dumps).

**Loader `FUN_005984c0(filename, preparsedDoc, unused, allocator)` — CONFIRMED — disassembly.** Refuses when the registry count `DAT_013de2bc` is already `0x80` (128 lightsets). With no pre-parsed document it checks the file exists (`FUN_00da90d0`) and opens it (`FUN_00dac9a0`); otherwise it wraps the given document (`FUN_00daca90`). Registry entry i at `0x013de2c8 + i·0x10`: `+0x00` filename hash, `+0x04` row array, `+0x08` `LightSet` count, `+0x0C` an allocator-derived handle. Row array = count × `0x70`.

**`LightSet` row (`0x70`) — CONFIRMED — disassembly:**

| Offset | Field |
|---|---|
| `+0x00` | `Name` hash |
| `+0x04` | `StartTime` (int, always) |
| `+0x08` | `EndTime` (int, always) |
| `+0x0C` | `Light` array (count × `0xB0`) |
| `+0x10` | `Light` count |
| `+0x14` | byte zeroed (the mode byte `FUN_00598160` writes) |
| `+0x18` | `Ambient_Grid` array (count × `0x88`) |
| `+0x1C` | `Ambient_Grid` count |
| `+0x20` | `Exposure` (float, if-present, default −1.0) |
| `+0x24` | `RampExposure` (bool byte, default 0) |
| `+0x30`–`+0x3F` | one 16-byte quad, zeroed |
| `+0x40`–`+0x6F` | three 16-byte quads, copied from the constant block at `0x012490c0` |

**`Lights` → `Light` record (`0xB0`) — CONFIRMED — disassembly:**

| Offset | Field |
|---|---|
| `+0x00` | pointer to an allocated copy of `Name` |
| `+0x04` | `Name` hash |
| `+0x08` | `u16` `Type`: `omni` → 0, `circular spotlight` → 2, any other text leaves the field unwritten |
| `+0x0C`–`+0x14` | `Color` (three-float scan of the inline text) |
| `+0x18` | `Multiplier` (float, always) |
| `+0x1C`–`+0x24` | `Color` × `Multiplier` (premultiplied copy) |
| `+0x28` | flags = `0x4200`, OR `0xC00` when `CastShadows` is true |
| `+0x2C` | category mask: `Category_0` → 4, `Category_1` → 8, `Category_2` → `0x10`, `Category_3` → `0x20`; if none is set, all four (`0x3C`) are set |
| `+0x30`–`+0x3C` | `Position` (x, y, z, z) |
| `+0x40`–`+0x4C` | `Position` again (same four values) |
| `+0x50`–`+0x7C` | 12 floats produced by `FUN_004cd740` from the two `Orientation` floats (a 3×4 orientation matrix — HIGH CONFIDENCE, callee not dumped) |
| `+0x80`/`+0x84` | `Hotspot` (two floats) |
| `+0x88`/`+0x8C` | `Attenuation` (two floats) |
| `+0x90` | hash of `Slate_Name` (hash variant `FUN_00d9e7e0`) when present and non-empty |
| `+0x98`/`+0x9C` | two dwords copied from the slate object found by `FUN_005982e0` (fallback `FUN_00598340`), validated by `FUN_00853b10`, else 0/0 |
| `+0xA0` | byte 0 |
| `+0xA1` | `Negative` (bool) |
| `+0xA4`–`+0xAC` | zeroed |

**`Ambient_Grids` → `Ambient_Grid` record (`0x88`) — CONFIRMED — disassembly** (not present in this group's three files, but part of the shared schema): `+0x08`/`+0x0C` slate pair from `Slate_Name` (lookup `FUN_004588f0`, gated on `DAT_02444dac` ≥ 1 and two flag tests); `+0x10`–`+0x18` `Level_Ambient_Color`; `+0x1C`–`+0x24` `Char_Ambient_Color` (both three-float scans); `+0x28` `Cutscene_Frame_Start`, `+0x2C` `Cutscene_Frame_End`, `+0x30` `Cutscene_Shot_Start`, `+0x34` `Cutscene_Shot_End` (ints, −1 if absent); `+0x38`–`+0x40` `Main_Light_Color` and `+0x44`–`+0x4C` `Light_Back_Color` (three floats, −1,−1,−1 if absent); `+0x50` `Shadow_Hardness`, `+0x54` `Light_Amplication` (sic), `+0x58` `Contrast`, `+0x5C` `Bleach` (floats, −1.0 if absent); `+0x60` `Light_Map_DynRange` (1.0 if absent); `+0x64` `Brightness`, `+0x68` `Saturation` (pre-set −1.0, if-present); `+0x6C`–`+0x74` `Tint_Color` (pre-set −1s, three-float scan if present); `+0x78` `Bloom/Radius`, `+0x7C` `Bloom/Cutoff`, `+0x80` `Bloom/` a third child whose literal at `0x0111d7e0` was not resolved in the dump (OPEN: one string read).

**Review status (2026-10-01): CONFIRMED — disassembly for the LightSet (0x70), Light (0xB0) and Ambient_Grid (0x88) records, element names, defaults and the registry (FUN_005984c0 and FUN_00598160 read in full); HIGH CONFIDENCE for the orientation-matrix helper; OPEN: the third `Bloom` child literal at `0x0111d7e0`; 1 × 3 row counts corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 12. `level_objects.xtbl` — placeable-object physical/audio/visual properties catalogue

Loader `FUN_008e8280` (base entry point `FUN_008e8540`; DLC entry point `FUN_008e84c0`, matching the DLC content registry's `level_objects` handler `spec-tables-weapons-combat.md` §15.2 already lists). The real per-row content builder is `FUN_008e7490`.

### 12.1 Record layout (`0xC8` = 200 bytes)

Row tag: `Table` → repeated **`Level_Object`** (stated in §12.3; added here 2026-09-30, desk review). Offsets `+0x35`–`+0x37`, `+0x70`–`+0x73` and `+0x7C`–`+0x87` are not described below.

| Offset | Field | Notes |
|---|---|---|
| `+0x00`–`+0x2F` | `Name` (`char[0x30]`) | row key, also hashed for lookup |
| `+0x30` | `Hitpoints` (`u32`) | |
| `+0x34` | `Material` (`u8`) | matched against the **33 physical-material names** (`spec-tables-weapons-combat.md` §1.6's `FUN_006F76F0`); default index **31** (`0x1F`) if no match — confirmed reuse of that exact resolver |
| `+0x38` | `Lifetime_seconds` → ticks (`s32`) | 0 if absent or negative; else trunc(seconds × 1000.0 + 0.5) ms (re-derived 2026-10-01: `DAT_0125e270` = 0.0 — xref static value — is the "negative" threshold; the 1000.0/0.5 constants are read via decompiler constant folding, HIGH CONFIDENCE, low dwords only in the xref) |
| `+0x3C` | `Weight` → scaled `f32` | integer pounds, read as unsigned 32-bit, × 0.45359236 → kilograms (re-derived 2026-10-01: read as an int, a negative value has 2^32 added i.e. reinterpreted unsigned, CONFIRMED — disassembly) |
| `+0x40` | `Friction` (f32, if-present) | default **0.5** (`DAT_0126d2cc` = `0x3f000000`, re-derived 2026-10-01: xref static value, CONFIRMED) |
| `+0x44` | `Restitution` (f32, if-present) | default **0.05** (`DAT_01115d6c` = `0x3d4ccccd`, CONFIRMED — xref static value) |
| `+0x48` | `Angular_Damping` (f32, if-present) | default **0.05** (`DAT_01115d6c`, same as above) |
| `+0x4C` | `Linear_Damping` (f32, if-present) | default **0.01** (`DAT_012a2dd0` = `0x3c23d70a`, CONFIRMED — xref static value) |
| `+0x50` | `Surface_Velocity` (f32, if-present) | default 0 |
| `+0x54` | `Buoyancy_Modifier` (f32, if-present) | default `DAT_01117a4c` (= 1.0, byte-confirmed in `spec-lua-api-behaviour.md`) |
| `+0x58` | `Anchored/Dislodge_Hitpoints` (u32) | only under `Anchored` |
| `+0x5C` | `Anchored/Dislodge_Effect` → effects CRC | `spec-tables-weapons-combat.md` §1.6's `FUN_005C50B0`; `−1` default |
| `+0x60` | `Anchored/Coins_Released` (u32, if-present) | |
| `+0x64` | `Emitting_Sound` → audio id | `FUN_00462960`, the same string→id resolver as everywhere else |
| `+0x68` | `Collision_Sound/VehicleIVS` (f32, if-present) | presence sets a flag bit |
| `+0x6C` | `Collision_Sound/ObjectMVS` (f32, if-present) | presence sets a flag bit |
| `+0x74` | `Collision_Sound/FoleyCollision` → foley id | `spec-tables-weapons-combat.md` §1.6's `FUN_00561370` |
| `+0x78` | `Death_Effect` → effects CRC | `−1` default |
| `+0x88` | `Death_Explosion` → explosions CRC | `spec-tables-weapons-combat.md` §1.6's `FUN_00590D20`; 0 default |
| `+0x8C` | `Death_Money/Min` (u32) | |
| `+0x90` | `Death_Money/Max` (u32) | |
| `+0x94`–`+0x9F` | `Death_Money/Cash_Out_Point` (`vec3`) | the builder calls the vec3-with-presence helper with the element name **`Cash_Out_Point`** on the `Death_Money` node, not `Death_Money`'s own `X`/`Y`/`Z` directly (re-derived 2026-10-01, `FUN_008e7490` read in full); whether `FUN_00DACFB0` (not dumped) falls back to the parent's `X`/`Y`/`Z` when the named child is missing is OPEN — the §12.3 empirical match describes the data (which happens to use that shape), not the reader |
| `+0xA0` | presence byte for the above vec3 | |
| `+0xA1` | `Death_Money/Just_Coins` (bool) | |
| `+0xA4` | `Vehicle_Repulsor_Scale` (f32, if-present) | default `DAT_01117a4c` (= 1.0, as above) |
| `+0xA8`–`+0xB3` | `center_of_mass/com_offset` (`vec3`) | |
| `+0xB4`–`+0xBF` | `center_of_mass/com_offset_corpse` (`vec3`) | |
| `+0xC0` | flags dword 1 | re-derived 2026-10-01 (`FUN_008e7490` read in full): **bits 0 and 1 are both set** when `Anchored` is present (cleared together with bit 13 when absent, base mode); bit13 `Anchored/Dislodge_On_Death`; bit24 `Movable_By_Humans` (a **row** child, bool helper; cleared when absent in base mode); bit25 `Vehicle_Obstacle`=`false`; bit26 `Vehicle_Obstacle`=`unanchored` (row child; 3-literal table `true`/`false`/`unanchored`, `true` sets nothing); bit28 `Collision_Sound/VehicleIVS` present; bit29 `Collision_Sound/ObjectMVS` present; bit30 `Anchored/Dislodge_Notoriety`; bit31 `Flag` `receives_bullet_impulse` |
| `+0xC4` | flags dword 2 | bit19 `Anchored` marker (set with `Anchored`); bit20 cleared after the `Anchored` block (meaning unknown); `Flag` bits 0–18 and 21–27 as listed in §12.2 (re-derived 2026-10-01). Before the chain (base mode) `+0xC4` is masked with `0xFC1FC000` — bits 0–13 and 21–25 are cleared while bits 14–20 and 26–31 are left as allocated |

`Vehicle_Obstacle` is a 3-literal enum (`true`/`false`/`unanchored`); only the last two set a bit (`true` is the "do nothing extra" default). **[CONFIRMED — disassembly; `FUN_008e7490`, 2,792 decompiled lines, read to its end.]**

### 12.2 The 27-literal `Flags` vocabulary (all on `+0xC4` except the first)

`receives_bullet_impulse` (`+0xC0` bit31), `disappear_on_death` (bit0), `disable_lights_on_dislodge` (bit1), `disable_effects_on_dislodge` (bit2), `ignore_human_collision` (bit3), `camera_collide` (bit4), `vehicle_camera_collide` (bit5), `ignore_bullet_collision` (bit6), `bullets_penetrate` (bit7), `do_not_aim_at_me` (bit8), `fire_hydrant` (bit9), `non_walkable` (bit10), `can_walk_up` (bit11), `nearby_player_despawn` (bit12), `shatters_against_world` (bit13), `blackjack` (bit14), `poker` (bit15), `zombie` (bit16), `basketball` (bit17), `tv` (bit18), `no_detour_until_moved` (bit21), `does_not_generate_detour` (bit22), `generate_detour_even_if_in_air` (bit23), `breakable_glass` (bit24), `ai_los_ignore` (bit25), `damaged_by_players_only` (bit26), `no_brute_pickup` (bit27) — an exhaustive `_stricmp` chain, no catch-all; bit numbers re-derived 2026-10-01, `FUN_008e7490` read in full. **[CONFIRMED — disassembly.]**

### 12.3 Validation — real base rows, and one dead-element finding

149 real `Level_Object` rows: **149/149 have `Name`; all 357 `Flag` children across all rows match one of the 27 literals above (357/357)**; 15 distinct `Material` strings seen (`Cardboard`, `Concrete`, `Cyberspace`, `Electric`, `Flame`, `Glass - Heavy`, `Glass - Medium`, `Metal - Fence`, `Metal - Solid`, `Metal - Thin`, …). `Death_Money`'s `X`/`Y`/`Z` and `Vehicle_Obstacle` were confirmed present with exactly the shapes §12.1 predicts (`<Death_Money><Min>0.0</Min><Max>0.0</Max><Just_Coins>False</Just_Coins><X>0.0</X><Y>0.0</Y><Z>0.0</Z></Death_Money>`, `<Vehicle_Obstacle>true</Vehicle_Obstacle>`). **One dead-element finding**: real `Anchored` blocks carry a `Dislodged_By` wrapper with repeated `Element` children (e.g. `<Dislodged_By><Element>none</Element></Dislodged_By>`) that **`FUN_008e7490`'s full body never reads** — authored data with no consumer in this reader, same family as `spec-tables-weapons-combat.md` §18's dead-element findings. **[CONFIRMED — empirical, `tools/harnesses/as_validate.py`.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_008e7490 read in full): all 27 flag bit positions, the `+0xC0` bit map, element paths, `Weight`/`Lifetime` formulas and the four defaults (0.5 / 0.05 / 0.05 / 0.01, xref static values); OPEN: `Death_Money` vec3 is requested as a `Cash_Out_Point` child (`FUN_00dacfb0` fallback behaviour); the `+0x70`/`+0x7C`–`+0x87` bytes are untouched by the builder; 149-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 13. `props.xtbl` — per-activity decorative-prop counts

Loader `FUN_0060ea40`. **Not a world-placement table** (despite the name) — a tiny fixed configuration table: 14 named activity slots, each with one integer.

`Table` → repeated **`Activity`** → `Name` (bounded string, matched `_stricmp` against a **fixed 14-literal list**), `Num_Props` (`s32`, always). Matched rows store `Num_Props` into a 14-`u32` global block (`DAT_014b02e4`, zeroed with a `memset(…, 0x38)` before parsing — `0x38` = 14×4 bytes, confirming the capacity). **CORRECTED 2026-10-01 — the literals have spaces:** 14 literals in a pointer table at `0x012ed268`, spelled with spaces — entries 0/1 read as `assault thug`, `assault killa` (CONFIRMED — disassembly); the rest, in the order below, HIGH CONFIDENCE (data range `0x012ed268`–`0x012ed29f` not dumped): `assault gangsta`, `assault kingpin`, `kill thug`, `kill killa`, `kill gangsta`, `kill kingpin`, `shooting thug`, `shooting killa`, `shooting gangsta`, `shooting kingpin`, `destroy gang car`, `collect item pickup`. The block index equals the table position. `Num_Props` uses the always-int helper into a local before the match, so an unmatched row costs nothing. **[CONFIRMED — disassembly.]**

**Validation:** all 14 real base `Activity` rows are present with exactly these names (space-separated in the XML, e.g. `assault thug`) and all 14 have `Num_Props`. **[CONFIRMED — empirical, 14/14.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_0060ea40 read in full): 14-entry table at 0x012ed268, space-separated literals (entries 0/1 read), index = table position; entries 2–13 HIGH CONFIDENCE pending a data dump; 14-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 14. `triggers.xtbl` — default properties patched onto pre-placed trigger instances

Loader `FUN_0093e0c0`. **This table does not allocate its own row array.** Each row's `Name` is hashed with the **multiply-by-33 bucket hash** (`FUN_00DAB330(name, 0x14)` — the same hash family `spec-tables-weapons-combat.md` §1.4 documents for camera-shake names, here with modulus 20 instead of 128) and looked up in an **already-populated runtime hash table** — re-derived 2026-10-01, `FUN_00dab330(name, 0x14)` gives a bucket 0–19; bucket heads are a byte array at `0x02622f0d` (20 bytes), chain-next a byte array at `0x02622f21` (20 bytes), `0xFF` = end; the comparator is a virtual call through the object at `0x02622f08` (its first vtable slot) with (key pointer, name); keys are 20 dwords at `0x02622f38`; the slot→instance index is 20 dwords at `0x02622f88` — the odd base addresses are byte arrays inside one object, so they are fine; capacity is 20, exactly the real row count — that maps a name to a **pre-existing trigger-instance slot index**. **CORRECTED 2026-10-01: a row whose name has no match in that table is parsed with slot index −1, so its fields land in the 0x24 bytes just below the array base (`0x02623284`–`0x026232A7`); nothing is "silently discarded".** This is a strong, direct structural link to wherever trigger *instances* are placed in the game world and named (almost certainly the zone/level data, i.e. `.czn_pc`'s parked object-placement/property-stream interior — noted here as a cross-reference only, per the standing instruction; that interior was not opened, touched, or acted on).

### 14.1 Per-instance record (patched fields; base `0x026232A8`, stride `0x24` = 36 bytes, indexed by the matched slot)

Row tag: `Table` → repeated **`Trigger`** (stated in §14.2; added here 2026-09-30, desk review).

| Offset | Field |
|---|---|
| `+0x00` | `Effect` → effects CRC (`FUN_005C50B0`) |
| `+0x04` | `Icon` → map-blip index: position in the 108-entry blip-name table at `0x012fff78` (entries 0/1 read as `map_other_replaceme`, `map_other_blip_human`), else the icon-class value stored before each `0x24`-byte name at `0x0115d2f4` (entries read: `icon_class_kill`, `icon_class_location`), else −1 — re-derived 2026-10-01, `FUN_00802f60` read in full, CONFIRMED structure / HIGH CONFIDENCE meaning |
| `+0x08` | `Foley` → audio id (`FUN_00462960`) |
| `+0x0C` | derived icon-frame index — **CORRECTED 2026-10-01:** only computed when `IconType` matches entry 1 (`Store`) of a 2-pointer table at `0x011708dc`: `0x18 ≤ icon ≤ 0x21` → 7; `0x22 ≤ icon ≤ 0x23` → 8; any other value leaves the field unchanged. Hazard: the icon value lives in a local initialised once before the row loop, so a row with `IconType` = `Store` and no `Icon` of its own uses the previous row's `Icon` value |
| `+0x10` | `UseMessage` → localized-string handle |
| `+0x20` | flag: `Flags` child present |
| `+0x21` | flags byte: bit0 `check_npcs`, bit1 `continuous_activation`, bit2 `disabled_for_demo`, bit4 `ignore_vehicles`, bit5 `ignore_on_foot` |

`IconType` is a 2-literal enum: `Save`=0, `Store`=1 (only `Store` triggers the `+0x0C` derivation). Offsets `+0x14`–`+0x1F` of the per-instance record are not touched by this reader — presumably filled by whatever else builds the trigger instances (again, out of scope). **[CONFIRMED — disassembly; `FUN_0093e0c0` read to its end.]**

### 14.2 Validation — real base rows

20 real `Trigger` rows, all with `Name`; 10 total `Flag` children, **10/10** matching the 5-literal set above. **`IconType` does not appear in any real row** (`0/20` — the literal only appears in the table's own embedded `TableDescription` schema block, not in any authored `Trigger`; consistent with it being a rarely-used field, not a parsing gap). **[CONFIRMED — empirical.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_0093e0c0 and FUN_00802f60 read in full): hash-table layout (20 slots), icon resolver, frame rule 0x18–0x21 → 7 / 0x22–0x23 → 8, flag bits; CORRECTED: unmatched names write to slot −1 rather than being discarded; 20-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 15. `items_inventory.xtbl` — player inventory item catalogue

Loader `FUN_008dc8c0`. Array anchor already known from `spec-tables-weapons-combat.md` §1.6 (`0x0250ADC0`, stride `0x34`, **110 slots**, live bit `+0x30`; resolver `FUN_008DCB10`, `_stricmp` on `+0x00`). This is the table `spec-save-format.md` §10.3 ties to save-file weapon-inventory ids via the item's `Name` hash (`spec-tables-weapons-combat.md` §16.3).

### 15.1 Record layout (`0x34` = 52 bytes)

Row tag: `Table` → repeated **`Inventory_Item`** (stated in §15.2; added here 2026-09-30, desk review).

| Offset | Field | Default if absent |
|---|---|---|
| `+0x00` | `Name` → interned string pointer (`FUN_00A74910`) | — (row key) |
| `+0x04` | `Name` hash, via `FUN_00d9e7e0` (re-derived 2026-10-01 — the same hash variant §9.1 uses, not `FUN_00d9e8b0`) | — |
| `+0x08` | `DisplayName` → localization handle (`FUN_0084A280`; `FUN_0084A280` is a distinct helper: hash lookup via `FUN_00849950`, else a `!!`-prefixed placeholder string is created (read 2026-10-01); it is not the `FUN_0084A1B0` used by §5/§8.1) | placeholder string `&DAT_0111fe04` |
| `+0x0C` | `DisplayName` → resolved display string pointer (`FUN_00849FF0`) | 0 |
| `+0x10` | `Bitmap` → resource handle (`FUN_00E22290`) | `−1` if the `Bitmap` text is empty |
| `+0x14` | `Impact_shape_min_offset` (f32, if-present) | 0 |
| `+0x18` | `Cost` (s32, if-present) | `−1` |
| `+0x1C` | `Default_Count` (s32, if-present) | 1 |
| `+0x20` | `Max_Inventory` (s32, if-present) | = the resolved `Default_Count` value |
| `+0x24` | `Description` → resolved string pointer (`FUN_0084A280` + `FUN_00849FF0`) | 0 |
| `+0x28` | load-order ordinal (`u8`, 0,1,2,… assigned sequentially) | — |
| `+0x2C` | `Use_Script` (s32) | 0 if the child is absent; else matched against a **single recognised literal `none`** (`FUN_00dac830(table 0x01306704, 1, node, 0)`, the single literal at `0x01306704` is the pointer `0x0129b26c` = `"none"`) — `none` → 0; any other text → the dword before the table, `0x00006573` (string bytes of `single_use`), read 2026-10-01; the address `0x0116bb20` belongs to this table only (§3.2 corrected) |
| `+0x30` | flags byte, bit0 = **live** (always set for a kept row — matches `spec-tables-weapons-combat.md` §1.6's cited live bit exactly) (re-derived 2026-10-01: loader, resolver `FUN_008DCB10` and save loader `FUN_008DCEC0` all use record `+0x30` bit0; the save-format spec's `+0x2C` is relative to the hash field, not the record — an offset-base difference, not a disagreement) | — |

**Row selection differs from the group idiom**: the **base load accepts every row unconditionally, regardless of its `Framework` text** (only a DLC-framework load filters by exact match) — items_inventory does not use the usual base-vs-DLC two-pass `Framework` filter for its *base* pass. **[CONFIRMED — disassembly; `FUN_008dc8c0` read to its end.]**

### 15.2 Validation — real base rows

94 real `Inventory_Item` rows (against the documented capacity of 110 — 16 slots unused in the base game). **94/94 have `Name`; of the 94 with `Default_Count` present, 94/94 satisfy `Default_Count ≤ Max_Inventory`** (the defaulting rule holds when both are given explicitly, and trivially when `Max_Inventory` is absent). **[CONFIRMED — empirical.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_008dc8c0, FUN_008dcb10, FUN_008dcec0 read in full): live bit `+0x30` bit0 in loader, resolver and save loader; `Use_Script` table and its out-of-range value; all defaults; `FUN_0084A280` identified; 94-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 16. `items_3d.xtbl` — 3D item/prop mesh catalogue (partial)

Array anchor from `spec-tables-weapons-combat.md` §1.6: `0x025F5B98`, stride `0xB8` (184 bytes), count `0x025F5B8C`, key CRC at `+0x04`, resolver `FUN_00904C10`. This table is what `weapons.xtbl`'s `Name` and `Projectile_Info/Model` cross-reference (`spec-tables-weapons-combat.md` §15.3).

### 16.1 What is confirmed

`Table` → repeated **`Item`** (row tag confirmed via the literal dump, `&DAT_011462fc` = `"Item"`). Loaders: base `FUN_008fa7a0`, DLC `FUN_008fd120` (matches the DLC registry's `items` handler, `spec-tables-weapons-combat.md` §15.2). **Re-derived 2026-10-01 (job `20261001T123123-team-a-ytgi`), `FUN_00904d10(itemNode, ctx, platformIndex, preloadPass)` read in full:** capacity `0x1AE` = **430** entries at `0x025f5b98`, stride `0xB8`, count `DAT_025f5b8c`. `streaming_category` (required child; its text at node `+0x0C`) classifies the item: `Permanent`, `Permanent Mesh Only`, `Permanent Preloaded Head` ⇒ "permanent" class. **The two labels select the preload pass (permanent-class rows) and the normal pass (all other `main` rows) — CONFIRMED — disassembly for the pass split; the label text itself is only passed through** (`FUN_00905630` loads `preload_anim.tbl`, opens the xtbl, zeroes the item count when the reset flag is set, and calls `FUN_009052e0(doc, label, …)`; this dump neither confirms nor refutes the "profiler label" reading given below, since `FUN_009052e0` was not dumped). Fields per entry (offsets from the entry base):

| Offset | Field |
|---|---|
| `+0x00` | pointer to an allocated copy of `Name` |
| `+0x04` | `Name` hash (`FUN_00d9e8b0`) |
| `+0x08` | `Mesh` filename: text via `FUN_00dac8c0(item, "Mesh")` + the platform suffix from the pointer table at `0x01152cc8` (entry read: `.csmesh_pc`), allocated copy; 0 if no `Mesh` |
| `+0x0C` | `character_mesh/character_mesh` filename + suffix table `0x01152cf0` (`.ccmesh_pc`); 0 if absent |
| `+0x10` | `character_mesh`'s rig child (literal `0x0116ed7c`, HIGH CONFIDENCE) filename + the rig suffix chosen by platform from `rig_pc`/`rig_ps2`/`rig_ps3`/`rig_xbox`/`rig_xbox2` (index 0 = `rig_pc`) |
| `+0x14` | allocated copy of `character_mesh/Anim_set` text — **`Anim_set` is consumed, not dead** |
| `+0x18` | mesh registration handle (`FUN_006f6f10`) or 0 |
| `+0x1C` | preload link: in the normal pass, when flags bit 17 (`0x20000`) is set, `FUN_00904180(charMeshName)` is stored here or, if an item of the same `Name` already exists (`FUN_00904c10`), into that item's `+0x1C`; else 0 |
| `+0x20` | byte, 0 |
| `+0x21` | `LargeProp` (bool) |
| `+0x24` | flags: OR of the `Item_Flags/Flag` matches against a 27-literal table at `0x01308838` (`FUN_00dac740`, not dumped — OPEN; `inherit_bone_transforms` is one of them per the real data — **`Item_Flags` is consumed, not dead**), plus `0x20000000` when `Override_Bounding_Box` exists, `0x40000000` when `streaming_category` = `Action Node`, `0x80000000` for permanent-class |
| `+0x2C` | flags 2: bit3 set iff `streaming_category` = `Permanent preloaded head` |
| `+0x30` | `DisplayName` handle (`FUN_0084a1b0`), only when present |
| `+0x58`–`+0x63`, `+0x64`–`+0x6F` | `Override_Bounding_Box` two vec3 children (literals `0x0116ed48`/`0x0116ed44`, unread — HIGH CONFIDENCE min/max) |

`FUN_008fbfd0(entry, ctx, node)` runs first on every entry (not dumped — the `Props`/`Prop`/`Flags` reader the spec attributes to other passes may live there; **`Glow_Type`, `Scale_Ambient`, `Color_Variants` are not read by `FUN_00904d10`** — HYPOTHESIS: `FUN_008fbfd0`). `Environmental` and `Action Node` categories feed the mesh-registration type code (4/5/0x0E/0x0F/0x10) with `FUN_009047c0`/`FUN_00904810` classifying the category text. `Mesh` and `character_mesh` are read independently (no exclusivity rule in code). Element names confirmed present and consumed by *other* passes over the same document (`FUN_00739b20`, `FUN_00be02c0`, `FUN_008c7e30`, all read fully): `Props` → repeated `Prop` → `Flags` → repeated `Flag` (tested against the literal `Attach by default`).

**A correction made mid-investigation, worth recording explicitly:** the strings `"items_preload_containers"` and `"items_containers"` passed as the 2nd argument to `FUN_00905630` are **not XML section names** — no such tag exists anywhere in the real base file (confirmed by a raw-text search of the extracted 269,240-byte base `items_3d.xtbl`). They are **profiler/memory-pool zone labels** (the same idiom as the neighbouring `"object item parsing"`/`"dlc object item parsing"` labels passed to `FUN_00db64a0`), differentiating two *passes* over the same `<Item>` rows (a "preload" pass and a normal pass), not two different data sections. The real per-item "contained items" fixup mechanism (a per-item sub-array of `0x1C`-byte entries, each resolving a contained-item name against `items_3d` itself via `FUN_00904C10` or, on a miss, against the effects registry via `FUN_005C50B0`, and cross-writing the target's own `+0x1C` with a "type" tag) is confirmed to exist in `FUN_00905630`/`FUN_00905e30`'s caller (`FUN_008fa7a0`/`FUN_008fd120`), but **its own XML element name was not identified this pass** (candidates not ruled out: a per-`Item` child element, since no separate top-level section exists; still OPEN after 2026-10-01).

### 16.2 Validation — real base rows

391 real `Item` rows: **391/391 have `Name`**; 125 have a `Mesh` child, 266 have `character_mesh`, and **all 391 have a `Props` wrapper** (usually empty). **[CONFIRMED — empirical for presence counts; the full per-item byte-offset schema is now given above, re-derived 2026-10-01.]** **[Desk review 2026-09-30: 125 + 266 = 391 implies `Mesh` and `character_mesh` never co-occur in a row; not stated as a rule.]**

**Review status (2026-10-01): CONFIRMED — disassembly for the per-row builder (FUN_00904d10 read in full): capacity 430, stride 0xB8, 14 field offsets, pass split by `streaming_category`; OPEN: the 27 `Item_Flags` literals (`0x01308838`), `FUN_008fbfd0`, `FUN_009052e0`, the contained-items element; 391-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 17. `contacts_sr3.xtbl` — in-game phone contact list

Loader `FUN_0083b750` (base entry `FUN_0083b870`; DLC entry `FUN_0083b940`, matching the DLC registry's `contacts` handler, `spec-tables-weapons-combat.md` §15.2, with the `<framework>_contacts_sr3.xtbl` naming convention `FUN_0045ba50` builds).

### 17.1 Record layout

`Table` → repeated **`Contact`** (row tag confirmed empirically, §17.2). Fixed array, capacity **35** (`0x23`, gated in the loader), stride `0x84` (132 bytes) **[Re-derived 2026-10-01: the loader checks `count < 35` (unsigned compare, `JNC` exits) before each row and stops the loop at 35, so a 36th `Contact` is skipped entirely — nothing is written past the array; both 64-byte text fields remain unbounded copies, so a value of 64 or more bytes still overruns the next field. CONFIRMED — disassembly.]**:

| Offset | Field |
|---|---|
| `+0x00`–`+0x3F` | `Name` (raw NUL-terminated string, unbounded copy) |
| `+0x40`–`+0x7F` | `Image` (same) |
| `+0x80` | `Persona` → resolved via the audio middleware string→id function `FUN_00462960` (the same resolver used for sound-event names elsewhere in this project — `Persona` here is almost certainly a voice-line/character-audio persona id, matching the persona theme of §18's table) |

**[CONFIRMED — disassembly.]**

### 17.2 Validation — real base rows

31 real rows (of a capacity of 35 — this is the game's phone-contacts roster, e.g. Kingdom Come Records staff and gang leaders by name), row tag confirmed as `Contact`; **31/31 have `Name`, `Image`, and `Persona`**. **[CONFIRMED — empirical.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_0083b750 read in full): capacity 35 enforced by a pre-row unsigned compare, no overflow path; 0x84 record; 31-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 18. `activity_player_persona_replacement.xtbl` — per-activity player-persona substitution

Loader `FUN_00615430`.

### 18.1 Record layout

`Table` → repeated **`Activity_Persona_Replacement`** → `Name` (resolved via the activity-name resolver `FUN_00614d70`; unmatched names are silently skipped) → `Persona_Replacements` → repeated **`Persona_Replacement`** (`8`-byte record, array per activity, `count`/`pointer` pair in a fixed table indexed by activity) **[Re-derived 2026-10-01: the fixed table is 19 {pointer, count} pairs at `0x014b2ac8`, zeroed before parsing; element arrays come from the `0x01495410` heap; CONFIRMED — disassembly; the activity-name list of `FUN_00614d70` and the unknown-persona return of `FUN_0070a2f0` remain OPEN]**:

| Offset | Field |
|---|---|
| `+0x00` | `Original` — persona name, resolved via `FUN_0070a2f0` |
| `+0x04` | `Replacement` — same resolver, if-present; 0 (none) if absent (desk review 2026-09-30: whether 0 can also be a valid persona id, and what `FUN_0070a2f0` returns for an unknown name, is OPEN) |

**[CONFIRMED — disassembly.]**

### 18.2 Validation — real base rows

3 real `Activity_Persona_Replacement` rows, all with `Name`; 6 total `Persona_Replacement` children, **6/6 with `Original` present**. **[CONFIRMED — empirical.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00615430 read in full): 19-entry {pointer,count} table at 0x014b2ac8, 8-byte records, `Original` required / `Replacement` optional; OPEN: `FUN_00614d70` literal list, `FUN_0070a2f0` unknown-name value; 3-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 19. `airplane_takeoff_land_curves.xtbl` — takeoff/landing approach-curve tuning

Loader `FUN_00b6a370`. **Not a per-vehicle-class table** — exactly **two** named curves are recognised, each stored in its own dedicated global (not an array): `Landing` → `DAT_028cd29c`, `Take Off` → `DAT_028cd2a8`. Any other `Name` is silently ignored.

### 19.1 Record layout

`Curve_Params` → `Points` → repeated **`Point`** (`0x10` = 16-byte record, stride matches a `count`/`pointer` pair per curve):

| Offset | Field |
|---|---|
| `+0x00` | reserved/runtime (explicitly zeroed, not read from XML) |
| `+0x04` | `Offset_Height` (f32, always) |
| `+0x08` | `Offset_Dist` (f32, always) |
| `+0x0C` | `Speed` (f32, always) |

If a curve has ≥ 2 points, a "slope" value is derived from the first two points' height/distance deltas via `FUN_00dae5b0(second.Offset_Height − first.Offset_Height, second.Offset_Dist − first.Offset_Dist)`, which returns 0 for (0, 0) and otherwise calls the CRT routine at `0x00ea3f4a`; the stored value is the negation (CONFIRMED — disassembly for the argument order and sign, re-derived 2026-10-01; the routine's identity, HYPOTHESIS `atan2`, needs a dump of `0x00ea3f4a`), stored as a third global field alongside the curve's own pointer/count — the exact consumer-side meaning of this derived value was not traced (HYPOTHESIS: an approach-angle used to blend/extrapolate beyond the sampled points). **[CONFIRMED — disassembly for the element tree, offsets, two-curve limitation and slope argument order/sign; HYPOTHESIS for the CRT routine's identity and the derived slope's downstream use.]**

### 19.2 Validation — real base rows

Exactly 2 real `Curve_Params` rows, named `Landing` and `Take Off` — **confirming the two-curve limitation empirically, not just from the code path.** 11 total `Point` children across both curves. **[CONFIRMED — empirical.]** **[Desk review 2026-09-30: `DAT_028cd29c` and `DAT_028cd2a8` are `0xC` apart, which fits {pointer, count, slope} per curve.]**

**Review status (2026-10-01): CONFIRMED — disassembly (FUN_00b6a370 and FUN_00dae5b0 read in full): two curves, 0x10 points, slope argument order and negation; HYPOTHESIS: the CRT routine `0x00ea3f4a` is atan2; 2-row count corroborated by Team B.** (job `20261001T123123-team-a-ytgi`)

---

## 20. Literal-and-loader index

| Table | Literal VA | Loader (base / DLC where distinct) | § |
|---|---|---|---|
| `vehicle_animation_modifiers.xtbl` | `0x01184ac8` | `FUN_00ac92d0` (called from `FUN_00ace640`) | 6 |
| `vehicle_interaction_info.xtbl` | `0x01186640` | `FUN_00b07330` | 3 |
| `vehicle_interaction_point_sets.xtbl` | `0x01186380` | `FUN_00b07090` | 4 |
| `vehicle_surfing_style_two.xtbl` | `0x01139af0` | `FUN_006c3eb0` | 10 |
| `vehicle_wheel_groups.xtbl` | `0x0118171c` | `FUN_00a973e0` | 5 |
| `Vehicle-Customization-Lightset.xtbl` | `0x0115e03c` | `FUN_00819210` / `FUN_00810330` | 11 |
| `vehicle_cust_interface.xtbl` | `0x0115ed24` | `FUN_00818630` | 8.2 |
| `vehicle_cust_slots.xtbl` | `0x011811b8` | `FUN_00a949f0` | 8.1 |
| `vehicle_cust_color_pool.xtbl` | `0x011812b4` | `FUN_00a94d10` | 9.1 |
| `vehicle_cust_color_sets.xtbl` | `0x01181314` | `FUN_00a952e0` | 9.2 |
| `vi_enter.xtbl` | `0x011866f0` | `FUN_00b07920` (via `FUN_00b07c40`) | 2 |
| `vi_exit.xtbl` | `0x011866e0` | `FUN_00b07920` (via `FUN_00b07c40`) | 2 |
| `vi_ride.xtbl` | `0x011866d0` | `FUN_00b07920` (via `FUN_00b07c40`) | 2 |
| `externalized_vehicle_components.xtbl` | `0x0118167c` | `FUN_00a971f0` | 7 |
| `level_objects.xtbl` | `0x0116cbf4` | `FUN_008e8540` / DLC `FUN_008e84c0` (row builder `FUN_008e7490`; §12 names `FUN_008e8280` as the loader and `FUN_008e8540` as its base entry point — desk review 2026-09-30) | 12 |
| `props.xtbl` | `0x011290e8` | `FUN_0060ea40` | 13 |
| `triggers.xtbl` | `0x01170ab0` | `FUN_0093e0c0` | 14 |
| `items_3d.xtbl` | `0x0114885c` | `FUN_008fa7a0` / DLC `FUN_008fd120` (row builder `FUN_00904d10`) | 16 |
| `items_inventory.xtbl` | `0x0116e4e8` | `FUN_008dc8c0` | 15 |
| `contacts_sr3.xtbl` | `0x011610dc` | `FUN_0083b870` / DLC `FUN_0083b940` (`FUN_0083b750`) | 17 |
| `activity_player_persona_replacement.xtbl` | `0x01129758` | `FUN_00615430` | 18 |
| `airplane_takeoff_land_curves.xtbl` | `0x01187cd4` | `FUN_00b6a370` | 19 |
| `store_gang_lightset.xtbl` | `0x0115e020` | `FUN_00810280` | 11 |
| `test_shop_light_01.xtbl` | `0x01160590` | `FUN_008367c0` | 11 |

Ghidra dumps: `tools/as_xrefs1.txt` (all 24 string cross-references), `tools/as_dec1.txt`…`as_dec5.txt` (loader/callee decompiles), `tools/as_ptrstrs1.txt`/`as_strs1.txt`/`as_strs2.txt` (literal tables and plain strings). Script: `tools/scripts/AsPtrStrs.java` (new this pass — dereferences a literal-pointer table to its strings; `AfDec.java`/`AfStrXrefs.java`/`AfMem.java` reused from agent AF's toolkit unchanged).

---

## 21. Cross-table reference map

| From (table.field) | To | How resolved |
|---|---|---|
| `vehicle_interaction_info.Seat_Info/Element/{Enter,Exit,Ride}_Animations` | `vi_enter/exit/ride.*` | CRC hash, linear scan of the loaded array (§2, §3) |
| `vehicle_interaction_info.Seat_Info/Element/Interaction_Point_Set` | `vehicle_interaction_point_sets.xtbl` rows | CRC hash, linear scan (§3, §4) |
| `vehicle_interaction_info.Seat_Info/Element/{Seat,Primary_Access_Seat,Secondary_Access_Seat}` | the shared 8-seat resolver (`FUN_00AC1BA0`) also used by `spec-vehicle-data.md` §7.3/§7.4's `ProhibitedGunfireSeats`/`Seat_Specific_Data` **[OPEN — desk review 2026-09-30: not supported by `spec-vehicle-data.md`, see §3.1]** | `_stricmp` against two parallel 8-name tables |
| `vehicle_wheel_groups.{Front,Rear}_{Rim,Spinner}` | `externalized_vehicle_components.xtbl` `Component` rows (re-derived 2026-10-01: CORRECTED — not `Slot` rows) | CRC hash, linear scan of every `Slot`'s `Component` array; `Component` `+0x00` hash, pointer result (§5, §7) |
| `externalized_vehicle_components.Slot` (`+0x00`) | `vehicle_cust_slots.xtbl` `Vehicle_slot` rows | `Name` hash, pointer result — a second cross-reference, distinct from the `Component`-name scan above; read 2026-10-01 (§7, §8.1) |
| `vehicle_cust_interface.slots/slots/name` | `vehicle_cust_slots.xtbl` `Vehicle_slot` rows | `Name` hash, row pointer result (re-derived 2026-10-01) (§8) |
| `vehicle_cust_color_sets.Color_Grid/Color_Element/Color` | `vehicle_cust_color_pool.xtbl` `Color` rows | CRC hash, linear scan (§9) |
| `vehicle_animation_modifiers.Vehicles/Vehicle/Name` | the global vehicle-info table (`spec-vehicle-data.md` §7's `0xB80`-stride entries) | name→entry lookup `FUN_00AC2400` (§6) |
| `vehicle_animation_modifiers.…/Weapon_Animation_Group/Name` | the seven weapon-category literals (`weapon_categories.xtbl`'s `WPNCAT_*` set, `spec-tables-weapons-combat.md` §15.3) | 7-entry enum table (§6) |
| `vehicle_animation_modifiers.…/Animation_State/Name` | the animation-state table (`spec-tables-weapons-combat.md` §15.3's `DAT_03171C10`) | `FUN_004BF810` (§6) |
| `level_objects.Material` | the 33 physical-material names (`spec-tables-weapons-combat.md` §1.6's `FUN_006F76F0`) | `_stricmp`, index 31 if no match (§12) |
| `level_objects.{Death_Effect,Anchored/Dislodge_Effect}` | `effects.xtbl` (other group) | CRC hash map `FUN_005C50B0` (§12), same resolver as `spec-tables-weapons-combat.md` §15.3's weapon-effect fields |
| `level_objects.Death_Explosion` | `explosions.xtbl` (`spec-tables-weapons-combat.md` §10.1) | CRC `FUN_00590D20` (§12) |
| `level_objects.Collision_Sound/FoleyCollision` | foley-collision records (`spec-tables-weapons-combat.md` §1.6's `FUN_00561370`) | Wwise-style id compare (§12) |
| `level_objects.Emitting_Sound`, `contacts_sr3.Persona`, `triggers.Foley` | audio middleware string→id (`FUN_00462960`) | same resolver reused across three of this group's tables (§12, §14, §17) |
| `triggers.Name` | a runtime trigger-instance hash table (pre-populated elsewhere — see §21.1 for the `.czn_pc` note) | multiply-by-33 bucket hash (`FUN_00DAB330`, modulus 20) (§14) |
| `weapons.Name`, `weapons.Projectile_Info/Model` (`spec-tables-weapons-combat.md` §15.3) | `items_3d.xtbl` / `items_inventory.xtbl` | CRC / `_stricmp` (§15, §16) |
| save-file weapon inventory ids (`spec-save-format.md` §10.3) | `items_inventory.xtbl` rows (via `Name` hash) | CRC, seed 0 (§15) — cross-reference confirmed by `spec-tables-weapons-combat.md` §16.3's save-id match |
| `activity_player_persona_replacement.Activity_Persona_Replacement/Name` | the activity-name resolver also used by `props.xtbl`'s literal-name match | `FUN_00614d70` vs. a fixed `_stricmp` list (§13, §18 — not the *same* table, but the same "named activity" family) |

### 21.1 The `.czn_pc` connection — noted, not acted on

`triggers.xtbl` (§14) patches default properties onto trigger records that are **already indexed by name in a runtime hash table before this loader runs** — the table itself allocates nothing and discards any row whose name doesn't already have a slot. The most likely source of those pre-existing named slots is the zone/level data (`.czn_pc`'s object-placement/property-stream interior, `HANDOFF.md` §27.3 item 2), since a trigger is a placed, named object in the game world. **This is noted here as a structural observation only.** No `.czn_pc` file was opened, no bytes of its interior were read or touched, and no further investigation toward it was performed, per this project's standing hold on that item. `level_objects.xtbl` (§12) was checked for the same pattern and does **not** show it — its loader builds and owns its own array and does not consult any pre-existing instance table, so it reads more like a stand-alone "object type" catalogue that a placed object could reference *by name* rather than a patch-table over pre-placed instances; whether anything in `.czn_pc` actually holds such a name reference is, again, not investigated.

**Review status (2026-09-30): DESK-PASS, text fixes applied (24 index rows; loader/entry naming for `level_objects` reconciled; seat-resolver row marked OPEN) — desk review (not re-derived from the executable).**

---

## 22. Validation summary

All figures below are from `tools/harnesses/as_validate.py` run against the real base rows extracted by `tools/harnesses/as_extract.py` (`tools/as_base_xtbl/`, all 24 files, byte-length-verified against the container directory).

| Table | Real rows | Key predicate(s) | Result |
|---|---|---|---|
| `vi_enter.xtbl` | 106 / 657 Elements | Name+Animation present, ≤6 sequential ParameterN | 657/657, 657/657 |
| `vi_exit.xtbl` | 99 / 813 Elements | same | 813/813, 813/813 |
| `vi_ride.xtbl` | 69 / 454 Elements | same | 454/454, 454/454 |
| `vehicle_interaction_info.xtbl` | 51 (base copy; the `patch_compressed.vpp_pc` copy has 52, §3.3) / 209 Seat_Info Elements | `Seat` text recognised by the 8-seat resolver | 209/209 (once checked against the real `<Seat>` tag, §3.3) |
| `vehicle_interaction_point_sets.xtbl` | 130 / 843 Set_Elements | `Interaction_Point_Type` ∈ 18 literals | 843/843 |
| `vehicle_wheel_groups.xtbl` | 7 / 51 Rim_Element | (structural; 0 spinners in base data) | n/a |
| `vehicle_cust_slots.xtbl` | 144 | `SlotType` ∈ 5; `Vehicle_Component_Type` ∈ 6 (6 present) | 144/144; 6/6 |
| `vehicle_cust_interface.xtbl` | 4 | `Display_Name` present; 77 `slots/slots/name` total | 4/4 |
| `vehicle_cust_color_pool.xtbl` | 694 Color rows | (structural only) | n/a |
| `vehicle_cust_color_sets.xtbl` | 29 / 874 Color_Element | `Color` name resolves into the 694-row pool | 874/874 |
| `items_inventory.xtbl` | 94 (of 110 capacity) | Name present; `Default_Count ≤ Max_Inventory` | 94/94; 94/94 |
| `level_objects.xtbl` | 149 | Name present; 357 Flag children ∈ 27 literals | 149/149; 357/357 |
| `props.xtbl` | 14 | matches the 14-literal Activity list; `Num_Props` present | 14/14; 14/14 |
| `triggers.xtbl` | 20 / 10 Flag children | Flag ∈ 5 literals | 10/10 |
| `contacts_sr3.xtbl` | 31 (of 35 capacity) | Name/Image/Persona present | 31/31 each |
| `activity_player_persona_replacement.xtbl` | 3 / 6 Persona_Replacement | Original present | 6/6 |
| `airplane_takeoff_land_curves.xtbl` | 2 (of 2 possible) | named `Landing`/`Take Off` | 2/2 |
| `items_3d.xtbl` | 391 | Name present | 391/391 |
| `vehicle_animation_modifiers.xtbl`, `externalized_vehicle_components.xtbl`, the three lightset files | extracted, byte-length-verified | not independently re-derived this pass beyond the disassembly read | — |

No predicate failed **[desk review 2026-09-30: several predicates are presence-only, and the 4-row (§8.2), 10-flag (§14.2) and 11-point (§19.2) samples support the data facts, not the record offsets; Team B's run corroborates row counts only]**. No table's schema was contradicted by real data; three corrections were made *during* validation, before being written above rather than after (the `Seat` vs `Name` child tag in §3, the `name` vs own-text child in §8.2, and the `items_preload_containers`/`items_containers` label-vs-section correction in §16).

---

## 23. Open items

1. ~~**`vehicle_cust_color_pool.xtbl`'s shader-value block** (§9.1) — the classification mechanism (three special reflection/glass parameters, a generic `Vector_Element`/`Float_Element` list, a separate string-valued list) is confirmed from a full read of `FUN_00a94d10`, but the exact byte offsets within the `0x24`-byte `Color` record beyond `+0x08` were not mapped.~~ **Closed 2026-10-01: `FUN_00a94d10` read in full, full `0x24` byte map given in §9.1. HIGH CONFIDENCE remains for the roles of `FUN_00a93990`/`FUN_00a94c60` (not dumped).**
2. ~~**The shared "LightSet" table's own XML→struct mapping** (§11) — `FUN_00598160` only covers the *runtime activation* pass (probe capture / colour override / fade-in), not the XML row's own fields (`Name`, `StartTime`, `EndTime`, `Exposure`, `RampExposure`, and the `Lights`/`Light` sub-array). The real reader is almost certainly inside `FUN_005984c0`, not decompiled this pass. Real element vocabulary is confirmed empirically (§11.2); byte offsets are not.~~ **Closed 2026-10-01: `FUN_005984c0` read in full, full `LightSet`/`Light`/`Ambient_Grid` record maps given in §11.3. OPEN remains: the third `Bloom` child literal at `0x0111d7e0`.**
3. ~~**`items_3d.xtbl`'s full per-item byte-offset schema** (§16) — the array location/stride/count/key are known (from `spec-tables-weapons-combat.md` §1.6), the row tag and several element names are confirmed, and the "contained items" fixup mechanism's existence is confirmed, but the actual per-row builder `FUN_00904d10` was not decompiled, so no offset table exists for `Mesh`/`character_mesh`/`Props`/`streaming_category`/`Anim_set`/`Glow_Type`/`Scale_Ambient`/`Color_Variants`/`Item_Flags` etc.~~ **Closed in part 2026-10-01: `FUN_00904d10` read in full, 14-field offset table given in §16.1.** The "contained items" list's own XML element name (a per-`Item` child, given no separate top-level section exists) remains unidentified — see item 12 below.
4. **`externalized_vehicle_components.xtbl`'s `Component` record** (§7) — only one flag byte (`+0x38` bit3) of its `0x3C`-byte layout was traced; the rest is unread. **Confirmed present in real shipped data (2026-09-28, Team B; re-verified directly against the real file, 235/199/199/199 occurrences respectively): `Name`, `DisplayName`, `Price`, `Buyable` are all real, populated fields on the `Component` record — not traced to a byte offset this pass (deliberately not added to the schema without disassembly, per this document's own standing rule against inferring structure from data alone) but a concrete follow-up target with real, non-empty data ready to validate against once the offsets are found.** Still OPEN after 2026-10-01: `FUN_00a96a80` (the per-`Component` filler) was not dumped.
5. ~~**`vehicle_animation_modifiers.xtbl`'s weapon-category 7-entry table** (`FUN_00B81900`, §6) was used but not itself dumped to confirm its 7 literals are exactly the `WPNCAT_*` set; treated as HIGH CONFIDENCE by the "7 categories" match to `spec-tables-weapons-combat.md` §15.3, not independently verified.~~ **Closed in part 2026-10-01: `FUN_00B81900` dumped — entry 0 is `WPNCAT_MELEE`, CONFIRMED — disassembly. Entries 1–6 remain HIGH CONFIDENCE (data range `0x011899d4`–`0x011899f0` not dumped).**
6. **The `.czn_pc` connection noted in §21.1** is exactly that — a noted structural observation from `triggers.xtbl`'s pre-populated-hash-table pattern. It was not investigated further, no `.czn_pc` file was opened, and this remains fully within the project's standing hold on that item.
7. Several small semantic HYPOTHESES left unresolved for lack of a consumer-side trace: the `Capsule_Shape` indirection table's real meaning (§3.2), the airplane takeoff/landing curve's derived "slope" value's downstream use (§19.1), and `FUN_00A74910`'s exact generic role (string intern vs. material-specific — used identically in §9.1, §15.1 and elsewhere in this project).
8. `vehicle_animation_modifiers.xtbl` and `externalized_vehicle_components.xtbl` were extracted and byte-length-verified against real base data but not run through the same per-field validation harness as the rest of §22 — a light, explicitly-flagged gap, not a contradiction.

No confirmed fact in this document was invented past what the disassembly or the real base rows support; where the trail ran out (items 1–4 above), that is recorded as OPEN rather than guessed, per `HANDOFF.md` §36 (archived §27.8).

9. **Added 2026-09-30 (desk review):** the `Direction`/`Capsule_Shape` table overlap and loop bound (§4.1); the shared `0x0116bb20` table (§3.2, §15.1); the `items_inventory` live-bit conflict (§15.1); the `Slot` stride contradiction (§7.1); the 4-vs-8 seat count (§6.1); the underscore/space activity literals (§13); the seat-name conflict with `spec-vehicle-data.md` (§3.1). **All seven resolved by the 2026-10-01 re-derivation (see each section's own Review status line).**
10. **Added 2026-10-01 (re-derivation):** whether `FUN_00dacfb0` (§12's `Death_Money/Cash_Out_Point` vec3 helper) falls back to `Death_Money`'s own `X`/`Y`/`Z` when the named child is absent — not dumped.
11. **Added 2026-10-01 (re-derivation):** `triggers.xtbl` (§14) — an unmatched row's fields now confirmed to land in slot −1 (`0x02623284`–`0x026232A7`) rather than being discarded, but what else reads or writes that pre-row memory is not identified.
12. **Added 2026-10-01 (re-derivation):** `items_3d.xtbl` (§16) — the 27-literal `Item_Flags` table at `0x01308838` (`FUN_00dac740`) was not dumped; `FUN_008fbfd0` (first-pass-per-entry filler) and `FUN_009052e0` (pass driver) were not dumped; the "contained items" element name (item 3 above) is unidentified.
13. **Added 2026-10-01 (re-derivation):** `vehicle_interaction_info.xtbl` (§3)/§21 — `FUN_00ac9060` and `FUN_00ac9910`, the other two callers of the shared seat resolver `FUN_00AC1BA0`, were not dumped, so which vehicle element (`ProhibitedGunfireSeats` or `Seat_Specific_Data`) they parse is still OPEN.
14. **Added 2026-10-01 (re-derivation, not itself dumped):** `vehicle_cust_color_sets.xtbl` (§9.2) — `FUN_00a952e0`'s own hash variant for its colour-pool lookup was not re-checked against `FUN_00a94d10`'s `FUN_00d9e7e0` (§9.1); §9.2's text citing a hash match is not re-verified this pass.

**Review status (2026-09-30): DESK-PASS, text fixes applied (predicate scope qualified; open items extended) — desk review (not re-derived from the executable).**

---

## 24. Artifacts

Ghidra project copy: `tools/gp_as1` (disposable, robocopied from `tools/ghidra_projects`). New Ghidra script: `tools/scripts/AsPtrStrs.java` (dereferences a literal-pointer table to its ASCIIZ strings — reusable by later agents; `AfStrXrefs.java`, `AfDec.java`, `AfMem.java` reused unchanged from agent AF's toolkit). Dumps: `tools/as_xrefs1.txt`, `tools/as_dec1.txt`…`as_dec5.txt`, `tools/as_ptrstrs1.txt`, `tools/as_strs1.txt`, `tools/as_strs2.txt`. Harnesses: `tools/harnesses/as_extract.py` (base-row extractor for all 24 tables, `tools/as_base_xtbl/`), `tools/harnesses/as_validate.py` (the §22 predicates). The parked `.czn_pc` interior was not touched (§21.1, §23 item 6).

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): qualified §6/§7 in the §1.1 coverage table (not structurally validated / `Component` record OPEN); fixed 3 cross-references (weapons-combat §14.5→§14.4, save-format §10.3/§16→§10.3, §22→§21.1); swapped the reversed From/To of the `vi_*` row in §21; added the 52-row patch-copy caveat to the §22 `vehicle_interaction_info` row.
- 2026-09-30 (cloud, self-containment pass): restated 1 load-bearing HANDOFF/WALLS-only facts inline (§1.4: extraction/XML-parse rule now cited to `spec-vpp-container.md` §7 / `spec-xtbl-format.md` §7); repointed 3 `HANDOFF.md` §27.x references to the archived headings (§27.2 ×2 → §31, §27.8 → §36); 0 left (see review).
- 2026-09-30 (adversarial desk review, `review/adv_tables-vehicle-world.md`): 23 "Review status" lines and a summary after the front matter (1 DESK-PASS, 13 with text fixes, 9 NEEDS-EXE, 0 VALIDATED-BY-DATA); removed a developer machine path (§1.4) and paraphrased a shipped diagnostic string (§9.2); imported constant values `DAT_012a2d90` = 1000.0 and `DAT_01117a4c` = 1.0 from other specs; showed by address arithmetic that the 7th `Direction` literal is `Capsule_Shape[0]` (§4.1); struck "matching the 4 flags" (3 named, §8.2); stated row tags in §12/§14/§15; marked OPEN: shared `0x0116bb20` table, seat-name conflict with `spec-vehicle-data.md`, `Slot` stride, 4-vs-8 seats, `Balance_Bar_Params` names, quad count, flag bit map, defaults, activity literal spelling, icon-frame rule, live bit, capacities; no labels raised.
- 2026-10-01 (executable re-derivation, `review/exe-notes-2026-10-01/rederive_tables-vehicle-world.md`, jobs `20261001T123123-team-a-ytgi`/`20261001T123128-team-a-dvje`): applied the re-derivation to all 18 units it covers, clearing every one of the 9 NEEDS-EXE units from the 2026-09-30 summary (§3, §5, §6, §7, §9.1, §11, §12, §14, §16) plus 9 further units (§2, §4, §8.2, §10, §13, §15, §17, §18, §19) with fresh "Review status (2026-10-01)" lines; CORRECTED findings: §3 `Capsule_Shape` indirection table moved to `0x011861d0` (`0x0116bb20` is §15's table only), §7 `Slot` record re-mapped (stride 0x10, `+0x00` is a `vehicle_cust_slots` row pointer, not a hash), §8.2 `+0x9C`/`is_wheel_menu` reworded, §9.1 full `0x24` record map and `Grid`/`Shader_Values`/`Value` tree with 1/255 colour scaling, §10 ticks conversion is truncation not rounding, §11 LightSet/Light/Ambient_Grid record maps added (§11.3) and the "two reflection-probe quads" wording corrected, §12 full 27-bit flag map and `Death_Money`/`Cash_Out_Point` child-name finding, §13 the 14 activity literals have spaces not underscores, §14 unmatched trigger names write to slot −1 rather than being discarded; added §23 items 10–13 (Death_Money fallback, triggers slot −1 consumer, `items_3d` `Item_Flags` table, `FUN_00ac9060`/`FUN_00ac9910` seat usage) and closed §23 items 1/2/3(part)/5(part); updated §21's `Component`/`Slot`/`cust_interface` cross-reference rows; updated the top-of-file Review status summary counts.
