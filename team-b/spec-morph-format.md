# Saints Row: The Third — `.cmorph_pc` Morph-Target Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, target selected from `spec-format-inventory.md` §6 — flagged open in both `spec-customization-data.md` and `spec-geometry-format.md`, and the last unresolved piece of the character pipeline alongside the already-documented mesh (`spec-geometry-format.md`), skeleton (`spec-rig-format.md`) and animation (`spec-anim-format.md`) formats.
**Scope:** The `.cmorph_pc` morph-target (blend-shape) file: its two-tier header, the target directory, the per-target descriptor records, the inline quantised delta payload, the trailing sentinel, the declared-but-unused `.gmorph_pc` GPU side, and the relationship between the **three** registered loaders that claim this extension pair.
**Method:** Disassembly of all three registered constructors and the two magic-number validators they lead to, cross-checked against **every `.cmorph_pc` file shipped in the game** — 2,946 entries across seven archives, 1,541 unique by name+size after removing loose/bundled duplicates. The loader-derived structural model was then *replayed* against all 1,541 files, and it reproduces every file's exact byte size. Every population-level claim below is stated against that full set; the two decisive claims are validated against built-in control baselines (§4, §5).
**Cleanroom compliance:** No decompiled code or original identifiers appear below. Magic numbers, offsets, strides and alignments are load-bearing literal format data and are stated freely. One literal engine error string is quoted because it is the format's own self-identification (the same convention used for the "Mesh" block in `spec-geometry-format.md` §4.1.1). Function addresses are cited as evidence, not reproduced as identifiers. Real filenames quoted are ordinary shipped data.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **The complete container structure is proven**: a loader-derived model reproduces the **exact byte size of 1,541 / 1,541** real files with zero residual, and every file ends in the expected trailing sentinel (§3, §4). **[CONFIRMED — empirical, whole population.]**
- Two developer-joke magic numbers, both constant across the population and both enforced by the loader: **`0x1337BEEF`** (outer, version 5) and **`0x0BADBEEF`** (inner, version 3). The inner block is the engine's literally-named **"Morph" block** — its parser carries the error string *"Morph version is (%d).  Expecting current version is (%d)."* — and it is **shared engine infrastructure**, living in the same module as the "Mesh" block already documented for `.ccmesh_pc`. **[CONFIRMED — disassembly + empirical.]**
- **Zero `.gmorph_pc` files ship anywhere in the game** (2,946 `.cmorph_pc`, 0 `.gmorph_pc`), even though the registration declares that secondary extension. §6 explains exactly why: the block supports a GPU-side payload (modes 0 and 2), but every shipped file uses **mode 1**, which stores its payload inline. **[CONFIRMED — disassembly + empirical, whole population.]**
- The payload is **sparse and quantised**, not raw floats: each affected vertex is a 12-byte element carrying a vertex index (confirmed at **25,789 / 25,789** records, §5) plus three quantised components, decoded via per-target parameters in a 40-byte descriptor. ~~The exact dequantisation formula is **not** resolved (§7).~~ **Corrected 2026-09-12: this bullet was written before §12–§13 and is now false as stated — the formula IS resolved, from the runtime applier rather than the loader. `pos += (i16/32767.0)·w·A[axis]`, `nrm += ((u8/255.0)·2.0−1.0)·w·B[axis]`, confirmed on 21,104,267 elements. See §13 (formula) and §14 (the slider weight `w` and the renormalisation that follows it). §7 is kept as the superseded pre-resolution record.**
- **The three-loader question is answered** (§8): one format, one parser; the two "Pcust" loaders don't parse at all — they stash the name and buffers into the customization system for deferred use.
- **Renormalisation and the slider→weight remap are resolved from raw disassembly (§14, 2026-09-12).** Renormalisation runs exactly once per vertex, on the fully-accumulated `(base + all active sliders' deltas)` vector, at the point of encoding to the output vertex buffer — never per-contribution. **[CONFIRMED — disassembly.]** The slider remap is the `lo`/`hi`/`hi2`/`flags` linear-clamp formula already stated in §13.3, now read from the complete kernel rather than a partial view, and — as the task that produced it states explicitly — this half **cannot reach CONFIRMED — empirical**, since no shipped file carries a live slider value.
- **Vertex-index correspondence to the paired `.ccmesh_pc`/`.gcmesh_pc` is RESOLVED (§15, 2026-09-13): the direct reading holds.** A morph index `i` addresses index `i` in the paired mesh's own real vertex array — no offset, remap table, or different-array indirection. Confirmed at population scale (34,068 targets, 11.3M elements, 100.0000% in-bounds against the real decoded mesh vs. 66.8% under a shuffled wrong-mesh control) and by two geometric case studies with shuffled-index controls, one of which (`cm_pc_fac.cmorph_pc`, the 95-slider player face file) has no filename or bundling relationship to its mesh at all and still resolves cleanly, down to visible bilateral left/right vertex-cluster pairs decoded from raw positions alone. **[CONFIRMED — empirical.]**

## 2. Population facts

| | |
|---|---|
| Shipped `.cmorph_pc` entries | 2,946 across `characters`, `customize_item`, `dlc1/2/3`, `player_morph`, `preload_items` |
| Unique by (name, size) | 1,541 — `customize_item.vpp_pc` ships many files both loose and inside their `custmesh_*` bundle, and NPC head morphs are shared across bundles |
| Shipped `.gmorph_pc` entries | **0** |
| Extraction reliability | All source archives store these entries raw (per-entry sentinel), so extraction is fully reliable — none of the container's mode-(a) limitations apply |
| Size range | 324 bytes (a hat) … 805,236 bytes (a full DLC body suit) |
| Most common size | **17,220 bytes × 218 unique files** — every NPC `*_head.cmorph_pc`. Identical size implies identical layout: one target, 1,426 affected vertices, max index 1,425. **[HIGH CONFIDENCE — inferred]**: every NPC head shares one base-head topology. |

## 3. Layout

All offsets from file start. Alignment is **8-byte** within the header/directory and **16-byte** around each bulk run, using the conditional form (pad 0 when already aligned — the loader's own arithmetic shows this explicitly, so the mandatory-pad rule of `spec-geometry-format.md` §3.1.1 does *not* apply here).

### 3.1 Outer header (`0x10` bytes)

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `+0x00` | 4 | Magic **`0x1337BEEF`** | **[CONFIRMED — loader requires it; 1,541/1,541.]** |
| `+0x04` | 4 | Version, must be **`5`** | **[CONFIRMED — loader requires exactly 5; 1,541/1,541.]** |
| `+0x08` | 8 | Two runtime pointer slots, zero on disk; the loader zeroes them and fills the first with an allocated GPU-side object when the block has any targets | **[CONFIRMED — disassembly.]** |

### 3.2 "Morph" block header (`0x18` bytes at `+0x10`)

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `+0x10` | 4 | Magic **`0x0BADBEEF`** | **[CONFIRMED — loader requires it; 1,541/1,541.]** |
| `+0x14` | 4 | Version, must be **`3`** (mismatch produces the "Morph version…" error) | **[CONFIRMED — disassembly + 1,541/1,541.]** |
| `+0x18` | 4 | **Mode**: `0`, `1` or `2`; anything else is rejected. Selects payload location and element stride (§6). **Every shipped file is mode 1.** | **[CONFIRMED — disassembly + 1,541/1,541.]** |
| `+0x1C` | 4 | **Target count** — number of directory entries. Observed 1…214; 52 distinct values (6 ×588, 1 ×239, 9 ×137, 7 ×83, 119 ×69, 96 ×56, …). | **[CONFIRMED — disassembly, drives the directory walk.]** |
| `+0x20` | 8 | Runtime directory-pointer slot, zero on disk | **[CONFIRMED — disassembly.]** |

### 3.3 Directory (`count × 0x10` at `+0x28`)

One 16-byte entry per target: `{ u32 id, u32 n, u32 runtime_ptr(=0 on disk), u32 0 }`. `id` is a 32-bit value unique per target that **recurs across files sharing the same target** (the player face morph and the base NPC head carry identical `id`s in the same positions) — **[HIGH CONFIDENCE — inferred: a hash of the target's name.]** `n` is the number of descriptor records for that target — `1` in every entry inspected; the loader supports more. **[CONFIRMED — disassembly for the walk; OPEN — whether `n > 1` ever occurs.]**

### 3.4 Descriptor records (mode 1: `n × 0x28` per target, immediately after the directory)

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `+0x00` | 4 | Zero in every record inspected | **[OPEN.]** |
| `+0x04` | 2 | **`N` — number of affected vertices.** Drives the bulk size `N × 12`. **`N = 0` occurs in 19,454 / 45,243 descriptors (43%)** — a target declared but touching no vertices; the exact-size replay includes these records, so they are structure, not an artifact. A plausible reading — every file carries the full slider set and only some targets are active for a given head or garment — is **[HYPOTHESIS]**, not asserted. *(Team B, 2026-09-10; re-verified.)* | **[CONFIRMED — the exact-size replay (§4) depends on it; 1,541/1,541.]** |
| `+0x06` | 2 | **Maximum vertex index** referenced by this target's elements | **[CONFIRMED — empirical: equals `max(index)` in 25,789 / 25,789 records, §5.]** |
| `+0x08` | 12 | Three floats, small positive (e.g. `0.015, 0.072, 0.066`) | **[HIGH CONFIDENCE — inferred: dequantisation parameters, see §7.]** — **RESOLVED, §13.3: `A`, the position-delta scale.** |
| `+0x14` | 12 | Three floats, larger (e.g. `1.72, 1.52, 1.23`); tiny for subtle targets | **[HIGH CONFIDENCE — inferred: dequantisation parameters, see §7.]** — **RESOLVED, §13.3: `B`, the normal-delta scale.** |
| `+0x20` | 8 | Runtime bulk-pointer slot, zero on disk; the loader writes the absolute address of this record's bulk run here | **[CONFIRMED — disassembly.]** |

### 3.5 Bulk runs (mode 1, inline)

For each descriptor record in order: align to 16, then **`N` elements of 12 bytes**, then align to 16. Element layout:

| Element offset | Size | Content | Confidence |
|---|---|---|---|
| `+0` `+2` `+4` | 3 × 2 | Three quantised components. Values are strongly bimodal, piling up near `0` and near `65535` — the signature of small signed deltas stored as biased / two's-complement 16-bit integers. | **[CONFIRMED — the distribution; HIGH CONFIDENCE — that they are quantised position deltas; OPEN — exact decode.]** — **RESOLVED, §13.2/§13.3: two's-complement `i16` position delta, divided by `32767.0` and scaled by `A`; the values use the full `int16` range (§12.1), not only small deltas.** |
| `+6` | 2 | **Vertex index** (§5) | **[CONFIRMED — empirical, 25,789/25,789.]** |
| `+8` | 2 | Not characterized | **[OPEN.]** — **RETIRED, corrected by §13.2: `+8`/`+9`/`+10` are three `u8` biased normal-delta bytes; `+11` is zero padding.** |
| `+10` | 2 | Always `< 4096` (10,415,507 / 10,415,507 elements) | **[CONFIRMED — the bound; OPEN — meaning.]** — **RETIRED, corrected by §13.2: not a 2-byte field; the bound is a side effect of `+11` being zero.** |

### 3.6 Trailer

Align to 4, then a `u32` that must equal **`0x0BADBEEF`** — a bookend sentinel the loader re-checks before accepting the block. Present in **1,541 / 1,541** files, and it is the last thing in the file. **[CONFIRMED — disassembly + empirical.]**

## 4. The structural proof: exact-size replay

The loader's walk (§3.1–§3.6) was re-implemented from the disassembly and run against all 1,541 unique files, predicting where the file should end:

| | |
|---|---|
| Files where the model's predicted end **equals the file size exactly** | **1,541 / 1,541** |
| Residual distribution | `0` × 1,541 |
| Files whose final `u32` is `0x0BADBEEF` at the predicted position | **1,541 / 1,541** |

**[CONFIRMED — empirical.]** An exact-size match across a population whose sizes span 324 to 805,236 bytes, driven by three independent count fields (targets, records, affected vertices) and two alignment rules, is not something a wrong model produces by accident. This is what upgrades §3's per-field claims from "disassembly says" to "verified against every real file."

A first-pass model that omitted the bulk-run pass had reproduced **0 / 1,541** with residuals of exactly `N × 12 (+ padding + 4)` — which is how the bulk stride and the trailer were pinned down: the residual *was* the missing structure.

## 5. The vertex index — and the control that placed it

The 12-byte element's meaning was tested by hypothesis, with the population as the arbiter:

| Hypothesis: the index is the `u16` at element offset… | strictly ascending within record | max equals descriptor `+0x06` |
|---|---|---|
| `+0` | 324 / 25,789 (1.3%) — **but 322 of those are `N = 1` records, ascending vacuously. Over the 25,467 records with `N ≥ 2`, where the test can actually fail: 2 / 25,467 (0.008%)** | 1 / 25,789 |
| **`+6`** | **25,789 / 25,789** | **25,789 / 25,789** |

Additionally, at `+6` every index is unique within its record (25,789 / 25,789). **[CONFIRMED — empirical.]** The `+0` row is not a failed guess to be embarrassed about — it is the control baseline that makes the `+6` result meaningful: an arbitrary 16-bit field is ascending in ~1% of records — and even that 1% is inflated: 322 of the 324 `+0` "hits" are single-element records, where "strictly ascending" has nothing to fail against. Counted only where the predicate can fail (`N ≥ 2`), the by-chance rate is **2 / 25,467 (0.008%)**, while `+6` stays at 25,467 / 25,467 over the same records — so the true contrast is 0.008% vs 100%, not 1.3% vs 100%. *(Correction from Team B's independent replay, 2026-09-10, re-verified here; a control over records where the predicate cannot fail dilutes itself — recorded in `HANDOFF.md` §5.)* The real index is ascending in 100% and its maximum is *exactly* the descriptor's declared maximum in 100%. That second identity also confirms the descriptor's `+0x06` field as the **maximum vertex index** rather than a vertex *count* — a distinction that matters for anyone allocating a remap table.

**Consequence:** morph targets are **sparse**. Only affected vertices are stored, each tagged with its index into the base mesh. This is consistent with the `size / count` ratio being highly variable across files (median 7,342 bytes per target, standard deviation 9,702) — subtle targets touch few vertices, full-body targets touch thousands.

## 6. Modes, and why `.gmorph_pc` never ships

The "Morph" block parser takes **two** buffer/cursor pairs — the primary (`c`-file) and a secondary one — exactly as the generic dispatcher hands every paired type both files (`spec-resource-dispatch.md` §4). The mode field decides which buffer the bulk lives in:

| Mode | Descriptor stride | Bulk location | Element stride | Bookends |
|---|---|---|---|---|
| **1** | `0x28` | **inline, primary file** | 12 | single trailing sentinel (§3.6) |
| 0 | `0x18` | secondary buffer (`.gmorph_pc`) | 12 | `0x0BADBEEF` before *and* after each target's run on the secondary side |
| 2 | `0x18` | secondary buffer (`.gmorph_pc`) | 8 | as mode 0 |

**[CONFIRMED — disassembly, all three paths read in full.]** Mode 2's 8-byte element (vs. 12) reads as a more compact quantisation for the GPU-resident variant. **[HYPOTHESIS.]**

Since **every one of the 1,541 shipped standalone files is mode 1**, the secondary buffer is never read for them, and no `.gmorph_pc` needs to exist. **Mode 0 is, however, exercised — by the Morph blocks embedded inside vehicle geometry** (`spec-vehicle-geometry.md` §7): there the `g`-file role is played by `.gcar_pc`, and the mode-0 per-target `0x0BADBEEF` bookend sits at exactly the `.gcar_pc` offset the vehicle header supplies, in **326 / 326** files against **0 / 2,400** random offsets. **[CONFIRMED — empirical.]** So the mode-0 layout in the table above is now observed, not merely read from the loader — which is exactly what the game ships: **2,946 `.cmorph_pc`, 0 `.gmorph_pc`**. The registration's secondary extension is real capability, not vestigial, just never exercised on PC. **[CONFIRMED — empirical + disassembly.]** This is also a useful data point for `spec-resource-dispatch.md`: the paired-file resolver must tolerate the secondary file being absent, since it is absent for every morph in the game.

## 7. The dequantisation as it looked before resolution — refuted readings and the sanity check that mattered (SUPERSEDED by §13)

**Heading corrected 2026-09-12: this section previously read "What is *not* resolved:
the dequantisation," which by the time §12–§13 were written was flatly false and
contradicted this file's own §1 and §13 headline claims — a reader stopping at this
heading would conclude the opposite of what the document establishes. The
dequantisation IS resolved; see §13.** This section is kept, retitled rather than
deleted, because it still records something worth having: the raw-float reading that the
byte-magnitude sanity check killed, which is exactly the kind of refuted-model record
this project's methodology keeps on purpose (`HANDOFF.md` §5). The two `[OPEN.]` labels
below are stale and are retired in place, not deleted, per the same discipline.

The bulk is provably not raw floats — read as `f32`, the three leading components are ~10³⁷–10³⁸ in essentially every file (a sanity check that *killed* the initial "float3 delta" reading, which the exact-size arithmetic alone could not distinguish from a 12-byte quantised element). What remains is well-constrained but not closed:

- The descriptor's two float triples (§3.4) have the shape of **per-axis dequantisation parameters** — a small triple and a larger triple, per target, with subtle targets carrying tiny values in both. Whether they are `(min, extent)`, `(offset, scale)`, or `(scale, center)` — and whether the 16-bit values are unsigned-biased or two's-complement — was not determined. **[HIGH CONFIDENCE — that these are the decode parameters; OPEN — the formula.]** — **RETIRED, resolved by §13.3: neither guess was right. `A` (the small triple) scales the *position* delta, `B` (the larger triple) scales the *normal* delta — two different quantities, not two parameters of one quantity, which is also why none of the three algebraic identities tried against them (§12.1) could have existed. The 16-bit position components are two's-complement, divided by exactly `32767.0`.**
- Element `+8` is uncharacterized; `+10` is bounded below 4,096. Plausibly a quantised normal delta or a per-vertex weight. **[OPEN.]** — **RETIRED, resolved by §13.2: `+8`/`+9`/`+10` are three individual unsigned bytes, one per normal axis, decoded as `(u8/255.0)*2.0-1.0`; `+11` is unused padding, confirmed zero in 21,104,267 / 21,104,267 elements. The old "`+10` as u16 is always `< 4096`" reading was a side effect of `+11` being zero, not a property of the format — there was no separate 2-byte field there at all.**
- The relationship between the vertex indices here and the vertex order in the paired `.ccmesh_pc` (`spec-geometry-format.md` §4.1.2) is untested. **[OPEN.]** *(Still open — not addressed by §12 or §13; see §11 item 6.)* — **RESOLVED, §15: index `i` addresses vertex `i` of the paired mesh directly.**

Closing this needs the code that *applies* a morph to a mesh at runtime (the GPU-side object the outer validator hands the block to, §3.1), not the loader — a separate, scoped target. — **That code was found and read: §12.2 shows the GPU-side object named here is actually dead code for every shipped file (mode 1 only), and §13 recovers the real applier, `FUN_009f6d30`, reached from the customization assembler instead. The "separate, scoped target" this paragraph called for is exactly §12–§13's subject.**

## 8. Three registered loaders, one format, one parser

*(Pattern note, 2026-09-10: the "stash-only" constructors found here at types 11/12 are not a morph quirk — `.csc_pc` (type 24) does the same thing, storing its buffer to a global that a separate subsystem later parses. When a registered constructor's body is a handful of instructions, the parser lives elsewhere and the global it writes is the trail. See `spec-cutscene-camera-format.md` §1, §8.)*

`spec-format-inventory.md` lists three types on this extension pair. Tracing all three:

- **Type 7, `Character morph`** — the real path. Its constructor claims a slot from a free-list pool (the intrusive-pool idiom documented in `spec-lua-bindings.md` §3), calls the `0x1337BEEF` validator, which calls the "Morph" block parser, registers the result by name, and finalizes registration through the same routine `.ccmesh_pc`'s constructor uses. It receives the secondary (`g`) buffer per the generic dispatcher and, for every shipped file, never touches it. **[CONFIRMED — disassembly, full chain.]**
- **Type 11, `Pcust morph`** and **Type 12, `Pcust creation morph`** — **do not parse.** Each is gated on a match against a field of a customization-system singleton; on match it copies the morph's *name* (up to `0x41` bytes) into the singleton and stashes the incoming buffer pointers there (four for type 11, two for type 12), then returns. Nothing reads the file's bytes. **[CONFIRMED — disassembly.]** These are deferred-use registrations for the player-customization and character-creation flows respectively.

So the answer to the inventory's implicit question — "do three loaders mean three formats?" — is **no**: one format, parsed in exactly one place, plus two entry points that record where a morph *is* for later. The inventory's warning is resolved rather than assumed away.

## 9. Semantics (inferred, stated as such)

- **Target count = number of morph targets (blend shapes).** The player face file (`cm_pc_fac.cmorph_pc`) carries **95**; full bodies carry 118–119; NPC heads carry **1**; hats and accessories 5–9. 95 targets on the player face is the character creator's slider set. **[HIGH CONFIDENCE — inferred from the counts alone.]**
- The 218 identical NPC head files (§2) each hold one target over the same 1,426 vertices with the same maximum index — one shared base head. **[HIGH CONFIDENCE — inferred.]**

## 10. Methodology notes worth keeping

1. **Two same-type samples agreeing is not confirmation.** Two NPC head files both showed `(1426, 1425)` at `+0x3C`, and it was nearly written up as "vertex counts in the header." Across 1,541 files that position reads `(1, 0)` in 1,246 — it isn't a header field at all, it's the first descriptor's `N`/max-index pair, which only *looks* fixed because all heads share a topology. Caught only by checking the population. Same trap as `HANDOFF.md` §5's small-sample rule, in a new costume.
2. **Exact-size arithmetic proves stride, not type.** The `N × 12` replay matched every file, which felt like confirmation of "float3 deltas." A cheap sanity check on the float magnitudes killed that reading outright. Structure and encoding are separate claims and need separate tests.
3. **A residual is a measurement.** The first replay's `0 / 1,541` result, with residuals of exactly `N × 12 + pad + 4`, located the missing bulk pass and the trailer more precisely than any amount of reading would have.
4. **A control over records where the predicate cannot fail dilutes itself** *(added 2026-09-10 from Team B's replay, re-verified).* The `+0` control's 1.3% was 322 single-element records passing "strictly ascending" vacuously plus 2 real coincidences. Restricting the denominator to `N ≥ 2` gives 0.008% — the finding was already right, but the contrast was understated ~160×. Before quoting a rate, count only the trials that could have gone either way.

## 11. Open Items

*(List written before §12–§14; items 1 and 2 are now resolved and kept here, struck
through rather than removed, so this list's history stays visible — see each item's
pointer.)*

1. ~~**Dequantisation formula** (§7) — the one thing between this spec and reconstructing actual vertex deltas.~~ **RESOLVED, §13.** No longer open.
2. ~~**Element `+8` and `+10`** (§3.5).~~ **RESOLVED, §13.2**: three individual normal-component bytes at `+8`/`+9`/`+10`, `+11` unused. No longer open.
3. **Descriptor `+0x00`** — zero in every record inspected.
4. **Directory `n > 1`** — supported by the loader, never observed; not tabulated population-wide.
5. **Directory `id` as a name hash** (§3.3) — consistent with the evidence, not confirmed against a known hash function.
6. ~~**Vertex-index correspondence to `.ccmesh_pc`** (§7).~~ **RESOLVED, §15.** A morph index addresses the same position in the paired mesh's own vertex array -- confirmed at population scale (34,068 targets / 11.3M elements, 100.0000% in-bounds against the real decoded mesh vs. 66.8% under a shuffled wrong-mesh control) and by two geometric case studies with shuffled-index controls, including one file (`cm_pc_fac.cmorph_pc`) with no filename/bundling link to its mesh at all. No offset, remap table, or different-array indirection. No longer open.
7. **Mode 2** — fully read from the loader, never exercised by any shipped file; unverifiable empirically on this build. *(Mode 0 was closed by the vehicle-geometry pass — see §6.)*


## 12. The dequantisation pass (2026-09-11, user-authorised) — architecture found, formula resolved next in §13

**Heading and this paragraph corrected 2026-09-12: at the time this section was written,
the formula was genuinely still open, and the paragraph below is kept verbatim as an
honest record of that state — but §13 (written immediately afterward, same date) resolved
it, so a reader must not stop here and conclude the formula is unknown.** What this
section reports stands on its own regardless: *where the formula cannot be* (§12.1, §12.2)
and *where the real consumer chain runs* (§12.3), which is exactly the groundwork §13
builds on.

**This section deliberately reports an incomplete result [as of when it was written —
see correction above].** The dequantisation formula
is **not** resolved [**at this point in the document's history; RESOLVED by §13**]. What was resolved is *where it cannot be* and *where the consumer
chain actually runs*, which removes two dead ends and names the addresses the next pass
needs — none of which this document previously recorded (it named no addresses at all).

### 12.1 A statistical identity search over the descriptor triples — clean negative

§7 proposed that the two float triples are per-axis dequantisation parameters. Rather
than test candidate formulas and keep the best score, the relation between them and the
actual quantised range was measured over **56,822 descriptors with `N > 0` (170,466 axis
samples)**, looking for an exact algebraic identity:

| Candidate identity | Result |
|---|---|
| `large[a] / small[a]` constant | **no** — scatters (0.0, 1.0, 0.1, 0.3, 0.2, 0.4 …) |
| `large[a] == small[a] × max|q[a]|` | **no** — ratio scatters, mode at 0.0 |
| `large[a] == small[a] × 32767` | **no** — ratio scatters across 0.000–0.005 |

**And one assumption in §3.5 needs qualifying.** The components were described as *small*
signed deltas from their bimodal distribution. They are signed, but they are **not
small**: `max|q|` reaches **≥ 32,000 in 136,804 of 170,466 axis samples (80%)**, so the
full `int16` range is in use and the decode is not a small-delta-times-scale form.
**[CONFIRMED — empirical.]** Harness `morph_dequant.py`.

*Predicate stated, per this project's rule: this rules out those three algebraic
relations between the descriptor triples and the quantised range. It does not rule out
a formula involving both triples together, a per-file constant, or a form where the
triples are consumed by different stages.*

### 12.2 The GPU-side class is the mode 0/2 path ONLY — dead code for every shipped file

The outer validator allocates an object from a free-list pool and hands it the parsed
block through **vtable slot `+0x10`** (vtable at `0x012a09cc`, allocator `0x004834f0`).
Ghidra defines no functions at three of that vtable's slots, because the only references
are indirect calls through the table — the same reason the registered constructors have
no direct xrefs. Creating them and reading all four:

| Slot | Address | Behaviour |
|---|---|---|
| `+0x0c` | `0x00483560` | — |
| **`+0x10`** | **`0x00483610`** | **Stores the block pointer at object `+8`, then returns unless `mode == 0 or 2`** |
| `+0x14` | `0x00483630` | Gated on `mode == 0 or 2`; binds a buffer with stride **`0xC` or `8`** |
| `+0x18` | `0x004836b0` | Gated on `mode == 0 or 2` |

**Every method gates on mode 0 or 2, and every shipped file is mode 1.** The stride pair
`0xC / 8` matches the documented mode-0/2 element strides exactly, so this class is the
**g-file-resident vertex-buffer path**. For shipped data it stores a pointer and does
nothing else. **[CONFIRMED — disassembly, all four slots read.]**

**Consequence for an implementer:** do not look for the morph decode here, and do not
implement this class at all — with zero `.gmorph_pc` shipping (§1), it is unreachable.
§7's instruction to find "the GPU-side object the outer validator hands the block to" was
pointing at the wrong object; that is corrected here rather than left to be rediscovered.

### 12.3 The mode-1 consumer chain, with addresses

| Thing | Address / offset |
|---|---|
| Outer `.cmorph_pc` validator (`0x1337BEEF` + v5) | `FUN_0074e500`, called from `FUN_0074e580` |
| Shared "Morph" block parser (`0x0BADBEEF` + v3) | `FUN_00e71940` — sibling of the "Mesh" parser in the same module |
| ~~Type 11 `Pcust morph` constructor | `0x009f0910`~~ **CORRECTED 2026-09-20 (§16.1): `0x009f0910` is type 10 `Pcust peg`; type 11 `Pcust morph` is `LAB_009f0970`** |
| ~~Type 12 `Pcust creation morph` constructor | `FUN_009f0970`~~ **CORRECTED: `FUN_009f0970` is type 11; type 12 `Pcust creation morph` is `LAB_009f09d0`** |
| **Customization singleton** | **`DAT_0263e0f4`**, ~~14 users~~ **17 users** (§16.7), all in `0x009e–0x009f` — really the *current job* of a 64-entry pool (§16.3) |
| ~~Type 11 stash~~ **Type 10 (`Pcust peg`) stash** | name → job `+0x68`; buffers → `+0xac`, `+0xb0`, `+0xb4`, `+0xb8` *(mislabelled "Type 11" before 2026-09-20; §16.1)* |
| ~~Type 12 stash~~ **Type 11 (`Pcust morph`) stash** | name → job `+0xbc`; buffers → `+0x100`, `+0x108`, `+0x104`, `+0x10c` *(mislabelled "Type 12" before)*. **The real Type 12 (`Pcust creation morph`) stash:** name → `+0x15c`; buffers → `+0x1a0`, `+0x1a4` |
| **Customization asset assembler** | **`FUN_009f36d0`** — ~~the only function touching **all eight** stash slots~~ **the reader of every `Pcust` stash, IDs 9–13 (§16.6)** |

`FUN_009f36d0` is the assembler, not the deformer: it runs the material-block validator
and the full mesh machinery, then loads the morph through `FUN_0074e580`, but performs
**14 float operations and no integer-to-float conversion at all** — so the arithmetic is
not in it. **[CONFIRMED — disassembly.]**

### 12.4 What the next pass should do — DONE, see §13; the specific leads named here did not pan out

**This pass has since happened (§13), so the two items below are historical rather than
a live plan.** Recorded as run, not silently dropped: the *general* strategy stated here —
start from `FUN_009f36d0`'s own call list rather than a whole-binary scan — is exactly
what worked, but the *specific* signature and callee guesses below were not how the
kernel was actually found, and are marked wrong rather than quietly removed.

**The deformation kernel is the remaining target, and it is a callee (direct or deeper)
of `FUN_009f36d0`, not of the loader and not of the GPU class.** ~~The signature to look
for is the one this pass established the data requires: **integer-to-float conversion**,
a **stride of 12**, and a multiply against a descriptor float at record `+0x08` or
`+0x14`. `FUN_004bcb30`, `FUN_004bcca0` and `FUN_004bca00` are among its callees and sit
in the same code region as the animation math, which makes them the cheapest first
look.~~ **Wrong lead, kept visible rather than deleted: the real kernel is
`FUN_009f6d30`, reached not through those three functions but through
`FUN_009f36d0` → `FUN_009f9020` (a dispatcher that allocates a pool literally named
`"temp_morph_mesh"` and loops `count × 0xC`) → `FUN_009f6d30` itself (§13.1). None of
`FUN_004bcb30`/`FUN_004bcca0`/`FUN_004bca00` are on that path; the "same code region as
the animation math" guess was a coincidence of address proximity, not a real lead.**

**Do not repeat:** a whole-binary scan for "stride `0xC` + int-to-float + float multiply"
returns **272 functions** and ranks by size — no selectivity, the fourth time that shape
of predicate failed on 2026-09-11 (`HANDOFF.md` §5). Start from `FUN_009f36d0`'s own call
list, which the compiler wrote. **This part held up: the pool-name string and the exact
`× 0xC` loop stride, both read straight off `FUN_009f36d0`'s own call list, are what
actually identified the dispatcher and then the kernel (§13.1) — the failure was in the
guessed signature/callees above, not in this strategy.**

## 13. The dequantisation — RESOLVED from the runtime applier (2026-09-11, user-authorised)

**RESOLVED.** The dequantisation is fully recovered from the runtime applier, and the
12-byte element layout is settled — correcting two earlier field readings. This closes the
item §7 left open and §12 could not reach from the loader side.

### 13.1 The chain to the kernel

| Step | Function |
|---|---|
| Customization asset assembler | `FUN_009f36d0` |
| Kernel dispatcher — allocates a pool named **`"temp_morph_mesh"`** (`0x200000`), loops over `count × 0xC` | `FUN_009f9020` |
| **The deformation kernel** | **`FUN_009f6d30(target, srcPos, srcNrm, dst, float weight)`** |

The dispatcher's pool name and its `× 0xC` loop stride identify the path beyond doubt, and
the kernel's last parameter being a **float weight** is the slider value.
**[CONFIRMED — disassembly.]**

### 13.2 The element layout — corrected

```
+0  i16   quantised position delta, axis X
+2  i16   quantised position delta, axis Y
+4  i16   quantised position delta, axis Z
+6  u16   vertex index                      (already confirmed, §5)
+8  u8    biased normal delta, axis X
+9  u8    biased normal delta, axis Y
+10 u8    biased normal delta, axis Z
+11 u8    UNUSED
```

> **This corrects §3.5.** That table read `+8` as a 2-byte field "not characterized" and
> `+10` as a 2-byte field "always `< 4096`". Both were **wrong groupings**: `+8`–`+10` are
> three *individual bytes*, one per axis of a normal delta, and `+11` is padding.
> **The old `< 4096` bound was a consequence, not a property** — a `u16` read at `+10`
> spans the normal's Z byte (≤ 255) plus a zero pad, so it is always below 256.
> **Predicted before measuring and confirmed: byte `+11` is zero in
> 21,104,267 / 21,104,267 elements (100.0000%).**

### 13.3 The formula

```
// per target entry {u32 id, f32 hi, f32 lo, f32 hi2, u8 flags} at (obj+0x2C), count at +0x28
// (entry stride 0x40, field offsets per §14.4)
w = clamp01( (slider - lo) / (hi - lo) )
if (flags != 0):
    w2 = clamp01( (slider - lo) / (hi2 - lo) )
    if (w < w2):  w = -w2                  // sign flip, XOR 0x80000000: bidirectional slider

// per descriptor record: N = u16(rec+0x04), A = 3 floats at rec+0x08, B = 3 floats at rec+0x14,
// bulk pointer at rec+0x20; per element i at bulk + i*12:
v = u16(elem + 6)
pos[v].axis += ( i16(elem + 0,2,4) / 32767.0 )                  * w * A[axis]
nrm[v].axis += ( (u8(elem + 8,9,10) / 255.0) * 2.0 - 1.0 )      * w * B[axis]
```

All four constants were read from the image as **exactly** `32767.0`, `255.0`, `2.0`,
`1.0`; the sign-flip mask is `0x80000000`. **[CONFIRMED — disassembly.]**

**So §7's question about the two float triples is answered, and the answer is why it was
hard: they are not two parameters of one quantity.** `A` scales **position**, `B` scales
**normal**. `(min, extent)`, `(offset, scale)` and `(scale, center)` were all wrong
because they all presume one quantity. **§12.1's search for an algebraic identity between
the triples correctly found none — there is no relation to find**, which retrospectively
validates that negative rather than merely excusing it.

### 13.4 Empirical confirmation over 21,104,267 elements

| Measurement | Result |
|---|---|
| byte `+11` zero (predicted by the formula) | **21,104,267 / 21,104,267 = 100.0000%** |
| decoded **position** delta magnitude | median **0.0037**, p90 0.0333, p99 0.0678, max 3.36 |
| position deltas within 0.25 units | **99.85%** |
| decoded **normal** delta magnitude | median 0.0412, p99 0.4806, **max 2.00000** |
| `B` triple maximum | **2.00000** — identical to the normal-delta maximum, as the formula requires |

Position deltas of a few millimetres to a few centimetres are physically right for facial
and body sliders; the ~0.01% reaching 3.36 units are body-scale targets. The normal
delta's maximum equals `max(B)` exactly, which is forced by
`(u8/255)×2−1 ∈ [-1, 1]` — an internal consistency check that costs nothing.

**Range check refutes the obvious alternative with no statistics at all:** had the
position term been `i16 × A` without the `/32767`, the same data would yield deltas up to
**110,117 units**. **[CONFIRMED — disassembly + empirical.]** Harness `morph_decode.py`.

### 13.5 What remains open

1. **The slider→weight remap is read but not empirically verified** — no shipped file
   carries slider values, so `hi`/`lo`/`hi2` and the bidirectional branch are
   **[CONFIRMED — disassembly]** only. **§14.4 reads the complete kernel (this section
   read it partially) and confirms the same formula field-for-field, adding the exact
   entry stride and offsets; the CONFIRMED-disassembly ceiling stated here still holds
   and is restated explicitly in §14.4 — no shipped file will ever carry a slider value.**
2. **Whether normals are renormalised after accumulation.** The kernel adds deltas; with
   `max(B) = 2.0` a sum can leave the unit sphere, so a later normalise is likely but was
   not traced. **[OPEN.]** **RESOLVED by §14.2, from raw assembly at the call site:
   renormalisation runs exactly once per vertex, on the fully-accumulated vector, at the
   point of encoding to the output vertex buffer. [CONFIRMED — disassembly.]**
3. **The 43% of descriptors with `N = 0`** remain as §3.4 describes — declared targets
   touching no vertices. Unchanged by this pass. **Still open — not addressed by §14
   either.**

## 14. Renormalisation order and the slider-to-weight remap, resolved from raw disassembly (2026-09-12)

**Resuming a died session**: an earlier run on this exact task reached one specific,
correctly-diagnosed blocker — "`FUN_00da05a0` is a square-root-based normalise... the
decompiler lost the pointer argument [at its call site]... get the raw assembly" — and
died on an API rate limit before acting on its own next step. Everything below starts
from that note rather than re-surveying. It also inherits, and now fully reads, decompiled
output the died session had already produced but not yet interpreted (`FUN_009f6d30`'s
and `FUN_009f9020`'s full bodies, and `FUN_009f7690`'s full body) in
`scratchpad\morph\slider_probe.txt` and `scratchpad\morph\consumer_clean.txt`.

### 14.1 The consumer chain, precisely

```
FUN_009f36d0  (assembler)
  -> FUN_009f9020  (dispatcher: zeroes float3 accumulation buffers, walks 14 slider
                     BUCKETS, and within each bucket calls the kernel once per SLOT --
                     see §14.6 for why "bucket" and "slot" are two different structures)
       -> FUN_009f6d30  (KERNEL: slider -> weight -> accumulate into the float buffers)
       -> FUN_009f7690  (CONSUMER, called exactly once, after every bucket's every slot
                          has finished accumulating -- not once per target)
            -> FUN_00da05a0  (normalise, per vertex, gated on the channel having a
                               normal at all)
            -> FUN_0047fec0  (byte-biased pack: round((v*0.5+0.5)*255) per axis --
                               confirmed the exact algebraic inverse of the `.cmorph_pc`
                               normal-delta decode already in §13.3)
```

`FUN_009f6d30` (the kernel) contains **zero** calls to `FUN_00da05a0` or `FUN_0047fec0` --
checked directly by reading its full body and its callee list. Both live only in
`FUN_009f7690`, and `FUN_009f7690` is called only once per `FUN_009f9020` invocation,
strictly after the bucket/slot loop that calls the kernel has completed. **[CONFIRMED —
disassembly.]** That alone already rules out "renormalise per contribution": there is
structurally only one call site, reached once, after every active slider for that mesh
has already added its contribution.

What raw assembly had to settle was *which vector* that one call normalises, since
Ghidra's decompilation of `FUN_009f7690` prints the call to `FUN_00da05a0` with **zero**
visible arguments, despite `FUN_00da05a0`'s own decompiled signature (correctly recovered
from *its own* body) declaring a single register-passed (`__fastcall`) float-pointer parameter. A call whose
callee is known to take one pointer argument, printed with none, is not merely unclear —
it is proof the call site's argument-loading instruction was not attributed to the call,
which raw disassembly can recover directly.

### 14.2 The raw assembly at the call site — renormalisation settled, and the bias constant closed

Ghidra script `MorphAsmProbe.java` (pre-existing, written but never run by the died
session; run this session) dumped the instruction stream around every call to
`0x00da05a0`, `0x0047fec0` and `0x009f5e10` inside `FUN_009f7690`. The block below is
**the complete, address-contiguous instruction stream** from the `CALL 0x00da05a0`
through the `CALL 0x0047fec0` — every instruction in this range is shown, none elided.
(An earlier draft of this section quoted a hand-picked subset of these same lines and
presented it as if contiguous; the addresses didn't close and a reviewer caught it. This
is the full window instead, verified address-by-address against
`scratchpad\morph\asm_probe.txt`: each address advances by exactly its own instruction's
length, with no gap anywhere in the range.)

**One thing has to be flagged before reading it, or the second half looks like it reads
from nowhere: a stack-pointer adjustment of `0x10` at `0x009f7f0d` shifts the frame.** Every `[ESP+n]`
reference *after* that instruction is `0x10` lower in physical memory than the same
offset would have been *before* it — so `[ESP+0x68]`/`[0x6c]`/`[0x70]`, read after the
`SUB`, are the exact same three stack addresses as `[ESP+0x58]`/`[0x5c]`/`[0x60]`, read
before it. That is not a coincidence of naming; it is the compiler building the next
call's argument frame directly underneath the buffer it just finished with.

The trace runs address-contiguously from `0x009f7ebc` to `0x009f7f3c` with every
instruction in that range accounted for and none elided:

- `0x009f7ebc`–`0x009f7ec6`: the code takes the address of a 3-float stack buffer (at
  `ESP+0x58`) as the normalise routine's argument, first writing the accumulated Z normal
  component into the buffer's third slot, then calling `0x00da05a0` (the normalise
  routine) on that buffer in place — after the call the buffer holds the normalised X/Y/Z.
- `0x009f7ecb`: an x87 `0.0` is pushed; it survives untouched on the x87 stack until it
  becomes the fourth argument of the pack call at the very end.
- `0x009f7ecd`–`0x009f7eea`: the normalised X component is reloaded from the buffer into
  `XMM0`, widened to double precision into `XMM1`, added (in double precision) to a bias
  constant loaded once from `0x01176dd8` (exactly `0.001953125`, i.e. `1/512`) into `XMM0`,
  narrowed back to single precision, and written back over the same buffer slot.
- `0x009f7ef0`–`0x009f7f01`: the identical widen/add-bias/narrow sequence runs for the
  normalised Y component, reusing the same bias value still held in `XMM0` from the X step
  rather than reloading it.
- `0x009f7f0d`: the stack pointer is adjusted down by `0x10` bytes to build the next call's
  own argument frame — from this instruction on, every `[ESP+n]` reference used below is
  `0x10` lower in physical memory than the same numeric offset used above it, so
  `[ESP+0x68]`/`[0x6c]`/`[0x70]` after this point are the identical three physical stack
  addresses as `[ESP+0x58]`/`[0x5c]`/`[0x60]` before it — the compiler building the next
  call's argument frame directly underneath the buffer it just finished with, not a
  coincidence of naming.
- `0x009f7f10`: the pending x87 `0.0` is popped into what becomes the pack call's fourth
  argument slot.
- `0x009f7f14`–`0x009f7f1f`: the same widen/add-bias/narrow sequence runs for the Z
  component, and the biased Z value is stored to the stack (at the address that is,
  post-shift, the same physical location as the pre-shift `[ESP+0x60]`).
- `0x009f7f25`–`0x009f7f39`: the three biased components are reloaded from the stack, in
  the order Z, then Y, then X, and pushed via the x87 stack into the pack call's argument
  positions in the order X, Y, Z.
- `0x009f7f3c`: `0x0047fec0` (the byte-biased pack routine) is called with the four
  arguments just built — biased X, biased Y, biased Z, and the literal `0.0`.

This closes completely, register by register. `XMM1` at `0x009f7ee2` is not
unexplained: it is set three instructions earlier, at `0x009f7ed7`, when the
single-precision value just reloaded into `XMM0` from the buffer at `0x009f7ecd` is
widened to double precision into `XMM1`. The bias is loaded once, as a double, at
`0x009f7eda`, and stays in `XMM0` across all three axes — `0x009f7ee2`, `0x009f7ef9` and
`0x009f7f17` all reuse that same register without reloading it. Each axis follows the
identical shape: widen to double precision, add the double-precision bias, narrow back to
single precision (using two different narrowing opcodes for the same scalar operation at
`0x009f7ee6` and at `0x009f7efd`/`0x009f7f1b` — not a discrepancy), write back to the
buffer slot it came from. The fourth packed argument is a literal `0.0`, produced by an
x87 push rather than a memory constant; the x87 stack carries that single `0.0`
untouched the entire time, since no other x87 instruction executes anywhere between the
push at `0x009f7ecb` and the pop that consumes it at `0x009f7f10` — checked against
the complete address-by-address account above, not assumed.

**Two separate claims, two labels, on purpose:**

- **Order — `[CONFIRMED — disassembly.]`** All three components of `baseNormal +
  deltaNormal` are written to the stack buffer *before* the buffer's address is taken
  (`0x009f7ebc`) and the call to `0x00da05a0` is issued (`0x009f7ec6`); after the call,
  the same three addresses are reread. Since
  `FUN_00da05a0` normalises its argument in place (already fully decompiled,
  `scratchpad\morph\normal_clean.txt`), the values read back are the normalised
  vector, not the pre-call sum. This claim rests only on the store/`LEA`/`CALL`/reload
  pattern and does not depend on anything past the point the call returns.
- **Bias — `[CONFIRMED — disassembly.]`** The double constant at `0x01176dd8` —
  **exactly `0.001953125` = 1/512** — is added, in double precision, to each of the
  three *post-normalise* components before they are narrowed back to single and passed
  to `FUN_0047fec0`; the fourth packed argument is a literal `0.0`. This is now shown
  completely (every register feeding the three `ADDSD`s is accounted for above) rather
  than resting on an annotation next to a call whose full argument-build wasn't shown.

**Renormalisation therefore runs exactly once per vertex, on the fully-accumulated
`(base normal + every active slider's summed delta)` vector, at the point of encoding
into the packed output normal — i.e. on the way out, after all contributions for that
vertex have already been summed, never per-contribution and never in a separate pass
between accumulation and encode.** `max(B) = 2.0` (§13.4) means the pre-normalise sum can
and does leave the unit sphere; this is exactly what the renormalise call exists to
correct, once, at output time. A reimplementation that renormalises before packing but
omits the `1/512` bias will differ from the game by that amount per component — small,
but exact, and now demonstrated rather than merely asserted.

**What a failing case (per-contribution renormalisation) would have looked like, and did
not:** a call to `0x00da05a0` reachable from inside `FUN_009f6d30`'s per-element loop, or
multiple calls to it per vertex inside `FUN_009f7690`. Neither exists — `FUN_009f6d30`
calls it zero times (checked against its full callee list), and `FUN_009f7690`'s own
per-vertex loop contains exactly one call to `0x00da05a0`, gated once per vertex on two
conditions together: a per-channel byte flag confirming the channel carries a normal at
all, and a non-null check on the destination normal-buffer pointer (`consumer_clean.txt`
line ~633).

### 14.3 The accumulation buffer is float end-to-end; the buffer is re-quantised exactly once, on the way out

`FUN_009f6d30`'s destination stores unconditionally read the destination's current float
value, add the freshly-computed double-precision contribution to it, and write the float
sum straight back to that same destination, for every position and normal write
(`slider_probe.txt` lines 261–301) — never an integer or byte store, never a rounding op
between one target's contribution and the next. `FUN_009f7690` reads those same buffers
(the dispatcher's own two accumulation-buffer stack locals, passed through unchanged)
directly as float pointers, with **no intermediate re-encode** between the kernel's last
write and the consumer's read.
**[CONFIRMED — disassembly.]**

The only re-quantisation in this entire chain is the single `FUN_0047fec0` call examined
above, which packs the **post-normalise, post-bias** float triple into the same
255-biased unsigned-byte encoding already documented as the `.cmorph_pc` payload's own
normal-delta decode inverse (§13.3: `(u8/255)*2-1`; the encoder computes
`round((v*0.5+0.5)*255)`, its exact algebraic inverse). **What a failing case (buffer
re-quantised between contributions) would have looked like:** the kernel's destination
pointer typed and stored as a byte/short with a round-trip pack/unpack inside the
per-target loop. It is not; the loop body has no integer truncation at all on the
destination side.

**A bonus, tangential finding on position, cross-referenced rather than restated in
depth (it belongs to `spec-vertex-format.md`, not here):** position gets the identical
treatment — a max-abs pass over `(basePos + deltaPos)` per axis (gated on layout code,
`consumer_clean.txt` lines 241–500) computes a **per-mesh, per-axis adaptive scale**
(floored at `DAT_012a2f84` ≈ 1.0e-5, and for layout code `0xf` further multiplied by
`DAT_01176de0` = 1/32768), stores it at the mesh header's `+0x58`/`+0x60`, and every
vertex's position is divided by that scale immediately before being packed (clamped to
±32767 for layout `0xf` via `FUN_009f5e10`, or stored as a raw float for layout `2`).
Same shape as normals: float accumulation throughout, one re-quantisation at the very
end, using a scale computed from the accumulated data itself rather than a fixed
constant. **[CONFIRMED — disassembly; flagged for `spec-vertex-format.md`, not developed
further here since it is outside this component's scope.]**

### 14.4 The slider→weight remap — the full kernel, not a reconstruction

§13.3 stated the remap formula as recovered from a (correctly identified but partially
reconstructed) reading of the kernel. This session read `FUN_009f6d30` in full
(`slider_probe.txt` lines 65–335) and it reproduces §13.3's formula exactly, term for
term, with no discrepancy — and adds the one thing §13.3 did not carry: **the exact
stride and intra-entry byte offsets of the per-slider target-entry array**, which §13.3
described only as "per target entry `{u32 id, f32 hi, f32 lo, f32 hi2, u8 flags}` at
`(obj+0x2C)`, count at `+0x28`" with no stated stride. This corrects/completes that
sentence in place (per this project's rule that corrections stay visible, not swapped):

**Correction to §13.3 — the entry stride is `0x40` (64) bytes, not implied by the
fields alone**, with those fields at exact fixed offsets inside it:

```
+0x00  u32  id
+0x04  f32  hi
+0x08  f32  lo
+0x0c  f32  hi2
+0x14  u8   flags
       (0x15..0x3f: 43 bytes not read by this kernel -- OPEN, plausibly a UI label
        or icon reference, not consumed by the deformation path at all)
```

Argument 2's `+0x28` (a count) and `+0x2c` (a pointer to this array) belong to a
*different*, enclosing 0x30-byte structure this document calls a **slider slot** (one
per UI slider) — not to be confused with the **slider bucket** (the 14-entry outer
table, §14.6), which is a different structure at a different stride holding an array of
slots. **[CONFIRMED — disassembly.]**

The weight computation itself, read directly rather than reconstructed:

```
w  = clamp01( (slider - lo) / (hi  - lo) )
if flags != 0:
    w2 = clamp01( (slider - lo) / (hi2 - lo) )
    if w < w2:  w = -w2        // IEEE-754 sign-bit XOR, mask 0x80000000 (DAT_01243130)
```

identical to §13.3, now confirmed against the complete function body rather than a
partial read. **[CONFIRMED — disassembly.]** Per the task's own framing, this **cannot
reach CONFIRMED — empirical**: no shipped `.cmorph_pc` (or any other file this project has
found) carries a live slider value, since sliders are pure runtime UI/customization
state, not stored asset data. That ceiling is stated explicitly here rather than implied.

**The bidirectional-slider hypothesis is CONFIRMED, not merely plausible.** `lo` is
literally the *shared pivot* subtracted in **both** ratios — the same subtraction, the
same denominator role, just against two different far endpoints (`hi`, `hi2`). `flags`
marks a target entry as two-sided; the two sides are **not two different morph targets**
but the **same** target entry (same `id`, same descriptor, same `A`/`B` scale triple),
driven by a weight whose sign encodes which side of the neutral pivot the slider sits on.
This is a textbook bidirectional slider centred on a neutral pose, exactly as the task
description hypothesised, now confirmed from the branch structure itself rather than
inferred from the sign-flip alone.

**Whether a curve precedes the linear interpolation, or `lo`/`hi`/`hi2` is the entire
remap: CONFIRMED — disassembly that it is the entire remap.** Between reading argument 5
(the slider float) and producing the final `w`, the kernel calls nothing but
`FUN_0074e710` (an *id lookup*, not a value transform — see §14.6) and the two helper
reads for the target's own directory entry. There is no call to any table-lookup,
spline, or easing function on the weight's value path. The two divisions and two
`clamp01`s are the complete transform.

### 14.5 Where the slider value lives, and what remains genuinely open

`FUN_009f9020` takes two arguments. Argument 2 is the mesh/morph-block being processed (its
`+0x78` byte carries the two gate bits already required for this whole path to run, and
its `+0x58` field is a **small integer handle** (`< 0x100`) into the free-list *pool
registry* — the same intrusive-pool idiom already documented at §8 for the type-7
constructor. `FUN_0074e490`/`FUN_0074e710` (used inside the kernel) index that pool
directly: `(&DAT_015bd398)[handle * 0x15]`. **[CONFIRMED — disassembly.]**

Argument 1 is a **separate** object, first visible only as a bare parameter. The
dispatcher reads the live slider value from it as:

The slider's own 48-byte **slot** (§14.6) carries, at its own `+0x24` field, an index
(`sliderIndex`) into a separate array. That array's base pointer is stored inside the
sub-object reached through argument 1's own `+4` field, and the array itself begins `8`
bytes past that base; the current slider value is then the first float of the 12-byte
record at position `sliderIndex` within that array (i.e. `sliderIndex * 0xc` bytes past
the `+8` starting point).

i.e. an array of **12-byte (3-float) records**, based at a fixed `+8` offset inside a
sub-object reached through argument 1's own `+4` field, one record per slider, selected by
an index carried in that slider's own 48-byte **slot** (§14.6 — distinct from the 14-entry
**bucket** table). **This is the disassembly-level
answer to "where does the slider value come from": a runtime array of per-slider
records, of which only the first float (the current value) is read by this deformation
path.** **[CONFIRMED — disassembly, for the *mechanism*.]**

**Genuinely open, and stated precisely rather than guessed:**

1. **The other two floats of each 12-byte record** (offsets `+4`/`+8`, relative to the
   record's own start) are never read by this kernel. Plausibly a min/max pair, or a
   smoothing target/velocity for animated slider transitions — not traced. **[OPEN.]**
2. **The stored numeric range of the slider value itself** cannot be determined from this
   executable at rest: the backing array is runtime-populated (all-zero in the static
   image, exactly as every other runtime pointer/value slot in this project reads zero on
   disk) and no shipped file carries a value either (§14.4). Recovering an actual range
   needs either live/dynamic inspection of a running game, or an authored slider
   min/max table in an unblocked `.xtbl` (the DLC-archive workaround already used for
   `customization_items.xtbl` in `spec-customization-data.md` §1 — not attempted this
   pass; flagged as the concrete next step). **[OPEN / UNKNOWN.]**
3. **Whether argument 1 is, or derives from, the documented customization singleton
   `DAT_0263e0f4`** (spec §12.3): NOT settled to CONFIRMED. A targeted textual scan
   (`MorphSliderSingletonCheck.java`) found that, of the three candidate functions on the
   call path (`FUN_009f36d0`, and its own two callers `FUN_009f46f0`/`FUN_009f4550`),
   only the assembler `FUN_009f36d0` references `DAT_0263e0f4` at all, and it does so
   near the very end of its body via a pointer-identity compare of `EBP` against
   `DAT_0263e0f4` (twice, at `0x009f443c` and `0x009f44a0`) followed by a store of
   register `ESI` into `DAT_0263e0f4` at `0x009f44a8` — ~~a compare-then-store
   shape consistent with "keep the singleton pointed at the object currently being
   assembled." That is suggestive but not proof: the actual instruction that loads
   `FUN_009f9020`'s first argument was not located (it lies elsewhere in
   `FUN_009f36d0`'s ~600-line body, only the first 90 of which were decompiled this
   pass). **[HIGH CONFIDENCE — inferred that `DAT_0263e0f4` is the same object or
   closely related to it; not CONFIRMED — disassembly, stated honestly rather than
   rounded up.]** Concrete next step: decompile `FUN_009f36d0` past line 90 to its
   `CALL ...009f9020`, or grep its raw instruction stream directly for the argument-load
   immediately preceding that call.~~

   **RESOLVED 2026-09-14 — `FUN_009f36d0` decompiled/disassembled past this point to
   locate the argument-load instruction, per the concrete next step above. Two findings,
   both from raw disassembly (`MorphSliderArgTrace.java`/`MorphSliderEbpTrace.java`,
   `tools/gp_morph2`), correcting/completing the paragraph struck through above:**

   **(a) The store of register `ESI` into `DAT_0263e0f4` at `0x009f44a8` stores zero,
   not the object pointer `EBP` — `ESI` was zeroed four instructions earlier at
   `0x009f4455`, and the store is gated on a compare of `EBP` against the current value
   of `DAT_0263e0f4`, skipped when they differ (the branch sits at `0x009f44a0`/
   `0x009f44a6`). This is a conditional **clear** ("if I am currently the
   registered singleton, un-register me"), not a "point the singleton at the object
   being assembled" store as the textual scan alone suggested — it immediately follows
   `EBP` linking itself into a *separate* sibling registry, a doubly-linked list rooted
   at the neighbouring global `DAT_0263e0fc` (fields `+0x1b4`/`+0x1b8` of the object in
   `EBP` serving as prev/next, `0x009f4444`-`0x009f449a`): an unregister-from-one-list/
   insert-into-another idiom, not a constructor pinning the active singleton.
   **[CONFIRMED — disassembly; corrects the prior pass's "consistent with
   keep-pointed-at" reading.]**

   **(b) `FUN_009f9020`'s first argument — item 3's actual question — is
   the 4-byte field at offset `+4` inside the object that is `FUN_009f36d0`'s
   OWN incoming first parameter, not `DAT_0263e0f4` and not a fresh reload of it.** The
   one and only call to `FUN_009f9020` inside `FUN_009f36d0` (confirmed by a full-body
   raw-asm scan for calls resolving to `0x009f9020` — exactly one hit) is at `0x009f3e64`;
   the instruction immediately loading its first argument, at `0x009f3e5b`, reads register
   `EAX` from offset `+4` of the object in `EBP`, three instructions before a cdecl call
   sequence that pushes `EBX` then `EAX` and calls `0x009f9020` (cdecl: last-pushed `EAX`
   is `FUN_009f9020`'s argument 1, first-pushed `EBX` is its argument 2 — confirmed as the
   mesh/morph-block of this section's own description, since the two instructions
   immediately before the argument load store `EAX` into `EBX`'s own `+0x58` field and OR
   bit `0x10` into `EBX`'s own `+0x78` byte). `EBP` itself is `FUN_009f36d0`'s own argument 1,
   register-pinned for the function's entire 3706-byte body: loaded exactly once, at
   the prologue — an instruction at `0x009f370b` that loads `EBP` from the stack at offset
   `+0x28c`, immediately followed by a zero-check-and-branch on `EBP` matching the
   decompiled null-check early exit on that argument (branch target `LAB_009f4528`) — and an exhaustive scan of every
   instruction in the function found no other write to `EBP` anywhere — so `EBP` at the
   call site is provably the same value as `EBP` at the later pointer-identity-compare
   sites against `DAT_0263e0f4`, with no register-reuse ambiguity in between.
   **[CONFIRMED — disassembly: the argument-load instruction is located, and the
   argument is a field of `FUN_009f36d0`'s own argument 1, not `DAT_0263e0f4` itself and
   not freshly read from it.]**

   **What this settles, and what it still doesn't.** Item 3's literal question —
   whether `FUN_009f9020`'s first argument *is* `DAT_0263e0f4` — is now answered:
   **no, it is not; it is one field (`+4`) inside `FUN_009f36d0`'s own argument 1
   object**, per (b). But (b) sits right next to (a): that same `FUN_009f36d0`
   argument 1 (`EBP`) is, a few dozen instructions later in the identical function
   invocation, checked by raw pointer identity against `DAT_0263e0f4` — a check only
   meaningful if argument 1 belongs to the same tracked-singleton class of object
   `DAT_0263e0f4` addresses. Whether argument 1 (`EBP`) actually *equals* `DAT_0263e0f4` at
   the moment of the `FUN_009f9020` call is a runtime fact no static disassembly read
   can settle, so that narrower claim stays **[HIGH CONFIDENCE, not CONFIRMED —
   `FUN_009f36d0`'s argument 1 is of the same object-class/registry `DAT_0263e0f4` tracks,
   evidenced by the direct identity compare against it inside the same function; this is
   a different, weaker claim than "argument 1 IS DAT_0263e0f4 at the time of the
   FUN_009f9020 call," which is runtime-state-dependent.]** No further static step is
   identified that would close this specific remaining gap — it would need dynamic/live
   inspection (break at `0x009f3e64`, read `EBP` vs. `DAT_0263e0f4` directly), consistent
   with item 2's own note that some of this subsystem's state is only recoverable at
   runtime. **[OPEN, precisely bounded.]**

### 14.6 What selects which part a morph block applies to, and the contributor bound

**Naming, fixed here because two different structures were both being called "slider
group" earlier in this section — corrected in place rather than left to cause a
mismatch later, per this project's naming-collision lesson (`HANDOFF.md` §5, the
anim-spec "sample bytes" vs "control record bytes" incident):**

- **Slider bucket** — one of the **14 entries** in table `DAT_02652ec0`, stride `0x10`
  (`{ptr, i16 count, ...}`). Each bucket owns an array of slots.
- **Slider slot** — a **48-byte (`0x30`)** record inside one bucket's array, one per UI
  slider. Carries the index into the runtime slider-value array (`+0x24`, §14.5) and,
  separately, a **count** (`+0x28`) and **pointer** (`+0x2c`) to an array of target
  entries.
- **Target entry** — a **64-byte (`0x40`)** record inside one slot's array:
  `{id, hi, lo, hi2, flags, ...}` (§14.4).

Three distinct strides (`0x10`, `0x30`, `0x40`), three distinct structures, used
consistently by these three names for the rest of this section (and retroactively
disambiguated in §14.1/§14.4/§14.5 above).

**Selection mechanism: CONFIRMED — disassembly, and simpler than a routing table.**
`FUN_009f9020`'s outer loop walks all 14 slider **buckets** and, within each, every
**slot** — **unconditionally** for every mesh/morph-block passed in — there is no
part-type or category branch in the dispatcher at all. The only gate is the per-target
**id lookup** inside the kernel: `FUN_0074e710(handle, id)` returns `-1` whenever the
current mesh's own directory (§3.3's `{id, n, ...}` entries) does not contain that id,
and the kernel's entire accumulation block for that target entry is skipped on `-1`.
**In other words: selection is purely data-driven by which target ids a given
`.cmorph_pc` file's directory happens to declare — every slot in every bucket is "tried"
against every mesh, and the vast majority are no-ops for any given part because that
part's morph file simply doesn't declare the matching id.**

**One slider (one slot) can drive multiple morph targets: CONFIRMED — disassembly.**
Each 48-byte **slot** carries its own `count` (`+0x28`) and pointer (`+0x2c`) to an array
of the 64-byte **target entries** from §14.4 — i.e. **one weight value fans out to every
target entry in that slot's own list**, each independently id-matched against the
current mesh. This is a genuine one-to-many structure (one slider, several named blend
shapes), confirmed from the kernel's own loop bounds and pointer dereference, though no
population count can be given: the bucket table is runtime-populated and reads as
all-zero in this static image, so "how many targets does a typical slider drive" is a
mechanism claim, not an "N of M" figure — stated honestly rather than invented.

**Whether the engine bounds simultaneous contributors per vertex: CONFIRMED —
disassembly, and the answer is that it does not.** The kernel's per-element write is an
unconditional `+=` into the shared position/normal buffers, indexed by the element's own
vertex index, with **no contributor counter, no maximum-active-target check, and no
per-vertex overflow guard anywhere in `FUN_009f6d30`, `FUN_009f9020`, or `FUN_009f7690`.**
Any number of active sliders whose sparse element lists happen to touch the same vertex
index all land on the same float, summed in whatever order the dispatcher's bucket,
slot and target loops visit them. **What a bound would have looked like, and does not
appear anywhere in the three functions read in full:** a comparison of a running
per-vertex count against a limit, or a fixed-size per-vertex accumulator array with a
rejection path on overflow.

### 14.7 Methodology notes for §5

*(§5 here means `HANDOFF.md` §5, the methodology notes — not this document's §5.)*

- **A died session's precisely-stated next step is worth more than a fresh survey.**
  The prior session's diagnosis — "the decompiler lost the pointer argument at this one
  call site; read the raw assembly there" — was exactly right and named the exact
  function and the exact failure mode. Picking up from that single sentence, rather than
  re-deriving "is there a renormalise," is what made this a two-Ghidra-run task instead of
  a re-survey. When a died session leaves a specific, falsifiable next step, trust it and
  start there.
- **A call printed with the wrong arity is stronger evidence than a call that "looks
  unclear."** The call to `FUN_00da05a0` decompiled with zero visible arguments, while
  `FUN_00da05a0`'s own body — decompiled correctly — declares one `float *` parameter.
  That mismatch is not merely uninformative; it is proof the call site's argument-loading
  instruction specifically was not attributed, which tells you exactly what raw
  disassembly needs to recover (not "reread everything," just the few instructions
  immediately before the `CALL`).
- **Reading memory around an opaque call site settles what the call site's own decompiled
  line cannot.** The three stack slots written just before `CALL 0x00da05a0` and reread
  just after it were both legible in the surrounding decompiled/disassembled code even
  though the call's own arguments were not; the technique that resolved this was not "get
  `FUN_00da05a0`'s pointer argument" directly but "watch the memory addresses that must
  hold it, immediately before and after the opaque call."
- **A cheap textual scan can usefully bound a question to "very likely, not proven" before
  committing to a full decompile.** Grepping three candidate functions' raw disassembly
  text for one known address (`MorphSliderSingletonCheck.java`, under two minutes to
  write and run) found the one function that touches the documented singleton and the
  exact instructions it uses there, without decompiling a 600-line function in full. It
  did not settle the question outright — and is reported here as HIGH CONFIDENCE rather
  than CONFIRMED for exactly that reason — but it is a real, cheap intermediate step
  between "decompile everything" and "mark it OPEN and move on," and it is now available
  to whichever later pass wants to close the remaining gap. *(Its suggested "keep
  pointed at" reading was later refuted, §14.5 item 3(a) — a cautionary as well as a
  positive example.)*
- **Restate a prior finding's exact fields before trusting it, even when re-deriving it
  agrees.** §13.3's remap formula was already CONFIRMED and this session's full kernel
  read did not change it — but the earlier read had never stated the per-entry stride
  (`0x40` bytes) or the exact intra-entry offsets, only the fields' existence. Reading
  the *complete* function, not a re-derivation from the same partial view, is what
  surfaced the number; a confirmation that adds no new specificity is worth checking for
  exactly that gap.
- **A hand-picked subset of a fully-available contiguous trace is a self-inflicted
  version of the filtering problem, and it is worse than an honest filter.** §14.2's
  first draft quoted six lines out of a 34-instruction block that was *entirely already
  captured* in the same tool output, and presented them with a `...` as if a small
  intervening chunk had been cut. It hadn't — the omission was mine, made while copying,
  not a property of the capture. A reviewer found it from the addresses alone
  (`7ecd → 7eda → 7ee2 → 7ef0 → 7f07` don't advance by plausible instruction lengths) and
  from a register that appeared to be used before it was set (`XMM1` at the `ADDSD`).
  **When the complete evidence is sitting right there in the same file, quoting less of
  it is not a simplification, it is a fabrication risk with no upside** — the fix was not
  "label it as filtered" but "paste the whole thing," which is what actually happened
  once checked, and it was both more convincing and shorter to defend than the trimmed
  version had been. Check whether a full trace is already in hand before deciding a
  window needs trimming at all.
- **A skipped instruction can hide a register write that closes an apparent gap, and a
  skipped stack-pointer adjustment can make correct code look like it reads from
  nowhere.** The
  specific instructions dropped from the first draft included the one that widens `XMM0`
  to double precision into `XMM1` (making the later add into `XMM1` legible) and the one
  that explains why later addresses are `0x10` higher than earlier ones referring to the
  same memory (a stack-pointer adjustment of `0x10`, a frame shift for the next call's own
  argument-build). Neither
  omission changed any conclusion — both were mechanical restatements of the same normal
  buffer — but both, missing, made a correct trace look broken. **A frame-pointer or
  stack-pointer adjustment instruction is never safe to elide from a window that spans
  it**, even when it looks like housekeeping, because every address on either side of it
  is only meaningful relative to it.
- **The same name for two different structures at two different strides is a defect even
  when every individual sentence using it is locally correct.** §14 called both the
  14-entry `DAT_02652ec0` table (stride `0x10`) and the 48-byte per-slider record inside
  it (stride `0x30`) a "slider group" in different places. Each sentence, read alone, was
  unambiguous from context; read together, "group" named two things one containment
  level apart, which is exactly the shape that broke a *different* team's replication of
  a *correct* finding in `spec-anim-format.md` ("sample bytes" vs "control record bytes",
  `HANDOFF.md` §5). **Fixed here by giving every structure a name tied to its stride**
  (bucket = `0x10`, slot = `0x30`, target entry = `0x40`) and stating all three strides
  together once, so a reader can check the names against the numbers instead of trusting
  prose continuity.

## 15. Vertex-index correspondence to `.ccmesh_pc`/`.gcmesh_pc` -- RESOLVED empirically (2026-09-13)

**RESOLVED empirically (2026-09-13).** §11 item 6 asked whether a morph record's
vertex index `i` (§3.5, confirmed at element `+6`) addresses the same vertex as
index `i` in the paired `.ccmesh_pc`/`.gcmesh_pc`'s own vertex array — the
straightforward reading — or some indirection (offset, remap table, a different
array such as a skinning-order one). **It is the straightforward reading.**
Tested purely by data analysis, no Ghidra needed: the empirical signal was clean
enough that the "if ambiguous, escalate" branch of the task was never reached.

### 15.1 Method

1. Decode each candidate mesh's real vertex positions from the `g`-file's
   position-bearing channel, using the already-confirmed `spec-vertex-format.md`
   machinery (`vertex_decl.walk_to_mesh` for the Mesh sub-block header,
   `vertex_attr.chan_offsets` for the channel's byte offset in the `g`-file, then
   `float3` at element offset 0 per §6.1 of that spec).
2. Decode each morph target's affected-vertex index list per this document's own
   §3.5/§13.2 (`u16` at element `+6`, already CONFIRMED — empirical as *the*
   index field; what was untested until now is what array it indexes *into*).
3. **Bounds check**, population-wide: does every target's maximum index stay
   below the *real, decoded* vertex count of its paired mesh?
4. **Geometric compactness test**: for each target with `2 ≤ N ≤ 400` (sparse
   enough to plausibly be a local feature, not a whole-body slider), compute the
   bounding-box diagonal of `{basePos[i] : i in indices}` under the direct
   mapping, and compare it against a **shuffled control** — the identical `N`,
   but with `indices` replaced by a random sample of that size drawn from the
   mesh's real vertex range (30 trials, averaged). This is exactly the control
   the task specified: if the true mapping were an offset, a remap table, or a
   different (e.g. skinning-order) array, then applying the *real* per-vertex
   index values from a `.cmorph_pc` directly against *this* mesh's position
   array should produce a geometrically meaningless, effectively random subset
   — indistinguishable from the shuffled control. A systematic gap between the
   two is the signal.
5. Two concrete case studies, chosen for having an independently-checkable
   geometric landmark (per the task's own suggested method): the player-
   customization base body meshes (`cm_body`/`cf_body`, full standing-figure
   topology, 7,977 vertices) and the dedicated 95-target player face morph
   (`cm_pc_fac.cmorph_pc`) against that same base mesh.

Harnesses: `morph_vidx_check.py` (population bounds check + compactness test),
`morph_vidx_body.py` (the `cm_body`/`cf_body` case study), `morph_vidx_face.py`
(the `cm_pc_fac` case study, including its own shuffled control).

### 15.2 Population-wide bounds check

Across every `.cmorph_pc` found bundled together with a `.ccmesh_pc`/`.gcmesh_pc`
pair in the same `custmesh_*.str2_pc` container (`customize_item.vpp_pc`,
`characters.vpp_pc`, `preload_items.vpp_pc`, `player_morph.vpp_pc`,
`customize_player.vpp_pc`, `dlc1`–`dlc3.vpp_pc`) — **1,606 bundles**, mesh
vertex counts spanning 22 to 9,676 (median 1,181):

| Test | Result |
|---|---|
| Targets with `N > 0` tested | **34,068** (11,310,109 elements) |
| Max index `<` the paired mesh's **real, decoded** vertex count | **34,068 / 34,068 (100.0000%)** |
| Max index `<` the Mesh-header's index-buffer count (the historically-mislabelled "vertex count" field, `spec-vertex-format.md` §1/§8 — included as a sanity check, not a sharp test) | 34,068 / 34,068 (100.0000%, uninformative here since `index_count ≥ vertex_count` for every shipped block) |
| **Control** — max index `<` a **randomly-paired wrong mesh's** vertex count (each morph file paired with a different, randomly-chosen mesh from the same population) | **22,764 / 34,068 (66.8193%)** |

**[CONFIRMED — empirical, whole matched population.]** Every single target, over
11.3 million decoded elements, addresses a vertex that actually exists in its own
paired mesh. The bounds check alone is necessary but not sufficient — the
shuffled-pairing control shows *why* it needs the compactness test alongside it:
even a **wrong** mesh pairing still satisfies the bound two-thirds of the time
by chance (most meshes in this population are large enough), so an in-bounds
result on its own is weak evidence. It rules out gross indexing failure (no
targets reference nonexistent vertices) without yet showing the mapping is
*correct*.

### 15.3 Geometric compactness test — the decisive control

For every target with `2 ≤ N ≤ 400` whose indices are in-bounds under the direct
mapping (**26,773** target instances across the population), the ratio of
`extent_direct` to the mean `extent_shuffled` (8 trials/target, same `N`, random
indices from the same mesh):

| Measurement | Result |
|---|---|
| ratio, median | **0.6308** |
| ratio, p10 / p90 | 0.1774 / 0.9866 |
| fraction of targets with ratio `< 0.8` | **71.84%** |
| fraction of targets with ratio `< 0.5` (twice as compact as chance or better) | **31.46%** |
| fraction of targets with ratio `> 1.0` (**less** compact than chance — what a wrong mapping predicts) | **2.08%** |

**[CONFIRMED — empirical.]** Under a wrong mapping (offset, remap, wrong array),
there is no mechanism that would make the resulting index set land on
spatially-clustered real vertices more often than a same-size random sample —
the two should be statistically indistinguishable, ratio ≈ 1 either side at
chance. Instead the population is overwhelmingly skewed toward *more* compact
than chance (median 0.63, and only 2.08% of targets land on the "less compact"
side that a wrong mapping would produce with roughly 50% frequency). This is the
exact control the task's methodology called for, run at population scale rather
than on a single hand-picked file.

### 15.4 Case study 1 — `cm_body` / `cf_body`, the full player base body mesh

`customize_item.vpp_pc`'s `custmesh_-233129239.str2_pc/cm_body.ccmesh_pc` (male)
and `custmesh_1632961868.str2_pc/cf_body.ccmesh_pc` (female) are both full
standing-figure meshes, 7,977 vertices, Y (vertical) range `[-0.004, 1.811]` /
`[-0.007, 1.788]` — matching the ~1.86 m standing-figure bounding box already
established in `spec-vertex-format.md` §6.1. Their own bundled morph files
(`cm_body_pc.cmorph_pc` / `cf_body_pc.cmorph_pc`, 119/118 targets with `N > 0`)
decode as follows, **using only the direct index mapping and real decoded
positions — no slider names, no id lookups**:

- **Every target whose affected-vertex bounding-box diagonal is under 0.15**
  units (a small fraction of the mesh's own ~2.3-unit diagonal) has its centroid
  in the **top 10–20% of the mesh's own vertical range** — 59/59 on `cm_body`,
  60/60 on `cf_body` — the neck/collar/upper-torso region for a body mesh whose
  head is otherwise represented by a separate file. **Reproduced independently
  on two unrelated files (male and female base bodies), each with its own
  distinct vertex data.**
- The handful of genuinely large-extent targets (indices 0–5 on both files)
  have bounding-box diagonals of 2.2–2.3 units, close to the mesh's own full
  diagonal — exactly the profile expected of whole-body scale/weight sliders,
  and clearly distinguishable from the localized group.
- **Control, stated explicitly per the task's method**: a wrong mapping (offset,
  remap, wrong array) gives no reason for "small-`N` targets" to cluster at one
  particular height band rather than anywhere on the mesh. Under the direct
  mapping, 100% of them do.

### 15.5 Case study 2 — `cm_pc_fac.cmorph_pc`, the dedicated 95-slider face file

This is the sharpest single result. `cm_pc_fac.cmorph_pc` (`player_morph.vpp_pc/
pc_face_morph.str2_pc/`, 95 targets, all with `N > 0`) is a **separate file, in
a separate archive, with no bundled mesh of its own** — exactly the case the
open item worried about, since there is no filename or container relationship
to lean on. Its global maximum vertex index is **7,976**.

- Against the two most obvious head-mesh candidates by name —
  `npc_basehead.ccmesh_pc` (1,426 vertices) and `npc_basehead_lod.ccmesh_pc`
  (311 vertices) — index 7,976 is **out of bounds on both**.
- Against `cm_body.ccmesh_pc` (7,977 vertices, §15.4) — the same full-body mesh
  the player customization screen uses as its base — index 7,976 is **in
  bounds, and is exactly `vertex_count − 1`**: the tightest possible bound, not
  a loose coincidence.
- Decoding all 95 targets' affected vertices against `cm_body`'s real positions,
  **every one of the 95 clusters tightly in the head region** (`Y` in roughly
  `[1.58, 1.72]`, against the mesh's full `Y` range of `[-0.004, 1.811]` — the
  top ~9% of the standing figure, consistent with a head sitting on a body that
  continues a little above it for hair). The shuffled control (30 trials/target,
  same method as §15.3) gives: **median ratio 0.0514, p90 0.0921, max 0.1319 —
  100/100 targets below 0.5, 0/100 above 1.0.** Every single target is at least
  ~7× more compact than a same-size random sample, several ×20 or more.
- **The decisive, non-statistical piece of evidence**: several target pairs are
  **bilateral mirror images of each other**, visible directly from the decoded
  centroids with no slider names involved — e.g. target #75 centroid
  `(+0.039, 1.689, 0.034)` against target #74 `(−0.040, 1.688, 0.034)`; target
  #73 `(+0.040, 1.688, 0.034)` against #76 `(−0.039, 1.688, 0.034)`; targets
  #87/#89 (`X ≈ +0.037`) against #88/#90 (`X ≈ −0.038`), all four at the
  identical height and depth to within 0.001–0.003 units. Target #93 and #94
  both centre on `X ≈ 0.000` — a midline feature, not a paired one. A left/right
  slider pair landing on geometrically mirrored vertex sets, matched in height
  and depth to a few millimetres, is not producible by an offset, a remap table,
  or an indexing error — it requires the indices to be addressing the actual
  left- and right-hand vertices of a real bilaterally-symmetric head mesh.

### 15.6 Conclusion, and the parallel to `spec-rig-format.md` §11.7 item 3

**The direct reading is CONFIRMED — empirical**, at population scale (§15.2,
§15.3) and by two independent, geometrically-verifiable case studies (§15.4,
§15.5) including one — `cm_pc_fac.cmorph_pc` — with no filename or bundling
relationship to its mesh at all, where the correspondence had to be established
purely from the index-bound coincidence and the geometry it produces. A morph
record's vertex index `i` addresses index `i` in the paired mesh's own vertex
array, full stop: no offset, no remap table, no per-channel indirection, and no
indexing into a different (e.g. skinning-order) array.

This document flagged, per the task brief, whether the same resolution that is
still open for `spec-rig-format.md` §11.7 item 3 (whether a *different*
consumer's gather is indexed by rig-bone order or by track order, because the
straightforward reading was refuted by that document's own §11.4) would apply
here too. **It does not — that is the point of contrast worth recording.** The
rig case remains open precisely because its straightforward reading failed a
direct test and an indirection had to be hypothesised to explain the conflict.
The morph case's straightforward reading did not fail: every test run above
supports it directly, with no residual to explain away. The two questions have
the same shape (does index `i` mean array position `i`, or something else?) but
resolved oppositely on their own evidence, and this document does not extend the
rig document's still-open indirection hypothesis to morphs — there is nothing
here for it to explain.

**Update:** `spec-rig-format.md` §11.4's verdict was retracted and its §11.7 item 3
marked RESOLVED (rig §11.15, same date): blend indices do go through the Mesh `+0x38`
bone palette, so the rig case is no longer open and the "opposite resolution" contrast
drawn above no longer holds as stated. The morph conclusion itself is unaffected.

### 15.7 What this does not settle

The morph *directory*'s `id`-based binding of a target to a *specific* mesh at
runtime (§3.3, §14.6 — "every slot in every bucket is tried against every mesh,"
gated by an id lookup) is unaffected by this result and remains as described:
this pass shows that *once* a target is bound to a mesh, its indices address
that mesh's real vertex array directly; it says nothing new about *which* mesh
a given target binds to at runtime (that mechanism was already resolved in
§14.6). The `cm_pc_fac` ↔ `cm_body` pairing used in §15.5 was established here
purely from the index-bound coincidence and the geometry it produces, not from
re-deriving the runtime id-lookup path — consistent with it, not a duplicate of
it.

## 16. Registered types 9–14 traced: the customization current-job record `DAT_0263e0f4`, the logo-record pool `DAT_0263e0e8`, and a corrected field map (2026-09-20)

`spec-format-inventory.md` listed IDs 9 `Pcust mesh`, 10 `Pcust peg` and 14 `Pcust logo peg` as "format yes; this variant's constructor untraced". This pass decompiled each constructor in full and read raw x86 at every call boundary. **Verdict: IDs 9, 10 and 14 are all stash-only — none of them reads a byte of its file.** That puts them in the same class as IDs 11, 12 and 13 (§8, §12.3, `spec-rig-format.md` §12), so the six `Pcust`-family constructors are one uniform "stash now, assemble later" mechanism. Along the way the pass found (a) that §12.3's ID→constructor labels were **shifted by one slot**, (b) that the "customization singleton" `DAT_0263e0f4` is really a **current-job pointer into a 64-entry pool**, not a single object, and (c) that the consumer `spec-rig-format.md` §12 called unidentified for ID 13's stash **is** `FUN_009f36d0`. All three are corrected here. Scripts: `tools/scripts/DecompileTaskS.java` (raw + decompile), `DecompileTaskS2.java` (one-global reference scan), `DecompileTaskS3.java` (linear disassembly of never-boundaried code); outputs `tools/taskS*_out.txt`.

### 16.1 Correction to §12.3: which constructor is which

§12.3's table gave the Type 11 constructor as `0x009f0910` (stash: name `+0x68`, buffers `+0xac..+0xb8`) and Type 12's as `FUN_009f0970` (name `+0xbc`, buffers `+0x100..+0x10c`). **Both were one slot early.** The ID→constructor pairing is read straight off the registration function `FUN_00700780` (decompiled in `tools/reg_table_full.txt`): each record assigns its type ID and its constructor address in the same block, next to the record's type-name bytes. ID 9 ↔ `LAB_009f08c0`, 10 ↔ `LAB_009f0910`, 11 ↔ `LAB_009f0970`, 12 ↔ `LAB_009f09d0`, 13 ↔ `LAB_009f0a20`, 14 ↔ `LAB_009f0a60`. It is cross-checked structurally: the six functions sit back-to-back in that order and write strictly ascending, non-overlapping job-record offsets (table below); and it agrees with §8's own counts (four buffers for type 11, two for type 12), which §12.3's table contradicted. The mislabel came from an earlier helper listing (`tools/pcust_ctors.txt`) that titled `009f0910` "type 11" and `009f0970` "type 12". **[CONFIRMED — disassembly.]**

| ID | Type | Constructor (body bytes) | Gate | Name copy (`0x41` bytes, bounded `strncpy` + NUL, `FUN_00da7930`) | a3 | a4 | a5 | a6 |
|---:|---|---|---|---|---|---|---|---|
| 9 | Pcust mesh | `0x009f08c0` (72) | a2 == job`+0x10` | job`+0x16` | `+0x58` | `+0x60` | `+0x5c` | `+0x64` |
| 10 | Pcust peg | `0x009f0910` (84) | same | `+0x68` | `+0xac` | `+0xb0` | `+0xb4` | `+0xb8` |
| 11 | Pcust morph | `0x009f0970` (86) | same | `+0xbc` | `+0x100` | `+0x108` | `+0x104` | `+0x10c` |
| 12 | Pcust creation morph | `0x009f09d0` (66) | same | `+0x15c` | `+0x1a0` | `+0x1a4` | — | — |
| 13 | Customization Rig | `0x009f0a20` (40) | same | none | `+0x1a8` | `+0x1ac` | — | — |
| 14 | Pcust logo peg | `0x009f0a60` (43) | `DAT_0263e0e8 != 0` only; a2 unused | none | rec`+0x4c` | rec`+0x50` | rec`+0x54` | rec`+0x58` |

Each body is: load the current-record global; (IDs 9–13) compare the incoming 2nd argument against job`+0x10` and return false on mismatch; copy the name; store the raw incoming values verbatim; return true. **No callee other than the bounded string copy; no buffer is dereferenced.** IDs 9–13 use job pointer `DAT_0263e0f4`; ID 14 uses a different global, `DAT_0263e0e8` (§16.5). **[CONFIRMED — disassembly, complete bodies read raw.]**

### 16.2 The constructor argument convention, and the gate

The generic dispatcher `FUN_00dd2e30` calls each constructor as `ctor(name, a2, a3, a4, a5, a6)`. `spec-resource-dispatch.md` §8.6 item 1 (agent T, same day) already established that a3/a4 are the primary buffer/size and a5/a6 the secondary buffer/size (the paired call; the unpaired call passes zeros for a5/a6). **This pass reproduces that independently from the consumer side**, which is a stronger check than reading the dispatcher again: (i) ID 23's constructor NUL-terminates `buf[a4-1]`, so a4 is a length; (ii) ID 3's constructor forwards `(a3, a4, a5, a6, name, flag)` to the shared PEG loader as `(c_buffer, c_size, g_buffer, g_size, …)`; (iii) the assembler's copy sites (§16.6) use each stash pair as `(src pointer, byte count)` with exactly these pairings; (iv) the code at `0x009f32f0` (§16.4) sums the `+0x60`/`+0xb0`/`+0x108` slots as the c-side total and the `+0x64`/`+0xb8`/`+0x10c` slots as the g-side total. **[CONFIRMED — disassembly.]**

**a2, the argument the constructors gate on, is the streaming *container* being loaded.** The dispatcher's only stack argument is forwarded unchanged to the constructor; its sole caller, the container load step `FUN_00db2ce0`, pushes its own `this` — a push of register `EDI`, followed by loading `ECX` from `ESI` and calling `0x00dd2e30`, at `0x00db2d44`. `spec-resource-dispatch.md` §8.6 calls it the "caller arg"; it is the container object. **[CONFIRMED — disassembly.]**

**job`+0x10` is the container the current job asked for.** It is written once, by the job scheduler `FUN_009f1730` (the store immediately precedes the container-start call `FUN_00dafea0(job+0x10)`, or `FUN_00daff10` on one path; the same field is later passed to the poll `FUN_00dafb60` (a result of `5` sends the job down the retire path) and the release `FUN_00dafad0`), and cleared to 0 when the job retires. The gate is therefore *"only stash if this is the container my job is waiting on"*: a Pcust-typed entry arriving in any other container makes the constructor return false, which `FUN_00db2ce0` reports as `Error calling load() function for streaming file (%s)`. The container-complete code at `0x009f32f0` repeats the same compare (`0x009f32f9`) against the container handed through the identity thunk `FUN_00c9b840` (a 2-instruction identity function returning its argument), and on mismatch sets job`+0x15 = 1` (failed). **[CONFIRMED — disassembly for the writer, both compares and the identity thunk; HIGH CONFIDENCE — inferred for the reading "container pointer", since the scheduler's stored value was traced to a request-descriptor field (`+4` or `+8`) and handed to container start/poll/release, not to an allocator.]**

### 16.3 What `DAT_0263e0f4` is: the current job of a 64-entry pool

`FUN_009f1370` (the subsystem initialiser) builds a pool of **64 job records of `0x224` bytes** at `0x02641720`–`0x0264a020` (`0x8900 / 0x224 = 64` exactly), labelled by the engine's own strings **"pcust comp %d"** and a **"component streaming"** memory pool of `0x450000` bytes. Jobs are chained by links at job`+0x1b4`/`+0x1b8`; `DAT_0263e0fc` is the free-list head, `DAT_0263e0f8` the pending-queue head, and **`DAT_0263e0f4` the *current* job — null while idle**. The scheduler pops the queue head into `DAT_0263e0f4` (when nothing is current), and every retire/cancel path (`FUN_009f2110`, `FUN_009f29f0`, `FUN_009f2fd0`, `FUN_009efc50`, `FUN_009eff50`, `FUN_009f0590`, and the assembler's own tail) releases the container handle (`FUN_00dafad0(job+0x10)`) and nulls `DAT_0263e0f4`; most of them also return the job to the free list. So the earlier "singleton" wording (§12.3, `spec-rig-format.md` §12) should be read as **"the current job record"**. The assembler `FUN_009f36d0` operates on a job passed as its argument 1 and un-registers it with a pointer-identity compare of that argument against `DAT_0263e0f4` followed by a clear at its end (§14.5 item 3); the ctors write through `DAT_0263e0f4`. That the two are the same object at run time remains a runtime fact (§14.5 item 3's bounded gap, unchanged), but the offsets line up field for field — every stash slot a constructor writes is read by the assembler at the identical offset, as a `(pointer, length)` pair. **[CONFIRMED — disassembly for the pool, lists and lifecycle; HIGH CONFIDENCE — inferred for identity of that argument and `DAT_0263e0f4`.]**

### 16.4 Job-record field map (everything with an identified writer or reader)

| job`+` | Meaning | Written by | Read by |
|---|---|---|---|
| `0x00` | request/asset descriptor pointer (dereferenced throughout) | queue insert (not traced); cleared by `FUN_009f2110`, `FUN_009efc50`, `FUN_009eff50` | assembler, scheduler, cancel paths |
| `0x04` | owner/context pointer; its `+0x14` is a slot index used in the per-slot tables at `DAT_0263dde0` and in the peg name (§16.6) | queue insert (not traced) | assembler, `FUN_009f1730` |
| `0x0c` | state (`1` set by the retire path `FUN_009eff50`; `== 3` tested by `FUN_009f1730`/`FUN_009f0590`) | `FUN_009eff50` | `FUN_009f1730`, `FUN_009f0590` |
| `0x10` | **container the job is waiting on** (§16.2) | `FUN_009f1730`; cleared by `FUN_009eff50`, `FUN_009f1730`, the assembler's tail | ctors 9–13 (gate), `0x009f32f0` (gate), `FUN_00dafb60`/`FUN_00dafad0` callers |
| `0x15` (byte) | failed flag | `0x009f32f0` (gate mismatch / budget overflow); assembler (`0x009f3a2a`, parse failure) | `FUN_009f1730` |
| `0x16`–`0x56` | name 1, `0x41` bytes — the base name everything is keyed by | **ID 9 ctor** | assembler (morph/rig/name keys) |
| `0x58` / `0x5c` / `0x60` / `0x64` | mesh c-buffer / g-buffer / c-length / g-length | **ID 9 ctor** | assembler `memcpy` pairs; `0x009f32f0` (lengths) |
| `0x68`–`0xa8` | name 2 (peg name) | **ID 10 ctor** | assembler (peg key) |
| `0xac` / `0xb0` / `0xb4` / `0xb8` | peg c-buffer / c-length / g-buffer / g-length | **ID 10 ctor** | assembler; `0x009f32f0` (lengths) |
| `0xbc`–`0xfc` | name 3 (morph name) | **ID 11 ctor** | assembler (morph key, `FUN_0074e580`) |
| `0x100` / `0x108` / `0x104` / `0x10c` | morph c-buffer / c-length / g-buffer / g-length | **ID 11 ctor** | assembler; `0x009f32f0` (lengths) |
| `0x15c`–`0x19c` | name 4 (creation-morph name) | **ID 12 ctor** | assembler |
| `0x1a0` / `0x1a4` | creation-morph buffer / length | **ID 12 ctor** | assembler (buffer passed to `FUN_0074e580(name, buf, 0, 1)`; the length slot is not read there) |
| `0x1a8` / `0x1ac` | rig buffer / length | **ID 13 ctor** | assembler (gated on request flag and `+0x1ac != 0`; copied, then `FUN_004bc810(name+suffix, slot, copy, length, 0)` — the rig cache-load wrapper, `spec-rig-format.md` §12) |
| `0x1b4` / `0x1b8` | pool list links | scheduler / retire paths | same |
| `0x1bc` (byte) | ready flag | `0x009f32f0` (after the size-budget check passes) | not traced |

Only the constructors write the stash slots among the direct users of `DAT_0263e0f4`; the other direct users' decompiles contain no access to those offsets (checked by pattern over all nine lifecycle functions). **[CONFIRMED — disassembly for every row's cited writer/reader; the two "not traced" cells are honest gaps.]**

**The undefined code at `0x009f32f0`–`0x009f34a0`.** Ghidra has no function here, which is why the previous "14 users" count missed five of the references. Its first instructions load `DAT_0263e0f4`, gate on the incoming container against job`+0x10`, then add the c-side lengths (`+0x108`, `+0xb0`, `+0x60`) and g-side lengths (`+0x10c`, `+0xb8`, `+0x64`) and compare them against per-type budgets in a table at `0x02668248` (20-byte rows) before setting the ready flag. **It is stored as a function-pointer immediate by `FUN_006ff730` (`0x006ff9a6`) — a *container-type* registration function, sibling of the resource-type registration `FUN_00700780`, which registers named container types through `FUN_00db1c90`.** The record it sits in is named **"Cust_Component"**; the sibling record **"Cust_Logo"** registers `FUN_009f0a50` (a 4-instruction function that stores `2` into the logo record's state field, §16.5), and "Cust_Composite"/"Cust_Shaderball" register other callbacks (`LAB_009f9b30`/`LAB_009f9c50`, `LAB_009f9c00`/`LAB_009f9c50`). `FUN_00db2ce0` invokes the container type's first function pointer with the container once every entry has loaded. So `0x009f32f0` is the **completion callback of container type "Cust_Component"** and `FUN_009f0a50` that of "Cust_Logo". **[CONFIRMED — disassembly for the stores and the strings; HIGH CONFIDENCE — inferred that the first function pointer of a container-type record is the completion callback `FUN_00db2ce0` calls (matching call shape, not read from the registration consumer).]**

### 16.5 The second pointer, `DAT_0263e0e8`: the logo-record pool (ID 14)

`FUN_009f1370` also builds a second pool: **64 records of `0x5c` bytes** at `0x0264a020`–`0x0264b720` (`0x1700 / 0x5c = 64`), links at record`+0x44`/`+0x48`, pending-queue head `DAT_0263e0ec`, free-list head `DAT_0263e0f0`, and **`DAT_0263e0e8` = the current logo record**. Its users (reference manager and a full operand scan agree, 26/26): `FUN_009f07d0` (lookup by the record's first dword over the current record and the pending list), `FUN_009f0a50` (sets `+0x40 = 2`), `FUN_009f0a60` (the ID 14 constructor), `FUN_009f1370`, `FUN_009f1730` (the scheduler, which starts the record's container by name via `FUN_00db3530`), `FUN_009f46f0` (§16.6).

| rec`+` | Meaning | Writer | Reader |
|---|---|---|---|
| `0x00`, `0x04` | request / owner pointers | queue insert (not traced) | `FUN_009f46f0` |
| `0x08`–`0x17` (count at `0x18`) | array of ids, at most four | not traced | `FUN_009f46f0` |
| `0x38` | container handle | `FUN_009f1730` (`FUN_00db3530(name)`) | `FUN_009f1730` (poll `FUN_00dafb60 == 5`) |
| `0x3c` | pointer to the container name string | not traced | `FUN_009f1730`, `FUN_009f46f0` |
| `0x40` | state: `0` idle, `2` loaded → apply, `3` fallback (`missing-black.tga` path) | `FUN_009f0a50` (`=2`), `FUN_009f46f0` (`=0`) | `FUN_009f46f0` |
| `0x44`/`0x48` | pool links | scheduler / `FUN_009f46f0` | same |
| `0x4c` / `0x50` / `0x54` / `0x58` | logo-peg c-buffer / c-length / g-buffer / g-length | **ID 14 ctor** | `FUN_009f46f0` |

ID 14's constructor has **no container gate and no name copy** — only a null test of `DAT_0263e0e8` — so *any* entry of type 14 that arrives while a logo record is current is stashed. **[CONFIRMED — disassembly.]**

### 16.6 What consumes each stash — and that it is the *sibling type's own parser*

The consumer of IDs 9–13 is the assembler `FUN_009f36d0` (two callers: `FUN_009f46f0` case 3 and the thin trampoline `FUN_009f4550`; the "PCC_CREATE/DELETE/SWAP/FINALIZE" debug labels in `FUN_009f46f0` name its event kinds). It copies the stashed blobs into two allocations it was handed (all c-side blobs into one, all g-side into the other, later blobs 16-byte-aligned after earlier ones) and then feeds each to the parser its sibling type uses:

| Stash | Fed to | Same parser as | Evidence |
|---|---|---|---|
| ID 9 mesh (`+0x58..+0x64`) | `FUN_00dd7d70(c, &cursor, 0)` → align cursor to 4 → `FUN_007527b0(c, &cursor, g, &out)` | **ID 5** (`FUN_00751f60`) — the same call sequence, the same argument shape, the same 12-instruction align-to-4 idiom (`0x00751f8c`–`0x00751faa` vs `0x009f3a67`–`0x009f3a85`) | raw x86, both sites. The one difference: ID 5 then registers the result by name (`FUN_005ce910(name, container, result)`); the assembler stores it in its output object and carries on (`FUN_00752fc0`). |
| ID 10 peg (`+0xac..+0xb8`) | `FUN_00dcefa0(name, c, c_len, g, g_len, 1)` → `FUN_00dce9f0` (magic `0x564B4547`, version 13, 72-byte record walk with pointer fix-up) → `FUN_00dce800` | **ID 3** (`FUN_005d7840` → `FUN_00dcf020` → the same `FUN_00dce9f0` → the same `FUN_00dce800`) | raw push order at `0x009f3a00`–`0x009f3a1e`; decompiles of both. Skipped silently if the g-buffer or g-length is zero. |
| ID 11 morph (`+0x100..+0x10c`) | `FUN_0074e580(name, c, g)` | **ID 7** — its constructor `FUN_0074e740` is literally `FUN_0074e580(name, c, g, 1)` followed by the by-name registration `FUN_005ce910` | decompile of both |
| ID 12 creation morph (`+0x1a0`) | `FUN_0074e580(name, buf, 0, 1)` | ID 7 | decompile |
| ID 13 rig (`+0x1a8/+0x1ac`) | `FUN_004bc810(name+suffix, slot, copy, length, 0)` | **ID 20** — `FUN_00748280` builds `name`+`DAT_0113a86c` (the same suffix constant the assembler uses), cache-checks, then calls `FUN_004bc810(name+suffix, a2, buf, len, 0)`: the same five-argument shape (the assembler passes the owner's slot index where ID 20 passes its a2) | decompile of both; supersedes the "consumer not identified" of `spec-rig-format.md` §12 |
| ID 14 logo peg (rec`+0x4c..+0x58`) | copied into the slot's two staging buffers (capacity-checked against slot`+0x18`/`+0x1c`), then `FUN_00dcefa0(name, c, c_len, g, g_len, 1)` — from `FUN_009f46f0` | **ID 3** (same `FUN_00dcefa0` chain as ID 10) | raw push order at `0x009f481e`–`0x009f4843` |

So **none of IDs 9/10/14 is a distinct format**: each is the sibling format's parse, reached late. IDs 10 and 14 register the peg under a synthesised name (`FUN_00dcefa0` with its flag set first claims a named slot via `FUN_00dcee60`; ID 10's name is the job's `+0x68` string with the slot index appended and the platform `.cpeg_pc` extension; ID 14's is built by `FUN_009ef900` as a `"%s_%d_%d.%s"` composite — its inputs were not traced). The assembled c-side region for one component is `[mesh c][peg c]`, then the morph blob at the next 16-byte boundary; the g-side region holds the mesh g blob, then the morph g blob at a 16-byte boundary (the peg's g bytes are copied to their own place before registration). **[CONFIRMED — disassembly.]**

### 16.7 The complete list of direct users of `DAT_0263e0f4`

**Seventeen functions, not fourteen** (55 references, reference manager and operand scan agreeing exactly): `FUN_009efc50`, `FUN_009eff50`, `FUN_009f0590`, `FUN_009f07c0` (returns whether a job is current), the five constructors `0x009f08c0`/`0x009f0910`/`0x009f0970`/`0x009f09d0`/`0x009f0a20`, `FUN_009f1370` (pool init), `FUN_009f1730` (scheduler), `FUN_009f2110`, `FUN_009f29f0`, `FUN_009f2fd0` (cancel/retire paths), the undefined `0x009f32f0` completion callback, `FUN_009f36d0` (assembler), `FUN_009f5220` (calls `FUN_009f46f0`, then `FUN_009ed530` when no job is current). The earlier 14 were the ones that were already Ghidra functions; `0x009f08c0` and `0x009f0a20` had no function and `0x009f32f0` still has none. **[CONFIRMED — disassembly.]**

### 16.8 What remains open

1. **How an entry gets typed 9–14 instead of 5/3/7/20.** The extensions are identical, so the type must come from the container (its container type, e.g. "Cust_Component"/"Cust_Logo", or the entry's own type byte in the manifest trailer, `spec-resource-dispatch.md` §8.5); which one was not traced. **[HYPOTHESIS — container type; OPEN.]**
2. Job`+0x00`/`+0x04`/`+0x0c` semantics beyond what §16.4 lists, and who enqueues jobs (the writer of the pending queue).
3. Whether the assembler's argument 1 equals `DAT_0263e0f4` at the moment it runs — a runtime fact (§14.5 item 3, unchanged).
4. The morph-slider value's home and range (§14.5 item 2) is unaffected by this section.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked 6 stale table rows/items resolved in place (§3.4 `+0x08`/`+0x14`, §3.5 `+0`/`+8`/`+10`, §7 vertex-index line → §13.2/§13.3/§15); added a stride pointer to the §13.3 pseudocode (§14.4); added an update note to §15.6 (rig §11.4 retracted, §11.7 item 3 resolved); clarified §14.7 "§5" as `HANDOFF.md` §5 and annotated its textual-scan bullet with the §14.5 item 3(a) refutation; reworded decompiler-shaped text (a decompiled signature, pseudocode calls/conditions/returns, `LAB_` goto, ~25 `param_N` uses) into plain English.
