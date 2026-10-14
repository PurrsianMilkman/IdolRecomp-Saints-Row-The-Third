# Saints Row: The Third — `.csc_pc` Cutscene Camera Script Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, target selected from `spec-format-inventory.md` §6 (type 24, internal name `Cutscene Camera Script`, no `g`-side extension).
**Scope:** The whole file: the ~~12-byte~~ **8-byte** header *[corrected 2026-09-30 per §3's own layout: `u16` version, `u16` count, `u32` record offset; the `+0x08` word was withdrawn 2026-09-20]*, the shot-record array, and the eight per-shot animation channels with their keyframe time and value arrays.
**Method:** Registration row → constructor → **the constructor turned out to parse nothing**, so the trail ran through the global it stashes to its single consumer and on to the real parser (`tools/csc_ctor.txt`, `tools/csc_consumers.txt`, `tools/csc_parser.txt`), then a loader replay against **all 96 distinct shipped files** (192 entries, every duplicate byte-identical) with an exact-file-size check. Harnesses: `scratchpad/csc_bulk.py`, `csc_replay.py`, `csc_values.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. Offsets, strides and the version constant are load-bearing format data. Function addresses cited as evidence.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30)** — adversarial desk review (`review/adv_cutscene.md`), 15 units (§1, §2, §3, §4, §5 with §5.2, §6, §7, §10.2–§10.8, §11): **DESK-PASS 3** (§6, §7, §10.8); **DESK-PASS, text fixes applied 2** (§1 — the scope line's "12-byte header" corrected to 8; §10.7); **NEEDS-EXE 5** (§10.2, §10.3, §10.4, §10.5, §10.6); **NEEDS-DATA 2** (§2 — where the 96 duplicate entries live; §11 — the §11.4 "smallest gap above 0.033401 s is 3.98 s" contradicts the median 1.267 s / p90 4.10 s of §10.6.3 and §11.5); **VALIDATED-BY-DATA 3** (§3, §4, §5 layout — Team B, `team-b/HANDOFF.md` §4.6 and §9.73: 192/192 entries = 96 distinct, exact size identity, every figure exactly doubled, 31,898/31,898 channel-1 unit norm, FOV 2,345/2,345, DOF ordering 3,929/3,958, absolute key times 9,260/9,260). A desk pass alone does not clear a unit: it means the text is internally consistent and agrees with the other specs, not that it was re-derived from the executable or data; only the VALIDATED-BY-DATA units are already backed by Team B's full-population run. **Awaiting the executable:** §10.2 (global reader lists, row-7 slot, clock field types), §10.3 (float vs double operand widths, slerp epsilon), §10.4 (component order, and whether the stored rows are `M` or its transpose — the handedness rests on disassembly only), §10.5 (DOF plumbing, the degrees constant's address and type, the always-false remap gate), §10.6 (clock, seek, shot count, scene-kind test), and §3's rejection rules. **Awaiting real data:** §2 (entries by archive), §3 (order of arrays in the data region, for writers), §10.6 item 6 / §10.9 item 5 (script `u16@2` vs XML `Shots` count), §11.4 (re-run the tail-gap histogram).

---

## 1. Headline results

- **The registered constructor does not parse the file.** It stores the buffer and its size into two globals and returns success — the same stash-only pattern as morph types 11/12. **[CONFIRMED — disassembly.]** Because the destination is a *global* rather than a per-instance object, **only one cutscene camera script can be live at a time** — a real engine constraint, not an artifact of the format. **[HIGH CONFIDENCE — inferred from the stash target.]**
- **The parser is elsewhere and was reached by following the global**, not the registration table: the cutscene load driver (which also allocates a literal *"cutscene load pool"* of `0x190000` bytes) passes the stashed buffer to a pure pointer-fixup routine. **[CONFIRMED — disassembly.]** This is the first format in this project whose real parser is *not* reachable from its registration row — a methodology point worth carrying forward (§8).
- **The structure is fully determined and the replay is exact: `record_offset + record_count × 0x68 == file size` in 96/96 files**, with zero parse errors and zero out-of-range pointers across 22,480 channel pointers. **[CONFIRMED — empirical.]**
- **A file is a list of contiguous camera shots.** Each record carries a float start and end time, and **record *N* starts exactly where record *N−1* ends in 1,309/1,309 consecutive pairs**, with the first record of every file starting at `0.0` (96/96). **[CONFIRMED — empirical.]**
- **Keyframes are authored at 30 fps.** Across 1,999 multi-key time tracks — **all 1,999 strictly ascending** — the dominant inter-key step is `0.0333`/`0.0334` s, with the next most common steps its multiples. **[CONFIRMED — empirical.]**
- **No shared engine block appears anywhere in these files** — the known-magic scan finds none of the material, Mesh, Morph, ANIM, foliage or tree magics. `.csc_pc` is a genuinely standalone format. **[CONFIRMED — empirical.]**
- **Update 2026-09-20 — the sampler (§10) closes the format.** Channel 1 is a 4 × `s16` quaternion scaled by `16385/2^28` (the "not a quaternion" verdict in §5.1 was a factor-of-two scale error), channels 3–7 are depth-of-field controls, interpolation is linear (shortest-arc slerp for channel 1) with hold at both ends, and key times are absolute cutscene time. **[CONFIRMED — disassembly + empirical.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (scope line: 12-byte header corrected to 8, per §3) — desk review (not re-derived from the executable).**

## 2. Population facts

| | |
|---|---|
| Entries | **192**; **96 distinct** names, every duplicate byte-identical |
| Location | `cutscenes.vpp_pc` (90), plus **6 in DLC** (`dlc1_gb_in/out`, `dlc2_gis_intro/outro`, `dlc3_mm_in/out`) — one bundle per cutscene |
| Sizes | 148 … 45,240 bytes |
| `.gsc_pc` | **0** — consistent with the registration declaring no secondary extension |
| Shots per file | 1 … 82; **1,405 shots total** |
| Naming | mission/scene codes (`01_in`, `07_z03`, `22_out2`) plus a few test assets (`test_vehicle_light`, `test_pc_light`, `genki`) |

**[OPEN — desk review 2026-09-30: 90 + 6 accounts for the 96 distinct files, but 192 entries mean each file appears twice, and the text does not say where the second copies live (the same archive twice, or a second archive); to be settled against real data (list the 192 entries with their archives).]**

**Review status (2026-09-30): NEEDS-DATA: location of the duplicate entries. Size range, shot counts and 0 `.gsc_pc` are reproduced by Team B (`team-b/HANDOFF.md` §4.6) — desk review (not re-derived from the executable).**

## 3. File layout

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `+0x00` | 2 | **Version — must equal `4`**; the parser rejects anything else | **[CONFIRMED — disassembly + 96/96.]** |
| `+0x02` | 2 | **Shot-record count** (1 … 82); drives the record walk | **[CONFIRMED — disassembly + 96/96.]** |
| `+0x04` | 4 | **Offset → the shot-record array**, fixed up to a pointer at parse; `-1` means null and the parser rejects the file | **[CONFIRMED — disassembly + 96/96.]** |
| ~~`+0x08`~~ | ~~4~~ | ~~**Zero in 96/96**, and the parser never reads it — padding to align the keyframe data to 12~~ **CORRECTED 2026-09-20 (Team B's independent reader; orchestrator re-derived on all 96 files): there is no header word at `+0x08`.** The zero that reads there is the first key (`0.0f`) of the first shot's channel-0 times array. | ~~[CONFIRMED — empirical + disassembly (absence).]~~ **[CONFIRMED — empirical, 96/96.]** |
| `+0x08` … | | **Keyframe data region** (starts at **`+0x08`**, not `+0x0C`) — the time and value arrays every channel points into, packed back to back with no padding and no headers of their own. **The unique arrays tile `[0x08, record_offset)` with no gap in 96/96 files; tiling from `0x0C` succeeds in 0/96.** (Widths and structure elsewhere in this section are unaffected.) | **[CONFIRMED — empirical, 96/96.]** |
| `record_offset` | `count × 0x68` | The shot-record array; **ends exactly at EOF in 96/96** | **[CONFIRMED.]** |

**Channels 3–7 share one times array (added 2026-09-20, Team B; orchestrator re-derived): in all 1,009 shots that carry them, channels 3, 4, 5, 6 and 7 point at the SAME times array and have the same key count** — five value arrays over one shared time base. Offsets inside the file are file-relative and fixed up to absolute pointers at parse time, with `-1` meaning null — the same idiom as every other format in this project. In practice **no shipped file uses the null case** in a channel pointer (0 of 22,480); empty channels instead carry a count of `0` with both pointers aimed at the end of the data region *(desk review 2026-09-30: i.e. at `record_offset`, the end of `[0x08, record_offset)` above)*. **[OPEN — desk review 2026-09-30: the order of the arrays inside the data region (per shot, per channel, times before values?) is not stated. A pointer-following reader does not need it, but a writer does. Nor has it been checked that every empty channel's pointers equal `record_offset`. To be settled against real data.]** The `-1` rejection and the version-4 check are never triggered by a shipped file, so they rest on disassembly alone.

*Validated by data (desk review 2026-09-30): Team B's reader enforces `record_offset + count × 0x68 == size` on 192/192 entries (96 distinct), and tiling from `0x08` succeeds while tiling from `0x0C` fails (`team-b/HANDOFF.md` §4.6, §9.73).*

**Review status (2026-09-30): VALIDATED-BY-DATA: exact size identity 192/192 entries = 96/96 distinct files (Team B §4.6, §9.73); rejection rules disassembly-only — desk review (not re-derived from the executable).**

## 4. Shot record (`0x68` = 104 bytes)

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `+0x00` | 4 | **Start time**, float seconds. `0.0` in the first record of every file (96/96), and equal to the previous record's end time in 1,309/1,309 consecutive pairs | **[CONFIRMED — empirical.]** |
| `+0x04` | 4 | **End time**, float seconds (0.567 … 226.967 across the population) | **[CONFIRMED — empirical.]** |
| `+0x08` | `8 × 12` | **Eight animation channels**, §5 | **[CONFIRMED — disassembly.]** |

Shots tile the cutscene's timeline exactly — end to end, no gaps and no overlaps. The last record's end time matches the file's latest keyframe (within 0.0345 s — no file matches to within a tight 1e-3 s tolerance; the exact threshold and its provenance are in §11) in only 39/96 files, so **the shot list is authoritative for timing and the keyframes need not extend to its end**. ~~**[CONFIRMED — empirical; OPEN — what the engine does in the tail.]**~~ **[CONFIRMED — empirical, re-derived exactly (§11.5); the engine's tail behavior is resolved, not open — §10.6 item 3 / §10.3 rule 3: every sampler holds its last value, the shot still runs to its end, then a hard cut.]**

*Validated by data (desk review 2026-09-30): Team B, `team-b/HANDOFF.md` §4.6 — 2,810 shots = 2 × 1,405, first shot at 0.0 on 192/192; contiguity 2,612/2,618 under exact float equality, and the six misses are all within 0.000107 s (float32 rounding at about 206 s), inside §10.7's 2e-4 s tolerance.*

**Review status (2026-09-30): VALIDATED-BY-DATA: shot layout and contiguity (Team B §4.6) — desk review (not re-derived from the executable).**

## 5. Animation channels (12 bytes each)

`{ u32 key_count, u32 values_offset, u32 times_offset }` — both offsets fixed up against the file base, `-1` → null. The **element width is not in the parser** (the consumer indexes the arrays), so it was derived from the data region's own boundaries: each array runs to the next array's start, giving `width = span / key_count`. The result is **unanimous across all 1,405 records**.

| # | Value width | Keys present | Reading | Confidence |
|---|---|---|---|---|
| 0 | **12 B** (`vec3`) | 1,405/1,405 | **Camera position.** 14,703 keys, all finite; x −453…1,247, y −324…588, z −869…735 — world-scale coordinates, and the only `vec3` channel | ~~**[CONFIRMED — 12-byte width; HIGH CONFIDENCE — that it is position.]**~~ **[CONFIRMED — 12-byte width; CONFIRMED — disassembly (§10.5): the position fed to the camera global.]** |
| 1 | **8 B** | 1,405/1,405 | ~~**Orientation or aim — encoding UNRESOLVED.** See §5.1~~ **RESOLVED (§10.4): orientation quaternion, 4 × `s16` (x, y, z, w) × 16385/2^28, slerped** | ~~**[CONFIRMED — 8-byte width; OPEN — encoding.]**~~ **[CONFIRMED — 8-byte width; CONFIRMED — disassembly + empirical — encoding.]** |
| 2 | **4 B** (float) | 1,405/1,405 | **Field of view, radians.** All 2,345 keys lie in (0, π); median 0.691 rad ≈ 39.6°, range 0.035…1.745 rad ≈ 2°…100°. 1,294 of 1,405 shots hold it on a single key (a static FOV) | ~~**[HIGH CONFIDENCE — inferred from range and the single-key majority.]**~~ **[CONFIRMED — disassembly (radians × 57.29578 → degrees, §10.5) + empirical.]** |
| 3–7 | **4 B** each | 1,009/1,405 | **A five-channel group, all-or-nothing** — §5.2 — **depth of field (§10.5): presence of channel 3 is the enable; channels 4–7 = the four components of the DOF pass's `Focal_params`; channel 3 = CoC scale + two/three-layer switch** | **[CONFIRMED — the grouping; ~~HYPOTHESIS — the meaning.~~ CONFIRMED — disassembly (§10.5); HIGH CONFIDENCE — that channels 4–7 are ordered depth breakpoints.]** |

Times are **always 4 bytes per key** (float seconds) on every channel. Channels are sampled independently: a channel may hold one key (a constant) or hundreds.

### 5.1 Channel 1 — what it is not

> **Superseded 2026-09-20 — see §10.4.** The verdict below ("not a quaternion") was wrong by a factor of two in the scale, not in kind: the sampler multiplies each `s16` by `16385/2^28` (≈ 1/16383), and dividing by 32767 gives exactly the 0.5000 median that the first paragraph reports. Kept, struck through, as the record of the wrong turn.

~~The obvious reading, a compressed quaternion as 4 × `s16 / 32767`, is **refuted**: only **0 of 31,898** keys have a norm within 10% of 1, and the median norm is exactly `0.5000`. The negative is meaningful because the same test **passes 30.2%** of the time when run against channel 0's bytes, which are known to be three floats — so the test had ample opportunity to produce a false positive and did not. **[CONFIRMED — empirical, controlled.]**~~

~~Read as two floats the values are mostly bounded in `[0, 2]` with a median of `0`, but a minority decode as implausible magnitudes, so that reading is not clean either. Eight bytes for an orientation alongside a `vec3` position is suggestive of a two-angle aim (yaw/pitch) or a packed direction, but nothing here settles it. **[OPEN.]** The cheapest way to close it is the consuming sampler, which this pass did not open.~~ **Resolved — §10.4.**

### 5.2 Channels 3–7 — the optional group

These five appear and disappear together: identical counts across all 1,405 records (absent in 396, single-key in 914, multi-key in 95). Their ranges are distance-like and escalating —

| # | median | range |
|---|---|---|
| 3 | 0.150 | −0.150 … 1.000 |
| 4 | 0.149 | −22.6 … 50.0 |
| 5 | 1.785 | −6.8 … 140.9 |
| 6 | 7.281 | −27.3 … 2,500.5 |
| 7 | 30.000 | 0.848 … 3,986.8 |

~~— with channel 3 normalized to roughly `[0, 1]` and channels 6–7 reaching world-scale distances. A **depth-of-field parameter set** (a strength plus near/focus/far distances) fits the shape, and its being optional per shot fits too. Stated as **[HYPOTHESIS]** — the magnitudes are consistent with it but nothing was read that consumes these fields.~~ **Resolved — §10.5: depth of field, confirmed through the consumer (value → global → render packet → shader constant). Channel 3 is a CoC scale / layer switch rather than a "strength"; channels 4–7 are the four `Focal_params` components, ascending on 99.27 % of keys.**

*Validated by data (desk review 2026-09-30): Team B reproduces the channel widths and key counts (channel 0 keys 29,406 = 2 × 14,703; FOV 4,690 = 2 × 2,345 in (0, π); optional group 792/1,828/190 = 2 × 396/914/95; `team-b/HANDOFF.md` §4.6), plus channel-1 unit norm 31,898/31,898 and DOF ordering 3,929/3,958 (§9.73). The desk review re-tied the key counts: 14,703 + 31,898 + 2,345 + 5 × 3,958 = 68,736 keys and 3 × 1,405 + 5 × 1,009 = 9,260 tracks (§10.7). The channel roles rest on the disassembly of §10.5.*

**Review status (2026-09-30): VALIDATED-BY-DATA (layout, widths, counts); roles NEEDS-EXE via §10.5 — desk review (not re-derived from the executable).**

## 6. What a reimplementation needs

1. Check `u16@0 == 4`; read `u16@2` as the shot count and `u32@4` as the record-array offset (reject `-1`).
2. Walk `count` records of `0x68` bytes; the array must end exactly at EOF.
3. Per record: float start and end time, then 8 channels of `{ count, values_offset, times_offset }`.
4. Per channel: `count` times of 4 bytes, and `count` values of the width in §5 (12 / 8 / 4 / 4×5).
5. Sample per shot by time; shots are contiguous, so the active shot is found by a scan over the start/end pairs. **Full sampling rule, encodings and the clock: §10.3–§10.8.**

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 7. Cross-format notes

- **Stash-only constructors are now a recognised pattern**, seen at morph types 11/12, here, and at `.ctdg_pc` type 42 (`spec-conversation-format.md`), where following the stashed global again went straight to the parser. When a registered constructor's body is a handful of instructions, the format is parsed on demand elsewhere and the trail runs through whatever global it writes.
- Nothing shared: no material block, no Mesh or Morph sub-block, no `ANIM` magic. Cutscene camera data does **not** reuse `.anim_pc`, despite the registered constructor sitting a few hundred bytes away from one of the two `.anim_pc` constructors in the binary — adjacency in the image is not kinship. **[CONFIRMED — empirical + disassembly.]**
- The 30 fps authoring step matches nothing yet measured in `.anim_pc`, whose keyframe payload remains undecoded **[resolved since: `spec-anim-format.md` §6c]**; if that payload is ever opened, this is a known-good frame rate to test against.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 8. Methodology notes worth keeping

1. **A registration row gets you a constructor, not necessarily a parser.** Here the constructor was 22 bytes and called nothing. The productive move was to treat the global it writes as the next link and enumerate that global's readers — one of which was the real entry point. Cost: one extra Ghidra pass.
2. **A negative needs its chance rate too.** The quaternion test returning 0/31,898 only means something because the control established that the same test fires on 30.2% of known-non-quaternion bytes. Without the control, "0%" and "the test never fires on anything" are indistinguishable.
3. **Derive element widths from the data's own boundaries** when the parser only fixes up pointers. Packed arrays make each array's start the previous one's end, so the widths fall out arithmetically — and unanimity across 1,405 records is the check that the derivation is sound.

## 9. Open Items

1. ~~Channel 1's 8-byte encoding (§5.1) — the one real gap in an otherwise complete format.~~ **RESOLVED 2026-09-20, §10.4: 4 × `s16` quaternion (x, y, z, w) × 16385/2^28, slerped; 31,898/31,898 keys unit-length within 2 LSB, controls fail.**
2. ~~Channels 3–7's meaning (§5.2); depth of field is a hypothesis only.~~ **RESOLVED, §10.5: depth of field, confirmed through the consumer chain. Residual (which of the four breakpoints is which) is §10.9 item 2.**
3. ~~What the engine does between the last keyframe and the last shot's end time (57/96 files have a gap).~~ **RESOLVED, §10.6.3: every channel holds its last key; hard cut to the next shot, or the clock stops on the last shot.**
4. ~~Whether `u16@2` has a hard upper bound (82 observed) — i.e. whether the one-live-script global implies a fixed shot budget.~~ **ADVANCED, §10.6.6: no fixed array or cap on the script side; the live bound is the XML-driven manager shot table. The population comparison with the XML shot count remains OPEN (§10.9 item 5).**
5. ~~The interpolation rule between keys (linear? spline?) — in the sampler, not in the file.~~ **RESOLVED, §10.3: linear (channels 0, 2–7) and shortest-arc slerp (channel 1); no tangents.**

**Open items arising from the sampler pass: §10.9.**

## 10. The sampler: per-frame evaluation of a shot (resolves §5.1, §5.2, §9)

**Resolves:** §5.1 (channel 1's encoding), §5.2 (channels 3–7), and §9 items 1, 2, 3 and 5; advances item 4. **Method:** the pass that §1 said had never been done — opening the consuming sampler. Everything below was read from the runtime code that evaluates a shot each frame, then checked against all 96 distinct shipped files with a controlled harness (`tools/harnesses/csc_sampler_validate.py`, output `tools/csc_sampler_validate_out.txt`, 34 checks, 0 unexpected). Dumps: `tools/csc_s1.txt` … `csc_s21.txt` (Ghidra headless, disposable copy `gp_csc1`); scripts `tools/scripts/CscSampler.java`, `CscStrings.java`, `CscInsnScan.java`, `run_csc.ps1`.

### 10.1 Headline results

- **Caveat on the component ORDER (added 2026-09-20 from Team B's independent reader):** the unit-norm test is permutation-invariant — all 24 component orders score identically — and a rotation-matrix probe is order-blind too, so the `(x, y, z, w)` order rests on the disassembly claim alone, not on any empirical check (the only data hint is weak: raw slot 3 is `>= 0` on 80.5% of keys vs 51–65% for the other slots, consistent with `w` being the last slot). Team B independently reproduced everything else here: 31,898/31,898 unit-norm within 2 LSB (max |norm−1| 1.69 LSB, median +3.19e-7), the wrong-scale controls failing, FOV 2,345/2,345 in (0, π), DOF ordering 3,929/3,958, absolute key times 9,260/9,260 (shot-relative control fails), and a decoder+sampler agreeing at 204,912 sample times.
- **Channel 1 is a rotation quaternion, 4 × signed 16-bit in `(x, y, z, w)` order, each multiplied by the double `16385 / 2^28` (= `2^-14 · (1 + 2^-14)`, ≈ 6.1038882e-5, i.e. 1.0 ≈ 16383).** The earlier "not a quaternion" verdict was wrong by a factor of two in the scale, not in kind: dividing by 32767 gives norm 0.5000 exactly, which is precisely the signature of a missing ×2. With the code's constant, **31,898 / 31,898 keys have unit length within 2 LSB** (0.999898 … 1.000103, median 1.0000003). **[CONFIRMED — disassembly + empirical, with controls, §10.7.]**
- **Interpolation is linear for channels 0 and 2–7 and shortest-arc spherical (slerp) for channel 1; the key search is a linear scan; both ends clamp (hold).** The time parameter is the shared `f = (t − T[i−1]) / (T[i] − T[i−1])` on that channel's own key list. There are no tangents, so no spline. **[CONFIRMED — disassembly.]**
- **Between the last key and the shot's end time nothing extrapolates or blends: every channel holds its last key value** until the clock reaches the shot's end (then a hard cut to the next shot, or the clock stops on the last shot). The same hold applies before the first key (never exercised by shipped data: the first key equals the shot start on 9,260 / 9,260 tracks). **[CONFIRMED — disassembly + empirical.]**
- **Channels 3–7 are depth-of-field controls**, no longer a hypothesis: the update copies them into a global block that the frame setup copies into the render packet, and the post pass named by the literal `rl_depth_of_field` binds them to its shader constants — channels 4, 5, 6, 7 → the four components of `Focal_params`, channel 3 → the pass's CoC-range constant and its 2-layer/3-layer switch. Presence of channel 3 is the enable flag (the reason the group is all-or-nothing). **[CONFIRMED — disassembly for the plumbing; HIGH CONFIDENCE — that the four values are ordered depth breakpoints; OPEN — which breakpoint is which, §10.9.]**
- **Key times are ABSOLUTE cutscene time, not shot-relative**: the clock the update reads is the running cutscene time, and every track's first key equals its shot's start (9,260 / 9,260). **[CONFIRMED — disassembly + empirical.]**

### 10.2 Where the sampler lives (evidence anchors)

Found from the named anchors, without a whole-binary search: the parser's output global (the parsed-script pointer, `0x013C8F80`, written by the fixup routine `0x00569290`) has **twelve references — ten reads in six functions, plus its two writers** — that reader list *is* the trail — and one of them, `0x00569640`, reads the shot array and calls three channel samplers.

| Role | Address |
|---|---|
| Parsed-script pointer (written by the fixup routine `0x00569290`; cleared by `0x00569500`, whose one caller is the cutscene-manager routine `0x0072a160`) | `0x013C8F80` |
| **Per-frame camera update — camera-mode 7's update slot** (row 7 of the 5-dword-per-row camera-mode table at `0x0111A1F0`; third dword = `0x00569640`) | `0x00569640` |
| Mode-7 enter (writes the identity "cutscene origin" basis + zero translation, pushes mode 7) / leave | `0x005695e0` / `0x00564910(7)` |
| Scalar channel sampler (FOV, DOF values) | `0x00751720` |
| `vec3` channel sampler (position) | `0x007518d0` |
| Quaternion channel sampler (unpacks the `s16` quad, slerps, emits a 3×3) | `0x00751b90`; slerp `0x00dccb10`; quaternion→matrix `0x00dcc8f0` (both shared, generic math — 15 and 21 call sites); 3×3→three 16-byte rows `0x004add90` |
| Next-shot lookahead (samples channel 0 of shot `idx+1` at the *end* time of the current shot; cached per shot by `0x00721e50`) | `0x00569520` |
| Cutscene clock object | `0x0153BA48` (update `0x0073cbb0`, seek-to-shot `0x0073c890`, shot-duration `0x0073c810`, current-shot index `0x00720760`) |
| Manager mirror of the script's start/duration (frames at 30 fps), total duration | `0x00732290` |
| Camera output globals | position `0x013C8750`; orientation rows `0x013C8790 / 0x013C87A0 / 0x013C87B0`; FOV (degrees) `0x013C87C4`; DOF block `0x013C8FD0`–`0x013C8FE4` |
| DOF: frame setup copies the block into the render packet (`+0x4F1` enable byte, `+0x4F4`…`+0x504`); pass class init (literal `rl_depth_of_field`) / pass body | `0x005e14a0` / `0x00481f40`, `0x00482230` |

The three samplers have **no other callers** (`0x00751720` ← `0x00569640` only; `0x007518d0` ← `0x00569640`, `0x00569520`; `0x00751b90` ← `0x00569640` only), and the parsed-script pointer's writers are exhaustive by cross-reference (one write of a pointer, one clear). Two disclosed uses of a bounded search: a literal-string lookup for the DOF machinery (`dof`, `focal`, …) and an instruction scan restricted to `0x480000`–`0x4A8000` for the effect object's field displacements, anchored on the effect class initialiser's address — it found the pass body. Neither is a whole-binary predicate search.

**[OPEN — desk review 2026-09-30: about thirty addresses here are disassembly-only, and only some of them can be checked against data; the clock object's field types (float or double, §10.6 item 1) are not given. To be settled against the executable (the reader list of `0x013C8F80`, row 7 of `0x0111A1F0`, the clock at `0x0153BA48`).]**

**Review status (2026-09-30): NEEDS-EXE: reader lists and table slots are disassembly-only (strides and adjacency checked by desk) — desk review (not re-derived from the executable).**

### 10.3 The sampling rule (a format fact, stated as a formula)

For one channel with `n` keys, times `T[0..n-1]`, values `V[0..n-1]`, evaluated at absolute cutscene time `t`:

1. `n = 0` → scalar `0.0`; `vec3` → the zero vector; quaternion → the 3×3 identity. (Never occurs on channels 0–2 in shipped data.)
2. `n = 1`, or `t ≤ T[0]` → `V[0]`.
3. `t ≥ T[n−1]` → `V[n−1]` (**hold; no extrapolation**).
4. Otherwise `i` = the first index with `T[i] > t` (a linear scan, unrolled by four; the result equals a binary search), so `1 ≤ i ≤ n−1`. `d = T[i] − T[i−1]`; if `d ≤ 0` → `V[i]`; else `f = (t − T[i−1]) / d` and the value is
   - scalars and `vec3` (componentwise): `(1 − f)·V[i−1] + f·V[i]`;
   - quaternion: shortest-arc slerp. With `c = q₀·q₁`: if `c < 0` negate `q₁` and `c`; if `1 − c > 10⁻⁷`, `θ = acos(c)` and the weights are `sin((1−f)θ)/sin θ` and `sin(fθ)/sin θ`; otherwise the weights are `1−f` and `f`. The result is `w₀q₀ + w₁q₁`, not renormalised.

A debug global at `0x015DFD41` is tested by all three samplers and, if non-zero, freezes every channel at `V[0]`. It has **no writer anywhere in the image** (three readers, zero writers by cross-reference), so it is a dormant switch; a reimplementation ignores it. **[CONFIRMED — disassembly of all three samplers; the exit conditions and the `f` formula read line by line.]**

**[OPEN — desk review 2026-09-30: the numeric type (float or double) of `T`, `V`, `f` and of the slerp threshold `10⁻⁷` is not stated. Team B's decoder, evaluated in double, agrees with its own sampler, but that check is circular for the formula. To be settled against the executable (operand widths in `0x00751720`, `0x007518d0`, `0x00751b90`, `0x00dccb10`).]**

**Review status (2026-09-30): NEEDS-EXE: operand widths and slerp-epsilon width; the rules themselves are self-consistent — desk review (not re-derived from the executable).**

### 10.4 Channel 1 — the encoding (resolves §5.1)

Element = 8 bytes = four little-endian `s16` `(a, b, c, d)`; the quaternion is `(x, y, z, w) = (a, b, c, d) · 16385/2^28`. The scale constant is a stored double (`0x3F10004000000000`), not a division by 32767 or 16384. The quaternion→matrix routine then builds the row-major 3×3

`M = [[1−2(y²+z²), 2(xy−wz), 2(xz+wy)], [2(xy+wz), 1−2(x²+z²), 2(yz−wx)], [2(xz−wy), 2(yz+wx), 1−2(x²+y²)]]`

(the routine negates `x, y, z` on entry, which is why the stored rows are those of the standard rotation matrix of the file's quaternion, written row-major). The update multiplies `M` by the cutscene-origin basis `O` (identity — its only writer is the mode-7 enter routine, which stores the identity and a zero translation) to get the three orientation rows, then **normalises only the third row** to unit length *(desk review 2026-09-30: the first two rows are left as produced — not renormalised)*. Empirically the scale is pinned by more than "roughly unit": the median norm is 1.0000003, whereas `2^-14` exactly gives 0.99994 and `2/32767` gives 0.99997 — both fail the median test (§10.7). **[CONFIRMED — disassembly (unpack, order, scale, matrix, slerp); CONFIRMED — empirical (unit norm, scale pin, continuity).]** *Which row is "forward/up/right", and the handedness, are consumer-side conventions not decoded here — §10.9.*

**[OPEN — desk review 2026-09-30: the unit-norm and scale checks cannot separate component order from transposition. The norm is unchanged by any permutation of `(x, y, z, w)` and by negating `x, y, z` together, which is the conjugate quaternion and so the transpose of `M`. So both the `(x, y, z, w)` order and the statement that the stored rows are `M`'s rows and not `Mᵀ`'s (the "negates `x, y, z` on entry" reading) rest on the disassembly alone, and so does the handedness. §10.1's order caveat extends to the transpose. To be settled against the executable (`0x00751b90` unpack order, `0x00dcc8f0` entry negation, `0x004add90` row write). A data check could narrow it: align the orientation rows with channel-0 motion for all 24 orders × {`M`, `Mᵀ`} against a shuffled control.]**

**Review status (2026-09-30): NEEDS-EXE: component order and transposition/handedness; scale and unit norm are VALIDATED-BY-DATA (Team B §9.73: 31,898/31,898, median +3.19e-7) — desk review (not re-derived from the executable).**

### 10.5 Channels 0, 2 and 3–7 — meaning and outputs (resolves §5.2)

| # | Element | Blend | Output | Confidence |
|---|---|---|---|---|
| 0 | `vec3` | linear | Camera position → `0x013C8750` (`p·O + T`; `O`, `T` are identity/zero, so the stored position); when the manager-state test `0x00720640` (state ≥ 2 and a manager present) is true, position and rows are additionally mapped through a manager-held rigid anchor transform (`0x007241f0` for the point, `0x00723fa0` for the rows; record array at manager `+0x30`, stride `0x80`, selected by manager `+0x1680`) — not decoded further | **[CONFIRMED — disassembly.]** Upgrades §5's "HIGH CONFIDENCE — that it is position" |
| 1 | `4 × s16` | slerp | Orientation rows → `0x013C8790…` (§10.4) | **[CONFIRMED.]** |
| 2 | `f32` | linear | **Field of view: radians in the file, multiplied by `57.2957763671875` and stored in degrees** at `0x013C87C4` — the camera-wide FOV field that 34 sites read and write. Axis (horizontal vs vertical) not decoded | **[CONFIRMED — disassembly (radians→degrees) + empirical (all 2,345 keys in (0, π); 2.0°…100.0°, median 39.60°).]** |
| 3 | `f32` | linear | **DOF gate and CoC scale.** Only channel 3's *count* is tested: non-zero → the enable byte `0x013C8FD0` is set and channels 3–7 are all sampled (so 4–7 are assumed present); zero → enable byte cleared, 4–7 not read. Value → `0x013C8FE4` → render packet `+0x504` → the DOF pass's CoC-range constant, whose first component is `1 / (2·c₃ + 1)` (unless a per-view flag forces `c₃` to 0); and **`c₃ > 0` selects the three-layer variant of the pass and an extra blur pre-pass, `c₃ ≤ 0` the two-layer variant** | **[CONFIRMED — disassembly.]** |
| 4 | `f32` | linear | → `0x013C8FD4` → packet `+0x4F4` → **`Focal_params.x`** | **[CONFIRMED — disassembly; HIGH CONFIDENCE — that the four are depth breakpoints.]** |
| 5 | `f32` | linear | → `0x013C8FD8` → `+0x4F8` → `Focal_params.y` | same |
| 6 | `f32` | linear | → `0x013C8FDC` → `+0x4FC` → `Focal_params.z` | same |
| 7 | `f32` | linear | → `0x013C8FE0` → `+0x500` → `Focal_params.w` | same |

The names above are string literals in the image, quoted as evidence: the pass class initialiser registers one effect (`rl_depth_of_field`), two technique names (`ThreeLayer`, `TwoLayer`) and six constants (`Near_clip_params`, `Focal_params`, `Coc_range_params`, `Depth_sampling_offsets`, `Override_blur_percent`, `Depth_map_scale`); the pass body writes the four packet values `+0x4F4…+0x500` as the four components of the constant at handle slot `+0x144` (the `Focal_params` slot), `1/(2·packet[+0x504]+1)` into the `+0x148` slot (`Coc_range_params`), and picks the technique slot `+0x138` (`ThreeLayer`) when `packet[+0x504] > 0`, else `+0x13C` (`TwoLayer`). The packet fields are read at identical offsets by the writer (`0x005e14a0`) and the pass (`0x00482230`); that they are one structure handed across the frame boundary is inferred from that, not walked — **HIGH CONFIDENCE**.

Why "four ordered breakpoints": `ch4 ≤ ch5 ≤ ch6 ≤ ch7` holds on **3,929 / 3,958 keys (99.27 %)**, while the best of the other 23 orderings of those four channels holds on 0.8 %; the engine's own debug defaults for the same slots are `1, 3, 10, 12` (and `0.5` for channel 3's slot), ascending too. Which breakpoint is the near-blur edge, near-focus, far-focus or far-blur edge is a property of the shader, which is not in the executable — **OPEN**. The remap the update applies to channel 3 (`c₃ → 4·c₃ + 1` when `c₃ > 0`) is gated on a 3-byte function that always returns false, so it never runs on this build. Per-shot DOF also has an XML-side enable (a `DOFEnabled` element per shot, read by the cutscene-table reader `0x00736420`) and a global graphics-option gate in the pass; both are outside the file.

*Desk review 2026-09-30: `57.2957763671875` is exactly representable as a float and lies just below 180/π (57.29577951…). **[OPEN — the constant's address and type (float or double) are not given, and the DOF plumbing, the always-false 3-byte remap gate and the debug defaults are disassembly-only. To be settled against the executable (`0x00569640`, `0x005e14a0`, `0x00481f40`, `0x00482230`).]***

**Review status (2026-09-30): NEEDS-EXE: DOF plumbing and the degrees constant. The data backs the FOV range (2,345/2,345) and the DOF ordering (3,929/3,958, Team B §9.73). The shader-constant names are left as short functional identifiers — desk review (not re-derived from the executable).**

### 10.6 Timeline: clock, active shot, the tail gap, cuts (resolves §9 item 3, advances item 4)

1. **The clock.** The object at `0x0153BA48` holds `+0x00` running cutscene time `t`, `+0x04` total duration, `+0x10` current shot index, `+0x14` elapsed within the shot, `+0x18` the shot's duration, `+0x1C`/`+0x20` the consumed/requested step. The shot duration is `end − start` of that shot's record (float subtraction). Each frame the update advances `t`; while `dt + 0.001 ≤ (duration − elapsed)` it stays in the shot; otherwise it **consumes the shot's remaining time into `t`, resets elapsed to 0, increments the index and applies the leftover step to the next shot**. On the last shot (index ≥ the manager's shot count − 1) it **clamps**: `t` is set to the total duration and the clock stops. The seek routine sets `t` to the *sum of the durations* of the preceding shots. Because shots tile from 0 (§4), that sum equals the shot's start time, which is why the samplers can be handed `t` directly and the keys are absolute. **[CONFIRMED — disassembly.]**
2. **Which shot the camera uses.** `idx` = the clock's shot index while the manager state is 10–13, else 0; and if `idx ≥ u16@2` the update falls back to shot 0. Until the first shot-begin handler has run (which sets a manager bit the update reads) the update also uses shot 0. **[CONFIRMED — disassembly.]**
3. **The tail gap (item 3).** ~~*(Threshold note added 2026-09-20 from Team B's reader: the "more than one frame" tail counts quoted in this section — 927 shots / 39 of 96 files — reproduce with a threshold of about **0.0335 s**, not exactly 1/30 s (which gives 1,091 shots / 26 files); the median, 90th-percentile and maximum tail values match. Not re-derived by the orchestrator: the tail's exact definition (which channel's last key) matters and was not re-fixed.)*~~ **Threshold resolved 2026-09-23 (§11): the reproducing value is a harness constant — `TAIL_GAP_THRESHOLD = 0.0345` in `tools/harnesses/csc_sampler_validate.py`, whose printed line used to mislabel it "(1/30 s)" — not an engine one; the clock/shot-advance code's only literals are a fixed 0.001 s epsilon (`0x012a2d78`) and an exact 30.0 fps conversion (`0x012a2d58`), neither near 0.033–0.035 s.** Nothing special happens between the last key and the shot's end: every sampler returns its last value for `t ≥ T[n−1]` (§10.3 rule 3). The shot still runs to its end time; then the camera cuts to the next shot, or, for the last shot, the clock stops. This is common, not an edge case: **927 / 1,405 shots (66.0 %) have channel 0's last key more than 0.0345 s before the shot's end** (§11.5; control at an exact 1/30 s threshold: 1,091/1,405, 77.7 %); median tail 1.267 s, p90 4.10 s, max 81.23 s *[CONFLICT — desk review 2026-09-30: contradicts §11.4's "smallest gap above 0.033401 s is 3.98 s"; see the marker there]*; ~~only 34 % end within one 1/30 s frame~~ **34.0 % end within 0.0345 s of the shot's end — the exact-1/30 s control instead leaves 22.3 % within threshold, i.e. 1,091/1,405 (77.7 %) exceed it.** **[CONFIRMED — disassembly + empirical; threshold derivation in §11.]**
4. **Shots are hard cuts.** One frame evaluates exactly one shot record; there is no blend across a shot boundary in the update (the whole 733-byte function body was read, assembly and decompile; it touches channel data of shot `idx` only). The new shot's first keys equal its start time, so there is no pre-roll. Any cut smoothing downstream of the output globals (camera-mode blend code reads the same globals) was not examined — **OPEN**. **[CONFIRMED — disassembly for the update; OPEN — downstream.]**
5. **Lookahead.** `0x00569520` samples channel 0 of shot `idx + 1` at `t + (duration − elapsed)` — the current shot's end time, i.e. the next shot's opening position — and `0x00721e50` caches it per shot in the manager. Use of the value (streaming / prefetch is the plausible reading) **[HYPOTHESIS — the consumer was not followed.]**
6. **Shot budget (item 4, advanced).** The fixup loop runs `u16@2` times with no other cap, and the update falls back to shot 0 on an out-of-range index; nothing on the script side is a fixed-size array. The bound on the live shots is the **manager's shot table**, allocated from the cutscene XML table's `Shots` node (one `0x700`-byte row per shot; `0x00732290` copies script times only for `i <` that count, `0x0073cbb0` stops at count − 1), and the duration/seek routines read `script.shot[idx]` **without** a bounds check against `u16@2` — so the script must have at least as many shots as the XML. So the "one live script" global does not imply a per-shot budget. Whether shipped `u16@2` equals the XML shot count cannot be checked here: only entry 0 of the cutscene-table archive is trustworthy (the parked limitation), so no XML row is available to compare. **[⚠ Superseded 2026-09-20: the non-first-entry limitation is resolved (`spec-vpp-container.md` §7; `cutscene_tables.vpp_pc` 118/118), so this comparison is now actionable — not yet run.]** **[CONFIRMED — disassembly for the mechanism; OPEN — the population comparison.]**
7. **When the script is used.** The load driver (`0x00737290`) parses the script only when a scene-kind test (`0x00721bc0`) returns false, and then a missing or unparsable script aborts the cutscene load; when the test returns true the script pointer stays null, the mode-7 update immediately pops itself, and durations come from the XML frame counts instead. The XML-side durations are frames at 30 fps; from a script, `0x00732290` stores `int((end − start) · 30.0)` (truncated) per shot and the float sum as the total duration. ~~**[CONFIRMED — disassembly; the meaning of the kind test is not decoded.]**~~ **The kind test's meaning is RESOLVED, 2026-10-08 (`spec-lua-api-behaviour.md` §57.2a — read there first for the architecture this item sits inside):** `0x00721bc0` tests, via the cutscene manager's own `+0x0` pointer to the current scene-table entry, whether that entry's `kind` field (`+0x8`, the same field `spec-lua-api-behaviour.md` §26.25 already documents — Designer=0/Zscene=1/Story=2) equals **0 (Designer)**. Only Designer-kind cutscenes skip camera-script loading entirely; Zscene- and Story-kind cutscenes both require and load a `.csc_pc` through this exact driver. **HIGH CONFIDENCE** (the control-flow read is CONFIRMED by disassembly; the semantic identification of manager `+0x0` as "pointer to the scene-table entry" was not independently re-traced by its own write-site census). **Also newly found: the "cutscene load driver" (`0x00737290`) itself is not a standalone entry point — it has exactly one caller in the whole binary, `0x0072bff0`, which is itself one state handler of the unified 20-state cutscene state machine (`0x0072d660`) that `cutscene_play_do`'s own `0x00725df0` starts.** Both `0x0072bff0`'s and `0x00737290`'s full bodies are now dumped — see `spec-lua-api-behaviour.md` §57.2a and `scratchpad/cutscene_playback_sequencing_2026-10-08.md` for the complete call chain and decompiled logic.

**Review status (2026-09-30): NEEDS-EXE: the clock, seek, shot-count and scene-kind mechanisms are disassembly-only; item 3's statistics conflict with §11.4 (NEEDS-DATA there); item 6's XML comparison has not been run — desk review (not re-derived from the executable).**

### 10.7 Validation

Harness `tools/harnesses/csc_sampler_validate.py`: the decode **model** of §10.3–10.5 (formulas only) run over every shipped file, each claim that can fail on real data paired with a control that must fail. Corpus: **192 entries → 96 distinct files → 1,405 shots**, duplicates byte-identical, 96/96 exact-fit (structural integrity gate). **Excluded:** zero `.csc_pc` entries were unreadable; 4,263 *other* entries in mode-(a) compressed containers are unreadable by the parked non-first-entry limitation **[since resolved, `spec-vpp-container.md` §7]** and were not touched, so a `.csc_pc` nested inside one of them could not be enumerated (none is known; the population matches the previous pass exactly). **34 checks, 0 unexpected.**

| Claim | Result | Control (must fail) → result |
|---|---|---|
| Version 4, array ends at EOF | 96/96 | — |
| Shots tile (`start[k+1] = end[k]` within 2e-4 s; first = 0) | 1,309/1,309, first = 0 on 96/96; worst gap 1.07e-4 s (float32 rounding at ~200 s) | — |
| Multi-key time tracks strictly ascending | 1,999/1,999 | — |
| **Times are absolute**: first key = shot start | **9,260/9,260**; all 68,736 keys inside `[start, end]` | "shot-relative" (first key = 0) → 558/9,260 (6.0 %, only the t = 0 shots) |
| **Ch1 unit norm within 2 LSB** (the theoretical truncation bound `2·scale`) | **31,898/31,898**; 0.999898…1.000103 | scale 1/32767 → 0/31,898 (median 0.5000); 1/32768 → 0; ch0's bytes read as quaternions → 0/14,703; random `s16` quads → 0/31,898; misaligned by 2 bytes → 80 % (a *weak* control: adjacent keys are nearly equal, so a shifted read is nearly unit; alignment is fixed by the 8-byte stride, not by this test) |
| **Ch1 scale pinned** (median norm within 2e-6 of 1) | median − 1 = **+3.2e-7** | `2/32767`: 99.94 % within 2 LSB but median − 1 = −3.0e-5 → fails; `2^-14`: 96.7 % and −6.1e-5 → fails |
| Ch1 continuity (consecutive keys) | median **0.61°**, p99 5.46° | randomly paired keys → median 93.3° |
| Ch0 position continuity | median step 0.0726, p99 6.94 | randomly paired → median 48.2 |
| Ch2 FOV in (0, π) rad | 2,345/2,345; 2.0°…100.0°, median 39.60° | channels 3–7 read as FOV → 98.3 / 46.9 / 67.5 / 14.4 / 3.5 % in range (none 100 %) |
| **DOF `ch4 ≤ ch5 ≤ ch6 ≤ ch7`** | **3,929/3,958 (99.27 %)** | best of the 23 other orders → 0.8 % |
| Ch3 denominator `2·c₃+1` never singular | c₃ ∈ [−0.150, 1.000] → min 0.700 | — |
| Sampler-model self-tests (key time → key; hold after last / before first; slerp midpoint unit) | 48,946/48,946; 4,215/4,215 ×2; 786/786 | — |

Informational: `c₃ > 0` (three-layer variant) on 98.3 % of DOF keys; 1,009/1,405 shots (71.8 %) carry the DOF group; 150/3,958 DOF keys have a negative component (ch3 down to −0.15, ch4 down to −22.6 — authoring residue, never harmful: the CoC denominator stays ≥ 0.7). 7,261 of 9,260 non-empty tracks are single-key constants. *(Desk review 2026-09-30: the two 98.3 % figures — channel 3 read as FOV in range, and `c₃ > 0` — are the same fact, since `c₃ ≤ 1.0 < π`: a channel-3 key lies in (0, π) exactly when it is positive. Tie-outs re-checked: 48,946 = 14,703 + 31,898 + 2,345; 4,215 = 3 × 1,405; 68,736 keys; 9,260 tracks.)*

**Review status (2026-09-30): DESK-PASS, text fixes applied (the coincident 98.3 % figures explained). The main rows are reproduced by Team B (§4.6, §9.73) — desk review (not re-derived from the executable).**

### 10.8 What a reimplementation adds to §6

1. Decode ch1 as `(s16 × 4) · (16385/2^28)` in `x, y, z, w` order; build `M` (§10.4); expose rows `M·O` with the third row normalised.
2. Evaluate every channel with §10.3: linear scan, hold at both ends, lerp / slerp, on **absolute** time; one shot record per frame, chosen by the clock; a hard cut at the shot boundary.
3. FOV: multiply channel 2 by `57.2957763671875` (radians → degrees).
4. DOF present iff channel 3 has keys; then feed channels 4–7 as an ascending 4-tuple of depth breakpoints and channel 3 as `1/(2c₃+1)` (+ three-layer variant if `c₃ > 0`).
5. The last shot's clock clamps at the total duration (`Σ (end − start)`); the tail after the last key holds.

**Review status (2026-09-30): DESK-PASS (matches §10.3–§10.5; item 1's order and row convention carry §10.4's OPEN note) — desk review (not re-derived from the executable).**

### 10.9 Still open

1. **Axis and handedness of the orientation rows**, and whether FOV is horizontal or vertical — consumer conventions (the camera-basis reader), not decoded. *(Desk review 2026-09-30: component order and `M` vs `Mᵀ` are also disassembly-only — §10.4's OPEN note.)*
2. **Per-component roles of the four DOF breakpoints** and the exact meaning of channel 3 beyond "CoC scale + three-layer switch" — the shader is in the effects files, not the executable.
3. The **manager anchor transform** (`0x007241f0` / `0x00723fa0`) applied to position and rows when `0x00720640` is true — its record layout and when it is the identity in practice.
4. What consumes the **next-shot lookahead** value; whether any **downstream cut smoothing** exists.
5. Whether shipped `u16@2` equals the XML shot count (needs XML rows beyond the one trustworthy entry — parked limitation, not pursued). **[⚠ Superseded 2026-09-20: the non-first-entry limitation is resolved (`spec-vpp-container.md` §7; `cutscene_tables.vpp_pc` 118/118), so this comparison is now actionable — not yet run.]**
6. ~~The meaning of the scene-kind test `0x00721bc0` that decides whether a script is loaded at all.~~ **RESOLVED 2026-10-08 — see §10.6 item 7's update.**

### 10.10 Methodology notes

1. **A refuted decode with a suspiciously round failure statistic is a scale factor, not a refutation.** The earlier pass's median norm of exactly 0.5000 was the answer, read the wrong way: "not a quaternion" was concluded from a scale off by 2. When a statistic lands on a simple ratio, test the ratio before dropping the hypothesis.
2. **Element encodings live in the consumer, and so do scale constants.** A parser that only fixes up pointers cannot tell you the width *or* the scale; the sampler names both, and the scale was a stored double, not a power of two — only the median test (not the loose unit-norm test) can tell `2^-14` from `2^-14·(1+2^-14)`.
3. **Follow the reader list, not the predicate.** The parsed-script global had ten reads in six functions; one of them was the whole update. No whole-binary search was needed for the sampler; the DOF identity took one string lookup plus one bounded scan, disclosed above.
4. **Prove plumbing to a named consumer before naming a channel.** "Distance-like and escalating" (§5.2) was a good hypothesis; what made it a finding was a value → global → packet → shader-constant chain ending in a literal the executable spells out.

## 11. Tail-gap threshold, resolved (2026-09-23): a harness constant, not an engine one

### 11.1 The question

Section 10.6 item 3 ("the tail gap") reports that channel 0's last authored key is more than "one frame" before the shot's end in **927/1,405 shots**, and §4 reports the last shot's final keyframe matches the file's overall latest keyframe in only **39/96 files**. Team B's independent reader flagged both counts as threshold-sensitive: reproducing them needed a cutoff of about 0.0335 s, not the literal 1/30 s (0.03333 s) the text named, which instead gives 1,091/1,405 and 26/96. This section traces the constant to ground.

### 11.2 What the clock/shot-advance code actually contains

`0x0073cbb0` (the per-frame update), `0x0073c890` (seek) and `0x0073c810` (shot duration) were fully decompiled and disassembled (`tools/scripts/CscSampler.java`, `d:`/`a:` commands, against a disposable project copy). Their only float/double literals:

| Address | Value (read as literal bytes from the image) | Used for |
|---|---|---|
| `0x012a2d78` | **0.0010000000474974513** (i.e. `0.001f` promoted to `double`) | The per-frame rollover test in `0x0073cbb0`: `dt + 0.001 ≤ (duration − elapsed)` keeps the clock in the current shot (§10.6 item 1) |
| `0x012a2d58` | **30.0** (double) | Converts an XML `u16` frame count to/from seconds in `0x0073c890`/`0x0073c810` (`frames / 30.0`) — the only frame-rate literal in any of the three functions |
| `0x012a2d90` | **1000.0** (double) | Converts `t` (seconds) to milliseconds for a logging/telemetry call (`0x00731750`) inside `0x0073cbb0`; unrelated to any gap test |

None of these three constants is within an order of magnitude of 0.033–0.035 s, and none of the three functions ever compares an authored keyframe time against a shot's end time — that comparison exists only in this project's own analysis code, run once over already-parsed, static file data (no per-frame clock ticking involved).

### 11.3 Where 0.0345 s actually comes from

`tools/harnesses/csc_sampler_validate.py`'s "gap statistics (informational)" section has always classified a shot's tail using the literal **0.0345 s**; its printed line used to mislabel that cutoff "(1/30 s)", which is off by a real margin (1/30 s = 0.03333 s, ≈3.5 % smaller) — large enough to move the count on this corpus. That mislabel is exactly what an independent reproduction using the literal 1/30 s ran into. The harness is corrected in this pass: the constant is now named (`TAIL_GAP_THRESHOLD`), documented in-code as a harness-only classification choice (not an engine constant), and the printed line states the real value; a matching file-level statistic (§11.5) was added beside it.

### 11.4 Why 0.0335 s and 0.0345 s give identical counts

The sorted list of all 1,405 per-shot tail gaps (`shot end − channel 0's last key time`) has no value in the open interval **(0.033401 s, 0.066599 s]** — the smallest gap above 0.033401 s is 3.98 s (authoring jumps straight from "about one frame of tail" to "well over a second"). **[CONFLICT / OPEN — desk review 2026-09-30: this cannot agree with §10.6 item 3 / §11.5 (median tail 1.267 s, p90 4.10 s over the same 1,405 shots). If 478 shots have gaps ≤ 0.033401 s and the smallest of the other 927 is 3.98 s, then the median (the 703rd sorted value, one of the 927) would be at least 3.98 s, and the p90 would sit almost at the minimum. Either the 3.98 s figure or the percentiles are wrong, and so, possibly, is the "straight to well over a second" remark. The claim that (0.033401, 0.066599] holds no gap, and hence the 927 and 39/96 counts, is not itself contradicted. The figure is left as written until the sorted gap list is re-run: the ten smallest gaps above 0.0334 s, plus the percentiles. To be settled against real data.]** Any threshold inside that interval — 0.0335, 0.0345, 0.04, 0.05, … — therefore classifies the identical 927/1,405 shots and, by the per-file definition below, the identical 39/96 files. An exact 1/30 s (0.033333 s) threshold falls just outside (below) that interval and catches 164 more marginal shots, giving 1,091/1,405 and 26/96. Verified directly against the sorted gap list, not inferred.

### 11.5 The two statistics, restated precisely

- **Shot-level (§10.6 item 3).** Of the 1,405 shots, channel 0's (position) last authored key precedes the shot's end time by more than 0.0345 s in **927 shots (65.98 %)**; median tail 1.267 s, p90 4.10 s, max 81.23 s. Control at an exact 1/30 s threshold instead: **1,091/1,405 (77.65 %)**.
- **File-level (§4).** Taking each file's LAST shot only, and comparing its end time against the latest key of ANY of its 8 channels (not just channel 0 — channel 0 alone does not reproduce this count), the gap exceeds 0.0345 s in **57/96 files**, i.e. it is within 0.0345 s in **39/96 files** — §4's statistic. No file matches to within a tight 1e-3 s tolerance (0/96); "matches" in §4 has always meant "within about a frame," not bit-exact. Control at an exact 1/30 s threshold: 26/96 match, 70/96 exceed it.

Both were re-derived directly from `tools/harnesses/csc_sampler_validate.py` §F against the full 96-file, 1,405-shot corpus (34/34 checks pass, 0 unexpected); see the harness source (`TAIL_GAP_THRESHOLD` and the file-level loop beside it) for the exact computation.

### 11.6 What doesn't change

§10.3/§10.6 item 3's mechanism finding is untouched: nothing in the engine reacts to gap size. Every sampler holds its last value once `t` passes the last key (§10.3 rule 3), the shot still runs to its recorded end time regardless of how early the last key fell, and the camera then hard-cuts to the next shot (or stops the clock, on the last shot). The threshold discussed above is purely a description of how much authored tail is typical in the shipped data — an analysis-side classification, not a fact about engine behavior; the engine holds and cuts identically whether the tail is 0.001 s or 81 s. **[CONFIRMED — disassembly (`0x0073cbb0`, `0x0073c890`, `0x0073c810` fully decompiled and disassembled; `0x012a2d78`/`0x012a2d58`/`0x012a2d90` read as literal bytes from the image, not inferred) + empirical (`tools/harnesses/csc_sampler_validate.py`, full 96-file/1,405-shot corpus, 34/34 checks, 0 unexpected).]**

**Review status (2026-09-30): NEEDS-DATA: §11.4's 3.98 s smallest-gap statement conflicts with the median and p90 of §10.6.3 / §11.5; re-run the gap histogram. The constants (0.001 as float widened to double, 30.0, 1000.0) and the 927/1,091/39/26 arithmetic check out on desk — desk review (not re-derived from the executable).**

## In-game cutscene playback, end to end (2026-10-14)

**Scope.** From a cutscene request (`cutscene_play`, or the native mission-flow driver) to the last frame: how the scene file picks real-time versus Bink, what is inside a real-time scene and how its actors, clips, camera, soundtrack and subtitles share one clock, how skip and end work, and two test vectors. It builds on §10 (sampler), `spec-lua-api-behaviour.md` §26.25 (the 20-state machine) and §57.2a (start entry, destinations), and `spec-bink-boundary.md` §2–§5 (the Bink module).
**Method.** Disassembly with pefile + capstone (no Ghidra). Scripts: `tools/scratchpad/cs_lib.py`, `cs_q.py`, `cs_grep.py`, `cs_dd.py`. Data was read from the shipped archives: `cs_list.py`, `cs_census.py` (all 115 `.cte_xtbl` in `cutscene_tables.vpp_pc`), `cs_opts.py` (option and tag census), `cs_shotcheck.py` (shot count and length check over `cutscenes.vpp_pc`), `cs_audio.py` (soundtrack banks), `cs_subs.py` (subtitle keys) and `cs_vectors.py` (test vectors; output in `cs_out/vectors1.txt`). Game files were only read.
**Status key.** **FINAL** = disassembly-read and checked against data where data exists. **PARTIAL** = the mechanism is located, but one named piece is still not read.

### A. Headline results

- **The kind does not choose Bink.** The kind (`CutsceneType`, entry `+0x8`) chooses only between a camera script (Zscene/Story) and no camera script (Designer, §10.6 item 7). **A Bink movie is an extra layer on top of a normal real-time Story scene.** If the scene file has `BinkMovieFilename`, the movie is opened at load. It is then started and stopped by **per-shot options**: `Fullscreen Bink Play`, `Bink Play` and `Bink Stop`. 15 of the 115 scene files name a movie, and all 15 are Story-kind scenes that also have a camera script and actors. **FINAL.**
- **The "4 remaining handlers" item (`spec-bink-boundary.md` §6.4 / OPEN 4) is closed.** None of `0x0072c790`, `0x00720200`, `0x0072c260` or `0x0072c980` opens a movie.
  - **Where the movie is opened:** the state-7 handler `0x0072bff0` calls the subsystem start `0x007201a0` after a successful scene load. That calls `0x00732fc0(manager +0x3590, manager)`, then the Bink module's `0x00bddbb0`, then `0x00bdd6e0`. There the path is built as `sprintf("%s%s", "videos\", <movie>)`, and `0x00bdc3b0` calls `BinkOpen`.
  - **How the file name is built:** `<movie>` is the base name of `BinkMovieFilename`. The scene reader cuts the authoring path at the last `\` or `/` (`0x00da84e0`) and stores a heap copy in manager `+0x3590` (`0x0073700d`…`0x00737047`).
  - **FINAL.**
- **The soundtrack is one Wwise event, `Cinematic_CTE`, on a 2D game object named `"Cutscene"`.**
  - The scene's `Soundtrack` text `GROUP : VALUE` becomes a SetSwitch(group, value). A second switch, `Cinematic_Persona`, is set to the player's voice persona. The event is posted once, at playback start.
  - `VALUE` is also the soundbank id. The bank is `<value>.bnk_pc`, and its id is FNV-1 of the lower-cased name (3/3 banks checked).
  - **FINAL.**
- **There is one clock: the cutscene clock object `0x0153ba48` (§10.6).**
  - It advances by the measured, uncapped frame time `0x0132a0b0` (`spec-lua-api-behaviour.md` resolution at l. 13763).
  - The camera samples absolute time `t`. Actor clips and subtitle lines are started from the frame index within the current shot, `int(elapsed × 30)`.
  - The soundtrack is posted once and runs free. It is only re-seeked when the clock jumps (seek, skip ranges, §D).
  - **FINAL** for the code paths. Drift between the audio clock and the frame clock is not corrected anywhere that was read: **PARTIAL (no re-sync found)**.

### B. Item 1 — Dispatch

1. **Entry points (FINAL).** `0x00725df0` has exactly 4 callers:
   - `cutscene_play_do` through `0x00728870` (`0x007288a0`);
   - the state machine's own re-entries (`0x0072d7ca`, Zscene resident; `0x0072e05b`, the replicated skip/play command);
   - **the native mission-flow driver `0x006db140`**:
     - `0x006db68f`: a direct call once a fade requested with a 2000 ms argument (`0x005a0580`) has fully faded out, with the hashed name taken from the bound mission record `+4`;
     - `0x006db9c9`: a call through `0x00728870` with the name string at global `0x014c846c`.

   Both of the mission-flow driver's calls pass the destination block `0x014e94c0`. Mission scripts use the `game_lib.lua` wrapper (`zscene_is_loaded` wait, then `cutscene_play_do`; §26.25 item 4). Which mission-table field feeds `0x014c846c` was not read: **PARTIAL**.
2. **Scene table row (FINAL, data).** All fields come from `<name>.cte_xtbl`, which is in two places: `cutscene_tables.vpp_pc` (table build, §26.25 item 5) and inside the scene's own `cutscenes.vpp_pc` → `<name>.str2_pc` (the copy streamed with the scene, size-identical for `05_in`).
   - Census of the 115 files: Story 64, Zscene 31, Designer 20, including the test scenes `stress_test`, `test_pc_light`, `test_vehicle_light` and `cte_test`. `07_out` is not well-formed XML but reads as Story.
   - `CameraScript` is present in 94 of the 114 parsed files (every non-Designer file), `Soundtrack` in 88, `BinkMovieFilename` in 15, `VintDocument` in 15, `NextScene` in 1 (`17_out` → `17_out_pt2`) and `CTSFilename` in 20.
3. **Kind gate (FINAL, §10.6 item 7 and population).** Every non-Designer package in `cutscenes.vpp_pc` has a `<name>.csc_pc`, and the 19 Designer packages have none.
   - **The script's shot count equals the XML `Shot` count in 89/89 packages.** This closes §10.9 item 5 and the open comparison in §10.6 item 6.
   - **Script shot length = `DefinedShotLength + 1` frames in 1,270/1,275 shots.** So the clock's total for a scene is `Σ(DefinedShotLength + 1)/30` s.
4. **Bink layer (FINAL).**
   - **Open at load (state 7).** `0x00732fc0` sets up a 3 MB (`0x300000`) pool named `"cs_bink_pool"`. It fills a descriptor {file name, pool ×3, size/flag word, two zero bytes} and opens the movie through `0x00bddbb0` → `0x00bdd6e0`. The open flags are `0x204400`, with `|0x1000000` when the descriptor `+0x10` word is non-zero. The handle goes to manager `+0x219c`. The movie is opened, not started: slot `+0xd6` = 0.
   - **Shot-option dispatch.** Options are in the table at `0x01147fa0`. Each record is 20 bytes {name, handler, value type, …, `+0x10` "pre-fire" byte}, with 82 entries. The dispatcher `0x00738970(list, preFire, force)` calls the handler of every option whose pre-fire byte matches, or all of them when `force` is set. It runs:
     - at shot begin (`0x0072b3d0` → `0x0072b875`/`0x0072b886`);
     - one shot ahead for pre-fire options (`0x00721e00`, from the clock update);
     - at playback start (`0x00729396`);
     - for scene-level options at load (`0x0072c1a6`).
   - **`Bink Play`** (`0x00738150`, a float frame number). It does nothing if there is no handle. A frame < 0 pauses the movie (`BinkPause(h,1)` via `0x00bdcd70`). Otherwise:
     - if the movie is not yet started (`0x00bdcea0`), it applies a descriptor through `0x00bdcc00` (handle, mode 1, drawn in-scene) and then seeks to the frame (`0x00bdcee0`);
     - if it is already started, it seeks and then resumes (`BinkPause(h,0)` via `0x00bdcdc0`).
   - **`Fullscreen Bink Play`** (`0x00738260`, bool).
     - True: `0x00bdbd00(handle)` **pushes game state 7** (the Bink movie state, `0x00706e00(7)`, registered by `0x00bdc120`). It then applies a full-screen descriptor sized by `0x00bdc890` (1280×720 or 640×360 by aspect) or resumes the movie, and sets manager `+0x35d8` bit `0x20`.
     - False: `0x00bdbcc0(1)` pauses the movie and pops the state.
     - While game state 7 is on top, it still steps the cutscene machine (§26.25, item 3 correction), so the cutscene clock and the soundtrack keep running under the movie.
   - **`Bink Stop`** (`0x00738200`). With bit `0x20` set: `0x00bdc160` (stop and pop the full-screen state). Otherwise: `0x00bdcc40` (stop).
   - **Teardown** `0x00731da0` (from `0x0072a160`) stops the movie the same way. **Skip** stops it too (§E).
   - **Population:** `Fullscreen Bink Play` is in 6 files (`01_in`, `15_in`, `22_choice`, `22_out2`, `genki`, `stress_test`). In-scene `Bink Play` is in the other 9 (`02_out`, `09_out`, `13_out`, `14_in`, `20_in`, `22_out1`, `dk_sh01`, `k_z02`, `ms_sh02`).
   - **All 14 shipped cutscene movies in `videos\` have 0 Bink audio tracks** (header word at `+0x28`). Their sound comes from the Wwise soundtrack. `stress_test` names `02_IN_v2.bik`, which is not shipped.
5. **In-scene Bink drawing (PARTIAL).** The Vint renderer has a Bink-by-handle draw callback. `0x00848c25` stores `0x00842ff0`, a jump to the module's draw-by-handle `0x00bdcba0`, into `0x02317a68`. Scenes with an in-scene movie mostly carry a `VintDocument` (`cte_news`, `cte_crack_binoc` …), loaded by the same `0x007201a0` into manager `+0x3588`. How a Vint element gets the movie handle, and how `13_out` (no `VintDocument`; a 768×1024 phone-screen movie) maps it onto a surface, was not read.

### C. Item 2 — Real-time scene contents

1. **Files per cutscene (FINAL, data).** One package, `cutscenes.vpp_pc` → `<name>.str2_pc`, holds:
   - `<name>.cte_xtbl` (the scene file), `<name>.csc_pc` (camera, non-Designer only) and `<name>-lightset.xtbl`;
   - every clip `<name>_<actor>_<shot>.anim_pc`;
   - item meshes and rigs (`.ccmesh_pc`/`.gcmesh_pc`/`.rig_pc`), textures (`.cvbm_pc`/`.gvbm_pc`) and effects (`.cefct_pc`).

   The female-player package `<name>_f.str2_pc` has the same file list, but each player clip is replaced by an `f_`-prefixed clip where one exists. For example, `09_in_f` swaps `09_in_pc_N` for `f_09_in_pc_N`; `05_IN_f` has `f_05_in_pc_6` and `f_05_in_pc_8` but no `f_05_in_pc_5`.
   - Package choice: entry `+0x10` is chosen when the player's byte `+0xa41` = 1 (§26.25).
   - Character meshes are not in the package. They come from the entry's `+0x20` resource list (the `Characters/Mesh` and `Vehicles/Variant` names; §26.25).
   - Soundtrack banks are in `cutscene_sounds.vpp_pc` (story/zscene/companion scenes) or `sounds.vpp_pc` (e.g. `m22_cte`, `genki_tutorial`).
   - Movies are loose files in `videos\`.
2. **Actor declaration (FINAL).** The scene-level `Characters/Character` list is read by `0x007356d0` → `0x007354e0` into records of `0x208+` bytes:
   - `+0x0` name. For a Zscene, if a live world object of that name exists (registry `0x02442750`, `0x005e4dd0`), the name becomes `"<name>_cs"`.
   - `+0x100` mesh:
     - `Mesh`;
     - with `IsAngel`: `npc_Angel_clean` if mission `m21` is active, `Npc_Angel_withmask` if `sh04` is active, otherwise `npc_Angel_nomask`;
     - with `Killbane` and `0x00a1f9f0(2)`: the alternate mesh.
   - `+0x200` "is the player". This is set for `Player`/`#Player#`, and it also sets manager `+0x35d8` bit 8.
   - `+0x201` `SpeakingThug`, `+0x202` `AmCribProp`, `+0x204` `CribPropID`.
   - Non-player actors are spawned at load (from `0x00736ec0`, then `0x00735bf0`, then `0x00735940`/`0x007357c0`). The character type comes from the mesh through `0x00be3360`, falling back to `npc_cop`/`npc_TestCop`. The actor is created inside an object group tagged `"-- Cutscene Script Group --"` at the scene anchor (`0x007287b0`). Vehicles (`Variant`, `mesh`) are spawned by `0x00733930`.
   - **FINAL** for the read; the exact human-create call inside `0x00735940` was not followed (**PARTIAL**).
3. **Per-shot actor rows (FINAL).** `0x00736420` reads `Shots/Shot` into a manager shot table: manager `+0x34d8`, count `+0x34e0`, stride `0x700`. Shot fields:
   - `+0x654` character rows (count `+0x65c`, stride `0xb0`, read by `0x00735d60`), plus vehicle rows (`0x00734ef0`) and item rows (`0x00736100`);
   - `+0x678` options, `+0x684` audio lines (count `+0x68c`), `+0x6a0` effects (`CSEffects`, count `+0x6a8`);
   - `+0x6bc` the shot length in frames: the larger of `DefinedShotLength` and the animation span.

   A character row holds:
   - `+0x88` the name. `#PLAYER#` is stored as **`#PLAYER1#`**, which the generic resolver `0x00a281a0` maps to the local player's **real** human. So the player's own customised body, clothing and voice persona are what play in the cutscene; no stand-in is spawned.
   - `+0x50` `Pos` (a vec, normalised by `0x00da38e0`) and `+0x84` `OrientY` × π/180.
   - `+0x48` option bits from `0x007341b0`: `FatBonesEnabled`/`FatBonesLeft`/`FatBonesRight`/`FatBonesFeet`/`FatBones` (bits 0–4), `HideChar` (5), `DontReposition` (6), `HeelsDisable` (7); `RenderOverUI` goes in `+0x49`.
   - `ItemLeft`/`ItemRight` hand-item names built as `"%s_%s_%d"` (actor name, item text, number), `HiddenVids`, `ClothBones`, and `+0x78` the clip list (count `+0x80`).

   `HideChar` is applied at shot setup through the visibility call `0x008ccb90(object, hide, 1, 0)` (`0x00727fc3`).
4. **Clip lookup (FINAL).** `0x00732ce0` reads each `Animations/Animation` into a 28-byte record:
   - `Filename`: base name (`0x00da8890`), then `.animx` stripped (`0x00da8790`), then the engine CRC of the lower-cased name (`0x00d9e8b0`). So `D:/…/05_IN_BIKINI_1.animx` → clip `05_in_bikini_1`.
   - If the player is female (`+0xa41` = 1) or option byte `0x0153ba01` is set, it first tries `"f_%s"`. If the registry lookup `0x004bec10` returns −1, it falls back to the plain name.
   - The record stores the clip index (`+0x14`), `MaxWeight` (`+0x10`), `StartFrame`/`EndFrame`/`OverlapPrevious` (u16 fields in `+0x0`…`+0xc`) and a slot handle (`+0x18`).
   - **Population: 0 clips are missing from the package in the two vectors (§F).**
5. **Clip start (FINAL).** `0x00725050(actor row, object, frame)` runs:
   - at shot setup (`0x00727660` → `0x0072807b`, frame 0);
   - every frame (`0x00729fb0`, from the per-frame shot update `0x0072bce0`, with `frame = int(elapsed_in_shot × 30)`).

   For each clip record:
   - **Start:** once `frame ≥ start` and the record has not started, the object is resolved as a human (`0x004e43f0`), vehicle (`0x00b71950`) or item (`0x004e4bd0`). `0x00722b40` then plays the clip on the object's animation controller (`object +0xd24 → +0x10`) through the record-index play `0x004b1ef0` (`spec-anim-runtime.md` §6.5). The call is in clip-index mode, flags `0x80000 | 0xc40` (Zscene/Story) or `| 0x848` (Designer), with f1 = 1.0, f2 = `MaxWeight` and f3 = −1.0. The slot handle is kept in `+0x18`.
   - **End:** at the end of the window the slot is released through `0x004b41b0`.

   After setup, every actor gets one animation and post-animation pass (`0x009a15f0`, `0x009a02a0`), so it is posed on the first frame.
6. **Camera (FINAL, §10).** Camera mode 7 is entered at playback start (`0x005695e0`, from `0x00729150`). It samples the script at the clock's `t` every frame.
7. **The shared clock (FINAL).**
   - The playing handler (`0x0072c980`, states 10–12) calls the clock update `0x0073cbb0(dt)` with `dt` = the measured frame time `0x0132a0b0`.
   - The clock steps `t` and the in-shot elapsed time. At each shot boundary it calls the shot-begin `0x0072b3d0(idx)` (options, actor setup, clips at frame 0) and the per-frame shot update `0x0072bce0` (clips, effects, audio lines).
   - In-shot stepping can be replaced by a **range jump**. `0x00721f00` returns a skip-ahead step when byte `0x0153b554` is set and the current frame falls in one of the shot's frame ranges (shot `+0x6cc`, count `+0x6d4`). The soundtrack is then re-seeked to `(t + step) × 1000` ms (`0x00731750` → Wwise seek `0x0045ef20`). This is most likely the `Censor` option (type 7). That reading is **HIGH**; the range writer was not read.
   - Pausing the game in states 10/11 pauses the soundtrack (`0x00731580`) and resumes it on unpause (`0x00731560`) (`0x0072d6b6`…`0x0072d6e6`).

### D. Item 3 — Audio and subtitles

1. **Bank load (FINAL).** `0x007315a0(bank id, isZscene)` requests bank `entry +0xf4` (= FNV-1 of the `Soundtrack` value) into one of two pools: `0x0153b730` for Zscene, `0x0153b7e0` otherwise. The bank is requested at Zscene promotion (`0x00720410`) or in state 5 (`0x00722f10`). State 6 (`0x0072c790`) waits until the bank's status is `0x66` (loaded) or 5 s have passed (`0x00731630`). It also waits for the scene request group and every `+0x20` resource to be resident, with a 120 s timeout → abort (`0x0072bf90`). The state then becomes 7.
2. **Start (FINAL).** The first frame of the playing state (manager `+0x35da` bit 0 clear) calls `0x007316e0(entry +0xf0, entry +0xf4, isZscene)`:
   - SetSwitch(`+0xf0` = FNV-1(`CINEMATIC`) = `0x6d96bfb0`, `+0xf4` = FNV-1(value), object) via `0x0045f5b0`;
   - if there is a player, SetSwitch(FNV-1(`Cinematic_Persona`) = `0xe0fc12b9`, the persona id from `player +0x1c70` via `0x0070a290`, object);
   - PostEvent(FNV-1(`Cinematic_CTE`) = `0x86e8dbcf`, object, play id → `0x0153b720`) via `0x0045ea70`.

   The object `0x0153b710` is created as `"Cutscene"` (`0x00731520` → `0x0045da50`). The ids are hashed once by `0x007314f0`.

   Every soundtrack bank checked holds exactly one Event, `0x86e8dbcf`, and its own `CINEMATIC`/value/persona ids (`story_05_intro`, `story_09_intro`, `story_13_outro`). So **"which audio" is decided by the switch value inside a shared event, not by a per-scene event name.**
3. **Stop (FINAL).** Teardown `0x0072a160`:
   - stops the play id and unloads the bank (`0x007317a0`);
   - posts `Stop_Cinematics` (`0x9895c6ce`) on the global 2D voice (`0x0045d990`);
   - posts `UnMute_After_Zscene` for a Zscene.

   Skip also posts `Stop_Cinematics` (§E).
4. **Lines, lipsync and subtitles (FINAL).** Each `Audios/Audio` row is read by `0x007345b0` into a 36-byte record:
   - `+0x0` a Wwise id from `Filename`. A player line gets a persona prefix (`0x00731ae0` with `"player"`). Shipped `Filename`s are all `Audio Data N` (676/676).
   - `+0x4` `DelayBefore`/30 s, measured **from the start of the shot**.
   - `+0x8` the subtitle text: `0x00849df0(Subtitle)`, i.e. a key hashed with the localisation CRC into the `subtitle` table (`spec-localization-format.md`).
   - `+0xc` `SubtitleDuration`/30 s; `+0x10` the lipsync clip; `+0x14` `LipsyncCharIndex` (a 1-based shot actor index).

   Every frame, `0x00721070` does the following:
   - 0.1 s before `+0x4` it starts the lipsync clip on that actor (`0x004b1ef0`, flags `0x200040`);
   - at `+0x4` it resumes the line's voice handle `+0x20` (`0x0045f0d0`, the same call the soundtrack unpause path uses; where that handle is posted was not read) and shows the subtitle through `0x0073d070(text, 0, duration, 0)`.

   **Subtitle keys are persona-independent.** Player lines are keyed `…_Player_BM` (173 rows), and no `_WF`/`_WM`/`_HM`/`_BF`/`_ZM` variant exists in `subtitle_us`.

### E. Item 4 — Skip and end

1. **Skip input (FINAL; the input hook is the `0x005b2170` entry of `spec-player-runtime.md`, l. 466).** While a cutscene is in progress, `0x005b2170` consumes `CBA_SKIP_MODAL_SEQUENCE` (`0x8e`) and calls `0x00720830`. That function accepts the skip only if all of these hold:
   - the state is 10–13;
   - manager `+0x35d9` has neither `0x02` nor `0x40` set;
   - **more than 2000 ms have passed since playback start** (stamp `0x0153b548`, set in state 9);
   - with the completion-screen flag (manager `+0x35d8` bit `0x80`, set by the `Show Completion Screen` option `0x00738050`), **more than 0.6 s of the scene remain**.

   It then sets state 11 and a 5 s deadline (`0x0153b290 = t + 5.0`).
2. **Skip execution (FINAL, `0x0072d030`…`0x0072d32e`).**
   - **Plain scene (no tail hold, no completion screen):** `0x0072b340` stops it. Manager `+0x35da` bit 7 = "stopped from state 11" (i.e. skipped). A fade-out is requested if not already fading: Zscene 125 ms (`0x012f5d28`), otherwise a 0 ms request. The state then becomes 13.
   - **Scene with a tail hold (manager `+0x2190` > 0) or a completion screen:**
     1. Request a fade-out and wait until fully faded (`0x0059fa10`).
     2. Seek the clock to the last shot (`0x0073c890`), then step it until only the hold remains (`+0x2190`, or 0.6 s).
     3. Stop the movie (§B.4).
     4. Post `Stop_Cinematics`.
     5. Return to state 10, so the tail and the completion screen still play.

     Special cases by name: `01_in` on its first shot goes to state 12 instead and re-stamps the skip timer; `17_out` forces the completion flag.
3. **Normal end (FINAL).**
   - **Fade before the end:** with ≤ 0.125 s left, a Zscene requests a 125 ms fade-out. Other kinds request a fade (0 ms argument) with ≤ 0.8 s left.
   - **When the clock reaches the end** (`0x0073c6e0`), `0x0072b340` runs. If the completion flag is set, it waits first until HUD screens `0x35`–`0x39` are gone (`0x007bfad0`).
   - **Later states:**
     - **13:** `CS_STATE_STOP` and `0x0072a160` teardown, covering soundtrack, movie, camera script (`0x00569500`), option restore (`0x007382d0`) and the actor/visibility restores. Manager `+0x35cc` (`NextScene`) is copied to `0x0153b52c`.
     - **14→15:** either chain to the next scene (`0x00725670`, state 5) or tear down.
     - **0x10:** memory unlock.
     - **0x11:** `CS_STATE_STOPPED`.
     - **0x12:** `CS_STATE_FINAL_STREAMING`; world objects are re-streamed.
     - **0x13:** if a fade is pending (`0x0153b6b5`), a fade-in of 125 ms (`0x0153b6b4` set) or 500 ms; then `cutscene_virtual_pool` and the manager are freed and the state returns to 0 (§26.25 item 6).
4. **What is restored.**
   - **Player position (PARTIAL).**
     - At load, `0x0072bff0` teleports the local player to the scene anchor (`0x007287b0`) when the anchor is not the origin.
     - `RestorePlayerPos` takes a snapshot, and `cutscene_play` destinations are written into the manager. No read-back was found for either (`spec-lua-api-behaviour.md` §57.2a, unchanged).
     - On the skip-all path, the destination is applied at once.
   - **Visibility (FINAL):** per-shot `HideChar` on `#PLAYER#` hides the real player through `0x008ccb90`. Teardown re-runs the visibility pass.
   - **HUD (PARTIAL):** the load-time prep `0x00726980` (called first in state 7) makes the world, HUD and script-group changes. The individual HUD calls were not itemised.
   - **Streaming (FINAL):** state 4 mounts `preload_items`/`preload_effects`, and states 0x10–0x12 release them and re-stream (§26.25).

### F. Item 5 — Test vectors (from `cs_out/vectors1.txt`)

**F.1 `05_in`: real-time Story (mission 5 intro; `m05.lua` calls `cutscene_play("05_IN", nil, start_navs, false)`).**
- **Files:**
  - `cutscenes.vpp_pc` → `05_IN.str2_pc`: 224 files, mode flags `0x4803` — `05_in.cte_xtbl` (176,213 B), `05_in.csc_pc` (8,668 B), `05_in-lightset.xtbl`, 161 `.anim_pc`, 2 item meshes (`BeerBottle_Z`, `Binoculars`), 26 texture pairs and 2 effects.
  - Female package `05_IN_f.str2_pc` (224 files).
  - `VintDocument` `cte_binoc_only.vint_doc`; `SceneLightset` `05_in-Lightset.xtbl`.
  - Scene options `Time Of Day Set` = 500, `TOD Mission Override` = `m05_tod_override.xtbl`.
  - No movie.
- **Kind:** Story (2) → the camera script is required.
- **Actors:**
  - `#PLAYER#` = the real player, as `#PLAYER1#`.
  - 24 NPCs, each with a mesh: e.g. `BIKINI`=`npc_ped_stripper05_fw`, `CLARA`=`npc_mstar_female_sol08`, `CLAUDE`=`npc_ped_clubber04_ma`, `PETE02`=`npc_mstar_male_sol02`, `STRIP01`=`npc_ped_stripper01_fw`, `VIC2`=`npc_mstar_male_sol03`. Four `*_Body` doubles reuse their owner's mesh.
  - Vehicle `HELI02`=`heli_fighter_03`.
  - Items `BEER01`…`BEER11`=`BeerBottle_Z` and `BINOC`=`Binoculars`.
- **Shots** (`DefinedShotLength` frames → script seconds):

| Shot | Defined | Script start–end (s) |
|---|---|---|
| 1 | 137 | 0.0000–4.6000 |
| 2 | 74 | 4.6000–7.1000 |
| 3 | 114 | 7.1000–10.9333 |
| 4 | 80 | 10.9333–13.6333 |
| 5 | 111 | 13.6333–17.3667 |
| 6 | 77 | 17.3667–19.9667 |
| 7 | 112 | 19.9667–23.7333 |
| 8 | 82 | 23.7333–26.5000 |

  - **Duration 26.5000 s** (795 frames = Σ(Defined + 1)). The XML sum is 787.
- **Clips:**
  - Shot 1: every NPC and item plays `05_in_<actor>_1.anim_pc`, frames 0–137, weight 1.0 (e.g. `05_in_bikini_1`, `05_in_fillvic_body_1`, `05_in_beer01_1`). `#PLAYER#`, `HELI02` and `BINOC` are hidden.
  - `#PLAYER#` is hidden in shots 1–4 and 7, and plays `05_in_pc_5` (0–111), `05_in_pc_6` (0–77; female `f_05_in_pc_6`) and `05_in_pc_8` (0–82; female `f_05_in_pc_8`).
  - Shot 5 has no female clip, so a female player falls back to `05_in_pc_5`.
  - 0 clips are missing.
- **Audio:**
  - `Soundtrack` = `CINEMATIC : STORY_05_INTRO` → switch `0x6d96bfb0` = `0x32492709`.
  - Bank `story_05_intro.bnk_pc` (BKHD id `0x32492709`) + `story_05_intro_media.bnk_pc`.
  - Event `Cinematic_CTE` `0x86e8dbcf`, persona switch `0xe0fc12b9`.
- **Subtitles** (absolute time = shot start + `DelayBefore`/30):

| Time (s) | Key | Duration (s) | Text |
|---|---|---|---|
| 16.1333 | `Story05_In_01_Pierce` | 1.833 | "Whatchu thinkin?" |
| 17.8000 | `Story05_In_02_Player_BM` | 2.333 | "Some asshole is in my pool." |
| 20.0667 | `Story05_In_03_Shaundi` | 3.667 | "The crew's ready downstairs--say the word and we crash the party." |
| 24.4000 | `Story05_In_04_Player_BM` | 2.500 | "Let's do it." |

- **First camera key:** shot 1, t = 0.0.
  - Position (40.3248, 16.6612, −15.0739).
  - Channel 1 raw s16 (−2384, 6201, −406, 14971) → quaternion (−0.145517, 0.378502, −0.024782, 0.913813), |q| = 1.000054.
  - FOV 1.396300 rad = 80.0021°; no DOF.
  - Track sizes: 82 position keys, 32 orientation keys, 1 FOV key.
  - Data note: every actor `Pos` (3.8914, 3.2939, 16.3286) is the negative of the slate navpoint `slate_SaintsHQ_helipad` (−3.8914, −3.2939, −16.3286).

**F.2 `22_choice`: Story scene with a full-screen Bink (the mission-22 ending choice).**
- **Files:**
  - `22_choice.str2_pc`: 34 files — `22_choice.cte_xtbl`, `22_choice.csc_pc` (1,092 B), 1 clip, 15 texture pairs, 1 effect.
  - `22_choice_f.str2_pc` (34 files).
  - Movie `BinkMovieFilename` `…\videos\22_choice.bik` → opened as **`videos\22_choice.bik`** (44,264,080 B, 1,128 frames, 1280×720, 10,000,000/333,667 = 29.970 fps, 37.638 s, **0 audio tracks**).
- **Kind:** Story (2).
- **Actors:** `#PLAYER#` only. It plays `22_choice_pc_1.anim_pc`, frames 0–1128.
- **Shots:** 1 (`Shot_1-v_cam01_1`, Defined 1128).
  - Script: 1 shot, 0.0000–37.6333 s. **Duration 37.6333 s.**
  - The shot option `Fullscreen Bink Play` = True fires at shot begin. The movie therefore covers the whole scene in game state 7, while the clock, the player clip and the soundtrack run underneath.
- **Audio:**
  - `CINEMATIC : M22_CTE` → `0x6d96bfb0` = `0x5da8d7a5`.
  - Bank `m22_cte.bnk_pc` + `m22_cte_media.bnk_pc`. These are in `sounds.vpp_pc`, not `cutscene_sounds.vpp_pc`.
  - Event `0x86e8dbcf`.
- **Subtitles** (10 lines, shot-relative):

| Time (s) | Key | Duration (s) | Text |
|---|---|---|---|
| 0.800 | `22_Choice_01_Angel` | 2.133 | "He's at the airport - there isn't much time!" |
| 3.433 | `22_Choice_02_Player` | 0.500 | "Angel -" |
| … | … | … | … |
| 31.600 | `22_Choice_10_Kia` | 4.000 | "We'll see how many people believe that when Shaundi and Viola are found in the wreckage." |

- **First camera key:**
  - t = 0.0, position (0.2313, 4.3595, 7.1449).
  - Raw (0, 11584, −11584, 0) → quaternion (0, 0.707074, −0.707074, 0), |q| = 0.999954.
  - FOV 0.785400 rad = 45.0001°.
  - The camera is mostly hidden by the movie.

### G. Still open after this pass

1. How an in-scene movie reaches a Vint element or world surface (§B.5).
2. The mission-table field behind `0x014c846c` (§B.1).
3. The human-create call inside `0x00735940` (§C.2).
4. A HUD item list for `0x00726980` (§E.4).
5. Who sets `0x0153b554` and fills the shot range list (§C.7).
6. The destination and `RestorePlayerPos` read-back (§57.2a).
7. Whether anything re-syncs the free-running soundtrack or a movie to the clock after a frame-time hitch.

### H. Team B wiring

1. **Load the scene from its package.** Read `<name>.cte_xtbl` and `<name>.csc_pc` from `cutscenes.vpp_pc` → `<name>.str2_pc`. Take `<name>_f.str2_pc` when the player is female. The clock length is `Σ(DefinedShotLength + 1)/30`, or the script's shot times. Do not use the XML sum.
2. **Spawn the actors.**
   - Spawn one NPC per scene-level `Character` from `Mesh` (Angel/Killbane rules in §C.2), named exactly as in the scene file.
   - Map `#PLAYER#` to the **live player**. Do not spawn a copy.
   - Spawn vehicles from `Variant`/`mesh` and items from `Mesh`.
3. **Start clips per shot.** At each shot begin, and on every frame with `frame = int(elapsed_in_shot × 30)`, start a row's clip once `frame ≥ StartFrame`:
   - Clip name: `basename(Filename)` without `.animx`, lower-cased. Try `f_`+name first for a female player, and fall back to the plain name.
   - Play at weight `MaxWeight` and release at the end of the window.
   - Apply `HideChar`, `Pos`/`OrientY` at shot setup.
4. **Drive one clock.** Step `t` by the real frame time. Sample the camera at `t` (§10). Run shot begin and the per-frame shot update from the same step.
5. **Soundtrack.**
   - Load bank `lower(value).bnk_pc` (id FNV-1(value)). Wait for it (≤ 5 s) before playback.
   - At the first playing frame, SetSwitch(FNV-1(left), FNV-1(right)) and SetSwitch(`Cinematic_Persona`, player persona) on a 2D object `"Cutscene"`, then PostEvent `Cinematic_CTE`.
   - Seek it only when the clock jumps. Post `Stop_Cinematics` and unload at the end.
6. **Subtitles and lipsync.** For each `Audios/Audio` row, show `subtitle[Subtitle]` at shot start + `DelayBefore`/30 for `SubtitleDuration`/30. Start the lipsync clip 0.1 s earlier.
7. **Bink.**
   - Open `videos\<basename(BinkMovieFilename)>` at load, paused (the movie has no audio).
   - `Fullscreen Bink Play` = true: push a full-screen movie layer and keep the cutscene machine ticking. False: pause the movie and pop the layer.
   - `Bink Play` = f: in-scene play from frame f; f < 0 pauses.
   - `Bink Stop`, skip and teardown: stop the movie.
   - The `bink_play` Lua native stays a no-op (§24.2).
8. **Skip.**
   - Accept `CBA_SKIP_MODAL_SEQUENCE` only in playing states, ≥ 2 s after playback start, and (with a completion screen) with > 0.6 s left.
   - Plain scene: fade out, then stop.
   - Tail hold or completion screen: fade out, jump to the last shot leaving the hold, stop the movie, post `Stop_Cinematics`, then play the tail.
   - Set the skipped bit for `cutscene_was_skipped`.
9. **End.**
   - Fade out near the end: 125 ms for a Zscene; a fade request at 0.8 s left for other kinds.
   - Tear down: soundtrack, movie, camera mode 7, option restores, visibility.
   - Chain `NextScene` if present; otherwise re-stream and fade in (500 ms; 125 ms after a quick fade).
   - Move the player only where a destination was given (§57.2a), plus the load-time anchor teleport.
10. **Replace the playtest's auto-skip** (`sr3_viewer.cpp:19373`) with steps 1–9. Use F.1 and F.2 as the acceptance vectors: duration, first camera key, clip names, subtitle times and texts, audio ids.

**Review status (2026-10-14): new section, single-pass (pefile + capstone + shipped data). Not desk-reviewed.**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the "only entry 0 is readable" limitation superseded in §10.6 item 6, §10.7 and §10.9 item 5 (`spec-vpp-container.md` §7); marked §7's "keyframe payload remains undecoded" resolved (`spec-anim-format.md` §6c).
- 2026-09-30 (adversarial desk review, `review/adv_cutscene.md`): review status lines on 15 units, plus a summary after the front matter. Scope line's "12-byte header" corrected to 8 (per §3). CONFLICT/OPEN at §11.4: 3.98 s vs the median 1.267 s / p90 4.10 s, with a pointer from §10.6 item 3; the figure is not changed. OPEN at §10.4: the unit-norm checks cannot separate component order or transposition, so the handedness rests on disassembly. OPEN notes added for the duplicate-entry location, data-region array order, operand widths, the degrees constant and the §10.2 anchors. Team B validation notes added on §3–§5. Shader-constant names left as is (short identifiers). No label raised.

- **2026-10-08 — §10.6 item 7 / §10.9 item 6 RESOLVED: the scene-kind test's meaning, and the "cutscene
  load driver"'s own triggering logic.** `0x00721bc0` tests whether the current cutscene's kind is
  Designer (0); only Designer-kind cutscenes skip `.csc_pc` loading, Zscene/Story kinds both require one.
  **New finding: `0x00737290` (the load driver) is not a standalone entry point — it is reached from
  exactly one caller in the whole binary, which is itself one state handler of the SAME unified 20-state
  cutscene state machine that the Lua-native `cutscene_play_do` starts** (full architecture, including
  both functions' complete decompiled bodies: `spec-lua-api-behaviour.md` new §57.2a). This resolves this
  file's own "parser not reachable from its registration row" framing (§1) one level further: the parser
  IS reachable, just through the state machine rather than the registration row directly. Full detail:
  `scratchpad/cutscene_playback_sequencing_2026-10-08.md`. Cleanroom self-check: 0 new hits.

- **2026-10-14 — new section "In-game cutscene playback, end to end".** Dispatch from `cutscene_play` and the native mission-flow driver; the cutscene-to-Bink chain closed (state-7 handler `0x0072bff0` → `0x007201a0` → `0x00732fc0` → `0x00bddbb0` → `0x00bdd6e0` → `BinkOpen` on `videos\<basename(BinkMovieFilename)>`, started/stopped by the `Bink Play`/`Fullscreen Bink Play`/`Bink Stop` shot options); actor declaration, `#PLAYER#` → `#PLAYER1#` live-player binding, clip lookup (`f_` female variant), clip start through `0x004b1ef0`; soundtrack = `Cinematic_CTE` with `CINEMATIC`/`Cinematic_Persona` switches; subtitles from the `subtitle` table at `DelayBefore`/30; skip and end; test vectors `05_in` and `22_choice`; Team B wiring. Also closes §10.9 item 5 (script shot count = XML shot count 89/89; script shot = `DefinedShotLength` + 1 frames, 1,270/1,275). pefile + capstone only; no Ghidra. Cleanroom self-check: 0 hits.
