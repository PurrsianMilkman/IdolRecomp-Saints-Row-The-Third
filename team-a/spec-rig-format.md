# Saints Row: The Third — `.rig_pc` Skeleton Format Specification (v2)

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1. **This version supersedes the original one-sample pass** (a 4-bone door prop). It was re-derived against **585 real rigs** after the format was found to block vehicles as well as characters (`spec-vehicle-geometry.md` §6). §9 lists exactly what the original got wrong.
**Scope:** The `.rig_pc` skeleton file: header, the per-bone name-hash table, the bone array (name, rest position, ~~rotation~~ offset-to-parent **[corrected §4.1: the bone record holds no rotation]**, parent), the attachment/IK-target array, and the name region. Standalone, not `c`/`g`-paired.
**Method:** The loader's pointer-fixup function (`FUN_004d0bb0`, bookmarked) read in full to derive the layout; every derived field then tested against all 585 shipped rigs — 40 in `preload_rigs.vpp_pc` (character bodies and the vehicle rigs) and 545 in `characters.vpp_pc` — with control baselines for every offset and identity claim. Several slot readings were found by *search* (which record offset satisfies a property across the population, against random-offset controls) rather than by assumption.
**Cleanroom compliance:** No decompiled code or original identifiers appear below. Offsets, strides and the hash algorithm's shape are load-bearing format data. Bone and attachment names quoted (`spine2`, `hsattach`, `l_elbow_targeta`…) are shipped data. Function addresses are cited as evidence only.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **The layout in the original spec was wrong.** There is a `u32`-per-bone table *before* the bone array, and the bone array begins at an 8-aligned offset past it. Under the loader-derived layout, **22,274 / 22,274** bone-name offsets resolve to real names; under the original "bones at `+0x50`" layout, **168 / 22,274** — the chance rate. **[CONFIRMED — empirical.]**
- **That per-bone table is the engine's filename hash applied to the lowercased bone name — 22,274 / 22,274**, against **0 / 22,274** for uppercase. The rotate-left-6 / XOR hash `spec-vpp-container.md` §2.2 documented for archive filenames is an **engine-wide string hash**, and header `+0x38` points at this table (585/585). ~~It is the lookup table behind the bone-by-name function the vehicle and animation loaders call. **[CONFIRMED — empirical + disassembly.]**~~ **[REFUTED 2026-09-20 — §13.1: the bone-by-name routine (`FUN_004bc880`) compares bone-name *strings* case-insensitively and never reads this table; no runtime reader of the table was found. The table's content (hash of the lowercased name, 22,274/22,274) is unaffected.]** **Update (2026-09-10, foliage pass): the hash routine itself has now been read in disassembly** (`FUN_00da7890`, called by the `.cfmesh_pc` constructor) — rotate-left-6 / XOR with an explicit `A–Z` → lowercase fold inside the routine, which is *why* the lowercased form matches. See `spec-foliage-format.md` §1.
- **Bone record:** name, **model-space rest position**, the **offset back to the parent**, and a **parent index** that forms a valid tree in 585/585 rigs. **The rest pose is purely positional — the bone record holds no orientation at all** (corrected 2026-09-11, §4.1; the field previously called a rotation triple is `pos(parent) − pos(bone)`, exact in 22,274/22,274). Consequently, composing bind transforms as pure translations is correct rather than an approximation. **[CONFIRMED.]**
- **The second array is attachment / IK-target points**, not a second bone list: named entries (`hsattach`, `l_elbow_targeta/b/c`, `l_knee_target*`, and per-bone attach points named after their bone), each with a parent-bone index (**12,480 / 12,480** valid) and ~~a quaternion (**12,473 / 12,480** within 10% of unit)~~ **[CORRECTED 2026-09-20, §13.4: a 3×3 rotation matrix plus a translation — the rows are orthonormal with determinant +1 in 12,480 / 12,480; there is no quaternion]**. **[CONFIRMED for names/parent; HIGH CONFIDENCE for the interpretation.]**
- Maximum bone count across all rigs is **109 — exactly the maximum of the `.anim_pc` header's `+0x0B` count** (`spec-anim-format.md` §2), supporting that field as the target rig's bone count. **[HIGH CONFIDENCE — inferred.]**

## 2. Population facts

| | |
|---|---|
| Rigs | **585** — 40 in `preload_rigs.vpp_pc` (player/NPC bodies, story characters, and **the vehicle rigs**: `heli_*`, `sp_vtol01*`…), 545 in `characters.vpp_pc` (one per character bundle). `vehicles.vpp_pc` ships none — vehicles fetch their rig from the shared preload via the vehicle-info table. |
| Sizes | 180 … 9,528 bytes |
| Bone count | 1 … **109**; median 22; p95 82 |
| Attachment array | populated in **286 / 585** (43–45 entries on full body rigs; 0 on props and most NPCs) |
| Extraction | all raw-stored; fully reliable |

## 3. File layout

Offsets from file start. Alignment is **8-byte** after the hash table (the loader aligns the *absolute* address; file buffers are 16-aligned, so file offsets behave the same).

| Region | Position | Content | Confidence |
|---|---|---|---|
| Header | `+0x00`–`+0x4F` | 80 bytes, copied verbatim by the loader (§3.1) | **[CONFIRMED — disassembly.]** |
| **Hash table** | `+0x50`, `bone_count × 4` | one `u32` per bone, in bone order — the lowercased-name hash (§5) | **[CONFIRMED — empirical, 22,274/22,274.]** |
| *(pad)* | to 8-byte alignment | | **[CONFIRMED — disassembly.]** |
| **Bone array** | pad end + header `+0x40` (observed 0), `bone_count × 0x28` | §4 | **[CONFIRMED.]** |
| **Attachment array** | bone-array end + header `+0x48` (observed 0), `second_count × 0x40` | §6 | **[CONFIRMED.]** |
| **Name region** | immediately after both arrays | null-terminated names; both arrays' first dword is an offset into here | **[CONFIRMED — empirical, 34,754/34,754 offsets resolve.]** |

### 3.1 Header fields

| Offset | Content | Confidence |
|---|---|---|
| `+0x24` | **Bone count** (`u32`) | **[CONFIRMED — the original one-sample cross-match, now 585 rigs.]** |
| `+0x2C` | An index into the bone array (`< bone_count` in 585/585). Points at `head` in 260/260 of the rigs with no attachment array **[⚠ these 260 are the head rigs (`T = 3`, §13.2); 585 − 286 = 299 rigs have no attachment array]**, and at `l-finger1` (141) / `l-hand` (45) in the full rigs — consistently **the first bone of a "detail" section** (face bones; finger bones). | **[CONFIRMED — a valid bone index; ~~HYPOTHESIS — a bone-LOD boundary (primary-bone count), see §7.~~ ADVANCED 2026-09-20, §13.2: `+0x28 + +0x2C = bone_count` in 585/585; no runtime reader found; the LOD reading is UNSUPPORTED.]** |
| `+0x30` | **Attachment count** (`u32`) | **[CONFIRMED — disassembly + empirical.]** |
| `+0x38` | Offset, relative to `+0x50`, fixed up to a pointer — **equals the hash table in 585/585** (value `0`) | **[CONFIRMED — disassembly + empirical.]** |
| `+0x3C` `+0x44` `+0x4C` | Runtime pointer-high/aux slots, zeroed by the loader | **[CONFIRMED — disassembly.]** |
| `+0x40` | Bone-array offset relative to the aligned hash-table end (`0` observed; `-1` = none) | **[CONFIRMED — disassembly.]** |
| `+0x48` | Attachment-array offset relative to the bone-array end (`0` observed; `-1` = none) | **[CONFIRMED — disassembly.]** |
| `+0x28` | *(added 2026-09-20, §13.2)* **Count `T` of the trailing bone group `[K, bone_count)` where `K = +0x2C`; `bone_count = +0x28 + +0x2C` in 585/585.** `T` = 45 in 281 rigs, 3 in 260, 1 in 37, six other values in one or two each. No runtime reader found | **[CONFIRMED — empirical; consumer OPEN.]** |
| `+0x00`–`+0x23`, `+0x34` | *(added 2026-09-20, §13.3)* zero in 585/585 on disk; at runtime `+0x00`–`+0x1F` is overwritten with the rig's registered name and `+0x20` becomes a flag word | **[CONFIRMED — empirical + disassembly.]** |
| others | not characterized | **[OPEN.]** |

## 4. Bone record (`0x28` = 40 bytes)

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | Name offset into the name region (`-1` = none); fixed up to a pointer | **[CONFIRMED — 22,274/22,274 resolve.]** Common names: `spine2`, `head`, `neck`, `l-eye`, `r-eye`, `pelvis`, `root`, `l-thigh`, `r-thigh`, `l-calf`, `r-calf`, `spine`. |
| `+0x04` | Runtime slot, zeroed by the loader | **[CONFIRMED — disassembly.]** |
| `+0x08` | **Rest position, `vec3 f32`, model-space** — root bones have median magnitude 0.30, child bones 1.18 (p95 1.76 ≈ head height). Parent-relative offsets would make children *smaller* than roots; these are larger. **Upgraded to CONFIRMED 2026-09-11 by a direct test rather than that indirect argument** — accumulating the vector down the parent chain (the parent-relative reading) scores a median residual of **3.04** against **0.126** for using it as stored, when matched against the mesh vertices actually weighted to each bone (§8.1). | **[CONFIRMED — empirical.]** |
| `+0x14` | **⚠ NOT a rotation — corrected 2026-09-11 (§4.1). It is the offset from this bone back to its parent: `pos(parent) − pos(bone)`**, with the origin standing in when there is no parent (so a root's value is `−pos(bone)`). **Exact on the whole population: 20,564 / 20,564 child bones and 1,710 / 1,710 roots**, median residual exactly `0.0`, maximum `3.8e−06` (float32 rounding). Fully derivable from `+0x08` and `+0x20` — a precomputed convenience, not independent data. **Consumer note (2026-09-20, §13.5): the runtime pose code reads *this* field (`FUN_004c21a0`, `FUN_004be9f0`); `+0x08` is read only on an attachment un-posed path (`FUN_004d0fe0`).** *(Previously recorded here as a rest rotation on the strength of "every component within `±π`" — see §4.1 for why that was not evidence.)* | **[CONFIRMED — empirical, exact, whole population.]** |
| `+0x20` | **Parent bone index**, `-1` = root. Parent < child in 22,274/22,274; every bone reachable from a root in 585/585; genuine fan-out (2–7 children share a parent), so this is the parent link and not a list pointer. Rigs have 1 root (300) or several (275 — typically 5, e.g. vehicles / multi-root props). | **[CONFIRMED — empirical.]** |
| `+0x24` | **`-1` in every shipped bone** (22,274/22,274). A runtime slot. | **[CONFIRMED — the on-disk value; ~~OPEN — its runtime meaning.~~ ADVANCED 2026-09-20, §13.5: no runtime reader or writer found in the scopes of §13.6 — a bounded negative, not "confirmed unused"; still OPEN.]** *(An intermediate reading of this slot as a second hierarchy link was vacuous — it passed tree tests only because it is always `-1`.)* |

**The rest pose is therefore purely positional: (model-space position, offset-to-parent, parent index). The bone record contains no orientation of any kind** — all 40 bytes are accounted for, and none of them is a rotation. No quaternion and no 4×4 matrix either (both searched for at every 4-byte offset; quaternion hit rate 1.2% vs 3.0% at random offsets). **[CONFIRMED.]**

**Practical consequence for anyone posing a mesh:** composing bind transforms as pure translations is the *correct* model, not an approximation to be revisited later. Orientation for bones is simply not in this file. Where orientation *does* live: the attachment/IK array carries ~~real quaternions (§6, record `+0x20`, within 10% of unit in 12,473/12,480)~~ **[corrected §13.4: a 3×3 rotation at record `+0x08` plus a translation at `+0x2C`, §6]**, and anything animating bones must get its rotations from `.anim_pc`.

### 4.1 How this field was misread for two sessions, and what the tell was

The row above previously read "**rest rotation, three `f32`**, every component within ±π in 22,157 / 22,274." That observation is true and it is **worthless as evidence of type**:

> **A range check is not a type check.** Bone offsets measured in metres fall inside ±3.14 for exactly the same mundane reason angles in radians do — limbs are short. The "within ±π" statistic never distinguished the two hypotheses; it only ever confirmed that the numbers were small.

**The disconfirming evidence was already written down and not followed.** The same row recorded that the 117 out-of-range exceptions were "all in vehicle rigs (`sp_vtol01`, `heli_*`)." Under the rotation reading that is an unexplained curiosity. Under the correct reading it is the answer: those are **large vehicles whose bone offsets genuinely exceed 3.14 metres**. An exception list that clusters perfectly by physical size is a statement about units.

**What settled it was a symmetry test that needed no convention at all.** Left/right bone pairs mirror as `(−x, y, z)` in **5,958 / 6,760** pairs — the same signature the *position* field shows (5,890 / 6,760). That is how an ordinary (polar) vector transforms under a reflection. A rotation vector or an Euler triple is a **pseudovector** and mirrors the other way round: the component about the mirror normal is preserved and the other two negate. **One test ruled out every rotation convention simultaneously, before any of them had to be enumerated.**

*(For the record, the enumeration was run first — 12 Euler orders × absolute/parent-composed × 6 canonical axes, scored against the direction from each bone to its children. Best result 33.7° against a shuffled-assignment control of 40.1°, with the leading candidates disagreeing on both composition order and axis. That is noise, and it was reported as inconclusive rather than resolved — had the 33.7° been taken as a result, it would have produced a confidently-stated Euler order that is entirely fictional.)*

## 5. The hash table

Each entry is the hash of the bone's **lowercased** name under the algorithm `spec-vpp-container.md` §2.2 describes for archive filenames (accumulate: rotate the 32-bit hash left by 6, XOR in the character) — since the foliage pass also read directly in disassembly at `FUN_00da7890`, which performs the lowercase fold itself:

| Variant tested | Matches |
|---|---|
| lowercased name | **22,274 / 22,274** |
| name as stored (mostly already lowercase) | 21,634 / 22,274 |
| uppercased name (control) | **0 / 22,274** |

**[CONFIRMED — empirical.]** The table is **not sorted** (ascending in 6/585), ~~so lookup is a linear scan by hash in bone order.~~ Header `+0x38` resolves to it, and ~~the bone-by-name routine the vehicle loader calls for every part name and for `camera`/`camtarget`/`camFOV`/`camDOF` (`spec-vehicle-geometry.md` §6) is its consumer — **[HIGH CONFIDENCE — inferred from the call pattern; the routine itself (`FUN_004bc880`) was not decompiled this pass.]**~~ **[REFUTED 2026-09-20 — §13.1: `FUN_004bc880` was decompiled; it is a case-insensitive string compare over the bone names and never reads this table. No runtime reader of the hash table was found (bounded: §13.1, §13.6).]** Practical consequence: **one hash function serves archive filenames and bone names** — the same one. *(Scope correction, 2026-09-10: that does **not** make it the engine's only string hash. A table-driven CRC-32 also exists and keys the mission-conversation registry; `.ctdg_pc` uses both at once. See `spec-conversation-format.md` §6.)*

## 6. Attachment / IK-target record (`0x40` = 64 bytes)

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | Name offset into the name region | **[CONFIRMED — 12,480/12,480 resolve.]** |
| `+0x04` | Runtime slot, zeroed | **[CONFIRMED — disassembly.]** |
| `+0x10` | ~~16 bytes: reads as a loosely-unit quaternion in 90% and as a unit `vec3` in 70%~~ **RESOLVED 2026-09-20 (§13.4): the last float of rotation row 0 and all of row 1 (rows at `+0x08` / `+0x14` / `+0x20`); no quaternion** | ~~**[OPEN — which; a second orientation or a direction.]**~~ **[CONFIRMED — disassembly + empirical, 12,480/12,480.]** |
| `+0x20` | ~~**Quaternion** — within 10% of unit norm in **12,473 / 12,480** (exactly unit in 72%; the rest 1.00–1.07 — stored without renormalization)~~ **RETRACTED 2026-09-20 (§13.4): this is rotation row 2 (`+0x20`–`+0x28`, a unit vector in 12,480/12,480) plus the translation's `x` at `+0x2C`; the 4-float "norm" was near 1 because `x` is usually small** | ~~**[CONFIRMED — empirical.]**~~ **[WITHDRAWN — a real measurement, mis-typed.]** |
| `+0x30` | A `f32`: `0.0` (54%), `-0.0` (14%), small values otherwise. **RESOLVED 2026-09-20 (§13.4): the translation's `y` (translation = `+0x2C`, `+0x30`, `+0x34`); zero-translation is exactly the same-name-as-parent attachments** | **[CONFIRMED — a float; ~~OPEN — meaning (a weight or limit).~~ meaning CONFIRMED, disassembly + empirical.]** |
| `+0x08` | *(added 2026-09-20, §13.4)* **3×3 rotation matrix, nine `f32`, rows at `+0x08` / `+0x14` / `+0x20`** — orthonormal, determinant +1 in 12,480 / 12,480 (shifted-offset controls 0 / 12,480). Row 0 of the same-name attachments points along the bone (71% vs 4.5% control): these are the per-bone orientation frames | **[CONFIRMED — disassembly + empirical.]** |
| `+0x2C` | *(added 2026-09-20, §13.4)* **Translation, `vec3 f32`** — bone-local (median `|t|` 0, p95 0.44) | **[CONFIRMED — disassembly + empirical.]** |
| `+0x38` | **Parent bone index** — `< bone_count` in **12,480 / 12,480**; read by the attachment-transform getter `FUN_004d0fe0` | **[CONFIRMED — empirical + disassembly.]** |
| `+0x3C` | *(added 2026-09-20, §13.4)* **Signed int tag, `-1` in 12,480 / 12,480**; read only by the tag-filtered by-name lookup `FUN_004d0e70` | **[CONFIRMED — disassembly + empirical.]** |
| others | not characterized | **[OPEN.]** |

**What they are:** 68% are named identically to their parent bone (`head`, `l-hand`, `l-foot`…) — per-bone attachment points. The remainder are explicit targets: `hsattach` on `spine2` (a holster attach point), `l_/r_elbow_target{a,b,c}` on the upper-arm twist bones and `l_/r_knee_target{a,b,c}` on the thighs (IK pole targets, three per joint). **[HIGH CONFIDENCE — inferred from the literal names and their parents.]** Only full body rigs carry them (43–45 each); props and most NPC rigs have none.

## 7. `+0x2C` — the honest state

It is always a valid bone index, and the bone it names is remarkably consistent within each rig class: the first head/face bone in no-attachment rigs, the first finger bone in full rigs, `steering_wheel` in the six vehicle rigs that have it. ~~That pattern fits **"index where the secondary/detail bones begin"** — a bone-LOD split, letting distant instances skip face and finger bones. It does not fit any simple function of the two counts (tested: `bone_count−1`, `bone_count−second_count`, `±1` variants — none hold across the population). **[HYPOTHESIS.]** It is neither read nor fixed up by the loader function traced here, so it is consumed elsewhere.~~ **[2026-09-20 — ADVANCED, §13.2: it *does* fit a simple function of two header counts — `bone_count = +0x28 + +0x2C` in 585/585, the sum tested in none of the variants above. But no runtime reader of either field was found in the scopes of §13.6, so the bone-LOD reading has no code behind it (UNSUPPORTED, not refuted); and the population says the boundary is `bone_count − T` with `T` a class constant (45 for humanoids) rather than "the first detail bone" by name (`l-finger1`/`l-hand` in only 186 / 281 humanoids).]**

## 8. Cross-format consistency

- `.anim_pc` header `+0x0B` **is the target rig's bone count — confirmed by per-character pairing** (`plym`/`plyf` clips → 69 = `cm_body`/`cf_body`; `brute` → 68 = `brute_flamethrower`; `gangf` → 77; `pml1`/`pfl1` → 65 = `jones`; `spec-anim-format.md` §2). Its sibling `+0x0A` is the *animated subset* of those bones (equal to `+0x0B` on prop rigs), **not** the attachment count — the earlier ≈45 coincidence was just that. Clips carry **no bone-name hashes** (2 / 4,209, chance), so animation binds to bones by **index**, and this rig's bone order is what that index means — whether the payload's tracks are laid out in that order is still open (`spec-anim-format.md` §6a).
- Vehicles (`spec-vehicle-geometry.md` §6) resolve every part name through this rig's ~~hash table~~ **bone-name strings — a case-insensitive string compare, not the hash table (corrected 2026-09-20, §13.1)**; the four camera bones are ordinary named bones here.
- ~~The vehicle rigs are where the rotation-triple exceeds ±π — plausibly rotor/spin bones stored beyond one turn.~~ **RESOLVED 2026-09-11 (§4.1), and it was the clue that broke the field open:** the field is a parent-relative offset in **metres**, so the "exceptions" are simply the rigs whose bones sit more than 3.14 m apart — large vehicles. Nothing to do with turns. **[CONFIRMED.]**

### 8.1 Mapping a rig into mesh space (added 2026-09-11)

~~**The transform is `mesh = (rig.x, −rig.y, −rig.z)` — a 180° rotation about the X axis. No scale, no translation.** **[CONFIRMED — empirical, consistent across six characters.]**~~

**⚠ RETRACTED for skinning specifically, 2026-09-13 — see §11.15.** This formula is only correct as a *centroid-distance-minimizing fit* over the wrong index mapping (direct blend-index-as-bone-index); it was never independently checked against the real per-vertex decode. The actual rig→mesh transform used by skinning is **`mesh = (−rig.x, −rig.y, −rig.z)`** — a full point inversion (`diag(−1,−1,−1)`, determinant −1, not a rotation), confirmed against the shipped `bone_palette`/`meshSpaceInversionMatrix` mechanism (Team B, `src/bone_palette.cpp`) and independently reproduced on this side (§11.15, 171/172 discrete side-match rate under the corrected sign, pooled over six meshes — see below). All three axes negate, not just Y and Z. Which hand the *game* calls "left" versus "right" is a separate naming question, unaffected by this correction, since `−I` commutes with any rotation.

~~**Blend indices in the mesh address this bone array directly.** There is no palette indirection **on the path a vertex's blend index travels** — ⚠ **qualified 2026-09-13, see §11.4:** an indirection table *does* exist, both in the engine (the render library's palette gather substitutes `source[table[i]]` per slot) and in the mesh file (the flags-bit-1 byte array at Mesh header `+0x38`, which is the ascending list of rig bones this mesh may be skinned to). It is simply **not** what the blend index resolves through: routing the blend index via that array scores a left/right side match of **0 / 87** against this reading's **68 / 78** and a shuffled control's **40 / 80**. The claim in this paragraph stands and is strengthened; the words "no palette indirection" were too broad. Candidate constant offsets from −6 to +6 were tested with the axis transform re-fitted independently for each; shift 0 with `+X −Y −Z` is the consistent winner (medians 0.080–0.144 across six characters), and the two characters where another shift scored marginally lower disagree with each other on both the shift and the axis mapping, i.e. noise. **[CONFIRMED — empirical.]**~~

**⚠ RETRACTED 2026-09-13, see §11.15 — the same-day qualification above was itself wrong, in a subtle and instructive way.** The Mesh-header-`+0x38` array **is** exactly what the blend index resolves through — it is a genuine bone-palette table (character meshes specifically; not tested for the other five carriers that share this Mesh sub-block, `spec-geometry-format.md` §4.1.1). The `0 / 87` side-match score that seemed to refute it was real, but it was measuring the palette reading under the **wrong** sign convention (the `+X −Y −Z` transform just retracted above, two paragraphs up) — under the correct transform (`−X −Y −Z`, a point inversion), the palette reading scores **171 / 172 (0.9942)** on the identical predicate, against a shuffled control at **0.5364** and the direct-index reading at **0.2566** under the same corrected sign (§11.15). A `0 / 87` this close to deterministic anti-correlation, rather than the ~0.50 a genuinely wrong mapping produces against its own shuffled control, was itself a clue this session read as "refutation" rather than "sign error" — recorded in `HANDOFF.md` §5 as a methodology lesson. The shift-search in this paragraph is undermined by the same root cause: it could only ever find a **constant offset**, and a bone-palette lookup is a **permutation**, a category of error that search was structurally incapable of detecting no matter how many shifts it tried. *(The highest blend index seen anywhere in the population is 63, against rigs of 66–68 bones.* ~~*That resembles a 64-entry palette limit and is not one — the high-numbered bones are helpers that nothing is skinned to.*~~ ⚠ **REFUTED 2026-09-13, see §11.2: it IS a 64-entry limit.** One of the engine's two palette load paths clamps the bone count to `0x40` = 64 outright (`FUN_00e649f0`, raw at `0x00e64a2f`). The observation that the high-numbered bones are helpers nothing is skinned to remains true and is separately confirmed — §11.4 names them — but it is not why the maximum is 63.)

**How this was measured, because the obvious method fails.** Comparing the rig's bounding box against the mesh's does not work: a rig contains **helper bones deliberately placed outside the body**, so its box is not the body's box. On `brad` the rig's Z extent is 1.126 against the mesh's 0.404, which looks like a scale or an axis remap and is neither — **it is one bone**, index 47, named `camera`, at `(0, 0, 1.000)` with no parent, sitting a metre in front of the character. Excluding it, the rig's Z matches the mesh's.

The method that does work uses the blend indices as the correspondence: take the centroid of the vertices weighted almost entirely (≥ 245/255) to a single bone, and match that against that bone's rest position, fitting only a translation. This converts a vague shape-matching problem into a per-bone assertion that can be scored, and it comes with a natural control — shuffling which bone owns which centroid, which scores **0.789** against the true assignment's **0.126**.

⚠ **The residual is now known to be partly real error — see §11.4 (2026-09-13).** A discrete left/right test finds **10 of 78** lateral blend-index slots resolving to a bone on the wrong side of the body under this reading, and on two body meshes the slot with the most dominantly-weighted vertices (542 and 667 vertices, on the midline) resolves to `r-toe0`. The magnitude story below is sound as far as it goes and does not cover that shape. The paragraph is kept as written because the expectation it sets for an implementer is still the right one.

**Expected residual: roughly 0.10–0.14 m, and that is not error.** A joint sits at the end of a limb segment while the skin weighted to it spreads along the segment, so the centroid is offset by about half a limb radius plus half a segment. An implementation checking itself should expect ~0.1 m, not zero. Helper bones (twist bones, `spinebend`, `handprop`) score much worse and are expected to.

## 9. Corrections to the original version of this document

1. **Bone array location.** Original: "immediately after the header at `+0x50`." Correct: a `bone_count × u32` hash table occupies `+0x50` first; the bone array follows at an 8-aligned offset. The original's byte range (`0x50`–`0xF0` on the 4-bone sample) was wrong by the table's size; it only *looked* right because names were never checked against it.
2. **The "second array".** Original: purpose unknown, "plausibly attachment points, sockets, or IK-constraint data." Now confirmed as attachment / IK-target points with names and parent-bone indices — the original guess was right in kind, and is now evidence rather than guess.
3. **`+0x2C = bone_count − 1`.** Held on the one sample; false in 548/585. Replaced by §7.
4. **Per-bone "transform data" was expected inside the 40-byte record as a quaternion / parent index pair.** Parent index: yes (`+0x20`). Quaternion: no — ~~the rotation is a float triple (§4)~~ **[corrected §4.1: there is no rotation at all; `+0x14` is the offset to the parent]**.
5. The original's core confirmed claim — `+0x24` is the bone count, cross-matched to real names — **stands**, now at 585 rigs.

## 10. Open Items

1. ~~The rotation triple's convention (`+0x14`): Euler order or axis-angle.~~ **CLOSED 2026-09-11 (§4.1) — the field is not a rotation.** It is `pos(parent) − pos(bone)`, exact on the whole population. **The bone record carries no orientation of any kind**, so there is no convention to determine; bone rotation exists only in `.anim_pc`. Oriented *attachment* points do exist — see ~~the quaternion at §6's record `+0x20`~~ **[corrected §13.4: the 3×3 rotation at §6's record `+0x08`]**.
2. ~~`+0x2C`'s true meaning (§7 hypothesis).~~ **ADVANCED 2026-09-20 (§13.2): `+0x28` (new) `+ +0x2C = bone_count` in 585/585; no runtime reader of either field found in the bounded scopes of §13.6; the bone-LOD hypothesis is UNSUPPORTED. What the authoring side uses them for stays OPEN.**
3. ~~Bone `+0x24` and attachment `+0x10`/`+0x30`/`+0x3C`… runtime vs on-disk meaning.~~ **Attachment fields RESOLVED 2026-09-20 (§13.4): 3×3 rotation at `+0x08`, translation at `+0x2C`, parent at `+0x38`, tag `-1` at `+0x3C`. Bone `+0x24`: no runtime reader or writer found (§13.5, a bounded negative) — remains OPEN.**
4. ~~Header fields other than those in §3.1.~~ **ADVANCED 2026-09-20 (§13.2–§13.3): `+0x28` characterized; `+0x00`–`+0x23` and `+0x34` are zero in 585/585 (runtime name/flag overlay). No uncharacterized header dword remains.**
5. ~~The bone-by-name routine (`FUN_004bc880`) — assumed the hash table's consumer, not decompiled.~~ **RESOLVED 2026-09-20 (§13.1): decompiled — a case-insensitive string compare over the bone names, not a hash-table consumer; all 61 call sites enumerated.**
6. ~~`"Customization_Rig"` (registration type 13) — a separate constructor on the same extension, still untraced (`spec-format-inventory.md`).~~ **RESOLVED 2026-09-18, see §12 — stash-only into the customization singleton (`DAT_0263e0f4` `+0x1a8`/`+0x1ac`), same family as ID 11/12's morph stashes; does not parse `.rig_pc` at all.**
7. **The runtime skinning path — partly resolved 2026-09-13, see §11.** Located end to end: the blend happens in a Direct3D 9 vertex shader, from a palette of 48-byte affine matrices. Still open from that pass: the shader's own arithmetic (unreadable from this side), the palette gather's bone-index limit, the conflict between the gather's addressing and the measured addressing, and the §11.4 side-match residual — each with a next step in §11.7. **[Update: the bone-index limit is resolved (§11.12.1), the addressing conflict is resolved (§11.15/§11.16), and the side-match residual is resolved (§11.17); the shader's own arithmetic (§11.7 item 1) remains open.]**
8. Whether `.anim_pc`'s tracks are laid out in this bone order — tested as far as the population allows (§8, `spec-anim-format.md` §6a): binding is by index, not name, and the index map lives inside the undecoded track payload. Closable only with the runtime sampling code.

## 11. The runtime skinning consumer, and why the near-degenerate seam class is not avoided by any engine mechanism (2026-09-13)

> **Scope.** This section answers the question logged in `HANDOFF.md` §28.8: *how does
> the real engine avoid seam fragmentation at near-degenerate bind-pose vertices under
> multi-bone skinning?* It traces the skinning consumer from the render library's
> bone-palette allocator through to the Direct3D 9 vertex declaration, reads the blend
> formula's inputs, and then tests the three remaining data-side explanations against
> shipped meshes. **The headline is a negative with a mechanism behind it: the engine
> uses the same technique the implementation team does — linear blend skinning from a
> matrix palette, blended in a vertex shader — and shipped character meshes contain both
> the cross-limb blend weights and the cross-limb triangles that make the seam class
> possible. No smoothing pass, no dual-quaternion blending and no weight-painting
> convention removes it.**
>
> New Ghidra scripts `SkinStringRecon.java`, `SkinProbe1/2/3.java`, `SkinDecompList.java`,
> `SkinCallers.java`, `SkinVtable.java`, `SkinStrDump.java`, `SkinSrcPaths.java`,
> `SkinD3DSlots.java`, `SkinVCallCensus.java`, `SkinRawCalls.java`, `SkinRawGather.java`,
> `SkinDeclTables.java`, `SkinDeclSpecs.java`. New harnesses `skin_bonemap.py`,
> `skin_palette_remap.py`, `skin_seam_topology.py`, `skin_edge_stretch.py`,
> `skin_edge_stretch2.py`, `skin_edge_diag.py`, `skin_table_id.py`, `skin_table_id2.py`,
> `skin_table_id3.py`, `skin_side_match.py`, `skin_stray_weight.py`,
> `skin_chk_channels.py`.

### 11.1 The skinning consumer is a GPU vertex-shader path — located exactly

The blend is **not** performed by CPU code. Blend indices and blend weights are declared
as Direct3D 9 vertex-stream attributes and handed to a vertex shader.

The engine builds its vertex declarations in one function, `FUN_00476ca0`, which assembles
an array of 8-byte records and passes it to the device method at vtable displacement
`+0x158` — index 86 of `IDirect3DDevice9`, `CreateVertexDeclaration`. The records are
`D3DVERTEXELEMENT9`: a `u16` stream index, a `u16` byte offset, then four bytes holding
type, method, usage and usage index. The function translates each element through three
static tables, and the tables are what settle the question:

| Table | Address | Entries | Content |
|---|---|---|---|
| Usage | `0x01351d58` | 51 × `u32` | semantic code → `D3DDECLUSAGE` |
| Usage index | `0x01351c64` | 51 × `u8` | semantic code → `UsageIndex` |
| Type | `0x01351c98` | 17 × `u32` | encoding code → `D3DDECLTYPE` |
| Element size | `0x01351d10` | 17 × `u32` | encoding code → bytes per element |

The usage table reads, exhaustively:

| Semantic codes | `D3DDECLUSAGE` value | Meaning |
|---|---|---|
| `0`–`7` | `0` | `POSITION`, usage index 0–7 |
| `8`–`15` | `5` | `TEXCOORD`, usage index 0–7 |
| `16`–`23` | `3` | `NORMAL`, usage index 0–7 |
| `24`–`31` | `6` | `TANGENT`, usage index 0–7 |
| `32`–`39` | `7` | `BINORMAL`, usage index 0–7 |
| `40`–`47` | `10` | `COLOR`, usage index 0–7 |
| **`48`** | **`2`** | **`BLENDINDICES`** |
| **`49`** | **`1`** | **`BLENDWEIGHT`** |
| `50` | `4` | `PSIZE` |

**[CONFIRMED — disassembly.]** The usage-index table mirrors that structure exactly,
cycling `0…7` within each group of eight and reading `0` for the three singletons — an
independent corroboration that the grouping above is the real one and not an artefact of
how the table was read.

Three further checks, all of which could have failed:

- The **element-size table matches the correct Direct3D 9 byte size of every one of the 17
  types the type table names — 17 / 17** (`FLOAT1` 4, `FLOAT2` 8, `FLOAT3` 12, `FLOAT4` 16,
  `FLOAT16_2` 4, `FLOAT16_4` 8, `UBYTE4` 4, `UBYTE4N` 4, `SHORT2N` 4, `SHORT4N` 8,
  `SHORT2` 4, `SHORT4` 8, `UBYTE4N` 4, `UBYTE4N` 4, `D3DCOLOR` 4, `SHORT4` 8, `UBYTE4` 4).
  Two tables written independently of each other agreeing on all 17 rows is not something a
  misread offset produces.
- The terminator constant at `0x01351c5c` is byte-exact `D3DDECL_END()`:
  `FF 00 00 00 11 00 00 00`, i.e. stream `0xFF`, offset `0`, type `17`
  (`D3DDECLTYPE_UNUSED`), method/usage/usage-index `0`.
- `FUN_00476ca0` validates each incoming element against the table bounds before using it
  — semantic code `< 0x34`, encoding code `< 0x12`, stream `< 8` — which is where the
  51-entry and 17-entry table sizes above come from rather than being guessed. Reading one
  entry past either table returns obvious garbage (`0x129e53c`), confirming the bound.

**[CONFIRMED — disassembly, all of the above.]**

**Consequence.** `spec-vertex-format.md` §6.3 confirmed from file data that a skinned
vertex carries four blend weights and four blend indices inside the interleaved vertex
element. Those are the bytes that semantic codes 49 and 48 declare to the GPU. **The
blend arithmetic therefore happens in the vertex shader, and this project's disassembly
cannot read it** — the shaders live in `shaders.vpp_pc`, a mode-(a) container to which the
entry-0-only rule applies (`spec-fxo-format.md` §5, `HANDOFF.md` §5). **That boundary is
where a CPU-side trace necessarily stops, and it is reached here rather than assumed.**
**[CONFIRMED — disassembly for the handoff point; OPEN / UNKNOWN for the shader body.]**

### 11.2 The palette the shader is given: 48-byte affine matrices, one per bone

The render library allocates a per-instance bone palette whose element is **`0x30` = 48
bytes**, i.e. three `float4` rows of a 3×4 affine matrix — the standard Direct3D 9
skinning-constant layout, three constant registers per bone.

| Fact | Evidence | Confidence |
|---|---|---|
| Allocation is `bone_count × 0x30` bytes, 16-aligned | `FUN_00e7b170`; raw at `0x00e7b1c9` computes `EAX×3` then shifts left 4 | **CONFIRMED — disassembly** |
| Accepted bone count is `1 … 256` | `FUN_00e651c0` tests `n − 1 < 0x100` | **CONFIRMED — disassembly** |
| Two backing pools, chosen at a threshold of **40** bones | `FUN_00e7b170` raw at `0x00e7b1a3`: `CMP EAX,0x28`, then one of two pool objects | **CONFIRMED — disassembly** |
| Palette record: `+0x00` reference count, `+0x04` live count, `+0x08` capacity, `+0x0C` matrix pointer, `+0x10` a flag byte cleared after every load | `FUN_00e7b170` (construct), `FUN_00e7ae90` (add-ref), `FUN_00e7b230` (release), `FUN_00e7ad30` / `FUN_00e7aff0` (load) | **CONFIRMED — disassembly** |
| The out-of-range fallback matrix at `0x01321424` is an **affine identity** | bytes are exactly `1,0,0,0 / 0,1,0,0 / 0,0,1,0` as `f32` | **CONFIRMED — disassembly** |
| The subsystem's own static block is tiny — the union of referencing sites over `0x013336c0 … 0x01333760` is **8** | data-side enumeration (`SkinProbe2.java`) | **CONFIRMED — disassembly** |

**Dual-quaternion skinning is refuted at the palette level.** A dual-quaternion palette
carries **32** bytes per bone (two `float4` quaternions) and its identity element has a
single `1.0` in one of eight lanes. This palette carries **48** bytes per bone and its
identity has `1.0` at float lanes **0, 5 and 10** — the diagonal of a row-major 3×4
matrix, and a pattern no quaternion-pair layout can produce. **[CONFIRMED — disassembly
for the element size and the identity's byte pattern; HIGH CONFIDENCE — inferred that the
shader consequently performs a weighted sum of matrix-transformed positions, since the
shader bytecode itself was not read (§11.1).]**

**The 64-entry palette has a code literal behind it now, not only a data inference —
but read the scope at the end of this paragraph before quoting it.** The palette
load path that takes no per-mesh table **clamps the bone count to `0x40` = 64** —
`FUN_00e649f0`, raw at `0x00e64a2f`: `CMP EAX,0x40`, and on `>=` the value is replaced by
`0x40` before the copy runs. `spec-vertex-format.md` §6.3 recorded "the largest
non-sentinel blend index observed is 63, consistent with a 64-entry bone palette" as
**CONFIRMED — empirical** for the value and **HIGH CONFIDENCE — inferred** for the palette
size. **The 64 is now a literal in the code rather than an inference from the data.**
Stated with its scope, because the two paths differ: the clamp sits on the **no-table**
load path only, and the allocator itself accepts up to 256 bones (§11.2 table), so the
right reading is that 64 is a real bound the engine enforces on at least one path and
that no shipped blend index exceeds 63 — not that no palette can ever be larger.
**[CONFIRMED — disassembly for the `0x40` clamp on that path; HIGH CONFIDENCE —
inferred that 64 is the palette size the authoring pipeline targets.]**

### 11.3 The palette load, read at instruction level

Two load paths exist, selected by whether the mesh supplies a bone index table.

**Straight copy**, `FUN_00e7ad30(palette, source, count)`: copies `count` × 48 bytes when
`count` does not exceed the palette's capacity, then clears the flag byte at `+0x10`.

**Gather**, `FUN_00e7aff0`: the destination slot `i` receives `source[table[i]]`, and
receives the **identity** matrix instead whenever `table[i]` is at or beyond a limit.

The decompiled argument list for the gather is not trustworthy — this is the register-passed
argument hazard `HANDOFF.md` §5 already records — so the signature was pinned from raw
instructions at `0x00e7aff0`–`0x00e7b060`:

- the receiver register holds the palette;
- **argument 1** is the source matrix array base (read back at `0x00e7b013`);
- **argument 2** is the bone-index limit (compared at `0x00e7b027`);
- **argument 3** is the table structure: a `u16` element count at its `+0x00` and a pointer
  to a **byte** array at its `+0x08` (read at `0x00e7b020`);
- the index is scaled by 48 (`LEA`/`SHL` at `0x00e7b030`) and 48 bytes are moved per slot.

**[CONFIRMED — disassembly, raw instructions.]**

Both call sites were read raw as well. `FUN_00e649f0` reaches the table through three
dereferences from the skinned instance (`+0xbc`, then `+0x30`, then `+0x38`), takes the
gather path only when that `u16` count is non-zero, and otherwise falls through to the
straight copy with the 64-bone clamp. `FUN_00b91a10` calls the gather with the source
matrix array and the limit fetched from the animation subsystem's instance registry
(`FUN_004be9c0` returns the registry record's `+0x00`; `FUN_004be910` returns the value at
`+0x04` of the object at the record's `+0x3c`; the registry has stride `0x3a0` and
validates a self-index at `+0x50`). **[CONFIRMED — disassembly.]** The runtime *value* of
the limit is **OPEN / UNKNOWN** — see §11.7.

### 11.4 The flags-bit-1 byte array in the Mesh sub-block is ~~**not**~~ **[RETRACTED, see §11.15 — it IS]** a blend-index remap, and reads as the mesh's skinnable-bone list (inferred, and now also CONFIRMED as the actual bone palette)

**⚠ This section's central verdict is RETRACTED, 2026-09-13 — see §11.15 for the full account.** Every measurement below is left as originally recorded, because every number in it is real and was correctly computed — **what was wrong was the interpretation, not the data.** Tests 1 and 2 scored the array reading against the rig's rest positions using the `(+X,−Y,−Z)` transform §8.1 published — which is now known to be wrong specifically for skinning (§8.1, §11.15: the real transform is `(−X,−Y,−Z)`, a point inversion). Test 3 compared raw mesh-space X against raw rig-space X with **no transform applied at all**, relying on an assumption ("X is lateral in both spaces, same sign") that is *also* falsified by the same correction. Once the sign is fixed, the array reading is not merely rescued — it wins decisively: **171 / 172 (0.9942)** on Test 3's own predicate, against a shuffled control at 0.5364 (§11.15). The `0 / 87` this section reported was never noise; it was the signature of a real, correct mapping scored under an exactly-inverted sign, and this section's own text below says as much ("perfect anti-correlation") without drawing the conclusion that should have followed from it. This is recorded in `HANDOFF.md` §5 as a methodology lesson: **a search that can only find a constant offset cannot rule out a permutation**, no matter how many offsets it tries — Tests 1–3 collectively tried offsets, reversals, and a re-ordering, but never a corrected sign convention, which is the actual defect.

The gather's table has the same shape as an array the `.ccmesh_pc` Mesh sub-block already
carries: `spec-vertex-format.md`'s walk advances the cursor by the `u16` at Mesh header
`+0x38` in **bytes** when flags bit 1 is set (that advance is what places the draw-range
group records correctly in 1,831 / 1,831 meshes). Measured over every character mesh in
`characters.vpp_pc`:

| Measurement | Population | Result |
|---|---|---|
| Validated Mesh sub-blocks | 428 c/g pairs | **428 / 428** |
| Blocks with exactly one channel, layout code 3 | 428 validated blocks | **428 / 428** (so no multi-channel vertex-count trap applies to anything below) |
| Flags bit 1 set | 428 validated blocks | **428 / 428** |
| `u16` at Mesh header `+0x38` non-zero | 428 validated blocks | **428 / 428** |
| The byte array is strictly ascending | 428 arrays | **428 / 428** |
| Array length equals the number of **distinct blend indices used** by the block's vertices | 428 blocks | **428 / 428** |
| The distinct blend indices used are exactly `0 … length−1`, densely | 428 blocks | **428 / 428** |

**[CONFIRMED — empirical.]** The rig-bone indices the array omits are, by name, exactly
the bones nothing can be skinned to. On two full body meshes the omitted set has **12**
members in both cases: `root`, `l-upperarmtwist1`, `r-upperarmtwist1`, `l-handprop`,
`r-handprop`, `spinebend`, `camtarget`, `camera`, `camfov`, `camdof`, plus `l-breast` /
`r-breast` on one and `l-eye` / `r-eye` on the other. **So the array is the ascending list
of rig bones this mesh may be skinned to.** **[HIGH CONFIDENCE — inferred, from the names
of the omitted indices plus the exact length match above.]**

~~**The tempting conclusion from that — that a vertex's blend index is a palette slot which
this array maps to a rig bone — is REFUTED.**~~ **[RETRACTED, §11.15 — this conclusion is CORRECT, not refuted; see the banner above.]** It matters because it would have been a
ready-made explanation for the implementation team's artefact (vertices bound to the wrong
bones), so it was tested three ways rather than adopted — the tests below just used the wrong sign convention throughout, which is why they failed to confirm something true.

**Test 1 — `spec-rig-format.md` §8.1's own centroid method, unchanged.** Per bone, the
centroid of the vertices weighted ≥ 245/255 to it; the confirmed `(+X, −Y, −Z)` axis map
applied to the rig's rest positions; only a translation fitted; median per-bone distance
reported. Over **232 character meshes** that pair with a rig and yield at least four
scoreable bones:

| Reading | Median per-bone residual |
|---|---|
| **blend index = rig bone index** (the published reading) | **0.1653 m** |
| blend index → `+0x38` array → rig bone | 1.1938 m |
| *control* — the same array **reversed** (right shape, wrong content) | 0.9576 m |
| *control* — bone/centroid assignment shuffled | 0.8187 m |

Per mesh: the published reading is better in **219 / 232**, the array reading in 13 / 232.
The array reading is worse than the shuffled control, which is what a *systematic* offset
does and a random assignment does not.

**Test 2 — the same measurement with the translation fitted from the component-wise
median instead of the mean**, because a handful of slots landing on distant helper bones
can move a mean-fitted translation and so inflate both readings. Published reading
**0.0875 m**, array reading **1.2584 m**, shuffled control 0.6831 m on one mesh; 0.0969 /
1.1912 / 0.8291 on a second. The gap survives the more robust fit.

**Test 3 — a discrete predicate with a real chance rate, because a distance median cannot
say which slots disagree.** The rig's own left/right mirror signature is `(−x, y, z)`
(§4.1), so `X` is the lateral axis in both spaces. For every blend-index slot whose vertex
cluster is clearly lateral (|mean X| > 4 cm) and whose candidate bone is clearly lateral
(|bone X| > 4 cm), the two signs must agree. Guessing scores 0.500. Pooled over three full
body meshes:

| Reading | Side match | Rate |
|---|---|---|
| **blend index = rig bone index** | **68 / 78** | **0.872** |
| blend index → `+0x38` array → rig bone, at its natural alignment | **0 / 87** | **0.000** |
| the same array read at eight other byte alignments, `−4 … +4` | best 56 / 73 | ≤ 0.767 |
| the array read assuming the two gated arrays are in the opposite order | 25 / 75 | 0.333 |
| *control* — shuffled | 40 / 80 | 0.500 |

~~**[CONFIRMED — empirical.]** **`spec-rig-format.md` §8.1's claim that blend indices
address the bone array directly therefore stands, and now has a second, discrete,
chance-rated control behind it rather than only a distance residual.**~~ **[RETRACTED, §11.15.]** The `0 / 87` is
in fact *exactly* what should have been read as a result rather than a failure at the time: it is *perfect anti-correlation*, and this document said so in the very next sentence without drawing the obvious next conclusion — that a correct mapping scored under an exactly-inverted sign produces exactly this signature, not the "displaced by one position" story that follows. **This is the single clearest teaching moment in this project's own `HANDOFF.md` §5**: the correct diagnosis was written down, in these words, one sentence before the wrong verdict.

~~**One residual, recorded rather than explained away.** The published reading mismatches
**10 of 78** lateral slots, and on two meshes the slot with the most
dominantly-weighted vertices (542 and 667 vertices, mean `X` −0.033 and 0.000, i.e. on the
midline) resolves under it to `r-toe0`. A midline cluster of several hundred rigidly-weighted
vertices is not a toe. §8.1 currently absorbs its 0.10–0.14 m residual as "about half a
limb radius plus half a segment" and notes that helper bones "score much worse and are
expected to"; that story cannot also cover a midline/limb confusion. **[OPEN — the
published reading is right in the large and wrong on a minority of slots; neither the
`+0x38` array nor any byte alignment of it is the correction.]**~~

**This residual is very likely the visible symptom of the exact bug retracted above, though not yet re-measured to confirm it directly (§11.15's open item).** A midline cluster resolving to `r-toe0`, and 10 of 78 lateral slots disagreeing under a mapping now known to compound two errors (direct indexing *and* the wrong sign), is exactly the shape a real palette+sign bug would leave as residue in a test that happened to score well overall by accident. **Credit due to this section's own discipline**: rather than force an explanation onto this residual once the array reading looked refuted, it was left open — precisely the practice `HANDOFF.md` §27.8 asks for, applied here before §27.8 existed as a named rule.

### 11.5 What the shipped weights actually contain: ~~cross-limb influences, in quantity~~ — RESOLVED under the corrected decode 2026-09-13, and the answer is **none** (§11.5.1)

**⚠ SUSPECT, not confirmed wrong, pending re-measurement — 2026-09-13, see §11.15.** Every figure below was computed under the direct-index reading (blend index = rig bone index), which §11.4/§11.15 now retract as the actual bone-resolution mechanism. Under the corrected palette+sign decode, blend index 21 (say) does not name whatever bone this section read it as — it names `palette[21]`, a specific, generally *different* bone. The classification below (which vertices count as "cross-limb," the 1,683/11,343 figure, the worked example) depends entirely on that mapping being right, and it wasn't. This section's own measurement technique is not being second-guessed — only its input. Team B independently flagged the identical risk on their own now-retracted §9.56 finding ("camera-rig contamination," confirmed to be the same underlying misread, not camera bones at all). ~~Do not cite this section's figures until it is re-run under the corrected decode; do not assume they're wrong either — re-measure, don't guess.~~ **RE-RUN 2026-09-13 — see §11.5.1 for the corrected figures and their controls.** The re-measurement was worth insisting on rather than assuming: the corrected figure is not *smaller*, it is **exactly zero** — **0 of 11,343** on this section's own two meshes and **0 of 34,442** over six, against two wrong-permutation controls at **29–37%** and this section's own retracted reading at 14.8%. Every figure in the three tables below is retracted; each table now carries the corrected column beside the struck-through original so the two are directly comparable.

This is the finding that decides the question the section was opened for. The
implementation team's screen was a *vertex-proximity* screen; the predicate used here is a
*skeletal* one, and deliberately makes no reference to vertex positions so that it cannot
be tuned by the result.

**Predicate — branch separation.** For two bones, let `A` be their lowest common ancestor
and let `branch = min(hops(a, A), hops(b, A))`. It is **0** whenever one bone is an
ancestor of the other — the shoulder-to-wrist case, which is ordinary skinning and no seam
risk at all — and is large only when the two bones sit on genuinely independent limbs.
"Different limbs" below means `branch ≥ 3`.

> **Raw hop distance was tried first and is refuted as a classifier.** `hop ≥ 6` is cleared
> by **31,584 of 33,643 random vertex triples (93.9%)** on the same meshes, because a
> 66-to-83-bone rig is deep and six hops is reached inside a single arm. It is recorded
> here so the predicate is not re-tried. **[CONFIRMED — empirical, a refuted predicate.]**

Measured on two full body meshes, over every vertex of the skinned channel. *(The two meshes are `brad` and `angel` — identified in 2026-09-13's re-measurement by re-running the original harness, `tools/harnesses/skin_stray_weight.py`, which reproduces every figure in this table digit-for-digit; the section had not named them.)*

| Measurement | Population | ~~Original — direct-index decode, RETRACTED~~ | **Corrected — palette decode (§11.5.1)** |
|---|---|---|---|
| Vertices carrying a **minority** weight on a bone on a different limb from their dominant bone | 11,343 vertices (5,044 + 6,299) | ~~**1,683 (14.8%)**~~ | **0 (0.00%)** |
| Such minority weights, total | — | ~~**2,042**~~ | **0** |
| Their magnitude | the weights above | ~~median **0.145**, p90 **0.388**, p99 **0.500**, max **0.500**~~ | *empty population — nothing to measure* |
| Rig bone index of the bone carrying them | the weights above | ~~min **6**, max **55**; **0 / 2,042** at index ≥ 64~~ | *empty population — nothing to measure* |
| *(diagnostic, added by the re-measurement)* dominant/minority bone pairs examined | 11,802 pairs | 11,802 — of which **3,198 (27.1%) had no lowest common ancestor at all**, silently bucketed as "not cross-limb" | 11,802 — **0** with no common ancestor; branch separation never exceeds **1** |

~~**[CONFIRMED — empirical.]** A worked case, so the claim is inspectable rather than
statistical: on one body mesh, vertex 4151 sits on the left foot and carries
`l-foot = 228/255` together with **`r-upperarmtwist3` = 27/255** — a bone in the opposite
arm. Its neighbours 4153 and 4154 carry `r-upperarmtwist3 = 1/255`. Across that mesh the
bones most often carrying a cross-limb minority weight are `r-upperarmtwist3` (225
vertices), `r-calf` (156), `r-toe0` (103), `l-clavicle` (66).~~

**⚠ The worked example is RETRACTED, and re-resolving it is the single most legible
demonstration of the retraction** — it is the same three vertices, the same two bytes, read
through the palette (`brad`, whose palette is 56 entries, strictly ascending):

| Vertex | Lanes as stored | ~~Direct-index reading (retracted)~~ | **Palette reading (corrected)** |
|---|---|---|---|
| 4151 | `idx 9 = 228/255`, `idx 55 = 27/255` | ~~`l-foot` + `r-upperarmtwist3` — **opposite arm**~~ | `r-foot` + **`r-calftwist1`** — *same leg, parent-adjacent* |
| 4153 | `idx 9 = 254/255`, `idx 55 = 1/255` | ~~`l-foot` + `r-upperarmtwist3`~~ | `r-foot` + `r-calftwist1` |
| 4154 | `idx 9 = 238/255`, `idx 55 = 1/255`, `idx 14 = 15/255` | ~~`l-foot` + `r-upperarmtwist3` + `l-toe0`~~ | `r-foot` + `r-calftwist1` + `r-toe0` |

The vertex was never on the left foot and never carried an arm influence. It is a **right**
foot vertex whose minority weight sits on the **right calf twist bone** — the single most
ordinary thing a foot vertex can be weighted to. The same substitution empties the whole
population: `r-upperarmtwist3` (225 vertices), `r-calf` (156), `r-toe0` (103) and
`l-clavicle` (66) were the four most frequent "cross-limb carriers" under the retracted
reading, and under the corrected one no bone carries a cross-limb minority weight at all.
**[CONFIRMED — empirical, `tools/harnesses/skin_stray_weight_palette.py`.]**

~~**And the triangles the engine actually draws do join different limbs.**~~ **⚠ REFUTED
under the corrected decode — they do not; the count is 0 on both meshes (§11.5.1).** The
edge enumeration itself is unaffected and is reproduced exactly, so the two columns below
differ only in how a blend index resolves to a bone. Built the way
`spec-vertex-format.md` §8.1/§8.2 specify — one level-of-detail group (group 0), one
independent triangle strip per draw range, zero-area triangles dropped, never reading
across a range boundary:

| Mesh | Drawn edges in group 0 | ~~Of which join different limbs — direct index, RETRACTED~~ | **Corrected — palette decode** |
|---|---|---|---|
| body mesh A (`brad`) | 8,554 | ~~**1,878** (0.2195)~~ | **0** (0.0000) |
| body mesh B (`angel`) | 10,523 | ~~**1,880** (0.1787)~~ | **0** (0.0000) |
| *control:* reversed palette | as above | — | 3,876 (0.4531) / 4,884 (0.4641) |
| *control:* shuffled palette | as above | — | 4,516 (0.5279) / 4,183 (0.3975) |

~~**[CONFIRMED — empirical.]**~~ **These are not strip-restart artefacts** — a conclusion
that survives, for the reason set out below the table. The obvious
suspicion — that an unhandled restart convention fabricates bridging triangles, which is a
live open item in `spec-vertex-format.md` §8.1 — was tested by splitting the triangles into
those within two positions of a repeated-index pair and those in clean strip interior:

| Triangle population | ~~Cross-limb — direct index, RETRACTED~~ | ~~Rate~~ | **Corrected — palette decode** |
|---|---|---|---|
| stitch-adjacent | ~~268 / 1,204~~ | ~~0.223~~ | **0 / 1,204** (0.0000) |
| clean-strip interior | ~~1,011 / 4,074~~ | ~~0.248~~ | **0 / 4,074** (0.0000) |

~~The rates are indistinguishable, and the interior rate is the higher of the two. Had
bridging triangles been a restart artefact, they would have concentrated in the
stitch-adjacent population. **[CONFIRMED — empirical; the restart-convention explanation is
REFUTED for this artefact, which leaves that open item open but removes it as a candidate
cause here.]**~~

**The conclusion this table was drawn for survives its own retraction, and that is worth
stating rather than quietly deleting the table.** It was built to ask whether the bridging
triangles were a **strip-restart** artefact, and it correctly answered *no*. They were not
restart artefacts — they were a **decode** artefact, a third possibility the test was never
designed to separate and never claimed to. Under the corrected decode both buckets are
empty (0 / 1,204 and 0 / 4,074), so the comparison is now **vacuous rather than wrong**:
there is no cross-limb triangle population left in either bucket whose origin needs
explaining. The strip-restart convention (`spec-vertex-format.md` §8.1) remains an open
item and remains **not** implicated here — unchanged in both directions by this
re-measurement.

#### 11.5.1 §11.5 RE-MEASURED under the corrected palette decode — the cross-limb population is **empty**, not smaller: 0 / 34,442 vertices, against controls at 29–37% (2026-09-13)

**One thing changed, and only one.** §11.5's predicate is untouched: the same skeletal
branch-separation test (`branch = min(hops(a, LCA), hops(b, LCA))`, "different limbs" =
`branch ≥ 3`), the same "minority weight beside the dominant weight" framing, the same
meshes, the same channel, the same weight bytes. Everywhere the original read
`bone = blend_index` it now reads `bone = palette[blend_index]`, with the palette taken from
the Mesh header exactly as `skin_side_match.py` / `skin_side_match_signfix.py` already
locate it — count at `mesh_header + 0x38` (u16), array immediately after the channel
records at `mesh_header + 0x70 + nchan * 0x18`. **§11.15's sign correction is deliberately
NOT applied here**, and that is not an oversight: §11.5's predicate is a pure
bone-hierarchy relation and never reads a coordinate, so no sign convention can touch it.
Harness: `tools/harnesses/skin_stray_weight_palette.py` (new), which also re-derives the
original reading in the same run so the before/after come out of one program rather than
two.

**The original figures were reproduced first, digit-for-digit, before anything was
changed** — `tools/harnesses/skin_stray_weight.py` re-run unmodified gives 831/5,044 +
852/6,299 = **1,683 / 11,343 (14.84%)**, 1,054 + 988 = **2,042** minority weights, median
**0.1451**, p90 **0.3882**, p99/max **0.5000**, carrier index **6..55**, 0 at ≥ 64. That
matches §11.5's published table exactly, which establishes that what follows differs from
it by the index mapping and by nothing else — not by a re-implementation, a different
population, or a drifted harness.

**Result, §11.5's own two meshes, all four readings side by side:**

| Reading | Cross-limb vertices | Rate | Cross-limb minority weights |
|---|---|---|---|
| ~~direct index (§11.5 as published, RETRACTED)~~ | ~~1,683 / 11,343~~ | ~~0.1484~~ | ~~2,042~~ |
| **palette[blend_index] (corrected)** | **0 / 11,343** | **0.0000** | **0** |
| *control:* palette read reversed | 4,132 / 11,343 | 0.3643 | 4,916 |
| *control:* palette shuffled (fixed seed) | 4,195 / 11,343 | 0.3698 | 5,523 |

**Extended to six character meshes** (the same six `skin_side_match_signfix.py` used, rigs
of 66–82 bones), because a zero on two meshes is a weaker claim than a zero on six:

| Reading | Cross-limb vertices | Rate |
|---|---|---|
| ~~direct index (retracted)~~ | ~~4,259 / 34,442~~ | ~~0.1237~~ |
| **palette[blend_index] (corrected)** | **0 / 34,442** | **0.0000** |
| *control:* reversed palette | 10,011 / 34,442 | 0.2907 |
| *control:* shuffled palette | 10,495 / 34,442 | 0.3047 |

Per mesh the corrected count is 0/5,044 (`brad`), 0/6,299 (`angel`), 0/6,462 (`alfred`),
0/8,047 (`zimos`), 0/4,750 (`oleg`), 0/3,840 (`antwon`) — not a small number six times, the
same exact zero six times. **[CONFIRMED — empirical.]**

**The control fired, in the direction that matters.** Both wrong-permutation controls are
built from the *same bone values* as the real palette — reversal and shuffle are
permutations of the identical multiset — so they prove the `branch ≥ 3` relation is richly
**reachable** inside each mesh's own palette (29–37% of vertices hit it), and that the
predicate is therefore fully capable of firing on this data. It does not fire on the real
mapping. Had the controls also come back near zero, the honest conclusion would have been
"this predicate cannot see anything through a palette" and neither §11.5's number nor this
one would have meant anything; that is the failure mode the controls were run to exclude,
and it did not occur.

**A zero needs a liveness check, not just a control — here it is.** A classifier can return
zero because nothing qualifies, or because it silently stopped working. Every intermediate
count is identical across all four readings, which pins down that only the bone identity
moved:

| Quantity (pooled, six meshes) | direct | **palette** | reversed | shuffled |
|---|---|---|---|---|
| live weight lanes read | 66,822 | **66,822** | 66,822 | 66,822 |
| lanes dropped as out-of-range | 0 | **0** | 0 | 0 |
| multi-influence vertices | 22,014 | **22,014** | 22,014 | 22,014 |
| dominant/minority bone pairs examined | 32,380 | **32,380** | 32,380 | 32,380 |
| all minority weights: median / p90 / max | 0.1797 / 0.4431 / 0.5000 | **0.1797 / 0.4431 / 0.5000** | same | same |
| pairs where the LCA lookup returned nothing | **6,451 (19.9%)** | **0** | 0 | 0 |

The corrected decode does not win by discarding data: it reads the same 66,822 lanes, drops
none, and examines the same 32,380 bone pairs. The minority weights themselves are entirely
unaffected — they still exist in quantity, still reach 0.5000, still go down to `1/255`.
**They are simply all same-limb.** The corrected branch-separation histogram over those
32,380 pairs is `0: 21,166` (one bone is an ancestor of the other), `1: 10,999` (one hop off
the shared parent — sibling joints), `2: 215`, and **nothing at all at 3 or above**; the
maximum branch separation reached anywhere in the corrected population is **2**, on `zimos`
and `oleg` only.

**An independent structural tell the original reading was leaving behind, visible only in
hindsight.** Under the direct-index reading, **6,451 of 32,380** dominant/minority bone
pairs (19.9%) had **no lowest common ancestor at all** — the two "bones" sat in disconnected
trees, which in these rigs means one of them was a parentless helper (§8.1's `camera` bone
and its kin). §11.5 could not see this because its classifier treated a `None` branch as
"not cross-limb" and moved on. Under the corrected decode that count is **0 of 32,380**:
every single influence pair in the shipped data lies inside one connected sub-tree. A
one-in-five rate of anatomically impossible bone pairs was sitting in the input the whole
time. **[CONFIRMED — empirical; recorded as a `HANDOFF.md` §5 methodology note: a classifier's
"unclassifiable" bucket is evidence about the decode, not a rounding error to skip past.]**

**Two incidental findings worth their own lines, because they bear on other open items.**

1. **The 64-entry limit applies to the palette slot, not to the rig bone index — which
   inverts §11.6 row 4's arithmetic and explains why the indirection exists at all.** Blend
   indices never exceed 63 anywhere in the population (`spec-vertex-format.md` §6.3), and
   §11.5 read that as "no influence lands on a bone at index ≥ 64, so the clamp never
   bites." Under the corrected decode, **5,307 of 66,822 live lanes (7.9%) resolve to a rig
   bone index of 64 or higher**, reaching **81** on `zimos` (per mesh: 542, 465, 1,864,
   1,629, 469, 338). A 6-bit blend index addressing a 64-entry palette is precisely the
   mechanism that lets a mesh skin to bone 81 of an 82-bone rig. **The `0x40` clamp
   (§11.2) therefore bounds the palette, not the rig bone index** — which is the whole
   reason the indirection has to exist, and the reason §11.5's "the clamp never bites"
   argument had it backwards. Two corroborating shapes in the same numbers: the palette
   length tracks the highest blend index the mesh actually uses at exactly `length − 1` on
   five of the six (56/55, 54/53, 48/47, 59/58, 48/47), i.e. each palette is fitted to its
   own mesh rather than being a fixed-size table; and the one array longer than 64 entries
   (`zimos`, 75) is the two-set case from finding 2 below, whose sets are 58 and 17 — each
   individually under 64. **[CONFIRMED — empirical for the ≥ 64 resolution and the
   length-fitting; the per-set-versus-per-array reading of the 64 bound is a HYPOTHESIS
   resting on one mesh.]**
2. **`zimos`'s `+0x38` count of 75 is two concatenated strictly-ascending runs**, 58 entries
   (1..81) then 17 entries (1..65), with 7 values duplicated across the boundary — the only
   non-ascending array in these six. Its highest blend index in use is 57, entirely inside
   run 0, so this measurement never crosses the boundary and is unaffected. Recorded because
   it is a concrete instance of §11.15's last open item (the per-draw-range multi-set
   selector, 46/318 character meshes carrying 2–3 sets): here the sets are stored back to
   back and `+0x38` counts their **total**, not the first set's length. **[OBSERVATION — one
   mesh; it does not settle which draw range selects which set, and no attempt was made to.]**

**Verdict, stated plainly.** The corrected population shows **no genuine cross-limb
minority-weight phenomenon — none, not a smaller one**. §11.5's 1,683 vertices, its 2,042
stray weights, its 3,758 bridging drawn edges and its `l-foot`/`r-upperarmtwist3` worked
example were all artefacts of the retracted blend-index mapping, with no residue left at any
magnitude. This independently corroborates Team B's before/after render comparison cited in
§11.15 (the visible artefact *disappears* rather than shrinking) from the file side, by a
method that never looks at a rendered image or a vertex coordinate — two unrelated
instruments, same answer. It was measured rather than assumed: the original figures were
reproduced first, two wrong-permutation controls were run and both fired at 29–37%, and the
zero was liveness-checked against six identical intermediate counts before being believed.

### 11.6 The answer, stated plainly

Taking §11.1–§11.5 together, each of the four candidate mechanisms `HANDOFF.md` §28.8
listed can be settled:

| Candidate | Verdict |
|---|---|
| **Dual-quaternion skinning** | **REFUTED.** The palette element is a 48-byte 3×4 affine matrix whose identity constant is an affine identity. A dual-quaternion palette is 32 bytes per bone. **[CONFIRMED — disassembly.]** |
| **A post-skin vertex-smoothing or normal-recalculation pass** | **REFUTED as a CPU pass.** Skinned positions are never computed on the CPU — the blend inputs are Direct3D 9 vertex-stream attributes (§11.1), so no CPU code ever holds a skinned position to smooth. A *shader-side* scheme cannot be excluded from this side. A vertex shader processes one vertex per invocation with no access to its neighbours in the primitive, which makes neighbour smoothing awkward rather than impossible — Shader Model 3.0 does support vertex texture fetch, so a shader could in principle read adjacency data from a texture, and nothing measured here rules that out. What *is* ruled out is any CPU stage. **[CONFIRMED — disassembly for the absence of a CPU pass; HYPOTHESIS — unconfirmed that no shader-side smoothing exists, since the shader was not read (§11.7 item 1).]** |
| **A minimum-weight floor, or a hard single-bone assignment near close seams** | **REFUTED — verdict RE-CONFIRMED on a repaired basis, 2026-09-13 (§11.5.1); the figure it originally rested on is retracted.** ~~⚠ SUSPECT, not confirmed — 2026-09-13, see §11.15.~~ ~~**REFUTED in the shipped data.** 1,683 of 11,343 vertices carry a cross-limb minority weight, median magnitude 0.145 — the opposite of a floor that clamps small weights away, and the opposite of a hard single-bone assignment. **[CONFIRMED — empirical.]**~~ **RESOLVED 2026-09-13 (§11.5.1) — the verdict REFUTED survives, on a repaired basis, and the cited figure does not.** The cross-limb classification was an artefact of the retracted mapping (corrected count: **0**), so the "1,683 of 11,343 … median 0.145" argument is withdrawn. What the corrected decode shows instead still refutes both candidates, and more directly: the shipped data carries **32,380 minority weights over six meshes** (median 0.1797, p90 0.4431, down to `1/255`) sitting beside their dominant weight — so there is no minimum-weight floor and no hard single-bone assignment. Those minority weights are simply all **same-limb** (branch separation 0, 1 or 2; never ≥ 3). *(The peer relay that categorized this row as resting on §11.1–§11.3 reached the right verdict for the wrong reason — the row did rest on a retracted figure, and flagging it rather than accepting that categorization was correct.)* |
| **A palette limit that turns stray influences into the identity matrix** | **REFUTED — verdict RE-CONFIRMED 2026-09-13 (§11.5.1), but this row's supporting arithmetic was inverted and is corrected below.** ~~⚠ SUSPECT, not confirmed — 2026-09-13, see §11.15.~~ ~~**REFUTED.** The gather does substitute the identity for any bone at or past its limit, and the no-table path clamps to 64 bones — a real mechanism that *would* neutralise strays living on high-numbered helper bones. They do not live there: the carrier bone indices run 6–55 and **0 of 2,042** are at 64 or above, while blend indices never exceed 63 anywhere in the population (`spec-vertex-format.md` §6.3), so the clamp never bites on real data. **[CONFIRMED — empirical + disassembly.]**~~ **RESOLVED 2026-09-13 (§11.5.1) — the verdict REFUTED survives, but this row's arithmetic was inverted and is corrected here.** There are no strays at all under the corrected decode (**0**), so the clamp has nothing to neutralise and the candidate is refuted more simply than the row argued. The reasoning itself, however, was backwards: "carrier bone indices run 6–55 and 0 of 2,042 are at 64 or above" read blend indices as bone indices. Corrected, **5,307 of 66,822 live weight lanes (7.9%) resolve to a rig bone index ≥ 64**, reaching **81** on `zimos`. The `0x40` clamp (§11.2) bounds the **palette**, not the rig bone index — and a 6-bit blend index addressing a 64-entry palette is exactly the mechanism by which a mesh reaches bone 81 of an 82-bone rig. The clamp is therefore not slack; it is saturated, and it is the reason the indirection exists. |
| **"The shipped game simply never poses characters this way"** | **NOT AVAILABLE as an explanation — unaffected by this retraction, since it measures the clip's own joint rotations, not blend-index-to-bone mapping.** The implementation team's own file-side census of `preload_anim.vpp_pc` found 2,964–3,496 of 4,072 walkable clips driving the risky bones, median rotation reached 32–108° and 90th percentile 55–177° across five bones. This is routine motion. *(Figure quoted from the implementation team's own count over their own population — not re-derived here, and not to be combined with any figure in this document.)* |

~~**So the engine has no seam-avoidance mechanism for this class, and the assets contain the
conditions for it.** The engine renders the same linear blend of the same weights over the
same topology. **[The conjunction is CONFIRMED from the two sides above; that the resulting
artefact is therefore visible in the shipped game is HYPOTHESIS — unconfirmed, because the
magnitude a viewer sees was not measured against a real animated pose here.]**~~

**⚠ RETRACTED, 2026-09-13, see §11.15 — and now DROPPED rather than left pending, §11.5.1.** The premise this conjunction rests on — that shipped weights carry real cross-limb minority influences in quantity — was measured under the retracted blend-index mapping (§11.5). It has since been re-measured under the corrected palette decode and is **false**: the cross-limb population is **0 / 34,442 vertices over six meshes**, against wrong-permutation controls at 29–37% (§11.5.1). **The assets do not contain the conditions for this artefact class.** The conjunction is not re-asserted and not merely suspect; its second half is refuted. Team B's own before/after render comparison under the corrected decode (§11.15) found the visible artefact this row predicted simply isn't there: clean, coherent figures, not a smaller version of the exploded-mesh wreckage. Whether any genuine cross-limb minority-weight population survives at a smaller, non-visible magnitude once correctly decoded is unmeasured on this side.

**⚠ The whole paragraph below is RETRACTED, 2026-09-13, see §11.15 — it analyzes the magnitude of an artefact that Team B's own §9.56 seam-fragmentation finding (the "175–291×"/"4–19mm" figures it cites) has since retracted in full. Kept for its own record rather than deleted; do not cite any figure in it.**

~~**Why that is a coherent state of affairs rather than a contradiction, and where the
remaining explanation must live.** The visible displacement a stray influence produces is
approximately its weight times the distance its bone has moved: `1/255` on a bone that
travels a metre is 4 mm and invisible, while `27/255` on the same bone is 106 mm and is
not. The published 175–291× figures are **stretch ratios measured against bind-pose gaps of
4–19 mm**, so they describe the denominator at least as much as the artefact — 175 × 4 mm
is 0.7 m, but the same absolute error on an ordinary 30 mm edge is a 23× ratio nobody would
have flagged. **The remaining question is therefore quantitative, not structural**, and it
is the one thing this side cannot measure: what absolute displacement, in metres, the real
shipped poses produce at these vertices. A synthetic pose was tried here and is reported as
inconclusive rather than as a number — giving every bone below each clavicle an independent
45° rotation moves *same-limb* edges by a p90 of 3.8× and a p99 of 16× as well, so the pose
is not realistic enough for the cross-limb figure to mean anything. **Stating that as a
refused measurement rather than quoting its number is deliberate: the control fired.**~~

**⚠ §11.7 through §11.14 individually assessed 2026-09-13 — Team B read all of it in full (not from titles) and gave a precise per-item verdict, cross-checked against this document rather than accepted blind. Net: most of it stands.**

| Item | Verdict |
|---|---|
| §11.7 item 1 (shader itself, mode-a blocked) | Untouched, stands. |
| §11.7 item 2 (bone-index limit = caller-supplied subset, §11.12.1) | Independent of blend-index/sign, stands. |
| §11.7 item 3 (gather-addressing conflict) | **Flips OPEN → RESOLVED**, see the updated item text below — this document had already found and honestly refused to paper over the exact inconsistency that the sign-fix resolves. |
| §11.7 item 4 / §11.10 / §11.11 (10-of-78 residual, midline slot) | **NEEDS RE-EXAMINATION under the palette decode, not just a flag** — dispatched separately, see §11.15's open-items list. **→ DONE §11.17 (13/13 resolve).** |
| §11.7 item 5 (absolute displacement under real poses) | **Moot** — asks the implementation team to measure an artefact §11.15 has since shown doesn't exist. Superseded, not actionable. |
| §11.8 items 1–3 (`CreateVertexDeclaration` correction, 64-clamp wording, strip-restart removal) | Independent of blend-index/sign, stand. |
| §11.8 item 4 | Already superseded (struck through) from the earlier same-day sync. |
| §11.9 (all six methodology notes) | Fully general, self-contained, stand as-is regardless of the blend-index question. **[⚠ Notes 1–5 stand; note 6's "refuted" verdict was itself inverted by §11.15 — see the annotation on note 6.]** |
| §11.12–§11.14 (bone-index-limit disassembly, `+0x40`=`+0x38` confirmation, attachment/prop-lag identification, uncarved-code scan) | All independent, survive completely. §11.12.2's disassembly-side confirmation that Mesh `+0x40` is the same array as `+0x38` is now read as independent corroboration of the file-side palette identity, from the opposite direction — not just "unaffected," genuinely reinforcing. |

Assessment credited to Team B's own full read; cross-checked against this document before accepting (their own earlier claim that §11.6 row 3 "stands" was itself wrong and corrected by them on a second look — see §11.6's own annotation).

### 11.7 Open items, each with a next step

1. **The vertex shader's blend arithmetic itself.** Located but not read (§11.1). The
   shaders are standard Direct3D 9 `vs_3_0` blobs inside a documented wrapper
   (`spec-fxo-format.md`), so this is not a format problem — it is the mode-(a) container's
   entry-0-only limitation. **Next step:** extract shader entry 0 from `shaders.vpp_pc` and
   disassemble it with any standard Direct3D 9 tool; if the skinned-character shader is not
   entry 0, the blocker is the container, not the shader.
2. **The runtime value of the gather's bone-index limit** (§11.3). It arrives from the
   animation instance registry via `FUN_004be910`. ~~**Next step:** decompile the registry
   record's producer to establish whether `+0x04` of the object at record `+0x3c` is the
   rig's bone count or something smaller.~~
   **RESOLVED 2026-09-13 — see §11.12.1.** It is neither: `+0x04` is a caller-supplied
   **subset size**, filled wholesale by a generic "named-bone-list binding" constructor
   (`FUN_004c1350`) that never consults the rig object at all — the same construction path
   the vehicle loader uses for part→rig-bone binding (`FUN_00ab0730`, already documented in
   `spec-vehicle-geometry.md`). Every consumer of the object treats the count as a bound
   (`≤` the rig's own bone count), never an equality.
3. **The gather's addressing versus the measured addressing — a real conflict, not a
   rounding.** The gather computes `destination[i] = source[table[i]]`, and the table is the
   mesh's skinnable-bone list (§11.4). If the shader indexes that destination with the
   vertex's blend index, then blend index `i` names rig bone `table[i]` — which §11.4's
   three tests refute. The two readings can only both be true if the **source array is not
   indexed by rig bone index**. `spec-anim-format.md` §6b and §10.1 independently show the
   animation subsystem working in *track* order with a bone→track reverse lookup, which is a
   plausible candidate ordering. ~~**Next step:** confirm what the object at mesh-resource
   `+0x30` actually is by reading where the `.ccmesh_pc` parser (`FUN_007524f0`,
   `FUN_00e71410`) stores a fixed-up pointer at Mesh header `+0x40`, and establish the
   source array's ordering from `FUN_004be9c0`'s producer. Until then the identification of
   the gather's table with the `+0x38` array is **HIGH CONFIDENCE — inferred**, not
   confirmed.~~
   **RESOLVED 2026-09-13 — see §11.15, superseding the "partially resolved" note below.**
   This item is the clearest vindication of this document's own discipline: it correctly
   identified a real, structural inconsistency (the gather's own mechanics say blend index
   `i` names rig bone `table[i]`; §11.4's test said otherwise) and refused to just trust one
   side over the other. The resolution is exactly what it should be: §11.4's test was
   measuring the *correct* relationship (`table` = the Mesh-header `+0x38` bone-palette
   array), under an inverted sign convention (§11.15 — the corrected rig→mesh transform for
   skinning is `(−x,−y,−z)`, not `(x,−y,−z)`), which turned a real, correct mapping into what
   looked like a clean refutation. Once the sign is fixed, both readings converge: **the
   source array is functionally indexed by rig-bone order after all** — the second reading
   this item's own text names as the only other way both could be true. A disassembly-level
   confirmation of `source`'s own fill mechanism (whether it's a bulk copy from an
   already-bone-ordered pose buffer, as the logic above implies) is separately in progress;
   this item's status does not wait on that to flip, since the sign-fix result is decisive on
   its own.

   **The disassembly-level corroboration landed 2026-09-13 — see §11.16, which does not
   change this item's RESOLVED status either way.** Two halves, both confirmed from raw
   instructions in a fresh project copy: (i) `table` **is** the Mesh-header `+0x38` array
   literally, not an array of the same shape — both call sites form it by *address
   arithmetic* (`ADD EAX,0x38` / `ADD ECX,0x38`), each followed by the vacuous null-test a
   compiler emits for `&mesh->member`, so the one alternative reading (a pointer *loaded*
   from `mesh+0x38`) is refuted rather than merely unlikely; and (ii) `source` is allocated
   by `FUN_004c79a0` as an array of **exactly `limit`** 48-byte matrices — `limit` being the
   identical value (the dword at `+0x04` of the object that `rec+0x3c` points to) the gather receives — so **the gather's own
   `table[i] >= limit → identity` test is an array-bounds check on `source`**, which is only
   type-correct if `table`'s values and `source`'s indices share one index space. §11.15's
   file-side 171/172 is what names that space rig-bone order; the two sides meet there.
   A bulk-copy palette fill was also located and read in full, though on the *sibling*
   no-table path (`FUN_00abfad0` → `FUN_00e7ad30`, a third straight-copy call site not
   previously documented). What remains unlocated is the per-frame writer of the matrices
   *inside* that block — and §11.16.3 now explains why three prior passes could not find one:
   `rec+0x00` is written exactly three times in the binary (ctor zero, allocator, release
   zero), so there is no per-frame store to the pointer to find; the pose is written
   *through* it.

   ~~**PARTIALLY RESOLVED 2026-09-13 — see §11.12.2–11.12.3.** The `+0x40` half is now
   **CONFIRMED — disassembly**: it is unconditionally the same `+0x38` array's own fixed-up
   pointer, written 8 bytes after its count by the same parser statement — there is no
   separate object there to have been wrong about. That *removes* the one explanation that
   could have dissolved this conflict for free, so it now stands sharper, not resolved: the
   source array's own ordering (the actual remaining variable) was traced to a named,
   7-function call graph and stays **OPEN** — its fill loop was not located.~~
4. **The 10-of-78 side-match residual and the midline slot that resolves to a toe**
   (§11.4). ~~**Next step:** list, for one mesh, every slot the published reading gets on the
   wrong side, with vertex counts and cluster centroids, and check whether they form a
   contiguous run — a contiguous run would indicate a partial reordering rather than noise.~~
   ~~**RESOLVED 2026-09-13, in two parts — see §11.10 (the 10 mismatches) and §11.11 (the
   midline slot).** Neither is a contiguous reordering. The 10 mismatches sit inside one
   narrow, already-documented structural band (the hand/finger "detail section," `+0x2C`)
   but are scattered *within* it — 1.3–7.5 cm from a bone on the anatomical mirror of the one
   named, the dominant-strength tail of §11.5's own cross-limb weight population, not an
   index-list error. The midline slot is the head (2–14 cm), mislabelled `r-toe0` because
   `r-toe0` genuinely occupies that list position — explained by a newly-observed fact about
   this rig format's bone ordering (§11.11), not by any defect in the published reading.~~
   **RESOLVED under the corrected decode, 2026-09-13 — see §11.17. All 10 mismatches, plus all
   three slot-15 instances, resolve correctly.** Re-running §11.4 Test 3's exact predicate with
   `bone = palette[slot]` and the corrected sign (§11.15) on the same 10 (mesh, slot) pairs:
   **13/13 side-matches**, using the palette-resolved bone's *exact* rest-X position (not just
   its sign) against the cluster's own measured mesh-space centroid — e.g. `brad` slot 21
   (published `l-hand`, wrong side) resolves to `palette[21] = 24 = l-finger2`, rest-X `−0.6487`
   against a measured cluster mean of `−0.655`, a 6 mm agreement, not a coincidence of sign.
   Team B's own independent slot-15 finding (`palette[15] = 17 = head`, §9.63.1) is reproduced
   exactly here on all three sample meshes. §11.10's original "dominant-strength tail of §11.5's
   cross-limb population" explanation is retracted along with §11.5 itself — these were never
   cross-limb minority weights at all, they were the *dominant* weight resolving to the *wrong*
   bone under the retracted mapping. §11.11's breadth-first bone-list-ordering observation is
   retracted as an explanation for slot 15 specifically (it isn't a near-miss by list position,
   it's an exact different bone) but may still be a real, separate structural fact about this
   format worth keeping — not re-verified independently of this specific use.
5. ~~**The absolute displacement under real shipped poses** (§11.6). This needs a decoded
   clip applied to a decoded mesh, which is the implementation team's pipeline rather than
   this one's. **Next step, for them:** report the artefact as **absolute vertex
   displacement in metres** at the flagged vertices under a real clip, alongside the stretch
   ratio, and separately for same-limb and cross-limb edges so the pose's own contribution
   is visible. A same-limb figure of the same order would mean the pose, not the seam.~~
   **MOOT, 2026-09-13, see §11.15.** Asks for a measurement of an artefact that doesn't exist — Team B's own before/after render comparison already answers this more directly than the requested displacement figure ever could have.

### 11.8 Corrections this pass makes to other documents

1. **⚠ `HANDOFF.md` §5 and this project's standing claim that "the binary makes zero D3D9
   vertex-shader-related COM vtable calls at the usual `CreateVertexDeclaration` /
   `DrawIndexedPrimitive` slots" is REFUTED as stated.** `CreateVertexDeclaration` **is**
   called, at `0x00476e58` inside `FUN_00476ca0`, through the device vtable at displacement
   `+0x158` (§11.1). The claim's *operational advice* — do not expect to recover the vertex
   layout by reading that call — happens to remain good, because the layout is assembled
   from the tables in §11.1 rather than being visible at the call site. But the stated fact
   is wrong, and it was wrong in a way that discouraged exactly the trace that answered this
   task. **[CONFIRMED — disassembly.]** *(This document cannot edit `HANDOFF.md`; recorded
   here for whoever maintains it.)*
2. **`spec-vertex-format.md` §6.3's "palette size is 64" gains a code literal behind it**
   (§11.2: the no-table palette load path clamps the bone count to `0x40`). The label
   should not go all the way to **CONFIRMED — disassembly** for "the palette size is 64",
   because the allocator accepts up to 256 bones and the gather path carries no such
   clamp; what is confirmed is the clamp itself. Recommended wording: 64 is an enforced
   bound on the no-table path, and no shipped blend index exceeds 63.
3. **`spec-vertex-format.md` §8.1's open item on the strip-restart convention is untouched,
   but is removed as a candidate cause of cross-limb bridging triangles** (§11.5: the
   cross-limb rate is 0.223 stitch-adjacent versus 0.248 in clean strip interior). **[⚠ figures
   retracted §11.5.1 — both buckets are now 0; the conclusion survives, see §11.5's note under its
   re-measured table.]**
4. ~~**`spec-rig-format.md` §8.1 is reconfirmed, with a stronger control, and gains a stated
   residual** (§11.4). The `(+X, −Y, −Z)` transform and direct blend-index addressing both
   stand; the sentence "There is no palette indirection" should be read as qualified by
   §11.4 — an indirection table **does** exist in the engine and in the mesh file, it simply
   is not the one the vertex's blend index travels through.~~ **RETRACTED 2026-09-13 — see
   §11.15 and §8.1's own retraction: the transform is a point inversion `(−X, −Y, −Z)`, and
   blend indices resolve through the Mesh-header `+0x38` bone palette.**

### 11.9 Methodology notes worth `HANDOFF.md` §5

1. **A census of an instruction pattern is only as good as the pattern the compiler
   actually emits — and this one silently produced a 60× undercount that read as a clean
   negative.** Scanning for `CALL dword ptr [reg + disp]` found **293** sites in 3.5 M
   instructions and **zero** at every Direct3D 9 device slot. The compiler here does not emit
   that form: it emits `MOV reg2, dword ptr [reg1 + disp]` followed by `CALL reg2`. Counting
   the two-instruction idiom instead found **17,916** virtual-call sites, and
   `CreateVertexDeclaration` among them. **The scan was caught only because it was made to
   assert a known positive** — a function already read in decompilation and known to make
   two vtable calls — **and that assertion failed, printing
   `KNOWN_POSITIVE_FIRED false`.** Without it the zero would have been published as
   confirmation of a standing project claim. This is `HANDOFF.md` §5's "state the predicate,
   not the conclusion" and its "put a known-positive case in every scan and assert it fires"
   in one event, and it is the second time in this project that a uniform zero was a
   connection failure rather than a data fact.
2. **A proxy predicate needs its chance rate before it is used as a classifier, not after.**
   "Bone-hop distance ≥ 6" reads like "different limbs" and is cleared by 93.9% of random
   vertex triples, because a deep rig reaches six hops inside one arm. The replacement —
   distance from the lowest common ancestor *on each side* — is zero whenever one bone is an
   ancestor of the other, which is the case the naive metric could not exclude, and it comes
   with a discrete left/right validation that has a 50% chance rate. **A classifier built
   from a plausible-sounding quantity is a hypothesis about that quantity.**
3. **Fit a correspondence's translation from the median, not the mean, when the point is to
   compare two candidate correspondences.** A mean-fitted translation is moved by the few
   slots that land on distant helper bones, and it is moved *differently* for each candidate,
   so part of the gap between them is the fit rather than the data. Here the verdict survived
   (0.0875 versus 1.2584 under the robust fit, 0.1653 versus 1.1938 under the original), but
   the magnitudes moved by a factor of two.
4. **An "expected residual" is where a real anomaly hides, and a story that explains its
   size does not license its shape.** `spec-rig-format.md` §8.1 accounts for a 0.10–0.14 m
   residual as about half a limb radius plus half a segment, and separately notes that helper
   bones score worse "and are expected to". Both statements are reasonable and both are about
   *magnitude*. A discrete test on a different axis — which side of the body a cluster is on —
   found 10 of 78 slots on the wrong side and one midline cluster of several hundred vertices
   resolving to a toe, which no magnitude story covers. **When a residual has an accepted
   explanation, test it on an axis the explanation says nothing about.**
5. **Refusing to publish a number is sometimes the result.** The synthetic-pose stretch
   measurement produced a headline-shaped figure (cross-limb edges stretching 3.4× median,
   205× at p99) and it is not reported as a finding, because the control fired: same-limb
   edges under the same pose reached p90 3.8× and p99 16×, so the pose was scrambling the
   mesh rather than posing it. The first version of that harness **had no same-limb
   population at all** and would have published the cross-limb figure unopposed. **Any
   measurement of "this subset behaves badly" needs the complementary subset measured under
   the identical treatment, and building the harness without it is the error, not forgetting
   to look.**
6. **A hypothesis that would resolve the whole task deserves the harshest test, not the
   quickest.** The mesh's flags-bit-1 byte array looked like a blend-index remap, and if it
   had been one it would have explained the implementation team's artefact outright — wrong
   bones, torn vertices, general across meshes. It was tested three ways (the published
   centroid method, a robust refit, and a discrete side-match with nine alignment variants
   and two controls) and refuted each time. `HANDOFF.md` §5 already records that "a result
   that would close several open threads at once deserves MORE scrutiny"; this is the first
   time in this project that the rule was applied to the session's own most attractive
   hypothesis and killed it. **[⚠ Inverted by §11.15: the flags-bit-1 array IS the bone palette;
   the three tests shared one confound (the sign convention), so the "refutation" was itself
   wrong. The lesson stands, but this example now illustrates it in reverse.]**

### 11.10 The 10-of-78 side-match residual, itemised — not a contiguous run, and not noise either (2026-09-13)

**⚠ Its "mirror-hand weight noise" explanation is RETRACTED, 2026-09-13 — see §11.17. The 10 mismatches below are real and correctly itemised; what they mean is not what this section concludes.** They resolve cleanly under the corrected palette+sign decode — not cross-limb minority weight at all.

**Sample and method: identical to §11.4 Test 3, decomposed per mesh.** §11.4's "10 of 78" and
"68/78" are pooled over the same three full-body meshes as that test — `brad`, `angel`,
`alfred`, the three whose lateral-scoreable-slot counts sum to 78 and whose side-match counts
sum to 68, reproduced exactly here: 23/25 + 25/29 + 20/24 = **68/78**. `skin_side_match.py`'s
own predicate, unchanged: per blend-index slot, the centroid of vertices whose dominant weight
(max of the four blend weights) is ≥ 245/255 on that slot; scored only if both the cluster's
mean X and the candidate bone's rest X exceed 4 cm in magnitude (`LAT = 0.04`); reading A is
slot k → rig bone k.

**The 10 mismatches, in full** (slot index, vertex count at ≥245/255 dominance, mesh-space 3D
centroid, published bone, and the side each reading gives it):

| Mesh | Slot | Bone (published) | n | Centroid (x, y, z), m | Bone restX | Published side | Actual side |
|---|---|---|---|---|---|---|---|
| brad | 21 | l-hand | 16 | (−0.6550, 0.9728, 0.0510) | +0.5915 | right | left |
| brad | 23 | l-finger1 | 18 | (−0.6232, 0.9774, 0.0047) | +0.6442 | right | left |
| angel | 21 | l-hand | 25 | (−0.6605, 0.9767, 0.0603) | +0.5915 | right | left |
| angel | 23 | l-finger1 | 28 | (−0.6403, 0.9594, 0.0122) | +0.6442 | right | left |
| angel | 41 | r-finger21 | 39 | (0.2184, 1.3044, −0.0696) | −0.6698 | left | right |
| angel | 43 | r-finger41 | 80 | (0.3522, 1.2724, −0.0775) | −0.6478 | left | right |
| alfred | 21 | l-hand | 29 | (−0.6600, 0.9662, 0.0616) | +0.5733 | right | left |
| alfred | 23 | l-finger1 | 24 | (−0.6467, 0.9477, 0.0139) | +0.6487 | right | left |
| alfred | 24 | l-finger2 | 19 | (−0.5852, 1.0192, 0.0774) | +0.6509 | right | left |
| alfred | 29 | r-finger1 | 19 | (0.5876, 1.0196, 0.0785) | −0.6487 | left | right |

("Published side" is the sign of the candidate bone's own rest-X under reading A, i.e. slot k
→ bone k; "actual side" is the sign of the cluster's own mean X. `n` counts vertices whose
dominant, ≥245/255 weight lands on that slot, not the mesh's total vertex count.)
**[CONFIRMED — empirical, this pass; reproduces §11.4 Test 3's 68/78 exactly.]**

**Contiguity, tested against the predicate §11.7 item 4 stated in advance — neither of the
two outcomes it named.** Per mesh, the mismatched slot indices are `{21, 23}` (brad),
`{21, 23, 41, 43}` (angel), `{21, 23, 24, 29}` (alfred). None of these is an unbroken run of
consecutive integers — brad skips 22; angel skips 22 and 24–40 and 42; alfred skips 22 and
25–28 — so **this is not a partial reordering of one contiguous sub-range**, the possibility
§11.7 item 4 named as the alternative to noise. But it is not scattered across the scored
population either: **10 of 10 mismatches, in all three independently-rigged meshes, fall
inside the same narrow index band, 21–44**, out of scored ranges that extend down to index
3–9. Zero mismatches occur among the scored slots covering root/pelvis/spine/legs/torso/
clavicles across the three meshes. **[CONFIRMED — empirical.]**

**That band is not an arbitrary window — it is this rig format's own documented "detail
section."** §3.1 already reports header field `+0x2C` as "the first bone of a 'detail'
section," pointing at `l-finger1` (141 rigs) or `l-hand` (45 rigs) on full-body rigs. On the
three sample rigs here, `+0x2C` is exactly 23 (`l-finger1`, brad and alfred) or 21 (`l-hand`,
angel) — i.e. the mismatch band's own left edge. **The residual is confined to the one
sub-tree this format already flags as structurally distinct, not spread evenly across the
bone list.** **[CONFIRMED — empirical: the population's own `+0x2C` value matches the mismatch
band's start in all three sample rigs.]**

**Whole-rig nearest-bone search resolves *why*, and it is not an index-list error.** For each
of the 10 mismatched clusters, searching every bone in the rig (not only the one the published
reading names) for the closest mesh-space centroid, using the confirmed `(+X, −Y, −Z)`
transform (§8.1):

| Mesh | Slot (published) | Nearest bone, any index | Distance | Distance to published bone |
|---|---|---|---|---|
| brad | 21 l-hand | r-finger2 | 0.018 m | 1.251 m |
| brad | 23 l-finger1 | r-finger4 | 0.013 m | 1.270 m |
| angel | 21 l-hand | r-finger2 | 0.019 m | 1.257 m |
| angel | 23 l-finger1 | r-finger4 | 0.013 m | 1.287 m |
| angel | 41 r-finger21 | l-upperarmtwist1/2 (tied) | 0.075 m | 0.968 m |
| angel | 43 r-finger41 | l-upperarmtwist3 | 0.027 m | 1.060 m |
| alfred | 21 l-hand | r-finger2 | 0.019 m | 1.238 m |
| alfred | 23 l-finger1 | r-finger4 | 0.016 m | 1.298 m |
| alfred | 24 l-finger2 | r-thumb1 | 0.026 m | 1.237 m |
| alfred | 29 r-finger1 | l-thumb1 | 0.026 m | 1.237 m |

Every nearest-bone distance is **1.3–7.5 cm** — tighter than the **10–14 cm** §8.1 already
treats as ordinary joint-to-skin-centroid offset — while the published reading's own distance
for these same 10 clusters is **0.97–1.30 m**. In **8 of 10** cases the nearest bone is a
finger/hand bone on the anatomical mirror of the one the published index names (`l-hand`→
close to `r-finger2`, `r-finger1`→close to `l-thumb1`, etc.). **[CONFIRMED — empirical.]**

**⚠ FLAGGED 2026-09-13 by §11.5.1, which refuted the population this verdict is built on
— not re-measured here, and not claimed to be wrong.** §11.5's cross-limb minority-weight
population is **0 / 34,442** under the corrected palette decode, so there is no longer a
"population §11.5 quantified" for these 10 clusters to be the dominant-strength tail *of*.
The clusters themselves, their 0.97–1.30 m published distances and the 8-of-10
anatomical-mirror observation are all measurements in their own right and are untouched by
this; what is withdrawn is the *explanation* offered below, because its stated mechanism no
longer exists. Re-examining §11.10/§11.11 under the corrected decode is the separately
tracked item in §11.15's open list and was deliberately not attempted by §11.5.1's pass
(whose brief was §11.5's own measurement only). **Do not cite the verdict below as settled.**

~~**Verdict: a mirror-image weight-painting artefact in the shipped hand/finger data, not a
decode-side reordering.** This is the dominant-strength tail of the population §11.5 already
quantified: 1,683 of 11,343 vertices (14.8%) carry a *minority* cross-limb weight, median
magnitude 0.145.~~ Ten small clusters here (14–80 vertices each, versus 60–90+ for the
well-behaved sibling fingers in the same meshes) carry that same mis-assignment strongly
enough — ≥ 245/255 — to become *dominant* rather than minority, and every one still lands,
to within a few centimetres, on a bone belonging to the opposite hand or forearm rather than
on any unrelated bone. Two of angel's four (41, 43) land on the opposite upper-arm-twist bones
rather than a sibling finger — close enough (2.7–7.5 cm) to be the same phenomenon reaching
slightly further up the mirrored limb, reported at lower confidence than the 8 finger-for-
finger swaps. **[CONFIRMED — empirical for the distances and the population match to §11.5;
HIGH CONFIDENCE — inferred for "same mechanism as §11.5's minority weights, now at dominant
strength," since the authoring step that produces it was not observed directly.]**

**Net effect on §11.4/§8.1: no change to either reading.** The blend-index-equals-bone-index
reading is not the cause and needs no correction — the fault is in the shipped vertex weights
themselves, at roughly 1% of vertices in the hand/finger region of these three meshes, not in
how the index is read. §11.7 item 4 is resolved on this half; the midline/toe slot named
alongside it in that item is a separate phenomenon, resolved separately in §11.11.

### 11.11 The midline slot that "resolves to a toe" is the head — and the mislabel is explained by the bone list's own construction order (2026-09-13)

**⚠ The "2 list positions short of its true bone" explanation is RETRACTED, 2026-09-13 — see §11.17.** Slot 15 is not near-miss by list position; `palette[15] = 17 = head` exactly. The measurement that slot 15's cluster centroid sits near `head` is correct and stands; the explanation for *why* is wrong.

**This is a different bone from the 10 slots in §11.10, and was never one of them.** §11.4's
`LAT = 0.04` lateral filter excludes it from the 78-slot population by design — its cluster's
mean X is 0.000–0.033 m, below the 4 cm threshold both there and in the candidate bone's own
rest X, because it sits on the body's midline. It is addressed here because §11.7 item 4 named
it alongside the 10, not because it is one of them.

**What it actually is.** On all three sample meshes, blend-index slot 15 carries the single
largest ≥245/255-dominant vertex cluster in the whole mesh: 542 (brad), 89 (angel), 667
(alfred) vertices. The published reading (slot k = bone k) names bone 15, which is `r-toe0`. A
whole-rig nearest-bone search of each cluster's mesh-space centroid against every bone in that
mesh's own rig:

| Mesh | n | Centroid (x, y, z), m | Nearest bone | Distance | Distance to published `r-toe0` |
|---|---|---|---|---|---|
| brad | 542 | (−0.033, 1.682, −0.036) | r-eye | 0.035 m | 1.691 m |
| angel | 89 | (−0.001, 1.625, −0.073) | head | 0.017 m | 1.633 m |
| alfred | 667 | (0.000, 1.668, −0.034) | l-eye | 0.057 m | 1.674 m |

`head` is the nearest or second-nearest bone on all three (0.017–0.141 m against `head`
itself); `r-toe0`, the bone the published reading names, is **1.63–1.69 m** away — roughly
three orders of magnitude worse than §8.1's own 10–14 cm expected residual. **This is the
mesh's head/face region, not a toe: slot 15 is 2 list positions short of its true bone,
`head` at index 17, not any distance away in space.** **[CONFIRMED — empirical, all three sample meshes.]**

**Why index 15 lands on the head: the bone list is a breadth-first, level-order traversal of
the skeleton tree — a structural fact about this format not previously recorded.** Walking the
same bone array §4 already documents, by parent index, level by level: level 0 is `root` (0);
level 1 is `pelvis, spine` (1–2); level 2 is `l-thigh, r-thigh, spine1` (3–5); level 3 is
`l-calf, r-calf, spine2` (6–8); level 4 is `l-foot, r-foot, l-clavicle, neck, r-clavicle`
(9–13) — `spine2`'s three children, in list order; level 5 is `l-toe0, r-toe0,
l-upperarmtwist1, head, r-upperarmtwist1` (14–18) — the children of level 4's five bones, in
the same order as their parents. This reproduces every one of indices 0–44 exactly, on all
three sample rigs, with no exception. **`head` is a level-5 bone because its parent `neck` is a
level-4 bone; it is not adjacent to the toes in the tree, but it is adjacent to them in this
list, because the toes (children of `l-foot`/`r-foot`, also level 4) are visited in the same
breadth-first wave, immediately before `neck`'s own child.** **[HIGH CONFIDENCE — inferred:
verified index-by-index against all 45 non-helper bones on three independent rigs; not
confirmed against the list-construction code itself, which was not read this pass.]**

**This does not generalise to a correction of the published reading elsewhere.** None of the
other scored slots in these three meshes — 68 of the 78 in §11.10's own accounting, including
the immediate neighbours `l-clavicle`/`neck`/`r-clavicle` (11–13) and `l-upperarmtwist1`/
`r-upperarmtwist1` (16, 18) that sit either side of this exact anomaly in the level-order list
— shows anything resembling a multi-metre miss. The mechanism above explains why this one
bone's list position looks short of where an anatomically-grouped ordering would put it; it is
not evidence of a broader index shift, and §11.10's own 10 mismatches have an unrelated cause.
**[CONFIRMED — empirical: the surrounding scored indices continue to satisfy reading A.]**

**Resolution of §11.7 item 4's second half:** the midline cluster is the head, correctly
readable at 2–14 cm under reading A once the search is not restricted to lateral slots; the
"toe" label was an artefact of the `LAT` filter's blindness to midline bones combined with
`r-toe0` genuinely occupying list position 15 — not a defect in the reading, and unrelated to
the §11.10 residual.

### 11.12 §11.7 items 2 and 3 — the bone-index limit identified as a caller-supplied subset (not the rig's own count); the `+0x40` pointer CONFIRMED as the same array; the source array's own ordering remains OPEN (2026-09-13)

**⚠ RESOLVED 2026-09-13 — item 3(b) is resolved by §11.15 (sign fix) and §11.16 (`source` is allocated by `FUN_004c79a0` as `limit` matrices in rig-bone order; §11.16.3: no per-frame writer of the pointer exists). The OPEN verdicts below are kept as the record of the search.**

> **Scope.** This section executes §11.7's items 2 and 3 together, since they are two ends of
> the same investigation: item 2 asks what `+0x04` of the object at registry-record `+0x3c`
> actually is; item 3 asks what object sits at Mesh header `+0x40`, and what order the
> gather's *source* array (registry-record `+0x00`) is indexed in. Both were investigated by
> disassembly in the dedicated Ghidra project copy `tools/gp_rig2`, using seven new scripts
> (`Rig117A.java`–`Rig117G.java`, in `tools/scripts/`). **Item 2 is answered with disassembly
> evidence: the count is a caller-supplied subset size, not the rig's bone count. Item 3(a) —
> what is at Mesh `+0x40` — is CONFIRMED and strengthens, rather than dissolves, the existing
> `+0x38`-array identification. Item 3(b) — the source array's own ordering — was pursued to a
> named, bounded stopping point and remains OPEN; the conflict item 3 raised is therefore
> sharpened, not resolved.**

#### 11.12.1 Item 2: `+0x04` of the record's `+0x3c` object is a caller-supplied subset size, not the rig's bone count

Raw disassembly of the two accessors themselves, confirming §11.3's account byte-for-byte:

```
FUN_004be9c0(selfIndex):                       FUN_004be910(selfIndex):
  if selfIndex == -1: return 0                   if selfIndex == -1: return 0
  if selfIndex >= DAT_0344c91a: return 0          if selfIndex >= DAT_0344c91a: return 0
  rec = selfIndex*0x3a0 + DAT_0344c924            rec = selfIndex*0x3a0 + DAT_0344c924
  if rec == 0: return 0                           if rec == 0: return 0
  if rec[+0x50] != selfIndex: return 0            if rec[+0x50] != selfIndex: return 0
  return rec[+0x00]                               return the dword at offset +0x04 of
                                                   the object rec[+0x3c] points to
```

`DAT_0344c924`/`DAT_0344c91a` are the registry's base pointer and count; stride `0x3a0`,
self-index re-validated at `+0x50` — exactly as §11.3 already stated. **[CONFIRMED —
disassembly, re-derived independently.]**

**The object at `+0x3c` is not the rig.** The rig pointer is a *different* field of the same
record, `+0x14` (set once at construction, `FUN_004c1730`, which stores its argument 1 there, and read
throughout the same family of accessors by following `record+0x14` to the rig object and then
reading that rig object's own `+0x24` for the rig's bone count and `+0x40` for its bone array —
both exactly the confirmed `.rig_pc` fields from ~~§2~~ §3.1/§4).
`+0x3c` points into a **separate global table**, `DAT_02f5e010`: 160 slots (`0xa0`), stride
`0x410` bytes, a live-count at `DAT_02f5e008`, looked up by a **linear scan matching a
caller-supplied ID against each slot's own `+0x00`** — not derived from the rig, the mesh, or
the registry record in any way. **[CONFIRMED — disassembly:** the lookup loop appears
identically in `FUN_004bcac0` (the function that *binds* one of these slots into a registry
record's `+0x3c`) and in `FUN_004c1730` itself (the registry-record constructor).**]**

**The slot's own constructor, `FUN_004c1350`, fills `+0x04` wholesale from a caller-supplied
argument block — never from the rig.** Reached through a 12-byte thin wrapper, `FUN_004bc9c0`
(`EDI = arg2; CALL FUN_004c1350`), which is the *only* path to `FUN_004c1350` in the whole
binary. `FUN_004c1350`, reading the register-passed block (`{u32 id; s32 count; u32*
recordArrayPtr}`):

```
slot = <first free entry, id == -1>
memset(slot, 0xFF, 0x410)
slot[+0x00] = block.id
slot[+0x04] = block.count                     // <-- item 2's target field
for i in 0 .. block.count-1:
    boneIdx = *(u32*)(block.recordArray + i*4)          // low u16 is the meaningful part
    slot[+0x10 + i*2]        = (u16)boneIdx             // slot i -> raw rig bone index
    slot[+0x110 + boneIdx*2] = (u16)i                   // raw rig bone index -> slot i  (reverse)
    slot[+0x210 + i*4] = 0
```

**`block.count` is supplied by the caller, byte for byte — no rig object is consulted to
produce it.** **[CONFIRMED — disassembly, `FUN_004c1350` read in full, register-passed block
pinned from the raw instructions at the one real call site, `0x004bc9c5`.]**

**Every other consumer of this object treats its count as a subset, never as the rig's full
bone count:**

- `FUN_004bcb30` (bone-name **retargeting** between two registered instances) explicitly bounds
  it: `subsetCount <= rig->+0x24 (bone count)`, then walks slots `0..subsetCount-1`, reading
  each slot's raw bone index from `+0x10`, looking that bone's **name** up in the current
  rig's own bone-name array (`rig+0x40`, the confirmed `.rig_pc` field, §4/§5), searching for
  the same name in a *second*, independently-registered instance's rig, and writing that
  second instance's own subset-relative slot (via *its* `+0x110` reverse lookup) into the
  first instance's `+0x34` array. This is a `<=` bound the code checks and relies on, never an
  equality. **[CONFIRMED — disassembly.]**
- `FUN_004be8b0` (`selfIndex, slotIndex → parent's slotIndex`) walks the **raw** rig hierarchy
  through the bone record's own confirmed parent-index field (`+0x20`, §4) to find the raw
  parent bone index, then converts it back into **slot space** through the same object's
  `+0x110` reverse array. Input and output are both slot-space; the object's own internal
  indexing is never the rig's raw bone order. **[CONFIRMED — disassembly.]**
- By contrast, `record+0x34` (a *different* field of the registry record itself, not of the
  `+0x3c` object) is a full-bone-count array, addressed directly by raw bone index and bounds-
  checked against `rig->+0x24` (`FUN_004bcca0`/`FUN_004bccf0`). The project's two array
  families sit side by side and are not interchangeable: one is subset/slot-sized, the other
  is rig-sized. **[CONFIRMED — disassembly.]**

**The construction path is a generic, reused mechanism, not something specific to the anim
registry.** `FUN_004bc9c0` (the wrapper into the slot constructor) has exactly **four callers
project-wide**:

| Caller | What it is |
|---|---|
| `FUN_00ab0730` | **Already documented** — `spec-vehicle-geometry.md`'s vehicle **part → rig-bone binding** function (`HANDOFF.md` §2b). |
| `FUN_009f36d0` | A mesh/rig construction routine. A few dozen lines before the `FUN_004bc9c0` call it runs the `.ccmesh_pc` payload chain (`FUN_007527b0`→`FUN_007524f0`), then resolves a rig handle via `FUN_004bc810` (the `"Rig"` cache-check/handle resolver, `HANDOFF.md` §2a) from data embedded in that just-parsed mesh payload, then immediately builds the `{id, count, array}` block and registers both the subset slot and the animation-instance record (`FUN_004c1730`). **This mechanism runs as one step of ordinary mesh loading.** The two source fields feeding the block (read at `+0xA` and `+0x20` of the parsed mesh object) were **not** pinned to the already-catalogued Mesh-header `+0x38`/`+0x40` skinnable-bone array — they are read from a different level of the parsed object than the Mesh sub-block struct itself (§11.12.2 below establishes that struct's own layout precisely, and `+0xA`/`+0x20` do not match it), so that specific numeric identification is deliberately **not** claimed. |
| `FUN_00753d40`, `FUN_008cb170` | Each builds the identical `{id, count-at-`+0xA`, array-at-`+0x20`}` block from a small input structure; not otherwise characterized this pass. |

**Answer to item 2:** `+0x04` of the object at registry-record `+0x3c` is **not the rig's bone
count** — it is a caller-supplied **subset size**, one instance of a generic "named-bone-list
binding" mechanism that the vehicle loader also uses for part→bone binding. **[CONFIRMED —
disassembly, for what the field is and is not.]** Which specific resource supplies that count
on the character-mesh path was not pinned to one named file field within this pass's budget —
named as the natural next step below (§11.12.3).

#### 11.12.2 Item 3(a): Mesh header `+0x40` CONFIRMED as the fixed-up pointer of the *same* `+0x38` array — strengthens, not weakens, the existing identification

Read directly from the `.ccmesh_pc` "Mesh" sub-block parser, `FUN_00e71410` (excerpt, offsets
exactly as in the disassembly):

```
if (flags & 2) == 0:                         // flags bit 1 clear
    mesh[+0x40 .. +0x47] = 0
elif *(u16*)(mesh+0x38) == 0:                 // the count is zero
    mesh[+0x40 .. +0x47] = 0
else:
    mesh[+0x40] = cursor + base                // <-- the fixed-up pointer, unconditional
    mesh[+0x44 .. +0x47] = 0
    cursor += *(u16*)(mesh+0x38)                // advance by the count, IN BYTES
```

`mesh+0x38` is the same `u16` element count already established (`spec-vertex-format.md`'s
walk advances the cursor by exactly this many bytes when flags bit 1 is set — the mesh's
skinnable-bone list, §11.4). **`mesh+0x40` is unconditionally that same array's fixed-up
absolute pointer, written 8 bytes after its count, in the same branch, by the same parser
statement.** There is no second, different object at `+0x40` to confuse it with. **[CONFIRMED
— disassembly, `FUN_00e71410` read in full.]**

This lines up exactly with the gather's own table-struct shape, already pinned from raw
instructions in §11.3 (`u16` element count at the table structure's own `+0x00`, pointer to a
byte array at the table structure's own `+0x08`): placing that structure's origin at
`mesh+0x38` makes its count field land on `mesh+0x38` and its pointer field land on
`mesh+0x38+0x08 = mesh+0x40` — precisely what was just read in the parser. **The table's
address is `mesh+0x38`, confirmed from both ends: the parser that writes it and the callee
that reads it.**

This is independently re-derived at **both** established gather call sites, not merely inferred
once:

- `FUN_00e649f0` (already documented, §11.3): reaches the table through the skinned instance's
  own `+0xbc → +0x30 → +0x38` chain — that trailing `+0x38` is this exact pair, reached from a
  completely different pointer chain than the one below.
- `FUN_00b91a10` (re-read fresh this pass with its register-argument hazard fully resolved —
  the whole function decompiles cleanly once the self-index accessors are recognised):
  ```
  meshPtr = the pointer reached by following renderEntry+0x5c → +0x64 → +0x50 → +0x30
            (four successive pointer hops)
  table   = (meshPtr == 0) ? 0 : meshPtr + 0x38
  FUN_00e7aff0(source, limit, table)
  ```
  the exact same `mesh+0x38`/`mesh+0x40` landing point, reached through the render-entry chain
  instead of the skinned-instance chain.

**Answer to item 3(a):** the object at Mesh header `+0x40` is the mesh's skinnable-bone-list
array's own fixed-up pointer — the *same* array §11.4 already characterised, not a different
one. The identification "the gather's table is the mesh's `+0x38` array" is raised from
**HIGH CONFIDENCE — inferred** to **CONFIRMED — disassembly** at the structural/address level.
**This tightens item 3's conflict rather than resolving it: the one alternative that could
have dissolved the conflict for free — that `+0x40` names some unrelated object and the
original identification was simply mistaken — is now closed off.** Whatever reconciles "the
table is definitely the skinnable-bone list" with "blend index equals rig bone index directly"
(§11.4) must therefore live on the *source* side of the gather, which is item 3(b).

#### 11.12.3 Item 3(b): the source array's own ordering — pursued to a named stopping point, OPEN

**⚠ RESOLVED 2026-09-13 — item 3(b) is resolved by §11.15 (sign fix) and §11.16 (`source` is allocated by `FUN_004c79a0` as `limit` matrices in rig-bone order; §11.16.3: no per-frame writer of the pointer exists). The OPEN verdicts below are kept as the record of the search.**

**Predicate, stated before searching.** A positive finding is the function that writes a
non-null value into a registry record's own `+0x00` (the pointer `FUN_004be9c0` returns,
confirmed via raw instructions in §11.3 to be the gather's `source` argument), read closely
enough to see whether its fill loop indexes by raw rig bone index, by the item-2 subset's own
slot index, or by something else (e.g. an animation track index, per the candidate this item
was opened on, drawn from `spec-anim-format.md` §6b/§10.1's track↔bone reverse lookup). A
negative finding is a documented, bounded search — stated as a claim about the search, not
about the format — that reaches no such writer.

**What was tried.** `FUN_004be9c0` has exactly **7 callers project-wide** — verified by a
full-binary reference search, deliberately wider than item 2's ~123-function set (which only
covers functions that re-derive the self-index via the registry's own two globals directly; a
per-frame pose writer need not do that at all if it already holds the record pointer, which is
exactly why it could sit outside that set). None of the ~123 direct accessors of
`DAT_0344c924`/`DAT_0344c91a` writes a non-zero value into a record's `+0x00` — the only writes
found there are the constructor's own zero-initialisation (`FUN_004c1730`, twice). The generic
accessor that supplies the self-index at the skin-gather call site itself, `FUN_00487e80`
(which simply returns the dword at `+0x20` of its argument), has **198 callers project-wide** and carries no discriminating power
as a search anchor — the exact "small struct displacements have no selectivity" trap
`HANDOFF.md` §5 already names. **[CONFIRMED — disassembly, both negative counts.]**

Of `FUN_004be9c0`'s 7 real callers:

| Caller | Finding |
|---|---|
| `FUN_00b91a10` | The already-documented skin gather (§11.3, re-confirmed in §11.12.2 above). |
| `FUN_0073f650` | Feeds the **identical** `(FUN_004be9c0, FUN_004be910)` pair straight into `FUN_00e649f0(source, limit)` — the *other* already-documented gather call site. **New confirmation, not previously stated**: both established gather entry points draw the same `(source, limit)` pair from the same two accessors, so `record+0x00` is genuinely one shared skin-palette source used identically by both paths, not two coincidentally-similar arrays. **[CONFIRMED — disassembly.]** |
| `FUN_00782880` | ~~An unrelated per-slot smoothing/lag computation running over the *same* item-2 subset object (calls `FUN_004be910` for its loop bound and `FUN_004be8b0` per slot). It indexes its own 48-byte-stride buffer with the **slot-space** value `FUN_004be8b0` returns — suggestive that slot/subset-local ordering is the house convention for buffers this subsystem owns — but that buffer (a stack-allocated buffer at `[ESP+0x38]` inside `FUN_00782880`) was not confirmed to be `record+0x00` itself, so this is a **lead, not a proof**.~~ **RESOLVED 2026-09-13, see §11.13.1: that `[ESP+0x38]` stack buffer is `record+0x04`, not `record+0x00` — REFUTED by raw disassembly, not merely unconfirmed — and the whole call graph it sits in (`FUN_00782880`→`FUN_00781e70`→`FUN_004bccf0`→`FUN_004c7640`) is positively identified as the attachment/prop spring-lag subsystem (§6 of this document), reading `record+0x04` as its own smoothing-state cache. Not a lead into the skin palette. |
| `FUN_0058b5b0` | Indexes `record+0x00` with stride `0x30` (48 bytes, matching the palette element size, §11.2) using a plain integer parameter whose own provenance (bone index vs. something else) was not traced back to source at the caller. |
| `FUN_00784a60`, `FUN_0094e6b0`, `FUN_009edc20` | Each is a large function (1.2–4.7 KB) that reuses the same local variable for several unrelated purposes; where it touches `record+0x00`-shaped data, it does so either by a sequential `+0x30` walk across consecutive entries (which visits every entry without revealing what index 0 means) or via large fixed offsets belonging to an unrelated object once the variable is reassigned. Read in full; neither settles bone-order vs. subset/track-order. |

**Answer to item 3(b): OPEN.** The fill loop for `record+0x00` was not located within this
pass's traced call graph. **Item 3's conflict is therefore not resolved** — §11.12.2 removed
the one competing explanation that could have dissolved it for free, which sharpens rather than
closes the question. **Until the source array's own producer is read, the identification of
the gather's table with the mesh's `+0x38` skinnable-bone array stays exactly where §11.7
left it: CONFIRMED at the structural level (§11.12.2), with the addressing conflict against
§11.4's direct-index reading still standing.**

**Next step, named:** both established gather call sites (`FUN_00e649f0`, `FUN_00b91a10`) are
themselves called from a higher-level per-frame "update this skinned instance" driver that
necessarily already holds the registry record (it is the thing calling the gather at all) —
that driver, not a fresh search from the registry's own globals, is where `record+0x00`'s fill
loop will actually be sitting; §11.12's search stopped at the gather call sites and their
sibling accessors precisely because going further meant leaving the traced, named surface this
pass covered. A second, independent angle worth trying first: confirming or refuting whether
`FUN_00782880`'s stack buffer at `[ESP+0x38]` (§11.12.3's lead) is literally `record+0x00` would
settle the ordering question immediately in either direction, without needing the fill loop at
all.

**Both of the above executed 2026-09-13 — see §11.13.** The `[ESP+0x38]`-buffer lead is REFUTED
(not merely unconfirmed) and positively explained; the higher-level driver for both gather
call sites was traced one level further. Item 3(b) itself stays **OPEN** — the search is
bounded further, not closed.

### 11.13 §11.12.3's two named next steps executed — the live lead identified and ruled out (attachment/prop lag, not skinning), both driver chains traced one level further, item 3(b) still OPEN (2026-09-13)

**⚠ RESOLVED 2026-09-13 — item 3(b) is resolved by §11.15 (sign fix) and §11.16 (`source` is allocated by `FUN_004c79a0` as `limit` matrices in rig-bone order; §11.16.3: no per-frame writer of the pointer exists). The OPEN verdicts below are kept as the record of the search.**

> **Scope.** This section executes, in the priority order §11.12.3 itself named, its own two
> named next steps: (1) confirm or refute whether `FUN_00782880`'s stack buffer at `[ESP+0x38]`
> is literally `record+0x00`; (2) if not, trace the higher-level per-frame "update this skinned
> instance" driver for both established gather call sites. Both were executed to a definite
> conclusion in the same Ghidra project copy, `tools/gp_rig3`, using seventeen new scripts
> (`Rig1213A.java`–`Rig1213Q.java` in `tools/scripts/`). **Step 1 is
> REFUTED, and — going beyond a bare refutation — the lead is positively identified: the
> whole call graph it sits in is the attachment/prop spring-lag subsystem, not the skinning
> palette. Step 2 is executed against both established call sites' own drivers, one level
> deeper than §11.12.3 reached, plus a wider re-run of the registry-accessor sweep (now
> including an indirect/bulk-copy write pattern the original sweep's predicate could not
> catch). No writer of `record+0x00` was found anywhere in either search. Item 3(b) stays
> OPEN — this is a bounded negative, stated as a claim about the search.**

#### 11.13.1 Step 1: the stack buffer at `[ESP+0x38]` is `record+0x04`, not `record+0x00` — REFUTED, and explained rather than left dangling

Raw disassembly of `FUN_00782880`'s entry (`0x00782909`–`0x00782924`) pins down what the
decompiler had misrepresented as a single call, `FUN_00487e80(&buf)` (`buf` being the stack
buffer at `[ESP+0x38]`), for what it actually is — two back-to-back calls whose arguments the
decompiler misattributed (the same register-passed-argument hazard already on file,
§11.3/~~§11.9.1~~/`HANDOFF.md` §5 **[no §11.9.1 exists; §11.9 item 1 is a different hazard]**). In instruction order: the address of the `[ESP+0x38]` buffer is
loaded into ECX and pushed as the **second** argument to the *next* call, not to
`FUN_00487e80`; ECX is then reloaded from `[EDI+0x10]`, the real `thiscall` receiver for
`FUN_00487e80`; that call returns the value at `ECX+0x20` (`selfIndex`) without touching the
stack; `selfIndex` is pushed as the first argument (`cdecl`, right-to-left, so the last-pushed
argument is read first); and the call that follows is `FUN_004be950(selfIndex, &buf)`.

`FUN_004be950` was disassembled directly (`0x004be950`–`0x004be988`) and shares
`FUN_004be9c0`'s exact validated preamble byte-for-byte (selfIndex bounds-check against
`DAT_0344c91a`, `rec = selfIndex*0x3a0+DAT_0344c924`, `rec[+0x50]==selfIndex`) before
diverging at the very last step:

```
FUN_004be9c0(selfIndex):            FUN_004be950(selfIndex, int *outPtr):
  ... identical validated rec ...     ... identical validated rec ...
  return rec[+0x00]                   *outPtr = rec[+0x04]
                                       returns a 32-bit value whose low byte is
                                       rec[+0x38] (the upper 24 bits are simply
                                       carried over from another register, not
                                       meaningful)
```

**The `[ESP+0x38]` buffer equals `rec[+0x04]` — a field four bytes away from, and distinct from, `rec[+0x00]`
(the gather's confirmed `source`). [CONFIRMED — disassembly, direct comparison of the two
accessors' validated preambles and final step.]**

This is corroborated, not merely asserted, by the record's own lifecycle. The constructor
(`FUN_004c1730`, already on file as the source of the "zero-init, twice" note in §11.12.3)
zeroes `rec[+0x00]` and `rec[+0x04]` as **two separate
statements**, and does so **twice** in its own body (once early, once again just before the
`+0x3c` slot lookup) — the "twice" was always about this *pair*, not one field zeroed
redundantly. A second function, `FUN_004c14f0`, was identified in this pass as the record's
**release/unregister** counterpart: it walks the anim-registry hash chain
(`DAT_0344c918`/`DAT_0344c920`/`DAT_0344c922`, the same `*0x3a0+DAT_0344c924` rec
computation) and, on release, explicitly zeroes `rec[+0x00]`, `rec[+0x04]` **and** `rec[+0x08]`
as three separate `MOV dword ptr [ESI+N],EBX` statements (`EBX` held `0` throughout),
alongside a byte flag at `+0x38`, before running a completely unrelated doubly-linked-list
unlink over a second, smaller object family (the `+0xc`/`+0x6d` fields `FUN_004bd700`
separately reads). **[CONFIRMED — disassembly: three explicitly separate fields, zeroed
identically at construction and at release — not one buffer under two names, and not
aliased.]**

**Having refuted the lead, this pass did not stop at the refutation — the stack buffer passed as
`rec[+0x04]` was traced to what it actually is.** `FUN_00782880`'s early-exit branch (taken when
`*(context+0x15b0) != 0`) calls `FUN_00781e70` directly with a fixed placeholder vector; that
same function, `FUN_00781e70`, is *also* the **sole caller** of `FUN_004bccf0`
(`getReferencesTo` returns exactly one hit, `0x00781e70`), which is itself a thin
selfIndex-validating wrapper that forwards the record plus its own arguments 2–5 into
`FUN_004c7640` — one of the ten candidates the wider sweep below surfaced. Reading
`FUN_00781e70` in full settles what this whole call graph is: it walks a **fixed** list of
eleven small integer constants (`{2,3,4,5,6,7,8,9,10,11,0}` — not derived from any mesh's
skinnable-bone list) against a per-instance context object's own `+0x1410`/`+0x1310` arrays
(the *same* context and the *same* two field offsets `FUN_00782880` itself reads,
`0x0077b0e0()`'s return value), builds a filtered index table of the *active* ones, and — for
each — either reads a physics/attachment transform directly or asks `FUN_004bccf0`/
`FUN_004c7640` for a **spring-damper-smoothed** version of it (`FUN_004c7640` reads
`rec[+0x04]` as its own previous-frame smoothing-state cache, indexed by that same filtered
table, and writes the smoothed result into *the caller's own local output arrays* — never back
into `rec[+0x00]` or `rec[+0x04]`), then **hands the result to a virtual call, dispatched
through the function-pointer table reached via `attachmentObj+0xe0`** (slot `+0x40` or `+0x44`,
chosen by a condition) on a per-slot attachment object. **This is the attachment/IK-target lag subsystem already named in §6 of this same
document (`0x40`-byte attachment record) — camera, weapon, prop and similar helper bones —
not the skin palette.** **[HIGH CONFIDENCE — inferred from the full read of `FUN_00781e70`
and `FUN_004c7640`; the fixed constant list, the shared context object, and the
attachment-record-shaped virtual dispatch all point the same way, but the vectorised
spring-damper arithmetic itself was not verified instruction-by-instruction the way the
offset comparisons above were.]**

**One further fact surfaced inside `FUN_004c7640`, offered as context and explicitly not as
evidence about `rec[+0x00]`'s ordering:** it calls `FUN_004c31b0(rec, ...)` (also bare-`rec`,
also in the ten-candidate list below), which reads `rec[+0x14]` (the confirmed rig pointer),
`rec[+0x3c]` (the confirmed subset object) and — new this pass — `rec[+0x30]` (a *second*
self-index, into the *same* registry, read to fetch **another instance's** `+0x04` field) and
`rec[+0x34]` (a retarget slot-map array, matching `FUN_004bcb30`'s already-documented
cross-instance retargeting, §11.12.1). Inside it, the per-slot loop indexes its own bind-pose
input array by **subset slot index** (a stack-local counter using the same 0…N−1 numbering
`FUN_004c1350` assigns), not by raw bone index. **This says something about the attachment-lag subsystem's
own source data — slot-ordering, cross-instance retargeting via a second self-index — not
about `rec[+0x00]`, which this function never touches. Recorded because it is new and
because a future pass chasing `rec+0x30`/`rec+0x34` should not have to re-derive it, not as
an answer to item 3(b).** **[CONFIRMED — disassembly, for the four offsets read;
uncorrelated with the skin gather's own source array.]**

#### 11.13.2 Step 2: both established drivers traced one level further; a wider registry-accessor sweep; still no writer

**`FUN_00b91a10`'s one and only caller was invisible to a direct reference search because
Ghidra had never carved a `Function` out of the bytes containing it — not because no caller
exists.** `getReferencesTo` on `FUN_00b91a10` returns exactly one hit, `0x0070314d`, and
`getFunctionContaining` returns `null` there; the surrounding bytes (`0x0070300d`–
`0x00703199`, ending in a clean `RET`, immediately preceding a separate, already-recognised
`FUN_007031e0`) disassemble as one coherent function body with no gaps. A `Function` was
created there (`createFunction`, name `FUN_007030d0`, in this session's own `gp_rig3` copy
only) and decompiled: it is a **top-level per-frame world-update tick**, gated by three calls
to `FUN_00706ab0()` excluding game-mode/kind values 7, 8 and 9, that then calls roughly
eighteen unrelated subsystem-update functions in a fixed sequence — physics-list maintenance,
audio, particle/prop culling, and, unconditionally among them, `FUN_00b93540()` immediately
followed by `FUN_00b91a10()` (the gather) and then `FUN_00823310()`/`FUN_00bdd520()`.
**All three of these immediate neighbours were opened and are unrelated to the registry**:
`FUN_00b93540` walks a `0x60`-stride free-list releasing entries and a separate linked list
calling `FUN_00b929b0()`; `FUN_00823310` maintains two unrelated culling/pool arrays
(`DAT_01300b98`/`PTR_DAT_01300b90`, stride 4, and `DAT_022d6168`/`DAT_022d6160`, stride
`0x58`); `FUN_00bdd520` walks a `0x128`-stride registry unrelated to `DAT_0344c924`
dispatching to `FUN_00bdd470`/`FUN_00bdc730`. **[CONFIRMED — disassembly/decompilation, all
three.]**

`FUN_007030d0` is itself registered as one of four items passed together to
`FUN_00706a40(5, &array, 0)` from `FUN_007041b0` (`FUN_00706a40` is a plain 5-slot table
setter, `DAT_01503bc0`-family, gated on argument 1 being below `0xb` — a small fixed roster of registration
slots, not specific to animation). The other three items in that same array —
`LAB_00702f90`, `FUN_00703c00`, `FUN_00703f50` — were checked and are **UI/map-load
transition lifecycle code** (`"ui_map_world_city"` string references, checkpoint/loading-state
flags such as `DAT_014ff6c1`–`DAT_014ff6c6`), not per-frame animation sub-steps — ruling out
the hypothesis that the fill loop is a sibling phase-step registered alongside the tick.
**[CONFIRMED — decompilation, both.]**

**`FUN_00b91a10`'s full body — previously only a three-line excerpt was on file
(§11.3/§11.12.2) — reveals a structural fact about the consumer side not previously
recorded.** It is not a per-entity render callback; it is a single **global per-frame batch
pass** with a nested loop: an outer loop over up to `DAT_0290ac9c` render entries (base
`DAT_0290acb0`, stride `0x888`), and — for each — an **inner loop of exactly five skin
slots** (`EBP+0x60`, stride `8` bytes: `slot[+0x00]` a pointer to that slot's own
self-index-holder object, read via `FUN_00487e80` for the gather's selfIndex; `slot[+0x04]`
the destination palette pointer, the gather's own `thiscall` receiver). The
skinnable-bone-list `table` (`renderEntry+0x5c→+0x64→+0x50→+0x30→+0x38`) is read once per
*outer* iteration and reused for all five inner slots. **This means one render entry can carry
up to five independently-skinned sub-parts, each with its own animation-instance self-index
and its own destination palette, sharing one skinnable-bone-list table** — new detail,
corroborated raw-instruction-by-instruction, that corrects the implicit "per-entity callback"
framing this document previously carried for this call site. **[CONFIRMED — disassembly,
`0x00b91a10`–`0x00b91ae8` read in full.]** It remains, however, purely a **consumer** of
`rec[+0x00]` — no write to it appears anywhere in this loop.

**The other established call site's own driver was traced the same distance.**
`FUN_0073f650` (site A) has two resolved callers, `FUN_009226b0` (new this pass) and the
already-known `FUN_0094e6b0`, plus two call sites (`0x009022f3`, `0x009024bd`) that — like
`FUN_00b91a10`'s own caller — sit in a second Ghidra analysis gap, one coherent uncarved
function spanning roughly `0x00902070`–`0x009026ec+` that calls `FUN_0073f650` **twice**
(a "cheap path" using a fixed transform, and a "blended path" that interpolates against a
per-bone rest pose first). `FUN_009226b0` was decompiled in full: a per-object animation-rate/
LOD gate (walks a "kind" flag at `+0x3a`/`+0x3b`, a vtable call at `+0x54`) that calls
`FUN_0073f650` once at its tail — unrelated to the registry's own fields otherwise.
**[CONFIRMED — decompilation.]** Neither of these two call chains was left unread; neither
contains a write to `rec[+0x00]`.

**The registry-accessor sweep from §11.12.3 was re-run wider, not merely re-counted.** All
123 functions referencing `DAT_0344c924` were fully disassembled (not sampled) and scanned
two ways: (a) any register-relative write at a small, literal displacement — this reproduces
§11.12.3's own negative and additionally surfaces only already-characterised getters/
out-param setters (`FUN_004bd700`, another `record[+0xc]` accessor sharing the same
validated preamble) and the release path (`FUN_004c14f0`, §11.13.1); (b) the bare validated
`rec` pointer (offset **zero**, not `rec+N`) pushed and passed whole into a further `CALL` —
added this pass specifically because a bulk-copy/`memcpy`-style fill would write through a
*callee's* body, not via a `MOV [reg],val` at the call site itself, and so would be invisible
to predicate (a). Predicate (b) found exactly **ten** sites in ten distinct functions.
`FUN_004b0080` and `FUN_004b2280`'s hits are writes to unrelated physics/blend objects at the
same generic `[reg]`/`[reg+4]` shape (false positives of the broad register-only match);
`FUN_004becb0` writes into a *caller-supplied* output array, not the record; the remaining six
distinct call targets (`FUN_004c64d0`, `FUN_004c6540`, `FUN_004c2170`, `FUN_004c6b30`,
`FUN_004c58c0`, `FUN_004c5d40`, `FUN_004c6830` — one first surfaced via `FUN_004be660`) were
each decompiled and are small helper functions over unrelated `0x50`-stride
attachment/physics sub-arrays (two shown in full above); **`FUN_004c7640`/`FUN_004c31b0`
are the one substantive hit, and they are the attachment-lag subsystem identified in
§11.13.1 — already resolved, not a second open lead.** **[CONFIRMED — disassembly, the full
ten-site enumeration and every target's disposition.]**

#### 11.13.3 Net verdict

**⚠ RESOLVED 2026-09-13 — item 3(b) is resolved by §11.15 (sign fix) and §11.16 (`source` is allocated by `FUN_004c79a0` as `limit` matrices in rig-bone order; §11.16.3: no per-frame writer of the pointer exists). The OPEN verdicts below are kept as the record of the search.**

**Item 3(b) stays OPEN.** Both of §11.12.3's own named next steps were carried to a definite
conclusion: step 1 is a clean REFUTE, with the lead positively re-identified rather than left
unexplained (attachment/prop spring-lag, §11.13.1); step 2's search is now bounded one level
deeper on **both** established call sites' own driver chains, plus a registry-accessor sweep
re-run with a strictly wider write-detecting predicate than §11.12.3 used. No function
anywhere in either search writes a non-null value into `record[+0x00]`. **This is stated as a
claim about the search — the traced call graph from both gather call sites up through their
own top-level per-frame drivers, all 123 direct accessors of the registry base pointer under
two independent write predicates, and the one substantive lead this pass's wider predicate
surfaced, run to ground — not as a claim that no such writer exists anywhere in the roughly
2,900-function binary **[⚠ sic: the binary has ~41,785 functions — `spec-world-streaming.md`, `spec-zone-data-format.md`]**.**

**Next step, named for whoever picks this up, ranked above another registry-accessor
sweep:** this pass found **two** coherent function bodies that a plain `getReferencesTo`-then-
decompile sweep silently skips because Ghidra's own analysis never carved a `Function` out of
them (`0x0070300d`–`0x00703199`, now named `FUN_007030d0` in `gp_rig3` only; the
`0x00902070`-ish region, still unnamed). Both happened to be the *drivers* this task went
looking for, found only because their single call to a *known* address (`FUN_00b91a10`,
`FUN_0073f650`) was followed manually rather than trusted as "0 callers." If the fill loop
for `record[+0x00]` itself sits in a similarly uncarved region, it would be **invisible to
`getReferencesTo` on any of this record's known accessors** regardless of how wide the
predicate is made — the productive next move is a targeted scan for uncarved-but-coherent
code (a `RET`-to-`RET` byte range with no owning `Function`) inside the same modules already
implicated (`0x0070xxxx`, `0x0090xxxx`, `0x004bxxxx`–`0x004cxxxx`), not a third pass over the
same 123 named accessors.

### 11.14 §11.13's own next step executed — a structural scan for uncarved-but-coherent code across the three implicated ranges; clean negative, recommend deprioritizing (2026-09-13)

> **Scope.** This is the third dispatched pass on item 3(b) (the skin gather's source array,
> `record[+0x00]`, and who fills it). Passes 1 and 2 (§11.12.3, §11.13) each produced a clean,
> bounded negative and are not repeated here — not the 123-accessor sweep, not the 7-caller
> enumeration, not `FUN_00782880`'s call graph. §11.13 named one untried angle: a **targeted
> scan for uncarved-but-coherent code** (a `Function`-shaped byte range with no owning
> `Function` object) inside the three address ranges this investigation has already implicated
> — `0x0070xxxx`, `0x0090xxxx`, `0x004bxxxx`–`0x004cxxxx` — rather than a fourth registry-
> accessor sweep. This section executes exactly that, using a fresh, independent Ghidra project
> copy (`tools/gp_rig4`) and thirteen new scripts (`RigGap1.java`, `RigGap2.java`,
> `RigGap3.java`, `RigGap4c.java`, `RigGap5.java`, `RigGap700.java`, `RigGap700b.java`,
> `RigGap700c.java`, `RigGapScan.java`, `RigGapScan2.java`, `RigGapScan3.java`,
> `RigGapBatch.java`, `RigGapBatch2.java`, in `tools/scripts/`). **Predicate, stated before
> searching (same as
> §11.12.3/§11.13): a positive finding is a function that writes a non-null value into
> `record[+0x00]`, read closely enough to tell whether its fill loop indexes by raw rig bone
> index, the item-2 subset's own slot index, or an animation track index. A negative finding is
> a bounded claim about the search — which ranges, what predicate, what was found instead — not
> a claim about the format.** **Net result: every function this pass carved or read — 36 newly
> created plus the full callee set of the driver at the centre of it — was checked by direct
> reference to the registry's own two globals (`DAT_0344c924`/`DAT_0344c91a`) and none touches
> them. This is a clean negative. Recommendation at the end: deprioritize item 3(b) rather than
> dispatch a fourth pass.**

#### 11.14.1 The second known gap, precisely bounded: `0x00902140`–`0x00902756`, a third driver of the site-A gather — not a writer

§11.13.2 left this gap approximately located (`~0x00902070`–`0x009026ec`) and only partially
read (it knew the gap called `FUN_0073f650` — site A's own driver — twice, and nothing more).
Precise boundaries, from raw disassembly: the existing `FUN_00902070` (already a named
function) ends cleanly at `0x00902137` (`RET`); real code resumes immediately at `0x00902140`
with a standard `thiscall` prologue (`SUB ESP,0x16c; PUSH EBP; PUSH ESI; MOV ESI,ECX`) and ends
with a shared epilogue (`POP EDI; POP ESI; POP EBP; ADD ESP,0x16c; RET`, three exit paths, two
of which tail-jump instead) at `0x00902756`, immediately followed by the already-named
`FUN_00902760`. **One coherent function, no internal gaps. [CONFIRMED — disassembly,
`0x00902140`–`0x00902756` read in full, byte-for-byte.]**

A `Function` was created there (`createFunction`, named `FUN_00902140`, in `gp_rig4` only) and
decompiled in full. It is a per-instance "compute this bone-like thing's transform" method
(argument 1 = a large render/animation-instance object reaching fields past `+0x190`, sharing
the exact `+0xf4`-subobject/`FUN_00d9e4c0`/`FUN_00d9e210`/`FUN_00d9e250` rate-scaling idiom
`FUN_00782880` uses — the attachment/prop spring-lag subsystem's own object family, §11.13.1 —
**not** the 0x3a0-stride registry record). Depending on a flag it either builds a **cheap**
fixed transform from constant data or interpolates a **blended** one against a rate computed
from an animation-instance handle, then calls `FUN_0073f650` with pointers to dwords `0x10` and `0x13` of argument 1 (plus further arguments)
— confirming §11.13.2's "cheap path... blended path" characterisation exactly. **[CONFIRMED —
decompilation, full body.]**

A direct, mechanical scan of every instruction in `FUN_00902140`'s body for a reference to
`DAT_0344c924` or `DAT_0344c91a` (the registry's own base-pointer/count globals) found **zero**
hits. Its full call list — `FUN_004d7f30`, `FUN_0091f5e0`, `FUN_00d9e4c0`, `FUN_00d9e210`,
`FUN_0073f650`, `FUN_0074e840`, `FUN_004bd700`, `FUN_0074e490`, `FUN_00ea23b0`,
`FUN_0074e710`, `FUN_00e65770`, `FUN_00456920`, `FUN_00d9e250`, `FUN_00752c70`,
`FUN_00e649a0`, `FUN_008cc1a0`, `FUN_00e63120`, `FUN_008cc950`, `FUN_00e39b50`,
`FUN_008fba80` — was each decompiled or resolved: `FUN_00e649a0` is a small array filter
(bounds-checks a list of raw ids against a mesh-header count, unrelated stride/shape to the
registry record); `FUN_00456920` is a 2-int equality predicate; `FUN_004bd700` operates on the
already-catalogued `+0xc`/`+0x6d` object family (§11.13.1), not the registry record; the rest
are generic lock/notify/lookup helpers already named in §11.13's own traces or clearly unrelated
by signature. **None references `DAT_0344c924`/`DAT_0344c91a`, and none receives a pointer
shaped like the validated `rec` (bounds-checked against `DAT_0344c91a`, strided `0x3a0` off
`DAT_0344c924`, re-validated at `+0x50`). [CONFIRMED — disassembly/decompilation, all 20 call
targets.]**

**New fact, beyond what §11.13.2 had:** `FUN_0073f650` was read in full for the first time this
pass (previously only excerpted). It does two things, not one: (1) it writes a computed
translation/rotation into **an object reached through the third entry of its own local
pointer array** — an object at the attachment-record family's own offsets (`+0x28`/`+0x30`/`+4`/`+0xc`/`+0x14`/`+0x1c`/`+0x24`/`+0x34`/`+0x70`/`+0x60`/`+0x68`/
`+0x6c`, the *identical* write pattern `FUN_00902140` itself uses on the object at dword 100
of its own argument 1, and the same shape as §6's `0x40`-byte attachment record) — this is attachment-lag
state, not the skin palette; (2) only when a branch flag is clear, it calls
`FUN_00487e80()`/`FUN_004be9c0()`/`FUN_004be910()`/`FUN_00e649f0()` — the confirmed site-B
gather chain — purely as a **consumer**, exactly as §11.12.3 already established for this
function. **`FUN_0073f650` straddles both subsystems (attachment lag and the skin-gather
driver) but writes `record[+0x00]` in neither branch. [CONFIRMED — decompilation, full body,
correcting §11.13.2's three-line excerpt to the complete picture.]**

**Answer for this gap: it is a third driver of the site-A gather chain** (alongside
`FUN_009226b0` and `FUN_0094e6b0`, both already on file), computing a cheap-vs-blended
transform and handing it to `FUN_0073f650` — not a writer of `record[+0x00]`, and nothing it
calls is either.

#### 11.14.2 The scan methodology and its two blind spots, both closed

A gap can be uncarved in two different states, and a single technique only sees one of them:

1. **Already-disassembled, but no owning `Function`.** Ghidra's disassembler reached the bytes
   (via some other flow) and created real `Instruction`s, but no `Function` object wraps them —
   this was `FUN_00902140`'s state, and pass 2's `FUN_007030d0` gap's state *inside `gp_rig3`
   after that session's edit*. Detected by walking every pair of address-adjacent `Function`s
   inside each range and checking whether the byte gap between them (≥ 8 bytes, to skip plain
   alignment stubs) already contains real `Instruction`s.
2. **Never disassembled at all — still raw "undefined" bytes.** This is the state pass 2's gap
   was in in any project copy that never had that session's `createFunction` call applied
   (including this pass's own fresh `gp_rig4`) — the first technique is blind to it, because it
   requires an `Instruction` to already exist. Detected the same way but checking the *absence*
   of an `Instruction` at the gap start (≥ 16 bytes, since a validated-preamble accessor plus
   any real loop body needs well more than that; smaller gaps are alignment padding by
   construction).

**Both were run over exactly the three implicated ranges** (`0x00700000`–`0x00710000`,
`0x00900000`–`0x00910000`, `0x004b0000`–`0x004d0000`) **and nowhere else** — this is not a
byte-pattern search of the binary, it only walks the existing function table inside these three
windows and inspects the gaps between neighbouring functions, per the task's own scope.

**Technique 1 found 4 candidates**: the `0x00902140` gap (§11.14.1, above); three trivial
one-line leaf methods (`FUN_00903730` — a conditional cleanup call; `FUN_00903930` — `return
400;`; `FUN_00905500` — a wrapper returning a bool) whose *only* cross-references are `DATA`
type (i.e. they are reached exclusively through a vtable slot or callback-pointer table, never
a direct `CALL` — which is exactly why a `getReferencesTo`-then-decompile sweep starting from a
known accessor would never surface them, the same blind spot pass 2 named, just via a different
mechanism than a missing-`Function`-at-a-`CALL`-target); and one **false positive**,
`0x004c511c`, which `createFunction` carved into a 95-byte "function" whose decompile leaked
`unaff_EBX`/`unaff_ESI`/`unaff_EDI` — the tell that the boundary is wrong. Raw disassembly
proved it: the real function is the pre-existing `FUN_004c50f0`, whose body ends at `0x4c511b`
right before a `JMP dword ptr [ECX*4 + 0x4c55dc]` — `0x4c511c` is **case 0 of that switch**, not
a hidden call target (its only xref is a `DATA` reference *from* the jump table itself, at
`0x4c55f4`). The whole switch body (`0x4c511c`–`0x4c55d6`, 8 cases, ending in the same
`__security_check_cookie` epilogue and `RET 0x8` throughout) was read and mechanically scanned
for `DAT_0344c924`/`DAT_0344c91a`: zero hits — it is a 4×4-matrix compose/translate helper
(calls into `FUN_004bc470`, `FUN_004bc640`, `FUN_0048d0a0`, `FUN_0048d110`, `FUN_0048cf50`,
`FUN_004c0170`, `FUN_004c07e0`, `FUN_004c0a70`, `FUN_004c0d10`, `FUN_004c01d0`, `FUN_004c0230`
— the animation pose-blend composition family, distinct object shapes throughout, no `0x3a0`
stride anywhere). **The erroneous `FUN_004c511c` Function was removed** (`removeFunction`) so
`gp_rig4`'s database does not carry a wrong boundary. **[CONFIRMED — disassembly for the
boundary/switch-table proof; mechanical reference scan for the registry-touch negative.]**

**Technique 2 found 70 raw candidates** (30 in `0x0070xxxx`, 17 in `0x0090xxxx`, 23 in
`0x004bxxxx`–`0x004cxxxx`) — most opening with a run of `0xCC` (or `0x90`) padding bytes before
real code, and a number that are pure jump-table data (a run of 4-byte values that are
themselves addresses back into the same neighbourhood, e.g. `0x00702781`'s and `0x0070a79f`'s
and `0x004bac42`'s candidates) rather than code at all. Systematic triage (skip leading padding
to the real code start; `createFunction`; decompile; reject anything with `unaff_` registers, a
failed decompile, or a degenerate ≤2-byte body as a non-function artifact, removing the
erroneous `Function`) resolved this into **29 genuine small functions** (sizes 3–375 bytes) and
**40 rejected non-function artifacts** (jump tables, switch-case fragments, and one-off stray
bytes between real functions). Every genuine function was mechanically scanned for
`DAT_0344c924`/`DAT_0344c91a`: **zero hits across all 29.** The two largest were also read in
full as an extra check (size alone doesn't prove innocence): `FUN_0070f690` (195 bytes) is a
game-mode-gated audio/state dispatcher (`FUN_00a29be0`, `FUN_008810b0`, `FUN_0070cba0`,
`FUN_004db850`, `FUN_00853b10`, `FUN_0070ced0`, `FUN_0070d9b0`, `FUN_00941c40`,
`FUN_00754410`); `FUN_004ba050` (375 bytes) is a UI/name-hash table-entry manager, striding its
own object at `0x5c` bytes (not `0x3a0`) via an index-times-`0x5c` address computation, with unrelated virtual-call
dispatch. Neither is anywhere near the registry. **[CONFIRMED — disassembly/decompilation for
the two large ones; mechanical reference scan for all 29.]**

#### 11.14.3 The region immediately bracketing pass 2's own `FUN_007030d0` — resolved into three functions, one of them entirely new

Pass 2 (§11.13.2) described this neighbourhood as "the surrounding bytes (`0x0070300d`–
`0x00703199`... immediately preceding a separate, already-recognised `FUN_007031e0`)" and named
the `Function` it created there `FUN_007030d0` — **the two numbers don't actually match each
other** (`0x0070300d` vs `0x007030d0` is a digit transposition), and re-deriving the region from
scratch in this fresh, independent copy resolves it precisely: the true unowned span the
technique-2 scan reported is `0x00702f8b`–`0x007031df` (597 bytes, bounded by the pre-existing
`FUN_00702a50` and `FUN_007031e0`), and it contains **three** distinct coherent functions, not
one:

- **`FUN_00702f90`** (`0x00702f90`–`0x007030c5`, 310 bytes) — this is pass 2's own
  `LAB_00702f90`, which that pass disassembled ad hoc and characterised as "UI/map-load
  transition lifecycle code" but never carved as a `Function`. Confirmed exactly right: it
  zeroes `DAT_014ff6c3`/`DAT_014ff6c5`, sets `DAT_014ff6c6`, and calls a long, fixed sequence of
  UI/streaming-adjacent helpers (`FUN_007f3940`, `FUN_0058ebc0`, `FUN_0059eec0`,
  `FUN_0059f8c0`, `FUN_007c6850`, `FUN_00704c30`, `FUN_0087ba20`, `FUN_0087dfc0`,
  `FUN_00831f70`, `FUN_008b9e90`, `FUN_00710b80`, `FUN_008318f0`, `FUN_00831f60`,
  `FUN_008b8880`, `FUN_0086bd40`, `FUN_00871550`, `FUN_00e217f0`, `FUN_00755410`,
  `FUN_007553c0`, `FUN_00db98d0`, `FUN_005dcbc0`). No registry touch.
- **`FUN_007030d0`** (`0x007030d0`–`0x00703199`, 202 bytes) — re-derives pass 2's own finding
  exactly: three `FUN_00706ab0()` game-mode gates excluding kinds 7/8/9, then the fixed
  eighteen-ish-call sequence including `FUN_00b93540()`, `FUN_00b91a10()` (the gather),
  `FUN_00823310()`, `FUN_00bdd520()`. No registry touch (re-confirming §11.13.2).
- **`FUN_007031a0`** (`0x007031a0`–end, a few dozen bytes) — **new this pass**, sitting in the
  small tail gap between `FUN_007030d0`'s `RET` at `0x00703199` and `FUN_007031e0`'s entry at
  `0x007031e0` that pass 2 never inspected. It is a small "is a map-load/transition in progress"
  predicate (checks a render-list emptiness condition via `FUN_0087ba20`, then
  `DAT_014ff6c1`/`DAT_014ff6c2`) — same flag family as `FUN_00702f90`, unrelated to the
  registry.

All three mechanically scanned for `DAT_0344c924`/`DAT_0344c91a`: zero hits. **[CONFIRMED —
disassembly/decompilation, full bodies, all three.]**

#### 11.14.4 Net verdict and recommendation

**⚠ RESOLVED 2026-09-13 — item 3(b) is resolved by §11.15 (sign fix) and §11.16 (`source` is allocated by `FUN_004c79a0` as `limit` matrices in rig-bone order; §11.16.3: no per-frame writer of the pointer exists). The OPEN verdicts below are kept as the record of the search.**

**Clean negative, third pass running.** Across both scan techniques, all three implicated
ranges, and every function this pass carved (`FUN_00902140`, `FUN_00702f90`, `FUN_007030d0`,
`FUN_007031a0`, the 3 trivial DATA-only leaf methods, the 29 recovered small functions from the
undefined-byte sweep — 36 genuinely new `Function` boundaries in total) plus every one of their
directly-called functions read this pass (`FUN_0073f650` in full, `FUN_00e649a0`,
`FUN_00456920`, and the rest of `FUN_00902140`'s 20-function call list) — **not one references
the registry's own globals (`DAT_0344c924`/`DAT_0344c91a`), and not one receives a pointer
shaped like the validated `rec`.** One erroneous `Function` boundary (`0x004c511c`, a
switch-case body misread as a standalone function) was created, diagnosed, and removed rather
than left in the database. This is stated as a claim about the search: **the uncarved-code
angle §11.13 opened, run to ground inside the three named address ranges under two
complementary detection techniques (already-disassembled-but-unowned, and never-disassembled),
with a documented size floor (≥ 8 bytes for the first technique, ≥ 16 bytes for the second, both
well below what a validated-preamble accessor plus any real fill loop would need) — not a claim
that no writer of `record[+0x00]` exists anywhere in the roughly 2,900-function binary **[⚠ sic: ~41,785 functions, as above]**.**

**Recommendation.** Three independent passes (§11.12.3, §11.13, this section) have each applied
a genuinely different method to the same question — a call-graph sweep from the gather's known
accessors, a wider write-predicate sweep over all 123 registry-base-pointer accessors plus one
level deeper on both drivers' own call chains, and now a structural scan for the specific class
of blind spot (uncarved code) the second pass's own discovery pointed at — and all three have
come back negative, each ruling out real, named candidates rather than returning nothing. There
is no fourth angle currently on file that isn't a re-run of one of these three. **Item 3(b)
should be deprioritized rather than dispatched a fourth time** unless a genuinely new angle
surfaces (for instance, a live dynamic trace that catches the actual write address at runtime —
this project's own methodology notes, `HANDOFF.md` §5, recommend reaching for a runtime trace once a
shape-dependent static search returns a confident negative, and three static passes now
qualify). The existing identification stands exactly where §11.12.2 left it: the gather's table
is CONFIRMED at the structural level to be the mesh's `+0x38` skinnable-bone-list array; the
addressing conflict against §11.4's direct-index reading is sharpened, not resolved, and the
source array's own fill/ordering remains OPEN.

**Tooling note.** All new `Function` boundaries this pass created exist only in
`tools/gp_rig4`, an independent Ghidra project copy — nothing was written back to
`tools/ghidra_projects`. Safe to delete once this section is considered consolidated, per
standing practice for per-agent `gp_*` copies.

### 11.15 The retraction: blend indices resolve through a bone-palette table with a point-inversion sign, not directly against the rig — §11.4's central verdict was backward (2026-09-13)

**This section supersedes §8.1's transform claim and §11.4's central verdict.** Both are retracted in place above (struck through, not deleted) rather than rewritten silently. This section records why, and what has and hasn't been independently checked on this side.

**The retraction, credited to Team B's own file-side work (their HANDOFF §9.63) and their Fable-tier engineering attempt at fixing the `.anim_pc` seam-fragmentation artefact §11.6 described (`HANDOFF.md` §27.8's own worked example, now itself superseded — see the note at the end of this section).** Two errors were compounding, not one:

1. **Blend indices are not rig-bone indices directly — they index a per-mesh bone-palette table.** The table lives at Mesh header `+0x38` (count) / the array immediately following the channel records (data) — the exact array §11.4 already located and correctly characterized as "the ascending list of rig bones this mesh may be skinned to." §11.4's own positive identification of this array was right; only its conclusion about what addresses it was wrong. **Confirmed, this session, character meshes specifically** — not tested against the other five carriers sharing this Mesh sub-block (vehicles, `.clmesh_pc`, trees, zones, foliage); Team B independently found and closed a real latent bug in their own reader on exactly this scoping question (their `bonePalette()` API read this slot unconditionally for any g-backed mesh, not gated by a skinning-layout flag — fixed, verified against 372/372 vehicles and their 237-mesh character population, unaffected either way since nothing currently calls it on non-character carriers).
2. **The rig→mesh transform for skinning is `mesh = (−rig.x, −rig.y, −rig.z)`, not `(+rig.x, −rig.y, −rig.z)` (§8.1).** A full point inversion (`diag(−1,−1,−1)`, determinant −1), not a rotation. Confirmed against the shipped `bone_palette.cpp`/`meshSpaceInversionMatrix()` mechanism (Team B, reading the actual constants, not a doc comment). Which hand the *game* calls "left"/"right" is a separate naming question, unaffected by this correction, since `−I` commutes with any rotation.

**Why §11.4's own three tests missed this, precisely — not "a shift test," a specific structural blind spot.** Tests 1 and 2 scored the palette reading using the *old* transform for the candidate-bone side, confounding a correct index mapping with an incorrect sign. Test 3 compared raw mesh-space X against raw rig-space X **with no transform applied at all**, resting on the assumption "X is lateral in both spaces, same sign" — true under the old transform (which left X alone) and false under the corrected one. All the offsets/reversals/reorderings Tests 1–3 tried are variations within the class **"the palette array is read at the wrong byte position or in the wrong order"** — none of them is in the class **"the palette array is read at the right position but scored against the wrong sign convention."** A search confined to one error class cannot falsify a hypothesis whose actual defect is in a different class, no matter how many variations of the first class it tries. This is the generalizable lesson (`HANDOFF.md` §5): **a constant-offset search cannot rule out — and, worse, can produce what looks like strong evidence against — a hypothesis whose real defect is a sign or permutation error instead.**

**Independent verification run on this side, before reading Team B's own numbers.** `tools/harnesses/skin_side_match_signfix.py` (new) reuses `skin_side_match.py`'s exact predicate and population (six characters, discrete lateral sign-match, |mean X| > 4cm threshold) and adds the one variant neither team had run: the palette array (Mesh header `+0x38`, natural alignment — `skin_side_match.py`'s own `B[d=+0]` reading) scored under the corrected sign.

| Reading | Side match | Rate |
|---|---|---|
| direct index, old sign `(+X)` | 113 / 152 | 0.7434 |
| direct index, new sign `(−X)` | 39 / 152 | 0.2566 |
| **palette, old sign `(+X)`** | **1 / 172** | **0.0058** |
| **palette, new sign `(−X)`** | **171 / 172** | **0.9942** |
| shuffled control, new sign `(−X)` | 81 / 151 | 0.5364 |

**[CONFIRMED — empirical, independently written and run before comparing against Team B's own parallel numbers.]** Per-mesh the palette+new-sign rate is 28/28, 33/33, 26/26, 27/28, 29/29, 28/28 — not merely a win, close to deterministic. The original `0/87` (§11.4, pooled slightly differently — 3 meshes there vs. 6 here) is confirmed to be the exact anti-correlation signature a correct mapping produces under an exactly-inverted sign, not noise: the shuffled control sits at chance (0.5364) as it should, and the palette+old-sign reading (0.0058) is the same structured near-zero §11.4 originally reported.

**Also independently reproduced: the retraction extends to the seam-fragmentation finding itself.** Team B's own before/after measurement on the exact clips this document's §11.5/§11.6 analysis was built on top of (their §9.56.4 baseline clip, and a second ordinary walk cycle) shows the cross-limb near-degenerate edge population **disappears** under the corrected decode — not shrinks. On one clip, all-edge max stretch ratio drops from 452× to 3.85×, and the cross-limb-near-degenerate subset drops from n=536 to n=66 with a max of 3.8cm (an ordinary knee bend, not a seam). On a second character's ordinary walk cycle, the cross-limb near-degenerate set is **empty** under the corrected decode — no such edge exists. Renders looked at directly (both sides, independently): the previously-reported "exploded fin" artefacts are simply gone, not reduced.

**What this means for the rest of §11, precisely:**
- **§11.1–§11.3 stand, and are now independent corroboration rather than merely unaffected.** The GPU-shader-consumer location (§11.1), the 48-byte affine palette (§11.2), and the gather's `destination[i] = source[table[i]]` mechanism with `table` left as "OPEN, but shaped like the `+0x38` array" (§11.3) are the *disassembly-side* view of the exact mechanism Team B found from the *file* side — two independent methods, converging. §11.3's own open item (what `table`'s source array actually is) is very likely now answered by this section, though not re-verified instruction-by-instruction against §11.3's own specific trace this pass. **→ DONE §11.16.1 (instruction-level identity).**
- **§11.4 is retracted** (struck through in place above), including its 0.10–0.14m "expected residual" absorption of the 10-of-78/midline-toe anomaly — that anomaly was very likely the visible symptom of exactly this bug, credited to §11.4's own discipline in leaving it open rather than explaining it away.
- **§11.5 is flagged suspect, not re-measured.** Its cross-limb-influence classification depends entirely on the retracted mapping. Re-measuring it under the corrected palette decode is the concrete next step if this thread is picked up again — not attempted this pass, since it needs the actual per-vertex re-decode, not just a flag. **→ DONE §11.5.1 (0 / 34,442).**
- **§11.6's rows 1–2 and 5 stand** (dual-quaternion and CPU-pass refutations rest on §11.1–§11.2 unaffected by this; the "never posed this way" row measures clip joint rotation, not blend-index mapping). **Rows 3–4 and the "assets contain the conditions for it" conjunction are suspect**, pending §11.5's re-measurement — one relay categorized row 3 as resting on §11.1–§11.3 and standing, which looks inconsistent with that row's own cited figure (a cross-limb classification) and has not been independently confirmed; treated as suspect here rather than accepted on that basis alone. **→ DONE §11.5.1; §11.6 rows 3–4 updated in place there.**
- ~~§11.7–§11.14 are flagged pending review~~ **DONE 2026-09-13 — see §11.7's own per-item verdict table (added same day) and §11.16.** Team B read all of it in full (not from titles) and gave a precise per-item verdict: items 1/2 and all of §11.8 (minus item 4, already superseded)/§11.9/§11.12–§11.14 stand independent of the blend-index/sign question; item 3 flips OPEN→RESOLVED (now further corroborated at the instruction level, §11.16); item 4/§11.10/§11.11 needed real re-examination, done in §11.17 (13/13 resolve); item 5 is moot.

**Note on `HANDOFF.md` §27.8's own worked example.** That section (this project's "defer rather than invent" policy) used the anim-seam-fragmentation question — "confirmed real, confirmed unreachable past the shader boundary" — as its concrete illustration of a case where confirmed facts run out and no mechanism should be invented. That illustration is now itself retracted: the facts didn't run out, one of them was wrong. The policy stands; the example needs replacing, and this episode (a summary that needed the full source text twice before acting, an independent same-day rerun before trusting either side's numbers) is arguably a better one. See `HANDOFF.md` §27.8 for the update.

**What is NOT yet settled, stated plainly rather than guessed:**
- ~~§11.5's actual re-measured figures under the corrected decode.~~ **DONE 2026-09-13 — §11.5.1.** Re-measured with the palette lookup and nothing else changed: **0 / 11,343** cross-limb vertices on §11.5's own two meshes and **0 / 34,442** over six, against a reversed-palette control at 0.2907 and a shuffled-palette control at 0.3047, with the original 1,683 / 11,343 reproduced digit-for-digit first and the zero liveness-checked (identical lane, vertex and bone-pair counts across all four readings; 0 unclassifiable pairs versus 6,451 under the retracted reading). The phenomenon is **absent, not diminished** — file-side corroboration of the render comparison above, by an instrument that reads no coordinates at all. This also closes §11.6 rows 3–4 and the "assets contain the conditions for it" conjunction (updated in place there).
- ~~§11.7–§11.14's individual disposition~~ **DONE — see the bullet above and §11.7's own verdict table.**
- ~~The 10-of-78 residual and slot-15's actual resolution~~ **DONE 2026-09-13 — §11.17. 13/13 resolve exactly under the corrected decode.**
- Whether `+0x38`/`+0x40`'s bone-palette identity, and `+0x48`'s set-descriptor role, hold for any of the other five carriers sharing this Mesh sub-block — untested in both directions, still open.
- ~~The per-draw-range multi-set selector~~ **RESOLVED on Team B's side, per relay 2026-09-13 — not independently re-verified on this side.** Their own multi-set palette-selector work landed and was verified against a real 2-set mesh (`gus`) rendering correctly with no flag; one genuine edge case (`reynolds`, 6 vertices structurally unskinnable from one array) still refuses by design, not a gap in the fix. Not re-derived or independently checked from this side.

### 11.16 §11.7 item 3's disassembly-level corroboration — `table` IS the Mesh-header `+0x38` array at the instruction level, and `source` is an array of exactly `limit` matrices allocated by a function no prior pass could have reached (2026-09-13)

> **Scope and status.** This is the **fourth** dispatched pass on the skin gather's `source`
> array. Item 3 is already **RESOLVED** on the sign-fix logic alone (§11.7 item 3, §11.15) and
> nothing here re-opens it — this section supplies the disassembly-level corroboration that
> item 3's own text said was "separately in progress." Passes 1–3 (§11.12.3, §11.13, §11.14)
> are not repeated: not the 7-caller enumeration, not the 123-accessor sweep, not the
> uncarved-code scan. **Fresh, independent Ghidra project copy `tools/gp_rig6`; new scripts
> `RigSrc1.java` … `RigSrc9.java` in `tools/scripts/`; read-only (no `createFunction`, nothing
> written back to `tools/ghidra_projects`).**
>
> **Predicate, stated before searching (per `HANDOFF.md` §5).** A positive finding is either
> (a) a confirmed identity between §11.3's `table` and the Mesh-header-`+0x38` bone palette
> **plus** a confirmed bulk-copy-shaped fill of `source`, or (b) a clear disproof (`table`
> turns out to be a genuinely different array). A negative finding is a documented, bounded
> search that settles neither way. **Result: (a), in both halves, with one honest boundary —
> the identity is confirmed outright, `source`'s extent and index space are confirmed via its
> allocator, a bulk-copy palette fill is located and read in full on the *sibling* no-table
> path, and the per-frame writer of the matrices *inside* `source`'s block is still not
> located — for a reason this pass can now name precisely.**

#### 11.16.1 The identity is literal, and the one alternative reading is refuted at the instruction level

§11.12.2 raised "the gather's table is the mesh's `+0x38` array" to CONFIRMED at the
*structural* level, by matching the parser's write pattern against the callee's read pattern.
One thing it did not do is check the actual arithmetic that forms the pointer — and there was a
real alternative it could have been: if the call sites **loaded** a pointer *from* `mesh+0x38`
(`MOV reg,[mesh+0x38]`), the table would be a separate object that merely happens to have the
same `{u16 count, ptr}` shape, and the `+0x38`/`+0x40` coincidence would be exactly that.

It is not a load. Both established call sites form the table by **address arithmetic**:

```
FUN_00e649f0  (site B)                         FUN_00b91a10  (site A)
  00e649fc  MOV EAX,[ECX + 0xbc]                 00b91a7e  MOV ECX,[EBP + 0x5c]
  00e64a06  MOV EAX,[EAX + 0x30]      ; mesh     00b91a81  MOV EDX,[ECX + 0x64]
  00e64a0b  TEST EAX,EAX / JZ                    00b91a84  MOV ECX,[EDX + 0x50]
  00e64a0d  ADD EAX,0x38              ; <<<<     00b91a87  MOV ECX,[ECX + 0x30]   ; mesh
  00e64a10  JZ  (null-test on the sum)           00b91a8a  TEST ECX,ECX / JZ
  00e64a12  CMP word ptr [EAX],0x0    ; count    00b91a8e  ADD ECX,0x38           ; <<<<
  00e64a1c  PUSH EAX                  ; arg 3    00b91a93  XOR ECX,ECX  (null arm)
                                                 00b91a95  PUSH ECX               ; arg 3
```

`ADD EAX,0x38` / `ADD ECX,0x38`, not `MOV reg,[reg+0x38]`. The value pushed as the gather's
third argument is **the address `mesh+0x38` itself**, and the very next instruction at site B
reads the element count as `word ptr [EAX]` — i.e. *at* `mesh+0x38`. Inside the callee
(`FUN_00e7aff0`, re-read in full at `0x00e7aff0`–`0x00e7b0e2` this pass) the byte-array pointer
is fetched at `00e7b020 MOV EDX,[EBX+0x8]`, i.e. **`mesh+0x38+0x08 = mesh+0x40`** — exactly the
fixed-up pointer `FUN_00e71410` writes (§11.12.2). **[CONFIRMED — disassembly, raw
instructions, both call sites.]**

A detail worth keeping, because it says what the *source code* looked like: at both sites the
compiler emitted a **null test immediately after the `ADD`** (site B's `JZ` at `0x00e64a10`;
site A's explicit `XOR ECX,ECX` null arm at `0x00b91a93`). Testing `mesh+0x38` for null is
vacuous for any real pointer — it is what a compiler emits for the source expression
`mesh ? &mesh->boneTable : nullptr`, i.e. **the address of a member**, never a member load.
Two independent code paths emitting the same vacuous test is the tell.

**Argument order re-pinned independently of §11.3**, from the raw prologue rather than the
decompiler: after `PUSH EBX`, `MOV EBX,[ESP+0x10]` makes **arg 3 = the table structure**
(`MOVZX EAX,word ptr [EBX]` = its count, `MOV EDX,[EBX+0x8]` = its byte array); `MOV EBP,
[ESP+0x10]` at `0x00e7b013` (three pushes deep) makes **arg 1 = the source matrix base**
(`MOVQ XMM0,[EAX + EBP*1]`, `EAX = index*48`); `CMP EAX,[ESP+0x18]` makes **arg 2 = the limit**;
the `thiscall` receiver is the destination palette (`[ECX+0x4]` live count, `[ECX+0x8]`
capacity, `[ECX+0xc]` matrix pointer, `[ECX+0x10]` the flag byte cleared at the end — §11.2's
palette record exactly). Site A's push order confirms it from the other end: `PUSH ECX`
(table) → `MOV ECX,[ESI+0x4]` (receiver = destination palette) → `PUSH EDI` (limit, from
`FUN_004be910`) → `PUSH EAX` (source, from `FUN_004be9c0`) → `CALL`. **[CONFIRMED —
disassembly.]** This reproduces §11.3's account exactly, from scratch.

**Answer to task half (a): `table` and the Mesh-header `+0x38` bone palette are the same
array, not two arrays of the same shape.** The disproof branch of the predicate is closed.

#### 11.16.2 `source` is an array of exactly `limit` matrices — so the gather's limit test is an array bound, not a semantic filter

The decisive fact this pass adds is **where `source` comes from**, found by abandoning the
anchor all three prior passes used.

`FUN_004be9c0` was first confirmed to return a **loaded pointer** (`004be9ec MOV EAX,
dword ptr [EAX]`), not the address `rec+0x00` — so passes 1–3 were hunting the right object
class, and the alternative "the array is inline at the record's start" is refuted.
**[CONFIRMED — disassembly.]**

Then: the record's release path `FUN_004c14f0` calls a pool free on `rec+0x394` and immediately
zeroes `rec+0x00`, `rec+0x04`, `rec+0x08` and `rec+0x38` together (`004c1573`–`004c157e`,
duplicated at `004c1628`). Three pointers freed by one release means one block — and the pool
it goes back to is one of two globals, `0x0344c850` / `0x0344c8b0`, selected by a flag bit
`0x10`. **Those two pool globals are a completely different search anchor from
`DAT_0344c924`/`DAT_0344c91a`**, which is what §11.12.3, §11.13 and §11.14 all keyed on.
Scanning for them found five functions, three of which are named nowhere in §11.12–§11.14.
One of them is the allocator:

```
FUN_004c79a0   (thiscall; ESI = the 0x3a0-stride registry record)
  if (rec[0x00] != 0) { rec[0x390] = 2; return true; }        // already allocated
  N     = the dword at +0x04 of the object rec[0x3c] points to  // the binding descriptor's count
  block = pool(N < 0x20 ? 0x344c8b0 : 0x344c850).alloc(N * 0x6c, 0x10)
  rec[0x394] = block ;  rec[0x6c] |= / &= 0x10                // remembered for the release
  rec[0x00]  = block                                          // matrix48[N]
  rec[0x04]  = block + N*0x30                                 // matrix48[N]   (LEA n,[n+n*2]; SHL 4)
  rec[0x08]  = block + N*0x60                                 // vec3[N]       (LEA n,[n+n*2]; SHL 5)
  memset(rec[0x08], 0, N * 0x0c)
  rec[0x38]  = 0 ;  rec[0x390] = 2
```

**The arithmetic closes exactly: `0x30 + 0x30 + 0x0c = 0x6c`.** One pooled block is carved into
three parallel N-length arrays — two 48-byte-matrix arrays and one `vec3` array — and every
offset and the total size agree. **[CONFIRMED — disassembly, `FUN_004c79a0` read in full, raw
and decompiled.]**

**`N` here is literally the value the gather receives as its `limit`.** `FUN_004be910` returns
the dword at `+0x04` of the object that `rec+0x3c` points to — the same value, from the same descriptor field. So:

> **`source` contains exactly `limit` matrices, and the gather's own test
> `if (table[i] >= limit) destination[i] = identity` is precisely an array-bounds check on
> `source`.**

That is the corroboration item 3 asked for, and it is a statement about the code rather than an
inference from the file side: a bounds check is only type-correct when the index and the array
share one index space, so **`table`'s values and `source`'s indices are the same index space by
construction**, and blend indices naming a bone outside that space receive the identity matrix
rather than reading past the array. §11.15's file-side result (171/172, palette + corrected
sign) is what names that space **rig-bone order**. The two halves meet here.

Two further instruction-level facts sharpen what the alternative would have had to look like:

- **The engine does carry an explicit slot ↔ rig-bone permutation, and the gather applies
  neither direction of it.** `FUN_004c1350` (the binding-descriptor constructor, §11.12.1's
  item-2 resolution) builds each descriptor from a caller-supplied `{key, N, u32 *values}`
  triple into a **shared global table at `DAT_02f5e010`, up to `0xa0` = 160 entries of `0x410`
  bytes** — not a per-record allocation, which is why the record constructor only *searches*
  that table by key (`FUN_004c1730`, `004c181d`). Descriptor layout, read from the constructor's
  own stores: `+0x00` key, `+0x04` N, `+0x10` `u16[]` **slot → value**, `+0x110` `s16[]` **value
  → slot** (the inverse permutation, written as `desc[0x110 + values[i]*2] = i`), `+0x210`
  `u32[]` per-slot flags. `0x210 + 128*4 = 0x410` — all three tables are 128 entries, and the
  stride confirms the layout. `FUN_004be8b0` and `FUN_004be9f0` both convert with
  `desc[0x10 + slot*2]` *before* indexing the rig's own `0x28`-stride bone array (reached as
  `[rec+0x14] → +0x40`; `0x28` = the `.rig_pc` bone-record stride, §4), which is what makes the
  `values[]` **rig bone indices**. Every other consumer of this record converts; the gather does
  not. **[CONFIRMED — disassembly.]**
- **A bulk-copy palette fill, located and read in full — on the sibling no-table path.**
  `FUN_00e7ad30` (the straight copy, `destination[i] = source[i]`) has a **third call site never
  previously documented**, `FUN_00abfad0` at `0x00abfe27`. That function builds its source on
  the stack: slot 0 is the 48-byte affine identity copied verbatim from `DAT_01321424`…
  `_DAT_0132144c` (§11.2's fallback matrix), then a loop over the `N` pointers at `obj+0x68c`
  (count at `obj+0x688`) writes one 48-byte matrix per entry (advancing its output cursor by
  6 qwords, i.e. 48 bytes, each time), substituting
  the identity for any entry whose flag word `[entry+0xf0] & 0x80e00000` is set, and finally
  `FUN_00e7ad30(&stackBuffer, N+1)` bulk-copies the whole thing into the palette at `obj+0xa98`.
  This is the vehicle/part carrier (the `0x00ab`–`0x00ad` module, `obj+0xa98` next to the
  already-documented vehicle `+0xa88`), not the animation registry — but it shows the engine's
  convention concretely, on the one path where no `table` intervenes: **the array handed to the
  palette load is indexed in exactly the space the destination palette slot — and therefore the
  vertex's blend index — is indexed in.** **[CONFIRMED — decompilation and raw, full body.]**

#### 11.16.3 Why three passes could not find a writer of `record[+0x00]` — the reason, not another negative

`rec+0x00` is written in exactly **three** places in the whole binary, and this pass has read
all three: zeroed by the constructor (`FUN_004c1730`), set once by the allocator
(`FUN_004c79a0`), zeroed again by the two releases (`FUN_004c14f0`, `FUN_004c7ad0`). **There is
no per-frame writer of the pointer, and there cannot be one** — the per-frame pose is written
*through* the pointer, into the pooled block. Passes 1–3 were searching for an object that does
not exist by construction, which is why each of them returned a clean negative while every
named candidate they ruled out really was unrelated.

The allocator was nonetheless reachable, and the two specific reasons it was missed are worth
recording as search-design lessons rather than as a criticism of those passes, whose predicates
were each stated honestly and each did what it said:

1. **`FUN_004c79a0` is a `thiscall` method — the record arrives in `ESI`/`ECX`, and the function
   never references `DAT_0344c924` or `DAT_0344c91a` at all.** It is therefore not one of the
   123 functions any of the three passes swept. The registry's own globals are the anchor for
   *accessors*, and the allocator is not an accessor; the pool globals are its anchor, and
   nothing pointed at them until the release path was read at the instruction level.
2. **§11.13.2's deliberately-widened predicate required the validated `rec` pointer to be
   *PUSHED* into a further `CALL`.** A `thiscall` passes it in `ECX` and never pushes it. That
   predicate was added specifically to catch a fill happening inside a callee's body — the right
   instinct, one calling convention short of the target.

**Generalisable, and offered for `HANDOFF.md` §5:** *when a search for "who writes field X"
comes back empty three times, check whether X is a pointer that is written once at allocation
and thereafter only written **through** — those are different predicates, and the second one is
invisible to every version of the first.* A companion to the existing "a negative result is
only as broad as its predicate" lesson, at the level of *which object* the predicate names.

#### 11.16.4 What this pass did not settle, stated plainly

- **The per-frame writer of the matrices inside `source`'s block is still not located.** That is
  a third predicate again — a store of 48-byte blocks through a pointer held in a record
  method's `this`, by a function that need not reference the registry globals, the pool globals,
  or any named accessor. Not attempted here; the honest boundary of this pass.
- **`FUN_00753d40` / `FUN_008cb170` / `FUN_009f36d0` supply the binding descriptor's `values[]`
  list for the non-vehicle carriers** (the fourth supplier, `FUN_00ab0730`, is the already-
  documented vehicle loader). `FUN_00753d40` was read: it takes `N` from a `u16` at `param+0x0a`
  and the array from `param+0x20` of a resource-shaped object, mints a fresh key from the global
  counter `DAT_012f7ba8`, and hands the triple to the descriptor constructor. ~~**Which resource
  that object is, and therefore whether `values[]` is the identity permutation over the rig's
  bones for characters, was not traced**~~ **RESOLVED for two of the three, 2026-09-13 — see
  §11.18. `FUN_009f36d0`'s object is CONFIRMED to be the `.ccmesh_pc`/`.csmesh_pc` outer
  `0x424BD00D` sub-header's own "array 4" (`spec-geometry-format.md` §4.1/§4.1.2), whose content
  is independently already-confirmed to be the fixed identity table `0..67` — so for this
  supplier, yes, `values[i] == i`. `FUN_008cb170` uses a materially different mechanism (a fixed
  engine global, not a per-resource field) that is ALSO the identity, `0..127`, confirmed from
  its own initializer loop in disassembly. `FUN_00753d40`'s own object was traced one hop
  short of full confirmation — high-confidence by field-shape analogy, not independently proven
  the same way.**
- Nothing here changes item 3's status. It was RESOLVED on the sign-fix before this pass started
  and is RESOLVED after it; this is corroboration, and it would have been worth recording as a
  bounded negative even if it had found nothing.

#### 11.16.5 Two scan defects caught by their own known-positive assertions, recorded because both would have published as clean results

Both are instances of `HANDOFF.md` §5's "put a known-positive in every scan and assert it fires."

1. **A field-access scan reported `FUN_004be9c0` does not read `rec+0x00` — its own known
   positive, and false.** The scan walked each function in **address order** and dropped the
   tracked record register on clobber; `FUN_004be9c0`'s error-path `XOR EAX,EAX` at `0x004be9e9`
   sits physically *before* the success-path `MOV EAX,[EAX]` at `0x004be9ec`, so tracking died
   one instruction early. A linear scan follows layout, not control flow, and the two differ
   exactly where a function has an early-out. Fixed by keeping the register live to the end of
   the function (over-reporting, then verifying every hit against raw disassembly — which is how
   `FUN_004bd3d0`, `FUN_004bdc30`, `FUN_004bf100` and `FUN_004be9f0`'s apparent `rec+0x00`
   touches were each shown to be register reuse). The corrected scan reproduces §11.13.2's
   123-function count **exactly**.
2. **A scan for the registry globals returned a uniform zero across 3.5M instructions.** The
   test was `text.contains("0x344c924")` against a rendering of `[0x0344c924]` — the leading
   zero makes that substring absent. **A connection failure, not a data fact**, and the second
   time in this project a uniform zero has been exactly that (§11.8 item 1's vtable-call census
   was the first). Re-run against scalar/address operand values, it finds 269 instructions in
   123 functions.

A third, smaller tooling caveat, recorded because later work will otherwise trust it:
**`getReferencesTo(FUN_004be9c0)` returns 3 of the 8 direct `CALL` sites** a raw instruction
scan finds — it misses `0x00b91a72`, the site-A gather's own call, among others. Reference-
manager counts are not trustworthy in these project copies; §11.12.3's "exactly 7 callers" is
nevertheless **reproduced exactly** by the raw scan (8 call sites in 7 distinct functions), so
that pass's own number was right and evidently not obtained this way.

#### 11.16.6 Registry record layout, as far as it is now read

Consolidated because four passes have each added a field and none has collected them. Record
stride `0x3a0`, base `DAT_0344c924`, count `DAT_0344c91a` (`u16`), self-index validated at
`+0x50`.

| Offset | Content | Source |
|---|---|---|
| `+0x00` | `matrix48[N]` — **the skin gather's `source`**; block base | `FUN_004c79a0`, `FUN_004be9c0` |
| `+0x04` | `matrix48[N]` — attachment/prop spring-lag state cache | `FUN_004c79a0`; §11.13.1, `FUN_004be950` |
| `+0x08` | `vec3[N]`, zeroed at allocation | `FUN_004c79a0`, `FUN_004c1730` |
| `+0x0c` | a separately-allocated `0x300`-byte block on a free list | `FUN_004c7b20` |
| `+0x14` | the rig-holder object; `[+0x14]→+0x40` is the rig's `0x28`-stride bone array | `FUN_004be8b0`, `FUN_004be9f0` |
| `+0x38` | flag byte, cleared at allocation and at release | `FUN_004c79a0`, `FUN_004c14f0` |
| `+0x3c` | → shared binding descriptor in `DAT_02f5e010[160]`, `0x410` bytes each | `FUN_004c1730`, `FUN_004c1350` |
| `+0x50` | **self index** (the validation field) | all accessors |
| `+0x58` | attachment count | `FUN_004bdc30` |
| `+0x6c` | flag byte; bit `0x10` records which pool `+0x394` came from | `FUN_004c79a0`, `FUN_004c14f0` |
| `+0x70 …` | inline attachment array, stride `0x50`, `memset` `0x2d0` at construction (9 entries) | `FUN_004bdc30`, `FUN_004c1730` |
| `+0x390` | state word, set to `2` when the block is live | `FUN_004c79a0`, `FUN_004c7ad0` |
| `+0x394` | pool handle for the `N*0x6c` block | `FUN_004c79a0`, `FUN_004c14f0` |
| `+0x39c` | copied from `[ctorArg1 + 0x50]` | `FUN_004c1730` |

Binding descriptor (`+0x3c`), `0x410` bytes, shared, up to 160 in `DAT_02f5e010`:
`+0x00` key · `+0x04` N · `+0x10` `u16[128]` slot → rig bone · `+0x110` `s16[128]` rig bone →
slot · `+0x210` `u32[128]` per-slot flags. **[CONFIRMED — disassembly, `FUN_004c1350` read in
full; the `0x410` stride and the three table origins agree exactly.]**

### 11.17 §11.7 item 4's re-examination executed: all 10 of §11.10's mismatches, and all three of §11.11's slot-15 instances, resolve exactly under the corrected decode (2026-09-13)

**Method: §11.4 Test 3's exact predicate, re-run on the identical 10 (mesh, slot) pairs from §11.10 plus slot 15 on all three meshes, with `bone = palette[slot]` (Mesh header `+0x38`, character meshes) and the corrected sign `mesh.x = −rig.x` (§11.15) applied to the palette-resolved bone's own rest-X — not just checked for sign agreement, checked against the bone's *exact* rest-X position.** New harness `tools/harnesses/skin_1010_1011_recheck.py`, reusing the same field-location logic as `skin_side_match_signfix.py`. Predicate stated before running: a positive finding is the resolved bone's rest-X landing close to the cluster's own measured mesh-space mean X (not merely same sign); a negative is disagreement on magnitude even where the sign happens to match.

| Mesh | Slot | Published (wrong) | Palette resolves to | Palette rest-X (corrected sign) | Measured cluster mean X | Agreement |
|---|---|---|---|---|---|---|
| brad | 21 | `l-hand` | `24 = l-finger2` | −0.6487 | −0.655 | 7 mm |
| brad | 23 | `l-finger1` | `26 = l-finger4` | −0.6334 | −0.623 | 10 mm |
| brad | 15 | `r-toe0` | `17 = head` | −0.0000 | −0.033 | 33 mm |
| angel | 21 | `l-hand` | `24 = l-finger2` | −0.6487 | −0.660 | 11 mm |
| angel | 23 | `l-finger1` | `26 = l-finger4` | −0.6334 | −0.640 | 7 mm |
| angel | 41 | `r-finger21` | `53 = r-upperarmtwist2` | +0.2427 | +0.218 | 25 mm |
| angel | 43 | `r-finger41` | `55 = r-upperarmtwist3` | +0.3343 | +0.352 | 18 mm |
| angel | 15 | `r-toe0` | `17 = head` | −0.0000 | −0.001 | 1 mm |
| alfred | 21 | `l-hand` | `24 = l-finger2` | −0.6509 | −0.660 | 9 mm |
| alfred | 23 | `l-finger1` | `26 = l-finger4` | −0.6408 | −0.647 | 6 mm |
| alfred | 24 | `l-finger2` | `28 = l-thumb1` | −0.5888 | −0.585 | 4 mm |
| alfred | 29 | `r-finger1` | `34 = r-thumb1` | +0.5888 | +0.588 | 1 mm |
| alfred | 15 | `r-toe0` | `17 = head` | −0.0000 | +0.000 | 0 mm |

**13/13 resolve, all within centimetres — the same order of magnitude as §8.1's own 10–14 cm expected residual, not the 1.63–1.69 m gap the published reading left for slot 15, or the 68–98 cm gaps the published reading left for the 10 lateral mismatches.** **[CONFIRMED — empirical, all 13 cases, exact rest-X agreement not merely sign agreement.]**

**What this means for §11.10 and §11.11's own conclusions, retracted in place above:** these 10 slots were never a minority cross-limb weight riding along with a correct dominant weight (§11.10's "mirror-hand weight noise" reading, itself borrowed from the now-empty §11.5 population) — the *dominant* weight itself was resolving to the wrong bone, because the published direct-index reading is wrong, not because of any noise in the weight painting. Slot 15 is not two list positions short of `head` (§11.11's breadth-first-traversal reading) — it names `head` exactly, with zero list-position error; the near-head centroid was never a symptom of an off-by-a-few-positions bug, it is the correct answer read the correct way. Team B's own independent finding for slot 15 alone (§9.63.1, `palette[15] = 17 = head`) is reproduced exactly here and now extended to all 10 of the lateral mismatches as well.

**What survives from §11.11 anyway:** the breadth-first/level-order bone-list-ordering observation itself (§11.11's structural account of how the bone array is traversed) was never tested by this re-examination and may still be a real, separate fact about this rig format — it is retracted only as an *explanation* for slot 15, not necessarily as a fact in its own right. Not independently re-verified this pass.

### 11.18 §11.16.4's own named next step executed — two of the three non-vehicle suppliers confirmed identity, the third traced to a sibling registration-table row one hop short of full confirmation (2026-09-13)

> **Scope.** §11.16.4 left one live, well-anchored question: which resource object
> `FUN_00753d40` (and, by the same field shape, `FUN_009f36d0`) reads its `{N, values[]}` pair
> from — `u16` count at `param+0x0a`, array pointer at `param+0x20` — and whether that array is
> the identity permutation over rig-bone indices. `FUN_008cb170` was named as a third supplier
> worth a quick look. **Fresh, independent Ghidra project copy `tools/gp_rig7`; new scripts
> `RigSrc10.java` … `RigSrc14.java` in `tools/scripts/`; one `createFunction` call (on
> previously-uncarved code at `0x007542a0`, this independent copy only) — otherwise read-only.**
>
> **Predicate, stated before tracing (per `HANDOFF.md` §5/§27.8).** A positive finding is either
> (a) confirmation that a given supplier's `values[]` is the identity permutation
> (`values[i] == i`) over its own declared count, established either from real file content or
> from the fill code itself, or (b) confirmation that it is a genuine, *different* permutation —
> in which case that permutation is read out and reported, not merely labelled "not identity".
> A negative finding is a documented, bounded trace that reaches a genuine boundary (an
> unresolved generic-dispatch call site, or a value this project's static image cannot read)
> without settling either way for that specific supplier. **Result: (a), confirmed two
> independent ways, for the two suppliers whose object this pass could fully identify
> (`FUN_009f36d0`, `FUN_008cb170`); a well-anchored but short-of-confirmed (a) for the third
> (`FUN_00753d40`) — recorded honestly as such, not rounded up to a third confirmation.**

#### 11.18.1 `FUN_009f36d0`'s object is the `.ccmesh_pc` outer sub-header's own "array 4" — already-confirmed identity table, now connected to this mechanism

`FUN_009f36d0` builds a runtime mesh-instance object (its argument 2) from a loaded resource
(its argument 1). Late in its body it does exactly what §11.16.4 flagged:

- instance dword `0x1b` ← the `u16` at offset `+0x0a` of the object held in instance dword 1,
  widened to a full int (this is `N`);
- instance dword `0x1c` ← the `u32` at offset `+0x20` of that same object (this is `vals`);
- it then calls `FUN_004bc9c0` with instance dword `0x1d` and the address of instance dword
  `0x1a` (the `{key, N, vals}` triple), which reaches the binding-descriptor constructor.

Instance dword 1 — the object read at `+0x0a`/`+0x20` — is set a few lines earlier from
`FUN_007527b0`'s return value. **`FUN_007527b0` already carries a pre-existing SPEC TEAM Ghidra
comment from an earlier pass, independent of this one**, identifying it as "the real direct
caller of `FUN_007524f0` (`.ccmesh_pc` payload parser)" and confirming the fixed sub-header is
exactly `0x88` bytes — this is the *identical* function `spec-geometry-format.md` §4.1.2 item 1
already used to pin the `0x424BD00D` sub-header's own `+0x88` offset. Reading `FUN_007527b0`
confirms it returns the **address of the sub-header's own magic field** — an aligned cursor
address (computed as an offset added to argument 1) at which the dword there reads `0x424BD00D`
and the `u16` four bytes later falls strictly between `0x28` and `0x2B` — the documented
version-field range `0x29`/`0x2A`.
**[CONFIRMED — disassembly, exact match against an independently-authored prior comment and
against `spec-geometry-format.md` §4.1.2 item 1's own formula.]**

That makes offsets `+0x0a` and `+0x20` of instance dword 1's object **exactly the sub-header's `+0x0A`/`+0x20`
fields** — and `spec-geometry-format.md` §4.1's array table already names `+0x0A` as **array 4's
own count field** (`u16`, 4-byte-stride elements), with no other array's count field claiming
`+0x20`. `+0x20` is therefore array 4's (fixed-up) data pointer. **This is the loader
instruction that reads array 4 into a live `{N, values}` pair and hands it to the shared
binding-descriptor constructor — the missing link `spec-geometry-format.md` §4.1.2 item 3 did
not have when it examined array 4 from the file side alone.**

**Array 4's content was already independently confirmed, before this pass, byte-for-byte
identical on two unrelated real files** (`cm_hand_f_leather.ccmesh_pc`, a static prop, and
`chester.ccmesh_pc`, a full player-adjacent NPC body) **to be the fixed ascending table
`0, 1, 2, …, 67` — 68 entries** (`spec-geometry-format.md` §4.1.2 item 3, `[CONFIRMED —
empirical, exact content match, 2/2 unrelated samples]`, tentatively read there as "a fixed
engine constant table… plausibly tied to a fixed max-bone-influence or max-slot palette size of
68"). **Connecting the two facts: `values[i] == i` for `i` in `0..67`, confirmed, for this
supplier.** `slot` space and rig-bone space coincide outright for this carrier — the last
residual ambiguity §11.16.2 flagged is answered for this specific supplier.

**Whose carrier this actually is, traced rather than assumed.** `FUN_009f36d0` has exactly two
raw callers, both inside a resource-finalize dispatch table read from a switch on an event-kind
byte with debug labels literally named `"PCC_DELETE"` / `"PCC_SWAP"` / `"PCC_FINALIZE"` /
`"IGNORED"` (`FUN_009f46f0`, case `3`, message `"PCC_FINALIZE: %s"`; and case `2` via a thin
trampoline `FUN_009f4550` that reaches the same call). `FUN_009f36d0` copies raw byte ranges out
of argument 1's own array-pointer/length pairs (dwords `0x16`/`0x18`, `0x17`/`0x19`,
`0x2b`/`0x2c`, `0x40`/`0x42`, `0x41`/`0x43`) into a freshly-owned buffer before handing
that buffer to `FUN_007527b0` — i.e. argument 1 is a loaded raw resource holding several byte
ranges, assembled into one contiguous `.ccmesh_pc`-shaped stream. This whole chain
(`LAB_009f08c0`-family addresses immediately preceding `FUN_009f36d0`/`FUN_009f46f0`/
`FUN_009f4550` in the same small module) matches `spec-format-inventory.md` §2 row **9, "Pcust
mesh"** — `.ccmesh_pc`/`.gcmesh_pc`, constructor `LAB_009f08c0`, explicitly flagged there as
**"this variant's constructor untraced"**. **This pass traces it**: type 9 is the
player-customization loading path for the shared `.ccmesh_pc` format (clothing/body pieces a
player equips), a separate registration from the plain "Character mesh" type 5
(`FUN_00751f60`, confirmed to share none of `FUN_00753d40`/`FUN_008cb170`/`FUN_009f36d0`'s raw
callers). **[CONFIRMED — disassembly, the caller chain and the shared-format identity; HIGH
CONFIDENCE — inferred, that this closes spec-format-inventory.md row 9's specific "untraced"
flag, since the address-range/PCC-dispatch evidence is strong but this pass did not separately
re-verify `LAB_009f08c0` itself.]** *(Later, consistent: `spec-format-inventory.md` row 9 was
since traced, 2026-09-20, and `spec-geometry-format.md` §8 checks this chain at instruction level.)*

#### 11.18.2 `FUN_008cb170` is a materially different mechanism — no per-resource field at all — and it is ALSO the identity, confirmed from its own fill loop

`FUN_008cb170` does not read a resource's `+0x0a`/`+0x20` at all. Its own body (raw + decompiled,
`gp_rig7`) reads `N` from offset `+0x24` of the object at `this+0xa0` — a field with no
correspondence to any of `spec-geometry-format.md`'s six array count-field offsets — and hands
the descriptor constructor a **fixed global array address, `&DAT_024fa348`**, not a value read
from the resource at all. At the instruction level (`0x008cb1cf`–`0x008cb1e4`): the object
pointer already in EAX is used to load that `+0x24` field into ECX for `N`; the literal address
`0x24fa348` is stored directly as `vals`, with no field read at all; and the descriptor
constructor `FUN_004bc9c0` is then called with `{key, N, vals}`.

`DAT_024fa348`'s static image is all-zero (Ghidra: `undefined4`, length 4 — a BSS-shaped,
runtime-populated slot, not stored file data), so the identity-or-not question had to be settled
by finding its **writer**, not by reading bytes. A targeted operand scan for every instruction
touching the address range `0x024fa340`–`0x024fa400` (rather than trusting reference-manager
counts, per the `getReferencesTo` caveat §11.16.5 already recorded) found exactly one writer,
`FUN_008ca670`. Its entire body is a simple loop: starting from index 0, it stores the loop
counter itself into `DAT_024fa348[index]` and increments, repeating while the index is below
`0x80`.

**`DAT_024fa348[i] = i` for `i` in `0..127` — 128 entries, exactly the shared binding
descriptor's own max slot count (`u16[128]`/`s16[128]`/`u32[128]`, §11.16.2/§11.16.6).**
**[CONFIRMED — disassembly, the exact loop body and bound — no inference needed, the fill is
the identity function by construction.]** Whatever `N` (`≤ 128`, gated by `[this+0xa0]+0x24`)
`FUN_008cb170` actually uses for a given instance, the values it supplies for indices `0..N-1`
are unconditionally `0, 1, …, N-1`.

**`FUN_008cb170` is itself a virtual method, not a directly-called function** — a raw scan found
**zero** direct `CALL` sites to it anywhere in the binary, and its address appears exactly once
as a plain data word, at `0x0116aaf4`, inside a 17-entry function-pointer table
(`0x0116aae0`–`0x0116ab20`) that also contains **`thunk_FUN_00754410`** — the shared destructor
`spec-format-inventory.md` §2/§3 already names as used by registered types **8 ("Character
cvtf"), 9 ("Pcust mesh") and 13 ("Customization Rig")**, i.e. the player-customization family.
**[⚠ Refined §11.19.2: this stub is a generic no-op destructor shared by 11 registered types
(`spec-format-inventory.md` §3), so it is weak evidence of subsystem.]**
**This ties `FUN_008cb170`'s class into the same customization subsystem as `FUN_009f36d0`**,
independently of the fact that both happen to feed the same low-level descriptor constructor.
**[CONFIRMED — disassembly for the vtable slot and its neighbour; HIGH CONFIDENCE — inferred for
the customization-subsystem membership, from the shared destructor alone.]**

#### 11.18.3 `FUN_00753d40` — traced to a sibling registration-table row, one hop short of full confirmation

A raw scan (not `getReferencesTo`, per the same §11.16.5 caveat) finds exactly **one** direct
caller of `FUN_00753d40`, at `0x007542be` — inside sixty-odd bytes of code Ghidra had never
carved into a `Function` (`getFunctionContaining` returned none). Defining it
(`createFunction`, this independent copy only) and decompiling gives a thin pass-through:

`0x007542a0` is a thin pass-through: it calls `0x00753d40` with the same five arguments, and if that call's own low byte reads as `0`, returns that same result immediately. Otherwise it zeroes five further fields on the receiver, sets two of them to `-1` and one to `1`, then returns `1`.

Both `FUN_00753d40` (`0x01152d58`) and `FUN_007542a0` (`0x01152d68`) turn up, sixteen bytes
apart, as **plain data words** in `.rdata` — not reached by any `CALL` at all for
`FUN_007542a0` either (zero raw callers). Reading the surrounding `.rdata` bytes as a string
decodes cleanly, byte-for-byte, to **`mvi_mesh_variant`** (16 characters, null-terminated
exactly at the following aligned dword), followed by two more function-pointer slots
(`FUN_00753ba0`, `FUN_007543b0`) and, further along the same `.rdata` run, the string
**`material_color_variants.xtbl`**. **[CONFIRMED — disassembly/data, the exact string bytes and
the table-slot layout.]**

This is a **previously-uncatalogued name found in `.rdata`** — `mvi_mesh_variant` does
not appear anywhere in `spec-format-inventory.md`'s existing 43-row inventory (built from
`FUN_00700780`). ~~So either it is registered through a separate, smaller table specific to a
material/mesh-variant subsystem, or it is the *real* internal name behind an already-catalogued
row under a different display label. **Not resolved this pass — flagged as a fresh lead for
`spec-format-inventory.md`, not chased further here.**~~ **RESOLVED 2026-09-14 — see §11.18.5:
neither. It is not read by `FUN_00700780` at all (confirmed by direct cross-reference, two
independent methods), and it is not itself a second registration table either — it is a pair of
class vtables/object pools unrelated to the resource-registration system.** Likewise, whether
`material_color_variants.xtbl`'s textual proximity in `.rdata` reflects a real field of the same
row (a declared dependency) or is simply adjacent, unrelated linker output was **not verified**
(no pointer *to* that string was traced from the row) — stated as an open question, not implied.
**§11.18.5 narrows this without fully closing it: the string now sits inside the SAME tight,
self-contained data island as the two vtables (immediately followed by a 4-float format string and
two xtbl field names), which is stronger circumstantial placement than "merely adjacent," but
still no pointer to the string was traced from either class — HIGH CONFIDENCE, not CONFIRMED.**

**What this means for argument 3.** The generic-dispatcher call chain that would show what buffer
argument 3 actually is at runtime was not reached — this hits the same class of boundary
`spec-geometry-format.md` §4.1.2 item 8 already named and stopped at deliberately ("a genuine
scope boundary… the real invocation happens through an indirect call"). What *is* established:
Argument 3 is read at the identical `+0x0a` (`u16` count) / `+0x20` (array pointer) offsets as
`FUN_009f36d0`'s confirmed sub-header object, `FUN_00753d40` sits in the same small module as
`FUN_007527b0`/`FUN_007524f0` (the `.ccmesh_pc` payload parser itself), and it is registered as a
constructor for a type that is very likely another consumer of the same shared `.ccmesh_pc`
payload format (matching the project's own "one format, several registered entry points"
precedent — `.ccmesh_pc` alone already has at least two, types 5 and 9). **[⚠ §11.18.5: this
slot is not a registration row at all but slot 0 of a class vtable, so this analogy is weaker than stated.]** **On that basis, "the
same kind of object as §11.18.1, i.e. array 4 of a `.ccmesh_pc`-shaped sub-header" is HIGH
CONFIDENCE — inferred. It is not CONFIRMED to the same standard as §11.18.1, and is reported
honestly at that lower confidence rather than rounded up.**

#### 11.18.4 Answer to §11.16.4's question, stated plainly

**For the two suppliers whose object this pass could fully identify, `values[]` IS the identity
permutation — confirmed two independent ways, not the same way twice.** `FUN_009f36d0`'s
supplier is array 4 of the `.ccmesh_pc`/`.csmesh_pc` outer sub-header, whose *file content* was
already confirmed identity (`0..67`) on real shipped data before this pass connected the
mechanism. `FUN_008cb170`'s supplier is a fixed engine global whose *fill code* is an explicit
identity loop (`0..127`), confirmed this pass from scratch. **For the third (`FUN_00753d40`),
the object is highly likely the same kind of thing by field-shape and module-locality analogy,
but its own construction site was not reached — recorded as HIGH CONFIDENCE, not CONFIRMED.**

**One scoping caveat, stated rather than smoothed over.** All three suppliers turned out to
belong to the **player-customization subsystem** (registered types 8/9/13's shared destructor
family, plus the newly-found `mvi_mesh_variant` row **[⚠ both qualified: the destructor is a generic 11-type stub, §11.19.2; `mvi_mesh_variant` is not a registration row but a class vtable, §11.18.5]**) — not the plain "Character mesh" (type 5)
path ordinary NPC/story-character bodies use, which shares none of these three functions' raw
callers. This is consistent with the binding-descriptor mechanism's *other* confirmed use
(§11.16.2: vehicle **part**→rig-bone binding, a genuine sub-object-to-whole-rig indirection) —
customization pieces have exactly the same shape (a clothing mesh's own bone palette, bound
against the wearer's full rig), whereas a mesh skinned 1:1 to its own dedicated rig has no
need for the gather/table path at all (§11.3's straight-copy alternative). **Whether the
specific descriptor(s) these three functions build are the same descriptor instance a given
skin-gather record's `+0x3c` consults at skin time, or a separate descriptor serving a narrower
customization-specific purpose, was not directly proven this pass** — the shared-mechanism
argument (all four known suppliers of the one descriptor constructor, vehicle part-binding
included) makes it likely, but this is stated as the honest residual rather than folded silently
into the headline.

#### 11.18.5 The (a)/(b) registration-mechanism question, settled: neither — `mvi_mesh_variant` is not read by `FUN_00700780`, and it is not itself a second registration table (2026-09-14)

> **Scope.** Direct follow-up to §11.18.3's own open lead: is the `.rdata` address `0x01152d58`
> (where `FUN_00753d40`'s pointer sits) read by `FUN_00700780` — the function that builds the
> already-catalogued 43-row table (`spec-format-inventory.md`) — meaning that table has a gap, or
> by a different function entirely? **Deliberately out of scope, per the brief this pass was given:
> the argument-3/generic-dispatcher boundary §11.18.3 already named and stopped at — not reopened
> here.** Fresh, independent Ghidra project copy `tools/gp_rig9`; new scripts `RegXref1.java`,
> `RegXref2.java` in `tools/scripts/`; read-only, no `createFunction`, no edits.
>
> **Predicate, stated before searching (per `HANDOFF.md` §5/§27.8).** A positive finding is either
> (a) confirmation that `FUN_00700780` itself reads `0x01152d58`, or (b) confirmation that a
> different, identifiable function reads it — characterized: what subsystem, how large, what else
> is nearby worth cataloguing. A negative is a documented, bounded search finding no traceable
> consumer at all (dead data, or reached only through an indirect mechanism this pass can't
> resolve). **Result: (b), confirmed by two independent cross-reference methods agreeing exactly,
> then fully characterized by decompilation — and (b) turns out to be a more specific finding than
> the task's own framing anticipated: not a second, smaller *registration table*, but a pair of
> C++-style class vtables / fixed-size object pools with no relationship to the resource-loading
> system `spec-format-inventory.md` documents at all.**

**Part 1 — the cross-reference itself.** `getReferencesTo(0x01152d58)` returns exactly **one**
reference in the entire binary: from `0x00753f96`, inside `FUN_00753f40` (a function in the same
`0x753xxx` address cluster as `FUN_00753d40`/`FUN_00753ba0`, nowhere near `FUN_00700780`). An
independent raw scan of every instruction in the binary for any operand (address or scalar
immediate) equal to `0x01152d58` — the same technique ~~`HANDOFF.md`~~ this document's §11.16.5 already established as
necessary because `getReferencesTo` can under-report — returns the **identical single hit**, same
instruction: `MOV dword ptr [EAX],0x1152d58` at `0x00753f96`. A third check, a raw scan of every
initialized non-`.text` memory block (`.rdata`/`.data`) for any data dword whose *value* equals
`0x01152d58` (i.e. a pointer-to-this-row stored elsewhere, such as an array of row pointers), found
**zero** hits — nothing else in static memory points at this row either. **[CONFIRMED —
disassembly, three independent search methods agreeing, one of them exhaustive over the whole
binary's data segments.]**

**`FUN_00700780` itself was checked directly, not inferred from the above.** Its full body
(`0x00700780`–`0x00701d9f`, 5,664 addresses) was scanned instruction-by-instruction for any operand
in a padded window around the row (`0x01152cf0`–`0x01152f58`). Four hits came back, all at two
addresses (`0x01152cf0`, `0x01152d04`) sitting 0x68/0x54 bytes *before* the row's own data window —
two unrelated globals `FUN_00700780` happens to read for its own bookkeeping, not connected to
`mvi_mesh_variant`'s row in the row dump (§11.18.3's own bytes, re-verified this pass). This is
exactly the "an address window is a population you did not inspect" trap (`HANDOFF.md` §5) turned
the other way — proximity in an address range is not evidence of a relationship, and here it
positively isn't one: **`FUN_00700780` does not reference `0x01152d58` by any method, at all.**
**This settles (a): refuted, directly.**

The neighbouring slot `0x01152d68` (`FUN_007542a0`'s row, per §11.18.3) shows the identical shape:
exactly one reference in the whole binary, from a *different* function again (`FUN_00754770`,
`0x007547c9`), also nowhere near `FUN_00700780`.

**Part 2 — what actually reads it, decompiled in full.** `FUN_00753f40` and `FUN_00754770` are
**pool-allocator / vtable-installer functions**, identical in shape apart from their object size:
each pops a slot from its own fixed-size free-list pool (own base/count/free-list fields at
object `+0xc`/`+0xe`/`+0x14`/`+0x18`, guarded through `FUN_005f6640`), then unconditionally stamps
the freshly-popped slot's first dword with the literal address of the row itself:
`FUN_00753f40` computes the slot address as its pool base plus `slotIndex * 0x68` and stores
`0x01152d58` there as the object's vtable pointer; `FUN_00754770` computes its slot address as
its own pool base plus `slotIndex * 0x28c` and stores `0x01152d68` there as the object's vtable
pointer.

This is the standard MSVC vtable-pointer idiom — the "row" **is** the class's own vtable, and
`FUN_00753d40`/`FUN_007542a0` (the values stored at `0x01152d58`/`0x01152d68`) are each that
class's virtual slot 0. **[CONFIRMED — disassembly and decompile, both functions.]** The two pools
use different object strides — `0x68` (104 bytes) for `FUN_00753f40`'s pool, `0x28c` (652 bytes)
for `FUN_00754770`'s — i.e. **two distinct classes**, each with its own dedicated fixed-size object
pool (roots `DAT_01600930` and `DAT_01603380` respectively — not traced further; identifying these
singletons would mean following the same generic-construction chain this pass was told not to
reopen).

**Each installer has exactly one caller in the whole binary**, and both callers are structurally
identical:

`FUN_007540c0` (the caller of `FUN_00753f40`) takes a lock, calls `FUN_00753f40` to allocate the
object and install its vtable (`0x01152d58`), releases the lock, and — if the allocation
succeeded — calls through the object's vtable slot 0 (`FUN_00753d40`) with the same 5
arguments the caller itself received; a non-zero result returns success (`1`), otherwise it
takes the lock again, releases the object via `FUN_00753ca0`, and returns `0`.

`FUN_00754930` (the sole caller of `FUN_00754770`) is byte-for-byte the same shape, calling through
vtable slot 0 (`FUN_007542a0`) and releasing via `FUN_00754490` on failure. **This is a
virtual-dispatch construct-and-call pattern — allocate, install vtable, call through vtable[0] with
the same 5 arguments `FUN_00753d40`/`FUN_007542a0` are already known to take (§11.18.3) — not a
resource-registration call in the `FUN_00700780` sense at all.** **[CONFIRMED — disassembly and
decompile, both callers.]**

The row's other two function-pointer slots were also decompiled, briefly, for context (not chased
further — this is the row-shape question, not the argument-3 question): `FUN_00753ba0` (`0x1152d60`)
releases one field (`FUN_004bc9d0` on object `+0x4c`) — destructor-shaped; `FUN_007543b0`
(`0x1152d70`) tears down three fields and then itself calls `FUN_00753ba0` — also destructor-shaped,
and its reuse of the other slot's own destructor is consistent with a base/derived or partial/full
teardown split between the two classes. **[CONFIRMED — disassembly and decompile, both functions;
their exact role (simple vs. deleting destructor, base vs. derived) not further pursued.]**

**Net verdict.** (a) is refuted directly and doubly (two independent whole-binary searches agreeing
exactly, plus an explicit negative check of `FUN_00700780`'s own body). (b) holds, but more
narrowly than the original framing anticipated: this is **not** a second, smaller *resource-type
registration table* — no file extension, no destructor-column shape matching `spec-format-inventory.md`'s
43 rows, and above all **no indexed/looped table mechanism at all** (each installer has exactly one
caller, each pool root is a single fixed global, not selected by any row number). It is a
self-contained pair of C++-style class vtables backing two fixed-size object pools (104 and 652
bytes), evidently the construction machinery for one xtbl-driven subsystem — the immediately
adjacent strings (`material_color_variants.xtbl`, the format string `"%f %f %f %f"`, and the xtbl
field names `_Entry_ID`/`color_entry`, all read directly off the same `.rdata` bytes §11.18.3
already dumped) read coherently as a material-color-variant entry parser (a small, 104-byte
per-entry object, plausibly one `color_entry` node's 4 floats) and a larger container/manager
object (652 bytes) for the document as a whole. **This reading is HIGH CONFIDENCE, not
CONFIRMED** — no pointer from either class to those strings was traced, so the connection rests on
data-proximity within one coherent, small `.rdata` island rather than on a followed reference.
`mvi_mesh_variant` itself is best read as this class pair's own internal type-name label, not a
registered resource type in `spec-format-inventory.md`'s sense — no sibling type names were found
nearby (the neighbouring strings are xtbl schema field names, not other registered-type names), and
there is no "table" of rows to catalogue at any scale beyond this one pair.

**What was deliberately left untouched, per this pass's own scope.** What `DAT_01600930` /
`DAT_01603380` are, what calls `FUN_007540c0`/`FUN_00754930`, and where their arguments 1–5
ultimately originate — this is the same argument-3/generic-dispatcher boundary §11.18.3 already
named and stopped at deliberately, not reopened here.

### 11.19 A fully independent second pass on `FUN_008cb170`/`FUN_009f36d0` — same verdict reached from scratch, plus one new fact §11.18 did not have: `FUN_008cb170`'s class, by RTTI, is `object_rig` itself (2026-09-13)

> **Scope.** This is the sibling half of the task that produced §11.18 above, run in parallel
> on a fully independent Ghidra project copy (`tools/gp_rig8`, never shared with `gp_rig7`) and
> written up **before** reading §11.18's own text. New scripts `Rig8Decomp1-3.java`,
> `Rig8Data1-2.java`, `Rig8Callers1-2.java`, `Rig8Final1-2.java`, `Rig8RttiScan.java` in
> `tools/scripts/`; read-only. §11.18 already answers this section's brief in full for both
> functions — this section does not repeat that work, only (a) records that a fully independent
> trace reached the identical verdict on every checkable point, which is itself worth stating
> plainly rather than silently folding in, and (b) adds one fact §11.18 did not have.
>
> **Predicate, stated before tracing (per `HANDOFF.md` §27.8), unchanged from §11.18's own:** a
> positive finding is confirmation that a supplier's `values[]` is the identity permutation, or
> confirmation that it is a genuine different permutation; a negative is a documented, bounded
> trace reaching a genuine boundary. **Result: (a) for both, independently reproduced — see
> §11.19.1 — plus a new, decisive fact for `FUN_008cb170`'s carrier identity (§11.19.2) that
> refines, without overturning, §11.18.2's own "customization subsystem" reading.**

#### 11.19.1 Independent reproduction, point for point

Without reading §11.18 first, this pass decompiled and disassembled both functions and reached
every one of the following, matching §11.18 exactly:

- `FUN_009f36d0` reads `N` from a 2-byte field at offset `+0xa` of the object held in argument 2's own
  second field, and `values` from a 4-byte field at that same object's offset `+0x20`, then mints
  a key and forwards `{key, N, values}` through `FUN_004bc9c0` → `FUN_004c1350` →
  `thunk_FUN_004c1730` — the identical offsets and chain §11.18.1 documents.
- `FUN_008cb170` reads `N` from offset `+0x24` of the object reached via `this`'s own `+0xa0`
  field, and sets `values` to the fixed address `&DAT_024fa348` — matching §11.18.2's disassembly
  exactly, down to the same instruction pair (register `EAX`'s `+0x24` field read into `ECX`, then
  the literal `0x24fa348` stored to the stack, in this pass's own numbering).
- `DAT_024fa348` has exactly one writer in the whole binary, `FUN_008ca670`, whose entire body
  is a loop storing each index 0…127 (`0x80` entries) into the corresponding element of `DAT_024fa348` — byte-for-byte the same finding as
  §11.18.2, reached via a raw xref/operand scan rather than `getReferencesTo` in both passes
  independently. This pass additionally confirmed the call site: `FUN_008ca670`'s **only**
  caller is `0x005d3006`, inside a long unconditional straight-line run of dozens of unrelated
  subsystem-`init` calls in `FUN_005d25f0` (no `TEST`/`Jcc` anywhere across
  `0x005d2fc5`–`0x005d3055`) — i.e. the identity fill runs unconditionally at engine start, not
  gated on any per-instance condition. §11.18.2 established *that* it's an identity loop; this
  adds that it is also unconditional and singular, closing off "maybe it's sometimes filled
  differently" as a possibility.
- `FUN_008cb170` has **zero** direct `CALL` sites anywhere in the binary and exactly one
  reference — a plain data word at `0x0116aaf4` — matching §11.18.2's own address and count
  exactly, from an independent raw scan.
- `FUN_004c1350`/`thunk_FUN_004c1730` were independently decompiled and read at the instruction
  level this pass (not done in §11.18, which cites §11.16.2's prose): `FUN_004c1350` takes a
  pointer to `{key@+0x0, N@+0x4, values_ptr@+0x8}`, allocates/reuses a `0x410`-byte slot in the
  160-entry `DAT_02f5e010` table, and for `i` in `[0,N)` reads a **32-bit** dword at
  `values_ptr + i*4` (confirmed `*4` — a real `u32[]`), writing the truncated 16-bit value into
  the slot→value table at `+0x10+i*2` and the inverse `s16` at `+0x110+value*2 = i`; no bound
  check against the 128-slot capacity exists — callers are trusted to keep `N ≤ 128`.
  `thunk_FUN_004c1730` (confirmed via `getThunkedFunction`) targets `FUN_004c1730`, which
  linear-scans `DAT_02f5e010` for a matching key and stores the result at the new record's
  `+0x3c`. This reproduces §11.16.2/§11.16.6's account from raw instructions, independently of
  both the original pass and of §11.18.

**Two fully independent passes, on two fully independent Ghidra copies, arriving at the
identical mechanism and the identical identity-permutation verdict for both suppliers, is
stronger corroboration than either pass alone** — the kind of redundant-but-cheap
cross-check `HANDOFF.md` §27.8's own worked example recommends when a finding matters.

#### 11.19.2 The one new fact: `FUN_008cb170`'s class is `object_rig`, by RTTI — and a caveat on reading its vtable neighbor as customization-specific

§11.18.2 identifies `FUN_008cb170` as a vtable slot (`0x0116aaf4`) whose table also contains
`thunk_FUN_00754410`, and reasons from that neighbor to "the same customization subsystem as
`FUN_009f36d0`" (types 8/9/13's shared destructor). This pass instead read the vtable's own
RTTI, which neither pass had used before: the slot-4 table at `0x0116aae4` has a standard MSVC
complete-object-locator at `vtable-4` (`0x0116aae0 = 0x012afb30`), whose `+0xc` field points at
a `TypeDescriptor` at `0x01305d5c` whose name string (`+0x8`, standard layout) reads literally
**`.?AVobject_rig@@`** — MSVC mangling for `class object_rig`. A same-pass sweep for related
RTTI names independently turned up `concrete_instantiator<object_rig>` (`0x01305eb4`,
confirming `object_rig` is concrete/instantiable) and a **separate** class `anim_rig` with its
own `hash_table<anim_rig*>`/`data_hash_table<anim_rig*, rig_bone_info>` (`0x01305d80`,
`0x01305e48`) — plausibly the animation-instance registry §11.16.6 already documents under a
different label, distinct from `object_rig` itself. **[CONFIRMED — disassembly; RTTI is
compiler-emitted data, citable per this project's own convention for literal strings,
`spec-format-inventory.md`'s methodology note.]**

**This refines §11.18.2's carrier reading rather than contradicting its mechanism finding.**
`thunk_FUN_00754410` is independently documented, by this project's own earlier work, as a
**generic no-op stub reused as the destructor by 11 different registered types**
(`spec-format-inventory.md` §3: *"a generic/no-op destructor for types needing no special
teardown, not a meaningful format relationship"* — confirmed there from the function's own
body, a bare `return`). Finding it as a vtable neighbor is consequently weaker evidence for
*which* class than a name would be — the same caution this project has already had to apply to
this exact stub once before applies again here. `object_rig`, by contrast, reads as the
**generic runtime rig-instance wrapper** — the natural class for *any* rigged game object
(player, NPC, or a non-customization rigged entity) to carry its skinning/attachment state on,
not a class scoped to customization items specifically. This is consistent with, not opposed
to, §11.18.4's broader finding that the *specific instances* reaching `FUN_008cb170` in this
game's actual content are customization pieces (§11.18's evidence for that — the
`PCC_FINALIZE`/type-9 registration match for `FUN_009f36d0`'s own call chain — is untouched by
this note and remains the stronger, more specific finding of the two); it just means the
*class* `object_rig` itself is not, by name or by RTTI, a customization-exclusive type — the
customization-specificity lives in which resource gets attached at a given call, not in the
vtable slot's own class.

One more data point in `FUN_008cb170`'s own body, not examined in §11.18: **immediately after
registering the binding descriptor, the same method formats a name from the two fields it
gated on at entry (`this+0x8c`/`this+0x90`), hashes it via `FUN_00d9e8b0`** (the engine's
second, CRC-32 string hash, `HANDOFF.md` §2b `ENGINE_HASH`), **resolves a handle, and starts an
animation blend at weight `0x3e99999a` = `0.3f` via `FUN_004b2280`.** Read together with the
identity-permutation binding, the method's own shape is "attach a bone-bound sub-object to this
`object_rig` instance, then start playing an animation on it in the same call" — plausibly the
mechanism behind attaching a held prop/weapon (with its own small bone set, hence the identity
mapping — no remapping needed when the sub-object's own slot space already IS the space it
binds into) and kicking off its idle/held animation, though the exact semantic identity (prop?
weapon? IK target?) is not pinned by any string or registration row and is left open rather
than guessed, per `HANDOFF.md` §27.8.

#### 11.19.3 Net effect

No change to §11.18's headline verdict: `values[]` is confirmed the identity permutation for
both `FUN_009f36d0` (`0..67`, via the `.ccmesh_pc` array-4 mechanism) and `FUN_008cb170`
(`0..127`, via the fixed engine-global fill loop), now on two independent traces rather than
one. The addition here is narrow and specific: `FUN_008cb170`'s own class is `object_rig`,
confirmed by RTTI rather than inferred from a shared stub, and that class reads as generic
rig-instance infrastructure rather than customization-exclusive — a refinement worth recording
precisely because the shared-stub inference it corrects is a pattern (`FUN_00754410` as a
false-signal neighbor) this project has now been bitten by, or nearly bitten by, more than
once.

## 12. Registered type ID 13, `Customization Rig` — traced: stash-only into the customization singleton (2026-09-18)

`spec-format-inventory.md` listed ID 13's constructor, `LAB_009f0a20`, as “format yes (`spec-rig-format.md`); this variant explicitly flagged untraced there” (§10 item 6 of this document). Decompiled in full — it is an 8-instruction function, confirmed at the raw x86 level: it loads the customization singleton pointer (`DAT_0263e0f4`) and compares incoming argument 2 against that singleton's own `+0x10` field; on a mismatch it returns `false` immediately. On a match, it stashes incoming arguments 3 and 4 verbatim into the singleton's own `+0x1a8`/`+0x1ac` fields and returns `true`.

**[CONFIRMED — disassembly, the complete function body, 8 instructions, one branch.]**

**`DAT_0263e0f4` is the same customization singleton `spec-morph-format.md` §12.3 already names** — 14 users, all in the `0x009e`–`0x009f` module range, with ~~types 11/12's morph stashes writing name/buffer fields into it at `+0x68`/`+0xac..+0xb8` and `+0xbc`/`+0x100..+0x10c`~~ **[CORRECTED 2026-09-20, `spec-morph-format.md` §16.1: `+0x68`/`+0xac..+0xb8` is type 10 `Pcust peg`'s stash, `+0xbc`/`+0x100..+0x10c` is type 11 `Pcust morph`'s, and type 12's is `+0x15c`/`+0x1a0`/`+0x1a4`; §16.3: the "singleton" is really the current job of a 64-entry pool, and 17 functions — not 14 — reference it]**. `LAB_009f0a20` is one of those ~~14~~ users **[17 per the correction just above]**, writing a **different** pair of fields, `+0x1a8`/`+0x1ac`. **[CONFIRMED — disassembly, same global address, same module range.]**

**This is stash-only, the same family as ID 11/12's morph variants (`spec-morph-format.md` §8), but mechanically simpler.** Types 11/12 copy up to `0x41` bytes of the resource's *name string* into the singleton before stashing buffer pointers; `LAB_009f0a20` does no string copy at all — it stores its two raw incoming values (whatever they are: pointers, handles, or plain integers — not determined by this pass) directly, gated on a bare equality test rather than any parsing. **No `.rig_pc` bytes are read anywhere in this function — no buffer is even dereferenced past the singleton itself.** **[CONFIRMED — disassembly.]**

**Comparison against ID 20's real `.rig_pc` parser, already fully characterized in this document:** `FUN_00748280` calls a cache-check wrapper into `FUN_004bc810` → `FUN_004d0cb0` (pool allocation) → `FUN_004d0bb0` (the actual bone/hash-table/attachment-point parser this whole document describes). `LAB_009f0a20` calls **nothing** — no callees at all, no file-buffer parameter read, nothing but the singleton touch above. So the answer to this task's question is unambiguous: **stash-only, does not parse `.rig_pc`, matching the shape (though not the exact mechanism) of ID 11/12's morph stashes.** **[CONFIRMED — disassembly.]**

**RESOLVED 2026-09-20 (`spec-morph-format.md` §16.2, §16.4, §16.6): the consumer is `FUN_009f36d0` itself, and the paragraph below is wrong that it does not touch `+0x1a8`/`+0x1ac` — it reads both (buffer, length), copies the buffer and calls `FUN_004bc810(name+suffix, slot, copy, length, 0)`, the same rig cache-load wrapper that ID 20's constructor `FUN_00748280` reaches with the same name-suffix constant. The gate field `+0x10` is the streaming container the current job is waiting on.** The paragraph as originally written follows, kept for the record.

**What remains open (as of 2026-09-18), stated honestly rather than guessed at:** which subsystem later reads singleton `+0x1a8`/`+0x1ac` and turns them into an actual loaded rig is not identified by this pass. The one confirmed consumer of the *morph* stash slots, the customization asset assembler `FUN_009f36d0` (`spec-morph-format.md` §12.3), is confirmed to touch only the eight morph-stash fields (`+0x68`, `+0xac`, `+0xb0`, `+0xb4`, `+0xb8`, `+0xbc`, `+0x100`, `+0x104`, `+0x108`, `+0x10c`) — **not** `+0x1a8`/`+0x1ac` — so it is confirmed **not** to be this stash's consumer, and no replacement candidate was searched for in this bounded pass. The gate value's meaning (what singleton `+0x10` actually tracks — a name hash, a selected component ID, something else) is likewise **not pursued**. **[~~OPEN~~ RESOLVED 2026-09-20 — the stash's consumer and the gate field's meaning are identified, `spec-morph-format.md` §16.2/§16.4/§16.6; this paragraph's claim that `FUN_009f36d0` does not touch `+0x1a8`/`+0x1ac` was wrong.]**

## 13. The runtime rig object, consumer-side: bone-by-name is a string compare, header `+0x28`/`+0x2C`, the attachment record is a rotation matrix (2026-09-20)

> **Scope.** Agent AD (`gp_rig10`), 2026-09-20: the **consumer side** of the rig's runtime fields — §10 items 2, 3 and 5 (header `+0x2C`, bone `+0x24` and the attachment fields, and the bone-by-name routine `FUN_004bc880`). Method: full decompile plus raw disassembly of the accessor family (`0x004bc7c0`–`0x004bcd00`, the loader `0x004d0bb0`–`0x004d0d50` and the bone/attachment getters `0x004d0a50`–`0x004d1160`), raw-instruction call-site enumeration (not the reference manager), and the 585-rig population. Harnesses: `tools/harnesses/rig10_header.py`, `rig10_joint.py`, `rig10_names.py`, `rig10_tail.py`, `rig10_validate.py`, `rig10_att.py`, `rig10_att2.py`, `rig10_att3.py`, `rig10_anim_split.py`; Ghidra scripts `tools/scripts/Rig10*.java`; outputs `tools/rig10_*.txt`. **Three results, one of them a correction to this document's own §5/§6, and one honest negative:** (1) bone-by-name is a case-insensitive **string** compare — it never reads the hash table; (2) header `+0x28` exists, is the partner of `+0x2C`, and `+0x28 + +0x2C = bone_count` in 585/585 — but **no runtime reader of either was found**; (3) the attachment record is a **rotation matrix + translation, not a quaternion** (§6's reading is retracted), 12,480/12,480.

### 13.1 `FUN_004bc880` is a string compare — the hash table has no consumer in the runtime paths read

**What it does.** `FUN_004bc880(rig, name)` returns `-1` if `rig` is null; otherwise it tail-calls `FUN_004d0a90`, which walks the bone array (`rig+0x40`, stride `0x28`, exactly `bone_count = rig+0x24` entries) and compares each bone's **name string** (bone `+0x00`, the pointer the loader fixes up) against `name` with the CRT case-insensitive compare (`FUN_00eaa178`, labelled `_stricmp` by the decompiler). It returns the index of the **first** match, or `-1`. **No hash is computed, the per-bone hash table (`rig+0x38`) is not touched, and there is no cache inside the routine** — every call is an `O(bone_count)` scan; callers keep the returned indices. **[CONFIRMED — disassembly, `FUN_004bc880` and `FUN_004d0a90` read in full.]** §5's "assumed the hash table's consumer" is therefore **refuted** (struck in place there). The old bookmark comment's "returns `0xff` when missing" is also not the routine's contract: it returns the 32-bit `-1`; the vehicle binder (below) narrows to a byte itself.

**The sibling accessors, all by string compare** (raw `CALL` enumeration over the whole `.text`; each row's caller list is exhaustive for direct calls). "Registry id" means the anim-instance record index (`spec-rig-format.md` §11.16.6), which reaches the rig through record `+0x14`.

| Routine | Behaviour | Callers (sites) |
|---|---|---|
| `FUN_004bc880` `(rig, name)` | exact, case-insensitive, bone array → bone index | **61 sites**: `FUN_00951250` ×18, `FUN_00a856d0` ×3, `FUN_00ab0730` ×6, `FUN_00bddee0` ×28, plus 6 in un-owned code at `0x00bde3ad`–`0x00bde704` |
| `FUN_004bc860` `(rig, name)` (→ `FUN_004d0a50`) | **case-insensitive substring** (`FUN_00da7780`, a naive scan that does not re-test the mismatching character as a new start) → first bone whose name *contains* the query | `FUN_0077a800` ×24 only |
| `FUN_004bc8a0` `(rig, name)` (→ `FUN_004d0ad0`) | exact, case-insensitive, **attachment** array (`rig+0x48`, stride `0x40`) → **`bone_count + a`** | 28 sites: `FUN_00951250` ×18, `FUN_00970e70` ×1 (`pelvis`), `FUN_00bddee0` ×7, un-owned code ×2 |
| `FUN_004be200` `(registryId, name)` (→ `FUN_004d0da0`) | as `FUN_004bc880`, via the record's rig | 9 sites in 5 functions (`0x0057f970`, `0x00729720` ×3, `0x008fb7c0`, `0x00942300`, `0x009b2de0` ×3 — constants `pelvis`, `spinebend`, rest dynamic) |
| `FUN_004be170` `(registryId, name)` (→ `FUN_004d0e00`) | attachment by name → `bone_count + a` | 14 caller functions |
| `FUN_004be1b0` (→ `FUN_004d0e70`) `(registryId, name, n, list)` | attachment by name **plus a tag filter** (§13.4, `+0x3C`) | `FUN_00980e20` only |

**So bones and attachments share one index space: `i < bone_count` is bone `i`, `i ≥ bone_count` is attachment `i − bone_count`.** `FUN_004d0f40` bounds its argument with `bone_count + attachment_count`; `FUN_004d0fe0` (the transform getter, §13.4) splits on `bone_count`. **[CONFIRMED — disassembly.]**

**What callers pass** (constant string sites; the string is the argument nearest the call):

- **`FUN_00ab0730`** (the vehicle part→rig-bone binder, `spec-vehicle-geometry.md` §6): the rig is found by `FUN_004bc7d0(name, -1)`; then **two dynamic sites** (the anchor name at vehicle-header `+0x3C`, then every part name, stride `0xE0`) and four constants `camera`, `camtarget`, `camFOV`, `camDOF`. Any result `≥ 0xFF` (including `-1`) is stored as byte `0xFF`; hits are also set in a 128-bit presence mask, and the binder then **sets every bit `< rig.bone_count`** before building the descriptor's value list. **[CONFIRMED — disassembly.]**
- **`FUN_00a856d0`**: `camera`, `camFOV`, `camDOF`.
- **`FUN_00bddee0`** — a per-record initializer over a global table of `0xEC`-byte records (table pointer `0x029972F8`, count `0x029972F4`; each record's rig is found by name at record `+0x20` and key at `+0xD8` and stored at `+0x24`, `FUN_00bde1d0`): 28 bone names (`root`, `spine`, `spine2`, `pelvis`, `head`, `neck`, `l-eye`/`r-eye`, `l/r-thigh`, `l/r-calf`, `l/r-foot`, `l/r-toe0`, `l/r-upperarmtwist1`, `l/r-foretwist`, `L-hand`/`R-hand`, `L-IKNode`/`R-IKNode`, `camTarget`, `Camera`, `CamFOV`, `CamDOF`) and 7 attachment names (`l-handprop`, `r-handprop`, `l/r-foretwist1`, `HSAttach`, `HS-R-Hand`, `flame`). **Mixed-case names (`L-hand`, `CamFOV`) against lowercase shipped bones (`l-hand`, `camfov`) is why the compare must be case-insensitive.**
- **`FUN_00951250`**: 18 bone lookups — `l/r-foretwist1`, `l/r-upperarmtwist2`/`3`, `l/r-calftwist1`, `l/r-thightwist1`, `l/r-elbow`, `l/r-knee`, and `l/r-hand`, `l/r-foot` — and 18 attachment lookups: the per-bone points `l/r-hand`, `l/r-foot`, `l/r-thigh`, `l/r-calf`, `l/r-foretwist`, `l/r-upperarmtwist1` (twice), and **only the `a` target of each IK triple**, `l_elbow_targeta`, `r_elbow_targeta`, `l_knee_targeta`, `r_knee_targeta` (§6's IK pole targets, named).
- **`FUN_0077a800`** (substring form): `head` (×3), `root`, `spine1` (×2), `spine2` (×2), `r/l-thigh`, `r/l-calf` (×2), `r/l-upperarm`, `r/l-foretwist` (×2), `r/l-foot`, `r/l-hand` — a 24-slot bone-index table. The substring form is what makes `r/l-upperarm` work: **no humanoid rig has a bone named exactly `l-upperarm`** (0 / 281 of the `T = 45` rigs), while `l-upperarmtwist1` is present in 281 / 281 — the query resolves to the twist bone that plays the upper arm. **[CONFIRMED — empirical for the names; the rationale is HIGH CONFIDENCE — inferred.]**

**Negative for the hash table itself, stated with its predicate.** The engine's name hash `FUN_00da7890` has **12 callers** (raw `CALL`/`JMP` flow scan, whole `.text`): `0x004aded0`, `0x004b1900`, `0x004ca2d0`, `0x004ca340`, `0x004d0260`, `0x00747920`, `0x00747970`, `0x00747ae0`, `0x008df690`, `0x008df740`, `0x00ddab60`, `0x00ddac20`. All 12 were decompiled; **none touches a rig object** (anim-graph trigger names, the foliage-mesh registry, xtbl name registries, a UI table). And no instruction in `[0x004bc000, 0x004d1000)` reads rig `+0x38` other than the loader's own fix-up (§13.6). **Predicate: "no function that calls the shared name hash, and no rig-object read in the accessor range, consumes the hash table."** It does *not* exclude an inlined copy of the rotate-6/XOR loop elsewhere. **[HIGH CONFIDENCE, bounded: the per-bone hash table is loader-side/authoring data with no runtime reader found.]** The table is still exactly the hash of each lowercased bone name (§5's 22,274/22,274 stands); only the sentence about its consumer is withdrawn.

### 13.2 Header `+0x28` and `+0x2C`: a partition pair, `+0x28 + +0x2C = bone_count` in 585/585 — and no reader found

**New empirical fact.** §3.1 listed `+0x28` as "not characterized". It is a count, and

> **`+0x24 (bone_count) == +0x28 + +0x2C` in 585 / 585 rigs.** Controls: the same sum test over every other adjacent header pair scores 0 / 585 (`+0x00`…`+0x1C`, `+0x24`+`+0x28`, `+0x30`+`+0x34`, `+0x34`+`+0x38`, `+0x38`+`+0x3C`); the one pair that also scores 585/585, `+0x20`+`+0x24`, is vacuous (`+0x20` is zero in 585/585 on disk). **[CONFIRMED — empirical, `rig10_validate.py`.]**

So the bones split into a **leading group `[0, K)`** with `K = +0x2C` and a **trailing group `[K, bone_count)`** of `T = +0x28` bones. `T` is a *class constant*, not a per-rig measurement: **45** in 281 rigs (the standard humanoid rigs, bone counts 65–109, 29 distinct; the one-off full rigs `tiger` = 31 and `avatar` = 51 differ), **3** in the 260 head rigs (`bone_count` 5; trailing group `[head, l-eye, r-eye]` in 260/260), **1** in 37 rigs (vehicles/props: the last bone), and 2/6/51/8/17/31 in one or two rigs each (nine distinct values in all). **[CONFIRMED — empirical.]**

**What this does to §7's reading.** The boundary bone `bone[K]` is `l-finger1` or `l-hand` in only **186 / 281** of the `T = 45` rigs (29 distinct names — `r-finger4`, `l-finger31`, `r-foretwist`, … fill the rest), and hands move between the groups (`l-hand` is in the leading group in 230 and the trailing group in 51; `l-finger1` in 89 vs 192; extra accessory bones appended to a rig push hands out of the trailing 45). Because `T` stays 45 while the *contents* shift, **`K` is `bone_count − 45` by arithmetic, not "the first detail bone" by name** — the "first bone of a detail section" pattern is a property of the standard humanoid template (leading 23 core bones, then 22 finger bones, then 23 helper bones: `spinebend`, the four camera bones, eyes, `l/r-upperarmtwist2`/`3`, `l/r-foretwist1`, `l/r-thightwist1`, `l/r-calftwist1`, `l/r-elbow`, `l/r-knee`, and the breast bones — most of which §13.1's `FUN_00951250`, `FUN_00bddee0`, `FUN_00a856d0` and `FUN_009b2de0` look up **by name**), not something the field encodes. **[HIGH CONFIDENCE — inferred from the population; `rig10_tail.py`, `rig10_names.py`.]**

**Side observation, not evidence of consumption.** For the 3,658 identity-mapped (`+0x28 == 0`) `.anim_pc` clips whose bone count selects a `T = 45` rig class, the clip's track count `+0x0A` exceeds `K` in 3,657 and `+0x0A − K == 22` (the finger-bone count) in 2,223 (61%) — the clips animate the leading group plus the finger bones and leave the helper tail to code. `+0x0A` is the clip's own field; nothing reads `K` to produce it. **[HIGH CONFIDENCE — empirical association only, `rig10_anim_split.py`.]**

**Consumer search — result: none found.** The two fields are copied verbatim into the runtime object by the loader (§13.3) and are **not read by it**. Scopes and predicates (§13.6): (a) every decoded instruction in `[0x004bc000, 0x004d1000)` — 25,226 instructions, 384 functions, the whole rig accessor / anim-registry / anim-graph region; its 49 non-stack `[reg+0x2C]` operands fall in 13 functions, each read (anim-graph nodes, list nodes, SSE arithmetic); (b) an anchored 681-function set (123 registry-global referencers + 55 direct callers of 22 accessors + 503 direct callees): all 118 non-stack `[reg+0x2C]` operands (in 49 functions) were classified by how their base register was last defined in a ≥ 8-instruction window, 15 of them read in full; (c) 17 named skinning-path functions (`FUN_00e649f0`, `FUN_00b91a10`, the palette allocator/load/gather, `FUN_00476ca0`, `FUN_008cb170`, `FUN_00753d40`, `FUN_004c1350`, `FUN_004c31b0`, `FUN_004c7640`, …). **No operand in any of them is a `+0x2C` (or `+0x28`) read through a rig-object pointer**: every hit resolves to a stack struct copy, a 48-byte pose-matrix row, an anim-graph node, a list node, or SSE arithmetic. **The bone-LOD hypothesis therefore has no code behind it in the ranges read** — the accessors that *do* exist bound by `bone_count` (`+0x24`) and the attachment count (`+0x30`) only. **[CONFIRMED — disassembly, for the stated scopes; HYPOTHESIS "bone-LOD boundary" — UNSUPPORTED, downgraded.]** A whole-binary "register with `+0x2C` and `+0x24`/`+0x40`" predicate returns 459 (function, register) pairs and was abandoned as unselective, per `HANDOFF.md` §5.

### 13.3 The runtime rig object (`0x60` bytes, 256 cached slots)

`FUN_004bc810` → `FUN_004d0cb0` allocates from a **256-slot cache of `0x60`-byte objects at `0x035198F0`** (flags live at slot `+0x20`, first flag word at `0x03519910`, bound `0x0351F910`; `0x6000 / 0x60 = 256`); `FUN_004bc7d0` → `FUN_004d0d50` finds a slot by (in-use bit, key at `+0x54`, case-insensitive name at `+0x00`). `FUN_004d0cb0` **rejects names of 32 or more characters**, zeroes the slot, runs `FUN_004d0bb0` (which copies the `0x50`-byte header), then overwrites the front of the copy with the name. **[CONFIRMED — disassembly.]**

| Runtime `+` | Content | On disk |
|---|---|---|
| `0x00`–`0x1F` | the rig's registered name, NUL-terminated (≤ 31 chars) — **overwrites file-header `+0x00`–`+0x1F`** | zero in 585/585 |
| `0x20` | flag word: bit 0 = slot in use; bit 1 = set iff the loader's fifth (flag) argument is non-zero (meaning not traced) — overwrites header `+0x20` | zero in 585/585 |
| `0x24` `0x28` `0x2C` `0x30` | bone count · `T` · `K` · attachment count | as §3.1 / §13.2 |
| `0x34` | not read or written by the loader | zero in 585/585 |
| `0x38` `0x40` `0x48` | fixed-up pointers: hash table · bone array · attachment array (`0` if the file field was `-1`); each high-half slot (`0x3C` `0x44` `0x4C`) zeroed | as §3.1 |
| `0x50` | the buffer length handed to the loader (`FUN_004bc810`'s fourth argument); copied into the anim-instance record at `+0x39C` by `FUN_004c1730` / `FUN_004bca80` | — (runtime) |
| `0x54` | the cache key (`FUN_004bc810`'s second argument; the vehicle binder looks its rig up with key `-1`) | — |
| `0x58` | pointer to the loaded file buffer (the bone/attachment arrays live *inside* it, fixed up in place) | — |

**[CONFIRMED — disassembly for the runtime columns; CONFIRMED — empirical for "zero on disk", 585/585.]** Consequence for §3.1: header `+0x00`–`+0x23` and `+0x34` are all-zero reserved space in every shipped file; the loader never reads them.

### 13.4 The attachment record is a rotation matrix + translation, not a quaternion (corrects §6)

`FUN_004d0fe0` — the "get the transform of bone-or-attachment `i`" routine behind `FUN_004bd020` (3 callers), `FUN_004bd840` (7) and `FUN_004d0f40` ← `FUN_004bd7f0` (**54 distinct caller functions**) — reads an attachment `a = i − bone_count` (valid for `0 ≤ a < rig+0x30`) as follows: **the parent bone from record `+0x38`; record `+0x08` as a pointer to nine floats (a 3×3 rotation) and record `+0x2C` as a pointer to three floats (a translation)**, both handed to `FUN_004aac40`, which builds a 4×4 (rows padded with `w = 0`, translation in row 3 with `w = 1` — the same 48-byte affine layout as §11.2's palette matrices). With a live pose array (`rec+0x04`) the result is *attachment matrix × parent bone's pose matrix* (`FUN_0048cf50`); in the un-posed path the translation row has the parent bone's **`+0x08` rest position subtracted** (`0x004d1100`–`0x004d1118`). **[CONFIRMED — disassembly, `FUN_004d0fe0`, `FUN_004aac40`, `FUN_0048cf50` read in full.]**

| Offset | Size | Content | Evidence |
|---|---|---|---|
| `+0x00` | 4 | name → pointer (fixed up) | compared by `FUN_004d0ad0` / `FUN_004d0e00` / `FUN_004d0e70` (§13.1) |
| `+0x04` | 4 | runtime slot, zeroed by the loader | zero on disk 12,480/12,480 |
| **`+0x08`** | 36 | **3×3 rotation, nine `f32`, three rows of three (rows at `+0x08` / `+0x14` / `+0x20`)** | see below |
| **`+0x2C`** | 12 | **translation, `vec3 f32`** (bone-local: median `|t|` = 0, p95 0.44) | see below |
| `+0x38` | 4 | parent bone index | `FUN_004d0fe0`; `< bone_count` 12,480/12,480 (§6) |
| **`+0x3C`** | 4 | signed int **tag**; **`-1` in 12,480/12,480**. Read only by `FUN_004d0e70`: among the attachments whose name matches, one whose tag is in the caller's list is returned at once (a tag of `0` in a list of two or more is held back as a second choice); an untagged (`-1`) name match is only the **fallback, and the last such match wins**. With shipped data (every tag `-1`) the tag list is therefore inert and the routine returns the **last** name match | disassembly (`0x004d0ebf`–`0x004d0f34`) + empirical |

**Empirical validation (`rig10_att.py`, 12,480 records).** The three rows at `+0x08` are **orthonormal (`|norm−1| < 0.01`, `|dot| < 0.01`) with determinant `+1` in 12,480 / 12,480** — a proper rotation. Controls: the same test with the first row at any other 4-byte offset from `+0x00` to `+0x1C` (excluding `+0x08`) scores **0 / 12,480** at both `0.01` and `0.05` tolerance; each such control *can* fail and does. The three floats `+0x20`–`+0x2B` are a unit vector within 1% in 12,480/12,480 — which is exactly why §6's "quaternion at `+0x20`, within 10% of unit in 12,473/12,480" looked right: the fourth float of that "quaternion" is the translation's `x`, and the 7 records outside the 10% band are exactly ones with `|t.x| ≥ 0.46` and `|row 2| = 1.0000` (checked). **⚠ §6's `+0x10` / `+0x20` / `+0x30` readings are withdrawn:** `+0x10` is `row0.z`, `+0x14`–`+0x1C` is row 1; `+0x20`–`+0x28` is row 2; `+0x30` is translation `y`. **[CONFIRMED — disassembly + empirical.]**

**What the rotation is, for the 68% "named after the bone" attachments.** 8,490 / 12,480 attachments carry a **zero translation**, and they are precisely the same-name-as-parent set: 8,489 records are both, 1 is zero-translation-only, 1 is same-name-only. **Predicate made explicit (2026-09-20, found by Team B's independent reader and re-derived by the orchestrator):** these counts use "zero translation" = every component of magnitude ≤ 1e-6. With a strict `== 0` test they read 8,489 zero / 8,488 both / 1 zero-only / 2 same-name-only, because `avatar.rig_pc` `r-upperarmtwist1` (same name as its parent) has t.x = 1.0e-6. The two genuine exceptions: `sp_mdsphere.rig_pc` `center` (translation (-0, 0, -0) but a different name from its parent `mass_damper_shere`) and `distant_human.rig_pc` `spine` (same name as its parent, t = (-0, 0.3087, 0.0257)). Team B independently reproduced the rest of §13.4/§13.2 on all 585 rigs: 12,480/12,480 orthonormal with det +1 (the same test with the first row shifted to seven other offsets: 0/12,480 each), tag == -1 12,480/12,480, `+0x28 + +0x2C == bone_count` 585/585 (adjacent-pair controls 0/585). Their rotation is **not** identity (identity in 2 of 12,480 records overall). **Row 0 of that rotation points along the bone**: across the 5,109 same-name attachments whose parent has a child, row 0 has `|cos| > 0.95` with the direction from the bone to its first child in **3,634 (71%)** (rows 1 and 2: 281 and 0), against 230 / 5,109 (4.5%) for a random direction in the control. These per-bone attachments are therefore **the per-bone orientation frames the bone record lacks** (§4: "the bone record holds no orientation"), with X along the bone under the engine's row-vector convention. **[HIGH CONFIDENCE — empirical; the 29% not aligned (branching/twist bones) is unexplained, and no code reading this *meaning* was traced.]**

### 13.5 Bone record: which fields the runtime actually reads; `+0x24`

| Bone `+` | Runtime reader found | Evidence |
|---|---|---|
| `0x00` name | the six by-name routines of §13.1 | `FUN_004d0a90` etc. |
| `0x08` rest position | `FUN_004d0fe0`'s un-posed attachment path (subtracts it from the attachment translation) | `0x004d1100`–`0x004d1118` |
| `0x14` offset to parent | the animation update `FUN_004c21a0` (`0x004c3139`: loaded per slot through the binding descriptor and **added (`ADDPS`) into a per-slot 16-byte position accumulator**, stride `0x40`, `w` reset to the unit constant at `0x01249140`) and `FUN_004be9f0` (returns it as a 4-float value to the attachment-lag drivers `FUN_00782880` / `FUN_00784a60`) | disassembly — so §4's "precomputed convenience" is the field the runtime *reads* for pose building (the sampled-translation path that feeds the accumulator was not traced) |
| `0x20` parent | `FUN_004be8b0`, `FUN_004c1e20`, `FUN_004c31b0`, `FUN_004c36f0`, `FUN_004d0b20` | disassembly |
| **`0x24`** | **none found — no reader and no writer** | §13.6 |

**Bone `+0x24` (`-1` in 22,274 / 22,274 bones, re-counted this pass): no runtime read or write was found.** The loader touches only `+0x00` (name fix-up) and `+0x04` (zero) of each bone; the records live inside the loaded file buffer, so a runtime slot would have to be an explicit store into that buffer. In the anim/accessor region every indexed `[base + idx + 0x24]` operand is a 48-byte pose-matrix translation row (`FUN_004c31b0`, `FUN_004c1e20`, `FUN_004c36f0`, …), never a bone record, and no store to a bone's `+0x24` exists in the scanned scopes. It is most simply an **unused reserved slot** that ships as `-1`; a consumer outside the scanned scopes is not excluded. **[OPEN — bounded negative: no reader/writer found in the scopes of §13.6; not "confirmed unused".]**

### 13.6 Scan scopes, predicates, and what stays open

Every negative above is a statement about a **predicate over a scope**, listed so it can be re-opened:

- **Range scope** `[0x004bc000, 0x004d1000)`: 86,016 bytes, 25,226 decoded instructions (3,459 bytes not decoded as instructions), 384 functions with an entry inside. Predicates run on it: non-stack memory operands with displacement `0x24`, `0x28`, `0x2C`, `0x30`, `0x38`, `0x40`, `0x48`, `0x50`, `0x54`, `0x58`, and `0x08`/`0x14`/`0x20` for bone-field reads. Known-positive assertions fired (`FUN_004d0a90` reads `+0x24` and `+0x40`; the loader `FUN_004d0bb0`'s own `+0x24`/`+0x38`/`+0x40`/`+0x48` accesses are listed).
- **Anchored scope** (`Rig10Anchored.java`): 123 functions referencing `DAT_0344c924`/`DAT_0344c91a` (**reproduces the 123 of §11.13.2 exactly**) ∪ 55 raw direct callers of 22 rig/record accessors ∪ 503 raw direct callees = **681 functions**; `+0x2C`, `+0x28`, `+0x38` in any addressing form and indexed-form `+0x24`, `+0x38`, `+0x3C`, `+0x10`. Known positives asserted (`FUN_004bcca0` ∈ S1, `FUN_0077a800` and `FUN_00951250` ∈ S2).
- **Whole-binary scans used only for enumeration, not for negatives:** call-site enumeration of the by-name routines and of `FUN_00da7890` (3,546,397 instructions). The displacement-only whole-binary predicate was tried once and abandoned (459 pairs).
- **Not covered:** rig pointers copied into other objects' fields and read by *deeper* (indirect, non-direct-callee) code; inlined copies of the name hash; register-passed record methods outside the range whose callers are not in the anchored set (the §11.16.3 blind spot). Header `+0x28`/`+0x2C` and bone `+0x24` are therefore **"no runtime consumer found"**, not "no consumer".

**Still open:** (a) what the authoring pipeline uses `T`/`K` for; (b) any consumer of bone `+0x24`; (c) the un-posed-path sign of `FUN_004d0fe0` (it subtracts the parent's `+0x08` from a translation that §13.4 shows is bone-local — the convention of the pose array at `rec+0x04` is not pinned); (d) the meaning of row 0 ≠ bone axis for the 29% of same-name attachments.

### 13.7 Cross-document notes (not edited here)

`spec-vehicle-geometry.md` §6 (line ~106) says part names are matched **by hash** against the rig's name-hash table (**HIGH CONFIDENCE — inferred**); §13.1 refutes that — the binder `FUN_00ab0730` uses the string-compare routine, and its per-part results are narrowed to a byte with `0xFF` = missing. `HANDOFF.md` §15 ("Vehicles resolve part names via this hash table") and the `FUN_004bc880` bookmark comment carry the same withdrawn claim. Left to those documents' owners (concurrent edits in flight).

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked stale quaternion/rotation text superseded (Scope, §4, §9 item 4, §10 item 1 → §4.1/§13.4), struck §11.8 item 4 as retracted (§11.15), inverted-lesson note on §11.9 note 6 and the §11.7 verdict table; marked stale OPEN items resolved in place (§10 item 7, §11.7 verdict row, §11.12/§11.12.3/§11.13/§11.13.3/§11.14.4 item 3(b) → §11.15/§11.16, three §11.15 bullets → §11.5.1/§11.16.1); annotated §11.18.2/§11.18.3/§11.18.4 with the §11.18.5/§11.19.2 refinements, §11.8 item 3's retracted figures, the 260 head-rig count (§3.1), the 14→17 user count (§12), the function-count slip (2,900 vs ~41,785) and the later tracing of inventory row 9; fixed cross-references (§11.9.1 removed, `HANDOFF.md` §11.16.5 → this document's §11.16.5, §2 → §3.1, bare §5/§27.8 → `HANDOFF.md`); reworded decompiler-shaped text (§11.18.1 code block, `param_N` identifiers throughout §11.12–§11.19, `sVar1`, a C `for` loop, pointer-cast expressions).
