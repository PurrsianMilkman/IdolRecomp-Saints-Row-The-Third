# Interp note — U1: Vint property-descriptor tables for all 13 element types

**Date:** 2026-10-03 · **Unit:** U1 of `review/scoping-ui-render.md` §4 (item 2). Investigation only: no spec file was edited. These findings are for whoever folds them into `spec-lua-bindings.md` §9 and `spec-vint-doc-format.md` §5/§8.

**Shared conventions:** the same as the other `exe-notes-2026-10-01` interp notes.
- Addresses are plain hex. Descriptions are in my own words.
- Confidence labels are **CONFIRMED** (read directly in this pass), **HIGH** (inferred from structure) and **HYPOTHESIS**.
- No Ghidra auto-names and no decompiler output appear in this note.
- Property names, enumeration names, type names and other strings are the engine's own shipped data, quoted as data.

**Method.**
1. **Ghidra, read-only.** Local headless runs of `CrreishDump.java` against a private project copy, `tools\gp_vintu1`, which was deleted afterwards.
   - `func` mode on the 13 registration functions, the registration callee `0x00e290c0`, the hash-cache filler `0x00e28bd0`, the hash `0x00d9e740`, the type-registry helpers `0x00e30560`/`0x00e30590`/`0x00e305e0`/`0x00e30680`/`0x00e306e0`/`0x00e30710`/`0x00e30770`, and the enumeration lookup `0x00e307c0`.
   - `ptrs` mode on all 13 descriptor tables.
   - `xref` mode on the 10 enumeration holder objects.
   - `range` mode on the C-runtime static-initializer block `0x010153f0–0x01015720` and on `0x00e306c0`.
2. **Direct read of the executable.** The same static bytes were read straight from `SaintsRowTheThird.exe` with `tools/harnesses/exe_va.py` plus the capstone disassembler (scratch scripts `u1_tables.py`, `u1_enums.py`). This decoded every one of the 96 descriptor rows and all 69 distinct accessor thunks, and cross-checked the Ghidra `ptrs` dumps word for word.
3. **Population check against real files.** Every property record in the 159 distinct shipped `.vint_doc` files was matched against these tables. The walk grammar was copied from `tools/harnesses/vintdoc_loader_walk.py` (scratch script `u1_hist.py`).

---

## 0. Summary

- **All 13 types fully resolved.** That is 96 descriptor rows in total, and every row's every field has been read.
- **Row counts per type:**

  | Type | Rows |
  |---|---|
  | `element` | 17 |
  | `tween` | 16 |
  | `animation` | 3 |
  | `group` | 4 |
  | `clip` | 2 |
  | `bitmap` | 5 |
  | `text` | 21 |
  | `point` | 1 |
  | `video` | 9 |
  | `sr2_map` | 2 |
  | `bitmap_circle` | 7 |
  | `document` | 1 |
  | `gradient` | 8 |

  The range is 1–21 rows per type.
- **The 32-byte row layout of `spec-lua-bindings.md` §9.2 holds for all 96 rows.** In every row, `+0x10`, `+0x14` and `+0x18` are zero in the static image, and `+0x1c` is either `0x100` or `0`.
- **The name hash is confirmed by data.** It is the engine's lower-cased reflected CRC-32 with seed 0 and no final XOR (`0x00d9e740` with second argument 0).
  - Of the 37,008 property records in the 159 shipped documents, **36,919 (99.76 %) match a row of the element's own type or of one of its ancestors**.
  - The 89 that match nothing all carry one single unknown hash (§3.4).
- **Type-code vocabulary.** The descriptor `+0x04` codes used are `1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xd, 0xe`. Code `0xc` is never used.
  - Code `0xd` is an enumeration that is named in the file. Its helper `0x00e307c0` was read in full, along with all **10** of its name tables (§2).
- **Surprises:**
  1. **The thunks are shared and fold across types.** The same thunk address is a *setter* in one table and a *getter* in another (for example `0x00e2e460` is the `element.render_mode` getter and the `tween.target_handle` setter). A thunk is only "call vtable slot N", so its getter or setter role belongs to the row, not to the address.
  2. **`gradient`'s parent is `bitmap`, not a third base class.** The "three global storage cells" of `spec-lua-bindings.md` §9.1 are really the parent-type id: "none", `element`'s id, or `bitmap`'s id.
  3. **The `0x100` flag does not mean "writable".** Lua writes the flag-0 property `end_event` (`vint_lib.lua:1172`). The flag's only reader found is the text/XML document path, which skips flag-0 rows. Together with the fact that no flag-0 row appears in any shipped binary file, this makes the flag a "serialised/loadable from a document" bit (§3.3).
  4. **Subtypes re-list some inherited rows** with identical contents (`group`, `text`, `point`, `bitmap_circle`).
  5. **One enumeration value is duplicated:** `tween.state` has both `idle` and `waiting` = 0.
- **The vtable-slot orientation question is closed.** Descriptor `+0x08` is the setter and points at the *higher* slot of each pair; `+0x0c` is the getter and points at the *lower* slot. This was CONFIRMED by reading both slot bodies for `visible` and `alpha` in the `group` vtable (§3.2). It settles the open conflict between `spec-lua-bindings.md` §9.3 and §12.7 Group 7 in favour of §9.3's slot numbers.

---

## 1. Shared mechanism — CONFIRMED

### 1.1 The registration call (`0x00e290c0`)

Every one of the 13 registration functions makes the same first call. It loads ECX with a fixed 12-byte **type-registration record** and pushes four arguments, in this order (last pushed first):
1. the parent type id;
2. the type-name string;
3. the descriptor-table pointer;
4. the row count.

`0x00e290c0` (a `this`-call that pops 16 bytes) first clears the record to {0, 0, −1}. If the name, the table and the count are all non-zero, it then:
- stores the table pointer at `+0`, the row count at `+4` and the parent id at `+8`;
- calls `0x00e28bd0`.

**`0x00e28bd0`** walks the record's rows at a **32-byte stride**. For each row it hashes the string at row `+0x00` with `0x00d9e740(name, 0)` and writes the result into row `+0x18`.

**`0x00e30560`** re-runs `0x00e28bd0` for every registry slot that has a registration record. The registration driver `0x00e23910` calls it at `0x00e23994`, two calls before the first of the 13 registrations.

**`0x00d9e740` (CONFIRMED).** For each byte of the string:
1. the C-runtime `tolower` is applied;
2. the result is XORed with the low byte of the running value;
3. that indexes the table at `0x01320da0`;
4. the running value is shifted right by 8 and XORed with the table entry.

The running value starts at the second argument, and there is no final XOR. With seed 0 this is the "lower-cased CRC-32 with no final inversion" that is already in use. My script reproduces it with the standard reflected polynomial `0xEDB88320`, and the 99.76 % match rate in §3.4 confirms both the table and the variant.

### 1.2 The type registry (`0x00e30590`, `0x00e305e0`)

Each registration function's second call is `0x00e30590(type name, parent id, registration record)`. It:
- appends a 16-byte slot to the registry at **`0x02a8a538`**: `+0` name, `+4` parent id, `+8` registration record;
- has a maximum of 32 slots, with the count at **`0x02a8a738`**;
- **returns the slot index, which is the type id**, or −1 when the registry is full.

The registration function stores that id in its own **type-id cell** (table below).

Each function except `element`'s then constructs a per-type object pool, giving it one pushed constant, and attaches the pool to registry slot `+0xc` through `0x00e305e0(type id, pool)`.

`0x00e30590` and `0x00e290c0` each have **exactly 13 callers** in `.text` (raw E8 scan), one per registration function. So the registry holds exactly these 13 types.

The driver `0x00e23910` calls the registration functions in this order (CONFIRMED by disassembly of `0x00e2399e`–`0x00e239da`):

`element`, `tween`, `animation`, `bitmap`, `gradient`, `text`, `group`, `document`, `point`, `clip`, `bitmap_circle`, `sr2_map`, `video`.

**HIGH:** the type ids are therefore 0–12 in that order. This assumes the registry count is zero at that point, and the call just before (`0x00e28c10`) was not read.

`bitmap` is registered before `gradient`, as it must be, because `gradient` reads `bitmap`'s id as its parent.

**Parent ids (CONFIRMED).**
- `element`, `tween` and `animation` push the constant at **`0x012570f0`**, which is `0xffffffff` in the static image: no parent.
- The nine element-family types push the contents of **`0x0132bdcc`**, reached through the pointer cell **`0x0132bdd0`**. `element`'s registration writes its own type id into `0x0132bdcc`.
- **`gradient` pushes the contents of `0x0132c864`** (through the pointer cell `0x0132c868`), which is **`bitmap`'s type id**.

This corrects the reading in `spec-lua-bindings.md` §9.1: these are not three opaque "storage cells" but the parent-type id. `gradient` is a subtype of `bitmap`, which is a subtype of `element`.

**Who consumes the parent link:**
- the descriptor lookup `0x00e28980` (per `spec-vint-doc-format.md` §5 it moves up to the parent type on a miss);
- `0x00e30770` (§2), which walks registry `+4` until it reaches −1.

**Per-type bookkeeping (CONFIRMED from each registration function's body):**

| Type | Reg. fn | Reg. record (ECX) | Type-id cell (ptr cell) | Parent pushed | Pool object, pushed constant |
|---|---|---|---|---|---|
| `element` | `0x00e26890` | `0x02a74bcc` | `0x0132bdcc` (`0x0132bdd0`) | none (`0x012570f0` = −1) | — (no pool) |
| `tween` | `0x00e2eef0` | `0x02a88000` | `0x0132c0cc` (`0x0132c0d0`) | none | `0x0132c480`, `0x485` |
| `animation` | `0x00e2f910` | `0x02a88034` | `0x0132c544` | none | `0x0132c5b0`, `0x201` |
| `group` | `0x00e30b10` | `0x02a8a740` | `0x0132c66c` | `element` | `0x0132c6f8`, `0x399` |
| `clip` | `0x00e31080` | `0x02a8a74c` | `0x0132c79c` | `element` | `0x0132c7e8`, `0x20` |
| `bitmap` | `0x00e317f0` | `0x02a8a758` | `0x0132c864` (`0x0132c868`) | `element` | `0x0132c910`, `0x5dc` |
| `text` | `0x00e332d0` | `0x02a8a76c` | `0x0132c994` (`0x0132c998`) | `element` | `0x0132cc70`, `0x1f4` |
| `point` | `0x00e33530` | `0x02a8a780` | `0x0132ccec` | `element` | `0x0132cd14`, `0` |
| `video` | `0x00e34190` | `0x02a8a7ac` | `0x0132cd9c` | `element` | `0x0132cec8`, `2` |
| `sr2_map` | `0x00e34640` | `0x02a8a7c0` | `0x0132cf4c` (`0x0132cf50`) | `element` | `0x0132cfa8`, `3` |
| `bitmap_circle` | `0x00e34c60` | `0x02a8a7cc` | `0x0132d02c` | `element` | `0x0132d118`, `0x64` |
| `document` | `0x00e35190` | `0x02a8a7d8` | `0x0132d1ac` | `element` | `0x0132d1d4`, `0x190` |
| `gradient` | `0x00e35ab0` | `0x02a8a7e4` | `0x0132d25c` | **`bitmap`** | `0x0132d368`, `5` |

The pushed pool constant is most likely the pool's capacity (HYPOTHESIS: the pool constructors `0x00e2ecd0`, `0x00e2f810`, `0x00e30a00`, `0x00e30f70`, `0x00e316e0`, `0x00e33130`, `0x00e33420`, `0x00e34080`, `0x00e34530`, `0x00e34b50`, `0x00e34e80` and `0x00e359a0` were not read). If it is, `point` gets a zero-capacity pool. That fits the population: no `point` element occurs in the 159 documents.

**Cross-references for U2/U5 (CONFIRMED identity of the cells; the uses themselves were not re-read):**
- `0x0132bdd0`, the cell that the scoping note's tree walk (§1.2 step 8) uses for its "element-base" child test, is the pointer to **`element`'s type id**.
- `0x0132c998`, the class cell in the walk's mask path (§1.2 step 6), is the pointer to **`text`'s type id**.
- `0x0132c0d0`, the cell behind the `start_value`/`end_value` redirect in `spec-lua-bindings.md` §20.1, is the pointer to **`tween`'s type id**. This upgrades that section's "tween" characterization from a naming inference to a structural one, provided the comparison there is against the dereferenced id, which is HIGH and was not re-read here.

### 1.3 The 32-byte descriptor row — CONFIRMED for all 96 rows

| Offset | Content |
|---|---|
| `+0x00` | pointer to the property-name string |
| `+0x04` | value type code (vocabulary in §3.1) |
| `+0x08` | **setter** thunk (orientation CONFIRMED in §3.2) |
| `+0x0c` | **getter** thunk |
| `+0x10`, `+0x14` | 0 in all 96 rows |
| `+0x18` | 0 in the static image; the CRC-32 of the name is written here at registration (§1.1) |
| `+0x1c` | `0x100` (77 rows) or `0` (19 rows); meaning in §3.3 |

**Every one of the 69 distinct thunk addresses has the identical 11-instruction shape (CONFIRMED by disassembling all 69):**
1. Load the object pointer (first stack argument).
2. If it is null, return false.
3. Otherwise load the object's vtable, fetch one fixed slot, and call it as a `this`-call with the remaining two stack arguments (the value record and a flag), passed through unchanged.
4. Return its result.

The only difference between thunks is the slot offset, and the linker has merged identical bodies. So the thunk address identifies a **vtable byte offset**, not a property and not a direction. The tables below give both the thunk and the slot it reaches.

### 1.4 The `0xd` enumeration branch — `0x00e307c0` read in full, CONFIRMED

This closes `spec-vint-doc-format.md` §8 item 11 at the helper level.

**Registration of an enumeration binding.** Ten registration-time calls, each `0x00e306e0(owner type id, property name)` with ECX = a holder object (exactly 10 callers, raw E8 scan), do the following:
- skip if the type id is −1, the name is null, or the holder's array pointer is still null;
- otherwise append a 16-byte entry to the binding table at **`0x02a8a338`** through `0x00e30680`:
  - `+0` owner type id, `+4` property-name pointer, `+0xc` holder object;
  - `+8` is never written;
  - maximum 32 entries, count at **`0x02a8a73c`**.

**The holder objects.** Each holder is 8 bytes: `+0` a pointer to an array of 8-byte {name pointer, signed 32-bit value} pairs, and `+4` a u16 entry count.
- They are filled at C-runtime start-up by ten tiny static initializers in `0x01015400–0x01015711`. Each one pushes a count and an array address and calls `0x00e306c0`, which stores both if the pointer is non-null and the count is non-zero.
- Those are the holders' only writers (xref).
- Because the C runtime runs them before the UI bring-up, the arrays are always in place by the time `0x00e306e0` runs.

**Lookup: `0x00e307c0(object type id, property name, string)`.**
1. If the property name or the string is null, return `0x7fffffff`.
2. Otherwise scan the binding entries. An entry matches when:
   - `0x00e30770(entry owner type, object type)` is true, and
   - the entry's property name equals the requested one, **case-insensitively**.
3. On the first match, call `0x00e30710` on that entry's holder.

**`0x00e30770(a, b)` — related types.** It returns true when either type is an ancestor-or-self of the other:
- first it walks *a*'s parent chain through registry `+4` looking for *b*;
- then it walks *b*'s chain looking for *a*.

So the match is two-directional. A `bitmap` finds `element`'s `auto_offset` binding. An `element`-typed object would also find `text`'s `horz_align` binding; that is harmless, since the descriptor lookup must already have matched the row.

**`0x00e30710(string)` — the name search.** A linear search through the holder's array, comparing names **case-insensitively** (C-runtime `_stricmp`). It returns the paired value, or `0x7fffffff` on a miss or a null string.

**The loader call site (CONFIRMED, `0x00e29cbf`–`0x00e29cf5` inside `0x00e29860`).** When the matched descriptor's `+0x04` is `0xd` *and* the decoded file value has tag `4` (a string):
1. It calls `0x00e307c0` with:
   - the object's type id (from the object's own vtable `+0x1c`);
   - the descriptor's `+0x00` name;
   - the string.
2. It stores the result as a signed integer value through `0x00e242d0`.
3. It then tests for `0x7fffffff`; that failure path starts at `0x00e29d2c`.

What the failure path does was not re-read here. The sibling note says it falls back to the original string.

---

## 2. The ten enumeration tables (code `0xd`) — CONFIRMED, every entry

Every code-`0xd` row in the 13 tables has exactly one binding. There are 10 rows and 10 bindings.

| Type.property | Array | Entries | Holder | Names = values |
|---|---|---|---|---|
| `element.auto_offset` | `0x0132bdd8` | 10 | `0x02a74bd8` | `none`=0, `c`=1, `w`=2, `n`=3, `e`=4, `s`=5, `nw`=6, `ne`=7, `se`=8, `sw`=9 |
| `element.render_mode` | `0x0132be28` | 6 | `0x02a74bc4` | `default`=0, `filter`=1, `additive`=2, `additive_alpha`=3, `subtractive`=4, `multiply`=5 |
| `tween.loop_mode` | `0x0132c0d4` | 3 | `0x02a87ff8` | `once`=0, `cycle`=1, `bounce`=2 |
| `tween.algorithm` | `0x0132c0f0` | 25 | `0x02a8800c` | `linear`=0, `ease_in_exp`=1, `ease_out_exp`=2, `ease_in_out_exp`=3, `ease_in_quad`=4, `ease_out_quad`=5, `ease_in_out_quad`=6, `ease_in_cubic`=7, `ease_out_cubic`=8, `ease_in_out_cubic`=9, `ease_in_quart`=10, `ease_out_quart`=11, `ease_in_out_quart`=12, `ease_in_quint`=13, `ease_out_quint`=14, `ease_in_out_quint`=15, `ease_in_sin`=16, `ease_out_sin`=17, `ease_in_out_sin`=18, `ease_in_circ`=19, `ease_out_circ`=20, `ease_in_out_circ`=21, `ease_in_bounce`=22, `ease_out_bounce`=23, `ease_in_out_bounce`=24 |
| `tween.state` | `0x0132c1b8` | 6 | `0x02a88024` | `idle`=0, `running`=1, `paused`=2, `disabled`=3, **`waiting`=0**, `finished`=4 |
| `tween.start_value_type` | `0x0132c1e8` | 3 | `0x02a8801c` | `absolute`=0, `stage`=1, `dynamic`=2 |
| `tween.end_value_type` | `0x0132c200` | 3 | `0x02a88014` | `absolute`=0, `stage`=1, `dynamic`=2 (a separate array with the same contents) |
| `text.horz_align` | `0x0132c99c` | 3 | `0x02a8a778` | `left`=0, `center`=1, `right`=2 |
| `text.force_case` | `0x0132c9b4` | 3 | `0x02a8a764` | `none`=0, `upper`=1, `lower`=2 |
| `sr2_map.map_mode` | `0x0132cf54` | 2 | `0x02a8a7b8` | `pause_map`=0, `minimap`=1 |

**Notes:**
- **`tween.state` maps two names to 0.** Because the lookup is first-match by *name*, both `idle` and `waiting` load as 0. Any reverse mapping from integer to name is ambiguous. `state` is a flag-0 row and never appears in a shipped file (§3.4), so this only matters for Lua.
- **Values seen in real files, all of which resolve.** Code-`0xd` properties occur only as tag-`4` strings in the 159 documents:

  | Property | Values (count) |
  |---|---|
  | `auto_offset` | all 9 non-`none` names |
  | `render_mode` | `additive_alpha` (21), `additive` (3), `multiply` (3), `subtractive` (2) |
  | `algorithm` | 17 of the 25 names |
  | `loop_mode` | `cycle` (70), `bounce` (86) |
  | `start_value_type` | `stage` (1,524), `dynamic` (3) |
  | `end_value_type` | `stage` (1,527) |
  | `horz_align` | `center` (29), `right` (18) |
  | `force_case` | `upper` (180), `lower` (1) |

  No file value misses its table.

---

## 3. Cross-type observations

### 3.1 Type-code vocabulary across all 96 rows

| `+0x04` code | Rows | Meaning (evidence) | Types using it | On-disk tag seen in shipped files |
|---|---|---|---|---|
| `1` | 4 | signed integer (on-disk tag 1, `spec-vint-doc-format.md` §5) | element, text | 1 |
| `2` | 8 | unsigned integer / handle / CRC | animation, bitmap, bitmap_circle, text, tween, video | 2; and **tag 1** once, for `video.frame_event_num` |
| `3` | 15 | float | 7 types | 3 |
| `4` | 6 | plain string (names, tags, `video` event names) | text, tween, video | 4 |
| `5` | 13 | bool | 6 types | 5 |
| `6` | 6 | vec3 / colour (`tint`, `shadow_tint`, the four `gradient_*` corners) | element, text, gradient | 6 |
| `7` | 21 | vec2 (positions, sizes, scales, source rectangles) | 7 types | 7 |
| `8` | 3 | Lua callback name: `tween.start_event`/`end_event`/`per_frame_event` (matches the `vint_set_property` case-8 callback claim in `spec-lua-bindings.md` §20.1) | tween | never in files |
| `9` | 1 | `document.document_name` | document | 4 |
| `0xa` | 5 | bitmap/image name: `bitmap.image`, `bitmap_circle.image`, `text.line_frame_w`/`_m`/`_e` | bitmap, bitmap_circle, text | 4 |
| `0xb` | 1 | font name: `text.font` | text | 4 |
| `0xd` | 10 | enumeration named in the file (§1.4, §2) | element, tween, text, sr2_map | 4 |
| `0xe` | 3 | variant: `tween.start_value`/`end_value`, `text.insert_values` | tween, text | `start_value`/`end_value`: 3, 6 and 7 |

**Notes on the vocabulary:**
- The by-number fit with the generic value switch in `spec-vint-doc-format.md` §5 is close but not exact. That paragraph puts codes `8`, `0xb` and `0xc` in the "resolve a string to a named resource" group. Here `8` is a callback, `0xb` a font and `0xa` an image name, and **`0xc` is used by no row in any table**. HIGH: `0xa`/`0xb` are the image and font resource lookups, from the property names; the resolving code was not read.
- **Code `0xe` is a real variant.** `tween.start_value`/`end_value` are stored with tag `3` (761/760), tag `7` (807/821) or tag `6` (36/36), depending on the target property. This matches the `start_value_type`/`end_value_type` companions and the tween write-through in `spec-lua-bindings.md` §20.1. The tween's own `start_value_type`/`end_value_type` enumeration (`absolute`/`stage`/`dynamic`) is about *where* the value comes from, not its data type.
- **The tags written to disk for codes 8–0xe are always tag `4`** (string). The binary format has no tags beyond 7 (§5 of the format spec). Every resource-, enum- or name-typed property therefore travels as a string-pool index and is resolved at load time.

### 3.2 Vtable-slot layout and getter/setter orientation

**Orientation — CONFIRMED by reading the two slot bodies.** `group`'s vtable (`0x012588a4`) for `visible`:
- **`+0x38` → `0x00e276d0`.** It reads the object byte at `+0x9e` and writes it into the value record at `+8`, after checking that the record's type field is 0 or 5 (bool). It is a **getter**, and it is reached from descriptor field `+0x0c`.
- **`+0x3c` → `0x00e27700`.** It converts the incoming value through `0x007cb5b0` and stores the result into object `+0x9e`. It is a **setter**, reached from descriptor `+0x08`.

The same holds for `alpha`:
- `+0x60` → `0x00e2e770` reads the float at object `+0x88` into the record (type check 0 or 3);
- `+0x64` → `0x00e27960` stores into `+0x88`.

**So in every pair, the lower slot is the getter and the higher slot is the setter.** That agrees with the loader evidence that `+0x08` is the setter (`spec-vint-doc-format.md` §5), and it settles `spec-lua-bindings.md` §9.3's OPEN item: `render_mode` set/get = `+0x34`/`+0x30`, as §9.3 lists it, and §12.7 Group 7's opposite reading is wrong.

**Bonus, HIGH:** `visible` is stored at element byte `+0x9e` and `alpha` at element float `+0x88`. This was read in the `group` class only, through the shared element-base accessors.

**Slot ranges:**
- **Element family.** The 17 `element` properties occupy getter/setter pairs `+0x30` … `+0xb4`, which is 34 slots, in table order except that `offset` (`+0x50`/`+0x54`) and `anchor` (`+0x48`/`+0x4c`) are swapped. The next three slots, `+0xb8`/`+0xbc`/`+0xc0`, are the pre-draw/draw/post-draw slots of the scoping note §1.4, which fits exactly.
- **Type-specific properties start at `+0xc8`** in every element subtype:
  - `clip` `+0xc8`…`+0xd4`;
  - `bitmap` `+0xc8`…`+0xec`;
  - `gradient` continues after `bitmap` at `+0xf0`…`+0x12c`, consistent with deriving from `bitmap`;
  - `text` `+0xc8`…`+0x15c`, with `insert_values` last at `+0x158`/`+0x15c` although it is table row 10;
  - `video`, `sr2_map` and `bitmap_circle` also start at `+0xc8`.

  Slot `+0xc4` is not reached by any descriptor (OPEN, not examined).
- **Tween/animation** (the `vint_object_base` family) start one pair lower, at `+0x2c`/`+0x30`. Their rows continue to `+0xa8` (tween) and `+0x40` (animation), in exact table order.

### 3.3 The `+0x1c` flag — not "writable"; HIGH: "serialised"

**The 19 flag-0 rows are:**
- `element` (and its re-listings): `screen_size`, `screen_nw`, `screen_se`, `unscaled_size`;
- `tween`: `target_handle`, `state`, `start_event`, `end_event`, `per_frame_event`;
- `animation`: `target_handle`;
- `bitmap.image_crc`, `text.text_tag_crc`, `text.insert_values`.

**Against `spec-lua-bindings.md` §9.2's HYPOTHESIS ("writable versus read-only/computed"):**
- **Lua writes flag-0 properties.** `vint_lib.lua:1172` sets `end_event`, `lua_play_tween` sets `target_handle`, and §20.1 of that spec documents the `vint_set_property` path for these without any flag gate.
- **The one reader of the flag that a pattern sweep found** (capstone with data-skipping over `0x00e1e000–0x00e38000`, every byte-`+0x1d` test and every `+0x1c` load followed by a `0x100` test) is **`0x00e1f824`**, in the text/XML document path.
  - That code gathers the type's descriptor rows into a local array.
  - For each row whose flag byte `+0x1d` is non-zero, it looks for `property` child nodes with matching `name`, `type` and `value`, and applies them through `0x00e29360`.
  - **Flag-0 rows are skipped there.**
- **No flag-0 row occurs in any of the 159 shipped binary documents** (§3.4). By contrast, 57 of the 77 flag-`0x100` rows occur.

**HIGH:** `0x100` marks a property that is **persisted in or loaded from a document**. Flag-0 rows are runtime-only: computed geometry, runtime handles, Lua callbacks, derived CRCs.

The binary loader `0x00e29860` showed no flag test in the sweep, so it relies on the files simply not containing such rows. This is HIGH and not CONFIRMED, because the sweep was pattern-based and `0x00e29860` was not re-read in full here.

### 3.4 Population check against the 159 shipped documents

**Setup.** This is my own run of `u1_hist.py`, using the loader-faithful grammar from `vintdoc_loader_walk.py`. Both override and baseline lists were counted. Each record's hash was looked up in its element's own table, then the parent's, and so on, as `0x00e28980` does.

**Results:**
- **37,008 records; 36,919 matched (99.76 %).** Every match's on-disk tag agrees with its row's code as tabled in §3.1, with one exception (`video.frame_event_num`, §3.5).
- **Element types seen:**

  | Type | Elements |
  |---|---|
  | `bitmap` | 1,820 |
  | `tween` | 1,527 |
  | `group` | 786 |
  | `text` | 512 |
  | `animation` | 438 |
  | `document` | 195 |
  | `bitmap_circle` | 26 |
  | `clip` | 19 |
  | `sr2_map` | 2 |
  | `video` | 1 |
  | `point` | 0 |
  | `gradient` | 0 |
  | `element` (as a concrete type) | 0 |

- **The 89 misses are all one hash, `0xfc6ad9f8`, always tag `5` (bool).** It occurs on `group` (68), `bitmap` (12), `document` (5), `text` (3) and `clip` (1). It is in no table.
  - Brute force found no match among roughly 1.05 million printable strings (every suffix) in the executable, nor among about 100 guessed authoring-tool names.
  - Per `spec-vint-doc-format.md` §5, the loader consumes such a record and skips it.
  - Most likely an authoring-tool-only flag (HYPOTHESIS). See OPEN.
- **Rows never seen in any file:** all 19 flag-0 rows, plus these flag-`0x100` rows:
  - `animation.start_time`;
  - `text.line_frame_enable`, `text.background`;
  - `video`: `vid_id_handle`, `is_paused`, `is_looped`, `frame`, `end_event`, `frame_event`;
  - `sr2_map`: `zoom`, `map_mode`;
  - `bitmap_circle.source_nw`;
  - all 8 `gradient` rows.

  There are no `gradient` elements in this sample at all. `spec-vint-doc-format.md` §8 item 1 notes that `interface.vpp_pc` was not swept.
- **The `bitmap` image reference closes `spec-vint-doc-format.md` §8 item 7 at the naming level.** The image is carried by **`image`** (code `0xa`, tag `4` string), present on 1,822 `bitmap` records. The sub-rectangle is `source_se` (1,820) and sometimes `source_nw`, with `custom_source_coords` as the switch. `bitmap_circle` uses the same `image`/`source_*` names. The hookup from that string to a PEG entry or a BitmapSheets entry is still OPEN.
- **`text`'s three `line_frame_w`/`_m`/`_e` image names are present on all 512 text records.** So is `font` (code `0xb`). `text_tag` is present 513 times across the 512 text elements. Override lists are counted too, so one element probably carries it in both lists; this was not checked per element.

### 3.5 Re-listed inherited rows and other inconsistencies

- **Re-listed rows.** Some subtypes repeat rows that `element` already has, with **identical** code, thunks and flag; only their position in the table differs:
  - `group`: `screen_size`, `screen_se`, `screen_nw`, `offset`;
  - `text`: `screen_size`, `background`;
  - `point`: `screen_size`;
  - `bitmap_circle`: `screen_size`.

  Because the lookup searches the own table first, these change nothing about dispatch: the real override lives in the vtable slot.
  - HIGH: they mark properties whose behaviour the subtype overrides. `group` has its own layout chain, per the scoping note §1.3. `text`, `point` and `bitmap_circle` compute their own `screen_size`.
  - `point`'s single row being `screen_size` fits this.
- **Same name, different code across types:**
  - `end_event` is code `8` (callback) on `tween` but code `4` (plain string) on `video`.
  - `is_paused` is `5` on both `animation` and `video`.
  - `target_handle` is `2` on both `tween` and `animation`.
  - `start_time` is `3` on both `tween` and `animation`.
  - `image`, `source_nw` and `source_se` have the same codes on `bitmap` and `bitmap_circle`.
- **`video.frame_event_num`** is code `2` (unsigned) but its one file occurrence is tag `1` (signed). The loader applies both through the same setter (`spec-vint-doc-format.md` §5), so this is harmless. It is noted as the only code/tag mismatch in the 36,919 matches.

---

## 4. Per-type sections

Each section gives the registration function's body in my own words, then every row of the table.

**What every registration function does.** Each one is a straight-line leaf of 20–60 instructions. Unless noted otherwise, it does exactly the following:
1. `0x00e290c0` with ECX = the registration record and pushes (parent id, name, table, count) (§1.1);
2. `0x00e30590(name, parent id, record)`, storing the returned type id in the type-id cell;
3. constructs the pool object with one pushed constant;
4. `0x00e305e0(type id, pool)`;
5. returns 1 (true).

Every row's `+0x10`, `+0x14` and `+0x18` are 0 in the image, and are omitted from the tables.

The CRC column is the value `0x00e28bd0` writes into `+0x18`. It was computed with the confirmed hash and validated by the §3.4 match.

The "Records" column counts the property records in the 159 documents that resolve to *that row* under the own-table-then-parent lookup (§3.4). For example, `element.offset` counts the `offset` records of every subtype that does not re-list `offset`. `group.offset` counts only `group`'s own.

### 4.1 `element` — `0x00e26890`, table `0x0132be58`, 17 rows

**Registration body.** Parent: none. There is no pool, because `element` is abstract. After the registry call, it makes **two enumeration bindings** through `0x00e306e0` (owner = `element`'s id): `auto_offset` (holder `0x02a74bd8`) and `render_mode` (holder `0x02a74bc4`). This table and its 17 names agree with `spec-lua-bindings.md` §9.2.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132be58` | `render_mode` | `0xc229df54` | `0xd` | `0x00e275f0` → `+0x34` | `0x00e2e460` → `+0x30` | `0x100` | 29 |
| 1 | `0x0132be78` | `visible` | `0xe7dc3727` | `0x5` | `0x00e2f7f0` → `+0x3c` | `0x00e2e4d0` → `+0x38` | `0x100` | 75 |
| 2 | `0x0132be98` | `mask` | `0x5e2b1c2c` | `0x5` | `0x00e2e5c0` → `+0x44` | `0x00e27720` → `+0x40` | `0x100` | 54 |
| 3 | `0x0132beb8` | `offset` | `0xe8c86b73` | `0x7` | `0x00e2e690` → `+0x54` | `0x00e2e7b0` → `+0x50` | `0x100` | 1,025 |
| 4 | `0x0132bed8` | `anchor` | `0xd693b0de` | `0x7` | `0x00e2e750` → `+0x4c` | `0x00e2e600` → `+0x48` | `0x100` | 2,869 |
| 5 | `0x0132bef8` | `tint` | `0x0f4d3468` | `0x6` | `0x00e278c0` → `+0x5c` | `0x00e27870` → `+0x58` | `0x100` | 3,353 |
| 6 | `0x0132bf18` | `alpha` | `0x16c2ce77` | `0x3` | `0x00e2e860` → `+0x64` | `0x00e27940` → `+0x60` | `0x100` | 380 |
| 7 | `0x0132bf38` | `depth` | `0x3c81eb74` | `0x1` | `0x00e279a0` → `+0x6c` | `0x00e2e8b0` → `+0x68` | `0x100` | 1,073 |
| 8 | `0x0132bf58` | `mouse_depth` | `0x3b90a63f` | `0x1` | `0x00e27a80` → `+0x74` | `0x00e27a30` → `+0x70` | `0x100` | 3,068 |
| 9 | `0x0132bf78` | `screen_size` | `0x9cf96009` | `0x7` | `0x00e309c0` → `+0x7c` | `0x00e322a0` → `+0x78` | `0x0` | 0 |
| 10 | `0x0132bf98` | `screen_nw` | `0x6c50b495` | `0x7` | `0x00e27b70` → `+0x84` | `0x00e27b10` → `+0x80` | `0x0` | 0 |
| 11 | `0x0132bfb8` | `screen_se` | `0x6085a9c1` | `0x7` | `0x00e27bd0` → `+0x8c` | `0x00e2ea70` → `+0x88` | `0x0` | 0 |
| 12 | `0x0132bfd8` | `rotation` | `0x4c5e4798` | `0x3` | `0x00e2eb70` → `+0x94` | `0x00e2ead0` → `+0x90` | `0x100` | 248 |
| 13 | `0x0132bff8` | `scale` | `0x2a64d299` | `0x7` | `0x00e27ec0` → `+0x9c` | `0x00e2eb30` → `+0x98` | `0x100` | 1,313 |
| 14 | `0x0132c018` | `auto_offset` | `0x18ebd5c7` | `0xd` | `0x00e27f60` → `+0xa4` | `0x00e27f20` → `+0xa0` | `0x100` | 1,054 |
| 15 | `0x0132c038` | `unscaled_size` | `0xd1d85671` | `0x7` | `0x00e28040` → `+0xac` | `0x00e27fe0` → `+0xa8` | `0x0` | 0 |
| 16 | `0x0132c058` | `background` | `0x5fe2dc26` | `0x5` | `0x00e28090` → `+0xb4` | `0x00e33030` → `+0xb0` | `0x100` | 25 |

### 4.2 `tween` — `0x00e2eef0`, table `0x0132c280`, 16 rows

**Registration body.** Parent: none. Pool `0x0132c480` (`0x485`). Five enumeration bindings with owner = `tween`'s id:

| Property | Holder |
|---|---|
| `state` | `0x02a88024` |
| `loop_mode` | `0x02a87ff8` |
| `algorithm` | `0x02a8800c` |
| `start_value_type` | `0x02a8801c` |
| `end_value_type` | `0x02a88014` |

`tween`'s accessor slots begin at `+0x2c`.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132c280` | `target_handle` | `0x97fe81eb` | `0x2` | `0x00e2e460` → `+0x30` | `0x00e2f580` → `+0x2c` | `0x0` | 0 |
| 1 | `0x0132c2a0` | `target_name` | `0xf8bcd95b` | `0x4` | `0x00e2e4d0` → `+0x38` | `0x00e275f0` → `+0x34` | `0x100` | 1,527 |
| 2 | `0x0132c2c0` | `target_property` | `0x868acf50` | `0x4` | `0x00e27720` → `+0x40` | `0x00e2f7f0` → `+0x3c` | `0x100` | 1,527 |
| 3 | `0x0132c2e0` | `state` | `0x65b125e6` | `0xd` | `0x00e2e600` → `+0x48` | `0x00e2e5c0` → `+0x44` | `0x0` | 0 |
| 4 | `0x0132c300` | `start_time` | `0xb3a79df1` | `0x3` | `0x00e2e7b0` → `+0x50` | `0x00e2e750` → `+0x4c` | `0x100` | 718 |
| 5 | `0x0132c320` | `duration` | `0xe37d5fa9` | `0x3` | `0x00e27870` → `+0x58` | `0x00e2e690` → `+0x54` | `0x100` | 1,527 |
| 6 | `0x0132c340` | `start_value` | `0x03a5f329` | `0xe` | `0x00e27940` → `+0x60` | `0x00e278c0` → `+0x5c` | `0x100` | 1,604 |
| 7 | `0x0132c360` | `end_value` | `0x8ae10b9f` | `0xe` | `0x00e2e8b0` → `+0x68` | `0x00e2e860` → `+0x64` | `0x100` | 1,617 |
| 8 | `0x0132c380` | `loop_mode` | `0xc98d31c7` | `0xd` | `0x00e27a30` → `+0x70` | `0x00e279a0` → `+0x6c` | `0x100` | 156 |
| 9 | `0x0132c3a0` | `algorithm` | `0x730cd817` | `0xd` | `0x00e322a0` → `+0x78` | `0x00e27a80` → `+0x74` | `0x100` | 325 |
| 10 | `0x0132c3c0` | `max_loops` | `0x090d4acc` | `0x3` | `0x00e27b10` → `+0x80` | `0x00e309c0` → `+0x7c` | `0x100` | 1,527 |
| 11 | `0x0132c3e0` | `start_event` | `0x257ca1ba` | `0x8` | `0x00e2ea70` → `+0x88` | `0x00e27b70` → `+0x84` | `0x0` | 0 |
| 12 | `0x0132c400` | `end_event` | `0xac38590c` | `0x8` | `0x00e2ead0` → `+0x90` | `0x00e27bd0` → `+0x8c` | `0x0` | 0 |
| 13 | `0x0132c420` | `per_frame_event` | `0x09943ad3` | `0x8` | `0x00e2eb30` → `+0x98` | `0x00e2eb70` → `+0x94` | `0x0` | 0 |
| 14 | `0x0132c440` | `start_value_type` | `0xbb45e85b` | `0xd` | `0x00e27f20` → `+0xa0` | `0x00e27ec0` → `+0x9c` | `0x100` | 1,527 |
| 15 | `0x0132c460` | `end_value_type` | `0xfac22db9` | `0xd` | `0x00e27fe0` → `+0xa8` | `0x00e27f60` → `+0xa4` | `0x100` | 1,527 |

### 4.3 `animation` — `0x00e2f910`, table `0x0132c550`, 3 rows

**Registration body.** Parent: none. Pool `0x0132c5b0` (`0x201`). No enumeration bindings.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132c550` | `start_time` | `0xb3a79df1` | `0x3` | `0x00e2e460` → `+0x30` | `0x00e2f580` → `+0x2c` | `0x100` | 0 |
| 1 | `0x0132c570` | `is_paused` | `0xf0f1d7ea` | `0x5` | `0x00e2e4d0` → `+0x38` | `0x00e275f0` → `+0x34` | `0x100` | 2 |
| 2 | `0x0132c590` | `target_handle` | `0x97fe81eb` | `0x2` | `0x00e27720` → `+0x40` | `0x00e2f7f0` → `+0x3c` | `0x0` | 0 |

### 4.4 `group` — `0x00e30b10`, table `0x0132c678`, 4 rows

**Registration body.** Parent: `element`. Pool `0x0132c6f8` (`0x399`). All four rows re-list `element` rows (§3.5).

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132c678` | `screen_size` | `0x9cf96009` | `0x7` | `0x00e309c0` → `+0x7c` | `0x00e322a0` → `+0x78` | `0x0` | 0 |
| 1 | `0x0132c698` | `screen_se` | `0x6085a9c1` | `0x7` | `0x00e27bd0` → `+0x8c` | `0x00e2ea70` → `+0x88` | `0x0` | 0 |
| 2 | `0x0132c6b8` | `screen_nw` | `0x6c50b495` | `0x7` | `0x00e27b70` → `+0x84` | `0x00e27b10` → `+0x80` | `0x0` | 0 |
| 3 | `0x0132c6d8` | `offset` | `0xe8c86b73` | `0x7` | `0x00e2e690` → `+0x54` | `0x00e2e7b0` → `+0x50` | `0x100` | 53 |

### 4.5 `clip` — `0x00e31080`, table `0x0132c7a8`, 2 rows

**Registration body.** Parent: `element`. Pool `0x0132c7e8` (`0x20`).

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132c7a8` | `clip_size` | `0x9b61af0e` | `0x7` | `0x00e31300` → `+0xcc` | `0x00e32100` → `+0xc8` | `0x100` | 19 |
| 1 | `0x0132c7c8` | `clip_enabled` | `0x99c0d3f8` | `0x5` | `0x00e30f30` → `+0xd4` | `0x00e31f10` → `+0xd0` | `0x100` | 1 |

### 4.6 `bitmap` — `0x00e317f0`, table `0x0132c870`, 5 rows

**Registration body.** Parent: `element`. Pool `0x0132c910` (`0x5dc`). Its type id (cell `0x0132c864`) is `gradient`'s parent.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132c870` | `custom_source_coords` | `0xeb591915` | `0x5` | `0x00e34ad0` → `+0xec` | `0x00e33df0` → `+0xe8` | `0x100` | 17 |
| 1 | `0x0132c890` | `image` | `0x031ff342` | `0xa` | `0x00e31300` → `+0xcc` | `0x00e32100` → `+0xc8` | `0x100` | 1,822 |
| 2 | `0x0132c8b0` | `image_crc` | `0xc2814300` | `0x2` | `0x00e30f30` → `+0xd4` | `0x00e31f10` → `+0xd0` | `0x0` | 0 |
| 3 | `0x0132c8d0` | `source_nw` | `0xb8cadfd6` | `0x7` | `0x00e32090` → `+0xdc` | `0x00e33bb0` → `+0xd8` | `0x100` | 2 |
| 4 | `0x0132c8f0` | `source_se` | `0xb41fc282` | `0x7` | `0x00e315a0` → `+0xe4` | `0x00e321f0` → `+0xe0` | `0x100` | 1,820 |

### 4.7 `text` — `0x00e332d0`, table `0x0132c9d0`, 21 rows

**Registration body.** Parent: `element`. Pool `0x0132cc70` (`0x1f4`). Two enumeration bindings with owner = `text`'s id (read through `0x0132c998`): `horz_align` (`0x02a8a778`) and `force_case` (`0x02a8a764`).

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132c9d0` | `font` | `0xf1d0d7ce` | `0xb` | `0x00e31300` → `+0xcc` | `0x00e32100` → `+0xc8` | `0x100` | 512 |
| 1 | `0x0132c9f0` | `text_tag` | `0x751144ee` | `0x4` | `0x00e30f30` → `+0xd4` | `0x00e31f10` → `+0xd0` | `0x100` | 513 |
| 2 | `0x0132ca10` | `text_tag_crc` | `0x22c8f4af` | `0x2` | `0x00e32090` → `+0xdc` | `0x00e33bb0` → `+0xd8` | `0x0` | 0 |
| 3 | `0x0132ca30` | `text_scale` | `0x5978f7af` | `0x7` | `0x00e315a0` → `+0xe4` | `0x00e321f0` → `+0xe0` | `0x100` | 345 |
| 4 | `0x0132ca50` | `word_wrap` | `0x42a6c79e` | `0x5` | `0x00e34ad0` → `+0xec` | `0x00e33df0` → `+0xe8` | `0x100` | 122 |
| 5 | `0x0132ca70` | `wrap_width` | `0x8d090dac` | `0x3` | `0x00e324a0` → `+0xf4` | `0x00e32440` → `+0xf0` | `0x100` | 512 |
| 6 | `0x0132ca90` | `horz_align` | `0x084dfe8d` | `0xd` | `0x00e35610` → `+0xfc` | `0x00e355b0` → `+0xf8` | `0x100` | 47 |
| 7 | `0x0132cab0` | `force_case` | `0xb03e7a3c` | `0xd` | `0x00e325e0` → `+0x104` | `0x00e33fe0` → `+0x100` | `0x100` | 181 |
| 8 | `0x0132cad0` | `leading` | `0x6b97c4ac` | `0x1` | `0x00e32bd0` → `+0x10c` | `0x00e32c90` → `+0x108` | `0x100` | 102 |
| 9 | `0x0132caf0` | `kerning` | `0xe6cdf714` | `0x1` | `0x00e32cd0` → `+0x114` | `0x00e357a0` → `+0x110` | `0x100` | 5 |
| 10 | `0x0132cb10` | `insert_values` | `0xb0bc9444` | `0xe` | `0x00e326f0` → `+0x15c` | `0x00e32690` → `+0x158` | `0x0` | 0 |
| 11 | `0x0132cb30` | `screen_size` | `0x9cf96009` | `0x7` | `0x00e309c0` → `+0x7c` | `0x00e322a0` → `+0x78` | `0x0` | 0 |
| 12 | `0x0132cb50` | `line_frame_enable` | `0x76d297e7` | `0x5` | `0x00e32890` → `+0x11c` | `0x00e35820` → `+0x118` | `0x100` | 0 |
| 13 | `0x0132cb70` | `line_frame_w` | `0x2dd1908f` | `0xa` | `0x00e358e0` → `+0x124` | `0x00e329b0` → `+0x120` | `0x100` | 512 |
| 14 | `0x0132cb90` | `line_frame_m` | `0xd0b369f5` | `0xa` | `0x00e329f0` → `+0x12c` | `0x00e35920` → `+0x128` | `0x100` | 512 |
| 15 | `0x0132cbb0` | `line_frame_e` | `0xde68e1c7` | `0xa` | `0x00e32ad0` → `+0x134` | `0x00e32b90` → `+0x130` | `0x100` | 512 |
| 16 | `0x0132cbd0` | `shadow_enabled` | `0x64b92e01` | `0x5` | `0x00e32db0` → `+0x13c` | `0x00e32df0` → `+0x138` | `0x100` | 184 |
| 17 | `0x0132cbf0` | `shadow_offset` | `0x782cc39f` | `0x7` | `0x00e32e40` → `+0x144` | `0x00e32ea0` → `+0x140` | `0x100` | 486 |
| 18 | `0x0132cc10` | `shadow_tint` | `0x649e26d7` | `0x6` | `0x00e32f00` → `+0x14c` | `0x00e32f70` → `+0x148` | `0x100` | 30 |
| 19 | `0x0132cc30` | `shadow_alpha` | `0x4d77b378` | `0x3` | `0x00e32f90` → `+0x154` | `0x00e32fd0` → `+0x150` | `0x100` | 144 |
| 20 | `0x0132cc50` | `background` | `0x5fe2dc26` | `0x5` | `0x00e28090` → `+0xb4` | `0x00e33030` → `+0xb0` | `0x100` | 0 |

### 4.8 `point` — `0x00e33530`, table `0x0132ccf4`, 1 row

**Registration body.** Parent: `element`. Pool `0x0132cd14` with pushed constant `0`. This agrees with `spec-lua-bindings.md` §9.2.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132ccf4` | `screen_size` | `0x9cf96009` | `0x7` | `0x00e309c0` → `+0x7c` | `0x00e322a0` → `+0x78` | `0x0` | 0 |

### 4.9 `video` — `0x00e34190`, table `0x0132cda8`, 9 rows

**Registration body.** Parent: `element`. Pool `0x0132cec8` (`2`). Recommended parked by the scoping note; the table is recorded here for completeness.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132cda8` | `vid_id_handle` | `0x56464c25` | `0x2` | `0x00e31300` → `+0xcc` | `0x00e32100` → `+0xc8` | `0x100` | 0 |
| 1 | `0x0132cdc8` | `vid_name` | `0x2250256d` | `0x4` | `0x00e30f30` → `+0xd4` | `0x00e31f10` → `+0xd0` | `0x100` | 1 |
| 2 | `0x0132cde8` | `is_paused` | `0xf0f1d7ea` | `0x5` | `0x00e32090` → `+0xdc` | `0x00e33bb0` → `+0xd8` | `0x100` | 0 |
| 3 | `0x0132ce08` | `is_stopped` | `0x5310a652` | `0x5` | `0x00e32bd0` → `+0x10c` | `0x00e32c90` → `+0x108` | `0x100` | 1 |
| 4 | `0x0132ce28` | `is_looped` | `0x0637a2c9` | `0x5` | `0x00e315a0` → `+0xe4` | `0x00e321f0` → `+0xe0` | `0x100` | 0 |
| 5 | `0x0132ce48` | `frame` | `0x73dacbd0` | `0x2` | `0x00e34ad0` → `+0xec` | `0x00e33df0` → `+0xe8` | `0x100` | 0 |
| 6 | `0x0132ce68` | `end_event` | `0xac38590c` | `0x4` | `0x00e324a0` → `+0xf4` | `0x00e32440` → `+0xf0` | `0x100` | 0 |
| 7 | `0x0132ce88` | `frame_event` | `0x93f10d89` | `0x4` | `0x00e35610` → `+0xfc` | `0x00e355b0` → `+0xf8` | `0x100` | 0 |
| 8 | `0x0132cea8` | `frame_event_num` | `0x4c441168` | `0x2` | `0x00e325e0` → `+0x104` | `0x00e33fe0` → `+0x100` | `0x100` | 1 |

### 4.10 `sr2_map` — `0x00e34640`, table `0x0132cf68`, 2 rows

**Registration body.** Parent: `element`. Pool `0x0132cfa8` (`3`). One enumeration binding, owner = `sr2_map`'s id (read through `0x0132cf50`): `map_mode` (`0x02a8a7b8`). Recommended parked by the scoping note. Only the table was read; nothing here goes near the minimap's world or zone code.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132cf68` | `zoom` | `0x966fa668` | `0x3` | `0x00e31300` → `+0xcc` | `0x00e32100` → `+0xc8` | `0x100` | 0 |
| 1 | `0x0132cf88` | `map_mode` | `0x61c03f57` | `0xd` | `0x00e30f30` → `+0xd4` | `0x00e31f10` → `+0xd0` | `0x100` | 0 |

### 4.11 `bitmap_circle` — `0x00e34c60`, table `0x0132d038`, 7 rows

**Registration body.** Parent: `element` (not `bitmap`, although it shares `image`/`source_*` names and codes with `bitmap`). Pool `0x0132d118` (`0x64`).

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132d038` | `image` | `0x031ff342` | `0xa` | `0x00e31300` → `+0xcc` | `0x00e32100` → `+0xc8` | `0x100` | 26 |
| 1 | `0x0132d058` | `screen_size` | `0x9cf96009` | `0x7` | `0x00e309c0` → `+0x7c` | `0x00e322a0` → `+0x78` | `0x0` | 0 |
| 2 | `0x0132d078` | `source_nw` | `0xb8cadfd6` | `0x7` | `0x00e30f30` → `+0xd4` | `0x00e31f10` → `+0xd0` | `0x100` | 0 |
| 3 | `0x0132d098` | `source_se` | `0xb41fc282` | `0x7` | `0x00e32090` → `+0xdc` | `0x00e33bb0` → `+0xd8` | `0x100` | 26 |
| 4 | `0x0132d0b8` | `start_angle` | `0x1e53ee14` | `0x3` | `0x00e315a0` → `+0xe4` | `0x00e321f0` → `+0xe0` | `0x100` | 14 |
| 5 | `0x0132d0d8` | `end_angle` | `0x971716a2` | `0x3` | `0x00e34ad0` → `+0xec` | `0x00e33df0` → `+0xe8` | `0x100` | 26 |
| 6 | `0x0132d0f8` | `num_wedges` | `0x84c659bc` | `0x2` | `0x00e324a0` → `+0xf4` | `0x00e32440` → `+0xf0` | `0x100` | 26 |

### 4.12 `document` — `0x00e35190`, table `0x0132d1b4`, 1 row

**Registration body.** Parent: `element`. Pool `0x0132d1d4` (`0x190`).

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132d1b4` | `document_name` | `0xf6e3b417` | `0x9` | `0x00e31300` → `+0xcc` | `0x00e32100` → `+0xc8` | `0x100` | 195 |

### 4.13 `gradient` — `0x00e35ab0`, table `0x0132d268`, 8 rows

**Registration body.** **Parent: `bitmap`.** It reads `0x0132c868` → `0x0132c864`, both at the `0x00e290c0` call and at the registry call. Pool `0x0132d368` (`5`). Its 8 rows add four corner colours (code `6`) and four corner alphas (code `3`) on top of `bitmap`'s `image`/`source_*` rows, which it inherits through the parent link.

| # | Row addr | Name | CRC-32 (`+0x18`) | `+0x04` code | `+0x08` setter thunk → vtable | `+0x0c` getter thunk → vtable | `+0x1c` | Records in 159 docs |
|---|---|---|---|---|---|---|---|---|
| 0 | `0x0132d268` | `gradient_nw` | `0x425d4fbb` | `0x6` | `0x00e324a0` → `+0xf4` | `0x00e32440` → `+0xf0` | `0x100` | 0 |
| 1 | `0x0132d288` | `gradient_ne` | `0xb1e43ef3` | `0x6` | `0x00e35610` → `+0xfc` | `0x00e355b0` → `+0xf8` | `0x100` | 0 |
| 2 | `0x0132d2a8` | `gradient_sw` | `0xbd3123a7` | `0x6` | `0x00e325e0` → `+0x104` | `0x00e33fe0` → `+0x100` | `0x100` | 0 |
| 3 | `0x0132d2c8` | `gradient_se` | `0x4e8852ef` | `0x6` | `0x00e32bd0` → `+0x10c` | `0x00e32c90` → `+0x108` | `0x100` | 0 |
| 4 | `0x0132d2e8` | `alpha_nw` | `0x2352eca4` | `0x3` | `0x00e32cd0` → `+0x114` | `0x00e357a0` → `+0x110` | `0x100` | 0 |
| 5 | `0x0132d308` | `alpha_ne` | `0xd0eb9dec` | `0x3` | `0x00e32890` → `+0x11c` | `0x00e35820` → `+0x118` | `0x100` | 0 |
| 6 | `0x0132d328` | `alpha_sw` | `0xdc3e80b8` | `0x3` | `0x00e358e0` → `+0x124` | `0x00e329b0` → `+0x120` | `0x100` | 0 |
| 7 | `0x0132d348` | `alpha_se` | `0x2f87f1f0` | `0x3` | `0x00e329f0` → `+0x12c` | `0x00e35920` → `+0x128` | `0x100` | 0 |

---

## 5. OPEN items

1. **The unknown property hash `0xfc6ad9f8`** (tag `5` bool; 89 records on `group`/`bitmap`/`document`/`text`/`clip`).
   - It matches no table row and no string in the executable.
   - The loader skips it, so it does not affect a port.
   - Its name, probably an authoring-tool flag, is unknown.
   - To settle it: check the authoring-side strings, or Lua sources in `interface.vpp_pc`.
2. **The `0xd` fallback path at `0x00e29d2c`** (what the loader stores when the enumeration lookup returns `0x7fffffff`) was not re-read in this pass. The sibling note says it falls back to the string.
3. **Codes `0xa`/`0xb`/`9`/`8`/`0xe`: the conversion code was not read.**
   - The resource resolution for codes `0xa` (image) and `0xb` (font) and the meaning of code `9` (`document_name` only) are named here from the property names alone (HIGH).
   - So is how the variant code `0xe` picks its runtime type.
   - The generic value switch in `spec-vint-doc-format.md` §5 (codes `8`/`0xb`/`0xc` resolve resources) needs re-checking against this vocabulary. `0xc` is unused by any table.
4. **The `+0x1c` flag.** HIGH as "persisted/loadable". The only reader found is `0x00e1f824` (text/XML path). Whether the binary loader `0x00e29860` or the Lua clone or copy paths ever test it was not settled by a full read.
5. **Type-id numbering.** HIGH as 0–12 in call order. `0x00e28c10`, called just before the registrations, was not read; it might reset or pre-fill the registry count.
6. **The pool constant's meaning** (capacity, HYPOTHESIS). The 12 pool constructors were not read. If it is a capacity, `point`'s is 0.
7. **Vtable slot `+0xc4`** (between post-draw `+0xc0` and the first type-specific accessor `+0xc8`) is not reached by any descriptor. Its role was not examined.
8. **Population coverage.** Only `interface_startup.vpp_pc`'s 159 documents were checked, and they contain no `gradient` and no `point` elements. A sweep of `interface.vpp_pc` would exercise the 20 unseen flag-`0x100` rows.
9. **Unrelated area.** The `.czn_pc` zone-data interior and named-object resolution topic was not crossed in this pass and was not opened. `sr2_map` was handled only at the descriptor-table level.

## 6. Suggested folds (for whoever serializes the spec edits)

- **`spec-lua-bindings.md` §9.1:**
  - replace "three distinct global storage cells" with "parent type id: none / `element` / `bitmap`";
  - add that `gradient` is a subtype of `bitmap`;
  - add the 12-byte registration record and the 16-byte registry slot.
- **`spec-lua-bindings.md` §9.2:**
  - add the 11 missing tables (§4);
  - extend the code list to `1–9, 0xa, 0xb, 0xd, 0xe` (no `0xc`);
  - downgrade the `+0x1c` "writable" HYPOTHESIS and replace it with §3.3;
  - record that the thunk addresses are shared across types (§1.3).
- **`spec-lua-bindings.md` §9.3:** close the orientation OPEN item (§3.2: setter = higher slot); mark §12.7 Group 7's orientation as wrong.
- **`spec-lua-bindings.md` §20.1:** `0x0132c0d0` is the pointer to `tween`'s type-id cell; code `0xe` is the variant code, also used by `text.insert_values`.
- **`spec-vint-doc-format.md`:**
  - §5 / §8 item 11: `0x00e307c0` read, plus the 10 enumeration tables (§1.4, §2);
  - §6 / §8 item 7: the image property is `image` (code `0xa`), the source rectangle is `source_nw`/`source_se` + `custom_source_coords`;
  - §7: `anchor` and `scale` are code `7` (vec2), CONFIRMED from the table and from 1,551/947 tag-`7` records on `bitmap` alone;
  - add the 99.76 % population match as data evidence for the hash variant.
- **Scoping note §1.2:** the cells `0x0132bdd0` and `0x0132c998` are the `element` and `text` type ids.

## 7. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names and no pasted pseudocode.
- Every behaviour above is described in my own words from disassembly listings (Ghidra `func`/`range` listings and capstone).
- Strings quoted as data are the engine's own:
  - property names;
  - enumeration names;
  - type names;
  - the text-path node names `property`/`name`/`type`/`value`;
  - the RTTI-derived name `vint_object_base`, already used by the scoping note.
- The C-runtime routine names `tolower` and `_stricmp` are library identities, not Ghidra auto-names.
- **Self-check, run on the finished file as the last step**, using the project's standard pattern `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b` (Python `re`, scratch script `u1_check.py`):
  - **0 hits on the whole file.**
  - The plain-substring backstop (`unaff_`, `extraout_`, `Stack_`, `Var`, `param_`, `local_`, `FUN_`, `DAT_`, `LAB_`, `undefined`) finds **0 hits in §0–§6**. Its only hits are in this §7, inside the quoted pattern and this list.
- No spec file was edited.
- The private Ghidra copy `tools\gp_vintu1` was deleted after the dumps (`Test-Path` = False).

