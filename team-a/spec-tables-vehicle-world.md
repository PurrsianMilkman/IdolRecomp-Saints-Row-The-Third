# Saints Row: The Third — Vehicle Interaction, World Objects and Items Data Tables: XML Schemas Recovered from the Loaders

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process), agent AS
**Phase:** Schema-from-loader campaign (`HANDOFF.md` §27.2) — group "vehicle interaction, world objects and items"
**Scope:** For each `.xtbl` gameplay table of this group whose literal filename appears in the executable: the loader, the element tree its reader accepts, each element's type and destination offset in the runtime record, defaults and required-vs-optional behaviour, unit conversions, name-hash keys, cross-table references and fixed capacities, then validation against the real base-game rows (now readable, `spec-vpp-container.md` §7). Base-game *values* are not out of scope here — they were extracted and used throughout (§22).
**Method:** Exact filename literals were located by a raw scan of the executable (`tools/harnesses/tbl_exe_lit.py`) and followed by string cross-reference in Ghidra 12.1.3 (project copy `tools/gp_as1`) to each table's loader; the per-row reader and its callees were decompiled and read to the end. The shared XML accessor helpers, name-hash convention and loader idioms were established once by agent AH (`spec-tables-weapons-combat.md` §1) and are reused/cited here, not re-derived. Real base rows for all 24 tables were extracted from `misc_tables.vpp_pc` with `tools/harnesses/vpp_modea.py` (the mode-a offset fix, `spec-vpp-container.md` §7) and validated against the schemas below (§22). No whole-binary predicate search was used.
**Cleanroom compliance:** No decompiled code is reproduced and no original internal identifiers are used. XML table/element names, enum literals and flag literals are *data* and are listed; offsets, sizes, strides, constants and function addresses are evidence anchors. Function addresses use the form `FUN_00XXXXXX` for the image address only.
**Confidence key:** CONFIRMED — disassembly (read directly from the reader's code) / CONFIRMED — empirical (checked against real shipped rows) / HIGH CONFIDENCE — inferred / HYPOTHESIS — unconfirmed / OPEN.

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

The node model (name/next-sibling/first-child/text pointers, case-insensitive lookup), the scalar accessor family (`always` vs `if-present` flavours for every integer/float/bool type), the float/integer text grammars, the vec3 readers, the `Flag`-list helpers, the name-hash convention (table-driven CRC-32, reflected, table `0x01320DA0`, seed passed per call site, no final XOR, input lower-cased), and the `Framework`/`Is_DLC` two-pass loading idiom are all documented once in `spec-tables-weapons-combat.md` §1 and reused verbatim here; this document only names the specific helper addresses where they matter for a particular field. Sections 1.6 of that document ("cross-table name resolvers") already covers two of *this* group's tables at the resolver level — `items_3d` (`FUN_00904C10`, stride `0xB8`, count `0x025F5B8C`, key CRC at `+0x04`) and `items_inventory` (`FUN_008DCB10`, stride `0x34`, 110 slots, live bit `+0x30`) — those anchors are the starting point for §15–§16 below, not re-derived.

### 1.3 Group-wide literal constants recovered this pass

**Retitled 2026-09-28 (Team B): this subsection's original title promised "a naming correction that recurs across this group," but its body is a plain literal-constant dump — the three actual, one-off naming corrections this document made during validation (the `Seat`-vs-`Name` child tag in §3, the `name`-vs-own-text child in §8.2, and the `items_preload_containers`/`items_containers` label-vs-section correction in §16) are recorded where they were found and summarised in §22's closing sentence, not here.**

`&DAT_0129ee6c` = the literal `"Name"` (the near-universal row key, confirmed in `spec-tables-weapons-combat.md` §1.5). Three more group-wide literal constants recovered this pass and reused across multiple tables below: `&DAT_0115f3b0` = `"Slot"`, `&DAT_0129ea94` = `"Flag"`, `&DAT_0111d44c` = `"main"` (the default framework name). Dumped with `tools/scripts/AsPtrStrs.java` / `AfMem.java` against the literal-pointer/plain-string addresses named at each call site (raw dumps: `tools/as_ptrstrs1.txt`, `tools/as_strs1.txt`, `tools/as_strs2.txt`).

### 1.4 Extraction and validation setup

Base rows for all 24 tables were pulled from `misc_tables.vpp_pc` (1,342 entries) with the standard pattern:

```python
sys.path.insert(0, r'D:\Project Crreish\TEAM A\tools\harnesses')
import mmap, os
import scan_meshes as sm, vehgeo_bulk as vb, vpp_modea as vm
# open_archive()/get_table() as in spec-vehicle-data.md §7 / HANDOFF §27.2
```

Harness: `tools/harnesses/as_extract.py` (all 24/24 found, byte-length checks all pass — `tools/as_base_xtbl/`). All 24 raw files parsed cleanly with `xml.etree.ElementTree` (none of them is one of the four known-quirky base files from `spec-xtbl-format.md` §7 — mismatched tag, control character `0x1F`, space in an element name — those are in unrelated tables). Validation harness: `tools/harnesses/as_validate.py`; results folded into each table's section and summarised in §22.

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
| `+0x00`–`+0x14` | **`Parameter1`, `Parameter2`, …** — read sequentially (names built with `"Parameter%d"`) until one is absent; each value is matched (`_stricmp`) against an **83-entry** literal token table (`0x0130e638`; sample of 20 dumped: `none`, `any`, `activate`, `deactivate`, `change seat`, `close`, `enter`, `exit`, `extract`, `open`, `prepare`, `traverse`, `fast`, `prepare lift`, `prepare pull`, `open standard`, `open shady`, `open shopper`, `traverse standard`, `traverse no door` — these read as vehicle-interaction action/state keywords), storing the matched index (0 if no match) into consecutive dwords starting at the record's own base. **No bound check exists** — a 7th `ParameterN` would corrupt the `Animation` field at `+0x18`; the real base data never reaches that (§22 confirms ≤ 6 for all three files). |
| `+0x18` | `Animation` — text resolved through the animation-state table (`FUN_004BF810`, the same resolver `spec-tables-weapons-combat.md` §15.3 cites for `melee.AttackAnim`); `−1` if absent |
| `+0x1C` | pointer to a `Camera_Pos` `vec3` array (from `Animated_Camera_Tests`) |
| `+0x20` | count of `Camera_Pos` children |

**[CONFIRMED — disassembly; the loop, the enum table, the field offsets and the unbounded-ParameterN hazard were all read directly from `FUN_00b07920`.]**

### 2.2 Validation — real base rows

`vi_enter.xtbl` 106 rows / 657 `Element`s, `vi_exit.xtbl` 99 rows / 813 `Element`s, `vi_ride.xtbl` 69 rows / 454 `Element`s. **657/657, 813/813, 454/454 `Element`s have both a `Name` and an `Animation` present, and ≤ 6 sequential `ParameterN` (never hits the unbounded-write hazard).** `Camera_Pos` is rare and file-specific: 21 total in `vi_enter.xtbl`, 0 in `vi_exit.xtbl`/`vi_ride.xtbl`. **[CONFIRMED — empirical, `tools/harnesses/as_validate.py`.]**

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

`Seat_Info` → repeated **`Element`**, each keyed by its own `<Seat>` text child (confirmed from real data — see §3.3) through the **same 8-entry seat resolver** used by the vehicle-info table's `ProhibitedGunfireSeats`/`Seat_Specific_Data` (`spec-vehicle-data.md` §7.3/§7.4): `FUN_00AC1BA0`, which accepts either the generic names `driver`/`passenger 1..7` or the vehicle-specific names `front driver`/`front passenger`/`rear driver`/`rear passenger`/`extra 1..4` for the same 8 indices (dumped at `0x0130dfa4`/`0x0130df84`). This is the exact 8-seat convention `spec-vehicle-data.md` documents — confirmed shared, not merely similar.

### 3.2 Per-seat sub-record (`0x24` bytes, base = `record + 0x44 + seatIndex*0x24`)

| Offset | Field |
|---|---|
| `+0x00` | `Interaction_Point_Set` — text hashed and matched against `vehicle_interaction_point_sets.xtbl`'s own loaded array (`DAT_02845ca0`, §4); pointer to that row, or NULL |
| `+0x04` | `Capsule_Shape` — enum, 6 literals (`0x0130e8b8`): `stand`=0, `vehicle`=1, `motorcycle`=2, `boat`=3, `boat stand`=4, `crouch`=5; each maps through a 6-entry indirection table (`0x0116bb20`… values `0x0, 0xF, 0x10, 0x11, 0x12, 0xB` — HYPOTHESIS on their consumer-side meaning) |
| `+0x08` | flags byte 1: bit1 `Mirror Interaction Points`, bit2 `Melee Brute Seat`, bit3 `Weapons Brute Seat`, bit4 `Rollerblader Seat`, bit5 `Riot Shield Seat`, bit6 `Entry Only`, bit7 `Check Point Projection for Usability`; **bit0 is always forced to 1 after any row is read** (a "populated" marker, preserved across a refresh reload rather than reset) |
| `+0x09` | flags byte 2: bit0 `No Door Required`, bit1 `Ragdoll On Death`, bit2 `Quick Despawn On Death`, bit3 `Ignore Distance Checks`, bit4 `Ignore Team Disposition`, bit5 `Snap Entry Animation`, bit6 `Hide Occupant`, bit7 `Ignore Human Type Check` |
| `+0x0A` | flags byte 3: bit0 `No AI Exit`, bit1 `Special Entry Only`, bit2 `No Freefall at Altitude`, bit3 `Hold Weapon in Left Hand`, bit4 `No Door Close Anim`, bit5 `Should Hide Backpack`, bit6 `Should Scrunch Hair`, bit7 `Should Hide Big Hats` |
| `+0x0B` | flags byte 4: bit0 `Equip Rifle on Exit` (the only flag in this byte) |
| `+0x0C` | `Queue` — enum, 8 literals `Queue 1`…`Queue 8` (`0x0130e834`) → 0…7; `−1` if absent/no match |
| `+0x10` | `Primary_Access_Seat` — text resolved through the same 8-seat resolver |
| `+0x14` | `Secondary_Access_Seat` — same resolver |
| `+0x18` | `Enter_Animations` — text hashed, matched against `vi_enter.xtbl`'s loaded array (`DAT_02845c90`); pointer or NULL |
| `+0x1C` | `Exit_Animations` — same, against `vi_exit.xtbl`'s array (`DAT_02845c88`) |
| `+0x20` | `Ride_Animations` — same, against `vi_ride.xtbl`'s array (`DAT_02845c80`) |

All four flags bytes and the `Flags`/`Queue`/`Capsule_Shape` reads are gated on their respective XML children being present; absent → the byte-4/`Capsule_Shape` fields keep whatever a previous refresh left there (only bit0 of byte1 and bit0 of byte4-adjacent field get an explicit unconditional reset before parsing). **[CONFIRMED — disassembly; the full reader `FUN_00b07330` was read to its end.]**

### 3.3 Validation — real base rows

51 `Vehicle_Interaction_Info` rows (base copy, `misc_tables.vpp_pc`) — **a real content difference, not a duplicate, found 2026-09-28 by Team B and re-verified: `patch_compressed.vpp_pc` carries a separate top-level copy with 52 rows, one row more than base, unlike this group's other three multi-location tables (`vehicle_surfing_style_two.xtbl`, `items_3d.xtbl`, `contacts_sr3.xtbl`), which are byte-identical duplicates across their own two locations. Per this project's own established archive-precedence finding (`spec-tables-progression.md` §14.2: `patch_compressed.vpp_pc` wins over `misc_tables.vpp_pc` for same-named entries at boot), the 52-row patch copy is very likely the one actually loaded at runtime — not re-extracted or re-validated against the 52-row copy this pass, flagged as a concrete follow-up.** 209 `Seat_Info`/`Element` children in real base data (from the 51-row base copy; not recounted against the 52-row patch copy). The child tag actually used for the seat identifier is **`<Seat>`** (confirmed from the raw file, e.g. `<Seat>Driver</Seat>`, `<Seat>Passenger 1</Seat>`) — matched case-insensitively by `FUN_00AC1BA0`. `Queue` values seen: all 8 (`Queue 1`…`Queue 8`). `Capsule_Shape` values seen in the sample: `stand` (the only one exercised by real rows so far checked). **[CONFIRMED — empirical.]**

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
| `+0x00` | `Interaction_Point_Type` — enum, **18 literals** (`0x0130e858`): `None`→`−1`, `Enter Start`→0, `Enter Start Convertible`→1, `Enter Start Lift`→2, `Enter Start Pull`→3, `Enter Start Extract`→4, `Enter Start Extract Convertible`→5, `Enter Traverse End`→6, `Enter Traverse End Extract`→7, `Exit End`→8, `Exit End Convertible`→9, `Exit End Fast Outward`→10, `Exit End Fast Forward`→11, `Exit End Fast Backward`→12, `Exit End At Speed Cruise`→13, `Exit End At Speed Fast`→14, `Exit End At Altitude`→15, `Exit Traverse End`→16 |
| `+0x04`–`+0x0F` | `Seat_Offset` (`vec3`) |
| `+0x10` | `Heading` (`f32`, always) |
| `+0x14` | `Direction` — enum, 7 literals (`0x0130e8a0`): `Left`=0, `Right`=1, `Front`=2, `Front Left`=3, `Front Right`=4, `Back`=5, `stand`=6 (the 7th literal is anomalous — likely table-tail bleed from an adjacent constant, not exercised by real data, see §4.2); `−1` if absent/no match |
| `+0x18` | `Orient_To_Seat` (bool) |
| `+0x19` | `Project_Onto_World` (bool) |
| `+0x1A` | `Orient_To_World` (bool) |
| `+0x1B` | `Ignore_When_In_Chassis` (bool) |

**[CONFIRMED — disassembly; `FUN_00b07090` was read to its end.]**

### 4.2 Validation — real base rows

130 `Interaction_Point_Set` rows, 843 `Interaction_Point_Set_Element` children. **843/843 have an `Interaction_Point_Type` that matches one of the 18 literals** (`tools/harnesses/as_validate.py`). `Direction` values actually seen: `Back`, `Front`, `Front Left`, `Front Right`, `Left`, `Right` — the anomalous 7th literal (`stand`) is **never used in real data**, consistent with it being a table-layout artefact rather than an authored option. **[CONFIRMED — empirical.]**

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
| `+0x10` | pointer to a parallel array of resolved `Front_Rim` handles |
| `+0x14` | pointer to a parallel array of resolved `Rear_Rim` handles |
| `+0x18` | rim count (`u32`) |
| `+0x1C` | pointer to the `S_Element` (spinner) array, stride `0x10` (16 bytes) |
| `+0x20` | spinner count (`u32`) |

`Rims_Grid` → repeated **`Rim_Element`**: `Display_Name` (localized handle), `Front_Rim`/`Rear_Rim` (text, each resolved via `FUN_00A96040` against `externalized_vehicle_components.xtbl`'s loaded array — §7 — confirmed cross-reference). **These three fields are stored as three separate parallel `u32[]` arrays, not one interleaved struct** — a structure-of-arrays layout, unlike almost every other table in this group.

`Spinners_Grid` → repeated **`S_Element`** (`0x10`-byte record): `+0x00` `Display_Name` (localized handle), `+0x04` `Price` (`s32`, if-present, default 0), `+0x08` `Front_Spinner` (text, resolved via `FUN_00A96040`; both spinner fields default to 0 if `Front_Spinner` is absent), `+0x0C` `Rear_Spinner` (same resolver, read only if `Front_Spinner` was present).

**[CONFIRMED — disassembly.]**

### 5.2 Validation — real base rows

7 `Wheel_Group` rows, 51 `Rim_Element` total, **0 `S_Element` (spinner) rows** — the base game ships no spinner customization data (real absence, not a parsing gap). **[CONFIRMED — empirical.]**

---

## 6. `vehicle_animation_modifiers.xtbl` — hierarchical seat/weapon animation-offset overrides

**Loaded from inside the same top-level function that loads `vehicles.xtbl` itself** (`FUN_00ace640`, the loader chain `spec-vehicle-data.md` §7.1 already documents), as the **last** step, after every `_veh.xtbl` and `vehicle_cameras.xtbl` have been parsed — confirming this table is a **patch applied to already-loaded vehicle-info entries** (`spec-vehicle-data.md` §7's `0xB80`-stride table), not a table with its own array. Reader: `FUN_00ac92d0`.

### 6.1 Element tree and override hierarchy

`Vehicle_Anim_Modifiers` → `Human_Seat_Offset` (`vec3`) — a **single global default** at `DAT_027C2ECC` (in the same manager-block region `spec-vehicle-data.md` §7.1 places the vehicle-info live-count globals). → `Vehicles` → repeated **`Vehicle`** → `Name` (resolved through the vehicle-name→entry lookup `FUN_00AC2400`; unmatched names are silently skipped):

Per matched vehicle entry (offsets relative to the vehicle-info entry base, `spec-vehicle-data.md` §7's `0xB80`-stride table):

| Offset | Field |
|---|---|
| `+0x484` | `Human_Scale` (`f32`, if-present) |
| `+0x488`–`+0x493` | `Human_Seat_Offset` (`vec3`) — per-vehicle override; **zeroed (not defaulted to the global) if absent** |
| `+0x90C` + `seatIndex*0x14` | a per-seat sub-record, `0x14` (20) bytes, for each `<Seat>` matched against exactly **4 named seats**: `driver`=0, `front passenger`=1, `back left passenger`=2, `back right passenger`=3 (a *different*, smaller seat convention than §3's 8-seat one) |

Per-seat sub-record (`0x14` bytes):

| Offset | Field |
|---|---|
| `+0x00`–`+0x0B` | `Human_Seat_Offset` (`vec3`) — per-seat override, zeroed if absent |
| `+0x0C` | running count of "weapon animation group" sub-entries |
| `+0x10` | pointer to the sub-entry array, stride `0x14` (20 bytes) |

Each `Weapon_Animation_Groups` → `Weapon_Animation_Group` produces **one base sub-entry**, plus **one more per nested `Animation_States/Animation_State`**:

| Offset | Field |
|---|---|
| `+0x00` | weapon-category index, via a **7-entry** weapon-class table (`FUN_00B81900()` + `FUN_00DAC830`) — almost certainly the same seven `WPNCAT_*` categories `spec-tables-weapons-combat.md` §15.3 names for `weapon_categories.xtbl` |
| `+0x04` | `Animation_State` index (`FUN_004BF810`, the animation-state table); `0xFFFFFFFF` for the group-level sub-entry itself (meaning "default state for this weapon category") |
| `+0x08` | `Human_Seat_Offset` (`vec3`) — per-(weapon-category, animation-state) override, zeroed if absent |

So the override chain is **global → per-vehicle → per-seat → per-weapon-category → per-animation-state**, each level independently overridable — a drive-by/ride animation seat-offset tuning table. **[CONFIRMED — disassembly; `FUN_00ac92d0` was read to its end.]**

### 6.2 Validation — real base rows

5,759 bytes of real base data exist for this table (extracted and byte-length-verified, `tools/harnesses/as_extract.py`); structural predicates were not separately re-run against it this pass (small table, already exhaustively read from code) — flagged in §23 as a light gap, not a blocker.

---

## 7. `externalized_vehicle_components.xtbl` — shared vehicle-customization component catalogue

Loader `FUN_00a971f0`. This is the table `vehicle_wheel_groups.xtbl` (§5) resolves `Front_Rim`/`Rear_Rim`/`Front_Spinner`/`Rear_Spinner` names against.

### 7.1 Element tree and layout

`Custom_vehicle_properties` → `Component_Slots` → repeated **`Slot`** (the child literal is `"Slot"`, `&DAT_0115f3b0`; count → `DAT_027b0bc4`, array `DAT_027b15f0`, stride `0x10` = 4 dwords):

| Offset | Field |
|---|---|
| `+0x00` | `Name` hash `u32` (matched by `FUN_00A96040`, §5) |
| `+0x04` | `Camera_Info` — text hashed; if absent, defaults to a shared "no override" global (`DAT_029C9964`) |
| `+0x08` | flag byte, bit3 set unconditionally per slot (marker, not element-driven) |
| `+0x0C` | count of `Components/Component` children |
| `+0x10`… | pointer to the `Component` array, `0x3C` (60) bytes per element (only a per-component flag byte at `+0x38` bit3 was traced — set for every parsed component; the remaining fields of the `0x3C`-byte record were not walked this pass, OPEN) |

**[CONFIRMED — disassembly for the `Slot` header fields and the component-array allocation/count; the `Component` record's own internal fields beyond the one flag byte are OPEN — see §23.]**

### 7.2 Validation — real base rows

133,707 bytes of real base data extracted and byte-length-verified. Not independently structurally validated this pass beyond the byte-length check (§23).

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

**[CONFIRMED — disassembly.]** **Validation:** 144 real `Vehicle_slot` rows; **144/144 `SlotType` values and 6/6 present `Vehicle_Component_Type` values** match the literal sets above. **[CONFIRMED — empirical.]**

### 8.2 `vehicle_cust_interface.xtbl` (loader `FUN_00818630`) — the customization-menu category list

`Table` → repeated **`vehicle_cust_interface`** (lowercase row tag; matching is case-insensitive throughout). Record `0xA4` (164 bytes), allocated as a C++-style vector with a per-element constructor call.

| Offset | Field |
|---|---|
| `+0x00` | `Display_Name` — raw NUL-terminated text copied in place (unbounded copy loop, no length helper) |
| `+0x80` | `Name` hash `u32` |
| `+0x84` | `parent_category` hash `u32` (0 if absent) |
| `+0x98` | count of `slots/slots` children |
| `+0x9C` | pointer to a `u32[]` array of resolved slot indices |
| `+0xA0` | flags byte: bit0 `is_color_menu`, bit1 `is_wheel_menu` (can only be set, never explicitly cleared), bit3 `is_perf_menu` |

`slots` (wrapper) → repeated **`slots`** (same tag name reused for the wrapper *and* each item — an odd doubly-named idiom) → **`name`** (lowercase child, holding the slot-type text) — resolved via `FUN_00A96130` directly against `vehicle_cust_slots.xtbl`'s own loaded array (`DAT_027b15e4`, §8.1) — a **confirmed cross-reference**, correcting an earlier guess in this same investigation that the wrapper's own text was used (it is not; the string dump of `&DAT_012a2558` = `"name"` settled it). **[CONFIRMED — disassembly.]**

**Validation:** 4 real `vehicle_cust_interface` rows (this is a short, fixed menu-category list — Color/Wheels/Performance/Body-Mod-ish, matching the 4 flags), all with `Display_Name` present; 77 `slots/slots/name` children total across the 4 rows. **[CONFIRMED — empirical.]**

---

## 9. `vehicle_cust_color_pool.xtbl` and `vehicle_cust_color_sets.xtbl` — paint colour catalogue and per-vehicle colour sets

### 9.1 `vehicle_cust_color_pool.xtbl` (loader `FUN_00a94d10`)

`Table` → repeated **`Color`**. Record `0x24` (36 bytes = 9 dwords).

| Offset | Field |
|---|---|
| `+0x00` | a resolved "material" handle (`FUN_00A74910` on the row's `Name` text — the same helper used generically elsewhere in this group as a string-intern/registration call, not necessarily material-specific; HYPOTHESIS on the exact semantic) |
| `+0x04` | `Name` hash `u32` |
| `+0x08`… | a **shader-value block**: a `Shader_Values` wrapper's children are classified against three special names hashed once per load (`Reflection_Cos_Min_Angles`, `Reflection_Inv_Range_Cos_Angles`, `Glass_Color`) plus a generic ordered list of `Vector_Element` (3 floats each) / `Float_Element` (1 float) values, plus a separate list of *String*-valued shader parameters (materials?) copied into a `0x40`-byte-stride name buffer. **The exact byte offsets inside this block were not fully mapped this pass** (mechanism confirmed from a full read of `FUN_00a94d10`; precise per-field offsets OPEN, see §23). |

**[CONFIRMED — disassembly for `+0x00`/`+0x04` and for the shader-value classification mechanism; OPEN for the exact byte layout of the rest of the `0x24`-byte record beyond `+0x08`.]** (694 real `Color` rows exist — §9.3.)

### 9.2 `vehicle_cust_color_sets.xtbl` (loader `FUN_00a952e0`)

**Loads `vehicle_cust_color_pool.xtbl` as a dependency first** (calls `FUN_00a94d10` before opening its own file). `Table` → repeated **`Color_Set`**. Record `0x14` (20 bytes = 5 dwords).

| Offset | Field |
|---|---|
| `+0x00` | `Display_Name` — localized-string handle |
| `+0x04` | `Name` hash `u32` |
| `+0x08` | `Price` (`s32`, always) |
| `+0x0C` | count of `Color_Grid/Color_Element` children |
| `+0x10` | pointer to a `u32[]` array of resolved colour-pool pointers |

`Color_Grid` → repeated **`Color_Element`** → `Color` (text, hashed and linear-scanned against `vehicle_cust_color_pool.xtbl`'s loaded array, `+0x04` key). **An unresolved colour name is not fatal** — the engine `sprintf`s a diagnostic (`"Could not find the color named '%s'."`) into a global buffer and stores 0. **[CONFIRMED — disassembly.]**

### 9.3 Validation — real base rows

694 `Color` rows in `vehicle_cust_color_pool.xtbl`; 29 `Color_Set` rows / 874 `Color_Element` children in `vehicle_cust_color_sets.xtbl`. **874/874 `Color_Element/Color` names resolve to a real row in the colour pool** (case-insensitive match against the 694 pool names) — the cross-table reference is exercised, not dead. **[CONFIRMED — empirical.]**

---

## 10. `vehicle_surfing_style_two.xtbl` — global surfing/handstanding tuning

Loader `FUN_006c3eb0`. Unlike almost every other table in this group, this is a **single-row global settings table**: it reads exactly one `Vehicle_Surfing` element and writes into a compact, non-indexed global block — there is no per-vehicle or per-row array.

| Element | Destination | Conversion |
|---|---|---|
| `Max_Surfing_Time` / `Min_Surfing_Time` (f32, always) | `DAT_014c770c` / `DAT_014c7710` | both forced to 0/0 if `Max ≤ 0` or `Max < Min`; else × a seconds→ticks constant (`DAT_012a2d90`), rounded |
| `Max_Handstand_Time` / `Min_Handstand_Time` (f32, always) | `DAT_014c7714` / `DAT_014c7718` | same rule and conversion |
| `Max_Handstand_Percent_Reward` (f32, always) | `DAT_014c7724` | ÷ a percent→fraction constant (`DAT_012a2dd8`), then clamped to `[0, DAT_01117a4c]` |
| `Surfing_Min_Speed` (f32, always) | `DAT_014c7728` | clamped ≥ 0, × `0.44704` (mph→m/s, `spec-vehicle-data.md` §7.2's constant), **squared** (stored as a squared-speed threshold) |
| `Surfing_Timeout` (f32, always) | `DAT_014c7700` | clamped ≥ 0, × ticks constant, rounded |
| `Start_Time` (f32, always) | `DAT_014c7704` | same |
| `Max_Speed_Delay` (f32, always) | `DAT_014c7708` | same |
| `Record_Display_Time` (f32, **if-present**) | `DAT_014c771c` | 0 if absent; else clamped ≥ 0, × ticks constant |
| `Record_Queue_Time` (f32, if-present) | `DAT_014c7720` | 0 if absent; else same |
| `Record_Threshold` (f32, always) | `DAT_014c7730` | clamped ≥ 0, ÷ percent constant |
| `Handstanding_Sensitivity_Multiplier` (f32, always) | `DAT_014c772c` | clamped to a floor `DAT_01117a4c` |
| `Max_Respect` (s32, always) | `DAT_014c7734` | clamped ≥ 0 |
| `Max_Lifetime_Respect` (s32, always) | `DAT_014c7738` | clamped ≥ 0 |
| `Max_Cash` (f32, always) | `DAT_014c773c` | clamped ≥ 0 |

**The `Record_Display_Time`/`Record_Queue_Time`/`Record_Threshold`/`Max_Respect`/`Max_Lifetime_Respect`/`Max_Cash` sextet is the exact same "on-screen stunt record" field group `spec-tables-weapons-combat.md` §14.4 (`windshield_cannon.xtbl`) and §11.2 (`combat_tricks.xtbl`) already document** — confirmed shared idiom, third table using it. A trailing call `FUN_006a4ad0(vehicleSurfingNode)` reads a **nested `Balance_Bar_Params`** block (12 more `f32` fields: `Balanced_Region_Size`, `_Size_Change`, `_Min_Size`, `_Acceleration`, `Balanced_Acceleration_Change`, `_Max`, `Unbalanced_Region_Acceleration`, `Unbalanced_Acceleration_Change`, `_Max`, `Balancing_Acceleration`, `_Change`, `_Max`) into a 22-dword local struct, then does an SR2-legacy-asset lookup (`"sr2_balance_meter"`) unrelated to the XML. **[CONFIRMED — disassembly.]**

---

## 11. The shared "LightSet" table — `Vehicle-Customization-Lightset.xtbl`, `store_gang_lightset.xtbl`, `test_shop_light_01.xtbl`

**These three tables (plus `spec-tables-weapons-combat.md` §17 item 6's still-open `store_weapon_lightset.xtbl`) all share one generic lightset loader**, resolving a previously-undocumented shared schema. Confirmed by code for two of the three consumers here (`FUN_00819210`/`FUN_00810330` for the vehicle-customization file, `FUN_00810280` for `store_gang_lightset.xtbl`, `FUN_008367c0` for `test_shop_light_01.xtbl`), all calling the same pair: `FUN_005990c0(filename)` (resolve-or-create a lightset object by filename hash — registry array `DAT_013de2c8`, stride `0x10`, count `DAT_013de2bc`) then `FUN_00598160(handle, mode, extra)` (per-row activation pass).

### 11.1 What the disassembly confirms — the *runtime activation* record, not the XML schema

Each lightset's row array has stride `0x70` (112 bytes); `FUN_00598160` reads/writes, per row: a mode byte at `+0x14`; a presence flag at `+0x1C` (checked on unload to clear two "any lightset active" globals); a resource pointer at `+0x10` gating a live ambient-probe capture; a float at `+0x20` (with a gating byte at `+0x24`) compared against a threshold (`DAT_0125e270`) to drive a light fade-in; and **two reflection-probe colour quads at `+0x30`–`+0x3C` and `+0x40`–`+0x6C`**, either populated live by an SH-probe capture (`FUN_00DA38E0` + `FUN_004ADD90`) or — for the three consumer functions in *this* group — **immediately overwritten with fixed, per-consumer colour constants** (each consumer supplies its own `0x30`/`0x40`/`0x50`/`0x60` quad of hard-coded globals; `test_shop_light_01.xtbl`'s consumer additionally supports a runtime "gang colour" clone via `FUN_00596ff0`). **This is a real, useful finding, but it is *not* the table's own XML-sourced content** — those `+0x00`–`+0x0F` bytes (and the object-creation path `FUN_005984c0`, not decompiled this pass) are where the real per-row XML fields must live; that function was not traced. **[CONFIRMED — disassembly for everything stated; the XML→struct mapping itself is OPEN, see §23.]**

### 11.2 What the real base rows confirm — the actual XML vocabulary

All three files were pulled from `misc_tables.vpp_pc` and read raw (they are small, one row each — `Table` → one **`LightSet`** element): `Name`, `StartTime`, `EndTime`, `Exposure`, `RampExposure` (bool), then **`Lights` → repeated `Light`**, each with: `Name`, `Type` (seen: `omni`, `circular spotlight`), `Category_0`..`Category_3` (bool), `CastShadows` (bool), `Color` (three 0–1 floats, space-separated — an inline vec3, *not* `X`/`Y`/`Z` children, unlike almost every other vec3 in this project), `Multiplier` (f32), `Template` (bool), `Position` (inline vec3), `Orientation` (two floats — azimuth/elevation), `Indoor`/`Outdoor` (int flags), `Hotspot` (two floats — spotlight cone angles), `Attenuation` (two floats — near/far falloff), `Slate_Name`, `LightCharacter`, `ShadowCharacter`, `LightLevel`, `ShadowLevel`. **[CONFIRMED — empirical; element vocabulary only, no byte offsets — the reader that consumes this vocabulary (`FUN_00598160`'s missing counterpart, almost certainly inside `FUN_005984c0`) was not decompiled this pass.]** Real row counts: 1 `LightSet` in each of the three files (`tools/harnesses/as_validate.py`).

---

## 12. `level_objects.xtbl` — placeable-object physical/audio/visual properties catalogue

Loader `FUN_008e8280` (base entry point `FUN_008e8540`; DLC entry point `FUN_008e84c0`, matching the DLC content registry's `level_objects` handler `spec-tables-weapons-combat.md` §15.2 already lists). The real per-row content builder is `FUN_008e7490`.

### 12.1 Record layout (`0xC8` = 200 bytes)

| Offset | Field | Notes |
|---|---|---|
| `+0x00`–`+0x2F` | `Name` (`char[0x30]`) | row key, also hashed for lookup |
| `+0x30` | `Hitpoints` (`u32`) | |
| `+0x34` | `Material` (`u8`) | matched against the **33 physical-material names** (`spec-tables-weapons-combat.md` §1.6's `FUN_006F76F0`); default index **31** (`0x1F`) if no match — confirmed reuse of that exact resolver |
| `+0x38` | `Lifetime_seconds` → ticks (`s32`) | 0 unless the value is ≥ a small threshold (`DAT_0125e270`); converted `value*ticksPerSec + rounding` |
| `+0x3C` | `Weight` → scaled `f32` | negative raw values get an unsigned-wraparound correction, then × a unit constant |
| `+0x40` | `Friction` (f32, if-present) | default `DAT_0126d2cc` |
| `+0x44` | `Restitution` (f32, if-present) | default `DAT_01115d6c` |
| `+0x48` | `Angular_Damping` (f32, if-present) | default `DAT_01115d6c` |
| `+0x4C` | `Linear_Damping` (f32, if-present) | default `DAT_012a2dd0` |
| `+0x50` | `Surface_Velocity` (f32, if-present) | default 0 |
| `+0x54` | `Buoyancy_Modifier` (f32, if-present) | default `DAT_01117a4c` |
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
| `+0x94`–`+0x9F` | `Death_Money`'s own `X`/`Y`/`Z` (`vec3`) | read as a "Cash_Out_Point"-labelled vec3 (`FUN_00DACFB0`); confirmed empirically to come from `Death_Money`'s own `X`/`Y`/`Z` children, not a separate wrapper — see §12.3 |
| `+0xA0` | presence byte for the above vec3 | |
| `+0xA1` | `Death_Money/Just_Coins` (bool) | |
| `+0xA4` | `Vehicle_Repulsor_Scale` (f32, if-present) | default `DAT_01117a4c` |
| `+0xA8`–`+0xB3` | `center_of_mass/com_offset` (`vec3`) | |
| `+0xB4`–`+0xBF` | `center_of_mass/com_offset_corpse` (`vec3`) | |
| `+0xC0` | flags dword 1 | bit0-1 `Anchored` present, bit13 `Anchored/Dislodge_On_Death`, bit24 `Movable_By_Humans`, bit25 `Vehicle_Obstacle`=`false`, bit26 `Vehicle_Obstacle`=`unanchored`, bit28 `VehicleIVS` present, bit29 `ObjectMVS` present, bit30 `Anchored/Dislodge_Notoriety`, bit31 `Flag: receives_bullet_impulse` |
| `+0xC4` | flags dword 2 | bit19 `Anchored` present (secondary marker) + the 26 `Flag` bits, §12.2 |

`Vehicle_Obstacle` is a 3-literal enum (`true`/`false`/`unanchored`); only the last two set a bit (`true` is the "do nothing extra" default). **[CONFIRMED — disassembly; `FUN_008e7490`, 2,792 decompiled lines, read to its end.]**

### 12.2 The 27-literal `Flags` vocabulary (all on `+0xC4` except the first)

`receives_bullet_impulse` (`+0xC0` bit31), `disappear_on_death`, `disable_lights_on_dislodge`, `disable_effects_on_dislodge`, `ignore_human_collision`, `camera_collide`, `vehicle_camera_collide`, `ignore_bullet_collision`, `bullets_penetrate`, `fire_hydrant`, `do_not_aim_at_me`, `non_walkable`, `can_walk_up`, `nearby_player_despawn`, `shatters_against_world`, `blackjack`, `poker`, `zombie`, `basketball`, `tv`, `no_detour_until_moved`, `does_not_generate_detour`, `generate_detour_even_if_in_air`, `breakable_glass`, `ai_los_ignore`, `damaged_by_players_only`, `no_brute_pickup` — an exhaustive `_stricmp` chain, no catch-all. **[CONFIRMED — disassembly.]**

### 12.3 Validation — real base rows, and one dead-element finding

149 real `Level_Object` rows: **149/149 have `Name`; all 357 `Flag` children across all rows match one of the 27 literals above (357/357)**; 15 distinct `Material` strings seen (`Cardboard`, `Concrete`, `Cyberspace`, `Electric`, `Flame`, `Glass - Heavy`, `Glass - Medium`, `Metal - Fence`, `Metal - Solid`, `Metal - Thin`, …). `Death_Money`'s `X`/`Y`/`Z` and `Vehicle_Obstacle` were confirmed present with exactly the shapes §12.1 predicts (`<Death_Money><Min>0.0</Min><Max>0.0</Max><Just_Coins>False</Just_Coins><X>0.0</X><Y>0.0</Y><Z>0.0</Z></Death_Money>`, `<Vehicle_Obstacle>true</Vehicle_Obstacle>`). **One dead-element finding**: real `Anchored` blocks carry a `Dislodged_By` wrapper with repeated `Element` children (e.g. `<Dislodged_By><Element>none</Element></Dislodged_By>`) that **`FUN_008e7490`'s full body never reads** — authored data with no consumer in this reader, same family as `spec-tables-weapons-combat.md` §18's dead-element findings. **[CONFIRMED — empirical, `tools/harnesses/as_validate.py`.]**

---

## 13. `props.xtbl` — per-activity decorative-prop counts

Loader `FUN_0060ea40`. **Not a world-placement table** (despite the name) — a tiny fixed configuration table: 14 named activity slots, each with one integer.

`Table` → repeated **`Activity`** → `Name` (bounded string, matched `_stricmp` against a **fixed 14-literal list**), `Num_Props` (`s32`, always). Matched rows store `Num_Props` into a 14-`u32` global block (`DAT_014b02e4`, zeroed with a `memset(…, 0x38)` before parsing — `0x38` = 14×4 bytes, confirming the capacity). The 14 recognised names: `assault_thug`, `assault_killa`, `assault_gangsta`, `assault_kingpin`, `kill_thug`, `kill_killa`, `kill_gangsta`, `kill_kingpin`, `shooting_thug`, `shooting_killa`, `shooting_gangsta`, `shooting_kingpin`, `destroy_gang_car`, `collect_item_pickup`. **[CONFIRMED — disassembly.]**

**Validation:** all 14 real base `Activity` rows are present with exactly these names (space-separated in the XML, e.g. `assault thug`) and all 14 have `Num_Props`. **[CONFIRMED — empirical, 14/14.]**

---

## 14. `triggers.xtbl` — default properties patched onto pre-placed trigger instances

Loader `FUN_0093e0c0`. **This table does not allocate its own row array.** Each row's `Name` is hashed with the **multiply-by-33 bucket hash** (`FUN_00DAB330(name, 0x14)` — the same hash family `spec-tables-weapons-combat.md` §1.4 documents for camera-shake names, here with modulus 20 instead of 128) and looked up in an **already-populated runtime hash table** (buckets/chain-next/comparator/key arrays around `0x02622f0d`/`0x02622f21`/`0x02622f08`/`0x02622f38`) that maps a name to a **pre-existing trigger-instance slot index**. **A row whose name has no match in that table is silently discarded — nothing is stored.** This is a strong, direct structural link to wherever trigger *instances* are placed in the game world and named (almost certainly the zone/level data, i.e. `.czn_pc`'s parked object-placement/property-stream interior — noted here as a cross-reference only, per the standing instruction; that interior was not opened, touched, or acted on).

### 14.1 Per-instance record (patched fields; base `0x026232A8`, stride `0x24` = 36 bytes, indexed by the matched slot)

| Offset | Field |
|---|---|
| `+0x00` | `Effect` → effects CRC (`FUN_005C50B0`) |
| `+0x04` | `Icon` → a value resolved through `FUN_00802f60` |
| `+0x08` | `Foley` → audio id (`FUN_00462960`) |
| `+0x0C` | derived icon-frame index — only computed when `IconType` matches the literal `Store` (see below), from the `+0x04` value's range (`0x18`–`0x21`→7, `0x22`–`0x23`/`<0x24`→8) |
| `+0x10` | `UseMessage` → localized-string handle |
| `+0x20` | flag: `Flags` child present |
| `+0x21` | flags byte: bit0 `check_npcs`, bit1 `continuous_activation`, bit2 `disabled_for_demo`, bit4 `ignore_vehicles`, bit5 `ignore_on_foot` |

`IconType` is a 2-literal enum: `Save`=0, `Store`=1 (only `Store` triggers the `+0x0C` derivation). Offsets `+0x14`–`+0x1F` of the per-instance record are not touched by this reader — presumably filled by whatever else builds the trigger instances (again, out of scope). **[CONFIRMED — disassembly; `FUN_0093e0c0` read to its end.]**

### 14.2 Validation — real base rows

20 real `Trigger` rows, all with `Name`; 10 total `Flag` children, **10/10** matching the 5-literal set above. **`IconType` does not appear in any real row** (`0/20` — the literal only appears in the table's own embedded `TableDescription` schema block, not in any authored `Trigger`; consistent with it being a rarely-used field, not a parsing gap). **[CONFIRMED — empirical.]**

---

## 15. `items_inventory.xtbl` — player inventory item catalogue

Loader `FUN_008dc8c0`. Array anchor already known from `spec-tables-weapons-combat.md` §1.6 (`0x0250ADC0`, stride `0x34`, **110 slots**, live bit `+0x30`; resolver `FUN_008DCB10`, `_stricmp` on `+0x00`). This is the table `spec-save-format.md` §10.3 ties to save-file weapon-inventory ids via the item's `Name` hash (`spec-tables-weapons-combat.md` §16.3).

### 15.1 Record layout (`0x34` = 52 bytes)

| Offset | Field | Default if absent |
|---|---|---|
| `+0x00` | `Name` → interned string pointer (`FUN_00A74910`) | — (row key) |
| `+0x04` | `Name` hash `u32` | — |
| `+0x08` | `DisplayName` → localization handle (`FUN_0084A280`) | placeholder string `&DAT_0111fe04` |
| `+0x0C` | `DisplayName` → resolved display string pointer (`FUN_00849FF0`) | 0 |
| `+0x10` | `Bitmap` → resource handle (`FUN_00E22290`) | `−1` if the `Bitmap` text is empty |
| `+0x14` | `Impact_shape_min_offset` (f32, if-present) | 0 |
| `+0x18` | `Cost` (s32, if-present) | `−1` |
| `+0x1C` | `Default_Count` (s32, if-present) | 1 |
| `+0x20` | `Max_Inventory` (s32, if-present) | = the resolved `Default_Count` value |
| `+0x24` | `Description` → resolved string pointer (`FUN_0084A280` + `FUN_00849FF0`) | 0 |
| `+0x28` | load-order ordinal (`u8`, 0,1,2,… assigned sequentially) | — |
| `+0x2C` | `Use_Script` (s32) | 0 if the child is absent; else matched against a **single recognised literal `none`**, then indexed into a translation table `DAT_0116BB20` (no-match reads `table[−1]`, i.e. the dword immediately preceding the table — evidently by design, not examined further) |
| `+0x30` | flags byte, bit0 = **live** (always set for a kept row — matches `spec-tables-weapons-combat.md` §1.6's cited live bit exactly) | — |

**Row selection differs from the group idiom**: the **base load accepts every row unconditionally, regardless of its `Framework` text** (only a DLC-framework load filters by exact match) — items_inventory does not use the usual base-vs-DLC two-pass `Framework` filter for its *base* pass. **[CONFIRMED — disassembly; `FUN_008dc8c0` read to its end.]**

### 15.2 Validation — real base rows

94 real `Inventory_Item` rows (against the documented capacity of 110 — 16 slots unused in the base game). **94/94 have `Name`; of the 94 with `Default_Count` present, 94/94 satisfy `Default_Count ≤ Max_Inventory`** (the defaulting rule holds when both are given explicitly, and trivially when `Max_Inventory` is absent). **[CONFIRMED — empirical.]**

---

## 16. `items_3d.xtbl` — 3D item/prop mesh catalogue (partial)

Array anchor from `spec-tables-weapons-combat.md` §1.6: `0x025F5B98`, stride `0xB8` (184 bytes), count `0x025F5B8C`, key CRC at `+0x04`, resolver `FUN_00904C10`. This table is what `weapons.xtbl`'s `Name` and `Projectile_Info/Model` cross-reference (`spec-tables-weapons-combat.md` §15.3).

### 16.1 What is confirmed

`Table` → repeated **`Item`** (row tag confirmed via the literal dump, `&DAT_011462fc` = `"Item"`). Loaders: base `FUN_008fa7a0`, DLC `FUN_008fd120` (matches the DLC registry's `items` handler, `spec-tables-weapons-combat.md` §15.2); real per-row content is built by `FUN_00904d10` (reached through `FUN_00905630`→`FUN_009052e0`, **not decompiled this pass** — see §23). Element names confirmed present and consumed by *other* passes over the same document (`FUN_00739b20`, `FUN_00be02c0`, `FUN_008c7e30`, all read fully): `Mesh` (a `Filename`-bearing wrapper, resolved to `.csmesh_pc`), `character_mesh` (wrapper containing a nested `character_mesh`→`Filename` for the `.ccmesh_pc` and a `Rig`/`rig`→`Filename` for the `.rigx`), `Props` → repeated `Prop` → `Flags` → repeated `Flag` (tested against the literal `Attach by default`), `LargeProp` (bool), `streaming_category` (text, resolved via `FUN_00904770`).

**A correction made mid-investigation, worth recording explicitly:** the strings `"items_preload_containers"` and `"items_containers"` passed as the 2nd argument to `FUN_00905630` are **not XML section names** — no such tag exists anywhere in the real base file (confirmed by a raw-text search of the extracted 269,240-byte base `items_3d.xtbl`). They are **profiler/memory-pool zone labels** (the same idiom as the neighbouring `"object item parsing"`/`"dlc object item parsing"` labels passed to `FUN_00db64a0`), differentiating two *passes* over the same `<Item>` rows (a "preload" pass and a normal pass), not two different data sections. The real per-item "contained items" fixup mechanism (a per-item sub-array of `0x1C`-byte entries, each resolving a contained-item name against `items_3d` itself via `FUN_00904C10` or, on a miss, against the effects registry via `FUN_005C50B0`, and cross-writing the target's own `+0x1C` with a "type" tag) is confirmed to exist in `FUN_00905630`/`FUN_00905e30`'s caller (`FUN_008fa7a0`/`FUN_008fd120`), but **its own XML element name was not identified this pass** (candidates not ruled out: a per-`Item` child element, since no separate top-level section exists).

Real base rows also show elements not traced to any reader in this pass — `Anim_set` (text, e.g. `none`/`NONE`, seen inside `character_mesh`), `Glow_Type`, `Scale_Ambient`, `Color_Variants` (empty wrapper), `Item_Flags` (a *different* wrapper from `Props/Prop/Flags`, e.g. `<Item_Flags><Flag>inherit_bone_transforms</Flag></Item_Flags>`) — these are HYPOTHESIS/OPEN, not confirmed dead, since the true per-row reader (`FUN_00904d10`) was not read.

### 16.2 Validation — real base rows

391 real `Item` rows: **391/391 have `Name`**; 125 have a `Mesh` child, 266 have `character_mesh`, and **all 391 have a `Props` wrapper** (usually empty). **[CONFIRMED — empirical for presence counts; the full per-item byte-offset schema is OPEN, see §23.]**

---

## 17. `contacts_sr3.xtbl` — in-game phone contact list

Loader `FUN_0083b750` (base entry `FUN_0083b870`; DLC entry `FUN_0083b940`, matching the DLC registry's `contacts` handler, `spec-tables-weapons-combat.md` §15.2, with the `<framework>_contacts_sr3.xtbl` naming convention `FUN_0045ba50` builds).

### 17.1 Record layout

`Table` → repeated **`Contact`** (row tag confirmed empirically, §17.2). Fixed array, capacity **35** (`0x23`, gated in the loader), stride `0x84` (132 bytes):

| Offset | Field |
|---|---|
| `+0x00`–`+0x3F` | `Name` (raw NUL-terminated string, unbounded copy) |
| `+0x40`–`+0x7F` | `Image` (same) |
| `+0x80` | `Persona` → resolved via the audio middleware string→id function `FUN_00462960` (the same resolver used for sound-event names elsewhere in this project — `Persona` here is almost certainly a voice-line/character-audio persona id, matching the persona theme of §18's table) |

**[CONFIRMED — disassembly.]**

### 17.2 Validation — real base rows

31 real rows (of a capacity of 35 — this is the game's phone-contacts roster, e.g. Kingdom Come Records staff and gang leaders by name), row tag confirmed as `Contact`; **31/31 have `Name`, `Image`, and `Persona`**. **[CONFIRMED — empirical.]**

---

## 18. `activity_player_persona_replacement.xtbl` — per-activity player-persona substitution

Loader `FUN_00615430`.

### 18.1 Record layout

`Table` → repeated **`Activity_Persona_Replacement`** → `Name` (resolved via the activity-name resolver `FUN_00614d70`; unmatched names are silently skipped) → `Persona_Replacements` → repeated **`Persona_Replacement`** (`8`-byte record, array per activity, `count`/`pointer` pair in a fixed table indexed by activity):

| Offset | Field |
|---|---|
| `+0x00` | `Original` — persona name, resolved via `FUN_0070a2f0` |
| `+0x04` | `Replacement` — same resolver, if-present; 0 (none) if absent |

**[CONFIRMED — disassembly.]**

### 18.2 Validation — real base rows

3 real `Activity_Persona_Replacement` rows, all with `Name`; 6 total `Persona_Replacement` children, **6/6 with `Original` present**. **[CONFIRMED — empirical.]**

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

If a curve has ≥ 2 points, a "slope" value is derived from the first two points' height/distance deltas via a two-argument trig function (`FUN_00dae5b0`, almost certainly `atan2`) and stored as a third global field alongside the curve's own pointer/count — the exact consumer-side meaning of this derived value was not traced (HYPOTHESIS: an approach-angle used to blend/extrapolate beyond the sampled points). **[CONFIRMED — disassembly for the element tree, offsets, and two-curve limitation; HYPOTHESIS for the derived slope's downstream use.]**

### 19.2 Validation — real base rows

Exactly 2 real `Curve_Params` rows, named `Landing` and `Take Off` — **confirming the two-curve limitation empirically, not just from the code path.** 11 total `Point` children across both curves. **[CONFIRMED — empirical.]**

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
| `level_objects.xtbl` | `0x0116cbf4` | `FUN_008e8540` / DLC `FUN_008e84c0` (row builder `FUN_008e7490`) | 12 |
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
| `vehicle_interaction_info.Seat_Info/Element/{Seat,Primary_Access_Seat,Secondary_Access_Seat}` | the shared 8-seat resolver (`FUN_00AC1BA0`) also used by `spec-vehicle-data.md` §7.3/§7.4's `ProhibitedGunfireSeats`/`Seat_Specific_Data` | `_stricmp` against two parallel 8-name tables |
| `vehicle_wheel_groups.{Front,Rear}_{Rim,Spinner}` | `externalized_vehicle_components.xtbl` `Slot` rows | CRC hash, linear scan (§5, §7) |
| `vehicle_cust_interface.slots/slots/name` | `vehicle_cust_slots.xtbl` `Vehicle_slot` rows | CRC hash, linear scan (§8) |
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

No predicate failed. No table's schema was contradicted by real data; three corrections were made *during* validation, before being written above rather than after (the `Seat` vs `Name` child tag in §3, the `name` vs own-text child in §8.2, and the `items_preload_containers`/`items_containers` label-vs-section correction in §16).

---

## 23. Open items

1. **`vehicle_cust_color_pool.xtbl`'s shader-value block** (§9.1) — the classification mechanism (three special reflection/glass parameters, a generic `Vector_Element`/`Float_Element` list, a separate string-valued list) is confirmed from a full read of `FUN_00a94d10`, but the exact byte offsets within the `0x24`-byte `Color` record beyond `+0x08` were not mapped.
2. **The shared "LightSet" table's own XML→struct mapping** (§11) — `FUN_00598160` only covers the *runtime activation* pass (probe capture / colour override / fade-in), not the XML row's own fields (`Name`, `StartTime`, `EndTime`, `Exposure`, `RampExposure`, and the `Lights`/`Light` sub-array). The real reader is almost certainly inside `FUN_005984c0`, not decompiled this pass. Real element vocabulary is confirmed empirically (§11.2); byte offsets are not.
3. **`items_3d.xtbl`'s full per-item byte-offset schema** (§16) — the array location/stride/count/key are known (from `spec-tables-weapons-combat.md` §1.6), the row tag and several element names are confirmed, and the "contained items" fixup mechanism's existence is confirmed, but the actual per-row builder `FUN_00904d10` was not decompiled, so no offset table exists for `Mesh`/`character_mesh`/`Props`/`streaming_category`/`Anim_set`/`Glow_Type`/`Scale_Ambient`/`Color_Variants`/`Item_Flags` etc. The "contained items" list's own XML element name (a per-`Item` child, given no separate top-level section exists) is unidentified.
4. **`externalized_vehicle_components.xtbl`'s `Component` record** (§7) — only one flag byte (`+0x38` bit3) of its `0x3C`-byte layout was traced; the rest is unread. **Confirmed present in real shipped data (2026-09-28, Team B; re-verified directly against the real file, 235/199/199/199 occurrences respectively): `Name`, `DisplayName`, `Price`, `Buyable` are all real, populated fields on the `Component` record — not traced to a byte offset this pass (deliberately not added to the schema without disassembly, per this document's own standing rule against inferring structure from data alone) but a concrete follow-up target with real, non-empty data ready to validate against once the offsets are found.**
5. **`vehicle_animation_modifiers.xtbl`'s weapon-category 7-entry table** (`FUN_00B81900`, §6) was used but not itself dumped to confirm its 7 literals are exactly the `WPNCAT_*` set; treated as HIGH CONFIDENCE by the "7 categories" match to `spec-tables-weapons-combat.md` §15.3, not independently verified.
6. **The `.czn_pc` connection noted in §21.1** is exactly that — a noted structural observation from `triggers.xtbl`'s pre-populated-hash-table pattern. It was not investigated further, no `.czn_pc` file was opened, and this remains fully within the project's standing hold on that item.
7. Several small semantic HYPOTHESES left unresolved for lack of a consumer-side trace: the `Capsule_Shape` indirection table's real meaning (§3.2), the airplane takeoff/landing curve's derived "slope" value's downstream use (§19.1), and `FUN_00A74910`'s exact generic role (string intern vs. material-specific — used identically in §9.1, §15.1 and elsewhere in this project).
8. `vehicle_animation_modifiers.xtbl` and `externalized_vehicle_components.xtbl` were extracted and byte-length-verified against real base data but not run through the same per-field validation harness as the rest of §22 — a light, explicitly-flagged gap, not a contradiction.

No confirmed fact in this document was invented past what the disassembly or the real base rows support; where the trail ran out (items 1–4 above), that is recorded as OPEN rather than guessed, per `HANDOFF.md` §27.8.

---

## 24. Artifacts

Ghidra project copy: `tools/gp_as1` (disposable, robocopied from `tools/ghidra_projects`). New Ghidra script: `tools/scripts/AsPtrStrs.java` (dereferences a literal-pointer table to its ASCIIZ strings — reusable by later agents; `AfStrXrefs.java`, `AfDec.java`, `AfMem.java` reused unchanged from agent AF's toolkit). Dumps: `tools/as_xrefs1.txt`, `tools/as_dec1.txt`…`as_dec5.txt`, `tools/as_ptrstrs1.txt`, `tools/as_strs1.txt`, `tools/as_strs2.txt`. Harnesses: `tools/harnesses/as_extract.py` (base-row extractor for all 24 tables, `tools/as_base_xtbl/`), `tools/harnesses/as_validate.py` (the §22 predicates). The parked `.czn_pc` interior was not touched (§21.1, §23 item 6).

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): qualified §6/§7 in the §1.1 coverage table (not structurally validated / `Component` record OPEN); fixed 3 cross-references (weapons-combat §14.5→§14.4, save-format §10.3/§16→§10.3, §22→§21.1); swapped the reversed From/To of the `vi_*` row in §21; added the 52-row patch-copy caveat to the §22 `vehicle_interaction_info` row.
