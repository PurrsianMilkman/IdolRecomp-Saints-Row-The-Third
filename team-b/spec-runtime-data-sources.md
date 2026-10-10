# Runtime Data Sources — locations and hashes ONLY, no reproduced bytes

**This file deliberately contains no hex, no raw bytes, and no decompiler pseudocode.** It exists
because of a project decision (2026-10-10): the public repo excludes three categories of
game/middleware DATA that other spec files had been carrying verbatim (the Wwise codebook library,
the Bink static decoder tables, and the Bink video decode test vectors). The reimplementation must
instead read that data at *runtime* from the player's own retail install. This file is the map for
doing that: for each item, which file, which offset, how many bytes, how to slice it, and a SHA-256
to verify the extraction — never the bytes themselves. If an editor is ever tempted to paste a hex
dump into this file, that is a bug in this file; delete it and put the location/hash back instead.

All hashes below were independently re-extracted and re-verified against real files during this
pass (2026-10-10), not copied from the source spec files without re-checking. Source files used for
verification: `D:\Project Crreish\Saints Row 3 CRREISH\SaintsRowTheThird.exe`,
`D:\Project Crreish\Saints Row 3 CRREISH\SaintsRowTheThird_DX11.exe`, and
`D:\Project Crreish\Saints Row 3 CRREISH\binkw32.dll`. VA→file-offset conversion used the same PE
section-table walk as `scratchpad/pe_byte_read_2026-10-09.py` (read `e_lfanew` at file offset
`0x3c`, walk the COFF/optional header to get `ImageBase` and the section table, then for a target VA
find the section whose `[VirtualAddress, VirtualAddress+max(VirtualSize,SizeOfRawData))` contains
`VA - ImageBase`, and compute `file_offset = PointerToRawData + (VA - ImageBase - VirtualAddress)`).

---

## 1. Wwise codebook library (599 Vorbis codebooks)

Full provenance, per-entry index table, and mechanism/findings narrative already exist in
`spec-audio-codebook-library.md` (data appendix) and `spec-audio-runtime.md` §11.11.3a/§11.11.4
(narrative). This section only restates the *locations* and re-verifies the *hashes* — the 599-row
per-entry table (`slot`, `blob_offset_dec`, `byte_len`, decoded header fields) is not reproduced
here; read it in `spec-audio-codebook-library.md` directly.

### DX9 (`SaintsRowTheThird.exe`)

- File offset range: `0xe7da38`..`0xe8f29a` (i.e. bytes `[0xe7da38, 0xe8f29a)`, length 71778).
- VA range (image base `0x00400000`): `0x127ea38`..`0x129029a`.
- SHA-256 of the raw 71778-byte blob, file-offset order:
  `d7ede6ddf80ab276e3c48777e20015c1b8654d17086078a2ebdf15786c4e7513`
  — **re-extracted and re-hashed directly from the real exe during this pass; matches the value on
  record in `spec-audio-codebook-library.md` exactly.**
- Per-entry slicing rule (see `spec-audio-codebook-library.md`'s index table for the full 599-row
  CSV): each codebook's body is a contiguous byte run inside this blob, located by that table's
  `blob_offset_dec` (offset into the 71778-byte blob, NOT the exe file offset) and `byte_len`
  (exact body length). To get codebook N's bytes at runtime: read the 71778 bytes at the file-offset
  range above out of the player's own exe, then slice
  `blob[blob_offset_dec : blob_offset_dec + byte_len]` using that table's own per-row values — do
  not recompute offsets by hand, the table already carries them 1:1 with `exe_file_offset_hex`.
- The library's pointer table is sized for 1024 slots; only indices 0-598 are real codebooks, slot
  599 is a NULL terminator, and indices 600-1023 are spurious (read off the end into an unrelated
  `.data` float array) — do not attempt to extract slots beyond 598.

### DX11 (`SaintsRowTheThird_DX11.exe`)

The existing record (`spec-bink-codec.md` lines ~3910-3914, ~4786-4790) already states that this
exe carries "the same table, 99.99% byte-identical" to the DX9 copy, with the only difference in
slot 598's trailing alignment-slack bytes — but it did not previously record this exe's own file
offset for the table. That offset is newly determined and verified during this pass (2026-10-10),
by searching `SaintsRowTheThird_DX11.exe` for the DX9 blob's leading bytes (not assumed to be the
same file offset as the DX9 exe — it is not; the two binaries have different section layouts):

- File offset range: `0xe815e8`..`0xe92e4a` (length 71778, same length as the DX9 copy).
- SHA-256 of the raw 71778-byte blob at that range, file-offset order:
  `5815d2c9a96efad86abc04661bd54cf350aed5011036d3f794753c94999b64cb`
- Byte-for-byte diff against the DX9 blob above: exactly 4 bytes differ, at blob offsets 71772,
  71773, 71776, 71777 (the last 6 bytes of the 71778-byte range) — consistent with the existing
  "slot 598 alignment-slack" note; every real codebook body (slots 0-597) is byte-identical between
  the two exes.
- Per-entry slicing rule: identical to the DX9 case (same `blob_offset_dec`/`byte_len` table in
  `spec-audio-codebook-library.md` applies unchanged — only the exe-level file-offset anchor differs
  between the two builds, not the internal layout).
- **Caveat:** this DX11 offset was derived by this pass via a raw byte-pattern search against a real
  DX11 exe on this machine, not by a prior independent disassembly pass of that binary the way the
  DX9 offset was. Treat it as verified-but-narrower (confirmed against one real copy of the file,
  not cross-checked against disassembly of the DX11 exe's own codebook-loading code path).

---

## 2. Bink static decoder tables (5 regions, `binkw32.dll`)

Full provenance and mechanism/findings narrative already exist in `spec-bink-static-tables.md`
(data appendix) and `spec-bink-codec.md` §5.3b/§5.3c/§5.3g (narrative). This section restates
locations and re-verifies hashes only.

A real copy of `binkw32.dll` exists on this machine at
`D:\Project Crreish\Saints Row 3 CRREISH\binkw32.dll` (and in several sibling install copies under
`C:\Games\...`), so all five VA→file-offset conversions below were verified against a real file,
not computed algebraically from a documented section table alone.

- `binkw32.dll` PE image base: `0x10000000`.
- Relevant section layout (name, VirtualAddress, PointerToRawData): `.data` VA `0x00033000` →
  raw pointer `0x00024000` (size `0x3c00`). All five regions below fall inside this section
  (region VA minus section VA plus section raw pointer gives the file offset below).

| label | VA | file offset | length (bytes) | SHA-256 (re-verified 2026-10-10) |
|---|---|---|---|---|
| `region_bd40` | `0x1003bd40` | `0x0002c940` | 96 | `6b5955deb66e92654c54d324ba92dd511b8a4fae4cd41a9eca0e3859f154f135` |
| `region_c140` | `0x1003c140` | `0x0002cd40` | 64 | `5a694c700b14d5e3b861c3aaa5921c34ad77d786d8e5fb6ed4f9b2ee944c0af4` |
| `region_bd60` | `0x1003bd60` | `0x0002c960` | 1104 | `a8ac69960cdddc8daa498b0412d9ce6587c4773e21fec12c6d43735f88179e97` |
| `intra_quant` | `0x10037cc0` | `0x000288c0` | 4096 | `74ea56443cd480620a0d69402c6a796d8cd1715c91259d11522803eb6b8c3ca8` |
| `inter_quant` | `0x10038cc0` | `0x000298c0` | 4096 | `4442cb0c6670d276dde6a5cbf51ba4567819cd916d5036f0f306a34c1351404b` |

All five SHA-256 values above were independently re-extracted (via the VA→file-offset formula at
the top of this document) and re-hashed from the real `binkw32.dll` on this machine during this
pass, and match the values on record in `spec-bink-static-tables.md` exactly. `intra_quant` and
`inter_quant` are back-to-back with zero gap (`0x10037cc0 + 4096 = 0x10038cc0`), confirmed by the
same read.

Layout/slicing notes (see `spec-bink-static-tables.md` and `spec-bink-codec.md` §5.3g for full
semantics — not reproduced here):

- `region_bd40` (96 bytes): byte 0-3 leading field, bytes 4-19 the 16-byte symbol-class table
  (max Huffman code length per nibble value), bytes 20-23 the 4-byte run-length sub-table, bytes
  24-95 trailing (role not fully pinned down upstream).
- `region_c140` (64 bytes): 16 × 4-byte little-endian dword offsets, indexing into `region_bd60`.
- `region_bd60` (1104 bytes): the shared variable-record blob that `region_c140`'s 16 offsets
  index into — read `region_c140`'s dwords first to find each record's start inside this blob.
- `intra_quant` / `inter_quant` (4096 bytes each): 16 rows × 64 little-endian int32 (256
  bytes/row), row-major, no re-sorting — `quant_byte_offset(k) = 4*k` for coefficient index
  k = 0..63 within a row.

---

## 3. Bink video decode test vectors — NOT retail runtime data

`spec-bink-video-test-vectors.md` catalogs ~17 `.bin` files (stage-buffer captures: entropy
pre-stage scratch, DC/AC coefficient structs, post-IDCT blocks, Y/chroma plane crops) with a
SHA-256 each. These do **not** belong in this file's framework, and are deliberately NOT given a
"source location in a retail install" entry above, because they are not game data at all:

- They are this project's own debug captures, produced by running `binkw32.dll`'s video decode
  core under a Unicorn CPU-emulation harness against one specific test file
  (`D:\Project Crreish\Saints Row 3 CRREISH\videos\main_menu_bg.bik`, frames 0-1) and dumping its
  internal intermediate buffers (case-selector scratch, per-block DC/AC coefficient structs,
  post-transform spatial blocks, final plane crops).
- They exist purely so Team B (or this project) can unit-test a from-scratch Bink video decoder
  implementation against known-good intermediate values — a correctness oracle for development,
  not something the shipped game or a player's install contains, ships, or reads at runtime.
- The genuinely excluded *runtime* data for Bink video decode is the 5-region static table set in
  §2 above (quantization matrices + entropy pre-stage tables) — those are the only Bink-side
  bytes the real decoder actually loads from `binkw32.dll` at runtime. The test vectors are a
  separate, unrelated category and should stay in `spec-bink-video-test-vectors.md` as research
  artifacts; they should not be re-framed as "data to read from the player's install" because
  there is no such install-side source for them — the player's `binkw32.dll` does not contain a
  pre-decoded copy of `main_menu_bg.bik`'s frame-0/frame-1 intermediate buffers, only the bitstream
  itself (which Team B's decoder will decode fresh, using the static tables in §2, each time the
  video plays).
- One entry (`cb_plane_frame0_crop128x128.bin` / `cb_plane_frame1_crop128x128.bin`, both hashing to
  `0196a9756465a29c136dd8706e5b612057bc91a38168d0b0e0ac9e5147d6ad79`) is explicitly flagged in the
  source file as sentinel-fill (`0xEE`), not real decoded output — a further reason these are
  research/debug artifacts, not data.

No action needed for this category beyond leaving the existing file as-is; it is out of scope for
"exclude from repo, read from player install" because it was never shipped game data in the first
place.

---

## Summary table

| item | retail source file | location | length | status |
|---|---|---|---|---|
| Wwise codebook library (DX9) | `SaintsRowTheThird.exe` | file offset `0xe7da38` | 71778 bytes | verified 2026-10-10 |
| Wwise codebook library (DX11) | `SaintsRowTheThird_DX11.exe` | file offset `0xe815e8` | 71778 bytes | verified 2026-10-10 (narrower confidence, see §1 caveat) |
| `region_bd40` | `binkw32.dll` | VA `0x1003bd40` / file offset `0x0002c940` | 96 bytes | verified 2026-10-10 |
| `region_c140` | `binkw32.dll` | VA `0x1003c140` / file offset `0x0002cd40` | 64 bytes | verified 2026-10-10 |
| `region_bd60` | `binkw32.dll` | VA `0x1003bd60` / file offset `0x0002c960` | 1104 bytes | verified 2026-10-10 |
| `intra_quant` | `binkw32.dll` | VA `0x10037cc0` / file offset `0x000288c0` | 4096 bytes | verified 2026-10-10 |
| `inter_quant` | `binkw32.dll` | VA `0x10038cc0` / file offset `0x000298c0` | 4096 bytes | verified 2026-10-10 |
| Bink video test vectors | — (not retail data) | — | — | out of scope, see §3 |

## Cross-references

- `spec-audio-codebook-library.md` — full 599-row codebook index table (data appendix).
- `spec-audio-runtime.md` §11.11.3a/§11.11.4 — codebook library mechanism/findings narrative.
- `spec-bink-static-tables.md` — full raw-byte appendix for the 5 Bink static regions.
- `spec-bink-codec.md` §5.3b/§5.3c/§5.3g — Bink static table mechanism/findings narrative, and
  the DX9/DX11 twin-build codebook comparison this file's §1 DX11 entry builds on.
- `spec-bink-video-test-vectors.md` — the research-artifact captures discussed in §3.
- `scratchpad/pe_byte_read_2026-10-09.py` — the VA→file-offset technique reused for this file's
  verification pass.
