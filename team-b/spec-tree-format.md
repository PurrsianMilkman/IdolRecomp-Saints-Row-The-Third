# Saints Row: The Third — `.csrt_pc` / `.gsrt_pc` Tree Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, target selected from `spec-format-inventory.md` §6 (type 36, internal name `Tree`, `c`/`g` pair). The `st_` filename prefix and the `srt` extension match the SpeedTree-family naming the inventory already noted; nothing below relies on any external format knowledge — everything is read from this engine's loader and the shipped files.
**Scope:** Both files: the `c`-file's fixed `'TREE'` block, a geometry sub-structure (bounds, LOD slots), a **shared material-set block** carrying inline material definitions, per-LOD shared **"Mesh" sub-blocks**, per-material/per-LOD runtime arrays, and collision capsules; and the `g`-file's per-mesh segmentation.
**Method:** Registration row → constructor → every parser callee (three Ghidra passes, `tools/tree_ctor.txt`, `tools/tree_subparsers.txt`, `tools/tree_callers.txt`), then a loader replay against **all 11 distinct trees** (28 shipped copies across 8 bundles; identical bytes wherever duplicated) that must land exactly on end-of-file. Replay harness: `scratchpad/tree_replay.py`, `scratchpad/tree_extra.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. Magic numbers, offsets, strides and alignments are load-bearing format data; texture names quoted are shipped data. Function addresses are cited as evidence.

**Cross-team verification, 2026-09-14 (relayed):** Team B built a from-scratch `sr3tree` reader against this spec alone (never having built one before) — reused their existing `MaterialBlock`/`MeshBlock` code unmodified per this document's own "shared block" claim, with only the three genuinely tree-specific structures (the `'TREE'` block, the geometry sub-structure, collision capsules) newly written. Real-data validation against the three archives this document's own population figures are drawn from matched exactly on the first attempt: 28/28 parsed, 11 distinct trees, 1 shader hash, capsule radius range 0.06–1.1 — all matching this document's own §2/§8 figures to the digit. Provenance per `HANDOFF.md` §27.8: relayed, not independently rerun against Team B's own binary on this side.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **Trees are an assembly of blocks this project already knows, plus one new fixed header.** The `c`-file is: shared material-reference block → mandatory pad → a `'TREE'` block (magic `0x54524545`, version ≥ `0xCC`, fixed `0x208` bytes) → a fixed `0x170` geometry sub-structure → a **material-set block** whose records are parsed by the *same* inline-material-definition routine foliage uses → a generic index list → up to **4 LOD slots**, each present slot holding a shared **"Mesh" sub-block** (version 9) whose data lives in the `g`-file → three small runtime arrays → **collision capsules**. The replay reproduces the exact file size in **11/11**. **[CONFIRMED — disassembly + empirical.]**
- **The `g`-file is contiguous per-mesh segments, each tagged.** Every Mesh sub-block's check value (the `u32` after its version) is the first `u32` of its segment in `.gsrt_pc`; the first segment starts at `g+0`, later ones follow at the previous segment's end; each segment's index buffer sits at segment `+0x10` — the `.gcmesh_pc` offset-`0x10` finding generalised to multi-mesh files. **25/25 meshes.** The Mesh sub-block also **ends with a copy of its check value** (a bookend, 25/25). **[CONFIRMED — empirical; the tag-match is what the loader checks per `spec-geometry-format.md` §4.1.2.]**
- **Sampler-slot hashes are confirmed across formats.** The three slot hashes trees share with foliage bind diffuse, normal and specular maps identically in both (by filename suffix, 185 + 79 bindings); two further slots are tree-only and always bound to placeholder textures. This upgrades the foliage spec's "sampler role, not filename" reading from single-format to **cross-format confirmed**, and again the slot value is never the engine hash of the name (0/185). **[CONFIRMED — empirical.]**
- **Only one shader hash and one material shape ship for all 37 tree materials** — `(5 textures, 4 constants, 6 vec4)`; every tree is the same shader with different textures. **[CONFIRMED — empirical.]**
- **The `c`-file is a memory image**: several dwords are stale heap pointers (the presence markers for LOD slots are nonzero *pointers*, used as booleans), and at least two files contain a fragment of a build-tool log line (`…uccessfully: st_`) inside the geometry sub-structure. Pointer slots are re-written at load; nothing reads the stale values. **[CONFIRMED — empirical + disassembly.]**

## 2. Population facts

| | |
|---|---|
| Shipped entries | **28** `.csrt_pc` + **28** `.gsrt_pc`, 8 bundles (`sr3_city_0` ×6 incl. `awld_compact` and the bridge bundles, `dlc2`, `dlc3`) |
| Distinct trees | **11** (`st_com_break/lrg/med/sml`, `st_pine_full/tall/xmas`, `st_shrub_lrg/med/sml/nice_01`); every duplicate copy is byte-identical |
| `c` sizes | 2,832 … 3,792 bytes; `g` sizes 10,844 … 288,260 |
| Pairing | 11/11 `c` have a `g`; the registration row declares both extensions |
| LOD slots | always **4**; **2 or 3** populated (25 Mesh sub-blocks total) |
| Materials | 3 or 4 per tree (37 total), one shader hash, one shape |
| Collision capsules | 0 (all three shrubs), 1 (most trees), 2 (`st_pine_tall`), 4 (`st_com_lrg`) |
| Runtime | each tree links itself into a global doubly-linked list at load (`FUN_00a6f500`); a large GPU-side build follows (`FUN_00a713e0`, not parsed here — it reads no further file bytes) |

> **Correction (2026-09-12), cross-referenced from `spec-vertex-format.md` §12.9.2 where it was found while tracing the tree-specific chain for the layout-11/13 vertex question.** `FUN_00a713e0` was read in full and is **not** a GPU-side vertex build — it never touches the LOD Mesh sub-blocks. It is the **collision-capsule → physics-primitive builder**: for each of the `T+0x18` capsules it compares the start/end points and, when they coincide (a degenerate, zero-length capsule) and the radius clears a minimum threshold, constructs a **sphere** primitive; otherwise, for a genuine non-degenerate capsule clearing the same threshold, it constructs a **capsule** primitive from both endpoints and the radius. The built primitives are then batch-registered. **[CONFIRMED — disassembly.]** Left here rather than silently rewritten, per this project's rule that corrections stay visible in place. **Further update (2026-09-20, §13.1, §13.7):** the list is walked only by wind / distance-scale code; the tree's GPU-side build is `FUN_00e7aae0`, called on a pooled template stored at `tree+0x1D8`.

## 3. `c`-file layout (replay-verified, 11/11 land on EOF)

| Region | Position | Content | Confidence |
|---|---|---|---|
| Material-reference block | `+0x00` | magic `0x00043854`, name-table length, texture count, names (11–13 `.cvbm_pc`/`.gvbm_pc` pairs). Validated first (`FUN_00dd7d70`) | **[CONFIRMED.]** |
| *(mandatory pad)* | to 16 | `spec-geometry-format.md` §3.1.1 | **[CONFIRMED.]** |
| **`'TREE'` block** `T` | fixed **`0x208`** | §4 | **[CONFIRMED — disassembly + 11/11.]** |
| *(align 16)* | | | |
| **Geometry sub-structure** `G` | fixed **`0x170`** | §5 | **[CONFIRMED.]** |
| *(align 8)* | | | |
| **Material-set block** | `0x20` header + names + slots + records | §6 (shared block, `FUN_00e40210`) | **[CONFIRMED.]** |
| *(align 8)* | | | |
| Index list | `0x10` header + `count × u32` | §7 (shared utility, `FUN_00e401d0`); identity `0…n−1` in 11/11 | **[CONFIRMED.]** |
| *(align 8)* | | | |
| LOD presence entries | `4 × 8` | nonzero first dword ⇒ a Mesh sub-block follows for that slot (on disk the nonzero value is a stale pointer) | **[CONFIRMED — disassembly + 11/11.]** |
| **Mesh sub-blocks** | one per present slot, each advanced by its own length field | version 9, g-backed (flags bit 0 set 25/25), check value = its `g` segment tag, bookended | **[CONFIRMED.]** |
| *(align 8)* | | | |
| Runtime arrays | `n_mat × 8`, then `4 × 8`, then `n16 × 16` | pointer slots per material and per LOD; `n16` (0 or 6) 16-byte records in the three tall trees | **[CONFIRMED — sizes; OPEN — the 16-byte records.]** |
| *(align 16)* | | | |
| **Collision capsules** | `n_cap × 0x60` | §8; `n_cap` from `T+0x18` | **[CONFIRMED — sizes; HIGH CONFIDENCE — meaning.]** *(start/end/radius meaning upgraded to CONFIRMED — disassembly by the §8 update, `FUN_00a713e0`)* |
| EOF | | exact in 11/11 | |

## 4. The `'TREE'` block (`0x208` bytes)

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | Magic **`0x54524545`** (the bytes `E E R T` — a packed four-character tag reading `TREE`) | **[CONFIRMED — loader requires it.]** |
| `+0x04` | Version, loader requires **> `0xCB`**; shipped value **`0xCC` (204)** in 11/11 | **[CONFIRMED.]** |
| `+0x08` / `+0x0C` | Pointer slot → the geometry sub-structure (stale on disk, fixed up) | **[CONFIRMED — disassembly.]** |
| `+0x18` | **Collision-capsule count** (0, 1, 2 or 4) | **[CONFIRMED — drives the `0x60`-record walk.]** |
| `+0x20` / `+0x24` | Pointer slot → the capsule array (fixed up) | **[CONFIRMED — disassembly.]** |
| `+0x28` … `+0x37` | Linked-list prev/next slots written at load | **[CONFIRMED — disassembly.]** |
| ~~`+0x3C` … `+0x207`~~ `+0x38` … `+0x207` | ~~A table of small positive floats (`5, 2.5, 50, 2, 0.1, 2, 1, 0.25, 5, 4, 3, 1, 5, 0, 0, 0.75, …, 0.85, 10, 0.5, 2, 2, 2, 2, 0.25, 1.1`). Identical in 5 files, different in 6 — i.e. a per-tree tuning set, not a constant. Plausibly wind/sway and LOD-transition parameters.~~ **Resolved 2026-09-20 (§13.2–§13.4, CPU side): the region is an embedded wind-state object — 38 file-loaded floats `k = 0…0x25` at `+0x38 + 4k` (the table really starts at `+0x38`), runtime state and seven output `vec4`s after it (zero on disk, 11/11). Wind: yes. LOD: no — the LOD distances are geometry `+0xE0` (§13.5.2).** | ~~**[CONFIRMED present; HYPOTHESIS — meaning.]**~~ **[CONFIRMED — disassembly (CPU side); GPU meaning OPEN]** |

## 5. Geometry sub-structure (`0x170` bytes)

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | Bounds **min** as `float4` (w duplicates z — SIMD padding) | **[HIGH CONFIDENCE — the values are a plausible box: e.g. `(−1.63, 0, −1.45)` to `(1.59, 4.56, 2.03)` for a 4.5 m tree; `(−11.7, −2.8, −11.0)` to `(11.6, 25.6, 11.7)` for the large one.]** |
| `+0x10` | Bounds **max** as `float4` | as above |
| `+0x20` / `+0x24` | Pointer slot ← the index list (§7) | **[CONFIRMED — disassembly.]** |
| `+0x28` / `+0x2C` | Pointer slot ← the material-set block (§6) | **[CONFIRMED — disassembly.]** |
| `+0x30` | **LOD slot count** — `4` in 11/11 | **[CONFIRMED — drives the presence-entry walk.]** |
| `+0x38` / `+0x3C` | Pointer slot → the presence entries | **[CONFIRMED — disassembly.]** |
| `+0x40` | **Material count** (3 or 4); equals the material-set block's count 11/11 | **[CONFIRMED.]** |
| `+0x48` / `+0x50` / `+0x70` | Pointer slots → the three runtime arrays | **[CONFIRMED — disassembly.]** |
| `+0x58` | Equals *material count − 1* in 11/11 (the last material's index?) | **[CONFIRMED — the identity; OPEN — meaning.]** |
| `+0x5C` | Count of 16-byte records (0 or 6) | **[CONFIRMED.]** |
| `+0x60` | A float that scales with tree size (3.1 small shrub … 35.9 large tree) — not the box diagonal or radius exactly | **[OPEN.]** |
| `+0x64` | A float within a few % of the tree's height (4.56, 27.2, 16.8, 44.7 …) | **[OPEN.]** |
| `+0x68` | Often equals bounds min-y (−3.0, −2.0, −0.62); not always | **[OPEN.]** |
| `+0x70` … | Mostly zero; a handful of stale pointers/small ints; `+0xD0` holds the log-string fragment in ≥ 2 files | **[CONFIRMED — memory image.]** |

**Update (2026-09-20, §13.5):** consumers of this structure were read. New confirmed fields: **`+0xE0…+0xEC` = four LOD distances A<B<C<D** (`+0xF0`/`+0xF4` derived, zero on disk), `+0xF8…+0x167` = destination of an unreferenced wind-constant copy; bounds `+0x00`/`+0x10` now CONFIRMED by their consumers; `+0x20` is the material handle table (overwritten at load), `+0x48`/`+0x50` per-material / per-LOD runtime objects. **No reader was found** (bounded scope, §13.5.3) for `+0x40`, `+0x58`, `+0x60`, `+0x64`, `+0x68`, `+0x70` or the six 16-byte records; the three trees that have those records are also the only ones with a meaningful float block at `+0x80…+0xDC` (a rectangle plus an atlas-like rectangle, `+0xD8/+0xDC = (a1−a0, b1−b0)` exactly, 3/3) — structure recorded, meaning HYPOTHESIS.

## 6. The material-set block — a shared engine block

Parsed by `FUN_00e40210` (three callers engine-wide; the tree geometry parser is one). Layout, from disassembly and replay:

1. Align 8. Header `0x20`: `+0x00` **record count**, `+0x08` pointer slot (→ record-pointer array), `+0x10` **name-table length**, `+0x18` pointer slot (→ names). **[CONFIRMED.]**
2. Names start at `header + 0x20`, padded to 16, **plus one byte** — the table begins with a leading NUL and every name offset is relative to that `+1` base. **[CONFIRMED — disassembly; the replay resolves 185/185 bindings to `.tga` names.]**
3. Align 8; `count × 8` pointer slots (zero on disk). **[CONFIRMED.]**
4. `count` **inline material definitions**, each parsed by **the same routine as foliage** (`FUN_00e71090`; `spec-foliage-format.md` §6): `u32 size`, align 8, `0x30` header, `A × 12` texture bindings, `B × 4` constant-name hashes, `C × 16` vec4 values; consumed = size in **37/37**. **[CONFIRMED.]**

Population: shape **`(5, 4, 6)` ×37**, flags `0x40` ×37, **one shader hash** (`0x967A81C0`) ×37; 21 distinct `.tga` names (`Bark_01_D/N/S`, `Leaves_01_N/S`, `Leaves_02_D`, `Branch_01_D/N`, `ShrubLeaves_D/N/S`, `pineneedles_d/n/s`, `common_bb_d/n` — the billboard pair — plus the placeholders `flat-normalmap_n`, `flat-normal`, `missing-alpha`, `misc-white`, `misc-static`). Note the names are case-preserved here while the material-reference block at `+0` lists the same textures lowercased — two tables, two conventions, in one file. **[CONFIRMED — empirical.]**

### 6.1 Sampler slots, confirmed across two formats

| slot hash | trees (185 bindings) | foliage (79 bindings) | role |
|---|---|---|---|
| `0x69B48F91` | every `_D` map, `common_bb_d`, `pineneedles_d` (+1 `misc-static`) | `fol_bush*`, `bodega_*`, `GarbageCan_01_D`, … | **diffuse** |
| `0x2808EB90` | every `_N` map, `pineneedles_n`, `common_bb_n` (+1 `flat-normal`) | `flat-normalmap_n` ×21, `GarbageCan_01_n` | **normal** |
| `0xE848C9CA` | every `_S` map, `pineneedles_s`, or `misc-white` ×15 | `GarbageCan_01_S`, `magazine01`, litter diffuse reused | **specular** |
| `0x7B52BC1B` | `flat-normalmap_n.tga` ×37 | — | tree-only; a second normal slot held at "flat" |
| `0x138D8F6E` | `missing-alpha.tga` ×37 | — | tree-only; an alpha/opacity slot held at a placeholder |

The same 32-bit values select the same texture *roles* in two formats with disjoint texture sets. That is what "sampler-slot identifier" predicts and what "filename hash" forbids. `slot == engine_hash(name)`: **0/185** (foliage: 0/79). **[CONFIRMED — empirical, cross-format.]** Two of the four tree constant-name hashes also occur among foliage's nine.

## 7. The index list — a shared utility

`FUN_00e401d0` (**ten** callers engine-wide, including the foliage block parser — it is the "count-headed handle table" `spec-foliage-format.md` §3 described): align 8; `0x10` header `{ pointer slot, 0, count, ? }`; `count × u32`. In trees the list is `0…n_mat−1` (identity) in 11/11 — ~~a material index remap that shipped un-permuted~~ **correction 2026-09-20 (§13.5.1): these are placeholders, overwritten at load with material-cache handles; not a remap.** **[CONFIRMED — disassembly + empirical.]**

## 8. Collision capsules (`0x60` bytes each, count at `T+0x18`)

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` … `+0x0F` | four dwords: small indices/flags in some files (`1,1,5,5`; `16,16,17,17`), byte-pattern fills (`0x01010101`) in others, float-like in the tall pine | **[OPEN.]** |
| `+0x10` | `float4` **start point** (w = z) | **[HIGH CONFIDENCE.]** *(upgraded to CONFIRMED — disassembly by the update below)* |
| `+0x20` | `float4` **end point** | **[HIGH CONFIDENCE.]** *(upgraded to CONFIRMED — disassembly by the update below)* |
| `+0x30` | **radius** (0.06 … 1.1) | **[HIGH CONFIDENCE.]** *(upgraded to CONFIRMED — disassembly by the update below)* |
| `+0x34` … | zero except in the tall pine (11 further floats) | **[OPEN.]** |

Why capsules: in 9 of 12 records the segment is exactly vertical (same x/z, y from ≈0 to ≈ the tree's height: `0.004 → 3.004` r `0.06`; `0.358 → 16.358` r `0.563`; `−2.385 → 25.604` r `0.8`); the large tree carries a trunk plus three tilted branches at r `0.33`; the three shrubs carry none. A trunk/branch collision (or wind-anchor) capsule fits all of that; the field semantics are inferred, not read from code that consumes them. **[HIGH CONFIDENCE — inferred.]**

**Update (2026-09-12) — the consumer WAS found, upgrading this to `CONFIRMED — disassembly`.** `FUN_00a713e0` (Population facts, §2) reads exactly `+0x10`/`+0x20`/`+0x30` as start point, end point and radius, and branches on whether the start and end points coincide: degenerate (zero-length) capsules become **sphere** primitives, non-degenerate ones become **capsule** primitives, both gated by the same minimum-radius constant. This is a physics/collision-shape build, not a rendering one — consistent with, and now explaining the mechanism behind, the trunk/branch collision reading above. See `spec-vertex-format.md` §12.9.2 for the full disassembly account (found while tracing a different question — the tree vertex-layout codes — not this one).

## 9. The `g`-file (`.gsrt_pc`)

- Contiguous segments, one per present Mesh sub-block, in LOD order. Segment `n` starts where segment `n−1` ends (first at `+0`); starts are 4-aligned, not 16. **[CONFIRMED — empirical, 25/25 by tag search; the loader threads a `g`-cursor through the Mesh parser, which is how consecutive meshes find their data.]**
- Segment `+0x00`: the **tag** — equal to the Mesh sub-block's check value in the `c`-file (`25/25`); `+0x04…+0x0F` zero; `+0x10`: the `count × stride` index buffer (first three `u16` all `< count`, 25/25). **[CONFIRMED — empirical; mirrors `spec-geometry-format.md` §4.1.2 items 9–10.]**
- Segment length per vertex ≈ 16–27 bytes across the population (index buffer plus vertex channels). **[CONFIRMED — measured; channel layout OPEN as in the geometry spec.]** **[The general channel array is closed in `spec-vertex-format.md`; for the tree layout codes 11/13, normal/tangent are located (code 13 HYPOTHESIS only) and position is still OPEN, `spec-vertex-format.md` §12.9/§12.10.]**

## 10. Cross-format notes

- **Three shared blocks in one file**: the material-reference block (`+0`), the inline material definition (foliage's record), and the Mesh sub-block (`.ccmesh_pc`/`.ccar_pc`/foliage). The material-record parser has **10 call sites in 8 functions**; the Mesh wrapper 8; both are used by several parsers this project has not yet opened — a lookup table for future passes is in `tools/tree_callers.txt`.
- The Mesh sub-block **bookend** (last `u32` = check value) was not noticed on `.ccmesh_pc`; worth re-checking there (`spec-geometry-format.md` §4.1.2 item 11). **[Since confirmed on 1,937/1,937 `.ccmesh_pc`/`.csmesh_pc` pairs, `spec-vertex-format.md` §4.1.]**
- Foliage's inline-mode Mesh (no `g`) vs trees' g-backed Mesh with per-mesh segments: same block, both branches of the bit-0 switch now observed on real files.

## 11. Methodology note

The first replay attempt guessed the material records started at "names end, aligned" and found a rule (align-32) that fit 4/4 files tried. The decompile gave the true rule — align 8, then `count × 8` pointer slots — which produces the same offset on those files by coincidence. **A rule that fits should still be replaced by the rule the loader uses**; the coincidence would have broken on a tree with a different name-table length.

## 12. Open Items

1. ~~`'TREE'` `+0x3C…` parameter table — meaning (wind/LOD is the guess).~~ **RESOLVED 2026-09-20 (CPU side, §13.2–§13.4): an embedded wind-state object at `+0x38`, 38 file-loaded floats, every one read; LOD is not in it (§13.5.2). Still open: the GPU-side meaning and upload of the seven output constants (§13.3, §13.9).**
2. ~~Geometry sub-structure `+0x58`, `+0x60`, `+0x64`, `+0x68`.~~ **ADVANCED 2026-09-20 (§13.5): the readers of the rest of the structure were identified (new confirmed fields `+0xE0…+0xF4`), but no reader of these four fields exists in the bounded scope (§13.5.3); empirical relations recorded. Still OPEN.**
3. ~~The 16-byte records (`+0x5C`, 6 in each of the three tall trees).~~ **ADVANCED 2026-09-20 (§13.5.3): no reader found; structure decoded (2×3 grid of `(a,b)` with constant `(c,d)`, `d` = `+0xDC`, 3/3) alongside a co-occurring float block at `+0x80…+0xDC`; meaning HYPOTHESIS only. Still OPEN.**
4. Capsule header dwords and the tall pine's trailing floats. **(Partially resolved 2026-09-12: the start/end/radius fields' CONSUMER is now confirmed by disassembly — see the update to §8 above and `spec-vertex-format.md` §12.9.2. The leading four dwords at `+0x00`..`+0x0F` and the tall pine's trailing floats remain OPEN; `FUN_00a713e0` does not read them.)**
5. Which two other formats use the material-set block (`FUN_00e40210`'s other callers `FUN_00866d80`, `FUN_00e3e590`). **[Partly resolved: `FUN_00e3e590` is the `.clmesh_pc` MIDDLE parser (`spec-physics-format.md` §4.4.6); `FUN_00866d80` is still open.]**
6. The `g` segment's channel layout beyond the index buffer (shared with the geometry spec). **[Now tracked in `spec-vertex-format.md` §12.9/§12.10: normal/tangent located for codes 11/13 (13 HYPOTHESIS only); position still OPEN.]**

## 13. Consumer-side decode — the wind object, LOD distances, GPU build (2026-09-20, agent AE)

**Agent AE, `gp_tree2b`, 2026-09-20 (consumer side).** Anchor: the global list head that `FUN_00a6f500` links every loaded tree into — a **singleton tree manager at `0x02706800`**. Its complete set of direct references is nine, all in known code (`FUN_00a71250` ×2 reads, `FUN_00a70ca0` ×3 call-argument uses, the ctor `FUN_00a71810` at `0x00a7189d`, the destructor tail at `0x00a7122c` (code Ghidra had not made a function: `0x00a711f0`–`0x00a7124e`, registry-unregister + unlink + physics release), and the CRT static initialiser/atexit pair `0x01009570`/`0x01019c40`). The list is therefore iterated by **exactly four routines, and every one of them is wind or distance-scale code, not rendering**: `FUN_00a6f5d0` (advance every tree's wind state), `FUN_00a6f650` (set every tree's wind strength), `FUN_00a71250` (rescale every tree's LOD-distance block), `FUN_00a6f700` (copy every tree's wind shader constants into its geometry sub-structure — **unreferenced**, see 13.3). All labels below: **CONFIRMED — disassembly** unless stated. Scope of every negative is stated where it is made.

### 13.1 The manager and its per-frame entry

| Address | Role |
|---|---|
| `0x02706800` | manager object. `+0x00` list head (tree `+0x28`/`+0x30` are next/prev); `+0x04` byte "wind enabled" (ctor sets 1; no other writer among the nine references); `+0x08` float global strength; `+0x0C` **master wind object** (the same layout as the per-tree object in 13.2, `0x1A0` bytes); `+0x1AC` float wind clock (seconds) |
| `FUN_00a6f490` | manager ctor (from `0x01009575`): enabled = 1, strength = 1.0, master wind object = the defaults written by `FUN_00a6f830`/`FUN_00a6fd30` |
| `FUN_00a70ca0` | **per-frame entry** (called at `0x00702ce6` in `FUN_00702a50`, itself called at `0x00703f04`): advances the clock `0x02706014 += frame dt` (`0x0132a0ac`, 1/30 s default); if the global wind level `0x0130cbbc` changed, calls `FUN_00a6f650(level)`; stores the clock into the manager (`FUN_00a6f4e0`); runs `FUN_00a6f5d0` (advance the master, then each tree); then `FUN_00a0d7e0` (instance-distance culling, 13.6) |
| `0x0130cbbc` | global wind level, static value **0.1**; 4 references, **all reads** (`0x00851dc9` in the foliage sway code `FUN_00851b10`, and `FUN_00a70ca0`) — no direct writer exists **[HIGH CONFIDENCE that the shipped level is the constant 0.1; a pointer-mediated writer cannot be excluded]** |
| `FUN_00a6f500` | link: puts the tree at the list tail, then `FUN_00a6f920` stores the master's address into the tree's wind object (offset `+0xA8` of the object, i.e. tree block `+0xE0`) and `FUN_00a6f950` seeds the tree's strength from the manager's |
| `0x02706020` | **tree registry** (thiscall object; init `FUN_00a70f50`): 100 hash buckets + a 100-entry free chain, two-word keys at `+0x330` (8 bytes each), tree pointers at `+0x650` (4 each) — **capacity 100 trees** (foliage's is 64). Lookup `FUN_00a71050`/`FUN_00a71360` (appends `.csrt_pc` to the name) |

### 13.2 The `'TREE'` `+0x38…` table is an embedded wind-state object — RESOLVES §12 item 1 (CPU side)

The whole region `T+0x38 … T+0x207` (`0x1D0` bytes) is one object handed to `FUN_00a70410` (advance), `FUN_00a6fe70` (gust), `FUN_00a6fa20` (direction setter) and `FUN_00a6f950` (strength setter) as `this`. **Float index `k` lives at block offset `0x38 + 4k`** (§4's "table starting at `+0x3C`" missed `k=0` at `+0x38`, which is `5.0` in 11/11). The four routines' every memory access to `this` was tabulated exhaustively from the assembly (`tools/harnesses/tree_wind_access.py`, over the four whole function bodies): they read **all 38 file-loaded floats `k = 0x00…0x25`** (block `+0x38…+0xCF`); everything at `k ≥ 0x26` (block `+0xD0…+0x207`) is state the routines write, and **is zero on disk in 11/11 trees**.

**File-loaded parameters** (`k` → block offset → what the consuming code does with it):

| `k` | block | Decoded role | Population | Label |
|---|---|---|---|---|
| `0x00` | `+0x38` | **strength response time (s).** A strength change from *s0* to *s1* is scheduled to finish at `now + 0.5·k0 + abs(s1−s0)·0.5·k0`, i.e. between `0.5·k0` (tiny change) and `k0` (full swing); the current strength then follows a pure logistic S-curve `1/(1+e^−(12u−6))` over that interval (the code's linear-blend weight is the constant 0). Also the base time of every gust phase (13.2.1) | `5.0` in 11/11 | **[CONFIRMED]** formula; curve shape HIGH CONFIDENCE (the exponential is an unnamed CRT intrinsic) |
| `0x01` | `+0x3C` | **direction response time (s).** Direction change finish = `now + 0.5·k1 + 0.5·k1·(1 − (1+cos θ)/2)`, θ = angle between old and new direction (between `0.5·k1` and `k1`); the direction passes through the normalised midpoint of old and new, eased by the mean of the linear and the logistic curve | `2.5` (5 trees) / `1.0` (6) | **[CONFIRMED]** |
| `0x02` | `+0x40` | packed as **`1/k2`** (0 if `k2 == 0`) into constant c2.z. A length-like quantity (it is reciprocated) — role on the GPU OPEN | `20 / 50 / 70 / 100 / 120` | **[CONFIRMED]** packing; OPEN meaning |
| `0x03` | `+0x44` | copied to c2.w | `2` / `1.5` | **[CONFIRMED]** |
| `0x04` | `+0x48` | copied to c6.x | `0.1` / `0.3` | **[CONFIRMED]** |
| `0x05…0x14` | `+0x4C…+0x8B` | **four oscillator bands**, band `g = 0..3`: `5+4g` = amplitude at wind level 0, `6+4g` = amplitude at level 1, `7+4g` = frequency (Hz-like, per second of clock) at level 0, `8+4g` = frequency at level 1. Per frame each band's response is `osc_g = level^e_g` (the CRT power routine; `e_g` = `k = 0x20+g`), the *live* amplitude is `lerp(amp0, amp1, osc_g)` (into c2.x, c2.y, c4.x, c3.x for `g = 0, 1, 2, 3`), and the band's **phase accumulator** advances by `lerp(freq0, freq1, osc_g) · dt` (into c1) | see 13.4 | **[CONFIRMED]** the arithmetic; amplitude/frequency *names* HIGH CONFIDENCE (they feed a shader) |
| `0x15` | `+0x8C` | **gust onset rate.** Scaled ×0.01; while no gust is active a new gust starts on a frame when a uniform random in `[0, dt)` falls below it. Read **only** on the master-less path (13.2.1) | `0.1` (the four `st_com_*` trees **and `st_shrub_lrg`**) / `0.5` (the three pines and the other three shrubs) *(wording corrected 2026-09-20 from Team B's note; re-derived: `st_shrub_lrg` = 0.1)* | **[CONFIRMED]** use; **inert for trees** (13.2.1) |
| `0x16` | `+0x90` | × `osc_0` → c5.y | `5, 10, 2` | **[CONFIRMED]** |
| `0x17` | `+0x94` | copied to c5.z | `2` / `1` | **[CONFIRMED]** |
| `0x18`,`0x19` | `+0x98`,`+0x9C` | **gust target amplitude range** (random in `[k18, k19]`) — master-less path only | `(0.5, 1)` in the com set, `(0, 0.7)` elsewhere; lo ≤ hi 11/11 | **[CONFIRMED]** use; inert for trees |
| `0x1A`,`0x1B` | `+0xA0`,`+0xA4` | **gust plateau duration range (s)**, random in `[k1A, k1B]` | `(1, 5)` in the com set, `(0, 0)` elsewhere; lo ≤ hi 11/11 | **[CONFIRMED]** |
| `0x1C`,`0x1D` | `+0xA8`,`+0xAC` | × current wind level → c6.y, c6.z | `k1C = 0` in 11/11; `k1D ∈ {0, 0.7, 0.85}` | **[CONFIRMED]** |
| `0x1E`,`0x1F` | `+0xB0`,`+0xB4` | copied to c4.y, c4.z | `(10, 0.5)`, `(0, 0)` | **[CONFIRMED]** |
| `0x20…0x23` | `+0xB8…+0xC4` | **power exponents** `e_0…e_3` of the four bands (`level^e`) | `0.75 … 3` | **[CONFIRMED]** |
| `0x24`,`0x25` | `+0xC8`,`+0xCC` | copied to c3.y, c3.z | `(0.25,1.1)` / `(0.5,0.5)` | **[CONFIRMED]** |

Why the earlier "wind/sway and LOD" guess was half right: **wind, yes; LOD, no** — the tree LOD distances are in the geometry sub-structure (13.5), not in this table.

**Runtime state (written by the routines; zero on disk):** `k=0x26` current strength (block `+0xD0`); `0x27–0x29` current unit wind direction; `0x2A` pointer to the master (block `+0xE0`); `0x2B` previous clock, `0x2C` dt; `0x2D` current gust value (clamped 0…1); `0x2E` gust target; `0x2F` gust rise-end time, `0x30` gust end time, `0x31` gust start time, `0x32` gust start value, `0x33` gust plateau end; `0x34` strength target, `0x35`/`0x36` strength transition start/end time, `0x37` previous strength; `0x38–0x3A` target direction, `0x3B–0x3D` midpoint direction, `0x3E`/`0x3F` direction transition start/end, `0x40–0x42` old direction; `0x43–0x46` the four `osc_g`; `0x47–0x4A` the four phases; `0x4B` **wind level = min(strength + gust, 1)**; `0x4C–0x67` the output constants (13.3). **Overlaid at the tail of the block** (constructor-written, not part of the wind routines): `+0x1D8` the pooled render-template object (13.5), `+0x1DC` = 0, `+0x1E0` the physics-shape handle built by `FUN_00a713e0` (§8).

#### 13.2.1 Gusts are inert in the shipped configuration **[HIGH CONFIDENCE]**

Every tree is linked to the master (`FUN_00a6f500` stores the master's address; `FUN_00a6f920` refuses only a self-link). A linked tree starts a gust **only by copying the master's gust target** while the master's plateau has not ended; the tree's own `k15`, `k18`, `k19` are read only when the master pointer is null. The master's parameter block is the ctor default (`FUN_00a6f830`: onset `k15 = 0`, gust range `(0.5, 1)`, plateau `(1, 4)`), and the master's gust target starts at 0. The nine references to the singleton (13.1) form a **closed set** of functions that touch it (ctor, `FUN_00a6f650`, `FUN_00a6f4e0`, `FUN_00a6f5d0`, link/unlink, scale change), none of which rewrites the master's parameter block. So the master never starts a gust and no tree follows one: `level = strength` (0.1). *Scope:* the closed set is over **direct** references to the singleton's address; the singleton is never passed elsewhere in the code read.

### 13.3 The seven output `vec4` constants (`k = 0x4C…0x67`, block `+0x168…+0x1D7`)

`FUN_00a70410` rebuilds seven `float4`s every frame (the *disabled* path zero-fills all seven and then sets the five `1.0` defaults shown):

| vec4 | block | components | source |
|---|---|---|---|
| c0 | `+0x168` | (dir.x, dir.y, dir.z, 0) | current wind direction (disabled: (1,0,0,0)) |
| c1 | `+0x178` | (phase₀, phase₁, phase₂, phase₃) | the four phase accumulators |
| c2 | `+0x188` | (amp₀, amp₁, `1/k2`, `k3`) | disabled: (0,0,1,1) |
| c3 | `+0x198` | (amp₃, `k24`, `k25`, 0) | disabled: (0,0,1,0) |
| c4 | `+0x1A8` | (amp₂, `k1E`, `k1F`, 0) | |
| c5 | `+0x1B8` | (level, `k16`·osc₀, `k17`, 0) | disabled: (0,0,1,0) |
| c6 | `+0x1C8` | (`k4`, `k1C`·level, `k1D`·level, 0) | |

Fourth components other than c2.w are never written on the enabled path (always 0). **Where they go is OPEN:** `FUN_00a6f940` returns the address of this block and `FUN_00a6f700` copies it into every tree's geometry sub-structure at `+0xF8…+0x167` (28 dwords), but **neither function has a reference in the project's reference table other than `FUN_00a6f700` → `FUN_00a6f940`** (looked up on those two addresses only; an indirect call through a computed pointer would not be recorded), so the upload to the GPU was not found. What each constant *means to the vertex shader* needs the tree shader's constant layout (material shader hash `0x967A81C0`, §6) and was not pursued. The CPU side is complete: a reader can reproduce the constants exactly.

### 13.4 Derived steady-state values in the shipped configuration (level 0.1, no gust) — DERIVED, not measured

`osc_g = 0.1^e_g`; the 11 trees have **5 distinct parameter sets** (`tools/harnesses/tree_wind_effective.py`). Live values of the time-independent components (frequencies are per clock second; phases grow without bound between frames):

| trees | e₀…e₃ | osc | band frequencies g0..g3 | amp g0, g1, g2, g3 |
|---|---|---|---|---|
| `st_com_break/lrg/med/sml`, `st_shrub_lrg` | 2,2,2,2 | 0.01 ×4 | 0.298, 1.04, 0.793, 0.793 | 1.99, 3.99, 0, 0.101 |
| `st_pine_full`, `st_pine_xmas` | 2,3,1,0.75 | 0.01, 0.001, 0.1, 0.178 | 1.01, 7.01, 1.68, 10.33 | 0.015, 0.502, 0, 0.0178 |
| `st_pine_tall` | 2,3,1,0.75 | same | 1.01, 2.01, 1.68, 10.33 | 1.515, 0.502, 0, 0.0089 |
| `st_shrub_med`, `st_shrub_sml` | 2,2,1,0.75 | 0.01, 0.01, 0.1, 0.178 | 3.04, 6.04, 0.2, 10.33 | 0.01, 0.02, 0.03, 0.0053 |
| `st_shrub_nice_01` | 2,3,1,0.75 | same as pines | 1.01, 2.01, 1.68, 5.27 | 0.015, 0.502, 0.015, 0.0178 |

The five-tree set is the §4 "identical in 5 files" observation: it does **not** follow tree size (`st_shrub_lrg` shares it with the four `st_com_*` trees); no reason for the grouping is visible in the code.

### 13.5 Geometry sub-structure — what reads what (RESOLVES §12 item 2 partly, item 3 negatively)

Consumers examined: `FUN_00e7a5a0` (parser), **`FUN_00e7aae0` (the tree's GPU-side build — see 13.7)**, `FUN_00e5f3a0`/`FUN_00e5f9f0`/`FUN_00e5f290` (per-instance render object from the pooled template), `FUN_00e5ee60`/`FUN_00e5f7d0`/`FUN_00e5ef50`/`FUN_00e7a700`/`FUN_00e7a760`/`FUN_00e7a840` (LOD blend), `FUN_00a71250`, `FUN_00a6f700`, and **every method of the world-object class in the `0x00a0c000…0x00a0e800` range whose vtable is at `0x01177fcc`** (`FUN_00a0c930`, `a0d0f0`, `a0d5a0`, `a0d5d0`, `a0d700`, `a0d3d0`, `a0ce40`, `a0e0c0`, `a0dfa0`, `a0cbf0`, `a0d540`, `a0c740`/`a0c760`/`a0c7c0` and the property loader — the loader's property vocabulary is deliberately not documented here).

| Offset | Confirmed reader / role | Label |
|---|---|---|
| `+0x00`, `+0x10` | bounds min / max (`float4`, w = z). Read by `FUN_00a0d700` (object-bounds accessor; returns (−1,−1,−1)/(1,1,1) when no tree), `FUN_00e7aae0` and `FUN_00e5f3a0` (centre = min + half-extent, half-extent = (max − min)/2, radius = the half-extent length, stored in the per-LOD mesh object and in the instance render object) | **[CONFIRMED]** (was HIGH CONFIDENCE in §5) |
| `+0x20` | the **material handle table** (§7): `FUN_00e7aae0` overwrites every `u32` with a material-cache handle (13.5.1) | **[CONFIRMED]** |
| `+0x28` | material-set block; `+0x08` of it is the record-pointer array `FUN_00e7aae0` walks | **[CONFIRMED]** |
| `+0x30` | LOD slot count (4) | **[CONFIRMED]** |
| `+0x38` | presence entries → the Mesh sub-block pointers; `FUN_00e5f7d0` reads each populated slot's Mesh header `+0x04` (its group count *n*, `spec-vehicle-geometry.md` §11.9) to choose a group per frame | **[CONFIRMED]** |
| `+0x48` | per-material runtime objects (`n_mat × 8`, filled by `FUN_00e7aae0`) | **[CONFIRMED]** |
| `+0x50` | per-LOD-slot **mesh runtime objects** (`4 × 8`); each is initialised from that slot's Mesh sub-block (the GPU upload path) | **[CONFIRMED]** |
| **`+0xE0…+0xEC`** *(new)* | **four LOD distances A < B < C < D (metres, LOD-scale 1.0)**, 13.5.2 | **[CONFIRMED]** |
| `+0xF0`, `+0xF4` *(new)* | derived at load: `(B−A)·s`, `(D−C)·s` — **zero on disk 11/11** | **[CONFIRMED]** |
| `+0xF8…+0x167` *(new)* | destination of `FUN_00a6f700`'s 7 × `vec4` copy (dead code path, 13.3) | **[CONFIRMED]** for the write; the copy never runs as far as references show |
| `+0x40`, `+0x58`, `+0x5C` (count), `+0x60`, `+0x64`, `+0x68`, `+0x70` (array pointer) | **no reader found** (13.5.3) | HIGH CONFIDENCE (bounded negative) |

#### 13.5.1 The material handle table = material-cache handles

Both loops (tree `FUN_00e7aae0`, foliage `FUN_00747b50`) do, per material *i*: allocate a **material object**, point it at the *i*-th parsed inline material record (`obj+0x14`), call the material cache `FUN_00e3fcb0`, and store the result at `table[i]`. The cache **dedups by content hash**: the hash (`FUN_00e5e450`, a running checksum over the record's shader hash, flags, counts, the constant `vec4`s and, per sampler slot 0…15, the *resolved* texture binding) is bucketed `% 0x5000`; an identical material already cached returns the existing handle, otherwise a new pool entry is made. The returned handle is `(pool index & 0xFFFF)` — an index into a `0x14`-byte-stride entry pool at `0x02ac693c` — plus `0x10000` if record flags bit 0 is set (virtual query at slot `+0x20`; shipped flags `0xC2`/`0x40` never set it) plus `0x20000` if the other virtual query (slot `+0x18`, a constant stub) returns 1 (never). `0xFFFF` = failure. **The on-disk `u32`s are placeholders and are overwritten at load**: identity `0…n−1` in **11/11 trees and 19/19 foliage files** (`tools/harnesses/mat_handle_table_disk.py`). Draw code binds a material by `FUN_00e3f740(handle, 0)`, which validates `entry+0x10 == handle` and dispatches the entry's material.

#### 13.5.2 The four LOD distances `+0xE0…+0xEC`

`FUN_00a71810` scales the block in place at load: `A,B,C,D ← ×s`, then `+0xF0 = (B−A)s`, `+0xF4 = (D−C)s` (`FUN_00e7a7a0`), with `s = 0x0130cbc0` — a **graphics-quality distance multiplier** (default 1.0). `FUN_00a71250(new)` re-scales every tree in the list by `new/old` when it changes; it is called by `FUN_005dd2c0(level)` with **0.5 for level < 3, 1.0 for 3, 10.0 for 4, 10 000 for ≥ 5** (`FUN_005dd2c0` is called from `0x007cb539`, `0x007cb740`, `0x007ccf81`; what those UI/settings functions are was not traced). The world-object instance copies the block (`FUN_00e5ef50`: accepted only if `A < B < C < D`, `B−A ≥ 0`, `D−C ≥ 0` — the copy routine's own validity test `FUN_00e7a700`; it stores the squares) and evaluates, for squared camera distance *q*:

| distance | LOD parameter *t* (`FUN_00e5ee60`) |
|---|---|
| `q < A²` | `1` |
| `A² ≤ q < B²` | `1 − (q − A²)/(B² − A²)` (fades 1 → 0) |
| `B² ≤ q < C²` | `0` |
| `C² ≤ q < D²` | `−(q − C²)/(D² − C²)` (0 → −1) |
| `q ≥ D²` | `−1` |

`FUN_00e5f7d0` turns *t* into a per-slot group choice: *t ≥ 1* → group 0; *t ≤ 0* → the last group; in between `floor(n·(1−t))`, with a fractional blend weight (`FUN_00e5efe0`); *t = −1* → no group (culled). **`D` is also the tree's cull distance**: `FUN_00a0d5d0` hides an instance when its distance exceeds `D` (the per-instance table `FUN_00a0dce0` fills with `D²`, evaluated four at a time by `FUN_00a0d7e0`). Which parts of this each *label* implies (that A–B is the LOD-0 → LOD-1 crossfade, C–D the final fade) is HIGH CONFIDENCE from the function shapes; the *t* values and `D` = cull are CONFIRMED.

#### 13.5.3 The remaining fields — honest gap (item 2 remainder, item 3)

`+0x58` = material count − 1 (11/11, unchanged) and no reader was found for it or for `+0x40`, `+0x60`, `+0x64`, `+0x68`, `+0x70`, nor for the six 16-byte records. *Scope of that negative:* the parser, the GPU build, the pooled-template and instance-render-object routines, the LOD blend, the distance-scale routine, the dead constant-copy routine, and every method of the one world-object class that holds a tree object — all read in full. It is **not** a whole-binary exhaustion (a tree pointer held elsewhere is not excluded). Empirical structure, for whoever finds a consumer (`tools/harnesses/tree_rec16_structure.py`, `tree_wind_lod_gates.py`):

- `+0x64 ≥ bounds max.y` in 10/11 (equal in 5/11); `+0x68 ≥ bounds min.y` in 11/11 (equal in 9/11) — a vertical extent slightly larger than the box; `+0x60` a size scalar (3.1 … 35.9).
- **The three trees with `n16 = 6`** (`st_com_lrg`, `st_pine_full`, `st_pine_tall`) are also the only ones whose geometry sub-structure holds meaningful floats at `+0x80…+0xDC` (in the other eight this window is stale heap pointers and the build-tool string fragment). Those floats are **four `float3` corners of an axis-aligned rectangle in a plane of constant third coordinate** (`+0x80…+0xAC`; e.g. `st_com_lrg`: first two coordinates spanning [−13.08, 11.03] × [−12.27, 11.47], third = 13.59; 3/3 rectangles, tested), then `(a0,b0)` at `+0xB0`, `(a1,b0)` at `+0xB8`, four zero dwords `+0xC0…+0xCC`, `(a1,b1)` at `+0xD0` (`+0xBC == +0xB4` and `+0xD0 == +0xB8` in 3/3) and **`(a1−a0, b1−b0)` exactly at `+0xD8`, `+0xDC`** (3/3, to float precision; e.g. `(0.6958, 0.6841)`, `(0.8408, 0.3987)` → `(0.145, −0.2854)`). The values lie in 0…1, atlas-coordinate-like.
- The six 16-byte records are four floats `(a, b, c, d)`: `c` and `d` are constant within a tree, **`d` equals `+0xDC` (the `b1−b0` above) in 3/3**, and the `(a, b)` pairs form a **2 × 3 Cartesian grid with equally spaced rows** in `st_com_lrg` and `st_pine_full` (row spacing 0.2942 and 0.3115, identical to float precision within each tree); in `st_pine_tall` the three rows are equally spaced (0.3115) but the two columns differ by up to 0.0004 between rows (not an exact product).
- **HYPOTHESIS — unconfirmed:** an impostor/billboard atlas description (a reference cell plus a 2×3 set of view cells); no code reads it (the tree materials do bind the billboard textures `common_bb_d/n`, §6). Do not build on this.

### 13.6 The world-object class that holds trees

The instance class (`vtable 0x01177fcc`; its type-name string is returned by `FUN_00a0c730`) is **not documented here beyond its tree use**: 0xF8-byte instances in a pool of 2500 at `DAT_02699c9c`, `inst+0xE4` = the tree object (found through the registry `0x02706020`), `inst+0xE0` = the per-instance render object built from the tree's template (`FUN_00a0d0f0` → `FUN_00e5f9f0` + `FUN_00e5f3a0`), `inst+0xB4` = the physics body created on activation from the tree's shape handle `tree+0x1E0` (`FUN_00a0c930` → `FUN_007594b0`). `FUN_00a0d7e0` culls all instances four at a time against a structure-of-arrays of squared cull radii (`0x0268d8c0`, stride `0x50` per 4 instances) that holds `D²`.

### 13.7 Correction to §2 and to the 2026-09-12 note: where the GPU-side build actually is

§2's "a large GPU-side build follows (`FUN_00a713e0`)" was corrected on 2026-09-12 (`FUN_00a713e0` is the capsule → physics-primitive builder). **The GPU-side build is `FUN_00e7aae0`**, called from the ctor (`0x00a71882`) on a pooled **template object** (`FUN_00e7a9d0`, stored at `tree+0x1D8`): it walks the handle table (13.5.1), then each present LOD slot creates a mesh runtime object initialised from that slot's Mesh sub-block (the shared vertex/index upload path, `spec-vehicle-geometry.md` §11.9) and stamps it with the **tree-level** bounds (centre, half-extent, radius). The order at load is: material-reference block → `'TREE'` block → geometry sub-structure → (scale block, template, `FUN_00e7aae0`) → registry insert `FUN_005ce910` → list link → physics-shape build `FUN_00a713e0` (its handle to `tree+0x1E0`). **[CONFIRMED — disassembly.]**

### 13.8 Validation (tools/harnesses)

| Gate | Result |
|---|---|
| W1 `k ≥ 0x26` all zero on disk | **11/11** |
| W2 band frequency pair `k = 7+4g, 8+4g` ascending (decoded pairing) | **44/44**; control (pairing shifted by one float) **0/33** |
| W3 gust ranges `k18 ≤ k19` and `k1A ≤ k1B` | **11/11**, **11/11** |
| W4 exponents `> 0` | 44/44 (0.75 … 3) |
| W6 distinct parameter sets | 5 (groups of 5, 2, 1, 2, 1) |
| L1 geometry `+0xE0…+0xEC` strictly ascending A<B<C<D (the copy routine's validity test) | **11/11**; the same predicate at every other 4-float window in `+0xC0…+0xFC`: **≤ 3/11** |
| L2 `+0xF0`, `+0xF4` zero on disk | 11/11 |
| Handle tables identity on disk | 11/11 trees, 19/19 foliage |

Amplitude pairs (`k = 5+4g, 6+4g`) are **not** ordered (34/44 ascending **counting ties (`<=`); strictly `<` only 26/44** — Team B, re-derived by the orchestrator: the five `st_com_*`-group trees have decreasing amplitude, `2 → 1`, `4 → 3`); they are interpolation endpoints, not min/max, and no gate is claimed for them. The constants' *GPU meaning* is not validated by any of this.

### 13.9 Still open after this pass

1. Where the seven wind constants are uploaded (the only copy routine found is unreferenced), and their GPU-side meaning. 2. A consumer for geometry `+0x40/+0x58/+0x60/+0x64/+0x68/+0x70` and the six 16-byte records / `+0x80…+0xDC` block (13.5.3). 3. The capsule header dwords and the tall pine's trailing floats (item 4) — not re-examined. 4. What `FUN_007cb200`/`007cb610`/`007cca80` (the callers of the quality-scale setter) are. 5. Whether any writer other than the ctor exists for the manager's `+0x04` "wind enabled" byte and the level `0x0130cbbc` via a pointer.

## 14. Independent cross-check by Team B's tree reader (2026-09-20)

Team B's independent tree reader (a fresh build, 2026-09-20; 11 distinct trees / 28 copies) reproduced this document's wind-table and LOD results with 0 gate mismatches: `k>=0x26` zero on disk 11/11; band-frequency pairs ascending 44/44 with the one-float-shifted control failing; gust ranges 11/11; exponents `k0x20..0x23` positive 44/44; five parameter sets in groups 5/2/1/2/1; LOD distances `A<B<C<D` 11/11 with the same predicate at the 12 other 4-float windows in `+0xC0..+0xFC` scoring at most 3/11; `+0xF0/+0xF4` zero 11/11; exact EOF 28/28; and the §13.4 derived table reproduced from a spec-built steady-state function in 132/132 values (worst deviation 0.66%). **[CONFIRMED — empirical, two independent implementations.]** Their five small notes, none contradicting a result:

1. **The LOD blend is linear in SQUARED distance**, so at the midpoint of `A..B` the blend factor is about 0.55–0.61, not 0.5. *(Reported by Team B; not re-derived by the orchestrator — confirm against the LOD routine before relying on it.)*
2. §13.8's "34/44 amplitude pairs ascending" holds only counting ties; strictly `<` it is 26/44. **(Re-derived by the orchestrator: 34/44 and 26/44; corrected in place above.)**
3. §13.2's `k0x15` row wording ("0.5 for pines, shrubs") was loose: `st_shrub_lrg` has 0.1 (it belongs to the five-tree 0.1 set with `st_com_*`, consistent with §13.4). **(Re-derived; corrected in place above.)**
4. The one-float-shifted frequency control that scores 0 is the shift to `(8+4g, 9+4g)`; the *other* one-float shift, `(6+4g, 7+4g)`, scores 26/44 (it coincides with the amplitude pairs). **(Re-derived by the orchestrator: 0/44 and 26/44.)**
5. §13.5.2 does not say whether `+0xF0 = (B−A)·s` and `+0xF4 = (D−C)·s` use the scaled or unscaled `A,B,C,D`; Team B's reader exposes `+0xF0/+0xF4` raw from the file and does not recompute them. **[OPEN — unspecified in the current text.]**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): annotated the §3/§8 capsule labels with the §8 update's CONFIRMED — disassembly result, marked the §9 bookend note confirmed (`spec-vertex-format.md` §4.1), pointed the §9 and §12 item 6 channel-layout notes to `spec-vertex-format.md` §12.9/§12.10, and marked §12 item 5 partly resolved (`FUN_00e3e590` = `.clmesh_pc` MIDDLE parser).
