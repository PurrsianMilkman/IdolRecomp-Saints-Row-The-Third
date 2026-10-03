# Interp note — U2: Vint shared render core (tree walk, culling, render-state inheritance, transform composition, depth sort)

**Date:** 2026-10-03 · **Unit:** U2 of `review/scoping-ui-render.md` §4 (item 3). Investigation only: no spec file was edited. For whoever folds it into `spec-lua-bindings.md` (§9, element fields) and a future Vint render spec.

**Shared conventions:** the same as `interp_vint_u1_property_tables.md` (U1) and the other `exe-notes-2026-10-01` interp notes — plain hex addresses, my own words, labels **CONFIRMED** / **HIGH** / **HYPOTHESIS**, no Ghidra auto-names and no decompiler output; property, type and metadata names are the engine's own shipped strings, quoted as data. Property names, descriptor rows, getter/setter thunks and the element-family vtable slot layout (`+0x30`…`+0xb4` the 17 `element` properties, lower slot = getter, `+0xb8`/`+0xbc`/`+0xc0` pre-draw/draw/post-draw) are cited from U1 §3.2 / §4.1 and not re-derived.

**Method (status: in progress — sections are written as each item closes).**
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

