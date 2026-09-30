# Saints Row: The Third — `.anim_pc` Animation Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, target selected from `spec-format-inventory.md` §6 — animation data is a prerequisite for anything to actually move, and it directly complements the already-resolved `.rig_pc` skeleton format (`spec-rig-format.md`).
**Scope:** The `.anim_pc` animation file: its header, the root-motion transform it carries, its optional-section mechanism, and the relationship between the **two** distinct registered loaders that both claim this extension.
**Method:** Combined pass, in the pattern that resolved `.rig_pc` — disassembly of both registered constructors and the shared acceptance function they hand into, cross-checked against **bulk statistical analysis of all 4,209 real `.anim_pc` files** shipped in `preload_anim.vpp_pc`. Every structural claim below is stated against that full population, not a handful of samples, and the central claim is validated against two independent control baselines (§4).
**Extraction reliability:** `preload_anim.vpp_pc` stores all 4,209 entries **raw/uncompressed** (every entry carries the `0xFFFFFFFF` raw sentinel), so extraction is fully reliable and none of the container format's mode-(a) limitations apply. This required a correction to this project's own container tooling — see §7.
**Cleanroom compliance:** No decompiled code or original internal identifiers appear below. The magic number and field offsets are load-bearing literal format data and are stated freely. Function addresses are cited as evidence, not reproduced as identifiers. Real animation filenames quoted (e.g. `rope_run.anim_pc`) are ordinary shipped data.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

> **2026-09-11 — the keyframe payload is resolved (§6c).** Stream layout, rotation
> samples (smallest-three quaternions with an adaptive per-axis step) and
> translation samples (`base/64 + delta/4000`) are all decoded. What flags bit
> `0x40` adds to the payload is the main remaining gap (§6c.4).

- The format has a clean four-byte ASCII magic, **`ANIM`**, present in **4,209 / 4,209** real files. **[CONFIRMED — empirical, whole population.]**
- **The two separately-registered types that both claim `.anim_pc` are the same format**, not two formats sharing an extension — they are two *entry points* into one shared animation subsystem. This was the specific open question raised by `spec-format-inventory.md` §3, and it's now answered with evidence rather than assumption (§5).
- Both loaders key on a **`.animx`** runtime name rather than the on-disk `.anim_pc` — an authoring-name/shipped-name split of the same kind already documented for `_cust.xtbl` → `.cvtf_pc` (`spec-geometry-format.md` §5). **[CONFIRMED — disassembly.]**
- The header carries a **root-motion delta transform**: a unit quaternion plus a translation vector, confirmed at **4,209 / 4,209** against **0 / 4,209** and **0 / 12,177** on two independent controls (§4). This is the single most decisive structural confirmation in this project to date.
- The per-keyframe/per-track payload itself is **not** resolved (§6). An honest negative: the obvious uniform layout model is measurably wrong.

## 2. Header layout

All offsets are from the start of the file. Field names are this document's own descriptive labels.

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `+0x00` | 4 | Magic `ANIM` (ASCII, bytes `41 4E 49 4D`) | **[CONFIRMED — empirical, 4209/4209.]** |
| `+0x04` | 1 | **Version. Must be exactly `14` (`0x0E`).** | **[CONFIRMED — both ways: the loader rejects anything not exactly equal to 14, and all 4,209 real files read exactly 14.]** |
| `+0x05` | 1 | **Flags bitfield.** Bit `0x08` is set in every file; bit `0x01` in nearly all. **Bit `0x10` gates the optional section at `+0x40`** (§3). Observed values: `0x09` (2167), `0x0d` (1308), `0x49` (320), `0x4d` (182), `0x1d` (87), `0x19` (60), `0x8d` (51), `0x59` (15), and four rarer values — *counts over the 4,197 files with a full `0x48`-byte header; the 12 shorter files all read `0x09`, so over all 4,209 the `0x09` count is 2,179.* | **[CONFIRMED — disassembly for bit `0x10`'s exact role; CONFIRMED — empirical for the value distribution. OPEN — the meaning of bits `0x01`, `0x04`, `0x08`, `0x40`, `0x80`.]** |
| `+0x06` | 2 | A `u16` count. Range `2`–`1099`, mean ~81, 365 distinct values; scales with file size. Plausibly a frame or key count, **but see §6 — the arithmetic does not support the obvious reading.** | **[CONFIRMED — present and varying; HYPOTHESIS — that it is specifically a frame count.]** |
| `+0x08` | 1 | Small count, range `0`–`20` | **[CONFIRMED present; OPEN — meaning.]** |
| `+0x09` | 1 | Small count, range `0`–`20` | **[CONFIRMED present; OPEN — meaning.]** |
| `+0x0A` | 1 | **Number of animated bones (tracks).** Equals `+0x0B` exactly on prop rigs (`+0x0B` = 2, 3, 7, 11, 13 → same value), is ~46 of 65–77 on humanoid rigs, and varies per clip (45/47/48/49 variants of the same character). `≤ +0x0B` in 99.4%. | **[HIGH CONFIDENCE — inferred from the subset relationship, §6a.]** |
| `+0x0B` | 1 | **Bone count of the target rig.** Matches the character's *own* rig, per name prefix: `plym`/`plyf` → 69 (`cm_body`/`cf_body`), `brute` → 68 (`brute_flamethrower`), `gangf` → 77, `pml1`/`pfl1`/`cml1` → 65 (`jones`), `life`/`ped` → 66; in the set of shipped rig bone counts for 4,163 / 4,209 clips. | **[CONFIRMED — empirical, per-character pairing with `spec-rig-format.md`.]** |
| `+0x0C` | 12 | **Root-motion rotation: quaternion `x`, `y`, `z`** (3 × `f32`) | **[CONFIRMED — empirical, see §4.]** |
| `+0x18` | 4 | **Root-motion rotation: quaternion `w`** (`f32`) | **[CONFIRMED — empirical, see §4.]** |
| `+0x1C` | 12 | **Root-motion translation: `x`, `y`, `z`** (3 × `f32`). **Exactly zero in 1,953 / 4,209 files** (in-place animations), with a further 7 within `1e-4` (largest `9e-5`, e.g. `gml1_fl_bk_getup.anim_pc`) — numerically zero for any practical purpose; median magnitude `0.12`; max `261.6`. *(An earlier version of this table said 1,954 — that figure came from a `< 1e-6` threshold that admitted one file at `1.000e-06`; the clean team's independent count of 1,953 exact is the correct one, and reconciles exactly with a `< 1e-4` count of 1,960.)* | **[CONFIRMED — empirical for the field; HIGH CONFIDENCE — inferred that it is specifically root translation, from the zero-for-in-place-animations pattern and the §4 pairing with the rotation.]** |
| `+0x28` | 4 | **Track-to-bone table offset, file-relative — and the header size.** Zero means the identity mapping (track *i* animates bone *i*); non-zero points at a byte table of `+0x0A` entries, one per track, each a bone index with **255 meaning "unused"**. Its value is **`(flags & 0x10) ? 0x48 : 0x38`** in **394 / 394** clips that have one — i.e. the table sits immediately after the header, whose length depends on whether `+0x40`/`+0x44` are present. **So the keyframe payload begins after the TABLE, not at a fixed `0x48`.** *(Previously recorded here as "observed zero in every sample inspected, OPEN" — true, and the 90% case mistaken for the only case: 3,803 of 4,209 clips use the identity mapping.)* | **[CONFIRMED — disassembly + empirical, see §6b.]** |
| `+0x30` | 4 | **An in-file offset.** `≤ file size` in 4,209 / 4,209. Points near, but not at, end of file; the gap varies widely (2, 10, 12, 122, 230 … bytes). | **[CONFIRMED — empirical, that it is an in-file offset; OPEN — what it points at.]** |
| `+0x34` | 12 | Not characterized | **[OPEN.]** |
| `+0x40` | 4 | **Optional file-relative offset, present only when flags bit `0x10` is set** (§3). Fixed up to an absolute pointer at load; `-1` means null. | **[CONFIRMED — disassembly and empirical, see §3.]** |
| `+0x44` | 4 | Paired high/zero word for `+0x40`, zeroed by the loader during fixup | **[CONFIRMED — disassembly.]** |

**On `+0x0A` / `+0x0B` — corrected reading:** an earlier version of this paragraph read these as "translation-track count ≤ rotation-track count." The rig population (`spec-rig-format.md` v2) settles it differently: `+0x0B` is the target rig's **total** bone count (confirmed per character), and `+0x0A` is the **animated subset** — every bone on simple prop rigs, ~46 of 65–77 on humanoids, varying per clip. The `≤` relation (4,172 / 4,197) is the subset relation. See §6a.

## 3. The optional section at `+0x40`, and a clean confirmation of the flag that gates it

The shared acceptance function reads the flags byte at `+0x05`, and **only if bit `0x10` is set** does it touch `+0x40`: it zeroes `+0x44`, and converts `+0x40` from a file-relative offset into an absolute pointer by adding the buffer's base address, treating `-1` as null. This is the same pointer-fixup-on-load idiom already documented for `.rig_pc` (`spec-rig-format.md` §2) and `.ccmesh_pc` (`spec-geometry-format.md` §4.1). **[CONFIRMED — disassembly.]**

That gating claim is independently confirmed empirically, with the ungated files serving as a built-in control group:

| Population | `+0x40` holds a valid in-file offset |
|---|---|
| Files **with** flags bit `0x10` set (167) | **167 / 167 — 100%** |
| Files **without** it (4,030) | 137 / 4,030 — 3.4% |

**[CONFIRMED — empirical.]** The 3.4% is the by-chance rate for arbitrary bytes happening to fall in `0..size`; the gated population's 100% against it is decisive. Two further internal-consistency checks both pass: **no** file too short to contain `+0x40` has the flag set (0 / 12 of the sub-`0x48`-byte files), and **every** file that does set the flag is at least 284 bytes. The format never declares a field it doesn't have room for.

## 4. The root-motion transform — the decisive confirmation

The floats at `+0x0C`–`+0x18` were suspected to be a quaternion after noticing that `+0x18` clusters on exactly `1.0` (3,547 files), exactly `0.70711` (286 files — i.e. `√2⁄2`, the `w` of a 90° rotation), and `0.0` (219 files — a 180° rotation), with the remainder scattered across `(0, 1)`. Those are precisely the values `cos(θ/2)` takes at natural rotation amounts.

The hypothesis was then tested the way this project requires — against controls, not on its own:

| Test | Result |
|---|---|
| `x²+y²+z²+w²` within `1e-4` of `1.0`, reading `(x,y,z,w)` at `+0x0C` | **4,209 / 4,209 — 100%**, median exactly `1.000000` |
| **Control 1:** same test, offset shifted by 16 bytes (`+0x1C`) | **0 / 4,209** |
| **Control 2:** same test at 12,177 randomly chosen aligned offsets across the same files | **0 / 12,177** |

**[CONFIRMED — empirical. 100% positive against 0% on two independent controls, across the entire shipped population.]** A unit quaternion is a strong, self-validating structure: arbitrary float quadruples essentially never normalize to 1, and both controls demonstrate that directly rather than by assertion.

Paired with the translation vector immediately following it at `+0x1C` — zero in exactly the animations that shouldn't move (in-place taunts, static prop poses) and large in the ones that should (`rope_run.anim_pc` carries a displacement of magnitude ≈ 12) — the two fields together are a **root-motion delta transform**: the net rotation and translation the animation applies to its root over its duration. **[CONFIRMED for the quaternion; HIGH CONFIDENCE — inferred for the root-motion interpretation of the pair.]**

This is directly load-bearing for any reimplementation: root motion is what makes a character actually traverse the world rather than sliding in place.

## 5. Two registered loaders, one format

`spec-format-inventory.md` §3 flagged that types **21 (`Anim File`)** and **41 (`Animation file`)** both register `.anim_pc` with completely different constructor and destructor addresses, and warned against assuming one parser covers both. Tracing both settles it:

- **Both** constructors are thin wrappers. Each builds a lookup name by appending **`.animx`** to the resource name, resolves it, and hands off into the **same** animation subsystem.
- **Type 21** performs an extra precondition check first, transforms the name through an additional resolution step, and hands off **by resolved integer handle**.
- **Type 41** skips that and hands off **by name**.
- Both handoffs land in the same small cluster of functions, and both ultimately reach the **same acceptance function** — the one that performs the version check and the `+0x40` fixup described in §2–§3.

**[CONFIRMED — disassembly.]** So: **one file format, two entry paths**, differing in how the resource is addressed rather than in how its bytes are laid out. The `spec-format-inventory.md` warning was correct to raise the question, and the answer is that a single parser *does* cover both — but that is now a traced fact rather than an assumption.

Supporting detail: loaded animations are tracked in a **handle table** of 36-byte (`0x24`) slots with a separate count global, carrying per-slot flags, a data pointer, and a payload pointer. The acceptance function refuses out-of-range handles and won't re-bind a slot that is already claimed. **[CONFIRMED — disassembly.]**

## 6. The keyframe payload — the obvious model is measurably wrong

> **Superseded in part by §6c (2026-09-11): the payload's stream layout is now
> resolved, and rotation samples are decoded.** This section is kept because its
> *refutations* are what forced the method change that worked — and §6.2's entropy
> measurement correctly predicted the mechanism §6c found. Translation samples
> remain undecoded.

The natural first guess for the bulk of the file is a uniform `frames × tracks × bytes_per_key` array, using `+0x06` as the frame count and `+0x0A`/`+0x0B` as track counts. **Tested against the whole population, that model does not hold.** Computing `(payload_extent) / (f06 × (b0A + b0B))` across all 4,197 full-header files gives a median of **0.55** with a standard deviation of **0.34** and quartiles of 0.35 / 0.55 / 0.78 — i.e. no consistent bytes-per-key value, and a nominal figure of roughly *half a byte* per frame-track, which cannot be a literal uncompressed key.

**[CONFIRMED — empirical, that the uniform-array model is wrong.]** The spread is consistent with **sparse or compressed keyframe storage** — only keyed frames stored rather than one key per frame per track, and/or quantised key data — which is entirely standard for shipped animation data, but means the payload cannot be decoded by arithmetic alone. **[HYPOTHESIS — sparse/compressed storage as the explanation; OPEN — the actual per-track encoding.]**

Reaching it needs the code that *samples* animations at runtime, not the code that loads them — a substantially larger subsystem than the loader traced here, and a separate target to scope deliberately rather than drift into.

### 6.1 A direct attempt on the payload (2026-09-11): partial, with three models refuted

Authorised directly by the user and attempted. **The payload is not resolved. What follows is what was established, positive and negative, so the next attempt starts further along.**

**Positive: sparse, timestamped, quantised keyframe records genuinely exist in the payload.** Scanning for 8-byte records `{s16, s16, s16, u16}` where the final `u16` strictly increases and stays within the clip's `+0x06` frame count finds runs whose contents are unmistakably animation data — three smoothly varying signed components and a rising frame index:

```
0x2752  (2, -24, 96,  2) (1, -26, 95,  6) (2, -27, 94, 10) (3, -29, 92, 16) (4, -31, 91, 19)
0x27A4  (-35, 89, -10, 1) (-34, 89, -15, 2) (-31, 88, -20, 3) (-31, 88, -23, 4)
0x0060  (288, 1, -2, 35) (289, 1, 2, 36) (290, 1, 6, 37) (290, 1, 8, 38) (287, 1, 10, 40)
```

Three quantised components plus a frame index is exactly the shape the format **must** carry, because `spec-rig-format.md` §4.1 established that the rig holds **no** rest rotation — all orientation has to come from here. **[CONFIRMED — empirical, that records of this shape are present; OPEN — that they are the whole encoding, which they are not, see below.]**

**Negative, and the more useful half. Three candidate models were each refuted at population scale:**

| Model | Result |
|---|---|
| Sections of `u16 frame-count` + 8-byte records `{u16 duration, s16 value, u32}` with **durations summing exactly to the count** | **Refuted.** Fit two tiny door clips perfectly. Across 598 clips: sections found in 209, **section count equalled the track count in 0 / 598**, median payload coverage **0.004**, against a **3.9%** by-chance rate for the sum rule at an arbitrary offset. |
| 8-byte `{s16,s16,s16,u16 time}` records, monotonic time, starting at `0x48` and 8-aligned | **Refuted.** Median coverage **0.000**. |
| The same, scanning **every byte alignment** for maximal runs | **Refuted.** Median coverage **0.000**, against **0.044** for the same scan with the monotonicity requirement dropped — i.e. the structure is real but explains almost none of the bytes. |

So the records above are **not** laid out as a uniform array, and the bulk of the payload is something else — per-track headers, a second encoding for other track types, or compression that the raw-record scan cannot see. **A reader cannot be written from this.**

**The first model is the instructive one.** It reproduced two small clips exactly — durations `1,1,1,1,1,2` summing to a declared `7`, values tracing a clean ease-in curve, and `ac_door_opening` / `ac_door_closing` holding the *same values in reverse* — and it is still wrong. **A model that explains a small sample perfectly, including a satisfying cross-file symmetry, is exactly the kind that survives until someone runs it against the population.** Same lesson as the tree "align 32" rule (`HANDOFF.md` §5).

**Routes ruled out for finding the sampler:** the `ANIM` magic appears as an immediate in **no** instruction in the binary, so there is no magic-compare to anchor on. Walking callers of the loader hand-off (`FUN_004bea70` / `FUN_004bec10` / `FUN_004beb00`) reaches only loader-side code and one large asset-enumeration routine (`FUN_004aea30`, a 30 KB-stack manifest/debug walker) — **not** the sampler. The loader hands the buffer to an animation manager and never parses the payload, so the loader chain does not lead to the decoder.

### 6.2 The unexplained bulk is **structured, not compressed** — which decides what to look for

Before assuming the payload needs a decompressor, it is worth measuring. Byte entropy over 116 clips, with the file header and the identified keyframe runs as known-structured reference points:

| Region | Entropy (bits/byte) | Zero bytes | Distinct byte values |
|---|---|---|---|
| File header — known structured | **2.50** | 64% | 22 |
| Identified keyframe runs — known structured | **3.31** | 43% | 30 |
| **Unexplained remainder** | **6.26** | 18% | 190 |

**8.00 is uniform random, i.e. compressed or entropy-coded. 6.26 is not that.** The high-nibble histogram of the remainder settles it further — an entropy coder would be flat at 0.0625 per nibble, and the real distribution is strongly spiky: `0x0_` at **0.226** and `0xC_` at **0.126**, against 0.03–0.07 elsewhere. Byte values are also far from exhausted (190 of 256).

**[CONFIRMED — empirical.]** **So the bulk of the payload is structured data that a record-shaped decode can reach.** The three refuted models in §6.1 failed because they had the wrong record shape or the wrong alignment — **not** because the data is hidden behind a compressor. That is the single most useful thing this pass established for whoever continues: **keep looking structurally; do not go hunting for a decompressor.**

**Where a next attempt should start:** the animation *manager* that receives the buffer, found from the other side — the per-frame update that consumes a clip and a time value and writes bone transforms — rather than from the file loader. That is a runtime-subsystem trace, not a format walk. With §6.2 in hand it is also a *narrower* trace than it looked: the decoder being sought reads structured records, so the target is a field-walking loop, not a bit-reader.

## 6a. How tracks bind to bones — what the population rules out

With `+0x0A` animated bones out of `+0x0B`, each clip must say *which* bones it animates. Three mechanisms were tested against all 4,209 clips, each with a control:

| Mechanism | Result | Control |
|---|---|---|
| **By name hash** — the rig's per-bone hash table (`spec-rig-format.md` §5) uses the engine-wide string hash; do clips contain those `u32`s? | **No.** 2 / 4,209 clips contain any of the 764 shipped bone hashes — chance-level single hits. | 0 / 12,000 random words |
| **By explicit index list** — a strictly ascending run of `+0x0A` byte or `u16` indices all `< +0x0B`, anywhere in the first 16 KiB | **No.** Present in 362 / 4,179 (8.7%), at scattered payload offsets, and 319 of those are the identity `0..n−1` — a structure, but not the binding. | 3 / 5,960 random positions |
| **By presence bitmask** — a header window of ⌈`+0x0B`/8⌉ bytes with popcount `+0x0A` | **No — and this one looked positive until controlled.** 2,005 / 4,053 clips have such a window somewhere in the first 256 bytes, but the per-window chance rate is 0.3% and ~240 windows were scanned per clip, so chance predicts ~53% of clips; observed 49%, no dominant offset. | 14 / 4,500 random windows |

**[CONFIRMED — empirical, all three negatives.]** Conclusion: **tracks are bound to bones by index, not by name** (no hash binding exists).

> **⚠ The rest of this section's conclusion was WRONG and is superseded by §6b.** It read: *"the index mapping is **not** stored as a separate list or mask — it is carried inside the track payload itself … answerable only by the runtime sampling code."* **The mapping is a separate list, exactly where a separate list would be**, and it is readable without decoding the payload at all.
>
> **Why this pass missed it, which is the instructive part:** the "explicit index list" test above searched for a strictly **ascending** run of indices. The real table is a **permutation** — `[0, 1, 3, 4, 2, 6, 7, 5, 9, 10, 8, 14]` — so an ascending-run search can never match it. The test was hunting the right object with the wrong predicate, and its negative result was then written down as *"no such list exists"* rather than *"no **ascending** list exists"*. **A negative result is only as broad as its predicate; record the predicate alongside it.**

## 6b. Track-to-bone mapping — resolved (2026-09-11)

```
table_off = u32(clip + 0x28)
if table_off == 0:
    bone_of_track[i] = i                 // identity
else:
    tbl = clip + table_off               // file-relative
    bone_of_track[i] = tbl[i]            // for i in 0 .. clip[+0x0A)
    // 255 = this track slot is unused
```

**Found in the disassembly**, in the routine that answers *"does this clip animate bone N?"*: it reads `u32(clip + 0x28)` and, when non-zero, scans that byte array for an entry equal to `N`, bounded by `clip[+0x0A]`; when zero it simply tests `N < clip[+0x0A]`. Located by intersecting the clip-registry consumers with functions that read both `+0x0A` and `+0x0B` as bytes. **[CONFIRMED — disassembly.]**

**Validated across all 4,209 clips:**

| Test | Result |
|---|---|
| `+0x28` zero — identity mapping | 3,803 clips |
| `+0x28` non-zero and a valid in-file offset | **394 / 394** |
| every entry a bone index `< +0x0B`, or the `255` sentinel | **394 / 394** |
| all non-sentinel entries distinct | **394 / 394** |
| `+0x28 == ((flags & 0x10) ? 0x48 : 0x38)` | **394 / 394** |
| *control* — same validity rule on the bytes immediately **before** the table | 5 / 340 (**0.0147**) |

**[CONFIRMED — empirical, controlled.]**

**Independently reproduced by the clean team** from their own header data, payload untouched: the same 394 non-zero split, non-zero values **only ever `0x38` or `0x48`** (the two header sizes), and a permutation test on the table bytes passing **355 / 394 against a control of 35 / 394**. *(Their zero count is 3,815 against this document's 3,803 — the difference is 12 clips this team excluded for having a zero track or bone count, not a disagreement.)* It also caught a false comment in their own reader claiming the field was "observed zero everywhere, not read".

**Consequences.** Tracks can be bound to bones without decoding the payload, so the binding question is closed independently of §6. And because `+0x28` is the header size, **the payload starts after the table**, not at a fixed `0x48` — any payload-extent arithmetic assuming `0x48` is slightly wrong for the 394 clips that carry a table.

The bitmask row is the methodology point: a 49% hit rate is meaningless until multiplied out against the per-trial chance rate and the number of trials. Recorded in `HANDOFF.md` §5.

## 6c. The keyframe payload — stream layout resolved (2026-09-11)

**The payload is a sequence of self-delimiting per-track blocks**, walked in track
order. There is no offset table, which is why every search for one failed (§6.1):
each block declares its own lengths, so the reader arrives at track *i+1* only by
having walked track *i*. Recovered from the runtime function that lays out one
track's stream cursors and then advances a walking pointer to the next track.

### 6c.1 Per-track block layout

`f` is the flags byte at `+0x05` (§2). The cursor starts at the payload — after the
header **and** the track table (§6b) — and is advanced once per track:

```
// alignUp2(x) = (x + 1) & ~1 ; alignUp4(x) = (x + 3) & ~3   -- round x UP, do
// NOT add the offset first and then align again (that applies it twice)
if (f & 0x80) == 0:  rotKeys = u8(p);  transKeys = u8(p+1);   p += 2
else:                p = alignUp2(p)
                     rotKeys = u16(p); transKeys = u16(p+2);  p += 4

// rotation control records, 4 bytes each; low 6 bits of byte 3 is a run span
n = 0; acc = 0
while acc < rotKeys:  acc += 1 + (u8(p + n*4 + 3) & 0x3F);  n += 1
p += n * 4
p += rotKeys * 3                      // rotation samples, one byte per axis
                                      // (+ rotKeys more in a second runtime
                                      //  mode that shipped data does not use)

// translation control records
if (f & 0x20) == 0:  p = align2(p+1)
                     m = 0; acc = 0
                     while acc < transKeys:  acc += u16(p + m*8);   m += 1
                     p += m * 8
else:                p = align4(p+3)
                     m = 0; acc = 0
                     while acc < transKeys:  acc += i32(p + m*16);  m += 1
                     p += m * 16
p += transKeys * 3                    // translation samples
p += transKeys                        // one more byte per translation key
```

**Evidence.** Exact-size replay over every clip in `preload_anim.vpp_pc`:

| Test | Result |
|---|---|
| Walk exactly `+0x0A` tracks without overrunning the file | **4,086 / 4,209 (97.1%)** |
| *control* — identical walk started 7 bytes late | **457 / 4,209 (10.9%)** |
| Cursor lands **exactly** on the offset declared at header `+0x30`, **clips with flags bit `0x40` clear** | **3,687 / 3,691 (99.89%)** |
| Same, **clips with flags bit `0x40` set** | **0 / 518** |
| Next most common landing residual, whole population | **3 clips** |

> **Corrected in place (§6c.4).** This rate was first published as **3,687 / 4,086
> (90.2%)** over all successful walks. That denominator mixed two populations:
> flags bit `0x40` separates them almost perfectly, and stratifying raises the
> clean rate to **99.89%** while isolating the entire residual. The original
> figure was not wrong, it was **diluted** — the same error shape as the morph
> control diluted by single-element records (§13) and the vehicle equality test
> diluted by a wrong denominator (§26.9).

The endpoint test is the strong one: `+0x30` is an oracle **the file itself
declares**, not a boundary chosen to make the walk look right, and the residual
distribution is a spike at zero against a floor of three. **[CONFIRMED —
disassembly + empirical.]** Harness `anim_layout.py`.

The two shape bits are real and both ship: `0x80` (wide counts) in 53 clips,
`0x20` (wide translation records) in none of this archive, `0x10` (long header)
in 167. **The `0x20` branch is therefore disassembly-only** — no sample exercises
it. **[CONFIRMED — disassembly; unexercised in shipped data.]**

### 6c.2 Rotation samples are smallest-three quaternions

**THE ASSEMBLY, stated first because everything else in this section is commentary on
it.** Per key, per axis `j ∈ {0,1,2}`:

```
base_j    = signext6( control_byte_j & 0x3F )            // signed, -32 .. 31
mult_j    = {1, 2, 4, 16}[ control_byte_j >> 6 ]         // per-axis step
delta_j   = the j-th of three 6-bit signed fields packed across the key's 3 sample bytes

component_j = ( base_j * 64 + mult_j * delta_j ) * 4 * SCALE
              //         ^^ 64            ^^ 4  -- BOTH factors are required
SCALE       = 8.6327287135645750e-05     (bits 3F16A15380000000)

fourth    = sqrt( 1 - (c0*c0 + c1*c1 + c2*c2) )          // clamped at 0
omitted   = control_byte_3 >> 6                          // which lane `fourth` fills
```

> **Why this is restated at the top (2026-09-11).** This expression was already in this
> section — **85 lines further down**, inside the numeric-replay subsection. An
> independent implementation read the decode description here, the bit allocation and
> the achievable range, and never reached it; it reconstructed
> `(base + mult*delta) * SCALE`, dropping **both** the `*64` on the base and the overall
> `*4`, and capped every component at 0.181 as a result. **A formula present but far
> from the description it belongs to is functionally absent.** Stating the achievable
> extreme is not a substitute: they could see the factors 64 and 4 inside
> `(16×32 + 32×64) × 4 × SCALE` without being able to place them.


The rotation decoder writes **four consecutive floats** and, when the stream is
absent, writes the constant `(0, 0, 0, 1)` — an identity quaternion, read
directly out of the binary. Three components are stored and the fourth is
reconstructed:

- **Which component is omitted** is the top 2 bits of control-record byte 3. They
  select one of four permutation rows, read from the binary as
  `[3,0,1,2]`, `[0,3,1,2]`, `[0,1,3,2]`, `[0,1,2,3]` — i.e. "insert the
  reconstructed component at position *k*". This is the canonical
  smallest-three arrangement.
- **The fourth component** is `sqrt(K - (x² + y² + z²))` with `K` read from the
  binary as exactly **1.0**, clamped so the argument cannot go negative — unit
  quaternion reconstruction.
- **Per-axis step size is adaptive.** The top 2 bits of each of **CONTROL RECORD**
  bytes 0, 1 and 2 form a 6-bit index into a 64-entry table, which yields a
  **per-axis multiplier drawn from {1, 2, 4, 16}**.

  > **⚠ Corrected 2026-09-11.** This sentence previously said *"sample bytes"*, which
  > is wrong and contradicted this section's own pseudocode (`control_byte_j >> 6`)
  > and the disassembly, which reads the step index from the 4-byte control records.
  > **The wrong reading is not merely mislabelled, it is unimplementable**: the delta
  > packing below already consumes the top bits of sample byte 0, so sourcing the step
  > index from the sample bytes double-allocates them. An implementer resolving that
  > conflict by reserving those bits for the step index gets a **narrower delta and
  > therefore a lower achievable range** — the likely cause of the clean team's
  > independent implementation topping out near 0.181 per component where this
  > document's formula reaches 0.884. **[The defect was mine; found by their
  > refutation failing to replicate.]** The table was dumped from the binary and
  is exactly the regular 4×4×4 product of that set. **This is the mechanism
  behind §6.2's entropy measurement**: an adaptive per-axis step is structured,
  not compressed, and it is why fixed-width record models were refuted.
**Bit allocation, stated exhaustively so no reader has to resolve an ambiguity.** Per
key there are 4 control-record bytes (shared by a run of keys) and 3 sample bytes (one
key's own data):

| Source | Bits | Meaning |
|---|---|---|
| control byte 0 | low 6 | signed base, axis 0 |
| control byte 0 | top 2 | step index, axis 0 |
| control byte 1 | low 6 | signed base, axis 1 |
| control byte 1 | top 2 | step index, axis 1 |
| control byte 2 | low 6 | signed base, axis 2 |
| control byte 2 | top 2 | step index, axis 2 |
| control byte 3 | low 6 | run span (keys covered, minus one) |
| control byte 3 | top 2 | which quaternion component is omitted |
| sample bytes 0–2 | **all 24 bits** | three 6-bit signed deltas (18 bits) + 6 unused |

The three step indices combine as `idx = a*16 + b*4 + c` into the 64-entry table; the
sample bytes are consumed **entirely** by the delta fields, so nothing else may be read
from them. Delta unpacking, explicitly:

```
delta_0 = signext6( s0 >> 2 )
delta_1 = signext6( ((s0 & 0x03) << 4) | (s1 >> 4) )
delta_2 = signext6( ((s1 & 0x0F) << 2) | (s2 >> 6) )
                                              // s2 low 6 bits are not read
```

**Achievable range, which any candidate reading must be checked against before it is
tested for fit** (the clean team's rule, adopted): with `base ∈ [-32, 31]`,
`mult ∈ {1,2,4,16}` and `delta ∈ [-32, 31]`, the extreme is
`(16×32 + 32×64) × 4 × SCALE = 0.884` per component. Observed maximum over 2,963,687
real samples is **0.8785** — inside the ceiling, and close enough to it that the field
widths are being fully exercised. Decoded rotation angle over the same samples:
**median 31.16°, p90 80.40°, p99 105.10°, max 180.00°**, with 43.2% of keys beyond 36.5°.
**A reading that cannot exceed ~36° is refuted by this measurement alone**, since real
character animation and this data both go far past it.

- **Scale.** Each assembled component is multiplied by a double constant read
  from the binary as **8.6327287135645750e-05** (bits `3F16A15380000000`;
  reciprocal ≈ 11,583.8). Its closed form is **not** derived here — it is close
  to, but not equal to, several obvious candidates, so it is recorded as the
  measured constant rather than guessed. **[OPEN — closed form.]**


**Numeric replay (2026-09-11) — PARTIAL, and deliberately not promoted to
CONFIRMED.** The per-sample arithmetic was replayed against real file bytes for the
first time. The shipped decode branch assembles each component as

```
base_j  = signext6(control_byte_j & 0x3F)          // j = 0,1,2
mult_j  = {1,2,4,16}[control_byte_j >> 6]
delta   = three 6-bit signed fields packed across the 3 sample bytes of the key
axis_j  = (mult_j * delta_j + base_j * 64) * 4 * SCALE
```

Note a free consistency check falling out of this: the base term alone spans
±8192 × SCALE = **±0.7072**, which is ±1/√2 — exactly the range a smallest-three
component occupies. The scale constant and the field widths agree with each other
independently of any test below.

**First attempt was near-vacuous and is reported as such.** The intended oracle was
the clamp: a smallest-three triple must satisfy `x²+y²+z² ≤ 1`, so breaches should be
absent. Over 2,963,687 samples the real reading breaches at **0.001%** — but the
byte-shifted controls breach at only **0.002–0.058%**. Most stored components are
small, so the constraint could rarely have failed, and the test barely
discriminates. **Quoting the 0.001% alone would have been the half-float screen
mistake again** (§6.1's `abs(u) < 64`, which denormals passed automatically).

Two sharper tests, the second being the informative one:

| Test | real | samples +1 | samples +2 | records +2 |
|---|---|---|---|---|
| clamp breach, restricted to samples whose **base term is already large** (n = 129,261) | **0.03%** | 0.12% | 0.28% | 1.89% |
| **intra-run jitter**, median — consecutive keys sharing a base must move smoothly | **0.0193** | 0.0497 | 0.0635 | 0.0442 |
| same, p90 | **0.0663** | 0.2155 | 0.1989 | 0.1436 |

The real reading is **2.6–3.3× smoother** than every control on all three
statistics, and breaches 4–63× less often on the restricted subset.

**Why this is still not [CONFIRMED — empirical].** The separations are single- to
low-double-digit multiples, where this project's structural confirmations run to
orders of magnitude (1,937/1,937; 4,086/4,209 against 10.9%). And **a byte-shifted
control is intrinsically weak here**: a shifted read still lands on real packed
6-bit delta fields whose values are small either way, so the control is not a
random baseline. What these tests establish is that the reading is **bounded and
smooth** — properties several nearby readings would also have. They do not uniquely
pin the bit packing. Status therefore moved from *"inferred, never replayed"* to
**[HIGH CONFIDENCE — inferred; replay consistent, controls weak]**.

> **UPGRADED to [CONFIRMED — disassembly + empirical] on 2026-09-11.** An independent
> implementation, after correcting its own assembly (it had dropped the `*64` and the
> `*4`), reproduces this decode numerically: **max angle 180.00°, median 34.40°, p90
> 83.69°** against this project's independent **180.00° / 31.16° / 80.40°**. Two
> independently-written decoders now agree on real data, which is the confirmation the
> weak byte-shift controls could not supply. The residual median difference (34.40 vs
> 31.16) is unexplained and small; it is recorded rather than smoothed over.

**What would settle it:** compare a decoded rotation against an orientation known
independently of this decode — the rig's bind pose, or the same named pose appearing
in two unrelated clips. That requires a rig-side reference this team does not hold.

Harness `anim_rot.py`, which keeps the vacuous first test **in place** rather than
deleting it, so the weak version cannot be rediscovered and quoted.

**[CONFIRMED — disassembly]** for the arrangement, the tables and the constants,
all of which were read out of the executable image rather than inferred. The
**numeric assembly of a component from its base, multiplier and delta is
[HIGH CONFIDENCE — inferred]**: it was read from one decompiled branch and has
not been replayed against file bytes, so the field widths within a sample byte
are not yet independently confirmed.

### 6c.3 What is still open in the payload

1. ~~**Translation sample decode.**~~ — **RESOLVED (§6c.5).** The control record
   carries the base position and the 3 bytes per key are signed per-axis deltas:
   `axis = base/64 + delta/4000`. Confirmed empirically over 86,051 records.
2. ~~**The 123 clips that do not walk and the 399 that land off `+0x30`**~~ —
   **characterised in §6c.4/§9.1: flags bit `0x40` is the discriminator, but the
   bit-carrying side is itself three sub-populations, not one** (corrected
   2026-09-12 — this item previously said "they are one population," which is
   superseded; see §9.1 for the split and §6c.4's inline correction). What the
   third sub-population (55 clips) carries beyond the declared tracks is
   characterised, with several models refuted, in **§9.1**; still **[OPEN]**
   overall.
3. ~~**The second rotation mode.**~~ — **RESOLVED (§9.1's disassembly side
   trip).** A runtime flag adds one more byte per rotation key; shipped data
   does not use it (replaying with it on drops the walk to 6.6%). **What
   selects it is flags bit `0x01`** — read directly by the per-clip bind
   function into the same decoder-state byte the stream-setup function tests:
   bit `0x01` **clear** takes the extra-byte branch, bit `0x01` **set** (true
   of nearly every shipped file, per §2's value table — hence unexercised)
   skips it. **[CONFIRMED — disassembly.]**
4. **The rotation control record's remaining bits** — byte 3 is fully accounted
   for (2 bits permutation + 6 bits span), bytes 0–2 give 2 bits each to the
   step-size index, and the remaining 6 bits per byte are the sample bases.
   Consistent, but only the span field is empirically confirmed. **[HIGH
   CONFIDENCE — inferred.]**

### 6c.4 The residual has a single DISCRIMINATOR, flags bit `0x40` — but not a single MECHANISM (superseded in part, see §9)

> **Heading and claim corrected 2026-09-12 (see §9.1).** This subsection originally
> read *"The residual is a single population: flags bit `0x40`"* and the paragraph
> below said **"They are not two mechanisms. They are one."** That is now known to
> be wrong: §9 characterises **three** sub-populations within the bit-`0x40` set
> (334 continuation-explained, 55 walk-cleanly-but-never-land, ~129 outright
> failures), and the 55 do not share the CONT population's mechanism — that is the
> entire subject of §9.1. **What survives, and is why the heading is corrected
> rather than deleted:** flags bit `0x40` genuinely is the discriminator that
> separates this residual from the clean population (the table below is
> unchanged and still accurate as a two-way split) — only the claim that the
> bit-carrying side is internally *uniform* is retracted. Kept visible rather than
> silently rewritten, per this project's standing rule on corrections.

The first publication of §6c.1 left two unexamined groups — 399 clips that walk but
miss the declared endpoint, and 123 that fail the walk. Flags bit `0x40` is the
discriminator between this residual and the clean population — **but the
bit-carrying side is not internally one mechanism; §9 splits it further**:

| Population | lands on `+0x30` | walks but misses | fails the walk |
|---|---|---|---|
| flags bit `0x40` **clear** (3,691 clips) | **3,687** | 2 | 2 |
| flags bit `0x40` **set** (518 clips) | **0** | 397 | 121 |

A clip without the bit lands on the declared endpoint **99.89%** of the time; a
clip with it **never does** — 0 out of 518. No other header field separates the
populations remotely as well (track count, bone count, `+0x06`, `+0x08`, `+0x09`
and the mixed-case name all overlap heavily between groups).

**What the bit appears to mean.** Two rates, because they answer different
questions and neither is meaningful without its exclusions stated:

| Question | Rate |
|---|---|
| Of the bit-carrying clips that **walk short**, how many recover — land exactly on `+0x30` when the same per-track walk is continued? | **334 / 397 (84.1%)** |
| Of **all** bit-carrying clips, how many are thereby explained? | **334 / 518 (64.5%)** |

The second is lower because the **121 clips that fail the walk outright are
excluded from the first** — they never reach the point where continuing is
possible. Quoting 84.1% alone would hide them.

So the block layout in §6c.1 is very likely still correct for the clips that walk,
and what is wrong is only how many blocks to walk.

> **Corrected 2026-09-11 (clean team's catch).** First published as **335 / 399
> (84%)**. `399` was the *combined* walk-but-miss count — 397 carrying the bit plus
> 2 not carrying it — so it mixed in the very clips this section had just argued
> are a different mechanism. **The same denominator-dilution error as §6c.1's,
> recurring one paragraph inside the correction for it**, because 399 was the
> number already in hand. Note the numerator moved too: **re-measuring** on the
> restricted population gives 334, not 335, since one of those two non-bit clips
> was inside the original count. **Re-dividing 335 by 397 would have carried the
> error forward** — a corrected denominator needs a re-measurement, not arithmetic. But the extra count is
**not** any header field tested — it is 39 blocks for 310 of them and varies
otherwise (74, 113, 90, …), and matches neither `+0x0B`, nor `+0x06`, nor twice
the track count (≤ 0.3% each). **So this is not yet "the track count is wrong";
it is "there is more payload after the tracks, and walking it as though it were
more tracks usually lands right."** Those are different claims and only the first
is measured. **[OPEN — what flags bit `0x40` adds to the payload.]**

**Why this matters beyond the residual:** the clips carrying the bit are
disproportionately vehicle-occupant and layered-action animations by name
(`auto_drv_*`, `auto_ridel_*`), which is consistent with an additive or secondary
channel set — but that is a **[HYPOTHESIS — unconfirmed]** from filenames, which
this project does not treat as evidence on its own.

### 6c.5 Translation samples — decoded

**The translation control record is not merely a run span: it carries the base
position.** That is why §6c.1 could measure its stride and leading field without
knowing what the rest of it was. Layouts, gated by flags bit `0x20`:

```
bit 0x20 clear:  8 bytes  { u16 span, i16 x, i16 y, i16 z }
bit 0x20 set  : 16 bytes  { i32 span, i32 x, i32 y, i32 z }
```

Each key's position is the record's base plus a **signed byte delta** taken from
the 3-bytes-per-key sample stream, one byte per axis:

```
axis = base / 64  +  delta / 4000
```

Both scales are exact and were read from the image as **float32 broadcast vectors**
(four identical lanes, i.e. SIMD constants): `0.015625` = **1/64** for the base and
`0.00025` = **1/4000** for the delta. A base in 1/64 units is plain 6-bit
fixed point; the per-key delta is a much finer refinement.

**Evidence.** Bone-local translation is physically bounded — a bone sits
centimetres to about a metre from its parent — so a correct reading must produce
tightly clustered small magnitudes and a wrong one must not. Decoding every
translation control record in the clean population (clips without flags bit `0x40`
that land on the `+0x30` oracle), **86,051 records**:

| Reading | median | p90 | within 1.0 | within 4.0 |
|---|---|---|---|---|
| **real alignment** | **0.203** | **2.02** | **79.5%** | **97.0%** |
| control, +1 byte | 76.0 | 356.0 | 1.1% | 2.9% |
| control, +2 bytes | 0.438 | 88.2 | 65.8% | 82.5% |
| control, +3 bytes | 56.0 | 364.0 | 0.6% | 4.9% |

**The `+2` control deliberately reads worse than it looks.** Shifting by two bytes
still lands on `i16` boundaries — it reads *real* fields, just the wrong ones — so
it is not a control for alignment at all but for **field assignment**, and it is
reported here rather than dropped because quoting only the ±1/±3 controls would
overstate the separation. It still separates clearly on the tail (p90 **2.02**
against **88.2**), which is the discriminating statistic: the median is
insensitive because many axes are genuinely near zero under any nearby reading.

**[CONFIRMED — disassembly + empirical.]** Harness `anim_trans.py`.

### 6c.6 What remains open after the translation decode

1. ~~**Whether the per-key delta is measured from the record's base or
   accumulates across keys.**~~ — **RESOLVED (§9.3), attributed to the partner
   team. [CONFIRMED — empirical.]** From-base, not accumulating. Method:
   cross-run continuity at control-record boundaries, scored against the gap to
   an unrelated run in the same track — a dose-response across run length
   (identical by construction at length 1; from-base separates from accumulate
   at length 2–4 and again more sharply at length 5+; on long runs, n=13,017,
   from-base separates from its own control by 6.76× where accumulate manages
   1.83×). Full figures and populations in §9.3.
2. **The fourth output lane.** The decoder writes four floats, and the first is
   assembled from a different field than the other three. Read as *(x, y, z)* plus
   a fourth value, the three that this project tested behave correctly; what the
   fourth carries is unread. **[OPEN.]**
3. **Interpolation between records.** The decoder loads 16 bytes where a record is
   8, which is consistent with fetching two consecutive records to blend between
   them, but the SSE shuffle sequence was not traced far enough to assert it.
   **[HYPOTHESIS — unconfirmed.]** **Answered as an OPEN null in §9.4, by the
   partner team's data:** the same cross-run continuity oracle that closed item 1
   above, stratified by control-record span, does **not** discriminate cleanly —
   short spans (2–4) favour blend, long spans (5+) favour no-blend, with a real
   crossover near span 4 (confirmed non-noise by a model-divergence check and a
   base-difference control). **Pooled and unstratified this same data reads
   9.89× vs 7.34× favouring blend — a clean-looking confirmation of the wrong
   answer**, kept here as the sharpest instance of the mixed-population-dilution
   trap this document has recorded. This pass's own disassembly (§9.4) separately
   found two distinct 16-byte loads for two different reasons (a batched
   sample-byte unpack, and a single-record fast path that overreads its own
   record without observably touching a second record's fields) — narrowing
   *where* a resolution lives in code without settling the question. **Status:
   [OPEN]** — recorded as a real null result, not forced to a verdict.
4. **The rotation path's replay is PARTIAL** (§6c.2, updated 2026-09-11). It now
   has one — bounded and smooth, 2.6–3.3× better than controls — but with weak
   controls, so it stays **[HIGH CONFIDENCE — inferred; replay consistent]** rather
   than confirmed. The asymmetry with translation is narrowed, not closed, and is
   deliberate to record.

   **Why it matters more than the tier difference suggests** (clean team's framing):
   a reader built on this today would place bones from confirmed arithmetic and
   *orient* them from unverified arithmetic. **A translation error is visually
   obvious — limbs detach. A rotation error looks like a slightly wrong animation**,
   which is the silently-plausible failure in its purest form: it renders, it moves,
   it reviews as "close enough", and nothing forces a second look. Treat the
   rotation arithmetic as unverified until the clamp-breach test in `HANDOFF.md`
   §27.1(b) has been run — that test needs no new disassembly and the oracle is
   already in the data (`x² + y² + z² ≤ 1` must hold almost everywhere).

## 7. A correction to this project's container tooling, found by this pass

`preload_anim.vpp_pc` sets the container's **shared-stream flag** (`0x2`) but stores **every one of its 4,209 entries uncompressed**, each carrying the per-entry raw sentinel `0xFFFFFFFF`. This project's extraction tooling checked the container-level shared-stream flag *first* and attempted a single zlib inflate over the whole payload, which fails outright here (`incorrect header check`).

**The per-entry raw sentinel must take precedence over the container-level flag.** With that ordering corrected, all 4,209 entries extract cleanly. **[CONFIRMED — empirical.]** A survey across five archives shows the two signals genuinely are independent: `characters.vpp_pc`, `customize_item.vpp_pc` and `sr3_city_0.vpp_pc` have flag `0x0` with all entries raw; `preload_anim.vpp_pc` has flag `0x2` with all entries raw; `misc_tables.vpp_pc` has flag `0x4801` with *no* raw entries (it is the mode-(a) archive whose non-first-entry decoding remains the project's long-standing parked limitation). This refines, but does not contradict, `spec-vpp-container.md` — worth folding into that document's compression-mode section.

## 8. Open Items

1. **The keyframe/track payload encoding** (§6) — the single most valuable remaining gap, and the one thing standing between this spec and a usable animation reimplementation. Needs the runtime sampling code, not the loader.
2. **Flags bits other than `0x10`** (§2) — `0x01`, `0x04`, `0x08` occur in real files in stable combinations and are not decoded. **`0x40` is now partly characterised (§6c.4): it perfectly separates the clips whose payload walk ends on the declared endpoint (0 / 518 with the bit, 3,687 / 3,691 without), so it adds payload beyond the `+0x0A` tracks — what exactly is OPEN.** **→ RESOLVED 2026-09-12, §10: it is a per-track layer-override table, parsed fresh from the clip's own bytes on every call to the runtime layer-blend sampler `FUN_004c21a0`; not more keyframe payload, and no offset/size field exists for it because nothing needs one -- its own bytes self-delimit it.** *(`0x80` and `0x20` are now decoded: they widen the payload's per-track counts and translation records respectively — §6c.1.)*
3. ~~**What `+0x30` points at**~~ — **RESOLVED (§6c.1): it is the end of the keyframe payload.** The per-track walk lands on it exactly in 3,687 of 4,086 clips. *(Previously: "a valid in-file offset in every file, target unidentified.")*
4. **`+0x06`, `+0x08`, `+0x09`** — confirmed present and varying; `+0x06`'s frame-count reading is actively undermined by §6. *(`+0x0A`/`+0x0B` are now resolved — see §2 and §6a.)*
5. **`+0x28` and `+0x34`** — zero or unexamined in every sample checked.
6. **The 12 sub-`0x48`-byte files** — all static prop poses (doors, barriers, crates), all flags `0x09`, all with an identity-ish transform. They parse consistently but represent a minimal-case shape not separately characterized.
7. **Cross-archive validation** — all 4,209 samples come from a single archive (`preload_anim.vpp_pc`). Other archives ship `.anim_pc` files too; none were checked. Consistent with this project's standing rule, this is flagged rather than generalized.

## 9. Flags bit `0x40`'s third sub-population, an empty-block audit, and two closed/OPEN items from the translation decode

> **Scope.** This section answers the four questions the partner team's `0x40`
> split (§6c.4, corrected above) handed to this pass: characterise the third
> sub-population within the bit-carrying clips that neither lands directly nor
> is explained by continuation (§9.1); audit the whole corpus's walks for
> empty-block triviality before trusting any of the walk statistics above or
> below (§9.2); close §6c.6 item 1 (§9.3); and re-examine the 16-byte-load
> question with the sample-bytes/control-record-bytes distinction stated
> precisely (§9.4). **The target population for §9.1 moved twice mid-pass and
> ended up BOUNDED, not settled** — from a task-brief figure of 55, to a
> reported "authoritative" 69, to a bounded range of `[55, 69]` once the cause
> of that correction turned out to be genuinely ambiguous — and §9.1 states
> plainly, throughout, which walker or population produced each figure. **A
> second live retraction happened during this same pass**: the continuation
> search that grounded the `CONT` = 334 figure, and the empty-block audit of its
> extra region, were both shown — by a control this project's own rules
> require but had not yet run — to be far weaker evidence than first reported;
> §9.1 and §9.2 state exactly what survives and what does not. New harnesses
> `anim_flag40{f,g,h,i,j,k,l,m,n}.py` (this pass; `anim_flag40{,b,c,d,e}.py`
> predate it and are reused, not redone). New Ghidra scripts
> `AnimDumpStreamSetup.java`, `AnimFindStreamSetupCaller.java`,
> `AnimFindLayerSetup.java`.

### 9.1 The third population: BOUNDED between 55/135 and 69/121, not settled — plus a live retraction of the continuation search's own evidential weight

> **Target size is bounded, not corrected — a second mid-pass revision, and it
> demotes the first one.** This subsection was drafted twice already: the task
> brief's split (518 = 328 continuation + 135 outright failures + 55 never-land),
> then a "corrected, authoritative" 328 / 121 / 69 after the partner team re-ran
> their census against the actual shipping reader. **That second version is
> itself now qualified.** The 14-clip divergence between the partner's own probe
> and the shipping reader has a byte-verified, single-cause explanation — a
> translation control record whose `span` field reads exactly `0`; the probe
> rejects it, the shipping reader advances past it without complaint — but
> **cause is not correctness**. `span == 0` occurs in **0 of the 3,687 clips
> that land cleanly** and in exactly **14 of the 399 clips that walk but miss**
> (all 25 such records in the whole corpus sit inside those 14 clips, confirmed
> below by this pass's own independent count). Two readings fit that distribution
> equally well: either it is **legitimate padding** the reader correctly skips
> (split **121 / 69**), or it is **a misparse symptom** — a record declaring zero
> covered keys being exactly what a cursor that has drifted into non-record bytes
> would produce (split **135 / 55**, the probe was more correct). **The shipping
> reader's own validation — the 3,687 clips it reads cleanly — never once
> exercises this construct, so that validation confers no authority over this
> specific dispute.** Nothing here decides it either; this pass's own oracle can
> only *corroborate* the lenient reading (a construct that completes cleanly and,
> in several cases, lands on the file's own declared endpoint) and cannot refute
> the alternative, for the identical reason the shipping reader's own confirmation
> cannot: a miss, or a clean parse, can each have more than one cause. **So: the
> third population's true size is bounded in `[55, 69]`, and this document does
> not adopt either endpoint as settled.** Everything below states, at each figure,
> which walker or population produced it, per the rule that a figure must name its
> implementation — and, new this round, that naming the implementation is not the
> same as establishing its authority over the specific behaviour being measured.

**Re-derivation, predicate stated, before anything else.** Re-run here from the
file bytes directly, independently of the partner team's C++ implementation,
using this project's own walker (`anim_layout.walk_track`) and this predicate per
clip: walk exactly `+0x0A` declared tracks; if the cursor equals `u32(+0x30)`,
`HIT`; if it exceeds it, `LONG`; if it is short, keep walking whole tracks (cap
4,096) and call it `CONT` if some count lands exactly on `u32(+0x30)`,
`SHORT_OV` if continuation completes but steps over the target without landing,
`SHORT_DS` if continuation desyncs (raises) before reaching or passing it; if the
*original* `+0x0A`-track walk itself does not complete, `FAIL`. Harness
`anim_flag40.py`, which gates on the population sizes (raises unless 518
bit-carrying / 3,691 bit-clear, exactly as published) rather than printing and
continuing.

| Stratum | bit `0x40` set | bit `0x40` clear |
|---|---|---|
| HIT | 0 | 3,687 |
| CONT | 334 | 1 |
| SHORT_DS | 52 | 0 |
| SHORT_OV | 3 | 0 |
| LONG | 8 | 1 |
| FAIL | 121 | 2 |

334 + 52 + 3 + 8 + 121 = **518**, exactly. **[CONFIRMED — empirical, from this
pass's own reimplementation — not the shipping reader; see below.]**

> **RETRACTED as evidence, live during this pass: "ambiguity exactly 1.00" does
> not show the `CONT` = 334 continuation-landing count locates real structure.**
> The partner team ran the control this project's own rules require and had not
> yet run: searching **512** candidate additional-block-counts, from the real
> end-of-declared-walk cursor versus a cursor shifted **+7 bytes** (this
> project's standard displacement), over their 518 bit-carrying clips —
> **328 / 518 (63.32%)** find a landing count at the real start, **333 / 518
> (64.29%)** find one from the *wrong* start, both at mean ambiguity **1.00**.
> The wrong start does marginally *better*. **A search over a 512-wide space is
> guaranteed to find something almost regardless of where it begins** — the
> same shape as this subsection's own exhaustive rk/tk search below, generalised
> from 55 clips to the whole bit-carrying population. **The `334` count itself is
> not retracted — 334 of 397 walk-shorts really do reach `u32(+0x30)` under
> continuation, and that arithmetic identity still holds. What is retracted is
> the *inference* from that count to "this locates real extra track blocks."**
>
> **Re-run on this project's own, correctly-matched denominator — 397, the
> bit-carrying "walks the declared tracks, does not land directly" bucket, not
> the partner's 518 — because their figure does not transfer across a different
> population and must not be compared to it directly.** Same design (512
> candidate additional block counts, real start vs. shifted), a second
> displacement added: **real start 334 / 397 (84.13%)**, mean ambiguity 1.00;
> **shifted +7: 37 / 397 (9.32%)**; **shifted −5: 54 / 397 (13.60%)** — both far
> below the real rate, a roughly **6–9×** separation rather than the partner's
> near-identity. **This is a genuinely different result from theirs, not a
> disagreement about the same thing** — ~~reconciled by which clips sit in each
> denominator: their 518 includes the 121 outright failures and the (bounded)
> 55–69 that never land under *any* start, real or shifted, which dilutes the
> minority that genuinely can land into a wash across the whole population;
> restricted to the 397 clips where landing is not already precluded by
> construction, the real alignment clearly dominates.~~
>
> **⚠ THAT DILUTION RECONCILIATION IS REFUTED (2026-09-12) — a feasibility bound kills it outright.** Our 397 is a strict subset of their 518 (their extra 121 being the outright walk failures). Under `+7`, ours finds **37 of 397**. If both implementations agreed on the 397 shared clips, their total could be **at most 37 + 121 = 158**, even granting that every one of the extra 121 hit spuriously. **They report 333. 333 > 158.** No population composition reconciles that — dilution is arithmetically impossible as an explanation. **At least one of the two shift-control implementations is not measuring what its description says.** The likely divergence is one of two one-line choices: what exactly is shifted (our `+7` displaces the payload start; theirs may displace a different anchor), or what counts as a hit (ours requires landing exactly on `u32(+0x30)`; theirs may count a clip merely for completing the walk without landing on that target — which would also explain their observed near-identity, since "completes" is plausibly insensitive to start while "lands on a specific offset" is not). **Next step: diff the two control implementations on those two points before trusting either verdict.** Until that is done, the status of this measurement is **UNKNOWN, not confirmed and not refuted** — the 334/397 real-vs-shifted separation stands as a measurement, but no inference from it (narrower or otherwise) should be treated as settled. **[STATUS: RESOLVED 2026-09-12, `CONFIRMED — empirical` — Team B's shift control had the wrong anchor (payload start, corrupting the declared-track walk itself, rather than `end_of_declared_walk`); rebuilt correctly it converges with this measurement within 4 clips, and a desync-rate diagnostic confirms the mechanism (2.04% completion under the correct anchor's shift vs 5.92–6.27% at the real start / wrong-anchor start). Full resolution in `HANDOFF.md` §27.2 item 1. **This settles that the continuation evidence for extra payload is real; it does not settle what flag `0x40` means at runtime, which stays OPEN.**]** What it shows: landing at the *correct*, byte-precise
> alignment is far more likely than landing at a nearby wrong one, for this
> specific population — consistent with *some* real, byte-precise structure
> being there for a walk-shaped parser to lock onto. **It does not show that
> structure is "more per-track blocks in the same format"** — that stronger
> claim is separately unsupported by the disassembly below, which found nothing
> in the per-track walker that ever tests bit `0x40`. Harness `anim_flag40l.py`
> (new).

**This walker's own span==`0` count, independently reproduced against the file
bytes — exact agreement with the partner's figures.** Walking every clip's
narrow (8-byte) translation control-record stream and counting records whose
leading `u16 span` field reads `0`: **0 occurrences in the 3,687 landing
clips**; among the 399 "walks but misses" clips (`CONT` + `SHORT_DS` + `SHORT_OV`
+ `LONG`, bit-carrying, plus the 2 bit-clear members of the same stratum),
exactly **14 clips** contain one, totalling **25 records** — both numbers match
the partner's figures exactly. **[CONFIRMED — empirical, independently
reproduced.]** Broken down by this walker's own strata, all 14 fall in **`CONT`
(6 clips) or `LONG` (8 clips) — none in `SHORT_DS` or `SHORT_OV`, i.e. none in
"the 55."** Harness `anim_flag40n.py` (new).

**Which side does this project's own walker (`anim_layout.walk_track`, used
throughout §6c and §9) fall on? The lenient side — same as the shipping reader,
not the probe.** Reading its own code: on a zero-span record the accumulator
`acc` simply does not advance (`acc += span` with `span == 0`), and the loop
continues to the next 8-byte record exactly as it would after a real one — there
is no check that rejects it. This is the *shipping-reader* behaviour, not the
probe's. Consequence, checked directly rather than assumed: **all 14 disputed
clips complete their declared-track walk without error under this walker** (zero
crashes), and the **6** classified `CONT` go on to land **exactly** on the file's
own declared `u32(+0x30)` endpoint via continuation. This corroborates the
lenient reading — a construct that is a genuine misparse symptom would be a
less likely candidate to complete cleanly and then land exactly on a
file-declared oracle in 6 of 6 tries — **but corroboration is not proof**, for
the same reason the shipping reader's own clean-population validation is not:
a clean completion can have more than one cause, and this pass's oracle can only
ever confirm the skip, never refute the alternative. **[HIGH CONFIDENCE —
inferred, leaning toward "legitimate padding"; not adopted as settled.]** Because
this walker already implements the lenient reading, **every figure in this
subsection (334 `CONT`, the 55, the 63 = 55+8 combined short/long) is computed
under the "121/69" assumption** — if the stricter reading is instead correct, an
unknown subset of these 14 clips (bounded by 14) would move from `CONT`/`LONG`
into a failure-like bucket, which is exactly the shape of the bound stated above.

**Scope note carried through the rest of this subsection: "the 55" below is this
reimplementation's `SHORT_DS + SHORT_OV` output, one of the two bound-defining
readings, not a settled 69 or a settled 55.** Every figure below is scoped
explicitly to "the 55 as this walker identifies them under the lenient reading"
and should not be read as having resolved which end of `[55, 69]` is correct.

**The corpus-wide two-way split this document already published (§6c.4) is
confirmed unaffected, and is not the same population as the 518 above** — worth
restating because a coordinator review flagged exactly this as a place a reader
could wrongly subtract two figures. Over *all* 4,209 clips, "walks but misses
the oracle" is **399** (2 bit-clear + 397 bit-carrying) and "fails the walk
outright" is **123** (2 bit-clear + 121 bit-carrying); 399 + 123 = 522. The
four-clip difference from the 518 (bit-carrying only) is exactly the four
bit-clear clips §6c.4's own table already lists separately. **No conflict; two
counts of different things.**

**Everything from here to the disassembly findings measures this
reimplementation's 55 under the lenient/"121-69-bound" reading (see the scope
note above), not the stricter reading's 69, and not a per-clip list from either
bound — neither was available to this pass.**

**Size of the unaccounted region (`u32(+0x30) - cursor`, only clips whose
`+0x0A`-track walk itself completed — a precondition, not a restriction that
could hide a failure), n = 55:** min **522**, median **1,172**, max **4,174**
bytes. **[CONFIRMED — empirical, on the 55.]**

**Does the size scale with a header count?** Least-squares fit of the residual
against each candidate field, over the 55:

| Field | slope (bytes/unit) | intercept | R² |
|---|---|---|---|
| `+0x06` (frame/key count) | 16.24 | 846.9 | **0.196** |
| `+0x0A` (track count) | −188.5 | 9,866 | 0.037 |
| `+0x0B` (bone count) | −28.3 | 3,174 | 0.029 |
| `+0x08` | 45.4 | 1,075 | 0.029 |
| `+0x09` | 8.2 | 1,194 | 0.001 |

**None of these is a usable fit.** `+0x0A` and `+0x0B` have *negative* slopes,
which is physically backwards for a channel that grows with track or bone count,
and their R² is negligible — that rules out "one more value per track" and "one
more value per bone" as literal models. `+0x06` is the least-bad candidate but
explains under a fifth of the variance; restricting to the region *after* a
leading zero-byte run (below) barely improves it, **R² = 0.224**. Exact-division
tests are consistent with the regression: `resid % (+0x06) == 0` in only
**6/55 (10.9%)**, `% ntrk` in 1/55, `% nbone` in 0/55. **[Weak or no scaling with
any single header count field — this specifically weighs against a literal
fixed-bytes-per-frame channel, HIGH CONFIDENCE — inferred from the regression.]**

**A near-constant leading run of literal zero bytes dominates the region, and it
does not track frame count.** Measuring the longest run of `0x00` bytes starting
immediately at the cursor: median **84** bytes, with **38 of 55** clips landing
at exactly 84 and a further 10 within ±3 (83, 86); 3 clips have no leading zero
run at all; the largest is 136. Two clips illustrate the non-scaling directly:
`brute_hvy_sd_trn90_l` (`+0x06` = 40) and `cop_ps_aim_alta_trn90_lft` (`+0x06` =
18) both carry an 83–86 byte leading zero run despite a >2× difference in frame
count. **[CONFIRMED — empirical, that the leading run is near-constant and does
not scale with `+0x06`; this is evidence against, not for, a literal per-frame
channel starting immediately at the cursor.]**

**Does the unaccounted region decode as a bare stream under the confirmed
rotation or translation schemes, keyed by the frame count?** Two clean,
single-shot tests (one candidate length per clip, not a search — so the test can
fail and its negative is trustworthy): parse the region as a rotation-only
stream (control-record runs + 3-byte samples, §6c.2's own format, no leading
count field) with `rotKeys = (+0x06)`, and separately as a translation-only
stream (§6c.5's format) with `transKeys = (+0x06)`, 8-byte and 16-byte record
variants both tried. All three: **0 / 55 land exactly on `u32(+0x30)`.**
**[REFUTED — empirical]** for "the region is a bare rotation or translation
stream whose key count is `+0x06`."

**A broader, exhaustive version of the same idea is vacuous, and is reported as
refuted rather than quietly dropped.** Searching every candidate key count from 1
to 4,000 (instead of the single value `+0x06`) finds *some* working length for
**18 / 55** clips under the rotation scheme and **26 / 55** under the
translation scheme, each with ambiguity exactly 1 where found — which looks, at
first glance, like the same "exactly one count lands" signature the CONT
population's continuation search once offered as evidence, **and which the note
above has since retracted as evidence in general.** This search independently
demonstrates the same failure mode on its own terms, before that retraction
arrived. Controlled by shifting the search's start point by ±2, ±4, ±6, ±8 bytes — an
otherwise-identical search that should find nothing if the real start point is
what matters — the hit rate is statistically indistinguishable from the real
alignment: rotation hits range **17–22 / 55** across six shifts against **18 / 55**
real; translation **26–29 / 55** against **26 / 55** real. **A search over 4,000
candidate lengths against one fixed target byte offset is a test that almost
cannot fail**, for the same reason a walk over empty blocks cannot fail — enough
candidates are tried that some will land by chance regardless of where the walk
begins. **[REFUTED — empirical; kept in the harness (`anim_flag40j.py` /
`anim_flag40k.py`) rather than deleted, per this project's standing rule on
vacuous tests, so the weak version cannot be rediscovered and quoted.]**
**§5-worthy methodology note:** this is the ambiguity-count safeguard's own
failure mode — ambiguity-1 is only informative when the candidate space searched
is narrow enough that landing at all is informative; a wide enough search makes
ambiguity-1 achievable almost anywhere.

**Where does the region actually start behaving unlike a track-block stream?**
Continuing the *standard* per-track walker (the one that works for CONT)
one block at a time and recording where it first throws: for most `SHORT_DS`
clips it successfully parses roughly **42–45** further pseudo-blocks — consistent
with it trivially walking through the leading zero run as a sequence of valid
*empty* (`rk=0,tk=0`, 2-byte) blocks, exactly the hazard §9.2 measures at
population scale — before desyncing at a **data-dependent** byte offset that
does not track the leading-zero-run length (examples: desync at +96, +150, +160,
+346, +624, +960, +1,140, +2,004 bytes, for leading runs of 83–86 bytes in every
one of those cases; only **2 / 52** desync within 2 bytes of the leading-run
length). **The desync point carries no located structural meaning** beyond
confirming the region is not a valid sequence of standard track blocks — it is
where the coincidental zero-block-parse first stops being coincidental.
**[CONFIRMED — empirical, that continuation fails for a data-dependent rather
than a positionally-fixed reason.]**

**Byte-level read of the dense (post-zero-run) content.** Decoded as a flat
`i16` array it is *not* a clean fixed-stride table population-wide (no constant
bytes-per-frame ratio: `dense / (+0x06)` ranges from 9.2 to 83.6 across the 55),
but individual clips show internal regularity worth recording as a lead rather
than a finding: `cop_ps_aim_alta_trn90_lft` and its mirrored pair
`cop_ps_aim_alta_trn90_rgt` (same skeleton, same clip family) both show a
repeating 4×`i16` (8-byte) period in which the 4th lane is a **constant −19**
across at least four consecutive groups in both files. That is a real,
reproduced-across-a-mirrored-pair regularity, and it is exactly the kind of
single/paired-sample observation this project's own rules (§5, "don't generalize
from one small sample") say not to promote further without a population test —
which, given the exhaustive-search vacuity result immediately above, was not run
for this specific record shape in this pass. **[HYPOTHESIS — unconfirmed; a
concrete unfinished lead, not a claim.]**

**Disassembly: what does the code that owns the flags byte actually do with bit
`0x40`?** Traced directly from the two functions most responsible for turning a
clip's bytes into per-track data — not guessed from struct-displacement search,
per this project's own standing rule:

- **`FUN_004c1020`** (the per-track stream-setup/walk function, §6c.1's source)
  reads flags bits `0x80` (wide counts) and `0x20` (wide translation records) —
  and **nothing else from the flags byte**. It never tests bit `0x40`.
  **[CONFIRMED — disassembly.]**
- **`FUN_004c1260`** (the per-clip *bind* function — called once per playing
  instance, before per-track sampling; it builds the bone→track reverse lookup
  from the already-confirmed track-to-bone table at `+0x28`, using the track
  count at `+0x0A` and bone count at `+0x0B`) reads flags bits `0x01`, `0x02` and
  `0x04` into decoder-state fields — and **also never tests bit `0x40`.**
  **Incidental resolution of an existing open item:** the state byte
  `FUN_004c1020` tests to decide whether the "second rotation mode" extra byte
  is present is set here from flags bit `0x01` directly — clear takes the
  extra-byte branch, set (true of nearly every shipped file) skips it. Folded
  into §6c.3 item 3 above as **[CONFIRMED — disassembly]**.
- **The only site among the functions traced that reads bit `0x40` at all** is
  `FUN_004c21a0`, a large runtime layer-blend sampler that iterates a
  caller-supplied list of blend layers (its first parameter) and, per track,
  per layer, checks bit `0x40` of the clip's own flags byte at offset `+5` to
  decide whether to consult a **per-layer override array** — a count table and a value table
  reached through fields of that *caller-supplied* layer descriptor, not through
  the clip's own file buffer at that point in the code. A count of exactly 1
  fetches a single override value via an index table; any larger count defers
  to a separate function (`FUN_004ca010`) not traced in this pass.
  **[CONFIRMED — disassembly, for what the check gates.]** **Whether those
  override arrays are themselves populated, elsewhere, from this clip's own
  trailing bytes was not traced — [OPEN], and is the most direct remaining
  lead**: it requires reading the callers of the whole layer-blend chain (two
  found, `FUN_004c7640` and `FUN_004bd0b0`, decompiled but not yet read for this
  question) to see how a layer's override-array fields get set.

**Three independent lines now converge on a leading reading worth stating
explicitly, though not as settled: there may be no `0x40`-keyed extra-block
structure to find at all — the walk goes off the rails in these clips, full
stop.**

1. **Nothing in the stream setup tests the bit.** `FUN_004c1020` and
   `FUN_004c1260` — the two functions that actually turn a clip's bytes into
   per-track data — never reference flags bit `0x40`; the only site that does
   (`FUN_004c21a0`) gates a *runtime* override lookup against caller-supplied
   data, not a read of the clip's own trailing bytes, as detailed above.
   **[CONFIRMED — disassembly.]**
2. **The `span == 0` translation-control-record anomaly clusters perfectly
   in the non-landing clips** — 0 of 3,687 landing clips, 14 of 399 non-landing
   ones, all 25 such records in the whole corpus inside exactly those 14 clips
   (confirmed above, independently, against this pass's own walker).
   **[CONFIRMED — empirical.]**
3. **The continuation search finds a landing count almost regardless of where
   it starts — but only on the partner's broader, 518-clip denominator,
   and this pass's own re-run does not reproduce that on the correctly-matched
   397-clip denominator** (84.13% real vs. 9.32%/13.60% shifted, above). This
   third line is therefore **not** a clean third confirmation — it is
   population-dependent, and on the population that actually matches what
   the `CONT` = 334 figure is drawn from, real and shifted alignments *do*
   separate sharply. **[Genuinely in tension with lines 1 and 2, not a third
   line supporting the same conclusion — recorded honestly rather than folded
   in as if it agreed.]**

> **⚠→✅ SUPERSEDED 2026-09-12 (§10).** The three-way tension below resolved: all of lines 1-3 were measuring a walk started one small table too early. Skipping a per-track layer-override table -- parsed fresh, on every call, straight from the clip's own bytes, by a function this synthesis had not yet traced (`FUN_004c1190`, reached via `FUN_004c1260` from the layer-blend sampler `FUN_004c21a0`) -- makes **518 of 518** bit-`0x40`-carrying clips land exactly on `u32(+0x30)`, with two shift controls and a wrong-population control all collapsing to near-zero (§10.4). There is no third sub-population, no `[55, 69]` bound to settle, and no walk that goes off the rails for a data-dependent reason -- kept below exactly as written, because the reasoning holding two lines solid and one in tension was sound given what was known when it was written.**

**So the honest synthesis is two solid lines and one that complicates the
picture, not three that agree.** Lines 1 and 2 are consistent with "the walk
goes off the rails in the bit-`0x40` population and there may be nothing
`0x40`-keyed to find" — a considerably better answer than a fourth refuted
payload model, and one this document is comfortable stating at **[HIGH
CONFIDENCE — inferred]**. But line 3, measured on the population that actually
produced the `334` figure, shows real byte-precise structure *is* there for a
walk-shaped parser to lock onto at the correct alignment — which is not nothing.
**The two are not necessarily contradictory:** a stream that goes off the rails
in a data-dependent way (as the desync-point analysis below shows for "the 55")
can still contain some genuine, byte-precise structure without that structure
being "more per-track blocks specifically gated by `0x40`" — the disassembly
found no code path that would produce such blocks *because* of the bit. **What
the extra trailing bytes actually are remains [OPEN]**; the leading candidate
narrows to "real structure that the per-track walker was never designed to
parse" rather than "more of the same per-track blocks," but that is inferred,
not located in code. **Concrete next step, unchanged:** trace `FUN_004c7640` /
`FUN_004bd0b0` and `FUN_004ca010`, the callers and helper this pass identified
but did not read for this question.

**The partner team's per-frame-root-orientation hypothesis, tested and labelled —
on two different populations, kept distinct.** First, this reimplementation's 55,
measured independently against every bit-`0x40` sibling stratum (not only the
clean population, closing the §5-style objection that a trait checked only
against the target is not yet a discriminator):

| Population | n | name contains `trn` | median root angle (`+0x0C`) | p90 |
|---|---|---|---|---|
| unexplained-short (this walker's 55) | 55 | **45 (81.8%)** | **90.00°** | 180.00° |
| CONT | 334 | 5 (1.5%) | 0.00° | 0.00° |
| FAIL | 121 | 2 (1.7%) | 0.00° | 0.00° |
| LONG | 8 | 0 (0.0%) | 0.00° | 0.00° |
| bit clear + HIT | 3,687 | 145 (3.9%) | 0.00° | 90.00° |
| whole archive | 4,209 | 197 (4.7%) | 0.00° | 90.00° |

**[CONFIRMED — empirical, on the 55.]**

**Second, the partner team's own re-measurement on their stricter-reading 69**
(attributed, not re-derived — this pass has no per-clip list of either bound to
check it against): `trn` in filename **65.2%** (against **1.7%** in their
121-clip failure group), root rotation above 45° in **71.0%** (against **9.1%**
in the same failure group), **median root rotation still exactly 90.00°.**
**[CONFIRMED — empirical, attributed to the partner team, on their 69-clip
reading — kept as a separate row rather than merged with the 55 above, since
the two populations are not proven identical and, per the bound stated at the
top of this subsection, neither is settled.]** Both signals **weakened**
relative to the smaller reimplementation-derived 55/81.8%/87.3% figures — moving
to the larger population made the correlation *less* striking, not more. **That
is the direction that argues the signal is real rather than an artifact of how
the group was assembled**: a pattern manufactured by a grouping quirk would be
expected to look *better*, not worse, once the grouping changes; this one got
worse and stayed strong regardless — and this holds **at either end of the
`[55, 69]` bound**, since it is the direction of change, not the specific
endpoint, that carries the argument. So the population genuinely is what the
labels and the root-motion field both say it is, under either reading.

**But the specific form of the hypothesis — a literal per-frame orientation
channel sized by the frame count — is not supported by the byte-level tests
above**: the region's size correlates weakly at best with `+0x06` (R² ≈ 0.20),
is dominated by a near-constant, non-scaling zero run, and does not decode as a
bare rotation or translation stream under any single-shot test tried. These
byte-level tests were run on this walker's 55, which is not proven identical to
either end of the `[55, 69]` bound — a caveat that weakens the byte-level
negatives slightly (they could in principle be an artifact of the population
being *nearly* but not *exactly* right) without changing their direction, since
the population-level signal (name and root angle) that motivates the hypothesis
is confirmed on **both** the 55 and the partner's 69-clip reading independently.
**Verdict: HYPOTHESIS — unconfirmed, refined.**
These clips are turn-in-place by every measure available on either population,
and very likely carry *some* extra orientation-relevant payload — but a simple,
literal, fixed-bytes-per-frame channel is the specific form this pass rules out,
not the general idea that something root-motion-related lives in the trailing
bytes. A compressed, run-length-structured per-frame channel (structurally akin
to, but evidently not identical to, the confirmed rotation stream) remains the
leading unconfirmed candidate, and the disassembly lead most likely to settle it
— the layer-override array's population — was identified but not completed in
this pass.

### 9.2 Empty-block audit — run population-wide, as the gate this project's own rules require

**Predicate.** For every clip whose relevant walk completes, classify each
walked block as *empty* (`rotKeys == 0 AND transKeys == 0`, the 2-byte minimum
block) or not, and separately measure the fraction of walked *bytes* that are
literal zero. A walk dominated by empty blocks or zero bytes is "a test that
cannot fail" — HANDOFF §5's rule, applied here for the first time at population
scale rather than to one 12-clip sample. Harness `anim_flag40e.py` (built by the
prior session, run for the first time in this pass) plus its extension
`anim_flag40f.py` (new).

**The headline clean-population claim (3,687 / 3,691 land on `+0x30`, §6c.1)
survives the audit.** The `bit-0x40-clear, HIT` stratum's declared-track walk is
mostly *non*-empty: fraction of blocks empty, median **0.022**, q3 **0.065**
(n = 3,687) — the opposite of a trivial walk. Restricting the endpoint test to
only the clips whose walked region is at least 90% non-empty blocks (n = 3,186,
nearly the whole clean population survives this filter) still lands **3,185 /
3,186 (99.97%)** — **100.00%** within the clean stratum itself — against a
**start-shifted-by-7-bytes control at the same restriction of 4 / 3,186
(0.13%)**. **[CONFIRMED — empirical; the 3,687/3,691 figure is not an artifact
of empty-block triviality.]**

**The CONT extra-region emptiness measurement stands, but is relabelled: it
describes the region, and — per a direct check named below — most likely cannot
validate the continuation reading at all.** The 334 CONT clips' *declared*
`+0x0A`-track walk is itself mostly empty (median **84.4%** empty blocks — most
of these clips' declared tracks carry no motion at all, consistent with them
being layered/occupant animations, §6c.4). The **extra** blocks walked past
`+0x0A` — measured here for the first time (`anim_flag40f.py`) — show the
*opposite* pattern: median **2.6%** empty blocks, q3 **5.1%**; **323 / 334
(96.7%)** majority-real, only **2 / 334 (0.6%)** ≥ 90% empty.

> **What would a failing case have looked like? Checked directly, and the
> answer weakens this measurement's evidential value.** For the audit to
> discriminate "real track-block structure" from "arbitrary bytes the search
> happened to span," a region defined by a *spurious* landing would need to read
> mostly *empty* — otherwise the test cannot fail, because most of this file
> format's non-padding content is non-zero regardless of what it means.
> Measured directly on the 37 (shift `+7`) and 54 (shift `−5`) clips from the
> retracted search above that *do* find a spurious landing at the wrong start:
> their "extra regions" read median **5.9%** and **9.8%** empty respectively —
> **also predominantly non-empty**, in the same ballpark as the real region's
> 2.6%, not the declared region's 84.4%. **So a failing case is close to
> impossible to produce with this predicate, and the audit could not have
> discriminated the continuation reading either way.** **[Relabelled: describes
> the region (it is not zero-padding), and does not validate the continuation
> reading — retracted as support for it, per the same logic that retracted the
> continuation search itself above.]** The one thing worth keeping, stated as
> weak and not as confirmation: the real region is somewhat *more* non-empty
> than either spurious one (2.6% vs. 5.9–9.8% median empty) — a 2–4× gap, far
> short of the orders-of-magnitude separations this project otherwise treats as
> decisive, and not promoted beyond a marginal, second-order observation.
> Harness `anim_flag40m.py` (new).

**A separate, partner-team measurement on a different population, kept
separate rather than merged into the above per the coordinator's instruction:**
of the 518 bit-`0x40`-carrying clips, the partner team found **4 have zero keys
total** (a walk trivially satisfied by construction), and **all 4 fall in the
328-clip continuation bucket** — none in their 55 or their 135. That bounds the
triviality risk in *their* run to at most 1.2% of their continuation bucket, and
zero in the two strata the open questions actually rest on. This is a
bit-`0x40`-only measurement on their split; it is not the same population as
this section's corpus-wide audit (which covers all 4,209 clips, clean and
bit-carrying alike) and the two are not combined here.

**Net, corrected from this subsection's first draft: the corpus-wide
clean-population figure (3,687/3,691) clears the empty-block hazard cleanly and
demonstrably; the bit-carrying continuation figure's own empty-block audit
turned out not to be able to fail, and is not evidence either way.** The
declared-region audit that grounds the headline 3,687/3,691 figure is real,
controlled, and holds under restriction to non-empty-dominated walks. The
CONT extra-region audit does not carry the same weight — as shown directly
above, it describes a region that is non-empty whether or not the continuation
reading is correct, so it is retracted as support for that reading (joining the
continuation search itself, retracted in §9.1 for the same underlying reason:
a measurement that cannot fail is not evidence, however it is dressed).

### 9.3 §6c.6 item 1 CLOSED: the per-key translation delta is from-base, not accumulating

**[CONFIRMED — empirical.]** Attributed to the partner team; recorded here
because this pass's brief was to close it, not to re-derive it — the figures
below are theirs, restated with the populations and method stated per this
project's figure rule.

**Method.** Cross-run continuity at control-record boundaries: within a track's
translation stream, a run's *last* decoded position should sit close to the
*next* run's first decoded position if positions are measured consistently,
scored against the gap to an *unrelated* run in the same track as the control for
"how close is close." Two candidate decodes were scored this way — **from-base**
(`axis = base/64 + delta/4000` for every key in the run, §6c.5's published
formula) and **accumulate** (delta added cumulatively across the run) — since
both fit the per-key arithmetic in isolation and only a cross-boundary continuity
test distinguishes them.

| Run length | pairs (n) | from-base gap | accumulate gap | separation |
|---|---|---|---|---|
| 1 | 30,602 | identical by construction | identical by construction | none (zero information at this length, by construction) |
| 2–4 | — | **0.0332** | 0.0599 | — |
| 5+ | — | **0.0137** | 0.1137 | — |
| long runs | 13,017 | separates from its own control by **6.76×** | separates from its own control by **1.83×** | — |

**The dose-response is the finding, not the margin at any single length.**
From-base's separation from its control *improves* as run length grows (identical
at length 1 → 0.0332 vs 0.0599 at 2–4 → 0.0137 vs 0.1137 at 5+ → 6.76× at n =
13,017), which is exactly the signature a correct from-base decode should show —
accumulate's error should compound with run length while from-base's should not,
and that is what separates. Accumulate's own separation (1.83× on long runs) is
real but far weaker and does not improve with length the same way.

**§6c.6 item 1 is updated in place above** (kept visible with the original
"[OPEN]" wording per this project's standing rule on corrections) rather than
silently marked done.

### 9.4 The 16-byte-load-over-8-byte-record question — answered as an OPEN null, not resolved either way

> **Lead with the authoritative result; this pass's disassembly is a
> complement to it, not a replacement.** The partner team ran the *same*
> cross-run continuity oracle that closed §9.3 against the blend-vs-no-blend
> question directly, stratified by control-record span, and it did **not**
> discriminate cleanly: short spans (2–4) favour blend decisively, long spans
> (5+) favour no-blend decisively, with a genuine monotonic crossover near span
> 4 — confirmed real (not noise) by a model-divergence check showing the two
> strata genuinely predict different things, and by a base-difference control
> ruling out "long runs are just static." **Recorded OPEN, not resolved either
> way — do not force a verdict.** **[CONFIRMED — empirical that the test does
> not discriminate cleanly; attributed to the partner team.]**
>
> **The number that would have been published wrong if not stratified, kept
> here as the sharpest §5 lesson this task produced:** pooled and unstratified,
> this same data reads **9.89× vs 7.34× favouring BLEND** — a clean-looking
> confirmation of the wrong answer, produced by exactly the kind of
> mixed-population dilution this project has hit repeatedly (§6c.4's own
> original 90.2% figure, §13's morph records, §26.9's vehicle equality test).
> **One weak directional lean, stated as weak and not as a verdict:** a true
> blend-toward-the-next-record decoder should get *better* as its prediction
> weight approaches the next record's own value at long spans, not roughly 3×
> *worse* — which argues against a simple linear blend, and is at least
> directionally consistent with §9.3's from-base finding, without being strong
> enough to close the question.

**The precision failure this task named, restated so it cannot recur:** §6c.2
once wrote "sample bytes" in prose where the disassembly and pseudocode both said
**control record bytes**, and that caused a failed replication (corrected in
place in §6c.2, still visible). This subsection's own disassembly work is about
a *different* 16-byte load, in the *translation* decoder, and both readings turn
out to be real — at different sites in the same function — which is presumably
how the earlier conflation happened in the first place. **This mechanism-level
work does not itself settle the question above** — the partner's data-level
stratified test is the answer, and it is OPEN — but it narrows *where* a
resolution would have to be found in code, which is offered here as the
concrete next step rather than as a competing verdict.

**Disassembly of `FUN_004c9b90`** (the translation decoder) shows **two** distinct
16-byte (SSE, `undefined1 [16]`) loads, doing two different jobs:

1. **Over the sample-byte stream.** The decoder holds a cursor to the 3-byte
   per-key delta stream (state offset `+0x2b8`, written by the stream-setup
   function as `payload-after-records`) and loads 16 bytes starting there in one
   instruction, then runs an extended SSE byte-shuffle sequence to unpack several
   keys' 3-byte delta triples out of that one load in parallel. **This is a
   batched read of the already-confirmed per-key delta encoding (§6c.5), not an
   inter-record blend** — it is the same 3-bytes-per-key sample data §6c.5
   already decodes, read several keys at a time for SIMD throughput.
   **[CONFIRMED — disassembly.]**
2. **Over the control-record bytes themselves, in the single-record fast path.**
   When a track's translation stream has exactly one control record
   (`transKeys` run count == 1 — a common case, not a rare one), the decoder
   loads 16 bytes starting **2 bytes into that one 8-byte record** (i.e. at
   `recordPtr + 2`, skipping the leading `u16 span` field) and extracts the
   record's `x`, `y`, `z` fields (each `i16`) from shifted/duplicated copies of
   that load. Because the record is only 8 bytes and the load starts 2 bytes in,
   it necessarily reads **8 bytes past the record's own end.**
   **[CONFIRMED — disassembly, that this overread happens.]**

**Does the traced code read a *second* record's fields — i.e. is this actually
inter-record blending?** In the single-record path traced, **no** — every
extraction observed in the decompilation comes from shifted views of the *same*
16-byte load, and the values used for
`x`/`y`/`z` are exactly the one record's own fields. There is no second
`u16 span` field consumed, and no second record's pointer is dereferenced in this
path. **The evidence found here does not support "blends between two consecutive
control records"** — the overread is at minimum consistent with, and not shown
to be more than, a SIMD-convenience load that happens to spill into whatever
follows (either padding or the start of the sample-byte stream). **[The
inter-record-blend hypothesis: still HYPOTHESIS — unconfirmed, narrowed rather
than resolved.]** The general case — a track whose translation stream has
**more than one** control record, which is where genuine inter-record blending
would have to be visible if it exists at all — sits deeper in the same
1,147-byte function and was **not reached in this pass**. **[OPEN — concrete
next step: trace the `transKeys > 1` branch of `FUN_004c9b90` specifically for
whether it dereferences two adjacent record pointers rather than one.]**

**Connecting to §9.3, precisely — and correcting an over-tight coupling in the
task brief.** §9.3's continuity result is about whether one record's *own*
stored delta is measured from that record's *own* base or accumulates across
keys — a question about a single record's arithmetic. Inter-record blending is
about whether the *decoder* additionally interpolates between two *different*
records' base positions when sampling near their boundary — a question about
what happens *between* records. **These are compatible but independent claims:**
a from-base decode within each record is fully consistent with the decoder never
blending between records at all, and §9.3's strong continuity result does not
itself supply evidence for blending — well-authored animation data naturally
continues smoothly across a run boundary even under a simple, non-blended
from-base decode, which is the more parsimonious reading of why continuity came
out so strong. **So §9.3 strengthens confidence in the from-base *formula*, and
neither strengthens nor weakens the separate inter-record-blend hypothesis.**
Recorded because the task brief's framing ("continuity is exactly what blending
would produce") is true but incomplete — continuity is *also* exactly what a
correct non-blended decoder run over smooth authored motion would produce, so it
cannot be used to choose between the two.

## 10. Flags bit `0x40`'s runtime consumer, found: a layer-override table parsed fresh from the clip's own bytes -- the third-sub-population thread closes

> **Scope.** This section answers the item HANDOFF.md flagged as the single
> highest-value remaining question on the `0x40` thread: who populates the
> per-layer override arrays that the runtime layer-blend sampler
> (`FUN_004c21a0`) reads, and when. It traces that sampler's own layout in
> full, follows every caller of the relevant helper functions back to source
> by raw disassembly (not just decompiled pseudocode, because the decompiler
> silently drops register-passed arguments under `-noanalysis` for at least
> one call in this chain -- see the methodology note at the end), and tests
> the resulting hypothesis directly against file bytes. New Ghidra scripts
> `AnimLayerOverrideSource.java`, `AnimLayerDisasm.java`. New harness
> `anim_layer_override_skip.py`.
>
> **Headline: the loop closes.** A second, independent site tests flags bit
> `0x40` -- not to *consult* an override table (that is `FUN_004c21a0`, already
> known from the prior pass) but to decide whether to *build* one, parsing it
> directly out of the clip's own bytes at exactly the address the ordinary
> per-track walker has always started from. Skipping that table before
> starting the ordinary walk makes **518 of 518 (100.00%)** bit-`0x40`-carrying
> clips land exactly on `u32(+0x30)` -- every clip in every stratum this
> project's own reimplementation previously called `CONT`, `SHORT_DS`,
> `SHORT_OV`, `LONG` or `FAIL`. There is no third sub-population, no `[55, 69]`
> bound left to settle, and no walk that "goes off the rails" -- the walker
> was starting one small table too early, for every single one of these
> clips. **[CONFIRMED -- disassembly + empirical.]**

### 10.1 What `FUN_004c21a0` actually reads the override data from

`FUN_004c21a0` is a `thiscall` function; its receiver (`ECX`, called `param_1`
below) is a per-call **blend/evaluator context**, not the clip buffer. Reading
its body in full, the fields actually touched are:

| Offset in the receiver | Contents |
|---|---|
| `+0x00`, `+0x04`, `+0x08` | pointers, gated as an existence check before anything runs |
| `+0x14` | a pointer, dereferenced at `+0x24` |
| `+0x34` | a pointer to a short exclusion/remap table (`-1` sentinel) |
| `+0x3C` | a pointer to the active skeleton/rig object |
| `+0x58` | the **active layer count** |
| `+0x74`..`+0x58*0x50+0x74`(exclusive) | an embedded array of **layer instance records**, stride `0x50` bytes, one per active layer |

Within a layer record (indexing from the loop pointer used in the
decompilation, which sits at record `+0x10`): `-0x10` is the **clip index**
(into the clip registry, `DAT_030f0c68`, stride `0x24` -- the buffer pointer
this function reads back out of the registry sits at entry `+0x20`, not
`+0x10`; this is a distinct field from the previously catalogued `+0x10`
buffer-pointer symbol and is not reconciled further here), `+0x00` is a small
bucket/priority index (0-3, used to group layers before blending), and `+0x10`
is a **float weight** -- zero or negative disables the layer outright before
any override lookup happens.

The actual per-track override data, though, lives **nowhere the caller
supplies** -- it lives in a **fully local, stack-resident array inside
`FUN_004c21a0` itself**, `memset` to zero at function entry: 9 slots, stride
`0x2d1` (721) bytes, one slot per active layer. Call this the **bind context**.
Only for layers whose weight is `> 0` does `FUN_004c21a0` call a bind function
(`FUN_004c1260`, see below) that fills in one slot fresh, immediately before
that layer's per-track sampling loop runs. Within a bind-context slot, the
fields the override lookup reads are:

| Bind-context offset | Contents |
|---|---|
| `+0x00` | `1` (built via reverse-lookup) or `2` (identity, no track-to-bone remap) |
| `+0x04` | pointer to the **per-track override index/count table** |
| `+0x08` | pointer to the **flattened override value table** (4 bytes/entry) |
| `+0x10`, `+0x14`, ... | a **prefix-sum array**, one `i32` per track, converting each track's own count into a starting offset into the value table |
| `+0x218`.. | the bone-index -> track-index reverse lookup (built when the clip has a track-to-bone table; `0xff`-sentinelled) |
| `+0x2cc`..`+0x2cf` | mirrored single-bit flags copied from the clip's own flags byte (`+0x05`), including the override index-table's element width |

The lookup itself, per track, per active layer: if flags `+0x05` bit `0x40` is
clear, the override path is skipped entirely and a default weight vector is
used. If it is set, the per-track entry in the `+0x04` index/count table is
read (1 or 2 bytes wide, chosen by flags bit `0x80`); a value of `0` again
falls back to the default; a value of exactly `1` fetches a single `i32` from
the `+0x08` value table at the offset the `+0x10..` prefix-sum array names for
that track; any larger count defers to a separate function, `FUN_004ca010`,
**not traced in this pass** (see §10.5). **[CONFIRMED -- disassembly, all of
the above, from a full read of `FUN_004c21a0`'s decompilation.]**

### 10.2 Traced to source: the override table is parsed fresh from the clip's own file bytes, on every call

`FUN_004c21a0` has exactly two callers (already found by the prior pass,
re-confirmed here): `FUN_004c7640` and `FUN_004bd0b0`, neither of which builds
or passes in any override data itself -- both simply invoke the sampler.
`FUN_004c21a0` calls the bind function, `FUN_004c1260`, once per active,
nonzero-weight layer, and `FUN_004c1260` itself decompiles with **zero visible
call arguments** to the function it tail-calls, `FUN_004c1190` -- a
`-noanalysis` decompiler artefact (register-passed arguments the caller never
itself reads are invisible to the pseudocode), resolved by reading the raw
x86 instead:

`FUN_004c1260` computes, unconditionally, a pointer `EBP = clip + hs`, where
`hs` is `0x38`, or `0x48` if the clip's own flags byte (`+0x05`) has bit
`0x10` set (`EBP` is first loaded as `clip + 0x38`, then bumped by a further
`0x10` when that bit is set) -- then adds the clip's own
track count (`byte` at `+0x0A`) to it. It then calls `FUN_004c1190` as
`(ECX = bind-context slot, EDX = track count, stack: [clip pointer, EBP])`.
**That `EBP` value is exactly `clip + payload_start(clip)`** in this project's
own established sense -- the identical address `anim_layout.payload_start()`
has always computed as the start of the ordinary per-track keyframe walk, and
the identical `hs` constant (`0x38`/`0x48` gated by bit `0x10`) that harness
already used, now independently corroborated at the instruction level rather
than only empirically. **[CONFIRMED -- disassembly, from raw instructions at
`0x004c1260`-`0x004c12fc`.]**

`FUN_004c1190` is where the second `0x40` test lives (§10.3). When it runs the
override-table-building path, it treats the memory at that same address --
**the clip's own resident file buffer**, not a separate allocation, not a
caller-supplied structure, not anything written by an external system -- as:

1. a **count table**, one entry per track (`byte`, or `u16` if flags bit `0x80`
   is set), how many override values that track has;
2. **4-byte aligned**;
3. a **value table**, one `f32`-sized (4-byte) entry per override value, total
   count = the sum of every track's own count;
4. a **tail table**, one entry per override value (`byte`, or `u16` if flags
   bit `0x02` is set), not traced further this pass (§10.5).

It stores the count/value table pointers directly into the bind-context slot
(`+0x04`, `+0x08`) and the address immediately after the tail table into a
"next data" field (bind-context `+0x210`) -- which, by construction, is
exactly where the ordinary rotation/translation track data must begin for
these clips. **This directly answers the "populated once at load, written
every frame by a manager, or supplied externally" question the task asked:
none of the three.** The table is neither cached nor externally sourced -- it
is **re-parsed from the clip's own static bytes on every single call to the
layer-blend sampler**, for every layer with nonzero weight, and discarded when
that call returns (the bind context is stack-local). This is cheap because
the tables are small (§10.4) and the source is immutable file data, so
re-parsing avoids needing any persistent cache at all. **[CONFIRMED --
disassembly.]**

A sibling bind function, `FUN_004c1300` (found in the prior pass as "a caller
of stream setup"), also calls `FUN_004c1190`, but with a visibly different
setup -- `EDX` (track count in `FUN_004c1260`'s call) is hard-coded to `1`, and
the pointer argument is computed as `clip + u32(clip+0x30)` (the header's own
declared-end-of-payload field used as a self-relative offset) rather than
`clip + payload_start(clip)`. `FUN_004c21a0` never calls `FUN_004c1300`
directly (confirmed absent from its full decompilation), so this is **not**
part of the chain this task asked about, and is flagged rather than chased
further: it looks like a distinct, single-slot mechanism belonging to
whichever *non-layered* single-clip playback path binds via `FUN_004c1300`,
possibly genuinely keyed off the declared end of the keyframe payload rather
than its start. **[OPEN / UNKNOWN -- not traced this pass, named so it is not
lost.]**

### 10.3 The direct bit-`0x40` test in the populate direction -- the loop closes

`FUN_004c1190` opens by reading the clip's flags byte (`+0x05`) into register
`AL` and testing bit `0x40` directly, branching past the entire
table-building path when the bit is clear. When the bit is **clear**, the
function does nothing but store the passthrough pointer (still exactly
`clip + payload_start(clip)`) into the bind context's "next data" field and
returns immediately -- no count table, no value table, nothing is built.
When the bit is **set**, it falls through into the count/value/tail table
construction described in §10.2. **[CONFIRMED -- disassembly, raw
instruction at `0x004c11a0`.]**

This is the second, independent test-site the prior pass's disassembly read
had flagged as the decisive missing piece: `FUN_004c21a0` tests the same bit
to decide whether to *consult* the override arrays; `FUN_004c1190` tests it to
decide whether to *build* them in the first place, from the clip's own bytes,
at the exact cursor the per-track walker starts from. Both readings are
mutually consistent and both are now traced to source. **This is exactly the
"different code path testing the bit" the runtime-layering hypothesis
predicted, and it is the mechanism, not an inference from file-side
correlation.** `0x40` is not a payload-location flag and never was; it is a
genuine runtime layering flag whose file-side footprint (a small table
inserted before the keyframe payload) is a side effect of the mechanism, not
the mechanism itself.

### 10.4 Empirical confirmation, with the controls the Figure Rule requires

**Predicate.** For every clip with flags bit `0x40` set and track count `> 0`
(published population: **518**), compute `p0 = payload_start(d)` (this
project's own established function, unchanged), read the per-track count
table starting at `p0` (width chosen by flags bit `0x80`), sum it, 4-align,
add the value table (`sum x 4` bytes) and the tail table (`sum x 1` or `x 2`
bytes, chosen by flags bit `0x02`) to get `p1`, then run the **unmodified**
`walk_track` exactly `ntrk` times starting from `p1` and check whether the
cursor lands exactly on `u32(+0x30)` -- the same oracle every prior pass in
this thread has used. Bit-`0x40`-**clear** clips are walked from `p0`
unchanged (this table is never built for them, per §10.3), so they serve as
an untouched sanity check on the same run.

| Run | Population (denominator) | Land exactly on `u32(+0x30)` |
|---|---|---|
| **Real: walk from `p1`** | bit `0x40` set, n = **518** | **518 / 518 -- 100.00%** |
| Sanity: bit `0x40` clear, walked from `p0` (unchanged) | n = 3,691 | 3,687 / 3,691 -- 99.89% (matches the already-published figure exactly, as it must -- nothing was changed for this population) |
| **Control A** -- shift the computed `p1` by **+7** bytes before walking | bit `0x40` set, n = 518 | **2 / 518 -- 0.39%** (511 now fail outright) |
| **Control B** -- shift `p1` by **-5** bytes | bit `0x40` set, n = 518 | **0 / 518 -- 0.00%** |
| **Control C** -- apply the identical count/value/tail-table formula to bit-`0x40`-**clear** clips too (wrongly -- `FUN_004c1190` never builds this table for them) | bit `0x40` clear, n = 3,691 | **1 / 3,691 -- 0.03%**, collapsed from the unmodified 3,687 / 3,691 |

**What a failing case would have looked like, stated per the house rule
before the number is trusted:** if the override-table layout recovered from
disassembly were wrong, or if the real keyframe payload for these clips
started somewhere else entirely, the corrected walk would reproduce the same
mixed `CONT`/`SHORT`/`LONG`/`FAIL` profile as walking from `p0`, not a clean
100%; a byte-precise but *wrong* formula could plausibly land a handful of
clips by chance but should not survive a 5-7 byte shift, and should not be
harmless (let alone catastrophic) when wrongly forced onto a population that
never has this structure. All three controls behave exactly as a real,
byte-precise, correctly-scoped structure predicts and a coincidence would not:
sharp collapse under either shift direction, and a **worse-than-chance**
result (1/3,691, versus a 3,687/3,691 baseline) when misapplied to the wrong
population. **[CONFIRMED -- empirical; harness `anim_layer_override_skip.py`,
reusing `anim_probe.collect()` and `anim_layout.payload_start()` /
`walk_track()` unmodified.]**

**Exhaustively, over the same 518 clips: flags bit `0x80` (wide index table)
and bit `0x02` (wide tail table) are both clear in every single case (0 / 518
each).** The narrow-width code paths inside `FUN_004c1190` are the only ones
this corpus exercises; the wide-width branches are read from disassembly only
and remain **HYPOTHESIS -- unconfirmed** for correctness, since no file in
hand exercises them.

**Descriptive, not yet interpreted:** counting, per clip, how many tracks
carry a nonzero override count (i.e. an actual entry in the value table) over
the same 518: the dominant mode is **10 tracks** (313 / 518), a second mode is
exactly **1 track** (87 / 518, of which 84 share the identical track index),
then 35 tracks (52 / 518), then smaller counts. **[CONFIRMED -- empirical, the
raw distribution.]** No bone-name mapping was consulted this pass, so which
bones these tracks address, and why 10 is the dominant count, is
**OPEN / UNKNOWN** -- flagged as a concrete, well-scoped next lead rather than
guessed at (a natural next step: cross-reference the 84-clip single-track
mode's shared track index against the rig's track-to-bone table for a handful
of clips, to see whether it consistently names the root/pelvis bone the
turn-in-place hypothesis already points at).

### 10.5 What this closes, and what is still open

**Closed.** The `[55, 69]` bound, the three-way `CONT`/outright-`FAIL`/
never-lands split, and the "does the walk go off the rails for a
data-dependent reason" question (HANDOFF.md §27.2 item 1, spec §9.1) are all
**dissolved, not settled at either endpoint** -- in the sense of the
project's own standing lesson that a mechanically explained absence closes a
question rather than answering it. There was never a genuine three-way
population split: every one of the 518 clips is explained by the single
mechanism above. The turn-in-place root-motion correlation (§9.1, median
root rotation 90.00° in the "never lands" population under the old framing)
is now additionally explained rather than merely correlated: those clips are
exactly the ones whose flags bit `0x40` is set, and bit `0x40`'s job is to
attach a small per-track table that lets the runtime layer blender inject a
root-orientation override on top of the ordinary keyframe data -- which is
precisely the mechanism a turn-in-place clip needs and a locomotion clip does
not.

**Still open, concretely scoped:**

- `FUN_004ca010` (the multi-value override path, taken when a track's override
  count is `> 1`) and the two functions it defers to (`FUN_004c8990`,
  `FUN_004c8a00`) are not traced. This governs what happens when a track has
  *more than one* override value queued, which this pass's descriptive count
  above shows is common (most of the 313 ten-track-override clips likely have
  a nonzero count only occasionally exceeding 1 per track, but this was not
  checked directly). **Next step:** decompile `FUN_004ca010` the same way
  `FUN_004c1190` was read here.
- `FUN_004c1300`'s distinct, `clip+u32(+0x30)`-relative use of `FUN_004c1190`
  (§10.2) is a related but separate mechanism, not part of the chain this
  task traced, and not chased further. **Next step:** find `FUN_004c1300`'s
  own callers to identify the non-layered playback path it belongs to, and
  check whether *that* path is where any remaining `0x40`-adjacent file bytes
  (if any exist beyond what §10.4 already accounts for) actually live.
- The exact semantic role of the tail table (flags bit `0x02`, one entry per
  override *value* rather than per track) is recovered structurally
  (§10.2/§10.4) but not interpreted -- what it is a table *of* was not
  determined.
- No dynamic pass is needed to go further on the specific question this task
  asked. The static evidence (100.00% landing against two shift controls and
  a misapplication control, all collapsing as predicted) is as decisive as
  this project's own methodology asks for; a dynamic breakpoint on
  `FUN_004c21a0` would only reconfirm what the byte-level test already shows.

**§5-worthy methodology note.** A decompiled call site showing zero visible
arguments is not evidence the callee takes none -- under `-noanalysis`,
register-passed arguments that the immediate caller never itself reads (only
forwards) are invisible to the decompiler's pseudocode, even though the
callee's own decompilation correctly recovers a full parameter list. The
tell was a genuine four-parameter callee (`FUN_004c1190`) reached by an
apparent zero-argument call (`FUN_004c1260`'s `FUN_004c1190();`); the fix was
reading raw disassembly at the specific call site rather than trusting the
decompiler's account of it. This is the same shape as the project's standing
rule about auditing an instrument's whole output list once it is shown wrong
at one point -- here inverted usefully: a decompiled call that *looks*
information-free is exactly the place to go back to raw instructions before
concluding a function has no traceable inputs.
