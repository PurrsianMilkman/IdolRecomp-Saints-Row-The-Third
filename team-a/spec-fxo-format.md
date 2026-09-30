# Saints Row: The Third — `.fxo_pc` Compiled Shader Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, Target #6 (from `spec-output.md` §4 (unpublished Team A working document, not in this repository))
**Scope:** The custom wrapper structure around `.fxo_pc` compiled-shader files, and identification of the standard, publicly-documented shader bytecode format(s) it contains.
**Method:** Black-box extraction and byte-level inspection of `.fxo_pc` entries from `shaders.vpp_pc`, using the already-confirmed container format. `shaders.vpp_pc` is a mode-(a) compressed container (per `spec-vpp-container.md`), so — learning directly from that document's own caution — **only entry 0 of the archive was treated as a fully trusted sample this pass**; a second entry was inspected for corroboration only, explicitly flagged as unverified everywhere it's used, precisely because "decompression succeeded without error" is known not to guarantee correct content for non-first entries in this archive family. **[Superseded 2026-09-20: `spec-vpp-container.md` §7's physical-offset rule makes every entry reliable; see §10.]**
**Cleanroom compliance:** No decompiled code or internal identifiers appear below. The wrapper's magic number is an exact literal (load-bearing format data). The shader payload itself is identified as a specific version of a **public, Microsoft-documented format** (Direct3D 9 compiled shader bytecode, including its standard "CTAB"/constant-table sub-chunk) — naming and citing a public vendor format by its own well-known structure is a fact, not proprietary information, exactly as with zlib and Targa/PNG-style formats elsewhere in this project; the bytecode's own internals were not further reverse-engineered, since Microsoft's own public documentation already covers them.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**. Pass 2 (§6–§9, 2026-09-20) adds **CONFIRMED — disassembly**: read directly from the loader's decompilation in the game executable.

**Status note (2026-09-20):** §2, §3, §4 and §5 below are the first (black-box) pass. Where the disassembly pass (§6–§9) corrects them, the original claim is kept and struck through, followed by a bracketed pointer to the corrected statement. **The header is variable-size and now fully mapped (§7); the "third shader slot" is real but is the geometry-shader stage, not the dword at `0xB8` (§8); the first-pass "208-byte header" was 200 bytes plus 8 bytes of alignment padding (§7.2).**

---

## 1. Overview

A `.fxo_pc` file is a small custom wrapper bundling **one or more complete, independent, standard Direct3D 9 compiled shaders** (a vertex shader and a pixel shader, in the one fully-confirmed sample) back-to-back, preceded by a fixed-purpose header. The wrapper does not modify or reframe the shader bytecode itself in any way — each embedded shader is a byte-for-byte intact, self-terminating, independently valid Direct3D 9 shader blob, immediately recognizable by its own standard version token and internal structure. **[CONFIRMED — empirical, on the one fully-trusted sample; consistent with, but not independently re-confirmed on, the one additional lower-confidence sample checked.]** **[Pass 2, 2026-09-20: "preceded by a fixed-purpose header" is corrected — the header is variable-size (§7.2) — and the no-reframing claim is now also confirmed from the consumer side: the loader hands each blob to the graphics device unmodified (§6.4).]**

Entries seen carry a `rfg-` name prefix in some cases (e.g. `rfg-skybox-meteors.fxo_pc`), reinforcing the Phase 0 finding that this engine's shader set is shared/descended from Volition's earlier title *Red Faction: Guerrilla*.

## 2. Header

~~The confirmed sample's header runs from offset `0x00` to `0xD0` (208 bytes) before the first embedded shader begins. Only a few fields within it were pinned down with confidence; the rest are present, real, and consistently-shaped but not decoded field-by-field this pass.~~ **[Superseded by §7 (2026-09-20): the header is variable-size — 200 bytes on this sample, padded to 208 by the 16-byte blob alignment — and every field below is now decoded; see §7.1–§7.4. The rows are kept as the first-pass record, each corrected in place.]**

| Offset | Size | Field | Confidence |
|---|---|---|---|
| `0x00` | 4 | **Magic number, exact value `0x4B42A1EE`** (on-disk bytes `EE A1 42 4B`). | **CONFIRMED — empirical**, identical leading 4 bytes on both samples checked. *Exact literal required by the format.* **Now also CONFIRMED — disassembly (§6.3, §7.1): exactly one instruction in the executable tests it.** |
| `0x04` | 4 | ~~Read as `14` on the one fully-trusted sample. Likely a format version number (small integer, same general role as the version fields seen in the container and `.asm_pc` formats), but not independently confirmed.~~ **Version number.** The loader rejects any value below 14 and checks no upper bound; the sample holds exactly 14 (§6.3, §7.1). | ~~**HYPOTHESIS — unconfirmed.**~~ **CONFIRMED — disassembly.** |
| `0xA8` | 4 | **Byte length of the first embedded shader** (the vertex shader, in the one confirmed sample) — read as `732`, and independently confirmed correct by locating that shader's own standard end-of-bytecode marker exactly `732` bytes after its start. | **CONFIRMED — empirical, on the one fully-trusted sample only.** ~~Whether this field sits at this *absolute* file offset in general, or at some *fixed offset relative to where the first shader begins* (which would place it differently in a file with a differently-sized header), was **not** distinguishable from a single sample — the one additional (lower-confidence) sample checked has its first shader starting `64` bytes later in the file, and a naive same-absolute-offset read there did not land on a sensible length value, consistent with the offset being relative to a variable-size header rather than fixed. **Treat as offset-from-shader-start, not a fixed absolute offset, until confirmed on more samples.**~~ **[Resolved, §7.2: this is entry 0 of the vertex-shader table. Its file offset is `0x80 + 0x10·c0 + 8·(c1+c2+c3+c4)` — a function of the header's own counts (variable-size header), so it is neither fixed-absolute nor relative to the shader start; it is `0xA8` on this sample.]** **Also CONFIRMED — disassembly (§7.3).** |
| `0xB0` | 4 | **Byte length of the second embedded shader** (the pixel shader) — read as `464`, independently confirmed the same way as the field above. | **CONFIRMED — empirical, on the one fully-trusted sample only**. ~~Same caveat about absolute-vs-relative offset as above.~~ **[Resolved, §7.2: entry 0 of the pixel-shader table, at `0x80 + 0x10·c0 + 8·(c1+c2+c3+c4) + 8·(nVS+nMid)` — `0xB0` on this sample. Also CONFIRMED — disassembly.]** |
| `0xB8` | 4 | ~~Read as `0xFFFF0000` on the one confirmed sample. Plausibly a "slot not present" sentinel for a third shader-length slot the format supports but this sample doesn't use (the two confirmed length fields are 8 bytes apart, and this sits a further 8 bytes along in the same pattern) — e.g. reserved for a geometry/hull/domain shader slot in a later shader-model era, or simply unused in this title.~~ **Corrected (§7.3): this is not a shader-length slot. The dword at `0xB8` is the first dword of the first entry of the pass table T8 (the bytes `00 00 FF FF` are two s16 values: vertex-shader index 0, then −1). The header's real third stage table sits between the vertex and pixel tables and is the geometry-shader slot (§7.2, §8).** | ~~**HYPOTHESIS — unconfirmed.**~~ **CONFIRMED — disassembly + empirical (correction).** |
| everything else | — | ~~Present and consistently shaped (non-zero, non-random-looking values) but not decoded.~~ **Decoded — see §7.1–§7.4 (counts, flags, role bytes, nine variable tables). Residual unread fields are listed in §5 item 2.** | ~~**OPEN / UNKNOWN.**~~ **Largely CONFIRMED — disassembly.** |

## 3. Embedded Shader Blobs

Each embedded shader is a **complete, standard Direct3D 9 compiled shader**, identifiable and delimitable using only Microsoft's own public, documented tokens — no proprietary framing needed:

- **Start:** the standard 4-byte D3D9 shader version token (`0xFFFE` + major/minor version in the low bytes for a vertex shader, `0xFFFF` + version for a pixel shader — both public, documented constants). The one confirmed sample contains one `vs_3_0` (Shader Model 3.0 vertex shader) and one `ps_3_0` (Shader Model 3.0 pixel shader).
- **Immediately after the version token:** a standard D3D9 "comment" token whose payload begins with the FourCC `CTAB` — Microsoft's own public constant-table sub-chunk, describing the shader's constant/uniform variable bindings. Its own declared size (also part of the public format) was independently checked and matches where it actually ends in the file.
- **End:** the standard 4-byte D3D9 end-of-shader token (`0x0000FFFF`), confirmed to land exactly at the byte position implied by the header's declared length field (§2) for both embedded shaders in the confirmed sample, and to land exactly at end-of-file for the last shader in the file.
- **Alignment between shaders:** the second shader does not begin immediately after the first shader's end token — there's a small zero-padding gap, and the second shader's start is aligned to an 8-byte boundary (confirmed: first shader ends at a non-8-aligned position, the actual gap is exactly enough zero bytes to reach the next multiple of 8, and the second shader begins exactly there). **[CONFIRMED — empirical, on the one fully-trusted sample.]** **[Corrected, §7.5: the rule is 16-byte alignment, applied before every blob including the first (the first blob is at 208 with a 200-byte header). The observed gap is consistent with both 8 and 16, which is why the black-box pass read it as 8.]**

**None of this needs to be treated as proprietary or reverse-engineered further** — the version token layout, the `CTAB` chunk, and the end token are all part of Microsoft's own public Direct3D 9 shader bytecode specification. An implementation that wants the shaders themselves (rather than just locating them) can hand the identified byte range to any standard D3D9 shader disassembler/analysis tool.

## 4. Practical Extraction Recipe

Two viable approaches, in order of how much of the header needs to be trusted:

1. **Scan-based (robust to header fields not being fully understood):** starting right after the wrapper's fixed 8-byte lead-in (magic + the field at `0x04`), scan forward for the next occurrence of a valid D3D9 shader version token; that marks a shader's start. From there, scan forward for the matching end token (`0x0000FFFF`) to find where it ends. Repeat from the next 8-byte-aligned position after that until end of file. This worked cleanly on the one fully-trusted sample and doesn't depend on resolving the header's remaining unknown fields. **[Note, 2026-09-20: §7.2/§7.5 make scanning unnecessary. If it is used anyway, the lead-in is not "8 bytes" — the header is variable-size (200 bytes here) — and each next search should restart at the next 16-byte boundary, not the next 8-byte one.]**
2. ~~**Header-length-field-based (more direct, less validated):** read the two confirmed length fields (§2) to get each shader's exact size without scanning — confirmed correct on the one fully-trusted sample, but the exact offset convention (absolute vs. relative to a variable-size header) needs confirming on more samples before this can be trusted as a general rule.~~ **[Resolved and now the primary recipe (§7.2, §7.5, CONFIRMED — disassembly + empirical): compute `H = 0x80 + 0x10·c0 + 8·(c1+c2+c3+c4) + 8·(nVS+nMid+nPS) + 0x10·c8` from the counts at `0x0C`–`0x18` and `0x78`; read each stage table entry's length dword (vertex table first, then pixel table; the middle/geometry table's blobs, if any, sit between them); place blob 1 at `align16(H)` and each later blob at `align16(previous end)`. On the trusted sample this lands the last blob exactly on the end of the file.]**

## 5. Open Items

1. ~~**Whether the header's total size is fixed or variable**, and if variable, what determines it (the lower-confidence second sample's header appears to be 64 bytes longer than the confirmed sample's) — this also directly affects whether the two length fields in §2 sit at a fixed absolute offset or a fixed offset relative to the first shader's start.~~ **RESOLVED 2026-09-20 (§7.2, CONFIRMED — disassembly + empirical): variable-size; determined solely by nine counts (`H = 0x80 + 0x10·c0 + 8·(c1+c2+c3+c4) + 8·(nVS+nMid+nPS) + 0x10·c8`), first blob at `align16(H)`. The two length fields sit at count-determined positions (neither fixed-absolute nor shader-relative). The 64-byte difference in the second sample is consistent with more table entries (`H` = 264 or 272) — inferred only.**
2. ~~**The bulk of the header's remaining fields** (§2) — present and consistent in shape, not decoded.~~ **LARGELY RESOLVED 2026-09-20 (§7.1, §7.3, §7.4).** Decoded: magic, version, role-presence flags, all nine counts, the nine table layouts, the role index bytes. **Still OPEN, exactly:** (a) bytes `0x19`–`0x1F`, `0x6B`–`0x6F`, `0x7C` (never read by either build; zero in the sample); (b) flag bits `0x002`, `0x008` and `0x2000` upward (never tested); (c) what roles 0–9 mean, i.e. which render pass each is; (d) byte `+5` of every named-constant entry and bytes `+6`/`+0x0C` of a pass entry (never read); (e) the name behind the pass hash `0xF0506BEA`; (f) whether the constant tables T3 and T4 are consumed anywhere (no accessor found) and how constants are split between T1/T2 and T3/T4; (g) T0's field meanings are inferred, and T0/T2/T4/geometry/roles 0–9 are unexercised by the one trusted sample.
3. ~~**Whether a third shader slot (§2, offset `0xB8`) is ever actually used** — only the "not present" sentinel value was observed in the one confirmed sample.~~ **RESOLVED 2026-09-20 (§8, CONFIRMED — disassembly in both executables): `0xB8` is not a slot (§7.3). The real third table is the byte count at `0x17` and its 8-byte-entry table between the vertex and pixel tables; it is the geometry-shader slot, consumed only by the Direct3D 11 build's loader and inert in the Direct3D 9 build. No hull/domain/compute slot exists. Still OPEN: no file with a non-zero geometry count has been seen.** **[Team B reports 277/847 `.fxo_pc_dx11` with `nMid = 1` (§10; lead, not yet re-derived).]**
4. **Broader sampling.** Only one archive (`shaders.vpp_pc`) and effectively one fully-trusted entry (its first) were checked this pass, due to the container format's known mode-(a) limitation on non-first entries — this is a small sample for a format that likely has real variety (shaders with more or fewer embedded stages, different shader model versions across the DX9 vs. DX11 executable builds, etc.). **UPDATE 2026-09-20 (§9.1): a full census of all 38 archives plus `launcher.vpp_pc` found `.fxo_pc` entries only in `shaders.vpp_pc` (844 by name), so the count of reliable samples stays exactly 1 (entry 0); no mode-(b) or raw-sentinel container holds any. Still OPEN — and blocked only by the parked mode-(a) limitation, which this pass did not touch. Note the `.fxo_pc_dx11` family (847 names in the same archive) shares this wrapper (§8.2) and is equally unreadable past its container's first entry.** **[Lifted 2026-09-20, §10.]**

None of these gaps block the practical goal: given a `.fxo_pc` file, the scan-based recipe in §4 reliably locates and extracts every embedded shader as a complete, standard, independently-usable Direct3D 9 shader blob. **[2026-09-20: the count-based recipe (§4 item 2, §7.2, §7.5) now does the same without scanning, and is what the shipped loader itself does.]**

## 6. The Loader: Anchors, Call Chain, Per-Stage Handling (disassembly)

**Second pass, 2026-09-20 (disassembly).** Everything in §6–§9 comes from decompiling the real loader and its consumers in the game executable and then replaying the result against real bytes (§9). Addresses are virtual addresses in the default image of **`SaintsRowTheThird.exe` (the Direct3D 9 build)** unless stated otherwise; §8 also uses the separate Direct3D 11 executable in the same install. This pass **supersedes** the black-box header reading in §2 wherever the two differ; §2, §3, §4 and §5 are corrected in place with strike-through rather than rewritten.

### 6.1 The two anchors and what they resolved

| Anchor | Result | Confidence |
|---|---|---|
| The exact 32-bit literal `0x4B42A1EE` as an instruction operand | **Exactly 1 hit in the whole image**: a 32-bit compare of the first dword of a buffer against the literal, at `0x0048DC13`, inside the routine that begins at `0x0048DBF0`. | **CONFIRMED — disassembly** |
| The raw byte pattern `EE A1 42 4B` anywhere in the image (code or data) | **Exactly 1 hit**, at `0x0048DC15` — the immediate bytes of that same instruction. So there is no second loader and no data-side copy of the magic. | **CONFIRMED — disassembly** |
| The string `.fxo_pc` | One copy, at `0x0125AF84`. **Exactly 2 code references, both inside the routine at `0x00E4A750`** (the extension argument of one file-enumeration call, once before the loop and once at the loop's tail). No other defined string in the image contains `fxo`. | **CONFIRMED — disassembly** |

**Consequence (this settles which path loads it):** `.fxo_pc` is **not** one of the resource-type-dispatch types. The string is referenced by nothing except the routine at `0x00E4A750`, so no registration table points at it (consistent with `spec-format-inventory.md`, which does not list it). Shader files are loaded by the renderer's own shader-library bring-up, not by the generic resource loader.

### 6.2 Call chain

| Address | Role |
|---|---|
| `0x00E49250` | Renderer-library initialisation routine (called once, from `0x005E0C80`). If the shader arenas are not yet set up it calls `0x0048DA70` and then `0x00E4A750`. |
| `0x0048DA70` | Allocates the two shader arenas (default sizes `0x28000` and `0x440000` bytes when the caller supplies 0). |
| `0x00E4A750` | Enumerates **every file whose name ends in `.fxo_pc`** across the mounted file sources (enumerator `0x00DA9330`), up to **`0x400` (1,024) files**, and hands each to `0x00E4A3A0`. |
| `0x00E4A3A0` | Opens the file in binary-read mode, reads the **whole file into one buffer**, closes it, and calls `0x0048DBF0(name, buffer)`. **No length is passed.** |
| **`0x0048DBF0`** | **The loader/validator** — steps in §6.3. |
| `0x0048E150` | Run later, from the shader-library set-up `0x00E4A8F0`: creates the GPU shader object for every stage record (§6.4), then builds one "shader object" per loaded file (§6.5) and registers it in a 400-slot library via `0x00E4A590` (§6.6). |

### 6.3 What the loader does, in order (`0x0048DBF0`)

1. **Magic.** The first dword must equal `0x4B42A1EE`; otherwise the file is rejected as "not a shader file". **[CONFIRMED — disassembly]**
2. **Version.** The dword at `+0x04` is compared as a **signed** value against `14` (`0x0E`); anything **below 14** is rejected with a message that the shader is out of date and "needs to be recrunched". **No upper bound is checked.** The trusted sample carries exactly `14`, i.e. sits at the minimum. **[CONFIRMED — disassembly]** This resolves §2's "likely a version" guess for `0x04`.
3. **Header size.** The loader computes the byte size of the header from counts stored inside the header — **the header is variable-size** (formula in §7.2). **[CONFIRMED — disassembly]**
4. **Header copy.** It asks the shader arena for that many bytes (alignment argument 8) and copies **only the header** (not the shader blobs) into it. Every later fix-up happens on this copy.
5. **Pointer fix-up.** Into the copy's fixed part it writes nine table pointers, each followed by a zeroed dword, locating the variable tables (§7.1). Whatever the file stored at those positions is ignored.
6. **Per-stage records** (§6.4). The routine takes only two arguments — the name and the buffer — **so it cannot check any blob against the file length; the header's own sizes are trusted completely.** **[CONFIRMED — disassembly]**
7. **Registry.** The copy is appended to a global registry of at most **`0x400` (1,024)** entries, each `0x44` bytes: a `0x40`-byte name followed by the pointer to the header copy. If the registry is already full the routine returns failure. **[CONFIRMED — disassembly]**

### 6.4 How each stage blob is consumed

The loader walks the **vertex-shader table** (count = the signed byte at `+0x16`) and then the **pixel-shader table** (count = the signed byte at `+0x18`); for each entry:

- The entry's **first dword is the blob's byte length**; a length of `0` means "no shader here" and the entry is skipped.
- The blob starts at the **running file offset rounded up to a multiple of 16**, and the running offset then advances by the blob length. The running offset starts at the **unrounded header size** and carries on from the vertex entries into the pixel entries, so alignment padding sits *before* every blob, including the first. **[CONFIRMED — disassembly; CONFIRMED — empirical, §9]**
- Both counts are read as **signed bytes and tested with `> 0`**, so a count byte of `0x80` or more makes that stage's loop not run at all (effective maximum 127 per stage).
- **Identity/dedupe.** A CRC-32 of the blob's bytes (reflected polynomial `0xEDB88320`, ordinary 256-entry table, **initial value 0, no final inversion** — §7.6) is looked up in a 3,000-bucket chained hash map (bucket = CRC modulo 3000), one map for vertex blobs and one for pixel blobs. If a record with that CRC already exists (from any earlier file) it is **reused** and no new record is made. No byte-for-byte comparison of the blobs was seen — the equality test goes through a virtual comparator that was not decompiled, so **HIGH CONFIDENCE — inferred:** identity is the CRC alone.
- Otherwise a record is taken from a fixed pool (**3,000 records per stage kind, `0x50` bytes each**) and filled: `+0x00` blob length, `+0x04` pointer to a **private 16-byte-aligned copy** of the blob (made because the file buffer's alignment is not guaranteed), `+0x08` the CRC, `+0x0C` the source file's name (`0x40` bytes), `+0x4C` the GPU shader object (zero until created, below). Exhausting the pool prints a "too many vertex/pixel shaders" error naming the file.
- The table entry is then **overwritten in memory**: dword 0 becomes the record pointer, dword 1 becomes `0`. In the file, dword 1 of every stage-table entry is therefore ignored (it is `0` in the sample).
- **The middle table (count byte `+0x17`) is not walked at all by this Direct3D 9 loader** — it adds to the header size (§7.2) but yields no records or blobs here. See §8 for what it is.

**Hand-off to the GPU (routine `0x0048E150`, run after every file has been read).** For every vertex record it calls the device's shader-creation method through **slot 91 of the public `IDirect3DDevice9` interface (byte offset `0x16C`, `CreateVertexShader`)** via the small wrapper at `0x004A8790`; for every pixel record, **slot 106 (`0x1A8`, `CreatePixelShader`)** via `0x004A8810`. **The bytecode argument is the record's copy pointer — the blob exactly as it sat in the file, from its version token onward, unmodified.** The returned object is stored in the record at `+0x4C`. An out-of-memory result and any other failure each print a distinct error naming the file. **[CONFIRMED — disassembly]** This is direct proof, from the consumer side, of §3's black-box claim that the wrapper does not reframe the bytecode.

### 6.5 The per-file "shader object" (how the header tables are read)

`0x0048E150` makes one small object per loaded file (a class whose method table is at `0x012A1254`; init routine `0x0048EFE0`), keeping the header pointer at object `+0x04`. Init builds a **sampler-state block** from table T0 (§7.3), shared with any earlier file that produced an identical block (a global array of up to 128 blocks of `0x510` bytes, compared by `0x0048ED90`), and computes a constant-register span from table T1 (the maximum, over T1 entries, of `register + registers-per-element·elements − 1`, floored at 0, plus 1 — so at least 1). The object's other methods are small accessors over the header tables and are the source of most of §7's field meanings:

| Method slot | Routine | Does |
|---|---|---|
| `+0x14` | `0x0048F7C0` | Selects a pass by role number (§7.4) and tells the render context to use it. |
| `+0x18` | `0x0048F5E0` | Returns the s16 at header `+0x0E` (T1 count). |
| `+0x1C` / `+0x20` | `0x0048F5F0` / `0x0048F630` | T1 lookup **by hash**: returns the entry's byte at `+4` / the product of its bytes at `+6` and `+7`; `-1` if absent. |
| `+0x24` | `0x0048EE50` | Returns the register span computed at init. |
| `+0x28` / `+0x2C` / `+0x30` | `0x0048F680` / `0x0048F690` / `0x0048F6D0` | The same three operations for **T2** (count at `+0x10`). |
| `+0x34` | `0x0048F760` | Returns the u32 at header `+0x78` (T8 count). |
| `+0x38` | `0x0048F890` | "Is role k present?" — tests the flags dword at `+0x08` (§7.4). |
| `+0x3C` | `0x0048F770` | T8 lookup by hash within the indexed group; returns the index relative to the group base, `-1` if absent. |
| `+0x40` | `0x0048F720` | **T0** lookup by hash: returns the entry's byte at `+4`; `-1` if absent. |

The render context's bind method (`0x00474890`) caches the (object, pass index) pair and, when it changes, calls `0x0048F830`, which takes the T8 entry for that pass index (the index clamped to the last T8 entry) and fetches its vertex-stage and pixel-stage records (§7.3, T8). **Nothing else that reads the header tables was found through this chain; in particular no accessor exists for T3 or T4** (§7.3).

### 6.6 File-name convention seen at registration (context only — not in the file's bytes)

The library-registration routine (`0x00E4A590`, with the name parser at `0x00E836C0`) lower-cases the file name and tests its tail against a table of **13 suffixes**: `_s`, `_bs`, `_ms`, `_bms` (class 1, variants 0–3 in the order 0, 1, 2, 3); `_c`, `_mc`, `_bc`, `_bmc` (class 2, variants 0, 2, 1, 3); `_t`, `_ts` (class 3, variants 0, 2); `_fd` (class 4, variant 0); `_v`, `_mv` (class 5, variants 0, 2). The suffix picks a slot (`class·4 + variant`) in an entry of up to **400** library entries (`0x6C` bytes each) keyed by the CRC of the remaining stem; a name with no matching suffix is class 0, variant 0. This explains the `_bs`/`_c`/`_mc`/`_ms`/`_mv` name patterns noted in `spec-output.md`. **It is a property of the file name; nothing in the file's bytes encodes it.** **[CONFIRMED — disassembly for the table; that the tail test runs on an extension-less name is HIGH CONFIDENCE — inferred.]**

## 7. Decoded Header Layout (supersedes Section 2)

This section is the decoded header. It supersedes §2's table.

### 7.1 Fixed part, `0x00`–`0x7F` (always present, 128 bytes)

| Offset | Size | Field | How established |
|---|---|---|---|
| `0x00` | u32 | Magic `0x4B42A1EE` | **CONFIRMED — disassembly** |
| `0x04` | u32 | Version; the loader accepts **≥ 14** (§6.3). Sample: `14`. | **CONFIRMED — disassembly** |
| `0x08` | u32 | **Role-presence flags** (§7.4). Sample: `0x00000100`. | **CONFIRMED — disassembly + empirical** for bits `0x001 0x004 0x010 0x020 0x040 0x080 0x100 0x200 0x400 0x800 0x1000`; bits `0x002`, `0x008` and `0x2000` upward are never tested by anything traced — **OPEN** |
| `0x0C` | s16 | **Count of T0** entries (sampler-state records, `0x10` bytes each). Sample: 0. | **CONFIRMED — disassembly** |
| `0x0E` | s16 | **Count of T1** entries (named constants, 8 bytes each). Sample: 1. | **CONFIRMED — disassembly + empirical** |
| `0x10` | s16 | **Count of T2** entries (named constants, 8 bytes each). Sample: 0. | **CONFIRMED — disassembly** |
| `0x12` | s16 | **Count of T3** entries (named constants, 8 bytes each). Sample: 4. | **CONFIRMED — disassembly + empirical** |
| `0x14` | s16 | **Count of T4** entries (8 bytes each). Sample: 0. | **CONFIRMED — disassembly** |
| `0x16` | s8 | **Vertex-shader count.** Sample: 1. | **CONFIRMED — disassembly + empirical** |
| `0x17` | s8 | **Middle-table count** — the geometry-shader slot, consumed only by the Direct3D 11 build (§8). Sample: 0. | **CONFIRMED — disassembly** |
| `0x18` | s8 | **Pixel-shader count.** Sample: 1. | **CONFIRMED — disassembly + empirical** |
| `0x19`–`0x1F` | 7 bytes | Never read by anything traced (either build). Zero in the sample. | **OPEN** |
| `0x20`–`0x5F` | 64 bytes | **Runtime pointer slots.** In the in-memory copy the loader overwrites the pairs `0x20/0x24` (vertex table), `0x28/0x2C` (middle table), `0x30/0x34` (pixel table), `0x38/0x3C` (T0), `0x40/0x44` (T1), `0x48/0x4C` (T2), `0x50/0x54` (T3), `0x58/0x5C` (T4) with *(pointer, 0)*. Whatever the file holds here is ignored; the sample holds zeros. | **CONFIRMED — disassembly** |
| `0x60`–`0x69` | 10 × s8 | **T8 index for roles 0–9**; `-1` (`0xFF`) = role absent (§7.4). Sample: all `0xFF`. | **CONFIRMED — disassembly + empirical** |
| `0x6A` | s8 | **Base T8 index of the "indexed group"** (role 10). Sample: `0`. | **CONFIRMED — disassembly + empirical** |
| `0x6B`–`0x6F` | 5 bytes | Never read by anything traced. Zero in the sample. | **OPEN** |
| `0x70`–`0x77` | 8 bytes | Runtime pointer slot for T8 (*(pointer, 0)*), same treatment as `0x20`–`0x5F`. | **CONFIRMED — disassembly** |
| `0x78` | u32 | **Count of T8** entries (`0x10` bytes each). Sample: 1. | **CONFIRMED — disassembly + empirical** |
| `0x7C` | u32 | Never read. Zero in the sample. | **OPEN** |

### 7.2 The variable part and the header-size formula — this answers §5 item 1

Immediately after the fixed part, at `0x80`, **nine tables follow back to back in this order**, each holding as many entries as its count says (a zero-count table occupies no bytes):

`T0` (`0x10`-byte entries) → `T1` → `T2` → `T3` → `T4` (8-byte entries each) → **vertex-shader table** → **middle table** → **pixel-shader table** (8-byte entries each) → `T8` (`0x10`-byte entries).

> **Header size `H = 0x80 + 0x10·c0 + 8·(c1 + c2 + c3 + c4) + 8·(nVS + nMid + nPS) + 0x10·c8`**, where `c0…c4` are the five s16 counts at `0x0C`–`0x14`, `nVS/nMid/nPS` the three byte counts at `0x16`–`0x18`, and `c8` the u32 at `0x78`. **The first shader blob starts at `H` rounded up to a multiple of 16.** **[CONFIRMED — disassembly (the loader computes exactly this, uses it as the size of the header copy, and starts its running offset from it)]**

**So the header is variable-size, and nothing but those nine counts determines its size.** For the trusted sample: `c0=0, c1=1, c2=0, c3=4, c4=0, nVS=1, nMid=0, nPS=1, c8=1` → `H = 0x80 + 0 + 8·5 + 8·2 + 0x10 = 0xC8 = 200`, so the first blob is at `208 = 0xD0`. **§2's "208-byte header" was therefore 200 bytes of header plus 8 bytes of alignment padding.** The "length fields" at `0xA8`/`0xB0` are the first entries of the vertex-shader table (which starts at `0x80 + 0x10·c0 + 8·(c1+c2+c3+c4) = 0xA8` here) and of the pixel-shader table (`0xB0` here, after the one vertex entry). **They are neither at a fixed absolute offset nor "relative to the shader start": their position is a function of the counts.** In a file with more constant-table entries they sit later. That is exactly what §2's lower-confidence second sample showed (first blob `64` bytes later, i.e. `align16(H) = 272`, so `H = 264` or `272` — `H` is always a multiple of 8); **that reading is HIGH CONFIDENCE — inferred, not re-verified, because that sample is not trustworthy** (§9).

### 7.3 Entry layouts

**Vertex-/middle-/pixel-shader table entry (8 bytes)** — **CONFIRMED — disassembly + empirical (vertex and pixel; 2/2)**

| Offset | Size | Field |
|---|---|---|
| `+0x00` | u32 | **Blob length in bytes.** `0` = no shader in this entry. |
| `+0x04` | u32 | Ignored on load (the loader overwrites it with `0`). Zero in the sample. |

**T1 / T2 / T3 / T4 entry (8 bytes) — named shader constants**

| Offset | Size | Field | Status |
|---|---|---|---|
| `+0x00` | u32 | **CRC-32 of the constant's name, lower-cased** (§7.6). | **CONFIRMED — empirical, 5/5** (§9) |
| `+0x04` | u8 | **First constant-register index.** | **CONFIRMED — empirical, 5/5**, against the bytecode's own public constant table |
| `+0x05` | u8 | Not read by any accessor. `0` on 5/5. | **OPEN** |
| `+0x06` | u8 | **Registers per element**, as declared. | **CONFIRMED — disassembly** (the accessor multiplies `+6·+7`); on the sample 4 of 5 equal the bytecode's register count and the fifth (a 4×4 matrix of which the compiled shader uses 3 rows) reads 4 against 3 — so this is the **declared** footprint, not the compiled one |
| `+0x07` | u8 | **Element count** (array length). `1` on 5/5. | **CONFIRMED — disassembly** |

- **T1 and T2 are read through accessors** (lookup by hash → register; lookup by hash → `+6·+7`), and T1 additionally defines the register span (§6.5). **T3 and T4 have no accessor among the object's method slots and no other reference was found**, so **whether T3 and T4 are consumed anywhere in this build is OPEN** (they are still *counted* into `H`, so they must be present and well-formed).
- **Which constant goes in which table is not decided.** On the sample, T1 holds one pixel-stage constant at register 0 (`Meteor_strength`); T3 holds three vertex-stage constants (`objTM` at 32, `projTM` at 28, `Time` at 40) **and** one pixel-stage constant (`Tint_color` at 37). **HYPOTHESIS — unconfirmed:** T1/T2 are per-material parameter banks and T3/T4 are engine-supplied constants. T4's entry layout is only **HIGH CONFIDENCE — inferred** from its 8-byte size (it is empty in the sample).

**T0 entry (`0x10` bytes) — sampler state.** Layout **CONFIRMED — disassembly**; meaning **HIGH CONFIDENCE — inferred**; **not validated on real bytes (the sample has zero T0 entries).**

| Offset | Size | Field |
|---|---|---|
| `+0x00` | u32 | Name hash of the sampler (matched by the `+0x40` lookup accessor). |
| `+0x04` | u8 | Sampler slot number (what that accessor returns). |
| `+0x05` | u8 | Not read. |
| `+0x06`, `+0x07`, `+0x08` | u8 ×3 | Address-mode selector for U, V, W, mapped through a small table: `0→1, 1→1, 2→2, 3→3, 4→4, 5→1`; `6` = "leave unset". The outputs are exactly Direct3D's wrap / mirror / clamp / border numbering (1/2/3/4). |
| `+0x09`, `+0x0A` | u8 ×2 | Minification / magnification filter selector: `0→3, 1→0, 2→1, 3→2, 4→3`; `5` = unset. (Outputs are Direct3D's none / point / linear / anisotropic numbering, 0/1/2/3.) |
| `+0x0B` | u8 | Mip-filter selector: `0→2, 1→0, 2→1, 3→2, 4→3`; `5` = unset. |
| `+0x0C` | u8 | **Border-colour selector**, consulted **only if some address mode mapped to "border" (4)**: `0` → `0xFFFFFFFF`, `1` → `0x00000000`, `2` → `0xFF000000`. |
| `+0x0D`–`+0x0F` | 3 bytes | Not read. |

The block built from these entries has at most 128 sub-records and is hashed and shared between files with identical blocks (§6.5).

**T8 entry (`0x10` bytes) — one "pass".** The fields marked CONFIRMED below are so by disassembly of the consumer `0x0048F830` and, for the sample, by empirical check.

| Offset | Size | Field | Status |
|---|---|---|---|
| `+0x00` | s16 | **Index into the vertex-shader table** of the vertex stage this pass uses; `-1` = none. Sample `0` (its one vertex shader). | **CONFIRMED** |
| `+0x02` | s16 | Not read by the Direct3D 9 consumer. Sample `-1`. Its position — between the vertex index and the pixel index, in the same order as the three header tables — suggests an index into the **middle (geometry) table**. | **HYPOTHESIS — unconfirmed** |
| `+0x04` | s16 | **Index into the pixel-shader table**; `-1` = none. Sample `0` (its one pixel shader). | **CONFIRMED** |
| `+0x06` | s16 | Not read. Sample `0`. | **OPEN** |
| `+0x08` | u32 | **Pass-name hash**, matched by the `+0x3C` lookup accessor against a caller-supplied hash. Sample: `0xF0506BEA`. The name that produces it was **not** found (a short list of obvious candidates — the file stem, `default`, `main`, `pass0`, the three constant names, and similar — was tried against the lower-cased and raw CRC forms, with and without inversion, and none matched). | **CONFIRMED** as a hash key; preimage **OPEN** |
| `+0x0C` | u32 | Not read on the traced path. Sample `0x00000200`. | **OPEN** |

**What §2 called "`0xB8 = 0xFFFF0000`" is the first dword of T8's first entry** — the bytes `00 00 FF FF` are the two s16 values *(vertex index 0, then −1)* — not a length slot. **It is therefore not a "third shader slot not present" sentinel.**

### 7.4 The role/flags mechanism (header `0x08`, `0x60`–`0x6A`)

A shader file can expose up to **11 "roles"**: ten fixed ones (0–9) and one "indexed group" (role 10, a run of consecutive T8 passes). The flags dword says which roles exist; the byte array says where each lives in T8. **[CONFIRMED — disassembly (`0x0048F890`, `0x0048F9E0`); CONFIRMED — empirical on the sample, 11/11 consistent]**

| Role k | Flag bit that must be set | Index byte |
|---|---|---|
| 0 | `0x001` | `0x60` |
| 1 | `0x200` | `0x61` |
| 2 | `0x004` | `0x62` |
| 3 | `0x400` | `0x63` |
| 4 | `0x010` | `0x64` |
| 5 | `0x020` | `0x65` |
| 6 | `0x040` | `0x66` |
| 7 | `0x080` | `0x67` |
| 8 | `0x800` | `0x68` |
| 9 | `0x1000` | `0x69` |
| 10 (member *n* of the indexed group) | `0x100`, **and** the base byte ≥ 0, **and** base + *n* < `c8` | `0x6A` = base; the T8 index used is base + *n* |

For roles 0–9 the presence test is the flag bit alone; the index byte is then resolved to a T8 index, with **`-1` or an out-of-range value falling back to T8 entry 0**. The sample: flags `0x100`, bytes `0x60`–`0x69` all `0xFF` (ten absent roles, ten clear bits), byte `0x6A` = `0` → its single T8 entry is member 0 of the indexed group. **What roles 0–9 mean (which render pass or technique each is) is OPEN** — the sample uses none of them, and the caller that picks a role was not traced beyond the render-context bind (§6.5).

### 7.5 Blob placement (replaces §3's alignment bullet and §4 item 2)

Blobs follow the header in the order **all vertex-shader blobs (table order), then all pixel-shader blobs**, each starting at the running offset rounded up to a **multiple of 16** (§6.4). On the trusted sample: `H = 200` → vertex blob at `208` (length 732) → ends `940` → pixel blob at `944` (length 464) → ends `1408`, **exactly the file length**. **The alignment is 16, not 8**: the first blob starts at `208` although `H = 200` is already 8-aligned, which an 8-byte rule could not produce. (§3's 8-byte reading was a coincidence at the second blob — `940` rounds to `944` under either rule.) **[CONFIRMED — disassembly + empirical]**

### 7.6 Hash primitives (exact, so a reader can reproduce them)

Both are the ordinary table-driven CRC-32 step `crc = (crc >> 8) XOR table[(byte XOR crc) & 0xFF]` with the standard reflected table (`table[1] = 0x77073096`, `table[2] = 0xEE0E612C`, `table[3] = 0x990951BA`; the table sits at `0x01320DA0`), **starting from 0 (or a caller-supplied seed) with no final inversion**: (a) over the raw bytes — used for blob identity (routine `0x00D9E6C0`); (b) over the **lower-cased** ASCII name — used for the constant, sampler and pass name hashes (routine `0x00D9E740`). **[CONFIRMED — disassembly + empirical, §9]**

## 8. The Third Stage Slot: the Geometry Shader (answers Section 5 item 3)

### 8.1 What the Direct3D 9 build shows

The header does carry a **third stage table**: a byte count at `+0x17` and 8-byte entries, positioned **between** the vertex table and the pixel table (§7.1, §7.2). The Direct3D 9 loader adds its size into `H` but **never creates records or reads blobs for it** (§6.4), and the Direct3D 9 device interface has no fourth programmable stage to give it to. So in that build the slot is present and inert. **`0xB8` is not this slot** (§7.3): it is the first dword of a pass entry.

### 8.2 What the Direct3D 11 build shows — the slot is the geometry-shader stage

The install ships a second executable, `SaintsRowTheThird_DX11.exe`. It carries **the same wrapper loader** — CONFIRMED — disassembly (addresses below are in that executable's own image):

| Fact | Evidence |
|---|---|
| Same magic, same version floor, same header-size formula, same nine-pointer fix-up, same 16-byte alignment, same CRC-32 identity | Loader at `0x004C22C0`; the raw pattern `EE A1 42 4B` occurs exactly once in that image, at `0x004C22E5` (the immediate of the magic compare inside that routine). Its CRC routine (`0x00DA28A0`, table at `0x04729010` beginning `0, 0x77073096, 0xEE0E612C, 0x990951BA`) is the same step as §7.6. |
| It loads **`.fxo_pc_dx11`** files, not `.fxo_pc` | The string `.fxo_pc_dx11` at `0x01258824` has exactly 2 code references, both in the enumerator `0x00E2C490` (the same shape as §6.2's `0x00E4A750`), which calls `0x00E2C060` → `0x004C22C0`. No standalone `.fxo_pc` string exists in that image. |
| **One extra loop**: between the vertex loop and the pixel loop it walks the **middle table** (count byte `+0x17`, table pointer at `+0x28`) exactly as it walks the others — length dword, 16-byte alignment, CRC-32 dedupe, private copy, a pool of `0x50`-byte records (pool size not read) — and its failure message says it ran out of **geometry shaders** | Decompilation of `0x004C22C0`. |
| The GPU-object walk (`0x004C29A0`) makes **three** passes — vertex, geometry, pixel — through wrappers that call the Direct3D 11 device's creation methods at **slot 12** (`0x004CCAF0`, byte offset `0x30`, `CreateVertexShader`), **slot 13** (`0x004CCB70`, offset `0x34`, `CreateGeometryShader`) and **slot 15** (`0x004CCBF0`, offset `0x3C`, `CreatePixelShader`) | Raw disassembly of the three wrappers: each pushes the record's blob pointer (`+4`), its length (`+0`), a null class-linkage pointer and a result slot that is stored back at record `+0x4C`. |

**Conclusion — CONFIRMED — disassembly (both executables):** the header's three stage tables are **vertex, geometry, pixel, in that order**, and the file's blobs follow in the same order — all vertex blobs, then all geometry blobs, then all pixel blobs, each starting on a 16-byte boundary. **The third slot is the geometry stage; it is real but consumed only by the Direct3D 11 build.** Neither loader reads any byte count for a hull, domain or compute stage (the byte counts at `0x19`–`0x1F` are unread by both), so **a slot for those stages does not exist in this wrapper.**

Two consequences worth stating precisely. (a) **The wrapper is shared between the two graphics back ends; only the payload differs** (Direct3D 9 token streams in `.fxo_pc`, Direct3D 11 stage programs in `.fxo_pc_dx11`; the latter payload identification is **HIGH CONFIDENCE — inferred** from the Direct3D 11 creation calls taking a pointer plus a length, not checked against sample bytes). (b) A wrapper with a non-zero geometry count fed to the Direct3D 9 loader would put that loader's pixel blobs at the wrong offsets, since it does not step over geometry blobs; the two file families are never mixed, so this is a property of the code, not an observed failure.

### 8.3 What is still open here

- **No file with a non-zero middle count has been seen.** The one trusted sample has 0, and the `.fxo_pc_dx11` entries in `shaders.vpp_pc` (847 by name) are non-first entries of a mode-(a) container, so they are not readable (§9). How often the geometry slot is used, and by which shaders, is **OPEN.** **[Lifted 2026-09-20, §10: these entries are now readable; Team B reports 277/847 with `nMid = 1` (lead, not yet re-derived).]**
- **T8 entry `+0x02` as a geometry-table index** (§7.3) is **HYPOTHESIS — unconfirmed**: the Direct3D 11 pass-bind routine was not traced.

## 9. Validation Against Real Bytes

Harnesses: `tools/harnesses/fxo_scan.py` (census + reliable-entry extraction), `fxo_parse.py` (header parser implementing §7.2, the CRC of §7.6, and the public constant-table reader), `fxo_validate.py` (N/N replay of every rule below). None of them touches a mode-(a) container's non-first entries.

### 9.1 Which samples exist, and which are trustworthy

- Directory names of **all 38 archives in `packfiles/pc/cache`** (plus `launcher.vpp_pc` in the install root, which holds two non-shader entries) were read — names are readable regardless of decode reliability. **Only `shaders.vpp_pc` contains any `.fxo_pc` entry: 844 by name** (plus 847 `.fxo_pc_dx11` and 2 plain `.fxo`, which are other files and are not read here). No mode-(b) container, no raw-sentinel entry and no nested archive reachable through a reliable entry holds any.
- `shaders.vpp_pc` has container flags `0x4801` and no raw-sentinel entries: it is a **mode-(a)** container. Under the standing limitation only entry 0 is trustworthy — **`rfg-skybox-meteors.fxo_pc`, 1,408 bytes** (its decoded length equals its directory-declared size). **Reliable samples: 1. The other 843 `.fxo_pc` entries are non-first mode-(a) entries and were deliberately not extracted or used.** No reliable second sample exists anywhere in the install, so every "N/N" below has N = 1 (or a count of items *within* that one file). **[Lifted 2026-09-20, §10; the counts below are kept as the historical one-sample replay.]**

### 9.2 Replay results

| Rule (section) | Result |
|---|---|
| Magic `0x4B42A1EE` at `0x00` (§6.3) | **1/1** |
| Version ≥ 14 (§6.3) | **1/1** (value 14) |
| **Header-size formula → running offset ends exactly at the file length** (§7.2, §7.5): `H = 200`, vertex blob at 208, pixel blob at 944, end 1408 = file size | **1/1** — no scan needed and no slack |
| Each stage blob begins with a Direct3D 9 version token (`0xFFFE….` vertex / `0xFFFF….` pixel) and ends with the end token `0x0000FFFF` at the length the table gives | **2/2** |
| Blob start offset is a multiple of 16 (§6.4, §7.5) — including the first blob (208, whereas `H` = 200 is already a multiple of 8) | **2/2** |
| T1/T3 entry hash = CRC-32 of a lower-cased constant name from the bytecode's own public constant table (§7.3, §7.6) | **5/5** (`Meteor_strength`, `objTM`, `projTM`, `Time`, `Tint_color`) |
| T1/T3 entry register byte = that constant's register index | **5/5** (0, 32, 28, 40, 37) |
| T1/T3 entry `(+6)·(+7)` = the constant table's register count | **4/5** — the fifth (`objTM`) declares 4 rows, the compiled shader lists 3 (§7.3: declared, not compiled, footprint) |
| Role flag bit ⇔ role index byte ≠ −1 (§7.4) | **11/11** (flags `0x100`; ten `0xFF` bytes; base byte 0) |
| T8 entry's vertex/pixel indices lie inside the two stage tables (§7.3) | **1/1** (0 and 0) |
| Fixed-part bytes the loader overwrites or never reads (`0x19`–`0x1F`, `0x20`–`0x5F`, `0x6B`–`0x77`, `0x7C`) are zero | all zero (consistent with "ignored"; **not** a test of anything) |

### 9.3 What one sample cannot show

Every rule is exercised only in its degenerate case: the sample has one vertex blob, one pixel blob, **no T0/T2/T4 entries, no geometry entry, no fixed role (0–9) in use**, and version exactly 14. So **T0's field meanings, T2/T4, the role table's individual roles, the geometry slot's blob placement, cross-file de-duplication, and any version above 14 are supported by disassembly only, not by real bytes.** The formula and the alignment rule are the parts with real empirical backing — and they are what a reader needs to locate every blob. A second reliable sample would strengthen this most; none exists in the install, and obtaining one is blocked only by the parked mode-(a) limitation, which this pass did not touch. **[Lifted 2026-09-20, §10.]**

## 10. Sample limitation lifted (2026-09-20)

The "one reliable sample" limitation stated in §1–§5 and §9 ("only entry 0 of a mode-(a) container is trustworthy") is lifted: `spec-vpp-container.md` §7 documents the physical-offset rule, and SPEC TEAM verified that all 1,693 compressed entries of `shaders.vpp_pc` inflate to exactly their `+0x0C` at the rule offsets (3,263/3,263 over four archives), with the `.fxo_pc`/`.fxo_pc_dx11` magic present on 1,691/1,691. **Not yet re-done on SPEC TEAM's side:** this document's header-size formula (§7), the constant-hash rule and the geometry-slot claims over the full population. **Reported by Team B (2026-09-20, fresh reader; not yet re-derived here):** at the rule offsets `.fxo_pc` consumes exactly on 844/844 files and 7,276/7,276 embedded D3D9 blobs carry the right-stage token plus end token; `.fxo_pc_dx11` 847/847 exact, of which 277 have `nMid = 1` and consume only with middle blobs stepped over (so §8.3's "none seen" becomes 277), blobs beginning `DXBC` 8,166/8,166; T1–T4 hash+register match 18,652/19,928 (1,269 hash a name absent from every constant table, 8 differ in register); version 14 ×1,691, never above 14; T0 populated in 784 files, T4 in 181, T2 in 1. Treat these as leads to verify, not settled facts. *(Team B later reported the same `.fxo_pc` 844/844 exact consumption and `.fxo_pc_dx11` 847/847 decoded through their fixed container reader — still Team B's measurement, not yet re-derived by SPEC TEAM.)*

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): marked the stale mode-(a) "entry 0 only" statements as lifted in place (Method header, §6 items 3–4, §8.3, §9.1, §9.3) with pointers to §10 / `spec-vpp-container.md` §7, noted Team B's 277 `nMid = 1` lead at the two "none seen" claims, and flagged `spec-output.md` as unpublished.
