# Interp note — U3: per-type draw (vtable `+0xbc`) for `bitmap`, `text`, `group`, traced through the callback table into the game's 2D wrappers

**Date:** 2026-10-03 · **Unit:** U3 of the proposed Vint render-path order (`review/scoping-ui-render.md` §4), scoped by the caller to the three most-populated element types. Investigation only: no spec file was edited. For whoever folds it into `spec-vint-render.md` (a per-type draw section plus the callback contract) and, incidentally, `spec-render-pipeline.md`.

**Shared conventions:** the same as `interp_vint_u1_property_tables.md` (U1) and `interp_vint_u2_tree_transform.md` (U2) — plain hex addresses, my own words, labels **CONFIRMED** (read directly in this pass) / **HIGH** (inferred from structure) / **HYPOTHESIS**, no Ghidra auto-names and no decompiler output; property, type and other strings are the engine's own shipped data, quoted as data. Cited without re-derivation:
- U1 §4.4/§4.6/§4.7: the `group`/`bitmap`/`text` descriptor rows and their getter/setter vtable slots.
- U2 / `spec-vint-render.md` §4.1: the 128-byte composed render state (`+0x04` tint r/g/b, `+0x10` alpha, `+0x34`…`+0x48` the 2×3 matrix `M`, row-major `m0 m1 m2 / m3 m4 m5` with the translation column always 0, `+0x4c`/`+0x50` the content-box origin, `+0x65` pass byte) and §2.3 (the walk calls vtable `+0xbc`(state, scratch, capacity) as a `this`-call).
- `spec-vint-render.md` §9: callback slots `+0x00`, `+0x08`, `+0x0c`, `+0x10`, `+0x14`, `+0x18`, `+0x1c` (argument shapes on the Vint side).

**Method.** As in U1/U2, the executable's bytes were read directly; Ghidra was not used.
- `SaintsRowTheThird.exe` via `tools/harnesses/exe_va.py` plus capstone. Scratch scripts (session scratchpad): U2's recursive-descent lister `u2dis.py` (re-used unchanged), `u3vt.py` (u32 table / vtable dump), plus one-off capstone windows for jump-table cases.
- Every function cited below was listed in full unless marked otherwise. Double constants were read as 8-byte values (the lister's inline annotation only shows the low dword).

---

## 1. `bitmap`'s draw, in full — CONFIRMED

### 1.1 The draw method `0x00e312b0` (31 insns, `bitmap` vtable `0x01258e04` slot `+0xbc`)

`this`-call, `ret 0xc` (state, scratch, capacity — only the state is used).
1. Resolves the element's image: the value record at element `+0xa0` is passed to `0x00e24150`, which returns null unless the record's type field is `0xa` (image), else looks its `+8` handle up with `0x00e30260` (the bitmap-resource pool, §1.4). **No image → no draw at all** (nothing is sent to the game, not even the colour).
2. **Colour:** `0x00e23450(&state.tint, state.alpha)`.
3. **Quad:** `0x00e30470` on the resource object, with `(&state.M, &state.origin, &element[+0xb8], &element[+0xc0])`.

### 1.2 `0x00e23450` — pack and send the colour (59 insns)

`cdecl (float rgb[3]*, float alpha)`. Each of r, g, b and alpha is multiplied by the double `255.0` (`0x012a2d88`) and converted to an integer with the x87 control word forced to **truncate** (chop, `| 0xc00`); the low byte of each is kept. The four bytes are packed in memory order **r, g, b, a** and their address is passed to **callback slot `+0x04`**, if the table pointer and the slot are non-null.

- No clamping: tint/alpha are the multiplicative products from the walk (U2 §4.1), and an out-of-range product wraps modulo 256 (e.g. a composed alpha of 1.2 → 306 → byte 50). HIGH that authored values stay in range; flagged.
- Shared with `gradient`/`bitmap_circle` per the scoping note §1.4 (not re-read here).
- **Correction to the scoping note §1.5:** slot `+0x04` is **"set current colour"**, not "text state/setup". `bitmap` calls it (via `0x00e23450`), and so does `text` (§2).

### 1.3 `0x00e30470` — resource → callback slot `+0x20` (37 insns)

`this` = the bitmap-resource object; `ret 0x10`.
1. If resource `+0x04` (the engine texture handle) is `−1`, return — no draw.
2. Copy `source_nw` (two **integers**, the 8 bytes at element `+0xb8`) to a local.
3. If resource `+0x0c` (its mode) is `1` (an image packed in a bitmap sheet, §1.4), look up its 24-byte sheet record with `0x00e21850(resource[+8])` and add that record's `+0x08`/`+0x0c` (the sub-image's x/y position inside the sheet's page, integers) to the local copy.
4. Call thunk `0x00e22870` (→ **callback slot `+0x20`**) with
   `(texture handle, &M, &origin, &adjusted source_nw, &element[+0xc0])` — `cdecl`, 5 arguments.

**Only `source_nw` is shifted into page coordinates; the `+0xc0` pair is passed through untouched.** That is consistent only because the game side (§4.3) treats the `+0xc0` pair as an **extent**, not a corner.

### 1.4 The bitmap-resource object and the image handle (enough to explain the two fields)

- **Pool** (`0x00e30260`, 16 insns): 36-byte records at `0x02a88178`, at most `0xf0`; a handle is `(serial << 16) | index`, and the lookup checks record `+0x14` == the full handle (stale handles resolve to null). Record fields read in this pass: `+0x04` texture handle (`−1` = none), `+0x08` sheet-record index (mode 1), `+0x0c` mode, `+0x10` name CRC, `+0x14` handle, `+0x18` reference count, `+0x1c`/`+0x20` circular list links (live list head `0x02a88168`, free list head `0x02a8816c`). Class `vint_resource_bitmap` (RTTI, vtable `0x01258534`, two slots: `0x00e2fe10`, `0x00e30290`).
- **Acquire by name** (`0x00e30120`, 109 insns, the `image` setter's string path) hashes the name (`0x00d9e8b0`), then tries **`0x00e2ffe0`** (by CRC): an existing live record with that CRC is re-used (ref count +1); otherwise `0x00e22490` looks the CRC up in the bitmap-sheet directory (`0x0132bd18`, `0x00e21ee0`) and, if found, makes a **mode-1** record whose texture handle is the sheet page's (`0x00e21820`: 24-byte sheet record `+0x04` → 12-byte page table `0x02a4d18c`). Only if the CRC is in no sheet does `0x00e30120` fall back to a **mode-0** record: texture handle = **resource-callback-table** slot `+0x00`(name) (table `0x02317a78`, pointer `0x02a5a004`, read via `0x00e22820`; game function `0x00842cf0`).
- **Image size** (`0x00e30360`): mode 1 → the sheet record's `+0x10`/`+0x14` (`0x00e21770`); mode 0 → resource-callback slot `+0x0c`(&w, &h, texture) (game `0x00842d90`; argument order as pushed in `0x00e303a1`–`0x00e303ae`).
- The 24-byte sheet records at `0x02a4e398` and the 12-byte page table at `0x02a4d18c` are therefore Vint's in-memory copy of the bitmap-sheet atlas (`spec-tables-environment.md` §17.4 BitmapSheets, HIGH by role; the filler of these tables was not read — U4).

### 1.5 What element `+0xb8` and `+0xc0` are — CONFIRMED from the setters/getters

Read from `bitmap`'s vtable (`0x01258e04`) accessor bodies for the rows of U1 §4.6:

| Element offset | Content | Written by |
|---|---|---|
| `+0xa0` (16-byte value record) | `image`: type `0xa`, `+8` = resource handle | `image` setter `+0xcc` → `0x00e31320`; `image_crc` setter `+0xd4` → `0x00e31400` |
| `+0xb8`/`+0xbc` | **`source_nw`** (x, y) as **int32** | setter `+0xdc` → `0x00e314c0`; getter `+0xd8` → `0x00e349f0` (int → float) |
| `+0xc0`/`+0xc4` | **`source_se`** (x, y) as **int32** | setter `+0xe4` → `0x00e315c0`; getter `+0xe0` → `0x00e31560` |
| `+0xc8`/`+0xcc` | image width/height (int32, texels) | `0x00e30360`, from the image setters |
| `+0xd0` | `custom_source_coords` (byte) | setter `+0xec` → `0x00e31660`; getter `+0xe8` → `0x00e316b0` |

Behaviour of these accessors:
- **`source_nw`/`source_se` setters** convert each float component to an int with `0x00dad900` — **round half away from zero** (`±0.5`, then truncate; `0x012a2dc0` = 0.5). If an image is set, each component is then clamped to **at most** the image width/height (`+0xc8`/`+0xcc`); there is **no lower clamp** and no ordering check against the other corner.
- **The `source_se` setter always returns false** (`0x00e3164f`: `xor al, al`), although it has stored the value; the `source_nw` setter returns true. HIGH: harmless to the loader (which applies and moves on), but a Lua `vint_set_property` on `source_se` would see a failure result. Not traced into the Lua wrapper.
- **`image` setter** (`0x00e31320`, 74 insns): accepts a value record of type 2 (handle, via `0x0067b160` + `0x00e30260`), type 4 (string, via `0x00e30120`) or `0xa` (an already-resolved image record); stores the new resource into `+0xa0` (`0x00e24090`, which also releases the old one through the resource's vtable slot `+4`); reads the image size into `+0xc8`/`+0xcc`; **if `custom_source_coords` is clear, resets `source_nw` = (0, 0) and `source_se` = (w, h)**; then **sets `unscaled_size` (`+0x74`/`+0x78`) = (w, h) as floats**, re-applies `auto_offset` (vtable `+0xc4`), and calls the resource's vtable `+4`.
- **`image_crc` setter** (`0x00e31400`, 58 insns): same, by CRC (`0x00e2ffe0`), **and additionally resets `scale` to (1, 1)** — the `image` setter does not touch `scale`.
- **`custom_source_coords` setter**: stores the byte; when the new value is false, resets `source_nw`/`source_se` to `(0,0)`/`(w,h)` exactly as above.

**So the U2 note's "two element fields `+0xb8`/`+0xc0`" are `source_nw` and `source_se`**, as integer texel coordinates in the image's own space, and the draw passes them (after the sheet-page shift of `source_nw`) to the game.

### 1.6 What the quad actually is (game side, details in §4.3)

For `bitmap`, with `T` = the texture page's size in texels (PEG header), `p` = the sheet sub-image's page position (0 for a mode-0 image), `nw` = `source_nw`, `se` = `source_se`, `o` = state origin, `M` = state matrix:

- **UVs:** `u0 = (p.x + nw.x)/T.w`, `u1 = u0 + se.x/T.w`; `v0 = (p.y + nw.y)/T.h`, `v1 = v0 + se.y/T.h`.
- **Positions:** `o`, `o + M·(se.x, 0)`, `o + M·(0, se.y)`, `o + M·(se.x, se.y)`.

**`source_se` is used as an extent (width, height), not as the south-east corner.** With the defaults (`nw` = 0, `se` = image size) this is exactly the whole image at one content-box unit per texel, matching `unscaled_size` (= image size). With `custom_source_coords` and a non-zero `source_nw` (2 records in the 159 shipped documents, U1 §4.6), the drawn rectangle is `nw … nw + se` in the image and the quad is `se` units big, **not** `unscaled_size` — the bounds/geometry chain (U2 §7, which uses `unscaled_size`) and the drawn quad then disagree. HIGH that this is the shipped behaviour (both sides read directly); whether authors compensate in the data is not checked (OPEN).

The quad's corner positions are the same four `origin + M·{…}` corners the query chain builds (U2 §7.1), but with `se` in place of `unscaled_size`.

---

## 2. `text`'s draw, in full — CONFIRMED

**Shape in one line:** `text` does no layout at draw time. Layout (§2.4) has already turned the string into a small **command stream** stored on the element; the draw just replays that stream, once for an optional drop shadow and once for the text itself. Glyph runs go to callback slot `+0x2c`, inline images to `+0x20`, colour changes to `+0x04`.

### 2.1 The draw method `0x00e31b50` (78 insns, `text` vtable `0x01259124` slot `+0xbc`)

`this`-call, `ret 0xc` (state, scratch, capacity — only the state is used, as for `bitmap`). The vtable's pre-draw and post-draw slots (`+0xb8`/`+0xc0`) are both `0x00754410`, a bare `ret`, so nothing else runs around it. Recall that the walk never calls this method in render pass 1 (`spec-vint-render.md` §2.3 step 5).

1. **Gate.** If element `+0x104` (the command-stream pointer, §2.3) is null, return. Nothing is sent to the game, not even a colour. **CONFIRMED.**
2. **Shadow pass**, only if byte `+0xe6` (`shadow_enabled`) is non-zero:
   - colour r/g/b = element `+0xd8`/`+0xdc`/`+0xe0` (`shadow_tint`) × state tint r/g/b, component by component;
   - alpha = element `+0xb8` (`shadow_alpha`) × state alpha;
   - sends that colour through `0x00e23450` (§1.2: ×255, truncate, pack r,g,b,a, callback `+0x04`);
   - builds a shifted origin: state origin (`+0x4c`/`+0x50`) **plus** element `+0xb0`/`+0xb4` (`shadow_offset`), added directly;
   - replays the stream: `0x00e374a0(stream, length, &state.M, &shifted origin, &shadow rgb, shadow alpha, 0)`.
3. **Main pass**, always:
   - `0x00e23450(&state.tint, state.alpha)`;
   - `0x00e374a0(stream, length, &state.M, &state.origin, &state.tint, state.alpha, 1)`.

**Consequences, all CONFIRMED from the body:**
- The shadow is drawn **first**, so it sits underneath the text.
- **`shadow_offset` is in device pixels.** It is added to the origin after the whole transform chain (screen fit, every ancestor's scale and rotation), so it is **not** scaled or rotated with the element and **not** scaled by the document's screen fit. The same authored offset is a bigger visual fraction of the text at 720p than at 1080p. A host that renders in document space has to divide it by `fitScale` to match.
- The shadow colour is the element's **inherited** tint × `shadow_tint`, not `shadow_tint` alone. With the default black `shadow_tint` this makes no difference; with a coloured shadow under a tinted parent it does.

### 2.2 Shadow and font fields — CONFIRMED from the accessors and the initialiser

| Element offset | Property (U1 §4.7 row) | Accessors read | Default (`0x00e31940`, called from the constructor `0x00e319a0` on element `+0xa0`) |
|---|---|---|---|
| `+0xa8`/`+0xac` | `text_scale` (x, y), floats | getter `+0xe0` → `0x00e32210`; setter `+0xe4` → `0x00e32250` (also sets layout-dirty bit 2 of `+0x10c`) | (1, 1) |
| `+0xb0`/`+0xb4` | `shadow_offset` (x, y), floats | getter `+0x140` → `0x00e32ec0`; setter `+0x144` → `0x00e32e60` (a value of the wrong type stores (0, 0)) | (0, 0) |
| `+0xb8` | `shadow_alpha`, float | getter `+0x150` → `0x00e32ff0`; setter `+0x154` → `0x00e32fb0` (value converted by `0x0071b9b0`) | 1.0 |
| `+0xcc` | `font`, a handle into the 10-entry Vint font pool at `0x02a88050` (28-byte records; lookup `0x00e2fc70` checks the record's `+8` against the full handle) | getter `+0xc8` → `0x00e32120`; setter `+0xcc` → `0x00e32150` (also sets layout-dirty bit 2) | 0 (none) |
| `+0xd8`/`+0xdc`/`+0xe0` | `shadow_tint` (r, g, b), floats | getter `+0x148` → `0x00e35570`; setter `+0x14c` → `0x00e32f20` (wrong type stores (0, 0, 0)) | (0, 0, 0) — black |
| `+0xe6` | `shadow_enabled`, byte | getter `+0x138` → `0x00e32e10`; setter `+0x13c` → `0x00e32dd0` (value converted by `0x007cb5b0`) | 0 (off) |
| `+0x104` | command-stream pointer (heap) | written only by layout `0x00e31c50` and freed by `0x00e31a00` | null |
| `+0x108` | command-stream length in bytes | written by layout `0x00e31c50` | — |

None of the shadow setters marks the layout dirty, and the draw reads all of them directly every frame. So animating `shadow_offset`, `shadow_tint`, `shadow_alpha` or `shadow_enabled` costs nothing extra; changing `text_scale` or `font` forces a re-layout. **CONFIRMED.**

Not read by the draw at all: `word_wrap`, `wrap_width`, `horz_align`, `force_case`, `leading`, `kerning`, `text_tag`, `insert_values` and the `line_frame_*` rows. They act only through the stream that layout writes. **CONFIRMED** (the draw body reads only the fields above plus the state).

### 2.3 The command-stream player `0x00e374a0` (387 insns) — CONFIRMED

`cdecl (stream, length, &M, &origin, &base rgb, base alpha, colour-commands-enabled byte)`; it returns nothing.

**Framing.**
- If the stream pointer is null, or the length is **below 2**, nothing happens.
- A cursor starts at byte 0. While it is below the length, the player reads a 4-byte opcode at the cursor, then a fixed-size payload.
- **Any** of the following stops the whole replay at once (the rest of the stream is dropped, with no error): the opcode word or the payload would run past `length`; an opcode outside 1–4.
- All payload fields are 4-byte little-endian (floats unless noted). Records are 4-byte aligned. The layout pads glyph runs to keep them aligned (below).

**The four opcodes.** Below, `M·(x, y)` means the state's 2×3 matrix applied to a point (`m0·x + m1·y + m2`, `m3·x + m4·y + m5`; `m2` = `m5` = 0 in practice, U2 §4.1), `o` is the origin handed in, and `S(sx, sy)` is a pure scale matrix.

| Op | Payload | What the player does |
|---|---|---|
| **1** | 8 bytes: `x, y` | Computes `M·(x, y)` into a scratch pen and moves on. Nothing in the player ever reads that scratch value back before overwriting it (opcodes 2 and 3 each recompute their own position), and no layout emitter of opcode 1 was found (see the note under the table). **HIGH: a vestigial "move pen" command.** |
| **2** | 28 bytes + text: `font`, `x`, `y`, `sx`, `sy`, `count` (int), `spacing` (low byte used, §4.2); then `count` UTF-16 code units | **Glyph run.** Position `p = o + M·(x, y)`; glyph matrix `G = M · S(sx, sy)` (product helper `0x0078b790`, `out = this · arg`, double precision). Calls thunk `0x00e22850` → **callback slot `+0x2c`** with `(font, &text[0], 0, count, &G, &p, spacing)`. Advances the cursor past the `count × 2` text bytes. The text bytes themselves are **not** bounds-checked against `length` before the call. |
| **3** | 20 bytes: `sheet index` (int), `x`, `y`, `sx`, `sy` | **Inline image** from the bitmap-sheet atlas. Looks up the 24-byte sheet record with `0x00e21850(index)`. If the index is out of range, the record is skipped (the replay continues). Otherwise: `p = o + M·(x, y)`; `G = M · S(sx, sy)`; texture handle = `0x00e21820(index)` (the page's, as in §1.4; −1 if the page index is bad); calls thunk `0x00e22870` → **callback slot `+0x20`** with `(texture, &G, &p, &record[+0x08] (page x, y), &record[+0x10] (width, height))`. This is exactly the `bitmap` call shape (§1.3): sub-image position as the source corner, its size as the extent. |
| **4** | 16 bytes: `r, g, b, a` | **Colour change.** Ignored entirely when the last argument is 0 (the shadow pass). Otherwise packs 4 bytes and calls thunk `0x00e22830` → **callback slot `+0x04`** (the same "set current colour" slot as §1.2): r, g, b come from the payload **unless the payload's `r` equals −1.0**, in which case all three come from the base rgb argument; a comes from the payload **unless it equals −1.0**, in which case it is the base alpha. Each channel is ×255 (the double at `0x012a2d88`) and **truncated**, low byte kept — the same arithmetic as `0x00e23450`. |

**Opcode 1 note.** A linear sweep of the text-layout code (`0x00e35e00`–`0x00e374a0`) for opcode stores found writers of opcodes **2** (`0x00e360ce`, in the run builder `0x00e36070`), **3** (`0x00e366f0`, in the markup path; `0x00e36bf8`, `0x00e36cd2`, `0x00e36d56`, in `0x00e36bd0`) and **4** (`0x00e364fc`, in `0x00e364e0`), and none of opcode 1. The sweep only catches a store of an immediate, so opcode 1 is "not found", not "proved absent" (OPEN, low priority, since the player gives it no visible effect either way).

**Three behaviours a replacement must copy:**
1. **Explicit colours are not modulated.** When opcode 4 carries an explicit `r` (≠ −1), that rgb replaces the element's composed tint; it is **not** multiplied by it. When it carries an explicit `a` (≠ −1), that alpha replaces the element's composed alpha, **so a parent's fade does not reach that text**. Only the −1 sentinel inherits. Layout seeds the base entry of its colour stack with (−1, −1, −1, −1) (`0x00e36ec8`–`0x00e36f26`), so untagged text always inherits. **Whether any markup ever writes an explicit alpha** (and so escapes fades) was not traced (OPEN; the markup parser is the text-layout unit's).
2. **The shadow pass is monochrome but not image-free.** With colour commands off, every glyph run in the shadow pass uses the shadow colour. **Inline images (opcode 3) are still drawn in the shadow pass**, at the shadow offset, and the `+0x20` callback draws them in the current colour (§4.3), i.e. tinted with the shadow colour.
3. **Colour persists across records.** Opcode 4 sets the game's *current* colour, which every later glyph run and image uses until the next opcode 4. The colour is **not** reset to the base colour at the end of the stream; the next element's draw sets its own colour before it draws anything (`bitmap` §1.1, `text` §2.1), so nothing leaks between elements in practice. HIGH: this relies on every drawing type setting the colour first, which holds for the two types read here.

### 2.4 Where the stream comes from (enough to read it; layout itself is the text-layout unit's)

- **Builder `0x00e31c50`** (177 insns, also reached as vtable `+0x24` → `0x00e330d0`, a one-instruction jump to it). It clears layout-dirty bit 2 of `+0x10c`, resolves the font (`0x00e2fc70(+0xcc)`), decodes the tagged string (`+0xf4`/`+0xf8`, via `0x00e227c0` or the decoder `0x00e22ac0`, then `0x00e22b30` with a 0x1000-unit buffer), then lays it out with **`0x00e36dd0(stack buffer, 0x3200, &element[+0xe8], decoded text, &element[+0xa0])`**. The return value is the stream length in bytes.
  - The stream is built in a **0x3200-byte (12,800-byte) stack buffer**, so that is the hard ceiling for one element's stream; every emitter checks the remaining space and silently stops emitting when it runs out. HIGH that very long texts are truncated rather than failing.
  - A non-zero length is copied to a heap block from the allocator table whose pointer is at `0x02a5a018` (slot `+0x00` called as (old block, new size), slot `+0x04` frees), and stored at `+0x104`/`+0x108`. A zero length, no font, or no string frees the block and leaves `+0x104` null, which is what the draw's gate (§2.1 step 1) tests.
  - The builder then copies the measured size (`+0xe8`/`+0xec`) into **`unscaled_size`** (`+0x74`/`+0x78`) and re-applies `auto_offset` (vtable `+0xc4`).
- **Run records (opcode 2), from the builder `0x00e36070`** (310 insns): `font` = the first dword of the Vint font record (the engine font handle); `sx`/`sy` = the current entry of layout's style stack (`0x00e35bb0`: 16 entries of 24 bytes at layout context `+0x2c`, index at `+0x1ac` capped at 15; each entry holds scale x, y then colour r, g, b, a); `spacing` = the low byte of layout context `+0x19`, which is the per-glyph advance adjustment that layout also adds when measuring (`0x00e36297`: font advance + that byte). Only that **low byte** is written; the other three bytes of the field are whatever was in the stack buffer. **A run with an odd count is padded with one 0 code unit and the count incremented** (`0x00e363d2`–`0x00e363f7`), which is what keeps the next record aligned. So `count` can include a trailing NUL, and the glyph callback must draw nothing for code unit 0.
- **Line-frame images (opcode 3), from `0x00e36bd0`** (167 insns): up to three opcode-3 records per line, for a descriptor whose three sheet indices are at `+0`, `+4`, `+8` (−1 = absent). Their positions are patched later through the layout's line table, as glyph-run positions are. These are the `line_frame_w`/`_m`/`_e` images (HIGH by the count of three and by the text element owning exactly three bitmap handles at `+0xc0`/`+0xc4`/`+0xc8`, which its teardown `0x00e31a00` releases). Opcode 3 takes a **sheet** index, so a line-frame image can only be drawn if it lives in a bitmap sheet (HYPOTHESIS for any standalone-file image; the handle → sheet-index step was not read). `line_frame_enable` has **0** records in the 159 shipped documents (U1 §4.7), so this path is live only if Lua turns it on.

### 2.5 What a replacement has to do for `text`, in short

1. Keep a per-element display list of three record kinds (glyph run, sheet image, colour change), rebuilt only when layout inputs change (the dirty bit), sized in the element's own content-box units.
2. At draw: if `shadow_enabled`, draw the list once with colour = `shadow_tint`·tint and alpha = `shadow_alpha`·alpha, colour changes suppressed, origin shifted by `shadow_offset` **in device pixels**; then draw it again normally.
3. Each glyph run is placed at `origin + M·(x, y)` with glyph-space matrix `M·S(sx, sy)`; each sheet image is a `bitmap`-style quad with matrix `M·S(sx, sy)`, source corner = the sub-image's page position, extent = its size.
4. Colour changes: −1 means inherit; anything else **replaces** the composed tint or alpha.

---

## 3. `group`'s draw — CONFIRMED: an empty body

### 3.1 The slot

`group`'s vtable `0x012588a4`, read directly in this pass:

| Slot | Target | Body |
|---|---|---|
| `+0xb8` (pre-draw) | `0x00754410` | `ret` — 1 instruction |
| **`+0xbc` (draw)** | **`0x00abd220`** | **`ret 0xc` — 1 instruction**: pops the three arguments (state, scratch, capacity) and returns. Reads nothing, writes nothing, calls nothing. |
| `+0xc0` (post-draw) | `0x00754410` | `ret` |

The same three targets fill these slots in `point` (`0x01259524`) and `document` (`0x0125a264`), also read in this pass. `0x00abd220` sits outside the Vint address range, so it is a generic `ret 0xc` stub shared by any three-argument method with an empty body (HIGH: identical-code folding by the toolchain). Nothing about it is `group`-specific.

**So a `group` draws nothing of its own: no background, no border, no clip, no debug outline.** This is not an assumption from "groups are containers"; the slot body is a single return. It matches the scoping note §1.4, which listed `group` among the types left on the shared no-op.

### 3.2 What a `group` still does during the walk (all from `spec-vint-render.md` §2.3/§4, not re-derived)

The empty draw does not make a `group` inert. Every step of the walk around the draw still runs for it:
- **Culling** (`visible`, own and inherited alpha). A hidden or fully transparent `group` drops its whole subtree.
- **State composition.** Its `anchor`, `offset`, `scale`, `rotation`, `tint`, `alpha`, `render_mode`, `mask` and `background` all reach its children through the 128-byte state.
- **The per-element callbacks** `+0x18` (effective `render_mode`) and `+0x1c` (inherited `background` flag) fire for a `group` exactly as for a drawing type.
- **Mask switching** when it has `mask` children, and the clip-stack unwind after its child loop.

The one `group`-specific behaviour on this path is query-time, not draw-time: its bounding box is the union of its element children only (`spec-vint-render.md` §7.1, override `0x00e308b0` in slot `+0x2c`).

### 3.3 What a replacement has to do for `group`

Draw nothing. Apply the shared walk (cull, compose, push `render_mode`/`background`, mask/clip bookkeeping, sorted children) exactly as for any other element.

---

## 4. The game side of the three slots these types use (`+0x04`, `+0x2c`, `+0x20`)

**Installation — CONFIRMED.** The UI bring-up `0x008489e0` writes the 18-slot table at `0x02317a30` by absolute stores (`0x00848b7b`–`0x00848c25`) and hands it to `0x00e225f0`, which stores the pointer at `0x02a5a008`. For the slots used here: **`+0x04` = `0x00847130`**, **`+0x20` = `0x00847d40`**, **`+0x2c` = `0x00848890`**. The Vint-side thunks (`0x00e22830` → `+0x04`, `0x00e22870` → `+0x20`, `0x00e22850` → `+0x2c`) each read the table pointer and the slot, and tail-jump to the slot only if both are non-null (8 insns each). So the arguments the Vint side pushes arrive unchanged as a `cdecl` call. (The sibling setters `0x00e225e0`/`0x00e22600`/`0x00e22610`/`0x00e22620` store the other installed tables at `0x02a5a004`/`0x02a5a00c`/`0x02a5a010`/`0x02a5a014`; the allocator table used by text layout is at `0x02a5a018`.)

Common to all three: the game's own 2D layer is reached through small wrappers that load the draw object from `0x0351f91c` and call an `rl_draw` method, and through a per-thread UI draw context returned by **`0x00846f10`** (TLS slot index at `0x02cc4700`, object cached at TLS `+0x1c`, created on first use through the object at `0x02317c30`). Context fields used here: byte `+0x09` (colour-space switch, §4.1), dword `+0x0c` (last colour sent), and the block at `+0x10` (passed to every primitive as its state; HIGH that this is where the `+0x18`/`+0x1c` per-element pushes land, not traced).

### 4.1 Slot `+0x04` — set current colour: `0x00847130` (84 insns) — CONFIRMED

Argument: a pointer to 4 packed bytes r, g, b, a.
1. **Optional sRGB → linear conversion.** If context byte `+0x09` is 1, the bytes are expanded to floats (÷255) and r, g, b go through `0x008433e0`: `c ≤ 0.04045` → `c / 12.92`, otherwise `((c + 0.055) / 1.055)^2.4` (constants at `0x01162f48`, `0x01118e90`, `0x01118e78`, `0x01118e80`, `0x01156908`; power routine `0x00ea4300`). This is the standard sRGB decoding curve. **Alpha is passed through unchanged.** The four values are then re-quantised as `×255 + 0.5`, truncated (round to nearest).
2. **Redundancy filter.** If the resulting packed dword equals context `+0x0c`, return; nothing is sent.
3. Otherwise **`0x005d91a0`**: expands the bytes to floats (× the double at `0x012a2e48`, which is 1/255 rounded up slightly) and calls **`rl_draw` `0x00e4ae90`** (per-thread draw state at TLS `+0x674`, then `0x00e482e0`). Then it records the dword at context `+0x0c`.

What sets context byte `+0x09` was not traced (OPEN). If it is set, the UI's colours are linearised before they reach the vertex colour, which matters for any host that composites in linear space.

### 4.2 Slot `+0x2c` — glyph run: `0x00848890` (43 insns) → `0x00b9cf20` (60) → `0x00b9bc10` (773)

**`0x00848890(font, text, start, count, &G, &p, spacing)` — CONFIRMED.**
- Returns without drawing if `text` is null or `text[0]` is 0. Note that it tests element 0, not `text[start]`; the Vint side always passes `start` = 0.
- Splits the 2×3 matrix `G` into its two rows, `(g0, g1, g2)` and `(g3, g4, g5)`, as two 3-float locals.
- Calls `0x00b9cf20(p.x, p.y, text + start, count, &row0, &row1, font, &context[+0x10], spacing)`.

**`0x00b9cf20` — CONFIRMED.**
- **Font index check.** The `font` value is checked against the count at `0x0290f548`. An out-of-range font **falls back to entry 0**; it does not fail.
- The font table at `0x0290f550` has 12-byte entries. The routine draws the run with `0x00b9bc10` using the sub-font id at entry `+8` (if not −1), then **again** with the id at entry `+4` (if not −1 and different from `+8`).
- So a font can be two layers drawn one over the other. What the two layers are is OPEN (HYPOTHESIS: an outline or effect layer plus a fill layer); this belongs to the font unit.

**`0x00b9bc10` — structure CONFIRMED; per-glyph geometry not read (font unit).**
- Resolves the sub-font record (`0x00dcac20`) and binds its texture, record `+0xb8`, with `0x005d8ff0`.
- Runs a pre-check `0x00b9b7e0`; a result of 1 means nothing is drawn.
- Reads back the **current colour** (`0x005d9090`) and packs it the same way as §4.3, so glyphs take the colour last set through `+0x04`.
- Gets the texture size (`0x00dcf390`) and computes `1/w`, `1/h`, `0.5/w`, `0.5/h`. Half-texel constants are present here but **not** in the bitmap quad (§4.3); where they are applied was not traced.
- Emits glyphs in **batches of at most 64**: `0x005d9c80(&context[+0x10])` (→ `rl_draw` `0x00e4bf30`), then `0x005d9ca0(6·n)`, which passes `6n / 3` triangles to `rl_draw` `0x00e4bff0` (a triangle list, **two triangles = 6 vertices per glyph**). The vertices use the same 28-byte stride as §4.3.
- **The `spacing` argument is read as a signed byte** (`movsx` at `0x00b9c1aa`, which reads the 9th argument at that stack depth) and added to each glyph's advance. This makes harmless the three unwritten bytes of the stream field (§2.4).
- Glyph metrics come from `0x00dcbde0`. The routine ends with `0x00b9ba20` and `0x005d9c70` (→ `rl_draw` `0x00e4bf20`).

### 4.3 Slot `+0x20` — textured quad: `0x00847d40` (328 insns) — CONFIRMED

Arguments: `(texture, &M, &o, &nw, &ext)`, where `nw` and `ext` are pairs of **int32**. These are the five arguments `bitmap` (§1.3) and `text`'s opcode 3 (§2.3) both pass.

1. **Texture.** `0x005d8ff0(texture)` → `rl_draw` `0x00e4aef0`.
2. **Vertex colour.** `0x005d9090` → `rl_draw` `0x00e4aec0` reads back the current colour as 4 floats. Each is ×255 and truncated, then packed in byte order **b, g, r, a** (`0x00847dae`–`0x00847dc2`), which is the D3D `D3DCOLOR` layout. The colour set by `+0x04` survives the round trip byte → float → byte exactly for all 256 values: the 1/255 constant at `0x012a2e48` is rounded up, so ×255 never falls below the integer. This was checked numerically over all 256 values; storage inside `rl_draw` was assumed to be unchanged, which is HIGH, since `0x00e82d30`/`0x00e82d50` were not read.
3. **Texture size `T`.**
   - If `texture` is −1, `T` = (10, 10). Text's opcode 3 can produce −1 through `0x00e21820`; `bitmap` never does (§1.3 step 1).
   - Otherwise `0x00dcf390(texture, &w, &h)` looks the handle up (`0x00dced00(handle, 1)`) and reads **u16 width and height at `+0x08`/`+0x0a`** of that texture's header record (HIGH: the PEG per-texture header, `spec-texture-format.md`; not cross-read here).
   - If the lookup fails, `w`/`h` are left as uninitialised stack values. HIGH that this never happens for a live handle.
4. **Dead arithmetic.** It computes a normalised inverse of `M`'s 2×2 part (each column divided by its length, then inverted). The result is only ever multiplied by a zero vector, so it adds 0 to the UVs. There is one side effect: **if either column of `M` has zero length** (a zero `scale` on an axis), the 0/0 makes the UVs NaN. The quad is degenerate in that case anyway, so nothing visible changes. A replacement can omit the computation.
5. **UVs:** `u0 = nw.x / T.w`, `u1 = u0 + ext.x / T.w`, `v0 = nw.y / T.h`, `v1 = v0 + ext.y / T.h`. **No half-texel offset.**
6. **Positions:** `P0 = o`, `P1 = o + M·(ext.x, 0)`, `P2 = o + M·(0, ext.y)`, `P3 = o + M·(ext.x, 0) + M·(0, ext.y)`. Each `M·` product includes `m2`/`m5`, which are 0 in practice.
7. **Submission.**
   - `0x005d9c30(&context[+0x10])` → `rl_draw` `0x00e4be40`.
   - `0x005d9c50(4, 0)`, which passes **4 − 2 = 2** to `rl_draw` `0x00e4bf00`: a 4-vertex, 2-triangle **strip** (HIGH: strip by the count arithmetic and the vertex order). It returns vertex memory or null.
   - If memory was returned, it writes 4 vertices of **28 bytes**: x at `+0x00`, y at `+0x04`, u at `+0x10`, v at `+0x14`, colour at `+0x18`. `+0x08`/`+0x0c` are not written here; HYPOTHESIS: they are pre-filled z/rhw.
   - The order is `P0 (u0,v0)`, `P1 (u1,v0)`, `P2 (u0,v1)`, `P3 (u1,v1)`.
   - It always ends with `0x005d9c70` → `rl_draw` `0x00e4bf20`.

This is the evidence behind §1.6: `ext` is an extent, applied through `M`, and the UV rectangle is `nw … nw + ext` in texels of the page.

---

## 5. Cross-observations

- **Colour is state, not an argument.** No drawing callback takes a colour. Every type sets the game's *current* colour through slot `+0x04` first, and the quad and glyph callbacks read it back as their vertex colour (§4.2, §4.3). The packing on the Vint side is the same everywhere: `bitmap` and the `text` base/shadow colours go through `0x00e23450`, and `text`'s opcode 4 repeats its arithmetic inline. In each case it is ×255, truncated, low byte kept, **no clamp**, bytes in order r, g, b, a. On the game side, colours may be sRGB-decoded (§4.1) and are re-rounded; repeats are filtered out.
- **One geometry contract for every textured quad.** `bitmap` and `text`'s inline images both call slot `+0x20` with `(texture, &M, &origin, source corner, extent)`, integers for the last two. `text` pre-multiplies `M` by a per-record scale `S(sx, sy)` and moves the origin by `M·(x, y)`; `bitmap` passes the state's `M` and origin unchanged. Glyph runs use the same `(M·S, origin + M·(x, y))` placement through slot `+0x2c`. The translation is always carried outside the matrix, as in U2.
- **Gates, not culls.** `bitmap` skips when its image or texture handle is missing, `text` when its stream is null, and `group` always. None of them tests bounds, screen extents or clip. The pass filter (render pass 1 skips `text` entirely, `spec-vint-render.md` §2.3) is the only per-type rule above them.
- **Genuine differences.**
  - `bitmap` is one quad.
  - `text` is a cached display list, replayed up to twice per frame (shadow, then main). Its explicit colours **replace** the inherited tint/alpha rather than modulate it.
  - `group` is a single `ret 0xc`.
- **Scoping-note corrections from this unit** (in addition to §1.2's correction of slot `+0x04`):
  - Slot `+0x2c` is "draw glyph run" (font, UTF-16 text, start, count, matrix, position, spacing), not just "glyph/metrics".
  - Slot `+0x20` draws a 4-vertex strip with **no half-texel offset**.
  - `text`'s main draw helper `0x00e374a0` is a command-stream player, not a layout routine.
- **Size read for this unit** (my lister's counts, from each entry):
  - `text`: draw 78 + player 387, plus 7 accessors, initialiser 32 and layout builder 177 read in full, and two emitters (310, 167) read for record shapes.
  - `group`: 1.
  - Game side: 84 + 74 (sRGB) + 31, 43 + 60 (+ 773 structural only), 328, and 8 one-call wrappers.
  - The scoping note's figures for these functions (78, 387, 177, 84, 43, 328) match exactly.
- **Footnote, out of scope, flagged for whoever takes up `gradient`:** `gradient`'s slot `+0xbc` (vtable `0x0125a514`) is **`0x00e312b0`, the same body as `bitmap`'s draw**. This fits `gradient`'s parent type being `bitmap` (U1 §4.6), and it is not `0x00e35410` as the scoping note §1.4 states. In this pass `0x00e35410` was found in `gradient`'s vtable at slot **`+0x130`**, a type-specific slot. How `gradient`'s corner colours reach callback `+0x28` when its draw slot is `bitmap`'s was not traced.

---

## 6. OPEN items

1. **Context byte `+0x09` (§4.1).** What sets the sRGB → linear colour switch, and whether the shipped game runs with it on. This decides whether a host should linearise UI colours.
2. **Explicit colours in text markup (§2.3).** Whether any markup tag emits an opcode 4 with an explicit alpha (which escapes parent fades) or explicit rgb (which ignores tint), versus −1 sentinels. This is for the text-layout unit, which owns the markup parser (`0x00e36dd0` and the emitters around `0x00e366f0`/`0x00e364e0`).
3. **Opcode 1.** No layout emitter was found by the immediate-store sweep (§2.3), and the player gives it no visible effect. Low priority.
4. **Two-layer fonts (§4.2).** What font-table entries `+4`/`+8` are, and the per-glyph geometry, UVs and half-texel use inside `0x00b9bc10` (read for structure only). Font unit, together with the `.vf3_pc` format.
5. **Line frames (§2.4).** How the three `line_frame_*` handles become sheet indices, what happens to an image that is not in a sheet, and the frame placement (`0x00e36bd0` negative offsets). Text-layout unit. Not exercised by shipped documents (`line_frame_enable` has 0 records).
6. **Context block `+0x10` (§4).** Its contents, and whether it is where the per-element `render_mode`/`background` pushes (`+0x18`/`+0x1c`) land. For the callback-contract / `render_mode` unit.
7. **Vertex fields `+0x08`/`+0x0c`** and the strip/list primitive types inside `rl_draw` `0x00e4bf00`/`0x00e4bff0`. Not read. Not needed for a D3D11 port beyond "screen-space position, uv, colour".
8. **Colour-cache coherence (§4.1).** Whether anything other than `0x00847130` changes the `rl_draw` colour without updating context `+0x0c`. If so, a later identical `+0x04` call would be skipped and leave a stale colour.
9. **`text` element `+0xa0`/`+0xa4`.** Two floats defaulting to 1.0 in the layout parameter block, read by layout; their meaning was not settled.
10. **Carried from §1:** the `custom_source_coords` geometry/bounds disagreement (§1.6), and the `source_se` setter's false return (§1.5). Neither was re-checked against data or the Lua wrapper.
11. **Live confirmation.** All of the above is static. A frame capture of one shadowed text element (two glyph batches, the offset in device pixels) and one `bitmap` with `custom_source_coords` would move §2.1 and §1.6 to CONFIRMED-by-data.
12. **Not crossed:** the `.czn_pc` zone-data interior and named-object resolution topic. The minimap (`sr2_map`, vtable `0x01259cb4`) appeared only as one row in this pass's draw-slot dump of all element vtables. Its slots were not followed.

---

## 7. Clean-room check

- **Plain hex addresses only.** There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names and no pasted pseudocode.
- **Own words.** Every behaviour is described in my own words from capstone listings of the executable's own bytes. The formulas (UVs, corner positions, the sRGB curve, the colour packing) are my algebraic restatements of the arithmetic.
- **No Ghidra.** It was not used for this unit. No project copy was made, so there is nothing to delete.
- **Scratch scripts** (session scratchpad, all read-only against the executable):
  - re-used: `u2dis.py`, `u3dis.py`, `u3vt.py`;
  - added this pass: `u3scan.py` (linear sweep with an operand regex) and `u3_colrt.py` (the 256-value colour round-trip check in §4.3);
  - the self-check: `u3_check.py`.
- **Shipped strings, quoted as data:** the engine's own property names (`shadow_offset`, `line_frame_enable`, …), type names, and the `.vf3_pc` extension cited from the scoping note. `D3DCOLOR` and `DrawPrimitiveUP`-family terms are public Direct3D API names. "sRGB" names a public standard curve. None of these is a Ghidra auto-name.
- **Self-check, run on the finished file as the literal last step.** I ran the project's standard auto-name pattern with Python `re` (`u3_check.py`). It is the pattern quoted in U1 §7, not repeated here so that this note stays at zero hits. **Result: 0 regex hits on the whole file.** I also ran the plain-substring backstop over the whole file, this section included, for the ten auto-name fragments listed in U1 §7. Four of them are checked with a trailing-character rule: a digit after the variable-name and parameter fragments and after the "undefined" fragment, a hex digit after the local-variable fragment. The fragments themselves are not spelled out here, again so that the note stays at zero hits. The backstop also catches the known regex gap, a register-variable name whose register part contains an underscore.
  
  **Result: 0 hits for each of the ten fragments, 0 in total.** Top-level headings are in strict order, §1 to §7.
  
  *(The first run of this check, made after §7 had been written, found 6 backstop hits. All six were in this section's own list of the fragment names, and there were none anywhere else. I removed the list, and the re-run gave the figures above.)*
- **No spec file was edited.**

