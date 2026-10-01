# Saints Row: The Third — Environment, Weather, Lighting, Post-Processing and Visual-Effects Data Tables (schema recovered from the loaders)

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process) — agent AK, 2026-09-20
**Phase:** Phase 1 follow-on — "schema-from-loader" campaign (`HANDOFF.md` §30, archived §27.2), environment / weather / lighting / post-processing / VFX group. Companion to `spec-vehicle-data.md` §7 (agent AF, the worked example) and the sibling table specs of the same campaign.
**Scope:** For each of 27 named `.xtbl` tables: does the exe carry the table's filename literal; which function loads it; the element tree it reads (names, nesting, repeated elements), each element's type, destination offset in the runtime struct (or global), defaults, required-vs-optional behaviour, unit conversions, name-hash keys, cross-table references, fixed capacities, and where the loaded rows live. **No table value is read from any base-game container.** Values are described only where a raw-readable DLC sample exists.
**Method:** Exact filename literals (`AfStrXrefs.java`) → the loader that references each literal → the per-row reader → the shared XML accessors it calls. Because Ghidra's decompiler drops the argument list of the XML child-lookup wrapper (`0x00DAB9E0`), a purpose-built call tracer (`tools/scripts/AkTrace.java`) resolves every `CALL`'s pushed arguments (string literals, `LEA`-derived destinations, prior call results, child-lookup path chains) from the disassembly; the decompiler output (`AfDec.java`, `AkTree.java`) supplies control flow and conversions. Address constants (`AfMem.java`) were read directly. No whole-binary predicate search was used; the only searches are exact NUL-delimited literals and reference lists of named global addresses. Ghidra project copy `tools/gp_tbl4` (disposable). Validation harness: `tools/harnesses/env_dlc_validate.py` against the raw DLC tables in `tools/ah_dlc_xtbl/`.
**Cleanroom compliance:** No decompiled code is reproduced and no original internal identifiers are used (Ghidra `FUN_…`/`DAT_…` auto-names appear only as addresses-as-evidence). XML element and table names are data and are listed verbatim. Offsets, strides, capacities and constants are stated as facts. The two parked items of `HANDOFF.md` §27.3 (mode-(a) container decompression; `.czn_pc` interior) were not touched; no compressed table was read.
**Confidence key:** **CONFIRMED — disassembly** (read directly from the code) · **CONFIRMED — empirical** (checked against real DLC data) · **HIGH CONFIDENCE — inferred** · **HYPOTHESIS — unconfirmed** · **OPEN** · **UNKNOWN**

**Address convention.** All addresses are virtual addresses of the analysed `SaintsRowTheThird.exe` (the DX9 build the project's Ghidra database holds; `HANDOFF.md` §2). Offsets written `+0x..` are byte offsets into the row struct being described.

**Review status summary (2026-09-30):** an adversarial desk review (`review/adv_tables-environment.md`) checked 47 units: 23 DESK-PASS, 18 DESK-PASS with text fixes applied (most also carry OPEN items, below), 6 NEEDS-EXE, 0 VALIDATED-BY-DATA. A desk pass alone does not clear a unit: it means only that the text is internally consistent and agrees with the other specs. No unit here is fully VALIDATED-BY-DATA: Team B's full-population run (Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "ALL GATES PASSED against all 38 real archives", "No type mismatches ... or enum anomalies found anywhere in this group") checks row-level element names, enum text and integer/float shape one level below each row; it does not check nested element paths (most of §3.3, `Spasm`, `Damage_Region`, `LOD`), and nothing about offsets, strides, capacities, defaults or loader addresses. Where a Team B figure backs part of a unit it is cited in that unit's status line. Units awaiting the executable: §1.2, §1.4, §1.5, §3.1, §3.2, §4.1, §4.2, §6.3, §7.2, §7.3, §9.2, §9.3, §10.1, §10.2, §12.2, §13, §15, §17.4. Units awaiting real data: §1.5, §3.3, §8, §10.1, §12.1, §12.2, §17 (effects copies; §17.2 instance count).

**Review status summary (2026-10-01):** a re-derivation from the executable (job `20261001T123123-team-a-ytgi`; `review/rederive_tables-environment.md`) covered the 18 units awaiting the executable plus §2 and §8. **Cleared for implementation: 4** (§2, §6.3, §8, §10.2). **CORRECTED: 7 units** (§1.4 float sign handled in the grammar; §1.5 file-name rule; §3.1 wrap boundary and cell-array size; §4.1 `0x012E6BB0` and `0x005AA310`'s arguments; §9.2 default pair is data; §10.2 `LOD`-absent fields stay 0; §15.2 table end `0x01311084`). **Partly CONFIRMED, still not cleared: 13** (§1.2, §1.4, §1.5, §3.1, §3.2, §4.1, §7.2, §7.3, §9.2, §9.3, §10.1, §12.2, and the §15 table range). **Untouched, still OPEN: §1.3, §4.2, §13, §17.4** (and the §11.1/§11.5 capacities). Units still awaiting the executable: 16 of the 18 above (§6.3 and §10.2 are cleared) plus §1.3. The DOF camera-position conflict with `spec-cutscene-camera-format.md` is resolved in §7.3 (two adjacent camera vectors written together). Corrections that affect Team B are flagged **FOR TEAM B** in §1.4, §3.1, §9.2, §10.2 and §12.2.

---

## 1. Overview, the anchor map, and the shared reader grammar

### 1.1 Result in one paragraph

**26 of the 27 named tables have an exact filename literal in the exe and a loader that was found from it** (§1.2). `materials.xtbl` has **no** standalone literal: the only occurrences of the byte string `materials.xtbl` are the tail of `weapon_tracer_materials.xtbl`, `bitmap_materials.xtbl` and `customization_materials.xtbl`, and the bare word `materials` in the image is unrelated mesh vocabulary (`materialIndices`); it is skipped. Every loader is a small function that (a) opens the named table with one shared helper, (b) counts and iterates the row elements, (c) calls a per-row reader that binds XML element names to struct offsets, (d) frees the parsed document. **Prioritised tables decoded in full: `weather`, `weather_time_of_day` (the per-time-of-day lighting record — ≈150 named parameters), `wind`, `rain`, `lightning`, `lens_flares`, `motion_blur`, `radial_blur`, `dof_situations`, `refraction_situations`, `skybox_effects`, `time_of_day_objects`, `external_light_override`, `effects`, `vfx`.** The remaining tables are decoded at loader level, with the depth stated per section. **Validation:** every element path of all raw-readable DLC samples of `effects`, `vfx` and `refraction_situations` (121 rows, 1,372 element-path instances) is accounted for (§14). **Cross-checks:** `dof_situations` outputs feed the same render-packet slots as the cutscene DOF channels (§7.4); `wind.xtbl` was checked against the tree wind decode and is **not** connected to it (§4.2). A third reader of the time-of-day lighting record (per-mission override files, §15) independently confirms §3.3's offsets and shows that the element names of four cloud-layer slots disagree between the two vocabularies (§15.3).


**Review status (2026-09-30): DESK-PASS ("26 of 27" agrees with Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "26 of 27 loaded tables; materials.xtbl has no standalone filename literal or loader"; 121 = 60 + 60 + 1 and 1,372 = 838 + 516 + 18 recomputed) — desk review (not re-derived from the executable).**

### 1.2 Anchor map — table → literal → loader **[CONFIRMED — disassembly for every row]**

The literal column is the address of the NUL-delimited filename string in the image; "loader" is the function that references it.

| Table | Literal | Loader (entry) | Notes |
|---|---|---|---|
| `weather.xtbl` | `0x0111F064` | `0x005A8D10` | optional (guarded by a file-exists test); row reader `0x005A8510` (§2) |
| `weather_time_of_day.xtbl` | `0x0111F08C` | `0x005A8F20` | optional; segment reader `0x005A89F0`, lighting-record parser `0x00BB6350`→`0x00BA1600` (§3) |
| `wind.xtbl` | `0x0111F0D8` | `0x005A93C0` | row reader `0x005A9260` (§4) |
| `rain.xtbl` | `0x0111DE90` | `0x0059B570` | §5.1 |
| `lightning.xtbl` | `0x0111DB68` | `0x005993B0` | row reader `0x005991C0` (§5.2) |
| `lens_flares.xtbl` | `0x0111E448` | `0x005A10E0` | §6.1 |
| `motion_blur.xtbl` | `0x0111DDAC` | `0x0059A800` | §6.2 |
| `radial_blur.xtbl` | `0x011238D4` | `0x005CE140` | §6.3 |
| `dof_situations.xtbl` | `0x0111CF0C` | `0x0058EC80` | §7 |
| `refraction_situations.xtbl` | `0x0111E120` | `0x0059EFB0` (+ DLC driver `0x0059F7B0`) | §8 |
| `skybox_effects.xtbl` | `0x01122E4C` | `0x005C36D0` | §9.1 |
| `time_of_day_objects.xtbl` | `0x0111E494` | `0x005A12E0` (row `0x005A11C0`) | §9.2 |
| `external_light_override.xtbl` | `0x011804C4` | `0x00A72B80` → generic `0x00A72AC0` | optional; §9.3 |
| `effects.xtbl` | `0x011231F0` | `0x005C4B10` (via `0x005CE8D0`, DLC driver `0x005C5160`); a second reader `0x00739A80` | §10.1 |
| `vfx.xtbl` | `0x01123228` | `0x005C3E70` row reader (via `0x005CE760`, `0x005C5160`); a second reader `0x00ACEAE0` | §10.2 |
| `interface_effects.xtbl` | `0x01155948` | `0x007AC920` | §11.1 |
| `decal_info.xtbl` | `0x0115280C` | `0x007474F0` (row `0x007471F0`) | §11.2 |
| `groundfires.xtbl` | `0x01128224` | `0x005FD580` | §11.3 |
| `shells.xtbl` | `0x0111C9D0` | `0x0058BD30` | §11.4 |
| `material_color_variants.xtbl` | `0x01152D74` (held by a pointer variable `0x012F7BE8`) | `0x007549F0` | §11.5 |
| `bitmap_materials.xtbl` | `0x0113E068` | `0x006F7780` | §11.6 |
| `bitmap_sheets.xtbl` | `0x01123AF4` | `0x005D2370` → `0x00E222E0` | different loader family (§11.7) |
| `map_districts.xtbl` | `0x0115A62C` | `0x007D8FB0` | optional; §11.8 |
| `camera_shake.xtbl` | `0x0111C178` | `0x0057C290` | §12.1 |
| `camera_free.xtbl` | `0x0111BDB4` | `0x0056E4C0` | the gameplay camera table; §12.2 |
| `fade_categories.xtbl` | `0x0111C54C` | `0x00584900` | §11.9 |
| `materials.xtbl` | **none** | — | skipped (see §1.1); real top-level entry with 25 rows found empirically, §17.6 |


**Review status (2026-09-30): NEEDS-EXE: the 27 literal and loader addresses have no independent cross-check outside this spec except `0x005C50B0` (agrees with `spec-tables-weapons-combat.md` §1.6) and the CRC routines; re-verify by cross-reference. Team B consumes no address — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`), partially: the literal→loader rows of `weather_time_of_day` (`0x0111F08C` pushed twice by `0x005A8F20`), `radial_blur` (`0x011238D4` in `0x005CE140`, sole caller `0x005CE8D0`), `refraction_situations` (`0x0111E120` in `0x0059EFB0`, callers `0x0059F260`, `0x0059F7B0`) and `external_light_override` (`0x011804C4` in `0x00A72B80`, sole caller `0x00707C80`), plus the loader→reader edges of `effects` (`0x005C4B10` ← `0x005C5160`, `0x005CE8D0`), `vfx` (`0x005C3E70` ← `0x005C45D0`), `weather` (`0x005A8510` ← `0x005A8D10`) and `camera_free` (`0x0056DCA0` ← `0x0056E4C0`), CONFIRMED — disassembly — not cleared: the remaining 18 literal/loader pairs (loaders not dumped).**

### 1.3 The shared loader skeleton **[CONFIRMED — disassembly]**

Every loader here has the same shape, built from the same six shared functions in `0x00DAB9D0`–`0x00DAD460` (the XML accessor library `spec-vehicle-data.md` §7.2 first catalogued):

1. **Open:** `0x00DAC9A0(filename, 0, 1)` parses the named table from the virtual file system and returns the document's **`Table`** element (`0` on failure; on failure it prints a formatted "table file missing or invalid" message carrying the file name and the parser error). Optional tables are first tested with the file-exists helper `0x00DA90D0(filename)` and silently skipped when absent.
2. **Count / iterate:** `0x00DAB9E0`'s siblings — `0x00DABA00(node, "Name")` = number of children with that name; `0x00DAB9E0(node, "Name")` = first child with that name; `0x00DAB9F0(parent, node, "Name")` = next sibling with that name — walk the row elements. **All name comparisons are case-insensitive** (`_stricmp`).
3. **Read the row** with the accessors of §1.4.
4. **Close:** `0x00DAB9D0(doc)` frees the parsed document.

**Node layout (parsed XML element):** `+0x00` name (`char*`), `+0x04` next sibling, `+0x08` first child, `+0x0C` text (`char*`, `NULL` when the element has no text). Children are matched by name, first match wins; text is never trimmed by the lookup (leading/trailing whitespace is handled by each parser below). **[CONFIRMED — disassembly, the three lookup functions read in full.]** **[Desk review 2026-09-30: for mixed content (an element with its own text and child elements, §17.2) the readers of §9.3/§11.5 take the element's own leading text; the real colour values sit in that leading text (§17.2).]** **[OPEN — desk review 2026-09-30: the text does not state whether `+0x0C` holds only the element's leading character data or also text that follows a child element; to be settled against the executable.]** **[2026-10-01, job `20261001T123123-team-a-ytgi`: still OPEN — the child lookups (`0x00DC4FF0`, thunk `0x00DAB9E0`) only read `+0x0C`; the text is assigned by the tokenizer/tree builder `0x00DC56B0`, reached from the open helper `0x00DAC9A0` through `0x00DC5AC0` → `0x00DC5820`, and `0x00DC56B0` was not dumped. Incidental, CONFIRMED — disassembly (`0x00DC5820`): the parse fails with a message carrying the line number when the file does not tokenise, holds no element, or leaves a tag unclosed.]**

**Re-entrancy.** Most array-backed loaders allocate on first call and, when called again with the array already present, only reload if the new row count fits the old capacity (`if capacity < new count: return`). Capacity is therefore fixed by the first load. **[CONFIRMED — disassembly for `weather`, `wind`, `rain`, `lightning`; the pattern is not universal.]**


**Review status (2026-09-30): DESK-PASS, text fixes applied (mixed-content note); NEEDS-EXE: node text for mixed content. The accessor addresses agree with `spec-tables-traffic-ai.md` §1.3 — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`): the dumps stop short of the parser — not cleared: node text for mixed content (`0x00DC56B0`).**

### 1.4 The accessor catalogue (all `__cdecl`, arguments `(dest, node, "child name")` unless stated) **[CONFIRMED — disassembly]**

| Function | Reads | Absent child | Grammar |
|---|---|---|---|
| `0x00DACCB0` | `f32` | **runs the parser on an uninitialised 1 KiB stack buffer — see the hazard below** | engine float grammar |
| `0x00DACD40` | `f32`, "write only if present"; returns 1 if it wrote | leaves `dest` untouched, returns 0 | engine float grammar |
| `0x00DACE40` | `f32` replicated into four consecutive `f32` (`dest[0..3]` all equal) | as `0x00DACCB0` | engine float grammar |
| `0x00DACDE0(node)` | `f32` from the element's **own** text (no child name) | — | engine float grammar |
| `0x00DC5210(node, default)` | `f32` from the element's own text, else `default` | returns `default` | C runtime `atof` (not the engine grammar) |
| `0x00DABC70` | `s32`, always writes | as `0x00DACCB0` | engine integer grammar |
| `0x00DABDF0` | `s32`/`u32`, always writes **[Desk review 2026-09-30: `spec-tables-traffic-ai.md` §1.3 records this one as **unsigned** — a leading `-` parses as 0]** | as `0x00DACCB0` | engine integer grammar |
| `0x00DABE80` | integer, write only if present **[Desk review 2026-09-30: unsigned per `spec-tables-traffic-ai.md` §1.3; its signed counterpart is `0x00DABD20` (signed, write only if present), not listed in this table]** **[CONFIRMED — disassembly 2026-10-01: `0x00DABE80` passes the text straight to the integer grammar (unsigned, write-if-present); `0x00DABD20` tests for a leading `-`, parses from the next character and negates (signed, write-if-present). Both return 1 when they wrote, 0 otherwise, and both accept a `NULL` child name, meaning "the node's own text".]** | untouched | engine integer grammar |
| `0x00DABF20` | integer narrowed to `u16`, always writes | as `0x00DACCB0` | engine integer grammar |
| `0x00DAC480` | `bool` (byte), always writes | as `0x00DACCB0` | `true`/`yes` → 1, everything else → 0 |
| `0x00DAC510` | `bool`, write only if present; returns 1 if it wrote | untouched | as above |
| `0x00DABA10(node, "name")` | `char*` to the child's text (pointer into the document — **only valid until the document is freed**) | `NULL` | — |
| `0x00DABA40(node, "name")` | same as `0x00DABA10` but without the null-node guard | `NULL` | — |
| `0x00DABA70(dst, size, node, "name")` | bounded string copy, NUL forced at `dst[size-1]` | `dst` untouched | — |
| `0x00DABAB0(dst, size, node, "name")` | as `0x00DABA70`, returns 1 if it copied | returns 0 | — |
| `0x00DAC900(dst, size, node, "name")` | copies the text of `node/name/Filename` | untouched | — |
| `0x00DAC880(node, "name")` | pointer to the text of `node/name/Filename` | `NULL` | — |
| `0x00DAC950(dst, size, node, "name")` | like `0x00DAC900`, returns 1 if it copied | returns 0 | — |
| `0x00DACF60(dest, node, "name")` | `vec3`: children **`X`**, **`Y`**, **`Z`** into `dest[0..2]` as by `0x00DACCB0` ~~**[OPEN — desk review 2026-09-30: `spec-tables-traffic-ai.md` §1.3 names `0x00DACF20` (X/Y/Z children of the node itself) as the vec3 reader and `spec-tables-weapons-combat.md` §1.3 lists `0x00DACF20`, `0x00DACF60` and `0x00DACFB0` as three vec3 readers; how `0x00DACF60` relates to `0x00DACF20` is not stated; to be settled against the executable.]**~~ **[CONFIRMED — disassembly 2026-10-01: `0x00DACF60` finds the named child (`0x00DC4FF0`) and, only if it exists, applies the same `X`/`Y`/`Z` triple of `0x00DACCB0` reads (literals `0x01121CB4`/`0x01121CB0`/`0x01121CAC`) that `0x00DACF20` applies to a node directly; `0x00DACF20(dest, node)` is the "node is the vector" form, `0x00DACF60` the "named child is the vector" form. `0x00DACFB0` and `0x00DAD010` push the same three literals — two more vec3 variants, not read.]** | whole vector untouched if `name` is absent | — |
| `0x00DAD0A0(dest3, node)` | **colour**: children **`R`**, **`G`**, **`B`**, each divided by **255.0**, into `dest[0..2]` | — | — |
| `0x00DAD130(dest3, node, "name")` | `0x00DAD0A0` on the named child, only if it exists | untouched | — |
| `0x00DE03E0(node, dest4)` | `vec4` from the element's own text: four whitespace-separated numbers (`strtok` on space/CR/LF/tab, then `atof`) | untouched | C `atof` |
| `0x00DE0200(node, dest)` | `f32` from the element's own text; returns 1/0 | returns 0 | C `atof` |
| `0x00DAC830(names[], count, node)` | **enum lookup**: index of the element's text in a table of literal names (`_stricmp`), `-1` if none or if the node is `NULL` | `-1` | — |

**Engine float grammar** (`0x00DACB20`): optional `-`; a leading `.` is accepted; **a leading `0x`/`0X` returns 0.0**; integer digits, optional `.` fraction, optional `e`/`E` exponent. **Engine integer grammar** (`0x00DAB8B0`): decimal, or `0x` hexadecimal; stops at the first non-digit. ~~**[Desk review 2026-09-30: sign handling is in the accessor, not here — per `spec-tables-traffic-ai.md` §1.3 only `0x00DABC70`/`0x00DABD20` accept a leading `-`; `0x00DABDF0`/`0x00DABE80` read it as 0.]**~~ **[CORRECTED — CONFIRMED — disassembly 2026-10-01: the float grammar `0x00DACB20` handles a leading `-` itself (it negates the integer part, the fraction and the result), so every float accessor built on it (`0x00DACCB0`, `0x00DACD40`, `0x00DACDE0`, `0x00DACE80`) is signed; `-.5` works and `-0x10` is 0.0. For integers the sign is handled by the accessor — `0x00DABD20` (and, per `spec-tables-traffic-ai.md` §1.3, `0x00DABC70`) negate after a leading `-`, `0x00DABE80` (and `0x00DABDF0`) pass the text straight to the grammar, which stops at the `-` and yields 0. The fraction is the digits after the first `.` found anywhere in the text, weighted by successive powers of 0.1 in single precision; an `e`/`E` right after them takes the rest as a C integer power of ten (`0x007D9B00`).]** **[OPEN — desk review 2026-09-30: this document does not say which accessor (signed or unsigned) each integer field below uses (`Density`, `Start_Time`, `On_Time`, `Priority`, `Life_Time`, `Fade_Time`, `Blocker/LifeSpan`, `rampin`/`duration`/`return`, …), so whether a negative value survives is not pinned; **[2026-10-01, CONFIRMED — disassembly: `Start_Time`, `Ramp_Out_Time` (`0x005A89F0`), `Priority` (`0x005CE140`) and `Blocker/LifeSpan` (`0x005C3E70`) use `0x00DABC70` (signed, always-write); `Audio_Occlusion` (`0x006F7780` is among the callers of `0x00DABE80`) is unsigned write-if-present. The others (`Density`, `On_Time`/`Off_Time`/`Variation`, `Life_Time`/`Fade_Time`, `rampin`/`duration`/`return`, `max_drunk_*`, `Skybox_Layer`, `TODRange`) remain OPEN.]** ~~also the float grammar's handling of a leading `+`, leading whitespace and a bare `-` is not stated~~ **[2026-10-01: a leading `+`, leading whitespace and a bare `-` are not treated by the float grammar itself and fall through to the integer parser `0x00DAB8B0` (not yet read; expected 0 for the integer part, so ` 1.5` and `+1.5` would read 0.5 — HYPOTHESIS)]**; to be settled against the executable.]** Both work on a 1 KiB copy of the text (`strncpy`, NUL forced at `[0x3FF]`). This is the grammar `spec-vehicle-data.md` §7.2 describes; the addresses of the accessors are the ones named there.

**FOR TEAM B (2026-10-01, CORRECTION):** every float field read through the engine float grammar accepts a leading `-` (the sign is handled in `0x00DACB20`, not in the accessor); the "leading `-` reads as 0" rule applies only to the unsigned integer accessors `0x00DABE80`/`0x00DABDF0`.

**A hazard in the "always write" accessors — a correction of, or at least a caveat to, `spec-vehicle-data.md` §7.2.** That section records `0x00DACCB0` as "always write, 0 if absent". The disassembly of `0x00DACCB0` (and of `0x00DABC70`, `0x00DABDF0`, `0x00DAC480`, `0x00DABF20`, which share the pattern) shows **no initialisation of the 1 KiB local buffer**: when the child (or its text) is missing, the `strncpy` is skipped and the parser runs over whatever the stack held. The value written for an absent element is therefore **indeterminate — in practice the residue of an earlier call at the same stack depth, most likely the value text of the previously parsed sibling** — not `0`. **[CONFIRMED — disassembly for the missing initialisation; the run-time value is not modelled.]** Consequence for a reimplementation: authoring data always supplies these elements (every DLC sample checked does), so the case does not arise in shipped data; treat "absent" as `0` or as a hard error, do not try to reproduce the residue. The write-only-if-present accessors (`0x00DACD40`, `0x00DAC510`, `0x00DABE80`) are not affected.

**Bool text.** `0x00DAB850` compares the whole text case-insensitively against `true`, `yes` (→ 1), `false`, `no` (→ 0); **any other text yields 0**. (`True`/`False` — the spelling every DLC sample uses — are therefore accepted.)

**Name hashes.** `0x00D9E8B0(str, seed, maxlen)` and `0x00D9E740(str, seed)` are the engine's table-driven **CRC-32** (reflected, table at `0x01320DA0`, input **lower-cased**, no final XOR). Every loader in this document calls it with **seed `0`**. **[CONFIRMED — disassembly.]** (Same routine, same seed convention as `spec-vehicle-data.md` §7.1.)

**Other string conventions.** Interned strings (`Name` of a weather/wind stage): `0x00DB12C0(str, 0, 0)` with the pool object in `ECX` returns a pointer into a per-table string pool (weather/TOD pool at `0x0140E328`, wind pool at `0x0140E5C0`). The "framework" convention for DLC rows and filenames is in §1.5. The localisation helper `0x0084A1B0(key, 0)` turns a display-name key into a text handle.


**Review status (2026-09-30): three units. Accessor catalogue: DESK-PASS, text fixes applied (signedness per `spec-tables-traffic-ai.md` §1.3, `0x00DABD20` named); NEEDS-EXE: per-field signed/unsigned accessor, `0x00DACF60` vs `0x00DACF20`, float-grammar edge cases. Always-write hazard: DESK-PASS (agrees with `spec-tables-traffic-ai.md` §1.3; Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "Always-field presence confirmed 100% at real scale"). Name hash: DESK-PASS (CRC-32 convention agrees with the other table specs); pool and localisation addresses unconsumed — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`): accessor catalogue — `0x00DACF60`/`0x00DACF20` relationship CONFIRMED — disassembly; `0x00DABD20` signed / `0x00DABE80` unsigned CONFIRMED; float sign handling CORRECTED (in the grammar `0x00DACB20`); `Start_Time`, `Ramp_Out_Time`, `Priority`, `Blocker/LifeSpan` use `0x00DABC70` and `Audio_Occlusion` uses `0x00DABE80` (CONFIRMED) — not cleared: the other integer fields' accessors and the `+`/whitespace/bare-`-` cases (`0x00DAB8B0` and the remaining row readers not dumped). Always-write hazard and name hash: unchanged.**

### 1.5 The framework (DLC) convention **[CONFIRMED — disassembly + CONFIRMED — empirical]**

Tables that a DLC can extend take a **framework name** (`main` for the base game). Three facts, all seen in code and in the raw DLC data:

- **Row filter.** A row carries an optional `Framework` child; absent means `main`. The loader keeps a row when the row's framework equals the framework being loaded (`effects`, `vfx`, `refraction_situations`; in `vfx` a `main` row is additionally visible to a non-`main` load **[Desk review 2026-09-30: per §10.2 this holds only for the second reader `0x00ACEAE0`; the row reader `0x005C3E70` uses strict equality]**).
- **File name.** The DLC's copy of the table is named `<framework>_<table>.xtbl` (`0x0045BA50` formats `"%s_%s"`, ~~skipping the prefix when the framework descriptor's byte at `+0x103` has bit `0x04` set or the base name already starts with the framework name~~ skipping the prefix when the framework descriptor's byte at `+0x103` has bit `0x04` set, or when the predicate `0x00DA73C0` applied to the framework name alone (descriptor `+0x80`) is true — the base name is never examined; the predicate's meaning is OPEN, likely an empty-name test; the buffer is `0x40` bytes with the NUL forced at the last byte (`0x00DA78D0`, a bounded `vsnprintf`). `0x0045BAB0` is the same formatter without the `+0x103` test (used for the `<framework>_vfx_containers` name). **[CORRECTED — CONFIRMED — disassembly 2026-10-01 for the two formatters; `0x00DA73C0` not read.]**). ~~**[OPEN — desk review 2026-09-30: "already starts with the framework name" does not say whether this is a plain prefix test or a `<fw>_` test, or whether it is case-insensitive (`0x0045BA50`/`0x0045BAB0`); to be settled against the executable.]**~~ **[2026-10-01: settled — there is no base-name test; see the corrected rule above.]** That is exactly the raw-archive names `dlc1_effects.xtbl`, `dlc2_refraction_situations.xtbl`, `dlc3_vfx.xtbl`. A framework descriptor (`+0x80` = the name string) has bit `0x02` of `+0x103` set when it carries a `refraction_situations` table (tested by `0x0059F7B0`).
- **Slot start.** For `effects.xtbl` the DLC rows are placed in the shared effects array starting at the **`Info_Slot_Index` of the first `Effect` row whose `Framework` matches** (§10.1). Other rows' `Info_Slot_Index` values are not read. **[OPEN — desk review 2026-09-30: whether any base `Effect` row carries `Info_Slot_Index` or `Framework` beyond the 60 DLC rows (Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "`Framework` in 60/1,272 (exactly the known 60 DLC rows)"; `Info_Slot_Index` is not counted there); to be settled against real data.]**


**Review status (2026-09-30): DESK-PASS, text fixes applied (vfx visibility pointer to §10.2); NEEDS-EXE: file-name skip rule; NEEDS-DATA: `Info_Slot_Index` on base rows. Default-`main` supported by Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "`Framework` in 60/1,272" — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`): file-name rule CORRECTED (no base-name test; a one-argument predicate on the framework name) — not cleared: `0x00DA73C0` (predicate meaning); NEEDS-DATA: `Info_Slot_Index` on base rows.**

---

## 2. `weather.xtbl` — weather stages **[CONFIRMED — disassembly for every field below]**

**Loader** `0x005A8D10`; row reader `0x005A8510`; post-pass resolvers `0x005A8BD0` (name → entry) and `0x005A8C20` (name → index). **Root/row:** `Table` → repeated **`Weather_Stage`** elements. **Runtime array:** base `0x0140E308`, entry stride **`0xDC`** (220 bytes), live count `0x0140E304`, capacity `0x0140E314` (fixed at first load = the row count), a `(capacity×5 + 0x14)×4`-byte scratch block at `0x0140E324`. The table is optional (file-exists guard).

**Load procedure.** (1) Count `Weather_Stage` rows, allocate `count × 0xDC` (16-byte aligned) and zero it. (2) Read each row with the row reader; the reader **returns the row's `Chance`**, and the loader sums the returned values. (3) After all rows: for every stage whose unresolved-flag byte (`+0xD8`) is set, replace each `Next_Stage` name pointer by a pointer to the entry with the same name (`_stricmp` scan; **`NULL` when unmatched**) and clear the flag; then divide every entry's `+0x08` by the sum — **`Chance` is stored as a probability weight normalised over all stages** (no guard against a zero sum). **[CONFIRMED — disassembly; the sum-and-return flow read from the instruction stream, since the decompiler misrenders it.]**

| Element path (under `Weather_Stage`) | Type | Offset | Notes |
|---|---|---|---|
| `Name` | interned string | `+0x00` | key for `Next_Stage`, `weather_time_of_day.Stage_Name`, `skybox_effects.Weather_Stage` (all `_stricmp`) |
| `DisplayName` | localisation key | `+0x04` | falls back to `Name` when absent; **spelled `DisplayName` here, `Display_Name` in `wind.xtbl`** |
| `Stage_Settings/Chance` | `f32` | `+0x08` | raw weight → normalised (above) |
| `Stage_Settings/Average_Duration` | `f32` | `+0x0C` | |
| `Stage_Settings/Variance` | `f32` | `+0x10` | |
| `Stage_Settings/PedDensity` | `f32` | `+0x14` | |
| `Rain_Settings/Rain_Density` | `f32` | `+0x18` | (not `rain.xtbl`'s integer `Density`) |
| `Rain_Settings/Lightning_Settings/Frequency` | `f32` | `+0x3C` | |
| `Rain_Settings/Lightning_Settings/Variance` | `f32` | `+0x40` | |
| `Rain_Fog_Settings/Layer_1/U_Scale` `V_Scale` `Scroll_Speed` `Opacity` | 4 × `f32` | `+0x1C` `+0x20` `+0x24` `+0x28` | |
| `Rain_Fog_Settings/Layer_2/U_Scale` `V_Scale` `Scroll_Speed` `Opacity` | 4 × `f32` | `+0x2C` `+0x30` `+0x34` `+0x38` | |
| `TOD_Settings/Tod_Lights_Pct` | `f32` | `+0x54` | |
| `TOD_Settings/Skydome_Settings/Blend_Factor` | `f32` | `+0x44` | |
| `TOD_Settings/Skydome_Settings/Color` | colour (`R`,`G`,`B` ÷ 255) | `+0x48`..`+0x50` | `vec3` |
| `Sun_Settings/Sun_Color_Multiply` | colour | `+0x58`..`+0x60` | |
| `Sun_Settings/Sun_Glow_Color_Multiply` | colour | `+0x64`..`+0x6C` | |
| `Sun_Settings/Sun_Opacity` | `f32` | `+0x70` | |
| `Moon/Moon_Color_Multiply` | colour, **only if present** | `+0x74`..`+0x7C` | untouched (zero) when absent |
| `Wind_Settings/Average_Speed` `Gust_Speed` `Frequency` `Variance` | 4 × `f32` | `+0x80` `+0x84` `+0x88` `+0x8C` | |
| `Cloud_Settings/Map_Strengths/Horizon_Cirrus` `Horizon_Cumulus` `Horizon_Storm` `Overhead_Cirrus` `Overhead_Cumulus` `Overhead_Storm` | 6 × `f32` | `+0x90` `+0x94` `+0x98` `+0x9C` `+0xA0` `+0xA4` | |
| `Cloud_Settings/Scrolling/Horizon_Scroll_Rate` `Overhead_Scroll_Rate` | 2 × `f32` | `+0xA8` `+0xAC` | |
| `Temp_Settings/Min_Temp_Day` `Max_Temp_Day` `Min_Temp_Night` `Max_Temp_Night` | 4 × `f32` | `+0xB0` `+0xB4` `+0xB8` `+0xBC` | |
| `Ambient_Wave_Settings/Ambient_Wave_Speed` `Ambient_Wave_Amplitude` | 2 × `f32` | `+0xC0` `+0xC4` | |
| `Audio/Audio_Intensity` | `f32` | `+0xC8` | |
| `Next_Stage_List/Next_Stage` (repeated, text = a stage name) | name → entry pointer | count `+0xCC`, capacity `+0xD0`, array pointer `+0xD4`, unresolved flag `+0xD8` | the array is sized on the first load (`count × 4`, zeroed); a later load whose `Next_Stage` count exceeds the capacity abandons the row |

All `f32` fields above use `0x00DACCB0` (always-write; hazard of §1.4). The struct ends at `+0xDC`; bytes `+0xD9`–`+0xDB` are padding. **Runtime consumers** of this array include `0x005A8C20` (name → index, used by `skybox_effects.Weather_Stage`, §9.1) and the weather state machine (not traced; §16).


**Review status (2026-09-30): DESK-PASS (offsets tile to `0xDC` with no overlap; the real `Chance` sum 138, §17.8, reproduces; the row-level element tree is backed by Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "weather.xtbl's 9 groups 5/5"; offsets and stride are not data-checked) — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`): every leaf element → offset pair, the colour readers (`0x00DAD0A0` at `+0x48`/`+0x58`/`+0x64`, the if-present `0x00DAD130` at `+0x74`), the `DisplayName` → `Name` fallback and the `Next_Stage` sub-array rule CONFIRMED — disassembly from `0x005A8510` (the group-element names and the `Chance` return value are not visible in this dump and rest on the earlier reading) — cleared for implementation.**

---

## 3. `weather_time_of_day.xtbl` — time segments × weather stages, and the per-cell lighting record

**Loader** `0x005A8F20` (optional table); segment reader `0x005A89F0`; per-cell parser `0x00BB6350` → `0x00BA1600` (the lighting-record parser, 4,060 bytes) with the colour helper `0x00BA1510`. `weather.xtbl` **must load first** (the segment array is sized by the weather capacity). **Root/row:** `Table` → repeated **`Weather_Time_Segment`**.

### 3.1 Segment level **[CONFIRMED — disassembly]**

| Element | Type | Segment field | Notes |
|---|---|---|---|
| `Name` | interned string | `+0x00` | |
| `Start_Time` | integer **HHMM** | `+0x04` `f32` = fraction of a day | value `t` → `((t mod 100)/60 + t/100) / 24`; ~~the result is wrapped into `[0,1]` by repeated `−1.0` **[Desk review 2026-09-30: for the HHMM reading `t/100` must be the integer hour (truncating division), e.g. 1430 → (30/60 + 14)/24 = 14.5/24; with real division it would give (0.5 + 14.3)/24. §9.2 states the same conversion unambiguously as `(hh×60 + mm)/1440`.]** **[OPEN — desk review 2026-09-30: whether the executable truncates `t/100`, which accessor (signed/unsigned) reads `t`, and whether a value of exactly 1.0 (t = 2400) wraps to 0; to be settled against the executable.]**~~ `t/100` is a truncating (signed) integer division; the result is reduced by 1.0 while it is strictly greater than 1.0 (so t = 2400 gives exactly 1.0, not 0). Read with `0x00DABC70` (signed, always-write). **[CONFIRMED — disassembly 2026-10-01 (`0x005A89F0`); the wrap boundary is a CORRECTION.]** |
| `Ramp_Out_Time` | integer (**minutes**) | `+0x08` `f32` = `minutes / 1440` | |
| `Weather_Stages/Stage` (repeated) | see §3.2 | array pointer `+0x0C` | ~~`stage_count × 0xE48` bytes per segment **[OPEN — desk review 2026-09-30: `stage_count` is not defined: the header above says the segment array is sized by the weather capacity and below each segment has one cell per weather stage, which fits the weather stage count/capacity (`0x0140E304`/`0x0140E314`), not the number of `Stage` rows in the segment (real data: 4 `Stage` rows against 5 weather stages, §17.2); `0x005A89F0`, `0x005A8F20`; to be settled against the executable.]**~~ one cell per weather-stage slot: weather capacity (`0x0140E314`) × `0xE48` bytes (16-byte aligned), allocated once at first load; each cell is initialised by `0x00B9E450` (not read), given its orbital vector `{data = cell+0x304, capacity 15, count 0}` and a zero back-pointer at `+0xE44`; a re-load keeps the arrays, zeroes each segment's `+0x00`/`+0x04`/`+0x08` and re-initialises every cell. **[CORRECTED — CONFIRMED — disassembly 2026-10-01 (`0x005A8F20`): the number of `Stage` rows plays no part in the size.]** |

**Segment array:** base `0x0140E310`, stride **`0x10`**, count `0x0140E30C`, capacity `0x0140E318` (= the `Weather_Time_Segment` row count at first load) ~~**[Desk review 2026-09-30: this is the segment-array capacity; "sized by the weather capacity" in the header above refers to the per-segment cell array, see the OPEN note in the table]**~~ (the cell-array size is now stated in the table above). After loading, the segments are **sorted ascending by `+0x04` (start time)** with `qsort` (comparator at `0x005A84D0`, verified). Each segment owns one **cell** per weather stage; cell *k* belongs to weather stage *k* (index in `weather.xtbl` order). A `Stage` row is matched to its cell by **`Stage_Name` ↔ weather `Name` (case-insensitive)**; unmatched rows are ignored; A re-load whose segment count exceeds the first load's capacity `0x0140E318` is abandoned before any row is read **[CONFIRMED — disassembly 2026-10-01]**; the matching `Stage` row's cell is `+0x0C + k·0xE48` with `k` = the matched weather entry's index, it is handed to `0x00BB6350` with its orbital vector and the `Stage` node, and its `+0xE44` back-pointer is set to the weather entry **[CONFIRMED — disassembly 2026-10-01; a `Stage` with no `Stage_Name` would hand `NULL` to `_stricmp` — HIGH CONFIDENCE hazard, not exercised]**; cells never named stay zeroed with all presence bits clear.


**Review status (2026-09-30): DESK-PASS, text fixes applied (integer-hour reading, capacity wording); NEEDS-EXE: cell-array allocation size and `t/100` truncation (`0x005A89F0`, `0x005A8F20`) — desk review (not re-derived from the executable).**

**FOR TEAM B (2026-10-01, CORRECTION):** `Start_Time` = 2400 converts to exactly 1.0 (kept, not wrapped to 0); only values strictly above 1.0 are reduced by 1.0. `t/100` truncates toward zero and `t` is signed.

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x005A89F0`, `0x005A8F20`): integer-hour truncation and the signed accessor CONFIRMED — disassembly; wrap boundary CORRECTED (1.0 kept); cell-array size CORRECTED to weather capacity `0x0140E314` × `0xE48`; re-load rule added — not cleared: the cell constructor `0x00B9E450` (initial cell contents).**

### 3.2 The cell — a `0xE48`-byte record **[CONFIRMED — disassembly]**

`+0x000`–`+0x2F7`: the **lighting-parameter block** (§3.3). `+0x2F8`: a small vector `{data pointer, capacity = 15, count}` of **orbital-object overrides** with its inline storage at `+0x304` (15 × `0xC0` = up to `+0xE44`); `+0xE44`: a back-pointer to the weather stage entry. The per-cell initialiser `0x00BB6350` clears the eleven presence bytes `+0x2EC`–`+0x2F6`, the LUT name byte `+0x220`, and creates one `0xC0`-byte orbital record per entry of the global orbital-object list (count at `0x0290FBB8`, names copied from `0x0290FBB0 + 0x565 + i×0x5D0`, 30 bytes each) **with no bound check against the capacity 15** (a sixteenth object would write through a `NULL` slot). **[Desk review 2026-09-30: arithmetic checks — `0x304 + 15×0xC0 = 0xE44`, `+4 = 0xE48`.]** **[OPEN — desk review 2026-09-30: the orbital record's bytes `+0x1E`–`+0x5B` (between the 30-byte name and `Tint` at `+0x5C`) are not described, and the real number of orbital objects ~~(the overflow condition) is unknown~~ is unknown; the record bytes outside `+0x00`–`+0x1D`, `+0x5C`–`+0x73`, `+0xB8`–`+0xBD` are untouched by `0x00BB6350`/`0x00BA1600` and can only come from the cell constructor `0x00B9E450` (not read) **[2026-10-01]**; `0x00BB6350`, list `0x0290FBB0`/`0x0290FBB8`; to be settled against the executable.]** **[2026-10-01, CONFIRMED — disassembly (`0x00BB6350`): the sixteenth-object case is real — when the count would exceed the capacity 15 the slot pointer is 0 and the following writes (name byte 0, `+0xBC` word = 0, `+0xB8` = `0xFFFFFFFF`, 30-byte name copy) go through address 0. `0x0290FBB0` is a list object (`+0x00` data pointer under the `+0x565 + i·0x5D0` names, `+0x08` count = `0x0290FBB8`); its writer is not among the dumped functions (`0x00BB6400`, which writes two of its fields, is the candidate — HYPOTHESIS).]**

**Presence bits.** Nearly every field of the block has one presence bit in the eleven bytes `+0x2EC`–`+0x2F6`, set when the element is present in the row (the row is a *sparse override layer*: absent fields are "inherit"). The table below gives `P(byte,bit)`. Where several fields share one bit the code makes successive assignments to it (the last one wins) — noted. **[Desk review 2026-09-30: counting the bits §3.3 assigns, 12 of the 88 are not assigned to any field: `2F0` bits 1–2, `2F3` bits 2–7, `2F4` bits 0–1, `2F6` bits 6–7.]** ~~**[OPEN — desk review 2026-09-30: what, if anything, sets those 12 bits; list every presence-bit set site in `0x00BA1600`; to be settled against the executable.]**~~ **[CONFIRMED — disassembly 2026-10-01: `0x00BA1600` (read in full) has no write site for those 12 bits (`2F0` bits 1–2, `2F3` bits 2–7, `2F4` bits 0–1, `2F6` bits 6–7); every other bit is set exactly as §3.3 lists. The nested-group bits are assigned (set or cleared) from the accessor's result — hence the last-wins West/East bits — while the 21 direct `Stage` children OR theirs in. Whether the mission-override reader `0x00BA0140` uses the 12 is OPEN.]**


**Review status (2026-09-30): DESK-PASS, text fixes applied (arithmetic shown, 12 unassigned bits listed); NEEDS-EXE: unassigned bits, orbital record gap, orbital list origin (`0x00BB6350`, `0x00BA1600`, `0x0290FBB0`) — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x00BB6350`, `0x00BA1600` in full, xrefs of `0x0290FBB0`/`0x0290FBB8`): the 12 unassigned presence bits CONFIRMED unassigned by this reader; scalar offsets and the 16-object overflow CONFIRMED — disassembly — not cleared: `0x00B9E450` (orbital record init), `0x00BB6400` (list origin), `0x00BA1510` (colour destinations).**

### 3.3 The lighting-parameter block — element paths under one `Stage` row

Colour elements are read by `0x00BA1510`: the named child's **`R`**, **`G`**, **`B`** children are read **raw (not divided by 255)** into `dest[0..2]`, and the **sibling element `<Name>_Intensity`** (same parent, written with `sprintf("%s_Intensity")`) is read write-if-present into `dest[3]` with default `1.0`; the function returns "present". Colours are therefore `f32[4]` = RGB + intensity (stride `0x10`).

**`District_Lighting/Lighting_Parameters/`** — `TOD_Light_Color` → `+0x90` P(2EC,3) · `Ambient_Color` → `+0xA0` P(2EC,4) · `Back_Ambient_Color` → `+0xB0` P(2EC,5) · `Fog_Color` → `+0xD0` P(2EC,7) · `Fog_Ground` `f32` `+0x100` P(2ED,2) · `Fog_Atmosphere_Scale` `+0x104` P(2ED,3) · `Fog_Density` `+0x108` P(2ED,4) **[OPEN — desk review 2026-09-30: the real authored spelling of `Fog_Density` was not reported in §17.2 (the mission-override vocabulary of §15.2 uses `fog_density_new`); to be settled against real data.]** · `Fog_Density_Offset` `+0x10C` P(2ED,5) · `Ground_Reflection_Gloss` `+0x20C` P(2F4,2) · `Ground_Reflection_Brightness` `+0x210` P(2F4,3) · `Ground_Reflection_Spec_Brightness` `+0x214` P(2F4,4) **[Desk review 2026-09-30: this is the loader's literal; every real row and the table's own TableDescription spell it `Ground_Reflection_Spec_Refl_Brightness`, so the field is never set through this table — see §17.2]** · `Star_Strength` `+0x218` P(2F4,5) · `Meteor_Strength` `+0x21C` P(2F4,6).

**`District_Lighting/Skybox_Parameters/Horizon/`** — `Cloud_Front_Color` `+0x17C` P(2F0,5) · `Cloud_Back_Color` `+0x18C` P(2F0,6) · `Cloud_Layer_Strength` `+0x19C` P(2F0,7) · `Cloud_Normal_Map_Height` `+0x1A0` P(2F1,0) · `Cloud_Highlight` `+0x1A4` P(2F1,1) · *an element literally named `?`* `+0x1A8` P(2F1,2) · `Storm_Strength` `+0x1AC` P(2F1,3) · `Cloud_Speed` `+0x204` P(2F3,0).
**`…/Skybox_Parameters/Overhead/`** — `Cloud_Front_Color` `+0x15C` P(2F0,3) · `Cloud_Back_Color` `+0x16C` P(2F0,4) · `Cloud_Layer_Strength` `+0x1B0` P(2F1,4) · `Cloud_Normal_Map_Height` `+0x1B4` P(2F1,5) · `Cloud_Highlight` `+0x1B8` P(2F1,6) · *`?`* `+0x1BC` P(2F1,7) · `Storm_Strength` `+0x1C0` P(2F2,0) · `Cloud_Speed` `+0x208` P(2F3,1).
**`…/Skybox_Parameters/`** direct — `Backlight_Strength` `+0x200` P(2F2,7) · `Backlight_Power` `+0x1FC` P(2F2,6) · `Mountain_Front_Color` `+0x1C4` P(2F2,1) · `Mountain_Back_Color` `+0x1D4` P(2F2,2) · `Mountain_Fog_Color` `+0x1E4` P(2F2,3) · `Mountain_Fog_Density` `+0x1F4` P(2F2,4) · `Mountain_Normal_Map_Height` `+0x1F8` P(2F2,5) **[Desk review 2026-09-30: as `Crest_Threshold`, absent from this table's authored schema, §17.2]**.
**`District_Lighting/Water/`** — `Ambient_Color` `+0x260` P(2F4,7) · `Diffuse_Color1` `+0x270` P(2F5,0) · `Diffuse_Color2` `+0x280` P(2F5,1) · `Specular_Color` `+0x290` P(2F5,2) · `Specular_Alpha` `+0x2A0` P(2F5,3) · `Specular_Power` `+0x2A4` P(2F5,4) · `Falloff_Color` `+0x2A8` P(2F5,5) · `Fog_Color` `+0x2B8` P(2F5,6) · `Crest_Color` `+0x2C8` P(2F5,7) · `Crest_Threshold` `+0x2D8` P(2F6,0) **[Desk review 2026-09-30: absent from this table's authored schema; reachable only through a mission-override file, §17.2]**.
**`District_Lighting/Color_Correction/`** — `Window_Tint` colour `+0xC0` P(2EC,6) · `LUT_Filename/Filename` → `char[0x40]` `+0x220` P(2F6,5). **`District_Lighting/Exposure/`** — `Desired_Brightness` `+0x2E0` P(2F6,2) · `Exposure_Min` `+0x2E4` P(2F6,3) · `Exposure_Max` `+0x2E8` P(2F6,4). **`District_Lighting/TOD_Audio/Ambient_RTPC`** `f32` `+0x2DC` P(2F6,1).
**`District_Skybox/`** (nine 4-float colours) — `West1` `+0x00` · `West2` `+0x10` · `West3` `+0x20` · `West4` `+0x30` · `East1` `+0x40` · `East2` `+0x50` · `East3` `+0x60` · `East4` `+0x70` · `West_Zenith` `+0x80` P(2EC,2). **Quirk:** the four `West*` colours share **P(2EC,0)** and the four `East*` colours share **P(2EC,1)**; because they are assigned in the order `West4,3,2,1` / `East4,3,2,1`, the bit reflects only **`West1`** / **`East1`**.
**Direct children of `Stage`** (each `f32`, read by `0x00DE0200` — C `atof`; the element names are held in a table of pointers at `0x01310F58`–`0x01310FA8`): `particle_ambient_color` (**`vec4`** text "r g b a", `0x00DE03E0`) `+0xE0` P(2ED,0) · `particle_tod_light_color` (`vec4`) `+0xF0` P(2ED,1) · `ldr_min` `+0x110` P(2ED,6) · `ldr_max` `+0x114` P(2ED,7) · `bloom_exposure` `+0x118` P(2EE,0) · `iris_rate` `+0x11C` P(2EE,1) · `luminance_max` `+0x120` P(2EE,2) · `luminance_min` `+0x124` P(2EE,3) · `luminance_mask_max` `+0x128` P(2EE,4) · `eye_adaption_base` `+0x12C` P(2EE,5) · `eye_adaption_amount` `+0x130` P(2EE,6) · `eye_fade_min` `+0x134` P(2EE,7) · `eye_fade_max` `+0x138` P(2EF,0) · `brightpass_threshold_new` `+0x13C` P(2EF,1) · `brightpass_offset_new` `+0x140` P(2EF,2) · `bloom_amount` `+0x144` P(2EF,3) · `bloom_theta` `+0x148` P(2EF,4) · `bloom_slope_A` `+0x14C` P(2EF,5) · `bloom_slope_B` `+0x150` P(2EF,6) · `tonemap_lum_range` `+0x154` P(2EF,7) · `tonemap_lum_offset` `+0x158` P(2F0,0). (Spelling `eye_adaption`, not `adaptation`, is the shipped literal.)
**`Orbital_Objects/Object/`** (repeated) — `Object_Name` matched (`_stricmp`) against the cell's orbital records; unmatched objects are ignored. Per matched record (`0xC0` bytes; `+0x00` name `char[30]`): `Tint` colour `+0x5C` P(rec+0xBC,5) · `Scale` `f32` `+0x6C` P(rec+0xBC,6) · `Opacity` `f32` `+0x70` P(rec+0xBC,7). The record's `+0xB8` (an effect index, initial `0xFFFFFFFF`, which would be set from an effect name at `+0x75` through `0x005C50B0`) is guarded by bit 1 of `+0xBD`, which the initialiser clears and this reader never sets — **that path is dead as far as this reader goes**.

**Two facts on the vocabulary.** (1) The two elements named `?` cannot be authored in XML, so **`+0x1A8` and `+0x1BC` are never written by this table's data** (their presence bits stay clear) — but the mission-override vocabulary of §15 does name those slots (`horizon_layer3_strength`, `overhead_layer3_strength`), and it names the neighbouring `+0x1A0`/`+0x1A4` and `+0x1B4`/`+0x1B8` as `…_layer1/2_strength` where this table says `Cloud_Normal_Map_Height`/`Cloud_Highlight` (§15.3). (2) The `Fog_Ground` … `Star_Strength` group and the `Window_Tint`/`West1` colours use the **RGB-raw + `_Intensity`** convention while `weather.xtbl`'s colours (§2) and `lightning.xtbl`'s overrides (§5.2) use the **RGB ÷ 255** convention (`0x00DAD0A0`) — two different colour encodings in the same engine.

**Cross-check with a second reader.** The mission-override reader `0x00BA0140` (§15) writes the same block at the same offsets (`+0x100`, `+0x104`, `+0x108`, `+0x10C`, `+0x110`…`+0x158`), which independently confirms those offsets. **[CONFIRMED — disassembly.]**


**Review status (2026-09-30): DESK-PASS, text fixes applied (Ground_Reflection spelling, two schema-absent fields annotated); NEEDS-DATA: census of every nested element path in the 4 real segments against these names, and the `Fog_Density` spelling. Data backing so far: Team B `team-b/HANDOFF.md` section 9.109 reads `TOD_Light_Color`, `Ambient_Color`, `Back_Ambient_Color` (raw RGB + `_Intensity`) and `Exposure_Min` from a real cell; the rest is disassembly-only. Direct `Stage` children use C `atof`, whose edge cases are not stated here — desk review (not re-derived from the executable).**

---

## 4. `wind.xtbl` — wind stages **[CONFIRMED — disassembly]**

**Loader** `0x005A93C0` (not guarded by a file-exists test); row reader `0x005A9260`. **Root/row:** `Table` → repeated **`Wind_Stage`**. **Runtime array:** base `0x0140E59C`, stride **`0x28`**, count `0x0140E594`, capacity `0x0140E598`, a `(capacity×9 + 9)×4`-byte scratch block at `0x0140E5A8`, string pool `0x0140E5C0`. Same load procedure as §2 (chance sum, `Next_Stage` resolved by `_stricmp` into pointers into this same array, `Chance` normalised).

| Element path (under `Wind_Stage`) | Type | Offset | Notes |
|---|---|---|---|
| `Name` | interned string | `+0x00` | |
| `Display_Name` | localisation key | `+0x04` | falls back to `Name` |
| `Stage_Settings/Chance` | `f32` | `+0x08` | normalised by the sum |
| `Stage_Settings/Average_Duration` | `f32` | `+0x0C` | |
| `Stage_Settings/Variance` | `f32` | `+0x10` | |
| `Wind_Settings/Average_Intensity` | `f32` | `+0x14` | |
| `Next_Stage_List/Next_Stage` (repeated) | name → entry pointer | count `+0x18`, capacity `+0x1C`, array `+0x20`, unresolved flag `+0x24` | unmatched → `NULL` |


**Review status (2026-09-30): DESK-PASS (stride `0x28` tiles; the section label covers the table and loader, §4.1 states its own lower depth) — desk review (not re-derived from the executable).**

### 4.1 Runtime state machine and accessors (evidence anchors, not a full decode)

The stage table is consumed by a small state machine in `0x005A9640`–`0x005AA580`: `0x005AA170` (per-frame: advances the clock `0x0140E5B4` by the frame time `0x0132A0AC`, then `0x005A9990`), `0x005A9990`/`0x005A9640` (stage selection and transition; the working globals are `0x0140E5A0`–`0x0140E5B8`), `0x005AA580` (reset: zeroes the scratch block, the override vector `0x0140E61C`/`0x0140E624` and flags `0x0140E5AF`, `0x0140E5BC`), `0x005AA3C0`/`0x005AA3F0` (set / clear a **wind override**: a 3-float vector + a scalar), and the two query accessors **`0x005AA1E0(index, flag)`** (returns one of two floats of slot *index* in the array at `0x0140E58C`/`0x0140E588`; ~~`index` −2 returns the constant at `0x012E6BB0`~~ `index` −2 returns the override scalar `0x0140E5BC` when an override is active, else the current blended intensity `0x012E6BB0` (recomputed every tick by `0x005A9990`; 0.5 in the image is only its initial value) **[CORRECTED 2026-10-01]**) and ~~**`0x005AA310(?, out*, index)`** (returns 1 and writes a float; consults `0x005A91A0`, which yields a 3-float wind direction — either the override vector or one computed from two CRT trigonometric calls)~~ **`0x005AA310(dir_out, out*, index)`** (writes the 3-float wind direction to `dir_out` through `0x005A91A0` — the override vector `0x0140E61C`/`0x0140E624` when `0x0140E5AF` is set, else (sin θ, 0, cos θ) of the current heading θ at `0x0140E5B8`; the cosine is `0x00EA3A70` per §17.3, the sine `0x00EA2470` by elimination, HIGH CONFIDENCE — then one float to `out*`: −2 as for `0x005AA1E0`, −1 → 0.0, 0…5 → the first float of pair `index` at `0x0140E58C`; returns 0 when the wind system is not initialised (`0x0140E5AD`) or `index` is outside −2…5) **[CORRECTED — CONFIRMED — disassembly 2026-10-01]**. **[CONFIRMED — disassembly for the accessor bodies; ~~the stage-transition logic in `0x005A9990` was not decoded.~~ the stage-transition logic is decoded below (2026-10-01).]** Callers of the accessors: `0x005AA1E0` ← `0x00563510` (two sites); `0x005AA310` ← `0x0059B9C0`, `0x005CB8B0`, `0x00741940`; the override setters ← `0x00A68730`, `0x00A68AB0`, `0x00A688F0`; the resetter `0x005AA580` ← `0x00708330`; the per-frame tick `0x005AA170` is entered by a jump at `0x00702760`.

**Transition mechanism (2026-10-01, HIGH CONFIDENCE — decompile of `0x005A9990`/`0x005A9640`; the helpers `0x005A95D0`, `0x00DAB6A0`, `0x006FF5D0`/`0x006FF440`, `0x00DAD960` not read).** Working state: `0x0140E5A8` points to a schedule of `0x24`-byte entries (`+0x00` wind-stage entry, `+0x04` duration in milliseconds, `+0x08` heading in radians); `0x0140E5A0` = entry count, `0x0140E5A4` = current entry; `0x0140E5AC`/`0x0140E5B0` = "forced stage" flag and pointer; `0x0140E5B8` = current heading; `0x012E6BB0` = current intensity (written every tick); `0x0140E5AE` = "intensity rising"; `0x0140E5AD` = "initialised"; `0x0140E5AF`/`0x0140E61C`/`0x0140E624`/`0x0140E5BC` = override flag, direction and scalar. Per tick: (1) with no forced stage and an empty schedule, a stage is picked (`0x005A95D0`) and placed in entries 0 and 1; the duration is a random value in `[avg − var·avg, avg + var·avg]` seconds (`Average_Duration` `+0x0C`, `Variance` `+0x10` as a fraction) × 1000; the clock `0x006FF440` is primed with a negative random fraction of it; heading 0 is random in `[0, 2π)`, heading 1 = heading 0 ± a random amount up to π/4; count 2, current 1. (2) Otherwise, when the elapsed time (`0x006FF5D0`) passes the current entry's duration and more entries follow, the schedule advances and the clock is rewound by that duration. (3) On the last entry the last two entries move to the front, a new target stage is picked and `0x005A9640(target)` appends a path; each appended entry gets heading = previous ± up to π/4; if all headings are negative 2π is added to all, if all are ≥ 2π it is subtracted. (4) A forced stage replaces the schedule with that stage twice (random duration, clock reset). (5) Output: a blend factor from elapsed/duration, shifted by ±0.5, clamped to `[0, 1]` and smoothed by `0x00DAD960(t, 1.0, 0.5)`; the intensity is the blend of the two stages' `Average_Intensity` (`+0x14`), the heading the blend of the two entries' headings, and `0x0140E5AE` records whether the intensity rises. `0x005A9640(target)` gives every wind stage (`0x0140E594` count, `0x0140E59C` base, stride `0x28`) a random duration and an infinite cost, then — if the current and target stages differ — runs a shortest-path search over the `Next_Stage_List` edges (`+0x18` count, `+0x20` array) with edge cost |Chance(u) − Chance(v)| × duration(v) (`+0x08` = the normalised `Chance`), walks the predecessors back from the target and appends the path's stages (entry, duration × 1000 ms) to the schedule, bounded by the stage count.


**Review status (2026-09-30): NEEDS-EXE: evidence anchors only; stage-transition logic undecoded (`0x005A9990`, `0x005A9640`, `0x005AA310`, `0x005A91A0`) — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`): accessor bodies CONFIRMED — disassembly with two CORRECTIONS (`0x012E6BB0` is the live intensity, not a constant; `0x005AA310`'s first argument is the direction output); transition logic decoded at HIGH CONFIDENCE (schedule of stage/duration/heading entries, `Chance`-weighted shortest path over `Next_Stage_List`) — not cleared: `0x005A95D0` (target choice), `0x00DAB6A0`, `0x00DAD960`, `0x006FF5D0`/`0x006FF440`.**

### 4.2 Cross-check with the tree wind decode (`spec-tree-format.md` §13) — **not connected [CONFIRMED — disassembly, for the stated predicate]**

`spec-tree-format.md` §13 records the global tree wind level `0x0130CBBC` (static `0.1`), the wind manager `0x02706800` (per-frame `0x00A70CA0`, level-change hook `0x00A6F650`) and per-tree 38-float wind objects loaded from the `'TREE'` file. The question was whether `wind.xtbl`'s stages drive any of these. **Predicate tested:** the complete reference list of the wind-stage globals `0x0140E594`–`0x0140E5C4` (15 functions, all listed by `tools/ak_refs_wind.txt`) and of the tree-wind level block `0x0130CBB8`–`0x0130CBC4` (9 references in `0x00851B10`, `0x00A70CA0`, `0x00A70D70`, `0x00A71250`, `0x00A71810`). **Result:** the two sets of functions are disjoint; none of the wind-stage functions or their three callers-of-accessors lie in the tree-wind manager's code range (`0x00A6F000`–`0x00A71400`) or in the foliage sway function `0x00851B10`, and `0x0130CBBC` still has no writer. `0x00A70D70` (writes `0x0130CBB8`) is the foliage "growth" enable, called once from `0x0072A893`, and `0x00A71250` (writes `0x0130CBC0`, a float scale it re-applies to every registered tree object) is called from `0x005DD2C0`. **Conclusion for this predicate:** the tree wind level and per-tree wind objects are **not** driven by `wind.xtbl` (nor, by the same test, by `weather.xtbl`'s `Wind_Settings`, whose destination `+0x80`–`+0x8C` has no reader in the tree code). This does not exclude an indirect path through a pointer-mediated write; it excludes any direct one. The one remaining route is that the accessors in §4.1 feed *particle* and *audio* wind (their callers are effect/rain code), not vegetation. **[OPEN — desk review 2026-09-30: the 9 tree-wind references come from `spec-tree-format.md` §13, which carries its own desk-review conflict about the wind manager layout (`0x02706800`); the negative result should be re-run once that is settled; to be settled against the executable.]**


**Review status (2026-09-30): NEEDS-EXE: negative result rests on the tree spec's reference list, itself under review — desk review (not re-derived from the executable).**

## 5. Rain and lightning

### 5.1 `rain.xtbl` — rain intensity levels **[CONFIRMED — disassembly]**

**Loader** `0x0059B570`. **Root/row:** `Table` → repeated **`Level`**, each with one **`Parameters`** child. **Runtime array:** base `0x013E767C`, stride **`0x38`**, count `0x013E7678`, capacity `0x013E7674`. After loading, the array is **sorted ascending by `+0x00` (density)** with `qsort` (comparator `0x0059B540`, verified: it compares the first `f32` of each element). Same re-entrancy rule as §2.

| Element path (under `Level/Parameters`) | Type | Offset | Notes |
|---|---|---|---|
| `Density` | **integer**, stored as `f32` = value ÷ 100.0 | `+0x00` | the only integer element; the sort key |
| `View_Radius` | `f32` | `+0x04` | |
| `Speed` | `f32` | `+0x08` | |
| `Opacity` | `f32` | `+0x0C` | |
| `Length_Near`, `Length_Far` | `f32` | `+0x10`, `+0x14` | |
| `Width_Near`, `Width_Far` | `f32` | `+0x18`, `+0x1C` | |
| `Splash_Lifetime` | `f32` | `+0x20` | |
| `Splash_Size_Near`, `Splash_Size_Far` | `f32` | `+0x24`, `+0x28` | |
| `Wind_Amount` | `f32` | `+0x2C` | |
| `Effect` | effect name → **effect index** (`0xFFFFFFFF` if absent or unknown) | `+0x30` | `0x005C50B0` (§10.1) |
| `Camera_drop_effect` | as `Effect` | `+0x34` | spelled `Camera_drop_effect` (lower-case `drop`) |


**Review status (2026-09-30): DESK-PASS (stride `0x38` tiles; the `Parameters` child names are backed one level by Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`" (row-level gate passed); `Density` signedness: see the §1.4 OPEN note) — desk review (not re-derived from the executable).**

### 5.2 `lightning.xtbl` — lightning types **[CONFIRMED — disassembly]**

**Loader** `0x005993B0`; row reader `0x005991C0`. **Root/row:** `Table` → repeated **`Lightning_Type`**. **Runtime array:** base `0x013DEB78`, stride **`0x48`**, count `0x013DEB70`, capacity `0x013DEB74`; the sum of all `Probability` values is accumulated in `0x013DEB90` (not normalised in the entries).

| Element path (under `Lightning_Type`) | Type | Offset | Notes |
|---|---|---|---|
| `Name` | string | **not stored** | read and discarded by the row reader |
| `Probability` | `f32` | `+0x00` | |
| `VFX_List/VFX` (repeated; text = a **skybox-effect name**) | name → skybox-effect index | count `+0x04`, capacity `+0x08`, array pointer `+0x0C` | the name is CRC-32'd and matched against `skybox_effects.xtbl`'s `Name` hash (`0x005C3980`, §9.1); **names that do not resolve are dropped** (the count only advances for resolved ones) |
| `TOD_Overrides/Fog_Color_Override` | colour (`R,G,B` ÷ 255) | `+0x10`..`+0x18` | presence bit `+0x44` bit 0 |
| `TOD_Overrides/Fog_Strength_Override` | `f32` (the element's own text) | `+0x1C` | `+0x44` bit 1 |
| `TOD_Overrides/Ambient_Override` | colour | `+0x20`..`+0x28` | `+0x44` bit 2 |
| `TOD_Overrides/Cloud_Brightness_Override` | `f32` (own text) | `+0x2C` | `+0x44` bit 3 |
| `TOD_Overrides/Cloud_Contrast_Override` | `f32` (own text) | `+0x30` | `+0x44` bit 4 |
| `TOD_Overrides/Sky_Brightness_Override` | `f32` (own text) | `+0x34` | `+0x44` bit 5 |
| `TOD_Overrides/TOD_Light_Override` | colour | `+0x38`..`+0x40` | `+0x44` bit 6 |

The `+0x44` byte is the entry's presence mask for the override group (set only when the element exists). These are the per-flash overrides of the lighting record of §3.3 (fog, ambient, cloud, sky, TOD light) — same parameter families, different (÷255) colour encoding. The link from weather to lightning is `weather.xtbl` `Rain_Settings/Lightning_Settings/{Frequency,Variance}` (§2); how a `Lightning_Type` is picked from `Probability` at run time was **not** traced.


**Review status (2026-09-30): DESK-PASS (stride `0x48` tiles; the 3 real `VFX` names resolve against real skybox names, §17.8) — desk review (not re-derived from the executable).**

## 6. Lens flares, motion blur, radial blur

### 6.1 `lens_flares.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x005A10E0`. **Root/row:** `Table` → repeated **`Flare`**. **The whole table is ignored if it holds more than 8 `Flare` rows** (the guard is on the row count before any row is read). **Runtime array:** base `0x013F0100`, stride **`0x10`**, count `0x014032D8` (reset to 0 at entry).

| Element path (under `Flare`) | Type | Offset | Notes |
|---|---|---|---|
| `ImageFilename/Filename` | file name → **texture handle** (`0x00DCECC0(name, 0)`) | `+0x00` | note the extra `Filename` level (accessor `0x00DAC880`) |
| `Radius` | `f32` | `+0x04` | |
| `Scale` | `f32` | `+0x08` | |
| `BaseAlpha` | `f32` | `+0x0C` | |
| `Name` **[added 2026-09-30, desk review]** | string | not stored | present in every real `Flare` row (Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "every real `Flare` row (6/6) carries an undocumented `<Name>` element"); not in the loader's element list, so not read (§16 lists this table as complete) |


**Review status (2026-09-30): DESK-PASS, text fixes applied (`Name` added as present, unread; Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": 6/6) — desk review (not re-derived from the executable).**

### 6.2 `motion_blur.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x0059A800`. **Root:** `Table` → one **`Motion_Blur_Settings`** element (not repeated; if absent the table is a no-op) whose named children are fixed groups. There is **no row array**: every value goes to its own global `f32`. All values use `0x00DACCB0` (always-write); `Fake_Velocity_Scale` uses `0x00DACE40` (**one value replicated into four consecutive floats**, the destination being a `vec4`).

| Group (child of `Motion_Blur_Settings`) | Elements → destination |
|---|---|
| `On_Foot_Walk_Run` | `Strength` `0x012E6888` · `Max_Offset` `0x012E688C` |
| `On_Foot_Sprint` | `Strength` `0x012E6890` · `Max_Offset` `0x012E6894` |
| `On_Foot_Freefall` | `Strength` `0x012E6898` · `Max_Offset` `0x012E689C` · `Min_Fall_Velocity` `0x012E68A0` · `Max_Fall_Velocity` `0x012E68A4` |
| `On_Foot_Explosion` | `World_Strength` `0x012E68A8` · `Target_Strength` `0x012E68AC` · `Max_Offset` `0x012E68B0` |
| `Vehicle_Base` | `World_Strength` `0x012E68B4` · `Target_Strength` `0x013E7584` · `Max_Offset` `0x012E68B8` · `Fake_Velocity_Scale` `0x013E7590`–`0x013E759C` |
| `Airplane_Base` | `World_Strength` `0x012E68BC` · `Target_Strength` `0x013E7588` · `Max_Offset` `0x012E68C0` · `Fake_Velocity_Scale` `0x013E75A0`–`0x013E75AC` |
| `Helicopter_Base` | `World_Strength` `0x012E68C4` · `Target_Strength` `0x013E758C` · `Max_Offset` `0x012E68C8` · `Fake_Velocity_Scale` `0x013E75B0`–`0x013E75BC` |
| `Vehicle_Nitrous` | `World_Strength` `0x012E68CC` · `Target_Strength` `0x012E68D0` · `Max_Offset` `0x012E68D4` · `Duration` `0x012E68D8` · `Decay_Time` `0x012E68DC` · `Fake_Velocity_Scale` `0x013E75C0`–`0x013E75CC` |
| `Vehicle_Peelout` | `World_Strength` `0x012E68E0` · `Target_Strength` `0x012E68E4` · `Max_Offset` `0x012E68E8` · `Duration` `0x012E68EC` · `Decay_Time` `0x012E68F0` · `Fake_Velocity_Scale` `0x013E75D0`–`0x013E75DC` |

(The three `Target_Strength` values of `Vehicle_Base`/`Airplane_Base`/`Helicopter_Base` live in the `0x013E758x` block, apart from the `0x012E68xx` run — the layout is what the code writes; no meaning is inferred from it.) The shipped tuning values are in a base-game container and were not read. **[Desk review 2026-09-30: the real `Motion_Blur_Settings` block also carries a `Name` child (Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": 1/1) that is not in the loader's element list, so it is not read.]**


**Review status (2026-09-30): DESK-PASS, text fixes applied (`Name` noted as present, unread); global addresses tile contiguously — desk review (not re-derived from the executable).**

### 6.3 `radial_blur.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x005CE140`. **Root/row:** `Table` → repeated **`Radial_blur`**. **Runtime array:** base `0x0148A418`, stride **`0x58`**, count `0x0148E8D8` (reset to 0 at entry). The loader has **no capacity check**; the count variable sits `0x44C0` bytes after the base, i.e. the array has room for 200 entries **[HIGH CONFIDENCE — layout, not exercised]**.

| Element (child of `Radial_blur`) | Type | Offset | Notes |
|---|---|---|---|
| `Name` | `char[0x40]` (bounded copy) | `+0x00` | looked up by `_stricmp` from `vfx.xtbl` `Radial_blur/Radial_blur_entry` (`0x005CE2D0`) |
| `Strength` | `f32` | `+0x44` | |
| `Duration` | `f32` | `+0x48` | |
| `Radius` | `f32` | `+0x4C` | |
| `Distance_fade` | `f32` | `+0x50` | |
| `Priority` | `s32` (`0x00DABC70`, signed, always-write — 2026-10-01) | `+0x54` | |

**[Desk review 2026-09-30: capacity arithmetic checks — `0x0148E8D8 − 0x0148A418 = 0x44C0 = 17,600 = 200 × 0x58`.]** ~~**[OPEN — desk review 2026-09-30: bytes `+0x40`–`+0x43` (after the `char[0x40]` name, before `Strength`) are not described — padding or an unread field; `0x005CE140`; to be settled against the executable.]**~~ **[CONFIRMED — disassembly 2026-10-01: `+0x40`–`+0x43` are not written by the loader (padding, zero in the static array); `Name` is copied with bound `0x40` (NUL forced at `+0x3F`); `Priority` is read with the signed always-write accessor `0x00DABC70`.]**

Names seen referenced by the DLC `vfx` samples (§14): `small`, `smaller`, `large`, `Quick large`.


**Review status (2026-09-30): DESK-PASS, text fixes applied (capacity arithmetic shown); NEEDS-EXE: `+0x40`–`+0x43` — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x005CE140`): layout, accessors and the `+0x40` padding CONFIRMED — disassembly; the capacity 200 remains layout-derived (HIGH CONFIDENCE) — cleared for implementation.**

## 7. `dof_situations.xtbl` — depth-of-field situations

### 7.1 Table and row schema **[CONFIRMED — disassembly]**

**Loader** `0x0058EC80(update)` — one function with a mode flag: `0` = initial load, non-zero = update-in-place. **Root/row:** `Table` → repeated **`DOF_situation`**. **Runtime array:** base `0x013CF4DC`, stride **`0x24`**, count `0x013CF4D8`; allocated as `count × 0x24` in the initial mode. Rows are keyed by the **CRC-32 (seed 0, lower-cased) of the row's `name`** child (the element is looked up case-insensitively, so `Name` also works).

| Element (child of `DOF_situation`) | Type | Offset |
|---|---|---|
| `name` | CRC-32 key | `+0x00` |
| `Start_Multiplier_A` | `f32` | `+0x04` |
| `Start_Multiplier_B` | `f32` | `+0x08` |
| `End_Multiplier_A` | `f32` | `+0x0C` |
| `End_Multiplier_B` | `f32` | `+0x10` |
| `Blur_Radius` | `f32` | `+0x14` |
| `Transition_Speed` | `f32` | `+0x18` |
| `Human_Spherecast_Radius` | `f32` | `+0x1C` |
| *(derived)* "Advanced" variant | entry pointer | `+0x20` |

**The `" Advanced"` pairing.** A row whose `name` contains the substring `" Advanced"` is the high-quality variant of the row whose name is the same text with that suffix removed. Whichever row of a pair is processed second sets the **base row's `+0x20` to point at the Advanced row** (both orders work: the hash of `"<name> Advanced"` is searched when a base row is read; the hash of the truncated name when an Advanced row is read). **In update mode (non-zero flag) a row whose `name` is not already in the table is dereferenced as `NULL`** — the update path assumes the row set is unchanged. **[CONFIRMED — disassembly; the crash path is HIGH CONFIDENCE, not exercised.]**


**Review status (2026-09-30): DESK-PASS (stride `0x24` tiles; real data has exactly two base/Advanced pairs, §17.1) — desk review (not re-derived from the executable).**

### 7.2 Selection **[CONFIRMED — disassembly]**

`0x0058EF60(hash)` makes a situation current: it finds the row by CRC (`0x0058EC40`, linear scan) and stores it at `0x013CF4E0`; **if the graphics-option predicate `0x006948C0` is true and the row has an Advanced variant (`+0x20 ≠ 0`), the Advanced row is used instead.** `0x0058EBC0` clears the current situation. Callers found by name: `0x00B6FB90` selects **`Satellite`** (hashes the literal), `0x009DCDB0` selects **`Fine Aim`**; ~~two more callers (`0x009E6510`, `0x00A93070`) select others (names not recovered)~~ `0x009E6510` (a player zoom-mode toggle) selects **`Zoom`** (literal `0x01176234`, hash cached in `0x0263B10C`) on entering zoom for the local player and clears the situation (`0x0058EBC0`) on leaving it; `0x00A93070` (entering a remote-control gun mode) selects **`RC Gun`** (literal `0x01180F8C`) — no base-game row (§17.1), so a no-op like `Satellite` **[CONFIRMED — disassembly 2026-10-01]**. The clear routine `0x0058EBC0` is called from `0x009DCF10`, `0x009E6510`, `0x00A93320`, `0x00B709D0`, `0x00B700D0` and `0x00702FA4`.


**Review status (2026-09-30): NEEDS-EXE: names selected by `0x009E6510` and `0x00A93070`; meaning of the predicate `0x006948C0` — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`): the four caller names are CONFIRMED — disassembly (`Satellite`, `Fine Aim`, `Zoom`, `RC Gun`) — not cleared: the graphics-option predicate `0x006948C0`.**

### 7.3 Per-frame evaluation (`0x0058F2D0`) **[CONFIRMED — disassembly for the arithmetic; HIGH CONFIDENCE for the meaning of the distance]**

With no current situation the outputs `0x013CF4E4`–`0x013CF4F4` are zeroed. Otherwise a **focus distance `d`** is formed ~~(the distance from the camera position `0x013C8740` to the situation's subject point; two candidate subject points are chosen between by `0x0058F000`, not decoded)~~ (the distance from the camera position `0x013C8740`–`0x013C8748` to the subject point: the nearest on-screen targetable object's position when `0x0058F000` finds one, else the point from `0x009CB3C0`) and low-pass filtered with the row's `Transition_Speed` `T`: if `T > 0`, the previous frame's `d` (kept in `0x013CF4F8`) and the target `d` are mapped through `u = T·(1 − 1/(1 + d/100))`, the mapped value is moved toward the target by `frame_time / T` per frame (`frame_time` = `0x0132A0AC`), and the result is mapped back through `d = (1/(1 − u/T) − 1)·100`; a difference smaller than one step snaps to the target. ~~**[OPEN — desk review 2026-09-30: the behaviour for `T ≤ 0`, the exact snap test and the subject-point choice (`0x0058F000`) are not given; and the camera position global `0x013C8740` here differs from the camera output position `0x013C8750` in `spec-cutscene-camera-format.md` §10.2 and §10.5 (camera output position) — conflict with another spec, not resolved here; `0x0058F2D0`, `0x0058F000`; to be settled against the executable.]**~~ **[CONFIRMED — disassembly 2026-10-01: with `T ≤ 0` there is no filtering (`d` = the target distance); with `T > 0` the step `frame_time / T` is taken only when it is ≤ |u_prev − u_target|, otherwise `d` snaps to the target; `0x013CF4F8` keeps the `d` actually used. The subject point is the position (`+0x40`–`+0x48`) of the nearest on-screen "targetable" object found by `0x0058F000`: objects of the world table `0x03171A64` (count `+0x1EC`, index list `+0x1E4`, pointer table `+0x58`) other than the player, of the player's `+0x16C0`/`+0x16C4` group when that pair is non-zero, with bit 3 of `+0x33`, not excluded by `0x0096F4F0`, whose projection lies within half the screen of its centre — the nearest to `0x013C8750`; with no such object, the point returned by `0x009CB3C0(out, 0, 0)` (not read — likely the aim point, HYPOTHESIS). `0x013C8740` and `0x013C8750` are two adjacent 16-byte camera vectors written together by the camera update functions (both written by `0x00564C20`, `0x00565A40`, `0x00566620`, `0x00566A40`, `0x0057EB10`, `0x00580260`, `0x008134D0`, `0x00813A80`, `0x008140A0`; the one dumped, `0x00564C20`, stores the same vector into both and records in `0x013C8738` the largest distance between the old `0x013C8740` and the new vector), so the cutscene spec's `0x013C8750` and this section's `0x013C8740` do not conflict — HIGH CONFIDENCE that both hold the camera position; whether another writer ever makes them differ is OPEN.]** Then

| Output global | Value |
|---|---|
| `0x013CF4E4` | `Start_Multiplier_A × d` |
| `0x013CF4E8` | `Start_Multiplier_B × d` |
| `0x013CF4EC` | `End_Multiplier_A × d` |
| `0x013CF4F0` | `End_Multiplier_B × d` |
| `0x013CF4F4` | `Blur_Radius` (unscaled) |


**Review status (2026-09-30): NEEDS-EXE: `T ≤ 0` branch, snap rule, subject point, camera-position global conflict. The filter mapping and its inverse are exact inverses — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x0058F2D0`, `0x0058F000`, xrefs of `0x013C8740`/`0x013C8750`, `0x00564C20`): `T ≤ 0` and the snap rule CONFIRMED — disassembly; subject point HIGH CONFIDENCE; camera-position conflict with `spec-cutscene-camera-format.md` resolved (the same vector is written to both globals) — not cleared: `0x009CB3C0` (fallback point), the other writers of `0x013C8740`/`0x013C8750`.**

### 7.4 Cross-check with the cutscene DOF channels (`spec-cutscene-camera-format.md` §10) **[CONFIRMED — disassembly]**

The render-packet builder `0x005E14A0` fills the packet's DOF block from **one of three sources, tested in this order**: (1) if the byte `0x0149AB40` is set (a debug override) it writes the fixed constants at `0x012EC26C`–`0x012EC27C` (the engine's debug defaults `1, 3, 10, 12` and `0.5` recorded in `spec-cutscene-camera-format.md` §10.5) and skips the rest; (2) if a situation is current (`0x0058EC30`: `0x013CF4E0 ≠ 0`) it calls the getter `0x0058EBD0`, which copies `0x013CF4E4 → packet +0x4F4`, `0x013CF4E8 → +0x4F8`, `0x013CF4EC → +0x4FC`, `0x013CF4F0 → +0x500`, `0x013CF4F4 → +0x504` and sets the enable byte `+0x4F1 = 1`; (3) otherwise it copies the **cutscene block** `0x013C8FD0` (enable), `0x013C8FD4`–`0x013C8FE0` (channels 4–7), `0x013C8FE4` (channel 3) into the same `+0x4F1`, `+0x4F4`…`+0x500`, `+0x504`. So:

- **A gameplay DOF situation has priority over the cutscene channels** — the situation block is tested first (after the debug override).
- **The four situation outputs occupy exactly the slots the cutscene channels 4–7 fill (`Focal_params.xyzw`)**, and **`Blur_Radius` occupies the slot of channel 3 (`+0x504`, the CoC scale)**. This is the tie the task asked for: `Start_Multiplier_A/B` and `End_Multiplier_A/B` (× the focus distance) are the four depth breakpoints of `Focal_params` (`spec-cutscene-camera-format.md` §10.5 already established `ch4 ≤ ch5 ≤ ch6 ≤ ch7`; here the gameplay path supplies the same four numbers as multiples of one distance). That the *packet slots* coincide is CONFIRMED; that `Blur_Radius` and channel 3 are the same physical quantity is inferred from the shared slot only (**HIGH CONFIDENCE — inferred**). The shader's use of the four values (`ThreeLayer`/`TwoLayer`) is in the effect files, not the executable (`spec-cutscene-camera-format.md` §10.9).


**Review status (2026-09-30): DESK-PASS (slots and the `0x013C8FD0`–`0x013C8FE4` cutscene block agree with `spec-cutscene-camera-format.md` §10.5) — desk review (not re-derived from the executable).**

## 8. `refraction_situations.xtbl` — refraction (heat-haze / shock) situations

**Loader** `0x0059EFB0(framework_filter, update_mode)`; DLC driver `0x0059F7B0(framework list)`. **Root/row:** `Table` → repeated **`refraction_situation`**. **Runtime array:** base `0x013EF5C0`, stride **`0x40`**, total count `0x013EF58C`, base-game row count `0x013EF590` (recorded on the first load). **[CONFIRMED — disassembly.]**

| Element (child of `refraction_situation`) | Type | Offset | Notes |
|---|---|---|---|
| `name` | CRC-32 key (seed 0, lower-cased) | `+0x00` | matched case-insensitively (`Name` in the data); the key `effects.xtbl` `Proximity_Refraction/Situation` hashes to (§10.1) |
| `Scale` | `f32` | `+0x04` | |
| `Frequency` | `f32` | `+0x08` | |
| `Offset_Delta` | `f32` | `+0x0C` | |
| `Fade_In_Time` | `f32` | `+0x10` | |
| `Fade_Out_Time` | `f32` | `+0x14` | |
| `Duration` | `f32`, **optional** | `+0x18` | absent → **−1.0** (the constant at `0x012A2D54`) |
| `Spasm/Spasm_Time_Min` | `f32` | `+0x1C` | the whole `Spasm` block is optional: absent → `+0x1C`…`+0x38` are all set to 0 |
| `Spasm/Spasm_Time_Max` | `f32` | `+0x20` | |
| `Spasm/Frequency_Min` | `f32` | `+0x24` | |
| `Spasm/Frequency_Max` | `f32` | `+0x28` | |
| `Spasm/Scale_Min` | `f32` | `+0x2C` | |
| `Spasm/Scale_Max` | `f32` | `+0x30` | |
| `Spasm/Duration_Min` | `f32` | `+0x34` | |
| `Spasm/Duration_Max` | `f32` | `+0x38` | |
| `Framework` | filter only | not stored | default `main`; see below |

**Framework handling.** With a non-`NULL` filter a row is accepted only when its `Framework` (default `"main"`) equals the filter (`_stricmp`); with a `NULL` filter every row is accepted. The DLC driver `0x0059F7B0` walks the framework list, and for each framework whose descriptor has byte `+0x101` not `0`/`0xFF` **and** bit `0x02` of byte `+0x103` set, formats the file name `<framework>_refraction_situations.xtbl` (§1.5) and loads it in **append mode** (`update_mode = 0`: the count is *added to*, rows are appended after the existing ones; the count is advanced by the file's full `refraction_situation` row count before filtering, so rows rejected by the framework filter leave unused zero entries inside the count; the base-game count `0x013EF590` is recorded only when the array was empty or in update mode — CONFIRMED — disassembly 2026-10-01). In update mode (non-zero) the count is reset and rows are matched to existing entries by CRC instead of appended (an unmatched row dereferences a `NULL` entry — HIGH CONFIDENCE 2026-10-01, not exercised). **[CONFIRMED — disassembly + CONFIRMED — empirical: the one raw DLC table is `dlc2_refraction_situations.xtbl`, one row `dlf_test`, `<Framework>dlc2</Framework>`, `<Scale>10</Scale>`, a full `Spasm` block; every element of it is in the table above, §14.]** The `Duration` element is absent from that sample, confirming that it is optional data. **[Desk review 2026-09-30: real data now confirms both optional parts — Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "`Duration` present 4/27, `Spasm` present 13/27, `Framework` present exactly 1/27". The −1.0 default itself rests on disassembly only.]** **[OPEN — desk review 2026-09-30: ~~bytes `+0x3C`–`+0x3F` of the `0x40` stride are not described (padding or an unread field; `0x0059EFB0`); and~~ **[2026-10-01: bytes `+0x3C`–`+0x3F` are not written by the loader (padding, zero in the static array) — CONFIRMED — disassembly]**; the `Spasm` child names have not been checked against the 13 real rows that carry the block; to be settled against the executable / real data.]**


**Review status (2026-09-30): DESK-PASS, text fixes applied (Team B optionality figures cited); NEEDS-EXE: `+0x3C`–`+0x3F`; NEEDS-DATA: `Spasm` child census — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x0059EFB0`): layout, the `name` literal (lower case), the −1.0 `Duration` default (`0x012A2D54`), the `Spasm` zeroing, the `+0x3C` padding and the framework filter CONFIRMED — disassembly; append-count and update-mode behaviour added — cleared for implementation. NEEDS-DATA: `Spasm` child census (unchanged).**

## 9. Skybox effects, time-of-day objects, external light overrides

### 9.1 `skybox_effects.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x005C36D0`. **Root/row:** `Table` → repeated **`Skybox_Effect`**. **Runtime array:** base `0x0143C7A8`, stride **`0x1C`**, count `0x0143C79C`. Rows are **appended after the existing count** (the loader does not reset it) and there is **no capacity check** and no framework filter.

| Element path (under `Skybox_Effect`) | Type | Offset | Notes |
|---|---|---|---|
| `Name` | CRC-32 (seed 0, lower-cased) of the text | `+0x00` | the key `lightning.xtbl` `VFX_List/VFX` resolves through (`0x005C3980`, linear scan on this field) |
| `Effect` | effect name → **effect index** (`0x005C50B0`, §10.1) | `+0x04` | `0xFFFFFFFF` if unknown |
| `Auto_Spawn` | presence flag | `+0x1B` bit 0 | absent → `+0x08` and `+0x0C` set to 0 |
| `Auto_Spawn/Min_Time_Spacing` | `f32` | `+0x08` | |
| `Auto_Spawn/Max_Time_Spacing` | `f32` | `+0x0C` | |
| `Random_Orientation` | `bool` | `+0x1B` bit 1 | always-write bool (hazard, §1.4) |
| `TODRange/Start_Time` | integer (HHMM) narrowed to `u16` | `+0x14` | absent `TODRange` → **0** |
| `TODRange/End_Time` | integer (HHMM) narrowed to `u16` | `+0x16` | absent `TODRange` → **`0x0937` = 2359** |
| `Weather_Stage` | stage name → **index in `weather.xtbl`** (`0x005A8C20`, `_stricmp`) | `+0x18` `u16` | absent or unmatched → **`0xFFFF`** (any weather) |
| `Skybox_Layer` | integer, low byte kept | `+0x1A` `u8` | |

`+0x10` is not written by the loader (a run-time slot; its use is not traced).


**Review status (2026-09-30): DESK-PASS (stride `0x1C` tiles; `0x0937` = 2359; Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "`Weather_Stage` absent in 8/8 real rows", so only the `0xFFFF` default path is exercised) — desk review (not re-derived from the executable).**

### 9.2 `time_of_day_objects.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x005A12E0`; row reader `0x005A11C0`. **Root/row:** `Table` → repeated **`Object_Type`**. **Runtime array:** base `0x01403260`, stride **`0x18`** (records of three `f32` used; five records). Before reading, **all five records are pre-set** to a default pair `(on, off)` ~~returned by `0x00BA2720` (the two values are produced by a group of stub functions that return immediately — effectively constants of the build)~~ returned by `0x00BA2720`: two HHMM integers read from the active time-of-day definition object (the global at `0x0290FBB0`, or the mission-override object `0x006E1D30()+0x2F8` when `0x006E1D60()` is true) at `+0x5744` and `+0x5748` (globally `0x029152F4`/`0x029152F8`, written only by the TOD-definition reader `0x00BB6400`), each converted exactly like `Start_Time` (§3.1; truncating division, no wrap) **[CORRECTED — CONFIRMED — disassembly 2026-10-01; the authored element names behind the two fields are OPEN — likely `day_begin`/`day_end` of §15.2]** and a default variation of `0x0111E4B0 = 10 / 1440`.

| Element (child of `Object_Type`) | Type | Destination |
|---|---|---|
| `Name` | enum via `0x00DAC830`, **case-insensitive**: `Street Lights` 0 · `Headlights` 1 · `Windows` 2 · `Distant Vehicle Headlights` 3 · `Searchlights` 4 | selects the record (`+0x18 × index`) |
| `On_Time` | integer **HHMM** → `f32` fraction of a day = `(hh×60 + mm)/1440` | record `+0x00` |
| `Off_Time` | integer HHMM → fraction of a day | record `+0x04` |
| `Variation` | integer **minutes** → `minutes/1440` | record `+0x08` |

An unrecognised `Name` gives index `−1` and the row is written **six dwords before the array** (there is no range check). **[CONFIRMED — disassembly for the missing check; not exercised.]** A row with no `Name` element is skipped. ~~**[OPEN — desk review 2026-09-30: the default `(on, off)` pair returned by `0x00BA2720` is not given; since `Street Lights` and `Searchlights` have no real row (§17.1) those records run on that default for the whole game, so it is needed to reproduce them; to be settled against the executable.]**~~ **[2026-10-01: the default pair comes from data, not code — see the loader paragraph; which of the two is "on" needs the caller `0x005A12E0`.]**

**FOR TEAM B (2026-10-01, CORRECTION):** the `time_of_day_objects` default `(on, off)` pair is not a build constant; it is read from the time-of-day definition data (`+0x5744`/`+0x5748` of the TOD-definition object, probably `day_begin`/`day_end`), so `Street Lights` and `Searchlights` follow that data.


**Review status (2026-09-30): DESK-PASS, text fixes applied (missing default noted); NEEDS-EXE: `0x00BA2720` and its stubs. Name enum backed by Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`" (enum gate: no anomalies) — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x00BA2720`): default source CORRECTED (TOD-definition fields `+0x5744`/`+0x5748` — data, not constants) — not cleared: `0x00BB6400` (which elements fill them), `0x005A12E0` (on/off order), `0x006E1D60`/`0x006E1D30`.**

### 9.3 `external_light_override.xtbl` **[CONFIRMED — disassembly for the reader; OPEN for the consumers]**

**Loader** `0x00A72B80` = reset (`0x00A723B0`) then the generic reader `0x00A72AC0(filename)`. The table is **optional** (file-exists guard); on success the flag `0x02718E59` is set to 1. **Root/row:** `Table` → repeated **`light_override`**.

| Element (child of `light_override`) | Type | Effect |
|---|---|---|
| `Name` | string | the **key prefix** |
| `front_color` | text `"r g b [a]"` (`sscanf("%f %f %f %f")`, unspecified components stay 1.0); only RGB is kept | stored in record `"<Name>-front_color"` |
| `back_color` | same text form | stored in record `"<Name>-back_color"` **after the RGB has been multiplied by the row's `back_intensity`** (`0x00DC5210(node, 1.0)`; absent → ×1.0). `front_color` is **not** multiplied. |
| `front_intensity` | `f32` (C `atof`, default 1.0) | stored in record `"<Name>-front_intensity"` |
| `back_intensity` | `f32` | stored in record `"<Name>-back_intensity"` (in addition to being the `back_color` multiplier) |

Each element lands in a **named record found or created by the string `"<Name>-<element>"`** (`sprintf("%s-%s")`): `0x008DF740` for colours (a `0x1C`-byte record `{u32 slot-mask; vec3 slot0; vec3 slot1}`), `0x008DF690` for floats (a `0x0C`-byte record `{u32 slot-mask; f32 slot0; f32 slot1}`); the pool is indexed by a hash of the key string (`0x00DA7890`, the rotate/XOR string hash) over a 128-entry index table at `0x0130D130` (`0x0130CE2D`–`0x0130CEAC` = a free/used byte map initialised to `0xFF`, `0x0130CEAD` an ascending 1..0x7F run — the reset routine). The loader always passes **slot 1** (literal argument 1 at all four call sites in `0x00A72AC0` — CONFIRMED — disassembly 2026-10-01) and sets bit 1 of the slot mask: **the override is written into slot 1; slot 0 is left to the record's owner** **[HIGH CONFIDENCE — inferred from the slot argument and mask bit]**. Which subsystem owns the records (a light-management pool keyed by light name) and reads slot 1 was **not** traced — **OPEN**. ~~**[OPEN — desk review 2026-09-30: the rotate/XOR hash `0x00DA7890` is named but not specified (rotation direction and amount, initial value, case handling, reduction to the 128-entry index), so bucket order cannot be reproduced from this text; to be settled against the executable.]**~~ **[CONFIRMED — disassembly 2026-10-01: `0x00DA7890` starts from 0 and, for each byte up to the NUL, sign-extends it, folds `A`–`Z` to lower case (+`0x20`), rotates the 32-bit hash left by 6 and XORs the byte in (bytes ≥ `0x80` therefore XOR in with the upper 24 bits set); a `NULL` string hashes to `0xFFFFFFFF`. The reduction to the 128-entry index is inside `0x008DF740`/`0x008DF690` (not read).]**


**Review status (2026-09-30): NEEDS-EXE: hash algorithm `0x00DA7890`; consumer of slot 1 (`0x00A72B80` callers). Record sizes `0x1C`/`0x0C` tile — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`): hash algorithm, reader skeleton and the slot-1 literal CONFIRMED — disassembly — not cleared: `0x00A72720`/`0x00A72A00` (value parsing), `0x008DF740`/`0x008DF690` (bucket reduction), the slot-1 consumer (`0x00A72B80`'s only caller is the start-up routine `0x00707C80`).**

## 10. `effects.xtbl` and `vfx.xtbl` — effect instances and the effect-file binding

These two tables are a pair: **`effects.xtbl` names a gameplay/effect entry and binds it to a `vfx.xtbl` visual and/or a sound; `vfx.xtbl` binds a visual name to a `.cefct_pc` effect resource (§10.3).** Both are extended by DLC frameworks. One-time initialisation is `0x005CE8D0` (guarded by a done-flag `0x0148E8E0`): pool set-up, then `radial_blur.xtbl` (§6.3), then `vfx.xtbl` (`0x005CE760`), then clear the effects "in use" marks (`0x005C4340`), then `effects.xtbl` for the `main` framework (`0x005C4B10("effects.xtbl", "main", 0)`). The DLC driver `0x005C5160` repeats the vfx and effects loads per framework (below).

### 10.1 `effects.xtbl` **[CONFIRMED — disassembly + CONFIRMED — empirical (§14)]**

**Loader** `0x005C4B10(table name, framework, start slot)`. **Root/row:** `Table` → repeated **`Effect`**. **Runtime array:** base `0x0143CB38`, stride **`0x98`**, **capacity `0x2A3` = 675** (the loader stops reading rows when the slot index reaches it), live-effect count `0x0143CB28`. **The slot index of an entry is the *effect index* other tables store** (`rain`, `groundfires`, `skybox_effects`, the TOD orbital objects): `0x005C50B0(name)` = CRC-32 of the name → open-addressing (linear-probing) hash table of slot indices → the slot number, `0xFFFFFFFF` if absent; the table compares against the entry's stored CRC at `+0x44`. **[CONFIRMED — disassembly.]** **[Desk review 2026-09-30: `spec-tables-weapons-combat.md` §1.6 (`FUN_005C50B0`) gives the probe start as `hash mod capacity`.]** ~~**[OPEN — desk review 2026-09-30: bytes `+0x40`–`+0x43` (after the `char[0x40]` name, before the CRC at `+0x44`) are not described; and what happens when two rows share a `Name` (first kept, last kept, or both inserted) is not stated — relevant because the base game ships two copies of this table (§17); `0x005C4B10`; to be settled against the executable.]**~~ **[CONFIRMED — disassembly 2026-10-01: `+0x40`–`+0x43` are not written (padding). Duplicate `Name`: the hash table is consulted (`0x005C49D0`) before insertion (`0x005C4970`); a repeated name is not inserted again, so lookups keep the first row and the later row occupies a slot unreachable by name (and, if it has neither visual nor sound, its drop path tombstones the first row's bucket) — HIGH CONFIDENCE, the lookup/insert helpers not read. Also: a switch-name text without `:` writes neither half; with a `NULL` framework argument the row filter is skipped; a `Damage_Region` without `Region_Shape`, or a `Proximity_Refraction` without `Situation`, hands a `NULL` text to `_stricmp`/the hash (hazard, not exercised).]**

| Element (child of `Effect`) | Type | Offset / effect | Notes |
|---|---|---|---|
| `Name` | `char[0x40]` (bounded copy) | `+0x00` | |
| *(derived)* | CRC-32 of `Name` (seed 0) | `+0x44` | the hash-table key |
| `Framework` | filter | not stored | default `main`; row kept only when equal to the framework being loaded |
| `Info_Slot_Index` | integer | not stored per row | read **only** on the first `Effect` row of a framework whose `Framework` matches, by the DLC driver — it becomes that framework's **start slot** (§10.1.1) |
| `visual` | **vfx name** → index | `+0x48` | linear `_stricmp` scan of the vfx array (`0x005C43F0`, §10.2); `0xFFFFFFFF` if absent or unmatched |
| `sound` | sound name → handle (`0x00462960`) | `+0x4C` | `0` if absent |
| `sound_parent_switch_name` | text `"Group:State"` | `+0x50` (group), `+0x54` (state) | split at the first `:`; each half converted with `0x00462960`; read **only when `sound` resolved to a non-zero handle** |
| `sound_switch_name` | text `"Group:State"` | `+0x58`, `+0x5C` | same rule |
| `vfx_kill_particles_fade_time` | `f32`, write-if-present | `+0x60` | read **only when `visual` resolved** |
| `scale_factor` | `f32`, write-if-present | `+0x64` | read for every row (before `visual` is resolved) |
| `Damage_Region/Region_Type` | CRC-32 (seed 0) of the text | `+0x68` | absent `Damage_Region` → the default hash `[0x029C9964]` (0 in the image) |
| `Damage_Region/Region_Shape` | text; `Box` (case-insensitive) → 1 | `+0x6C` (byte) | ~~any other text, or absent block → 0~~ absent block → 0; any other text leaves the byte untouched (0 in a fresh array) — 2026-10-01 |
| `Damage_Region/Region_Offset` | `vec3` (`X`,`Y`,`Z`) | `+0x70`..`+0x7B` | absent block → `(0,0,0)` (constants at `0x029CDB98`) |
| `Damage_Region/Region_Size` | `vec3` | `+0x7C`..`+0x87` | absent block → `(0,0,0)` |
| `restart_sound_at_loop` | `bool`, write-if-present | `+0x88` bit 0 | read **only when `sound` resolved** |
| `kill_sound_when_done` | `bool` | `+0x88` bit 1 | same rule |
| `sound_follows_effect` | `bool` | `+0x88` bit 2 | same rule |
| `vfx_kill_particles` | `bool` | `+0x88` bit 3 | read **only when `visual` resolved** |
| `stop_when_host_destroyed` | `bool` | `+0x88` bit 4 | same rule |
| `stop_under_car` | `bool` | `+0x88` bit 5 | same rule |
| `stop_under_player` | `bool` | `+0x88` bit 6 | same rule |
| `only_at_night` | `bool` | `+0x88` bit 7 | same rule |
| `use_mission_srid` | `bool` | `+0x89` bit 0 | same rule |
| *(runtime)* "in use" mark | — | `+0x89` bit 1 | set when the entry is kept; cleared for every entry by `0x005C4340` |
| `Proximity_Refraction/Situation` | CRC-32 (seed 0) of a **refraction-situation name** (§8) | `+0x8C` | absent block → default hash `[0x029C9964]` |
| `Proximity_Refraction/Near_Radius` | `f32` | `+0x90` | absent block → 0 |
| `Proximity_Refraction/Far_Radius` | `f32` | `+0x94` | absent block → 0 |

`_Editor/Category` (present in every DLC row) is editor metadata the loader never looks up. **[Desk review 2026-09-30: wider than stated — Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`" finds `_Editor` in effectively every row of all 26 tables, not only in `effects`.]** The `bool` elements are write-if-present, so an absent bool leaves its bit at 0 (the array is zero-initialised).

**The keep/drop rule.** After a row is read, if **both** `visual` did not resolve (`+0x48 = 0xFFFFFFFF`) **and** `sound` did not resolve (`+0x4C = 0`) the entry is **removed from the hash table** (its bucket is overwritten with `0x7FFFFFFF`, a global live counter is decremented) **and its slot number is reused by the next row**; otherwise the slot advances, `0x0143CB28` is incremented and `+0x89` bit 1 is set. An effect with neither a resolvable visual nor a sound is therefore not addressable by name.

**Second reader.** `0x00739A80(registry)` also reads `effects.xtbl`: for every `Effect` that has a `visual` it registers the pair `(Name → visual)` — both strings duplicated to the heap (`0x00A74910`) — through a virtual call (slot `+0x10`) on the caller-supplied registry object. Its purpose (a name→visual string map, presumably for streaming or an editor hook) was not traced — **OPEN**.


**Review status (2026-09-30): DESK-PASS, text fixes applied (`_Editor` scope, probe start cross-reference); NEEDS-EXE: `+0x40`–`+0x43`, duplicate-name policy; NEEDS-DATA: the 4 real `Damage_Region` rows' child names, duplicate `Name`s. Real-data figures: Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "`Proximity_Refraction` present in 0/1,272", "`Damage_Region` in 4/1,272", the 9 bools "1,272/1,272" — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x005C4B10` in full): every offset, gating rule, default and the keep/drop rule CONFIRMED — disassembly; `+0x40` padding CONFIRMED; duplicate-name policy HIGH CONFIDENCE — not cleared: `0x005C49D0`/`0x005C4970` (duplicate policy); NEEDS-DATA: `Damage_Region` children, duplicate `Name`s (unchanged).**

#### 10.1.1 DLC slots **[CONFIRMED — disassembly + CONFIRMED — empirical]**

The DLC driver `0x005C5160(framework list)` — for each framework, in list order — (a) builds the file names `<fw>_vfx.xtbl`, `<fw>_vfx_containers` and `<fw>_effects.xtbl` (§1.5), (b) loads the vfx rows and resource containers (§10.2), (c) opens `<fw>_effects.xtbl`, scans its `Effect` rows for the first whose `Framework` equals the framework name and reads that row's **`Info_Slot_Index`** as the start slot, and (d) calls `0x005C4B10(<fw>_effects.xtbl, fw, start)` so the framework's rows are written to slots `start, start+1, …`. Real data: **`dlc1_effects.xtbl` first `Info_Slot_Index` = 606 (21 rows), `dlc2` = 627 (25 rows), `dlc3` = 652 (14 rows)** — contiguous (606+21 = 627, 627+25 = 652), ending at slot 666 of the 675 available; so the base game occupies slots `0…605`. (Same arrangement as the vehicle table of `spec-vehicle-data.md` §7.1, and confirmed the same way.) All 60 DLC rows carry `visual`; 12 carry `sound`.


**Review status (2026-09-30): DESK-PASS (606 + 21 = 627, 627 + 25 = 652, 652 + 14 = 666 ≤ 675 recomputed; 606 base rows, §17.1) — desk review (not re-derived from the executable).**

### 10.2 `vfx.xtbl` **[CONFIRMED — disassembly + CONFIRMED — empirical (§14)]**

**Row reader** `0x005C3E70(framework filter, …, platform-extension index, pass flags)`; driven by `0x005C45D0` (which loads the named table, calls the row reader once per `Effect`, and hands the resource requests to the container loader). The base-game initialiser `0x005CE760` calls it **twice** — `vfx_preload_containers` (a row is processed only when its `Streaming_Category` is `Preload`) and `vfx_containers` (all other rows) [HIGH CONFIDENCE — the row reader compares the row's preload-ness with the pass flag] — while the DLC driver `0x005C5160` makes **one call per framework, with the non-preload container name `<framework>_vfx_containers`**; how the DLC `…_vfx_preload_containers` archive entries are mounted was not traced. **Root/row:** `Table` → repeated **`Effect`**. **Runtime array:** base `0x01455C00`, stride **`0xB8`**, count `0x0143CB2C`. Rows are appended; a row is skipped when its `Framework` (default `main`) does not equal the framework being loaded (strict equality in the row reader; the second reader below additionally lets a `main` row pass in a non-`main` load).

| Element path (under `Effect`) | Type | Offset | Notes |
|---|---|---|---|
| `Name` | `char[0x40]` | `+0x00` | the string `effects.xtbl` `visual` is matched against |
| `VFX/Filename` | file name with its extension **replaced** by `.cefct_pc` (`0x00DA8790`) | `+0x41` (`char[0x40]`) | the **resource name** of the effect file — §10.3. Extension table for platforms: `.cefct_pc`, `.cefct_ps2`, `.cefct_ps3`, `.cefct_xbox`, `.cefct_xbox2` (PC = index 0) |
| `Streaming_Category` | enum by `_stricmp`: **`Preload` 0 · `Multiplayer` 1 · `Vehicle` 2 · `Environment` 3 · `Env Preload` 4 · `Cutscene` 5**, else −1 | `+0x98` | selects the container pass; category 5 (`Cutscene`) is **not** loaded by the normal path (its handle is left 0) |
| `Radius` | `f32`, write-if-present | `+0x84` | default **1.0** |
| `Radius_expands` | `bool`, write-if-present | `+0x90` bit 5 (`0x20`) | **default true** (the local is preset to 1 and the bit is set when it stays non-zero) |
| `Blocker` | presence | `+0x90` bit 2 (`0x04`) | |
| `Blocker/Opacity` | `f32` | `+0x88` | absent `Blocker` → **−1.0** |
| `Blocker/LifeSpan` | `s32` (`0x00DABC70`, signed — 2026-10-01) | `+0x8C` | (the reader builds the message text "%s: Opacity should be greater than 0" for a debug check; not enforced here) |
| `Radial_blur/Radial_blur_entry` | **radial-blur name** → index (`0x005CE2D0`, `_stricmp` scan of §6.3's array) | `+0x9C` | |
| `LOD` | presence | `+0xA0` (byte) | absent → `+0xA0` = 0 and nothing else written (2026-10-01) |
| `LOD/Spawning/Distance` | `f32` | `+0xA8` | absent `Spawning` → default **1.0e8** and `+0xA1 = 0` |
| `LOD/Spawning/View` | `bool` | `+0xA1` | |
| `LOD/Distance/Fading_start` | `f32` | `+0xAC` | absent `Distance` → **1.0e10** for both ~~**[OPEN — desk review 2026-09-30: the defaults are stated for an absent `Spawning`/`Distance` block inside a present `LOD`; what `+0xA1`–`+0xB0` hold when the whole `LOD` element is absent (401 of 529 real rows, §17.1) is not stated; `0x005C3E70`; to be settled against the executable.]**~~ **[CORRECTED — CONFIRMED — disassembly 2026-10-01: when the whole `LOD` element is absent only `+0xA0` = 0 is written; `+0xA1`–`+0xB0` are left untouched (0 in the zero-filled array) — the 1.0e8/1.0e10 defaults apply only to a present `LOD` lacking `Spawning`/`Distance`.]** |
| `LOD/Distance/Fading_end` | `f32` | `+0xB0` | |
| `LOD/Distance/Restore` | `bool` | `+0xA2` | |
| `LOD/Update` | presence | `+0xA3` (byte) | |
| `LOD/Update/Minimum_time` | `f32` | `+0xA4` | |
| `Start_time` | `f32`, write-if-present | `+0xB4` | default 0 |
| *(runtime)* | flags `u32` | `+0x90` | initial `0x10` (bit 4); bits 2 and 5 above |
| *(runtime)* | resource handle | `+0x94` | result of the resource lookup by the `+0x41` name (`0x00DB3530`); if not already loaded a load request is made |

**Second reader.** `0x00ACEAE0(registry, framework)` walks `vfx.xtbl` again and registers `(Name → "Streaming_Category is Preload")` through a virtual call (slot `+0x10`) — a name→preload-flag map. Its reading of `Streaming_Category` dereferences the child without a null check (a row with no `Streaming_Category` element would fault there) **[CONFIRMED — disassembly; not exercised]**.


**Review status (2026-09-30): DESK-PASS, text fixes applied (LOD-absent case marked OPEN); NEEDS-EXE: `0x005C3E70` with no `LOD`. Stride `0xB8` tiles; the `Streaming_Category` enum is backed by Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`" (enum gate over 589 rows: no anomalies); `Blocker` used in 0/589 — desk review (not re-derived from the executable).**

**FOR TEAM B (2026-10-01, CORRECTION):** a `vfx` row without `LOD` holds `+0xA0` = 0 and zeros in `+0xA1`–`+0xB0` (`Spawning Distance`, `Fading_start`, `Fading_end` = 0.0; `View`, `Restore`, `Update` = 0; `Minimum_time` = 0.0) — not 1.0e8/1.0e10. An `OrDefault()` reading that applies those defaults on any absence models a value the engine does not hold; the defaults apply only inside a present `LOD`.

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x005C3E70` in full): layout, defaults, the preload pass logic and strict framework equality CONFIRMED — disassembly; LOD-absent case CORRECTED (fields stay 0, no defaults); `LifeSpan` signed — cleared for implementation.**

### 10.3 The effect name → file binding (cross-check with `spec-effects-format.md`)

1. A game system names an effect by its **`effects.xtbl` `Name`** (hashed to a slot index, §10.1).
2. The entry's `visual` names a **`vfx.xtbl` `Name`** (a plain case-insensitive string match, not a hash).
3. That row's `VFX/Filename` — an authoring-tool `.effectx` source file name in the data (every DLC row: e.g. `vfx_skydive_panda.effectx`) — has its extension replaced with `.cefct_pc`, giving the **resource name of the compiled effect file** (the format of `spec-effects-format.md`, whose §1 records that each `.cefct_pc` embeds its `.effectx` source name). The resource is looked up by that name in the effect containers `vfx_preload_containers` / `vfx_containers`; **for a DLC framework the container name is prefixed with the framework name** (`<framework>_vfx_containers`, formatted by `0x0045BAB0`) — and the three raw DLC archives contain exactly `dlc1/2/3_vfx_preload_containers.str2_pc`, `dlc1/2/3_vfx_preload_containers.asm_pc` and `dlc1/2/3_vfx_containers.asm_pc` **[CONFIRMED — empirical: archive entry names; the containers' contents were not opened]**.
4. Parameters that belong to the *instance* rather than the file — sound, kill-on-host-destroyed, damage region, refraction proximity, mission-SRID flag — live in `effects.xtbl`; parameters that belong to the *resource* — radius, LOD/fade, blocker, radial blur, streaming class — live in `vfx.xtbl`.

The `.cefct_pc` internal effect-class registry and parameter blocks (`spec-effects-format.md` §6) are unaffected; these two tables contribute **no** new field to that format.


**Review status (2026-09-30): DESK-PASS (consistent with §10.1/§10.2 and §14 P9) — desk review (not re-derived from the executable).**

## 11. Interface effects, decals, ground fires, shells, material colours, bitmap materials and sheets, map districts, fade categories

Loader-level schemas for the remaining tables. Depth per table is stated; nothing here was checked against sample data (none of these tables has a raw-readable DLC copy).

### 11.1 `interface_effects.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x007AC920`. **Root/row:** `Table` → repeated **`InterfaceEffect`**. **Array:** pointer `0x012FC878`, stride **`0x4C`**, capacity `0x012FC87C`, count `0x012FC880` (reset at entry; rows beyond the capacity are dropped).

| Element | Type | Offset |
|---|---|---|
| `Name` | `char[0x20]` | `+0x00` |
| `Camera/FOV` | `f32` | `+0x20` (row zeroed first) |
| `Camera_Blur` (a **direct child of the row**, not under `Camera`) | `f32`, write-if-present | `+0x24` |
| `LUT/LutName` | `char[0x20]` | `+0x28` (first byte zeroed first) |
| `LUT/LutStrength` | `f32` | `+0x48` (only when `LUT` exists) |


**Review status (2026-09-30): DESK-PASS (stride `0x4C` tiles; capacity value `0x012FC87C` not read, §16 item 8) — desk review (not re-derived from the executable).**

### 11.2 `decal_info.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x007474F0` (opens the `decal_containers` resource group, then the table); row reader `0x007471F0`. **Root/row:** `Table` → repeated **`Decal_Info`**. **Runtime array:** base `0x015535C8`, stride **`0xB8`**, count `0x01554CC8`, which sits `0x1700` bytes after the base — **room for 32 records** [HIGH CONFIDENCE — layout, no check in the loader]. Each row also registers its name in a small hash-indexed name→slot pool (`0x00596210`; buckets by the multiply-33 hash `0x00DAB330` with 0x20 buckets; slot pointers `0x012F6BF0`, name copies `0x012F6C70`, `0x40` bytes per slot) — the lookup `0x00746F00(name)` returns **`(record address − 0x015535C8)/0xB8`**, i.e. the **decal index** that `bitmap_materials.xtbl` stores (§11.6); `−1` when unknown.

| Element (child of `Decal_Info`) | Type | Offset | Notes |
|---|---|---|---|
| `Name` | `char[0x40]` | `+0x00` | |
| `Material/Filename` | file name with its extension **replaced** by `matlib_pc` (the platform table is `matlib_pc`, `matlib_ps2`, `matlib_ps3`, `matlib_xbox`, `matlib_xbox2`) | `+0x41` (`char[0x40]`) | the resource name of the decal's material library; loaded from the `decals` group |
| `Preload` | `bool` | `+0xB4` bit 0 | always-write bool |
| `Double_sided` | `bool` | `+0xB4` bit 2 | |
| `Life_Time` | `s32` | `+0x84` | |
| `Fade_Time` | `s32` | `+0x88` | |
| `Width` | `f32` | `+0x8C` | |
| `Length` | `f32` | `+0x90` | |
| `Depth` | `f32` | `+0x94` | |
| `Depth_Fade_Start` | `f32` | `+0x98` | |
| `Depth_Fade_End` | `f32` | `+0x9C` | |
| `Slope_Fade_Start` | `f32` **degrees**: value × 0.017453 (`0x012A30D8`), then passed through the CRT trigonometric routine `0x00EA3A70` | `+0xA0` | ~~which of sine/cosine `0x00EA3A70` is was not established (**HYPOTHESIS: cosine**, a slope threshold against the surface normal)~~ **CONFIRMED — disassembly (§17.3): `0x00EA3A70` is `cos`/`cosf`** — its x87 fallback (`0x00EA3AC8`) executes `FCOS` and its domain-error path loads the literal string `"cos"` (`0x013447D0`); the slope-fade thresholds are cosines of the authored angles, against the surface normal |
| `Slope_Fade_End` | same | `+0xA4` | |
| `has_normal` | `bool` | `+0xA8` | |
| `alpha_test` | `f32` | `+0xAC` | |

`+0xB0` is zeroed at load (a run-time slot).


**Review status (2026-09-30): DESK-PASS (stride `0xB8` tiles; `0x1700 / 0xB8 = 32` exactly; Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": decal_info fields 14/14 present) — desk review (not re-derived from the executable).**

### 11.3 `groundfires.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x005FD580`. **Root/row:** `Table` → repeated **`Groundfire`**. **Array:** pointer `0x014A29AC` (allocated `count << 5`), count `0x014A2950`, stride **`0x20`**.

| Element | Type | Offset | Notes |
|---|---|---|---|
| `Name` | heap copy of the string | `+0x00` | |
| *(derived)* | CRC-32 of `Name` (seed 0) | `+0x04` | |
| `Damage_Radius` | `f32` | `+0x08` | |
| `Damage_Region` | heap copy of the string | `+0x0C` | |
| `effects/effect` (repeated; text = an **effect name**) | effect index (`0x005C50B0`) | array pointer `+0x10`, count `+0x14` | `count × 4` heap array; unknown names are stored as `0xFFFFFFFF` |
| `Duration_Min` | `f32` seconds → **integer milliseconds** (`(int)(x × 1000.0)`) | `+0x18` | |
| `Duration_Max` | same | `+0x1C` | |


**Review status (2026-09-30): DESK-PASS (stride `0x20` tiles) — desk review (not re-derived from the executable).**

### 11.4 `shells.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x0058BD30`. **Root/row:** `Table` → repeated **`Shell`**; **at most 8 rows are read** (the loop stops at the ninth). **Array:** base `0x013CF220`, stride **`0x0C`**, count `0x013CF280` (reset at entry).

| Element | Type | Offset |
|---|---|---|
| `Name` | heap copy | `+0x00` |
| `Static_Mesh` | mesh name → handle (`0x00904C10`) | `+0x04` |
| `Collision_Foley` | sound name → handle (`0x00462960`); `0` when absent | `+0x08` |


**Review status (2026-09-30): DESK-PASS (8 × `0x0C` = `0x60` = `0x013CF280 − 0x013CF220`) — desk review (not re-derived from the executable).**

### 11.5 `material_color_variants.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x007549F0`; the file name is taken from a **pointer variable** (`0x012F7BE8`, initialised to the literal). **Root/row:** `Table` → repeated **`color_entry`**. **Array:** pointer `0x012F7BF0`, stride **`0x10`**, capacity `0x012F7BF4`, high-water mark `0x012F7BF8` (= greatest `_Entry_ID` + 1, updated only while it fits the capacity; reset to 0 at entry when the capacity is non-negative).

| Element (child of `color_entry`) | Type | Destination |
|---|---|---|
| `_Entry_ID` | integer via `sscanf("%d")`; **must be ≤ 256** (`< 0x101`) or the row is skipped | the record index |
| `Color` | text `"r g b a"` via `sscanf("%f %f %f %f")`, unspecified components stay **1.0** | `f32[4]` at `record[_Entry_ID]` |
| `Name` **[added 2026-09-30, desk review]** | string | not stored — present in every real row (Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "every real `color_entry` row (145/145) carries an undocumented `<Name>`"); not in the loader's element list, so not read |

A row missing either element is skipped; a `Color` element with no text leaves the record untouched. The table is therefore a **palette of up to 257 RGBA colours indexed by `_Entry_ID`**. This is the loader of the "material-colour-variant subsystem" `spec-rig-format.md` §11.18.5 located next to the `mvi_mesh_variant` name; the consumer of the palette was not traced.


**Review status (2026-09-30): DESK-PASS, text fixes applied (`Name` added as present, unread; Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": 145/145); capacity value `0x012F7BF4` not read — desk review (not re-derived from the executable).**

### 11.6 `bitmap_materials.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x006F7780`. **Root/row:** `Table` → repeated **`Bitmap_Material`**. **Array:** base `0x014F6B98`, stride **`0x18`**, **33 slots indexed by the material name's position in a 33-entry name list** (`0x0113DF70`); a row's `Name` is matched case-insensitively against the list, and **an unknown name falls into slot `0x1F` (`not set`)**. Later rows for the same slot overwrite earlier ones.

The 33 names, slot 0…32: `concrete`, `cardboard`, `carpet`, `ceramic`, `dirt`, `drywall`, `flesh`, `foliage`, `glass - heavy`, `glass - medium`, `glass - light`, `grass`, `gravel`, `marble`, `metal - fence`, `metal - solid`, `metal - thin`, `plastic`, `rubber`, `sand`, `water`, `wood`, `corrugated brick`, `corrugated metal`, `cyberspace`, `vibration`, `water container`, `flame`, `electric`, `steam`, `concrete - reflectable`, `not set`, `wrestling mat`.

| Element (child of `Bitmap_Material`) | Type | Offset in the slot |
|---|---|---|
| `Name` | slot selector (above) | — |
| `MaterialProperties/Suffix` | `char[3]` | `+0x00` |
| `Bullet_Decals/Bullet_Decal` (repeated; text = a **decal name**) | decal index (`0x00746F00`, §11.2) | count `+0x04`, heap array pointer `+0x08` (`count × 4`; `0` when there are none) |
| `Blast_Decal` | decal index | `+0x0C` |
| `Crash_Decal` | decal index | `+0x10` |
| `Audio_Occlusion` | integer, low byte kept; write-if-present (preset 0) | `+0x14` |


**Review status (2026-09-30): DESK-PASS (`not set` is position 31 = `0x1F` in the 33-name list; 25 + 8 = 33 against §17.6) — desk review (not re-derived from the executable).**

### 11.7 `bitmap_sheets.xtbl` **[CONFIRMED — disassembly for the row shape; OPEN for what happens after]** **[Resolved: "what happens after", see §17.4.]**

**Loader** `0x005D2370` (a start-up routine that also initialises the bitmap machinery) → **`0x00E222E0(name, pool, stream)`**, which parses the table with the caller's stream object and reads: `Table` → repeated **`BitmapSheets`**, each with one **`Name`** child (default empty string `0x0129A0E3`). **At most `0x180` = 384 sheets** (the loop stops when the count exceeds `0x17F`). Per sheet: the name gets **`.tga`** appended (literal `0x0113DDB4`) and is interned; its CRC-32 (seed 0) is registered with `0x00E21E80`; a 12-byte record is written at `0x02A4D188 + 12×n` (`+0x00` interned name pointer, `+0x04` `0xFFFFFFFF` = "not yet loaded"); after the pass every sheet is processed by `0x00E21F70(index, name, stream)`. The registry it builds belongs to the **"Bitmap Image Names"** pool (a `0xC000`-byte pool created by `0x00E22200`). The sheet contents are not in this table. The `.tga`-suffixed names are UI/bitmap image names, not the `.peg_pc` files themselves. **[HIGH CONFIDENCE for the last sentence — the literal suffix is `.tga`; the mapping to `.peg_pc` sheet files was not traced.]**


**Review status (2026-09-30): DESK-PASS (resolved in §17.4; consistent with `spec-tables-customization.md`) — desk review (not re-derived from the executable).**

### 11.8 `map_districts.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x007D8FB0` (optional table). **Root/row:** `Table` → repeated **`Map_district`**. **Array:** base `0x02299F28`, stride **`0xB4`**, count `u16` at `0x0229A2B0`.

| Element (child of `Map_district`) | Type | Offset |
|---|---|---|
| `Name` | `char[0x20]` | `+0x00` |
| `data_item_name` | `char[0x20]` | `+0x20` |
| `team_name` | `char[0x20]` | `+0x40` |
| `contact_icon` | `char[0x20]` | `+0x60` |
| `contact_name` | `char[0x28]` | `+0x80` |
| `text_location` (`X`,`Y`,`Z`) | **`X` → `+0xA8`, `Z` → `+0xAC` (`Y` is read but not stored)** | `+0xA8`, `+0xAC` |
| *(derived)* | the value at `+0x44` of the object `0x0084BA60(position)` returns for the full point `(X, Y, Z)` — a containment lookup (tolerance `1e-4`, `0x012A2EA4`) of the zone/district object at that position — else 0 | `+0xB0` |


**Review status (2026-09-30): DESK-PASS (stride `0xB4` tiles) — desk review (not re-derived from the executable).**

### 11.9 `fade_categories.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x00584900`. **Root/row:** `Table` → repeated **`Category`**. **Array:** pointer `0x012E5A08`, stride **`0x10`**, capacity `0x012E5A0C`, count `0x012E5A10` (reset at entry); rows beyond the capacity are dropped. The index of the category named **`Default`** is kept in `0x012E5A00` (initial −1).

| Element (child of `Category`) | Type | Default | Record field |
|---|---|---|---|
| `Name` | CRC-32 (seed 0) of the text | — | `+0x00` |
| `medium_lod_distance` | `f32` | 60.0 | **squared** → `+0x04` |
| `low_lod_distance` | `f32` | 200.0 | **squared** → `+0x08` |
| `Distance` | `f32` | 500.0 | **squared** → `+0x0C` |

(The defaults are the constants at `0x01173DE4`, `0x012A30D4`, `0x01118BC4`. Distances are stored squared so the run time can compare against squared distances; the always-write accessor is used, so an absent element is subject to the hazard of §1.4 rather than to the default — the default only applies to the destination's initial value.)


**Review status (2026-09-30): DESK-PASS (the real `Default` row equals the three code defaults, §17.8) — desk review (not re-derived from the executable).**

## 12. Camera tables: `camera_shake.xtbl` and `camera_free.xtbl`

Both are loader-level: the element vocabulary and destinations are complete for what the loader reads; the run-time behaviour behind them is not traced.

### 12.1 `camera_shake.xtbl` **[CONFIRMED — disassembly]**

**Loader** `0x0057C290(update)`. **Root/row:** `Table` → repeated **`Camera_Shake`** — **at most `0x80` = 128 rows** (a larger table is rejected before any row is read). Records (`0x198` bytes each) come from a fixed-size **pool** created on the initial load with one slot per row (`0x0057BCE0`, free list in `0x013CB80C`…; slot data at `0x013CB810`); each row takes a slot and is registered by name in a 128-bucket name→slot index (multiply-33 hash `0x00DAB330`, `0x0057BC70` lookup; slot table `0x012E4420`/`0x012E4620`; name copies `char[0x20]` at `0x012E4820`). **Record layout** (all `f32`, all read with the always-write accessor):

| Group (child of `Camera_Shake`) | Elements → record offset |
|---|---|
| `Wobble` | `Pitch` `+0x00` · `Roll` `+0x04` · `Yaw` `+0x08` · `Variation` `+0x0C` |
| `Destabilization` | `Pitch` `+0x10` · `Roll` `+0x14` · `Yaw` `+0x18` · `Frequency` `+0x1C` |
| `Wander` | `Pitch` `+0x20` · `Yaw` `+0x28` · `Frequency` `+0x2C` (**no `Roll`**; `+0x24` unused) |
| `Jitter` | `Pitch` `+0x30` · `Roll` `+0x34` · `Yaw` `+0x38` |
| `Oscillation1` | `Pitch` `+0x3C` · `Roll` `+0x40` · `Yaw` `+0x44` · `Frequency` `+0x48` |
| `Oscillation2` | `Pitch` `+0x4C` · `Roll` `+0x50` · `Yaw` `+0x54` · `Frequency` `+0x58` |
| `Direct` | `Pitch` `+0x5C` · `Roll` `+0x60` · `Yaw` `+0x64` |
| `Wander_Direct` | `Pitch` `+0x68` · `Yaw` `+0x70` (**no `Roll`**; `+0x6C` unused) |
| `Strength_Graph/Strength_Element` (repeated) | count `+0x194` (u32); element *n* at `+0x74 + 0x30·n`: `Time` `+0x00` · `Wobble` `+0x04` · `Destable` `+0x08` · `Wander` `+0x0C` · `Jitter` `+0x10` · `Oscillation1` `+0x14` · `Oscillation2` `+0x18` · `Direct` `+0x1C` · `Wander_Direct` `+0x20` · `Blur` `+0x24` · `Strong_Vibration` `+0x28` · `Weak_Vibration` `+0x2C` |

`Name` (the key, hashed as above) is read from the row. **The graph holds at most 6 elements** (`+0x74 + 6×0x30 = +0x194`); the loader does not check the count. The `Strength_Element` rows are a piece-wise time curve of per-channel strengths (the element order and `Time` semantics — that the array is a curve sampled by time — is inferred from the element names, **HIGH CONFIDENCE — inferred**). **[OPEN — desk review 2026-09-30: the largest `Strength_Element` count in the 70 real rows has not been measured, so whether the unchecked 6-element limit is ever exceeded is not known; to be settled against real data.]**


**Review status (2026-09-30): DESK-PASS (record `0x198`: `0x74 + 6 × 0x30 = 0x194` + 4-byte count; Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "camera_shake's 9 groups 70/70"); NEEDS-DATA: maximum `Strength_Element` count — desk review (not re-derived from the executable).**

### 12.2 `camera_free.xtbl` — the gameplay camera parameter table **[CONFIRMED — disassembly for every name and destination listed]**

**Loader** `0x0056E4C0`; group readers `0x0056DCA0` (top) and the five sub-readers named below. **Root:** `Table` → one **`Camera`** element with eight named groups (`0x0056DCA0` looks up exactly these eight; it never names `melee_lock_group` or `sway_group` — their reader, if any, is the loader `0x0056E4C0` or another function, OPEN — CONFIRMED — disassembly 2026-10-01) **[Desk review 2026-09-30: the loader reads eight; the real file has ten — `melee_lock_group` and `sway_group` are also present, §17.5 (Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": 2/2 files)]**. Nothing here is per-row except the three arrays.

**`panning_group`** (`0x00569FB0`) — 51 floats in one contiguous block **`0x012E3E58`–`0x012E3F20`** (4-byte steps, in the reading order below except where stated): `fast_pan_horizontal_multiplier`, `fast_pan_vertical_multiplier`, `slow_pan_horizontal_multiplier`, `slow_pan_vertical_multiplier`, `peg_accel_pan_horizontal_min`, `peg_accel_pan_horizontal_max`, `peg_accel_pan_vertical_min`, `peg_accel_pan_vertical_max`, **`zoom_scale_min` `0x012E3E78` then `zoom_scale_max` `0x012E3E7C`** (read in the opposite order), `fast_pan_input_threshold` (`0x012E3E80`), `accel_scale`, `decel_scale`, `fine_aim_accel_scale`, `fine_aim_decel_scale`, `hpan_threshold`, `vpan_threshold`, then the dampening set `{fine_aim, heavy_weapon, human_shield, tank, tank_…_genki}` × `{horiz, vert}_dampening` (`0x012E3E9C`–`0x012E3EC0`) **[OPEN — desk review 2026-09-30: only `tank_horiz_dampening_genki_mouse`, `tank_vert_dampening_genki_mouse` and the two `interior_*` pairs are given as literal names; the other dampening element names and the horiz/vert order within each pair are written as a pattern, not listed, so they are not CONFIRMED by this text (the real `panning_group` children can settle the names); to be settled against the executable / real data.]**, `interior_v_dampening` `0x012E3EC4` and `interior_h_dampening` `0x012E3EC8` (**v before h in memory**), `skydive_h`/`skydive_v`, `freefall_h`, `parachute_h`, `helicopter_h` `_dampening` (`0x012E3ECC`–`0x012E3EDC`), and the same set with a `_mouse` suffix for mouse input (`0x012E3EE0`–`0x012E3F20`; `interior_v_dampening_mouse` `0x012E3F08` before `interior_h_dampening_mouse` `0x012E3F0C`). The four destinations printed as ASCII by the tracer (`slow_pan_vertical_multiplier`, `fast_pan_input_threshold`, `tank_horiz_dampening_genki_mouse`, `tank_vert_dampening_genki_mouse`) are the sequential slots `0x012E3E64`, `0x012E3E80`, `0x012E3F00`, `0x012E3F04` **[HIGH CONFIDENCE — inferred from the run of 4-byte steps]**.
**`asvct_group`** — `default_time` `0x012E3F44`, `stationary_time` `0x012E3F48`, `stationary_threshold` `0x012E3F4C`. **`follow_aggression_group`** — `default_swing_rate` `0x012E3F50`. **`hill_tracking_group`** — `default_aggression` `0x012E3F54`, `genki_aggression` `0x012E3F58`.
**`miscellany_group`** (`0x0056A4A0`) — `pitch_reset_time` `0x012E3F24`, `backaway_speed` `0x012E3F28`, `Manual_Aim_Elevation_Angle` `0x013C9364`, `Manual_Vehicle_Aim_Elevation_Angle` `0x013C9368`, `Helicopter_Landing_Pitch` `0x012E3F2C`, `Helicopter_Landing_Max_Speed` `0x012E3F30`, `Helicopter_Landing_Max_Altitude` `0x012E3F34`, `Exterior_Interior_Blend_Time` `0x012E3F38`, `Ragdoll_Blend_Time` `0x012E3F3C`.
**`submodes/submode`** (repeated; `0x0056B660`) — the row's **`name`** is matched (`0x00DAC830`, case-insensitive) against a **61-entry mode-name table at `0x012E3D48`**: `exterior close`, `interior close`, `interior sprint`, `vehicle driver`, `vehicle driver alt`, `watercraft driver`, `helicopter driver`, `vehicle aim`, `vehicle fine aim`, `vehicle zoom`, `airplane driver`, `tank driver`, `zoom`, `zoom crouch`, `swimming`, `spectator`, `fence`, `ragdoll`, `falling`, `leaping`, `fine aim`, `fine aim underslung`, `fine aim crouch`, `melee lock`, `fine aim vehicle`, `freefall`, `parachute`, `riot shield`, `riot shield fine aim`, `human shield`, `human shield fine aim`, `sprint`, `crouch`, `crouch interior`, `avatar`, `shooting qte`, `wrestling`, `interior melee`, `rc vehicle`, `rc watercraft`, `rc helicopter`, `rc airplane`, `rc tank`, `downed`, `skydiving down`, `skydiving down fine aim`, `skydiving dive`, `skydiving dive fine aim`, `skydiving up`, `skydiving up fine aim`, `skydiving tank`, `skydiving tank gunner`, `skydiving tank bail out`, `rappelling`, `rappelling fine aim`, `rappelling zoom`, `Script`, `Script Fine Aim`, `Script Crouch`, `Script Fine Aim Crouch`, `MAS Fine Aim` (index 0…60). The selected slot is a `0x40`-byte record at `0x013C93B0 + 0x40·index`. **[Desk review 2026-09-30: arithmetic checks — 61 × `0x40` from `0x013C93B0` ends at `0x013CA2F0`, the `Vehicle_Aims` base of §17.5.]** ~~**[OPEN — desk review 2026-09-30: the real file has 69 `submode` rows (§17.1) for 61 name slots; whether the submode reader applies an `Aspect` filter like `Vehicle_Aim` (§17.5) or lets later rows overwrite a slot is not stated; also where `lookat_offset`/`z_dist`/`y_dist` land and whether those three are always-write or write-if-present; `0x0056B660`; to be settled against the executable / real data.]**~~ **[CONFIRMED — disassembly 2026-10-01: the submode reader applies the same `Aspect` filter as `Vehicle_Aim` (a `widescreen` row is skipped when the display flag `0x01493661` is clear, a `standard` row when it is set, any other text passes), skips rows whose name is not in the table (index −1, no out-of-range write), and lets a later row with the same name overwrite the record (no duplicate check — last wins). `lookat_offset` (`0x00DACF60`) is combined by `0x00DA38E0` into four dwords at `+0x00`–`+0x0C`; `min_elevation`/`max_elevation`/`default_elevation` are converted from degrees to radians (× 0.017453); `default_elevation` present → `+0x18` = 1, absent → `+0x1C` = 0.0 and `+0x18` = 0; `z_dist`/`y_dist` are always-write reads (`0x00DACCB0`) handed to `0x0056AD50(z, y)` and are not stored in the record (what `0x0056AD50` does is OPEN). `0x00DAC830` returns −1 for a `NULL` node or at a `NULL` table entry.]** Other elements: `Aspect` (string, read), `lookat_offset` (`vec3`) with `z_dist` and `y_dist` (combined into an offset vector by `0x00DA38E0`), `min_elevation` `+0x10`, `max_elevation` `+0x14`, `default_elevation` `+0x1C` (write-if-present), `base_fov` `+0x20`, `blend_time` `+0x2C`, `x_shift` `+0x34`, `override_exit_blend_time` (`bool`) `+0x30`.
**`vehicle_fallbacks/vehicle_fallback`** (repeated; `0x0056A580`) — `name` matched against a **7-entry table at `0x012E3E3C`**: `nitrous`, `burnout`, `stunt cam 1`, `stunt cam 3`, `stunt cam 4`, `parachute open`, `chainsaw`; record `0x10` bytes at `0x013C92F0 + 0x10·index`: `distance` `+0x00` (`f32`), `rampin` `+0x04` (`s32`), `duration` `+0x08` (`s32`), `return` `+0x0C` (`s32`). **`Vehicle_Aims/Vehicle_Aim`** (repeated; `0x0056B880`) — `name`, `Aspect`, `Lookat_Offset` (`vec3`), `Min_Pitch` `+0x20`, `Max_pitch` `+0x24`, `X_Shift` `+0x28`, `Heading_Range` `+0x2C`, `Heading_Center` `+0x30`, `base_fov` `+0x34`, `z_dist` `+0x38`, `y_dist` `+0x3C`, and a `flags/Flag` list of names (repeated text elements; the name→bit map was **not** decoded **[Resolved: §17.5]**). The offsets of the `submode` and `Vehicle_Aim` fields are relative to the record the reader selected; the record bases are as stated only for `submodes` and `vehicle_fallbacks` (those of `Vehicle_Aims` were not tabulated **[Resolved: §17.5 — base `0x013CA2F0`, stride `0x50`, capacity 64]**).


**Review status (2026-09-30): DESK-PASS, text fixes applied (ten groups in real data, arithmetic shown); NEEDS-EXE: submode reader `0x0056B660` (Aspect filter, duplicate policy), top reader `0x0056DCA0` (`melee_lock_group`/`sway_group`); NEEDS-DATA: the real `panning_group` element names. Panning block arithmetic checks: `(0x012E3F20 − 0x012E3E58)/4 + 1 = 51` = 17 + 17 + 17 — desk review (not re-derived from the executable).**

**FOR TEAM B (2026-10-01, CORRECTION):** `submode` `min_elevation`, `max_elevation` and `default_elevation` are authored in degrees and stored in radians; `+0x18` flags a present `default_elevation`; `z_dist`/`y_dist` are always-write and are not record fields; a later duplicate `submode` name (after the `Aspect` filter) overwrites the earlier one.

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; `0x0056B660`, `0x0056DCA0`, `0x00DAC830`): `Aspect` filter, −1 skip, last-wins, submode destinations and the degree→radian conversions CONFIRMED — disassembly; `melee_lock_group`/`sway_group` CONFIRMED absent from the top reader — not cleared: `0x0056E4C0`, `0x0056AD50`, `0x00569FB0` (dampening names); NEEDS-DATA: real `panning_group` names.**

## 13. Cross-table reference map

Every "name field looked up in another table" found in this group, with the key type. **[CONFIRMED — disassembly for each row.]**

| Referencing field | Target table | Key / lookup | Function |
|---|---|---|---|
| `weather.xtbl` `Next_Stage_List/Next_Stage` | `weather.xtbl` `Name` | `_stricmp`, resolved to an entry pointer after load; `NULL` if unmatched | `0x005A8BD0` |
| `weather_time_of_day.xtbl` `Weather_Stages/Stage/Stage_Name` | `weather.xtbl` `Name` | `_stricmp` → cell index | inline in `0x005A89F0` |
| `skybox_effects.xtbl` `Weather_Stage` | `weather.xtbl` `Name` | `_stricmp` → `u16` index, `0xFFFF` if none | `0x005A8C20` |
| `wind.xtbl` `Next_Stage_List/Next_Stage` | `wind.xtbl` `Name` | `_stricmp` → entry pointer | inline in `0x005A93C0` |
| `lightning.xtbl` `VFX_List/VFX` | `skybox_effects.xtbl` `Name` | CRC-32 (seed 0) compared with the array's `+0x00`; unresolved names dropped | `0x005C3980` |
| `skybox_effects.xtbl` `Effect` | `effects.xtbl` `Name` | CRC-32 → hash table → slot index (effect index) | `0x005C50B0` |
| `rain.xtbl` `Effect`, `Camera_drop_effect` | `effects.xtbl` `Name` | as above | `0x005C50B0` |
| `groundfires.xtbl` `effects/effect` | `effects.xtbl` `Name` | as above | `0x005C50B0` |
| `weather_time_of_day.xtbl` orbital `Object` (dead path, §3.3) | `effects.xtbl` `Name` | as above | `0x005C50B0` |
| `effects.xtbl` `visual` | `vfx.xtbl` `Name` | `_stricmp`, linear scan → vfx index | `0x005C43F0` |
| `effects.xtbl` `Proximity_Refraction/Situation` | `refraction_situations.xtbl` `name` | CRC-32 (seed 0) stored, compared with the situation's `+0x00` at use time **[OPEN — desk review 2026-09-30: no function for the use-time comparison is named, so this row has no anchor despite the section label; real data never exercises it (Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`": "`Proximity_Refraction` present in 0/1,272"); list reads of `+0x8C` of the effects array `0x0143CB38`; to be settled against the executable.]** | — |
| `vfx.xtbl` `Radial_blur/Radial_blur_entry` | `radial_blur.xtbl` `Name` | `_stricmp`, linear scan → index | `0x005CE2D0` |
| `bitmap_materials.xtbl` `Bullet_Decal`, `Blast_Decal`, `Crash_Decal` | `decal_info.xtbl` `Name` | multiply-33 hash pool → decal index `(addr − 0x015535C8)/0xB8`, `−1` if none | `0x00746F00` |
| `effects.xtbl` `sound`, `sound_*switch_name`; `shells.xtbl` `Collision_Foley` | the audio name registry (`0x0046FD00`) | sound-name hash | `0x00462960` |
| `shells.xtbl` `Static_Mesh` | the static-mesh resource registry | mesh name → handle | `0x00904C10` |
| `camera_free.xtbl` `submode/name`, `vehicle_fallback/name` | fixed name tables in the image (`0x012E3D48`, `0x012E3E3C`) | `_stricmp` enum | `0x00DAC830` |
| `time_of_day_objects.xtbl` `Name` | fixed 5-name table (`0x0111E2D0`) | `_stricmp` enum | `0x00DAC830` |
| `bitmap_materials.xtbl` `Name` | fixed 33-name table (`0x0113DF70`) | `_stricmp` enum | inline |
| DOF situation selection (code, not data) | `dof_situations.xtbl` `name` | CRC-32 (seed 0) of a literal, e.g. `Satellite`, `Fine Aim` | `0x0058EF60` |

**Hash conventions in this group.** *CRC-32, seed 0, lower-cased* (`0x00D9E8B0`/`0x00D9E740`): `dof_situations`, `refraction_situations`, `effects`, `skybox_effects`, `groundfires`, `fade_categories`, and the two `effects`-side hashes above. *Multiply-33 bucket hash* (`0x00DAB330`, bucket count as a second argument): `camera_shake` (128 buckets), `decal_info` (32). *Rotate-6/XOR hash* (`0x00DA7890`): `external_light_override`. *No hash, `_stricmp` scan*: weather/wind stage names, vfx names, radial-blur names, enum tables.

**Table load order implied by the cross-references.** `weather` → `weather_time_of_day`; `effects` requires `vfx` (initialiser `0x005CE8D0` loads `radial_blur`, then `vfx`, then `effects`); `radial_blur` before `vfx`; `decal_info` before `bitmap_materials`; `skybox_effects`, `rain`, `groundfires` need `effects` loaded first (they resolve names at load time); `skybox_effects` needs `weather`; `lightning` needs `skybox_effects`. Whether the engine's initialisation order actually satisfies each of these was not checked.


**Review status (2026-09-30): DESK-PASS, text fixes applied (unanchored `Proximity_Refraction` row marked OPEN); NEEDS-EXE: consumer of effects `+0x8C` — desk review (not re-derived from the executable).**

## 14. Validation against the raw DLC tables

**Harnesses:** `tools/harnesses/env_dlc_validate.py` (effects, vfx, refraction_situations), `env_name_scan.py` (filename literals), plus the inline checks quoted in §15. **Data:** the raw-stored DLC archives `dlc1/2/3.vpp_pc` (container flag `0`, every entry raw — no mode-(a) question is touched), tables extracted to `tools/ah_dlc_xtbl/`. **Only the DLC copy of a table can be checked**: no base-game value of any table in this group was read.

### 14.1 Predicates (stated before running) and results **[CONFIRMED — empirical]**

| Predicate | Result |
|---|---|
| **P1.** Every element path found under `<Table>` in a raw DLC `effects` sample is a path the recovered `effects` schema reads (or the editor-only `_Editor`/`Category`) | **60 rows, 838 element-path instances, 0 unaccounted** (`dlc1/2/3_effects.xtbl`) |
| **P2.** Same for `vfx` | **60 rows, 516 instances, 0 unaccounted** |
| **P3.** Same for `refraction_situations` | **1 row, 18 instances, 0 unaccounted** (`dlc2_refraction_situations.xtbl`) |
| **P4.** DLC `effects` slot arithmetic: first `Info_Slot_Index` + row count of each framework equals the next framework's first index, and the end is ≤ 675 | dlc1 `606 + 21 = 627` = dlc2 start; `627 + 25 = 652` = dlc3 start; `652 + 14 = 666 ≤ 675` — **contiguous** |
| **P5.** Every `bool` element of the DLC `effects` rows parses as `true`/`false`/`yes`/`no` (case-insensitively) | **501 / 501** (all written `True`/`False`) |
| **P6.** Every `Streaming_Category` value of the DLC `vfx` rows is one of the six enum names | **60 / 60** (`Environment` 44, `Vehicle` 12, `Cutscene` 4; the four `Cutscene` rows are the ones the loader leaves unloaded) |
| **P7.** Every numeric `vfx` value (`Radius`, `Start_time`, `Fading_start`, `Fading_end`, `Minimum_time`, `Opacity`) matches the engine float grammar | **56 / 56** |
| **P8 (control).** The `effects` schema applied to a `vfx` file must **fail** (a schema test that cannot fail would be vacuous) | **117 unaccounted instances in `dlc1_vfx.xtbl`** — the test discriminates |
| **P9.** DLC `vfx` `VFX/Filename` values are `.effectx` source names and the archive carries the effect containers named as §10.3 predicts | Filenames: **60 / 60** end in `.effectx`; archive entries `dlc1/2/3_vfx_preload_containers.str2_pc`, `…_preload_containers.asm_pc`, `…_vfx_containers.asm_pc` present in all three archives (the containers themselves were not opened) |

**Coverage (the denominator for P1–P3).** The samples do **not** exercise every schema path: `effects` — 17 of 33 paths exercised; **not exercised:** `scale_factor`, the whole `Damage_Region` subtree and the whole `Proximity_Refraction` subtree (those two are confirmed from the code only). `vfx` — 14 of 23; **not exercised:** `Blocker` (`Opacity`, `LifeSpan`), `LOD/Spawning` (`Distance`, `View`), `LOD/Update` (`Minimum_time`), `Radius_expands`. `refraction_situations` — 16 of 17; **not exercised:** `Duration`. (`Duration` is the one element whose optionality is *seen* — it is absent from the only sample — matching the loader's `write-if-present` reading.)

**Other DLC-visible facts that agree with the code:** `Radial_blur_entry` values are `small` ×3, `large` ×6, `Quick large` ×3, `smaller` ×2 — the four radial-blur names the base `radial_blur.xtbl` must contain (a case-insensitive name, spaces allowed); 16 `Info_Slot_Index` values are present, of which exactly 3 (the first row of each file) are non-zero — matching "read only from the first matching row".

### 14.2 What this validation does **not** establish

- Nothing about the **base-game** rows (`weather`, `weather_time_of_day`, `wind`, `rain`, …): their schemas rest on the loader disassembly alone, with the two independent code-level agreements noted in §3.3 (the mission-override reader writes the same record offsets) and §7.4 (the DOF outputs feed the cutscene slots).
- **Absent-element behaviour** of the always-write accessors (§1.4) — authoring data supplies these elements, so no sample can show it.
- **Value semantics** (units of `Density`/`Speed`/…): no unit constant beyond those stated (÷100 `Density`, ÷255 colours, HHMM times, ×1000 groundfire durations, degrees→radians decal slopes (then cosine, §17.3), squared fade distances) is applied by the loaders; the rest are stored verbatim.


**Review status (2026-09-30): DESK-PASS (60 + 60 + 1 = 121 rows, 838 + 516 + 18 = 1,372 instances, P4 slot arithmetic recomputed; coverage disclosure stated) — desk review (not re-derived from the executable).**

## 15. Adjacent finding: the mission time-of-day override files and their flattened vocabulary

Not one of the 27 named tables, but found while cross-checking §3 and directly relevant to it: a **third** reader of the time-of-day lighting record, with a *flattened, lower-case* element vocabulary, used by per-mission override files. Recorded here because it independently confirms §3.3's offsets and exposes a naming disagreement. **[CONFIRMED — disassembly for the names and destinations quoted; CONFIRMED — empirical for the DLC agreement.]**

### 15.1 What it is

Per-mission **time-of-day overrides** are `.xtbl` files whose rows are **`mission_override`** elements. The raw DLC archives hold seven: `dlc1_genki_tod_override.xtbl`, `dlc2_m01/m02/m03_tod_override.xtbl`, `dlc3_m01/m02/m03_tod_override.xtbl`; the base game's are in the virtual directory the reader enumerates (the literal `data\tables\tod\mission_overrides`, extension `xtbl`; **no filename literal exists**, so these files are found by directory listing, not by name). Anchors: **`0x006E2460`** (enumerates and parses every file: reads `mission_override` rows, `mission_name`, `skybox_mesh_filename`, `cloud_mesh_filename` — the latter two are turned into resource names of the types `rfg_skybox` / `skybox_clouds`), **`0x006E2150`** (applies one row: `mission_name`; **`time`** = `f32` — DLC values `2.0`, `5.0`, `14.5`, `21.0`, i.e. **hours of the day**, default −1.0 = "none"; **`weather_stage`** = a `weather.xtbl` stage name, resolved with `0x005A8BD0` — DLC values `Clear Skies`, `Heavy Rain`, `Post-Storm`; and an optional child **`external_light_override`**, presumably a light-override block as §9.3), **`0x00BA0140`** (reads the flattened lighting vocabulary into the §3.2 cell) and a sibling reader `0x00BA4A10` over the same vocabulary (not traced).

### 15.2 The flattened vocabulary (94 names, pointer table `0x01310F10`–~~`0x01311074`~~`0x01311084`)

~~**[Desk review 2026-09-30: the range does not fit 94 contiguous 4-byte pointers — `0x01310F10 + 93 × 4 = 0x01311084` would be the last entry, while `0x01311074` is entry 89. The start is consistent with §3.3's sub-range `0x01310F58`–`0x01310FA8` = entries 18–38 (21 names, `particle_ambient_color` … `tonemap_lum_offset`), and the list below does count 94 (82 + 12).]** **[OPEN — desk review 2026-09-30: either the end address is 16 bytes short or four names are stored elsewhere; dump 94 entries from `0x01310F10`; to be settled against the executable.]**~~ **[CORRECTED — CONFIRMED — disassembly 2026-10-01: the last entry `0x01311084` is read by the TOD-definition reader `0x00BB6400`, the first (`0x01310F10`) by `0x00BA0140`/`0x00BA4A10`; 94 entries. The strings themselves were not re-dumped (names 90–93 not re-verified).]**

`east0`–`east3`, `west0`–`west3`, `zenith` (colours) · `fog_ground`, `fog_atmosphere_scale`, `fog_density_new`, `fog_density_offset`, `fog_color`, `ambient_color`, `back_ambient_color`, `window_color`, `tod_light_color`, `particle_ambient_color`, `particle_tod_light_color` · `ldr_min`, `ldr_max`, `bloom_exposure`, `iris_rate`, `luminance_max`, `luminance_min`, `luminance_mask_max`, `eye_adaption_base`, `eye_adaption_amount`, `eye_fade_min`, `eye_fade_max`, `brightpass_threshold_new`, `brightpass_offset_new`, `bloom_amount`, `bloom_theta`, `bloom_slope_A`, `bloom_slope_B`, `tonemap_lum_range`, `tonemap_lum_offset` · `ground_reflection_gloss`, `ground_reflection_brightness`, `ground_reflection_spec_refl_brightness`, `star_strength`, `meteor_strength` · `clouds_front_light_color`, `clouds_rear_light_color`, `clouds_horizon_front_light_color`, `clouds_horizon_rear_light_color`, `horizon_mountain_color`, `horizon_mountain_color_back`, `horizon_mountain_fog_color`, `horizon_mountain_fog_density`, `horizon_mountain_normal_map_height`, `horizon_layer0_strength` … `horizon_layer3_strength`, `horizon_storm_strength`, `overhead_layer0_strength` … `overhead_layer3_strength`, `overhead_storm_strength`, `cloud_backlight_strength`, `cloud_backlight_power`, `cloud_layer01_speed`, `cloud_layer23_speed` · `lut_filename`, `water_ambient_color`, `water_diffuse_color1`, `water_diffuse_color2`, `water_specular_color`, `water_specular_alpha`, `water_specular_power`, `water_falloff_color`, `water_fog_color`, `water_crest_color`, `water_crest_threshold`, `ambient_audio_rtpc`, `desired_brightness`, `exposure_min`, `exposure_max` · and (indices 82–93, used by other readers of the table, e.g. `0x00B9E4D0`, `0x00BB6400`) `horizon_mountain_enabled`, `fog_camera_follow`, `modified`, `day_begin`, `day_end`, `skybox_mesh_filename`, `cloud_mesh_filename`, `cloud_mesh_horizon_mat`, `cloud_mesh_overhead_mat`, `cloud_mesh_skyline_mat`, `tod_segments`, `segment`. Scalars are read with C `atof` (`0x00DE0200`), colours as **`vec4` text "r g b a"** (`0x00DE03E0`), `lut_filename` as plain text (not the `LUT_Filename/Filename` nesting of §3.3).

### 15.3 What it confirms and what it changes in §3.3

- **Offsets confirmed.** Every scalar destination of `0x00BA0140` is the §3.3 offset of the same-named parameter: `fog_ground` `+0x100`, `fog_atmosphere_scale` `+0x104`, `fog_density_new` `+0x108`, `fog_density_offset` `+0x10C`, `ldr_min` `+0x110` … `tonemap_lum_offset` `+0x158` (the 19 names `ldr_min`…`tonemap_lum_offset`, same order as §3.3's direct-child group), `ground_reflection_gloss` `+0x20C`, `ground_reflection_brightness` `+0x210`, `ground_reflection_spec_refl_brightness` `+0x214`, `star_strength` `+0x218`, `meteor_strength` `+0x21C`, `horizon_mountain_fog_density` `+0x1F4`, `horizon_mountain_normal_map_height` `+0x1F8`, `cloud_backlight_power` `+0x1FC`, `cloud_backlight_strength` `+0x200`, `water_specular_alpha` `+0x2A0`, `water_specular_power` `+0x2A4`, `water_crest_threshold` `+0x2D8`, `ambient_audio_rtpc` `+0x2DC`, `desired_brightness` `+0x2E0`, `exposure_min` `+0x2E4`, `exposure_max` `+0x2E8`. **[CONFIRMED — disassembly; this is an independent second reader.]**
- **The four "cloud layer" floats.** The flattened names give **`horizon_layer0…3_strength` at `+0x19C`, `+0x1A0`, `+0x1A4`, `+0x1A8`** (then `horizon_storm_strength` `+0x1AC`) and **`overhead_layer0…3_strength` at `+0x1B0`, `+0x1B4`, `+0x1B8`, `+0x1BC`** (then `overhead_storm_strength` `+0x1C0`). §3.3's `weather_time_of_day` reader labels the same offsets `Cloud_Layer_Strength`, `Cloud_Normal_Map_Height`, `Cloud_Highlight` and the un-authorable element `?`. **The two vocabularies disagree on the *names* of slots 1–3 of each band while agreeing on the offsets and on there being four.** The reading that fits both is: **each cloud band has four per-layer strengths (`+0x19C…+0x1A8`, `+0x1B0…+0x1BC`) and the `weather_time_of_day` element names for slots 1–3 are misnomers** (and, because the fourth is spelled `?`, `weather_time_of_day.xtbl` can never set it). **[HIGH CONFIDENCE — inferred: offsets and count are identical in two independent readers; which vocabulary is "right" is not established, and a `Cloud_Normal_Map_Height`/`Cloud_Highlight` reading is not excluded.]** A reimplementation should key the record by **offset/slot**, and accept both spellings.
- **Cloud speed.** `cloud_layer01_speed` `+0x204` and `cloud_layer23_speed` `+0x208` are the offsets §3.3 calls the Horizon and Overhead `Cloud_Speed` — consistent with "layers 0/1" and "layers 2/3" being the horizon and overhead bands.

### 15.4 Validation against the raw DLC override files **[CONFIRMED — empirical]**

7 files, 7 `mission_override` rows, **251** child-element instances, classified by the 94-name vocabulary: **217** are names of that vocabulary, **24** are the row keys (`mission_name` ×7, `time` ×7, `weather_stage` ×7, `external_light_override` ×3), and **10** are the flat orbital-object elements `orbital_object_name` (×4; values `02-noonsun`, `04-moon`), `tint` (×1; four numbers, i.e. RGB + intensity), `opacity` (×4) and `scale` (×1) **[2026-09-30, desk review: the copied values were removed (clean-room)]** — the flat analogue of §3.3's `Orbital_Objects/Object/{Object_Name, Tint, Opacity, Scale}`, read at `0x00BA25E0` (a fourth reader, not tabulated). **251 / 251 accounted for; 0 unknown names.** The orbital-object names have the shape `NN-name`, the shape §3.2's 30-byte per-cell orbital names carry. The `external_light_override` child (3 of 7 rows) holds repeated **`ELO`** rows with exactly the elements of §9.3 (`Name`, `front_color`, `back_color`, `front_intensity`, `back_intensity`; e.g. `Common`, `common_bounce`, `common_bright`) — presumably the same light-override reader with a different row-element name **[HIGH CONFIDENCE — inferred from identical element names; the call from `0x006E2150` was not traced]**. Note the DLC `back_color` here has three components (`"1.0 1.0 1.0"`), which the `sscanf("%f %f %f %f")` of §9.3 accepts (the fourth stays 1.0).


**Review status (2026-09-30): DESK-PASS, text fixes applied (pointer-table range arithmetic shown, OPEN); NEEDS-EXE: `0x00BA0140` table end, `0x00BA4A10`, `0x00BA25E0`. 251 = 217 + 24 + 10 recomputed; the cloud-layer naming stays HIGH CONFIDENCE — desk review (not re-derived from the executable).**

**Review status (2026-10-01): re-derived from the executable 2026-10-01 (job `20261001T123123-team-a-ytgi`; xrefs): table range CORRECTED to `0x01310F10`–`0x01311084` (94 entries) — not cleared: the bodies of `0x00BA0140`, `0x00BA4A10`, `0x00BA25E0`, `0x00BB6400`; the cloud-layer naming stays HIGH CONFIDENCE.**

## 16. Open items, status and artifacts

Checkpoint state at the coordinator's pause (2026-09-20): **schemas complete at loader level for 24 of the 26 loaded tables; two partial; one skipped (no literal).** Status per table is repeated in `HANDOFF.md` §30 (archived §27.1).

**Status.** *Complete (every element the loader reads, with type and destination):* `weather`, `weather_time_of_day`, `wind`, `rain`, `lightning`, `lens_flares`, `motion_blur`, `radial_blur`, `dof_situations`, `refraction_situations`, `skybox_effects`, `time_of_day_objects`, `external_light_override` (reader complete; consumers open), `effects`, `vfx`, `interface_effects`, `decal_info`, `groundfires`, `shells`, `material_color_variants`, `bitmap_materials`, `map_districts`, `fade_categories`, `camera_shake`. *Partial:* `camera_free` (every name and global destination of the flat groups, both name tables and the two array record layouts are listed; ~~the `Vehicle_Aim` `flags/Flag` name→bit map and the `Vehicle_Aims` record base are not decoded), `bitmap_sheets` (row shape and capacity only; what the sheet loader does with each name is not decoded)~~ **§17 (2026-09-23): both now substantially resolved against real base data — `camera_free`'s `Vehicle_Aim` flags map and `Vehicle_Aims` record base (§17.5) and `bitmap_sheets`'s per-name companion-file mechanism (§17.4) are CONFIRMED; `camera_free` keeps a smaller "Partial" tag only for two newly-found, still-undecoded groups (`melee_lock_group`/`sway_group`, §17.5) and the 69 submodes' per-mode meaning.** *Skipped:* `materials.xtbl` (no standalone filename literal) — confirmed a real, inert, loader-less entry, §17.6.

**Open items**

1. **Run-time consumers** were not traced for any table: how `weather` stage transitions use `Chance`/`Average_Duration`/`Variance`/`Next_Stage_List`; how the wind state machine (`0x005A9990`) picks stages; how a `Lightning_Type` is picked; what reads the `external_light_override` records (slot 1) and the `weather_time_of_day` cells (blending between segments/stages, the presence-bit semantics — "override present" is the reading fitted to the parser, not seen in a consumer); the readers of the `0x0290FBB0` orbital-object list (its origin and the meaning of its `0x5D0` stride).
2. **Absent-element behaviour of the always-write accessors** (§1.4): the run-time value is stack residue; not modelled. Worth a one-line check in `spec-vehicle-data.md` §7.2 by its owner (that section says "0 if absent"). *(Already done: `spec-vehicle-data.md` §7.2 was corrected 2026-09-20.)*
3. **The cloud-layer naming disagreement** (§15.3): which vocabulary names the four per-band strength slots correctly.
4. **The mission-override family** (§15): the `0x00BA4A10` sibling reader, the `0x00BA25E0` orbital sub-reader, the `0x006E2150` call into the light-override reader, and the meaning of the vocabulary indices 82–93 (`day_begin`, `day_end`, `tod_segments`, `segment`, …) — the last suggest another, TOD-*definition* file format read at `0x00B9E4D0`/`0x00BB6400` that has no filename literal in this group and was not chased.
5. **DLC `vfx_preload_containers` mounting** (§10.2) and the contents of the `…_vfx_containers` groups (not opened).
6. **The second readers** `0x00739A80` (`effects`) and `0x00ACEAE0` (`vfx`) register name→string / name→flag maps through a virtual call; their consumer is unknown.
7. ~~**`decal_info` slope-fade trig function** (§11.2): sine vs cosine of `0x00EA3A70` unresolved (HYPOTHESIS: cosine).~~ **RESOLVED, §17.3: `0x00EA3A70` is `cos` (disassembly: `FCOS` instruction + `"cos"` domain-error string).**
8. **Capacity of `map_districts` and `interface_effects`** (`0x012FC87C` value) and `radial_blur` (200 by layout) are layout-derived, not checked in code.
9. ~~**`camera_free` `Vehicle_Aim` `flags`**, and the record bases of `Vehicle_Aims`~~ **RESOLVED, §17.5** (record base `0x013CA2F0`, stride `0x50`, capacity 64; the 6-flag name→bit map). Still open: the per-mode meaning of the 69 real submode entries, and the destinations of the two undocumented groups `melee_lock_group`/`sway_group` found in §17.5.
10. ~~**No base-game values.** Every value-level statement in this document rests on DLC samples; base-game tables remain behind the parked mode-(a) limitation (`HANDOFF.md` §27.3), untouched here.~~ **→ CLOSED, §17** (real base-game tables extracted and validated; run-time consumption still untraced, §17.9).

**Review status (2026-09-30): DESK-PASS (status consistent with §17; strike-throughs already applied) — desk review (not re-derived from the executable).**

**Artifacts.** Ghidra post-scripts `tools/scripts/AkTrace.java` (argument-resolving call tracer), `AkTree.java` (callee-closure decompiler), `AkPtrStr.java` (pointer-table dumper), with `AfStrXrefs.java`, `AfDec.java`, `AfMem.java`, `AfRangeRefs.java`, `AfCallers.java`, `AfAsm.java` from AF; runners `tools/run_tbl4.ps1`, `tools/run_ak_traces.ps1`; harnesses `tools/harnesses/env_name_scan.py`, `env_dlc_validate.py`, `env_append.py`. Dumps: `tools/ak_*.txt` (`ak_tr_<table>.txt` = per-table call traces; `ak_dec_*.txt` = decompiles; `ak_refs_*.txt`, `ak_callers_*.txt` = reference/caller lists; `ak_ptrs.txt` = pointer-table strings).

## 17. Empirical validation against real base-game data (2026-09-23, agent AK-2)

**Every table this spec covers was located and extracted from the now-readable base containers** (`spec-vpp-container.md` §7/§8) and parsed tolerantly (a hand-written mini-parser, `tools/harnesses` scratch copy, tolerant of the three shipped-data quirks of `spec-xtbl-format.md` §7 — none of the four quirk files are in this table group, but the parser does not assume strict XML either way). **All 27 named tables were found as real top-level entries**, 26 with a working loader (as §1.1 already established) plus `materials.xtbl` itself (§17.6). Sources: `misc_tables.vpp_pc` (26 of the 27, all but the second `map_districts.xtbl` copy), `da_tables.vpp_pc` (a second, byte-identical `map_districts.xtbl`), `cutscene_tables.vpp_pc` (a second, byte-identical `effects.xtbl` **[OPEN — desk review 2026-09-30: Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`" calls this "a second, distinct base copy ... (worth Team A diffing the two)"; whether the two copies are byte-identical needs a hash of both entries; to be settled against real data.]**), `patch_compressed.vpp_pc` (post-launch-patch copies of `camera_free.xtbl` and `bitmap_sheets.xtbl`, each a superset of the `misc_tables.vpp_pc` copy — see §17.5/§17.4). This closes §16 open item 10 ("no base-game values") for every table in this group.

### 17.1 Row counts and structural facts, confirmed against real data **[CONFIRMED — empirical]**

| Table | Real base row count | Notes |
|---|---|---|
| `weather.xtbl` | **5** `Weather_Stage` | names below (§17.8); matches the loader's "capacity fixed at first load = row count" rule |
| `weather_time_of_day.xtbl` | **4** `Weather_Time_Segment` | **not pre-sorted by `Start_Time` in the shipped file** — authored order is `sunrise(800), noon(1430), sunset(1900), night(500)`; the loader's `qsort` (§3.1) is doing real work, not a no-op (recorded here only) |
| `wind.xtbl` | **2** `Wind_Stage` | `Calm`, `Light Wind` — near-duplicate settings (§17.8) |
| `rain.xtbl` | **5** `Level` | `Density` values `0, 88, 90, 95, 100` — already ascending in the file, consistent with (but not proof against) the loader's own sort |
| `lightning.xtbl` | **3** `Lightning_Type` | |
| `lens_flares.xtbl` | **6** `Flare` | ≤ 8, so the table loads (the ignore-if->8 guard is not triggered) |
| `motion_blur.xtbl` | **1** `Motion_Blur_Settings` | as expected (not a row array) |
| `radial_blur.xtbl` | **6** `Radial_blur` | 4 of 6 names were already known from DLC `vfx` references; `very large` and `EDF_Artillery_Gun` were not (§17.8) |
| `dof_situations.xtbl` | **4** `DOF_situation` | exactly the two base/Advanced pairs `Fine Aim`/`Fine Aim Advanced`, `Zoom`/`Zoom Advanced`; **no `Satellite` row** (§17.8) |
| `refraction_situations.xtbl` | **26** `Refraction_Situation` | half are `"...(LR)"` low-res-variant duplicates of the other half |
| `skybox_effects.xtbl` | **8** `Skybox_Effect` | `Weather_Stage` element absent from all 8 (every base skybox effect applies to any weather) |
| `time_of_day_objects.xtbl` | **3** `Object_Type` (of 5 possible) | `Street Lights` and `Searchlights` have no row — those two records keep the code's pre-set default pair forever in the base game |
| `external_light_override.xtbl` | **22** `light_override` | |
| `effects.xtbl` | **606** `Effect` | **exactly** the value the DLC slot arithmetic already implied (§10.1.1: DLC starts at 606) — direct structural confirmation |
| `vfx.xtbl` | **529** `Effect` | 128 rows carry an `LOD` block; 17 of those 128 use a non-conforming flattened form (§17.7) |
| `interface_effects.xtbl` | **5** `InterfaceEffect` | |
| `decal_info.xtbl` | **14** `Decal_Info` | well under the layout capacity of 32 |
| `groundfires.xtbl` | **4** `Groundfire` | |
| `shells.xtbl` | **3** `Shell` | well under the 8-row read cap |
| `material_color_variants.xtbl` | **145** `color_entry` | well under the ≤257 capacity |
| `bitmap_materials.xtbl` | **32** `Bitmap_Material` (of 33 named slots) | every slot except the internal `not set` fallback has a real row — see §17.8 |
| `bitmap_sheets.xtbl` | **55** `BitmapSheets` (misc_tables) / **63** (patch_compressed) | fully resolved, §17.4 |
| `map_districts.xtbl` | **5** `Map_district` | identical row set in both archive copies |
| `camera_shake.xtbl` | **70** `Camera_Shake` | well under the 128-row read cap |
| `camera_free.xtbl` | **1** `Camera` record; **69** `submode`, **7** `vehicle_fallback`, **36** `Vehicle_Aim` (misc_tables) / **38** `Vehicle_Aim` (patch_compressed, adds `UFO Aim`/`UFO Fine Aim`) | fully resolved, §17.5 |
| `fade_categories.xtbl` | **11** `Category` | includes `Default`, whose authored values equal the code's hard-coded defaults exactly (§17.8) |
| `materials.xtbl` | **25** `Material` | real entry, no loader — §17.6 |

### 17.2 Coverage: what real rows add beyond the loader-derived schema

The method throughout this section: extract every row's full element-path set (recursively) from the real archive copy, diff against the documented schema, and for anything extra check whether the row reader's own disassembly trace (already captured in `tools/ak_tr_*.txt`/`ak_dec_*.txt` by the original pass, re-read here) contains a lookup for that exact literal anywhere in the function body — an exhaustive per-function check, not a known-field list, per `HANDOFF.md` §5. Every base `.xtbl` file also carries a `<TableDescription>` block (the authoring tool's own schema, `spec-xtbl-format.md` §2–§3's "schema-block-first" cousin, here appended *after* `<Table>`) which is a second, independent source of the full **authored** vocabulary — including fields the tool allows an author to type in that the loader was never wired to read. Both sources were used.

**Confirmed dead (present in the TableDescription and/or in real rows; the row reader's full function trace never looks up the name):**

- `weather.xtbl`: `Moon/Moon_Opacity`, `Rain_Settings/Lightning_Settings/Min_Distance`, `Rain_Settings/Lightning_Settings/Max_Distance` — all three are present in the real file's `<TableDescription>` (typed `Float`) and the reader (`0x005A8510`, full 215-line trace re-read start to `RET`) never requests any of the three literals; the trace requests `Moon_Color_Multiply` (not `Moon_Opacity`) and `Lightning_Settings/{Frequency,Variance}` only (not `Min_Distance`/`Max_Distance`). **[CONFIRMED — disassembly, exhaustive: the traced function body was read in full, not grepped for a known-name list.]**
- `weather_time_of_day.xtbl`'s lighting-parameter block (`0x00BA1600`, full 307-line trace re-read start to `RET`): `District_Lighting/Lighting_Parameters/{Fog_Skybox_Scale, Fog_Skybox_Offset, Fog_Clouds_Scale, Fog_Clouds_Offset, Fog_Matte_Scale, Fog_Matte_Offset, Fog_Camera_Min, Fog_Camera_Max}` (8 elements — present in the TableDescription, absent from all 4 real rows, never read) and `District_Lighting/Color_Correction/{Saturation, Tint, Tint_Intensity}` (present in the TableDescription, absent from all 4 real rows, never read — only `Window_Tint`/`Window_Tint_Intensity`/`LUT_Filename` are read, exactly as §3.3 already said). `effects.xtbl`'s TableDescription additionally lists `tod_controlled` (typed alongside `use_mission_srid`), present in no real row and never looked up by the full 169-line trace of `0x005C4B10`. **[CONFIRMED — disassembly, exhaustive for all four traces.]** None of this contradicts §1.4's accessor catalogue; it just shows the authoring tool's schema running ahead of (or behind) the loader in several places, independently in three different tables.
- `material_color_variants.xtbl` and `external_light_override.xtbl` both show **mixed content** (`spec-xtbl-format.md` §8's 56-entry/1,827-element phenomenon, here seen directly): every `Color`/`front_color`/`back_color` element carries its real "r g b a" value as the element's own leading text *and*, in about half the rows, decorative child elements `<R>255</R><G>255</G><B>255</B>` (always exactly 255/255/255, an editor colour-swatch artifact) after it. Because §11.5/§9.3 already establish that the reader takes the element's own text (`sscanf`), these children are inert either way — no correction needed, but worth recording since a strict-XML reader that took `.text` as "only the text before the first child" (as most parsers do) reads correctly, while one that concatenated all descendant text would not.

**Confirmed live but unreachable from this table (reachable only through the mission-override sibling reader, §15):** `weather_time_of_day.xtbl`'s own `<TableDescription>` for the `Water` block ends at `Crest_Color_Intensity` — **`Crest_Threshold` and `Skybox_Parameters/Mountain_Normal_Map_Height` are absent from this table's authored schema entirely** (zero occurrences anywhere in the 3,619-line real file, not merely absent from the 4 rows), yet `0x00BA1600`'s trace does read both, at `+0x2D8` and `+0x1F8` respectively, exactly as §3.3 already recorded. So in the base game these two parameters can only ever be set by a per-mission override file (§15), never by `weather_time_of_day.xtbl` itself — a real, data-confirmed asymmetry between the two readers' *reachable* vocabularies, sharper than §15.3's existing note (which only covers the four cloud-layer slots).

**A genuine authored/loader name mismatch (new — not merely an unread field) — `Ground_Reflection_Spec_Brightness` vs `Ground_Reflection_Spec_Refl_Brightness`:** `0x00BA1600` looks up the literal **`Ground_Reflection_Spec_Brightness`** (confirmed four independent ways: the disassembly trace, the decompile, and the executable's own string table at `0x0118CC9C`), but **every real row** — all 12 instances across the 4 segments × the (non-`Clear Skies`) stages **[OPEN — desk review 2026-09-30: 4 segments × 4 stages gives 16 cells (§17.2, last paragraph), so either 4 cells omit the element or the count is wrong; to be settled against real data.]** — and the table's own `<TableDescription>` spell it **`Ground_Reflection_Spec_Refl_Brightness`** (with "Refl"; also the string the *mission-override* reader's pointer table uses, `0x0118C658`, §15.2's `ground_reflection_spec_refl_brightness`). `0x00DACD40` (§1.4: write-only-if-present) therefore **never finds this child in any base `weather_time_of_day.xtbl` row**, and the field at record offset `+0x214` — and its presence bit `P(2F4,4)` — stay at whatever the per-cell initialiser leaves them (cleared), in every shipped weather-stage × time-of-day combination. **The parameter is functionally dead when set through `weather_time_of_day.xtbl`; it is settable only through a mission-override file's flattened `ground_reflection_spec_refl_brightness`.** **[CONFIRMED — disassembly + CONFIRMED — empirical: two different literal-string addresses in the binary, `0x0118CC9C` "Ground_Reflection_Spec_Brightness" and `0x0118C658` "ground_reflection_spec_refl_brightness", cross-checked against 12/12 real authored instances of the "Refl" spelling.]**

**A confirmed structural fact about the sparse-override design (§3.1/§3.2):** in every one of the 4 real `Weather_Time_Segment` rows, exactly **4** of the 5 weather stages have a `<Stage>` cell — **`Clear Skies` (the base game's most common stage, the largest `Chance` weight of the five) never appears as a `Stage_Name` in any segment.** Its cell is therefore entirely absent (all presence bits clear, "inherit") for every time of day, in the shipped base game — the one concrete instance behind §3.1's "cells never named stay zeroed" rule.

### 17.3 `decal_info.xtbl` slope-fade trigonometry — HYPOTHESIS resolved to CONFIRMED (cosine) **[CONFIRMED — disassembly]**

§11.2's open question — which of sine/cosine `0x00EA3A70` computes — is settled by decompiling the function itself (`tools/gp_tbl4`, `AfDec.java`, addresses `0x00EA3A70`/`0x00EA3AC8`/`0x00EB3A70`): `0x00EA3A70` is the CRT's SSE2-vs-x87 dispatch wrapper (checks `MXCSR`/the FPU control word, exactly the shape of MSVC's `sin`/`cos` runtime dispatchers), and its x87 fallback path, `0x00EA3AC8`, executes the **`FCOS`** instruction (`0x00EA3ADD` and again at `0x00EA3B14` after an argument-reduction retry loop) and — on a domain error — loads the error-context string **`"cos"`** from `0x013447D0`. **`0x00EA3A70` is `cos`/`cosf`, not `sin`/`sinf`.** The real base `decal_info.xtbl` values are consistent with this (e.g. `item_shimmer`'s `Slope_Fade_Start=0°`/`Slope_Fade_End=90°` is exactly cosine's full range `1→0`), though the value shapes alone (`Start < End`, both in `[0°,90°]`) would have been equally consistent with sine used the other way around — the disassembly, not the data, is what actually decides it. §16 open item 7 is closed.

### 17.4 `bitmap_sheets.xtbl` — the "what happens after" question, RESOLVED **[CONFIRMED — disassembly + CONFIRMED — empirical]**

Decompiling the per-sheet processor `0x00E21F70` (called once per `BitmapSheets/Name` by the loader, §11.7) shows it takes the sheet's name, replaces/adds the extension with the literal at `0x012A2970` — which is **`".xtbl"`** — and parses the resulting file (`<sheet-name>.xtbl`) as a **second, small table**: `Table` → `BitmapSheets` → one `Name` (matched against the literal `"Name"`, `0x0129EE6C`) → `Images` → repeated `Properties`, each read for `StartX`, `StartY`, `ImageWidth`, `ImageHeight` (integers, `0x00DC5270`) into a **global flat array** at `0x02A4E398`, stride `0x18` (24 bytes: `+0x00` interned sub-image name, `+0x08` the caller's argument 1, `+0x0C` `StartX`, `+0x10` `StartY`, `+0x14` `ImageWidth`/`ImageHeight` pairing per the decompile — exact byte roles of `+0x08`/final dword not disambiguated further **[OPEN — desk review 2026-09-30: the four integers `StartX`, `StartY`, `ImageWidth`, `ImageHeight` are mapped to the three dwords `+0x0C`–`+0x14`, and §17.9 names a `+0x18` dword that lies outside a `0x18`-byte record; the layout is internally inconsistent; `0x00E21F70`; to be settled against the executable.]**), capacity **`0x680` = 1,664** sub-images total across all sheets, each keyed by CRC-32 of its own name via an open-addressing insert (`0x00E21E80`). **Empirically confirmed exactly as predicted:** `misc_tables.vpp_pc` contains 55 files named `<sheet>.xtbl` — `ui_bms_00.xtbl`, `ui_bms_01.xtbl`, … — one per `bitmap_sheets.xtbl` row; a real sample (`ui_bms_00.xtbl`, 58,238 bytes) has exactly the predicted shape: each `Properties` record holds `Name`, `StartX`, `StartY`, `ImageWidth`, `ImageHeight`, `ImageIsInset` and `PersistFilename` (a `.tga` authoring path) **[2026-09-30, desk review: a verbatim sample record was replaced by this description (clean-room)]** — confirming both the read fields (`StartX`/`StartY`/`ImageWidth`/`ImageHeight`) and, as a bonus, the original developer's authoring path convention (`.tga` source per §11.7) and two further **unread** fields per sub-image, `ImageIsInset` and `PersistFilename` (an absolute authoring-tool path — editor-only, never looked up by `0x00E21F70`). §11.7 moves from *OPEN* to *CONFIRMED — disassembly + empirical*; §16's "Partial" classification for `bitmap_sheets` is retired.

### 17.5 `camera_free.xtbl` — `Vehicle_Aims` record base and `Vehicle_Aim` flags map, RESOLVED; two undocumented groups found **[CONFIRMED — disassembly + CONFIRMED — empirical]**

Decompiling the `Vehicle_Aim` row reader `0x0056B880` in full (`tools/ak_dec_vehaim_flags.txt`) resolves everything §12.2/§16 item 9 left open:

- **Record base and capacity.** The array starts at **`0x013CA2F0`**, stride **`0x50`** (0x14 dwords), and the loader's own bound check (`0x13CB6EF < record_pointer → stop`) gives capacity **`(0x13CB6F0 − 0x13CA2F0) / 0x50 = 64`** `Vehicle_Aim` slots. The real base table uses 36 of them (38 in the post-launch-patch copy, which adds `UFO Aim`/`UFO Fine Aim`).
- **Field layout** (offsets from the record base): `+0x00` a name-derived key (via the CRC/intern helper `0x00D9E8B0`); `+0x10..+0x1C` `Lookat_Offset` (4 dwords via the same `0x00DA38E0` combiner used by `submodes`, of which only the first 3 are the authored `X`/`Y`/`Z`); `+0x20` `Min_Pitch`; `+0x24` `Max_pitch`; `+0x28` `X_Shift`; `+0x2C` `Heading_Range`; `+0x30` `Heading_Center`; `+0x34` `base_fov`; `+0x38` `z_dist`; `+0x3C` `y_dist`; `+0x40` the flags byte (below).
- **A unit conversion not previously documented for this table:** after the flags loop, the reader multiplies **`Min_Pitch`, `Max_pitch`, `Heading_Center` and `Heading_Range`** by the same degrees→radians constant used elsewhere in this document (`0x012A30D8`, §11.2's decal slope-fade constant) — **these four fields are authored in degrees and stored in radians**, ~~exactly the same convention as `decal_info.xtbl`'s `Slope_Fade_*`~~ **(corrected 2026-09-30:** the same ×0.017453 constant `decal_info.xtbl`'s `Slope_Fade_*` uses; `decal_info` additionally takes the cosine, §17.3). `X_Shift`, `z_dist`, `y_dist`, `base_fov` are not converted.
- **The `Vehicle_Aim` `flags/Flag` name→bit map, fully and exhaustively resolved** (the decompile shows every branch of the comparison chain, with no unexamined fallthrough): `limit_heading_range` → `0x01`, `turn_player_toward_camera` → `0x02`, `turn_camera_with_vehicle` → `0x04`, `allow_zoom` → `0x08`, `target_player` → `0x10`, `use_seat_pos` → `0x20`. **Empirically, only two of the six are ever used** across all 36 (38) real rows: `limit_heading_range` (the 4 helicopter primary/secondary aims plus the 2 heli-skid aims — 8 rows) and `use_seat_pos` (the 16 seated-passenger/turret aims: tank/bike/ball/passenger/boat variants). `turn_player_toward_camera`, `turn_camera_with_vehicle`, `allow_zoom`, `target_player` are authored nowhere in the base game.
- **An aspect-ratio row filter, not previously noted:** a `Vehicle_Aim` row is skipped entirely (never added to the array) unless its `Aspect` is `"all"`, or its `Aspect` is `"widescreen"`/`"standard"` matching the truth of a runtime display-mode flag (`0x01493661`) — i.e. `Aspect` is not just stored data (as §12.2 says) but an active load-time filter keyed to the current display-mode setting.
- **Two wholly undocumented top-level groups**, found in the real file's `<TableDescription>` and confirmed present with real values in the row: **`melee_lock_group`** (`Max_COC_Radius`, `Preblur_Radius`, `Near_Dist`, `Ideal_Near_Dist`, `Ideal_Far_Dist`, `Far_Dist`, `Vertical_Rotation`, `Horizontal_Rotation`, `Vertical_Rotation_Finisher`, `Horizontal_Rotation_Finisher`, `Vertical_Rotation_Zombie`, `Horizontal_Rotation_Zombie`, all carrying real values) and **`sway_group`** (`roll_offset_multiplier`, `drunk_sway_speed`, `max_drunk_heading`, `max_drunk_roll`, the latter two typed `Int`) **[2026-09-30, desk review: the quoted real values were removed (clean-room); names and types kept]**. Neither is in §12.2's group list; their destinations were not decompiled here (no address anchor was already in hand for either, unlike `Vehicle_Aims`) — recorded as an **OPEN** addition to the table's vocabulary, not a decoded one.

§16 open item 9 (`Vehicle_Aim` flags + `Vehicle_Aims` record base) is closed; the per-mode meaning of the 61 (really 69 real, §17.1) submode entries remains open, as does the destination of `melee_lock_group`/`sway_group`.

### 17.6 `materials.xtbl` — a real entry with no reader **[CONFIRMED — empirical]**

`materials.xtbl` **does exist** as an ordinary top-level entry of `misc_tables.vpp_pc` (25 `<Material>` rows, each only `<Name>` plus `<_Editor><Category>Entries</Category></_Editor>`, followed by a `<TableTemplates/>` stub, a `<TableDescription>` describing exactly one `String` field (`Name`), and an `<EntryCategories>` block with one category, `Entries`). Its 25 names are an **exact subset** of `bitmap_materials.xtbl`'s 33-name slot list (§11.6) — missing precisely the 8 later-added names `foliage`, `corrugated brick`, `corrugated metal`, `cyberspace`, `vibration`, `concrete - reflectable`, `not set`, `wrestling mat`. This is a real, empirical confirmation of §1.1's disassembly-only conclusion (no exact-literal reference exists anywhere in the executable, so no loader was ever found for it): the table is not fabricated or absent from the data, it is simply an **orphaned/superseded editor table** — `bitmap_materials.xtbl` grew a longer material-name list without `materials.xtbl` (whatever once consumed it, if anything ever did) being kept in step, and nothing in the executable was ever wired to read it. §1.1's "skipped" verdict for *decoding* stands; this section only adds that the table is real, small, and content-wise inert.

### 17.7 `vfx.xtbl` — a real authoring bug: flattened `LOD` blocks silently drop values **[CONFIRMED — disassembly + CONFIRMED — empirical]**

Of the 128 real base `vfx.xtbl` rows carrying an `<LOD>` block, **17 (13.3%)** author at least one of `View`, `Minimum_time`, `Fading_start`, `Fading_end`, `Restore` as a **direct child of `<LOD>`**, omitting the wrapper element (`<Spawning>`, `<Update>`, or `<Distance>`) that §10.2's reader actually looks under (`LOD/Spawning/View`, `LOD/Update/Minimum_time`, `LOD/Distance/{Fading_start,Fading_end,Restore}`). Real examples include rows `Env_StreetLamp_green`, `Env_Stoplight_GreenGlare` and `Env_Stoplight_RedGlare`, whose `<LOD>` holds `View`, `Fading_start`, `Fading_end`, `Restore` and `Minimum_time` directly, with no `Spawning`/`Distance`/`Update` wrapper **[2026-09-30, desk review: a verbatim XML excerpt was replaced by this description (clean-room)]**.

Because `0x00DAB9E0(lod, "Distance")` (and `"Spawning"`, `"Update"`) finds no such child here, the reader's nested lookups are never attempted at all, and the record falls back to the documented absent-block defaults — `Fading_start`/`Fading_end` → **`1.0e10`** (not the authored `100`/`200`), `Restore` → untouched (its always-write hazard applies, §1.4). **In 5 of the 17 rows this drops an authored, materially different fade distance (100–200 units) in favour of the "never fades" default of `1.0e10`** — a real, shipped, silently-inert piece of tuning data, invisible to the small DLC sample (§14 lists `LOD/Spawning`/`LOD/Distance`/`LOD/Update` as "not exercised" there). The other 12 of the 17 rows only misplace `View` and/or `Minimum_time` (their `Distance` block, when present, is correctly nested), which given the zero-initialised record and a typically-authored `Minimum_time=0.0` is usually — but not necessarily — inconsequential.

### 17.8 Other confirmed values (structural and content) from the real base tables **[CONFIRMED — empirical]**

- **`weather.xtbl`**'s 5 real stages and their transition graph: `Clear Skies` (→`Overcast`), `Light Rain` (→`Heavy Rain`/`Post-Storm`), `Overcast` (→`Light Rain`/`Clear Skies`/`Heavy Rain`), `Heavy Rain` (→`Post-Storm`), `Post-Storm` (→`Clear Skies`); the five `Chance` weights sum to 138 **[2026-09-30, desk review: the per-stage weights were removed (clean-room); the sum is kept as the check]**, matching §2's "weight normalised over all stages" rule.
- **`wind.xtbl`**'s 2 real stages (`Calm`, `Light Wind`) are near-duplicates: identical `Average_Duration`, `Variance` and `Wind_Settings/Average_Intensity`; only `Chance` (equal weights) and the display name differ **[2026-09-30, desk review: the copied values were removed (clean-room)]**.
- **`dof_situations.xtbl`**: the base table has no row named `Satellite`, yet `0x00B6FB90` (decompiled: hashes the literal `"Satellite"` once, lazily, then calls the confirmed DOF selector `0x0058EF60`) unconditionally tries to select it — in the un-patched base game this call resolves to "no situation found" and is a no-op (the DOF selection is silently cleared/left absent), consistent with `Satellite` being added by downstream content (a DLC or later patch) that was not sampled here. `Fine Aim`/`Zoom` and their `" Advanced"` pairs (§7.1) are exactly the 4 real rows.
- **`radial_blur.xtbl`**'s 2 base-only names never seen in the DLC sample: `very large` and `EDF_Artillery_Gun` (the latter has the largest `Distance_fade` of the six) **[2026-09-30, desk review: the copied row values were removed (clean-room)]**.
- **`bitmap_materials.xtbl`**: the 32 real rows cover **every** one of the 33 slot names except the internal `not set` fallback (§11.6) — i.e. every addressable slot has real authored data in the base game. `Audio_Occlusion` real values are small integers as text (`'1'`…`'50'`), confirming the numeric reading over the `<TableDescription>`'s own (misleading) `String` type tag; 2 of 32 rows (`Metal - Fence`, `Vibration`) omit it, confirming the write-if-present default.
- **`fade_categories.xtbl`**'s `Default` row (`medium_lod_distance=60`, `low_lod_distance=200`, `Distance=500.0`) matches the code's hard-coded pre-load defaults (`60.0`/`200.0`/`500.0`, the constants at `0x01173DE4`/`0x012A30D4`/`0x01118BC4`) **exactly** — an author keeping the data in sync with the code, not the code deriving from the data.
- **Cross-table consistency, confirmed with real names on both sides:** `lightning.xtbl`'s 3 `VFX_List/VFX` values (`skybox_lightning_strike`, `skybox_lightning_cld01`, `skybox_lightning_cld02`) are exactly 3 of the 8 real `skybox_effects.xtbl` `Name` values, and all 3 have `Auto_Spawn=False` (consistent with being lightning-triggered rather than timer-spawned) while the other 5 real skybox effects have `Auto_Spawn=True`. `bitmap_materials.xtbl`'s real decal-name references (`blast_decal`, `big_impact`, `decal_bullet_hole`, `decal_bullet_metal`, `decal_bullet_cem`, `decal_bullet_glass`, `decal_bullet_wood`, `decal_bullet_plastic`) are all real `decal_info.xtbl` `Name` values.

### 17.9 What remains open after this pass

- `camera_free.xtbl`'s `melee_lock_group`/`sway_group` destinations (real elements, real values, no decompiled anchor yet — §17.5); the per-mode meaning of the 69 real `submode` entries.
- `weather`/`wind`/`weather_time_of_day` run-time consumers (stage-transition logic, presence-bit blending) — unchanged from §16 item 1; this pass only adds real authored content to the tables, not a trace of who reads the resulting arrays at run time.
- The exact byte roles of `bitmap_sheets.xtbl`'s companion-file record's `+0x08` and `+0x14`/`+0x18` dwords (§17.4) beyond "the caller's argument 1" and "`ImageWidth`/`ImageHeight`" were read from one decompile pass, not independently re-derived.
- Everything already listed in §16 items 1–6, 8, 10 (the last now substantially addressed by this section, but run-time consumption of the values is still untraced) stands as before.


**Review status (2026-09-30): DESK-PASS, text fixes applied (copied real values and two verbatim excerpts replaced by descriptions; OPEN notes on the effects copies, the 12-of-16 instance count and the §17.4 record layout); NEEDS-DATA: hash of both base `effects.xtbl` copies; NEEDS-EXE: `0x00E21F70` record layout. Row counts agree with Team B `team-b/HANDOFF.md` section "9.80 `sr3tables_environment`" where both report (weather 5, skybox 8, lens_flares 6, motion_blur 1, color_entry 145, camera_shake 70, decal_info 14, effects 1,272 = 606 + 606 + 60, vfx 589 = 529 + 60, refraction 27 = 26 + 1) — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): fixed 9 cross-references (§17.10→§17.8 ×6, §17.7→§17.6 ×2, §17.8→§17.7; dropped a dangling §17.9) and 1 external ref (`spec-xtbl-format.md` §8→§2–§3); marked resolved: §11.7 heading (§17.4), §12.2 flag map and `Vehicle_Aims` base (§17.5), §16 items 2 and 10; corrected the camera_free/decal "same convention" slip (decal takes the cosine, §17.3, §14.2); reworded 2 `param_1` tokens.
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline; repointed 2 `HANDOFF.md` §27.x references to the archived headings (§27.2/§27.1 → §30; header and §16); 0 left (see review).
- 2026-09-30 (cloud, manager ruling): shortened a `PersistFilename` example in §17 to its file name (`…\ui_cell_frame_left.tga`); the developer's local machine path prefix is omitted. Rule: no developer machine paths or usernames in specs.
- 2026-09-30 (cloud): §1.3: the loader's failure message is paraphrased instead of quoted verbatim (manager clean-room line).
- 2026-09-30 (cloud, adversarial desk review `review/adv_tables-environment.md`): review status summary + 47 per-unit status lines (23 DESK-PASS, 18 with text fixes, 6 NEEDS-EXE, 0 VALIDATED-BY-DATA); added the unread real `Name` element to §6.1/§6.2/§11.5 and noted ten real `camera_free` groups (§12.2), citing Team B HANDOFF 9.80; accessor signedness per `spec-tables-traffic-ai.md` §1.3 (§1.4); `Ground_Reflection` spelling and two schema-absent fields annotated in §3.3; arithmetic shown (§3.2 12 unassigned presence bits, §6.3, §12.2, §15.2 range end); OPEN markers for the exe/data questions (HHMM truncation, cell-array size, gaps at `+0x40` in radial_blur/effects and `+0x3C` in refraction, LOD-absent defaults, duplicate effect names, DOF camera-position conflict with the cutscene spec, `0x00DA7890`, submode 69-vs-61, effects copies); clean-room: removed copied tuning values and two verbatim XML excerpts (§15.4, §17.2, §17.4, §17.5, §17.7, §17.8). No label raised; no section renumbered.
- 2026-10-01 (cloud, re-derivation from the executable, job `20261001T123123-team-a-ytgi`; `review/rederive_tables-environment.md`): 18 new per-unit status lines (4 cleared for implementation: §2, §6.3, §8, §10.2); CORRECTIONS: float sign in `0x00DACB20` (§1.4), file-name rule has no base-name test (§1.5), `Start_Time` 2400 → 1.0 and cell array = weather capacity × `0xE48` (§3.1), live wind intensity and `0x005AA310` arguments (§4.1), `time_of_day_objects` defaults read from TOD-definition data (§9.2), `vfx` `LOD`-absent fields stay 0 (§10.2), pointer-table end `0x01311084` (§15.2); CONFIRMED: `0x00DACF60`/`0x00DACF20`, accessor signedness of five fields, 12 unassigned presence bits, padding at radial_blur/effects `+0x40` and refraction `+0x3C`, `Zoom`/`RC Gun` DOF callers, DOF `T ≤ 0` and snap rule, `0x00DA7890` hash, submode `Aspect` filter/last-wins/degree→radian; the §7.3 camera-global conflict with the cutscene spec resolved (adjacent vectors written together); wind transition mechanism and effects duplicate-name policy added at HIGH CONFIDENCE; FOR TEAM B notes in §1.4, §3.1, §9.2, §10.2, §12.2. Superseded text struck, nothing deleted, no renumbering.
