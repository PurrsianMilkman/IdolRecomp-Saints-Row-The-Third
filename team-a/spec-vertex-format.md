# Saints Row: The Third — Vertex Format Specification (the "Mesh" sub-block channel array)

*Created 2026-09-11. Resolves `spec-geometry-format.md` §6 open item 8's channel-array half, and the standing "we have topology but no vertices" gap.*

---

## 1. Headline

**The per-vertex attribute data is fully located and fully decoded.** Every shipped mesh in the game stores its vertices as one or more **channels**, each described by a 24-byte record in the shared "Mesh" sub-block, and each stored as a separate contiguous, 16-byte-aligned array inside the paired `g`-file.

The three things that were missing are now all answered:

| Question | Answer | Confidence |
|---|---|---|
| Where are the vertices? | In the `g`-file, immediately after the index buffer, one contiguous array per channel, each 16-byte aligned | **CONFIRMED — disassembly + exact-size replay** |
| How big is a vertex? | `base(layout_code) + 4 × texcoord_count`, where the two size bytes in the record already state it directly | **CONFIRMED — disassembly + 26,601/26,601 channels** |
| What is *in* a vertex? | `float3` position; `UBYTE4N` normal; `UBYTE4N` tangent + handedness sign; `UBYTE4N` blend weights and `UBYTE4` blend indices; then N texture-coordinate sets of two signed 16-bit fixed-point values at `1024 = 1.0` *(generic layout codes; tree codes 11/12/13 carry FLOAT16×3 position and zone code 24 carries no texcoord — see §12.10.5)* | **CONFIRMED — empirical, with controls (§6)** |

The single most load-bearing correction in this document: **the `count × stride` buffer that `spec-geometry-format.md` §4.1.1 nominated as "the single strongest candidate for the actual interleaved vertex buffer" is the *index* buffer, not the vertex buffer.** Its element size is 2 bytes in 100% of shipped blocks. The vertex data is the per-channel arrays that follow it — a structure that section did describe, but as a secondary detail rather than as the answer.

---

## 2. Where this sits in the file

This document describes one structure: the **channel array** of the "Mesh" sub-block already documented in `spec-geometry-format.md` §4.1.1. That sub-block is shared engine machinery — it is embedded, unchanged, by at least six carrier formats (character meshes, static-prop meshes, level meshes, vehicles, trees, zones, foliage). Everything here therefore applies to all of them at once.

To reach it, follow `spec-geometry-format.md` §4.1.2 (material block → mandatory pad → `0x424BD00D` sub-header → `+0x88` → length-prefixed name blob → arrays 1–4 → align 8). The Mesh sub-block begins with a `u16` version field reading exactly `9`.

**The relevant Mesh sub-block header fields** (the header is a fixed `0x70` bytes):

| Offset | Size | Meaning | Confidence |
|---|---|---|---|
| `+0x00` | 1 | Flags. **Bit 0** set = this block's bulk data lives in the paired `g`-file; clear = inline in the `c`-file. **Bit 2** selects an alternative multi-stream representation. | **CONFIRMED — disassembly** |
| `+0x10` | 4 | **Channel count** | **CONFIRMED — disassembly** |
| `+0x18` | 4 | Pointer to the channel array, fixed up at load; the array physically follows the `0x70` header | **CONFIRMED — disassembly** |
| `+0x20` | 4 | **Index count** (see §8 — this is the field previously read as a vertex count) | **CONFIRMED — disassembly + empirical** |
| `+0x28` | 4 | Pointer to the index buffer, fixed up at load | **CONFIRMED — disassembly** |
| `+0x30` | 1 | **Index element size in bytes** — reads `2` in every shipped block observed | **CONFIRMED — disassembly + empirical** |

Immediately before the header, the block declares three values used below. **Their offsets are fixed**, measured from the block's `u16` version field:

| Offset from the version field | Size | Meaning |
|---|---|---|
| `+0x00` | 2 | version, reads `9` |
| `+0x04` | 4 | **check value** — equals the first `u32` of the paired `g`-file |
| `+0x08` | 4 | byte length this block occupies in the `c`-file |
| `+0x0C` | 4 | **byte length its segment occupies in the `g`-file** |
| `+0x10` | — | the `0x70`-byte header begins here, padded to an 8-byte boundary |

These offsets are **unconditional, not merely observed**: the loader rejects the block outright unless both the buffer base and the cursor are 4-byte aligned, which fixes every subsequent pad in the run-up to the header. **[CONFIRMED — disassembly for the alignment precondition; CONFIRMED — empirical, the clean team reports 400/400 using the `g`-file's own first word and file size as independent oracles, and this team reproduces it.]** *(An earlier version of this document said only "immediately before the header" without a relative anchor — correct but not directly implementable. Fixed.)*

The `g`-length field is what makes the replay in §4 an exact test rather than a plausibility argument.

---

## 3. The channel record — 24 bytes

Each channel is described by one 24-byte record. Every byte the loader reads is accounted for:

| Offset | Size | Meaning | Confidence |
|---|---|---|---|
| `+0x00` | 4 | **Element count** — the number of vertices in this channel | **CONFIRMED — disassembly** |
| `+0x04` | 1 | **Component-group A size**, in bytes per element | **CONFIRMED — disassembly** |
| `+0x05` | 1 | **Layout code** — selects a predefined vertex layout (§5). Rewritten in place at load time (§7). | **CONFIRMED — disassembly** |
| `+0x06` | 1 | **Texture-coordinate set count** — also the input to the load-time rewrite (§7) | **CONFIRMED — disassembly + empirical** |
| `+0x07` | 1 | **Component-group B size**, in bytes per element | **CONFIRMED — disassembly** |
| `+0x08` | 8 | Not read by this parser. Zero in every sample inspected. **[Resolved: zero on disk in 26,601/26,601 records, CLOSED as a strong negative, §12.1; at runtime the GPU-upload routine stores the created vertex-buffer handle here, `spec-vehicle-geometry.md` §11.9.2.]** | **OPEN / UNKNOWN** *(superseded, see §12.1)* |
| `+0x10` | 4 | Data pointer, written by the loader (not meaningful on disk) | **CONFIRMED — disassembly** |
| `+0x14` | 4 | Zeroed by the loader — the high half of a 64-bit pointer slot | **CONFIRMED — disassembly** |

**The channel's data block is `element_count × (sizeA + sizeB)` bytes.**

> **Correction to `spec-geometry-format.md` §4.1.1.** That section recorded this size formula as `record_count × (byte_at_record+1 + byte_at_record+7)`. **The first size byte is at `+0x04`, not `+0x01`.** The error came from reading a decompiler expression that indexes an `int`-typed pointer by 1 — which advances **four** bytes, not one — as though it were a byte offset. The `+0x07` half was right. This matters directly: at `+0x01` the loader would be reading the layout code's neighbour, and no size arithmetic would replay.
>
> This is worth recording as a methodology note, not just a typo: **when a decompiler expression mixes pointer types, the printed index is in units of the pointed-to type.** The same trap appears anywhere the decompiler casts an `int`-typed pointer down to a `byte` pointer before indexing it -- the printed offset then reads as a byte count even though the underlying arithmetic actually advanced by the original, wider element size.

**Both size bytes are summed.** In every channel observed in the paired-mesh population, group B reads `0` and group A carries the whole per-vertex stride, i.e. the engine supports a second interleaved component group per channel that no shipped asset uses. **[CONFIRMED — empirical, group B = 0 in 2,029/2,029 channels of `.ccmesh_pc`/`.csmesh_pc`; HYPOTHESIS — that group B is a second interleaved group rather than something else, since no example exercises it.]**

---

## 4. The `g`-file segment layout — confirmed by exact-size replay

The loader walks the `g`-file segment with a cursor that starts at the segment base. Two separate functions implement this walk — one for the inline case and one for the `g`-file case — and **they perform the identical arithmetic against different base buffers**. **[CONFIRMED — disassembly, both implementations read side by side.]**

```
segment start (16-byte aligned)
  u32            check value               <- must equal the block's own check value
  align to 16
  index buffer   index_count × index_size  (§8)
  for each channel, in record order:
      align to 16
      channel data   element_count × (sizeA + sizeB)
  align to 4
  u32            check value               <- bookend, same value again
```

The total must equal the `g`-length the block itself declares.

### 4.1 Replay results

Reproducing that walk from the `c`-file's declared fields alone and comparing against the real `g`-file:

| Test | Result |
|---|---|
| Replay total == the block's own declared `g`-length | **1,937 / 1,937** |
| Replay total == actual `g`-file size | **1,937 / 1,937** |
| First 4 bytes of the `g`-file == check value | **1,937 / 1,937** |
| `u32` at (replay total − 4) == check value | **1,937 / 1,937** |

*(All `.ccmesh_pc`/`.gcmesh_pc` and `.csmesh_pc`/`.gsmesh_pc` pairs found across 18 archives. Not a sample — every such pair in those archives. The wider sweep in §4.2 covers 20 archives and so reaches a slightly larger pair count for the same extensions; the two numbers differ only by archive coverage, not by any file failing.)*

**Why this is a strong test rather than a fitted one:** the replay consumes the index buffer and every channel using counts and sizes read from a *different file*, and must land on a byte total that the block declared independently — and then find a specific 32-bit value sitting exactly there. A single wrong stride, a single missed alignment, or a single misread count breaks it. **[CONFIRMED — empirical, exact, whole population.]**

### 4.2 The same walk validates blocks in carriers whose containers are only partly understood

Because the segment is self-checking (check value + declared length + bookend), a block can be validated **without understanding the format that contains it** — roughly a 2⁻⁶⁴ false-positive rate. Scanning carriers for Mesh blocks and validating each purely on the `g`-side:

| Carrier | Pairs | Validated Mesh blocks |
|---|---|---|
| `.ccmesh_pc` / `.gcmesh_pc` (character meshes) | 1,963 | 1,963 |
| `.csmesh_pc` / `.gsmesh_pc` (static props) | 97 | 97 |
| `.ccar_pc` / `.gcar_pc` (vehicles) | 388 | 388 |
| `.csrt_pc` / `.gsrt_pc` (trees) | 11 | 15 |
| `.czn_pc` / `.gzn_pc` (zones) | 1,083 | 2,843 |
| `.clmesh_pc` / `.glmesh_pc` (level meshes) | 11,371 | **12,628** |
| **Total** | | **17,934** |

Across those 17,934 blocks, **26,601 channels** were read, and the stride law of §5 reproduces every one of them: **26,601 / 26,601 exact, zero misses, zero unrecognised layout codes.**

> ⚠ **The zone and level-mesh rows above are confirmed UNDER-counts, found 2026-09-13 while investigating a Team B reader bug** (`spec-zone-data-format.md` §10.4–§10.7). `mesh_scan.py`'s shared g-side locator required its search hit to land on a 16-byte boundary; the real grid is 4 bytes, because `.gzn_pc` is a contiguous chain of segments where only the *first* is 16-aligned. Measured, controlled correction factors: **zones 2,843 → 38,152 (13.4×)**, **level meshes (`.clmesh_pc`) 12,628 → 29,908 (1.83×)**, **trees 15 → 25 (1.67×)**. The character/static-prop/vehicle rows are confirmed **unaffected (1.00×)** — their own carriers don't have this multi-segment chain shape. **The corrected zone and level-mesh totals, and the new grand total, are not re-derived here** — the 13.4× figure is validated on `.czn_pc` specifically; re-running the same corrected search across `.clmesh_pc`/`.csrt_pc` and re-measuring the channel-count total is flagged as the concrete next step, not done this pass. **Do not cite "17,934" as the current total.**

> **Update to `spec-geometry-format.md` §4.1.2 / §6 item 6, stated carefully so as not to overturn something that is already right.**
>
> That document establishes two things about `.clmesh_pc`/`.glmesh_pc`: it is registered with a **different constructor pair**, and its **outer container is genuinely a different format** — its own magic (`0x4fe66afa`), its own required version (`20`), a `0x140`-byte header, and no six-array walk. **Both of those findings stand. This pass does not contradict either.**
>
> What this pass adds is one level down: **the shared "Mesh" sub-block is nonetheless embedded inside `.clmesh_pc`, and its vertex data is confirmed — 12,628 blocks validated by exact `g`-side replay including the bookend check.** So the correct statement is that `.clmesh_pc` reaches the *same* shared vertex machinery by a *different* route through a *different* outer container. The open item those sections left — "`.clmesh_pc`'s own vertex format" as a separate unknown — is **closed, and the answer is that it does not have one of its own.** **[CONFIRMED — empirical.]**
>
> The earlier hedge that the shared mechanism was "only a plausible-but-unconfirmed guess" for this format was therefore right to be cautious about the *six-array structure* (which really is absent) and wrong only in extending that caution to the vertex data itself.

---

## 5. The layout enumeration and the stride law

The layout code at record `+0x05` selects one of a fixed set of predefined vertex layouts. **Across every code and every carrier, the per-vertex stride obeys one law:**

> **`stride = base(layout_code) + 4 × texcoord_count`**

Each texture-coordinate set costs exactly 4 bytes. Observed codes and their measured bases:

| Code | Base | Layout (see §6) | Where it ships |
|---|---|---|---|
| `0` | 16 | position + normal | zones, level meshes, static props |
| `1` | 24 | position + normal + skinning | characters (rare) |
| `2` | 20 | position + normal + tangent | level meshes, zones, static props |
| `3` | 28 | position + normal + tangent + skinning | **characters (dominant)** |
| `4` | 12 | position only | zones, level meshes |
| `11` | 32 | *(not probed — 13 channels)* | trees |
| `13` | 32 | *(not probed — 2 channels)* | trees |
| `24` | 20 | *(not probed — 448 channels)* | zones |
| `100` | 20 | position + normal + rigid part index | **vehicles** |
| `101` | 24 | position + normal + tangent + rigid part index | **vehicles** |

**Codes 0–3 form a regular family** in which the low two bits are independent feature flags: bit 0 adds skinning (+8 bytes), bit 1 adds a tangent (+4 bytes), over a 16-byte position+normal base. Predicting stride from that bitmask alone — with no per-code table at all — is exact in **20,773 / 20,773** channels of those four codes. **[CONFIRMED — empirical.]** Code 4 breaks the bitmask reading (it is position-only, not a bit combination), so the codes are best understood as **an enumeration of predefined layouts that happens to be ordered regularly at the start**, not as a bitfield. **[HIGH CONFIDENCE — inferred.]**

**On what is and is not predictive here, stated plainly:** the base is *measured* per code, so quoting a hit rate for the base alone would be circular. The **slope is genuinely predictive** — it is a single constant that must hold across every texture-coordinate count a code is observed at, and it does, for all 7 codes seen at two or more distinct counts (codes 0, 1, 2, 3, 4, 100, 101), spanning counts 0 through 4. Codes 11, 13 and 24 are each observed at one count only and so contribute no test of the slope. **[CONFIRMED — empirical, for the slope; measured, not predicted, for the bases.]**

**One clean out-of-sample confirmation.** The base table was fixed from an earlier sweep in which code 3 appeared only at texture-coordinate counts 1, 2 and 3. The final whole-population sweep turned up **4 channels of code 3 at count 4**, a combination absent when the table was written. The law predicts a 44-byte stride for them, and all 4 read exactly 44. A small number, but it is a genuine prediction rather than a refit. **[CONFIRMED — empirical, out of sample.]**

---

## 6. What is in a vertex element

Fields are laid out in a fixed order, all sizes in bytes:

```
+0   float3   position          (12)
+12  UBYTE4N  normal             (4)
     UBYTE4N  tangent            (4)   only when the layout has one
     UBYTE4N  blend weights      (4)   \ skinned layouts
     UBYTE4   blend indices      (4)   /
     ...      texcoord set 0     (4)   repeated texcoord_count times
```

### 6.1 Position — `float3` at offset 0

Decoded as three little-endian floats at element offset 0, positions are finite in **every element of every channel tested, in every layout class** (110,362 / 110,362 in the largest single class alone). **[CONFIRMED — empirical.]**

**The confirming evidence is semantic, not just statistical.** A full character body mesh decodes to a bounding box of **1.376 × 1.855 × 0.525** units with the vertical minimum at approximately zero — a correctly proportioned human figure, roughly 1.86 m tall, standing on the origin plane. No fitting, searching, or scaling was involved: the numbers fall out of reading offset 0 as three floats. **[CONFIRMED — empirical.]**

**Control:** decoding a `float3` at offsets +4, +8 or +12 instead does not merely give a worse bounding box — it does not produce finite floating-point values at all. The position field's location is therefore not a choice among several plausible readings. **[CONFIRMED — empirical, the control is decisive.]**

### 6.2 Normal at +12, tangent at +16

Both decode as **UBYTE4N** — four unsigned bytes, each mapped to [−1, 1] as `(b / 255) × 2 − 1` — with the first three components forming a unit vector.

Measuring "is the low-3-byte vector unit length to within 6%" across the population:

| Layout code | +12 | +16 |
|---|---|---|
| 0 (pos+normal) | **1.000** | — |
| 1 (skinned) | **1.000** | *(weights)* |
| 2 (tangent) | **1.000** | **1.000** |
| 3 (tangent+skinned) | **1.000** | **1.000** |
| 100 (vehicle) | **1.000** | *(part index)* |
| 101 (vehicle+tangent) | **1.000** | **1.000** |

A rate of exactly 1.000 for a 6%-tolerance geometric test is not something an arbitrary 4-byte field passes.

> **Independently confirmed on the vehicle layouts at full population (clean team, 2026-09-11).** This document's own figures for codes 100/101 came from a 60-block-per-class sample. Team B decoded **every** vehicle channel — **3,872 channels, 5,629,811 vertices across all 372 vehicles, zero decode failures, and normals unit-length within 6% on all 5,629,811** — with a free side-check that rigid part indices stay inside the real part-array bounds. That exercises the vehicle code path end to end rather than inferring it from the fact that the Mesh block parses. Same strength of evidence as the original character-normal measurement, now on both carriers the format covers. Layouts whose code says they have no tangent correspondingly fail the test at +16, and layouts whose code says they do, pass — the two agree in every case. **[CONFIRMED — empirical, with the code-to-presence agreement acting as its own control.]**

**Which of the two is the normal and which the tangent — settled by their fourth bytes.** Unit length alone cannot distinguish them, and an earlier version of this section left the assignment as inference from ordering. The fourth bytes decide it:

| Field | Fourth byte |
|---|---|
| the vector at **+12** | **113 distinct values**, spread across the range; at the extremes (≤1 or ≥254) only **0.0002** of the time |
| the vector at **+16** | **exactly 2 distinct values** — `0` (80.4%) and `255` (19.6%); at the extremes **1.0000** |

A field taking only the two extreme values is a **sign**, and a packed unit vector carrying a sign in its fourth component is the standard way to store a **tangent** plus the handedness needed to reconstruct the bitangent. So **+16 is the tangent and +12 is the normal** — now established by the data rather than by convention, and consistent with the ordering argument and with the fact that layouts exist having the first vector and not the second but never the reverse. **[CONFIRMED — empirical, 49,598 vertices.]** *(Carrier qualifier added by §12.5: the two-value handedness byte holds on characters, static props, level meshes and zones; it does NOT hold on vehicles (`.ccar_pc`), where the same byte takes 256 distinct values — see §12.5.)*

**The normal's own fourth byte is therefore *not* a sign, and remains unidentified** — 113 distinct values with a dominant mode (`112`, 23%) is real per-vertex scalar content, not padding. **[OPEN / UNKNOWN — but now known *not* to be handedness.]** §12.4 narrows this considerably (per-region tag, finer than material, most likely smoothing-group-style); §12.13 item 1 runs the direct material-id correlation §12.4 itself flagged as the next step, and REFUTES literal material-id identity while reinforcing the smoothing-group reading — see there for the full test.

### 6.3 Skinning — weights then indices (codes 1 and 3)

**Weights** are four bytes forming a partition of unity in 8-bit fixed point. Their sum lies in [250, 260] in **37,885 / 37,885** vertices, and takes only three distinct values across the whole population: 254, 255 and 256 — rounding residue around a normalised 255. **[CONFIRMED — empirical.]**

The number of non-zero weights per vertex distributes as 1 influence 47.6%, 2 influences 22.1%, 3 influences 18.3%, 4 influences 12.0% — the expected profile for four-bone skinning. **[CONFIRMED — empirical.]**

**Indices** are four bytes, with **255 used as an "unused influence" sentinel**. The decisive evidence is a per-vertex cross-check between the two fields, which are separately encoded and four bytes apart:

> **A weight lane is zero exactly when the corresponding index lane is 255: 70,054 / 70,054 vertices (1.000000).**
> **Control** — the same test against the normal field instead of the index field: **0.1498**.

Two independently-decoded fields agreeing per-lane, per-vertex, without exception, cannot be produced by a misaligned offset. **[CONFIRMED — empirical, controlled.]**

The largest non-sentinel index observed is **63**, consistent with a 64-entry bone palette. **[CONFIRMED — empirical for the value; HIGH CONFIDENCE — inferred for "palette size is 64".]**

### 6.4 Vehicles — a single rigid part index (codes 100 and 101)

Vehicles carry **4 bytes where characters carry 8**, and no partition-of-unity field anywhere in the element. That field's bytes are **always below 80**, which alone rules out vertex colour (an alpha byte would pin at 255).

> **All four bytes hold the same value: 77,816 / 77,816 and 62,505 / 62,505 vertices (1.000000).**
> **Control** — the same test against the normal field: **0.000000**.

The bytes are either all zero or all non-zero, never mixed. This is a **single part index broadcast across four lanes** — a rigid bind, where every vertex belongs to exactly one movable part and the weight is implicitly 1.0, stored in the same 4-byte slot shape the skinned path uses so one shader path serves both. Maximum values observed are 28 and 21, the right order for a car's separable parts. **[CONFIRMED — empirical for the replication and the value range; HIGH CONFIDENCE — inferred for the "rigid part bind" interpretation.]**

### 6.5 Texture coordinates — 4 bytes each, at the end

Every remaining 4-byte slot is a texture-coordinate set, and the count is stated directly by record `+0x06`. Each is **two signed 16-bit fixed-point values with 1024 representing 1.0** — decode as `int16 / 1024.0`.

> **Correction, same day, to the first version of this document.** That version said "a half-float pair is the reading consistent with the observed ranges." **Half-float is refuted, decisively and easily:** across 2,222,554 shipped coordinate values, **94.6% have a half-float exponent field of zero** — i.e. under that reading essentially the entire population would be denormal, decoding to ~0.0001 and collapsing whole meshes to a single point. A genuine half-float population sits near 0% there, not 95%. The values are small integers, and reading them as floats produces nothing.

Three independent measurements fix the encoding:

| Measurement | Result |
|---|---|
| Per-channel maximum raw value | median **1021**; 58.8% of channels peak in [1000, 1100] |
| Implied coordinate extent at scale 1024 | median **0.9971** |
| Decoded range, signed, scale 1024 | median **[−0.0098, 1.0059]** |

A median maximum of 1021 against a scale of 1024, and a decoded interval of [−0.01, 1.006], is the canonical texture-coordinate range landing on the nose. **[CONFIRMED — empirical.]**

**Signed, not unsigned.** 2.79% of values exceed 32767. Read unsigned those become coordinates up to 63.99 (median channel range `[0.002, 63.990]`); read signed they are small negatives, which is ordinary for tiled or mirrored mapping. Because the two readings differ by a **wrap** rather than a rescale, surface continuity separates them: over the 150 channels that actually contain such values, mean absolute texture-coordinate change along a triangle edge is **0.019 signed versus 0.035 unsigned**. **[CONFIRMED — empirical, controlled.]**

**The signedness is demonstrated in each lane independently, not just in aggregate** — worth stating explicitly, because an aggregate result here would only have proved that *something* in the field is signed:

| | negative values | share |
|---|---|---|
| **U lane** | 31,513 / 847,967 | 3.72% |
| **V lane** | 22,186 / 847,967 | 2.62% |

Of 400 channels examined, 48 carry negatives in **both** lanes, 51 in U only, 40 in V only. Every texture-coordinate **set** is exercised too (set 0: 3.2%, set 1: 5.0%, set 2: 2.7%), so the reading is not established on the first set and assumed for the rest. **[CONFIRMED — empirical, per lane and per set.]**

*(Honest note: the original test qualified a channel if **either** lane held a value above 32767, so it would have passed even had only one lane ever carried negatives — proving signedness "somewhere in the field" rather than in each lane that uses it. The finer measurement above was run afterwards, prompted by the clean team hitting exactly that gap in their own test fixtures, and the conclusion survives it. It was right by luck of the data, not by design of the test. See `HANDOFF.md` §5.)*

> **A methodology note, because this nearly went in wrong.** The first continuity test normalised each edge delta by that reading's own coordinate range. The unsigned reading inflates its own range roughly 64-fold, so the normalisation flattered it and the metric "preferred" the wrong answer by 2.7×. Switching to an absolute delta reversed the verdict. **A ratio whose denominator is itself affected by the hypothesis under test is not a measurement.** Same failure family as the control-rate lessons in `HANDOFF.md` §5.

That `+0x06` is a texture-coordinate **count** rather than an arbitrary variant id is confirmed by the stride law (§5): each increment adds exactly 4 bytes, in every layout code, across counts 0 to 4.

### 6.6 Does the 1024 scale hold for vehicles (codes 100/101) specifically? — CLOSED

**Raised 2026-09-29 by a peer team's real shader-driven render**, whose translated vehicle-paint vertex shader (`ir_sr3carpaint_gr_v.fxo_pc`) carries its own compiled UV-descale constant of **1/256**, not this section's **1/1024** — a real, 4× discrepancy worth checking directly rather than assuming §6.5's population (predominantly characters/static props, per its own 400-channel sample) already covers vehicles.

**First, a direct measurement restricted to vehicle channels only** (sem 100/101, 400 channels sampled from `vehicles.vpp_pc`, same per-channel-max-raw-value statistic §6.5 uses): median **1869**, p25 landing at exactly **1021** (matching the character/static-prop control's own near-uniform 1021–1034 cluster), but a much longer right tail (p75 **2875**, max **30125**). A quarter of vehicle channels sit exactly where the character population sits; the rest skew well above it — the expected shape of **legitimate UV tiling** on a large flat body panel repeating a detail/damage texture, not a uniformly shifted base scale (which would shift the *whole* distribution, not just its upper half). **[CONFIRMED — empirical, real data, 2026-09-29.]**

**Second, and decisive: real runtime evidence (dirty-side, out of this document's normal disassembly scope — an external runtime capture; findings summarized here, the source is not for clean-side consultation) directly answers §12.9.4's open `D3DDECLTYPE` question and explains the exact 4×.** Real captured D3D9 vertex declarations show texture coordinates registered as raw **`D3DDECLTYPE_SHORT2`** (confirmed twice independently: a Vulkan-side format-rejection log reading `VkFormat 80 = R16G16_SSCALED = D3DDECLTYPE_SHORT2`, and a directly-read declaration string `short2 TEXCOORD @28`) — **not** the hardware-normalizing `SHORT2N`, so the GPU passes the stored int16 through completely unchanged and the entire descale is software, done inside the vertex shader. Real disassembled shader bytecode (two independent shaders, `ir_bbsimple2_decal_s` and `ir_bbsimple_1uv_decal_s`) shows the exact, consistent pattern:

```
mul r0.x, c1.x, v1.x        ; per-material TilingU * raw_u
mul r0.y, c2.x, v1.y        ; per-material TilingV * raw_v
mul o1.xy, r0, c3.x         ; c3.x = 0.0009765625 = 1/1024 exactly
```

**i.e. `uv = raw × tiling × (1/1024)`**, with the tiling pair being a genuine, real, per-shader/per-material constant (its register location moves between shaders — `c1`/`c2` in one, `c2`/`c3` in another — and some shaders carry no tiling pair at all, defaulting to 1). **The base decode mechanism is settled: raw `SHORT2`, universal `1/1024`, with a separate per-shader tiling factor applied before it.** **[CONFIRMED — real captured runtime declarations + real disassembled shader bytecode from two independent real shaders.]**

**The vehicle-paint shader's own `1/256` is specifically this pattern.** Peer-team verification (2026-09-29), checked against their translator's own disassembly-to-bytecode mapping (not just the rendered constant name): the constant is sourced from a genuine `def` bytecode instruction — a compile-time literal baked into the shader by the compiler, structurally distinct from, and never confusable with, an ordinary CTAB-reflected app-settable c-register (the translator only ever labels a register this way when it walked a real `DEF` opcode for that index; there is no path for a runtime-settable register to be mislabeled this way). A `def`-sourced constant is exactly what a real SM2/3 compiler emits when it folds a compile-time-literal tiling factor together with the always-present `1/1024` into one immediate — this shader's own tiling factor is simply fixed in source rather than exposed as a material parameter, unlike `ir_bbsimple2_decal_s`/`ir_bbsimple_1uv_decal_s` above. **[CONFIRMED — real disassembled bytecode, `def`-vs-CTAB distinction checked directly against the peer team's own translator source, not inferred.]** Vehicles use the same universal `1/1024` texcoord scale as every other carrier; no exception exists for this layout family.

**Actionable note for whoever wires this at draw time:** the general, robust mechanism — demonstrated necessary across multiple real shaders — is two separate constants (a per-shader-located tiling pair, defaulting to 1 when absent, read from that shader's own CTAB by name, typically `Normal_Map_TilingU`/`Normal_Map_TilingV`) always followed by the fixed universal `1/1024`, not a single hardcoded divisor per shader — that part is safe to build against now. §12.9.4's `D3DDECLTYPE` gap is resolved for texcoords specifically (raw `SHORT2`).

## 7. The load-time rewrite of the layout code

The loader passes each channel record through a small routine that **rewrites the layout-code byte in place**, combining it with the texture-coordinate count. Codes are accepted from a fixed set — 0 through 4, 14 through 26, and 100 and 101 — and each accepted code owns a **4-wide** range in the rewritten numbering, into which the texture-coordinate count is added. When the count is zero the routine returns without changing anything, so a layout with no texture coordinates keeps its original code. **[CONFIRMED — disassembly, the exact accepted set and the exact arithmetic.]**

The effect is to fold *(layout, texcoord count)* into a single flat identifier, which is the natural shape of an index into a table of prebuilt vertex declarations. **[HIGH CONFIDENCE — inferred.]**

**Two consequences worth stating.** First, **the on-disk byte is the pre-rewrite value** — anything reading these files sees the raw layout code, and should not expect the flattened numbering. Second, the accepted set is **wider than what ships**: codes ~~5–10, 12, and~~ 14–23, 25, 26 are accepted by the loader but were not observed in any shipped asset examined here. *(Corrected: codes 5–10 are not in the accepted set stated above — the case list is `0x00`–`0x04`, `0x0E`–`0x1A`, `0x64`–`0x65`, §12.9.3/§12.10.3 — and code 12 does ship (trees, §10 item 2, §12.10.1) but is likewise not accepted (§12.10.3).)* Their layouts are consequently unknown. **[CONFIRMED — disassembly for the accepted set; OPEN / UNKNOWN for the unobserved codes' layouts.]**

---

## 8. The index buffer

The buffer that `spec-geometry-format.md` §4.1.1 identified as the leading vertex-buffer candidate is the **index buffer**:

- Its element size field reads **2** in **every** shipped block observed, across all carriers. A vertex buffer with a 2-byte stride is not possible; a 16-bit index buffer is exactly that.
- **Every index is less than the vertex count of the channels that follow it: 428 / 428 blocks (1.000000).** Control — the same range test applied to the vertex data region instead: **0 / 428 (0.000000)**.
- **The largest index equals exactly the last vertex** (`max index / (vertex count − 1)` = 1.0000 as both median and mean, in 100% of blocks) — the signature of a complete, fully-referenced index buffer over exactly that vertex array.

**[CONFIRMED — empirical, controlled.]** This also retroactively explains the very first black-box observation recorded for this format — a long run of smoothly ascending 16-bit values at `g`-file offset `0x10` — which was correct all along.

### 8.1 Primitive topology — triangle **strips**, stitched with degenerate triangles

**These are triangle strips, not triangle lists.** The buffer is consumed as a sliding window — triangle *k* is indices *k*, *k+1*, *k+2* — and separate strips are joined by repeating an index, producing zero-area triangles the rasteriser discards. **[CONFIRMED — empirical, controlled.]**

The decisive measurement is a counting argument that needs no rendering. A surface has roughly two triangles per vertex (Euler; somewhat fewer in practice, since UV seams and open boundaries duplicate vertices). Counting **non-degenerate** triangles under each reading:

| Reading | Non-degenerate triangles per vertex |
|---|---|
| triangle list | **0.532** |
| triangle strip | **1.594** |

**The list reading is not merely worse, it is impossible.** Half a triangle per vertex cannot describe a surface at all — there are not enough triangles to attach the vertices to. The strip reading lands exactly where a real mesh with seams should.

Four further observations all agree:

- **48 of 120 blocks have an index count not divisible by three.** A triangle list cannot have that; a strip has no such constraint.
- **14% of adjacent index pairs are equal.** Under the strip reading those are the stitches, and they are *meaningful*; under the list reading they are 39% of the mesh's triangles being degenerate, which no shipped asset would contain.
- **Mean longest triangle edge**, as a fraction of the mesh bounding-box diagonal: **0.0476 strip** vs **0.0507 list** vs **0.4198** for a random-triple control. Both readings are real topology — consecutive indices are spatially local either way — but the strip reading is tighter, and far more so at the median (0.0271 vs 0.0358).
- **Average vertex reuse is 2.46×.** A triangle list over a closed mesh reuses each vertex about six times; a strip reuses each one once or twice.

*(The normaliser above is the position bounding box, which is identical under both hypotheses — chosen deliberately so that neither reading can inflate its own denominator. See §6.5's methodology note for what happens when that care is not taken.)*

**What is still not pinned:** whether the buffer is additionally subdivided per render batch by descriptors in the outer six-array structure — arrays 1–3, since resolved as attachment-socket/collision data unrelated to render batching (`spec-geometry-format.md` §4.1.5), not per-batch descriptors. *(Resolved: the per-range subdivision lives in the Mesh sub-block's group/draw-range records, §8.2; §10 item 7.)* A renderer can draw correct geometry: consume as a strip and drop zero-area triangles. **[CONFIRMED — strip over list.]**

**The exact stitching/winding convention within a range — CLOSED, 2026-09-29 (§12.13 item 2).** Triangle *k*, 0-indexed from each draw range's own start (degenerate slots counted), is `(idx[k], idx[k+1], idx[k+2])` for even *k* and `(idx[k+1], idx[k], idx[k+2])` for odd *k* — the standard GPU triangle-strip parity rule, with parity measured **locally from each range's own start**, never carried across a range boundary from the buffer's global position. Confirmed against real decoded vertex normals as an independent ground truth (not assumed): 5,043,382 / 5,077,382 (99.33%) of 5,077,382 real triangles across 1,756 real character/static-prop meshes agree, including 97.9–99.6% agreement on the very first real triangle immediately following a restart, at every observed restart run length (1 through 5). A naive fixed-winding reading scores only 51.58% (chance level); measuring parity from the buffer's global position instead of each range's own start scores only 75.33% — confirming parity resets per range. **No separate, explicitly-encoded winding correction exists at restarts of any length; this fixed rule is sufficient on its own.** **[CONFIRMED — empirical, controlled, real ground truth, 2026-09-29.]** Not yet extended to vehicles (`.ccar_pc`, different outer-header shape and multiple channels per mesh — untangling which channel each LOD group's ranges index was out of scope) or to `.clmesh_pc`/`.czn_pc` (different outer-header shape entirely) — do not assume cross-carrier confirmation beyond characters and static props.

### 8.2 Draw ranges — the index buffer is partitioned, and drawing it as one strip is wrong

> **Read these two sentences before the rest of the section.**
> **(a) The groups are LEVEL-OF-DETAIL steps. Draw ONE group — group 0 for full detail — not all of them.** Drawing every group renders the same character several times over, at descending detail, stacked on itself.
> **(b) Within that group, draw each range separately.** Concatenating ranges fabricates triangles bridging unrelated parts.
>
> *Both of these have been got wrong independently by two different readers of an earlier draft, which is this document's fault rather than theirs: the partition-into-ranges fix was stated as the headline and the LOD interpretation was left to a paragraph further down, so a reader skimming for the defect found (b) and missed (a). Hence the hoist.*

**The index buffer must not be drawn as a single continuous strip.** It is partitioned into **draw ranges**, and consecutive ranges are unrelated pieces of the model. Reading across a boundary fabricates triangles that bridge them — on a character, visible spikes joining hands to feet. The clean team measured this artifact at 55 / 7,527 triangles on one body mesh and 32 / 8,318 on another, scaling with material complexity. **Every one of those is a boundary crossing, and every one disappears once the ranges are respected.**

**Where the ranges live — not where this document previously guessed.** §8.1 suggested the outer six-array structure (arrays 1–3) as the likely home of per-batch descriptors. **That was wrong.** Array 1 is *absent* on the very meshes exhibiting the artifact, and arrays 2–3 hold per-material float data. The ranges are inside the **"Mesh" sub-block itself**, in a record array the earlier passes walked past without decoding:

- **Mesh header `+0x04`** holds a **group count**. The groups are `0x30`-byte records, laid down after the channel array (and after the flag-bit-1 arrays when present), 16-byte aligned.
- Each group's first `u32` is **how many draw ranges it owns**.
- After all the group records come the ranges themselves, **20 bytes each**, grouped in record order.

**The 20-byte draw range:**

| Offset | Size | Meaning |
|---|---|---|
| `+0x00` | 4 | **material id — and on `.ccar_pc` this word is PACKED 16:16.** `id = field & 0xFFFF` (dense from 0 within a mesh, 86/86 vehicles); `field >> 16` is a ~~**sub-mesh / part index**~~ **vertex-channel index** *(corrected 2026-09-28: it selects which channel of the Mesh sub-block's channel array (header `+0x18`) the range's indices address — `spec-vehicle-geometry.md` §11.1/§11.5, `spec-render-pipeline.md` §18.6; the part-index reading is refuted there)*, running 0,1,2,3… in order. Read whole on a vehicle it yields values like `65542`, `131072`, `196609` — the signature of a 16:16 pack read as one word. **Now measured on both populations (clean team), replacing this document's earlier safety argument:** on **characters**, zero of 6,402 ranges carry a non-zero high half — so masking is a provable no-op there and the plain-`u32` reading was never wrong. On **vehicles**, 77,886 of 87,012 ranges carry one, values running 0–14 in a smooth descending distribution — the shape of a real per-asset index, not noise. Masked, the maximum material id across all 372 vehicles falls from **917,541 to 50**. **Mask unconditionally. [CONFIRMED — empirical, both carriers.]** Keep the high half rather than discarding it: it is the leading untried handle for the material-association question (`spec-vehicle-geometry.md` §7.1). *(Superseded: the high half is the vertex-channel index above, not an association handle; association is positional, `spec-vehicle-geometry.md` §7.1/§11.2. The 86/86 denominator is extended to 393/393 in §8.4.1.)* |
| `+0x04` | 4 | **start index** into the index buffer |
| `+0x08` | 4 | **index count** |
| `+0x0C` | 4 | lowest vertex index this range references |
| `+0x10` | 4 | highest vertex index this range references |

That is the standard indexed-draw descriptor, including the minimum/maximum vertex bounds a D3D9-era `DrawIndexedPrimitive` call needs.

**Exact validation:**

| Test | Result |
|---|---|
| Ranges contiguous from 0 and together covering the whole index buffer | **1,831 / 1,831 meshes** |
| Every range: `minVertex ≤ maxVertex < vertex count` | **1,831 / 1,831 meshes** |
| **Indices actually inside each range lie within its declared `[min, max]`** | **8,899 / 8,899 ranges** |

The third test is the one that settles it. The declared vertex bounds are not used to locate anything — they are an independent *assertion about content*, and the real index data honours it in every range of every mesh. A misidentified field cannot do that. **[CONFIRMED — empirical, exact.]**

**The groups are level-of-detail steps — now confirmed by rendering, not inferred.** Of 1,831 meshes, 1,347 have a single group and 281 have four. A head LOD mesh has exactly one group holding exactly one range, `{slot 0, start 0, count 882, vertices 0–310}` — the entire buffer, the entire vertex array. A full body has four groups of four or five ranges each, contiguous, the later groups progressively shorter.

Shrinking index counts alone cannot separate "LOD levels" from "progressively smaller *parts* of one model", so the clean team tested the competing hypothesis directly by **rendering each group on its own**: every group draws the complete character — same pose, proportions and silhouette — with detail falling away as the counts drop (on one character the hair disappears entirely at group 3, matching its range count going 5/5/5/4). A part-wise split would have rendered a fragment; none does. **[CONFIRMED — empirical, refuted by images rather than argued away.]**

**One trap for an implementer.** On multi-channel meshes the channels have **different vertex counts**, and the draw ranges are bounded by the **largest** one, not by channel 0's. Checking ranges against channel 0 produces 22 spurious failures out of 1,831 — all of them multi-channel meshes where `maxVertex` is exactly the last channel's count minus one. Bound against the largest channel and it is 1,831 / 1,831. *(Recorded because it cost this pass a false anomaly: the number looked like a format irregularity and was a wrong denominator in the test.)*

**How to draw it:** pick one group, then issue one indexed draw per range in it, resetting the strip at every range boundary. Do not concatenate ranges. Within a range, consume as a strip and discard degenerate triangles (§8.1).

> **Two wording traps, both hit by an implementer reading this cold.**
>
> 1. **Contiguity runs across *all* ranges in group order, as one continuous partition of the whole buffer — it does not restart per group.** Range starts do not reset to 0 at each group boundary; group 1's first range begins exactly where group 0's last range ended.
> 2. **A group is a contiguous *slice* of the index buffer, not an independent full copy of the geometry.** Each group covers its own disjoint span, so the LOD levels are stored end to end in one buffer rather than as separate meshes.
>
> Both follow from the contiguity result above, but neither is obvious from the field names alone.

> **The `0x30`-byte group record is now fully decoded from the loader, and the `+0x0C`/`+0x10` reading is confirmed from the CONSUMER side** (2026-09-13, `spec-vehicle-geometry.md` §11.9). `FUN_00e71310` is the group/draw-range array parser: Mesh header `+0x04` = group count, `+0x08` = group array base (an 8-byte pointer slot, `+0x0C` zeroed). Group record: `+0x00` draw-range count, `+0x04` vec3 bbox min, `+0x10` vec3 bbox max, `+0x1C` unknown, `+0x20` pointer to this group's `0x14`-byte draw-range array, `+0x28` pointer to an array of `rangeCount` 8-byte slots each pre-resolved to `meshHeader[+0x50] + storedValue * 2`. All groups' range arrays are contiguous first, then all groups' 8-byte slot arrays — not interleaved per group. And `FUN_00e72d00` is an engine function that takes a packed 16:16 draw-range handle, walks exactly this path, and returns the range's `+0x0C` and `+0x10` as its two output parameters — i.e. the renderer itself treats them as the minimum and maximum vertex index, independently of this document's validation statistics.

**Locating the group array:** its offset is derived above, but it can also be *found* rather than computed — search for a position at which the invariants hold (ranges contiguous from 0, exact coverage of the index buffer, vertex bounds sane) and only one candidate survives. That is the same search-and-check technique that pinned the pre-header offsets in §2, and it is worth preferring to arithmetic wherever a structure exposes invariants this strong.

### 8.3 The material/slot id, and where per-material data lives

Each draw range's first field is a **material id**. Two facts about it are settled, and the thing an implementer most wants is not.

**Settled:**

- **The material count is a `u16` at offset `+0x0C` of the `0x424BD00D` sub-header.** Spelled out, because an earlier draft said only "outer sub-header" and an implementer reasonably read it against the *Mesh* sub-block's `0x70` header, where it reads zero on every file:
>
> ```
> material_block_end = 0x20 + u32(file + 0x04)
> subheader          = material_block_end + (16 - (material_block_end % 16))   // mandatory pad, always 1..16
> assert u32(subheader) == 0x424BD00D
> materialCount      = u16(subheader + 0x0C)
> ```
>
> This is the same sub-header whose `+0x08`/`+0x0A`/`+0x38`/`+0x3A` hold the six-array counts (§4.1). It sits **before** the Mesh sub-block by the length of the `0x88` header, the name blob and arrays 1–4, so searching a window around the Mesh sub-block will never reach it.
>
> It equals `1 + max(material id)`, ids dense from zero, in **241 / 241 multi-material meshes** — and the next-best `u16` offset anywhere in `−0x20 .. +0x90` scores **14.5%**. **[CONFIRMED — empirical.]**
>
> *(The first version of this line quoted 428/428 over all meshes. That population is ~47% **single**-material, where the expected value is 1 and any field that often reads 1 will match — so most of the denominator was carrying no information. The clean team caught it. Restricted to meshes where the test can actually fail, the result is unchanged, but the honest figure is the restricted one. Third denominator error of the session; see `HANDOFF.md` §5.)*
- **Material ids separate real surfaces.** Rendered as flat per-material colours they land on genuine garment boundaries — jacket, trousers, gloves, shoes, hair on one character, seven similarly clean groups on a more complex one. **[CONFIRMED — empirical, clean team.]**
- **Update, 2026-09-30 — zones (`.czn_pc`) carry this exact same draw-range material-id field, real and varying, despite having no `0x424BD00D` GeometryBlock sub-header at all (§12.13 item 4/§8.4 below; `spec-terrain-format.md` open item 3).** Directly decoded from 448 real zone Mesh sub-blocks' own group/range records (the generic `0x30`-byte group + `0x14`-byte range structure this section already describes, which lives inside the universal Mesh sub-block and does not depend on the GeometryBlock wrapper): real, non-trivial, 16:16-packed material ids are present, dense from 0, with observed masked values running at least 0-16 across the population — the identical packing convention already confirmed for vehicles above. **This settles that zones DO have a real per-range material-id concept** — the wall is specifically the *texture-binding* mechanism below (which needs the missing GeometryBlock and its own post-Mesh-sub-block per-material record region), not the existence of a material id in the first place. **Directly tested and REFUTED: material id count does not equal the sibling `.czh_pc` file's own shared-material-block reference count** (0/279 real zone tiles matching `max(material id)+1 == czh reference count` — e.g. one real tile's ranges top out at masked id 5 while its `.czh_pc` lists 42 texture references) — so zone material ids are not a direct index into that tile's own `.czh_pc` name list; whatever the real lookup is, it is not that simple relationship. **[CONFIRMED — empirical, real data, for the material-id field's existence and packing; REFUTED — empirical, for the `.czh_pc`-index hypothesis specifically; OPEN — the real lookup mechanism.]**

**Settled as of 2026-09-11: which textures a material id selects.** Every index-based hypothesis failed for one reason — **textures are not referenced by index at all.** Each per-material record carries a contiguous run of **12-byte binding entries**:

| Offset | Size | Meaning |
|---|---|---|
| `+0x00` | 4 | **Byte offset of a texture name within the mixed-case name blob** (the length-prefixed blob at sub-header `+0x88`) — it addresses a string *start* |
| `+0x04` | 4 | Sampler / parameter name hash |
| `+0x08` | 4 | **Slot index**, sequential from 0 |

Note *which* name list: the **mixed-case blob**, not the lowercase list in the material block. That is the whole reason every `textureNames[materialId]` variant failed.

> **⚠ Corrected 2026-09-11: the SLOT INDEX is not the semantic — the HASH is.** The table below was derived from character meshes alone and reads as though slot 0 *means* diffuse. It does not. The same sampler hash `0x2808EB90` occupies **slot 1 in `.ccmesh_pc` and slot 0 in `.ccar_pc`**, so the index is merely the entry's position within its material. **A reader must key on the parameter hash, not on the slot number.** The roles below are the character-mesh *convention*, reliable within that format and not a format rule. This was over-generalisation from a single carrier.

**Slot semantics in character meshes, and the evidence:**

| Slot | Meaning | Evidence |
|---|---|---|
| 0 | diffuse | name ends `_d` or `_dp` in **1,261 / 1,266** |
| 1 | normal map | name ends `_n` in **1,266 / 1,266**; parameter hash is the single constant `0x2808EB90` in **1,266 / 1,266** |
| 2 | surface type | parameter hash `0xDFE71DA8` in **1,251 / 1,264**; names are `Cloth_Matte`, `Skin_Ca`, `leather`, `Metal`, `Reflection`, `Gold` |

A run is present in **1,478 / 1,478** material records. **The per-slot hashes being a single constant across entirely unrelated meshes is what makes this structural rather than a pattern that happens to fit** — a coincidental run would not agree on its hash values across the population. **[CONFIRMED — empirical.]**

Worked example (4 materials), showing the shared-pair behaviour that defeated the slice hypotheses:

```
material 0: alfred_sm_upper_dp.tga, alfred_sm_upper_n.tga, Cloth_Matte_02_SB.tga
material 1: alfred_sm_upper_dp.tga, alfred_sm_upper_n.tga, Metal_01_SB.tga
material 2: alfred_sm_upper_dp.tga, alfred_sm_upper_n.tga, Skin_Ca_02_SB.tga
material 3: alfred_sm_lower_dp.tga, alfred_sm_lower_n.tga, Cloth_Matte_02_SB.tga
```

> **Implementation caveat, and it matters.** The run's **start offset within the record varies** between meshes (`+0x64`, `+0x04`, `+0x74`, `+0x54`, `+0x4C`, …), so the records are variable-layout and this document cannot give a fixed offset for the run. **Locate it by its own constraint** — the position whose slot indices read `0, 1, 2, …` consecutively *and* whose every offset lands on a string start in the blob. A fixed-offset reader will be wrong. **[OPEN — the rest of the record's layout, and therefore a direct offset to the run.]** *(Superseded: the record layout and a direct offset are now CONFIRMED — the run starts at the material record's `0x30`-byte header + `0x30`, §12.8; `spec-vehicle-geometry.md` §11.2. Association is positional.)*
>
> **Two conditions are REQUIRED, and without them the rule produces confident nonsense:**
>
> 1. **run length ≥ 2**, and
> 2. **a non-zero parameter hash**.
>
> **If neither holds, the material has no binding — report that, do not return what the search found.**
>
> Across ten archives the search finds a run in 3,954 / 3,954 material records, which looks like a clean result and is not. Stratified by run length it splits into two unrelated populations:
>
> | Run length | Positions tying for best | Records | Verdict |
> |---|---|---|---|
> | ≥ 2 | **exactly one** | 1,285 | unambiguous, trustworthy |
> | 1 | **six or more** | 300 | meaningless — the search picks arbitrarily |
>
> The length-1 hits are not bindings at all: their parameter hash is `0x00000000` in **290 / 300** (a real slot carries a constant — slot 1 is `0x2808EB90` in 1,266/1,266), and the names they select are placeholders: `missing.tga` (189), `flat-normalmap_n.tga`, `normal_blank_n.tga`, `normal_temp_n.tga`. These meshes — LOD and placeholder-material meshes — simply have no per-material texture binding, and an unguarded reader would bind `missing.tga` to a real surface and render something plausible rather than failing.

**The hypotheses this replaced** are kept because each is a natural first guess and each is refuted, not merely untested:

| Hypothesis | Verdict |
|---|---|
| `textureNames[materialId]` — id indexes the name list directly | **Refuted.** `max(id) < textureCount` fails outright on 3 / 549 meshes. |
| A fixed number of textures per material | **Refuted.** `textureCount % materialCount == 0` holds on 19 / 277 multi-material meshes — chance. |
| Materials own a contiguous slice of the name list | **Refuted by counter-example.** One character has 5 materials and 8 names because a `_d`/`_n` pair is *shared* across two materials while other slots differ in count. |
| An index array hidden in the material block's name-table padding | **Refuted.** The declared name-table length matches the string data exactly — **zero slack**. |
| The name list is referenced by string hash somewhere in the file | **Refuted.** No value anywhere in the file matches any of the three documented engine hashes (rotate-6/XOR, table-driven CRC-32, multiply-33) of any texture name, with or without extension, in either case. |

**Where the answer almost certainly is.** Every one of these files continues **past the end of the Mesh sub-block** — a region no pass in this project had walked, since the geometry chain ends there. It is not padding:

- **No mesh's file ends at the Mesh block: 0 / 428.**
- **Its size scales linearly with the material count** — median 458 bytes at 1 material, 748 at 2, 1,092 at 3, 1,440 at 4, 1,788 at 5, 2,160 at 6, 2,500 at 7 (≈ 348 bytes per material).
- **It is periodic with exactly one period per material.** Testing every stride and base offset for self-similarity of the zero/non-zero word pattern gives a best period repeating **5, 5 and 7 times** on three meshes whose material counts are 5, 5 and 7, at **0.98–0.99** agreement. The stride itself varies by file (336–352 bytes), so the records are variable-length.

So the per-material data is one record per material, immediately after the Mesh sub-block. Decoding it is the obvious next step and was not attempted this pass. **[CONFIRMED — empirical, that the region exists and is one record per material; OPEN — its contents, and therefore the id-to-texture binding.]** *(Superseded: contents and binding now CONFIRMED — §12.8; `spec-vehicle-geometry.md` §11.2.)*

**Until it is decoded, do not guess a mapping.** A wrong texture assignment renders a plausible-looking character that is silently wrong, and the usual check — looking at it — cannot catch that. Flat per-material colour is the correct interim: it displays exactly what is actually known.

*(One negative result recorded so it is not re-derived: sub-header `+0x10` is **not** the LOD/group count. It matched on the first two meshes examined and holds in only **189 / 428** — a coincidence caught by population-testing a field identification that looked settled on a small sample.)*

---

### 8.4 The material-set advance rule — fixed-stride arrays, then a per-material walk (2026-09-11)

The implementation team asked whether the material region uses **length-prefixed
sub-records** (as another carrier's material records do) or a **fixed stride**. The
answer is **neither alone — it is fixed-stride descriptor arrays followed by a
separate variable-length per-material loop**, and because vehicles run this exact
same function (`spec-vehicle-geometry.md` §7.2 Q1), the rule holds for `.ccar_pc`
identically.

The parse cursor enters the array-data region at **sub-header `+0x88`** (§2) and is
advanced like this:

```
N2 = u16(subheader + 0x0E)          // a SECOND count, not previously documented
if N2 > 0:
    cursor = align8(cursor)
    array_A = cursor;  cursor += N2 * 8      // fixed stride 8
    array_B = cursor;  cursor += N2 * 4      // fixed stride 4

N  = u16(subheader + 0x0C)          // the MATERIAL COUNT (§8.3)
if N > 0:
    cursor = align8(cursor)
    array_C = cursor;  cursor += N * 8       // fixed stride 8
    repeat N times:                          // per-material, VARIABLE advance
        <helper>(base, &cursor, ...)         // each material consumes its own length
```

**So there are no length-prefixed sub-record headers to find at the array level** —
the boundaries are `count × fixed size`, with an 8-byte alignment before each array
and nothing between elements. Any greedy segmentation of that region is solving a
problem that does not exist; the counts give the boundaries directly. The
**variable-length** part is the per-material loop that follows, which runs exactly
`u16(+0x0C)` times, each iteration consuming as much as that material needs.

**[CONFIRMED — disassembly.]**

#### 8.4.1 A consequence: `+0x0C` is an exact count of material records

`+0x0C` **drives the per-material loop**, so it is the precise number of material
records present — not an approximation of it. That matters because §26.9 of
`HANDOFF.md` recorded `+0x0C` as *"exact on meshes, only an upper bound on
vehicles"*, and **that phrasing is now visibly the wrong frame rather than a wrong
measurement.** The count is exact on both; what is *not* exact on vehicles is the
relationship between the count and the number of **draw runs**, which is a different
quantity. A run references a material, several runs may share one, and a material
need not be referenced by any run — so `runs ≤ materialCount` is the relation to
expect, one-sided, with equality merely common on meshes.

> ### ⚠ Corrected 2026-09-11 — this paragraph originally stated the relation **backwards**
>
> It read: *"the implementation team measured exactly that independently — runs `<`
> count in 369/372, `==` in 3/372, never `>`."* **That figure counts BINDING runs. This
> section is about DRAW runs. They are different quantities and the substitution
> inverts the conclusion.** Re-measured here directly over every `.ccar_pc`
> (393 located, 90,290 draw ranges):
>
> | Relation to `materialCount` = `u16(subheader+0x0C)` | `<` | `==` | `>` |
> |---|---|---|---|
> | **draw range count** | 1 | 0 | **392** |
> | distinct **masked** material ids (`field & 0xFFFF`) | **393** | 0 | 0 |
>
> **Draw ranges EXCEED `materialCount` in 392 of 393 vehicles** — the opposite of what
> was published. And this section's own premise predicted that: *"a run references a
> material, several may share one, a material need not be referenced"* implies draw
> runs ≥ distinct materials used. **The premise was right and the conclusion
> contradicted it**, which is the tell that should have been caught before publishing.
>
> **What IS bounded** is the number of *distinct materials referenced*, once the
> draw-range id is **masked** — `field & 0xFFFF`, dense from 0, per §8.2. That holds in
> **393/393**, extending §8.2's existing 86/86 measurement to the full population. So
> the intended claim was true of the right quantity all along and was attached to the
> wrong one.
>
> **⚠ But that 393/393 is itself a WEAK result, and it is stated here so nobody quotes
> it as strong** (clean team's catch, the same near-vacuity test I had just applied to
> my own high-half number one paragraph earlier). **Ask what a failure would have
> looked like:** a draw range naming a material id that is not a valid index at all —
> i.e. a corrupt file. So "393/393" says approximately *"no vehicle has an
> out-of-range material id"*. That is a genuine invariant and worth having, but it is
> **not evidence about the association**, and it is never even close to tight: no file
> has zero slack and the mean slack is 21.6.
>
> **The informative measurement is the slack itself — see §8.4.3, where it turned out
> to be a real finding rather than a diagnostic.**
>
> **A second error was nearly published inside this correction.** Comparing the
> draw-range id **unmasked** against `materialCount` appears to show distinct ids
> exceeding it in 385/393 — which is meaningless, because that field is **packed
> 16:16** (§8.2), a trap this project had already documented in `HANDOFF.md` §26.9 and
> which I walked into while fixing a different error. Masked, the relation reverses to
> 393/393. **Also discarded as near-vacuous:** `max(field >> 16) < materialCount` at
> 393/393 sounds supporting but is not — the high half runs 0–14 while `materialCount`
> has a median of 44, so it could barely have failed.

An equality guard was never correct for either carrier: for **binding** runs the
bound holds but equality is incidental, and for **draw** runs the bound does not hold
at all.

#### 8.4.2 What a name-validity check must require

Relaxing an equality guard to `≤` is **not sufficient on its own**, and the reason is
worth stating because it produces a silently-plausible result. A check that accepts
"any printable string containing a dot" also accepts **mid-name offsets**, because the
name blob is a run of NUL-separated strings — so a pointer landing partway into
`T_bulldog_D1B.tga` yields `_bulldog_D1B.tga`, or in the limit a bare `.tga`, and all
three pass. Measured by the implementation team against characters as a
known-correct control: characters **100.00%** name-at-boundary, vehicles **99.44%**,
with 226 entries whose sampler hash reads below 256 or as `-1`.

**A ~1% impurity is roughly 99 wrong bindings across the population, and 99%-correct
renders fine on screen.** The structural requirements that make it checkable:

1. **The offset must land on a name boundary** — the first byte of the blob, or the
   byte immediately after a NUL. The blob begins with a **leading NUL** by the
   `align16(cur+4) + 1` convention (§8.3), so offset 0 is not a name.
2. **The sampler hash must be a full 32-bit constant.** §8.3 established that the
   *hash*, not the slot index, identifies the sampler; a value under 256 or `-1` is
   not a hash and is the cheapest available rejection test.

**These two requirements are correctness checks, not just measurement predicates —
confirmed the hard way.** The implementation team built the name-start test purely to
*quantify* the truncation problem, then moved it into their reader's actual name
validator. It is neutral where things already worked (characters unchanged, 546/546
located and 6,064/6,064 cross-checked, full suite green), but on vehicles **2 of the 3
previously "successfully bound" vehicles flipped to rejected**. Those two had at least
one truncated name accepted as real: **live wrong bindings that had been presenting
as correct on exactly the path a renderer trusts.** A diagnostic that changes a
verdict when promoted to a check was never merely diagnostic.

**And the shape of the original guard is worth understanding, because it is not
intuitive.** The old guard required an exact run-count match. It was **too tight in
the dimension everyone was watching** — whether it accepted enough vehicles, the
visible "why only 3 of 372?" complaint — while being **wide open in a dimension nobody
was watching**, happily accepting a name carved out of the middle of another string.
**Strictness is not a scalar.** A guard can simultaneously reject true cases and
accept false ones, and the loud half conceals the quiet half: the too-tight failure
generates complaints, the too-loose failure generates renders that look fine.

#### 8.4.3 Vehicle material sets are PAIRED — half of every set is unreferenced

Following the clean team's point that the *slack* is more informative than the bound
that brackets it, it was measured directly — and it is not noise:

| | characters (`.ccmesh_pc`) | vehicles (`.ccar_pc`) |
|---|---|---|
| files measured | 549 | 393 |
| median `materialCount` | 2 | 44 |
| referenced ids form a dense prefix `0..k-1` | **549 / 549** | **393 / 393** |
| mean slack (declared − referenced) | **0.0** | **21.6** |
| files with **zero** slack | **549 / 549** | **0 / 393** |
| slack as a share of declared | **0.0%** | **49.9%** |

On characters every declared material is referenced by a draw range, exactly, with no
exceptions. **On vehicles almost exactly half are referenced by nothing**, and never
zero. Since the referenced ids are a dense prefix, the unreferenced materials are a
contiguous **suffix** `[k, materialCount)`.

That 49.9% is not approximate. Testing the ratio directly:

| Relation | Vehicles |
|---|---|
| `materialCount == 2 × referenced` | **351 / 393** |
| `materialCount == 2 × referenced − 1` | 36 / 393 |
| other | 6 / 393 |
| ratio within `[1.9, 2.1]` | **383 / 393** |
| **maximum ratio observed** | **exactly 2.000** |

**The ratio never exceeds 2.** So a vehicle declares one material per referenced
material plus one more, and the second is bound to no geometry.
**[CONFIRMED — empirical, 393 vehicles against a 549-file control at ratio 1.0.]**

**This explains the "upper bound" behaviour directly.** §26.9 of `HANDOFF.md` recorded
`+0x0C` as behaving like an upper bound on vehicles and an exact count on meshes. It
is an exact count of *declared* materials on both (`HANDOFF.md` §26.16, from disassembly) — what
differs is that **vehicles declare twice what they draw**, so any test comparing the
count to something geometry-derived was guaranteed to miss by a factor of two. Six
earlier count-matching candidates were refuted at chance against a quantity that was
never going to match. **"Upper bound on vehicles" is retired as a description of the
field.**

> **⚠ This does NOT revive counting as a route to the association, and must not be
> read that way** (distinction the clean team asked to have held explicitly). Both of
> these are true at once: the six candidates failed for a reason that is now
> *explained*, **and** counts still cannot produce an association. **A corrected count
> tells you HOW MANY, never WHICH.** The operative statement remains the stronger,
> principled one — the association is **not recoverable from counts at all**, because
> no cardinality, however exactly matched, distinguishes *which* material a given run
> uses. Fixing the 2× removes a false explanation for the failures; it does not turn a
> counting approach into a viable one. The routes remain the high half of the packed id
> and the unread per-material helper (then `HANDOFF.md` §27.2). *(Both routes since closed: the high half is a vertex-channel index, `spec-vehicle-geometry.md` §11.1; the helper was read and has no identity field — association is positional, ibid. §11.2/§7.1.)*

**Independently confirmed.** The clean team re-measured on a separate population (372
vehicles) with separate tooling: **336/372 = 90.3%** at exactly 2× against this
document's **351/393 = 89.3%**, the same cap at **2.0000 never exceeded**, and the
character control at **exactly 1.0000** across all 549 files when it could have landed
anywhere. **[CONFIRMED — empirical, two independent measurements and two toolchains.]**

> **One artifact, flagged rather than buried** — because an unexplained number beside a
> clean result is exactly what a future reader tries to reconcile as real. Their
> `== 2× − 1` bucket reads **272/549 on characters**, which appears to contradict the
> clean 1.0000 ratio. It does not: the test is **degenerate at exactly one material**,
> where `declared == d` and `declared + 1 == 2d` are both trivially true, so those are
> single-material meshes counted by a test that cannot discriminate there. Vehicles are
> unaffected, and **this document's own control is slack-based** (`declared − referenced
> == 0` in 549/549), which has no such degeneracy.

**What the second half IS remains open.** The obvious reading — a paired variant per
surface, with vehicle **damage state** the natural candidate in this title — is a
**[HYPOTHESIS — unconfirmed]** and is doing no work in the conclusion above. The
measurement stands on its own: the pairing is real, the partner is unreferenced by
geometry, and something other than a draw range must select it.

**Why this is the best remaining lead on the association.** Roughly 22 declared
materials per vehicle are inert as far as draw ranges are concerned. If the
per-material record carries an association key at all (then `HANDOFF.md` §27.2; *it does not — read since, `spec-vehicle-geometry.md` §11.2*), the unreferenced half
is where evidence for what it keys *on* would be most visible — those records exist
for a reason that is not "a draw range points at me". Harness `car_runcount.py`.

## 9. What a reader needs, in order

1. Walk to the Mesh sub-block (`spec-geometry-format.md` §4.1.2); confirm its version field reads `9`.
2. Read the check value, the `c`-length and the `g`-length that precede the `0x70` header.
3. From the header take flags `+0x00`, channel count `+0x10`, index count `+0x20`, index size `+0x30`.
4. Read `channel_count` records of 24 bytes immediately after the header.
5. If flags bit 0 is set, the data is in the `g`-file; otherwise it is inline in the `c`-file at the same cursor. Bit 2 selects a different representation not covered here.
6. Walk the segment per §4: check value, align 16, index buffer, then each channel aligned to 16.
7. Decode each channel's elements per §6, using the layout code at record `+0x05` and the texture-coordinate count at `+0x06`.
7a. Read the group count at mesh header `+0x04` and the draw ranges that follow the groups (§8.2). **Draw one range at a time** — never the whole index buffer at once.
7b. To skin the mesh, map rig rest positions into vertex space with `(x, −y, −z)` and use the blend indices as direct bone-array indices — `spec-rig-format.md` §8.1.
8. Verify: the walk must consume exactly the declared `g`-length and end on a repeat of the check value. **If it does not, stop — do not guess.**

---

## 10. Open items

1. ~~**Layout codes 11, 13, 12 and 24** — validated and sized (32, 32, size unconfirmed, 20), but their element fields were not probed.~~ **Resolved, 2026-09-29 (§12.10):** normal/tangent now CONFIRMED for 11 (§12.9.5) and 12 (§12.10.2), both at `+8`/`+12`; position/normal CONFIRMED and the `+16` slot REFUTED-as-tangent for 24 (§12.3); 13 stays HYPOTHESIS-only (small sample). Code 12's size is now CONFIRMED at 36 bytes (32-byte base + one 4-byte texcoord set), three independent measurements agreeing (§12.10.1) — the one figure this item still listed as "unconfirmed." **Position remains OPEN for 11/12/13** *(superseded: position for 11/12/13 is now HIGH CONFIDENCE, FLOAT16×3 at `+0`, §12.12, pending a consumer trace)*, and the `+16` slot remains OPEN for 24 — see §12.10.5's summary table for the full, final per-code state rather than repeating it here.
2. **Codes accepted by the loader but never shipped** (5–10, 14–23, 25, 26) *(corrected: 5–10 are not accepted either — the case list is `0x00`–`0x04`, `0x0E`–`0x1A`, `0x64`–`0x65`, §12.9.3/§12.10.3; the accepted-but-unshipped codes are 14–23, 25, 26)* — layouts unknown, and unknowable from shipped data alone. **Correction, 2026-09-29: code 12 was wrongly included in this list.** ~~5–10, 12, 14–23, 25, 26~~ A peer team's real population sweep of all 11 shipped trees' every present LOD slot found code 12 genuinely shipped, in 3 real LOD slots (`st_pine_tall` LOD 1, `st_shrub_med` LOD 1, `st_shrub_sml` LOD 1) — not one of the codes this item originally claimed never ships. Moved to item 1 above as a real, present, unprobed code, alongside 11/13/24. **[CONFIRMED — empirical, real shipped data, cross-team.]** The remaining list (5–10, 14–23, 25, 26) is unaffected by this correction and was not independently re-verified this pass. **Further correction, same day (§12.10.3):** code 12 is, like 11/13, structurally excluded from the §7 load-time rewrite table (no switch case for `0x0B`/`0x0C`/`0x0D` in `FUN_00e711c0`, confirmed directly from the real case list) — so the "master per-layout-code declaration table" this item's own framing gestures at cannot contain entries for 11/12/13 by construction, and for 24 (which is accepted, flattened id = `texcoord_count + 0x57`) the table's one identified consumer is the same confirmed-unreachable dead end §12.9.14 already found. This closes the "read the master table directly" angle for all four codes at once, as a disassembly-confirmed negative rather than an unexplored option.
3. ~~**The 8 bytes at channel record `+0x08`** — not read by this parser, zero in every sample inspected. Something else may consume them.~~ **CLOSED — §12.1 (26,601/26,601 zero on disk); at runtime the GPU-upload routine stores the created vertex-buffer handle here (`spec-vehicle-geometry.md` §11.9.2).**
4. ~~**The fourth byte of the normal and tangent.**~~ **Half closed** (§6.2): the tangent's fourth byte is the **handedness sign** (two values only, 0 and 255) *(on every carrier except vehicles, where it takes 256 values — §12.5)*. **The normal's fourth byte remains open, but narrowed further, 2026-09-29 (§12.4/§12.13 item 1):** REFUTED as literal draw-range material id (0.11% exact match, and the byte is constant over regions finer-grained than a material-bounded range, mean per-range purity 0.64 vs. ~99% per-triangle) and REFUTED as vertex color (byte caps at 127, never reaches 255) — reinforces, without confirming, the "smoothing-group-style" reading already flagged as best-supported.
5. ~~**Texture-coordinate encoding.**~~ **CLOSED** (§6.5) — signed 16-bit fixed point, `int16 / 1024.0`. Half-float refuted.
6. ~~**Normal-versus-tangent assignment.**~~ **CLOSED** (§6.2) — decided by the handedness byte, not by convention.
7. ~~**Primitive topology.**~~ **CLOSED** (§8.1, §8.2) — triangle strips stitched with degenerate triangles, not lists; the list reading yields an impossible 0.53 triangles per vertex. The per-range subdivision is also now resolved (§8.2) and is required for geometric correctness. ~~The exact stitching convention *within* a range (repeats per restart, winding correction) remains open.~~ **CLOSED, 2026-09-29 (§8.1/§12.13 item 2):** standard alternating-parity strip rule, measured locally from each range's own start, no separate restart correction needed — 99.33% agreement against real decoded normals on 5M+ real triangles.
8. ~~**Component group B** — always zero in shipped data; its purpose is inferred from the summing arithmetic alone.~~ **CLOSED — §12.2 (26,601/26,601 zero; live-read and summed by the loader).**
9. **The multi-stream representation** (flags bit 2) — never observed set in the 1,937 paired meshes; the `g`-side scanner in §4.2 skips such blocks, so its frequency in other carriers is untested.
10. **Foliage and other inline-mode carriers** — foliage stores this block inline (flags bit 0 clear), so the `g`-side validation used throughout this document does not apply to it. Its channel records are readable by the same §3 rules, but were not replayed here.

---

## 11. Corrections applied to other documents

| Document | Correction |
|---|---|
| `spec-geometry-format.md` §4.1.1 | Channel data size uses the byte at record **`+0x04`**, not `+0x01` — a decompiler pointer-arithmetic misread (§3). |
| `spec-geometry-format.md` §4.1.1 | The `+0x20` × `+0x30` buffer is the **index buffer**, not "the single strongest candidate for the actual interleaved vertex buffer" (§8). |
| `spec-geometry-format.md` §4.1.2 item 6 | The ambiguity it left open — "consistent with a 16-bit index buffer *or* a compact vertex-attribute stream" — resolves to **index buffer** (§8). |
| `spec-geometry-format.md` §4.2 | `.clmesh_pc`/`.glmesh_pc` vertex data is **confirmed**, not "a plausible-but-unconfirmed guess" — ~~11,446~~ 12,628 blocks validated (§4.2; itself an under-count, corrected to 29,908 per the §4.2 warning). |
| `spec-geometry-format.md` §6 item 8 | The channel-array half is **closed**. Arrays 1, 2, 3 and 5 of the outer six-array structure remain open — this pass did not touch them. *(Arrays 1–3 since resolved, `spec-geometry-format.md` §4.1.5; array 5 remains open.)* |

## 12. The Remaining Unidentified Fields — Normal's 4th Byte, Layout Codes 11/13/24, Channel Record `+0x08`, Component Group B (2026-09-12)

This section closes out the five gaps `spec-vertex-format.md` §10 left open, working from a fresh
pass (`tools/harnesses/vtx_*.py`, `tools/scripts/VtxChannelRecordRead.java`). Two are closed as
strong negatives, one is substantially narrowed (three of five candidate interpretations refuted
by range arithmetic, a fourth weakened, a fifth left as the leading but unconfirmed candidate),
and one produces a real correction-candidate to §6.2. Population sizes are stated with every
figure per the project's Figure Rule; none of the totals below are subtracted or combined with a
figure from an earlier session — every count here was re-derived from this pass's own scans.

### 12.1 Channel record `+0x08` (8 bytes) — CLOSED, a strong negative

**File side.** `vtx_record_scan.py` re-ran the full six-carrier sweep (`mesh_scan.py`'s generic
g-side validator, which needs no knowledge of the enclosing container) across all 38 shipped
archives and reproduces the project's own published totals exactly: **17,934 validated Mesh
blocks, 26,601 channel records** — the same denominator `spec-vertex-format.md` §4.2 already
established, which is itself a check that this scan is counting the right population.
*(Population note: these counts come from the pre-fix 16-byte-grid scan — see the §4.2 warning; the zone, level-mesh and tree populations are under-counted. The negatives in §12.1–§12.3 have not been re-run on the corrected population.)*

For every one of those 26,601 records, both 32-bit halves of the 8 bytes at `+0x08`/`+0x0C` read
zero, broken out by carrier:

| Carrier | records | `+0x08` u32 == 0 | `+0x0C` u32 == 0 |
|---|---|---|---|
| `.ccar_pc` | 4,016 | 4,016 | 4,016 |
| `.ccmesh_pc` | 2,076 | 2,076 | 2,076 |
| `.clmesh_pc` | 16,723 | 16,723 | 16,723 |
| `.csmesh_pc` | 107 | 107 | 107 |
| `.csrt_pc` | 15 | 15 | 15 |
| `.czn_pc` | 3,664 | 3,664 | 3,664 |
| **total** | **26,601** | **26,601** | **26,601** |

**26,601 / 26,601, all six carriers, zero exceptions.** The predicate could fail — any single
nonzero byte anywhere in this 8-byte span, on any of 26,601 independently-authored records across
six different carrier formats and at least four different content pipelines (character, vehicle,
tree, zone/level-mesh tooling), would have shown up as a nonzero count in the table above. This
strengthens the earlier claim ("zero in every sample inspected", §3) from an unstated sample to the
full shipped population.

**Disassembly side.** `VtxChannelRecordRead.java` decompiled both functions that walk the 24-byte
channel-record array — `FUN_00e71410` (inline/c-file path) and `FUN_00e71740` (g-file path), the
same pair `spec-vertex-format.md` §4 already identifies as performing "the identical arithmetic
against different base buffers". Reading the loop bodies directly (own words, not reproduced
verbatim):

- The **layout-code rewrite pass** (§7) touches only record `+0x05` (written) and reads `+0x06`.
- The **size-computation pass** that follows reads record `+0x00` (element count), `+0x04`
  (component-group A size) and `+0x07` (component-group B size), and writes the data pointer at
  `+0x10` and zeroes `+0x14` — exactly the fields `spec-vertex-format.md` §3 already documents.

**Neither pass, in either implementation, reads or writes any byte in `+0x08`..`+0x0F`.** This is
true in both the inline-path and the g-file-path function, which perform identical arithmetic
against different buffers — so the negative is not an artifact of checking only one of the two
paths.

**[CONFIRMED — disassembly, both channel-array walkers read directly, neither touches the
displacement; CONFIRMED — empirical, 26,601 / 26,601 channel records across all six carrier
formats.]**

**What this does and does not prove.** Per the constant-value-trap caution: a field that is
uniformly one value tells you about plumbing, not semantics, and an unexplained constant is not a
confirmation. This field's constancy IS explained — the two functions that read every other byte
of the record never touch it — so the honest reading is "reserved/unused by the documented loader
path", not "meaningless forever". A different engine subsystem could in principle read the same
displacement through a different pointer (e.g. at the vertex-declaration/renderer level, well
outside this loader chain); searching the whole 41,785-function binary for such a consumer was not
attempted here, deliberately, per the standing rule that a whole-binary predicate over a small
struct displacement has no selectivity. Given the field is zero in literally every shipped
instance, even a real but unexercised consumer would never see a nonzero value in any shipped
asset — which is the practically load-bearing fact for anyone implementing this format. **OPEN,
narrowly:** whether any other engine subsystem reads this displacement at all *(partly answered: at runtime the GPU-upload routine stores the created vertex-buffer handle at this displacement, `spec-vehicle-geometry.md` §11.9.2)*; **CLOSED** for every
purpose a format implementer has.

### 12.2 Component group B — CLOSED, a strong negative, extended to the full population

Task 4 asked what varies with component group B. Answer: **nothing, anywhere, ever** — extending
the existing 2,029/2,029 (`.ccmesh_pc`/`.csmesh_pc` only) result to the full six-carrier sweep.

| Carrier | records | sizeB == 0 | sizeB != 0 | distinct nonzero values |
|---|---|---|---|---|
| `.ccar_pc` | 4,016 | 4,016 | 0 | (none) |
| `.ccmesh_pc` | 2,076 | 2,076 | 0 | (none) |
| `.clmesh_pc` | 16,723 | 16,723 | 0 | (none) |
| `.csmesh_pc` | 107 | 107 | 0 | (none) |
| `.csrt_pc` | 15 | 15 | 0 | (none) |
| `.czn_pc` | 3,664 | 3,664 | 0 | (none) |
| **total** | **26,601** | **26,601** | **0** | |

**26,601 / 26,601.** The predicate is the same shape as §12.1's: a single nonzero `sizeB` byte on
any record, in any of the six carriers, would have shown up. None did. This is not a dead field in
the sense of §12.1 — the disassembly already on file (`spec-vertex-format.md` §3, and reconfirmed
directly in this pass's read of `FUN_00e71410`/`FUN_00e71740`) shows the loader unconditionally
reads `sizeB`, sums it with `sizeA`, and multiplies by the element count to get the channel's byte
size (`(sizeA + sizeB) * count`) — a live, exercised code path, not a dead one. The engine's
"second interleaved component group per channel" mechanism is real and reachable; **no shipped
asset in the 26,601-record population ever populates it.**

**[CONFIRMED — empirical, 26,601 / 26,601, all six carriers; CONFIRMED — disassembly for the
arithmetic that would have used a nonzero value had one existed.]** This is exactly a
well-characterised negative with a control (the control is that the field is live-read and
arithmetically load-bearing, not skipped) — it is not being upgraded into "the engine never
supports a second component group", only into "shipped content never exercises it".

### 12.3 Layout codes 11, 13, 24 — partially closed

**Population, per code, from the full 38-archive sweep (`vtx_layout_probe.py`):**

| Code | Carrier | Channels | Vertices | Base (measured) | texcoord idx observed |
|---|---|---|---|---|---|
| 11 | `.csrt_pc` (trees) | 13 | 17,624 | 32 | 1 |
| 13 | `.csrt_pc` (trees) | 2 | 260 | 32 | 1 |
| 24 | `.czn_pc` (zones) | 448 | 581,647 | 20 | 0 |

Every channel of every code was decoded (not sampled) — these are the full populations of codes
11/13/24 across all 38 shipped archives; there is nothing left unexamined for lack of a sample.
*(Under-count: the 16-byte-grid scan bug, §4.2 warning. Corrected populations: code 11 = 22 channels / 28,457 vertices, code 13 = 4 channels / 1,172 vertices, §12.11.)*

**Code 24 — position and normal CONFIRMED, tangent-slot OPEN.** Position at `+0` decodes as a
finite `float3` in **581,647 / 581,647** vertices (1.000000) — the same test, at the same rate,
used to confirm position for every other code in `spec-vertex-format.md` §6.1. The vector at `+12`
passes the unit-length test in **581,647 / 581,647** — matching the normal signature everywhere
else in this format. **So code 24's first 16 bytes are structurally identical to code 2's
(position + normal), which its measured base of 20 already suggested.**

The last 4 bytes (`+16`, the slot that is a tangent in code 2) do **not** show a consistent
signature. Pooled across all 448 channels the unit-vector rate is only 8.0% and the
extremes-at-byte-3 rate varies from 0.0 to 1.0 depending on the individual channel (mean 0.4555) —
this is not the ~100% seen for a real tangent field anywhere else in this document, nor a flat
constant. Classifying each of the 448 channels individually by that slot's behaviour
(`vtx_layout_detail.py`):

| Bucket | Channels |
|---|---|
| unit-vector AND binary 4th byte (tangent-like) | 1 (a 3-vertex channel — too small to trust, flagged rather than generalised) |
| binary 4th byte but NOT a unit vector | 62 |
| neither (all-zero, or neither signature) | 385 |

**REFUTED:** "code 24's last 4 bytes are the same tangent field as code 2's" — the pooled
unit-vector rate (8.0%) is far below the ~100% a real tangent shows on every carrier tested in
§12.4 below, and the one channel that does pass both tests is a 3-vertex sample, which the
project's small-sample rule (`HANDOFF.md` §5) says to flag rather than trust. **OPEN:** what the
62-channel "binary but not unit" bucket actually holds — it is a real, nonrandom sub-population
(14% of channels), not chance, but this pass did not characterise it further.
**[CONFIRMED — empirical for position/normal at `+0`/`+12`, full population; REFUTED — empirical
for the tangent-at-+16 reading; OPEN for what `+16` actually is.]** **Update, 2026-09-29 (§12.13
item 3):** FLOAT16×2, SHORT2 (raw and ÷1024), and USHORT2 raw are now ALL also REFUTED for `+16`
as position, secondary-UV, or skinning-weight — a real, per-vertex-controlled negative, not merely
an unfitted candidate, extending this refutation well beyond the original tangent-shape test.
The field remains real (measurable per-vertex variation exists in every channel) and its content
remains OPEN — see §12.13 item 3 for the full test, including a methodology trap it caught along
the way.

**Codes 11 and 13 (trees) — the base position-at-`+0` convention itself fails.** Every other layout
code in this format decodes a finite `float3` at offset 0 in effectively 100% of vertices (§6.1,
and confirmed again for code 24 above). For code 11, scanning every 4-byte-aligned offset in the
36-byte stride (`vtx_layout_detail.py`, full 17,624-vertex population):

| Offset | float3 finite |
|---|---|
| +0 | 6,607 / 17,624 (0.3749) |
| +4 | 8,785 / 17,624 (0.4985) |
| +8 | 5,409 / 17,624 (0.3069) |
| +12 | 10,363 / 17,624 (0.5880) |
| +16 | 7,547 / 17,624 (0.4282) |
| +20 | 9,828 / 17,624 (0.5576) |
| +24 | 5,259 / 17,624 (0.2984) |

**No offset comes close to the ~100% baseline** every other code achieves. **REFUTED:** trees use
the plain-`float3`-position-at-a-fixed-offset convention the rest of this format uses.
**[CONFIRMED — empirical, full 17,624-vertex population, 7 candidate offsets tested.]** The
straightforward reading is that tree/foliage geometry uses a different position encoding
entirely — plausibly quantized or reconstructed relative to a per-branch pivot, consistent with
this carrier's known wind-animation and per-tree parameter table (`spec-tree-format.md` §4/§13.2) — but
this pass did not identify what it is. **Concrete next step:** trace the disassembly consumer
specific to trees (`FUN_00a71810`/`FUN_00e7a5a0`, `HANDOFF.md` §17's tree chain) rather than
continuing to black-box-search offsets, per the standing rule that a confident negative from a
shape-dependent search is the signal to switch methods, not to keep guessing shapes.

**Code 13 is flagged, not concluded, for a different reason: the sample is too small to trust.**
Only 2 channels / 260 vertices exist in the entire shipped game *(under-count: corrected to 4 channels / 1,172 vertices, §12.11)*. At `+12` this tiny population
reads **260 / 260 (1.0000)** finite — which would ordinarily read as a clean confirmation, but per
the small-sample rule (`HANDOFF.md` §5, "don't generalize from one small sample, ever — flag it
instead") a result from 2 channels is exactly the shape that rule warns about, and two same-type
samples agreeing is explicitly listed there as *not* confirmation on its own.
**[HYPOTHESIS — unconfirmed: position at `+12` for code 13, insufficient population to promote
further.]** *(Superseded: `+12` is the tangent slot for codes 11/13, §12.9.5 (code 13 as small-sample corroboration); position is FLOAT16×3 at `+0`, §12.12.)* Codes 11 and 13 share a measured base (32 bytes) but that is not evidence they share a
layout — `spec-vertex-format.md` §5 already notes the codes are "an enumeration... not a bitfield",
so equal bases can be coincidental (code 4 already breaks the regular-family reading at base 12).

### 12.4 The normal's 4th byte

**Population.** `vtx_normal_probe.py` decoded **555,771 vertices across 1,102 sampled channels, 10
(carrier, layout-code) classes, spanning 4 of the format's 6 carriers** — characters (`.ccmesh_pc`,
`.csmesh_pc`), vehicles (`.ccar_pc`), level meshes (`.clmesh_pc`) and zones (`.czn_pc`), from 8
archives (`characters`, `customize_item`, `items`, `vehicles`, `dlc1`, `dlc2`, `dlc3`,
`sr3_city_0`). Trees are excluded here because their normal-bearing layout (if any) is codes 11/13,
whose position field itself is unresolved (§12.3) — decoding a "normal" at a fixed offset there
would be building on sand. Most classes are capped at a 150-channel, 1,000-vertex-per-channel
sample; two (`.ccmesh_pc` layout 1, `.csmesh_pc` layouts 0/2) are exhaustive because fewer than 150
channels of those exist in the archives scanned. This is a large, multi-carrier sample, not the
full 26,601-record population — stated so the figure is not mistaken for one.

**Internal correctness check, run before trusting anything else here:** the same offset arithmetic
was used to re-decode the tangent's own 4th byte (already CONFIRMED as handedness, §6.2) as a
control. It reproduced the known result exactly in every character/level-mesh/zone class tested —
distinct values = 2, extremes rate 1.000000 — in `.ccmesh_pc` layout 3 (105,583/105,583),
`.clmesh_pc` layout 2 (79,949/79,949), `.csmesh_pc` layout 2 (9,306/9,306), `.czn_pc` layout 2
(7,733/7,733). Since this control reproduces a known-true result exactly, the same arithmetic
applied to the normal's own byte is trustworthy.

**New range fact, load-bearing for the hypotheses below.** In **every one of the 10 classes**, the
normal's 4th byte's maximum observed value is **exactly 127**, never higher — i.e. **bit 7 (0x80)
is 0 in all 555,771 sampled vertices.** `spec-vertex-format.md` §6.2 already noted extremes
(≤1 or ≥254) are rare (0.0002); this pass makes it exact and much stronger: the entire top half of
the byte's range is unused, not merely rare at the very top. **[CONFIRMED — empirical, 555,771 / 6
carrier/layout-independent samples, zero exceptions.]**

**Testing the five candidate interpretations, range arithmetic first:**

1. **A second handedness/sign bit — REFUTED.** A sign or flag bit's achievable range is 2 values
   (or a small handful, combined with something else). Observed: 103–128 distinct values per
   class, with the byte using all 7 low bits (max exactly 127 in every class). The achievable range
   of "a bit" cannot produce the observed range. A failing case for this refutation would have been
   any class showing ≤ 4 distinct values; none did.

2. **A blend-weight remainder — REFUTED by range arithmetic before any correlation was run.**
   §6.3 already establishes the weight-byte sum lies in `{254, 255, 256}` only. Any remainder
   derived from that sum (`256 - sum`, say) can therefore only take the values `{0, 1, 2}` — a
   3-value range. Measured directly on both skinned classes: observed range **exactly `[0, 2]`**
   (12,592 vertices, `.ccmesh_pc` layout 1; 105,583 vertices, layout 3) — confirming the arithmetic,
   not the hypothesis. The normal's 4th byte in those same populations has **128 distinct values**,
   more than 40× wider than the remainder's achievable range, and the two agree in only
   **0.010–0.015%** of vertices (16/105,583 and 132/12,592) — chance-level. **REFUTED, twice over:
   by range before fitting, and by direct correlation after.**

3. **Padding — REFUTED trivially.** A padding byte is constant; this one takes 100+ distinct
   values in every class tested, with no class showing fewer than 104. This is the mirror image of
   the constant-value trap the task flagged: this field genuinely is not constant, so calling it
   padding would be the wrong direction of the same mistake.

4. **An occlusion/bake term — weakened by the decisive test below, though one supporting signal is
   real.** *Supporting:* at vertices that share an identical (rounded) 3D position within a channel
   (duplicate copies from a hard-edge/UV-seam split), the byte matches far more often than a
   same-channel random-pair control in 8 of 10 classes — e.g. `.czn_pc` layout 0: 6,123/9,428
   (0.6494) vs control 853/22,437 (0.0380), a 17× elevation; `.ccmesh_pc` layout 3: 12,854/18,540
   (0.6933) vs 15,106/29,617 (0.5100). A pure per-vertex geometric quantity unrelated to the shared
   surface point would show no elevation over the random-pair control; this rules that out and is
   compatible with occlusion (a property of the point, not the direction). *Against:* see (5) below
   — the finer triangle-level test this same data supports is much more specific, and it points
   away from a continuously-varying quantity.

5. **A material/smoothing-group-style region index — the best-supported candidate, and the
   decisive test.** For every class where the block's single channel could be matched unambiguously
   to the shared index buffer (8 of 10; `nchan == 1`, index element size 2), real triangles were
   built from the strip-decoded index buffer (§8.1) and scored for whether **all three corners read
   the identical byte**, against a random-triple control from the same channel:

   | Class | corners all-equal | control (random triples) |
   |---|---|---|
   | `.ccmesh_pc` layout 1 | 5,399 / 5,447 (0.9912) | 2,390 / 18,106 (0.1320) |
   | `.ccmesh_pc` layout 3 | 100,458 / 100,540 (0.9992) | 96,210 / 239,420 (0.4018) |
   | `.clmesh_pc` layout 0 | 4,748 / 4,768 (0.9958) | 295 / 13,462 (0.0219) |
   | `.clmesh_pc` layout 2 | 14,023 / 14,075 (0.9963) | 2,544 / 43,259 (0.0588) |
   | `.csmesh_pc` layout 0 | 4,946 / 4,996 (0.9900) | 220 / 12,807 (0.0172) |
   | `.csmesh_pc` layout 2 | 8,138 / 8,168 (0.9963) | 694 / 21,098 (0.0329) |
   | `.czn_pc` layout 2 | 1,109 / 1,111 (0.9982) | 1,662 / 1,788 (0.9295) |

   **State the failing case:** if this byte were a continuously-computed per-vertex quantity (bake
   term, quantization residual, anything smoothly varying across a surface with 100+ representable
   levels), real triangles would score close to their own control — three independently-computed
   nearby values landing on the *exact same* one of 100+ byte levels essentially never happens by
   chance, and the random-triple control in every row above confirms that (2–93%, driven entirely
   by how skewed that channel's own value distribution is). Instead every measurable class scores
   **99.0–99.9%** — real triangles are almost always byte-for-byte constant at this field, which
   only happens if it is constant over regions much larger than a single triangle. That is the
   signature of a per-region label (material id, smoothing group, or similar enumerated tag), not a
   computed scalar. Consistent with this: the per-channel distinct-value count is often small — the
   largest, cleanest class (`.ccmesh_pc` layout 3, 150 channels) has a **median of 3 distinct values
   per channel**, in the same order of magnitude as this carrier's own median declared material
   count of 2 (§8.4.3), though the two are not shown to be the same quantity.

   **[HIGH CONFIDENCE — inferred: the field is a per-region enumerated tag rather than a
   continuously-computed value, from the triangle-constancy test above, replicated across 4
   carriers and 8 classes with a control that could fail (and does, showing the channel's own
   chance rate) in every row.]** **NOT CONFIRMED:** what the tag actually indexes. It is not shown
   to equal the draw-range material id (§8.2/§8.3) directly — that would need per-range
   correlation against the already-decoded draw-range structure, which this pass did not run (the
   concrete next step, using `car_runcount.py`-style range decoding against this byte per range).
   A "smoothing group" reading (a finer partition than material, orthogonal to draw ranges) fits
   the evidence at least as well as "material id" and is not distinguished from it here.

**Net effect on `spec-vertex-format.md` §10 item 4 and §6.2:** the normal's 4th byte is not
handedness, not a weight remainder, and not padding (all three now REFUTED rather than merely
unfitted); it is most likely a region-constant index of some kind (HIGH CONFIDENCE, not proven);
and whether it is occlusion-like is weakened rather than settled. This section does not edit §6.2
or §10 directly — see §12.5 for how those should be read alongside it.

**Update, 2026-09-29 (§12.13 item 1):** the "concrete next step" flagged just above — correlating
this byte's dominant value directly against the real draw-range material id — has been run. It
REFUTES literal material-id identity (0.0011 exact match across 2,647 real draw ranges) and, more
fundamentally, shows the byte is constant over regions substantially *smaller* than a
material-bounded draw range (mean per-range purity 0.6366, vs. the ~99% per-TRIANGLE constancy
measured above) — i.e. finer-grained than material, not coextensive with it. This REFUTES material
id specifically while reinforcing rather than confirming the smoothing-group-style reading (a
partition finer than and orthogonal to material assignment is exactly what real smoothing groups
look like). Vertex color was also directly refuted on existing data (the byte caps at 127 in all
555,771 sampled vertices; a real color/alpha channel pins near 255 or spans 0–255, this does
neither). See §12.13 item 1 for the full test.

### 12.5 A carrier-specific correction candidate: the tangent's 4th byte is NOT a clean handedness signal on vehicles

While building the internal control for §12.4, the control **failed** on one of the five carriers it
was run against. `spec-vertex-format.md` §6.2 states, without a carrier qualifier, that the
tangent's 4th byte is a handedness sign taking only two values (0 and 255). That holds exactly on
every non-vehicle carrier this pass tested (four classes, all at 1.000000, tabulated in §12.4). It
does **not** hold on vehicles.

**Full vehicle population** (`vtx_veh101_check.py`, 388 `.ccar_pc`/`.gcar_pc` pairs from
`vehicles.vpp_pc`, `dlc1.vpp_pc`, `dlc2.vpp_pc`, `dlc3.vpp_pc` — layout 101, position + normal +
tangent + rigid part index):

- The vector at `+16` passes the unit-length test in **832,194 / 832,194** vertices, and bytes
  `+20..+23` (the rigid part index, §6.4) are all identical in **832,194 / 832,194** — both confirm
  the offset arithmetic is correct (a decisive control tested against the alternative ordering:
  swapping which 4-byte group is the tangent and which is the rigid index scores **0 / 832,194** on
  both of the same tests, so the ordering used matches every other measurement in this document).
- With the offsets confirmed, the tangent's own 4th byte (`+19`), measured on the full population
  of **3,405,780 vertices**: **256 distinct values** (not 2), range 0–255 (not `{0, 255}`), and
  only **610,935 / 3,405,780 (17.94%)** sit at the two extremes — against **100.0000%** on every
  other carrier tested. Top values by frequency: `0` (14.6%), `127` (6.5%), `128` (5.9%), `255`
  (3.3%), then a long tail — i.e. the two sign extremes plus a strong cluster at the byte's
  midpoint (127/128 ≈ 0.0, under the same signed decode used for the vector's other three bytes),
  not a clean binary split.
- This byte does not simply duplicate the normal's own 4th byte at the same vertex: they agree in
  only **18,000 / 3,405,780 (0.53%)**, at or below the ≈0.78% chance rate implied by the normal
  byte's own 128-value spread.

**Failing case, stated:** had the offsets been wrong (e.g. tangent and rigid-index swapped), the
unit-length and all-identical controls above would have failed; they did not, on the full
population, so the byte being measured is genuinely the slot that holds the handedness sign on
every other carrier. **[CONFIRMED — empirical, full 388-vehicle / 3,405,780-vertex population.]**
**Correction candidate:** `spec-vertex-format.md` §6.2's tangent-handedness finding should be read
as confirmed on characters, static props, level meshes and zones, and **not yet established on
vehicles**, where the same structural slot carries a richer, non-binary value whose meaning this
pass did not determine. This is recorded here rather than edited into §6.2 directly, per this
project's standing rule that corrections stay visible in place — a future pass should fold it in
alongside a decision on whether vehicles genuinely lack encoded handedness or encode it
differently.

### 12.6 One adjacent, unlabelled field noticed in passing — flagged, not chased

Reading `FUN_00e71410` for §12.1 surfaced two Mesh-header fields this document does not currently
list (the header table only covers `+0x00`, `+0x10`, `+0x18`, `+0x20`, `+0x28`, `+0x30`): header
`+0x04` gates whether the channel/primary-buffer walk proceeds at all (nonzero, or a called
helper's return value nonzero), and header `+0x08` stores that helper's return value (a pointer;
its high half at `+0x0C` is zeroed, the same pattern used for every other pointer-sized field in
this header). The helper (`FUN_00e71310`) takes at least one implicit register-passed argument the
decompiler drops, and an earlier team's own comment on that function already flags it as needing
raw-x86 argument tracing rather than decompiler output. **Scope-jump discipline applies:** this is
a real, previously-unlisted pair of header fields, but chasing it fully is a separate investigation
from the five assigned here and was not pursued past noticing it. **[OPEN / UNKNOWN — flagged for
a future pass, not this one.]** **[Resolved 2026-09-13: `+0x04` = group count, `+0x08` = group-array pointer returned by `FUN_00e71310` — §8.2; `spec-vehicle-geometry.md` §11.9.1.]**

### 12.7 Summary

| Item | Verdict | Confidence |
|---|---|---|
| Channel record `+0x08` (8 bytes) | Not read by either channel-array walker; zero in 26,601/26,601 records, all 6 carriers | CONFIRMED — disassembly + empirical |
| Component group B | Zero in 26,601/26,601 records, all 6 carriers, despite being live-read/summed arithmetic | CONFIRMED — empirical + disassembly |
| Layout code 24 (zones) | Position/normal confirmed (581,647/581,647); tangent-slot reading REFUTED, actual content OPEN | CONFIRMED (pos/normal) / OPEN (`+16`) |
| Layout codes 11/13 (trees) | Plain-float3-position convention REFUTED for code 11 (full 17,624-vertex population); code 13 flagged, sample too small (2 channels) | CONFIRMED (refutation) / HYPOTHESIS (code 13) |
| Normal's 4th byte: sign bit, weight remainder, padding | REFUTED (range arithmetic + direct measurement) | CONFIRMED — empirical |
| Normal's 4th byte: occlusion/bake term | Weakened — one supporting signal, one decisive signal against | HIGH CONFIDENCE against, not fully refuted |
| Normal's 4th byte: region index (material/smoothing-group-like) | Leading candidate — 99.0–99.9% per-triangle constancy across 4 carriers | HIGH CONFIDENCE — inferred |
| Tangent 4th byte = handedness, on vehicles specifically | Does not hold as currently stated in §6.2 (17.94% extremes, not ~100%) | CONFIRMED — empirical (correction candidate) |
| Mesh header `+0x04`/`+0x08` | Previously unlisted; gates the channel walk / stores a helper's pointer return | OPEN / UNKNOWN **[Resolved 2026-09-13: group count / group-array pointer, §8.2; `spec-vehicle-geometry.md` §11.9.1.]** |

### 12.8 A relayed finding, verified: §8.3's texture-binding search can be replaced by direct arithmetic

A parallel session working the vehicle material question relayed a candidate simplification to
§8.3 without editing it, per the same rule this document follows: that the texture-binding run's
offset, measured from a per-material record's own internal `0x30`-byte header (identified there as
`FUN_00e71090`, the shared inline-material-definition parser this project already documented for
trees and foliage, `spec-tree-format.md` §6 / `spec-foliage-format.md` §6), is fixed —
`align4` immediately after the header — even though the record's own position in the file moves.
The instruction was explicit: verify against this project's own population before changing
anything. This subsection is that verification.

**The formula tested, stated as code, chaining from the sub-header's already-established fields:**

```
mo    = <the embedded Mesh sub-block's version-field position>   // spec S2 / geometry-format S4.1
c_len = u32(mo + 0x08)                       // measured from `mo` itself, NOT from checkval
cursor = mo + c_len                          // end of the Mesh sub-block's c-file footprint

N2 = u16(subheader + 0x0E)
if N2 > 0: cursor = align8(cursor); cursor += N2*8 + N2*4      // array_A, array_B (S8.4)
cursor = align8(cursor)
cursor += N * 8                              // array_C, N = materialCount (S8.4)

// per material i = 0 .. N-1, chained:
size          = u32(cursor)                  // this material's own record length
header_start  = align8(cursor + 4)           // the record's internal 0x30-byte header
A             = u16(header_start + 0x0C)     // texture-binding count (S6, foliage/tree)
run_start     = header_start + 0x30          // ALREADY 4-aligned; no further pad needed
cursor        = cursor + size                // ⚠ measured from the SIZE FIELD'S OWN position
```

**Two measurement-origin traps, both hit and fixed while building this check** (recorded because
the relay's own methodology note predicted exactly this shape of error, and it is worth having a
second, independently-hit instance on file rather than only the first-hand account):

1. **`c_len`'s origin.** A first attempt measured `c_len` from `checkval_pos` (by analogy with
   `g_len`, which `vertex_decl.walk_to_mesh` measures from the check value's own position). That
   produced a uniform, total failure — a full-record scan for the run's own defining invariants
   found it **nowhere in the record, at any offset, in 100% of files tried** — which is the "0 of N
   recovered" shape `HANDOFF.md` §5 says to read as a plumbing bug before a format conclusion, not
   as a negative result about the layout. The already-proven `batch_probe.walk_full` (the function
   every downstream texture-binding tool in this project's harness set is built on) measures
   `c_len` from `mo`, the version field's own position, not from checkval. The `g_len` convention
   does not carry over; assuming it did was a real instance of the trap the relay warned about, not
   a hypothetical one.
2. **`name_offset`'s origin.** Not mentioned in the relay, found independently while debugging (1):
   the texture-binding entry's `name_offset` is **relative to the mixed-case blob's own start**, not
   an absolute file position. §8.3's existing prose ("byte offset of a texture name *within* the
   mixed-case name blob") already says this correctly, but nothing in this document previously
   stated it as sharply as "relative, not absolute" for an implementer building a validity check —
   testing it as an absolute file offset also produces zero matches, for the same reason as (1).

**Result, corrected-origin, full population, both carriers:**

| Carrier | files (materialCount 1–64, block resolved) | whole-mesh match (every `A>0` material's run lands exactly at `header_start+0x30`) |
|---|---|---|
| `.ccmesh_pc` (characters, from `characters.vpp_pc`, `vehicles.vpp_pc`, `dlc1–3.vpp_pc`, `customize_item.vpp_pc`, `items.vpp_pc`) | 2,002 | **2,002 / 2,002** |
| `.ccar_pc` (vehicles, from `vehicles.vpp_pc`, `dlc1–3.vpp_pc`) | 365 | **365 / 365** |

Broken out **per material index** (0 through 61, the largest observed vehicle material count),
every single index scores **1.0000** on both carriers, with zero exceptions — the only entries
excluded from the denominator are materials whose own header declares `A == 0` (no texture
bindings at all), which is `§8.4.2`'s own already-documented "material has no binding" case, not a
failure of this formula: there is nothing for it to validate there, and it correctly reports
nothing rather than a false run. **[CONFIRMED — empirical, exact, whole population, both
carriers.]** The predicate could fail case by case (any material whose real run does not start
exactly at `header_start + 0x30`, or whose declared `A` does not match a real sequential run
there) and did not, once on 2,002 character files across every material index each carries, and
again independently on 365 vehicle files across up to 62 material indices each.

**What this means for §8.3, stated but not applied there:** the sliding search §8.3 currently
requires ("Locate it by its own constraint... A fixed-offset reader will be wrong") is not actually
needed — that conclusion was correct about the *record's* variable position in the file, but the
offset *within* a located record is constant. Direct arithmetic from `header_start` — itself
reached by chaining `u32 size` fields exactly as `spec-vertex-format.md` §8.4 (unmodified here)
already documents — replaces it as the primary path; the search remains a valid, useful fallback
for verifying a computed position or recovering from a corrupt/unusual record. This document does
not edit §8.3 directly, per this project's standing rule that corrections stay visible in place
rather than quietly overwriting the text they update; a future pass should fold this in as the
primary method there, with the search demoted to a cross-check.

### 12.9 Layout codes 11/13 (trees) — normal and tangent located via the tree-specific disassembly chain; position still open (2026-09-12)

*(Position superseded by §12.12: FLOAT16×3 at `+0`, HIGH CONFIDENCE. The FLOAT32 negatives in §12.9 remain valid for FLOAT32 only.)*

§12.3 refuted the plain-float3-position convention for codes 11/13 by black-box offset scanning
(best offset 58.8% finite against a ~100% baseline) and named the tree-specific disassembly chain
as the next step rather than further offset guessing. This subsection follows that chain —
`FUN_00a71810` (the registered `.csrt_pc` constructor) → `FUN_00a6f780` (`'TREE'` block +
geometry sub-structure entry, `spec-tree-format.md` §3–§5) → `FUN_00e7a5a0` (the geometry
sub-structure parser) → the material-set block (`FUN_00e40210`) / index list (`FUN_00e401d0`) /
Mesh sub-block wrapper (`FUN_00e718b0`) → the shared Mesh sub-block walkers `FUN_00e71410`
(c-file) / `FUN_00e71740` (g-file) → the load-time layout-code rewrite `FUN_00e711c0` — and, per
the task's framing that the real consumer "may be a sibling function to the material one," also
`FUN_00a6f500` and `FUN_00a713e0`, the two functions the tree constructor calls immediately after
the file parse completes.

**12.9.1 No CPU-side field consumer exists anywhere in this chain — CONFIRMED — disassembly.**
`FUN_00e71410`/`FUN_00e71740` (already the subject of `spec-vertex-format.md` §4's "two separate
functions implement this walk... against different base buffers") were read in full for this
pass. Neither one reads or branches on a vertex element's own field bytes (position, normal,
texcoord, or anything else in the 0–31 range this section is about). They read only the channel
record's count, sizeA, sizeB, layout-code byte and texcoord-count byte — purely to compute
strides, alignments and pointer fixups — then advance a cursor by `count × (sizeA+sizeB)` without
ever dereferencing the data itself. This confirms directly, from the actual decompiled bodies
rather than by inference, what §6's empirical (not disassembly-derived) methodology already
implied: nothing on the CPU side interprets an individual vertex field's byte content for *any*
layout code, tree or generic. Whatever assigns semantic meaning to those bytes does so on the GPU,
via a vertex declaration built from the layout code — which is why the generic codes' fields were
only ever established by statistical byte tests (§6), never by reading a consumer.

**12.9.2 `FUN_00a713e0` is the collision-capsule → physics-primitive builder, not a vertex-buffer
build — CONFIRMED — disassembly, correcting an open inference in `spec-tree-format.md` §2.**
That document's own text reads: "a large GPU-side build follows (`FUN_00a713e0`, not parsed
here — it reads no further file bytes)." Read in full for this pass, `FUN_00a713e0` does not touch
the LOD Mesh sub-blocks or their channel arrays at all. It iterates exactly the tree's own
collision-capsule count (`'TREE'` block `+0x18`) over the capsule array (`+0x20`/`+0x24`), stride
`0x60` per record — precisely `spec-tree-format.md` §4/§8's own capsule table. For each capsule it
compares the start point (`+0x10`) against the end point (`+0x20`) component-wise (x, y, z); when
all three coincide (a degenerate, zero-length capsule) **and** the radius (`+0x30`) clears a
minimum-radius constant, it constructs a **sphere** collision primitive from the point and radius;
otherwise, for a genuine non-degenerate capsule clearing the same threshold, it constructs a
**capsule** primitive from both endpoints and the radius. The built primitives are then
batch-registered as a group. Every field this touches is a field `spec-tree-format.md` §8 already
lists as `HIGH CONFIDENCE`/`OPEN` on plausibility grounds alone (start point, end point, radius);
this pass upgrades that guess to `CONFIRMED — disassembly` and identifies the missing mechanism
(degenerate capsule → sphere, gated by a minimum radius) that §8's own worked example ("9 of 12
records the segment is exactly vertical... the three shrubs carry none") was already circling
without naming. Per this project's rule that corrections stay visible in place, this correction is
recorded here (the vertex-format spec, since it was surfaced while tracing the vertex question) and
should be folded into `spec-tree-format.md` §2 and §12 item 4 without deleting the superseded text.

**12.9.3 Codes 11 and 13 are excluded from the layout-code rewrite table every other shipped code
uses — CONFIRMED — disassembly.** `FUN_00e711c0` (§7's load-time rewrite routine, called once per
channel record from inside `FUN_00e71410`) dispatches on the channel record's raw layout-code byte
through a fixed set of cases: `0x00`–`0x04`, `0x0E`–`0x1A` (14–26, which includes zones' code 24),
and `0x64`–`0x65` (100–101, the vehicle codes) — every code this format has shipped or reserved,
**except** `0x0B` (11) and `0x0D` (13). Both fall through with the byte left unchanged, exactly like
an unrecognised code would. This is new, direct evidence — independent of anything found by
examining the vertex bytes themselves — that trees are handled by a mechanism separate from the
one every other carrier's layout codes participate in: whatever table the rewritten, flattened code
is meant to index (§7 infers "a table of prebuilt vertex declarations") **structurally cannot
contain entries for codes 11/13**, because they never reach it.

**12.9.4 A generic (non-tree-specific) vertex-declaration cache mechanism exists, but connecting it
to any specific layout code — 11/13 included — was not achieved in this pass; flagged rather than
chased, per this project's scope-jump rule (`HANDOFF.md` §5).** **The one piece of this that mattered
for a real question — which `D3DDECLTYPE` texcoord elements get — is now resolved a different way,
via real runtime declarations rather than tracing this cache's builder caller: §6.6 confirms
`D3DDECLTYPE_SHORT2` (raw, non-normalizing), from real captured D3D9 declarations in the separate
RTX-remix effort. The broader question below — which layout code maps to which cached declaration,
11/13 included — remains open.** A single sentinel byte pattern
resembling a Direct3D `D3DVERTEXELEMENT9` terminator, already located by an earlier pass at
`0x01351c5c` (referenced from `FUN_00476ca0`), leads to a genuine, working, engine-wide
vertex-declaration builder: `FUN_00476ca0` walks a caller-supplied list of element descriptors
(each validated against small range checks — an internal type code `< 0x34`, an internal usage
code `< 0x12`, a method byte `< 8`), translates the internal type/usage/method codes to Direct3D's
`D3DDECLTYPE`/`D3DDECLUSAGE`/`D3DDECLMETHOD` byte values through three small lookup tables in
nearby memory, appends a terminator record, and commits the result through what is, by its stack
offset, very likely the Direct3D device's `SetVertexDeclaration` slot — gated behind a
de-duplicating cache capped at `0x87` (135) distinct declarations. This is real, working
infrastructure for exactly the kind of table §7 already inferred must exist somewhere. But its only
located reference is a data/vtable slot (`0x0129fda4`), not an ordinary call site, so the function
that actually *builds* the descriptor list for any of this format's layout codes — 0–4, 24, 100/101,
or 11/13 — was not identified. This is exactly the generic, non-tree-specific infrastructure this
project's methodology notes warn against chasing once found by accident; it is recorded here as a
lead for whoever next has reason to resolve a layout code from first principles (not only 11/13),
and deliberately not pursued further in this pass.

**12.9.5 Normal and tangent ARE present in code 11/13, eight bytes earlier than the generic
layout's own +12/+16 — CONFIRMED — empirical, full population.** Having exhausted the disassembly
chain without finding a byte-level consumer (§12.9.1), the remaining avenue is exactly the one
§12.3 already used successfully for every other code: the same UBYTE4N unit-vector test §6.2 uses
to identify normal/tangent fields everywhere else in this format (three bytes mapped to [−1, 1] via
`(b/255)×2−1`, magnitude within 6% of 1.0), tested standalone at each of the eight 4-byte-aligned
offsets in the 32-byte base — not only the plain-float3 window §12.3 already tested and refuted.

| Offset | Code 11 unit-vector rate (17,624 vertices) | Code 13 unit-vector rate (260 vertices) |
|---|---|---|
| +0  | 3,336 / 17,624 (18.93%) | 56 / 260 (21.54%) |
| +4  | 2,549 / 17,624 (14.46%) | 32 / 260 (12.31%) |
| **+8**  | **17,624 / 17,624 (100.0000%)** | **260 / 260 (100.0000%)** |
| **+12** | **17,624 / 17,624 (100.0000%)** | **260 / 260 (100.0000%)** |
| +16 | 3,291 / 17,624 (18.67%) | 0 / 260 (0.00%) |
| +20 | 96 / 17,624 (0.54%) | 52 / 260 (20.00%) |
| +24 | 12,638 / 17,624 (71.71%) | 196 / 260 (75.38%) |
| +28 | 62 / 17,624 (0.35%) | 60 / 260 (23.08%) |

Two of eight offsets sit at an exact 100.0000% ceiling on both populations while the other six sit
well below it (0.35%–71.71%) — the test plainly has discriminating power here (six clean misses),
which is what makes the two clean hits meaningful rather than an artefact of a lenient predicate.
`+12`'s fourth byte (absolute offset `+15`) takes exactly two values across the code-11 population —
`0` in 15,332/17,624 (87.0%) and `255` in 2,292/17,624 (13.0%) — the same bimodal-extremes-only
signature §6.2 uses to tell the tangent (handedness sign) apart from the normal (unidentified
scalar) on every generic code; `+8`'s fourth byte (absolute `+11`) instead carries continuous
content (128 distinct values, range 0–127), matching the normal's own never-identified 4th byte
elsewhere in this format. (Code 13's matching 100.0000% figures at the identical two offsets, and
its own `+15` reading constant `0` / `+11` reading constant `112`, are recorded as corroborating —
the same structural slots, independently — but per the small-sample rule [`HANDOFF.md` §5] are not
treated as independently conclusive on their own 2-channel/260-vertex population.)

**CONFIRMED — empirical, full population (17,624/17,624), code 11: offset +8 is the normal and
offset +12 is the tangent.** This is the first field identification for either tree layout code.
It also explains, after the fact, why §12.3's float3 sweep could never have found position even if
position sits adjacent to this pair: a contiguous 3-float window spanning any of `+0`, `+4`, `+8`,
or `+12` necessarily includes at least one of these two confirmed non-float, byte-packed slots, so
that window fails regardless of whether its *other* members would individually have been valid
floats. A windowed multi-field test can be defeated by a single non-conforming member even at
offsets where the rest of the window is sound — worth carrying forward as a general caution
alongside this format's other windowed test (§6.1's float3 scan), and a concrete instance of the
"assumed search shape" family of lessons in `HANDOFF.md` §5.

**12.9.6 Position is still OPEN — every remaining offset was range-checked before being reported,
per this project's rule to check a candidate's achievable shape before running a population test on
it.** With `+8`/`+12` accounted for, the six remaining 4-byte-aligned base offsets (`+0`, `+4`,
`+16`, `+20`, `+24`, `+28`) were tested as **standalone** floats (one 4-byte window each, not a
3-float span, since any 3-float span touching `+8`/`+12` is disqualified by §12.9.5):

| Offset | Standalone-float-finite rate, code 11 (17,624 vertices) |
|---|---|
| +0  | 12,604 / 17,624 (71.52%) |
| +4  | **17,624 / 17,624 (100.0000%)** |
| +16 | 12,610 / 17,624 (71.55%) |
| +20 | **17,624 / 17,624 (100.0000%)** |
| +24 | 9,828 / 17,624 (55.76%) |
| +28 | **17,624 / 17,624 (100.0000%)** |

Three offsets (`+4`, `+20`, `+28`) are each individually finite in 100.0000% of vertices — but no
two of them are 4-byte-adjacent (the closest pair, `+4`/`+20`, is 16 bytes apart), so **no
contiguous float3 exists anywhere in the remaining 24 bytes of the base**: the shape a "plain
position" candidate would need to have does not fit the achievable layout, which is why no positive
positional claim is made here rather than reporting a partial or misleading offset. This is the
same range-check discipline `HANDOFF.md` §5 already names ("check a candidate formula's
achievable range... before testing whether it fits"), applied to adjacency/shape rather than
numeric range. **Position remains OPEN**, consistent with — not newly refuted beyond — §12.3's
existing conclusion.

**12.9.7 Offsets +0 and +16 (and, more weakly, +4 and +20) are exact byte-for-byte duplicates far
above chance, but only in code 11, and only partially — CONFIRMED — empirical, controlled,
full population; HYPOTHESIS — unconfirmed for what it means.** Testing raw 4-byte equality (no
type assumed) between offsets, with a shuffled-vertex control (offset *a* of vertex *k* against
offset *b* of a **different** vertex, cyclically shifted by one, so the control measures the chance
that two independently-drawn field values happen to coincide):

| Slot pair | Real (code 11, 17,624 vertices) | Shuffled-vertex control |
|---|---|---|
| +0 vs +16 | 12,279 / 17,624 (69.67%) | 345 / 17,624 (1.96%) |
| +4 vs +20 | 4,090 / 17,624 (23.21%) | 38 / 17,624 (0.22%) |
| +16 vs +20 | 12 / 17,624 (0.07%) | 2 / 17,624 (0.01%) |

`+0` vs `+16` sits at roughly 35× its own control; `+4` vs `+20` at roughly 105×; `+16` vs `+20`
(included as a same-order-of-magnitude sanity check) sits at chance level in both real and control,
as it should for two offsets with no claimed relationship. Neither of the two real signals is a
single-channel artefact: broken out per channel, `+0`/`+16` ranges from 21.7% (`st_shrub_sml`,
n=46) to 100% (one `st_shrub_nice_01` channel, n=115) — present, at a real rate, in **every one of
the 13 code-11 channels**; `+4`/`+20` ranges from 0.0% (three of the 13 channels) to 46.1%
(`st_pine_full`) — present in most but not all channels. **For code 13, by contrast, both relations
score exactly 0/260 (0.00%) in both of its two channels** — a clean, explicit point of divergence
between the two codes, worth recording precisely because §12.3 already cautions that a shared
measured base (32 bytes) is not evidence of a shared layout; this is a place where they now
provably differ, not merely where they have not yet been shown to agree.

Neither offset in either strongly-related pair is itself a clean, reliable float: `+0`/`+16` are
each finite in only ~71.5% of vertices (§12.9.6), well short of the ~100% this document treats as
the signature of real, always-meaningful field content everywhere else. **HYPOTHESIS —
unconfirmed:** `spec-tree-format.md` §1 already establishes, independently of this investigation,
that the `.csrt_pc` c-file "is a memory image" carrying "stale heap pointers... re-written at
load" for other fields entirely (the LOD presence markers, §3 of that document). A partial,
non-universal byte-duplication paired with a partial (not 100%) finite-float rate at the same two
offsets is *consistent with* `+0`/`+16` (and possibly `+4`/`+20`) being runtime-populated or
partially-uninitialised-on-disk content rather than reliably authored per-vertex data — but this
pass did not locate a disassembly site that writes them, so this is not confirmed, and a
coincidental-content explanation (two genuinely different, independently-varying per-vertex
quantities that simply agree at low-displacement/anchor vertices) has not been ruled out either.
Recorded as OPEN with the concrete next step being to find whichever load-time or runtime function
(if any) writes into this specific channel's data region after the file parse — the same kind of
trace that resolved `+8`/`+12`.

**12.9.8 Offset +24 shows the same real-but-channel-varying signature §12.3 already reports for
code 24's own +16 slot — OPEN.** Pooled across all code-11 channels, `+24` passes the unit-vector
test in 12,638/17,624 (71.71%) — but per channel this ranges from 0/62 (0.0%, `st_shrub_med`) to
2,202/2,202 (100.0%, one `st_shrub_nice_01` channel), with every intermediate value in between
(48.0%–98.5%) at the other 11 channels. This is the same qualitative shape §12.3 already documents
for code 24 (there: 1 tangent-like / 62 binary-but-not-unit / 385 neither, out of 448 channels) —
real, nonrandom, per-channel-varying structure, not a single chance rate, and not yet characterised
further. Given the task's own framing of a plausible wind-related field, a second orientation-like
vector (e.g. a bend axis, distinct from the confirmed normal/tangent pair) is a candidate consistent
with the data, but nothing here distinguishes it from other explanations, so it is left at
`OPEN / UNKNOWN` rather than promoted.

**12.9.9 Two clean strong negatives, plus one near-constant byte, full population, code 11.**
Offset `+20`'s third and fourth bytes (absolute `+22`, `+23`) read `0` in 17,624/17,624
(100.0000%) vertices; offset `+28`'s third and fourth bytes (absolute `+30`, `+31`) likewise read
`0` in 17,624/17,624 (100.0000%). **[CONFIRMED — empirical]**, in the same style as the
channel-record-`+0x08` and component-group-B negatives already closed in §12.1/§12.2. Offset
`+28`'s first byte (absolute `+28`) takes only three values, tightly clustered around the byte that
encodes zero under the `(b/255)×2−1` mapping used for normal/tangent components elsewhere: `127` in
14,836/17,624 (84.2%), `128` in 949/17,624 (5.4%), `129` in 1,839/17,624 (10.4%) — effectively
constant-at-zero. Its second byte (absolute `+29`) is not constant (83 distinct values) but is
tightly clustered in the upper half of the byte range (165–255, i.e. always on the positive side of
the same mapping) rather than spanning the full 0–255 a continuous field normally shows elsewhere in
this document. **OPEN** for what, if anything, this near-constant slot and its one variable byte
encode; flagged rather than guessed at, since nothing here range-checks against a specific
candidate the way §12.9.5/§12.9.6 do.

**12.9.10 Texcoord slot sanity check.** The declared texcoord set (offset `+32`, the 4 bytes beyond
the 32-byte base, per the `stride = base + 4×texcoord_count` law of §5) decodes, read as two signed
16-bit fixed-point lanes at scale 1024 exactly as §6.5 already establishes generically, to a wider
range (roughly −17.3 to +3.7 in one lane) than this document's own population median — consistent
with, not contradicting, ordinary tiled bark-texture UVs repeating many times along a trunk, and not
pursued further since §6.5 already covers the encoding; recorded here only because it was checked
in passing while locating the base fields above.

**12.9.11 Summary of this subsection, and the concrete next steps.** For code 11 (full population)
and, with the small-sample caveat, code 13: the normal (`+8`) and tangent (`+12`, sign in its 4th
byte) are now `CONFIRMED — empirical`, located by the same test this document already trusts
elsewhere, after the tree-specific disassembly chain (§12.9.1–§12.9.4) established that no CPU-side
consumer exists to read the fields directly and that codes 11/13 are structurally excluded from the
generic layout-code rewrite/declaration-slot mechanism (§12.9.3) — independent, disassembly-level
support for treating trees as a genuinely separate mechanism rather than a shifted version of the
generic one. Position remains `OPEN / UNKNOWN`: no contiguous float3 exists anywhere in the
32-byte base outside the confirmed normal/tangent pair (§12.9.6), three individual offsets are each
cleanly finite on their own (`+4`, `+20`, `+28`) without being adjacent to one another, and two
offsets (`+0`, `+16`) show a real but partial structural relationship to each other that is more
consistent with runtime-populated or partially-stale content than with reliably authored position
data, though that is `HYPOTHESIS` rather than confirmed. Offset `+24` repeats code 24's own
channel-varying, not-yet-characterised signature. Concrete next steps, in priority order: (1) find
whichever function (if any) writes `+0`/`+4`/`+16`/`+20` at load or first-render time, which would
settle §12.9.7 either way; (2) find the tree/billboard-specific vertex-declaration or draw-setup
function that must exist given §12.9.3's exclusion — a promising entry point is whatever calls the
generic declaration-cache builder identified in §12.9.4 for a tree material, rather than a
whole-binary search for literal comparisons against `11`/`13`, which this project's own methodology
notes (`HANDOFF.md` §5) already show has no selectivity at binary scale; (3) characterise which
channels carry a high `+24` unit-vector rate and which do not, exactly as `vtx_layout_detail.py`
already did for code 24. Harness: `tools/harnesses/tree2_offset_relations.py` (new, `tree2_`-prefixed
per this pass's naming convention); Ghidra scripts: `tools/scripts/Tree2VertexDeclTable.java`,
`tools/scripts/Tree2LayoutRewriteDecompile.java`.

### 12.9.12 Per-channel `+24` unit-vector rate, item 3 of Sec 12.9.11's next steps -- real per-instance variation, mechanism still not characterised (2026-09-14)

Full per-channel breakdown of the pooled 71.71% figure (Sec 12.9.8), computed directly from the
same population as the rest of Sec 12.9 (`sr3_city_0.vpp_pc`, `dlc2.vpp_pc`, `dlc3.vpp_pc`), via
`tree2_offset_relations.py`'s existing `per_channel_unit_rate()` (already written for this exact
purpose but not previously tabulated in full):

| Channel | n | +24 unit-vector hits | Rate |
|---|---|---|---|
| st_shrub_med | 62 | 0 | 0.0000 |
| st_pine_full | 1,686 | 810 | 0.4804 |
| st_pine_tall | 1,710 | 822 | 0.4807 |
| st_pine_xmas (a) | 1,243 | 665 | 0.5350 |
| st_com_break | 819 | 516 | 0.6300 |
| st_shrub_nice_01 (a) | 115 | 75 | 0.6522 |
| st_com_lrg | 4,616 | 3,306 | 0.7162 |
| st_com_sml | 1,262 | 906 | 0.7179 |
| st_com_med | 1,650 | 1,203 | 0.7291 |
| st_shrub_lrg | 627 | 530 | 0.8453 |
| st_shrub_sml | 46 | 41 | 0.8913 |
| st_pine_xmas (b) | 1,586 | 1,562 | 0.9849 |
| st_shrub_nice_01 (b) | 2,202 | 2,202 | 1.0000 |

**New fact, CONFIRMED -- empirical: two of these thirteen channel names (`st_pine_xmas`,
`st_shrub_nice_01`) each occur twice -- not a naming collision, but two genuinely distinct Mesh
sub-blocks at different byte offsets within the same c/g file pair** (`st_pine_xmas`: g-file
offsets 4,432 and 53,952; `st_shrub_nice_01`: offsets 464 and 13,456 -- gaps far too large to be
the same block misread twice), most plausibly a near/far LOD pair sharing one asset name. **This is
evidence against a simple per-species or per-asset-name explanation for the rate**: the two
`st_pine_xmas` instances score 0.5350 and 0.9849 -- a ~45-point spread for nominally the same tree
-- and the two `st_shrub_nice_01` instances score 0.6522 and 1.0000, an even wider spread. Whatever
drives the rate varies at the level of the individual channel/mesh instance (LOD level, vertex
composition, or similar), not at the level of "which tree species this is."

**Still OPEN, not chased further this pass, per `HANDOFF.md` Sec 27.8 (defer rather than invent):**
no disassembly trace was attempted to identify what `+24` actually encodes or why the rate should
track LOD/instance-level composition rather than species. Sec 12.9.8's "plausible wind-related
field" framing remains exactly as speculative as it was there -- this pass adds a real, controlled
structural fact (the rate is per-mesh-instance, not per-species) without resolving the underlying
mechanism. Sec 12.9.11's item 3 is complete as scoped (a full per-channel characterization now
exists, matching `vtx_layout_detail.py`'s code-24 precedent); items 1 and 2 remain independently in
flight (see `HANDOFF.md` Sec 27.2).

### 12.9.13 Item 1 of Sec 12.9.11's next steps -- no CPU-side writer of +0/+4/+16/+20 exists anywhere in the traced load chain, CONFIRMED clean negative (2026-09-14)

Per Sec 12.9.7/12.9.11's own framing, the concrete next step was to find "whichever load-time or
runtime function (if any) writes into this specific channel's data region after the file parse."
The named entry point was `FUN_00a6f500` -- the tree constructor's second post-file-parse call, a
sibling of the already-traced `FUN_00a713e0` (Sec 12.9.2, confirmed to be the collision-capsule
builder, unrelated). `FUN_00a6f500` had not previously been examined for this specific question.
It, everything it calls, and the two Mesh sub-block walkers (`FUN_00e71410`/`FUN_00e71740`,
already read for a different purpose in Sec 12.9.1) were all re-decompiled in full and their raw
instruction streams scanned for store-class operations, specifically looking for any write whose
target address is the vertex data pointer plus a small displacement of `0`, `4`, `16`, or `20`.

**The tree constructor's last two calls, confirmed from raw disassembly (`FUN_00a71810`,
`0x00a718a2`/`0x00a718a8`):** `CALL 0x00a6f500` immediately followed by `CALL 0x00a713e0`, both
taking the tree object as their explicit stack argument -- exactly the order the task
brief names, with nothing else interposed after the file parse completes.

**`FUN_00a6f500` is a global doubly-linked-list insertion, not a vertex-buffer touch --
CONFIRMED, disassembly.** The instruction immediately preceding the call (`0x00a7189d`) is
`MOV ECX,0x2706800` -- a fixed global address, not anything derived from the tree object or its
geometry sub-structure -- confirming the function's "this" argument is a
module-level singleton (a global tree-registry list head), while the tree object itself is only
the second, explicit argument. The function body writes exactly four fields, all on
the **tree object's own** `+0x28`/`+0x2c`/`+0x30`/`+0x34` (prev/next list-link slots inside the
`'TREE'` block, matching this function's pre-existing project annotation), then calls two more
functions:

- `FUN_00a6f920(head+0xc)` -- one conditional dword write of its second argument into the first
  argument's `+0xA8` slot (only when the two differ), into the **global list head's own** bookkeeping area (`head+0xc+0xa8`,
  i.e. a "last-registered" tracking slot on the manager singleton, not on the tree object).
- `FUN_00a6f950(*(head+8))` -- an exponential-smoothing/decay update (the difference between two
  of the six values below, blended back in via an interpolation weight) across six `float` fields
  at byte offsets `+0x98`/`+0xac`/`+0xd0`/
  `+0xd4`/`+0xd8`/`+0xdc` of **whatever object the global head's own `+8` slot points to** -- a
  shape (six scattered floats past offset `0x98`) that matches neither a 32-byte vertex record nor
  anything addressed relative to a per-vertex base.

Neither `FUN_00a6f500` nor either function it calls ever dereferences the tree's geometry
sub-structure, its Mesh sub-block pointers, or a channel's data pointer at any point -- all three
functions' writes land exclusively on the tree object's own list-link fields or on a *different*,
unrelated global manager object's own bookkeeping fields. **This closes `FUN_00a6f500` as a
vertex-buffer writer: it is a registration/bookkeeping routine operating entirely outside the
geometry data, full stop.**

**`FUN_00e71410`/`FUN_00e71740` write only their own header struct and the vertex data *pointer*,
never the bytes it points to -- CONFIRMED, disassembly, extending Sec 12.9.1's read-only finding
to writes.** Both functions were re-decompiled in full for this pass (Sec 12.9.1 read them only
for branches/reads). Every store either of them performs targets one of: (a) the channel record's
own fixed-stride header fields -- zeroing the four padding/high-dword bytes immediately following
a pointer field the walker just fixed up (byte ranges `+0x1C..+0x1F`, `+0x2C..+0x2F`,
`+0x40..+0x47`, `+0x50..+0x57`, and the dword at `+0x14`) *[note: the offsets past `+0x17` exceed the 24-byte channel record (§3), so those ranges are Mesh-header pointer slots (header `+0x18`, `+0x28`, `+0x40`, `+0x50`; `spec-geometry-format.md` §4.1.1), not channel-record fields; only `+0x10`/`+0x14` are channel-record fields]*, or (b) the data-pointer field itself
being *assigned* the freshly-computed address (the dword at `+0x10`, set to the cursor plus
base). Neither function
contains a single instruction of the form `[data_ptr + 0]`, `[+4]`, `[+16]`, or `[+20]` (or any
other offset in a vertex record's 0-31 byte range) as a **store destination** -- they write the
pointer, never through it.

**Verdict: CONFIRMED -- disassembly, clean negative, in the same style as
`spec-rig-format.md` Sec 11.14's closed "no per-frame writer of record+0x00."** Combined with the
two pieces already established elsewhere in Sec 12.9 -- `FUN_00a713e0` is the collision-capsule
builder and touches no Mesh sub-block data at all (Sec 12.9.2), and `FUN_00e711c0`, the one
routine in this whole chain that *does* rewrite per-channel bytes for other layout codes, falls
through unchanged for codes 11/13 specifically and so never executes its rewrite body for trees at
all (Sec 12.9.3) -- this pass now covers every function named in the task's own call chain
(`FUN_00a71810` -> `FUN_00a6f780` -> `FUN_00e7a5a0` -> material-set/index-list/Mesh-sub-block
wrapper -> `FUN_00e71410`/`FUN_00e71740` -> the two post-parse calls) with **zero writers into the
tree vertex buffer's own byte content found at any offset**, `+0`/`+4`/`+16`/`+20` included.

**What this settles, and what it does not.** It directly refutes the specific mechanism Sec 12.9.7
drew by analogy from `spec-tree-format.md` Sec 1/Sec 3 -- that a load-time fixup pass, of the same
general kind already confirmed to re-write *other* fields (the LOD presence markers) from a stale
memory-image state, might also be what populates `+0`/`+4`/`+16`/`+20`. The load-time pass that
performs exactly that class of pointer-fixup rewrite for this data (`FUN_00e71410`/`FUN_00e71740`)
is now confirmed, from its actual instructions, to never touch those four offsets at all. **It does
not** positively settle what `+0`/`+4`/`+16`/`+20` are: per Sec 12.9.1, this format's per-vertex
field content is consumed GPU-side via a vertex declaration, never read back by the CPU, so a
fixup happening in a vertex/geometry shader or in a first-render-time GPU upload/lock routine this
pass did not chase (that is the separately-dispatched vertex-declaration-builder lead, Sec
12.9.11 item 2, `HANDOFF.md` Sec 27.2) would be invisible to this trace. **Scope is explicit: "no
writer reachable from the traced CPU load chain," not "no writer anywhere in the engine."** With
the loader-rewrite analogy now ruled out specifically, the coincidental-content explanation Sec
12.9.7 also named (two genuinely different, independently-varying per-vertex quantities that
happen to agree at low-displacement/anchor vertices) and a genuinely pre-existing-on-disk
authored-but-unidentified content explanation are left standing exactly as they were --
`+0`/`+4`/`+16`/`+20` remain `OPEN / UNKNOWN`, narrower than before but not resolved.

Ghidra addresses for reference: `FUN_00a6f500` (`0x00a6f500`), `FUN_00a6f920` (`0x00a6f920`,
writes global-manager `+0xa8`), `FUN_00a6f950` (`0x00a6f950`, smoothing update on a second global
object's `+0x98..+0xdc`), the global tree-registry list head (`DAT_02706800`), call site
`0x00a718a2` (`MOV ECX,0x2706800` at `0x00a7189d` immediately before). Ghidra script:
`tools/scripts/Tree4WriterTrace.java` (full decompile + call-target list + raw-instruction store
scan for `FUN_00a6f500`/`FUN_00e71410`/`FUN_00e71740`), `tools/scripts/Tree4WriterTrace2.java`
(`FUN_00a6f920`/`FUN_00a6f950`/`FUN_00a71810` full decompile), `tools/scripts/Tree4CallSiteAsm.java`
(raw disassembly of the `FUN_00a71810` call sequence).

### 12.9.14 The vertex-declaration-builder's own caller (Sec 12.9.11 item 2) — traced two levels deeper than Sec 12.9.4; both named entry points reach genuine, disassembly-confirmed dead ends, position still OPEN (2026-09-14)

*(Position superseded by §12.12: FLOAT16×3 at `+0`, HIGH CONFIDENCE. The negatives below remain valid for what they tested.)*

This subsection follows up Sec 12.9.11's item 2 directly: find the function that builds the D3D
vertex-element descriptor list for tree layout codes 11/13, using the two entry points named in
the task brief (the vtable slot `0x0129fda4` referencing `FUN_00476ca0`, and the vehicle-geometry
GPU-upload chain). Both were pushed further than any prior pass; neither reached a caller that
supplies a tree-specific element list.

**12.9.14.1 Entry point 1: the real vtable base is `0x0129fd90`, found by byte-pattern search after `getReferencesTo` came back empty — CONFIRMED, disassembly.** Re-running `getReferencesTo(0x0129fda4)` (the slot Sec 12.9.4 named) confirms it directly: 0 references, exactly matching "not an ordinary call site." The neighbourhood (`0x0129fd8c`-`0x0129fde4`) turns out to hold a small, genuine C++-style vtable, not raw scattered data — established by searching memory for the literal little-endian encoding of candidate base addresses, which `getReferencesTo` had also returned 0 hits for on the table-base addresses themselves:

| Search target | Hits | Context |
|---|---|---|
| `0x0129fd8c` (one slot before the first function pointer) | 0 | — |
| `0x0129fd90` (the first function-pointer slot) | 2 | `00476bef` in `FUN_00476bd0`: `MOV dword ptr [ESI],0x129fd90`; `00476c48` in `FUN_00476c40`: `MOV dword ptr [EDI],0x129fd90` |

Both hits are the classic MSVC ctor/dtor "(re)set vptr to this class's own vtable" idiom — confirming `0x0129fd90` is the real vtable base (not `0x0129fd8c`, which fits the standard MSVC layout slot for an RTTI locator pointer at `vtbl-4`, consistent with, not independently proven as, RTTI). `FUN_00476bd0` is therefore this class's constructor and `FUN_00476c40` its destructor body. Decompiling the full 8-slot primary vtable (`0x0129fd90`-`0x0129fdac`) resolves every member:

| Offset | Function | Role, read directly from the decompiled body |
|---|---|---|
| +0x00 | `FUN_00476c20` | deleting destructor (calls `FUN_00476c40`, then conditionally frees) |
| +0x04 | `FUN_008a5bf0` | shared no-op stub (`{ return; }`) — 400+ unrelated vtables engine-wide reuse this same address; not format-specific |
| +0x08 | `FUN_00476bb0` | calls the object's own destructor slot with flag 0, then passes the object to `FUN_00e49600` — a `Release()`-shaped method: invokes the destructor, then frees via the pool-free counterpart of the ctor's own `FUN_00e49580` allocator |
| +0x0C | `FUN_00435660` | shared stub, `{ return 0xffffffff; }` — ~83 unrelated call sites engine-wide reuse this address as a default "unimplemented" virtual |
| +0x10 | `FUN_00476c90` | zeroes the dword at `this+0x1C0`, fills `0x1B8` bytes from `this+0x08` with `0xFF`, then calls `FUN_00e79570` — resets a cache: `this+0x1c0` is exactly the live-count field `FUN_00476ca0` checks against the `0x87` cap Sec 12.9.4 already found, so this is that cache's own reset/init method |
| +0x14 | **`FUN_00476ca0`** | **the `CreateVertexDeclaration` builder itself — Sec 12.9.4's original target** |
| +0x18 | `FUN_00476ea0` | thin wrapper that calls slot `+0x14` on the **same object** via indirect dispatch, passes that call's result together with its own argument 2 to `FUN_00e7bfa0`, and returns the same result — i.e. this is the "public" entry point whose caller this task needs, and it internally re-enters the builder rather than being a separate implementation |
| +0x1C | `FUN_00476ed0` | a flag/bitmask test against a per-cache-entry array at `this+0x3e0`/`this+0x3e4` — the same array `FUN_00476ca0` populates on a successful build |

A second, secondary-base vtable begins at `0x0129fdb4` (the constructed object's own `+0x4` field is set to `&PTR_FUN_0129fdb4` alongside `+0x0` = `&PTR_FUN_0129fd90`, i.e. multiple inheritance); its slot 0 (`FUN_00476f60`) just forwards to the same `FUN_00476c20` deleting destructor and was not pursued further, since the target method (`+0x14` on the primary vtable) is already resolved.

**12.9.14.2 `FUN_00476ca0`'s parameter shape, confirmed directly by a fresh full decompile — refines, does not contradict, Sec 12.9.4.** It is a `thiscall` method taking one pointer argument (argument 2) to a `{elements pointer, count, ...}` header (first dword = element-record array base, second dword = count), record stride `0x10` bytes (type at `+0`, usage at `+4`, a method byte at `+8`, a further byte at `+9`, a field at `+12`). Each record is validated exactly as Sec 12.9.4 already reported (type ≠ -1 and < 0x34; usage ≠ -1 and < 0x12; method byte < 8; the `+12` field < 3), translated through the same three lookup tables (`DAT_01351d58`, `DAT_01351c98`, `PTR_DAT_01351c64`) into a `D3DVERTEXELEMENT9`-shaped record, terminated with the `DAT_01351c5c`/`DAT_01351c60` sentinel pair Sec 12.9.4 located, and committed via a **thread-local-storage-derived** device object's vtable `+0x158` (`FS:[0x2c]` → a pointer at `+0x674` → a second pointer at `+0x764` → dereferenced once more → `vtable[+0x158]`) — not a fixed global, which is new detail. On success, a 64-bit value from `FUN_00e7bfc0()` is stored into `this+0x3e0+cacheIndex*8`/`this+0x3e4+cacheIndex*8`, the live count at `+0x1c0` is incremented, and the new cache index is returned. **No table indexed by anything layout-code-shaped exists anywhere in this function's body.** It is a pure "build exactly what you hand me" declaration factory; whichever element records (and therefore which offset carries `D3DDECLUSAGE_POSITION`) go into argument 2 is decided entirely by the caller, before this function is ever reached.

**12.9.14.3 The class is constructed exactly once, engine-wide, and its pointer is provably discarded at the only call site — CONFIRMED, raw disassembly, a genuine dead end.** `FUN_00476bd0` (the constructor: allocates `0x818` bytes via `FUN_00e49580`, installs both vtable pointers, returns the new object) has exactly one caller in the entire binary: `FUN_00e49250`, at call site `0x00e492c1`. `FUN_00e49250` is the engine's top-level Render Layer bring-up routine, identified unambiguously by four debug strings it passes to a logging callback in sequence: `"RL: factory"`, `"RL: shader library"`, `"RL: other systems"`, `"RL: gpu visibility"` — the vertex-declaration-class constructor call sits in the "RL: factory" section, called the same bare-statement way as several sibling one-shot subsystem initializers around it. **Checked in raw disassembly, not only the decompiler's C view** (which could plausibly elide an assignment to an as-yet-unnamed global): the instruction immediately after `CALL 0x00476bd0` at `0x00e492c1` is `MOV EAX,[0x0132db98]` — reading an unrelated global — with no intervening store of `EAX` anywhere before it is overwritten. **The constructed object's pointer is not captured into any register, stack slot, or global reachable from this call site.** Combined with 12.9.14.1's finding that none of this class's 8 primary-vtable methods (destructor aside) has a single caller anywhere else in the binary besides their own vtable-slot self-reference (every one of `FUN_00476bb0`/`FUN_00435660`/`FUN_00476c90`/`FUN_00476ca0`/`FUN_00476ea0`/`FUN_00476ed0` shows exactly 1 total reference — the data slot that stores it into the vtable, nothing else), this is as far as static cross-reference analysis can go: **there is no statically-visible path from "an instance of this class exists" to "some caller supplies it a tree-shaped element list."** Two explanations are left open, neither confirmed: the instance is retained through a mechanism this pass did not locate (e.g. participation in a pool/registry maintained by the allocator itself, not chased further here), or this specific class — despite matching Sec 12.9.4's description of the `CreateVertexDeclaration`-calling infrastructure exactly — is not actually the path trees' declarations go through at all.

**12.9.14.4 Entry point 2: the vehicle GPU-upload chain's one non-vehicle caller is the unrelated Effects format, not trees — CONFIRMED, cross-checked against `spec-effects-format.md`.** `FUN_00482e00` (~~the vehicle-class constructor~~ the pooled renderer-object constructor — corrected per `spec-vehicle-geometry.md` §11.9.2, which names the vehicle constructor as `FUN_00ab1080`; installs vtable `PTR_FUN_012a0418`, per `spec-vehicle-geometry.md` Sec 11.9) has exactly 3 callers engine-wide: `FUN_00489260`, `FUN_00482d20` (the class pool itself), and `FUN_0043e420` — the task brief's suggested lead, checked here. `FUN_0043e420` opens with a version gate that rejects the call unless the dword at its second argument's `+4` field falls in the range 42-44, and is reached — via its sole caller `FUN_0043e1c0` — only after its first argument's leading dword reads `0x57423137`. That is the exact `71BW` marker and 42-44 (43 or 44 observed) version range `spec-effects-format.md` already documents, independently, as its own format's fixed identifying marker. **`FUN_0043e420` is the Effects/particle geometry loader, not trees.** It does call the same shared helpers trees use (`FUN_00e71090`, the tree/foliage/vehicle/effects material parser; `FUN_00e718b0`, the Mesh sub-block wrapper) and the same vehicle-shaped renderer-object class (`FUN_00482e00`/`FUN_00482f00`) — useful corroboration that this renderer-object class is a genuinely generic "renderable sub-mesh" reused by at least three unrelated formats — but it settles nothing about trees specifically. `FUN_00482f00` (the GPU-upload committer this class uses) was re-decompiled in full this pass: it contains only vertex/index buffer creation (`DAT_03171ac4` vtable `+0x68`/`+0x6c`) and a raw `memcpy` of file bytes into the locked buffer — **no declaration-building call and no layout-code branch of any kind**, for either format that reaches it. This is consistent with, not new evidence against, Sec 12.9.1's finding that declaration binding never happens inline in any load-time GPU-upload path traced so far — tree, vehicle, or effects alike.

**12.9.14.5 Verdict — position remains OPEN; this pass narrows the gap without closing it.** Stacked against Sec 12.9.13 (no CPU-side writer of `+0`/`+4`/`+16`/`+20` anywhere in the traced load chain) and Sec 12.9.1/12.9.3 (no CPU-side field reader anywhere; codes 11/13 excluded from the generic layout-code rewrite table), this pass adds a third independent negative result: the one concretely-identified, engine-wide vertex-declaration-cache/builder class (vtable `0x0129fd90`, `FUN_00476ca0` at `+0x14`) is real, internally consistent with Sec 12.9.4's original summary, and now read start-to-finish in full — but is **statically unreachable** from any caller that could be shown to supply a tree-specific element list, because its one instance's pointer is discarded at its one construction site and none of its methods have a literal caller anywhere in the ~6 MB binary. Per `HANDOFF.md` Sec 27.8, this is recorded as the honest gap rather than a guess: **no disassembly-side answer for which of `+0`/`+4`/`+16`/`+20`/`+24`/`+28` carries `D3DDECLUSAGE_POSITION` was found in this pass.** Both of the task's named entry points were followed to a genuine, verified dead end (not merely to the same wall Sec 12.9.4 already hit — this pass went one level further on each: found the real vtable base and every one of its methods, confirmed the sole instance's pointer loss in raw disassembly, and positively identified entry point 2's one live lead as a different, already-documented format instead of trees). The natural next step, if this is picked up again, is a method class this project has not yet tried on this exact question: a **runtime/dynamic trace** (e.g. a breakpoint on `FUN_00476ca0` or on `CreateVertexDeclaration` itself while a tree is on screen, dumping the live element array passed as argument 2) — static cross-reference analysis has now been run against this specific target twice, by two independent passes, with the same negative result both times.

Ghidra scripts, this pass (`tools/scripts/`): `Tree2VtxDeclSource.java`, `Tree2VtxDeclSource2.java`, `Tree2VtxDeclSource3.java`, `Tree2VtableByteSearch.java`, `Tree2VtableCtor.java`, `Tree2VtableOwner.java`, `Tree2FindStore.java`, `Tree2LastCheck.java`, `Tree2Decl476ca0.java`. Ghidra project copy: `tools/gp_treeA` (robocopied from `tools/ghidra_projects`; safe to delete once consolidated).

### 12.10 Codes 11/12/13/24 — code 12 fully field-probed for the first time, the "master declaration table" angle closed for all four codes, and a peer-flagged second `CreateVertexDeclaration` lead REFUTED (2026-09-29)

Prompted by a peer clean-room team's blocking need: their tree/zone renderer's shared
MeshBlock reader cannot decode layout codes 11, 12, 13 or 24 without real element-field
offsets, and a real population sweep of every shipped tree's every LOD slot found code
12 genuinely ships (`spec-vertex-format.md` §10 item 2's correction, same day) — a code
this document had never field-probed at all, unlike 11/13/24. This subsection (a)
field-probes code 12 for the first time, by the same byte-level battery §12.3/§12.9
already used for the other three; (b) directly re-checks, from the real disassembly
rather than by inference, whether code 12 participates in the §7 load-time rewrite the
same way 11/13 were already shown not to; and (c) runs down the one concrete lead this
document's own §12.9.14 had left dangling — a second, unexplored pair of
`CreateVertexDeclaration`-shaped call sites `spec-render-pipeline.md` §8 flagged as
"OPEN, plausible... not confirmed" — to see whether it is the missing tree/zone
declaration path. Two of three sub-questions close with real answers; the third closes
as a clean negative that also corrects another document.

#### 12.10.1 Code 12's population, size and stride — CONFIRMED, three independent measurements agreeing

A fresh full-population sweep of every `.csrt_pc`/`.gsrt_pc` pair in all 38 shipped
archives (`tools/harnesses/mesh_scan.py`'s scan pattern reused, corrected to accept a
4-byte-aligned g-side segment start rather than that module's stock 16-byte grid — the
same undercount
`spec-vertex-format.md` §4.2's own warning box already documents for trees/zones,
relevant here because code 12 ships at LOD slot 1, not slot 0, so its segment is a
"later" one in the chained `.gsrt_pc` file, exactly the case that grid misses) finds
**code 12 in exactly 4 channels, 3 of them distinct trees**: `st_pine_tall` (153
vertices), `st_shrub_med` (144 vertices), and `st_shrub_sml` (156 vertices, shipped as
two byte-identical copies — one in `dlc2.vpp_pc`, one in `sr3_city_0.vpp_pc` — matching
`spec-tree-format.md` §2's already-documented "every duplicate copy is byte-identical"
population fact). This matches a peer team's own independent population sweep exactly
(same 3 trees, same LOD-1 slot).

Every one of the 4 channels reads **element size 36 bytes**, taken directly from the
channel record's own on-disk `sizeA`/`sizeB` fields (`sizeA=36`, `sizeB=0` in 4/4) —
**CONFIRMED — empirical, exact, full population, and independently cross-checked
against a peer team's own measurement of the same 36-byte figure by the same
direct-read method.** A second, independent derivation agrees: all 4 channels carry
texcoord-count `1` (record `+0x06`), and `spec-vertex-format.md` §5's stride law
(`stride = base(layout_code) + 4 × texcoord_count`) then implies **base(12) = 32
bytes** — numerically identical to the already-measured bases of codes 11 and 13
(§12.3), which is the natural reading given all three codes are tree-exclusive and (per
§12.10.2 below) share the same exclusion from the generic rewrite table. **Three
independent numbers (direct on-disk read, this pass's own stride-law back-derivation,
and a peer team's own measurement) agree on 36 bytes with zero discrepancy to
reconcile.** This closes the size half of `spec-vertex-format.md` §10 item 1 for code
12, the one code among the four this document had never even structurally sized before.

#### 12.10.2 Code 12's element fields — normal at `+8`, tangent at `+12`, identical convention to codes 11/13; position OPEN, same shape as 11 — CONFIRMED / OPEN, empirical, full population

*(Position superseded by §12.12: FLOAT16×3 at `+0`, HIGH CONFIDENCE. The FLOAT32 negatives below remain valid for FLOAT32 only.)*

Running the same UBYTE4N-unit-vector-plus-sign-byte battery `spec-vertex-format.md`
§12.9.5 already used to locate codes 11/13's normal/tangent pair, at every 4-byte
offset across code 12's full 36-byte stride, over all 609 vertices (453 once the
duplicate `st_shrub_sml` copy is not double-counted; the rate is identical either way,
since duplicating a 100%-or-0%-scoring population cannot change its own rate):

| Offset | Low-3-byte unit-vector rate | 4th byte behaviour | Reading |
|---|---|---|---|
| `+0`, `+4` | 0% standalone-float-finite, low unit-vector rate | — | not FLOAT32 position, not normal/tangent *(FLOAT16×3 position at `+0` found later, §12.12)* |
| **`+8`** | **609 / 609 (100.0000%)** | continuous (never only 0/255) | **normal** |
| **`+12`** | **609 / 609 (100.0000%)** | **binary, exactly {0, 255}, 609/609 (100.0000%)** | **tangent + handedness sign** |
| `+16` | 0% unit-vector | standalone-float-finite in 312/609 (51.2%) — exactly the two `st_shrub_sml` copies, 0% in the other two channels | per-channel-varying, not a clean field (see below) |
| `+20`, `+24` | low | standalone-float-finite in 153/609 (25.1%) — exactly `st_pine_tall` alone, 0% elsewhere | per-channel-varying, not a clean field |
| `+28` | low | — | no signature |
| `+32` (the declared texcoord slot) | — | reads as a signed 16-bit fixed-point pair at scale 1024 exactly as §6.5/§12.9.10 already establish generically | texcoord set 0 |

**`+8` = normal, `+12` = tangent (4th byte = handedness sign) is now CONFIRMED —
empirical, full population (609/609 both offsets), identical byte offsets and identical
signature shape to codes 11 and 13** (`spec-vertex-format.md` §12.9.5) — the same
UBYTE4N unit-vector encoding, the same "continuous 4th byte at the normal, binary
0-or-255 4th byte at the tangent" distinction this document already uses everywhere
else to tell the two apart. This is the strongest possible outcome for a 4-code
"family" question: three separate layout codes, all tree-exclusive, all sharing the
same measured base size (32 bytes) and now shown to share the exact same normal/tangent
byte offsets too.

**Position is OPEN for code 12, in the same partial, per-channel-varying shape already
on record for code 11 (§12.9.6/§12.9.7), not a clean negative and not a confirmation.**
No offset reaches the ~100% standalone-float-finite rate this document treats as the
signature of real, always-present position data anywhere else in this format (§6.1).
The two partial signals that do exist (`+16` passing only for the `st_shrub_sml`
duplicate pair, `+20`/`+24` passing only for `st_pine_tall`) are each **isolated to a
single tree**, which is a weaker and more fragmented shape than even code 11's own
`+0`/`+16` partial signal (present, at a real rate, in *every* code-11 channel per
§12.9.7) — consistent with, not contradicting, this document's existing HYPOTHESIS that
such partial signals are more likely stale/runtime-populated content than authored
per-vertex position data. **Per this project's small-sample rule, three distinct trees
is not enough to characterise a per-tree-varying signal further, so this is recorded as
OPEN rather than pushed toward either explanation.** [**CONFIRMED — empirical** for the
normal/tangent offsets and the negative result on the rest; **OPEN** for position,
unchanged in kind from 11/13's own open position question.]

#### 12.10.3 Code 12 is excluded from the load-time rewrite table exactly like 11 and 13 — CONFIRMED, disassembly, directly (not inferred)

`spec-vertex-format.md` §12.9.3 already established, by reading `FUN_00e711c0` (the §7
rewrite routine), that codes 11 and 13 fall through unchanged because the routine's
accepted case set is `0–4, 14–26, 100–101` — a range that has a gap exactly where 11
(`0x0B`), 12 (`0x0C`) and 13 (`0x0D`) sit, but that section's own text only explicitly
named 11 and 13 as the checked exclusions (12 had not shipped as far as anyone knew at
the time). This pass re-decompiled `FUN_00e711c0` in full and confirms directly, from
the real case list rather than by arithmetic on the documented range: **the switch has
cases for `0x00`–`0x04`, `0x0E`–`0x1A` and `0x64`–`0x65` only — there is no case for
`0x0B`, `0x0C` or `0x0D`.** Code 12 falls through completely unchanged, on exactly the
same footing as 11 and 13. **[CONFIRMED — disassembly, directly, extending §12.9.3's
existing 11/13 finding to include 12.]**

This matters for the task's own suggested angle of attack (§7's inferred "master
per-layout-code declaration table," indexed by the flattened *(layout, texcoord count)*
id this routine produces): **codes 11, 12 and 13 structurally never reach that
mechanism at all — there is nothing to look up, because the rewrite that would produce
their flattened index never runs for them.** Whatever table exists downstream of this
routine (if it exists at all — `spec-vertex-format.md` §12.9.14 could not find it
either) cannot, by construction, hold entries for these three codes. This is a clean,
disassembly-confirmed reason the "read the master table's entries directly" approach
cannot work for 11/12/13, rather than an artifact of not having found the table yet.

**Code 24 is different: it IS accepted (case `0x18`), and the routine's real arithmetic
for it can now be stated exactly** — the flattened id it writes is `texcoord_count +
0x57` (`'W'`), a small added detail §12.9.3 did not previously quantify. So code 24, in
principle, does reach whatever downstream mechanism consumes the flattened id — but
`spec-vertex-format.md` §12.9.14 already traced that consumer (the one concretely
identified engine-wide declaration-builder class, vtable `0x0129fd90`) to a genuine,
twice-confirmed static dead end: its single instance's pointer is discarded at its only
construction site, and none of its methods have a real caller anywhere else in the
binary. **Reading "the master table's real entries for code 24" is therefore not
possible either, for the same reason §12.9.14 already gives — not because the table was
never located, but because no live caller supplying content to build one for zones was
ever found.** This does not reopen or contradict §12.9.14; it confirms the same
dead end applies to code 24 specifically, closing the task's item 2 for all four codes
at once rather than leaving it untested for 24.

#### 12.10.4 A second `CreateVertexDeclaration`-shaped lead, REFUTED — the "0xf3xxxx module" is Wwise audio middleware, not a second vertex-declaration path

`spec-render-pipeline.md` §8 records a whole-binary census of the exact
`MOV reg,[base+0x158]` / `CALL reg` idiom used at the one confirmed
`CreateVertexDeclaration` call site (`FUN_00476ca0`) and found **two further real
matches**, both in a previously-uninvestigated address region: `FUN_00f3a6d0`, and one
further, at the time unlabeled, call site. That document flagged both as "OPEN,
plausible candidates for a second, DX11-adjacent or debug-tool vertex-declaration path,
not confirmed" — a live lead this document's own §12.9.14 did not know about when it
went looking for a tree-specific declaration caller. This pass runs it down directly.

**Both sites are REFUTED as any kind of vertex-declaration call — the containing module
is Wwise audio middleware, and the vtable-offset match is coincidental.** Full decompile
of both functions and their immediate siblings in the same address region:

- The call inside `FUN_00f3a6d0` (the `+0x158`-offset call `spec-render-pipeline.md` §8
  flagged) passes **zero explicit arguments** — the public `CreateVertexDeclaration`
  signature needs two (an element-array pointer and an out-pointer). This is exactly the
  argument-count tell `WALLS.md`'s own catalogued "coincidental vtable-slot offset"
  failure mode already uses to dispose of matches like this one.
- The second, previously-unlabeled site (`0x00f3bcf6`) sits inside a tiny thunk-shaped
  function that just tail-calls through a register with no argument setup of its own —
  consistent with, not contradicting, the same conclusion.
- The containing address region is unambiguously **Wwise audio SDK code, not a
  renderer**: a directly adjacent sibling function (`FUN_00f387a0`, itself one of the
  region's other coincidental vtable-offset hits — the `DrawIndexedPrimitive`-shaped
  `+0x148` slot per the same census) calls the literal, unmistakable Wwise API symbol
  **`AK::MemoryMgr::Malloc`** under an `EnterCriticalSection`/`LeaveCriticalSection`
  pair, in a device/capability-enumeration loop shape. Three further near-identical
  sibling functions in the same region (`FUN_00f398c0`, `FUN_00f3be50`, `FUN_00f3c3d0`)
  share one body template — read a capability code off one object, a count off another,
  compare both against small fixed constants (6/7/8/9) — the classic shape of an audio
  device/output-format negotiation routine, not a mesh draw call. This is a fresh,
  independently-discovered instance of the exact failure mode `WALLS.md` already
  catalogues for `DAT_0351f8f8`'s `0x98`/`0xAC` and the `0xd0c890`/`0xd0e6c0`
  OpenSSL-bignum pair: a numeric vtable-offset match with no argument-count or
  base-object-identity check behind it.

**[CONFIRMED — disassembly: both call sites take the wrong argument count for
`CreateVertexDeclaration`; CONFIRMED — disassembly, via the literal `AK::MemoryMgr::Malloc`
symbol, that the containing module is Wwise audio middleware, matching this project's
own existing "Audio: Wwise + wrapper" finding (`spec-audio-format.md`) rather than
anything render- or tree-related.]** `spec-render-pipeline.md` §8's own "OPEN,
plausible... not confirmed" hedge on this lead should be read as **REFUTED** going
forward — recorded in that document directly, and as a new `WALLS.md` entry, so a
future agent does not re-open this exact lead expecting a second declaration path.

#### 12.10.5 Summary — per-code element layout, all four codes, final state

| Code | Carrier | Size (bytes) | `+0` | `+8` | `+12` | texcoord slot | Confidence |
|---|---|---|---|---|---|---|---|
| **11** | trees | 32 + 4×texcoord (32 observed at idx 1, i.e. 36 total on disk) | **POSITION** — `D3DDECLTYPE_FLOAT16_3` (half-precision) / `D3DDECLUSAGE_POSITION`, at `+0`; 99.88% in-bounds, 22/22 channels (§12.12) | **NORMAL** — `D3DDECLTYPE_UBYTE4N` / `D3DDECLUSAGE_NORMAL` | **TANGENT** — `D3DDECLTYPE_UBYTE4N` / `D3DDECLUSAGE_TANGENT`, 4th byte = handedness sign | `D3DDECLTYPE_SHORT2` (raw) / `D3DDECLUSAGE_TEXCOORD`, at `+32` | Normal/tangent **CONFIRMED — empirical** (§12.9.5); position **HIGH CONFIDENCE — empirical, real consumer not yet found** (§12.12) |
| **12** | trees | 32 + 4×texcoord (36 total on disk, idx 1 observed) | **POSITION** — `D3DDECLTYPE_FLOAT16_3` at `+0`; 100.00% in-bounds, 4/4 channels (§12.12) | **NORMAL** — same encoding/offset as 11/13 | **TANGENT** — same encoding/offset/sign convention as 11/13 | `D3DDECLTYPE_SHORT2` (raw) / `D3DDECLUSAGE_TEXCOORD`, at `+32` | Size **CONFIRMED — empirical, 3 methods agreeing** (§12.10.1); normal/tangent **CONFIRMED — empirical, full population** (§12.10.2); position **HIGH CONFIDENCE — empirical** (§12.12) |
| **13** | trees | 32 + 4×texcoord (36 total on disk, idx 1 observed) | **POSITION** — `D3DDECLTYPE_FLOAT16_3` at `+0`; 100.00% in-bounds, 4/4 channels (§12.12) | **NORMAL** (small-sample corroboration only) | **TANGENT** (small-sample corroboration only) | `D3DDECLTYPE_SHORT2` (raw) / `D3DDECLUSAGE_TEXCOORD`, at `+32` | Normal/tangent **HYPOTHESIS — unconfirmed, insufficient population** (§12.9.5); position **HIGH CONFIDENCE — empirical** (§12.12) |
| **24** | zones | **20 bytes, NO texcoord** (`idx`=0 really does mean zero here — see correction below) | **POSITION** — `D3DDECLTYPE_FLOAT3` / `D3DDECLUSAGE_POSITION`, full population 581,647/581,647 | **NORMAL** at `+12` (this code's base is 8 bytes shorter than 11/12/13, so its own normal sits at `+12`, not `+8`) — `D3DDECLTYPE_UBYTE4N` / `D3DDECLUSAGE_NORMAL` | `+16` slot is **OPEN**, not a clean tangent (pooled unit-vector rate 8.0%, real but uncharacterised per-channel-varying structure in 62/448 channels); also now REFUTED as FLOAT16×2/SHORT2-family position, secondary-UV, or skinning weight (§12.13 item 3) | **NONE — this code carries no texcoord at all, in 448/448 real channels** (corrected, 2026-09-30; see below — an earlier version of this row wrongly stated a texcoord at `+20`, extrapolated from codes 11/12/13's pattern rather than measured for 24 specifically) | Position/normal **CONFIRMED — empirical, full population** (§12.3); `+16` slot **OPEN, REFUTED as tangent** (§12.3) **and REFUTED as FLOAT16×2/SHORT2-family position/UV/weight** (§12.13 item 3); **no-texcoord CONFIRMED — empirical, full population, cross-team** (§12.13 item 4) |

**A note on the `D3DDECLTYPE`/`D3DDECLUSAGE` enum names in the table above, stated
plainly so this is not overclaimed:** the *byte-level encoding* (UBYTE4N unit vector,
raw signed-short pair, float3) is CONFIRMED by the same empirical battery this document
uses for every other layout code, and the enum name given is the direct, unambiguous
Direct3D name for that encoding/role pair — the same non-hedged convention this
document already applies throughout (e.g. §6.2's normal/tangent, §6.6's texcoord
`SHORT2`). What is **not** established for any of these four codes, and is a real gap
shared with the rest of this format (§12.9.4 notes it generically): the literal numeric
type/usage/method **byte values** a real in-memory element-descriptor record would
carry into `FUN_00476ca0` (§12.9.14.2's `{type, usage, method, ...}` shape) were never
read from an actual descriptor list for 11/12/13/24, because no live caller supplying
one was ever found for any of the four (§12.10.3). This is the same "encoding confirmed,
literal descriptor bytes not observed" gap this document already carries for codes
0–4/100/101, not a new weakness specific to the tree/zone codes.

**Net effect on the task's own three concrete asks:** (1) the tree/zone call site that
builds the descriptor list feeding `FUN_00476ca0` was **not found** for any of the four
codes — for 11/12/13 because they structurally never reach that function at all
(§12.10.3, newly confirmed for 12), and for 24 because its one live consumer class is a
confirmed static dead end (§12.9.14, re-confirmed applicable here in §12.10.3). (2) The
"master per-layout-code declaration table" was checked directly, via the real switch
cases of the routine that would feed it, and closes as the same negative for all four
codes. (3) Code 12's element size was cross-checked empirically against the stride law
and agrees exactly with a peer team's own direct measurement (36 bytes, §12.10.1) —
resolving the one sub-item that was genuinely open (size was never even structurally
established for 12 before this pass) with no discrepancy to reconcile.

Harness: `tools/harnesses/vtx_code12_probe.py` (this pass, reuses `mesh_scan.py`,
`vehgeo_bulk.py`, `vertex_decl.py`, `vertex_fields.py`, `vertex_fields2.py` unmodified
except for the corrected 4-byte g-side alignment grid `spec-vertex-format.md` §4.2
already documents as necessary for trees/zones). Ghidra scripts, this pass
(`tools/scripts/`): `VtxCode12F3Module.java`, `VtxCode12RewriteCheck.java`, run against
`tools/gp_render9`.

### 12.11 Codes 11/12/13's Position field — a second, independent, genuinely falsifiable negative (2026-09-29)

The static "master declaration table" dead end (§12.10.3) left Position for codes
11/12/13 open on one method only. This pass tried a structurally different, empirical
method with a real control that could fail: decode each candidate byte offset within
the confirmed 36-byte stride as a plain float3 (and, secondarily, as a bbox-dequantized
`SHORT3`/`USHORT3`), for every real tree LOD channel of all three codes, and check the
decoded XYZ against that SAME tree's own on-disk bounding box (`spec-tree-format.md`
§5's `G+0x00`/`G+0x10` min/max float4 fields) — a wrong offset should produce
out-of-bounds or degenerate output, not a plausible-looking match.

**Candidate offsets tested:** `+0` (the pattern code 24 uses, §12.10.5's own row for
24), `+4`, `+16`, `+20` — the only bytes in the 36-byte stride not already claimed by
the confirmed Normal (`+8`)/Tangent (`+12`). Pooled across all channels of each code
(re-run independently by the orchestrator for code 13 and reproduced exactly): code 11
(28,457 vertices, 22 channels — a larger population than previously recorded, because
this pass also applied the 4-byte alignment fix to code 11 and found most trees ship a
second code-11 LOD channel the original scan had missed), code 12 (609 v, 4 chans),
code 13 (1,172 v, 4 chans, orchestrator-reproduced figures: `+16` finite 58.0%/in-bounds
53.2%, `+20` finite 9.1%/in-bounds 7.4%, matching exactly).

**Result: NO candidate offset, for ANY of the three codes, decodes to coherent,
tree-shaped position data.** A handful of per-channel cells clear a naive ≥90%-pass
bar (`+0`/`+4` on 3/22 code-11 channels; `+20` on 2/22 code-11 channels; `+16` on 1/4
code-13 channels) — but checking the actual decoded numbers behind every one of these
(not just the pass rate, per this method's own falsifiability requirement) shows they
are IEEE-754 denormal artifacts, not position: values on the order of `1e-14` to
`1e-41`, computationally indistinguishable from zero, which trivially satisfy a bounds
check spanning the origin without encoding anything real. The mechanism is traced
directly: §12.9.9's already-documented "offset `+20`'s bytes `+22`/`+23` read constant
`0` in 100% of code-11 vertices" extends to code 12 (also constant-`0`, new finding
this pass) — forcing the little-endian float at `+20` to always be a near-zero denormal
regardless of any position hypothesis. (Code 13's own `+20` near-miss has a different,
uncharacterized cause — its `+22`/`+23` bytes vary freely, unlike 11/12.) The secondary
quantized-`SHORT3`/`USHORT3` check is also negative: per-axis bbox-coverage fractions
cluster at 0.5–0.8 across offsets, with several axes at exactly `0.000` coverage — the
same constant-zero-byte artifact, not a genuine quantized signal.

~~**Position for codes 11, 12, and 13 remains OPEN — but this is now a second,
independently-derived, genuinely falsifiable negative, not just the static dead end.**
Neither a plain float3 nor a bbox-driven quantized short format at any free offset in
the 36-byte stride decodes to real position data, for any of the three codes, across
the full named tree population (11 trees, every LOD channel found). **[CONFIRMED —
empirical, full population, real per-tree bounding-box control that demonstrably can
and does fail.]**~~

**SUPERSEDED same day, §12.12 — this negative was real for the decode type tried
(plain FLOAT32/quantized SHORT3/USHORT3) but the type itself was wrong.** A follow-up
pass tried FLOAT16 at these same offsets and found a real, non-denormal, tree-shaped
positive at `+0` (duplicated at `+16`) — see §12.12. The FLOAT32 negative above is not
wrong on its own terms (it correctly shows FLOAT32 doesn't work at these offsets) and
is kept for the record, but do not cite "no explicit position field exists" as this
document's conclusion — see §12.12 instead.

~~**A new hypothesis, flagged honestly as unconfirmed and out of this pass's own
scope:** given Normal/Tangent ARE present and correctly decode, but no stored float3
or quantized position survives a real bounds check anywhere in the remaining bytes, it
is worth considering that these tree LOD vertex streams may not carry an explicit
per-vertex position at all — position could instead be reconstructed GPU-side from a
per-draw/per-instance root transform plus a smaller relative offset, or generated
procedurally (e.g. a billboard-corner or wind-function expansion), consistent with the
already-confirmed wind-state object at `TREE+0x38` (§4) and with §12.9.14's finding
that no CPU-side reader/writer or reachable declaration-builder caller exists for these
codes. **This would need a live/dynamic trace to confirm or refute — not claimed as
fact, only flagged as a real, motivated possibility for whoever picks this up next.**~~
**This hypothesis is now most likely moot — §12.12 found a real candidate position
field, so the "no explicit position" possibility is de-prioritized, though not
completely excluded until §12.12's own open caveats are resolved.**

Harness: `tools/harnesses/tree_position_boundscheck.py` (reuses `mesh_scan.py`,
`vehgeo_bulk.py`, `vertex_fields2.py`, and the 4-byte g-alignment fix `vtx_code12_probe.py`
established). Bug caught and fixed during this pass: the bbox-read step's material-block
trailing pad must be a mandatory 1–16 byte ADVANCE (a full 16 when the block already
ends 16-aligned), not a plain round-up-to-16 — `spec-geometry-format.md` §3.1.1's rule,
already correctly implemented in `tree_replay.py`; fixed to match, then verified exactly
against this document's own previously-cited bbox examples (`st_com_break`,
`st_com_lrg`) before trusting the rest of the run.

### 12.12 Codes 11/12/13's Position field — FOUND: FLOAT16×3 at `+0` (duplicated at `+16`), a real, scaled, non-denormal positive (2026-09-29)

**§12.11's own FLOAT32/quantized-short negative was real but tested the wrong decode
TYPE, not just candidate offsets.** A peer reviewer's lead: §12.11's denormal artifacts
(FLOAT32 values ~`1e-14` to `1e-41`) mean the high half of each 32-bit word is
small/zero — exactly what a genuine 16-bit field looks like when misread as one 32-bit
float. Re-running the SAME per-tree bbox control (decode a candidate offset, compare
against that tree's own on-disk bbox, a control that can and does fail) against every
free offset in the 36-byte stride, now also decoded as FLOAT16×3/×4, SHORT4 (raw and
÷1024, reusing this document's own confirmed universal texcoord descale constant,
§6.6), and bbox-dequantized SHORT4N/UBYTE4N (signed and unsigned):

**Result: FLOAT16×3 at offset `+0` passes cleanly, at real non-denormal magnitude, for
all three codes, independently reproduced twice (once by the investigating pass, once
by the orchestrator from scratch, exact figures matching both times):**

| Code | In-bounds / total | Channels ≥90% |
|---|---|---|
| 11 | 28,423 / 28,457 (99.88%) | 22/22 |
| 12 | 609 / 609 (100.00%) | 4/4 |
| 13 | 1,172 / 1,172 (100.00%) | 4/4 |

Offset `+16` gives the identical result (expected — §12.9.7 already documents `+16` as
a near-duplicate of `+0`, ~70% byte-identical). **This is NOT a repeat of §12.11's
denormal artifact** — the decoded numbers themselves were checked, not just the pass
rate: per-tree, decoded XYZ spans a real, appropriately-scaled fraction of that tree's
own bbox (e.g. `st_pine_tall`, a 43 m pine with bbox Y=[0, 43.03], decodes Y values
spanning nearly its full real height, while X/Z stay inside its narrow trunk spread — a
real tree silhouette, not zero-clustered noise). A side-by-side check also rules out
"this is secretly Normal/Tangent under a different reading": the FLOAT16@`+0` vector
shows no correlation with the confirmed unit Normal(`+8`)/Tangent(`+12`) at the same
vertices. **[CONFIRMED — empirical, full population, independently reproduced twice,
real per-tree bbox control, non-denormal decoded values checked directly.]** *(Qualified by the Net effect paragraph below: this label covers the FLOAT16×3 decode passing the bbox control; the field's identity as POSITION is HIGH CONFIDENCE only, pending a consumer trace.)*

**Every other decode type tried at these offsets is a clean negative, for completeness:**
SHORT4 raw (no rescale): 0.00–0.06% in-bounds. SHORT4 raw÷1024 (this project's own UV
constant, reused as a cheap position hypothesis): 1.4–4.6%. **The bbox-dequantized
SHORT4N/UBYTE4N tests are a methodological dead end, flagged explicitly so nobody
reuses this specific recipe uncritically:** dequantizing any N-bit int through
`bias + (raw/max_raw) × scale`, where `bias`/`scale` come from the SAME tree's own bbox,
lands inside `[bmin, bmax]` *by construction* — it "passes" 100% at every offset
simultaneously (`+0`, `+16`, `+20`, `+24` all at once), which is logically impossible
for a real position field and immediately outs itself as non-falsifiable, not a real
signal. No separately-documented "vehicle position scale/bias" mechanism exists
anywhere in this project's specs to reuse instead (checked directly — vehicles' own
codes 100/101 use plain FLOAT3 position, unquantized).

**Why every prior FLOAT32 test at this exact offset failed (§12.9.6/§12.11) is now
explained, not contradicted:** a genuine 16-bit-packed field, misread as one 32-bit
float, produces exactly the garbage/non-finite results already measured there (0–71%
finite) — the old negatives were real measurements of the wrong interpretation, not a
wrong method.

**Honest caveats, not smoothed over:**
1. `+0`/`+16` is the same pair §12.9.7 already found to be only ~70% byte-identical
   cross-channel, with an existing hedge that it could be runtime-populated/stale
   rather than authored. This finding doesn't resolve that tension — it shows that
   *when read as FLOAT16*, the content looks like real, correctly-scaled tree
   geometry, which is new evidence toward "real, authored data," not proof against
   "stale-but-coincidentally-valid."
2. No CPU/GPU consumer of this exact byte range was found — consistent with §12.9.1's
   existing finding that neither Mesh-block walker dereferences individual vertex
   fields for these codes. This result stays empirical/statistical, same confidence
   tier as the rest of this section, not disassembly-confirmed against a real reader.
3. Some of the "22 channels" for code 11 are byte-identical cross-archive duplicates
   of the same physical tree — the real independent sample is closer to 19 unique
   tree/LOD combinations, still substantial.
4. A spatial-continuity control (checking whether adjacent-index vertices decode to
   smoothly-varying rather than shuffled positions) was tried as an extra check but
   turned out not to be discriminating — it fired "smoother than random" even at
   offsets already known wrong — so it is NOT cited as supporting evidence here.

**Net effect: Position for codes 11/12/13 is HIGH CONFIDENCE — empirical, FLOAT16×3 at
offset `+0`, promoted from §12.11's exhausted-negative state, but stopped short of
CONFIRMED pending a real consumer trace (caveat 2 above).** §12.10.5's own summary
table is updated accordingly. Team B's tree-rendering golden-scene baseline can treat
this as the best available real decode for Position and prototype against it, with the
above caveats stated plainly rather than hidden.

**Two independent runs of the underlying check, both exact-match:** the investigating
pass's own run and the orchestrator's independent re-run from the same (audited, not
just trusted) harness extension both produced the identical 28,423/28,457, 609/609
figures; the orchestrator did not independently re-run code 13 but has no reason,
given the exact match on the other two codes and the mechanistic explanation, to doubt
the third figure.

Harness: `tools/harnesses/tree_position_boundscheck.py`, extended this pass with the
additional FLOAT16/SHORT4/UBYTE4N decode branches (working copies audited under the
investigating agent's own scratch directory; the orchestrator re-ran the extended
script directly rather than trusting the report alone).

### 12.13 Targeted refinements — strip-stitching winding CONFIRMED, normal's 4th byte narrowed (not material id, not vertex color), code 24's `+16` slot multiply-refuted, and code 24 confirmed genuinely texcoord-free with a real textured companion stream (2026-09-29/30)

A dedicated pass took on three specific open items this document had each flagged with a concrete next step, and closed or narrowed all three. No disassembly was needed — all three are empirical/harness work built on top of already-CONFIRMED structures (the draw-range decode of §8.2, the channel-array walk of §4). Population: 1,756 real character/static-prop meshes for item 2, 752 real multi-material meshes (2,647 draw ranges) for item 1, 448 real code-24 channels (581,647 vertices) for item 3.

**Item 1 — the normal's 4th byte (§6.2/§12.4/§10 item 4): the material-id correlation §12.4 flagged as its own next step, now run.** For 752 real meshes with ≥2 materials in LOD group 0, the byte's dominant value and purity were computed within each draw range's actual referenced vertices (2,647 ranges):

- **Per-range purity (dominant value's share of that range's own vertices): mean 0.6366, median 0.5859** — far below the ~99% per-**triangle** constancy §12.4 already measured. The byte is constant over regions substantially smaller than a material-bounded draw range, i.e. finer-grained than material.
- Cross-range-pair test within the same mesh: ranges with **different** material ids share the same dominant byte value in 488 pairs vs. 4,064 pairs where they differ — a real but incomplete separation, not the clean 1:1 mapping literal identity would require.
- Direct numeric identity (dominant byte value equals the range's material slot id): **3 / 2,647 (0.0011)** — a clean rejection.

**Conclusion: the normal's 4th byte is NOT the draw-range material id** — REFUTED both by exact-value and by granularity mismatch — while remaining fully consistent with, and reinforcing, §12.4's "smoothing-group-style" alternative (a partition finer than and orthogonal to material assignment is exactly what real smoothing groups look like). Vertex color and AO/occlusion were also directly re-checked: vertex color is REFUTED decisively (the byte's range caps at 127 in all 555,771 of §12.4's sampled vertices — bit 7 never set; a real color/alpha channel pins near 255 or spans 0–255, this does neither); AO/occlusion was not re-tested (already weakened in §12.4, one signal each way). No other project document was found (grepped all `spec-*.md` for "smoothing group"/"vertex color"/"occlusion"/"region id") to cross-check an analogous field against. **[CONFIRMED — empirical, real data, controlled: material-id identity and vertex color both REFUTED; the smoothing-group-style reading stands as the best-supported, still-unconfirmed candidate.]**

**Item 2 — the exact within-range strip stitching/winding convention (§8.1/§8.2/§10 item 7), flagged there as unpinned: now CONFIRMED with an exact, falsifiable rule.** Real draw ranges of 1,756 real character/static-prop meshes (`.ccmesh_pc`/`.csmesh_pc`) were walked; for every non-degenerate triangle, a face normal computed from real decoded vertex positions was scored against the dot product with the mean of the three corners' already-CONFIRMED decoded vertex normals (§6.2) — an independent ground truth, not assumed:

| Convention | Agreement |
|---|---|
| RAW, always `(i0,i1,i2)`, no correction | 2,618,734 / 5,077,382 (0.5158) — chance level |
| ALT (odd-*k* swaps first two indices), parity from each range's own start | **5,043,382 / 5,077,382 (0.9933)** |
| ALT, parity from the buffer's global position instead | 3,824,717 / 5,077,382 (0.7533) |

The global-parity control scoring far below the local-parity reading confirms parity resets at each draw-range boundary, consistent with real D3D9 `DrawIndexedPrimitive` semantics (each draw call's own strip parity starts fresh regardless of `StartIndex`). Restart run-lengths were also checked directly: of 3,345,760 degenerate triangle-slots (1,043,236 length-1 restarts, 8,905 length-2, 9,110 length-3, 268,761 length-4, 236,468 length-5), the ALT/local-*k* convention scores 97.9–99.6% on the very first real triangle immediately after a restart, **at every observed run length** — i.e. no separate, explicitly-encoded winding correction exists at restarts of any length; the fixed rule, continuously counted from the range's own start with degenerate slots included in the count, is sufficient on its own.

**Rule, stated for an implementer:** triangle *k* (0-indexed from each draw range's own start, degenerate slots counted) is `(idx[k], idx[k+1], idx[k+2])` for even *k*, `(idx[k+1], idx[k], idx[k+2])` for odd *k*; discard degenerate triangles; never carry parity across a range boundary. **[CONFIRMED — empirical, controlled, real ground truth, 5,077,382 real triangles.]**

**Not extended to every carrier — flagged explicitly so nobody assumes otherwise.** Vehicles (`.ccar_pc`) were attempted and did not work in the time available: their outer container uses a different header locator entirely, and store multiple channels per mesh (apparently separate LOD copies as distinct channels) rather than one channel with grouped ranges, so untangling which channel each LOD group's ranges actually index was out of scope given the character/prop result was already decisive on 5M+ real triangles. `.clmesh_pc`/`.czn_pc` also don't share `.ccmesh_pc`'s outer-header shape and weren't pursued. This result is CONFIRMED for characters and static props specifically, not generalized further.

**Item 3 — code 24's `+16` slot (§12.3/§12.10.5/§10 item 1): FLOAT16×2 and the entire SHORT2 family now also REFUTED for position/UV/weight, extending the existing UBYTE4N-tangent refutation — and a real methodology trap caught along the way.** Tested against each channel's own real position bbox (computed from the already-CONFIRMED `+0` float3 decode — zones have no independent external bbox field the way trees do, §12.12):

- FLOAT16×2: 67.55% finite, only 25.13% in-bounds, 0/448 channels ≥90% — negative.
- SHORT2 raw: 100% finite, 0% in-bounds — negative.
- USHORT2 raw: 0% in-bounds — negative.
- **SHORT2÷1024 (this format's universal texcoord constant, §6.5) initially looked like a real positive: 96.58% pooled in-bounds, 349/448 channels ≥90%.** Applying the §12.12 lesson before trusting it: a **shuffled-mesh control** (scoring the same values against a randomly-mismatched *other* channel's bbox) scored **80.71%** — nearly as high as the "real" figure, showing the per-channel-bbox test has weak discriminating power here (zone-chunk position bboxes are apparently similar-sized/local, so almost any small 2-vector passes against almost any chunk's box). A much tighter **per-vertex** test (does this exact vertex's decoded `(a,b)` track that same vertex's own real coordinate, tolerance 0.5 units) came back at **chance level for both the real pairing and a shuffled-vertex control** (real: 0.000003–0.000065 across all 6 axis-pairings; shuffled: 0.000005–0.000062, statistically indistinguishable) — **decisively REFUTED** as position data.
- Also checked: weight-partition-of-unity (the §6.3 skinning-weight signature, bytes summing to ~255: only 1.15% of vertices land in `[240,270)`, vs. ~100% for real skinning weights) — refuted. **Correction, 2026-09-30: this pass's own additional sub-check, "resemblance to the real second texcoord at `+20` on the same vertex," rested on a false premise and its `0/581,647` result should be read as "the premise was wrong," not as an independent refutation.** There is no texcoord at `+20` for code 24 to compare against at all — see item 4 below, where direct re-measurement against real bytes shows code 24's own measured stride is exactly 20 bytes (`a=20, b=0`, 448/448 real channels), meaning byte offset `+20` is not a field of this vertex, it is the *next* vertex's own `+0` (position.x). The rest of this item's conclusion (FLOAT16×2/SHORT2-family REFUTED for position/secondary-UV/weight via the per-vertex bbox-tracking test, which does not depend on this premise) is unaffected.
- Byte-level stats: **not** padding — real per-vertex variation exists in every channel (7 to 167+ distinct raw 4-byte values per channel). Bytes 0–2 skew low; byte 3 skews high (57% in `[240,255]`, 32.32% exactly 255) — *suggestive of* an RGB-blend-mask-plus-alpha/opacity byte, but flagged as an unconfirmed, motivated lead only — no independent vertex-color or blend-mask reference exists elsewhere in this project to validate it against.

**Conclusion: FLOAT16×2 and the SHORT2 family (raw, ÷1024, unsigned) are all real, controlled negatives for position, secondary-UV, and weight at this offset.** The field is real (not padding) and its content remains unidentified. **[CONFIRMED — empirical, controlled, full population, including a per-vertex control that catches a bbox-inclusion-test false positive a less careful check would have reported as a breakthrough.]**

**Methodology note — a good `WALLS.md` candidate, flagged by the investigating pass and not yet promoted there: the naive per-channel-bbox-inclusion test that initially looked like a breakthrough for SHORT2÷1024 is a real instance of the same non-falsifiability trap §12.12 already warned about for bias/scale dequantization — but this shows it can also bite a *fixed, no-fitted-parameters* decode, not only a fitted one, whenever the reference bbox itself is too generic (as zone-chunk position boxes apparently are). A per-vertex control, not a per-channel-bbox control, is the safer default check going forward.**

**Item 4 (2026-09-30) — code 24 has NO texcoord at all, in the full real population, confirmed independently on both sides of the clean-room boundary; and it has a real, textured companion stream in the same mesh, which narrows what it can be.** Cross-checking a peer team's own file-side population sweep of `.czn_pc` (their own independently-built reader, `sr3zone::ZoneGeometry::locate()`) against this side's disassembly-confirmed channel-record model (`spec-geometry-format.md` §4.1.1) turned up the item-3 premise error corrected above, and resolving it produced a real, useful finding:

- **Direct re-measurement, real bytes, full population** (`.czn_pc`/`.gzn_pc`, `sr3_city_0`/`sr3_city_1`, `mesh_scan.py`+`vertex_decl.py`'s own channel-record reader): every one of 448 real code-24 channels reads `a=20, b=0`, i.e. **measured stride 20 bytes exactly, zero texcoord bytes appended** — not 24. `predict_stride(24, idx=0) = base(20) + 4×0 = 20` was the correct reading all along; the error was in an earlier pass's own table row, which stated a texcoord existed at `+20` by analogy with codes 11/12/13 (which genuinely do carry one) rather than by measuring code 24's own channel record. **[CONFIRMED — empirical, full population, re-measured directly from real bytes, matching the peer team's own independent file-side finding of `texcoordCount == 0` exactly.]**
- **Code 24 channels usually, but not always, co-occur in the same Mesh sub-block with other channels that DO carry real texcoords.** Of 448 real code-24-bearing meshes, **404/448 (90.2%) have at least one companion channel** — exclusively **sem 0** (base 16, seen at texcoord counts 0/1/2/3 — i.e. `(a,b)` = `(16,0)`/`(20,0)`/`(24,0)`/`(28,0)`), **sem 2** (base 20 — position+normal+tangent — at texcoord counts 0/1/2), or **sem 4** (base 12, position-only, always with 1 texcoord) — while **44/448 (9.8%) have code 24 as their ONLY channel (`nchan`=1), no companion of any kind.** **Correction, 2026-09-30 (peer cross-check, Team B): an earlier version of this bullet said the companion channels are present without qualifying how often — Team B independently ran the identical check over their own larger population (535 code-24-bearing blocks, a different archive scope than this document's 448) and found the same shape at almost the identical count: 491/535 (91.8%) have a textured sibling, 44/535 (8.2%) do not — the same absolute count of 44 "no companion" cases as this side's own 448-mesh population, despite the different denominator, which is a real, if coincidental-looking, cross-check.** **The sem-0/idx-1 channel's own texcoord slot (at its own `+16`) decodes to clean, plausible UV values** (real samples: `(0.270, 0.410)`, `(0.005, 0.410)`, `(0.999, 1.000)`, `(0.744, 0.192)` — the expected [0,1]-with-mild-tiling-overflow shape §6.5 already establishes generically) using the same `SHORT2÷1024` decode this document uses everywhere else. **[CONFIRMED — empirical, real decoded UVs, plausible range; the 90/10 split independently cross-checked by a peer team on a different, larger population.]**
- **Reading: code 24 is USUALLY a UV-less companion stream sharing a Mesh sub-block with an otherwise-normal, textured mesh stream, not a texture-less version of the *only* geometry for that surface — but in ~1 in 10 real cases it is the only stream present, which is either a distinct sub-case (a different role for that ~10%) or means the textured counterpart lives in a different block/file for those meshes specifically; not distinguished here.** Where a companion exists, code 24 is not evidence that zone surfaces are untextured in general — it is evidence that **this specific stream** doesn't need UVs. **Role update, 2026-09-30 (Team B, real render evidence — their own HANDOFF §9.130): a composite render of both channels together in one frame, at two camera angles, settles what code 24 actually is. The code-0/2/4 window and facade detail cards sit inside code 24's own wall faces and hug its silhouette edges — the two are coplanar, and the combined result reads unambiguously as buildings with windows on their walls.** **Code 24 is therefore CONFIRMED to be the visible building shell itself, not a shadow/occlusion/collision proxy** — that reading is retired (Team B has retired it project-wide). **What remains genuinely open is how a UV-less shell gets its surface appearance at all** — the standard techniques for exactly this situation are a world-space or triplanar projection computed from position/normal (no stored UV needed), vertex-color-driven shading, or a flat/parameter-only material with no texture sampling. **[CONFIRMED — real composite render, cross-team, for the visible-shell role; OPEN — the texturing mechanism itself; a dedicated shader-consumer trace is in progress to settle it from the disassembly side.]**

**Net effect: the `+16` slot's REFUTED status (item 3) is unaffected by this correction — every one of item 3's real tests (bbox-inclusion, per-vertex tracking, weight-partition-of-unity) stands independently of the erroneous `+20` comparison, which is now removed rather than counted as supporting evidence.** What changes is the surrounding picture: code 24 is now understood to be genuinely, confirmedly texcoord-free (not just unmeasured), and to be one of several parallel vertex streams inside a zone mesh's Mesh sub-block rather than the sole geometry description for that surface.

**Item 5 (2026-09-30) — why code 24 never carries a texcoord: a dedicated shader-consumer trace across the entire real shader corpus finds it's the vertex shape this engine's own shared G-buffer normal/lighting-parameter fill pass requires, not a special case.** Following up on the role update above (code 24 confirmed the visible building shell, not a proxy), a pass built a real D3D9 SM2.0/3.0 shader-bytecode disassembler (validated against the exact two shaders §6.6 already cites — reproduced §6.6's own `mul r0.x,c1.x,v1.x`/`mul r0.y,c2.x,v1.y`/`mul o1.xy,r0,c3.x` pattern and independently re-derived the same `1/1024` `def` constant, byte-for-byte) and used the project's own already-resolved mode-(a) physical-offset rule (`spec-vpp-container.md` §7) to bulk-extract and disassemble the ENTIRE real shader corpus: all 844 distinct `.fxo_pc` entries in `shaders.vpp_pc`, 3,731 vertex-shader blobs total — not hand-picked samples.

- **The position+normal/zero-texcoord vertex shape (exactly what code 24 measures) is routine in this engine, not special-cased: 761 VS-blob instances across 100+ distinct files.** It appears in car glass, TV screens, swimming-pool water, distant-vehicle LODs, decal materials, vertex-colored materials, "diffuse-color-only" materials, and window-reflection materials, among others — real, disassembly-confirmed, not inferred from naming. **[CONFIRMED — real disassembled bytecode, full-corpus sweep, 844/844 files.]**
- **Two real sub-families exist within that shape.** (a) Genuinely flat/parameter-only materials: e.g. `ir_vertexcoloring1_s.fxo_pc` technique 1 (VS input = `{normal, position}` only; its paired pixel shader, found via the file's own real T8 pass table, has ZERO texture-sample instructions and computes its 3 outputs purely from the interpolated normal via `dp3`/`rsq`/`mad`/`nrm`) — the same boilerplate shape recurs, byte-for-byte, in `ir_sr3diffcolonly_s.fxo_pc`, `ir_sr3megatv_s.fxo_pc`, `ir_sr3_swimmingpool_s.fxo_pc`, `ir_sr3glass_diffuse_reflect_s.fxo_pc`, `ir_window_reflectmask_scraper_s.fxo_pc`. (b) Texture sampled with zero per-vertex UV, but via **spherical environment-map reflection**, not triplanar: of 2,432 real texture-sample instructions reachable from this vertex shape across the whole corpus, only 14 files (`rl_ir_envmap_bs/s.fxo_pc`, `ir_decal_*.fxo_pc`, `ir_blood_pool_*.fxo_pc`, `ir_sr3carglass_*.fxo_pc`) show a position/normal-derived value projected into a 2-component UV before sampling, and in every one of those 14 the pattern is the standard normalized-reflection-vector-into-2-components formula — not a world-space XZ/dominant-axis triplanar projection. **No literal triplanar pattern was found anywhere in this family — a real, though not provably exhaustive, negative.** **[CONFIRMED — real disassembled bytecode for both sub-families; OPEN/negative — no triplanar found, not proven absent everywhere.]**
- **The concrete link: code 24's own vertex shape is exactly what this project's already-CONFIRMED "role 4/5" cheap G-buffer pass requires, checked directly against real T8 pass tables.** `spec-render-pipeline.md` §23.3/§23.5 already established, via exe-side disassembly, that a byte-flag mechanism selects a real, CONFIRMED "cheap pass" (role 4/5) that binds exactly 3 color render targets + 1 depth-stencil surface before running — independently corroborated by a peer team's own 844/844 CTAB scan finding the real named samplers `IR_GBuffer_NormalsSampler`/`LightingSampler`/`DepthSampler` on the pass that reads them back (`spec-render-pipeline.md` §23.5/§23.6.2). Checking 8 real, unrelated materials' own file-internal T8 pass tables: in every case, the pass role 4/5 points to has a vertex shader in exactly the position+normal(+optional vertex-color)/zero-texcoord shape, and its pixel shader has zero texture-sample instructions and writes exactly 3 outputs via a normal-encode formula (`×0.5+0.5` on the world/view normal) — matching the G-buffer normals-fill role exactly. **[CONFIRMED — real file-internal T8 pass-table structure plus real disassembled bytecode, 8 real files checked, cross-corroborated by an independent peer CTAB scan of the same 844-file population.]** **HIGH CONFIDENCE, not traced end-to-end:** role 4/5 is resolved generically for any material draw (part of the shared material technique-select path, not a vehicle-specific one), so it's plausible zone/building geometry goes through the identical universal pass — but no actual `.czn_pc` draw call was traced reaching that selection code this pass.

**Reading: code 24 isn't a special-cased offscreen "proxy" — it's the vertex shape this engine's own shared, near-universal G-buffer normal/lighting-parameter fill pass structurally requires.** That explains both why it never carries a texcoord (that pass never samples a diffuse map at all) and why the shell alone renders as a flatly-shaded solid (that's exactly what a G-buffer normals/depth fill looks like in isolation) — while the surface's real diffuse appearance is most plausibly carried by the sibling, texcoord-bearing channel (sem 0/2/4) through a *different* pass on the same or a paired shader. **Genuinely open, not decided:** no confirmed binding from a specific real zone draw range's material id to any one `.fxo_pc` filename exists (the vehicle-style `shaderHash`-based mechanism is confirmed absent for zones, unchanged from this section's own item 4 finding); no confirmed trace that a real `.czn_pc` draw call reaches the role-4/5 selection path specifically; the ~10% of code-24 blocks with no textured sibling channel (this section, item 4) remains unexplained by this finding.

Harnesses (new this pass, `tools/harnesses/`): `strip_winding_check.py` (item 2; `strip_winding_check_veh.py` incomplete, vehicles not resolved), `normal_byte_material_corr.py` (item 1), `code24_slot16_probe.py`/`code24_slot16_control.py`/`code24_slot16_pervertex.py`/`code24_slot16_names.py`/`code24_slot16_bytestats.py` (item 3). All reuse existing modules (`mesh_scan.py`, `vertex_decl.py`, `vertex_fields2.py`, `batch_probe.py`, `batch_array.py`, `batch_validate.py`, `car_runcount.py`, `car_textures.py`) rather than re-deriving structure already established elsewhere in this document. Item 4's own verification: ad hoc scratchpad scripts against `mesh_scan.py`/`vertex_decl.py`/`vehgeo_bulk.py`/`vertex_fields2.py` directly, real data (`sr3_city_0.vpp_pc`/`sr3_city_1.vpp_pc`), not yet promoted to a named `tools/harnesses/` script. Item 5's own tooling (new, `tools/harnesses/`): `sm_disasm.py` (a real, validated D3D9 SM2/3 shader-bytecode disassembler — the first in this project), `fxo_layout24_shape_scan.py` (the full 844-file/3,731-VS-blob corpus scan), `fxo_role_to_pass_check.py` (role-byte → T8 pass mapping check), `fxo_uvless_texld_sweep.py` (the 2D-sampler-texld-without-UV sweep) — built on the existing `vpp_modea.py`/`fxo_parse.py`.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): fixed 10 cross-references (8 leftover §1.x→§12.x in §12.1–§12.6, `spec-tree-format.md` §17→§4/§13.2, §12.10.5 code-13 row §12.3→§12.9.5) and named the doc on 5 bare/HANDOFF refs (§26.16, §27.2 ×2, §5/§27.6→§5); marked stale text superseded in place (accepted layout-code set §7/§10 item 2, code-12 shipping, §6.2/§10 item 4 tangent byte not holding on vehicles, §8.2 `high16` = vertex-channel index, §8.3 texture-binding OPENs →§12.8, §8.4.3 association routes, §11 block count 11,446→12,628 and arrays 1–3, §12.3 tree counts and code-13 `+12`, §12.10.2 `+0` row, headline vertex content qualifier); marked 7 stale OPEN items resolved (channel record `+0x08` ×3, component group B, tree position §10 item 1 and §12.9/§12.9.14/§12.10.2 headings, Mesh header `+0x04`/`+0x08` ×2, §8.1 per-range subdivision); qualified the §12.12 CONFIRMED label to match its own Net effect; added a population note to §12.1 and an offset note to §12.9.13; reworded 6 decompiler-shaped lines (§12.9.13, §12.9.14.1/.2/.5).
