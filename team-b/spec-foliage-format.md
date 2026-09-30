# Saints Row: The Third — `.cfmesh_pc` Foliage Mesh Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, target selected from `spec-format-inventory.md` §6 (type 37, `Foliage Mesh`, registered with **no** `g`-side extension). Ties to the `.fmeshx` foliage source references already noted in `spec-terrain-format.md` §2 — the same authoring-name/shipped-name split seen for `.animx` → `.anim_pc`.
**Scope:** The `.cfmesh_pc` file: the shared material-reference block, a new outer geometry block (`0x0FF1C1A1`), the embedded — and here **inline** — "Mesh" sub-block, a per-material table, inline **material definitions** (textures, shader constants), a texture-name table, and a small LOD/fade table.
**Method:** Registration-table-and-magic-scan first, then the registered constructor and every callee down to the material binder, replayed against **all 19 shipped `.cfmesh_pc` files** (the entire population — all in `sr3_city_0.vpp_pc`'s `awld_compact` bundle) with an exact-size check that ends precisely where the file's own offset field says it should.
**Cleanroom compliance:** ~~No decompiled code or original identifiers appear below.~~ *(Corrected, desk review 2026-09-30: no decompiled code appears below; the body does cite about thirty auto-named function addresses (`FUN_xxxxxxxx`) and raw data addresses, as evidence only, and a few short shipped asset/texture names as functional identifiers.)* Magic numbers, offsets, strides and alignments are load-bearing format data. Texture names quoted (`fol_bushi01.tga`, `flat-normalmap_n.tga`) and the three literal special sampler names are shipped data. Function addresses are cited as evidence only.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30)** — adversarial desk review (`review/adv_foliage.md`), 18 units: DESK-PASS 3 (§8, §9, §11.4); DESK-PASS, text fixes applied 8 (§1, §2, §3, §4, §6, §6.1, §10, §12 — §12's notes folded into the bodies as superseded markers); NEEDS-EXE 3 (§5, §6.2, §11.1); VALIDATED-BY-DATA 4 (§6.3, §7, §11.2 layout, §11.3 on-disk identity — the last two also NEEDS-EXE for runtime semantics). A desk pass alone does not clear a unit: it only means the text is internally consistent; VALIDATED-BY-DATA units are already backed by Team B's full-population run (`sr3foliage`, 19/19, `team-b/HANDOFF.md` §4.5, plus §12's second reader). Awaiting the executable: §5 (Mesh wrapper / inline path), §6 (record size semantics, array alignment base, header `+0x28..+0x2F`), §6.1 (location of flag bit `0x08`, `name_offset` base), §6.2 (B-hash → `vec4` mapping), §11.1 (registry, resident cap, quality scaling), §11.2 runtime fade semantics, §11.3 cache and which slot-array value the loader reads, §4 (on-disk role of `+0x0C`/`+0x10`/`+0x14`/`+0x24`). Awaiting data: §3 handle-table/slot-array alignment for odd material counts, §4 high halves `+0x0C`/`+0x1C`/`+0x2C`/`+0x3C`, §5 Mesh header census, §6.1 on-disk `resolved_index`/`flags`, §11.2 `drawGroupIndex` range check, §9 15/19 vs 6/19 reconciliation.

---

## 1. Headline results

- **A fast win by reuse, as predicted**: every file opens with the shared material-reference block (19/19), and the new outer block's `+0x08` embeds the same **"Mesh" sub-block** as `.ccmesh_pc` (version field reads `9` in 19/19; the entry function is a thin wrapper into the same parser). **[CONFIRMED — empirical + disassembly.]**
- **First observed inline-mode Mesh block.** Foliage has no `g`-file, and the Mesh block's flags bit 0 is clear in **19/19** — its vertex/channel data lives inside `.cfmesh_pc` (the path `spec-geometry-format.md` §4.1.2 item 6 describes but never saw on a real file). **[CONFIRMED — empirical.]**
- **A full loader replay reproduces the structure exactly**: after a 16-byte alignment that the first attempt missed, the chain of material sub-records ends **exactly at the file's `+0x28` target in 19/19**. **[CONFIRMED — empirical.]**
- **Materials are embedded inline**, not referenced: each sub-record is a material definition — a shader hash, texture bindings by name, shader-constant name hashes, and `vec4` constant values — bound at load by a routine that also handles video-texture channels. ~~Only **two** distinct materials exist across the population.~~ *(Corrected, desk review 2026-09-30: there are **two distinct shader hashes** (two material shapes, §6) across 33 records carrying 13 distinct variant hashes — not two materials.)* **[CONFIRMED — disassembly + empirical.]**
- **The engine-wide string hash is now confirmed by disassembly** (`FUN_00da7890`, called by this constructor): fold `A–Z` to lowercase, then `hash = rotl32(hash, 6) XOR byte` — exactly the algorithm `spec-vpp-container.md` §2.2 inferred for filenames and `spec-rig-format.md` §5 verified empirically on 22,274 bone names. *(Seed `0`, one step per byte up to but excluding the terminator, fold applied before the rotate — `spec-vpp-container.md` §2.2.)* **[CONFIRMED — disassembly.]**
- *Note (desk review 2026-09-30): the "flags bit 0 clear 19/19" figure is Team A's only; Team B's reader does not parse Mesh internals (`team-b/HANDOFF.md` §4.5). Version 9 in 19/19 and the chain oracle 19/19 are Team B-validated.*

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## 2. Population facts

| | |
|---|---|
| Files | **19**, all in `sr3_city_0.vpp_pc/awld_compact.str2_pc` — ferns, grasses, weeds, and litter props (`fol_fern_a01`…`litter_cans_a01`) |
| `.gfmesh_pc` | **0** — consistent with the registration declaring no secondary extension |
| Sizes | 1,564 … 24,564 bytes |
| Material block textures | 2–12 names, all `.cvbm_pc`/`.gvbm_pc` pairs (the PEG redirect format, `spec-geometry-format.md` §2) |
| Materials per file | 1–4; **33 records total, exactly two material shapes** (§6) |
| Runtime registry | the constructor registers each mesh by **name hash** into a 64-slot global table (19 used) — ~~a hard cap of 64 foliage meshes per build **[CONFIRMED — disassembly]**~~ *(corrected, desk review 2026-09-30, per §11.1: at most 64 meshes **resident at once**, not per build)* **[CONFIRMED — disassembly for the 64-slot table; HIGH CONFIDENCE for the resident-list refusal beyond 64, §11.1]** |

Figures (19 files, 0 g-files, 1,564…24,564 bytes, 33 records, two shapes, 1–4 materials per file) match Team B's full-population run (`team-b/HANDOFF.md` §4.5).

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## 3. File layout

| Region | Position | Content | Confidence |
|---|---|---|---|
| Material-reference block | `+0x00` | magic `0x00043854`, name-table length, texture count, names (`spec-texture-format.md` appendix; field offsets and the block end `0x20 + u32(file+0x04)` in `spec-geometry-format.md` §3.1 / §4.1.3). **Required** here — its validator's failure aborts the load (unlike vehicles, where it is optional). | **[CONFIRMED — disassembly + 19/19.]** |
| *(mandatory pad)* | to the next 16-byte boundary, **always ≥ 1 byte** (`spec-geometry-format.md` §3.1.1) | | **[CONFIRMED — 19/19.]** |
| **Outer block** `B` | padded end | §4 | **[CONFIRMED.]** |
| Mesh sub-block | `B + (+0x08)` | the shared "Mesh" structure, **inline** data, total length given by its own length field (§12 note 2: Team B takes the end as the Mesh offset plus the `u32` at Mesh `+0x08`; not re-derived by Team A) | **[CONFIRMED.]** |
| Material-handle table | 8-aligned after the Mesh block (§12 note 2: starts exactly at `align8` of the Mesh end, 19/19 per Team B) **[OPEN — desk review 2026-09-30: whether the 8-alignment is taken relative to the file start or to `B`; to be settled against the executable (`FUN_00747b50`) / real data.]** | `0x10` header (`u32 ptr-slot, 0, count, ?`) + `count × u32`; `count` equals the outer block's `+0x20` in 19/19. *(Identified in the tree pass as the generic **index list** utility `FUN_00e401d0`, ten callers engine-wide — `spec-tree-format.md` §7.)* | **[CONFIRMED — disassembly + empirical.]** |
| Runtime slot array | `B + (+0x18)`, `count × 8` | begins exactly where the handle table ends (19/19); ~~zero-filled slots the loader populates~~ *(superseded — see §12 note 1: non-zero on disk in 19/19, each slot `{u32 B-relative offset of material record i, u32 0}`, Team B's measurement; the loader overwrites the slots with material objects, §11.3)* **[OPEN — desk review 2026-09-30: the handle table is `0x10 + 4 × count` bytes from an 8-aligned start, so for odd counts (1 and 3 occur) it ends 4 mod 8; whether an extra alignment precedes the slot array is not stated; to be settled against the executable / real data (slot-array start vs handle-table end for files with count 1 or 3).]** | **[CONFIRMED.]** |
| *(pad)* | **to 16-byte alignment** | the step the first replay missed: with 8-byte alignment the chain held in 6/19, with 16-byte in **19/19** | **[CONFIRMED — empirical.]** |
| **Material sub-records** | `count` of them, length-prefixed (§6) | end **exactly** at `B + (+0x28)` in 19/19 | **[CONFIRMED.]** |
| Texture-name table | `B + (+0x28)` | null-terminated `.tga` names (§6) | **[CONFIRMED.]** |
| LOD/fade table | `B + (+0x38)` to EOF (i.e. `B + i32(B+0x38)`, §11.2 — **not** `B+0x38` itself) | 1–3 records of `0x18` bytes (§7, §11.2): four fade distances, a draw-group index, a billboard flag | **[CONFIRMED present]** ~~**[HYPOTHESIS — meaning.]**~~ **meaning CONFIRMED 2026-09-20 (§11.2)** |

*Notation (added desk review 2026-09-30):* `B + (+N)` means `B` plus the signed 32-bit value **stored at** `B+N`, not the address `B+N`; §11.2 and §12 establish this for `+0x38` (19/19, reading `B+0x38` itself 0/19). **[OPEN — desk review 2026-09-30: that `+0x08`, `+0x18` and `+0x28` use the same B-relative base is implied by §11.2's shared idiom and by Team B's 19/19 chain, but not separately stated; to be settled against the executable (`FUN_00747b50`).]** The chain as a whole is VALIDATED 19/19 by Team B's reader, which enforces the `+0x28` oracle (`team-b/HANDOFF.md` §4.5).

**Review status (2026-09-30): DESK-PASS, text fixes applied (slot-array content superseded by §12 note 1; odd-count alignment OPEN) — desk review (not re-derived from the executable).**

## 4. Outer block (`0x0FF1C1A1`)

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | Magic **`0x0FF1C1A1`** | **[CONFIRMED — loader requires it; 19/19.]** |
| `+0x04` | Version, must be **`5`** | **[CONFIRMED — loader requires it; 19/19.]** |
| `+0x08` | Offset (block-relative, `-1` = none) → the embedded **Mesh sub-block**, handed to the shared parser | **[CONFIRMED — disassembly + 19/19 version-9 reads.]** |
| `+0x0C` | Runtime pointer slot | **[CONFIRMED — disassembly.]** |
| `+0x10` / `+0x14` | ~~Runtime: an allocated per-mesh object~~ *(superseded as an on-disk description — see §12 note 2: on disk `+0x10` is non-zero 19/19 and equals the Mesh block end (Team B's relation), `+0x14` is 0 in 19/19; at load `+0x10` is overwritten with the handle-set object, §11.3)* | **[CONFIRMED — disassembly (runtime use).]** **[OPEN — desk review 2026-09-30: whether the loader reads the on-disk `+0x10` value at all; to be settled against the executable (`FUN_00747b50`).]** |
| `+0x18` / `+0x1C` | Offset → the runtime slot array (`count × 8`) | **[CONFIRMED.]** |
| `+0x20` | **Material count** (1–4) | **[CONFIRMED.]** |
| `+0x24` *(added desk review 2026-09-30 from §12 note 3)* | `u32`, 19 distinct non-zero heap-pointer-like values in 19 files (`0x001E9CB8` … `0x045AFF90`) | **[CONFIRMED present — empirical, §12 note 3; OPEN — meaning.]** |
| `+0x28` / `+0x2C` | Offset → the texture-name string table | **[CONFIRMED.]** |
| `+0x30` *(new 2026-09-20)* | **Record count** of the LOD/fade table (1–3); `+0x34` is a further `u32`, values 40…292, no reader found (§11.2) | **[CONFIRMED — disassembly (the instancer stores it) + empirical (tail = count × 24, 19/19)]** |
| `+0x38` / `+0x3C` | Offset → the LOD/fade table, fixed up by the validator | **[CONFIRMED — disassembly.]** |
| `+0x40`.. | the material sub-record stream follows the runtime structures (§3) | |

**[OPEN — desk review 2026-09-30: for the pairs `+0x08/+0x0C`, `+0x18/+0x1C`, `+0x28/+0x2C`, `+0x38/+0x3C` the low half is the offset (established for `+0x38` by §11.2); the on-disk contents of the high halves `+0x0C`, `+0x1C`, `+0x2C`, `+0x3C` are not reported; to be settled against real data.]**

The `+0x08`, `+0x18`, `+0x28`, `+0x38` offsets are ~~file-relative~~ **B-relative signed `i32`** *(corrected desk review 2026-09-30: §11.2 states the offset is relative to the outer-block base `B` and the table starts at `B + i32(B+0x38)`, 19/19 on two readers, §12; the other three are covered by the OPEN notation note in §3)* on disk and fixed up to absolute pointers at load, with `-1` meaning absent — the idiom shared by every format in this project.

Validated by Team B (19/19): magic, version, `+0x08` (version-9 read), `+0x20`, `+0x28`, `+0x30`/`+0x38` (`team-b/HANDOFF.md` §4.5; §12).

**Review status (2026-09-30): DESK-PASS, text fixes applied (`+0x10`/`+0x14` superseded by §12 note 2, `+0x24` row added from §12 note 3, offset base corrected to B-relative; high halves OPEN) — desk review (not re-derived from the executable).**

## 5. The embedded Mesh sub-block

Identical to `spec-geometry-format.md` §4.1.1–§4.1.2 (version `9`, channel array, count×stride primary buffer), reached through a one-line wrapper into the same parser. Two population facts specific to foliage: flags bit 0 is **clear in 19/19**, so the vertex and channel data are read **inline from this file** rather than from a `g`-buffer; and the primary `count × stride` buffer (stride **2** in 19/19, counts 30…858 — the same two-byte-element buffer `.ccmesh_pc` shows — i.e. the `u16` **index** buffer, per `spec-geometry-format.md` §7 / `spec-vertex-format.md`, not vertex data) is only 5–8% of the block's declared length — the remainder is the inline channel data that the g-file would otherwise hold. **[CONFIRMED — empirical.]** This is the first real-file observation of the inline path.

**[OPEN — desk review 2026-09-30: Team B's reader locates and version-checks the Mesh sub-block but does not parse its internals (`team-b/HANDOFF.md` §4.5), so bit 0 clear, stride 2, counts 30…858 and the 5–8% ratio are single-source; the wrapper's address is not cited; to be settled against the executable (the wrapper and the `FUN_007527b0`-family parser) / real data (a Mesh-header census of the 19 files).]**

**Review status (2026-09-30): NEEDS-EXE: wrapper not cited, inline-mode figures single-source — desk review (not re-derived from the executable).**

## 6. Material sub-records — inline material definitions

Each record is length-prefixed: `u32 size`, then 8-align, a `0x30` header, then three arrays; the bytes consumed must equal `size` (the loader checks this; the replay reproduces it 33/33). **[OPEN — desk review 2026-09-30: whether "consumed" is counted from the size prefix, from after it, or from after the 8-align pad, and whether the 16-alignment of the `vec4` array is relative to the record start, to `B` or to the file start, are not stated; to be settled against the executable (`FUN_00e71090`).]** *(Team B's reader reproduces 33/33, `team-b/HANDOFF.md` §4.5.)*

**Header (`0x30`)**

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | **Material/shader hash** — resolved at load to a shader object. Exactly **2 distinct values** across all 33 records: one on every plant material, one on every litter material. | **[CONFIRMED — disassembly for the resolution; empirical for the population.]** |
| `+0x04` | A second hash, 13 distinct values — per-material variant | **[CONFIRMED present; OPEN — meaning.]** |
| `+0x08` | Flags: `0xC2` on plant materials, `0x40` on litter. Bit `0x08` selects a fallback texture path (§6.1). *(Pairing, Team B: `0xC2` ×10 are the `(1, 1, 3)` records, `0x40` ×23 the `(3, 9, 11)` records, `team-b/HANDOFF.md` §4.5.)* **[OPEN — desk review 2026-09-30: both shipped values have bit `0x08` clear, and §6.1 places bit `0x08` on the binding `flags`; which field carries it is unsettled; to be settled against the executable (`FUN_00e71090`).]** | **[CONFIRMED — disassembly for bit `0x08`; OPEN — the rest.]** |
| `+0x0C` | `u16 A` texture-binding count; `+0x0E` `u8 B` constant count; `+0x0F` `u8 C` `vec4` count. Only two shapes ship: **`(3, 9, 11)`** ×23 and **`(1, 1, 3)`** ×10. | **[CONFIRMED.]** |
| `+0x10` `+0x18` `+0x20` | Runtime pointers to the three arrays, zero on disk | **[CONFIRMED — disassembly.]** |
| `+0x28`…`+0x2F` | not described **[OPEN — desk review 2026-09-30: these 8 header bytes are untiled, and whether the pointer slots at `+0x10`/`+0x18`/`+0x20` are 4 or 8 bytes wide is not stated; to be settled against the executable / real data.]** | |

**Arrays** (in order, with the alignments the loader applies): `A × 12`-byte texture bindings (4-aligned), `B × 4`-byte constant-name hashes (4-aligned), `C × 16`-byte `vec4` constant values (16-aligned).

Shapes, flags, distinct hash counts (2 / 13) and 33 records are VALIDATED by Team B (`team-b/HANDOFF.md` §4.5).

**Review status (2026-09-30): DESK-PASS, text fixes applied (shape/flags pairing cited; size semantics, alignment base and `+0x28..+0x2F` OPEN) — desk review (not re-derived from the executable).**

### 6.1 Texture bindings (12 bytes each)

`{ u32 name_offset, u32 slot_hash, u16 resolved_index, u16 flags }`. `name_offset` indexes the `+0x28` string table **[OPEN — desk review 2026-09-30: whether it is a byte offset or an index, and relative to which base (the table start at `B + (+0x28)` or another), is not stated; to be settled against the executable (`FUN_00e71090`) / real data.]**; at load the name is normalized and resolved to a texture handle, with three literal special names — `*video_y`, `*video_cr`, `*video_cb` — mapping to video-playback channels, and flags bit `0x08` substituting the literal fallback `misc-static.tga`. `slot_hash` is passed to the shader object's slot lookup, so it names the **sampler slot**, not the file: it is **not** the engine hash of the texture filename (checked against the name, its stem and its lowercase form: 0/79 each), and the population agrees — only **3 distinct** `slot_hash` values occur across 79 bindings that reference **16 distinct** names, i.e. one value per sampler role (diffuse / specular / normal), not one per file. Bindings the shader rejects are compacted out of the array. **[CONFIRMED — disassembly; the 0/79 negative and the 3-vs-16 count empirical.]** **Cross-format confirmation (2026-09-10, `spec-tree-format.md` §6.1):** the same three values — `0x69B48F91`, `0x2808EB90`, `0xE848C9CA` — bind diffuse, normal and specular maps respectively in `.csrt_pc` trees too (185 bindings, disjoint texture set, 0/185 against the engine hash). Same 32-bit value, same role, two formats: these are **sampler-slot identifiers**. **[CONFIRMED — empirical, cross-format.]**

**[OPEN — desk review 2026-09-30: the on-disk values of `resolved_index` and `flags` over the 79 bindings are not reported, and whether bit `0x08` is in this `flags` or the record header's (§6) is unsettled; to be settled against the executable (`FUN_00e71090`) / real data.]** 79 bindings, 3 slot hashes and 16 names are VALIDATED by Team B (`team-b/HANDOFF.md` §4.5).

**Review status (2026-09-30): DESK-PASS, text fixes applied (`name_offset` base and bit-`0x08` location OPEN) — desk review (not re-derived from the executable).**

### 6.2 Shader constants

The `B` hashes are looked up on the shader object for register slot and size, and the corresponding `vec4` values are staged into a global constant buffer. Observed values are defaults such as `(1, 1, 1, 1)` (a tint) and zeros. **[CONFIRMED — disassembly for the mechanism; values observed.]** **[OPEN — desk review 2026-09-30: the mapping from the `B` hashes to the `C` `vec4`s is not stated — `B ≠ C` in both shapes (9 vs 11, 1 vs 3), so it is not one-to-one, and whether hash *i* covers several `vec4`s by its looked-up size is unsettled; to be settled against the executable (the constant-staging path under `FUN_00e71090`).]**

**Review status (2026-09-30): NEEDS-EXE: B-hash → vec4 mapping — desk review (not re-derived from the executable).**

### 6.3 The name table

Sixteen distinct `.tga` names across the population (79 bindings, every one resolving to a `.tga` name in this table): four plant diffuse maps (e.g. `fol_bushg01.tga`), the placeholder `flat-normalmap_n.tga`, and eleven litter maps (e.g. `GarbageCan_01_D/_S/_n.tga`) *(list reduced to representative names, desk review 2026-09-30, clean-room; the full list is recoverable from the files and is held by Team B's validation harness)* — diffuse (`_D`), specular (`_S`) and normal (`_n`) maps, following the suffix convention seen in terrain and elsewhere. They are the `.tga` sources of the `.cvbm_pc`/`.gvbm_pc` pairs the material block lists. Plant materials bind one texture (diffuse only); litter materials bind three (diffuse, specular, normal). *(Clarified desk review 2026-09-30: since plants bind only a diffuse map, `flat-normalmap_n.tga` is bound by **litter** materials in the normal slot — ×21 per `spec-tree-format.md` §6.1 — not by plants.)* **[CONFIRMED — empirical, whole population.]**

VALIDATED by Team B: 3 slot hashes / 16 names, "names match §6.3's list exactly" (`team-b/HANDOFF.md` §4.5).

**Review status (2026-09-30): VALIDATED-BY-DATA: 79 bindings / 16 names, Team B 19/19; name list reduced for clean-room — desk review (not re-derived from the executable).**

## 7. The `+0x38` table

One to three records of six floats each (24 / 48 / 72 bytes to EOF). Per slot across files: `[0]` is `0` or `−0.437`, `[1]` `0`, `[2]`/`[3]` in `5…25` *(numeric ranges in this paragraph superseded by §11.2 and §12 note 4, desk review 2026-09-30)*, ~~`[4]`/`[5]` `0`~~ *(wrong — §11.2: `[4]` is a `u32` draw-group index taking 0/1/2, `[5]` a `u32` billboard flag; not all zero)*; the second record's `[2]`/`[3]` in `12…25` and `15…25`; the third's up to `260`/`270`. ~~Ascending distance-like bands per record, consistent with **LOD or fade distance ranges** — **[HYPOTHESIS]**.~~ **Resolved 2026-09-20 (§11.2): each record is `{fadeIn start, fadeIn end, fadeOut start, fadeOut end, drawGroup, billboard}`; successive LODs cross-fade over the same interval in 15 of 18 pairs. [CONFIRMED — disassembly + empirical.]** This is the field the outer-block validator fixes up. *(Position: `B + i32(B+0x38)`, not `B+0x38` — §11.2.)*

**Review status (2026-09-30): VALIDATED-BY-DATA: tail = count × 24 in 19/19, 37 records, two readers (§12) — desk review (not re-derived from the executable).**

## 8. Cross-format notes

- `.fmeshx` (`spec-terrain-format.md` §2) is the authoring name; `.cfmesh_pc` is the shipped name — the `.animx`/`.anim_pc` pattern again. **[HIGH CONFIDENCE — inferred.]**
- The shared hash (`FUN_00da7890`) is now **disassembly-confirmed**, including the lowercase fold, upgrading `spec-vpp-container.md` §2.2 and `spec-rig-format.md` §5.
- Materials embedded inline contrast with `.ccmesh_pc`, which only lists texture names and leaves shader binding elsewhere. Foliage carries its own shader parameters — likely because its instancing/LOD path is a separate renderer.
- The inline material definition is not foliage-specific: `.csrt_pc` trees carry the same records inside a shared **material-set block** (`spec-tree-format.md` §6), and the record parser has ten call sites in eight functions engine-wide (`tools/tree_callers.txt`). **[OPEN — desk review 2026-09-30: "ten call sites" here (record parser `FUN_00e71090`) and "ten callers" of the index-list utility `FUN_00e401d0` (§3) are separate counts; the first was not re-listed; to be settled against the executable.]**

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 9. Methodology note

The replay failed 15/19 on the first pass with a zero size prefix — the signature of reading a runtime slot array as data. The fix was a single alignment step (16, not 8) after that array, discriminated by which choice makes the chain end exactly at the file's own `+0x28` offset. **When an exact-size chain fails, the file's other offset fields are the oracle.** **[OPEN — desk review 2026-09-30: "15/19 failed" here and "with 8-byte alignment the chain held in 6/19" (§3) imply 15 vs 13 failures; the text does not say whether they count the same replay stage; to be settled against real data (a re-run of the 8-aligned replay).]**

**Review status (2026-09-30): DESK-PASS (15/19 vs 6/19 OPEN) — desk review (not re-derived from the executable).**

## 10. Open Items

1. ~~The `+0x38` table's meaning (§7).~~ **RESOLVED 2026-09-20 (§11.2): per-LOD fade-distance / draw-group / billboard records, count at outer `+0x30`; consumer chain `FUN_00852050` → `FUN_00e600e0`/`FUN_00e5fd00`/`FUN_00485dc0`; exact tail 19/19, controls 0/19.**
2. Header `+0x04`'s second hash and the flag bits other than `0x08`.
3. ~~What the material-handle table's `u32`s resolve to (a hash-keyed lookup at load; the resolved object not characterized).~~ **RESOLVED 2026-09-20 (§11.3): the loader overwrites each `u32` with a handle from the deduplicating material cache (16-bit pool index into the `0x14`-byte-stride pool at `0x02ac693c`); the on-disk values are identity placeholders, 19/19.**
4. Whether the two material hashes correspond to named shader files elsewhere in the archives (a name→hash search over `.fxo_pc`/shader names would settle it).
5. The inline Mesh block's channel layout — the same open item as `spec-geometry-format.md` §4.1.2, now with 19 inline samples available to test against.
6. *(Added desk review 2026-09-30.)* Outer `+0x24` (§12 note 3) and `+0x34` (§11.2); the high halves `+0x0C`/`+0x1C`/`+0x2C`/`+0x3C` (§4); binding `resolved_index`/`flags` values and the location of bit `0x08` (§6.1); the `B`-hash → `vec4` mapping (§6.2); handle-table/slot-array alignment for odd counts (§3); record size semantics and header `+0x28..+0x2F` (§6).

**Review status (2026-09-30): DESK-PASS, text fixes applied (missing items added) — desk review (not re-derived from the executable).**

## 11. Consumer-side decode — the LOD table and the material handles (2026-09-20, a later pass)

**A later consumer-side pass, 2026-09-20.** Anchor: the 64-slot registry the constructor stashes into, and its readers. All labels **CONFIRMED — disassembly** unless stated.

### 11.1 The registry and its consumers

The registry is the object at **`0x012f74f8`** (`FUN_00747860` insert, `FUN_007477f0` lookup, `FUN_00747690` probe, `FUN_00747720` remove): a 6-bit-bucket hash table over the engine name hash — keys at `+0x88` (64 × `u32`), values at `+0x188` (= the array `0x012f7680` the ctor writes; 64 × `u32`, each the **absolute address of the file's `0x0FF1C1A1` outer block**), a free chain and 64 bucket heads. `FUN_00747970(name)` appends `.cfmesh_pc` to a name, hashes it and looks it up. It has **three** readers, all in the vegetation ("growth") system: `FUN_00851b10` (per-frame scroll of the layer grids; it also animates an 8-entry table of pseudo-random drift values scaled by the shared wind level `0x0130cbbc`, `spec-tree-format.md` §13.1), **`FUN_00852050` (the per-frame instancer — the consumer that matters here)** and `FUN_008529f0` (layer setup: each layer descriptor carries up to 16 mesh *names*, `0x10` bytes apart; each is resolved with `FUN_00747970` and the results kept per grid); `FUN_0084aa90` maintains a **resident-name list** (`0x0242dc90`, count `0x0242dc88`, refused beyond `0x40` = 64): it diffs the wanted names against it and calls `FUN_00862740(name, 1)` / `(name, 0)` to add / drop — the mechanism behind the 64-mesh cap **[HIGH CONFIDENCE for the add/drop reading]**. The instancer builds one **instanced-mesh render object** per patch (`FUN_00e60a30` initialises it from the outer block's `+0x08` runtime mesh object and `+0x10` handle-set object) and then **overrides two of its fields from the file**: object `+0x108` ← outer `+0x30`, object `+0x10C` ← outer `+0x38` (`0x00852301`/`0x00852307`). The draw routine `FUN_00485dc0` and its LOD-range helper `FUN_00e600e0` / fade helper `FUN_00e5fd00` read those two fields.

**[OPEN — desk review 2026-09-30: consumer semantics (bucket layout, the `0x40` resident refusal, and whether foliage distances are scaled by the quality multiplier `0x0130cbc0` as tree LODs are, `spec-tree-format.md` §13.5.2) are not re-checked; to be settled against the executable (`FUN_00747860`, `FUN_007477f0`, `FUN_00747970`, `FUN_0084aa90`, `FUN_00862740`, `FUN_00e600e0`).]**

**Review status (2026-09-30): NEEDS-EXE: registry and cap semantics; addresses arithmetically consistent — desk review (not re-derived from the executable).**

### 11.2 The `+0x38` table — RESOLVES §7 and §10 item 1

**Layout (per-LOD record, `0x18` bytes; count = outer `+0x30`):**

| Offset | Field | Meaning (from the consumers) |
|---|---|---|
| `+0x00` | `f32 fadeInStart` | distance where this LOD starts fading **in** |
| `+0x04` | `f32 fadeInEnd` | distance where it is fully in. The routine writes the constants `(1/(end − start), −start/(end − start))` (a degenerate range gets a `1e-4` epsilon) — i.e. a linear 0 → 1 ramp `d·scale + bias`; the ramp's use in the shader is HIGH CONFIDENCE, not read |
| `+0x08` | `f32 fadeOutStart` | distance where it starts fading **out**; `min`-ed with the layer's own value (object `+0x100`) — when the global flag `0x02bd43bc` is non-zero the layer's value is used **instead** of the record's |
| `+0x0C` | `f32 fadeOutEnd` | distance where it is gone (same `min`/flag rule against object `+0x104`); also the record's **far distance** used by the LOD range test |
| `+0x10` | `u32 drawGroupIndex` | which **draw group** (`0x30`-byte record of the Mesh header's group array, §5) this LOD draws; record 0's value is also the group whose bounds set the instance height |
| `+0x14` | `u32 billboardFlag` | non-zero → the item is drawn **camera-facing** (the routine builds a view-aligned orientation from the direction to the camera); zero → the item's own transform |

`FUN_00e600e0` picks, from the object's squared min/max distance to the camera, the range `[first, last]` of records whose `[fadeInStart, fadeOutEnd]` interval intersects it (the last LOD wins beyond its far distance); the draw loop then runs `FUN_00e5fd00(i)` (which turns `(fadeIn*)` and `(fadeOut*)` into the two `(scale, bias)` pairs written to object `+0x90…+0xA0`) and draws group `drawGroupIndex(i)`. The default record used by an instance object before the file's table is attached is `{0, 0, 5000, 5000, 0, 0}` (static data `0x0132dfec`): **one LOD, visible from 0 to 5000**.

**Correction of §7:** §7's "`[4]`/`[5]` `0`" was wrong — they are `u32`s and **not** all zero: `drawGroupIndex` takes the values 0/1/2 and `billboardFlag` 0/1 (two records, one each in `fol_weed_a01` and `oas_fol_grass_a`). `+0x30` of the outer block, absent from §4, is the **record count** (1–3). **[CONFIRMED — disassembly + empirical.]**

**Empirical validation (`tools/harnesses/foliage_lod_table.py`, all 19 files):** the tail from the table start to EOF is **exactly `count × 24` bytes with `count = outer+0x30`, 19/19** *(wording clarified 2026-09-20 by a later pass, after a first replay misread the original: **the u32 at `outer+0x38` is a signed OFFSET relative to the outer-block base `B`; the table starts at `B + i32(B+0x38)`, not at `B+0x38` itself** — independently re-run with that reading: 19/19 exact, stride controls 0/19, 15/18 cross-fade pairs)* (controls at strides 16/20/28/32: **0/19 each**); `fadeInStart ≤ fadeInEnd` and `fadeOutStart ≤ fadeOutEnd`, every record; `billboardFlag ∈ {0,1}`, 19/19; **consecutive LODs cross-fade over the same interval** (`fadeIn(i+1) == fadeOut(i)`, both ends) in **15 of 18** consecutive pairs — the three exceptions are `fol_weed_a01` (its billboard LOD fades in at 10–12, *before* the previous LOD starts fading out at 16–18) and both consecutive pairs of `oas_fol_grass_a` (which has the only negative fade-in start, −0.437, and a 260–270 billboard record). Distances are world units (likely metres) and span 5–270 (~~plants 8–32~~ *(loose — plant fade-out values go down to 5–6, §12 note 4)*; litter fixed at 15 / 25).

`outer +0x34`: not read by any consumer found (the instancer reads `+0x30` and `+0x38` only); values 40, 80, 94, 96, 148, 228, 238, 262, 280, 292. It is **not** the name-table length, texture-binding count, constant/`vec4` count or any of six simple combinations of them (0/19 each, `tools/harnesses/foliage_w34_probe.py`). **OPEN.**

**[OPEN — desk review 2026-09-30: §5 gives neither the offset nor the count of the Mesh header's group array, so `drawGroupIndex` (up to 2) is not checked against the group count; to be settled against real data.]**

**Review status (2026-09-30): VALIDATED-BY-DATA: layout, tail = count × 24 19/19 × two readers, controls 0/19, 37 records, 15/18 pairs; runtime fade/min/flag semantics NEEDS-EXE (`FUN_00e600e0`, `FUN_00e5fd00`, `FUN_00485dc0`) — desk review (not re-derived from the executable).**

### 11.3 What the material-handle table's `u32`s resolve to — RESOLVES §10 item 3

The block parser `FUN_00747b50` (§3 "handle table"; the tail that fills it was not followed in the original pass) does, for each of the outer block's `+0x20` materials: parse the inline material record (`FUN_00e71090`), allocate a **material object** (pooled, `FUN_00e5e240`), store it in the runtime slot array at outer `+0x18` (`count × 8`), set its `+0x14` to the parsed record, and pass it to the **material cache `FUN_00e3fcb0`**, storing the returned **handle into `table[i]`** — the handle table is *written* here, not read. Afterwards `outer+0x10` receives a pooled **handle-set object** (`FUN_00e3ffd0`) whose `+0x04` is set to the table (`FUN_00e47f80`). The cache, the handle encoding and the content-hash key are in `spec-tree-format.md` §13.5.1 (the tree loader does the same for its index list): the handle is a 16-bit index into a `0x14`-byte-stride pool at `0x02ac693c` (plus two flag bits, both clear for every shipped material), deduplicated by a content hash of the *resolved* material (shader hash, flags, counts, constant `vec4`s, per-sampler-slot resolved texture bindings) — so two meshes that use an identical material should share one handle **[HIGH CONFIDENCE — the equality test is the cache's own functor, not read; not tested empirically]**. The draw code binds it with `FUN_00e3f740(handle, 0)`. **The on-disk `u32`s are placeholders**: identity `0…n−1` in **19/19** files (and 11/11 trees), overwritten at load. So the earlier "hash-keyed lookup at load" was right about the *mechanism* (a hash-keyed cache) but the table's file content carries no key at all. **[CONFIRMED — disassembly for the mechanism; CONFIRMED — empirical for the identity content.]** **[OPEN — desk review 2026-09-30: §12 note 1 shows the slot array holds B-relative record offsets on disk; whether the loader takes each record's location from that slot or from the running chain is not stated; to be settled against the executable (`FUN_00747b50`).]**

**Review status (2026-09-30): VALIDATED-BY-DATA: on-disk identity 19/19 (Team B); cache mechanism NEEDS-EXE — desk review (not re-derived from the executable).**

### 11.4 Still open

1. Outer `+0x34` (11.2). 2. What the instancer's per-layer inputs are (the layer descriptor format lives in the terrain/zone data — not read here). 3. Header `+0x04` second hash and flag bits (§10 item 2, unchanged). 4. Whether the two material shader hashes name shader files (§10 item 4, unchanged; needs the shader archive, not touched). 5. The instance object's `+0x100/+0x104` inputs (layer far-fade values) — read but not traced to their source. 6. *(Added desk review 2026-09-30.)* Outer `+0x24` (§12 note 3) — see also §10 item 6.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 12. Independent cross-check by Team B's foliage reader (2026-09-20)

Team B's independent foliage reader (fresh build, 2026-09-20, all 19 `.cfmesh_pc`) matched every §11.2 gate: table at `B + i32(B+0x38)`, tail == count × 24 in 19/19 (reading from `B+0x38` itself: 0/19); stride controls 16/20/28/32: 0/19 each; fade ordering 37/37 records; `billboardFlag` in {0,1} 19/19 (0: 35, 1: 2); `drawGroupIndex` 0 ×19, 1 ×16, 2 ×2; records per file 1×3, 2×14, 3×2; cross-fade pairs 15/18 with the same three exceptions; handle table located 19/19, identity 19/19, header count == material count 19/19; the reader passed a 13-mutant test. **[CONFIRMED — empirical, two independent implementations.]** Their notes:

1. §3's statement that the runtime slot array is zero-filled on disk is wrong: it is non-zero in 19/19 — each 8-byte slot is `{u32 = B-relative offset of material record i, u32 0}`. **(Team B's measurement; not re-derived by a later pass.)**
2. §4 marks outer `+0x10`/`+0x14` runtime-only: on disk `+0x10` is **non-zero in 19/19** and equals the end of the Mesh block (Mesh-relative offset plus the `u32` at mesh `+0x08`), the handle table starts exactly at `align8` of that, and `+0x14` is **0 in 19/19**. **(Non-zero / zero counts re-derived by a later pass; the "equals the Mesh block end" relation is Team B's.)**
3. Outer `+0x24` is not described in §4: 19 distinct non-zero heap-pointer-like values (`0x001E9CB8` … `0x045AFF90`, none zero), not decoded. **(Re-derived: 19 distinct values of 19, min `0x1E9CB8`, max `0x45AFF90`.)** Outer `+0x34` (values 40 ×8, 80, 94, 96 ×2, 148 ×2, 228, 238, 262, 280, 292) remains **OPEN**.
4. §11.2's "plants 8–32" is loose: fade-out values go down to 5–6 (`fol_grass_lg_a01`: 5.0/6.0; `oas_fol_grass_a`: 5.68/7.86). **(Re-derived: smallest fadeOutStart 5.0, fadeOutEnd range 6–270.)**

*Notes 1–4 are folded into the bodies as superseded markers (desk review 2026-09-30): note 1 → §3, §11.3; note 2 → §3, §4; note 3 → §4, §10, §11.4; note 4 → §7, §11.2.*

**Review status (2026-09-30): DESK-PASS, text fixes applied (notes folded into §3/§4/§7/§11.2) — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/adv_foliage.md`): folded §12 notes 1–4 into §3/§4/§7/§11.2 as superseded markers (zero-filled slot array, runtime-only `+0x10/+0x14`, missing `+0x24` row, "plants 8–32"); corrected §4 offset base to B-relative and §2 "per build" cap; fixed §1 "two materials" wording; reduced §6.3 texture list to count + representative names and fixed `flat-normalmap_n` attribution; corrected the front-matter clean-room claim; replaced meta-comments with neutral provenance; added OPEN notes (odd-count alignment, record size semantics, header `+0x28..+0x2F`, `name_offset` base, bit `0x08` location, B-hash/`vec4` mapping, high halves, 15/19 vs 6/19) and per-unit review status lines.
