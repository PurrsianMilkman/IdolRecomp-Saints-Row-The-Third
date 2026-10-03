# Scoping note — HUD/UI ("Vint") render path

**Date:** 2026-10-03 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.
**Question:** what turns a loaded `.vint_doc`, plus its current property values, into draw calls; how big that system is; what is already specified and what is not; and in what order to specify it.
**Method:** read-only Ghidra headless runs against a private copy of the project (`tools\gp_uiscope`, deleted afterwards). These were mainly call-graph walks, vtable reads via RTTI, and instruction counts, plus full listings of a handful of key functions. I also read `spec-vint-doc-format.md`, `spec-lua-bindings.md` §5/§9/§15/§16, `spec-lua-api-behaviour.md` §26.26 and others, `spec-render-pipeline.md` §6/§7/§18, and Team B's `sr3vintdoc` reader and `sr3render` headers.

**Size figures.** Instruction counts come from function bodies. Many vtable targets had no function defined in the shared project; for those, a function was created in the throwaway session only, so treat those counts as ±10 %.

**Confidence labels** are the project's usual ones:
- **CONFIRMED**: read directly in this pass.
- **HIGH**: inferred from structure.
- **HYPOTHESIS**: a plausible reading, not checked.

**Naming.** Class names below come from the binary's own RTTI type-descriptor strings, following the precedent in `spec-lua-bindings.md` (vint_callback_lua) and `spec-render-pipeline.md` §18.10/§22.3. They are shipped data.

---

## 0. Verdict up front

**This is real reverse-engineering work, not only an integration gap.** Three reasons:

1. **The data model is not fully specified.** The premise "Team B has the format; only the renderer is missing" does not hold.
   - `spec-vint-doc-format.md` still has §3.1, §3.2, §4 and §5 at NEEDS-EXE.
   - Its string-table layout is contradicted by Team B's own population data.
   - No whole-file walk reaches end-of-file. Team B's header states there is "no parseDocument() yet".
   - So Team B cannot yet build the element tree, let alone draw it.
2. **Everything between "element tree with property values" and "pixels" is unspecified.** That covers:
   - the per-type property tables for 11 of the 13 types;
   - render-state inheritance (transform, tint, alpha);
   - the culling rule and the depth sort;
   - the mask and clip semantics;
   - each element type's drawing behaviour;
   - the 18-slot callback contract between the UI library and the game's 2D drawing code;
   - the font file format.
3. **Only the last hop needs little work.** That hop is the D3D9 command buffer, plus texture decoding and the bitmap-sheet atlas table. It is either specified already or irrelevant, because Team B renders with D3D11 (`sr3render/device.h`).

**Rough size:**

| Part | Functions | Instructions |
|---|---|---|
| Core render path, excluding the minimap and video | 60–70 | ~10k |
| Minimap (`sr2_map`) callbacks | — | ~7k more at one call level |
| Tween/animation runtime that animates the HUD | — | ~2–4k (HYPOTHESIS, not measured) |

**Of the 13 element types:**
- 7 override the per-type draw method: `bitmap`, `text`, `gradient`, `bitmap_circle`, `clip`, `video`, `sr2_map`.
- 4 are pure containers or markers: `element`, `group`, `point`, `document`.
- 2 are animation objects: `tween`, `animation`.

---

## 1. The pipeline: from a loaded document to D3D9 draw calls

Overall, the UI library ("Vint", class family `vint_element_*`) is engine-agnostic. It walks the tree and computes state. It never draws anything itself. All drawing goes out through a **table of function pointers that the game installs at boot**. The game side turns each callback into 2D primitives for the engine's generic `rl_draw`/`rl_primitive_renderer` layer. That layer records D3D9 commands into the deferred command buffer that `spec-render-pipeline.md` already documents.

```
game frame (render callback 0x00848d90, no static caller)
  └─ 0x007b3a70  per-document loop  ──► 0x00e1f0b0  document render
                                          └─ 0x00e26a30  recursive element walk  (+ depth sort, cull, mask, clip)
                                               ├─ 0x00e26680  per-element state/transform compose
                                               └─ vtable +0xbc (slot 47) per-type draw
                                                    └─ 13 tiny thunks 0x00e22830…0x00e22a10
                                                         └─ callback table (18 slots) at 0x02317a30, via ptr 0x02a5a008
                                                              └─ game 2D wrappers 0x005d8ff0…0x005d9ca0
                                                                   └─ rl_draw 2D funcs 0x00e4af50 / 0x00e4b930 / 0x00e4be40 …
                                                                        └─ rl_primitive_renderer 0x00e530f0 / 0x00e531a0 / 0x00e525e0
                                                                             └─ rl_d3d_primitive_renderer flush 0x0048d540
                                                                                  └─ record opcode 41 (0x004763d0)
                                                                                       └─ replay 0x0049d5d0 → device vtable +0x14c (DrawPrimitiveUP)
```

### 1.1 Frame entry (game side)

- **`0x00848d90` (41 insns) — the render entry.** It has no static callers, so it is registered somewhere as a callback (HIGH). It calls callback slot +0x00 (`0x008470a0`), then the per-document loop **`0x007b3a70`** (41 insns).
- **`0x007b3a70` — the per-document loop.** For each live document it:
  - resolves the document (`0x00e1f2f0` by name, `0x00e1f330` by id, both already cited in the Lua-API work);
  - calls the document renderer `0x00e1f0b0`.
  - **CONFIRMED.**
- **The `ui_interface_manager_render_task` class.** Its RTTI vtable is at `0x01156444`. Slot 1 is a jump to **`0x007b3fd0`** (132 insns), the render-thread side of the UI.
  - **OPEN:** how it connects to `0x00848d90`. No static path was found within 4 call levels.
- **The per-frame update path, separate from render.** `0x00846ae0` calls `0x00e22630` (10 insns), which runs:
  - tween processing (`0x00e2efd0`, 169);
  - animation processing (`0x00e2f470`, 57);
  - data-item processing (`0x00e21130`, 110).

  Render does not depend on this path structurally, but it supplies the *current* property values an animated HUD shows.

### 1.2 Document render and tree walk (Vint side)

**`0x00e1f0b0` — document render (51 insns, CONFIRMED).** It:
- fetches the screen record (`0x00e1ef70`, which reads the width/height record already documented in `spec-lua-api-behaviour.md` §26.26);
- calls callback slot +0x00 through thunk `0x00e229b0`;
- walks the root element with `0x00e26a30`.

**`0x00e26a30` — the recursive element walk (197 insns, CONFIRMED by full listing).** In my own words, per element it does the following:
1. It stops if the element's hidden byte is set.
2. It stops if the inherited or own alpha fails a comparison against a float constant at `0x01122d70`. This is an alpha cull.
3. It calls a pre-draw hook (vtable +0xb8; a no-op in the base class).
4. It copies the parent's **128-byte inherited render state** into a local and composes this element into it with **`0x00e26680`** (165 insns).
5. It pushes two state values to the game through callback slots +0x1c and +0x18.
6. It handles a per-element mask mode:
   - one value checks the element's class through vtable +0x18 against a class cell at `0x0132c998`;
   - mode changes go through `0x00e237b0`/`0x00e23770` and callback slot +0x0c.
7. It calls the **per-type draw method (vtable +0xbc)**.
8. It gathers the children from the linked list (first child at element +0x1c, next sibling at +0x20). Only children whose class passes the element-base test against cell `0x0132bdd0` are kept.
9. When there are 2 or more children, it **sorts them with the C runtime's qsort**, using comparator **`0x00e26980`** (72 insns). This is the z-order (HIGH: a depth sort).
10. It recurses into each child.
11. It unwinds any clip-stack entries this subtree pushed, using document field +0x584 and `0x00e1f6b0` → callback slots +0x10/+0x14.
12. It calls a post-draw hook (vtable +0xc0).

### 1.3 Layout and transform

- **Render time: `0x00e26680` (165 insns).** It composes the element's transform and colour into the inherited state. It calls a small matrix/vector helper (`0x0078b790`, 74 insns) and two C-runtime math routines. **HIGH.**
- **Query time: the chain `0x00e28540` (61) → `0x00e280d0` (150) → `0x00e272c0` (164, recursive).**
  - It sits in vtable slot 11 of every visible element class; `group` uses its own `0x00e308b0`.
  - It walks parents recursively, which fits the computed, read-only properties `screen_size`, `screen_nw` and `screen_se` in `spec-lua-bindings.md` §9.2 (HIGH).
- **Unattributed overrides.** Shared element-base slots 30/31 (`0x00e27c00`, 35 insns; `0x00e27c70`, 129) are overridden by `group`, `text` and `point`. **HYPOTHESIS:** an anchor/offset-style property pair. Not checked.
- **Resolution dependence, already known and outside the render code.** Three pieces feed layout:
  - per-resolution property override blocks inside the file (`spec-vint-doc-format.md` §5);
  - the standard/wide layout index `0x0132c0ac`, which calls each document's `_reset()` when it flips;
  - the safe frame (`spec-lua-api-behaviour.md` §26.26).

### 1.4 Per-type draw (vtable slot 47, +0xbc)

How slot 47 was identified: it is the only slot overridden by exactly the visible types and left as the shared no-op (`0x00abd220`) by `group`, `point` and `document`. The walk calls it once per element. **HIGH CONFIDENCE.**

| Type | Draw method | Main helper(s) | Callback slot(s) used |
|---|---|---|---|
| `bitmap` | `0x00e312b0` (31) | `0x00e30470` (37) | +0x20 |
| `gradient` | `0x00e35410` (97) | `0x00e303c0` (54) | +0x28 |
| `bitmap_circle` | `0x00e34880` (38) | `0x00e304e0` (44) | +0x24 |
| `text` | `0x00e31b50` (78) | `0x00e374a0` (387) for drawing; layout `0x00e31c50` (177) → `0x00e36dd0` (509), `0x00e22b30` (363) | +0x04, +0x2c, +0x20 |
| `clip` | `0x00e30db0` (46) | `0x00e30c60` (114), `0x00e1f5c0` (83) | +0x10 / +0x14 (clip push/pop, HIGH) |
| `video` | `0x00e33a50` (37) | — | +0x38 |
| `sr2_map` | `0x00e34340` (62) | reads the callback table directly | probably +0x3c / +0x40 / +0x44 (HYPOTHESIS) |

Some shared pieces:
- `bitmap`, `gradient` and `bitmap_circle` share `0x00e23450` (59 insns, which reads the callback table) and `0x00e24150` (9).
- Bitmap resources resolve through a handle table of 24-byte records at `0x02a4e398` (`0x00e21850` and `0x00e21820`).
- Text layout calls the tagged UTF-16 string decoder `0x00e22ac0`. That decoder, and its encoder `0x00e22a30`, are already documented in `spec-lua-api-behaviour.md` (tranches 09 and 21).

Slot 4 is overridden by `bitmap`, `text`, `document`, `video` and `gradient` (`0x00e31270`, `0x00e31a00` at 114 insns, `0x00e35200`, `0x00e33a20`, `0x00e353f0`). **HYPOTHESIS:** a per-type resource-acquire hook. Not part of the draw path proper.

### 1.5 The callback table — the seam between the UI library and the game

**How it is installed (CONFIRMED).**
- `0x008489e0`, the UI bring-up already cited in `spec-lua-bindings.md` §16.1, fills a static 18-slot table at **`0x02317a30`**.
- It hands the table to `0x00e225f0`, which stores the pointer at **`0x02a5a008`**.
- Thirteen thunks of 8–36 instructions (`0x00e22830`…`0x00e22a10`) each read one slot and forward to it when it is non-null.
- Three sibling tables are installed in the same place: `0x02317a78` (15 slots, via `0x00e225e0`), `0x02317ab4` (via `0x00e22610`) and one via `0x00e22600`. Their roles are not render-related as far as this pass looked (**HYPOTHESIS:** resources, input, sound). They were not examined.

**The 18 slots.** Offsets are bytes from `0x02317a30`; instruction counts are for each function's own body. Every role is a HYPOTHESIS taken from the calling element type, except where a slot is marked otherwise.

| Slot | Game function (insns) | Called from | Likely role |
|---|---|---|---|
| +0x00 | `0x008470a0` (30) | document render, frame entry | begin document/frame |
| +0x04 | `0x00847130` (84) | text draw | text state/setup |
| +0x08 | `0x00847110` (11) | — | ? |
| +0x0c | `0x00842fe0` (1, jumps to `0x005de1b0` → `0x00e525b0`) | tree walk, mask path | primitive flush or mask switch |
| +0x10 / +0x14 | `0x00844960` (64) / `0x00842fb0` (12), both → `0x005de290` (95) | clip, clip-stack unwind | clip push/pop |
| +0x18 / +0x1c | `0x00847060` (11) / `0x00847080` (11) | tree walk, per element | inherited render-state push |
| +0x20 | `0x00847d40` (328) | bitmap, text | textured quad |
| +0x24 | `0x00847280` (691) | bitmap_circle | radial (circular) fill |
| +0x28 | `0x00848280` (377) | gradient | gradient quad |
| +0x2c | `0x00848890` (43) → `0x00b9cf20` → `0x00b9bc10` (773) | text | font glyph/metrics |
| +0x30 / +0x34 | `0x00848920` (17) / `0x00848960` (32) | not traced | ? |
| +0x38 | `0x00842ff0` (1, jumps to `0x00bdcba0`) | video | video frame |
| +0x3c / +0x40 / +0x44 | `0x00809910` (754) / `0x0080a460` (325) / `0x007dea30` (667) | sr2_map | minimap drawing — large; pulls in world/map code |

### 1.6 Game 2D wrappers → rl_draw → primitive renderer → D3D9 (CONFIRMED as a call chain)

**Game wrappers.** The large callbacks call small game helpers that forward into the engine's `rl_draw` 2D functions (vtable `0x0125b0b0`):
- `0x005d8ff0`, `0x005d9090`, `0x005d9c30`/`9c50`/`9c70`/`9c80`/`9ca0`, `0x005d9390`, `0x005d9300`, `0x005d9b00`;
- they forward to `0x00e4af50` (145), `0x00e4b930` (70), `0x00e4be40` (60), `0x00e4bf30` (60) and `0x00e4bf00`.

**Primitive renderer.** Those functions feed `rl_primitive_renderer` (vtable `0x0125b190`):
- add a primitive: `0x00e531a0` (223), `0x00e530f0` (35), `0x00e52210` (87);
- commit state: `0x00e525e0` (432).

The state commit reaches `0x00474ea0` → `0x00475790`, which records opcode `0x14` — the batched state record already in `spec-render-pipeline.md` §18.2.

**D3D9 submission (CONFIRMED end to end in this pass).**
- The D3D9 subclass `rl_d3d_primitive_renderer` (vtable `0x012a1024`) has a 313-instruction flush in slot 11, **`0x0048d540`**.
- It reaches `0x00477390` → **`0x004763d0`**, which writes **command-buffer opcode 41 (`0x29`)** with a variable payload copied into the record.
- The replay handler for opcode 41, **`0x0049d5d0`** (handler-table entry at `0x01350a9c`), calls **device vtable +0x14c, i.e. `DrawPrimitiveUP`**.
- The same flush also records opcode 34 (`0x004760d0`; replay `0x0049d410` → device +0x1a0) and opcode 35 (`0x00476130`; replay `0x0049d440` → device +0x190 and +0x198).

**New to `spec-render-pipeline.md`.** That spec documents only the indexed draw path (opcodes 40/42) and the `0x14` batch. Opcodes 41, 34 and 35 are not in it. This is a cheap side contribution to that spec, independent of the UI work.

### 1.7 Where this meets the shader-bytecode work

- The UI's draws go through `rl_primitive_renderer`'s state and material binding. That binding reaches the material-bind family already documented in `spec-render-pipeline.md` §18.1 (`0x00476f70`).
- Which `.fxo_pc` shader(s) the UI primitives use is **OPEN**. Once identified, the `.fxo_pc` container (`spec-fxo-format.md`) and Team B's bytecode tools can read them with no new RE work. Those tools are `d3d9bc_disassembler.cpp`, `d3d9bc_hlsl_translator.cpp` and `d3d9bc_ctab.cpp`, which implement `TEAM B\spec-d3d9-sm2-sm3-bytecode.md`.
- **Correction to the brief:** `spec-asm-format.md` is the `.asm_pc` streaming-manifest spec. It has no shader-bytecode material. The D3D9 bytecode material is in Team B's `spec-d3d9-sm2-sm3-bytecode.md` and Team A's `spec-fxo-format.md` / `spec-render-pipeline.md` §20.12.
- **Relevance:** probably low. Team B renders through D3D11 and cannot use the game's D3D9 shaders anyway (`sr3render/device.h`). For UI drawing, what matters is the blend and colour arithmetic, which can be read from the shader or from the render state. A port would not replay these shaders.

---

## 2. Main functions and data structures, with rough sizes

| Stage | Key addresses | Instructions (approx.) | What it does |
|---|---|---|---|
| Frame entry (game) | `0x00848d90`, `0x007b3a70`, render task `0x007b3fd0` | 41 + 41 + 132 | starts UI rendering; loops over documents |
| Document render | `0x00e1f0b0`, `0x00e1ef70`, `0x00e26630` | 51 + 48 + 25 | per-document setup; root walk |
| Tree walk | `0x00e26a30`, comparator `0x00e26980`, mask `0x00e23770`/`0x00e237b0`, clip unwind `0x00e1f6b0` | 197 + 72 + 22 + 15 | cull, state inheritance, mask, per-type draw, depth sort, recursion |
| Layout / transform | `0x00e26680`; `0x00e28540` → `0x00e280d0` → `0x00e272c0`; `0x00e27c00`/`0x00e27c70` | 165 + 375 + 164 | composes transform, tint and alpha; computes screen-space geometry |
| Per-type draw (7 types) | table in §1.4 | ~1,100 | type-specific geometry and parameters → callbacks |
| Text layout | `0x00e31c50`, `0x00e36dd0`, `0x00e22b30`, decoder `0x00e22ac0` | 177 + 509 + 363 + 44 | wrapping, alignment, `force_case`, tagged-string decode |
| Callback thunks | `0x00e22830`…`0x00e22a10` (13) | ~160 | the boundary between Vint and the game |
| Callbacks (game, excluding minimap and video) | 14 functions in §1.5 | ~1,700 own bodies | turn UI requests into rl_draw calls |
| Minimap callbacks | `0x00809910`, `0x0080a460`, `0x007dea30` | 1,746 own bodies; ~6,750 one call level deep | sr2_map drawing |
| Fonts (game) | loader `0x00b9cab0` (strings `font_header_pc`/`font_body`); glyph/metrics `0x00b9bc10`; extension `.vf3_pc` (string `0x0124ac00`, used by `0x00dcb6c0`) | 278 + 773 + 60 | font resource load and glyph lookup |
| rl_draw 2D + primitive renderer (UI-used subset) | `0x00e4af50`, `0x00e4b930`, `0x00e4be40`, `0x00e4bf30`, `0x00e530f0`, `0x00e531a0`, `0x00e52210`, `0x00e525e0` + game wrappers | ~1,600 | 2D primitive batching |
| D3D9 submission | `0x0048d540`, `0x00477390`, `0x004763d0`, replay `0x0049d5d0` | ~460 | opcode 41 → `DrawPrimitiveUP` |
| Per-frame animation (feeds values) | `0x00e22630` → `0x00e2efd0`, `0x00e2f470`, `0x00e2f3e0`, `0x00e21130`; tween/animation vtables `0x01257e24`/`0x01258164` | ~400 measured at the top; ~2–4k in total (HYPOTHESIS) | tween/animation playback |

**Data structures** (scoping identifications only; no layouts claimed):
- the per-class vtables (§3, U1);
- the 128-byte inherited render state copied per element in the walk;
- the element's child/sibling links;
- the 18-slot callback table at `0x02317a30`;
- the 24-byte bitmap-resource records at `0x02a4e398`;
- the per-type property-descriptor tables (§4, U1);
- the 32-slot type registry at `0x02a8a538` (16 bytes per slot, filled by `0x00e30590`);
- the per-type object pools (constructed with capacities such as `0x485` for tweens, `0x1f4` for text and `0x190` for documents).

**Totals.** "Measured" means each listed body was counted; the totals are sums of the counts above.

| Scope | Functions | Instructions |
|---|---|---|
| Core render path, excluding minimap/video/animation (Vint side) | ~45 | ~3.7k |
| Game callbacks | 14 | ~1.7k |
| Fonts | 3 | ~1.1k |
| rl_draw/primitive subset | ~12 | ~1.6k |
| D3D tail | ~4 | ~0.5k |
| **Core render path, total** | **~60–70** | **~8.5–10k** |
| Minimap add-on | — | ~7k at one call level; ~18k at six levels (it fans out into world/map code) |
| Animation runtime add-on | — | ~2–4k (HYPOTHESIS) |

**Context.** The whole Vint core address range `0x00e1ef00–0x00e37fff` is ~30.6k instructions in ~500 functions. It also contains the loader, the type and pool registry, and the tween and data-item systems. The Lua-native range `0x00e0c000–0x00e1eeff` (~23.7k instructions) is largely covered by the Lua-API work already. The surrounding `rl_draw`/primitive range `0x00e47f00–0x00e539ff` (~12k instructions) is a general-purpose 2D/3D draw library; the UI uses only a small part of it.

**Element types:**
- 13 are registered (`spec-vint-doc-format.md` §4 / `spec-lua-bindings.md` §5). Each has its own registration function, all called from `0x00e23910`; addresses are in §4, U1.
- Property counts read from those calls so far: `element` 17, `text` 21 (`0x15`), `tween` 16 (`0x10`), `point` 1, `document` 1. The rest were not read in this pass.

---

## 3. Specified versus missing

### 3.1 What is specified or implemented

- **The file format, partly.**
  - The header is CONFIRMED and validated by data.
  - The string pool, critical-resource and metadata sections, the element-record grammar, and the property-block offsets and byte order are all **NEEDS-EXE or contradicted** (`spec-vint-doc-format.md` §3.1–§5).
  - The executable job `teamb-requests-1.json` that should settle them is queued.
- **Team B's reader.**
  - It exposes the header, a string-table reader (the disputed layout, kept as written), and per-record decoders: critical resource, metadata, element head, override header, tagged property list (tags 1–7).
  - It has **no whole-document walk, no element tree, no application of property values and no runtime state** (`vint_doc.h`, top comment).
- **Type registry.**
  - The 13 type names.
  - The 32-byte property-descriptor record layout.
  - The setter/getter mechanism.
  - Full property lists for `element` (17) and `point` (1) only (`spec-lua-bindings.md` §9).
- **Lua-facing surface.** Handle resolution (`0x00e25890`), the property setter (`0x00e26140` → `0x00e26040`), `vint_get_property`/`vint_set_property`, `vint_is_std_res`, the safe frame, and the resolution-change `_reset()` behaviour. All in `spec-lua-api-behaviour.md` / `spec-lua-bindings.md`.
- **Textures and atlas.**
  - The PEG texture formats (`spec-texture-format.md`), implemented in Team B's `texture_upload`.
  - The `BitmapSheets` atlas table (`spec-tables-environment.md` §17.4).
- **The D3D9 command buffer** (`spec-render-pipeline.md`). Not needed for a D3D11 port.

### 3.2 What is missing, and whether it needs RE

| Gap | Needs RE? | Notes |
|---|---|---|
| A walkable `.vint_doc` (string pool, section starts, property offsets and byte order) | **Yes** — already queued | Blocks everything else |
| Per-type property tables for 11 of 13 types (names, type codes, writable flags) | **Yes, cheap** | 13 registration sites now located (§4, U1); reading tables only |
| Render-state inheritance and transform composition (anchor, offset, scale, rotation, tint, alpha; contents of the 128-byte state) | **Yes** | `0x00e26680`, `0x00e28540` family; the core of "where does it appear" |
| Visibility/alpha culling rule | **Yes, small** | inside `0x00e26a30` |
| Z-order (the depth comparator, and its behaviour on equal values, since qsort is not stable) | **Yes, small** | `0x00e26980` |
| Mask / `render_mode` semantics (blend modes, stencil-style masking) | **Yes** | `0x00e23770`/`0x00e237b0` + callbacks +0x0c/+0x18/+0x1c |
| Clip semantics (scissor or otherwise; nesting) | **Yes, small** | `clip` draw method + clip-stack unwind + callbacks +0x10/+0x14 |
| Per-type drawing: `bitmap` (UV/atlas, tint), `gradient`, `bitmap_circle` (radial fill) | **Yes** | Vint side is small; the game callbacks are 328–691 insns each |
| Hookup from a `bitmap` property to a texture (`spec-vint-doc-format.md` §8 item 7) | **Yes** | handle table `0x02a4e398` + BitmapSheets |
| Text: layout, wrap, alignment, `force_case`, glyph placement | **Yes, large** | ~1.5k insns on the Vint side |
| **Font file format (`.vf3_pc`)** | **Yes — completely unspecified** | Zero mentions of `vf3` or fonts in any Team A spec; game-side loader `0x00b9cab0` |
| The 18-slot callback contract (arguments per slot) | **Yes, cheap** | the natural interface for Team B to implement |
| Tween/animation playback and on-disk keyframe encoding | **Yes** | `spec-vint-doc-format.md` §7/§8 item 8 OPEN; needed only for animated HUD |
| `sr2_map` (minimap) | **Yes, large** | Recommend parking (§4) |
| `video` | **Yes** | Probably Bink; recommend parking |
| D3D submission, texture decode, atlas table, shader container | **No** — write code | Team B already has a D3D11 quad path (`quad_renderer`) and texture upload |

**Plain answer.** About **one sixth** of what is needed — the GPU-facing bottom and the asset formats except fonts — can simply be written. Nearly everything else has to be derived from the executable first. The very first blocker is the loader, which is not even a renderer question: Team B cannot build the tree until the `.vint_doc` NEEDS-EXE items close.

---

## 4. Proposed order of specification

The order goes by dependency first, then by how much of the screen each unit unlocks, in the same spirit as the call-count ordering of the Lua-API ranking tranches. Each unit is sized from §2.

1. **U0 — Close the loader** (the `spec-vint-doc-format.md` NEEDS-EXE items; executable job `teamb-requests-1.json` is already queued).
   - *Why first:* nothing downstream can be checked against real files until a whole-file walk lands exactly on end-of-file for all 159 documents.
   - *Size:* small to medium.
   - *Gate:* Team B's validator reaches exact end-of-file on 159/159.
2. **U1 — Property-descriptor tables for all 13 types.** Each type's registration function and the descriptor-table argument it pushes:

   | Type | Registration function | Descriptor table |
   |---|---|---|
   | `element` | `0x00e26890` | read from that call |
   | `tween` | `0x00e2eef0` | `0x0132c280` (16) |
   | `animation` | `0x00e2f910` | `0x0132c550` |
   | `group` | `0x00e30b10` | `0x0132c678` |
   | `clip` | `0x00e31080` | `0x0132c7a8` |
   | `bitmap` | `0x00e317f0` | `0x0132c870` |
   | `text` | `0x00e332d0` | `0x0132c9d0` (21) |
   | `point` | `0x00e33530` | `0x0132ccf4` (1) |
   | `video` | `0x00e34190` | `0x0132cda8` |
   | `sr2_map` | `0x00e34640` | `0x0132cf68` |
   | `bitmap_circle` | `0x00e34c60` | `0x0132d038` |
   | `document` | `0x00e35190` | `0x0132d1b4` (1) |
   | `gradient` | `0x00e35ab0` | `0x0132d268` |

   - *Why second:* these are pure table reads with a validator ready to hand. Matching the CRC-32 name hashes in real files against the tables gives a per-type property histogram. Every later unit needs to know which properties a type has.
   - *Size:* small.
3. **U2 — The shared core: tree walk, culling, render-state inheritance, transform composition, depth sort** (`0x00e26a30`, `0x00e26680`, `0x00e26980`, the `0x00e28540` family; ~1k insns).
   - *Why here:* every visible type depends on where it lands and how alpha and tint inherit. It has no dependency on fonts or animation.
   - *Validation:* the computed geometry can be checked against the read-only properties `screen_nw`/`screen_se`/`screen_size`, which Lua scripts already query.
4. **U3 — The callback contract** (18 slots: argument shapes and when each is called; ~13 thunks plus slot 0 and slots +0x18/+0x1c).
   - *Why here:* it is cheap and defines the exact interface Team B implements. Once it exists, Team B can start a D3D11 drawing backend in parallel with U4–U6.
5. **U4 — bitmap, gradient, bitmap_circle** (Vint side ~300 insns; game callbacks +0x20/+0x28/+0x24, 328/377/691 insns; the bitmap handle table and the BitmapSheets hookup).
   - *Why here:* these draw most of the HUD — meters, icons, backgrounds — and need nothing from fonts.
6. **U5 — clip and mask/`render_mode`** (the `clip` draw method, mask mode `0x00e23770`, callbacks +0x0c/+0x10/+0x14; small).
   - *Why after U4:* the semantics only show once something is drawn under them. Without them, menus and the cellphone UI overdraw.
7. **U6 — text: `.vf3_pc` font format, then text layout, then text drawing** (~1.5k Vint-side + ~1.1k font-side insns; callbacks +0x04/+0x2c/+0x20).
   - The largest single unit, and probably its own spec (a font-format spec plus a text-layout section).
   - *Why late:* it depends on U2 and U4 (glyphs reuse the textured-quad callback) and needs a new file format specified from scratch.
8. **U7 — tween/animation playback and keyframe encoding** (~2–4k insns; `spec-vint-doc-format.md` §7/§8 item 8).
   - *Why last:* static HUD frames render correctly without it. It only changes property values over time.

**Recommended to park:**
- **`sr2_map`.** Roughly 7k instructions at one call level and 18k at six; it pulls in world/map code. It is a subsystem of its own, and staying out of it also keeps this work well away from the parked `.czn_pc` zone-data topic.
- **`video`.** Most likely a Bink playback bridge.

**Not needed for the port:** the D3D9 submission tail (§1.6). The finding that opcode 41 is `DrawPrimitiveUP` (plus opcodes 34 and 35) should go to `spec-render-pipeline.md` as a small, separate note.

**Rough weight against the other candidates.** The core HUD (U0–U6) is ~10k instructions with an unusually clean architecture: one library, one callback seam, virtual dispatch per type. That makes it medium-sized and well bounded. On size alone this suggests it is smaller than AI runtime or physics/vehicle handling (not measured here), with the minimap as the main hidden cost. Its first two units (U0 and U1) also unblock Team B's already-started `.vint_doc` reader directly.

---

## 5. Notes for whoever takes it up

- **Tooling.** RTTI resolution is reliable here: the type descriptor sits 8 bytes before each `.?AV…` string, then the complete-object locator, then the vtable. Every `vint_element_*` vtable is listed below. Many slot targets have no function defined in the shared project, so dumps need `range` mode or a session-only function creation.
- **Vtables:**

  | Class | Vtable |
  |---|---|
  | `vint_object_base` | `0x01257dd4` |
  | `vint_object_tween` | `0x01257e24` |
  | `vint_object_animation` | `0x01258164` |
  | `vint_resource_bitmap` | `0x01258534` |
  | group | `0x012588a4` |
  | clip | `0x01258b44` |
  | bitmap | `0x01258e04` |
  | text | `0x01259124` |
  | point | `0x01259524` |
  | video | `0x01259974` |
  | sr2_map | `0x01259cb4` |
  | bitmap_circle | `0x01259f74` |
  | document | `0x0125a264` |
  | gradient | `0x0125a514` |

  No standalone vtable was found for `vint_element_base`; it is abstract.
- **Slot layout (HIGH):**
  - slots 0–11 are object-level;
  - slots 12–45 are the property get/set pairs from `spec-lua-bindings.md` §9.3;
  - slots 46/47/48 are pre-draw / draw / post-draw;
  - slots 49 and up are type-specific property accessors.
- **Out of bounds:** nothing here touches `.czn_pc` zone data or named-object resolution. Keep the minimap parked so it stays that way.
- Every label above is from this scoping pass only. A spec pass must re-derive each claim from the executable before recording it.

## 6. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, no pasted pseudocode. Class names (`vint_object_base`, `vint_object_tween`, `vint_resource_bitmap`, etc.)
  are the engine's own RTTI type-descriptor strings, read directly off the `.?AV…` mangled names ahead of each
  vtable, not decompiler inventions — quoted as data, same as elsewhere in this project's scoping/spec work.
- Self-check, run on the finished file: `grep -noP` with the project's standard pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`:
  **0 hits.** (Run directly — the agent that produced this note was killed by a session rate-limit before it reached
  its own self-check step; the check was performed on the finished file as the literal last step before this note
  was treated as complete.)
- No spec file was edited. The private Ghidra copy `tools\gp_uiscope` was deleted after the dumps.
