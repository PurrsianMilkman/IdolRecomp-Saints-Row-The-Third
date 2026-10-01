# Saints Row: The Third — `.asm_pc` Manifest Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, Target #4 (from `spec-output.md` §4 (unpublished Team A working document, not in this repository))
**Scope:** The structure of `.asm_pc` manifest files found as a sibling entry inside most `.vpp_pc` archives.
**Method (updated 2026-09-20, agent Y):** §1–§5 below are the original **black-box** pass and are **superseded where struck through**; §6–§10 are **disassembly-derived** (the PC executable's manifest parser, per-record reader and per-entry reader, plus the DX11 executable's twin) and **population-validated over all 805 shipped `.asm_pc` files** with an exact-consumption gate (`tools/harnesses/asm_manifest_parse.py`). *Original method line, kept for the record:* Black-box extraction and byte-level inspection of 4 real `.asm_pc` files pulled from `effects.vpp_pc`, `decals.vpp_pc`, `sr3_city_missions.vpp_pc`, and `vehicles.vpp_pc`, using the already-confirmed container format. Every `.asm_pc` entry sampled is stored **raw/uncompressed** (directory entry `+0x10` reads the `0xFFFFFFFF` sentinel in all 4 cases — see `spec-vpp-container.md` §3.1), so none of the container format's known compressed-entry limitations apply here; extraction was fully reliable. ~~No disassembly was needed or done this pass — the format was straightforward enough to resolve entirely from real file bytes, as anticipated.~~ **Retracted 2026-09-20: the 4-sample black-box reading mis-aligned the per-record header, the count width, the per-entry size fields and the trailer — see §10. Disassembly was needed.**
**Cleanroom compliance:** No decompiled code or internal identifiers appear below *(2026-09-20: §6–§10 additionally cite function addresses and the engine's own registered data-string names as evidence anchors, as the other post-disassembly specs do; still no decompiled code)*. The container magic number is an exact literal (load-bearing format data); every human-readable string quoted below is ordinary shipped game *data* (category/asset names, not source code or internal debug identifiers), so quoting them is describing content, not leaking implementation detail.

**Confidence key** (same as prior specs): **CONFIRMED — empirical**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30)** — adversarial desk review (`review/adv_asm.md`), 18 units (§1–§5 as one legacy unit, §6.1, §6.2, §7.1–§7.5, §8.1–§8.3, §9.1–§9.5, §10.1, §10.2): **DESK-PASS 5** (§7.5, §9.1, §9.5, §10.1, §10.2); **DESK-PASS, text fixes applied 2** (§1–§5 legacy banner, §8.1); **NEEDS-EXE 6** (§6.1, §6.2, §7.3, §7.4, §8.2, §8.3 — §7.3/§7.4 layouts are themselves validated by data, only named behaviours are open; §8.2 also had a count fix); **NEEDS-DATA 1** (§9.3 — also text fixes: the container-kind vs resource-type naming note and the `header_region_size` arithmetic); **VALIDATED-BY-DATA 4** (§7.1, §7.2, §9.2, §9.4 — Team B's independent reader, `team-b/HANDOFF.md` §9.71: 805/805 exact-EOF, 8,362 records, 390,134 entries, record count == records parsed 805/805, `header_region_size`/`payload_length` == sibling 7,990/7,990 each). A desk pass alone does not clear a unit: it means the text is internally consistent and agrees with the other specs, not that it was re-derived from the executable or data; only the VALIDATED-BY-DATA units are already backed by Team B's full-population run. **Awaiting the executable:** §6.1 (untraced loader callers and destub-callback invoker), §6.2 (version 8–10 branches; no later magic compare), §7.3 (negative `entry_count`; whether `extra` is consumed on every path), §7.4 (`variant_select` store), §8.1 (`file_id` 255), §8.2 (42 registered kinds vs 41 table-3 entries), §8.3 (destub callback invoker). **Awaiting real data:** §9.3 (container kinds 29/30 counted 1,194/1,686 here vs 597/843 in `spec-world-streaming.md` §10.2 over the same 805 manifests).

---

## 1. Overview

**[Desk review 2026-09-30: §1–§5 are the historical black-box pass, kept for the record; some of their tables still read as if live. Implement only from §7.1–§7.4 (byte layout) and §8 (what the tables mean).]**

`.asm_pc` is a small, simple, fully-uncompressed binary format — genuinely the easiest of the formats looked at so far. Every sample is built from the same two ingredients, in sequence:

1. **Three fixed, engine-wide lookup tables** — byte-for-byte identical across every one of the 4 samples checked, regardless of which archive they came from. These are name↔ID tables for shared taxonomies (memory pool categories, asset/container type names, and a third grouping — see §3).
2. **One archive-specific table** describing each named sub-container this particular archive holds, cross-referencing the entries against a related file and carrying that related file's compressed/uncompressed sizes (see §4) — this is the part that's actually specific to the archive the `.asm_pc` ships alongside.

## 2. File Header

| Offset | Size | Field | Confidence |
|---|---|---|---|
| `0x00` | 4 | **Magic number, exact value `0xBEEFFEED`** (on-disk bytes `ED FE EF BE`) — a deliberate, human-readable hex-speak constant. **Addendum 2026-09-20: the PC and DX11 executables' parser never compares it** (§6.2); 805/805 shipped files carry it. | **CONFIRMED — empirical, 4/4 samples.** *Exact literal required by the format.* *[Desk review 2026-09-30: present in every shipped file but not enforced by the loader — §6.2, §10.1.]* |
| `0x04` | 2 | **Format version.** Every sample read `11`. | **CONFIRMED — empirical, 4/4 samples, all identical; only one version observed.** |
| `0x06` | 2 | ~~**Record count for the archive-specific table (§4)**, i.e. a forward declaration of how many entries that later table holds.~~ **Corrected: it IS the number of container records the parser loops over (§7.1) — an EOF-driven walk finds exactly that many records in 805/805 files (§9). CONFIRMED — disassembly + empirical.** | **HIGH CONFIDENCE — inferred.** Confirmed to equal the number of records actually found and successfully parsed in §4's table for the two smaller samples checked in full (`decals.vpp_pc`: field reads `14`, and the archive's own directory separately lists exactly 14 `.str2_pc` sibling entries; `sr3_city_missions.vpp_pc`: field reads `15`, matching its 15 `.str2_pc` siblings exactly). ~~For the two larger samples this field reads much larger numbers (`249`, `372`) that don't correspond to top-level directory-entry counts in those archives (which have far fewer top-level entries) — consistent with this being a count of **named sub-groupings described within the manifest itself**, which can be more numerous than the archive's own top-level directory when one `.str2_pc` sibling bundles many named sub-items, rather than literally "number of sibling files."~~ **The premise was wrong:** `vehicles.vpp_pc` has exactly 372 top-level `.str2_pc` (372 = its record count); `249` belongs to `effects.vpp_pc`, whose only entry is the manifest itself (its 249 records are effect containers with no top-level `.str2_pc`; §9.4). There is no anomaly. |

## 3. The Three Fixed Lookup Tables

Each table shares one common record shape and follows immediately after the previous one, with no gap or padding:

```
u32 entry_count
repeated entry_count times:
    u16 name_length
    char name[name_length]      (no null terminator)
    u8  id
```

IDs are **not necessarily sequential or contiguous** — real gaps were observed (e.g. one table skips from `26` to `28`, and separately jumps to a standalone `254` at the very end) — consistent with a stable, hand-maintained enumeration that doesn't get renumbered when an entry is removed during development, rather than a simple positional index. **[CONFIRMED — empirical.]**

The three tables observed, identical byte-for-byte in every sample checked:

1. **39 entries** (e.g. `Vehicle slots`, `Character slots`, `Player slots`, `item preload`, `cutscene`, `decal mempool`, `mip_streaming`, ending with a distinct standalone entry at id `254`) — names read as **memory pool / allocation-budget categories** used by the engine's resource-streaming system.
2. **45 entries** (e.g. `Vehicles`, `Vehicle cvtf`, `Vehicle PEG`, `Vehicle VFX`, `Character mesh`, `Character morph`) — names read as **asset/content-type category names**, matching the kinds of sub-file formats already catalogued in the container-format work (`.cvbm_pc`/`cvtf`-style, `.cpeg_pc`/`.gpeg_pc`/PEG, VFX, mesh, morph data).
3. **41 entries** (e.g. `Vehicle`, `Vehicle Fully Customizable`, `Items Preload Mempool`, `Items`, `Items Preload`) — a third categorization, overlapping in theme with the second table but reading more like **top-level gameplay-object categories** (vehicle, item, character) than raw asset-type names.

**[CONFIRMED — empirical for existence, exact content, and record shape, 4/4 samples. ~~Semantic *purpose* of each table (the three one-line summaries above) is our best reading of the actual name lists, not independently verified against code — labeled HIGH CONFIDENCE, not CONFIRMED, for that interpretation specifically.~~ **Verified against code 2026-09-20 (§8): table 1 = memory-pool ("streaming allocator") names, table 2 = resource-type ("prim type") names — 43 of its 45 are exactly the registry of `spec-format-inventory.md` §2, the other two are the two `DESTUB` helper types — and table 3 = container-kind names. All three are per-parse id-translation lists, not global maps (§8). Table 3 is *not* "gameplay-object categories" in any deeper sense: it is the container-kind registry.]**

## 4. The Archive-Specific Table

Starts immediately after the third fixed table ends, and runs for exactly the record count declared at header offset `0x06` (§2). **Corrected substantially this pass** (mission-package investigation, `spec-mission-packages.md`): the original description below modeled each record as holding exactly one reference. Real multi-entry samples (from `sr3_city_missions.vpp_pc`) show this is actually a **repeating structure, one sub-block per entry in the referenced `.str2_pc` sibling** — the single-reference case documented originally was simply what a 1-entry sibling produces. The corrected shape **(itself superseded 2026-09-20 — do not implement from this block; see §7.3–§7.4. Its `u8 zero, u8 entry_count, u16 zero, u16 span_field, 7 zero bytes` are really `u8 container_kind, u16 record_flags, s16 entry_count, u32 header_region_size, …`, and its interleaved `(size, unknown, name, trailer)` sub-blocks are really a size table followed by the named entries)**:

```
u16  name_length
char name[name_length]                        (no null terminator, the sibling .str2_pc's own name)
u8   zero
u8   entry_count                              (matches the sibling .str2_pc's real entry count exactly)
u16  zero
u16  span_field                               (= 8 + 8 × entry_count; exact byte-level role not confirmed)
... 7 zero bytes ...
u32  referenced_container_total_compressed_size   (see note below — one value for the whole sibling, not per-entry)
repeated entry_count times, one sub-block per sibling entry, in the same order as that entry's own directory listing:
    u32  this_entry_uncompressed_size
    u32  unknown_field                         (reads 0 in every sample checked)
    u16  ref_name_length
    char ref_name[ref_name_length]             (no null terminator — this entry's own filename)
    ... a further ~12-13 byte trailer, partly understood (see below) ...
```

**The single aggregate size field is confirmed to be the referenced `.str2_pc` container's own header field `0x168`** (`spec-vpp-container.md` §1 — "total on-disk byte length of the payload region") **— not a per-entry compressed size.** This makes sense in hindsight: every real multi-entry sibling sampled so far is a mode-(b) shared-stream container (`spec-vpp-container.md` §3.2), where individual entries don't have independently meaningful compressed byte ranges — only the whole shared stream does. **[CONFIRMED — empirical, exact match on two independent real samples: `2,459` for a 2-entry sibling and `6,025` for a different 2-entry sibling, both matching that sibling's own header field `0x168` exactly.]** Each per-entry sub-block's own size field is that specific entry's **uncompressed** size (also cross-checked exactly against the sibling container's real per-entry directory data on both samples).

~~**The previously-found extra 4-byte field** (between the entry's uncompressed size and its `ref_name_length`, first found by the implementation team cross-validating against `decals.vpp_pc`) fits this corrected model directly — it's part of each per-entry sub-block, not a one-off addition to a single-reference record. It still reads `0` in every sample checked and its meaning is still **OPEN / UNKNOWN**.~~ **Resolved 2026-09-20: that `u32` is the entry's SECONDARY (`g`-side) size — the second half of the per-entry `(primary, secondary)` size-table pair (§7.3). It read 0 in the samples only because none of their entries was paired; 328,312 of the population's 390,134 entries carry a non-zero one. And the pair is not interleaved with the names — the whole table precedes all names (it only looks interleaved for `entry_count = 1`).**

**What's confirmed, with real cross-checks (now from two independent multi-entry samples, in addition to the original single-entry one):** the `name` field is the sibling `.str2_pc`'s own name (e.g. `decal_bullet_wood`, `Horde Mode`, `Running Man` — matching real top-level directory entries), `entry_count` matches that sibling's real entry count exactly (checked at 0, 1, and 2) ~~[true only for containers with no paired entries; a paired entry costs two directory entries — corrected 2026-09-20, §9.4]~~, and each `ref_name` names one of that sibling's own real entries in order (confirmed: not just `.matlib_pc` files as the original single-entry sample suggested — a `Horde Mode`/`Running Man`-style sibling's entries reference `.cefct_pc`/`.xtbl` files directly by their real names, so `.matlib_pc` is just one possible referenced extension among several, not the only one). **[CONFIRMED — empirical, cross-validated against independently-established container-format ground truth on 3 total samples now (1-entry, and two different 2-entry cases).]**

~~**What's not fully resolved:** the exact byte-level role of `span_field` (confirmed to equal `8 + 8×entry_count` on both multi-entry samples, but not tied to any other independently-confirmed quantity); the per-entry ~12-13 byte trailer after each `ref_name` (includes what looks like a repeat of that entry's own uncompressed size, plus at least one more small integer field); and whether the per-record header bytes preceding `entry_count` (the leading `u16`/`u8` constants) ever vary. **[OPEN / UNKNOWN — flagged rather than guessed further, since the parts that matter most for *finding and sizing every referenced file* are now solidly confirmed across multiple samples.]**~~ **All four resolved 2026-09-20 (§7, §10): `span_field` was a coincidence — the bytes it read are a `u32` equal to the sibling `.str2_pc`'s header-region size (`0x0800` for an empty container, `0x1800` for the common small container, …); "8 + 8×N" equals 24 only at N = 2, and `0x1800`'s middle byte is 24. The trailer is exactly 13 bytes. The header bytes vary (§9.3).**

## 5. Open Items

1. ~~**The exact byte layout of the per-record header's `span_field` and each entry's ~12-13 byte trailer** (§4) — the load-bearing fields (entry count, each entry's uncompressed size and name, the aggregate compressed size) are confirmed by position; these surrounding bytes' individual meanings are not.~~ **RESOLVED 2026-09-20 — §7. The live open-items list is §10.2.**
2. **The `.matlib_pc` format** referenced by some archive-specific record entries — since resolved (lightly) as an appendix to `spec-texture-format.md`: it's a material definition listing named texture-slot references (normal/diffuse/specular observed). §4's correction also clarifies that `.matlib_pc` is just one possible referenced extension, not the only one — `.cefct_pc` and `.xtbl` entries are referenced the same way.
3. **The zero-entry record shape — now fully resolved, was previously open.** ~~Confirmed exact byte layout (§4): `name` + a fixed 19-byte tail (`entry_count` = 0, `span_field` = 8, remaining bytes zero), with no per-entry sub-blocks at all.~~ **Re-confirmed and re-decoded 2026-09-20 (§7.3): the 19-byte tail is right (`1 + 2 + 2 + 4 + 2 + 4 + 4`), but its content is `container_kind`, `record_flags`, `entry_count = 0`, `header_region_size = 0x0800`, an empty `source_name`, `extra_len = 0` and `payload_length`; there is no `span_field`.** Directly explains the short-record shape the implementation team found on `sr3_city_missions.vpp_pc`'s empty siblings (`Trafficking` and 12 other activity-name entries in that same archive turned out to be genuinely empty `.str2_pc` containers — see `spec-mission-packages.md`) — it isn't a separate, mysterious record variant, it's this same record shape with `entry_count = 0`, which the corrected model now accounts for directly rather than needing a special case.

None of these gaps block the format's main practical use: given a `.vpp_pc`'s `.asm_pc` sibling, an implementation can already reliably enumerate every named sub-group in the archive, resolve each one's referenced material-library file by name, and know that file's exact compressed and uncompressed size before even opening it.

**Review status (2026-09-30): DESK-PASS, text fixes applied (implement-only-§7 banner; §2 magic row annotated) — desk review (not re-derived from the executable).**

## 6. The parser in the executable — found, decompiled, and what it does and does not check (added 2026-09-20, agent Y)

**Added 2026-09-20 (agent Y, `gp_asm1`).** Everything in §6–§10 comes from the disassembled PC executable (Ghidra project copy `tools/gp_asm1`, target already fully analysed), cross-read against the DX11 executable (`tools/gp_fxo_dx11`, opened `-readOnly`, nothing re-imported), and is then replayed over the whole shipped population (§9). Cleanroom: no decompiled code is reproduced; addresses are evidence anchors, and the diagnostics quoted are ordinary data strings. Labels as in the confidence key; stronger than the black-box §1–§5, which are struck through where they lose.

### 6.1 Call chain **[CONFIRMED — disassembly]**

Method: the entry reader `FUN_00dd2f50` (agent T, `spec-resource-dispatch.md` §8.5) was walked **up** — its sole caller is the per-record reader, whose sole caller is the manifest parser — with no predicate search. The literal `0xBEEFFEED` was used as a second anchor and **found nowhere** (§6.2).

| Function | Role | Evidence |
|---|---|---|
| **`FUN_00db4980`** | **The manifest parser**: reads the 8-byte header, the three tables (§8), then loops the per-record reader `record_count` times. | Body 713 bytes; sole caller `FUN_00db4c60`; contains the three "unrecognized … (%s) in prepackaged streaming data (%s)" diagnostics. |
| **`FUN_00db41a0`** | **Per-record reader** (one container record). Two modes selected by a context byte: *build* (registers a new container and one entry per file entry) and *verify* (finds the already-registered container of the same name and checks count/kind/entries against it). | Body 1,790 bytes; sole caller `FUN_00db4980`; calls the entry reader from two sites (`0x00db4437` build, `0x00db453f` verify). |
| `FUN_00dd2f50` | Per-entry reader — name, then exactly 13 bytes (§7.4). Build mode registers the entry (`FUN_00db48a0` → `FUN_00dd35e0`); verify mode compares type/pool/group/name and refreshes the two sizes. | T's function; re-read here field by field. |
| `FUN_00db4c60` | Manifest **loader**: given an open stream, parse it; given a file name, open it, read the *entire* file into a temporary arena, close it, re-open a memory stream over the buffer, then parse. | Callers: `FUN_006f70f0` (builds `<name>` + the literal `.asm_pc` at `0x006f724d`, arena string "tmp container mem", 0x20000 bytes), `FUN_0085a070` (literal at `0x0085a6df`), `FUN_006e2960`, `FUN_00856690`, and the stub-container callback `FUN_00daf770`. `FUN_006e2960` and `FUN_00856690` (and `FUN_0085a070` beyond its literal reference) were not traced further. |
| `FUN_00daf770` | The per-kind callback of the **"destub" container** (stored in the kind's row by `FUN_00db01a0`): given a 2-entry container whose entries have types `0xFE`/`0xFD`, it opens a memory stream over the first entry's bytes and parses it with `FUN_00db4c60`, in a 0x20000-byte arena named "stream2_request tmp". (The site that invokes the callback was not traced.) | `FUN_00db01a0` registers an allocator `GSA_DESTUB_ALLOCATOR` (`0xFE`), two resource types `GSP_DESTUB_PRIM` (`0xFE`, **primary extension `.asm_pc`**) and `GSP_DESTUB_PRIM_BUF` (`0xFD`), and the container kind `GSC_DESTUB_CONTAINER` (`0xFE`); the `.asm_pc` literal (`0x0113ddd0`) has exactly three references (`FUN_006f70f0`, `FUN_0085a070`, `FUN_00db01a0`). |
| `FUN_00daa5d0` `FUN_00daa8a0` `FUN_00daa8c0` `FUN_00daa900` `FUN_00daa940` `FUN_00daaa50` | Stream primitives: raw *n*-byte read; `u8`; **signed** 16; `u16`; `u32`; **length-prefixed string** (`u16` length, characters, no terminator; at most 63 characters kept, the rest of an over-long name is read and discarded; an exhausted stream yields an empty string). The 16/32-bit readers byte-swap when the stream is flagged big-endian (flag bit `0x04` of the stream's byte at `+0x168`). | Disassembled. |

**The DX11 executable holds the same code** at `FUN_00db8b40` (body 713 — same size as the PC parser), `FUN_00db8360` (body 1,790 — same size as the record reader) and entry reader `FUN_00dd7170`. (Located by address proximity to the DX11 copy of the container-creation function — the one diagnostic string that resolved by cross-reference there — then decompile-confirmed: same three diagnostics, same `version > 7` test, same body sizes.) **[CONFIRMED — disassembly for the parser and record reader; the DX11 entry reader `FUN_00dd7170` was identified only as the record reader's loop callee, not read.]**

**[OPEN — desk review 2026-09-30: the loader callers `FUN_006e2960` and `FUN_00856690` and the site that invokes the destub callback `FUN_00daf770` are untraced (not needed to parse the format); to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: untraced loader callers and destub-callback invoker; the roles and addresses are disassembly-only and nothing in the data can test them — desk review (not re-derived from the executable).**

### 6.2 What the parser validates — and what it does not **[CONFIRMED — disassembly]**

- **The magic `0xBEEFFEED` is never checked.** The 8-byte header is fetched with one raw read; the first four bytes are simply not compared. Corroborated two ways over the *whole* loaded image of both executables: **0** hits for the 32-bit immediate, **0** for the byte pattern `ED FE EF BE` in any section (a `CMP r/m32, imm32` would encode it), **0** for 16-bit `0xFEED`/`0xBEEF` immediates in the PC image. (So as far as any loader in this executable is concerned the literal is decorative — presumably a crunch-tool marker, HIGH CONFIDENCE. §2's "exact literal required by the format" is true of the *files* — 805/805 carry it — but nothing enforces it: a file with a different first dword would parse identically.) **[OPEN — desk review 2026-09-30: the scans rule out an immediate compare, not a later byte-wise compare of the stored 8-byte header copy; to be settled against the executable (header handling in `FUN_00db4980`).]**
- **The version is a set of threshold tests, not an equality check.** `version > 7` enables the three tables; `version < 9` reads one extra `u32` in each record and sizes the size-table skip differently; `version ≥ 10` adds the `source_name` string (§7.3); `version ≥ 11` makes the per-entry size table unconditional. Every shipped file is version **11**, so the branches below 11 are read from code and **never exercised by data** (HIGH CONFIDENCE for their width, no empirical check possible). **[OPEN — desk review 2026-09-30: where the extra `u32` sits for `version < 9` and how the size-table skip differs are not given, so versions 8–10 cannot be implemented from this text — implement version 11 only; to be settled against the executable (`FUN_00db4980`, `FUN_00db41a0`).]**
- **The tables are validated by name.** A name that the engine cannot find in the matching registry prints the corresponding "unrecognized streaming allocator / prim type / container type" diagnostic and aborts the whole parse (the message text says it means someone *"renamed … without recrunching data"*, i.e. a versioning safeguard, §8).
- **No end-of-file check.** The loop ends after `record_count` records; bytes after the last record would be ignored. §9's exact-consumption gate is therefore *stricter* than the engine's own acceptance test — and it still passes 805/805.
- A failure in any record aborts the loop (containers already registered stay registered).

**Review status (2026-09-30): NEEDS-EXE: branches for versions below 11 are not fully specified; no later magic compare to be confirmed — desk review (not re-derived from the executable).**

## 7. Corrected byte layout — version 11 (added 2026-09-20, agent Y)

**Supersedes §2's `0x06` row and §4's record model. Population-validated: exact consumption on 805/805 files (§9).** `lpstr` = `u16` length + that many bytes, no terminator. All integers little-endian. Field names are ours, chosen for what the code demonstrably does with each field.

### 7.1 File header (8 bytes) **[CONFIRMED — disassembly + empirical, 805/805]**

| Offset | Size | Field | Notes |
|---|---|---|---|
| `0x00` | 4 | `magic` = `0xBEEFFEED` | Not checked by the loader (§6.2). |
| `0x04` | 2 | `version` = **11** (805/805) | Threshold-tested (§6.2). |
| `0x06` | 2 | **`record_count`** | The loop bound of the record reader. An EOF-driven walk that ignores this field finds exactly this many records in **805/805** files. Range 0–1,812; 3 files declare 0 (2,238 bytes = header + tables only). |

Read as a single raw 8-byte read, i.e. **not** through the byte-swapping readers *(desk review 2026-09-30: so `version` and `record_count` are always little-endian, whatever the stream's big-endian flag)*.

*Validated by data (desk review 2026-09-30): Team B's independent reader, `team-b/HANDOFF.md` §9.71 — 805/805 exact-EOF, records parsed == header count 805/805.*

**Review status (2026-09-30): VALIDATED-BY-DATA: record count == records parsed 805/805 (Team B §9.71) — desk review (not re-derived from the executable).**

### 7.2 The three tables (`version ≥ 8`) **[CONFIRMED — disassembly + empirical]**

Three times in sequence: `u32 count`, then `count` × `{ lpstr name; u8 file_id }`, no padding — exactly §3's shape. Order: **(1) memory-pool / streaming-allocator names, (2) resource-type names, (3) container-kind names.** What they are for is §8. Byte-identical in all 805 files (one distinct triple).

*Validated by data (desk review 2026-09-30): Team B, `team-b/HANDOFF.md` §9.71 — "the three tables are byte-identical in all 805", consumed within the 805/805 exact-EOF gate.*

**Review status (2026-09-30): VALIDATED-BY-DATA: tables byte-identical and consumed exactly, 805/805 (Team B §9.71) — desk review (not re-derived from the executable).**

### 7.3 Container record — repeated `record_count` times **[CONFIRMED — disassembly + empirical, 805/805, 8,362 records]**

| Field | Type | Meaning (what the code does with it) | Label |
|---|---|---|---|
| `name` | lpstr | The container's name; it becomes the container object's name, and `<name>.str2_pc` is its backing file in 8,057 of 8,362 records (§9.4). | CONFIRMED |
| `container_kind` | u8 | File id in **table 3**, translated through the per-parse container-kind map to the engine's container-kind row; selects the kind's default pool and name ("Decal", "Zone", "Vehicle", …). 35 distinct values in the population. | CONFIRMED |
| `record_flags` | u16 | Only two bits are read: **`0x0080`** = *the record's size hints are live* (its `header_region_size`, `payload_length` and size table are consumed: stored on the container and compared with the entries' registered sizes; the container is flagged so the streaming-request builder `FUN_00db1fd0` later uses them); **`0x0100`** = forwarded to the container's creation as a property flag. Population: `0x0080` on 7,990 records, `0x0300` on 305 (exactly container kinds 24 and 25, the two effects kinds), `0x0200` on 21 (kind 39), `0x0000` on 46. | 0x80 gate CONFIRMED; 0x100's effect on the container object read but not followed (HIGH CONFIDENCE); 0x200 alone has no reader (OPEN) |
| `entry_count` | **s16** (signed) | Number of entries; sign-extended (`MOVSX`). Max **3,820** in the population; **491** records exceed 255, so §4's `u8` cannot hold it. **[OPEN — desk review 2026-09-30: what the engine does with a negative value (the size table is sized by it) is not stated; no shipped record is negative, so a reader should reject one (Team B's does); to be settled against the executable (`FUN_00db41a0`).]** | CONFIRMED |
| `header_region_size` | u32 | Stored on the container; copied into the streaming request for the container's `.str2_pc`. **Equals the sibling `.str2_pc`'s `payload_start`** (its header + directory + name table, each a `0x800`-rounded block; `0x0800` for an empty container, `0x1800` for the common small container — 5,887 records — and larger for containers whose directory or name table spills into further blocks). Zero when `record_flags` lacks `0x80` (372/372). | Role HIGH CONFIDENCE (call shape read); value CONFIRMED — empirical 7,990/7,990 |
| `source_name` | lpstr (`version ≥ 10`) | Its only reader is the branch that records a source-file name on the container (container `+0x34`, via `FUN_00db1690`): the string is used if non-empty, otherwise the manifest stream's own name is substituted; the branch is taken only when the loader's group byte is non-zero and the kind row's `+0x20` field is set. Non-empty on 3,213 records — all naming another top-level entry (mostly the per-tile `.asm_pc`, e.g. `1018.asm_pc`); empty on 5,149. *(Desk review 2026-09-30: only the use is conditional — the string itself is always present in a `version ≥ 10` record; §9.2's "without `source_name`" control collapses to 3/805.)* | HIGH CONFIDENCE (branch read; the field's consumer not followed) |
| `extra_len` + `extra[extra_len]` | u32 + bytes | An opaque blob read into a stack buffer and handed to the container-instantiation helper `FUN_00db3fd0` on the build path when the loader's group byte is 0. **Zero in 8,362/8,362 records, so never exercised** — the width is from code only (a control that skips the blob still parses 805/805 because of this; it proves nothing). **[OPEN — desk review 2026-09-30: whether the blob is read from the stream on the verify path and when the group byte is non-zero (a reader that skips it on only one path would lose sync), and the stack buffer's size limit, are not stated; data cannot settle it (0 everywhere); to be settled against the executable (`FUN_00db41a0`, build site near `0x00db4437`, verify site near `0x00db453f`).]** | Width CONFIRMED — disassembly; role OPEN |
| `payload_length` | u32 | Stored beside `header_region_size` and copied into the same streaming request. **Equals the sibling `.str2_pc`'s header field `0x168`, or 0 when that field is the all-raw sentinel `0xFFFFFFFF`** (`spec-vpp-container.md` §1) — 7,990/7,990 (this is §4's "total compressed size", correctly identified there). Zero when `record_flags` lacks `0x80`. | Role HIGH CONFIDENCE; value CONFIRMED — empirical |
| `size_table[entry_count]` | `(u32 primary, u32 secondary)` × N | **Present when `version ≥ 11` or `record_flags & 0x80`** (so always present in the shipped version 11). Consumed only when `0x80` is set: each pair is compared with the size currently registered for that entry, and any entry that differs is queued (12-byte records: entry, primary, secondary) on the container; both the streaming-request builder and the allocation routine read that queue. **In the population every pair equals the entry's own trailer sizes (390,134/390,134)**, so on a first load the queue is always empty; its purpose (**HYPOTHESIS: overriding an already-registered container's sizes, e.g. patch/DLC re-declarations**) is not evidenced by any shipped file. | Layout + condition CONFIRMED; purpose HYPOTHESIS |
| `entries[entry_count]` | §7.4 | One per **primary** file of the container, in directory order. | CONFIRMED |

The record therefore has a fixed part of **19 bytes after `name`** when `entry_count = 0` (`1 + 2 + 2 + 4 + 2 + 4 + 4`) — the same "19-byte tail" §5 item 3 found, now decoded.

**The `1a 80 00` bytes:** they are `container_kind = 0x1A` (26, "Decal") and `record_flags = 0x0080`, **not** "one constant byte" — the count that follows is the `s16` at record `+3` after `name`.

**Review status (2026-09-30): NEEDS-EXE: negative `entry_count` behaviour; whether `extra` is consumed on every path. The layout itself is VALIDATED-BY-DATA (8,362 records, 805/805 exact-EOF, Team B §9.71) — desk review (not re-derived from the executable).**

### 7.4 Entry — a name plus exactly 13 bytes **[CONFIRMED — disassembly, byte order checked against T's reader; empirical 390,134/390,134]**

| Field | Type | Meaning | Population |
|---|---|---|---|
| `name` | lpstr | The primary file's name (for kinds whose entries are files). | max 41 characters |
| `type_id` | u8 | File id in **table 2**, translated through the per-parse type map, then the row of the engine's resource-type registry (`spec-format-inventory.md` §2). **The id is the registry id** (§8). | 41 distinct values — table 2's 1–45 except 6, 8, 14, 28 (§9.3, §9.5) |
| `pool_id` | u8 | File id in **table 1**, translated through the pool map; **`0` = "inherit the container kind's default pool"** (the allocation code substitutes the kind row's pool byte). | 14 distinct values; 0 on 230,554 entries |
| `entry_flags` | u8 | OR-ed into the runtime entry's flag byte (`+0x15`). Only three bits occur: **`0x04` = paired — the entry has a `g`-side secondary file** (set on exactly the types whose `g`-side file ships; §9.3), **`0x40`** (the allocation routine tolerates a failed allocation for an entry with this bit by dropping the entry instead of failing the container — HIGH CONFIDENCE reading of the failure branch; 122,170 entries, of which 108,960 are type 17 and 13,210 type 16, i.e. the streaming-mip texture entries), **`0x20`** (12 entries, all type 32; read by the allocation routine's first pass, which then queries two file names — role OPEN). | values {0: 55,699; 4: 212,253; 68: 122,170; 32: 12} |
| `variant_select` | u8 | Only the **low 2 bits** are used, stored sign-extended in the entry's `+0x18` field, which indexes the type row's list of extension strings — i.e. it picks *which* registered file extension the entry's file has. **Empirically: `1` ⇔ the alternate `.cvbm_pc` extension on type 16 (Peg) — 19,755/19,755; `0` ⇒ the primary (`.cpeg_pc` 1,214/1,214); `0xFF` (= −1) only on type 39 (Buffer, no file), 2,447/2,447.** Which extension slot each value indexes was not read from the row. **[OPEN — desk review 2026-09-30: "low 2 bits, sign-extended" does not say how the byte maps to the stored value (sign-extending two bits gives 3 → −1 and 2 → −2; 2 never occurs); to be settled against the executable (the `+0x18` store in `FUN_00dd2f50`).]** | {0: 367,932; 1: 19,755; 255: 2,447} |
| `primary_size` | u32 | Entry `+0x04`: the `c`-side size **as an exact byte count, unpadded** (T's finding; alignment is applied at runtime). **Equals the sibling directory's uncompressed size of the `c`-file** on every checked entry. | — |
| `secondary_size` | u32 | Entry `+0x08`: the `g`-side size (0 when unpaired or when the `g`-file has size 0). **Equals the sibling directory's uncompressed size of the `g`-file** whenever that file is present. | — |
| `alloc_group` | u8 | Entry `+0x16`. **`0xFF` = allocated alone; any other value = entries of the same record with the same group and pool are sized together and allocated as one block** (`spec-resource-dispatch.md` §8.3 sizing pass). | values {0: 255,753; 1: 15,195; 2: 1,693; 3: 8; 4: 8; 5: 4,572; 255: 112,905}; group 5 is the zone-header entries (types 44/45) |

Layout order in the file: `type_id, pool_id, entry_flags, variant_select, primary_size, secondary_size, alloc_group` (= T's "type ID, 3 bytes, primary, secondary, group"). Only `type_id` and `pool_id` pass through translation maps.

**Review status (2026-09-30): NEEDS-EXE: `variant_select` byte-to-value mapping. The 13-byte layout is VALIDATED-BY-DATA (390,134/390,134 entries, Team B §9.71) — desk review (not re-derived from the executable).**

### 7.5 Worked example — the first record of `decals.vpp_pc`'s manifest **[CONFIRMED — byte-exact]**

`11 00` `decal_bullet_wood` │ `1a` │ `80 00` │ `01 00` │ `00 18 00 00` │ `00 00` │ `00 00 00 00` │ `f4 00 00 00` │ `00 03 00 00  00 00 00 00` │ `1f 00` `vfx_decal_bullet_wood.matlib_pc` │ `23 00 00 00` │ `00 03 00 00` │ `00 00 00 00` │ `00`

= `container_kind` 26 ("Decal"), `record_flags` `0x0080`, `entry_count` 1, `header_region_size` 6,144, empty `source_name`, `extra_len` 0, `payload_length` 244, size table `(768, 0)`, then the entry: type 35 ("Material Library"), pool 0, flags 0, variant 0, primary 768, secondary 0, group 0. The 22 bytes between the count and the entry's name length are `4 + 2 + 4 + 4` fixed bytes plus the 8-byte size table — which is why the black-box read saw a single "interleaved" sub-block.

**Review status (2026-09-30): DESK-PASS (every byte re-derived by hand against §7.3/§7.4; Team B built its synthetic test from this example byte for byte, §9.71) — desk review (not re-derived from the executable).**

## 8. What the three tables are — per-parse id translations, not global maps (added 2026-09-20, agent Y)

Answers the brief's "are they loaded into engine-global name→id maps? which globals?": **no globals are written.**

### 8.1 Mechanism **[CONFIRMED — disassembly]**

For each table the parser allocates a **255-byte translation array from the arena it was given** (allocate slot `+0x38`, size `0xFF`; only element 0 is cleared), and for each `(name, file_id)` pair looks the *name* up in the matching engine registry and stores `array[file_id] = engine_id`. The three arrays' pointers live in the parse context (`+0x08` pool map, `+0x0C` type map, `+0x10` container-kind map) and are read only by the record and entry readers. A `file_id` that no table lists is never validated (the array byte is whatever the arena gave). A lookup that returns 0 is a failure (index 0 is not a valid engine id in any of the three registries).

| Table | Looked up in | How | Failure text (data) |
|---|---|---|---|
| 1 — pools | The engine's pool registry: 255 slots (array base `0x02a3c170`, the pool array of agent T's §8.3; each slot points to a pool object whose name string starts at `+4`), filled at startup by `FUN_005d58f0` and `FUN_00db01a0` | `FUN_00dd6470`: case-insensitive `stricmp` over slots 0–254 | "unrecognized streaming allocator" |
| 2 — types | The resource-type registry: 255 rows (base `0x02a39158`, stride `0x30`, name pointer at row `+0x14`), `FUN_00daf5c0` | case-insensitive `stricmp`, skipping unregistered rows | "unrecognized prim type" |
| 3 — container kinds | The container-kind array: 255 rows (base `0x029eadf8`, stride `0x28`, name pointer at `+0x0c`), filled by the 41 registration calls of `FUN_006ff730` (+ `FUN_00db01a0`) | inline `stricmp` loop over rows 1–254 | "unrecognized container type" |

*Desk review 2026-09-30: each array has 255 bytes (indices 0–254), so a `file_id` of 255 would index past it; the shipped tables never use 255 (their highest id is 254), and a reader should reject it.* **[OPEN — desk review 2026-09-30: the engine's behaviour for `file_id` 255, the `0xFF` allocation size and the three registry bases are disassembly-only; to be settled against the executable (`FUN_00db4980`).]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (`file_id` 255 note) — desk review (not re-derived from the executable).**

### 8.2 The file ids equal the engine ids — the maps are the identity in shipped data **[CONFIRMED — empirical]**

- **Table 2:** its 45 entries are the 43 registered types of `spec-format-inventory.md` §2 — **43/43 ids and names identical** (checked by parsing that document's table against the file) — plus `253 GSP_DESTUB_PRIM_BUF` and `254 GSP_DESTUB_PRIM`, whose registrations in `FUN_00db01a0` carry exactly those ids. So a `type_id` in an entry **is** the registry's resource-type id; the registry's 255-row array (T's census) is indexed by it.
- **Table 1:** 38 of its 39 names were recovered as `(id, "name")` pairs from the pool builder `FUN_005d58f0` — **38/38 ids equal**, 0 mismatches — and the 39th, `GSA_DESTUB_ALLOCATOR`, is registered at `0xFE` by `FUN_00db01a0`, as in the file. (E.g. id 30 `mip_streaming`, 38 `level container cache`, 40 `zone header cache`, 41 `TOD_luts`.)
- **Table 3:** **32 of 41** `(id, name)` pairs were extracted from the registration function `FUN_006ff730` with **0 mismatches** (the other nine names are assembled from short inline constants the extractor did not pair — `Items`, `UI`, `Zone`, `LUT`, `Skybox`, `Zscene`, `UI peg`, `Vehicle Fully Customizable` — not checked) *[desk review 2026-09-30, recount: eight names are listed; 41 = 32 + these 8 + `GSC_DESTUB_CONTAINER`, which is checked just below — so eight are unchecked, not nine. `Zone` has since been read field by field as row `0x1D` in `spec-world-streaming.md` §10.2]*; `GSC_DESTUB_CONTAINER` is `0xFE` in `FUN_00db01a0`, as in the file. **[HIGH CONFIDENCE for the ~~9~~ 8 unchecked, CONFIRMED for the 32.]** **[OPEN — desk review 2026-09-30: §8.1 counts 41 registration calls in `FUN_006ff730` plus the destub kind from `FUN_00db01a0` (42 kinds), against 41 table-3 entries including the destub kind; which registered kind is missing from table 3 (or whether the call count is off) is not stated; to be settled against the executable (list every `FUN_006ff730` registration with its id and name).]**

The design intent, from the diagnostics themselves, is **data versioning**: the crunched manifest carries its own name list so that an engine whose enum was renumbered can still resolve every id, and refuses to run (rather than mis-load) when a *name* has vanished. In the shipped build nothing was renumbered, so the translation is inert. A re-implementation can treat the ids as engine ids and skip the tables, **provided** it does not need to survive a renumbering.

**Review status (2026-09-30): NEEDS-EXE: 42 registered kinds vs 41 table-3 entries; eight table-3 names are unchecked. Text fix applied: "nine" unchecked recounted to eight — desk review (not re-derived from the executable).**

### 8.3 The `DESTUB` entries (ids 253/254) **[CONFIRMED — disassembly + empirical]**

`GSP_DESTUB_PRIM` (`.asm_pc` is its registered primary extension), `GSP_DESTUB_PRIM_BUF`, `GSC_DESTUB_CONTAINER` and `GSA_DESTUB_ALLOCATOR` are the **manifest's own bootstrap types**: `FUN_00db01a0` registers them so that an `.asm_pc` is itself loaded through the ordinary container pipeline (as the primary entry of a 2-entry "destub" container), whose registered per-kind callback `FUN_00daf770` parses it (§6.1). They occur in the tables (so a manifest can in principle describe destub containers) but in **no** entry of the population (no entry has type 253 or 254; §9.5). **[OPEN — desk review 2026-09-30: the bootstrap role rests on disassembly; the data only shows that these types are absent, and the site that invokes the callback is untraced; to be settled against the executable (`FUN_00db01a0`, `FUN_00daf770` and the caller of the kind row's callback slot).]**

**Review status (2026-09-30): NEEDS-EXE (low priority): destub bootstrap role and callback invoker — desk review (not re-derived from the executable).**

## 9. Population validation over every shipped `.asm_pc` (added 2026-09-20, agent Y)

Harness: `tools/harnesses/asm_manifest_parse.py` (`--controls`, `--xcheck`; parser = §7, gate = exact consumption). Population: every `.asm_pc` directory entry of every `.vpp_pc` under the game install — **39 archive directories read** (the 38 in `packfiles/pc/cache/` plus `launcher.vpp_pc`) — giving **805 files in 20 archives** (18 archives hold 1–20 manifests; `sr3_city_0`/`sr3_city_1` hold 331/405; the rest hold none), **20,956,590 bytes**, 2,238 … 8,182,847 bytes each, **8,362 records, 390,134 entries**.

### 9.1 Sample trust / exclusions **[CONFIRMED — empirical]**

**0 files excluded.** All 805 are stored raw (directory `+0x10 = 0xFFFFFFFF`, 805/805), so the mode-(a) limitation of `spec-vpp-container.md` was never in play and the parked mode-(a) question was not touched. The sibling `.str2_pc` directories used in §9.4 are also top-level raw entries (all 6,393 top-level `.str2_pc` are raw; only their headers/directories were read, never a compressed payload). **Nested `.asm_pc`: none** — 0 of the 6,393 top-level `.str2_pc` directories list one (compressed-nested containers, if any, were not opened). *(Desk review 2026-09-30: 8,057 records have a top-level sibling (§9.4) but only 6,393 top-level `.str2_pc` exist, so at least 1,664 records share a sibling with another record; which records, and whether manifests in different archives describe the same containers, was not measured — compare §9.3's kind-29/30 note.)*

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

### 9.2 The gate and its controls **[CONFIRMED — empirical]**

| Model | Files that consume to exactly EOF |
|---|---|
| **Code-derived layout (§7), driven by the header `record_count`** | **805 / 805** |
| Same, but ignoring `record_count` (walk to EOF) — record count found | 805 / 805 equal to the header field |
| §4 as originally written (`u8` count, interleaved sub-blocks) | 3 / 805 |
| Agent T's probe walker (3-byte header, `u16` count, interleaved) | 13 / 805 |
| Real parser with the size table moved **after** the entries | 3 / 805 |
| Real parser without the size table | 3 / 805 |
| Real parser without the `source_name` string | 3 / 805 |
| Real parser with a `u8` count | 3 / 805 |
| Real parser skipping the `extra` blob | 805 / 805 — **uninformative** (`extra_len` is 0 everywhere, §7.3) |
| Truncate the last byte / append one zero byte / delete byte 8 (first table's count) | 0 / 805 each |

The 3 files that survive every perturbation are exactly the three **zero-record** manifests (2,238 bytes: header + tables), which contain no record to mis-parse.

*Validated by data (desk review 2026-09-30): Team B, `team-b/HANDOFF.md` §9.71, reproduced the gate (805/805) and every control collapse (3/805 or 0/805), including the same caveat that skipping `extra` is uninformative.*

**Review status (2026-09-30): VALIDATED-BY-DATA: gate 805/805 and every control collapse reproduced (Team B §9.71) — desk review (not re-derived from the executable).**

### 9.3 Field variation — value sets and correlations **[CONFIRMED — empirical]**

- **`magic`** `0xBEEFFEED` ×805; **`version`** 11 ×805; the **three tables** are byte-identical in all 805 (one distinct triple).
- **`record_count`** = records parsed 805/805; per-file median 1, max 1,812 (`sr3_city_0`'s `stream_grid.asm_pc`); 527 files hold exactly one record (the per-tile and per-interior manifests).
- **`container_kind`**: 35 distinct ids. Counts (id name n): 1 Vehicle 318, 2 Vehicle Fully Customizable 75, 3 Items Preload Mempool 2, 5 Items Preload 147, 6 Weapon High Res 44, 7 Character 303, 8 Character High Res 283, 9 Player 2, 10 Cust Component 1,348, 12 Cust Composite 337, 13 Cust Shaderball 45, 14 Small static mesh 7, 15 Large static mesh 4, 16 Environmental item 1, 17 Skybox 1, 18 Cutscene 130, 19 Zscene 82, 21 UI 183, 22 UI peg 928, 23 Effects Preload 243, 24 Effects (Vehicle) 33, 25 Environment Effects 272, 26 Decal 14, 27 Level Always Loaded 7, 28 Level Always Loaded Peg 4, 29 Zone 1,194, 30 Zone (High LOD) 1,686 *[desk review 2026-09-30: these are **container kinds** (table 3, kind registry `0x029eadf8`; `spec-world-streaming.md` §10.2 reads rows `0x1D` "Zone" and `0x1E` "Zone (High LOD)"). They are a different registry from the **resource types** 29/30 "Zone (fast)"/"Zone (slow)" (table 2, `spec-format-inventory.md` §2); the numbers coincide.]* **[CONFLICT / OPEN — desk review 2026-09-30: `spec-world-streaming.md` §10.2/§10.6 count 597 kind-29 records (468 tile + 129 `^name`) and 843 kind-30 records over the same 805 manifests, exactly half of 1,194 and 1,686 here. One count probably includes records repeated across manifests (e.g. the per-tile manifests and `stream_grid`, or `sr3_city_0` vs `_1`), but neither spec says which. `spec-world-streaming.md` §10.7 also finds 1,194 kind-29 containers with a zone header, keyed from the manifest record. Team B's §9.71 run does not list per-kind counts. To be settled against real data: recount kind-29/30 records per archive and per manifest, with and without de-duplication by name.]**, 31 Interior zone 150, 32 Large interior zone 183, 33 mission 155, 34 large mission 3, 35 activity_type 19, 39 mission model data 155, 40 large mission model data 3, 41 Vehicle customization camera 1. **Every kind id is in table 3 (8,362/8,362).**
- **`record_flags`**: `0x0080` 7,990; `0x0300` 305; `0x0200` 21; `0x0000` 46. **`0x0300` occurs exactly on kinds 24 and 25** (33 + 272; `effects.vpp_pc`'s `vfx_containers.asm_pc` and the three DLC `*_vfx_containers.asm_pc`). `0x0200` occurs only on kind 39 and `0x0000` only on kinds 27/28/31/32/33, **all inside the three DLC `*_sr3_city.asm_pc` manifests**. Kinds 27/28/31/32/33/39 also occur with `0x0080` elsewhere, so the flag is per-record, not per-kind, except for 24/25.
- **`header_region_size` / `payload_length` are exactly 0 on every record whose `record_flags` lacks `0x0080` (372/372)**; on the 7,990 records that have it, `header_region_size` takes 44 distinct values, **all multiples of `0x800`**, `0x1800` (6,144) on 5,887, `0x0800` on the 155 empty containers, 2,103 records with any other value (up to `0x36000`) *[desk review 2026-09-30, recomputed: 5,887 + 155 + 2,103 = 8,145 ≠ 7,990. 2,103 = 7,990 − 5,887 is the count of values other than `0x1800` (§9.4 uses it that way), so it includes the 155 `0x0800` records; the values other than both `0x1800` and `0x0800` then number 7,990 − 5,887 − 155 = 1,948, assuming all 155 are among the 7,990 as this sentence says. The histogram itself was not re-run]*; `payload_length` is 0 on 138 records (all zero-entry containers, whose sibling header field is the raw sentinel) and otherwise the sibling's payload byte length.
- **`source_name`** empty 5,149, non-empty 3,213 (all name an existing top-level entry in some archive; 1,597 equal the manifest's own file name — the per-tile manifests of `sr3_city_0/1`; the rest are the tile names listed in `stream_grid` and the DLC `*_stream_grid`/`*_sr3_city` manifests).
- **`extra_len`** = 0 on 8,362/8,362.
- **`entry_count`**: 0 on 155 records, up to 3,820; 491 records exceed 255 (spread over **238 files**: city tile and interior manifests, both `stream_grid` manifests, the cutscene and DLC manifests).
- **Entry trailer:** `type_id` 41 distinct values; `pool_id` {0 230,554; 7 200; 8 25,000; 9 3,636; 21 792; 22 239; 26 2,705; 30 109,223; 31 12,269; 32 878; 33 63; 37 2; 39 1; 40 4,572}; `entry_flags` {0; 4; 32; 68}; `variant_select` {0; 1; 255}; `alloc_group` {0…5, 255} (§7.4). **Every `type_id` is in table 2 and every non-zero `pool_id` is in table 1 (390,134/390,134 each).**
- **Size table vs trailer:** **390,134/390,134** entries have `size_table[i] == (primary_size, secondary_size)`.
- **Type ↔ file extension.** Agrees with `spec-format-inventory.md`'s registered extensions for every type that registers one, with **two exceptions** (type 17: manifests name `.cvbm_pc`, the registered primary is `.cvbl_pc`; type 23: files are `.cte_xtbl`, registered `.xtbl`), and the types that register no extension carry their real file names. Observed: 1 `.ccar_pc`; 2 `.cvtf_pc`; 3 `.cpeg_pc`; 4 and 15 `.cefct_pc`; 5 and 9 `.ccmesh_pc`; 7, 11 and 12 `.cmorph_pc`; 10 `.cpeg_pc`; 13 and 20 `.rig_pc`; **16 `.cpeg_pc` (variant 0) or `.cvbm_pc` (variant 1)**; 17 `.cvbm_pc` (variant 0 — type 17's registered primary is `.cvbl_pc`, an inventory statement this population does not reproduce: the type-17 entries name `.cvbm_pc`); 18 `.cpeg_pc`; 19 `.csmesh_pc`; 21 and 41 `.anim_pc`; 22 and 35 `.matlib_pc`; 23 `.cte_xtbl`; 24 `.csc_pc`; 25, 33, 34 and 43 `.xtbl`; 26 `.vint_doc`; 27 and 32 `.lua`; 29 and 30 (resource types Zone (fast)/(slow) — not container kinds 29/30, see above) `.czn_pc`; 31 and 40 `.clmesh_pc`; 36 `.csrt_pc`; 37 `.cfmesh_pc`; 38 `.lightmult_pc`/`.lightrot_pc`/`.lightpos_pc`; 39 no extension (a name only); 42 `.ctdg_pc`; 44 and 45 `.czh_pc`. Types 6, 8, 14, 28 (and the DESTUB pair) never occur.
- **`entry_flags & 0x04` (paired) is set on exactly the types whose files come with a shipped `g`-side file** and never on the others: paired types 1, 3, 4, 5, 9, 10, 15, 16, 17, 18, 19, 29, 30, 31, 36; unpaired 2, 7, 11, 12 (the three morph types register a secondary extension whose file never ships — `spec-morph-format.md` §6), 13, 20–27, 32–35, 37–45. Bit `0x40` occurs only on types 16 and 17; bit `0x20` only on type 32.

**Review status (2026-09-30): NEEDS-DATA: kind-29/30 counts are twice those in `spec-world-streaming.md` §10.2. Text fixes applied: container-kind vs resource-type naming note, `header_region_size` breakdown recomputed. All other sums tile — desk review (not re-derived from the executable).**

### 9.4 Cross-check against the sibling `.str2_pc` directories **[CONFIRMED — empirical]**

A record's backing container is `<name>.str2_pc` (7,442 records) or, for records named by a whole file name, `<name minus its extension>.str2_pc` (615 records) — **8,057 of 8,362 records have a top-level sibling**; the other **305** are the effect containers of `effects.vpp_pc`'s `vfx_containers.asm_pc` (249) and the three DLC `*_vfx_containers.asm_pc` (17 + 25 + 14), whose backing content is not a top-level `.str2_pc` (the same outcome as §2's old "249 doesn't match"). For the 8,057:

- **Matching conventions (added 2026-09-20 from Team B's independent reader, confirmed against this project's own matcher `asm_manifest_parse.py`, which already does both):** (1) names are compared **case-insensitively** — the manifest keeps the authored case (e.g. `ViolaBC_body_high.cpeg_pc`) while sibling directory names are all lowercase (0/384,479 contain an uppercase letter); case-exact equality holds for only 6,608/8,057 records, case-insensitive for 8,057/8,057. (2) When a record's `<name>.str2_pc` exists in more than one archive (68 records; it changes the result for 24), **prefer the sibling in the same archive as the manifest**; with that rule 8,057/8,057. **Independently reproduced by Team B (2026-09-20, freshly built reader): 805/805 files exact-EOF, 8,362 records, 390,134 entries, header record count == records parsed 805/805, header_region_size == sibling payload_start 7,990/7,990, payload_length == sibling header `0x168` 7,990/7,990; every control collapses (3/805 or 0/805).**
- The manifest-derived file list **equals the sibling directory in name (case-insensitively, see above), order and size on 8,057/8,057 records**: every entry except type 39 contributes its primary file; a **paired** entry contributes its `g`-side file (`.c…` → `.g…`) immediately after (mandatory when `secondary_size ≠ 0`; when `secondary_size = 0` the `g`-file is present as a zero-size file in some containers and absent in others — both accepted). **Every sibling directory name is accounted for by the manifest (0 unaccounted).** The old §2/§4 "count matches the sibling's entry count" therefore holds only for unpaired containers; a paired entry costs two directory entries and one manifest entry. Type 39 (`Buffer`) entries (2,447) name no file at all.
- `primary_size` and `secondary_size` equal the sibling directory's uncompressed sizes — the same 8,057/8,057.
- **`header_region_size` = sibling `payload_start` on 7,990/7,990 records with `0x0080`; `payload_length` = sibling header field `0x168` (0 for the raw sentinel) on 7,990/7,990.** (The first is weak evidence on its own — 5,887 records share the three-block minimum — but the 2,103 records with any other value all match, and the second is strong.)
- **Control (each record matched against the sibling of the *next* record):** 110 of 8,057 lists, 142 of 7,990 `payload_length`s match by chance.

*Validated by data (desk review 2026-09-30): Team B, `team-b/HANDOFF.md` §9.71 — `header_region_size` == sibling `payload_start` 7,990/7,990, `payload_length` == sibling `0x168` 7,990/7,990, case-insensitive list match 8,057/8,057.*

**Review status (2026-09-30): VALIDATED-BY-DATA: 7,990/7,990 twice and 8,057/8,057 (Team B §9.71) — desk review (not re-derived from the executable).**

### 9.5 What the population does *not* show

Types 6, 8, 14, 28, 253 and 254 never occur as entry types, and no entry names a `.cvbl_pc` file. `extra_len ≠ 0`, `version ≠ 11`, a size-table pair that differs from its entry's trailer, and big-endian-flagged streams never occur either. Those code paths are described from disassembly only.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 10. Corrections to §2–§5, and the live open items (added 2026-09-20, agent Y)

### 10.1 What §2–§5 got wrong, in one place **[CONFIRMED — disassembly + population]**

| Black-box claim | Correct reading |
|---|---|
| `0x06` = "record count", but inconsistent with sibling counts on the big archives (249, 372) | It **is** the record count (805/805). The "anomaly" was a wrong denominator: 372 = `vehicles.vpp_pc`'s 372 `.str2_pc` exactly; 249 = `effects.vpp_pc`'s effect containers (no top-level `.str2_pc`). |
| `u16 name_length, name, u8 zero, u8 entry_count, u16 zero, u16 span_field, 7 zero bytes, u32 total` | Mis-aligned. The real bytes after `name` are `u8 container_kind, u16 record_flags, s16 entry_count, u32 header_region_size, lpstr source_name, u32 extra_len, extra[], u32 payload_length`; e.g. `1a 80 00 01 00` = kind 26, flags `0x0080`, count 1. The old model's "zero" bytes were the empty `source_name`/`extra_len` and the high bytes of the `u32` fields. |
| `entry_count` is `u8` | **`s16`** (max 3,820; 491 records > 255). |
| `span_field = 8 + 8×entry_count` | Coincidence: the bytes are the `u32` `header_region_size` (sibling `payload_start`, a multiple of `0x800`); its middle byte is 24 for the common small container (`0x1800`), which equals `8 + 8×N` only at N = 2 (and `0x0800` → 8 at N = 0). |
| Per-entry sub-block `u32 size, u32 unknown(=0), lpstr name, ~12–13-byte trailer` interleaved | A **size table** `(primary, secondary) × N` precedes all names; each entry is `lpstr name` + **exactly 13 bytes**. `unknown_field` = the secondary (`g`-side) size, 0 in the old samples only because none was paired. |
| The trailer "includes a repeat of the entry's uncompressed size" | Yes: primary size = size-table primary = sibling directory size, 390,134/390,134 and 8,057/8,057. Full field list: §7.4. |
| One aggregate size field = sibling's `0x168` | **Correct** (`payload_length`, 7,990/7,990) — but only when `record_flags & 0x80`. |
| The three tables are name↔id lookup tables (purpose inferred) | Confirmed as per-parse translation lists against three engine registries (§8); ids equal the engine's own. |
| "`entry_count` matches the sibling `.str2_pc`'s real entry count exactly" | Only for unpaired containers; a paired entry adds a second directory entry (§9.4). |
| The magic is "required by the format" | Never enforced by the PC or DX11 loader (§6.2). |

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

### 10.2 Live open items (the honest gaps — nothing below is guessed at)

1. **What consumes `header_region_size` / `payload_length` downstream.** Both are stored on the container and copied into the `.str2_pc` stream request built by `FUN_00db1fd0` (call shape read: name, the `.str2_pc` extension, these two values, per-entry size/handle arrays); *what the streaming layer does with them* was not followed. Values are fully characterised (§9.4).
2. **`record_flags` `0x0100` / `0x0200`.** `0x0100` is forwarded as a property flag when the container object is created (sets a `0x0300` word on the object) and occurs exactly on kinds 24/25; **what that word means was not traced.** `0x0200` alone (kind 39, DLC manifests) has no reader found.
3. **The `extra` blob** (`extra_len` is always 0) — width confirmed, role open. **The `source_name`** role is a read of the name-substitution branch (HIGH CONFIDENCE), not of its consumer.
4. **`entry_flags` bit meanings beyond `0x04`.** `0x40` = failure-tolerant is a reading of the allocation routine's failure branch; `0x20` (12 entries) drives an override-file existence check whose outcome was not traced.
5. **`variant_select`:** which extension slot in the type row values 0/1/−1 index was not read; the population correlation (type 16: `1` ⇔ `.cvbm_pc` 19,755/19,755) is the evidence. The inventory's "type 17 primary `.cvbl_pc`" is not reproduced by the manifests (all 109,219 type-17 entries are `.cvbm_pc`, variant 0); this is left for the inventory's owner. **[2026-09-30: `spec-format-inventory.md` row 17/§6 item 2 and `spec-low-mips.md` now mark their "no file of this type ships / type 17 can be ignored" statements superseded by this finding.]**
6. **Purpose of the size table's mismatch queue** (HYPOTHESIS: re-declaration/override) — not evidenced by shipped data, where the queue is always empty.
7. **Versions below 11 and big-endian streams** — code paths read, never exercised.
8. **Whether the 305 records without a top-level `.str2_pc`** (effect containers) have their content anywhere in the shipped archives; and whether `.str2_pc` files nested inside compressed containers carry `.asm_pc` files (0 found in the raw ones).
9. **DX11 entry reader** `FUN_00dd7170` was identified but not read line-by-line (the parser and record reader were).

**Review status (2026-09-30): DESK-PASS (none overclaimed; the new desk-review OPEN notes are in §6.2, §7.3, §7.4, §8.1, §8.2, §8.3 and §9.3) — desk review (not re-derived from the executable).**

### 10.3 Artifacts

Harness `tools/harnesses/asm_manifest_parse.py` (parser + `--controls` + `--xcheck`); Ghidra post-scripts `tools/scripts/AsmY1.java` (magic-immediate scan + reader caller walk), `AsmY9.java` (DX11: magic + parser located), `AsmY11.java` (16-bit halves + `.asm_pc` string xrefs), `AsmYRange.java` (range decompile), runner `tools/run_asm.ps1`, dumps `tools/asm_y1.txt` … `asm_y15.txt`; disposable project `tools/gp_asm1` (safe to delete). The parked mode-(a) question and the `.czn_pc` interior were not touched.

None of these blocks the format's main practical use: a re-implementation can now enumerate every container, every entry's type/pool/paired/variant/group and its exact primary and secondary byte sizes, and the container's header-region size and payload length, all from the manifest alone.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): noted in §10.2 item 5 that the inventory and `spec-low-mips.md` now defer to §9.3 on type 17; flagged the `spec-output.md` citation as unpublished.
- 2026-09-30 (adversarial desk review, `review/adv_asm.md`): review status lines on 18 units, plus a summary after the front matter; a banner saying §1–§5 are historical; container-kind vs resource-type 29/30 naming note (§9.3); CONFLICT/OPEN on kind-29/30 counts (twice `spec-world-streaming.md` §10.2); `header_region_size` breakdown recomputed (2,103 includes the 155; 1,948 others); "nine" unchecked kind names recounted to eight (§8.2); OPEN notes for negative `entry_count`, `extra` consumption, `variant_select` mapping, `file_id` 255, 42-vs-41 kinds, and versions below 11. No label raised.
