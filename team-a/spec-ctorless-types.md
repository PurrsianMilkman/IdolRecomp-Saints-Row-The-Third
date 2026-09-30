# Saints Row: The Third — The Five Constructor-less Registered Types

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, final structural item of the registration-system investigation (`spec-format-inventory.md` §1 item 4).
**Scope:** The question the inventory left open — *five registered types have no constructor, so how do they reach their data?* — plus a census of what each actually ships, and the **zone-header block (`SR3Z`)**, which this pass resolves completely.
**Method:** The generic dispatcher's null-constructor branch; the registration function's extension-table references; a string-driven hunt for the zone module; then a loader replay against **all 2,971 shipped `.czh_pc` files** with an exact-size check and a stride control. Evidence: `tools/ctorless.txt`, `zone_version.txt`, `czh_table.txt`, `zone_subsystem.txt`; harnesses `scratchpad/zone_bulk.py`, `czh_replay.py`, `czh_records.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. Magic values, offsets, strides, alignments and the accepted version range are load-bearing format data. Two literal engine strings are quoted because they are the subsystem's own self-identification — the same convention used for the "Mesh" and "Morph" blocks.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **The mechanism: a null constructor is not an error — it is a supported mode.** The generic dispatcher fetches the type's constructor pointer and, when it is null, **skips the call entirely, marks the resource loaded, and returns success**. The null check sits in both the paired and unpaired branches, identically. **[CONFIRMED — disassembly.]**
- **So constructor-less types are "load the bytes and stop" types.** The generic pipeline still resolves the filename, reads and decompresses the entry, and leaves a valid buffer registered against the resource; it simply never hands that buffer to a per-type parser. Whatever wants the data goes and fetches it from the resource system on its own schedule. **[CONFIRMED — disassembly.]**
- **The zone module does not use the registration table to find its files.** It carries its **own** per-platform extension tables and builds `.czh_pc` / `.czn_pc` filenames itself, in a function that references both tables and lives in the level/zone-layout module. The registration table's zone-header rows carry *no* extension at all, and its zone rows carry extensions but no constructor. **[CONFIRMED — disassembly.]**
- **The zone-header block is now fully resolved**: magic `SR3Z`, version range 27–29, fixed `0x40` header, then a 4-aligned array of `count × 14` bytes. The replay reproduces the **exact byte size of 2,971 / 2,971** files, and a stride control discriminates perfectly (§5). **[CONFIRMED — disassembly + empirical.]**
- **Type 39 `Buffer` ships nothing at all.** Its registered extensions are empty strings, and no archive contains a single extensionless entry — consistent with a purely in-memory resource type that exists so runtime code can hold a buffer under the resource system's bookkeeping. **[CONFIRMED — empirical for the absence; HIGH CONFIDENCE — for the purpose.]**

## 2. The five types, and what each actually ships

| ID | Name | Registered extensions | Constructor | Files shipped |
|---|---|---|---|---|
| 29 | Zone (fast) | `.czn_pc` / `.gzn_pc` | **none** | `.czn_pc` **2,971**, `.gzn_pc` **1,083** |
| 30 | Zone (slow) | `.czn_pc` / `.gzn_pc` (same tables) | **none** | *(shares the above)* |
| 39 | Buffer | `""` / `""` — empty strings, not null | **none** | **0** — no extensionless entry exists in any archive |
| 44 | Zone header (fast) | **none in the registration table** | **none** | `.czh_pc` **2,971** (found by the zone module's own table, §4) |
| 45 | Zone header (slow) | **none in the registration table** | **none** | *(shares the above)* |

Zones are split `fast`/`slow` at the type level while sharing extensions and file set — the distinction is a loading policy, not a format difference. **[HIGH CONFIDENCE — inferred from the shared extension tables and identical file set.]**

Note the asymmetry: **2,971 `.czn_pc` and 2,971 `.czh_pc`, but only 1,083 `.gzn_pc`** — most zones have no `g`-side payload, and one shipped `.gzn_pc` is **0 bytes**. **[CONFIRMED — empirical.]**

## 3. The dispatcher's null-constructor path

The generic resource dispatcher (`spec-resource-dispatch.md` §4–5; the null-constructor path itself is documented in its §6a) reads the constructor pointer from the type's row, then branches on the paired-file flag. **Both branches begin with the same null test, and both jump past the call to the shared success tail on null.** The tail sets the "loaded" bit and returns success. A constructor that *runs* and returns false is a failure; a constructor that is *absent* is a success. **[CONFIRMED — disassembly.]**

That single branch is the whole answer to the inventory's question. It also means the buffer's lifetime and residency are managed exactly as for any other resource — only the parse step is missing.

## 4. How zones actually find their files

Two distinct facts, both confirmed:

1. **The registration function references the `.czn_*` platform table twice** — once for each zone row (29, 30) — and **never references the `.czh_*` table at all**. Rows 44/45 really do have no extensions; this was re-checked against the full decompiled registration function after an initial suspicion that the project's table parser had missed them. It had not. **[CONFIRMED — disassembly.]**
2. **A function in the level/zone-layout module references *both* the `.czh_*` and `.czn_*` platform tables**, and a sibling function references the `.czh_*` table twice more. These are the only code references to the `.czh_*` table anywhere. The zone subsystem therefore composes its own filenames from its own tables. **[CONFIRMED — disassembly.]**

The `.czh_*` table is laid out exactly like the registration table's: five pointers in platform order (pc, ps2, ps3, xbox, xbox2), pointing at `.czh_pc`, `.czh_ps2`, `.czh_ps3`, `.czh_xbox`, `.czh_xbox2`. So it is the same convention, maintained privately by a different subsystem. **[CONFIRMED — empirical, read from the binary's data.]**

Corroborating, the binary ships the source path of that module — a literal string naming `world_zone_layout_build_shared.cpp` under a level directory — referenced from three functions in the same address neighbourhood. **[CONFIRMED — empirical; cited as shipped data, in the same way this project cites engine error strings.]**

**The authoring extension for zones is `.zonex`**, present as a literal in the binary — joining `.fmeshx`, `.animx`, `.effectx` and `.ctd` in this project's authoring-vs-shipped list. **[CONFIRMED — empirical.]**

## 5. The zone-header block (`SR3Z`) — resolved

A `.czh_pc` file is: the shared **material-reference block** (`spec-terrain-format.md` §2 documents its name lists, including the `cc:N`-delimited texture and `.fmeshx` groups) → the **mandatory 16-byte pad** (`spec-geometry-format.md` §3.1.1) → the `SR3Z` block.

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | **Magic `SR3Z`** (`0x5A335253`) | **[CONFIRMED — disassembly + 2,971/2,971.]** |
| `+0x04` | **Version.** Accepted only in the inclusive range **27 … 29**; outside it the loader emits its own error naming the range and rejects the file. Every shipped file is **29** | **[CONFIRMED — disassembly + 2,971/2,971.]** |
| `+0x18` | Runtime pointer slot — fixed up to the record array, or explicitly zeroed when the count is zero. **Zero on disk in 2,971/2,971** | **[CONFIRMED — disassembly + empirical.]** |
| `+0x1C` | `u16` **record count** (0 … 3,701) | **[CONFIRMED — disassembly.]** |
| `+0x1E` | `u16`, small (2 and 3 dominate, 1,194 files each) | ~~**[OPEN.]**~~ **RESOLVED 2026-09-20 (`spec-world-streaming.md` §10.7): the zone type, the selector of the container kind** — the registration routine switches on it (`{1,3,8,10,11,13}` → Level Always Loaded, `{5,6}` → mission, `7` → Interior zone, `9` → Large interior zone, `12` → large mission, anything else incl. `2` → Zone); observed pairs match with 0 exceptions over all 2,971 `.czh_pc` files (Zone↔2: 1,194; Level Always Loaded↔3: 1,194). **[CONFIRMED — disassembly + empirical.]** |
| `+0x40` | End of the fixed header; **align to 4**, then `count × 14` bytes | **[CONFIRMED — disassembly.]** |

**Independently confirmed from the code side (2026-09-10, `spec-zone-data-format.md` §4):** the zone-header *file* reader validates the shared material block, applies **the same mandatory 16-byte pad arithmetic** as `spec-geometry-format.md` §3.1.1, then runs this parser and stores the material-block pointer at **`SR3Z + 0x08`** — reproducing by disassembly the layout established above by replay, and identifying one further field. **[CONFIRMED — disassembly.]**

The loader's error string states the version bounds explicitly and, unusually, suggests a remedy — that a stale file may be present or the layout must be re-built — which is why the range check is a range and not an equality like the Mesh block's.

**The replay is exact.** `material_block + mandatory_pad + 0x40 + align4 + count × 14 == file size` in **2,971 / 2,971** files, residual zero in every one; 276,012 records total; 2,342 files carry zero records.

**Control.** Re-running the same arithmetic with strides 12, 13, 15 and 16 matches 2,342/2,971 in each case — exactly the zero-record files, where the stride cannot matter. Restricted to the **629 files that actually contain records**, the correct stride 14 matches **629/629** and every wrong stride matches **0/629**. **[CONFIRMED — empirical, controlled.]**

### 5.1 The 14-byte record — measured, not resolved

276,012 records were profiled and the internal layout **did not fall out**, so nothing is asserted:

- **Not floats.** Read as `f32`, offsets 4 and 8 yield **zero** plausibly-scaled values out of 276,012 (magnitudes are ~1e38 garbage). Offset 0 yields 18,453 — still 7%.
- Bytes 6–9 are **predominantly zero** (215,847 / 232,596 / 210,834 / 227,266 of 276,012 respectively) — a sparse or optional region.
- `u16` at 0, 2, 4 and 10 are **full-range and high-entropy** (13k–23k distinct values, spanning 0…65,535) — hash-like or packed, not small enumerations.
- `u16` at **12 is bounded**: 4,638 distinct, maximum **8,593** — the only index-shaped field in the record.

**[OPEN.]** Resolving this needs the consumer that walks the array, which is inside the zone streaming subsystem — deliberately not opened here (§7).

## 6. Cross-format notes

- This is the **fourth** load-time pattern the project has catalogued: parse-in-constructor (most types), **stash-only constructor** (morph 11/12, csc 24, ctdg 42), **flag-variant thin constructor** (the PEG family, `spec-low-mips.md`), and now **no constructor at all**. Together they cover every registered type.
- The material-reference block now appears in a seventh context (`.czh_pc`), further supporting `spec-geometry-format.md` §3.1's reading of it as a general-purpose embedded block rather than a per-format header.
- `.czh_pc` is the file `spec-terrain-format.md` §2 already partly documents; that section covers the material block's name lists, and §5 here covers everything after them. The two together account for the whole file.

## 7. Scope note

The zone **streaming** subsystem — the function owning the literal strings *"zone header cache"* and *"zone always loaded"*, at roughly 7.6 KB of code, plus the two filename-composing functions at 2.7 KB and 3.4 KB — was **deliberately not opened**. It is a runtime subsystem, the category the user previously scoped out, and the structural question this pass was asked does not require it. Its entry points are recorded in the Ghidra bookmarks so a future pass starts from a known door.

## 8. Open Items

1. The 14-byte record layout (§5.1) — needs the array's consumer.
2. ~~`SR3Z` `+0x1E`~~ **(`+0x1E` RESOLVED 2026-09-20, see the row above and `spec-world-streaming.md` §10.7; also `+0x0C..+0x17` = three `f32` world-space origin the record positions are relative to)**, and the header bytes between `+0x20` and `+0x40`.
3. What actually distinguishes `fast` from `slow` for zones and zone headers (a loading policy is the inference, not a finding).
4. `.czn_pc`'s own structure — investigated 2026-09-10 and **not resolved**; see `spec-zone-data-format.md`. Its leading `{id, length}` record is confirmed with a control (98.7% vs 2.6%), but a uniform chunk walk is **refuted** population-wide (0/2,971 reach EOF), and **no id value appears as an immediate anywhere in the binary** — the ids are data-driven, so there is no standalone parser to read. Completing it requires the zone streaming subsystem, i.e. a runtime pass. **[Update: `spec-zone-data-format.md` is now "PARTIALLY RESOLVED" — the zone geometry is readable (its §7, §10); the object/property stream remains open.]**
5. Why type 39 `Buffer` registers empty-string extensions rather than null ones — the distinction is deliberate in the registration data.

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): added the `spec-resource-dispatch.md` §6a pointer for the null-constructor path (§3); noted in §6 item 4 that `spec-zone-data-format.md` now reads the zone geometry (object/property stream still open).
