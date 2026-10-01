# Saints Row: The Third — Animation Data Tables: Loader-Recovered Schemas

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process), agent AN
**Phase:** 2, "schema-from-loader" campaign (`HANDOFF.md` §31, archived §27.2), group *animation data tables* (`anim_*.xtbl` + lipsync/visemes).
**Scope:** For each `.xtbl` table assigned to this group, the element tree the exe's loader asks for, each element's type and destination in the runtime table (or global), defaults, required/optional behaviour, unit conversions, hash keys, capacities, cross-table references, and where loaded rows live (address/stride/count) — recovered **from the exe alone**, then validated against the real base-game table content (now readable, `spec-vpp-container.md` §7).
**Method:** Exactly the method of `spec-vehicle-data.md` §7 (agent AF) and `spec-tables-weapons-combat.md` §1 (agent AH): the literal table filename in the exe → its cross-reference(s) → the loader → the per-row reader → the element-name literals passed to the shared XML accessors. Ghidra project copy `tools/gp_an1` (disposable, robocopied from `tools/ghidra_projects`). Tooling: reused agent AF's/AJ's scripts unchanged (`tools/scripts/AfStrXrefs.java`, `AfRangeRefs.java`, `AfMem.java`, `AjBoth.java`); no new Ghidra scripts were written. Dumps: (evidence dump, not in repo). Validation harness: a tolerant node-tree parser (`spec-xtbl-format.md` §7 rules: mismatched close tags, control chars, name-space tolerant) written for this pass, run against the 16 tables extracted with `tools/harnesses/vpp_modea.py` from `misc_tables.vpp_pc` (all 16 live there). No whole-binary predicate search was used; every function was reached from a filename literal, an element-name literal, or a named global already documented elsewhere in this project.
**Cleanroom compliance:** No decompiled code is reproduced verbatim; no original internal identifiers are used (functions are cited by address only, as evidence anchors). XML element/table names, enum strings and other data literals are game data and are listed as such, per this project's standing convention. Offsets, strides, capacities and constants are stated as measured. `.czn_pc`'s interior was not touched.
**Confidence key:** **CONFIRMED — disassembly** · **CONFIRMED — empirical** · **HIGH CONFIDENCE — inferred** · **HYPOTHESIS — unconfirmed** · **OPEN / UNKNOWN**.

**Review status summary (2026-09-30).** An adversarial desk review (no executable, no game data; Team B's `team-b/HANDOFF.md` §9.90 "`sr3tables_animation`" used as the independent real-data source) covered 22 units: **DESK-PASS 1**, **DESK-PASS, text fixes applied 6**, **NEEDS-EXE 13**, **NEEDS-DATA 1**, **VALIDATED-BY-DATA 1**. A desk pass alone does not clear a unit: it means the text is internally consistent and its arithmetic reproduces, not that it was re-derived from the executable. Only the VALIDATED-BY-DATA unit (§3.5) is already backed by Team B's full-population run (all 38 archives), and that run checks row tags, top-level child names, enum/flag membership and caps only, never record offsets, hashes, cross-table resolution or children below the top-level row (so not `Triggers/Trigger/*`). Units awaiting the executable: §1.3, §2, §3.3–§3.4, §4, §5, §6, §7, §8, §11, §12, §13, §14, §15; awaiting real data: §3.1–§3.2 (`Trigger` child shape, high priority for Team B). Six internal contradictions are marked OPEN at both places rather than resolved: the §7.2/§7.3 `State` stride vs control-point cap, the blend-tree flag bit (§3.3 vs §7.1), the preset write order (§8.1 vs §8.2), truncate vs drop for long trigger names (§9.1), whether slots 7 and 9 are read at all (§1.4 vs §15.1), and the `<Trigger>` shape (§3.1 vs §3.2). One arithmetic slip was corrected from the spec's own numbers (§18.2 size range).

---

## 1. Overview, coverage matrix, and the shared reader grammar

### 1.1 Coverage matrix

Every one of the 16 assigned filenames was searched as an exact NUL-delimited string in the exe image (`AfStrXrefs.java`, case-sensitive pattern, checked case-insensitively against the literal's actual casing). **All 16 have exactly one literal in the exe** — none had to be skipped, and none appears in a different case than listed in the task.

| Table | Code xrefs | Loader / reader (entry) | Specced in |
|---|---|---|---|
| `anim_set_filenames.xtbl` | 1 | `0x004AEB1B`→`FUN_004AEA30` | §2 |
| `anim_files.xtbl` | 2 | `0x004AE7A0` (+ a second unrelated xref inside `0x008C7F80`, not traced — out of this group's scope) | §3 |
| `anim_groups.xtbl` | 1 | `FUN_004CCCC0` | §4 |
| `anim_states.xtbl` | 1 | `FUN_004BF8D0` | §5 |
| `anim_actions.xtbl` | 1 | `FUN_004BF8D0` (same function) | §5 |
| `anim_transitions.xtbl` | 1 | `FUN_004AEA30` (same function as `anim_set_filenames.xtbl`, different code region) | §6 |
| `anim_blend_trees.xtbl` | 1 | `FUN_004AEA30` (same function, third region) | §7 |
| `anim_ik_situation.xtbl` | 1 | `FUN_004AE390` | §8 |
| `anim_triggers.xtbl` | 1 | `FUN_004B15A0` | §9 |
| `anim_flinches.xtbl` | 1 | `FUN_0097DDB0` | §10 |
| `anim_synced.xtbl` | 1 | `FUN_0095DE40` | §11 |
| `anim_correction_offsets.xtbl` | 1 | `FUN_009580F0` | §12 |
| `creation_lipsync_animations.xtbl` | 1 | `FUN_008314A0` | §13 |
| `character_visemes.xtbl` | 1 | `FUN_00978CA0` | §14 |
| `anim_set_properties.xtbl` | 1 | registered in the table-registry manifest (§1.4) but **no function anywhere in the exe opens it** | §15 |
| `anim_prop_sets.xtbl` | 1 | registered in the table-registry manifest (§1.4) but **no function anywhere in the exe opens it** | §15 |

`anim_files.xtbl`'s literal has a second code xref inside `0x008C7F80`, a function this document did not trace ~~(it lies in the action-node/AI territory already assigned to `spec-tables-traffic-ai.md`'s group)~~ **[Correction — desk review 2026-09-30: `spec-tables-traffic-ai.md` never mentions `0x008C7F80` (its descriptor readers are `0x004CB510`/`0x004CAEE0`), so this function is untraced in every spec.]**; everything in §3 below comes from the first xref's reader only.

**[Note — desk review 2026-09-30: the "Loader / reader (entry)" column names the reader, not the literal's xref site. For the eleven registry tables (§1.4) the single literal xref is inside `FUN_005D25F0`; for `anim_files.xtbl` the two xrefs are `FUN_005D25F0` and `0x008C7F80`, and `0x004AE7A0` is the reader.]** **[OPEN — desk review 2026-09-30: whether `0x008C7F80` opens `anim_files.xtbl` itself (a second reader of this table) or only compares or prints the literal; to be settled against the executable.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (second `anim_files.xtbl` xref NEEDS-EXE) — desk review (not re-derived from the executable).**

### 1.2 The shared reader grammar (cited, not re-derived)

This group's readers use the **same hand-written XML node model and shared accessor family already fully documented in `spec-tables-weapons-combat.md` §1.2–§1.4**: node layout `{name, next-sibling, first-child, text}`, case-insensitive name matching, the "always"/"if present" scalar-reader pairs (`FUN_00DACCB0`/`FUN_00DACD40` for `f32`, etc.), the `Filename` child getters (`FUN_00DAC880`/`FUN_00DAC8C0`), the `Flag`-list testers (`FUN_00DAC740`/`FUN_00DAC7D0`), and the name hash `FUN_00D9E8B0`/`FUN_00D9E740` (table-driven CRC-32, reflected, init 0, no final XOR, lower-cased; this group's callers all pass seed `0`). This document does not re-derive any of that; **§1.3 below records only where this group's readers differ or add a new shared primitive**, and each table's section cites the specific accessor calls it uses.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

### 1.3 Additions to the shared grammar found in this group

- **`FUN_004BF810(name)`** — looks up a text name in the **merged State/Action name registry** (`DAT_03171C10`, stride `0x10`, count `DAT_03171C08`; built by `FUN_004BF8D0`, §5) by `_stricmp`; returns the index or `-1`. Used by `anim_transitions.xtbl` (§6) and `creation_lipsync_animations.xtbl`'s `Animation` field (§13) and `anim_flinches.xtbl`'s `Name` field (§10).
- **`FUN_004BF650(name)` → `FUN_004BF610(ptr)`** — resolve a **loaded-animation `Filename`** to a validated index into the shared "loaded animation" record array (`DAT_02EF2178`, stride `0x44`, count `DAT_03171BE8`, cap `0x1964`=6,500). `FUN_004BF650` hashes the name and looks it up in a generic open-chained string→pointer hashtable (buckets `DAT_0340B228`, chain `DAT_034117B8`, keys `DAT_03417D48`, values `DAT_0341E2D8` — the same hashtable object used throughout this group); `FUN_004BF610` then range-checks the returned pointer and divides by the `0x44` stride to get a bounded index, `-1` on any failure. This is the cross-table key that ties `anim_files.xtbl` (the record's owner, §3), `anim_blend_trees.xtbl` (§7), `anim_correction_offsets.xtbl` (§12) and `anim_synced.xtbl` (§11) together — **all four bind additional data onto an *existing* loaded-animation record by matching its `Filename`/`Name`, never by defining a new one.**
- **`FUN_00462960`** — the audio middleware's string→event-id function, already named in `spec-tables-weapons-combat.md` §1.4 ("Sound-event names go to the audio middleware's own string-to-id function"); this group's `creation_lipsync_animations.xtbl` (§13) is a second, independently-found caller (via the one-line wrapper `FUN_0070A2F0`), confirming the citation.
- **Rotate/XOR hash `FUN_00DA7890`** (the rotate-6/XOR name hash over the lowercased string; per the hash-breadth correction in `spec-extensionless-types.md` §4 it has only ≈18 call sites, against ≈835 for the table-driven CRC-32 — it is the narrowest of the engine's string hashes, not the engine-wide one; correction recorded in `HANDOFF.md` §24, previously cited here as §27.2) — `anim_triggers.xtbl`'s registry (§9) is one of those sites, and it is the hash `anim_files.xtbl`'s `<Anim_file><Triggers><Trigger>` validator (§3) checks its own text against, **not** the CRC-32 `FUN_00D9E8B0` family used everywhere else in this group.

**[OPEN — desk review 2026-09-30: for `FUN_004BF650` this text does not state which hash is used, whether the key keeps the `.animx` extension or is lower-cased, the bucket count, or what happens on a duplicate name, so the name→index binding cannot be implemented from it (§3.5's 109 extensionless rows depend on this); `FUN_00DA7890`'s handling of bytes ≥ 0x80 (signed or unsigned) is also not stated. To be settled against the executable (`0x004BF650`, `0x004BF610`, `0x00DA7890`) and real data (duplicate `Animation/Filename` census, case-insensitive).]**

**Review status (2026-09-30): NEEDS-EXE: `FUN_004BF650` hash, key form and duplicate policy not stated — desk review (not re-derived from the executable).**

### 1.4 A note on this group's architecture: a 16-slot table-dependency registry

Eleven of the sixteen filenames (`anim_files`, `anim_states`, `anim_actions`, `anim_groups`, `anim_set_filenames`, `anim_triggers`, `anim_set_properties`, `anim_ik_situation`, `anim_prop_sets`, `anim_transitions`, `anim_blend_trees`) do **not** appear as a direct argument to the document-open call `FUN_00DAC9A0` at their literal's own xref. Instead, the literal is used once, inside the huge game-bootstrap function `FUN_005D25F0` (already named "game init" by `spec-tables-weapons-combat.md` §2), to populate one **16-slot, `0x100`-byte-stride manifest** at global `DAT_03518830` (persisted after a `memcpy` from the stack; validated as complete by `FUN_004C8690`, which checks that slot `N`'s name buffer at `slot+4` is non-empty for all 16 slots plus six trailer fields **[OPEN — desk review 2026-09-30: the six trailer fields are not listed. This document places `DAT_03519834` (+0x1004), `DAT_03519838` (+0x1008) and `DAT_03519840`/`44` (+0x1010/+0x1014) inside the `0x1048` block (§4, §5), while `spec-tables-traffic-ai.md` §22 describes pointers at +0x102C onward; it also says the descriptor is copied into a global block by `0x004C8780`, so whether `DAT_03518830` is the staging or the final block is not settled. To be settled against the executable (`0x005D25F0` write order, `0x004C8690`, `0x004C8780`).]**). Each slot's `slot+4` field holds **the filename string itself** (not a "loaded" flag, as a first read of the validator suggested — corrected after checking who actually reads each slot address, §1.4.1). A handful of dedicated functions then open one slot's filename by taking its *address* (`&DAT_03518830 + 0x100·N + 4`) as the `FUN_00DAC9A0` argument, rather than the original string literal — which is why `AfStrXrefs` (a literal-string search) finds only one code reference for each of these eleven tables, all inside the manifest-building code, even though the files are genuinely opened and read elsewhere.

**Slot map (base `DAT_03518830`, name field at `slot·0x100 + 4`), recovered by cross-referencing every address in the manifest's `0x1048`-byte range (`AfRangeRefs`, 52 refs in 16 functions — exhaustive, not sampled) against the write order inside `FUN_005D25F0`:**

| Slot | Table | Reader |
|---|---|---|
| 0 | (a `tables\` directory-prefix string, not one of the 16) | consumed by `FUN_004AEA30`'s path-building code only |
| 1 | `anim_files.xtbl` | `FUN_004AE7A0` |
| 2 | `anim_states.xtbl` | `FUN_004BF8D0` |
| 3 | `anim_actions.xtbl` | `FUN_004BF8D0` |
| 4 | `anim_groups.xtbl` | `FUN_004CCCC0` |
| 5 | `anim_set_filenames.xtbl` | `FUN_004AEA30` |
| 6 | `anim_triggers.xtbl` | `FUN_004B15A0` |
| 7 | `anim_set_properties.xtbl` | **none found** (§15) |
| 8 | `anim_ik_situation.xtbl` | `FUN_004AE390` |
| 9 | `anim_prop_sets.xtbl` | **none found** (§15) |
| 10 | `anim_transitions.xtbl` | `FUN_004AEA30` |
| 11 | `anim_blend_trees.xtbl` | `FUN_004AEA30` |
| 12–15 | `control_parameters.xtbl`, `control_filters.xtbl`, `node_graph_files.xtbl`, a second directory-prefix string | out of this group's scope (UI/controls, agent AP) |

**[CONFIRMED — disassembly]** for the slot base/stride/count and for slots 1–6, 8, 10, 11 (each verified by reading the exact instruction that pushes `&DAT_03518830 + 0x100·N + 4` as the `FUN_00DAC9A0` argument, §1.4.1). **HIGH CONFIDENCE** for the slot-0/12–15 identifications (positional, from `FUN_005D25F0`'s write order, not independently confirmed by a load call since they are out of scope or a directory string). Slots 7 and 9 are addressed and validated as non-empty by `FUN_004C8690` but **no function in the exe ever passes their address to `FUN_00DAC9A0`** (§15) — a real, exhaustively-checked dead registration, not a gap in this pass's search. **[OPEN — desk review 2026-09-30: this sentence says `FUN_004C8690` reads slots 7 and 9, but §15.1 says no function reads either name address "for any purpose". Both cannot hold unless the validator uses indexed addressing (base + N·0x100), and indexed access would also escape the literal-address range scan §15.1 relies on. Which is right is to be settled against the executable (`0x004C8690` loop body; refs to the `DAT_03518830` base with indexed addressing).]**

#### 1.4.1 Correction made during this pass

An earlier reading of `FUN_004C8690` (the slot-completeness check) took `slot[N]+4` for a per-slot **loaded** boolean, because that is the first byte the validator tests. Decompiling the actual consumers (`FUN_004AE7A0`, `FUN_004B15A0`, `FUN_004AE390`, `FUN_004CCCC0` — all four push `&DAT_0351xxxx` directly into `FUN_00DAC9A0`, which per `spec-tables-weapons-combat.md` §1.2 takes a **filename**, not a document handle) shows `slot+4` is the **filename text itself**; the validator's test is simply "does this slot have a non-empty name", not a runtime-loaded flag. ~~Struck through above before being written anywhere else.~~ **[Desk review 2026-09-30: there is no struck-through text above; the first guess was never written into this document, so this sentence has nothing to point at.]** **[CONFIRMED — disassembly; corrected in place, not left as the first guess.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (trailer fields, staging vs final block, and the slot-7/9 read NEEDS-EXE) — desk review (not re-derived from the executable).**

## 2. `anim_set_filenames.xtbl` — a name→satellite-file binding, not a name→`.anim_pc` binding

**Loader:** slot 5 of the registry (§1.4), opened and read by `FUN_004AEA30` at `0x004AEB1B`. **[CONFIRMED — disassembly + empirical.]**

### 2.1 What the task brief expected vs. what this table actually is

The task brief flagged this table as "very likely the name→`.anim_pc` filename binding" — the highest-priority guess for this group. Tracing the reader and then reading the real base-game rows **refutes the direct-binding reading and replaces it with a different, still-useful mechanism**: each row names **another `.xtbl` file** (a per-vehicle-family or per-character-class *animation set description* file), not a `.anim_pc`/`.animx` clip directly.

**Root element:** `<Anim_Set_Filename>`, repeated. Real base-game sample (`misc_tables.vpp_pc`, 103 rows):

```
<Anim_Set_Filename><Name>anim_auto.xtbl</Name><Path>\vehicles\automobiles\base</Path>
<Default_model>cm_body.cmeshx</Default_model><Default_rig>cm_body.rigx</Default_rig></Anim_Set_Filename>
```

### 2.2 Field-by-field, traced side ~~**[CONFIRMED — disassembly]**~~ **[CONFIRMED — disassembly for items 1–3 and the call sequence in item 4; the satellite files' schema and the element matched in them (item 4) are OPEN]**

Only `Name` is consumed by the traced reader:

1. `FUN_00DABA70(dest, 0x100, row, &DAT_0129EE6C)` — bounded copy (≤ `0x100` bytes) of the row's `Name` text (the same `"Name"` literal address used by every other table in this group and by `weapons.xtbl`, per `spec-tables-weapons-combat.md` §1.5.4).
2. The copied `Name` is appended, byte-for-byte, right after a fixed **`tables\`** directory-prefix string (slot 0 of the same registry, §1.4) into a rolling 256-byte-stride array (count tracked in a local, persisted afterward at `DAT_03171BD8`/`DAT_03171BDC`).
3. **`Name`'s value is itself a filename with an `.xtbl` extension** (confirmed empirically, §2.4) — so the concatenation produces a path of the shape `tables\anim_auto.xtbl`, i.e. **this table lists other top-level `.xtbl` files to be opened**, prefixed with the tables directory.
4. Immediately after building this path list, `FUN_004AEA30` calls `FUN_004AE7A0` — **the `anim_files.xtbl` reader itself (§3)** — and then walks the collected path array a second time, opening each named `.xtbl` in turn and matching its rows' element text against `DAT_03519838` (the `anim_groups.xtbl` name array, §4) to accumulate two per-group counts before a second pass fills per-group data (`FUN_004AE4F0`, not traced further in this pass). **This makes `anim_set_filenames.xtbl` the entry point of a second layer of animation-group-scoped `.xtbl` files** (one per vehicle family/character class — `anim_auto.xtbl`, `anim_moto_cruiser.xtbl`, `anim_jet.xtbl`, … per the real sample below), each of which presumably carries its own `Anim_Group`-tagged content; **the internal schema of those satellite files was not traced** (OPEN — out of budget for this pass; each is itself a full `.xtbl` and would need its own loader trace). **[OPEN — desk review 2026-09-30: the reader must name some element or tag in each satellite file to extract the text it matches, and that name is not given; nor are the path array's capacity, the order and overflow behaviour of the prefix + `Name` concatenation in a 256-byte slot, or the difference between `DAT_03171BD8` and `DAT_03171BDC`. To be settled against the executable (`0x004AEA30` around `0x004AEB1B`, `FUN_004AE4F0`).]**

### 2.3 Fields present in real data but not reached by the traced reader **[CONFIRMED — empirical for presence; HIGH CONFIDENCE that they are unread, from the same exhaustive trace of `FUN_004AEA30` as §1.4]**

Real rows also carry `Path` (a directory string, e.g. `\vehicles\automobiles\base`), `Default_model` (a `.cmeshx` filename) and `Default_rig` (a `.rigx` filename) — none of which appear in the traced code path. These plausibly feed a **customization/creation-mode default-appearance lookup** (ties to `spec-customization-data.md`'s and `spec-rig-format.md`'s territory) rather than the animation loader itself, but that consumer was not found in this pass — **OPEN**.

### 2.4 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_set_filenames.xtbl` (12,484 bytes): **103/103 rows have a non-empty `Name`**; sampled `Name` values are all `.xtbl` filenames (`anim_auto.xtbl`, `anim_moto_cruiser.xtbl`, `anim_moto_rocket.xtbl`, `anim_boats_craft.xtbl`, `anim_moto_atv.xtbl`, `anim_jet.xtbl`, `anim_plane_standard.xtbl`, `anim_moto_dirt.xtbl`, …) — confirming §2.1's "satellite `.xtbl` file list" reading and refuting a direct `.anim_pc`/`.animx` binding. **[Desk review 2026-09-30: only a sample of the 103 names is listed; that all 103 end in `.xtbl` and resolve to archive entries is to be settled against real data.]**

**Review status (2026-09-30): NEEDS-EXE: satellite match element, path-array capacity and concatenation order not stated — desk review (not re-derived from the executable).**

## 3. `anim_files.xtbl` — the loaded-animation record, and the `.animx` name→index binding

**Loader:** slot 1 of the registry (§1.4), read by `FUN_004AE7A0`, which delegates to three sub-readers (`FUN_004ADED0` for `Triggers`, `FUN_004AE010` for `IKs`, `FUN_004AE1E0` for `Sounds`) called once per row. **[CONFIRMED — disassembly.]** This is the largest table in the group by byte size (2,576,431 bytes / 5,968 rows in the base game) and is the **actual name→`.animx` binding this group's animation system runs on** — the role the task brief expected of `anim_set_filenames.xtbl` (§2).

### 3.1 Element tree **[CONFIRMED — disassembly for the reader; CONFIRMED — empirical for the tag spelling and nesting (the shape of `<Trigger>` was OPEN; resolved by data 2026-10-01, see below), corrected against real base-game rows where the disassembly's stack-variable naming was ambiguous — see §3.5]**

```
<root><Table>
  <Files><Anim_files>
    <Anim_file>
      <Animation><Filename>…</Filename><Preload>bool</Preload></Animation>
      <Triggers><Trigger><Name>…</Name><Frame>…</Frame></Trigger>…</Triggers>   (corrected 2026-10-01 from data, job …-kmsh; was drawn as <Trigger>…</Trigger>)
      <IKs><IK>…</IK>…</IKs>
      <Sounds><Sound>…</Sound>…</Sounds>
      <Voice_Lines>…</Voice_Lines>          <!-- authored, always empty in the base game, never read -->
      <misc>…</misc>                         <!-- generic extension point, see 3.4 -->
      <Flags><Flag>…</Flag>…</Flags>
    </Anim_file>
    …
  </Anim_files></Files>
</Table></root>
```

~~**[OPEN — desk review 2026-09-30: the tree above draws `<Trigger>…</Trigger>` with direct text, but §3.2 says each `Trigger`'s `Name` text is hashed, i.e. `<Trigger><Name>…</Name></Trigger>`. The two shapes conflict and the empirical check in §3.5 counted only non-empty `Triggers` containers, not the child shape. To be settled against real data (count `Triggers/Trigger` elements with a `Name` child vs direct text) and the executable (`FUN_004ADED0`).]**~~ **[Resolved by data 2026-10-01: §3.2's shape is the real one — `<Trigger><Name>…</Name>…</Trigger>`. Team B's job `20261001T004133-team-b-kmsh` (`team-b/HANDOFF.md`, item "Job 08": "7,060 elements, Name child 7,060, direct text 0, both 0, neither 0") counted every `anim_files.xtbl` `Triggers/Trigger` element. The tree above is corrected to read `<Trigger><Name>…</Name><Frame>…</Frame></Trigger>`. **VALIDATED-BY-DATA (kmsh)** for the shape. Every `Trigger` also carries a **`Frame`** child (7,060/7,060), which the loader description in §3.2 does not mention: observed field, its reader and meaning OPEN until the executable (`FUN_004ADED0`).]**

### 3.2 `<Anim_file>` row fields

- **`Animation`** (container) → **`Filename`** (text): resolved via `FUN_004BF650`/`FUN_004BF610` (§1.3) into the shared loaded-animation array `DAT_02EF2178` (stride `0x44`, cap 6,500) — **this Filename is the `.animx` name the row is keyed by**; every row gets one array slot (free-list allocator `FUN_004CA4A0`/`FUN_004BEC10` over a 7,500-slot secondary hash cache `DAT_030F0C68`). **`Preload`** (bool, inside the same container): present in the real data (`True`/`False` on every sampled row) but **not reached by the traced reader body** — `FUN_004AE7A0`'s full 648-instruction body was read in full and never queries this sibling; it is presumably consumed by the streaming/on-demand-load path rather than this row reader (OPEN — not traced further in this pass).
- **`Triggers`** → repeated **`Trigger`**: each `Trigger`'s `Name` ~~text~~ child **~~[OPEN — desk review 2026-09-30: `Name` child vs direct text conflicts with the §3.1 tree; see the note under §3.1]~~ [VALIDATED-BY-DATA 2026-10-01 (job `…-kmsh`): 7,060/7,060 `Trigger` elements have a `Name` child, 0 have direct text; each also has a `Frame` child, not described here — meaning OPEN, NEEDS-EXE]** is hashed with the **rotate/XOR hash `FUN_00DA7890`** (§1.3, not the group's usual CRC-32) and checked against the registry `anim_triggers.xtbl` builds (`DAT_03132B18`/`DAT_03132CD0`, §9); an unrecognised name causes a formatted diagnostic message naming what is being parsed, the group and the tag — **this is a validation pass only, no per-trigger data is written into the `Anim_file` record.** **[CONFIRMED — disassembly.]**
- **`IKs`** → repeated **`IK`**, each: `Enable` (bool) · `Location` (enum, 4 values: `Left Hand`=0, `Right Hand`=1, `Left Foot`=2, `Right Foot`=3, table at `0x012959C0`) · `Situation` (text, resolved against `anim_ik_situation.xtbl`'s runtime array `DAT_034248C8`, §8 — cross-table reference) · `Frame` (int) · `Blend` (int, converted to a float divisor `DAT_012A2D58/Blend`, 0.0 if `Blend`=0). Stored in a **global shared pool** `DAT_02EDC998` (stride `0x14`=20 bytes), one contiguous run per `Anim_file` referenced from the record at `+0x38`, count at record `+0x29` (byte). **[CONFIRMED — disassembly.]**
- **`Sounds`** → repeated **`Sound`**, each: `Frame` (int) · `Audio_Switch_0` / `Audio_Switch_1` (text of the form `bank:event`, split on `:` and resolved through the audio subsystem, `FUN_0046FD00`) · `Replay_On_Cycle_Restart` (bool) · `Stop_When_Anim_Stops` (bool) · `Audio_Event` (text, resolved the same way). Stored in a global shared pool `DAT_02E99688` (stride `0x1C`=28 bytes), referenced from the record at `+0x3C`, count at record `+0x2A` (byte). **[CONFIRMED — disassembly.]**
- **`Voice_Lines`**: present as an element in every sampled row but **always empty** (0/5,968 base-game rows carry any child, §3.5) and **not read by the traced row reader at all** — a doubly-dead field (unauthored *and* unread). **[CONFIRMED — empirical for emptiness; CONFIRMED — disassembly for "not read", from the same full-body read as `Preload` above.]**
- **`misc`** (child named `misc`, literal `"misc"` at `0x012A2184`): if present, and a global callback pointer `DAT_03171BC4` is set, calls the function pointer stored at `DAT_03171BC4` with the child element, the record and the value of `DAT_03171BF8` as arguments — a generic extension hook for other subsystems to hang data off an `Anim_file` row; **which subsystem installs this callback was not traced (OPEN)**.
- **`Flags`** → repeated **`Flag`**, tested against a **fixed 30-entry literal table** (`0x013507B0`) and OR'd into record `+0x2C`: `Additive`(0)·`Block_Rotation`(1)·`Block_Translation`(2)·`Transition_Action`(3)·`Zero_Input`(4)·`Female_Root_Offset`(5)·`No_Interpolation`(6)·`No_Spinebending`(7)·`Anim_Movement`(8)·`Disable_state_audio`(9)·`Use_Shortest_Rotation`(10)·`Steering_Offset_180`(11)·`Ignore_camera_collision`(12)·`Use_Camera_Root_Offset`(13)·`Use_Animated_Camera`(14)·`No_IK`(15)·`Sitting`(16)·`Translates_In_Vehicle`(17)·`Transform_When_Offscreen`(18)·`Combat_Ready`(19)·`Hide_Weapon`(20)·`No_Fall`(21)·`Fat_bones_forward`(22)·`Fat_bones_disabled`(23)·`Disable_Controller_Actions`(24)·`Delay_Weapon_Fire`(25)·`Disable_Weapon_Fire`(26)·`"Transition to 50%"`(27)·`Invalid_synced_target`(28)·`"Cloth Sim Hack"`(29). **[CONFIRMED — disassembly for the reader (`FUN_00DAC740`, the shared flag-list bitmask tester, §1.2) and the exact 30 strings (read directly from the literal pointer table); CONFIRMED — empirical that real data uses only these 30, §3.5.]**

**[OPEN — desk review 2026-09-30: the reader flavour ("always" or "if present", §1.2) and so the default on absence is not stated for IK `Enable`/`Frame`/`Blend` or for Sound `Frame`/`Replay_On_Cycle_Restart`/`Stop_When_Anim_Stops`; the field offsets inside the IK pool records (stride `0x14`) and Sound pool records (stride `0x1C`) are not given; for `Audio_Switch_0/1` it is not stated which half of `bank:event` is which or what happens without a `:`; whether the `Location` compare is case-sensitive is not stated. To be settled against the executable (`0x004AE010`, `0x004AE1E0`, `0x004ADED0`; the float at `0x012A2D58`).]**

**Review status (2026-09-30): ~~NEEDS-DATA: `<Trigger>` child shape (§3.1 vs §3.2)~~ `<Trigger>` shape VALIDATED-BY-DATA 2026-10-01 (job `…-kmsh`: `Name` child 7,060/7,060; `Frame` child 7,060/7,060, meaning OPEN); reader flavours and pool offsets NEEDS-EXE; diagnostic text paraphrased — desk review (not re-derived from the executable).**

### 3.3 Record layout summary **[CONFIRMED — disassembly, offsets record-relative within the `0x44`-byte `DAT_02EF2178` element; the blend-tree bit number is OPEN]**

`+0x00` name-hash-copy fields (identity of the source name, used by the generic hashtable) · `+0x08` a validated hashtable slot index into `DAT_030F0C68` · `+0x2A` `Sounds` count (byte) · `+0x29` `IKs` count (byte) · `+0x2B` an `Anim_file`-local flag cleared before triggers/IK/sound processing (bit `0x80000000` **[OPEN — desk review 2026-09-30: §7.1 gives bit `0x40000000` for the same write; both lie outside the 30 `Flags` bits 0–29, so the spec's own numbers do not decide which is right; to be settled against the executable (the OR immediate in `0x004AEA30`'s third region)]** of `+0x2C` is later set by `anim_blend_trees.xtbl`, §7, when a matching `Blend_trees` row is found — this record is also the *target* of that table's name lookup) · `+0x2C` the 30-bit `Flags` word · `+0x38` `IKs` array pointer · `+0x3C` `Sounds` array pointer · `+0x40` cleared to 0 before the `misc` callback.

### 3.4 Runtime location **[CONFIRMED — disassembly]**

Loaded-animation records: `DAT_02EF2178`, stride `0x44`, live count `DAT_03171BE8`, capacity `0x1964`=6,500. A secondary open-chained name→record hashtable shares this array (buckets `DAT_0340B228`/chain `DAT_034117B8`/keys `DAT_03417D48`/values `DAT_0341E2D8`, generic across the group, §1.3). A separate free-list cache (`DAT_030F0C68`, stride `0x24`, cap `0x1D4C`=7,500) backs `FUN_004BEC10`/`FUN_004CA4A0`'s name→handle lookups. **[OPEN — desk review 2026-09-30: what happens when the live count reaches 6,500 (or the cache 7,500) and whether a repeated `Filename` reuses its record or takes a new slot are not stated (5,968 base rows do not reach the cap); bytes `+0x0C..+0x28`, `+0x30..+0x37` and `+0x41..+0x43` of the record are not described, and the number of `+0x00` identity dwords is not given. To be settled against the executable (`0x004AE7A0`, `0x004CA4A0`, `0x004BEC10`) and real data (duplicate `Filename` census).]**

**Review status (2026-09-30): NEEDS-EXE: blend-tree bit conflict, cap overflow and duplicate-Filename policy — desk review (not re-derived from the executable).**

### 3.5 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_files.xtbl` (2,576,431 bytes, 5,968 `<Anim_file>` rows): **5,968/5,968** rows have a non-empty `Animation/Filename` (extension distribution: **5,859 `.animx`, 109 with no extension** — those 109 are presumably resolved through a different, unexamined name-resolution path, OPEN); **6,850/6,850** `Flags/Flag` values fall inside the 30-name table above; **1,859/1,859** `IKs/IK/Location` values fall inside the 4-name table; **991/5,968** rows have a non-empty `Sounds`; **2,454/5,968** rows have a non-empty `Triggers`; **0/5,968** rows have any child under `Voice_Lines` (confirms §3.2's "authored container, structurally empty and never read" finding — this element carries zero information in the shipped game). This pass also corrected two field-name guesses made from the disassembly alone: the IK container is `<IKs>` (plural; the disassembly's literal `"IK"` at `0x012A2054` is the **per-item singular tag inside it**, not the container), and `Animation` is a **container** (`Filename`+`Preload`), not a leaf text field as the raw decompile output first suggested — corrected here before being asserted anywhere else, per this project's standing "verify before writing" discipline (`HANDOFF.md` §36 (archived §27.8)).

**[Validated by data — Team B `team-b/HANDOFF.md` §9.90: "6,850 Flags, 1,859 IK locations" and "`anim_files.xtbl` Voice_Lines present 5,968/5,968 with 0 carrying a child", reproduced over all 38 real archives. Team B does not report the 5,859/109 extension split, the 991 or the 2,454 figures, so those remain Team A empirical only.]**

**Review status (2026-09-30): VALIDATED-BY-DATA: 5,968 rows, 6,850/6,850 Flags, 1,859/1,859 IK locations, Voice_Lines 0 (Team B §9.90) — desk review (not re-derived from the executable).**

## 4. `anim_groups.xtbl` — group name registry with parent links

**Loader:** slot 4 of the registry (§1.4), read by `FUN_004CCCC0`. **[CONFIRMED — disassembly.]** This is the array `spec-tables-weapons-combat.md` §1.6 already cites (`DAT_03519838`, "animation-group name array, count `DAT_03171C1C`") as a cross-table resolver used by the weapons/combat group; this section is its own loader trace.

### 4.1 Element tree and fields **[CONFIRMED — disassembly]**

Root repeats `<Anim_Group>`. Fields:
- **`Name`** (text, `0x0129EE6C` literal — the same shared "Name" address used everywhere in this project): hashed (`FUN_00D9E8B0`, seed 0) and stored as the row's key in `DAT_03519838` (array of name pointers, count `DAT_03171C1C`, a copy of registry header field `DAT_03519834`).
- **`Parent`** (text, optional): looked up by `_stricmp` against the same `Name` array; if found, the **matching row's own parent-record slot is set to point at the parent's record** — i.e. `Parent` names another `<Anim_Group>` row by its `Name`, building a tree. Stored in a second array `DAT_03171C24` (stride `0xC`=12 bytes; only the first dword — the parent pointer — is populated by this reader, the other two dwords of the stride are left as the group's slot for other subsystems, not written here).

### 4.2 Runtime location **[CONFIRMED — disassembly]**

Name array: `DAT_03519838` (pointers), count `DAT_03171C1C` (mirrors registry header `DAT_03519834`). Parent-link array: `DAT_03171C24`, stride `0xC`, same count. **Name resolver:** `FUN_004CCE70(index)` → name pointer (bounds-checked); the reverse (name→index) is the same `_stricmp` linear scan used internally by the loader and by `spec-tables-weapons-combat.md` §1.6's citation.

### 4.3 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_groups.xtbl` (2,356 bytes): **33 `<Anim_Group>` rows**, every row has a `Name`, and **32/32** rows carrying a `Parent` value resolve to another row's `Name` in the same file (case-insensitive) — confirming both the element grammar and the parent-resolution mechanism against real authored data with zero unresolved parents.

**[OPEN — desk review 2026-09-30: §4.1 says `Name` is "hashed and stored as the row's key" but also that `DAT_03519838` is an array of name pointers; whether a hash, a pointer or both is stored is not stated. `DAT_03519838` is at +0x1008 of the `0x1048`-byte registry block (§1.4), with `DAT_03519840` 8 bytes later, so it can only be a pointer to the array, not the array itself. Also not stated: which value the parent slot receives (pointer, record address or index), who writes `DAT_03519834` before the loader runs, and whether a `Parent` naming a later row resolves. To be settled against the executable (`0x004CCCC0`, `0x004CCE70`, the writer of `DAT_03519834`) and real data (parent-before-child order over the 33 rows).]**

**Review status (2026-09-30): NEEDS-EXE: key vs pointer storage, parent-slot value, and name-array location — desk review (not re-derived from the executable).**

## 5. `anim_states.xtbl` and `anim_actions.xtbl` — a merged name registry, honestly not more

**Loader:** slots 2 and 3 of the registry (§1.4), both read by the **same function**, `FUN_004BF8D0`. **[CONFIRMED — disassembly.]**

### 5.1 What the reader does **[CONFIRMED — disassembly]**

`FUN_004BF8D0` opens `anim_states.xtbl`, reads every root-level `<State>` element's **`Name`** text only (`0x0129EE6C`), then opens `anim_actions.xtbl` and reads every `<Action>` element's **`Name`** text only, merging both into **one shared name/hash table** alongside a small compiled-in preset list (`DAT_03519840`/`DAT_03519844`). The merged table (`DAT_03171C10`, stride `0x10`=16 bytes: `+0x00` name pointer, `+0x04` CRC-32 hash, `+0x08`/`+0x0C` left zeroed for file-sourced entries) is exactly the registry `FUN_004BF810` (§1.3) looks up — the lookup `anim_transitions.xtbl` (§6), `creation_lipsync_animations.xtbl` (§13) and `anim_flinches.xtbl` (§10) all use. Total count: `DAT_03171C08`.

**No other element of `<State>` or `<Action>` is read by any function this project has found.** This is not an assumption from an incomplete search: `AfRangeRefs` over the entire `0x1048`-byte registry block (§1.4) found exactly one consumer of each slot's address (`FUN_004BF8D0`, both slots), and that function's full body (1,101 instructions) was decompiled and read in full — it never looks up a second child name inside either `<State>` or `<Action>`.

### 5.2 Element tree

```
anim_states.xtbl:   <root><Table><State><Name>…</Name></State>…</Table></root>
anim_actions.xtbl:  <root><Table><Action><Name>…</Name><_Editor>…</_Editor></Action>…</Table></root>
```

`_Editor` is the standard per-row editor-metadata sibling this project already established is never visited by any row reader (`spec-tables-weapons-combat.md` §1.2).

### 5.3 Validation against real base-game rows **[CONFIRMED — empirical]** — closes the question rather than leaving it as a reader-side negative

This is the one place in this document where the base-game data was checked not just to confirm the reader's behaviour but to settle whether the reader is *leaving data on the table*. `misc_tables.vpp_pc`:

- `anim_states.xtbl` (56,793 bytes): **1,220 `<State>` rows**, and the **complete element vocabulary across all 1,220 rows is `{Name: 1220}`** — no row anywhere in the shipped table carries any child other than `Name`.
- `anim_actions.xtbl` (284,201 bytes): **2,347 `<Action>` rows**, vocabulary **`{Name: 2347, _Editor: 2347}`** — again nothing beyond `Name` (and the universally-ignored editor metadata).

**So the "name-only" finding is not a gap in this pass's trace — the authored data itself carries no other fields.** `anim_states.xtbl` and `anim_actions.xtbl` are, both by design and by what the engine reads, flat name registries that other tables' fields point into by string (`From_State`/`To_State`/`Action` in §6, `Animation` in §13, `Name` in §10) — the actual per-state *behaviour* (durations, blend curves, transition conditions) lives entirely in the tables that reference these names, principally `anim_blend_trees.xtbl` (§7) and `anim_transitions.xtbl` (§6), not in `anim_states.xtbl`/`anim_actions.xtbl` themselves.

**[OPEN — desk review 2026-09-30: the registry indices are the stable keys used by §6, §10 and §13 and by other specs, but this text does not give the compiled-in preset count or contents, whether presets come before or after the file entries, the registry capacity, or what happens to duplicate names within or across the two files or to an empty `Name`; nor why a CRC-32 is stored at `+0x04` when `FUN_004BF810` looks up by `_stricmp`. To be settled against the executable (`0x004BF8D0`, `DAT_03519840/44`) and real data (name overlap and case-variant duplicates between states and actions).]**

**Review status (2026-09-30): NEEDS-EXE: preset list, merge order, capacity and duplicate policy (element vocabulary agrees with Team B's reader) — desk review (not re-derived from the executable).**

## 6. `anim_transitions.xtbl` — the state-machine transition table, and a base-game surprise

**Loader:** slot 10 of the registry (§1.4); read by `FUN_004AEA30`'s first code region (the same giant function that also reads `anim_set_filenames.xtbl`, §2, and `anim_blend_trees.xtbl`, §7, in later regions). **[CONFIRMED — disassembly, traced instruction-by-instruction — this was the one table in the group whose mechanism needed the annotated disassembly rather than the decompiler's C output to resolve correctly, §6.1.]**

### 6.1 Element tree and fields **[CONFIRMED — disassembly]**

Root repeats `<Anim_Transition>`. Fields, each resolved against the merged State/Action registry (`FUN_004BF810`, §1.3/§5):
- **`From_State`** (text) → index.
- **`To_State`** (text) → index.
- **`Action`** (text) → index.

### 6.2 Runtime storage and a genuine single-slot-per-state finding **[CONFIRMED — disassembly]**

Transitions are stored in `DAT_02EA4588`, an array of `From_State`-count 8-byte entries (`{To_State index, Action index}`), **directly indexed by the `From_State` index** — not hashed, not a list. The write is guarded by exactly one free-slot check (`if DAT_02EA4588[From_State] is already occupied (≠ -1), the row is silently dropped and processing moves to the next `<Anim_Transition>` element; nothing is appended, no error is raised`). **This means the table can hold at most one transition rule per distinct `From_State` value** — a second `<Anim_Transition>` row sharing an already-used `From_State` is authored but has no effect. This was checked by reading the exact instruction sequence (evidence dump, not in repo), not inferred from the decompiler's higher-level (and, on a first pass, more ambiguous) C rendering of the same code.

### 6.3 A base-game surprise: the shipped `<Table>` is empty **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_transitions.xtbl` (6,970 bytes): the document's `<root><Table>` element is **structurally empty** (`<Table>\r\n\t</Table>`) — **every authored `<Anim_Transition>` row in the shipped file lives under the sibling `<TableTemplates>` element instead.** Per `spec-tables-weapons-combat.md` §1.2's documented behaviour (confirmed again by this group, §1.2), the loader `FUN_00DAC9A0` specifically returns the **`Table`** child of the root and row readers never visit `TableTemplates`. **The direct, checkable consequence is that this base-game table contributes zero runtime transitions** — the single-slot-per-`From_State` mechanism in §6.2, however real in the code, is exercised by nothing in the shipped base game from this file. **[OPEN — desk review 2026-09-30: the array length (state count or registry count `DAT_03171C08`), who fills it with -1 and when, and what happens when any of the three names does not resolve (an index of -1 used as an array index) are not stated; to be settled against the executable (`0x004AEA30` first region).]** (Whether a DLC or patch archive supplies a populated `<Table>` for this file was not checked in this pass — `patch_compressed.vpp_pc` was scanned for the filename in §17's location pass and this table was found *only* in `misc_tables.vpp_pc`.) This is reported as found, not smoothed over: the mechanism is real and traced from disassembly; its exercise by real data is what turned out to be empty.

**[Validated by data — Team B `team-b/HANDOFF.md` §9.90: "`anim_transitions.xtbl` 0 in `<Table>` / 23 in `<TableTemplates>`" across all 38 real archives. This agrees with the empty `<Table>` above and adds the row count (23). It is an aggregate over the copies found, so it shows that no copy among the 38 archives supplies a populated `<Table>`.]**

**Review status (2026-09-30): NEEDS-EXE: array size and -1 handling (empty `<Table>` agrees with Team B, 0 / 23) — desk review (not re-derived from the executable).**

## 7. `anim_blend_trees.xtbl` — 1D blend-space definitions attached to loaded-animation records

**Loader:** slot 11 of the registry (§1.4); the third and last code region of `FUN_004AEA30` (§2, §6). **[CONFIRMED — disassembly, every tag name read directly off the annotated instruction operands, not inferred.]**

### 7.1 Root and row binding **[CONFIRMED — disassembly; the flag bit number is OPEN]**

Root child fetched by name is **`Blend_trees`** (`0x012A21C8`) — and, unusually, **the per-row tag repeats the same name** (`<Table><Blend_trees>…</Blend_trees><Blend_trees>…</Blend_trees>…</Table>`; confirmed against real data, §7.4): the first row comes from "first child named `Blend_trees`", every next row from "next sibling named `Blend_trees`" (`FUN_00DAB9F0`).

Each row's **`Name`** is hashed (CRC-32, seed 0) and looked up in the **generic loaded-animation hashtable** (§1.3) — the *same* lookup `anim_files.xtbl` records live in (§3), *not* a lookup local to this table. **If the name is not found, the entire row is skipped** (no data is stored anywhere for it). If found, the target `Anim_file` record's `+0x2C` flags word gets bit `0x40000000` **[OPEN — desk review 2026-09-30: §3.3 gives bit `0x80000000` for the same write; to be settled against the executable (the OR immediate in `0x004AEA30`'s third region)]** OR'd in — **a `Blend_trees` row attaches a blend-tree definition onto an *existing* `anim_files.xtbl` record by name; it does not define an independent animation.**

### 7.2 Per-row fields **[CONFIRMED — disassembly; the control-point cap vs the `State` stride is OPEN, see §7.3]**

- **`Control_input_low`** (f32, "if present") · **`Control_input_high`** (f32, "if present") · **`Ramp_speed`** (f32, "if present") — a ramped 1D control-input axis (matches this pass's independent finding of a `"Ramped input"` flag literal used elsewhere in the same function's neighbouring code, which this document's search confirmed belongs to a *different*, out-of-scope table — see §1.1's note on `FUN_004AEA30`'s other regions).
- **`States`** (container) → repeated **`State`**, capacity unbounded by count but the array is sized exactly to the counted total (two-pass: count then allocate then fill), stride `0x3C`=60 bytes. Each `<State>`:
  - **`Animation`** (via the `Filename`-getter `FUN_00DAC880`, so really `<Animation><Filename>…`, matching `anim_files.xtbl`'s own `Animation/Filename` shape, §3.2) → resolved against the **same loaded-animation hashtable** again; unresolved → the `State` entry is written with its animation-index field defaulted to `-1` and is otherwise kept (unlike an unresolved row `Name`, an unresolved `State/Animation` does **not** skip the whole row).
  - **`Blend_time`** (f32, "if present").
  - **`Control_points`** (container) → repeated **`Control_point`**, capacity **10** per `State` (a fixed cap observed directly in the allocation/indexing code, not inferred) **[OPEN — desk review 2026-09-30: conflicts with the `0x3C` `State` stride, see §7.3]**, each: **`Range`** (f32, "if present") and **`Value`** (f32, "if present") — a `(Range, Value)` pair, the classic shape of a 1D blend curve control point (e.g. speed→blend-weight for locomotion blending).

### 7.3 Record layout (per `State` entry, stride `0x3C`) **[CONFIRMED — disassembly for `+0x00`..`+0x10`; the inline control-point storage is OPEN]**

`+0x00`/`+0x04` a copy of the resolved animation record's own identity fields · `+0x08` the animation's **index** into `DAT_02EF2178` (computed as `(record_ptr − DAT_02EF2178) / 0x44`, range-checked, `-1` on failure or on an unresolved `Animation`) · `+0x0C` `Blend_time` · `+0x10` count of `Control_point` entries · beyond `+0x10`, up to 10 `(Range, Value)` float pairs. **[OPEN — desk review 2026-09-30: these numbers do not tile. The header through the `+0x10` count is `0x14` bytes and 10 pairs × 8 bytes = `0x50`, so 10 inline pairs need `0x14 + 0x50 = 0x64` bytes, but the stride is `0x3C`; only `(0x3C − 0x14) / 8 = 5` pairs fit. At least one of the stride `0x3C` (§7.2), the cap 10 (§7.2), the 8-byte pair or inline storage is wrong, and the spec's own numbers do not say which. Also not stated: where the row-level `Control_input_low`/`high`, `Ramp_speed` and the `States` array pointer/count are stored. To be settled against the executable (`0x004AEA30` third region: allocation size, cap compare, row-level stores) and real data (maximum `Control_point` count per `State`).]**

### 7.4 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_blend_trees.xtbl` (72,010 bytes): **31 `<Blend_trees>` rows** (confirming the per-row tag repeats the root-fetch name); **111/111 `States/State` entries have both `Animation` and `Blend_time`**; **353/353 `Control_points/Control_point` entries have both `Range` and `Value`** — the full element grammar, including the container/singular-vs-plural tag pairing, validates cleanly against every sampled instance. **[Desk review 2026-09-30: this checks element presence only; whether any of the 31 `Blend_trees/Name` or 111 `State/Animation` values actually resolves against `anim_files.xtbl`'s `Filename` was not checked (if none resolve, the table is a runtime no-op like §6). To be settled against real data.]**

**Review status (2026-09-30): NEEDS-EXE: `State` stride vs control-point cap, flag bit, destination of row-level fields — desk review (not re-derived from the executable).**

## 8. `anim_ik_situation.xtbl` — IK situation records, with a hard-coded 34-entry preset block

**Loader:** slot 8 of the registry (§1.4); read by `FUN_004AE390`. **[CONFIRMED — disassembly.]** This is the table `anim_files.xtbl`'s `<IK>` items' `Situation` field resolves against (§3.2).

### 8.1 Compiled-in presets **[CONFIRMED — disassembly for the 34-entry preset table; when presets are written relative to file rows is OPEN]**

Before the file is opened, the reader fills slots `0..0x21` (34 entries) of its runtime array from a **compiled-in literal name table** (`0x01350718`, e.g. `Two_Handed_Weapon`, …), each with `Flags` defaulted to bit 0 set (`Hard Coded`, §8.2) — these are not authored in the `.xtbl` at all. **[OPEN — desk review 2026-09-30: "before the file is opened" conflicts with §8.2's "the next row (or preset, at load's end) overwrites it". The count starting at 34 (§8.4) fits presets-first, but the spec's numbers do not rule out a second preset write at load's end; the slot a given `Name` gets depends on this. To be settled against the executable (`0x004AE390` control flow).]**

### 8.2 Element tree and fields **[CONFIRMED — disassembly; `Prop_Name`'s hash algorithm and the preset-overwrite order are OPEN]**

Root repeats `<IK_Situation>`:
- **`Name`** (text) → hashed (CRC-32, seed 0) into the record.
- **`Prop_Name`** (text, optional) → hashed with a **different** hash function, `FUN_00DB1510` (not the group's usual `FUN_00D9E8B0`/CRC-32 — this function was not independently identified elsewhere in this pass; noted as a distinct hash, OPEN on its exact algorithm).
- **`Morphed_Surface_Max_Adjustment`** (f32, "always" reader).
- **`Flags`** → repeated `Flag`, tested against a **fixed 3-entry table** (`0x013507A0`): `Hard Coded`(bit 0) · `Two Person`(bit 1) · `Morphed Surface`(bit 2).

**Row-vs-preset interaction:** every row read from the file is written into the next free slot after the 34 presets; if its own `Flags` include bit 0 (`Hard Coded`), the slot is **not** kept — the next row (or preset, at load's end) **[OPEN — desk review 2026-09-30: conflicts with §8.1's presets-first order; see the note there]** overwrites it. In effect, **rows flagged `Hard Coded` in the authored file are discarded** at this stage (their data is momentarily written then immediately overwritten), while non-`Hard Coded` rows are kept and grow the array past the 34 presets.

### 8.3 Record layout (stride `0x10`=16 bytes) **[CONFIRMED — disassembly]**

`+0x00` `Name` hash · `+0x04` `Prop_Name` hash (0 if absent) · `+0x08` `Morphed_Surface_Max_Adjustment` (f32, unspecified if absent per the group's "always"-reader convention, `spec-tables-weapons-combat.md` §1.3) · `+0x0C` `Flags` (3-bit).

### 8.4 Runtime location **[CONFIRMED — disassembly]**

`DAT_034248C8` (name-hash column), `DAT_034248CC` (`Prop_Name` hash column), `DAT_034248D0` (`Morphed_Surface_Max_Adjustment` column), `DAT_034248D4` (`Flags` column) — four parallel columns, stride `0x10` between corresponding entries, count `DAT_03171BA8` (starts at 34 from the presets).

### 8.5 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_ik_situation.xtbl` (18,508 bytes): **46 `<IK_Situation>` rows**, and **20/20** `Flags/Flag` values fall inside the 3-name table (`Hard Coded`/`Two Person`/`Morphed Surface`) with no unrecognised strings found.

**Review status (2026-09-30): NEEDS-EXE: preset write order (§8.1 vs §8.2), `FUN_00DB1510` hash, Hard Coded discard path — desk review (not re-derived from the executable).**

## 9. `anim_triggers.xtbl` — a flat, bounded trigger-name registry

**Loader:** slot 6 of the registry (§1.4); read by `FUN_004B15A0` → `FUN_004CA2D0`. **[CONFIRMED — disassembly.]** This is the registry `anim_files.xtbl`'s `<Triggers><Trigger>` elements validate against (§3.2).

### 9.1 Element tree and fields **[CONFIRMED — disassembly; truncate vs drop for names of 32+ characters is OPEN]**

Root repeats `<Trigger>`, each carrying only a **`Name`** (`0x0129EE6C`), read with a **bounded copy of at most 31 characters + NUL** (`FUN_00DABA70(dest, 0x20, …)`) **[OPEN — desk review 2026-09-30: a copy bounded to 31 characters + NUL always yields a length < 0x20, so the length guard in the next sentence could never reject; truncation and dropping exclude each other]**. Names ≥ 32 characters or once the registry's 110-entry cap is reached are silently dropped (`FUN_004CA2D0`'s own guard: length `< 0x20` **and** count `< 0x6E`) **[OPEN — desk review 2026-09-30: conflicts with the bounded copy in the previous sentence. Whether over-long names are truncated or dropped, and whether the hash is taken over the full or the truncated text, is to be settled against the executable (`0x004CA2D0`, `0x004B15A0`). Base data is unaffected (105/105 names are shorter than 32).]**

**Hash:** the **rotate/XOR hash `FUN_00DA7890`** (§1.3), not this group's usual CRC-32 — the same hash `anim_files.xtbl`'s trigger validator computes over its own `<Trigger>` text before comparing.

### 9.2 Runtime location **[CONFIRMED — disassembly]**

Hash array `DAT_03132B18`, name array `DAT_03132CD0` (stride `0x20`=32 bytes, matching the 31-char+NUL bound), count `DAT_02F5E00C`, capacity `0x6E`=110.

### 9.3 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_triggers.xtbl` (13,050 bytes): **105 `<Trigger>` rows** (under the 110 cap), and **105/105** `Name` values are shorter than the 32-byte buffer bound — no truncation or drop would occur on this data. **[Validated by data — Team B `team-b/HANDOFF.md` §9.90: "105/110 triggers" over all 38 real archives. The arrays tile: `0x03132B18 + 110 × 4 = 0x03132CD0`.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (truncate vs drop NEEDS-EXE; 105/110 agrees with Team B) — desk review (not re-derived from the executable).**

## 10. `anim_flinches.xtbl` — hit-reaction selection records

**Loader:** slot not in the 16-slot registry (§1.4) — `anim_flinches.xtbl` is opened directly by its own literal at `FUN_0097DDB0`. **[CONFIRMED — disassembly.]**

### 10.1 Element tree and fields **[CONFIRMED — disassembly]**

Root repeats `<Flinch>`, capacity **149** (`0x95`, checked and enforced — the loader returns early once the cap is hit):
- **`Name`** (text) → resolved via `FUN_004BF810` (§1.3) against the **merged State/Action registry** (§5) — i.e. a flinch's `Name` is expected to also be a known state or action name.
- **`Hit_Location`** (text, if/else literal chain, unrecognised text leaves the field at its zero default): `Head`→6 · `Chest`→2 · `Groin`→3 · `Pelvis`→0xB.
- **`Hit_Direction`** (text, 4-entry literal table `0x01173084`): `front`→0 · `right`→1 · `back`→2 · `left`→3.
- **`Type`** (text, if/else literal chain): `Normal`→1 · `Heavy`→2 · `Death`→4 · `"Death Heavy"`→8 · `Base`→0x10 · `Riot1`→0x20 · `Riot2`→0x40.
- **`Flags`** → repeated **`Flag`**, tested against **only three** literals: `Crouch`(bit 0) · `Cover`(bit 1) · `Skydive`(bit 2).

**[OPEN — desk review 2026-09-30: not stated: whether a row whose `Name` does not resolve (-1) is kept or skipped; what unrecognised `Hit_Direction` or `Type` text does (an unmatched `Hit_Direction` left at zero would read as `front`); whether the text compares are case-sensitive; that `+0x07` and `+0x09..+0x0B` of the record are padding. To be settled against the executable (`0x0097DDB0`) and real data (exact-case match census).]**

### 10.2 Record layout (stride `0xC`=12 bytes) **[CONFIRMED — disassembly]**

`+0x00` `Name` index (into the merged State/Action registry, dword) · `+0x04` `Hit_Location` (byte enum) · `+0x05` `Hit_Direction` (byte enum) · `+0x06` `Type` (byte bitmask) · `+0x08` `Flags` (byte bitmask, `Crouch`/`Cover`/`Skydive`).

### 10.3 Runtime location **[CONFIRMED — disassembly]**

`DAT_0262B2E8` (base of the stride-`0xC` array), count `DAT_0262B9F0`, capacity 149.

### 10.4 Validation against real base-game rows, including a genuine unrecognised-value finding **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_flinches.xtbl` (46,617 bytes): **129 `<Flinch>` rows** (under the 149 cap); **129/129** `Hit_Location`, **129/129** `Hit_Direction` and **129/129** `Type` values fall inside their respective tables above. **`Flags/Flag`: 24/26 match** — the two non-matching values are both the literal text **`Male`**, which the reader's three-literal `Crouch`/`Cover`/`Skydive` test does not recognise. `Male` is therefore **authored dead data**: present in the shipped table, silently contributing nothing to the flags byte at load time (the same "authored but unrecognised" pattern `spec-vehicle-data.md` §7.7 already documented for other tables in this project, e.g. `Foley`'s `Min_Speed`/`Max_Speed`/`Modifier` children). **[Validated by data — Team B `team-b/HANDOFF.md` §9.90: "129/149 flinches" and the `Male` ×2 unmatched flag text over all 38 real archives (Team B's matcher is case-insensitive, so exact case is not covered).]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (OPEN notes for unresolved-Name and unmatched-text paths; row count and `Male` ×2 agree with Team B) — desk review (not re-derived from the executable).**

## 11. `anim_synced.xtbl` — two-character synchronized-move records

**Loader:** not in the 16-slot registry — opened directly by its own literal, `FUN_0095DE40`, which delegates the detailed per-row read to `FUN_0095DAA0`. **[CONFIRMED — disassembly.]**

### 11.1 Two-pass, refresh-aware loading **[CONFIRMED — disassembly]**

`FUN_0095DE40` takes a `refresh` byte parameter (the standard first-load-vs-refresh idiom, `spec-tables-weapons-combat.md` §1.5 item 2): on first load it builds a name/hash index (`DAT_02624B50`, stride `0x30`=48 bytes) over every `<SyncedMove>`'s `Name`, then calls the detailed reader `FUN_0095DAA0` **unconditionally on every row**; on a refresh it instead hashes the incoming row's `Name`, finds the matching existing slot in the index, and re-reads **only that one row**.

### 11.2 Element tree and fields (from `FUN_0095DAA0`) **[CONFIRMED — disassembly]**

Root repeats `<SyncedMove>`:
- **`AttackerAnim`** (via the `Filename`-getter `FUN_00DAC880`) → resolved through the loaded-animation lookup (`FUN_004BF650`/`FUN_004BF610`, §1.3) to an index into `anim_files.xtbl`'s record array; default `-1`.
- **`VictimAnim`** (same getter/resolution) → default `-1`.
- **`VictimOffsets`** (container) → **`XOffset`** (f32, "always") · **`YOffset`** (f32, **"if present"** — the only field in this row that differs in presence-behaviour from its siblings) · **`ZOffset`** (f32, "always") · **`heading`** (f32, "always").
- **`Flags`** (container) → repeated **`Flag`** (`0x0129EA94`), each child's **full text** compared verbatim (not a short identifier, unlike every other `Flag`-list in this group) against ten fixed phrases, building a 10-bit mask: `"victim is object/prop"`(0) · `"attacker use weapon"`(1) · `"victim use weapon"`(2) · `"attacker doesn't correct"`(3) · `"victim doesn't correct pos"`(4) · `"attacker is brute"`(5) · `"victim is brute"`(6) · `"attacker is avatar"`(7) · `"victim is avatar"`(8) · `"hold last frame"`(9).

**[Note — desk review 2026-09-30: the row's `Name` is not listed above because `FUN_0095DAA0` does not read it, but it is read: `FUN_0095DE40` hashes every `<SyncedMove>`'s `Name` into the index (§11.1) and stores it at `+0x10` (§11.3), so `Name` is the table's key. Team B's `team-b/HANDOFF.md` §9.90 also reports direct-child `XOffset`/`ZOffset` on 224 of 349 rows (outside `VictimOffsets`), plus `Name` ×349, `_Editor` ×349 and `Notes` ×28.]** **[OPEN — desk review 2026-09-30: whether any reader uses the direct-child `XOffset`/`ZOffset`, what `+0x04` holds when `YOffset` is absent, and whether the phrase compares are case-sensitive are to be settled against the executable (`0x0095DAA0`, `0x0095DE40`, `0x0095DA50`) and real data (direct vs nested X/Z equality).]**

### 11.3 Record layout (stride `0x30`=48 bytes) **[CONFIRMED — disassembly, dword offsets]** **[Desk review 2026-09-30: the offsets below are byte offsets (dword-aligned); `+0x0C` and `+0x24..+0x2F` are not described.]**

`+0x00`/`+0x04`/`+0x08` `VictimOffsets` X/Y/Z · `+0x10` `Name` hash (index key, written during the first-pass index build) · `+0x14` `AttackerAnim` loaded-animation index · `+0x18` `VictimAnim` loaded-animation index · `+0x1C` `heading` · `+0x20` the 10-bit `Flags` mask.

### 11.4 Runtime location **[CONFIRMED — disassembly]**

`DAT_02624B50`, stride `0x30`, count `DAT_02624B54`.

### 11.5 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_synced.xtbl` (244,821 bytes): **349 `<SyncedMove>` rows**; **349/349** carry a `VictimOffsets` with `XOffset`/`ZOffset`/`heading` all present; **472/472** `Flags/Flag` text values match one of the ten fixed phrases exactly (no unrecognised phrase found in this table, unlike §10's `anim_flinches.xtbl`).

**Review status (2026-09-30): NEEDS-EXE: direct-child offsets, `YOffset` default, unused record bytes (`Name` key note added) — desk review (not re-derived from the executable).**

## 12. `anim_correction_offsets.xtbl` — per-animation position correction, keyed by filename

**Loader:** not in the 16-slot registry — opened directly by its own literal, `FUN_009580F0`. **[CONFIRMED — disassembly.]**

### 12.1 A generic row walk, not a named-tag lookup **[CONFIRMED — disassembly, empirically confirmed §12.4]**

Unlike every other table in this group, this reader does **not** ask for a row by tag name — it walks the `<Table>` element's children directly via the raw node-model `next-sibling`/`first-child` pointers (`spec-tables-weapons-combat.md` §1.2's node layout), accepting whatever tag each child happens to have. Real data (§12.4) shows the authored tag is `<Correction_Offset>`, but the reader itself is tag-agnostic.

### 12.2 Element tree and fields ~~**[CONFIRMED — disassembly]**~~ **[CONFIRMED — disassembly for the reads; the stored 4-float value is OPEN]**

Per row:
- **`File`** (container, via the `Filename`-getter `FUN_00DAC880` with child name `"File"`, so really `<File><Filename>…`) → resolved through the loaded-animation lookup (`FUN_004BF650`/`FUN_004BF610`, §1.3) to an index into `anim_files.xtbl`'s record array; **a row whose `File` doesn't resolve is skipped entirely.**
- **`Offset`** (vec3 `X`/`Y`/`Z`, "always" reader `FUN_00DACF60`, pre-seeded from a compiled-in default `DAT_029CDB98`/`9C`/`A0` rather than zero).
- A second, 4-float block is then derived by a helper (`FUN_00DA38E0`) that was not fully traced in this pass — **OPEN: whether this is a rotation quaternion built from a further (unidentified) element on the same row, or purely a function of the `Offset` just read, was not resolved.** What is confirmed is that these 4 floats, not the raw `Offset` vec3, are what gets stored into the runtime table.
- A row's target animation gets **at most one** correction record: a per-animation byte-indexed slot allocator (`FUN_00957520`, capacity 255, keyed by the same animation index `File` resolved to) reuses the slot on a repeat. **[OPEN — desk review 2026-09-30: whether the first or the last repeated row wins, and why a byte-indexed allocator holds 255 rather than 256 slots, are not stated; to be settled against the executable (`0x00957520`, `0x009580F0`, `0x00DA38E0`, defaults at `0x029CDB98`/`9C`/`A0`) and real data (duplicate `File/Filename` among the 23 rows).]**

### 12.3 Runtime location **[CONFIRMED — disassembly]**

`DAT_01309940`, stride `0x10`=16 bytes (the 4 derived floats), indexed by the small per-animation slot from `FUN_00957520` (capacity 255, distinct from and much smaller than the main loaded-animation array).

### 12.4 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `anim_correction_offsets.xtbl` (7,361 bytes): **23 rows**, all tagged **`<Correction_Offset>`** (confirming §12.1's "tag-agnostic reader, single authored tag" reading); **23/23** rows have both a `File` and an `Offset/X` child. Sample row:

```
<Correction_Offset><Name>fence 05m</Name>
  <Offset><X>0.0</X><Y>-0.8185</Y><Z>-0.510</Z></Offset>
  <_Editor><Category>Entries</Category></_Editor>
  <File><Filename>plym_fnce_half_entr.animx</Filename></File>
</Correction_Offset>
```

The row also carries a **`Name`** element (a human-readable label, e.g. `"fence 05m"`) that is never read by the traced code — consistent with the generic child-walk in §12.1 not looking up any tag by name, including `Name` itself.

**Review status (2026-09-30): NEEDS-EXE: stored 4-float derivation and repeat policy — desk review (not re-derived from the executable).**

## 13. `creation_lipsync_animations.xtbl` — character-creation idle lipsync chances

**Loader:** not in the 16-slot registry — opened directly by its own literal, `FUN_008314A0`. **[CONFIRMED — disassembly.]**

### 13.1 Element tree and fields **[CONFIRMED — disassembly]**

Root repeats `<Entry>`, capacity **7** (checked and enforced):
- **`Name`** (text, heap-duplicated) → record identity.
- **`Chances`** (container) → count of **`Chance`** children (`FUN_00DC5150`, the shared "count of children of a name" primitive, `spec-tables-weapons-combat.md` §1.3) and an allocated array sized to that count, then filled by a second pass over the same children:
  - **`Trigger`** (text) → the **audio middleware's string→event-id function `FUN_00462960`** (via the one-line wrapper `FUN_0070A2F0`), confirming `spec-tables-weapons-combat.md` §1.4's citation of this function from a second, independent call site. **If `Trigger` doesn't resolve to a non-zero event id, the rest of the `Chance` entry (`Variant`/`Animation`) is not read and the array slot is not advanced** — a subsequent `Chance` would overwrite it. **[OPEN — desk review 2026-09-30: whether the stored `Chance` count (`+0x04`, §13.2) is the child count or the number of entries kept after this skip is not stated; if it is the child count, tail entries are left unset. What `Variant` holds when absent is also not stated. To be settled against the executable (`0x008314A0`, `0x00462960`).]**
  - **`Variant`** (u16, "if present" — `FUN_00DABF20`).
  - **`Animation`** (text) → resolved via `FUN_004BF810` (§1.3) against the **merged State/Action registry** (§5) — **not** the loaded-animation-file lookup used elsewhere in this group, despite the element's name.

### 13.2 Record layout **[CONFIRMED — disassembly]**

`Entry`, stride `0xC`=12 bytes: `+0x00` `Name` (duplicated string pointer) · `+0x04` `Chance` count · `+0x08` `Chance` array pointer.
`Chance`, stride `0xC`=12 bytes: `+0x00` `Trigger` audio-event id · `+0x04` `Variant` (u16) · `+0x08` `Animation` index (into the State/Action registry).

### 13.3 Runtime location **[CONFIRMED — disassembly]**

`DAT_022FFDB8` (`Name`), `DAT_022FFDBC` (`Chance` array pointer), `DAT_022FFDC0` (`Chance` count) — three parallel columns, stride `0xC` between entries; overall count `DAT_023008B0`, capacity 7.

### 13.4 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `creation_lipsync_animations.xtbl` (5,394 bytes): **6 `<Entry>` rows** (under the 7 cap); **24/24** `Chance` children have a `Trigger`. **[Desk review 2026-09-30: Team B `team-b/HANDOFF.md` §9.90 reports "6/7 lipsync", agreeing with the row count. Whether the 24 `Trigger` names resolve to audio events and the 24 `Animation` names exist in the State/Action registry was not checked.]**

**Review status (2026-09-30): NEEDS-EXE: stored `Chance` count semantics on the unresolved-trigger path — desk review (not re-derived from the executable).**

## 14. `character_visemes.xtbl` — facial viseme → morph-target weight lists

**Loader:** not in the 16-slot registry — opened directly by its own literal, `FUN_00978CA0`. **[CONFIRMED — disassembly.]**

### 14.1 Element tree and fields ~~**[CONFIRMED — disassembly]**~~ **[CONFIRMED — disassembly for the reads; the `Target_Value` scale factor and the `Target_Name` consumer are OPEN]**

Root repeats `<Viseme>` (count via `FUN_00DC5150`, array pre-allocated to the exact count):
- **`Name`** (text, heap-duplicated) → record identity, plus a CRC-32 hash (`FUN_00D9E740`, seed 0) stored alongside it.
- **`Targets`** (container) → count of **`Target`** children, array pre-allocated to that count:
  - **`Target_Name`** (text) → CRC-32 hash (`FUN_00D9E740`, seed 0) — **HIGH CONFIDENCE** this is a morph-target name meant to resolve against a character's morph-target table (`spec-morph-format.md`'s territory); the resolving consumer was not traced in this pass (OPEN).
  - **`Target_Value`** (f32, "always") → scaled by a fixed global multiplier `DAT_01117DC8` before storage; the multiplier's exact value/meaning (e.g. a percent-to-unit or degree-to-radian conversion) was not read out of the image in this pass (OPEN). **[Desk review 2026-09-30: `spec-tables-diversions.md` §1.4 lists the same constant (`_DAT_01117dc8`) as a percent → fraction multiplier for other tables; not confirmed for this caller, so still OPEN here, to be settled by reading the f32 at `0x01117DC8`.]**

### 14.2 Record layout **[CONFIRMED — disassembly]**

`Viseme`, stride `0x14`=20 bytes: `+0x00` `Name` (duplicated string pointer) · `+0x04` `Name` CRC-32 hash · `+0x08` **unwritten by this reader** (left as whatever the allocator provided — no field is assigned here) · `+0x0C` `Target` count · `+0x10` `Target` array pointer.
`Target`, stride `8` bytes: `+0x00` `Target_Name` CRC-32 hash · `+0x04` `Target_Value` (f32, scaled).

### 14.3 Runtime location **[CONFIRMED — disassembly]**

`DAT_026270E8`, stride `0x14`, count `DAT_026274A8`.

### 14.4 Validation against real base-game rows **[CONFIRMED — empirical]**

`misc_tables.vpp_pc` entry `character_visemes.xtbl` (19,133 bytes): **12 `<Viseme>` rows**; **162/162** `Target` children have both `Target_Name` and `Target_Value`. **[OPEN — desk review 2026-09-30: `DAT_026274A8 − DAT_026270E8 = 0x3C0` = 48 × `0x14`, which suggests a fixed 48-entry array, while §14.1 says the array is pre-allocated to the exact count; whether `DAT_026270E8` is a pointer cell or the inline array is to be settled against the executable (`0x00978CA0`).]**

**Review status (2026-09-30): NEEDS-EXE: scale factor value, array storage form — desk review (not re-derived from the executable).**

## 15. `anim_set_properties.xtbl` and `anim_prop_sets.xtbl` — registered, never opened

Both filenames occupy slots in the 16-slot table-dependency registry (§1.4: `anim_set_properties.xtbl` slot 7, `anim_prop_sets.xtbl` slot 9) — meaning both pass the registry-completeness check (`FUN_004C8690` confirms their name field is non-empty **[OPEN — desk review 2026-09-30: this is a read of both name fields, which §15.1 says does not happen; see the note in §15.1]**) — **but no function anywhere in the executable takes either slot's address and passes it to `FUN_00DAC9A0` (the document-open call).**

### 15.1 How this negative claim was reached ~~**[CONFIRMED — disassembly, exhaustive]**~~ **[CONFIRMED — disassembly for the literal-address range scan; exhaustiveness against indexed or copied access is OPEN]**

`AfRangeRefs` was run over the **entire** `0x1048`-byte registry memory range (`DAT_03518830`–`DAT_03519878`), returning **every** cross-reference to **every** address in that range: 52 references across 16 functions, with zero gaps (this is the same exhaustive scan §1.4's slot map is built from). Both slot 7's name field (`DAT_03518F34`) and slot 9's (`DAT_03519134`) are **absent from that list entirely** — no function reads either address at all, for any purpose, let alone to open it as a document. This is an exhaustive-scan negative, not a "didn't find a caller in a quick look" negative (`HANDOFF.md` §5's standard for a defensible absence claim). **[OPEN — desk review 2026-09-30: §1.4 and §15's opening say `FUN_004C8690` validates slots 7 and 9 as non-empty, which is a read of these two addresses, yet neither address is in the 52-reference list. Both cannot hold unless the validator indexes the block (base + N·0x100), and an indexed loop, or a reader of the copy that `spec-tables-traffic-ai.md` §22 says `0x004C8780` makes, would not appear in this scan. To be settled against the executable (`0x004C8690` loop body, `0x004C8780` copy destination, refs to the `DAT_03518830` base with indexed addressing).]**

### 15.2 What the authored data would have contained, for the record **[CONFIRMED — empirical]**

Neither table's schema was reverse-engineered from a reader (there is none), but both were extracted and their authored shape recorded for completeness, since the task asked each table's element tree be found or the absence explicitly noted:

- **`anim_set_properties.xtbl`** (`misc_tables.vpp_pc`, 5,169 bytes): **18 rows**, tag `<AnimSet>`, each with children `Name`, `Flags`, `_Editor` (the last never read by anything in this project's convention).
- **`anim_prop_sets.xtbl`** (`misc_tables.vpp_pc`, 2,170 bytes): **3 rows**, tag `<Anim_Prop_Set>`, each with children `Name`, `Props`, `_Editor`.

### 15.3 Assessment

This is the animation group's equivalent of `spec-morph-format.md` §16's "stash-only" ctors and `spec-tables-traffic-ai.md`'s `traffic_types.xtbl` (no literal at all) — an authored `.xtbl` file that the retail executable carries a name for but never reads. Unlike `traffic_types.xtbl`, these two *do* have a literal and *are* registered (they pass the registry's own completeness check), which makes the "never opened" finding more surprising and worth flagging rather than silently omitting: whatever `Name`/`Flags`/`Props` content these files hold is authored and shipped but **has zero effect on the retail game**, at least via this executable's animation subsystem.

**Review status (2026-09-30): NEEDS-EXE: the negative rests on literal-address refs; indexed or copied access not excluded — desk review (not re-derived from the executable).**

## 16. Cross-table reference map

Every arrow below was traced from disassembly (§2–§15), not inferred from field naming alone. **[Desk review 2026-09-30: overstated. The `anim_set_filenames.xtbl` row (satellite schema not traced) and the `character_visemes.xtbl` row ("presumed" target) carry OPEN parts.]**

| From table / field | Resolves against | Mechanism |
|---|---|---|
| `anim_blend_trees.xtbl` `Blend_trees/Name` | `anim_files.xtbl` loaded-animation record | `FUN_004BF650`/`FUN_004BF610`, by filename hash (§7.1) |
| `anim_blend_trees.xtbl` `State/Animation` | `anim_files.xtbl` loaded-animation record | same lookup (§7.2) |
| `anim_correction_offsets.xtbl` `File` | `anim_files.xtbl` loaded-animation record | same lookup (§12.2) |
| `anim_synced.xtbl` `AttackerAnim`/`VictimAnim` | `anim_files.xtbl` loaded-animation record | same lookup (§11.2) |
| `anim_files.xtbl` `Triggers/Trigger` (validation only, no field written) | `anim_triggers.xtbl`'s name registry | rotate/XOR hash `FUN_00DA7890` (§3.2, §9.1) |
| `anim_files.xtbl` `IKs/IK/Situation` | `anim_ik_situation.xtbl` runtime array | text match against `DAT_034248C8` (§3.2, §8.4) **[OPEN — desk review 2026-09-30: `DAT_034248C8` holds CRC-32 name hashes (§8.3), so the match is presumably a hash compare, not a text match; to be settled against the executable (`0x004AE010`)]** |
| `anim_transitions.xtbl` `From_State`/`To_State`/`Action` | merged `anim_states.xtbl`+`anim_actions.xtbl` name registry | `FUN_004BF810` (§6.1, §5.1) |
| `anim_flinches.xtbl` `Name` | merged `anim_states.xtbl`+`anim_actions.xtbl` name registry | `FUN_004BF810` (§10.1, §5.1) |
| `creation_lipsync_animations.xtbl` `Chance/Animation` | merged `anim_states.xtbl`+`anim_actions.xtbl` name registry | `FUN_004BF810` (§13.1, §5.1) |
| `anim_groups.xtbl` `Parent` | another `anim_groups.xtbl` row's `Name` | `_stricmp` self-reference (§4.1) |
| `weapons.xtbl` and other groups' animation-group refs (`spec-tables-weapons-combat.md` §1.6) | `anim_groups.xtbl`'s `DAT_03519838` array | `FUN_004CCE70`, `_stricmp` — already documented outside this group, cited here for completeness |
| `anim_set_filenames.xtbl` `Name` | a second-layer `.xtbl` file (e.g. `anim_auto.xtbl`), each presumably `Anim_Group`-tagged | filename concatenation + generic `.xtbl` open, **internal schema of the target files not traced** (§2.2) |
| `character_visemes.xtbl` `Target/Target_Name` | (presumed) a character's morph-target name table | CRC-32 hash stored, resolving consumer not traced (§14.1, OPEN) |
| `creation_lipsync_animations.xtbl` / ~~`anim_synced.xtbl`~~ `anim_files.xtbl` (`Sounds/Sound`, §3.2; `anim_synced.xtbl` §11.2 has no audio fields) `Audio_Event`/`Trigger`/`Audio_Switch_*` | Wwise audio-event ids | `FUN_00462960` (audio string→id) / `FUN_0046FD00`, per `spec-tables-weapons-combat.md` §1.4's citation (§13.1, §3.2) |

**Not cross-referenced by anything traced in this pass:** `anim_states.xtbl`/`anim_actions.xtbl` are referenced *by* five other tables (above) but reference nothing themselves beyond their own `Name` (§5). `anim_ik_situation.xtbl`, `anim_triggers.xtbl` and `anim_groups.xtbl` are each referenced by exactly one other table in this group. `anim_set_properties.xtbl`/`anim_prop_sets.xtbl` participate in no traced reference in either direction (§15).

**Review status (2026-09-30): DESK-PASS, text fixes applied (overstatement annotated; IK `Situation` match mechanism OPEN) — desk review (not re-derived from the executable).**

## 17. Validation summary

All 16 tables were located in `misc_tables.vpp_pc` (searched there plus `da_tables.vpp_pc`, `cutscene_tables.vpp_pc` and `patch_compressed.vpp_pc`; none appear in the latter three) and extracted with `tools/harnesses/vpp_modea.py`, decoding cleanly (`sz_uncomp` matched the inflated length for all 16, zero decode failures). Each was parsed with a tolerant node-tree parser written for this pass (name/next-sibling/first-child/text node model, mismatched-close-tag and stray-whitespace tolerant, per `spec-xtbl-format.md` §7's documented base-table quirks) and checked against the schema recovered from disassembly.

**51 individual pass/fail predicates were run (§2–§15) [desk review 2026-09-30: the 51 are not enumerated in this document and the harness is not in the repository, so the figure cannot be reproduced here]; 49 passed outright, and the 2 that did not are themselves confirmed, reportable findings, not parser or schema errors:**

1. `anim_transitions.xtbl`'s shipped `<Table>` element is empty; every authored row lives under `<TableTemplates>` (§6.3) — the game-side mechanism is real (traced from disassembly) but unexercised by the base game's own data.
2. `anim_flinches.xtbl` has 2 `Flag` values (`Male`) the reader's fixed three-literal test does not recognise (§10.4) — authored, shipped, silently ignored.

**Headline counts:** `anim_files.xtbl` 5,968 rows (6,850/6,850 `Flags` values valid, 1,859/1,859 `IKs` locations valid, 0/5,968 `Voice_Lines` populated); `anim_states.xtbl` 1,220 rows (100% name-only, confirming §5's reader-side finding); `anim_actions.xtbl` 2,347 rows (likewise); `anim_groups.xtbl` 33 rows (32/32 `Parent` links resolve); `anim_blend_trees.xtbl` 31 rows / 111 `State` entries / 353 `Control_point` entries (100% valid); `anim_ik_situation.xtbl` 46 rows (20/20 `Flags` valid); `anim_triggers.xtbl` 105/110 rows used; `anim_synced.xtbl` 349 rows (472/472 `Flags` phrases valid); `anim_correction_offsets.xtbl` 23 rows (100% valid, tag `Correction_Offset` confirmed); `creation_lipsync_animations.xtbl` 6/7 rows used; `character_visemes.xtbl` 12 rows; `anim_set_filenames.xtbl` 103 rows (confirmed as an `.xtbl`-file list, not a `.anim_pc` binding, refuting the task brief's leading hypothesis — §2); `anim_set_properties.xtbl` 18 rows / `anim_prop_sets.xtbl` 3 rows (both confirmed dead — §15).

Harness and extracted-table locations are recorded in §18.

## 18. Open items and artifacts

### 18.1 Open items, by table

- **§2 `anim_set_filenames.xtbl`** — the internal schema of the satellite `.xtbl` files it names (e.g. `anim_auto.xtbl`) was not traced (a full second loader trace, out of this pass's budget); the row's own `Path`/`Default_model`/`Default_rig` fields were found in real data but no consumer for them was located.
- **§3 `anim_files.xtbl`** — `Animation/Preload`'s consumer was not found despite a full read of the row reader's body (109 real rows have no `.animx` extension on `Filename` at all — an unexamined second name-resolution path); the `misc` extension-point callback's installer was not traced; `Voice_Lines` is confirmed authored-empty and unread but *why* it exists at all (a removed feature?) is unknown.
- **§6 `anim_transitions.xtbl`** — whether any DLC or patch archive ships a non-empty `<Table>` for this file was not checked (only the four archives listed in §17 were searched, and this table was found in `misc_tables.vpp_pc` only). **[Desk review 2026-09-30: Team B's run over all 38 real archives (`team-b/HANDOFF.md` §9.90) found 0 rows in `<Table>` and 23 in `<TableTemplates>` across the copies found; see §6.3.]**
- **§7 `anim_blend_trees.xtbl`** — the ramped-control-input fields (`Control_input_low`/`high`/`Ramp_speed`) were named from their literal tags but their exact consumer (how the ramp value drives blending at runtime) was not traced past the loader.
- **§8 `anim_ik_situation.xtbl`** — `Prop_Name`'s hash function `FUN_00DB1510` was not independently identified elsewhere in this project; its exact algorithm is unconfirmed (only that it differs from the group's usual CRC-32).
- **§12 `anim_correction_offsets.xtbl`** — the second 4-float block's derivation (`FUN_00DA38E0`) was not fully traced; whether it is a rotation quaternion, and whether it reads a further row element beyond `Offset`, is OPEN.
- **§14 `character_visemes.xtbl`** — the consumer that resolves `Target_Name`'s hash against an actual morph-target table (ties to `spec-morph-format.md`) was not traced; the `DAT_01117DC8` scale constant's value/meaning was not read.
- **§15 `anim_set_properties.xtbl`/`anim_prop_sets.xtbl`** — confirmed dead (exhaustive scan); no further work is expected to change this without a broader whole-binary search outside this group's scope.

### 18.2 Artifacts

- **Ghidra project copy:** `tools/gp_an1` (disposable, robocopied from `tools/ghidra_projects`; safe to delete once this document is folded in).
- **Ghidra scripts used (all pre-existing, none new):** `tools/scripts/AfStrXrefs.java`, `AfRangeRefs.java`, `AfMem.java`, `AjBoth.java`. Headless runner: `tools/run_an1.ps1` (new, follows the pattern of `tools/run_tbl3.ps1`).
- **Dumps:** (evidence dump, not in repo): filename literal census, game-init function, registry-consumer functions (including the annotated disassembly §6.1/§7 were read from), slot readers, the five standalone loaders, literal-table dumps (enum strings, flag tables).
- **Validation harness (new this pass):** `tools/harnesses/an_tolerant_parse.py` (tolerant node-tree parser per `spec-xtbl-format.md` §7), `tools/harnesses/an_find_tables.py` (locates all 16 filenames across the four mode-(a) archives), `tools/harnesses/an_extract.py` (extracts them via `vpp_modea.py` into a local folder (evidence dump, not in repo)), `tools/harnesses/an_validate.py` (the 51 pass/fail predicates of §17, re-runnable against the extracted files).
- **Extracted base-game tables:** (evidence dump, not in repo) — all 16 files, raw XML text, ~~7,361–2,576,431~~ 2,170–2,576,431 bytes each **[corrected — desk review 2026-09-30: the sizes stated in §2–§15 range from 2,170 (`anim_prop_sets.xtbl`, §15.2) to 2,576,431 (`anim_files.xtbl`, §3.5); `anim_groups.xtbl` 2,356, `anim_set_properties.xtbl` 5,169 and `creation_lipsync_animations.xtbl` 5,394 are also below 7,361]**.

### 18.3 What was not touched

`.czn_pc`'s interior (out of scope per the task brief and `HANDOFF.md` §27.3 item 2) was not touched. No mode-(a) container decompression beyond the documented `vpp_modea.py` rule was attempted. No whole-binary predicate search was used anywhere in this pass (`HANDOFF.md` §36's archived §27.4 standing caution, now kept in `WALLS.md` "Whole-binary predicate/census dead ends") — every function reached in §2–§15 was found from a filename literal, an element-name literal, or a named global already established either in this document or cited from a sibling spec.

**Review status (2026-09-30): DESK-PASS, text fixes applied (§18.2 size range corrected; repo-absent dump paths replaced; the 51 predicates not enumerated) — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): fixed 4 cross-references (§5.2 and §6.3 `spec-xtbl-format.md` §1.2 → `spec-tables-weapons-combat.md` §1.2; §6.3 "§0's location pass" → §17), corrected the §16 audio-id row's table name (`anim_synced.xtbl` → `anim_files.xtbl` `Sounds/Sound`, struck in place), and reworded 1 pseudocode-shaped call (§3.2 `misc` callback).
- 2026-09-30 (cloud, self-containment pass): restated 1 load-bearing HANDOFF/WALLS-only facts inline (§1.3: the rotate-6/XOR hash's ≈18-call-site breadth, cited to `spec-extensionless-types.md` §4); repointed 4 `HANDOFF.md` §27.x references to the archived headings (§27.2 ×2 → §31/§24, §27.8 → §36, §27.4 → §36 + `WALLS.md`); 0 left (see review).
- 2026-09-30 (cloud format desk review, `review/adv_tables-animation.md`): added a review status summary and 22 unit status lines (DESK-PASS 1, DESK-PASS with text fixes 6, NEEDS-EXE 13, NEEDS-DATA 1, VALIDATED-BY-DATA 1); marked OPEN at both places 6 internal contradictions (§7.2/§7.3 stride vs cap, §3.3/§7.1 blend-tree bit, §8.1/§8.2 preset order, §9.1 truncate vs drop, §1.4/§15.1 slots 7 and 9, §3.1/§3.2 `Trigger` shape); narrowed 12 CONFIRMED headings that contain OPEN items (§2.2, §3.1, §3.3, §7.1, §7.2, §7.3, §8.1, §8.2, §9.1, §12.2, §14.1, §15.1) and annotated the §16 preamble; corrected the §18.2 size range (7,361 → 2,170); struck the §1.1 traffic-ai attribution and the stale §1.4.1 "struck through above" sentence; added a `Name`-key note to §11.2; paraphrased the §3.2 engine diagnostic string; replaced repo-absent dump paths with "(evidence dump, not in repo)"; added Team B §9.90 figures at §3.5, §6.3, §9.3, §10.4, §11.2, §13.4.
- 2026-10-01 (cloud, manager relay of Team B job `20261001T004133-team-b-kmsh`): §3.1/§3.2 `<Trigger>` shape resolved to §3.2's `Name` child (7,060/7,060, direct text 0) — VALIDATED-BY-DATA; the §3.1 tree drawing corrected; the always-present `Frame` child (7,060/7,060) added as an observed field, meaning OPEN until the executable.
