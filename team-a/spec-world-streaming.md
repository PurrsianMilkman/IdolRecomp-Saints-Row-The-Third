# Saints Row: The Third — Open-World Streaming Grid Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, follow-on target (flagged as ruled-out-but-not-investigated in `spec-mission-packages.md` §5)
**Scope:** `sr3_city_0.vpp_pc` / `sr3_city_1.vpp_pc` — the open-world city streaming system: tile naming/coordinate scheme, reliability, and what's actually inside a streamed bundle.
**Method:** Black-box extraction using the already-confirmed container format, cross-referencing every sub-format encountered against this project's existing specs. No disassembly this pass.
**Cleanroom compliance:** No decompiled code or internal identifiers appear below. Real file/entry names quoted throughout (e.g. `angels_crib`, `1018h0.str2_pc`) are ordinary shipped data (container directory entries), not code.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Overview and headline result: this is a large system, but reliability is excellent

`sr3_city_0.vpp_pc` (1,502,024,767 bytes, 1,033 entries) and `sr3_city_1.vpp_pc` (2,611,376,351 bytes, 1,516 entries) together hold the entire open-world city's streamable content — terrain, props, and every named building interior/landmark. Both archives are **fully raw/uncompressed at the top level** (flags `0x0`), and — this is the headline finding, since reliability was explicitly the thing worth checking — **every nested `.str2_pc` bundle sampled this pass (tile bundles, LOD-tier bundles, and named-landmark bundles alike) is a mode-(b) shared-stream container**, meaning **all of it decodes reliably, not just first entries**, per the already-established mode-(b) guarantee (`spec-vpp-container.md` §3.6). **[CONFIRMED — empirical, every sample checked, zero exceptions.]** This is one of the largest content categories in the game, and none of it is blocked by the parked mode-(a) container limitation that has constrained several other targets.

## 2. The coordinate grid

Most entries are named by a **4-digit zero-padded coordinate** (`CCRR` — 2-digit column, 2-digit row, e.g. `1018`, `0924`), each with an `.asm_pc` manifest and an `.str2_pc` content bundle (`1018.asm_pc` / `1018.str2_pc`). **[CONFIRMED — empirical, 360 such coordinate pairs found in `sr3_city_0.vpp_pc` alone.]** Scanning every coordinate-named entry across both archives: **columns range 03–22 (20 distinct values), rows range 01–24 (24 distinct values)** — a roughly 20×24 tile city grid. **[CONFIRMED — empirical, exact min/max across both archives.]** `sr3_city_1.vpp_pc`'s coordinate-named tiles (e.g. `2224`, `1101`) extend the same coordinate space rather than using a separate numbering scheme — the two archives are two halves of one continuous grid, not two independent ones. **[HIGH CONFIDENCE — inferred from the shared, non-overlapping-looking coordinate ranges; not independently proven via a real-world-position cross-check.]**

## 3. Per-tile content variants

Beyond the plain `CCRR.str2_pc` base bundle, each coordinate can have additional sibling entries following consistent suffix conventions:

- **`CCRRhN.str2_pc`** (`N` = 0–3, e.g. `1018h0.str2_pc` … `1018h3.str2_pc`) — additional content bundles per tile, **no `.asm_pc` manifest of their own**. **[CONFIRMED — empirical, naming pattern, 91 such entries in `sr3_city_0.vpp_pc`.]** Content differs meaningfully between `h0` and `h3` on the one tile checked in full — not the same props at different quality, but different specific named props entirely (e.g. `h0` held lamp posts/light fixtures/drainage pieces, `h3` held fencing/utility-tower pieces) — ~~**so "LOD tier" is a plausible but not confirmed reading; a per-tile spatial subdivision or prop-category batching reads equally well from the one sample checked.** **[HYPOTHESIS — the `h`-suffix grouping is real and consistent; its exact meaning (LOD vs. spatial vs. category split) is not determined.]**~~ **[RESOLVED 2026-09-20, agent AA — §10: `hN` is fine-grid cell N (a 2×2 quarter) of tile `CCRR`, a container of the engine kind "Zone (High LOD)" holding the full-detail LOD-0 level meshes that overlap that quarter; the tile bundle holds the `~L1` versions. It is a location, not a quality step and not a prop-category batch. The differing "named props" of h0 vs h3 are simply different quarters of the tile. Also corrected: the hN records are not manifest-less — they are records inside the parent tile's own `.asm_pc` (§10.2).]**
- **`CCRR^<name>.asm_pc` / `.str2_pc` / `.str2_pc`+`hN`** (e.g. `1018^planecrash`, `1015^megab1`, `1015^megab2`) — a **named special set-piece or sub-area** anchored to a specific tile, getting its own manifest and its own `h0`–`h3` variants, just like a full tile. **[CONFIRMED — empirical, naming pattern; 70 base + 46 LOD-suffixed entries in `sr3_city_0.vpp_pc`.]** Names read as real, specific in-game locations (a plane-crash wreckage site, two "megab" — plausibly "mega building" — set-pieces).

## 4. Named landmark/interior bundles (the largest category by entry count)

The majority of non-coordinate entries (512 of 673 "other" entries in `sr3_city_0.vpp_pc`) are **named building interiors and landmarks**, each with its own `.asm_pc`/`.str2_pc` pair, completely independent of the coordinate grid's naming. Real examples found: `angels_crib` (a player-owned property, 14.3 MB / 1,673 entries — by far the richest single bundle sampled), `smiling_jacks`, `sw_planetsaints`, `sw_rimjobs_1`/`_2`, `bank_m1^bank`, `int_dt_ps2`, `sbe^bridge`. **[CONFIRMED — empirical.]** A large sub-family follows a systematic `_a_<2-letter-type>_<2-letter-direction>_<NN>[_modal]` pattern (e.g. `_a_sn_nw_03_modal`, `_a_tb_sw_01`, `_a_es_ne_03_modal`) — the direction codes (`nw`/`sw`/`ne`) matching city-quadrant compass directions — consistent with a library of **reusable, procedurally-placed generic ambient building interiors**, distinct from the one-off named landmarks. ~~**[HIGH CONFIDENCE — inferred from the systematic naming; the 2-letter type codes' specific meanings were not decoded.]**~~ **[CORRECTED 2026-09-20, agent AA — §10.8: the `_a_<type>_<region>_<NN>` bundles are not generic ambient interiors; they are the zones of the game's *activity instances* (container kind "mission", zone type 6) with a `_modal` companion (kind "mission model data") holding the activity's `.xtbl` table and conversations. The second code is the hood-region group, the first code one of ten activity mechanics; the letters are not parsed by the code examined.]** Many entries (both landmark and generic) carry a `_modal` suffix — plausibly marking an interior that's loaded as a self-contained instanced space (entering swaps to it) rather than being part of the seamless exterior world, matching the "modal"/dialog-like connotation of the word. **[HYPOTHESIS — plausible reading from the name alone, not confirmed against loading behavior.]**

## 5. What's inside a bundle — mostly already-solved formats, a few genuinely new ones

Every `.str2_pc` bundle sampled (a base tile, both `h0`/`h3` LOD-style variants, and the large `angels_crib` landmark) is internally organized the same way: a flat list of named sub-assets, each following the established `c`-prefix (CPU-side) / `g`-prefix (GPU-side) paired-file convention already documented elsewhere in this project. **What's already solved:**

- **`.cvbm_pc`/`.gvbm_pc`** — confirmed, this is the already-documented `.cpeg_pc`/`.gpeg_pc` texture-pair format (`spec-texture-format.md` §7) — used pervasively here for prop textures, and for a **per-tile minimap texture** (`minimap_1018.cvbm_pc`/`.gvbm_pc`), a genuinely new confirmed use of this format.
- **`.cefct_pc`** — particle effects, already documented (`spec-effects-format.md`) — found inside `angels_crib` (e.g. `vfx_slotmachine_dth.cefct_pc`, `vfx_fruitflys.cefct_pc`).
- **`.asm_pc`** — every manifest here (per-tile and the archive-wide `stream_grid.asm_pc`, a single 8.2 MB / 1,812-record manifest covering the whole archive's content collectively) uses the exact same confirmed format as `spec-asm-format.md`, including the same fixed magic/version and record shapes — a per-tile `.asm_pc` describes exactly one sibling entry, matching the already-documented single-reference record shape exactly. **[CONFIRMED — empirical, header magic/version/record-count checked directly.]**

**What's genuinely new, found here for the first time, and not decoded this pass:**

- **`.czh_pc` / `.czn_pc` / `.gzn_pc`** — since documented in full: `spec-terrain-format.md`. Summary: `.czh_pc` is a material/prop reference list (the shared material block again, split into ground-texture and foliage-mesh-source groups); `.czn_pc` holds multi-region chunk metadata (real, readable `"Region<NNN>"` labels found); `.gzn_pc` is the geometry payload, showing the same 16-byte-header-then-index-buffer shape as `.gcmesh_pc`/`.glmesh_pc` — vertex-attribute layout still open there, same as those two. *[Since resolved: the per-vertex attribute layout is decoded in `spec-vertex-format.md`.]*
- **`.clmesh_pc` / `.glmesh_pc`** — since documented: `spec-geometry-format.md` §4.1. Confirmed structurally analogous to `.ccmesh_pc`/`.gcmesh_pc` (material block on the CPU side, likely index buffer on the GPU side); vertex-attribute layout still open, same as `.gcmesh_pc`. *[Since resolved: the per-vertex attribute layout is decoded in `spec-vertex-format.md`.]* Also seen with a `~lN` per-object suffix (e.g. `mathmatikron~l1.clmesh_pc`, `mathmatikron~l2.clmesh_pc`) that looks like a genuine per-mesh LOD-level convention, distinct from the tile-level `hN` suffix in §3.
- **`.rig_pc`** — found once in `angels_crib` (`doubledoor01.rig_pc`, 312 bytes) — plausibly an animation rig/skeleton reference for an animated prop (a door). Not investigated. *[The `.rig_pc` format has since been documented: `spec-rig-format.md`.]*
- **`.lightmult_pc`** — found once in `angels_crib` (`fireflicker_001.lightmult_pc`, 514 bytes) — plausibly a lighting-parameter/multiplier asset for a named light effect. Not investigated.
- **`sr3_city.grid_pc`** — see §5.1, resolved in a follow-up pass.

### 5.1 `sr3_city.grid_pc` — record layout resolved; it's a flat name/ID directory, not a spatial lookup

A follow-up pass fully parsed this file's record structure. **Header:** 16 bytes, four `u32` fields read as `4, 5, 1, 1` — `4` is very likely a section count (see below; 4 real sections were found). **Preamble:** one special, differently-shaped leading entry (`awld_compact`, a 12-byte name plus a handful of extra bytes not fully accounted for) — plausibly a world/map identifier, not part of the regular record stream. **[CONFIRMED — empirical, exact byte layout.]**

**The regular record shape, confirmed by parsing hundreds of real records cleanly:**
```
u8  marker (always 1)
u16 section_id
u16 index          (see below — meaning differs by section)
u16 name_count
repeated name_count times:
    u16 name_length
    char name[name_length]   (no null terminator)
```
**[CONFIRMED — empirical, parsed mechanically across all 4 sections, exact byte accounting verified against real known names.]**

**Four sections, each with real, identifiable content:**

- **Section 1** (52 entries) and **Section 2** (41 entries) — **named landmark/interior directories**, matching the bundle names from §4 exactly (`angelslobby`, `bank_m1`, `saints_hq`, `angels_crib`, etc.). `index` here is a **plain sequential counter starting at 0** within each section — not a coordinate or spatial value. Entries with multiple in-game variants (destroyed states, sub-rooms) carry more than one name per record — e.g. the `bank_m1` record holds all four of `bank_m1`, `bank_m1^bank`, `bank_m1^bank_destroy`, `bank_m1^stilwater` together as one 4-name entry, and `saints_hq` holds 5. **[CONFIRMED — empirical, every name cross-checked against real `.str2_pc`/`.asm_pc` bundle names from §4.]** A `u32` field appears between section 1 and section 2 whose value (`41`) exactly matches section 2's real entry count — **[CONFIRMED — empirical, exact match]** — consistent with a "next section's entry count" separator, though this specific mechanism did not clearly generalize to the section 2→3 transition (see below), so it should not yet be assumed to be the general rule.
- **Section 3** — **the base coordinate-tile directory.** Here `index`'s two bytes are **not** a little-endian counter — they are the tile's **column and row values directly** (byte 1 = column, byte 2 = row), and the record's one name is the plain `"CCRR"` tile string — confirmed exactly on a real example: the record for tile `0423` has index bytes `(0x04, 0x17)` = column 4, row 23 (`0x17` = 23 decimal), matching the name digit-for-digit. **[CONFIRMED — empirical, exact match, spot-checked on a real record.]** This directly answers "how do coordinates map into this file": for grid tiles (as opposed to named landmarks), the mapping is trivial and direct — the coordinate *is* the name, and the index field redundantly repeats it in binary form.
- **Section 4** — **the tile-variant (`hN`/`^`-suffix) grouping directory**, tying directly back to §3's naming scheme: e.g. one record groups `1607h0`, `1607^3count1h0`, `1607^3count2h0` together as a 3-name entry — confirming that a tile's `hN` LOD-style bundle and its co-located named sub-area's matching `hN` variant are treated as one logical group by the streaming system. **[CONFIRMED — empirical, direct name match against §3's documented convention.]** ~~Section 4's `index` values are large (observed in the thousands) and don't obviously continue any simple counter from the earlier sections — **[OPEN / UNKNOWN — not resolved.]**~~ **[RESOLVED 2026-09-20, agent AA — §10.6(e): the index is the fine-cell address `(fine_row << 8) | fine_col` with `fine_col = 2·col + (N & 1)`, `fine_row = 2·row + (N >> 1)`; 437/437 groups, all member names agreeing, controls 0/422 for the wrong models. Section 3's index is the same encoding one level up, `(row << 8) | col`, 468/468.]**

**The important honest finding for "how names map to world positions":** **this file does not map landmark names to coordinates or world positions.** Sections 1–2 (the actual named landmarks — the harder, more interesting case) carry only a bare sequential ID alongside each name/name-group; there is no coordinate, position, or grid-tile reference attached to a landmark's own record anywhere in this file. **[CONFIRMED — empirical, both sections fully parsed, no such field present.]** Whatever ties `angels_crib` to a specific place in the world is stored elsewhere (very likely embedded directly in world/prop-placement data, or resolved by a different mechanism entirely, e.g. a script-driven trigger volume) — **not** in this index. This file's real role looks like a flat **name-to-small-integer-ID directory** (the same general shape as the fixed lookup tables in `.asm_pc`, `spec-asm-format.md` §3) — useful for resolving a name to an internal ID quickly, not for spatial lookup.

**What's still open:** the exact section-boundary/transition mechanism (a large, only-partly-explained gap was found between section 2 and section 3 that a simple "next section's count" separator doesn't fully account for), the preamble entry's exact field layout, and ~~section 4's index-value scheme~~ [resolved, §10.6(e)].

## 6. Open Items

1. **`.czh_pc`/`.czn_pc`/`.gzn_pc` — since documented, see `spec-terrain-format.md`.** Material/foliage references, multi-region metadata, and geometry payload all sketched; the geometry payload's own vertex-attribute layout remains open (needs disassembly, same as `.gcmesh_pc`). *[Since resolved: the per-vertex attribute layout is decoded in `spec-vertex-format.md`.]*
2. **`.clmesh_pc`/`.glmesh_pc` — since documented, see `spec-geometry-format.md` §4.1.** Confirmed structurally analogous to `.ccmesh_pc`/`.gcmesh_pc`; vertex-attribute layout still open there too. *[Since resolved: the per-vertex attribute layout is decoded in `spec-vertex-format.md`.]*
3. **`.rig_pc`** and **`.lightmult_pc`** — each seen once, not investigated at all. *[`.rig_pc` format since documented in `spec-rig-format.md`; `.lightmult_pc` still not investigated.]*
4. **`sr3_city.grid_pc`'s binary layout — substantially resolved, see §5.1.** The record shape and all 4 sections' semantics are confirmed (two landmark-name directories, a coordinate-tile directory, a tile-variant grouping directory). The honest, load-bearing finding: **this file does not do the spatial name→position mapping it was hoped to** — landmark records carry no coordinate/position field at all, only a bare sequential ID. Whatever actually places a named landmark in the world is a different, still-unlocated mechanism. What's left here specifically: the section 2→3 transition/boundary mechanism, the preamble entry's fields, and ~~section 4's index-value scheme~~ [resolved, §10.6(e)].
5. ~~**The `hN` suffix's exact meaning** (§3) — LOD tier, spatial subdivision, or prop-category batch are all plausible from the one tile checked; not distinguished.~~ **RESOLVED 2026-09-20 (agent AA) — §10.** `hN` = fine cell N of the tile's 2×2 subdivision, engine container kind "Zone (High LOD)", full-detail (LOD 0) level meshes; three independent legs (engine kind/pool names in code; the runtime level-4 grid at twice the tile resolution; the shipped grid file's cell addresses 437/437) plus population checks. Still open there: what re-triggers the load/unload reconciliation each frame (§10.4).
6. ~~**The `_a_<type>_<dir>_<NN>` generic-interior naming scheme's type codes** (§4) — the systematic pattern is confirmed, the individual 2-letter codes' meanings are not.~~ **ADVANCED 2026-09-20 (agent AA) — §10.8.** The names are activity-instance zones, not generic interiors; the second code equals the game's hood-region prefix (data-confirmed for `nw`/`dt`); the first code is one of ten activity mechanics identified from each `.xtbl`'s element vocabulary; the code examined never splits or tables the letters. Still open: display names of the ten types (`activity_types.xtbl` was not located as a top-level archive entry) *[since found: `spec-tables-progression.md` §14.16 read it from `misc_tables.vpp_pc` (19 activity-type names with the DLC1 copy); mapping the ten 2-letter codes to those names is still open]* and whether the region letters are compass sectors.
7. **Whether the two archives' coordinate ranges are truly non-overlapping halves of one grid**, or whether there's a more complex relationship (e.g. `sr3_city_1.vpp_pc` holding override/patch content for the same coordinates) — inferred from the ranges looking disjoint in the samples checked, not exhaustively verified.
8. **`stream_grid.asm_pc`'s full 1,812-record content** was not enumerated — confirmed to use the established `.asm_pc` format, but not cross-checked entry-by-entry the way the smaller `sr3_city_missions.asm_pc` was in `spec-mission-packages.md`.

None of these gaps block the core deliverable: the tile/coordinate/landmark naming scheme is now clearly documented, reliability is confirmed excellent (mode-(b) throughout, no blocked content found), every bundle's contents can be enumerated and its already-solved sub-formats (textures, effects, manifests) extracted with full confidence, and `sr3_city.grid_pc`'s own record structure is now decoded — only the terrain and static-prop-mesh formats *(their vertex layouts since decoded: `spec-vertex-format.md`)*, and the still-unlocated mechanism that actually places named landmarks at world positions, remain open.

## 7. The zone streaming subsystem — the category table (2026-09-11, user-authorised)

**The zone streaming subsystem registers a fixed table of named streaming categories at
startup.** One ~7.6 KB function builds each entry as a small stack struct — `{u32 id,
const char *name, …}` — and hands it to one of **seven** ~147-byte registration
helpers. **41 registration calls**, **40 ids** recovered spanning **`0x01`–`0x29`** (38
distinct), and **34 names**. This function was explicitly left unopened by the
2026-09-10 ctorless pass; it is opened here under direct user authorisation.

### 7.1 The category table

| id | name | id | name |
|---|---|---|---|
| `0x01` | Vehicle slots | `0x16` | effect preload |
| `0x02` | Vehicle Fully Cust slots | `0x17` | *(name not recovered; see §8.1: `decal mempool`)* |
| `0x03` | Character slots | `0x18` | debug_superzone |
| `0x04` | Character high res slots | `0x19` | test_level_mip_streaming |
| `0x05` | CS High Res slots | `0x1A` | **zone always loaded** |
| `0x06` | Player slots | `0x1B` | always loaded textures |
| `0x07` | item preload | `0x1C` | **medium lod level allocator** |
| `0x08` | cutscene | `0x1D` | **high lod level allocator** |
| `0x09` | zscene | `0x1E` | mip_streaming |
| `0x0A` | customization streaming | `0x1F` | **interiors** |
| `0x0B` | *(name not recovered; see §9.4: `customization logo`)* | `0x20` | modal gameplay |
| `0x0C` | Customization compositing | `0x21` | large modal gameplay |
| `0x0D` | *(name not recovered; see §9.4: `cust shaderball slots`)* | `0x24` | Texture compression scratch |
| `0x0E` | Compositing render target | `0x25` | morph preload mempool |
| `0x0F` | *(name not recovered; see §9.4: `Small smesh slots`)* | `0x26` | level container cache |
| `0x10` | Large smesh slots | `0x27` | *(name not recovered; see §9.4: `vehicle_customization_camera(s)`)* |
| `0x11` | weapon high res gpu | `0x28` | **zone header cache** |
| `0x12` | time of day | `0x29` | *(name not recovered; see §9.4: `TOD_luts`)* |
| `0x13` | interface streaming | | |
| `0x15` | interface image gpu compacting | | |

**[CONFIRMED — disassembly.]** Ids and names are literals in the registration code, so
the pairing is exact rather than inferred: the name always sits 4 bytes after the id in
the same struct, which is what makes the extraction reliable.

**Ids `0x14`, `0x22` and `0x23` do not appear** in this function. They may be registered
elsewhere, or unused. **[OPEN.]** *→ closed §8.6 / §9.2.*

**Two ids are registered twice** — `0x19` (test_level_mip_streaming) and `0x1E`
(mip_streaming) — each via a *different* helper of the seven. So a category can be
registered through more than one helper, which means the helpers are not
one-per-category and probably distinguish *kind* of registration (a pool versus a
policy, or a fast/slow variant). **[OPEN — what the seven helpers do differently.]** *→ closed §8.3 / §8.4.*

### 7.2 What this gives a reimplementation, and what it does not

**Gives:** the zone-relevant categories are now named and numbered, and four of them
bear directly on zone loading — `zone always loaded` (`0x1A`), `zone header cache`
(`0x28`), `interiors` (`0x1F`), and the **paired LOD level allocators** `medium lod`
(`0x1C`) and `high lod` (`0x1D`). The `.czn_pc` type split observed in the resource
table (`Zone (fast)` / `Zone (slow)`, and `Zone header (fast)` / `Zone header (slow)`)
sits alongside a category set that separates *headers* from *content* and *medium* from
*high* LOD — consistent with the split being a loading policy rather than a format
difference, as the inventory already recorded.

**Does not give:** any per-category budget. The struct carries further immediates that
look like capacities and allocation sizes, and a **first extraction attempt at those was
discarded** — it paired "most recent string" with "last small number" and
cross-associated fields between adjacent registrations, reporting an allocation
belonging to `cloth sim runtime cpu` against `time of day`. Sizes are therefore **not**
published here. Attributing them correctly needs the seven helper bodies read, so the
field order inside the struct is known rather than guessed. **[OPEN.]** *→ closed §8.2 / §8.5 / §9.4.*

**Separately, a small set of named memory pools is created through a different, directly
readable call** taking `(buffer, size, alignment, name, …)`. Only **15** such call sites
exist binary-wide, and one is in this function. Read exactly, they are: `renderlib heap`
(`0xA00000`), `renderlib dynamic materials` (`0x80000`), `renderlib material block map`
(`0x20000`), `light volumes` (`0x1E0000`), `undergrowth` (`0x60000`), `cloth sim runtime
cpu` (`0x60000`), `RL_skin large bone pool` (`0xC00`), `RL_skin small bone pool`
(`0x60`), `Deformation weights` (`0x39000`), `Sr3_gds_mempool` (`0x40000`),
`synced_resource` (`0x1D000`), `dc_states` (`0x10`). **[CONFIRMED — disassembly, literal
arguments at each call site.]** These are *not* the streaming categories above — a
different mechanism that happens to share the subsystem.

Harness `zone_pools.py`; dumps `tools/stream_full.txt`, `tools/stream_pools.txt`,
`tools/zone_module.txt`.

### 7.3 A negative worth recording about the `.czn_pc` payload

`spec-zone-data-format.md` §2 refuted the uniform `{id, length}` chunk walk, noting the
walk stops on a word that is not id-shaped, **commonly a `0xFFFB`-family value**. That
value is **not literal-checked anywhere in the binary**: every site where `0xFFFA`–
`0xFFFE` appears as an immediate is an unrelated bitmask or constant, none in zone code.
**So the stopping word is not a sentinel or a terminator — it is ordinary data being
misread**, which is consistent with the walk having already left the top level and
entered a section interior. **[CONFIRMED — disassembly, whole-binary immediate scan.]**
*Predicate stated: this rules out a literal comparison against those five values. It
does not rule out a computed or masked test.*

## 8. The seven registration helpers, the double-registration mechanism, and per-category budgets (2026-09-12)

Continues §7 under the same user authorisation. §7.1/7.2 left three items open: what the
seven helpers do differently; the per-category budgets (deliberately unpublished after a
first extraction cross-associated fields); and ids `0x14`, `0x22`, `0x23`. All three are
addressed below. **Corrections stay visible in place**: §8.4 corrects §7.1's framing of
the two double-registered ids rather than editing §7.1 itself.

### 8.1 Method: replaying the writes, one field per pass, two independent replays

Per HANDOFF.md §26.22: a first budget extraction read "the most recent string" next to
"the last small number" and cross-associated fields between adjacent registrations. The
fix used here is the decompiler's own stack-slot arithmetic: a field `N` bytes into the
same struct is always the local variable whose numeric suffix is `N` less, so a sibling
is computed, never guessed from proximity. Two **independent** replays were built and
cross-checked against each other and against hand-verified offset arithmetic:

- **`tools/harnesses/strm_replay.py`** — replays every assignment statement in the full
  1,028-line decompilation of `FUN_005d58f0` (`tools/stream_full.txt`) in address order,
  tracking a symbolic value per local variable and re-deriving each of the seven
  helpers' two call arguments (the id/name struct, and a second "descriptor" argument)
  at the exact call site.
- **`tools/harnesses/strm_disasm_replay.py`** — replays the **raw instruction stream**
  (`StrmDisasm.java`'s output, one line per instruction with Ghidra's own frame-relative
  stack offsets), independent of the decompiler's variable naming entirely.

**Two plumbing bugs found and fixed while building the first replay, kept here as the
methodology lesson this pass earns:**
1. The Ghidra headless log's trailing `(GhidraScript)` tag is not reliably on the same
   physical line as the statement it tags — some lines wrap it onto its own following
   line, console-width dependent, not content dependent. A first version anchored its
   assignment regex on `;\s*$` and silently lost every value on a line long enough to
   wrap, under-reporting real findings as "unresolved." Fixed by stripping the tag from
   every line before any pattern sees it, gated by a known-positive assertion.
2. **The replay's own bookkeeping reproduced HANDOFF.md §26.22's exact trap one level up.**
   A stack slot reused by a registration's own `if`/`else` branches (one branch writing
   a struct field directly, the other leaving it to the shared setter function
   `FUN_00dd5fd0`) was read by "direct value if present, else the setter's" — which
   silently preferred the **other branch's stale leftover** over the real value for this
   branch, fabricating a `0x0` budget where the true value (confirmed by hand) is
   `0x40000`. The fix: track the **line number** of the most recent write from *either*
   mechanism and take whichever is more recent, never "whichever mechanism" by default.
   Caught only because the alignment was cross-checked against a hand-derived value
   before trusting it — exactly the discipline this task requires.

The second (disassembly-only) replay independently reproduces the **first 14** of the 41
calls' id/name pairs exactly (`0x25`→`0x6`→`0x11`→`0x12`→`0x19`→`0x1c`→`0x1d`→`0x1b`→
`0x1e`→`0x1a`→`0x1f`→`0x26`→`0x28`→`0x4`, in call order) — a genuine cross-validation from
a route that cannot inherit the first replay's bugs, since it never touches decompiler
variable names at all. It could not be extended to the remaining 27 calls: Ghidra's own
stack-frame annotation (present on the first 14 calls, which use a push-argument idiom)
is absent on the rest, which use a direct-store-to-stack-slot idiom the original analysis
pass evidently did not annotate this deep into a 7,594-byte function — a bounded,
stated tool limitation, not a finding about the format. **[CONFIRMED — disassembly, for
the struct layout and the first 14 id/name pairs; CONFIRMED — disassembly via the
decompile-text replay, cross-checked by hand against raw instructions, for the rest.]**

**A third, independent recovery**: reading the raw instructions directly (not either
replay engine) at address `0x005d74fe` shows a store of the literal value `0x17` to the
stack at offset `+0x4bc`, immediately followed by a store of the literal string
`"decal mempool"` to the stack at offset `+0x4c0`, then a call to `FUN_005d5440`.
**Id `0x17`'s name is `decal mempool`** — a 35th recovered name,
where §7.1 recorded 34. The decompiled pseudocode this project's tables were built from
does not surface it as a clean sibling assignment (both replays report it unresolved);
it is plainly visible in the disassembly. **[CONFIRMED — disassembly.]**

### 8.2 The registration struct's field layout

Every one of the 41 registration calls builds the **same** struct shape, confirmed from
three independent sources agreeing on every offset: the shared budget-field setter
`FUN_00dd5fd0`'s own 158-byte body (writes its four numeric arguments to fixed offsets
of its first argument); the shared finisher `FUN_00dd64b0` (reached by all seven
helpers), which indexes a global table by the struct's own `+0x00` field and reads an
allocator pointer from `+0x08`; and, independently, at least four *inline* registrations
(`time of day`, `Player slots`, `weapon high res gpu`, `level container cache`) that set
the identical offsets by direct assignment instead of calling the shared setter — the
same offsets recovered twice, by two different code shapes:

| offset | field | evidence |
|---|---|---|
| `+0x00` | category id (`u32`) | already established; every write pairs with `+0x04` |
| `+0x04` | name (`char*`) | already established |
| `+0x08` | allocator/"class" pointer | `FUN_00dd64b0` reads the second argument's third dword (`+0x08`) as a pointer to an object whose own vtable slot `+0x38` is then used |
| `+0x0C`,`+0x0D` | flag bytes, set `1` once budget fields are written | `FUN_00dd5fd0` |
| `+0x10` | CPU-side pointer (an allocation result, or a bump-allocator address) | `FUN_00dd5fd0`; matches the result of a `FUN_00dad460` call assigned directly to this field in the `time of day` row |
| `+0x14` | **CPU size** | `FUN_00dd5fd0` param 2; matches the inline value `0x1400` assigned directly to this field in the same row |
| `+0x18` | CPU alignment | `FUN_00dd5fd0` param 3; matches the inline value `0x80` assigned directly to this field |
| `+0x1C` | GPU-side pointer | `FUN_00dd5fd0` writes `DAT_02a3c580+DAT_02a3c584`; matches the result of a `FUN_00ea3362` call assigned directly to this field |
| `+0x20` | **GPU size** | `FUN_00dd5fd0` param 4; matches the inline value `0x200000` assigned directly to this field |
| `+0x24` | GPU alignment | `FUN_00dd5fd0` param 5; matches the inline value `0x80` assigned directly to this field |

**[CONFIRMED — disassembly.]** This is the layout that makes §8.5's budget table possible:
CPU/GPU size live at fixed, struct-relative offsets regardless of which of the seven
helpers is used or whether the setter function or a direct assignment wrote them.

One further mechanical fact from `FUN_00dd64b0`: it zeroes the entry indexed by the category id in the global array at `0x02A3C170`,
i.e. **category ids are used as a direct index into a global array** — consistent with
the enum being sparse-but-dense-ish over `0x01`–`0x29` rather than a hash key, and
relevant to §8.6.

### 8.3 What the seven helpers do differently

All seven share one skeleton (confirmed in `helpers.txt`, all read together): allocate a
fixed-size object through the id-struct's own allocator pointer (`+0x08`, vtable slot
`0x38`), stamp a **distinct vtable pointer per helper** (seven distinct vtable
addresses — a distinct C++ class per helper, not a shared class with a parameter), call
one small "configure" function, then call the shared finisher `FUN_00dd64b0`. The
allocated object size and the configure callee are what differ:

| helper | object size | configure callee | what the callee does |
|---|---|---|---|
| `FUN_005d5440` | `0x50` (80 B) | `FUN_00dd6ed0` | Reads CPU/GPU ptr+size+align (`+0x10`…`+0x24`); if the struct's `+0x20` count is `>0`, additionally drives a named-pool creation through the allocator's `+0x90` slot. The general "sized pool, optional secondary named allocation" shape. |
| `FUN_005d54e0` | `0x64` (100 B) | `FUN_00dd7ce0` | Reads CPU/GPU **alignment** from the struct, then calls `FUN_00dd5c00` passing the **second helper argument's own first two dwords** (not the struct's own size fields) as the size input — i.e. this helper's size is conventionally supplied through the second argument, not `+0x14`/`+0x20` (§8.5 explains why several `54e0` rows show `0` there). |
| `FUN_005d5580` | `0x4c` (76 B) | `FUN_0045a180` | Hashes the name (`FUN_00dd5640`); if `+0x24` (GPU align) is `≥0x10`, allocates **two** further auxiliary objects (`0x60` B, `0xb8` B) through the same allocator and drives a GPU-side call using the second argument's slot 1. |
| `FUN_005d5620` | `0x4c` (76 B) | `FUN_00dd5ce0` | Bails out immediately if the allocator pointer (`+0x08`) is null; otherwise calls the allocator's `+0x38` slot **with no size arguments at all**. Never reads `+0x10`…`+0x24`. This is the "no real budget, placeholder" helper — see §8.4. |
| `FUN_005d56b0` | `0x4c` (76 B) | `FUN_00dd6620` | Does **not** read the struct's CPU/GPU fields either. Takes the second helper argument's own first dword as an **already-constructed object pointer**, tags a flag bit on it, and just copies the category name into the new wrapper. This is the "adopt an externally-built allocator" helper (§8.7). |
| `FUN_005d5750` | `0x50` (80 B) | `FUN_00459d30` | Same shape as `FUN_00dd6ed0`/`FUN_005d5440`'s callee (reads `+0x10`…`+0x24`, optional secondary named allocation), textually near-identical. |
| `FUN_005d5850` | `0x198` (408 B) | `FUN_00dd7050` | Same field reads as `FUN_005d5440`'s callee, **plus** — only for this helper — an extra initialiser `FUN_005d5810` runs on success, which zeroes and links a **fixed 16-slot, 20-byte-entry sub-pool** embedded in the object (head pointer, count`=0`, capacity`=16`). The largest and most structurally distinct of the seven. |

**[CONFIRMED — disassembly, all seven bodies and all seven configure callees read in
full.]** Reading the seven side by side answers the task's framing directly: the helpers
are **not** one-per-facet of a category (not "CPU vs GPU", not "primary vs variant
pool") — they differ in **how the size/allocator information reaches the new object**:
three read the struct's own CPU/GPU fields directly (`5440`, `5750`, and — via a
size-through-the-second-argument route — `54e0`, `5580`); one deliberately reads no size
at all (`5620`); one adopts an object built elsewhere rather than sizing anything itself
(`56b0`). `5850` is a superset of the "reads its own fields" shape with an added
sub-pool. Population: **41 calls, all seven helper bodies, all seven configure callees —
every one read, none sampled.**

### 8.4 The two double-registered ids — corrected framing

§7.1 recorded: *"Two ids are registered twice — `0x19` … and `0x1E` … each via a
different helper of the seven … a category can be registered through more than one
helper."* That is imprecise in a load-bearing way. Reading both call sites in full:

The registration struct's id field (`+0x00`) is set to `0x19` and its name field (`+0x04`) to the literal string `"test_level_mip_streaming"`. Depending on the tier argument: when it is `0`, the remaining fields are left at their defaults (or explicitly zeroed) and the placeholder helper `FUN_005d5620` is called on the struct; otherwise the real setter `FUN_00dd5fd0` populates the struct's CPU/GPU size fields — given a real, tier-computed size among its inputs — before the sized-allocation helper `FUN_005d5580` is called on the same struct.

And, independently, structurally identically for `0x1E` / `"mip_streaming"`: when the tier argument is `0`, only the placeholder helper `FUN_005d5620` is called on that registration's own struct; otherwise `FUN_00dd5fd0` populates it before `FUN_005d5580` is called on it — the same two-branch shape as the `0x19` case above.

**These are not two simultaneous registrations of one id. Each id is registered exactly
once per run, through exactly one of two mutually-exclusive branches on a single runtime
parameter (the *tier argument*, the sole argument to `FUN_005d58f0`) — the same parameter that
gates several quality-tier budget branches elsewhere in the function** (observed values
`0`, `2`, `4`; anything else falls through unhandled — see §8.8). The "41 calls" count is
a count of **call sites present in the compiled code**, not a count of registrations
that execute on any one run.

**Both ids use the identical helper pair** — `FUN_005d5620` when the tier argument is 0,
`FUN_005d5580` otherwise — which, per §8.3, is exactly the "no size at all" helper versus
the "hash the name, allocate a real sized backing store" helper. **So the two-helper
split is a real-allocation-vs-placeholder distinction, gated by the same runtime
parameter that selects a texture/streaming quality tier elsewhere in this function, not
a per-category "two facets" or "primary plus variant" split.** Each id's
tier-argument-0 branch registers *test_level_mip_streaming*/*mip_streaming* with **no
backing store** (a debug/no-op registration — matches the "test_level" name); the other
branch computes a real size from the same tier-selection block that also feeds
`debug_superzone` (§8.5). **[CONFIRMED — disassembly; corrects §7.1's framing, which
is left in place above as the record of what was believed before this pass.]**

### 8.5 Per-category budgets — alignment demonstrated before publication

**Method, restated as the task requires:** every figure below is read from the **same
call event** as its id and name — the same struct pointer, the same line/instruction —
never assembled by proximity. Values are shown only where a clean literal was replayed;
where the source computes an address (a pointer into an already-allocated region) rather
than a byte count, or where the value is a call's return (unrecoverable without
executing the allocator), the field is marked **unresolved**, per the constraint against
publishing a guess. Several rows are tier-dependent on the tier argument; where hand-verified,
all three observed tiers (`0`, `2`, `4`) are given.

**The required cross-check, done first:** `0x1A` (*zone always loaded*) and `0x28`
(*zone header cache*) do **not** have similar budgets, which is what the task asked to
verify before publishing anything further. `0x1A`'s CPU budget is `0xA00000` (10 MiB) at
tier `0`, `0xCE5400` (~13.5 MiB) at `2`, `0x793000` (~8 MiB) at `4` — set by direct
field assignment, scaling with the tier. `0x28`'s budget is a **flat `0x200000`** (2 MiB)
at every tier, and — structurally distinct from `0x1A` — is not built from the struct's
own CPU/GPU fields at all: it is the return value of a **dedicated, named allocator
call** (the named-allocator routine `FUN_00db52d0` with name "zone header cache" and size `0x200000`), passed in as the helper's second
argument. Different magnitude (4–7×), different mechanism, same category table. That is
the shape a correct alignment produces; two rows that happened to collide would be the
signal to distrust the extraction, not to publish it.

| id | name | helper(s) | CPU budget | GPU budget | tier-dependent? |
|---|---|---|---|---|---|
| `0x06` | Player slots | `54e0` | `0x8000` (32 KiB) | `0x6000` (24 KiB) | no (flat) |
| `0x11` | weapon high res gpu | `5440` | `0x4000` (16 KiB) | `0x190000` (1.5 MiB) | GPU align only (table lookup) |
| `0x12` | time of day | `5440` | `0x1400` (5 KiB) | `0x200000` (2 MiB) | no |
| `0x15` | interface image gpu compacting | `5750` | `0x80000` (512 KiB) | `0xC00000` (12 MiB) *(corrected §9.5: flag-gated)* | no *(corrected §9.5)* |
| `0x19` (real branch) | test_level_mip_streaming | `5580` | `0x40000` (256 KiB) | `0x3F00000` (~63 MiB, clamped) | yes — only exists when the tier argument ≠ 0 |
| `0x19` (default branch) | test_level_mip_streaming | `5620` | `0` | `0` | only when the tier argument is 0 |
| `0x1E` (real / default) | mip_streaming | `5580` / `5620` | same shape as `0x19` | same shape | same split |
| `0x1A` | zone always loaded | `5440` | `0xA00000`/`0xCE5400`/`0x793000` (tier `0`/`2`/`4`) | unresolved (computed address) | **yes** |
| `0x1C` | medium lod level allocator | `54e0` | via 2nd arg: `0x404000` (tier 0) / `0x1EC400` (tier 2 & 4) | — *(extended §9.6)* | **yes** |
| `0x1D` | high lod level allocator | `54e0` | via 2nd arg: `0x100000` (tier 0) / `0x61800` (tier 2 & 4) | — *(extended §9.6)* | **yes** |
| `0x1F` | interiors | `5440` | `0x200000`/`0xE1000`/`0xE1000` (tier `0`/`2`/`4`) | unresolved (computed address) | **yes** |
| `0x1B` | always loaded textures | `5440` | `0x20000` (128 KiB) | unresolved | **only registered at all when the tier argument is 4** — see below |
| `0x20` | modal gameplay | `5850` | `0x120000` (tier 0) / `0x110000` (tiers 2,4) | `0x2E0000` / `0x260000` | yes |
| `0x21` | large modal gameplay | `5850` | `0x320000` (tier 0) *(tiers 2/4 extended §9.6)* | unresolved for 2/4 *(extended §9.6)* | yes |
| `0x26` | level container cache | `54e0` | `0xB0000` (704 KiB) | linked value `0x16000` (88 KiB) via 2nd arg | no |
| `0x28` | zone header cache | `56b0` | `0x200000` (2 MiB), via dedicated named allocator | — | **no (flat)** |
| `0x17` | decal mempool | `5440` | unresolved this pass | unresolved | — |

**[CONFIRMED — disassembly]** for every value above (each hand-verified against the raw
struct-offset arithmetic, not just the replay's printed output — the `0x1C`/`0x1D`/
`0x19`/`0x1E` rows specifically, since those are exactly where the replay's two bugs
(§8.1) were caught and fixed). **Population for the table: 17 of the 41 call events**
where a clean literal (or a fully-traced tier-branch set of literals) was recoverable;
the remaining 24 are genuinely unresolved — either because the value is a computed
pointer/address rather than a byte count (`debug_superzone`, several others), or because
it is a call's return value with no accompanying literal (several `54e0`/`5440` rows).
**Those 24 are reported as unresolved, not guessed**, per the task's constraint — the
replay tooling (`tools/harnesses/strm_replay.py`) can be re-run against any of them if a
future pass adds register/call-return tracking.

**A conditional registration worth flagging on its own:** `0x1B` (*always loaded
textures*)'s single call site is reached only when the tier argument equals 4, with **no**
alternative branch — at every other observed tier this category is **not registered at all**. The
static "41 calls" count includes this site regardless; whether it executes depends on
the tier argument.

### 8.6 The three absent ids — `0x14` is dead code, `0x22`/`0x23` are absent entirely

**`0x14` — prepared, never registered.** The full body of `FUN_005d58f0` contains
exactly two writes of `"Interface slots"` paired with id `0x14` (address `0x005d735b` in
the raw instructions), immediately preceded by two real allocator calls
(a `FUN_00dad460` allocation of `0x85000` bytes with alignment 4, and a second `0x400`-byte allocation through the
struct's own allocator vtable) — i.e. the code goes as far as **carving out real memory**
for this category. No call to any of the seven helpers references that struct's address
anywhere in the function: exhaustively, by text search of the full 1,028-line
decompilation (zero hits), and spot-checked against the raw instruction stream by
computing the absolute stack address at the write site and confirming it matches **none**
of the addresses pushed as arguments to the nearest subsequent helper call (`FUN_005d54e0`
at `0x005d73e3`, four dwords off). **[HIGH CONFIDENCE — inferred: the decompile-text
search is exhaustive over the one known registration site; the instruction-level check
is a spot-check of the nearest candidate call, not an exhaustive re-derivation of every
address for all 41 calls.]** `0x14` is dead/vestigial code, not a gap in a sparse enum
and not evidence of registration elsewhere.

**`0x22` and `0x23` — absent, not merely unregistered.** Unlike `0x14`, **no** id/name
pair, no allocator call, and no partial setup for these values appears anywhere in
`FUN_005d58f0`'s full decompilation — a direct text search for `0x22`/`0x23` used as an
id-shaped assignment (immediately followed by a name-string sibling) returns zero
matches, where the same search returns `0x14`'s two matches cleanly. This rules out "dead
code, like `0x14`" as an explanation.

The task's other stated hypothesis — registered through a **different caller** of one of
the seven helpers — was checked directly and **bounded**, not via a whole-binary scan.
`FUN_005d56b0` is the only one of the seven with a second caller (`FUN_00db01a0`, 725
bytes; found earlier via `helpers.txt`'s own caller lists — exactly the "read a named
entry point's own call list" method this project's methodology calls for). Decompiled in
full: it registers **exactly one** category, id `0xFE`, name `"GSA_DESTUB_ALLOCATOR"`,
wrapping an object built by the named-allocator routine `FUN_00db52d0` with name "stream2 dynamic container allocator"
and size `0x20000`. **`0xFE` is far outside the `0x01`–`0x29` range this category table
occupies** — this is evidently the same generic registration infrastructure reused for
an unrelated debug-stub allocator, not a second entry point into the streaming category
enum. A literal scan of `FUN_00db01a0`'s own instructions for `0x14`/`0x22`/`0x23` as
scalar operands finds only unrelated stack-frame displacements (`[ESP+0x14]`), never an
id-shaped write. **[CONFIRMED — disassembly, for what this specific, bounded search
covered: the only known registration site plus the only known second caller of any of
the seven helpers.]**

**Net for `0x22`/`0x23`: OPEN.** *[Closed §9.2: CONFIRMED gaps in a sparse enum.]* What was checked and came back negative: the main
registration site (exhaustive), and the one other known caller of a shared helper
(exhaustive for that function). What was **not** checked, and is explicitly out of
scope for the bounded-search method this project uses (a whole-binary immediate scan is
the disallowed alternative): whether some other, not-yet-identified call site reaches
`FUN_00dd64b0` or one of the seven helpers directly. Concrete next step: enumerate every
caller of `FUN_00dd64b0` itself (the shared finisher all seven helpers funnel through) —
a small, bounded set, not yet read this pass — since a hypothetical third call site
would have to reach it exactly as `FUN_00db01a0` does. Absent that, the honest reading is
that these two ids are **most likely gaps in a sparse enum** (matching `spec-format-inventory.md`'s
already-documented pattern of skipped ids elsewhere in this binary's registration
tables, e.g. resource types 6 and 28), but this is not yet a closed question.

### 8.7 How the budgets interact — mechanism-level answer, physical sharing still open

Three distinct **mechanisms** for conveying a budget were found in §8.3/§8.5, and the
zone-relevant categories split across them cleanly:

- **`0x1A` (zone always loaded) and `0x1F` (interiors) share a mechanism**: both use
  helper `FUN_005d5440`, both set CPU/GPU fields by **direct assignment** rather than
  through the shared setter, and both scale with the same tier set (`0`/`2`/`4`).
  **[CONFIRMED — disassembly.]** Whether they draw from the *same physical buffer* is
  **OPEN** — no shared base-pointer variable was found connecting their two
  tier-selection blocks; each computes its own address/DAT reference independently.
- **`0x28` (zone header cache) uses a third, distinct mechanism**: a dedicated named
  allocator call with a flat, tier-independent size, via helper `FUN_005d56b0` (the
  "adopt an externally-built allocator" helper, §8.3). It is mechanically **separate**
  from `0x1A`/`0x1F` in every respect checked — different helper, different sizing
  method, different tier behaviour. **[CONFIRMED — disassembly.]**
- **`0x1C` (medium LOD) and `0x1D` (high LOD) share a mechanism with each other**: both
  use helper `FUN_005d54e0`, both route their real size through the **second** helper
  argument (a pointer to a computed value) rather than the struct's own `+0x14`/`+0x20`
  fields (which are explicitly zeroed), and both feed the same downstream sizing call
  (`FUN_00dd7ce0` → `FUN_00dd5c00`). **[CONFIRMED — disassembly.]** They do **not**
  share a mechanism with `0x1A`/`0x1F`/`0x28`. Whether they share a physical pool with
  *each other* is **OPEN**: their computed sizes (`0x404000` vs `0x100000` at tier 0)
  come from two independently-computed locals with no shared base pointer found between
  them either.

**Net: three separate mechanisms, no confirmed physical pool-sharing between any pair.**
The mechanism-level grouping is itself informative — it is exactly the "headers vs
content, medium vs high LOD" policy split `spec-format-inventory.md` and §7.2 already
inferred from the *name strings* and the `Zone (fast/slow)` type split, now independently
confirmed from the *code path* each category takes. Closing the physical-sharing question
would need the allocator vtable's own implementation (the `+0x38`/`+0x90` slots called
throughout this section), which is a further, larger investigation outside this pass's
bound.

### 8.8 Open items and concrete next steps

1. **`0x22`/`0x23`**: enumerate `FUN_00dd64b0`'s full caller list (bounded, not yet done)
   to close the "different caller" hypothesis completely. If that list contains only
   the seven helpers, the sparse-enum reading is confirmed. *[Closed §9.2.]*
2. **24 of 41 budget rows unresolved** (§8.5) — recoverable in principle by extending
   `strm_replay.py` to follow allocator-call chains (e.g. `FUN_00dad460`'s own first
   argument almost always **is** the size, just one hop further than this pass's
   resolver follows) rather than marking any call-derived value opaque. Cheap, bounded
   follow-on. *[Addressed §9.4.]*
3. **The tier argument's real-world meaning** — observed values `0`, `2`, `4`, branched
   explicitly; other values fall through unhandled at several sites (§8.4, §8.5). The
   pattern (tier `0` consistently gives the *largest* budget, `2`/`4` smaller) is
   consistent with `0` meaning "streaming disabled, load generously" versus `2`/`4`
   being active-streaming presets — **[HYPOTHESIS — unconfirmed]**, not traced to a
   caller of `FUN_005d58f0` this pass.
4. **Physical pool-sharing** between `0x1A`/`0x1F` and between `0x1C`/`0x1D` (§8.7) —
   needs the allocator vtable implementation, out of this pass's bound.
5. **The disassembly-only replay's coverage gap** (14 of 41 calls, §8.1) — the remaining
   27 use a direct-store argument-passing idiom that Ghidra's existing stack-frame
   analysis does not annotate; re-running analysis with a higher stack-depth limit on
   just this function (rather than the whole binary) may close this without a full
   re-analysis. *[Closed as a dead end for this technique, §9.3.]*

**Tooling this pass:** `tools/scripts/StrmBudgetFns.java` (decompiles the shared
setter/finisher/configure-callee functions), `tools/scripts/StrmSecondCaller.java`
(decompiles and literal-scans `FUN_00db01a0`), `tools/scripts/StrmDisasm.java` (fixed —
had an uncompilable illegal escape sequence left from before the rate-limit interruption
this task resumed from; never actually run until this pass), `tools/harnesses/strm_replay.py`
and `tools/harnesses/strm_disasm_replay.py` (the two independent replays, §8.1).

## 9. Closing the 24 unresolved budget rows, the 0x22/0x23 caller-list check, and the stack-annotation coverage gap (2026-09-12, second pass)

Continues §8 under the same authorisation, per §8.8's own open-items list (items 1, 2, 5
addressed in full; item 3 given a partial, honestly-bounded answer; item 4 confirmed out
of bounds). New Ghidra project copy `tools/gp_strm2` (robocopy `/E` of the `.rep` plus the
`.gpr` marker file, 466 MB / 9 files / 0 failures). New scripts prefixed `Strm2`, new
harness work done by direct, hand-verified reading of the existing `tools/stream_full.txt`
dump against a **freshly-built, complete offset→variable map taken from the function's own
declaration block** (see §9.1) rather than by extending the automated replay blindly —
the replay's own naming assumption turned out to be the single largest source of the
prior pass's "unresolved" cells, and fixing the reader by hand first, on paper, made it
possible to see exactly what an automated fix needs to do before spending more script time.

### 9.1 Two tooling defects found in `strm_replay.py`, one new mechanism confirmed, one refinement to §8.2

**Defect A — the sibling-name lookup assumes a shared variable-name PREFIX that Ghidra does
not actually guarantee.** `strm_replay.py`'s `sibling()` helper reconstructs a sibling
field's name by keeping the base variable's own prefix (`local_`, `uStack_`, etc.) and
substituting the arithmetic-derived hex suffix. Ghidra's decompiler instead names each
stack slot by **the C type it infers for that slot**, independently per slot — an `int`
field gets one prefix family, a `char *` field another, regardless of what the
**neighbouring** field is named. Every id/name pair straddles exactly this boundary (the id
is numeric, the name is `char *`), so whenever the two fields happen to receive different
type-based prefixes, the sibling lookup silently returns nothing. **This, not a missing
literal, is why ids `0xF`, `0xD`, `0xA` and `0xB` showed as name-unresolved in the prior
pass's table** despite the name being written in plain sight two lines below the id.
Verified by building the complete offset→name map directly from `FUN_005d58f0`'s own
~230-line local-variable declaration block (every declared local from `+0x56c` down to
`+0x4c`, read in full, not sampled) and looking up each sibling offset in that map instead
of by string surgery on the base variable's name. **[CONFIRMED — disassembly.]** This map
is the real fix; a future automated pass should build it from the declaration block once
per run rather than pattern-matching names.

**Defect B — "most recent write wins" silently collapses a multi-way branch to whichever
value's source line comes last in the decompiled text, with no record that a branch was
even present.** This is not the already-documented tier-branch bug from §8.1 (which the
prior pass caught and fixed for the four rows it hand-verified) — it is the **general
form** of the same trap, and it was still live for every row that pass did not hand-check.
Two concrete examples, both caught by re-reading the full source around the write site
rather than trusting the printed value:
- `debug_superzone` (`0x18`)'s GPU field is written inside a three-way tier-argument ladder (tier 2 / 4 /
  0) (values `0x45fd800` / `0x52e5000` / `0x9a84000`); the naive replay reports
  only the last (`0x9a84000`) with no indication the other two exist.
- `interface image gpu compacting` (`0x15`, **already published as CONFIRMED** in §8.5)'s
  GPU value is gated not by the tier argument at all but by an unrelated boolean flag
  (`DAT_0149365c`): the base value is `0xa00000`, overridden to `0xc00000` only when that
  flag is set. The published row states a flat `0xC00000` unconditionally — **this is
  corrected in §9.5 below, left visible rather than silently edited.**

**Mechanism confirmed — helper `FUN_005d54e0`'s second-argument descriptor carries BOTH
sizes, not one.** §8.3 already stated (from reading the configure callee, `FUN_00dd7ce0`)
that this helper's real size is "conventionally supplied through the second argument… the
second helper argument's own first two dwords." What had not been carried through into the
budget table is that those two dwords are not "a size, repeated" — reading
`FUN_00dd7ce0`'s full body again with this specifically in mind: it reads the id/name
struct's own CPU alignment (`+0x18`) and GPU alignment (`+0x24`) fields, then calls the
downstream sizing routine (`FUN_00dd5c00`) with **the descriptor's first dword paired with
the CPU alignment, and the descriptor's second dword paired with the GPU alignment** — i.e.
**descriptor dword 0 = CPU size, dword 1 = GPU size**, end to end. **[CONFIRMED —
disassembly, `FUN_00dd7ce0` read in full.]** This single fact is what closes the GPU column
for `0x1C`/`0x1D` (previously "—") and unlocks the majority of §9.4's new rows below, all of
which use this same helper with the struct's own `+0x14`/`+0x20` fields explicitly zeroed.

**Refinement to §8.2/§8.3, not a correction — the two "allocator" functions are generic,
not CPU/GPU-dedicated.** §8.2 read `FUN_00ea3362` as "the GPU-side pointer" allocator
because the one row checked at the time (`time of day`) used it to fill the GPU pointer
field. Reading every call site of `FUN_00ea3362` (24 total) and `FUN_00dad460` (8 total) in
the function shows both are **plain sized-allocator wrappers** (`alloc(size, align, …)` /
`alloc(size, align)`) used for **whichever** pointer field — CPU or GPU — the surrounding
code happens to be filling; `level container cache` (`0x26`) uses `FUN_00ea3362` to fill
its **CPU** pointer, for example. **[CONFIRMED — disassembly, exhaustive over both
functions' call sites within `FUN_005d58f0`.]** The task's own named technique — "an
allocator call's first argument is almost always the size" — holds for both functions
equally; which struct field it feeds (and therefore whether that size is CPU- or
GPU-relevant) has to be read from the **assignment target's own offset**, not the
allocator's identity. Where a row's struct field is written **directly** with the same
literal the allocator receives (the common case, e.g. `0x11`/`0x12` already published),
the allocator call is redundant confirmation. Where the struct field is **explicitly
zeroed** and only the allocator call carries a nonzero literal (seen repeatedly below,
first at `0xF`), the two numbers can legitimately disagree — see the explicit caution in
§9.4.

### 9.2 `0x22`/`0x23` — CLOSED. `FUN_00dd64b0`'s full caller list contains only the seven known helpers

§8.8 item 1's concrete next step, executed exactly as specified: `Strm2Dd64b0Callers.java`
enumerated `FUN_00dd64b0`'s callers two independent ways — the API's own
`Function.getCallingFunctions()`, and a raw scan of every `Reference` in the program whose
target is `FUN_00dd64b0`'s entry point, filtered to call-type references. **Both methods
agree exactly: 7 callers, and they are the seven already-known helpers
(`FUN_005d5440`/`54e0`/`5580`/`5620`/`56b0`/`5750`/`5850`) — nothing else.** Population:
every function in the ~41,785-function binary that contains a `CALL` instruction targeting
`0x00dd64b0` — not a sample, not bounded to a subset, the complete set by construction
(this is a query against one function's own reference list, not a whole-binary heuristic
scan, so it carries none of this project's documented no-selectivity risk). Control: two
structurally different enumeration routes (a pre-built call-graph edge vs. a raw
reference-table scan) landing on the identical 7 addresses is the alignment check itself —
had a call site existed that Ghidra's function-boundary analysis failed to attribute to a
containing function, the two counts would have disagreed (7 vs. 8, or same-7-different-8th).
They did not. **A failing case would have been an 8th address in either list, or the two
lists disagreeing on which 7.** Neither happened.

**Conclusion: the sparse-enum reading is CONFIRMED, not merely "most likely."**
`FUN_00dd64b0` is reached **only** through the seven helpers; there is no third entry
point into the streaming-category registration surface anywhere in the binary. Combined
with §8.6's already-exhaustive text search of the one known registration site
(`FUN_005d58f0`) and the one known second caller of any helper (`FUN_00db01a0`, registering
the unrelated `0xFE` debug-stub allocator), **every route by which `0x22`/`0x23` could be
registered has now been checked and is negative.** `0x22` and `0x23` are gaps in a sparse
enum, matching the already-documented pattern elsewhere in this binary's registration
tables (resource types 6 and 28) — **[CONFIRMED — disassembly]**, upgraded from §8.6's
"most likely… not yet a closed question."

### 9.3 The disassembly-only replay's coverage gap — bounded, and now closed as a dead end for this specific technique

§8.8 item 5 named a concrete experiment: re-run analysis with a higher stack-depth limit,
scoped to `FUN_005d58f0` only. Two steps, in order, per this project's "reach for the named
lever, don't guess" rule:

1. **Recon (`Strm2ListAnalysisOptions.java`):** dumped every one of the 130 analysis option
   names currently registered for this program. Population: all registered options, not a
   guess at likely names — the full set the `Options` API exposes for
   `Program.ANALYSIS_PROPERTIES`. Result: the only `Stack.*` options are `Stack` (on/off),
   `Stack.Create Local Variables` (bool), `Stack.Create Param Variables` (bool), and
   `Stack.Max Threads` (an integer, but for **parallelism**, not depth). **No option
   resembling a distance/depth limit exists for either the `Stack` analyzer or the `x86
   Constant Reference Analyzer`** (the other candidate that creates references from
   constant/scalar stack-relative operands). **[CONFIRMED — disassembly: this is a direct
   dump of the program's own registered option set, not an inference.]**
2. **Experiment, not just recon (`Strm2RestackOneFunc.java`):** since no depth parameter
   exists to raise, the next-cheapest literal reading of "try re-running analysis… on just
   this function" is to force a scoped one-time re-run of the two candidate analyzers
   (`Stack`, `x86 Constant Reference Analyzer`) against `FUN_005d58f0`'s body only, with
   their existing default settings, and count `StackReference`-annotated instructions
   before and after. **Before: 266. After: 266. Delta: 0.** The predicate this measures:
   "does re-running the relevant analyzer, scoped to just this function, change the number
   of stack-relative references it records" — a failing case (evidence the technique
   works) would have been any nonzero delta, even a small one. None appeared.

**Conclusion: this specific technique is closed, honestly, as a dead end — [CONFIRMED —
disassembly].** There is no exposed stack-depth lever in this Ghidra version to raise, and
a bare re-run with the existing settings (scoped or not) is deterministic and changes
nothing, ruling out "stale/one-off analysis" as the explanation for the gap. This narrows,
rather than answers, the open question: the coverage split (14 push-idiom calls annotated,
27 direct-store-idiom calls not) is a property of **which listing-level `Reference` objects
get created for a `MOV [ESP+const], value` instruction versus a `PUSH value` instruction**,
separate from the decompiler's own stack-frame/local-variable model (which is demonstrably
complete for this function end-to-end — every one of the ~230 declared locals, including
ones physically at the end of the 1,028-line decompilation, is correctly named and
offset-addressed, so the *underlying* frame analysis has not degraded or run out of budget
anywhere in this function). **Next step, if resumed:** manually annotate the 27 call
sites' argument-establishing `MOV` instructions with `CreateStackReferenceCmd` (or
equivalent) rather than hunting for an automatic-analysis setting, since the frame model
itself is already complete and correct — the gap is specifically in reference-object
creation, not in stack-offset knowledge.

### 9.4 The 24 unresolved budget rows — resolved by hand-tracing the source against the offset map, not by blind script extension

**Method, matching §8.5's own bar exactly:** every value below comes from reading
`tools/stream_full.txt` directly against the offset→name map built in §9.1, the same
discipline §8.5 used ("hand-verified against the raw struct-offset arithmetic, not just the
replay's printed output"). Where a helper is `FUN_005d54e0`, the size is read via the
now-confirmed descriptor mechanism (§9.1); where a row's own struct fields are written
directly, those are used instead, with the two cross-checked against each other wherever
both exist. **Every tier-branched value is reported for all three observed tiers (`0`,
`2`, `4`) where the branch was found** — no row below reports only "whichever branch's
text came last," the exact trap named in §9.1. Rows are grouped by outcome.

**Required alignment check, done first, per this task's own instruction:** `0x09`
("zscene") and `0x24` ("Texture compression scratch") are both plain `FUN_005d5440` rows
with a clean CPU literal and an explicit **zero** GPU field — and they are **not** the
same number (`0x200000` vs. `0xbb8000`), confirming the extraction is not defaulting to one
constant regardless of category. Separately, `0x04` ("Character high res slots") and `0x05`
("CS High Res slots") land on the **identical** descriptor pair (`0x9800` CPU / `0x280000`
GPU) — checked deliberately because an identical pair is exactly the shape a
cross-association bug would produce. It is not one here: both values are read from
**physically separate, non-overlapping declaration sites** (`+0x31c`/`+0x318` for `0x04`
vs. a distinct pair at `+0x354`/`+0x350` for `0x05`, set independently a few hundred bytes
apart in the source), and a "character" high-res pool and a "cutscene" high-res pool
sharing one design constant is an unremarkable real coincidence, not a shared variable. A
failing case for either check would have been two structurally-unrelated categories
resolving to the same value **through the same variable** — that did not happen.

**Newly resolved, clean (single value or fully tier-scoped, CPU and GPU both accounted for):**

| id | name | CPU budget | GPU budget | tier-dependent? |
|---|---|---|---|---|
| `0x04` | Character high res slots | `0x9800` (38 KiB) | `0x280000` (2.5 MiB) | no |
| `0x05` | CS High Res slots | `0x9800` (38 KiB) | `0x280000` (2.5 MiB) | no |
| `0x09` | zscene | `0x200000` (2 MiB) | `0` (no GPU budget) | no |
| `0x01` | Vehicle slots | `0x96000` (t2/4) / `0x200000` (t0) | `0` (no GPU budget) | yes |
| `0x03` | Character slots | `0x9800` (t2/4) / `0x14000` (t0) | `0xfa000` (t2) / `0x112000` (t4) / `0x200000` (t0) | yes |
| `0x07` | item preload | `0x70000` (t2/4) / `0xa0000` (t0) | `0xb20000` (t2) / `0xcc0000` (t4/t0) | yes |
| `0x16` | effect preload | `0x203000` (t2/4) / `0x300000` (t0) | `0x77e800` (t2) / `0x984800` (t4) / `0xaf0000` (t0) | yes |
| `0x0F` | Small smesh slots | `0x800` (2 KiB) | `0xb400` (45 KiB) | no |
| `0x10` | Large smesh slots | `0x800` (2 KiB) | `0x2b000` (172 KiB) | no |
| `0x0A` | customization streaming | `0x450000` (4.5 MiB), single value via dedicated named allocator | — | no |
| `0x0B` | customization logo | `0x80000` (512 KiB), single value via dedicated named allocator | — | no |
| `0x27` | vehicle_customization_cameras **[spelling conflict: the call literal quoted below this table reads `vehicle_customization_camera`; not re-checked]** | `0` (dedicated named allocator called with a literal zero size) | — | no |

**[CONFIRMED — disassembly]** for all twelve rows above: each is a direct literal (or a
directly-read tier-selected literal) at the exact struct/descriptor offset the mechanism
in §9.1 establishes, re-derived from the source rather than carried over from the prior
pass's replay printout. `0x0A`/`0x0B`/`0x27` additionally recover the **id and name**,
previously blank in the prior pass's table (Defect A, §9.1) — `0x0A`/`0x0B` use the same
"adopt a dedicated named allocator" mechanism already confirmed for `0x28` (§8.7); `0x27`
is the same mechanism with a literal-zero size, read directly off the
call to the named-allocator routine `FUN_00db52d0` with name "vehicle_customization_camera" and size 0 **[spelling conflict: the table above writes `vehicle_customization_cameras`; not re-checked]**.

**Newly resolved, partially (one side clean, the other genuinely open — reported as such,
not guessed):**

| id | name | resolved side | open side, and why |
|---|---|---|---|
| `0x18` | debug_superzone | GPU: `0x45fd800` (t2) / `0x52e5000` (t4) / `0x9a84000` (t0) | CPU: each tier assigns the *address* of a distinct reserved data region (`&DAT_...`), never a byte count — genuinely not a size in this function |
| `0x0E` | Compositing render target | CPU: `0x10000` (64 KiB), flat | GPU: the field holds the address of a code location (`0x01008000`), not a byte count |
| `0x0C` | Customization compositing | CPU: `0x9800` (t2/4) / `0x14000` (t0) | GPU: a multiplied/offset expression (`tier value × 25, minus a running pointer`) — a computed quantity, not a literal |
| `0x24` | Texture compression scratch | CPU: `0xbb8000` (t2/4); GPU: `0` (explicit literal, both confirmed) | CPU at tier 0 only: the address of a reserved region, not a byte count |
| `0x0D` | cust shaderball slots | CPU `0x800` / GPU `0x4000`, both already numerically correct in the prior pass's own replay output | name only — "cust shaderball slots", missing purely from Defect A (§9.1) |

**[CONFIRMED — disassembly]** for the resolved sides; the open sides are reported with the
specific reason a size could not be recovered (an address, a code-label reference, or a
derived arithmetic expression), per this project's standing rule that a negative states the
predicate, not a bare "unresolved."

**Resolved with a flagged complexity, not swept under a single number:**

`0x02` ("Vehicle Fully Cust slots") has **two different, internally consistent size
readings that disagree by exactly 2×, and both are real.** The category's own struct field
(and the allocator call that sizes its CPU pointer) carries `0x400000` (t0) / `0x1f4000`
(t2/4); the §9.1-confirmed descriptor mechanism — the value the configure callee actually
passes to the real sizing call — carries exactly half of that, `0x200000` (t0) / `0xfa000`
(t2/4). GPU is `0` either way (both routes agree on that). **Read as: the category's
*reported streaming budget* is the descriptor value (`0x200000`/`0xfa000`), since that is
what §9.1's mechanism shows the configure callee actually sizes the real allocation with;
the doubled figure sizes a separate CPU-side pointer field whose purpose this pass did not
trace.** Stated as `HIGH CONFIDENCE — inferred` rather than `CONFIRMED`, specifically for
*which* number is "the budget" — the arithmetic doubling itself is `CONFIRMED —
disassembly`. **Open, concrete next step:** read whichever code actually dereferences that
CPU pointer field to learn what the doubled allocation is for (a front/back buffer, a
worst-case bound, or something else) — flagged rather than guessed, per this project's
rule against forcing an explanation onto a suspiciously round factor.

`0x29` ("TOD_luts") has **no struct field or descriptor at all** — its only helper call
carries a plain flag byte, and no sibling offset for a CPU/GPU size is even declared for
its stack region (confirmed against the full offset map, not merely "not found by grep").
**Immediately before** this registration, however, the function builds a pair of
externally-managed pools named "lut cpu" / "lut gpu", sized `0x400` (CPU) / `0x30000`
(GPU), and the registration's own second argument is a pointer that plausibly refers to
that same pool object by adjacency and by the shared "lut" naming with the category itself
(`TOD_luts`). **`HIGH CONFIDENCE — inferred`, explicitly not `CONFIRMED`**: the connection
is by position and name, not by a traced pointer identity (the intervening code goes
through an indirect call via the allocator's vtable slot `+0x90` that this pass did not
resolve to a concrete callee). Reported as the best available lead rather than left as a
bare "unresolved."

**Still genuinely OPEN — no recoverable size anywhere in this function, reason stated:**

- **`0x25`** (morph preload mempool) — no CPU/GPU struct field and no allocator call feeds
  this category's registration at all; its only argument is a single masked flag byte.
- **`0x08`** (cutscene) — its setter call's four size/align arguments are all **residual
  arithmetic** (`a running "how much has already been claimed" accumulator, minus a small
  per-tier multiple`) built up across the preceding ~130 lines shared with several other
  categories' setup (`0x03`, `0x07`, `0x16`, `0x24` above all draw on the *same* preceding
  tier ladder). This is a real, tier-dependent remainder computation, not a missing
  literal — recoverable only by fully replaying that shared arithmetic block, which this
  pass did not attempt given the other 22 rows available.
- **`0x13`** (interface streaming) — confirmed, by reading the complete span between this
  registration and the previous one that shares its stack region, that **no assignment to
  any CPU/GPU-position sibling offset exists at all** between them; the fields are neither
  zeroed nor set, simply never touched for this category.
- **`0x17`** (decal mempool) — unchanged from §8.5; re-confirmed against the complete
  offset map that no sibling declaration exists for this row's CPU/GPU fields either.

**Not a 24th budget row at all — a dead-code artefact, and it explains a standing number.**
One of the 41 call-events the original replay counted is not a distinct category: it is
the tail of the already-documented `0x14` dead-code sequence (§8.6) — the same helper call
(`FUN_005d54e0`) fires using two stack pointers whose most recent meaningful writes belong
to **id `0x0F`'s own name string and an unrelated 16-byte copy**, i.e. leftover values with
no relation to `0x14` or to any real category. This is an independent, second confirmation
of §8.6's finding that no helper call ever references `0x14`'s own struct address — and it
resolves a loose thread from `HANDOFF.md` §27.1's "41 registrations, 40 ids" (the same figures are recorded in HANDOFF.md §26.22: "41 calls, 40 ids"): **the 41st
"registration-shaped" call site is this artefact, not a 41st id.** **[CONFIRMED —
disassembly.]**

**Tally for this pass, stated as the figure rule requires:** of the 24 rows §8.8 named as
open, **17 now carry at least one newly-recovered, hand-verified budget figure** (12 fully
on both CPU and GPU or as a single dedicated-allocator value, 5 partially with the open
side's reason stated), **1 is resolved with an explicitly flagged 2× ambiguity**, **1 is a
plausible-but-unconfirmed lead rather than a resolved figure**, **4 remain fully OPEN** with
the specific structural reason recorded, and **1 was not a real budget row to begin with**
(the dead-code artefact above): `17 + 1 + 1 + 4 + 1 = 24`. Denominator: all 24 rows named
in §8.8, none excluded or sampled.

### 9.5 Correction to §8.5 — `0x15`'s GPU budget is flag-gated, not flat

§8.5 publishes `0x15` ("interface image gpu compacting") as `CONFIRMED — disassembly` with
GPU budget `0xC00000 (12 MiB)`, tier-dependent: **no**. Re-reading the source around this
row (found while tracing the general "most-recent-write-wins" defect, §9.1 Defect B):
the value is not flat. It is `0xa00000` (10 MiB) by default, overridden to `0xc00000`
(12 MiB) only when an unrelated boolean flag (`DAT_0149365c`) is set — the same flag that
also appears inside the neighbouring `test_level_mip_streaming`/`mip_streaming` tier logic.
This flag is **not** the tier argument (the tier selector §8.4/§8.5 already document) — it is a
second, independent switch. **[CONFIRMED — disassembly.]** §8.5's row is left in place
above per this project's visible-correction rule; the corrected reading is: **CPU
`0x80000` (unchanged); GPU `0xa00000` (10 MiB) by default, `0xc00000` (12 MiB) when
`DAT_0149365c` is set — flag-dependent, not the flat, unconditional value previously
published.** What that flag actually gates (a graphics-quality option distinct from the
streaming tier, most plausibly) is `OPEN / UNKNOWN` and out of this pass's scope.

### 9.6 Two already-published rows extended (`0x1C`/`0x1D` GPU, `0x21` GPU)

Direct consequences of the mechanism confirmed in §9.1, applied to rows §8.5 already
published with a CPU-only figure and a bare "—"/"unresolved" GPU column:

- **`0x1C`** (medium lod level allocator): GPU budget `0x439800` (t2) / `0x43a000` (t4) /
  `0x980000` (t0) — recovered from the same descriptor whose dword 0 already gave the
  published CPU figure.
- **`0x1D`** (high lod level allocator): GPU budget `0x2a7000` (t2) / `0x2e7000` (t4) /
  `0x640000` (t0), same mechanism.
- **`0x21`** (large modal gameplay): §8.5 published CPU `0x320000` at tier 0 only, marking
  tiers 2/4 and the whole GPU column unresolved. Re-reading the tier ladder in full: CPU is
  `0x1f1000` at **both** tier 2 and tier 4 (not just missing, genuinely identical between
  those two tiers) in addition to the published `0x320000` at tier 0; GPU is `0xbe0000`
  (t2) / `0xe60000` (t4) — tier 0's GPU field, however, holds the address of a reserved
  data region rather than a byte count, so it stays **OPEN** for that one tier only,
  exactly the same "address, not a size" shape seen repeatedly in §9.4.

**[CONFIRMED — disassembly]** for all newly-added figures in this subsection, cross-checked
against the same tier ladder already used (and already trusted) for these rows' published
CPU figures — i.e. no new source location was introduced, only additional fields read from
one already-verified block.

### 9.7 Items 3 and 4 — status unchanged, restated briefly

**Item 3 (the tier argument's real-world meaning):** no new evidence this pass. The pattern already
on record — tier `0` gives the largest budget on every row measured in §9.4 that has three
distinct tiers (`0x01`, `0x03`, `0x07`, `0x16`, `0x18`, `0x21`, and the already-published
`0x1A`/`0x1C`/`0x1D`/`0x1F`), with `2` and `4` consistently smaller and frequently
**identical to each other** (`0x01`, `0x03`, `0x07`, `0x16`, `0x18`'s CPU, `0x21`'s CPU) —
is now measured on a much larger set of rows than the four §8.5 hand-checked, and holds
without exception across all of them. This strengthens the existing
`HYPOTHESIS — unconfirmed` reading ("`0` = streaming disabled / load generously" vs. `2`/`4`
= active-streaming presets, with `2` and `4` often sharing one design point) without
promoting it to confirmed — the caller of `FUN_005d58f0` that actually supplies the tier argument
was still not traced this pass.

**Item 4 (physical pool-sharing between `0x1A`/`0x1F` and `0x1C`/`0x1D`):** unchanged and
still out of this pass's bound, as §8.7/§8.8 already state — it needs the allocator
vtable's own implementation (the `+0x38`/`+0x90` slots), which this pass did not open.
Worth noting only as a side observation, not a resolution: `0x1C` and `0x1D` (confirmed
sharing a *mechanism* in §8.7) now also have a fully tier-scoped GPU figure each (§9.6),
and the two rows' CPU:GPU ratios are **not** constant across tiers or between the two ids
(`0x1C` runs roughly 2.2–2.4×, `0x1D` roughly 6.4–7.5×) — a real difference between the two
categories, and, on its own, weak evidence *against* the two sharing one physical pool with
a fixed split (a single shared pool sized by one governing ratio would be expected to show
one consistent ratio, not two). Flagged as a side observation only; it does not reach the
allocator implementation the question actually needs.

## 10. What the `hN` suffix denotes — the "Zone (High LOD)" fine-cell containers, and the `_a_` activity zones (2026-09-20, agent AA)

Closes §6 item 5, advances §6 item 6, resolves §5.1's section-4 `index` question and the §3 suffix hedging. New Ghidra project copy `tools/gp_hn1`; scripts `tools/scripts/Hn1Strings.java`, `Hn1CallChain.java` (plus the existing `SaveWDecomp.java`); harnesses `tools/harnesses/hn1_*.py`; dumps `tools/hn1_*.txt`. Labels: CONFIRMED — disassembly / CONFIRMED — empirical / HIGH CONFIDENCE — inferred / HYPOTHESIS / OPEN. No decompiled code is reproduced; function addresses are evidence anchors, quoted strings are engine data literals.

**Population and exclusions.** All 805 shipped `.asm_pc` manifests (agent Y's exact-consumption parser, `spec-asm-format.md` §9; 737 of them inside `sr3_city_0/1`); `sr3_city.grid_pc` (998 regular records); and **every `.czh_pc` in the game — 2,971 files, all read out of raw top-level containers**. No mode-(a) or non-raw container was met, so nothing was excluded. **No `.czn_pc` interior was read or decoded** (parked, `HANDOFF.md` §27.3); the only placement-like data touched is the position words of the `SR3Z` record array in `.czh_pc` (a separate, already-documented structure, `spec-ctorless-types.md` §5.1) and it is used only as a spatial control.

### 10.1 The answer

`CCRRhN` is **the fine-grid cell number N of tile `CCRR`, packaged as a container of the engine's own container kind `Zone (High LOD)`; it holds the full-detail (LOD 0) level meshes whose volumes overlap that cell.** The tile's own bundle (kind `Zone`) holds only the reduced-detail `~L1` versions of the same meshes. Each tile is cut into a 2×2 block of fine cells; `N → (column offset N & 1, row offset N >> 1)`, row 0 being the higher-z row. Of §3's three readings: "LOD tier" is right about *what* (the kind is literally named High LOD and the entries are the LOD-0 files), "spatial subdivision" is right about *which* (N is a location, not a quality step), and "prop-category batch" is refuted. **[CONFIRMED — disassembly + empirical; three independent legs, §10.2–§10.6.]**

What is *not* settled: what re-triggers the load/unload reconciliation during free-roam driving (§10.4).

### 10.2 The engine's own names: container kinds and their default allocators

The container-kind registration function (`FUN_006ff730`; 41 rows through `FUN_00db1c90`, row array base `0x029eadf8`, stride `0x28`, `spec-asm-format.md` §8) contains these two rows, read field by field:

| kind id | name (engine literal) | default pool id | secondary pool id | callbacks |
|---|---|---|---|---|
| `0x1D` (29) | `Zone` | `0x1C` (28) `medium lod level allocator` | `0x26` (38) `level container cache` | three: `FUN_00866580` (zone-instantiate on load: looks through the loaded container's entries for Zone fast/slow, Zone header fast, Buffer and builds the runtime zone), `LAB_00857ad0`→`FUN_008570a0`, `FUN_00866bf0` |
| `0x1E` (30) | `Zone (High LOD)` | `0x1D` (29) `high lod level allocator` | `0x26` (38) | **only** the middle one (`LAB_00857ad0`); no zone object is built for a High-LOD container |

Entries whose manifest `pool_id` is 0 inherit the kind's default pool (`spec-asm-format.md` §7.4), and **every** entry of every `hN` record has `pool_id` 0 (41,689 entries) — so hN meshes are allocated from "high lod level allocator" (`0x1D`, budgets `spec-world-streaming.md` §8.5/§9.6) while the tile record's own pool-0 entries come from "medium lod level allocator" (`0x1C`). This is the code-level meaning of the two allocator names that §7.2 could only read off the strings. **[CONFIRMED — disassembly (both rows); pool ids equal the manifest table-1 ids 28/29, empirical.]**

**Correction to §3:** hN containers have "no `.asm_pc` manifest *of their own*" — true, but their manifest **records are in the parent tile's `.asm_pc`**: `1018.asm_pc` holds five records (`1018`, `1018h0`..`1018h3`). The same holds for `CCRR^<name>` and its `hN`. **[CONFIRMED — empirical: 468 `Zone` records + 422 `CCRRhN` records + 129 `CCRR^name` records + 421 `CCRR^nameHN` records; every hN-suffixed record has `container_kind` 30 `Zone (High LOD)`, every tile / `^name` record `container_kind` 29 `Zone`.]**

### 10.3 The runtime: six level managers, and level 4 is the fine grid

World startup (`FUN_0084e3b0`, reached from the main init `FUN_007acfe0`) calls the world-layout loader `FUN_0085d500` → `FUN_0085a070` (the routine that prints "Processing … zones", loads every manifest and registers the containers), which builds the runtime layout `FUN_00855f80`. That constructor makes **six level managers** and gives each a container kind, a grid, a cell size and a "window" (load-volume extent). The cell id used everywhere is a `u16` whose top 3 bits are the level number (`FUN_0085c7a0` checks `id >> 13 == level`). **[CONFIRMED — disassembly, constructor arguments read from the instruction stream and cross-checked against the decompile.]**

| level | kind | grid (cols × rows) | cell size (x, z) | window (x, z) | desired-list capacity |
|---|---|---|---|---|---|
| 0 | `0x1B` Level Always Loaded | 1 × 1 | (whole world) | — | 6 |
| 1 / 2 | `0x1F` Interior zone / `0x20` Large interior zone | list class | — | — | 1 / 7 |
| 3 | `0x1D` **Zone** | `num_zones_x` × `num_zones_z` = **25 × 25** | **320 × 280 m** | 479.5 × 559.5 m | 7 |
| 4 | `0x1E` **Zone (High LOD)** | **2·25 × 2·25 = 50 × 50** | **160 × 140 m** | 159.5 × 139.5 m | **4** |
| 5 | (kind 0) | list class | — | — | — |

The sizes are the defaults the loader writes when its optional `.wlayoutx` file is absent (zone width 320.0, zone height 280.0, 25 × 25 zones, "trigger grow" 120.0, city extent 2000.0); **no `.wlayoutx` exists in any shipped archive or in the install folder (21,217 top-level archive entries scanned)**, so these are the shipped values. **[CONFIRMED — disassembly (constants at `0x01301e00`+, `0x01164e40`+) + empirical: the placed-object extents of 112 tiles span a median 317 m (p90 321) in x and 279 m (p90 285) in z, §10.6(g).]** Level 4's grid is exactly twice the resolution of level 3 in both axes; its per-level origin offset vector is (−80, 0, +70), i.e. a quarter of the tile size, which is what makes the fine cells nest exactly inside the tiles. Each cell of levels 3 and 4 can hold up to 10 container variants (constructor argument 10 = the variant-list capacity used when a cell record is created, see the next-but-one paragraph). Both grids have a **half-cell brick stagger**: the row parity term (`row / this[+0xB4]`, `+0xB4` = 1 for level 3, 2 for level 4) shifts every other tile row by `+0xB0` = 160 m (level 4: every other *pair* of fine rows, so a tile's two fine rows stay together). **[CONFIRMED — disassembly (`FUN_0085b430`, `FUN_0085e9d0`); direction and size CONFIRMED — empirical, §10.6(g).]**

Cell numbering, from the cell-enumeration routine (`FUN_0085e9d0`) and the cell-bounds routine (`FUN_0085b430`): columns increase with +x; **rows increase toward −z** (a cell's upper z bound is a constant minus row × cell height); the enumeration is row-major (rows outer, columns inner). A tile's box therefore yields its four fine cells in the order (low x, high z), (high x, high z), (low x, low z), (high x, low z). **[CONFIRMED — disassembly.]**

**Where the name is composed.** The per-level "register the containers of this zone" method (grid-class vtable `0x01164ed4`, slot 9 = `FUN_00860900`; list-class twin `FUN_0085e490` at `0x01164f94`) takes the zone's name, turns it into a box (`FUN_0085bac0`), enumerates the level's cells over that box, and — **when the level is 4, composes the container name with the format string `"%sh%d"` (literal at `0x01164bc0`) using the zone name and the cell's index in that enumeration** — then looks the container up by name (`FUN_00db3530`, the container registry filled from the `.asm_pc` files) and stores it in the cell's record. A name containing `^` or `~` (a sub-area / state variant) is *appended* to the cell record's variant list instead of replacing slot 0; each cell record `{variant array, count, selected index, current index, level}` (built by `FUN_0085ae20`) therefore holds the default `CCRRhN` plus its `CCRR^<name>hN` alternatives, and the loader acquires the **selected** variant (`record+6`; level 0 acquires all). **[CONFIRMED — disassembly.]** This is exactly `sr3_city.grid_pc` section 4 (§10.6(e)).

### 10.4 Which fine cells are wanted, and what is still open

The per-level "which cells do I want" method (vtable slot `+0x08`, `FUN_00860c50`) is short and was read in full together with every callee except the box-to-point distance primitive `FUN_00dc8be0`:

1. Build an axis-aligned box centred on the streaming **focus position** with half-extents = ½ × the level's window vector (level 4: ±79.75 m in x, ±69.75 m in z; level 3: ±239.75 × ±279.75 m; the y extent is 4,000 m, i.e. unbounded in practice).
2. Enumerate every cell of the level's grid that box overlaps (`FUN_0085e9d0`, box shrunk by a small epsilon so touching neighbours do not count).
3. If there are more candidates than the list capacity, sort them by a distance comparator against the focus (`FUN_0085cd70`, nearest-first — the x87 return plumbing was not fully unravelled, direction HIGH CONFIDENCE) and truncate; keep only cells that have a registered container record.

**For level 4 the window (159.5 × 139.5 m) is smaller than one fine cell (160 × 140 m), so it overlaps 1, 2 or 4 cells and never more — the capacity of 4 never truncates.** In other words: *the fine cells wanted are exactly those touched by a cell-sized window around the focus point; near a tile corner they belong to up to four different tiles.* The routine takes no quality/graphics parameter (inputs: focus position, the level's constant window, the output list). **[CONFIRMED — disassembly.]**

The reconciliation itself (`FUN_008610e0`, with `FUN_00860e70`): for each level that is not on hold (a per-level counter) and whose pending list (`+0x58`) is empty, compute the wanted list, diff it against the level's active list (`+0x18`), **release** the containers of dropped cells (and of cells whose selected variant changed) (manager slot `+0x34` = `FUN_0085ef70` → `FUN_00dafad0`, a reference-count decrement that frees at zero) and **acquire** those of new cells (slot `+0x30` = `FUN_0085ee30` → `FUN_00dafea0`, a load request). **[CONFIRMED — disassembly for the flow; the two container primitives' roles HIGH CONFIDENCE — bodies read, refcount-style.]** The same selection routine is used read-only by gameplay code (`FUN_00862020` → `FUN_008b9060`: "is cell X inside the streaming window around the player").

**OPEN — what calls the reconciliation in steady state.** `FUN_008610e0` has seven static call sites in five functions: `FUN_008618f0` (per-level hold-counter inc/dec plus an optional flush; six callers, one of them the modal-content hold/cleanup path `FUN_006d2080`), `FUN_00861800` and `FUN_00861460` (startup preload, both reached from `FUN_0084e3b0`), `FUN_008625f0` (the startup wait-until-loaded loop, likewise) and `FUN_00861850` (call-and-poll helper; four callers — one through a vtable, two around `0x7A8180`/`0x7B0650`, one at `0xBDBF30` — not classified). The focus position it uses is written by `FUN_0085bce0` (five call sites: world init, a game-state start routine and two others). **No per-frame caller was found among these**, so whether free-roam streaming re-runs this exact routine every frame or reaches the manager slots through another path is not established here. The hN *selection rule* above does not depend on the answer.

Budget observation, not a finding: a median hN record totals 2.4 MB of GPU-side data (p90 4.2 MB, max 6.2 MB, 422 records) against the "high lod level allocator" GPU budgets of 6.55 MB (tier 0) / 2.7 MB (tier 2) / 3.0 MB (tier 4) (§9.6); how up to four resident cells fit the smaller tiers (shared meshes, tolerated allocation failure — flag `0x40` entries) was not traced. **[OPEN.]**

### 10.5 What goes into an hN: the packing rule

The routine that registers one zone's content (`FUN_00859110`; the build-shared source path string in `FUN_008568f0`/`FUN_00857560` names `world_zone_layout_build_shared.cpp`; at run time in the shipped game the layout methods above take the "look the container up by name" branch instead, because the authoring `.zonex` inputs do not exist) does the following for a zone whose `SR3Z +0x1E` type selects kind `Zone` (§10.7):

- registers the tile container, and for each level mesh placed in the zone registers **the `~L1` file** (`FUN_00858290` → `FUN_00858060`, default case; kind `Level Always Loaded` registers `~L2`, kind `Large interior zone` registers base + `~L1` + `~L2`);
- then, only if both the fast and the slow zone files exist, asks `FUN_00854db0` for the tile's **four sub-boxes** (tile box halved in x and z; order (low x, high z), (high x, high z), (low x, low z), (high x, low z) — read from the instruction stream, identical to the runtime enumeration order of §10.3, an independent cross-check) and, for each sub-box N: counts (`FUN_00858e40`, count-only pass) the placed level-mesh objects whose transformed bounding box overlaps the sub-box, **and only if the count is positive** opens a container named `"%sh%d"` (tile name, N) — **with container-kind byte `0x1E`, i.e. `Zone (High LOD)`, at the container-creation call (`FUN_006f6c20` from `0x00859AFF`)** — registers each overlapping mesh's **base** `.clmesh_pc` in it (second, registering pass; a mesh spanning several sub-boxes is registered in each), and then appends a fixed list of 13 named skyline meshes (`FUN_00855520`).

**[CONFIRMED — disassembly for every step; the placed objects are the 14-byte records of the fast `.czh_pc` header, §10.7.]** Consequences that the data then confirms (§10.6): empty cells get no container (hence partial sets), a big mesh is duplicated into every cell it touches, and the four hN of one tile are *not* ordered by quality.

### 10.6 Empirical validation over the whole population

(a) **Kinds and contents.** `CCRRhN` / `CCRR^nameHN` records: 843, all kind `Zone (High LOD)`, all entries type `Level Mesh` (41,689), **all base-named (0 with a `~L` suffix)**. Tile / `^name` records (597): kind `Zone`, 23,726 `Level Mesh` entries **all `~L1`**, plus `Low Mips` textures in pool `mip_streaming` (44,066), zone/zone-header/buffer entries, VFX, rigs, light curves. `Level Always Loaded` (`awld_compact`, 3,820 entries): 456 `~L2` meshes. `Large interior zone` (62 containers): base + `~L1` + `~L2` of the same 6,119 meshes. **[CONFIRMED — empirical; matches the per-kind registration code of §10.5.]**

(b) **A monotone LOD ladder in the data.** For the 6,119 base/`~L1`/`~L2` triples of the large interiors the GPU-side size is strictly decreasing base > L1 > L2 in **87.97 %** (5,383) and non-increasing in **98.10 %** (6,003); base is larger than L1 in 93.14 %. For the 18,637 pairs "hN base mesh vs the same mesh's `~L1` in the tile record" (86 full tiles) the GPU-side size ratio has median 1.99 (p10 1.53, p90 2.76, min 0.97, max 25.3), base larger in **99.37 %**, equal 0.35 %, smaller 0.27 %. (CPU-side sizes are *not* monotone — median ratio 0.69 — that side holds material/collision metadata, not vertex data.) So hN is the high-detail rung of a three-rung ladder L2 (always resident, level 0) / L1 (tile window, level 3) / L0 (fine cells, level 4). **[CONFIRMED — empirical.]**

(c) **Set relations.** 122 of 468 tiles carry any `~L1` mesh; **all 122 have at least one hN record, and none of the 346 mesh-less tiles has one (0 counter-examples)**. 86 tiles have all four cells; the other 36 show partial sets (e.g. `{h0,h1}` 7 tiles, `{h1,h2,h3}` 5, …) — the empty-cell rule. In 77 of the 86 full tiles the union of the four hN mesh sets **equals** the tile's `~L1` set; in the rest the only additions are meshes from the 13-name skyline list — **across all 122 tiles that have hN records, all 43 "hN-only" entries belong to it, 0 outside** (`skygen_80m_med02`, `airport_terminal_a`, `ss_syn_*`, `ss_decker_tower01`, `ssdecker_tower_base`, `ss_luch_*`, `ss_ori_base01`, `ss_oiron_tower01`; the same 13 literals `FUN_00855520` builds). Across the 86 full tiles a mesh appears in 1 / 2 / 3 / 4 of the four cells in 5,351 / 2,758 / 1,191 / 1,060 cases, and **whenever it appears in several cells its (primary, secondary) sizes are identical — 8,320 of 8,320 repeat appearances** (the same file, not a lower-quality variant). **[CONFIRMED — empirical.]** This also explains §3's "h0 = lamp posts / drainage, h3 = fencing / utility tower": different quarters of the tile hold different objects.

(d) **Not a quality tier.** Total GPU-side size of h0..h3 is monotone in only 12 of 86 tiles (chance for four distinct values ≈ 7/86 ≈ 8.3 %), mean entries per cell are flat (52.9 / 53.6 / 54.4 / 56.2). **[CONFIRMED — empirical (negative control for the LOD-tier reading of N).]**

(e) **`sr3_city.grid_pc` section 4 is the fine-cell directory.** The file parses to 998 records (sections 1 / 2 / 3 / 4 = 52 / 41 / 468 / 437); the 468 of section 3 are exactly the 468 tile records. **Section 4's `index` is the fine-cell address `(fine_row << 8) | fine_col`** with `fine_col = 2·col + (N & 1)`, `fine_row = 2·row + (N >> 1)` (`col`,`row` = the tile's `CCRR` digits): predicted correctly for **all names of all 437 groups** (a group = the default `CCRRhN` plus its `CCRR^…hN` variants, which share a cell; 15 name slots hold the literal `null` = empty variant slot). Controls: swapping the two bits of N matches 209/422 (only the N = 0/3 cases), a row stride of 50 or reversed N match 0/422, the true model 422/422. Section 3's `index` is the same encoding one level up, `(row << 8) | col`, 468/468. **This resolves §5.1's "section 4's index-value scheme".** **[CONFIRMED — empirical.]**

(f) **Orientation of N, independent of the code.** Using only the position words of each full tile's `.czh_pc` records, count records per quadrant and correlate with the number of meshes in each hN over all 24 assignments of quadrants to N: the assignment N = qx + 2·qz with q_z = 0 for the *higher-z* half is best of 24 (Spearman **0.843**, next best 0.625). **[CONFIRMED — empirical (86 tiles × 4 cells).]**

(g) **The tile lattice is exact.** With the record positions (`s16 / 64` m plus the header origin, §10.7), the midpoint of each tile's placed-object extents obeys, over 112 tiles, **`centre_x = 320·col − 4000 − 160·(row mod 2)`** (even rows −4000.0, odd rows −4160.0, MAD 2.2 / 1.4 m) and **`centre_z = 3640 − 280·row`** (MAD 1.6 m). That reproduces the code's 320 × 280 tile, the half-tile stagger *and its sign* (even rows shifted +160 relative to odd rows, as the parity term adds `+0xB0` on even rows), rows running toward −z, and puts the populated columns 03–22 symmetrically about x = 0 on even rows. A fine cell's centre is the tile centre ± (80, 70): N = 0 (−80, +70), 1 (+80, +70), 2 (−80, −70), 3 (+80, −70). **[CONFIRMED — empirical; lattice constants HIGH CONFIDENCE as *exact* because they come from extents midpoints, not from a boundary field.]**

### 10.7 Header-level side results (all in `.czh_pc`, none in `.czn_pc`)

- **`SR3Z +0x1E` is the zone type — the selector of the container kind.** The registration routine switches on this word: `{1, 3, 8, 10, 11, 13}` → kind `0x1B` Level Always Loaded; `{5, 6}` → `0x21` mission; `7` → `0x1F` Interior zone; `9` → `0x20` Large interior zone; `12` → `0x22` large mission; anything else (incl. 2) → `0x1D` Zone. Over **all 2,971 `.czh_pc` files** (every container with a zone-header entry in every archive, kind taken from its manifest record) the observed pairs are: Zone ↔ 2 (1,194), Interior ↔ 7 (140), Large interior ↔ 9 (174), Level Always Loaded ↔ 1 / 3 / 8 / 10 / 13 (2 / 1,194 / 58 / 51 / 21), mission ↔ 5 / 6 (62 / 72), large mission ↔ 12 (3) — **0 exceptions, 11 is the only case value never observed.** Resolves `spec-ctorless-types.md` §5 / §8 item 2 for `+0x1E` (that document is not edited here). In `sr3_city_0/1` the zone file names carry a marker character before the extension: activity zones (type 6, 59) end in a backtick, the six arena zones (type 9, §10.8) in `$`, ordinary zones have none (the name parser tolerates `! $ \` ^ ~` after the four digits). **[CONFIRMED — disassembly + empirical, 2,971/2,971.]**
- **`SR3Z +0x0C..+0x17` are three `f32`: the world-space origin the record positions are relative to.** Over the 1,265 tile / sub-area / activity-zone headers of `sr3_city_0/1`: zero in every file without records, non-zero in all 248 files that have records. **All 248 files with records are `~f` (fast) headers; all 603 `~s` (slow) headers in the same set have count 0** — an observation on `spec-ctorless-types.md` §8 item 3 (fast vs slow). **[CONFIRMED — disassembly (used as the translation) + empirical.]**
- **Record fields as read by the hN packing routine** (partial answer to `spec-ctorless-types.md` §5.1): `+0`, `+2`, `+4` = `s16` position words, **1/64 m** per unit (constant `2⁻⁶`), added to the header origin (validated: spreads 317 / 279 m, §10.6(g)); `+12` = `u16` name index compared with a list of the source zone file's level-mesh names; `+6` = `s16` scaled by `2⁻¹²` and fed to a one-scalar transform builder (a rotation angle in radians fits the zero-heavy distribution better than a scale factor — **HYPOTHESIS**, not established); `+8`, `+10` are not read by this consumer. The shipped `.czh_pc` name list does **not** contain the level-mesh names (it holds textures and `.fmeshx` names, `spec-terrain-format.md` §2), so mapping a record's name index to a mesh file is not possible from the header alone — **OPEN**.

### 10.8 The `_a_<type>_<region>_<NN>` names (secondary; §6 item 6)

**What they are — a correction to §4.** They are not "generic ambient building interiors". They are the **activity instances' zones**: 59 pairs (118 records) plus 6 arena bundles. Each activity has a base record (`_a_dt_dt_01`: kind `mission`, zone type 6, with Level Mesh / Peg / Animation / Rig / VFX entries and a zone whose fast header usually has **no** placement records — 51 of the 59 have count 0) and a companion `…_modal` record (kind `mission model data`, kind id 39) holding the activity's **`.xtbl` table (`Activity table file`, the file's own `<Name>` is `_A_DT_DT_01`)**, the `Mission Conversation` (`.ctdg_pc`) files named `<name>_<beat>_<voice>` (voice suffixes `bf bm hf wf wm wma z`) and sometimes a `Mission LUA script`. The code that registers a zone's modal content (`FUN_00855ba0`) does exactly this: modal script, **`<name>.xtbl` as the "activities" table when the zone type is 6**, and every `<name>*.ctdg_pc` conversation. The six `rm` activities additionally have a large `Large interior zone` bundle (`_a_rm_dt`, `_a_rm_dt_2`, `_a_rm_dt_3`, `_a_rm_sw_1`..`3`, zone type 9, 108–132 level meshes each) — which is why their base names are irregular. **[CONFIRMED — disassembly + empirical.]**

**What the code does with the names: nothing structural.** The activity-instance parser (`FUN_0061c6e0`, read in full) copies the instance name, hashes it with the engine name hash (the save-file key of `spec-save-format.md` §9.3a/§9.8) and uses it verbatim in one composed lookup string (`"activity_start_%s_%s"`, first `%s` = a start-location name from the same table); the DLC completion checks (`FUN_006d6450`) compare whole literal names (`dlc1_a_tbp_01`, `dlc1_a_es_01`, `dlc1_a_bm_nw_01`, …). **In these functions the type and region codes are never split out or tabled.** *(Bounded negative: the two functions above and the world-layout module functions read in this pass; no whole-binary search was made.)* The names are authored row names; the codes' meaning lives in the data.

**Second code (region) — matches the game's own hood-region prefixes. [CONFIRMED — empirical for `nw`, `dt`; HIGH CONFIDENCE for `sw`, `ne`.]** The values are `dt`, `ne`, `nw`, `sw` (4). The `.xtbl` of the `mh` activities with second code `nw` carry `Hood_Name` = `HOOD_NW_01`..`_04`; the `if` activities with `dt` carry `Active_Hood` = `HOOD_DT_01`..`_03` — exactly the engine's hood-id strings (`HOOD_NW_01..04`, `HOOD_NE_01..04`, `HOOD_DT_01..03` and `HOOD_SE_01` are literal in the binary). The `sw` activities name five *named* hoods instead (`HOOD_BRIDGEPORT`, `HOOD_YEARWOOD`, `HOOD_DOWNERS_GROVE` for `if`; `HOOD_POINT_PRYOR`, `HOOD_NEW_BARANEC`, `HOOD_BRIDGEPORT`, `HOOD_YEARWOOD`, `HOOD_ZOMBIE` for `mh`), which the binary also holds as literals but does not label `SW`; no `ne`-coded `.xtbl` carries a hood tag. Gang fields agree with region grouping (`ha` `dt` → Morningstar, `ha` `nw` → Deckers; `dt` activities → Luchadores / Morningstar). So the second code is the **hood-region group** of the activity; that `nw`/`ne`/`sw` mean the compass sense of those letters and `dt` downtown is the natural reading of the shared prefix but is **not** established (the hood polygons are not in any file read here).

**First code (type) — ten codes, ten disjoint element vocabularies. [HIGH CONFIDENCE — inferred from content; the code→official-activity-name table is not stored in these names.]** Types `dt es ga ha if mh rm sn tb tm` (DLC adds 3-letter codes such as `tbp`, and the region code can be absent, e.g. `dlc1_a_es_01`, or two-part, `dlc1_a_bm_nw_01` — so the pattern is `<dlc prefix>_a_<type>[_<region>]_<NN>`, not fixed-width). Each type's `.xtbl` uses element names that no other type uses, and they describe distinct mechanics: `dt` — `Dealer_Type`, `Deals`, `Buyer_Spawn`, `Deal_Use_Count` (deal-and-buyer loop); `es` — `Client`, `Dropoff_Navpoints`, `Drift`/`Air`/`Crash`/`Drive_By` bonus counters; `ga` — `Attackers`, `Attack_Group`, `Camera_Restriction` (2 files only); `ha` — `Aircraft_path`, `Enemy_Helicopter_Type`, `Chance_of_rocket_launcher`, `Convoy_Level_Info`; `if` — `Crazy_Chance`, `Bonus_Spots`, `Active_Hood`; `mh` — `Combo_Max_Multiplier`, `Hood_Name`; `rm` — `Electric_Trap`, `Explosion_Timer`, `Door_Mover`, `Alternative_Cash_Amount`, `Cutscene`, `Banter` (trap arena, the one type with an interior bundle); `sn` — `Ho`, `Ho_Rescue`, `Dropoff_Effect`; `tb` — `Checkpoint_Group`, `Barrel_Group`, `Bonus_Time`; `tm` — `Convoy`, `Crowd`, `High_Value_Targets`, `Coop_Tank_Spawn_Location` (9 files = 3 regions × 3). The population is 59 instances (8 types × 2 regions × 3 ordinals, `tm` 3 regions × 3, `ga` only 2; the save table has 60 keys, `spec-save-format.md` §9.3a). **The letters are not decoded from the letters** — the mechanic behind each code is read from the table content; the game's own display names for the 19 activity types would come from `activity_types.xtbl` (loaded by the world-startup routine `FUN_0084e3b0`), which is **not a top-level entry of any archive** *[superseded: `spec-tables-progression.md` §14.16 read it from `misc_tables.vpp_pc` (19 activity-type names with the DLC1 copy); mapping the ten codes to those names is still open]* and was not searched for inside nested containers this pass, so a code → display-name table cannot be given here. Activity zones mostly carry no placement records (51 of 59 fast headers have count 0), so their world positions are largely not recoverable from headers; the only two `nw`-coded zones that do carry an origin (`_a_tm_nw_01`, `_a_tm_nw_03`, 2 and 5 records) sit at world x = +1,402 / +1,075, z = −28 / −952 — with the lattice of §10.6(g) that is not obviously "north-west" if +x is east, and a zone with 2–5 records need not have its origin in its activity area, so this is a caution against reading the letters as compass points, not a refutation.

### 10.9 What this changes in earlier sections, and what stays open

- **§3** — the suffix hedging is struck through there and points here; the "no manifest of their own" wording is corrected in §10.2.
- **§4** — the `_a_` "generic ambient building interiors" reading (HIGH CONFIDENCE — inferred) is **refuted** by §10.8 (activity zones, kind `mission`); the `_modal` "self-contained instanced space" hypothesis is refined: `_modal` = the mission-model-data companion (activity table + conversations).
- **§5.1** — section 4's `index` scheme resolved (§10.6(e)); section 3's index is `(row << 8) | col`.
- **§6 items 5 and 6, and item 4's "section 4's index-value scheme"** — struck through in place.
- **§7.2 / §8** — the two allocator names now have a code-level meaning: `medium lod level allocator` = the pool of container kind `Zone` (tile bundle, `~L1`), `high lod level allocator` = the pool of kind `Zone (High LOD)` (fine cells, base meshes). `zone always loaded` is the level-0 kind's pool (**HYPOTHESIS** by name — §10.3; that kind row's pool byte was not read this pass).
- **OPEN:** (1) what drives the reconciliation each frame (§10.4) — **advanced, not closed, by §11.5**; (2) how up to four resident cells fit the tier-2/4 high-LOD budgets (§10.4); (3) the meaning of record word `+6` and the name-index → mesh mapping for shipped headers (§10.7); (4) display names of the ten activity types (needs `activity_types.xtbl`, not found as a top-level archive entry *[since found in `misc_tables.vpp_pc`, `spec-tables-progression.md` §14.16; the code→name mapping is still open]*) and whether the region letters are compass sectors (§10.8); (5) the per-level window/capacity numbers of levels 0–2 and 5 (only levels 3 and 4 were tabulated); (6) the meaning of `SR3Z +0x1E` case 11 and of the level-5 manager (kind 0).

## 11. Runtime streaming behavior: the distance/box rule, hysteresis, the camera-position source, and load/eviction order (2026-09-30)

A follow-up pass, scoped deliberately to the streaming manager's own executable code (no `.czn_pc` record content read at any point — this section is entirely code-derived), characterizes the RUNTIME behavior on top of the container kinds/ladder §10 already establishes: when the engine actually loads or unloads each of the three levels most relevant to open-world traversal as the camera/player moves — **L2 = `awld_compact`** (engine level 0, "Level Always Loaded"), **L1 = the plain tile bundle** (engine level 3, "Zone"), **L0 = the `hN` "Zone (High LOD)" fine cells** (engine level 4) — using the same level-manager/vtable-method terms §10.3/§10.4 already establish.

### 11.1 The distance/box rule — confirms and re-derives §10.4's own figures independently

The per-level "which cells do I want" method (`0x00860c50`, already read in full at §10.4) builds its box by reading the level manager's own stored window-size fields (three floats at fixed offsets `+0xa4`/`+0xa8`/`+0xac` of the manager object) and multiplying each by a constant read directly from data as the IEEE-754 double **0.5** (address `0x012a2dc0`) — i.e. `box = focus ± (window / 2)`, re-deriving §10.4's own already-published window figures from the box-construction code itself rather than from constructor arguments: L0 (engine level 4) window ≈159.5×139.5 m; L1 (engine level 3) window ≈479.5×559.5 m. **L2 (engine level 0) goes through the IDENTICAL generic function `0x00860c50` as L0/L1 — there is no special-cased "always loaded" branch anywhere in this chain.** Camera position is structurally irrelevant to L2's own load/unload decision only because its own grid is 1×1 covering the whole world (§10.3), not because the code exempts it. This is a world-unit axis-aligned box test, re-evaluated fresh on every reconciliation call — never expressed anywhere as "N cells in each direction." **[CONFIRMED — disassembly, re-deriving §10.4's own window figures from a second, independent code path (the box constructor rather than the manager's own constructor arguments).]**

### 11.2 Hysteresis — CONFIRMED NEGATIVE; a temporal debounce exists instead of a spatial gap

The SAME box (same window, same focus point, from the single call to `0x00860c50`) is used both to build the "wanted" set that drives acquisition AND — by plain set-difference against the level's previous active list inside the reconciliation function `0x008610e0` — to determine what gets released. **No second, larger "keep" box or margin value exists anywhere in this chain: one threshold for both directions.**

What does exist, and could be mistaken for hysteresis, is purely temporal, not spatial:
- Two adjacent 6-entry per-level integer counter arrays at `0x02444f0c` and `0x02444f24` (exactly 24 bytes apart, i.e. contiguous 6-int blocks). `0x02444f24` is incremented/decremented by a small helper at `0x008618f0` and, while nonzero for a level, suppresses rebuilding that level's wanted list at all (inside `0x00860e70`). `0x02444f0c` separately gates whether the acquire/release loops run for that level inside `0x008610e0` itself. Whether these are one logical per-level "hold" state read through two symbols or genuinely two independent counters is **OPEN**.
- Independently, `0x00860e70` skips rebuilding a level's wanted list while that level's own "pending" list (read via the level manager's own vtable slot `+0x14`) is still non-empty, routing instead to an unexamined function at `0x0085fdd0` — i.e. a level with an outstanding in-flight request is not re-evaluated until it drains. This is a real anti-thrash debounce, but it is gated on "is a load still in flight," not on distance.

**[CONFIRMED — no distance-based load/unload gap for any of L0/L1/L2; the anti-thrash mechanism found is request-in-flight / hold-counter debouncing, not a wider keep-radius.]**

### 11.3 Camera-position source — CONFIRMED single global feed; HIGH CONFIDENCE it is the render-camera/view state, not the player pawn directly

All three levels' reconciliation reads one global 12-byte Vector3 — 8 bytes at `0x02447c64` plus 4 bytes at `0x02447c6c` — written **exclusively** by one 25-byte function, `0x0085bce0`, whose entire body is "copy caller's 12-byte argument into these two globals, return." Every consumer (`0x00860c50`, reached via `0x00860e70` / `0x008610e0` / `0x00861850` / `0x008618f0`) treats it purely as the focus point.

Five static call sites into `0x0085bce0` land in at least three structurally distinct systems — reconfirming this project's own prior finding that different systems really do use different "current position" sources even though they happen to funnel into the same global here:
1. **World/game init** (`0x0084e3b0`, reached from the main init routine `0x007acfe0`) — a one-time startup default.
2. **A camera-cut / view state-machine function, `0x007a82c0`.** It maintains a small 2-slot ring buffer of pending "camera shot" records (12-byte position + blend/transition byte parameters per record, `0x3c` bytes each, base ~`0x0224206c`, indexed by a sign-safe mod-2 counter at `0x0224205c` — the `(x+1)&0x80000001` idiom WALLS.md already flags as a branching round-up form *[correction 2026-09-30: WALLS.md flags `& 0x80000003` and the `(x-1|0xfffffffc)+1` round-up, not this mod-2 idiom; it is in the same family of non-literal idioms WALLS.md warns about]*). When a new shot becomes pending it calls `0x0085bce0` with that record's own position field, then immediately calls two further functions (`0x00da38e0`, `0x00564c20`) reading the same local buffer — consistent with building/applying a view matrix from it. **HIGH CONFIDENCE (not independently proven by opening those two functions' own bodies): the position source in steady state is the active render-camera record, not the player pawn directly and not a separate dedicated "streaming anchor" object.**
3. **A second, distinct path** through a large function at `0x00702a50` (itself reached from a cluster of further-out functions in the `0x0072axxx`/`0x0072bxxx` region that look mission/activity-transition-shaped). Immediately before calling `0x0085bce0` it fetches a fresh 12-byte value via a function at `0x009e7cb0` and writes it straight through — reading as a "warp the streaming focus to a specific place" call (mission start / fast travel / respawn), not a per-frame camera follow. **OPEN:** exactly what `0x009e7cb0` reads.

A **separate, structurally unrelated** global position (12 bytes at `0x029cdb98`) is used only by the one-time startup/interior-preload path (`0x00861800` → `0x00861460`, which reconciles levels 0/1/2 individually at boot) — a further confirmation that "current position" is not a single universal concept in this codebase.

### 11.4 Load/eviction order and budget — nearest-first plus a second flag-gated sort; no per-tick load budget; eviction is immediate

**Load order, two layers:**
- **Build-time sort (`0x00860c50`, already established at §10.4):** when overlap candidates exceed a level's list capacity, they are sorted via a qsort-style call using comparator `0x0085cd70` before truncating — that comparator computes a box for each candidate cell (via the level manager's vtable slot `+0x28`) and a box-to-point distance against the focus (via `0x00dc8be0`), then compares the two distances, independently re-confirming §10.4's own "nearest-first" reading from raw instructions. The sort's own comparison *direction* stays HIGH CONFIDENCE, not CONFIRMED, per §10.4's own already-recorded x87 return-value ambiguity — this pass hit the identical ambiguity and did not resolve it further.
- **Acquire-time sort (`0x0085ee30`, the level manager's own "acquire" method) — a genuinely new finding:** if a global flag at `0x0149365c` is set, the candidate list is **re-sorted again** with a **different** comparator (`0x0085d010`, not decompiled this pass) before any load requests are issued; if the flag is clear, acquires are issued in whatever order the list already has. `0x0149365c` is set to `1` at the top of a large startup display/quality-mode init function (`0x005d1a30`, alongside three sibling flag bytes at `0x0149365d`/`e`/`f`) and is the SAME literal address §9.5 already found gating an unrelated streaming-budget calculation — consistent with a general boot-time quality-tier bit reused across unrelated call sites, not something streaming-specific. **HIGH CONFIDENCE this second sort step exists and runs; OPEN what `0x0085d010` actually orders by and what `0x0149365c` semantically means.**

**Per-tick budget: CONFIRMED negative.** No "load at most N per frame" throttle exists anywhere in the acquire chain (`0x0085ee30` → `0x00dafea0`) — every candidate remaining after the sort/truncate steps above gets an acquire call issued in the same pass, unconditionally. The only bound is each level's own small fixed list capacity (6/1/7/7/4 for engine levels 0–4, §10.3) plus the fact that only the delta between the new wanted list and the previous active list is ever touched.

**Eviction: CONFIRMED immediate, not deferred/lazy.** The release path (`0x0085ef70` for the general per-variant case; the level-0/L2 case releases every variant of a dropped cell rather than just the selected one) calls a reference-count-decrement primitive, `0x00dafad0` (the SAME primitive §10.4 already cites as "a reference-count decrement that frees at zero," now independently confirmed and fully traced), and — the instant it reaches zero — calls further teardown routines (`0x00daf270` / `0x00daf010`) in the same call: no keep-alive counter, delay timer, or LRU structure found anywhere in this chain. The mirror-image acquire primitive (`0x00dafea0`, the SAME primitive §10.4 cites as "a load request") is likewise an immediate refcount increment/attach with a same-call fast path when already resident. Both primitives additionally check the calling thread's id (`GetCurrentThreadId` against a recorded owner at `0x01329ea4`) and silently no-op off-thread — confirming all loading/unloading is expected to run on one specific thread, with no async eviction worker.

### 11.5 The top-level per-frame driver (§10.9's own OPEN item 1) — advanced, still not closed

Starting from the driver hunt rather than assuming it is the same unresolved driver `spec-render-pipeline.md` separately flags for the render pipeline: the reconciliation entry `0x008610e0` is reached, among other already-known paths, through a short all-levels wrapper (`0x00861850`), itself called from: (i) unconditionally, at the tail of a Bink-video-playback state machine (`0x00bdbf30`); (ii) directly from a function (`0x007a8180`) that also flushes per-viewport pending render state; (iii) through a two-hop wrapper (`0x00854c90`) sitting in a real, resolved vtable at `~0x01164690` (surrounded by other genuine function-pointer slots at `0x01164680`–`0x011646a0`) behind a gate (`0x004578b0`/`0x00457210`) that is unconditionally true; (iv) one further raw call site not resolved to a named function. Critically, `0x007a8180` itself has **zero** static callers anywhere in the binary per Ghidra's own reference database — its one known invocation site (`0x007a8511`) is likewise called by nothing statically — meaning both are reached only through some indirect/callback dispatch not located this pass. **This is consistent with, and meaningfully extends, §10.9's own conclusion** ("no per-frame caller was found" — §10.4's wording, carried as §10.9 OPEN item 1) rather than closing it: the population of concrete trigger paths is now much richer (Bink-video tick, a viewport-flush routine, a vtable slot, camera-cut transitions, mission/activity transitions, hold-counter changes, startup/interior preload), but the single "runs once per rendered frame, unconditionally" driver remains **OPEN**. Concrete next step if resumed: find whatever writes a function pointer to `0x007a8511` (or resolves the vtable class at `0x01164690`), and separately trace what the registration helper `0x00bdc120` (called from the startup init `0x005d1a30`) actually registers `0x00bdbf30` *into* — that table's own per-frame walker is the most likely place the real driver lives.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): fixed 4 cross-references (§9.1 §9.4→§9.5; HANDOFF §11→§26.22 twice; §11.5 quote attributed to §10.4) and annotated the WALLS.md idiom reference (§11.3); marked vertex-layout, `.rig_pc` and `activity_types.xtbl` open items resolved by other specs (§5, §6, §10.8, §10.9); added forward pointers on §7.1/§7.2 unrecovered names and [OPEN] tags and on superseded §8.5/§8.6/§8.8 rows; fixed the wrong id in §8.4 (`0x14`→each id); added a spelling-conflict marker for category `0x27` (§9.4); reworded decompiler-shaped expressions and replaced the `param_1` auto-name with "the tier argument" (§8.2–§8.8, §9.1, §9.4, §9.5, §9.7); labelled the level-0 pool reading HYPOTHESIS (§10.9).
