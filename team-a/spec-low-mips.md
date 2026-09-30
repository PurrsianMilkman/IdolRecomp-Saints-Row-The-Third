# Saints Row: The Third — Type 17 "Low Mips" (`.cvbl_pc`/`.gvbl_pc`): an Unused Registered Type

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, final item of the untouched-registered-types pass (`spec-format-inventory.md` §6).
**Read this first:** **this is not a file-format specification, because no file of this type ships.** It documents what type 17 *is*, what it would load, and the evidence that the mip-streaming path it belongs to carries no content in the PC build. It also corrects an inference this project had previously published about it. **[⚠ SUPERSEDED 2026-09-30 in part — `spec-asm-format.md` §9.3/§10.2 item 5 (CONFIRMED — empirical, 390,134/390,134 manifest entries): the shipped `.asm_pc` manifests type **109,219 entries as type 17**, every one naming a `.cvbm_pc` file (variant 0); none names `.cvbl_pc`. So no file with type 17's *registered* extension ships, but type 17 is used by shipped data and cannot be ignored.]**
**Method:** Registration row → constructor → the two sibling constructors on either side of it → the shared loader and its callers; plus an extension census over **all 38 shipped archives** and a direct inspection of `high_mips.vpp_pc`. Evidence: `tools/vbl_ctor.txt`, `vbl_shared.txt`, `peg_siblings.txt`; harnesses `scratchpad/vbl_bulk.py`, `mips_check.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. Header field offsets and the magic are load-bearing format data.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **No `.cvbl_pc` or `.gvbl_pc` file ships anywhere.** A census across all 38 archives finds **0** of either, against **67,665 `.cvbm_pc` + 67,665 `.gvbm_pc`** under the same type's alternate extensions. The registration declares extensions nothing uses. **[CONFIRMED — empirical, whole install.]**
- **`high_mips.vpp_pc` is an empty 2,048-byte stub.** Its own header says so: entry count `0`, directory-table size `0`, filename-table size `0`, total size `2048` — all internally consistent, and consistent with a normal archive's magic and version. **[CONFIRMED — empirical, read against the header layout in `spec-vpp-container.md` §1.]**
- **Type 17 is the PEG texture loader with one boolean flipped.** Types 3 (`Vehicle PEG`), 17 (`Low Mips`) and 18 (`AL peg`) are three consecutive thin constructors, `0x30` bytes apart, all forwarding to the same shared loader with the same five arguments and differing only in a trailing flag: **type 3 passes `0`, type 17 passes `1`**. Types 3 and 17 additionally **share a destructor**. **[CONFIRMED — disassembly.]**
- **Net: the low/high mip streaming split is implemented in the engine but carries no content in this build.** **[CONFIRMED — empirical + disassembly.]** **[⚠ SUPERSEDED 2026-09-30 — see the note in the header and `spec-asm-format.md` §9.3: 109,219 manifest entries are type 17 (`.cvbm_pc`).]**

## 2. The correction

`spec-format-inventory.md` §4 previously recorded, as **[HIGH CONFIDENCE — inferred]**, that type 17 is "the low-resolution mip-level half of the game's texture streaming system", and offered four supports: the name, the shared PEG destructor, the shared PEG size tables, and — as explicit corroboration — that "the game install ships a `high_mips.vpp_pc` archive, which would be the counterpart holding the high-resolution mip levels."

Testing it for the first time splits that claim in two:

- **The reading is now better supported than it was**, and for a reason the original did not have: the constructor is byte-for-byte the PEG constructor apart from a single flag argument. That is much stronger than a naming argument. **[CONFIRMED — disassembly.]**
- **The corroboration was hollow.** `high_mips.vpp_pc` exists but is empty. Its existence was cited as evidence for a counterpart holding high-resolution mips; it holds nothing. The inference survived, but the specific evidence offered for it does not, and **the conclusion should never have leaned on a filename**. **[CONFIRMED — empirical.]**

The §4 text also called this "a previously-undocumented **format pair**". That phrasing is now misleading: there is no format here to document, because there are no files. It is a registered *type* with an unused extension pair.

## 3. What type 17 would load

Since the constructor forwards to the PEG loader, a `.cvbl_pc`/`.gvbl_pc` pair would be **PEG-format data** — the format already specified in `spec-texture-format.md`, whose header magic is the four bytes `G E K V` and which splits metadata (`c`-side) from pixel payload (`g`-side). The shipped `.cvbm_pc`/`.gvbm_pc` files confirm the shape: 67,665 pairs, `c`-side 104–132 bytes, magic `GEKV`, each ending in its source `.tga` name. **[CONFIRMED — empirical + `spec-texture-format.md` §7.]**

### 3.1 The sibling trio

| Type | Name | Ctor | Flag | Destructor |
|---|---|---|---|---|
| 3 | Vehicle PEG | `0x005D7840` | **0** | `0x005D7990` |
| 17 | **Low Mips** | `0x005D7870` | **1** | `0x005D7990` *(shared with type 3)* |
| 18 | AL peg | `0x005D78A0` | **1** | `0x005D79B0` |

All three pass `(c_buffer, c_size, g_buffer, g_size, name, flag)` to the shared loader — the six-argument paired-resource shape `spec-resource-dispatch.md` documents. Type 18 additionally calls one further routine and checks the loader's result; types 3 and 17 ignore it. **[CONFIRMED — disassembly.]**

The flag's meaning inside the loader was **not** traced to its effect on pixel data; what is established is that it is the *only* difference between the vehicle-texture path and the low-mips path. **[OPEN — what the flag changes downstream.]**

## 4. Why nothing ships (assessment)

The engine supports a two-tier texture residency scheme — a low-mip set always resident, a high-mip set streamed from a dedicated archive — and this build ships **neither tier separately**: no `.cvbl_pc`, and an empty `high_mips.vpp_pc`. ~~The textures that do ship (`.cvbm_pc`/`.gvbm_pc`) carry their full mip chains through the ordinary type-3 path.~~ **[Refuted 2026-09-30: per `spec-asm-format.md` §9.3, type-3 manifest entries name only `.cpeg_pc`; `.cvbm_pc` entries are typed 17 (109,219) or 16 (variant 1, 19,755).]** A console build, or an earlier PC build, plausibly used the split; this one does not. **[HYPOTHESIS — for the *why*; the *what* is confirmed.]**

Practical consequence for a reimplementation: ~~**type 17 can be ignored entirely**~~ **[SUPERSEDED 2026-09-30 — `spec-asm-format.md` §9.3/§10.2 item 5: shipped manifests load `.cvbm_pc` files as type 17, i.e. through the §3.1 constructor; a reimplementation must support it]**, and `high_mips.vpp_pc` can be treated as absent. Anything that enumerates archives should tolerate a well-formed container with zero entries — which this archive is, and which is a real edge case worth handling.

## 5. An incidental data point for an existing open item

`spec-vpp-container.md` §1 lists header field `0x164` as **OPEN / UNKNOWN**, noting it "does not fit a consistent formula". In `high_mips.vpp_pc` — a container with **zero entries and zero payload** — that field holds **3,936,256**. So whatever it is, **it is not derived from payload size, entry count, or directory size**, all of which are zero here. A small but genuinely narrowing constraint on that open item. **[CONFIRMED — empirical.]**

## 6. Methodology notes worth keeping

1. **The existence of a file is not evidence of its contents.** The whole "high mips counterpart" argument rested on a filename in a directory listing. Two minutes of reading the file would have caught it at the time — and the claim had been carried, unchallenged and labelled HIGH CONFIDENCE, through several later passes.
2. **Inherited inferences deserve the same controls as new ones.** This claim was written by this project, not by a peer, which is precisely why it went unre-examined. A confidence label records what was known when it was written, not what is known now.
3. **A negative result can be a complete result.** "No files of this type ship, and here is why the type exists anyway" fully answers the question the inventory asked. It needed a census and three decompiled thin functions, not a format investigation.

## 7. Open Items

1. What the loader flag actually changes (§3.1).
2. Whether a console build ships `.cvbl_pc` files — untestable from this install.
3. ~~What type 18 ("AL peg") is for; it shares the flag value with type 17 but has its own destructor and an extra call. It remains the one texture-family type this project has not characterised.~~ **[Resolved: see `spec-texture-format.md` §8 ("AL" = always loaded).]**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the "nothing ships / type 17 can be ignored / `.cvbm_pc` goes through type 3" statements (header, §1, §4) superseded by `spec-asm-format.md` §9.3/§10.2 item 5 (109,219 type-17 `.cvbm_pc` manifest entries); marked §7 item 3 resolved (`spec-texture-format.md` §8).
