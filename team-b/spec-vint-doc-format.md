# Saints Row: The Third — `.vint_doc` UI Document Format Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, follow-on target (gap identified against `spec-lua-bindings.md` §5/§9/§15 — 727 of 804 shipped Lua scripts drive `.vint_doc` documents, and no spec previously described the format itself)
**Scope:** The `.vint_doc` binary file format: its magic/header, string pool, element tree, per-element property encoding, and (as far as this bootstrapping pass could reach) bitmap/peg references and anchor/tween serialization. Explicitly out of scope this pass: the runtime consumer of a loaded document beyond the load-time element-construction/property-apply path (e.g. per-frame tween playback, render-time bitmap/PEG resolution) and the separate, unused text/XML "editor" sibling format (documented briefly in §1.2 for completeness, since no shipped file uses it).
**Method:** Ghidra-based static analysis (decompiler + raw disassembly) of `SaintsRowTheThird.exe`, cross-referenced continuously against the already-confirmed code-side facts in `spec-lua-bindings.md` §5 (the 13-type UI element-type registry), §9 (the per-type property-descriptor table), and §15 (the shared VDO object handle/registry machinery) — plus population-wide validation against every real `.vint_doc` file found in one shipped archive, using a from-scratch Python parser built from the disassembly and cross-checked against the real bytes.
**Cleanroom compliance:** No decompiled code, disassembly listings, or original internal identifiers are reproduced verbatim below — every mechanism is described in this document's own words, with Ghidra addresses cited only as addresses-as-evidence, consistent with how every other spec in this project cites function addresses as supporting evidence for a Ghidra-derived finding. Real string content quoted from shipped files (e.g. `vdo_bitmap_viewer`, property names like `anchor`/`scale`) is ordinary shipped data, not proprietary source.

**Confidence key** (as in prior specs): **CONFIRMED — disassembly/empirical**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Overview

### 1.1 Population and scope of this pass

159 real `.vint_doc` files were extracted from `interface_startup.vpp_pc` (844 KB, the boot-time UI archive) — covering the cellphone UI, HUD, pause/store menus, cutscene-title screens, and every `vdo_*` base-widget document. **Zero `.vint_xdoc` files** were found in that same archive. The much larger `interface.vpp_pc` (280 MB, 2,097 entries) was **not** exhaustively swept this pass for additional copies — a solid sample (159 of the project's 804 real scripts' worth of UI surface) was validated, not the full corpus; sweeping `interface.vpp_pc` for completeness is the natural next step (§8, item 1). Every population count given below is against these 159 files specifically, not claimed project-wide.

### 1.2 Two formats exist in the executable; only one ever ships

The executable contains a real, complete, generic **text/XML-style document reader** (a TinyXML-shaped node-tree API) capable of expressing a `.vint_doc`/`.vint_xdoc` as authored markup, dispatched by a version/type-tag check (looking for a `vint_doc_type` node equal to the literal `vint_document`, or `vint_project` for the `.vint_xdoc` sibling, plus a `vint_doc_version` node equal to `1` or `2`). This is the **editor/authoring-format branch** — real and decompiled, but never observed in any of the 159 shipped files, exactly analogous to this project's own precedent elsewhere for a format variant the code supports but no shipped file actually uses. **Every real `.vint_doc` file is the separate, undocumented BINARY format** this whole document describes; the text/XML branch is noted here for completeness and is not otherwise covered.

### 1.3 File identification

Every real file begins with the identical leading 4-byte little-endian value. The loader reads this value first and branches: that exact value routes into the binary reader (this document); any other value falls through to the text/XML reader (§1.2). **[CONFIRMED — disassembly + 159/159 population: the magic value is `0x00003027`.]**

---

## 2. File header

**[CONFIRMED — disassembly, cross-checked field-by-field against 159/159 real files via an independently-written parser.]** All fields are little-endian; offsets are from the start of the file. Two tiny, dedicated stream-reader primitives (a bounds-checked little-endian 32-bit read that advances an internal cursor by 4 bytes) do every field read described in this section and in §3/§4 below — there is no separate "header struct" read as one block; the reader is a simple sequential cursor over the whole file.

| Offset | Size | Field | Population result (159 files) |
|---|---|---|---|
| `0x00` | u32 | magic | `0x00003027`, 159/159 |
| `0x04` | u32 | unknown/reserved | `0`, 159/159 |
| `0x08` | u16 | **version** | `1` (4 files) or `2` (155 files) — every real file is one of exactly these two, matching the runtime's own "version equals 1 or 2" gate exactly |
| `0x0A` | u32 | unknown | `0` in 154/159; the remaining 5 hold plausible IEEE-754 float bit patterns (three files share the identical value `0x3EB0C000` ≈ 0.345) — real data, not garbage, but role **OPEN** |
| `0x0E` | u32 | metadata-item count | 0–8 across the sample, median 2 |
| `0x12` | u32 | critical-resource count | 0–8, median 1 |
| `0x16` | u32 | secondary offset | always less than the file's own total size, 159/159 — **not** the string pool's base pointer (§3 explains the real base-pointer formula); read immediately after being compared against file length, consistent with some kind of bounds/section marker; exact role **OPEN** |
| `0x1A` | u16 | top-level **elements** count | 0–18, median 1 |
| `0x1C` | u16 | top-level **animations** count | 0–58, median 1 |

Total fixed header size: 30 bytes (cursor sits at `0x1E` for everything that follows). Versions `1` and `2` are both real, live formats found in this one archive (155 v2 files vs. 4 v1 files) — not a deprecated/current split. The loader's own binary-reader function handles both through one code path, with a version-dependent extra byte read per critical-resource entry (§3).

---

## 3. String pool, critical resources, and metadata

### 3.1 String pool — HIGH CONFIDENCE (mechanism confirmed by disassembly; exact byte-for-byte resolution not fully closed)

Immediately after the fixed 30-byte header, the file holds a **length-prefixed array of 32-bit string offsets**: one `u32` count *N*, then *N* `u32` offset values. **[CONFIRMED — disassembly.]** The reading function allocates `N×4` bytes for this array through the loader's own generic allocator and — this is the key, independently-confirmed fact — records the **stream cursor position immediately after the offset array itself** as the string pool's base pointer. A given string-pool index is resolved as `base + offsetArray[index]`, where `base` is that recorded post-array cursor position — **not** the header's own `0x16` "secondary offset" field (an earlier validation attempt using the header field as the base produced systematically truncated strings, e.g. `ument_depth` instead of `document_depth` — the exact symptom of a wrong base; switching to the cursor-position base produced clean, correct strings throughout, e.g. `vdo_bitmap_viewer`, `RADIO STATION`, `640x480`, full sentence-length text, and literal file-name tails like `ards.vint_doc`).

**Not yet fully closed:** after correcting the base-pointer formula, roughly half the sample (85/159 files) still resolves under 90% of its own string-table entries to clean printable text; many of the remaining "bad" resolutions are short fragments (e.g. `grp`, `t_bar`, `_grp`, `sub_grp`, `menu_sub_grp` all appearing together in one file) that look like a **suffix-sharing string pool** — distinct table entries deliberately pointing partway into a longer stored string, so a string that is a literal suffix of another one isn't duplicated. This is a plausible, known space-saving technique and several such suffix chains line up cleanly, but it was **not** confirmed against the reader's own code this pass (doing so would require tracing exactly how an entry's own end is delimited, since there is no per-entry length field alongside the offset — presumably each string is still read as NUL-terminated at the moment it's needed). **[OPEN — exact string-pool boundary semantics / whether suffix-sharing is real, or whether some index↔offset pairings are still off for a subset of entries.]** This uncertainty does **not** affect the header, element-tree, or property-tag findings in §4/§5 below, all of which were validated independently of exact string content (by count, offset-boundedness, and tag-byte structure, not string text).

### 3.2 Critical resources and metadata — CONFIRMED structurally (disassembly), selector byte OPEN

After the string-offset array, the file holds (in order): the critical-resource entries (count from the header's `0x12` field; each is a 1-byte selector plus a 4-byte raw value, with one further byte read only when the header's version field is `2`) — the exact selector-byte semantics (distinguishing, e.g., "autoload" vs. a plain critical-resource reference) were **not** pinned down this pass; then the metadata entries (count from the header's `0x0E` field; each is simply a flat pair of two string-pool indices, name then value) — this metadata-entry shape was cross-checked directly against the unused text/XML sibling's own `metadata`/`metadata_item` node handling and matches field-for-field, giving high confidence in the shape even where the binary reader's own body wasn't the sole source. **[CONFIRMED — disassembly for both entry shapes and their counts; OPEN — the critical-resource entry's own 1-byte selector meaning.]**

---

## 4. Element tree

**[CONFIRMED — disassembly.]** After the string pool, critical resources, and metadata, the file holds its top-level **elements** list and top-level **animations** list (counts from the header's `0x1A`/`0x1C` fields respectively). Both lists are walked by the **identical recursive record-reading function** — there is no structurally distinct "animation tree" encoding on disk. A top-level "animation" is an ordinary element-tree record (below) whose resolved type name happens to be `tween` or `animation` — two of the 13 registered element types `spec-lua-bindings.md` §5 already confirms — parented under the document's separate animations root instead of its elements root, rather than being a different record shape.

Each element record is laid out as:

| Field | Size | Content |
|---|---|---|
| type reference | u32 | a string-pool index; resolves to the element's registered type name, e.g. `bitmap`, `text`, `tween` (one of the 13 names in `spec-lua-bindings.md` §5) |
| name reference | u32 | a string-pool index; the element's own instance name |
| child count | u16 | number of immediately-nested child element records that follow this one's own property block |
| (unexamined) | 1 byte | read and unconditionally skipped by the reader; role **OPEN** — a flag or alignment pad |
| property block | variable | §5, below |
| children | variable | exactly *child-count* recursive element records, each parented under this one |

**This directly closes `spec-lua-bindings.md` §15's own open framing of "what a VDO element instance actually is when constructed":** the resolved type-name and name strings are passed straight into the same function `spec-lua-bindings.md` §15 already identifies as used by the shared handle/registry machinery, which itself bottoms out in the shared handle resolver and the shared VDO base-class registry cell that §15 documents. **The on-disk type reference is therefore resolved by NAME through the exact same registry the runtime type system uses — it is not a raw numeric type-ID baked into the file.** Type-specific object construction happens on the far side of that shared resolve call; the exact factory mechanism was not traced further this pass, though it is consistent with (not proven identical to) the per-type registration call `spec-lua-bindings.md` §9.1 already documents.

A boolean "reload mode" flag, threaded all the way down from the top-level dispatch function through every layer of the reader, makes the type/name resolution first try a hash-keyed lookup against a resolved parent's *existing* children (matching by the same hash field the runtime object registry already keys on) before constructing a fresh object. **HIGH CONFIDENCE:** this is a genuine hot-reload path — the same file can be re-parsed to update an already-live document's elements in place, not only to build a document from scratch — but no actual reload call site was observed exercising it this pass, so it is not independently confirmed against real runtime behavior.

---

## 5. Property encoding — the load-bearing result of this pass

**[CONFIRMED — disassembly.]** Each element's property block begins with a **per-resolution override lookup**, mirroring the unused text/XML sibling's own `overrides`/`baseline` node handling exactly (confirmed side-by-side against that sibling's own decompiled body): a `u32` baseline byte-offset, then a 1-byte override count, then that many `(resolution-name string-pool index, override-block byte-offset)` pairs. The currently-active resolution's name is compared against each override entry in turn; the reader jumps its cursor to whichever block matches, or to the baseline offset if none do. From whichever offset is selected, a flat list of property records follows, terminated by a zero tag byte:

| Tag | Following bytes | Value kind |
|---|---|---|
| `0` | (none) | end of property list |
| `1` | 4 bytes | a single raw value (applied through a dedicated path, not through the generic named-property setter described below — exact real-world kind **OPEN**) |
| `2` | 4 bytes | a single raw value (same — dedicated path, **OPEN**) |
| `3` | 4 bytes | a **float** |
| `4` | 4 bytes | a string-pool index, resolved to a **string** |
| `5` | 1 byte | a **bool** |
| `6` | 12 bytes | a **vec3** (three floats) |
| `7` | 8 bytes | a **vec2** (two floats) |

Before any of these tagged values, the property name itself is read as a **raw 32-bit hash value — NOT a string-pool index**. This hash is passed, unresolved through the string pool, into the same lookup function that ultimately returns a property-descriptor-shaped record; that record's own `+4` field is checked for the value `0xd` (13), and the record's own `+8` field (a function pointer) is invoked with the parsed value once decoding finishes.

**This is the single most important confirmed fact of this pass, and it directly settles `spec-lua-bindings.md` §9.2's own explicitly-flagged open hypothesis:** the `0xd` (13) check matches EXACTLY the property-descriptor `+0x04` "type/size code" field §9.2 already found and flagged ("values seen: 1, 3, 5, 6, 7, 0xd") — and the raw hash read from the file is computed, at property-table registration time, by exactly the same engine string-hash function §9.2 already identified as filling that same descriptor table's `+0x18` cache field. **On-disk element records reference a type's own named properties by NAME, hashed with the engine's standard string-hash, matched directly against the live property-descriptor table's own cached hash — not by a separate on-disk enumeration or index.** §9.2's own property-descriptor table (already confirmed for the `element` type's 17 named properties and `point`'s 1) is therefore the authoritative source for which property names a given element type accepts; this document's job is only the on-disk *encoding* of a name-hash/tag/value triple, not a duplicate listing of which names exist per type.

**RESOLVED, 2026-09-30 (adversarial review): the `spec-lua-bindings.md` §9.2/§9.3 getter/setter labels for offsets `+0x08`/`+0x0c` WERE swapped — now fixed there.** The loader invokes the descriptor-shaped record's own `+0x08` function with the freshly-parsed file value as a payload argument (`(targetObject, parsedValue, 0)`) while loading a document — a genuine write, confirming `+0x08` is the real **setter** and `+0x0c` is the real **getter**, the reverse of that section's original labeling. This was confirmed directly against one of the three already-disassembled property trampolines, per the concrete cheap-check this section originally proposed.

The seven on-disk value-type tags line up cleanly, by number, against a separate, independently-decompiled generic property-value switch used elsewhere in the property system (reached from the unused text/XML path, where property values arrive as parsed text rather than raw tagged bytes): `1`=a 64-bit signed integer, `2`=a 64-bit unsigned integer, `3`=float, `4`/a 9th code=string, `5`=bool, `6`=vec3 (with a named-color-string fallback), `7`=vec2, plus three further codes (`8`, `0xb`, `0xc`) that this generic switch handles by resolving a string to a named resource and reading one of that resource's own fields — plausibly a color-table entry, a font, and a third resource kind, none individually traced — and a `0xa`/default case. **The shipped BINARY `.vint_doc` format only ever emits tags `1`–`7`** — a genuine, population-visible fact, not a guess: the richer resource-lookup codes exist in the engine's general property-value vocabulary but were never observed in any of the 159 real files' own tagged-value streams.

---

## 6. Bitmap/peg references — HIGH CONFIDENCE, not traced end-to-end to a texture entry

No bitmap-specific on-disk record shape exists, and none was expected to: `bitmap` is simply one of the 13 registered element types (`spec-lua-bindings.md` §5), so a bitmap element is an ordinary element-tree record (§4) whose resolved type name is `bitmap`; whatever image it shows is almost certainly carried as one of that type's own named properties — most plausibly an ordinary **tag-4 (string) property** (§5), i.e. a plain resource-name string, resolved at load/runtime the normal way this project has already found for other named-resource references elsewhere (population-confirmed elsewhere in this project for shader names: stem-plus-hash resolution). This pass did not decompile the `bitmap` type's own property-descriptor table — `spec-lua-bindings.md` §9.2 only examined `element`'s 17 properties and `point`'s single property — so the exact property name that carries a bitmap's image reference is **OPEN**, as is confirmation of whether it names a `.peg_pc`/`.gpeg_pc` texture entry directly or names an intermediate atlas entry.

That intermediate atlas layer does genuinely exist elsewhere in this subsystem, just not inside the `.vint_doc` file itself: a cluster of functions (found via the literal string cluster `ImageWidth`/`Images`/`BitmapSheets`/`Bitmap Image Names`) implements a **separate, `.xtbl`-shaped manifest** — confirmed to explicitly look for a root `Table` → `BitmapSheets` → `Images` → `Properties` node shape, using the identical generic node-tree reader this document's own §1.2 already describes for the unused text/XML `.vint_doc` variant, and to build its own manifest filename using the literal `.xtbl` extension string. This manifest maps a named bitmap-sheet entry to a `StartX`/`StartY`/`ImageWidth`/`ImageHeight` sub-rectangle within a larger sheet texture — almost certainly the "many small UI images packed into one PEG sheet" atlas layer that sits between a bitmap element's own name string and the final PEG texture lookup. **[HIGH CONFIDENCE for the atlas layer's existence and shape; OPEN for the exact bitmap-type property name and the precise hookup from that property's string value through to either a direct PEG entry or through this BitmapSheets indirection.]**

---

## 7. Anchor/tween encoding — HIGH CONFIDENCE on structure, OPEN on the fine detail

`anchor` is a confirmed, settable property of the `element` type (`spec-lua-bindings.md` §9.2). By everything found this pass, it is serialized exactly like any other named property (§5) — almost certainly tag `7` (vec2), consistent with `anchor` being a 2D screen-space point, though this pass did not specifically read `element`'s own property-descriptor entry for `anchor` to confirm its `+0x04` type/size code matches `7` rather than `6` (both live in the same small-integer code space §9.2 already documents). A strong, independent, disassembly-confirmed data point supports the vec2 reading regardless: the property reader's own code explicitly hardcodes default values for exactly two properties, by name-hash, before reading any file data for them at all — `scale` defaults to `{1.0, 1.0}` and `anchor` defaults to `{0.0, 0.0}`, both pre-seeded as vec2-shaped (tag `7`) values used whenever the file supplies no explicit value.

`tween` is confirmed to be an ordinary element-tree type (§4/§5), parsed via the document's own top-level "animations" list using the identical element-record shape (type, name, child-count, properties, then recursive children) as everything else — there is **no** separate binary "tween chunk" format anywhere in this reader. Whatever keyframe/animation data a tween actually carries must therefore be expressed either (a) as ordinary properties directly on the tween element itself (plausible for simple from/to/duration-shaped tweens, using the existing float/vec3/vec2 tags), or (b) as **child elements** of the tween, using the confirmed child-count-plus-recursion mechanism — plausibly typed `point` (whose one known property, `screen_size`, is a plausible per-keyframe waypoint shape, per §9.2). This pass did **not** decompile a real tween- or animation-typed element's own property table to settle which of these it is, and did not trace the runtime's own keyframe-interpolation consumer at all. **[OPEN — the concrete keyframe-array encoding for `tween`/`animation` elements; flagged as the natural next target, since the general element-record and property mechanisms needed to read it, §4/§5, are both now confirmed.]**

---

## 8. Open items

1. **Population completeness.** Only `interface_startup.vpp_pc` (159 files) was swept this pass. `interface.vpp_pc` (280 MB, 2,097 entries) almost certainly holds further real `.vint_doc` files and was not searched — sweeping it would strengthen every population count above and might surface counter-examples this 159-file sample didn't.
2. **Header fields `0x0A` and `0x16`.** `0x0A`'s non-zero values in 5/159 files look like real float data, role unknown. `0x16` ("secondary offset") is read and bounds-checked against the file's own total size but its actual consumer was not identified.
3. **The critical-resource entry's own 1-byte selector.** Its value space and exact meaning (autoload vs. plain reference, or something else) were not pinned down.
4. **The one unconditionally-skipped byte following an element record's child-count field.** Possibly a flag or alignment pad; not decoded.
5. **String-pool suffix-sharing.** Whether the ~half of files with imperfect string resolution genuinely use a suffix-sharing scheme, or whether some index/offset pairings are simply still wrong, was not settled (§3.1).
6. **The possible getter/setter offset swap** flagged in §5 against `spec-lua-bindings.md` §9.2/§9.3 — a cheap, concrete check against an already-disassembled trampoline would resolve this quickly.
7. **The exact property name(s) carrying a `bitmap`-type element's image reference**, and its precise hookup to either a direct PEG entry or the BitmapSheets atlas indirection (§6).
8. **The concrete keyframe-array encoding for `tween`/`animation` elements** (§7), and the runtime's own keyframe-interpolation consumer.
9. **Whether `.vint_xdoc` ships anywhere at all** — zero found in the one archive searched this pass.
10. **The tags-`1`/`2` raw-value property paths** ("applied through a dedicated path, not the generic named-property setter") — their real-world kind was not identified.
