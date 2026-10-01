# Saints Row: The Third — Character/Vehicle Customization Table Schemas (`spec-tables-customization.md`)

**New 2026-09-23, agent AQ.** Loader-recovered schemas (Ghidra disassembly, plain DX9 `SaintsRowTheThird.exe`) for the character/player-customization `.xtbl` group: `customization_*.xtbl`, `character_*.xtbl`, the shared named-color-pool family, and the `player_creation*`/`player_cust_*`/`player_master_sliders`/`player_presets`/`player_regional_presets` face-and-body-creation subsystem — plus a full round-up of the group's dead/unreferenced sibling files. Method matches every other `spec-tables-*.md` in this session: (1) confirm each table's filename as an exact, singly-occurring literal in the executable via Ghidra; (2) follow it to its loader, decompile, and recover the per-row/per-field structure; (3) cross-check against real shipped base-game rows extracted from `misc_tables.vpp_pc` via the mode-(a) container fix (`spec-vpp-container.md` §7), parsed tolerantly (`spec-xtbl-format.md` §7) with `tools/harnesses/an_tolerant_parse.py`, **counting only each table's own `<Table>` node's direct children** — never `TableTemplates`/`TableDescription` scaffolding (see §21 for two real, confirmed-empty `<Table>` cases this caught).

**Relationship to `spec-customization-data.md`:** that earlier document (2026-09-20) covered the same subject area from DLC raw samples alone, before the mode-(a) base-container fix existed ("No new disassembly this pass" — its own words). This document supersedes it for every table both cover (`customization_items.xtbl`, `customization_outfits.xtbl`, `customization_slots.xtbl`, `customization_slot_defaults.xtbl`): the base-game rows it correctly flagged as inaccessible are now readable and validated below (§21), and every field it inferred from DLC samples is independently confirmed from the loader's own disassembly. `spec-customization-data.md` §5 (the `custmesh_<N>.str2_pc` bundle-name hash recipe) and §3 (vehicle customization / `vehicle_cust_*` family) are untouched and still current — this document does not re-cover vehicle customization at all (see scope note below).

**Explicitly out of scope (owned elsewhere — cross-referenced by section number, not re-covered):** `vehicle_cust_color_pool.xtbl` and `vehicle_wheel_groups.xtbl` (`spec-tables-vehicle-world.md` §9.1, §5), the whole `vehicle_cust_*`/`Vehicle-Customization-Lightset.xtbl` family (`spec-tables-vehicle-world.md` §8–§11), the `*_cameras*.xtbl` family including `player_cust_cameras_new.xtbl`/`item_cust_cameras.xtbl`/`vehicle_cust_cameras_new.xtbl` (`spec-tables-ui-controls.md` §8.1–§8.2), `character_visemes.xtbl` (`spec-tables-animation.md`), `distant_ped_colors.xtbl`/`distant_vehicle_colors.xtbl` (`spec-tables-traffic-ai.md` §14), and `material_color_variants.xtbl` (`spec-tables-environment.md` §11.5).

**Review status summary (2026-09-30):** an adversarial desk review (`review/adv_tables-customization.md`) checked 36 units: 2 DESK-PASS, 2 DESK-PASS with text fixes applied, 4 VALIDATED-BY-DATA, 1 NEEDS-DATA, 27 NEEDS-EXE. A desk pass alone does not clear a unit: it only means the text is internally consistent; VALIDATED-BY-DATA units are already backed by Team B's full-population run (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements"), which covers element trees, row tags and row counts but NOT runtime record offsets, record sizes, caps or loader addresses. Cross-cutting: every row cap in this document is stated either without its compare instruction or only as a prose "exceeds N" test, so whether each loop keeps N or N-1 rows is OPEN until the compare is read in the executable. Units awaiting the executable: §2, §3.1, §3.2, §4.1, §4.2, §4.3, §4.4, §5.2, §7.2, §7.3, §8, §9.1, §9.2, §9.3, §10, §11, §12, §13.1, §13.2, §13.3, §14.1, §14.2, §15.1, §16.2, §16.3, §17, §18. Units awaiting real data: §15.2 (plus data checks noted in §10, §11, §15.1).

## 1. Overview, method, and the shared subsystem map

### 1.1 Assignment and coverage

A larger candidate list than the original 30 was ultimately examined for this group (the full candidate list produced by grepping `customization_*`/`character_*`/`*color*`/`player_creation*`/`player_*` entries out of `misc_tables.vpp_pc`, `da_tables.vpp_pc` and `patch_compressed.vpp_pc`, then removing everything already owned by another spec — see the scope note above). **Corrected 2026-09-28 (Team B flagged this sentence didn't match §2–§17's own content; re-verified directly by SPEC TEAM against §20's own index): 35 tables have exactly one real code loader and are covered to full field/element level below (§2–§17, cross-checked directly against §20's own literal-and-loader index, which has 35 data rows) — this document's scope grew substantially after an original "18" figure was written, and that sentence was never updated to match.** 12 further candidate filenames exist as real archive entries but have ZERO code references anywhere in the executable — confirmed dead/orphaned, round-up in §18. Two more filenames (`emotions.xtbl`, `death_faces.xtbl`) were incidentally reached while tracing a neighbouring function but are facial-expression/death-pose tables, not customization data — out of this document's scope, not covered.

### 1.2 The shared XML accessor grammar — cited, not re-derived

Every reader below uses the same primitives already documented in `spec-tables-diversions.md` §1.3 and confirmed project-wide: `FUN_00dac9a0` (open table by filename, cached), `thunk_FUN_00dc4ff0`/`FUN_00daba40` family (get first child element by name, case-insensitive `__stricmp`), `thunk_FUN_00dc5030` (get next sibling by name), `thunk_FUN_00dc5150` (count children by name), `FUN_00daba10` (get element text), `FUN_00dac7d0` (case-insensitive text-equality flag test, used throughout for `Flags>Flag` membership tests), `FUN_00dac830` (name → index lookup against a fixed string array — the mechanism behind every "known name" enum in this document), `FUN_00d9e740`/`FUN_00d9e7e0`/`FUN_00d9e8b0` (the engine string hash~~, `spec-tree-format.md` §13's `FUN_00da7890`~~ **[corrected: these are the engine's table-driven CRC-32, `spec-tables-weapons-combat.md` §1.4, with `FUN_00d9e7e0` the non-lower-casing entry point, `spec-tables-audio-radio.md` §1.3 item 2; `FUN_00da7890` is the separate rotate-6/XOR hash, `spec-vpp-container.md` §2.2]**, applied to an element's text and returned as a 32-bit id), `thunk_FUN_00dab960` (close/free the parsed document). `FUN_004bf810` resolves a `State`/`Animation` pair to an animation-state-machine handle (the same primitive `spec-tables-animation.md` documents from the anim side). None of these are re-derived here.

### 1.3 The customization subsystem, at a glance

Tracing the loaders surfaced one structural fact not visible from the filenames alone: **the character-customization data is organised as several independent init chains, not one monolithic loader**, each triggered from its own registered content-handler entry point (the same DLC-content-registry pattern `spec-tables-weapons-combat.md` §1 and every other table-schema spec in this session already documents — `0x0118D1F8`, per-type handler table):

| Chain | Entry point(s) | Tables loaded | Section |
|---|---|---|---|
| Clothing/item catalogue | `FUN_00822210` (base) / `FUN_00822540` (DLC) → `FUN_0082abe0` | `customization_items.xtbl`, `customization_outfits.xtbl`, `customization_stores.xtbl` | §4–§5 |
| Item-catalogue support tables | `FUN_0082a470` → `FUN_008285f0` etc. | `customization_categories.xtbl`, `customization_slot_defaults.xtbl` (via `FUN_0082abe0` directly), `customization_flags.xtbl`, `customization_icons.xtbl`, `customization_materials.xtbl` | §6–§9 |
| Slot/normal-map/compositing init | `FUN_009fd3b0`/`FUN_009fd480` | `customization_slots.xtbl`, `customization_normals.xtbl`, `customization_compositing.xtbl` | §6, §9 |
| Character/NPC definitions | `FUN_00bdfa30` (base) / `FUN_00be0030` (DLC) → `FUN_00be1670` | `character_definitions.xtbl` (+ dependency `character_height.xtbl`), `character_customization_categories.xtbl`, `character.xtbl` | §12–§13 |
| Palette/gang init | `FUN_00be4330` | `npc_color_palette.xtbl`, `character_customization_categories.xtbl` (again, "main" framework), `character.xtbl` (again) | §3, §13 |
| Named colour pools | `FUN_00746c80` → `FUN_00746800` (data-driven, §2) + `FUN_007466a0` | `character_color_pool.xtbl`, `hair_color_pool.xtbl`, `makeup_color_pool.xtbl`, `tattoo_color_pool.xtbl`, `char_color_balance.xtbl` | §2 |
| Player face/body creation | `FUN_00836460` (one umbrella init calling the rest in sequence) | `player_creation.xtbl`, `player_creation_morph_groups.xtbl`, `player_creation_normal_maps.xtbl`, `player_creation_hair_color.xtbl`, `player_creation_skin_colors.xtbl`, `player_master_sliders.xtbl`, `player_presets.xtbl`, `player_regional_presets.xtbl` | §14–§16 |
| Standalone registries | independent call sites | `gang_customization.xtbl`, `customizable_action.xtbl`, `customization_default_items.xtbl`, `player_cust_shot_map.xtbl`, `items_color_pool.xtbl` | §8, §10, §11, §17, §3 |

**§1.4 A convention seen throughout this group, called out once:** every "list of known names" (customization slots, category flags, body-type morph groups, race, etc.) is a small fixed C string-pointer array in the data segment, resolved by `FUN_00dac830(array, stride, needle, caseInsensitive)` — a linear scan, not a hash table. These arrays are dumped verbatim where relevant (§6, §9, §14) because they are themselves part of the schema (an authored `Slot` value that doesn't match one of the 24 names in §6.1 is simply skipped by a different code path, not an error).

**Review status (2026-09-30): DESK-PASS (§1.1–§1.4; the hash-primitive correction in §1.2 was already applied) — desk review (not re-derived from the executable).**

## 2. The shared named-color-pool family — `character_color_pool.xtbl`, `hair_color_pool.xtbl`, `makeup_color_pool.xtbl`, `tattoo_color_pool.xtbl`, `char_color_balance.xtbl`

**[CONFIRMED — disassembly + empirical]**

### 2.1 One reader, four files, driven by a data array — not four independent loaders

All four filenames are exact single literals in the executable (`character_color_pool.xtbl` at `0x012f6a78`, `hair_color_pool.xtbl` at `0x012f6a7c`, `makeup_color_pool.xtbl` at `0x012f6a80`, `tattoo_color_pool.xtbl` at `0x012f6a84`) **[Desk review 2026-09-30: these four addresses are 4 bytes apart, so they are the `char*` slots of the pointer array described next, not the string literals themselves (each filename is longer than 4 bytes); the string addresses were not recorded.]** **[OPEN — desk review 2026-09-30: the literal address each slot points to is not given; to be settled against the executable (dereference the four slots).]** — but **they are consecutive elements of one 4-entry `char*[4]` pointer array**, not four separate `FUN_00dac9a0(...)` call sites. `FUN_00746800` walks that array in a loop that runs while its pool-index counter is below 4, opening each file and reading its `Color_Entry` list into the matching per-pool slot of the parallel `DAT_015535b8` array. This is why a direct filename-literal xref search on `hair_color_pool.xtbl`/`character_color_pool.xtbl` finds exactly one code cross-reference each, but it is a **data** reference (a pointer-table slot), not an instruction operand — `makeup_color_pool.xtbl`/`tattoo_color_pool.xtbl` show **zero** code xrefs at all for the same reason (nothing but the pointer array touches their address). `FUN_00746800` is called once from `FUN_00746c80` (no separate per-pool entry points).

### 2.2 `Color_Entry` record — one shared 48-byte (0x30) layout for all four pools

| Field | Source | Notes |
|---|---|---|
| `Name` (hash) | `Color_Entry > Name` text, hashed | the lookup key every other table's colour reference (`Item/Color1..3`, `Variants > Default_Colors_Grid`, etc.) resolves against — the shared helper `FUN_00746d60` (`Name` hash → pool index → `Color_Entry`) |
| `Color` (RGBA float) | `Color_Entry > Color` text, parsed by `FUN_00dad0a0` | base authored linear colour |
| `DisplayName` | `Color_Entry > DisplayName`, or `0.0` if absent | localized string id |
| Two derived ~~RGBA~~ **RGB float** triples **[corrected 2026-09-30, desk review: a triple is 3 floats; only 12-byte RGB triples make the record tile — Name 4 + Color 16 + DisplayName 4 + 2 × 12 = 48 = 0x30 (two RGBA quads would give 56); the 0x30 size itself is still disassembly-only]** | computed, not authored | see §2.3 |

### 2.3 The one real per-pool difference: `char_color_balance.xtbl` applies ONLY to `character_color_pool.xtbl`

A single per-pool flag byte (`DAT_012f6a88` = `{0x01, 0x00, 0x00, 0x00}`, one byte per pool in array order) gates a colour-balance transform read from `char_color_balance.xtbl` (`ColorBalance > Black_Level`/`White_Level`/`Saturation`/`Ped_Black_Level`/`Ped_White_Level`/`Ped_Saturation`, six floats, loaded once by `FUN_007466a0`). **Only pool index 0 (`character_color_pool.xtbl`) has the flag set** — its `Color_Entry` records get two additional derived colour triples baked in at load time (a "player" variant using `Black_Level`/`White_Level`/`Saturation` and a "ped" variant using the `Ped_*` triple, each a linear remap plus a saturation blend against luminance). **[OPEN — desk review 2026-09-30: the remap/blend formula, luminance weights and order of operations are not given, so the derived colours cannot be reproduced from this text; to be settled against the executable.]** `hair_color_pool.xtbl`, `makeup_color_pool.xtbl` and `tattoo_color_pool.xtbl` skip the transform entirely and just replicate the base colour into both derived slots. `char_color_balance.xtbl` itself is a single-row table (`Table` → exactly one `ColorBalance` element, confirmed empirically, §21) — there is no per-entry variation, it's one global tuning knob.

### 2.4 Validation — real base rows

| Table | Real `Color_Entry` rows (`misc_tables.vpp_pc`) |
|---|---|
| `character_color_pool.xtbl` (#71) | **126** |
| `hair_color_pool.xtbl` (#149) | **17** |
| `makeup_color_pool.xtbl` (#174) | **117** |
| `tattoo_color_pool.xtbl` (#251) | **11** |
| `char_color_balance.xtbl` (#77) | **1** `ColorBalance` root (matches the reader treating it as a singleton, not a list) |

No capacity cap was found in `FUN_00746800` (each pool's array is heap-allocated to the exact counted size) — all four are comfortably below any plausible fixed limit regardless.

**Review status (2026-09-30): NEEDS-EXE: 0x30 record layout, colour-balance formula and the four pool string addresses; text fixes applied (RGB triples, pointer-slot wording); element tree and row counts VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 3. `items_color_pool.xtbl` (partial) and `npc_color_palette.xtbl`

### 3.1 `items_color_pool.xtbl` — a fifth, structurally separate colour pool **[CONFIRMED — schema empirical; consumer OPEN]**

`items_color_pool.xtbl` is a genuine single literal (`0x0116e55c`, one xref at `0x008fb6fd`, inside `FUN_008fb6b0`) but is **not** part of the §2 four-slot array (its own element tag is `Item_Color`, not `Color_Entry`, and it sits at a different, unrelated data address). `FUN_008fb6b0` is a broad per-subsystem init function (it also builds several icon/UI lookups via `FUN_00904c10`) whose body could not be narrowed to the exact `FUN_00dac9a0("items_color_pool.xtbl", ...)` call site in this pass — the single xref address falls inside a run of sibling `FUN_00904c10(...)`/`FUN_005c50b0(...)` calls with only one argument each visibly a data pointer near, but not matching, the confirmed string address. **Honest gap: the schema is known from the real data (below), the loader function is narrowed to one candidate, but the exact call and consumer were not pinned to field level.**

Real data (`misc_tables.vpp_pc` #160, 30 `Item_Color` rows): flat records, three children each — `Name`, `Color`, `_Editor`. Structurally this looks like a small, standalone colour catalogue for a specific item category (candidate: vehicle interior/prop paint swatches, given the small count relative to the four §2 pools) — not confirmed.

**Review status (2026-09-30): NEEDS-EXE: loader call and consumer inside `FUN_008fb6b0` (xref `0x008fb6fd`) not pinned; the 30-row `Item_Color` tree is VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 3.2 `npc_color_palette.xtbl` — a three-tier nested RGB palette tree **[CONFIRMED — disassembly + empirical]**

Single literal (`0x01190434`), loader `FUN_00be29c0` (also directly reachable from the DLC/framework init `FUN_00be4330`, which calls it unconditionally before its own `character_customization_categories.xtbl`/`character.xtbl` loads — see §13). This is structurally distinct from every other colour table in this document: instead of a flat name→colour list, each `Palette` row owns a **recursive three-level colour hierarchy**:

```
Palette (Name, hashed)
  └─ Primary_Colors > Primary_Color[]   (R/G/B floats × 255, alpha forced 0xff)
       └─ Secondary_Colors > Secondary_Color[]  (same 4-byte RGBA shape)
            └─ Tertiary_Colors > Tertiary_Color[]  (same 4-byte RGBA shape)
```

Each level is packed to a 4-byte `{R,G,B,0xff}` colour cell (`DAT_012a2d88` = the `255.0` scale constant) with its own nested count/array-pointer pair (12-byte `{name_or_count, count, array_ptr}`-style records at every level, `0xc` stride). **[OPEN — desk review 2026-09-30: the text does not reconcile the 4-byte colour cell with the 12-byte per-level record (which is the element and which the container header), and `name_or_count` is a hedge, not a field; to be settled against the executable.]** This is a colour-variation tree (e.g. a base gang/NPC colour that branches into acceptable secondary and tertiary accent shades), not a flat swatch list — no other table in this session uses this recursive shape. **Validation:** 34 real `Palette` rows (`misc_tables.vpp_pc` #191, 35,806 bytes; **36 in the `patch_compressed.vpp_pc` copy, §21**) — nesting depth and per-level field names read directly off the disassembly above. **[OPEN — desk review 2026-09-30: which archive copy (34-row base or 36-row patch) is loaded at boot is not settled by this document; to be settled against the executable (load order in `FUN_00be29c0` / archive search order).]**

**Review status (2026-09-30): NEEDS-EXE: cell/header layout and archive precedence; element tree and both row counts VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements", with `npc_color_palette` 34 base vs 36 patch reported as one of exactly 2 row-count mismatches) — desk review (not re-derived from the executable).**

## 4. `customization_items.xtbl` — the clothing/accessory item catalogue

**[CONFIRMED — disassembly + empirical]** Single literal (`0x0115f190`, 2 xrefs: `FUN_00822210` base load, `FUN_00822540` DLC load). Reader `FUN_00829650`, called from `FUN_0082abe0` (§1.3, §4–§5's shared init). By far the richest single record in this table group — comparable in depth to `weapons.xtbl` (`spec-tables-weapons-combat.md` §1).

### 4.1 Capacity and top-level record

**Hard cap 858 rows (`0x35a`)** — the load loop stops once `DAT_022db988` exceeds `0x359` (not a base-game overflow; real count is 574/858, §21). **[OPEN — desk review 2026-09-30: the prose test does not say whether the counter is checked before or after a row is stored, so whether the loop keeps 858 or 859 rows is not pinned (the compare instruction was not quoted); to be settled against the executable.]** Each accepted row occupies a `0x7c`-byte (124-byte) slot in a fixed global array `DAT_022db9c8`.

| Field | Source | Notes |
|---|---|---|
| Flags | `Flags > Flag` text membership (`FUN_00dac7d0`) | 3 bits recovered: **`not ready`** (bit 0 — row is entirely skipped, `goto` back to the next `Customization_Item` without allocating a slot), **`big hat`** (bit 1), **`variants are same item`** (bit 2) |
| `Slot` | text, hashed via `FUN_00dc4ff0(...,&DAT_0115f3b0)` then resolved through `FUN_009fd510` (§6.1's 24-name slot table lookup — index `-1` skips the row unless `Flags` includes `npc only`, in which case it's accepted with no slot binding) | |
| `Base_Price` / `Base_Respect_Bonus` | int fields | fallback defaults used by `Variants` below when a variant omits its own `Price`/`Respect_Bonus` |
| `Brand` | text, matched against a **6-entry fixed brand array** (`Astro Gaming` confirmed as one entry; the rest were not individually dumped) | `-1` if unmatched or absent |
| `Name` | hashed | the item's own lookup key; also compared against the literal string `"basicparachute"` as a one-off special case tying it to the parachute slot group |
| DLC-ness | `Is_DLC` yes/no | `0xff` (base) / `0` (DLC) sentinel byte |
| `DisplayName` | localized text id | falls back to a default (`DAT_029c9964`) if the string has no localization entry |
| `Wear_Options > Wear_Option[]` | array | §4.2 |
| `Variants > Variant[]` | array, `0x70`(112)-byte records | §4.3 |
| `Default_Colors_Grid > Default_Color[]` | up to the item's own `+0x58` colour-slot count | §4.4 |
| `Heel_Angle` / `Heel_Height` | floats, default 0 | shoe-specific |
| `ShoeAudioSwitch` / `ClothingAudioSwitch` | text → `FUN_00462960` (audio switch resolver) | **both write the same `+0x6c` field** — whichever element is present last wins; if both are present, `ClothingAudioSwitch` overwrites `ShoeAudioSwitch` (real code behaviour, not corrected here) |
| `Fat_Bone_Arm` / `Fat_Bone_Leg` | floats, default 0 | fatness-morph scale per limb |
| `Style` | text enum: `Cool`→bit `0x10`, `Costume`→bit `0x8`, `NyteBlayde`→bit `0x20` (OR'd into the row's own flags dword) | |

**[OPEN — desk review 2026-09-30: no offset table is given for the `0x7c` record (only `+0x58`, `+0x6c`, `+0x78`), so the struct cannot be tiled from this text; the other 5 `Brand` array entries are not dumped (§22 item 8); to be settled against the executable (field stores in `FUN_00829650`).]**

**Review status (2026-09-30): NEEDS-EXE: 0x7c record layout, brand array, cap compare; the 574-row `Customization_Item` tree is VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 4.2 `Wear_Option` — the mesh/flag record (0x28 = 40 bytes each)

A `Wear_Option` with `Disabled=yes` is dropped entirely (the array's element count and cursor are both decremented, i.e. disabled options simply don't exist in the parsed output). For accepted options: `Name` (hashed), `Mesh_Information > Male_Mesh_Filename > Filename` (the `.cmeshx` name, hashed via the engine string-hash `FUN_00da7930` **[OPEN — desk review 2026-09-30: conflicts with other specs — `spec-morph-format.md` (registry table, "Name copy" column) and `spec-lua-api-behaviour.md` (0x00da7930, "bounded-copy helper") identify `FUN_00da7930` as a bounded string copy, and `spec-customization-data.md` §5.1 says the mesh-name half is the CRC-32 `FUN_00D9E8B0`, computed inside the composer; to be settled against the executable (the mesh-filename step of `FUN_00829650` and of the composer `FUN_009FD8C0`).]** — this is the exact mesh-string half of the `custmesh_<N>.str2_pc` bundle-name recipe `spec-customization-data.md` §5 already resolved), `Active_Flags > Active_Flag[]`/`Required_Flags > Required_Flag[]`/`Incompatible_Flags > Incompatible_Flag[]` — **all three resolve their flag names against the SAME 39-entry `customization_flags.xtbl` registry** (§7.2's `DAT_022db970` array), each also carrying its own `Comparison` (`yes`/`no`) sub-flag packed into the flag-index byte's top bit. **[OPEN — desk review 2026-09-30: this section names only `Male_Mesh_Filename`; `spec-customization-data.md` §2.1/§5.2 also documents `Female_Mesh_Filename > Filename` (it drives the `custmesh_<N>f` bundle) — conflict with another spec; and no offsets are given for the `0x28` record; to be settled against the executable and real `Customization_Item` rows.]**

**Review status (2026-09-30): NEEDS-EXE: identity of `FUN_00da7930`, `Female_Mesh_Filename` handling, 0x28 record layout — desk review (not re-derived from the executable).**

### 4.3 `Variant` — the purchasable colour/material variant (0x70 = 112 bytes each)

`Name` (localized + hashed), `Price`/`Respect_Bonus` (default to the item's own `Base_Price`/`Base_Respect_Bonus` when the authored value equals a sentinel), `Mesh_Variant_Info > Variant_Name` (hashed) + `VariantID` (int), `Material_List > Material_Element[]` (each: `Material` name hashed against `customization_materials.xtbl`, §9.1; `Shader_Type`, plus a hardcoded `__stricmp(name,"cm_suit_deckersuit")` special case feeding `FUN_00828de0`). **[OPEN — desk review 2026-09-30: the sentinel value that triggers the `Base_Price`/`Base_Respect_Bonus` fallback is not given, the `0x70` record is not tiled, and whether `Shader_Type` is a child of `Material_Element` or of `Variant` is unsettled (`spec-customization-data.md` §2.1 lists it at variant level; Team B reads it under `Material_Element`); to be settled against the executable (`FUN_00829650`, `FUN_00828de0`) and real rows.]**

**Review status (2026-09-30): NEEDS-EXE: sentinel constant, record layout, `Shader_Type` parent — desk review (not re-derived from the executable).**

### 4.4 `Default_Colors_Grid` — per-item preset colour slots

Up to the item's own accumulated slot count (`+0x58`), each `Default_Color` element is one of `Clothing_Color` / `Tattoo_Color` (both resolved through the shared §2 colour-pool helper `FUN_00746d60`, pool index 0 = character, 3 = tattoo) / `Makeup_Color` (pool index 2). If the item's own `Variants[0]`'s `Material_List` supplies a colour-slot count (`+0x2c`/`+0x1c`), the final slot count is clamped to the smaller of the two and any shortfall is back-filled from a fixed name list (at `0x01300c48`, first entry `Crimson`) rather than left unset. **[OPEN — desk review 2026-09-30: "clamped to the smaller" leaves no shortfall to back-fill, and the analogous §8 override says "larger"; which bound applies, and to which operand, is not stated; to be settled against the executable (tail of `FUN_00829650`).]**

**Review status (2026-09-30): NEEDS-EXE: clamp rule (min vs max) and back-fill condition — desk review (not re-derived from the executable).**

## 5. `customization_outfits.xtbl` and `customization_stores.xtbl`

Both share `customization_items.xtbl`'s init chain (`FUN_0082abe0`, §1.3) and both reference the item array built in §4 by array index, not by re-opening the file.

### 5.1 `customization_outfits.xtbl` — preset outfit bundles **[CONFIRMED — disassembly + empirical]**

Single literal (`0x0115f1ac`, 2 xrefs matching the same base/DLC split as §4). Reader `FUN_008290c0`. Each `Outfit`: `Name` (hashed + localized `Display_Name`), `Is_DLC`, a CRC field (`&DAT_0115f594`), and two child grids —

- `Content_Grid > Content_Element[]`: `Item` (name → §4's item array, by the shared name-hash lookup `FUN_00db0c50`), `Default_Wear_Option` (resolved against that specific item's own `Wear_Option` list), `Primary_Color`/`Secondary_Color`/`Tertiary_Color` (all three or none — resolved through the §2 colour-pool helper `FUN_00746d60`, pool 0).
- `Styles_Grid > Style_Element[]`, each with a `Variant_Grid > Variant_Element[]` whose length **must equal** `Content_Grid`'s element count (checked for exact numeric equality against a count field on the outfit's own record — a style with a mismatched variant count is silently skipped) — each `Variant_Element`'s `Variant` text is matched by name against that content item's own `Variant` array (§4.3), giving one variant selection per content slot per named style (e.g. "Sunday Best" picks specific colour/material variants for every garment in the outfit at once).

**Validation:** 70 real `Outfit` rows (`misc_tables.vpp_pc` #102). **[OPEN — desk review 2026-09-30: the CRC input (the string `&DAT_0115f594` points to) and the exact count-equality check are not specified; to be settled against the executable.]**

**Review status (2026-09-30): VALIDATED-BY-DATA: Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`" cross-check "70/70 match, covered by sr3customization" (element tree and row count); CRC and equality-check semantics NEEDS-EXE — desk review (not re-derived from the executable).**

### 5.2 `customization_stores.xtbl` — in-game clothing store inventories **[CONFIRMED — disassembly + empirical]**

Single literal (`0x0115f880`, one xref at `0082ac89`, inside `FUN_0082abe0` itself — the filename is loaded through a local/stack variable, not visible as a direct call argument in the decompile, but the xref count confirms it belongs to this same init chain, called unconditionally right after the outfits load). Reader `FUN_0082a6f0`. Each `Store` row is a large **`0x133c`-byte (4,924-byte)** fixed record:

- `Name` (hashed), `Flags > allow_wardrobe` (single bit, text `"allow_wardrobe"`)
- `Store_Item[]` — **capped at 255 (`0xfe`) items per store** **[OPEN — desk review 2026-09-30: the decimal and hex disagree (0xfe = 254) and no compare operator is given, so 254 vs 255 is not settled; to be settled against the executable.]**, each: item lookup by name (§4's array), a viability check (`FUN_0045b3a0` on the item's own `+0x78` byte — likely the item's own dead/DLC-availability flag), then `Variants > Item_Variant[]` (if present, matched by name against the item's own `Variant` array; if absent, **every** variant of the item is listed by default) — each resolved variant gets a back-reference (`+0x28`/`+0x68`-style linked slot) pointing at this store, i.e. the relationship is bidirectional (item variants know which stores sell them).
- `Outfits > Outfit_Element[]` — **capped at 73 (`0x48`+1) outfits per store** **[OPEN — desk review 2026-09-30: no compare operator is given for this cap; to be settled against the executable.]**, each an `Outfit` name resolved against §5.1's outfit array, filtered the same way through the outfit's own DLC-availability byte.

**Validation:** 15 real `Store` rows (`misc_tables.vpp_pc` #105) — well under the per-item/per-outfit caps (never observed near either limit in real data). **[OPEN — desk review 2026-09-30: the `0x133c` record cannot be tiled because no sub-record strides are given, and the `+0x78` viability check is only "likely" a DLC-availability flag; to be settled against the executable (`FUN_0082a6f0`, `FUN_0045b3a0`).]**

**Review status (2026-09-30): NEEDS-EXE: entry strides, both cap compares, viability flag; 15-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 6. `customization_slots.xtbl` and `customization_slot_defaults.xtbl`

### 6.1 `customization_slots.xtbl` — the memory-budget/flag registry behind the 24 canonical slot names **[CONFIRMED — disassembly + empirical]**

Single literal (`0x011774d0`), reader `FUN_009fca30`. This table does **not** define the slot names themselves — those are a fixed 24-entry array in the executable's data segment (`PTR_DAT_0130b780`, dumped in full below); this table supplies per-slot **budgets and flags** for the names that already exist in that array:

**The 24 canonical slot names** (`0x0130b780`, index = the id used everywhere else in this group as a `Slot` reference): `body, head hair, beard, eyebrows, mouth, headwear, facewear, eyewear, ear piercings, long chain, bra, underwear, upper body, lower body, shoes, left wrist, right wrist, gloves, face piercings, suit, hat hair combined, privacy bar top, privacy bar bottom, parachute`.

A `Slot` element whose `Name` does **not** match one of the 24 (index `-1`) is not an error — the code instead checks that slot's `Flags` for `npc only` and, if absent, simply continues (no budget/flag row is written, but the row isn't rejected outright either — see the open item, §22). Matched slots get:

| Field | Notes |
|---|---|
| `CPU_Size` / `GPU_Size` | authored value **inflated by `value + value/8`** (a fixed 12.5% headroom margin) before being stored |
| `Flags > Flag` membership | 3 bits: `required` (0x1), `npc only` (0x2), `remove on outfit` (0x4) |
| `Render_Order` | int, stored per-slot |

**Validation:** 22 real `Slot` rows (`misc_tables.vpp_pc` #103) against the 24-name array — **`beard` and `eyebrows` have no matching `Slot` row in the shipped base game** (confirmed by direct name comparison, not by count alone). ~~Both are plausible corroborations of well-known SR3 content: beard/eyebrow customization is not present in the retail character creator despite name slots for them surviving in the engine's fixed slot-id array.~~ **[Struck 2026-09-30, desk review: narrative not supported by any evidence in this document; the data fact above stands.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (24-name count re-verified = 24; 22-row tree VALIDATED-BY-DATA, Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 6.2 `customization_slot_defaults.xtbl` — **shipped `<Table>` is EMPTY** **[CONFIRMED — disassembly + empirical, this is exactly the TableTemplates trap the task brief warns about]**

Single literal (`0x0115f4d0`), reader `FUN_00828f00` (called unconditionally from `FUN_0082abe0`, §1.3, right after `customization_items.xtbl`). Structurally this table maps a `Slot` name (§6.1's 24-entry array, via `FUN_009fd510`) + an `Item` name (§4's item array) + a `Variant` name (that item's own `Variant` array) to a default-selection record — the mechanism that would decide what a freshly-created character starts wearing in each slot.

**Real base data check (this document's own application of the task brief's central lesson): `misc_tables.vpp_pc` #104's `<Table>` node has ZERO direct children.** All apparent content — `Slot_Default` rows for `undershirt`/etc. with real-looking `Name`/`Slot`/`Item`/`Mesh`/`Variant` fields — lives under the sibling `<TableTemplates>` node (confirmed by direct string search: `<Table>` and `</Table>` are adjacent with only whitespace between them; `<TableTemplates>` immediately follows with the actual example rows). **The mechanism is real and the loader runs, but at retail it initialises a zero-length array — the base game ships with no default-selection data through this specific table.** (Character-creator default outfits are presumably driven by `player_presets.xtbl` instead, §16.2.)

**Review status (2026-09-30): VALIDATED-BY-DATA: empty `<Table>` matches Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements" — desk review (not re-derived from the executable).**

## 7. `customization_categories.xtbl`, `customization_flags.xtbl`, `customization_icons.xtbl` — menu-organisation registries

**[CONFIRMED — disassembly + empirical, all three]**

### 7.1 `customization_categories.xtbl`

Single literal (`0x0115f3d4`, plus one unrelated suffix-only substring hit inside an unrelated string at `0x0119013a`, zero refs — not a second loader). Reader `FUN_008285f0`, called from `FUN_0082a470` (a sibling init alongside the main `FUN_0082abe0` chain, §1.3). Each `Category` (`0x44`=68-byte record): `Name` (hashed via a category-specific helper `FUN_00a74910`) + `DisplayName`; `Slots_Grid > Slot_Element[]` (each resolved against §6.1's 24-slot table); `Obscure_Grid > Obscure_Element[]` (a `Slot` + a `Flag` name, the flag resolved against the SAME 39-entry `customization_flags.xtbl` registry §4.2's `Wear_Option` flags use — one shared flag vocabulary across both `Customization_Item` rows and `Category` rows); `Male_Animations`/`Female_Animations` (each a `State`+`Animation` pair resolved via `FUN_004bf810` to an anim-state handle — per-category, per-gender try-on animations); `No_Wear_String` (localized text, shown when nothing is equipped in this category).

**Validation:** 39 real `Category` rows (`misc_tables.vpp_pc` #94).

**Review status (2026-09-30): VALIDATED-BY-DATA: element tree and 39 rows (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements"); the `0x44` record has no field offsets and stays NEEDS-EXE (`FUN_008285f0`) — desk review (not re-derived from the executable).**

### 7.2 `customization_flags.xtbl` — the shared flag-name registry

Single literal (`0x0115f340`), reader `FUN_00828450`. A flat list of `Flag` elements, each just `Name` (hashed into `DAT_022db970`, an 8-byte-stride array — this is the array every `Active_Flag`/`Required_Flag`/`Incompatible_Flag`/`Obscure_Element` flag reference in §4.2 and §7.1 resolves against by linear scan). Four names are recognised as **special sentinels** with dedicated global slots outside the generic array: `BLING_COLLIDER_ACTIVE` (part of a 4-entry parallel checklist, exact other 3 names not individually dumped), `HAT ACTIVE`, `PIMP COAT`, `flasher coat` — each sets one of three dedicated globals (`DAT_01300ba8`/`DAT_01300bac`/`DAT_01300bb0`) used elsewhere as fast-path flag-index caches rather than looked up generically each time. **[OPEN — desk review 2026-09-30: four special names are listed against three dedicated globals; which name sets which global, and what the 3 unnamed companions of `BLING_COLLIDER_ACTIVE` are, is not stated; to be settled against the executable (`FUN_00828450`, uses of `DAT_01300ba8`/`bac`/`bb0`).]**
**Validation:** 39 real `Flag` rows (`misc_tables.vpp_pc` #97) — matches the `DAT_022db974` count used throughout §4/§7.1 exactly.

**Review status (2026-09-30): NEEDS-EXE: special-name→global mapping; 39-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 7.3 `customization_icons.xtbl`

Single literal (`0x0115f42c`), reader `FUN_008289e0`. Each `Icon` (`0x2c`=44-byte record): `Name` (hashed via `FUN_00a74910`, same helper as §7.1's categories) + `DisplayName`; `Categories > Category_Element[]` (each `Category` name resolved via `FUN_00828340` — presumably against §7.1's category array, not independently confirmed); `Outfits_Only` (yes/no bool).

**Validation:** 10 real `Icon` rows (`misc_tables.vpp_pc` #98).

**Review status (2026-09-30): NEEDS-EXE: `FUN_00828340`'s lookup target (§22 item 5); 10-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 8. `customization_default_items.xtbl`

**[CONFIRMED — disassembly + empirical]** Single literal (`0x0115f148`), reader `FUN_00820f40`, called from `FUN_00822210` (§1.3's base-load path only — no DLC-content-handler counterpart was found for this table). Root shape: `Table > Defaults > Defaults_List > Default[]` (the table-direct-children count is `1` — the single `Defaults` wrapper — the real row array is two levels further in; **58 bytes per row, cap not explicit** **[OPEN — desk review 2026-09-30: "58 bytes per row" contradicts the `0x50` (80-byte) record size in the next paragraph; neither 58 nor 0x58 is reconciled; to be settled against the executable (`FUN_00820f40`).]** (heap-sized exactly to the counted `Default` element count).

Each `Default` (`0x50`=80-byte record): `Item` (name → §4's item array, via `FUN_008282c0`), `Wear_Option` (name → that item's own `Wear_Option` array), `Variant` (name → that item's own `Variant` array), a combined lookup id from both (`FUN_009fd5b0`); `Gender` (`male`/`female`/absent→`3`, i.e. an explicit "either" sentinel distinct from both real genders); `Race` (`asian`/`black`/`hispanic`/`white`, using the field name `Race` — **note this is a 4-name enum specific to this table**, distinct from the `Asian`/`Black`/`Hispanic`/`Caucasian` 4-name array `customization_normals.xtbl` uses, §9.2 — same concept, different authored vocabulary and different underlying array); a colour-slot carry-over from the item's own `Default_Colors_Grid` (§4.4) clamped to the item's authored slot count; then an **optional override** — if the `Default` itself has a `Color_Pool > Character_Color_Grid` (or, failing that, `Makeup_Color_Grid`) child, its `Color_Element > Color` entries (resolved through the shared §2 helper `FUN_00746d60`) **replace** the item's own default colours for this specific default-selection row, again clamped to the larger of the two counts. **[OPEN — desk review 2026-09-30: "larger" here vs "smaller" in §4.4 for the analogous clamp; which bound applies is not settled; to be settled against the executable (`FUN_00820f40`).]**
**Validation:** 55 real `Default` rows (`misc_tables.vpp_pc` #96, confirmed at the `Defaults > Defaults_List` nesting level — the table-direct-child count of `1` is a real, correctly-parsed nesting artifact, not an empty table).

**Review status (2026-09-30): NEEDS-EXE: record size (58 vs 0x50) and override clamp; 55-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 9. `customization_materials.xtbl`, `customization_normals.xtbl`, `customization_compositing.xtbl` — the texture-compositing trio

**[CONFIRMED — disassembly + empirical, all three]** `customization_normals.xtbl` and `customization_compositing.xtbl` share one reader function, called for two different purposes (base-game load and a DLC/runtime-rebuild variant, selected by a flag in the reader's second argument).

### 9.1 `customization_materials.xtbl`

Single literal (`0x0115f4a0`), reader `FUN_00828be0`, called from `FUN_0082a470` (§7.1's sibling init). Each `Cust_Material` (`0xc`=12-byte record): `Name` (hashed — with one hardcoded sentinel: the row named `SR2Skin1` gets its own dedicated global pointer, `_DAT_022db9b0`, a Saints Row 2 legacy-naming callback matching the `SR2_Player_Gang_Cust`/`SR2Skin1` pattern seen again in §10); `Variables > Variable[]` (each: `Shader_Var_Name` (hashed) + `Display_Name_Game` (localized)) — this is the registry §4.3's `Variant > Material_List > Material_Element > Material` names resolve against.

**Validation:** 7 real `Cust_Material` rows (`misc_tables.vpp_pc` #100; **8 in the `patch_compressed.vpp_pc` copy, §21**). **[OPEN — desk review 2026-09-30: which archive copy (7-row base or 8-row patch) is loaded at boot is not settled; to be settled against the executable (archive search order).]**

**Review status (2026-09-30): NEEDS-EXE: archive precedence, `0xc` record layout; both row counts VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements", `customization_materials` 7 vs 8 reported as one of exactly 2 row-count mismatches) — desk review (not re-derived from the executable).**

### 9.2 `customization_normals.xtbl` — per-race/gender body normal-map sets, AT its 8-slot capacity

Single literal (`0x0117755c`), reader `FUN_009fcc00` (base-load branch, second argument zero). **Hard cap 8 rows** (the load loop stops once its row counter exceeds 7) **[OPEN — desk review 2026-09-30: whether the counter is tested before or after a row is stored is not pinned; this table ships exactly at the stated cap, so the compare instruction decides whether all 8 rows load; to be settled against the executable.]** — the real data ships **exactly 8** (§21), i.e. this table is at capacity in the shipped base game (not overflowing it, unlike the `qte_sequences.xtbl` case elsewhere in this session — worth flagging as the same shape of risk, confirmed safe here). Each `Normals` row: `Race` text matched against a **4-entry array `Asian`/`Black`/`Hispanic`/`Caucasian`** (`0x0130b7e0` — default index `3` if unmatched), `Female` (bool), and five body-composite normal-map bindings (`Ideal`/`Muscular`/`Skinny`/`Fat`/`Age`, each resolved via `FUN_00db1160` and then baked into a runtime texture-load request tagged `"pcust"`).

**Validation:** 8 real `Normals` rows (`misc_tables.vpp_pc` #101) — exactly at the 8-row cap.

**Review status (2026-09-30): NEEDS-EXE: cap compare in `FUN_009fcc00`; 8-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 9.3 `customization_compositing.xtbl` — texture layer-compositing recipe (largest of the trio)

Own single literal (`0x01177514`), read by the same reader function `FUN_009fcc00` (unconditional second half — runs regardless of the second argument, immediately after the `customization_normals.xtbl` block above). **Hard cap 600 rows** (the load loop stops once `DAT_0268d13c` exceeds 599). **[OPEN — desk review 2026-09-30: as in §9.2, whether the test runs before or after a row is stored is not pinned; to be settled against the executable.]** Each `Composite_Layer` (`0x44`=68-byte record): `Name` (hashed, `FUN_00db12c0`), `Display_Name` (optional), `diffuse` texture (required), `normal` texture (optional), a `Slot` text field matched against a **separate 34-entry name array** (`0x0130b7f0`: `lips, eyes, tattoo upper/lower/entire arm left/right ×6, tattoo upper/lower/entire chest ×3, tattoo upper/lower/entire back ×3, tattoo head, tattoo neck, tattoo leg left/right, tattoo hand left/right, body mask, shaderball, fingernails, fingernails_alt, body diffuse, makeup eyeliner/eyeshadow/cheeks/entire face, eyebrows, facial hair, body hair, face features, face items` — 34 entries, **distinct from both the §6.1 24-slot array and the §9.2 4-race array**), `x_coord`/`y_coord`/`Alpha`/`Layer` (int/float compositing-sheet placement), `Flags` resolved against a **4-entry array** `Colorizable`/`Player_creation`/`Show_alpha_selection`/`No_diffuse` (`0x0130b87c` — note the stored bit order is remapped: authored bit 0→stored bit 1, bit 1→bit 0, bits 2/3 pass through unchanged), `Price`, `Is_DLC`.

**Validation:** 374 real `Composite_Layer` rows (`misc_tables.vpp_pc` #95, 201,628 bytes — the largest file in this document after `customization_items.xtbl`), well under the 600-row cap. **[OPEN — desk review 2026-09-30: the `0x44` record is not tiled: the 12 listed fields account for at most 48 of 68 bytes; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: record layout and cap compare; 34-name array count re-verified = 34; 374-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 10. `gang_customization.xtbl`

**[CONFIRMED — disassembly + empirical]** Single literal (`0x01160c94`), reader `FUN_00839290`. Root: a single `GangCustomization` element (matches the empirical single-child `Table`, §21) with a hardcoded name sentinel check against `"SR2_Player_Gang_Cust"` (a **Saints Row 2 legacy row name** carried forward as a magic constant — the same convention as §9.1's `SR2Skin1` material) — the branch taken if the row's `Name` does *not* match this sentinel simply walks to the next `GangCustomization` sibling without reading anything, so in practice only one specific row is ever consumed.

Once the sentinel row is found: `gang_vehicles > gang_vehicles[]` (yes, the wrapper and the row element share the identical tag name) — each `0x14`=20-byte record: a gang-vehicle-group `Name` (hashed), a `locked` flag read from `Flags > Flag` text membership (sets **two** bytes identically, `+8` and `+9`), and `Vehicles > Vehicle[]` (each name resolved against the global vehicle-info table, `FUN_00ac2400` — the exact lookup `spec-vehicle-data.md` §7.1 documents). `PlayerDefaults > DefaultVehicles > Slot1`/`Slot2`/`Slot3` (three named vehicle slots, each resolved the same way then further reduced to a vehicle-group id via `FUN_00ac1ae0`). `Gang_Signs > Pose[]` (each: `display_name` (localized) + `animation` (`State`/`Animation` pair via `FUN_004bf810`, the same anim-state resolver §7.1 uses) — the player's gang-sign gesture list).

**Validation:** 1 real `GangCustomization` row (`misc_tables.vpp_pc` #141, 17,172 bytes — all the real content lives inside that one row's nested grids, matched exactly by the disassembly's single-sentinel-row design). **[OPEN — desk review 2026-09-30: this document does not state that the shipped row's `Name` is `SR2_Player_Gang_Cust` (if it were not, the loader would read nothing), and the `0x14` record is not tiled; to be settled against real data (the row's `Name`) and the executable (`FUN_00839290`).]** Cross-references `spec-tables-progression.md` §11's `unlockables → Gang_Vehicle_Customization/Vehicle_Group` reference into this table's `gang_vehicles.gang_vehicles.name` field, confirming both sides independently.

**Review status (2026-09-30): NEEDS-EXE: `0x14` record layout (plus a data check of the row `Name`); 1-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 11. `customizable_action.xtbl`

**[CONFIRMED — disassembly + empirical]** Single literal (`0x01160198`), reader `FUN_00831270`, called from the same umbrella init as the player-creation subsystem (`FUN_00836460`, §14-§16) but structurally independent of it — this table drives the taunt/compliment gesture wheel, not face/body creation.

Rows are split at load time into **two separate fixed arrays** by their own `ActionType` text (`"Insult"` vs `"Compliment"`, matched via `FUN_00dac830` against a 2-entry array) — `DAT_023008a4`/`DAT_0230089c` (insults) and `DAT_023008a8`/`DAT_023008a0` (compliments), each entry `0x10`=16 bytes: `Action` (`State`/`Animation` pair via `FUN_004bf810`), `Situation` (optional, via `FUN_0070a2f0`), `Team` (optional enum, via `FUN_0094cc60` — same field name/helper family as `spec-tables-weapons-combat.md`'s team enums), `LocalizedTag` — with **one hardcoded localization override**: if the current text language is one of three specific ids (9, 0, 10) and the tag is exactly `"CUST_COMPLIMENT_K"`, the display text is force-set to the literal wide string `"TOUCH DOWN"` regardless of what the string table actually contains for that language — a genuine, confirmed shipped-code special case, not a data-driven value. **[OPEN — desk review 2026-09-30: the language-id → language mapping for 9, 0 and 10 is not given, and which global of each pair is the array and which the count is not stated; to be settled against the executable (`FUN_00831270`).]**
**Validation:** 56 real `CustomizableActions` rows (`misc_tables.vpp_pc` #93) — split unknown-fraction between the two action types at parse time (not separately tallied from the raw XML in this pass, since the split key is the element's own child, not a distinguishing tag name). **[OPEN — desk review 2026-09-30: the Insult/Compliment split is a direct count of `ActionType` values; to be settled against real data.]**

**Review status (2026-09-30): NEEDS-EXE: language ids, array/count roles; 56-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 12. `character_definitions.xtbl` and `character_height.xtbl` — the NPC/character definition subsystem

**[CONFIRMED — disassembly for the loader chain and the dependency table; per-`Character` field reader located but not decompiled to byte-offset level — an honest partial]** **[Label scope, desk review 2026-09-30: CONFIRMED covers the filename literals, loader entry points and row tags/counts only, not the field layout.]**

### 12.1 Load chain

`character_definitions.xtbl` is a single literal (`0x011900c8`, 3 xrefs across `FUN_00bdfa30` [base init] and `FUN_00be0030` [DLC content-handler]). Both paths funnel into the same wrapper, `FUN_00be1670`, which is itself gated behind a small unrelated dependency load (`"preload_anim.tbl"`, a non-`.xtbl` preload manifest, out of scope here) and then opens `character_definitions.xtbl` itself, iterates its `Character` elements filtered by `Framework` (matching `"main"` or the current DLC framework string, the same convention as every other framework-scoped table in this session), and for each matching row calls **`FUN_00be0630`** — the real per-`Character` field reader. `FUN_00be0630` was **located but not decompiled this pass**; its record size is known indirectly from the caller `FUN_00bdf770`, which pre-sizes the destination array to `(realCharacterCount + 0x18) * 0xec` bytes, i.e. **each `Character` record is 236 bytes (`0xec`)**, with 24 extra slack slots reserved beyond the real count (possibly for runtime-spawned NPCs with no authored row). **[OPEN — desk review 2026-09-30: `0xec` comes from an allocation in a different function than the reader; the stride the reader actually writes was not checked; to be settled against the executable (`FUN_00be0630`, `FUN_00bdf770`).]**

### 12.2 `character_height.xtbl` — a hard dependency, loaded first every time

Single literal (`0x01190098`), reader `FUN_00bdf770` opens this file **before** `character_definitions.xtbl` itself, on every call (base or DLC). Flat list of `Height_Class` elements: `Height` (float) + `Name` (hashed) — a small named enum of body-height presets that `character_definitions.xtbl` rows presumably reference by name (the cross-reference itself lives inside the undecompiled `FUN_00be0630`, so this is inferred from load order and naming, not directly confirmed field-by-field).

### 12.3 `char_cust_cats.xtbl` — a named-but-never-shipped alternate filename

`FUN_00be0030`'s DLC path picks between two filenames for the customization-categories load, gated by bit 2 of the content record's own flags byte at offset `+0x103`: if the bit is set it opens `character_customization_categories.xtbl` (§13.1); **if clear, it opens a filename `"char_cust_cats.xtbl"` that does not exist anywhere in any archive checked** (`misc_tables.vpp_pc`, `da_tables.vpp_pc`, `patch_compressed.vpp_pc` — confirmed by direct entry-name scan, not just absence from the xref search). This branch is therefore either dead in every shipped DLC framework (the bit is always set in practice **[desk review 2026-09-30: unsupported — the `+0x103` flags byte was not sampled across content records]**) or a genuinely unreachable legacy code path — no shipped content exercises it. Not counted among this document's 12 "dead file" entries (§18) because it isn't a real archive file at all, just an unreachable string literal.

**Validation:** 282 real `Character` rows (`misc_tables.vpp_pc` #73, 299,123 bytes); 3 real `Height_Class` rows (`misc_tables.vpp_pc` #74).

**Review status (2026-09-30): NEEDS-EXE: `FUN_00be0630` field reader, 0xec stride, bit 2 of `+0x103`; 282/3-row trees VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 13. `character_customization_categories.xtbl`, `character_types.xtbl`, `character.xtbl`

### 13.1 `character_customization_categories.xtbl` **[CONFIRMED — disassembly + empirical]** **[Label scope, desk review 2026-09-30: CONFIRMED covers the filename literal, loader entry and row tag/count only; the reader was not decompiled, see below.]**

Single literal (`0x01190130`, 2 xrefs — `FUN_00be0030`'s DLC path §12.3, and `FUN_00be4330`, the "main"/base-framework palette-and-category init that also loads `npc_color_palette.xtbl`, §3.2). Reader **not separately decompiled** (both callers route through the same generic `FUN_00be4120`/`thunk_FUN_00be4120` element-tree stash function used across this subsystem); the real per-row field layout was not recovered this pass. Element tag is lowercase `category` (confirmed against real data, distinct casing from `customization_categories.xtbl`'s `Category`, §7.1 — the accessor grammar is case-insensitive so this has no functional effect, noted only to avoid confusing the two tables).

**Validation:** 16 real `category` rows (`misc_tables.vpp_pc` #72).

**Review status (2026-09-30): NEEDS-EXE: reader `FUN_00be4120` not decompiled; 16-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 13.2 `character_types.xtbl` **[CONFIRMED — disassembly + empirical]**

Single literal (`0x011903b8`), reader `FUN_00be1d80`. **Hard cap 120 rows** (the load loop's row counter, read as a 16-bit value, is compared against `0x78`). **[OPEN — desk review 2026-09-30: the compare operator is not given, so whether 119 or 120 rows are kept is not settled; what the loop-index "salt" argument does inside `FUN_00db1510` is also unspecified; to be settled against the executable (`FUN_00be1d80`, `FUN_00db1510`).]** A flat name-only registry (matches `spec-tables-animation.md`'s `anim_states.xtbl`/`anim_actions.xtbl` pattern exactly): each `Type` element has just a `Name`, hashed via `FUN_00db1510` with the loop index as a salt/context argument, stored into a flat array (`DAT_02998ea8`). No other field is read.

**Validation:** 96 real `Type` rows (`misc_tables.vpp_pc` #75), well under the 120-row cap.

**Review status (2026-09-30): NEEDS-EXE: cap compare and hash salt; 96-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 13.3 `character.xtbl` — loaded twice, not decompiled to field level **[CONFIRMED — real, single-literal, consumed twice; schema OPEN]** **[Label scope, desk review 2026-09-30: CONFIRMED covers the filename literal, both call sites and row tag/count only.]**

Single literal (`0x01190120`, 2 xrefs — `FUN_00be0030`'s DLC path (its very last call, after `character_definitions.xtbl` and `character_customization_categories.xtbl`) and `FUN_00be4330`'s base/"main" path (via `FUN_00be4010`, likewise its last call, after `npc_color_palette.xtbl` and `character_customization_categories.xtbl`)). Both consumers are thin wrappers whose inner field-reading function was not identified this pass — **the element name is `Character`, the same tag `character_definitions.xtbl` uses (§12.1), but this is confirmed to be a structurally separate file with its own loader entry point and its own, larger, real row count** (382 vs. 282 — see §21) — not a duplicate load of the same data. **[Desk review 2026-09-30: "separate" rests on the distinct row counts and call sites only; the two `Character` schemas were not compared.]**

**Validation:** 382 real `Character` rows (`misc_tables.vpp_pc` #70, 334,615 bytes — the largest file in this section after `customization_items.xtbl` and `customization_compositing.xtbl`). **Open item:** what distinguishes a `character.xtbl` `Character` row from a `character_definitions.xtbl` `Character` row (e.g. NPC archetype vs. player-usable, or a completely different field set under the same tag name) was not resolved — flagged in §22 rather than guessed.

**Review status (2026-09-30): NEEDS-EXE: inner readers behind `FUN_00be4010`/`FUN_00be4120` (plus a data comparison of the two `Character` element sets); 382-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 14. `player_creation.xtbl` and `player_creation_morph_groups.xtbl` — the face/body sculpting root

**[CONFIRMED — disassembly + empirical, both]**

### 14.1 `player_creation.xtbl` — the 14-category face/body `Morph_Set` root

Single literal (`0x01176dac`), reader `FUN_009f6870` — a thin dispatcher: it iterates the file's `Morph_set` elements (real data uses the casing `Morph_Set`; matched case-insensitively as usual, §1.4) and calls `FUN_009f6700` per row. Each `Morph_Set`'s `Name` is matched against a **fixed 14-entry category array** (`0x0130b5ac`) — this is the actual list of face/body-sculpting regions the character creator exposes:

**`Body, Face Global, Crown, Forehead, Brow, Eyes, Nose, Cheekbones, Ears, Chin, Mouth, Jaw, Hair, Skin`**

For each matched category: `DisplayName` (localized); `Morph_Infos > Morph_Info[]`, each processed by `FUN_009f6570` (not decompiled — a per-slider detail reader) into a **shared, cross-category pool capped at 128 (`0x80`) total `Morph_Info` entries across ALL 14 categories combined** **[OPEN — desk review 2026-09-30: no compare operator is given for the 128 cap; to be settled against the executable.]** (`DAT_0264d694` is a single running total, not per-category) — i.e. the 14 categories compete for one global 128-slot budget, not 14 independent budgets. Each `Morph_Info` records its owning category index and (on the base-game load path only) a running offset into a second, larger array (cap 129, `0x81` slots, ~~one entry short of `player_creation_morph_groups.xtbl`'s own array — see §14.2,~~ not confirmed to be the same array). **[Struck 2026-09-30, desk review: 129 is one MORE than 128, and §14.2/§21 give `player_creation_morph_groups.xtbl` no cap, so the comparison has nothing to be "one short" of.]** **[OPEN — desk review 2026-09-30: the 129 cap's compare operator and what this second array is are not given; to be settled against the executable (`FUN_009f6700`, `FUN_009f6570`).]**

**Validation:** 12 real `Morph_Set` rows (`misc_tables.vpp_pc` #201, 102,422 bytes) — 12 of the 14 possible category slots are populated in the base game (2 categories present in the fixed array have no matching authored row; which two was not individually identified this pass).

**Review status (2026-09-30): NEEDS-EXE: `FUN_009f6570`/`FUN_009f6700`, the 128/129 caps; 14-name array count re-verified = 14; 12-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 14.2 `player_creation_morph_groups.xtbl` — named UI-facing morph groupings

Single literal (`0x01160110`), reader `FUN_00831060`. Each `morph_group` (`0xc`=12-byte record): `Name` (both hashed AND kept as a display string — the same text is used for both, re-copied into a second buffer for the string form); `morph_list > morph_item[]`, each `0x8`-byte: `morph_name` (resolved via `FUN_009f5c30` against §14.1's morph namespace — a row with an unresolved name is silently dropped, not stored), `display_type` (only one recognised value, `"gender"`, sets a 1-bit flag; anything else leaves the flag clear).

**Validation:** 13 real `morph_group` rows (`misc_tables.vpp_pc` #204, 16,507 bytes). **[OPEN — desk review 2026-09-30: a 12-byte record holding a name hash and a string pointer leaves only 4 bytes for the `morph_list` pointer AND its count, so the stated `0xc` size and field list do not tile; to be settled against the executable (`FUN_00831060`).]**

**Review status (2026-09-30): NEEDS-EXE: `0xc`/`0x8` record layouts; 13-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 15. `player_creation_normal_maps.xtbl`, `player_creation_hair_color.xtbl`, `player_creation_skin_colors.xtbl`

**[CONFIRMED — disassembly + empirical, all three]**

### 15.1 `player_creation_normal_maps.xtbl` — per-body-type morph-to-normal-map curve

Single literal (`0x01176fc0`), reader `FUN_009fbf10`. Each `Normal_Map_Settings` row's `Name` is matched against an **8-entry body-type array** (`0x0130b670`): **`Base, Female, Male, Skinny, Fat, Muscle, Body Age, Face Age`** (the same array `player_master_sliders.xtbl`/`player_presets.xtbl` index into by category, §16). Per matched body type: `Morph_Sliders > Morph_Slider[]`, **capped at 4 sliders per body type**, each: `Slider_Name` (hashed), `Slider_Keys > Slider_Key[]` **capped at 8 keys per slider** **[OPEN — desk review 2026-09-30: no compare operator is given for either cap; to be settled against the executable (`FUN_009fbf10`).]** — each key `Slider_Position`/`Normal_Map_Strength` (float pair) — the accumulated key list is then run through `FUN_008f4700` (a sort/curve-normalise helper, likely the same sort primitive used for other authored keyframe curves in this project) to produce an interpolation-ready LUT.

**Validation:** 5 real `Normal_Map_Settings` rows (`misc_tables.vpp_pc` #205) — 5 of the 8 possible body-type slots are populated (the 3 unpopulated ones were not individually identified).

**Review status (2026-09-30): NEEDS-EXE: both cap compares (which 3 names are unpopulated is a data check, §22 item 6); 8-name array count re-verified = 8; 5-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 15.2 `player_creation_hair_color.xtbl`

Single literal (`0x01160480`), reader `FUN_00835cd0` (has a DLC-append variant, selected by a flag in its first argument). Each `Hair_Color` (`0x48`=72-byte record): `Name` (+ copy for string form), `Display_Name`, `Shaderball` (texture ref via `FUN_009f9f20` — the same helper `player_presets.xtbl`'s `Composites > Layer` field uses, §16.2, strongly suggesting a shared "shaderball preview" concept across hair/compositing), `swatch_image` (hashed texture name), `swatch_label` (optional text, defaults to a fixed placeholder string if absent), `Hue` (float, scaled by a fixed constant `DAT_01154b78`), `Saturation_light`/`Saturation_dark`/`Specular_alpha`/`Brightness`/`Brightness_bias` (plain floats), `Specular_bright`/`Specular_dark` (each parsed by `FUN_00dad130`, a multi-component parser — likely vec2/vec3, not narrowed further). A post-load hook (`FUN_00821740`/`FUN_00834460`, DLC path only) re-validates newly-appended DLC hair colours against an existing runtime array.

**Validation:** 35 real `Hair_Color` rows (`misc_tables.vpp_pc` #203). **[OPEN — desk review 2026-09-30: if every other field is 4 bytes, 12 dwords = 48 of 72 bytes leaves 24 = two vec3, which favours vec3 for `Specular_bright`/`Specular_dark` (§22 item 9), but this rests on that assumption; to be settled against real data (component count in the shipped text).]**

**Review status (2026-09-30): NEEDS-DATA: vec2 vs vec3 for `Specular_bright`/`Specular_dark`; 35-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 15.3 `player_creation_skin_colors.xtbl`

Single literal (`0x011602a4`), reader `FUN_00833690` (also has a DLC-append variant, selected by a flag in its first argument). Each `Entry` (`0x38`=56-byte record): `Name` (+ string copy), `display_name` (falls back to a copy of `Name` if the localization lookup misses), `swatch` (hashed texture name), `Shaderball` (via the same `FUN_009f9f20` helper as §15.2), `Unmasked_Hue`/`Unmasked_Saturation`/`Unmasked_Brightness` (floats — a distinct triple from the masked/final ones), `Hue` (reusing the §15.2 field-name constant `&DAT_01160254`), `Saturation`/`Brightness` (floats), `Specular_Alpha`/`Fresnel_Alpha`/`Specular_Power` (floats — a Fresnel term not present on `player_creation_hair_color.xtbl`'s record).

**Validation:** 55 real `Entry` rows (`misc_tables.vpp_pc` #206).

**Review status (2026-09-30): DESK-PASS (the `0x38` record tiles as 14 dwords = 56 bytes; 55-row tree VALIDATED-BY-DATA, Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 16. `player_master_sliders.xtbl`, `player_presets.xtbl`, `player_regional_presets.xtbl`

### 16.1 `player_master_sliders.xtbl` — **shipped `<Table>` is EMPTY, second confirmed instance this document** **[CONFIRMED — disassembly + empirical]**

Single literal (`0x01160a7c`), reader `FUN_00838330`. Structurally: `MasterSliders[]` (top-level category rows, each `Name`+`DisplayName`), each owning `Slider[]` (`0x14`=20-byte records: `Name`, `DisplayName`, `InitialValue` (float), and a `MorphList` text blob parsed via `FUN_00dac5b0` into a name list, each name resolved against §14's morph namespace via `FUN_009f5c30` into an `{id, weight=0}` pair array) — a UI slider that drives one or more named face/body morphs simultaneously by id, weight initialised to zero (presumably filled at runtime from the slider's live value, not from authored data).

**Real base data check: `misc_tables.vpp_pc` #211's `<Table>` node has ZERO direct children, and unlike `customization_slot_defaults.xtbl` (§6.2), its `<TableTemplates>` sibling is ALSO genuinely empty** (confirmed by direct string inspection — `<TableTemplates>\t</TableTemplates>`, no example rows either) — only a `<TableDescription>` schema-documentation block exists. **The whole "master slider" mechanism, while real and fully wired into the executable, has zero authored content at retail** — no master-slider category ships in the base game at all. This is a stronger version of the same lesson as §6.2: not just "don't count `TableTemplates`," but a genuine case where a real, non-trivial loader mechanism is completely unexercised by shipped data.

**Review status (2026-09-30): VALIDATED-BY-DATA: empty `<Table>` matches Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements" — desk review (not re-derived from the executable).**

### 16.2 `player_presets.xtbl` — full character-creator preset records **[CONFIRMED — disassembly + empirical]**

Single literal (`0x01160554`), reader `FUN_00835f90`. Each `Preset` is a **100-byte fixed record**: `Name` (hashed), `Race` (via `FUN_00832000`, presumably the same 4-name race vocabulary as elsewhere in this group — not independently confirmed to be the identical array), `Gender` (via `FUN_00831fc0`), `Default` (yes/no — marks the character creator's initial preset), `DisplayName`; `Hair` (item lookup via `FUN_008282c0` — **the same helper `customization_default_items.xtbl`'s `Item` field uses, §8** — confirming a preset's hair selection is a real `customization_items.xtbl` row, not a separate hairstyle list); `Hair_Length` (float); `Hair_Color_Primary`/`Hair_Color_Secondary` (each matched by name against §15.2's real loaded `Hair_Color` array — a live cross-reference into already-parsed data, not just an id); `Skin_Color` (matched against §15.3's loaded `Entry` array the same way); `Composites > Composite[]` (each: `Layer` via `FUN_009f9f20` — §9.3's `customization_compositing.xtbl` `Composite_Layer` lookup — plus an RGB `Color`, defaulting to opaque white if absent); `Preset_Grid > Preset_Element[]` (each: `Morph_Name` resolved against §14's morph namespace + a float `Value` — the actual per-preset sculpt data); `RegionalPresets > RegionalPreset[]` (each: `Category` via `FUN_00838cc0` + `Preset` via `FUN_00838d10` — both delegate into §16.3's own registry, confirming presets can reference regional-preset entries directly).

**Validation:** 8 real `Preset` rows (`misc_tables.vpp_pc` #212). **[OPEN — desk review 2026-09-30: the 100-byte record is given without offsets, and the `Race`/`Gender`/regional helpers (`FUN_00832000`, `FUN_00831fc0`, `FUN_00838cc0`/`FUN_00838d10`) were not decompiled; to be settled against the executable (`FUN_00835f90`).]**

**Review status (2026-09-30): NEEDS-EXE: record layout and helper vocabularies; 8-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

### 16.3 `player_regional_presets.xtbl` — named regional face templates, three levels deep

Single literal (`0x01160acc`), reader `FUN_008388d0`. Each top-level `RegionalPresets` row (yes, plural-named wrapper used as the per-row tag, same convention as `gang_customization.xtbl`'s `gang_vehicles`, §10): `Name`+`DisplayName`, then `Presets > Preset[]` (each: `Name`+`DisplayName`, `figure` via `FUN_00831f80`, `MorphTargets > MorphTarget[]` — each `Morph` resolved against §14's morph namespace + a float `Target` value). This is the registry §16.2's `RegionalPresets > RegionalPreset` cross-reference resolves into.

**Validation:** 1 real `RegionalPresets` row (`misc_tables.vpp_pc` #213, 9,783 bytes — all real content nested inside that one region's `Presets` grid). **[OPEN — desk review 2026-09-30: the value set `figure` is mapped to by `FUN_00831f80` is not given; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: `figure` vocabulary; 1-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 17. `player_cust_shot_map.xtbl`

**[CONFIRMED — disassembly + empirical]** Single literal (`0x0115d7c4`), reader `FUN_0080c330` — structurally and address-wise unrelated to every other table in this document (`0x0080xxxx`, not the `0x0082xxxx`/`0x00be xxxx`/`0x0083xxxx`/`0x009fxxxx` clusters above), and unrelated to `spec-tables-ui-controls.md`'s camera-preset tables despite the similar naming (it is NOT part of the `*_cameras*.xtbl` family that spec owns — see the scope note in §1). Each `NewEntity` row: `Name` (hashed) + `Anim_Pos` (hashed), the pair looked up together against a small fixed `0xc`-byte-stride array (`0x022cc0c8`..`0x022cc114`, ~~roughly 12 slots~~ **[corrected 2026-09-30, desk review: 0x022cc114 − 0x022cc0c8 = 0x4c = 76 bytes = 6⅓ strides of 0xc, so "roughly 12" is not supported by these addresses; the true slot count (6 or 7, depending on whether the end address is inclusive) is OPEN]**) of known entity/anim-position combinations; on a match, the resolved slot id and a copy of the entity's own local position (a stack-allocated float triple assembled just before the lookup — not shown fully in this pass's decompile) are pushed into a bounded output array (capacity `DAT_022cbf4c`, not itself read from this table). **[OPEN — desk review 2026-09-30: §22 item 7 names the output as `DAT_022cbf48`, a different address; whether these are one variable (count and array, or two fields) is not stated; to be settled against the executable (`FUN_0080c330`).]** This reads as a **promotional/store-screenshot camera-position binding table** — mapping specific named world entities (mannequins, kiosk displays) to a fixed catalogue of camera "shot" anchor points — consistent with its two dead siblings `player_cust_camera_anim_positions.xtbl`/`player_cust_camera_shots.xtbl` (§18) sharing the exact same `NewEntity` root-element convention despite neither being referenced by the executable.

**Validation:** 5 real `NewEntity` rows (`misc_tables.vpp_pc` #210, 6,590 bytes).

**Review status (2026-09-30): NEEDS-EXE: lookup-array bounds/stride and output variable; slot-count text fix applied; 5-row tree VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "34/34 live tables + 1/1 outfits cross-check + 12/12 dead files located, 0 typed-reader-vs-raw disagreements") — desk review (not re-derived from the executable).**

## 18. Dead / unreferenced files found while tracing this group

**[CONFIRMED — disassembly: zero occurrences of the exact NUL-delimited filename string anywhere in `SaintsRowTheThird.exe`, checked individually for each entry below via a fresh Ghidra literal scan of the whole image, not inferred from absence in a partial search]** All 12 exist as real, well-formed archive entries with real authored rows (row counts below) — this is the same phenomenon `spec-tables-traffic-ai.md` §14 already documented for `traffic_types.xtbl` and `spec-tables-weapons-combat.md` documented for `Beat_Down_Kill`: present in the shipped data, never opened by name anywhere in the retail binary.

| File | Real rows (`misc_tables.vpp_pc`) | Note |
|---|---|---|
| `player_creation_hair.xtbl` | 1 `Hair` row | minimal content; plausibly superseded by hairstyles living as ordinary `customization_items.xtbl` "head hair" slot items instead (§4, §6.1) |
| `player_choice_tutorial.xtbl` | 1 `Entity` row | |
| `player_cust_camera_anim_positions.xtbl` | 6 `NewEntity` rows | same root-element convention as the live `player_cust_shot_map.xtbl`, §17 — a dead sibling of a live mechanism |
| `player_cust_camera_shots.xtbl` | 38 `NewEntity` rows | ditto |
| `ui_bms_store_nobody.xtbl` | 1 `BitmapSheets` row | **misleading name** — despite "store" in the filename this is a `BitmapSheets` (texture-atlas) record, the same schema family `spec-tables-environment.md` already documents for `bitmap_sheets.xtbl`, not a customization store; noted here only because it surfaced in the `customization_stores.xtbl` archive-name search, not claimed as in-scope content |
| `custcharactercolors.xtbl` | 126 `CustCharacterColors_Identifier` rows | **same row count as `character_color_pool.xtbl`'s real 126 `Color_Entry` rows (§2.4)** — strong circumstantial evidence this is a Saints Row 2-era predecessor of that table, orphaned after the `character_color_pool.xtbl` rename (consistent with the `SR2_Player_Gang_Cust`/`SR2Skin1` legacy sentinels found live in §9.1/§10) |
| `custcolorpool.xtbl` | 52 `CustColorPool_Identifier` rows | same naming family; no single §2 pool matches its row count exactly |
| `custvehiclecolors.xtbl` | 121 `CustVehicleColors_Identifier` rows | vehicle-side counterpart; out of this document's scope even if it were live (would belong to `spec-tables-vehicle-world.md`) |
| `custvehiclecolorslots.xtbl` | 84 `CustVehicleColorSlots_Identifier` rows | ditto |
| `gang_customization-lightset.xtbl` | 1 `LightSet` row | shares the shared "LightSet" schema `spec-tables-vehicle-world.md` §11 documents for `Vehicle-Customization-Lightset.xtbl`/`store_gang_lightset.xtbl`/`test_shop_light_01.xtbl` — a fourth, dead sibling of that live family |
| `anim_customization.xtbl` | 1 `Skeleton_Set` row | animation/rig-adjacent, not visual customization; dead |
| `anim_gang_customization.xtbl` | 1 `Skeleton_Set` row | ditto |

**Why this matters for the task brief's framing:** the brief's premise ("customization_*.xtbl, character_*.xtbl, color pools, player_creation*.xtbl, and any other customization-related .xtbl files") turns out to include a substantial dead-file population — of the 30 candidate filenames examined, 12 (40%) **[stale count: the final candidate list was larger than 30 (§1 correction) — 35 live (§20) + 12 dead = 47, so 12/47 ≈ 26%]** are archive-only orphans with no code path reaching them at all, not a small edge case. `char_cust_cats.xtbl` (§12.3) is a 13th unreachable name, but isn't a real archive file, so it's counted separately. **[OPEN — desk review 2026-09-30: the deadness test is a whole-literal scan; a name built at runtime from parts (a stem such as `custcolorpool` or `gang_customization` plus a suffix, or a `%s.xtbl`-style format string) would not be found by it, and stem/format-string searches are not recorded; to be settled against the executable (stem and format-string scans).]**

**Review status (2026-09-30): NEEDS-EXE: stem/format-string scans; existence and row counts of all 12 files VALIDATED-BY-DATA (Team B `team-b/HANDOFF.md` section "9.95 `sr3tables_customization`": "12/12 dead files located") — desk review (not re-derived from the executable).**

## 19. Cross-table reference map

Every cross-reference below was independently confirmed either from this document's own disassembly (marked "this doc") or already documented by another spec and re-confirmed consistent here (marked with that spec's section).

| From | To | Mechanism | Section |
|---|---|---|---|
| `customization_items.xtbl` `Wear_Option > Active/Required/Incompatible_Flag` | `customization_flags.xtbl` `Flag.Name` | linear scan, hash compare | §4.2, §7.2 (this doc) |
| `customization_items.xtbl` `Variant > Material_List > Material` | `customization_materials.xtbl` `Cust_Material.Name` | hash lookup | §4.3, §9.1 (this doc) |
| `customization_items.xtbl` `Default_Colors_Grid`/`Variant` colours | §2's 4-slot colour pool (`FUN_00746d60`) | shared helper, pool index 0/2/3 | §4.4 (this doc) |
| `customization_outfits.xtbl` `Content_Element.Item` | `customization_items.xtbl` `Customization_Item.Name` | hash lookup into the loaded item array | §5.1 (this doc) — matches `spec-customization-data.md` §2.2's original DLC-sample-derived finding, now confirmed from disassembly |
| `customization_outfits.xtbl` `Variant_Element.Variant` | that item's own `Variant` array | name match | §5.1 (this doc) |
| `customization_stores.xtbl` `Store_Item`/`Outfits` | `customization_items.xtbl` / `customization_outfits.xtbl` arrays | index lookup | §5.2 (this doc) |
| `customization_categories.xtbl` `Slots_Grid`/`Obscure_Grid` | §6.1's 24-slot array / §7.2's flag registry | linear scan | §7.1 (this doc) |
| `customization_slot_defaults.xtbl` (mechanism only — shipped empty) | `customization_items.xtbl` / that item's `Wear_Option`/`Variant` | name match | §6.2 (this doc) |
| `customization_default_items.xtbl` `Item`/`Wear_Option`/`Variant`/`Color_Pool` | `customization_items.xtbl` array + §2 colour pool | name match / shared helper | §8 (this doc) |
| `player_creation.xtbl` `Morph_Info` / `player_creation_morph_groups.xtbl` `morph_item.morph_name` | shared player-creation morph namespace | `FUN_009f5c30`/`FUN_009f6570` | §14 (this doc) |
| `player_master_sliders.xtbl` `Slider.MorphList` | same morph namespace | `FUN_009f5c30` | §16.1 (this doc) |
| `player_presets.xtbl` `Hair` | `customization_items.xtbl` array | `FUN_008282c0`, same helper as `customization_default_items.xtbl`'s `Item` field | §16.2 (this doc) |
| `player_presets.xtbl` `Hair_Color_Primary/Secondary` | `player_creation_hair_color.xtbl`'s own loaded array | name match against already-parsed data | §16.2 (this doc) |
| `player_presets.xtbl` `Skin_Color` | `player_creation_skin_colors.xtbl`'s own loaded array | name match | §16.2 (this doc) |
| `player_presets.xtbl` `Composites.Layer` | `customization_compositing.xtbl` `Composite_Layer.Name` | `FUN_009f9f20` | §16.2, §9.3 (this doc) |
| `player_presets.xtbl` `RegionalPresets.Category`/`.Preset` | `player_regional_presets.xtbl` | `FUN_00838cc0`/`FUN_00838d10` | §16.2, §16.3 (this doc) |
| `gang_customization.xtbl` `Vehicles.Vehicle`/`DefaultVehicles.Slot*` | the global vehicle-info table | `FUN_00ac2400`/`FUN_00ac1ae0` | §10 (this doc), `spec-vehicle-data.md` §7.1 |
| `unlockables.xtbl` → `Gang_Vehicle_Customization/Vehicle_Group` | `gang_customization.xtbl` `gang_vehicles.gang_vehicles.name` | CRC-32 stored | `spec-tables-progression.md` §11 — confirmed consistent (this doc, §10) |
| `unlockables.xtbl` → `Clothes/Items/Item/ItemName` | `customization_items.xtbl` `Customization_Item.Name` | item lookup by name | `spec-tables-progression.md` §11 — confirmed consistent (this doc, §4) |
| `unlockables.xtbl` → `Outfit/Outfit` | `customization_outfits.xtbl` `Outfit.Name` | CRC → outfit list node | `spec-tables-progression.md` §11 — confirmed consistent (this doc, §5.1) |
| `unlockables.xtbl` → `Item/Color1..3` | `character_color_pool.xtbl` `Color_Entry.Name` | colour name → id | `spec-tables-progression.md` §11 — confirmed consistent (this doc, §2) |
| `unlockables.xtbl` → `Killbane_Mask/Mask` | `customization_items.xtbl` | authoring-schema-only, no reader (dead at retail) | `spec-tables-progression.md` §4.4 |
| `horde_mode.xtbl` `Customization.{Persona,Outfit,Customization_Items.*}` | `audio_personas.xtbl` / `customization_outfits.xtbl` / `customization_items.xtbl` / `character_color_pool.xtbl` | Reference (schema-confirmed only) | `spec-tables-diversions.md` §9 — confirmed consistent (this doc, §2, §4, §5.1) |
| `customization_items.xtbl` `Wear_Option.Mesh_Information...Filename` | `custmesh_<N>.str2_pc` bundle naming | engine string hash | `spec-customization-data.md` §5 — confirmed consistent, the exact mesh-string half of that recipe (this doc, §4.2) |

## 20. Literal-and-loader index

| Table | Literal address | Xrefs | Loader | Section |
|---|---|---|---|---|
| `character_color_pool.xtbl` | `0x012f6a78` (pointer slot, not the string — §2.1) | 1 (data, §2.1) | `FUN_00746800` (shared, array-driven) | §2 |
| `hair_color_pool.xtbl` | `0x012f6a7c` (pointer slot, not the string — §2.1) | 1 (data) | `FUN_00746800` | §2 |
| `makeup_color_pool.xtbl` | `0x012f6a80` (pointer slot, not the string — §2.1) | 0 | `FUN_00746800` | §2 |
| `tattoo_color_pool.xtbl` | `0x012f6a84` (pointer slot, not the string — §2.1) | 0 | `FUN_00746800` | §2 |
| `char_color_balance.xtbl` | `0x011526e8` | 1 | `FUN_007466a0` | §2.3 |
| `items_color_pool.xtbl` | `0x0116e55c` | 1 | `FUN_008fb6b0` (narrowed, not pinned) | §3.1 |
| `npc_color_palette.xtbl` | `0x01190434` | 1 | `FUN_00be29c0` | §3.2 |
| `customization_items.xtbl` | `0x0115f190` | 2 | `FUN_00829650` | §4 |
| `customization_outfits.xtbl` | `0x0115f1ac` | 2 | `FUN_008290c0` | §5.1 |
| `customization_stores.xtbl` | `0x0115f880` | 1 | `FUN_0082a6f0` | §5.2 |
| `customization_slots.xtbl` | `0x011774d0` | 1 | `FUN_009fca30` | §6.1 |
| `customization_slot_defaults.xtbl` | `0x0115f4d0` | 1 | `FUN_00828f00` | §6.2 |
| `customization_categories.xtbl` | `0x0115f3d4` | 1 | `FUN_008285f0` | §7.1 |
| `customization_flags.xtbl` | `0x0115f340` | 1 | `FUN_00828450` | §7.2 |
| `customization_icons.xtbl` | `0x0115f42c` | 1 | `FUN_008289e0` | §7.3 |
| `customization_default_items.xtbl` | `0x0115f148` | 1 | `FUN_00820f40` | §8 |
| `customization_materials.xtbl` | `0x0115f4a0` | 1 | `FUN_00828be0` | §9.1 |
| `customization_normals.xtbl` | `0x0117755c` | 1 | `FUN_009fcc00` | §9.2 |
| `customization_compositing.xtbl` | `0x01177514` | 1 | `FUN_009fcc00` (same fn, 2nd half) | §9.3 |
| `gang_customization.xtbl` | `0x01160c94` | 1 | `FUN_00839290` | §10 |
| `customizable_action.xtbl` | `0x01160198` | 1 | `FUN_00831270` | §11 |
| `character_definitions.xtbl` | `0x011900c8` | 3 | `FUN_00be1670` → `FUN_00be0630` (not decompiled) | §12.1 |
| `character_height.xtbl` | `0x01190098` | 1 | `FUN_00bdf770` | §12.2 |
| `character_customization_categories.xtbl` | `0x01190130` | 2 | shared stash fn (not decompiled) | §13.1 |
| `character_types.xtbl` | `0x011903b8` | 1 | `FUN_00be1d80` | §13.2 |
| `character.xtbl` | `0x01190120` | 2 | not decompiled | §13.3 |
| `player_creation.xtbl` | `0x01176dac` | 1 | `FUN_009f6870` → `FUN_009f6700` | §14.1 |
| `player_creation_morph_groups.xtbl` | `0x01160110` | 1 | `FUN_00831060` | §14.2 |
| `player_creation_normal_maps.xtbl` | `0x01176fc0` | 1 | `FUN_009fbf10` | §15.1 |
| `player_creation_hair_color.xtbl` | `0x01160480` | 1 | `FUN_00835cd0` | §15.2 |
| `player_creation_skin_colors.xtbl` | `0x011602a4` | 1 | `FUN_00833690` | §15.3 |
| `player_master_sliders.xtbl` | `0x01160a7c` | 1 | `FUN_00838330` | §16.1 |
| `player_presets.xtbl` | `0x01160554` | 1 | `FUN_00835f90` | §16.2 |
| `player_regional_presets.xtbl` | `0x01160acc` | 1 | `FUN_008388d0` | §16.3 |
| `player_cust_shot_map.xtbl` | `0x0115d7c4` | 1 | `FUN_0080c330` | §17 |

All 35 rows above are exact, singly-sourced literal matches (a "1" or "2" xref count reflects genuine base+DLC call sites, not ambiguity) except the two explicitly marked "0" (data-only references, §2.1) — no table in this document was reached through a fuzzy/substring/multi-candidate search.

## 21. Validation summary

All row counts below are **`<Table>`'s own direct children only** — `TableTemplates`/`TableDescription` scaffolding explicitly excluded per the task's governing methodology, verified with `tools/harnesses/an_tolerant_parse.py` (`spec-xtbl-format.md` §7 tolerant grammar) against real bytes extracted from `misc_tables.vpp_pc` via the mode-(a) reader (`spec-vpp-container.md` §7/§8, `tools/harnesses/vpp_modea.py`). `da_tables.vpp_pc` and `patch_compressed.vpp_pc` were also searched for every table in this document — **all 30 candidate filenames resolved from `misc_tables.vpp_pc` alone**, none needed the other two archives (no patched/DLC-only copy of any of these tables exists in `patch_compressed.vpp_pc`, unlike some tables in other groups this session). **[Superseded by the 2026-09-28 rows below: `npc_color_palette.xtbl` (36 rows) and `customization_materials.xtbl` (8 rows) do have `patch_compressed.vpp_pc` copies, which likely win at boot; and the candidate list was larger than 30 (§1 correction: 35 live tables).]**

| Table | Real row count | Cap found in disassembly | At/over cap? |
|---|---|---|---|
| `character_color_pool.xtbl` | 126 `Color_Entry` | none (heap-sized) | — |
| `hair_color_pool.xtbl` | 17 `Color_Entry` | none | — |
| `makeup_color_pool.xtbl` | 117 `Color_Entry` | none | — |
| `tattoo_color_pool.xtbl` | 11 `Color_Entry` | none | — |
| `char_color_balance.xtbl` | 1 `ColorBalance` (singleton) | n/a | — |
| `items_color_pool.xtbl` | 30 `Item_Color` | not determined (consumer OPEN) | — |
| `npc_color_palette.xtbl` | 34 `Palette` base (**36 in `patch_compressed.vpp_pc`** — same-named top-level entry, not a container-walk artifact; confirmed 2026-09-28, Team B, re-verified by SPEC TEAM; per this project's own archive-precedence finding, `spec-tables-progression.md` §14.2, the patch copy likely wins at boot — not re-extracted against the 36-row copy this pass) | none | — |
| `customization_items.xtbl` | 574 `Customization_Item` | **858** | under (67%) |
| `customization_outfits.xtbl` | 70 `Outfit` | none | — |
| `customization_stores.xtbl` | 15 `Store` (255 items/store **[OPEN: 254 vs 255, §5.2]**, 73 outfits/store caps) | none on store count | — |
| `customization_slots.xtbl` | 22 `Slot` (of 24 canonical names) | n/a (name-array driven) | 2 names unmatched |
| `customization_slot_defaults.xtbl` | **0** (confirmed empty `<Table>`, real content only in `TableTemplates`) | none | n/a |
| `customization_categories.xtbl` | 39 `Category` | none | — |
| `customization_flags.xtbl` | 39 `Flag` | none | — |
| `customization_icons.xtbl` | 10 `Icon` | none | — |
| `customization_default_items.xtbl` | 55 `Default` (nested under `Defaults > Defaults_List`) | none | — |
| `customization_materials.xtbl` | 7 `Cust_Material` base (**8 in `patch_compressed.vpp_pc`**, same finding/caveat as `npc_color_palette.xtbl` above) | none | — |
| `customization_normals.xtbl` | **8** `Normals` | **8** | **exactly at cap** **[OPEN: compare operator, §9.2]** |
| `customization_compositing.xtbl` | 374 `Composite_Layer` | **600** | under (62%) |
| `gang_customization.xtbl` | 1 `GangCustomization` (singleton, sentinel-name-gated) | n/a | — |
| `customizable_action.xtbl` | 56 `CustomizableActions` | none (2 sub-arrays, unsized individually) | — |
| `character_definitions.xtbl` | 282 `Character` | none (array sized to count+24) | — |
| `character_height.xtbl` | 3 `Height_Class` | none | — |
| `character_customization_categories.xtbl` | 16 `category` | none | — |
| `character_types.xtbl` | 96 `Type` | **120** | under (80%) |
| `character.xtbl` | 382 `Character` | not determined (loader not decompiled) | — |
| `player_creation.xtbl` | 12 `Morph_Set` (of 14 canonical names) | **128** total `Morph_Info` (shared across categories) | 2 names unmatched |
| `player_creation_morph_groups.xtbl` | 13 `morph_group` | none | — |
| `player_creation_normal_maps.xtbl` | 5 `Normal_Map_Settings` (of 8 canonical names) | 4 sliders/type, 8 keys/slider | 3 names unmatched |
| `player_creation_hair_color.xtbl` | 35 `Hair_Color` | none | — |
| `player_creation_skin_colors.xtbl` | 55 `Entry` | none | — |
| `player_master_sliders.xtbl` | **0** (confirmed empty `<Table>` AND empty `<TableTemplates>`) | none | n/a |
| `player_presets.xtbl` | 8 `Preset` | none | — |
| `player_regional_presets.xtbl` | 1 `RegionalPresets` | none | — |
| `player_cust_shot_map.xtbl` | 5 `NewEntity` | ~~~12-slot~~ lookup array **[slot count OPEN, §17: 0x4c bytes = 6⅓ × 0xc]** (not itself a row cap) | — |

**Two genuinely confirmed-empty `<Table>` nodes** (`customization_slot_defaults.xtbl`, §6.2; `player_master_sliders.xtbl`, §16.1) were found and are the direct, deliberate application of this task's governing methodology — both were checked by raw string inspection (not just the tolerant parser) to rule out a parser artifact before being written up (see each section for the confirming snippet). No overflow (shipped-rows > cap) was found anywhere in this document's tables — `customization_normals.xtbl` sits exactly at its 8-row cap, which is a real, confirmed-safe boundary condition, not an overflow.

## 22. Open items

1. **`character_definitions.xtbl`'s per-`Character` field reader, `FUN_00be0630`** (§12.1) — located (single call site, from the framework-filtered loop in `FUN_00be1670`) but not decompiled; the record size (236 bytes) is known indirectly from the caller's own allocation arithmetic, not from the reader itself. Same for `character_customization_categories.xtbl`'s reader (§13.1) and `character.xtbl`'s reader (§13.3, both call sites identified but neither field-reading function decompiled).
2. **What distinguishes `character.xtbl` from `character_definitions.xtbl`** (§13.3) — both use a `Character` root element, both are loaded from the same DLC-content-handler pass, but they are confirmed-separate files with different row counts (382 vs. 282). Not resolved; flagged rather than guessed.
3. **`items_color_pool.xtbl`'s exact consumer** (§3.1) — the loader function is narrowed to one strong candidate (`FUN_008fb6b0`, the single xref's containing function) but the exact `FUN_00dac9a0(...)` call and per-row field reader were not isolated from that function's other, unrelated initialisation work in this pass.
4. **`customization_slots.xtbl`'s unmatched-name fallback path** (§6.1) — when a `Slot` element's name doesn't match one of the 24 canonical names, the code checks for an `npc only` flag and then simply continues; what (if anything) happens to that row's other fields was not traced further.
5. **`customization_icons.xtbl`'s `Category` resolution target** (§7.3) — `FUN_00828340` is presumed to resolve against `customization_categories.xtbl`'s own array (§7.1) by naming-convention proximity, not independently confirmed by decompiling `FUN_00828340` itself.
6. **Which 2 of 24 slot names, which 2 of 14 `Morph_Set` categories, and which 3 of 8 body types are unpopulated at retail** — all three counts are confirmed exactly (§21), but only the slot case (`beard`/`eyebrows`, §6.1) was individually identified; the `Morph_Set` and `Normal_Map_Settings` gaps were not (a straightforward follow-up: diff the loaded array against the two fixed name lists, not attempted this pass for time).
7. **`player_cust_shot_map.xtbl`'s output array and the exact local-position fields** (§17) — the lookup mechanism and both input fields (`Name`/`Anim_Pos`) are confirmed; the destination array's own consumer (what reads `DAT_022cbf48` afterward) was not traced.
8. **Brand array contents** (§4.1) — `customization_items.xtbl`'s `Brand` field resolves against a 6-entry fixed array; only one entry (`Astro Gaming`) was individually read off the disassembly, the other 5 were not dumped.
9. **`Specular_bright`/`Specular_dark` field width** (§15.2) — parsed by a multi-component helper (`FUN_00dad130`) whose exact output arity (vec2 vs vec3) was not narrowed.

None of these gaps block the deliverable: all 18 live tables in this group are reached from their exact filename literal (none skipped, none approximated), 15 of 18 are resolved to full byte-offset/field level **[stale count: 35 live tables per the §1 correction and §20; the fully-resolved count was not recounted]**, and the remaining 3 (`character_definitions.xtbl`'s per-row fields, `character_customization_categories.xtbl`, `character.xtbl`) have a confirmed, singly-sourced loader entry point with an honestly flagged reader-level gap rather than a guessed schema. The dead-file round-up (§18) is exhaustive for the 30-filename candidate list this pass started from. **[The final candidate list was larger than 30, §1 correction.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (§19–§22: §20 pointer-slot annotation, §21 cap annotations; §19 unlockables rows agree with `spec-tables-progression.md` §11) — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): corrected the §1 hash-primitive note (struck the `FUN_00da7890`/tree-spec equation; these are the CRC-32 entry points), fixed the §20 row count (34→35), marked stale counts in §18, §21 and §22 (30 candidates / 18 live tables) and the §21 "no patch copy" claim superseded in place, added the patch-copy row counts at §3.2 and §9.1, and reworded 8 decompiler-shaped expressions (§4.1 and §9.3 loop conditions, §4.4 auto-label, 5 auto-named arguments in §9, §9.2, §9.3, §15.2, §15.3).
- 2026-09-30 (adversarial desk review, `review/adv_tables-customization.md`): added a review status summary and a per-unit review status line (36 units); fixed §2.2 "RGBA triples"→RGB (48-byte tiling), §17 "roughly 12 slots" (0x4c/0xc = 6⅓), struck the §6.1 narrative and the §14.1 "one entry short" comparison; annotated the §2.1/§20 pool addresses as pointer slots; added OPEN markers for every cap compare (858, 255/0xfe, 73, 8, 600, 120, 128, 129, 4, 8), the §4.2 `FUN_00da7930`/`Female_Mesh_Filename` conflicts with other specs, the §4.4/§8 clamp contradiction, §8 58-vs-0x50, and the other layout/semantic gaps; added label-scope notes on §12, §13.1, §13.3. No confidence label raised or lowered.
