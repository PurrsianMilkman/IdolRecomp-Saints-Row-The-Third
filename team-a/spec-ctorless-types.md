# Saints Row: The Third — The Five Constructor-less Registered Types

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, final structural item of the registration-system investigation (`spec-format-inventory.md` §1 item 4).
**Scope:** The question the inventory left open — *five registered types have no constructor, so how do they reach their data?* — plus a census of what each actually ships, and the **zone-header block (`SR3Z`)**, which this pass resolves completely.
**Method:** The generic dispatcher's null-constructor branch; the registration function's extension-table references; a string-driven hunt for the zone module; then a loader replay against **all 2,971 shipped `.czh_pc` files** with an exact-size check and a stride control. Evidence: `tools/ctorless.txt`, `zone_version.txt`, `czh_table.txt`, `zone_subsystem.txt`; harnesses `scratchpad/zone_bulk.py`, `czh_replay.py`, `czh_records.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. Magic values, offsets, strides, alignments and the accepted version range are load-bearing format data. Two literal engine strings are quoted because they are the subsystem's own self-identification — the same convention used for the "Mesh" and "Morph" blocks.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30)** — adversarial desk review (`review/adv_ctorless-types.md`), 9 units (§1–§8, with §5.1 separate): **DESK-PASS 1** (§7); **DESK-PASS, text fixes applied 5** (§1, §3, §5.1, §6, §8); **NEEDS-EXE 1** (§4); **NEEDS-DATA 1** (§2); **VALIDATED-BY-DATA 1** (§5: Team B's independent reader, `team-b/HANDOFF.md` §9.55 "First zone/streaming reader": "2,971/2,971 found and parsed", "276,012 total records, 2,342 zero-record / 629 non-zero, both exact matches to the spec"). A desk pass alone does not clear a unit: it means the text is internally consistent and agrees with the other specs, not that it was re-derived from the executable or data; only the VALIDATED-BY-DATA unit is already backed by Team B's full-population run. **Cross-spec conflict:** `spec-asm-format.md` §9.3 (CONFIRMED — empirical) shows the manifests list types 44/45 with real `.czh_pc` names and types 29/30 with `.czn_pc`, and every entry except type 39 contributes its file (§9.4, 8,057/8,057). The "zone module finds its own files" conclusion (§1, §2 row 44, §4) is therefore marked OPEN, and its CONFIRMED label is downgraded in place for the conclusion only. **Awaiting the executable:** §4 (addresses of the `.czn_*`/`.czh_*` tables and the functions that reference them, and how they map to `FUN_00863170`/`FUN_00863310`), plus the OPEN notes in §1 (how a manifest name reaches a type with no registered extension), §3 (null-destructor teardown) and §5 (header bytes `+0x20..+0x3F`, align-to-4 base). **Awaiting real data:** §2 (number of zero-byte `.gzn_pc`: 1 here vs 81 in `spec-zone-data-format.md` §7.1).

---

## 1. Headline results

- **The mechanism: a null constructor is not an error — it is a supported mode.** The generic dispatcher fetches the type's constructor pointer and, when it is null, **skips the call entirely, marks the resource loaded, and returns success**. The null check sits in both the paired and unpaired branches, identically. **[CONFIRMED — disassembly.]**
- **So constructor-less types are "load the bytes and stop" types.** The generic pipeline still resolves the filename, reads and decompresses the entry, and leaves a valid buffer registered against the resource; it simply never hands that buffer to a per-type parser. Whatever wants the data goes and fetches it from the resource system on its own schedule. **[CONFIRMED — disassembly.]** *(Desk review 2026-09-30: except type 39. Per `spec-resource-dispatch.md` §2 and §8.6 item 2, row flag bit `0x4` makes `FUN_00dd2a70` resolve no file, and only ID 39 sets it.)* **[OPEN — desk review 2026-09-30: types 44/45 register no extension (§4), so the registration row cannot supply their file name. `spec-asm-format.md` §9.3 shows the manifest entry supplies it (`.czh_pc`, `type_id` 44/45). How that name reaches the dispatcher for a type with no registered extension is not stated — to be settled against the executable.]**
- **The zone module does not use the registration table to find its files.** It carries its **own** per-platform extension tables and builds `.czh_pc` / `.czn_pc` filenames itself, in a function that references both tables and lives in the level/zone-layout module. The registration table's zone-header rows carry *no* extension at all, and its zone rows carry extensions but no constructor. ~~**[CONFIRMED — disassembly.]**~~ **[Label downgraded, desk review 2026-09-30: CONFIRMED — disassembly stays for the table references and the registration rows (§4). The conclusion that the zone module's own name-building is how the shipped files are found is OPEN. `spec-asm-format.md` §9.3 (CONFIRMED — empirical) shows the manifests list types 44/45 with real `.czh_pc` names and types 29/30 with `.czn_pc` (paired with `.gzn_pc`), and §9.4 shows every entry except type 39 contributes its file (8,057/8,057). So shipped zone files reach their types through manifest entries that carry the `type_id`. What the zone module's private tables are used for is OPEN — to be settled against the executable (§4).]**
- **The zone-header block is now fully resolved**: magic `SR3Z`, version range 27–29, fixed `0x40` header, then a 4-aligned array of `count × 14` bytes. The replay reproduces the **exact byte size of 2,971 / 2,971** files, and a stride control discriminates perfectly (§5). **[CONFIRMED — disassembly + empirical.]** *(Validated by data: `team-b/HANDOFF.md` §9.55 "First zone/streaming reader": "2,971/2,971 found and parsed", "276,012 total records, 2,342 zero-record / 629 non-zero, both exact matches to the spec".)*
- **Type 39 `Buffer` ships nothing at all.** Its registered extensions are empty strings, and no archive contains a single extensionless entry — consistent with a purely in-memory resource type that exists so runtime code can hold a buffer under the resource system's bookkeeping. **[CONFIRMED — empirical for the absence; HIGH CONFIDENCE — for the purpose.]** *(Desk review 2026-09-30: no file, but `spec-asm-format.md` §9.4 counts 2,447 type-39 manifest entries, each naming no file, so the type is used by shipped manifests.)*

**Review status (2026-09-30): DESK-PASS, text fixes applied ("except type 39"; zone-file-finding conclusion downgraded to OPEN per `spec-asm-format.md` §9.3; SR3Z bullet validated by data) — desk review (not re-derived from the executable).**

## 2. The five types, and what each actually ships

| ID | Name | Registered extensions | Constructor | Files shipped |
|---|---|---|---|---|
| 29 | Zone (fast) | `.czn_pc` / `.gzn_pc` | **none** | `.czn_pc` **2,971**, `.gzn_pc` **1,083** |
| 30 | Zone (slow) | `.czn_pc` / `.gzn_pc` (same tables) | **none** | *(shares the above)* |
| 39 | Buffer | `""` / `""` — empty strings, not null | **none** | **0** — no extensionless entry exists in any archive *(2,447 name-only manifest entries, `spec-asm-format.md` §9.4)* |
| 44 | Zone header (fast) | **none in the registration table** | **none** | `.czh_pc` **2,971** (found by the zone module's own table, §4) **[OPEN — desk review 2026-09-30: `spec-asm-format.md` §9.3 shows the manifests list type-44/45 entries under their real `.czh_pc` names; see §1.]** |
| 45 | Zone header (slow) | **none in the registration table** | **none** | *(shares the above)* |

Zones are split `fast`/`slow` at the type level while sharing extensions and file set — the distinction is a loading policy, not a format difference. **[HIGH CONFIDENCE — inferred from the shared extension tables and identical file set.]** *(Desk review 2026-09-30: the layout is shared, but the contents differ by suffix. `spec-world-streaming.md` §10.7 finds, over the 1,265 `sr3_city_0/1` headers, all 248 files with records are `~f` and all 603 `~s` have count 0. Whether types 44/45 map one-to-one to `~f`/`~s` is unmeasured; see §8 item 3.)*

Note the asymmetry: **2,971 `.czn_pc` and 2,971 `.czh_pc`, but only 1,083 `.gzn_pc`** — most zones have no `g`-side payload, and one shipped `.gzn_pc` is **0 bytes**. **[CONFIRMED — empirical.]** **[OPEN — desk review 2026-09-30: count conflict. `spec-zone-data-format.md` §7.1 says "1,083 `.gzn_pc` ship against 2,971 zones, and 81 are zero bytes" [CONFIRMED — empirical, controlled], and its 1,002/1,002 non-empty denominator is 1,083 − 81. This sentence says one. To be settled against real data: count zero-size `.gzn_pc` over all archives (and distinct contents).]** *(Validated by data, for the 2,971 counts only: `team-b/HANDOFF.md` §9.126, "all three independent measurements … now agree exactly at 2,971" for `.czn_pc`, and §9.55, "2,971/2,971 found and parsed" for `.czh_pc`.)*

**Review status (2026-09-30): NEEDS-DATA: number of zero-byte `.gzn_pc` (1 here vs 81 in `spec-zone-data-format.md` §7.1); the 2,971 counts are Team B-validated — desk review (not re-derived from the executable).**

## 3. The dispatcher's null-constructor path

The generic resource dispatcher (`spec-resource-dispatch.md` §4–5; the null-constructor path itself is documented in its §6a) reads the constructor pointer from the type's row, then branches on the paired-file flag *(desk review 2026-09-30: dispatcher `FUN_00dd2e30`, `spec-resource-dispatch.md` §4; the paired test is the per-entry flags byte `+0x15` bit `0x4`, its §8.6)*. **Both branches begin with the same null test, and both jump past the call to the shared success tail on null.** The tail sets the "loaded" bit and returns success. A constructor that *runs* and returns false is a failure; a constructor that is *absent* is a success. **[CONFIRMED — disassembly.]**

That single branch is the whole answer to the inventory's question. It also means the buffer's lifetime and residency are managed exactly as for any other resource — only the parse step is missing. **[HIGH CONFIDENCE — inferred; desk review 2026-09-30: the null-constructor branch does not show this. These five types also have a null destructor (`spec-format-inventory.md` §1), and how teardown handles a null destructor is OPEN — to be settled against the executable.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## 4. How zones actually find their files

Two distinct facts, both confirmed:

1. **The registration function references the `.czn_*` platform table twice** — once for each zone row (29, 30) — and **never references the `.czh_*` table at all**. Rows 44/45 really do have no extensions; this was re-checked against the full decompiled registration function after an initial suspicion that the project's table parser had missed them. It had not. **[CONFIRMED — disassembly.]**
2. **A function in the level/zone-layout module references *both* the `.czh_*` and `.czn_*` platform tables**, and a sibling function references the `.czh_*` table twice more. These are the only code references to the `.czh_*` table anywhere. The zone subsystem therefore composes its own filenames from its own tables. ~~**[CONFIRMED — disassembly.]**~~ **[Label downgraded, desk review 2026-09-30: CONFIRMED — disassembly stays for the table references. The "therefore" conclusion, read as how shipped `.czh_pc`/`.czn_pc` files are found, is OPEN: `spec-asm-format.md` §9.3/§9.4 shows the manifests list type 44/45 entries as `.czh_pc` and 29/30 as `.czn_pc`, and every non-39 entry contributes its file (8,057/8,057); see §1.]**

The `.czh_*` table is laid out exactly like the registration table's: five pointers in platform order (pc, ps2, ps3, xbox, xbox2), pointing at `.czh_pc`, `.czh_ps2`, `.czh_ps3`, `.czh_xbox`, `.czh_xbox2`. So it is the same convention, maintained privately by a different subsystem. **[CONFIRMED — empirical, read from the binary's data.]**

Corroborating, the binary ships the source path of that module — a literal string naming `world_zone_layout_build_shared.cpp` under a level directory — referenced from three functions in the same address neighbourhood. **[CONFIRMED — empirical; cited as shipped data, in the same way this project cites engine error strings.]**

**The authoring extension for zones is `.zonex`**, present as a literal in the binary — joining `.fmeshx`, `.animx`, `.effectx` and `.ctd` in this project's authoring-vs-shipped list. **[CONFIRMED — empirical.]**

**[OPEN — desk review 2026-09-30: this section gives no address for the `.czn_*`/`.czh_*` tables or for the functions that reference them. `spec-zone-data-format.md` §9.2 names `FUN_00863170`/`FUN_00863310` as `.zonex` filename helpers that swap authoring and shipped extensions, and `FUN_00863080` as the SR3Z parser; whether these are the functions meant here is not stated. To be settled against the executable: data references to both tables, and zero `.czh_*` references from the registration function `FUN_00700780`.]**

**Review status (2026-09-30): NEEDS-EXE: table and function addresses; role of the private tables given the manifest names — desk review (not re-derived from the executable).**

## 5. The zone-header block (`SR3Z`) — resolved

A `.czh_pc` file is: the shared **material-reference block** (`spec-terrain-format.md` §2 documents its name lists, including the `cc:N`-delimited texture and `.fmeshx` groups) → the **mandatory ~~16-byte~~ 1–16-byte pad, which always advances** (`spec-geometry-format.md` §3.1.1: a full 16 bytes when the block already ends 16-aligned, so not a round-up-to-16; wording corrected, desk review 2026-09-30) → the `SR3Z` block.

| Offset | Content | Confidence |
|---|---|---|
| `+0x00` | **Magic `SR3Z`** (`0x5A335253`) | **[CONFIRMED — disassembly + 2,971/2,971.]** |
| `+0x04` | **Version.** Accepted only in the inclusive range **27 … 29**; outside it the loader emits its own error naming the range and rejects the file. Every shipped file is **29** | **[CONFIRMED — disassembly + 2,971/2,971.]** |
| `+0x18` | Runtime pointer slot — fixed up to the record array, or explicitly zeroed when the count is zero. **Zero on disk in 2,971/2,971** | **[CONFIRMED — disassembly + empirical.]** |
| `+0x1C` | `u16` **record count** (0 … 3,701) | **[CONFIRMED — disassembly.]** |
| `+0x1E` | `u16`, small (2 and 3 dominate, 1,194 files each) | ~~**[OPEN.]**~~ **RESOLVED 2026-09-20 (`spec-world-streaming.md` §10.7): the zone type, the selector of the container kind** — the registration routine switches on it (`{1,3,8,10,11,13}` → Level Always Loaded, `{5,6}` → mission, `7` → Interior zone, `9` → Large interior zone, `12` → large mission, anything else incl. `2` → Zone); observed pairs match with 0 exceptions over all 2,971 `.czh_pc` files (Zone↔2: 1,194; Level Always Loaded↔3: 1,194). **[CONFIRMED — disassembly + empirical.]** |
| `+0x40` | End of the fixed header; **align to 4**, then `count × 14` bytes | **[CONFIRMED — disassembly.]** |

*(Desk review 2026-09-30: bytes not in the table above, gathered from this spec: `+0x08` holds the material-block pointer written by the loader at run time (paragraph below); `+0x0C..+0x17` hold three `f32` origin values (§8 item 2, `spec-world-streaming.md` §10.7); `+0x20..+0x3F` are OPEN (§8 item 2). "Every shipped file is 29": Team B's HANDOFF §9.55 quotes version 29 only for its 93-file spot-check ("version 29 in 93/93"); its full run reports "2,971/2,971 found and parsed" without a per-version tally.)* **[OPEN — desk review 2026-09-30: whether "align to 4" is relative to the file start or the `SR3Z` block start is not stated. It is a no-op whenever the block starts 16-aligned. To be settled against the executable (`FUN_00863080`), along with header bytes `+0x20..+0x3F`.]**

**Independently confirmed from the code side (2026-09-10, `spec-zone-data-format.md` §4):** the zone-header *file* reader validates the shared material block, applies **the same mandatory ~~16-byte~~ 1–16-byte, always-advancing pad arithmetic** as `spec-geometry-format.md` §3.1.1, then runs this parser and stores the material-block pointer at **`SR3Z + 0x08`** — reproducing by disassembly the layout established above by replay, and identifying one further field. **[CONFIRMED — disassembly.]**

The loader's error string states the version bounds explicitly and, unusually, suggests a remedy — that a stale file may be present or the layout must be re-built — which is why the range check is a range and not an equality like the Mesh block's.

**The replay is exact.** `material_block + mandatory_pad + 0x40 + align4 + count × 14 == file size` in **2,971 / 2,971** files, residual zero in every one; 276,012 records total; 2,342 files carry zero records.

**Control.** Re-running the same arithmetic with strides 12, 13, 15 and 16 matches 2,342/2,971 in each case — exactly the zero-record files, where the stride cannot matter. Restricted to the **629 files that actually contain records**, the correct stride 14 matches **629/629** and every wrong stride matches **0/629**. **[CONFIRMED — empirical, controlled.]**

*Validated by data (desk review 2026-09-30): `team-b/HANDOFF.md` §9.55 "First zone/streaming reader": "2,971/2,971 found and parsed", "276,012 total records, 2,342 zero-record / 629 non-zero, both exact matches to the spec"; the same section reproduces the stride control on a 93-file subset ("the correct stride (14) matches 93/93").*

**Review status (2026-09-30): VALIDATED-BY-DATA: 2,971/2,971 found and parsed, 276,012 records, 2,342/629 (Team B HANDOFF §9.55). Text fixes: pad wording, table gaps; header bytes `+0x20..+0x3F` and the align-to-4 base remain OPEN — desk review (not re-derived from the executable).**

### 5.1 The 14-byte record — measured, not resolved

276,012 records were profiled and the internal layout **did not fall out**, so nothing is asserted:

- **Not floats.** Read as `f32`, offsets 4 and 8 yield **zero** plausibly-scaled values out of 276,012 (magnitudes are ~1e38 garbage). Offset 0 yields 18,453 — still 7%.
- Bytes 6–9 are **predominantly zero** (215,847 / 232,596 / 210,834 / 227,266 of 276,012 respectively) — a sparse or optional region.
- `u16` at 0, 2, 4 and 10 are **full-range and high-entropy** (13k–23k distinct values, spanning 0…65,535) — hash-like or packed, not small enumerations. *(Desk review 2026-09-30: superseded in part by `spec-world-streaming.md` §10.7. `+0`, `+2`, `+4` are `s16` positions at 1/64 m, so "hash-like" is the wrong reading for them, since negative values show as large `u16`; `+12` is a `u16` name index; `+6` is an `s16` scaled by 2⁻¹² (HYPOTHESIS there); `+8`/`+10` are not read by that consumer.)*
- `u16` at **12 is bounded**: 4,638 distinct, maximum **8,593** — the only index-shaped field in the record.

**[OPEN.]** Resolving this needs the consumer that walks the array, which is inside the zone streaming subsystem — deliberately not opened here (§7). *(Partly answered since: see the note above and `spec-world-streaming.md` §10.7.)*

**Review status (2026-09-30): DESK-PASS, text fixes applied (forward pointer to `spec-world-streaming.md` §10.7) — desk review (not re-derived from the executable).**

## 6. Cross-format notes

- This is the **fourth** load-time pattern the project has catalogued: parse-in-constructor (most types), **stash-only constructor** (morph 11/12, csc 24, ctdg 42), **flag-variant thin constructor** (the PEG family, `spec-low-mips.md`), and now **no constructor at all**. Together they cover every registered type. *(Desk review 2026-09-30: `spec-extensionless-types.md` §3 numbers these as five shapes, adding "no extension", and its §1 also names "register-into-a-hash-table". `spec-format-inventory.md` §2 also lists types 9, 10, 13, 14 and 23 as stash-only. The pattern numbering is not unified across the specs.)*
- The material-reference block now appears in a seventh context (`.czh_pc`), further supporting `spec-geometry-format.md` §3.1's reading of it as a general-purpose embedded block rather than a per-format header.
- `.czh_pc` is the file `spec-terrain-format.md` §2 already partly documents; that section covers the material block's name lists, and §5 here covers everything after them. The two together account for the whole file.

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## 7. Scope note

The zone **streaming** subsystem — the function owning the literal strings *"zone header cache"* and *"zone always loaded"*, at roughly 7.6 KB of code, plus the two filename-composing functions at 2.7 KB and 3.4 KB — was **deliberately not opened**. It is a runtime subsystem, the category the user previously scoped out, and the structural question this pass was asked does not require it. Its entry points are recorded in the Ghidra bookmarks so a future pass starts from a known door.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 8. Open Items

1. The 14-byte record layout (§5.1) — needs the array's consumer.
2. ~~`SR3Z` `+0x1E`~~ **(`+0x1E` RESOLVED 2026-09-20, see the row above and `spec-world-streaming.md` §10.7; also `+0x0C..+0x17` = three `f32` world-space origin the record positions are relative to)**, and the header bytes between `+0x20` and `+0x40`.
3. What actually distinguishes `fast` from `slow` for zones and zone headers (a loading policy is the inference, not a finding). *(Partial observation: `spec-world-streaming.md` §10.7 finds, in `sr3_city_0/1`, records only in `~f` headers (248) and none in the 603 `~s` headers; added by desk review 2026-09-30.)*
4. `.czn_pc`'s own structure — investigated 2026-09-10 and **not resolved**; see `spec-zone-data-format.md`. Its leading `{id, length}` record is confirmed with a control (98.7% vs 2.6%), but a uniform chunk walk is **refuted** population-wide (0/2,971 reach EOF), and **no id value appears as an immediate anywhere in the binary** — the ids are data-driven, so there is no standalone parser to read. Completing it requires the zone streaming subsystem, i.e. a runtime pass. **[Update: `spec-zone-data-format.md` is now "PARTIALLY RESOLVED" — the zone geometry is readable (its §7, §10); the object/property stream remains open.]**
5. Why type 39 `Buffer` registers empty-string extensions rather than null ones — the distinction is deliberate in the registration data.
6. *(Added by desk review 2026-09-30.)* How shipped zone files listed in the manifests (`spec-asm-format.md` §9.3) reach types 44/45, which have no registered extension, and what the zone module's private `.czh_*`/`.czn_*` tables are used for (§1, §4).

**Review status (2026-09-30): DESK-PASS, text fixes applied (item 3 pointer, item 6 added) — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): added the `spec-resource-dispatch.md` §6a pointer for the null-constructor path (§3); noted in §6 item 4 that `spec-zone-data-format.md` now reads the zone geometry (object/property stream still open).
- 2026-09-30 (adversarial format desk review, `review/adv_ctorless-types.md`): review status summary plus a status line on 9 units (DESK-PASS 1, text fixes 5, NEEDS-EXE 1, NEEDS-DATA 1, VALIDATED-BY-DATA 1 via Team B HANDOFF §9.55 "2,971/2,971 found and parsed"). Marked OPEN, citing `spec-asm-format.md` §9.3/§9.4, the "zone module finds its own files" conclusion (§1, §2 row 44, §4), with the CONFIRMED label downgraded in place for the conclusion only. Marked OPEN the `.gzn_pc` zero-byte count (1 here vs 81 in `spec-zone-data-format.md` §7.1). Changed the pad wording from "16-byte" to "1–16-byte, always advances" per `spec-geometry-format.md` §3.1.1 (old text struck). Noted "except type 39" for file resolution, the 2,447 type-39 name-only manifest entries, the header bytes missing from the §5 table, and forward pointers to `spec-world-streaming.md` §10.7. Added OPEN notes for null-destructor teardown, the align-to-4 base and §4 addresses, and §8 item 6. Nothing deleted, no label raised.
