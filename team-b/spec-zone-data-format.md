# Saints Row: The Third — `.czn_pc` Zone Data: a Scoped Partial Investigation

**Status: PARTIALLY RESOLVED (upgraded 2026-09-10 by the `.gzn_pc` pass; the top level's
independent-anchor negative added 2026-09-11, §8; the registration-table/entry-point pass
added 2026-09-12, §9; the `.gzn_pc` segment chain and a 13.4× correction to the published
block count added 2026-09-13, §10).** The **geometry is readable** — zones embed the
project's shared "Mesh" sub-block and `.gzn_pc` is its payload, both confirmed
population-wide (§7, and independently reproduced over the full population by a different
validator, §9.4). ⚠ **But how MUCH geometry was badly under-reported until 2026-09-13:**
a `.gzn_pc` is a contiguous chain of self-delimiting Mesh segments of which only the first
is 16-aligned, and the shared validator searched for segments at 16-aligned offsets only —
so §9.4's **2,843 blocks is really 38,152** over the same 1,083 pairs (§10.5). The
**file**-level coverage figure (1,002 of 1,083) is unaffected, and §7.3's inline-mode
population is separately **refuted** as a header-offset artefact (§10.6). The
**object placement / property stream is still opaque** — the per-type resource-construction
route to it is now a confirmed dead end (§9.1), and a promising but unconfirmed lead (a
generic named-property mechanism, §9.3) has replaced it as the open thread. *(2026-09-30, §9.5: a real top-level `.czn_pc` walker has since been found; the "no walker" negative of §9.2 is superseded.)* This document
records what holds, what was **tested and refuted**, and exactly where the investigation
stops. It is not titled as a format specification because the format as a whole is not
specified here.

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, taken after the registered-format list was exhausted — `.czn_pc` is the largest unexplored data set in the project (2,971 files, up to 2.5 MB each).
**Method:** Population census over all 2,971 shipped files; a structural hypothesis built from the bytes, then **tested against the whole population with controls**; an immediate-constant search across the entire binary; and a string-driven sweep of the level/zone module. Evidence: `tools/czn_parser.txt`, `czn_863310.txt`, `zone_strings.txt`, `zone_reader.txt`; harnesses `scratchpad/czn_bulk.py`, `czn_chunks.py`, `czn_probe.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. Observed field values and offsets are format data. Quoted strings are shipped data.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30).** An adversarial desk review (spec text and Team B's files only, not the executable or the game data) covered 27 units: **9 DESK-PASS** (§4, §7.1, §7.4, §8.2, §9.3 consistency-only, §9.6, §10.2, §10.7, §10.9); **11 DESK-PASS, text fixes applied** (§3, §5/§6, §7.2, §7.3, §8.1, §9.1, §9.2, §9.4, §10.3, §10.4, §10.6); **3 NEEDS-EXE** (§1, §9.5, §10.5.1); **2 NEEDS-DATA** (§2, §10.8); **2 VALIDATED-BY-DATA** (§10.1 for `p ≡ 0 / 4`, §10.5 for the 38,152-block count and 1,002-file coverage). A desk pass alone does not clear a unit: it means the text is internally consistent, not that it was re-derived. VALIDATED-BY-DATA units are already backed by Team B's full-population run. Two conflicts are marked in place and not settled here: the top-level step alignment (`align8` in §1/§8.1 versus the 4-byte round-up in §9.5), and the `.gzn_pc` tiling residuals, where Team B's full-population 1,002 / 1,002 tile supersedes this spec's 74-file / 284-block residuals (§10.8). **Awaiting the executable:** §1/§8.1/§9.5 step rule (0x00864c60, 0x007512f0, 0x008652d0); §7.2 version-field width (Mesh sub-parser via 0x00e718b0); §9.1 rows 29/30 of 0x00700780 and the 0x00dd2e30 null-ctor branch; §10.5.1 `~al` padding decision. **Awaiting real data:** §1 (D1, D3), §2 (D4), §7.1 (D5), §7.3 (D6), §9.4 (D7), §10.1 odd parity (D2), §10.3 (D8), §10.4 (D9), §10.5.1/§10.8 (D10, D11, D13), §10.6 (D12). The `.czn_pc` object-stream interior is ON HOLD by the owner; nothing about its content was added. *(Data-check and address lists: the 2026-09-30 zone desk review, `adv_zone.md`, not in this repository.)*

---

## 1. What was established

- **Population.** 2,971 distinct files, 180 bytes … 2,586,376 bytes, **every one a multiple of 4**. Paired 1:1 with `.czh_pc` (the zone header, resolved in `spec-ctorless-types.md` §5); only 1,083 have a `.gzn_pc` side. **[CONFIRMED — empirical.]**
- **The file opens with a typed record.** The first eight bytes are `{ u32 id, u32 length }` where the id always has its high bit set and its upper half is exactly `0x8000`. **The record that follows lands exactly at `align8(8 + length)` in 2,931 / 2,971 files (98.7%)**, against a **2.6%** control (the rate at which an id-shaped word appears at a *random* 8-aligned offset in the same files). The id/length reading is therefore real, not a coincidence of shape. **[CONFIRMED — empirical, controlled.]** **[CONFLICT — step alignment — RESOLVED 2026-10-08, see below.]** **[OPEN — desk review 2026-09-30: the 40 non-conforming files are not characterised; to be settled against real data (D1).]**

  **RESOLVED 2026-10-08 (SPEC TEAM, agent `ab80684a4e2668c8e`, dispatched to decode the Heightmap content kind, live-playtest priority):** the real top-level header is **12 bytes**, `{u32 tag, u32 length, u32 extra}` (`extra` observed `0x00000000` in every real record checked), with the next record at exactly `tagOffset + 12 + length` — **no alignment rounding at all**. Confirmed by independent disassembly of two handlers (`0x00864200` Heightmap, `0x008642a0` Navmesh) and population-validated with a corrected walker achieving **1,333/1,333 exact-EOF landings, zero errors**, over all `sr3_city~s*.czn_pc` files in both city archives. **Scope note, stated plainly: this 1,333-file population is the `~s`-family subset, not independently re-run against this section's own 2,971-file full population** — the two should agree (the `~s` family is not a special case in any way this project has found), but that re-run has not been done; flagged so a reader doesn't over-read "2,971" into a number that is actually "1,333." This explains why §2's own test (align4/align8/align16, assuming an 8-byte header) reached EOF in 0/2,971 files under every alignment tried: none of the three walked model used a 12-byte header at all, let alone a header-plus-rounding scheme — the header-SIZE assumption, not the rounding granularity, was the real bug. Full detail: `scratchpad/heightmap_0x2244_decode_2026-10-08.md` ("Finding 0"). **[CONFIRMED — disassembly, two independent handlers; CONFIRMED — empirical, 1,333/1,333 on the tested subset.]**
- **Only three distinct leading ids ship**: `0x80002233` (1,500 files), `0x80002237` (1,365), `0x80002234` (106). Across the whole population **790 distinct id values** occur. **[CONFIRMED — empirical.]** **[OPEN — desk review 2026-09-30: the harvest method for the 790 is not stated; with chains only 2–3 records deep (§2) and ~9 tag-handlers in the walker (§9.5), the figure may count id-shaped words at arbitrary or interior offsets; to be settled against real data by re-counting over records reached by the first 2–3 steps only (D3).]**
- **No id is a hardcoded magic.** A search over every instruction in the binary for the immediates `0x80002237` and `0x2237` returns **nothing**. The loader never compares the leading word against a constant, so these ids are resolved through data — a registry or table — not by a literal check. **[CONFIRMED — disassembly-level search of the whole image.]** *(Qualified 2026-09-30, §9.5: the real walker does compare `tag & 0x7fffffff` against fixed constants, but they are held in the two tag-constant globals at 0x013026f4 / 0x0130279c rather than encoded as immediates. The immediate-search negative stands; the "registry, not a literal check" inference does not.)*
- **Zone files carry world-object placement and behaviour data.** The embedded strings are property and asset names: a faction name and AI-behaviour property names (among them `use_default_loadout`, which §9.3 finds compiled into the executable as a property name the engine hashes and looks up), a script-execution property, platform / bounding-box / floating-state properties, a character-template reference, an environment prop asset name and a load-visibility flag. *(Paraphrased 2026-09-30 by the desk review: ten verbatim strings were quoted here; only `use_default_loadout` is shown by this spec to be a name the code itself compares, so only it is kept literally.)* **[CONFIRMED — empirical.]**
- **The zone module resolves dependencies by authoring extension.** One function references `.effectx`, `.cmeshx`, `.rigx`, `.todx`, `.ctdgx`, `.lmeshx`, `.smeshx`, `.fmeshx`, alongside `"tree objects"` and `"misc_resource"` — so zone contents reference other assets by their **authoring** names, which the module maps to shipped files. **[CONFIRMED — disassembly.]** This completes the project's authoring-extension list (§4).

**Review status (2026-09-30): NEEDS-EXE: the step alignment (align8 here versus the 4-byte round-up in §9.5) and the 790-id harvest; 0x00864c60 / 0x007512f0 / 0x008652d0, data checks D1, D3 — desk review (not re-derived from the executable).**

## 2. What was tested and refuted

**Hypothesis: the file is a flat stream of `{id, length}` records, payload, padded to 8 — i.e. a uniform tagged-chunk format.**

It is not. Walking that model:

| alignment | walks without error | **lands exactly on EOF** |
|---|---|---|
| 4 | 2,855 / 2,971 | **0 / 2,971** |
| 8 | 882 / 2,971 | **0 / 2,971** |
| 16 | 2,695 / 2,971 | **0 / 2,971** |

**No alignment reaches end-of-file in a single file.** Chains stop after 2 records (1,531 files) or 3 (1,144), and the walk then hits words that are not id-shaped — commonly a `0xFFFB`-family value. **[CONFIRMED — empirical, whole population, three alignment variants.]** **[OPEN — desk review 2026-09-30: the 8-aligned row (882) is the worst of the three although §1 states align8 as the rule (see §1's CONFLICT marker); "0 / 2,971 lands on EOF" sits beside §8.1's "reached EOF 32" over the 1,002 pairs; the definitions of "error" and "EOF" and the alignment used for the 2/3-record counts are not stated (1,531 + 1,144 = 2,675, leaving 296 files unaccounted); to be settled against real data (D4).]** **Explained 2026-10-08 (see §1's RESOLVED note above): all three rows tested an 8-byte header model; the real header is 12 bytes with no rounding — none of 4/8/16 could ever land on EOF because the header-size assumption, not the step granularity, was wrong.**

This is recorded because the model *looked* right: it reproduces the smallest file's five records perfectly by hand, which is exactly the trap the project's own lesson warns about — a rule that fits some data is not the loader's rule. The refutation is as useful as a confirmation would have been: it says the top level is **not** a uniform chain, so a reimplementation must not walk one.

**Review status (2026-09-30): NEEDS-DATA: alignment-table labelling and the EOF / error definitions (D4); step-rule conflict, see §1 — desk review (not re-derived from the executable).**

## 3. The reading the evidence supports

**[HYPOTHESIS — unconfirmed.]** A zone file is a **small number of typed top-level sections** (2–3 in the overwhelming majority of files), each introduced by `{id, length}`, whose **interiors use per-type encodings** rather than a recursive uniform chunk format. The large section holds the object stream. Supporting observations, each independently confirmed above: the leading record is real and its successor is exactly where its length predicts; chains are 2–3 deep, not dozens; 790 distinct ids exist but no id is literal-checked in code *(qualified 2026-09-30, §9.5: ids are compared against constants held in globals, not immediates)*; and the embedded content is property-name / asset-name data of the kind a data-driven object description produces. *(Desk review 2026-09-30: "2–3 sections" is a lower bound set by where the flat walk stopped — §8.1 shows most stops are on words that are not id-shaped — not a count of sections; and the 790 figure is subject to §1's OPEN harvest-method marker.)*

What this does **not** establish: the meaning of any id, the interior layout of any section, or how the `0xFFFB`-family values fit. Those are open.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## 4. Cross-format contribution

Independent of `.czn_pc` itself, this pass produced two durable results, both already folded into other documents:

- **The complete authoring-extension list** for this engine: `.zonex`, `.cmeshx`, `.lmeshx`, `.smeshx`, `.fmeshx`, `.rigx`, `.effectx`, `.todx`, `.ctdgx` — plus `.ctd` and `.animx` known previously. Authoring names appear *inside* shipped data as cross-references; the shipped counterparts are the `c`/`g` pairs this project has specified.
- **`.czh_pc` confirmed from the code side.** The zone-header reader validates the shared material block, applies **the same mandatory 16-byte pad arithmetic** documented in `spec-geometry-format.md` §3.1.1, then runs the `SR3Z` parser and stores the material-block pointer at `SR3Z + 0x08` (a field of the parsed in-memory header object, not a file offset). This independently reproduces the layout that `spec-ctorless-types.md` §5 established by replay, and identifies one further field. **[CONFIRMED — disassembly.]**

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 5. Where this stops, and why

Completing `.czn_pc` requires the **per-section readers**, and those are inside the zone streaming subsystem: a ~7.6 KB function owning the *"zone header cache"* / *"zone always loaded"* strings, a ~2.7 KB dependency resolver, and a ~3.4 KB loader. That is a **runtime-subsystem investigation** — the category the user explicitly scoped out of this project — rather than a format pass, which is what this was taken on as.

That boundary was not visible in advance: `.czn_pc` was selected precisely *because* it looked like a format pass, and it is reasonable that it did. The honest finding is that **this particular format does not have a standalone parser to read** *(⚠ SUPERSEDED 2026-09-30, §9.5: a real top-level walker exists, `0x00864c60`/`0x007512f0` via `0x008652d0`)* — unlike every other format in this project, its structure is only expressed in the code that walks it section by section.

**What a reimplementation can and cannot do.** It *can* read the zone **header** (`spec-ctorless-types.md` §5, fully resolved), it *can* read the zone's **geometry** — the embedded Mesh sub-blocks and their `.gzn_pc` payload, using machinery already specified in `spec-geometry-format.md` §4.1 (§7 below) — and it *can* extract the property and asset name strings. It **cannot** read the object placement/property stream, which is what still needs the per-section readers.

## 6. Open Items

1. The per-section interior layouts **for the non-geometry sections** — needs the zone streaming subsystem (§5); a **user scope decision**, not a next step this pass should take unilaterally. *(The geometry sections are no longer open — see §7. The scope restriction was released 2026-09-11, HANDOFF §27.3, and this item was attempted under that release: see §9. Result — the per-type registration/constructor route is now a confirmed dead end (§9.1), four further named entry points contain no top-level walker and an exhaustive instruction-idiom census found none elsewhere in the binary (§9.2) *(⚠ SUPERSEDED 2026-09-30, §9.5: a real walker exists, `0x00864c60`/`0x007512f0` via `0x008652d0`; the census predicate missed its compiled form, `WALLS.md`)*, and a new, disassembly-confirmed lead — a generic hash-keyed named-property mechanism independently tied to a confirmed zone-content string — was found by a different route and is not yet closed (§9.3/§9.5).)*
2. What the 790 id values are and where the table that resolves them is built.
3. The `0xFFFB`-family values that terminate every flat walk.
4. The relationship between the three leading ids (`…2233` / `…2234` / `…2237`) and the zone `fast`/`slow` type split. *(Desk review 2026-09-30: `fast`/`slow` are the registration-table names of §9.1; `spec-asm-format.md` §9.3 names `container_kind` 29/30 "Zone"/"Zone (High LOD)", a different id space — see §9.1's note.)*
5. ~~`.gzn_pc`~~ — **resolved as the shared g-file payload, §7** — and its internal
   structure resolved further 2026-09-13: it is a **contiguous chain of self-delimiting Mesh
   segments** beginning at byte 0, each `[u32 tag][payload][u32 tag]`, packed back to back
   (ordinary zones) or padded to the next 16-byte boundary between segments (`~al` zones);
   928 of 1,002 pairs tile exactly to EOF, controls 0 of 1,002 both ways (§10.5) *(⚠ superseded as a population figure by Team B's 1,002 / 1,002 tile to EOF — see the conflict note in §10.8.)*. §7.1's
   "tag, three zero words, then the index buffer at `+0x10`" describes the FIRST segment;
   nobody had asked how many segments a `.gzn_pc` holds, and the answer is a mean of ~38.** Remaining within it: the per-channel vertex layout beyond the index buffer, which is the same standing open item as `spec-geometry-format.md` §4.1.2 and not specific to zones.
6. Which of the 790 section ids introduces the geometry sections — the Mesh blocks were located by their own signature, not by walking the section list. *(Partially advanced 2026-09-30, §9.5: ids `0x2237` and `0x2251` have geometry-shaped handlers; field layout provisional, `spec-terrain-format.md` §5 item 2.)*
7. **Why 74 of 1,002 `.gzn_pc` files do not tile to EOF under any segment-packing rule** (§10.8). They break late — e.g. `sr3_city~s1213` at cursor 1,880,316 of 1,883,284 after 256 segments — on "no bookend at the declared length" or "declared length overruns". Added 2026-09-13. **[OPEN / UNKNOWN.]** **[SUPERSEDED as stated — desk review 2026-09-30: Team B tiles 1,002 / 1,002 under the cursor-or-next-16-boundary rule, see §10.8's conflict note; what remains open is why this pass's walker failed on these 74 (D10).]**
8. **Whether the 921 of 39,073 g-backed zone candidates that close under no alignment are the same set as the ~915 whose primary-buffer element-size byte is not 2** (§10.8). Two residuals of nearly equal size; a set comparison would settle it and was not run. Added 2026-09-13. **[OPEN.]**
9. **The ~671 `.ccar_pc` g-backed Mesh-block candidates that close under neither alignment** (1,038 candidates over 367 pairs, 367 validating either way, §10.7). Vehicles are not affected by the alignment gate, so this residual needs its own explanation. Flagged, not diagnosed. Added 2026-09-13. **[OPEN / UNKNOWN.]**
10. **`mesh_scan.py`'s `p + 0x0E` and `p + 0x12` header-offset branches** (anchors at `p ≡ 2` / `p ≡ 6 (mod 8)`) are unexercised by any shipped zone block — 0 of 2,843 validated, 36 and 16 candidates respectively (§10.1). Untested, not confirmed. Added 2026-09-13. **[OPEN / UNKNOWN.]** **[OPEN — desk review 2026-09-30: measured on the old 2,843-block set, not re-run on the corrected 38,152; Team B's locator uses 4-aligned anchors only, so its 38,152 does not cover these branches; to be settled against real data (D2).]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (items 5, 7, 10 marked against §10.5/§10.8 and Team B; item 4 naming note) — desk review (not re-derived from the executable).**


## 7. The geometry *is* readable — the shared Mesh mechanism generalises (added 2026-09-10)

The `.gzn_pc` pass tested whether the g-file mechanism established for `.gcmesh_pc` (`spec-geometry-format.md` §4.1.2 items 9–11) and `.gsrt_pc` (`spec-tree-format.md` §9) also covers zones. **It does, exactly.**

### 7.1 `.gzn_pc` is a tagged segment payload

| Finding | Result | Control |
|---|---|---|
| The g-file's leading `u32` appears as a `u32` in its **own** c-file | **1,002 / 1,002 (100%)** | another pair's leading word, in the same c-file: **6.5%** |
| `+0x04`, `+0x08`, `+0x0C` are zero | **1,002 / 1,002** | — |
| `+0x10` begins an ascending `u16` index buffer (`0,1,2,2,3,3,3,4,…`) | `u32@0x10 == 0x00010000` in **991 / 1,002** | — |

That is byte-for-byte the segment shape documented for trees: **tag, three zero words, then the index buffer at `+0x10`**. 1,083 `.gzn_pc` ship against 2,971 zones, and **81 are zero bytes** — a zone with no geometry payload. **[CONFIRMED — empirical, controlled.]**

**Review status (2026-09-30): DESK-PASS (the 11 of 1,002 files whose `+0x10` word is not `0x00010000` are uncharacterised, data check D5) — desk review (not re-derived from the executable).**

### 7.2 `.czn_pc` embeds the shared "Mesh" sub-block

At the position where the g-file's tag occurs in the c-file, the preceding `u32` is **`9` in 1,002 / 1,002 files** — the shared Mesh sub-block's version field. Scanning a ±20-byte window, **no other offset shows a version-9 reading above 4/1,002**, and a random-offset control's best single offset is 6/1,002. The word after the tag is a plausible block length in 980/1,002 *(the plausibility criterion is not stated)*. **[CONFIRMED — empirical, controlled.]**

~~So a zone's geometry is laid out as: `u32 version (9)` | `u32 check value` | `u32 block length` | the Mesh header — the structure `spec-geometry-format.md` §4.1.1 specifies, with the check value tagging its `.gzn_pc` segment.~~ **Corrected 2026-09-30 (desk review, from §10.1 and Team B's byte-level read in `team-b/HANDOFF.md` lines 6512-6517):** the pre-header is a computation, not a fixed layout — the version word (9), then round up to 4, then three `u32` fields (check value, c-side length, g-side length), then round up to 8, where the Mesh header of `spec-geometry-format.md` §4.1.1 begins; the check value tags the block's `.gzn_pc` segment. The struck line omitted the g-side length. **[OPEN — desk review 2026-09-30: §10.1 calls the version field a `u16` while this section's anchor reads a `u32 == 9`; the two readings agree only for 4-aligned anchors; the width is to be settled against the executable (the Mesh sub-parser reached via 0x00e718b0).]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (pre-header layout corrected to §10.1; version width OPEN) — desk review (not re-derived from the executable).**

### 7.3 Population profile of the embedded meshes — ⚠ two of five bullets REFUTED 2026-09-13, see §10.6

Enumerating blocks whose check value is hash-like (`≥ 0x10000`, which removes the small-integer coincidences that made a first attempt uninterpretable) across 400 zones:

- **4,478 Mesh blocks**; most zones carry 1–3, but 51 carry eight or more. *(Desk review 2026-09-30: unreconciled with §10.5 — over the 1,002 nonzero pairs, 580 files carry 37,730 blocks, a mean of ~65 per multi-block file — and §10.6 could not re-derive the 4,478 denominator; the 400-zone sample is not stated to be representative.)* **[OPEN — desk review 2026-09-30: per-file block histogram over all 1,002 pairs; to be settled against real data (D6).]**
- **The bookend holds in 4,477 / 4,478** — each block ends with a second copy of its own check value, the same property found in trees. A specific `≥ 0x10000` value landing at a computed offset has a ~2⁻³² chance rate per trial, so this is decisive on its own arithmetic.
- ⚠ **REFUTED 2026-09-13 — §10.6.** Original wording: ~~**Flags bit 0 set (g-backed) in 3,448 / 4,478**; the remainder are inline-mode, as foliage is.~~ The ~1,030-block "inline-mode" remainder is an artefact of this bullet's own harness (`gzn_meshes.py`), whose header offset `ap(S+12,8)` lands 8 bytes early for anchors at `S ≡ 4 (mod 8)` — in **1,055 of 1,055** such anchors the byte it read as "flags" is the low byte of the g-length field, and that field is even in **1,048 of 1,055**, so bit 0 reads clear. **Replacement figure, whole 1,083-pair population, `mesh_scan.py`'s dynamic header offset: 292 of 39,366 candidates (0.74%) read flags bit 0 clear**, not 23%. See §10.6.
- ⚠ **PARTLY REFUTED 2026-09-13 — §10.6.** Original wording: ~~**Stride byte: 2** in 3,448 (matching the g-backed count exactly), then 12, 4 and 16 — the same stride profile as `.ccmesh_pc` and `.csrt_pc`.~~ The leading value is right and holds far more strongly than stated — **2 in 38,152 of 38,152** blocks over the corrected zone population (§10.5). **The "12, 4 and 16" tail is the same artefact as the bullet above**, read 8 bytes early at `S ≡ 4 (mod 8)` anchors: **0 of those 1,055 anchors reads 2**. And the parenthetical "matching the g-backed count exactly" was read at the time as corroboration; it is the opposite — both reads come off the same header pointer, so both fail together on the same anchors (§10.9's methodology note).
- Count field: median 132, maximum 21,822. *(Desk review 2026-09-30: which header field and offset "count" denotes is not stated, and it was read off the same partly mis-offset anchors, §10.6.)*

**[CONFIRMED — empirical, for the block count, the bookend rate, the leading stride value and the count field. REFUTED for the flags/inline split and the stride histogram's tail — see the two marked bullets and §10.6. This label previously read a bare `CONFIRMED — empirical` covering all five bullets.]** *(Desk review 2026-09-30: the block count 4,478 is qualified by §10.6's finding that its denominator is not re-derivable; read it as OPEN, not CONFIRMED.)*

**Review status (2026-09-30): DESK-PASS, text fixes applied (bullet 1 and count field marked; block-count label qualified per §10.6) — desk review (not re-derived from the executable).**

### 7.4 What this changes

Zone **geometry** is not a new format and does not need the zone streaming subsystem: it is the machinery this project already specified, reachable by locating the version-9 signature. ⚠ **Added 2026-09-13:** locating the version-9 signature finds the *descriptor*; finding its matching payload segment in the `.gzn_pc` is a **chain walk, not a search** — see §10.4/§10.5.1, and do not reuse `mesh_scan.py`'s 16-aligned tag search, which reaches only 7.28% of them. What remains opaque is the **object placement and property stream** — the sections carrying §1's faction and behaviour property names (`use_default_loadout` and the rest) — which is what §5's scope boundary actually applies to.

**Correction to this document's original framing:** the first version of §5 stated that a reimplementation "cannot currently read `.czn_pc`". That was too pessimistic — it was written before the shared-mechanism test was run. The geometry was accessible the whole time through machinery already documented elsewhere in the project.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 8. Attacking the top level from an independent anchor (2026-09-11, user-authorised)

**One usable result, both of this pass's other measurements discarded as vacuous.**
Reported that way because the vacuous ones looked like progress and the surviving one is
a negative.

### 8.1 The Mesh block is NOT a top-level section — controlled negative

The top level is a chain of `{u32 tag (high bit set), u32 length}` records, 8-aligned. **[CONFLICT — step alignment: see §1's marker and §9.5; 0x00864c60 / 0x007512f0 / 0x008652d0, data check D1. RESOLVED 2026-10-08, see §1: the real header is 12 bytes `{tag,length,extra}`, no rounding.]**
§7.2 gives a locator for the embedded Mesh sub-block that is **independent of those
lengths** (`u32 == 9` followed by the paired `.gzn_pc`'s own tag), so the file carries two
independent descriptions of where things are and they can be checked against each other.

| Test | Result |
|---|---|
| A chunk **body starts exactly** at the first Mesh block | **0 / 1,002** |
| *control* — a chunk body starts exactly at a random 4-aligned offset in the walked span | 3 / 1,002 (0.3%) |

**The real reading scores at or below its own control.** So the Mesh block never begins at
a top-level record boundary: **the geometry is nested inside a record's interior, not
carried as a top-level section of its own.** A reimplementation must not expect to reach
geometry by walking the top level. **[CONFIRMED — empirical, controlled.]**

Descriptively, walking from offset 0 over the 1,002 `c`/`g` pairs stops as: `tag lacks
the high bit` 567, `zero tag` 228, `length overruns the file` 175, `reached EOF` 32; and
the chain is 2 records deep in 444 files and 3 in 505, matching §2's shape.

*(Desk review 2026-09-30: 0 / 1,002 against an expected ~3 has little statistical power; the "nested, not top-level" conclusion rests more on the independent walker finding of §9.5 than on this statistic. The "reached EOF 32" stop count here sits against §2's 0 / 2,971, see §2's OPEN marker.)*

**Review status (2026-09-30): DESK-PASS, text fixes applied (step-rule conflict marked; statistical power noted) — desk review (not re-derived from the executable).**

### 8.2 Two measurements discarded — recorded so they are not repeated

**(a) "A declared chunk brackets the Mesh block in 830 / 1,002" is TAUTOLOGICAL.** Chunks
tile the walked span contiguously, so any offset the walk reaches is inside some chunk by
construction. The giveaway was arithmetic: the bracket count (830) and the count of files
where the walk simply got past the mesh (830) are **the same number**. A containment test
against a contiguous tiling measures only how far the walk travelled.

**(b) "Re-anchoring the walk at the Mesh bookend repairs 400 files" is VACUOUS.**
Resuming the chain just past the geometry raised clean termination from 260/1,002 to
623/987, which looked decisive. It is not: **600 of those 623 terminations traversed ZERO
records** — the walk found an immediate zero `u32` and declared success — and controls
resuming at bookend+8/+16/+64/+256 score 52.4% / 51.6% / 36.7% / 54.3% against the real
reading's 63.1%. Zones contain enough zero padding that "a zero word appears where a tag
was expected" is close to free. **A terminator test must require a minimum record count
and be quoted against controls**; neither was true of that measurement.

Both were caught by asking what a failure would have looked like, before publication and
not after. The independent-anchor *idea* is still the right one — §8.1 is its product —
but two of the three tests built on it could not have failed.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 9. The zone module's own parser: a confirmed absence, and the mechanism it most likely feeds (2026-09-12)

**Prepared by:** SPEC TEAM, 2026-09-12. This section resumes the item §6.1/§8's "read the
zone module's own parser" left open, working forward from the registration table exactly as
directed. **The registration-table route is a confirmed dead end for a structural reason,
not a search failure** — and four further named-entry-point traces plus one exhaustive
instruction-idiom census also came back empty for the top-level walker *(⚠ SUPERSEDED 2026-09-30, §9.5: a walker exists)*. One genuinely new
mechanism was found by a different route (a confirmed zone-content string), and one existing
finding (§7) was independently reproduced over the complete population with a different,
stronger validator. Evidence: `tools/scripts/CznFindTagWalk.java`,
`CznDumpPrepackaged.java`, `CznFindZoneManager.java`, `CznPropStrings.java`,
`CznPropTable.java`, `CznPropResolver.java`, `CznPropVtable.java`, `CznPropClass.java`,
`CznPropCallers.java`; dumps in `scratchpad/czn/*.txt`; harnesses
`tools/harnesses/czn_mesh_scan_zones.py`, `czn_mesh_scan_zones_perfile.py`.

### 9.1 The registration-table route is closed by construction: `.czn_pc`/`.gzn_pc` have a literal NULL constructor

The master resource-registration table (`0x00700780`, `spec-geometry-format.md` §4.3) has
two rows for these extensions — registered-type ordinals **29** (`0x1d`, "Zone (fast)") and
**30** (`0x1e`, "Zone (slow)"). Read directly from the disassembly (`tools/reg_table_full.txt`
lines 493–520): **both rows point their extension-string fields at the same `.czn_pc`/
`.gzn_pc` string pair, both declare a 16-byte size field, and both set their constructor AND
destructor function-pointer fields to a literal `0`.** No vtable, no thunk, no stub — the
table entry itself is null. **[CONFIRMED — disassembly.]**

The generic resource-construction dispatcher (`0x00dd2e30`, already documented for a
different set of ctorless types in `spec-ctorless-types.md`) contains an explicit branch for
this case: it fetches the ctor pointer for the resource's registered-type ordinal, and if
that pointer is `0`, it jumps past the indirect call entirely, straight to the shared
success tail that marks the resource "loaded" and returns success. **A null constructor is
therefore not an error path — it is a supported, intentional mode**, already established
for types 39/44/45 and **now separately confirmed to include 29 and 30, i.e. `.czn_pc`/
`.gzn_pc` specifically.** The dispatcher decompresses the entry, resolves the paired
`.gzn_pc` buffer (the same generic filename-substitution mechanism `.ccmesh_pc` uses,
`spec-geometry-format.md` §4.1.2 item 9), and leaves both buffers registered — but **no
type-specific code ever runs against them through this path.** **[CONFIRMED —
disassembly.]**

This sharpens, with an exact mechanism, something this document already believed for a
different reason (§1/§3's "no id is literal-checked" argument, and HANDOFF §22's "no standalone
parser"): there was never a vtable-dispatched constructor here for data-side discovery to
find, because the table slot is a literal zero, not a hidden indirection. **Methodology
note:** the task brief's expectation — that a registered type's constructor is
vtable-dispatched and only findable by data-side discovery — held for the morph applier and
for every other type this project has resolved, but it does not hold universally, and the
registration table is cheap enough to check first: reading two 4-byte fields settled this in
under a minute, against what could have been an open-ended vtable hunt.

*(Desk review 2026-09-30: consistent with `spec-format-inventory.md` rows 29/30 and `spec-ctorless-types.md`. `spec-asm-format.md` §9.3 names `container_kind` 29 "Zone" and 30 "Zone (High LOD)"; that is the `.asm_pc` manifest's container-kind id space, a different table from this registration ordinal, and this spec does not establish that the two numberings correspond. Re-check against the executable: rows 29/30 of 0x00700780 and the null-ctor branch in 0x00dd2e30.)*

**Review status (2026-09-30): DESK-PASS, text fixes applied (naming note added) — desk review (not re-derived from the executable).**

### 9.2 Four further named entry points traced; none contain a walker of the top-level `{tag, length}` chain

Since the per-type path is closed, the walker (if a single one exists) must be bespoke code
reached some other way. Four candidates reachable from already-established zone-relevant
entry points were read in full:

| Entry point | What it actually does | Touches `.czn_pc` bytes? |
|---|---|---|
| `0x005d58f0` + seven ~147-byte helpers (`0x005d5440/54e0/5580/5620/56b0/5750/5850`) | Registers the fixed streaming-category table at startup (`spec-world-streaming.md` §7) | No — startup-time pool declarations only |
| `0x00859110` (2,705 bytes) | Given a zone's base name, classifies it (a byte read from a per-name lookup) into a streaming category, then builds filenames and issues load requests for the zone's *referenced* assets (`.clmesh_pc`, `.ctdg_pc`, `.cvbm_pc` minimap — **consumer-side write-up, 2026-10-08: `spec-hud-runtime.md` §6.3**, confirming the minimap tile texture streams in/out through this exact mechanism, not a separate system) | No — resolves dependencies by name, never opens `.czn_pc`'s own buffer |
| `0x00863080` ("SR3Z" parser) + `0x00863170`/`0x00863310` (~~`.zonex` filename helpers~~ zone-name part parsers — corrected 2026-10-02) | ~~Parses the **`.czh_pc`** header block (magic `0x5A335253`, version range 27–29) and swaps authoring/shipped extensions~~ **Corrected 2026-10-02:** `0x00863080` parses the **`.czh_pc`** header block (magic `0x5A335253`, version range 27–29) — this half stands. `0x00863170`/`0x00863310` do **not** swap extensions and reference none of the `.czn_*`/`.czh_*`/`.gzn_*` tables; they pull apart and test pieces of a zone *name* (see the correction note below the table) | No — this is the already-documented zone-*header* reader, a different file *(2026-10-02: true of `0x00863080`; the other two never touch file bytes at all, only name strings)* |
| `0x00db1ad0`/`00db2ce0`/`00db33d0`/`00db3450`/`00db3fd0`/`00db4980` (a "streaming container" cluster found via the strings "prepackaged stream", "unrecognized prim type/streaming allocator/container type") | Generic, engine-wide bookkeeping for any streamed container (retry/allocate-wait state machine) plus a **text-driven** registry that resolves *names* of primitive types, stream allocators and container types via per-name lookup calls | No — this is shared infrastructure for every streaming container in the game, not zone-specific, and it resolves by name/string, not by the numeric `{tag,length}` records `.czn_pc` actually uses |

**Correction note (2026-10-02, re-derived from the executable; cross-checked against `spec-ctorless-types.md` §4).** The third row's two helpers were mis-described as extension swappers. Neither references the `.czh_*` (`0x011654e0`), `.czn_*` (`0x011654f4`) or `.gzn_*` (`0x01165508`) platform tables, and neither replaces one extension with another. Both work only on the text of a zone name. Sources: bus job `20261001T123128-team-a-dvje` (`func1/func_0x00863170.txt`, `func1/func_0x00863310.txt`), plus a fresh read-only dump made for this pass of the two helpers they lean on, `0x00ea5af0` (a forward search for the first occurrence of one character) and `0x00da8850` (returns a name's extension: the text from the last `.`, or an empty string when a `/` or `\` follows that dot).

- **`0x00863170`** (body `0x00863170`–`0x0086320f`; takes a zone name, an output buffer and a keep-markers flag; returns success/failure) **extracts the name part that follows the first `~`.** It finds the first `~` in the name and fails if there is none. It steps past that `~` and the one character after it. It then searches onward for a closing delimiter, which depends on the name's extension: when the extension equals `.zonex` (case-insensitive compare) the delimiter is another `~`, otherwise it is `.`. It fails if no delimiter is found. Otherwise it copies the characters between the start point and the delimiter into the output buffer and terminates them. Last, unless the flag is set, it deletes from the copy the first character that is one of `!`, `$` or `` ` `` (the table of these three at `0x01165610`, shared with `0x00863210`), closing up the gap; only one such character is removed. The `.zonex` literal (`0x01165614`, its only reference in the binary) is used purely as this comparison key. It has ten call sites in nine functions, among them `0x00859110` (this table's second row), `0x00859cd0` (twice) and `0x00856f50`. **[CONFIRMED — disassembly.]**
- **`0x00863310`** (body `0x00863310`–`0x00863380`; takes one name; returns true/false) **tests for a four-digit numeric lead.** It returns true only when the first four characters are all decimal digits *and* the name is either exactly four characters long or has one of `!`, `$`, `` ` ``, `^` or `~` as its fifth character. A name shorter than four characters, or with any other fifth character, fails. Five callers, among them `0x00859110`. **[CONFIRMED — disassembly.]**
- **How the pieces fit — inferred.** The sibling spec `spec-ctorless-types.md` §4 reports that `0x00863110` reads the character right after a zone name's first `~` as a fast/slow letter (`f` / `s`), which is exactly the character `0x00863170` skips. That points to zone names built roughly as: a four-digit number, an optional marker (`!`, `$`, `` ` ``, `^`), a `~`, the fast/slow letter, then a name part that ends at the extension's dot (or at a further `~` in a `.zonex` authoring name). **[HIGH CONFIDENCE — inferred from the two parsers' shape plus the sibling spec's `0x00863110` finding; which string each caller passes, and the meaning of the marker characters, were not traced.]**

~~**[CONFIRMED — disassembly, each function read and described above.]**~~ **[Label scoped 2026-09-30 (desk review): CONFIRMED — disassembly for the description of each function above only; as support for "no top-level walker exists" it is SUPERSEDED by §9.5.]** *(2026-10-02: the original third-row description of `0x00863170`/`0x00863310` did not hold up — see the correction note above; the label covers the corrected wording only.)* None reads a `u32`
at a cursor, checks its upper half against `0x8000`, and advances by a second `u32` length —
the operation `spec-zone-data-format.md` §2/§8 established purely from file-side replay. *(⚠ SUPERSEDED 2026-09-30, §9.5: the walker exists elsewhere, in `0x00864c60`/`0x007512f0` via `0x008652d0`, not among the entry points above.)*

**A whole-binary census closes this further.** Rather than another small-displacement scan
(no selectivity at this project's scale, HANDOFF §5), the search looked for the
*co-occurrence*, inside one function, of (a) any instruction referencing the 32-bit value
`0x80000000` and (b) an `AND` instruction masking by `0xFFFFFFF8` (an 8-byte-alignment idiom)
or `0xFFFF0000` (an upper-half isolation idiom) — the two arithmetic signatures a `{high
bit/upper-half-set tag, 8-aligned length}` walker would need together. This is exhaustive,
not sampled: every one of the binary's instructions was inspected once. **Result: 14
functions total in the entire 41,785-function binary satisfy both conditions, and all 14 are
CRT/heap-allocator internals** (`__cftoa_l`, two copies of `__tsopen_nolock`,
`___sbh_alloc_block`, and ten functions in the tight `0x452000–0x454000`/`0x4b2000` range
consistent with the statically-linked small-block heap). **None sits in any zone-relevant
address range or is reachable from any entry point in the table above.** ~~**[CONFIRMED —
disassembly, exhaustive census, `CznFindTagWalk.java`.]**~~ **[SUPERSEDED 2026-09-30 by §9.5 — the census's 14-function count is not disputed, but its reading as a walker negative is refuted; label withdrawn (desk review).]** Stated as a predicate: a walker
matching this shape, anywhere in the binary, would have shown up in this list; it did not. *(⚠ SUPERSEDED 2026-09-30, §9.5: the real walker's compiled form uses `& 0x80000003`, a branching round-up and `& 0x7fffffff`, none of which this predicate covered; see `WALLS.md`.)*

**Two scope-jumps, flagged and not pursued**, per this project's standing discipline
(HANDOFF §5): `0x00da90d0`/`0x00da8d90`, called from `0x00859110` to test whether a
named handler exists for a zone's base name, turned out to have **75 unique direct
callers** spread across totally unrelated subsystems (rendering, physics, UI, audio) — a
generic, engine-wide named-callback/event lookup, not a zone-specific manager. It was not
traced further. **[CONFIRMED — disassembly, call-site census.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (CONFIRMED labels beside the SUPERSEDED marks struck/scoped) — desk review (not re-derived from the executable).** **Review status (2026-10-02, re-derived from the executable / cross-checked against `spec-ctorless-types.md` §4): third row CORRECTED.** `0x00863080` as the `SR3Z` / `.czh_pc` header parser is unchanged. `0x00863170` and `0x00863310` were re-read (bus job `20261001T123128-team-a-dvje` dumps, plus fresh dumps of `0x00ea5af0` and `0x00da8850`). They are zone-name part parsers, not extension swappers, and reference no `.czn_*`/`.czh_*`/`.gzn_*` table. The rest of the unit is not re-derived.

### 9.3 A different route found the engine's generic named-property mechanism, tied to a confirmed zone-content string

`spec-zone-data-format.md` §1 already lists `use_default_loadout` and a script-execution property
among the strings embedded in shipped `.czn_pc` data. Searching the **executable's own**
string data for these exact strings (rather than searching for zone-specific code) finds
that several of them are *also* compiled into the game's code as literal comparison targets
— i.e. they are not just file content, they are property names the engine's own code
recognises. **[CONFIRMED — empirical, string match against the EXE's data section,
`CznPropStrings.java`.]**

Following the direct code reference to `use_default_loadout` (`0x00a33ff0`) leads to a
small, complete, disassembly-readable subsystem:

- **`0x00d9e740`** — a table-driven hash over the **lowercased** string (shape matches
  this project's already-documented general-purpose name hash, `spec-vpp-container.md`
  §2.2/`spec-extensionless-types.md` §4; not proven to be byte-identical to the specific
  routine documented there, since it lives at a different address, but the fold-to-lowercase
  + table-driven-word loop shape is the same). **[CONFIRMED — disassembly for this
  routine's own mechanics; HIGH CONFIDENCE — inferred that it is the same hash family.]**
  *(Resolved 2026-09-30: `0x00D9E740` is the engine's table-driven CRC-32 (lower-cased, no
  final XOR), CONFIRMED in `spec-tables-environment.md` §1.4 and `spec-tables-weapons-combat.md`
  §1.4; `spec-extensionless-types.md` §4 explains why this CRC-32, not the rotate-6/XOR hash of
  `spec-vpp-container.md` §2.2, is the engine's general-purpose name hash.)*
- **`0x00a33730`** — resolves a hashed name to a small ordinal (a byte, `0xFF` = absent)
  via a **40-bucket** hash table (`hash mod 0x28`) with an internal free-list allocator that
  assigns a fresh ordinal to a name seen for the first time. **[CONFIRMED — disassembly.]**
- **`0x00a33ff0`** — a setup routine that, for each of roughly thirty hardcoded property
  names (`always_see_player`, `attack_enemies_on_sight`, `cant_flee`, `respawn`,
  `immaterial`, `use_default_loadout`, and others — all AI/spawn-behaviour flag names), hashes
  the name, resolves its ordinal, and records which bit of a flags word that ordinal maps to.
  **[CONFIRMED — disassembly.]**
- A small class, reached through a vtable at `0x0117f2a4`, built around that ordinal space:
  slot 2 (`0x00a33de0`) is a **`HasProperty(hash)`** query; slot 4 (`0x00a33e80`) is a
  **`SetProperty(hash, 8-byte value)`** call that resolves the hash to an ordinal and writes
  into a fixed 40-entry, 8-byte-stride array; slot 3 (`0x00a33e00`) **bulk-deserialises the
  whole object directly from the engine's generic serialisation stream** (`0x00da7e90`
  read / `0x00da8020` tell / `0x00da7f30` seek — the same generic stream abstraction used
  for on-disk and in-memory object I/O elsewhere in this project) — a fixed `0xF8`-byte header,
  an 8-byte alignment, then a fixed `0x140`-byte (= 40 × 8) value table. **[CONFIRMED —
  disassembly.]**

**What this shows, confirmed independent of any zone-specific claim:** this engine has a
generic, hash-keyed named-property mechanism — get/set by name, backed by a small
per-class ordinal table built once from a hardcoded name list — and at least one of that
list's names is confirmed to be literal `.czn_pc` object-stream content. This is a
mechanism-level explanation, not previously available, for **why no zone id or property is
ever literal-compared** (§1/§3): identity here is a *runtime hash*, resolved through a table,
never a compile-time immediate — exactly the shape that defeats an immediate-value search of
the kind this project already ran and reported negative (§1's "search over every instruction
for `0x80002237`/`0x2237` returns nothing").

**What is NOT established, and is the honest boundary of this lead:** whether *this specific
class's* bulk loader (`0x00a33e00`) is ever invoked with the stream cursor positioned
inside a `.czn_pc` buffer specifically, as opposed to a separately-registered
character/behaviour-template resource that a zone's spawn record merely *references* by
name. All three of this class's own entry points (`0x00a33e00`, `0x00a33e80`,
`0x00a33ff0`) resolve, on a caller search, to a single unresolved indirect/data reference
each — consistent with dispatch through yet another table this pass did not open, and not
informative about who holds that table. **[HIGH CONFIDENCE — inferred, that this mechanism
is what the object/property stream ultimately feeds; OPEN, whether this exact class's
loader is ever handed a `.czn_pc`-backed stream directly.]**

**Review status (2026-09-30): DESK-PASS (consistency only — the object-stream interior is on hold; internal arithmetic ties: `0x28` = 40 buckets, `0x140` = 40 × 8) — desk review (not re-derived from the executable).**

### 9.4 Independent, full-population confirmation that zone geometry uses the shared Mesh sub-block (§7, reproduced by a different method)

The task's secondary question — whether zone geometry uses the same Mesh sub-block as other
carriers — is already answered CONFIRMED in §7 by a bespoke anchor script over a 400-zone
sample. `mesh_scan.py`, the project's shared, format-agnostic Mesh-block validator (locates
the block's check value inside the paired g-file at a 16-aligned offset, replays the channel
walk, and requires it consume **exactly** the block's own declared length **and** land on a
second copy of the same check value — a compound false-positive rate on the order of 2⁻⁶⁴ by
construction, not a heuristic threshold), was run independently against the **complete**
paired population, not a sample.

**Population:** every `.czn_pc`/`.gzn_pc` filename pair found (recursively, through nested
containers) in `sr3_city_0.vpp_pc`, `sr3_city_1.vpp_pc`, `sr3_city_missions.vpp_pc`,
`dlc1.vpp_pc`, `dlc2.vpp_pc`, `dlc3.vpp_pc`, `misc.vpp_pc`, `startup.vpp_pc`,
`patch_compressed.vpp_pc`, `patch_uncompressed.vpp_pc` — **1,083 pairs**, exactly the total
this document's §7.1 already established as the full shipped population (no other archive
was checked, but this total's exact match to the previously-published whole-install count is
the evidence there was nothing left to find elsewhere). **Predicate:** a pair counts as a hit
if `mesh_scan.py`'s validator confirms at least one block. **What a failure would have looked
like:** a `.czn_pc` file whose embedded block(s) claim geometry (a nonzero, non-degenerate
`.gzn_pc`) that does not actually replay to the declared length or does not end on the
bookend — i.e. a zone where the two files' descriptions of the geometry disagree.

**Result: 1,002 of 1,083 pairs (92.5%) contain at least one independently validated
g-backed Mesh block** — ~~2,843 validated blocks in total across those files~~.

⚠ **Corrected 2026-09-13 (§10.5): the *file* figure above stands unchanged, but the
*block* count was a 13.4× UNDER-COUNT — the real figure is 38,152.** `mesh_scan.py`'s
`validate` locates a block's `.gzn_pc` segment by searching for the check value *at a
16-aligned offset*, and a `.gzn_pc` is a contiguous chain of segments in which only the
first is 16-aligned — so the gate admitted one segment per file plus the ones that landed
16-aligned by luck. Changing that single gate from 16-aligned to 4-aligned and leaving
every other test byte-identical takes the count from **2,843 to 38,152 of 39,073 g-backed
candidates**, with the foreign-`.gzn_pc` control at 0.13% and the perturbed-declared-length
controls at 0–0.21% (§10.5).

The 81 pairs
with zero validated blocks are, **verified as an exact set match** (not merely a matching
count), precisely the 81 files this document's §7.1 already identified as carrying a
zero-byte `.gzn_pc`. Restricted to the 1,002 files where the test could actually fail (a
nonzero `.gzn_pc`), the rate is **1,002 / 1,002 — no failure occurred.**

> ⚠ **Cross-reference note, 2026-09-12** (✅ **confirmed correct by Team B on direct inspection** — they read `spec-terrain-format.md` §3 themselves and matched its two "magic values" to the two dominant ids among the 790 the population pass found)**:** `spec-terrain-format.md` §3 characterised `.czn_pc`'s top level differently from §1–§3/§8 above (a single-sample "magic value" reading rather than this section's `{id, length}` chain), never previously cross-checked. Reconciled: they describe the same structure; terrain's sample also found a readable region-label string inside a top-level record's interior that this document's population-wide pass never looked for — recorded there, cross-referenced here, and not pursued further (record-interior content remains the project's one parked question, `HANDOFF.md` §27.3).
>
> **Also flagged by Team B's own independent reader — both hypotheses below were checked against Team B's own code and REFUTED as stated, corrected in place rather than left standing.** Their spot-check found a 93.1% Mesh-block hit rate against this section's 100% (restricted-denominator) figure, and 0% inline-mode blocks against §7.3's ~23%.
>
> ~~**The most likely explanation for the second… `mesh_scan.py`… is structurally blind to \[inline-mode blocks\]… §7.3's ~23% is not in tension with a 0%-inline result from a g-backed-only validator.**~~ **⚠ REFUTED for Team B's validator, though the underlying fact about `mesh_scan.py` itself is still correct.** `mesh_scan.py` genuinely cannot see inline-mode blocks — that part stands. What does not transfer: Team B's own `src/zone_geometry.cpp` **explicitly attempts inline parsing** (branches on the flags byte, tries a direct parse against `.czn_pc` content when the g-file bit is clear), so their validator is not blind the way `mesh_scan.py` is, and their 0% needs its own explanation — either a genuine negative in what they scanned, or their stricter parser rejecting real inline blocks a looser scan would still count. **They are checking it directly rather than accepting the borrowed explanation.**
>
> ~~**The 93.1%-vs-100% gap is plausibly the same family of explanation — a differently-scoped denominator, most likely one that does not exclude the 81 already-known zero-byte-`.gzn_pc` files this section's 100% figure deliberately excludes.**~~ **⚠ REFUTED by Team B's own arithmetic.** Both denominators already exclude zero-byte files: this section's 1,002/1,002 is 1,083 total minus the 81 zero-byte files; Team B's 932/1,001 is 1,037 found minus 36 zero-byte. The denominators are not the difference. **Their better lead:** some of their "non-empty" `.gzn_pc` reads may be partial or corrupted under the same `OkUnconfirmedContent` extraction-reliability status already implicated in the 46-file gap below — they are checking whether the 69 non-validating files correlate with that status.
>
> **✅ All three of Team B's flags now closed on their side (2026-09-12), independently re-verified by them before sending — recorded here rather than left as open questions.**
>
> **The extraction gap was 37, not 46 — and it was Team B's own harness bug, not a shipped-reader issue.** Their `validate_zone.cpp` had no per-entry try/catch, so one bad file's exception silently aborted every remaining sibling in that container — a 9-file overcount from that single defect. Fixed directly, confirmed 47→56 found on dlc1-3 (exactly 93−37, self-consistent). **The real, exact predicate:** `.czn_pc` entries whose paired `.gzn_pc` is `PayloadKind::Raw` with `dataOffset + uncompressedSize` exceeding the containing `.str2_pc`'s total size — 37 files across 10 named DLC containers (`dlc1_awld_compact.str2_pc` ×11, `dlc2_awld_compact.str2_pc` ×8, six `dlc1_mm_0N.str2_pc` singles, and four smaller genki/gis containers). **Their own `OkUnconfirmedContent` correlation lead for the 93.1% gap is refuted**: 0 occurrences anywhere, pass or fail. **The real driver, narrowed but not fully closed:** of 70 failures, 74% have no anchor candidate findable in the `.czn_pc` at all — the open question is now specifically *why* the anchor signature is simply absent for those, a smaller and sharper question than before.
>
> **The inline-mode 0% is confirmed genuine on Team B's side, not their reader being too strict — and this narrows, rather than contradicts, this document's own §7.3 figure once the two validators' actual histories are compared.** Team B ran a full-population raw-signature scan ignoring `.gzn_pc` pairing entirely: 14,920 candidates, a *higher* raw hit rate than this document's ~23% estimate — but **0 of 14,920 survive full validation**, 53.9% failing their own check value immediately and the rest failing on absurdly small declared lengths, the signature of coincidental byte patterns rather than corrupted real blocks. **Checked against this document's own tooling before accepting either figure as the anomaly:** the harness behind §7.3's "bookend holds in 4,477/4,478" (`czn_anchor.py`) defines that bookend specifically as a match against **the paired `.gzn_pc`'s own tag** — meaning §7.3's validation, wherever it actually applied that check, was tied to g-file cross-referencing from the start, not a from-scratch full-replay of inline content the way `mesh_scan.py` validates g-backed blocks in §9.4 or the way Team B's new scan validates candidates here. **No rigorous full-replay validation of the inline-mode population has ever been run on this side.** So the two findings are not necessarily in tension — a weaker or differently-scoped check passing candidates that a full-population, full-validation scan then refutes is exactly the shape this project has hit before (§5) — but confirming that precisely would need reading §7.3's original extraction script's exact test, not just its comment, which this note does not do. ~~**Left honestly as: Team B's 0/14,920 stands on its own merits as a real, well-controlled negative on their side; whether it *also* means this document's original ~23% inline-mode figure needs revising is `OPEN / UNKNOWN`, not resolved by this note.**~~
>
> ✅ **RESOLVED 2026-09-13 — §10.6. It does need revising: the ~23% figure is REFUTED.** The step this note declined to take — reading §7.3's original extraction script's exact test rather than its comment — was taken, and the script (`gzn_meshes.py`) computes the Mesh header at `ap(S+12,8)`, which lands **8 bytes early** for every anchor at `S ≡ 4 (mod 8)`. In **1,055 of 1,055** such anchors the byte it read as "flags" is the low byte of the g-length field, even in **1,048**, so bit 0 reads clear. Corrected figure over the whole 1,083-pair population with `mesh_scan.py`'s dynamic offset: **292 of 39,366 candidates (0.74%) read flags bit 0 clear.** Team B's 0 of 14,920 and this document's figure were never in tension because this document's figure was not measuring inline mode at all. **Note which half was wrong: the note above correctly refused to explain away a disagreement it had not diagnosed, and correctly identified the one unread artefact that would settle it — the cost of leaving it was one day, not a wrong publication.**
>
> Their third flag, a 46-file extraction gap, could not be diagnosed from this side without their own numbers; **2,971 total `.czn_pc` files vs 1,083 with any `.gzn_pc` present at all (§7.1) is a real, already-explained ~1,888-file gap and does not appear to be the source of their separate 46-file figure.** ~~Team B will send the exact predicate once it lands.~~ → received, see the ✅ note above (37 files, not 46).
>
> **Both refuted hypotheses were labelled `HYPOTHESIS — unconfirmed` when first written, specifically so a refutation would be a small correction rather than a retraction of something asserted as fact — that discipline is why fixing this cost one paragraph, not a re-investigation.**
**[CONFIRMED — empirical, controlled by construction, full population,
`czn_mesh_scan_zones.py`/`czn_mesh_scan_zones_perfile.py` — but scoped, 2026-09-13, to the
*coverage* claim only: 1,002 of 1,083 pairs, and the exact set match on the 81-file
shortfall. The block COUNT this section published (2,843) is REFUTED as a 13.4×
under-count; see §10.5. Every block-level statistic anywhere in the project that was
measured on `mesh_scan.py`'s validated population inherits the same wrong denominator —
§10.7 lists them.]**

This is offered as an independent reproduction using a materially different validator (exact
replay + bookend, vs. §7.3's hash-likeness filter) over a complete rather than partial
population — not a restatement — and it closes any residual doubt that §7's finding was an
artifact of the 400-zone sample or the specific anchor script used there.

⚠ **What it did NOT close, added 2026-09-13:** the two validators disagreed on blocks per
zone by roughly 4× (§7.3's ~11 per zone against this section's 2.84) and that discrepancy sat
unreconciled between two published figures in the same document. It was the visible symptom of
§10.5's defect the whole time. **When an independent reproduction of a finding lands on a
materially different COUNT while confirming the finding, the count discrepancy is a result in
its own right and must be reconciled before either number is published** — `HANDOFF.md` §5's "when your
count disagrees with another team's by a factor, publish neither until it reconciles", which
this document violated against itself rather than against a peer.

**[OPEN — desk review 2026-09-30: Team B's pair count was 1,037 found plus a 37-file extraction gap (the note above), 1,074, not reconciled to this section's 1,083; to be settled against real data (D7).]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (1,083 versus Team B's 1,037 + 37 marked OPEN) — desk review (not re-derived from the executable).**

### 9.5 Where this leaves the object/property stream

**REFUTED (as a route to the interior parser):** the per-type resource-construction path.
`.czn_pc`/`.gzn_pc` have a literal null constructor and destructor; the generic dispatcher's
own null-ctor branch is the complete explanation for how their buffers reach memory without
any type-specific code ever running. **[CONFIRMED — disassembly.]**

~~**Well-scoped negative, not yet overturned:** no function reachable from the zone streaming
category registrar, the zone dependent-asset dispatcher, the `.czh_pc` header reader, or the
generic "prepackaged stream" container-validation cluster contains a walker of `.czn_pc`'s
own top-level `{tag, length}` chain, and an exhaustive whole-binary census for the walker's
minimum required instruction-level signature returns only CRT/heap-allocator internals.
**[CONFIRMED — disassembly for each entry point traced; the census is exhaustive, not
sampled, over the whole 41,785-function binary.]**~~

**CORRECTED, 2026-09-30 — the walker exists; the census's own instruction-signature predicate
never covered its real compiled form.** A fresh trace, starting from the render-side question
of what draws zone/building geometry (`spec-vertex-format.md` §12.13 item 5,
`spec-vehicle-geometry.md` §11.9.3), found a real top-level `.czn_pc` walker: `0x00864c60`
and `0x007512f0` (the latter reached via `0x00864000`, which calls it at 0x00864081,
`spec-vehicle-geometry.md`) each walk a buffer: they read the `u32` tag at the current cursor,
compare the tag with its high bit cleared against a fixed id held in one of two tag-constant
globals (0x013026f4 holds `0x2237`, 0x0130279c holds `0x2251` — both real, in-population ids, `0x2237`
being the exact "Region" id `spec-terrain-format.md` §3's own real sample already found
preceding readable region-label strings), and on a mismatch step the cursor past the record by
its header, its length field and an alignment pad — exactly this document's own §2/§8
`{high-bit tag, length}` chain shape. *(Reworded 2026-09-30 by the desk review from
pseudocode-shaped lines into prose.)* **[OPEN — desk review 2026-09-30: the exact step (the base
the cursor is relative to, whether the length excludes the 8-byte header, and the rounding
granularity) is stated only verbally; to be settled against the executable at 0x00864c60 /
0x007512f0 / 0x008652d0 — see the step-alignment CONFLICT below.]** **RESOLVED 2026-10-08, see §1:
base is `tagOffset`, header is 12 bytes not 8, length excludes that 12-byte header, and there is no
rounding step — confirmed independently on `0x00864200`/`0x008642a0`, not yet re-run on
`0x00864c60`/`0x007512f0` specifically but the same 12-byte-header model is what population-wide
1,333/1,333 exact-EOF landings validates.** Both are called, alongside (at least) 6 further
sibling tag-handlers, from one master dispatcher, `0x008652d0`, itself called from
`0x00866580`/`0x00861460`/`0x00861800`/`0x0084e3b0`/`0x007acfe0` — a chain sitting
in the same tight `0x863000`–`0x866000` code module as the already-documented `.czh_pc`
header parser (`0x00863080`) but never checked by name in the table above.
**Why the original census missed it, precisely:** the real compiled walker masks with
`0x80000003` (not the literal `0x80000000` the census required), rounds its alignment step up
to the next multiple of 4 with a branching sequence built on an OR with `0xfffffffc` (not an
`AND`-mask instruction at all), and masks with `0x7fffffff` (not `0xFFFFFFF8`/`0xFFFF0000`) to
strip/test the tag's high bit — none of which match either of the two required literals the
census's predicate searched for. **[CONFLICT — step alignment, desk review 2026-09-30: this
4-byte round-up contradicts §1/§8.1's `align8(8 + length)`; see §1's marker. To be settled
against 0x00864c60 / 0x007512f0 / 0x008652d0 (base, header size, length semantics, 4 versus 8
rounding) and data check D1.]** **RESOLVED 2026-10-08, see §1's note: the "4-byte round-up" this
paragraph describes is itself superseded — real behavior is a fixed 12-byte header with NO
rounding step at all (confirmed on two independent handlers, `0x00864200`/`0x008642a0`); the
original census's `0x80000003`/`0xfffffffc`/`0x7fffffff` mask readings for `0x00864c60` etc. are
unaffected by this correction (header-size, not mask-constant, was the open question).** **Same family as `WALLS.md`'s standing "an instruction-form census undercounts without
a control on every compiler-emitted form" lesson (the D3D9 vtable-call census, the vehicle
`high16` stride census) — a new instance, not a new category.** This corrects the strong
form of the negative (a walker exists); it does **not** by itself confirm the *object/property
interior* specifically is read this way — only 2 of `0x008652d0`'s ~9 tag-handlers have been
opened so far (both geometry-shaped: a shared "Mesh sub-block + material set + index lists +
bounding volume" record for id `0x2237`, and a single mesh+material per-instance record for
id `0x2251`), and the remaining ~6 handlers' own content is uncharacterized — genuinely
possible, not yet shown, that one of them is the interior object/property stream's own real
reader. **[CONFIRMED — disassembly, full chain traced this pass; the "does this reach the
object/property interior" question stays OPEN, narrowed from "no walker exists at all" to
"which of ~6 uncharacterized tag-handlers, if any, is it."]** **Scope note, added after a peer
flagged it 2026-09-30:** everything in this paragraph is derived from disassembling the
game's own executable code (function calls, embedded tag constants) — none of it required
reading any real `.czn_pc` file's own content. ~~The more detailed field-level layout of the
`0x2237` record specifically (beyond the "geometry-shaped" summary above) is flagged
provisional pending a scope question about whether it sits inside the PARKED object/property
interior — see `spec-terrain-format.md` §5 open item 2's own pending-scope note.~~ **RESOLVED
2026-10-07** (the `.czn_pc` interior hold was lifted and this question was checked against real
shipped bytes): the `0x2237` ("WE geom") record's own payload is a `{meshRefCount, materialCount}`
header followed by a byte-length-prefixed `.tga` texture-name pool (not a fixed-slot table — real
counts observed from 1 to 85+ across the population) feeding `materialCount` inline-parsed
materials via the shared MATSET parser — fully specified in `spec-terrain-format.md` §5 item 2,
with the real storage offsets now disassembly-confirmed. It is a separate, sibling mechanism to
this file's own §11 Objects (0x2234) object/property interior, not the same stream — §11's own
format already stands independently confirmed.

**New lead, not yet closed:** the engine's generic hash-keyed named-property mechanism
(§9.3), independently tied to confirmed `.czn_pc` content, is the strongest concrete
candidate yet for how the object/property stream's per-instance values reach a live object —
by name, hashed, through a small per-class ordinal table, not by fixed file offset. This is
consistent with, and gives a mechanism for, every negative this document and its predecessors
have already recorded about `.czn_pc` never containing a literal-checked id. *(Qualified 2026-09-30: the top-level walker in the CORRECTED paragraph above does compare tags against constants held in globals; the hash mechanism remains a lead for the property interior only.)*

**Concrete next step:** find who holds the table that resolves `0x00a33e00`'s (and
siblings') indirect call sites — i.e. the registration mechanism for this
property-class family, analogous to `0x00700780` but for behaviour/property classes rather
than file types. That table's entries, read directly, would show whether any class in this
family is constructed while a `.czn_pc` buffer is the active stream, which is the one fact
needed to close §9.3's open half. A string-hash brute-force of the remaining unidentified
top-level ids (`0x2233`/`0x2234`/`0x2237` and the wider 790) against candidate names was
considered and **not attempted this pass**: the project's one precedent for this exact
technique (`spec-conversation-format.md` §6.1, hashing 3,917 candidate strings against 57
unnamed ids) scored 0 matches with a 0-match control, i.e. unsuccessful rather than
miscalibrated, and there is no stronger candidate name list here than there was there.

**Review status (2026-09-30): NEEDS-EXE: exact step formula and tag compare at 0x00864c60 / 0x007512f0 / 0x008652d0, uses of the globals 0x013026f4 / 0x0130279c; pseudocode-shaped lines reworded — desk review (not re-derived from the executable).**

### 9.6 Methodology note

The task brief's expected shape for this search — a registered type whose constructor is
vtable-dispatched and reachable only by data-side discovery — is exactly right for most of
this project's formats and was exactly wrong for this one: the table slot is a literal `0`,
checkable in two field reads, not a hidden indirection. **Check the registration table's own
ctor/dtor fields for a literal zero before spending any budget on data-side constructor
discovery** — it is the cheapest possible test and it is conclusive either way. Separately:
a compound instruction-idiom census (two specific immediates required in the same function)
is exhaustive and cheap to run over the whole binary and returned a clean, informative
negative here *(⚠ SUPERSEDED 2026-09-30, §9.5: the negative was wrong — the predicate missed the walker's real compiled form)*, in contrast to this project's repeated experience with single-displacement
predicates (HANDOFF §5, four prior failures at 469/461/23/272 hits) — the difference being
that a compound, mechanism-specific predicate over 32-bit immediates is inherently far
narrower than a single small-displacement byte offset, even before any address-range
filtering is applied.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 10. Zone geometry was under-counted 13x: the `.gzn_pc` segment chain, and two header-offset defects (2026-09-13)

**Prepared by:** SPEC TEAM, 2026-09-13, prompted by Team B's report that 21,634 of 24,477
Mesh-block anchor candidates in `.czn_pc` fail to close under any displacement their reader
tried, while 98.3% of them pass an independent self-check saying they are genuinely real
blocks. The task was to find out whether this project's own tooling has the same gap and
whether this document's published zone-geometry figures under-count.

**It does have the same gap, the cause is now identified byte-for-byte, and yes — §9.4's
block count is a 13.4× under-count.** The file count is not affected. Two separate
header/offset defects were found and only one of them was Team B's; the other is in this
project's own §7.3 harness and it refutes §7.3's inline-mode figure, closing the
`OPEN / UNKNOWN` cross-reference question at the end of §9.4.

**Population used throughout this section** (identical to §9.4's, re-derived not inherited):
every `.czn_pc`/`.gzn_pc` filename pair found recursively through nested containers in
`sr3_city_0`, `sr3_city_1`, `sr3_city_missions`, `dlc1`, `dlc2`, `dlc3`, `misc`, `startup`,
`patch_compressed`, `patch_uncompressed` — **1,083 pairs**, of which **81 have a zero-byte
`.gzn_pc`**, matching §7.1/§9.4 exactly. Harnesses: `zg2_zone_cache.py`,
`zg2_anchor_ladder.py`, `zg2_candidate_reality.py`, `zg2_gside_diag.py`, `zg2_dump_cases.py`,
`zg2_tile_gzn.py`, `zg2_tile2.py`, `zg2_align_audit.py`, `zg2_order_and_residual.py`,
`zg2_s73_repro.py` (and `zg2_s73_repro_hashlike.py`, the same harness with §7.3's
`≥ 0x10000` check-value filter, which is the variant that reproduces §7.3's published
numerators), `zg2_clmesh_sweep.py`. Every count attributed to `mesh_scan.py` below was
produced by **importing and calling** `mesh_scan.read_block`/`validate`/`replay_g`, never by a
second walker, so those figures are "by `mesh_scan.py`" in the sense `HANDOFF.md` §5 requires.

### 10.1 `mesh_scan.py` does NOT share Team B's "Gap 2" — confirmed on real files, not on the arithmetic

`mesh_scan.py` computes the Mesh header's position from the anchor `p` (the `u16` version
field) dynamically: step over the version field, round up to 4, read three `u32` fields
(check value, c-side length, g-side length), round up to 8. Written out, that yields

| anchor `p` mod 8 | header at | Team B's name for it |
|---|---|---|
| 0 | `p + 0x10` | their pre-fix assumption |
| 4 | `p + 0x14` | their "Gap 2" correction |
| 2 | `p + 0x0E` | — |
| 6 | `p + 0x12` | — |

**Of the 2,843 Mesh blocks `mesh_scan.py` validates across the 1,083 pairs, 2,596 sit at
`p ≡ 0 (mod 8)` and 247 at `p ≡ 4 (mod 8)`.** Forcing a fixed `p + 0x10` on those 247 — every
other test in `read_block` left byte-identical, the header offset the only variable — makes the
flags byte read **`0x00` in 247 of 247**, i.e. bit 0 clear, i.e. "inline mode". That is Team B's
reported symptom reproduced exactly: a filler zero word occupying the gap, a flags byte read
out of it, and the reader sent down the inline path. **`mesh_scan.py` lands on `p + 0x14` for
all 247 and validates them.**

The dynamic rule is right and *neither* fixed constant is: forcing a fixed `p + 0x14` on the
2,596 blocks at `p ≡ 0` leaves 2,355 passing the plausibility gates but failing the g-side
replay, and 241 reading as inline mode (flags `0x02`). **[CONFIRMED — empirical: 2,843 of
2,843 validated blocks, the alignment split measured rather than assumed, and the wrong-rule
outcome measured on exactly the cases where the two rules differ.]**

**What a failing case would have been:** a validated block at `p ≡ 4 (mod 8)` whose header the
dynamic rule placed at `p + 0x10`. There are none, and 247 blocks exist where the two rules
differ, so the test could have failed.

Also measured, because it bounds the claim: **no anchor at `p ≡ 2` or `p ≡ 6 (mod 8)` validated
(0 of 2,843)**, although 36 and 16 candidates respectively passed the plausibility gates there.
In shipped zone data the Mesh anchor is always 4-aligned; the two odd-parity branches of
`mesh_scan.py`'s arithmetic are never exercised by zones and are therefore **untested**, not
confirmed. **[HIGH CONFIDENCE — inferred, that the anchor is always 4-aligned; the `p + 0x0E`
and `p + 0x12` branches are OPEN / UNKNOWN for want of a single shipped case.]**

*(Desk review 2026-09-30: VALIDATED-BY-DATA for the two 4-aligned branches — Team B, `team-b/HANDOFF.md` lines 6519-6530, over all 24,477 anchors with two independent oracles: 2,596 close at `0x10`, 247 at `0x14`, zero disagree. The `p ≡ 2 / 6` branches remain OPEN: the 0-of-2,843 figure was not re-run on the corrected 38,152 (data check D2). The mod-8 classes are of the file offset, which matches the rule only if the loaded buffer is itself 8-aligned; that is not stated.)*

**Review status (2026-09-30): VALIDATED-BY-DATA: 2,596 at `0x10` / 247 at `0x14`, Team B two-oracle run; `p ≡ 2 / 6` NEEDS-DATA (D2) — desk review (not re-derived from the executable).**

### 10.2 The same gap exists on this side: 39,073 g-backed candidates, 2,843 close

The ladder below is the same file set, one pass, each rung strictly stricter than the one above.
"Positions" means byte offsets in a `.czn_pc`, not files.

| Rung | Predicate | Positions |
|---|---|---|
| L0 | `u16 == 9` at an even offset (the bare version signature) | **83,701** |
| L0′ | …of which 4-aligned | 63,341 |
| L1 | passes `mesh_scan.read_block`'s plausibility gates | **39,366** |
| L2 | L1 and flags bit 0 set (g-backed) | **39,074** |
| L2b | L1 and flags bit 0 clear (inline-mode) | 292 |
| L3 | L2 and `mesh_scan.validate` confirms (exact replay to the declared g-length **and** a bookend copy of the check value at the landing site) | **2,843** |
| — | L2 minus L3 (g-backed, does not close) | **36,231** |

L3 = 2,843 in 1,002 of 1,083 pairs reproduces §9.4 exactly, which is this pass's plumbing
check. One of the 39,074 g-backed candidates lives in a zone whose `.gzn_pc` is zero bytes, so
measurements that need a g-file use a denominator of **39,073**.

**So this project's own instrument closes 2,843 of 39,073 g-backed candidates (7.28%) and
leaves 36,231 (92.72%) unclosed.** Team B's split is 2,843 of 24,477. The candidate predicates
are different and the totals must not be combined — but **their count of closing anchors is
identical to ours, and 24,477 − 21,634 = 2,843 exactly**, which is strong circumstantial
evidence that their closing set *is* §9.4's validated set. That is a count match, not a set
match, and per `HANDOFF.md` §5 it should be confirmed by set comparison from their side before either team
relies on it. **[CONFIRMED — empirical for this side's ladder; HYPOTHESIS — unconfirmed that
the two closing sets are the same set.]**

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

### 10.3 The non-closing candidates are real Mesh blocks — five discriminators, each with a control that fires

Strata: **L3** = the 2,843 `mesh_scan`-validated blocks (known-real, used to calibrate every
test below before it is applied); **L2F** = the 36,231 g-backed candidates that do not close.

| # | Discriminator | L3 | L2F | Control |
|---|---|---|---|---|
| D1 | check value present in the **paired** `.gzn_pc` | 2,843 / 2,843 (at a 16-aligned offset) | 36,191 / 36,231 = 99.89% present somewhere (21,529 at a 16-aligned offset, 14,662 only elsewhere, **40 absent**) | same value in a **different** zone's `.gzn_pc`: L3 1 / 2,843 (0.04%), L2F 77 / 36,231 (0.21%) |
| D2 | primary-buffer element-size byte `== 2` (`spec-geometry-format.md` §4.1.1's published invariant) | 2,843 / 2,843 | **35,316 / 36,231 = 97.47%** | a single byte equalling 2 by chance: 1/256 = 0.39% |
| D3 | closed-form per-channel stride prediction exact (`spec-vertex-format.md` §5's law) | 3,664 / 3,664 channel records | **37,583 / 42,738 = 87.94%** exact, 5,115 miss, only **40** unrecognised semantic codes | an unrecognised-semantic rate of 0.09% is not what garbage bytes produce |
| D4 | candidate lies inside a validated block's own declared c-side span (i.e. is interior bytes of a known block) | — | **40 / 36,231** | — |
| D5 | distinct check values per file vs candidates per file (i.e. are these duplicates of one descriptor?) | — | 39,074 candidates carry 38,236 distinct check values over 1,003 files; median ratio **1.00**, maximum **2.00** | a duplicate population would show a ratio ≫ 1 |

**The decisive one is on the g-side and needs no replay at all.** For each candidate, take the
offset where its check value occurs in the paired `.gzn_pc` and test whether a second copy sits
at `offset + declared_g_len − 4`:

- **L3: 2,843 / 2,843 hold, and the offset where they hold is 16-aligned in 2,843 of 2,843.**
- **L2F: 35,311 / 36,230 hold (97.46%) — and the offset where they hold is 16-aligned in ZERO
  of 35,311** (4 mod 16 in 21,853; 12 mod 16 in 11,670; 8 mod 16 in 1,788).
- From that offset, `mesh_scan.py`'s own `replay_g` consumes **exactly** the declared g-length
  in **35,308 of 35,311** (residuals: one at −4, two at +64).

Controls, all able to fire: the same tag-and-length pair against a different zone's `.gzn_pc` —
tag absent in 35,737 of 36,230, found in 493, bookend held in **3**. The declared length
perturbed — **+16: 0 / 37,206; −16: 0 / 38,152; −4: 1 / 38,152 (0.003%); +4: 80 / 37,206
(0.215%)**. The same length at a random 16-aligned offset in the same `.gzn_pc`: 1,275 / 114,462
(1.11%).

**[OPEN — desk review 2026-09-30: these controls do not agree with the D1 row above or with §10.5 — D1 gives the check value present in a different zone's `.gzn_pc` for 77 / 36,231, yet the tag here is "found in 493" of 36,230; the perturbation figures here (−4: 1 / 38,152; +4: 80 / 37,206) differ from §10.5's (−4: 0; +4: 81 / 39,073), and the denominators 37,206 / 38,152 / 39,073 are not explained; to be settled against real data by one re-run on a single denominator (D8).]**

**[CONFIRMED — empirical, controlled: the 36,231 non-closing candidates are real Mesh
sub-block headers, each naming a distinct segment that genuinely exists in the paired
`.gzn_pc`, at a byte offset the block's own declared length independently confirms.]**

⚠ **One number to correct in the framing this investigation started from, and it matters for
Team B.** The obvious "independent ~2⁻³² self-check" available on the c-side alone — a
hash-like check value repeating at `anchor + declared_c_len − 4` — passes on **38,152 of the
38,307 hash-like L1 candidates (99.60%)**, which looks like overwhelming proof on its own.
*(That 38,152 is numerically identical to §10.5's corrected block count and the two are
unrelated measurements over different predicates and different denominators — recorded so a
later reader does not mistake the coincidence for a relation.)* It
is not a 2⁻³² test on this data: the identical predicate applied at **random 4-aligned offsets
with no version signature required at all** fires in **250 of 3,584 trials that could fire
(6.98%)**. A ~7% background rate still leaves a 14× separation, so the check is informative —
but it is nowhere near the per-trial rate its arithmetic suggests, because `.czn_pc` content is
full of hash-and-length-shaped records. **The load-bearing evidence above is the g-side
bookend plus the exact replay, whose controls sit at 0.003–0.215%.** If Team B's 98.3% came
from a c-side-only check, the conclusion they drew from it is right but the stated
false-positive rate is not, and this is worth them re-measuring. **[CONFIRMED — empirical, for
the 6.98% chance rate, `zg2_candidate_reality.py` test D7, 200 random 4-aligned trials per
`.czn_pc` over 1,083 files; the applicability filter — hash-like value, usable length — is the
same one the real test uses, which is why only 3,584 of ~200,000 trials could fire.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (control-figure disagreements marked OPEN, D8) — desk review (not re-derived from the executable).**

### 10.4 The cause: a `.gzn_pc` is a contiguous CHAIN of segments, and only the first one is 16-aligned

`mesh_scan.py`'s `validate` locates a block's g-side segment by searching the `.gzn_pc` for the
block's check value **and requiring that offset to be 16-aligned**. Byte-level inspection of one
zone (`sr3_city~s0715`, 201,928-byte `.czn_pc`, 557,224-byte `.gzn_pc`, 12 g-backed candidates,
2 of them validated) shows why that gate discards most of the population:

| c-file anchor | check value | declared g-length | g-side offset | offset mod 16 | replay |
|---|---|---|---|---|---|
| `0x1E60` | `0x35EBB829` | 612 | 0 | 0 | exact |
| `0x1F48` | `0x498D4803` | 1,280 | 612 | 4 | exact |
| `0x203C` | `0xC1FA4A67` | 720 | 1,892 | 4 | exact |
| `0x2150` | `0x8E076BA2` | 1,176 | 2,612 | 4 | exact |
| `0x224C` | `0xD9B7446D` | 234,088 | 3,788 | 12 | exact |
| `0x2338` | `0xD2FB9F14` | 60,832 | 237,876 | 4 | exact |

Each segment is shaped `[u32 tag][payload][u32 copy of the tag]`, **each one begins exactly
where the previous one ended**, and the c-side blocks appear in the `.czn_pc` in the same order.
**Only segment 0 is 16-aligned; the rest are 4-aligned and land wherever the running total puts
them.** The gate therefore admits the first segment of each `.gzn_pc` plus any later segment that
happens to fall on a 16-byte boundary — which is precisely 2,843 blocks over 1,002 files, a mean
of 2.84.

**Why this is a wrong guard rather than a strict one, argued from the format's own declared
value:** the channel walk aligns to the **absolute** 16-byte grid, not to the segment start, so
the number of pad bytes a segment contains depends on where that segment begins. The declared
g-length is therefore only reproducible from the true start offset. For the six blocks above,
replaying from the nearest 16-aligned position instead gives 1,284 vs 1,280, 724 vs 720, 1,172
vs 1,176, 234,100 vs 234,088 and 60,836 vs 60,832. **The declared length encodes the segment's
own alignment residue, so `mesh_scan.py`'s 16-alignment gate and `mesh_scan.py`'s exact-length
oracle directly contradict each other on 92.7% of zone blocks — and the oracle is the value the
format publishes.** **[CONFIRMED — empirical, byte-level, with the contradiction demonstrated
numerically on both readings of the same six blocks.]**

**[OPEN — desk review 2026-09-30: the offsets tile (0 + 612 = 612, + 1,280 = 1,892, + 720 = 2,612, + 1,176 = 3,788, + 234,088 = 237,876) and the residues mod 16 are right, but the replay-from-16-aligned figures are inconsistent — rows at residue 4 give +4 (1,284, 724, 60,836) except the `0x2150` row (1,172, −4); "nearest 16-aligned position" does not say round up or down; and the table shows six of the zone's 12 candidates with only one of its two validated blocks visible (segment 0); to be settled against real data (D9).]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (replay-figure inconsistency marked OPEN, D9) — desk review (not re-derived from the executable).**

### 10.5 The corrected count: 2,843 → 38,152 blocks, a 13.4× under-count

Changing **only** the g-side search alignment in `mesh_scan.validate` and leaving every other
test byte-identical (exact replay to the declared g-length, bookend at the landing site):

| g-side search offset must be | Validated g-backed Mesh blocks, 1,083 pairs | of 39,073 |
|---|---|---|
| 16-aligned (as shipped, §9.4) | **2,843** | 7.28% |
| 8-aligned | 4,630 | 11.85% |
| **4-aligned** | **38,152** | **97.64%** |
| 2-aligned | 38,152 | 97.64% |
| 1-aligned (unrestricted) | 38,152 | 97.64% |

Loosening below 4 adds nothing, which independently confirms 4 as the real grid rather than
merely a looser threshold. Controls on the 4-aligned figure, each able to fire: the identical
relaxed validator against a **different** zone's `.gzn_pc` — **49 of 39,073 (0.13%)**; with the
declared g-length perturbed by **−4, +16, −16 — 0 each**; by **+4 — 81 (0.21%)**.

**The file-level figure does not move: 1,002 of 1,083 pairs contain at least one validated block
at either alignment**, so §9.4's "1,002 of 1,083 (92.5%)" and its exact-set-match explanation of
the 81-file shortfall both stand unchanged. **What was wrong is the block count, not the
coverage claim.**

**`spec-geometry-format.md` §4.1.1's published invariant re-measured over the corrected
population:** the primary-buffer element-size byte reads **2 in 38,152 of 38,152** zone blocks
(the histogram contains no other value), against a 1/256 chance rate per block. Channels per
block over the corrected population: 1 in 35,649; 2 in 2,113; 3 in 272; 4 in 93; 5 in 18; 6 in 7.
**A published invariant holding exactly over a 13.4× larger population is the strongest single
confirmation in this section that the added blocks are real.** **[CONFIRMED — empirical,
controlled, whole population.]**

**A second, independent confirmation that uses no search at all.** Walk a `.gzn_pc` from byte 0:
read the tag at the cursor, look up the c-side block that declares it, require a bookend copy at
the declared length, advance by that length, and require the walk to land **exactly** on EOF.

- **Segments packed back to back ("tight"): 831 of 1,002 pairs with a nonzero `.gzn_pc`.**
- **Allowing a skip to the next 16-byte boundary when the tag at the cursor does not resolve:
  928 of 1,002 (92.6%), 34,799 segments walked.** 97 of those 928 files actually use a skip, and
  the ones that do are `sr3_city~fNNNN~al` files — the "always loaded" zone variants pad between
  segments where ordinary zones do not.
- Per block: **37,446 of 37,730 blocks in multi-block files begin exactly where the previous
  segment ended.**
- Order: among the 580 pairs with ≥ 2 locatable blocks, the g-side offsets are monotonically
  increasing in c-file order in **510 (87.9%)**; shuffling the same offsets gives **109 / 580
  (18.8%)**.
- Controls: perturbing every advance by +4 breaks the walk in **1,002 of 1,002**; walking the
  same tag→length map against a **different** zone's `.gzn_pc` breaks it in **1,002 of 1,002**.

**[CONFLICT — tiling figures, desk review 2026-09-30: Team B's full-population chain check tiles 1,002 / 1,002 (successors 37,035 contiguous / 115 padded / 0 other; its +4 control leaves 21 / 1,002 tiling), `team-b/HANDOFF.md` lines 6622-6627. The 831 / 928 / 97, 37,446 / 284 and 510 / 580 figures above are SUPERSEDED as population facts by that figure — see the full note in §10.8.]**

**[CONFIRMED — empirical, controlled: the `.gzn_pc` payload is a chain of self-delimiting
Mesh segments beginning at byte 0, each `[tag][payload][tag]`, packed contiguously (ordinary
zones) or padded to the next 16-byte boundary between segments (`~al` zones).]**

*(Desk review 2026-09-30: VALIDATED-BY-DATA — Team B's full-population run reproduces the 38,152-block count exactly and 1,002 files with at least one block, `team-b/STATE.md` line 32 and `team-b/HANDOFF.md` lines 6616-6622.)*

**Review status (2026-09-30): VALIDATED-BY-DATA: 38,152 blocks and 1,002 / 1,002 files (Team B); tiling sub-figures superseded, §10.8 — desk review (not re-derived from the executable).**

#### 10.5.1 What a reimplementation should do

Do **not** locate a block's g-side segment by searching for its check value. Two correct routes,
both search-free:

1. **From the c-side**, in c-file order, keep a running cursor starting at 0; block *k*'s segment
   begins at the cursor and the cursor advances by that block's declared g-length (for a `~al`
   zone, rounded up to the next 16-byte boundary). Correct for 37,446 of 37,730 blocks in
   multi-block files. *(⚠ Unlabelled figure, superseded by Team B's full-population count: with the running cursor, else the cursor rounded up to 16, in c-file order, skipping candidates that fit neither, every one of the 37,150 successors is placed — 37,035 contiguous + 115 padded + 0 other, `team-b/HANDOFF.md` lines 6625-6627; see §10.8.)*
2. **From the g-side**, walk the chain as above and resolve each tag to its c-side block.

If a search is used anyway, the offset must be **4-aligned, not 16-aligned**, and the exact
replay plus bookend must still gate it — that combination is what the 0.13% foreign-file control
and the 0-of-38,152 perturbed-length controls above measure.

**[OPEN — desk review 2026-09-30: whether the 16-byte pad applies after every segment of a `~al` zone or only when the tag at the cursor does not resolve (97 files "use a skip" in §10.5; Team B counts 115 padded blocks), and whether any non-`~al` zone pads, is not stated, nor what happens to a candidate that fits neither position; to be settled against the executable (whatever advances the g-cursor alongside the Mesh sub-parser reached via 0x00e718b0) and real data (D10, D11).]**

**Review status (2026-09-30): NEEDS-EXE: the `~al` padding decision and failure behaviour; data checks D10, D11 — desk review (not re-derived from the executable).**

### 10.6 §7.3's ~23% inline-mode population is REFUTED — a second header-offset defect, this one ours

This closes the `OPEN / UNKNOWN` question recorded at the end of §9.4 ("whether Team B's
0 / 14,920 also means this document's original ~23% inline-mode figure needs revising").
**It does. The figure is an artefact.**

§7.3's harness is `gzn_meshes.py`. Its header offset is `ap(S + 12, 8)` — round `S + 12` up to a
multiple of 8 — which equals `S + 0x10` when `S ≡ 0 (mod 8)` but **`S + 0x0C` when
`S ≡ 4 (mod 8)`**: 8 bytes *before* the real header, landing on the g-length field. (Note this is
a different defect from Team B's Gap 2 and displaces in the opposite direction; `gzn_meshes.py`
also models only two `u32` fields after the version word where there are three.)

Reproduction over the first 400 pairs with the `≥ 0x10000` check-value filter §7.3's own prose
describes, split by `S mod 8`:

| | anchors | c-side bookend holds | flags byte `== 0x01` | element-size byte `== 2` |
|---|---|---|---|---|
| `S ≡ 0 (mod 8)` | 3,525 | 3,448 | 3,448 | 3,448 |
| `S ≡ 4 (mod 8)` | 1,055 | 1,029 | **7** | **0** |
| total | 4,580 | **4,477** | **3,448** | **3,448** |

The three numerators reproduce §7.3's published figures exactly — bookend **4,477** (§7.3:
4,477), flags `0x01` **3,448** (§7.3: "bit 0 set 3,448"), element size 2 **3,448** (§7.3: 3,448).
§7.3's published *denominator* of 4,478 is not re-derivable: this reproduction finds 4,580
anchors and 4,522 whose declared length fits the file, so that denominator was some further
filtered count this pass cannot reconstruct. **Recorded as a discrepancy rather than papered
over; it does not affect the mechanism, which is entirely in the split.**

**The mechanism, measured rather than inferred: in 1,055 of 1,055 `S ≡ 4` anchors the byte
`gzn_meshes.py` read as "flags" IS the low byte of the g-length field, and that field is even
in 1,048 of 1,055 — so bit 0 reads clear and the block is reported as inline-mode.** §7.3
published roughly 1,030 inline-mode blocks (4,478 − 3,448); the artefact population is 1,048.

§7.3's own text contained the tell: *"Stride byte: 2 in 3,448 (**matching the g-backed count
exactly**)"*. That exact agreement was read as corroboration. It is the opposite — both reads
come from the same header pointer, so both fail together on exactly the `S ≡ 4` anchors, and two
statistics agreeing perfectly because they share a defect is indistinguishable from two
statistics agreeing because the format is regular. (Same shape as §8.2(a)'s tautological
bracket count, where the giveaway was also two identical numbers.)

**Corrected figure.** Under `mesh_scan.py`'s dynamic header offset, **292 of the 39,366
candidates passing its plausibility gates (0.74%) read flags bit 0 clear**, across the whole
1,083-pair population — not 23%. `mesh_scan.py` does not attempt to validate inline blocks, so
this pass says nothing about whether those 292 are real; what it establishes is that the
inline-mode *population* is sub-1%. **This removes the tension with Team B's 0 of 14,920 rather
than explaining it away: ~~there is essentially no inline-mode zone geometry to find.~~** *(Narrowed 2026-09-30 by the desk review: no inline-mode block has been validated by any full replay; the 292 remain unvalidated, data check D12.)*
**[REFUTED — §7.3's ~23% inline-mode figure and its non-2 element-size population, both
mechanically explained as one header-offset defect; CONFIRMED — empirical for the replacement
figure of 292 / 39,366.]**

⚠ **Corrections to §7.3 proper, which is left standing above with this pointer rather than
rewritten in place:** its block count (4,478 over 400 zones), its bookend rate (4,477 / 4,478)
and its element-size profile's leading value are sound. **Its flags/inline split and the "12, 4
and 16" tail of its element-size histogram are refuted** — those are the `S ≡ 4` anchors read 8
bytes early. Its confidence label of `CONFIRMED — empirical` must be read as applying only to
the surviving claims.

**And the tidy unifying hypothesis is REFUTED.**

The natural and tidy reading — that the non-closing anchors are concentrated among candidates
misread as inline-mode under a naive offset, so one header-offset bug explains both puzzles —
does not survive measurement. **The 36,231 non-closing g-backed candidates split 28,997 at
`p ≡ 0 (mod 8)` and 7,213 at `p ≡ 4 (mod 8)` *(28,997 + 7,213 = 36,210; the remaining 21 are not assigned in the text — desk review 2026-09-30)*: 80.0% sit in the alignment class where no
header-offset rule was ever in dispute.** The header-offset defects explain the inline-mode
figure and nothing else; the 13.4× under-count is entirely the g-side segment locator.
**[REFUTED — empirical, controlled by the alignment split of the non-closing population
itself.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (overreach narrowed; 21-candidate remainder noted) — desk review (not re-derived from the executable).**

### 10.7 Scale, and the same instrument's effect on other carriers

`mesh_scan.py` is the project's **shared** validator for all six carrier pairs, so per `HANDOFF.md` §5 its
defect is correlated rather than distributed and the whole list of figures resting on it is
suspect at once. Re-running the identical one-line change per carrier:

| Carrier | Pairs (this pass) | g-backed candidates | 16-aligned | 4-aligned | ratio |
|---|---|---|---|---|---|
| `.ccmesh_pc` / `.gcmesh_pc` | 1,617 | 1,629 | 1,617 | 1,617 | **1.00×** |
| `.csmesh_pc` / `.gsmesh_pc` | 8 | 8 | 8 | 8 | **1.00×** |
| `.ccar_pc` / `.gcar_pc` | 367 | 1,038 | 367 | 367 | **1.00×** |
| `.csrt_pc` / `.gsrt_pc` | 11 | 25 | 15 | **25** | **1.67×** |
| `.clmesh_pc` / `.glmesh_pc` | 14,690 | 30,266 | 16,324 | **29,908** | **1.83×** |
| `.czn_pc` / `.gzn_pc` | 1,083 | 39,073 | 2,843 | **38,152** | **13.42×** |

**Denominators, stated because they differ from `spec-vertex-format.md` §4.2's:** the tree row is
population-matched — 11 pairs and 15 validated blocks are exactly what §4.2 publishes, so
**trees are a 15 → 25 correction on the identical population**. The `.ccmesh_pc`, `.ccar_pc` and
`.clmesh_pc` rows are **not** matched: §4.2 swept 38 archives and de-duplicated stems, while this
pass used a narrower archive list for the first two and paired within each archive for
`.clmesh_pc` (which is why 14,690 exceeds §4.2's 11,371 — the same stem ships in several
archives). **Those rows establish direction and magnitude, not a replacement number.**

Consequences, stated as work to do rather than as new figures:

1. **`spec-vertex-format.md` §4.2's total of "17,934 validated Mesh blocks / 26,601 channel
   records" is an under-count and needs re-deriving on its own population.** Every per-channel
   figure computed against that denominator — including §12.1's `26,601 / 26,601` all-zero
   results — is measured on a subset selected by the alignment gate. Those particular negatives
   have an independent disassembly route (`spec-vertex-format.md` §12.1 / `HANDOFF.md` §30, archived 27.1 table:
   neither channel-array walker reads `+0x08` at all), so per `HANDOFF.md` §5
   they are the kind that survives their instrument being discredited; but the *denominator* in
   each table is wrong and should be restated.
2. **`spec-geometry-format.md` §4.1.1's "2 in 17,934 of 17,934" carries the same wrong
   denominator.** The claim itself is reinforced, not weakened: over the corrected zone
   population alone it is 38,152 / 38,152.
3. **`.ccar_pc` has a separate, unexplained residual this pass did not chase**: 1,038 g-backed
   candidates over 367 pairs, of which 367 validate at **either** alignment. Vehicles are not
   affected by the alignment gate, so the other ~671 candidates need their own explanation.
   Flagged, not diagnosed. **[OPEN / UNKNOWN.]**

**Review status (2026-09-30): DESK-PASS (out of zone scope for the other carriers; Team B quotes `.clmesh_pc` 12,628 → 29,908 at `team-b/HANDOFF.md` line 6566 against 16,324 → 29,908 here, unexplained) — desk review (not re-derived from the executable).**

### 10.8 Residuals, honestly

- **921 of 39,073 g-backed zone candidates (2.36%) close under no alignment.** Their profile
  matches non-blocks rather than unwalked blocks: the element-size byte reads 2 in 38,152 of
  38,152 *validating* blocks and in 35,316 of 36,231 L2F candidates, so the ~915-candidate
  element-size residual and the 921-candidate validation residual are nearly the same size.
  Not proven to be the same set. **[OPEN — a set comparison would settle it and was not run.]**
- **74 of 1,002 pairs tile under no packing rule.** They break late — e.g. `sr3_city~s1213`
  at cursor 1,880,316 of 1,883,284 after 256 segments — with "no bookend at the declared
  length" or "declared length overruns". A tail-end phenomenon, not a structural refutation.
  **[OPEN / UNKNOWN.]**
- **284 of 37,730 blocks in multi-block files do not start where the previous segment ended**,
  and 70 of 580 multi-block files have non-monotone segment order. **[OPEN / UNKNOWN.]**

**[CONFLICT with Team B, resolved against this text by a full-population figure — desk review 2026-09-30:** Team B's chain check (`team-b/tools/validation/validate_zone_chain.cpp`, header comment lines 10-18; `team-b/HANDOFF.md` lines 6622-6627) reports over the same 10 archives that **1,002 / 1,002** files tile their `.gzn_pc` to EOF exactly (0 short, 0 past) with the located blocks taken in c-file order, 38,152 blocks, and over the 37,150 successors in the 580 multi-block files (37,730 − 580) a split of **37,035 contiguous / 115 at the next 16-byte boundary / 0 other**, with a firing +4 control (21 / 1,002 still tile). Its placement rule is: segment at the running cursor, else at the cursor rounded up to 16, candidates that fit neither skipped without moving the cursor (`team-b/src/zone_geometry.cpp` lines 165-201; a 16-aligned search is used only before the first segment is placed). A demonstrated tiling of every file plainly refutes "74 files tile under no packing rule", the 284 misplaced blocks and the 70 non-monotone files; those figures, and §10.5's 831 / 928 / 97, 37,446 / 284 and 510 / 580, are **SUPERSEDED as population facts** and stand only as what this pass's own g-side walker measured. (HANDOFF line 6390's "matching §10.5 exactly" is itself inaccurate: 37,035 / 115 / 0 does not match 37,446 / 284, and the +4 controls differ, 21 / 1,002 tiling there versus 0 / 1,002 here.) **Still OPEN:** why this pass's walker failed on 74 files — run both walkers on the same pairs and diff the failing sets (review data check D10).**]**

- **40 of 36,231 L2F candidates have a check value absent from the paired `.gzn_pc` entirely**
  and 40 lie inside a validated block's declared span; these are the clearest non-block
  candidates in the population.
- **The `p ≡ 2` and `p ≡ 6 (mod 8)` branches of `mesh_scan.py`'s header arithmetic are
  unexercised by any shipped zone block** (0 of 2,843 validated). Untested, not confirmed.

**Review status (2026-09-30): NEEDS-DATA: 74 / 284 / 70 residuals superseded by Team B's 1,002 / 1,002 tile, cause of this walker's failures open (D10); 921 versus ~915 set comparison (D13); odd parity (D2) — desk review (not re-derived from the executable).**

### 10.9 What to send Team B

The actionable content, in one paragraph, since their 21,634 is the same phenomenon:
**a `.gzn_pc` is not a bag of segments to be found by searching for a tag — it is a chain of
self-delimiting segments starting at byte 0, each `[u32 tag][payload][u32 tag]`, and only the
first one is 16-aligned.** Segment *k* starts at the sum of the preceding blocks' declared
g-lengths (rounded up to 16 between segments in `sr3_city~fNNNN~al` zones only). If their reader
locates a segment by searching for the tag at a 16-aligned offset, that alone produces a gap
of exactly this shape and magnitude — on our side 7.28% closing against their 11.6% — because
the alignment residue is baked into the declared length and the replay can only land on it from
the true offset. **Their predicate has not been seen from this side, so that is a diagnosis to
test, not a conclusion about their code.** Three further items worth their time: (a) their "98.3%
pass a ~2⁻³² self-check" is very likely a c-side-only check whose measured chance rate on this
content is **6.98%**, not 2⁻³² — the conclusion holds, the stated rate does not; (b) their
closing count of 2,843 is numerically identical to ours and the two sets should be compared
directly rather than count-matched; (c) their Gap 2 fix should be the **dynamic** padding rule,
not a fixed `+0x14` — a fixed `+0x14` breaks 2,596 of the 2,843 blocks that a fixed `+0x10`
gets right.

#### Methodology notes for `HANDOFF.md` §5

- **A search predicate and an exact-replay oracle in the same validator can contradict each
  other, and the search will win silently.** `mesh_scan.py` carried both a 16-alignment gate on
  *where* to look and an exact-declared-length test on *what it found*. On 92.7% of zone blocks
  the gate rejected the only position at which the oracle can pass — and the output looked like
  a clean 2,843 / 2,843, because everything the gate admitted did pass. **When a validator both
  searches for a location and checks an exact property of it, the search is an untested
  hypothesis wearing the credibility of the check.** **The tell sat in this document for three days:** the
  block-per-file mean was 2.84 (§9.4, 2026-09-12) while §7.3's anchor scan (2026-09-10) found
  ~11 per zone — a 4× discrepancy between two of this document's own published numbers, never
  reconciled. Sibling of the harness-cap lesson (`HANDOFF.md` §26.10), but worse in one specific
  way: a cap is visibly a parameter, while an alignment constant reads as a fact about the
  format.
- **When two statistics agree *exactly*, ask whether they are computed off the same pointer
  before reading it as corroboration.** §7.3 wrote "stride byte 2 in 3,448 (matching the
  g-backed count exactly)" — both reads come from the same header base, so both fail together on
  the same anchors. This is the byte-identical-control lesson (`HANDOFF.md` §26.16)
  and the tautological-bracket lesson (§8.2a of this document) in a third costume: **exact agreement between two numbers
  is evidence of a shared dependency at least as often as of a real regularity.**
- **A per-trial false-positive rate computed from field widths is a claim about the data, not
  about arithmetic — measure it.** A 32-bit value repeating at a self-declared distance "is
  ~2⁻³²" only if the content is random. On `.czn_pc` it fires at 6.98% of applicable random
  positions, six orders of magnitude off, because the content is full of hash-and-length-shaped
  records. Both teams quoted such a rate this week. **Run the predicate at random positions with
  the same applicability filter before quoting its arithmetic rate** — and note the filter
  matters enormously here: 3,584 of ~200,000 trials could fire at all.
- **The same bug class appeared twice in one day in two independently written harnesses on two
  different teams, in opposite directions** (Team B: header 4 bytes too early at one alignment
  parity; `gzn_meshes.py`: 8 bytes too early at the other). Both were "a constant where the
  format has a running alignment computation". **Prefer writing the alignment arithmetic out and
  testing both parity classes to writing the constant you observed** — and when a spec states an
  offset, state it as the computation, per `HANDOFF.md` §5's existing rule, because this is exactly the defect
  that rule exists to prevent and it recurred anyway.
- **An instrument defect that under-counts by 13× leaves a self-consistent survivor set.** All
  2,843 survivors were real blocks, all passed every invariant, and every statistic computed on
  them was internally coherent — the same signature as the 64-group cap that discarded 286 of 372
  vehicles (`HANDOFF.md` §26.10). **The more selective the wrong filter, the healthier the surviving evidence
  looks.** Here the corrected population *strengthened* every claim measured on the old one
  (element-size byte 2 in 38,152 / 38,152), which is the lucky outcome, not the guaranteed one.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 11. The Objects (0x2234) section: the real placement/property-stream format, and the section-kind spine formalized (units U0+U1, 2026-10-07, user-authorized `.czn_pc` interior work)

**Date:** 2026-10-07. **Scope:** formalizes and extends `review/scoping-czn-interior.md`'s own
proposal for units U0 (the section-kind spine) and U1 (object placement & property stream).
**Method:** direct static disassembly only, `tools/harnesses/exe_va.py` + Python `capstone`
(no Ghidra this pass), against the shipped `SaintsRowTheThird.exe`. Every function cited below
was independently disassembled this pass (not merely read from another note's prose), and every
non-trivial claim carried over from `review/exe-notes-2026-10-01/interp_named_object_resolution.md`
(a prior, same-project investigation that reached this exact code from the opposite direction —
the "who names `Killbane`" question — and had already read `0x00457c60`'s full 330-instruction
body, `0x0045bc20`, `0x00456760`, `0x00456660`, `0x00dab2b0`, and the registry's full
vtable census) was re-verified against the executable's own bytes in this pass, not taken on
trust. Where this pass's own disassembly disagreed with that prior note's wording, this document
says so explicitly and gives the corrected reading; in every case checked, the prior note's own
*mechanism* was right and the apparent disagreement came from this pass initially mis-assigning a
register, not from an error in that note.

**Confidence key** (project standard): **CONFIRMED — disassembly** (control flow/operands read
directly this pass), **CONFIRMED — empirical** (shipped bytes/strings read directly),
**HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

---

**Unit U0 — the section-kind spine, formalized and extended.**

### 11.1 The call chain, read end to end, with exact function boundaries

`spec-world-streaming.md` §10.2 already names `0x00866580` as the zone-instantiate-on-load
callback. This pass disassembled it, and every function below it in the chain, **to their actual
`ret`/int3-padding boundaries** (not partial reads) — addresses below are the real,
disassembly-confirmed extents, not heuristic size estimates:

```
0x00866580  "zone-instantiate on load"        body 0x00866580-0x008667f3  (two ret points; the
                                               success path alone is ~290 instructions)
   -> 0x008660e0  (unconditional, call at file-confirmed site 0x00866661)
                                               body 0x008660e0-0x00866531, ret at 0x00866531,
                                               followed immediately by two inline jump tables
                                               (0x00866532-0x00866573) then int3 padding that
                                               runs right up to the next function's own entry —
                                               i.e. the *entire* function was read this pass,
                                               not the ~250-byte partial read the scoping note
                                               flagged as its own open item. [CONFIRMED]
        -> 0x008652d0  (call at 0x0086630d)   body 0x008652d0-0x008653ae, ret, int3 padding.
                                               Exactly 9 calls in strict sequence, each gated
                                               "on failure, bail to the shared false-return at
                                               0x00865300": 0x00864c60, 0x00863f40, 0x00864000,
                                               0x00864150, 0x008642a0, 0x00864200, 0x00864330,
                                               0x008646f0, 0x008648c0 (the last with its own
                                               argument adjusted by +0x110). These are exactly
                                               the 9 ids already in `spec-zone-data-format.md`
                                               §9.5 / the scoping note's §2 table (0x2237,
                                               0x2240, 0x2251→0x7512f0, 0x2246, 0x2235, 0x2244,
                                               0x2245, 0x2248, 0x2253). [CONFIRMED]
        -> 0x00865f90  (call at 0x008663b3)   body 0x00865f90-0x008660d5, ret, int3 padding,
                                               then its caller's own entry point, 0x008660e0,
                                               begins immediately after — confirming both
                                               functions' boundaries from opposite ends.
                                               Exactly 11 calls in strict sequence, same
                                               fail-fast shape: 0x00863df0, 0x00865ba0,
                                               0x00863c80, **0x00863e90**, 0x00864330,
                                               0x008644b0, 0x00864770, 0x00864840, 0x008640a0,
                                               0x008648c0 (again, this time with its argument
                                               adjusted by +0x10c), 0x008643f0. These are
                                               exactly the scoping note's claimed 11 ids
                                               (0x2242, 0x2233, {0x2236/0x2238/0x2241}, **0x2234
                                               = Objects**, 0x2245, 0x2247, 0x2249, 0x2250,
                                               0x2252, 0x2253, 0x2254). [CONFIRMED]
```

**Correction to the scoping note's wording:** §3 of the scoping note describes the two
dispatcher calls as "unconditional in sequence." Read end to end, that is true of the *call
instructions themselves* (`0x0086630d`/`0x008663b3` always execute if control reaches that far),
but each call is **individually gated on a buffer-presence test earlier in `0x008660e0`**
(`test edi,edi; je <skip>` on the resolved "Zone fast"/"Zone header fast" buffer pointer for the
first call, and a second, separate presence test before the second). If a real zone's container
omits one of those two buffers, `0x008660e0` skips straight past the corresponding dispatcher
call without ever reaching it. For a normal shipped zone (which always carries both buffers) the
practical effect is indistinguishable from "always runs," which is almost certainly why the
original note read it that way — but the two are not the same claim, and only the weaker one is
disassembly-true. **[CONFIRMED — disassembly; whether a real shipped zone ever omits one of the
two buffers is OPEN, not checked against file content this pass.]**

**The `{tag,length}` top-level walker and record-header format, confirmed independently and
sharpened.** `0x00863430` (body `0x00863430`-`0x008634d1`, ret, int3 padding; this is the "shared
tag-find helper" the scoping note names, re-read in full this pass) is exactly the walker
`spec-zone-data-format.md` §9.5 already describes: `AND 0x80000003`, a branching
`(x−1 | 0xfffffffc)+1` round-up, `AND 0x7fffffff` to strip the tag's high bit. This pass adds one
new fact §9.5 did not have: ~~**the top-level record header is 8 bytes in the common case, widened
to 12 bytes when a specific bit in the tag word's top 3 bits is set**~~ **CORRECTED 2026-10-07
(unit U2, real-data population replay): the mask arithmetic touches exactly ONE bit — the tag's
own bit 31 — not "the top 3 bits"; the `shr edx,0x1d`/`and eax,4` pair discards the other two bits
it extracts.** `header_size = 8 | ((tag>>29)&4)` → 12 bytes iff bit 31 is set, 8 bytes otherwise;
when 12, a third dword at `tag+8` is read out as an extra per-record field (`extra`). **And since
every genuine top-level section id in this engine ships as `0x8000xxxx` (bit 31 set by
construction, `spec-zone-data-format.md` §1), the 8-byte branch is never exercised by any real
on-disk record: a population-wide walk of all 19,725 top-level records across every one of the
2,971 shipped `.czn_pc` files found `header_size == 12` in 19,725/19,725 (100%), zero exceptions.
The real, on-disk top-level record format is uniformly 12 bytes: `{u32 tag, u32 length, u32
extra}`.** The 8-byte branch is presumably still live code for some other, non-`.czn_pc` caller
of this shared walker — nothing here rules it in or out there — it is simply dead for every real
top-level record this project has ever found. `extra` is `0` for every real `0x2234` (Objects)
occurrence (861/861); of the engine's 22 known content-kind ids, only four (`0x2237`, `0x2240`,
`0x2251`, `0x2252`) ever carry a nonzero `extra` in real shipped data — its meaning for those
kinds is OPEN, out of scope for the Objects-section work this file covers. **[CONFIRMED —
disassembly, `0x00863430` read in full independently by two units; CONFIRMED — empirical,
full 2,971-file/19,725-record population, unit U2.]**

**The zone-content-kind id table, re-verified independently.** `0x013026d0` (the dword
`0x00863e90` loads as its own search target) reads `0x2234` directly — **CONFIRMED — empirical,
independent re-read** of exactly the slot the scoping note's §2 table already names for "Objects"
(entry index 1 of the 12-byte-stride array at `0x013026bc`; this pass also independently
re-read `0x013026c4 = 0x2233` and `0x013026c8` as a valid `.rdata` pointer, consistent with the
note's "name one slot ahead" layout convention). This satisfies the scoping note's own §2
"recommended first action of U0" (re-verify the table against a second method) for the two
entries actually needed by U1; the other 20 entries were not independently re-walked this pass.

### 11.2 `0x00863e90`, the Objects (0x2234) section handler — read in full

Body `0x00863e90`-`0x00863f34`, ret, int3 padding, next function (`0x00863f40`, Light clip
meshes) begins exactly at `0x00863f40`. 60 real instructions (matches the scoping note's own
estimate almost exactly).

1. Calls `0x00863430(buffer, lengthBound, targetId=0x2234, &recordLength, &extra)`. If the walk
   returns `-1` (section absent from this zone), returns success trivially — a zone with no
   Objects section is not an error. **[CONFIRMED]**
2. Otherwise calls **`0x0045bc20(bufferBase, dataOffset, outerLength)`** — this is the function
   that turns the raw section bytes into the "batch" wrapper every downstream reader consumes;
   see §3 below for its own format. The result is stored at **zone-object `+0x11c`** (the same
   field `0x00866580`'s own zone-unload cleanup code — read directly at the very top of this
   pass's investigation, before any of this was understood — walks as a handle list; see §6).
   A null result (bad magic/version) makes the whole handler return failure. **[CONFIRMED]**
3. Compares the walker's `extra` out-value against `0x1c` (28): **below** the threshold, calls
   `0x008538f0(batch, 0x01165540, zone+0x7c)`; **at or above**, tests bit 0 of
   `zone-object+0x130` and calls `0x00853910(batch, flagBit, zone+0x7c)`. `0x01165540` is 8 zero
   bytes in `.rdata` (confirmed empirically: `cstr(0x01165540)` reads empty), immediately
   followed by the unrelated string `missing_mesh.clmesh_pc` — i.e. for the Objects section this
   "owner handle override" argument is always the null/no-op pair in practice, not a real
   override. **[CONFIRMED — disassembly for the branch and the argument values; the semantic
   label "owner handle override" is HIGH CONFIDENCE, carried from §4 below where it is read
   directly inside the two real consumers.]**

Both `0x008538f0` (body `0x008538f0`-`0x00853909`, exactly 26 bytes) and `0x00853910` (body
`0x00853910`-`0x00853929`, exactly 26 bytes) are confirmed-trivial forwarders: they reload their
3 stack arguments, fix `ecx = 0x2442750` (the registry singleton — see §4), and tail-call
`0x00457c60` / `0x00457a50` respectively. **Each has exactly the caller count the scoping note
claimed**: `0x00457c60`/`0x00457a50` are each reachable through exactly one forwarder each,
confirmed by a raw `E8`-relative-call scan of the whole `.text` section (0 other callers of
either real function). `0x008538f0` itself has a **second** caller, `0x00a85b31`, inside a large
vehicle-setup function (`0x00a856d0`) — traced and reconciled with
`interp_named_object_resolution.md` §2.2/§2.3: this is the *same, already-known* vehicle
"cover-node batch" load (`vehicle-definition+0xcc`, same `FBOW`/version-5 batch header format),
not a new or unrelated use. **This confirms directly, from this pass's own disassembly, that
`0x00457c60` is the registry's generic serialised-object-batch loader, not an Objects-section-
private function** — the Objects (0x2234) handler is simply its first/primary feeder for zone
loads. **[CONFIRMED — disassembly, raw call-site census: `0x00457c60`→1 caller, `0x00457a50`→1
caller, `0x008538f0`→2 callers, `0x00853910`→1 caller.]**

---

**Unit U1 — object placement & property stream: the real, concrete format.**

### 11.3 The Objects-section payload header (`0x0045bc20`)

`0x0045bc20(bufferBase, dataOffset, outerLength)` — body `0x0045bc20`-`0x0045bca4`, ret, int3
padding — is a pure function (no `this`/registry access) with a single job: turn the raw section
bytes at `bufferBase+dataOffset` into the wrapper every downstream function reads. Read in full
this pass, independently reproducing `interp_named_object_resolution.md` §2.3's own finding with
the same addresses:

- `*(P+0x00)` **must equal `0x574f4246`** — the bytes `F`,`B`,`O`,`W` in ascending memory order
  (`P = bufferBase+dataOffset`, the record's own start). Mismatch → the whole handler returns
  null (treated as "no payload," not an error). **[CONFIRMED — disassembly.]**
- `*(P+0x04)` **must equal exactly `5`** (two bound checks collapsing to equality) — the
  Objects-section payload's own format version. **[CONFIRMED — disassembly.]**
- `*(P+0x0c)` is read as a count and used to size an **8-byte-per-entry directory/handle-list
  array** that starts at `align4(dataOffset + 0x20)` relative to `bufferBase` — i.e. there is a
  **32-byte fixed region** at the front of the payload (magic+version are the first 8 of it; the
  remaining 24 bytes, offsets `0x08`-`0x1f`, are not otherwise read by this function) before the
  directory array begins. **[CONFIRMED — disassembly for the offsets and the alignment
  arithmetic; the semantic meaning of payload bytes `0x08`-`0x1f` beyond the `0x0c` count field is
  OPEN — not examined by any function read this pass.]**
- After skipping `count × 8` bytes of directory entries and re-aligning to 4, the function writes
  three fields into the wrapper **in place, overwriting the original header bytes it no longer
  needs**: `P+0x14` = absolute pointer to the directory array's own start; `P+0x18` = absolute
  pointer to the **per-object record stream** (right after the directory array); `P+0x1c` =
  **remaining byte length** of that stream (`outerLength` minus everything consumed so far).
  **[CONFIRMED — disassembly.]** The function's return value is `P` itself (reused as the
  "batch" wrapper for every later caller).

**`P+0x08` (the per-object *record* count, read later by `0x00457c60`/`0x00457a50` — see §4)
is a *different* field from `P+0x0c` (the directory-entry count read by this function) — confirmed
as two separate reads at two separate offsets, not a transcription error in either direction.**
Both are read directly from the untouched original payload bytes (this function never writes to
either offset). Whether real shipped data always carries the same numeric value at both offsets
(one directory entry per placed object) is plausible but **not verified against real file bytes
this pass — OPEN.**

**The directory array is not dead weight: it is the zone-unload handle list — confirmed directly
at `0x00866950`, a separate function, not a continuation of `0x00866580`.** This pass's very
first exploratory disassembly (run before this document's own function-boundary discipline was
in place) read past `0x00866580`'s real `ret`/int3 boundary at `0x008667f3`-`0x008667ff` without
noticing it, and so initially mis-attributed the code that follows — at `0x00866800`, then
`0x008668d0`, then `0x00866950` — to "`0x00866580`'s own tail." Caught and corrected before
publication: a dedicated re-disassembly of `0x00866950` on its own (body
`0x00866950`-`0x00866a4b`, ret, int3 padding, next function at `0x00866a50`) confirms it is
exactly the function `interp_named_object_resolution.md` §1.3/§1.7 already names for this role,
not `0x00866580` at all. Read in full: `eax = [zone-object + 0x11c]` (the wrapper pointer §2
stores), `eax2 = [eax + 0x14]` (directory array start — matching `P+0x14` exactly), looping `esi`
from `0` to `[eax+0xc]` (the directory count, `P+0xc`, matching exactly), reading **8-byte
`{handleLo, handleHi}` pairs** at `eax2[esi*8]`/`eax2[esi*8+4]`, resolving each through the handle
table (`0x00458230` on `0x024433a8`) and, unless the resolved object is a protected/liveness-gated
class (tested via its own `0x02cc9900`-row flags and a liveness probe at `0x00853b10`), tearing it
down (`0x008addb0`/`0x008add40` then `0x00854410`); finally zeroing the wrapper pointer itself at
`[zone-object+0x11c]` once every entry has been processed. **[CONFIRMED — disassembly, read as
its own standalone function this time, boundary-verified both directions; the earlier
mis-attribution was caught and fixed before this document's own publication, not left standing.]**

### 11.4 The per-object record head (28 bytes) — confirmed field-by-field

`0x00456760(destRecord, streamHeaderPtr)` — body `0x00456760`-`0x004567c3`, ret 4, int3
padding — is the generic copy routine both `0x00457c60` and `0x00457a50` call once per object.
Read in full this pass; every offset below was read directly off its own `MOV` instructions
(destination offsets relative to `ecx`, source offsets relative to `eax`):

| stream offset (from record start) | size | copied to (40-byte in-memory record) | role, as the registry code actually uses it |
|---|---|---|---|
| `+0x00` | u32 | rec`+0x00` | object's own handle, low dword (`0` = allocate a fresh handle via `0x004576e0`) |
| `+0x04` | u32 | rec`+0x04` | object's own handle, high dword |
| `+0x08` | u32 | rec`+0x08` | owner/parent handle, low dword (overridden by the *caller's* own owner-handle argument when that argument is itself non-zero — this is exactly the `0x01165540` null pair the Objects handler passes, §2 item 3) |
| `+0x0c` | u32 | rec`+0x0c` | owner/parent handle, high dword |
| `+0x10` | u32 | rec`+0x10` | **class-name hash — a plain 32-bit integer, NOT a string pointer** (see §5; this corrects an early, already-retracted misreading flagged in `interp_named_object_resolution.md` §1.7, independently re-derived and confirmed this pass by disassembling the "hash" function itself, §5) |
| `+0x14` | u16 | rec`+0x14` | copied through; not read by any of the registry code disassembled this pass |
| `+0x16` | u16 | rec`+0x18` **and** rec`+0x1a` (the same value written to both) | **length of the variable tail** that follows the 28-byte head — this is the field the per-object loop's own seek computation actually uses (see below); the scoping note's wording ("u16 field at header+0x1a") named the in-memory *destination* offset, not the on-disk *source* offset — **corrected here: the on-disk field is at `+0x16`, not `+0x1a`.** |
| `+0x18` | u16 | consumed into rec`+0x1c` (not stored verbatim) | **byte offset, within the tail, of the object's name property** — `rec+0x1c := (recordStart+0x1c) + thisValue` when the value is non-zero, else `rec+0x1c = 0` (unnamed object) |
| `+0x1c` | — (start of tail) | rec`+0x20` = recordStart`+0x1c`; rec`+0x24` := `0` | start-of-tail pointer bookkeeping only; the tail's own bytes are not touched by this function |

Total stream bytes directly examined by this function: 26 (`0x00`-`0x19`); the function never
touches on-disk bytes `0x1a`-`0x1b`, consistent with the record's own fixed head being exactly
`0x1c` (28) bytes with its last 2 bytes unread by the generic head-copier.
**[CONFIRMED — disassembly, every offset above read directly from `0x00456760`'s own
instructions.]**

**The per-object seek, confirmed via the generic stream primitives directly:**
`0x00457c60`/`0x00457a50` both compute `seekLen = (word at destRecord+0x1a) + 0x1c` and call
`0x00da7f30(stream, seekLen, mode=1)`. `0x00da7f30` (body `0x00da7f30`-..., read in full this
pass) is confirmed to implement three modes via a three-way branch on its mode argument:
**`0` = absolute from buffer start, `1` = relative to the stream's current position, `2` =
relative to the stream's end.** Mode `1` (the only mode used here) means **the seek is relative
to the record's own start** (the stream cursor had not moved since the head was peeked), so this
one seek instruction advances the cursor **past the entire current record** — fixed head
(`0x1c`) plus variable tail (the `+0x16` word) — landing exactly on the next record's own start.
**This both confirms and sharpens the scoping note's "28-byte fixed base + variable tail" finding
into an exact formula: `recordSize = 0x1c + (word at stream-offset +0x16)`.** **[CONFIRMED —
disassembly of `0x00da7f30`'s three-mode branch and both call sites' `mode=1` argument.]**

Two further generic stream primitives, confirmed this pass and not previously named in
`spec-zone-data-format.md` §9.3's three (`0x00da7e90` read / `0x00da8020` tell / `0x00da7f30`
seek — note that §9.3's "tell" is a *different* function from the one used here):

- **`0x00da8070(stream)` = AtEnd()** — compares `stream+0x15c` (current position) against
  `stream+0x160` (end position); returns true iff position ≥ end. **[CONFIRMED — disassembly,
  11 instructions, `sbb`/`inc` idiom decoded directly.]**
- **`0x00da8090(stream) = GetCurrentPointer()`** — when `stream+0x148 == 2` (a buffer-mode flag,
  confirmed to be the mode this Objects-section stream always runs in, since `0x00dab0e0` opens
  it directly over the section's own in-memory bytes), returns `*(stream+0x14c) + stream+0x15c`:
  an absolute pointer into the **live, already-loaded buffer** at the current cursor position,
  **without advancing it**. This is exactly the "peek" accessor `0x00456760` is handed — i.e.
  the whole per-object head-copy operates directly on the file's own loaded bytes, not on a
  separately-materialized temporary. **[CONFIRMED — disassembly, 16 instructions.]**
- **`0x00da9210(stream, n)` = AlignTo(n)`** — rounds the current position up to the next
  multiple of `n` (called with `n=4` after every per-object seek), consuming the padding bytes.
  **[CONFIRMED — disassembly.]**

### 11.5 Class-name resolution: how the hashed integer reaches a vtable (the `0x2cc9900` table, walked)

**The field at record-head `+0x10` is a precomputed 32-bit integer hash, not a string pointer —
independently confirmed this pass by disassembling the mixing function itself**, `0x00dab2b0`
(body `0x00dab2b0`-`0x00dab2e5`, read in full). It takes exactly **one 32-bit value and one
modulus** (`[esp+4]`, `[esp+8]`) — there is no byte-by-byte character loop anywhere in its body —
and applies a textbook Robert-Jenkins-style integer avalanche mix (shift-15/not/add,
shift-10/xor, multiply-by-9 via `lea ecx,[ecx+ecx*8]`, shift-6/xor, shift-11/not/add,
shift-16/xor) before an `idiv` by the modulus and returning the remainder. **This settles, by
direct disassembly rather than by trusting a prior note's wording, that the class-name field
is authored as a precomputed integer hash (almost certainly a CRC-32 or similar of the class's
real name string, computed once at content-build time) and requires no string data and no pointer
fixup to resolve from raw file bytes — a materially better position for Team B's parser than
"a pointer that needs a live, already-fixed-up buffer" would have been.** **[CONFIRMED —
disassembly, the function's full 24-instruction body decoded directly; it independently
corroborates `interp_named_object_resolution.md` §1.7's own correction of its earlier draft.]**

The resolution chain, confirmed end to end this pass (addresses relative to the registry
singleton `0x02442750`, confirmed live at `0x0116467c`'s construction — see §6):

1. `hash = 0x00dab2b0(recordHead+0x10, 0x1fe)` — mixes the stored integer modulo **510**
   buckets.
2. `bucketOrdinal = 0x004586a0(this = registry+0xc6c, hash, recordHead+0x10)` — walks a
   chained hash table (bucket array at table`+0x08`, chain-link array at table`+0x800`, a
   **value array at table`+0x1000`-ish holding the original 32-bit key** at table`+0xff8`,
   matching a 510-slot table: `8 + 510*4 = 0x7f8`, so the chain-link array at `+0x800` and the
   key array at `+0xff8` are simply the next two parallel 510-entry arrays laid out back to
   back) and returns the matching chain-entry ordinal, or `-1` if the integer key was never
   registered. **[CONFIRMED — disassembly, `0x004586a0` read in full, 20 real instructions.]**
3. On a hit, the **final class index** is a single byte at
   `registry + 0xc6c + 0x17f0 + bucketOrdinal` (= `registry + 0x245c + bucketOrdinal`, the two
   notations are the same address, cross-checked against `interp_named_object_resolution.md`
   §1.7's own "registry`+0x245c`+index" wording and found to agree exactly). `0xFF` is the
   explicit absent/not-found sentinel re-tested after this second lookup. **[CONFIRMED —
   disassembly.]**
4. `0x02cc9900[classIndex]` (a plain 4-byte-stride pointer array, one pointer per registered
   class) is indexed directly by that byte. **The table itself is a non-file-backed global
   (lives in the zero-fill tail of `.data`, confirmed via `pefile`'s own section metadata:
   `SizeOfRawData < VirtualAddress-relative offset`) — it reads as all-zero in the shipped
   executable's own bytes and is populated entirely at runtime**, exactly the dead-end shape
   `WALLS.md`'s "Emulator-harness dead ends" section already warns about for this project. A
   raw `.text`-wide scan for the literal `0x2cc9900` found **3,757 reference sites across the
   whole binary** (not just inside the Objects reader) — confirmed by spot-checking several far
   -away hits (e.g. `0x00b9a6f7`, inside unrelated code near `0x00b9axxx`) to be the *exact same*
   `byte[object+0x34] → 0x02cc9900[index]` idiom used throughout the engine for ordinary
   per-object class dispatch, not a zone-specific mechanism. **This settles one of the scoping
   note's own open questions: `0x02cc9900` is the engine's one universal object-class descriptor
   table, used for every game object everywhere, not a zone-Objects-private table and almost
   certainly *not* the same family as `spec-zone-data-format.md` §9.3's 40-bucket
   AI/spawn-behaviour-flag table (`0x00a33730`), which is a much smaller, differently-scoped
   mechanism — correcting the scoping note's speculative "very plausibly that table" framing to a
   documented negative-leaning finding.** **[CONFIRMED — disassembly + empirical (section
   metadata, xref count, spot-checked sample) for everything except the exact relationship to
   `0x00a33730`'s own table, which remains genuinely untested either way — see open items.]**

Each entry of `0x02cc9900` points to a small per-class descriptor struct whose **first field is
a vtable pointer**. Two vtable slots are confirmed directly used by the per-object loaders,
addressed by *byte offset into the vtable* (i.e. `offset/4` = slot index in the more common
convention):

- **Slot `+0x0c` (index 3) — capacity/availability probe.** Called `thiscall` with the
  descriptor itself as `this` and **zero explicit arguments**; a signed result `≤ 0` means "skip
  this object" (no error raised — the record is simply dropped from the batch).
  **[CONFIRMED — disassembly, both loaders.]**
- **Slot `+0x00` (index 0) — Create().** Called identically, `this`-only, **zero explicit
  arguments**, returns the new object pointer (or null). The newly-created object receives **no
  stream access of its own** — every generic field (handle, owner handle) is written into it
  directly by the loader after the fact (see §6); Create() is a bare allocate-and-default-
  construct. **[CONFIRMED — disassembly, both loaders.]**

Class names actually registered in this table were **not recoverable by static analysis this
pass** — the table is runtime-populated and, unlike `spec-zone-data-format.md` §9.3's
`0x00a33ff0` (which hardcodes ~30 property names as literal immediate operands, directly
readable), no single, enumerable "register all classes" function was found reachable by a direct
`E8`-call-site census (the one plausible wrapper, `0x00458300`, has **zero** direct callers —
it is reached only through a vtable, meaning each class's own registration happens through
per-class static-initializer code scattered across the binary, not one central list). This is a
genuine, honestly-reported limitation, not a guess — see Open Items.

### 11.6 The 40-byte in-memory record, the two loaders, and what actually gets written where

**`0x00457c60` (body `0x00457c60`-`0x00458069`, ret 0xc, int3 padding; 321 real instructions —
matches the scoping note's size estimate closely) is the registry's generic batch loader**,
confirmed this pass to be functionally identical in every particular already read to
`interp_named_object_resolution.md` §1.6's own independent 330-instruction read of the same
function (the 9-instruction difference is consistent with two different readers choosing
slightly different stop points, not a disagreement about content). Entry signature,
confirmed by register provenance tracing through both forwarders:
`0x00457c60(this=registry, batch=[ebp+8], ownerHandlePtr=[ebp+0xc])`.

1. Refuses unless **registry byte `+0x26a4`** is set (the "open" flag — confirmed this pass to
   also gate `0x00457a50` identically, both check it as their very first instruction after the
   prologue) and `batch` is non-null; does nothing (trivial success) if `batch+0x08`
   (**the per-object record count** — a separate field from the `+0x0c` directory-entry count
   §3 names, confirmed by direct re-check of both instructions' own byte encodings,
   `397e08`/`8b4608` vs `8b500c`) is zero.
2. Through the registry's own allocator (vtable `+0x38` off a pointer at registry`+0x54`),
   allocates **`count × 0x28` (40) bytes** for a scratch record array and **`count × 4` bytes**
   for an object-pointer output array. **[CONFIRMED — disassembly, the `×5, ×2, ×2, ×2` shift
   sequence (`lea ecx,[eax+eax*4]` then three `add edx,edx`) computing exactly `40×count`.]**
3. Opens a read stream over `batch+0x18`/`batch+0x1c` (buffer pointer/length, §3) via
   `0x00dab0e0`.
4. Per object, while not at end of stream: peek the current pointer (`0x00da8090`), copy the
   28-byte head into the next 40-byte scratch slot (`0x00456760`, §4), seek past the whole
   record (`0x00da7f30`, mode 1, §4), align to 4 (`0x00da9210`). If the batch's own
   `ownerHandlePtr` argument is non-null **and its two dwords are not both zero**, that pair
   **overwrites** the record's own owner-handle fields (rec`+0x08`/`+0x0c`) — i.e. a batch-level
   owner override exists generically (used by the vehicle cover-node path, §2), but is a no-op
   for the Objects section specifically since it always passes the null pair. If the record's
   own handle fields (rec`+0x00`/`+0x04`) are both zero, a fresh handle is allocated
   (`0x004576e0`); otherwise the pre-existing handle is reused verbatim.
5. Resolves the class via §5's chain (hash → bucket → byte index → `0x02cc9900[index]`); skips
   the object on any failure (absent class, `0xFF` sentinel, capacity probe `≤ 0`).
6. **Create()** (§5) produces the new object. The loader then writes, directly, with no further
   indirection:
   - new object `+0x08`/`+0x0c` := the record's own (resolved) handle
   - new object `+0x10`/`+0x14` := the record's owner handle (post-override)
   - registers the handle via `0x00458110` on the registry's handle table (registry`+0xc58`)
   - stamps a sequential `u16` object index at new object `+0x30`
   - appends the pointer to the output array at the current index and advances the index
     (objects skipped at step 5 do **not** advance the index — the output array is tightly
     packed, with no gaps for skipped records)
7. **A second pass, after the stream is fully consumed**, iterates every freshly-created object
   (those whose `+0x33` flag bit `0x10` — "uninitialised" — is still set) and, following each
   object's own owner-handle chain outward up to 16 levels, calls **registry vtable slot `+0x2c`
   (index 11) bottom-up** on each link, `(object, its own 40-byte record, 0)`. A `false` result
   tears the object back down via `0x00456d00`.
8. Closes the stream and frees the scratch record array.

**`0x00457a50` (body `0x00457a50`-`0x00457c54`, ret 0xc, int3 padding; 207 real instructions —
smaller than the scoping note's ~166-instruction estimate but the right order of magnitude) is a
genuinely different, older-style implementation of the *same* record format**, confirmed this
pass to have real, structural differences from `0x00457c60`, not just a cosmetic rewrite:

- Works **one record at a time on the stack**, with no upfront heap allocation of either array
  — it does not need the record count ahead of time the way the batch loader's allocation sizing
  does (though it still reads `batch+0x08` once, purely as the "nothing to do" short-circuit).
- **Caches the previous object's class-name hash** and skips the whole hash/lookup/remap chain
  when consecutive records share the same value (`cmp eax,[cachedNamePtr]; je <reuse ordinal>`)
  — a micro-optimisation `0x00457c60` does not have.
- **Does not call the capacity probe (vtable slot `+0x0c`) at all** before Create() — it calls
  Create() unconditionally once a class is resolved.
- **Does not apply the registry-default handle-allocation fallback the same way** — no call to
  `0x004576e0` was found anywhere in its body; a zero handle field appears to pass through
  unmodified (not independently resolved what effect this has downstream — OPEN).
- Writes new-object `+0x08`/`+0x0c`/`+0x10`/`+0x14` directly from the per-object local copy with
  **no owner-handle-override test at all** — its third parameter, `[ebp+0xc]`, is read as a
  **plain boolean flag** (tested once, late, to select between two registry-vtable-`+0x2c`-style
  calls at `0x00457bf7`), not as an owner-pair pointer the way `0x00457c60`'s third parameter
  is. **This means the scoping note's §4 description ("tests a flags bit... same idiom reused
  here") was right about `0x00457a50` specifically, but the earlier paragraph in the same
  note that implicitly treated both forwarders' third arguments as the same kind of thing is an
  oversimplification — corrected here: the two sibling functions take a *differently-typed* third
  parameter (owner-pair pointer vs. boolean flag), matching the different literal argument each
  forwarder actually passes (`0x01165540`, a pointer, vs. `zone+0x130 & 1`, a bit).**
- Inserts into the handle table via a single call to `0x00458180` rather than `0x00457c60`'s
  two-step probe-then-`0x00458110` sequence — functionally equivalent as far as this pass
  traced, but a genuinely different code path, not re-use of the exact same insertion routine.

**Which of the two real functions a given shipped `.czn_pc` Objects section actually exercises
depends on the walker's own `extra` value compared against `0x1c` (§2 item 3) — this pass did not
open any real file to determine which branch real shipped data takes; both are live, reachable,
real code, and Team B's parser should be written to expect either.** **[CONFIRMED — disassembly
for everything above; OPEN for the real-data branch frequency.]**

### 11.7 The variable tail: the property-list format, confirmed via its own writer

The tail starting at `recordStart+0x1c` (length given by the `+0x16` word, §4) is **not** a
zone-private format — it is the registry's generic per-object property list, the exact same one
`0x00456660` (body `0x00456660`-..., read in full this pass) builds when engine code constructs
a record in memory instead of streaming one from a file. Confirmed directly from `0x00456660`'s
own instructions (not merely cited from the prior note): a found-or-inserted property slot is
written as

```
offset +0x00  u16  kind
offset +0x02  u16  size   (byte length of the data that follows)
offset +0x04  u32  id     (identifies which named property this is)
offset +0x08  ...  size bytes of property data
```

**[CONFIRMED — disassembly: `mov word ptr [eax],cx` / `mov word ptr [eax+2],bp` / `mov dword ptr
[eax+4],edx` writing exactly this 8-byte header immediately before the data, with the matching
read-back comparisons (`movzx ecx,word[eax]; cmp ecx,<kind>` / `movzx edx,word[eax+2]; cmp
edx,<size>`) confirming the field order independently.]**

**MAJOR CORRECTION 2026-10-07 (unit U2, real-data population replay) — each property's data
region is individually 4-byte-aligned, not just the tail's own trailing end.** The format above,
read literally (sequential entries with no inter-entry padding), replays correctly against a
hand-picked single-property example but **fails on population**: a literal, no-padding sequential
walk reaches a real tail's own declared length (the record head's `+0x16` field) exactly in only
196,200 of 383,346 real objects (51.2%) — the other 48.8% drift and fail partway through. **The
real rule: `nextEntryOffset = round_up4(dataOffset + size)`, not `dataOffset + size`** — i.e. each
property's own data is padded up to a multiple of 4 bytes before the next property's 8-byte
header begins. Applying this one fix brings the SAME 383,346-object population to **100%** exact
completion (the tail's own declared length already accounts for this; no further end-of-tail
padding is needed once the per-entry rule is applied). **A re-implementer who follows the literal
(no-padding) format above will silently misparse roughly half of every multi-property tail past
its first property.** **[CONFIRMED — empirical, full population, before/after control: 51.2%→100%.]**

**The object's authored name is simply one such property.** The descriptor at `0x01352568`
points at the literal string `"ctg_object_name"` (confirmed empirically: read directly from the
executable's own `.rdata`, `cstr(0x129ea5c) == "ctg_object_name"`) ~~with `id = 0`~~ — the record
head's `+0x18` "name offset" field (§4) is the byte offset, *within the tail*, where this specific
property's data (a NUL-terminated string) begins. **CORRECTED 2026-10-07 (units U2 and the
character-spawn-props pass, independently, same value): the on-disk `id` is NOT the literal
integer `0` — it is `0x355ef946`, found verbatim at the predicted tail offset in 19,800/19,800 real
named objects (U2) and separately in a second real file (the character-spawn-props pass). This
value is exactly `CRC32_reflected(lowercase("ctg_object_name"), seed=0)` under the engine's own
already-known table-driven CRC-32 (`0x00D9E740`, table `0x01320DA0`, confirmed 256/256 against the
standard reflected table, polynomial `0xEDB88320`; the per-character case-fold, `0x00ea739d`, is a
plain ASCII `tolower`). This is not a per-property compile-time constant: every registered
property (and every registered class, §11.5) gets its own tiny, individually `int3`-padded,
~0x20-instruction static-initializer stub in `.text` (found for `ctg_object_name` itself at
`0x00fea3a0`, among 1,018 total call sites to the hash function found by a raw `.text` scan) that
computes this hash ONCE at process start-up and permanently caches it into an 8-byte,
`.data`-resident (writable, not `.rdata`) descriptor struct (`{u32 namePtr, u32 id}`) — the raw
file image shows `0` at these `+4` slots only because that is the pre-initialization placeholder,
never the value actually compared against tail entries at runtime. This is the exact same naming
convention §11.5 already speculated for the *class*-name hash ("almost certainly a CRC-32 or
similar... computed once at content-build time") — now settled precisely for property ids too,
and for the first time with content-side proof (not just a runtime-side mechanism) that the
authoring tool bakes this exact value into shipped files.** This gives a complete, closed answer
for how a placed object's name — e.g. a mission-critical NPC like `Killbane` — is actually encoded
on disk: it is the `ctg_object_name` property (id `0x355ef946`) inside that object's own
0x2234-record tail, pointed at by the fixed-head `+0x18` field. **[CONFIRMED — empirical for the
string and the id value; CONFIRMED — disassembly for the mechanism, independently re-verified by
two separate passes this pass, originally traced in `interp_named_object_resolution.md` §2.4/§2.8.]**

**The property-id and class-id namespaces are both this same CRC-32, confirmed by direct
disassembly of the registration code itself (not inferred from the dictionary-attack results
below).** A raw `.text` scan found 1,018 direct calls to the hash function `0x00d9e740`; the
preceding `mov eax,[descriptor]` was pattern-matched for 453 of them, yielding **443 distinct,
directly-read (not guessed) property/class name strings** — including the complete `script_npc_*`
family (16 properties: `script_npc_hp`, `script_npc_bleed_hp`, `script_npc_knockdown_hp`,
`script_npc_flags`, `script_npc_team`, `script_npc_peer`, `script_npc_items`, `script_npc_loots`,
and others), the character base class's own `human_*` properties, and a previously-undocumented
`script_vehicle_*` family (`script_vehicle_hp`/`_flags`/`_mp_team`/`_peer`/`_resource_uid`/
`_type_name`/`_variant_name` — the vehicle equivalent of `script_npc`'s HP-override/flags/team
mechanism). A separate, independent CRC-32 dictionary attack (hashing ~29,285 plain-identifier
strings pulled from the exe's own `.rdata`/`.data`, run before the registration-stub scan above
was available) resolved **265 of the 275 distinct real property ids seen on real objects (96.4%)**
and **40 of 42 distinct real class-name hashes (95.2%)** by exact hash match — cross-validated,
not merely plausible, by exact population-count agreement between a class and its own
class-specific properties once the tail-alignment fix above is applied (e.g. `mover_c_obj_idx`/
`mover_level_object_property_crc`, both carried by exactly 114,129 objects, matches the
`general_mover` class's own object count exactly; `script_npc_char_preset_name` at 6,788 matches
`script_npc` at 6,788 exactly; `cover_pos`/`cover_angle_left`/`cover_angle_right` at 12,956 each
matches `cover_node` at 12,956 exactly). Both methods — ground-truth registration-code reads and
the independent dictionary attack — agree on every name found by both. **Resolved classes
include** (object count in the population): `general_mover` (114,129), `level_light` (52,449),
`action_node_group` (33,989), `object_tree` (18,775), `effect_object` (17,307), `script_npc`
(6,788), `Navpoint` (5,982), `occluder` (4,076), `cover_node` (12,956), `cutscene_slate_obj` (111
— the class of the real, named `slate_bridge_killbane` object found in this population),
`Trigger_Volume` (1,656), `Script_Group`, `object_rig`, `Script_Mover`, `Script_Vehicle`,
`scripted_path`, `door`, `script_item`, `light_list`, `spawn_region`, `interior_volume`, and 20
more (1-1,927 objects each); 2 of 42 class hashes remain unresolved. **[CONFIRMED — disassembly for
the registration mechanism and the 443 directly-read names; CONFIRMED — empirical for the
dictionary-attack hash matches, cross-validated by exact population-count agreement.]**

**The max-HP/ignore-AI character-spawn override, left parked at the `.czn_pc` boundary by
`review/exe-notes-2026-10-01/interp_character_spawn_state.md`, is now CLOSED with real-data
proof.** That note had already traced the code-side mechanism (character `+0x1cac` := the
`script_npc_hp` property's value verbatim when ≥1, else a table default from
`spawn_info_ranks.xtbl`; an "ignore AI" bit set when `script_npc_flags` contains the substring
`ignore_ai`) but left whether any real placed object actually carries these values as an open,
code-only inference. A population scan of all 2,971 real `.czn_pc` Objects sections found
`script_npc_hp` authored **1,028 times** (values 1 to 100,000), `script_vehicle_hp` **354 times**
(up to 10,000,000, the vehicle equivalent), and `script_npc_flags` **6,702 times**, of which **425
literally contain the string `ignore_ai`** — including a concrete base-game (not DLC) example:
`sr3_city_0.vpp_pc/_a_tm_dt_02.str2_pc/sr3_city~f_a_tm_dt_02.czn_pc`, objects #21-25,
`script_npc_flags = "never_pickup_weapons ignore_ai should_not_be_grabbed dont_drop_cash
use_default_loadout"`. `script_npc_team` (4,104 occurrences) decodes to real gang-name strings
(`"Luchadores"`, `"Brotherhood"`, `"Morningstar"`, `"Neutral Gang"`, `"STAG"`). **This mechanism is
real, live, and used on well over a thousand placed objects across the shipped base game and all
three DLCs — not merely theoretically supported by the code.** Two honest negatives from the same
scan: `npc_script_ptr` and `human_preset` are both confirmed registered (ids computed, descriptors
present) but were not found authored with an explicit value anywhere in the 2,971-file sample —
plausible since the character↔script_npc bind is authored from the `script_npc` side and resolved
at bind time, not chased further. **[CONFIRMED — empirical, full population.]**

**The specific named boss character `Killbane` is now independently resolved end-to-end against
real data, settling the above by name (2026-10-07).** `crc32l('script_npc') = 0xa518e725` (computed
directly, matching the class's already-published 6,788-object population count exactly). A
full-population scan for this class hash finds 8 real objects whose `script_npc_char_preset_name`
property or own `ctg_object_name` contains `"Killbane"`, across three real zones —
`sr3_city~fm14\`.czn_pc`, `sr3_city~fm21\`.czn_pc`, `sr3_city~fm22\`.czn_pc` (×4),
`sr3_city~fm24\`.czn_pc` (×2) — all inside `sr3_city_0.vpp_pc`'s own per-mission `.str2_pc`
containers (`m14`/`m21`/`m22`/`m24`), **not inside any DLC archive** (an exhaustive raw re-scan
confirms zero of the 93 real `.czn_pc` files inside the three DLC archives combined mention
Killbane at all). Preset values seen: `npc_Killbane`, `npc_Killbane_flamboyant`,
`npc_Killbane_wrestling`, `npc_Killbane_blk_nomask`, `npc_Killbane_scifi`. `script_npc_hp` is 100 on
four scripted/invulnerable QTE-staging copies and **60000** on the two full-fight copies in `fm24`;
`script_npc_team` reads `Luchadores` on two records and `Brotherhood` on one (unexplained). This is
a different object from the `cutscene_slate_obj`-classed `slate_bridge_killbane` already named
above (a free-standing, unowned cutscene marker in a separate file, `sr3_city~f0904~al.czn_pc`,
with no HP/flags/team of its own) — the two are both real, both named after the character, and
serve different purposes. **[CONFIRMED — empirical, full population scan.]**

**Two more named characters, `Suko` and `Cyrus`, resolve by the exact same mission-prefixed-tile
mechanism (2026-10-08)** — closing a Team B probe that found neither bare name anywhere, which is
correct: the on-disk `ctg_object_name` is never the bare character name. **Cyrus**:
`sr3_city~fm13`.czn_pc`(offset 56788)/`sr3_city~fm23`.czn_pc`(offset 54232), preset
`npc_Cyrus_Temple`, `script_npc_team`=`STAG`, `ctg_object_name`=`npc_cyrus` (lowercase). **Suko**:
`sr3_city~fm19`.czn_pc`, three records — `Suko_inside`(25436), `Suko_start`(25664),
`Suko_checkpoint`(42800), `script_npc_team`=`Playas` — plus two `m19_cte_SukoWarp_01`/`_02`
cutscene-VFX anchors, and a `"M20_Pre_Phone_Call"`/`"M19_Pre_Phone_Call"` persistent open-world
trigger object sitting next to a `Suko` marker in two always-loaded tiles
(`sr3_city~f1212~al.czn_pc`, `sr3_city~f1312~al.czn_pc`). Each mission's own Lua always indirects
through a one-level group table that supplies the real on-disk name (e.g.
`M13_groups.penthouse_stag.cyrus = "npc_cyrus"`) before any live-object call — the bare character
name is never itself the resolved name, exactly like Killbane. **Two further names from the same
probe, `Paparazzi_WM` and `STAG_WF`, are NOT zone objects at all** — they are 2D Wwise audio-persona
names from `audio_personas.xtbl` (`spec-lua-api-behaviour.md` §8.5), a namespace completely disjoint
from `.czn_pc`; zero occurrences in any `.czn_pc`/`.czh_pc` file anywhere in the shipped game,
exhaustively checked (base + all 3 DLCs). **[CONFIRMED — direct `.czn_pc`/mission-Lua inspection.]**
Full citations: `scratchpad/named_objects_sentinels_0x954be8_m05_2026-10-08.md` §1.

**How the tail is actually consumed, traced to its real endpoint.** Registry vtable slot `+0x2c`
(index 11) — base implementation `0x00456d80`, derived override `0x008544b0` — is the function
every newly-created object is handed to in both loaders' second pass (§6 step 7). Read in full
this pass (100 instructions):

1. If the object has a non-zero owner handle, resolves the owner through the handle table and
   calls **the owner's own** vtable slot `+0x2c` with the child object (a recursive attach — this
   is why the second pass walks the owner chain outward, bottom level first).
2. Clears the object's own "uninitialised" flag bit.
3. If the record carries a name (`rec+0x1c` non-zero) and no other live object already resolves
   to that name (checked via the same per-kind resolver family `spec-lua-api-behaviour.md`
   already documents), calls `0x00457650` — confirmed to be the registry's set-name method,
   which inserts into the global, case-insensitive name map at `registry+0x2660` and writes the
   map-node's own name-string pointer back into the object's own `+0x18` field, so later lookups
   never need to re-walk the tail.
4. **Finally, calls the new object's own vtable slot `+0x04` (index 1), passing the 40-byte
   record, with no other arguments** — this is the class-specific "read the rest of my properties
   out of the tail" step. This single call is the hand-off point from "generic registry record"
   to "whatever a specific class (an NPC, a prop, a navpoint, etc.) actually does with its own
   named properties" — and it is **class-specific code, reached only via the per-class
   `0x02cc9900` vtable, not read this pass for any concrete class** (see Open Items — this is
   the one piece of "what a couple of concrete classes actually do with the bytes" the task asks
   for that this pass could not close).
5. On success, calls `0x00456b40` on the registry and returns true; on failure, the caller
   (the batch loader's second pass, or `0x00457a50`'s inline equivalent) tears the object down.

**[CONFIRMED — disassembly, `0x00456d80` read in full; cross-checked against the registry's own
static base/derived vtable tables at `0x01164648`/`0x0116467c`, both independently re-read this
pass and both matching slot `+0x2c` to the exact addresses above.]**

**The class-specific vtable slot `+0x04` step above is now resolved for two concrete classes
(2026-10-07, unit U2) — including the real registration mechanism for the whole `0x02cc9900`
table, found and read directly rather than only inferred from the read side.** Each class's own
C++ static-initializer (found via its resolved name string's raw 4-byte-immediate xrefs in
`.text`, made possible only once the dictionary attack above supplied the string to search for)
does, in order: (1) writes a shared, generic "base" vtable into the new descriptor object (seen at
this exact step for 21 of 40 resolved classes); (2) writes a literal, compile-time-fixed class
index as a byte (`general_mover` = 5, `script_npc` = 42) — confirming by disassembly, not
inference, that the index is baked into each class's own object code as an immediate, never
looked up from a central table; (3) calls a registration/hashing helper, `0x00d9e7e0` (a sibling
of the property-hash function `0x00D9E740`); (4) **writes the actual table entry**:
`0x02cc9900[classIndex] = descriptorPointer` — the exact stride (`×4`, pointer-sized) this
section already inferred from the read side; (5) zeroes several fields, THEN **writes a SECOND,
different vtable into the same descriptor slot** before the constructor returns. **Methodology
catch, disclosed in place:** reading only the FIRST vtable write (step 1) for `general_mover` at
first looked like a real, if anticlimactic, answer — its `Create()`/capacity-probe/slot-`+0x04`
all resolved to trivial 1-2-instruction stubs. Re-reading the function's full body showed the
table actually stores a pointer whose vtable field gets overwritten a second time (step 5) before
the constructor exits — the step-1 vtable is a transient base-class vtable, live only during
construction, never the one a fully-constructed object in the running game actually has. Reading
slot `+0x04` off the CORRECT (step-5) vtable for both `general_mover` (vtable `0x01177ee4`) and
`script_npc` (vtable `0x0117f2d0`) gives a real, substantive, non-trivial function in both cases:
each re-checks the object's own class-index byte (defensive), computes a pool-array slot index
from the object's own pointer via a magic-number division by a per-class element stride
(confirming the descriptor's own `+0x30` sub-object holds a per-class object pool), bounds-checks
it, and — only if in-bounds — makes one further, genuinely class-specific call (`0x006ce2d0` for
`general_mover`, `0x00a33860` for `script_npc`, neither traced further). **A concrete,
population-grounded answer to this section's own §9.3 cross-reference question:** `script_npc`'s
real vtable, `0x0117f2d0`, is exactly `0x0117f2a4 + 0x2c` — and `0x0117f2a4` is the *exact* address
§9.3 already names for its own, independently-found generic named-property mechanism
(`HasProperty`/`SetProperty`/bulk-deserialize), found by a completely different starting point
(the `use_default_loadout` string). §9.3's own slots 11-15 are byte-identical to `script_npc`'s
slots 0-4 — the standard MSVC shape for a class with multiple polymorphic bases sharing one
combined vtable array at different offsets. **This answers, for at least this one concrete class,
§9.3's own open "whether this exact class's bulk loader is ever invoked with the stream cursor
positioned inside a `.czn_pc` buffer specifically": yes, for `script_npc`** — not proven for every
class using that mechanism, but proven for this one, by two independently-reached codepaths
meeting at the same vtable bytes. **[CONFIRMED — disassembly, both classes' registration code and
both resolved vtables read in full; CONFIRMED — empirical (exact byte match of 5 consecutive
vtable slots between §9.3's and this section's independently-reached addresses).]**

**A third class resolved, 2026-10-10: `Trigger_Volume`.** Its real, live vtable is `0x01170934`
(written by both its constructor `0x0093b860` and destructor `0x0093ba90`) — this class deviates
from the general_mover/script_npc pattern observed above: its constructor performs only a SINGLE
vtable write, with no earlier transient "base" vtable observed, so the "two writes, take the
second" heuristic does not hold universally and should be explicitly verified per class rather
than assumed. Slot `+0x04` (`0x0093e520`) is itself the complete tail-property parser (after one
pool-bounds guard call) — no further indirection needed, unlike the other two classes' thin
wrapper-then-call-out shape. Full property-by-property mapping (shape type as an authored string
resolved to an enum, box/sphere/cylinder-specific vec3/scalar property reads, flags/action/delay
fields): `spec-lua-api-behaviour.md` §1.14 (primary splice, keeps the gameplay-semantics framing
alongside the already-landed runtime layout there) — not repeated here to avoid duplicate
maintenance. One genuinely open residual: the one-time class-registration stub (literal
class-index byte + `0x02cc9900[classIndex]=descriptor` write) was not located for this class, only
the per-instance constructor/destructor vtable-write site — the vtable identification itself does
not depend on finding it, since it was confirmed by a different, equally direct route.

**Bonus finding, population-wide: the `0x2239` top-level id ("Trailer") is not dead content — it
is a universal end-of-chain terminator.** All 2,971/2,971 real shipped `.czn_pc` files carry
exactly one `0x2239` record, always zero-length, always either the literal last top-level record
or followed by fewer than 4 trailing bytes. Zero `.text` references to it (already known) is
therefore the expected signature of a terminator sentinel the walker's own loop-exit condition
(reaching the end of the available buffer length) renders unnecessary to special-case in code —
not evidence the kind is unused. (Lightly corrected in place in `review/scoping-czn-interior.md`
by the same unit.) **[CONFIRMED — empirical, whole population.]**

**Empirical confirmation, 2026-10-07 (closing §11.9 open item 8 — this is the first pass in §11's
history to actually open real `.czn_pc` file bytes rather than reading code alone):** the entire
format chain above — top-level `{tag,length}` header, FBOW/version-5 payload header, 28-byte record
head, variable property-tail — was replayed against a real shipped Objects section
(`sr3_city~fawld.czn_pc`'s own `0x2234` record, 463,404 bytes) using an unmodified, line-for-line
translation of these exact offsets (`tools/harnesses/czn_u2_replay.py`). Result: **2,172 declared
objects, 2,172 objects walked, zero tail-overruns, zero non-advancing-cursor failures** — the first
real-data, population-level confirmation of the whole format end to end. Two property ids are now
pinned down numerically, confirmed identical across every file sampled (the "awld" open-world
aggregate zone plus 28 further safehouse/mission/minigame zones, 19,800 named objects swept):
- **`id = 895416646`** is the `ctg_object_name` string property (§11.7's own finding, now with its
  numeric id attached).
- **`id = 3367956165`** (`0xC8BEEEC5` — **[corrected 2026-10-08: the hex parenthetical previously read `0xC8B3B245`, a wrong conversion of the same decimal value; `3367956165` itself is the population-confirmed figure (19,800-object sweep) and is unchanged]**) is the position/transform property, in two observed shapes:
  `kind=2`/`size=12` (plain float32 X/Y/Z, no orientation) and `kind=3`/`size=28` (X/Y/Z plus a
  4-float quaternion) — which shape a given object uses is an authoring choice per marker, not a
  format constant; both are real and common.

**A directly relevant population finding, not previously documented anywhere in this project: zone
data genuinely stores player spawn/entry-point markers, as ordinary objects in this exact format —
confirming `spec-save-format.md` §6.6's own "crib/safehouse default" hypothesis for the first time
against real data.** A population-wide sweep (861 files with a parseable `0x2234` section) found 83
objects named `player_start` (bare, numbered, or zone-prefixed) across 29 files, plus 23
`local_player_start`/`remote_player_start` co-op pairs and 39 bare `local_player`/`remote_player`
objects. All of them resolve to the **same** generic class hash, `0x445c1f3d`, as the open-world
zone's own Chop Shop/Hitman/Lockdown activity-spawn markers — **there is no dedicated "player spawn"
class; the role is carried entirely by the `ctg_object_name` string on an otherwise completely
generic named-point/marker object**, confirming this format's own general design (the task brief's
own hypothesis, now empirically settled). Co-op local/remote pairs are consistently placed a few
units apart (e.g. safehouse 01's pair, 3.06 units apart, offset on one horizontal axis only). The
one deliberate negative check run: the game's literal opening bank-heist intro mission's own zone
files (`bank_m1$`/`bank_ext$` and all four named variants) carry **no** player/start/spawn-named
object at all — checked exhaustively, not assumed — consistent with the already-landed
`spec-lua-api-behaviour.md` §4.3/§35/§48.2 findings that the brand-new-game opening is placed by
script (`teleport`/`teleport_coop`), not by a passive zone marker; ordinary zone entry (a
safehouse, a diversion, resuming a mission) reads a real on-disk `player_start` object exactly as
described above. **[CONFIRMED — empirical, full write-up: `scratchpad/czn_player_spawn_2026-10-07.md`.]**

**Correction, 2026-10-08 — the negative above is correct only for the `bank_m1`/`bank_ext` landmark
files specifically, not for mission 1's own scripted-content zone, which was never checked by that
pass and does carry a marker.** `bank_m1`/`bank_ext` are the bank building's own
architecture/interior bundle, a separate file family from per-mission script data (parallel to the
already-documented `sr3_city~fm13`/`~fm14`/`~fm19`/`~fm21`-`~fm24` zones for Killbane/Cyrus/Suko,
§11.7). Mission 1's own equivalent, **`sr3_city~fm01.czn_pc`** (inside
`sr3_city_0.vpp_pc/m01.str2_pc/`), is a real, 100%-named 415-object zone — re-confirmed as the real
bank-heist mission's own data by its own population (`Josh_mark_vault`/`Shaundi_mark_vault`/
`Gat_mark_vault`, the `Horsie`/`Horse_trigger` "horse falls past the window" gag, `slate_bank_lobby`)
— and it DOES carry a player-start-shaped `Navpoint` marker, named **`Start_player_nav`**
(appearing twice, likely a co-op pair), the same generic class every other confirmed `player_start`
object above uses. A census of this zone's 11 `Trigger_Volume` objects found none shaped like a
"mission start" gate (all are in-mission scripted beats — bomb timers, the horse gag, a vault-access
gate, a free-run segment); a hash check confirmed neither `scripted_mission_start`
(`crc32l`=`0x5e20a824`) nor `silent_mission_start` (`0x4d3aec73`) appears anywhere in this zone's
object population — advancing, not closing, `spec-lua-api-behaviour.md` §36.1's own open question
of where those records are really carried, for this one mission specifically. HIGH CONFIDENCE
(synthesis of 3 independent facts, not an EXE re-trace of mission 1's own bind call — see
`spec-lua-api-behaviour.md` §21.27a's own cross-reference): mission 1 most likely launches via the
already-documented `0x006D9310` direct-relaunch bypass rather than the ordinary walk-into-marker
mechanism, since it is the one mission with no free-roam period before it at all. Full detail:
`scratchpad/first_mission_bootstrap_dataside_2026-10-08.md`.

### 11.8 The registry singleton's relevant field layout (`0x02442750`)

Confirmed this pass, by direct disassembly of the functions that touch each field (not asserted):

| offset | role |
|---|---|
| `+0x00` | vtable pointer (constructed once at CRT start-up; `0x01164648` base / `0x0116467c` derived — both independently re-read this pass) |
| `+0x54` | pointer to the registry's own generic allocator object (vtable `+0x38` = allocate, used for the per-batch scratch arrays) |
| `+0xc58` | the handle table (vtable `0x01164608`; `0x00458110` inserts, `0x00458230` looks up) |
| `+0xc6c` | the class-name hash table (510 buckets; §5) |
| `+0x245c` | `= +0xc6c + 0x17f0`, the byte array mapping bucket ordinal → final class index (§5) |
| `+0x2660` | the global, case-insensitive name map (2,999 slots; `0x00458850` find-or-insert, `0x00457650` set-name) |
| `+0x26a4` | the "registry is open" flag both per-object loaders gate on at entry |

**[CONFIRMED — disassembly for every offset above, independently re-derived this pass from the
functions that reference them, not copied from the prior note's own table without re-reading the
code.]**

### 11.9 Open items

1. ~~**Which of the two real per-object loaders (`0x00457c60` batch, or `0x00457a50`
   one-at-a-time) does a real shipped `.czn_pc` Objects section actually exercise?**~~ **RESOLVED
   2026-10-07 (unit U2): only the batch loader, `0x00457c60`.** The walker's `extra` out-value is
   `0` (and therefore below the `0x1c` threshold) in 861/861 real Objects sections across the full
   2,971-file population, zero exceptions. `0x00457a50` is real, reachable, live code (its own
   forwarder `0x00853910` has exactly the one caller already found) — it is simply never reached
   for the Objects section specifically by any shipped base game/DLC/patch content. Team B's
   parser should still handle both shapes defensively (nothing here proves `0x00457a50` is
   unreachable in principle — modded content, a different build, or the other known caller of
   `0x008538f0`, the vehicle cover-node batch, could exercise it), but for shipped content one
   branch is the only one that fires.
2. **The top-level `{tag,length}` record's own header fields beyond the tag/length pair are not
   fully mapped.** §11.1 found the header widens from 8 to 12 bytes when a specific bit in the tag
   word's top 3 bits is set (an extra dword at `tag+8`), and that the record's `extra` out-value
   (compared against `0x1c` by `0x00863e90`) comes from this same walker — but exactly which field
   of the 8/12-byte top-level header `extra` is, and what the 12-byte variant's extra dword means,
   were not pinned down. This sits one level **above** the FBOW/version-5 payload header (§11.3),
   a different, nested structure — don't conflate the two when reading real bytes.
3. ~~**The FBOW-payload's own bytes at offset `0x08`-`0x1f` ... whether real
   files always carry equal values at both, or can legitimately differ, is untested.**~~
   **RESOLVED 2026-10-07 (unit U2): `recCount@+0x08 == dirCount@+0x0c` in 861/861 real Objects
   sections (100%), zero exceptions — one directory entry per placed object, population-wide.**
   The remaining unidentified bytes at `+0x10`-`+0x1f` were not chased further this pass.
4. ~~**Concrete class identities registered in the `0x02cc9900` table were not recovered.**~~
   **SUBSTANTIALLY RESOLVED 2026-10-07 (unit U2): 40 of 42 distinct real class-name hashes named
   by exact CRC-32 match, and the real per-class registration code found and read directly for
   two of them** — see §11.7's own "class-specific vtable slot `+0x04`" paragraph for the full
   mechanism and the class table. Not every one of the 40 had its registration code independently
   read (only `general_mover`/`script_npc` were); 2 class hashes remain unresolved.

   **Extended, 2026-10-09** (`spec-player-runtime.md` §7.11, OPEN 55 extension): a whole-`.text`
   byte-shape scan (not name-guessing) found 43 total registered classes, extending this to 9
   fully-disassembled class registration stubs (`vehicle`, `player`, `npc`, `general_mover`,
   `weapon`, `door`, `item`, `script_vehicle`, `script_npc`) — all sharing one byte-identical
   template, confirming the `0x02cc9900[classIndex]` write mechanism directly for 9 concrete
   classes, not just 2. Also found the real per-object `Create()` writer of an object's own class
   index byte (`+0x34`) for `player`/`npc`/`vehicle`. **Dispatched specifically to find the writer
   of a non-zero kind-row bit (the `0x00853b30` per-class descriptor bytes `+6..+0x25`,
   `spec-player-runtime.md` §7.11): an exhaustive 4-strategy whole-`.text` search found NONE** —
   every registration stub unconditionally zeroes its own kind row and no later setter exists
   anywhere in the binary. Full detail in `spec-player-runtime.md` §7.11.
5. **Whether `0x02cc9900` is the same table family as §9.3's `0x00a33730` (the 40-bucket
   AI/spawn-behaviour-flag table) remains formally untested, though this pass leans negative.**
   3,757 raw xrefs to `0x02cc9900` across the whole engine, confirmed by sampling several far-away
   call sites to be the same generic `object+0x34 → class table` idiom used everywhere, versus
   `0x00a33730`'s much narrower, AI-behaviour-specific 40-entry scope — corrects §9.3's own
   speculative "very plausibly that table" framing to a documented negative-leaning finding, not
   yet a direct structural comparison.
6. **`0x00457a50`'s handle-allocation behaviour when a record's own handle fields are zero is not
   confirmed.** Unlike `0x00457c60` (which calls `0x004576e0` to allocate a fresh handle), no call
   to that function, or any other obvious handle-allocator, was found anywhere in `0x00457a50`'s
   body. **Resolves with:** reading `0x00458180` (the insertion call this path uses instead) in
   full.
7. **The per-object record head has two separate, adjacent u16 fields that are easy to transpose
   by hand** — on-disk offset `+0x16` (tail length, duplicated into the in-memory record at both
   `+0x18` and `+0x1a`) and on-disk offset `+0x18` (the name's byte offset within the tail,
   consumed into the in-memory record's `+0x1c`). Flagged for whoever next cites either number in
   isolation — re-check §11.4's own table first.
8. ~~**No real `.czn_pc` file's Objects-section bytes were opened at any point this pass** — every
   finding in §11 is a code-only (disassembly + static `.rdata` string) result. The natural next
   unit (the scoping note's own U2, "record interiors") is exactly this: take a real shipped
   Objects section and replay it field-by-field against the format §11 specifies, with the
   population-and-controls rigor §10 already models, to confirm or correct every byte offset
   against real data rather than code alone.~~ **RESOLVED 2026-10-07** — see §11.7's own
   "Empirical confirmation" paragraph: a real shipped Objects section (2,172 objects) was replayed
   end to end with zero failures, and the two most-common property ids (name, position/transform)
   are now pinned down numerically. The class-specific tail-parse step (item 9, immediately below)
   remains the one still-open piece.
9. ~~**The class-specific "read my properties from the tail" step** ... was not read for any
   concrete class — the same underlying gap as item 4.~~ **RESOLVED for 2 of 40 named classes,
   2026-10-07 (unit U2)** — see §11.7's own paragraph. The remaining 38 classes' own slot `+0x04`
   implementations, and the two genuinely class-specific follow-on calls found inside the two read
   so far (`0x006ce2d0` for `general_mover`, `0x00a33860` for `script_npc`), were not read.
10. **Whether a real shipped zone ever has one of the two "fast"/"header fast" buffers absent**
    (the condition that would make `0x008660e0`'s two dispatcher calls not both fire, §11.1) was
    not checked against any real container. Affects only whether the Objects section is reached
    at all for a given zone, not the format once reached.
11. **Non-item, resolved during this same pass, kept for the record:** `0x00866950` (the
    zone-unload handle-list walker, §11.3) was briefly mis-attributed mid-investigation as part of
    `0x00866580`'s own tail (an early exploratory dump read straight through `0x00866580`'s real
    `ret` without noticing the boundary) — caught and corrected before publication via a dedicated
    standalone disassembly confirming its own `ret`/int3-padded boundary.

### 11.10 Named-object resolution, end to end: a consolidated cross-reference (2026-10-09, no new findings — a synthesis pass for Team B's mission-host work)

The pieces needed to answer "how does a mission script's string name (`"m05_car_01"`, `"#PLAYER#"`,
a group or navpoint name) resolve to a live world object" are already fully CONFIRMED but scattered
across this section and `spec-lua-api-behaviour.md`. This subsection is a pointer, not new content —
every claim below was independently established elsewhere and is cited, not re-derived.

1. **On disk:** the name is one property, `ctg_object_name` (id `0x355ef946`), in the per-object
   property tail this section documents (§11.4/§11.7) — see the worked `Killbane`/`Cyrus` examples
   in §11.7.
2. **Live registration — the moment a placed record becomes a resolvable handle:** registry
   base-vtable slot `+0x2c` (`0x00456d80`), called once per object in the batch loader's second
   pass, calls `0x00457650(obj, name)` to insert the name into the global name map at
   `registry+0x2660` (registry singleton `0x02442750`, §11.8). This path is generic across 4
   producers (zone placement, vehicle cover-node batches, in-memory engine records including group
   members, and co-op-replication replay) — not `.czn_pc`-private.
3. **The lookup path:** every Lua native that resolves an object by name goes through a unified
   two-layer mechanism — a shared hash lookup `0x004588f0` against that same name map, then a
   per-native "kind" bit-test against the object's own row in the class-descriptor table
   `0x02cc9900` (indexed by the object's class-index byte at `+0x34`). Full resolver-family table
   (5 confirmed members with exact bit/offset) in `spec-lua-api-behaviour.md` §4.13.
4. **`"#PLAYER#"` and its 7 sibling sentinels** (`#PLAYER1#`, `#FOLLOWER#`/`#FOLLOWER1-3#`,
   `#PLAYER1_VEHICLE#`, `#CLOSEST_PLAYER#` — a closed set of exactly 8, confirmed by whole-`.rdata`
   sweep) are special-cased literal-string short-circuits in the *generic* resolver
   (`0x00a280c0`/`0x00a281a0`/`0x00a28150`) only — they never touch the name map, and a native built
   on a bare type-gated resolver (item 3) does not see them at all. Full detail:
   `spec-lua-api-behaviour.md` §3 front matter/§4.13.
5. **Groups and navpoints are NOT separate mechanisms** — both resolve through the identical
   `0x004588f0`-on-`registry+0x2660` lookup as any other object name, differing only in which
   `0x02cc9900` bit the calling native requires (`0x005eab60` for groups, `0x005982e0` for
   navpoint/marker-class objects). `group_create_do`/`group_create_hidden_do` require the group to
   already resolve by name (it's an ordinary placed `Script_Group` object), and spawn previously-
   dormant member NPCs/vehicles into live objects for the first time — cross-confirmed against a
   real mission trace (`m05.lua` line 649, `group_create` → the Pierce character constructor).

**Still genuinely open** (unchanged by this pass): the plain-English meaning of each `0x02cc9900`
bit is inferred from caller names, not from the table's own contents (it's runtime-populated, reads
as all-zero in the file image); two resolvers' (`0x005e4e30`, `0x004584e0`) own bit/offset are
unpinned; only 2 of ~40 classes' own tail-parse steps have been read. Full synthesis, with every
address cited against its source: `scratchpad/named_object_resolution_2026-10-09.md`.

---

## 12. The Navmesh (0x2235) section: handler, on-disk format, and its link to the runtime navmesh-cell API (unit U3, 2026-10-07)

**Scope:** unit U3 (`review/scoping-czn-interior.md` §6/§8) — read the Navmesh (`0x2235`) section
handler past the tag match, settle the handler-address question against §11.1's own call list, and
test the falsifiable question of whether the file-embedded section feeds the already-documented
runtime navmesh-cell API (`spec-ai-runtime.md`'s cell-resolve/cell-connectivity functions,
cross-cited via `spec-player-runtime.md` §10.6) or is a separate/dead representation.

### 12.1 The handler address, resolved independently of §11.1's own prose

**`0x008642a0` is the real Navmesh (`0x2235`) handler; `0x00864200` is the Heightmap (`0x2244`)
handler.** §11.1's own call list already has this right when read by position (the 5th id,
`0x2235`, pairs with the 5th address, `0x008642a0`; the 6th id, `0x2244`, pairs with the 6th
address, `0x00864200`) — an apparent conflict here traced to a mis-summary of that list elsewhere,
not an error in §11.1 itself. Confirmed a second, independent way, unrelated to call-list position:
each handler hard-codes its own search target as a direct memory read — `0x008642a6`:
`mov edi,[0x013026dc]`; `0x0086420b`: `mov edi,[0x01302748]` — and the zone-content-kind table at
`0x013026bc` (stride 12 bytes, `{ptr,ptr,u32 id}`) shows `0x013026dc` = entry 2's id field =
`0x2235` ("Navmesh"), `0x01302748` = entry 11's id field = `0x2244` ("Heightmap"). **[CONFIRMED —
disassembly, two independent methods agreeing.]**

### 12.2 `0x008642a0` read in full (130 bytes/52 instructions) — the handler itself does almost nothing

Past the inlined `{tag,length}` chain-walk (the same idiom §9.5/§11.1 already describe — a
different handler inlining it directly rather than calling the shared helper `0x00863430` the
Objects/Interior-Triggers handlers use, not a contradiction of that earlier finding), on a tag
match the handler reads one gate dword immediately after the header; if that dword is nonzero, it
passes a raw pointer into the section's own bytes, plus this zone's own u16 id (read from the
zone-context argument's `+0x134`), to a generic function: `0x00447160(zoneId, mode=0, payload_ptr)`.
Both the "tag not found" path and the "gate dword is 0" path converge on an unconditional success
return — a zero gate is a silent no-op, not an error. **All real parsing of navmesh bytes happens
downstream of this call, not in the `.czn_pc` handler itself.** **[CONFIRMED — disassembly, full
function body.]**

### 12.3 The chain from the file handler to the already-documented runtime navmesh-cell API

`0x00447160` (133 bytes, generic per-zone "content registration," mode must be `< 3`) stores the
payload pointer into a fixed-capacity global registry (base `0x3171a5c`, count `0x3171a58`,
16-byte stride: `+0` zoneId, `+2` presence/mode bitmask, `+4..+0xf` up to three 4-byte mode-data
pointers — i.e. exactly 3 "modes" per zone; navmesh is mode 0, the other two modes exist but weren't
identified this pass) via the lookup/allocate helpers `0x00447010`/`0x00447050`, then calls up to
two registered per-mode callbacks from a separate table at `0x0135213c`. For mode 0 these are
`0x00446d50`→`0x00446750` (the real format parser, below) and `0x00446d60`→`0x00446930`/
`0x004501a0` (cross-zone cell stitching, §12.4). A third mode-0 slot, `0x00446d80`, exists in the
same table row but is never called from `0x00447160` itself — plausibly a zone-unload/unregister
counterpart, not traced.

`0x00446750` (~366 bytes) is the real format validator/parser, called with `esi` = the exact
`payload_ptr` the handler produced: it checks `[esi]=='VANB'` (file order), `[esi+4]==0x50464C49`
(file-order bytes `'I','L','F','P'`), `[esi+8]==0x10000` (version), bailing on any mismatch without
registering anything; then fixes up six self-relative offset fields at `+0x34,+0x38,+0x3c,+0x40,
+0x44,+0x48` (each `-1`→null or a file-relative byte offset converted to an absolute pointer),
walks a `u16@+0x28`-counted array at `+0x38` (stride `0x1c`/28 bytes, the cell-record array, each
entry's own `+0`/`+4` sub-fields fixed up as indices into the `+0x3c`/`+0x40` arrays), and two
further parallel 4-byte-stride index arrays at `+0x3c`/`+0x40` (counts `u16@+0x2a`/`u16@+0x2c`).

**This exact structure is independently confirmed to be the same one the already-documented
runtime navmesh-cell-resolve function reads.** Re-disassembling `0x00446c60` (69 bytes/29
instructions, matching its existing citation almost exactly by size) shows it: calls the SAME
registry lookup `0x00447010`; tests the SAME mode-0 presence bit; reads the SAME mode-0 data
pointer; reads the SAME `+0x28` count; bounds-checks against it; indexes the SAME `+0x38` array at
the SAME 28-byte stride. **Same pointer, same offsets, same stride, read by both sides of the
chain — not merely "both are about navmeshes."** `0x00445980` (the already-documented
cell-connectivity function, 214 bytes/89 instructions, re-confirmed) reaches its cell data via a
pointer already resolved by its own caller rather than a fresh `0x00447010` lookup — consistent
with, but not an independent re-confirmation of, the registry link. **[CONFIRMED — disassembly,
the full call-graph edge `0x00446c60`→`0x00447010`←`0x00447160`←`0x008642a0` traced address by
address.]**

### 12.4 Cross-zone stitching (the other mode-0 callback)

`0x00446930` (~655 bytes) walks the same global registry across every other currently-registered
zone, doing AABB overlap tests between pairs of navmesh blobs' own header fields (`+0xc,+0x10,
+0x14,+0x18,+0x1c,+0x20`), and on overlap calls a cell-linking function (`0x004468c0`, not traced)
to connect boundary cells between zones. `0x004501a0` performs a similar bounding-box walk against
a different global list (`0x031c5390`, 44-byte stride) and calls `0x00448870` (not traced) — the
same "stitch adjacent zones" shape. This parallels the Heightmap kind's own documented
"stitched into the larger world" description (§2) — independent thematic confirmation that this
project's zone system generally expects per-zone data to be joined at load time. **[CONFIRMED —
disassembly for control flow; stitching semantics beyond that are HIGH CONFIDENCE/HYPOTHESIS, not
labelled field-by-field.]**

### 12.5 The on-disk Navmesh format, population-validated against 2,971 real shipped `.czn_pc` files

Extraction via the same `vehgeo_bulk.collect()` helper other `.czn_pc` units this session already
used, against every `.vpp_pc` archive in the game's `packfiles/pc/cache`. The literal tag dword
`0x80002235` was found at **1,647** distinct positions across the 2,971 files. Immediately after
the generic `{tag,length}` top-level record, an 8-byte Navmesh-specific sub-header:
`u32 gate` (**CONFIRMED EMPIRICAL: exactly 0 in all 1,647 real instances, zero exceptions**),
`u32 subcount` (observed `1` in every one of the 645 instances carrying a real blob — not
independently tested against a `subcount != 1` case, since none was found). The VANB blob itself,
population-validated against all 645 real occurrences (every one sits at exactly tag-offset+16,
confirming the 8-byte sub-header):

| offset | field | population result (645 real blobs) |
|---|---|---|
| `+0x00` | magic `"VANB"` (file order) | 645/645 match |
| `+0x04` | magic2, raw dword `0x50464C49` (file-order `'I','L','F','P'`) | 645/645 match |
| `+0x08` | version `u32` | 645/645 = `0x00010000` |
| `+0x24` | `u16`, stamped into every cell record's own `+0x14` | observed 0 in sampled rows |
| `+0x28` | `u16` cell-array count | min 1, median 1, max 3,530 |
| `+0x2a` | `u16` count (paired with `+0x3c` array, stride 4) | always observed equal to `+0x2c` |
| `+0x2c` | `u16` count (paired with `+0x40` array, stride 4) | always observed equal to `+0x2a` — two parallel index arrays of identical length, not independently tested against a counterexample |
| `+0x2e` | `u16` count (paired with `+0x44`, stride 8) | 0 in most samples; nonzero when `+0x44` present |
| `+0x34` | offset to a constant-size header region | exactly `80` in all 645, zero variance |
| `+0x38` | offset → cell array, stride `0x1c` | range 112–39,304 |
| `+0x3c` | offset → index array, stride 4 | range 140–137,696 |
| `+0x40` | offset → index array, stride 4 | range 156–194,356 |
| `+0x44` | offset, stride 8 (optional) | absent (`-1`) in 39/645 |
| `+0x48` | offset (optional) | absent (`-1`) in 470/645 (73%) — rarely-populated, not characterized further |
| `+0x4c` | offset → name/debug table | never `-1`; range 172–252,448 |

`+0x28`/`+0x38`'s count/array/stride triple is the exact one `0x00446c60` reads at runtime (§12.3)
— a real, population-wide, zero-exception match between the handler's own downstream parser and
the already-documented runtime consumer. **[CONFIRMED — empirical, full population scan.]**

### 12.6 The discriminator: wired as the same system, but gated off in every real shipped file

**Answer: the structural link to the already-documented runtime navmesh-cell API is real and
confirmed at the level of exact field offsets and a shared helper (`0x00447010`) — but the call
that would exercise it is gated by a single dword found to be exactly `0` in all 1,647 real
occurrences of the Navmesh section across every shipped `.czn_pc` file examined (2,971 files, every
archive in `packfiles/pc/cache`), including all 645 occurrences carrying a large (up to ~252 KB),
well-formed, correctly-versioned navmesh blob. Zero counterexamples.** This is neither a clean
"same system" nor a clean "separate system": it is **wired to be the same system, structurally, but
switched off in the data for every real shipped file found.** Three honest readings, none picked as
the answer: (1) shipped-dead editor/tool data, the navmesh baked by the World Editor but the
shipped game's actual pathing built some other way (a parallel to the already-documented
`spec-world-streaming.md` §10.5 dead authoring-only branch, not confirmed to be the same mechanism
here); (2) a deliberate build/debug switch left off for retail; (3) a field whose real purpose this
pass has mis-identified (considered unlikely — the gate read was checked against raw opcode bytes,
not just disassembler text, but not ruled out). **What is not in question:** the field-offset match
between the file parser and the runtime consumer, and the call-graph edge through `0x00447010`, are
both real, checked directly and by full population — not a coincidence of shared terminology.

### 12.7 Open items (unit U3)

1. Why the gate dword is always `0` in every real shipped file — the central open question, §12.6.
2. ~~Whether a second, live loading path for navmesh data exists elsewhere (e.g. runtime-built from
   collision geometry) — not found or traced this pass.~~ ~~**CLOSED 2026-10-07** (a direct follow-up
   unit, `scratchpad/real_pathfinding_source_2026-10-07.md`): no second loading path exists anywhere
   in the binary. An exhaustive whole-binary call-site census found `0x00447160` (this section's own
   mode-registration function) has exactly one caller in the entire executable — the dead gate this
   section already documents — and its slot-allocator `0x00447050` has exactly one caller in the
   entire executable (`0x00447160` itself)...**~~ **RE-OPENED, LIVE-REFUTED, 2026-10-08
   (`spec-ai-runtime.md` §14.7a): a live Frida capture against the real running game found the
   registry this "closed" finding declared dead IS populated and functioning in real gameplay —
   87,262 of 87,918 real calls (99.25%) to the registry reader `0x00447770` succeeded, and the
   dependent path query `0x00a12a80` returned success 10/13 times, closely correlated with real
   cover-selection attempts. The "exhaustive call-site census" above was wrong or incomplete — a real
   second writer exists that it did not find.** **Important: the §12.6 DATA finding immediately above
   (the gate dword reads exactly `0` in all 1,647 real file occurrences) is UNAFFECTED by this — that
   remains a confirmed population-level fact about the file format.** What's refuted is the INFERENCE
   that this file-side gate's deadness means the runtime registry it would feed is therefore never
   populated by anything — the live evidence shows it IS populated, just not (or not only) through
   this specific file-side gate. Candidate "a runtime-generated navmesh from collision geometry," a
   different loader, or a streaming callback are all back on the table, un-refuted. **A dedicated
   search for the real writer is IN PROGRESS as of this edit (`spec-ai-runtime.md` §14.7a) — this item
   will be updated again once it concludes.** **A specific hypothesis tested and REFUTED, 2026-10-08:**
   the original §12.6 gate-dword scan aggregated all 2,971 files together; since the `~f<CCRR>`/
   `~s<CCRR>` tile-file-family finding (§13.2) shows only `~s` carries real Mesh geometry, it was
   proposed that the gate might be SET specifically in the `~s` family, buried in the all-files
   aggregate. **Re-ran the scan broken out by family, both real city archives specifically
   (`sr3_city_0.vpp_pc`/`sr3_city_1.vpp_pc`, 2,878 `.czn_pc` files, 1,588 Navmesh-tag occurrences):
   `~f`=468/468 gate=0, `~s`=468/468 gate=0, plain-named=652/652 gate=0 — zero nonzero gates in ANY
   family, including `~s`.** This hypothesis is cleanly refuted; the §12.6 finding was already correct
   and complete, not an artifact of aggregation. The contradiction between the file-side gate (always
   0, now re-confirmed per-family too) and the live capture's own retvals (99.25% success) remains
   unresolved by this check.
3. The other two registry "modes" per zone (`0x3171a5c`'s `+4..+0xf` supports 3 total; only mode 0
   was identified) — not traced.
4. `0x00446d80` (the third mode-0 callback slot never called from `0x00447160` itself, plausibly
   zone-unload teardown) — not traced.
5. Cell-record field semantics inside the 28-byte stride beyond "count/array/stride" (§12.3/§12.5)
   are HYPOTHESIS, not labelled against real cell-geometry meaning.
6. Untraced callees: `0x00446bc0`, `0x00450230`, `0x004468c0`, `0x00448870`, `0x00dc6190` (looks
   like a name/debug-table registration call off `+0x4c`), plus several shared low-level helpers.
7. The human-navigator chain (`0x00993e90`/`0x00993630`) was not read this pass — the discriminator
   answer didn't require it; flagged in case a future unit needs it.
8. Per `purrsian-a4`'s relay (2026-10-07): Team B independently confirmed Havok's own navmesh
   module is not linked into this binary (0 occurrences of `hkai*`) and no spec anywhere defines an
   in-memory navmesh format — consistent with, and not re-tested against Havok specifically by, this
   unit's own finding that the format is a bespoke `"VANB"`/`"ILFP"`-magic layout, not a Havok
   pass-through.

### 12.8 Clean-room note

No decompiled code, pseudocode, or original identifiers reproduced. Function addresses are
evidence anchors per this project's standard convention. The quoted ASCII strings (`"VANB"`,
`"ILFP"`/`"PFLI"`) are shipped engine magic-number bytes read directly from the executable's
immediate operands and from real shipped `.czn_pc` file bytes, cited the same way this file already
cites other magic/FOURCC values (`SR3Z`, `0x0BADBEEF`). Real shipped `.czn_pc` file bytes (2,971
files) were read directly from the user's own game installation via the project's existing
`vehgeo_bulk.collect()` helper, already used by multiple other units this session. No leaked
Saints Row IV/Re-Elected source, leaked Havok source, or any other forbidden material was read,
consulted, or used.

---

## 13. Survey of the remaining content-kind ids (2026-10-08) — and why `.czn_pc` is not where street pavement comes from

**Scope:** the other 18 top-level zone-content-kind ids beyond Objects (`0x2234`, §11) and Navmesh (`0x2235`, §12) — all independently re-disassembled this pass, not re-citing the old scoping note. The id→handler table is independently re-confirmed (18/18 reads, plus a clean arithmetic self-check on the 22-entry table's own per-slot byte spacing — `0x0c` bytes × slots skipped, 15/15).

### 13.1 The "Mesh+Material+AABB" family is three private siblings, not one shared reader (corrects a same-day killed-attempt's own lead)

`0x2237` (WE geom, bespoke inline, no magic, `0x70`-byte mesh-ref stride — already documented, `spec-terrain-format.md` §5 item 2), `0x2240` (Light clip meshes, delegates to `0x0074d260`, magic `0xf117ab0b`, version 1-2, `0x50`-byte stride) and `0x2251` (Rain killer meshes, delegates to `0x007512f0`, magic `0x05adbeef`, version must `==1`, `0x44`-byte stride) are three **independently-called** readers built from the same five low-level primitives (`0xe718b0` mesh-ref, `0xe401d0` index-list, `0xe71090` material, `0xe5e240`/`0xe5e220` material-allocate, `0xe3fcb0`). **Exhaustive whole-`.text` xref census: `0x0074d260` and `0x007512f0` each have exactly ONE caller in the entire binary** (their own id's handler) — **zero** call sites to either, or to any of the five primitives, exist inside any of the other 15 remaining handler bodies. The family does not extend beyond these three; the id space is now closed for this question.

### 13.2 Traffic / Sidewalks / Traffic signals (`0x2236`/`0x2238`/`0x2241`) — one shared dispatcher, three unrelated gameplay subsystems, none of them geometry

One shared function, `0x00863c80`, checks all three ids in sequence. Traffic: gate `0x00a69720()` (tests singleton `0x02705fe0`), loader `0x00a69570`, sets zone `+0x130` bit `0x40`. **Sidewalks: gate `0xbbdd90()` (tests a separate singleton, `0x02924d40`), loader `0xbbdcc0`, sets zone `+0x130` bit `0x80`.** Traffic signals: unconditional `0x00a6a6c0`, no gate. **Runtime consumer now confirmed for both graphs, 2026-10-08 (`spec-ai-runtime.md` §17.3) — previously "load-time only, no runtime consumer traced":** the Traffic singleton (`0x02705fe0`) is walked by the distant-vehicle population tick `0x00b93670` (road-spline candidate search + `Spline_Type` eligibility filter), and the Sidewalks singleton (`0x02924d40`) is walked by the distant-pedestrian population tick `0x00b90960` (path-graph node pick) — both gated on these exact same load-gate functions. Disassembled in full for Sidewalks specifically: its loader installs the section payload into the `0x2924d40` singleton and fixes up **four embedded sub-arrays** of small fixed-stride records (one, 36-byte stride, anchored relative to the *Traffic* singleton's own `+0x4c` field — a real, confirmed cross-link between the two sections) plus a spatial-index build (`0x00dc6190`). **This is a node-array-plus-spatial-index shape — a pedestrian AI path-graph mirroring the vehicle Traffic graph it shares code and a storage base with — not render geometry.** Zero calls to any of the five Mesh+Material+AABB primitives anywhere in the function or its callees. **This REFUTES "Sidewalks" as a pavement/render-geometry source and substantially weakens the idea of a dedicated decal/road-streaming asset family among the 22 named `.czn_pc` kinds** — the only plausible candidate for one turned out to be AI data, and the game's actual decal system (`decal_info.xtbl`) is confirmed elsewhere to be bullet/blast/crash damage decals only, unrelated.

**Sidewalks singleton `0x02924d40` substantially extended, 2026-10-09 (`spec-ai-runtime.md` §20.2,
wave 6 item 2):** 121 real uses across 50 functions were found and read (previously only the loader
itself had been opened). The "four embedded sub-arrays" above are refined: `+0x00`/`+0x04` is a
**24-byte-stride SEGMENT array** (two `u16` node-index fields per record, with dedicated pointer↔index
helpers) — distinct from the already-cited 36-byte-stride Traffic-cross-linked array (`+0x18`/`+0x1c`);
`+0x10`/`+0x14` is the already-documented node-count array; `+0x08`/`+0x0c` remains uncharacterized. A
previously entirely unread ~28-function query/steering-support cluster (`0x00bbdc30`-`0x00bc0970`) was
opened in full — segment acceptance tests, bounding-box/direction-vector resolution, and the real
multi-destination path query feeding both the near-field pedestrian spawn chain and the per-frame
sidewalk/wander movement tick. Full detail: `spec-ai-runtime.md` §20.1/§20.2.

**Traffic (`0x2236`)'s own payload format, previously undisassembled, is now CONFIRMED, 2026-10-08 — a
dedicated road-network topology graph, distinct from `0x2237`'s render geometry and from the XTBL
`Spline_Type` one-hot bitmask (`spec-tables-traffic-ai.md` §14.4), which shares no code with either.**
The loader `0x00a69570` (full body, 139 instructions) unconditionally stores its own record-pointer
argument into the gate's singleton (`0x02705fe0`), then runs a strict, repeated self-relative-pointer
fixup over **14 independent `{count:s32, selfRelativePointer:s32}` sub-array descriptor pairs** packed
into a `0x70`-byte header (an exact arithmetic closure — 14 × 8 bytes — not estimated): generic fixups at
pairs 1/2/3/5/6/10/11/12/13 (offsets `+0x00`/`+0x08`/`+0x10`/`+0x18`/`+0x20`/`+0x50`/`+0x58`/`+0x60`/
`+0x68`, each `+4` its own pointer), pair #14 (`+0x48`/`+0x4c`, the exact field this section already cites
as the Sidewalks loader's own cross-link anchor — an independent byte-level confirmation from the
Traffic side), and three structurally special pairs:
- **Pair #7 (`+0x30`/`+0x34`) is a genuine segment→point cross-reference graph**: 24-byte-stride segment
  records, each resolving its own `+4` field either to null (sentinel `-1`) or to a self-relative offset
  into pair #8's array (`+0x38`/`+0x3c`) — N road segments, each pointing into a shared point pool.
- **Pair #9 (`+0x40`/`+0x44`) becomes a persistent global "active traffic array" pool descriptor**
  (`0x028921d0`, re-anchored at the newly-loaded record's own pair-#9 array on every zone load) —
  install/reset via `0x00b1a670`/`0x00b1a650`, with push_back/pop-swap-remove/remove-by-index methods
  (`0x00b1a690`/`0x00b1a6e0`/`0x00b1a6c0`). This is a free-list/pool-of-handles container, most plausibly
  a per-zone pool of ambient-vehicle/traffic-slot handles (matching the already-documented distant-vehicle
  spawn theme, `spec-tables-traffic-ai.md` §14) — **not** a lane-iteration cursor, despite that being the
  natural first guess.
- **Pair #1 (`+0x00`/`+0x04`) is a 64-byte-stride, index-parallel "extended" per-segment data block** —
  confirmed index-parallel with pair #7's segments three independent ways (pointer-to-index recovery via
  signed division-by-24, index→address via `base + index*64`, and a last-element boundary check), via a
  separate consumer `0x00b1c5a0`. Each 64-byte extended record carries: a 2-bit "segment kind" flag at
  bits 28–29 of `+0x1c` (extractor `0x00a69af0`; at least one consumer branches away from its "normal"
  path when non-zero, consistent with distinguishing ordinary lane segments from junction/intersection
  segments); a CSR-style cumulative point-count at `+0x3e` driving a variable-length per-segment 3-D
  point-list copy (via a generic count/get-Nth-position accessor pair, `0x007d1040`/`0x007d3830`, object
  class not identified); and a separate compact **quantized anchor+int16-offset position codec**
  (`0x00a6a442`, full body, 86 instructions, pure SSE/x87, no calls) that decodes two real 3-D points per
  record from one `float3` anchor (indexed by the record's own `+0x00` u16) plus two signed-16 triples at
  `+0x24`/`+0x2a`, each axis computed as `anchor.axis + s16value * K` (`K` = a fixed quantization-scale
  double at `0x0115a220`, value not decoded) — most plausibly a segment's start/end or left/right
  lane-boundary points.

Registration follows the same shared primitive this section already names for `0x2245`/`0x2254`
(`0x00dc6190`), confirming the "spatial-index build" framing from the Traffic side too, not just
Sidewalks'. **[CONFIRMED — disassembly, all 14 pairs and all three special-pair mechanisms read
instruction-for-instruction.] OPEN:** the exact steering/target-point computation and the junction
branch-choice algorithm (neither function reads a vehicle position/heading, so the consumer code that
does was not reached); the literal write-site inside the traffic module for the Havok driver-input fields
(`spec-physics-runtime.md` §10.6); fields `+0x00`/`+0x08`-`+0x17` of each 24-byte segment entry; the
`0x00a69330` tail-jump target; the `0x0115a220` quantization constant's value; whether `0x007d1040`/
`0x007d3830`'s object is really pair #8's point pool or a shared generic point-list class used elsewhere.
Full runtime-consumer detail (pool mechanics, the two position codecs' likely roles, the Havok
integration argument): `spec-ai-runtime.md` §18. Full disassembly: `scratchpad/traffic_ai_lane_following_2026-10-08.md`.

**Net answer to "where does street/sidewalk pavement live" (a standing open question, paired with the `~al` question below) — RESOLVED 2026-10-08, against real shipped bytes.** `0x2237` ("WE geom") genuinely carries road/sidewalk/pavement-shaped geometry in a large real subset of the shipped population — not merely a road-described-but-unexercised section. Method: for each of 2,862 real `.czn_pc` files carrying a `0x2237` record, scanned that record's own declared body span for its embedded `.tga` texture-name pool; 374 files (13%) contain road/sidewalk/pavement-flavoured names (e.g. `roads_crosswalk_01_d_co.tga`, `sidewalk_01_d_co.tga`, `asphalt_lot_01_d_co.tga`). Decoding each such record's own validated Mesh sub-block channels (position `float3` at channel offset 0, per-channel stride by the sem/idx law, `spec-vertex-format.md` §5/§6.1/§12.3 — **not** the block's primary/index buffer, a real methodology trap this pass found and fixed, see below) gives real vertex positions for all 374/374, and **359/374 (96%)** are flat and horizontal by a conservative test (≥3 m horizontal footprint, ≥4:1 horizontal:vertical ratio) — e.g. tile `sr3_city~s1312`'s `roads_crosswalk_01_d_co.tga`-pool record decodes to a real 31 m × 140 m slab 0.55 m thick at a constant ~12 m height (a second independent proof case, `sr3_city~s1017`, gives a 31.5 m × 3.6 m × 23.3 m block). The prior single rendered sample (vertex layout code 24, building-wall-shaped) and this pass's road-textured population (layout codes 2/4) are different individual Mesh-block channels within the same general mechanism, not a contradiction — `0x2237` carries both building-shell and street-surface geometry depending on which real record/channel is sampled. **Hypothesis (b) (`.clmesh_pc` Level-Mesh) was checked directly on the same real tiles and found to carry only street furniture** (142 distinct props across 7 tiles' fine cells, zero road-named, only small curb/barrier-scale objects like `walls_cmntpad_01`/`rdblock_b` pass even the same lenient flat-shape test, none remotely tile-scale) — consistent with, not competing against, `0x2237` being the real pavement source: `.clmesh_pc` here is street furniture, not street surface. One nuance: `~al` tiles pass at a real but visibly lower flat:vertical ratio (5–20:1 vs. 50–250:1 for ordinary tiles), consistent with — not contradicting — this project's own `~al`/always-resident-rooftop finding above: an `~al` tile's `0x2237` record most plausibly mixes real ground-level pavement with some taller always-resident geometry in the same aggregate. **A real methodology bug was found and fixed en route, worth recording:** the Mesh sub-block's own "primary buffer" is the index buffer, not the vertex buffer — real per-vertex positions live in the block's own CHANNEL array instead (caught by the known-real-house-mesh control check: a real house reading as "no position data anywhere" is impossible, which is what flagged the bug before any negative result was trusted). **[CONFIRMED — empirical, population-wide, real shipped `.czn_pc`/`.gzn_pc` and `.clmesh_pc`/`.glmesh_pc` bytes.]** Full method, the methodology-bug writeup, and the complete per-tile numbers: `scratchpad/pavement_0x2237_real_sample_2026-10-08.md`.

**Independent confirmation, 2026-10-08, from a live-playtest ground-mesh-origin bug investigation:**
the position pipeline this paragraph establishes (raw baked world-space `float3` vertex data, no
runtime transform) is now independently confirmed end-to-end from the OWNING HANDLER's own
disassembly, not just inferred from decode results — `0x00864c60`'s per-mesh-reference loop applies
zero translation/rotation/scale to the actual rendered geometry anywhere in its body (full detail,
`spec-terrain-format.md` §5 item 2). That investigation also re-confirmed, directly on 9 real tiles,
that this content's positions are baked exactly as shipped — a per-tile origin BUG, where one exists
(CORRECTED 2026-10-08: reproduced cleanly for 3 of 4 reported tiles once a block-location
undercounting bug in the investigating pass itself was found and fixed; the 4th remains an
unreconciled exception — `spec-terrain-format.md` §5 item 2's own correction entry has the full
detail), is not explained by any missing runtime offset computation in this handler. **Whether the
origin-collapsed blocks are genuinely unused "garbage" geometry (HYPOTHESIS only, not a conclusion —
retail renders these streets correctly, which argues against it) or are referenced/transformed by
something outside this handler entirely is still being checked — see `spec-terrain-format.md` §5
item 2's own correction entry for the open questions.**

**Update, 2026-10-08 (same investigation, resumed a second time, two static questions chased):**
(1) no external transform source was found for these specific blocks — the Objects (`0x2234`)
content kind, the only already-documented handle-based transform carrier, is absent from every
`~s<CCRR>.czn_pc` file, and the sibling `~f<CCRR>.czn_pc`'s own `0x2237` record is an empty,
record-free stub in every tile checked (not exhaustive: 9 other content-kind ids in the same file
were not individually checked for a numeric index into `0x2237`'s array); (2) no structural marker
(flags, stride, channel shape, length ratio, ordering) distinguishes the origin-collapsed blocks
from correctly-placed ones in the same file — CONFIRMED CLEAN NEGATIVE on every field checked, all
9 tiles, and the near-origin content turns out to be a normal, ubiquitous, thoroughly-interleaved
feature of every tile sampled (including all 5 "good" comparison tiles), not an anomaly confined to
the 4 reported-broken ones. **Net: leans toward the near-origin content being genuinely
local-space-authored (most plausibly reusable decal/kerb/street-furniture templates) rather than
"garbage," but this is explicitly NOT proof** — a live draw-path capture remains the decisive test,
left to the orchestrator/user to dispatch. Full detail: `spec-terrain-format.md` §5 item 2;
`scratchpad/terrain_ground_mesh_tile_origin_2026-10-08.md` ("SECOND ADDENDUM" section).

**Newly found 2026-10-08, directly verified while cross-checking Team B's own independent negative result: `~f<CCRR>` and `~s<CCRR>` name TWO SEPARATE REAL FILES per tile coordinate, not alternate names for the same content — and only `~s` carries the `.gzn_pc` Mesh companion.** Opened the real nested container for coordinate `1312` (`sr3_city_1.vpp_pc` → `1312.str2_pc`) directly: it holds both `sr3_city~s1312.czh_pc`/`.czn_pc`/`.gzn_pc` (all three) AND a separate `sr3_city~f1312.czh_pc`/`.czn_pc` (only two — **no `.gzn_pc` at all**). Every road-textured tile this section's own population scan found (§13.2's `sr3_city~s1312`/`~s1017`/`~s1217`/`~s1914`/etc.) uses the `~s` family. **A consumer that only ever looks up the `~f<CCRR>` family will find zero ground-system Mesh data for every tile, everywhere — not because the geometry is missing from the archive, but because that specific file variant never carries a `.gzn_pc` companion.** Checked for exactly one coordinate so far, not population-verified; consistent with, and likely the same general residency/variant-tier mechanism as, the already-documented `~al` ("Always Loaded") suffix above — but `~f` vs `~s` is a different letter/meaning, not re-derived from `SR3Z +0x1E`'s type-field table this pass. **[CONFIRMED for the one coordinate checked — directly verified container contents, `tools/harnesses/scan_meshes.py`+`vpp_modea.py`. OPEN: whether this generalizes population-wide, and what `~f`'s own type/container-kind value and real purpose are — flagged for a future pass, not chased further here.]**

**The `~al` ("Level Always Loaded") zone-entry question is unrelated to pavement, now CONFIRMED.** `~al` (§10.5's own padding finding) is the on-disk marker for container kind `0x1B`, the engine's one global, distance-independent streaming tier (`spec-world-streaming.md` §10.7/§11.1) — not an LOD rung, not a lighting proxy, not a literal "all objects" aggregate. Its real, format-identical `.czn_pc`/`.gzn_pc` content is dispatched through the same per-id handlers as any ordinary tile. The one render sample available (rooftop/window-grid geometry) is consistent with this: `~al` zones most plausibly carry the always-resident distant building-silhouette/rooftop detail that must never pop in/out with camera distance — the opposite end of a building's vertical extent from street level, and a poor fit for pavement (which only matters at close range, where the already-active distance-gated tiers would carry it instead). HIGH CONFIDENCE, not independently re-verified that `~al` content specifically and exclusively routes through the `0x2237` handler.

### 13.3 The other 14 ids, surveyed (not full field census — depth varies, every gap flagged OPEN honestly)

- **`0x2233` Crunched ref geom** — own bespoke `0x70`-byte-stride outer array (via `0x00862dd0`, NOT the Mesh+Material+AABB family despite the same stride number) plus a nested 12-byte instance list that re-indexes it and computes a derived AABB.
- **`0x2242` Mover constraints** — a 3-stage chained sub-block (`0x00777e10`→`0x00776360`→`0x007762e0`), each stage found via an offset inside the previous stage's own record.
- **`0x2243` Interior triggers** — independently re-confirms the prior scoping note exactly: `0x90` (144)-byte stride, per-entry `0x00dcd3d0`; the `triggers.xtbl` link (`spec-tables-vehicle-world.md` §21.1) stays OPEN.
- **`0x2244` Heightmap** — stores the raw record pointer with NO inline decode at all; real payload format deferred entirely downstream, mirroring Navmesh's own register-now/parse-later pattern (§12.3). **Flagged 2026-10-08, from the ground-mesh-origin investigation (`spec-terrain-format.md` §5 item 2):** `0x2237` ("WE geom") shows no defect for 2 of the 4 reported-broken tiles (0914/0715) — this undecoded carrier is the single most promising remaining candidate for "the base walkable terrain surface" as distinct from `0x2237`'s road/sidewalk decal overlay, if those two tiles' symptom needs a different carrier to explain it. (Decode of this content kind is in progress as a separate top-priority item as of this note.)
- **`0x2245` c_object rbb tree** / **`0x2254` Audio Emitter rbb tree** — both call the same shared "register a named runtime-collection blob" primitive `0x00dc6190`; `0x2254` layers one extra per-zone-id registration call (`0x00562fa0`) `0x2245` lacks.
- **`0x2246` Undergrowth** — own magic `0xa52137c2`, version must `==2`, a two-level nested grouped array, global registration into a 256-slot table at `0x01301c44`.
- **`0x2247` Water volumes** — `{count,arrayPtr}` header, 5-field-per-entry pointer-fixup record; one field feeds the same `0x00dcd3d0` primitive Interior Triggers uses.
- **`0x2248` Wave killers** — minimal `{count,arrayPtr}`, zero inline processing, same lazy-materialization pattern as Heightmap.
- **`0x2249` Water surfaces** — two-level array, outer 16-byte stride (subcount + fixup to an inner 20-byte-stride array).

**Water-vs-missing-ground hypothesis, tested 2026-10-11 — orchestrator-routed static pass on the
long-parked ground-missing live-playtest bug, CLEAN NEGATIVE as a tile-classification signal**
(SPEC TEAM, self, direct real-byte read against the real shipped `sr3_city_0.vpp_pc`/
`sr3_city_1.vpp_pc`/`sr3_city_missions.vpp_pc` archives via `tools/harnesses/czn_u2_replay.py`'s
own `collect_pairs`/top-level-tag-walk primitives, extended to census `0x2247`/`0x2249`
presence+length per tile; the `.czn_pc` interior hold was lifted by the user 2026-10-07, see
HANDOFF §27.H-0c — raw bytes read directly, no agent dispatch needed). Hypothesis: the 4
reported-broken tiles (`0914`/`0915`/`0715`/`0716`) might be coastal/water tiles whose missing
"ground" is actually a missing WATER surface. **Real per-tile `~f<NNNN>.czn_pc` census, both
groups:**

| tile | class | has `0x2247` (water volume) | has `0x2249` (water surface) |
|---|---|---|---|
| `0914` | broken | **no** | **no** |
| `0915` | broken | yes (5768 B) | no |
| `0715` | broken | yes (4000 B) | yes (14948 B) |
| `0716` | broken | yes (5412 B) | yes (17328 B) |
| `0916` | good | yes (12836 B) | no |
| `0818` | good | yes (3824 B) | yes (11868 B) |
| `0912` | good | yes (4180 B) | yes (17828 B) |
| `1015` | good | **no** | **no** |
| `1016` | good | yes (2000 B) | yes (6948 B) |

**Both the "broken" and "good" groups span the full range** (neither, water-volume-only, or
both) — `0914` (broken) has neither water content kind, exactly like `1015` (good); `0715`/`0716`
(broken) have both, exactly like `0818`/`0912`/`1016` (good). **This is a clean negative: per-tile
water-volume/water-surface record presence does NOT correlate with the known broken/good split**
— it does not explain why these 4 specific tiles show the void. This does not fully rule out a
water-surface explanation (these two content kinds may represent local decorative water features
— pools/canals/ponds — rather than the main sea, and the player's own report of a void near the
default spawn at (-1280,-560) involves tile `0915`, whose own water-volume record is present but
small; also see `spec-player-runtime.md` §6.9-area's `0x0079a420` finding below, which found water
clamping is driven by a single GLOBAL bounding box, not per-tile records at all, so a per-tile
census was never guaranteed to be decisive either way) — but it closes this specific, cheap
empirical test as a non-explanation. Script: `scratchpad/water_check_2026-10-11_run.py`.

**`0x0079a420` opened (orchestrator-routed, same pass) — the player-camera "surface clamp"
previously flagged only as `HYPOTHESIS: water surface` (`spec-player-runtime.md` §6.9-area) is
CONFIRMED to be a global-box-gated downward raycast, not a water-specific or per-tile
mechanism.** Full body opened (`0x0079a420`-`0x0079a65b`), independently re-verified against the
live Ghidra server before splicing. It first classifies the X/Y position against a single
GLOBAL rectangular bounding box (4 zero-fill runtime floats at `0x0223a7cc`-`0x0223a7d8` — no
static initializer, no direct writer found in this pass, so the box's real in-session extent is
genuinely unknown without either locating an indirect writer or a live capture) via `0x00797500`.
Only when the position classifies as "near/inside" that box (return value 2 or 3) does it probe a
downward ray from the position to 100 units below it, via the raycast primitive `0x0075dfc0`, and
return the hit surface's position/height; classified as 0 or 1 (further outside), it returns a
fixed default instead of raycasting at all. **This is a generic anti-clip/surface-snap mechanism
gated by one global region, not a per-tile or per-water-volume lookup** — consistent with being
the single ocean/sea-level boundary for the whole map (HIGH CONFIDENCE, not proven: the box's
real coordinates were not recovered), but equally consistent with a more general "stay above the
nearest collidable surface" safety net unrelated to water specifically. **Net for the
ground-missing bug: if the default spawn at (-1280,-560) falls inside this global box, this
clamp would raise the CAMERA (not the player) above whatever the downward raycast hits — if
nothing is hit (e.g. no collision geometry present, consistent with the known-sparse `0x2237`
content at tile `0915`), the clamp simply does nothing, which is consistent with, but does not
independently prove, the void being an absence of collidable surface rather than a missing water
plane specifically.** Whether (-1280,-560) actually falls inside the global box remains OPEN
(needs the box's real runtime value, which needs either its writer traced or a live capture).
- **`0x2250` Parking** — **the one genuine structural outlier, now deep-dived (§13.4):** not a byte-format record at all, but a thin generic-property wrapper around a raw embedded Havok 2010.2.0-r1 physics packfile — one `hkpBoxShape` per ambient parking space, MOPP-accelerated. Prop ids `0x13`/`7` are generic base-class bookkeeping (self-handle/dispatch-context, NOT the CRC-32 name-hash convention §9.3/§11.7 use), correctly not forced through that dictionary attack.
- **`0x2252` Supplemental LOD** — own magic `0xa58137c8` (version `==2`), calls `0x00e40210` — a distinct address from the family's own `0xe401d0`, deliberately not conflated despite the resemblance.
- **`0x2253` Fading grid data** — handler tail-jumps (not calls) into `0x00854ce0`; no magic; pure self-relative-pointer-fixup grid/tree shape, `0x28`-byte-stride records each with 3 fixup fields.

**Net verdict:** the "is it the same reader" question is now closed for the whole 22-entry id space (§13.1). Field-level semantics for most of the 14 ids above remain OPEN by design (survey depth, not full census) — Parking (`0x2250`, deep-dived below, §13.4) and Undergrowth (`0x2246`) were flagged as the most structurally interesting candidates for a future deeper pass. Full addresses, exact instruction sequences, and the negative-control xref census are preserved in `scratchpad/czn_remaining_contentkinds_survey_2026-10-08.md` and `scratchpad/pavement_source_and_al_suffix_2026-10-08.md`.

### 13.4 `0x2250` Parking, deep-dive: a wrapped Havok packfile, RTTI-confirmed class `parking_spot` (2026-10-08)

**Full on-disk payload format, CONFIRMED (178/178 population validation, zero exceptions):** after the standard 12-byte chain header (§11.1 convention), the payload is `u32 innerLength`, zero-padding up to the next absolute 16-byte-aligned offset (a well-known, public Havok object-alignment convention, not from any leaked source), then `innerLength` bytes of a **raw embedded Havok 2010.2.0-r1 packfile** (public magic `0x57E0E057`/`0x10C0C010`). Validated via a compound, non-heuristic check (the 4 possible padding values 0/4/8/12 correctly predict the magic's exact byte offset in every one of 178/2,971 real files — the only files, out of the full population, that carry a `0x2250` entry at all, 6.0%, never more than one per file). `innerLength` ranges 928–12,128 bytes across 47 distinct values.

**The embedded Havok content is homogeneous across the entire population.** 7 samples spanning the full size range all decode the same 3-section minimal tagfile (`__classnames__`/`__types__`/`__data__`) and the identical 5-class set: `hkpMoppBvTreeShape`, `hkpMoppCode`, `hkpListShape`, `hkpTransformShape`, `hkpBoxShape` — a textbook `hkpListShape` of per-instance-transformed `hkpBoxShape`s (one box per physical parking space) wrapped in a MOPP acceleration tree. Class names are public Havok Physics (`hkp`-namespace) nomenclature, used here for naming only, read directly from the shipped data. 91 distinct exact (SHA-1-compared) blob contents across the 178 records; 42 are reused byte-for-byte by 2–4 files each, every group an already-known mission-variant sibling set (e.g. `sr3_city~f1012.czn_pc`/`^burn`/`^daedelus`/`^lockdown`) — a clean sanity confirmation, not a new mechanism. **OPEN:** every one of the 178 records has unexplained "leftover" bytes past the parsed Havok blob, within the chain entry's own declared length (not a second Havok-magic blob) — not chased to a conclusion.

**The constructed object's real engine class, CONFIRMED by direct RTTI mangled-name match in `.rdata`: `parking_spot`** (`.?AVparking_spot@@`, plus corroborating `bitfield_table<parking_spot_flags>` and `concrete_instantiator<parking_spot>` instantiation strings) — the engine's own name for this content kind matches the survey label exactly. Constructed via `0x00757c00` (shared base constructor, vtable `0x01153898`, `0x220`-byte allocation), fed by Parking's own delegate `0x00862ae0`.

**Property ids `0x13`/`7`, resolved (correctly NOT via the CRC-32 dictionary attack §9.3/§11.7 use elsewhere — REFUTES the premise that every typed-property-id-plus-value call in this project shares one mechanism):** `0x00c21b20` is a different, simpler mechanism — a per-object growable array of 16-byte `{id, pad, valueLo, valueHi}` slots, linear-searched by plain small integer id, not a hash table. Id `8` = universal self-handle (set generically in the shared constructor, before any Parking-specific code runs). Id `0x13` (19) = hardcoded literal `1` for every real record (not file-driven at all). Id `7` = a `u16` sourced from a fixed offset inside the per-content-kind dispatch context structure (the same structure whose neighboring field the Traffic/Sidewalks dispatcher, §13.2, already uses for load-status bits) — not from the record's own bytes or the Havok blob; exact semantic meaning OPEN. HIGH CONFIDENCE both ids belong to generic base-class bookkeeping shared by other physics-shape-attached object types, not `parking_spot`-private fields.

**The real, richly-named `parking_spot` fields exist in the executable but are untouched by this construction path** — found by direct string search: `parking_spot_max`, `_spawn_pct`, `_forced_vehicle`, `_forced_variant`, `_forced_vehicle_resource_uid`, `_mopp_key`, `_bbox`, `_flags`, `_enable`, `_disable_all`, `_set_max`, `_reset_max`. Three of these were already independently documented from the Lua-API side (`spec-lua-api-behaviour.md` §18.29/§49): `parking_spot_disable_all`/`_set_max`/`_reset_max` manipulate a **separate, global, city-wide byte array** (base `0x02609394`) — a city-wide ambient-vehicle spawn-budget/eligibility layer, independently Lua-tunable, distinct from the per-zone placement geometry this section documents. **Correction, 2026-10-08 (`spec-ai-runtime.md` §17.6): this array is 8 slots (indices 0–7), not "capped 0–18" as this section previously claimed** — confirmed directly from the query/set accessor functions (`0x00905dc0`/`0x00905ea0`/`0x00905ec0`), which also show `0x02609394` is one of **6** sibling 8-slot arrays sharing the same registration primitive (`"os_parking"`, alongside `"os_distant_vehicle"`/`"os_distance_ped"`/`"os_boat"`/`"os_vehicle"`/`"os_ped"`) — see `spec-ai-runtime.md` §17.6 for the full table and runtime trace. Cross-confirmed by two more existing specs pointing at the same overall reading: `spec-tables-progression.md`'s global flags byte `+0x43` bit 3 ("disable parking spawns") and `spec-vehicle-data.md`'s per-vehicle `ParkingSpawn` eligibility flag. **Net conclusion: the content kind represents ambient/decorative parked-vehicle spawn-point placement geometry, not AI parking-maneuver/pathing (no link to the already-documented dead file-backed navmesh, §12.7/`spec-ai-runtime.md` §14) and not player-garage data (`spec-save-format.md`'s garage/crib systems are unrelated).**

**[CONFIRMED — disassembly + RTTI decode + full-population empirical validation (178/178). HIGH CONFIDENCE — the generic-bookkeeping framing for ids `7`/`0x13`/`8`. OPEN — the exact semantic name of ids `7`/`0x13`; the "leftover" bytes; where the richly-named tuning fields get their per-spot runtime values.]**

**Evidence anchors:** `0x00862ae0` (Parking delegate), `0x0075fa80`/`0x00c960c0` (generic sub-blob open/validate), `0x00757c00`/`0x01153898` (shared constructor/vtable), `0x00c21b20` (property array), `0x00c1f540`/`0x00c2b0d0` (generic defaults staging), RTTI strings `.?AVparking_spot@@` etc. Full detail: `scratchpad/czn_parking_0x2250_deepdive_2026-10-08.md`.

**Review status (2026-10-08): CONFIRMED for the payload format, Havok content, and class identity (full-population + RTTI); HIGH CONFIDENCE/OPEN as noted above for the two property ids' exact semantics.**

### 13.5 `0x2244` Heightmap, deep-dive: real per-tile elevation data, a dedicated 1,008-slot registry, and a previously undocumented runtime grid sampler (2026-10-08)

**Dispatched top-priority by `purrsian-04`, in direct response to a live-playtest "ground missing" report, to confirm or refute the theory that this entirely-undecoded content kind is the real base terrain surface, distinct from `0x2237`'s decal/road-surface overlay (§13.2).** This supersedes the one-line survey entry in §13.3 above (left in place, not deleted, per this project's annotate-don't-delete convention).

**Handler, read in full for the first time (`0x00864200`, 157 bytes/62 instructions):** on a tag match, stores the record's absolute body pointer unconditionally into `zoneContext+0x138` — no inline decode of any kind, confirming §13.3's own one-line survey entry at the instruction level. A zoneId "category" test (`0x0085be60`: `(zoneId & 0xE000) == 0`) gates one further step: for category-0 zones whose own name string contains `^` (the already-documented mission-variant separator, `0x00ea5af0`, a `strchr`-style SWAR scanner), the real downstream registration call is skipped entirely; every other zone registers unconditionally. **[CONFIRMED — disassembly, full function body.]**

**Downstream registration is a dedicated, independently-implemented 1,008-slot open hash table (`0x028a0248`), NOT a reuse of Navmesh's own fixed-array registry (§12.3) despite sharing the same high-level "register now, parse later" pattern** — corrects §13.3's "mirroring Navmesh's own... pattern" framing from "same mechanism" to "same pattern, different mechanism." Keyed by the zone's own `u16 zoneId` (generic hash-mix `0x00dab2b0`, `0x3f0`=1,008 buckets); each slot holds a 20-byte value `{originX, originZ, gridW, gridH, payloadPtr, spacing}`, confirmed three independent ways (the constructor's own zero-fill, the insert path's own write, and the "already registered" fast-path's own read-back all agree byte-for-byte on this layout). `spacing` is assigned `category0 ? 40 : 10` at registration time (not stored on disk) — reconciled below against the real on-disk grid sizes. **[CONFIRMED — disassembly, every address/offset read directly, cross-confirmed three ways.]**

**On-disk record format (12-byte chain header, §1's Finding-0 correction, + a 16-byte mini-header + a flat height array), population-validated 1,194/1,194, zero exceptions:**

| offset (from record body) | field | type | population result |
|---|---|---|---|
| `+0x00` | origin X | `i32`, world-space metres | varies per tile — see below |
| `+0x04` | origin Z | `i32`, world-space metres | varies per tile |
| `+0x08` | grid width | `u16` | 32 (597/1,194) or 8 (597/1,194) |
| `+0x0a` | unused by every reader traced | `u16` | 0 in all 1,194, zero exceptions |
| `+0x0c` | grid height | `u16` | 28 (597/1,194) or 7 (597/1,194) |
| `+0x0e` | unused by every reader traced | `u16` | 0 in all 1,194, zero exceptions |
| `+0x10`.. | height samples | `u16[width×height]`, row-major (width fast-varying), little-endian | length always exactly `16 + width×height×2`, zero slack, all 1,194 records |

**Origin: CONFIRMED (1,194/1,194, exact zero-error match) to be the tile's own low-X/low-Z corner**, `origin_x = centre_x − 160`, `origin_z = centre_z − 140` against the already-documented tile-centre formula (`spec-world-streaming.md` §10.6(g)) — a second, independent empirical confirmation source for that formula. Re-verified directly on the same 9 tiles today's sibling ground-mesh-origin investigation used (§13.2 above): every one of the 4 reported-broken tiles (0915/0914/0715/0716) AND all 5 reported-good tiles (0916/0818/0912/1015/1016) carries an exact match, **zero difference between the two groups.**

**Fine vs. coarse grid: CONFIRMED discriminator is the `~al` suffix (container kind `0x1B`, "Level Always Loaded," §13.2), NOT mission-variant (`^`) naming — a competing hypothesis tested directly and refuted (both groups split evenly, 468 base-named/129 caret-named, in either grid-size bucket).** Ordinary tiles: 32×28 @ 10 m (320/32, 280/28 exact). `~al` companions: 8×7 @ 40 m (a clean 4× downsample per axis). This reconciles with the registration-time `spacing` assignment above as a necessary (HIGH CONFIDENCE, not directly observed — zoneIds are assigned at load time, not stored on disk) internal-consistency inference: `~al` zones ⇒ category-0 ⇒ spacing 40; ordinary tiles ⇒ not-category-0 ⇒ spacing 10.

**Height-value encoding:** raw `u16`/`i16`, no inline scale or bias field in the mini-header (`+0x0a`/`+0x0e` uniformly zero). Global range across all records checked: −5..+1,498. **HIGH CONFIDENCE, not independently confirmed: raw value = elevation in whole metres, no transform** — no independent ground-truth elevation source (e.g. a placed object's own Y-position, §11) was cross-checked against it this pass. **OPEN.**

**Material/blend-layer data: CONFIRMED ABSENT inside this record** — the body past the 16-byte mini-header is consumed exactly, byte-for-byte, by the flat height array in every one of 1,194 records, zero slack. If a rendered heightmap surface is ever textured, that texturing is not carried by `0x2244` itself; whether it borrows `0x2237`'s own per-zone `.tga` pool (§13.2) was not searched for — **OPEN, not asserted.**

**A previously entirely undocumented runtime query chain was found and traced, proving this is live, actively-used, `/GS`-hardened code — not dead/vestigial data:** `0x00861aa0` resolves a 3-D world position to its containing zoneId; `0x00b41260` checks whether that zone has registered heightmap data; and — the centerpiece — **`0x00b412c0`, the real grid-cell sampler**, converts a raw world `(x,z)` into a bounds-checked grid `(col,row)`:
```
col = (int)( ((double)position.x − (double)originX) / (double)spacing )
row = (int)( ((double)position.z − (double)originZ) / (double)spacing )
fail if col > gridW or row > gridH   (boundary-inclusive: col==gridW/row==gridH accepted)
```
This directly confirms, from the real consumer's own arithmetic rather than file-side inference alone: `spacing` really is a literal metres-per-cell divisor; the row-major width-vs-X/height-vs-Z axis assignment; and the origin-as-corner reading. A sibling function (`0x00b41370`) computes a bounds-checked neighbour cell via an 8-direction offset table — HIGH CONFIDENCE support for bilinear interpolation or a normal-vector computation, not independently established. **[CONFIRMED — disassembly, full function bodies, every field offset and the division formula read directly.]**

~~**The one genuinely missing link, stated honestly: neither `0x00b412c0` nor `0x00b41370` ever dereferences the payload pointer to read an actual height sample** — both resolve a `(col,row)` index and return a bool/neighbour-index. The literal `payloadPtr + (row×gridW+col)×2` read, and whatever final scale/bias is applied, lives in an unlocated caller of `0x00b412c0` (no xref was run on it this pass — a well-anchored next step).~~ **RESOLVED 2026-10-11 — the literal read is `0x00b41000`, CONFIRMED by disassembly, independently re-verified against the live Ghidra server before splicing (matched exactly).** Given the resolved-cell struct `0x00b412c0`/`0x00b41370` produce, it bounds-checks `(col,row)` against `gridW`/`gridH` (struct `+0x8`/`+0xa`) and reads the signed 16-bit height sample at `payloadPtr(+0xc) + (row×gridW+col)×2`, returning it cast directly to `float10` — **no scale or bias applied**, confirming from the consumer side the "raw value = whole metres" HIGH-CONFIDENCE reading above. Out-of-bounds returns a `-5.0` sentinel (`0x01131200`), which collides with the population's own observed global minimum of `-5` — a legitimate `-5` sample is indistinguishable from "out of bounds" to any caller. **New narrow OPEN item**, not material to the render question below.

**Whether this content kind is ever rendered — RESOLVED, as a negative: every traced caller is a non-rendering consumer; no render-geometry construction found in this call cluster.** A full caller trace on `0x00b412c0`/`0x00b41370` (6 direct callers, plus `0x00b41000`/`0x00b40fd0` and one further caller `0x00b44150`, plus one representative upstream consumer `0x00933760` — 9 functions read in full) finds every one built from the same "resolve cell → read/compare one scalar height value" primitive and nothing else: a single-point height query (`0x00b41670`); a 9-point max-height query whose one real caller (`0x00933760`) overwrites an EXISTING entity/decal transform's Y-position (a ground-conform/snap write-back, not geometry construction) (`0x00b41530`); a slope-capped grid-walkability/line-of-traversal test (`0x00b42d80`); and a full A*-style cost-search pathfinder using per-step height delta as a slope penalty (`0x00b44150`, via `0x00b443b0`/`0x00b43fc0`). **None of the 9 functions allocates or writes a vertex buffer, index buffer, mesh, or terrain patch, or touches any D3D9 object/vtable** — every one returns a bare scalar float or a bool/pathfinding handle. HIGH CONFIDENCE (not exhaustive — the rest of the binary outside this call cluster was not searched for an unrelated `0x2244` render path, OPEN as a bounded stretch goal): this content kind is a physics/AI-pathing elevation-query and cost-field API only, with no rendering consequence through this call chain.

**Net effect on the live-playtest "ground missing" bug: if `0x2244` is the intended base-terrain surface, no code path reached from its own runtime query chain ever builds the visible ground geometry.** Combined with the earlier "data is not corrupt" cross-check, the bug most likely lies either in a still-unfound separate renderer for this data (unsearched, OPEN) or in an entirely different content kind/mechanism — most plausibly the `0x2237` render-geometry pipeline (§13.2), or a render-state/culling bug unrelated to data decode. **Concrete next step for whoever picks this up:** search for an entirely separate `0x2244` consumer elsewhere in the binary (this pass's call-cluster trace, while thorough within that cluster, did not do a whole-binary search), or pivot to auditing `0x2237`'s own render path directly for the actual cause.

**Evidence anchors:** `0x00864200` (handler), `0x028a0248` (registry global), `0x00b417c0` (registration), `0x00b411f0`/`0x00b416d0` (hash GET/INSERT), `0x00861aa0`/`0x00b41260`/`0x00b412c0`/`0x00b41370` (runtime query chain), `0x00b41000` (literal height read), `0x00b40fd0` (cell-equality helper), `0x00b41530`/`0x00b41670`/`0x00b42d80`/`0x00b43fc0`/`0x00b443b0`/`0x00b44150` (callers), `0x00933760` (upstream ground-conform consumer). Full address index, the complete decode test vector (real `sr3_city~s0916.czn_pc` bytes, file offset `0x76588`), and 6 honestly-flagged open items: `scratchpad/heightmap_0x2244_decode_2026-10-08.md`.

**Review status (2026-10-11): CONFIRMED for the handler, registry mechanism, on-disk record format (population-wide), origin formula, the runtime sampler through the resolved grid index, the literal value read (no scale/bias), and that this content kind's own query chain has no rendering consequence. HIGH CONFIDENCE for height-value-equals-metres and the `~al`-spacing reconciliation. OPEN for the `-5.0` sentinel/legitimate-value ambiguity and for an exhaustive whole-binary search for any separate `0x2244` render path.**

### 13.6 `0x2237`'s own Havok collision shape — cross-reference, full chain lives in `spec-player-runtime.md` §5.6 (2026-10-11)

`0x2237` (already confirmed above and in §13.2 to carry street pavement's visible geometry) also
builds its own Havok collision shape directly from the same record's data — a third real instance
of the "`.czn_pc` content kind embeds a raw Havok shape with no `.clmesh_pc` involvement" pattern
§13.4 establishes for `0x2250`/Parking, confirmed by SHARED CODE: `0x2237`'s handler
(`0x00864c60`) reaches the identical generic physics-shape wrapper (`0x007a78a0`, classId
`0xc006c`, property id `7` via `0x00c21b20`) that `0x2250`'s own Parking delegate
(`0x00862ae0`→`0x00862bcb`→`0x00c16840`) uses, and the identical MOPP-wrap constructor
(`0x00d54300`, confirmed by direct xref to have exactly 4 callers project-wide) that
`.clmesh_pc`'s own collision path (§4.4.6 cross-ref, `spec-physics-format.md`) uses. Full
chain, address-by-address: `spec-player-runtime.md` §5.6's own "character proxy" sub-passage,
dated 2026-10-11 — not duplicated here to avoid two sources of truth for the same chain. This
closes the "which file supplies ground collision" open item that section tracked.

---

## Changelog

- **2026-10-11 — water-vs-missing-ground hypothesis tested for the long-parked ground-missing
  live-playtest bug: CLEAN NEGATIVE as a tile-classification signal** (SPEC TEAM, self, direct
  real-byte read against the real shipped city archives, no agent dispatch — orchestrator-routed).
  Census of `0x2247`/`0x2249` (Water volumes/Water surfaces) presence across the 4 known-broken
  tiles (`0914`/`0915`/`0715`/`0716`) and 5 known-good tiles (`0916`/`0818`/`0912`/`1015`/`1016`):
  both groups span the full range (neither/water-volume-only/both) — `0914` (broken) has neither,
  exactly like `1015` (good). Per-tile water-content presence does not correlate with the
  broken/good split, closing this specific empirical test as a non-explanation (does not fully
  rule out water as a factor via a different, non-per-tile mechanism — see the `0x0079a420`
  finding immediately below). Also resolved `spec-player-runtime.md` §6.9-area's standing
  `HYPOTHESIS: water surface` tag on `0x0079a420`: it's a generic global-box-gated downward
  raycast (box at `0x0223a7cc`-`0x0223a7d8`, zero-fill runtime, no writer found this pass), not a
  per-tile or water-specific lookup. Cleanroom self-check: strict regex 0 hits, C-cast-dereference
  check 0 hits.

- **2026-10-11 — new §13.6: `0x2237`'s own Havok collision shape, cross-referenced from
  `spec-player-runtime.md` §5.6's full chain** (SPEC TEAM, agent `a73e5e44581178516`,
  independently re-verified). Closes the "which file supplies ground collision" open item that
  section tracked: `0x2237`'s handler builds its own `hkpExtendedMeshShape`/`hkpMoppBvTreeShape`
  directly from the record's own data, registered via the same generic physics-shape wrapper
  §13.4 already documents for `0x2250`/Parking — shared code, not just an analogous pattern.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §13.5 RESOLVED: the `0x2244` heightmap's own query chain has NO rendering
  consequence — likely rules out "heightmap data missing" as the live-playtest ground-missing
  cause, narrows the search to `0x2237` or a render-state/culling bug** (SPEC TEAM, agent
  `a1f07c493367ef98b`, user-flagged top priority). Found the missing literal-value read
  (`0x00b41000`: bounds-checked, no scale/bias, direct `float10` cast) and traced every caller of
  the grid-cell samplers (9 functions total) — every single one is a scalar elevation-query or
  pathfinding-cost consumer (single/max-height queries, a ground-conform position write-back, a
  slope-capped walkability test, an A*-style cost-search pathfinder); NONE allocates or writes a
  vertex/index buffer, mesh, or touches any D3D9 object. Independently re-verified `0x00b41000`'s
  full disassembly against the live Ghidra server before splicing (matched exactly, including the
  `-5.0` sentinel value and its collision with the population's real global minimum — flagged as a
  narrow new OPEN item). Net: if `0x2244` is the intended base terrain, nothing reached from its
  own query chain ever builds the visible ground — the bug most likely lies in `0x2237`'s render
  pipeline or a render-state/culling issue, not in heightmap data/decode. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-10 — §11.7: `Trigger_Volume` added as a third resolved class-specific tail-property
  parser, mission-blocking (routed by orchestrator)** (SPEC TEAM, agent `a94b114a2ccb78105`). Real
  vtable `0x01170934`, slot `+0x04` (`0x0093e520`) is itself the complete parser, no further
  indirection. Notably this class's constructor writes only ONE vtable, not two — the
  general_mover/script_npc "two writes, take the second" heuristic does not hold universally and
  must be checked per class. Full property-by-property mapping kept at
  `spec-lua-api-behaviour.md` §1.14 (gameplay-semantics framing) to avoid duplicate maintenance.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — new §11.10: named-object resolution end-to-end, a consolidated cross-reference for
  Team B's mission-host Phase 2** (SPEC TEAM, agent `a6a74141c0fa8dbe5`, WAVE 8 item 1). A
  synthesis/consolidation pass, not new disassembly — assembles the already-CONFIRMED pieces of how
  a mission script's string name (`"m05_car_01"`, `"#PLAYER#"`, group/navpoint names) resolves to a
  live object into one navigable answer: on-disk property (§11.4/§11.7), live registration via
  registry vtable `+0x2c`/`0x00457650` (§11.8), the unified `0x004588f0` + `0x02cc9900`-bit lookup
  family (`spec-lua-api-behaviour.md` §4.13), the `"#PLAYER#"`-family sentinel short-circuit (never
  touches the name map), and groups/navpoints confirmed NOT separate mechanisms. Explicitly ruled
  out `spec-mission-packages.md` as an unrelated format. Full synthesis:
  `scratchpad/named_object_resolution_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — §13.2's Sidewalks singleton substantially extended with real runtime-consumer detail**
  (SPEC TEAM, wave 6 item 2, cross-reference from `spec-ai-runtime.md` §20.1/§20.2). 121 real uses across
  50 functions found and read; refines the "four embedded sub-arrays" to identify a 24-byte-stride
  segment array distinct from the already-cited 36-byte-stride one, plus a previously entirely unread
  ~28-function query/steering-support cluster feeding both a newly-found near-field pedestrian spawn
  chain and the per-frame sidewalk/wander movement tick. No new disassembly in this file. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §11.9 item 4 extended: class-registration mechanism confirmed for 43 real classes,
  cross-referencing `spec-player-runtime.md` §7.11's new kind-row-writer search** (SPEC TEAM, agent
  `a39496c2b1abfebed`). A whole-`.text` byte-shape scan found 43 registered classes total (up from the
  2 whose registration code was independently read before), including the real runtime
  `vehicle`/`player`/`npc` classes, confirming the `0x02cc9900[classIndex]` write mechanism directly
  for 9 fully-disassembled stubs. Full detail, including an exhaustive but unsuccessful search for the
  kind-row bit writer, lives in `spec-player-runtime.md` §7.11 (not duplicated here). Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §13.5: `0x2244` Heightmap fully decoded for the first time** (SPEC TEAM, agent
  `ab80684a4e2668c8e`, dispatched top-priority by `purrsian-04` in response to a live-playtest
  "ground missing" bug). Handler (`0x00864200`) read in full; a dedicated 1,008-slot hash registry
  (`0x028a0248`) found, distinct in implementation from Navmesh's own fixed-array registry despite
  sharing the same high-level pattern; on-disk record format decoded population-wide (1,194/1,194):
  a 16-byte mini-header (world-space origin, grid dims) plus a flat row-major `u16` height-sample
  array, zero slack. Origin confirmed exactly (zero-error, 1,194/1,194) as the tile's own low-X/low-Z
  corner against the already-documented tile-centre formula. A previously entirely undocumented
  runtime query chain was found and traced, including the real grid-cell sampler (`0x00b412c0`,
  `col=floor((x−originX)/spacing)`) — proving this is live, actively-used code, not dead data.
  **Critical cross-check against the live-playtest bug: the same 9 tiles reported broken/good today
  (§13.2) show byte-identical, correctly-formed `0x2244` data regardless of group — refutes "the
  file data is corrupt"; whether this data is ever consumed to build visible ground geometry remains
  OPEN and is the natural next step.** Side finding: resolves the long-standing §1/§8.1/§9.5
  8-vs-12-byte top-level-header CONFLICT (RESOLVED notes added in place at each marker) — the real
  header is 12 bytes `{tag,length,extra}` with no rounding step, population-validated 1,333/1,333
  exact-EOF landings on the `~s`-family subset. Full detail:
  `scratchpad/heightmap_0x2244_decode_2026-10-08.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-08 — §11's "no player marker in the opening mission's zone" correction: mission 1 has its
  own, separate, marker-bearing zone** (SPEC TEAM, agent `a30695cf2a54f0459`). Identified
  `sr3_city~fm01.czn_pc` (inside `sr3_city_0.vpp_pc/m01.str2_pc/`) as the real bank-heist mission's
  own scripted-content zone — distinct from the `bank_m1`/`bank_ext` landmark/architecture files a
  prior pass checked and correctly found empty. Found a real `Start_player_nav` player-start-shaped
  `Navpoint` marker inside it; census'd its 11 `Trigger_Volume` objects (none mission-start-shaped);
  confirmed by hash that neither `scripted_mission_start` nor `silent_mission_start`-classed objects
  exist in this zone's population. Cross-refs added to `spec-lua-api-behaviour.md` §36.1 and
  `spec-frontend-menu-runtime.md` §2. Full detail:
  `scratchpad/first_mission_bootstrap_dataside_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — `0x00859110`'s table row gains a consumer-side cross-reference** to new
  `spec-hud-runtime.md` §6.3, which confirms the minimap's per-tile `.cvbm_pc` texture streams in/out
  through this exact zone-dependency loader, alongside every other asset a zone references — not a
  separate mechanism. No new disassembly in this file. Cleanroom self-check: 0 new hits.

- **2026-10-08 — §13.2's Traffic (`0x2236`) own payload format CONFIRMED for the first time: a dedicated
  road-network topology graph (14 count/pointer sub-array pairs in a `0x70`-byte header; a segment→point
  cross-reference graph; a persistent "active traffic array" pool; an index-parallel extended per-segment
  block carrying a 2-bit segment-kind flag and two position codecs), confirmed structurally separate from
  both `0x2237`'s render geometry and the XTBL `Spline_Type` bitmask (no shared code with either).** Full
  consumer-module trace (lane-following shape, junction-kind flag, Havok driving-slice integration
  argument) lives in new `spec-ai-runtime.md` §18, per this project's DATA/CODE separation convention.
  Full detail: `scratchpad/traffic_ai_lane_following_2026-10-08.md`. Cleanroom self-check: 0 new hits.
  Synced to Team B.

- **2026-10-08 — §13.2 gains a confirmed runtime consumer for the Traffic/Sidewalks singletons; §13.4
  corrected (8 toggle slots, not 0–18).** Cross-reference update only — the runtime trace itself lives in
  new `spec-ai-runtime.md` §17 (ambient population spawn/despawn): the Traffic singleton (`0x02705fe0`)
  is walked by the distant-vehicle tick `0x00b93670`, the Sidewalks singleton (`0x02924d40`) by the
  distant-pedestrian tick `0x00b90960` — both previously documented as "load-time only, no runtime
  consumer traced." §13.4's `0x02609394` array is corrected from "slots capped 0–18" to 8 slots
  (confirmed from its own accessor functions), one of 6 sibling 8-slot toggle-category arrays. Full
  detail: `scratchpad/ambient_population_spawn_despawn_2026-10-08.md`. Cleanroom self-check: unchanged.
  Synced to Team B.

- **2026-10-08 — §13.2's pavement OPEN item RESOLVED: `0x2237` confirmed on real decoded vertex data to carry street/sidewalk/crosswalk pavement geometry, population-wide** (SPEC TEAM, agent `a35ddef8842ff0b4b`, follow-up requested after the prior day's survey left the question explicitly OPEN). Of 2,862 real `.czn_pc` files carrying a `0x2237` record, 374 (13%) have road/sidewalk-flavoured `.tga` names in their own embedded texture pool; decoding those records' real Mesh sub-block channels gives vertex positions for 374/374, and 359/374 (96%) are flat/horizontal by a conservative test — e.g. tile `sr3_city~s1312` decodes to a real 31m×140m slab 0.55m thick. Hypothesis (b), the static Level-Mesh/`.clmesh_pc` alternative, was checked on the same real tiles and found to be street furniture only (142 props, zero road-named) — a clean negative that doesn't compete with the `0x2237` finding. **A real methodology bug was found and fixed along the way:** the Mesh sub-block's own "primary buffer" is the index buffer, not the vertex buffer — real positions live in the block's own channel array instead (caught by a known-real-house control check before any negative result was trusted). Resolves the apparent tension with the prior single rendered sample (vertex layout code 24, building-shaped): that's a different individual channel (UV-less) within the same mechanism, not a contradiction with this pass's road-textured population (layout codes 2/4). Full detail: `scratchpad/pavement_0x2237_real_sample_2026-10-08.md`. Cleanroom self-check: unchanged (0 strict decompiler-token hits), new content contributes 0.

- **2026-10-08 — new §13.4: `0x2250` Parking deep-dived** (SPEC TEAM, agent `ae2a412f428235e2f`).
  Not a byte-format record — a thin generic-property wrapper around an embedded raw Havok
  2010.2.0-r1 physics packfile (public magic, RTTI-confirmed class `parking_spot`, MOPP-accelerated
  `hkpBoxShape` list, one box per ambient parking space). 178/2,971 files (6.0%) carry one record
  each, 100% pass Havok-magic validation at the predicted offset. Property ids `0x13`/`7` resolved
  as generic base-class bookkeeping via a different, simpler mechanism (plain small-integer ids,
  linear-searched array) — correctly NOT the CRC-32 name-hash convention §9.3/§11.7 use elsewhere,
  a useful negative refuting "every typed-property call shares one mechanism." Cross-confirms
  against pre-existing Lua-API/vehicle-data specs that this is ambient/decorative parked-vehicle
  spawn-point placement, not AI pathing or player-garage data. Full detail:
  `scratchpad/czn_parking_0x2250_deepdive_2026-10-08.md`. Cleanroom self-check: unchanged at 76
  hits, new content contributes 0.

- **2026-10-08 — §11.7 extended: `Suko`/`Cyrus` resolve by the same mission-prefixed-tile mechanism as
  Killbane; `Paparazzi_WM`/`STAG_WF` ruled out as zone objects entirely** (SPEC TEAM, wave-3 agent
  `a65253bea947bc320`). Cyrus = preset `npc_Cyrus_Temple` in `sr3_city~fm13`.czn_pc`/`~fm23`.czn_pc`
  (`ctg_object_name`=`npc_cyrus`); Suko = three records (`Suko_inside`/`Suko_start`/`Suko_checkpoint`)
  in `sr3_city~fm19`.czn_pc` plus a persistent open-world phone-call trigger in two always-loaded
  tiles. Both mission Lua scripts indirect through a one-level group table before any live-object
  call, exactly like Killbane — the bare character name is never the on-disk `ctg_object_name`.
  `Paparazzi_WM`/`STAG_WF` are a disjoint namespace entirely: 2D Wwise audio-persona names from
  `audio_personas.xtbl`, zero occurrences in any `.czn_pc`/`.czh_pc` file, base game or all 3 DLCs
  (exhaustively checked). Full detail:
  `scratchpad/named_objects_sentinels_0x954be8_m05_2026-10-08.md` §1. Cleanroom self-check: unchanged
  at 76 hits, new content contributes 0.

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the pre-2026-09-30 "no walker exists"/"no standalone parser"/"clean census negative" statements superseded by §9.5 (status header, §5, §6 item 1, §9 intro, §9.2 ×2, §9.6) and qualified the "no literal check" inference (§1, §3, §9.3, §9.5); recorded that `0x00D9E740` is the confirmed CRC-32 (§9.3); pointer from §6 item 6 to §9.5; struck the stale "Team B will send the predicate" line (§9.4); fixed 6 cross-references (§5→§1/§3 ×2, §6→§1–§3/§8, conversation §6a→§6.1, bare §5→`HANDOFF.md` §5 ×3).
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline (none needed: every cited fact is already stated in this spec or cited to another spec); repointed 1 `HANDOFF.md` §27.x reference to the archived headings (§10.7 item 1, §27.1 → §30 archived 27.1 table); qualified 2 bare §5 refs as `HANDOFF.md` §5 (§10 method paragraph, §10.9 methodology notes); 2 left (§6 item 1 and §9.4 cross-reference note `HANDOFF.md` §27.3, interior thread on hold) (see review).
- 2026-09-30 (format desk review, `adv_zone.md`): added the review status summary and a status line per unit (27 units); marked the top-level step-alignment conflict (§1/§8.1 `align8` versus §9.5 4-byte round-up) on both sides, unsettled, pointing at 0x00864c60 / 0x007512f0 / 0x008652d0 and D1; marked the `.gzn_pc` tiling residuals (§6 items 5/7, §10.5, §10.5.1, §10.8) superseded by Team B's full-population 1,002 / 1,002 tile and 37,035 / 115 / 0 split (`team-b/HANDOFF.md` lines 6622-6627), cause of this pass's 74 failures left OPEN; corrected §7.2's pre-header layout to §10.1's (g-length was missing); struck/scoped §9.2's CONFIRMED labels beside the SUPERSEDED marks; reworded §9.5's pseudocode-shaped lines into prose; paraphrased quoted game strings in §1/§7.4/§9.3/§9.4/§9.5 (kept only `use_default_loadout`, a name the code compares); narrowed §10.6's inline-mode overreach; added OPEN markers for the data checks D1-D13 and the executable checks. No content added about the object-stream interior (on hold).
- **2026-10-07 — §11 added (units U0+U1), the `.czn_pc` zone-data hold LIFTED by direct user authorization.** New §11 "The Objects (0x2234) section: the real placement/property-stream format, and the section-kind spine formalized." Formalizes §9.5's own gap assessment and §9.1/§9.2's dead-end routes with a from-scratch disassembly of the real call chain and format: the zone-instantiate-on-load spine (`0x00866580`→`0x008660e0`→{9-call and 11-call dispatcher tables}→the Objects handler `0x00863e90`) read to exact `ret`/int3 boundaries end to end (§11.1); the FBOW/version-5 payload header, 28-byte per-object record head (with a real correction to the on-disk tail-length field's offset, `+0x16` not `+0x1a` — that was the in-memory destination, not the source), and the variable property-list tail format (§11.2-§11.4, §11.7); class-name resolution confirmed as a 32-bit integer hash (not a string pointer) through a 510-bucket chained table into the engine's one universal per-object class-descriptor table `0x02cc9900` (3,757 xrefs binary-wide, settling §9.3's own "very plausibly" framing as a documented negative-leaning finding against the AI/spawn-behaviour table `0x00a33730`) (§11.5); both real per-object loaders (`0x00457c60` batch, `0x00457a50` one-at-a-time) read in full as genuinely different sibling implementations of the same record format, not a cosmetic duplicate (§11.6); the full consume-path traced to its real endpoint — registry vtable slot `+0x2c` (owner-attach + name registration) handing off to the new object's own class-specific vtable slot `+0x04` for tail parsing, the one piece not closed since the table is runtime-populated with no statically-recoverable class names (§11.7-§11.8). Independently cross-checked throughout against a prior same-project investigation (`review/exe-notes-2026-10-01/interp_named_object_resolution.md`) that had reached the same code from the opposite direction; one self-caught-and-corrected mid-pass mistake disclosed in place (§11.3's note on `0x00866950`'s own boundary). 11 open items appended as §11.9 (unresolved: real-data loader-branch frequency, the FBOW header's unidentified `0x08`-`0x1f` bytes, concrete class identities in `0x02cc9900`, the class-specific tail-parse step for any concrete class — the scoping note's own proposed U2 "record interiors" is the natural next unit to close several of these against real file bytes). Ghidra-free (capstone over `exe_va.py`); no `.czn_pc` data bytes were ever opened (code-only); no leaked material read. Cleanroom self-check (narrow-family regex, this file's own pre-existing `FUN_`/`DAT_`-accepting convention from earlier passes): 36/36, unchanged — the new §11 content itself contributes 0 hits (plain hex throughout), confirmed by isolating the new section's own line range before counting.
- 2026-10-02 (re-derived from the executable, cross-checked against `spec-ctorless-types.md` §4): corrected §9.2's third table row. `0x00863170`/`0x00863310` were described as `.zonex` helpers that swap authoring and shipped extensions. They reference none of the zone extension tables. `0x00863170` extracts the name part after the first `~` plus one letter, ending at the next `~` for a `.zonex` name and at `.` otherwise, and optionally drops one `!`/`$`/`` ` `` marker. `0x00863310` tests for a four-digit numeric lead followed by end-of-name or one of `!`, `$`, `` ` ``, `^`, `~`. The old wording is struck; a correction note goes under the table; the §9.2 label is qualified and its review status updated. `0x00863080` as the `SR3Z` parser is unchanged.
- **2026-10-07 — §11 replayed against real shipped file bytes for the first time (unit U2), plus a dedicated character-spawn-properties pass; both folded into §11 in place, no renumbering.** Two independent, cross-confirming real-data passes over 2,971 real shipped `.czn_pc` files. **Major correction to §11.7:** the tail/property-list format needs per-property 4-byte alignment (`round_up4(dataOffset+size)`, not `dataOffset+size`) — a literal read of the prior wording silently misparses roughly half of every multi-property tail past its first property (population control: 51.2%→100% once fixed, same 383,346-object population). **Correction to §11.1:** the "8 bytes commonly, 12 sometimes" top-level header framing is replaced with "uniformly 12 bytes for every real on-disk id" (19,725/19,725 records, 100%) — the 8-byte branch is dead for any tag with the standard `0x8000xxxx` shape every real id has. **Correction to §11.7's "`id = 0`" claim:** the real on-disk id for `ctg_object_name` is `0x355ef946` = `CRC32_reflected(lowercase("ctg_object_name"), seed=0)` under the engine's own already-known hash (`0x00D9E740`/table `0x01320DA0`) — `0` is only the pre-initialization placeholder in the raw `.data` image, confirmed verbatim in real file bytes by both passes independently. §11.9 items 1/3 settled population-wide (only the batch loader fires; the two count fields always agree). §11.9 item 4 substantially closed: 40 of 42 real class-name hashes named by exact CRC-32 match (the same hash family, now proven for both properties and classes by direct disassembly of the registration stubs, not just inference), with the real `0x02cc9900`-registration mechanism found and read for two classes (§11.9 item 9 closed for those two) — including a concrete, population-grounded link between `script_npc`'s own vtable and §9.3's independently-found generic property mechanism (§9.3's own "is this the same class" question: yes, for `script_npc`). Bonus: `0x2239` ("Trailer") confirmed a universal end-of-chain terminator, not dead content (light-touch corrected in `review/scoping-czn-interior.md` by the same unit). The character-spawn-properties pass independently reached the identical `id=0x355ef946` correction from a second real file, and separately closed the `interp_character_spawn_state.md`-parked max-HP/ignore-AI override question with real-data proof: `script_npc_hp` authored 1,028 times (values 1-100,000), a previously-undocumented `script_vehicle_hp`/`_flags`/`_team` family (354/6,702-sibling-scale/4,104 occurrences) not previously in any spec, and 425 real `script_npc_flags` instances containing `ignore_ai` (including a base-game, non-DLC example). Ghidra-free (capstone over `exe_va.py` plus real `.vpp_pc`/`.str2_pc` extraction via existing project tooling, one real extraction-tooling bug found and fixed by the U2 pass before any population count was taken from it, disclosed in place); no leaked material read. Cleanroom self-check (narrow-family regex): unchanged at 76 hits, all pre-existing — the new content contributes 0, confirmed by isolating line ranges before counting.
- **2026-10-08 — §13 added: survey of the remaining 18 content-kind ids, plus the pavement/`~al` question.** REFUTES a same-day killed attempt's own lead ("several ids share one generic Mesh+Material+AABB reader") with an exhaustive negative — `0x2237`/`0x2240`/`0x2251` are three privately-called siblings built from the same 5 primitives, no other id touches that toolkit (zero xrefs, whole-`.text` census). Disassembles the shared Traffic/Sidewalks/Traffic-signals dispatcher (`0x00863c80`) and REFUTES "Sidewalks" (`0x2238`) as a render-geometry/pavement source — it's a pedestrian AI path-graph sharing code and storage with the vehicle Traffic graph, substantially weakening the idea of any dedicated decal/road-streaming `.czn_pc` kind. Answers the paired `~al`-zone question: CONFIRMED as container kind `0x1B` ("Level Always Loaded," the engine's one global non-distance-gated streaming tier), unrelated to pavement (its one render sample is rooftop/building-silhouette geometry). The core "where does ordinary street pavement live" question stays OPEN (terrain-system `0x2237` vs. static Level-Mesh/`.clmesh_pc` content, neither ruled out — explained why Team B's own 3-tile negative result doesn't discriminate between them, §10.5's 63.6%-empty population fact). The other 14 ids independently surveyed to varying depth, each gap flagged OPEN by design. Ghidra-free (capstone over `exe_va.py`, two new scratch scripts, no file content opened — code-only); no leaked material read. Cleanroom self-check: 0 new hits (plain hex throughout), baseline unchanged at 76. Synced to Team B.

- **2026-10-08 — LIVE-REFUTED: §12.7 open item 2's "CLOSED 2026-10-07, no second loading path exists" finding, re-opened.** A live Frida capture against the real running game (`spec-ai-runtime.md` §14.7a) found the registry this item declared dead IS populated and functioning in real gameplay (99.25% success rate on 87,918 real reader calls during one captured gunfight) — the "exhaustive whole-binary call-site census" this item cited was wrong or incomplete. **The §12.6 DATA finding is unaffected and stands unchanged** (the Navmesh section's own file-side load gate really does read exactly `0` in all 1,647 real occurrences — that is a population-level fact about the file format, independently re-confirmed by nothing in this correction). What's refuted is only the INFERENCE that this file-side deadness means the runtime registry it would feed is never populated by anything — it demonstrably is, just not (or not only) through this gate. A dedicated search for the real writer is in progress (`spec-ai-runtime.md` §14.7a); this item will be updated again once it concludes. Cleanroom self-check: 0 new hits, baseline unchanged at 76.

- **2026-10-08 — the real-writer search concludes (`spec-ai-runtime.md` §14.7b has full detail, not reproduced here): the registry's COUNT writer is found (a real, content-independent, engine-boot allocation, `0x004470a0(count=8)` — NOT file-driven), but the presence-bit mystery remains an exhausted, unresolved static contradiction after two full passes. The `~f`-vs-`~s` buried-in-aggregate hypothesis this item's own text implicitly left open ("a real second writer exists... different loader... streaming callback... all back on the table") is specifically tested and REFUTED for the §12.6 gate dword: re-scanned both real city archives broken out by family (2,878 files, 1,588 Navmesh-tag occurrences) — `~f`=468/468, `~s`=468/468, plain=652/652, ALL gate=0, zero exceptions in any family. §12.6's own original finding was already correct and complete; the live/static contradiction is not explained by file-family stratification. Cleanroom self-check: 0 new hits, baseline unchanged at 76.

- **2026-10-08 — §13.2's ground-mesh-origin bug paragraph gains the two static questions' answers (same investigating agent, resumed a second time), and §13.3's Heightmap (`0x2244`) bullet gains a forward-pointer.** (1) External-reference-by-handle check: mostly negative but not exhaustive (the Objects `0x2234` content kind is absent from every `~s<CCRR>.czn_pc`; `~f<CCRR>.czn_pc`'s own `0x2237` is an empty stub in all 4 tiles checked; 9 sibling content-kind ids not individually checked for a numeric index into `0x2237`'s array). (2) Structural-distinction check: CONFIRMED CLEAN NEGATIVE on every field the shared Mesh sub-block header exposes, all 9 tiles — the near-origin content is a normal, ubiquitous, thoroughly-interleaved feature of every tile sampled, including all 5 "good" ones, not an anomaly confined to the 4 reported-broken tiles. Net: leans toward the near-origin blocks being genuinely local-space-authored (most plausibly reusable decal/kerb/street-furniture templates) rather than "garbage," explicitly NOT proof — a live draw-path capture remains the decisive test, dispatch left to the orchestrator/user. Heightmap flagged as the most promising unexamined carrier if tiles 0914/0715's own reported symptom needs a different content kind to explain it (their `0x2237` shows no defect). Full detail: `spec-terrain-format.md` §5 item 2; `scratchpad/terrain_ground_mesh_tile_origin_2026-10-08.md` ("SECOND ADDENDUM"). Cleanroom self-check: 0 new hits, baseline unchanged at 76.
