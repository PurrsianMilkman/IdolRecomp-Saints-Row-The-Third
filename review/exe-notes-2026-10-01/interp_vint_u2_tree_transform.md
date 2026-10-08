# Interp note — U2: Vint shared render core (tree walk, culling, render-state inheritance, transform composition, depth sort)

**Date:** 2026-10-03 · **Unit:** U2 of `review/scoping-ui-render.md` §4 (item 3). Investigation only: no spec file was edited. For whoever folds it into `spec-lua-bindings.md` (§9, element fields) and a future Vint render spec.

**Shared conventions:** the same as `interp_vint_u1_property_tables.md` (U1) and the other `exe-notes-2026-10-01` interp notes — plain hex addresses, my own words, labels **CONFIRMED** / **HIGH** / **HYPOTHESIS**, no Ghidra auto-names and no decompiler output; property, type and metadata names are the engine's own shipped strings, quoted as data. Property names, descriptor rows, getter/setter thunks and the element-family vtable slot layout (`+0x30`…`+0xb4` the 17 `element` properties, lower slot = getter, `+0xb8`/`+0xbc`/`+0xc0` pre-draw/draw/post-draw) are cited from U1 §3.2 / §4.1 and not re-derived.

**Method.** Ghidra was not needed: as in U1's method item 2, the executable's bytes were read directly.
- Direct read of `SaintsRowTheThird.exe` with `tools/harnesses/exe_va.py` plus capstone. Scratch scripts (session scratchpad): `u2dis.py` (recursive-descent function lister with absolute-operand annotation), `u2vt.py` (element-family vtable dump, all 10 concrete element classes, slots `+0x00`…`+0xc4`), `u2scan.py` (linear-sweep regex scan of an address range), `u2misc.py` (constants, jump tables, CRT identities).
- Every function cited below was listed in full by `u2dis.py` unless marked otherwise.

---

## 0. Element fields read by the render core — CONFIRMED

These are read directly from the shared element-base accessor bodies, reached through the vtable slots U1 assigned to each property (vtable `+0x30`…`+0xb4`; identical in all 10 concrete element vtables except where noted).

| Element offset | Property (U1 §4.1) | Getter slot → body | Evidence |
|---|---|---|---|
| `+0x1c` | first child (link) | — | already in `spec-lua-bindings.md` (clone section); walk reads it |
| `+0x20` | next sibling (link) | — | walk and bounds code iterate it |
| `+0x24` | parent (link) | — | already in `spec-lua-bindings.md`; bounds/scale recursion climbs it |
| `+0x28` | handle (serial) | — | already in `spec-lua-bindings.md`; written by `0x00e28610` from counter `0x02a74de0` |
| `+0x2c` | name hash | — | written by `0x00e28770`, which re-buckets the element in a 64-way table at `0x02a74ce0`; its callers (`0x00e28846`, `0x00e28ce9`) pass `0x00d9e740(string, 0)`, the lower-cased CRC-32 of U1 §1.1. HIGH that the string is the element's name. |
| `+0x30` | owning document | — | walk reads the document's clip-stack count through it |
| `+0x58`/`+0x5c`/`+0x60` | `tint` (r, g, b) | `+0x58` → `0x00e27890` | copies 3 floats |
| `+0x64`/`+0x68` | `anchor` (x, y) | `+0x48` → `0x00e27790` | setter `+0x4c` → `0x00e277c0` stores here |
| `+0x6c`/`+0x70` | `offset` (x, y) | `+0x50` → `0x00e27800` | setter `+0x54` → `0x00e27830` |
| `+0x74`/`+0x78` | `unscaled_size` (w, h) | `+0xa8` → `0x00e28000` | getter first calls vtable `+0x24` (a per-type refresh hook; `text` overrides it) |
| `+0x7c`/`+0x80` | `scale` (x, y) | `+0x98` → `0x00e27e80` | setter `+0x9c` → `0x00e27ee0` |
| `+0x84` | `rotation` (float) | `+0x90` → `0x00e27e20` | setter `+0x94` → `0x00e27e60` converts the value record to float (`0x0071b9b0`) and stores it unscaled |
| `+0x88` | `alpha` (float) | `+0x60` → `0x00e2e770` | (U1 §3.2) |
| `+0x8c` | `render_mode` (dword) | `+0x30` → `0x00e275d0` | |
| `+0x90` | `auto_offset` (dword) | `+0xa0` → `0x00e27f40` | |
| `+0x94` | `mouse_depth` (int) | `+0x70` → `0x00e27a50` | **the getter returns `depth` (`+0x98`) when `+0x94` holds `0x7fffffff`** — "unset" means "same as depth" |
| `+0x98` | `depth` (int) | `+0x68` → `0x00e27980` | |
| `+0x9c` | `mask` (bool byte) | `+0x40` → `0x00e27740` | |
| `+0x9d` | `background` (bool byte) | `+0xb0` → `0x00e28060` | |
| `+0x9e` | `visible` (bool byte) | `+0x38` → `0x00e276d0` | (U1 §3.2) |

**Identity of the three computed properties' slots (CONFIRMED, vtable dump):**

| Property | Getter slot | Shared body | `group` | `text` | `point` | Setter slot body |
|---|---|---|---|---|---|---|
| `screen_size` | `+0x78` | `0x00e27c00` | `0x00e309b0` | `0x00e322c0` | `0x00e333f0` | `+0x7c`: `0x00e27c70` (a real setter, see §6); `group`/`point` `0x00e27bf0`; `text` `0x00e322f0` |
| `screen_nw` | `+0x80` | `0x00e27b30` | `0x00e309f0` | shared | shared | `+0x84`: `0x00e27bf0` in all 10 |
| `screen_se` | `+0x88` | `0x00e27b90` | `0x00e309e0` | shared | shared | `+0x8c`: `0x00e27bf0` in all 10 |

`0x00e27bf0` is two instructions: return false. This resolves the scoping note's "unattributed overrides" of slots 30/31 (§1.3 there): they are the `screen_size` getter/setter pair (`+0x78`/`+0x7c`), not anchor/offset.

Vtable slot `+0xc4` (U1 OPEN item 7) is **`0x00e27530` in all 10 classes: the `auto_offset` applier** (§4.5).

---

## 1. The tree walk

### 1.1 What triggers a walk — CONFIRMED (call chain), HIGH (frame cadence)

There is **no cached or dirty-flag render tree**: every element of every rendered document is re-walked and its state recomposed on every call.

- **Main path.** The game's UI frame function **`0x00848e40`** (427 insns by my lister) is called from `0x007025bc`, immediately after the per-frame UI update `0x00846ae0` (which the scoping note §1.1 shows driving tween/animation/data-item processing), and also from `0x005d40ce`. At `0x00849010` it calls **`0x00e22660`**, a one-instruction jump to **`0x00e1f6e0` — "render all documents"**:
  1. clears the global `0x02a4d184`;
  2. walks the circular list of documents starting at `0x02a4d17c` (next pointer at document `+0`), collecting every document whose byte **`+0x59b`** is non-zero into a 64-entry stack array (0x100 bytes; no bound check in the loop);
  3. sorts them with the C-runtime `qsort` (`0x00ea50f0`) and comparator **`0x00e1edb0`**, which returns `b.+0x58c − a.+0x58c` — **descending document depth** (§5.1);
  4. calls the document renderer `0x00e1f0b0` with **pass byte 0** for each document whose byte **`+0x59c`** is zero.
- **Side path.** The callback-registered entry `0x00848d90` (scoping note §1.1) calls `0x007b3a70` (`this` = `0x012fced8`), which walks a game-side array backwards (count at `this+0x24`), and for each entry whose byte `+0x1e` bit 0 is set and whose vtable `+0x34` returns true, resolves its document by id (entry `+0x24` → `0x00e1f330`) and renders it with **pass byte 1**; it then resolves the document named `bg_saints` (string at `0x01155ac4`, via `0x00e1f2f0`) and renders it with **pass byte 0**. What this path is for is OPEN (§8).
- **Document `+0x58c` is the `document_depth` metadata value.** `0x00e21490`–`0x00e214bb` scans the document's 64-byte metadata entries (at document `+0xa8`, count at `+0x588`) for the key `document_depth` (string `0x012563ec`, compared through `0x00eaa178`, HIGH a C-runtime string compare) and parses its value with `sscanf("%i")` (format `0x0118f9f8`, call `0x00ea500b`) straight into `+0x58c`. The code at `0x00e1f44e`–`0x00e1f466` (document initialization) zeroes `+0x584`, `+0x58c`, `+0x59b`, `+0x59c`.

### 1.2 Document render `0x00e1f0b0(pass)` — CONFIRMED

`this` is the document object (not an element; elements point back at it through element `+0x30`). In order:
1. Builds a fresh 128-byte render state on its stack with `0x00e26630` (initial values, §3.1), then overwrites: tint r/g/b and alpha = 1.0; render mode dword (`+0x00`) = 1; byte `+0x14` = 0; byte `+0x64` = 0; byte **`+0x65` = the pass argument**.
2. Calls **`0x00e1ef70`** (screen fit, §4.1) to get a uniform scale and a centring offset, adds the offset to the state's position and writes the scale into the matrix diagonal.
3. Calls callback slot `+0x00` through thunk `0x00e229b0`.
4. If the root element pointer at document **`+0x594`** is non-null, calls the walk `0x00e26a30` on it with: the state, a **scratch array** = the current thread's TLS block (`fs:[0x2c]`, slot index in `0x02cc4700`) `+ 0x270`, and **capacity `0x100`** (256 element pointers).

### 1.3 The recursive walk `0x00e26a30(state, scratch, capacity)` — CONFIRMED, 197 insns

A `this`-call on the element; three stack arguments, popped on return. Per element:

1. **Cull** (§2). If culled, return immediately: no hooks, no children.
2. Call **pre-draw** (vtable `+0xb8`).
3. Copy the parent's 128-byte state (32 dwords) into a local and **compose** this element into it with `0x00e26680` (§3, §4).
4. Push two values of the composed state to the game: state `+0x64` (the inherited `background` flag, as a dword whose low byte is the flag) through thunk `0x00e229f0` → **callback slot `+0x1c`**; state `+0x00` (the effective `render_mode`) through thunk `0x00e229d0` → **callback slot `+0x18`**. This happens for every non-culled element, drawn or not.
5. **Pass filter on the draw call** (state byte `+0x65`, set only by the document render):
   - pass 0 → draw;
   - pass 1 → draw unless the element is-a `text` (vtable `+0x18` with `text`'s type id, cell `0x0132c998`, U1 §1.2);
   - any other pass → no draw.

   The draw is **vtable `+0xbc`(composed state, scratch, capacity)** — the per-type draw gets the same scratch array.
6. **Gather children** into `scratch[0..n)`: from element `+0x1c`, following `+0x20`, keeping only children for which vtable `+0x18`(`element`'s type id, cell `0x0132bdd0`) is true — so `tween`/`animation` children are skipped. The gather stops early when `n` reaches `capacity` (the check is at the loop head), silently dropping the remaining children.
7. If `n > 1`, `qsort(scratch, n, 4, 0x00e26980)` (§5.2).
8. **Recurse** into each child in sorted order, passing the composed state, `scratch + n` and `capacity − n`. So the scratch array is used as a stack: each level's sorted child list stays live in its slice while deeper levels use the space above it. The total of sibling-list lengths along any root-to-leaf path is bounded by 256.
   - If composed state byte `+0x14` (inside a mask subtree, §3.2) is **zero**, the loop also drives the mask-mode state machine around each child (§3.2) and afterwards **unwinds the clip stack**: if the document's clip count (`+0x584`) is higher than it was before the children were walked, it calls `0x00e1f6b0` once per extra entry (§3.3).
   - If byte `+0x14` is **non-zero**, the children are recursed with no mask-mode handling and no clip unwind.
9. Call **post-draw** (vtable `+0xc0`), then return.

**Order:** depth-first, pre-order. A parent's own draw happens before its children's; siblings go in comparator order (§5.2). There is no separate "collect then sort globally" phase — sorting is per sibling list only, so **depth never reorders an element across different parents**.

**Instruction counts (my lister, recursive descent from the entry):** walk `0x00e26a30` 197; document render `0x00e1f0b0` 51; state init `0x00e26630` 25; render-all `0x00e1f6e0` not listed in full (≈40, read linearly to the qsort call and the loop after it).


## 2. Culling — CONFIRMED

The **only** cull in the render core is at the top of the walk `0x00e26a30`, before anything else happens:

1. **`visible`**: if element byte `+0x9e` is zero, return. (The scoping note's "hidden byte set" has the polarity reversed: it is the `visible` byte being *clear*.)
2. **Inherited alpha**: load `FLT_EPSILON` (float `0x34000000` = 1.1920929e-07 at `0x01122d70`) and compare it with the *parent's* composed alpha (incoming state `+0x10`). If the parent's alpha is **≤ epsilon**, return.
3. **Own alpha**: compare the same epsilon with the element's own `alpha` (`+0x88`). If **≤ epsilon**, return.

Notes:
- Both alpha tests use the x87 "less than" flag only, so a NaN alpha is **not** culled (an unordered compare sets that flag too). Negative alpha is culled.
- A culled element skips everything: pre-draw, state push, draw, its whole subtree, post-draw. Because the composed alpha is the product of the ancestors' alphas (§3.1), test 2 also catches any ancestor at ~0, but test 3 is still needed for the element itself.
- **There is no off-screen / bounds culling** in the walk, the compose step `0x00e26680`, the document render or the render-all loop. Nothing in them reads `unscaled_size` or any bound. The one per-type draw method I listed in full, `bitmap`'s `0x00e312b0` (31 insns), has no bounds test either: it resolves its resource (`0x00e24150` on element `+0xa0`) and goes straight to the callback. Whether any other draw method or game callback rejects off-screen primitives is left to U4/U6.
- **The pass filter is not a cull**: it suppresses only the element's own draw call (§1.3 step 5). Hooks, state pushes and children still run.
- **Tween/animation children are skipped structurally** (the is-an-`element` test in the child gather, §1.3 step 6), not culled.
- **Query-time geometry ignores all of this**: the `screen_*` chain (§6) never reads `visible` or `alpha`, so a hidden element still reports bounds, and an element's bounds include hidden children.

## 3. Render-state inheritance

### 3.1 The 128-byte render state — CONFIRMED except where marked

Built per document by `0x00e26630` + `0x00e1f0b0` (§1.2); copied per element by the walk (`rep movsd`, 32 dwords) and composed by `0x00e26680`. The parent's copy is never modified, so siblings all start from the same parent state.

| State offset | Content | Root value | Child rule (`0x00e26680`) |
|---|---|---|---|
| `+0x00` | effective `render_mode` (dword) | **1** (set by `0x00e1f0b0`) | **override** by the element's `render_mode` (`+0x8c`) **if non-zero**; 0 (`default`) inherits |
| `+0x04`/`+0x08`/`+0x0c` | effective tint r, g, b | 1, 1, 1 | **multiply** component-wise by element `tint` (`+0x58`/`+0x5c`/`+0x60`) |
| `+0x10` | effective alpha | 1 | **multiply** by element `alpha` (`+0x88`) |
| `+0x14` | byte: inside a mask subtree | 0 | **OR** with element `mask` (`+0x9c`) |
| `+0x18`…`+0x23` | — | not written | not touched by init, document render, compose or walk (OPEN: per-type draws) |
| `+0x24`/`+0x28` | cumulative scale product (x, y) | 1, 1 (screen fit **not** included) | **multiply** by element `scale` (`+0x7c`/`+0x80`) |
| `+0x2c`/`+0x30` | position (pivot of the current frame) | screen-fit offset (§4.1) | `+= M_parent · anchor` (§4.2) |
| `+0x34`…`+0x48` | 2×3 matrix `M` (row-major: `m0 m1 m2 / m3 m4 m5`) | `diag(fit scale x, fit scale y)`, translation column 0 | `M = M_parent · R(rotation) · S(scale)` (§4.2) |
| `+0x4c`/`+0x50` | element origin = top-left of its own content box | — (written by compose only) | `= position + M · offset` (§4.2) |
| `+0x54`/`+0x58` | screen-fit scale (x, y) | from `0x00e1ef70` | carried unchanged |
| `+0x5c`/`+0x60` | screen-fit offset (x, y) | from `0x00e1ef70` | carried unchanged |
| `+0x64` | byte: inside a `background` subtree | 0 | **OR** with element `background` (`+0x9d`) |
| `+0x65` | byte: render pass | the document-render argument | carried unchanged |
| `+0x66`…`+0x7f` | — | not written | not touched (OPEN) |

**What each child inherits, in one line each:**
- **alpha, tint: multiplicative** down the whole path; the root is (1, 1, 1, 1). No clamping anywhere in compose.
- **render_mode: nearest non-`default` ancestor-or-self wins**; an all-`default` path yields the document's seed value **1**, which is the number of the `filter` enumerator (U1 §2). HYPOTHESIS: the game's blend callback treats 1 as ordinary alpha blending, so `filter` is the engine's name for normal blending; not checked against callback `+0x18` (U3).
- **mask, background: sticky flags** (logical OR) — once set by an ancestor, every descendant has them.
- **Clip region: not in the state at all** (§3.4).
- The **pass** byte and screen-fit values are per-document constants.

The two values pushed to the game per element (§1.3 step 4) are exactly the effective `render_mode` (callback `+0x18`) and the inherited `background` flag (callback `+0x1c`, passed as a dword whose low byte is the flag). Tint, alpha, matrix and origin are not pushed by the walk; the per-type draw reads them from the state it is handed (`bitmap`'s draw passes state `+0x04` tint, `+0x10` alpha, `+0x34` matrix and `+0x4c` origin to its helpers; CONFIRMED in `0x00e312b0`).

### 3.2 Mask subtrees — CONFIRMED mechanism; semantics for U5

Mask behaviour lives in the walk's child loop (only when the parent's composed byte `+0x14` is zero, i.e. the parent is not itself inside a mask subtree). The mode is a per-thread value read by `0x00e237b0` (through `0x00e236f0`), and changed by `0x00e23770(mode)`, which calls **callback slot `+0x08`(mode)** only when the mode actually changes and then records it.

For each child in sorted order (mask children sort before non-mask ones, §5.2):
- mode 0 and child is a mask (`+0x9c` = 1): call **callback slot `+0x0c`** (thunk `0x00e22990`, no arguments), then set mode **2**, and remember that this loop opened a mask;
- mode 2 and child is not a mask: set mode **1**;
- then recurse into the child.

After the loop, if this loop opened a mask, set mode **0**.

So among one parent's children: the mask children draw first in mode 2 (writing the mask), then the remaining siblings draw in mode 1 (masked by it), then the mode returns to 0. Everything below a mask child sees state byte `+0x14` set, so its own child loops do no mask switching and no clip unwinding. HIGH: modes 2/1/0 are "write mask"/"test against mask"/"off", and slot `+0x0c` clears the mask buffer. U5 should confirm from callbacks `+0x08`/`+0x0c`.

### 3.3 Clip — the document clip stack (mechanism CONFIRMED; for U5)

Clip is a **document-level stack**, not part of the inherited state:
- **Push.** Only the `clip` draw method `0x00e30db0` pushes, and only when byte `+0xa8` is 1 (HIGH: the `clip_enabled` storage). It:
  1. gets an integer rectangle from `0x00e30c60` (which reads `offset`, `clip_size` and the cumulative-scale and position helpers `0x00e274f0`/`0x00e274d0`, and does not read rotation);
  2. maps it to device pixels with the state's screen-fit scale and offset (`+0x54`…`+0x60`), rounding through `0x00ea2560`/`0x00dad900`;
  3. pushes it with `0x00e1f5c0` onto the document's stack: 16-byte entries from document `+0x08`, count at **`+0x584`**, entries written only while the count is below 8;
  4. each new entry is the **intersection** with the previous top (an empty intersection stores zeros), and is sent to **callback slot `+0x10`**. The count is incremented even when the entry is not stored.
- **Pop.** `0x00e1f6b0` decrements the count. At zero it calls **callback slot `+0x14`** (thunk `0x00e22970`). Otherwise, below 8, it calls slot `+0x10` with the entry at index *new count*. By the push's own indexing that is the entry being popped, not the one beneath it. U5 should check whether that re-applies the inner rectangle (a real nested-clip bug) or whether I have the indexing off by one.
- **Who pops.** The walk saves the document's count **after** the element's own draw (`0x00e26b4d`, which follows the draw call at `0x00e26aef`). After the child loop, it pops back down to that saved value. So:
  - pushes made by a **child's own draw** are popped by the **parent's** frame, after the parent's whole child loop;
  - a `clip` element therefore clips **its own subtree and every later sibling** under the same parent. Combined with the depth comparator, which sorts `clip` children **first** (§5.2), a `clip` child effectively clips **the whole of its parent's remaining content**;
  - inside a mask subtree (state `+0x14` set) nothing is unwound locally; the nearest ancestor frame running the normal path pops everything.

  HIGH, from the control flow. The visual consequence should be checked against a real document in U5.


## 4. Transform composition

### 4.1 Document → device: the screen fit `0x00e1ef70` — CONFIRMED (48 insns)

`0x00e1ef70(document, out scale, out offset)` uses:
- the screen size, as two integers from `0x00e237e0` (the per-thread record already in `spec-lua-api-behaviour.md` §26.26);
- the document's authored size, as floats at document **`+0x5a0`** (width) / **`+0x5a4`** (height).

Let `a = docW / docH`.
- If the global byte **`0x0132bbfc`** is non-zero (**fit to height**): `H = screenH`, `W = a · screenH`.
- Otherwise (**fit to width**): `W = screenW`, `H = screenW / a`.

From these:
- **scale** = `(W / docW, H / docH)`, which is always uniform;
- **offset** = `((screenW − W) · 0.5, (screenH − H) · 0.5)`, with 0.5 the double at `0x012a2dc0`.

`0x0132bbfc` is 1 in the static image. A raw sweep of `.text` finds only reads of it (here and in the sibling at `0x00e1f046`), no direct writer, so HIGH: the shipped behaviour is always fit-to-height. A document wider than the screen's aspect gets a negative x offset (cropped sides); a narrower one gets pillarboxed.

The document render seeds the state with this scale as the matrix diagonal and this offset as the position (§1.2). **Everything below the document is therefore composed in device pixels.**

### 4.2 Per-element composition `0x00e26680` — CONFIRMED (165 insns)

**Notation.** `P` is the parent's composed state and `e` the element. `c = cos(e.rotation)`, `s = sin(e.rotation)`. The sine and cosine come from the C-runtime routines `0x00ea2470` and `0x00ea3a70` respectively. That identity is HIGH: both are the standard x87/SSE2-dispatch shape, and the matrix built from them is the identity only if the second is cosine. The matrices are:
- `R = [c −s 0 ; s c 0]`
- `S = [sx 0 0 ; 0 sy 0]`

**The steps, in code order:**
1. `render_mode`, tint, alpha and the two flags, as in §3.1.
2. `pos = P.pos + P.M · anchor` (state `+0x2c`/`+0x30`), using the full affine form including `P.M`'s translation column, which is always 0.
3. `M = P.M · R`, then `M = M · S`, giving `M = P.M · R · S` (state `+0x34`…`+0x48`).
   - The product helper `0x0078b790` (74 insns) computes `out = this · arg` for 2×3 affine matrices, `out[0..5] = {a0b0+a1b3, a0b1+a1b4, a0b2+a1b5+a2, a3b0+a4b3, a3b1+a4b4, a3b2+a4b5+a5}`, in double precision, rounding each entry back to float.
4. `origin = pos + M · offset` (state `+0x4c`/`+0x50`).
5. `cumulative scale = P.cs ⊙ scale` (state `+0x24`/`+0x28`).

**Closed form.** For a root-to-element path `e0 … en`, a point `q` in `en`'s content box (`0 ≤ q ≤ unscaled_size`) lands on screen at:

```
device(q) = fitOffset + Σ_{k=0..n} M_{k−1} · anchor_k + M_n · (offset_n + q)
M_{−1} = diag(fitScale),   M_k = M_{k−1} · R(rotation_k) · S(scale_k)
```

**Consequences:**
- **It is a full 2-D affine composition.** The translation is carried outside the matrix. A rotated child under a non-uniformly scaled parent is sheared, because `M = P.M · R · S` keeps the parent's non-uniform scale on the left.
- **`anchor` is the element's position in its parent's frame.** It is scaled and rotated by the parent's accumulated matrix, never by the element's own.
- **Rotation and scale pivot about the anchor point.** `offset` is applied *inside* the element's own rotated and scaled frame, so it places the content box relative to the pivot. `auto_offset` (§4.5) just sets `offset` to minus an alignment point of the box.
- **A parent's `offset` does not move its children.** Children accumulate from the parent's `pos` (`+0x2c`), not from its `origin` (`+0x4c`). This is CONFIRMED: compose step 2 reads only `+0x2c`/`+0x30`.
- **`rotation` is in radians.** The stored float goes straight to the C-runtime sine and cosine, and the setter `0x00e27e60` converts the value record without scaling. A positive angle maps `x' = c·x − s·y`, `y' = s·x + c·y`; with y pointing down on screen, that is clockwise.
- **The composed state is a stack local.** It is copied per element and thrown away when the element's walk frame returns. **Nothing is written back to the element.**

### 4.3 What the draw receives — CONFIRMED for `bitmap`

The walk hands the per-type draw a pointer to the composed state. `bitmap`'s draw `0x00e312b0`:
1. calls `0x00e23450` with the tint (`+0x04`) and the alpha (`+0x10`);
2. calls `0x00e30470` with the matrix (`+0x34`), the origin (`+0x4c`) and two element fields (`+0xb8`, `+0xc0`, HIGH: size/source data). `0x00e30470` forwards to the callback thunk `0x00e22870`.

So the quad is built game-side from `origin` and `M`. How the corner points are formed is for U3/U4.

### 4.4 Query-time chain (feeds `screen_*`, §6) — CONFIRMED

**`0x00e272c0(pos out, M out, M′ out)` (164 insns).**
- If the parent link (`+0x24`) is non-null and is-an-`element`, it recurses into the parent with the same three buffers.
- Otherwise it starts from `pos = 0` (`0x00db91c0` zeroes a 2-vector) and `M = M′ = identity`.
- It then applies this element exactly as compose does:
  - `pos += M · anchor` (anchor read through getter slot `+0x48`);
  - `M′ = M · R`, then `M = M′ · S` (the same two helpers, the same `R`/`S` layout).
- On return, `M` is the full matrix and `M′` is the same chain without this element's own scale.

**`0x00e280d0(nw, ne, sw, se out)` (150 insns).**
1. Calls `0x00e272c0`.
2. `origin = pos + M · offset` (offset read through getter slot `+0x50`).
3. With `(w, h)` = `unscaled_size` read directly from `+0x74`/`+0x78`, it outputs the four corners:
   - `origin`;
   - `origin + M·(w, 0)`;
   - `origin + M·(0, h)`;
   - `origin + M·(w, h)`.

**`0x00e28540(rect out, flag)` (61 insns) — vtable slot `+0x2c` for 8 of the 10 classes.**
- It takes the axis-aligned box of those four corners and unions in the slot-`+0x2c` box of **every** is-an-`element` child, recursively.
- The rect layout is `{nw.x, nw.y, se.x, se.y}`.
- The min/max helpers each compare with SSE2 `comisd`:
  - `0x00dddab0`: box from two points;
  - `0x00dddba0`: extend by a point;
  - `0x00dddb10`: union of two boxes.
- The `flag` argument (pushed as 1 by every caller seen) is not read by this body.

**Overrides of slot `+0x2c`:**
- **`group` — `0x00e308b0` (97 insns).** The box is the union of the element children's boxes only; the group's own size plays no part. With **no** element child, the box collapses to the single point `pos + M′ · offset`. In that degenerate case the group's own `scale` is *not* applied to its offset, unlike in compose.
- **`text` — `0x00e31ee0` (17 insns).** If the text's layout is dirty (vtable `+0x20` → `0x00e330c0`, bit 1 of byte `+0x10c`), it re-runs text layout `0x00e31c50`, then falls into the shared `0x00e28540`.

### 4.5 `auto_offset` — vtable slot `+0xc4` = `0x00e27530` (CONFIRMED; closes U1 OPEN 7)

Eleven instructions plus two 9-entry byte-indexed jump tables:
- the x table: indices at `0x00e275ac`, targets at `0x00e275a0`;
- the y table: indices at `0x00e275c4`, targets at `0x00e275b8`.

It writes `offset` (`+0x6c`/`+0x70`) from `unscaled_size` `(w, h)`. Value `none` (0) and anything above 9 leave `offset` untouched.

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

So `auto_offset` names the point of the content box that sits on the anchor (the pivot).

Slot `+0xc4` is fetched at six sites: `0x00e27fb9` (in the `auto_offset` setter `0x00e27f80`), `0x00e313da`, `0x00e3149d`, `0x00e31e37`, `0x00e32ab0` and `0x00e349d5`, which are size-changing paths of `bitmap`, `text` and `bitmap_circle`. HIGH that it is re-applied whenever `auto_offset` or the size changes; those callers were not read individually.

## 5. Depth sort

### 5.1 Between documents — CONFIRMED

The render-all loop (§1.1) sorts active documents with `0x00e1edb0`, which returns `b.document_depth − a.document_depth` (document `+0x58c`, from the `document_depth` metadata key).
- **The larger `document_depth` is drawn first**, i.e. further back.
- Equal depths compare as 0, and the C-runtime `qsort` is not stable, so the relative order of equal-depth documents is unspecified.
- The comparator also returns 0 when either pointer is null.

### 5.2 Between siblings — comparator `0x00e26980` (CONFIRMED, 72 insns)

`qsort` puts `a` after `b` when the comparator returns a positive value. The keys are, in order:
1. **Class key, larger first.** `key = (mask byte +0x9c ≠ 0 ? 1 : 0) + (exact type id == clip ? 2 : 0)`.
   - The type id comes from vtable `+0x1c`, compared with `clip`'s type-id cell `0x0132c79c` through the pointer `0x0132c7a0` (U1 §1.2).
   - Resulting order: **clip-and-mask, clip, mask, everything else.**
2. **`depth` (`+0x98`, signed), larger first.** Painter's order: a larger `depth` is drawn earlier, i.e. behind; a smaller or negative `depth` is drawn on top.
3. **Element name hash (`+0x2c`, unsigned), smaller first.**
   - The comparator **never returns 0**: equal hashes return −1.
   - Two siblings that tie on all three keys therefore give an inconsistent comparator (`a<b` and `b<a`), and their order is whatever `qsort` produces.
   - In practice that means same-named siblings with equal depth (HIGH).

**Notes:**
- **Scope.** Sorting is per sibling list, every frame. `depth` never reorders elements across parents: a child is always drawn after its parent and before its parent's later siblings.
- **The class key outranks `depth`.** A mask or clip child draws before every ordinary sibling whatever their depths.
- **Pass interaction.** In pass 1 the `text` draws are skipped but the order of everything else is unchanged.

### 5.3 `mouse_depth` and hit-testing — CONFIRMED (hit-pick), HIGH (geometry builder)

**Storage.** `mouse_depth` is stored at `+0x94`.
- The element initializer stores the sentinel `0x7fffffff` there (`0x00e26947`).
- The getter `0x00e27a50` returns `depth` (`+0x98`) while the sentinel is in place. So **an unset `mouse_depth` equals `depth`.**
- The setter (`+0x74` → `0x00e27aa0`) writes `+0x94` on four paths, which were not read in full.

**The hit test `0x00e2c4b0` (196 insns, read in full).** It runs once per input subscription. Its caller is `0x00e2cd79`, which was not read.

1. It requires all of the following on the subscription:
   - kind `+0x2c` = 2;
   - byte `+0x30` ≠ 1;
   - event code `+0x34` ∈ {`0x23`, `0x24`, `0x26`, `0x27`, `0x28`, `0x29`};
   - element handle `+0x38` resolving through `0x00e28c70`.
2. It reads the pointer position as two unsigned integers from `0x00dcfc90`.
3. It gets the element's quad from **`0x00e28290`** (240 insns):
   - it calls the hit-geometry builder **`0x00e26c70`** (500 insns, structure read, not every instruction) for the element's local quad;
   - it maps that quad by the *parent* chain from `0x00e272c0`;
   - it replaces any non-finite corner with the parent position (`0x00dae560` tests for an all-ones exponent).
4. If the element has a document, it maps the corners to device pixels with the **same screen fit** `0x00e1ef70`. This confirms that query-time geometry is pre-fit (§6).
5. It tests the point with **`0x00e2b4e0`** (104 insns): a parallelogram test. The projections of `(p − corner₀)` onto the two edges leaving `corner₀` must each lie in `[0, |edge|²]`, edges inclusive.
6. **On a hit:**
   - it reads `mouse_depth` through slot `+0x70`;
   - it lowers the global minimum at `0x0132c0bc` (static `0x7fffffff`);
   - it makes the element the candidate at `0x02a7caf8` if there is none, or if its `mouse_depth` is **≤** the current candidate's.

   So **the lowest `mouse_depth` wins, and ties go to the element tested later.**

**What the hit test does not consult.** The body of `0x00e2c4b0` reads neither `visible`, alpha nor the clip stack. Whether `0x00e26c70` does is OPEN.

**`0x00e26c70`, as far as it was read.**
- It reads `offset` (`+0x50`), `unscaled_size` (`+0xa8`), `scale` (`+0x98`), `rotation` (`+0x90`) and `anchor` (`+0x48`) through their getter slots.
- It uses the same sine/cosine routines and the same matrix helper.
- It recurses into is-an-`element` children.
- For an exact `group` (type id compared with cell `0x0132c66c` via `0x0132c670`), the first child's quad replaces the group's own.

  This is the same group rule as §4.4, re-implemented.

**Subscription order** (HIGH, `0x00e2b730`, 70 insns). New subscriptions are inserted into the list at `0x02a7cae0`:
- **ascending** by the `document_depth` of the subscription's document (subscription `+0x24` → document `+0x58c`), i.e. front-most document first, the reverse of draw order;
- then by **descending** subscription `+0x20`, with a special case for kind 0 / event `0x16` entries.


## 6. Validation against `screen_nw` / `screen_se` / `screen_size`

### 6.1 The finding: the getters read nothing the walk writes — CONFIRMED

The premise to test was that the walk *writes* final bounds which the three read-only getters then *read*. That premise is false, and the reason is itself evidence:
- The walk's composed state is a per-element **stack copy** (§4.2). `0x00e26a30` and `0x00e26680` store nothing into the element.
- The three getters do not read any element field. Each one **recomputes** the geometry on demand through vtable slot **`+0x2c`**, the query-time chain of §4.4.

**The getter trampolines** (U1 §4.1), each the shared null-checked forwarder shape of U1 §1.3 (13 instructions by my lister), all CONFIRMED:
- `0x00e322a0` → slot `+0x78`;
- `0x00e27b10` → slot `+0x80`;
- `0x00e2ea70` → slot `+0x88`.

**The slot bodies** (shared for 9 of the 10 classes; overrides in §0):

| Property | Body | What it does |
|---|---|---|
| `screen_nw` | `0x00e27b30` (22 insns) | `slot+0x2c(rect, 1)`, returns `(rect[0], rect[1])` as a vec2 record (type 7) |
| `screen_se` | `0x00e27b90` (22 insns) | same call, returns `(rect[2], rect[3])` |
| `screen_size` | `0x00e27c00` (35 insns) | calls the refresh hook vtable `+0x24` first (a bare `ret`, `0x00754410`, in 9 of the 10 classes; `text`'s `+0x24` is `0x00e330d0`, not read), then the same call, returns `(rect[2] − rect[0], rect[3] − rect[1])` |

**The `group` overrides** (`0x00e309b0`/`0x00e309f0`/`0x00e309e0`, 2 insns each) only zero the getter's second stack argument and jump to the shared body. The shared bodies never read that argument, so `group`'s geometry differs only through its own slot `+0x2c` (`0x00e308b0`, §4.4).

**`point`** (`0x00e333f0`) returns the constant `(1.0, 1.0)` for `screen_size`. **`text`** (`0x00e322c0`) re-runs layout first if dirty, then calls the shared body.

### 6.2 The proof that compose is the real transform code: op-for-op correspondence — CONFIRMED

Slot `+0x2c` → `0x00e28540` → `0x00e280d0` → `0x00e272c0` re-implements the walk's composition step for step. It uses the same helpers, the same matrix layout and the same order of operations:

| Step | Render time (`0x00e26680`) | Query time (`0x00e272c0` / `0x00e280d0`) |
|---|---|---|
| anchor into parent frame: `pos += M_parent · anchor` | `0x00e26783`–`0x00e267bd` (anchor read directly from `+0x64`/`+0x68`) | `0x00e2737f`–`0x00e273c1` (anchor via getter slot `+0x48` → `0x00e27790`, which reads `+0x64`/`+0x68`) |
| sin/cos of `rotation` (`+0x84`) | `0x00e266f5`–`0x00e26721`, `0x00ea2470` / `0x00ea3a70` | `0x00e273c4`–`0x00e27401`, the same two routines |
| `R = [c −s 0; s c 0]`, `S = [sx 0 0; 0 sy 0]` built on the stack | `0x00e26725`–`0x00e2677f` | `0x00e27405`–`0x00e2745a` (instruction-for-instruction the same shape) |
| `M = M · R`, then `M = M · S` | `0x00e267c0`, `0x00e267f3` (`0x0078b790`) | `0x00e2745e`, `0x00e27491` (`0x0078b790`) |
| content origin: `pos + M_full · offset` | `0x00e2681a`–`0x00e26864` → state `+0x4c`/`+0x50` | `0x00e280ec`–`0x00e2818f` (offset via getter slot `+0x50` → `0x00e27800`, which reads `+0x6c`/`+0x70`) |
| box corners `origin + M_full · {(w,0), (0,h), (w,h)}` | in the per-type draw from state `+0x34` / `+0x4c` (§4.3) | `0x00e28197`–`0x00e2827d`, with `(w, h)` = `unscaled_size` `+0x74`/`+0x78` |

The independent third copy in the hit-test builder `0x00e26c70` (§5.3) uses the same getters and helpers, and is cross-checked against the same screen fit. Three code paths, all built on `0x0078b790` and the `[c −s; s c]·diag(s)` construction, are consistent with each other only if §4.2's closed form is the engine's transform.

### 6.3 Where render-time and query-time geometry differ — CONFIRMED

These differences are what a port must reproduce if Lua's `screen_*` values are to match.

1. **The root frame.**
   - The render walk starts from the document's screen fit: matrix `diag(fitScale)`, position `fitOffset` (§4.1).
   - The query chain starts from identity and zero at the topmost is-an-`element` ancestor (`0x00e27305`–`0x00e27343`).
   - **So `screen_nw`/`screen_se`/`screen_size` are in the document's authored coordinate space** (the `+0x5a0` × `+0x5a4` canvas), **not in device pixels.**
   - Device = `doc · fitScale + fitOffset`. The engine itself does exactly this conversion before hit-testing (`0x00e2c587`–`0x00e2c678`).
   - HIGH: the walk's root is the document's root element (document `+0x594`), and that element's parent link is null or not an element, so the two chains climb the same ancestors. Not checked on a live tree.
2. **The bounds are an axis-aligned box of the transformed content box, unioned with every element child's box (recursively).**
   - So `screen_size` is **not** `unscaled_size ⊙ cumulative scale`. A rotated element reports its axis-aligned extent, and any element with children reports the union.
   - Invisible, zero-alpha and pass-skipped children are included (§2).
3. **`group`'s bounds** are its element children's union only. A childless `group` reports a zero-size box at `pos + M′ · offset`, which leaves out the group's own scale (§4.4).
4. **`point`** reports `screen_size` = `(1, 1)` always. Its `screen_nw`/`screen_se` use the shared path.

### 6.4 Worked example (by hand from the formulas; arithmetic, not a runtime capture)

**Setup.**
- Document 1280 × 720, screen 1920 × 1080 (fit to height): `fitScale` = 1.5, `fitOffset` = (0, 0).
- A `bitmap` directly under the document root: `anchor` (100, 50), `auto_offset` `c`, `unscaled_size` (64, 32), `scale` (2, 2), `rotation` 0, root at identity.

**Computation.**
- `auto_offset` `c` gives `offset` = (−32, −16).
- Query time:
  - `pos` = (100, 50) and `M` = diag(2, 2);
  - origin = (100 − 64, 50 − 32) = (36, 18);
  - far corner = (36 + 128, 18 + 64) = (164, 82).

**Results.**
- **`screen_nw` = (36, 18), `screen_se` = (164, 82), `screen_size` = (128, 64).**
- Render time draws the quad at device (54, 27)–(246, 123), the same box × 1.5.

### 6.5 The `screen_size` *setter* exists — CONFIRMED (U1 flag-0 rows are not all read-only)

U1 lists `screen_size` with a setter thunk (`0x00e309c0` → slot `+0x7c`). The slot body is a real setter for 7 of the 10 classes:

- **Shared body `0x00e27c70` (129 insns).**
  1. Clamps each requested component to at least 1e-4: the double at `0x012a2f90` and the float at `0x012a2ea4` both hold 1e-4.
  2. Reads the current box through slot `+0x2c`.
  3. Reads the current `scale` through slot `+0x98`.
  4. Writes **`new scale = old scale × requested / current screen_size`** per axis through the `scale` setter slot `+0x9c`.

  It also multiplies in the parents' cumulative scale (`0x00e274f0`, 24 insns: the product of `scale` up the `+0x24` chain), but that factor cancels algebraically. It is computed and then divided back out.

  A zero current size divides by zero. Nothing guards it.
- **`text` (`0x00e322f0`, 80 insns).** Acts only when bit 0 of byte `+0x10c` is set (returns false otherwise). It writes `unscaled_size` from the request, with each component at least 1.0 and divided by the element's own `scale` on one comparison path, then sets bit 1 of `+0x10c` (layout dirty). Not traced to every branch.
- **`group` and `point`.** Use `0x00e27bf0`, so setting fails (returns false).
- **`screen_nw`, `screen_se` and `unscaled_size`.** Their setters are `0x00e27bf0` in all 10 classes, so they are truly read-only.

This refines U1 §3.3: flag 0 means "not persisted", and `screen_size` is a **writable, computed** property.

## 7. Cross-observations

- **Element defaults (CONFIRMED, element initializer `0x00e26900`).** After the base initializer `0x00e28610` (handle serial, links cleared):

  | Property | Default |
  |---|---|
  | `anchor`, `offset` | (0, 0) |
  | `unscaled_size` | (1, 1) |
  | `rotation` | 0 |
  | `auto_offset` | 0 (`none`) |
  | `mask` | 0 |
  | `depth` | 0 |
  | `visible` | 1 |
  | `mouse_depth` | `0x7fffffff` (= follow `depth`) |
  | `tint` | (1, 1, 1) |
  | `render_mode` | 0 (`default`) |
  | `alpha` | 1 |
  | `scale` | (1, 1) |
  | `background` | 0 |

  A port can seed every element with these before applying the file's property records.
- **Scoping-note corrections.**
  - §1.2 step 1: the cull is on `visible` being **clear**.
  - §1.2 step 5: the two pushed values are **`render_mode` (+0x18)** and the **`background` flag (+0x1c)**.
  - §1.2 step 6: the byte tested is the **document's render pass**, not a per-element mask mode. Masking is driven by the per-child `mask` byte inside the child loop.
  - §1.3: slots 30/31 are the `screen_size` getter/setter, and the `0x00e28540` family is vtable slot `+0x2c` (slot 11).
  - §1.5: callback slot `+0x08` is "set mask mode" and `+0x0c` is "begin mask" (HIGH).
- **Callback slots touched by U2 code, for U3:**

  | Slot | Arguments | Called from |
  |---|---|---|
  | `+0x00` | (none) | document begin |
  | `+0x08` | (mode 0/1/2) | via `0x00e23770` |
  | `+0x0c` | (none) | mask begin |
  | `+0x10` | (16-byte rect ptr) | clip set |
  | `+0x14` | (none) | clip off |
  | `+0x18` | (`render_mode`) | per element |
  | `+0x1c` | (`background` flag) | per element |

  All are `cdecl` with the stated argument count, from the call sites above.
- **Thread-locality.** The scratch child array (TLS `+0x270`, 256 slots) and the mask mode are per-thread. The mask mode is dword `+0` of the per-thread UI record returned by `0x00e236f0`; that record's `+4`/`+8` are the screen width and height that `0x00e237e0` hands to the screen fit. Two threads rendering documents at once do not share sort scratch.
- **The 256-slot scratch limit is silent.** A sibling list that does not fit has its tail silently dropped (§1.3 step 6). The budget is shared down the current path, so deep trees with wide sibling lists can lose elements. In the 159 shipped documents this is unlikely to bite; not measured.
- **Unchecked bound in render-all.** `0x00e1f6e0` gathers active documents into a 64-entry stack array with no bound check (CONFIRMED by listing the loop), so more than 64 simultaneously active documents would overrun the stack. HIGH that the game never gets there.
- **Matrix arithmetic precision.** Products are computed in double and stored as float, with x87 elsewhere. A port using float throughout will differ in the last bits only.

## 8. OPEN items

1. **The `0x00848d90` → `0x007b3a70` side path.** What the game-side array at `0x012fced8` holds, and why its documents render in pass 1 (no `text`) followed by `bg_saints` in pass 0. Possibly a render-to-texture or pause-background pass. The relation to the `ui_interface_manager_render_task` (scoping note §1.1 OPEN) also remains open.
2. **Callback semantics** (`+0x08`/`+0x0c`/`+0x18`/`+0x1c` values, and whether `render_mode` 1 = `filter` = normal alpha blend) belong to U3/U5.
3. **Clip pop indexing** (§3.3): whether `0x00e1f6b0` re-applies the popped rectangle (a nested-clip restore bug) or the index is right. Also `0x00e30c60`'s use of `0x00e274d0` (not read) and the lack of rotation in clip rectangles. For U5.
4. **Render-state bytes `+0x18`…`+0x23` and `+0x66`…`+0x7f`** are never written by the U2 code. Whether a per-type draw uses them is for U4/U6.
5. **The hit-geometry builder `0x00e26c70` (500 insns)** was read for structure only (getters, helpers, recursion, `group` rule), not instruction by instruction. Also not read: whether it skips invisible elements; the caller `0x00e2cd79` that iterates subscriptions; the `mouse_depth` setter `0x00e27aa0`.
6. **The `text` refresh/size hooks** (vtable `+0x24` → `0x00e330d0`; layout `0x00e31c50`) are U6.
7. **Live confirmation.** Everything here is static. A runtime capture of `screen_nw`/`screen_se` for a known element, compared with §6.4-style arithmetic, would move §6.3 item 1 (document-space coordinates) from CONFIRMED-by-code to CONFIRMED-by-data.
8. **The `.czn_pc` zone-data interior / named-object resolution topic was not crossed and was not opened.** The minimap (`sr2_map`) was touched only as a vtable column in the slot dump.

## 9. Size — actual against the scoping estimate

All counts are from my recursive-descent lister, from each function's entry. They match the scoping note's figures wherever both exist: 197, 165, 72, 61, 150 and 164.

| Set | Functions | Instructions |
|---|---|---|
| The scoping note's named U2 set (`0x00e26a30`, `0x00e26680`, `0x00e26980`, `0x00e28540`, `0x00e280d0`, `0x00e272c0`) | 6 | **809** |
| Helpers needed to close the six items: state init, document render, screen fit, matrix product, `group`/`text` slot `+0x2c`, the three `screen_*` bodies + three trampolines, `screen_size` setter, `auto_offset` applier, cumulative scale, three rect helpers, document comparator, mask-mode get/set, clip pop, four callback thunks | 21 | **777** |
| **U2 closure (render + query geometry)** | **27** | **≈1.59k** |
| Hit-test add-on: `0x00e2c4b0` 196, `0x00e2b4e0` 104, `0x00e28290` 240, `0x00e26c70` 500, `0x00e2b730` 70 (the last ≈740 read structurally only) | 5 | ≈1.11k |
| Also read for context (not counted above): element accessors (~20 bodies, ~250), element initializer, `bitmap` draw path (31 + 37), `clip` draw/push (46 + 83), render-all, `0x007b3a70` (41), `0x00848d90` (41) | — | — |

**Verdict.** The "~1k" estimate is right for the named core (809). Closing the six questions needed about twice that (≈1.6k). Hit-testing for `mouse_depth` adds another ≈1.1k.

## 10. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names and no pasted pseudocode.
- Every behaviour is described in my own words from capstone listings of the executable's own bytes. Ghidra was not needed for this unit: no project copy was made, so there is nothing to delete.
- The formulas in §4.2/§6 are my algebraic restatement of the arithmetic, not decompiler output.
- Strings quoted as data are the engine's own:
  - property names;
  - enumeration names;
  - type names;
  - the metadata key `document_depth`;
  - the document name `bg_saints`;
  - the format string `%i`.
- `qsort`, `sscanf`, sine, cosine and `FLT_EPSILON` are C-runtime and library identities, not Ghidra auto-names.
- **Self-check, run on the finished file as the literal last step.** The project's standard auto-name pattern (the one quoted in U1 §7, not repeated here so that this note stays at zero hits) was run with Python `re` (scratch script `u2_check.py`): **0 hits on the whole file.** The plain-substring backstop for the ten auto-name fragments listed in U1 §7 also found **0 hits anywhere in the file**, this section included.
- No spec file was edited.
