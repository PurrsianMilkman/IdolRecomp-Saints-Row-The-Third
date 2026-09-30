# Saints Row: The Third — `.ccar_pc` / `.gcar_pc` Vehicle Geometry Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, target selected from `spec-format-inventory.md` §6. Vehicle *data* (stats, customization) was already documented (`spec-vehicle-data.md`, `spec-geometry-format.md` §5); the 3D model container itself was untouched.
**Scope:** The `.ccar_pc`/`.gcar_pc` pair: the outer vehicle-assembly header, the part hierarchy and its binding to `.rig_pc` bones, the two *embedded* already-documented formats (mesh and morph), and the layout of the GPU-side `.gcar_pc`.
**Method:** Constructor-first, per the peer's direction — and it paid off immediately. Disassembly of the registered constructor and its direct callees, cross-checked against **every shipped `.ccar_pc`/`.gcar_pc` pair** (393 pairs across `vehicles`, `vehicles_preload`, `dlc1/2/3`). A population magic-scan detected the reuse of two existing formats before any new structure was read; every structural claim below is tested against the full population, with control baselines where the claim is an offset or a sentinel.
**Cleanroom compliance:** No decompiled code or original identifiers appear below. Magic numbers, offsets, strides and enum values are load-bearing literal format data. Vehicle and part names quoted (e.g. `PD_wheel_FL`, `truck_2dr_garbage01`) are ordinary shipped data. Function addresses are cited as evidence, not reproduced as identifiers.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **`.ccar_pc` is not a new mesh format. It is a vehicle *assembly* container** that embeds two formats this project has already resolved and adds a part hierarchy on top:
  - at header `+0x04`, the **complete `.ccmesh_pc` sub-header / six-array / "Mesh" sub-block machinery** (`spec-geometry-format.md` §4.1–§4.1.2) — the shared chain lands on the "Mesh" version field in **393 / 393** files and its cross-reference value equals the paired `.gcar_pc`'s leading word in **393 / 393**;
  - at header `+0x08`, the **`.cmorph_pc` "Morph" block** (`spec-morph-format.md`) in **326 / 393** files — vehicle damage deformation as morph targets — and these are **mode 0**, the GPU-side path that *no standalone morph file used*. `.gcar_pc` plays the `g`-file role: the mode-0 bookend sentinel sits at exactly the `.gcar_pc` offset given by header `+0x0C` in **326 / 326**, against **0 / 2,400** random offsets. **[CONFIRMED — empirical.]** This closes an open item in the morph spec (its §11 item 7).
- **Vehicle parts are a named hierarchy bound to `.rig_pc` bones**: 9,951 part records across the population, every name pointer resolving to a real part name (`PD_wheel_FL`, `Hood`, `steering_wheel`, `rotor_main_a`…), each with a 4×4 transform and a parent index — and the loader matches every part name to a bone in the vehicle's rig. **[CONFIRMED — empirical + disassembly.]**
- The material-reference block that opens standalone meshes is **optional here and absent in 393 / 393** — the loader calls its validator and proceeds regardless of the result. Materials come from elsewhere (§7).
- A **part-type enum** (1–26) decodes cleanly from the shipped names: wheels front/rear, hinged panels, windows, suspension, steering, wings, rotors (§4).
- The engine **hardcodes special cases by vehicle name** for four vehicles (§5) — a reimplementation must reproduce them.

## 2. Population facts

| | |
|---|---|
| Pairs | **393**, every `.ccar_pc` has its `.gcar_pc` |
| `.ccar_pc` size | 16,556 … 166,468 bytes |
| `.gcar_pc` size | 172,036 … 1,257,308 bytes |
| Extraction | all pairs extracted cleanly (nested `.str2_pc` bundles are raw-stored; no mode-(a) limitation applies) |
| Embedded morph | 326 present / 67 absent — and header `+0x08` reads `-1` in exactly those 67 |

## 3. Outer header

Offsets from file start. "Offset" fields are file-relative on disk and fixed up to absolute pointers at load, with `-1` meaning absent — the idiom documented for `.rig_pc`, `.ccmesh_pc` and `.anim_pc`. Which fields are offsets comes from the loader; the population figures confirm none ever violates its range.

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | `u32` **`0x38`** — required by the loader | **[CONFIRMED — disassembly + 393/393.]** |
| `+0x04` | **Offset of the embedded mesh region.** The material-block validator is invoked there but its result is ignored when it fails (absent in 393/393); the `0x424BD00D` sub-header sits at exactly this offset (393/393, already 8-aligned), and the full `.ccmesh_pc` chain from it reaches the "Mesh" version field `9` (393/393). | **[CONFIRMED — disassembly + empirical.]** |
| `+0x08` | **Offset of the embedded Morph block** (`0x1337BEEF`/v5 → `0x0BADBEEF`/v3), or `-1`. Exact in 326/326; `-1` in the 67 morph-less files. | **[CONFIRMED — disassembly + empirical.]** |
| `+0x0C` | **Offset into `.gcar_pc`** handed to the morph as its secondary buffer, or `-1`. Within the paired `.gcar_pc` in 326/326, and ≥ the `.ccar_pc` size in all 326 (it is *not* a `.ccar_pc` offset). | **[CONFIRMED — disassembly + empirical.]** |
| `+0x10` `+0x18` `+0x28` `+0x2C` `+0x38` | Offsets, never `-1` (393/393); fixed up by the constructor; targets not characterized | **[CONFIRMED — disassembly; OPEN — what they point at.]** |
| `+0x1C` | Offset (`-1`=null), added 2026-09-12 §11.7 — previously absent from this table (a real gap, not a zero). **Fixed up lazily inside `FUN_00ab0ef0`, not eagerly with the row above.** Feeds the part reference-record remap (§11.7): a small header (own `+0x4` gated `>= 7`, own `+0xC` a count `≤ 100`) followed by a chunked-list array of 16-byte elements. **Added 2026-09-20 (agent AF): the constructor passes the vehicle's `Preload` flag (vehicle-info entry `+0x870` bit 14, `spec-vehicle-data.md` §7.6) as the mode argument of this remap.** | **[CONFIRMED — disassembly.]** |
| `+0x20` | Offset of a byte array the loader *writes*: the part→rig-bone remap (§6) | **[CONFIRMED — disassembly.]** |
| `+0x3C` | Name pointer, looked up as a bone before any part (an anchor/root bone name) | **[CONFIRMED — disassembly; content not tabulated.]** |
| `+0x60`–`+0x7F` | Eight floats seeding the bounding-box accumulation (two `vec4`: min / max) | **[HIGH CONFIDENCE — inferred from use.]** |
| `+0xBC` `+0xC4` | Optional offsets (`-1` in 30 / 386 files) | **[CONFIRMED present; OPEN.]** |
| `+0xC8` | Optional offset to a further embedded resource resolved by a separate loader | **[CONFIRMED — disassembly; OPEN — type.]** |
| `+0xCC` / `+0xD0` | Offset + count (0…3,216) passed through a name-resolution routine | **[CONFIRMED — disassembly; OPEN — very likely a material/name table, see §7.]** |
| `+0xD8` | Optional offset (`-1` in 347) | **[OPEN.]** |
| `+0xDC` / `+0xE0` | Count + array of 32-byte records, each holding two sub-arrays of offsets (counts at record `+0x10`/`+0x18`, pointers at `+0x14`/`+0x1C`) | **[CONFIRMED — disassembly; OPEN — semantics.]** |
| `+0x134` `+0x13C` `+0x140` | Two offset arrays of `count@+0x138` (0…51) plus one offset; the count differs from the part count in 389/393 | **[CONFIRMED — disassembly; OPEN.]** |
| `+0x390` | **Part count** (2…43) | **[CONFIRMED — disassembly.]** |
| `+0x3A0` | **Part records**, `0xE0` (224) bytes each (§4) | **[CONFIRMED — disassembly + empirical.]** |

## 4. Part records (`0xE0` bytes, `count` of them at `+0x3A0`)

| Record offset | Content | Confidence |
|---|---|---|
| `+0x00` | **4×4 `f32` transform, row-major, translation in row 3** — column 3 of rows 0–2 is zero and `[3][3] = 1.0` in **9,951 / 9,951** records (the column-major test passes only in the 1,694 rotation-free records, where both hold) | **[CONFIRMED — empirical.]** |
| `+0x40` | **Name pointer** — resolves to a printable part name in **9,951 / 9,951** | **[CONFIRMED — empirical.]** |
| `+0x44` | **Part type** enum, values 1–26 (§4.1) | **[CONFIRMED present; enum decoded HIGH CONFIDENCE.]** |
| `+0x48` | Flags. Bit `0x8000` is set by the constructor's special cases (§5); bit `0x2000` is cleared when the parent's type is 2 or 3 (a wheel); bit `0x200` gates a nested pointer fixup. Common low-16 values: `1`, `0x41`, `0x201B`, `5`, `7`, `0x43`. | **[CONFIRMED — disassembly for the three bits; OPEN — the rest.]** |
| `+0x4C` | **Parent index**, `-1` or `< count` in **9,951 / 9,951**. Roots per vehicle are typically 14–21: the hierarchy is a *forest* (most parts attach directly to the body/rig), not a single tree. | **[CONFIRMED — empirical.]** |
| `+0x54` `+0xA0` `+0xA4` `+0xAC` `+0xB4` `+0xBC` `+0xC4` `+0xD0` | Offset fields, fixed up with `-1` = null | **[CONFIRMED — disassembly; OPEN — targets.]** |
| `+0x68` | Byte; bit `0x80` excludes the part from bounding-box accumulation | **[CONFIRMED — disassembly.]** |
| `+0xC0` / `+0xC4` | Count + pointer to 64-byte records whose `+0x04` index is remapped through a 100-entry table built by a resolution call | **[CONFIRMED — disassembly; HIGH CONFIDENCE — per-part sub-mesh/material references.]** *(Material-reference reading REFUTED: the remap resolves through a generic deferred request-registration mechanism, not a material-index lookup — §11.7, §11.5; target domain OPEN.)* |
| `+0xCC` | `u32`, clamped to at least `4` for type `17` | **[CONFIRMED — disassembly; OPEN — meaning.]** |

### 4.1 Part-type enum, decoded from 9,951 shipped names

| Type | Records | Representative names | Reading |
|---:|---:|---|---|
| 1 | 3,756 | `windshield`, `bumper_f`, `win_rear`, `bumper_b` | fixed body pieces |
| 2 | 598 | `PD_wheel_FL`, `PD_wheel_FR` | front wheels |
| 3 | 653 | `PD_wheel_BL`, `PD_wheel_BR` | rear wheels |
| 4 | 1,606 | `hood`, `trunk`, `door_r`, `door_l` | hinged / openable panels |
| 6 | 110 | `PD_wheel_FL02`, `PD_wheel_FR02` | additional front axle |
| 8 | 704 | `win_fl`, `win_fr`, `win_door_r` | door windows |
| 14 | 103 | `wing_al`, `wing_cr` | aircraft wings |
| 17 | 265 | `axle_br`, `a_armr`, `shock_f`, `spindle_f` | suspension |
| 18 | 518 | `PD_wheel_FL01`, `PD_wheel_FR01` | front wheels, second set |
| 22 | 292 | `steering_wheel`, `handlebars`, `steeringcolumn` | steering |
| 23 | 562 | `PD_wheel_BR01`, `PD_wheel_BL01` | rear wheels, second set |
| 24 | 118 | `PD_wheel_BR02`, `PD_wheel_BL02` | rear wheels, third set |
| 25 | 183 | `rotor_main_a`, `tail_rotor_b`, `blade_b` | rotors / blades |

**[HIGH CONFIDENCE — inferred from name/type co-occurrence across the whole population.]** Types 5, 7, 9–13, 15, 16, 19–21 and 26 occur too rarely (≤ 92 records) to read confidently and are left **OPEN**. The wheel "sets" (`FL`/`FL01`/`FL02`) most plausibly correspond to customization wheel options, matching the `Wheel_Group` concept in `spec-vehicle-data.md` §3 — **[HYPOTHESIS.]**

## 5. Hardcoded per-vehicle special cases

The constructor compares the vehicle's name against four literals and, on a match, sets flag `0x8000` on specific type-4 (hinged) parts by name — **[CONFIRMED — disassembly]**:

| Vehicle | Affected part(s) |
|---|---|
| `truck_2dr_garbage01` | `Hatch_Rear` |
| `sp_backhoe01` | every hinged part **except** `frontRarm` / `frontRarmB` |
| `truck_2dr_tow01` | `tow_cable_swing` |
| `car_2dr_muscle04` | `trunk` |

These are behaviour, not data — a reimplementation that reads the files faithfully will still differ on these four vehicles unless it reproduces them. **Added 2026-09-20 (agent AF): two further per-name special cases live in the vehicle-info reader rather than the constructor — `car_4dr_genki` and `bike_jet01` (`spec-vehicle-data.md` §7.6).**

## 6. Parts bind to `.rig_pc` bones by name

After parsing, the loader fetches the vehicle's rig (referenced from the global vehicle-info table, not from this file — **the entry's `+0xAC8` name, i.e. the `Vehicle_Animation_Rig_Name` element, `spec-vehicle-data.md` §7.1/§7.3; added 2026-09-20**), then looks up — by name — the anchor at `+0x3C`, **every part name**, and four fixed names: `camera`, `camtarget`, `camFOV`, `camDOF`. Each lookup yields a bone index (or "missing"), and the results are compacted into a byte remap array written at header `+0x20`. **[CONFIRMED — disassembly.]** ~~The lookup resolves against the rig's **per-bone name-hash table** (`spec-rig-format.md` §5 — the engine-wide string hash of the lowercased name), so part names are matched by hash, not by string compare.~~ **CORRECTED 2026-09-20 (`spec-rig-format.md` §13.1, agent AD; consumer scope stated there): the by-name routine is a case-insensitive string compare over the bone-name pointers (a linear scan of the bone array, first match or -1) — it never touches the hash table; no runtime reader of that hash table was found in the rig accessor range.** **[HIGH CONFIDENCE — inferred.]** Consequence: vehicle part transforms (§4) are expressed relative to rig bones, and the `.rig_pc` format (`spec-rig-format.md`) is a hard dependency of vehicle rendering, not just of characters. The four camera bones are how the driving/customization cameras attach.

Three bounding boxes are computed from root parts (parent `-1`) filtered by type bitmask, seeded from `+0x60` — **[HIGH CONFIDENCE — inferred: three AABBs for three purposes]**.

## 7. What is embedded, and what is not

- **Mesh** (`+0x04`): the `.ccmesh_pc` machinery in full. The "Mesh" sub-block's flags bit 0 is set in **393 / 393** — vertex data lives in `.gcar_pc`, as for character meshes. Its primary buffer count/stride reads as a 2-byte-element stream (e.g. 19,728 × 2) — the same shape documented in `spec-geometry-format.md` §4.1.2. **[CONFIRMED — empirical.]** The g-side cursor initialisation traced for character meshes (`FUN_00751f60`) is *not* the code path here; the vehicle constructor passes a single cursor. The primary buffer starting at `.gcar_pc` offset `0x10` is therefore **[HIGH CONFIDENCE — inferred from the shared code path, not re-derived for vehicles]**. *(See also `spec-vertex-format.md` §4.2: exact `g`-side replay — check value, align 16, index buffer, then channels — validates 388/388 `.ccar_pc`/`.gcar_pc` pairs.)*
- **Morph** (`+0x08`, `.gcar_pc` data at `+0x0C`): mode **0** in 326/326 — descriptor stride `0x18`, 12-byte elements read from the secondary buffer with per-target `0x0BADBEEF` bookends, exactly the layout `spec-morph-format.md` §6 derived from the loader but could not observe. Now observed. The morph region always follows the vertex data in `.gcar_pc` (326/326), typically ~350 KiB later — the intervening space being the mesh's remaining channel/index streams. **[CONFIRMED — empirical.]**
- **Materials — substantially resolved 2026-09-11, see §7.1.** What vehicles lack is only the material-*reference* block; they **do** carry the mixed-case name blob and the same per-material texture-binding entries as `.ccmesh_pc`.
- **Materials (original entry):** the shared material-reference block is absent from every vehicle file. The `+0xCC`/`+0xD0` table passed through a name-resolution routine, together with `spec-geometry-format.md` §5's per-vehicle `.cvtf_pc` color/material catalogue, is the likely source — **[HYPOTHESIS; OPEN.]**

### 7.1 Texture binding — vehicles use the mesh mechanism (added 2026-09-11)

**Vehicles are not missing their texture data.** They lack the material-*reference* block (the lowercase name list at file `+0x00`), but they carry the **length-prefixed mixed-case name blob** at sub-header `+0x88` — the same blob `.ccmesh_pc` binding entries point into, located by the same rule including its `+1`:

```
blob_data = align16(subheader + 0x88 + 4) + 1
```

Confirmed the cheap way: the first `.tga` string in a car lands on exactly that address. **[CONFIRMED — empirical.]**

Applying `spec-vertex-format.md` §8.3 unchanged — 12-byte entries `{u32 blob offset, u32 sampler hash, u32 slot index}`, with that section's required guards (run length ≥ 2, non-zero hash) — yields binding runs in **372 / 372** vehicles, every offset landing on a string start.

**The decisive evidence is cross-format:** two of the sampler hashes are **the same 32-bit constants already measured in character meshes**, `0x2808EB90` and `0x69B48F91`. Two unrelated carriers agreeing on specific sampler identifiers is not something a pattern search produces. **[CONFIRMED — empirical.]**

Vehicle sampler hashes observed: `0x2808EB90`, `0x3819300B`, `0x69B48F91`, `0xD263FB52`, `0x23C46985`, `0x433202E1`, `0xA1F59A7A`, `0x5C9FF0A8`.

**And a correction this forced on `spec-vertex-format.md` §8.3:** the **slot index is not the semantic**. `0x2808EB90` is slot 1 in meshes and slot 0 in vehicles, so a reader must key on the **hash**. The "slot 0 = diffuse, slot 1 = normal" reading is a character-mesh convention, not a format rule — over-generalised from a single carrier.

**⚠ `+0x0C` does NOT mean the same thing here as in `.ccmesh_pc`.** There it equals `1 + max(material id)` with ids dense from zero (241/241 multi-material meshes). **On vehicles that is false.** It is an **upper bound**: `max(material id) < u16(subheader + 0x0C)` in **372/372**. *(Reframed 2026-09-11, `spec-vertex-format.md` §8.4.1/§8.4.3: `+0x0C` is an exact count of declared materials on both carriers; vehicles declare 2× what their draw ranges reference, which is why it looked like an upper bound.)*

Whole-population figures, all **372 / 372**: draw ranges parse; max material id below `+0x0C`; material ids dense from zero; and the index buffer fully covered by the ranges. *(An earlier version quoted these as 86/86 — that denominator was an artefact of a group-count cap of 64 in this project's own parser, a limit taken from character meshes where nothing exceeds it. Vehicles are multi-part assets and legitimately have more groups, so the cap silently discarded 286 of 372 files and the harness still reported a confident N/N. See `HANDOFF.md` §5.)* A vehicle declares a material set larger than any single mesh inside it uses, which is what a multi-part asset should do. *(An earlier version of this section asserted the mesh semantics here; retracted.)* **[CONFIRMED — empirical.]**

**Vehicle draw-range material ids are packed 16:16** — `id = field & 0xFFFF`, `field >> 16` a ~~sub-mesh/part index~~ **vertex-channel index** *(corrected: §11.1, RESOLVED 2026-09-28; the part-index reading is refuted there, and the 86/86 denominator is retracted in the paragraph above — 393/393 vehicles, 90,290/90,290 ranges per §11.1)*. See `spec-vertex-format.md` §8.2's table. **[CONFIRMED — empirical, 86/86.]**

**Formerly "still open" — item 1 below is RESOLVED, 2026-09-18. See the closure after the refuted-candidates table before treating it as open.**

1. ~~**Which material each binding run belongs to.** Meshes have one record per material and the association is direct. Vehicles do **not** yield to the same technique: the tail is **not** a clean one-record-per-material periodic array (best periodicity 0.74–0.90, against the 0.98–0.99 that located the mesh records), and the association cannot be recovered from `+0x0C` because that is a whole-vehicle upper bound while a run belongs to one mesh. **A reader requiring `run count == material count` will therefore refuse vehicles structurally, not marginally — relaxing that equality to a bound would not be safe.**~~

**Six candidate associations are refuted**, each tested as "does the binding-run count equal this?" across all 372 vehicles:

| Candidate | Rate |
|---|---|
| distinct materials | 4 / 372 (0.011) |
| distinct sub-meshes | 3 / 372 (0.008) |
| groups | 1 / 372 (0.003) |
| `u16(subheader + 0x0C)` | 1 / 372 (0.003) |
| distinct (sub-mesh, material) pairs | 0 / 372 |
| total draw ranges | 0 / 372 |

All at or below chance — **including the sub-mesh index, which this document previously called the most promising handle.** It is a real per-asset index (0–14, non-zero in 77,886 of 87,012 vehicle ranges) but it is not the association key. **[CONFIRMED — empirical, all six negative.]**

~~**Where the answer probably is not:** more count-matching. In `.ccmesh_pc` the association is direct because the tail is one record per material with the runs inside it; vehicles have no such clean periodic array (periodicity 0.74–0.90 against 0.98–0.99 for meshes), so a tail scan finds runs without knowing their record boundaries. **Segmenting the vehicle-side record structure has to come first.**~~

**RESOLVED, 2026-09-18 — this document's own §11.2 already did the segmentation this item called for, six days before this item was updated to reflect it; the two were never cross-referenced until now.** §11.2 (2026-09-12) fully disassembled and validated the vehicle per-material loop's helper (`FUN_00e71090`, the shared inline-material-definition parser also used by trees/foliage) and found each material's own record has a **fixed-offset array A** — 12-byte `{blob offset, sampler hash, slot index}` entries, the exact "binding run" shape this item is asking about — sitting immediately after that material's own `0x30`-byte header. That parser (`tools/harnesses/veh_pairing.py`'s `parse_materials()`) is already validated 393/393 on a hard exact-consumption gate (every material's header+arrayA+arrayB+arrayC must sum to exactly its own self-declared size, or the file is excluded rather than mismeasured — zero exclusions in practice). **§11.2 itself stopped short of the connection**, reading its own negative ("no identity field in the record") as "route (b) supplies no additional handle for the association task" — true for identity-by-name, but the record's own array A *is* its binding run by direct construction, needing no separate identity field at all: association is positional (material `i`'s own array A belongs to material `i`), the same way the per-material loop's own indexing already establishes material identity (§11.2's own "material identity is purely positional" finding, one paragraph earlier). **[CONFIRMED — disassembly, `spec-vehicle-geometry.md` §11.2, re-verified this session by running `parse_materials()` fresh across all 393 `.ccar_pc` files: 0 parser-untrusted (the exact-consumption gate holds 393/393), 16,781 total binding-run entries recovered this way.]**

**One honest methodology note, not a doubt about the finding above.** A natural sanity check — does concatenating each material's own `entries` reproduce what the *original*, boundary-blind periodicity scanner (`car_matassoc.py`'s `guarded_run`, the tool that produced the six-candidate refutation table above) finds when run independently over the same raw bytes? — does **not** cleanly match (1/393 files exact). Traced to ground, not left as an unexplained wrinkle: `guarded_run` requires a run length ≥ 2 before it will report a match at all (a sensible guard when you have no record boundaries and need to reject coincidental single-triplet false positives in raw bytes) — but real materials legitimately carry exactly **one** texture binding (`a_count == 1`), which that guard silently drops. Confirmed directly on `car_4dr_standard03_0.ccar_pc`: materials 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28 (12 of 36) each have `a_count == 1` with the identical single entry `(168, 1773440913, 0)`, all silently excluded by the ≥2 filter. Relaxing the guard to accept length-1 runs was tried and made agreement *worse* (365 found vs. 76 expected on the same file) — without record boundaries to anchor it, a length-1 acceptance threshold picks up unrelated coincidental matches elsewhere in the blob, which is exactly why the ≥2 guard existed in the first place. **The disagreement is fully explained by the older heuristic's own known trade-off, not by any defect in the exact parser** — `parse_materials()`'s own internal validation (byte-exact consumption, 393/393) is strictly stronger evidence than agreement with a heuristic that was only ever a workaround for not having record boundaries, which this item no longer needs.

New harness: `tools/harnesses/veh_matassoc_final.py` (the re-verification + methodology-note script above).

**Cross-team implementation, 2026-09-18 (relayed, independently verified on their end before acting on it — they re-read this section and §11.2 directly rather than the summary alone).** Team B rewrote their reader's `MaterialBindings::parse()` from a sliding-window guess-and-check search to direct arithmetic over the fixed-stride descriptor arrays + self-declaring chain — no `runs found == declared count` guess needed, which is exactly why it unblocks vehicles specifically (they declare roughly 2× the materials their draw ranges reference, so that guard was structurally unsatisfiable for them before). **Real-data result: vehicles went from 3/372 located to 393/393**, recovering 17,026 binding runs / 36,010 texture entries; characters stayed unregressed at 1,995/2,002 (the 7 unlocated are one already-explained class, tiny censor-overlay placeholders too small for a full record). They separately found their own project already had an independent copy of this same finding on file (`spec-vertex-format.md` §12.8 on their side, since 2026-09-12/13, flagged "stated but not applied") — this closure connected that too. **Provenance note, per `HANDOFF.md` §27.8: relayed, not independently rerun against their binary on this side.** One figure checked directly against this session's own re-verification run and matches exactly: 36,010 texture entries (this side's own `veh_matassoc_final.py` rerun independently gets 36,010 flat entries across the same 393 files). **Update — fully reconciled, exactly, 2026-09-18.** The 17,026 vs. 16,781 gap was not a data disagreement: their harness counts one "run" per **material**, regardless of whether it has any texture bindings; this side's 16,781 counted only materials with `a_count > 0`. `17,026 − 16,781 = 245` — and `245` is exactly the count of non-null materials with `a_count == 0` across the same 393-vehicle population (independently re-verified on this side, not just accepted: `total non-null materials 17,026`, `a_count==0: 245`, `a_count>0: 16,781`, matching Team B's own reconciliation digit-for-digit). Real and expected, not an artifact: vehicles declare roughly twice the materials their draw ranges reference (`spec-vertex-format.md` §8.4.3), so a real chunk of materials legitimately carry zero texture bindings. Two different quantities sharing the word "run," now fully reconciled rather than left as an unexplained wrinkle.

**Scope note Team B stated themselves, not overclaimed:** this closes their reader/parsing path only — their interactive viewer still has no rendering surface for vehicle files, a separate open task on their side.
2. **Where vehicle paint comes from.** The runs are dominated by shared/generic textures — `shd_damagenormal_n.tga` leads nearly every run, then `burn_test`, `viewsphere_chrome`, `viewsphere_rubber`, `missing-alpha`. The car's own livery (`al_veh_universal.tga` and siblings) is *present in the blob* but was not observed bound. That is consistent with paint being applied at runtime from the `.cvtf_pc` customisation catalogue — which is what §7 hypothesised — rather than baked into the model. **[HYPOTHESIS.]** **[Resolved for the source: CONFIRMED — disassembly, §7.2 Q3; the application path is still unread.]**

### 7.2 Three loader questions from the implementation team, answered (2026-09-11)

#### Q1. How is the vehicle material region walked? — **by the identical `.ccmesh_pc` code**

**There is no vehicle-specific material walk.** The embed function reached from the
registered vehicle constructor calls the shared material-*reference*-block validator
and then hands the cursor to **the full `.ccmesh_pc` sub-header / six-array / Mesh
machinery, the same function, unmodified**.

Two consequences, and the second is the useful one:

1. The reference-block validator **returns "absent" in 393/393 vehicles and its
   result is ignored** — the documented reason vehicles carry no material-reference
   block (§26.10 of `HANDOFF.md`). So the "material region" a vehicle has is the
   material set inside the shared Mesh sub-block, not a reference block.
2. Because both carriers go through **the same function**, the question *"do vehicle
   material sub-records use the length-prefixed idiom?"* is **not a vehicle question
   at all** — it is the same question about `.ccmesh_pc`, and any answer established
   for one carrier transfers to the other by construction. That collapses the
   problem as hoped, but via *"identical code path"* rather than *"same idiom
   reused"*, which is a stronger reduction: it needs no assumption of similarity.

**[CONFIRMED — disassembly.]**

> **RESOLVED 2026-09-11, shortly after this section was written — see
> `spec-vertex-format.md` §8.4.** The material set is **fixed-stride descriptor
> arrays** (`count × 8` and `count × 4`, 8-byte aligned, counts at sub-header `+0x0C`
> and `+0x0E`) followed by a **variable-length per-material loop** that runs exactly
> `u16(+0x0C)` times. There are no length-prefixed sub-record headers at the array
> level, so that region needs no segmentation at all. By the identical-code-path
> result above, this holds for vehicles unchanged.

**What was NOT answered when this section was first written:** the advance rule *inside* the material set. At the
reference-block level the advance is length-prefixed — a length field in the block
header, advance = length + 1, then 16-byte align, the documented leading-NUL
convention — but that validator **skips the whole block by its declared length and
never enumerates sub-records**, so it supplies no per-record boundaries. Reading the
sub-record advance means reading the mesh material-set walk itself, which this pass
did not do. **[OPEN — stated as a limit, not as a negative result.]**

#### Q2. Is the material set scoped per-part? — **not established; do not assume it**

> **Related finding (2026-09-11), `spec-vertex-format.md` §8.4.3:** vehicles declare
> **exactly twice** the materials their draw ranges reference — ratio `2.000` with a
> maximum of 2.000, against characters at 1.0 in 549/549. Half of every vehicle
> material set is bound to no geometry. That does **not** answer per-part scoping, but
> it does mean any count compared against a geometry-derived quantity is off by 2× by
> construction.

Part records are walked by a dedicated pointer-fixup pass over `count` records of
`0xE0` bytes at `+0x3A0`. That pass fixes up **eight pointer fields** per record
(`+0x54`, `+0xA0`, `+0xA4`, `+0xAC`, `+0xB4`, `+0xBC`, `+0xC4`, `+0xD0`), several of
which have no documented semantics. So per-part scoping is **structurally possible
and the candidate fields are enumerable**, but nothing read in this pass shows a
material count or index inside a part record. **[OPEN.]**

**A caution about how this question was approached**, because it cost a pass: a
whole-binary scan for functions touching `+0x390` together with `0xE0` returned 23
matches, none of them informative — small struct displacements have no selectivity
at binary scale (the same failure recorded in `HANDOFF.md` §26.12). The part walker
was found instead by reading the registered constructor's own call list. **Reach for
the entry point, not the displacement.**

#### Q3. Do vehicle body textures come from a runtime catalogue? — **YES**

**`.cvtf_pc` is a separately registered resource type with its own constructor**,
registered in the same resource table as `.ccar_pc` but entirely independent of it —
and registered **twice**, as distinct *vehicle* and *character* variants selected by
an index into a pointer table. The vehicle mesh loader never parses it.

Corroborating, and independent of the registration table: a **separate paint
subsystem** exists with its own named colour slots (`Base_Paint_Color`, and
`HeroGloss Paint` / `HeroMatte Paint` finishes), living in functions unrelated to any
geometry loader, alongside purchase/slot-selection entry points.

**So generic-looking bound textures on vehicle body panels are a CONFIRMED
NON-BUG** — the mesh is not the source of truth for those surfaces, and a renderer
that binds what the mesh names is behaving correctly. This resolves §7.1's
"where vehicle paint comes from" from **[HYPOTHESIS]** to **[CONFIRMED —
disassembly]** for the *source*; the paint application path itself is still unread.

#### The registered constructor, which this spec never named

`.ccar_pc`'s registered constructor is documented in `HANDOFF.md` and the format
inventory but was **missing from this document**, which cited only the *mesh*
constructor — so the vehicle-side entry point was one indirection away from anyone
reading here. Recorded now: the vehicle constructor is reached only through the
resource registration table's stored function pointer, never called directly, and
its callee list is the index to everything above (embedded mesh+morph, name table,
`+0xE0` records, `+0x134`/`+0x140` arrays, part records, part-to-rig-bone mapping).

## 8. `.gcar_pc`

16-byte header — `u32` cross-reference value (equal to the embedded "Mesh" sub-block's check value, 393/393) followed by 12 zero bytes (393/393) — then the mesh's vertex/channel streams, then (for morph-bearing vehicles) the mode-0 morph region at the header `+0x0C` offset. **[CONFIRMED — empirical.]** Same convention as `.gcmesh_pc`.

## 9. Methodology note

"Check the constructor and the registration table first" turned a potentially large investigation into a short one: a single population scan for known magic numbers showed the mesh sub-header in 393/393 files and the morph block in 326/393 *before any new structure was read*. Two prior specs did most of the work here. Worth generalizing: **scan a new format's population for every magic number already catalogued before assuming it is new.**

## 10. Open Items

1. Header offsets `+0x10`, `+0x18`, `+0x28`, `+0x2C`, `+0x38`, `+0xBC`, `+0xC4`, `+0xC8`, `+0xD8` — targets uncharacterized.
2. The `+0xCC`/`+0xD0` name-resolved table and the material path (§7).
3. The `+0xE0` 32-byte records and the `+0x134`/`+0x140` arrays.
4. Part-record pointer fields (§4) and the undecoded flag bits; type-enum values not in §4.1.
5. The vehicle g-side vertex-buffer offset — assumed `0x10` by shared code path, not traced for the vehicle cursor. *(See `spec-vertex-format.md` §4.2: exact `g`-side replay validates 388/388 vehicle pairs; the cursor itself is still not traced.)*
6. ~~The global vehicle-info table the constructor consults (0xB80-byte entries) — the runtime link to `spec-vehicle-data.md`, not characterized.~~ **RESOLVED 2026-09-20 (agent AF) — `spec-vehicle-data.md` §7: 132 entries × `0xB80` bytes at `0x027C87B0`, one per `<Vehicle>` of a `_veh.xtbl`, slot-indexed in `vehicles.xtbl` order; the full name→offset map and flag words are there. CONFIRMED — disassembly.**
7. ~~The packed draw-range id's **high half** (0–14, i.e. `N = max+1` taking only ten values, 324/393 of them multiples of three) — six candidates refuted; §11.9.8 gives the concrete next step (factor the field as a 3-valued × a 1-to-5-valued term instead of testing it whole).~~ **RESOLVED 2026-09-28 — vertex-channel index (§11.1, §11.5).**
8. The `0x30`-byte group record's `+0x1C` dword — the only undetermined field left in that record (§11.9.1).
9. The Mesh header `+0x48`/`+0x50` `u16` table that each group's `+0x28` slots hold pre-resolved pointers into — confirmed NOT the index buffer (§11.9.2), contents still unknown.
10. The `0x28`-byte runtime record array at the render holder's `+0xfc`, whose `u16` at `+0x24` is a group index — its remaining fields and its element count (§11.9.5); ~~if it is also sized `3 × k`, it meets item 7~~ (moot: item 7 is resolved).

## 11. Vehicle material-to-draw-range association, per-part scoping, and the 2x pairing (2026-09-12)

Vehicle material -> draw-range association (task 1), per-part scoping (task 2), and
the 2x pairing (task 3), continuing `HANDOFF.md` section 27.2 items 5 and 6. Every
population is stated with what was counted; "draw range" and "material record" are
distinguished from "binding run" throughout, and no binding-run figure appears below
at all -- this section works entirely from draw ranges and from an exact,
disassembly-derived material-record parser, never from the heuristic texture-binding
scan.

### 11.1 Route (a): the packed draw-range id's high half indexes neither the material set nor the part list

Tested against the **full population**: 393 of 393 `.ccar_pc` files collected, all 393
usable (subheader, Mesh block, group array and part count all located in every file --
0 excluded for any reason), **90,290 draw ranges total**. New harness `veh_assoc.py`.

**Parser sanity check before trusting anything new out of it:** the low half of the
packed id forms a dense prefix from 0 in **393 / 393 vehicles** -- the already-published
result, reproduced here as a wiring check on this harness, not as a new finding.

**Feasibility check (pure arithmetic, before any correlation was tested) -- the task's
own required first step:**

| Candidate index space | Vehicles where `max(high16)` is too large to be a valid index | Population |
|---|---|---|
| materialCount (`u16(subheader+0x0C)`) | 0 / 393 | every usable vehicle |
| partCount (`u32(file+0x390)`) | **12 / 393** | every usable vehicle |
| this vehicle's own group count (`nb`, Mesh header `+0x04`) | 0 / 393 | every usable vehicle |

**The part-list candidate is refuted outright, for free, exactly as the task
anticipated.** Five of the twelve failing vehicles: `plane_giant01_0` (max high16 =
10, partCount = 7), `plane_giant01_1` (10 vs 8), `plane_giant01_2` (10 vs 8),
`boat_wavecraft01_2` (11 vs 5), `boat_wavecraft01_3` (10 vs 5). A failing case here is
a draw range naming a part index that does not exist in that vehicle's own part array
-- exactly what would have to be true for "indexes the part list" to be false, and it
is true 12 times. **[REFUTED -- CONFIRMED, empirical.]**

**The material-set candidate survives the feasibility check but fails a dose-response
test.** `materialCount` ranges from 3 to 102 across the population (median in the
40s, per `spec-vertex-format.md` section 8.4.3); if `high16` indexed into the material
set, vehicles with a larger material set should be able to show a larger `high16`.
They do not: binned by `materialCount`, the mean of `max(high16)` per vehicle sits
between roughly 2 and 12 with no trend as `materialCount` climbs from 3 to 102, and
the **global maximum of `high16` over all 90,290 draw ranges in all 393 vehicles is
exactly 14** -- the same ceiling reported for a smaller sample in
`spec-vertex-format.md` section 8.2, now confirmed on the full population and shown to
be independent of `materialCount`, `partCount` and group count alike. A field that
truly indexed a variable-sized array would grow with that array; this one does not.
**[HIGH CONFIDENCE -- inferred; the feasibility bound alone does not refute it, the
absence of any dose-response does.]**

**A third candidate, not named in the task but cheap to test with the same harness:
does `high16` equal the draw range's own group index** (Mesh header `+0x04`'s group
array; a vehicle's groups run from 6 to over 300, per `HANDOFF.md` section 5's
harness-cap lesson)? Tested with a **shuffled control**, since a bare match rate
without one is not a result:

| | match rate |
|---|---|
| `high16 == own group index` | 2,835 / 90,290 (3.14%) |
| control: group index shuffled within each vehicle | 2,085 / 90,290 (2.31%) |
| vehicles where **every** draw range matches its own group index | 0 / 393 |

The real rate and the shuffled-control rate are close enough that the match is
chance-level, not structural, and no vehicle matches on every range. **[REFUTED --
CONFIRMED, empirical, with control.]**

**So all three candidates tested here are refuted or unsupported, and the achievable
range itself is the clue worth carrying forward:** `high16` is capped at 14
irrespective of any per-vehicle structural count, which argues for a small,
fixed-size enumeration (an engine-level constant, not a per-file array size) rather
than an index recoverable from this file's own declared counts. ~~**[OPEN -- what the
15-value cap (0-14) actually enumerates.]**~~ **RESOLVED 2026-09-28, jointly (SPEC TEAM disassembly + Team B population test) -- see the corrected row later in this section's summary table and `spec-render-pipeline.md` §16.4/§18.6: `high16` is a vertex-CHANNEL index. Disassembly (`spec-render-pipeline.md` §18.6, `FUN_00b13bf0`) shows it read directly from the draw-range record and used with no scaling or offset as a plain index (times the array's own stride) into the vertex-channel array at the mesh header's `+0x18` -- the exact array `meshHeader+0x10`(count)/`+0x18`(array), stride `0x18` this document's own ~~line 955-956~~ §11.9.2 'vertex channels' bullet (pre-dating this pass) already documents as the ordinary vertex-channel array `spec-vertex-format.md` describes. Team B independently confirmed the MEANING survives a full-population, same-vehicle join with zero counterexceptions: 393/393 vehicles and all 90,290/90,290 draw ranges have `high16` (their `submeshIndex`) `<` that same vehicle's own real channel count; the real per-vehicle channel-count range is 2-15, so the population-wide max valid index is exactly 14 -- matching `high16`'s own already-measured maximum to the integer. Two independent methods (causal disassembly, exhaustive real-data join), same field, same answer, zero exceptions either side -- the same bar that killed the four earlier candidates for this field.**

**Further corroboration, 2026-09-29 (Team B, a real shader-driven draw of `car_4dr_genki_0`).** A tighter, per-channel version of the same check: across all 57 real draw groups of that vehicle, the maximum vertex index actually used by draw ranges pointing at `submeshIndex`=N equals channel N's own `elementCount − 1` exactly, for all 9 real channels (elementCounts 6954, 3662, 15, 1605, 1917, 11, 1046, 1145, 2), zero exceptions. This is a stronger bound than the earlier population join (which checked `high16` stays within the valid channel-count range) -- it confirms a draw range pointing at channel N never reads a vertex index outside that specific channel's own real bounds, on real geometry that has now actually been drawn. **[CONFIRMED -- empirical, real data, one real vehicle's full draw-group population.]**

*(A loose, uncontrolled observation, flagged as exactly that and not promoted: 27,887
of 41,087 groups have every one of their own draw ranges sharing a single `high16`
value, and where consecutive groups differ the value drops rather than rises 8,426
times across the 393 vehicles -- consistent with a value that resets per something
larger than a group, but this was not tested against a control and is recorded only
as a lead, not a result.)*

### 11.2 Route (b): the per-material loop's helper has no identity field -- and is not vehicle- or mesh-specific at all

`FUN_007522f0` (503 bytes, the material-set walk named in `HANDOFF.md` section 26.16)
was dumped in full this session (`VehMatHelper.java`, output in
`scratchpad/veh/mathelper.txt`) together with its callees. Its body matches
`spec-vertex-format.md` section 8.4's published pseudocode exactly: an `N2`-count
fixed-stride pass (arrays of `N2*8` and `N2*4` raw bytes, 8-aligned), then an
`N`-count (`materialCount`) fixed-stride array (`N*8`), then a loop of exactly `N`
iterations calling one helper per material with a **variable** advance.

**That helper is `FUN_00e71090` -- and it is the same shared inline
material-definition parser this project already characterised for trees and
foliage**, per a bookmark comment already present in this Ghidra project from that
earlier work (33/33 foliage records, 37/37 tree records, exact-size gated). Reading it
against the vehicle/mesh call site confirms the identical layout applies unchanged:

```
u32 declaredSize                 // measured from THIS field's own start (see below)
align 8
0x30-byte header:
  +0x00 u32 shaderHash   +0x04 u32 secondHash   +0x08 u32 flags
  +0x0C u16 A_count (texture bindings)   +0x0E u8 B_count   +0x0F u8 C_count
align 4 ; array A: A_count * 12 bytes  {u32 blob offset, u32 param hash, u32 slot}
align 4 ; array B: B_count * 4 bytes
align 16; array C: C_count * 16 bytes
assert (cursor - position of the declaredSize field) == declaredSize
```

Array A's 12-byte entries are exactly the `{blob offset, sampler hash, slot index}`
shape `spec-vertex-format.md` section 8.3 already documents for material texture
bindings -- but located here by an **exact, disassembled offset** (immediately after
the fixed 0x30-byte header, 4-aligned), not by the sliding search that section's own
text says varies per record (`+0x64`, `+0x04`, `+0x74`, ...). That variation is real
for *where the record itself starts* in the file, but not for *where array A sits
relative to the record's own header* -- the offset from a material record's header to
its own array A is fixed. **This is a candidate improvement to the sliding search in
`spec-vertex-format.md` section 8.3, flagged here for the session that owns that file
rather than applied by this one** (this document's hard rule is to never edit that
file). **[CONFIRMED -- disassembly.]**

**There is no identity field.** The 0x30-byte header holds two hashes, flags, and
three array-length bytes -- nothing that names a part, a draw range, or a position in
any other array. **Material identity is purely positional**: the i-th call in the
per-material loop populates material record `i`, which is exactly the index the
draw-range low16 field already names (`spec-vertex-format.md` section 8.2).
Consequently **route (b) supplies no additional handle for the association task** --
it rules out "the identity is encoded inside the material's own record" rather than
finding it. **[CONFIRMED -- disassembly, a negative result.]**

**This record layout was turned into an exact parser and validated on the full
population before being trusted for section 11.4 below: 393 / 393 `.ccar_pc` files
replay with every material record's consumed byte count matching its own declared
size exactly** (`veh_pairing.py`, function `parse_materials`) -- a hard gate, not a
heuristic: any file that failed this would have been excluded rather than silently
mismeasured. *(One methodology note volunteered because it cost a debugging pass: the
declared size in `FUN_00e71090` is compared against `cursor - (the position of the
size field itself)`, not against `cursor - (the position right after the size
field)`. Using the latter -- the more natural reading of "consumed since the size was
read" -- undercounts by exactly 4 on every record and makes every material after the
first look corrupt. The disassembly's own comparison is between the current read
cursor and the cursor position captured *before* the four-byte size field was read,
not the position immediately after it; that pre-read cursor is the only correct anchor;
this is the same family as `HANDOFF.md` section 5's "an offset without an explicit
base is not a location" lesson, one level more subtle because the ambiguity is in
which of two adjacent byte positions a self-declared length counts from, not in which
field holds a base at all.)*

### 11.3 Per-part scoping: the eight pointer fields, and the +0xC0/+0xC4 mechanism read in full

`FUN_00aaf690` (the part-record pointer-fixup pass, already on file in
`tools/car_parts.txt` from an earlier pass, re-read here) walks `partCount` records of
`0xE0` bytes at file `+0x3A0`. Re-deriving its eight fixed-up offsets independently
from the disassembly's own pointer arithmetic (the fixup loop's own cursor enters
already pointed at `record+0x54`) reproduces the existing table exactly: `+0x54, +0xA0, +0xA4, +0xAC,
+0xB4, +0xBC, +0xC4, +0xD0`. **[CONFIRMED -- disassembly, reproducing the existing
finding independently.]**

**New in this pass:** flag bit `0x200` (record `+0x48`) gates a **second, nested**
fixup. When the bit is set and the already-fixed-up `+0xA4` pointer is non-null, the
code additionally treats the dword at `(target of +0xA4) + 0x08` as itself an
offset-or-null field and fixes it up the same way (`-1` -> null, else `+= base`). So
whatever `+0xA4` points at is a structure with its own embedded pointer at its own
`+0x08`, conditionally present. **[CONFIRMED -- disassembly; OPEN -- the rest of that
structure's contents.]**

**The `+0xC0`/`+0xC4` mechanism, previously recorded as `HIGH CONFIDENCE` from an
earlier pass, was read in full this session** (`VehPartRefs.java`; the ctor's own
direct callee `FUN_00ab0ef0`, 388 bytes, called immediately after the four hardcoded
per-vehicle special cases and before the three bounding-box passes -- see
`scratchpad/veh/ctor_full.txt`):

1. It builds a table of up to 100 entries, each entry a 4-byte value, by calling
   `FUN_007616c0` (2,060 bytes, read in full together with its own callee
   `FUN_0075f880`). **That function is a generic, exception-handling-instrumented
   chunked-list copy/pointer-fixup utility using per-thread TLS allocation -- it is
   not name- or material-specific.** It fixes up three embedded pointer fields per
   16-byte source element and copies elements one at a time into the destination
   buffer. This refutes my own working hypothesis while investigating it (that this
   was a material-name-to-handle resolver, by analogy with the material lookup seen
   in `FUN_00e3fcb0` during route (b)'s reading) -- **read before trusting the
   analogy, and the analogy was wrong.** **[CONFIRMED -- disassembly.]**
2. For every part record (`partCount` iterations, stride `0xE0`, base = the part
   array), it reads `count = part+0xC0` and walks that many 64-byte records at the
   pointer `part+0xC4`. For each 64-byte record whose own `+0x04` field is not `-1`,
   it **overwrites that field** with that 100-entry table's entry at the old value's
   index -- remapping a small
   per-record index through the 100-entry table built in step 1.
   **[CONFIRMED -- disassembly, independently reproducing the mechanism the existing
   spec table already asserted.]**
3. A companion function, `FUN_00ab0d20` ("unresolved reference"), is called when a
   64-byte record's `+0x04` reads `-1` while its own `+0x00` reads `0` -- i.e. a
   record of one particular kind that was expected to resolve and did not. Its
   457-byte body carries **no string literals** (checked directly), so it does not
   name the referenced domain textually. **[CONFIRMED -- disassembly; OPEN -- what
   kind `+0x00 == 0` denotes.]**

**Net effect on task 2: the mechanism is now nailed down by disassembly two levels
deeper than before (which function, what it does, and that its "resolver" step is
generic rather than domain-specific), but the mechanism's own genericity means the
semantic target -- material, mesh, or some other resource -- is still not confirmed
from this reading alone.** The existing `HIGH CONFIDENCE -- per-part sub-mesh/material
references` label is therefore **kept, not promoted** -- confirming the machinery is
not the same as confirming what runs through it. **[Per-part material scoping
remains OPEN, more narrowly:]** the eight pointer fields are now fully enumerated and
two of them (`+0xA4`'s nested structure, `+0xC0`/`+0xC4`'s 64-byte records) have a
disassembly-confirmed shape; the concrete next step is to read what writes the 64-byte
records' `+0x00` field (the "kind" tested by the unresolved-reference path) at file
build time, since that is the field this pass never explained.

### 11.4 The 2x pairing is not an aligned or permuted duplication -- falsified, not merely left consistent

Using the exact material-record parser validated in section 11.2, and the referenced
material count `k` computed independently from the draw ranges' own low16 field (not
re-derived from the count-matching this project already retired in
`spec-vertex-format.md` section 8.4.3), two concrete, falsifiable versions of "the
second half is a paired copy" were tested (`veh_pairing.py`).

**Population:** 393 vehicles parsed with the exact-consumption gate passing on all
393; 42 excluded from the pairing tests because their `materialCount` does not permit
a clean split at `2k` (`2k > materialCount`) -- leaving **351 vehicles**. This 42
matches, almost to the record, the **36 at `2k-1`
+ 6 "other"** already reported in `spec-vertex-format.md` section 8.4.3 for the same
population (351 + 36 + 6 = 393), which is a useful cross-check that this harness's `k`
agrees with the earlier, independently-built one.

**Hypothesis 1 -- aligned pairing, material `i` (referenced) paired with material
`i+k` (unreferenced), for every `i` in `[0, k)`:** tested over every such pair where
neither side is a null (size-0) record -- **7,805 pairs across the 351 vehicles**
(0 pairs excluded for a null on either side):

| Property compared between material `i` and material `i+k` | Match |
|---|---|
| `shaderHash` identical | 830 / 7,805 (10.6%) |
| texture-binding count (`A_count`) identical | 2,221 / 7,805 (28.5%) |
| full ordered list of bound texture names identical | 950 / 7,805 (12.2%) |

**Hypothesis 2 -- permuted pairing, the two halves share the same *multiset* of
`shaderHash` values regardless of order:** tested per vehicle across the same 351:
**0 / 351 vehicles** have an identical multiset.

**Both are refuted.** A failing case for hypothesis 1 would be exactly what most
pairs show: a referenced material and its `+k` counterpart with unrelated shaders and
unrelated textures (example, `car_4dr_standard03_4.ccar_pc`, `i=0` uses only
`shd_damagenormal_n.tga`, `i+k=18` uses only `al_veh_universal.tga` -- no relation). A
failing case for hypothesis 2 would be any vehicle where the two halves' shader sets
differ even after sorting, and **every** vehicle in the population is a failing case.
**[CONFIRMED -- empirical, 7,805 pairs and 351 vehicles respectively.]**

**What this does and does not settle.** It does not weaken the count-level 2x finding
(`spec-vertex-format.md` section 8.4.3, `HANDOFF.md` section 26.18), which is about
*how many* materials are declared versus referenced and stands on its own evidence.
What it rules out is the natural next guess about *which* materials the extra half
are -- neither "the same material, listed twice in a fixed order" nor "the same
materials, listed twice in some order" survives contact with the actual per-material
records. **A corrected count still tells only how many, and this section shows that
even a very specific *positional* guess about which ones fares no better.**

**A qualitative observation, stated as exactly that and not as a population
figure, since no controlled diversity metric was built for it:** in the worked
example above, the unreferenced half's texture names repeat a small handful of
generic entries (`al_veh_universal.tga`, `burn_test.tga`,
`flat-normalmap_n.tga`) across many indices rather than each index carrying a
distinct name -- consistent with, and now confirmed by an exact parser rather than
the heuristic scan that first noticed it in section 7.1's closing paragraph, the
reading that the unreferenced half is dominated by shared/generic placeholder
materials. **[HYPOTHESIS -- unconfirmed at population scale; the aligned- and
permuted-pairing refutations above are the confirmed results of this subsection.]**

**What the second half is remains OPEN.** Concrete next step: section 11.3 found that
part records carry their own 64-byte reference records at `+0xC0`/`+0xC4`, remapped
through a per-vehicle table at load time, and that mechanism's domain is unconfirmed.
The natural test this motivates is whether the *set of values ever written* by that
remap, collected across a vehicle's parts, coincides with the unreferenced material
index range `[k, materialCount)` -- i.e. whether the second half is selected
per-part or per-customization-slot at runtime (a paint layer, a damage stage, or an
LOD choice) rather than baked into the file as a fixed pairing. That test was not run
this pass; it requires resolving the 64-byte record's own `+0x00` "kind" field first
(section 11.3's own open item), and the two are now linked rather than independent
unknowns.

### 11.5 Summary

| Question | Status |
|---|---|
| Does draw-range `high16` index the material set? | **HIGH CONFIDENCE -- refuted** (dose-response flat against materialCount 3-102; global cap of 14) |
| Does draw-range `high16` index the part list? | **REFUTED** -- 12/393 vehicles have `max(high16) >= partCount`, a real failing case |
| Does draw-range `high16` equal the draw range's own group index? | **REFUTED** -- chance-level match (3.14% vs 2.31% shuffled control), 0/393 exact |
| What does `high16` (range 0-14, population-wide) actually enumerate? | ~~**OPEN**~~ **RESOLVED 2026-09-28: a vertex-channel index (disassembly + full-population zero-exception join, see §11.1's own updated text above and `spec-render-pipeline.md` §16.4/§18.6)** |
| Does the per-material record encode its own identity/association? | **REFUTED** -- no such field exists; identity is positional |
| Is per-part material scoping established? | **Still OPEN**, but two of the eight pointer fields now have a disassembly-confirmed shape (section 11.3) |
| Does the vehicle 2x material pairing correspond to an aligned duplicate? | **REFUTED** -- 10.6% shaderHash match, 12.2% exact texture-name match, both far below a real pairing |
| Does it correspond to a permuted (same-multiset) duplicate? | **REFUTED** -- 0/351 vehicles |
| What is the unreferenced half, if not a duplicate? | **OPEN** -- next step given in section 11.4 |

**⚠ Amendment, 2026-09-12 (second pass) -- added rows, table above left as published.**
Sections 11.6-11.8 below settle two more rows on the same evidentiary standard as this
table (feasibility + dose-response, or empirical + shuffled control) rather than leaving
them as open guesses:

| Question | Status |
|---|---|
| Does draw-range `high16` track a per-vehicle part-TYPE taxonomy (the task's own candidate)? | **REFUTED** -- passes feasibility (max 13 distinct types/vehicle, cap 15) but flat dose-response (mean 6-11 across distinctTypes 1-13), the same shape as the materialCount refutation (§11.6) |
| Is the high half read/tested anywhere in the load-time construction chain? | ~~**REFUTED, as a real negative** -- zero `SHR/SAR 0x10` and zero `CMP` against 14/15 in the entire named neighbourhood (§11.6)~~ **NARROWED 2026-09-28 -- that negative was scoped to `SHR/SAR`+`CMP`-shaped code, a real instruction-form blind spot (same class already on record, `HANDOFF.md` §27.4): `spec-render-pipeline.md` §16.4 independently found a render-time (not load-time) consumer, `FUN_00e58480`, that reads this exact field via a direct sub-field load (a `u16` at the packed dword's `+0x2` byte -- byte-identical, little-endian, to "the high 16 bits", so almost certainly the same field) rather than a shift+compare. Team B cross-checked the value-range match independently from real data (this section's own 90,290-draw-range population: max 14 = 15 values = the render-pipeline side's independently-derived "≤15-valued selector") before either side had compared notes -- two methods, same byte offset, same value width. Does not resolve what the field means (still OPEN below) *(resolved separately: vertex-channel index, row above / §11.1)*; does confirm `FUN_00e58480` is reading the real field, not a lookalike. `this+0x130`'s vtable+`0x1c` target -- what actually consumes the selector after `FUN_00e58480` forwards it -- is the open next hop, being pursued in `spec-render-pipeline.md`'s own follow-up passes, not re-derived here.** |
| Do the +0xC0/+0xC4 remap's written values coincide with the unreferenced material range `[k, materialCount)` -- the sharpest test named in `HANDOFF.md` §27.2 item 5? | **REFUTED, disassembly + empirical.** The remap resolves through a generic deferred request-registration mechanism, not a material-index lookup (domain mismatch, §11.7); the on-disk pre-remap value also lands in `[k, materialCount)` at chance rate (10.4% real vs 10.5% shuffled control, n=6,727). **This settles per-part scoping and the 2x pairing's identity as independent questions, not as one still open** |

New harnesses: `veh_assoc.py`, `veh_pairing.py` (both `tools/harnesses/`, run from that
directory). New Ghidra scripts: `VehPartRefs.java` (`tools/scripts/`, output
`scratchpad/veh/partrefs.txt`); this session also re-ran the pre-existing
`VehMatHelper.java` (output already on disk at `scratchpad/veh/mathelper.txt`,
produced by an earlier pass) and `DecompileCarCtor.java` (output
`scratchpad/veh/ctor_full.txt`, superseding the truncated `tools/car_ctor.txt`). Second
pass (2026-09-12): Ghidra scripts `Veh2CallSiteRaw.java`, `Veh2ResolveChain.java`,
`Veh2HighHalf.java` (outputs `scratchpad/veh/callsite_raw.txt`, `resolve_chain.txt`,
`highhalf.txt`); harness `veh2_kind.py` (`tools/harnesses/`).

### 11.6 What the 0-14 cap on the packed high half enumerates -- still OPEN, a fourth candidate refuted, and the search space narrowed

*(Superseded 2026-09-28: `high16` is a vertex-channel index, §11.1/§11.5, so `N = max(high16)+1` is bounded by the vehicle's own channel count, 2–15. The enumeration question below is answered by that; what the `3a+b` factoring reflects in channel-count terms was not re-examined.)*

**Restated precisely, so the next session does not re-derive it:** the packed
draw-range id's high half is capped at 14 (15 values, 0-14) across the full
90,290-draw-range population, independent of `materialCount` (3-102),
`partCount` (2-43) and this vehicle's own group count -- established in section
11.1. That independence is the whole reason a *sixth* candidate is worth
checking rather than another per-file count.

**Restricted-neighbourhood search for a consumer, per the task's own framing
("not a whole-binary scan -- start from a named entry point... and read
outward").** Every function already on file as part of the mesh/vehicle
draw-chain neighbourhood -- the nested "Mesh" sub-block parser and its channel
sibling (`FUN_00e71410`, `FUN_00e71740`, both decompiled in full this session
for the first time), the shared geometry-loader chain (`FUN_00751f60` →
`FUN_007527b0` → `FUN_007524f0` → `FUN_007522f0`), the vehicle constructor
chain (`FUN_00ab1080`, `FUN_00aaf7c0`, `FUN_00aaf690`, `FUN_00ab0ef0`), and the
shared per-material helper (`FUN_00e71090`) -- was scanned for two things: any
`SHR`/`SAR` by `0x10` (the idiom that extracts a packed high half) and any
`CMP` against `0xd`/`0xe`/`0xf` (14/15, the observed cap). **Zero hits, in
every one of these eleven functions.** (`Veh2HighHalf.java`, output
`scratchpad/veh/highhalf.txt`.)

**[CONFIRMED — disassembly, a real negative, stated as a predicate rather than
a conclusion per `HANDOFF.md` §5's rule on negatives:** no instruction in this
named neighbourhood extracts or bounds-checks the packed id's high half. This
does not show the value is unused -- it shows its consumer, if one exists, is
not reachable from this load-time construction chain. That is consistent with,
and now directly extends to vehicles, this project's standing finding that the
renderer wraps Direct3D behind its own runtime-built abstraction with zero
static vtable calls to `CreateVertexDeclaration`/`DrawIndexedPrimitive`
(`HANDOFF.md` §5, §26) — there is no reason to expect a draw-range field to be
interpreted anywhere *before* that abstraction, and every function read here
sits before it.**

**The task's own named candidate, tested data-side instead of by more
disassembly:** vehicle part categories (chassis, wheel, door, …) as "a fixed
small taxonomy." The part-record TYPE enum (`+0x44`, §4.1) is the obvious
concrete form of this. New harness `veh2_kind.py` (`tools/harnesses/`),
population 393/393 vehicles, 9,951 part records (matching §4's own figure).

- **Feasibility:** the RAW type enum is not itself a 15-slot space --
  **26 distinct values occur somewhere in the population** (every value
  1-26). But no single vehicle uses anywhere near that many at once: the
  **maximum distinct-type count observed in any one vehicle is 13**
  (histogram: 1→9, 2→6, 3→24, 4→9, 5→33, 6→31, 7→30, 8→130, 9→57, 10→45,
  11→16, 12→1, 13→2 vehicles), comfortably under a 15-slot cap and, unlike the
  materialCount check this refutes in the same section, with real headroom
  rather than a near-vacuous margin. **0/393 vehicles violate a ≤15 bound.**
  **[CONFIRMED — empirical, feasibility only — this alone proves nothing, per
  the house rule below.]**
- **Dose-response, the test that actually decides it:** binned by each
  vehicle's own distinct-type count, that vehicle's own `max(high16)` does
  **not** climb with it. Mean sits in a flat 6.1-11.0 band across
  distinctTypes 1 through 13, and the observed maximum sits at 11-14
  throughout with no trend (distinctTypes=1: mean 9.33, max 11 · distinctTypes=6: mean
  7.81, max 14 · distinctTypes=9: mean 10.68, max 14 · distinctTypes=13: mean
  11.00, max 11). **[REFUTED — CONFIRMED, empirical.]** This is the identical
  shape, and the identical standard, applied to the materialCount refutation
  in section 11.1: a candidate that passes feasibility but shows no
  dose-response is not carrying the relationship.

**Net position, four candidates deep:** the part list (refuted, feasibility),
the range's own group index (refuted, chance-level with control), the material
set (refuted, no dose-response), and now the part-type taxonomy (refuted, no
dose-response) are all down. Combined with the clean negative above, the cap's
consumer is not in this file's own declared counts and is not read anywhere in
this asset's load-time code. **[OPEN — the enumeration itself.]** **[Resolved 2026-09-28: vertex-channel index, §11.1.]** **Concrete
next step, sharper than "read more disassembly here":** the consumer, if it
exists, must be found from the *data* side of the runtime abstraction rather
than the construction side — enumerate every cross-reference to the
`0x424BD00D` sub-header magic and to the mesh-header group-count field's own
byte pattern (stride `0x30` records immediately followed by stride `0x14`
records) across the **whole binary**, restricted to that specific structural
signature rather than a small-displacement predicate (the search shape this
project's own methodology notes rule out, `HANDOFF.md` §5/§26.12/§26.15) — i.e.
find who else, besides this load-time chain, ever touches the *same loaded
memory* the group/range array occupies. That is a materially larger search
than this session's scope and is recorded as the honest next step rather than
attempted here.

> **▶ EXECUTED 2026-09-13 — read §11.9 for the result.** The anchor is Mesh header `+0x08` (`FUN_00e71310` is the group/draw-range array parser); the runtime holder class keeps it at its own `+0x30`; the cross-reference set is 10 call sites, and the negative above extends to a controlled **168-function** closure with **zero** high-half extractions. The search did find the engine's own packed 16:16 draw-range handle (`FUN_00e72d00`), but a population test **refutes** identifying the file field with it, and a sixth candidate (`high16` → `g mod 3`) is refuted too. The cap is now characterised as **`3 × 5`, occasionally minus one** — §11.9.8 replaces this paragraph's next step with a sharper one.

### 11.7 The +0xC0/+0xC4 remap traced to its actual endpoint: a deferred request-registration queue, not a material-index lookup

This is the sharpest remaining test named explicitly in `HANDOFF.md` §27.2
item 5: whether the values the part record's `+0xC0`/`+0xC4` mechanism writes
ever coincide with the unreferenced material range `[k, materialCount)`.
**Stated precisely before running anything, per the house rule:** a
**positive** result (a real, above-chance concentration in that range) would
mean the mechanism selects per-part *into* the unreferenced half at runtime —
settling per-part material scoping and the 2× pairing's identity in the same
move, exactly as `HANDOFF.md` frames it. A **negative** result would mean the
two questions are genuinely independent — this specific mechanism does not
carry the association, whatever else might.

**The mechanism, read two levels deeper than the existing table
(`FUN_00ab0ef0`, `FUN_007616c0`, and — new this session — their own callees
`FUN_00c960c0` and `FUN_00760c30`).** The calling convention was checked
against raw disassembly rather than trusted from the decompiler's variable
names alone, because a plausible-looking substitution is exactly the failure
mode `HANDOFF.md` §5 records as this component's worst published error
(`Veh2CallSiteRaw.java`, `scratchpad/veh/callsite_raw.txt`). Confirmed: the
existing table's 4th/5th arguments (part-array base, `partCount`) are
correct as published. **New:** the `this` argument (register `ECX` at the call site) is
a pointer to a **previously undocumented outer-header field at file offset
`+0x1C`** — this spec's §3 table jumps from `+0x18` straight to `+0x20`; that
gap was a real gap, not an oversight elsewhere. It is fixed up **lazily,
inside `FUN_00ab0ef0` itself**, using the same `-1`⇒null / else `+=fileBase`
idiom every other outer-header offset uses, rather than eagerly at ctor entry
with the rest. **[CONFIRMED — disassembly.]**

That `+0x1C` structure is the actual *source* for the "100-entry remap table"
(the 100-entry table), read via `FUN_007616c0` (re-decompiled in full this session —
the earlier pass had truncated 76 of its ~340 lines, `scratchpad/veh/resolve_chain.txt`):

- gated on `(structure+0x4) >= 7` (a version/size floor);
- a count at `structure+0xC`, used **both** as the loop bound — hard-capped at
  the caller's requested 100, the same 100 the existing spec table already
  names — **and** handed whole to a dictionary/hashmap constructor;
- a chunked-list array of 16-byte-stride elements at `structure+0x10`, walked
  by the **same generic chunked-list pointer-fixup utility** already
  characterised in section 11.3 (`FUN_0075f880`, read in full: it fixes up
  three embedded pointer fields per 16-byte element and advances a
  linked-list cursor — generic, not domain-specific, exactly as before).

**Per entry, the code does not perform a numeric lookup.** It (a) builds or
finds a dictionary/hashmap object gated on an internal type-tag field reading
exactly `9`; (b) calls a **virtual method** at that object's own vtable slot
`+0x14` with `(index, buffer)` — "get the key *at this position*", not "get
the value *for this key*" — where `buffer` is **524 bytes**, a size far more
consistent with a name/string than with any numeric index; then (c) hands
that result to `FUN_00760c30`.

**`FUN_00760c30` (202 bytes, read in full) does not compute or return a
designed value.** It writes its four inputs, plus eight further caller-stack
dwords, into a **fixed-capacity global table of 5,000 slots**
(`DAT_0163a8f0`/`DAT_0163f710`), silently declining once past slot 4,999; it
invokes a **virtual call at vtable slot `+0x1C`** on its own first argument
(the shape of "register a pending reference", not "compute an answer"); and
its **last instruction before `RET` is `MOV EAX,EDI`, where `EDI` is never
assigned anywhere in this function's body** — it returns whatever its caller
happened to leave in `EDI`, not a value this function designed.
**[CONFIRMED — disassembly; raw tail in `scratchpad/veh/highhalf.txt`.]**

**So the value eventually written into the 64-byte reference record's own
`+0x04` field — the 100-entry table's entry at the original value's index — is drawn from the domain of this
global request-registration table, or is an incidental register value; it is
not derived anywhere in this chain from the vehicle's own material-set array,
its count, or `materialCount`.** This is a disassembly-grounded reason the two
quantities belong to different domains, established *before* any count was
taken — the domain-mismatch check the task's own house rule asks for, not an
assumption smuggled in past it.

**Empirical confirmation, run anyway** — a mechanism argument is not a
substitute for the data, and this project has been burned before by an
argument that sounded sufficient on its own (`HANDOFF.md` §5, the 2× pairing's
own "mechanical explanation ≠ revived approach" lesson). If the domain-mismatch
reading is right, the file's own **pre-remap** `+0x04` value — the raw byte
content the loader reads before any of this machinery runs — should show no
above-chance relationship to `[k, materialCount)`; a real concentration there
would have meant the *authoring* tool independently encoded a material-scoped
value that the runtime chain above then overwrites or ignores, which would
have been a genuine surprise worth chasing further. New harness `veh2_kind.py`.

| | |
|---|---|
| 64-byte reference records read | 7,579, across 393/393 vehicles, 9,951 part records |
| — with a non-null (`≠ -1`) pre-remap `+0x04` | 6,727 |
| pre-remap `+0x04` range (non-null) | 1 … 34 |
| land in `[k, materialCount)` | **702 / 6,727 (10.4%)** |
| **shuffled control** (each vehicle's own values re-tested against a random *other* vehicle's `(k, materialCount)` pair, same 393-vehicle population) | **705 / 6,727 (10.5%)** |

**Real and control are indistinguishable. [REFUTED — CONFIRMED, empirical,
with control.]**

**This settles the sharpest test named in `HANDOFF.md` §27.2 item 5, as a
negative, on two independent grounds (mechanism and measurement) rather than
one:** per-part material scoping is not carried by the `+0xC0`/`+0xC4`
mechanism, and it does not explain the 2× pairing's unreferenced half either.
**The two questions remain genuinely independent** — not because the test was
inconclusive, but because the specific mechanism it targets is now shown, by
disassembly and then confirmed empirically, to operate in an unrelated domain.
A well-characterised negative is a real result, and this component has
several; this is another one, recorded rather than left as a loose end.

*(A loose, uncontrolled observation noticed while building the example table
above and flagged as exactly that, not promoted: in the ten examples pulled
for manual inspection, the pre-remap value equalled that reference's **own
part-record index** more often than seemed like nothing — 657 / 6,727 (9.8%)
population-wide, no control run. Given the population-wide rate is close to
the chance-level overlap already measured above, this is recorded as a
curiosity, not a finding — it would need its own shuffled control before it
means anything, and that control was not built this pass.)*

### 11.8 The 64-byte reference record's `+0x00` "kind" field (task 3)

Population: 7,579 reference records, 393/393 vehicles, same harness
(`veh2_kind.py`).

**Distribution:** `kind == 0` in **7,536 / 7,579 (99.4%)**. Every other value
is rare (at most 7 occurrences) and clusters in bands rather than spreading
uniformly: `{1:7, 2:5, 3:5, 4:7, 20:1, 21:2, 50:2, 51:2, 70:1, 71:1, 72:1,
73:2, 75:1, 135:2, 136:3, 137:1}`. **[CONFIRMED — empirical.]** The banding
(0-4, 20-21/50-51, 70-75, 135-137) is more consistent with a small enumerated
kind/type tag than a hash or a free-running counter. **[HYPOTHESIS —
unconfirmed]** what each band denotes; not pursued further this pass.

**The disassembly-derived "unresolved reference" predicate reproduces exactly
on real files.** Section 11.3 established that `FUN_00ab0d20` (the
"unresolved reference" reporter) fires when a record's `+0x04 == -1` **and**
`+0x00 == 0`. Measured directly: of the **852** null (`+0x04 == -1`) records,
`kind == 0` in **852 / 852 (100.0%)**; conversely, every one of the **43**
non-zero-kind records has a non-null `+0x04` (**43 / 43**). **[CONFIRMED —
empirical, exact match to the disassembly-derived predicate.]** `kind == 0` is
necessary but not sufficient for "unresolved" — the large majority of
`kind == 0` records (6,684 / 7,536) **do** resolve to a non-null value.

**Correlation against sections 11.6-11.7:** none shown. The "unresolved"
pattern is confined entirely to `kind == 0`, and section 11.7's chance-level
overlap result holds regardless of `kind` (the 6,727-record denominator there
is not stratified by `kind` because there was nothing in the disassembly or
the data to suggest it should be). The kind field identifies a different axis
— which of several reference domains a 64-byte record belongs to, of which
`kind == 0` is overwhelmingly the common case — than either the packed-id cap
(section 11.6) or the pairing/scoping question (section 11.7). **[Formerly
OPEN, resolved (negative) in section 11.11: the concrete next probe this entry
named, `FUN_00ab0a10`, was read in full and turned out not to be part of the
64-byte record machinery at all -- it is the fallback resolver for an unrelated
single per-part field. What the non-zero bands denote is still OPEN; see 11.11
for exactly what was ruled out and what remains untried.]**

### 11.9 Section 11.6's next step executed: the runtime anchor found, the engine's own 16:16 draw-range handle decoded, and two more candidates refuted

Section 11.6 left a named concrete next step: find the consumer from the **data**
side -- locate where the loaded (post-construction, in-memory) group/draw-range
array is anchored on the runtime object, enumerate every cross-reference to that
anchor, and check each one outside the eleven already-excluded load-chain
functions for a high-half extraction used as a bounded small index. That step was
executed in full. It produced a large amount of confirmed structure, one genuine
consumer-side hit, and two further refutations. Outputs:
`scratchpad/veh/anchor.txt`, `consumers.txt`, `render.txt`, `vtbl.txt`,
`slot4b.txt`, `deref.txt`, `narrow.txt`, `drawsig.txt`, `hit.txt` (Ghidra scripts
`Veh3Anchor.java`, `Veh3Consumers.java`, `Veh3Render.java`, `Veh3Vtbl.java`,
`Veh3Slot4.java`/`Veh3Slot4b.java`, `Veh3Deref.java`, `Veh3Narrow.java`,
`Veh3DrawSig.java`, `Veh3Hit.java`) and `scratchpad/veh/handle.txt`, `mod3.txt`
(harnesses `veh3_handle.py`, `veh3_mod3.py`).

**The predicate, written before any search ran**, per `HANDOFF.md` section 5's rule
on negatives: a POSITIVE is a function outside the eleven that reaches the
runtime anchor and, on a dword loaded from a `0x14`-strided record, extracts the
high 16 bits and uses the result as a bounded small index (jump table, global
array base + index*stride, comparison against 13/14/15). A NEGATIVE is that no
such function exists. No zero was to be believed without first showing the same
search technique recovers known positives, and without a whole-binary count of
every instruction form the predicate relies on -- the correction recorded in
`HANDOFF.md` section 5, where a `CALL [reg+disp]` census undercounted virtual
calls 60x and read as a clean confirming negative.

#### 11.9.1 Step 1 answered: the anchor is `FUN_00e71310`, and it is the group/draw-range array parser

`FUN_00e71310` -- previously on file only as "helper called from the Mesh
sub-block parser after the channel array, takes an implicit register-passed
argument, OPEN" -- **is the group and draw-range array parser.** Read from raw
disassembly at the call site `00e7156e`-`00e71590` inside `FUN_00e71410`: the call
passes the file buffer base and the shared parse cursor, together with
`Mesh header + 0x38` as a third argument, while the register that will hold the
call's return value is separately loaded beforehand with `Mesh header +0x04` (the
group count). The call target is `0x00e71310`; its return value is stored at
`Mesh header +0x08` (the group array base), and `Mesh header +0x0C` is zeroed
alongside it, forming the 8-byte pointer slot. Afterward, a null group-array
pointer is only accepted when the group count is zero -- if the count is non-zero
the pointer must be non-null -- and either check failing routes to a parse failure.

**[CONFIRMED -- disassembly.] The runtime anchor asked for by section 11.6's step 1
is Mesh header `+0x08`/`+0x0C`** -- an 8-byte pointer slot holding the group array
base, gated by the group count at `+0x04`. `FUN_00e71310` returns it.

Its body then yields the **full `0x30`-byte group record**, which this document
previously knew only by its first field:

| Offset | Size | Meaning | Evidence |
|---|---|---|---|
| `+0x00` | 4 | **draw-range count** for this group | already on file (section 8.2 of `spec-vertex-format.md`); re-derived here from the loader's own `count * 0x14` and `count * 8` arithmetic |
| `+0x04` | 12 | **vec3 A** -- bounding-box minimum | `FUN_007524f0` copies `+0x04..+0x0F` and `+0x10..+0x1B` into a 24-byte struct and calls `FUN_00749230`, which computes `(B-A)*0.5` as half-extents, `A + halfExtent` as centre, and `SQRT(hx²+hy²+hz²)` as a radius -- an AABB-to-sphere conversion. `FUN_0074d260` does the same via `FUN_0042e160(&grp+0x04, &grp+0x10)`, a plain two-vec3 copy |
| `+0x10` | 12 | **vec3 B** -- bounding-box maximum | as above |
| `+0x1C` | 4 | **unknown** -- the only undetermined dword in the record | -- |
| `+0x20` | 8 | **pointer to this group's `0x14`-byte draw-range array**, fixed up at load (`+0x24` zeroed) | `FUN_00e71310` pass 1: `*(grp+0x20) = cursor + base; cursor += grp[+0x00] * 0x14` |
| `+0x28` | 8 | **pointer to an array of `rangeCount` 8-byte slots**, present only when Mesh header `+0x48` is non-zero, else zeroed (`+0x2C` zeroed) | `FUN_00e71310` pass 2 |

**[CONFIRMED -- disassembly.]** The `+0x28` array's own contents are *generated at
load*, not read: each 8-byte slot's first dword is written as
`meshHeader[+0x50] + (u32 read from the file at that slot) * 2` -- a pre-resolved
absolute pointer into the `u16` array whose count sits at Mesh header `+0x48`. So
the loader hands the renderer one ready-made pointer per draw range into that
`u16` table. **[OPEN -- what the `+0x48`/`+0x50` `u16` table holds.]**

The two passes also settle the array *order*, independently reproducing what
`tools/harnesses/batch_array.py` had assumed: all groups' `0x14`-byte range arrays
are laid down contiguously first, and only then all groups' 8-byte slot arrays --
not interleaved per group. **[CONFIRMED -- disassembly, agreeing with the existing
harness.]**

**The structural fact that reframes the whole question:** every pointer here is
fixed up **in place, inside the loaded file buffer** (`ptr = fileOffset + base`).
There is no separate runtime copy of the group or draw-range arrays. So "the same
loaded memory the group/range array occupies" *is* the file buffer, reached
through whatever object holds the Mesh-header pointer -- which is what the rest of
this section enumerates.

#### 11.9.2 The runtime class that holds the Mesh header, and its field `+0x30`

`FUN_00e71410` returns the Mesh-header pointer to exactly **two** callers
(`getReferencesTo`, CONTROL A below): `FUN_00e718b0` (the known 8-caller generic
wrapper) and `FUN_00e71910` (3 callers). Both pass it out in `EAX`, giving
**10 distinct call sites** that store a loaded Mesh header somewhere. At several of
them the pointer is immediately handed to a pooled renderer object: the code calls
the class-specific pool allocator (`0x00482de0`, a thunk to `FUN_00482d20`) to
obtain a new instance, then invokes that instance's own virtual method at vtable
slot `+0x10`, passing the Mesh header pointer and a literal `0`.

That class is now identified. `FUN_00482e00`, the constructor run on the popped
free-list node, installs `PTR_FUN_012a0418`; the pool's globals are
`DAT_01353a00` (free-list head), `DAT_013539f8` (live count), `DAT_013539fc`
(peak), `DAT_01353a04`/`DAT_01353a05` (overflow flags). The vtable has **six**
slots:

| Slot | Target | Role |
|---|---|---|
| `+0x00` | `FUN_00482e70` | destructor-with-delete-flag (calls `FUN_00482e90`) |
| `+0x04` | `FUN_00476fe0` | empty stub (`RET`) |
| `+0x08` | `FUN_00482d90` | release -- returns the node to the pool |
| `+0x0C` | `FUN_00482d80` | returns the constant `0x7f903b60` (a type tag) |
| `+0x10` | `FUN_00483110` | **initialise from a Mesh header** |
| `+0x14` | `FUN_00483140` | **re-upload the vertex channels** |

**The class's field `+0x30` holds the Mesh-header pointer, and `+0x48` holds the
created index-buffer object.** Proof, from `FUN_00482f00` (called out of slot
`+0x10`) and `FUN_00483140` (slot `+0x14`), both of which
return immediately when the pointer at `this+0x30` is null:

- **vertex channels:** count from `meshHeader+0x10`, array from `meshHeader+0x18`,
  stride `0x18`; per channel, create a buffer through the device wrapper
  `DAT_03171ac4`'s vtable slot **`+0x68`**, store the handle in the channel record's
  own `+0x08`, then `Lock` (buffer vtable `+0x2c`), `memcpy` from the channel
  record's `+0x10` data pointer, `Unlock` (`+0x30`).
- **index buffer:** `if (meshHeader[+0x20] != 0)`, create through the device
  wrapper's slot **`+0x6c`** with length `meshHeader[+0x30] * meshHeader[+0x20]`
  and a format value computed by comparing the element-size byte at `meshHeader+0x30`
against 2 and adding the character `'e'` to that comparison's boolean result --
i.e. **`0x65` when the
  element size is 2 and `0x66` otherwise**, which are `D3DFMT_INDEX16` (101) and
  `D3DFMT_INDEX32` (102). The handle goes to `this+0x48`; then `Lock`, `memcpy`
  from `meshHeader[+0x28]`, `Unlock`.

**[CONFIRMED -- disassembly.]** Two things follow. First, `spec-vertex-format.md`
section 8 already established that Mesh header `+0x20`/`+0x28`/`+0x30` is the
**index buffer** (count / data pointer / element size) rather than a vertex buffer,
from element-size and max-index evidence; the `D3DFMT_INDEX16`/`D3DFMT_INDEX32`
constant here **confirms it a second, independent way, from the consumer**, and is
the stronger form of the proof. Second, and this *is* a correction: the surviving
hypothesis that `+0x48`/`+0x50` is "a cleaner, more direct match for a plain
16-bit index buffer" (`spec-geometry-format.md` section 4.1.1, and the plate
comment on `FUN_00e71410`) is **REFUTED** -- the index buffer is created from
`+0x20`/`+0x28`/`+0x30`, and `+0x48`/`+0x50` is a separate `u16` table, the one the
group record's `+0x28` slots hold pre-resolved pointers into. The `D3DFMT_INDEX16`/`INDEX32` constant is
what makes this a proof rather than an inference, and the per-channel vertex data
living at channel record `+0x10` with its own buffer at channel record `+0x08` is
consistent with `spec-vertex-format.md`'s channel table throughout.

Incidentally this also names, for the first time in this project, the renderer's
own device abstraction that `HANDOFF.md` section 5 and section 26 describe as
wrapping Direct3D: the singleton `DAT_03171ac4`, vtable slot `+0x68` = create
vertex buffer, `+0x6c` = create index buffer; and on the returned buffer objects,
`+0x2c` = Lock, `+0x30` = Unlock, `+0x08` = release.

#### 11.9.3 Step 2: the full cross-reference set, and where each call site puts the pointer

| Caller | Site | What it does with the returned Mesh header |
|---|---|---|
| `FUN_0043e420` | `0043e774` | stores into an 8-byte slot at `[[EBP+0xc]+0x48] + EDI + 0x58`; also carries the pool allocator inlined |
| `FUN_007524f0` | `00752672` | keeps it in `EBX`; allocates the renderer object into `[EDI+0x50]`, calls slot `+0x10`, then reads `meshHeader+0x08` and copies **group[0]** `+0x04`/`+0x10` into `FUN_00749230` |
| `FUN_0074d260` | `0074d314` | tests `meshHeader+0x04` (group count) > 0, resolves a material via `FUN_00e401d0`/`FUN_00e71090`, allocates the renderer object, calls slot `+0x10`, then `FUN_0042e160(grp+0x04, grp+0x10)` -> `FUN_00749230` |
| `FUN_007512f0` | `0075137b` | tests `meshHeader+0x04` > 0, same material path |
| `FUN_00864c60` | `00864f26` | tests `meshHeader+0x04` > 0, allocates the renderer object, calls slot `+0x10` |
| `FUN_00e3c4e0` | `00e3c58a` | stores into `[ESI]` (8-byte slot), allocates the renderer object into `[ESI+0x8]`, calls slot `+0x10` |
| `FUN_00e7a5a0` | `00e7a65c` | stores into `[[ESI+0x38] + EBP]` -- the per-LOD-slot array already documented for `.csrt_pc` trees |
| `FUN_007499d0` | `00749a71` | stores into `[EBP + EBX*8]` -- an 8-byte-slot array indexed by a loop counter bounded by `[EDI+0x48]` |
| `FUN_00747b50` | `00747b79` | stores into `[ESI+0x8]`, then allocates the renderer object into `[ESI+0x8]`'s neighbour |
| `FUN_00e3c4e0` | `00e3c56d` | the inline (`FUN_00e71910`) variant of the same |

Only **two** of the ten read the group array at all, and both only to take
**group[0]**'s two vec3s for a bounding volume. **No call site walks the draw
ranges.**

**Rows 4/5 (`FUN_007512f0`/`FUN_00864c60`) identified, 2026-09-30 — both are genuine `.czn_pc`
zone-side consumers, completing this table's domain attribution to 10/10** (the other 8 rows
were already attributed elsewhere: `FUN_0043e420` = Effects, `FUN_007524f0`/`FUN_0074d260`/
`FUN_00e3c4e0` = vehicle-material, `FUN_007499d0`/`FUN_00747b50` = physics (`spec-physics-format.md`),
`FUN_00e7a5a0` = trees, the inline `FUN_00e71910` variant = foliage). `FUN_00864c60` handles
top-level `.czn_pc` record id `0x2237` ("Region", the exact id `spec-terrain-format.md` §3's own
real sample already found preceding readable `"Region<003>"`/`"Region"` strings); `FUN_007512f0`
handles id `0x2251`, reached via `FUN_00864000` (which calls it at `0x00864081`). Both are called
from one master dispatcher, `FUN_008652d0`, whose sole caller `FUN_00866580` sits in the same
tight `0x863000`–`0x866000` code module as the already-documented `.czh_pc` header parser
(`FUN_00863080`, `HANDOFF.md` §2a `CTORLESS_TYPES`). This directly corrects
`spec-zone-data-format.md` §9.1/§9.2's own "no top-level `.czn_pc` walker found anywhere"
negative — the walker exists, compiled with a masking idiom (`& 0x80000003`, a branching
round-up rather than a literal AND-mask, `& 0x7fffffff`) that census's own predicate never
covered; see that document's own correction for the full account. **[CONFIRMED — disassembly,
full chain traced from `FUN_00866580` down through both zone consumers and their tag-match
reads (`DAT_013026f4 = 0x2237`, `DAT_0130279c = 0x2251`, both real reads against the on-disk
`{id (high bit set), length}` chain, `spec-zone-data-format.md` §6).]** **Note on this row's own scope, added after a peer flagged it 2026-09-30:** everything stated above about these two rows is derived purely from disassembling the game's own executable code (which functions call which, and the literal tag constants `0x2237`/`0x2251` embedded in that code) — none of it required reading any real `.czn_pc` file's own content. The separate, more detailed field-level record layout (`{meshRefCount,...}` etc.) is flagged as provisional pending a scope question about the PARKED `.czn_pc` interior — see `spec-terrain-format.md` §5 open item 2's own pending-scope note for the full account; this row's own claims are not affected by that question.

#### 11.9.4 Step 3: the negative, with its controls

**CONTROL A (does the xref technique work at all?).** `getReferencesTo` recovered
2 callers of `FUN_00e71410`, 1 of `FUN_00e71310`, 8 of `FUN_00e718b0`, 3 of
`FUN_00e71910`, 2 data references to the vtable constant, and 8/7/6/8/3 references
to the five pool globals -- every one of them a known-correct edge. **The
technique finds hits.**

**CONTROL B (are the instruction forms ones this compiler actually emits?).**
Whole binary, 3,546,397 instructions: high-half extraction
(`SHR`/`SAR` by `0x10`, or `AND 0xffff0000`) **999** sites; `0x14`-stride
arithmetic (`IMUL`/`LEA`/`ADD`/`SUB` with `0x14`) **14,318**; comparison against
`0xd`/`0xe`/`0xf` **1,361**; 8,249 functions carry at least one. **Every form is
abundant, so a zero from a restricted search is interpretable.**

**CONTROL C (can the switch detector see switches?).** **1,345** computed-jump
switches with 2+ targets binary-wide, of which **10 have exactly 15 targets and 6
have exactly 16** -- `00403b3d`, `00416e52`, `004dac4d`, `004e83ec`, `00b02003`,
`00b0a702`, `00b3b22f`, `00bd2f66`, `00c21d56`, `00c4e3d4`, `00c52659`,
`00ce82c9`, `00e01c10`, `00e07311`, `00e0c320`, `00fc2723`. **None is in the mesh
(`0xe7xxxx`) or vehicle (`0xaaxxxx`/`0xabxxxx`) modules, and none sits in a
function that also extracts a high half.** That closes the "find the 15-slot table
before finding the index" half of the task with a negative.

**PREDICATE REPAIR, before trusting any stride-based zero.** The `0x14`-stride
predicate above misses the MSVC idiom for a 20-byte stride, `LEA r,[r+r*4]` (x5)
followed by `SHL r,2` or an `[..r*4..]` operand; likewise `0x30` as
`LEA r,[r+r*2]` then `<<4`. Whole binary: **1,547** x5 sites (532 completing to
x20) and **3,015** x3 sites (471 completing to x48) across 2,413 functions --
a real gap, exactly the shape of the corrected D3D9 census. **27** functions carry
both such a half-stride and a high-half extraction. **This repair is what found
the hit in 11.9.5**; without it the search would have returned a false clean
negative.

**The negative itself.** With all four controls passing:

- Breadth-first scan of the entire static call graph reachable from all ten
  Mesh-header call sites plus `FUN_00e718b0`/`FUN_00e71910`/`FUN_00e71410`/
  `FUN_00e71310`, to depth 3: **168 functions, zero high-half extractions** of any
  draw-range field. The six high-half instructions found in that closure are
  `_rand` (a fixed-point RNG), `FUN_00dab2b0` (already documented as a Thomas
  Wang integer mixer) and `FUN_00c1f540` (a fixed-point math helper).
- Breadth-first scan from all six slots of the renderer class's vtable, depth 4:
  **31 functions, zero.** That closure contains **14 virtual call sites**, so the
  static graph is genuinely blind at those edges -- recorded rather than ignored.
- Every function in the renderer class's own translation unit, `0x00482c00`-
  `0x00483600`: **20 functions, zero high-half extractions**; three carry a single
  `0x14`-stride instruction each.

**[CONFIRMED -- a real negative, controlled: nothing reachable from the runtime
anchor, and nothing in the renderer class that owns it, extracts the packed draw
range's high half.]** This extends section 11.6's eleven-function negative by an
order of magnitude and, unlike it, is bounded by the actual data-flow of the
anchor rather than by a hand-picked neighbourhood.

**Two searches that failed for lack of selectivity, recorded so they are not
retried.** (a) A register-tracked two-step dereference `MOV rX,[rY+0x30]` then
`MOV rZ,[rX+0x08]` -- the literal anchor path -- gives **51 hits in 44 functions**;
its controls `(0x30,0x10)` and `(0x30,0x18)` give 15 and 27 hits and do recover
`FUN_00483140`, so the technique works, but `+0x30` and `+0x08` are too common for
the result to mean anything. (b) The draw-range record's unique four-field
signature (loads at `+0x04`, `+0x08`, `+0x0C`, `+0x10` off one base register, the
`DrawIndexedPrimitive` argument set) funnels 3,848/2,465 -> 1,179/837 -> **384 hits
in 298 functions**, the majority with `base=ESP`, i.e. stack frames rather than
heap records. Both are the shape `HANDOFF.md` section 5 / 26.12 / 26.15 warns
about and both behaved exactly as warned.

**One honest correction to my own predicate:** `TEST reg,0xf` was initially
counted as a "bound against 15". It is not -- in `FUN_00e71310` and
`FUN_00e71740` it is the 16-byte **alignment** check. The bound counts above use
`CMP` only.

#### 11.9.5 The hit: the engine's own packed 16:16 draw-range handle, fully decoded

Three sites in the `0xe7xxxx` Mesh module -- `FUN_00e72c60`, `FUN_00e72d00`, and
`FUN_00e73e80` -- carry the exact idiom the whole search was for. `FUN_00e72d00`
in full raw form (103 bytes), reads `this+0xbc` (the mesh-resource holder pointer)
and, from that holder's own `+0x30` field, the loaded Mesh header pointer; it also
separately reads `this+0xfc` (a runtime record array pointer). The holder value is
then treated as a packed 16:16 quantity: its high half is multiplied by 5 (via a
shift-and-add sequence), used to index a `u16` field at the record array's `+0x24`
(stride `0x28`), and that `u16` is bounds-checked against Mesh header `+0x04` (the
group count) -- taking the failure branch if the index is out of range. The
bounds-checked value is then multiplied by 3 and by 16 in turn (an effective `x48`
= `0x30`, the group-record stride) and added to Mesh header `+0x08` (the group
array base) to land on one group record. That group record's own `+0x20` field
(the draw-range array) is then indexed by the packed value's low half, multiplied
by 4 and by 4 again (an effective `x16` = `0x14`, the draw-range stride): the
resulting draw-range record's `+0x0C` field is written to the function's first
output parameter, and its `+0x10` field to the second.

**[CONFIRMED -- disassembly.] This is the first function in this project that
reads the loaded draw-range array at runtime, and it reads it through exactly the
structure derived above:** `holder+0x30` -> Mesh header, `+0x04` group count as an
explicit bound, `+0x08` group array, `x0x30` group stride, group `+0x20` range
array, `x0x14` range stride, fields `+0x0C` and `+0x10`. Its two output
parameters are the draw range's **lowest and highest vertex index** --
`spec-vertex-format.md` section 8.2's `+0x0C`/`+0x10`, now confirmed from the
**consumer** side rather than from validation statistics alone. The function is a
`getVertexRange(handle, &minVertex, &maxVertex)`.

`FUN_00e72c60` (146 bytes) is the same lookup with a different tail;
`FUN_00e73e80` (448 bytes) is the same lookup followed by real work and is called
directly from `FUN_00b138a0`, itself a vtable slot (`01186c4c`). All three appear
as slots in the vtables at `01186c60`/`0125b2c0`/`0125c4e0` and `0125b2ac`/
`0125c4cc`, i.e. three related classes override the same two adjacent slots.

**So a packed 16:16 draw-range handle demonstrably exists in this engine**, and
its decoding is: **low half = draw-range index within a group (`x0x14`); high half
= a slot index into a `0x28`-byte record array at `this+0xfc`, whose `u16` at
record `+0x24` is the GROUP index** (bounds-checked against the group count).
Candidate writers of `this+0xfc` in the same module: `FUN_00e72220`,
`FUN_00e726e0`, `FUN_00e75720`, `FUN_00e7cfd0`, `FUN_00e7f120`. **[OPEN -- the
`0x28`-byte record's remaining fields and its element count.]**

#### 11.9.6 Candidate 5 REFUTED: the file's draw-range `+0x00` is not a handle of that runtime form

If the file's packed `+0x00` were a value of the kind `FUN_00e72d00` consumes,
four things must hold. `veh3_handle.py`, full population 393/393 vehicles,
90,290 draw ranges:

| Prediction | Result | |
|---|---|---|
| **P1** `low16` == the range's index within its own group | **11,802 / 90,290 (13.07%)** | **fails** |
| **P2** `low16` == the range's global index across all groups | **690 / 90,290 (0.76%)** | fails |
| **P3** per group, the `low16` multiset == `{0..cnt-1}` (a permutation) | **2,590 / 41,087 groups (6.30%)** | fails |
| **P4** every range sharing one `high16` lies in ONE group (a slot maps to one group index, so this is the sharp one) | **307 / 4,064 (vehicle, high16) pairs (7.55%)** | **fails outright** |
| **P5** `high16` values are dense from 0 per vehicle | **393 / 393 vehicles** | holds |

P4's counterexamples are flagrant, not marginal: in `car_4dr_standard03_4.ccar_pc`
(125 groups) `high16=1` appears in 41 different groups. **[REFUTED -- CONFIRMED,
empirical.]** The file field's low half is a material id, as sections 8.2/8.3
establish; the runtime handle's low half is a range index; they are not the same
value, and therefore the runtime handle's high half does not identify the file
field's high half. The mechanism in 11.9.5 stands on its own as a confirmed
finding about the engine; it does **not** answer section 11.6's question.

#### 11.9.7 Candidate 6 REFUTED: `high16` does not encode the group index's residue

P4's counterexample listing looked, on that one vehicle, startlingly clean --
`high16` in `{0,1}` -> groups `== 0 mod 3`, `{2,3}` -> `1 mod 3`, `{4,5}` ->
`2 mod 3` across 125 groups. Tested population-wide (`veh3_mod3.py`), it is not
the mechanism:

| Test | Real | Shuffled control | |
|---|---|---|---|
| `g mod 2` constant per (vehicle, `high16`) | 11.56% | 13.07% | at/below chance |
| **`g mod 3` constant per (vehicle, `high16`)** | **33.10%** (1,345/4,064) | **10.29%** (418/4,064) | 3.2x lift, but nowhere near functional |
| `g mod 4` constant | 8.61% | 9.18% | chance |
| `g mod 5` constant | 8.19% | 8.49% | chance |
| `g mod 6` constant | 8.93% | 8.37% | chance |

And with `m = N/3` (`N` = the vehicle's distinct-`high16` count), over the 324
vehicles where `N` is divisible by 3: `high16 // m == g mod 3` in
**43.39%** (32,543/75,002) and `high16 % 3 == g mod 3` in **35.22%** -- against a
~33% baseline for a three-valued target, which the shuffled control above
calibrates. Nor does `b = high16 % m` track anything: `b == low16` 10.71%,
`b == (g//3) mod m` 30.61%, `b` constant within a group 70.04%
(23,825/34,014 groups). **[REFUTED -- CONFIRMED, empirical, with control rates.]**
The single clean vehicle was an accident of that file's layout -- the same trap
`HANDOFF.md` section 5's hash-sort lesson describes, caught by the control.

#### 11.9.8 The sharpest new empirical handle, left for the next pass

*(Superseded 2026-09-28: `high16` is a vertex-channel index, §11.1/§11.5, so `N = max(high16)+1` is bounded by the vehicle's own channel count, 2–15. The enumeration question below is answered by that; what the `3a+b` factoring reflects in channel-count terms was not re-examined.)*

One fact from this pass is worth more than the refutations, and no previous
candidate test looked at it. Because `high16` is **dense from 0 in 393/393
vehicles** (P5), the enumeration is fully described by `N = max(high16) + 1`. And
`N` takes only **ten values** across the whole population:

| `N` | vehicles | | `N` | vehicles |
|---|---|---|---|---|
| 2 | 1 | | 10 | 4 |
| 3 | 7 | | 11 | 28 |
| 6 | 35 | | 12 | 113 |
| 8 | 23 | | 14 | 13 |
| 9 | 129 | | 15 | 40 |

**324 / 393 are exact multiples of three** (3, 6, 9, 12, 15), **65 / 393 sit one
below a multiple of three** (2, 8, 11, 14), and **4** are the stray `N=10`.
`N` is never 1, 4, 5, 7 or 13. Written as `3k` and `3k-1` for `k = 1..5`, the
distribution is `k=1`: 1+7, `k=2`: 0+35, `k=3`: 23+129, `k=4`: 28+113,
`k=5`: 13+40 -- i.e. **the enumeration is three sub-values per unit, with between
one and five units, and the topmost combination sometimes absent.** The cap at 14
that section 11.1 measured is therefore not "15 of something" but
**"3 x 5, occasionally minus one"**.

Two supporting descriptives: `high16` is a **single value in 27,887 / 41,087
groups** (and 2, 3, 4, 5 values in 9,134 / 3,191 / 834 / 41), so it is nearly but
not quite a per-group attribute; and `N` is uncorrelated with the group count's
own residue mod 3, so the three does not come from the group array's layout.

**Concrete next step, materially sharper than section 11.6's:** stop testing
whole-field hypotheses and **factor the field**. Test `high16 div (N/3)` and
`high16 mod (N/3)` -- a 3-valued factor and a 1-to-5-valued factor -- separately
against per-vehicle quantities, and look specifically for a vehicle property that
comes in **threes** (three damage stages? three paint/decal layers? three
material passes?) crossed with one that ranges **one to five**. The three-valued
factor is the more identifiable of the two and should be attacked first. The
disassembly side has a matching concrete target: read the `0x28`-byte record array
at `this+0xfc` (writers listed in 11.9.5) and its element count -- if that array
is also built as `3 x k`, the two halves of this section meet.

### 11.10 Section 11.9.8's factoring executed: `high16 = a*3 + b` confirmed, the fast factor is a clean fixed-3 negative, the slow factor inherits the whole field's correlation-not-index shape, and every named engine candidate for either factor is refuted

*(Superseded 2026-09-28: `high16` is a vertex-channel index, §11.1/§11.5, so `N = max(high16)+1` is bounded by the vehicle's own channel count, 2–15. The enumeration question below is answered by that; what the `3a+b` factoring reflects in channel-count terms was not re-examined.)*

Section 11.9.8 left a named next step: stop testing `high16` as one field and **factor**
it, since `N = max(high16)+1` takes only ten values that cluster at exact multiples of
three or one below. That step is executed here in full, data-side only, on the same
393-vehicle / 90,290-draw-range population as sections 11.1–11.9 (new harness
`veh4_factor.py`, `tools/harnesses/`, output `scratchpad/veh/factor.txt`). The harness
reuses the group/draw-range extraction from `veh_assoc.py`/`veh3_handle.py` unchanged
(`car_subheader`, `car_mesh_off`, `batch_offset`, `mesh_header`, `mesh_scan.read_block`)
-- no new parsing or gating logic, so nothing here can be attributed to a new harness
bug. **393 / 393 vehicles usable, 0 skipped, 90,290 draw ranges** -- the identical
population figure sections 11.1 and 11.9 report.

#### 11.10.1 The decomposition itself: `a = high16 // 3`, `b = high16 % 3`, confirmed against the fixed-divisor-5 alternative

**Predicate, stated before running:** section 11.9.6's P5 already confirmed `high16` is
dense from 0 in 393/393 vehicles, i.e. every vehicle's used values are exactly
`{0..N-1}`. A dense integer run split by **any** fixed divisor mechanically fills every
non-top band -- that check cannot fail for either candidate divisor and is **not**
evidence for one over the other. **This was tried first and caught mid-run as vacuous**
(recorded rather than silently dropped, per `HANDOFF.md` section 5's transparency
rule): both divisors scored "100% of non-top bands full" (divisor 3: 986/986, divisor 5: 579/579),
which is guaranteed by construction, not a result. The real discriminator is **where in
each divisor's band width `N` happens to land** -- i.e. this is section 11.9.8's own
`N`-histogram, re-expressed per divisor to make the choice between "3 outer, 5 inner"
and "5 outer, 3 inner" explicit and comparable on one footing, exactly as the task
asked ("both orderings and both arithmetic directions"). A POSITIVE divisor
concentrates on a small number of the `divisor+1` possible cut points (ideally "full"
or "short by exactly the top value"); a NEGATIVE divisor spreads roughly evenly across
all of them.

| Divisor | Top-band pattern (vehicle count, % of 393) | Concentration on top 2 patterns |
|---|---|---|
| **3** (`a=h//3`∈0..4, `b=h%3`∈{0,1,2}) | full `()`: 324 (82.4%) · missing `(2,)`: 65 (16.5%) · missing `(1,2)`: 4 (1.0%) | **389 / 393 (99.0%)** of 3 possible cut points |
| 5 (`a=h//5`∈0..2, `b=h%5`∈0..4) | missing `(4,)`: 142 (36.1%) · missing `(2,3,4)`: 114 (29.0%) · missing `(1,2,3,4)`: 63 (16.0%) · full `()`: 44 (11.2%) · missing `(3,4)`: 30 (7.6%) | 256 / 393 (65.1%) of 5 possible cut points |

**[CONFIRMED -- empirical.]** Divisor 3 lands on essentially two outcomes (full band, or
short by exactly the top sub-value) 99.0% of the time; divisor 5 spreads across all five
possible cut points with no dominant outcome. **The population's own `N` values pick out
divisor 3, not divisor 5.** The four-vehicle `missing=(1,2)` case is exactly section
11.9.8's `N=10` stray (`a` reaches 3, but the top band only has `b=0`) -- so the
"occasional minus-one" and the four-vehicle stray are now both accounted for inside one
decomposition rather than as two separate irregularities. Adopted going forward:
**`high16 = 3a + b`, `b` the fast/inner factor (0,1,2), `a` the slow/outer factor
(0..K-1, K = ceil(N/3), 1..5 per vehicle)** -- this is the "a+3*b" form the task named,
with the fast dimension mod 3, not the "a*5+b" form.

#### 11.10.2 Dose-response of the slow factor `a` (`K`): a real, moderate correlation exists, and it refines rather than reopens the index refutation

**Predicate:** sections 11.1/11.6 described the combined field's dose-response
qualitatively as "no trend" from a binned-mean table read by eye. That description is
tested here quantitatively for the first time, on `K` (`a`'s own per-vehicle cardinality,
`=ceil(N/3)`) against the same four structural counts, using Spearman rank correlation
with a shuffled-vehicle control (breaks the real per-vehicle pairing, keeps both
marginal distributions -- the same control shape as every other candidate in this
section). A POSITIVE is `|rho|` clearly above its own control; a NEGATIVE is `rho` at or
near its control.

| vs. structural count | `K` rho (shuffled control) | `N` rho (shuffled control), for comparison |
|---|---|---|
| groupCount (`nb`) | **+0.410** (−0.148) | +0.412 (−0.136) |
| partCount | **+0.244** (−0.141) | +0.235 (−0.124) |
| materialCount | **+0.412** (−0.121) | +0.393 (−0.126) |
| distinct part-TYPE count | **+0.329** (−0.159) | +0.338 (−0.146) |

**[CONFIRMED -- empirical, a correction to imprecise prior language, not to the prior
conclusion.]** Every correlation is real -- moderate, consistent in sign, and clearly
separated from its shuffled control (which sits at chance, near 0, as it should). **"No
trend" was too strong a description of the combined field's dose-response** and should
be read as "no *index-shaped* dose-response" going forward. What is **not** overturned:
the achievable ceiling. `max(a)` (equivalently `max(N)`) never exceeds 5 (14) anywhere
in the population, across the full observed range of every count checked --
`materialCount` 3–102, `partCount` 2–43, `groupCount(nb)` 6–311. An index into any of
these would need headroom to climb into the dozens or hundreds at the high end and it
never does; that is what the section 11.1/11.6 feasibility+dose-response tests actually
refuted, and it still holds. **`K` carries essentially the same correlation `N` already
carried** (every pair of rho values above agrees to within ~0.02) -- so the slow factor
in isolation does **not** supply a signal the combined field lacked; it inherits the
field's own shape, compressed. The honest reading: bigger, more complex vehicles tend to
*use* more of the fixed 1–5-unit space without the space itself ever growing --
correlation with general complexity, not indexing.

#### 11.10.3 The fast factor `b` is the one that lands differently: pinned at 3, with no measurable dose-response at all

**Predicate:** if `b` indexed anything sized per-vehicle, the number of distinct `b`
values a vehicle uses should climb with that thing's own size, the same shape as `a`
above. If `b` is a small fixed enumeration independent of vehicle content, its distinct
count should sit flat regardless of any structural count.

Distinct-`b`-count histogram, full population: **`{2: 1, 3: 392}`** -- 392 of 393
vehicles use all three values `{0,1,2}`; the single exception is the same `N=2` vehicle
already on file (section 11.9.8), where only `high16∈{0,1}` exist at all, so only `b∈{0,1}`
can occur. Binned by `groupCount` or `materialCount`, the mean is **flat at exactly
3.00 in every bin** except that one vehicle's own `groupCount=6` bin (mean 2.00) --
not a gradient, a constant. **[CONFIRMED -- empirical, and the cleanest negative this
investigation produced: there is no dose-response to fail here because there is no
variance to correlate in the first place.]** This is the factor that behaves differently
from the whole combined field, as the task hoped one might: **`b` is a fixed-cardinality
dimension, universally 3-valued, with zero measured dependence on materialCount,
partCount, groupCount, or distinct part-type count.** That is consistent with (not proof
of) a small fixed engine enumeration -- three render passes, three paint/damage tiers,
three LOD-independent quality bands -- but no such identity is confirmed data-side; see
11.10.5.

#### 11.10.4 The task's own named engine-level candidates, tested as a SCALED relationship on each factor separately -- all four refuted at chance level

**Predicate, per the task's explicit request to test a *scaled* relationship rather than
the bare equality section 11.1 already refuted:** for a per-range ordinal position `o`
(this range's own group index out of `nb` groups, or its own material id `low16` out of
`materialCount` materials) and a target band count `nbands` (3 for `b`, or this vehicle's
own `K` for `a`), predicted = `min(nbands-1, o*nbands // ordinal_max)`. A POSITIVE is a
real match rate well above its own shuffled-within-vehicle control; a NEGATIVE is real ≈
control.

| Candidate | Real | Shuffled control |
|---|---|---|
| `b` == group-thirds-band (by group index `g`, out of `nb`) | 31,201 / 90,290 (34.56%) | 31,348 / 90,290 (34.72%) |
| `a` == group-`K`-band (by group index `g`, out of `nb`) | 25,854 / 90,290 (28.63%) | 25,661 / 90,290 (28.42%) |
| `b` == material-thirds-band (by `low16`, out of `materialCount`) | 35,178 / 90,290 (38.96%) | 34,374 / 90,290 (38.07%) |
| `a` == material-`K`-band (by `low16`, out of `materialCount`) | 26,344 / 90,290 (29.18%) | 26,880 / 90,290 (29.77%) |

**[REFUTED -- CONFIRMED, empirical, with control, all four.]** Every real rate sits
within a point of its own shuffled control. **This directly answers the task's named
LOD-group-count candidate**, now tested as a scaled relationship rather than the
chance-level bare-equality test section 11.1 ran before the groups were fully decoded:
it is still chance-level. The material-set analogue (a natural candidate now that
`low16` is confirmed as the material id) fails identically. **For completeness, `b`
was also re-tested directly against the raw `g % 3` residue** section 11.9.7 already
refuted for the *combined* field: `b == g % 3` in 31,462 / 90,290 (34.85%) against a
shuffled control of 30,295 / 90,290 (33.55%) -- chance, confirming section 11.9.7's
refutation holds for the isolated fast factor too, not only for the field as a whole.

No fixed per-vehicle paint-variant or damage-stage count is currently extracted
anywhere in this project's own tooling (`.cvtf_pc`/`.xtbl` customization data, per
`spec-customization-data.md`, carries a per-vehicle "Variants" count that was **not**
pulled in here -- doing so would mean cross-referencing a `.ccar_pc`/`.gcar_pc` pair
against its sibling `.cvtf_pc` inside the same `.str2_pc` bundle, a materially different
extraction than anything this pass's harnesses already do). **Flagged as an open lead,
not tested** -- chasing it further belongs to a session that owns that scope, per
`HANDOFF.md` section 5's scope-jump discipline, not folded into this one uncontrolled.

#### 11.10.5 Net position: the factoring is a confirmed structural result; neither factor's real-world meaning is identified

**The factoring itself is CONFIRMED:** `high16 = 3a + b` with `b = high16 % 3` (fast,
exactly 3 values in 392/393 vehicles, 2 in the one `N=2` exception) and
`a = high16 // 3` (slow, `K = ceil(N/3)` values, `K` in 1..5) reproduces section
11.9.8's ten-value `N` histogram exactly, including its one irregular case (the
four-vehicle `N=10` stray, now explained as `a` reaching 3 while the top band holds
only `b=0`), and does so with a decisively better fit than the competing fixed-divisor-5
decomposition (99.0% vs 65.1% concentration on two dominant outcomes, section 11.10.1).

**What is now known about each factor separately, which the combined field's own tests
could not show:**
- `b` (fast, 3-valued) has **zero measured dependence on any of the four structural
  counts already tried** -- a cleaner, flatter negative than the combined field ever
  produced, and consistent with a fixed small engine enumeration.
- `a` (slow, 1-to-5-valued) **inherits the combined field's own moderate
  complexity-correlation** (Spearman rho 0.24–0.41, section 11.10.2) without adding an
  independent signal beyond it, while its **ceiling** stays exactly as capped and exactly
  as unrelated to any of those counts as the whole field's was.

**What was tested and refuted for both factors:** the task's own named engine-level
candidates -- LOD-style group banding (bare equality *and* scaled banding, both by group
index) and the material-set analogue (scaled banding by material id) -- at chance level,
controlled, for every one of the four factor/candidate pairings tried (section 11.10.4).

**Per this project's own house rule** (a candidate that passes feasibility but fails
dose-response is refuted, not "probably right anyway," and a clean factoring without an
identified meaning is still real progress rather than a stall): **the factoring
`high16 = 3a + b` is published as a confirmed structural fact about the field.
Neither factor's real-world meaning could be identified from data already in hand,
and no further disassembly was attempted this pass** (the task scoped this as a
pure data-analysis follow-up; section 11.9.8's own disassembly-side suggestion --
reading the element count of the `0x28`-byte runtime record array at `this+0xfc`,
section 11.9.5 -- remains open and unattempted, and would be the natural next step
for whichever session picks this back up with Ghidra access budgeted for it).
**[OPEN -- what `a` and `b` individually mean; CONFIRMED -- that they are the right two
factors to ask that question about.]**

### 11.11 Section 11.8's named next probe executed: `FUN_00ab0a10` read in full -- it is not part of the 64-byte record machinery at all, and no function in this chain branches on a non-zero `kind` value

Method: disassembly only, no new data harness. `FUN_00ab0a10` (775 bytes) was
decompiled and disassembled in full, together with its five callees and the full
disassembly of its one caller (`FUN_00ab0ef0`, the same 388-byte per-part walker
section 11.3 already read), to settle section 11.8's own named next step. New
scripts `tools/scripts/VehKindResolver.java` and `tools/scripts/VehOuterDisasm.java`;
output `tools/vehkind_resolver_run.txt`, `tools/veh_outer_disasm.txt`.

**`FUN_00ab0a10` does not touch the 64-byte record array, or its `+0x00` kind field,
at all.** Its only caller is `FUN_00ab0ef0` at a single call site (disassembly
`0xab0fd3`-`0xab0fe3`): the part object's own `+0x80` field is compared against `-1`,
and only when it still equals `-1` does the code call `FUN_00ab0a10` (passing the
part object itself as its argument) and store the call's result back into that same
`+0x80` field.

`ESI`/`EBX` here is the same per-part walking pointer section 11.3 describes --
**not** a 64-byte-record pointer. `FUN_00ab0a10` is the fallback resolver for one
single scalar field living directly on the part object, `part+0x80`, gated by
the identical "try the 100-entry table, fall back if still `-1`" shape the
64-byte records' own `+0x04` field uses (`part+0x80 != -1` remaps through
that same 100-entry table earlier in the same function; if it is *still* `-1` afterward,
`FUN_00ab0a10` supplies a value). `part+0x80` is a structurally separate field
from the `+0xC0`/`+0xC4` record array described in section 11.3 -- the task's own
framing of `FUN_00ab0a10` as "the fallback resolver called when a runtime cache
slot needs a fresh value" is accurate, but that cache slot belongs to the part,
not to a reference record. **[CONFIRMED -- disassembly, `0xab0fd3`-`0xab0fe3`.]**

Read start to end (`0xab0a10`-`0xab0d16`, matching the known 775-byte size),
`FUN_00ab0a10`'s body:

1. Reads six floats from a pointer chain rooted at its own first argument
   (`[[[arg]+4]+0x50]`, three dereferences, then offsets `+0x10,+0x14,+0x18,+0x20,+0x24,+0x28`
   off that) -- geometry state belonging to the part, not a kind tag.
2. Allocates a fresh 0x30-byte (48-byte) block from a per-thread pooled
   allocator (`TlsGetValue` + an indirect call through the TLS block's own
   vtable, `0xab0b76`-`0xab0b86`) and tags its `+4` field with type `0x30`.
3. Calls `FUN_00d4d7f0` (130 bytes) -- sets a vtable pointer
   (`PTR_FUN_01243174`) on a second, stack-local object and copies/clamps
   values into it from a global constant (`_DAT_0131e81c`).
4. Calls `FUN_00c8a890` (271 bytes) and `FUN_004f00f0`/`FUN_00da4170` --
   further floating-point packing/combination steps. **What these two
   specifically compute (e.g. a basis, a support point, a normalized extent)
   was not pinned down this pass and is reported as unconfirmed rather than
   guessed.** **[OPEN -- exact geometric meaning of steps 3-4; not pursued,
   since it is orthogonal to the kind question this probe was run to answer.]**
5. Calls `FUN_00760c30` (202 bytes) -- registers the new object into a fixed
   global table capped at 5000 entries (`DAT_0163a8f0` freelist,
   `DAT_0163f710` count, parallel per-slot arrays `DAT_0163f720`-`DAT_0163f750`)
   and returns the slot handle in `EAX`. This is the "runtime cache slot"
   mechanism the task's framing named.

None of these five steps, nor any of the five callees (`FUN_00c8a890`,
`FUN_00d4d7f0`, `FUN_004f00f0`, `FUN_00da4170`, `FUN_00760c30`, plus
`TlsGetValue`), reads a byte/dword at an offset matching the 64-byte record
layout's `+0x00`, branches on a small-integer enum, or carries a string literal
naming a resource domain. **[CONFIRMED -- disassembly, full function body plus
full callee bodies.]**

**The only place in this entire neighborhood that reads the record's `+0x00`
kind field is `FUN_00ab0ef0`'s own second loop** -- the same loop section 11.3
already identified as calling `FUN_00ab0d20`. Read at the instruction level
(`0xab101e`-`0xab102f`): the record's own `+0x00` kind field is compared against
`0`, and the call to `FUN_00ab0d20` -- passing the part-level context, the
record's index, and the record array's base pointer -- is only made when that
kind field reads exactly `0`.

`kind` is compared **only** for equality to zero, and is **not** itself passed
to `FUN_00ab0d20` -- the call forwards the record's index, the record array's
base pointer, and the part-level context, never the kind value. There is no
comparison anywhere in `FUN_00ab0ef0`, `FUN_00ab0a10`, or any of their combined
callees against 1, 2, 3, 4, 20, 21, 50, 51, 70, 71, 72, 73, 75, 135, 136, or
137. **[CONFIRMED -- disassembly, `0xab101e`-`0xab102f`, cross-checked against
the decompiled C in section 11.3.]**

**Net result: a clean, disassembly-confirmed negative.** The task's own named
next probe turns out not to belong to the kind-aware machinery at all -- it
resolves an unrelated per-part scalar. The actual kind-reading code
(`FUN_00ab0ef0`'s second loop) treats kind as a **binary gate** (zero vs.
non-zero) at ctor time, never a multi-way branch. **What the non-zero bands
(1-4, 20-21, 50-51, 70-75, 135-137) denote remains OPEN** -- this pass rules out
load-time dispatch in this call chain as the place that distinguishes them.
The next place to look, not attempted this pass (per the task's own scope:
named-function-and-call-graph only, no whole-binary predicate search), is
whatever consumes the already-remapped `record+0x04` handle *after*
construction -- code downstream of the ctor that dereferences a resolved
reference record, which lies outside `FUN_00aaf690`/`FUN_00ab0ef0`'s own call
graph and was not searched for here. **[OPEN -- what the non-zero kind bands
denote; CONFIRMED -- that `FUN_00ab0a10` and every function it or
`FUN_00ab0ef0` calls are blind to that distinction.]**

### 11.12 Section 11.11's named next probe executed: every real downstream consumer of `part+0xC0`/`+0xC4` traced -- clean negative, and the offset-only search this task explicitly forbade was tried anyway (on a sharper anchor) to confirm why it was forbidden

Method: two complementary approaches, both disassembly-only (`VehKindDownstream.java`
through `VehKindDownstream7.java`, outputs `tools/vehkind_downstream*_run.txt`), per
11.11's own named next step -- find code, other than `FUN_00aaf690`/`FUN_00ab0ef0`
and their already-read callees, that reads a resolved reference record's `+0x04`
handle or `+0x00` kind field after construction.

**(A) Real call-graph tracing from the ctor's own siblings of `FUN_00ab0ef0`.** The
vehicle ctor `FUN_00ab1080` (1,531 bytes, `scratchpad/veh/ctor_full.txt`) calls, in
order: `FUN_00aaf690` (part fixups) -> hardcoded per-vehicle special cases ->
`FUN_00ab0ef0` (the ref-record resolver, already fully read) -> `FUN_00aaaea0` three
times (`0xab157d`, `0xab15ca`, `0xab1617`) -> `FUN_00ab0730` (`0xab165f`). These two
are the ctor's *only* other per-part-array consumers, i.e. the concrete place 11.11
pointed at ("downstream of the ctor that dereferences a resolved reference record").
Both were read in full for the first time this pass:

- **`FUN_00aaaea0`** (145 bytes, "bbox accumulation over root parts") walks the part
  array itself (reading the part count from its own first argument's `+0x390` field,
  and addressing each part at `+0x3A0` plus index times the `0xE0` stride, matching
  section 11.3's own layout exactly) and, per part passing a
  `part+0x44` type-bitmask test and a `part+0x4c == -1` ("is root") test, calls
  **`FUN_00aaad50`** (331 bytes) with the part pointer directly. `FUN_00aaad50` is
  recursive -- it walks each part's children via `part+0x54`'s own array (section
  11.3's first nested-pointer field), reads `part+0x48` flag bits `0x2000`/`0x40000`,
  and accumulates min/max over `part+0x60`..`+0x7F` (two bbox vec4s) with SSE
  `MINPS`/`MAXPS`. **Neither function contains a single instruction touching
  `+0xC0` or `+0xC4` on any register** -- confirmed by an exhaustive instruction scan
  of both bodies plus their own callees (`FUN_00583580`, `FUN_004c0170`,
  `FUN_00533de0`): 0 hits in all five. **[CONFIRMED -- disassembly, full bodies.]**
- **`FUN_00ab0730`** (724 bytes, "part -> rig-bone binding") reads the vehicle
  header's `+0x20` bone-remap byte array and `+0x390`/`+0x3E0` (part count and each
  part's name field, stride `0xE0`, offset by the name field's own `+0x40` position)
  to resolve bone indices by name via `FUN_004bc880`. **Zero instructions touch
  `+0xC0`/`+0xC4`.** **[CONFIRMED -- disassembly, full body plus callees
  `FUN_00753b90`, `FUN_004bc9c0`, `FUN_004bc880`, `FUN_004bc7d0`.]**
- `FUN_00aaaea0` has exactly one caller *besides* the ctor, found by an unrestricted
  binary-wide xref search (not an offset guess): **`FUN_0092d300`** (701 bytes,
  called once, from `FUN_0092ff20` at `0x930197` -- a genuine runtime/post-construction
  call site). It resolves a hardpoint/attach-target transform via a `switch` on a
  type field and, in case 0, calls `FUN_00aaaea0` against a *different* object
  (read from that object's own `+0x10` field, not a part pointer) purely to get a bbox centre. **Zero
  instructions in its own 701-byte body touch `+0xC0`/`+0xC4`.** **[CONFIRMED --
  disassembly, full body; its own single caller also checked.]**

**Net result of (A): every real, call-graph-derived downstream consumer of the
per-part construction pipeline -- both siblings in the ctor and the one runtime
caller reachable from them -- is clean. No reference-record dereference exists in
this branch of the call graph.**

**(B) The offset-only whole-binary search this task's brief explicitly warned
against was run anyway, on a sharper anchor than a plain literal match, specifically
to see whether it could rescue anything (A) missed -- it could not, and it
reproduced the exact "no selectivity" failure `HANDOFF.md` section 5/27.4 already
documents for this technique:**

1. Plain conjunction -- any register `+0xC0` and, in the same function, any register
   `+0xC4` -- gave **1,553** functions touching `+0xC0` and **1,017** touching
   `+0xC4` out of the binary's full instruction stream (3,546,397 instructions
   scanned). Useless on its face.
2. Restricting to non-stack-frame accesses (excluding `ESP`/`EBP` bases, i.e. real
   pointer dereferences rather than deep locals in oversized frames) still left
   **932**/**546** functions, **279** in conjunction.
3. Sharpening the anchor to the part array's own address arithmetic -- literal
   `0x3A0` (the array's base offset on the vehicle object) **and** literal `0xE0`
   (its stride) co-occurring in the same function, i.e. actually computing a part
   address rather than merely matching a common round-number displacement --
   narrowed this to **38** functions. Of those, only **7** had a genuine
   (non-stack) `+0xC0`/`+0xC4` hit, and every one was individually read in full and
   ruled out:
   - `FUN_00aaad50`/`FUN_00aaaea0` -- already covered by (A), clean.
   - `FUN_00ab1080` (the ctor itself) -- its `+0xC4` use (`0xab119c`-`0xab11b1`) is
     the **vehicle-header**-level fixup field this same file's own ctor-decompile
     comment already names (`"fixes up offset fields +0x10,+0x18,+0x28,+0x2c,+0x38,
     +0xbc,+0xc4"`) -- a different structural level from the per-part 64-byte record
     array, not a downstream consumer of it.
   - `FUN_0043e420` -- already independently identified this session as Effects
     (`71BW`-magic) format code, not vehicle code at all (`HANDOFF.md` section
     27.1's "Tree layout 11/13 declaration-builder caller" row).
   - `FUN_004cea50` -- its `EBX+0xC0`/`+0xC4` pair is two floats on an
     animation-blend/time object (`EBX` = a parameter tested at `+0x30` bit `0x10`,
     feeding `CVTSS2SD`/`SUBSD` against a value from `FUN_004bc550`, the same
     bone-lookup family as `FUN_00ab0730`'s callees but operating on an anim state,
     not a part).
   - `FUN_00b75390` -- its `EBX+0xC0` is reached through a **double indirection**
     (`ECX = [EDI+8]`, `EBX = [ECX+0xC]`, then `[EBX+0xC0]`) into an unrelated
     object amid dense SSE lerp/clamp math (`MULPS`/`CMPLTPS`/`ANDPS`) -- not the
     single-hop part-array addressing every genuine part consumer in this project
     uses.
   - `FUN_00c81a90` -- reads `ESI+0x340` through `ESI+0x3B0` as eight consecutive
     SSE quadwords (a bone/matrix-palette buffer) before writing `ESI+0xC0`; those
     source offsets alone exceed the part record's own `0xE0`-byte size, so `ESI`
     cannot be a part pointer.
   - `FUN_00f10e40` -- reaches `ESI+0xC0` through C++ vtable dispatch
     (`CALL EDX` off `*(*ECX)+0x14`) into what reads as a stat/event-counter object
     (`ADD dword ptr [EAX+0x40],ECX`), unrelated to vehicle parts.

   **Every hit resolves to either an already-known different structural level, an
   already-refuted non-vehicle function, or a coincidental offset collision with an
   unrelated struct -- exactly the "whole-binary predicate over a small struct
   displacement has no selectivity" failure mode `HANDOFF.md` section 5/27.4
   already catalogues (469/461/23/272 hits on unrelated searches; 1,553/1,017 and
   932/546/279 here match that signature precisely).** **[CONFIRMED -- disassembly,
   all 7 candidates read in full; the remaining 31 of the 38 had zero non-stack
   `+0xC0`/`+0xC4` hits at all.]**

   Note the technique's own blind spot, for completeness: a function that receives
   an *already-computed* part/record pointer as a parameter -- the exact shape
   `FUN_00ab0ef0` itself uses, and the shape a genuine downstream consumer would
   most plausibly take -- carries no `0x3A0` literal of its own and cannot appear in
   this list. (A) is not subject to that blind spot, since it follows real xrefs
   regardless of how each callee's pointer arithmetic is written; (B) is reported
   only as corroboration that the offset-scanning route the task warned off is
   indeed unproductive here, not as additional positive evidence.

**Net result: a second clean, disassembly-confirmed negative, on top of 11.11's.**
Between the ctor's own two downstream siblings (and their one further runtime
caller) and a structurally-anchored scan of every function that actually computes a
part-record address, no code was found anywhere in the binary that reads a resolved
reference record's `+0x04` handle or `+0x00` kind field after `FUN_00ab0ef0`
constructs it. **What the non-zero kind bands (1-4, 20-21, 50-51, 70-75, 135-137)
denote remains OPEN** -- this task's own scope (named-function/call-graph tracing,
explicitly not a literal-band search) has now been exhausted twice (11.11, this
section) without finding a consumer; a further pass would need a genuinely new angle
(e.g. a runtime trace/breakpoint on the resolved handle's value, which this project's
disassembly-only tooling cannot perform) rather than a third static sweep of the
same call graph. **[CONFIRMED -- disassembly, exhaustive within the scope described;
OPEN -- the non-zero kind bands' meaning.]**


## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): fixed 2 cross-references (§8.4.1→`spec-vertex-format.md` §8.4.3; "line 955-956"→§11.9.2); marked stale text superseded in place (§7.1 `+0x0C` "upper bound" and `high16` "part index", §4 `+0xC0`/`+0xC4` material-reference reading downgraded per §11.7); marked 6 stale OPEN/HYPOTHESIS items resolved (§7.1 paint source, §10 items 7 and 10, §11.5 amendment row, §11.6 "enumeration" OPEN, and superseded-notes under the §11.6/§11.9.8/§11.10 headings); added pointers to `spec-vertex-format.md` §4.2's 388/388 replay at §7 and §10 item 5 (labels unchanged); reworded 3 decompiler-shaped lines (§11.7, §11.9.2, §11.11).
