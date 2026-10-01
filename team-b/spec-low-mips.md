# Saints Row: The Third — Type 17 "Low Mips" (`.cvbl_pc`/`.gvbl_pc`): an Unused Registered Type

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, final item of the untouched-registered-types pass (`spec-format-inventory.md` §6).
**Read this first:** **this is not a file-format specification, because no file of this type ships.** *(Desk review 2026-09-30: read with the supersession note at the end of this line — no file with type 17's **registered** extensions `.cvbl_pc`/`.gvbl_pc` ships; type 17 itself is used by 109,219 manifest entries naming `.cvbm_pc`.)* It documents what type 17 *is*, what it would load, and the evidence that the mip-streaming path it belongs to carries no content in the PC build. It also corrects an inference this project had previously published about it. **[⚠ SUPERSEDED 2026-09-30 in part — `spec-asm-format.md` §9.3/§10.2 item 5 (CONFIRMED — empirical, 390,134/390,134 manifest entries): the shipped `.asm_pc` manifests type **109,219 entries as type 17**, every one naming a `.cvbm_pc` file (variant 0); none names `.cvbl_pc`. So no file with type 17's *registered* extension ships, but type 17 is used by shipped data and cannot be ignored.]**
**Method:** Registration row → constructor → the two sibling constructors on either side of it → the shared loader and its callers; plus an extension census over **all 38 shipped archives** and a direct inspection of `high_mips.vpp_pc`. Evidence: constructor, shared-loader and sibling-constructor dumps (evidence dump, not in repo); census and header-check harnesses (evidence dump, not in repo).
**Cleanroom compliance:** No decompiled code or original identifiers. Header field offsets and the magic are load-bearing format data.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

**Review status summary (2026-09-30):** an adversarial desk review (`review/adv_low-mips.md`) graded 9 units: **DESK-PASS 2** (§2, §6); **DESK-PASS-WITH-FIXES 4** (header block, §3, §4, §7 — the fixes are applied in place below, old text kept struck or annotated); **DESK-PASS-WITH-FIXES + VALIDATED-BY-DATA / NEEDS-DATA 1** (§1: bullet 1's zero `.cvbl_pc`/`.gvbl_pc` is backed by Team B's 405,694-entry census, `team-b/HANDOFF.md` §9.77; bullet 2 needs a header re-dump; bullet 3 needs the executable); **DESK-PASS-WITH-FIXES + NEEDS-DATA 1** (§5); **NEEDS-EXE 1** (§3.1). A desk pass alone does **not** clear a unit for implementation — it was checked against the other specs and Team B's files, not re-derived from the executable; the VALIDATED-BY-DATA part is the one already backed by Team B's full-population run. **Awaiting the executable / real data:** §1 bullets 2–3 (`high_mips.vpp_pc` header re-dump; the constructor trio's argument count and flag); §3 (`.cvbm_pc` c-file size histogram for the "104–132 bytes" range); §3.1 (constructors `0x005D7840`/`0x005D7870`/`0x005D78A0`, destructors `0x005D7990`/`0x005D79B0`, the shared loader's address); §5 (`0x164` across all containers); §7 item 1 (what runtime bit `0x2000` does downstream).

**Review status (2026-09-30), header block: DESK-PASS, text fixes applied (the read-first line qualified; repo-absent evidence paths replaced) — desk review (not re-derived from the executable).**

---

## 1. Headline results

- **No `.cvbl_pc` or `.gvbl_pc` file ships anywhere.** A census across all 38 archives finds **0** of either, against ~~**67,665 `.cvbm_pc` + 67,665 `.gvbm_pc`**~~ **67,710 `.cvbm_pc` + 67,710 `.gvbm_pc` entries** under the same type's alternate extensions *(corrected, desk review 2026-09-30: the census figure is 67,710 each — `spec-texture-format.md` §12.2, "`.cvbm_pc` 67,710, `.gvbm_pc` 67,710", and Team B `team-b/HANDOFF.md` §9.77, "67,710 each"; 67,665 is the number of reliable `.cvbm_pc` c-files parsed, 66,827 + 838 = 67,665, and adding the 45 mode-(a) `.cvbm_pc` entries excluded then (11 in `misc` + 34 in `patch_compressed`) gives 67,710)*. The registration declares extensions nothing uses. **[CONFIRMED — empirical, whole install.]**
- **`high_mips.vpp_pc` is an empty 2,048-byte stub.** Its own header says so: entry count `0`, directory-table size `0`, filename-table size `0`, total size `2048` — all internally consistent, and consistent with a normal archive's magic and version. **[CONFIRMED — empirical, read against the header layout in `spec-vpp-container.md` §1.]** *(Desk review 2026-09-30: the field offsets are those of `spec-vpp-container.md` §1 and are not restated here; this is a single-file reading with no Team B counterpart — re-dump pending.)*
- **Type 17 is the PEG texture loader with one boolean flipped.** Types 3 (`Vehicle PEG`), 17 (`Low Mips`) and 18 (`AL peg`) are three consecutive thin constructors, `0x30` bytes apart, all forwarding to the same shared loader with the same five arguments *(plus the trailing flag: six in all, as §3.1 lists — count clarified, desk review 2026-09-30)* and differing only in a trailing flag: **type 3 passes `0`, type 17 passes `1`**. Types 3 and 17 additionally **share a destructor**. **[CONFIRMED — disassembly.]**
- **Net: the low/high mip streaming split is implemented in the engine but carries no content in this build.** **[CONFIRMED — empirical + disassembly.]** **[⚠ SUPERSEDED 2026-09-30 — see the note in the header and `spec-asm-format.md` §9.3: 109,219 manifest entries are type 17 (`.cvbm_pc`).]**

**Review status (2026-09-30): VALIDATED-BY-DATA for bullet 1's zero (`team-b/HANDOFF.md` §9.77: no `.cvbl/.gvbl/.cvbh/.gvbh` over 405,694 entries; `.cvbm_pc`/`.gvbm_pc` 67,710 each); text fixes applied (67,665 → 67,710; five vs six arguments); NEEDS-DATA: `high_mips.vpp_pc` header re-dump (bullet 2); NEEDS-EXE: constructor trio (bullet 3) — desk review (not re-derived from the executable).**

## 2. The correction

`spec-format-inventory.md` §4 previously recorded, as **[HIGH CONFIDENCE — inferred]**, that type 17 is "the low-resolution mip-level half of the game's texture streaming system", and offered four supports: the name, the shared PEG destructor, the shared PEG size tables, and — as explicit corroboration — that "the game install ships a `high_mips.vpp_pc` archive, which would be the counterpart holding the high-resolution mip levels."

Testing it for the first time splits that claim in two:

- **The reading is now better supported than it was**, and for a reason the original did not have: the constructor is byte-for-byte the PEG constructor apart from a single flag argument. That is much stronger than a naming argument. **[CONFIRMED — disassembly.]**
- **The corroboration was hollow.** `high_mips.vpp_pc` exists but is empty. Its existence was cited as evidence for a counterpart holding high-resolution mips; it holds nothing. The inference survived, but the specific evidence offered for it does not, and **the conclusion should never have leaned on a filename**. **[CONFIRMED — empirical.]**

The §4 text also called this "a previously-undocumented **format pair**". That phrasing is now misleading: there is no format here to document, because there are no files. It is a registered *type* with an unused extension pair.

**Review status (2026-09-30): DESK-PASS (methodology; the "low-mip" meaning of the flag remains a reading from the name and flag, see §7 item 1) — desk review (not re-derived from the executable).**

## 3. What type 17 would load

Since the constructor forwards to the PEG loader, a `.cvbl_pc`/`.gvbl_pc` pair would be **PEG-format data** — the format already specified in `spec-texture-format.md`, whose header magic is the four bytes `G E K V` *(on-disk bytes `47 45 4B 56`, value `0x564B4547` — `spec-texture-format.md` §2)* and which splits metadata (`c`-side) from pixel payload (`g`-side). The shipped `.cvbm_pc`/`.gvbm_pc` files confirm the shape: 67,665 pairs *(desk review 2026-09-30: 67,665 is the count of reliable `.cvbm_pc` c-files parsed, not of pairs; the census is 67,710 entries each, §1)*, `c`-side 104–132 bytes **[OPEN — desk review 2026-09-30: no Team B figure or number in this spec fixes this range; `spec-texture-format.md` §12.3/§12.4 show 24-byte zero-texture c-files and multi-record files across `.cvbm_pc` + `.cpeg_pc` together, but do not split them by extension; to be settled against real data (size histogram of `.cvbm_pc` c-files by record count).]**, magic `GEKV`, each ending in its source `.tga` name. **[CONFIRMED — empirical + `spec-texture-format.md` §7.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (on-disk magic bytes; 67,665 relabelled; 104–132 B marked OPEN); the PEG/GEKV structure of the files type-17 entries name is VALIDATED-BY-DATA by Team B's PEG reader (70,524/70,524 c-files, 76,651/76,651 records, `team-b/STATE.md`) — desk review (not re-derived from the executable).**

### 3.1 The sibling trio

| Type | Name | Ctor | Flag | Destructor |
|---|---|---|---|---|
| 3 | Vehicle PEG | `0x005D7840` | **0** | `0x005D7990` |
| 17 | **Low Mips** | `0x005D7870` | **1** | `0x005D7990` *(shared with type 3)* |
| 18 | AL peg | `0x005D78A0` | **1** | `0x005D79B0` |

All three pass `(c_buffer, c_size, g_buffer, g_size, name, flag)` to the shared loader **[OPEN — desk review 2026-09-30: the shared loader's address is not given here; to be settled against the executable.]** — the six-argument paired-resource shape `spec-resource-dispatch.md` documents. Type 18 additionally calls one further routine and checks the loader's result; types 3 and 17 ignore it. **[CONFIRMED — disassembly.]**

The flag's meaning inside the loader was **not** traced to its effect on pixel data; what is established is that it is the *only* difference between the vehicle-texture path and the low-mips path. **[OPEN — what the flag changes downstream.]** *(Partly traced since: a non-zero flag ORs runtime bit `0x2000` into every record, `spec-texture-format.md` §10.7 (CONFIRMED — disassembly); what that bit then changes is still open.)*

**Review status (2026-09-30): NEEDS-EXE: re-decompile `0x005D7840`/`0x005D7870`/`0x005D78A0` and `0x005D7990`/`0x005D79B0` (flag byte, argument count, shared destructor, type 18's extra call) and record the shared loader's address — desk review (not re-derived from the executable).**

## 4. Why nothing ships (assessment)

The engine supports a two-tier texture residency scheme — a low-mip set always resident, a high-mip set streamed from a dedicated archive *(desk review 2026-09-30: the code side is in `spec-texture-format.md` §10 — the extension chooser `FUN_00dd4a50` and the "Mip Interleaving" merge `FUN_00dd42b0`, which joins `.cvbl_pc` to `.cvbm_pc` streams; the residency semantics themselves are inferred from names and the flag, not observed)* — and this build ships **neither tier separately**: no `.cvbl_pc`, and an empty `high_mips.vpp_pc`. ~~The textures that do ship (`.cvbm_pc`/`.gvbm_pc`) carry their full mip chains through the ordinary type-3 path.~~ **[Refuted 2026-09-30: per `spec-asm-format.md` §9.3, type-3 manifest entries name only `.cpeg_pc`; `.cvbm_pc` entries are typed 17 (109,219) or 16 (variant 1, 19,755).]** A console build, or an earlier PC build, plausibly used the split; this one does not. **[HYPOTHESIS — for the *why*; the *what* is confirmed.]**

Practical consequence for a reimplementation: ~~**type 17 can be ignored entirely**~~ **[SUPERSEDED 2026-09-30 — `spec-asm-format.md` §9.3/§10.2 item 5: shipped manifests load `.cvbm_pc` files as type 17, i.e. through the §3.1 constructor; a reimplementation must support it]**, and `high_mips.vpp_pc` can be treated as absent. Anything that enumerates archives should tolerate a well-formed container with zero entries — which this archive is, and which is a real edge case worth handling.

**Review status (2026-09-30): DESK-PASS, text fixes applied (code citations for the two-tier machinery); data check: Team B's container reader should yield an empty entry list for `high_mips.vpp_pc` — desk review (not re-derived from the executable).**

## 5. An incidental data point for an existing open item

`spec-vpp-container.md` §1 lists header field `0x164` as **OPEN / UNKNOWN**, noting it "does not fit a consistent formula". In `high_mips.vpp_pc` — a container with **zero entries and zero payload** — that field holds **3,936,256**. So whatever it is, **it is not derived from payload size, entry count, or directory size**, all of which are zero here. A small but genuinely narrowing constraint on that open item. **[CONFIRMED — empirical.]** *(Desk review 2026-09-30: 3,936,256 = `0x003C1000` = 1,922 × 2,048, i.e. a multiple of the 2,048-byte block; this is one container's value, with no population figure.)*

**Review status (2026-09-30): NEEDS-DATA: histogram of `0x164` over all containers, testing 2,048-alignment and relation to sizes; text fix applied (hex/alignment note, single-sample caveat) — desk review (not re-derived from the executable).**

## 6. Methodology notes worth keeping

1. **The existence of a file is not evidence of its contents.** The whole "high mips counterpart" argument rested on a filename in a directory listing. Two minutes of reading the file would have caught it at the time — and the claim had been carried, unchallenged and labelled HIGH CONFIDENCE, through several later passes.
2. **Inherited inferences deserve the same controls as new ones.** This claim was written by this project, not by a peer, which is precisely why it went unre-examined. A confidence label records what was known when it was written, not what is known now.
3. **A negative result can be a complete result.** "No files of this type ship, and here is why the type exists anyway" fully answers the question the inventory asked. It needed a census and three decompiled thin functions, not a format investigation.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 7. Open Items

1. What the loader flag actually changes (§3.1). *(Partly answered: it ORs runtime bit `0x2000` into every record, `spec-texture-format.md` §10.7; what bit `0x2000` does downstream is the remaining question.)*
2. Whether a console build ships `.cvbl_pc` files — untestable from this install.
3. ~~What type 18 ("AL peg") is for; it shares the flag value with type 17 but has its own destructor and an extra call. It remains the one texture-family type this project has not characterised.~~ **[Resolved: see `spec-texture-format.md` §8 ("AL" = always loaded).]**

**Review status (2026-09-30): DESK-PASS, text fixes applied (item 1 updated to `spec-texture-format.md` §10.7) — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the "nothing ships / type 17 can be ignored / `.cvbm_pc` goes through type 3" statements (header, §1, §4) superseded by `spec-asm-format.md` §9.3/§10.2 item 5 (109,219 type-17 `.cvbm_pc` manifest entries); marked §7 item 3 resolved (`spec-texture-format.md` §8).
- 2026-10-01 (adversarial format desk review, `review/adv_low-mips.md`): added the review status summary and a status line per unit (DESK-PASS 2, WITH-FIXES 4, §1 WITH-FIXES + VALIDATED-BY-DATA/NEEDS-DATA, §5 WITH-FIXES + NEEDS-DATA, NEEDS-EXE 1); §1 "67,665 + 67,665" corrected to the 67,710-each census (`spec-texture-format.md` §12.2, `team-b/HANDOFF.md` §9.77; 67,665 + 45 excluded = 67,710); "five arguments" clarified as five plus the flag; §3 GEKV on-disk bytes, "67,665 pairs" relabelled, "104–132 bytes" marked OPEN; §3.1 loader address OPEN; §3.1/§7 item 1 cite the `0x2000` runtime bit (§10.7); §4 code citations; §5 hex/alignment note; repo-absent evidence paths replaced with "(evidence dump, not in repo)"; no confidence label raised.
