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

---

## 1. What was established

- **Population.** 2,971 distinct files, 180 bytes … 2,586,376 bytes, **every one a multiple of 4**. Paired 1:1 with `.czh_pc` (the zone header, resolved in `spec-ctorless-types.md` §5); only 1,083 have a `.gzn_pc` side. **[CONFIRMED — empirical.]**
- **The file opens with a typed record.** The first eight bytes are `{ u32 id, u32 length }` where the id always has its high bit set and its upper half is exactly `0x8000`. **The record that follows lands exactly at `align8(8 + length)` in 2,931 / 2,971 files (98.7%)**, against a **2.6%** control (the rate at which an id-shaped word appears at a *random* 8-aligned offset in the same files). The id/length reading is therefore real, not a coincidence of shape. **[CONFIRMED — empirical, controlled.]**
- **Only three distinct leading ids ship**: `0x80002233` (1,500 files), `0x80002237` (1,365), `0x80002234` (106). Across the whole population **790 distinct id values** occur. **[CONFIRMED — empirical.]**
- **No id is a hardcoded magic.** A search over every instruction in the binary for the immediates `0x80002237` and `0x2237` returns **nothing**. The loader never compares the leading word against a constant, so these ids are resolved through data — a registry or table — not by a literal check. **[CONFIRMED — disassembly-level search of the whole image.]** *(Qualified 2026-09-30, §9.5: the real walker does compare `tag & 0x7fffffff` against fixed constants, but they are held in globals (`DAT_013026f4`/`DAT_0130279c`) rather than encoded as immediates. The immediate-search negative stands; the "registry, not a literal check" inference does not.)*
- **Zone files carry world-object placement and behaviour data.** The embedded strings are property- and asset-names: `"Neutral Gang"`, `"gang offensive"`, `"use_default_loadout"`, `"execute lua script"`, `"enabled platform"`, `"bounding box"`, `"Floating"`, `"#npc_ng_male_soldier"`, `"Env_StreetLamp_Cool"`, `"Zload_hidden"`. **[CONFIRMED — empirical.]**
- **The zone module resolves dependencies by authoring extension.** One function references `.effectx`, `.cmeshx`, `.rigx`, `.todx`, `.ctdgx`, `.lmeshx`, `.smeshx`, `.fmeshx`, alongside `"tree objects"` and `"misc_resource"` — so zone contents reference other assets by their **authoring** names, which the module maps to shipped files. **[CONFIRMED — disassembly.]** This completes the project's authoring-extension list (§4).

## 2. What was tested and refuted

**Hypothesis: the file is a flat stream of `{id, length}` records, payload, padded to 8 — i.e. a uniform tagged-chunk format.**

It is not. Walking that model:

| alignment | walks without error | **lands exactly on EOF** |
|---|---|---|
| 4 | 2,855 / 2,971 | **0 / 2,971** |
| 8 | 882 / 2,971 | **0 / 2,971** |
| 16 | 2,695 / 2,971 | **0 / 2,971** |

**No alignment reaches end-of-file in a single file.** Chains stop after 2 records (1,531 files) or 3 (1,144), and the walk then hits words that are not id-shaped — commonly a `0xFFFB`-family value. **[CONFIRMED — empirical, whole population, three alignment variants.]**

This is recorded because the model *looked* right: it reproduces the smallest file's five records perfectly by hand, which is exactly the trap the project's own lesson warns about — a rule that fits some data is not the loader's rule. The refutation is as useful as a confirmation would have been: it says the top level is **not** a uniform chain, so a reimplementation must not walk one.

## 3. The reading the evidence supports

**[HYPOTHESIS — unconfirmed.]** A zone file is a **small number of typed top-level sections** (2–3 in the overwhelming majority of files), each introduced by `{id, length}`, whose **interiors use per-type encodings** rather than a recursive uniform chunk format. The large section holds the object stream. Supporting observations, each independently confirmed above: the leading record is real and its successor is exactly where its length predicts; chains are 2–3 deep, not dozens; 790 distinct ids exist but no id is literal-checked in code *(qualified 2026-09-30, §9.5: ids are compared against constants held in globals, not immediates)*; and the embedded content is property-name / asset-name data of the kind a data-driven object description produces.

What this does **not** establish: the meaning of any id, the interior layout of any section, or how the `0xFFFB`-family values fit. Those are open.

## 4. Cross-format contribution

Independent of `.czn_pc` itself, this pass produced two durable results, both already folded into other documents:

- **The complete authoring-extension list** for this engine: `.zonex`, `.cmeshx`, `.lmeshx`, `.smeshx`, `.fmeshx`, `.rigx`, `.effectx`, `.todx`, `.ctdgx` — plus `.ctd` and `.animx` known previously. Authoring names appear *inside* shipped data as cross-references; the shipped counterparts are the `c`/`g` pairs this project has specified.
- **`.czh_pc` confirmed from the code side.** The zone-header reader validates the shared material block, applies **the same mandatory 16-byte pad arithmetic** documented in `spec-geometry-format.md` §3.1.1, then runs the `SR3Z` parser and stores the material-block pointer at `SR3Z + 0x08`. This independently reproduces the layout that `spec-ctorless-types.md` §5 established by replay, and identifies one further field. **[CONFIRMED — disassembly.]**

## 5. Where this stops, and why

Completing `.czn_pc` requires the **per-section readers**, and those are inside the zone streaming subsystem: a ~7.6 KB function owning the *"zone header cache"* / *"zone always loaded"* strings, a ~2.7 KB dependency resolver, and a ~3.4 KB loader. That is a **runtime-subsystem investigation** — the category the user explicitly scoped out of this project — rather than a format pass, which is what this was taken on as.

That boundary was not visible in advance: `.czn_pc` was selected precisely *because* it looked like a format pass, and it is reasonable that it did. The honest finding is that **this particular format does not have a standalone parser to read** *(⚠ SUPERSEDED 2026-09-30, §9.5: a real top-level walker exists, `FUN_00864c60`/`FUN_007512f0` via `FUN_008652d0`)* — unlike every other format in this project, its structure is only expressed in the code that walks it section by section.

**What a reimplementation can and cannot do.** It *can* read the zone **header** (`spec-ctorless-types.md` §5, fully resolved), it *can* read the zone's **geometry** — the embedded Mesh sub-blocks and their `.gzn_pc` payload, using machinery already specified in `spec-geometry-format.md` §4.1 (§7 below) — and it *can* extract the property and asset name strings. It **cannot** read the object placement/property stream, which is what still needs the per-section readers.

## 6. Open Items

1. The per-section interior layouts **for the non-geometry sections** — needs the zone streaming subsystem (§5); a **user scope decision**, not a next step this pass should take unilaterally. *(The geometry sections are no longer open — see §7. The scope restriction was released 2026-09-11, HANDOFF §27.3, and this item was attempted under that release: see §9. Result — the per-type registration/constructor route is now a confirmed dead end (§9.1), four further named entry points contain no top-level walker and an exhaustive instruction-idiom census found none elsewhere in the binary (§9.2) *(⚠ SUPERSEDED 2026-09-30, §9.5: a real walker exists, `FUN_00864c60`/`FUN_007512f0` via `FUN_008652d0`; the census predicate missed its compiled form, `WALLS.md`)*, and a new, disassembly-confirmed lead — a generic hash-keyed named-property mechanism independently tied to a confirmed zone-content string — was found by a different route and is not yet closed (§9.3/§9.5).)*
2. What the 790 id values are and where the table that resolves them is built.
3. The `0xFFFB`-family values that terminate every flat walk.
4. The relationship between the three leading ids (`…2233` / `…2234` / `…2237`) and the zone `fast`/`slow` type split.
5. ~~`.gzn_pc`~~ — **resolved as the shared g-file payload, §7** — and its internal
   structure resolved further 2026-09-13: it is a **contiguous chain of self-delimiting Mesh
   segments** beginning at byte 0, each `[u32 tag][payload][u32 tag]`, packed back to back
   (ordinary zones) or padded to the next 16-byte boundary between segments (`~al` zones);
   928 of 1,002 pairs tile exactly to EOF, controls 0 of 1,002 both ways (§10.5). §7.1's
   "tag, three zero words, then the index buffer at `+0x10`" describes the FIRST segment;
   nobody had asked how many segments a `.gzn_pc` holds, and the answer is a mean of ~38.** Remaining within it: the per-channel vertex layout beyond the index buffer, which is the same standing open item as `spec-geometry-format.md` §4.1.2 and not specific to zones.
6. Which of the 790 section ids introduces the geometry sections — the Mesh blocks were located by their own signature, not by walking the section list. *(Partially advanced 2026-09-30, §9.5: ids `0x2237` and `0x2251` have geometry-shaped handlers; field layout provisional, `spec-terrain-format.md` §5 item 2.)*
7. **Why 74 of 1,002 `.gzn_pc` files do not tile to EOF under any segment-packing rule** (§10.8). They break late — e.g. `sr3_city~s1213` at cursor 1,880,316 of 1,883,284 after 256 segments — on "no bookend at the declared length" or "declared length overruns". Added 2026-09-13. **[OPEN / UNKNOWN.]**
8. **Whether the 921 of 39,073 g-backed zone candidates that close under no alignment are the same set as the ~915 whose primary-buffer element-size byte is not 2** (§10.8). Two residuals of nearly equal size; a set comparison would settle it and was not run. Added 2026-09-13. **[OPEN.]**
9. **The ~671 `.ccar_pc` g-backed Mesh-block candidates that close under neither alignment** (1,038 candidates over 367 pairs, 367 validating either way, §10.7). Vehicles are not affected by the alignment gate, so this residual needs its own explanation. Flagged, not diagnosed. Added 2026-09-13. **[OPEN / UNKNOWN.]**
10. **`mesh_scan.py`'s `p + 0x0E` and `p + 0x12` header-offset branches** (anchors at `p ≡ 2` / `p ≡ 6 (mod 8)`) are unexercised by any shipped zone block — 0 of 2,843 validated, 36 and 16 candidates respectively (§10.1). Untested, not confirmed. Added 2026-09-13. **[OPEN / UNKNOWN.]**


## 7. The geometry *is* readable — the shared Mesh mechanism generalises (added 2026-09-10)

The `.gzn_pc` pass tested whether the g-file mechanism established for `.gcmesh_pc` (`spec-geometry-format.md` §4.1.2 items 9–11) and `.gsrt_pc` (`spec-tree-format.md` §9) also covers zones. **It does, exactly.**

### 7.1 `.gzn_pc` is a tagged segment payload

| Finding | Result | Control |
|---|---|---|
| The g-file's leading `u32` appears as a `u32` in its **own** c-file | **1,002 / 1,002 (100%)** | another pair's leading word, in the same c-file: **6.5%** |
| `+0x04`, `+0x08`, `+0x0C` are zero | **1,002 / 1,002** | — |
| `+0x10` begins an ascending `u16` index buffer (`0,1,2,2,3,3,3,4,…`) | `u32@0x10 == 0x00010000` in **991 / 1,002** | — |

That is byte-for-byte the segment shape documented for trees: **tag, three zero words, then the index buffer at `+0x10`**. 1,083 `.gzn_pc` ship against 2,971 zones, and **81 are zero bytes** — a zone with no geometry payload. **[CONFIRMED — empirical, controlled.]**

### 7.2 `.czn_pc` embeds the shared "Mesh" sub-block

At the position where the g-file's tag occurs in the c-file, the preceding `u32` is **`9` in 1,002 / 1,002 files** — the shared Mesh sub-block's version field. Scanning a ±20-byte window, **no other offset shows a version-9 reading above 4/1,002**, and a random-offset control's best single offset is 6/1,002. The word after the tag is a plausible block length in 980/1,002. **[CONFIRMED — empirical, controlled.]**

So a zone's geometry is laid out as: `u32 version (9)` | `u32 check value` | `u32 block length` | the Mesh header — the structure `spec-geometry-format.md` §4.1.1 specifies, with the check value tagging its `.gzn_pc` segment.

### 7.3 Population profile of the embedded meshes — ⚠ two of five bullets REFUTED 2026-09-13, see §10.6

Enumerating blocks whose check value is hash-like (`≥ 0x10000`, which removes the small-integer coincidences that made a first attempt uninterpretable) across 400 zones:

- **4,478 Mesh blocks**; most zones carry 1–3, but 51 carry eight or more.
- **The bookend holds in 4,477 / 4,478** — each block ends with a second copy of its own check value, the same property found in trees. A specific `≥ 0x10000` value landing at a computed offset has a ~2⁻³² chance rate per trial, so this is decisive on its own arithmetic.
- ⚠ **REFUTED 2026-09-13 — §10.6.** Original wording: ~~**Flags bit 0 set (g-backed) in 3,448 / 4,478**; the remainder are inline-mode, as foliage is.~~ The ~1,030-block "inline-mode" remainder is an artefact of this bullet's own harness (`gzn_meshes.py`), whose header offset `ap(S+12,8)` lands 8 bytes early for anchors at `S ≡ 4 (mod 8)` — in **1,055 of 1,055** such anchors the byte it read as "flags" is the low byte of the g-length field, and that field is even in **1,048 of 1,055**, so bit 0 reads clear. **Replacement figure, whole 1,083-pair population, `mesh_scan.py`'s dynamic header offset: 292 of 39,366 candidates (0.74%) read flags bit 0 clear**, not 23%. See §10.6.
- ⚠ **PARTLY REFUTED 2026-09-13 — §10.6.** Original wording: ~~**Stride byte: 2** in 3,448 (matching the g-backed count exactly), then 12, 4 and 16 — the same stride profile as `.ccmesh_pc` and `.csrt_pc`.~~ The leading value is right and holds far more strongly than stated — **2 in 38,152 of 38,152** blocks over the corrected zone population (§10.5). **The "12, 4 and 16" tail is the same artefact as the bullet above**, read 8 bytes early at `S ≡ 4 (mod 8)` anchors: **0 of those 1,055 anchors reads 2**. And the parenthetical "matching the g-backed count exactly" was read at the time as corroboration; it is the opposite — both reads come off the same header pointer, so both fail together on the same anchors (§10.9's methodology note).
- Count field: median 132, maximum 21,822.

**[CONFIRMED — empirical, for the block count, the bookend rate, the leading stride value and the count field. REFUTED for the flags/inline split and the stride histogram's tail — see the two marked bullets and §10.6. This label previously read a bare `CONFIRMED — empirical` covering all five bullets.]**

### 7.4 What this changes

Zone **geometry** is not a new format and does not need the zone streaming subsystem: it is the machinery this project already specified, reachable by locating the version-9 signature. ⚠ **Added 2026-09-13:** locating the version-9 signature finds the *descriptor*; finding its matching payload segment in the `.gzn_pc` is a **chain walk, not a search** — see §10.4/§10.5.1, and do not reuse `mesh_scan.py`'s 16-aligned tag search, which reaches only 7.28% of them. What remains opaque is the **object placement and property stream** — the sections carrying `"Neutral Gang"`, `"use_default_loadout"` and the rest — which is what §5's scope boundary actually applies to.

**Correction to this document's original framing:** the first version of §5 stated that a reimplementation "cannot currently read `.czn_pc`". That was too pessimistic — it was written before the shared-mechanism test was run. The geometry was accessible the whole time through machinery already documented elsewhere in the project.

## 8. Attacking the top level from an independent anchor (2026-09-11, user-authorised)

**One usable result, both of this pass's other measurements discarded as vacuous.**
Reported that way because the vacuous ones looked like progress and the surviving one is
a negative.

### 8.1 The Mesh block is NOT a top-level section — controlled negative

The top level is a chain of `{u32 tag (high bit set), u32 length}` records, 8-aligned.
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

The master resource-registration table (`FUN_00700780`, `spec-geometry-format.md` §4.3) has
two rows for these extensions — registered-type ordinals **29** (`0x1d`, "Zone (fast)") and
**30** (`0x1e`, "Zone (slow)"). Read directly from the disassembly (`tools/reg_table_full.txt`
lines 493–520): **both rows point their extension-string fields at the same `.czn_pc`/
`.gzn_pc` string pair, both declare a 16-byte size field, and both set their constructor AND
destructor function-pointer fields to a literal `0`.** No vtable, no thunk, no stub — the
table entry itself is null. **[CONFIRMED — disassembly.]**

The generic resource-construction dispatcher (`FUN_00dd2e30`, already documented for a
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

### 9.2 Four further named entry points traced; none contain a walker of the top-level `{tag, length}` chain

Since the per-type path is closed, the walker (if a single one exists) must be bespoke code
reached some other way. Four candidates reachable from already-established zone-relevant
entry points were read in full:

| Entry point | What it actually does | Touches `.czn_pc` bytes? |
|---|---|---|
| `FUN_005d58f0` + seven ~147-byte helpers (`FUN_005d5440/54e0/5580/5620/56b0/5750/5850`) | Registers the fixed streaming-category table at startup (`spec-world-streaming.md` §7) | No — startup-time pool declarations only |
| `FUN_00859110` (2,705 bytes) | Given a zone's base name, classifies it (a byte read from a per-name lookup) into a streaming category, then builds filenames and issues load requests for the zone's *referenced* assets (`.clmesh_pc`, `.ctdg_pc`, `.cvbm_pc` minimap) | No — resolves dependencies by name, never opens `.czn_pc`'s own buffer |
| `FUN_00863080` ("SR3Z" parser) + `FUN_00863170`/`FUN_00863310` (`.zonex` filename helpers) | Parses the **`.czh_pc`** header block (magic `0x5A335253`, version range 27–29) and swaps authoring/shipped extensions | No — this is the already-documented zone-*header* reader, a different file |
| `FUN_00db1ad0`/`00db2ce0`/`00db33d0`/`00db3450`/`00db3fd0`/`00db4980` (a "streaming container" cluster found via the strings "prepackaged stream", "unrecognized prim type/streaming allocator/container type") | Generic, engine-wide bookkeeping for any streamed container (retry/allocate-wait state machine) plus a **text-driven** registry that resolves *names* of primitive types, stream allocators and container types via per-name lookup calls | No — this is shared infrastructure for every streaming container in the game, not zone-specific, and it resolves by name/string, not by the numeric `{tag,length}` records `.czn_pc` actually uses |

**[CONFIRMED — disassembly, each function read and described above.]** None reads a `u32`
at a cursor, checks its upper half against `0x8000`, and advances by a second `u32` length —
the operation `spec-zone-data-format.md` §2/§8 established purely from file-side replay. *(⚠ SUPERSEDED 2026-09-30, §9.5: the walker exists elsewhere, in `FUN_00864c60`/`FUN_007512f0` via `FUN_008652d0`, not among the entry points above.)*

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
address range or is reachable from any entry point in the table above.** **[CONFIRMED —
disassembly, exhaustive census, `CznFindTagWalk.java`.]** Stated as a predicate: a walker
matching this shape, anywhere in the binary, would have shown up in this list; it did not. *(⚠ SUPERSEDED 2026-09-30, §9.5: the real walker's compiled form uses `& 0x80000003`, a branching round-up and `& 0x7fffffff`, none of which this predicate covered; see `WALLS.md`.)*

**Two scope-jumps, flagged and not pursued**, per this project's standing discipline
(HANDOFF §5): `FUN_00da90d0`/`FUN_00da8d90`, called from `FUN_00859110` to test whether a
named handler exists for a zone's base name, turned out to have **75 unique direct
callers** spread across totally unrelated subsystems (rendering, physics, UI, audio) — a
generic, engine-wide named-callback/event lookup, not a zone-specific manager. It was not
traced further. **[CONFIRMED — disassembly, call-site census.]**

### 9.3 A different route found the engine's generic named-property mechanism, tied to a confirmed zone-content string

`spec-zone-data-format.md` §1 already lists `use_default_loadout` and `execute lua script`
among the strings embedded in shipped `.czn_pc` data. Searching the **executable's own**
string data for these exact strings (rather than searching for zone-specific code) finds
that several of them are *also* compiled into the game's code as literal comparison targets
— i.e. they are not just file content, they are property names the engine's own code
recognises. **[CONFIRMED — empirical, string match against the EXE's data section,
`CznPropStrings.java`.]**

Following the direct code reference to `use_default_loadout` (`FUN_00a33ff0`) leads to a
small, complete, disassembly-readable subsystem:

- **`FUN_00d9e740`** — a table-driven hash over the **lowercased** string (shape matches
  this project's already-documented general-purpose name hash, `spec-vpp-container.md`
  §2.2/`spec-extensionless-types.md` §4; not proven to be byte-identical to the specific
  routine documented there, since it lives at a different address, but the fold-to-lowercase
  + table-driven-word loop shape is the same). **[CONFIRMED — disassembly for this
  routine's own mechanics; HIGH CONFIDENCE — inferred that it is the same hash family.]**
  *(Resolved 2026-09-30: `0x00D9E740` is the engine's table-driven CRC-32 (lower-cased, no
  final XOR), CONFIRMED in `spec-tables-environment.md` §1.4 and `spec-tables-weapons-combat.md`
  §1.4; `spec-extensionless-types.md` §4 explains why this CRC-32, not the rotate-6/XOR hash of
  `spec-vpp-container.md` §2.2, is the engine's general-purpose name hash.)*
- **`FUN_00a33730`** — resolves a hashed name to a small ordinal (a byte, `0xFF` = absent)
  via a **40-bucket** hash table (`hash mod 0x28`) with an internal free-list allocator that
  assigns a fresh ordinal to a name seen for the first time. **[CONFIRMED — disassembly.]**
- **`FUN_00a33ff0`** — a setup routine that, for each of roughly thirty hardcoded property
  names (`always_see_player`, `attack_enemies_on_sight`, `cant_flee`, `respawn`,
  `immaterial`, `use_default_loadout`, and others — all AI/spawn-behaviour flag names), hashes
  the name, resolves its ordinal, and records which bit of a flags word that ordinal maps to.
  **[CONFIRMED — disassembly.]**
- A small class, reached through a vtable at `0x0117f2a4`, built around that ordinal space:
  slot 2 (`FUN_00a33de0`) is a **`HasProperty(hash)`** query; slot 4 (`FUN_00a33e80`) is a
  **`SetProperty(hash, 8-byte value)`** call that resolves the hash to an ordinal and writes
  into a fixed 40-entry, 8-byte-stride array; slot 3 (`FUN_00a33e00`) **bulk-deserialises the
  whole object directly from the engine's generic serialisation stream** (`FUN_00da7e90`
  read / `FUN_00da8020` tell / `FUN_00da7f30` seek — the same generic stream abstraction used
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
class's* bulk loader (`FUN_00a33e00`) is ever invoked with the stream cursor positioned
inside a `.czn_pc` buffer specifically, as opposed to a separately-registered
character/behaviour-template resource that a zone's spawn record merely *references* by
name. All three of this class's own entry points (`FUN_00a33e00`, `FUN_00a33e80`,
`FUN_00a33ff0`) resolve, on a caller search, to a single unresolved indirect/data reference
each — consistent with dispatch through yet another table this pass did not open, and not
informative about who holds that table. **[HIGH CONFIDENCE — inferred, that this mechanism
is what the object/property stream ultimately feeds; OPEN, whether this exact class's
loader is ever handed a `.czn_pc`-backed stream directly.]**

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

> ⚠ **Cross-reference note, 2026-09-12** (✅ **confirmed correct by Team B on direct inspection** — they read `spec-terrain-format.md` §3 themselves and matched its two "magic values" to the two dominant ids among the 790 the population pass found)**:** `spec-terrain-format.md` §3 characterised `.czn_pc`'s top level differently from §1–§3/§8 above (a single-sample "magic value" reading rather than this section's `{id, length}` chain), never previously cross-checked. Reconciled: they describe the same structure; terrain's sample also found a readable `"Region<003>"` string inside a top-level record's interior that this document's population-wide pass never looked for — recorded there, cross-referenced here, and not pursued further (record-interior content remains the project's one parked question, `HANDOFF.md` §27.3).
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
`spec-vehicle-geometry.md` §11.9.3), found a real top-level `.czn_pc` walker: `FUN_00864c60`
and `FUN_007512f0` (reached via `FUN_00864000`) each walk a buffer computing
`tag = *(u32*)(cursor+base)`, test `tag & 0x7fffffff` against a fixed constant
(`DAT_013026f4 = 0x2237`, `DAT_0130279c = 0x2251` — both real, in-population ids, `0x2237`
being the exact "Region" id `spec-terrain-format.md` §3's own real sample already found
preceding readable `"Region<003>"`/`"Region"` strings), and on mismatch advance
`cursor += cursor_field + length_field + alignment` — exactly this document's own §2/§8
`{high-bit tag, length}` chain shape. Both are called, alongside (at least) 6 further
sibling tag-handlers, from one master dispatcher, `FUN_008652d0`, itself called from
`FUN_00866580`/`FUN_00861460`/`FUN_00861800`/`FUN_0084e3b0`/`FUN_007acfe0` — a chain sitting
in the same tight `0x863000`–`0x866000` code module as the already-documented `.czh_pc`
header parser (`FUN_00863080`) but never checked by name in the table above.
**Why the original census missed it, precisely:** the real compiled walker uses
`& 0x80000003` (not the literal `0x80000000` the census required), a branching
`(x-1|0xfffffffc)+1` round-up idiom (not an `AND`-mask instruction at all) for its alignment
step, and `& 0x7fffffff` (not `0xFFFFFFF8`/`0xFFFF0000`) to strip/test the tag's high bit —
none of which match either of the two required literals the census's predicate searched
for. **Same family as `WALLS.md`'s standing "an instruction-form census undercounts without
a control on every compiler-emitted form" lesson (the D3D9 vtable-call census, the vehicle
`high16` stride census) — a new instance, not a new category.** This corrects the strong
form of the negative (a walker exists); it does **not** by itself confirm the *object/property
interior* specifically is read this way — only 2 of `FUN_008652d0`'s ~9 tag-handlers have been
opened so far (both geometry-shaped: a shared "Mesh sub-block + material set + index lists +
bounding volume" record for id `0x2237`, and a single mesh+material per-instance record for
id `0x2251`), and the remaining ~6 handlers' own content is uncharacterized — genuinely
possible, not yet shown, that one of them is the interior object/property stream's own real
reader. **[CONFIRMED — disassembly, full chain traced this pass; the "does this reach the
object/property interior" question stays OPEN, narrowed from "no walker exists at all" to
"which of ~6 uncharacterized tag-handlers, if any, is it."]** **Scope note, added after a peer
flagged it 2026-09-30:** everything in this paragraph is derived from disassembling the
game's own executable code (function calls, embedded tag constants) — none of it required
reading any real `.czn_pc` file's own content. The more detailed field-level layout of the
`0x2237` record specifically (beyond the "geometry-shaped" summary above) is flagged
provisional pending a scope question about whether it sits inside the PARKED object/property
interior — see `spec-terrain-format.md` §5 open item 2's own pending-scope note.

**New lead, not yet closed:** the engine's generic hash-keyed named-property mechanism
(§9.3), independently tied to confirmed `.czn_pc` content, is the strongest concrete
candidate yet for how the object/property stream's per-instance values reach a live object —
by name, hashed, through a small per-class ordinal table, not by fixed file offset. This is
consistent with, and gives a mechanism for, every negative this document and its predecessors
have already recorded about `.czn_pc` never containing a literal-checked id. *(Qualified 2026-09-30: the top-level walker in the CORRECTED paragraph above does compare tags against constants held in globals; the hash mechanism remains a lead for the property interior only.)*

**Concrete next step:** find who holds the table that resolves `FUN_00a33e00`'s (and
siblings') indirect call sites — i.e. the registration mechanism for this
property-class family, analogous to `FUN_00700780` but for behaviour/property classes rather
than file types. That table's entries, read directly, would show whether any class in this
family is constructed while a `.czn_pc` buffer is the active stream, which is the one fact
needed to close §9.3's open half. A string-hash brute-force of the remaining unidentified
top-level ids (`0x2233`/`0x2234`/`0x2237` and the wider 790) against candidate names was
considered and **not attempted this pass**: the project's one precedent for this exact
technique (`spec-conversation-format.md` §6.1, hashing 3,917 candidate strings against 57
unnamed ids) scored 0 matches with a 0-match control, i.e. unsuccessful rather than
miscalibrated, and there is no stronger candidate name list here than there was there.

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
second walker, so those figures are "by `mesh_scan.py`" in the sense §5 requires.

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

**[CONFIRMED — empirical, controlled: the `.gzn_pc` payload is a chain of self-delimiting
Mesh segments beginning at byte 0, each `[tag][payload][tag]`, packed contiguously (ordinary
zones) or padded to the next 16-byte boundary between segments (`~al` zones).]**

#### 10.5.1 What a reimplementation should do

Do **not** locate a block's g-side segment by searching for its check value. Two correct routes,
both search-free:

1. **From the c-side**, in c-file order, keep a running cursor starting at 0; block *k*'s segment
   begins at the cursor and the cursor advances by that block's declared g-length (for a `~al`
   zone, rounded up to the next 16-byte boundary). Correct for 37,446 of 37,730 blocks in
   multi-block files.
2. **From the g-side**, walk the chain as above and resolve each tag to its c-side block.

If a search is used anyway, the offset must be **4-aligned, not 16-aligned**, and the exact
replay plus bookend must still gate it — that combination is what the 0.13% foreign-file control
and the 0-of-38,152 perturbed-length controls above measure.

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
than explaining it away: there is essentially no inline-mode zone geometry to find.**
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
`p ≡ 0 (mod 8)` and 7,213 at `p ≡ 4 (mod 8)`: 80.0% sit in the alignment class where no
header-offset rule was ever in dispute.** The header-offset defects explain the inline-mode
figure and nothing else; the 13.4× under-count is entirely the g-side segment locator.
**[REFUTED — empirical, controlled by the alignment split of the non-closing population
itself.]**

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
   have an independent disassembly route (`spec-vertex-format.md` §12.1 / `HANDOFF.md` §27.1:
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
- **40 of 36,231 L2F candidates have a check value absent from the paired `.gzn_pc` entirely**
  and 40 lie inside a validated block's declared span; these are the clearest non-block
  candidates in the population.
- **The `p ≡ 2` and `p ≡ 6 (mod 8)` branches of `mesh_scan.py`'s header arithmetic are
  unexercised by any shipped zone block** (0 of 2,843 validated). Untested, not confirmed.

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
  offset, state it as the computation, per §5's existing rule, because this is exactly the defect
  that rule exists to prevent and it recurred anyway.
- **An instrument defect that under-counts by 13× leaves a self-consistent survivor set.** All
  2,843 survivors were real blocks, all passed every invariant, and every statistic computed on
  them was internally coherent — the same signature as the 64-group cap that discarded 286 of 372
  vehicles (`HANDOFF.md` §26.10). **The more selective the wrong filter, the healthier the surviving evidence
  looks.** Here the corrected population *strengthened* every claim measured on the old one
  (element-size byte 2 in 38,152 / 38,152), which is the lucky outcome, not the guaranteed one.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the pre-2026-09-30 "no walker exists"/"no standalone parser"/"clean census negative" statements superseded by §9.5 (status header, §5, §6 item 1, §9 intro, §9.2 ×2, §9.6) and qualified the "no literal check" inference (§1, §3, §9.3, §9.5); recorded that `0x00D9E740` is the confirmed CRC-32 (§9.3); pointer from §6 item 6 to §9.5; struck the stale "Team B will send the predicate" line (§9.4); fixed 6 cross-references (§5→§1/§3 ×2, §6→§1–§3/§8, conversation §6a→§6.1, bare §5→`HANDOFF.md` §5 ×3).
