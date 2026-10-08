# Saints Row: The Third — `.vf3_pc` Font Format and Font Tables Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Scope:** The `.vf3_pc` bitmap-font file (header, kerning table, glyph table, texel-position tables), how the game loads it and its texture, the game's derived ("scaled variant") fonts, the game's named font table that a UI `font` property resolves to (including the two-layer "secondary" entry), the 10-entry Vint-side font pool, and the game-side metric callbacks Vint text layout calls. Per-glyph drawing geometry (quads, UVs, the half-texel inset) is in `spec-vint-render.md` §14.1; text layout and markup are in `spec-vint-render.md` §14.2–§14.7. This file is the font half of UI-render unit U5 (scoping note `review/scoping-ui-render.md` §4 item 7, called "U6" in that note's own numbering).
**Method:** direct read of `SaintsRowTheThird.exe`'s bytes (no Ghidra project), with the same scratch capstone lister used by units U1–U3, plus — new for this unit — **emulated calls of the real shipped functions** (the project's `unicorn` call harness, `tools/harnesses/unicorn_call.py`) fed with the real shipped font files, so the per-glyph numbers below are the real code's output, not a reading of it. Six real `.vf3_pc` files were extracted from `misc.vpp_pc` (a per-entry-compressed "mode (a)" container, read with `tools/harnesses/vpp_modea.py`).
**Cleanroom compliance:** no decompiled source, pseudocode, or internal variable names are quoted — plain hex addresses as evidence anchors only, as in every other spec here. File names, font names (`thin`, `header_n_overlay`, …), colour names and format strings are the game's own shipped data, quoted as data.
**Confidence key:** **CONFIRMED** (read directly in disassembly this pass, or reproduced by an emulated call of the real function — the latter is marked *emulated*), **HIGH CONFIDENCE** (inferred from structure), **HYPOTHESIS** (plausible, unchecked), **OPEN**.

---

## 1. Population

| File (in `misc.vpp_pc`) | Bytes | Glyphs | Kerning pairs | Texture named inside |
|---|---|---|---|---|
| `font_body.vf3_pc` | 18,280 | 336 | 1,668 | `font_body_nobdr.tga` |
| `font_header.vf3_pc` | 20,488 | 336 | 2,036 | `font_header_nobdr.tga` |
| `font_header_pc.vf3_pc` | 20,488 | 336 | 2,036 | `font_header_pc_nobdr.tga` |
| `font_sk.vf3_pc` | 39,424 | 1,607 | 108 | `font_sk_nobdr.tga` |
| `jap.vf3_pc` | 52,816 | 2,192 | 0 | `jap_nobdr.tga` |
| `ug-debug.vf3_pc` | 5,584 | 224 | 0 | `ug-debug_nobdr.tga` |

The same archive holds the matching textures as ordinary `.cvbm_pc`/`.gvbm_pc` pairs (`spec-texture-format.md`): `font_body_nobdr`, `font_header_nobdr`, `font_header_pc_nobdr`, `font_sk_nobdr`, and also `font_body`/`font_header`/`font_header_pc` *without* the `_nobdr` suffix, which the loader never asks for (§3). ~~No `jap_nobdr`/`ug-debug_nobdr` texture... were found in `misc.vpp_pc`~~ **[Corrected 2026-10-03, found by Team B's text renderer and independently re-confirmed here by direct container read: `jap_nobdr.cvbm_pc`/`.gvbm_pc` and `ug-debug_nobdr.cvbm_pc`/`.gvbm_pc` ARE present in `misc.vpp_pc`** (entries 21–22 and 26–27 of 220; this pass's earlier container scan missed them). **`jap_nobdr` is 2,048 × 1,024; `ug-debug_nobdr` is 256 × 256** (read from each `.cvbm_pc` header, `spec-texture-format.md` §10.3). Team B's viewer reports loading `ug-debug` successfully (their own tool, not independently re-run here). **`fnt_chinese.vf3_pc`** (a name the set-up routine asks for, §4.2) is still **not found** in `misc.vpp_pc` by name — a fresh search for that exact string found nothing — so it remains OPEN, narrowed: of the three non-default language arrangements (§4.2), codes 3 (`jap`) and `0xb` (`font_sk`) now have every font and texture they need present in this install; only code `0xd` is blocked, specifically by the missing `fnt_chinese.vf3_pc` font file itself, not a texture.]**

**Layout check, CONFIRMED on 6/6 files:** file size = `0xd0 + 6·K + 24·C` exactly (`K` kerning pairs, `C` glyphs), i.e. the four tables of §2.2 follow the header back to back with no padding and nothing after them.

---

## 2. On-disk layout

All values little-endian. The loader reads the whole file into one block and patches the pointer fields in place (§3), so the header doubles as the in-memory font record.

### 2.1 Header (`0xd0` bytes)

| Offset | Type | Meaning | Shipped values | Evidence |
|---|---|---|---|---|
| `+0x00` | 4 bytes | magic, the bytes `T N F V` (dword `0x56464e54`) | all 6 | **not checked** by the loader — CONFIRMED (`0x00dcb6c0` tests only `+0x04`) |
| `+0x04` | u32 | version; must be exactly **4** or the load fails | 4 | CONFIRMED (`0x00dcb6c0`) |
| `+0x08` | u32 | glyph count `C` | see §1 | CONFIRMED (loader table sizes; metric bound check in `0x00dcba50`) |
| `+0x0c` | u32 | code of the first character; glyph index = UTF-16 code unit − this | 32 in all 6 | CONFIRMED (`0x00dcba50`) |
| `+0x10` | s32 | **default advance** — the advance of a character outside the font | 14 / 22 / 24 / 23 / 25 / 8 | CONFIRMED (`0x00dcba50` out-of-range path; *emulated*) |
| `+0x14` | u16 | **line height** — what text layout uses for one line | 36 / 58 / 58 / 38 / 36 / 17 | CONFIRMED (`0x00842e20`) |
| `+0x16` | u16 | **glyph cell height** — the height of every glyph quad and of its texture rectangle; a stored 0 is replaced by `+0x14` at load | 36 / 100 / 100 / 30 / 26 / 20 | CONFIRMED (`0x00dcb6c0` default; drawer, *emulated*) |
| `+0x18` | u32 | not read by any function in this pass | 0 in all 6 | OPEN (unused here) |
| `+0x1c` | s32 | **advance adjustment** added to every glyph's advance (and to the default advance) | −3 / −6 / −6 / −1 / −2 / −5 | CONFIRMED (`0x00dcba50`; *emulated*) |
| `+0x20` | s32 | kerning pair count `K` | see §1 | CONFIRMED |
| `+0x24` | s16 | **vertical glyph origin offset** (font units) — added to the top of every glyph quad | 0 / −20 / −20 / 3 / 4 / −1 | CONFIRMED (*emulated*: `font_header` quads start at y = −20) |
| `+0x26` | s16 | **horizontal glyph origin offset** (font units) | 0 / −1 / −1 / 0 / 0 / −1 | CONFIRMED (*emulated*) |
| `+0x28` | char[64] | a name of the form `<font>.peg` | all 6 | **not read** by the loader (OPEN whether anything else reads it) |
| `+0x68` | char[64] | the texture name, `<font>_nobdr.tga` — used to find and, if needed, load the glyph texture | all 6 | CONFIRMED (`0x00dcb6c0`) |
| `+0xa8`…`+0xcf` | — | zero on disk; runtime fields written by the loader: `+0xa8` kerning-table pointer (0 when `K` = 0), `+0xb0` glyph-table pointer, `+0xb8` texture handle, `+0xc0` x-table pointer, `+0xc8` y-table pointer, `+0xac`/`+0xb4`/`+0xc4`/`+0xcc` zeroed; `+0xbc` is not written | zero in all 6 | CONFIRMED (`0x00dcb6c0`) |

(Value lists are in §1's file order: body, header, header_pc, sk, jap, ug-debug.)

### 2.2 Tables after the header

In this order, starting at `0xd0`:

1. **Kerning pairs**, `K` × 6 bytes: u16 left glyph index, u16 right glyph index, **s8 adjustment** (font units, added to the left glyph's advance), u8 padding — `0xCD` in every one of the 5,848 shipped records (an uninitialised-memory fill from the authoring tool; never read).
2. **Glyph records**, `C` × 16 bytes: s32 **advance**, s32 **width** (the drawn quad's width; equal to the advance in every shipped glyph), u16 + u16 (0 in every shipped glyph, never read in this pass), s16 **index of this glyph's first kerning record** (−1 = none), u16 (0, never read).
3. **Texel x table**, `C` × u32: the glyph's left edge in the texture, in texels.
4. **Texel y table**, `C` × u32: the glyph's top edge in the texture, in texels.

All CONFIRMED (loader pointer arithmetic in `0x00dcb6c0`; field uses in `0x00dcba50` and the drawer; and *emulated* UVs matching these tables exactly, `spec-vint-render.md` §14.1). The glyph's texture rectangle is `x … x + width` by `y … y + cell height (+0x16)`. In `font_body`, for example, `A` (glyph 33) is advance/width 21 at texel (60, 40), and rows of glyphs sit 39 texels apart (cell 36 plus a 3-texel gutter). The furthest glyph corners in `font_body` reach texel (507, 427), inside its 512 × 512 texture (the `.cvbm_pc` header's own size fields); ~~the other fonts reach x ≈ 2,032–2,041, i.e. 2,048-wide pages (HIGH; their texture headers were not cross-read)~~ **[Confirmed 2026-10-03 by direct read of each `.cvbm_pc` header (prompted by Team B's text-renderer finding, §1): `font_header_nobdr` is 2,048 × 512, `font_header_pc_nobdr` and `font_sk_nobdr` are 2,048 × 1,024, and `jap_nobdr` (§1) is also 2,048 × 1,024 — every one of "the other fonts" is exactly 2,048 wide, settling the HIGH note at CONFIRMED.]**

### 2.3 Kerning — the lookup, and a real quirk in it

A glyph's kerning records are contiguous, start at the index its glyph record names, and are ordered by right index (every file with pairs is fully sorted except `font_body`, which has one record out of order, at index 2; `jap` and `ug-debug` have no pairs). Kerning applies only when the following character is not 0, not a line feed, and is itself inside the font. **CONFIRMED** (`0x00dcba50`).

The lookup walks forward from the glyph's first record while the record's left index is this glyph and its right index is below the next glyph's index, and applies the adjustment if the record it stops on has a right index equal to the next glyph's. **It does not re-check the left index of the record it stops on.** So when every one of a glyph's own records has a smaller right index than the next glyph, the walk stops on the *following glyph's* first record and applies that record's adjustment if only its right index matches. **CONFIRMED, *emulated*:** in `font_body`, an apostrophe followed by `T` is drawn 2 units tighter, because the apostrophe's only pair (with `J`) is exhausted and the next record — comma + `T`, −2 — matches on its right index alone. A replacement that must match the original's spacing pixel-for-pixel has to reproduce this; one that only needs plausible kerning can do a correct pair lookup instead. (The walk can also read one record past the end of the table when a glyph's records are the last ones; no shipped glyph is placed where this changes a result — HIGH.)

Kerning pairs whose right index is at or beyond the glyph count can never apply (the next character fails the in-font test first): 165 / 169 / 169 such records in `font_body` / `font_header` / `font_header_pc` (right indices up to 8,189 — they appear to target typographic quotes, U+2019 and similar, that these 336-glyph fonts do not contain). **CONFIRMED** by count.

---

## 3. Loading a font

**`0x00dcbd40`(name, allocator, texture-id override)** → **`0x00dcb6c0`**, CONFIRMED:
1. The path is the given name plus `.vf3_pc` (extension string `0x0124ac00`, through the pointer `0x0132a29c`); the file is opened read-binary and read whole into memory from the allocator (16-byte aligned).
2. The version must be 4 (§2.1); anything else returns failure, and the new id is given back.
3. Pointer fix-ups as in §2.1. If `+0x16` is 0 it is set to `+0x14`.
4. **The texture.** If the texture named at `+0x68` is not already resident, the loader loads `<directory of the requested font name> + <texture name without extension> + ".cvbm_pc"` (format string `0x0129c8cc`, extension pointer `0x0124af9c`); then it looks the texture up by the `+0x68` name and stores the handle at `+0xb8`. **So the glyph texture is always the `_nobdr` one**; the same-named textures without `_nobdr` in `misc.vpp_pc` are not loaded by this path (HIGH that "nobdr" means "no border", i.e. the shipped glyph art has no baked outline — §4.3).
5. The loaded font gets the next **game font id** (0, 1, 2 … in load order; list at `0x02a07d64`, count `0x02a07d60`, capacity from `0x0132ab88`). The first font ever loaded is remembered (`0x02a07d6c`) as the default that id −1 resolves to.

**Derived fonts — `0x00dcaee0`(base id, scale, vertical offset, line-height adjustment)**, CONFIRMED: appends a 16-byte record to `0x02a07d5c` (count `0x02a07d70`, capacity `0x0132ab8c`): f32 **scale**, s32 **base font id**, f32 **vertical draw offset** (font units), f32 **line-height adjustment** (font units). Derived ids are `0x80000001` + record index. **Resolving any id (`0x00dcac20`):** a derived id resolves through its base id; −1 resolves to the default font; ids at or beyond the font count resolve to nothing. What the four values do (all CONFIRMED, *emulated* with a real `font_body`/`font_header` record): every glyph quad, every advance and every texel-to-screen size is multiplied by the scale (advances and widths are truncated to whole units *after* scaling, quad heights and offsets are not); the vertical draw offset is added to the font's own vertical origin before scaling; the line-height adjustment only changes the line height that layout sees (§6), not the drawing. A plain (non-derived) id behaves as scale 1, offsets 0 (`0x00dcac80`, `0x00dcad10`, `0x00dcacd0`).

---

## 4. The game's named font table — what a `font` property resolves to

### 4.1 Layout of the table — CONFIRMED

12-byte entries at `0x0290f550`, count at `0x0290f548`:

| Entry offset | Content |
|---|---|
| `+0x00` | hash of the font's registered name (engine string hash `0x00d9e8b0`) |
| `+0x04` | **primary** game font id (plain or derived) |
| `+0x08` | **secondary** game font id, or −1 |

Registration (`0x00b9bb00`(name, primary, secondary)) refuses a new entry once the count would reach 8, so **at most 7 entries** exist; the shipped set-up registers 6. Lookup by name (`0x00b9bb60`) compares hashes and returns the entry index, or −1.

**Who uses which column — CONFIRMED:**
- **Drawing** (`0x00b9cf20`, behind callback slot `+0x2c`, `spec-vint-render.md` §12.4): draws the run with the **secondary** id first if it is not −1, then with the **primary** id if it is not −1 **and differs from the secondary**. An out-of-range entry index falls back to **entry 0**.
- **Measuring** (`0x00b9b720`, behind the metric callbacks of §6): uses the **primary** id only. An out-of-range entry index gives **game font id 0** — not entry 0's font. In the shipped English set-up entry 0 is `debug` (the `ug-debug` font) while game font id 0 is `font_body`, so a bad index would draw in one font and measure in another (HIGH that no shipped document does this — the font names found in the 159 documents are all registered ones, §4.2; an exhaustive per-record check of `font` values was not run).

### 4.2 What the shipped set-up registers — CONFIRMED (`0x00b9cab0`, called once from `0x005d23e0`)

The scoping note called `0x00b9cab0` "the loader"; it is the game's **font set-up**, and it calls the loader (§3) by name. It reads the language code at `0x01301a90` and picks one of four arrangements:

| Language code | Fonts loaded (game ids in load order) | Body font (`thin`, `thin_overlay`) | Header font (`header_n`, `header_n_overlay`) | Body-italic font (`bodyital_overlay`) |
|---|---|---|---|---|
| 3 | `jap`, then `ug-debug` | derived from `jap`: scale 1.02, offsets 0 | derived from `jap`: scale 2.35, vertical draw offset −5, line-height adjustment −11 | derived from `jap`: scale 1.02 |
| `0xd` | `fnt_chinese`, then `ug-debug` | derived: scale 1.2 | derived: scale 1.85 | derived: scale 0.9 |
| `0xb` | `font_sk`, then `ug-debug` | derived: scale 0.918 | derived: scale 2.115, vertical draw offset −5, line-height adjustment −11 | derived: scale 1.02 |
| anything else | `font_body` (id 0), `font_header_pc` (id 1), `ug-debug` (id 2) | `font_body` itself | `font_header_pc` itself | `font_body` itself |

(Offsets not listed are 0. Every derived font in the table is created separately, twice — once for the primary column and once for the secondary column of §4.1's table.)

The six registered names and their columns (`P` = primary, `S` = secondary):

| Entry | Name | P | S |
|---|---|---|---|
| 0 | `debug` | the `ug-debug` font | −1 |
| 1 | `thin` | body font | −1 |
| 2 | `thin_overlay` | body font | body font (second copy, see below) |
| 3 | `header_n` | header font | −1 |
| 4 | `header_n_overlay` | header font | header font (second copy) |
| 5 | `bodyital_overlay` | body-italic font | body-italic font (second copy) |

In the default arrangement every "second copy" is literally the same game font id as its primary. In the three language arrangements the primary and secondary are **two separately created derived fonts with identical parameters**, so they have different ids. Those derived line heights are tuned to the default fonts: `jap` 36 × 1.02 → 36 (= `font_body`'s 36); (36 − 11) × 2.35 → 58 (= `font_header_pc`'s 58). The shipped documents use `thin` (86 of the 159 documents), `header_n` (38), `thin_overlay` (18), `header_n_overlay` (5) and `debug` (2); `bodyital_overlay` appears in none (string occurrences in the 159 distinct documents of `tools/scratchpad/vintdoc_samples`).

### 4.3 The "two-layer font" question (`spec-vint-render.md` §11 item 11) — answered

**It is not an outline/fill pair, and in the shipped data it is never two different looks.** CONFIRMED from the registration calls and the draw test above:
- **Default arrangement (every language except codes 3, `0xd`, `0xb`):** an `_overlay` name has secondary = primary, so the "different from the secondary" test suppresses the second pass and the run is drawn **exactly once**, identically to the non-`_overlay` name.
- **Codes 3, `0xd`, `0xb`:** the two ids differ, so the run is drawn **twice, with identical glyphs, at the identical position, in the same colour** (HIGH, from the ids alone: the visible effect is a double alpha-blend — anti-aliased glyph edges come out more opaque — not a second style). Whether that was intended is unknowable from the code.
- The glyph textures are the `_nobdr` art (§3), so no shipped font carries a baked border either.

**For a replacement:** treat every `_overlay` name as its base font and draw once. Drawing twice in the three language arrangements is only needed for a pixel-exact match there. The mechanism itself is general — the secondary is drawn first, underneath, so it *could* hold an outline font — but nothing ships that uses it that way.

---

## 5. The Vint-side font pool — CONFIRMED

The Vint library keeps its own small pool of font references; a `text` element's `font` property (`spec-lua-bindings.md` §9, `text` table, element `+0xcc`) holds a handle into it.

- **Records:** 10 × 28 bytes at `0x02a88050` (set up by `0x00e2fa20`): `+0x00` **game font-table entry index** (§4.1; −1 once released), `+0x04` lower-cased CRC-32 of the font name (`0x00d9e740`), `+0x08` the **handle**, `+0x0c` **"break anywhere" flag** (§6, slot `+0x24`), `+0x10` reference count, `+0x14`/`+0x18` next/previous links. A live list (head `0x02a88040`) and a free list (head `0x02a88044`); a 16-bit serial counter at `0x02a88048`.
- **Handle** = `(serial << 16) | record index`. **Lookup (`0x00e2fc70`):** the low 16 bits must be below 10 and the record's `+0x08` must equal the whole handle, otherwise no font (a stale handle resolves to nothing).
- **Acquire by name (`0x00e2fae0`):** a live record with the same name CRC is shared (reference count +1). Otherwise a free record is taken; the game resolves the name through resource-callback slot `+0x10` (§6; a failure, −1, fails the acquire), and slot `+0x24` fills the "break anywhere" flag. **Quirk (CONFIRMED by reading):** slot `+0x24` is called with the record's `+0x00` *before* that field is filled in — a stale value (0 on a never-used record, −1 on a recycled one). The game side only compares that value against the debug font's id, so in practice the flag is simply "language code is 3" (HIGH; §6).
- **Release (`0x00e2fcd0`):** decrements the count; at the last reference it calls resource slot `+0x18` (a no-op on the game side), clears the handle, sets `+0x00` to −1 and returns the record to the free list.
- **Ten is the hard limit** of distinct font names in use at once; the shipped set has 6 names (§4.2), of which 5 are used.

---

## 6. The metric callbacks layout uses — CONFIRMED

Vint never reads a font file itself. It measures through the **resource-callback table** (pointer at `0x02a5a004`, installed by the UI bring-up `0x008489e0` from the game table at `0x02317a78`), a different table from the 18-slot drawing table of `spec-vint-render.md` §9/§12.4. The font-related slots:

| Slot | Game body | Arguments → result | What it computes |
|---|---|---|---|
| `+0x10` | `0x00842dd0` | (name) → entry index or −1 | font-table lookup by name (§4.1) |
| `+0x14` | `0x00842df0` | (name, add-ref callback) → entry index | same, then calls the given callback |
| `+0x18` | `0x00754410` | (entry index) | release — a bare return on the game side |
| `+0x1c` | `0x00842e20` | (&height, entry index) → success | **line height** = trunc((`+0x14` line height + derived line-height adjustment) × derived scale), of the entry's **primary** font |
| `+0x20` | `0x00842ed0` | (&advance, character, next character, entry index) → glyph exists | **advance** of one character = trunc((glyph advance + `+0x1c` adjustment + kerning with the next character, §2.3) × derived scale). **For a character not in the font it returns failure, and Vint then uses 0** (`0x00e2fdd0`) — see the mismatch note below |
| `+0x24` | `0x00842ea0` | (&flag, entry index) → true | **"break anywhere" flag**: 0 if the index equals the debug font's game id (`0x0130f5e0`), else 1 exactly when the language code (`0x01301a90`) is 3 |
| `+0x28` | `0x00842f20` | (&width, text, start, count, entry index) → true | sum of the advances of up to `count` characters; **not called by text layout** (its caller was not traced — OPEN) |

The Vint-side wrappers are `0x00e2fd90` (line height; 0 if the slot is missing) and `0x00e2fdd0` (advance; 0 if the slot is missing or the glyph is absent).

**Measure/draw mismatch for characters outside the font — CONFIRMED, *emulated* on both sides.** Layout measures such a character as **0 wide** (slot `+0x20` fails), but the game's glyph drawer advances its pen by the font's **default advance** (`+0x10` + `+0x1c`, scaled) and draws a zero-width quad there (`spec-vint-render.md` §14.1). Example, `font_body`: the string `A`, U+4E00, `B` measures 34 wide in layout, but the drawer places `B` 11 units further right than layout assumed. Control characters (below U+0020) measure 0 *and* draw with no advance, so they agree. A replacement that measures and draws from one shared metric will not reproduce this — which only matters for text containing characters its font lacks.

---

## 7. Worked numbers (`font_body`, real shipped file, *emulated*)

These are real outputs of the shipped functions on the real file, given as data for checking an independent implementation (texture taken as 512 × 512; plain, non-derived font):

| Input | Output |
|---|---|
| advance of `A` before `B` | 18 (21 − 3, no kerning pair) |
| advance of `-` before `A` | 9 (14 − 3, kerning −2) |
| advance of `'` before `T` | 3 (8 − 3, −2 via the §2.3 quirk) |
| line height (slot `+0x1c`) | 36 |
| glyph `A` quad at pen (0, 0) | (0, 0)–(21, 36); texture rectangle u 60.5 → 81, v 40.5 → 76 (texels) |
| `font_header`, glyph `A` at pen (0, 0) | (−1, −20)–(56, 80) — the origin offsets of §2.1 and cell height 100 |
| derived font over `font_body`, scale 2.35, vertical offset −5, `A` at pen (0, 0) | (0, −11.75)–(49, 72.85); u 60.5 → 80.851 (the width is truncated to 49 after scaling, then mapped back through the scale) |

The full per-glyph rules are in `spec-vint-render.md` §14.1.

---

## 8. OPEN items

1. ~~Where `jap_nobdr`, `ug-debug_nobdr` and `fnt_chinese` live (not in `misc.vpp_pc`); whether this install can even reach language codes 3, `0xd`, `0xb`.~~ ~~**[Narrowed 2026-10-03, §1: `jap_nobdr` and `ug-debug_nobdr` are in `misc.vpp_pc` after all. This install can reach codes 3 and `0xb` in full (every font and texture `0x00b9cab0` needs for them is present); only code `0xd` stays blocked, by the still-missing `fnt_chinese.vf3_pc` font file itself.]**~~ **[Corrected 2026-10-03, `spec-localization-format.md` §5.2, which reads the actual language-selection code: the font/texture presence finding above still stands, but it doesn't matter — on PC the language code at `0x01301a90` can never actually BE 3, `0xb` or `0xd` in the first place. Both the Steam-detected language and the start-up override are filtered through the supported-code string `US DE ES FR IT NL CZ PL RU` before anything is set; any other detected or requested language silently becomes `US` (code 0). So all three of these font arrangements are dead code in this build, not merely unexercised or partially blocked — this is a stronger and different conclusion than the narrowing above, which only looked at data availability, not the code path that would need to select the data.]**
2. Header `+0x18`, glyph-record bytes `+0x08`–`+0x0b` and `+0x0e`, and the `<font>.peg` name at `+0x28`: zero or unread everywhere in this pass. A writer of new fonts should copy the shipped values.
3. Resource slot `+0x28` (string width): its caller.
4. ~~Which human language each code in §4.2 is. (The code at `0x01301a90` is set elsewhere; not traced. `font_sk`'s 1,607 glyph slots run from U+0020 to U+0666, a range that spans the Greek and Cyrillic blocks; which slots hold real glyphs was not checked.)~~ **[Closed 2026-10-03, `spec-localization-format.md` §5.2/§7 item 5: code 3 = Japanese (`JP`), code `0xb` = Korean (`SK` — the code's own platform-language name is literally `korean`, and `charlist_sk.dat`'s own header says `LANGUAGE=Korean`), code `0xd` = `CH`, Chinese (HIGH, from the `fnt_chinese` font name). The "Greek and Cyrillic" reading of `font_sk`'s glyph range was a misreading: codes `0x0100`–`0x0666` are Hangul, assigned through `charlist_sk.dat`'s own code page (that file's own format is specified there), not raw Unicode — `font_sk` is a Korean font, consistent with the language name.]**
5. **Live confirmation.** The numbers in §2–§7 are real code run on real files, but in emulation with the texture size supplied by hand; a frame capture of one glyph run would close the loop.
6. Not crossed: the `.czn_pc` zone-data interior and named-object resolution.

---

## 9. Size

| Set | Functions | Instructions |
|---|---|---|
| Loader, derived fonts, id resolution, glyph metric (`0x00dcbd40`, `0x00dcb6c0`, `0x00dcaee0`, `0x00dcac20`, `0x00dcac80`, `0x00dcad10`, `0x00dcba50`) | 7 | ≈430 |
| Font set-up and named table (`0x00b9cab0`, `0x00b9bb00`, `0x00b9bb60`, `0x00b9b720`) | 4 | ≈340 |
| Resource-callback bodies (`0x00842dd0`, `0x00842df0`, `0x00842e20`, `0x00842ed0`, `0x00842ea0`, `0x00842f20`) | 6 | ≈170 |
| Vint font pool (`0x00e2fa20`, `0x00e2fae0`, `0x00e2fc70`, `0x00e2fcd0`, `0x00e2fd90`, `0x00e2fdd0`) | 6 | ≈300 |

---

## Changelog

- 2026-10-03 (local, quick fix from Team B's text-renderer merge): corrected §1 and §8 item 1 — `jap_nobdr`/`ug-debug_nobdr` textures ARE present in `misc.vpp_pc` (a miss in this file's original container scan, not a real absence); read their real dimensions (2,048 × 1,024 and 256 × 256) and, prompted by the same finding, cross-read the other three fonts' texture headers to confirm §2.2's "2,048-wide pages" HIGH note (now CONFIRMED for `font_header`/`font_header_pc`/`font_sk`/`jap`). `fnt_chinese.vf3_pc` is still genuinely absent; only language code `0xd` stays blocked. No sync-affecting cleanroom change (0/0, unchanged).
- 2026-10-03 (local, second correction, from `spec-localization-format.md`'s own language-selection read): §8 item 1's same-day narrowing is itself superseded — the real reason codes 3/`0xb`/`0xd` don't matter isn't data (fonts/textures present or not), it's that the language code at `0x01301a90` can never be set to any of the three on PC at all (Steam detection and the start-up override are both filtered to `US DE ES FR IT NL CZ PL RU`). Closed §8 item 4 in the same pass: code 3 = Japanese, `0xb` = Korean (confirmed by the platform-language name and `charlist_sk.dat`'s own header), `0xd` = Chinese; `font_sk`'s glyph range is charlist-assigned Hangul, not raw Unicode Greek/Cyrillic as previously guessed. No sync-affecting cleanroom change (0/0, unchanged).
- 2026-10-03 (local, new spec, UI-render unit U5 — fonts; numbered U5 because a parallel unit had already taken U4 and `spec-vint-render.md` §13 for `gradient`/`bitmap_circle`/`clip`/mask): initial version. First specification of the `.vf3_pc` format (version 4, `0xd0`-byte header, kerning/glyph/x/y tables; layout confirmed byte-exact on all 6 shipped files), the loader and its `_nobdr` texture rule, derived (scaled) fonts, the game's 7-slot named font table and the shipped six-name set-up per language, the Vint 10-entry font pool, and the six metric callbacks layout uses. Answered `spec-vint-render.md` §11 item 11: the font-table's second column is a draw-first secondary font, but every shipped `_overlay` name either repeats its primary (drawn once) or, in three language arrangements, a parameter-identical copy (drawn twice in place) — never an outline/fill pair. Found a cross-glyph kerning false match in the shipped lookup, and a measure/draw advance mismatch for characters a font lacks. Corrected the scoping note: `0x00b9cab0` is the font set-up, not the file loader (`0x00dcb6c0`). Cleanroom self-check: 0 hits (the regex and the ten-fragment plain-substring backstop, run over the finished file).
