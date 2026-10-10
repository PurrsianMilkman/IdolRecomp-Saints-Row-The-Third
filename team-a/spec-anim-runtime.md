# Saints Row: The Third — Animation Runtime: the Control-Parameter Contract

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** Animation runtime, unit **U0** of the proposed order in `review/scoping-animation.md` §5 (the
control-parameter contract). Later units (U1 controller state slots, U2 node-graph file format, U3 node-graph
runtime, U4 time / layers / hand-off, …) are expected to be appended to this file as further sections.
**U1 is §6**, **U2 is §7** and **U3 is §8** (all added 2026-10-03). Their scope, method and confidence notes are at the head of
each section. The header fields below describe U0.
**Scope:** the global control-parameter registry built from `control_parameters.xtbl` (loader `0x004cae20`,
per-row reader `0x004d00e0`); the two name → id resolvers (`0x004cbaf0` global, `0x004b8d90` graph-scoped); the
per-controller 8-byte-per-parameter value block at controller `+0x28`, its three typed setters, child-ring
propagation and lazy allocation; how values and `default_value`s reach a node-graph instance; and the complete
census of code that writes parameters, with the field → parameter mapping of the human writer `0x00958690`.
This file covers the **runtime code contract**. It does not cover the `control_*.xtbl` element schemas
(`spec-tables-ui-controls.md` §14), the node-graph file grammar (U2), the graph evaluator that consumes the
values (U3), or the inputs that feed the human writer (`review/scoping-player.md` U3). `.czn_pc` zone data was
not opened.
**Method:** Ghidra-free, read-only. Direct byte reads of the shipped executable through
`tools/harnesses/exe_va.py`, capstone listings of single function bodies, a raw `E8`/`E9` rel32 call-site scan
and a raw dword-reference scan over the whole image, push surveys at every call site of the resolvers and
setters, a heuristic whole-`.text` scan for 8-byte-stride accesses through a `+0x28` pointer, and RTTI
type-descriptor reads for two vtables. Shipped data was read by extracting `control_parameters.xtbl`,
`control_filters.xtbl`, `node_graph_files.xtbl` and the 249 node-graph files from the game's archives with the
project's existing container walker (`scan_meshes` / `vpp_modea`, as `tools/harnesses/an_extract.py` uses them).
No Ghidra project was opened or copied, so there is no private copy to delete. Helper scripts live in the session
scratchpad (`scratchpad/u0/`), not in the repository.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear anywhere
below; no original identifiers are used as identifiers — every function, global and field is cited by its plain
hex address or offset. Parameter names (`desired speed`, `move left bool`, …), element names (`data_type`,
`default_value`, …), type keywords (`bool`, `integer`, `float`), Lua native names and RTTI type-descriptor names
are the engine's own strings, read from the image or the shipped tables and quoted as data — the same convention
every other spec in this project applies.
**Confidence key:** **CONFIRMED** (read directly in a listing, a static table read, or a direct read of shipped
data in this pass); **HIGH CONFIDENCE** (follows from dumped instructions or reference lists, but the target body
or meaning was not read in full); **HYPOTHESIS** (plausible, not settled); **OPEN** (collected in §5).
**Relationship to other specs:** `review/scoping-animation.md` is the scoping note this unit refines; every
correction to it is flagged **[corrects scoping]** at the point it applies. `spec-tables-ui-controls.md` §14
documents the `control_parameters.xtbl` element schema (the DATA side); this file documents the code that loads
and uses it. `spec-lua-api-behaviour.md` §25.6 (`mesh_mover_update_control`) calls into the graph-scoped write
path of §4.3 below. `review/exe-notes-2026-10-01/interp_tabs.md` Q13.2 traced the controller play entry
`0x004b1b90`, whose lazy block allocation §3.4 re-reads. Side findings that touch other files are collected in
§5.3 for someone else to fold in; no other file was edited.

---

## 1. The registry

### 1.1 Load order and storage (CONFIRMED)

- The animation-network loader `0x004cb990` (one caller, `0x0073f080`) calls `0x004cae20` first, before the
  `control_filters.xtbl` reader `0x004caee0` and the `node_graph_files.xtbl` reader `0x004cb510`
  (`review/scoping-animation.md` §1.3). Filters and graphs resolve parameter names while they load (§2.3), so the
  registry must exist first.
- `0x004cae20` fetches manifest slot 12 (`0x00dc5ac0` on the manifest at `0x03519434` with the context
  `[0x03171c04]`). **If that returns nothing, the function returns without touching the registry**: the count
  stays 0, every lookup returns −1, and every parameter write in the game is silently dropped.
- It finds the `control_parameters` element, counts its `control_parameter` children (`0x00dc5150`), stores the
  count as a **signed 16-bit** value at **`0x03171bc8`**, allocates `0x14 × count` bytes through the allocator at
  `[0x03171bf4]` (`0x00dad360`, overflow-checked multiply) and stores the array pointer at **`0x03171bcc`**.
- It then walks the children in document order (first child `0x00dc4ff0`, next sibling `0x00dc5030`), and for
  each one zeroes record `+0x00` and `+0x04` and calls the row reader `0x004d00e0` with the record in `ESI` and
  the row element in `EDI` (register arguments, no stack arguments).
- **A parameter's id is its row index in the shipped file**, 0-based. Ids are not stable across data edits, which
  is why every caller resolves by name (§2).

### 1.2 The `0x14`-byte record (CONFIRMED from `0x004d00e0`) **[corrects scoping: completes §1.3's layout]**

| Offset | Size | Content |
|---|---|---|
| `+0x00` | 4 | pointer to a private copy of the `name` text (allocated through the vtable `+0x38` slot of `[0x03171bf4]`, length + 1, byte copy). A missing `name` element yields the text `none`. |
| `+0x04` | 4 | type code (§1.3) |
| `+0x08` | 4 | default value, typed |
| `+0x0c` | 4 | minimum, typed |
| `+0x10` | 4 | maximum, typed |

What `0x004d00e0` writes at `+0x08`…`+0x10` depends on the type:
- **float (3):** `default_value`, `min_value`, `max_value` each read as a 32-bit float through `0x00dc5210`, with
  0.0 when the element is absent.
- **integer (2):** the same three elements read as 32-bit integers through `0x00dc5240`, with 0 when absent.
- **bool (1):** `default_value` is compared case-insensitively with the text `false`; **anything other than
  `false` — including `true`, any other text, or a missing element (read as `none`) — gives 1**. The byte at
  `+0x08` receives that 0/1, and the **bytes** at `+0x0c` and `+0x10` receive 0 and 1. Only the low byte of each of
  the three dwords is written; the other bytes are whatever the allocator returned (the loader zeroes only
  `+0x00` and `+0x04`). `min_value`/`max_value` are not read for bool rows.
- **type 0:** nothing is written at `+0x08`…`+0x10`.

### 1.3 Type codes (CONFIRMED) **[corrects scoping: type 2 = integer settled]**

`0x004d00e0` reads `data_type` (absent → `none`) and compares it case-insensitively, in order, with the four
entries of the static table at **`0x013503d4`**: index 0 `none`, 1 `bool`, 2 `integer`, 3 `float`. The index of
the first match is stored at `+0x04`; with no match the zero written by the loader stays.

| Code | `data_type` text | Value slot (§3.1) | Setter (§3.2) |
|---|---|---|---|
| 0 | `none`, absent, or unrecognised | — | none accepts it: such a parameter can be named and resolved but never written |
| 1 | `bool` | byte | `0x004b1410` |
| 2 | `integer` | dword | `0x004b1490` |
| 3 | `float` | float | `0x004b1510` |

Type 2 is confirmed three ways: the table entry, the integer branch of the row reader, and the integer setter's
`cmp …, 2` (§3.2). The same 1/2/3 codes are carried in the network message of §4.3.

### 1.4 The shipped file (CONFIRMED, direct read)

`control_parameters.xtbl` is entry 946 of `misc_tables.vpp_pc` (15,465 bytes). A scan of all 38 archives under
`packfiles\` finds no second copy, so there is no patch or DLC override of it in this install.

- **87 rows**: 52 `bool`, 29 `float`, 6 `integer`. Every row has `name`, `data_type` and `default_value`; the 35
  numeric rows (and only they) also carry `min_value`/`max_value`. This settles the row count left OPEN by
  `spec-tables-ui-controls.md` §14 (§5.3).
- **Non-zero defaults: exactly three**, all bool `true`: `Supported By Ground` (row 25), `spike closed bool`
  (row 50), `Vehicle Landed Bool` (row 73). Every float and integer default is 0.
- The six integers are `cower type` (0–1), `idle type` (0–1), `Ball Rider Stage Number` (0–14), `Weapon Group`
  (0–32), `Coop Rider Stage Number` (0–10), `Climb Duration ms` (0–10000).
- The range values are stored (§1.2) but **no code read in this pass clamps a write against them**; the setters
  do not (§3.2). Whether the graph evaluator uses them is U3's question (§5.1).

The full name → id → type list, in id order, is the shipped file itself; the names that matter to code are in
§4.

---

## 2. Name → id resolution

### 2.1 Global resolver `0x004cbaf0` (CONFIRMED)

- `cdecl`, two stack arguments. Walks ids 0 … count−1 and compares each record's name with the first argument
  using the C runtime's case-insensitive compare `0x00eaa178` (the CRT routine's shape: locale check, `EINVAL`,
  `0x7fffffff` on a null argument). **The first match wins**; the result is the 16-bit index in `AX`, or −1 in
  `EAX` when nothing matches.
- **The second argument is never read** (only `[esp+0xc]`, the first argument after two pushes, is loaded).
  Callers push 0 or 1 with no pattern tied to type or writer. HYPOTHESIS, as in the scoping note: a compiled-out
  "must exist" assertion flag.
- **No hashing**: a linear string compare over 87 records. All callers in game code cache the result (§4.1),
  except the two `combat ready` branch lookups in `0x00958690`, which resolve `direction` and `desired direction`
  **every frame without caching** (`0x009590a4`, `0x00959191`; CONFIRMED). A port may cache them; the result is
  identical because the registry never changes after load.
- `0x004cbb40` is the matching id → record accessor (returns `0x03171bcc + 0x14 × id`, or null for −1 or an id
  outside `[0, count)`). Its one caller is the condition-operand type query at `0x004d09f2` (§2.3).

### 2.2 Graph-scoped resolver `0x004b8d90` **[corrects scoping: not a by-name variant of the global lookup]**

CONFIRMED by listing, together with its two companions:
- **`0x004b8d90`** is `thiscall` on a **node-graph definition** (the object whose `+0x1c` is a 16-bit alias count
  and `+0x20` an array of 16-bit global ids — the "alias table" of `review/scoping-animation.md` §1.4). It takes
  a name and a second, unread argument (`ret 8`), and returns the **local alias index** within that graph whose
  registry name matches case-insensitively, or −1. It returns −1 at once if `+0x20` is null.
  - **Hazard (CONFIRMED):** an alias entry holding −1, a negative id or an id ≥ count maps to a null record, and
    the loop then dereferences that null (`0x004b8ddd`/`0x004b8ddf`). A graph with an unresolved alias crashes
    this lookup. A port should skip such entries.
- **`0x004b8e90`** (`thiscall`, one argument): local alias index → global id (`+0x20[index]`), or −1 when the index
  is not below `+0x1c`.
- **`0x004b8e10`** (`thiscall`, global id in `SI`): global id → local alias index, linear, or −1.

So the "by-name" writers (§4.3) do not resolve a name globally: they resolve it **only if the target object's
own graph uses that parameter**, then convert the alias index back to a global id for the controller setter.

### 2.3 Load-time resolution inside the library (CONFIRMED as call sites; roles HIGH)

Two of the 64 call sites of `0x004cbaf0` are inside the library, and both cache the id in a definition record:
- `0x004d04ae`: a transition-condition operand of the "control parameter" kind stores the id at operand `+0x08`.
  At evaluation (`0x004d0543`… switch) the id is mapped to the graph's local alias (`0x004b8e10`) and the value is
  read from the graph instance's array (`+0x0c` + 8 × alias). The operand's type is taken from the registry
  (`0x004d09f2` → `0x004cbb40` → `+0x04`).
- `0x004d12f3`: a `control_filter` `_info` row resolves its `source_name` as a parameter (absent → `none`),
  stores the id at `+0x12`, clears the "source is a filter" byte `+0x10`, and copies the registry type to `+0x0c`.

Both belong to U2/U3; they are listed here because they make the registry a **load-time** dependency of the
filter and graph tables, not only a runtime one.

---

## 3. Value storage, setters and lazy allocation

### 3.1 The per-controller block (CONFIRMED)

- A pointer at **controller `+0x28`** to `8 × count` bytes (count = `0x03171bc8`), indexed by **global id**.
- Slot layout by type: bool → byte `+0`; integer → dword `+0`; float → float `+0`. **Bytes `+4`…`+7` of a slot
  are never written by the setters.**
- One block per controller (human, vehicle, `object_rig` object, …; `review/scoping-animation.md` §1.4). It is
  not shared and has no per-parameter "written" flag.

### 3.2 The three typed setters (CONFIRMED)

`0x004b1410` (bool), **`0x004b1490` (integer)**, `0x004b1510` (float): all `thiscall` on the controller, two stack
arguments (16-bit id, value), `ret 8`. Identical structure:
1. **If controller `+0x28` is null, return at once** — before the child-ring step. A controller without a block
   therefore also never forwards a write to its children.
2. Walk the **child ring**: start at `[controller+0x08]`, call the same setter on each child with the same id and
   value, advance through child `+0x00`, stop on null or on returning to `[controller+0x08]`. The recursion makes
   this depth-first over nested rings.
3. If the id is −1, return. Otherwise look the record up (null for an id outside `[0, count)`); if the record is
   null or its type is not the setter's own (1, 2 or 3), return.
4. Store the value: byte (bool), dword (integer), or 32-bit float (float).

There is no clamp to `min_value`/`max_value`, no change detection, no diagnostic, and no return value.

**[corrects scoping]** `0x004b1490` is **the integer setter, not a reader** (`cmp dword [record+4], 2`, then a
dword store). Its callers are `0x009896d0` (`Climb Duration ms`), `0x008cb380` (graph-scoped integer write) and
`0x00a3c910` (`Ball Rider Stage Number`), §4. No dedicated "read a parameter from a controller" function was found:
the only code that reads the block is the per-frame copy (§3.5) and the `object_rig` replication pair (§4.3).

`0x004b1590` (`thiscall`, one float, `ret 4`) is a different kind of setter: it stores its argument at controller
`+0x2c` with **no** child-ring step. See §3.6.

### 3.3 Child ring (CONFIRMED as structure; role HYPOTHESIS)

The ring is the one `interp_tabs.md` Q13.2 step 1 describes for the play entry, which also replays its call over
the ring. The setters treat it the same way: a write to a controller is a write to every controller in its ring.
What attaches a child controller (a second rig on the same character, a held prop) was not traced (§5.1).

### 3.4 Lazy allocation (CONFIRMED)

There are exactly three allocation sites, found through the readers of the count `0x03171bc8`. All three are
guarded by "the started animation record has bit `0x80000000` at `+0x2c`" and "controller `+0x28` is null":

| Site | Inside | Form |
|---|---|---|
| `0x004b1c86` | `0x004b1b90` (play entry) | inline |
| `0x004b23f3` | `0x004b2280` (blend-tree-backed play) | inline |
| `0x004b2c6a` | `0x004b2b50` (blend-tree state evaluation) | calls the helper `0x004b1150` (controller in `ESI`) |

Each allocates `8 × count` bytes from the pool object at `0x03424868` (`0x00db5930`), stores the pointer at
controller `+0x28`, and **zero-fills it** (`0x00ea23b0`). Immediately after (`0x004b1dfb`, `0x004b2519`,
`0x004b2c9e`) the same path instantiates the node graph through `0x004cbd20`, passing the graph index read from
the animation record's `+0x08` word in `AX` and the controller's instance handle (`+0x20`) on the stack.

Consequences:
- **The block is never initialised from `default_value`** (CONFIRMED: the only fill is the zero fill). This answers
  the first half of the scoping note's OPEN item 2. Defaults do matter, but in the graph instance, not in the
  block (§3.5).
- **Before any node-graph state has started on a controller, every write to it is discarded** (step 1 of §3.2).
  The human writer additionally gates itself on controller byte `+0x24` being non-zero (§4.2).
- No site was found that frees or re-zeroes the block. Values therefore persist for the controller's lifetime,
  across graph-state changes (HIGH: release paths were not traced).

### 3.5 How values reach a graph instance; where defaults apply (CONFIRMED) **[corrects scoping: OPEN item 2 resolved]**

**At instancing, `0x004cbd20` seeds the instance with defaults.** It takes a free row from the 128-row pool
(`0x03133a90`, stride `0x4c`), stores the graph index at row `+0x04` and the instance handle at `+0x08`, and
allocates `8 × aliases` bytes (alias count = graph `+0x1c`) from the pool object at `0x03519890` for row `+0x0c`.
For each alias whose global id is valid it copies the **registry dword `+0x08` (the default)** into dword `+0` of
the slot and clears byte `+4`. It also builds the filter-output array at `+0x10`, sets `+0x3c` = 0.0,
`+0x40` = 1.0 (`0x0135101c`), clears `+0x44`, and sets `+0x48` = 1.

**Every frame, the controller update overwrites them with the controller's values.** `0x004b0b80`, called once per
in-use state slot from the controller update `0x004b3720` (call site `0x004b3cf1`), does this for the slot's graph
instance (slot `+0x6c`):
- It returns at once if the slot has no instance or **controller `+0x28` is null**.
- **Freeze rule:** if the slot is not the controller's current slot (`+0x50`), has slot flag `0x02` (labelled
  "fading in" by `review/scoping-animation.md` §1.4, not re-derived here), and the instance's `+0x30` is already
  set, the copy is skipped and instance `+0x48` bit `0x02` is set. The instance keeps its last values.
- Otherwise it copies controller `+0x30`/`+0x34` (the two anim-group tables) to instance `+0x30`/`+0x34`, and
  controller `+0x44` to `+0x38` when slot flag `0x02` is set. Then **for every alias i with a valid id, it copies
  dword `+0` of block slot [id] into dword `+0` of instance slot [i]**, and clears instance `+0x48` bit `0x02`.
- If instance `+0x44` is non-zero, it sets instance `+0x3c` = `+0x40` × controller `+0x2c` (§3.6).

Consequences (HIGH; the graph evaluation itself is U3):
- **A `default_value` is visible to the graph only from instancing until that slot's first copy.** After that the
  graph sees the controller's block value, which is 0 for anything no code has written. Whether the play path
  evaluates the graph before the first update (for example `0x004b2d70` → `0x004b2b50`) was not traced (§5.1).
- So the three shipped non-zero defaults (§1.4) are effectively replaced by `false` on the first update unless a
  writer has set them: `Supported By Ground` and `Vehicle Landed Bool` are written every frame by their writers
  (§4.2, §4.4), but `spike closed bool` is written only on activity events (§4.3).
- Writes made earlier in the same frame are visible to the graph in that frame, because the writers run
  immediately before `0x004b3720` (§4.5).
- Only dword `+0` is copied. Byte `+4` of an instance slot is cleared at instancing and not touched by the copy;
  its meaning is U3's (§5.1).
- The copy uses only the first dword, so for a bool it also carries the three undefined high bytes of the
  default (§1.2) or of the block slot (always zero there). A reader of a bool must test the low byte only.

### 3.6 Controller `+0x2c`: the per-frame time step (CONFIRMED values; role HIGH) **[corrects scoping §1.3: not a rate scale]**

`0x004b1590` is called on the controller's own update path, before the update, by five of the six callers of the
controller update (the sixth, `0x00b905f9`, has no such call):
`0x008cba4b`, `0x0090064f`, `0x00921fc8`, `0x009599d4` (human), `0x00a76528` (vehicle). The human writer passes:
- the global `0x0153ba64` when `0x00722000` returns true (the same selector the pose poster uses,
  `review/scoping-animation.md` §1.6);
- otherwise **1/30 s** (`0x0132a0ac`, 0.033333);
- or 1/30 × **0.2** (double at `0x012a2e30`) when all of these hold: human `+0x1c9c` bits 19 and 20 set, `+0xcce`
  ≠ `0x27`, `0x0098b970` true, and `0x0096e670` (the `Supported By Ground` predicate, §4.2) false.

`0x004b0b80` multiplies it by the instance's `+0x40` (initialised to 1.0) into instance `+0x3c`. That is a time
step, not a playback-rate factor. Its consumer is U3.

---

## 4. The writer census

### 4.1 Method and totals (CONFIRMED)

A raw scan of every `E8`/`E9` rel32 in `.text` gives these call sites:

| Function | Sites | Note |
|---|---|---|
| `0x004cbaf0` global resolver | **64** | 2 inside the library (§2.3), 62 in game code |
| `0x004b8d90` graph-scoped resolver | 6 | `0x0064a23c`, `0x008cb015`, `0x008cb3cb`, `0x008cb457`, `0x008d64cf`, `0x008d64fb` |
| `0x004b1410` bool setter | 36 + 1 recursive | |
| `0x004b1490` integer setter | 3 + 1 recursive | |
| `0x004b1510` float setter | 36 + 1 recursive | |
| `0x004b1590` time step | 5 | §3.6 |

Every setter site was paired with the name whose cached id it pushes (the cache global written right after the
matching resolver call, or the resolver's result used directly). Of the 75 external setter sites, 72 carry a
compiled-in name, and **all 72 agree with the registry type of the shipped row: no type-mismatched write exists in
the shipped code** (CONFIRMED). The other three are the graph-scoped wrappers of §4.3, whose names arrive at run
time. The name at each resolver site is the string literal pushed before the call.

A heuristic scan of the whole of `.text` for "load a pointer from `+0x28`, then index it with stride 8" found the
setters, the per-frame copy (§3.5) and the `object_rig` replication pair (§4.3) as the only accesses to a parameter
block. The other 24 hits are unrelated structures that use both dwords of 8-byte entries. Code that reaches the
block by another instruction pattern would be missed, so this is HIGH, not CONFIRMED.

**Totals (CONFIRMED) [corrects scoping: ~56]:**
- The 62 game-code resolver sites carry 57 distinct names. The repeats are `direction` twice, `Jump Start Land Bool`
  three times, `Jump Left Ground Bool` twice and `Vehicle MPH` twice.
- Five more names are fixed in code only at the graph-scoped callers.
- That makes **62 distinct compiled-in names**, and every one matches a shipped row **exactly, including case**.
- **Thirteen functions** write parameters:
  - nine write named parameters through the setters;
  - three graph-scoped wrappers write names their callers supply (§4.3);
  - one replication slot writes the block directly.

The full table:

| Writer | Names written (setter type) | Controller | Status |
|---|---|---|---|
| `0x00958690` | 32 names, §4.2 | `[[human+0xd24]+0x10]` | in scoping table; mapping completed here |
| `0x009896d0` | `Climb Height` (F), `Climb Dist` (F), `Climb Platform Bool` (B), **`Climb Duration ms` (I)** | human | **[corrects scoping]**: the fourth name and the integer setter were reported as "calls the reader" |
| `0x009895b0` | `Jump Start Land Bool` (B) = `0x0098c500(human)` | human | as scoping |
| `0x0098bea0` | `Jump Left Ground Bool` = 0, `Jump Start Land Bool` = 0 (B) | human | as scoping; five callers (`0x0054b06d`, `0x005b2c6b`, `0x006c4e2d`, `0x008b2482`, `0x009c8491`) — a reset |
| `0x0098c840` | `Jump Left Ground Bool` = 1, `Jump Start Land Bool` = `0x0098c500(human)` (B) | human | as scoping |
| `0x00af4b80` | `Vehicle Steering`, `Vehicle Steering Time`, `Vehicle MPH`, `Vehicle Forward Lean`, `Vehicle Vert Lean`, `Vehicle Tilt` and the three `… Abs` forms (F, each written on two paths), `Wheelie Bool`, `Endo Bool` (B) | rider's | as scoping; called only from `0x009586f0` |
| `0x00a760e0` | `Helicopter Steer Vertical`, `Helicopter Steer Horizontal`, `Vehicle MPH` (F), `Airplane Mode Bool`, `Vehicle Landed Bool`, `Vehicle On` (B) | the vehicle's own | as scoping; caller `0x00a76769` |
| `0x00b09ab0` | `Collision Bool` (B, two sites), `Collision Dir` (F) | — | as scoping; caller `0x00b0a12f` |
| **`0x00a3c910`** | **`Ball Rider Stage Number` (I)** | the local player's (`0x009da4e0`), `[[player+0xd24]+0x10]` | **[corrects scoping: missing from the table]**. It is the Lua native **`animation_sequence_advance`** (registration pair at `0x00a20b66`/`0x00a20b71`): two numbers truncated to integers; when the first is 1 and the second is non-zero, the second is written. Not in `spec-lua-api-behaviour.md` (§5.3). |
| `0x0064a1f0` / `0x008cb380` / `0x008cb400` | any name in the object's own graph (B / I / F) | `[object+0x9c]` | graph-scoped wrappers, §4.3 |
| `0x0089f3b0` | up to four aliases, raw dwords | `[object+0x9c]` | **new**: `dc_object_rig_registration` slot 3, a direct block write, §4.3 |

`0x008cafc0` (the remote-forwarding path of the wrappers) and the existence tests at `0x008d64a0` resolve names
but write nothing.

### 4.2 The human writer `0x00958690`: field → parameter (CONFIRMED by full listing unless marked)

`0x00958690` (1,187 instructions; human in **`ESI`**; single caller `0x0095b8fe` in `0x0095b1a0`, which runs the
controller update `0x004b3720` immediately afterwards at `0x0095b90d`). "Controller" below is
`[[human+0xd24]+0x10]`.

**Entry gate:** if controller byte `+0x24` is 0, the whole function is skipped (`0x009586d8`). The scoping note
reads `+0x24` as the number of node-graph states (not re-derived here). Together with §3.4 this means a human with
no graph state running has no parameter writes at all.

**Pre-step:** if `0x009b9160(human)` holds, `0x00af4b80(human)` runs first (the rider writer, §4.1).

**Cache:** 31 cached ids at `0x02624ac4`…`0x02624b3c`, one guard bit each in `0x02624b40` (bits 0–30), set on first
use. The two `combat ready` branch lookups of `direction` and `desired direction` are uncached (§2.1).

In execution order. "Every frame" means every call that passes the entry gate. The `0x21` class test is
`0x00853b30(human, 0x21)`, which `review/scoping-animation.md` §1.2 reads as "is a player" (HIGH, not re-derived).

| # | Parameter (type) | Value written | When |
|---|---|---|---|
| 1 | `Jump Dive Bool` (B) | human byte `+0xcce` == `0x26` | only when byte `+0xccd` == 4 |
| 2 | `Offhand Grenade Bool` (B) | `+0xcce` == `0x27` | only when `+0xccd` == 4 |
| — | (climb writer) | `0x009896d0(human)`, which writes its four names only when `+0xccd` == 5 | when `+0xccd` == 5 |
| 3 | `dead` (B) | `0x0096f4f0(human)` | every frame |
| 4 | `combat ready` (B) | `0x00943a20(human)` | every frame |
| 5 | `Supported By Ground` (B) | `0x0096e670(human)` | every frame |
| 6 | `desired speed` (F) | when `0x0091f5e0(human)` holds: the length of the desired-move vector `+0x2048`/`+0x204c`/`+0x2050`, read through the `+0xa` bit-`0x02` "is a player" mask (as the scoping note records); otherwise float `+0xa60` | every frame |
| 7 | `Vertical Speed` (F) | float `+0xd08` | every frame |
| 8 | `move forward bool` (B) | 1 when **none** of bits `0x0400`, `0x0800`, `0x1000` of dword `+0xd40` is set | every frame |
| 9 | `move left bool` (B) | `+0xd40` bit 10 (`0x0400`) | every frame |
| 10 | `move right bool` (B) | `+0xd40` bit 11 (`0x0800`) | every frame |
| 11 | `move back bool` (B) | `+0xd40` bit 12 (`0x1000`) | every frame |
| 12 | `backpedal bool` (B) | `+0xd40` bit 13 (`0x2000`) | every frame |
| 13 | `alternate strafe bool` (B) | `+0xd40` bit 9 (`0x0200`) | every frame |
| 14 | `walk bool` (B) | `+0xd40` bit 6 (`0x0040`) | every frame |
| 15 | `run bool` (B) | `+0xd40` bit 7 (`0x0080`) | every frame |
| 16 | `sprint bool` (B) | `0x00941e60(human)` | every frame |
| 17 | `crouch bool` (B) | `+0xd40` bit 1 (`0x0002`) | every frame |
| 18 | `aim bool` (B) | `+0xd40` bit 4 (`0x0010`) | every frame |
| 19 | `fine aim bool` (B) | `+0xd40` bit 5 (`0x0020`) | every frame |
| 20 | `current speed` (F) | float `+0xd1c` (both arms of the `0x21` branch at `0x00958f0c` write the same value) | every frame |
| 21a | `direction` (F) | `0x21` class and `combat ready`: heading of the velocity `+0xa50`…`+0xa58` (0 when its length is 0; when `0x009ba300(human)` holds, `0x004adc80` on `+0x64` is used instead) minus a reference heading derived from the runtime vector `0x013c8780` (`0x00da06b0`, `0x00dae5b0`), passed through `0x00dadeb0` (HIGH: angle wrap; the shipped range is 0…2π) | `0x21` class only |
| 21b | `desired direction` (F) | same branch: heading of the desired-move vector `+0x2048` (0 when zero) minus the same reference, through `0x00dadeb0` | `0x21` class, `combat ready` |
| 21c | `direction` (F) | `0x21` class, not `combat ready`: heading of (`+0x2048`, `+0x2050`) minus heading of the facing axis (`+0x64`, `+0x6c`), both through `0x00dae5b0`, then `0x00dadeb0` | `0x21` class only |
| 22 | `Free Fall Vert Control` (F) | `0x00994f60(human)` ? float `+0xaf4` : 0 | every frame |
| 23 | `Parachute Open Bool` (B) | dword `+0xe8` bit 26 | every frame |
| 24 | `Speed Along rvec` (F) | dot product of the right axis `+0x4c`/`+0x50`/`+0x54` with the vector `+0x1cec`/`+0x1cf0`/`+0x1cf4` ~~(HYPOTHESIS: a velocity)~~ **[Resolved 2026-10-03, `spec-player-runtime.md` §5.4.3 step 10: `+0x1cec` is the character controller's own desired velocity, read here one frame old (the velocity composer `0x0099d1b0` runs after `0x00958690` in the same frame). CONFIRMED by disassembly.]** | every frame |
| 25 | `turn angle` (F) | 0 when `0x00994f60(human)` holds and either `0x00994f40(human)` is false or state `0x1da9` is active on the controller (`0x004b4ce0`); otherwise float `+0xaac` | every frame |
| 26 | `Aim Pitch` (F) | `+0xf74` minus lane 1 of `0x008f4df0` applied to an aim vector; when `0x009b9160(human)` holds (rider), plus `asin` (`0x00ea3440`) of the facing axis's `+0x68` component clamped to [−1, 1] | every frame |
| 27 | `Aim Heading` (F) | `+0xf70` plus `0x00dae190`( lane 0 of `0x004cd810` on the same aim vector, `0x00da3910` on the orientation at `+0x4c` ) | every frame |
| 28 | `fire bool` (B) | dword `+0xdc` bit 31. Before the write: if `0x004b4320(controller, [0x0153bcb0], −1, −1)` holds and `+0xe0` bit 0 is clear, `+0xdc` bit 31 is cleared; `+0xe0` bit 0 is then always cleared | every frame |
| 29 | `Weapon Sprint Bool` (B) | `0x21` class and `0x00941e60(human)`: bit 5 of byte `+0x1d97` (read through the "is a player" mask); otherwise 0 | every frame |
| 30 | `vehicle surf lean` (F) | `0x006c4990(human)` ? `0x006c4430(human)` : 0 | every frame |
| 31 | `Reloading Bool` (B) | `0x009ba8b0(human)` | every frame |
| — | controller `+0x2c` (time step) | §3.6 | every frame |

The aim vector of rows 26–27 is chosen as follows (HIGH: the vector and angle helpers were not read):
- `0x21` class: the aim point from `0x009cb3c0` (transformed by `0x00da38e0`) minus the position `+0x40`/`+0x44`/`+0x48`, through `0x004d6210`;
- otherwise `0x009b3250` when `0x009b0770` holds;
- otherwise `0x009576d0` on `+0xea0` (when `0x009adb00` returns 1 and `0x008addb0(human, 0)` is false) or on `+0x780`.

Notes for a port:
- **Latching.** Rows 1–2 are written only while `+0xccd` == 4, and the climb names only while it is 5. Outside those
  states the last written value stays in the block (§3.4), so the graph keeps seeing it. `direction` and
  `desired direction` likewise freeze for objects failing the `0x21` test. These are the engine's own semantics,
  not a defect to fix.
- The human-object field roles (`+0x40` position, `+0x4c`/`+0x58`/`+0x64` orientation axes, `+0xd40` movement
  flags) are HIGH, from use. ~~The *sources* of those fields are `review/scoping-player.md` U3's subject.~~
  **[Answered 2026-10-03, `spec-player-runtime.md` §5 (the locomotion bridge, Player U3, landed as a spec unit
  rather than staying scoping-only): `+0x40` (position) is written from the Havok proxy's own position each
  frame (`0x0096e710`, §5.7); the orientation axes `+0x4c`/`+0x58`/`+0x64` come from the animation's own root
  rotation, applied through the orientation step `0x004b6530` before this writer runs (§5.4.1 step 3,
  §5.4.5); and rows 20/7 above (`current speed`/`Vertical Speed`, `+0xd1c`/`+0xd08`) are the proxy's
  velocity, written back by `0x0099e2a0` (§5.6.6) — also one frame old, same as row 24. **The movement
  flags `+0xd40` were not traced there either and stay OPEN** (that file's own OPEN 38).]** Row 21a's
  `+0xa50` specifically, for the player, is `unit(r)` — the smoothed move vector's own direction
  (`spec-player-runtime.md` §5.4.4), not a generic "velocity heading" as this row's own phrasing might
  suggest for other object classes.
- **[corrects scoping]** the name at `0x00959906` is `Reloading Bool`. The scoping table's `@Reloading Bool` is a
  transcription artefact: the byte before the literal at `0x0117167c` is the high byte of the preceding dword.
- The resolver's unread flag is 0 at rows 2 and 22–27 and 1 elsewhere. There is no behavioural consequence.

### 4.3 Graph-scoped, script and network writes (CONFIRMED by listing; roles HIGH)

**The three wrappers** `0x0064a1f0` (bool), `0x008cb380` (integer), `0x008cb400` (float) are `thiscall` on a world
object, with (name, pointer to value). Each:
1. calls `0x008addb0(object, 0)`; when it returns false, forwards to **`0x008cafc0`** with type code 1, 2 or 3 and
   returns (HYPOTHESIS: "not locally authoritative → send to the owner"; `0x008cafc0` builds a message, id `0x45`
   through `0x0086f5f0`);
2. otherwise requires object byte `+0x33` bit `0x08` and a controller at `+0x9c`, resolves the name in the
   object's graph `+0xa8` (`0x004b8d90`), converts the alias to a global id (`0x004b8e90`), and calls the typed
   setter on `[object+0x9c]`.

**Their callers** (push survey):

| Caller | Wrapper | Name(s) |
|---|---|---|
| `0x0064ee89`, `0x0065069c` | bool | `spike closed bool` (activity band) |
| `0x0064f0f1`, `0x0064f244`, `0x0064f693`, `0x006500bb`, `0x00650966` | bool | `target down bool` |
| `0x008d5f31`, `0x008d5f4d`, `0x008d6775` (door band) | bool | `is door open`, `is door open back` |
| `0x0093ae82` | bool | `Light` |
| `0x00a02c84` | bool | the empty string at `0x0129a0e3`. It cannot match a shipped name, so it is a no-op unless a graph aliases an empty name (HIGH) |
| `0x00a531b9` / `0x00a53221` / `0x00a5323c` | bool / int / float | caller-supplied: the Lua native **`mesh_mover_update_control`** (`0x00a53100`, `spec-lua-api-behaviour.md` §25.6). The object is the mover sub-record at the resolved handle's `+0xc4` |
| `0x008cc106` / `0x008cc0e9` / `0x008cc0d5` | bool / int / float | caller-supplied: the network receiver `0x008cc030` (referenced from `0x0116993c`). It reads a 2-byte object handle, a name string, a 4-byte type code (1/2/3) and a 4-byte value, then dispatches. It is the counterpart of `0x008cafc0` |

`0x008d64a0` resolves `is door open` and `is door open back` through `0x004b8d90` only to test whether the door's
graph uses them, and stores the answer in bit `0x08` of its `+0x7a`. It writes no parameter.

**Replication, a direct block access.** `dc_object_rig_registration` (vtable `0x011673ac`; RTTI read in this pass)
has slot 2 `0x0089f2c0` and slot 3 `0x0089f3b0`:
- slot 2 reads dword `+0` of the block slots of the **first ≤4 aliases** of the object's graph into a buffer;
- slot 3 writes them back **straight into `[[object+0x9c]+0x28]`**, without the setters: no type check, no
  child-ring step, and no null check of the block.
- A port replicating object rigs must reproduce this, or replace it with typed writes.

**Consequence:** every parameter that an object's own graph aliases can be written by script
(`mesh_mover_update_control`) or the network, under any name. The five literal names above are the only ones fixed
in code.

### 4.4 Other writers (names and types CONFIRMED; field mapping by U7/Player U6.8)

The vehicle (`0x00a760e0`), rider (`0x00af4b80`) and collision (`0x00b09ab0`) writers were checked for names,
setter types and callers only. **[2026-10-07: the vehicle and collision writers' value sources are §10.3/§10.1 (U7); the
rider writer's value source is `spec-player-runtime.md` §12.4 (Player U6.8), not U7 — U7 explicitly deferred this one
function to the concurrent Player unit, see §10.2.]** The jump and climb values are given in §4.1.

### 4.5 Order relative to the controller update (CONFIRMED as call edges)

The two per-frame writers each run in the same function as, and before, the update of their controller:
- human: `0x00958690` → `0x004b3720` (`0x0095b8fe`, `0x0095b90d`);
- vehicle: `0x00a760e0` → `0x004b3720` (`0x00a76769`, `0x00a7697d`).

`object_rig` objects have no per-frame writer. `0x008cb9f0` sets the time step (`0x008cba4b`) and runs the update
(`0x008cba57`), driven from the world update through `0x008cbfe0`; their parameters arrive by event (§4.3).

Of the remaining update callers:
- `0x0090065e` and `0x00921fd7` sit in the two pose clients the scoping note leaves unidentified (`0x00900540`,
  `0x00921fa0`);
- `0x00b905f9` was not placed. Event-style writers (jump, collision, door, activity, script, network) run earlier in the frame
or between frames, and their values are copied at the next update (§3.5).

### 4.6 Validation against the shipped data (CONFIRMED, direct read) **[closes scoping OPEN item 9]**

The 87 shipped rows were compared with the 62 compiled-in names, and with the references to each name in the 249
node-graph files listed by `node_graph_files.xtbl` (249 of 249 extracted from `misc_tables.vpp_pc`; one is
overridden by `patch_compressed.vpp_pc`, and that version was used). A "reference" is a graph element whose text
equals the name, case-insensitively, or a reference to a `control_filters.xtbl` filter whose `source_name` chain
ends at the name. This is a data-side count only; the grammar is U2's.

| Class | Count | Names |
|---|---|---|
| Written by code **and** referenced by a graph | 57 | the rest |
| Written by code, **referenced by no graph** | 5 | `move forward bool`, `move left bool`, `move right bool`, `move back bool`, `aim bool`. All are written every frame by `0x00958690` but read by no shipped graph, so a port may skip them. |
| Referenced by a graph, **no compiled-in writer** | 10 | `locomotion` (1 file), `cower type` (6), `idle type` (1), `cower bool` (2), `speed` (1), `Left Foot Bool` (2), `Weapon Equipped` (2), `Sprint Holster Bool` (2), `Coop Rider Stage Number` (1), `is statue broken` (1). Each is either written by name through §4.3 (script or network, when the graph is an object's) or holds 0 / false for good after the first update (§3.5) (HYPOTHESIS which applies per name, §5.1). |
| Neither written nor referenced | 15 | `turnto`, `jump bool`, `land bool`, `fall bool`, `supported bool`, `idle bool`, `panic bool`, `land hard bool`, `land slam bool`, `getup bool`, `l deflection`, `stand bool`, `Weapon Group`, `Right Foot Bool`, `Left Stick Horizontal` (vestigial rows; `turnto` is the source of two `turnto linear map filter` rows that no graph uses) |

So the scoping note's "~56 compiled-in names" is **62**, all shipped; **25** shipped rows have no compiled-in writer.

---

## 5. OPEN items, and side findings for other files

### 5.1 OPEN

1. **Graph evaluation before the first copy.** Does any play path evaluate a freshly instanced graph (seeing
   `default_value`s) before `0x004b0b80` first overwrites them (§3.5)? This decides whether the three non-zero
   defaults are ever observable. Next step: U3, from `0x004b2d70` / `0x004b2b50` / `0x004cc000`.
2. **Instance slot byte `+4`.** It is cleared at instancing and not touched by the per-frame copy. Is it a
   "changed" flag the evaluator sets (U3)?
3. **Range use.** The registry stores `min_value`/`max_value` (§1.2), but no setter clamps. Does the evaluator or
   a filter read registry `+0x0c`/`+0x10` (U3)?
4. **The child ring.** What controllers attach as children (§3.3), and does any shipped path build a ring longer
   than one level?
5. **The ten graph-referenced names with no compiled-in writer** (§4.6). Are their graphs object graphs (reachable
   through `mesh_mover_update_control` / the network) or human graphs (permanently 0)? This needs U2's per-file root
   and owner mapping.
6. **The freeze rule's flag.** Slot flag `0x02` (§3.5) carries the scoping note's label "fading in". A slot that is
   *not current* yet "fading in" is odd; U1 should re-derive the slot flags.
7. **The remote path.** Confirm that `0x008addb0` is a locality / ownership test and that message `0x45` is the
   parameter-forward message (network scope; not needed for single player).
8. **Release.** The block's free path, and whether a controller can lose its block and regain a fresh zeroed one
   (§3.4).
9. **Helper semantics behind the human rows** (HIGH rows of §4.2): the angle-wrap range of `0x00dadeb0`, the
   predicates `0x00994f60`/`0x00994f40` (free fall / parachute family by name use), `0x009ba8b0`, `0x00941e60`,
   `0x006c4990`/`0x006c4430`, and the aim-vector helpers. These sit on the inputs side (`review/scoping-player.md`
   U3) or need a one-function read each.

**Added by U1 (§6); items 1, 6 and 8 above are closed by §6.10, and item 2 is answered for its consumer side.**

10. **Re-trigger data.** Animation-record bytes `+0x24`…`+0x27` and the pointer at `+0x30` drive §6.8, but no writer
    was found (§6.8). Look for a dword or block store through a computed record pointer, or watch the bytes at run
    time. If they stay zero, the re-trigger and linked-child machinery is dead code.
11. **Phase sync** (§6.7 step 6). Controller `+0x1a4` bit `0x40` is not cleared after a sync is applied, so the sync
    repeats every update while its condition holds. Confirm the effect at run time; the "hold the locomotion phase"
    reading is a HYPOTHESIS.
12. **Blend-input setters on child rings.** `0x004b72a0`/`0x004b7320` replay on children through the stop routine
    `0x004b41b0`, not through themselves (§6.6). Determine whether any controller that receives them has children
    (with item 4).
13. **Minor slot flags.** The setter of `0x1000` (none in `0x004af000`…`0x004b8600`), the routine behind `0x2000`
    (`0x004b7f07`/`0x004b7f1c`, and `0x004b7f80` on free), and controller `+0x1a4` bit `0x10`.
14. **The layer API the slots call** (U4): weight posting by maximum in `0x004c6590`; fade `0x004c6d90`/`0x004c6c70`;
    phase `0x004c7510`; rate `0x004be510`; the gone test `0x004bdd40`/`0x004c6350`; clip creation `0x004bce30` →
    `0x004c6980`, which consumes f1–f3 (Q13.2's OPEN); and the held layer at instance `+0x5c`.
15. **The `Action` branch of `0x004b0080`** and `0x004afee0`. No shipped row has `Actions` (§6.11). Specify it only
    if modded `anim_blend_trees.xtbl` rows are in scope.
16. **Resource hazards.** Check whether the graph-row leak of `0x004b2b50` and the in-place eviction of `0x004b2280`
    (§6.5, §6.8) are reachable.
17. **`0x004cc000` beyond its first 140 instructions** (U3): who sets graph-row `+0x48` bit `0x04`, and the global
    `0x029c9964` that it compares with a clip node's `match_name` hash.

The scoping note's OPEN items 2 (both halves) and 7, and 9, are closed by §3.5, §1.2–§1.3 and §4.6.

**Added by U2 (§7); item 5 above is answered by §7.13.**

18. **Graph `+0x0c`.** The file-name CRC that the loader stores (§7.2) has no reader in the code read for U2, and
    graph-to-animation linking uses the CRC of `name` instead. Find a reader, or record the field as unused.
19. **The 31 unreferenced graphs** (§7.13). Check zone data, and any by-name play from code with a computed name, for a
    route that instances any of them. This decides whether the ten parameters of item 5 are ever read.
20. **Run-time meaning of the clip fields** (U3): clip-block `+0x0c`, `clip_name`, `start_time`, `layer`, the "do not
    loop" bit, and the node constants `+0x18`/`+0x1a` (§7.4, §7.8).
21. **Local control parameters** (U3). Confirm at the evaluator that a `local control parameter` operand reads the
    condition's private `default_value`, and whether its `min_value`/`max_value` matter (§7.7).
22. **Control points out of ascending order** (18 inputs, §7.12). How does the multi-way-blend evaluator `0x004bc0d0`
    treat them (U3)? The one patched file was reordered to ascending ranges.
23. **Unresolved `tag_name`s** (9 nodes, §7.12). What does a clip block with registry index −1 play (U3)?
24. **Graph-file directory.** The open call passes only `<name>.xtbl`. Whether manifest slot 15
    (`tables/anim/network/node_graphs/`) supplies the directory was not traced inside `0x00dc5ac0`.

Item 17, in part: the global `0x029c9964` is the engine-wide "no name" hash value. It has 789 code references, and the
node initialisers store it as the `alias_name` and `match_name` hashes, which the parsers replace only when the element
is present (§7.4). Comparing a clip node's `match_name` hash with
it therefore tests "this clip node has no `match_name`" (HIGH).

**Added by U3 (§8); U0 items 1, 2 and 3 are closed and items 17 (first half), 20 (mostly), 21, 22 and 23 are answered by
§8.11.**

25. **Animation-instance float `+0x60`.** `0x004bcdb0` reads it as the phase reference for `cyclic_percent` alignment
    (§8.3). What it holds is U4's question.
26. **The root-motion sync outputs.** Row byte `+0x1c` and floats `+0x20`/`+0x24` are set from a
    `sync_controller_transform` transition in progress (§8.3). Their consumer was not traced (U5/U7).
27. **Blend-node constants `+0x18`/`+0x1a`** (U2 §7.8). No run-time slot read for U3 reads them. Find a reader, or
    record them as unused.
28. **State-machine record time.** The (graph, −1, −1) instance's `+0x08` is advanced every frame, but no reader was found.
29. **First-frame `?%` percent.** On the frame a clip node is created, its instance `+0x38` (clip index) has not yet been
    written by the post step, so a `clip time as percent` operand on `?` divides by the duration of a stale clip index
    (§8.2, §8.5). The numerator is 0 on that frame, so only a zero duration would matter. Confirm at run time.
30. **Reachability of `spinslower_sm` and `tiger_ride_sm`.** Their default states emit nothing (§8.6). Trace the play
    path that reaches them, to say whether the player ever sees an empty graph state.
31. **Post-step weighting** (U1 §6.10 step 5, U4). Bit 1 entries are kept at weight 0.0001 rather than dropped, and the
    layer-time setters `0x004bd9b0`/`0x004bda10` are fed the clip times of §8.6. Their layer-side effect is U4's.

**Added by U5 (§9).** U1 items 13 and 14 are answered, and item 11 is extended, by §9.13. `spec-player-runtime.md` OPEN 39 is answered except for the items below.

32. **The human-side drivers of the spine-bend chain** (U6). The chain's inputs are written by these, all read here only for the contract:
    - `0x009b0e90`: the directions `+0x60`/`+0x70` and the speeds `+0x80`/`+0x84`;
    - `0x009b20a0`: the joint mask `+0xd8` (which values, and what `0x400` means);
    - `0x009b34f0`: the limit scale and flags;
    - `0x009b4360` / `0x009b0900`: `+0xac`.

    Also: what `0x009b3a20` does with the unabsorbed yaw `+0xa8`. HIGH: it swaps the current slot's clip through `0x004b73f0`, such as a turn-to-look clip.
33. **Flags with no setter found.** The spine-bend object's `+0x30` bits `0x01` (disabled) and `0x8000` (snap pitch); the global byte `0x03171bca`; and controller `+0x1a4` bit `0x10` (skip corrections). If they are never set, they are dead switches. *Next step:* watch them at run time, or scan for writes through a computed pointer.
34. **The synced-pair path `0x004b50a0`** (1,277 instructions). It replaces the correction step while a slot carries flag `0x2000`, and it reads X (`+0x70`) and `+0xe0`. How a synced pair aligns its two characters is not specified here. *Next step:* a full read, with `0x0095f5b0` (`spec-player-runtime.md` §5.4.6) as the caller side.
35. **What turns a human outside corrections.** `0x004b6530` returns the orientation unchanged outside a window (§9.7), so ordinary heading changes come from elsewhere. Candidates: the facing written by U2/U3 through the human's virtual slot `+0x48`, the NPC heading shaper `0x009918b0`, or a root-rotation path not found in this pass. *Next step:* list every caller of virtual slot `+0x48` and every writer of human `+0x4c`…`+0x6c`.
36. **Override modes 3–8 of `0x004c50f0`.** Only modes 1 (this module) and 2 (the shoulder writes) were read. Also: the second 3×3 and the float that modes 4 and 6 store through `0x004c7df0`, and who uses those modes.
37. **`0x004ca010`**, the animated per-track override value. This is `spec-anim-format.md` §10.5's open item, reached here through `0x004cad00` for override kind ≥ 2.
38. **Other trigger names.** The `Spinebend` trigger (105 rows: flinches) and the `free move` trigger (415 rows) are hashed at `0x01350700`/`0x01350704` and read by the graph post step and the flag summary (`0x004b0f1f`, `0x004b0f48`, `0x004b3501`, `0x004b3522`); their effect was not traced. `rot dest correction start`/`end` (one row) has no reader.
39. **`0x004c8a80` bit for bit.** Its structure is identified (§9.9). Checking its decode against `spec-anim-format.md` §6c.2/§6c.3 is a format-spec task, not a runtime one.

**Added by U7 (§10).**

40. **Vehicle tail logic** (`0x00a760e0`'s `Airplane Mode Bool` block; `0x00a76690`'s end-of-function branch, §10.3). Both contain vehicle-type-conditional calls (`0x0045f5b0`/`0x00556750`/`0x00b1a020`, by-name ids through `0x00d9e8b0` rather than `0x004cbaf0`) outside the U0 parameter contract — read as telemetry/achievement or VFX/audio triggers tied to a state transition, not traced past the shape. Low priority; flagged so a future reader doesn't mistake either for a parameter write.
41. ~~**The vehicle's per-bone matrix overlay** (`0x00a76690` step 5, §10.3: `0x00595d20` transpose + `0x004c0230` re-orthonormalize, sourced from vehicle `+0x1d88`/`+0x1dac` tables). Confirmed by listing to be real per-bone matrix math layered after the shared compose step, keyed by a small per-vehicle-type table. Not confirmed: which bones it targets, or whether "wheel spin"/"suspension offset" is the right reading versus something else (a turret, landing gear, a rotor). *Next step:* read the two tables for a representative car/bike/helicopter and check whether the resulting bone indices land on wheel/suspension-named rig bones.~~ **RESOLVED 2026-10-08, REFUTED as a wheel/suspension mechanism (direct follow-up, dispatched to check exactly this):** full decompile of `0x00595d20` shows it is a generic 4×4→compact-3×4 matrix packer, applied here only to the vehicle's single "anchor" bone (part-remap index 0 — matching the `+0x3C` anchor-bone name `spec-vehicle-geometry.md` already documents), with the result cached for unrelated gameplay consumers (`0x00a7fa20`). Not wheel-related, not suspension-related, not a turret/landing-gear/rotor overlay either — a one-bone anchor-matrix cache, full stop. The real wheel steer/spin/suspension mechanisms are elsewhere entirely — see `spec-vehicle-geometry.md`'s new wheel-render-pose section. Full detail: `scratchpad/vehicle_wheel_render_pose_2026-10-08.md`.
42. **`0x004b4030`'s exact role** (§10.3 step 6, §10.4 step 9). Confirmed by listing to walk the controller's `0x84`-byte slot array (U1's layout) and, on a condition involving the controller's current instance handle and that instance's `+0x5c` field, zero controller `+0x198`/`+0x19c`-area fields — read as "clear a finished/expired slot," not confirmed against the rest of U1/U3's controller-update semantics. Called identically by the vehicle driver and `object_rig` right after compose; not in the original shape diagram for `0x004b3720`'s family. *Next step:* U1 should check whether the human path calls it too (perhaps folded inside `0x004b3720` itself, not traced here).
43. **`0x004bd4a0`/`0x004c2060`, `0x004bd130`/`0x004c19a0`, `0x004bcff0`, `0x004be950`.** Confirmed by listing to be thin validated-handle wrappers into U4's layer-sampler/instance-handle sub-band (`0x004bc000`–`0x004ca000`); their exact semantics are U4's territory, not traced past the wrapper shape. Noted so U4 knows these four are exercised by non-human clients too (`object_rig`, `0x00900540`, `0x00921fa0`).
44. **`0x00722130` vs `0x00722000`.** The vehicle's own per-frame driver (`0x00a76690`) selects its advance dt through `0x00722130`, a different address from the `0x00722000` every other dt-selecting client uses (the human poster, the vehicle's own parameter writer `0x00a760e0`, and `0x00900540`). Not disassembled. *Next step:* disassemble `0x00722130` and diff against `0x00722000` — may be a non-finding (identical forwarding wrapper) or a genuinely different rule.
45. **The object category behind `0x00929da0`/`0x009296a0`/`0x00921fa0`'s list** (§10.5.2; count/index at `[0x03171a64+0xb4]`/`[+0xac]`). Identified only as "some physics-synced world-object category, a subset of which carries a secondary rig at `self+0x1f4`" — not pinned to a specific RTTI class or game concept. The achievement tail at the end of `0x00929da0`'s walk (vehicle-class-keyed, on the local player's current vehicle) is a red herring for identifying the list itself. *Next step:* an RTTI/vtable check on a live instance from this list, or trace who populates `[0x03171a64+0xb4]`'s backing array.
46. **The object category behind `0x00900990`/`0x00900540`'s list** (§10.5.2; count/index at `[0x03171a64+0x1d4]`/`[+0x1cc]`). HIGH, not CONFIRMED, that this is a motorcycle/two-wheeler-linked secondary rig, from the shared `{+0x10,+0x14}` component shape and the cross-reference to the bike-riding gate `0x009b9160` when the linked target is a human. The lean-value computation itself (division/clamp against `0x12a2d70`/`0x11169c0`/`0x12a2dc0`, stored at a third pointer's `+0x54`) borders Player U6.8's territory (the rider-lean writer `0x00af4b80`) and was not traced further. *Next step:* once U6.8 lands, check whether it already answers this as a side-effect of tracing `0x00af4b80`'s own context, before spending more time here.
47. **`0x00b8f580`** (called from `0x00b8fef0`'s circular-list walk at global `0x0130f434`, before the weapon-pose loop, §10.5.1). Not traced; shape suggests unrelated per-node maintenance (no controller/instance fields touched), possibly weapon-inventory bookkeeping. Low priority.
48. **Exact byte semantics of `object_rig` status flags `+0x130` and the per-part skip array `+0xb0`** (§10.4), and of `0x00900540`/`0x00921fa0`'s own status bytes (`+0x33`, `+0x3a`, `+0xdb`, `+0xdc`, `+0x200`/`+0x201` on the `0x009296a0`/`0x00929da0` side, §10.5.2). Read only far enough to follow control flow for this unit's purposes; individual bit meanings beyond "in use"/"skip"/"valid" were not catalogued.

### 5.2 Corrections to `review/scoping-animation.md` (recorded here; that note was not edited)

- `0x004b1490` is the **integer setter**, not a reader (§3.2); `0x009896d0` uses it for `Climb Duration ms`.
- Type code 2 = `integer` is CONFIRMED; the three record dwords are typed default / min / max (§1.2–§1.3).
- `0x004b8d90` is a **graph-scoped** resolver returning a local alias index, not a by-name variant of
  `0x004cbaf0` (§2.2).
- The writer table lacks `0x00a3c910` (`animation_sequence_advance`, `Ball Rider Stage Number`), the five literal
  names of the graph-scoped callers, and the direct block writer `0x0089f3b0` (§4.1, §4.3).
- 62 distinct compiled-in names, not ~56; 64 resolver sites including two inside the library (§4.1).
- `@Reloading Bool` → `Reloading Bool` (§4.2).
- `0x004b1590` / controller `+0x2c` is a time step (1/30 s by default), not a playback-rate scale (§3.6).
- OPEN item 2: values reach the graph instance by a per-frame alias-wise copy in `0x004b0b80`; defaults are
  applied at instancing (`0x004cbd20`) and overwritten by that copy (§3.5).

**Added by U1 — corrections to U0 (this file) and further corrections to the scoping note:**

- **U0 §3.4** said no site frees or re-zeroes the block. **Wrong.** At the end of its slot loop, the update returns
  the block to the pool when no managed slot was processed (`0x004b3d23`…`0x004b3d42`). A later managed play
  allocates a fresh zeroed block (§6.7 step 5, §6.10; closes §5.1 item 8).
- **U0 §3.5** said the copy covers "every alias with a valid id". The copy skips only id −1; it does not check the
  upper bound (`0x004b0c41`, §6.10).
- **U0 §3.5, consequences.** A `default_value` is **never** seen by the graph evaluator, not "until the first copy"
  (§6.10; closes §5.1 item 1).
- **U0 §3.5, freeze rule.** Flag `0x02` means "cross-fade member", not "fading in". The rule freezes the parameters of
  a graph state that is being cross-faded out (§6.2, §6.10; closes §5.1 item 6).
- **U0 §4.2 row 25.** "State `0x1da9`" is the named clip-node id 7,593, `Transition` (§6.4, §6.11).
- **U0 §4.2, latching.** The latching notes hold only while at least one graph state stays live from update to update
  (§6.10).
- **Scoping §1.4, slot flags.** Flag `0x02` is "cross-fade member" and `0x04` is "explicit-weight slot". The full flag
  table is §6.2.
- **Scoping §1.4, cursors.** `+0x54` is the outgoing slot, not the next one. `+0x58` is the cross-fade duration and
  `+0x5c` the last rate (§6.1).
- **Scoping §1.4, id bounds.** The bounds are the constants 7,589 and 7,603. The 7,589…7,602 ids are 14 named clip-node
  aliases (§6.4).
- **Scoping §1.4, query counts.** The query numbers are function counts. Raw call sites are 339 / 49 / 51 / 19
  (§6.6).
- **Scoping §1.4, slot release.** Slots are freed at exactly zero weight by the separate reaper `0x004b4030`, called
  by the clients, not by the update (§6.3).
- **Scoping §1.4, graph evaluation.** The node graph is evaluated in the per-slot step `0x004b0b80` →
  `0x004cc6c0`, not in `0x004cc000`, which collects clip nodes for the layers (§6.10).

**Added by U2 — further corrections to the scoping note (U2 corrects nothing in U0 or U1):**

- **Population.** There are 127 blend trees, not 128: the 128 counts the `patch_compressed.vpp_pc` copy of
  `free_fall_bt` separately. The 249 files are 122 state machines and 127 blend trees (§7.1).
- **Classes.** There are 23 classes in `0x012a26dc`…`0x012a2a60`, not 20. The expression-tree classes
  `condition_object`, `condition` and `condition_group` were missing (§7.11).
- **The operator strings.** `==`, `!=`, `>=`, `<=`, `and` and `xor` belong to the tokenizer of the transition
  `start` expression (`0x004ba590`). The `comparison` element of `conditions` uses words (`equal`, `less or equal`, …).
  `xor` is tokenized but has no effect (§7.6, §7.7).
- **Parsers.** `0x004cb200` is the alias and filter collector, not a parser, and `0x004cc7b0` is run-time code (§7.9,
  §7.11).
- **Tooling.** U2 did not need a Ghidra pass or function creation: capstone recursive descent from the vtable entries
  read every slot target (§7, method).
- **§1.5's last bullet** is CONFIRMED and refined. The `0x03171bdc` pass keys each graph by the CRC of its `name`, and
  creates the animation record when no `anim_files.xtbl` row supplies one (§7.2).

**Added by U3 — corrections to U2 (this file) and to the scoping note:**

- **U2 §7.4, `loop_flag`.** Block `+0x2c` bit 0 is set **when** the text is `true` (the compare at `0x004b8b26` returns 0
  on a match), so the bit means **loop**, not "do not loop". U2's loop / no-loop counts were taken from the text and
  stand (§8.6).
- **U2 §7.7.** The HYPOTHESIS that a `local control parameter` compares the private record's `default_value` is CONFIRMED
  (§8.5).
- **U2 §7.8 / §7.12.** `max_blend_rate` is a **duration** (seconds for a 0 → 1 change; 0 snaps), not a rate (§8.7).
- **U2 §7.12, out-of-order points.** The evaluator does not assume ascending order. It takes the first segment that
  contains the input, and descending segments are dead. The 12 wrap-around inputs work as authored. Only orders like
  `free_fall_bt`'s base copy, where a later segment overlaps earlier ones, misbehave (§8.7, §8.10).
- **Scoping §1.4 and §5 U3.** "Per-frame `0x004cc000`" is not the graph runtime. The runtime is `0x004cc6c0` and the
  slots it reaches (about 4,300 instructions read, §8). `0x004cc000` serves rows whose clips carry a `match_name` (§8.6).
- **Scoping §6 tooling.** "U2/U3 will need [a Ghidra run]": U3, like U2, read every slot target by capstone recursive
  descent from the vtable entries, with no Ghidra project.
- **Scoping §1.4 line on filters.** CONFIRMED and completed: the filters read the instance's alias-indexed value array.
  The math is §8.8.

**Added by U5 — corrections to U1 (this file) and to the scoping note:**

- U1 §5.1 items 13 and 14 are answered, and item 11 is extended (§9.13).
- `review/scoping-animation.md` §1.6, §1.7, §2.1 and §4.2: the post-sample module (`0x004cfb60` and the module under
  it) is the human's spine-bend and look chain — the library half of U6 — not root-motion extraction or foot
  alignment. It writes nothing the velocity composer, the correction step or the character controller reads (§9.0
  item 1, §9.13).

### 5.3 Side findings for other files (no other file was edited)

- **`spec-tables-ui-controls.md` §14:**
  - the slot-12 reader is `0x004cae20`, with per-row reader `0x004d00e0` (as the scoping note already flagged);
  - **the shipped row count is 87** (52 bool, 29 float, 6 integer; 35 rows with `min_value`/`max_value`);
  - a bool `default_value` other than the literal `false` (case-insensitive), including a missing one, loads as
    true.
- **`spec-lua-api-behaviour.md` §25.6 (`mesh_mover_update_control`):** its OPEN on the internal writes of
  `0x0064a1f0`/`0x008cb380`/`0x008cb400` is answered by §4.3. Each resolves the name against the target object's
  own graph (`+0xa8`), maps it to a global id, and calls the typed setter on the object's controller (`+0x9c`), or
  forwards to `0x008cafc0` when `0x008addb0` is false.
- **`spec-lua-api-behaviour.md`:** `animation_sequence_advance` (`0x00a3c910`) is not documented. Its body is in
  §4.1 here.
- **`review/exe-notes-2026-10-01/interp_tabs.md` Q13.2 step 5:** `0x03171bc8` is the control-parameter count
  (already flagged by the scoping note). The block is allocated at three sites, not one (§3.4).

**Added by U1 (no other file was edited):**

- **`review/exe-notes-2026-10-01/interp_tabs.md` Q13.2:**
  - On the managed route the call returns before the play flags reach `+0x14` and before `+0x70` is armed.
  - An `Additive` (bit 0) record never takes the managed route in `0x004b1b90`, though it does in `0x004b2280`.
  - "Returns 0 when there is no row" holds only for a free slot.
  - The record bits it lists are, by `spec-tables-animation.md` §3.2's table: `0x1` `Additive`, `0x8`
    `Transition_Action`, `0x40` `No_Interpolation`, `0x8000000` `"Transition to 50%"` (§6.1).
  - Its "random hold / re-trigger interval" bytes `+0x24`/`+0x25`/`+0x27` appear never to be written (§6.8).
- **`spec-tables-animation.md` §3.3.** The runtime reads record bytes `+0x24`…`+0x27` and a pointer at `+0x30`
  (§6.8), which that section lists as unwritten by the loader. No writer was found anywhere.
- **`spec-tables-animation.md` §7.**
  - A plain census of the extracted `anim_blend_trees.xtbl` counts **32** `Blend_trees` rows, not 31. Not reconciled.
  - The same census finds no `Actions`, 3–4 `States` per row and 2–4 `Control_point`s per `State`.
  - The runtime keeps per-slot weights for at most 5 `States` and 2 `Actions` (§6.2).
  - An input outside every control-point segment gives a weight target of 0, not the end value.
  - `States` play phase-locked (§6.9).
- **`spec-anim-format.md` §2 / §4 (clip header):**
  - `0x004bd8c0` uses word `+0x06` as the clip duration in 30 Hz frames. This supports Team B's §9.47 reading.
  - The update and the blend-tree evaluator use bytes `+0x08`/`+0x09` as blend-in / blend-out lengths in 30 Hz
    frames (HIGH, from use; §6.7, §6.8).
- **`spec-ai-runtime.md` §1.4 step 3.** The gate's ids are preset registry entries: `0x2be` = `stun flinch` and
  `0x408` = `react sitting shocked`. They are matched against slot `+0x08` (§6.11).
- **`review/exe-notes-2026-10-01/rederive_tables-animation.md` §1.4 item 3.** The "7,589 / 7,589 / 7,603" fields
  and the 14-string list are the slot finder's id-space bounds and the named clip-node alias list (§6.4).

**Added by U2 (no other file was edited):**

- **`spec-tables-traffic-ai.md` §22 and the capacity table that lists `node_graph_files.xtbl`.**
  - The table's "384 (checked)" is not a checked bound. Pass 1 has no row check: rows 0…382 fit, and a 384th row
    overwrites the stack cookie. It also has no name-length check (§7.2).
  - Two behaviours are worth adding:
    - a file that fails to open aborts the remaining loads and both link passes;
    - an unvalidated graph is skipped silently.
  - The file-name CRC is computed on the lower-cased name including `.xtbl`. It is **not** what links graphs to
    animations; that is the CRC of the graph's `name` (§7.2).
- **`spec-xtbl-format.md` §8.** Its "128 `blend_tree` files" counts the patch copy of `free_fall_bt`. There are 127
  distinct blend trees (§7.1).
- **`spec-tables-ui-controls.md` §14.**
  - The filter reader accepts four kinds. The fourth, `multiplier` (elements `basis` and `multiplier`), is unused by
    the 22 shipped rows: 9 `cap`, 7 `linear map`, 6 `delta multiplier`.
  - `validated` is never read.
  - A missing float element loads as 0.0.
  - An unknown `type`, or a `source_name` that names neither a parameter nor a filter, crashes the loader (§7.10).
- **`spec-tables-animation.md` §2 / §3.3 (the `0x03171bdc` pass of `0x004aea30`).** For every accepted graph, the
  pass:
  - looks up the CRC-32 (seed 0, lower-cased) of the graph's `name` in the loaded-animation hashtable;
  - creates a `0x44`-byte record if none has that key (capacity 6,500);
  - then sets bit `0x80000000` and writes the graph index to `+0x08`.

  So the "input list" is the accepted node-graph array, and every graph is an animation named by its `name` (§7.2).
- **U1 §6.4.** The clip-node offsets there are counted from node `+0x04`, as `0x004cbcb0` does. They agree with
  §7.4's clip block, so no correction is needed.


**Added by U3 (no other file was edited):**

- **`spec-tables-ui-controls.md` §14 (`control_filters.xtbl`).** The row semantics are now known (§8.8):
  - `linear map` clamps its input to [`input_min`, `input_max`] and maps it linearly onto [`output_min`, `output_max`];
  - `delta multiplier` smooths toward its input at `multiplier` per second (out += `multiplier` × dt × (in − out),
    starting from 0);
  - `cap` clamps to [`output_min`, `output_max`];
  - `multiplier` gives `basis` + (in − `basis`) × `multiplier`.

  A filter used as another's source is evaluated first. A `delta multiplier` in that position runs twice per frame
  (`Turn Angle Delta Multi`).
- **`spec-tables-ui-controls.md` §14 (`control_parameters.xtbl`).** `min_value`/`max_value` are never read at run time
  (§8.8).
- **`spec-tables-animation.md` §3.3.** A node graph's `clip_name` is resolved at run time by the same lower-cased CRC-32
  key as `anim_files.xtbl` `Filename`, through the loaded-animation hashtable `0x0340b220` (`0x004bf650`). A `tag_name`
  is resolved per character through the row's two anim-group tables (`0x004ccba0`) (§8.6).

**Added by U5 — unlike previous units, `spec-player-runtime.md` was edited directly, not only recorded here:**

- **`spec-player-runtime.md` OPEN 39** is answered for the orientation step (`0x004b6530`, §9.7), `0x004b49e0`,
  `0x009987d0`, controller `+0x1a4`, `0x004cad00` and the creator at `0x004c6ebb` (all §9.9), and controller
  `+0x1a4`'s full bit table (§9.10); `0x004c8a80` is identified and its bit-for-bit decode is routed to
  `spec-anim-format.md`. What remains is re-scoped as this file's §5.1 items 32–39 (§9.13).
- **`spec-player-runtime.md` §5.0 item 4, §5.3 and §5.4.5** said orientation comes from the animation's root
  rotation through X and `0x004b6530`. That is corrected in both files: `0x004b6530` applies no rotation outside
  an authored correction window; X feeds only that window's motion warp (§9.7, §9.13). What turns a human in
  ordinary locomotion is left as this file's OPEN item 35.

---

## 6. U1 — Controller state slots and the play / query API

**Scope.** Unit U1 of `review/scoping-animation.md` §5: the controller's state-slot array, the three play entries
and their siblings, the slot finder and its four query wrappers, the cross-fade and slot bookkeeping of the
controller update `0x004b3720`, the re-trigger step `0x004b2d70`, and the 1-D blend-tree evaluator `0x004b0080`.
§6.10 re-reads the per-slot graph step `0x004b0b80` (U0 §3.5), because that is where the question of
§5.1 item 1 is decided.

**Method.** The same Ghidra-free toolchain as U0. Every function was read as a capstone listing produced by
recursive descent over its body (`exe_va.py` byte reads). Call-site counts come from a raw `E8`/`E9` rel32 scan of
`.text`, and global references from a raw dword scan. Shipped data came from the already-extracted
`tools/an_xtbl/anim_xtbl/` tables. No Ghidra project was opened or copied. Helper scripts are in the session
scratchpad (`scratchpad/u1/`).

**Read in full** (instruction counts from the recursive-descent listing, which match the scoping census):
- the slot finder `0x004afa20` (201) and the play entries `0x004b1b90` (241), `0x004b21a0` (88), `0x004b2280` (373);
- the queries `0x004b4ce0` (11), `0x004b41b0` (90), `0x004b4320` (43), `0x004b44a0` (117);
- the update `0x004b3720` (644), the re-trigger step `0x004b2d70` (524), the blend-tree evaluator `0x004b0080` (772)
  and the graph step `0x004b0b80` (425);
- the slot-array helpers `0x004afc70` (144, free), `0x004b11a0` (122, grow), `0x004afe50` (52, sort) and
  `0x004b4030` (122, reaper);
- the record-index plays `0x004b1ef0` (167) and `0x004b2b50` (142), and the slot-flag summariser `0x004b3570` (156)
  with `0x004b34c0` (57);
- the graph instancing and evaluation entries `0x004cbd20` (209) and `0x004cc6c0` (90);
- the easing helper `0x00dad960` (112);
- the small accessors `0x004cc6a0`, `0x004cbcb0`, `0x004bdb50`, `0x004bd8c0`, `0x004be510`, `0x004c7510`,
  `0x004c6d90`, `0x004c6c70`, `0x004c7090` and `0x004c6590`.

`0x004cc000` was read for its first 140 instructions only (§6.10).

**Cited, not re-derived.** `review/exe-notes-2026-10-01/interp_tabs.md` Q13.2 already traced `0x004b1b90`. §6.5
re-reads it only for allocation, eviction and the managed route, and records five small corrections to Q13.2
(§5.2).

### 6.1 Controller fields touched by the slot machinery (CONFIRMED by listing; roles HIGH unless stated) **[corrects scoping §1.4]**

| Offset | Content |
|---|---|
| `+0x24` | byte: **number of managed (node-graph) slots processed in the last update**. The update zeroes it before its slot loop and adds 1 after each managed slot's graph step (`0x004b3cfc`). Each managed play (`0x004b1b90`, `0x004b2280`, `0x004b2b50`) also adds 1 at once, so it is non-zero from the moment a graph state starts. The scoping label "count of node-graph states" is right in effect. |
| `+0x48` / `+0x4c` | slot count / slot array (`0x84`-byte slots, §6.2) |
| `+0x50` | pointer to the **current slot**, or 0. Only `0x004b2280` sets it (§6.5). |
| `+0x54` | pointer to the **outgoing slot**, or 0. `0x004b2280` sets it to the highest-weight cross-fade member other than the current slot. The slot finder's id 10002 reads it (§6.4). **[corrects scoping: not a "next" cursor]** |
| `+0x58` | cross-fade duration in seconds (written by `0x004b2280`, §6.5) |
| `+0x5c` | the cross-fade rate used in the last update (§6.7) |
| `+0x60` / `+0x64` | the "current pair" (state id, slot `+0x04`), set by plays carrying flag `0x200000`. It is cleared to −1/−1 when the slot whose `+0x08` equals `+0x60` is freed (`0x004afcf5`). |
| `+0x198` / `+0x19c` | the OR of the animation-record flag words (record `+0x2c`) of every playing animation, and of the current or non-blending ones only. They are rebuilt by `0x004b3570` / `0x004b34c0` and by the graph step. Bits 1–2 (`Block_Rotation`, `Block_Translation`) and bit 7 (`No_Spinebending`) are masked out when the instance query `0x004bddb0` reports an override. |
| `+0x1a0` | phase offset for phase-synchronised transitions: 0.0, or 0.5 for records with `"Transition to 50%"` (§6.7 step 6) |
| `+0x1a4` | bit `0x40` phase sync pending; bit `0x200` restart the current clip at phase 0 (one-shot); bit `0x10` cleared when a slot with flag `0x1000` is freed |

Animation-record flag bits are named from the 30-entry table in `spec-tables-animation.md` §3.2. Bit 0 is
`Additive`, bit 3 `Transition_Action`, bit 6 `No_Interpolation` and bit 27 `"Transition to 50%"`. Bits 30 and 31
are the blend-tree and managed marks (§3.3 there).

### 6.2 The `0x84`-byte slot (CONFIRMED by listing unless marked) **[completes scoping §1.4]**

| Offset | Size | Content | Kinds |
|---|---|---|---|
| `+0x00` | 4 | animation-record index (`0x02ef2178` + `0x44` × index); −1 in an empty slot | all |
| `+0x04` | 4 | **layer handle** in the controller's animation instance: the layer's serial number shifted left by 4, as returned by `0x004c6590` and `0x004bce30`. For plain clips the update re-posts the layer every frame and rewrites this. It is −1 when the effective weight is below 0.0001 or the pose is in flight. For blend-tree and managed slots it is the layer of the highest-weighted clip. The slot finder's last partition matches it (§6.4). | all |
| `+0x08` | 4 | state/action id (registry index, `spec-tables-animation.md` §5), or −1 when started by record index (`0x004b1ef0`, `0x004b2b50`) | all |
| `+0x0c` | 4 | caller tag: free-form, but by convention in [7,500, 7,589) so the slot finder can match it (§6.4) | all |
| `+0x10` | 4 | slot weight, 0…1 | all |
| `+0x14` | 4 | the play flags as passed by the caller, with `0x2000` added for `Additive` records and `0x10000` for `No_Interpolation` records. They are forwarded, OR 1, as the layer flags (§6.7). | plain, blend tree |
| `+0x18` | 4 | blend-tree input at initialisation | blend tree |
| `+0x1c` | 4 | blend-tree input target (set by `0x004b7320`, §6.6) | blend tree |
| `+0x20` | 4 | blend-tree input, current (ramped toward `+0x1c`) | blend tree |
| `+0x24` | 4 | blend-tree shared phase (§6.9) | blend tree |
| `+0x28`…`+0x3b` | 5 × 4 | per-`State` weights, one float per `State` of the blend-tree row | blend tree |
| `+0x3c`…`+0x63` | 2 × `0x14` | per-`Action` runtime records (HIGH layout: `+0x00` countdown, `+0x04` weight, `+0x08` progress, `+0x0c` layer handle, `+0x10` flags; bit 0 active, bit 1 ending) | blend tree |
| `+0x64` | 1 | blend-tree input initialised | blend tree |
| `+0x68` | 4 | blend-tree row pointer, `0x02eebd58` + `0x20` × row (`spec-tables-animation.md` §7.3) | blend tree |
| `+0x6c` | 4 | node-graph instance row (`0x03133a90` + `0x4c` × row), −1 when none | managed |
| `+0x70` | 4 | re-trigger deadline (timer word, −1 = disarmed) | any with record byte `+0x27` ≠ 0 |
| `+0x74` | 4 | sub-action deadline (timer word) | same |
| `+0x78` | 4 | pointer to a linked partner slot (§6.8), rebased by grow and by compaction | same |
| `+0x7c` | 4 | no access found in any function read for this unit | — |
| `+0x80` | 4 | slot flags (below) | all |

The per-`State` and per-`Action` areas are packed between `+0x28` and `+0x64`. **A slot can therefore hold at most
5 `State` weights and 2 `Action` records** (CONFIRMED from the offsets). The shipped `anim_blend_trees.xtbl` stays
inside both limits (§6.11): every row has 3 or 4 `States` and none has `Actions`. A port should reject rows that
exceed them; the engine does not check.

**Slot flags (`+0x80`).** The scoping labels for `0x02` and `0x04` were wrong **[corrects scoping §1.4]**:

| Bit | Meaning | Set by | Cleared / consumed by |
|---|---|---|---|
| `0x01` | in use | every play | free (`0x004afc70`) |
| `0x02` | **cross-fade member** (not "fading in"): the slot's weight is driven by the cross-fade of §6.7 | **only `0x004b2280`** | free |
| `0x04` | **explicit-weight slot** (not "fading out"): the slot's weight is set by its caller, outside the cross-fade | **only `0x004b2b50`** (reached from `0x004b2a20` and `0x004b2d70`) | free |
| `0x08` | blend-tree slot (record bit 30) | `0x004b2280`, `0x004b2b50` | — |
| `0x10` | managed (node-graph) slot (record bit 31) | `0x004b1b90`, `0x004b2280`, `0x004b2b50` | — |
| `0x20` | transition-clip slot: the secondary clip of a `0x004b21a0` request (§6.5) | `0x004b2280` | faded out over 0.2 s by the next `0x004b2280` |
| `0x40` | linked child (weight scaled by the partner's, §6.7 step 4) | `0x004b2d70` | — |
| `0x80` | partner of a "replace" child: this slot's weight is scaled by (1 − child weight) | `0x004b2d70` (entry kind 1) | — |
| `0x100` | child posted with its own layer | `0x004b2d70` | — |
| `0x200` | child fade-in pending | not found in the functions read | `0x004b2d70` tail |
| `0x400` | **phase-synchronised / restart-in-place**: record `Transition_Action`, or play flag `0x4000` | `0x004b1b90`, `0x004b1ef0` | cleared on every new play into the slot |
| `0x800` | child fading out over the clip's blend-out length | `0x004b2d70` tail | — |
| `0x1000` | on free, clears controller `+0x1a4` bit `0x10` | no setter in `0x004af000`…`0x004b8600` | free |
| `0x2000` | on free, calls `0x004b7f80(controller)` | `0x004b7f07`, `0x004b7f1c` (not read) | `0x004b8023`, free |

So **flag `0x02` on a slot that is not current means "being cross-faded out"**. This settles §5.1 item 6, see §6.10.

**Not the instance layer flags [added 2026-10-03].** The animation instance's `0x50`-byte layer records (from instance `+0x70`) carry their own flag word at layer `+0x10`; for layer 0 that is instance `+0x80`, so listings that read the instance appear to test "`+0x80` bit 0". That bit is not "in use": it marks the layer as a member of the instance's **phase-synchronised group**. `0x004c6590` (every plain-clip and blend-tree post, and looping graph clips through `0x004bce90`) always sets it; `0x004c6980` (non-looping graph clips with flags | `0x100400`, and the plays' first creation) does not. The advance `0x004c4810` (→ `0x004c3d20` shared phase at instance rate `+0x68`, or `0x004c45a0` per-layer clocks) and the root-motion sum `0x004c58c0` both split on it. Full account: `spec-player-runtime.md` §5.4.3, "Step 3 in detail".

### 6.3 Slot-array management (CONFIRMED by listing)

- **The in-use slots are always a contiguous prefix of the array.** The free routine `0x004afc70(controller, index)`
  fills the hole. It moves the last in-use slot of the run that follows the hole into the hole (a `0x21`-dword
  copy). Order is not preserved.
  - It then redirects every `+0x78` link that pointed at the moved slot.
  - It fixes `+0x50`/`+0x54`: a pointer to the freed slot becomes 0, and a pointer to the moved slot follows it.
  - It zeroes the vacated slot and sets its `+0x00` and `+0x6c` to −1.

  Before the move, it runs the side effects of flags `0x2000`/`0x1000`. It unlinks a `+0x78` partner (zeroing the
  partner's weight) unless this slot is the child (`0x40`), clears the "current pair" if it matches, and releases
  the graph instance (`0x004cc590`).
- Because of this invariant, every scan in the library (the slot finder, all play searches, the sort `0x004afe50`
  and the update loop) **stops at the first free slot**.
  - The state-id partition of the finder has a quirk: it continues past a free slot 0 (`0x004afa65`). With the
    invariant this cannot change a result.
- **Growth, not eviction.** When no slot qualifies, `0x004b11a0` (controller in `ESI`) grows the array by exactly
  one slot.
  - It first asks the pool object `0x03424868` whether `0x84 × (count+1)` bytes can be had (`0x00db5820`), then
    allocates, copies, rebases `+0x50`, `+0x54` and every slot's `+0x78`, frees the old array, and initialises the
    new slot (zeroed, `+0x00` = `+0x6c` = −1).
  - **There is no cap.** Only pool exhaustion stops growth.
  - On exhaustion, `0x004b1b90`, `0x004b1ef0` and `0x004b2b50` return −1/0 and start nothing.
  - **`0x004b2280` alone evicts.** It takes the cross-fade member (flag `0x02`) with the smallest weight below 50.0
    (`0x01351c34`), i.e. the lowest-weight member. If there is none, it starts nothing.
- **Reaping happens outside the update.** `0x004b4030(controller)` is called by the clients after their update:
  `0x008cbaf7` (object rig), `0x009a039b`/`0x009a055d` (human) and `0x00a76a7a` (vehicle). For each in-use slot
  with a record, it re-examines the same index after a free.
  - A slot with flag `0x02` or `0x04` is freed when its weight is exactly 0.0 and it is not the current slot.
  - Any other slot is freed when:
    - managed: its weight is 0.0;
    - plain: `0x004bdd40` reports its layer gone, meaning the instance is invalid, or the layer is missing or
      ~~inactive (layer `+0x10` bit 0 clear) with zero weight~~ outside the phase-synchronised group (layer
      `+0x10` bit 0 clear, §6.2) with no play time left: `0x004c6350` returns the remaining time in seconds
      (current ticks under layer flag `0x04`, otherwise `0x004ca5f0` − current; clamped at 0; / 4,800) and
      the test is equality with 0.0 **[corrected 2026-10-03: both read in full; the second test is time, not
      weight]**. **Cross-reference (2026-10-07): §11.7 restates this same function from an independent read and
      gives a third, narrower description (handle validity only, with no mention of the layer-flag/play-time half
      below); that restatement is wrong and is corrected in place there. This §6.3 reading — including the role of
      `0x004c6350` as the remaining-play-time helper `0x004bdd40` itself falls back on — is the one CONFIRMED
      against the disassembly (full listings of `0x004bdd40`, `0x004c6350`, `0x004c60a0` and `0x004ca5f0`,
      2026-10-07) and is the one a port should follow.**

    In both cases the slot's layer must not be the instance's held layer (instance `+0x5c` × 16 = slot `+0x04`).
  - If anything was freed, it rebuilds `+0x198`/`+0x19c` over all slots.
- **"Freed below 0.001" is approximately right.** The cross-fade zeroes a weight outright once the step would bring
  it within 0.001 of zero (§6.7 step 3), and the reaper frees exact zeros.

### 6.4 The id space and the slot finder `0x004afa20` (CONFIRMED by full listing; constants CONFIRMED by static read)

**Signature.** Register arguments `EDX` = controller and `ECX` = first slot index. Stack (cdecl): the id, a pointer
to an optional out-dword (set to −1 on entry when non-null), and the first node index. The result is the slot index
in `EAX`, or −1.

Tests in this order:

| Id range | Matched against | Scan rule |
|---|---|---|
| id < 7,500 (`0x1d4c`) | slot `+0x08` (state/action id) | stop at the first free slot (except slot 0, §6.3) |
| 7,500 ≤ id < `[0x03171c20]` = **7,589** | slot `+0x0c` (caller tag) | stop at the first free slot |
| 10,001 (`0x2711`) | the slot whose address is controller `+0x50` (current) | address compare, no in-use test; −1 when `+0x50` is 0 |
| 10,002 (`0x2712`) | the slot at `+0x54` (outgoing) | same |
| −1 | — | always −1 |
| 7,589 ≤ id < `[0x03519854]` = **7,603** | a **named clip node** inside a managed slot's graph instance | every in-use managed slot; see below |
| anything else (≥ 7,603) | slot `+0x04` (layer handle) | stop at the first free slot |

- **The two bounds are game-init constants.** `0x03171c20` is a copy (made by `0x004ccc90`) of the
  manifest-registry field `+0x100c` = 7,589. `0x03519854` is registry field `+0x1024` = 7,603. Both are stored by
  game init, as `review/exe-notes-2026-10-01/rederive_tables-animation.md` §1.4 item 3 records.
  - The scoping note left these bounds symbolic.
  - The cursor ids 10,001/10,002 are reached only because they lie above 7,603. The "up to 7,589" tag range is
    tested before them, so the order is safe.
- **The 14 node ids (CONFIRMED by static read and listing).** Registry field `+0x1018` points at 14 names
  (`0x01151c90`), and `+0x1028` = 14 is their count. Id 7,589 + k is name k:

  | Id | Name | Id | Name |
  |---|---|---|---|
  | 7,589 | `Default` | 7,596 | `VTOL Hover` |
  | 7,590 | `Stand To Run` | 7,597 | `Ride Face Forward` |
  | 7,591 | `Run 180` | 7,598 | `Ride Face Back` |
  | 7,592 | `Sprint 180` | 7,599 | `Ride Forward to Back` |
  | 7,593 | `Transition` | 7,600 | `Ride Back to Forward` |
  | 7,594 | `Climb` | 7,601 | `Fire` |
  | 7,595 | `VTOL Jet` | 7,602 | `Motorcycle Stand` |

  - The node-graph clip-node parser converts a clip node's **`alias_name`** to this id. It calls `0x004b84f0`,
    which hashes the name, compares it case-insensitively with the 14 names, and returns registry `+0x1020`
    (7,589) + k, or −1. The parser stores the result at clip-node definition `+0x18` (`0x004b8b07`).
  - The parser's other fields are `clip_name` → `+0x08`, `match_name` hashed → `+0x20`, and a name resolved
    through the State/Action registry (`0x004bf810`) → `+0x04`. The element grammar is U2's.
- **Node search.** For each in-use slot with flag `0x10`, the finder takes the instance row (`0x004cc6a0`) and
  walks its node instances: count is the word at row `+0x14`, array at row `+0x18`, stride `0x5c`, starting at the
  third argument. It considers node instances with words `+0`/`+2` ≠ −1 and `+4` = −1. It resolves the clip-node
  definition with `0x004cbcb0(graph, node)` (kind 3, sub-kind 1 at `+0x10` or sub-kind 2 with `+0x10` = 3 at
  `+0x24`) and compares definition `+0x18` with the id. On a match it writes the node index to the out-dword and
  returns the slot index. A caller can resume the search from the next node.

### 6.5 The play family (CONFIRMED by listing)

All plays are cdecl, controller first.

| Entry | Arguments | Child ring | Slot choice | Sets current / cross-fade | Managed route |
|---|---|---|---|---|---|
| **`0x004b1b90`** (55 external sites + 1 recursive) | state id, tag, flags, f1, f2, f3 | yes, first | first free, or (unless flags `0x400`) the slot already holding the **same state id**; grow; no eviction | **no**: flag `0x02` is never set and `+0x50` is untouched, so the slot plays at whatever weight it has (1.0 for managed) | graph instance; weight 1.0; returns −1 |
| **`0x004b21a0`** (32 external + 1 recursive + 1 at `0x004b217e`) | state id, tag, transition state id, transition tag, blend seconds, restart byte | yes, first | via `0x004b2280` | **yes** | via `0x004b2280` |
| **`0x004b2280`** (4 external + 1 from `0x004b21a0`) | **record index**, tag, transition record index, transition tag, blend seconds, restart byte | **no** | first free, or the slot holding the **same record** with no graph instance; grow; **evict** the lowest-weight cross-fade member | **yes**: becomes `+0x50`, flags `0x01`/`0x02` | graph instance, no release of an older one (none can exist, see the search rule) |
| `0x004b1ef0` (6 external + 1 from `0x004b2280`) | record index (or clip index with flags `0x80000`), tag, flags, f1, f2, f3 | no | as `0x004b1b90` but keyed on the record | no | **none**: a managed record would be played as a plain clip |
| `0x004b2a20` → `0x004b2b50` (6 external + 1 recursive) | state id / tag, tag, weight, flags | yes, first | an existing flag-`0x04` slot matching on record, state or tag; else first free | no: sets flag `0x04` and the caller's weight | graph instance, with **no release** of an existing one |

**`0x004b1b90`, beyond Q13.2.**
- Allocation is "first free slot, or the slot already holding this state id" (unless flags `0x400`), then growth.
  There is never eviction (§6.3).
- **Managed route:** the state's record has bit 31 and not bit 0 (`Additive` wins: an additive record with bit 31 is
  played as a plain clip, `0x004b1d57`). In that case the slot gets flag `0x10` and any previous instance is
  released (`0x004cc590`). A new row comes from `0x004cbd20(graph index = record +0x08 word, controller +0x20)`. The
  row gets `+0x40` = f1 and `+0x38` = controller `+0x44`, controller `+0x24` is incremented, slot weight = 1.0, and
  the call **returns −1 before storing the play flags at `+0x14` or arming `+0x70`**.
  - So the managed path keeps whatever `+0x14`/`+0x70` the slot held: zero and zero for a freshly freed slot, the
    old values for a same-state slot that is reused. That matches Q13.2's ordering but is not stated there.
  - Q13.2's "returns 0 when there is no row" is exact only for a free slot, which the free and grow routines
    zero. A reused same-state slot returns its previous `+0x04` (§5.2).
- **The return value** is slot `+0x04`, the layer handle created by `0x004bce30`, which callers use as an id
  (§6.4, last partition).

**`0x004b21a0` = the cross-fade request by state id.**
1. It replays itself on the child ring.
2. It maps the state id and the optional transition state id to record indices through `0x004b1630` (Q13.2 step 4),
   and returns if the first is −1.
3. It calls `0x004b2280` with the two record indices, the tag, the transition tag, the blend time and the restart
   byte.
4. It then writes the state id into the current slot's `+0x08` (`0x004b222b`; this faults if `0x004b2280` left no
   current slot). If a transition record was given, it labels the first slot holding that record with the
   transition state id.

**`0x004b2280` = the cross-fade engine** (record indices; no child ring).
1. **No-op cases.** Return if the record index is −1 or equals the current slot's record. If the new and current
   records share the same `+0x08` index and the new one is a blend tree (bit 30) or managed (bit 31), only retag
   the current slot (`+0x0c` = tag) and return. Two states that resolve to the same blend tree or graph therefore
   do not restart it.
2. **Choose the slot** as in the table.
   - An evicted slot is overwritten in place without being freed: its flags are OR-ed, not reset, and a graph row
     it held is not released.
   - This is CONFIRMED as code and reachable only on pool exhaustion. A port should free the victim first.
3. **Allocate.** For a managed record, lazily allocate the parameter block (U0 §3.4).
4. **Make it current.** `+0x50` = slot; flags |= `0x01` | `0x02`; `+0x0c` = tag; controller `+0x1a4` bit `0x200` =
   restart byte & 1.
5. **Starting weight.** If the slot held a different record, weight = 0, then `+0x00` = record. If the chosen slot is
   **slot 0** (an otherwise empty controller), weight = 1.0, so the first state snaps in. A slot that already held
   this record keeps its weight, so a state that is fading out fades back in from where it was.
6. **Kind.** `Additive` → `+0x14` |= `0x2000`.
   - Unlike `0x004b1b90`, an additive record still takes the blend-tree or managed route below.
   - Blend tree → flag `0x08` and `+0x68` = row pointer.
   - Otherwise managed → flag `0x10`, `+0x6c` = new graph row, controller `+0x24` += 1.
   - In every case, arm `+0x70` from record bytes `+0x24`/`+0x25`/`+0x27` exactly as Q13.2 describes (or −1).
7. **Outgoing.** `+0x54` = the highest-weight cross-fade member other than the new current slot (`0x004afe50`
   sorted list), or 0.
8. **Retire the previous transition clip.** Every in-use slot with flag `0x20` has its layer faded out over 0.2 s
   (`0x0111f718`) through `0x004c6d90`, unless the pose is in flight.
9. **Transition clip, when given.**
   - It is played with `0x004b1ef0(controller, transition record, transition tag, flags 0x10, 1.0, 1.0, −1.0)`.
     That slot is located by its returned handle (`0x004afa20`, last partition) and given flag `0x20`.
   - **The cross-fade duration `+0x58` then becomes that clip's length minus 0.1 s, floored at 0**
     (`0x004bd8c0` on record `+0x08`, minus `0x012a2d38`).
   - **Without a transition clip, `+0x58` = the blend-seconds argument.**

   So a transition clip replaces the caller's blend time: the cross-fade lasts as long as the transition clip,
   less 0.1 s.

**Differences in caller contract, in one line each.**
- `0x004b1b90` *adds* an animation (an action, overlay or graph state) beside whatever is playing, at its own weight,
  without touching the cross-fade.
- `0x004b21a0` *replaces* the controller's current state by cross-fading to it, optionally through a transition
  clip, for the controller and every child.
- `0x004b2280` is the same by record index, without children; its callers are the human step (`0x0095b6ad`,
  `0x0095f502`, `0x0095f98a`) and `0x008cb330`.
- `0x004b1ef0` adds a plain clip by record or raw clip index (custom and script animations).
- `0x004b2a20` sets the weight of an explicit-weight slot directly, creating it if needed.

### 6.6 The query family (CONFIRMED by listing)

Site counts are raw rel32 call sites. The scoping note's 185/32/31/17 are function counts, which were not
re-derived.

| Entry | Signature | Behaviour |
|---|---|---|
| **`0x004b4ce0`** (339 sites) | (controller, id) → bool in `AL` | `0x004afa20(controller, start 0, id, no out, node 0) ≥ 0`. "Is anything matching this id playing", under the partition of §6.4: a state id matches a slot's `+0x08`, a tag its `+0x0c`, 10,001/10,002 "is there a current/outgoing slot", 7,589…7,602 "is that named clip node live in a graph", and a layer handle its `+0x04`. |
| **`0x004b41b0`** (46 external + 1 recursive + 2 in `0x004b72a0`/`0x004b7320`) | (controller, id, seconds, freeze byte) → nothing | **Stop with fade.** It replays on the child ring, then finds the slot. Plain or blend-tree slot: when the byte is set, the layer's rate is set to 0.0 (`0x004be510`, layer `+0x18`); then, unless the pose is in flight, `0x004c6d90` fades the layer out over the given seconds (`0x004c6c70`; 0 or less removes it at once). Managed slot: the byte does the same on the slot's `+0x04`, then the **slot weight is set to 0** and the reaper frees it. A cross-fade member stopped this way loses its layer, but its slot keeps its cross-fade weight until the cross-fade or the reaper removes it. |
| **`0x004b4320`** (51 sites) | (controller, trigger id, id or −1, occurrence or −1) → bool | **Did an animation trigger fire.** With id ≠ −1 it finds the slot and returns false for a managed slot; otherwise it takes the slot's layer handle. With id = −1 it uses every layer. Then `0x004c7090` (instance in `ECX`, handle in `EAX`): for each layer considered, it walks the clip's trigger list (clip-cache entry `+0x14` count, `+0x18` array of 8-byte entries, `+0` trigger id). For entries equal to the trigger id, it counts occurrences and, when the occurrence matches or is −1, tests bit 1 of that entry's 3-bit group in the layer's 64-bit mask at layer `+0x30`. A trigger id outside [0, `[0x02f5e00c]`) gives false. The human writer's `fire bool` gate (U0 §4.2 row 28) is this query with the trigger id cached at `0x0153bcb0` on any layer. |
| **`0x004b44a0`** (15 external + 4 internal) | (controller, id) → float on the x87 stack | **Duration, in seconds, of what the slot plays.** Managed slot: 0.0. Plain slot: `0x004bd8c0`(record `+0x08`) = clip header word `+0x06` / 30 (the clip-cache entry's `+0x20` header). Blend-tree slot: the weight-sum of the clip durations, Σ weight × duration. It uses the active `Actions` (slot `+0x40` + `0x14`k weights) if any has positive weight, otherwise the `States` (slot `+0x28` + 4k weights). A slot whose record index is out of range dereferences address 8 (`0x004b45df`). |

Two blend-tree input setters complete the API (CONFIRMED by listing):
- `0x004b72a0` (controller, id, value) **snaps** the input:
  - it sets `+0x18` = `+0x1c` = `+0x20` = value clamped to [`Control_input_low`, `Control_input_high`];
  - it marks the slot initialised (`+0x64`) and runs the `Action` start rules (`0x004afee0`);
  - it sets every `State` weight directly to its curve value (§6.9) without ramping.
- `0x004b7320` (controller, id, value) sets the **target** `+0x1c` (clamped). On a slot that has not been
  initialised it snaps instead.
- Both accept only a blend-tree slot (flag `0x08`, not `0x10`).
- **Both replay on the child ring by calling the stop routine `0x004b41b0`**, with (child, id, value, 0), not
  themselves (`0x004b72be`, `0x004b733e`). On a controller with children, setting a blend input therefore fades out
  the same id on every child over "value" seconds. This is CONFIRMED as code. Whether any shipped controller has
  children is U0's OPEN item 4.
- Their callers (`0x0095b7df`, `0x0095b855`, `0x0095b885` in the human step; `0x0095f591`; `0x009e9567`,
  `0x009e9608`, `0x009e96cc`) pass speeds derived from human fields (`+0xa60`, the desired-move vector) divided by
  a constant.
- **The blend-tree input is not a control parameter** (HIGH, from the callers).

### 6.7 The controller update `0x004b3720` (CONFIRMED by full listing)

**Cross-reference (2026-10-07):** this section's phase handling (step 6, below) and §6.8's rate scaling both name
`0x004c7510` and `0x004be510`. §11.7 restates both from an independent read and, at first pass, swapped their
roles; that is corrected in place there, against this section's original framing (which §5.1 item 14 had already
labelled correctly: "phase `0x004c7510`; rate `0x004be510`"). See §11.7 for the disassembly-level evidence.

`thiscall`-like: the controller is the one stack argument. Steps in order:

1. **Instance bump.** If the instance handle `+0x20` is valid, call `0x004c64d0(instance, 0)`.
2. **Cross-fade** (only when at least one cross-fade member exists and `+0x50` is non-null). Let *n* be the number of
   in-use slots with flag `0x02`, sorted by descending weight (`0x004afe50`, an insertion sort that places a new
   slot after equal weights).
   1. **Base rate** r = 1.0 if `+0x58` = 0.0, else **r = (1/30) / `+0x58`**. The frame time is the constant 1/30
      (`0x0132a0ac`), **not** the controller's time step `+0x2c`. A cross-fade therefore lasts `+0x58` seconds of
      30 Hz updates.
   2. **Restart override.** Scan the in-use slots in order for one with flag `0x400`.
      - If found and n > 1: r = **0.0001** while that slot's layer weight (`0x004bdb50`, layer `+0x24`) is below
        1.0, else r = **100.0**. The first such slot decides.
      - If no slot carries `0x400`, clear `+0x1a4` bit `0x40`.
      - Effect: while a `Transition_Action` animation is fading in, the cross-fade is frozen; once that animation
        is at full weight, the cross-fade completes in one step.
   3. **Crowding factor:** if **n ≥ 3, r ← r × (0.5 + 0.2 × (n − 1)²)**. That is ×1.3 for 3 members, ×2.3 for 4 and
      ×3.7 for 5; n counts the current slot. The constant is the double nearest float 0.2 (`0x012a2e30`), and the
      0.5 is `0x012a2dc0`.
   4. `+0x5c` = r.
   5. **Current slot:** w ← min(w + r, 1.0).
   6. **The others give up r between them, lowest weight first.** Remove the current slot from the sorted list,
      leaving *m* slots indexed 0 (heaviest) … m − 1 (lightest). Keep a budget b = r and walk k = m − 1 down to 0:
      - the step is s = b, **except s = b / 2 at k = 1**;
      - if s + 0.001 > w_k, then b ← b − w_k and w_k ← 0;
      - otherwise w_k ← w_k − s and b ← b − s.

      While the others hold enough weight, they lose exactly what the current slot gains, so the group's total
      weight is conserved.

      With one other slot, it takes the whole step. With two, the lighter takes r/2 and the heavier the rest. With
      three or more, the lightest takes all of r if it can.
3. **Per-slot loop** (only when `+0x20` ≠ −1). Controller `+0x24`, `+0x198` and `+0x19c` are reset first. For each
   in-use slot in order (the loop stops at the first free slot):
   1. **Re-trigger.** Call `0x004b2d70` (slot index in `EAX`, controller on the stack) unless the slot is a
      cross-fade member whose `+0x70` deadline is disarmed and which has no `+0x78` link (§6.8).
   2. **Flag summary.** Call `0x004b3570` (controller in `EAX`, slot on the stack). It ORs record flags into
      `+0x198`/`+0x19c` (§6.1). For blend trees it uses every `State` with positive weight; for managed slots,
      every live clip node whose layer weight is positive.
   3. **Weighting step**, only when (flag `0x02` and w > 0) or flag `0x04`:
      - **Explicit-weight fade-out.** A slot with flags `0x04` and `0x800` and without `0x40` decays: w ← max(w −
        160 / T, 0). Here T = trunc(B / 30 × 4800), and B is byte `+0x09` of the clip header (the clip's blend-out
        length in 30 Hz frames; HIGH, from this use). B = 0 gives an immediate 0. The 160 is (1/30) × 4800.
      - **Blend tree** (flag `0x08`): `0x004b0080(controller, clip list, slot)` (§6.9).
      - **Managed** (flag `0x10`): nothing here.
      - **Plain clip:**
        - **De-duplicate by clip.** A frame-local list of up to 32 (clip index, weight) pairs is shared with the
          blend-tree and graph steps. A clip already listed gets min(listed + w, 1) and posts that sum; a new one
          is appended with w.
        - **Link scaling** (only where `+0x78` is set, §6.8). If the partner slot is free, w = 0 and the slot is
          skipped. If the partner has flag `0x80`, eff = (1 − partner weight) × eff. Then, if this slot has
          `0x40`, eff = partner weight × eff.
        - **Easing.** If eff ≠ 1.0 and the slot is not an explicit-weight slot, eff ← ease(clamp(eff, 0, 1)) with
          `0x00dad960`(eff, 1.0, 0.5), which with these arguments is **ease(t) = 2t² for t ≤ ½ and 1 − 2(1 − t)²
          for t > ½** (quadratic ease-in/out; CONFIRMED by evaluating the listing). Blend-tree `States` and graph
          clips are **not** eased.
        - **Post.** If |eff| < 0.0001, `+0x04` = −1. Otherwise `+0x04` = `0x004c6590(instance, record +0x08,
          +0x14 | 1, eff, 0.0, 1.0)`, or −1 when instance `+0x6c` bit `0x02` (pose in flight) is set.
          `0x004c6590` finds or creates the clip's layer and raises the layer's posted weight `+0x20` to eff if eff
          is larger. The layer bookkeeping is U4's.
   4. **Graph step.** If the slot has flag `0x10`: `0x004b0b80(controller, clip list, slot)` (§6.10), then `+0x24`
      += 1.
4. **Graph clip collection.** `0x004cc000()`, once, over all live graph rows (§6.10).
5. **Parameter-block release** (CONFIRMED). If `+0x28` is non-null and `+0x24` is 0 (no managed slot was
   processed), the block is returned to the pool (`0x00db5d20` on `0x03424868`) and `+0x28` is set to 0. **[corrects
   U0 §3.4]**
6. **Phase handling.**
   - **Restart at zero.** If `+0x1a4` bit `0x200` and a current slot exist, set the current layer's phase to 0.0
     (`0x004c7510`), then clear bits `0x200` and `0x40`.
   - **Phase sync.** If bit `0x40` is set and a current slot exists, the sync applies only when every in-use slot
     other than the current one that carries play flag `0x4000` or slot flag `0x400` has a layer at weight ≥ 1.0.
     When it applies:
     1. Let D be the blend-out length (`+0x09` / 30 s) of the last such slot's clip, and L = `0x004b44a0`
        (controller, 10,001), the current slot's duration.
     2. Set the current layer's phase (`0x004c7510`) to p. When L > 0, p = `+0x1a0` − D / L, and a negative p is
        wrapped into [0, 1) by subtracting its floor. Otherwise p = `+0x1a0`.

     Nothing in the update clears bit `0x40` after a sync, so the sync repeats each frame while its condition
     holds. **HYPOTHESIS:** the current (locomotion) state is held at the phase from which it should resume when the
     transition action blends out; `"Transition to 50%"` makes that phase 0.5.

### 6.8 Re-trigger `0x004b2d70` and the record-index play `0x004b2b50` (CONFIRMED by listing; consequences HIGH)

**`0x004b2d70`** (slot index in `EAX`, controller on the stack; one caller, the update) **returns at once unless
the slot's record has byte `+0x27` ≠ 0.** Byte `+0x27` is the count of 24-byte sub-action entries at record
`+0x30`. Entry fields: `+0x04` record index, `+0x08` kind (1 or 2), `+0x0c` and `+0x10` cumulative probabilities,
`+0x14` a delay in ms. Byte `+0x26` / 255 is a probability. Bytes `+0x24`/`+0x25` × 180 ms are the [min, max] of the
re-trigger delay. When the byte is non-zero:
- **Sub-action start** (no link yet, probability > 0, and the slot's layer within 1.1 frames of its end):
  1. A random draw (`0x00dab6a0`) is tested against the probability. A second draw picks an entry by its cumulative
     `+0x0c`.
  2. `0x004b2b50` starts the entry's record as an explicit-weight child at weight 0.001.
  3. The child gets flag `0x40`, plus `0x80` for kind 1. The two slots are linked through `+0x78`.
  4. A plain clip is created for the child with layer group flags 0x10 for kind 2, and its rate is scaled by the
     parent's remaining time over the child's duration (`0x004be510`). The child gets flag `0x100`, and the parent's
     `+0x74` deadline is armed from the entry.
- **Deadline re-trigger** (no link and `+0x70` expired, `0x00d9e1e0`):
  1. A draw against the entries' `+0x10` picks one, and `+0x74` is armed.
  2. A cross-fade member starts the entry as a linked child, as above.
  3. Otherwise, for kind 1, the slot's own clip is replaced in place: the layer is stopped (`0x004bcf00`), `+0x00`
     takes the entry's record, and `0x004bce30` starts its clip.
  4. Then `+0x70` is re-armed from bytes `+0x24`/`+0x25` (cross-fade members only), or set to −1.
- **Linked-child weighting** (tail, for a slot that is not itself a child):
  - With child flag `0x100`: the child's weight rises by 160 / T₈ per update until full, while the parent's layer is
    before its blend-out window or `0x200` is pending. Otherwise it falls by 160 / T₉ with flag `0x800`, and the
    link is cut at 0.
  - With child flag `0x04`: the same ramps use the parent's `+0x74` timer against the child's blend-out.

  T₈ and T₉ are clip header bytes `+0x08`/`+0x09` converted to ticks as in §6.7.

**`0x004b2b50`** (controller in `EAX`; stack: record index, tag, weight, play flags; returns the slot pointer or 0):
- A request with weight below 0.001 is refused.
- It reuses an explicit-weight slot holding the same record, or takes the first free slot, growing if needed.
- A weight of exactly 0.0 frees the slot instead.
- It sets the blend-tree or managed kind like `0x004b2280`. **For a managed record it allocates a new graph row
  without releasing an existing one** (`0x004b2c9e`). Re-weighting a live managed explicit-weight slot through
  `0x004b2a20` therefore leaks one of the 128 graph rows per call (CONFIRMED as code; reachability not traced).
- It then writes `+0x00`, the weight, `+0x08` = −1, `+0x14` and the tag, and sets flags `0x01` | `0x04`.

**Shipped consequence (HIGH): the whole re-trigger and linked-child machinery appears to be inert.**
- `spec-tables-animation.md` §3.3 records that the `anim_files.xtbl` loader writes neither record bytes
  `+0x24`…`+0x28` nor `+0x30`…`+0x37`.
- A whole-image scan for byte stores at `+0x25`…`+0x27` found no plausible writer. Its only clustered hits are
  colour code at `0x008472f9` and `0x00c950ba`.
- No shipped animation table carries such data (element census of `anim_files`, `anim_states`, `anim_actions`,
  `anim_set_properties` and `anim_prop_sets`).
- The record array sits in zero-initialised `.data`, so `+0x27` = 0 for every record.
- If so:
  - `0x004b2d70` always returns at once, and `+0x70` is always −1;
  - no `+0x78` link is ever formed, so flags `0x40`/`0x80`/`0x100`/`0x200`/`0x800` and the link scaling of §6.7
    never occur;
  - `0x004b2b50` is reached only from `0x004b2a20`.
- This also bears on Q13.2's "random hold / re-trigger interval" reading (§5.3). A dword or block write through a
  computed record pointer would escape the scan, hence HIGH, not CONFIRMED (§5.1).

### 6.9 The 1-D blend-tree evaluator `0x004b0080` (CONFIRMED by listing)

The arguments are (controller, clip list, slot). It is called from the update only, for flag-`0x08` slots that pass
the weighting gate. The row R is at slot `+0x68` (`spec-tables-animation.md` §7.3 layout).

1. **Input ramp.**
   - On first use (`+0x64` = 0), set `+0x18` = `+0x1c` = `+0x20` = 0 clamped to [`Control_input_low`,
     `Control_input_high`], and mark the slot initialised.
   - Every update, move `+0x20` toward the target `+0x1c` by at most `Ramp_speed` × 1/30, without overshoot.
2. **`Action` start rules.** `0x004afee0` runs, then the `Action` loop.
   - Per `Action` (row `+0x18` count, `+0x1c` array, stride `0x44`), using the start/end/cancel ranges,
     `Start_state_percent`, `End_state_percent`, `Time_variance`, `Cap_ramp_*` and the clip's blend-in/out bytes,
     it fades an action layer in or out at 1/30 s per update and posts it eased with `0x00dad960`.
   - **No shipped row has `Actions` (§6.11)**, so this branch is dead in the base game and is summarised, not
     specified. A port can omit it unless it supports modded tables.
3. **`State` weights.** For each `State` k (row `+0x10` count, `+0x14` array, stride `0x3c`), with x = `+0x20`:
   - **Curve target.** Find the first i with Range[i] ≤ x ≤ Range[i+1], over the entry's `Control_point` count − 1
     segments. The target is Value[i] + (Value[i+1] − Value[i]) × (x − Range[i]) / (Range[i+1] − Range[i]).
     **If no segment contains x the target is 0** (no clamp to the end values); a single control point always gives
     0.
   - **Ramp.** The step is (1/30) / `Blend_time`, or (1/30) / an action-supplied duration when an `Action` set one.
     The weight `+0x28` + 4k moves to the target if within one step, else by one step toward it.
   - **Post.** eff = slot weight × `State` weight. If |eff| < 0.0001 the `State` posts nothing this update.
     - Otherwise the clip is de-duplicated in the frame list (sum, capped at 1), and scaled by (1 − partner weight)
       when a `+0x78` partner has `0x80` (inert, §6.8).
     - It is then posted **without easing** through `0x004c6590`(instance, clip, slot `+0x14` | 1, eff, 0.0, 1.0).
   - **Dominant layer.** The `State` with the largest eff gives its layer handle to slot `+0x04`.
   - **Phase.** With no `Action`-imposed phase, every posted `State` layer is aligned to the shared phase `+0x24`
     after the loop (`0x004c75c0`), and `+0x24` is read back from the dominant layer (`0x004be6e0`). **The `States`
     of a blend tree therefore play phase-locked** (foot-synchronised locomotion blending).

### 6.10 The per-slot graph step revisited; §5.1 items 1, 2, 6 and 8 (CONFIRMED by listing)

`0x004b0b80` does more than the copy that U0 §3.5 describes. In order:
1. **Guards.** Slot, instance row and parameter block must exist.
2. **Freeze or copy** (U0 §3.5).
   - The freeze applies when the slot is not current, is a **cross-fade member**, and the row's `+0x30` is already
     non-zero. Its meaning is now clear: **an outgoing graph state stops receiving parameter updates while it is
     cross-faded out.** It keeps the values it had when it lost "current", and row `+0x48` bit `0x02` records the
     freeze.
   - Otherwise it copies the anim-group tables, the skeleton-set id (cross-fade members only), and dword `+0` of
     the block slot of every alias id ≠ −1.
   - **No upper bound is checked**: an alias id ≥ the registry count reads past the block (`0x004b0c41`).
3. **Time step.** If the row's evaluation counter `+0x44` is non-zero, row `+0x3c` = row `+0x40` × controller `+0x2c`.
4. **Evaluate.** `0x004b8850` initialises a 32-entry frame-local output list (`0x24`-byte entries). Then
   **`0x004cc6c0`** runs twice on the row (`ESI`), with `EDX` = 0 and then 1.
   - **It does nothing while row `+0x30` is 0** (`0x004cc6ef`).
   - Pass 0 clears row `+0x48` bit `0x04`, evaluates every filter object (vtable `+0x04`, row and index), runs
     `0x004cca80`, then calls the graph definition's vtable `+0x10` (pass 0).
   - Pass 1 calls vtable `+0x10` (pass 1).
   - Then, **for every alias whose instance byte `+4` is set, it inverts byte `+0` and clears byte `+4`**.
   - Finally it increments `+0x44` and zeroes `+0x3c`.
5. **Post.** Each output entry is turned into clip layers, scaled by the slot weight: create, re-weight, set rate and
   time (`0x004cca40`, `0x004bce30`/`0x004bce90`, `0x004bdb10`, `0x004bd9b0`, `0x004bda10`). The flags are folded
   into `+0x198`/`+0x19c`, and the highest-weighted layer becomes slot `+0x04`. **If a slot that is not a cross-fade
   member produced no output, its weight is set to 0**, so the reaper frees it: a finished graph state ends itself.

`0x004cc6c0` has exactly these two call sites. `0x004cbd20` (read in full) only allocates, seeds the defaults,
constructs four kinds of 8-byte filter objects (vtables `0x012a29bc`, `0x012a29dc`, `0x012a29fc`, `0x012a2a1c`), sets
`+0x14` = 0 (no node instances) and links the row into the live ring. It calls no evaluator. `0x004cc000` (one
caller, update step 4) gathers clip nodes from rows flagged `0x01` | `0x04`. A fresh row has none, and in the part
read it reads no parameter values.

**Resolutions.**
- **§5.1 item 1: closed, no.** A graph instance is never evaluated before its first per-frame copy.
  - The only evaluation entry runs after the copy, within the same call, and refuses to run until the copy has set
    row `+0x30`.
  - The freeze rule cannot apply before that first copy.
  - So the **`default_value`s seeded at instancing are never observed by the evaluator** for any alias the copy
    covers. Defaults survive only in an alias slot whose id is −1, where nothing is seeded either.
  - U0 §3.5's consequence ("visible only from instancing until the first copy") narrows to "never visible". The
    three non-zero shipped defaults are inert.
  - Caveat: a graph whose controller has a null `+0x30` table is never evaluated at all.
- **§5.1 item 2: answered for the consumer side.** Instance byte `+4` is a **deferred toggle request**. After each
  full evaluation the bool in byte `+0` is inverted and the request cleared. The next copy overwrites the inverted
  value with the block value, unless the slot is frozen. Which graph code sets the request is U3's.
- **§5.1 item 6: closed.** Flag `0x02` is "cross-fade member" (§6.2), and the freeze rule freezes outgoing graph
  states.
- **§5.1 item 8: closed.** The block is freed by the update whenever no managed slot remains (§6.7 step 5), and a
  fresh zeroed block is allocated by the next managed play. Values written while no graph state runs are discarded
  (the setters drop them anyway, U0 §3.2). Values written during one graph state's life do not survive into a later
  graph state if there is a gap of one update with no managed slot. The latching notes of U0 §4.2 hold only within
  such a run.

### 6.11 Validation against callers and shipped data (CONFIRMED by static read)

- **AI think gate** (`spec-ai-runtime.md` §1.4 step 3): ids `0x2be` = 702 and `0x408` = 1,032 are below 7,500, so
  `0x004b4ce0` matches them against slot `+0x08`. They are entries of the compiled-in preset list (`0x011507f8`):
  702 = `stun flinch` and 1,032 = `react sitting shocked`.
- **Tags.** The tags pushed by callers read in this pass (7,500 = `0x1d4c` by `0x004b1ef0`'s script callers;
  `0x1d55`, `0x1d56` and `0x1d8d` in the human step; `0x1da1` cited by `spec-lua-api-behaviour.md` §7.21) all lie in
  [7,500, 7,589), the tag partition.
- **U0 §4.2 row 25** tests "`0x1da9` active" through `0x004b4ce0`. 7,593 is not a state but the **named clip node
  `Transition`** (§6.4): the `turn angle` row zeroes the parameter while a graph's `Transition`-aliased clip node is
  live.
- **Blend-tree data.**
  - A census of the extracted `anim_blend_trees.xtbl` finds 32 `Blend_trees` rows, all with 3 or 4 `States` and
    **none with `Actions`**, and 2–4 `Control_point`s per `State`. The slot limits of §6.2 (5 `State` weights, 2
    `Actions`) and the 5-point `State` record are never exceeded.
  - The human step's tests of `+0x54`'s state id against `0x165`/`0x166` name the preset entries `FreeFalling_AIM`
    and `FreeFalling_Dive_AIM`, which are also shipped blend-tree row names.
  - The row count differs from `spec-tables-animation.md` §7.4's 31 (§5.3).
- **Clip header.** `0x004bd8c0` computes duration = word `+0x06` / 30 s through a ×4,800 / 30 tick conversion. The
  update and the blend-tree evaluator use bytes `+0x08`/`+0x09` as blend-in/blend-out lengths in 30 Hz frames. Both
  are side findings for `spec-anim-format.md` (§5.3).

---

## 7. U2 — The node-graph file format

**Scope.** Unit U2 of `review/scoping-animation.md` §5. It covers:
- the population and grammar of the `state_machine` / `blend_tree` files that `node_graph_files.xtbl` lists;
- the loader `0x004cb510` and every parser it reaches;
- the transition-expression language;
- the `control_filters.xtbl` filter binding and the per-graph alias table;
- the link from graphs to animation records.

What a transition, a blend node or a filter does each frame is U3's. An evaluator was read only where a grammar question depended on it: which of `start` and `conditions` counts, and operator precedence (§7.6, §7.7).

**Method.** Ghidra-free, as U0 and U1.
- **No Ghidra project was needed.** The scoping note expected U2 to need a private project with functions created at the 38 undefined vtable-slot targets. Instead, every slot target was listed directly from its vtable entry by capstone recursive descent (`exe_va.py` byte reads, jump tables followed). That does not depend on function boundaries in any database.
  - No Ghidra project was opened or copied. `tools/gp_animU2` was never created, so there is nothing to delete.
- Call sites and string or table references come from raw rel32 and dword scans of the image.
- Shipped data was extracted fresh by a walker over every archive, nested containers included, built on `scan_meshes` / `vpp_modea` in the way `tools/harnesses/locstr_extract.py` uses them.
  - The zone archives `sr3_city_0/1.vpp_pc` and the sound banks were skipped. No `.czn_pc` was opened.
- A Python re-implementation of the loader and parsers was run over the whole population (§7.12).
- Helper scripts are in the session scratchpad (`scratchpad/u2/`), not in the repository.

**Read in full** (instruction counts from the recursive-descent listings; about 4,460 instructions in total):
- **loader and link passes:** `0x004cb510` (348), `0x004cb200` (269), `0x004cb140`, `0x004cb170`, `0x004cbc30`, `0x004cbc80`, `0x004cbb70`;
- **state machines:** `0x004b9070`, `0x004b90a0` (123), `0x004b91f0` (87), `0x004b8eb0`, `0x004b8fb0`, `0x004b9ae0`, `0x004b9b40` (107), and the clip-field reader `0x004b8930` (237);
- **transitions:** `0x004ba1d0` (245), `0x004ba480`, `0x004ba7e0` (344), the tokenizer `0x004ba590` (195), `0x004ba540`, `0x004bac80`, the condition reader `0x004b8780`, and the operand readers `0x004d0260` (142) and `0x004d0430`;
- **blend trees:** `0x004bb240`, `0x004bb270` (200), `0x004bb510` (104), and the node slots `0x004bb780`, `0x004bb7b0`, `0x004bbaa0`, `0x004bbb80`, `0x004bbbc0`, `0x004bbd10`, `0x004bbd80`, `0x004bbf00`, `0x004bbf40` (126);
- **filters:** `0x004caee0` (179) and the `_info` slots `0x004d1200`…`0x004d19a0`;
- **read only to settle precedence:** the transition test `0x004baea0` (256) and the two expression-node evaluators `0x004bacc0` and `0x004badb0`;
- **helpers:**
  - the name lookups `0x004b84f0` and `0x004bf810`;
  - the hashes `0x00d9e8b0`, `0x00d9e740` and `0x00da7890`;
  - the document accessors `0x00dc4ff0`, `0x00dc5030`, `0x00dc5070`, `0x00dc5150`, `0x00dc5190`, `0x00dc51b0`, `0x00dc5210` and `0x00dc5240`;
- **the graph pass of the animation-table loader,** `0x004aec01`…`0x004aee53` (linear listing).

### 7.1 The file population (CONFIRMED, direct read) **[corrects scoping §1.5: 127 blend trees, not 128]**

- **The list.** `node_graph_files.xtbl` (17,122 bytes in `misc_tables.vpp_pc`, the only copy in any archive) has **249 rows**, with 249 distinct names (case-insensitive).
- **The files.** A walk of all 1,797 `.xtbl` entries in the 30 archives searched found exactly 250 files whose first element under `<root>` is `state_machine` or `blend_tree`:
  - 249 in `misc_tables.vpp_pc`, one per row;
  - a second copy of `free_fall_bt.xtbl` in `patch_compressed.vpp_pc`.

  No graph file exists that the table does not list, and every row has its file.
- **Effective population: 249 files, 122 `state_machine` and 127 `blend_tree`.** The scoping note's "122 / 128" (from the breakdown in `spec-xtbl-format.md` §8) counts the patch copy as a separate blend tree.
- **The patch.** The patch copy of `free_fall_bt` differs from the base copy only in the order of the control points of `Multi Way Blend 1`'s three inputs, and the pairs are the same:
  - base file: ranges −20, 0, 20, −100, 100;
  - patch: −100, −20, 0, 20, 100.

  The fix suggests that the blend evaluator expects ascending ranges (§7.12).
- **File names carry no type.** 103 files ending `_bt` are blend trees and 88 ending `_sm` are state machines. The other 58 have neither suffix: 34 state machines and 24 blend trees. The loader goes by the root element alone.
- **Shape.** Every file is XML text with `<root>` holding exactly one graph element. No element has attributes, no element mixes text with child elements, and no entity escapes occur.

### 7.2 The loader `0x004cb510` and the graph object (CONFIRMED by full listing)

`0x004cb990` calls it after the parameter and filter tables (U0 §1.1). It fetches the manifest entry at `0x03519634` (slot 14; the parameter and filter tables use `0x03519434` and `0x03519534`) through `0x00dc5ac0`. Then it makes four passes.

**Pass 1, names.**
- Each `node_graph_files` → `node_graph_file` → `name` text is copied into a 256-byte stack slot and `.xtbl` is appended. The rows are counted.
- **There is no length check.** A name longer than 250 characters runs into the next slot.
- **There is no row-count check either.** The slots start at frame `+0xc24` with stride `0x100`, and the stack cookie at `+0x18c14` lies inside slot 383. So rows 0…382 fit, and a 384th row overwrites the cookie.
- A 384-dword array at frame `+0x24` records the row of each accepted graph.

**Pass 2, headers.** The loader allocates `count` pointers at `0x03171bdc` and clears the count word `0x03171bd8`. For each row, in order:
1. **Open** `<name>.xtbl` through `0x00dc5ac0` (only the name is passed; the directory comes from manifest slot 15 per `spec-tables-traffic-ai.md` §22, HIGH).
   - **If a file cannot be opened, the loader returns at once.** No later file is loaded, and passes 3 and 4 never run for any graph. Every graph accepted so far is left with unresolved links and no alias table.
2. **Choose the root.** Find `state_machine` under the document root, or else `blend_tree`, and read its `validated` with the bool reader (§7.3; a missing element is false).
   - **A graph that is not validated is skipped silently.** It gets no index, and references to it by name fail (§7.10).
3. **Build the object.** Allocate a `0x30`-byte graph object, vtable `state_machine_info` (`0x012a2a44`) or `blend_tree_info` (`0x012a2a60`).
   - Set `+0x0c` to the CRC-32 of the file name including `.xtbl`, computed by `0x00d9e8b0` (table `0x01320da0`, each byte lower-cased first, seed 0, no final inversion).
   - Call vtable `+0x04` (initialise) and `+0x08` (parse pass 1) with the graph element.
   - Append the object to the array. **Its position there is the graph index** used everywhere else.

**Pass 3, links.** The loader reopens each accepted graph's file and calls vtable `+0x0c` with the **document root**, not the graph element. A failed reopen also aborts.

**Pass 4, aliases.** For each graph:
- `0x004cb200` collects the parameter ids and filter ids the graph needs (§7.10) into two 256-entry word buffers on the stack. There is no bound check.
- The loader then allocates `+0x20` and `+0x28` and copies the buffers there.

**The `0x30`-byte graph object:**

| Offset | Content |
|---|---|
| `+0x00` | vtable |
| `+0x04` | 2 (object class "graph"; nodes carry 3 and transitions 4, §7.10) |
| `+0x08` | 1 for a state machine, 2 for a blend tree |
| `+0x0c` | file-name CRC (above); no reader of it was found in the code read for this unit (§5.1) |
| `+0x10` | private copy of `name` (`none` when absent) |
| `+0x14` / `+0x18` | node count (word) / node pointer array, in document order |
| `+0x1c` / `+0x20` | alias count (word) / alias ids (words): U0 §2.2's alias table |
| `+0x24` / `+0x28` | filter count (word) / filter ids (words) |
| `+0x2c` | state machine: `default_node` index; blend tree: `output_node` index (initialised to −1) |

**Graphs are animation records (CONFIRMED by listing of `0x004aec01`…`0x004aee53`) [completes scoping §1.5's last bullet].** After the graphs are loaded, the animation-table loader does this for every graph index i:
1. Take the CRC-32 (`0x00d9e740`: the same table, lower-cased, seed 0) of the graph's **`name`** (`+0x10`), not of its file name.
2. Look it up in the loaded-animation hashtable. `anim_files.xtbl` fills that table, keyed the same way by `Filename` (`spec-tables-animation.md` §3.3).
3. If no record has that key, create a new `0x44`-byte record (up to 6,500, `0x1964`).
   - This is HIGH: the creation path was read, but its calls into the clip cache (`0x004bec10`, `0x004ca4a0`) were not.
4. Set record `+0x2c` bit `0x80000000` and record `+0x08` = i.

**So a graph is an animation whose name is the graph's `name`.** Anything that plays an animation of that name plays the graph through U1's managed route: an `anim_files.xtbl` row, or a satellite `Skeleton_Set` row's `Animation/Filename` (§7.13).

### 7.3 Lexical rules shared by every parser (CONFIRMED by listing of the accessors)

- **Element lookup.** `0x00dc4ff0` (first child with a name), `0x00dc5030` (next sibling with a name), `0x00dc5070` (n-th child with a name) and `0x00dc5150` (count by name) all compare element names **case-insensitively** (`0x00eaa178`). Where the grammar expects a single element, the **first** match is used and duplicates are ignored.
- **Text.** `0x00dc5190` returns the element's text, or the caller's default when the element or its text is missing. The defaults used are `none` for names, types and references, and the empty string for optional names.
- **Floats.** `0x00dc5210` converts the text with the C runtime's string-to-double routine `0x00ea801f` (HIGH: the routine was not read). A missing element gives the caller's default, 0.0 everywhere in this unit.
- **Integers.** `0x00dc5240` converts with base argument 0 through `0x00ea7ea9` (HIGH: so a leading `0x` or `0` would select hex or octal). A missing element gives the caller's default.
- **Booleans.** `0x00dc51b0` reads `yes`, `true` or `1` (case-insensitive) as true and any other text as false. A missing element gives the caller's default. Two fields use their own rule instead: `loop_flag` (§7.4) and `sync_controller_transform` (§7.6).
- **Names.** Every reference by name (node, graph, parameter, filter, alias and State/Action names) is case-insensitive, and the first match wins.
- **Storage.** Every string kept by a definition is a private heap copy, allocated through vtable `+0x38` of `[0x03171bf4]`.

### 7.4 Graph headers and state nodes (CONFIRMED by listing of `0x004b90a0`, `0x004b91f0`, `0x004b9ae0`, `0x004b9b40`, `0x004b8930`)

**`state_machine`:**

| Element | Content | Stored at / use |
|---|---|---|
| `validated` | bool | loader gate (§7.2) |
| `name` | text | `+0x10`. It is the graph's identity for `child_node_graph` (§7.10) and for animation records (§7.2). |
| `default_node` | node name | `+0x2c`, resolved in pass 1 by `0x004b8eb0`: −1 if no node has that name, 0 if the graph has no nodes |
| `nodes` | one or more `node` | `+0x14`/`+0x18` |

**`blend_tree`** is the same, except:
- `output_node` (a node name, resolved in pass 3) replaces `default_node` at `+0x2c`;
- its `node`s follow §7.8.

Every shipped file has exactly these four elements, once each, and `validated` is `true` in all 249. A state machine has 1–18 nodes and a blend tree 1–39.

**Two passes per graph.**
- Pass 1 (`0x004b90a0` / `0x004bb270`) reads `name`, then creates and parses every node. For a state machine it also resolves `default_node`.
- Pass 3 (`0x004b91f0` / `0x004bb510`) walks the nodes again by index and resolves everything that names another node or graph:
  - in a state machine: `child_node_graph` and each transition's `to_node` and expression;
  - in a blend tree: `child_node_graph`, each `input`'s name and `output_node`.

So nodes may name nodes that come later, and graphs may name graphs that come later in the list.

**State nodes.** Each `node` of a `state_machine` becomes a `0x4c`-byte `state_node_info` (vtable `0x012a26f4`; `+0x04` = 3, `+0x08` = 1). It has no `type` element.

| Element | Content | Default | Stored at |
|---|---|---|---|
| `name` | text | `none` | `+0x0c` |
| `tag_name` | State/Action name | empty: not set | clip block `+0x00` (text) and `+0x04` (registry index through `0x004bf810`; −1 if not found) |
| `clip_name` | text | empty: not set | clip block `+0x08` |
| `alias_name` | one of the 14 clip-node alias names (U1 §6.4) | empty: not set | clip block `+0x10` (text), `+0x14` (CRC), `+0x18` (alias id 7,589 + k through `0x004b84f0`, or −1) |
| `match_name` | text | empty: not set | clip block `+0x1c` (text), `+0x20` (CRC; the engine's "no name" hash `[0x029c9964]` when absent) |
| `loop_flag` | text | `false` | clip block `+0x2c` bit 0 is set **unless the text is `true`** (case-insensitive) |
| `start_time` | float | 0.0 | clip block `+0x24` |
| `layer` | integer | 4 when absent | clip block `+0x28` (a present but non-numeric value gives 0) |
| `child_node_graph` | graph `name` | — | `+0x10`, graph index (pass 3; −1 when absent or not found) |
| `transitions` | zero or more `transition` (§7.5) | — | count `+0x44` (word), array `+0x48` (`0x38` bytes each) |

**The clip block.**
- It is the `0x30`-byte record that `0x004b8930` fills: at node `+0x14` in a state node, and at node `+0x28` in a blend-tree `clip` node (§7.8).
- It is what U1 §6.4's clip-node search compares. `0x004cbcb0` returns node `+0x14` for a state node and node `+0x28` for a clip node; U1's offsets there are counted from node `+0x04`, which is consistent with these.
- Block `+0x0c` is initialised to −1 and written by no parser (HIGH: it is filled at run time, U3).

Notes:
- **The `loop_flag` bit means "do not loop".** An empty `loop_flag` element (8 shipped nodes) and a missing one both give "do not loop".
- **The parser does not check what a state node is.** A node may name a clip (`tag_name` or `clip_name`) and a nested graph (`child_node_graph`) at once.
  - Shipped: 236 clip states, 128 nested-graph states and 60 states with neither, such as the `Starting Point` entry states. No node has both.
  - Of the 128 nested graphs, 98 are blend trees and 30 state machines.

### 7.5 Transitions (CONFIRMED by listing of `0x004b9b40`, `0x004ba1d0`, `0x004ba480`, `0x004ba7e0`, `0x004baea0`)

Each `transition` is a `0x38`-byte record in its node's array. The node parser sets `+0x00` = 4 (the object class "transition", which the alias collector tests, §7.10).

| Element | Content | Default | Stored at |
|---|---|---|---|
| `name` | text | `none` | `+0x04` |
| `to_node` | node name | — | `+0x08` (word; pass 3; −1 if not found) |
| `length` | float (seconds, HIGH) | 0.0 | `+0x0c` |
| `cancelable_after` | float | −1.0 when absent | `+0x10` |
| `cancelable_before` | float | −1.0 when absent | `+0x14` |
| `cyclic_percent` | float | −1.0 when absent | `+0x18` |
| `start` | expression text (§7.6) | empty: no expression | expression holder `+0x1c` |
| `parameter` | operand (§7.7); zero or more; **read only when `start` is non-empty** | — | holder count and array |
| `conditions` | zero or more `condition` (§7.7) | — | count `+0x2c` (word), array `+0x30` (`0x38` bytes each) |
| `or` | bool | false | byte `+0x34` |
| `sync_controller_transform` | **presence** | absent: 0 | byte `+0x35` = 1 if the element exists, whatever its text |

The expression holder occupies `+0x1c`…`+0x2b`:
- `+0x1c` the converted text;
- `+0x20` the root of the expression tree;
- `+0x24` the `parameter` count;
- `+0x28` the `parameter` array (`0x18` bytes each).

**Which condition form counts (CONFIRMED by listing of `0x004baea0`).**
- If the transition has an expression tree (a non-empty `start`), **only the tree is evaluated, and `conditions` is ignored.**
- Otherwise the `conditions` are evaluated in order and combined with AND, or with OR when `or` is true.
- With no conditions at all, the AND form is true and the OR form false.
- The rest of `0x004baea0` (timing against `length`, the `cancelable_*` windows and clip time) is U3's.

**Shipped:** 530 transitions in 97 state machines.
- All 530 have `length`.
- 304 use `start` (in 61 files) and 171 use `conditions` (in 38 files).
- 7 have both, so their conditions never run: 5 in `climb_fence_sm` and 2 in `crouch_aim_movement`.
- 62 have neither, so they are unconditional.
- `sync_controller_transform` appears on 226 transitions, always with the text `true`, and `or` on 8, always `true`.
- `cancelable_after` appears on 7, `cancelable_before` on 5 and `cyclic_percent` on 6.
- A node has 1–7 transitions; 94 state nodes have none.

### 7.6 The `start` expression language (CONFIRMED by listing of `0x004ba1d0`, `0x004ba590`, `0x004ba7e0`, `0x004bac80`, `0x004bacc0`, `0x004badb0`)

**Character fix-up.** Pass 1 copies `start` and replaces every `[` with `<` and every `]` with `>` in place. XML text cannot hold a raw `<`, so the files write `<`, `>`, `<=` and `>=` as `[`, `]`, `[=` and `]=`. Every comparison in the shipped expressions is written that way.

**Tokens** (`0x004ba590`, given a position, the text and its length):
1. Spaces are skipped. A `(` or a `)` is a token on its own. Otherwise the characters up to the next space, `(` or `)` form one token.
2. The token is compared case-insensitively with the operators. Their codes are `(` 0, `)` 1, `==` 2, `!=` 3, `>` 4, `<` 5, `>=` 6, `<=` 7, `and` 8, `or` 9, `xor` 10 (strings at `0x012a2644`…`0x012a2668`, and `or` at `0x012a261c`).
3. Otherwise, a token whose first character is a decimal digit (`isdigit`) is a **number** token (code 12).
4. Otherwise it is a **name** token (code 11). The tokenizer then looks one token ahead, and while the next token is also a name it appends it after a single space.
   - So a multi-word parameter name such as `Jump Start Land Bool` is one token, as long as none of its words is an operator word, starts with a digit, or contains a parenthesis.
   - A negative literal such as `-0.785` does not start with a digit, so it is a name token. It still works, because operands are matched by their text (below).
   - A name followed by a number is not merged: `Climb Duration ms ]= 750` gives the name `Climb Duration ms`.

**Tree** (`0x004ba7e0`). There are two node classes:
- `condition_group` (vtable `0x012a273c`, `0x1c` bytes) for a parenthesised group;
- `condition` (vtable `0x012a2730`, `0x28` bytes) for a comparison.

Their fields:

| Offset | Content |
|---|---|
| `+0x04` | class: 1 group, 0 comparison |
| `+0x08` | connective to the next sibling: −1 none, 0 `and`, 1 `or` |
| `+0x0c` / `+0x10` / `+0x14` | parent / previous sibling / next sibling |
| `+0x18` | first child (groups) |
| `+0x1c` / `+0x20` / `+0x24` | comparison only: first operand index, comparison code, second operand index (−1 = none) |

Tokens are consumed left to right. The builder keeps a "current node" (none at first) and the previous token's code:
- **`(`** makes a new group, which becomes current. It is the root if there is no current node, the first child of the current node if the previous token was `(`, and otherwise the next sibling of the current node.
- **`)`** moves up to the current node's parent. If the current node has none, the builder walks back through previous siblings to the first that has a parent and takes that parent. If no node in the chain has one, it stays at the chain's first node.
- **A comparison operator** stores its code on the current node: `==` → 1, `!=` → 2, `>` → 3, `>=` → 4, `<` → 5, `<=` → 6. This is the numbering of the `comparison` words (§7.7).
- **`and` / `or`** set the current node's connective to 0 / 1. **`xor` is recognised but sets nothing**, so the connective keeps its previous value.
- **A name or number** is an operand. Its index is found by comparing the token with each `parameter`'s `source_name` (case-insensitive, first match, `0x004bac80`); no match gives −1. Then:
  - with no current node, a new comparison becomes the root and takes it as its first operand;
  - if the current node is a group, a new comparison is created (the first child if the previous token was `(`, else the next sibling) and becomes current;
  - a comparison takes the operand as its first operand if that is −1, else as its second if that is −1, else a new comparison is created as its next sibling and takes it.

Consequences a re-implementation must keep:
- **There is no operator precedence.** The evaluators fold each sibling chain strictly left to right:
  - each node's value is combined with the running value through the connective of the node before it;
  - the connective is `and`, `or`, or −1 (for the first node of a chain, or after an `xor`), and −1 means "replace";
  - a group's value is its child chain, folded the same way.

  So `a or b and c` means `(a or b) and c`.
- **An operand that matches no `parameter` is silently dropped** (index −1), and the next operand takes its slot.
- **A comparison operator directly after `)` writes the code past the end of a group**, at `+0x20` of a `0x1c`-byte object. Shipped data never does this (§7.12).
- **Unbalanced parentheses are accepted.**
- **If two parameters have the same `source_name`, the first is used.**

**The `parameter` list is the operand table.**
- It has one entry per distinct operand text, in an order the editor chose; the order does not follow the expression.
- Each entry is an operand record (§7.7). Literals are entries too: `true` and `false` are `boolean` operands, `0.5` is a `float`, `750` an `integer`.
- The entries are read in pass 3, and only when `start` is non-empty.

**Shipped (304 expressions).**
- Tokens: 843 names and 85 numbers.
- Comparisons: `==` 260, `<=` 109, `>` 40, `<` 35, `>=` 20.
- Connectives: `and` 126, `or` 34. Parentheses: 89 `(` and 83 `)`.
- **`!=` and `xor` never occur.**
- 209 expressions are a single comparison, and the largest has 7. 33 use parentheses.

### 7.7 Operands and `conditions` (CONFIRMED by listing of `0x004b8780`, `0x004d0260`, `0x004d0430`)

**The operand record** (`0x18` bytes) is used by:
- transition `parameter`s;
- the `parameter1` / `parameter2` of a `condition`;
- the `parameter` of `switch` and `multi way blender` nodes.

| Element | Content |
|---|---|
| `type` | one of the 14 names in the table at `0x01350394` (below); unknown text leaves 0 (`none`) |
| `source_name` | text whose meaning depends on the type. **A missing `source_name` crashes the reader**: the copy at `0x004d032b` reads through a null pointer. |

Layout:

| Offset | Content |
|---|---|
| `+0x00` | type code |
| `+0x04` | `source_name` copy |
| `+0x08` | word id: parameter, filter or State/Action |
| `+0x0c` | trigger index (−1) |
| `+0x10` | literal value |
| `+0x14` | bit 0: "the node's own clip" |

| Code | `type` text | `source_name` | Resolved to |
|---|---|---|---|
| 0 | `none` | — | nothing |
| 1 | `boolean` | `true` / `false` (case-insensitive) | byte `+0x10` = 1 / 0; other text leaves 0 |
| 2 | `integer` | number | `+0x10` (`0x00ea47e1`, HIGH: `atoi`) |
| 3 | `float` | number | `+0x10` (`0x00ea5d9b`, HIGH: `atof`) |
| 4 | `control parameter` | registry name | `+0x08` = global id (`0x004cbaf0`, U0 §2.1), −1 if none |
| 5 | `control filter` | name of a `control_filters.xtbl` row | `+0x08` = filter id (`0x004cbb70`), −1 if none |
| 6 | `local control parameter` | (text not used) | nothing at load; the condition's own `control_parameter` (below) |
| 7 | `animation group` | — | nothing at load |
| 8 | `clip time` | `?` or a State/Action name | `?` sets `+0x14` bit 0; otherwise `+0x08` = registry index (`0x004bf810`) |
| 9 | `clip time remaining` | as 8, **after dropping the last character** | as 8 |
| 10 | `clip time as percent` | as 9 | as 8 |
| 11 | `transition length` | — | nothing |
| 12 | `trigger` | trigger name | `+0x0c` = index in the trigger table `0x03132b18` (count `[0x02f5e00c]`), matched by the case-insensitive rotate-and-xor hash `0x00da7890`; −1 if none |
| 13 | `node active time` | — | nothing |

- Types 1–5 are resolved by `0x004d0430` (pass 3 for transitions and conditions, at once for nodes), and types 8–12 by the first reader `0x004d0260`.
- **The dropped last character of types 9 and 10 is a unit mark in the shipped files.**
  - All 90 `clip time remaining` operands are `?@`.
  - The 29 `clip time as percent` operands are `?%` (10) or a State/Action name followed by `%` (19).
  - The single `clip time` operand is a bare `?`.

**`conditions`** is a list of `condition` (`0x38` bytes each):

| Element | Content | Stored at |
|---|---|---|
| `control_parameter` | optional private parameter definition (`name`, `data_type`, `default_value`, `min_value`, `max_value`), read by U0's row reader `0x004d00e0` into a private `0x14`-byte record (U0 §1.2) | `+0x00` pointer (null if absent) |
| `comparison` | `equal`, `not equal`, `greater`, `greater or equal`, `less` or `less or equal` (table `0x013503e4`, case-insensitive); anything else gives 0 (none) | `+0x34`, code 1…6 |
| `parameter1`, `parameter2` | operand records | `+0x04`, `+0x1c` |

**Shipped: 249 conditions in 38 files.**
- **243 compare with a private constant.** Each has a `control_parameter` named `local` (138 float, 97 bool, 8 integer), and its `parameter2` is a `local control parameter` operand with `source_name` `local`. Presumably the value compared is the private record's `default_value` (HYPOTHESIS: the read is U3's, §5.1).
- **6 compare two registry parameters:** their `parameter2` is a `control parameter` operand (`Vehicle Tilt Abs`, `Vehicle Vert Lean Abs` or `Vehicle Forward Lean Abs`).
- `parameter1` types: `control parameter` 180, `control filter` 32, `clip time as percent` 29, `trigger` 7, `clip time` 1.
- Comparisons: `equal` 105, `greater` 62, `greater or equal` 37, `less` 22, `less or equal` 22, `not equal` 1.

### 7.8 Blend-tree nodes (CONFIRMED by listing of `0x004bb270`, `0x004bb510`, `0x004bb7b0` and the per-kind slots)

Every `node` of a `blend_tree` has `name` and `type`. `type` is matched case-insensitively against the table at `0x013506e0`, and the index selects the class and size:

| Code | `type` text | Class (vtable) | Size | Elements besides `name` and `type` |
|---|---|---|---|---|
| 0 | anything else | — | — | **no object is created, and the parser then calls through an unset array slot (a crash)** |
| 1 | `merge` | `merge_node_info` (`0x012a2748`) | `0x28` | `inputs` |
| 2 | `switch` | `switch_node_info` (`0x012a2760`) | `0x40` | `parameter`, `inputs` |
| 3 | `clip` | `clip_node_info` (`0x012a2778`) | `0x58` | the clip fields of §7.4 |
| 4 | `multi way blender` | `multi_way_blender_node_info` (`0x012a2790`) | `0x44` | `parameter`, `inputs` with weights |
| 5 | `blend tree reference` | `blend_node_info` (`0x012a270c`) | `0x28` | `child_node_graph` |
| 6 | `state machine reference` | `blend_node_info` | `0x28` | `child_node_graph` |

**The common header** is written by `0x004bb7b0`, which every kind's parse slot calls first. Merge's own slot `0x004bbae0` is only a jump to it.

| Offset | Content |
|---|---|
| `+0x04` / `+0x08` | 3 (node) / 2 (blend-tree node; state nodes have 1) |
| `+0x0c` | `name` |
| `+0x10` | child graph index (−1) |
| `+0x14` | type code |
| `+0x18` / `+0x1a` | two per-kind constants set by the initialisers: merge 0/8, switch 2/8, clip 0/0, multi way blender 2/8, references 0/0 (meaning U3's) |
| `+0x1c` | input count: the number of `input` children of `inputs`, for any kind |
| `+0x20` | input node indices (words), filled in pass 3 from each `input`'s `name` |
| `+0x24` | word −1 |

The kinds:
- **`inputs`** is a list of `input`. Each has a `name` naming another node of the same tree, resolved in pass 3 by `0x004b8eb0` (−1 if none). For `merge` and `switch` that is all an `input` holds.
- **`parameter`** (`switch` and `multi way blender`) is an operand record (§7.7) at node `+0x28`, resolved at once by both operand readers.
- **`multi way blender` inputs** are read by `0x004bbf40`. Each `input` gets a `0x30`-byte record in the array at node `+0x40`:
  - `+0x00` `max_blend_rate` (float, 0.0 if absent) and `+0x04` the control-point count;
  - `+0x08` five `range` floats and `+0x1c` five `value` floats. This is the planar layout of the `State` record of `anim_blend_trees.xtbl` (U1 §6.9).

  Each `control_point` holds `range` and `value` (floats, 0.0 if absent). **An input may have at most 5 control points.** A sixth makes the parser return at once: that input and every later input of the node are abandoned, and their records stay unfilled.
- **The two reference kinds** have `child_node_graph`, which names a graph by its `name` (pass 3). Both use the same class and differ only in `+0x14`. The parser does not check that the target's root matches the kind.
- **`clip`** carries the clip block of §7.4 at node `+0x28`; the `0x58`-byte node is the `0x28`-byte header plus the `0x30`-byte block.

### 7.9 Graph references and aliasing (CONFIRMED by listing of `0x004cbc30`, `0x004cb200`, `0x004cb140`, `0x004cb170`)

**Graph references.** Pass 3 resolves `child_node_graph` (on state nodes and on both reference kinds) through `0x004cbc30`:
- it compares the text case-insensitively with each **accepted** graph's `name` (`+0x10`), and the first match wins;
- the result is −1 if nothing matches, and 0 if no graphs are loaded at all;
- the file name plays no part.

**The alias table and the filter list.** `0x004cb200` builds both in pass 4: the alias table at graph `+0x1c`/`+0x20` (U0 §2.2) and the filter list at `+0x24`/`+0x28`. It walks the graph and appends ids without duplicates:
- **from each transition:** every expression `parameter` of type `control parameter` (its id) and every `condition` operand of that type. Operands of type `control filter` add the filter instead.
- **from each `switch` and `multi way blender` node:** its `parameter`, in the same way.
- **from each node with a `child_node_graph`: the whole child graph, recursively** (`0x004cbc80` maps the index to the graph object).
  - So a graph's alias table also covers its nested graphs.
  - A cycle of references would recurse without end (none exists, §7.12), and an out-of-range index dereferences null.
- **for every filter added:** `0x004cb170` follows the filter's source chain. Each filter in the chain goes into the filter list, and the parameter at the end of the chain into the alias table.

Local control parameters (type 6) and literal operands are not aliased.

### 7.10 The filter binding: `control_filters.xtbl` (CONFIRMED by listing of `0x004caee0` and the `_info` slots)

The reader is `0x004caee0`, on the manifest entry `0x03519534`. The element list is in `spec-tables-ui-controls.md` §14.

**Pass 1.** The reader counts the `control_filter` rows into `0x03171bd0` and allocates `0x03171bd4`. For each row it matches `type` against the table at `0x01350374` and builds the `_info` object:

| Code | `type` | Class | Size | Fields after the common part |
|---|---|---|---|---|
| 1 | `multiplier` | `multiplier_filter_info` | `0x1c` | `basis` `+0x14`, `multiplier` `+0x18` |
| 2 | `delta multiplier` | `delta_multiplier_filter_info` | `0x18` | `multiplier` `+0x14` |
| 3 | `linear map` | `linear_map_filter_info` | `0x24` | `input_min` `+0x14`, `input_max` `+0x18`, `output_min` `+0x1c`, `output_max` `+0x20` |
| 4 | `cap` | `cap_filter_info` | `0x1c` | `output_min` `+0x14`, `output_max` `+0x18` |

- **Any other `type` leaves the slot unset, and the call that follows crashes.**
- The common part (`0x004d1220`) holds `+0x04` `name` and `+0x08` the kind code.
- A missing float element gives 0.0. The 1.0 defaults that the initialisers write are overwritten.

**Pass 2.** `0x004d12d0` runs for every row in order and resolves `source_name`:
1. first as a registry parameter: `+0x12` = its id, `+0x10` = 0, `+0x0c` = the parameter's type code;
2. else as a filter: `+0x12` = the filter id, `+0x10` = 1, `+0x0c` = that filter's `+0x0c`, which is still 0 if that filter comes later in the file.

**A `source_name` that is neither dereferences null.** The table's `validated` element is never read.

The filter objects that each graph instance gets (U1 §6.10) are U3's.

### 7.11 Class map (CONFIRMED for the vtables and RTTI, and for the slots read; HIGH for the run-time slots) **[completes scoping §1.5: 23 classes, not 20]**

The `.rdata` range `0x012a26dc`…`0x012a2a60` holds 23 vtables. The scoping census missed three: `condition_object` (`0x012a2724`), `condition` (`0x012a2730`) and `condition_group` (`0x012a273c`), the expression-tree classes of §7.6.

| Family | `+0x00` | `+0x04` | `+0x08` | `+0x0c` | `+0x10` | `+0x14` |
|---|---|---|---|---|---|---|
| graphs (`node_graph_info`, `state_machine_info`, `blend_tree_info`) | destructor `0x004cb970` | initialise (`0x004b9070` / `0x004bb240`) | parse pass 1 (`0x004b90a0` / `0x004bb270`) | parse pass 3 (`0x004b91f0` / `0x004bb510`) | evaluate (`0x004b9300` / `0x004bb660`; U1 §6.10) | `0x004bb6c0`, shared (U3) |
| nodes (seven classes) | destructor `0x004bb4f0` | initialise | parse | run-time (U3) | run-time (U3) | — |
| expression nodes (three) | destructor `0x004ba570` | evaluate (`0x004bacc0` comparison, `0x004badb0` group) | — | — | — | — |
| filter definitions (five `_info` classes) | destructor `0x004cb120` | initialise | parse | resolve the source (`0x004d12d0`) | — | — |
| filter instances (five) | destructor `0x004ccc70` | evaluate (U3) | — | — | — | — |

- **Base classes that no loader instantiates:** `node_graph_info`, `node_graph_node_info`, `condition_object`, `control_filter_info` and `control_filter`. No loader read for this unit creates them (the loaders have no case for kind 0), and their unused slots point at stubs such as `0x00ea2778` and `0x00444690`.
- **Run-time slots, listed but not read:**
  - the node classes' (`0x004ba050`, `0x004bb8a0`, `0x004bba30`, `0x004bbaf0`, `0x004bbb50`, `0x004bbc00`, `0x004bbda0`, `0x004bbe70`, `0x004bc0d0`; the state node's `+0x0c` slot `0x004b8fd0` is an empty return);
  - the graph evaluators and the filter evaluators.

  They take an instance row and node indices, and they belong to U3. Their shared node-instance lookup `0x004cc7b0` has 27 call sites, all in these slots and evaluators or in their helpers `0x004b8b80`, `0x004b9cf0`, `0x004bae50`, `0x004bb1d0` and `0x004cca40`.
- The scoping note listed `0x004cb200` and `0x004cc7b0` among the parsers. `0x004cb200` is the alias collector of §7.9, and `0x004cc7b0` is run-time code.

### 7.12 Full-population validation (CONFIRMED: direct read and re-implementation)

**Method.** `scratchpad/u2/validate.py` re-implements §7.2–§7.10, including the tokenizer and tree builder exactly as listed and the case and first-match rules. It ran over the 249 effective files and checked every reference, and every crash, abort or truncation condition named above, against:
- the shipped `control_parameters.xtbl` (87 rows) and `control_filters.xtbl` (22 rows);
- the State/Action registry: the 1,318 compiled-in presets read from `0x011507f8`, plus the names in `anim_states.xtbl` and `anim_actions.xtbl`;
- `anim_triggers.xtbl`.

**Result: all 249 files parse, and none reaches a crash, abort or truncation path.**
- **Headers.** Every file opens, is `validated` and has a known root, and all 249 graph `name`s are distinct.
- **Node references.** Every `default_node`, `output_node`, `to_node` and `input` name resolves, and no graph has two nodes with the same name.
- **Graph references.** All 155 `child_node_graph` references resolve, every reference kind matches its target's root, and there are no reference cycles.
- **Nodes and operands.**
  - Every blend-tree `type` is known, and no input has more than 5 control points.
  - Every operand has a `source_name` and a known `type`.
  - Every `control parameter` operand resolves; 16 do so only case-insensitively, such as `Supported by Ground`.
  - Every `control filter` operand resolves, every trigger is in `anim_triggers.xtbl`, and every clip-time name resolves once its unit mark is dropped.
- **Expressions.** Every `start` token resolves to a `parameter` entry, and every `parameter` is used. Every comparison node gets two operands and a code, and no comparison follows `)`.
- **Alias tables are small.** The largest holds 10 parameters (`driving_sm`, nested graphs included) and the largest filter list 2, against 256-entry buffers.
- **Filters.** All 22 rows have a known `type` and a source that resolves, and none sources a later row.

**Data defects that load without error but change behaviour:**
- **9 `tag_name`s resolve to no State/Action** (registry index −1):
  - `cower_test`: 2, `cower run ` and `cower stumble `, each with a trailing space;
  - `spinslower_sm`: 1, `SpinSlower`, which is an animation file name, not a state;
  - `tiger_ride_sm`: 6, `tiger calm`, `tiger stumble left` and `tiger stumble right`, whereas the registry has `escort tiger calm` and so on.

  What a clip block with index −1 plays is U3's.
- **4 expressions mix `or` and `and` in one chain where the left-to-right fold differs from the usual precedence.**
  - Two are in `climb_fence_sm`, for example `(climb fall start == true) and ((Supported by Ground == true) or (Jump Start Land Bool == true) and (tag start == true)`.
  - Two are in `jump_sm`: `(Jump Left Ground Bool == true and Supported By Ground == true) or (Jump Start Land Bool == true) and (tag start == …) and desired speed ]= 0.5`.

  The engine reads these as `((A or B) and C) …`. A port must use the fold, not precedence.
- **6 `start` texts in `climb_fence_sm` have one more `(` than `)`.** They are accepted.
- **7 transitions have both forms**, and their `conditions` never run (§7.5).
- **18 multi-way-blend inputs list their control points out of ascending `range` order.** These are angle wrap-arounds, such as 305, 360, 0, 90 in `crouch_walk_blend_forward`; others are in `locomotion_bt`, `riot_aim_walk_bt` and the `fires_*` / `reloads_*` blend trees.
  - The one patched file (§7.1) was patched to put exactly such points in ascending order. That suggests the evaluator assumes ascending order (HYPOTHESIS, U3; §5.1).

**Node-kind census:**

| Kind | Nodes | Files |
|---|---|---|
| state node (all in state machines) | 424 | 122 |
| … of which nest a graph (`child_node_graph`) | 128 | 64 |
| `clip` | 405 | 120 |
| `multi way blender` | 180 | 93 |
| `merge` | 33 | 32 |
| `state machine reference` | 19 | 17 |
| `blend tree reference` | 8 | 5 |
| `switch` | 7 | 7 |

- **Inputs.** Every `merge` has 2. A `switch` has 3 (6 nodes) or 4 (1). A `multi way blender` has 2 (109), 3 (65) or 4 (6).
- **Control points per input:** 1 (1 input), 2 (34), 3 (237), 4 (97), 5 (68).
- **Node parameters.**
  - Every `switch` `parameter` is a `control parameter`: `cower type` 6, `idle type` 1.
  - The `multi way blender` `parameter`s are 108 `control parameter` and 72 `control filter`.
- **Clip fields**, over all 829 clip blocks (424 state nodes and 405 `clip` nodes):
  - `layer` is 4 (735), 3 (62), 2 (29) or 1 (3);
  - 416 loop and 413 do not;
  - `start_time` is always 0.0;
  - `clip_name` is non-empty in only 5 state nodes;
  - `alias_name` is set on 82 blocks and uses 12 of the 14 names (never `Default` or `Sprint 180`): `Fire` on 48;
  - `match_name` is set on 76: `walk_fire` and `walk_reload` 24 each, `ride_fire` 18, `drive_fire` 10.

**Kinds that the shipped data never uses:**
- **`multiplier` filters.** None of the 22 rows is one, so `multiplier_filter_info` and `multiplier_filter` are dead in the base game. The rows are 9 `cap`, 7 `linear map` and 6 `delta multiplier`, and 13 of the 22 are reachable from some graph.
- **Operand types** `none`, `animation group` and `node active time`.
- **The expression operators** `!=` and `xor`.
- **All other kinds are used:** every node kind, every expression class and every `comparison` word occurs at least once.

### 7.13 Which graphs are reachable, and U0's ten unwritten parameters **[answers §5.1 item 5]**

A graph plays in one of two ways:
- an animation record named by its `name` is played (§7.2);
- or another graph nests it.

**Method.** Three searches:
- every extracted `.xtbl` (1,797 entries) for element text equal to a graph `name`;
- the image for the names as strings;
- the extracted Lua scripts in the scratchpad.

**Results.**
- **139 graphs are nested in another graph** (155 references).
- **79 of the other 110 are named from tables:**
  - `anim_files.xtbl` and the per-family satellite files (`Animation/Filename`, for example `anim_auto.xtbl` and `plym.xtbl`);
  - `vi_ride.xtbl` (`Element/Parameter5`, 231 references);
  - the weapon tables, `camera_free.xtbl`, `aim_assist.xtbl` and `dof_situations.xtbl`.
- **31 graphs are referenced by nothing searched.** Among them are `player_state_machine`, `cower_sm` (and through it six cower blend trees), `idle_bt`, `sprint_sm`, `walk_run_sm`, `sprint_to_stand_sm`, `coop_rider`, `statue_sm`, `searchlight_sm` and `tank_turret_bt`.

**U0's ten parameters.** U0 §4.6 found ten parameters that a graph references but no code writes. Every one of them is referenced only from graphs in that unreferenced set:

| Parameter | Graphs that reference it |
|---|---|
| `locomotion` | `player_state_machine` |
| `cower type` | the six blend trees nested in `cower_sm` |
| `idle type` | `idle_bt` |
| `cower bool` | `cower_sm`, `cower_test` |
| `speed` | `cower_sm` |
| `Left Foot Bool` | `sprint_sm`, `walk_run_sm` |
| `Weapon Equipped` | `sprint_sm`, `sprint_to_stand_sm` |
| `Sprint Holster Bool` | `sprint_sm`, `sprint_to_stand_sm` |
| `Coop Rider Stage Number` | `coop_rider` |
| `is statue broken` | `statue_sm` |

**So no graph that reads any of the ten is instanced through any route searched** (HIGH).
- Zone data (`.czn_pc`, not opened) could still name a few of them. `statue_sm`, `searchlight_sm` and `tank_turret_bt` look like graphs of placed objects.
- A placed object's graph can be written by name through U0 §4.3 (§5.1).

---

## 8. U3 — The node-graph runtime

**Scope.** Unit U3 of `review/scoping-animation.md` §5: what a live graph instance does every frame. It covers:
- the evaluation entry `0x004cc6c0` (U1 §6.10 listed it; this unit reads everything it reaches);
- node instances, the per-slot output list and garbage collection;
- the state-machine step, the transition test and the expression and condition evaluators;
- clip, merge, switch, multi-way-blend and reference nodes;
- the four filter kinds.

What the post step of `0x004b0b80` does with the output list (layers, rates, times) is U1's §6.10 step 5 and U4's. It is cited here only where a field's meaning depends on it.

**Method.** Ghidra-free, as U0–U2.
- **No Ghidra project was needed or opened.** Every slot target was listed by capstone recursive descent from its vtable entry (`exe_va.py` byte reads, jump tables followed), as U2 did. `tools/gp_animU3` was never created, so there is nothing to delete.
- Call sites come from a raw `E8`/`E9` rel32 scan of `.text`.
- A Python re-implementation of the evaluators was run over the 249 shipped graphs and the two control tables, re-using U2's extraction (§8.10).
- Helper scripts are in the session scratchpad (`scratchpad/anim_u3/`), not in the repository.

**Read in full** (instruction counts from the recursive-descent listings; about 4,300 in total):
- **entry and infrastructure:** `0x004cc6c0` (90, re-read), the node-instance lookup `0x004cc7b0` (201), `0x004cca40`, the garbage collector `0x004cca80` (55), the instance clear `0x004ccc00`, the output-list initialiser `0x004b8850` and tagger `0x004b8900`;
- **state machines:** the graph evaluate slot `0x004b9300` (613), the state step `0x004b9cf0` (285), the transition test `0x004baea0` (256), `0x004bae50`, `0x004bb1d0`, the active-list helpers `0x004b9c70` and `0x004b9cb0`, and the state reset slot `0x004ba050` (130);
- **expressions and operands:** `0x004bacc0` (91), `0x004badb0` (67), the operand reader `0x004d04e0` (379), the comparison-type query `0x004d09d0`, the comparator `0x004d0070` with its integer and float halves `0x004cff10` and `0x004cff80`, and the clip-node collectors `0x004b8fe0` and `0x004b8f20`;
- **blend trees:** `0x004bb660`, the shared reset `0x004bb6c0`, the shared input evaluator `0x004bb8a0` (147), and the node slots `0x004bba30`, `0x004bbaf0`, `0x004bbb50`, `0x004bbc00` (108), `0x004bbda0`, `0x004bbe70` and `0x004bc0d0` (265);
- **clips:** `0x004b8b80` (159), with `0x004ccba0`, `0x004bf650`, `0x004bf610` and `0x004bf5e0`;
- **filters:** `0x004d1390` (the shared base), `0x004d14b0`, `0x004d1630`, `0x004d1820` and `0x004d19f0`;
- **small helpers:** `0x004b8e50`, `0x004cbbc0`, `0x004bcdb0`, `0x004bddb0` and `0x00dad830`.

### 8.1 The evaluation entry `0x004cc6c0` and the frame order (CONFIRMED by listing) **[closes U0 §5.1 item 1 from the run-time side]**

`0x004cc6c0` takes the graph-instance row in `ESI`, the pass (0 or 1) in `EDX` and the output list on the stack. The row's graph object is graph `+0x04` of the row, looked up in `0x03171bdc`. Nothing happens while row `+0x30` (the anim-group table, U0 §3.5) is 0.

**Pass 0** (`EDX` = 0):
1. Clear row `+0x48` bit `0x04`.
2. Evaluate every filter instance, in the order of the root graph's filter list (graph `+0x24`/`+0x28`): instance vtable `+0x04` with (row, list index). §8.8.
3. **Garbage-collect node instances** (`0x004cca80`, row in `EDX`). A node instance whose last-evaluated counter is more than one evaluation old (row `+0x44` − instance `+0x0c` > 1, unsigned) is freed. Every other live instance gets `+0x14` = −1.0 and `+0x30` = 0.0. All get bit 0 of `+0x40` cleared. Then row byte `+0x1c` = 0 and floats `+0x20`/`+0x24` = 0.0 (§8.3).
4. Call the graph's evaluate slot (vtable `+0x10`) with (0, row, graph index, output list).

**Pass 1** (`EDX` = 1):
1. Call the same slot with pass 1.
2. Apply the alias toggle requests (U1 §6.10).
3. Increment the evaluation counter `+0x44` and zero the time step `+0x3c`.

**Pass 0 decides, pass 1 emits.** Pass 0 advances clocks, runs transitions and fixes every choice: switch selections, clip records and the current state. Pass 1 walks the same tree again and appends weighted clip entries to the output list. Both passes run back to back for one row (U1 §6.10 step 4), so no other row can run in between.

**Defaults, from the run-time side.**
- `0x004cc6c0` has exactly two call sites, `0x004b0c9e` and `0x004b0cad`. Both are in `0x004b0b80`, after its per-frame copy.
- Inside the tree, a registry parameter's value is read in exactly two places, and both read the instance slot at row `+0x0c` + 8 × alias:
  - the operand reader for `control parameter` operands (`0x004d055e`);
  - the filter slots for a parameter source (§8.8).
- No evaluator reads registry `+0x08`. The only registry access in the tree is the type query `0x004d09f2` → `0x004cbb40` → `+0x04`.

So U1's finding stands from both sides: **the registry `default_value`s seeded at instancing are never seen by any evaluator.** The `default_value` that *is* used is a different one: the private `control_parameter` of a `condition`, read as the constant for a `local control parameter` operand (§8.5).

**The toggle requests (U0 §5.1 item 2).** No code in the library `0x004af000`…`0x004d2000` sets byte `+4` of an instance slot. The only stores are the clear at instancing (`0x004cbdd2`) and the clear in `0x004cc6c0` (`0x004cc785`). A heuristic whole-`.text` scan for byte stores at `+4` found no plausible writer elsewhere. The toggle machinery is therefore inert (HIGH).

### 8.2 Node instances, the output list and resets (CONFIRMED by listing)

**Node instances.** Every piece of per-instance state lives in `0x5c`-byte node instances in the array at row `+0x18` (count word `+0x14`; U1 §6.4).
- `0x004cc7b0` (row in `ESI`; stack: three key words and a create byte) finds the instance whose words `+0`/`+2`/`+4` equal the key.
- Keys:
  - (graph, −1, −1) for a state machine's own record;
  - (graph, node, −1) for a node;
  - (graph, node, transition) for a transition.

  Here "graph" is a graph index and "node" a node index within that graph.
- With the create byte, a missing key takes the first free instance (word `+0` = −1), or the array grows by one through the pool `0x03519890`. A new instance has a time of 0.0 and its last-evaluated counter set to the current row `+0x44`.
- `0x004ccc00` frees an instance (key −1, fields reset).

| Offset | Content |
|---|---|
| `+0x00`/`+0x02`/`+0x04` | key words (−1 = free) |
| `+0x08` | **active time**, seconds: advanced by row `+0x3c` once per evaluation, clamped at 0. For a transition, the elapsed time (§8.4). |
| `+0x0c` | last-evaluated counter (row `+0x44` when last advanced) |
| `+0x10` | word: a state machine's **current state** (graph record), or a state node's **transition in progress** (−1 = none) |
| `+0x14` | phase offset to apply this frame (−1.0 = none), from `cyclic_percent` (§8.3) |
| `+0x18`…`+0x27` | a state node's **active-transition list**: 8 words, −1 = empty |
| `+0x28` / `+0x2c` | a clip node's **clip time** and the previous frame's clip time (§8.6) |
| `+0x30` | phase for the clip's layer this frame (0.0 = none) |
| `+0x34` | animation-record index the clip plays (−1 = none) |
| `+0x38` | clip index, written by the post step of `0x004b0b80` when it creates or refreshes the layer |
| `+0x3c` | layer handle (−1 = none) |
| `+0x48`…`+0x5b` | a multi-way blender's **current input weights**, one float per input: **at most 5 inputs** (§8.7) |

**The output list.** It is initialised by `0x004b8850` on the stack of `0x004b0b80`, with capacity word 32, a used-count word, and `0x24`-byte entries:

| Offset | Content |
|---|---|
| `+0x00` | the clip block's `layer` |
| `+0x04` | word: input tag, used by the enclosing multi-way blender (§8.7) |
| `+0x06` / `+0x08` | words: graph and node of the emitting clip |
| `+0x0c` | State/Action registry index of `tag_name` |
| `+0x10` | animation-record index |
| `+0x14` | **weight**, 1.0 when emitted, then scaled by every enclosing blender and transition |
| `+0x18` / `+0x1c` | clip time and previous clip time (non-looping clips only, §8.6) |
| `+0x20` | bit 0 loop; bit 1 "weight became 0 in a transition"; bit 2 "outgoing side of a transition" |

**Hazard: the capacity is never checked.**
- `0x004b8b80` appends at the used count with no test against the 32.
- The list's 32 entries end exactly at the return address of `0x004b0b80`. Its frame is `0x4b0` bytes, the entries start at frame offset `+0x30`, and there is no stack cookie.
- **A 33rd clip in one evaluation overwrites the return address.**
- Shipped data, counted the way §8.10's census counts it (a nested graph at its own steady bound, no transition of its own in flight), peaks at 30. **The format's own worst case is 31, not 30** (§8.10): nothing ties a nested state machine's transition timing to its parent's, so a parent's own in-flight transition and its nested state machine's own in-flight transition can coincide. Shipped data never drives both at once, so the margin against what ships is 2 (32 − 30); the margin against what the format allows is 1 (32 − 31). A port should check, or size the list dynamically.

**Resets.**
- Each node class's vtable `+0x10` slot frees its instance and resets what hangs below it:
  - **state node** `0x004ba050`: resets its nested graph, stops the clip layer (`0x004bcf00`, from instance `+0x3c`), and for every transition with a live instance frees that instance and resets the transition's target node;
  - **clip node** `0x004bbe70`: stops the layer the same way;
  - **reference node** `0x004bba30`: resets its nested graph;
  - **merge, switch and multi-way blender** (`0x004bbb50` → `0x004bba30`): free only themselves. Their inputs are collected by `0x004cca80` once they stop being evaluated.
- A graph's vtable `+0x14` (`0x004bb6c0`, shared by both graph kinds) resets every node and frees the graph's own record.

### 8.3 State machines: `0x004b9300` and the state step `0x004b9cf0` (CONFIRMED by listing)

**The state step `0x004b9cf0`** (row in `EDI`, pass in `EAX`; stack: node, graph, node index, output list, test byte):
- **Pass 0:**
  1. Find or create the node's instance.
  2. Advance its active time (once per evaluation).
  3. Evaluate the nested graph (pass 0) if the node has one; otherwise the clip block (§8.6), if it names a clip.
  4. **Only when the test byte is set**, run the transitions (below).
- **Pass 1:** emit the nested graph or the clip.

**Pass 0 of the state machine (`0x004b9300`).**
1. Find or create the machine's record (graph, −1, −1) and advance its time. No reader of that time was found (§5.1).
2. If no state is current, the `default_node` becomes current and its instance is created.
3. Run the state step on the current state with the test byte set.
4. **Completion loop.** While the current state's instance has a transition in progress and that transition's elapsed time equals its `length` exactly (the test clamps it there, §8.4):
   - find or create the target's instance;
   - write the transition's `cyclic_percent`, if present, into the target's `+0x14`;
   - free the transition's instance;
   - reset the old state (its slot `+0x10`);
   - make the target current, and run the state step on it with the test byte set.

   **Completions therefore chain within one frame.** A new state's zero-length transitions fire at once.
5. **In-flight targets.** For every transition of the current state that has an instance (in progress or rewinding), the target state is stepped too, with the test byte clear. Its `+0x14` gets the transition's `cyclic_percent`, if any.
   - **A target's clock starts when its transition starts, not when it completes**, so its clip time on arrival includes the transition time.
   - Targets do not test their own transitions while in flight.
6. **Root-motion sync.** If a transition is in progress and has `sync_controller_transform`, the step sets:
   - row byte `+0x1c` = 1;
   - row `+0x24` = the transition's `length`;
   - row `+0x20` = its elapsed time (−1.0 if it has no instance).

   `0x004cca80` clears the three every frame. Their consumer is outside the evaluator (§5.1).

**Pass 1 of the state machine: the cross-fade.**
1. Let S₀ be the output count. Emit the current state and tag its entries 0.
2. If the state's `+0x14` is set, align the phase of each new entry's clip instance:
   - take x = float `+0x60` of the controller's animation instance (`0x004bcdb0` on row `+0x08`);
   - set `+0x30` = x − offset, plus 1.0 if negative.
3. Then, for each transition in the current state's active list, in list order:
   - let p = elapsed / `length` (computed as (1 / `length`) × elapsed);
   - let Sₜ be the output count; emit the target state, tag its entries 1, 2, … and phase-align them as above;
   - multiply **every entry in [S₀, Sₜ)** by 1 − p and set bit 2;
   - multiply every entry from Sₜ on by p, and set bit 1 on any entry whose weight became 0 from a non-zero value.

So a single transition is a linear cross-fade over `length` seconds of graph time. Several active transitions nest: each later one fades out everything emitted before it.

**Zero-length transitions never reach pass 1.** They complete in pass 0 of the frame they start (§8.4) and are never put in the active list, so the 1/`length` above never divides by zero.

### 8.4 The transition test `0x004baea0` (CONFIRMED by listing) **[completes U2 §7.5]**

**Signature.** Row in `EAX`; stack: (transition, 0, graph, node, k, current), where k is the transition's index and "current" the node's transition in progress (or −1). It returns a code.

**Steps.**
1. **Clock.** If transition k has an instance, its elapsed time moves by **+dt when k is in progress and −dt otherwise**, clamped at 0. Here dt is row `+0x3c`.
2. **Condition.** U2 §7.5's rule holds: the expression tree if there is one, otherwise the `conditions` (§8.5).
3. **Start.** When **no transition is in progress, the condition holds, and row `+0x48` bit `0x02` is clear**, the instance is created if missing and the result is code 1.
   - Bit `0x02` is U1's freeze flag. **A graph state being cross-faded out keeps playing but starts no new transitions.**
4. **Completion.** With an instance whose elapsed time is at least `length`, the time is clamped to `length` and the result is code 2. A transition whose `length` is 0 thus returns 2 on the frame it starts.
5. **Rewinding.** For k other than the one in progress, an elapsed time that has reached 0 resets the target state, frees the instance (`0x004bb1d0`) and gives code 4.
6. **Cancelling the transition in progress.** While its condition still holds, the result is 0. Once the condition fails:
   - with neither `cancelable_after` nor `cancelable_before` present, it is 0 and the transition runs to completion;
   - otherwise let a = `cancelable_after` (absent → 0) and b = `cancelable_before` (absent → `length`), and let e be the elapsed time;
   - when b ≥ a the transition is cancelled (code 3) if a ≤ e ≤ b;
   - when b < a the window wraps: it is cancelled if e ≤ b or e ≥ a.

**How the state step uses the codes** (`0x004b9cf0`):
1. **The transition in progress is tested first.** Code 2 returns at once, and the completion loop of §8.3 finishes the transition. Code 3 clears "in progress", but the transition stays in the active list and starts to rewind. Code 4 removes it.
2. **Then every other transition is tested in document order.** While one is in progress, only those already in the active list are tested, and they can only rewind.
   - Code 1 makes k the transition in progress and moves it to the end of the active list.
   - Code 2 makes k the transition in progress and returns, so a zero-length transition completes in the same frame.
   - Code 4 removes k from the list.

Consequences for a port:
- **The first transition in document order whose condition holds wins.** Once one starts, the later ones see a transition in progress and cannot start in that frame.
- Most transitions are **fire-and-forget**: without a cancel window, a started transition completes even if its condition stops holding.
- A cancelled transition **rewinds at the rate it advanced**, cross-fading back. It is restartable from where it stands if its condition holds again while nothing else is in progress.
- **The active list holds 8 transitions.** Shipped nodes have at most 7 (§8.10).

### 8.5 Expressions, conditions and operands (CONFIRMED by listing) **[confirms U2 §7.6's fold in the evaluator]**

**The fold, in the evaluators.**
- Both expression-node slots take (row, graph, node, k, operand table, running value, previous connective).
- A comparison (`0x004bacc0`) evaluates itself, then combines with the running value through the **previous node's** connective:
  - `and` → running AND value;
  - `or` → running OR value;
  - −1 → the value replaces the running value.
- If it has a next sibling, it calls that sibling with the combined value and **its own** connective (`+0x08`), and returns the sibling's result.
- A group (`0x004badb0`) passes its incoming running value and connective to its first child, then combines the child chain's result with them again. That equals combining the group's plain left-to-right value once, so parentheses behave as expected.
- The transition test starts the root with running value false and connective −1.

This is U2's left-to-right fold, read now in the code that runs every frame, with no precedence anywhere. Two hazards:
- An empty group would call through a null child (none is shipped, §8.10).
- The values are full booleans, so a comparison against `xor`'s missing connective simply replaces.

**`conditions`** are evaluated in order with short-circuit:
- the AND form starts true and stops at the first false;
- the OR form starts false and stops at the first true.

**Operand values** (`0x004d04e0`; node index in `ECX`, the condition's private record in `EDX`, 0 for expressions):

| Type | Value |
|---|---|
| `boolean`, `integer`, `float` | the literal dword |
| `control parameter` | dword `+0` of row `+0x0c` + 8 × the parameter's alias in the **root** graph (row `+0x04`) |
| `control filter` | the filter instance's output (`+0x04`), found through the root graph's filter list |
| `local control parameter` | **the private record's `default_value`** (`+0x08`), or 0 when the condition has none. `min_value`/`max_value` are not read. **[answers §5.1 item 21]** |
| `animation group` | row `+0x38`, the skeleton-set id (unused in shipped data) |
| `clip time`, `clip time remaining`, `clip time as percent` | see below |
| `transition length` | transition k's `length` |
| `trigger` | `0x004bddb0`(row `+0x08`, trigger index, −1, −1): the "did this trigger fire on any layer" test of U1 §6.6 (`0x004c7090`) |
| `node active time` | float `+0x28` of the node's instance, with **no null check** (unused) |

**The clip-time family.**
1. Collect the instance keys of the source node and of every node of every graph nested under it (`0x004b8fe0` / `0x004b8f20`, at most 250).
2. Look among the row's live clip instances with those keys.
   - **`?` (the node's own clips):**
     - `clip time` is the clip time of the last match;
     - `clip time remaining` is duration − clip time (last match);
     - `clip time as percent` is the largest clip time / duration among the matches;
     - **with no live clip, time and remaining give 0.0 and percent gives 1.0.**
   - **A State/Action name:** the clip instance must belong to a state node or `clip` node whose `tag_name` index equals the operand's, and must have a layer.
     - Time and remaining take the last match.
     - Percent takes the first match.
     - No match gives 0.0.

Clip time is clamped at the clip's duration (§8.6). After one cycle of a looping clip, percent therefore stays at 1.0 and remaining at 0.0.

**Comparison type.** It comes from operand 1 alone (`0x004d09d0`):

| Operand 1 | Comparison type |
|---|---|
| literals | their own type |
| `control parameter` | the registry type |
| `control filter` | the filter's type `+0x0c`, which is its source parameter's type |
| `local control parameter` | the private record's type |
| `animation group` | integer |
| clip-time family, `transition length` | float |
| `trigger` | bool |
| `node active time` | **0, so it always compares false** |

**Comparator** (`0x004d0070`):
- **bool:** low byte only, and only `==`/`!=`; any ordering comparison of bools is false;
- **integer:** signed;
- **float:** compared as doubles; `==` is exact.

Operand 2 is read as raw bits of whatever type it has. Shipped data never mixes types (§8.10).

### 8.6 Clip blocks: `0x004b8b80` (CONFIRMED by listing) **[answers §5.1 items 20 and 23; corrects U2 §7.4's `loop_flag` bit]**

The clip evaluator serves both state nodes and `clip` nodes. Row in `EAX`; stack: (clip block, pass, graph, node, output list).

**Which animation plays.** The `tag_name` mapping is tried first, then `clip_name`:
1. **`clip_name` is resolved lazily.** On first use, if clip block `+0x0c` is −1 and `clip_name` is set, the name is looked up through `0x004bf650`. That function takes the CRC-32 of the name (lower-cased, seed 0) and looks it up in the loaded-animation hashtable `0x0340b220`, the same key `anim_files.xtbl` uses. The record index is cached in block `+0x0c`. **That is block `+0x0c`: a per-definition cache shared by every instance.**
2. If neither the `tag_name` index (block `+0x04`) nor `+0x0c` is set, **the block emits nothing in either pass.**
3. Pass 0 maps the `tag_name` index to a record through `0x004ccba0`. That function asks the row's two anim-group tables (row `+0x30`, `+0x34`, with the skeleton-set id `+0x38`) through `0x004ccf30`, and the first table that answers wins. This is how one graph plays different clips on different characters.
4. When that gives −1, the cached `clip_name` record is used. The result goes to instance `+0x34`. With −1 nothing is emitted.

**Clip time.**
- In pass 0, previous time ← clip time, then clip time ← the node's active time, **clamped to the clip's duration** (`0x004bd8c0` on the record's clip).
- A clip's time therefore starts at 0 when its node instance is created: when the state is entered, or when a transition to it starts. It then holds at the end.

**Emission** (pass 1) appends one entry:
- layer from `layer`, the record, the `tag_name` index, weight 1.0, the loop bit, and the graph and node;
- **for a non-looping clip, the entry also carries the clip time and the previous clip time** (`+0x18`/`+0x1c`).

The post step then:
- sets a non-looping clip's layer time explicitly from the graph, so it plays once and holds its last frame;
- lets a looping clip's layer run on its own clock, and applies only the phase from instance `+0x30` (U1 §6.10, `0x004b0f91`…`0x004b1006`).

**`loop_flag` polarity [corrects U2 §7.4].**
- The parser `0x004b8930` sets block `+0x2c` bit 0 **when the text equals `true`**: the case-insensitive compare at `0x004b8b26` returns 0 on a match, and the store at `0x004b8b33` is skipped otherwise.
- So bit 0 means **loop**, not "do not loop". U2's counts were taken from the text and are unaffected: 416 looping and 413 not; an empty or missing `loop_flag` does not loop.

**The other clip fields.**
- **`layer`** is carried to entry `+0x00`. The post step maps 1 → layer-group flag `0x40`, 2 → `0x20`, 3 → `0x10`, and anything else (the default 4) → none (`0x004b0ebf`…`0x004b0eed`, U1's listing).
- **`start_time` has no effect.** It is written into the time fields and immediately overwritten by the clip times; shipped values are all 0.0.
- **`match_name`:** a clip whose `match_name` hash is not the engine's "no name" value sets row `+0x48` bit `0x04` in pass 1. Pass 0 clears it. This is the flag U1 found `0x004cc000` selecting on **[answers the first half of §5.1 item 17]**.

**Unresolved `tag_name`s (U2 §7.12) [answers §5.1 item 23].** A block whose `tag_name` resolved to −1 and has no `clip_name` (all 9 shipped cases) plays nothing. Of the three graphs:
- `cower_test` is unreachable;
- `spinslower_sm` and `tiger_ride_sm` are reachable (§7.13), and **their default states emit nothing**.

By U1 §6.10's post rule, a managed slot that is not a cross-fade member and produces no output is set to weight 0 and reaped. HIGH: the play path that reaches these two graphs was not traced.

### 8.7 Blend-tree nodes (CONFIRMED by listing) **[answers §5.1 item 22]**

**Blend trees.** A blend tree's evaluate slot (`0x004bb660`) runs its `output_node`'s vtable `+0x0c` with (pass, row, graph, node, output list). Every blend node then works through the shared input evaluator `0x004bb8a0`:
- With a nested graph (a reference node), it runs the nested graph's evaluate slot. In pass 1 it tags the new entries 0.
- Otherwise it runs the node's inputs: all of them while the selector word at node `+0x24` is −1, else only that one. In pass 1, each input's new entries are tagged with the input's index.
- A parent's tag overwrites a child's, so an entry's tag is always relative to the nearest enclosing tagger.

**`merge`** (`0x004bbaf0`): **all inputs, unweighted.** A merge is a union: every input's clips go out at the weight they already carry.

**`switch`** (`0x004bbc00`). In pass 0 it sets the selector from its `parameter`:
- a bool gives 1 or 0;
- an integer gives its value;
- any other type gives −1 (all inputs).

Pass 1 uses the same selector. Two quirks:
- **An out-of-range value selects nothing.** An integer of −1 selects everything.
- **The selector is stored in the node definition (`+0x24`), not in the instance**, so all instances of a graph share it. This is harmless only because both passes of one row run back to back (§8.1).

**`multi way blender`** (`0x004bc0d0`). Pass 0 just runs all inputs. Pass 1:
1. Find or create the node's instance, and note whether it is **fresh** (created during this evaluation).
2. Emit all inputs, tagged by index. **Inputs at zero weight are still emitted.**
3. Read the `parameter` value **as a float, whatever its type**.
4. For each input:
   1. **Target.** Use the first segment j with range[j] ≤ x ≤ range[j+1]: value[j] + (value[j+1] − value[j]) × (x − range[j]) / (range[j+1] − range[j]), or value[j] for a zero-width segment. **No containing segment gives 0**, and so does a single control point. This is U1 §6.9's curve rule.
   2. **Ramp.** The weight is at instance `+0x48` + 4 × input. It snaps to the target when the instance is fresh or `max_blend_rate` is 0, and otherwise moves toward it by at most **dt / `max_blend_rate`** per evaluation.
5. Multiply every new entry's weight by the current weight of the input its tag names.

What a port should note:
- **`max_blend_rate` is a duration, not a rate:** the seconds a weight takes to cross 0 → 1. Shipped values run from 0.1 to 1.0 s, and 6 inputs snap.
- **The inputs are not normalised.** Each weight is its own curve value, and the authored curves are responsible for summing to 1.
- **Out-of-order control points (§5.1 item 22).**
  - A segment whose range[j+1] < range[j] can never contain x, so it is dead. Earlier segments win over later ones.
  - **The 12 wrap-around inputs** (305, 360, 0, 90 and similar) therefore work as authored: their two real segments cover both sides of 0°.
  - The other 6 end with a point back at range 0 (for example 0, 90, 225, 0). That last segment is dead, and the replay gives the same weights as for sorted points.
  - **The free-fall defect was genuine.** In the base copy of `free_fall_bt`, the order −20, 0, 20, −100, 100 left one long segment (−100 … 100) for |x| > 20. That blended the opposite turn in (§8.10), and the patch removes it by sorting.

**Reference nodes** (`blend tree reference` and `state machine reference`, `0x004bb8a0` via vtable `+0x0c`) simply run the referenced graph. The node constants `+0x18`/`+0x1a` (U2 §7.8) are read by no slot read for this unit (§5.1).

### 8.8 Filters (CONFIRMED by listing) **[answers U0 §5.1 item 3 for filters]**

**Filter instances.**
- Each filter instance is an 8-byte object: a vtable and an output value at `+0x04`.
- `0x004cbd20` creates one per entry of the root graph's filter list and zeroes the output by its type: byte, dword or 0.0 (`0x004cbf22`…`0x004cbf49`).

**The shared base** (`0x004d1390`, called first by every kind, thiscall with (row, list index)):
- It finds the filter's definition.
- **If its source is another filter, it evaluates that source first, by a recursive call through the source instance's vtable.**

**The input x** is then one of two things:
- the source filter's output;
- or the raw dword of the source parameter's instance slot (row `+0x0c` + 8 × alias in the root graph's table), **read as a float whatever the parameter's type**.

The math (all values float, written to `+0x04`):

| Kind | Output | Notes |
|---|---|---|
| `linear map` (`0x004d1820`) | c = x clamped to [`input_min`, `input_max`]; out = (c − `input_min`) / (`input_max` − `input_min`) × (`output_max` − `output_min`) + `output_min` | If `input_max` < `input_min`, c is always `input_max`. Equal bounds divide by zero (none shipped). |
| `delta multiplier` (`0x004d1630`) | out ← out + `multiplier` × dt × (x − out) | First-order smoothing toward x. `multiplier` is a rate per second, and dt is row `+0x3c`. Stateful: starts at 0 and persists while the row lives. It overshoots if `multiplier` × dt > 1 (none shipped). |
| `cap` (`0x004d19f0`) | x clamped to [`output_min`, `output_max`] | With `output_min` > `output_max` the result is `output_max`. |
| `multiplier` (`0x004d14b0`, dead in shipped data) | `basis` + (x − `basis`) × `multiplier` | |

The field names, kinds and ranges are those U2 §7.10 extracted, and the math uses each field as its name says.

**No code reads the registry's `min_value`/`max_value`.**
- The filter slots clamp only to their own fields.
- The operand reader reads only slot values.
- The registry accessor `0x004cbb40` has one caller, which reads the type.

This answers U0 §5.1 item 3: the ranges are stored and never used (HIGH: a direct access to `0x03171bcc` + `0x0c`/`0x10` outside the functions read would be missed).

**Hazard: a stateful filter that is also a source runs twice per evaluation.**
- Every filter of a chain is in the root graph's filter list (U2 §7.9), and each list entry is evaluated once.
- A `delta multiplier` that is the source of another listed filter is therefore evaluated once in its own right and again inside its consumer's base call.
- In shipped data this is `Turn Angle Delta Multi`, the source of `Turn Angle Cap`, in `free_fall_sm` and `parachute_bt`, both reachable. Its smoothing per frame is 1 − (1 − ½)² = 0.75 instead of 0.5 (§8.10).
- It is the engine's own behaviour and should be kept for fidelity, but it is not what the data says.

### 8.9 What a graph instance produces each frame

1. Pass 0 fixes the state, the switch selections and the clip records, and moves clocks and blend weights.
2. Pass 1 then produces, into a 32-entry list, one entry per clip reachable through:
   - the current state;
   - every in-flight or rewinding transition target;
   - every input of every merge and multi-way blender;
   - the selected input of every switch.
3. Each entry carries the layer group, the animation record, the State/Action index, a weight, the loop bit, and (when not looping) the clip time.
4. The weight is the product of the multi-way-blend weights and transition fractions along its path.
5. `0x004b0b80` multiplies by the slot weight and turns the entries into layers (U1 §6.10 step 5).
6. Clips at weight below 0.0001 are dropped there, unless bit 1 marks them as just faded out by a transition. A dropped entry does not count as output, so a non-cross-fade slot whose clips are all near zero ends itself.

### 8.10 Validation against the shipped graphs (CONFIRMED: re-implementation and replay)

**Method.** `scratchpad/anim_u3/u3val.py` and `u3replay.py` implement §8.3–§8.8 as read, on top of a lean loader of the 249 effective files (U2's extraction and U2's tokenizer and tree builder). "Reachable" means U2 §7.13's table-named graphs plus everything they nest: 203 graphs.

**Typing.** All of these were checked, and **there are no mismatches anywhere**:
- all 697 comparisons that run: 464 in expressions and 233 in `conditions`. The 16 conditions shadowed by an expression never run. There are 340 bool, 12 integer and 345 float comparisons, each against an operand 2 of the same type;
- all 180 multi-way-blender parameters, which are float (108 parameters and 72 filters);
- all 7 switch parameters, which are integer;
- all 22 filter sources, which are float parameters or float filters.

So the raw-bits reads of §8.5, §8.7 and §8.8 never reinterpret a value in the base game. A port can type-check instead.

**The fold, replayed.** All 304 expression trees were evaluated over every assignment of their comparisons, with the evaluator of §8.5 and with conventional precedence. **Exactly U2's four differ**, all in reachable graphs:

| Graph / transition | Assignments that differ | Example (engine / precedence) |
|---|---|---|
| `climb_fence_sm` / `3m Fence to 3m Fence Land 0` | 2 of 16 | `climb fall start` and `Supported by Ground` true, the other two false: **false** / true |
| `climb_fence_sm` / `3m Fence to 3m Fence Land High 0` | 2 of 16 | the same |
| `jump_sm` / `State 1 to Jump Land 0` | 6 of 32 | `Jump Left Ground Bool` and `Supported By Ground` true, the rest false: **false** / true |
| `jump_sm` / `Jump Run Start to Jump Run Land High 0` | 6 of 32 | the same |

In each case the engine's `((A or B) and C) …` blocks the landing transition, which precedence would allow, when the player is grounded but the `tag start` / `Jump Start Land Bool` terms fail. U2's data-side reading is confirmed by the code that runs.

**Multi-way blend replay** (`Multi Way Blend 1` of `free_fall_bt`, parameter `Speed Along rvec`; targets for Turn Left / Normal / Turn Right):

| x | base copy | patch copy |
|---|---|---|
| −60 | 0.8 / 0 / **0.2** | 1 / 0 / 0 |
| −10 | 0.5 / 0.5 / 0 | 0.5 / 0.5 / 0 |
| 60 | **0.2** / 0 / 0.8 | 0 / 0 / 1 |
| 150 | 0 / 0 / 0 | 0 / 0 / 0 |

- The base file blends 20 % of the opposite turn into fast turns, and the patch fixes it.
- Outside ±100 both give zero weight to all three inputs.
- `crouch_walk_blend_forward` (`direction degrees`, inputs 305, 360, 0, 90 and 0, 90, 145) gives full forward at 330° and 0°, an even split at 45°, and **zero for both inputs between 145° and 305°**. It is one half of a directional pair, and its parent supplies the other half.
- Over the 180 nodes, 20 have points where the input weights do not sum to 1, and 5 have stretches where all inputs are 0. These are all directional halves like this one, not defects.
- **Ramp.** With `max_blend_rate` 0.1, 0.4 and 1.0, a 0 → 1 change takes 3, **13** and 30–31 updates at 1/30 s. The 0.4 case: a naive double-precision `12 × (1/30) / 0.4` lands exactly on 1.0, but the real evaluator (`0x004bc0d0`, §8.7, pure SSE/SSE2 — no x87) stores the running weight as a 32-bit float and reloads that same 32-bit value every evaluation, so no extra precision survives between frames. After 12 updates the stored weight is `0.9999998807907104` — one float32 ULP short of 1.0 — and the 13th update's remaining distance (`1.1920929×10⁻⁷`) is smaller than the per-step `dt/max_blend_rate` (`≈0.08333334`), so the comparison falls into the snap branch and the 13th update is what actually reaches 1.0. **[Corrected 2026-10-07, following up a Team B report, settled by disassembly of the real instruction sequence rather than by abstract reasoning — see `0x004bc0d0`'s own scalar `movss`/`cvtpd2ps` round-trip through the weight slot.]**

**Filters.**
- `direction degrees` maps 3.14159 → 180 and clamps 7.0 → 360.
- `speed linear map filter` maps 2.5 → 0.5 and clamps 9 → 1.
- `Turn Angle Cap` clamps 0.3 → 0.05.
- `Turn Angle Delta Multi` (`multiplier` 15) answers a unit step with 0.5, 0.75, 0.875 … when run once per frame. As run in `free_fall_sm` and `parachute_bt` it gives 0.75, 0.938, 0.984 ….
- Every delta multiplier is stable (k per 1/30 s step ≤ 0.5), and no linear map is degenerate.

**Transitions.**
- 54 transitions have `length` 0. None chains into another zero-length one, so there is no zero-length cycle that could spin the completion loop of §8.3, and there are no self-transitions.
- The most transitions on one node is 7, within the 8-entry active list.
- Seven transitions carry a cancel window, all in `jump_sm`, `player_aim_movement_sm`, `player_move_aim_left_sm` and `player_move_aim_right_sm`.

**Output-list bound.** This is an upper bound on clips per evaluation:
- one per clip state or `clip` node;
- merges and blenders summed;
- switches maxed;
- nested graphs included;
- one transition in flight per state machine, and every cancelable transition rewinding at once.

The largest **as the game's own code evaluates one frame of one row** is **30, in `ride_left_ready_sm` and `ride_right_ready_sm`** (20 steady), then `drive_non-ready_sm` at 27 and `driving_sm` at 26. This counts "nested graphs included" at the nested graph's own steady bound — no transition of its own in flight — because that is as far as one row's own two passes walk: the nested graph is a separate row with its own clock, and this census doesn't give it one of its own in-flight transitions on top of its parent's.

**That is not the format's own worst case.** `ride_left_ready_sm`/`ride_right_ready_sm` are themselves reached as the `Ready` state's nested graph of `ride_left_sm`/`ride_right_sm`. Nothing in the file format or the evaluator ties the nested state machine's transition timing to its parent's — each is its own row, stepped independently. If the nested state machine also has its own transition in flight at the same moment as `ride_left_sm`/`ride_right_sm`'s own in-flight transition out of `Ready`, the bound for `ride_left_sm`/`ride_right_sm` rises to **31**: the nested pair's own 30 (itself already including *their* one in-flight transition) plus the parent's own in-flight clip state out of `Ready`. Applying that reading across all 249 files, 31 is the population maximum — **still below the 33rd-entry hazard, but by a margin of 1, not 2.**

**No shipped graph has been observed to actually drive both transitions at once**; the as-shipped, as-evaluated peak stays 30 (`ride_left_ready_sm`/`ride_right_ready_sm` themselves). So: margin 2 (32 − 30) against the observed census; margin 1 (32 − 31) against what the format itself permits. **[Corrected 2026-10-07, following up a Team B report; re-derived from Team B's own real-data test (`real_anim_node_graph_test.cpp`), which reproduces every other census figure in this section exactly, swept over all 249 shipped files under both the steady-bound and recursive readings.]**

**Unresolved tags.** `spinslower_sm` and `tiger_ride_sm` are reachable, and their default states have unresolved `tag_name`s and no `clip_name` (§8.6).

### 8.11 Resolutions

- **U0 §5.1 item 1:** closed again, from the evaluator side (§8.1).
- **U0 §5.1 item 2:** closed. Instance byte `+4` is a toggle request with no setter, so it is inert (HIGH, §8.1).
- **U0 §5.1 item 3:** closed. The registry ranges are never read; filters clamp to their own fields (HIGH, §8.8).
- **§5.1 item 17:** first half answered. Row `+0x48` bit `0x04` means "a clip with a `match_name` was emitted this evaluation" (§8.6). The rest of `0x004cc000` stays U1's.
- **§5.1 item 20:** answered for `clip_name` (block `+0x0c` cache), `start_time` (no effect), `layer` (group flags) and the loop bit (polarity corrected). The node constants `+0x18`/`+0x1a` stay open (§5.1 item 27).
- **§5.1 items 21, 22 and 23:** answered (§8.5, §8.7, §8.6).

---

## 9. U5 — Root motion and the post-sample adjusters

**Scope.** Unit U5 of `review/scoping-animation.md` §5. It covers:
- the post-sample step `0x004cfb60` and the module under it (`0x004cd000`–`0x004cfb60`);
- the animation-side inputs of the velocity composer that `spec-player-runtime.md` §5.4.3 left open (its OPEN 39): who consumes X (controller `+0x70`), the speed `0x004b49e0`, the test `0x009987d0`, the controller bits `+0x1a4`, the override weight `0x004cad00`, the layer-creation call at `0x004c6ebb`, and `0x004c8a80`.

Cited, not re-derived:
- the T/E/X split of the per-step root translation and controller flag `0x100` = `Anim_Movement`: `spec-player-runtime.md` §5.4.3, "Step 3 in detail";
- the clip-header root-motion transform (`+0x0C` rotation, `+0x1C` translation): `spec-anim-format.md` §2 and §4;
- the per-track override table: `spec-anim-format.md` §10;
- the character controller and what it does with `+0x1cec`: `spec-player-runtime.md` §5.6 and `spec-physics-runtime.md` §2 (nothing here changes the controller contract; §9.7 only adds a second writer of `+0x1cec`).

**Method.** Ghidra-free, as U0–U3.
- Capstone recursive-descent listings over `exe_va.py`; raw `E8`/`E9` rel32 and dword scans; U3's linear sweep of `.text` for displacement scans of the `+0xea0` object's fields.
- **Emulation of shipped code** (Unicorn, the image mapped read-only, no game state): the Euler builders `0x004cd740`/`0x004cd3d0`, the heading helpers `0x004cd810`/`0x004bc550`, the arcsine `0x008f4df0`, the follower `0x004cd8c0`, the joint writer `0x004ce670` (its matrix captured before the write) and the whole chain step `0x004cf7f0` on a synthetic object (§9.11). This runs the game's own instructions; it is not a re-implementation.
- Shipped data: `anim_files.xtbl` and `anim_triggers.xtbl` from `tools/an_xtbl/anim_xtbl/`, as U1.
- No Ghidra project was opened or copied, so there is nothing to delete. No `.czn_pc` data was touched. Helper scripts are in the session scratchpad (`scratchpad/u5/`).

**Read in full** (instruction counts from this pass's listings):
- **the module:** `0x004cfb60` (259), `0x004cf7f0` (244), `0x004cdd20` (617), `0x004ce670` (261), `0x004cf130` (261), `0x004cedb0` (243), `0x004cea50` (221), `0x004cd8c0` (284), `0x004cf5f0` (134), `0x004cf4a0` (97), `0x004cd810` (49), `0x004cd740` (35), `0x004cd7b0` (13), `0x004cd7e0` (16);
- **its math helpers**, read for structure and pinned by emulation: `0x004cd020` (99, vector sine/cosine), `0x004cd1b0` (74) and `0x004cd2d0` (65) (arctangent), `0x004cd3d0` (163, matrix → Euler), `0x004cd640` (74, Euler → matrix). A call-target index finds 21 function starts in `0x004cd000`–`0x004d0000` (these 19 plus U3's comparator halves `0x004cff10`/`0x004cff80`); the scoping census says 22, and the difference was not chased;
- **the output side:** `0x004c7df0` (66), `0x004bf0a0` (31), `0x004bf100` (25), the compose step `0x004c31b0` (361) and `0x004c50f0` (its cases 1 and 2);
- **the correction step and its setters:** `0x004b6530` (783 by this pass's walk), `0x004b74f0` (209), `0x004b7850` (181), `0x004b7ae0` (176), `0x004b7d50` (49), `0x004b7de0` (92), `0x004b7f70` (3), `0x004b7f80` (67), `0x004be350` (26), `0x004ca6d0` (115), `0x004be3b0` (first 40), `0x00da4130` (21);
- **the composer's other inputs:** `0x004b49e0` (197), `0x004b47f0` (159), `0x004cab10` (17), `0x004cab40` (41), `0x004bee30` (38), `0x004bda70` (21), `0x009987d0` (47), `0x009572f0` (69), `0x004b2950` (20), `0x004c71b0` (67), `0x004ca340`, `0x004cad00` (43), `0x004ca620` (40), `0x004c6e10` (181), `0x004b73f0` (90), `0x004bcf50`;
- **human side, for the contract only:** `0x009b2de0` (120), `0x009b34f0` (323), `0x009b0790` (44), and the construction chain `0x0073f2d0` → `0x004b1700` (head).

**Read for structure only:** `0x004c8a80` (1,236; §9.9), `0x004b50a0` (1,277; only its uses of `+0x70`/`+0xe0`), and the human-side drivers `0x009b0e90`, `0x009b3a20` and `0x009b4360`, which are U6's.

### 9.0 Results up front

1. **The post-sample step is not root motion [corrects scoping §1.6, §1.7, §2.1 and §4.2].** `0x004cfb60` and the ~3.3k-instruction module under it are the human's **spine-bend and look chain**. After the pose is sampled and before it is composed, it turns nine bones toward a look direction and an eye direction. The bones are `spine`, `spine1`, `spine2`, `neck`, `head`, `r-upperarmtwist1`, `l-upperarmtwist1`, `r-eye` and `l-eye` (engine strings, table `0x0130a8f0`).
   - The bone it reads at `+0xc8` is the rig bone named `spinebend`. The value it keeps is that bone's **heading** (`+0xbc`) and its change since last frame (`+0xb8`), used to counter-rotate the chain while the body animation turns.
   - It extracts no root motion and aligns no feet. It writes nothing that the velocity composer, the correction step or the character controller reads.
   - **Its output** is a set of **per-bone rotation overrides in the animation instance**, which the compose step `0x004c31b0` applies through `0x004c50f0` in the same pose job (§9.6). It also produces one scalar, the yaw the chain could not absorb (`+0xa8`), which the human-side `0x009b3a20` reads.
   - It is the library half of the scoping note's U6 (human procedural modifiers), which already names the shared follower `0x004cd8c0`.
2. **Where X goes: the correction step [corrects `spec-player-runtime.md` §5.0 item 4, §5.3 and §5.4.5].** `0x004b6530` is not a general "orientation from root motion" step. Outside an authored correction window it returns the human's current orientation unchanged. Inside a window it is a **motion-warp step** (§9.7):
   - it steers the desired velocity `+0x1cec` so that the character reaches a target position by the end of the window;
   - it rotates the orientation by a share of the remaining heading error each frame.

   X is read only here and by the synced-pair path `0x004b50a0`. During a **start correction** the body is driven by X, laid out in the target frame. The windows are clip triggers named `start correction begin`/`start correction end` (37 rows of `anim_files.xtbl`) and `dest correction start`/`dest correction end` (265 rows).
3. **`0x004b49e0`** is the speed of the current state's clip. For a plain clip it is \|root translation\| / duration; for a blend tree, the live `State`'s root speed over its last time step; for a node-graph state, 0 (§9.9).
4. **`0x009987d0`'s state test** is a trigger window: "a playing clip is inside `force use velocity start` … `force use velocity end`". 56 rows carry it, all player jump clips (§9.9).
5. **Controller `+0x1a4`:** every bit the band uses is now named (§9.10).
   - `0x40` is U1's "phase sync pending" (§6.1, §6.7).
   - `0x100` keeps the vertical velocity during a correction.
   - `0x800`, `0x1000`, `0x2000` and `0x4000` are the correction modes.
   - `0x10` has no setter anywhere in the image; its one consumer skips the correction step.
6. **`0x004cad00`** is the root track's per-track override value (`spec-anim-format.md` §10): 1.0, a stored constant, or an animated value, with anything above 1.01 read as 1.0 (§9.9).
7. **`0x004c6ebb`** is a call inside `0x004c6e10`, an **in-place clip swap with a timed cross-fade**. It copies the old layer's flags (bit `0x01` included), clock, rate, weights and fired-trigger bits, so a swapped layer keeps its T/E membership. It is reached from the controller's slot swap `0x004b73f0` (§9.9).
8. **`0x004c8a80`** is the bound-track rotation decoder (smallest-three keys, spherical interpolation), whose encoding is `spec-anim-format.md` §6c.2's. It matters to this unit only as "the root rotation at time t" (§9.7), so it was read for structure and not re-derived (§9.9).

### 9.1 The object at human `+0xea0` (CONFIRMED by listing of the initialiser `0x009b2de0`, the step and the human-side writers)

- The object is `0x470` bytes, cleared by `0x009b2de0`, which preserves only the instance handle at `+0xe0`.
- Offsets below are from the object; the human offset is `+0xea0` more.
- Writer roles are HIGH. The human-side drivers are U6's.

| Offset | Content | Writer |
|---|---|---|
| `+0x30` | flags (below) | `0x009b34f0`, `0x009b0790`, the step |
| `+0x38` | bit 0 = **relative mode**: the body target is taken in the `spinebend` bone's frame, the offsets `+0xd0`/`+0xd4` are added, pitch is followed at full speed, and the follower runs in linear mode | set at `0x009e9d57`; cleared by `0x009b2de0` and `0x009b1fe0` |
| `+0x60` | body look direction (vector) | `0x009b0e90` |
| `+0x70` | eye look direction (vector) | `0x009b0e90` |
| `+0x80` / `+0x84` | body / eye speed factor (× 3π rad/s) | `0x009b0e90` |
| `+0xa8` | **out:** the yaw the body chain could not absorb | the step; read by `0x009b3a20` |
| `+0xac` | extra yaw added to the body-turn compensation | `0x009b0900`, `0x009b4360` |
| `+0xb8` / `+0xbc` | `spinebend` heading change / heading | the step |
| `+0xc0` / `+0xc4` | `+0xc0` = heading of pose bone 1 − `+0xc4`, then `+0xc4` := 0 | the step (`0x004cea50`); `+0xc4` is seeded by the human side |
| `+0xc8` | rig bone index of `spinebend` | `0x009b2de0` |
| `+0xd0` / `+0xd4` | yaw / pitch offsets in relative mode | zeroed by `0x009b2de0`; no other writer found |
| `+0xd8` | **joint mask** (§9.2) | `0x009b34f0`, from `0x009b20a0` |
| `+0xdc` | limit scale: 1.0 by default; 1.1–1.4 or 0.6 by human state | `0x009b34f0` |
| `+0xe0` | animation-instance handle (the human's, re-read every frame) | `0x009b34f0` |
| `+0xe4`, `+0xe6`, `+0xe8`, `+0xea` | four bone indices from rig-info bytes `+0xa6`, `+0xa5`, `+0xa8`, `+0xa7` | `0x009b2de0` |
| `+0xec` | angle for those four bones | `0x009b34f0` (human `+0x1b54` when byte `+0x1b4c` is set and `+0x1b50` > 0; else 0) |
| `+0xf0` / `+0xf2` | two bone indices from rig-info bytes `+0x9a`/`+0x9c` | `0x009b0790` |
| `+0xf4`, `+0xf8`, `+0xfc` | an angle (human `+0xacc`) and two gains (1.9 running or 1.8 sprinting; 1.1) | `0x009b0790` |
| `+0x110` + `0x60`·j | joint record j = 0…8 (§9.3) | — |

**Flags `+0x30`** (CONFIRMED by listing; set-side roles HIGH):

| Bit | Meaning |
|---|---|
| `0x01` | disabled: only the shoulder and run-lean writes run (`0x004cf4a0`, `0x004cf5f0`). Cleared at `0x009b0768`; no setter found by a whole-`.text` displacement scan. |
| `0x04` | at rest: the last run ended with every output below 1e‑4 |
| `0x08` | returning to neutral: all four requested angles are below 1e‑4 |
| `0x10` | reset: the next run zeroes the per-frame chain deltas. Cleared at the end of every run. |
| `0x20` | run lean (`0x009b0790`, when locomotion state `+0xccc` is 2 or 3). Writes the two lean bones and limits the joint writes to `neck`/`head` (§9.6). |
| `0x40` | driven this frame (`0x009b34f0`) |
| `0x80` / `0x100` / `0x200` / `0x400` | zero the body pitch / body yaw / eye pitch / eye yaw request |
| `0x1000` | player class: pitch headroom × 2; with `0x8000`, snap to target |
| `0x2000` | set by `0x009b34f0` when human `+0xac4` > 20; not read by the module |
| `0x4000` | body-motion compensation while the controller is cross-fading (current slot weight below 1). Adds the chain's own animated change and the pose-bone-1 heading to the counter-rotation, and pre-steps yaw by at most 2 rad/s. |
| `0x8000` | snap pitch to target this run. Cleared at the end of `0x004cfb60`; no setter found. |
| `0x10000` | player in fine aim: yaw approaches its target at no more than 2π rad/s, bypassing the follower |

### 9.2 The joint table (CONFIRMED by static read)

**Records.** Nine 24-byte records at `0x01350498`. Each holds four limits (pitch maximum, pitch minimum, yaw maximum, yaw minimum, in radians), a **share** weight, a 16-bit **yaw mask** (record `+0x14`) and a 16-bit **pitch mask** (record `+0x16`).
- **Children:** `0x01350428`, 3 × i32 per joint, −1 for none.
- **Parents:** `0x01350404`.

| j | Bone | Pitch max / min | Yaw max / min | Share | Yaw mask | Pitch mask | Children | Parent |
|---|---|---|---|---|---|---|---|---|
| 0 | `spine` | 0.6109 / −0.4014 | 0.3491 / −0.3491 | 0.02 | `0x1ff` | `0x1ff` | 1 | — |
| 1 | `spine1` | 0.4363 / −0.4014 | 0.3491 / −0.3491 | 0.025 | `0x1ff` | `0x1ff` | 2 | 0 |
| 2 | `spine2` | 0.6981 / −0.4712 | 0.3491 / −0.3491 | 0.03 | `0x1ff` | `0x1ff` | 3, 5, 6 | 1 |
| 3 | `neck` | 0.0873 / −0.4363 | 0.1745 / −0.1745 | 0.4625 | `0x2c0` | `0x2c0` | 4 | 2 |
| 4 | `head` | 0.4363 / −0.9599 | 1.1344 / −1.1344 | 0.4625 | `0x2c0` | `0x2c0` | — | 3 |
| 5 | `r-upperarmtwist1` | 0.6981 / −1.2217 | 1.5708 / −1.0472 | 0.6 | `0x001` | `0x404` | — | 2 |
| 6 | `l-upperarmtwist1` | 0.6981 / −1.2217 | 1.0472 / −1.5708 | 0.6 | `0x002` | `0x008` | — | 2 |
| 7 | `r-eye` | 0.3491 / −0.1396 | 0.4363 / −0.4363 | 1.0 | `0x100` | `0x100` | — | 4 |
| 8 | `l-eye` | 0.3491 / −0.1396 | 0.4363 / −0.4363 | 1.0 | `0x100` | `0x100` | — | 4 |

**How the masks work.** A joint takes part in an axis when its mask word for that axis ANDs non-zero with the object's mask `+0xd8`.
- The arm-twist joints have mirrored yaw limits; the eyes look up 20° and down 8°.
- That the first pair is pitch and the second yaw is CONFIRMED by use: §9.4 applies the second pair to the heading request.
- Which `+0xd8` values the human side selects (`0x009b20a0`; `0x400` is special-cased) is U6's.

**Euler convention** (CONFIRMED by emulating `0x004cd740` and `0x004cd3d0`). An Euler vector here is (rotation about X, about Z, about Y). Y is up, as in the rest of the human code.
- A direction v has **heading** atan2(v.x, v.z) (`0x004cd810`) and **pitch** −asin(v.y) (`0x008f4df0` is a lane-wise arcsine), so a positive pitch looks down.
- A matrix's heading is atan2 of its third row's x and z (`0x004bc550`, with a gimbal fallback onto the first row).

### 9.3 The joint record (`0x60` bytes at object `+0x110` + `0x60`·j) (CONFIRMED by listing)

| Offset | Content |
|---|---|
| `+0x00` / `+0x04` | current yaw / pitch (the applied angles) |
| `+0x08` / `+0x0c` | target yaw / pitch |
| `+0x10` / `+0x14` | last frame's target yaw / pitch |
| `+0x18` / `+0x1c` | the animated pose's cumulative yaw / pitch from the chain root to this joint |
| `+0x20` / `+0x24` | their change since last frame |
| `+0x28` / `+0x2c` | the yaw the subtree below can still take: maximum / minimum |
| `+0x30` / `+0x34` | the same for pitch |
| `+0x38` | the subtree's share sum W |
| `+0x3c` / `+0x40` | own pitch headroom: maximum / minimum |
| `+0x44` / `+0x48` | own yaw headroom: maximum / minimum |
| `+0x4c` / `+0x50` | yaw / pitch velocity (follower state) |
| `+0x54` | rig bone index (−1 = joint absent) |
| `+0x58` | reference bone: `0x004be8b0` of the bone, or the bone named `pelvis` when that returns 0 (HIGH: the parent) |
| `+0x5c` | bit 0: "target jumped" (yaw target moved faster than π rad/s) |

### 9.4 The step `0x004cfb60` and the chain step `0x004cf7f0` (CONFIRMED by listing)

`0x004cfb60` (`this` = object; argument = the freshly sampled pose, 64-byte matrices, U4's step 4):
1. If `+0x30` bit `0x01` is set, apply only the shoulder and run-lean writes (§9.6) and return.
2. **Spinebend heading.** `+0xb8` = 0. If there is a pose, the bone `+0xc8` is valid, and its instance slot entry has flag `0x10`:
   - h = heading of that bone's matrix;
   - `+0xb8` = h − `+0xbc` (left at 0 when the old value is exactly 0.0);
   - `+0xbc` = h.

   Otherwise `+0xbc` = 0.
3. **Requests.** v = `+0x60`. In relative mode (`+0x38` bit 0) with the bone available, v is rotated by the bone's rotation and normalised (`0x00474fe0`, `0x004c0360`, `0x004998e0`, `0x00446f60`).
   - The body request is (yaw_v, pitch_v) = (heading v, −asin v.y).
   - The eye request is (yaw_t, pitch_t), the same from `+0x70`.
   - `+0x30` bits `0x80`/`0x100`/`0x200`/`0x400` zero pitch_v / yaw_v / pitch_t / yaw_t.
4. **Neutral handling.** If all four requests are within 1e‑4 of 0: `+0x30` |= `0x08`.
   - If `0x04` (at rest) is already set, also set `0x10` and apply only the shoulder and run-lean writes. The joints keep their last overrides, which are within 1e‑4 of identity by construction.
   - Otherwise, keep running the full chain with the zero requests, so the joints ease back.

   Any non-zero request clears `0x04` and `0x08`.
5. yaw_v is wrapped into [−π, π) (`0x00dadeb0`). In relative mode, `+0xd4` is added to pitch_v and `+0xd0` to yaw_v.
6. With a pose, call `0x004cf7f0`(pitch_v, yaw_v, pitch_t, yaw_t, h). Without one, apply only the shoulder and run-lean writes. Then clear `+0x30` bit `0x8000`.

`0x004cf7f0`, in order:
1. The shoulder writes `0x004cf4a0` and the run-lean writes `0x004cf5f0` (§9.6).
2. **Headroom from the pose** (`0x004cea50`, §9.5).
3. **Subtree aggregates** (`0x004cedb0` on joint 0, recursive, §9.5).
4. **Distribute the body request** over the tree from joint 0 (`0x004cf130`, §9.5) with mask `+0xd8`. Its return value is the yaw the chain could not absorb, stored to **`+0xa8`**.
5. **Distribute the eye request** to each eye separately (`0x004cf130` on joints 8 and 7). The request is first reduced by what the body chain already turns: (yaw_t − Σ current yaw of joints 0–4, pitch_t − Σ current pitch of joints 0–4). The eyes therefore lead and then re-centre as the head arrives (§9.11).
6. **Follow** (`0x004cdd20`, §9.5): joint 0's tree with speed factor `+0x80`, then joints 8 and 7 with `+0x80` temporarily replaced by `+0x84`. Each call returns the accumulated current yaw and pitch along its deepest branch.
7. If `+0x30` bit `0x08` is set and the body and eye totals are all within 1e‑4 of 0: `+0x30` |= `0x04` (at rest).
8. **Write** the overrides (`0x004ce670`, §9.6) for joints 8 and 7, then for joint 0's tree, or joint 3's (`neck`, `head`) when run lean `0x20` is set. Every write uses the turn angle Y = (body yaw total from step 6) + h.
9. Clear `+0x30` bit `0x10`.

### 9.5 Headroom, distribution and the follower (CONFIRMED by listing; the end-to-end behaviour by emulation, §9.11)

**Headroom `0x004cea50`.**
- Unless `+0x30` bit `0x10` is set: `+0xc0` = heading of pose bone 1 − `+0xc4`, then `+0xc4` = 0.
- For each joint with a bone, its pose rotation is read as Euler angles (`0x004cd3d0`). Joint 0's are taken relative to its reference bone. The angles are zero when the instance is invalid or the slot lacks flag `0x10`.
- Let a_y and a_p be the yaw and pitch, wrapped into [−π, π), and s the limit scale `+0xdc`. Pitch limits use 2s when `+0x30` bit `0x1000` is set.
  - **Yaw headroom:** `+0x44` = 0 if a_y > L_ymax·s, else min(L_ymax·s − a_y, π). `+0x48` = 0 if L_ymin·s > a_y, else max(L_ymin·s − a_y, −π).
  - **Pitch headroom:** `+0x3c` = 0 if a_p > L_pmax·s, else L_pmax·s − a_p. `+0x40` = 0 if L_pmin·s > a_p, else L_pmin·s − a_p.
  - That is, the authored limit minus what the animation already uses.
- Cumulative angles are summed down the parent table into `+0x18`/`+0x1c`. Their per-frame change goes to `+0x20`/`+0x24`, zeroed while `+0x30` bit `0x10` is set.

**Subtree aggregates `0x004cedb0`.** These are post-order over the participating joints. A joint whose two masks both miss returns "not participating" and is ignored by its parent.
- **Store.** `+0x28`/`+0x2c`/`+0x30`/`+0x34` = the minimum of the children's maxima and the maximum of their minima (starting from ±2π). `+0x38` = W = min(1, the children's minimum).
  - With no participating child, all five are 0.
- **Report up.** Each axis the joint takes reports (own headroom + subtree) to its parent:
  - for yaw, the own headroom is first scaled by f;
  - W is reported as share + W;
  - an axis the joint does not take reports 0.
- **f couples pitch to yaw.** f = clamp(1.5·(h − p)/h, 0.5, 1) when the joint's last pitch target p is past 0 toward a pitch-headroom bound h with \|h\| > 0.1; otherwise f = 1, and always 1 for the eyes. A joint pitched near its limit offers at most half its yaw.

**Distribution `0x004cf130`(a_yaw, a_pitch, object, joint, mask)**, recursive. The two masks behave differently:
- If neither of the joint's masks hits: both targets are 0, and the children get the request unchanged (the joint is transparent).
- If only one hits: the other axis's target is 0, and that axis's request passed down is also 0. The axis is cut off for the subtree.

For an axis that hits, with request a, subtree range [M⁻, M⁺], own headroom [h⁻, h⁺], share σ (table) and subtree share W:
- c = clamp(a, M⁻, M⁺): what the descendants could take.
- r = a − c: the excess.
- r′ = clamp(r, h⁻, h⁺).
- **target = clamp(c·σ/(σ + W) + r, h⁻, h⁺)**, stored to `+0x08` (yaw) or `+0x0c` (pitch).
- Passed to each child: c − (target − r′).

The return value is a_yaw − target_yaw − (yaw passed down), which is the root's unabsorbed yaw. Children's returns are discarded.

**Follower `0x004cdd20`(object, joint, &yaw_total, &pitch_total)**, recursive.
- **Speeds.**
  - Base: A = `+0x80`·3π rad/s.
  - Pitch uses A/3 unless relative mode is set.
  - `neck` and `head` use 2A.
  - The eyes use a fixed 14 rad/s.
  - A speed is × 0.75 when the current value is within 0.05 rad of its target and the target is not jumping (`+0x5c`).
- **Counter-rotation of yaw.**
  - o = `+0xb8` + `+0xac`, plus 1.1·`+0x20` + `+0xc0` under `+0x30` bit `0x4000`.
  - It is applied only when it moves the current value toward the target (o and target − current of opposite sign): current −= o, or current := target with the velocity × 0.25 if that would overshoot.
  - Pitch is counter-rotated the same way by `+0x24`, multiplied by (1.2 + σ) under `0x4000`.
- **Yaw update**, first matching case:
  - `0x10000`: step toward the target by at most 2π·Δt;
  - `0x1000` with `0x8000`: snap, velocity 0;
  - otherwise: the follower `0x004cd8c0`(current, target, speed, &velocity, relative mode), clamped to the own yaw headroom, with the velocity clamped to ±speed·Δt.

  Under `0x4000`, a pre-step of at most 2·Δt toward the target comes first.
- **Pitch update** is the same, without the headroom clamp. It snaps when `+0x30` bit `0x8000` is set or the global byte `0x03171bca` is non-zero. That byte has no writer by direct address anywhere in the image (HIGH: a debug switch that stays 0).
- The velocity is zeroed when current equals target, and clamped to ±A.
- The targets are saved to `+0x10`/`+0x14`.
- **Return:** own current value + the largest-magnitude child total, per axis.

**The follower `0x004cd8c0`**(current, target, A, &velocity, linear) is shared with the aim/look code (`0x009b51de`).
- **Linear mode:** constant rate 0.15·A rad/s.
- **Otherwise:** an acceleration-limited approach that brakes to land on the target. Emulated step responses from 0 to 1 rad show no overshoot, reaching within 1e‑3 in 34, 19, 15 and 13 frames for A = π, 3π, 14 and 6π.
- **Time step:** Δt is the frame time `0x0132a0ac`.

### 9.6 Output: per-bone rotation overrides in the animation instance (CONFIRMED by listing; the matrix by emulation)

**The joint writer `0x004ce670`(object, joint, Y)**, recursive over the children.
- It builds a 3×3 rotation from the joint's current yaw ψ and pitch θ: **R = Rx(θ)·Ry(ψ)**, row-vector convention, with the pitch axis turned by Y about the up axis when Y ≠ 0.
- Y (§9.4) is the chain's total yaw plus the `spinebend` heading, so the pitch axis stays perpendicular to the look direction.
- A joint whose current yaw and pitch are both exactly 0 is not written.

**Storage.**
- The matrix goes into the instance's lazily allocated **override block** (instance `+0x348`, `0x004c8150`): 15 records of `0x4c` bytes, the 3×3 at record `+0x24`, **record index = joint index**.
- The bone's 12-byte **slot entry** (instance `+0x08` array) is marked:
  - `+0x08` = **mode 1**;
  - flags &= ~`0x20`, |= `0x04` ("override present");
  - `+0x04` = record index.

**The other writers into the same block.**
- **Shoulder writes `0x004cf4a0`:** the four bones `+0xe4`…`+0xea` get Rx(±`+0xec`) through the instance API `0x004bf0a0` → `0x004c7df0`, **mode 2**, records 10–13. `+0xe6` and `+0xea` get +angle; `+0xe4` and `+0xe8` get −angle.
- **Run-lean writes `0x004cf5f0`** (only under `+0x30` bit `0x20`): bones `+0xf0` and `+0xf2` get a rotation about the axis vector at `0x029f5680`, by `+0xf4`·`+0xf8` and `+0xf4`·`+0xfc`, as mode 1 in records 5 and 6. Joints 5 and 6 use the same records, which is why the joint writes start at `neck` under `0x20`.
- **`0x004c7df0`** is the general setter. Mode 3 sets flag `0x20` instead of `0x04`. Modes 4 and 6 also store a second 3×3 at record `+0x00` and a float at `+0x48`.
- **`0x004bf100`** clears one slot's override. Its one caller is `0x00998711`.

**Consumer.**
- The pose job runs step 4 (this module) before step 8 (compose); see scoping §1.6. In compose, `0x004c31b0` checks each slot whose entry has flag `0x04` and mode 1–8, and calls `0x004c50f0` with the record. `0x004c50f0` dispatches through the 8-way table at `0x004c55dc`.
- **Mode 1** replaces the bone's rotation rows with the product of the rows and the override, keeping its translation. **Mode 2** composes the override as a full transform. Both are CONFIRMED as structure; the product order is HIGH.
- The overrides therefore take effect in the same frame.
- Nothing in the library clears the overrides each frame. A joint keeps its last matrix until it is rewritten or cleared.

### 9.7 The correction step `0x004b6530`: where X goes (CONFIRMED by listing) **[corrects `spec-player-runtime.md` §5.4.5]**

**The pointers it works through** (CONFIRMED): controller `+0x14`, `+0x18` and `+0x1c` point to human `+0x40` (position), `+0x4c` (orientation, 3×3) and `+0x1cec` (desired velocity). The chain is `0x0096eec8` → `0x0073f2d0` → `0x004b1700`.

**Call.** It is called once per human per frame from `0x009989b0` step 3 (`0x00998fcd`), after the velocity composer has written `+0x1cec` and before physics, as `0x004b6530`(out, controller, Δt). The caller applies `out` through the human's virtual slot `+0x48` (set orientation).

**Entry.**
1. out = a copy of the human's orientation (`0x00da4130`, nine floats).
2. Clear `+0x1a4` bit `0x04`.
3. Return at once when `+0x1a4` bit `0x10` is set or the instance is invalid.
4. If any in-use slot carries slot flag `0x2000` (a synced pair, §9.8), run `0x004b50a0` instead (not read beyond its `+0x70`/`+0xe0` uses) and return.

**Timers.** T₁ = `+0x16c` advances by Δt at entry. T₂ = `+0x170` advances at exit while `0x4000` or `0x2000` is set.

**Destination position correction** (`+0x1a4` bit `0x800`). Slot by state id `+0x138`; target `+0x140`; window [S, E] = [`+0x12c`, `+0x12c` + `+0x124`].
- Once T₁ > S: d = the clip's root translation over [t − Δt, t] (`0x004be3b0`, t = the layer's time), rotated by the current orientation R.
- The remaining error is err = target − (P + R·d).
- **Window upkeep:**
  - past E with the slot still playing, set `0x1000` (held open);
  - while held, E := T₁ + 2·Δt, so each held frame closes a third of the remaining error;
  - when the slot is gone, clear `0x800`/`0x1000` (and set `+0x138` = −1).
- Correction A = f·err, with f = (min(T₁, E) − max(T₁ − Δt, S)) / (E − max(T₁ − Δt, S)): this frame's share of the remaining window.

**Start correction** (`+0x1a4` bit `0x4000`). Slot `+0x160`; target position `+0xb0`; target orientation R_t (9 floats at `+0x80`); window [`+0x164`, `+0x168`] on T₂; start position `+0xc0`.
- Let d₀ = the clip's root translation from 0 to t. The clip's implied origin is O = P − R_t·d₀.
- `+0xd0` += f·(target − O), with f as above on T₂. The mode clears itself past the window end.

**Applying the corrections** (when either fired this frame):
1. `+0xe0` += **X**, where X is controller `+0x70` as written by the composer.
2. **Start correction:**
   - `+0xd0` += R_t·X;
   - the velocity is **replaced**: `+0x1cec` = (`+0xc0` + `+0xd0` − P)/Δt.

   The body follows the one-shot layers' root motion laid out in the target frame, plus the warp toward the target.
3. **Destination correction:** `+0xd0` += A, and `+0x1cec` += A/Δt.
4. `+0x1a4` |= `0x80`, so the controller step treats the vertical as animation-driven.
5. With `+0x1a4` bit `0x100`, the old vertical component of `+0x1cec` is restored.
6. A speed above 199.5 units/s (\|v\|² ≥ 39,800.2) is scaled down to 199.5.

**Heading corrections.** Both rotate `out` about the up axis (`0x00da61b0`) by f·e and set `+0x1a4` bit `0x04`.
- **Start heading** (with `0x4000`, once T₂ > `+0x164`): desired = `+0x15c` + the clip's heading change from the window start to t; e = wrap(desired − current heading).
- **Destination heading** (`0x2000`, once T₁ > `+0x12c`): desired final heading `+0x134`; e = wrap(`+0x134` − (current heading + the clip's heading change from t to its end)); window [`+0x12c`, `+0x12c` + `+0x128`].
- The clip heading change is `0x004be350` → `0x004ca6d0`: two root rotations from `0x004c8a80`, the conjugate product, then `0x004bc550`.

**Consequences.**
- **Outside every window, `out` is the human's orientation unchanged.** This step applies no root rotation in ordinary locomotion. What turns a human then is not established here (§5.1 item 35).
- **X has exactly two readers:** this step (`0x004b6aad`) and the synced-pair path (`0x004b5289`, `0x004b52a4`). The writers are the composer and the constructor zero at `0x004b1780`.

### 9.8 The correction setters and the clip triggers that time them (CONFIRMED by listing; shipped counts by direct read)

**Trigger hashes.** At start-up, `0x004b1a52`…`0x004b1b77` hash six trigger names with `0x00da7890`, the `anim_triggers.xtbl` hash of `spec-tables-animation.md` §1.2/§9:

| Global | Name | Count |
|---|---|---|
| `0x01350700` | `Spinebend` | — |
| `0x01350704` | `free move` | — |
| `0x01350708` | `start correction begin` | — |
| `0x0135070c` | `start correction end` | — |
| `0x01350710` | `dest correction start` | — |
| `0x01350714` | `dest correction end` | — |

`0x004bdec0`(hash, clip, 0) returns a trigger's time in ticks, and the setters divide by 4,800 to get seconds.

| Setter | Arguments (HIGH where marked) | Sets | Window | Callers |
|---|---|---|---|---|
| `0x004b74f0` (wrapped by `0x004b7800`) | out, controller, state id, target position, target orientation (9 floats), window end (< 0 = clip duration), keep-vertical byte | `0x4000`; `0x04` when the heading differs by more than 0.01 rad, otherwise `out` := the target orientation; `0x100` from the byte, or always when the target equals the current position. `+0xc0` = position; `+0xd0` = `+0xe0` = 0; `+0x158`/`+0x15c` heading error / target | `start correction begin` … `end` | 20 + 13 call sites (vehicle, AI and synced-move code) |
| `0x004b7ae0` | controller, state id, target, window length (< 0 = duration), keep-vertical byte | `0x800` (and `0x100`); T₁ = 0. `+0x140` = the target less the clip's root motion after the window (HIGH) | `dest correction start` … `end` | 6 |
| `0x004b7850` | controller, state id, target matrix, window length | `0x2000` with `0x04`. `+0x134` = the target heading; `+0x130` = the initial error | the same `dest correction` triggers | 19 |
| `0x004b7d50` | controller, state id, target 4×4, length, keep-vertical | `0x004b7ae0` with the matrix's translation, then `0x004b7850` with its rotation | — | 12 |
| `0x004b7f70` | controller | clears `0x800`, `0x1000`, `0x2000` and `0x4000` | — | 19 |
| `0x004b7de0` | two controllers, states, matrices | links a **synced pair**: mutual `+0x10c`; slot flag `0x2000` on both slots; the leader gets `+0x1a4` bits `0x01` and `0x02` | — | 2 |
| `0x004b7f80` | controller | unlinks the pair and clears the alignment bits on both. Called from U1's slot free `0x004afc70` (`0x004afc9a`) | — | 6 |

**Shipped data** (`anim_files.xtbl`, 5,968 rows):
- `dest correction start`/`end`: 265 rows. By prefix, `cop` 39, `bnch` 30, `auto` 28, `plym` 27, `fr` 18, `vtol` 16, `tank` 14, … — vehicle entries and exits, bench and cover moves.
- `start correction begin`: 36 rows, and `start correction end` 37 (`brute_mc_atkvcl3` has only the end), 29 of them `cop` cover moves.
- Both pairs: 27 rows. Only one destination-correction row also carries `Anim_Movement`, so corrections and the root-motion-driven velocity case are almost disjoint.
- `rot dest correction start`/`end` (one row, `auto_entrl_bigtrck_turret`) has no reader: no code hashes the name.

### 9.9 The composer's other animation inputs (CONFIRMED by listing)

**`0x004b49e0`(controller, state id): the current state's clip speed.**
- **Gate.** The composer calls it only when the speed `+0xa60` is −1, controller `+0x1a4` bit `0x40` (U1's phase sync pending) is set, and a current slot exists. It passes the current slot's state id (`+0x50` → `+0x08`).
- **Managed (node-graph) slot:** 0.
- **Blend tree:** the first `State` with positive weight and a live layer gives \|root velocity over the layer's last time step\|. The path is `0x004bda70` → `0x004bee30` → `0x004cab40`, where the velocity is Δ/elapsed seconds.
  - Without such a `State`: (Σ weight × clip-header root translation)/duration, via `0x004b47f0` and `0x004b44a0`.
- **Plain clip:** \|clip-header root translation\| / duration. The translation is header `+0x1C` (`0x004cab10`; `spec-anim-format.md` §2/§4), and the result is 0 when the duration is at most 0.01 s.
- So an NPC with "use the animation's speed" that is blending back into a phase-synced locomotion state moves at that state's own average root speed, with E dropped.

**`0x009987d0`(&E), human in `ESI`.**
- **True** when `0x009572f0`(human, start, end) holds for the trigger pair `force use velocity start` (`0x0153bcf4`) / `force use velocity end` (`0x0153bcf0`). Those ids are resolved by name through `0x004ca340` at `0x0073da46`…`0x0073da69`.
- **Otherwise false** when `0x00994f60` holds (free-fall family, U0 §5.1 item 9) or the human is jumping (`0x0098b970`).
- **Otherwise** the result is \|E.y\| > 1e‑4.

**`0x009572f0`(human, a, b)** is the generic "inside a trigger window" test: count(a) > 0 and count(a) > count(b).
- Here count(x) = `0x004b2950` → `0x004c71b0`, the number of the instance's layers whose clip carries trigger x and whose fired mask (layer `+0x30`, 3 bits per trigger) has that trigger's "passed" bit.
- A special case, when human `+0x1c9c` bit `0x80000` is set and state `0x1d7e` is playing, also accepts the pairs `movement lock`/`unlock` and `face camera start`/`end` for state times in [0, 4.75] s. It does not apply to the composer's pair.
- **Shipped data:** 56 rows carry `force use velocity`, all player jump clips (`plym` 52, `plyf` 4). So during a jump's take-off window the jump clip's vertical root motion drives the vertical (`+0x1a4` |= `0x80`), even though the human is "jumping".

**`0x004cad00`: the root track's override value.** It is called by `0x004ca620`, the per-layer factor f of the layer walk, after `0x004c1300` binds the clip.
- A track remapped to `0xff` (binding `+0x218`) → 0. A track at or past the clip's track count → 0.
- Clip flag `0x40` clear → 1.0.
- Otherwise the track's override kind (u8 or u16 per flag `0x80`, `spec-anim-format.md` §6c.1/§10):
  - 0 → 1.0;
  - 1 → the stored constant;
  - ≥ 2 → `0x004ca010`(time), the multi-value path still open in `spec-anim-format.md` §10.5.
- A value above 1.01 is replaced by 1.0.

**`0x004c6ebb` → `0x004c6e10`: in-place clip swap.** Arguments: handle in `EAX`; instance, new clip, fade time.
1. Find the layer by serial. Refuse mode-2 layers (layer `+0x48` = 2).
2. Create a new layer for the new clip with **the old layer's flags** (`0x004c6980`).
3. Copy the old layer's times, rate, group, weights and flags (layer `+0x08`…`+0x28`).
4. Carry over its fired-trigger bits by trigger id.
5. Set the new layer's fade-in length (`+0x40` = fade × 4,800 ticks).
6. Fade out the old layer (`0x004c6c70`).
- The chain is `0x004b73f0` (controller: swap the record of a non-managed slot in place, recursing over the child ring) → `0x004bcf50` → `0x004c6e10`. The callers are `0x0095b0f5` and `0x0095b721` (per-human step) and `0x009b3d67` (in `0x009b3a20`, the reader of `+0xa8`).
- Layer flag bit `0x01` is copied, so a swapped layer stays in T or E as it was. This completes the T/E work's creator list.

**`0x004c8a80`.** This is the rotation sampler of one bound track. Its structure is CONFIRMED:
- no keys → identity (0, 0, 0, 1);
- one key → decode it;
- several → key search, then spherical interpolation (`0x00ea2b90`, `0x00ea2470`);
- keys decoded as smallest-three quaternions through the scale table `0x01350580` and the lane table `0x01350680`, with the rotation mode at binding `+0x2ce`.

The encoding is `spec-anim-format.md` §6c.2/§6c.3's. Its callers are the sampler (`0x004c2864`, `0x004c2dda`, `0x004c2e3e`), the delta routines (`0x004ca97d`, `0x004ca9c7`, `0x004caaac`, `0x004cacac`) and the heading change `0x004ca6d0`. U5 needs only its contract, "root rotation at tick t", so a bit-for-bit read is left to the format spec (§5.1 item 39).

### 9.10 Controller `+0x1a4`: all bits (CONFIRMED by listing)

| Bit | Meaning | Set by | Cleared by / read by |
|---|---|---|---|
| `0x01` | synced-pair leader | `0x004b7de0` | `0x004b7f80`; `0x004b5379`, `0x004b64af` |
| `0x02` | synced pair (leader) | `0x004b7de0` | `0x004b5f6c` |
| `0x04` | orientation corrected this frame | `0x004b6530`, `0x004b74f0`, `0x004b7850` | `0x004b6530` at entry |
| `0x10` | skip the correction step | **no setter found** | U1 free path (slot flag `0x1000`); read by `0x004b6530` |
| `0x40` | phase sync pending (U1 §6.1, §6.7) | play entries `0x004b1b90` and `0x004b1ef0`, re-trigger `0x004b2d20` | U1 update; read by the composer (§9.9) |
| `0x80` | the animation drives the vertical | the composer (via `0x009987d0`), `0x004b6530` | the controller write-back (`spec-player-runtime.md` §5.4.3 step 3) |
| `0x100` | keep the vertical velocity during a correction | `0x004b74f0`, `0x004b7ae0`, game code (for example `0x00526b0e`) | `0x004b7de0`, `0x004b7ae0` |
| `0x200` | restart at phase 0 (U1) | `0x004b2280` | U1 update |
| `0x800` | destination position correction | `0x004b7ae0` | `0x004b6530`, `0x004b7f70`, `0x004b7f80` |
| `0x1000` | destination window held open | `0x004b6530` | the same |
| `0x2000` | destination heading correction | `0x004b7850` | the same |
| `0x4000` | start correction | `0x004b74f0` | the same |

### 9.11 Validation (CONFIRMED by emulation and direct read)

**Chain step, emulated** (`scratchpad/u5/emu_chain.py`). The object had no instance (no writes), an identity pose, all nine joints bound, a limit scale of 1.0 and speed factors of 1.0. Requests were held constant for 40 frames.
- **Yaw 1.0 rad, mask `0x1ff`:**
  - The targets are `spine` 0.030, `spine1` 0.037, `spine2` 0.044, `neck` 0.175 (its full range) and `head` 0.714. They sum to 1.000.
  - The arm twists take the residual 0.889 that `spine2` passes on.
  - The eyes jump to their 0.436 limit, then return to 0 as the head arrives. The chain total reaches 1.000 by frame 20.
- **Mask `0x2c0`:** the arm twists and eyes drop out (their masks miss), and `spine`…`spine2` take exactly their shares (0.02 / 0.025 / 0.03).
- **Yaw 2.5 rad with pitch −0.5:**
  - The spine joints saturate at 0.349, `neck` at 0.175 and `head` at 0.825. The total is 2.047.
  - `+0xa8` = 0.4535, which is 2.5 − 2.047, the unabsorbed yaw the human side reads.
  - The pitch total settles at −0.500.

**Joint writer, emulated.** Yaw-only and pitch-only matrices match `0x004cd740`'s Ry and Rx. Combined, they equal Rx(θ)·Ry(ψ), and Y ≠ 0 tilts the pitch axis as described.

**Follower, emulated:** see §9.5.

**Shipped data:** the trigger counts in §9.8 and §9.9.

### 9.12 What a port must reproduce

1. **Spine-bend and look chain** (only if the port draws heads, spines and eyes that track targets).
   - The nine-joint table of §9.2 and the per-axis masks.
   - Headroom equals the authored limit minus the animated angle, coupled through f.
   - The proportional distribution with excess pushed to the joint.
   - The eye request reduced by the head chain.
   - The follower and its speeds.
   - Counter-rotation by the `spinebend` heading change.
   - Overrides as Rx(θ)·Ry(ψ) applied as a rotation on top of the sampled local pose before composition.
   - The unabsorbed yaw handed back to the game.
2. **Corrections** (needed for any synced or aligned move, such as vehicle entry and exit, cover and benches).
   - The trigger-timed windows.
   - The per-frame share f of the remaining window.
   - The start correction **replaces** the desired velocity with (start + accumulated(R_t·X + warp) − P)/Δt.
   - The destination correction **adds** f·err/Δt.
   - The 199.5 units/s cap.
   - The keep-vertical bit.
   - Heading corrections rotate the orientation directly.
   - Velocity-driven locomotion (`spec-player-runtime.md` §5.4.4) is unaffected outside the windows.
3. **Velocity composer inputs.**
   - The clip speed of §9.9 for "use the animation's speed" NPCs during phase sync.
   - The `force use velocity` window turning on animation-driven vertical motion during jump take-off.

### 9.13 Resolutions and corrections

- **`spec-player-runtime.md` OPEN 39:** answered for the orientation step (it is the correction step, §9.7), `0x004b49e0` (§9.9), `0x009987d0` (§9.9), controller `+0x1a4` (§9.10), `0x004cad00` (§9.9) and the creator at `0x004c6ebb` (§9.9). `0x004c8a80` is identified, and its full read is routed to the format spec. What remains is re-scoped there and in §5.1 items 32–39.
- **`spec-player-runtime.md` §5.0 item 4, §5.3 and §5.4.5** say orientation comes from the animation's root rotation through X and `0x004b6530`. That is corrected: X feeds only corrections, and the step applies authored correction rotations only.
- **`review/scoping-animation.md` §1.6, §1.7 and §2.1:** the post-sample module is the spine-bend and look chain (U6's library half), not root extraction or foot alignment. The scoping note's §4.2 "root motion" row is now covered by `spec-player-runtime.md` §5.4 plus this section. Both files were updated in this pass.
- **U1 §5.1 item 13:** slot flag `0x2000` is the synced-pair mark (set by `0x004b7de0`, cleared by `0x004b7f80`), and the routine behind it in the correction step is `0x004b50a0`. Controller `+0x1a4` bit `0x10` has no setter in the image; its one reader skips the correction step.
- **U1 §5.1 item 14:** clip creation has a second caller, the clip swap `0x004c6e10` (§9.9).
- **U1 §5.1 item 11**, the phase-sync bit `0x40`: one more consumer, the composer's clip-speed path (§9.9). The run-time effect is still open.
- **U3 §5.1 item 26** (the graph row's root-motion sync outputs) was not resolved. None of the correction setters reads a graph row.

---

## 10. U7 — Other controllers on demand

**Scope** (`review/scoping-animation.md` §5 item 8): the non-human clients of the animation library's shared API — vehicle (`0x00a760e0`, `0x00a76690`), rider lean (`0x00af4b80`, **not investigated here, see below**), `object_rig` (`0x008cb9f0`), the vehicle ride-state collision writer (`0x00b09ab0`, **animation side only, cited from `spec-player-runtime.md` §8.5, not re-derived**), and the world update's two remaining "`[unidentified]`" pose clients from `review/scoping-animation.md` §1.0's shape diagram. Per that note's own framing, each of these is small and was expected to "reuse U0–U4 unchanged." §10.6 below gives the verdict: true for the shared function surface, not entirely true for the behavior built around it.

**Method.** Ghidra-free: `tools/harnesses/exe_va.py` plus a small capstone-based linear lister (scratch: `list_func.py`) run through `py` (PowerShell, `PYTHONIOENCODING=utf-8`). No `.czn_pc` zone data opened.

### 10.1 — The vehicle ride state's per-frame handler `0x00b09ab0` (animation side only)

**Not re-derived.** `0x00b09ab0` is Player unit U6.4's ride-state per-frame handler, fully specified in `spec-player-runtime.md` §8.5 (the body-lag model, the collision-velocity test, the camera hand-off is §8.6). The only animation-API surface it touches is §8.5 steps 7–8, cited here rather than re-read from the executable:

- It looks up two parameter ids **once**, through the registry lookup `0x004cbaf0` — the identical U0 function, with the identical "cache the id behind a guard-bit" idiom `0x00958690` uses — for the names `"Collision Bool"` and `"Collision Dir"`.
- It writes them with the **same bool/float setters U0 already specifies**: `0x004b1410` for `Collision Bool`, and the wrapped `Collision Dir` value (also through `0x004b1410`, since both are stored as the controller's typed value block at `+0x28`).
- The write is gated on "a vehicle and an animation controller whose `+0x24` byte is set" — exactly U1's controller field (`+0x24`, node-graph-state count) used as the "has at least one managed state, so the parameter block actually exists" guard that `0x00958690` and the setters themselves also rely on (`review/scoping-animation.md` §1.3's lazy-block-allocation note).

No special-cased parameter path, no alternate setter, no alternate registry. For this root, **"reuses U0 unchanged" is exactly true**, and nothing else about `0x00b09ab0` belongs in this unit — see `spec-player-runtime.md` §8.5 for the lag model, timers and tweak globals that produce the values being written.

### 10.2 — Rider lean `0x00af4b80`

**[2026-10-07: landed.]** Fully specified by Player unit U6.8, `spec-player-runtime.md` §12.4 (cited here, not re-derived). All 11 names in `review/scoping-animation.md` §1.3's writer table for this address are confirmed by direct string read and gated on the vehicle-interaction slot's resolved phase: while the slot is in the ride phase, `Vehicle Steering`/`Vehicle Steering Time`/`Vehicle MPH` come from the vehicle's own state (remote-control flag, steering-angle query, linear velocity), `Vehicle Forward Lean`/`Vehicle Vert Lean`/`Vehicle Tilt` (plus their three `… Abs` forms) are a bounded remap of the ride body-lag model's error fields (`spec-player-runtime.md` §8.5) through three newly-named `VI_*` tweaks, and `Wheelie Bool`/`Endo Bool` are two vehicle-side predicates; outside the ride phase all 11 are held at a neutral/false value. Setters are the same two of U0's "three typed setters" (`0x004b1510` float, `0x004b1410` bool) on the same controller convention `0x00958690` uses — "reuses U0 unchanged" holds for the API surface, same as §10.1/§10.3.

### 10.3 — The vehicle's own controller: `0x00a760e0` and `0x00a76690`

**Shape.** Both are `thiscall`s on a vehicle sub-object at **vehicle `+0x688`** (not the vehicle record's base). That sub-object's own `+0x3f0` holds the vehicle's animation controller pointer — the vehicle's counterpart to the human's `[human+0xd24]+0x10`. A byte at sub-object `+0x548` (= vehicle `+0xbd0`) gates both functions: "driver present" or similar; `0x00a76690` tests it twice (once directly on the vehicle, once again on the sub-object at the same absolute address, an apparent inlining artifact rather than two different conditions).

**`0x00a760e0` (300 instructions, CONFIRMED by full listing) — the parameter writer.** Gated on controller `+0x24` (same "has a managed state" guard as §10.1's `0x00b09ab0`). It writes, in order, through the **identical U0 API** — `0x004cbaf0` name→id lookup with the same per-name cached-id/guard-bitmask idiom `0x00958690` uses, `0x004b1410` (bool) and `0x004b1510` (float) setters:

| Name (string, CONFIRMED read) | Setter |
|---|---|
| `Helicopter Steer Vertical` | float |
| `Helicopter Steer Horizontal` | float |
| `Vehicle MPH` | float (from an int, `cvtsi2ss`) |
| `Airplane Mode Bool` | bool |
| `Vehicle Landed Bool` | bool |
| `Vehicle On` | bool |

— exactly the six names `review/scoping-animation.md` §1.3's writer table already lists for this address, now confirmed by direct string read at `0x1180784`/`0x1180768`/`0x118075c`/`0x1180748`/`0x1180734`/`0x1180728`.

The function ends by setting the controller's **playback-rate float** (`+0x2c`, via `0x004b1590`) to a value chosen by `0x00722000()` between the global variable-dt float `0x0153ba64` and the fixed 1/30 constant `0x0132a0ac` — **the identical dt-selection function and the identical two global dt sources** `review/scoping-animation.md` §1.6 records for the human poster `0x009a15f0`.

**One confirmed non-parameter side-effect inside the same function, unrelated to U0:** around the `Airplane Mode Bool` write, the function also flips bits `0x20`/`0x40` of a flags byte on a *different* object (obtained from a rig/skeleton lookup `0x00aefa00`, not a parameter id), and when a bit's value changes it fires a tagged call through `0x0045f5b0`/`0x00556750` with fixed hash-looking constants. This touches no control-parameter id and is not part of the U0 contract — it reads like a stat/telemetry or audio-cue trigger on a state transition (plausibly airplane-mode related, given the surrounding code), not traced further. **OPEN**, see §5.1 item 40.

**`0x00a76690` (CONFIRMED by full listing) — the vehicle's own per-frame driver**, the vehicle's structural counterpart to the human's `0x00994970`/`0x0095b1a0`+`0x004b3720` pairing, but doing more in one function:

1. Gates on the driver-present byte and on the controller's slot 0 being in-use (`controller+0x48 > 0`, slot array at `controller+0x4c`, slot `+0x80` bit 1) — all U1 controller fields, used exactly as specified there.
2. Calls `0x00a760e0` (above) on the resolved sub-object — confirmed by direct call-site read (`mov ecx, esi; call 0xa760e0`).
3. Calls **`0x004b3720`** — the literal same controller-update entry point `review/scoping-animation.md` §1.2/§1.4 already names for the human path. Not a vehicle-specific clone; the same function address.
4. Then, unlike the human poster, it does **not** go through the shared pose-request queue (`0x004bf4c0`/dispatch `0x004bf360`). It calls the per-step job dispatchers **directly and inline**: advance `0x004bcfb0`, sample `0x004bd0b0`, compose `0x004bd0f0` → `0x004c2170` → `0x004c31b0`/`0x004c36f0` — the same three dispatcher addresses the shape diagram already names, just invoked synchronously. The post-sample-adjust step (bit 4, `0x004cfb60`) is **not called at all** — consistent with that step being human-specific spine-bend/look work (§9.0 item 1).
5. **After compose**, a genuinely new, vehicle-only step with no human counterpart: it reads the rig binding descriptor's slot count `N` through the small validated-handle wrapper `0x004be910` (confirmed by listing: validates the instance handle, then returns `[[instance+0x3c]+0x4]` — exactly the "shared binding descriptor with its `N`" §1.6/`spec-rig-format.md` §11.16 already specify; not a new field). For up to `N` entries it copies and transposes a 4-row matrix via `0x00595d20` (confirmed by listing: pure SIMD row/column repacking, no branches) and re-orthonormalizes three row vectors with a Newton–Raphson `rsqrtps` refinement via `0x004c0230` (confirmed by listing: the `1.5`/`0.5` constant pair at `0x1249160`/`0x1249150` is the classic fast-inverse-sqrt refinement step, applied three times to re-orthogonalize what reads as a 3×3 rotation block). The source data is read per-entry from two vehicle-type-specific tables at **vehicle `+0x1d88`/`+0x1dac`** (0xFF-sentinel-terminated byte lists), through `0x004b4030` and a small lookup (`0x00595d20`'s caller builds the per-entry record from those tables, gated by the per-entry type byte). **This is the extra per-bone matrix work** (steering-wheel pose / wheel spin / suspension-driven bone offsets): the shape — re-orthonormalized per-bone override matrices, keyed by a small per-vehicle-type table, layered on *after* the shared compose step — fits a per-bone override such as wheel-spin or suspension offset. **Which bones, and what the matrix represents exactly, is not confirmed** — see §5.1 item 41.
6. `0x004b4030(controller)` is called again right after this overlay. Full listing shows it walks the controller's `0x84`-byte slot array (`+0x48`/`+0x4c`, exactly U1's layout), comparing each in-use slot's clip/instance id (`+0x04`) against the controller's current instance handle and that instance's own `+0x5c` field, and — when a mismatch/expiry is detected — zeroes controller `+0x198`/`+0x1a0`-region fields and loops again. Read as a **controller-level "clear a finished/invalidated slot" pass**; it is not previously named in the scoping note's shape diagram, and it is called identically by `0x008cb9f0` (`object_rig`, §10.4) in the same post-compose position. See §5.1 item 42.
7. The function's tail (after the matrix overlay) branches on two by-name-cached ids read through `0x00d9e8b0` (**not** `0x004cbaf0` — a different lookup/registry entirely) compared against a field on the vehicle's type-info block, and ends with a call to `0x00b1a020`, outside the animation library band. This reads as vehicle-type-conditional, non-animation bookkeeping (possibly physics- or VFX-adjacent); not pursued further, see §5.1 item 40.

### 10.4 — `object_rig` (`0x008cb9f0`, caller `0x008cbfe0`)

**Iteration.** `0x008cbfe0` (CONFIRMED by listing) walks a dedicated object-manager sub-list — count at `[0x03171a64+0x1a4]`, index array at `[+0x19c]`, resolved through the manager's object-pointer table at `[+0x58]` — and calls `0x008cb9f0(object)` once per live entry. This is a **distinct, separately-counted list** from the two lists walked by the clients in §10.5, which read counts/indices at different fixed offsets (`+0x1d4`/`+0x1cc` and `+0xb4`/`+0xac`) of the same manager object `0x03171a64`. So the central object manager keeps at least three separate typed sub-lists relevant to animation, of which `object_rig` is one.

**`0x008cb9f0` (239 instructions per the census; 241 printed here including prologue, CONFIRMED by full listing).** Fields used (all on `this`, confirmed by listing): `+0x9c` controller pointer, `+0xa4` instance handle, `+0x94` a part count, `+0x98` an array of part records (8 bytes each: a two-dword object handle), `+0xb0` a per-part skip-flag byte array, `+0x130` a status-flags byte, `+0x4c`/`+0x58`/`+0x64` three row vectors (a base rotation) and `+0x40` a base position.

Sequence:
1. Skip entirely if the controller (`+0x9c`) is null or status flag `+0x130` bit 0 is set.
2. Set playback rate to the **fixed** 1/30 constant (`0x0132a0ac`) via `0x004b1590` — **no `0x00722000` dt-selection call** here, unlike the vehicle parameter writer and the human poster. `object_rig` always ticks at the fixed cadence.
3. `0x004b3720(controller)` — the same controller-update entry point, again.
4. Validate the instance (`0x004bd580`/`0x004bd5d0` on `+0xa4`, the same "is valid" pair the vehicle driver uses); bail if invalid.
5. Advance (`0x004bcfb0`, inline, same dispatcher address).
6. A loop over the part count (`+0x94`) calling the small validated-instance wrapper `0x004bd4a0(instance, index+1, 1, 1)` once per part — `0x004bd4a0` (confirmed by listing) is itself a thin handle-validated wrapper tail-calling `0x004c2060`, inside U4's layer-sampler sub-band; read as per-layer binding/activation ahead of sampling, not re-derived past that shape (U4's territory).
7. Allocate the **8,192-byte stack buffer** via `0x00ea2ca0(0x2000)` — exactly the "128 bones × 64 bytes" buffer `review/scoping-animation.md` §1.6 already attributes to the general sampler; `object_rig` uses the identical size, corroborating that claim independently.
8. Sample (`0x004bd0b0`) and compose (`0x004bd0f0` → `0x004c2170` → `0x004c31b0`/`0x004c36f0`) — again the identical dispatchers.
9. `0x004b4030(controller)` — the same post-compose "clear a finished slot" pass §10.3 step 6 found in the vehicle path. Confirms it is shared, unmodified controller-internals, not vehicle- or object_rig-specific.
10. **Then a genuinely new, `object_rig`-only step with no counterpart in any human or vehicle code examined**: for each of the `+0x94` parts not flagged skip (`+0xb0` bit 0 per entry):
    - build a local transform from the base rotation/position fields (`+0x4c`/`+0x58`/`+0x64`/`+0x40`) via `0x00da38e0` (the same position-accessor the vehicle code also calls);
    - query the **posed bone transform for part `index+1`** on the instance through `0x004bd7f0`, and combine it with the local transform via `0x0056ca10` — i.e., read a named-bone pose out of the just-composed skeleton and combine it with the object_rig's own base offset;
    - resolve the part's actual game object from its stored two-dword handle (`[+0x98 + 8·index]`) via `0x00458230` with the fixed key pair `(0x24433a8, 0x31d152c)` — a general engine "resolve handle pair to live object" call, **not** animation-specific (the same function and the same key pair are called again, independently, by one of §10.5's clients and by an unrelated achievement-style check — see §10.5.2);
    - gate on the resolved object's per-class property table (`0x02cc9900`, indexed by a class-id byte at the object's `+0x34`) and an "is being destroyed" predicate `0x00853b10`;
    - then either feed the composed transform into physics (`0x00759ee0`/`0x00755f10`/`0x00755f90`, when the object has a cached physics body at `+0xb4` and status flag `+0x130` bit 1 is set), blend toward it over time (`0x00756350`, gated on a stored speed float via `0x00756790`), or — the fallback, no physics body — call a **virtual "set transform" method through the resolved object's own vtable** (`0x004f00f0` to prepare the matrix, then `mov edx,[edi]; mov edx,[edx+0x40]; call edx`), passing the composed world transform.
11. Clears status flag `+0x130` bit 1 at the end of the full part loop.

**Identification.** This resolves `review/scoping-animation.md` §1.6's own HYPOTHESIS ("it can instead copy a slot from another instance's matrix block... a child rig following a parent, such as clothing") and sharpens §1.2 step 5's HYPOTHESIS about the human's second instance handle at `+0xf80` ("a held prop or second rig"): **`object_rig` is the generic attached-part placement controller.** It drives its own small controller/instance through the identical U0–U4 plumbing (same controller-update, same advance/sample/compose dispatchers, same 8,192-byte pose buffer), then for each of up to `+0x94` named "parts" — resolved as real game objects via a two-dword handle, the same resolution mechanism the engine uses elsewhere for arbitrary object references — reads that part's bone transform out of the freshly composed pose and either drives the part's physics body toward it or calls a virtual placement method on the part object directly. This matches props, held items, and attached clothing/accessory pieces far better than "destructibles": the mechanism is "attach named game objects to named bones of a rig," independent of what kind of object is attached.

**The by-name writer dispatcher `0x008cc030`** (the other half of `review/scoping-animation.md` §1.3's writer-table row for `object_rig`): confirmed by listing to be a pure string-length switch (1/2/3/other characters) on a name argument, routing to `0x008cb380`/`0x008cb400`/`0x0064a1f0` respectively — i.e., a thin front-end that still bottoms out in U0's by-name lookup variant (`0x004b8d90`) and the same typed setters, just reached by name instead of a cached id. No new parameter mechanism; confirms the scoping note's existing attribution rather than changing it.

### 10.5 — The two unidentified pose clients

`review/scoping-animation.md` §1.0's shape diagram names three `[unidentified]` call chains in the world update; §4.2 item 8 groups them as "two" — `0x00b8fef0` alone, and `0x00900540`/`0x00921fa0` treated as one identification task. Investigation shows the latter two are **structurally related but come from two different outer callers walking two different object-manager lists** — the note's grouping is a reasonable simplification (both end in the same small generic helper shape), not a literal identity. Both are now identified below.

#### 10.5.1 — `0x00b8fef0` (75 instructions, CONFIRMED by full listing): the held-weapon pose driver

Gated on two predicates, `0x00bbdd90()` and `0x00905df0(-1)` (role not traced; feature/mode gates — if either is false the function is a no-op). Then:

- Walks an unrelated circular linked list at global `0x0130f434`, calling `0x00b8f580(node)` per node — a separate maintenance step, not traced further (not pose-related by shape: no controller/instance fields touched).
- Loops over a fixed-size global array: **count `[0x0290ac9c]`, base `0x0290ad10`, record stride `0x888` bytes, 5 sub-entries of 8 bytes at the start of each record** (outer loop = records, inner loop = 5 entries per record). Each 8-byte entry's first dword is used directly as a controller pointer — no object-handle resolution, so this is a dedicated fixed-capacity pool, not a generic object list.
- For each of the 5 entries whose instance validates (`0x004bd580`/`0x004bd5d0`, the same validity pair used throughout): appends a pose-request queue entry via **`0x004bf4c0`** (the literal U4 queue-append function) and fills it exactly per `review/scoping-animation.md` §1.6's field layout — `+0x0c` instance handle (via `0x00487e80`, the one-instruction getter), `+0x10` dt = **fixed 1/30** (`0x0132a0ac`), `+0x14` owner = 0, `+0x08` scratch block = **`0x0262cd60`** (the same global §1.6 calls out as the human poster's own scratch block — shared, not duplicated), `+0x04` bone cap = **`0x64` (100)**, not the human poster's `0x80` (128), `+0x18` step bits `|= 0xb` (advance+sample+compose, the same three bits, no post-sample-adjust).
- Ends with a **tail-jump to `0x004bf360`** — the identical dispatch/flush function the per-human loop calls after its own pass — so this client, like the human path, posts then flushes its own requests before the rest of the frame runs, matching the shape diagram's label exactly.

**Identification.** The weapon/item function band is `0x00b8xxxx`–`0x00b9xxxx` throughout the rest of the project (`spec-ai-runtime.md`'s `0x00b81440`/`0x00b81850`/`0x00b869b0`/`0x00b88610`; `spec-player-runtime.md`'s `0x00b85fa0`, whose own §6.11 item 6 and §8.4 record that it "gives an animation-set id... applied to the animation controller `0x004b2140`, a second time when w `+0xe8` is non-zero" for "the human's **and the weapon's** rigs" — i.e., held weapons are already known to carry their own separate rig/controller). `0x00b8fef0` sits in that same address range and does exactly what that separate weapon rig needs each frame: advance/sample/compose up to 5 weapon-slot controllers per record, independent of the holder's own per-human pose step. **Identified: the held/carried-weapon animation pose driver** — each weapon slot gets its own small posed rig, through the shared queue mechanism, with its own fixed bone cap (100) and fixed dt (1/30).

#### 10.5.2 — `0x00900990`→`0x00900540` and `0x00929da0`→`0x009296a0`→`0x00921fa0`: a generic "secondary rig" helper, used by two different object categories

**The common inner mechanism, `0x00921fa0` (70 instructions, CONFIRMED by full listing — exact match to the shape diagram's count).** A minimal, generic helper: reads an animation component pointer from **`self+0x1f4`**; if null or its `+0xc` field is zero, no-op. Otherwise: fixed-1/30 playback rate (`0x004b1590`), controller update (`0x004b3720`), instance-valid check (`0x004bd580`), a per-count loop calling `0x004bd4a0(instance, index+1, 1, 1)` (same wrapper `object_rig` uses, count read from `[[component]+0x10]`'s own `+0x50` field), then **appends a pose-request queue entry** via `0x004bf4c0` with the same field layout as §10.5.1 (instance, dt = fixed 1/30, owner 0, scratch `0x0262cd60`, bone cap `0x64`=100, step bits `0xb`), and tail-jumps to **`0x004bf550`** (not `0x004bf360` — this one does not flush the queue itself; the caller's list-walk does that once, after the whole list, see below). This is the cleanest example found of "reuses U0–U4 unchanged": no extra per-bone matrix work, no per-part attachment resolution, just the standard calls with the standard fields.

`0x00921fa0` is called conditionally (only when `self+0x1f4` is non-null) from **`0x009296a0`** — a much larger function (160+ instructions, not fully characterized: it is mostly *not* animation) that performs a generic per-frame physics-to-game-object transform sync for its `this` (a `0x00853b10`/`0x008cc4d0` liveness check, a physics-body query `0x007573b0`, a virtual "set transform" call through the object's own vtable `+0x40` — the same vtable-dispatch idiom `object_rig` uses for its attached parts — angle/quaternion extraction via `0x00da1330`, and several further non-animation calls `0x008addb0`/`0x00923180`/`0x00929450`/`0x00922ef0`/`0x00926b60`). **The animation step is one small optional branch of a much larger, mostly physics-driven per-object update; the object category itself was not pinned down** — see §5.1 item 46. `0x009296a0` is in turn called once per entry of a list walked by **`0x00929da0`** (count `[0x03171a64+0xb4]`, index array `[+0xac]`), which after the loop also runs an unrelated one-shot achievement/telemetry check keyed off the **local player's current vehicle's class property table** (`0x009da4e0` → `+0x16c0`/`+0x16c4` handle → `0x00458230` resolve → `0x02cc9900` class table bit `0x80` → `0x00558fd0`/`0x0045f5b0`/`0x0045ea70` event calls) — this tail is unrelated to animation and should not be read as identifying the list's own object type; it just happens to be scheduled at the same call site.

**The richer variant, `0x00900540` (316 instructions, CONFIRMED by full listing — exact match to the shape diagram's count), called from `0x00900990` (72 instructions printed; the shape diagram's "71" is prologue-padding-level noise, same function).** `0x00900990` walks a *third* object-manager list (count `[0x03171a64+0x1d4]`, index array `[+0x1cc]`) and calls `0x00900540(object, flag=0, dt=1/30)` per entry unless the object's `+0x33` byte has bit 2 set (in which case it only clears a flag on it and skips the pose call); afterward it tail-jumps to **`0x004bf360`** (the same flush function §10.5.1 uses — so, unlike `0x00921fa0`'s caller chain, this list *does* flush its own posted requests).

`0x00900540` uses the component shape **`{+0x10: controller, +0x14: instance}` at `self+0xe8`** — the same two-field layout as the human's `+0xd24` component — and, beyond `0x00921fa0`'s plain sequence (same controller-update, same advance/sample/compose dispatchers with the shared-flush variant of the pose-request fields, same `0x004bd130`/`0x004bcff0` pair for a post-sample bookkeeping step), adds a **target-resolution step with no counterpart in `0x00921fa0`**: it reads a two-dword handle at `self+0x10`/`self+0x14`, resolves it via the same `0x00458230`/`(0x24433a8, 0x31d152c)` call `object_rig` uses, gates it on the resolved object's `+0x33`/class-table bits and the "being destroyed" predicate `0x00853b10`, and runs a fresh predicate `0x009424c0` on it to decide a "skip this frame's pose" flag (`self+0x3a` bit `0x10`, tested right before the controller-update call). When the resolved target has a **`+0xd24` field with its own `+0xc` byte set — i.e., the target is specifically a human, using the human's own known animation-component offset** — it calls **`0x009b9160`**, the exact bike-riding gate `review/scoping-animation.md` §1.3's writer table already cites for the rider-lean writer `0x00af4b80`, and uses the result to set (or, when false, zero) a float at a third pointer's (`self+0xec`) `+0x54` offset, via a short division/clamp against fixed constants (`0x0112a2d70`/`0x11169c0`/`0x12a2dc0`). **This computation borders Player U6.8's in-flight territory (the rider-lean writer itself), so it was traced only far enough to identify the shape and the shared gate function — the lean-value semantics were not re-derived here and should be left to U6.8** (§5.1 item 47).

**Identification.** Given the shared-component shape, the cross-reference to the bike-riding gate, and the explicit requirement that the linked target be a human: `0x00900540`/`0x00900990`'s list is most consistent with **a motorcycle/two-wheeler-linked secondary rig** — plausibly the handlebar/front-fork assembly or an equivalent small rider-linked part that needs to be posed in sync with the rider's lean state, tracked in its own object-manager list distinct from both `object_rig` and the vehicle body controller of §10.3. `0x00921fa0`'s list (via `0x009296a0`/`0x00929da0`) is the same generic mechanism applied to some other, unidentified physics-synced object category whose only common thread with the bike case is "optionally carries a small secondary rig at a fixed self-offset" — identified only as "a client of the same generic helper," not as a named object type. Both conclusions are **HIGH, not CONFIRMED**, for the object categories themselves; the mechanism each uses is CONFIRMED by listing.

### 10.6 — Verdict: does "reuses U0–U4 unchanged" hold?

**For the literal function surface: yes, cleanly.** Every client examined — the vehicle, `object_rig`, the weapon driver, and both generic secondary-rig helpers — calls the exact same addresses U0–U4 already specify, with no caller-specific branch found inside any of them: `0x004cbaf0` (name→id), `0x004b1410`/`0x004b1510`/`0x004b1590` (setters), `0x004b3720` (controller update), `0x004bcfb0`/`0x004bd0b0`/`0x004bd0f0` (advance/sample/compose dispatchers), `0x004bf4c0`/`0x004bf360` (queue append/flush), and the controller/instance field layout U1/§1.6 already record (`+0x24`, `+0x48`/`+0x4c` slots, `+0x3c` binding descriptor, the `0x3a0`-byte instance pool). No client re-implements, shadows, or special-cases any of these.

**But "unchanged" undersells how much bespoke work two of the five clients layer around that shared core:**

1. **Two call conventions, not one.** The vehicle and `object_rig` call the advance/sample/compose dispatchers **directly, inline, every frame**; the weapon driver and both generic helpers **post through the shared queue and flush it**, the same way the human path does. Both conventions reuse the same underlying functions, but a port needs to reproduce both shapes, not just one.
2. **Bone cap is not a constant 128 everywhere.** The human poster's queue entries use `0x80` (128, per §1.6); the weapon driver and both generic helpers use `0x64` (100); `object_rig` allocates the full `0x2000`-byte (128-bone) buffer like the general sampler. A port that hard-codes 128 bones for every pose request will over-allocate for three of these five clients.
3. **dt handling is inconsistent.** `object_rig`'s playback rate is always fixed 1/30 with no `0x00722000` selection; the vehicle's parameter writer and `0x00900540`'s generic helper both select between fixed and variable dt via `0x00722000`, the same selector the human poster uses; the vehicle's own per-frame driver instead calls a **different**, unexamined sibling (`0x00722130`) for that choice. Not a single uniform rule.
4. **Two clients add genuinely new post-compose behavior with no human-path counterpart at all:** the vehicle's per-bone matrix transpose/re-orthonormalization overlay sourced from vehicle-type tables (§10.3 step 5), and `object_rig`'s per-part attached-object resolution and placement (§10.4 step 10). Neither is a thin wrapper; each is real, additional logic (on the order of 60–150 instructions) built on top of the shared pipeline, not a parameterization of it.
5. **A shared controller-internal step not in the original shape diagram:** `0x004b4030`, a slot-array "clear a finished/expired entry" pass, is called identically by the vehicle and `object_rig` right after compose — see §5.1 item 42.
6. **Non-animation side logic rides along inside otherwise-animation functions**, which a cursory read could mistake for part of the contract: the vehicle parameter writer's tagged-event calls around the `Airplane Mode Bool` write (§10.3), and the achievement/telemetry tail appended to `0x00929da0`'s list-walk (§10.5.2). Neither touches a control-parameter id or the instance/controller API.

**So: "small" is accurate** — the additions beyond the shared plumbing are each on the order of a few dozen to a couple hundred instructions, well within the scoping note's own size estimate (~2.9k own instructions across the eight roots). **"Unchanged" is accurate for the shared API surface and inaccurate as a description of overall behavior for two of the five clients examined (vehicle, `object_rig`), which carry real, non-trivial additions; it is a good description of the weapon driver and both generic secondary-rig helpers, which really are thin, uniform wrappers around the shared pipeline.**

**Cross-references into already-specified material (not re-derived, only cited):**
- `spec-player-runtime.md` §8.5 (U6.4) for `0x00b09ab0`'s surrounding ride-state logic.
- `spec-player-runtime.md` §6.11 item 6, §8.4 for the weapon-rig cross-reference supporting §10.5.1's identification.
- `review/scoping-animation.md` §1.3 (writer table), §1.4 (controller fields), §1.6 (pose-request field layout, instance pool, binding descriptor) for every U0–U4 address and field cited above by number.
- `spec-rig-format.md` §11.16 for the binding descriptor and instance matrix block that `0x004be910`/`0x004c36f0` touch.

---

## 11. U4 — Time, layers and the pose hand-off

**Scope note.** This unit's original brief (`review/scoping-animation.md` §5, "U4 — Time, layers and the pose hand-off") named the pose-request queue and job steps, the `+0xcd2` human-poster mode table, time advance `0x004c4810` with `0x004c3d20`/`0x004c45a0`, and compose `0x004c31b0`/`0x004c36f0`, at roughly 5k instructions. Most of the *time-advance* half of that brief — the 4,800-tick time base, the split on layer flag bit `0x01` into `0x004c3d20`'s shared phase versus `0x004c45a0`'s independent clocks, the instance's inline layer array layout, and the resolution of root motion versus velocity — was already read in full as a side effect of `spec-player-runtime.md` §5.4.3 ("Step 3 in detail", dated 2026-10-03) and is **cited below, not re-derived**. This section's own contribution is the parts that account genuinely remained open: the pose-request queue and its job steps, the full `+0xcd2` mode table (all six table entries plus mode 0), the compose pipeline's exact mechanics, and several `spec-anim-runtime.md` §5.1 OPEN items (14, 25, 31) that turned out to belong to this same unit.

Every function below was read in full by direct capstone disassembly of the shipped executable (`tools/harnesses/exe_va.py`), not structurally. Confidence labels follow the project's convention: CONFIRMED (read in full), HIGH (strong structural inference), HYPOTHESIS (plausible, unchecked), OPEN (collected in §11.10).

### 11.0 What is cited, not repeated

- **Time advance** (`0x004c4810`, `0x004c3d20`, `0x004c45a0`), **the layer-flag-`0x01` phase-synchronisation split**, the instance's **inline layer array** (count `+0x58`, records at `+0x70` — not `+0x74`, see §11.7 — stride `0x50`, fields `+0x04` clip index, `+0x08`/`+0x0c` previous/current time in 4,800 Hz ticks, `+0x10` flags, `+0x14` group, `+0x24` effective weight), the shared normalised phase at instance `+0x60` and its rate at `+0x68`, and the full root-motion-versus-velocity resolution: all `spec-player-runtime.md` §5.4.3 "Step 3 in detail", citing `0x004be240`/`0x004c58c0`. Not re-read here.
- **The `0x3a0`-byte animation-instance pool** (`0x0344c924`, count `0x0344c91a`, self-index at `+0x50`), **the shared binding descriptor** at instance `+0x3c` with its slot count at `+0x04` and remap table at `+0x10`, and the **rig holder** at instance `+0x14`: `spec-rig-format.md` §11.16.6, confirmed again here as a side effect of reading the compose functions.
- **The layer sampler** `0x004c21a0` and clip decode: `spec-anim-format.md` §6c/§10.
- **The controller's animation-instance handle is `[[human+0xd24]+0x10]+0x20`.** `spec-anim-runtime.md` (table of field sources, e.g. row for `0x00958690`) already identifies `[[human+0xd24]+0x10]` as **"the controller"** itself. This section adds, by full listing of `0x00487e80` (`mov eax,[ecx+0x20]; ret` — 2 instructions, CONFIRMED), that **every** generic "resolve to the controller's instance handle" call site in the pose-posting path is exactly `0x00487e80(controller)`, i.e. a read of controller `+0x20`. This is the *same* `0x00487e80` that `spec-player-runtime.md` §5.4.3 cites for `h` in `0x004be240`. See §11.4 for why this matters for the `+0x14` question.

### 11.1 The pose-request queue: `0x004bf4c0` (append), full listing

**`0x004bf4c0()` (15 instructions, CONFIRMED by full listing, matches census)** appends one entry to the queue at `0x02eefae0` (count `0x03171be0`):

```
eax = count (0x03171be0)
slot = 0x02eefae0 + eax*0x1c        ; stride confirmed = 0x1c = 28 bytes
zero slot[0x00..0x17] (two movq) and slot[0x18] (dword)   ; whole 28-byte entry zeroed
count = eax + 1                                           ; stored back to 0x03171be0
return &slot                                              ; = 0x2eefac4 + (count_new)*0x1c, algebraically the same address as `slot` above
```

This confirms `spec-anim-runtime.md`/scoping's existing reading exactly, now from a full instruction-level trace rather than a structural one: the entry is **always fully zeroed** on append, and the function **returns a pointer to the zeroed entry** for the caller to fill in. Field layout (confirmed below by the mode-table bodies that fill it in, §11.3): `+0x00` target object, `+0x04` bone cap, `+0x08` scratch/global block pointer, `+0x0c` instance handle, `+0x10` dt (float), `+0x14` owner, `+0x18` step bits (byte-tested, dword-sized).

### 11.2 Dispatch and execution: `0x004bf360`, `0x004bf350`, `0x004bf270`, full listings

**`0x004bf360()` (91 instructions, CONFIRMED by full listing, matches census).**

1. One-time scheduler job-group setup, cached in the static `0x02edc994` (calls `0x00dc2160` once; harmless on later calls).
2. An indirect call through the global function-pointer slot `0x0101c14c` obtains the calling thread's job-scheduler context (its result is reused for every job submitted by this call — infrastructure, not pose-contract-relevant).
3. **First loop**, over queue indices `[dispatched-up-to `0x03171be4`, count `0x03171be0`)`: for each entry whose **step-bits byte (`+0x18`) bit `0x10` is CLEAR**, resolve the entry's instance handle (`+0x0c`) to a `0x3a0`-byte record through the usual pool/self-index check, OR that record's `+0x6c` byte with `0x02` ("pose in flight" — matches scoping), then **submit a scheduler job** (`0x00dc21f0`, job body `0x004bf350`) carrying a pointer to the *start* of the entry (`entry_ptr`, not the handle). Entries whose bit `0x10` **is set** are skipped in this loop (deferred to the second loop, run inline).
   - **If the resolved handle is `-1`** (no instance requested), `eax` is forced to `0` and the subsequent `or byte [eax+0x6c], 2` would address near-null memory. This path is unreachable in practice for a freshly-zeroed entry whose caller never explicitly wrote the handle field to `-1` (the zeroed default is `0`, not `-1`, which instead fails the *pool-bounds/self-index* check harmlessly). It is dead/defensive code as read — flagged, not chased further (§11.10).
4. **Second loop**, over the *same* range: for each entry whose **step-bits byte `+0x18` bit `0x10` IS set**, call `0x004bf270(entry_ptr)` **directly** (inline, synchronous, `thiscall` with `ecx = entry_ptr`).
5. `0x03171be4` (dispatched-up-to) is set to the current count — the queue's "already-handled" watermark advances to cover every entry processed this call, whichever loop handled it.

**Conclusion, confirmed by full listing (not previously spelled out):** **step-bits bit `0x10` is "run this entry's steps synchronously on the calling thread instead of as a background scheduler job."** Clear = background job via `0x00dc21f0`/`0x004bf350`; set = inline call. This matches scoping's §1.6 wording exactly ("submits every entry without bit `0x10` as a scheduler job ... and runs the rest inline") — now CONFIRMED by full trace rather than structural read.

**`0x004bf350` (2 instructions, CONFIRMED):** `mov ecx, [esp+8]; jmp 0x4bf270` — the scheduler job body is a pure tail-jump into `0x004bf270`, unpacking the job's payload pointer (which `0x004bf360` set to `entry_ptr`) into `ecx`. So the job and the inline path share *exactly* the same code.

**`0x004bf270(entry_ptr)` (81 instructions, CONFIRMED by full listing, matches census).** `thiscall`, `ecx = entry_ptr` (not the handle — the entry's own address). Stack-cookie prologue; `esi = ecx` (the entry); if `esi` is null, skip to epilogue (no-op). Otherwise:

- Allocates an **8,192-byte buffer on its own stack via `_alloca`-style probe** (`mov eax,0x2000; call 0xea2ca0; mov edi,esp`) — `edi` = this job's **local pose-sample scratch buffer**, 128 bones × 64 bytes, matching the `0x80`-bone cap the human poster writes at entry `+0x04` and matching scoping's existing reading.
- Tests **entry `+0x18` byte**, one bit at a time, each gating one step:
  - **bit `0x01` (advance):** `0x004bcfb0(handle=entry+0xc, dt=entry+0x10)`.
  - **bit `0x02` (sample):** `0x004bd0b0(handle=entry+0xc, buffer=edi, block=entry+0x8, owner=entry+0x14)` — **confirms the local buffer `edi` is the sample target**, not the instance's own storage.
  - **bit `0x04` (post-sample adjust):** `0x004cfb60(this=entry+0x00 [the target object], buffer=edi)` — **confirms the post-sample adjuster (`spec-anim-runtime.md` §9/U5) consumes the *same* local per-job buffer that sampling just wrote**, not the instance's committed layer data. This is a structurally different data path from the root-motion layer walk `0x004c58c0` that `spec-player-runtime.md` §5.4.3 reads later in the frame (that one reads the *instance's own layer array* through the composer, after this job has already committed its result) — a boundary note for U5, not a contradiction.
  - **bit `0x08` (compose):** `0x004bd0f0(handle=entry+0xc, buffer=edi, block=entry+0x8, owner=entry+0x14)` — **the same four-argument signature as the sample call**, confirming compose reads from the identical local buffer that sampling just filled.
- After running whichever steps were requested, it re-resolves the handle one more time and, on success, **sets record `+0x6c` bit `0x08` ("pose ready") and clears bit `0x02` ("pose in flight")** — CONFIRMED now by full listing (scoping already read this correctly from a partial view).

**Thin per-bit wrappers (all CONFIRMED by full listing, ~20–22 instructions each):** `0x004bcfb0(handle, dt)` resolves the handle and calls `0x004c4810(record, dt, 1)`; `0x004bd0b0(handle, buffer, block, owner)` resolves and calls `0x004c21a0(record, buffer, owner, block)`; `0x004bd0f0(handle, buffer, block, owner)` resolves and calls `0x004c2170(record)` with `buffer`/`block`/`owner` passed through in registers (`edx`/`edi`/`esi` respectively) rather than on the stack — see §11.5. All three share the identical pool-lookup/self-index validation idiom used throughout the library; an invalid handle is simply a no-op.

### 11.3 The human poster `0x009a15f0` and the six-way table `0x009a1bd0`, full listing

**`0x009a15f0(human)` (CONFIRMED by full listing of every reachable branch).**

**Mode-independent prefix** (runs for every `+0xcd2` value, including 0):

1. Caches `human` at the global `0x0262cf6c`, then calls `0x009a1290` (not traced — unrelated bookkeeping) and `0x00720640()` (a bool).
2. **Sets a per-instance manual-position override, new finding not in scoping.** Resolves via `[human+0xd24]+0x10` as the `0x00487e80` argument (see §11.4 for the exact chain), then calls **`0x004bf1c0(handle, &vec)`**, where `vec` is either the fixed constant `(0,0,0,1)`-shaped quad at `0x01249140` (when `0x00720640()` was true) or a vector splatted from `[human+0xd24]+0x18` (when false).
   - **`0x004bf1c0(handle, vec_ptr)` (22 instructions, CONFIRMED by full listing):** resolves `handle` to the `0x3a0`-byte record; on success, **sets record byte `+0x6e` to `1`** and **copies the 16-byte `vec` into record `+0x350`**.
   - **This is exactly what compose (`0x004c31b0`, §11.5) reads back**: `0x004c31b0` tests record `+0x6e` and, when set, feeds record `+0x350` into `0x004c03d0`/`0x004c0170`. So **every frame, before any pose request is queued, the human poster stages a position/orientation override on the controller's own instance that compose applies unconditionally** (not gated by `+0xcd2`). The exact effect of `0x004c03d0`/`0x004c0170` was not traced past their call shape; HYPOTHESIS: a root-lock or camera-relative position stamp, given the vector source (either a fixed identity-ish constant or a field read directly off the holder struct at `[human+0xd24]+0x18`).
3. Resolves `h = 0x00487e80([human+0xd24]+0x10)`, then calls `0x004bf1c0(h)` — the instance handle fed to the override setter above and the "is this instance alive" probes below are drawn from the same `[+0xd24]+0x10 → controller → +0x20` chain, confirmed identical across both uses by listing.
4. Picks **dt**: `0x00722000()` selects between the global dt `0x0153ba64` and the fixed `1/30 s` constant `0x0132a0ac` — exactly as scoping already read.
5. **Clears `human+0xe4` bit `0x100`** (CONFIRMED; this is the *same* "post-animation update done" bit `spec-player-runtime.md` §5.4.1 already names as cleared "by the animation job's `0x009a15f0`" — direct cross-confirmation between the two independently-read specs) **and clears `human+0xdc` bit `0x04`** (new finding; re-armed by most of the modes below before the function returns — HYPOTHESIS: "a pose request was posted this frame").
6. Reads the mode byte `al = human+0xcd2`.

**Mode 0** (`al == 0`, branches directly, not through the jump table): body at `0x009a1860`.

- Appends a queue entry via `0x004bf4c0`. **Entry `+0x0c` (handle) = `[[human+0xd24]+0x14]`** — a *direct* field read, with **no** pass through `0x00487e80`/controller `+0x20`. This literally confirms scoping's original §1.6 text for mode 0 ("posts an advance on the instance handle at `[human+0xd24]+0x14`"). Entry `+0x10` (dt) = the dt chosen in the prefix. Step bits start with bit `0x01` (advance) unconditionally set.
- Calls `0x004bf150([[human+0xd24]+0x14])` (role below, §11.4) and `0x004bd620([human+0xd24]+0x14)` (clears record `+0x6c` bit `0x08` when the handle resolves — a "pose not ready yet" reset; not otherwise traced) and `0x009564a0(human)` (not traced).
- Tests `human+0xdc` bit `0x02`. **If clear:** only toggles a couple of minor flags (`human+0xe8` bit `0x02`, `human+0xac0 = 0x32`) and jumps straight to the shared finisher — **the posted entry keeps only step bit `0x01` (advance-only)**, matching scoping's "when `+0xdc` bit `0x02`" gate exactly.
- **If set:** resolves the *controller's own* instance handle (`0x00487e80([[human+0xd24]+0x10])`) and gates on its readiness (`0x004bd580`, §11.4) and on `human+0xe8` bit `0x02` being clear. If either fails, same "advance-only" exit. If both pass: tests `human+0xe4` bit `0x01`; if set, calls `0x0096f4f0(human)` — **the "dead or gone" predicate `HANDOFF.md` §34.5/`spec-lua-api-behaviour.md` §34.5 already names** (null, or `+0xcc8==5`, or `+0xe3` bit `0x01`) — and skips an aim/IK block (vtable call through `[[human]+0x44]`, `0x00da4130`/`0x00da52a0`/`0x009532a0`) if the human is dead/gone. This aim/IK block and the double-precision turn-damping math that follows it (reading `human+0x1ac0`/`+0x1abc`/`+0x1ac4` against a fixed threshold at `0x012a2fb8`) are **out of this unit's scope** — they belong with the aim/look-at family (§12, `0x009b4360` and kin) and were not traced further.
- **The shared finisher** (reached by every successful mode-0/3/6 path, at `0x009a1b5d`–`0x009a1bb8`): calls `0x00999d00(human)` (not traced); sets `human+0xdc` bit `0x04`; **sets entry `+0x14` (owner) = `human+0xac8`, `+0x08` (block) = the global `0x0262cd60`, `+0x04` (bone cap) = `0x80`** — all three exactly as scoping already read structurally, now CONFIRMED by full listing and **common to modes 0, 3 and 6** (not duplicated per mode). Then computes the **post-sample step bit (`0x04`) via a branchless formula keyed on bit `0x1000` of the 16-bit field `[[human+0xf0]]+0x48`** (a rig-level flag word: `human+0xf0` is the rig pointer `spec-player-runtime.md` §5.4.2 also reads via `+0xf0`), replacing entry `+0x18` wholesale with the toggled value — **this is scoping's "bit `0x08` is gated on a rig flag" claim, now CONFIRMED (not merely HIGH) and precisely located**, though the exact polarity (does the rig bit *enable* or *suppress* post-sample) was not resolved algebraically with full certainty from the listing alone (§11.10). Finally sets **entry `+0x00` (target object) = `human+0xea0`** (confirms scoping exactly) and unconditionally **ORs in step bit `0x08` (compose)**, then calls **`0x004bf550` — a one-instruction stub (`ret`) — currently a dead hook**, and returns.

**Modes 1 and 2** (`al ∈ {1,2}`, table entries 0 and 1 — **identical code**, body at `0x009a173e`, 28 instructions): resolve the controller's instance handle (`0x00487e80([[human+0xd24]+0x10])`) and gate on readiness `0x004bd580`; if not ready, bail with no queue interaction at all. If ready: call **`0x0077b960(human)`**; if it returns false, additionally call **`0x0077b910(human)`** then **`0x007800d0(human)`**. Finally set `human+0xdc` bit `0x04` and return. **No call to `0x004bf4c0` anywhere in this path** — modes 1 and 2 never touch the pose-request queue; they are pure calls into the physics band, matching scoping's attribution to `review/scoping-physics-vehicle.md` but now precisely: it is these two functions (`0x0077b960`, and conditionally `0x0077b910`+`0x007800d0`), not a queue post.

**Mode 4** (`al == 4`, table entry 3, body at `0x009a16c2`, 95 instructions including the shared epilogue): **always** calls **`0x0077c910(human)`**; if true, applies an impulse via **`0x009a48e0(human, 0, 0, const@0x1174218, const@0x117421c, 0,0,0,0,0)`** — the *same* `0x009a48e0` `spec-player-runtime.md` §5.8 already documents as the knock-down/ragdoll-impulse function, here called with two fixed constants from the image rather than a per-contact value (HYPOTHESIS: a scripted fall/impact force distinct from the contact-replay path). Then resolves the controller's instance handle and gates on readiness (`0x004bd580`) exactly as modes 1/2; if ready, calls **`0x00783a90(human, [human+0xd24])`** — passing the raw holder pointer itself, not a resolved handle. Sets `human+0xdc` bit `0x04` and returns. **Also no queue interaction.**

**Mode 5** (`al == 5`, table entry 4) **and any out-of-range value** (`al > 6`, unsigned bounds check `ja`) **share the identical target, `0x009a1bb8`: the function's own epilogue, nine instructions, no body at all.** This is the literal, CONFIRMED answer to "mode 5 posts nothing — why": **mode 5 is wired to the same code as the switch's own default/invalid-mode fallback.** It performs none of the mode-independent prefix's follow-on work beyond what already ran (dt selection, the `+0x6e`/`+0x350` override stash, the `+0xe4`/`+0xdc` flag clears) and does not touch the queue, the readiness tests, or `human+0xdc` bit `0x04`.

**Modes 3 and 6** (`al ∈ {3,6}`, table entries 2 and 5 — **identical code**, body at `0x009a1795`): the mechanics scoping already summarised as "advance the controller's own instance", now fully confirmed:

- Appends a queue entry via `0x004bf4c0`. **Entry `+0x0c` (handle) = `0x00487e80([[human+0xd24]+0x10])` — the controller's own instance handle**, read fresh *twice* (once before, once after an intervening call) and confirmed identical both times. Entry `+0x10` = dt. Step bit `0x01` set unconditionally.
- Calls `0x004bf150(handle)` (§11.4), `0x004bd620([human+0xd24]+0x14)` and `0x009564a0(human)` — the same auxiliary calls as mode 0, but note **`0x004bd620` here still reads the holder's `+0x14` field**, not the controller handle — this field is read for this side-effect call in every one of modes 0/3/6, even though only mode 0 uses it as the *queue entry's* handle.
- Tests `human+0xdc` bit `0x02`; if clear, exits with advance-only (same as mode 0). If set: resolves the controller handle again, gates on `0x004bd580` readiness; if it fails, same advance-only exit. If it passes: sets `human+0xdc` bit `0x04`, calls `0x00999d00(human)`, **ORs entry `+0x18` with `0x02` (sample)** — then falls into the **same shared finisher** described under mode 0 (`0x009a1b5d` region, owner/block/bone-cap/target-object/bit-0x04-rig-gate/bit-0x08/`0x004bf550`-stub), i.e. **modes 0, 3 and 6 converge on identical finishing code once their respective readiness gates pass.**

### 11.4 Resolving the `+0x10` / `+0x14` OPEN item

`review/scoping-animation.md` §1.6/§4.2 left open "whether the human's instance handle at `[human+0xd24]+0x14` is the controller's own instance or a separate output instance." Having read every mode's body in full, the precise answer is:

- **`[human+0xd24]` is a pointer to a small holder struct** (`spec-anim-runtime.md`'s own field-source table already names `[[human+0xd24]+0x10]` as **"the controller"** itself, confirmed again here).
- **Holder `+0x10` = the controller pointer.** The controller's own animation-instance handle is `controller+0x20`, read uniformly through the one-line accessor `0x00487e80(controller)` (`mov eax,[ecx+0x20]; ret`). **This is the handle used by the queue entries of modes 3 and 6**, and by every readiness/override probe in the mode-independent prefix and in modes 1/2/4.
- **Holder `+0x14` is a *different*, directly-stored field of the holder struct — not reached through the controller at all.** It is fed, unresolved, straight into `0x004bf150` and `0x004bd620`, both of which perform their *own* independent pool/self-index validation on whatever value they are given — exactly the same validation every other handle-taking function in this library performs. Since `0x004bf150`/`0x004bd620` accept the controller's `+0x20` handle *just as readily* when modes 3/6 pass it, **`+0x14` must itself be a valid handle into the same `0x0344c924` pool**, structurally independent of the controller chain. **This is the handle mode 0's queue entry actually uses** (`entry+0xc = [[human+0xd24]+0x14]`, confirmed above) — exactly as scoping's original text described, but *only* for mode 0.
- **So the honest answer is mode-dependent and the two handles are structurally separate fields, confirmed by full listing**: mode 0 posts against holder `+0x14`; modes 3 and 6 post against the controller's own `+0x20` (reached via holder `+0x10`). Whether holder `+0x14` and controller `+0x20` ever hold the *same numeric value* at runtime cannot be settled from a static read (§11.10).

**`0x004bf150(handle)` (22 instructions, CONFIRMED by full listing) — a lazy-allocate, not a simple probe.** Resolves `handle` to the record; if valid **and record `+0x348` is still null**, it asks a **global factory object at `0x034a79d0`** for an allocation (two virtual calls through that object's vtable, slots `+0xc` and `+0x10`, with `0x004c8240()` run in between) and **stores the result at record `+0x348`**. `record+0x348` is read elsewhere (`0x004c31b0`, §11.5) as the base of a `0x4c`-byte-stride (76-byte) array, indexed by a small per-bone count read from the per-slot flag array. **HYPOTHESIS: a lazily-created per-bone constraint/IK-chain array**, consumed by the compose-time helper `0x004c4970` (§11.6). Not fully traced (§11.10).

**`0x004bd580(handle)` and `0x004bd620(handle)` (CONFIRMED by full listing, ~12–20 instructions each).** `0x004bd580` resolves the handle and returns a bool from `0x004c79a0`-adjacent record state when the handle is `-1` (a different, non-pool-indexed check) or the resolved record's own live flag otherwise — the **"is this instance ready" gate** every mode uses before sampling/composing. `0x004bd620` resolves the handle and, on success, **clears record `+0x6c` bit `0x08`** (un-sets "pose ready") — a reset called unconditionally in the mode-independent-prefix-adjacent code of modes 0/3/6, before that frame's new request is posted.

### 11.5 Compose: `0x004c2170` → `0x004c31b0` / `0x004c36f0`, full listing

**`0x004c2170(record)` (18 instructions, CONFIRMED by full listing, matches census).** `ebx = [ebp+8]` (the record, the only true stack argument). Calls **`0x004c31b0(record)`** first, then **`0x004c36f0(record, esi, edi)`** (`thiscall`, `ecx = record`). The registers `esi`/`edi`/`edx` that `0x004bd0f0` set up (`buffer`, `block`, `owner`, see §11.2) are never touched by `0x004c2170` itself and remain live across both calls — **and, by a legitimate (if unusual) compiler artefact, `0x004c31b0`'s own "stack" parameters at `[ebp+0xc]`/`[ebp+0x10]` turn out to be exactly `0x004c2170`'s own callee-saved copies of `block`/`owner`** (pushed by `0x004c2170` purely to preserve its registers, and happening to land at the right stack offsets for `0x004c31b0` to read as if they were pushed arguments). **Net signatures, confirmed:**
- `0x004c31b0(record, block, owner)` with `buffer` live in `edx`.
- `0x004c36f0(record, block, owner)` (`thiscall`) with no buffer argument — it never reads one.

**`0x004c31b0(record, block, owner, edx=buffer)` (361 instructions, CONFIRMED by full listing).** Per-slot setup/working pass, **not** itself the final model-space composition (refining scoping's HYPOTHESIS "local-to-model composition"):

1. **Attachment ordering gate.** Resolves record `+0x30` (a handle into the same `0x3a0`-byte pool) exactly like every other handle field in this library. **If `+0x30` is a valid handle to another instance *and that instance's own byte `+0x38` is still `0`*, the entire function returns immediately, doing nothing.** `+0x38` is set to `1` at the very end of this same function's main loop (see point 5) — so **`+0x38` is a per-instance "composed-this-frame" flag**, and **this is specifically an ordering dependency: an attached/child instance will not compose until the instance it is attached to (via `+0x30`) has already composed this frame.** This sharpens scoping's "a child rig following a parent" HYPOTHESIS into a confirmed mechanism, and shows `+0x30`/`+0x34` are read independently by *both* compose functions for *different* purposes (here, an ordering gate plus a source base for a later per-slot copy; in `0x004c36f0`, the actual slot-matrix copy — see below). When the parent is valid, this function also stashes the **parent's own output-array base** (`[parent_record+0x00]`) for later use by individual slots.
2. **The `+0x6e`/`+0x350` manual override** (set every frame by the human poster, §11.3): if record `+0x6e` is nonzero, feeds record `+0x350` through `0x004c03d0` then `0x004c0170` before the main loop. Not traced past call shape.
3. **Per-slot loop** (`0 ≤ slot < N`, `N` = binding descriptor `+0x04`, per the already-specified descriptor layout): for each slot, reads the descriptor's per-slot flag byte at `+0x210 + slot*4` and branches:
   - **Attachment present** (descriptor slot flag bit tested): copies the referenced slot's data directly from the *parent's* output array (stashed in point 1) using a small copy helper `0x004aac40`.
   - **No attachment, per-bone flag `0x02` clear** (the per-slot flag array at record `+0x08`, stride `0xc`/12 bytes): reads the raw local sample for this bone straight out of the `edx` **sample buffer** (`buffer + slot*64`) and writes a copy into record `+0x04`'s array at `slot*0x30` via `0x004c00d0`.
   - **Otherwise** (descriptor slot flag bit `0x02` set, or the per-bone flag `0x04`): calls **`0x004c4970`** (contract below) or **`0x004c50f0`**, both receiving the sample-buffer slot pointer, the record, and a local scratch pointer — the IK/attachment-constraint family, not traced past its call contract (same family the project already defers elsewhere).
4. **Bounding-box accumulation.** Each slot's resulting vector is `minps`/`maxps`-accumulated into record `+0x370`/`+0x380` (CONFIRMED — the per-instance model-space bounding box, a new field identification).
5. **End of loop:** sets **record `+0x38` = 1** ("composed this frame", consumed by point 1 above on any attached child instances).

**Conclusion on `0x004c31b0`'s role:** it is the **per-slot setup pass** — resolving attachment sourcing, populating a *working* per-slot matrix array at record `+0x04` from the raw sampled pose (or from IK/attachment sources), applying the manual override, and bookkeeping (bounding box, the composed-flag). It is **not** the function that performs the bone-hierarchy local-to-model concatenation; that is `0x004c36f0`.

**`0x004c36f0(record, block, owner)` (`thiscall`, 215 instructions, CONFIRMED by full listing, matches census).** This is the function `spec-rig-format.md` §11.16.4 already names as the per-frame writer of the matrix block at record `+0x00`; this pass adds the exact mechanism:

- `ecx(this)=record`; reads the **rig bone array** (`[record+0x14]+0x40`, stride `0x28` — already specified), the **binding descriptor** (`record+0x3c`) and its slot count `N` (`+0x04`), and (independently of `0x004c31b0`) resolves `record+0x30` to get the **parent instance's output-array base** for the straight-copy case.
- **Per-slot loop** (`0 ≤ slot < N`), each iteration tracking three parallel strides: the output byte offset (`slot*0x30`, 48 bytes — the confirmed per-slot matrix stride), the per-bone-flag byte offset (`slot*0xc`, 12 bytes, at `record+0x08`), and the descriptor per-slot-flag offset (`slot*4` at descriptor `+0x210`):
  1. **Remap-table short-circuit.** If descriptor `+0x34`'s remap table has a non-`-1` entry for this slot *and* a valid parent (`+0x30`) is resolved: **straight-copy the parent's slot matrix** (6×`movq`, the full 48 bytes) from `parent_output_base + remap[slot]*0x30` into **this instance's own `+0x00` output array at `slot*0x30`**, and skip everything else for this slot.
  2. **Per-bone flag `0x02` set:** skip this slot's body entirely (frozen/excluded — role not further resolved).
  3. **Otherwise**, look up this slot's **rig-bone index** through descriptor `+0x10` (`[descriptor+0x10 + slot*2]`, the already-specified slot→rig-bone remap), then check a **generation gate**: if `block` is non-null and `block[rig_bone_index] < owner` (both CONFIRMED by listing as 4-byte-stride, raw-rig-bone-indexed), take the **"fresh" branch**; otherwise take the **"cached" branch**:
     - **Fresh branch (CONFIRMED mechanism — genuine bone-hierarchy composition):** reads the rig-bone record's own local offset (`rigarray[rig_bone_index]+0x08`, three floats) and transforms it through `0x00452220`; reads **`rigarray[rig_bone_index]+0x20`** — a **parent-bone-index field within the rig-bone record** — and looks that value up in **binding descriptor `+0x110`** (a *second*, already-specified remap table, confirmed here to map rig-bone-index → binding-slot-index, i.e. the reverse of `+0x10`); uses the resulting slot index to read **this instance's own already-written `+0x00` output array at the parent bone's slot**; copies that parent matrix into the working scratch (`0x004aac40`) and carries it straight through to the shared store below. **This is the local-to-model composition proper: each bone's model-space matrix is built from its parent's already-composed model-space matrix (found via the rig's own parent-index chain) plus its local offset.**
     - **Cached branch:** reads this slot's own previously-stored `+0x00` and `+0x04` matrices (via `0x004aac40` twice) and combines them with the rig-array's local offset via an explicit 3×3 matrix-vector multiply-accumulate (three `mulps`/`addps` pairs against the three rows loaded from the two prior matrices) — an **incremental update** rather than a full hierarchy walk.
  4. **Shared tail**, both branches: if descriptor slot-flag bit `0x10` is set, call **`0x004c4970`** again (same IK-family helper `0x004c31b0` also calls, under a *different* flag bit of the same byte — confirming `0x004c4970` is shared compose-time constraint machinery, not two unrelated functions). Then **store the resulting 48-byte matrix at `[record+0x00] + slot*0x30`** via `0x004c00d0` — **the confirmed per-frame write `spec-rig-format.md` §11.16.4 asked for.**

**Resolving the OPEN item on matrix space.** No step anywhere in `0x004c31b0` or `0x004c36f0` multiplies by an inverse bind pose, nor references any second per-bone constant array distinct from the rig's own live bone array. Every value fed into the final store is built by chaining a bone to its *parent's own freshly-stored* matrix through the rig's own parent-index field — the textbook definition of **model-space (object-relative) skeleton evaluation, not skin-space**. **The matrices written at record `+0x00` are model-space transforms; the inverse-bind-pose multiply needed for true GPU skin matrices has not been found anywhere in this pipeline and must happen downstream, in the skin gather / palette upload (`0x00e7ad30`/`0x00e7aff0`) `spec-rig-format.md` §11.2/§11.16 already cover.** This resolves scoping §1.6's "model-space or skin-space" OPEN item at HIGH-to-CONFIRMED: **model-space.**

**The generation gate's exact purpose remains open** (§11.10). `block`/`owner` (the same two fields threaded through the whole pose-job call chain from the human poster's `0x0262cd60`/`human+0xac8`) gate *both* `0x004c31b0`'s early-exit check (`block[owner-index] < required`, there keyed by owner directly rather than by rig-bone-index) and `0x004c36f0`'s per-bone fresh-vs-cached choice (there keyed by `block[rig_bone_index]`). The structural mechanism — a per-rig-bone-indexed array compared against a per-request value — is CONFIRMED; its semantic purpose (a dirty flag? a generation counter deciding whether another system has already recomputed that bone this frame?) is carried to §11.10.

### 11.5a The cross-layer blend itself: `0x004c21a0` (completes the "sample" step from §11.2; corrects the compose/blend boundary)

**Scope correction, found immediately.** §11.5 already establishes that by the time `0x004c31b0`/`0x004c36f0` ("compose") run, the per-job sample buffer already holds *one* value per bone — compose reads "the raw local sample for this bone straight out of the `edx` sample buffer," never a per-layer value. This pass (full `capstone` listing of `0x004c21a0`, 4,081 bytes / 1,019 instructions, via `tools/harnesses/exe_va.py`) confirms why: **cross-layer blending happens entirely inside the sampler, `0x004c21a0`** (the function `0x004bd0b0` calls under §11.2's step-bit `0x02`), **before compose ever runs.** "Compose," in this engine's own vocabulary, means bone-hierarchy (parent→child) matrix concatenation — a different operation from cross-layer blending.

**Structures** (`ecx` = the same `0x3a0`-byte instance `0x004bd0b0`/`0x004c58c0` already resolve). Layer-count at `+0x58`, layer array at `+0x70` (stride `0x50`) — independently re-confirms `+0x70` over `spec-anim-format.md` §10.1's earlier, partial-pass `+0x74` guess (a decompiler-pseudocode artefact; no real conflict). Per-layer fields read here match the published map exactly: `+0x04` clip index, `+0x10` flags, `+0x14` group, `+0x24` weight; binding descriptor `+0x110` (bone-index→slot-index remap, already named from the *compose* side) is read here from the *sampler* side — independent confirmation. Three working buffers, all `memset`-zeroed at entry: the **output buffer** (caller's arg, 64 bytes/bone, final blended pose), a **bind-context array** (9×721 bytes, matching `spec-anim-format.md` §10.1's "bind context," now confirmed to live in this same function), a **per-track-per-layer weight scratch** (`[9][128]` f32, new — the fully resolved per-(layer,bone) weight after masking), and a **per-bone accumulator scratch** (`rigBoneCount`×32 bytes: `+0x00` running weight total, `+0x10..0x1f` a running quaternion-ish value seeded from identity constant `0x12490f0`; new).

**Phase 1 — per-(layer, bone) weight (the masking pass).** Per track (outer loop), groups walked **descending** (highest-priority group first). Per layer in the current group: `f` = the per-track override factor (clip-header byte `+0x05` bit `0x40`, same mechanism `spec-anim-runtime.md` already documents at `0x004ca620`); `w = f·layer.weight`. **Additive layers (flag `0x2000`) are excluded from the group's running sum entirely** (not merely zero-weighted); non-additive layers accumulate into `Σ_group`. After the group: `mask_scale = min(1, (1−Σ_prior)/Σ_group)` (`Σ_prior` = the raw, pre-mask sum carried over from every strictly-higher group already processed; **not clamped below 0** — if `Σ_prior` already exceeds 1.0, the ratio goes negative and nothing clamps it back, flagged OPEN below, plausibly unreachable for authored content). Every non-additive layer's weight in the group is rescaled by `mask_scale`; every additive layer's weight is instead rescaled by the raw, unclamped `(1−Σ_prior)` — **a structurally distinct branch, confirming additive-vs-override is a real fork in the weight arithmetic, not just a label.** `Σ_prior` then accumulates the group's raw sum for the next (lower) group.

**Phase 2 — the value combination** (not described anywhere by the root-motion text). Ascending layer order; samples each clip (`0x004c8a80` for rotation, already HIGH-confidence-named "the root rotation" sampler elsewhere; `0x004cabc0` for a second channel, almost certainly translation, not previously named at this call site), combined into the bone's output slot, gated on Phase-1 weight `>0`. **Two mechanisms, selected by flag `0x2000`:**
- **Non-additive (override):** a component-wise, order-independent, incremental weighted **LERP** — `t = accumWeight_prior/(accumWeight_prior+w); output = (1−t)·sample_new + t·output_old`. No slerp, no renormalise call found on this path. Algebraically a true `Σ(w·sample)/Σ(w)` weighted average, independent of traversal order. A per-bone accumulator flag (`+0x4` of the accumulator slot) selects between two sub-paths afterward (one `rsqrt`-renormalises a cached quaternion at accumulator `+0x10`; the other calls `0x004bc470`/`0x004bc550`/`0x004c06b0` first) — **this selector's own source was not found** (the byte is read but never written anywhere in this function); flagged OPEN, doesn't change the LERP finding.
- **Additive:** a genuine delta-apply, not a blend. Translation-ish channel: `output += w·(sample_t1 − sample_ref)`. Rotation channel: a full SSE quaternion Hamilton-product (`shufps`/`xorps` sign-mask pattern) combining the existing output with a delta rotation sampled at `t1` and conjugated against the reference-time sample — **using the exact same conjugate sign-mask constant, `0x011942a0`, that `spec-player-runtime.md` §5.4.3 names for the root-motion walker's own `Δ′` term.** This one piece is a confirmed, exact shared primitive (same constant, same conjugate-then-combine shape), not an analogy.

**Closed form.** For bone `b`, with `Override(b)`/`Additive(b)` the active nonzero-weight layers split by flag `0x2000`, each with per-(layer,bone) weight `w_{b,ℓ}` from Phase 1:

```
pose(b) = [ Σ_{ℓ∈Override(b)} w_{b,ℓ}·sample_{b,ℓ}(t1)  /  Σ_{ℓ∈Override(b)} w_{b,ℓ} ]
          ⊕ Σ_{ℓ∈Additive(b)} w_{b,ℓ}·( sample_{b,ℓ}(t1) ⊟ sample_{b,ℓ}(t_ref) )

w_{b,ℓ} = f_{ℓ,track(b)}·layer_ℓ.weight · { min(1,(1−Σ_prior(g))/Σ_group(g))  if ℓ non-additive
                                            (1−Σ_prior(g))                     if ℓ additive }     [g = group(ℓ)]
```

where the override bracket is a plain weighted LERP accumulation and `⊕`/`⊟` are vector add/subtract (translation) or quaternion-multiply/conjugate-delta (rotation).

**Correction to the reused root-motion formula (Team B's own port reused `0x004c58c0`'s formula here; this is the needed correction, not a spec-file change elsewhere — `spec-player-runtime.md` §5.4.3's own root-motion documentation is unaffected and remains correct for what it describes).** The *ingredients* are the same (`f`, `layer.weight`, a higher-group non-additive sum, an additive-flag exclusion) and the underlying intuition — the two mechanisms are kin — is right. But the **arithmetic differs**: root motion applies a **per-layer subtractive mask** (`1 − min(1,Σf′)`, independently per layer); the sampler applies a **per-group multiplicative ratio** (`min(1,(1−Σ_prior)/Σ_group)`, uniformly across a group). They agree only in the common one-dominant-layer-per-group case. Root motion's text also has **no analogue at all** for the override-layer value-combination step (the incremental weighted LERP) — that mechanism had to be traced fresh here. The one piece that genuinely *is* the same mechanism, not merely similar, is the additive-rotation conjugate (`0x011942a0`), confirmed above.

**OPEN (U4 extension):**
a. The per-bone accumulator's `+0x4` selector byte — read but never written in this function; its value source and the exact difference between its two override-combine sub-paths are unresolved.
b. The second sampled channel (`0x004cabc0`) — inferred as translation from call shape/position, not independently confirmed.
c. A final pass (`0x004c302e` onward) copies the accumulator into the real output, adds a rig bind-pose translation term, and calls the already-deferred IK/constraint family `0x004c50f0` (§11.6) for a subset of bones — not traced further, consistent with how that family is already deferred elsewhere.
d. Whether the unclamped `Σ_prior > 1` case (Phase 1) is reachable in any shipped layer configuration — not checked against real data.
e. No Unicorn/emulation cross-check was run (judged lower-value given how decisive the static constant/offset matches already are); flagged as the natural next step if the closed form above needs pinning against real float vectors.

### 11.6 `0x004c4970`: call contract only

`0x004c4970` (469 instructions) is called from both compose functions under distinct descriptor per-slot flag bits, always with the same three live inputs: a pointer into the sample buffer at `slot*64`, the record, and a small local scratch (plus a constant `0x012490c0` and the slot index on the stack). It was **not traced beyond its call contract** — at 469 instructions it is comparable in size to the project's existing "post-sample adjusters (root/IK family)" module (`0x004cfb60` and kin, ~3.3k, already deferred to U5/U6), and this section treats it the same way: **confirmed as a compose-time per-bone constraint/IK solver, consuming the lazily-allocated `record+0x348` array `0x004bf150` creates (§11.4), deferred for whoever specifies the IK/attachment family.**

### 11.7 Layer-API bonus: resolving §5.1 items 14, 25 and 31

These three OPEN items were explicitly marked "U4's question" by the already-landed node-graph-runtime spec. Read in full:

- **§5.1 item 25 (instance float `+0x60`).** Already answered by citation: `spec-player-runtime.md` §5.4.3 identifies instance `+0x60` as the shared, normalised phase `0x004c3d20` advances and `0x004be580` retargets via the stride-matching rate. No new content needed; cross-referenced here so the citation closes the item formally.
- **§5.1 item 14 (the layer API the slots call).** **Cross-reference (2026-10-07): the three entries below for
  `0x004c7510`, `0x004be510` and `0x004bdd40` were first read for §6.3/§6.6–§6.8, which already used them (and, in
  §5.1 item 14 itself, already labelled them "phase `0x004c7510`; rate `0x004be510`; the gone test
  `0x004bdd40`/`0x004c6350`"). This pass's first read of the three swapped the phase/rate roles and narrowed the
  "gone" test to only its handle-validity half; both are corrected in place below against the disassembly
  (`0x004c45a0`, `0x004c3d20`, `0x004c60a0`, `0x004ca5f0` added to the read set) and against §6's original,
  correct framing. See §6.3 and the note opening §6.7 for the other side of this cross-reference.**
  - **`0x004c6d90(this=instance [esi], key=eax)` (CONFIRMED by full listing past its first `ret`):** linear-searches the instance's layer array for a layer whose own `+0x00` field equals `key>>4`; on a match, validates the layer's clip index and, **if the matched layer's key equals the value cached at instance `+0x5c`, clears `+0x5c` to `-1` and clears instance `+0x6c` bit `0x01`.** This resolves "the held layer at instance `+0x5c`": **`+0x5c` caches the key of the instance's current "held" layer (or `-1` if none); `+0x6c` bit `0x01` tracks whether a held layer exists; this function is a remove/fade-out-by-key operation that clears both when the layer being removed is the held one.** (`+0x6c` now has three confirmed bits across this unit and `0x004bf270`/`0x004bf360`: `0x01` held-layer-present, `0x02` pose-in-flight, `0x08` pose-ready.)
  - ~~**`0x004c7510(this=instance [edi], key=eax, rate=[esp+8])` — the rate setter (CONFIRMED by full listing past
    its first `ret`):** finds the layer by key (same search idiom), computes `phase(instance+0x60) − rate`
    (wrapped by `+1.0` if negative) and stores it at **layer `+0x1c`** (a layer-record field not previously named —
    a per-layer cached phase offset relative to the instance's shared phase), then re-snaps the layer's
    previous/current time fields (`+0x08`/`+0x0c`, via `0x004c3b90`) to the shared phase immediately, rather than
    waiting for the next `0x004c4810` advance.~~ **RESOLVED (2026-10-07): this is the PHASE setter, matching
    §6.7 step 6 ("set the current layer's phase … to p"), not a rate setter — the third stack argument is a
    *phase* value, not a rate. The mechanism description above (phase − arg, wrapped, stored at layer `+0x1c`,
    then the time fields re-snapped via `0x004c3b90`) is accurate; only the top-level label and the parameter's
    name ("rate") were backwards. The function never multiplies by elapsed time (no `dt`/ticks parameter reaches
    it at all), which is the tell: calling it with `arg` makes the layer's *effective* phase become `arg`
    immediately (`instance_phase − (instance_phase − arg) = arg`), exactly the one-shot "set phase to p" that §6.7
    calls it for (`arg = 0.0` on restart-at-zero, `arg = p` on phase sync). Confirmed by full capstone listing of
    `0x004c7510` (`tools/harnesses/exe_va.py`): no change to the mechanics above, only to the label and the
    parameter name, which should read `phase=[esp+8]`.**
  - ~~**`0x004be510(instance_handle, key, phase=[esp+0xc])` — the explicit-phase setter (CONFIRMED by full listing
    past its first `ret`):** resolves the handle, finds the layer by key, and stores `phase` at **layer `+0x18`**
    (another newly-named field — an explicit per-layer phase override, consistent with the layer-flag `0x20000`
    "own clock" case `spec-player-runtime.md` §5.4.3 already names).~~ **RESOLVED (2026-10-07): this is the RATE
    setter, matching §6.6/§6.8's framing ("the layer's rate is set to 0.0", "its rate is scaled … `0x004be510`"),
    not a phase setter — layer `+0x18` is the layer's own *rate*, not a phase override. `0x004be510` stores its
    third argument (`rate=[esp+0xc]`, not `phase`) at layer `+0x18` completely unmodified (no reference to phase
    or to instance `+0x60` anywhere in its body), which on its own refutes "phase override" — a phase setter would
    have to read the shared phase to compute anything, and this function never does. Independently confirmed from
    the *consumer* side, which is dispositive: `spec-player-runtime.md` §5.4.3 already named layer `+0x18` as the
    rate read by the clear-flag (non-phase-synchronised) advance path, and full listings of both per-frame advance
    functions bear this out —
    `0x004c45a0` (independent-clock layers) computes `layer+0xc += trunc(layer[+0x18] * elapsed_ticks +
    layer[+0x28])` (a standard rate-times-dt-plus-carried-remainder tick accumulator, `layer+0x28` the carried
    fractional remainder) to advance the layer's own current-time field `+0xc`; and `0x004c3d20` (the
    phase-synchronised group advance) runs the *identical* `layer[+0x18] * elapsed_ticks + layer[+0x28]`
    computation for any group member that carries layer flag `0x20000` ("own clock" — i.e. opts out of the
    group's shared-phase broadcast and advances independently off its own rate instead, which is what "own clock"
    actually means). Neither function ever treats layer `+0x18` as a phase. Confirmed by full capstone listings
    of `0x004be510`, `0x004c45a0` and `0x004c3d20` (`tools/harnesses/exe_va.py`).**
  - ~~**`0x004bdd40(handle)` — the "gone" test (CONFIRMED):** returns true (`al=1`) iff the handle is `-1`, out of
    pool bounds, or fails the self-index check; false otherwise.~~ **RESOLVED (2026-10-07): this description is
    incomplete, not merely imprecise — it only captures the function's first half. `0x004bdd40` takes TWO stack
    arguments, `(handle, key)`, not one. The handle-validity check above (`-1`, out of pool bounds via the count
    at `0x344c91a`, or a self-index mismatch at resolved-instance `+0x50`) is correct as far as it goes and returns
    `al=1` ("gone") immediately when it fails — but when the handle IS valid, the function goes on to resolve the
    layer by `key` (`0x004c60a0`, the same find-by-key idiom as `0x004c7510`/`0x004be510`) and: returns **false**
    at once if the layer is found and its flags (`+0x10`) bit `0x01` is set (the layer is in the
    phase-synchronised group, `spec-player-runtime.md` §5.4.3); otherwise (layer not found, or found with bit
    `0x01` clear) calls `0x004c6350(key)` and returns true iff that result equals exactly `0.0`. This is exactly
    §6.3's already-corrected (2026-10-03) reading — "the instance is invalid, or the layer is missing or outside
    the phase-synchronised group … with no play time left" — confirmed here by full capstone listing of
    `0x004bdd40` itself. §6.3 is right; this entry's "iff the handle is …; false otherwise" was wrong by omission.**
  - ~~**`0x004c6350()` — weight-or-zero (CONFIRMED):** calls `0x004c60a0` (a layer/weight resolver) and returns
    `0.0` if it yields null, else the resolved value; the "gone" companion for weight queries.~~ **RESOLVED
    (2026-10-07): `0x004c6350` is not a weight query, and `0x004c60a0` is not a weight resolver — `0x004c60a0`
    resolves a layer **pointer** by key (returning 0 on a miss), full stop, no weight involved anywhere in it.
    `0x004c6350` then does the real work: null layer → `0.0`; otherwise it clip-cache-bounds-checks the layer's
    clip index, and computes **remaining play time in seconds** — current ticks (`layer+0xc`) directly under
    layer flag `0x04` (reverse play), otherwise the clip's total duration in ticks (`0x004ca5f0`) minus current
    ticks — clamped at 0 and divided by the 4,800 constant already named in §6.7. This is §6.3's "remaining time"
    helper, not a sibling weight query: it is the very function `0x004bdd40` calls internally for its play-time
    half, above. Confirmed by full capstone listing of `0x004c6350`, `0x004c60a0` and `0x004ca5f0`.**
  - **`0x004bf150` and instance `+0x348`:** see §11.4 — the lazy per-bone constraint-array allocator, not a layer-API function proper but discovered while tracing the mode table.
- **§5.1 item 31 (layer-time setters `0x004bd9b0`/`0x004bda10`).** Both (CONFIRMED by full listing) are thin handle-resolving wrappers with the identical shape: resolve the instance handle, then delegate `(record_or_null, key, time)` to one of two distinct targets — `0x004bd9b0` → `0x004c6830`, `0x004bda10` → `0x004c6930`. The two targets were not traced internally; given the call sites `spec-anim-runtime.md` already names (`0x004b107e`/`0x004b10e1`, explicit non-looping-clip times), the most likely split is "set both previous and current time" versus "set current time only" — left as HYPOTHESIS (§11.10).

### 11.8 Record and layer field map (consolidated, this unit's additions in bold)

| Field | Role | Source |
|---|---|---|
| instance `+0x00` | skin-gather matrix-array pointer (model-space) | `spec-rig-format.md` §11.16.6; writer confirmed in full here (`0x004c36f0`) |
| instance `+0x04` | **working/local per-slot matrix array pointer, same stride as `+0x00`** | **new, §11.5** |
| instance `+0x08` | per-slot flag array, stride `0xc` (`0x01`/`0x02`/`0x04` tested) | **completed, §11.5** (scoping named the array, not the stride or bit count) |
| instance `+0x14` | rig holder | `spec-rig-format.md` §11.16.6 |
| instance `+0x30`/`+0x34` | attachment parent handle / remap table | scoping §1.6; **roles split and sharpened, §11.5**: `+0x30` gates compose ordering in `0x004c31b0` *and* sources the straight-copy in `0x004c36f0`; `+0x34` is the remap used only by the latter |
| instance `+0x38` | **"composed this frame" flag, set by `0x004c31b0`** | **new, §11.5** |
| instance `+0x3c` | shared binding descriptor | `spec-rig-format.md` §11.16.6 |
| instance `+0x58`/`+0x60`/`+0x68`/`+0x70` | layer count / shared phase / phase rate / layer array base | `spec-player-runtime.md` §5.4.3 |
| instance `+0x5c` | **key of the currently-held layer, or `-1`** | **new, §11.7** |
| instance `+0x6c` | flags: `0x01` held-layer-present, `0x02` pose-in-flight, `0x08` pose-ready | scoping (bits `0x02`/`0x08`); **bit `0x01` new, §11.7** |
| instance `+0x6e` | **manual position-override pending flag** | **new, §11.3/§11.5** |
| instance `+0x348` | **lazily-allocated per-bone constraint array (`0x4c`-byte stride), `0x004bf150`** | **new, §11.4** |
| instance `+0x350` | **16-byte manual override vector, set by `0x004bf1c0`, read by `0x004c31b0`** | **new, §11.3/§11.5** |
| instance `+0x370`/`+0x380` | **per-instance model-space bounding box (min/max)** | **new, §11.5** |
| layer `+0x00` | **the layer's own key/id (used by the find-by-key family, shifted right 4 bits for comparison)** | **new, §11.7** |
| layer `+0x04`/`+0x08`/`+0x0c`/`+0x10`/`+0x14`/`+0x24` | clip index / prev time / cur time / flags / group / weight | `spec-player-runtime.md` §5.4.3 |
| layer `+0x18` | ~~explicit per-layer phase override, `0x004be510`~~ **RESOLVED (2026-10-07): the layer's own independent-clock rate, set verbatim by `0x004be510` (no computation). Read as a rate (multiplied by elapsed ticks) by both per-frame advance paths: `0x004c45a0` for layers outside the phase-synchronised group, and `0x004c3d20` for group members carrying layer flag `0x20000` ("own clock"). Matches §6.6/§6.8's framing; see §11.7's corrected entry for the full evidence.** | **new, §11.7; role corrected §11.7 (2026-10-07)** |
| layer `+0x1c` | **cached phase offset relative to the instance's shared phase, `0x004c7510`** (role confirmed correct, 2026-10-07 — see §11.7) | **new, §11.7** |
| per-job bind-context array (9×721 bytes) | **scratch for the sampler's own blend pass** | **new, §11.5a; matches `spec-anim-format.md` §10.1's "bind context"** |
| per-job weight scratch `[9][128]` f32 | **resolved per-(layer,bone) weight after Phase-1 masking** | **new, §11.5a** |
| per-job accumulator scratch (`rigBoneCount`×32 bytes) | **`+0x00` running weight total; `+0x10..0x1f` running quaternion; `+0x4` an unresolved sub-path selector** | **new, §11.5a** |
| queue entry `+0x00`/`+0x04`/`+0x08`/`+0x0c`/`+0x10`/`+0x14`/`+0x18` | target object / bone cap / block / handle / dt / owner / step bits | scoping §1.6; **all seven CONFIRMED by full listing here, with exact values for the human path** (bone cap `0x80`, block `0x0262cd60`, owner `human+0xac8`, target `human+0xea0`) |

### 11.9 Cleanroom check (U4)

Every function and global above is cited by its real hex virtual address. No `FUN_`/`DAT_`/`LAB_` or decompiler-style names appear anywhere in this section; all disassembly was read via `capstone` over direct byte reads (`tools/harnesses/exe_va.py`), never a decompiler. Class/field names used ("the controller", "held layer", "the fresh branch") are this section's own descriptive labels, consistent with the project's convention, not engine-native strings.

### 11.10 OPEN items (U4)

1. **The `block`/`owner` generation gate's exact semantics** (`0x004c31b0`'s early-exit test, `0x004c36f0`'s fresh-vs-cached branch). Structurally CONFIRMED: both read a 4-byte-stride array (`block`, e.g. the global `0x0262cd60` for humans) indexed either by `owner` directly (`0x004c31b0`) or by raw rig-bone-index (`0x004c36f0`), comparing against `owner` (e.g. `human+0xac8`). What the array actually counts (a per-bone generation/dirty tag? a per-owner frame counter?) was not determined.
2. **`0x004c4970` and `0x004c50f0`'s internal math** (the compose-time IK/attachment-constraint family). Call contract is CONFIRMED; internals not traced, consistent with how the project already treats the sibling post-sample IK family as deferred. `record+0x348`'s exact 76-byte (`0x4c`) record layout, and the global factory object at `0x034a79d0` that allocates it, are both unread past their call shape.
3. **Whether `[[human+0xd24]+0x14]` and `0x00487e80([[human+0xd24]+0x10])` (the controller's own instance) are ever the same runtime *value*.** CONFIRMED structurally separate fields (§11.4); whether they coincide in the common case is a runtime-value question this static pass cannot answer.
4. **The exact polarity of mode 0's branchless post-sample-bit (`0x04`) toggle**, keyed on bit `0x1000` of `[[human+0xf0]]+0x48`. The *existence* and *location* of the rig-flag gate is CONFIRMED; whether setting that bit *enables* or *suppresses* the post-sample step was not algebraically resolved with certainty.
5. **`0x00452220`, `0x00467ed0`, `0x0048d070`, `0x0048cf50`, `0x00755d60`, `0x004c03d0`, `0x004c0170`, `0x004c00d0`, `0x004aac40`, `0x004c3b90`, `0x004c60a0`** — the per-bone math and copy helpers compose and the layer API call. Treated as opaque primitives throughout this section, based on calling-context inference only.
6. **`0x009564a0`, `0x00999d00`, `0x0096f4f0`'s full bodies.** `0x0096f4f0` is already named elsewhere (the "dead or gone" predicate); `0x009564a0`/`0x00999d00` are called identically by modes 0, 3 and 6 but not traced.
7. **Mode 0's aim/IK block and turn-damping tail** (`0x009a1916`–`0x009a1b5c`): a vtable call plus double-precision turn-rate math. Entirely out of this unit's scope — belongs with the aim/look-at family (§12).
8. **`0x004bf550`.** Confirmed to be a single-instruction stub (`ret`, no-op), called unconditionally at the end of every successful mode-0/3/6 post. Dead hook, or a feature gated out in this build — not determined.
9. **`record+0x08`'s remaining bytes.** Confirmed `0xc`-byte stride and at least one flags byte plus a secondary field; remaining bytes not identified.
10. **Which of `0x004c6830` / `0x004c6930` sets both layer-time fields versus only the current one.** HYPOTHESIS only.
11. **The near-null defensive path in `0x004bf360`'s first loop.** Confirmed unreachable for any entry appended through `0x004bf4c0` and left at its zeroed default; whether any *other* poster (the two unidentified pose clients of U7, or `object_rig`) deliberately posts a `-1`-handle entry was not checked.
12. **`0x00487e80`'s other callers.** Confirmed as a universal one-line accessor; not re-checked whether every other caller in the image also passes an animation controller.
13. **Possible overlap with Animation U5.** `0x004cfb60` is called from `0x004bf270`'s bit `0x04` with the *local per-job sample buffer*, a structurally different data source from the layer walk `0x004c58c0` §9 reads later through the velocity composer — flagged for U5 to confirm these are genuinely two separate consumers.
14. **The cross-layer blend's own open items (§11.5a).** The per-bone accumulator `+0x4` sub-path selector's source; the second sampled channel's (`0x004cabc0`) exact identity; the final bind-pose-add/IK-gate pass past `0x004c302e`; whether Phase 1's unclamped `Σ_prior > 1` case is reachable in shipped data; no emulation cross-check performed on the closed form.

---

## 12. U6 — Human procedural modifiers

**Scope.** Unit U6 of `review/scoping-animation.md` §5, as restated by the task brief: aim/look `0x009b4360`
with the shared solver `0x004cd8c0`, and the spine-bend cluster `0x009b34f0`/`0x009b0e90`, `0x009a0a90`,
`0x009b0ad0`. **This pass finds the spine-bend cluster as named is wrong: only `0x009b34f0`→`0x009b0e90` is
spine bend. `0x009a0a90` and `0x009b0ad0` are unrelated procedural modifiers that merely run from the same
call site under the same gate — see §12.6.** The real spine-bend situation code, `0x009b20a0`, was added to the
read set because `0x009b34f0` calls it directly and it decides whether spine bend runs at all.

**Method.** Ghidra-free: direct byte reads of the executable (`tools/harnesses/exe_va.py` conventions, a small
VA-driven capstone listing script written for this pass) via PowerShell with `$env:PYTHONIOENCODING='utf-8'`.
No Ghidra project was opened. Every function below was read as a full linear capstone listing from its entry to
its own `ret`/padding boundary — not sampled, not decompiled. Jump-table targets were resolved by reading the
raw dwords at the table address directly. Ten addresses were read in full this way (4,007 instructions total).

**Read first, cited not re-derived.** `spec-anim-runtime.md` §6.1/§6.2 (the controller's state-slot fields and
the `0x84`-byte slot layout, including the `+0x50` current-slot pointer, slot `+0x80` flag bit `0x10` "managed",
slot `+0x10` weight, slot `+0x6c` node-graph instance row, and controller `+0x58` cross-fade duration). This
pass's aim/look reading cross-references those exact fields directly against the human object's own controller,
confirming them independently rather than assuming them (§12.1 below).

### 12.1 Aim/look — `0x009b4360`, the shared solver `0x004cd8c0`, and their helpers

**`0x009b4360` (1168 instructions, FULLY READ) is a per-human, per-frame aim/look-at state machine**, run from
the per-human animation step (`review/scoping-animation.md` §1.2 step 3) when the human's pose-mode byte
`+0xcd2` is 0, 3 or 6. It is not a spatial IK solver by itself — it computes a target **bearing** (a signed
horizontal angle) and hands the result to a smoothing/clamping writer; whatever turns that bearing into bone
rotations is downstream (not reached by this function).

**Shape, in order:**

1. **Early camera/player gating.** Clears human `+0xa98`/`+0xa99` (output flags — `+0xa99` means "aim/look
   wrote something this frame", read by spine bend, §12.2). Checks is-player (`0x0091f5e0`, the same predicate
   `review/scoping-player.md` U3 already names), a camera-mode global (`0x005649c0`) and a related predicate
   (`0x005780f0`), then always calls `0x009b24a0` (role not resolved — outside this pass's addressed set).
2. **Target-vector early-out.** Reads human `+0xea0` as a 16-byte vector (**not** a pointer — see the
   correction in §12.6 below about what "the target object" at `+0xea0` really is) and computes its
   per-component absolute value with the `max(x,-x)` trick against the same epsilon (`0x012a2f84`) that
   `0x004cd810` uses for its own near-zero vector clamp. If every component is near zero and two companion
   fields (`+0xeb0`/`+0xeb4`) are both zero, it calls `0x009b0870(human, 0.0, 0.0)` (clears the aim output) and
   returns immediately.
3. **Look-mode selector, human `+0xab0` (byte, values 0–3).** A priority cascade over weapon-state bits
   (`+0xd40`/`+0xd44`, including bit `0xb` and mask `0x1400`), a controller-flag query (`0x004b4fc0` with mask
   `0x400`), a state query (`0x00943a20`), and a distance check against a tunable global (`0x00262d4b8`)
   resolves `+0xab0` to 0–3. A 4-entry jump table at `0x009b5568` (targets `0x009b4d32`, `0x009b4de9`,
   `0x009b4e1c`, `0x009b4b43` for modes 0–3) dispatches to one of four arms, each of which computes a signed
   yaw bearing between the human's own aim-origin point and the look target's aim-origin point.
4. **The aim-origin point, `0x009b2fe0` (157 instructions, FULLY READ).** Given an object, this returns its
   base position (`+0x64`/`+0x68`/`+0x6c`, a plain world-space vec3) plus a bone-relative offset
   (`0x0094bb70`/`0x009b2940`), and — only when the object is a player (`0x00853b30` class-mask `0x21`) — adds a
   further offset keyed on the equipped item's hold style (`+0x16c8`, one of the enum values `2`/`4`/`5`), via
   the equipped-item lookup `0x004dcf00` and a weapon-bone-relative transform (`0x00da5aa0`/`0x00da60c0`, keyed
   by the bone-name literal `0x013213e4`). **This same helper is reused by the spine-bend chain for the player's
   own look target** — see §12.2 — the one concrete point of contact between the two systems beyond the shared
   solver.
5. **Cross-reference against the already-landed controller slot fields (`spec-anim-runtime.md` §6.1/§6.2),
   CONFIRMED independently by this pass.** Before finishing the bearing computation, `0x009b4360` reads the
   human's controller `+0x50` (**current-slot pointer**, §6.1), tests slot `+0x80` bit `0x10` (**managed/
   node-graph slot**, §6.2), reads slot `+0x10` (**weight**, §6.2) and controller `+0x58` (**cross-fade
   duration**, §6.1). If the current slot is managed and its weight is non-negligible, it resolves slot `+0x6c`
   (**node-graph instance row**, §6.2) through `0x004cc6a0` (the small accessor `spec-anim-runtime.md` §6 already
   names) to a node record and reads two of that node's own fields (`+0x20`/`+0x24`) to compute a ratio. That
   ratio is subtracted into the look bearing before it is finalised — in effect, **look/aim does not double-count
   a turn the currently-playing managed clip is already supplying**; it only asks the pitch/yaw writer for the
   remainder. This is a genuinely useful, independently-verifiable tie between the procedural modifier and the
   node-graph runtime (§8's territory), worth citing there.
6. **The shared scalar solver, `0x004cd8c0` (284 instructions, FULLY READ). CONFIRMS the scoping note's claim**
   that `0x009b4360` shares this function with the post-sample family. It is called once, late in the function
   (at `0x009b51de`), on the smoothed yaw delta. **It is not a spatial/IK solver — it is a generic scalar
   damped-approach function**: given a current value, a target value, a rate parameter, a pointer to a
   persistent per-call state scalar, and a mode byte, it returns a new current value and updates the state. Mode
   0 is a short cubic-ease-toward-target; mode non-zero (the ~150-instruction tail from `0x004cd907`) is a longer
   acceleration-limited, critically-damped-spring-style computation using several `.rdata` tuning constants.
   Both modes write the updated state back through the pointer argument. **Caution for anyone reading this
   address cold:** two calls earlier in `0x009b4360` (`0x009b46ee`, `0x009b4702`) go to `0x004cd810`, a
   *different*, merely-adjacent function (49 instructions, FULLY READ) that only snaps a vector's near-zero
   components to exactly zero (it calls `0x004cd2d0` internally) — it is easy to conflate the two because of the
   address proximity, but only `0x004cd8c0` is "the solver".
7. **Output: `0x009b0870` (108 instructions, FULLY READ), called three times with `(human, pitch, yaw)`
   pairs.** This writes human `+0xf4c` = **pitch** directly (after a validity remap through `0x00da41c0` if the
   input is non-finite or exceeds a small epsilon `0x00125e270`), and human `+0xf54` = **yaw**, *rate-limited*:
   the maximum change this frame is `yaw_param / 0x00132a0ac` (the same global the rest of the animation runtime
   uses as a fixed 1/30 s dt, per `spec-anim-runtime.md`/scoping — so the incoming parameter is a turn-rate in
   units/second, not an absolute angle), clamped around the *previous* `+0xf54` by that delta after a plausibility
   check through the human's own virtual method at vtable `+0x48` (HYPOTHESIS: a basis-from-angle sanity check,
   not fully resolved). **`+0xf4c`/`+0xf54` are the final smoothed aim pitch/yaw this system produces; nothing in
   this pass's read set shows where a downstream bone-driver consumes them** — that is outside the four named
   roots.
   - The address `0x009b0900` the original brief carried over from U5's shallow citation is **not a separate
     function** — it is 0x90 bytes inside this same `0x009b0870`, landing in the middle of the yaw rate-limit
     clamp. See §12.5 for the full accounting.
8. **A "noticed target" side effect**, late in the function: when a distance/visibility test passes, it plays a
   one-shot reaction/flinch state through the already-documented play wrapper `0x00959b50`
   (`review/scoping-animation.md` §1.3), tagged `0x1d66` (= 7526 decimal — inside the `[7500, 7589)` caller-tag
   convention `spec-anim-runtime.md` §6.2 documents, which this independently confirms), after querying slot
   activity (`0x004b4ce0`) and a cross-fade state (`0x004b41b0`) with the same tag. The actual state/action id
   played is resolved through `0x004b4c30` from values seen as `0x1fd`/`0x1fe`/`0x201`/`0x202`/`0x2da`/`0x2db`.
   This is a side effect of the aim/look step, not the look-at write itself (HYPOTHESIS for its exact trigger
   semantics; the mechanism and the tag convention are CONFIRMED).

### 12.2 Spine bend, part 1 — the gatekeeper, `0x009b34f0` (323 instructions, FULLY READ)

Runs from the same per-human step, gated on human `+0xdc` bit `0x02` (`review/scoping-animation.md` §1.2 step
4). In order:

- Unconditionally clears human `+0xed0` bit `0x40`, then caches the controller's own animation-instance handle
  (`0x00487e80` on the controller, exactly the accessor `review/scoping-animation.md` §1.6 already names) into
  human `+0xf80`. **This corrects the scoping note's own hedge at §1.2 step 5** ("HYPOTHESIS: a held prop or
  second rig" for `+0xf80`): it is the *same* controller instance handle, re-cached by this very function, not a
  second instance. (Bit `0x40` of `+0xed0` is set back later in this same function, right after the
  `0x009b20a0` call below — it means "the situation code was (re)computed this call", not "a prop is held".)
- **Is-player skips an NPC-only gate block entirely** (`0x00853b30` class mask `0x21`). For NPCs only, *all* of
  the following must hold before a procedural lean-offset call (`0x00987410`, role not resolved, outside the
  animation band) runs:
  human `+0xa99` == 0 (**aim/look did not write anything this frame** — see §12.1 step 2/7 — so aim/look and this
  particular NPC lean correction are mutually exclusive per human per frame); `0x00941da0(human)` true (role not
  resolved); `0x0096f4f0(human)` false (the documented dead predicate, `review/scoping-animation.md` §1.3); human
  `+0xccc` == 0 (the pose-mode-adjacent byte also read elsewhere); human `+0xe0` bit `0x1000` clear; **human
  `+0xed0` bit `0x01` clear** (note: this is the human object's own `+0xed0`, at bit `0x01` — see §12.5 for why
  this is *not* the "`+0x30`" the original brief asked about); human `+0xf78` has any bit
  set outside `{0x40, 0x80, 0x100}` (this is last frame's cached situation code, see below); and human `+0xeb8`
  (a float) inside `(epsilon, global 0x001174f78)`.
- **A script-settable override, human `+0x1b4c`/`+0x1b50`/`+0x1b54` → human `+0xf8c`.** If byte `+0x1b4c`
  (enable) is non-zero and float `+0x1b50` (a threshold) is `> 0`, human `+0xf8c` takes the value of float
  `+0x1b54`; **otherwise `+0xf8c` is forced to exactly `0.0` every single call.** The shape (an enable byte, a
  positive-threshold gate, and a value, as three adjacent fields) matches a Lua-settable override; no native
  that writes `+0x1b4c` was traced in this pass, so the writer is still OPEN, but the consequence is clear:
  **unless something external sets this override, the field this pass believes is the "limit scale" the
  original brief cited for `0x009b34f0` is zero on every single frame.** (This is the one of U5's four shallow
  citations that checks out cleanly — see §12.5.)
- **Human `+0xed0` bit `0x01` gates everything from here on** (re-tested, same bit as above): if set, the
  function skips straight to its own return, and neither the situation-code call nor the actual spine-bend chain
  runs this frame. This is the closest thing in this unit to a single master "suppress spine bend" switch.
- **`0x009b20a0(human)` — the situation code, 164 instructions, FULLY READ.** Its return value is stored
  directly into **human `+0xf78`** (overwriting the stale value the gate above just read). See §12.3 for the full
  breakdown of this function; the short version is that it returns one of exactly four values — `0`, `0x130`,
  `0x1c0`, `0x400` — never a per-joint bitmask. **If the result is `0`, `0x009b34f0` bails out entirely: no
  joints are selected for bending this frame.** If the result is exactly `0x400`, human `+0xec4` is reset to a
  small epsilon as a side effect.
- **The bend target, human `+0xee0` (xmmword).** If the human is the camera-focus object (`0x009da4e0() ==
  human`) under a specific camera mode (`0x005649c0() == 2`) and a further predicate (`0x005780f0()`) holds, a
  fresh target point is computed using **the same aim-origin helper `0x009b2fe0`** used by aim/look (normalised
  through `0x0048d110`/`0x004f47f0`/`0x00446fb0`/`0x00446f60`). Otherwise (NPCs, or players outside that specific
  camera mode) the previous frame's cached `+0xee0` is reused, except under one more camera-mode branch
  (`0x005649c0() == 0`) which also recomputes. The resulting normalised vector is passed by pointer to
  `0x009b0e90` — **this is the actual call into the spine-bend chain** (`lea ecx, [local]; call 0x009b0e90`).
- **The bend weight, human `+0xf7c`.** A priority cascade, first match wins: (1) controller `+0x44`
  (the skeleton-set id, `spec-anim-runtime.md`/scoping §1.4) `== 5` → a fixed constant (a different body type
  gets a hard-coded different weight); (2) is-player and the controller has both a current (`+0x50`) and an
  outgoing (`+0x54`) slot → a linear ramp either side of a cross-fade threshold (global `0x001174edc`, the same
  threshold `0x009b20a0` uses for its own No-Spinebending-vs-weight check, §12.3) between two tunable constants —
  spine bend is suppressed mid-cross-fade and full once a clip is steady-state; (3) otherwise, a mount/ride
  object's validity and a flag/threshold on it (`0x00971cf0`/`0x009da830`) select one of three further fixed
  constants (one specifically "pinned while mounted/riding"); (4) default: an epsilon, i.e. effectively off.
- **Remaining `+0xed0` bits, all set or cleared every call** (none are the `+0x8000` the original brief asked
  about — see §12.5): `0x80`/`0x100`/`0x200`/`0x400` from a weapon/combat-state cascade gated on
  `+0xac8 > 1` (NOTE — see below); `0x1000` set for NPCs and cleared for players; `0x10000` from mount/ride
  validity; `0x4000` from whether the controller's current slot exists with non-negligible weight; `0x2000` from
  a turn-speed float (`+0xac4`) against a threshold; `0x800` from a current-slot-weight check plus a predicate
  `0x00956210`.
  - **Side note for whoever owns the pose-request queue (§11):** `review/scoping-animation.md` §1.6 cites
    "human `+0xac8`" as the pose-request queue entry's *owner pointer* field. This function reads the identical
    literal offset on the identical object as a plain signed integer compared against `1`. Both readings are on
    the same object at the same offset; this pass did not resolve whether the field is genuinely reused for two
    purposes or whether one of the two citations is wrong (§12.7).

### 12.3 The situation code, `0x009b20a0` (164 instructions, FULLY READ)

**Correction to the original brief's framing:** this is cited (by U5, shallowly) as "joint
mask `+0xd8`". Read in full, it is neither: it never touches any object's offset `0xd8`, and it does not return
a per-joint bitfield. It returns exactly one of four values in `eax` — `0`, `0x130`, `0x1c0`, `0x400` — making
it closer to a small enumerated **situation/suppression code** than a mask. Logic, in order:

1. **No-Spinebending suppression.** Queries the controller's animation-record flag words via two sibling
   accessors, `0x004b4fc0(controller, 0x80)` and `0x004b4fe0(controller, 0x80)` — mask `0x80` is bit 7, which
   `spec-anim-runtime.md` §6.1 already names **`No_Spinebending`** on exactly this field pair (controller
   `+0x198`/`+0x19c`, the OR of every playing animation record's flag word, across all slots vs. non-blending
   slots respectively). If the broader query is true, or the narrower one is true *and* the controller's
   current-slot weight (`+0x50`→`+0x10`) is above the same cross-fade threshold `0x009b34f0` uses for its own
   weight ramp (`0x001174edc`), a local flag `bl` is set true — meaning "the currently playing clip(s) say don't
   spine-bend".
2. `0x00941f40(human)` twice (the second call's result's `+0x19c` field becomes `ebp` — role not resolved, a
   weapon- or vehicle-like object).
3. **A per-species gate.** `human+0x34` (byte, a species/class index) indexes a global per-class table at
   `0x002cc9900` (`[idx*4]`); bit `0x02` of that record's own `+0xa` byte decides whether a local `edi` carries
   the human pointer through (`edi = human`) or is forced to `0` (via the `neg`/`sbb` all-bits trick). This is
   later used as a plain boolean (does this species get special handling).
4. Returns `0` immediately if: dead (`0x0096f4f0`); pose-mode byte `+0xcd2` is `1` or `3`; the No-Spinebending
   flag from step 1 (`bl`) is set; or any of five further predicates is true — `0x0098b970` (also used by
   `0x009b4360`'s own look-mode cascade, §12.1), `0x00989670`, `0x00994f60`, `0x0098a3f0`, `0x009b91e0`.
5. If `0x009aa1e0(human, 0)` is true: the return is literally `0x009aa260(human, 0)` turned into `0` or `0x400`
   by an all-bits trick — **confirms `0x400` is a genuine single-situation code**, independently cross-checked
   against `0x009b34f0`'s own special-case handling of exactly that value (§12.2).
6. Otherwise, three more "return 0" predicates (`0x0095d720`, `0x00956380`, `0x009634f0`), then a cascade over
   the species gate (`edi`), pose-mode byte `+0xccc` (**a different byte from `+0xcd2`** — this one compared
   against `2`), `0x00943a20(human)`, and a predicate on `ebp` (`0x00b83a00`) that together select between
   returning **`0x1c0`** and falling through to one more check: `0x009ba7e0(human)` (called for side effects
   only, return discarded), then human `+0xed8` bit `0x01` (**the same bit `0x009b0e90` tests at its own
   entry**, §12.4) — if set, return **`0x130`**; if clear and the species gate is off, return `0x1c0`; if clear
   and the species gate is on, check a per-human timer at `[human + 0x2094]` via the same timer-expiry helper
   (`0x00d9e1e0`) `0x009b0e90`'s own randomised state machine uses throughout — expired/ready returns `0x1c0`,
   not-ready returns `0`.

**So the four non-zero codes are, as far as this pass traced them:** `0x400` = a specific one-off situation
(tied to `0x009aa1e0`/`0x009aa260`, not identified further); `0x130` = the `+0xed8` bit-`0x01` condition (shared
with `0x009b0e90`'s own "did the target change" reset test); `0x1c0` = the general "normal bending allowed"
case reached by several different paths. `0` means "do not bend at all this frame" and is the value
`0x009b34f0` checks explicitly to bail out.

### 12.4 Spine bend, part 2 — the chain proper, `0x009b0e90` (979 instructions, FULLY READ)

Takes the normalised target vector from `0x009b34f0` in `ecx`. Carries the literal string `spinebend`
(`review/scoping-animation.md` §1.2 step 4), confirming this is the chain's real body.

**This is not a one-shot bend — it is a continuously-running, timer-driven procedural micro-motion system**
layered on top of the raw target direction, with its own two interacting state machines:

- **Target-change handling.** Human `+0xed8` bit `0x01` (set externally — not traced where) gates whether the
  incoming `ecx` vector is accepted as a new target at all; when accepted, it is cached at human `+0xf10` and
  `+0xf00`, and the whole state machine below is reset (main state `+0xef0` = `0`, sub-state `+0xef8` = `1`,
  sub-state `+0xef4` = `0xb`). A fast path (same gate, is-player, and a scene/distance predicate against
  `0x00dad830` false) just decays `+0xf20`/`+0xf24` (the output bend amount, see below) toward zero by a fixed
  per-frame factor and returns without running anything else.
- **An angle deadzone with hysteresis.** The new target direction is dot-producted against the *previous*
  cached look vector at `+0xee0`; if the angle between them exceeds a cosine threshold (global `0x0012a30c0`),
  the state machine resets as above — **unless** a short grace-period timer at `+0xf34` is still running, in
  which case the brief excursion is tolerated without a reset (the timer is only disarmed, not reset, once it
  expires). `+0xee0` is then unconditionally refreshed to the new target every call regardless.
- **Main state machine, human `+0xef0` (values 0–3), dispatched by a 4-entry jump table at `0x009b1f6c`.**
  State `0` just snaps the output (`+0xf20`/`+0xf24`) to a near-zero constant (`+0xec4`). State `1` is a
  "decide" state: it calls a time/random source (`0x00dab5b0`) and, depending on the result (and on human
  `+0xed8` and a per-human counter at `+0xefc`), picks a **random hold duration** from one of several ranges
  (roughly 1.0–1.5 s, 1.0–4.0 s, or 1.0–6.0 s — the raw millisecond bounds pushed are `0x3e8`/`0x4b0`/`0xfa0`/
  `0x1770`, via the random-in-range helper `0x00dab660` and the timer setter `0x00d9e140`) and advances to
  state `2` or `3`, which are both "holding" states that, once their shared timer at `+0xf30` expires, return to
  state `1` to pick a new random hold. **This is a classic idle-fidget/micro-sway timer, not a deterministic
  formula** — the spine does not bend by a fixed, computable amount; it bends toward the current target and
  then holds that pose for a randomised duration before the secondary sub-state machine (below) perturbs it
  again.
- **Secondary sub-state machine, human `+0xef8`/`+0xef4` (values 0–10), dispatched by a 12-entry jump table at
  `0x009b1fa8`.** Each state (seen: picking further random sub-durations in similar millisecond ranges via the
  same `0x00dab660`/`0x00d9e140` pair; re-deriving a damped update of `+0xf20` via calls to `0x00dab6a0`
  (a different random/noise source taking two float bounds) blended against a running value at `+0xf38`;
  periodic calls to `0x005eb780`/`0x00446f60` which rebuild/renormalise the cached direction at `+0xf00`) adds
  further small, randomised perturbation on top of the main state machine's hold/transition cycle. The overall
  effect reads as layered randomised micro-motion (a believable "can't hold perfectly still" idle variation)
  rather than a single bend curve.
- **Output, human `+0xf20`/`+0xf24` (the X/Y bend amount).** Every state-machine arm eventually rate-limits its
  update of these two floats the same way the aim/look pitch/yaw writer does: new value is clamped to within
  `rate / 0x00132a0ac` (the same fixed-1/30s-dt convention) of the previous value, using tunable per-arm
  constants. **These two floats are this chain's actual numeric output** — nothing in this function's 979
  instructions turns them into a bone rotation; that step is downstream and outside this pass's four named
  roots (most plausibly inside the IK/post-sample family `0x004cd000`-`0x004d0000`, `review/scoping-animation.md`
  §2.1, which this pass did not open).
- **A read-only, five-entry joint budget check.** At one point the function sums the X components and,
  separately, the Y components of **five records spaced exactly `0x60` bytes apart**, starting at human
  `+0xfb0` (so `+0xfb0`, `+0x1010`, `+0x1070`, `+0x10d0`, `+0x1130` — each presumably a per-joint record with at
  least an `(x, y)` pair at its own `+0x00`/`+0x04`). **This pass greped every write to that address range across
  the whole function and found none — it is read-only here.** The summed magnitude is compared against a
  tunable threshold (global `0x001174f90`); if exceeded, a dirty/overflow flag is set on human `+0x2a2` bit
  `0x01` (only if bit `0x08` of the same byte is not already set). **Reading:** these five slots are almost
  certainly per-joint bend amounts *already applied* by whatever consumes `+0xf20`/`+0xf24` downstream (i.e.
  this function monitors a running total so the procedural layer does not keep pushing a spine chain that is
  already near its rotation budget), rather than something `0x009b0e90` itself writes. This is a HYPOTHESIS
  about the consumer's identity, but the read-only/write-never split is CONFIRMED by exhaustive search of this
  function's own body.

### 12.5 U5's shallow citations — verified independently, two corrections

A concurrent unit's own scope note listed, as a shallow input-contract citation for root motion:
> `0x009b0e90` (directions `+0x60`/`+0x70`, speeds `+0x80`/`+0x84`), `0x009b20a0` (joint mask `+0xd8`),
> `0x009b34f0` (limit scale/flags), `0x009b4360`/`0x009b0900` (`+0xac`).

Having now read all four of these in full, independently:

- **`0x009b0e90` directions `+0x60`/`+0x70`, speeds `+0x80`/`+0x84`: NOT FOUND.** A full read of all 979
  instructions shows no access to `+0x60`, `+0x70`, `+0x80` or `+0x84` of any object inside this function (the
  nearest things it touches are `0x009b0e90`'s *own* locals, `esi+0xee0`/`+0xf00`-`+0x1190ish`/`+0xed0`/`+0xed8`/
  `+0xf78`/`+0xf7c`/`+0xf8c`/`+0x2a2`, none of which are `+0x60`/`+0x70`/`+0x80`/`+0x84`). If U5 meant a
  *different* structure at those offsets, that record was not identified in this pass either.
- **`0x009b20a0` joint mask `+0xd8`: NOT FOUND.** A full read of all 164 instructions shows `0x009b20a0` never
  touches an offset `0xd8` of anything. It is a self-contained predicate/classifier that returns one of exactly
  four values — `0`, `0x130`, `0x1c0`, `0x400` — in `eax`, computed from controller animation-flag queries,
  pose-mode byte `+0xccc`, a per-species table lookup (`[esi+0x34]*4 + 0x2cc9900]`), and about nine unresolved
  predicate calls. Its only traced caller (`0x009b34f0`) stores the *return value itself* at **human `+0xf78`**,
  not at any object's `+0xd8`. **Correction:** this is not "a joint mask" in the sense of a per-joint bitfield —
  it is closer to a small situation/state code.
- **`0x009b34f0` limit scale/flags: CONFIRMED, with the exact mechanism.** This one checks out exactly: see
  §12.2's account of human `+0x1b4c`/`+0x1b50`/`+0x1b54` (a script-settable enable/threshold/value triple)
  feeding human `+0xf8c` (defaults to `0.0` whenever the override is not active — i.e. **off by default**), and
  of human `+0xed0`'s many bits.
- **`0x009b4360`/`0x009b0900` `+0xac`: the address is wrong, not just the offset.** `0x009b0900` is **not a
  function entry** — it is an address 0x90 bytes inside `0x009b0870` (the pitch/yaw output writer that
  `0x009b4360` calls), landing mid-instruction-stream in the middle of the max-turn-rate clamp block. There is
  no field literally `+0xac` anywhere in `0x009b4360`'s 1168 instructions or in `0x009b0870`'s 108. The nearest
  real cluster of `+0xaXX` fields on the human object, all confirmed by this pass, is `+0xa50`/`+0xa54`/`+0xa58`
  (an aim-offset vector), `+0xa78`/`+0xa70` (a validity-checked qword), `+0xa98`/`+0xa99` (aim-active output
  flags), `+0xab0`/`+0xab4`/`+0xab8`/`+0xabc` (the look-mode selector and its yaw clamp state), and — in the
  *other* sibling function `0x009b0ad0`, not `0x009b0900` — `+0xacc` (a rate-limited recoil/kick accumulator).
  Likely U5's shallow pass meant to cite `0x009b0ad0` (or the `+0xacc`/`+0xac4`/`+0xac8` cluster inside it) and
  mistyped/misremembered the address as `0x009b0900`; either way, `0x009b0900` itself is not a distinct function
  and has no `+0xac` of its own.

### 12.6 Corrections to the scoping note's own framing

- **`0x009a0a90` and `0x009b0ad0` are not spine bend.** Both were read in full (574 and 201 instructions) and
  neither touches any field `0x009b34f0`/`0x009b0e90` use, nor calls either of them. `0x009a0a90` reads/writes
  only controller `+0x18`/`+0x1c` under weapon/combat/camera gating (HYPOTHESIS: a weapon-sway or aim-settle
  playback-rate modifier). `0x009b0ad0` writes a single **global** 12-entry history ring buffer at
  `0x0262d440`-`0x0262d4b4` from a smoothed accumulator at human `+0xacc` fed by the aim-offset vector at
  `+0xa50`/`+0xa54`/`+0xa58` (HYPOTHESIS: a weapon recoil/aim-kick sample history, plausibly feeding camera
  shake). `review/scoping-animation.md` has been annotated with this correction directly (not raced).
- **`0x009b20a0` is a four-valued situation code, not a per-joint mask** — see §12.3.
- **The original brief's `0x009b0900` is not a function** — it is an address inside `0x009b0870`. See §12.1
  step 7 and §12.5.
- **The shared solver claim (`0x009b4360` ↔ `0x004cd8c0`) is CONFIRMED correct** as stated in the scoping note
  — the only caution is not to confuse it with the merely-adjacent `0x004cd810` (§12.1 step 6).

### 12.7 OPEN items (U6)

1. **Spine-bend object `+0x30` bits `0x01`/`0x8000`: absent from this unit entirely.** No field at offset
   `0x30` of the human, the controller, or any local structure inside `0x009b34f0`/`0x009b0e90`/`0x009b20a0` is
   ever touched. If real, it belongs to a structure none of these three functions opens.
2. **Global byte `0x03171bca`: not referenced anywhere in this unit's ten fully-read functions.** It sits
   between the control-parameter count (`0x03171bc8`) and array pointer (`0x03171bcc`), most plausibly padding;
   not this unit's concern either way.
3. **Controller `+0x1a4` bit `0x10`: still not resolved, but the negative search is now wider.**
   `spec-anim-runtime.md` §6.2 already found no setter for the slot flag (`0x1000`) that clears this bit,
   searching the whole animation-library band. This pass searched the whole human-side procedural-modifier band
   instead and also found nothing. It may genuinely be a dead switch, or the setter is in a third region
   (vehicle/`object_rig` controllers, §10; or script natives) that neither pass has opened.
4. **The field `+0xac8`'s possible dual role.** `review/scoping-animation.md` §1.6 cites human `+0xac8` as the
   pose-request queue entry's owner pointer field; this unit's `0x009b34f0` reads the identical offset on the
   identical object as a plain signed integer compared against `1`. Flagging for reconciliation, not asserting
   either side incorrect.

---

## ~~6.~~ ~~7.~~ ~~8.~~ ~~9.~~ ~~10.~~ ~~11.~~ 13. Cleanroom check

- Addresses and offsets are plain hex throughout. There are no tool-generated function, data or label names, no
  decompiler variable names and no pasted pseudocode. Only capstone listings, raw byte and dword reads, and
  call-site scans were used; no decompiler output was read.
- Names quoted as data: control-parameter names and `control_filters` / `node_graph_files` content read from the
  shipped tables; element and keyword strings (`control_parameter`, `data_type`, `default_value`, `min_value`,
  `max_value`, `none`, `false`, `bool`, `integer`, `float`, `source_name`) and parameter literals read from the
  image; two Lua native names read from the native registration tables; one RTTI type-descriptor name
  (`dc_object_rig_registration`); and the class names already quoted by the scoping note.
- Self-check, run on the finished file as the last step: the project's standard auto-name regex plus the
  plain-substring backstop list. The U0 pass recorded its result in the final report, not here.
- **U1 (§6), the same discipline.**
  - Only capstone listings (recursive descent over single bodies), raw rel32 and dword scans, static reads of
    constants and string tables, and a regex census of extracted shipped tables were used. No Ghidra project was
    opened and no decompiler output was read.
  - Additional names quoted as data:
    - the 14 clip-node alias strings of the table at `0x01151c90`;
    - two preset State/Action names from `0x011507f8` (`stun flinch`, `react sitting shocked`) and two that are also
      blend-tree row names (`FreeFalling_AIM`, `FreeFalling_Dive_AIM`);
    - the element names `alias_name`, `clip_name`, `match_name` and `loop_flag` read from the image;
    - the `anim_files.xtbl` flag names quoted from `spec-tables-animation.md` §3.2;
    - the `anim_blend_trees.xtbl` element names quoted from §7 there.
  - **Self-check result on the whole finished file (U0 and U1): 0 hits** for the standard auto-name regex and 0 for
    the plain-substring backstop list (run 2026-10-03 as the last step; the run is described in the final report).
- **U2 (§7), the same discipline.**
  - Only these were used:
    - capstone recursive-descent listings of single bodies, started from vtable entries where no function is defined
      in any database;
    - raw rel32 and dword scans;
    - static reads of string and pointer tables;
    - a Python re-implementation of the parsers, run over the extracted shipped files.
  - No Ghidra project was opened or copied (`tools/gp_animU2` was never created), and no decompiler output was read.
  - Additional names quoted as data:
    - the node-graph element and keyword strings read from the image and the shipped files (`start`, `conditions`,
      `parameter1`, `source_name`, `multi way blender`, the operand-type and comparison words, the operator tokens, and
      so on);
    - the RTTI type-descriptor names of the 23 classes;
    - shipped graph, node, parameter, filter, trigger and State/Action names, and graph-file and table file names;
    - example expressions quoted from shipped files.
  - **Self-check result on the whole finished file (U0, U1 and U2): 0 hits** for the standard auto-name regex and 0 for
    the plain-substring backstop list (run 2026-10-03 as the last step; the run is described in the final report).

- **U3 (§8), the same discipline.**
  - Only these were used:
    - capstone recursive-descent listings of single bodies, started from vtable entries;
    - raw rel32 scans and a heuristic byte-store scan;
    - static reads of constants and jump tables;
    - a Python re-implementation of the evaluators, run over the extracted shipped files.
  - No Ghidra project was opened or copied (`tools/gp_animU3` was never created), and no decompiler output was read.
  - Formulas are written in this file's own notation, not transcribed from any listing.
  - Additional names quoted as data:
    - shipped transition, node, filter and parameter names and example expressions;
    - the element names `cancelable_after`, `cancelable_before`, `cyclic_percent`, `sync_controller_transform` and
      `max_blend_rate`;
    - the strings `true`/`false` read at `0x004b8b26`.
  - **Self-check result on the whole finished file (U0–U3): 0 hits for the standard auto-name regex and 0 for the plain-substring backstop list** (run 2026-10-03 as the last step).
- **U5 (§9), the same discipline.**
  - Only these were used:
    - capstone recursive-descent listings of single bodies;
    - raw rel32 and dword scans, and the linear-sweep displacement scan;
    - static reads of tables, constants and strings;
    - emulation of the image's own code (Unicorn, with no game state) on chosen inputs;
    - a regex count over the extracted shipped tables.
  - No Ghidra project was opened or copied, and no decompiler output was read.
  - Formulas are written in this file's own notation, not transcribed from any listing.
  - Additional names quoted as data:
    - the bone names `spine`, `spine1`, `spine2`, `neck`, `head`, `r-upperarmtwist1`, `l-upperarmtwist1`, `r-eye`, `l-eye`, `spinebend` and `pelvis`;
    - the trigger names `Spinebend`, `free move`, `start correction begin`/`end`, `dest correction start`/`end`, `rot dest correction start`/`end`, `force use velocity start`/`end`, `movement lock`/`unlock`, `face camera start`/`end` and `invalid synched target start`/`end`, read from the image and `anim_triggers.xtbl`;
    - animation file names and prefixes from `anim_files.xtbl`;
    - the flag names `Anim_Movement` and `Additive`.
  - **Self-check result on the whole finished file (U0–U3, U5): 0 hits for the standard auto-name regex** (run 2026-10-04 as the last step; re-confirmed 2026-10-07 after insertion).
- **U4/U6/U7 (§§10–12) and the §6/§11.7 phase-rate-gone contradiction fix, same discipline.** U4, U6 and U7 used
  only capstone listings over direct byte reads (`tools/harnesses/exe_va.py`), raw rel32/dword scans and static
  reads; no Ghidra project was opened for any of the three, and no decompiler output was read (each unit's own
  changelog entry records its "0 hits" result individually). The 2026-10-07 contradiction fix between §6.6–§6.8
  and §11.7 (below) added full capstone listings of `0x004c7510`, `0x004be510`, `0x004bdd40`, `0x004c6350`,
  `0x004c60a0`, `0x004ca5f0`, `0x004c45a0` and `0x004c3d20` to the read set, all via `tools/harnesses/exe_va.py` +
  `capstone`, no Ghidra. **Self-check result on the whole finished file (U0–U7, after this fix): 0 hits for the
  standard auto-name regex and 0 for the plain-substring backstop list** (run 2026-10-07 as the last step of the
  fix).

---

## Changelog

- **2026-10-03 — created (U0).**
  - Registry record layout and type codes settled.
  - Graph-scoped resolver identified.
  - Setters (including the integer setter), child ring, three lazy-allocation sites, and the default-seed /
    per-frame copy into graph instances.
  - Full writer census: thirteen functions, 62 names.
  - Field → parameter table for `0x00958690`.
  - Validation of 87 shipped rows against code and the 249 graph files.
- **2026-10-03 — U1 appended as §6 (controller state slots and the play / query API); the cleanroom check renumbered
  to §7.**
  - Full `0x84`-byte slot layout and flag table; flags `0x02`/`0x04` relabelled.
  - Compact-array invariant, grow / free / reaper; no eviction except in `0x004b2280`.
  - Id-space bounds 7,589 / 7,603 and the 14 named clip-node ids.
  - The three play entries compared, plus `0x004b1ef0` and `0x004b2a20`/`0x004b2b50`.
  - The four queries, with signatures.
  - The cross-fade formula: rate (1/30)/duration, crowding factor 0.5 + 0.2·(n−1)², lowest-weight-first
    redistribution, quadratic easing.
  - The re-trigger machinery, which appears inert in shipped data.
  - The blend-tree evaluator's `State` path.
  - The graph step re-read.
  - §5.1 items 1, 6 and 8 closed and item 2 answered; OPEN items 10–17 added; corrections to U0 §3.4, §3.5 and §4.2
    added to §5.2.
- **2026-10-03 — U2 appended as §7 (the node-graph file format); the cleanroom check renumbered to §8.**
  - Population: 249 files, 122 state machines and 127 blend trees, plus one patch copy. Every file is accounted for.
  - The loader's four passes, the `0x30`-byte graph object, and the graph → animation-record link by `name` CRC.
  - The grammar of graph headers, state nodes and the shared clip block, transitions, the `start` expression language
    (tokenizer, tree builder, left-to-right fold with no precedence), operands with their 14 types, `conditions`, the
    six blend-node kinds, `child_node_graph` resolution, the alias and filter collector, and the
    `control_filters.xtbl` binding.
  - 23 classes mapped by vtable slot.
  - Full-population validation: all 249 files parse with no crash path. Data defects listed. Multiplier filters, three
    operand types, `!=` and `xor` are unused.
  - §5.1 item 5 answered (§7.13); OPEN items 18–24 added; corrections to the scoping note in §5.2; side findings in
    §5.3.
- **2026-10-03 — U3 appended as §8 (the node-graph runtime); the cleanroom check renumbered to §9.**
  - The evaluation entry `0x004cc6c0` and its two passes; node instances, the 32-entry output list (its unchecked
    capacity is a stack-smash hazard; shipped maximum 30), garbage collection and resets.
  - State machines: chained completions, in-flight targets, the transition test's codes, rewind and cancel windows, and
    the linear `length` cross-fade with nesting.
  - The left-to-right fold confirmed in the evaluators and replayed over all 304 trees (U2's four traps, exactly).
  - Operand values, comparison typing and the comparator.
  - Clip blocks: lazy `clip_name` cache, per-character `tag_name` mapping, clamped clip time, `loop_flag` polarity
    corrected.
  - Merge (union), switch (shared selector), multi-way blender (first-segment curves, `max_blend_rate` as a duration).
  - The four filter formulas; the double-evaluated `Turn Angle Delta Multi`.
  - U0 items 1–3 closed; items 17, 20–23 answered; OPEN items 25–31 added; corrections to U2 and the scoping note in
    §5.2; side findings in §5.3.
- **2026-10-03 — layer-flag cross-reference (SPEC TEAM, from the `spec-player-runtime.md` §5.4.3 T/E work).** §6.2: a paragraph separating the slot flag word from the instance layer flag word (layer `+0x10`, seen as instance `+0x80`), whose bit `0x01` is the phase-synchronised group mark. §6.3: the reaper's "gone" test `0x004bdd40` corrected — the second condition is no play time left (`0x004c6350`), not zero weight.
- **2026-10-07 — U5 appended as §9 (root motion and the post-sample adjusters); the cleanroom check renumbered to
  §10.**
  - The post-sample step `0x004cfb60` and its ~3.3k-instruction module identified as the human's spine-bend and
    look chain (nine bones, nine-entry joint table with per-axis limits, shares and masks; headroom, proportional
    distribution and a shared follower; per-bone rotation overrides consumed by the compose step), not root
    motion or foot alignment — corrects `review/scoping-animation.md` §1.6, §1.7, §2.1 and §4.2.
  - The correction step `0x004b6530` identified as a motion-warp and heading-correction step confined to authored
    trigger windows (`start correction` / `dest correction`); outside them it returns the orientation unchanged.
    X (controller `+0x70`) feeds only the start-correction warp. Corrects `spec-player-runtime.md` §5.0 item 4,
    §5.3 and §5.4.5.
  - The composer's remaining animation-side inputs resolved: `0x004b49e0` (clip speed), `0x009987d0` (the
    `force use velocity` trigger window), controller `+0x1a4`'s full bit table, `0x004cad00` (the root-track
    override value), `0x004c6ebb` (an in-place clip swap with cross-fade) and `0x004c8a80` (identified as the
    bound-track rotation decoder, routed to `spec-anim-format.md` for its bit-for-bit read).
  - Validation by emulation (Unicorn, no game state) of the Euler/heading helpers, the follower and the whole
    chain step on a synthetic object.
  - U1 items 11, 13 and 14 advanced or closed; `spec-player-runtime.md` OPEN 39 answered except for the items
    re-scoped as OPEN items 32–39 here; corrections to the scoping note and to `spec-player-runtime.md` in §5.2 and
    §5.3; `spec-player-runtime.md` was edited directly for OPEN 39 and the §5.4.5 correction.
- **2026-10-07 — two §8 precision corrections, raised by Team B's graph-loader/evaluator merge** (SPEC TEAM). The output-list hazard bullet and §8.10's "Output-list bound" paragraph now separate two facts that were conflated as one "margin of two" claim: the format's own worst case is **31** (not 30) of 32, because nothing ties a nested state machine's own in-flight transition to its parent's — `ride_left_sm`/`ride_right_sm`'s nested `ride_left_ready_sm`/`ride_right_ready_sm` (itself already at 30) plus the parent's own in-flight transition out of `Ready` reaches 31; the as-shipped, as-evaluated peak stays 30, since no real graph drives both at once. Re-derived from Team B's own real-data test over all 249 shipped files (steady-bound and recursive readings both swept), which independently reproduces every other census figure in this section. Separately, §8.10's ramp-timing line is corrected from 12 to **13** updates for `max_blend_rate` = 0.4: settled by disassembling the real evaluator `0x004bc0d0` (pure SSE/SSE2, no x87) rather than by abstract reasoning — the running blend weight is stored and reloaded as a 32-bit float every evaluation, so after 12 updates it sits one float32 ULP short of 1.0, and the 13th update is what actually snaps it to 1.0. Cleanroom self-check on the entire file: 0 hits, re-run after these edits.
- **2026-10-07 — U7 added as §10 "Other controllers on demand"** (SPEC TEAM). The five non-human animation clients: the vehicle ride-state collision writer `0x00b09ab0` (cited from `spec-player-runtime.md` §8.5, not re-derived — exactly reuses U0); rider lean `0x00af4b80` (not investigated, owned by the concurrent Player U6.8); the vehicle's own controller `0x00a760e0`/`0x00a76690` (the identical U0 parameter API, but a genuinely new post-compose per-bone matrix overlay from vehicle-type tables, and a different call convention — direct inline dispatch rather than the shared pose-request queue); `object_rig` `0x008cb9f0` (resolves the scoping note's own "child rig following a parent, such as clothing" HYPOTHESIS: the generic attached-part placement controller, driving named parts' physics bodies or calling a virtual placement method, matching props/held-items/clothing rather than destructibles); and the two previously-unidentified pose clients, now both identified — `0x00b8fef0` is the held-weapon pose driver (up to 5 weapon-slot controllers, bone cap 100 not 128), and `0x00900540`/`0x00921fa0` turn out to be the SAME generic "secondary rig" helper applied to two different object-manager lists, not one object type as the scoping note's grouping implied (`0x00900540`'s list is HIGH for a motorcycle-linked secondary rig, cross-referencing the bike-riding gate `0x009b9160`; `0x00921fa0`'s list's object category stays unidentified). Verdict on "reuses U0–U4 unchanged": true for the literal function surface across all five clients, not true as a description of overall behavior — the vehicle and `object_rig` both carry real, non-trivial post-compose additions; the weapon driver and both generic secondary-rig helpers are genuinely thin wrappers. Former §10 (Cleanroom check) renumbered to §11; OPEN items 40–48 appended to §5.1. Cleanroom self-check on the entire file: 0 hits, re-run after these edits.
- **2026-10-07 — U4 added as §11 "Time, layers and the pose hand-off"** (SPEC TEAM). The pose-request queue fully traced (`0x004bf4c0` append, `0x004bf360` dispatch — step-bit `0x10` selects inline-call vs. background scheduler job, `0x004bf270` the per-job body running advance/sample/post-sample-adjust/compose against an 8,192-byte local stack buffer, not the instance's own storage). The full `+0xcd2` human-poster mode table read in every branch: modes 1/2 are identical physics-band calls with no queue interaction; mode 4 is a distinct physics call with a scripted-impulse branch, also no queue interaction; mode 5 and the switch's own out-of-range default share the identical empty-epilogue target (settles "why mode 5 posts nothing"); modes 0/3/6 converge on a shared finisher after diverging only in which instance handle they post (mode 0 against a separate holder field `+0x14`; modes 3/6 against the controller's own instance via `+0x10`→`+0x20` — structurally separate handles, confirmed by full listing, resolving scoping's own OPEN item). Compose's two functions fully disentangled: `0x004c31b0` is a per-slot setup/working pass (attachment ordering via a new "composed this frame" flag, the manual-override application, bounding-box accumulation) and `0x004c36f0` is the real bone-hierarchy concatenation (a remap-table straight-copy path for attached children, and a fresh-vs-cached branch on a per-rig-bone generation gate, chaining each bone to its parent's own just-written matrix) — resolving the model-space-vs-skin-space OPEN item at model-space. Also resolved three `spec-anim-runtime.md` §5.1 items (14, 25, 31) explicitly marked "U4's question": the held-layer mechanism at instance `+0x5c`/`+0x6c` bit `0x01`, and two new layer-record fields (`+0x18` explicit phase override, `+0x1c` cached phase offset). Several new instance/layer fields named (`+0x04` working matrix array, `+0x38` composed-flag, `+0x6e`/`+0x350` manual override, `+0x348` lazy per-bone constraint array, `+0x370`/`+0x380` bounding box). Corrections folded into `review/scoping-animation.md` directly (§1.6's `+0x74`/`+0x70` ambiguity, the mode-table paragraph, the compose model-space/skin-space paragraph, §4.2 items 4/5, and marking §5's U4 entry done). 13 OPEN items appended (§11.10). Cleanroom self-check: 0 hits, re-run after these edits.
- **2026-10-07 — U6 added as §12 "Human procedural modifiers"** (SPEC TEAM). Aim/look (`0x009b4360`, 1168 insns) confirmed as a per-human bearing calculator feeding a smoothed pitch/yaw writer (`0x009b0870`), not a spatial solver — it correctly cross-references the already-landed controller slot fields (§6.1/§6.2) to avoid double-counting a turn the current clip already supplies, and the shared scalar solver `0x004cd8c0` is confirmed (a generic damped-approach function, not IK — caution against confusing it with the merely-adjacent `0x004cd810`). **Major correction to the scoping note's own grouping**: the real spine-bend chain is only `0x009b34f0`→`0x009b0e90` (a continuously-running, timer-driven randomized idle-sway system with two interacting state machines, not a one-shot bend); `0x009a0a90` (weapon-sway/aim-settle rate, HYPOTHESIS) and `0x009b0ad0` (a global weapon-recoil/aim-kick history ring, HYPOTHESIS) were found to be unrelated procedural modifiers that merely share the same call site and gate, not spine bend at all. The situation code `0x009b20a0` corrected from "a per-joint mask" to a four-valued enumerated suppression code. Independently verified Animation U5's four shallow citations into this unit's own functions: only one of four held up cleanly. Corrections folded into `review/scoping-animation.md` directly (4 annotated spots). 4 OPEN items appended (§12.7), one flagging a possible dual role for human `+0xac8` shared with U4's pose-request-queue owner field. Cleanroom self-check: 0 hits, re-run after these edits.
- **2026-10-07 — §6/§11.7 contradiction resolved: `0x004c7510`, `0x004be510` and `0x004bdd40`** (SPEC TEAM, raised
  by Team B while testing a port against the real executable). §6.6–§6.8 (landed first, already matched by Team
  B's working port) and §11.7 (landed later, during the U4 pass immediately above, with no cross-reference back
  to §6) disagreed on which of `0x004c7510`/`0x004be510` sets a layer's phase versus its rate, and §11.7 also
  disagreed with §6.3 — and, inside itself, with its own `0x004c6350` entry — on what `0x004bdd40`'s "gone" test
  actually checks. Independently re-derived from the disassembly (`tools/harnesses/exe_va.py` + capstone, no
  Ghidra; full listings of all eight functions below), without deferring to either existing section or to Team
  B's port:
  - **`0x004c7510` is the PHASE setter, confirmed.** §6.6–§6.8 were right; §11.7 had called it "the rate setter"
    and named its argument `rate`. The argument is a phase value: the function computes `instance_phase − arg`
    (wrapped), caches that offset at layer `+0x1c`, and immediately re-snaps the layer's time fields to match —
    the net effect, confirmed from the caller side (§6.7 step 6), is that the layer's *effective* phase becomes
    `arg` at once. It never multiplies by elapsed time, which a true rate setter would have to.
  - **`0x004be510` is the RATE setter, confirmed.** §6.6/§6.8 were right; §11.7 had called it "the explicit-phase
    setter" and named layer `+0x18` an "explicit per-layer phase override". `0x004be510` stores its third
    argument at layer `+0x18` completely unmodified, with no reference to phase anywhere in its body. Layer
    `+0x18` is read as a rate — multiplied by elapsed ticks, with a carried fractional remainder at layer
    `+0x28` — by both per-frame advance paths: `0x004c45a0` (layers outside the phase-synchronised group) and
    `0x004c3d20` (synchronised-group members carrying layer flag `0x20000`, the "own clock" exception
    `spec-player-runtime.md` §5.4.3 already named off this same field).
  - **`0x004bdd40`'s "gone" test: §6.3 was right, §11.7 was wrong by omission, and §11.7's own `0x004c6350` entry
    was also wrong.** §11.7 described `0x004bdd40` as a one-argument, handle-only validity check. It is actually
    `(handle, key)`: past the handle check, it resolves the layer by `key` and returns false at once if the layer
    is in the phase-synchronised group, otherwise falls back on `0x004c6350`'s remaining-play-time-in-seconds
    result (true iff exactly `0.0`) — matching §6.3's already-corrected (2026-10-03) reading exactly, which this
    pass independently re-confirmed by full listing rather than taking on faith. §11.7's separate `0x004c6350`
    entry called it a "weight-or-zero" query and `0x004c60a0` "a layer/weight resolver"; neither resolves or
    returns a weight anywhere — `0x004c60a0` resolves a layer pointer, and `0x004c6350` computes remaining play
    time from the clip cache, exactly as §6.3 already said.
  - Team B's port matching §6's framing was a real, independently-weighed data point going in, not the basis for
    this resolution — it turned out to be correct for the right reason (the disassembly), not by coincidence.
  - **Fixed in place** with this project's strike-and-append convention: §11.7's five affected entries (the three
    functions plus the field-map rows for layer `+0x18`/`+0x1c`, §11.8) now carry `~~struck~~` originals followed
    by **RESOLVED (2026-10-07)** corrections with the evidence inline; §6.3 and the lead-in to §6.7 each gained a
    short cross-reference to §11.7's correction, and §11.7's item-14 lead-in gained one back to §6.3/§6.6–§6.8, so
    this unit's two sections can no longer silently disagree about the same three functions.
  - Cleanroom self-check on the entire file, run as the last step of this fix: 0 hits for the standard auto-name
    regex and 0 for the plain-substring backstop list. Synced to Team B (`diff -q` byte-identical).
- **2026-10-08 — cross-layer blend arithmetic (§11.5a, new).** Full listing of the pose sampler `0x004c21a0`
  (4,081 bytes / 1,019 instructions) confirms cross-layer blending happens entirely inside the sampler, before
  compose runs — a terminology sharpening of §11.5's existing framing, not a contradiction. Two genuinely
  different per-bone combination mechanisms, selected by layer flag `0x2000`: a component-wise weighted-average
  LERP for override layers, and a true additive delta-apply (vector subtract/add for translation, SSE
  quaternion-multiply/conjugate for rotation) for additive layers. Corrects an external assumption (not previously
  written into any spec-*.md file) that cross-layer blending reuses the root-motion walker's own masking formula
  verbatim (`spec-player-runtime.md` §5.4.3, `0x004c58c0`): the ingredients are shared but the masking expression
  differs (per-group multiplicative ratio here vs. per-layer subtractive mask there); the one piece that is a
  genuine, confirmed shared primitive is the additive-rotation conjugate constant `0x011942a0`. New field-map rows
  added (§11.8); 5 new OPEN items appended (§11.10 item 14). Cleanroom: 0 hits, standard regex. Synced to Team B.
