# Interp note — U4: `gradient` (the vtable `+0x130` mystery), `bitmap_circle`'s draw, `clip`'s draw and the clip pop, mask-mode and `render_mode` semantics on the game side

**Date:** 2026-10-03 · **Unit:** U4 of the Vint render-path order (`review/scoping-ui-render.md` §4 items 5/6, merged: `gradient`/`bitmap_circle` from item 5, `clip` and mask/`render_mode` from item 6). Investigation only: no spec file was edited, nothing was synced. For whoever folds it into `spec-vint-render.md` (a new §13, plus §9/§10/§11). Text layout and fonts are a separate, parallel unit and are not touched here.

**Shared conventions:** as in U1–U3 — plain hex addresses, my own words, labels **CONFIRMED** (read directly in this pass) / **HIGH** (inferred from structure) / **HYPOTHESIS** / **OPEN**, no Ghidra auto-names and no decompiler output; property, type and other strings are the engine's own shipped data, quoted as data. Below, "spec §N" means `spec-vint-render.md` §N. Cited without re-derivation:
- `spec-vint-render.md` §2.3 (the walk calls vtable `+0xbc`(state, scratch, capacity) as a `this`-call, found again this pass at `0x00e26ae2`), §4.1 (the 128-byte composed state: `+0x04` tint r/g/b, `+0x10` alpha, `+0x34`…`+0x48` the 2×3 matrix `M`, `+0x4c`/`+0x50` origin, `+0x54`…`+0x60` screen-fit scale/offset), §4.2/§4.3 (mask and clip mechanics), §12 (U3: `bitmap`'s draw `0x00e312b0`, colour packer `0x00e23450`, resource → `+0x20` helper `0x00e30470`, callback installer `0x008489e0`, table `0x02317a30`, table pointer `0x02a5a008`, per-thread UI draw context `0x00846f10`).
- U1 (`interp_vint_u1_property_tables.md`) §4.5/§4.11/§4.13: the `clip`, `bitmap_circle` and `gradient` descriptor rows and their vtable slots; §3.4: the 159-document population sweep (`interface_startup.vpp_pc` only).

**Method.** As in U1–U3, the executable's bytes were read directly through `tools/harnesses/exe_va.py` plus capstone; Ghidra was not used. Scratch scripts (session scratchpad): U2/U3's `u2dis.py`, `u3dis.py` (recursive descent with jump-table following), `u3vt.py`, `u3scan.py` (linear sweep with an operand regex), re-used unchanged; added this pass: `u4vtcmp.py` (side-by-side vtable dump of six element classes), `u4raw.py` (raw whole-image search for a 32-bit value, i.e. every stored pointer to an address), `u4vcall.py` (whole-`.text` linear sweep for virtual dispatch through one vtable offset, both `call [reg+off]` and the load-then-`call reg` form). `u4vcall.py` was validated by running it for `+0xbc`: it finds the walk's one draw dispatch (`0x00e26ae2` → `0x00e26aef`) and nothing else in the Vint range.

---

## 1. `gradient` — the corner colours are never drawn — CONFIRMED (reachability), HIGH (cause)

### 1.1 `gradient`'s vtable against `bitmap`'s

`gradient`'s vtable `0x0125a514` (RTTI locator pointer at `0x0125a510` → `0x012be9f4`, whose type descriptor names `.?AVvint_element_gradient@@`, decoded this pass) has **77 slots, `+0x000`…`+0x130`**; the next dword (`0x0125a648`) is the start of the ASCII string `gradient`. `bitmap`'s vtable `0x01258e04` has 60 slots, `+0x000`…`+0x0ec` (its `+0x0f0` is the next class's RTTI locator pointer). Read slot by slot this pass, CONFIRMED:

| Slot | `bitmap` | `gradient` | What the `gradient` body does |
|---|---|---|---|
| `+0x00` | `0x00e318b0` | `0x00e35b20` (44) | Vector-deleting destructor; **identical code to `bitmap`'s except the element stride, `0x118` instead of `0xd8`**. So a `gradient` element is `0x118` bytes, i.e. `bitmap`'s `0xd8` plus 64 bytes = 16 floats. It restores `bitmap`'s vtable, then `element`'s (`0x01257dd4`), around the shared teardown `0x00e24930`. |
| `+0x04` | `0x00e31220` (24) | `0x00e35370` (27) | Initialiser: calls `bitmap`'s `0x00e31220` first, and if that succeeds **sets all 16 floats `+0xd8`…`+0x114` to 1.0**. Default = four white, fully opaque corners. |
| `+0x0c` | `0x00e34870` | `0x00e35400` | A 1-instruction `jmp 0x00e34870` — same behaviour as `bitmap`. |
| `+0x10` | `0x00e31270` | `0x00e353f0` | A 1-instruction `jmp 0x00e31270` — same behaviour as `bitmap`. |
| `+0x18` | `0x00e311e0` | `0x00e35330` (13) | Is-a test: true for `gradient`'s own id (`0x0132d25c`), `bitmap`'s (`0x0132c864`) and `element`'s (`0x0132bdcc`). |
| `+0x1c` | `0x00e31210` | `0x00e35360` (3) | Returns `gradient`'s type id. |
| `+0x20`…`+0xec` | — | **identical to `bitmap`** | Includes the pre-draw/draw/post-draw slots `+0xb8`/`+0xbc`/`+0xc0` = `0x00754410` / **`0x00e312b0`** / `0x00754410`, and `bitmap`'s five property pairs `+0xc8`…`+0xec`. |
| `+0xf0`…`+0x12c` | (none) | 16 accessors | The 8 `gradient` property pairs (below). |
| **`+0x130`** | (none) | **`0x00e35410`** (97) | The gradient draw body (§1.3). **The last slot of the vtable.** |

**Element fields, CONFIRMED from the 16 accessor bodies** (getter = lower slot, setter = higher, per U1 §3.2):

| Field | Property | Getter → body | Setter → body |
|---|---|---|---|
| `+0xd8`/`+0xdc`/`+0xe0` | `gradient_nw` (r, g, b) | `+0xf0` → `0x00e35570` | `+0xf4` → `0x00e32f20` |
| `+0xe4`/`+0xe8`/`+0xec` | `gradient_ne` | `+0xf8` → `0x00e355d0` | `+0xfc` → `0x00e35630` |
| `+0xf0`/`+0xf4`/`+0xf8` | `gradient_sw` | `+0x100` → `0x00e35680` | `+0x104` → `0x00e356c0` |
| `+0xfc`/`+0x100`/`+0x104` | `gradient_se` | `+0x108` → `0x00e35710` | `+0x10c` → `0x00e35750` |
| `+0x108` | `alpha_nw` | `+0x110` → `0x00e357c0` | `+0x114` → `0x00e35800` |
| `+0x10c` | `alpha_ne` | `+0x118` → `0x00e35840` | `+0x11c` → `0x00e35880` |
| `+0x110` | `alpha_sw` | `+0x120` → `0x00e358a0` | `+0x124` → `0x00e35900` |
| `+0x114` | `alpha_se` | `+0x128` → `0x00e35940` | `+0x12c` → `0x00e35980` |

(The `gradient_nw` pair, `0x00e35570`/`0x00e32f20`, is the same code as `text`'s `shadow_tint` pair — both classes keep that vec3 at `+0xd8`, so the toolchain folded the identical bodies. The alpha setters convert through `0x0071b9b0`, as `text`'s `shadow_alpha` setter does.)

### 1.2 Nothing ever dispatches through slot `+0x130`

- **`0x00e35410` has exactly one stored reference in the whole image**: the vtable slot itself (`0x0125a644`, raw 32-bit search with `u4raw.py`). No direct `call`/`jmp` to it exists anywhere in `.text` (whole-`.text` sweep).
- **No virtual dispatch through offset `+0x130` targets a Vint element anywhere in the executable.** The whole-`.text` sweep (`u4vcall.py 0x130`) finds four load-then-call sites and no `call [reg+0x130]`/`jmp [reg+0x130]` at all. Three of the four are unrelated: `0x0040d4b7`/`0x0040d518` call a plain function-pointer **field** at object `+0x130`, `cdecl` with four pushed arguments and `add esp, 0x10` afterwards (not a `this`-call); `0x00a0c31f` loads an object **pointer** from `+0x130` and dispatches through *its* vtable `+0x60`. The fourth, `0x00e32ba1`, is inside `0x00e32b90`, which is `text`'s `line_frame_e` **getter trampoline** (U1 §4.7 row 15: getter thunk `0x00e32b90` → `+0x130`); it is reached only through `text`'s descriptor table and pushes two arguments, whereas `0x00e35410` takes one (`ret 4`) — it is never applied to a `gradient`.
- **The `+0x28` chain is reachable only from `0x00e35410`.** Its helper `0x00e303c0` (54) has no stored pointer and one caller (`0x00e35558`, inside `0x00e35410`); the `+0x28` thunk `0x00e22890` (36) has no stored pointer and one caller (`0x00e30453`, inside `0x00e303c0`). Every reference to the table pointer `0x02a5a008` in the executable was listed (`u3scan.py` over all of `.text`: one store, `0x00e225f4`, and 19 reads) and each read's slot offset checked: the others use `+0x00`, `+0x04`, `+0x08`, `+0x0c`, `+0x10`/`+0x14`, `+0x18`, `+0x1c`, `+0x20`, `+0x24`, `+0x2c`, `+0x38`, `+0x3c`, `+0x40`, `+0x44`, or only null-check the pointer (`0x00e2391c`); **only `0x00e22890` touches slot `+0x28`**. The game installs slot `+0x28` = `0x00848280` (store at `0x00848bdf`) and nothing else references `0x00848280` or the slot's address `0x02317a58`.

**So in this build, the walk draws a `gradient` with `bitmap`'s `0x00e312b0`, and the gradient body, its helper, its callback thunk and the game's `+0x28` body are all dead code.** CONFIRMED to the limit of a linear sweep: a dispatch whose offset is computed at run time rather than encoded in the instruction cannot be excluded by this method, but nothing in the Vint code (all vtable dispatches there use constant offsets) suggests one.

**Why — HIGH.** The walk's draw slot `+0xbc` is a three-argument `this`-call (`ret 0xc`: state, scratch, capacity). `0x00e35410` is a **one-argument** `this`-call (`ret 4`: state only) placed in a **brand-new slot appended after `gradient`'s last property accessor**, not in `+0xbc`. That is exactly how a C++ compiler lays out a derived-class virtual whose parameter list does not match the base-class virtual it was meant to override: it becomes a separate virtual (an overload that hides rather than overrides), and the inherited `+0xbc` stays `bitmap`'s. The source-level intent cannot be read from the binary, but the layout fits nothing else as well.

### 1.3 What the dead gradient path would have drawn (read for the record, CONFIRMED)

**`0x00e35410` (97 insns, `this`-call, `ret 4`, one argument = the composed state).**
1. Resolves the image exactly as `bitmap` does (`0x00e24150` on element `+0xa0`).
2. Builds four corner colours, **each corner's rgb multiplied component-wise by the state tint and each corner alpha by the state alpha**: nw = `+0xd8…` × tint, `alpha_nw` (`+0x108`) × alpha; likewise ne (`+0xe4…`, `+0x10c`), sw (`+0xf0…`, `+0x110`), se (`+0xfc…`, `+0x114`).
3. If there is no image, returns (nothing sent).
4. Sends the **plain composed tint/alpha** through `0x00e23450` (callback `+0x04`, set current colour) — the same call `bitmap` makes, although the `+0x28` body below never reads the current colour back.
5. Calls `0x00e303c0` on the resource with `(&M, &origin, &source_nw, &source_se, &nw rgb, &ne rgb, &sw rgb, &se rgb, a_nw, a_ne, a_sw, a_se)`.

**`0x00e303c0` (54 insns, `ret 0x30`)** is `bitmap`'s `0x00e30470` (spec §12.1) widened by eight arguments: returns if the texture handle is `−1`; copies `source_nw` and, for a sheet-packed image (mode 1), adds the sub-image's page position; then calls thunk `0x00e22890` with 13 `cdecl` arguments `(texture, &M, &origin, &adjusted source_nw, &source_se, &nw rgb, &ne rgb, &sw rgb, &se rgb, a_nw, a_ne, a_sw, a_se)`. The thunk re-pushes all 13 unchanged and calls slot `+0x28` (it is a `call` + `add esp, 0x34`, not a tail jump, because of the argument count).

**Game side `+0x28` = `0x00848280` (377 insns), structure CONFIRMED, quad arithmetic HIGH by correspondence.** It is the `+0x20` textured-quad body (spec §12.4) with one change:
- Same prologue: binds the texture (`0x005d8ff0`), takes `T = (10, 10)` for a `−1` handle or reads the size through `0x00dcf390`, computes the same normalised-inverse-of-`M` terms that end up multiplied by zero (the `xorps xmm0, xmm0` operand at `0x008483a3`), divides `nw`/`ext` by `T` for the UVs, and builds the same four corners `o`, `o+M·(ext.x,0)`, `o+M·(0,ext.y)`, `o+M·(ext.x,0)+M·(0,ext.y)`; submits a 4-vertex strip through `0x005d9c30(&context[+0x10])` / `0x005d9c50(4, 0)` / `0x005d9c70`. I matched this against U3's `+0x20` listing by structure, not line by line (HIGH).
- **The difference, CONFIRMED:** there is **no read-back of the current colour** (no call to `0x005d9090`). Instead each of the four 28-byte vertices gets its **own** colour from the per-corner arguments: vertex 0 (`+0x18`…`+0x1b`) from nw rgb / `a_nw`, vertex 1 (`+0x34`…) from ne / `a_ne`, vertex 2 (`+0x50`…) from sw / `a_sw`, vertex 3 (`+0x6c`…) from se / `a_se`; each channel ×255 (`0x012a2d88`), truncated (`cvttsd2si`), low byte kept, **no clamp**, stored **b, g, r, a** (the `D3DCOLOR` order, as in `+0x20`). So the intended gradient is a bilinear (Gouraud) colour interpolation across the textured quad, modulating the texture.

### 1.4 Consequences for a reimplementation

- **Shipped behaviour (CONFIRMED by reachability): a `gradient` element draws exactly like a `bitmap`** — one textured quad from `image`/`source_nw`/`source_se`, uniformly coloured by the composed tint/alpha; `gradient_nw`…`alpha_se` have no visible effect; without an `image`, nothing is drawn at all (the same gate as `bitmap`, spec §12.1).
- `gradient` still allocates and initialises the 16 corner floats (all 1.0), and Lua can read and write them through the normal property calls (the accessors work); writes are simply never consumed.
- Population: **no `gradient` element exists** in the 159 swept documents (U1 §3.4), and no shipped Lua script in the session's extraction (`extracted_lua`, 205 files) names any of the eight corner properties (grep, this pass). So the dead path is not hiding a visible difference in the swept content. `interface.vpp_pc` was never swept (U1 OPEN item 8), so "no `gradient` in shipped data" is not claimed globally.
- A faithful port should **draw `gradient` as `bitmap`**. A port that "fixes" it by honouring the corner colours (per §1.3) would diverge from the shipped game wherever a `gradient` element does exist in unswept data.
- **Scoping-note correction:** `review/scoping-ui-render.md` §1.4 lists `0x00e35410` as `gradient`'s draw method and §1.5 lists slot `+0x28` as "gradient quad" fired by `gradient`. The first is the intended draw but not the one the walk calls; the second is never fired. `spec-vint-render.md` §11 item 15 ("how the corner colours reach `+0x28`") is answered: **they do not**.

---

## 2. `bitmap_circle`'s draw — a textured pie made of `num_wedges` triangles — CONFIRMED

### 2.1 The draw method `0x00e34880` (38 insns, `bitmap_circle` vtable `0x01259f74` slot `+0xbc`)

`this`-call, `ret 0xc` (state, scratch, capacity — only the state is used, as for `bitmap`). Pre-/post-draw slots `+0xb8`/`+0xc0` are the shared bare `ret` `0x00754410`.
1. Resolves the image exactly as `bitmap` (`0x00e24150` on element `+0xa0`). **No image → no draw**, not even the colour.
2. **Colour:** `0x00e23450(&state.tint, state.alpha)` → callback `+0x04` (spec §12.1: ×255, truncate, r,g,b,a, no clamp).
3. Calls `0x00e304e0` on the resource with `(&state.M, &state.origin, &element[+0xb8], &element[+0xc0], element float +0xd0, element float +0xd4, element dword +0xd8)`.

**`0x00e304e0` (44 insns, `ret 0x1c`)** is `bitmap`'s `0x00e30470` (spec §12.1) with three extra arguments: returns if the texture handle is `−1`; copies `source_nw` and, for a sheet-packed (mode 1) image, adds the sub-image's page position; calls thunk **`0x00e22900` → callback slot `+0x24`** with 8 `cdecl` arguments **`(texture, &M, &origin, &adjusted source_nw, &source_se, start_angle, end_angle, num_wedges)`**. The thunk (26 insns) re-pushes all 8 unchanged and calls the slot (`call` + `add esp, 0x20`). As for `bitmap`, only `source_nw` is shifted into page coordinates; `source_se` is an extent.

### 2.2 Element fields and defaults — CONFIRMED from the accessors and the initialiser

| Field | Property (U1 §4.11) | Getter → body | Setter → body | Default (initialiser `+0x04` → `0x00e34800`) |
|---|---|---|---|---|
| `+0xa0` (16-byte record) | `image` | `+0xc8` → `0x00e348f0` | `+0xcc` → `0x00e34920` (66) | none |
| `+0xb8`/`+0xbc` (int32) | `source_nw` | `+0xd0` → `0x00e349f0` (`bitmap`'s) | `+0xd4` → `0x00e314c0` (`bitmap`'s) | (0, 0) |
| `+0xc0`/`+0xc4` (int32) | `source_se` (an extent) | `+0xd8` → `0x00e31560` (`bitmap`'s) | `+0xdc` → `0x00e315c0` (`bitmap`'s; returns false, U3) | (0, 0) |
| `+0xc8`/`+0xcc` (int32) | image width/height | — | written by the image setter via `0x00e30360` | — |
| `+0xd0` (float) | `start_angle` | `+0xe0` → `0x00e34a30` | `+0xe4` → `0x00e34a70` | 0 |
| `+0xd4` (float) | `end_angle` | `+0xe8` → `0x00e34a90` | `+0xec` → `0x00e34af0` | **2π** (`0x012a2d4c` = 6.28319) |
| `+0xd8` (u32) | `num_wedges` | `+0xf0` → `0x00e34b10` | `+0xf4` → `0x00e34b30` | **16** |

The initialiser also runs the shared element initialiser (`0x00e26900`), clears the image record and sets `scale` = (1, 1), like `bitmap`'s.

Accessor behaviour, CONFIRMED:
- **`start_angle`/`end_angle` setters** convert through `0x0071b9b0` and store the float as is: no wrap, no clamp, no ordering check.
- **`num_wedges` setter** converts through `0x0067b160` and stores the 32-bit result: **no clamp, no minimum**.
- **`image` setter `0x00e34920`** accepts a value record of type 2 (handle), 4 (name) or `0xa` (resolved image), like `bitmap`'s, then **always** resets `source_nw` = (0, 0) and `source_se` = (w, h) — `bitmap_circle` has no `custom_source_coords` — sets `unscaled_size` = (w, h), calls the resource's vtable `+4`, and re-applies `auto_offset` (vtable `+0xc4`). It does not touch `scale`.
- The `source_*` accessors are `bitmap`'s own bodies (same field offsets), so the `source_nw`/`source_se` rounding and upper-clamp rules of spec §12.1 apply unchanged.

### 2.3 The game side: slot `+0x24` = `0x00847280` (691 insns) — structure CONFIRMED, every vertex formula below read from the listing

Arguments `(texture, &M, &o, &nw, &ext, start, end, n)` with `nw`/`ext` int32 pairs (`n` = `num_wedges`).

1. **Texture and colour.** Binds the texture (`0x005d8ff0`), then **reads back the current colour** (`0x005d9090`, set by `+0x04` in §2.1 step 2) and packs it once, **b, g, r, a** — the same read-back and `D3DCOLOR` packing as the `+0x20` quad (spec §12.4). Every vertex of the pie gets this one colour.
2. **Texture size `T`**: `(10, 10)` for a `−1` handle, else `0x00dcf390`, exactly as in `+0x20`.
3. **The same dead inverse-of-`M` arithmetic** as `+0x20` (its result is multiplied by an `xorps`-zeroed operand), then the same UV rectangle: `u0 = nw.x/T.w`, `du = ext.x/T.w`, `v0 = nw.y/T.h`, `dv = ext.y/T.h` — **no half-texel offset**.
4. **Centre.** `c = o + ½·(M·(ext.x, 0) + M·(0, ext.y))` — the centre of the content box `ext`, the same box `+0x20` would fill. Centre UV = `(u0 + ½du, v0 + ½dv)`, the centre of the source rectangle.
5. **Angle table.** `step = (end − start) / n`, with `n` converted as an **unsigned** 32-bit integer (the `fild` is followed by a `+2³²` fix-up when the sign bit is set, `0x008476ef`, constant `0x012a3078`). A table of **`n + 1`** 24-byte entries is allocated **on the stack** (`(3n + 3)·8` bytes through the 16-byte-aligning stack-allocation probe `0x00ea2ca0`). Entry `k` is the 2×3 rotation matrix `R(a_k) = [cos −sin 0; sin cos 0]` for `a_0 = start`, `a_{k+1} = a_k + step` — an accumulated float sum, so the last angle is `start + n·step` up to rounding, not exactly `end`. Sine and cosine are the C-runtime routines `0x00ea2470` / `0x00ea3a70`, the pair the compose step uses for `rotation` (U2: HIGH that the first is sine). So **angles follow `rotation`'s convention: radians, 0 = the +x axis (east), increasing clockwise on screen** (y down).
6. **Submission.** `0x005d9c80(&context[+0x10])` (→ `rl_draw` `0x00e4bf30`, the list-begin call also used by glyph runs, spec §12.4), then `0x005d9ca0(3n, 0)`, which divides by 3 (`imul 0x55555556`) and asks `rl_draw` `0x00e4bff0` for **`n` triangles — a triangle list, 3n vertices, not a fan or strip**. If no vertex memory comes back, or `n` is 0, nothing is written. The routine always ends with `0x005d9c70` (→ `rl_draw` `0x00e4bf20`).
7. **Per wedge `k` (0 … n−1), three 28-byte vertices** (x `+0x00`, y `+0x04`, u `+0x10`, v `+0x14`, colour `+0x18` — the `+0x20` layout):
   - **v0 = centre**, at `c` with the centre UV;
   - **v1 = rim point for `a_k`**, **v2 = rim point for `a_{k+1}`**, where the rim point for angle `a` is
     - **position** `= c + (ext.x/2 · w.x, ext.y/2 · w.y)` with `w = M · R(a) · (1, 0)` — i.e. the unit vector at angle `a`, pushed through the element's matrix, then scaled **per screen axis** by the half extents;
     - **UV** `= (u0 + (½ + ½cos a)·du, v0 + (½ + ½sin a)·dv)` — the point at angle `a` on the ellipse inscribed in the source rectangle.

**So the primitive is a pie / sector: `n` independent triangles centre → rim(a_k) → rim(a_{k+1}), covering the angle range `start … start + n·step`, textured with the inscribed disc of the source image (the texture is not rotated by the angles — each rim UV sits under its own rim position), all in the one current colour.** CONFIRMED. A full circle (`start` = 0, `end` = 2π, the defaults) is a 16-sided polygon; `end_angle` has 26 records in the swept documents, and `cmp_common.lua` sets it from Lua (`vint_set_property(…, "end_angle", …)`, the respect meter) to sweep a radial fill.

### 2.4 Consequences and quirks a reimplementation must copy (or consciously drop)

- **Gate:** no image → no draw (Vint side); `−1` texture → no draw (helper); `num_wedges` 0 → nothing drawn (the 1-entry angle table is still built; `step` is `(end − start)/0`, never used).
- **`end < start` is legal:** `step` goes negative and the wedges sweep anticlockwise. `|end − start| > 2π` overlaps itself (no wrap). Nothing clamps.
- **Stack-sized hazard (HIGH):** the angle table lives on the stack and `num_wedges` is unclamped, so a very large value (on the order of 40,000+ for a typical 1 MB thread stack) would overrun the stack before `rl_draw` is even asked for vertices. The 26 authored `num_wedges` values in the swept documents were not read this pass (OPEN, low priority); a port should clamp or heap-allocate rather than copy the hazard.
- **The matrix's translation column enters the rim twice** (the rim term is computed as `2·(M·R(a))·(½, 0)` including the translation column). It is 0 in practice (U2 §4.1), so this has no visible effect; noted only so a line-by-line port does not "fix" a non-issue.
- **Ellipse axes are screen-aligned, not element-aligned (CONFIRMED from the formula, consequence HIGH).** The half extents multiply `w` **after** `M`, per screen axis. For a circular source (`ext.x = ext.y`) under a uniform scale this is exactly a rotated circle, as expected. But with `ext.x ≠ ext.y` **and** a rotation in `M`, the shape is not the element's own ellipse rotated: it is `M·R(a)·(1,0)` stretched along screen x/y — the outline will not match the `+0x20`-style quad the same element would occupy, and the position no longer matches the UV mapping. Under a non-uniform scale with no rotation it is still correct (the matrix is diagonal). A port that computes the rim as `c + M·(ext.x/2·cos a, ext.y/2·sin a)` — the "obvious" formula — matches the shipped result only when `M` is diagonal or `ext` is square.
- **Radii scale twice?** — No: `w` carries `M`'s scale once and the half extents are in texels (content-box units), so for `M = diag(s, s)` the radius is `s·ext/2`, the same size as the bounding quad. CONFIRMED by the arithmetic.
- **Bounds:** the query-time geometry (`spec-vint-render.md` §7) still uses `unscaled_size` (= image size), i.e. the full box, whatever the angles — a partial pie reports the full square's bounds.
- **No half-texel offset**, **no per-vertex colour**, **no anti-aliasing** of the rim: the edge is the `n`-gon's straight chords.

---

## 3. `clip`'s draw, the clip stack, and the pop — CONFIRMED (including the nested-clip restore defect)

### 3.1 Element fields and defaults (`clip` vtable `0x01258b44`, 54 slots `+0x00`…`+0xd4`)

| Field | Property (U1 §4.5) | Getter → body | Setter → body | Default (initialiser `+0x04` → `0x00e30c30`) |
|---|---|---|---|---|
| `+0xa0`/`+0xa4` (floats) | `clip_size` | `+0xc8` → `0x00e30e50` | `+0xcc` → `0x00e30e90` (30) | **(20, 20)** (`0x01116990` = 20.0) |
| `+0xa8` (byte) | `clip_enabled` | `+0xd0` → `0x00e30f00` | `+0xd4` → `0x00e30f50` (bool via `0x007cb5b0`) | **1 (on)** |

- **The `clip_size` setter also writes `unscaled_size`** (`+0x74`/`+0x78`) = the same value, so a `clip`'s query-time bounds (`spec-vint-render.md` §7) follow `clip_size`. A value record of the wrong type stores (0, 0) into both. It always returns true. It does **not** re-apply `auto_offset` (no call through `+0xc4`), so an `auto_offset`-derived `offset` is only refreshed when something else re-applies it (HIGH; load order of the two records decides).
- `clip`'s pre-/post-draw slots are the shared `ret` `0x00754410`; `+0x10` (the per-type resource hook) is also the bare `ret`.

### 3.2 The draw method `0x00e30db0` (46 insns, `ret 0xc`)

1. **Gate: `clip_enabled` must be exactly 1** (`cmp byte [+0xa8], 1`). Otherwise nothing at all happens — no push, no callback.
2. **Document-space rectangle** from `0x00e30c60` (114 insns, `this` = the clip element, one out-pointer):
   - `p` = the element's **query-time pivot position** via `0x00e274d0` → `0x00e272c0` (the U2 pos/M builder, document coordinates, identity at the topmost ancestor);
   - `k` = the **cumulative scale** via `0x00e274f0` (the product of `scale` up the `+0x24` parent chain, own scale included; 24 insns, recursive);
   - `q0 = p + offset ⊙ k`, `q1 = q0 + clip_size ⊙ k` (component-wise; `offset` read straight from `+0x6c`/`+0x70`);
   - sorts each axis (`min`/`max`, so a negative scale still gives a well-ordered box), then **rounds all four edges to integers, half away from zero** (`0x00dad900`, ±0.5 then truncate).
3. **To device pixels** with the composed state's screen fit (`+0x54`/`+0x58` scale, `+0x5c`/`+0x60` offset): `x0 = trunc(x0·fs.x + fo.x)`, `y0 = trunc(y0·fs.y + fo.y)` through the C-runtime float-to-int routine `0x00ea2560` (`cvttsd2si`: **truncation toward zero**), but `x1 = round(x1·fs.x + fo.x)`, `y1 = round(y1·fs.y + fo.y)` through `0x00dad900` (**round half away from zero**). So the near edges are truncated and the far edges rounded: compared with rounding all four edges the same way, the device rectangle can come out up to one pixel larger. CONFIRMED.
4. **Push**: `0x00e1f5c0(&rect)` on the owning document (element `+0x30`).

**Rotation — the spec's "not rotation-aware" claim, refined (CONFIRMED):** the pivot `p` *does* follow every ancestor's rotation (it is the full query-time `Σ M·anchor` position), but the box hung off it is built from `offset ⊙ k` and `clip_size ⊙ k` only — no rotation and no shear of `offset` or of the box, from the element's own `rotation` or any ancestor's. Contrast the render-time content box, whose origin is `pos + M·offset` (rotated). So under a rotated ancestor a `clip`'s rectangle stays axis-aligned and its `offset` is applied unrotated: it no longer coincides with where the same `offset`/size would draw. The matrix `M` in the composed state is never read.

### 3.3 Push `0x00e1f5c0` (83 insns, `this` = document, `ret 4`) — CONFIRMED instruction by instruction

The stack is 8 entries of 16 bytes at document `+0x08` (entry `i` at `+0x08 + 16·i`: `x0, y0, x1, y1` as int32), count at document `+0x584`. With `c` = the count on entry:
- **`c ≥ 8`: nothing is stored and nothing is sent, but the count is still incremented.** The 9th and deeper nested clips are silently ignored (the scissor stays as it was), and the bookkeeping stays balanced for the pops.
- **`c = 0`:** entry 0 = the rectangle, unchanged.
- **`c > 0`:** entry `c` = the intersection of the rectangle with entry `c − 1`: `x0 = max`, `y0 = max`, `x1 = min`, `y1 = min`. **If the two do not overlap** (any of `new.x0 > top.x1`, `top.x0 > new.x1`, `new.y0 > top.y1`, `top.y0 > new.y1`), entry `c` is **(0, 0, 0, 0)**. Touching edges (equal coordinates) count as overlapping and give a zero-width rectangle.
- Then (for `c < 8`) **entry `c` is sent to callback slot `+0x10`** (thunk `0x00e22950`), and the count becomes `c + 1`.

So after a push the current clip is always entry `count − 1`.

### 3.4 Pop `0x00e1f6b0` (15 insns, `this` = document) — CONFIRMED instruction by instruction; settles `spec-vint-render.md` §11 item 3

1. If the count is 0, return.
2. `count ← count − 1` (stored).
3. If the new count is **0**: tail-jump to thunk `0x00e22970` → **callback slot `+0x14`** (clip off). (The thunk tests slot `+0x10` for null but jumps through `+0x14` — harmless since the game installs both.)
4. Else if the new count is **≥ 8**: return, nothing sent (matches the push's overflow rule).
5. Else: **send entry `[new count]` to callback slot `+0x10`**.

By §3.3, entry `[new count]` is the entry **that was just popped** (the top before the pop was `count − 1` = new count). The entry beneath it, `[new count − 1]`, is never re-sent. **The off-by-one is real**, not an artefact of the walk's indexing: it is in the pop body itself.

**How the walk drives it** (`0x00e26a30`, re-read this pass, `0x00e26b4d`–`0x00e26c0b`): after an element's own draw, the walk records the document's count; after the element's whole child loop it calls the pop `(count − recorded)` times. The last pop therefore sends entry `[recorded]` (when `recorded > 0`), where a correct restore would send `[recorded − 1]`. And the callback (§3.5 below) sets the scissor absolutely; it is not itself a stack. **Effect, CONFIRMED as the code's behaviour (HIGH that it is unintended):** in

> parent → `clip` C1 (rect A) → children [`clip` C2 (rect B), Y]; C1's later sibling X

C1 pushes A; C2 pushes A∩B; Y is drawn under A∩B (correct — later siblings of a `clip` are clipped by it, §4.3 of the spec); C1's unwind pops 2 → 1 and **re-sends A∩B**; **X, which should be clipped to A, is clipped to A∩B**; the parent's unwind pops 1 → 0 and turns clipping off. Only nesting of a `clip` inside a `clip`'s subtree with drawable content after the inner subtree under the outer clip exposes it; a single level of clip is unaffected (its unwind goes to count 0 → clip off). How many shipped documents nest clips was not checked (19 `clip_size` records in the swept 159 documents, U1 §4.5) — OPEN.

**The unwind is skipped inside mask subtrees (CONFIRMED, new).** The walk's child loop has two branches on the element's own composed mask flag (state `+0x14`, `0x00e26b42`): the normal branch does mask switching **and** the clip save/unwind; the inside-a-mask-subtree branch (`0x00e26c22`–`0x00e26c48`) just recurses — **no clip unwind at all**. So a `clip` anywhere under a `mask` element is not popped by its own parent; it stays pushed until the unwind of the nearest ancestor that is *not* inside a mask subtree, i.e. the parent of the outermost `mask` element. In between, it also clips that parent's remaining (masked, mode-1) children. HIGH that this is incidental rather than designed.

### 3.5 Game side: `+0x10` and `+0x14` are absolute scissor set/reset — CONFIRMED to the `rl_draw` boundary

- **`+0x10` = `0x00844960` (64 insns)**: clamps the incoming `x0, y0, x1, y1` into the UI bounds rectangle held in `0x02317bb0`/`0x02317bb4`/`0x02317bb8`/`0x02317bbc` (min x, min y, max x, max y), then calls `0x005de290(x0, y0, x1, y1, 0)`.
- **`+0x14` = `0x00842fb0` (12 insns)**: `0x005de290(bounds…, 0)` — the scissor goes back to the **whole UI bounds**, not "off".
- **The bounds** are written by the UI frame function `0x00848e40` before documents render: `(0, 0, screen width, screen height)` from the per-thread screen-size record `0x00e237e0` (`0x00848e8f`–`0x00848eaf`; two alternative assignments at `0x00848f8c`… and `0x00848fca`… on other paths of the same function were not followed).
- **`0x005de290` (95 insns)**: with its last argument 0 (the only value the UI passes), it forces `x1 ≥ x0` and `y1 ≥ y0` (so an empty or inverted rect becomes zero-area at its near corner) and hands the 16-byte rect to the per-thread render object (TLS `+0x674`) vtable `+0x20` — HIGH: the scissor-rectangle setter; not followed into `rl_draw`. With a non-zero last argument it would first clamp to the render target's size; the UI never does.
- **The document begin callback `+0x00` (`0x008470a0`) also resets the scissor to the bounds** (`0x00847107`), so every document starts unclipped whatever the previous one left.
- An empty intersection (§3.3) therefore becomes a zero-area scissor: **everything under it is clipped away**. HIGH (scissor semantics are `rl_draw`'s).
- **Minimap aside (not followed, `sr2_map` stays parked):** the minimap's game-side callbacks call the clip-off thunk `0x00e22970` themselves (`0x0080a433`, `0x0080a846`), so a minimap element resets the scissor to the full bounds mid-document regardless of the Vint clip stack.

### 3.6 What a reimplementation has to do for `clip`

1. If `clip_enabled` is exactly 1: build the axis-aligned box `pivot + offset⊙k … + clip_size⊙k` in document space (pivot through the full transform, extents through cumulative scale only), round to integers half-away-from-zero, map to device pixels (near edges truncated, far edges rounded), intersect with the current top (empty → `(0,0,0,0)`), push, set the scissor.
2. Keep the 8-deep cap with the "count but ignore" overflow.
3. Unwind after the element's child loop, not after its own draw — so a `clip` clips its own subtree and every later sibling (spec §4.3) — and **not** inside mask subtrees.
4. Decide deliberately whether to reproduce the off-by-one restore. Bit-exact parity needs it (re-apply the popped rect; reset to the full bounds only at count 0); a "fixed" port will differ only for nested clips with content after the inner clip's subtree.

---

## 4. Mask modes on the game side — CONFIRMED to the `rl_draw` boundary; settles most of `spec-vint-render.md` §11 item 2

### 4.1 The callback table, re-read in full this pass

The installer `0x008489e0` stores the 18 slots at `0x00848b7b`–`0x00848c25` (absolute stores, CONFIRMED): `+0x00` `0x008470a0` · `+0x04` `0x00847130` · `+0x08` `0x00847110` · `+0x0c` `0x00842fe0` · `+0x10` `0x00844960` · `+0x14` `0x00842fb0` · `+0x18` `0x00847060` · `+0x1c` `0x00847080` · `+0x20` `0x00847d40` · `+0x24` `0x00847280` · `+0x28` `0x00848280` · `+0x2c` `0x00848890` · `+0x30` `0x00848920` · `+0x34` `0x00848960` · `+0x38` `0x00842ff0` · `+0x3c` `0x00809910` · `+0x40` `0x0080a460` · `+0x44` `0x007dea30` — matching the scoping note's table.

### 4.2 The per-thread UI draw context, field by field (CONFIRMED from the bodies below)

The context returned by `0x00846f10` (U3) holds, in this pass's reading:

| Context field | Written by | Content |
|---|---|---|
| `+0x00` dword | `+0x18` body `0x00847060`; document begin `0x008470a0` (= 1) | effective `render_mode` |
| `+0x04` dword | `+0x08` body `0x00847110`; document begin (= 0) | mask mode 0/1/2 |
| `+0x08` byte | `+0x1c` body `0x00847080`; document begin (= 0) | inherited `background` flag |
| `+0x09` byte | **the UI frame function `0x00848e40` writes 0 at `0x00848e75`, every frame** | the sRGB-decode switch of spec §12.4 |
| `+0x0c` dword | `+0x04` body (U3); document begin (= `0xffffffff`) | last colour sent |
| `+0x10` dword | the state builder `0x00846f70`; document begin (= the dword at `0x0132d488`, `0x0ff00005`) | a packed render-state word handed to every primitive (U3's "block at `+0x10`") |

`+0x08`, `+0x18` and `+0x1c` share one shape (11 insns each): if the new value differs from the stored field, store it and **tail-jump to the state builder `0x00846f70`**; otherwise do nothing. So the two per-element pushes of `spec-vint-render.md` §2.3 step 4 land at context `+0x00`/`+0x08` and are folded into the `+0x10` word, which answers U3 OPEN item 13 (`spec-vint-render.md` §11 item 13).

### 4.3 The state builder `0x00846f70` (61 insns) — CONFIRMED

It rebuilds the `+0x10` word from the four fields above:

1. **Base word by mask mode** (context `+0x04`):
   - mode **0** → `0x0ff00005` (from `0x0132d488`);
   - mode **1** → `0x0ff02000` (from `0x0132d4c0`);
   - mode **2** → `0x0ff01000` (from `0x0132d4bc`) with bits 13/15 cleared and `0x5000` OR-ed in = **`0x0ff05000`**, stored, and the builder **returns immediately** — `render_mode` and `background` are **not applied in mode 2**, and the colour is not re-sent;
   - any other value → return without touching the word.
2. **`background`** (modes 0/1): if context `+0x08` is set **and** context `+0x09` is 0, bits 8–11 become `0x1`; otherwise bits 8–11 are cleared. So `background` changes the word only while the sRGB switch is off.
3. **`render_mode`** (modes 0/1): bits 0–3 are set from a 5-entry jump table (`0x00847040`) on `render_mode − 1` — see §5. A value of 0 or above 5 leaves bits 0–3 as the base word had them.
4. Re-sends the **cached** colour (`0x005d91a0(&context[+0x0c])`), then stores the word at `+0x10`.

**So the three mask modes differ only in the bits 12–15 nibble of the state word** — `0` (mode 0), `2` (mode 1), `5` (mode 2) — plus, in mode 2, a forced 0 in bits 0–3 and bits 8–11 regardless of the element's `render_mode`/`background`. CONFIRMED. What each nibble value means to `rl_draw` (which D3D9 stencil / colour-write states it selects) is inside `rl_draw`'s consumer of this word and was **not** read (the boundary U3 kept): OPEN.

### 4.4 Slot `+0x0c` — mask-subtree begin: `0x00842fe0` → `0x005de1b0` → `0x00e525b0` — CONFIRMED shape, HIGH meaning

`0x00842fe0` is a 1-instruction jump to `0x005de1b0`, which calls `0x00e525b0(0x02a8a7f0, 4)`; `0x00e525b0` calls virtual slot `+0x40` of the `rl_renderer` singleton at `0x0351f8f8` (`spec-render-pipeline.md` §4.4) with `(0x02a8a7f0, 4, 1.0f, 0)`. The one sibling caller of `0x00e525b0` (`0x005de190`) passes `(0x0132d430, −1)`. **HIGH: `+0x40` is the renderer's clear call, the second argument is a clear-flags mask in the public Direct3D convention (`D3DCLEAR_STENCIL` = 4; −1 = everything), `1.0` the depth value and `0` the stencil value — so mask-subtree begin clears the stencil buffer to 0.** The renderer's `+0x40` body was not read (the `rl` boundary).

### 4.5 Mode meanings, put together

| Mode | When the walk sets it (spec §4.2, re-read this pass at `0x00e26b70`–`0x00e26be4`) | Game side | Meaning |
|---|---|---|---|
| 0 | default; restored after a mask-opening child loop | base word `0x0ff00005`, stencil nibble 0, `render_mode`/`background` applied | **masking off** — CONFIRMED (word), HIGH (meaning) |
| 2 | first mask child in mode 0: `+0x0c` fires (stencil clear, §4.4), then mode 2 | word `0x0ff05000`, element `render_mode`/`background` ignored, blend bits 0 | **write the mask** — HIGH (the mask elements' own quads are drawn into the stencil under a fixed state; whether their colour reaches the target is decided by `rl_draw`'s reading of the word, OPEN) |
| 1 | first non-mask child after mode 2 | word `0x0ff02000` + `render_mode`/`background` | **draw tested against the mask** — HIGH |

This upgrades the spec's §4.2 "write mask" / "test against mask" from HYPOTHESIS-grade guesses to HIGH: the stencil clear at mask begin, the fixed no-blend state while writing, and the normal blend state plus a distinct stencil nibble while testing are all read directly; only the final D3D9 translation of the word is unread.

**Walk-side quirks re-confirmed or new this pass (CONFIRMED):**
- The mask flag that selects the loop branch is the **looping element's own** composed `+0x14` (which already includes its own `mask`), so a `mask` element's own children are walked without mode switching and without clip unwinding (§3.4).
- Sort order is clip-and-mask, clip, mask, rest (spec §6.2). A plain (non-mask) `clip` child that sorts **between** a clip-and-mask child and the mask children flips mode 2 → 1, so the remaining mask children are drawn in mode 1 (tested, not written). Only reachable when a `clip` with `mask` set and a `clip` without it share a parent with other mask children. HIGH that no shipped document does this (not checked).
- `0x00e23770` (Vint side) records the new mode only when the callback table and slot `+0x08` exist; with no table installed the Vint-side mode never changes (harmless — nothing is drawn either).
- Document begin (`0x008470a0`) zeroes the game-side mode directly (no `+0x08` call), but not the Vint-side copy (`0x00e236f0`); the walk's own restore keeps them in step in practice.

---

## 5. `render_mode` — the real enumeration on the game side — CONFIRMED (mapping), OPEN (D3D9 blend factors)

The game handles **every** value of the shipped `render_mode` enumeration (`spec-lua-bindings.md` §9.2a), not just 0/1. From the jump table at `0x00847040` (index = `render_mode − 1`), each sets bits 0–3 of the state word (mask modes 0 and 1 only):

| `render_mode` (§9.2a name = value) | Bits 0–3 written | Operation |
|---|---|---|
| `default` = 0 | — (unchanged) | never reaches the game as an effective value: the walk resolves 0 to the nearest non-`default` ancestor, ultimately the document seed 1 (spec §4.1) |
| `filter` = 1 | `0x5` | `and ~0xa, or 0x5` |
| `additive` = 2 | `0x1` | `and ~0xe, or 0x1` |
| `additive_alpha` = 3 | `0x2` | `and ~0xd, or 0x2` |
| `subtractive` = 4 | `0x9` | `and ~0x6, or 0x9` |
| `multiply` = 5 | `0xc` | `and ~0x3, or 0xc` |
| > 5 (not in the enumeration) | — (unchanged) | mode 0 keeps the base `0x5` (= `filter`), mode 1 keeps `0x0` |

- The document begin sets context `render_mode` = 1 and the base word's own low nibble is `0x5`, i.e. **`filter` is both the seed and the base** — so the spec's HIGH "1 = `filter`" is now CONFIRMED by the game's own table, and "`filter` = ordinary alpha blending" stays HIGH by name (the nibble's D3D meaning is `rl_draw`'s, unread).
- **These five are distinct blend selections** — a reimplementation must support all five, not just alpha blending, since Lua (and documents, 29 `render_mode` records in the swept 159, values not re-read this pass) can set any of them. The natural reading by name (HYPOTHESIS for the exact factors): `filter` = source-alpha / inverse-source-alpha; `additive` = one / one; `additive_alpha` = source-alpha / one; `subtractive` = reverse-subtract; `multiply` = destination-colour / zero. Settling the factors means reading `rl_draw`'s decode of the state word's low nibble — OPEN.
- **In mask mode 1 with an out-of-range `render_mode` the low nibble stays 0** (the mode-1 base word has no blend bits): a distinct, probably unintended, state. Only reachable by setting an undefined value from Lua. HIGH.
- `background` contributes bits 8–11 = `0x1` only while the sRGB switch (context `+0x09`) is 0; with it on, `background` has no effect on the word. Its meaning to `rl_draw` is OPEN (out of scope here).

---

## 6. Cross-observations

- **One resource-to-callback idiom, three widths.** `bitmap` (`0x00e30470` → `+0x20`, 5 arguments), `bitmap_circle` (`0x00e304e0` → `+0x24`, 8) and the dead `gradient` path (`0x00e303c0` → `+0x28`, 13) are the same helper: texture-handle `−1` gate, copy `source_nw`, add the sheet page position for a mode-1 image, pass `source_se` untouched as an extent. The game sides share one prologue too (texture bind, `(10,10)` fallback size, the same dead inverse-of-`M` arithmetic, UVs `nw/T … (nw+ext)/T` with no half-texel offset). A port can implement one "textured shape from image + source rect" primitive with three shapes: quad, quad with per-corner colours (unused), pie.
- **Colour is still state, with one designed exception that never runs.** `bitmap_circle`, like `bitmap`, sets the current colour through `+0x04` and the game reads it back for every vertex. Only the dead `+0x28` body carries per-vertex colours. So in the shipped game **every Vint primitive is uniformly coloured**.
- **Every per-element state change re-sends the cached colour** (`0x00846f70` → `0x005d91a0(&context[+0x0c])`), and document begin both sets the cache to `0xffffffff` and sends it. Within the UI's own callbacks the `rl_draw` colour and the cache therefore stay in step, which narrows U3's colour-cache coherence question (`spec-vint-render.md` §11 item 14). The one exception found: **the minimap's game-side callbacks call `0x005d91a0` directly** (`0x0080a43d`, `0x0080a8e1` and others in the `0x00809910`/`0x0080a460` bodies) without going through the cache — a minimap element can leave `rl_draw`'s colour different from the cache, so the next Vint element whose colour equals the stale cache would be skipped by the filter. HIGH as a mechanism, not followed (`sr2_map` is parked).
- **Context byte `+0x09` (U3's sRGB switch) is written 0 at the start of every UI frame** (`0x00848e75`, in `0x00848e40`, right after the frame function itself calls the document-begin body `0x008470a0`). No write of 1 was found in that function; a raw sweep for byte stores at `[reg+9]` across `.text` gives 205 candidates, not filtered this pass. So during UI rendering the switch is **HIGH: off** (linear decode not applied) unless some callee sets it mid-frame. This narrows `spec-vint-render.md` §11 item 9 materially; it also means `background` (§4.3) is live in practice.
- **Gates, not culls, again.** `bitmap_circle` skips on no image / texture `−1` / `num_wedges` 0; `clip` on `clip_enabled ≠ 1`; `gradient` behaves as `bitmap`. None tests bounds. `clip`'s push is the only per-type draw that changes walk-visible state (the document clip stack); `bitmap_circle` and `gradient` touch nothing but the callbacks.
- **Rounding conventions differ by site** and a bit-exact port must copy each: `source_*` setters and the clip box in document space use round-half-away-from-zero (`0x00dad900`); the clip box's near device edges use truncation (`0x00ea2560`), its far edges round-half-away; colour bytes use truncation (×255, `cvttsd2si`) everywhere on the Vint and game sides except the `+0x04` body's re-quantisation (round to nearest, U3).
- **Scoping-note corrections from this unit** (`review/scoping-ui-render.md`):
  - §1.4 `gradient` row: the draw the walk calls is `0x00e312b0` (`bitmap`'s, U3); `0x00e35410` is an unreachable slot-`+0x130` method (§1). The "main helper" `0x00e303c0` and slot `+0x28` are dead with it.
  - §1.5 slot `+0x0c` ("primitive flush or mask switch"): **mask-subtree begin, a stencil clear** (HIGH, §4.4). Slot `+0x08` ("?"): **set mask mode** (§4.2). Slots `+0x10`/`+0x14` ("clip push/pop"): **set scissor rect / reset scissor to the full UI bounds** — absolute, not a game-side stack (§3.5). Slots `+0x18`/`+0x1c` ("inherited render-state push"): store `render_mode`/`background` into the context and rebuild its state word (§4.2).
  - §1.4 `clip` "callback(s) used: +0x10 / +0x14": the `clip` draw only ever reaches `+0x10` (through the push); `+0x14` comes from the walk's unwind via the pop (and from the minimap).
- **Size read for this unit** (my lister's counts, each function from its entry): Vint side and shared helpers ≈ 1,300 instructions (including the 16 `gradient` and 9 `bitmap_circle` accessors, the `bitmap` lifecycle bodies read for comparison, the five callback thunks, and the walk's child-loop tail re-read); game side ≈ 1,380 (`+0x24` 691 and `+0x28` 377 read for structure with the vertex formulas checked, the rest line by line). The scoping note's figures for the named functions (`0x00e35410` 97, `0x00e303c0` 54, `0x00e34880` 38, `0x00e304e0` 44, `0x00e30db0` 46, `0x00e30c60` 114, `0x00e1f5c0` 83, `0x00e1f6b0` 15, `0x00847280` 691, `0x00848280` 377, `0x005de290` 95, `0x00844960` 64, `0x00842fb0` 12, `0x00847110` 11) match exactly.

---

## 7. OPEN items

1. **`gradient` population outside the swept set.** No `gradient` element exists in `interface_startup.vpp_pc`'s 159 documents (U1); `interface.vpp_pc` was never swept. If any document there uses `gradient`, the shipped game draws it as a plain `bitmap` (§1) — a sweep would say whether that ever produces a visibly "wrong" result worth noting for content authors. Low priority for a faithful port (draw as `bitmap` either way).
2. **`gradient` cause.** "A one-argument overload that failed to override the three-argument draw" is HIGH by layout, not provable from the binary. No effect on behaviour.
3. **The state word's D3D9 meaning.** Bits 12–15 (mask: 0 / 2 / 5), bits 0–3 (blend: `filter` 5, `additive` 1, `additive_alpha` 2, `subtractive` 9, `multiply` 0xc), bits 8–11 (`background` 1) and the constant upper part `0x0ff0…` are decoded inside `rl_draw` (the consumer of context `+0x10`, reached through `0x005d9c30`/`0x005d9c80`), not read. Needed for the exact stencil ops/reference/colour-write mask in mask modes 1/2 and the exact blend factors per `render_mode`. This is the one remaining piece of `spec-vint-render.md` §11 item 2; it belongs with `spec-render-pipeline.md`'s state-block work.
4. **Stencil clear (§4.4).** `rl_renderer` slot `+0x40` read as "clear (flags, depth, stencil)" by its argument shape and the public `D3DCLEAR` flag values; the body and the 16 bytes at `0x02a8a7f0` (probably a rect or colour, zero-initialised) were not read.
5. **Nested-clip population.** Whether any shipped document nests a `clip` inside a `clip`'s subtree with content after the inner subtree (the only case §3.4's defect changes), and whether any document has a `clip` under a `mask` element (§3.4's skipped unwind).
6. **`num_wedges` values.** The 26 authored values (swept documents) and any Lua-set values were not read; only matters for the unclamped stack-allocation hazard (§2.4).
7. **`bitmap_circle` under rotation with a non-square extent** (§2.4): the screen-axis radius scaling is read directly; whether any shipped `bitmap_circle` has `ext.x ≠ ext.y` and a rotated ancestor (the only case where it shows) was not checked.
8. **Context `+0x09` writers beyond the frame function** (§6): the 205-candidate byte-store sweep was not filtered; a filter keeping only functions that also call `0x00846f10` would finish it.
9. **The UI bounds' alternative assignments** in `0x00848e40` (`0x00848f8c`…, `0x00848fca`…) — other paths that set the scissor bounds differently (HYPOTHESIS: the side render path of spec §11 item 1); not followed.
10. **Live confirmation.** All static. A frame capture with one `bitmap_circle` at partial `end_angle`, one nested-clip document and one masked group would move §2.3, §3.4 and §4.5 to CONFIRMED-by-data.
11. **Not crossed:** the `.czn_pc` zone-data interior and named-object resolution. The minimap appeared only as the source of two clip-off calls and direct colour sets (§3.5, §6); its slots were not followed. Text layout/fonts (the parallel unit) were not touched.

---

## 8. Clean-room check

- **Plain hex addresses only.** No Ghidra auto-names for functions, globals or labels, no decompiler variable names, no pasted pseudocode. Formulas (the pie's rim position and UV, the clip box, the state-word bit operations) are my algebraic or bit-level restatements of the arithmetic read in capstone listings.
- **Own words throughout.** No Ghidra project was opened or created for this unit, so there is nothing to delete.
- **Scratch scripts** (session scratchpad, all read-only against the executable): re-used `u2dis.py`, `u3dis.py`, `u3vt.py`, `u3scan.py`; added `u4vtcmp.py`, `u4raw.py`, `u4vcall.py`, and the self-check `u4_check.py`. Listings saved there as `u4_*.txt`.
- **Shipped strings, quoted as data:** the engine's own property, type and enumeration names (`gradient_nw`, `num_wedges`, `clip_enabled`, `filter`, `additive_alpha`, …), the type-name string `gradient` at the end of its vtable, the RTTI type name `.?AVvint_element_gradient@@` (§1.1), and `rl_renderer` (an RTTI type name already cited in `spec-render-pipeline.md` §4.4). `D3DCLEAR_STENCIL`, `D3DCOLOR` and "stencil"/"scissor" are public Direct3D terms. None of these is a Ghidra auto-name.
- **Self-check, run on the finished file as the literal last step:** I ran the project's standard auto-name pattern (the one quoted in U1 §7, not repeated here so that this note stays at zero hits) with Python `re` (`u4_check.py`) over the whole file, this section included. **Result: 0 regex hits.** I also ran the plain-substring backstop for the ten auto-name fragments listed in U1 §7, four of them with the trailing-character rule (a digit after the variable-name, parameter and "undefined" fragments, a hex digit after the local-variable fragment); the fragments are not spelled out here for the same reason. **Result: 0 hits for each of the ten fragments, 0 in total.** Top-level headings are in strict order, §1 to §8. The check was run once with this line still a placeholder (0 / 0), then re-run after the line was filled in, with the same result (0 / 0).
- **No spec file was edited and nothing was synced.**
