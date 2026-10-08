# Scoping note — the animation runtime: from named control parameters to a posed skeleton

**Date:** 2026-10-03 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.
**Question:** what happens to the named animation control parameters that the per-human writer `0x00958690` sets every frame (`review/scoping-player.md` §1.4); where they are read; how a parameter name becomes a runtime slot; what turns the result into bone transforms; where that pose becomes available to the renderer; how big this system is; what is already specified; and in what order to specify the rest. Team B already has CPU skinning and clip playback, so the note concentrates on the missing bridge between "parameters written" and "clips chosen, weighted and timed".
**Method:** Ghidra-free, same toolchain as `review/scoping-player.md`:
- direct byte reads of the executable (`tools/harnesses/exe_va.py`) with capstone listings of individual function bodies, plus linear listings of table-slot targets the shared project never defined;
- the read-only function / call / data-reference / string-reference census and the RTTI table exported earlier this session from the shared Ghidra project (function bodies and instruction counts are the shared project's own; no decompiler output was used anywhere);
- raw dword scans of the image for string and table addresses, push surveys at call sites, a raw PE-import read, and a whole-image string census for middleware signatures.
- No Ghidra project was opened or copied for this pass, so there is no private project to delete.
- Documents read for cross-reference: `review/scoping-player.md` (whole), `review/scoping-physics-vehicle.md` §3, `spec-ai-runtime.md` §1, `spec-tables-animation.md` §1–§7, `spec-tables-ui-controls.md` §14, `spec-tables-traffic-ai.md` §22–§23, `spec-xtbl-format.md` §8 (corrections paragraph), `spec-anim-format.md` §1–§5 and §10, `spec-rig-format.md` §11.2 and §11.16, `spec-lua-api-behaviour.md` §7.11, §15.2, §18.5, §18.8, §21.15, §21.28, §22.29, §28.23, and `review/exe-notes-2026-10-01/interp_tabs.md` Q13.

**Size figures.** Instruction counts are function-body counts from the shared project. "Closure" counts unique functions reachable from the listed roots, restricted to the named code band unless marked "any band", skipping any single callee over 1,500 instructions. "The animation library band" means `0x004ad000`–`0x004d2000` (§2). Many virtual-slot targets in that band have no function defined in the shared project (38 of the 57 slot targets of the node and filter classes, §6); those are cited by address without a count, and band totals undercount by roughly their size.

**Confidence labels** are the project's usual ones:
- **CONFIRMED**: read directly in this pass (a listing, a static table read, or a census query).
- **HIGH**: inferred from structure (call shape, field use, string anchors), not read line by line.
- **HYPOTHESIS**: a plausible reading, not checked.
- **OPEN**: not settled; collected in §4.2.

**Naming.** Class names (`state_machine_info`, `blend_tree_info`, `clip_node_info`, `control_filter`, `linear_map_filter`, `object_rig`, …) are the binary's own RTTI type-descriptor strings. Control-parameter names (`desired speed`, `move forward bool`, …), node-graph element names (`state_machine`, `transitions`, `max_blend_rate`, …) and source-path strings are engine data read from the image. All are shipped data, quoted as elsewhere in this project. Descriptive role words used below ("controller", "instance", "pose request") are this note's own labels, not engine names.

---

## 0. Verdict up front

**This is Volition's own animation library ("animlib", one source-path string), statically linked, with no licensed animation middleware anywhere in the image. The boundary that matters is not a vendor edge but the line between authored data — `.anim_pc` clips, the `anim_*.xtbl` tables, `control_parameters.xtbl`/`control_filters.xtbl`, and about 250 node-graph files — and the runtime that evaluates it. The data side is roughly half specified; the runtime is not specified at all, and its single largest unknown is the node-graph file format.**

1. **No animation middleware** (CONFIRMED, §3).
   - No `hka`/`hkb` RTTI class (the Havok families present are only `hk` and `hkp`); no Havok Animation or Behavior source path; the Havok product keycode names Physics only.
   - The only three `hka…` strings in the image are job-type names inside Havok Base's job-queue timer table (one undefined-code site at `0x00bf6a73` in Havok range A), not code that is called.
   - No Morpheme, Euphoria, Granny, EmotionFX, HumanIK, Edge or FaceFX signature; no animation DLL among the 16 imported DLLs.
   - The animation library's own source path is under the studio's shared engine tree (`…\ctg\src\lib\animlib\anim_core\anim_core.cpp`), next to `vlib` and `render_lib`.
2. **A control parameter is a small global registry entry, resolved by name once and cached** (CONFIRMED, §1.3).
   - `control_parameters.xtbl` is read by **`0x004cae20`** (61; per row `0x004d00e0`, 133) into a registry of `0x14`-byte records at the pointer `0x03171bcc`, with a 16-bit count at `0x03171bc8`. This is the slot-12 reader `spec-tables-ui-controls.md` §14 says is not identified.
   - Name → id is a linear case-insensitive compare, **`0x004cbaf0`** (28). Callers cache the 16-bit id in function-local statics (`0x00958690` keeps 31 of them at `0x02624ac4`…`0x02624b3c`, with the guard mask at `0x02624b40`).
   - Values live in an **8-byte-per-parameter block** at `+0x28` of the per-character animation controller. They are written by typed setters `0x004b1410` (bool) and `0x004b1510` (float), which check the registry type (1 = bool, 3 = float) and silently drop a type mismatch, an id of −1, or a missing block. Every write is repeated first on the controller's child ring at `+0x08`.
   - Nine game functions write parameters, about 56 distinct names in all. `0x00958690` is the human locomotion/aim/combat writer; the others cover jump and climb, vehicles (their own controller), bike lean, and collision.
3. **The parameters are consumed by a node-graph network, not by the clip sampler directly** (HIGH, §1.4–§1.5).
   - The game's own RTTI classes `node_graph_info`, `state_machine_info`, `blend_tree_info`, `state_node_info`, `clip_node_info`, `merge_node_info`, `switch_node_info`, `multi_way_blender_node_info`, and five `control_filter` classes (`linear_map`, `delta_multiplier`, `multiplier`, `cap`, the base) form a data-driven state-machine / blend-tree system loaded from `node_graph_files.xtbl`.
   - A controller's **state slot** whose animation record carries bit `0x80000000` gets a **node-graph instance** (a 128-entry pool of `0x4c`-byte rows at `0x03133a90`). The per-character parameter block is allocated only at that moment (`review/exe-notes-2026-10-01/interp_tabs.md` Q13.2 step 5, which reads the same count global as "bone count" — it is the parameter count, §6).
   - Filters read parameter values through a per-graph alias table into an 8-byte-per-slot value array (`0x004d1820`, CONFIRMED by linear listing).
4. **Per frame, three stages run inside the world update's animation step** (CONFIRMED as call edges, §1.1–§1.2, §1.6):
   - (a) `0x0095b1a0` writes the parameters (`0x00958690`) and immediately runs the **controller update `0x004b3720` (644)**: state-slot cross-fades, 1-D blend trees from `anim_blend_trees.xtbl` (`0x004b0080`), and node-graph instance updates (`0x004cc000`);
   - (b) `0x009a15f0` posts **pose requests** (28-byte entries at `0x02eefae0`) with four step bits: advance time, sample, post-sample adjust, compose;
   - (c) `0x004bf360` (the `anim_core.cpp` site) runs each request as a scheduler job: **time advance at 4,800 ticks per second** (`0x004c4810`), the **layer sampler `0x004c21a0` (1025)** already documented by `spec-anim-format.md` §10, a post-sample adjuster `0x004cfb60` (HYPOTHESIS: root and ground alignment), and **compose `0x004c36f0`**.
5. **The pose hand-off to the renderer is a 48-byte-matrix array at `+0x00` of the `0x3a0`-byte animation instance record** (pool `0x0344c924`, count `0x0344c91a`). That is exactly the skin gather's `source` in `spec-rig-format.md` §11.16.
   - This pass finds what that section records as not located, the **per-frame writer**: `0x004c36f0` stores 48-byte blocks through `[record+0x00]` for each of the binding descriptor's `N` slots (CONFIRMED as stores; role HIGH).
   - For Team B, this is the natural seam: everything upstream of it is the bridge this note scopes; everything downstream is skinning they already have.
6. **Root motion versus velocity is not settled** (OPEN, §1.7).
   - Clips carry a root-motion delta (`spec-anim-format.md` §4), and `anim_files.xtbl` flags include `Anim_Movement`, `Block_Translation` and `Block_Rotation`.
   - The post-sample step reads one bone of the sampled pose, keeps a per-frame delta, and masks individual axes. Whether the character controller moves the capsule from that delta or from `desired speed` was not traced.

**Rough size:**

| Part | Functions | Instructions | Needs RE? |
|---|---|---|---|
| Parameter bridge: registry, lookup, setters, and the nine writers (human, jump/climb, vehicle, bike, collision, by-name) | ~20 | ~3.5k own (`0x00958690` alone 1.2k) | Yes, small; the inputs side is `review/scoping-player.md` U3 |
| Per-human animation step `0x00994970` and its ten children (controller hand-off, aim/look, spine bend, pose posting) | 11 roots | ~4.9k own; ~18k at two levels in the human band | Partly; the human-specific modifiers are separate units |
| Controller: state slots, cross-fade, play/query API, 1-D blend trees | 82 (band) | ~10.0k | Yes, medium; the play entry `0x004b1b90` is already traced (interp_tabs Q13.2) |
| Node-graph definitions: `state_machine` / `blend_tree` parse and link, node classes | 52 (band) | ~4.4k (+ undefined slot targets) | Yes; **the file format is the largest data-side gap** |
| Parameters, filters, `node_graph_files` loaders, graph-instance pool, groups | 107 (band) | ~5.9k | Partly; the `control_*` schemas are done |
| Animation instance (`0x3a0` record) handle API | 160 (band) | ~4.9k | Mostly accessors of ≤40 instructions; small |
| Time advance, sampler, compose, clip cache | 130 (band) | ~11.4k | Partly; clip decode and the override table are `spec-anim-format.md` |
| Post-sample adjusters (root / IK family) | 22 (band) | ~3.3k | Yes, medium |
| Animation-table loaders (`anim_*.xtbl`) | 24 (band) | ~3.1k | **No — specified** (`spec-tables-animation.md`) |
| **Animation library band in full** | **577** | **~43k** | |
| Other pose clients (vehicles, `object_rig`, two unidentified world-update clients, the character-movement second pass) | 8 roots | ~2.9k own; ~10k at one level | On demand |

So the animation runtime is **about 45–50k instructions**, comparable to the player system's measured root set (~74k) and the game-side physics (~70k), and four to five times the UI-render core. About **6–7k of it is already specified** (the `anim_*` table loaders, the clip decoder and the layer-override table); everything else is not.

**The smallest slice that makes the named parameters move a character's animation** is U0–U3 of §5: the parameter contract, the controller's state slots, the node-graph file format, and the graph runtime for the locomotion graph. That is roughly **15–20k instructions of code plus about 250 small readable data files**, ending at the layer list that Team B's playback already consumes. A port that accepts hand-authored locomotion blending can drive Team B's playback straight from the U0 parameters and defer U2–U3 (the decision point in §5).

---

## 1. The pipeline: from a written parameter to a pose the renderer can consume

### 1.0 Shape in one picture

Call edges CONFIRMED by listing or census; roles as labelled in §1.1–§1.7.

```
world update 0x00702a50 (324)
  ├─ @0x00702b52 0x00b8fef0 (75)        pose client, posts and flushes its own requests        [unidentified]
  ├─ @0x00702b84 player list 0x009ea630 ┐ each human: 0x00994dd0 (48) submits job 0x00994db0 → 0x00994a80 (208)
  ├─ @0x00702b90 NPC list    0x0099bba0 ┘   (source-path string human_multithreaded.cpp)
  ├─ @0x00702b95 0x004bf490 (9)          clear the pose-request queue
  ├─ @0x00702b9f 0x00994e80 (64)         finish unrun human jobs, wait on the scheduler (0x00dc22c0, 0x00dc19a0), then
  │     for every human on the object manager's +0x1e4 list with +0x33 bit 0x08:
  │       ├─ 0x00994970 (88)  per-human animation step, SERIAL (§1.2)
  │       │     ├─ 0x0095b1a0 (573) ─┬─ 0x00958690 (1187)  write named parameters (human in ESI)        §1.3
  │       │     │                     └─ 0x004b3720 (644)   controller update ([[human+0xd24]+0x10])   §1.4
  │       │     ├─ 0x009b4360 (1168), 0x009a0a90 (574), 0x009b0ad0 (201), 0x009b34f0 (323)   aim / look / spine bend [HYPOTHESIS]
  │       │     └─ 0x009a15f0 (394)   post pose requests via 0x004bf4c0 (15)                          §1.6
  │       └─ 0x004bf360 (91)  dispatch the posted requests as scheduler jobs → 0x004bf350 → 0x004bf270 (81)
  │             ├─ bit 1  0x004bcfb0 (20) → 0x004c4810 (115)    advance time (4,800 ticks/s)
  │             ├─ bit 2  0x004bd0b0 (22) → 0x004c21a0 (1025)   sample and blend layers into an 8 KB local pose
  │             ├─ bit 4  0x004cfb60 (259)                       post-sample adjust (root / IK family) [HYPOTHESIS]
  │             └─ bit 8  0x004bd0f0 (22) → 0x004c2170 (18) → 0x004c31b0 (361), 0x004c36f0 (215)
  │                                                               compose → matrix48[N] at instance +0x00   §1.6
  ├─ @0x00702bab 0x00997880 (9)          clear the queue again, scheduler sync
  ├─ @0x00702c16 0x008cbfe0 → 0x008cb9f0 (239)   object_rig animation (same instance API)
  ├─ @0x00702c46 physics frame 0x0075ac20
  ├─ @0x00702c91 ride animation 0x00af8f70
  ├─ @0x00702c9b character movement 0x0099ebe0 (1482)  second pose pass: 0x009ef120 (511) (own clear / post / flush)
  │                                                     and 0x009999b0 (231) (direct advance / sample / compose)
  ├─ @0x00702ca0 0x00929da0 → 0x009296a0 → 0x00921fa0 (70)   pose client                     [unidentified]
  ├─ @0x00702cb9 0x00900990 (71) → 0x00900540 (316)          pose client                     [unidentified]
  └─ @0x00702cdc 0x008fb7b0  (a jump to 0x004bf490)          end-of-frame queue clear

renderer side: skin gather reads instance +0x00 (spec-rig-format.md §11.16) → palette load 0x00e7ad30 / 0x00e7aff0
```

### 1.1 Where animation sits in the frame

- **One correction to the frame picture in `review/scoping-player.md` §1.0/§1.4** (CONFIRMED by listing of `0x00994e80`, `0x00994dd0`, `0x00994db0`).
  - The scheduler fan-out is **not** the animation step. Each human's job, submitted from the player and NPC list walks through `0x00994dd0`, runs `0x00994a80` (208; callees in the camera band and `0x0095cbc0`; role OPEN).
  - At `0x00702b9f`, `0x00994e80` first runs any job that has not run yet on the calling thread, then waits for the job group. Only after that does it walk the humans **serially**, calling `0x00994970` and then `0x004bf360` for each one.
  - So parameter writing and the controller update are serial per human. Only the pose work for human *n* (time advance, sample, compose) overlaps the serial step for human *n*+1.
- **The ordering is fixed** (CONFIRMED from the world update's call sites above):
  - the player and NPC updates run before the animation step, so the AI tick and the player controls have already written this frame's intent;
  - the animation step runs before physics;
  - character movement runs after physics and may sample poses a second time.

  A port that moves the parameter write after the controller update, or the controller update after physics, introduces a one-frame lag between intent and pose.
- **The pose-request queue is cleared four times per frame:** before the animation step (`0x00702b95`), after it (`0x00997880` at `0x00702bab`), inside character movement (`0x009ef120` at `0x009ef2f0`), and at the end (`0x008fb7b0` at `0x00702cdc`). Requests never survive a frame (CONFIRMED as call sites; HIGH as the reading).

### 1.2 The per-human animation step `0x00994970`

CONFIRMED by full listing (88 instructions). In order:
1. `0x0099e840` (265) and `0x00986400` (53). Callees in the camera, cutscene and pause predicates; role OPEN.
2. **`0x0095b1a0` (573)**, which ends with `0x00958690` (parameters) followed immediately by **`0x004b3720`** on `[[human+0xd24]+0x10]`, the controller. That tail is CONFIRMED by listing at `0x0095b8fe`/`0x0095b90d`. Earlier in the same function come the state queries and play requests on the same controller (`0x004b4ce0`, `0x004b41b0`, `0x004b2280`, `0x004b21a0`, `0x004b2a20`) and the anim-group lookup `0x004b1630`. HIGH: those are the human's own per-frame state picks before the network runs.
3. If the human's pose mode byte `+0xcd2` is 0, 3 or 6: `0x009b4360` (1168), which returns a float. HYPOTHESIS: aim and look-at. It shares the solver `0x004cd8c0` with the post-sample family of §1.6.
4. If `+0xdc` bit `0x02` is set: `0x0099a7d0` (only for objects passing the `0x21` class test, i.e. players), `0x009a0a90` (574), `0x00956970`, `0x009b0ad0` (201, which writes a block of globals at `0x0262d440`…), and `0x009b34f0` (323) → `0x009b0e90` (979). The last carries the string `spinebend`. HYPOTHESIS: procedural upper-body modifiers.
5. If `+0xed0` bit `0x40` is set: `0x004bf150` on a second instance handle at `+0xf80`, an instance attach / allocate helper (HYPOTHESIS: a held prop or second rig).
6. **`0x009a15f0` (394), which posts the pose requests** (§1.6).

### 1.3 Control parameters: registry, name → id, value slot, writers

**Registry** (CONFIRMED by listing and census):
- **Loader:** `0x004cb990` (105; one caller, `0x0073f080`) runs three loaders in order: `0x004cae20` (`control_parameters.xtbl`, slot 12 of the manifest at `0x03519434`), `0x004caee0` (`control_filters.xtbl`, slot 13), and `0x004cb510` (`node_graph_files.xtbl`, slot 14). It then threads a free list through the 128 graph-instance rows at `0x03133a90` (stride `0x4c`; head `0x03171c14`, count `0x03171c18`).
- **Parameter records:** `0x004cae20` writes the count (16-bit) at `0x03171bc8` and the array pointer at `0x03171bcc`. Per row it calls `0x004d00e0` (133), whose strings are `data_type`, `default_value`, `min_value`, `max_value` and `false`, matching `spec-tables-ui-controls.md` §14's empirical schema.
  - Record stride `0x14`: `+0x00` name pointer, `+0x04` type (1 = bool and 3 = float CONFIRMED from the setters' checks; 2 = integer HYPOTHESIS).
  - The remaining three dwords are HYPOTHESIS: default, min, max.
- **Name → id:** `0x004cbaf0` (28) walks the array with the C runtime's case-insensitive compare (`0x00eaa178`) and returns the 16-bit index or −1.
  - It takes a second argument (every caller pushes 0 or 1) that it never reads (CONFIRMED). HYPOTHESIS: a compiled-out "required" assertion.
  - `0x004b8d90` (49) is a by-name variant used by four callers in the `object_rig` band and by `0x0064a1f0`.
- **No hashing.** Unlike most of the engine's name tables (CRC-32, `spec-tables-animation.md` §1.2), this lookup is a plain string compare. Callers resolve once and cache. `0x00958690` keeps 31 cached ids at `0x02624ac4`…`0x02624b3c` behind the guard bitmask `0x02624b40`, one bit per name, set on first use (CONFIRMED by listing).

**Value storage and setters** (CONFIRMED by listing of `0x004b1410`, `0x004b1510`):
- The per-controller value block is a pointer at controller `+0x28`, 8 bytes per registry entry, indexed by the global id. The bool goes in byte `+0`, the float in dword `+0`.
- The setters:
  - first recurse over the controller's **child ring** (next pointer at child `+0x00`, ring head at controller `+0x08`, the same ring `interp_tabs.md` Q13.2 step 1 describes for the play entry);
  - then write only if the id is in range, the block exists, and the registry type matches;
  - give no diagnostic otherwise.
- **The block is allocated lazily.** Per `interp_tabs.md` Q13.2 step 5, `0x004b1b90` allocates it (8 × the count at `0x03171bc8`, zeroed) the first time a state whose animation record carries bit `0x80000000` starts on the controller.
  - Consequence (HIGH): a character with no node-graph state running silently discards every parameter write.
  - The zeroed block, not `default_value`, is the starting value (HYPOTHESIS: unless the graph instance applies defaults; not traced).
- `0x004b1490` (49) is the matching reader (two callers). `0x004b1590` (3) writes a float at controller `+0x2c` (five callers, including `0x00958690` and the vehicle and `object_rig` clients). HYPOTHESIS: a playback-rate scale.

**The writers** (exhaustive over the census callers of `0x004cbaf0`, `0x004b8d90`, `0x004b1410` and `0x004b1510`; names read from the pushed literals):

| Writer | Insns | Names (engine strings) | Whose controller |
|---|---|---|---|
| `0x00958690` | 1187 | `dead`, `combat ready`, `Supported By Ground`, `desired speed`, `Vertical Speed`, `move forward/left/right/back bool`, `backpedal bool`, `alternate strafe bool`, `walk/run/sprint/crouch bool`, `aim bool`, `fine aim bool`, `fire bool`, `@Reloading Bool`, `Weapon Sprint Bool`, `Offhand Grenade Bool`, `Jump Dive Bool`, `current speed`, `direction`, `desired direction`, `turn angle`, `Aim Heading`, `Aim Pitch`, `Free Fall Vert Control`, `Parachute Open Bool`, `Speed Along rvec`, `vehicle surf lean` (32 names, 33 lookup sites) | the human's, `[[human+0xd24]+0x10]` |
| `0x009896d0` | 139 | `Climb Height`, `Climb Dist`, `Climb Platform Bool` (also calls the reader) | human |
| `0x009895b0`, `0x0098bea0`, `0x0098c840` | 48, 215, 230 | `Jump Start Land Bool`, `Jump Left Ground Bool` | human |
| `0x00af4b80` | 498 | `Vehicle Steering`, `Vehicle Steering Time`, `Vehicle MPH`, `Vehicle Forward Lean`, `Vehicle Vert Lean`, `Vehicle Tilt` and their `Abs` forms, `Wheelie Bool`, `Endo Bool` | the rider's (called from `0x00958690` when `0x009b9160` holds; HIGH: bikes) |
| `0x00a760e0` | 300 | `Helicopter Steer Vertical`, `Helicopter Steer Horizontal`, `Vehicle MPH`, `Airplane Mode Bool`, `Vehicle Landed Bool`, `Vehicle On` | the vehicle's own controller |
| `0x00b09ab0` | 320 | `Collision Bool`, `Collision Dir` | vehicle-interaction band (HYPOTHESIS: rider impact) |
| `0x0064a1f0`, `0x008cafc0`, `0x008cb380`, `0x008cb400` | 44–117 | by name, from a register (not recoverable by push survey) | `object_rig` and one activity-band caller (HYPOTHESIS: script- or data-named) |

Nine functions call the bool setter and six the float setter; there are 62 lookup sites through `0x004cbaf0` and four through `0x004b8d90`, for **about 56 distinct compiled-in names**. How many rows `control_parameters.xtbl` ships is `spec-tables-ui-controls.md` §14's own OPEN. Comparing it with this compiled-in set would show which authored parameters are never written by code (OPEN, §4.2).

**What `0x00958690` reads** is the input contract `review/scoping-player.md` U3 owns. That note already records the desired-move block `+0x2048`…`+0x2050` and the stance byte `+0x1d97`. This pass adds only that `desired speed` is the length of that vector when the predicate `0x0091f5e0` holds (the object pointer is masked by the `+0xa` bit-`0x2` "is a player" descriptor bit before the read), and otherwise the float at `+0xa60`; that `Vertical Speed` is `+0xd08`, and that `dead` is the dead predicate `0x0096f4f0` (CONFIRMED by listing). Nothing else is re-derived here.

### 1.4 The controller and the node-graph network — where the parameters are read

**The controller** is the object at `[human+0xd24]+0x10`; vehicles and `object_rig` objects have their own. Fields CONFIRMED as used by listing; roles HIGH:
- `+0x08` child ring; `+0x20` the handle of its animation instance (`0x00487e80` is the one-instruction getter); `+0x24` count of node-graph states; `+0x28` the parameter block (§1.3); `+0x30`/`+0x34` the two anim-group tables and `+0x44` the skeleton-set id (`interp_tabs.md` Q13.2 step 4; `spec-lua-api-behaviour.md` §18.8 and §21.15 set them from script);
- **`+0x48`/`+0x4c` the state-slot array: `0x84`-byte entries**, each holding `+0x00` animation-record index, `+0x04` a clip or instance id, `+0x08` state/action id, `+0x0c` a caller tag, `+0x10` weight, `+0x6c` a node-graph instance row, `+0x70` a random re-trigger deadline, and `+0x80` flags (bit 1 in use, bit 2 fading in, bit 4 fading out, bit `0x10` managed, bit `0x400` "restart in place");
- `+0x50`/`+0x54` the current and next slot cursors; `+0x58`/`+0x5c` blend time and rate; `+0x1a0`/`+0x1a4` a blend override.

**Two ways animation is driven onto a controller:**
1. **Direct state and action requests**, by index into the merged State/Action registry (`spec-tables-animation.md` §5). The play entry is `0x004b1b90` (241, 30 callers), already traced in `interp_tabs.md` Q13.2. Around it sit `0x004b21a0` (88, 26 callers; it walks the child ring), `0x004b2280` (373, the blend-tree-backed variant) and the anim-group binding `0x004b1630` → `0x004ccf30` (`spec-tables-animation.md` §2). Callers include AI modes, the vehicle-interaction seat-state step, the cellphone and synced-action natives, and `0x00959b50` (335, 63 callers; HIGH: the human-side "play action" wrapper).
2. **Parameters into a node graph.** When the started state's animation record has bit `0x80000000`, the slot gets **no clip instance** (Q13.2 step 7). Instead it gets a node-graph instance from `0x004cbd20` (251): a row in the 128 × `0x4c` pool at `0x03133a90`, which also instantiates the graph's control filters. **This is the only path through which the named parameters have any effect.**

**Queries.** `0x004afa20` (201) finds the slot matching an id. The id space is partitioned by range (CONFIRMED by listing; meanings HIGH):
- below 7,500: a state/action id (slot `+0x08`);
- up to the global `0x03171c20`: the slot's `+0x0c` tag;
- 10,001 and 10,002: the current and next cursors;
- up to the global `0x03519854`: a node inside the slot's graph instance, through `0x004cc6a0` → graph `+0x18` node array (`0x5c`-byte node records) → `0x004cbcb0`;
- otherwise: slot `+0x04`.

Its wrappers are the most-called entry points of the library: `0x004b4ce0` "is id active" (11 instructions, **185 callers**, including the AI think gate's tests of `0x2be`/`0x408` in `spec-ai-runtime.md` §1.4 step 3), plus `0x004b41b0` (32 callers), `0x004b4320` (31) and `0x004b44a0` (17).

**The controller update `0x004b3720` (644)**, CONFIRMED by partial listing:
1. bump the instance (`0x004c64d0`);
2. cross-fade: the current slot's weight ramps toward 1.0 at `+0x5c` per frame, with a rate scaled when three or more slots are fading, and the other slots ramp down and are freed below 0.001;
3. then for each in-use slot:
   - re-trigger the state when its deadline passes (`0x004b2d70`, 524, which also evaluates blend-tree states through `0x004b2b50` and `0x004cbd20`);
   - `0x004b3570` (156);
   - **`0x004b0080` (772), the 1-D blend-tree evaluator.** It looks up the input value among the five planar `Range` floats at `+0x14` and interpolates the `Value` floats at `+0x28`, exactly the `State` record layout of `spec-tables-animation.md` §7.3 (CONFIRMED by listing);
   - `0x004b0b80` (425);
4. finally **`0x004cc000` (413)**, which walks the live graph-instance rows (`0x03133a90`…`0x03136098`) and their `0x5c`-byte node records. It resolves each clip node through the graph table `0x03171bdc` (count `0x03171bd8`; graph `+0x18` node array; a node `+0x04` kind of 3 with subkinds 1 and 2 at `+0x10`/`+0x24`), collects up to 32 active clip nodes, and pushes them into the instance through `0x004c6760`/`0x004c6880`. HIGH: this is where graph evaluation becomes layers.

**Where the parameter values are read.** The filter classes' evaluate slots (vtable `+0x04`) are not functions in the shared project. A linear listing of `linear_map_filter`'s slot `0x004d1820` shows (CONFIRMED):
- it resolves the graph (`+0x04` → `0x03171bdc`) and maps the filter's local index through graph `+0x24`/`+0x28` to a global `control_filter_info` record (registry pointer `0x03171bd4`, count `0x03171bd0`);
- if that record's source is another filter (byte `+0x10`), it reads the source filter's output from the graph instance's `+0x10` array;
- otherwise it maps the source **parameter id** (`+0x12`) through the graph's **alias table** (count `+0x1c`, `u16` ids at `+0x20`) to a local index and reads the float at instance `+0x0c` + 8 × index;
- it then applies the record's range (`+0x14`…).

So a parameter is read **by the graph instance, through a per-graph alias list**, not straight from the controller's global-id block.
- How values reach the instance's `+0x0c` array from the controller's `+0x28` block (copy or pointer) is OPEN.
- A missing alias yields index −1 and reads 8 bytes before the array, unguarded (CONFIRMED by listing; an implementation hazard).
- The other parameter consumers are transition conditions (the strings `condition`, `conditions`, `comparison`, `equal`, `parameter1`, `parameter2`, `control_parameter`, parsed in `0x004b8780` and `0x004ba1d0`) and blend-node inputs (`inputs`, `input`, `parameter`, `range`, `value`, `control_point`, `max_blend_rate`, parsed in `0x004bbf40`). Their runtime evaluators sit among the undefined slot targets (§6) and were not read.

### 1.5 The node-graph network as authored data

- **Files.** `node_graph_files.xtbl` (reader `0x004cb510`, 348; `spec-tables-traffic-ai.md` §22) lists about 250 graph files under `tables/anim/network/node_graphs/` (Team B's census, cited there: 249 rows). Each file's root is a `state_machine` (122 files) or a `blend_tree` (128 files), per the family split recorded in `spec-xtbl-format.md` §8's corrections paragraph. The count and array of accepted graphs are `0x03171bd8`/`0x03171bdc` (CONFIRMED as written by `0x004cb510`).
- **Classes** (RTTI, CONFIRMED; vtables in `.rdata` at `0x012a26dc`…`0x012a2a60`):
  - graphs `node_graph_info` → `state_machine_info` / `blend_tree_info`;
  - nodes `node_graph_node_info` → `state_node_info`, `blend_node_info` → `clip_node_info`, `merge_node_info`, `switch_node_info`, `multi_way_blender_node_info`;
  - filters `control_filter` / `control_filter_info` → `linear_map_filter`, `delta_multiplier_filter`, `multiplier_filter`, `cap_filter`, each with its `_info` twin.

  The `_info` classes are the load-time definitions. Their slots are the parsers: `state_machine_info` slot 3 `0x004b91f0` (strings `nodes`, `transitions`, `child_node_graph`); `0x004b90a0` (`default_node`); `state_node_info` slot 2 `0x004b9b40` (`transitions`); `blend_tree_info` slot 2 `0x004bb270` (`nodes`, `merge`; it constructs node objects with the node vtables); `multi_way_blender_node_info` slot 2 `0x004bbf40` (inputs and control points).
- **Vocabulary visible in the image** (engine strings in `.rdata` `0x012a246c`…`0x012a26a8`): `state machine reference`, `blend tree reference`, `multi way blender`, `clip`, `switch`, `merge`, `layer`, `name`, `nodes`, `node`, `default_node`, `state_machine`, `child_node_graph`, `transitions`, `transition`, `length`, `cyclic_percent`, `cancelable_before`, `cancelable_after`, `condition(s)`, `comparison`, the comparison operators (`==`, `!=`, `>=`, `<=`, `and`, `xor`), `parameter`, `type`, `blend_tree`, `inputs`, `input`, `output_node`, `max_blend_rate`, `control_point`, `range`, `value`, `alias_name`, `clip_name`, `validated`.

  This is enough to see the format's shape (state machines of states with timed, conditional transitions; blend trees of clip, merge, switch and multi-way-blend nodes driven by parameters; graphs nesting graphs by reference), but **not its grammar, which no spec covers.**
- **Link to the animation records.** `spec-tables-animation.md` §2/§3.3 record that the `anim_*` loader `0x004aea30` sets bit `0x80000000` of an animation record in a pass over `0x03171bdc`, which that spec calls an "input list". Since `0x03171bdc` is the accepted-graph array (above), the bit marks **animation records that a node graph references**. That is what makes a state "managed" (§1.4). HIGH; to raise for review (§6).

### 1.6 Pose requests, the animation instance, and the hand-off

**The instance** is a `0x3a0`-byte record in the pool at `0x0344c924` (16-bit count `0x0344c91a`; a handle is an index, validated by the record's self-index at `+0x50`). `spec-rig-format.md` §11.16.6 already documents it from the skinning side: `+0x00` the matrix block, `+0x14` the rig holder, `+0x3c` the shared binding descriptor with its `N`, allocation by `0x004c79a0`, construction by `0x004c1730`, release by `0x004c14f0`/`0x004c7ad0`. That is cited, not re-derived. About 160 small accessors (`0x004bc8c0`…`0x004bf210`, almost all of 14–45 instructions) form its handle API. This pass adds (CONFIRMED by listing of `0x004c4810`, `0x004bf270`, `0x004bf360`):
- **Time.** `0x004c4810` multiplies the frame time by **4,800.0** (double at `0x012a2d60`), splits it into whole ticks plus a fraction carried at `+0x398` (kept in [−1, 1]), then advances the layers in two groups split on the layer flag `+0x80` bit 0 (`0x004c3d20`, 575; `0x004c45a0`, 178). The human poster passes either a fixed 1/30 s (`0x0132a0ac`) or the global `0x0153ba64`, selected by `0x00722000`.
- **Layers.** `+0x58` count and `+0x74`… an inline array of `0x50`-byte layer records, as `spec-anim-format.md` §10.1 reads them for the sampler. `spec-rig-format.md` §11.16.6 labels the same array (`+0x58`, `+0x70`, 9 × `0x50`) "attachments". The time advance treats the entries as clip layers, which supports §10.1's reading; to raise for review (§6).
- **Pose requests.** `0x004bf4c0` appends a zeroed 28-byte entry to the queue at `0x02eefae0` (count `0x03171be0`, dispatched-up-to `0x03171be4`). Fields: `+0x00` the target object, `+0x04` the bone cap (the human poster writes `0x80`), `+0x08` a scratch or global block (`0x0262cd60` for humans), `+0x0c` the instance handle, `+0x10` dt, `+0x14` an owner field (`human+0xac8`), `+0x18` step bits.
  - `0x004bf360` submits every entry without bit `0x10` as a scheduler job (`0x00dc21f0`, job function `0x004bf350`) and runs the rest inline.
  - `0x004bf270` executes the steps:
    - **1:** advance;
    - **2:** sample into an 8,192-byte stack buffer, which is 128 bones × 64 bytes and matches the `0x80` cap (CONFIRMED by the `0x2000` allocation);
    - **4:** `0x004cfb60` on the target object;
    - **8:** compose.

    It then sets instance `+0x6c` bit `0x08` and clears bit `0x02`. HIGH: "pose ready" and "pose in flight".
- **The human poster `0x009a15f0`** switches on `+0xcd2`:
  - **mode 0** posts an advance on the instance handle at `[human+0xd24]+0x14`, then (when `+0xdc` bit `0x02` and the instance is live) sample, compose, and conditionally the post-sample step. Bit `0x08` is gated on a rig flag. The target object is **`human+0xea0`**;
  - **modes 1, 2, 4** route to physics-band functions (`0x0077b910`, `0x0077b960`, `0x0077c910`, `0x007800d0`, `0x00783a90`, `0x009a48e0`). HYPOTHESIS: ragdoll-driven poses, owned by `review/scoping-physics-vehicle.md`;
  - **modes 3 and 6** advance the controller's own instance;
  - **mode 5** posts nothing.

  The six-way table is at `0x009a1bd0` (CONFIRMED static read). Whether the instance at `+0x14` is the controller's (`+0x10` → `+0x20`) or a separate output instance is OPEN.
- **Sampling** is `0x004c21a0` (1025), which binds each layer through `0x004c1260` → `0x004c1190` and decodes tracks through `0x004c1020`. That is `spec-anim-format.md` §6c and §10, already specified.
- **Compose.** `0x004c2170` calls `0x004c31b0` (361) and then **`0x004c36f0` (215)** on the instance. `0x004c36f0` loops over the binding descriptor's `N` slots (`[+0x3c]+0x04`), converts each slot to a rig bone through descriptor `+0x10` and back through `+0x110`, reads the rig bone array at `[+0x14]+0x40` (stride `0x28`), and **stores a 48-byte matrix at `[+0x00] + 0x30 × slot`** (CONFIRMED by listing).
  - It marks each slot done in the per-slot flag array at `+0x08`, and runs `0x004c4970` (469) for slots whose descriptor flag has `0x10`.
  - It can instead copy a slot from another instance's matrix block (handle at `+0x30`, remap table at `+0x34`). HYPOTHESIS: a child rig following a parent, such as clothing.
- **This is the hand-off.** The matrix block at instance `+0x00` is the skin gather's `source` (`spec-rig-format.md` §11.16.2), and the palette upload is in the render library (`0x00e7ad30`/`0x00e7aff0`, §11.2 there).
  - `spec-rig-format.md` §11.16.4 says the per-frame writer of that block is not located; `0x004c36f0` is that writer (CONFIRMED as stores through `[+0x00]` at stride `0x30` over `N` slots; that no other writer exists was not checked).
  - Whether the stored matrices are model-space or already skin-space (multiplied by the inverse bind pose) is OPEN. `0x004c31b0`'s role is HYPOTHESIS: local-to-model composition.

### 1.7 Root motion versus velocity — what is known

- **Data side, specified:** every clip carries a root-motion delta quaternion and translation in its header (`spec-anim-format.md` §4). `anim_files.xtbl` flags include `Anim_Movement`, `Block_Rotation`, `Block_Translation`, `Translates_In_Vehicle` and `Female_Root_Offset` (`spec-tables-animation.md` §3.2).
- **Runtime, read in this pass** (CONFIRMED by listing; the role is HYPOTHESIS):
  - the post-sample step `0x004cfb60` runs `this` = `human+0xea0`;
  - it reads one bone (index at `+0xc8`) of the freshly sampled 64-byte-per-bone pose and keeps that bone's value at `+0xbc` and its change since last frame at `+0xb8`;
  - it zeroes individual components according to bits `0x80`, `0x100`, `0x200` and `0x400` of `+0x30`;
  - it then runs `0x004cf7f0` (244) → `0x004cdd20` (617), `0x004ce670`, `0x004cea50`, `0x004cedb0`, `0x004cf130` (a 22-function, ~3.3k module). HYPOTHESIS: root extraction plus ground or foot alignment.
- ~~**Not traced:** whether character movement (`0x0099ebe0`, the proxy callback `0x00961d70`) moves the capsule from that per-frame delta, from `desired speed`, or from both depending on `Anim_Movement`. **OPEN, and the most consequential open question for a port.** Velocity-driven movement can reuse Team B's playback unchanged; root-motion-driven movement needs the extracted delta to feed the character controller.~~ **[Answered 2026-10-03, `spec-player-runtime.md` §5.4/§5.4.5 — the player locomotion bridge, read from the executable rather than inferred: this note's own two named functions turned out not to be the right ones (`0x0099ebe0` is a post-move pass that reads no intent; `0x00961d70` handles vehicle-touch events, not motion); the real bridge is the velocity composer `0x009989b0`→`0x009a02a0`→`0x0099d1b0`, which answers the question directly. Movement is velocity-driven, not root-motion-driven, as the default: the composer computes a world-space velocity from `desired speed`/`desired direction` (the player's smoothed intent vector, or the equivalent NPC-shaped intent) every frame before physics, and root motion's only normal job is to scale the animation's own playback rate (clamped to [0.1, 1.5]) so the clip's stride visually matches that velocity — it does not drive the capsule. The one exception: under animation-controller flag `0x100` ("root-motion-driven" — set per state/clip, ~~presumably from `Anim_Movement` as this note guessed, not independently re-traced~~ **confirmed 2026-10-03 as exactly `Anim_Movement`: controller `+0x198` is the OR of every playing animation record's own `anim_files.xtbl` flag word, and bit 8 of that word is `Anim_Movement` — `spec-player-runtime.md` §5.4.3's "Step 3 in detail"**), the composed velocity becomes the root-motion translation itself, divided by the frame time. So it is genuinely both, selected per animation state, with velocity-driven as the default and root-motion-driven as a flagged exception — not an either/or at the system level. This closes the "most consequential open question" at CONFIRMED for the mechanism and **CONFIRMED (not just HIGH) for which clips carry the flag — it is literally `Anim_Movement`, 220/5,968 rows, dominated by taunts/reactions/stumbles, not the locomotion set.** Note this is a *different* bit from the one that splits the per-frame root-motion sum into T vs. E for the velocity composer's other input — that one is **not** `Anim_Movement` either; see the same "Step 3 in detail" and the correction at §1.7's own composer citation above.]**

---

## 2. Main functions and data structures, with rough sizes

### 2.1 The animation library band `0x004ad000`–`0x004d2000`

**577 functions / ~43.1k instructions** (census). The band's edges are where the markers stop.
- Below `0x004ad000`, the code is render-technique code (`Blur_scale`, `Base Technique`, …) and generic helpers.
- Above `0x004d2000`, it is AI code (functions called from more than a thousand sites, such as `0x004d4000`'s bucket).
- 288 band functions are called from outside the band, through 3,400 call sites in 1,088 outside functions. That is the library's API surface, and it is dominated by the instance accessors and the state queries.

| Sub-band | Funcs | Insns | Contents (markers: data globals and strings) | Status |
|---|---|---|---|---|
| `0x004ad000`–`0x004af000` | 24 | ~3.1k | `anim_*.xtbl` loaders and the `Triggers` / `IKs` / `Sounds` sub-readers (`0x004ae7a0`, `0x004aea30`, `0x004ae390`, `0x004aded0`…) | **specified** (`spec-tables-animation.md`) |
| `0x004af000`–`0x004b8000` | 82 | ~10.0k | controller: slot search `0x004afa20`, cross-fade, blend trees `0x004b0080`, play `0x004b1b90`/`0x004b21a0`/`0x004b2280`, queries `0x004b4ce0`/`0x004b41b0`/`0x004b4320`/`0x004b44a0`, parameter setters, update `0x004b3720` | play entry traced (Q13.2); rest no |
| `0x004b8000`–`0x004bc000` | 52 | ~4.4k | node-graph definitions: condition, transition, state-machine and blend-tree parsers, node classes | no |
| `0x004bc000`–`0x004c0000` | 160 | ~4.9k | instance handle API (validated accessors of the `0x3a0` pool), `anim_core.cpp` dispatch `0x004bf360`, queue `0x004bf490`/`0x004bf4c0`, State/Action registry `0x004bf8d0` (specified) | partly |
| `0x004c0000`–`0x004ca000` | 130 | ~11.4k | layer sampler `0x004c21a0`, clip bind and decode `0x004c1020`/`0x004c1260`/`0x004c1190`, time advance `0x004c4810`, compose `0x004c31b0`/`0x004c36f0`, instance construction `0x004c1730`/`0x004c79a0`, clip cache (`0x030f0c68` users), a 1,236-instruction routine `0x004c8a80` (role OPEN) | decode and override table specified (`spec-anim-format.md`); instance fields partly (`spec-rig-format.md` §11.16.6) |
| `0x004ca000`–`0x004cd000` | 54 | ~3.9k | parameter, filter and graph loaders (`0x004cae20`, `0x004caee0`, `0x004cb510`, `0x004cb990`), graph instancing `0x004cbd20`, graph runtime `0x004cc000`, anim groups `0x004cccc0` (specified) | schemas specified; runtime no |
| `0x004cd000`–`0x004d0000` | 22 | ~3.3k | post-sample adjusters `0x004cfb60`, `0x004cf7f0`, `0x004cdd20`, solver `0x004cd8c0` (shared with `0x009b4360`) | no |
| `0x004d0000`–`0x004d2000` | 53 | ~2.0k | `control_parameter` row parse `0x004d00e0`, the filter classes' `_info` slots and evaluators (mostly undefined slot targets) | no |

### 2.2 Closures of the main root sets

| Root set | Roots | Own | Closure |
|---|---|---|---|
| Parameter writers (all nine) | 9 | ~3.0k | 115 functions / ~9.5k at one level, any band (the vehicle writers pull in vehicle code) |
| Per-human animation step (`0x00994970`, `0x0099e840`, `0x00986400`, `0x0095b1a0`, `0x00958690`, `0x009b4360`, `0x009a0a90`, `0x00956970`, `0x009b0ad0`, `0x009b34f0`, `0x009a15f0`) | 11 | ~4.9k | 73 / ~12.7k at one level, 148 / ~18.0k at two, in the human band `0x00940000`–`0x009f0000` |
| Controller update `0x004b3720` | 1 | 644 | 63 / ~6.4k at three levels in the library band |
| Play and query API (`0x004b21a0`, `0x004b1b90`, `0x004b2280`, `0x004b4ce0`, `0x004b4320`, `0x004b41b0`, `0x004b44a0`, `0x004b1630`) | 8 | ~1.0k | 32 / ~3.0k at three levels in band |
| Pose jobs (dispatch, queue, advance, sample, compose, post) | 13 | ~2.3k | 80 / ~10.5k at three levels in band |
| Load side (`0x004cb990` and its three loaders) | 4 | ~0.7k | 28 / ~2.6k at three levels in band (the graph parsers are mostly virtual and not reached by the call graph) |
| Instance construct / allocate / release / descriptor | 5 | ~0.4k | 7 / ~0.5k |
| Other pose clients (`0x008cb9f0`, `0x00a76690`, `0x00900540`, `0x00921fa0`, `0x00b8fef0`, `0x009ef120`, `0x009ee0d0`, `0x009999b0`) | 8 | ~2.9k | 106 / ~10.3k at one level, any band |
| Already-specified table loaders | 6 | ~2.1k | 17 / ~2.9k |

### 2.3 Data structures (scoping identifications only)

- **Parameter registry:** count `0x03171bc8` (`i16`), array `0x03171bcc`, `0x14`-byte records (name, type, three more dwords). Filter registry: count `0x03171bd0`, array `0x03171bd4` (`control_filter_info` pointers). Graph registry: count `0x03171bd8`, array `0x03171bdc` (`node_graph_info` pointers; graph `+0x18` nodes, `+0x1c`/`+0x20` parameter alias list, `+0x24`/`+0x28` filter map).
- **Graph-instance pool:** 128 rows × `0x4c` at `0x03133a90`…`0x03136090`, free list `0x03171c14`/`0x03171c18`, built by `0x004cb990`; row → node records of `0x5c` bytes; the parameter value array at `+0x0c`; filter outputs at `+0x10`.
- **Controller** (per character, vehicle and rig object): `+0x08` child ring, `+0x20` instance handle, `+0x28` parameter block (8 bytes × registry count), `+0x48`/`+0x4c` `0x84`-byte state slots, `+0x50`/`+0x54` cursors, `+0x58`/`+0x5c` blend time and rate.
- **Human hooks:** `+0xd24` animation component (`+0x10` controller, `+0x14` an instance handle, `+0x18` a time scale); `+0xcd2` pose mode (0–6); `+0xdc` bit `0x02` "pose this frame"; `+0xea0` the post-sample target object; `+0xf80` a second instance handle.
- **Animation instance:** `0x3a0`-byte records at `0x0344c924`, count `0x0344c91a`; layout per `spec-rig-format.md` §11.16.6, plus `+0x58`/`+0x74` layers, `+0x6c` pose flags and `+0x398` the tick fraction (§1.6).
- **Pose-request queue:** `0x02eefae0`, 28-byte entries, counters `0x03171be0`/`0x03171be4`; human scratch `0x0262cd60`.
- **Already specified elsewhere:** the animation records `0x02ef2178` (`0x44` bytes), the clip cache `0x030f0c68` (`0x24` bytes), the State/Action registry `0x03171c10`, the blend-tree rows `0x02eebd58`, the binding arrays at `0x031675a0`/`0x03136090` (`spec-tables-animation.md`), and the binding descriptors at `0x02f5e010` (`spec-rig-format.md`).

**Totals.**

| Scope | Functions | Instructions |
|---|---|---|
| Animation library band | 577 | ~43k |
| Game-side bridge (parameter writers + per-human step, own bodies, de-duplicated) | ~19 | ~6.7k |
| Other pose clients (own bodies) | 8 | ~2.9k |
| **Animation runtime, measured** | **~600** | **~50k** |
| of which already specified (table loaders, clip decode, override table, instance allocation) | ~45 | ~6–7k |

---

## 3. Where the boundary sits — no third-party animation component

The physics and audio notes each found a licensed component (Havok, Wwise). The AI and player notes checked for one and found none. Because a game can license an animation SDK separately even when its physics and audio come from other vendors, this pass checked the animation runtime on its own.

| Check | Result |
|---|---|
| PE imports | 16 DLLs: `WS2_32`, `SHLWAPI`, `binkw32` (video), `steam_api`, `IPHLPAPI`, `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `SHELL32`, `ole32`, `WSOCK32`, `d3d9`, `XINPUT1_3`, `DINPUT8`, `OLEAUT32`. No animation DLL. CONFIRMED (import table). |
| RTTI census (1,579 named classes) | 173 names match animation-adjacent keywords. Every one is the game's own (`node_graph_info`, `state_machine_info`, `blend_tree_info`, the node and filter classes of §1.5, `object_rig`, `anim_rig` inside two hash-table template names, `rig_bone_info`, `state_blend_data`, `animation_properties`, `rl_skin_bone_manager`, `dc_npc_animation_data_compressor`, `dc_blocking_animation_compressor_*`, `dc_object_rig_data_compressor`, `dc_human_ragdoll_data_compressor`), Havok Physics (`hkpRagdollConstraintData`, `hkpPoweredChainMapper`, …), Wwise (`CAkCompressorFX`) or UI. **Havok class families present: `hk` (88) and `hkp` (244) only — zero `hka` (Animation) and zero `hkb` (Behavior).** CONFIRMED. |
| Havok product and source paths | The Havok product keycode (`review/scoping-physics-vehicle.md` §3.1; key not reproduced) names Physics and no other product (tested for Animation, Behavior, Cloth, Destruction, AI). Havok source-path components in the image are only `Common\Base` and `Physics\{Collide, Dynamics, Utilities}`, with no `Animation` tree. CONFIRMED. |
| The three `hka…` strings | `hkaCpuSampleAndCombineJob`, `hkaCpuSampleAnimationJob` and `hkaQuantizedSampleAndCombineJob` (`.rdata` `0x01194770`…`0x011947b8`) sit in one name table with `Cloth`, `SpuAnim`, `Collide And Solve` and `Mesh Bone Deform`. Each is referenced once, from undisassembled code at `0x00bf6a73`…`0x00bf6b50` inside Havok range A (Havok Base). HIGH: Havok Base's job-queue timer names for every job type the SDK knows, compiled in regardless of which products are linked. No function in the census calls into an `hka` routine. |
| Other middleware signatures (whole-image ASCII and UTF-16) | Zero hits for `Morpheme`/`morpheme`, `NaturalMotion`, `Euphoria`, `Granny`/`GrannyGet`, `RAD Game Tools`, `EmotionFX`, `Endorphin`, `HumanIK`, `Autodesk`, `Kynapse`, `Edge Animation`/`edgeAnim`, `FaceFX`, `MR::Network`, `Havok Animation`. Two `NMP` hits are an SNMP string and resource bytes. CONFIRMED. |
| Source paths | 45 distinct build paths. The animation library's is `…\pc_rc\ctg\src\lib\animlib\anim_core\anim_core.cpp`, one of seven under the studio's shared `ctg\src\lib` tree with `vlib` and `render_lib`. The only `vendor` directory is the online SDK (`agora games (hydra sdk)`). CONFIRMED. |
| Who calls the library | 1,088 game functions outside the band call 288 band functions, and the library calls out only to engine helpers (the scheduler `0x00dc21f0`, pools `0x00db5930`, XML accessors `0x00dc4ff0`…, math). It makes no call into a Havok range (HIGH, from the closures of §2.2). |

**So there is no vendor edge. The real boundaries are two internal seams:**
1. **Authored data versus runtime.** Upstream of the runtime sit the clips (`.anim_pc`, specified), the `anim_*.xtbl` tables (specified), `control_parameters.xtbl`/`control_filters.xtbl` (schemas specified, row semantics not), and the ~250 node-graph files (unspecified). Their names, enumerations and file lists are the vocabulary every runtime unit below keys on, so the remaining data-side gap (node graphs) is on the critical path.
2. **Pose versus renderer.** The matrix block at animation-instance `+0x00` (§1.6) is consumed by the skin gather and palette upload already documented in `spec-rig-format.md` §11. A port with its own sampler and skinning, which is Team B's position, can replace everything downstream of the layer list and keep the upstream units unchanged. A port with only the upstream units can feed Team B's existing playback by handing it the layer list (clip, time, weight, layer group) that `0x004cc000` and the controller update produce.

---

## 4. Specified versus missing

### 4.1 What is already specified (cite it; do not redo it)

- **Clips.** `.anim_pc` header, root-motion delta, track-to-bone table, keyframe stream, the bit-`0x40` layer-override table, and the bind / decode functions inside the sampler: `spec-anim-format.md` §2–§4, §6b, §6c, §10. The acceptance function and clip-cache slot (`0x004bea70`, `0x030f0c68`): §2's 2026-10-03 status line.
- **Animation tables.** The 16-slot manifest and every `anim_*.xtbl` loader, record layout and cross-table key: `spec-tables-animation.md` §1–§14, in particular:
  - the loaded-animation records and their 30 flags (§3);
  - the 34 anim groups (§4);
  - the merged State/Action registry (§5);
  - the single-slot transitions table and its empty shipped `<Table>` (§6, so the authored transitions that matter live in the node graphs, not here);
  - the 1-D blend trees whose `State` layout `0x004b0080` consumes (§7);
  - IK situations (§8) and triggers (§9).
- **`control_filters.xtbl` / `control_parameters.xtbl` schemas.** Empirical element trees and filter kinds (`linear map`, `delta multiplier`, `cap`), 22 filter rows: `spec-tables-ui-controls.md` §14. The `control_filters` reader `0x004caee0` and the `node_graph_files` reader `0x004cb510` with its graph-acceptance rule: `spec-tables-traffic-ai.md` §22.
- **The instance record from the skinning side.** Pool, self-index, matrix block, allocation, binding descriptor, palette layout and upload: `spec-rig-format.md` §11.2, §11.16.
- **The controller's play entry.** `0x004b1b90`: slot allocation, state-row lookup, the "managed" kind, the lazy parameter block (`review/exe-notes-2026-10-01/interp_tabs.md` Q13.2).
- **Script surface.** `set_animation_state` (§7.11), `vehicle_anim_start` (§15.2), `action_play_synced_is_finished` (§18.5), `set_animation_set` (§18.8, the controller `+0x30` write), `set_script_animation_group` (§21.15), `action_stop_custom` (§21.28), `action_play_do` (§22.29), `cellphone_animate_start_do` (§28.23), the character-action cluster (§39.1): all in `spec-lua-api-behaviour.md`.
- **What feeds the parameters.** The player's intent fields and the frame position of the writer: `review/scoping-player.md` §1.3–§1.4 and its U3. The AI think gate's animation tests (`0x004b4ce0` with `0x2be`/`0x408`) and the `[[human+0xd24]+0x10]+0x50` guard: `spec-ai-runtime.md` §1.4 steps 3 and 5.6.
  - The brief for this pass says `spec-ai-runtime.md` also references `0x00958690`; it does not. Only `review/scoping-player.md` does. Its animation touch-points are the two above.

### 4.2 What is missing, and whether it needs RE

| Gap | Needs RE? | Notes |
|---|---|---|
| **Control-parameter contract**: registry record layout (default/min/max dwords), type codes, lazy block allocation, child-ring propagation, the ~56 compiled-in names against the shipped rows, `0x00958690`'s field → parameter mapping | **Yes, small** (~2k) | §1.3. The inputs half is `review/scoping-player.md` U3; this is the outputs half of the same contract. |
| **Controller**: slot array semantics, cross-fade and blend-rate rules in `0x004b3720`, re-trigger deadlines, the id-space partition of `0x004afa20`, the query family, `0x004b2280` / `0x004b2d70` / `0x004b0b80` | **Yes, medium** (~5–6k) | Every AI mode, script native and vehicle-interaction step requests animation through it (26 + 30 + 185 callers of the three main entries). |
| 1-D blend-tree evaluation (`0x004b0080`, 772) | **Yes, small–medium** | Data layout specified (`spec-tables-animation.md` §7.3); the interpolation, ramp, cap and cancel-range rules of the `Actions` sub-records are runtime-only. |
| **Node-graph file format** (`state_machine` / `blend_tree` files, ~250, readable from the table archives) | **Yes, medium (data-side)** | Nothing specifies it (`spec-tables-traffic-ai.md` §22 stops at the header). Parsers are `0x004b8000`–`0x004bc000` plus `0x004cb200`/`0x004cc7b0`, mostly virtual slots. Fits the project's schema-from-loader method, with full-population validation. |
| **Node-graph runtime**: instancing `0x004cbd20`, per-frame `0x004cc000`, transition and condition evaluation, blend / merge / switch / multi-way weights, filter evaluation, parameter aliasing, defaults | **Yes, medium–large** (~6–8k, much of it in undefined slot targets) | The core of the bridge. Needs functions created at the slot targets (§6). |
| Instance time and layers: 4,800-tick time base, layer groups, looping, the two advance routines `0x004c3d20`/`0x004c45a0`, `0x004c8a80` | **Yes, medium** (~3–4k) | Team B's playback already does this in its own way; specify for fidelity (event timing, loop wrap). |
| Pose-request queue and job steps (`0x004bf360`, `0x004bf270`, bits 1/2/4/8, the `+0xcd2` modes) | **Yes, small** (~1k) | Ordering and threading contract; §1.1 and §1.6. |
| Compose `0x004c31b0`/`0x004c36f0` and `0x004c4970` | **Yes, small–medium** (~1k) | Closes `spec-rig-format.md` §11.16.4. Decides model space versus skin space at the hand-off. |
| **Root motion versus velocity**; post-sample module `0x004cfb60` → `0x004cf7f0` … (~3.3k) | **Yes, medium** | §1.7. Coordinate with physics P5 and the character-movement step. |
| Human procedural modifiers: aim / look `0x009b4360`, spine bend `0x009b34f0`/`0x009b0e90`, `0x009a0a90`, `0x009b0ad0` | **Yes, medium** (~3.5k own) | Needed for aiming and look-at, not for walking. |
| Vehicle, rider and `object_rig` controllers (`0x00a760e0`/`0x00a76690`, `0x00af4b80`, `0x008cb9f0`) | **Yes, small each** | Same library and same contract; on demand. |
| Physics-driven pose modes (`+0xcd2` = 1, 2, 4; `0x004c7640` via `0x004bccf0` from `0x00781e70`) | **Yes — physics-owned** | Belongs with `review/scoping-physics-vehicle.md`'s ragdoll and character units. |
| Two unidentified pose clients (`0x00b8fef0`; `0x00900540` and `0x00921fa0`) | **Yes, trivial to identify** | Not needed for characters. |
| Network replication of animation state (`dc_npc_animation_data_compressor`, `dc_blocking_animation_compressor_*`, `dc_object_rig_data_compressor`) | **Yes, if multiplayer matters** | Skip for single player. |
| Zone-placed animated objects and any zone-authored animation overrides | **Blocked** | Would come from `.czn_pc` zone data (parked). Not opened. |

**Plain answer.** The *vocabulary* of the animation system (clips, records, states and actions, groups, blend-tree rows, parameter and filter schemas) is about **one third** of the work and is done. The *runtime* (the controller, the graph evaluator, time and layers, compose, root motion) is not specified at all, and the one remaining data format, the node graphs, is the hinge between them.

**OPEN items collected from this pass** (scoping-level questions, not spec defects):
1. Root motion versus velocity: what moves the capsule (§1.7).
2. How parameter values reach a graph instance's `+0x0c` array from the controller's `+0x28` block, and whether `default_value` is ever applied (§1.3, §1.4).
3. The node-graph file grammar, and how many of the 250 files the shipped controllers actually instantiate.
4. Whether the human's instance handle at `[human+0xd24]+0x14` is the controller's own instance or a separate output instance (§1.6).
5. Whether the compose output is model-space or skin-space matrices; and whether `0x004c36f0` is the only per-frame writer of the matrix block.
6. The role of `0x004c8a80` (1,236) and of `0x00994a80`, the per-human job body.
7. The meaning of the registry record's last three dwords, and type code 2.
8. The two unidentified pose clients.
9. The compiled-in parameter names (~56) against the shipped `control_parameters.xtbl` rows (row count is `spec-tables-ui-controls.md` §14's own OPEN).

---

## 5. Proposed order of specification

Ordered by dependency first, then by how directly each unit turns "parameters written" into "clips chosen and weighted". The U-numbers are local to this note and unrelated to the other scoping notes' U- or P-units.

1. **U0 — Control-parameter contract** (§1.3: registry and loader `0x004cae20`/`0x004d00e0`, lookup `0x004cbaf0`, setters and reader, child-ring propagation, lazy block, the writer census, and `0x00958690`'s field → parameter table; ~2k).
   - *Why first:* it is the seam both the player controls and every NPC feed, it is small, and every later unit keys on its ids. It pairs with `review/scoping-player.md` U3, which defines the inputs; together they give the full intent → parameter contract.
   - *Validation:* the shipped `control_parameters.xtbl` rows (names, `data_type`, ranges) against the ~56 compiled-in names and the setter types used for each.
2. **U1 — Controller state slots and the play / query API** (§1.4: `0x004afa20`, `0x004b1b90` (already traced; cite Q13.2), `0x004b21a0`, `0x004b2280`, `0x004b4ce0` family, and the cross-fade half of `0x004b3720`; ~5k).
   - *Why here:* every non-locomotion animation (AI actions, script natives, vehicle entry, cellphone, synced moves) goes through it, and it is the object on which a node-graph state is started. It needs only already-specified data (registry ids, animation records, anim groups).
   - *Validation:* the `spec-lua-api-behaviour.md` script natives listed in §4.1 above, and the AI gate's `0x2be`/`0x408` tests, must resolve to slot states the controller reports.
3. **U2 — Node-graph file format (data side)** (the `state_machine` / `blend_tree` grammar, the node classes, aliasing, the filter binding; parsers in `0x004b8000`–`0x004bc000`, `0x004cb200`, `0x004cc7b0`; ~5k of loader code plus ~250 files).
   - *Why here:* without the graphs there is nothing for the parameters to drive. The work is data-side, fits the project's schema-from-loader method, and can be validated against the entire shipped population before any runtime unit depends on it.
   - *Prerequisite:* create functions at the undefined slot targets (§6) in a private project copy.
4. **U3 — Node-graph runtime** (instancing `0x004cbd20`, per-frame `0x004cc000`, transition and condition evaluation, blend-node and filter evaluation, defaults; ~6–8k).
   - *Why here:* this is the bridge proper. Its output is the layer list (clip, time, weight, group) that Team B's existing playback can consume directly. **U0–U3 (~15–20k plus the graph files) are the "named parameters move the character's animation" slice.**
   - **Decision point:** a port that accepts hand-authored locomotion can map the U0 parameters straight onto Team B's playback (for example `desired speed` → a walk/run/sprint blend) and defer U2–U3. That loses the authored transitions and blend rules, so every character class would need the mapping redone by hand.
5. **U4 — Time, layers and the pose hand-off** (§1.1, §1.6: the pose-request queue and job steps, the `+0xcd2` mode table, time advance `0x004c4810` with `0x004c3d20`/`0x004c45a0`, compose `0x004c31b0`/`0x004c36f0`; ~5k; the sampler itself is already specified).
   - *Why here:* Team B already plays and skins clips, so this unit is about fidelity rather than unblocking: tick base, loop wrap, layer grouping, and the exact matrix space at the hand-off. It also closes `spec-rig-format.md` §11.16.4. It can run in parallel with U2/U3 because it depends only on U1.
6. **U5 — Root motion and post-sample adjusters** (§1.7: `0x004cfb60` → `0x004cf7f0` module, plus the character-movement side of the delta; ~3.5k plus the movement-side trace; coordinate with physics P5).
   - *Why here:* it decides whether the character controller is driven by animation or by `desired speed`. A port can start velocity-driven (the simpler reading) and switch only if U5 shows root motion is authoritative.
7. **U6 — Human procedural modifiers** (aim / look `0x009b4360` with the shared solver `0x004cd8c0`, spine bend `0x009b34f0`/`0x009b0e90`, `0x009a0a90`, `0x009b0ad0`; ~3.5k own).
   - *Why after U5:* needed for aiming, look-at and upper-body lean, not for locomotion. It shares solver code with U5.
8. **U7 — Other controllers on demand:** vehicle (`0x00a760e0`, `0x00a76690`), rider lean (`0x00af4b80`), `object_rig` (`0x008cb9f0`), collision (`0x00b09ab0`), the two unidentified clients. Each is small and reuses U0–U4 unchanged. The vehicle-interaction animation sequencing in `review/scoping-player.md` U6(c)/(f) calls into U1.

**Recommended to park:**
- **Physics-driven pose modes** (ragdoll family; `+0xcd2` = 1, 2, 4 and `0x004c7640`), which belong with the physics note's units.
- **Network replication** of animation state.
- **Zone-placed animated objects** (`.czn_pc`, parked).

**Not needed:** any further work on the `anim_*.xtbl` tables, the clip format, or the palette and skinning side (§4.1).

**Rough weight against the other candidates.** U0–U3 (~15–20k) is about the size of the player's walk-and-look slice and twice the UI-render core. The whole runtime (~50k, of which ~6–7k is done) sits between the player system (~74k) and the AI root set (~33k). Its single riskiest piece is U2/U3, because about two thirds of the node and filter classes' slot targets are not yet functions in the shared project.

---

## 6. Notes for whoever takes it up

- **Tooling.** Everything here came from the session's census tables plus capstone listings of single functions. No Ghidra run was needed for a scoping pass. U2/U3 will need one.
  - **38 of the 57 virtual-slot targets of the 20 node and filter classes are not defined functions in the shared project.** That includes every filter evaluate slot (`0x004d1390`, `0x004d14b0`, `0x004d1630`, `0x004d1820`, `0x004d19f0`) and most node-class slots (`0x004bb4f0`…`0x004bbf00`, `0x004b8d70`, `0x004b8f10`, `0x004b9ae0`, `0x004ba050`, `0x004cb120`, `0x004cb970`, `0x004ccc70`).
  - They do not appear in caller lists. Find them through the vtables at `0x012a26dc`…`0x012a2a60`, and create functions at them in a private copy before any decompiler-based pass.
  - `0x00958690` takes its human in **ESI**, not on the stack. A push survey of its single caller shows no argument; the register is loaded at `0x0095b8f8`.
  - The setters and many accessors are `thiscall` on the controller or instance. Searches keyed on the pool globals miss methods that receive the record in a register, as `spec-rig-format.md` §11.16.3 already learned.
- **Identical-code folding.** The shared one-instruction `RET` at `0x00754410` appears in `0x0095b1a0`'s callee list, as in the AI and control tables.
- **Frame order is load-bearing** (§1.1): parameters are written immediately before the controller update, in the same serial per-human step; pose jobs overlap the next human; a second pose pass happens after physics.
- **Side findings to raise for review** (no spec was edited):
  - `spec-tables-ui-controls.md` §14: the slot-12 (`control_parameters.xtbl`) reader is `0x004cae20`, with the per-row reader `0x004d00e0`. The registry is count `0x03171bc8` and array `0x03171bcc`, `0x14`-byte records.
  - `review/exe-notes-2026-10-01/interp_tabs.md` Q13.2 step 5: "the word `0x03171bc8` … HYPOTHESIS: the bone count". It is the control-parameter count (written only by the `control_parameters.xtbl` reader), so the lazily allocated controller `+0x28` block is the 8-byte-per-parameter value block (§1.3).
  - `spec-tables-animation.md` §2 and §3.3: `0x03171bd8`/`0x03171bdc` are the accepted node-graph count and array written by the `node_graph_files.xtbl` reader `0x004cb510` (census writes). The "`0x03171bdc` pass" of `0x004aea30` that sets bit `0x80000000` therefore marks animation records referenced by node graphs (HIGH), which is what makes a state "managed".
  - `spec-rig-format.md` §11.16.4: the per-frame writer of the matrix block at instance `+0x00` is `0x004c36f0` (stores at stride `0x30` over the descriptor's `N` slots), reached from pose-request step bit `0x08` through `0x004bd0f0` → `0x004c2170`.
  - `spec-rig-format.md` §11.16.6 versus `spec-anim-format.md` §10.1: the 9 × `0x50` array at instance `+0x70`/`+0x74` with its count at `+0x58` is labelled "attachments" in the first and "layer instance records" in the second. The time advance `0x004c4810` treats the entries as clip layers, which supports the second.
  - `review/scoping-player.md` §1.4: the scheduler fan-out at `0x00702b9f` belongs to the per-human job `0x00994a80`. The animation-parameter step `0x00994970` runs serially after the jobs complete (§1.1).
  - `spec-tables-animation.md` §1.2's "this group's callers all pass seed 0" CRC convention does **not** apply to control parameters, which are looked up by plain case-insensitive string compare (§1.3).
- **Out of bounds.** Nothing here opened `.czn_pc` zone data. Zone-placed animated objects and any zone-authored animation data are flagged, not examined.
- Every label above is from this scoping pass only. A spec pass must re-derive each claim from the executable before recording it.

## 7. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names and no pasted pseudocode. Only listings (capstone and the census) were read, never decompiler output. Where cited specs use auto-generated names for the same addresses, this note restates them as plain hex.
- Names quoted as data:
  - RTTI class names (`state_machine_info`, `blend_tree_info`, `clip_node_info`, `linear_map_filter`, `object_rig`, `hkpRagdollConstraintData`, …);
  - control-parameter names read from pushed string literals (`desired speed`, `Vehicle MPH`, …);
  - node-graph and table element names (`state_machine`, `transitions`, `max_blend_rate`, `data_type`, …);
  - `anim_files.xtbl` flag names, Havok job-name strings and build source-path strings read from the image;
  - DLL names from the PE import table.
- Self-check, run on the finished file as the last step: the project's standard auto-name regex (decompiler variable, stack, parameter, register-input, label, function and data auto-names), plus the plain-substring backstop list from the brief, run with Python `re` (the same `cleanroom.py` used for `review/scoping-player.md`). **Regex: 0 hits. Backstop: 0 hits.** A known-positive control string matched 5/5 on the regex and 5 on the backstop in the same run, so the zeros are real. The check was re-run after this line was written; the result is recorded in the final report, not assumed here.
- No spec file was edited. No Ghidra project copy was made for this pass, so none needed deleting. The census tables and helper scripts used live in the session scratchpad (`scratchpad/anim/`), not in the repository.
