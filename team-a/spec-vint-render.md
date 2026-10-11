# Saints Row: The Third — Vint UI Render Path Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Scope:** How a loaded Vint UI document (element tree, `spec-vint-doc-format.md`) plus its current property values (`spec-lua-bindings.md` §9's property-descriptor tables) actually gets turned into draw calls: the per-frame tree walk, culling, render-state inheritance (tint/alpha/mask/background/render_mode), 2-D affine transform composition, depth sorting (both between documents and between siblings), the query-time geometry chain that backs the read-only `screen_nw`/`screen_se`/`screen_size` properties, and mouse hit-testing. This is a distinct layer from `spec-render-pipeline.md` (the low-level Direct3D 9 device/command-buffer architecture that this system's own draw calls eventually reach) and from `spec-vint-doc-format.md` (the on-disk file format this system loads).
**Origin:** scoping note `review/scoping-ui-render.md` sized this system and proposed a unit order (U0 loader closure, U1 property tables, U2 this document's core, U3 callback contract, U4+ per-type drawing). U1 (all 13 element types' property-descriptor tables) is folded into `spec-lua-bindings.md` §9/§9.2a. This document is U2: the shared render core.
**Method:** direct read of `SaintsRowTheThird.exe`'s own bytes (no Ghidra project needed for this pass — a disposable scratch disassembler plus capstone), recursive-descent from each named entry point, cross-checked function body against function body for every shared helper. Full method notes in `review/exe-notes-2026-10-01/interp_vint_u2_tree_transform.md`.
**Cleanroom compliance:** no decompiled source lines, pseudocode, or internal variable names are quoted anywhere below — only plain hex addresses, cited as evidence anchors, exactly as in this project's other specs. Property names, type names, and metadata key strings (`document_depth`, `bg_saints`) are the engine's own shipped data, quoted as data, the same convention as every other spec in this project.
**Confidence key:** **CONFIRMED** (read directly in disassembly), **HIGH CONFIDENCE** (inferred from structure, not independently traced), **HYPOTHESIS** (plausible, unconfirmed), **OPEN** (not settled).

---

## 1. Element fields read by the render core

These are the concrete object fields behind the property names `spec-lua-bindings.md` §9 already catalogues, read directly from the shared `element`-base accessor bodies (reached through the vtable slots that spec assigns: `+0x30`…`+0xb4`, lower slot = getter, identical across all 10 concrete element classes except where noted). **CONFIRMED** throughout.

| Element offset | Property | Getter slot → body | Notes |
|---|---|---|---|
| `+0x1c` | first child (link) | — | walk reads it to gather children |
| `+0x20` | next sibling (link) | — | walk and bounds code iterate it |
| `+0x24` | parent (link) | — | bounds/scale recursion climbs it |
| `+0x28` | handle (serial) | — | written by `0x00e28610` from counter `0x02a74de0` |
| `+0x2c` | name hash | — | written by `0x00e28770` (re-buckets the element in a 64-way table at `0x02a74ce0`); the hash is `0x00d9e740(string, 0)`, the engine's lower-cased CRC-32 — HIGH that the string is the element's name |
| `+0x30` | owning document | — | walk reads the document's clip-stack count through it |
| `+0x58`/`+0x5c`/`+0x60` | `tint` (r, g, b) | `+0x58` → `0x00e27890` | 3 floats |
| `+0x64`/`+0x68` | `anchor` (x, y) | `+0x48` → `0x00e27790` | setter `+0x4c` → `0x00e277c0` |
| `+0x6c`/`+0x70` | `offset` (x, y) | `+0x50` → `0x00e27800` | setter `+0x54` → `0x00e27830` |
| `+0x74`/`+0x78` | `unscaled_size` (w, h) | `+0xa8` → `0x00e28000` | getter first calls vtable `+0x24`, a per-type refresh hook (`text` overrides it) |
| `+0x7c`/`+0x80` | `scale` (x, y) | `+0x98` → `0x00e27e80` | setter `+0x9c` → `0x00e27ee0` |
| `+0x84` | `rotation` (float, radians) | `+0x90` → `0x00e27e20` | setter `+0x94` → `0x00e27e60` stores the value record's float unscaled |
| `+0x88` | `alpha` (float) | `+0x60` → `0x00e2e770` | |
| `+0x8c` | `render_mode` (dword) | `+0x30` → `0x00e275d0` | |
| `+0x90` | `auto_offset` (dword) | `+0xa0` → `0x00e27f40` | |
| `+0x94` | `mouse_depth` (int) | `+0x70` → `0x00e27a50` | returns `depth` (`+0x98`) while `+0x94` holds the sentinel `0x7fffffff` — "unset" means "follow depth" |
| `+0x98` | `depth` (int) | `+0x68` → `0x00e27980` | |
| `+0x9c` | `mask` (bool byte) | `+0x40` → `0x00e27740` | |
| `+0x9d` | `background` (bool byte) | `+0xb0` → `0x00e28060` | |
| `+0x9e` | `visible` (bool byte) | `+0x38` → `0x00e276d0` | |

**The three computed, query-time properties** (`screen_size`/`screen_nw`/`screen_se`) share one vtable slot — `+0x2c` (slot 11) — across all per-type overrides, each reached via the three getter trampolines already catalogued in `spec-lua-bindings.md` §9 (shared thunk shape: null-check, forward to a fixed vtable slot):

| Property | Getter slot → shared body | `group` override | `text` override | `point` override | Setter slot → shared body |
|---|---|---|---|---|---|
| `screen_size` | `+0x78` → `0x00e27c00` | `0x00e309b0` | `0x00e322c0` | `0x00e333f0` | `+0x7c` → `0x00e27c70` (a real setter, §5.5); `group`/`point` fail via `0x00e27bf0` |
| `screen_nw` | `+0x80` → `0x00e27b30` | `0x00e309f0` | shared | shared | `+0x84` → `0x00e27bf0` (always fails: read-only) |
| `screen_se` | `+0x88` → `0x00e27b90` | `0x00e309e0` | shared | shared | `+0x8c` → `0x00e27bf0` (read-only) |

`0x00e27bf0` is two instructions (return false). Vtable slot `+0xc4` is `0x00e27530` in all 10 classes — the `auto_offset` applier (§4.4).

---

## 2. The tree walk

### 2.1 What triggers a walk

**There is no cached or dirty-flag render tree.** Every element of every rendered document is re-walked and its render state fully recomposed, every frame.

- **Main path.** The UI frame function `0x00848e40` runs from `0x007025bc`, immediately after
  `0x00846ae0`. **Correction, 2026-10-10: `0x00846ae0` is NOT a tween/animation/data-item update
  function — that label is wrong, struck here.** Opened in full disassembly (body
  `0x00846ae0`-`0x00846c52`, independently re-verified against the live Ghidra server): it's a
  critical-section-guarded block of OS-cursor-visibility/exclusive-mode/window-focus housekeeping —
  reads window focus/state flags, compares against two cached bytes, conditionally calls a
  cursor/focus-state handler (`0x008464b0`) only on change, conditionally releases a cached object
  (gated by `0x00841ba0`), and ends with a few more state/flag calls. Nothing in its body touches
  any Vint document, element, or property. This closes `spec-vint-doc-format.md` §7/§8 item 8's own
  runtime-consumer question *negatively* for this specific address — the real tween-interpolation
  consumer is still unidentified; see the trace below for how far the real call chain was pushed.
  `0x00848e40` itself calls `0x00e22660` → `0x00e1f6e0`, "render all documents":
  1. clears global `0x02a4d184`;
  2. walks the circular document list rooted at `0x02a4d17c` (next pointer at document `+0`), collecting every document whose byte `+0x59b` is non-zero into a **64-entry stack array** (no bound check in the loop — CONFIRMED by listing; HIGH that the game never exceeds 64 simultaneously active documents);
  3. sorts them with `qsort` and comparator `0x00e1edb0` — **descending `document_depth`** (§6.1);
  4. calls the document renderer `0x00e1f0b0` with **pass byte 0** on each document whose byte `+0x59c` is zero.
- **Side path (OPEN purpose).** `0x00848d90` calls `0x007b3a70` (`this` = `0x012fced8`), which walks a game-side array backward and renders matching entries' documents with **pass byte 1**, then renders the document named `bg_saints` (string `0x01155ac4`) with **pass byte 0**. Possibly a render-to-texture or paused-background pass; not settled.
- **`document_depth`** is a document metadata value: `0x00e21490` scans the document's 64-byte metadata entries (document `+0xa8`, count `+0x588`) for key `document_depth` and parses it with `sscanf("%i")` into document `+0x58c`.

### 2.2 Document render `0x00e1f0b0(pass)`

In order: (1) builds a fresh 128-byte render state (`0x00e26630`, §3.1), overwriting tint/alpha to 1.0, `render_mode` dword to 1, and the pass byte into state `+0x65`; (2) calls the **screen fit** `0x00e1ef70` (§4.1) and folds its scale/offset into the state; (3) fires callback slot `+0x00`; (4) if the document's root element (`+0x594`) is non-null, calls the walk `0x00e26a30` on it with the state, a per-thread **256-slot scratch array** (TLS `+0x270`), and capacity 256.

**Cross-reference, 2026-10-10 (vint-doc tween/keyframe investigation):** step (3)'s "callback slot
`+0x00`" is a direct (not vtable-indirect) `CALL 0x00e229b0` at `0x00e1f12c`, immediately followed
by the `+0x594` root-element check that gates step (4)'s walk — independently re-verified against
the live Ghidra server. `0x00e229b0` was flagged by that investigation as a candidate home for
tween-progress/interpolation computation, but it was **not opened**, and given it's framed here as
a generic per-document "callback slot" (plausibly used by other, non-animation document types
too, not animation-specific), that identification is NOT confirmed — whoever opens `0x00e229b0`
next should settle whether it's animation-specific or a generic hook first, before assuming it's
the tween interpolation consumer. `spec-vint-doc-format.md` §7/§8 item 8 has the full open-item
framing and the now-fully-confirmed file-format side (every tween keyframe is an ordinary named
property on the tween element itself — no separate keyframe-array encoding exists).

### 2.3 The recursive walk `0x00e26a30(state, scratch, capacity)` — 197 instructions

Per element:

1. **Cull** (§3). If culled, return immediately — no hooks, no children, nothing.
2. Call **pre-draw** (vtable `+0xb8`).
3. Copy the parent's 128-byte state into a local and **compose** this element into it (`0x00e26680`, §4.2).
4. Push the composed `render_mode` (state `+0x00`) through callback slot `+0x18`, and the composed `background` flag (state `+0x64`) through callback slot `+0x1c` — for **every** non-culled element, drawn or not.
5. **Pass filter on the draw call** (state byte `+0x65`): pass 0 draws everything; pass 1 draws everything except `text` elements; any other pass draws nothing. The draw itself is vtable `+0xbc`(composed state, scratch, capacity).
6. **Gather children** into `scratch[0..n)` — only is-an-`element` children (so `tween`/`animation` children are structurally skipped, not culled); stops early at `capacity`, silently dropping the remainder.
7. If `n > 1`, sort with `qsort(scratch, n, 4, 0x00e26980)` (§6.2).
8. **Recurse** into each child in sorted order (scratch array used as a stack — `scratch + n`, `capacity − n`), driving the mask-mode state machine and the clip-stack unwind around the child loop when not already inside a mask subtree (§4.3/§4.4).
9. Call **post-draw** (vtable `+0xc0`), then return.

**Order:** depth-first, pre-order. A parent draws before its children; siblings go in comparator order (§6.2). Sorting is per sibling list only — depth never reorders an element across different parents.

---

## 3. Culling

The **only** cull in the render core is at the very top of the walk, before anything else:

1. **`visible`** clear (element byte `+0x9e` is 0) → cull.
2. **Inherited alpha** (the *parent's* composed alpha, state `+0x10`) ≤ `FLT_EPSILON` → cull.
3. **Own `alpha`** (`+0x88`) ≤ `FLT_EPSILON` → cull.

Both alpha tests use the x87 "less than" flag only, so a **NaN alpha is not culled** (an unordered compare also sets that flag); a negative alpha is culled. A culled element skips everything — pre-draw, state push, draw, its whole subtree, post-draw.

**There is no off-screen / bounds culling anywhere** — not in the walk, the compose step, the document render, or the render-all loop; nothing in them reads `unscaled_size` or any bound. `bitmap`'s own draw method (`0x00e312b0`) has no bounds test either. The pass filter (§2.3 step 5) is **not** a cull: it only suppresses the element's own draw call — hooks, state pushes, and children still run. **Query-time geometry (§6) ignores culling entirely**: `screen_nw`/`screen_se`/`screen_size` never read `visible` or `alpha`, so a hidden element still reports real bounds, and a visible element's bounds include hidden children.

---

## 4. Render-state inheritance

### 4.1 The 128-byte composed render state

Built per document (`0x00e26630` + `0x00e1f0b0`); copied per element by the walk and composed by `0x00e26680`. The parent's own copy is never modified.

| State offset | Content | Root value | Per-child rule |
|---|---|---|---|
| `+0x00` | effective `render_mode` | 1 (seeded) | override by element `render_mode` **if non-zero**; `default` (0) inherits |
| `+0x04`/`+0x08`/`+0x0c` | effective tint r/g/b | 1,1,1 | multiply component-wise by element `tint` |
| `+0x10` | effective alpha | 1 | multiply by element `alpha` |
| `+0x14` | inside-a-mask-subtree flag | 0 | OR with element `mask` |
| `+0x24`/`+0x28` | cumulative scale (x,y) | 1,1 | multiply by element `scale` |
| `+0x2c`/`+0x30` | position (pivot) | screen-fit offset | `+= M_parent · anchor` (§5) |
| `+0x34`…`+0x48` | 2×3 matrix `M` | `diag(fitScale)` | `M = M_parent · R(rotation) · S(scale)` (§5) |
| `+0x4c`/`+0x50` | content-box origin | written by compose only | `= position + M · offset` |
| `+0x54`/`+0x58`, `+0x5c`/`+0x60` | screen-fit scale, screen-fit offset | from `0x00e1ef70` | carried unchanged |
| `+0x64` | inside-a-`background`-subtree flag | 0 | OR with element `background` |
| `+0x65` | render-pass byte | document-render argument | carried unchanged |

`+0x18`…`+0x23` and `+0x66`…`+0x7f` are never written by this layer (OPEN — possibly used by per-type draw code).

**Summary per child:** `alpha`/`tint` are multiplicative down the whole path (no clamping); `render_mode` is "nearest non-`default` ancestor-or-self wins" (an all-`default` path yields the document's seed value **1** — HIGH that this is the `filter` enumerator, i.e. ordinary alpha blending); `mask`/`background` are sticky logical-OR flags; clip is **not** part of this state at all (§4.3); pass and screen-fit values are per-document constants. The two values actually pushed to the game per element (§2.3 step 4) are exactly the effective `render_mode` (callback slot `+0x18`) and the inherited `background` flag (callback slot `+0x1c`); tint/alpha/matrix/origin are read by the per-type draw directly from the state it's handed.

### 4.2 Mask subtrees

Driven only when the parent's composed byte `+0x14` is zero (not already inside a mask subtree). A per-thread mode value (0/1/2) is read/written by `0x00e237b0`/`0x00e23770` (which fires **callback slot `+0x08`(mode)** only on an actual change). Walking a parent's sorted children (mask children sort first, §6.2): a mask child in mode 0 fires **callback slot `+0x0c`** (no args) and sets mode 2; the first non-mask child in mode 2 sets mode 1; mode is restored to 0 after the loop if this frame opened it. So: mask children draw first (mode 2, "write mask"), remaining siblings draw masked by it (mode 1, "test against mask"), then mode returns to 0 (off). Everything under a mask child inherits `+0x14` set, so nested child loops below it do no further mask switching — **and, found by U4 (§13.3), also skip the clip-stack unwind entirely**, with a real consequence for a `clip` nested under a `mask`. **Upgraded from HYPOTHESIS to HIGH by U4 (§13.4/§13.5):** slot `+0x0c` is a stencil-buffer clear on the renderer singleton (public `D3DCLEAR_STENCIL`); the three modes correspond to three distinct state words whose mask-related nibble is confirmed (0/2/5) and whose colour/blend bits are forced off only in mode 2; the exact D3D9 stencil ops those words select stay OPEN (inside `rl_draw`, §13 item 3).

### 4.3 Clip — the document clip stack

Clip is a **document-level stack**, not part of the inherited state. Only the `clip` draw method (`0x00e30db0`, gated on `clip_enabled`) pushes: it computes an integer rectangle from `offset`/`clip_size`, maps it to device pixels with the state's screen-fit scale/offset, and pushes it (`0x00e1f5c0`) onto the document's 8-deep stack (document `+0x08`, count at `+0x584`) as the **intersection** with the current top — sent to **callback slot `+0x10`**. `0x00e1f6b0` pops: at count 0 it fires **callback slot `+0x14`**; otherwise it re-sends the entry at the *new* count, which is the entry **being popped, not the one beneath it**. **CONFIRMED as a real defect by U4 (§13.3), from a direct instruction-level read of the pop body itself** — not inferred from the walk's own indexing, and not merely a theoretical possibility: the off-by-one restore fires whenever a `clip` is nested inside another `clip`'s subtree with drawable content after the inner subtree, widening the outer clip's later siblings to the (wrong) intersection. **"Not rotation-aware" refined (§13.3):** the box's *pivot* does follow every ancestor's rotation (the full query-time position), but `offset`/`clip_size` themselves are only scaled, never rotated or sheared — so a `clip` under a rotated ancestor stays axis-aligned while its content-box sibling would not.

**Who pops:** the walk saves the document's clip count after an element's own draw and pops back to that saved value after the element's whole child loop. Consequence: a `clip` element clips **its own subtree and every later sibling under the same parent** — and since the depth comparator sorts `clip` children first (§6.2), a `clip` child effectively clips the whole of its parent's remaining content.

---

## 5. Transform composition

### 5.1 Document → device: the screen fit `0x00e1ef70`

Using the per-thread screen-size record (`0x00e237e0`, already in `spec-lua-api-behaviour.md` §26.26) and the document's authored size (`+0x5a0` width / `+0x5a4` height, `a = docW/docH`): if the global byte `0x0132bbfc` is non-zero (**fit to height** — HIGH that this is the shipped-build constant, no writer found), `H = screenH, W = a·screenH`; otherwise fit to width. `scale = (W/docW, H/docH)` (always uniform); `offset = ((screenW−W)·0.5, (screenH−H)·0.5)`. A document wider than the screen's aspect gets cropped sides; narrower gets pillarboxed. **Everything below the document is composed in device pixels** at render time (contrast §6.3).

### 5.2 Per-element composition `0x00e26680` — 165 instructions

With `P` the parent's composed state, `e` the element, `c = cos(rotation)`, `s = sin(rotation)` (standard C-runtime routines), `R = [c −s 0; s c 0]`, `S = [sx 0 0; 0 sy 0]`:

```
pos  = P.pos + P.M · anchor
M    = P.M · R · S
origin = pos + M · offset
cumulative_scale = P.cumulative_scale ⊙ scale
```

The matrix product helper `0x0078b790` computes `out = this · arg` for 2×3 affine matrices in double precision, rounded back to float on store.

**Closed form**, for a root-to-element path `e0…en`, a point `q` in `en`'s content box (`0 ≤ q ≤ unscaled_size`):

```
device(q) = fitOffset + Σ_{k=0..n} M_{k−1}·anchor_k + M_n·(offset_n + q)
M_{−1} = diag(fitScale),   M_k = M_{k−1}·R(rotation_k)·S(scale_k)
```

**Consequences:**
- Full 2-D affine composition; translation carried outside the matrix. A rotated child under a non-uniformly-scaled parent is **sheared** (`M = P.M·R·S` keeps the parent's non-uniform scale on the left).
- `anchor` positions the element in its **parent's** accumulated frame — never scaled/rotated by the element's own transform.
- Rotation and scale **pivot about the anchor point**; `offset` is applied inside the element's own rotated/scaled frame (places the content box relative to the pivot).
- **A parent's `offset` does not move its children** — children accumulate from the parent's `pos` (`+0x2c`), never from its `origin` (`+0x4c`).
- `rotation` is radians, stored unscaled; a positive angle is **clockwise on screen** (`x' = c·x − s·y, y' = s·x + c·y` with y down).
- The composed state is a stack local, thrown away when the element's walk frame returns — **nothing is written back to the element**.

### 5.3 What the draw receives (confirmed for `bitmap`)

`bitmap`'s draw (`0x00e312b0`) receives a pointer to the composed state: tint (`+0x04`) and alpha (`+0x10`) go to `0x00e23450`; the matrix (`+0x34`), origin (`+0x4c`), and two element fields (`+0xb8`/`+0xc0`, HIGH size/source data) go to `0x00e30470`, which forwards to callback thunk `0x00e22870`. The quad is built game-side from `origin` and `M`.

### 5.4 `auto_offset` — vtable slot `+0xc4` = `0x00e27530`

Writes `offset` from `unscaled_size (w,h)` via a 9-entry jump table; `none` (0) and values above 9 leave `offset` untouched:

| `auto_offset` | `offset` written |
|---|---|
| `c` (1) | `(−w/2, −h/2)` |
| `w` (2) | `(0, −h/2)` |
| `n` (3) | `(−w/2, 0)` |
| `e` (4) | `(−w, −h/2)` |
| `s` (5) | `(−w/2, −h)` |
| `nw` (6) | `(0, 0)` |
| `ne` (7) | `(−w, 0)` |
| `se` (8) | `(−w, −h)` |
| `sw` (9) | `(0, −h)` |

`auto_offset` names the point of the content box that sits on the anchor (the pivot). Slot `+0xc4` is re-fetched on every size-changing path of `bitmap`/`text`/`bitmap_circle`.

---

## 6. Depth sort

### 6.1 Between documents

`qsort` with comparator `0x00e1edb0`, which returns `b.document_depth − a.document_depth` — **larger `document_depth` drawn first** (further back). Equal depths and null pointers both compare as 0; `qsort` is not stable, so equal-depth order is unspecified.

### 6.2 Between siblings — comparator `0x00e26980`, 72 instructions

Keys, in order:
1. **Class key**, larger first: `(mask ? 1 : 0) + (exact type is clip ? 2 : 0)` → order **clip-and-mask, clip, mask, everything else**.
2. **`depth`** (signed), larger first — painter's order: larger `depth` drawn earlier (further back); smaller/negative on top.
3. **Element name hash**, smaller first — but the comparator **never returns 0** for equal hashes (returns −1), so two siblings tying on all three keys give an inconsistent comparator and an unspecified final order.

The class key outranks `depth`: a mask or clip child always draws before ordinary siblings regardless of depth. Sorting is per sibling list, every frame — `depth` never reorders across parents.

### 6.3 `mouse_depth` and hit-testing

`mouse_depth` (`+0x94`) defaults to sentinel `0x7fffffff`, read back by its getter as `depth` when unset ("unset `mouse_depth`" = "follow `depth`"). The hit test `0x00e2c4b0` (per input subscription): filters by subscription kind/event code, resolves the element, builds its quad via the hit-geometry builder `0x00e26c70` (same getters/helpers as §5, mapped by the parent chain, non-finite corners replaced with the parent position), maps to device pixels with the **same screen fit** as render time, then tests the point with a parallelogram test (`0x00e2b4e0`). **On a hit, the candidate with the lowest `mouse_depth` wins; ties go to whichever element is tested later.** The hit test consults neither `visible` nor `alpha` nor the clip stack directly (whether the geometry builder does is OPEN).

---

## 7. Query-time geometry — `screen_nw`/`screen_se`/`screen_size`

### 7.1 The getters recompute; they do not read anything the walk wrote

The render walk's composed state is a **stack-local copy**, discarded per element — the walk writes nothing back to any element field. The three read-only getters instead **recompute the geometry on demand** every call, through a separate query-time chain reached via vtable slot `+0x2c`:

- `0x00e272c0` (pos/M/M′ builder, 164 insns): recurses to the parent first (or starts from identity/zero at the topmost is-an-`element` ancestor), then applies this element's `anchor`/`rotation`/`scale` exactly as the render-time compose step does.
- `0x00e280d0` (corner builder, 150 insns): calls the above, adds `offset`, and emits all four corners of the content box (`unscaled_size`).
- `0x00e28540` (bounding-box builder, 61 insns, vtable slot `+0x2c` for 8 of 10 classes): takes the axis-aligned box of those four corners and **unions in every is-an-`element` child's own box, recursively**. `group` overrides this to union only its element children's boxes (a childless `group` collapses to a single point, dropping its own `scale`); `text` re-runs layout first if dirty, then falls through to the shared body.

**This query chain is an operation-for-operation re-implementation of the render-time compose step** — same matrix-product helper, same `R`/`S` construction, same order — cross-checked instruction range against instruction range (full correspondence table in the interp note, `review/exe-notes-2026-10-01/interp_vint_u2_tree_transform.md` §6.2). A third independent copy exists in the hit-test geometry builder (§6.3).

### 7.2 Where render-time and query-time geometry differ

1. **Coordinate space.** The render walk starts from the document's screen fit (`diag(fitScale)`, `fitOffset`); the query chain starts from **identity and zero** at the topmost ancestor. **So `screen_nw`/`screen_se`/`screen_size` are in the document's own authored coordinate space (the `+0x5a0`×`+0x5a4` canvas), not device pixels.** The engine itself converts with the same screen fit before hit-testing — `device = doc·fitScale + fitOffset`.
2. **Bounds are an axis-aligned box of the transformed content box, unioned with every child's box, recursively** — so `screen_size` is **not** simply `unscaled_size ⊙ cumulative_scale`; a rotated element reports its axis-aligned extent, and a parent with children reports the union. Invisible, zero-alpha, and pass-skipped children are still **included** (culling plays no part in this chain at all).
3. `group`'s bounds are its element children's union only (own `scale` dropped if childless, §7.1). `point` always reports `screen_size = (1,1)`.

**Worked example** (arithmetic, not a runtime capture — flagged OPEN for live confirmation): document 1280×720, screen 1920×1080 (fit to height, `fitScale` 1.5); a `bitmap` directly under the root with `anchor (100,50)`, `auto_offset c`, `unscaled_size (64,32)`, `scale (2,2)`, `rotation 0`. `auto_offset c` gives `offset (−32,−16)`; query time gives `pos (100,50)`, `M = diag(2,2)`, `origin (36,18)`, far corner `(164,82)`. Result: **`screen_nw = (36,18)`, `screen_se = (164,82)`, `screen_size = (128,64)`**; the render-time quad is drawn at device `(54,27)`–`(246,123)` — the same box × 1.5.

### 7.3 `screen_size` has a real setter (not all flag-0 properties are read-only)

The shared setter body `0x00e27c70` (129 insns, 7 of 10 classes): clamps the request to ≥ 1e-4 per axis, reads the current box (slot `+0x2c`) and current `scale`, then writes `new_scale = old_scale × requested / current_screen_size` through the `scale` setter. (It also multiplies in the parent chain's cumulative scale and divides it back out — the factor cancels algebraically.) **A zero current size divides by zero, unguarded.** `text` has its own override (writes `unscaled_size` directly, gated on a layout-ready bit); `group`/`point` fail (return false, no-op). `screen_nw`/`screen_se`/`unscaled_size` are genuinely read-only (setter is the universal fail-stub in all 10 classes). This means the flag-`0` marking in `spec-lua-bindings.md` §9.2 ("persisted/loadable") is unrelated to writability — `screen_size` is flag-0 (never persisted to a document) **and** writable at runtime.

---

## 8. Element defaults

From the element initializer `0x00e26900` (after the base link/handle initializer `0x00e28610`):

| Property | Default |
|---|---|
| `anchor`, `offset` | (0, 0) |
| `unscaled_size` | (1, 1) |
| `rotation` | 0 |
| `auto_offset` | 0 (`none`) |
| `mask` | 0 |
| `depth` | 0 |
| `visible` | 1 |
| `mouse_depth` | `0x7fffffff` (follow `depth`) |
| `tint` | (1, 1, 1) |
| `render_mode` | 0 (`default`) |
| `alpha` | 1 |
| `scale` | (1, 1) |
| `background` | 0 |

A host should seed every element with these before applying the loaded document's own property records.

---

## 9. Callback contract surface touched by this unit

For whoever specifies the callback contract (U3): every slot this unit fires, with its argument shape, all `cdecl`.

| Slot | Arguments | Fired from | Game-side body (§13.4/§13.5) |
|---|---|---|---|
| `+0x00` | (none) | document-render begin | `0x008470a0` — resets the scissor to the full UI bounds and the mask mode to 0 |
| `+0x08` | mode (0/1/2) | mask-mode change, via `0x00e23770` | `0x00847110` — stores the mode and rebuilds the context's packed state word |
| `+0x0c` | (none) | mask-subtree begin | `0x00842fe0` → the renderer's stencil-clear slot (`D3DCLEAR_STENCIL`) |
| `+0x10` | 16-byte rect pointer | clip push | `0x00844960` — sets the scissor rect absolutely (clamped to the UI bounds) |
| `+0x14` | (none) | clip stack empties | `0x00842fb0` — resets the scissor to the full UI bounds (not "off") |
| `+0x18` | effective `render_mode` | per element, every non-culled element | `0x00847060` — stores it and rebuilds the state word |
| `+0x1c` | effective `background` flag | per element, every non-culled element | `0x00847080` — stores it and rebuilds the state word |

**Unit U3 (§12) adds three further slots**, fired by per-type draw rather than the walk itself: `+0x04`
(set current colour; `bitmap`, `text` base/shadow, `text` stream colour-change records), `+0x20`
(textured quad; `bitmap`, `text` inline sheet images), `+0x2c` (glyph run; `text`). See §12.4 for
arguments and the game-side bodies.

**Unit U4 (§13) adds two more**, both per-type draw rather than the walk: `+0x24` (textured pie;
`bitmap_circle`) and `+0x28` (textured quad with four per-corner colours; installed, thunked, and
structurally complete, but **never actually reached** — `gradient`'s own draw body that would call it
is itself unreachable, §13.1). U4 also fully characterises the per-thread draw context `+0x04`'s body
(U3) builds/reads — all six of its fields, and the packed state word every primitive is handed — closing
§11 item 13; see §13.4.

**Unit U5 (§14, text) adds no slot to this table**; it reads U3's `+0x2c` game body in full (§14.1). What
text layout calls instead is a **second table, the resource-callback table** (pointer `0x02a5a004`, game
table `0x02317a78`): slots `+0x10`/`+0x14` (font by name), `+0x18` (font release, a game no-op), `+0x1c`
(line height), `+0x20` (advance of one character, with kerning), `+0x24` ("break anywhere" flag), plus the
already-known `+0x00`/`+0x0c` image slots (§12.1); the font slots are specified in
`spec-vint-font-format.md` §6. The string builder also uses a **third table** (pointer `0x02a5a014`, game
table `0x02317aec`): ~~`+0x04` localised string by key hash, `+0x08` key hash of a tag string, `+0x0c` named
insertion symbol, `+0x10` character case-folding for tag matching~~ **[Corrected 2026-10-03,
`spec-localization-format.md` §6.3, which specifies this whole table: this list omitted `+0x00`, add it —
`+0x00` is localised string by key *string* (null → null), the "game lookup" the §14.2 insertion table
means by "a string value is first tried as a localisation key" (the actual call site is `0x00e22e98`, not
the `+0x04` slot this list previously implied). `+0x04` localised string by key *hash* (no cache, searches
up to 14 loaded tables in load order, returns null for hash 0 or an unknown hash); `+0x08` key hash of a
tag string, returning **0** — not merely "a hash" — when the string is not a key in any loaded table (this
is why a plain non-key `text_tag` string still draws, §14.2's row below); `+0x0c` named insertion symbol;
`+0x10`** character case-folding for tag matching (HIGH: a per-character table lookup,
game `0x00841a80`, with a built-in Latin-1 table `0x00db0720` as the fallback) (§14.2/§14.4). All `cdecl`, all reached
only when the table and slot are non-null.

---

## 10. Size

| Set | Functions | Instructions |
|---|---|---|
| Named core (walk, compose, sibling comparator, bounding-box builder, corner builder, pos/M builder) | 6 | 809 |
| Full closure (+ state init, document render, screen fit, matrix product, per-type overrides, the three `screen_*` bodies, `screen_size` setter, `auto_offset` applier, cumulative-scale helper, rect helpers, document comparator, mask-mode get/set, clip pop, callback thunks) | 27 | ≈1,590 |
| + mouse hit-testing (`0x00e2c4b0`, `0x00e2b4e0`, `0x00e28290`, `0x00e26c70` structural, `0x00e2b730`) | 5 | ≈1,110 |
| + per-type draw, U3 (`bitmap` draw+quad+pool, `text` draw+player+builder+2 emitters+7 accessors+initialiser, `group`'s 1-insn stub, game-side `+0x04`/`+0x20`/`+0x2c` incl. the 773-insn glyph drawer read for structure only) | ~20 | ≈2,400 |
| + per-type draw, U4 (`gradient`'s 16 accessors + dead draw chain, `bitmap_circle`'s draw+9 accessors, `clip`'s draw+push+pop, the mask-mode context/state-word machinery, game-side `+0x24`/`+0x28`/`+0x0c`/`+0x10`/`+0x14`/`+0x18`/`+0x1c` bodies incl. `+0x28`'s 377-insn body read for structure only) | ~30 | ≈2,680 (≈1,300 Vint-side + ≈1,380 game-side) |
| + text, U5 (layout `0x00e36dd0` 509, run builder 310, segment dispatcher 146, the five tag handlers 537, line frames 266, colour emitter 43, small helpers 148, insertion formatter 363, colour-name lookup 50; game-side glyph drawer 773 now read in full + its two-layer wrapper 60 + repeat cache 196; 11 `text` property accessors ≈480 re-read for offsets) | ~35 | ≈3,880 (≈2,850 Vint-side + ≈1,030 game-side) |
| + fonts, U5 (`spec-vint-font-format.md` §9: loader, derived fonts, metric, font set-up, named table, metric callbacks, Vint font pool) | 23 | ≈1,240 (≈300 Vint-side + ≈940 game-side) |
| + the 2-D draw layer, U6 (§15: `rl_draw` entries, begin/reserve/end, the primitive renderer's allocator/draw/set-up, the state-word decoder, record writers and replay handlers for opcodes 11/17/`0x14`/`0x15`/41/43, the render-state commit, shader set-up and sampler-block builder; plus two shader programs) | ≈30 | ≈2,400 |

The scoping note's "~1k instructions" estimate matched the named core (809) exactly; the full closure needed roughly twice that, hit-testing adds a third increment on top, and per-type draw for the five types covered by U3/U4 (of 13) adds two more comparable increments. Every per-function count the scoping note gave for the U3/U4 functions matched exactly. **For text (U5)** the scoping note's ~1.5k Vint-side + ~1.1k font-side estimate was low by about 1.8× overall (≈5,100 read): the markup handlers, the insertion formatter and the game's font set-up were outside its address list, and its "loader `0x00b9cab0`" is the font set-up, not the file loader (`spec-vint-font-format.md` §4.2). Its per-function counts for `0x00e36dd0` (509) and `0x00b9bc10` (773) matched exactly.

---

## 11. OPEN items

1. The `0x00848d90` → `0x007b3a70` side render path's purpose (pass-1 documents plus `bg_saints` in pass 0) — possibly render-to-texture or a paused background.
2. ~~Callback semantics for slots `+0x08`/`+0x0c`/`+0x18`/`+0x1c` (exact meaning of mask modes 0/1/2; whether `render_mode` 1/`filter` is ordinary alpha blending) — for the callback-contract unit.~~ **Settled by U4 (§13.4/§13.5), upgraded HYPOTHESIS → HIGH: mode 2 = write the mask (stencil clear, fixed no-blend state), mode 1 = draw tested against it, mode 0 = off; `render_mode` has 5 real values, not just `filter`, each selecting a distinct 4-bit blend code. The one thing still OPEN is the exact D3D9 stencil ops and blend factors each code selects inside `rl_draw` — new item 16.**
3. ~~Clip-pop indexing (§4.3): whether the pop re-applies the rectangle being removed rather than the one beneath it (a possible nested-clip restore bug) — not checked against a real document.~~ **CONFIRMED as a real defect by U4 (§13.3), from a direct instruction-level read of the pop body itself.** Whether any shipped document's structure actually triggers it (a `clip` nested inside a `clip`'s subtree, with content after the inner subtree) is still open — new item 17.
4. Render-state bytes `+0x18`…`+0x23` and `+0x66`…`+0x7f`, never written by this layer — whether any per-type draw method uses them.
5. The hit-geometry builder `0x00e26c70` was read for structure only (500 instructions), not confirmed line-by-line; whether it itself skips invisible elements is unknown.
6. ~~`text`'s layout-dirty refresh hook (vtable `+0x24` → `0x00e330d0`, layout `0x00e31c50`) — deferred to the text-drawing unit.~~ **Closed by U3 (§12.2, the builder) and U5 (§14.2/§14.3, the layout it calls).**
7. **Live confirmation.** Every finding above is static analysis; a runtime capture of `screen_nw`/`screen_se` for a known element, compared against §7.2's worked-example arithmetic, would move the document-space-coordinates finding from CONFIRMED-by-code to CONFIRMED-by-data.
8. The `.czn_pc` zone-data interior / named-object resolution topic was not crossed and was not opened by this unit. ~~The minimap (`sr2_map`) was touched only as a vtable column, not investigated further — it stays parked per the scoping note's own recommendation.~~ **CLOSED, 2026-10-08.** `spec-hud-runtime.md` §6 restates `sr2_map`'s own registration/type-id/property facts (type id 11 of 13, properties `zoom`/`map_mode`) from the live-data-flow side and traces its live data feed in depth (shared blip-pool mechanism, the pause map's per-frame update chain, the base-map-texture asset pipeline). **Correction: `0x0132cf68` is `sr2_map`'s property-descriptor table, not its vtable** (matching `spec-lua-bindings.md` §9.2's own per-type table). **The real per-instance vtable's own base address is now CONFIRMED: `0x01259cb4`** (traced via the construction chain — type-registration → pool constructor `0x00e34530` → the per-object constructor `0x00e34290`, which writes `*this = 0x01259cb4` as its first act) — **the full 54-slot vtable (`+0x00`-`+0xd4`) was read in its entirety and terminates exactly there; `+0xd8` onward is the `"map_mode"` string literal, not more vtable.** Property-pair slot offsets, corrected from an earlier labeling artifact: zoom get=`+0xc8`/set=`+0xcc`; map_mode get=`+0xd0`/set=`+0xd4` — the same "lower slot = getter" convention this document's own `element` base-class pattern uses, not reversed as an earlier pass claimed. Draw (`+0xbc`) and post-draw (`+0xc0`) read in full: a single mode-gated 2D shape primitive (viewport-mask circle / border line) through a generic immediate-draw singleton, no icon blit, no per-object loop. The player-position-blip question is now resolved mechanistically (hypothesis (b) CONFIRMED: the corner minimap's background pans/rotates under a fixed-point player marker, driven by a separate, newly-located update function reading the player's own live position+heading) — see `spec-hud-runtime.md` §6.4 for the full chain; one narrow gap remains there (the literal glyph-draw instruction itself).
9. **(U3) Context byte `+0x09`** (§12.4): what sets the game-side sRGB→linear colour switch, and whether the shipped game runs with it on. **Narrowed further by U4 (§13.6):** the UI frame function writes it to **0 every frame**, unconditionally, right after the document-begin callback; no write of 1 was found anywhere (a raw sweep of 205 byte-store candidates at `[reg+9]` across all of `.text` was not filtered to the ones that matter). **HIGH: off throughout normal UI rendering** — `background` (which the state builder only applies while this switch is 0, §13.4) is therefore live in practice. Filtering that 205-candidate sweep to functions that also call `0x00846f10` would close this outright.
10. ~~**(U3) Explicit colours in `text` markup** (§12.2): whether any markup tag emits a stream colour-change record with an explicit alpha (escaping parent fades) or explicit rgb (ignoring tint), versus the `−1` inherit sentinel — for the text-layout unit, which owns the markup parser.~~ **Closed by U5 (§14.4/§14.5), CONFIRMED and *emulated*: yes to both.** Every `[color:…]` writes explicit rgb (all 711 English colour tags ignore the composed tint); `[color:#RRGGBBAA]` also writes an explicit alpha and so escapes parent fades — and it ships, in the 27 per-platform/per-language cheats-warning strings (`[format][color:#edc300FF]{0}[/format]`, alpha 1.0). Names and `#RRGGBB` keep alpha at −1 (fades still apply).
11. ~~**(U3) Two-layer fonts** (§12.4): font-table entry roles `+4`/`+8`, and per-glyph geometry/UVs/half-texel use inside the glyph-run drawer — font unit, with `.vf3_pc`.~~ **Closed by U5 (§14.1, `spec-vint-font-format.md` §4).** `+4` = primary font (also the only one measured), `+8` = secondary, drawn first when present and different; every shipped `_overlay` name repeats its primary (one draw) or, in three language arrangements, a parameter-identical copy (two identical draws) — not an outline/fill pair. Per-glyph quads/UVs are specified in §14.1; the half-texel constants are a near-edge-only UV inset.
12. ~~**(U3) Line-frame images** (§12.2): how the three `line_frame_*` handles become sheet indices and their placement — not exercised by any shipped document (`line_frame_enable` has 0 records).~~ **Closed by U5 (§14.6), CONFIRMED and *emulated*:** the bitmap resource's `+0x08` is used as the sheet index without checking the resource's mode (so only sheet-packed images work); W sits left of the line (negative x), M is stretched to the line width, E starts at the line's end, all centred on the line's middle and drawn under its text. Still unexercised by shipped documents.
13. ~~**(U3) Context block `+0x10`** (§12.4): whether this per-thread UI draw-context block is where the per-element `+0x18`/`+0x1c` pushes (§9) land — for the callback-contract unit.~~ **Settled by U4 (§13.4): yes.** The context's six fields are now fully mapped (`+0x00` effective `render_mode`, `+0x04` mask mode, `+0x08` inherited `background`, `+0x09` the sRGB switch of item 9, `+0x0c` the colour cache of item 14, `+0x10` a packed state word rebuilt from the other five and handed to every drawing primitive).
14. **(U3) Colour-cache coherence** (§12.4): whether anything other than the `+0x04` slot's own body changes the game's current draw colour without updating its redundancy-filter cache — a stale skip would otherwise be possible. **Narrowed by U4 (§13.6):** every per-element state rebuild re-sends the cached colour, so the cache and the actual draw colour stay in step within the UI's own callbacks. **One real exception found:** the minimap's game-side callbacks (`sr2_map`) call the raw colour setter directly, bypassing the cache — a minimap element can leave the cache stale, so the next Vint element whose colour happens to equal that stale value would be wrongly skipped. **`sr2_map`'s draw/post-draw vtable slots are no longer parked — read in full, 2026-10-08, item 8 above:** `+0xbc`/`+0xc0` draw a single mode-gated 2D shape primitive through a generic immediate-draw singleton; its live DATA feed is traced in `spec-hud-runtime.md` §6.
15. ~~**(U3) `gradient`'s draw** is `bitmap`'s own body...~~ **Fully settled by U4, §13.1.** `gradient` has its own intended draw method (`0x00e35410`, a per-corner-colour variant appended as a one-argument virtual at the end of its vtable — almost certainly a signature mismatch with the three-argument draw slot it was meant to override) but nothing anywhere in the executable ever calls it: the walk's draw slot still holds `bitmap`'s body, inherited unchanged. **In the shipped game a `gradient` draws exactly like a `bitmap`; its eight corner-colour properties are readable/writable but have no visible effect.** CONFIRMED by exhaustive linear sweep (no stored pointer to the method except the vtable slot itself; no virtual dispatch through that vtable offset anywhere in `.text`).
16. ~~**(U4, new) The packed render-state word's D3D9 meaning** (§13.4/§13.5): the exact stencil ops/reference/colour-write mask per mask mode, and the exact blend factors per `render_mode` value, are decoded inside `rl_draw`'s consumer of this word — not read, by the same boundary U3 kept. This is the one remaining open piece of what was item 2.~~ **Settled by U6 (§15.3–§15.6), CONFIRMED:** the decoder is `0x00e525e0`; all 16 blend codes, the depth nibble (`background` = depth test `LESSEQUAL` against a shader depth of 1.0, no writes) and all six stencil nibbles are tabulated there.
17. **(U4, new) Nested-clip population.** Whether any shipped document actually nests a `clip` inside a `clip`'s subtree with drawable content after the inner subtree (the one case the confirmed off-by-one restore, §4.3/§13.3, changes anything), and whether any document has a `clip` under a `mask` element (whose unwind the walk now confirmedly skips, §13.3).
18. **(U4, new) `num_wedges` values and the stack hazard** (§13 item 2.4): `bitmap_circle`'s `num_wedges` is unclamped and its per-draw angle table is stack-allocated — a sufficiently large authored or Lua-set value could overrun the stack. The 26 authored values in the swept documents were not read.
19. **(U4, new) `bitmap_circle` under rotation with a non-square extent** (§13.2): the ellipse's radii are applied along screen axes after the composed matrix, not the element's own axes, so a rotated, non-uniformly-sized `bitmap_circle` draws a different ellipse than the "obvious" formula would predict. Whether any shipped element hits this combination was not checked.
20. **(U5, new) `force_case` is dead** (§14.2): no reader of the value layout copies was found and *emulation* shows no effect. If a later pass finds a case-folding step outside the text paths read here (for example in a game-side localisation hook), this needs revisiting; until then a replacement should not change case.
21. **(U5, new) Stream-overflow behaviour** (§14.3): running out of the 12,800-byte stream garbles the output and, with a colour tag pending, never returns (*emulated*, contrived inputs). Shipped strings never approach the limit; Lua-built text was not surveyed.
22. **(U5, new) Mode-0 line-frame images** (§14.6): what a stand-alone (non-sheet) bitmap resource holds at `+0x08`, i.e. which sheet record a misused line frame would draw.
23. **(U5, new) Live confirmation of text.** The layout and glyph numbers in §14 are the real code's output under emulation with metrics from the real font file but the texture size supplied by hand and no live game state; a frame capture of one wrapped, centred, coloured `text` element would move §14.3–§14.5 to CONFIRMED-by-data.
24. **(U5, new) The readiness check at the top of the builder** (vtable `+0x20`, §14.2 step 1): what it tests was not read.
25. **(U6, new) Scissor-test enable at UI time** (§15.3, §15.9): the UI sets only the scissor rectangle; `D3DRS_SCISSORTESTENABLE` is inherited from the 3-D passes' state blocks, which set it to 1 at every site read. HIGH on; a capture (or a trace of the last block recorded before the UI buffer replays) would confirm it.
26. **(U6, new) Pixel constant `c37` during UI draws** (§15.7): every UI pixel is multiplied by it; its UI-time value comes from the state manager's two colours and two scalars (`0x00489ec0`, engine id `0x32`), whose writers for the UI frame were not traced. HYPOTHESIS: (1, 1, 1, 1).
27. **(U6, new) Output sRGB and the shaders' second pass** (§15.7): whether `D3DRS_SRGBWRITEENABLE` is on while the UI replays (the renderer's slots `+0x2c`/`+0x30`, `0x00477f30`/`0x00477fa0`, switch it off/on; callers not traced), and what selects the alpha-only second pass of `rl_prim_2d`/`rl_prim_2d_tex`.
28. **(U6, new) Two state-manager flags and one per-stage record** (§15.2 steps 2 and 7): `+0x4ec`/`+0x4f0` (set to 1/0 by every primitive draw) and the record `0x00474ae0` → `0x00476130` — effect not read.

---

## 12. Per-type draw: `bitmap`, `text`, `group` (unit U3)

**Origin:** `review/exe-notes-2026-10-01/interp_vint_u3_draw_bitmap_text_group.md`. Covers the draw method
(vtable slot `+0xbc`) for the three element types the scoping note ranked as most-populated, traced
through to the game's own 2-D draw layer. Method: the same Ghidra-free, capstone-based direct read of
`SaintsRowTheThird.exe` as U2. All addresses CONFIRMED by listing unless marked.

### 12.1 `bitmap`'s draw — `0x00e312b0` (31 instructions)

`this`-call, `ret 0xc` (state, scratch, capacity — only the state is used).
1. Resolves the element's image: the value record at element `+0xa0` is passed to `0x00e24150`, which
   returns null unless the record's type field is `0xa` (`image`), else looks its `+8` handle up with
   `0x00e30260` (the bitmap-resource pool, below). **No image → no draw at all** (nothing is sent to the
   game, not even the colour).
2. **Colour:** `0x00e23450(&state.tint, state.alpha)` (packs r,g,b,a: each ×255 via the double
   `0x012a2d88`, truncated, low byte kept, **no clamp**; sends the 4 bytes to **callback slot `+0x04`**
   if the table and slot are both non-null).
3. **Quad:** `0x00e30470` on the resource object, with `(&state.M, &state.origin, &element[+0xb8],
   &element[+0xc0])`.

**`0x00e30470` — resource → callback slot `+0x20` (37 insns).** `this` = the bitmap-resource object:
1. If resource `+0x04` (the engine texture handle) is `−1`, return — no draw.
2. Copy `source_nw` (two int32 at element `+0xb8`) to a local.
3. If resource `+0x0c` (mode) is `1` (an image packed in a bitmap sheet), add the sheet record's sub-
   image page position (`0x00e21850(resource[+8])` → `+0x08`/`+0x0c`) to the local copy.
4. Call thunk `0x00e22870` → **callback slot `+0x20`** with `(texture handle, &M, &origin, &adjusted
   source_nw, &element[+0xc0])` — `cdecl`, 5 arguments. **Only `source_nw` is shifted into page
   coordinates; `+0xc0` is passed through untouched**, consistent only because the game side (§12.4)
   treats `+0xc0` as an extent, not a corner.

**Element fields `+0xb8`/`+0xc0` are `source_nw`/`source_se`** (int32 x,y each), set via vtable `+0xdc`/
`+0xe4` and read via `+0xd8`/`+0xe0` — already catalogued as properties in `spec-lua-bindings.md` §9
and cross-referenced here for the draw. The `image`/`image_crc` setters reset both to `(0,0)`/
`(w,h)` whenever `custom_source_coords` is clear, and re-seed `unscaled_size` from the image's own
size. The `source_se` setter always returns false (stores the value but reports failure); `source_nw`
returns true.

**The bitmap-resource pool (`0x00e30260`):** 36-byte records at `0x02a88178`, at most `0xf0`; handle =
`(serial<<16)|index`, stale handles resolve to null. Mode 1 = packed in a bitmap-sheet atlas (texture =
the sheet page's own handle); mode 0 = a standalone image acquired through the resource-callback table
slot `+0x00`(name) — a different channel from the sheet atlas. `spec-tables-environment.md` §17.4
(BitmapSheets) is Vint's on-disk source for the sheet records this pool mirrors in memory.

**The quad, with `T` = the texture page's size in texels, `p` = the sheet sub-image's page position (0
for mode 0), `nw`/`se` = `source_nw`/`source_se`, `o`/`M` = the composed state's origin/matrix:**
```
u0 = (p.x+nw.x)/T.w,  u1 = u0 + se.x/T.w,   v0 = (p.y+nw.y)/T.h,  v1 = v0 + se.y/T.h
positions: o, o+M·(se.x,0), o+M·(0,se.y), o+M·(se.x,se.y)
```
**`source_se` is used as an extent (width, height), not a south-east corner.** With the defaults
(`nw`=0, `se`=image size) this draws the whole image at one content-box unit per texel, matching
`unscaled_size`. With a non-zero custom `source_nw`, the drawn quad is `se` units big — **not**
`unscaled_size` — so the bounds/geometry chain (§7, which uses `unscaled_size`) and the drawn quad can
disagree (HIGH that this is shipped behaviour on both sides; whether authored data compensates is OPEN,
§11 item 10).

### 12.2 `text`'s draw — `0x00e31b50` (78 instructions)

**No layout at draw time.** Layout has already turned the string into a small **command stream**
(element `+0x104` pointer / `+0x108` length). The draw just replays it: once for an optional drop
shadow, once for the text itself. If the stream is null, nothing is drawn, not even a colour. The
pre-/post-draw slots are both the shared no-op `0x00754410`.

1. **Shadow pass**, only if `shadow_enabled` (`+0xe6`) is set: colour = `shadow_tint` (`+0xd8`/`+0xdc`/
   `+0xe0`) × state tint, component-wise; alpha = `shadow_alpha` (`+0xb8`) × state alpha; origin =
   state origin **+ `shadow_offset`** (`+0xb0`/`+0xb4`) added directly, **after** the whole transform
   chain — so `shadow_offset` is in **device pixels**, not scaled/rotated with the element.
2. **Main pass**, always: colour = state tint/alpha unchanged; origin = state origin.

Both passes call the stream player `0x00e374a0(stream, length, &M, &origin, &rgb, alpha,
colour-commands-enabled)`.

**The stream player, 4 opcodes** (4-byte aligned, replay stops silently on any out-of-range read):
1. **move pen** (8 bytes `x,y`) — vestigial; no layout emitter found, no visible effect. **U5: CONFIRMED absent from layout** — every layout, run, tag and line-frame emitter was read in full (§14.3–§14.6) and none writes opcode 1.
2. **glyph run** (28 bytes + UTF-16 text: `font, x, y, sx, sy, count, spacing`) → **callback slot
   `+0x2c`** with `(font, &text, 0, count, &(M·S(sx,sy)), &(o+M·(x,y)), spacing)`. Odd-length runs are
   padded with one 0 code unit; only the low byte of `spacing` is written (read back as a signed byte
   game-side, §12.4).
3. **inline sheet image** (20 bytes: `sheet index, x, y, sx, sy`) → **callback slot `+0x20`** with
   exactly `bitmap`'s argument shape (texture, `M·S(sx,sy)`, `o+M·(x,y)`, page position, size). This is
   how `line_frame_*` images draw (up to 3 per line, §11 item 12). Drawn in the shadow pass too, tinted
   with the shadow colour — colour-change records (opcode 4) are the only ones suppressed there.
4. **colour change** (16 bytes `r,g,b,a`) → **callback slot `+0x04`**. **A component of exactly `−1.0`
   inherits the pass's base colour; any other value replaces it — not multiplies.** Layout seeds the
   base colour-stack entry at `(−1,−1,−1,−1)`, so untagged text always inherits; ~~whether any markup
   writes an explicit value is OPEN (§11 item 10)~~ **U5: markup does — every `[color:]` writes explicit
   rgb, and `#RRGGBBAA` an explicit alpha that escapes parent fades; it ships (§14.5).**

**Colour persists across records and across the shadow/main boundary** (it is the game's *current*
colour, not stream-local) — harmless in practice only because every element sets its own colour before
drawing anything.

**Shadow/font fields**, all read directly at draw time except where noted (values default from the
constructor `0x00e319a0`): `text_scale` (`+0xa8`/`+0xac`, default (1,1), dirties layout) · `font`
(`+0xcc`, a handle into the 10-entry Vint font pool `0x02a88050`, default none, dirties layout) ·
`shadow_offset` (`+0xb0`/`+0xb4`, default (0,0)) · `shadow_alpha` (`+0xb8`, default 1.0) ·
`shadow_tint` (`+0xd8`/`+0xdc`/`+0xe0`, default black) · `shadow_enabled` (`+0xe6`, default off). None
of the four shadow fields dirties layout — they're read fresh every frame, so animating them is free;
`text_scale`/`font` force a re-layout. Not read by the draw at all: `word_wrap`, `wrap_width`,
`horz_align`, `force_case`, `leading`, `kerning`, `text_tag`, `insert_values` — these act only through
what layout bakes into the stream.

**The stream builder** (`0x00e31c50`, also vtable `+0x24`, the layout-dirty refresh hook §4.1/§5.3
flagged OPEN): builds into a **12,800-byte (`0x3200`) stack buffer** — the hard per-element ceiling;
every emitter stops silently on overflow (~~HIGH: very long text is truncated, not exercised~~ **U5:
not a clean truncation — *emulated* overflow garbles the stream or never returns; shipped strings stay far
below the ceiling, §14.3**). A non-zero
result is copied to a heap block (`+0x104`/`+0x108`); the measured size is also copied into
`unscaled_size`, and `auto_offset` (§5.4) is re-applied.

### 12.3 `group`'s draw — `0x00abd220` (1 instruction)

`group`'s draw slot, and its pre-/post-draw slots, are all `ret` stubs — the same generic empty-body
stub shared with `point` and `document` (not `group`-specific). **A `group` draws nothing of its own:
no background, border, clip, or outline** — not an inference from "groups are containers," the slot
body is a literal bare return. Everything else in the walk around it (§2–§6: culling, state
composition, the `+0x18`/`+0x1c` pushes, mask/clip handling) still runs normally for a `group`; the one
`group`-specific behaviour is query-time only — its bounding box unions its element children only (§7.1).

### 12.4 The game side of slots `+0x04`, `+0x20`, `+0x2c`

**Installation (CONFIRMED):** the UI bring-up `0x008489e0` writes an 18-slot table at `0x02317a30`
(stored at `0x02a5a008`); for the slots this unit uses, **`+0x04` = `0x00847130`**, **`+0x20` =
`0x00847d40`**, **`+0x2c` = `0x00848890`**. The Vint-side thunks each tail-jump into these only when
both the table and the slot are non-null, so the pushed arguments arrive unchanged as a plain `cdecl`
call. A per-thread UI draw context (`0x00846f10`, TLS-cached) carries: byte `+0x09` (colour-space
switch, below), dword `+0x0c` (last colour sent, the redundancy-filter cache), and a block at `+0x10`
passed to every primitive (HIGH: possibly where the `+0x18`/`+0x1c` per-element pushes of §9 land —
OPEN, §11 item 13).

- **`+0x04` — set current colour (`0x00847130`, 84 insns).** If context byte `+0x09` is set, r/g/b are
  decoded from sRGB to linear (the standard `c≤0.04045 → c/12.92`, else `((c+0.055)/1.055)^2.4` curve;
  **alpha is passed through unchanged**), then all four are re-quantised (×255+0.5, truncated). If the
  resulting packed value equals the context's cached colour, **nothing is sent** — a redundancy filter.
  Otherwise it expands to floats and submits to the game's `rl_draw` layer, and updates the cache.
  What sets context byte `+0x09` is OPEN (§11 item 9) — it decides whether a host should linearise UI
  colours to match.
- **`+0x20` — textured quad (`0x00847d40`, 328 insns).** Arguments `(texture, &M, &o, &nw, &ext)` —
  `nw`/`ext` int32 pairs, exactly what `bitmap` (§12.1) and `text`'s inline-image opcode (§12.2) both
  pass. Reads back the current colour (set by `+0x04`) as the vertex colour, packed **b,g,r,a**
  (`D3DCOLOR` order — the colour survives the byte→float→byte round trip exactly for all 256 values,
  numerically checked). Texture size: `(10,10)` if the handle is `−1` (text's sheet lookup can produce
  this; `bitmap` never does), else a `u16` width/height header read. UVs: `u0=nw.x/T.w,
  u1=u0+ext.x/T.w` (same for v) — **no half-texel offset** *(in the vertex UVs; U6, §15.7: the pixel shader adds exactly half a texel to every textured UI sample, so the sampling is texel-centre aligned after all)*. Positions: `o, o+M·(ext.x,0), o+M·(0,ext.y),
  o+M·(ext.x,0)+M·(0,ext.y)` — a 4-vertex, 2-triangle strip, order `(u0,v0),(u1,v0),(u0,v1),(u1,v1)`.
  (A dead inverse-matrix computation earlier in the function multiplies by zero and has no effect
  except turning a zero-length scale axis into a NaN on an already-degenerate quad.)
- **`+0x2c` — glyph run (`0x00848890`, 43 insns → `0x00b9cf20`, 60 → `0x00b9bc10`, structure only).**
  **U5 read `0x00b9bc10` in full: per-glyph geometry, UVs and the half-texel inset are in §14.1.**
  Splits the matrix into two row vectors and forwards. An out-of-range `font` index falls back to font
  0. Each font-table entry (12 bytes, table at `0x0290f550`) can carry **two sub-font ids**, both drawn
  one over the other if present (~~HIGH: an outline/fill pair — font-unit material, §11 item 11~~ **U5:
not an outline/fill pair — the second id is a secondary drawn first, and every shipped `_overlay` font
repeats or duplicates its primary; §14.1, `spec-vint-font-format.md` §4.3**). Reads
  back the current colour the same way as the quad path. Emits glyphs in **batches of at most 64**, as
  a triangle list (2 triangles / 6 vertices per glyph, same 28-byte vertex stride as the quad path).
  **`spacing` is read back as a signed byte** and added to each glyph's advance, which is what makes
  the stream field's three unwritten bytes (§12.2) harmless.

### 12.5 Cross-observations

- **Colour is state, not a draw argument.** No drawing callback takes a colour; every type sets the
  game's *current* colour via `+0x04` first, and the quad/glyph callbacks read it back. Packing is
  identical everywhere it happens (Vint side and the stream player alike): ×255, truncated, low byte
  kept, no clamp, r,g,b,a order.
- **One geometry contract for every textured quad.** `bitmap` and `text`'s inline images both call
  `+0x20` with `(texture, M, origin, source corner, extent)`; glyph runs use the same
  `(M·S, origin+M·(x,y))` placement idiom through `+0x2c`. Translation is always carried outside the
  matrix, consistent with §5.
- **Gates, not culls.** `bitmap` skips on a missing image/texture, `text` on a null stream, `group`
  always — none of them test bounds or the clip stack; this is on top of, not instead of, §3's cull.
- **`gradient` shares `bitmap`'s exact draw body** (`0x00e312b0`) rather than its own — see §11 item 15.

---

## 13. `gradient`, `bitmap_circle`, `clip`, and mask/`render_mode` on the game side (unit U4)

**Origin:** `review/exe-notes-2026-10-01/interp_vint_u4_gradient_circle_clip_mask.md`. Resolves the
`gradient` mystery §11 item 15 left open, `bitmap_circle`'s draw, `clip`'s own draw body (the stack
mechanics were already in §4.3), and upgrades the mask-mode/`render_mode` semantics from guesses to
direct reads. Same Ghidra-free method as U2/U3.

### 13.1 `gradient` has a real draw method — that nothing ever calls

`gradient`'s vtable (77 slots) matches `bitmap`'s (60 slots) slot-for-slot through `+0xec`, then adds
16 property accessors for eight corner colours (`gradient_nw`/`_ne`/`_sw`/`_se`, each rgb, plus
`alpha_nw`/`_ne`/`_sw`/`_se`) and one more slot at `+0x130` — the vtable's **last** slot, **not**
`+0xbc` (the draw slot every other type uses): `0x00e35410` (97 instructions).

**That method is permanently unreachable.** An exhaustive linear sweep of the whole binary found: no
stored pointer to `0x00e35410` anywhere except the vtable slot itself; no virtual dispatch through
vtable offset `+0x130` targets any Vint object anywhere in `.text` (the few load-then-call sites at
that offset are unrelated code — a plain function-pointer field, an unrelated object's own vtable, and
one genuinely different method on `text` that happens to share the offset by coincidence). Its own
callee chain (`0x00e303c0` → thunk `0x00e22890` → callback slot `+0x28` → game body `0x00848280`) has
no caller outside this chain either. **So `gradient`'s draw slot (`+0xbc`) still holds `bitmap`'s own
body, `0x00e312b0` (§12.1) — a `gradient` element draws exactly like a `bitmap`, uniformly coloured by
tint/alpha, and its eight corner-colour properties are readable/writable but have no visible effect.**

**HIGH cause:** `0x00e35410` is a one-argument `this`-call (state only); the draw slot it should have
overridden is a three-argument `this`-call (state, scratch, capacity). A C++ virtual whose signature
doesn't match the base method it was meant to override becomes a separate, non-overriding virtual —
exactly this binary's own layout (a new slot appended after every other member, rather than installed
at `+0xbc`). Not provable from the binary alone, but nothing else fits as well.

**What the dead path would have drawn, for the record:** `0x00e35410` would multiply each corner's rgb
by the composed tint and each corner's alpha by the composed alpha, then call a `bitmap`-style
resource helper widened to 13 arguments (texture, matrix, origin, source rect, four corner colours),
through callback slot `+0x28`. The installed game-side body for `+0x28` (`0x00848280`, 377
instructions) is otherwise identical to the `+0x20` textured-quad body (§12.4) but writes each of the
quad's four vertices its **own** colour instead of reading back one shared current colour — a
bilinear (Gouraud) colour blend across the quad. This body is fully built, installed and reachable
*from the game side*; only the Vint-side call into it never happens.

**No shipped document in the swept population (`spec-lua-bindings.md`/U1's 159-document sweep) uses a
`gradient` element**, and no extracted Lua script names any of the eight corner properties — so this
dead path is not hiding a visible authored difference in the data this project has read. A faithful
port draws `gradient` as `bitmap`; "fixing" it to honour the corner colours would diverge from the
shipped game in any unswept content that does use one.

### 13.2 `bitmap_circle`'s draw — a textured pie of `num_wedges` triangles

Draw `0x00e34880` (38 insns) resolves the image exactly as `bitmap` (§12.1) and sends the composed
colour through callback `+0x04`, same as every other type — **every vertex of the pie shares the one
current colour**, unlike the dead `gradient` path. It then calls a `bitmap`-style resource helper
(`0x00e304e0`, widened by three arguments) which forwards to **callback slot `+0x24`** with
`(texture, M, origin, source_nw, source_se, start_angle, end_angle, num_wedges)`.

**Element fields** (defaults from the initialiser): `start_angle` (`+0xd0`, default 0), `end_angle`
(`+0xd4`, default 2π), `num_wedges` (`+0xd8`, default 16) — **none of the three setters clamps or
validates** (no wrap, no ordering check, no minimum/maximum on the wedge count).

**Game-side body `0x00847280` (691 instructions, structure confirmed, every vertex formula read):**
builds a stack-allocated table of `num_wedges + 1` rotation matrices sweeping from `start_angle` to
`end_angle` in `num_wedges` equal steps (angles follow the same convention as the element `rotation`
property: radians, clockwise on screen), then emits `num_wedges` independent triangles — **centre →
rim(aₖ) → rim(aₖ₊₁)** — as a triangle list (not a fan or strip). The rim position applies the half
extents **along screen axes, after the composed matrix**; the rim UV samples the ellipse inscribed in
the source rectangle, with the texture itself not rotated by the angle sweep. A full circle (the
defaults) is therefore a 16-sided polygon approximation, not a true circle.

**Consequences for a port:** `end_angle` < `start_angle` sweeps anticlockwise (legal, no special-casing
needed); `|end − start| > 2π` self-overlaps with no wrap guard; `num_wedges` is unclamped and its
angle table is **stack-allocated**, so an authored or script-set value in the tens of thousands could
overrun the stack — a port should clamp or heap-allocate rather than copy this hazard; because the
half-extent scaling happens along screen axes rather than the element's own axes, a rotated element
with a non-square source rectangle does not draw the "obviously expected" rotated ellipse (the
"naive" formula `centre + M·(r·cos a, r·sin a)` only matches the shipped result when `M` is diagonal or
the source rectangle is square); the query-time bounds (§7) still report the full `unscaled_size` box
regardless of the angle range, so a partial pie's reported bounds are always the complete square.

### 13.3 `clip`'s own draw body, the push/pop stack, and the confirmed off-by-one

`clip`'s draw (`0x00e30db0`, 46 insns) only runs when `clip_enabled` is exactly 1 (default on,
`clip_size` defaults to (20, 20) — and its setter also writes `unscaled_size`, unlike every other
type's size-bearing setter, so a `clip`'s query-time bounds, §7, follow `clip_size` directly). It
builds a document-space rectangle from the element's **query-time pivot position** (which *does*
follow every ancestor's rotation — refining §4.3's "not rotation-aware" to specifically mean the box
itself: `offset` and `clip_size` are scaled by the cumulative parent scale but never rotated or
sheared, so a `clip` under a rotated ancestor stays axis-aligned while its own content box would not),
rounds half-away-from-zero in document space, then maps to device pixels — **the near edges are
truncated, the far edges rounded**, which can make the device-space rectangle up to one pixel larger
than rounding all four edges the same way would give.

**The push (`0x00e1f5c0`, document-level 8-deep stack) intersects with the current top**, giving
`(0,0,0,0)` on no overlap (touching edges count as overlap, giving a zero-width/zero-height box); at a
count of 8 or more, nothing is stored or sent but the count still advances, so the overflow bookkeeping
for later pops stays balanced.

**The pop (`0x00e1f6b0`) is confirmed, by direct instruction-level read of its own body, to re-send the
entry it just removed rather than the one beneath it** — settling §4.3/§11 item 3 outright rather than
leaving it as an inferred possibility. **Effect:** `clip` C1 containing a nested `clip` C2 followed by
more of C1's own content under the *same* parent as C2 — specifically, any sibling of C1 drawn after
C1's subtree closes — ends up clipped to C1∩C2 instead of plain C1, because C1's own unwind re-applies
the (now-popped) C1∩C2 rectangle instead of restoring C1 alone. A single level of `clip` (no nesting)
is unaffected, since its unwind goes straight to "clip off." Whether any shipped document's structure
actually triggers this (§11 item 17) was not checked.

**New this pass: the walk skips the clip unwind entirely inside mask subtrees.** The child-loop
branch taken once inside a mask subtree (§4.2) only recurses — it does none of the normal branch's
clip save/restore bookkeeping. So a `clip` under a `mask` element is not popped by its own immediate
parent; it stays pushed until the unwind of the nearest ancestor that is *not* itself inside a mask
subtree, clipping that ancestor's own later content too, in between.

**Game side (`+0x10`/`+0x14`) is an absolute scissor set/reset, not itself a stack:** `+0x10` clamps
the incoming rectangle into the UI's own screen bounds and sets the scissor; `+0x14` resets the
scissor to those **full bounds** (not "off"). Every document's own begin callback (`+0x00`) also resets
the scissor to the bounds, so each document starts unclipped regardless of what the previous one left.

### 13.4 The per-thread draw context and the packed render-state word

The context U3 found (`0x00846f10`) has six fields, all now mapped: `+0x00` effective `render_mode`,
`+0x04` mask mode (0/1/2), `+0x08` inherited `background`, `+0x09` the sRGB switch (§12.4, §11 item 9),
`+0x0c` the last colour sent (§12.4, §11 item 14), `+0x10` a **packed render-state word** rebuilt from
the other five fields and handed to every drawing primitive — this is U3's "block at `+0x10`," settling
§11 item 13. `+0x08`/`+0x18`/`+0x1c` (the per-element pushes, §9) share one shape: store the new value
only if it changed, then rebuild the word.

**The state-word builder** picks a base word from the mask mode (0 → one constant, 1 → a second, 2 → a
third with `render_mode`/`background` forcibly ignored and colour re-send skipped entirely), then, for
modes 0/1 only, ORs in `background`'s contribution (**active only while the sRGB switch is 0**) and
`render_mode`'s 4-bit code (§13.5), and finally re-sends the cached colour before storing the word.
**So the three mask modes differ in one 4-bit nibble of this word (0 / 2 / 5), with mode 2 additionally
forcing the colour/blend bits off** — upgrading §4.2's HYPOTHESIS-grade "write mask"/"test against
mask" reading to HIGH: mode 2 also clears the renderer's stencil buffer first (callback `+0x0c`, a
public `D3DCLEAR_STENCIL` call on the renderer singleton `spec-render-pipeline.md` §4.4 already
documents). **The exact D3D9 stencil/blend operations each nibble value selects are inside `rl_draw`'s
own consumer of this word and remain OPEN (§11 item 16)** — the boundary this project has kept
throughout U2/U3/U4. *(Resolved by U6: §15.3 (decoder), §15.5 (depth), §15.6 (the three mask modes, including the stencil clear's own record and replay).)*

### 13.5 `render_mode` — the full five-value enumeration, game-confirmed

The game handles **all five** shipped `render_mode` values (`spec-lua-bindings.md` §9.2a), not just
`filter`, each selecting a distinct 4-bit blend code in the state word (modes 0/1 only — mode 2 forces
the code to 0 regardless):

| `render_mode` name | Value | Blend code |
|---|---|---|
| `filter` | 1 | `0x5` |
| `additive` | 2 | `0x1` |
| `additive_alpha` | 3 | `0x2` |
| `subtractive` | 4 | `0x9` |
| `multiply` | 5 | `0xc` |

`default` (0) never reaches the game as an effective value — the walk already resolves it to the
nearest non-`default` ancestor, ultimately the document's own seed value, which is `filter` (the
document-begin callback sets the context to mode `1`/`filter`, and the base state word's own blend
bits independently default to `filter`'s own code — both seed and base agree). A value above 5 leaves
the previous blend code in place rather than erroring. **These are five genuinely different blend
selections a reimplementation must support**, not just plain alpha blending; the exact source/
destination blend factors each one selects are, again, inside `rl_draw` and OPEN. *(Resolved by U6, §15.4: `filter` = SRCALPHA/INVSRCALPHA/ADD, `additive` = ONE/ONE/ADD, `additive_alpha` = SRCALPHA/ONE/ADD, `subtractive` = ONE/ONE/REVSUBTRACT, `multiply` = DESTCOLOR/ZERO/ADD.)*

### 13.6 Cross-observations

- **One resource-to-callback idiom, three widths.** `bitmap` (§12.1, 5 arguments), `bitmap_circle`
  (§13.2, 8) and the dead `gradient` path (§13.1, 13) all share one helper shape: reject a `−1`
  texture, shift `source_nw` into page coordinates for a sheet-packed image, pass `source_se` through
  untouched as an extent. A port can implement one "textured shape from image + source rect" primitive
  with three shapes — quad, pie, and (unused) per-corner-coloured quad.
- **Colour is state, with one designed exception that never runs.** Every reachable primitive
  (`bitmap`, `text`, `bitmap_circle`) sets the current colour through `+0x04` and the game reads it
  back per vertex; only the unreachable `+0x28` body carries per-vertex colours. **In the shipped game,
  every Vint primitive is uniformly coloured.**
- **The colour cache stays coherent within the UI's own callbacks** (every state rebuild re-sends it),
  **except the minimap, which calls the raw colour setter directly** — the one known way the cache
  could go stale (§11 item 14; `sr2_map` stays parked).
- **Rounding conventions differ by site and a bit-exact port must copy each**: `source_*` setters and
  the clip box in document space round half-away-from-zero; the clip box's device-space near edges
  truncate while its far edges round half-away-from-zero; colour bytes truncate (×255) everywhere
  except the `+0x04` body's own re-quantisation, which rounds to nearest.

---

## 14. Text: glyph drawing, layout, markup (unit U5)

**Origin:** the text unit of the scoping note's order (its "U6", `review/scoping-ui-render.md` §4 item 7); numbered U5 here because U4 (§13) was taken in parallel. The font file, the game's font tables, the Vint font pool and the metric callbacks are a separate new spec, **`spec-vint-font-format.md`**; this section covers what turns a `text` element's string into the 4-opcode stream of §12.2 and what the game does with each glyph run. Method: the same capstone-based direct read as U1–U4, plus **emulated calls of the real shipped functions** (the project's `unicorn` harness, `tools/harnesses/unicorn_call.py`) on the real `font_body.vf3_pc`, with the game-side metric callbacks also run for real — so the layout results below are the shipped code's own output, not a reading of it. Results reproduced that way are marked *emulated*. Text property names are those of `spec-lua-bindings.md` §9's `text` table; their element offsets are in §14.2.

### 14.1 Per-glyph geometry — the game's glyph-run drawer `0x00b9bc10` (closes §11 item 11 with `spec-vint-font-format.md` §4.3)

Read in full this pass and run under emulation on real font records. Inputs (§12.4's `+0x2c` path): pen origin `p`, the 2×3 glyph matrix `G = M·S(sx, sy)` split into two rows, the UTF-16 text and count, the game font id, the draw context, and the signed `spacing` byte. With `f` = the font record (`spec-vint-font-format.md` §2.1), `s` = the derived-font scale (1 for a plain font), `dy` = the derived vertical draw offset (0 for a plain font), `T` = the texture size in texels:

- **The pen** starts at 0 in glyph space and moves only along glyph-space x. Each character advances it by the font metric (`spec-vint-font-format.md` §6: glyph advance + `f.+0x1c` + kerning with the next character, × `s`, truncated) **plus the `spacing` byte** (not multiplied by `s`, but carried through `G` like everything else). CONFIRMED, *emulated* (`AB` in `font_body`: `B` starts at 18; with spacing +5 at 23, with −2 at 16).
- **The quad** for a character in the font has its top-left at glyph-space `(pen + f.+0x26·s, (f.+0x24 + dy)·s)` and size `(w, f.+0x16·s)`, with `w` = trunc(glyph width × `s`); its corners go through `G` and are offset by `p`. CONFIRMED, *emulated* (`font_body` `A`: (0,0)–(21,36); `font_header` `A`: (−1,−20)–(56,80); a scale-2, offset-3 derived `font_header`: (−2,−34)–(112,166)).
- **UVs, and where U3's half-texel constants go:** with `(tx, ty)` from the font's texel tables, `u0 = (tx + 0.5)/T.w`, `u1 = (tx + w/s)/T.w`, `v0 = (ty + 0.5)/T.h`, `v1 = (ty + f.+0x16)/T.h`. **The `0.5/w`, `0.5/h` constants are a half-texel inset on the near (top-left) edge only**; the far edge is the exact glyph boundary. CONFIRMED, *emulated* (`font_body` `A`: u 60.5 → 81 texels, v 40.5 → 76, for any texture size supplied). The quad path of §12.4 (`+0x20`) has no inset at all; only glyphs get one. *(U6, §15.7: both paths are then shifted a further half texel by the pixel shader, so a glyph's near edge is sampled a whole texel in.)*
- **Vertices:** 6 per glyph (corners 0, 1, 2 then 2, 1, 3), 28-byte stride; position at `+0x00`/`+0x04`, UV at `+0x10`/`+0x14`, colour at `+0x18` (the current colour, read back and packed b, g, r, a as in §12.4); `+0x08`/`+0x0c` untouched. Batches of at most 64 glyphs (*emulated*: 70 glyphs → 64 + 6).
- **Characters below U+0020** (tab, CR, LF included) give a zero-size quad at the pen and **no advance**. **A NUL code unit ends the run** even when `count` is larger (the remaining reserved vertices become zero-size quads at the last pen position) — which is why §12.2's odd-length padding is harmless. **A character the font lacks** gets a zero-width, full-height quad and the font's **default advance** (`f.+0x10 + f.+0x1c`, × `s`). All CONFIRMED, *emulated*. That advance disagrees with layout, which measures such a character as 0 wide (`spec-vint-font-format.md` §6).
- **`G`'s third column must be zero.** The drawer applies it as though multiplied by `(x + y)` rather than by 1 (*emulated*: a first row of (1, 0, 7) moves the corner (21, 0) to x = 168). It is always zero in practice (`G = M·S`, and `M`'s translation column is 0, §4.1); this matters only to someone feeding the drawer directly.
- **A one-entry repeat cache** (`0x00b9b7e0` checks, `0x00b9ba20` stores, at most 384 characters): drawing the same text, count, font, spacing and matrix again re-sends the previously built vertices, shifted by the change in `p`. A speed-up only; the output is identical.
- **Two layers** (§12.4's two sub-font ids): the secondary is drawn first, then the primary if it differs. What ships makes this one draw (or, in three language arrangements, two identical draws in place) — never an outline/fill pair. Full account: `spec-vint-font-format.md` §4.3.

### 14.2 From properties to a string — the stream builder `0x00e31c50` and the insertion formatter `0x00e22b30`

**Where the layout inputs live** (element offsets, read directly from the accessor bodies this pass — CONFIRMED; the layout parameter block handed to layout is element `+0xa0`, so a block offset is the element offset − `0xa0`):

| Property | Element field | Notes |
|---|---|---|
| `text_tag` | `+0xf4` key hash, `+0xf8` heap copy of the raw string | a string sets both (the hash through the game, table `0x02a5a014` slot `+0x08`); an integer sets only the hash and frees the copy. **[Completed 2026-10-03, `spec-localization-format.md` §6.3/§7 item 2: slot `+0x08` (game `0x00849fa0`) returns the real hash only if that string is a key in some loaded table — otherwise it returns `0`.** So for the common case of a plain, non-key `text_tag` string, `+0xf4` ends up `0` and the raw `+0xf8` string is what actually draws (§14.2 step 2) — this is why ordinary placeholder/English `text_tag` text works at all, not a fallback edge case.] |
| `text_tag_crc` | `+0xf4` | frees the raw copy |
| `insert_values` | `+0xfc`/`+0x100` | the builder passes `+0x100` and a fixed count of 32 when it is set |
| `font` | `+0xcc` | §12.2 |
| `text_scale` | `+0xa8`/`+0xac` | §12.2 |
| `word_wrap` | **bit 0 of byte `+0x10c`** (bit 1 is the layout-dirty flag) | default off |
| `wrap_width` | `+0xf0`, float | **default 300**; the setter raises any value below 20 to 20 (a NaN is kept) |
| `horz_align` | `+0xd0` | 0 `left`, 1 `center`, 2 `right` |
| `force_case` | `+0xd4` | **has no effect** — see below |
| `leading` | `+0xe4`, **signed byte** | setter: ≤ −127 → −127; anything ≥ 128 is stored as the byte `0x80`, which layout reads as **−128** |
| `kerning` | `+0xe5`, signed byte | same clamp as `leading` |
| `line_frame_enable` | `+0xe7` | default off |
| `line_frame_w`/`_m`/`_e` | `+0xc0`/`+0xc4`/`+0xc8` | bitmap-resource handles (§12.1's pool) |
| (effective wrap width) | `+0xbc` | written by the builder before every layout: `wrap_width` if `word_wrap` is set, else 0 |
| (measured size) | `+0xe8`/`+0xec` | written by layout; copied into `unscaled_size` (§12.2) |

Layout-related defaults from the constructor/initialiser (`0x00e319a0`/`0x00e31940`): 0 / off / `none` / `left`, except `wrap_width` 300 and `text_scale` (1, 1).

**`force_case` is dead in this build — CONFIRMED.** Its setter stores it and marks layout dirty, and layout copies it into its working state, but nothing in layout, the run builder, the tag handlers, the stream player or the glyph drawer ever reads that copy, and no other reader of element `+0xd4` exists besides the property's own getter. *Emulated:* `Hello world` with `force_case` = `upper` (1) or `lower` (2) produces exactly the stream it produces with `none`. The 181 shipped documents that set it (`spec-lua-bindings.md` §9) get no visible change from it. A replacement should **not** change case.

**Building the string, in order — CONFIRMED (`0x00e31c50`):**
1. A readiness check through the element's vtable `+0x20` must pass; the dirty bit is cleared; no font → the stream is freed and nothing draws.
2. **Source text.** If the key hash `+0xf4` is non-zero, the localised string for it is fetched from the game (`0x00e227c0` → table `0x02a5a014` slot `+0x04`). **[Completed 2026-10-03, `spec-localization-format.md` §6.1, which specifies the game-side function this step calls: slot `+0x04` is `0x00849950` — hash 0 returns null at once (no search); otherwise it searches up to 14 loaded string tables in load order, first match wins, no cache. A miss there, or `+0xf4` = 0, falls through to the raw string below.]** If that fails or there is no hash, the raw `+0xf8` string is used: decoded with the tagged-string decoder `0x00e22ac0` (`spec-lua-api-behaviour.md` tranche 09, §44.4 — not re-derived) or, if it is not in that tagged form, widened byte-for-byte (`0x00db06e0`; HIGH). Neither → the stream is freed.
3. **Insertion** through `0x00e22b30` into a 4,096-unit buffer (below).
4. Effective wrap width into `+0xbc`; then **layout** (§14.3).

**The insertion formatter's syntax — CONFIRMED (read in full; the vectors are *emulated* calls of the real function with no game tables installed):**

| Written | Becomes |
|---|---|
| `{{` | `{` |
| `%%` | `%` |
| `%s` / `%S` | the next value in order, as text (a nil value inserts nothing) |
| `%` + any other character | that character (the `%` is dropped): `p%dq` → `pdq` |
| `{}` | the next value in order |
| `{N}` (decimal; spaces/tabs allowed inside the braces) | value `N`. **An index at or beyond the value count inserts nothing**, so with no `insert_values` at all every `{N}` vanishes (`{0} and {1}` → ` and `). A nil value inserts the literal `{No insertion value specified!}`. A string value is first tried as a localisation key (game lookup) and used as plain text if that fails; other types are converted to text (`0x00e23f40`) |
| `{N:text_tag_crc}` | value `N` is a key hash to look up: a non-integer value gives `{Invalid insertion value for a hash!}`, an unknown hash `{Hash not found!}` |
| `{N:not_localized}` | value `N`'s string inserted as-is, no lookup |
| `{NAME}` (starting with a non-digit) | a game-side named symbol (table `0x02a5a014` slot `+0x0c` → game `0x008437a0`; HIGH: input/control names), else the literal `{Symbol not found!}` |

Inserted text is not re-scanned for `{`, but it **is** subject to layout markup (§14.4) — this is how button glyphs arrive as `[image:…]` tags. The modifier keywords are compared case-insensitively against the table at `0x0132bd78`.

### 14.3 The layout algorithm — `0x00e36dd0` with the run builder `0x00e36070` and the segment dispatcher `0x00e369f0`

Read in full; every rule below was also reproduced by *emulated* layout of the real code on the real `font_body` metrics.

**Working state.** A **style stack** of 16 entries × 24 bytes (`scale x`, `scale y`, `r`, `g`, `b`, `a`); entry 0 starts as (`text_scale.x`, `text_scale.y`, −1, −1, −1, −1). A **line table** (16 bytes per line: width, height, x offset, y offset; up to 1,024 lines) and a **record table** (up to 1,024 entries: pointer to an emitted record's x/y, its line, its height) used for the final placement pass. Layout calls only two font metrics: the line height and the per-character advance (`spec-vint-font-format.md` §6, slots `+0x1c`/`+0x20`).

**Segmenting.** At a `[` that is not followed (after spaces) by another `[`, a **tag** is processed (§14.4). Anything else is handed to the **run builder**, which emits one glyph-run record (opcode 2) holding as many characters as it consumes, with `x` = the pen, `y` = 0, `sx`/`sy` = the current style entry's scale, `spacing` = the `kerning` byte, and **registered height = (line height + `leading`) × style `sy`**.

**Per character, in the run builder:**
- **NUL** — end of text; ends the current line.
- **LF**, and the two-character sequences **`\n`/`\N`** (a real backslash followed by `n` or `N`) — line break, consumed. CR, tab and other control characters are ordinary characters (they measure 0 and draw nothing, §14.1).
- **`[[`** — one literal `[`. A lone `]` is ordinary text.
- **space** — a break opportunity *at* the space; if a wrap happens there the space is dropped.
- **`-`** — a break opportunity *after* the hyphen (it stays on the first line).
- **"break anywhere" fonts** (the Vint font pool's flag, set when the language code is 3, `spec-vint-font-format.md` §5/§6) — every character is a break opportunity, and spaces are not dropped.
- **everything else**, including the above once accepted — advance = (font advance with kerning against the next character + the `kerning` byte) × style `sx`; the pen moves by it.

**Wrapping.** If the effective wrap width is above 0 and placing the character would take the pen **strictly past** it (pen + advance > wrap; an exact fit stays), the line breaks at the most recent break opportunity on the line, or — if there is none — **before the current character**, splitting the word. The first character of each run is never tested, so a glyph wider than the wrap width still gets placed (one per line). After a break the new line begins exactly where the old one stopped: **leading spaces are kept**, only the one space at the break point is dropped, and spaces before it stay on the old line and count toward its width. CONFIRMED, *emulated* (`The quick brown fox jumps over the lazy dog`, wrap 200 → `The quick brown` / `fox jumps over` / `the lazy dog`; `AB` at wrap 34 stays on one line, at 33 splits; `Supercalifragilistic` at wrap 50 → `Sup` / `erc` / `alifr` / `agil` / `istic`).

**Lines.** An empty line (two line breaks in a row) is given a single space so it has height. A trailing line break at the very end adds no line. At each line end the trailing `kerning` byte is removed from the line's width, and the line-frame pieces are finished (§14.6).

**Final placement — CONFIRMED (code and *emulated*):**
1. Each line's **height** = the largest registered height of any record on it (text runs as above; inline images their own height × `sy`; line-frame pieces 0).
2. The text block's **size** = (widest line's width, sum of line heights). It is written to `+0xe8`/`+0xec` and becomes `unscaled_size`.
3. Each line's **x offset** by `horz_align`: `left` 0, `center` (block width − line width) / 2, `right` block width − line width. **Alignment is within the widest line, not within `wrap_width`** — a wrapped, centred paragraph is only as wide as its longest line, and the element's `auto_offset`/anchor then place that block. Trailing spaces kept on a line count toward its width and so shift its alignment (*emulated*: `ab␠␠` / `cd` centred → `cd` offset 5.5, not 0).
4. Each line's y offset = the sum of the heights above it.
5. Every record is moved by its line's x offset, and **vertically centred on its line**: `y += line y + (line height − record height) / 2`. With uniform text this is just the line's top; a taller element on the line (a `[scale:]`d run or an image) pushes everything else down to the shared middle (*emulated*: `a[format][scale:2.5]b[/format]c` → line height 90, `b` at y 0, `a`/`c` at y 27).

**Stream end.** If at least 4 bytes remain, a zero dword is appended and **counted in the stream length**; the player (§12.2) stops at it silently as an unknown opcode. CONFIRMED (*emulated*: `Hello` → one 44-byte run record (4-byte opcode, 28-byte header, 6 code units including the padding NUL) + the 4-byte terminator = 48 bytes; an empty string → just the 4-byte terminator).

**Limits.** 12,800 stream bytes (§12.2); 1,024 lines and 1,024 placed records (HIGH: the capacities layout initialises its tables with; what happens past them was not tested). **Running out of stream space is not a clean truncation** — in contrived *emulated* tests a too-small buffer produced a garbled stream, and with a colour tag pending the layout never returned (the instruction cap was hit). The longest shipped English string is 371 characters (about 1 KB of stream), so shipped text never gets near; a replacement should simply truncate.

**Emulated reference vectors** (real code, real `font_body` metrics, `text_scale` (1, 1), no tags unless shown; runs listed as `x, y, text`; sizes as width × height):

| Input | Stream | Size |
|---|---|---|
| `Hello` | run 0, 0, `Hello` | 64 × 36 |
| `Hello` LF `World` | run 0, 0, `Hello`; run 0, 36, `World` | 71 × 72 |
| `Hi` LF `Hello`, `center` | run 19.5, 0, `Hi`; run 0, 36, `Hello` | 64 × 72 |
| `Hi` LF `Hello`, `right` | run 39, 0, `Hi`; run 0, 36, `Hello` | 64 × 72 |
| `A` LF `B`, `leading` +10 | runs at y 0 and 46 | 18 × 92 |
| `A` LF `B`, `leading` +10, `text_scale` (1, 2) | runs at y 0 and 92 (leading is scaled too) | 18 × 184 |
| `AB`, `kerning` +3 | run 0, 0, `AB`, spacing 3 | 37 × 36 |
| `AB`, `kerning` +3, `text_scale` (2, 1) | as above, scale (2, 1) | 74 × 36 |
| `ab` LF LF `cd` | runs `ab`, a single space, `cd` at y 0 / 36 / 72 | 34 × 108 |
| `A`, U+4E00, `B` | one run of all three | **34** × 36 (the drawer will place `B` 11 further right, §14.1) |

The run records' text includes the odd-length padding NUL of §12.2 where it applies.

### 14.4 Markup tags — the complete set

The tag table at `0x0132d3f0` has **exactly five** entries (count `0x0132d42c` = 5). A tag is `[`, optional spaces, a name matched **case-insensitively as a prefix** of the text that follows (so `[FORMAT]` and even `[formatting]` act as `[format]`), arguments, `]`. CONFIRMED (table dump; matcher `0x00e35c20`; *emulated*).

| Tag | Handler | Effect |
|---|---|---|
| `[format]` | `0x00e35d20` | **push**: copies the current style entry into the next slot and makes it current. Emits nothing |
| `[/format]` | `0x00e36550` | **pop** (only if not already at entry 0), then **always emits a colour record** (opcode 4) with the now-current entry's colour |
| `[color:…]` | `0x00e367a0` | **overwrites the current entry's colour in place** (no push) and emits a colour record with it. Argument forms below |
| `[scale:N]` | `0x00e35da0` | sets the current entry's scale to **(N, N) — absolute, replacing `text_scale`**, not multiplying it (*emulated*: `[scale:2]` under `text_scale` 3 draws at 2). `N` is decimal digits with an optional `.` fraction. Emits nothing itself |
| `[image:NAME]` | `0x00e365b0` | an **inline image** (opcode 3): `NAME` (up to 63 characters, each UTF-16 unit truncated to its low byte) is looked up in the **bitmap-sheet** directory (`0x00e22290`) — only sheet-packed images can appear inline. Placed at the pen, `y` 0, scale = the current entry's scale; the pen advances by width × `sx`; registered height = image height × `sy`. If it would overrun the wrap width and the line is not empty, the line breaks first and the image starts the next line. **An unknown name still emits a record**, with sheet index −1 (and, HIGH, size 0 and no advance — the size query for index −1 is answered by the game, which the emulation stood in for); the player skips it (§12.2) |

**Colour arguments — CONFIRMED (*emulated*):**
- **`#RRGGBB`** — explicit r, g, b (each pair ÷ 255); **alpha copied from the current entry**, i.e. −1 (inherit) unless something earlier set one.
- **`#RRGGBBAA`** — explicit r, g, b **and explicit alpha**.
- Fewer than three complete hex pairs (e.g. `#FFF`) — **ignored entirely**: no colour change, no record.
- **A name** — looked up in the game's named-colour registry (`0x00e22730`, 64-entry table at `0x02a59c00`, filled at UI start-up by `0x00843090` from the compile-time list at `0x01301860`): explicit r, g, b; alpha copied from the current entry. Matching is case-insensitive and compares only as many characters as the tag gives, so **a prefix matches the first registered name it starts** (`gre` → `green`, *emulated*). An unknown name is ignored entirely.

The 26 registered names and their values (r, g, b as stored; hex is the nearest byte):

| Name | Hex | Name | Hex | Name | Hex |
|---|---|---|---|---|---|
| `kill` | `#CC2200` | `syndicate` | `#D04A92` | `pink` | `#DB00DB` |
| `use` | `#F6BB4F` | `red` | `#CC2200` | `orange` | `#F5961F` |
| `revive` | `#6864EE` | `yellow` | `#F6BB4F` | `grey` | `#828282` |
| `defend` | `#65B24C` | `purple` | `#9400C5` | `gold` | `#EBD96E` |
| `location` | `#199DE5` | `green` | `#65B24C` | `silver` | `#C4C4C4` |
| `coop` | `#9400C5` | `blue` | `#199DE5` | `bronze` | `#A8612E` |
| `em` | `#680788` | `teal` | `#199DE5` | `mp_friend` | `#33A8FF` |
| `saints` | `#9400C5` | `cyan` | `#199DE5` | `mp_foe` | `#CF1F1F` |
| `highlight` | `#D9BF1F` | `full_bright` | `#FFFFFF` | | |

**Anything else in brackets** is shown, not hidden: the bracket contents are formatted as `[[unrecognized: '…']` (format string `0x0125a8dc`) and laid out as text — which renders `[unrecognized: '…']` — and **a line break follows it** (the inserted text's own end-of-string ends the line). CONFIRMED by reading and *emulated* (with only the formatting call itself supplied by the harness): `a[bogus]b` → `a[unrecognized: 'bogus']` on one line, `b` on the next. The only such tag in the English strings is the empty `[]` inside a character-set test string.

**`[format]` nesting overflow.** The stack has 16 entries, but `[format]` at entry 15 still copies into a 17th slot, which is the stack's own index field: the index is overwritten with a scale value and then incremented. Seventeen nested `[format]`s corrupt layout (*emulated*: the following text moved to x = 1.0). **Shipped English strings never nest deeper than 1**, so this is a hazard for new content only; a replacement should cap the depth.

**Shipped usage (the 13 English `*_us.le_strings` files of the 117 localisation files in `misc.vpp_pc`, scanned as UTF-16 text):** 719 `[/format]`, 718 `[format]`, 711 `[color:…]`, 75 `[image:…]`, 70 `[scale:…]`. Colour arguments: `location` 224, `green` 79, `kill` 76, `use` 65, `red` 61, `full_bright` 60, `em` 57, `teal` 25, `defend` 22, `blue` 12, `yellow` 7, `purple` 4, `Purple` 3, `#edc300FF` 3, `orange` 2, `saints` 2, and one each of `grey`, `bronze`, `gold`, `silver`, `#AAAAAA`, `revive`, `#9400c5`, `#9400C5`, `highlight`. 710 of the 711 colour tags sit inside an open `[format]` (648 directly after it, 60 after a `[scale:]` — e.g. `[format][scale:0.9][color:full_bright][image:ui_ctrl_360_btn_a][/format]`, the PC button icons — and 2 later in a span); the one bare tag is in a PS3-only string (`[color:yellow]MOTION SENSOR CONTROL`), and one English string has a `[/format]` with nothing open.

### 14.5 Markup colours and inherited tint/alpha (closes §11 item 10) — the correctness answer

**Yes: shipped markup does write explicit colours, and the stream player (§12.2) makes every explicit component *replace* the element's inherited value rather than multiply it.** CONFIRMED by reading the colour handler and the player, and *emulated* end to end:

1. **Every `[color:…]` writes explicit r, g, b.** All 711 shipped colour tags therefore **ignore the element's composed tint** (its own `tint` × every ancestor's, §4.1) for the text they colour. A parent tint animation — a red flash, a dimming — does not reach coloured spans; the uncoloured text around them does follow it. *Emulated:* `a[format][color:red]b[/format]c` → `a` inherits, colour record (0.8, 0.134, 0, −1), `b`, colour record (−1, −1, −1, −1), `c` inherits.
2. **Alpha stays inherited (−1) for names and `#RRGGBB`** — so a parent's **fade does still reach** those spans. This covers 708 of the 711 shipped tags.
3. **`#RRGGBBAA` writes an explicit alpha, and that span escapes every parent fade.** It ships: **`[format][color:#edc300FF]{0}[/format]`** — the opening of the PC, PS3 and Xbox 360 "using cheats prevents you from earning achievements/trophies and autosaves" warnings, in every language (27 strings; one per platform file in English). Alpha `FF` = 1.0, so whatever `{0}` inserts (HYPOTHESIS: the cheat's name) is drawn **fully opaque even while a parent element is part-way through a fade** (CONFIRMED as a mechanism; whether this dialog actually fades was not checked). *Emulated:* `[format][color:#edc300FF]x[/format]y` → colour record (0.929, 0.765, 0, **1.0**) before `x`, then (−1, −1, −1, −1) before `y`. The cull (§3) still hides it completely once the inherited alpha reaches 0 — it is the partial fades in between that differ.
4. **The shadow pass is unaffected** (§12.2: it ignores colour records): a coloured span's shadow keeps the shadow colour and the inherited alpha, so under a `#RRGGBBAA` span the shadow fades while the body does not.
5. **A `[color:]` with no `[format]` around it changes entry 0 itself**, so the colour holds to the end of the string, and a stray `[/format]` (nothing to pop) re-emits that same colour rather than restoring inheritance (*emulated*). In the English strings this happens once, in a PS3-only string (§14.4); every other colour tag is inside a `[format]`.
6. **Inline images take the current colour** (§12.4): `[color:full_bright]` before a button icon makes it pure white with tint ignored and alpha inherited.

**For a replacement:** an explicit component from markup must override, not modulate; −1 means "use the composed value". Doing the obvious multiply instead would make every coloured HUD/menu span follow tints it ignores in the original, and make the cheats-warning span fade when the original does not.

### 14.6 Line frames (closes §11 item 12)

CONFIRMED by reading (`0x00e35f20`, `0x00e36bd0`, `0x00e35fc0`) and *emulated* with three sheet-packed images of known size.

- **Handle → sheet index.** When `line_frame_enable` is on, each of `line_frame_w`/`_m`/`_e` is resolved through the bitmap-resource pool (§12.1) and its record `+0x08` is taken as the sheet index, with the image size from `0x00e30360`. **The resource's mode is not checked**: `+0x08` is the sheet-record index only for a sheet-packed (mode 1) resource, so a stand-alone (mode 0) image cannot work as a line frame (HIGH: it would draw whatever sheet record that field happens to name). An unset or unresolvable handle leaves that piece out (−1).
- **Placement**, per line, with `sx`/`sy` the current style scale and `mid` the line's vertical middle (pieces register height 0, so they never change the line's height, and they are centred on `mid` by the final placement pass, §14.3):
  - **W** (`_w`): emitted at line start, `x = −W.width × sx` — **entirely left of the line, outside the content box** — `y = mid − W.height × sy / 2`, scale (`sx`, `sy`).
  - **M** (`_m`): emitted at line start, finished at line end: `x = 0`, `y = mid − M.height × sy / 2`, **horizontal scale = line width / M.width** (stretched across exactly the line's measured width, independent of `sx`), vertical scale `sy`.
  - **E** (`_e`): emitted at line start, finished at line end: `x = line width`, `y = mid − E.height × sy / 2`, scale (`sx`, `sy`).
  - All three are emitted **before** the line's text, so they draw underneath it, and they shift with the line's alignment offset.
  - *Emulated* (W 10 × 40, M 4 × 30, E 12 × 50; `Hello`, line 64 × 36): W at (−10, −2), M at (0, 3) with horizontal scale 16, E at (64, −7).
- **They do not enter `unscaled_size`**, and W hangs outside it.
- Still exercised by no shipped document (`line_frame_enable` has 0 records, `spec-lua-bindings.md` §9) — live only if Lua turns it on.

### 14.7 What a replacement has to do for `text`, in short

1. Resolve the font through the named table (`spec-vint-font-format.md` §4); measure with its primary font only.
2. Build the string: localised text by key hash, else the raw string; then the insertion syntax of §14.2.
3. Lay out exactly as §14.3 — break rules, `\n` sequences, kept spaces, the "first character of a run never wraps" guarantee, alignment within the widest line, vertical centring on each line, size = (widest line, total height). Ignore `force_case`.
4. Markup per §14.4; colours per §14.5 (**explicit replaces, −1 inherits**; `#RRGGBBAA` escapes fades).
5. Draw glyphs per §14.1, including the near-edge half-texel inset; draw `_overlay` fonts once.

---

## 15. The game's 2-D draw layer: `rl_draw`, the primitive renderer, and the Direct3D 9 state the UI actually sets (unit U6)

**Origin:** the boundary every earlier unit stopped at (`spec-render-pipeline.md` §2's note; §11 item 16 here), opened because a Direct3D 11 replacement needs the exact blend, stencil and sampler behaviour rather than a plausible one. Method: the same Ghidra-free capstone read of `SaintsRowTheThird.exe` as U2–U5, plus RTTI decoding of every object involved, plus the two UI shaders read from `shaders.vpp_pc` with the project's own container reader (`tools/harnesses/vpp_modea.py`, `fxo_parse.py`) and D3D9 bytecode lister (`sm_disasm.py`) — the shader instruction set is Microsoft's public format. Every address below was read this pass; labels as usual.

### 15.1 Who is who — four objects and one thread-local block

| Global | Live class (RTTI read this pass) | Role for the UI |
|---|---|---|
| `0x0351f91c` | **`rl_draw`** (vtable `0x0125b0c0`; created at `0x00e4c210`, an 8-byte object) | The entry points the UI callbacks reach (`0x00e4ae90` and neighbours). **It holds no drawing state of its own** — every entry reads the calling thread's block (below). |
| `0x0351f918` | **`rl_d3d_primitive_renderer`** (vtable `0x012a1024`), derived from `rl_primitive_renderer` (base vtable `0x0125b190`) | Batching, vertex staging and the actual draw. Created at `0x0048d190`, which runs the base constructor `0x00e537b0` (that one stores the singleton pointer) and then installs the derived vtable. The base vtable's slots `+0x28`/`+0x2c` are pure-virtual traps; the live ones are `0x0048d410` (allocate) and `0x0048d540` (draw). |
| `0x0351f8f8` | **`rl_d3d_renderer`** (vtable `0x0129fddc`), derived from `rl_renderer` | Clear (slot `+0x40` = `0x004780f0`), shader constants by engine id (`+0x44` = `0x00477160`, four floats; `+0x48` = `0x00477120`, a pointer), texture binding (`+0x54` = `0x004771f0`). Created at `0x00477620`, which runs `rl_renderer`'s constructor `0x00e38350` and then installs the derived vtable — see the note added to `spec-render-pipeline.md` §21. |
| `0x0351f914` | not identified this pass | Vertex-declaration registry: its `+0x14` builds a declaration from an element table, `0x00476f10` binds one by index (cached in the state manager's `+0x884`). |

**The thread-local block** (the module's TLS block, indexed through `0x02cc4700`), as far as this layer uses it:

| Offset | Holds |
|---|---|
| `+0x1c` | the game-side UI draw context of §12.4/§13.4 (`0x00846f10` creates it on first use) |
| `+0x674` | the current **`rl_d3d_state_manager`** (vtable `0x0129fcc4`, derived from `rl_state_manager`) — current colour, texture, scissor and the command buffer every record goes into (§15.8) |
| `+0x67c` | the material-bind cache used by `0x00e3f740` (last material bound, so a repeat bind is skipped) |
| `+0x684` | the primitive renderer's per-thread **batch block** (§15.2, §15.8) |

All CONFIRMED.

### 15.2 Submission: begin / reserve / end, and what reaches the device (closes the "how are quads submitted" question)

**The game-side wrappers** each load `rl_draw` and forward:

| Wrapper | `rl_draw` entry → body | Effect |
|---|---|---|
| `0x005d91a0(&rgba8)` (the colour setter's back end, §12.4) | `0x00e4ae90` → `0x00e82d30` | bytes × 1/255 → four floats, stored as the state manager's current colour (its `+8` sub-object, `+0xd8`…`+0xe4`). **No GPU work.** |
| `0x005d9090(&r,&g,&b,&a)` | `0x00e4aec0` → `0x00e82d50` | reads that colour back (how every body gets its vertex colour). |
| `0x005d8ff0(texture)` | `0x00e4aef0` → `0x00e485e0` | stores the texture handle if it changed; a real handle (not −1) also clears the three-handle Bink binding and the material handle. **No GPU work.** |
| `0x005d9c30(&word)` | `0x00e4be40` | begin a **triangle-strip** batch |
| `0x005d9c80(&word)` | `0x00e4bf30` | begin a **triangle-list** batch |
| `0x005d9c50(n)` | `0x00e4bf00(n−2)` → `0x00e531a0(n)` | reserve `n` strip vertices; returns the write pointer |
| `0x005d9ca0(n)` | `0x00e4bff0(n/3)` → `0x00e531a0(3·⌊n/3⌋)` | reserve list vertices (rounded down to whole triangles) |
| `0x005d9c70()` | `0x00e4bf20` → `0x00e52210` | end: draw the batch |
| `0x005d92e0(&word)` | `0x00e4c200` → `0x00e525e0` | apply a state word on its own — used once per frame by the UI frame function (`0x00848e79`) with the document base word at `0x0132d488` |

`0x005d9c20`, listed with this family by earlier units, is **not an entry point**: its bytes are the tail of the preceding function and nothing calls it or stores its address. CONFIRMED.

**Primitive codes.** Begin chooses one of 22 codes from what is currently bound: a strip begin gives 8 (no texture), 9 (texture), `0xc` (all three Bink handles bound) or `0xd` (a material handle bound); a list begin gives `0xf`/`0x10`/`0x13`/`0x14` by the same rule. The Bink triple wins over the texture, which wins over the material handle. Each code fixes the primitive type (table `0x01351090`), the vertex stride (`0x013510e8`) and the shader (`0x00e53794` → five materials built at start-up by `0x00e52ec0` from the shader names `rl_prim_2d`, `rl_prim_2d_tex`, `rl_prim_3d`, `rl_prim_3d_tex`, `rl_prim_2d_bink`; codes `0xd`/`0xe`/`0x14`/`0x15` use the bound material instead):

| Codes | Primitive | Stride | Shader |
|---|---|---|---|
| 0–3 | line strip | 28, 28, 24, 24 | 2d, 2d_tex, 3d, 3d_tex |
| 4–7 | line list | same pattern | same pattern |
| 8–11 | triangle strip | same pattern | same pattern |
| 12 / 13 / 14 | triangle strip | 28 / 28 / 24 | 2d_bink / material / material |
| 15–18 | triangle list | 28, 28, 24, 24 | 2d, 2d_tex, 3d, 3d_tex |
| 19 / 20 / 21 | triangle list | 28 / 28 / 24 | 2d_bink / material / material |

**The Vint UI only ever produces codes 9 and `0x10`** (and 8/`0xf` when a texture handle is −1): `bitmap` and inline images strip-draw a textured quad (code 9), glyph runs and `bitmap_circle` list-draw textured triangles (code `0x10`). HIGH — follows from the bodies of §12.4/§13.2 (each sets a texture, then begins); the Bink and material codes are reached only from non-UI callers. **One consequence:** an inline image whose sheet lookup gives texture −1 (§12.4) is drawn as an *untextured* rectangle in the current colour (code 8) — HIGH, not exercised.

**Begin** (`0x00e530f0(code, &word)`): if the word is not −1 it is applied (§15.3); the batch block is reset (count 0, first offset −1), the code, word and stride stored.

**Reserve** (`0x00e531a0(n)`):
- Asks the derived allocator (`0x0048d410`) for `n × stride` bytes and **zero-fills them** — which is why the vertex fields `+0x08`/`+0x0c` the UI bodies never write are 0 (§12.4, §14.1).
- Within one begin/end, consecutive reserves are **merged into one draw** when the new block is contiguous with the last; strips are joined with degenerate vertices — the previous last vertex repeated (twice when the batch's vertex count so far is odd, once when even) and the new first vertex repeated once, so the triangles' winding parity survives — and line strips repeat one vertex. If the batch would exceed `0x3f00` bytes (16,128 — 576 vertices at 28 bytes) or the new block is not contiguous, the batch so far is drawn and a new one begun with the same code and word. CONFIRMED.
- A failed allocation returns null; the bodies then write nothing (the pie checks this explicitly). HIGH: an absurd `num_wedges` draws nothing rather than overrunning the staging buffer — the stack-table hazard of §13.2 is separate and stays.

**Staging** (`0x0048d410`): two CPU-side buffers in the renderer object — mode 0, a linear scratch area (base `+0x1d4`, cursor `+0x1d0`, size `+0x14`, rewound after every draw); mode 1, a ring (base `+0x1e4`, head `+0x1d8`, tail `+0x1dc`, size `+0x10`, outstanding count `+0x1e0` kept with interlocked operations; a full ring either waits or fails per the caller's flag). The batch block's `+0x08` picks the mode. **The game frame (`0x005d4260`) switches to mode 1 (`0x00e52340(1)`) and brackets the whole UI frame (`0x005d40b0` → `0x00848e40`) with the renderer's slots `+0x14` (`0x0048d2c0`, which also records the calling thread's id at `0x02e5d640`) and `+0x18` (`0x0048d2f0`, which hands the descriptor `0x02e5d630` to `0x0049c790` — HIGH: submits the recorded UI buffer)**. CONFIRMED except where marked.

**End** (`0x00e52210`): primitive count from the code's class (line strip `n−1`, line list `n/2`, strip `n−2`, list `n/3`), then the derived draw `0x0048d540(code, first offset, count, reserve count, mode)`, then reset (count −1).

**The draw** (`0x0048d540`), in order — CONFIRMED:
1. Vertex declaration by stride: 28-byte codes use element table `0x0132dd4c`, 24-byte codes `0x0132dd1c`. Each lists three elements; with the shaders' own declarations (§15.7) the 28-byte vertex is **position float4 at `+0x00`, texture coordinate float2 at `+0x10`, colour (4 bytes, b,g,r,a) at `+0x18`**, the 24-byte one the same with a float3 position (element-field meanings HIGH, from the strides and the shader declarations).
2. State-manager slots `+0x64(1)` and `+0x68(0)` — two flags at `+0x4ec`/`+0x4f0`; their effect is OPEN.
3. The code's material is bound through `0x00e3f740` — the same material-bind path meshes use (`spec-render-pipeline.md` §22); it records the shader's own sampler-state block (§15.7). Skipped when the same material is already bound.
4. Textured codes: shader constant **id 6 = (0.5 / texture width, 0.5 / texture height, 0, 0)** (`+0x44`), then the texture is bound to stage 0 (`+0x54`). (Bink codes bind three stages and use a vector form; not used by the UI.)
5. 28-byte codes: shader constant **id `0x1c` = (1 / viewport width, 1 / viewport height, 1, 1)** (`+0x48`), viewport from the state manager (`0x00e47fb0`).
6. **`0x00477390` → `0x004763d0`, which records opcode 41 (`0x29`) — `DrawPrimitiveUP` — with (primitive type, primitive count, stride, byte length) and the vertex bytes copied inline into the command buffer** (padded to 4). The staging block is released immediately afterwards. A draw counter is incremented.
7. A per-stage record through `0x00474ae0(0,0,0,0,1)` (not decoded) and, if flagged, `0x004760d0(0)`.

On the replay thread, opcode 41's handler (`0x0049d5d0`) first runs the render-state commit (`0x004ad930`, §15.3) and then calls `IDirect3DDevice9::DrawPrimitiveUP` (vtable `+0x14c`) on the inline vertices. CONFIRMED.

**So, for item 4:** the UI never locks a vertex buffer; every UI primitive becomes one `DrawPrimitiveUP` whose vertices travel inside the deferred command buffer of `spec-render-pipeline.md` §6. **The UI is a producer of opcode 41 — the first producer of that opcode this project has identified.** **Batching never crosses Vint primitives:** every body calls begin, one reserve, end — one draw per `bitmap` quad, one per pie, one per batch of at most 64 glyphs (two per run when a font has two sub-fonts, §12.4). A replacement is free to batch harder only if it keeps draw order and per-primitive state.

### 15.3 The state-word decoder `0x00e525e0`

| Bits | Selects | UI values |
|---|---|---|
| 0–3 | blend code (§15.4) | `5`/`1`/`2`/`9`/`0xc` by `render_mode`; `0` in mask-write mode |
| 4–7 | stage-0 texture addressing: `0` → wrap, `1` → clamp, anything else → untouched (§15.7) | always `0` |
| 8–11 | depth mode (§15.5) | `0`, or `1` with `background` |
| 12–15 | stencil mode (§15.6) | `0` / `2` / `5` (mask modes 0 / 1 / 2) |
| 16–19 | cull: `0` → none, `1` → counter-clockwise culling when the second cull key says so, else untouched | always `0` |
| 20–31 | alpha-test reference, used only by blend code 15 | `0x0ff` (unused) |

**How it is applied** — CONFIRMED. The decoder fills a render-state block on its stack — the same `0x510`-byte accumulator `spec-render-pipeline.md` §18.2 describes (up to 32 engine render-state key/value pairs through `0x00476f70`, up to 128 sampler entries through `0x0047aa50`, two dirty masks) — **always starting with three fixed entries: alpha function ALWAYS, two-sided stencil off, colour writes RGBA** — then appends the entries each nibble selects, in nibble order (later entries win). The block is recorded as **opcode `0x14`** through the state manager (`0x00474ea0` → `0x00475790`). It is **skipped** when the state manager's flag `+0x50a` is set and the batch block's `+0x30` already holds this word — i.e. when nothing else has recorded a state block since this same word was applied (the flag is cleared by every other block record, including the shader bind of §15.7).

**Engine render-state keys → Direct3D 9** (table at `0x01350828`, 35 entries of {render state, special flag}), CONFIRMED:

| Keys | `D3DRS_` |
|---|---|
| 0 / 1 / 2 / 3 | `ALPHABLENDENABLE` / `SRCBLEND` / `DESTBLEND` / `BLENDOP` |
| 4 / 5 / 6 | `ALPHAREF` / `ALPHATESTENABLE` / `ALPHAFUNC` |
| 7 / 8 / 9 / 10 / 11 | `ZENABLE` / `ZWRITEENABLE` / `ZFUNC` / `DEPTHBIAS` / `SLOPESCALEDEPTHBIAS` |
| 12 / 14 / 15 / 16 / 17 / 18 / 19 | `STENCILENABLE` / `STENCILFUNC` / `STENCILFAIL` / `STENCILPASS` / `STENCILZFAIL` / `STENCILREF` / `STENCILMASK` |
| 13 + 20 (special) | `STENCILWRITEMASK` = key 20's mask when key 13 is 1, else 0 — key 13 is a "stencil writes on" switch |
| 21 / 22 / 23 / 24 / 25 | `TWOSIDEDSTENCILMODE` / `CCW_STENCILFUNC` / `CCW_STENCILFAIL` / `CCW_STENCILPASS` / `CCW_STENCILZFAIL` |
| 26 + 27 (special) | `CULLMODE`, combined by `0x004ad290`: key 26 = 7 → `CCW` when key 27 is 1, else `NONE`; key 26 = 8 → `CW` when key 27 is 1, else `NONE`; any other key-26 value is sent as is |
| 28 / 29 / 30 | `FILLMODE` / `COLORWRITEENABLE` / `SCISSORTESTENABLE` |
| 31 / 32 / 33 / 34 | `SEPARATEALPHABLENDENABLE` / `SRCBLENDALPHA` / `DESTBLENDALPHA` / `BLENDFACTOR` |

**Engine sampler keys → `D3DSAMP_`** (table `0x01350940`): 0 `ADDRESSU`, 1 `ADDRESSV`, 2 `ADDRESSW`, 3 `BORDERCOLOR`, 4 `MAGFILTER`, 5 `MINFILTER`, 6 `MIPFILTER`, 9 `MAXANISOTROPY`, 11 `MIPMAPLODBIAS`, 12 `MAXMIPLEVEL`; 7, 8 and 10 are never sent. CONFIRMED.

**Replay.** Opcodes `0x14` (inline block, handler `0x0049d040`) and `0x15` (a block by pointer, `0x0049d0c0`) do not touch the device: they write pending values into a shadow (render states at `0x033958e0`, sampler states at `0x03395970` indexed stage × 16 + key) and OR dirty masks. **The device calls happen in the commit `0x004ad930`, which every draw handler (opcodes 40–43) and the Clear handler (opcode 11) run first**: on the first commit after start-up every state is sent; afterwards only values that differ from the last ones sent. The commit sends pixel-stage `MAGFILTER = ANISOTROPIC` as `LINEAR`. Start-up defaults of the shadow are set by `0x004ad3a0`. CONFIRMED.

**Consequence a replacement must respect:** a state the word does not set is **inherited** from whatever was last recorded before it in the replay stream. The UI words never set: `SEPARATEALPHABLENDENABLE` (start-up default off), `STENCILMASK` and `STENCILWRITEMASK`'s mask value (defaults `0xff`), `STENCILFAIL`/`STENCILZFAIL` in the write mode, `SCISSORTESTENABLE` (start-up default off; several 3-D passes' own blocks set it to 1 — e.g. `0x00e37adc`, `0x00e3df1f`, `0x00e45f49`, `0x00e748fa`, `0x00fcfa06` — so the UI's scissor rectangle relies on an inherited enable, §15.9 item 1), `FILLMODE`, the depth biases, and every sampler state except stage-0 U/V addressing.

### 15.4 Blend codes — the exact factors (closes §13.5's open half)

Read from the decoder's 16-way jump table (`0x00e52b84`). "Alpha test off" means the code also appends `ALPHATESTENABLE` = FALSE. All CONFIRMED:

| Code | `ALPHABLENDENABLE` | `SRCBLEND` | `DESTBLEND` | `BLENDOP` | Alpha test | Used by |
|---|---|---|---|---|---|---|
| `0x0` | FALSE | — | — | — | off | mask-write mode (forced) |
| `0x1` | TRUE | `ONE` | `ONE` | `ADD` | off | **`additive`** |
| `0x2` | TRUE | `SRCALPHA` | `ONE` | `ADD` | off | **`additive_alpha`** |
| `0x3`, `0x8`, `0xb` | nothing appended — blend left as it was | | | | | — |
| `0x4` | TRUE | `DESTALPHA` | `ONE` | `ADD` | off | — |
| `0x5` | TRUE | `SRCALPHA` | `INVSRCALPHA` | `ADD` | off | **`filter`** (the default) |
| `0x6` | TRUE | `DESTALPHA` | `INVDESTALPHA` | `ADD` | off | — |
| `0x7` | TRUE | `INVDESTALPHA` | `DESTALPHA` | `ADD` | off | — |
| `0x9` | TRUE | `ONE` | `ONE` | `REVSUBTRACT` | off | **`subtractive`** |
| `0xa` | TRUE | `SRCALPHA` | `ONE` | `REVSUBTRACT` | off | — |
| `0xc` | TRUE | `DESTCOLOR` | `ZERO` | `ADD` | off | **`multiply`** |
| `0xd` | TRUE | `ZERO` | `SRCALPHA` | `ADD` | off | — |
| `0xe` | TRUE | `ONE` | `INVSRCALPHA` | `ADD` | off | — |
| `0xf` | FALSE | — | — | — | **on**: `ALPHAFUNC` = `GREATEREQUAL`, `ALPHAREF` = word bits 20+ | — |

(Raw values as sent: `ZERO`=1, `ONE`=2, `SRCALPHA`=5, `INVSRCALPHA`=6, `DESTALPHA`=7, `INVDESTALPHA`=8, `DESTCOLOR`=9; `ADD`=1, `REVSUBTRACT`=3; `GREATEREQUAL`=7 — the public enumerations.)

**What this means for the five `render_mode` values** — HIGH (arithmetic on the confirmed states and the shader of §15.7, whose output colour is texture × vertex colour × one constant, **not** premultiplied by alpha):
- **`filter`**: ordinary straight-alpha "over" — `dst = src·a + dst·(1−a)`.
- **`additive`**: `dst = dst + src.rgb` — **the element's alpha has no effect at all**; fading an `additive` element through `alpha` changes nothing until the cull of §3 removes it at exactly 0. Only `tint` (which scales rgb) dims it.
- **`additive_alpha`**: `dst = dst + src.rgb·a` — the fading variant.
- **`subtractive`**: `dst = dst − src.rgb` (reverse subtract, alpha ignored like `additive`).
- **`multiply`**: `dst = dst · src.rgb` (alpha ignored; a white texel leaves the target unchanged).
- The same factors apply to the target's alpha channel (separate alpha blending is never turned on by the UI).

### 15.5 Depth — what `background` really does

Bits 8–11: `0` → `ZENABLE` FALSE; `1` → `ZENABLE` TRUE, `ZFUNC` `LESSEQUAL`, `ZWRITEENABLE` FALSE; `2` → TRUE, `EQUAL`, no write; `4` → TRUE, `LESSEQUAL`, writes on; `3` and others untouched. CONFIRMED. The UI uses `0` normally and `1` for `background` (§13.4 — only while the sRGB switch is 0, which is every frame).

The 2-D vertex shader outputs depth 1.0 (§15.7), and the scene's depth clear uses 1.0 (the frame clear at `0x005dda5d`). **So a `background` element draws only where the depth buffer still holds the cleared far value — behind every piece of 3-D geometry, visible only through empty sky/background** — and an ordinary element ignores depth entirely and never writes it. HIGH (depends on the depth buffer bound when the UI replays, not traced).

### 15.6 Stencil — the three mask modes exactly (closes §11 item 16)

Bits 12–15, CONFIRMED (jump table `0x00e52bd8`):

| Nibble | State appended | Mask mode |
|---|---|---|
| `0` | `STENCILENABLE` FALSE | **0 — masking off** |
| `5` | `ALPHATESTENABLE` TRUE, `ALPHAFUNC` `GREATER`, `ALPHAREF` 0; `STENCILENABLE` TRUE, `STENCILFUNC` `ALWAYS`, `STENCILPASS` `REPLACE`, `STENCILREF` 1, stencil writes on; **`COLORWRITEENABLE` 0** | **2 — write the mask** |
| `2` | `STENCILENABLE` TRUE, `STENCILFUNC` `EQUAL`, `STENCILREF` 1, `STENCILPASS`/`STENCILFAIL`/`STENCILZFAIL` `KEEP`, stencil writes off | **1 — draw inside the mask** |
| `1` | as `5` but colour writes stay on | unused |
| `3` | as `2` but `NOTEQUAL` — draw outside the mask | unused |
| `4` | as `5` but `STENCILREF` 0 — erase the mask, no colour | unused |

With the fixed first entries (two-sided stencil off, colour writes RGBA) and the forced blend code 0 and depth mode 0 of mask-write mode (§13.4), the three modes a replacement needs are:
1. **Mask begin** — the stencil buffer is cleared to 0: `0x00e525b0` → renderer `+0x40` (`0x004780f0`) → `0x00475610` records **opcode 11** (Clear: colour, engine flags, depth, stencil); the handler (`0x0049cb60`) turns the engine flags into public ones (`1` → `TARGET`, `2` → `ZBUFFER`, `4` → `STENCIL`, −1 → all three; depth/stencil dropped when `0x03395ea0` is 0) and calls `Clear(0, NULL, STENCIL, …, 1.0, 0)` after a commit.
2. **Writing (mode 2)** — no colour reaches the target; every pixel the mask element covers with **final alpha above 0** gets stencil 1. **The mask is binary**: a soft-edged mask image becomes a hard-edged mask at alpha > 0 (any non-zero alpha, even 1/255, counts). No blending, no depth test.
3. **Testing (mode 1)** — drawn normally with its own `render_mode` blend, only where stencil equals 1; the stencil is not modified. The read mask is not set by the UI (inherits; start-up default `0xff`).
Mode 0 only disables the test; it does not clear anything.

### 15.7 Shaders, samplers and texture filtering

**The two shaders the UI uses** (`rl_prim_2d_s.fxo_pc`, `rl_prim_2d_tex_s.fxo_pc` in `shaders.vpp_pc` — the engine names them without the `_s`; HIGH that the suffix is added by the shader lookup), read as public D3D9 bytecode, CONFIRMED:
- **Vertex shader (both):** `clip.x = 2·x·c52.x − 1`, `clip.y = 1 − 2·y·c52.y`, `clip.z = 1`, `clip.w = 1`; texture coordinate and colour passed through. `c52` here is engine constant id `0x1c` = (1/viewport width, 1/viewport height, 1, 1) — the engine's constant table `0x01350b20` (16 bytes per id: type, stage 0 = vertex / 1 = pixel / 2 = both, register, count) maps id `0x1c` to vertex register 52. **Positions are therefore viewport pixels, with no half-pixel shift**; the vertex's `+0x08`/`+0x0c` are ignored.
- **Pixel shader, normal pass:** textured — `colour = texture(s0, uv + c52.xy) × vertex colour × c37`; untextured — `vertex colour × c37`. Here `c52` is engine constant **id 6** (both stages, register 52) = **(0.5/texture width, 0.5/texture height)**; the draw sets id 6 before id `0x1c`, so the vertex stage ends with the viewport value and the pixel stage keeps the half-texel value. `c37` is the constant whose name hashes to `0x8f88517a` (`Tint_color` in `spec-render-pipeline.md` §20.12) — **its value during UI draws is OPEN** (§11 item 26).
- **A second pass** in both files outputs (0, 0, 0, alpha) only; what selects it is OPEN, HIGH that the ordinary UI draw uses the first.

**Correction to §12.4 and §14.1: there *is* a half-texel offset — in the pixel shader.** The quad's vertex UVs carry none (§12.4 stands as a statement about the vertices), but every textured UI sample is taken at `uv + (0.5/w, 0.5/h)`. With the vertex shader's unshifted pixel mapping and Direct3D 9's convention that pixel centres sit on integer coordinates, the result is the classic Direct3D 9 texel-centre alignment: a 1:1, axis-aligned quad at integer positions samples every texel exactly at its centre. Glyphs (§14.1) get this offset **on top of** their own near-edge vertex inset, so a glyph's near edge is sampled a full texel in. **For a Direct3D 11 port the exact equivalent is: keep the shader's +half-texel UV offset and shift every UI vertex by (−0.5, −0.5) pixels** (Direct3D 11 samples pixels at their centres, half a pixel further on); this reproduces both the covered pixels and the sample points. Dropping the offset instead matches only 1:1 unscaled quads.

**Sampler state for stage 0 during textured UI draws:**

| State | Value | Source |
|---|---|---|
| `ADDRESSU`/`ADDRESSV` | **`WRAP`** | the state word (bits 4–7 = 0, value 1) and the shader's sampler record — both, CONFIRMED |
| `ADDRESSW` | `WRAP` | shader sampler record, CONFIRMED |
| `MAGFILTER` | **`LINEAR`** | shader record says `ANISOTROPIC`; the commit sends it as `LINEAR`, CONFIRMED |
| `MINFILTER` | `ANISOTROPIC` | shader record, CONFIRMED |
| `MAXANISOTROPY` | 1 | never set by the UI path; start-up default 1, and the only literal writer found (`0x005dd910`, reached from the frame routine `0x005d7aa0`, which sets all 16 stages together with `LINEAR`/`LINEAR`/`LINEAR` filters) also sets 1 — **HIGH: minification is effectively bilinear** |
| `MIPFILTER` | **`LINEAR`** | shader record, CONFIRMED — trilinear whenever the texture has mip levels |
| `MIPMAPLODBIAS`, `MAXMIPLEVEL`, `BORDERCOLOR` | not set | inherited (start-up 0 / 0 / 0) |
| `SRGBTEXTURE` | on unless the PEG record's flags (`spec-texture-format.md` §10.7) carry `0x200`, and only while the global byte `0x03395e80` (start-up 1) is set | `0x004771f0` → `0x00fccbf0` (flag test) → `0x00474c80` → opcode 36 (`spec-render-pipeline.md` §20.1), CONFIRMED |

The shader sampler record is `rl_prim_2d_tex`'s single sampler entry (slot 0, address selectors 0/0/0, filter selectors 4/4, mip selector 0); this pass re-read the selector tables (`0x01351028`, `0x01351040`, `0x01351058`) and the builder `0x0048efe0`, confirming `spec-fxo-format.md` §7.3's mapping, and the shader's bind method (`0x0048f7c0`, `rl_d3d_shader` slot `+0x14`) records the block as **opcode `0x15`**. The `3d_tex` and `2d_bink` shaders use the same filters (all selectors 0: `ANISOTROPIC`/`ANISOTROPIC`/`LINEAR`, wrap).

**Practical effect:** with `WRAP` and bilinear filtering, a sample within half a texel of a sub-image's edge blends with the opposite edge of a stand-alone image, or with the neighbouring cell of a sheet-packed one — e.g. under scaling, rotation, or a non-integer position. An unscaled, axis-aligned quad at integer positions samples texel centres only and shows no bleed. A replacement that switches to clamp addressing or point filtering will look different from the original in exactly those cases.

### 15.8 The per-thread state manager and batch block

**`rl_d3d_state_manager`** (thread block `+0x674`), the parts this layer touches — CONFIRMED unless marked:

| Offset | Holds |
|---|---|
| `+0x08` | the draw-state sub-object: current colour (`+0xd8`…`+0xe4`, four floats), current texture handle (`0x00e82c90`/`0x00e82ca0`), the three-handle Bink binding (`0x00e82cb0`/`0x00e82ce0`), the material handle (`0x00e48630`/`0x00e82d20`), the scissor rectangle (`0x00e82940`), and the inputs of pixel constant `c37` (two colours at `+0x50`/`+0x60`, two scalars at `+0x1c`/`+0x20`; set through vtable slots `+0x30`…`+0x3c`, combined by `0x00489ec0` as colour × colour × scalar × scalar, alpha × alpha) |
| `+0x4ec`, `+0x4f0` | two flags set by every primitive draw (1, 0); meaning OPEN |
| `+0x4fe` | cleared when a material bind receives "no material" |
| `+0x50a` | "the last state block recorded was a UI state word" (§15.3's skip) |
| `+0x50b` | dirty flag (texture change, any block record) |
| `+0x760` | the command-buffer descriptor every record goes into |
| `+0x780`… | a 16-entry per-stage cache of four dwords (`0x00474ae0`) |
| `+0x884` | the vertex declaration last bound |
| `+0x888`, `+0x8c8` | per-stage texture handle and Direct3D texture pointer caches (16 stages; `spec-render-pipeline.md` §20.5) |
| vtable `+0x20` | `0x00474400`: set scissor — stores the rectangle and records **opcode 17**, whose handler (`0x0049cf90`) calls `SetScissorRect` (vtable `+0x12c`). This is what the UI's clip callbacks (`+0x10`/`+0x14`, §13.3, via `0x005de290`) reach. |
| vtable `+0x60` | `0x00474890`, the shader-bind entry `spec-render-pipeline.md` §10 names |

**Batch block** (thread block `+0x684`): `+0x04` frame open; `+0x08` staging mode (0 scratch / 1 ring); `+0x0c` vertex count (−1 when idle); `+0x10` reserves in the batch; `+0x14` first vertex offset; `+0x18` next expected offset (the contiguity test); `+0x1c` code; `+0x20` stride; `+0x24` state word; `+0x28`/`+0x2c` strip-join pointers; `+0x30` the last state word applied. CONFIRMED.

### 15.9 What a Direct3D 11 replacement has to do

1. Per primitive: blend state from §15.4 (five `render_mode` codes), depth from §15.5, stencil from §15.6, rasteriser cull none, colour writes RGBA except in mask-write mode. **Scissor:** the UI sets only the rectangle (opcode 17); the test enable is never set by the UI layer and is inherited — HIGH on, since the 3-D passes' own state blocks set engine key 30 to 1 (§15.3) — and confirming it is on when the UI replays is §11 item 25. A port should enable the scissor test for UI draws and set the rectangle exactly as §13.3 describes.
2. Vertices in viewport pixels, texture/colour per vertex (colour b,g,r,a bytes); output depth 1.0.
3. Pixel colour = texture(uv + half texel) × vertex colour × `c37`; shift geometry by −0.5 px in D3D11 (§15.7).
4. Sampler: wrap U/V/W, linear magnification, bilinear-equivalent minification, linear mip filter; sRGB decode per the PEG flag.
5. Mask: clear stencil to 0 at mask begin; write 1 where alpha > 0 without colour; test `== 1` for masked content.
6. Draw in exactly the walk's order — the original never reorders or merges across primitives.

### 15.10 Size

About 2,400 instructions read this pass: the `rl_draw` entries and the state manager's accessors (≈200), begin/reserve/end and the primitive renderer's allocator, draw and set-up (≈1,100), the state-word decoder (432), the record writers and replay handlers for opcodes 11, 17, `0x14`, `0x15`, 41, 43 and the commit (≈500), plus the shader set-up and sampler-block builder (≈380) and two shader programs.

---

## Changelog

- 2026-10-03 (local, new spec, UI-render unit U2, `review/exe-notes-2026-10-01/interp_vint_u2_tree_transform.md`): initial version. Resolved the shared render core in full: the per-frame tree walk (no caching, depth-first pre-order), the single three-test cull (visible/parent-alpha/own-alpha, no bounds culling anywhere), render-state inheritance (multiplicative tint/alpha, nearest-ancestor render_mode, sticky mask/background flags, clip as a separate document-level stack not part of the state), the full 2-D affine transform composition (closed form given), sibling depth-sort (class key before numeric depth, with a genuinely inconsistent tie-break), and the independent query-time geometry chain backing `screen_nw`/`screen_se`/`screen_size` (which recomputes rather than reads anything the walk writes, operates in document-authored coordinates rather than device pixels, and — contrary to the originating scoping note's premise — was proven correct by op-for-op correspondence with the render-time compose step rather than by a direct read/write relationship). Found that `screen_size` has a real, divide-by-zero-unguarded setter, correcting the flag-0-implies-read-only reading in `spec-lua-bindings.md` §9.2 for this one property. Corrected several scoping-note (`review/scoping-ui-render.md`) details in passing: the `visible` cull polarity, the identity of the two per-element callback pushes, and the identity of vtable slots 30/31. Cleanroom self-check: 0 hits.
- 2026-10-03 (local, UI-render unit U3, `review/exe-notes-2026-10-01/interp_vint_u3_draw_bitmap_text_group.md`): added §12, per-type draw for `bitmap`/`text`/`group` plus the game-side bodies of callback slots `+0x04`/`+0x20`/`+0x2c`, and extended §9/§10/§11 accordingly. Headline findings: `bitmap`'s quad treats `source_se` as an extent, not a corner, which can disagree with the `unscaled_size`-based bounds chain (§7) under a custom `source_nw`; `text` does no layout at draw time and instead replays a small 4-opcode command stream built once by layout, with explicit stream colours **replacing** (not multiplying) the inherited tint/alpha; `group`'s draw is a literal 1-instruction empty stub shared with `point`/`document`; the game-side quad draw has no half-texel offset and an optional sRGB→linear colour decode gated by an untraced context flag. Corrected the scoping note's slot `+0x04` label ("text state/setup" → "set current colour", confirmed shared by `bitmap` and `text`) and its address for `gradient`'s draw slot (shares `bitmap`'s body, not a distinct address). Cleanroom self-check: 0 hits (regex and the plain-substring backstop, both over the finished note; the backstop's own first pass caught itself quoting its ten search fragments and was corrected before the final run).
- 2026-10-03 (local, UI-render unit U4, `review/exe-notes-2026-10-01/interp_vint_u4_gradient_circle_clip_mask.md`): added §13, resolving the `gradient` mystery §11 item 15 left open, `bitmap_circle`'s draw, `clip`'s own draw body, and the mask-mode/`render_mode` semantics; updated §4.2/§4.3/§9/§10/§11 and this entry accordingly. Headline findings: `gradient` has its own intended draw method appended as a one-argument virtual at the end of its vtable — almost certainly a signature mismatch with the three-argument draw slot it should have overridden — so nothing ever calls it and a `gradient` draws exactly like a `bitmap` in the shipped game, confirmed by an exhaustive linear sweep for any caller or dispatch; `bitmap_circle` draws a textured pie of unclamped, stack-allocated wedge triangles whose ellipse radii apply along screen axes rather than the element's own, a real divergence under rotation with a non-square source; the clip-stack pop CONFIRMED (not merely suspected) to re-apply the entry it just removed rather than the one beneath it, a real nested-clip restore bug, and the walk was found to skip the clip unwind entirely inside mask subtrees; mask-mode semantics upgraded from HYPOTHESIS to HIGH (mode 2 = write the mask via a real stencil clear, mode 1 = test against it, mode 0 = off) and `render_mode` confirmed to have five real, distinct blend-code values rather than just `filter`. Cleanroom self-check: 0 hits (regex and the ten-fragment backstop, both over the finished note).
- 2026-10-03 (local, UI-render unit U5 — text; the scoping note's "U6"; numbered U5 because U4 had been taken in parallel for §13): added §14 (glyph drawing, the property-to-string pipeline, layout, markup, markup colours, line frames) and the new companion spec `spec-vint-font-format.md` (the `.vf3_pc` format, loader, derived fonts, the game's named font table, the Vint font pool, the metric callbacks); updated §9 (the resource-callback and string tables text uses), §10, §11 (closed items 6, 10, 11, 12; new items 20–24) and §12 (strike-and-append notes in §12.2/§12.4). Method: direct capstone read plus, new for this spec, emulated calls of the real shipped functions on the real `font_body.vf3_pc` (the project's `unicorn` harness), so layout and glyph numbers are the shipped code's output. Headline findings: (1) **markup colours do escape inheritance** — every `[color:]` writes explicit rgb that replaces the composed tint, and `[color:#RRGGBBAA]` writes an explicit alpha that escapes parent fades; it ships in the 27 cheats-warning strings (`#edc300FF`); (2) the font table's second id is a draw-first secondary, never an outline/fill pair in shipped data; (3) glyph UVs carry a half-texel inset on the near edge only; (4) the full layout rules — wrap at the last space/after a hyphen or mid-word, `\n`/`\N` escapes, kept leading spaces, alignment within the widest line (not `wrap_width`), vertical centring of mixed-height records on each line; (5) `force_case` has no effect in this build; `leading`/`kerning` are signed bytes whose setters turn ≥ 128 into −128; `[scale:]` replaces `text_scale` rather than multiplying it; unknown tags render visibly and force a line break; 17-deep `[format]` nesting corrupts layout; (6) a cross-glyph kerning false match and a measure/draw advance mismatch for characters a font lacks (font spec). Corrected the scoping note: `0x00b9cab0` is the font set-up, not the loader; the text unit's size is ≈5,100 instructions, not ≈2,600. Cleanroom self-check: 0 hits in both files (regex and the ten-fragment backstop), and 0 hits in this file before the edit.
- 2026-10-03 (local, UI-render unit U6 — the 2-D draw layer every earlier unit stopped at): added §15 and closed §11 item 16; new §11 items 25–28; strike-and-append notes in §12.4 (half-texel), §13.4/§13.5 (now resolved) and §14.1; a §10 row. Method: capstone read plus RTTI decoding, and the two UI shaders read from `shaders.vpp_pc` as public D3D9 bytecode. Headline findings: (1) the blend codes are exact D3D9 factors — `filter` SRCALPHA/INVSRCALPHA/ADD, `additive` ONE/ONE/ADD (so an `additive` element's alpha has no visible effect), `additive_alpha` SRCALPHA/ONE/ADD, `subtractive` ONE/ONE/REVSUBTRACT, `multiply` DESTCOLOR/ZERO/ADD, plus eleven unused codes; (2) mask modes: write = stencil ALWAYS/REPLACE ref 1 with alpha test > 0 and colour writes off (a binary mask), test = stencil EQUAL 1 with KEEP, off = stencil disabled, after a `Clear` of the stencil to 0; (3) `background` is a depth test (LESSEQUAL, no writes) against a shader depth of 1.0 — such elements show only where no 3-D geometry wrote depth; (4) every UI primitive is one `DrawPrimitiveUP` with its vertices copied into the deferred command buffer (opcode 41 — the first producer of that opcode found), never merged across primitives; (5) stage-0 sampling is WRAP addressing with linear magnification, anisotropic-at-1 (bilinear) minification and linear mip filtering, plus a half-texel UV offset applied in the pixel shader, correcting §12.4's "no half-texel offset"; (6) the live renderer object is `rl_d3d_renderer`, not `rl_renderer` (noted in `spec-render-pipeline.md` §21). Cleanroom self-check: 0 hits in this file (regex and the ten-fragment backstop), unchanged from 0 before the edit.
- 2026-10-08 — item 8 gains a cross-reference to new `spec-hud-runtime.md` §6, which restates `sr2_map`'s structural facts from the live-data-flow side and names its vtable as the recommended starting point for a future minimap pass. No new disassembly in this file. Cleanroom self-check: 0 hits. Synced to Team B.
- 2026-10-08 — item 8's cross-reference updated and item 14's "minimap stays parked" note qualified, after `spec-hud-runtime.md` §6 was fully replaced by a live-data-feed tracing pass. **Correction carried over: `0x0132cf68` is `sr2_map`'s property-descriptor table, not its vtable** — the real per-instance vtable's own base address is still not located by any unit, but the 2 property pairs' slot offsets are now pinned (zoom get=`+0xcc`/set=`+0xc8`, map_mode get=`+0xd4`/set=`+0xd0`, by disassembling all 4 get/set trampolines directly). No new disassembly in this file; both edits are cross-reference corrections only. Cleanroom self-check: 0 hits. Synced to Team B.
- 2026-10-08 — item 8 fully CLOSED and item 14's own note updated, from a cross-team follow-up
  investigation (`spec-hud-runtime.md`/`spec-weapons-runtime.md` territory). **`sr2_map`'s real
  per-instance vtable base address is now CONFIRMED: `0x01259cb4`, full 54 slots read in entirety
  (`+0x00`-`+0xd4`, terminating exactly there).** The get/set slot assignment from the prior rewrite was
  itself a labeling artifact, now corrected: zoom get=`+0xc8`/set=`+0xcc`; map_mode get=`+0xd0`/set=`+0xd4`
  — the same "lower slot = getter" convention this document's own `element` base-class pattern uses, not
  reversed. Draw/post-draw slots (`+0xbc`/`+0xc0`) read in full — a mode-gated 2D shape primitive, no icon
  blit. No new disassembly in this file; this entry documents a cross-reference update only. Full detail:
  `scratchpad/weapons_fire_damage_and_player_blip_2026-10-08.md`. Cleanroom self-check: 0 new hits. Synced
  to Team B.
- 2026-10-10 (SPEC TEAM, agent `a8dce4cc1610b5a56`): **correction** — `0x00846ae0`, previously
  cited here as "tween/animation/data-item processing," is NOT that — opened in full, it's OS-
  cursor-visibility/exclusive-mode/window-focus housekeeping with zero Vint document/element
  touches, independently re-verified against the live Ghidra server before splicing. The real
  per-frame document walk (§2.1's own `0x00848e40`→`0x00e22660`→`0x00e1f6e0` chain) is unaffected
  by this correction. Cross-referenced §2.2's own "callback slot `+0x00`" (`0x00e229b0`, a direct
  call, re-verified) as a candidate — but unconfirmed — home for the still-OPEN tween-interpolation
  question (`spec-vint-doc-format.md` §7/§8 item 8, now closed on the file-format side). Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.
