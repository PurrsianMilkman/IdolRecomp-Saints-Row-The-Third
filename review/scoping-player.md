# Scoping note — the player: controller object, input, camera, on-foot and vehicle-entry state

**Date:** 2026-10-03 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.
**Question:** what turns a key press or stick deflection into the player character moving, the camera following, and the player getting into or out of a vehicle; how big that system is; what is already specified elsewhere in this project and what is not; and in what order to specify the rest. Team B's missions are now blocked on "move the player somewhere and have the game notice", so the note also marks the smallest slice that unblocks that.
**Method:** Ghidra-free for everything read in this pass:
- direct byte reads of the executable (`tools/harnesses/exe_va.py`) with capstone listings of individual function bodies;
- the read-only function / call / data-reference / string-reference census exported earlier this session from the shared Ghidra project, and its RTTI table (function bodies and instruction counts are the shared project's own; no decompiler output was used anywhere);
- push-argument surveys at call sites, static table reads, and a raw `E8`/`E9` rel32 scan of `.text` for callers inside code Ghidra never disassembled.
- No Ghidra project was opened or copied for this pass, so there is no private project to delete.
- Specs read for cross-reference: `spec-ai-runtime.md` (whole), `spec-lua-api-behaviour.md` §3 preamble, §3.1, §4.6, §4.12, §7.7/§7.8/§7.22/§7.25, §12.15, §14.29, §15.21, §17.6, §18.26/§18.27, §21.11, §22.3/§22.4, §34, §35, §38.1; `spec-tables-ui-controls.md` §1–§5, §8, §14; `spec-tables-environment.md` §12; `spec-tables-vehicle-world.md` §2–§4; `spec-cutscene-camera-format.md` §10.2; `spec-world-streaming.md` §11.3; `spec-physics-runtime.md` §2; `spec-render-pipeline.md` §11; and the four sibling scoping notes.

**Size figures.** Instruction counts are function-body counts from the shared project. "Closure" figures count unique functions reachable from the listed roots, restricted to the named code band unless marked "any band", skipping any single callee over 1,500 instructions. Several table-slot targets have no function defined in the shared project; those are cited by address without a count.

**Confidence labels** are the project's usual ones:
- **CONFIRMED**: read directly in this pass (a listing, a static table read, or a census query).
- **HIGH**: inferred from structure (call shape, field use, string anchors), not read line by line.
- **HYPOTHESIS**: a plausible reading, not checked.
- **OPEN**: not settled; collected in §4.2.

**Naming.** Class names (`player`, `human`, `npc`, `rl_camera`, `character_controller`) are the binary's own RTTI type-descriptor strings. Action names (`CAA_WALK_FORWARD_BACKWARD`, …), HUD keys and camera-angle names are engine data strings read from static tables. All are shipped data, quoted as elsewhere in this project.

---

## 0. Verdict up front

**This is Volition's own code end to end. There is no middleware to bound, and an unusually large share of the edges is already specified — but the per-frame controller in the middle is not, and nobody has documented it as a system.**

1. **No licensed component sits in this path** (CONFIRMED, §3).
   - Input reaches the game only through operating-system and DirectX APIs called from the engine library: XInput ordinals 2 and 3, `DirectInput8Create`, `RegisterRawInputDevices`, `GetCursorPos`, `ClipCursor`, `ShowCursor`.
   - The Steam API imports carry no controller interface, and there is no animation or camera middleware string or RTTI class anywhere in the image.
   - The only third-party code in reach is Havok's character proxy, which `review/scoping-physics-vehicle.md` already scoped as unit P5.
2. **The player object is an RTTI `player` class derived from `human`** (vtable `0x01175e4c`, constructor `0x009de970`). Its pool stride is `0x28d0` bytes against `0x1e70` for the sibling `npc` class, so it carries roughly **2.9 KB of player-only fields** after a ~7.4 KB `human` base. About a dozen of those fields and flag bits are already named by the Lua specs, most of them replicated "player sync flags" (§1.5).
3. **The controller is five layers, all reached from fixed tables, with no virtual dispatch:**
   - a **device poll** in the engine library;
   - a **control context latch** that evaluates the CBA/CAA bindings `spec-tables-ui-controls.md` already specifies;
   - a small **control-state machine** (8 rows, push/pop stack; state 2 is normal play) run once per world update;
   - the **per-frame player controls**, which dispatch twice — on a control profile and on the human's condition field `+0xcc8` (19 rows) — and write *intent* into the player object (desired move, stance bits, per-frame context words);
   - the **human's own animation-parameter writer and character controller**, which consume that intent like they consume AI steering.
4. **The camera is a separate 18-row mode machine with an 8-deep push stack.** Mode 0 is the gameplay follow camera, mode 7 is the cutscene camera `spec-cutscene-camera-format.md` already documents, and it is all fed by the `camera_free.xtbl` data `spec-tables-environment.md` §12.2 already specifies.
5. **The AI `Player` mode (43) is not the controller.** It is a thin AI-side shell (HIGH): its tick handles the out-of-bounds "turn around" drive and scripted moves for the player, through kind-2 requests. On-foot control never passes through the AI mode machine. The two connect only in that the player's `Idle`/`Passenger` begins redirect to `Player`, and the per-human update still runs the AI update for the player.
6. **Vehicle entry and exit is the largest single piece** (~30k instructions in a vehicle-interaction module with its own per-frame tick, an 8-slot occupancy state machine and a camera-blend helper). Its data tables and Lua surface are specified; its runtime is not.

**Rough size:**

| Part | Functions | Instructions | Needs RE? |
|---|---|---|---|
| Input: device poll (engine library) + context latch + binding evaluation + query API | ~105 | ~7.7k | Yes, small–medium; the binding *data* is done |
| Control-state machine | 6 | ~0.1k | Yes, trivial |
| Per-frame player controls (state 2, both dispatch tables, on-foot and in-vehicle action handlers) | ~50 in band; ~260 at one level any band | ~8.4k in band; ~23k at one level | Yes |
| Player object, per-player update, player sync flags | ~40 core; 325 in the module band | ~7.5k core; ~24.5k band | Partly; the Lua-facing flags are specified |
| Locomotion bridge (intent → animation parameters → character controller) | 3 roots, ~70 at one level | ~3.2k own, ~11k at one level | Yes; shared with NPCs |
| Camera (mode machine, follow camera, panning, submode blend) | ~170 | ~16k (band total ~32k incl. already-specified loaders and the cutscene sampler) | Yes |
| Vehicle entry/exit/ride runtime | ~170 | ~30k | Yes — the largest unit |
| AI mode 43 `Player` | 8 | ~1.2k | Yes, small |
| **Measured root set, de-duplicated** | **~550** | **~74k** | |

So the player system is about **7–8× the UI-render core** (~10k), comparable to the game-side physics (~70k) and smaller than the full human-AI runtime (~80–120k). It overlaps heavily with both: the locomotion bridge, character controller and per-human update are shared with every NPC.

**The smallest slice that lets a mission "move the player and notice"** is the device-to-intent path plus the follow camera plus the streaming focus: roughly **15–20k instructions** (§5, U0–U4). The game "notices" the player through one per-frame call, the streaming focus (`0x009e7cb0` → `0x0085bce0`, §1.6), plus whatever trigger checks a script makes against the player's position. The vehicle-entry unit can follow afterwards.

---

## 1. The pipeline: from a key press to the player moving and the camera following

### 1.0 Shape in one picture

All addresses CONFIRMED as call edges by listing or census; roles as labelled in §1.1–§1.9.

```
main loop 0x005d3ac0 → frame 0x005d14b0 (58)
  ├─ 0x00db8d40 (335)  device poll [engine library]
  │     └─ 0x00dd1ad0 (492) pads: XInput 0x00dd0fa0 (173), rumble 0x00dd13b0 (152), DirectInput 0x00dd19e0/0x00dd1a70
  ├─ 0x00707440 (18)   input latch
  │     ├─ 0x005be880 → 0x005bcde0 (582)  device state → control context 0x01411b18; cursor clip 0x005bac90 (80)
  │     ├─ 0x00dcfc60 → 0x00de2dc0 (156)  mouse (GetCursorPos); raw input registration 0x00de2d40
  │     └─ 0x005c2700 (63)  stick helpers 0x005c2190 / 0x005c24b0 / 0x005c2540
  ├─ 0x00db9970 (241)  timers (spec-lua-api-behaviour.md §37.2)
  └─ 0x00706be0 (148)  game-mode dispatcher → mode 5 per-frame 0x00703c00 (228) → world update 0x00702a50 (324)
        ├─ @0x00702a93 camera pre-update 0x00565c60 (76)  → camera-mode slot +0x04 (dt)
        ├─ @0x00702a98 camera update     0x00566a40 (464) → camera-mode slot +0x08
        ├─ @0x00702ad4 control-state dispatcher 0x005aa6c0 (34) → table 0x012e6bb8, state 2 per-frame 0x005b7430 (248)
        │      ├─ profile pre-hook 0x005b3d30 → condition row(+0xcc8).classify  (e.g. 0x005ac620 (452) → context words +0x2070…+0x2084)
        │      ├─ gates: menu/UI, dead, camera mode 3, cinema editor, cutscene skip, script control, controls disabled
        │      ├─ 0x005b73c0 (31) action dispatch by context: on foot 0x005b2ca0 (420) │ vehicle 0x005b5c50 (586) │ 0x005ac1f0 │ 0x009fef60
        │      │        + interaction prompts 0x005b6af0 (603) + 0x0060d960 (325)
        │      ├─ profile camera hook 0x005ab5b0 → row.look  (row 0: 0x005ab6a0, CAA 0/1)
        │      └─ profile post-hook 0x005ab390 → row.move   (row 0: 0x005b3e80 (207) → 0x005ad510/0x005ad130 (CAA 2/3/0) → 0x005aee30 (1307))
        │                                                    writes desired move +0x2048…, stance bits +0x1d94/+0x1d95/+0x1d97
        ├─ @0x00702b61 vehicle-interaction tick 0x00af9260 (122) → 0x00b01f90 → 0x00b01630 (705) / 0x00affc10 → 0x00afbd90 (1551)
        ├─ @0x00702b84 player list 0x009ea630 (40) → per-player update 0x009e91c0 (1314)
        │      ├─ per-human update 0x0099bcb0 (851) → AI update 0x004e8d00 → mode tick (43 `Player`: 0x004f1440)
        │      ├─ 0x009ec410 → 0x009ebde0 (316)  movement-driven camera assist (CAA 2/3)        [HYPOTHESIS]
        │      └─ HUD/meter upkeep 0x007ebae0 (1138), 0x007f2000…0x007f7c80
        ├─ @0x00702b90 NPC list 0x0099bba0 (spec-ai-runtime.md §1.1)
        ├─ @0x00702b9f animation jobs 0x00994e80 → 0x00994970 → 0x0095b1a0 (573) → 0x00958690 (1187)
        │                                       ("desired speed", "desired direction", move/sprint/crouch bools … ← player +0x2048…)
        ├─ @0x00702c46 physics frame 0x0075ac20 (spec-physics-runtime.md §4.3)
        ├─ @0x00702c91 ride animation 0x00af8f70 → 0x00af6310 (2149)
        ├─ @0x00702c9b character movement 0x0099ebe0 (1482) → 0x007a5760 (239) character controller → 0x0079e900 → 0x00961d70 (980)
        ├─ @0x00702d18, @0x00702eee camera update 0x00566a40 again
        ├─ @0x00702e87 streaming focus 0x0085bce0 ← 0x009e7cb0 (120)  (camera position, or the player's vehicle, or a player point)
        └─ @0x00702e9b control-state secondary hook 0x005aa610 → row +0x0c (state 2: 0x005b69c0)
```

**Order matters, and it is fixed** (CONFIRMED from the call sites above): devices are polled and latched before the game mode runs; inside the world update the camera runs first (on last frame's player), then the controls write intent, then the vehicle-interaction tick, then the player and NPC updates, then the animation jobs that turn intent into animation parameters, then physics, then character movement, then the camera twice more, then the streaming focus.

### 1.1 Device poll and the control context (input, layers 1–2)

- **Device poll `0x00db8d40` (335 insns)** is engine-library code, called once per frame from `0x005d14b0` and also from `0x005d3700`. Its block of globals is `0x029f4440`–`0x029f5014`. CONFIRMED as call edges:
  - pads through `0x00dd1ad0` (492), which reaches the XInput import thunk `0x00ecb400` (ordinal 2) from `0x00dd0fa0` and DirectInput creation (`0x00ecb40c`) from `0x00dd19e0`/`0x00dd1a70`;
  - `0x00dd13b0` reaches XInput ordinal 3 (HIGH: the vibration setter, by ordinal);
  - the mouse is read separately: `0x00707440` → `0x00dcfc60` → `0x00de2dc0` (156, `GetCursorPos`), raw-input registration in `0x00de2d40`.
- **Latch `0x00707440` (18)** runs right after the poll. Through `0x005be880` it calls `0x005bcde0` (582), which copies the engine-library device state into the game's **control context at `0x01411b18`**. HIGH, from its callees (`0x00db86a0`…`0x00db8af0`, `0x00dcf*`, `0x00dd02*`) and its single writer role.
  - Per key, the context holds an 8-byte record at `+0xa4 + 8·scan code` (CONFIRMED by listing of the two test helpers). Both helpers require byte `+0`; the "held" test `0x005ba4f0` then reads byte `+2`, and the "pressed" test `0x005bc8b0` reads byte `+1`. The byte meanings (present/enabled, went down this frame, held) are HIGH, from which query uses which helper.
  - `0x005bac90` (80) handles the cursor clip (`ClipCursor`).
- **Binding evaluation.** The runtime binding tables are rebuilt from the active binding set and scheme by `0x005c1f70` (117), one of the slot-array readers already listed in `spec-tables-ui-controls.md` §2.2 (HIGH: it is the only writer of both runtime tables in the census, and it carries the `General Controls`/`Satellite Controls` scheme names).
  - **Buttons:** 164 records of `0x20` bytes at `0x0140ff98`, one per CBA action (CONFIRMED: the query rejects an index above `0xa3`).
  - **Axes:** 34 records of `0x34` bytes at `0x01411418`, one per CAA action (CONFIRMED: the query rejects an index above `0x21`).
  - The record contents line up with the `Control`/`Axis` binding records of `spec-tables-ui-controls.md` §2.2–§3.2: pad buttons, a stick direction, two keys, two mouse buttons. That match is HIGH.
- **The query API** that every consumer calls (CONFIRMED signatures by listing; 178 functions outside the control layer call it, ~40k instructions in total):

  | Query | Insns | Callers | Meaning (HIGH from listing) |
  |---|---|---|---|
  | `0x005bae10(cba, out)` | 70 | 29 | action held; the optional `out` receives which kind of input satisfied it (−1 none, otherwise 0–3; HYPOTHESIS: pad button, stick, key, mouse) |
  | `0x005bd7b0(cba, out)` | 76 | 38 | action went down this frame (edge test) |
  | `0x005bdfe0(cba, …)` | 72 | 9 | edge variant (cutscene skip `0x8e` = `CBA_SKIP_MODAL_SEQUENCE`) |
  | `0x005bb530(caa, …)` | 274 | 31 | axis value as a float |
  | `0x005bb980`, `0x005bb140` | 274, 257 | 2, 5 | alternate axis evaluators (vehicle/camera/UI callers) |
  | `0x005bc5f0`, `0x005bc680` | 45 each | 2 | an axis's positive/negative half as a digital press |
  | `0x005bbf60` | 33 | 11 | clear latched state (called by `player_controls_disable`'s apply `0x009e2dd0`, among others) |
  | `0x005bc5d0` | 2 | 43 | "active input is a gamepad" — already specified as `game_is_active_input_gamepad`'s getter (`spec-lua-api-behaviour.md` §2.6). Its byte `0x0141250d` is context `+0x9f5`, written through the context pointer by `0x005bc9d0` (65), which the latch calls: set on pad activity, cleared on keyboard or mouse activity (CONFIRMED by listing). That is the writer §2.6 records as not located. |

- **The axis names that matter here** (engine data at `0x01122690`): `CAA_CAMERA_ROTATE` = 0, `CAA_CAMERA_ELEVATE` = 1, **`CAA_WALK_FORWARD_BACKWARD` = 2, `CAA_WALK_TURN_LEFT_RIGHT` = 3**, `CAA_DRIVE_STEER` = 9. The button names used below: `CBA_OFC_SPRINT` = `0x12`, `CBA_OFC_SPRINT_TOGGLE` = `0x13`, `CBA_OFC_WALK` = `0x14`, `CBA_OFC_CROUCH` = `0x19`, `CBA_SKIP_MODAL_SEQUENCE` = `0x8e`.

### 1.2 The control-state machine (layer 3)

CONFIRMED by listing of `0x005aa6c0`, `0x005aa610`, `0x005aa780`, `0x005aa740`, `0x005aa690` and a static read of the table.

- **Table `0x012e6bb8`: 8 rows of `0x14` bytes**, slots `+0x00` enter, `+0x04` exit, `+0x08` per-frame, `+0x0c` secondary hook, `+0x10` unused here.
  - Rows 0 and 1: enter `0x005aa5f0` (clear latched input, then one frame of the normal handler), per-frame `0x005b2170`. That is the "locked" frame: cutscene-skip test, camera pre-update, the movement routine called with the player's own position (HYPOTHESIS: no new intent), a stance reset (`0x00985df0`, `0x009fef60`), and in the cinema editor a direct camera update.
  - **Row 2: normal play.** Enter is the shared no-op `0x00754410`, exit `0x005ac4f0`, per-frame **`0x005b7430`**, secondary `0x005b69c0`.
  - Row 7: per-frame `0x005c2800` (412), which writes the camera output globals directly. HYPOTHESIS: a free/debug camera, since its only pusher is the camera band (`0x00564cd0` pushes 7).
  - Rows 3–6 are empty.
- **Selection.** Current state `0x0140e62c`, previous `0x0140e628`. A request stack at `0x012e6c58` holds 12-byte entries with capacity 5 and a handle counter at `0x0140e634`. The top of the stack wins, and an empty stack means state 2.
  - **Push `0x005aa780(state, extra)` returns a handle; pop `0x005aa740(handle)` removes that entry wherever it sits; reset `0x005aa690` empties the stack and sets state 1.**
  - Pushers (12 sites, push survey): UI and menus push 1; `player_controls_disable`'s apply `0x009e2dd0` pushes 1; mission/cutscene code `0x006d86f0` pushes 0; the camera band pushes 7.
  - **This identifies the "session-level input-lock object" that `spec-lua-api-behaviour.md` §7.22 leaves OPEN:** `0x005aa780`/`0x005aa740` are this push and pop. The handle is what `0x0263ae3c` stores.
- **Dispatch `0x005aa6c0` (34)**, once per world update at `0x00702ad4`: on a state change it runs the old row's exit and the new row's enter, then tail-calls the current row's per-frame. `0x005aa610` (7) runs the secondary hook later in the same update (`0x00702e9b`).

### 1.3 Per-frame player controls (layer 4) — two table dispatches

**`0x005b7430` (248 insns), state 2's per-frame, CONFIRMED by full listing.** In my own words:
1. It returns at once in two global conditions (`0x00707490`, `0x00831f70`; HYPOTHESIS: game paused, or a full-screen UI).
2. It zeroes six per-frame **context words** on the local player (`+0x2070`…`+0x2084`).
3. It runs the **control profile's pre-hook**, `[0x0111f1d0 + 12·p]`, where `p` is the global `0x012fd5c8`. That is a two-row table: row 0 is {`0x005b3d30`, `0x005ab390`, `0x005ab5b0`} and row 1 is empty. `p` is written by the UI layer (`0x007c6610`…`0x007c6990`), whose reset value is −1. The tail hooks test for −1 but the pre-hook does not, so either −1 never reaches this code or a menu path can read before the table (OPEN).
4. The pre-hook `0x005b3d30` (not a defined function in the shared project; read linearly):
   - if "actions disabled" is set (`+0x1d99` bit `0x20`, `player_actions_disable`, `spec-lua-api-behaviour.md` §18.27), it forces a fixed context and returns;
   - otherwise it calls **slot `+0x00` of a 19-row table at `0x0111f1e8`, indexed by the human's condition field `+0xcc8`**.
   - That field is the one `spec-lua-api-behaviour.md` §34.5 reads as "5 = dead, 6 = downed, 7 = human-shield-eligible" and `spec-ai-runtime.md` §3.4 uses as the "health/state field". Its value range here is 0–18 (HIGH: the table is bounded by the next constant at `0x0111f2cc`).
   - For row 0, the normal condition, slot `+0x00` is `0x005ac620` (452). It classifies the situation and sets the context words: bit 1 "show interaction prompts", bit 2 "on foot", bit 4 "driving", bit 8 a third context. The bit meanings are HIGH, from the dispatch in step 7.
5. It then works through the special cases, in order:
   - the menu path (`0x007c1ec0`): zero the context, clear latched input, and call the movement routine `0x005aee30` directly with the player's own position;
   - a dead-player step (`0x0096f4f0`): `0x009e6510` and a vehicle-occupancy release `0x00ab7920`; it does not stop the frame;
   - then the first that applies ends the input part of the frame: camera mode 3; the cinema editor (`0x00bc5610`); a cutscene in progress (`0x00720640`; `CBA_SKIP_MODAL_SEQUENCE` → `0x00720830` skips it); **script control** (`+0x28ac` bit `0x02`, the bit `player_script_controlled_clear` clears, `spec-lua-api-behaviour.md` §14.29), which keeps only a menu toggle; and **controls disabled** (`+0x1d99` bit `0x08`, §7.22), which calls the movement routine directly with the player's own position (HIGH: no new intent).
6. Otherwise, unless an animation-state flag (`0x1000000` in the rig object's state) is set, it runs **`0x005b73c0`, the action dispatch**:
   - `+0x2070` bit 2 with no vehicle handle → **on-foot actions `0x005b2ca0` (420)**;
   - bit 4 → **in-vehicle actions `0x005b5c50` (586)**;
   - bit 8 → `0x005ac1f0`;
   - otherwise `0x009fef60` (186, a shared reset of sprint and stance state);
   - then, if bit 1 is set, the **interaction-prompt routine `0x005b6af0` (603)** (party dismiss, door, enter-vehicle prompts), and always `0x0060d960` (325, QTE and prompt upkeep).
7. Tail: the profile's camera hook `0x005ab5b0` → **slot `+0x08` of the same 19-row table** (the look input; row 0: `0x005ab6a0`, reading `CAA_CAMERA_ROTATE`/`ELEVATE`), then the camera pre-update `0x00565c60` again, then the profile's post-hook `0x005ab390` → **slot `+0x04`** (the movement routine; row 0: `0x005b3e80`).

**Movement intent.** Row 0's slot `+0x04`, `0x005b3e80` (207), calls `0x005ad510` → `0x005ad130` (268). That function reads **`CAA_WALK_FORWARD_BACKWARD`, `CAA_WALK_TURN_LEFT_RIGHT` and `CAA_CAMERA_ROTATE`**, then **`0x005aee30` (1307)**. That last function is the only player-controls routine that writes the player's **desired-move block `+0x2048`** (and `+0x203c`, `+0xa60`/`+0xa68`) together with the stance bytes `+0x1d94`/`+0x1d95`/`+0x1d97` (CONFIRMED as writes by a displacement scan; the block's layout is HIGH from neighbouring reads: a vector at `+0x2048`…`+0x2050`).

**Who reads the walk axes** (exhaustive push survey of all 31 callers of the axis query; a raw rel32 scan found no extra callers in undisassembled code):
- `0x005ad130` — the movement routine above;
- `0x005ab930` and `0x005b2ca0` — stance gating only: sprint/walk/crouch toggles with a dead-zone test;
- `0x009ebde0` — inside the per-player update; HYPOTHESIS: a camera assist that steers the follow camera from movement (it writes only private globals `0x0263b124`…`0x0263b13c` and calls into the camera band);
- the `Idle` tick `0x00513270` — HYPOTHESIS: stick input breaks a player out of an action node;
- seven vehicle-interaction functions (`0x00afeb30`…`0x00b06d30`, §1.7);
- two routines in the activity/diversion code band (`0x00624240`, `0x006a4ef0`; HYPOTHESIS by band) and the stick helper `0x005c2190`.

### 1.4 From intent to motion (layer 5) — shared with every NPC

- **Animation-parameter writer `0x00958690` (1187)** runs for every human inside the per-human animation jobs (`0x00702b9f` → `0x00994e80` → `0x00994970` → `0x0095b1a0` → `0x00958690`). The job fan-out goes through the engine scheduler `0x00dc22c0`/`0x00dc19a0` (HIGH).
  - Its strings name the animation network's control parameters: `desired speed`, `desired direction`, `current speed`, `direction`, `turn angle`, `move forward/back/left/right bool`, `walk`/`run`/`sprint`/`crouch bool`, `aim heading`, `aim pitch`, `fine aim bool`, `combat ready`, `Supported By Ground`, `Vertical Speed`, `Free Fall Vert Control`, `vehicle surf lean`.
  - **It reads the player's desired-move block `+0x2048`…`+0x2050` and stance byte `+0x1d97`** (CONFIRMED as reads).
  - So the player is not moved by the controls code: **the controls write intent, and the same animation-parameter path that drives NPCs turns it into animation**. These are the parameters `control_parameters.xtbl`/`control_filters.xtbl` describe (`spec-tables-ui-controls.md` §14).
- **Character movement `0x0099ebe0` (1482)** runs once per frame after the physics step (`0x00702c9b`). It drives the character controller `0x007a5760` (239) over Havok's character proxy (classes `hk_custom_character_proxy`, `character_controller`; `spec-physics-runtime.md` §2.1 rows 21–25). The proxy calls back into the human through `0x0079e900` → `0x00961d70` (980), the per-human motion callback. That callback queries one button (`CBA_OFC_SPRINT`) for the player (CONFIRMED push), and otherwise reads only human fields.
- What the animation network does with these parameters (root motion vs. velocity, blend trees) belongs to the animation runtime, which no spec covers yet (`spec-tables-animation.md` and `spec-anim-format.md` are data-side only).

### 1.5 The player object and its per-frame update

- **Class and storage** (CONFIRMED). RTTI `player` derives from `human`, which derives from `physical_object`. The vtable is `0x01175e4c` (33 slots; overrides of slots 0, 1, 2, 4–10, 15, 19, 20 against `human`'s `0x01171374`).
  - Constructor `0x009de970` (351), reached from a pool instantiator `0x009e85f0` that strides **`0x28d0`** bytes per object. The sibling `npc` class (vtable `0x011758d4`, constructor `0x009d4700`) strides `0x1e70`.
  - The `human` constructor `0x00954340` (864) initialises fields up to `+0x1d8c`; the player constructor initialises `+0x1d94`…`+0x28c4`. So the player-only tail is **`+0x1d90`…`+0x28cf`, about 2.9 KB** (HIGH at the edges).
  - Local player singleton `0x0262edfc`, read by the zero-argument getter `0x009da4e0` (the "local player" of `spec-lua-api-behaviour.md` §3 preamble).
- **Player-only fields already named by other specs** (cited, not re-derived):
  - `+0x1d95` bit `0x2` (sprint allowed, §18.26);
  - `+0x1d99` bits `0x08`/`0x10`/`0x20` (controls / movement / actions disabled, §7.7/§7.8/§18.27);
  - `+0x1e7c`…`+0x1e9c` (written by the camera snap in the general teleport, §22.4);
  - `+0x2088` (customization context), `+0x209c` (current crib);
  - `+0x2888` script mode and `+0x2890`/`+0x2894` its reference;
  - `+0x2898` forced seat and `+0x28a0`/`+0x28a4` forced-seat vehicle (§17.6);
  - `+0x28ac` bits `0x02` script-controlled, `0x10` cannot enter/exit vehicles, `0x80` cannot take human shields (§4.6, §14.4, §14.29).
- **Player-only fields found in this pass** (HIGH unless marked):
  - the per-frame context words `+0x2070`…`+0x2084` (CONFIRMED zeroed every frame by `0x005b7430`);
  - the desired-move block `+0x2048`…`+0x2050`;
  - `+0x2054`…`+0x205c` (written by `0x00985df0`, read by the movement routine);
  - stance bytes `+0x1d94`…`+0x1d97`.
- **The replicated player flag set.** Twenty setters `0x009dfb50`…`0x009e1680`, plus `0x009e1c40`, `0x009e2440` and `0x009e3200`, each carry a `"player"` network tag plus a field tag:
  - `sync_flags` + `allow_max_followers`, `script_controlled`, `use_melee_weapons_only`, `ignore_equip_on_grab`, `cant_enter_exit_vehicles`, `cant_enter_exit_rc_vehicles`, `cant_bust_me`, `cant_take_human_shields`, `cant_be_downed`, `warp_to_shore_disabled`, `skydiving_state_locked`, `immune_to_flat_tires`, `human_shield_state_locked`, `smg_dual_wield_unlocked`, `pistol_dual_wield_unlocked`;
  - and `damage_percent`, `reload_scalar`, `forced_vehicle_seat`, `forced_seat_vehicle_handle`, `undercover_team`, `player_last_vehicle_handle`, `respect`, `script_mode_params`.

  These are the player object's whole script-facing state surface. Several are already in `spec-lua-api-behaviour.md` (§4.6, §14.29, §17.6, §22.3); the rest are one-line setters of the same shape.
- **Per-player update `0x009e91c0` (1314)**, reached only from `0x009ea630`, which walks the object manager's `+0x1f0` list. That list is the player list: `0x009df3d0` scans it for the co-op player, per `spec-lua-api-behaviour.md` §14.31. **This settles `spec-ai-runtime.md` §8 item 7's HYPOTHESIS at HIGH: the second human list holds players.**
  - The update runs vehicle and seat bookkeeping (`0x00af1cf0`, `0x00af2460`), the **per-human update `0x0099bcb0`** part-way through (so the player gets the same per-human and AI step as NPCs, `spec-ai-runtime.md` §1.1), melee/weapon upkeep (`0x009d26b0`, 890), the camera assist `0x009ec410`, and HUD and meter upkeep (`0x007ebae0`, 1138, and six `0x007f2000`…`0x007f7c80` routines).
  - Closure: ~39 functions / ~7.5k in the player band, ~13k at one level in any band.

### 1.6 How "the game notices" the player

- **The streaming focus is fed from the player every frame.** At `0x00702e87` the world update calls `0x0085bce0` with the point `0x009e7cb0` (120) returns (CONFIRMED by listing; the only gate before it selects a two-instruction call, not the focus update). That point is:
  - the camera position `0x013c8740` by default;
  - **the position of the player's vehicle** when the local player has one (`+0x16d8`/`+0x16dc` handle);
  - a point taken from the player's rig in camera modes 0 and 11;
  - the player's own position in camera mode 4 and in three UI states (`0x012fced8` flag `0x2b`).
- **This answers `spec-world-streaming.md` §11.3's OPEN "what `0x009e7cb0` reads" and its "steady state" question, to raise for review** (this note does not edit that spec). The focus *is* fed every frame in free roam; the teleport and start-up writers are the exceptions, not the rule.
- Everything else a mission uses to "notice" the player — trigger volumes, distance checks, `teleport_check_done` — reads the player's position through the `#PLAYER#`/`#PLAYER1#`/`#PLAYER2#` resolver chain (`spec-lua-api-behaviour.md` §3 preamble) or the scripted-request pool (§35.2). None of it is new here.
- **Spawn placement.** The player start points are named objects, `Local_Player_Start`/`Remote_Player_Start` (resolved in `0x0062b650`) and `Player_Start`/`player_start` (`0x006764f0`, `0x008eb2e0`). HYPOTHESIS: these are world-editor-placed objects, i.e. `.czn_pc` zone data, the same parked interior `spec-lua-api-behaviour.md` §34.4 stops at. **Not opened.** A new-game spawn position therefore needs either that interior or a hard-coded stand-in; a checkpoint or save spawn does not (`spec-save-format.md`).

### 1.7 Vehicle entry and exit, from the player's side

**Already specified:** the data — `vi_enter`/`vi_exit`/`vi_ride.xtbl`, `vehicle_interaction_info.xtbl` and `vehicle_interaction_point_sets.xtbl` (`spec-tables-vehicle-world.md` §2–§4). The Lua surface — `vehicle_enter_do`/`_group_do`/`_guardian_angel` and the shared submitter `0x00af22a0` with its 8 occupancy slots (§38.1), `vehicle_exit_do` and its exit/cancel pair (§21.11), `vehicle_enter_check_done` (§22.3), `set_player_can_enter_exit_vehicles` (§4.6), `player_force_vehicle_seat` (§17.6), `on_vehicle_enter`/`on_vehicle_exit` (§4.12/§12.15). The AI modes `Veh enter` (38), `Veh trans` (41), `Passenger` (25) and `Hijack` (18), as table rows only (`spec-ai-runtime.md` §6). The human's vehicle state `+0x16d4` (0 none, 1/2 entering, 3 seated, §21.11) and vehicle handle `+0x16c0`/`+0x16c4`.

**The runtime, scoped in this pass** (call edges CONFIRMED, roles HIGH):

```
on foot: interaction prompts 0x005b6af0 → {test, handler} pairs at 0x0111f2d0… (walked by 0x005b6870 / 0x005b68e0 / 0x005ac390)
           └─ enter/exit handler 0x009e5e80 (109)
                ├─ already seated → exit request 0x00af3cd0 (111; 70 callers)
                └─ else → 0x009e1e60 (74, flag = sprint pressed or +0x1d96 bit 2) → pick vehicle/seat 0x00b105c0 (224) → 0x00b0fdf0 (616)
                          → submitter 0x00af22a0 (162, spec-lua §38.1) → occupancy step 0x00afe570 (351)
per frame: world update @0x00702b61 → 0x00af9260 (122) → per-slot tick 0x00b01f90 (157)
             → 0x00b01630 (705) / 0x00affc10 (128) → seat-state step 0x00afbd90 (1551)
                   → 0x00afd4c0 (976), 0x00af2eb0 (539), 0x00af9a90, 0x00afb930 …  (states keyed by +0x16d4 and the
                     seat-transition registry 0x02845cb0, spec-lua §21.11)
           world update @0x00702c91 → 0x00af8f70 → ride animation 0x00af6310 (2149)
exit apply: 0x00af3cd0 → 0x00b058f0 (190, mode 0x14) → 0x00b03d80 (928), 0x00b036c0 (570) …
camera blend: 0x00b0bc10 (381) → camera submode/blend 0x005753e0, 0x0056cd80, 0x0056cf60, snap 0x00577cb0
              (callers: the seat-state step, AI mode 43's tick, vehicle code 0x00a93070/0x00a93320, …)
```

- **The hand-off between "on foot" and "in a vehicle" is the context word, not the AI mode machine.** Once `+0x16d4` = 3 and a vehicle handle exists, `0x005ac620` sets context bit 4, and the same per-frame dispatch sends input to the in-vehicle handler `0x005b5c50` instead of the on-foot one (§1.3 step 6).
  - The driving inputs themselves are read by vehicle code: `0x00a91800` (397) reads `CAA_DRIVE_STEER` and the walk axes, and `0x00a91440` (255) reads the walk axes and two buttons. Both are reached from `0x005b5c50`. They then feed the vehicle model that `review/scoping-physics-vehicle.md` P4 scopes (HIGH; the hand-off into `hk_custom_vehicle_driver_input` `0x00ae30c0` was not traced).
  - The AI side follows separately: a player in a vehicle stays in mode 43, which is on the vehicle whitelist (`spec-ai-runtime.md` §3.4).
- **Animation sequencing.** The seat-state step reaches the animation-table cross-references (`vi_enter`/`vi_exit` elements chosen by their `ParameterN` tokens), drives `Veh trans`-style sub-states, and reads the walk axes to steer or abort an entry (seven of the vehicle-interaction functions read `CAA_WALK_*`). **None of this is specified.** The token selection rule (which `Element` of an animation set plays, given seat side, door, speed and so on) is the core missing piece.
- **Camera blend.** `0x00b0bc10` is the only place the vehicle-interaction runtime touches the camera. It selects and blends camera submodes from the `camera_free.xtbl` submode table (`vehicle driver`, `exterior close`, …) and can snap the follow camera. It is also called by the `Player` mode tick, so the scripted drive uses the same blend.
- Sizes: the closure from 17 roots is ~170 functions / ~30k in the vehicle-interaction band (`0x00af0000`–`0x00b12000`, 298 functions / ~40k in total, including the specified table loaders).

### 1.8 The camera (scoped separately from the cutscene and vehicle-camera data)

- **Mode table `0x0111a1f0`: 18 rows of `0x14` bytes**, indices 0–17 (CONFIRMED read; mode `0x11` is pushed by a caller, and row 17's last two dwords are the start of a float constant block, so only its first three slots are real — HIGH).
  - Slots: `+0x00` enter, called on every change; `+0x04` pre-update with the frame time (only `0x00565c60` reads it); `+0x08` update (only `0x00566a40` reads it); `+0x0c` and `+0x10` read by `0x00565a40` and `0x00566830` respectively (roles OPEN).
  - Populated rows: 0, 1, 2, 3, 6, 7, 9–17. Rows 4, 5 and 8 are empty.
  - **Row 0 is the gameplay follow camera** (enter `0x005732b0`, pre `0x005779b0`, update `0x00577cb0` → **`0x00575e30` (1558)**). Row 7 is the cutscene camera (`spec-cutscene-camera-format.md` §10.2, specified). Row 17's update `0x00bca9b0` sits in the cinema-editor band (HYPOTHESIS: the editor camera). The other rows were not identified.
- **Mode selection**: current mode `0x013c87c8`, an 8-entry stack `0x013c87cc`…`0x013c87e8` with count `0x013c87ec`, and a base mode `0x013c87f0` used when the stack is empty.
  - Push `0x00564880`, pop `0x00564910`, set-base `0x005649d0` (CONFIRMED by listing).
  - Push survey: modes 3, 4, 6, 7, 9 and `0x11` are pushed; 0, `0xc`, `0xd`, `0xe` and `0x10` are set as base.
- **The follow camera consumes the specified data**: the `panning_group` block (`0x012e3e58`…) is read by the stick-to-pan routines `0x0056f8b0` (323) and `0x0056fdb0` (289); the 61 submode records (`0x013c93b0` + `0x40`·n) by the submode selector/blend `0x005753e0` (238), `0x005750f0` (199) and `0x00574180` (111); `miscellany_group` and `follow_aggression_group` by the core `0x00575e30`. The look input reaches it through the condition table's look slot (§1.3 step 7).
- **Outputs**: position `0x013c8740`/`0x013c8750`, orientation rows `0x013c8790`/`0x013c87a0`/`0x013c87b0`, FOV `0x013c87c4` — the same globals the cutscene camera writes (`spec-cutscene-camera-format.md` §10.2; `spec-tables-environment.md` §7 notes the two adjacent position vectors). How those globals become the view/projection matrices is `spec-render-pipeline.md` §11's OPEN, unchanged here.
- **Update cadence**: the world update calls the update slot three times (`0x00702a98`, `0x00702d18`, `0x00702eee`), around the controls and after physics (HYPOTHESIS: three phases of one camera frame; not read).
- **Vehicle cameras.** The driver views are submodes of mode 0 (`vehicle driver`, `helicopter driver`, `vehicle aim`, … in the 61-name submode table), not separate camera modes (HIGH). Where the per-vehicle camera blocks of `vehicle_cameras.xtbl` (`spec-tables-ui-controls.md` §8.1) enter the follow camera was not traced (OPEN).
- Sizes: the camera band `0x00564000`–`0x00583000` is 442 functions / ~31.9k. The closure from 16 camera-core roots is ~170 functions / ~15.8k. About 1.8k of the band is already-specified loaders and the cutscene sampler.

### 1.9 Where the AI mode machine meets the player

All addresses from `spec-ai-runtime.md` §3.3 and §6, re-read here only for their callers.
- **Mode 43 `Player`**: begin `0x004f13a0` (43 insns), enter `0x004f1350` (no function defined in the shared project), tick `0x004f1440` (906), event handler the return-1 stub, no network slots.
  - The begin has 10 callers: the apply step `0x004e8310`, the `Idle` and `Passenger` begins and the `Idle` tick (which redirect a player there), plus `0x004f1420`, `0x0050fb20`, `0x00950b70`, `0x009daa20`, the teleport mover `0x009e69d0` (`spec-lua-api-behaviour.md` §35.3) and `0x00726570`.
- **What the tick does** (HIGH from its callees and strings, not read in full):
  - it switches on the player's vehicle class (`0x00ad2f80`, four cases);
  - it polls and allocates **kind-2 move requests** (`0x006f6380`/`0x006f62b0`; this is the "Player mode handler ×5" of `spec-ai-runtime.md` §1.6);
  - it sets `Player` sub-states (`0x004dd2d0`);
  - it calls the vehicle-interaction camera blend `0x00b0bc10`;
  - it uses the strings `HUD_BORDER_TURNAROUND` and "The Saints need you in Stillwater!".

  So it is the **out-of-bounds turnaround and scripted move/drive shell for the player**, not the controller. Of the human's fields above `+0x100`, it writes only `+0xa50` and `+0xa68` directly.
- **Consequence for a port:** a player can be driven entirely by §1.3–§1.4 with the AI mode pinned at 43 and its tick stubbed, until a mission needs the turnaround or a scripted player move (`move_to` on `#PLAYER#`, `vehicle_enter_do` on the player, which sets script mode 2, §38.1).

---

## 2. Main functions and data structures, with rough sizes

Own-body counts from the shared project. "Closure" as defined in the front matter. Functions named only by address with no count are table-slot targets the shared project never defined.

| Stage | Key addresses | Instructions (approx.) | What it does |
|---|---|---|---|
| Device poll (engine library) | `0x00db8d40`, `0x00dd1ad0`, `0x00dd0fa0`, `0x00dd13b0`, `0x00dd19e0`/`0x00dd1a70`, `0x00de2dc0`, `0x00de2d40`, `0x00dcfc60` | 1.4k own; closure 25 functions / ~2.2k | XInput / DirectInput / mouse / raw input into the engine library's device block `0x029f4440`… |
| Latch, binding evaluation, query API | `0x00707440`, `0x005be880`, `0x005bcde0` (582), `0x005bac90`, `0x005c2700`; `0x005c1f70`; queries `0x005bae10`, `0x005bd7b0`, `0x005bdfe0`, `0x005bb530`, `0x005bb980`, `0x005bb140`, `0x005bc5f0`/`0x005bc680`, `0x005bbf60` | ~2.0k roots; closure 81 functions / ~5.5k (the band `0x005b9000`–`0x005c3000` is 183 functions / ~10.9k, including the two specified binding loaders and the remap UI) | device state → control context `0x01411b18`; per-action held / pressed / axis queries |
| Control-state machine | `0x005aa6c0`, `0x005aa610`, `0x005aa780`, `0x005aa740`, `0x005aa690`; table `0x012e6bb8` | ~0.1k | normal / locked / free-camera states with a push stack |
| Per-frame player controls | `0x005b7430` (248), `0x005b3d30`, `0x005ab390`, `0x005ab5b0`, `0x005b73c0` (31); condition table `0x0111f1e8` (19 × 3 slots); profile table `0x0111f1d0` | ~0.3k for the dispatchers | gates and the two table dispatches |
| Condition row 0 (normal) | classify `0x005ac620` (452); look `0x005ab6a0` (64); move `0x005b3e80` (207) → `0x005ad510` (114) → `0x005ad130` (268) → `0x005aee30` (1307) | ~2.4k | context words, look input, desired move and stance |
| Other condition rows (18) | classify slots `0x005aae40`, `0x005aaf50`, `0x005acc80`, `0x005ab190`…`0x005aafa0`; move slots `0x005b0350` (519), `0x005b0bc0` (730), `0x005b2240` (190), `0x005b4190` (184), `0x005b4440` (292), `0x005b3750` (329, also a classify slot); look slots `0x005b2540` (282), `0x005ad6b0` | ~3k | downed, human shield, swimming, ragdoll-style conditions (HYPOTHESIS from the `+0xcc8` value names in `spec-lua-api-behaviour.md` §34.5) |
| Context action handlers | on foot `0x005b2ca0` (420) with `0x005ab930` (317), `0x005ad9a0` (376), `0x005b29d0` (210); vehicle `0x005b5c50` (586) with `0x005ade10` (380), `0x005b5110` (503); prompts `0x005b6af0` (603); `0x0060d960` (325); `0x009fef60` (186) | ~4k | sprint / walk / crouch toggles, weapons, melee, interaction prompts |
| Player controls, as a whole | the rows above | closure 49 functions / ~8.4k in the band `0x005aa000`–`0x005b9000` (band total 114 / ~14.3k); ~260 / ~23k at one level in any band; ~840 / ~58k at two | fans out into human, weapon, HUD and vehicle code |
| Player object | constructor `0x009de970` (351), pool `0x009e85f0`, vtable `0x01175e4c`; singleton `0x0262edfc` / getter `0x009da4e0`; 23 sync-flag setters `0x009dfb50`…`0x009e3200` | ~0.4k + ~2.3k setters | the `+0x1d90`…`+0x28cf` player block |
| Per-player update | `0x009ea630` (40) → `0x009e91c0` (1314); camera assist `0x009ec410` → `0x009ebde0` (316) | closure 39 functions / ~7.5k in band (player band `0x009d9000`–`0x009f0000` is 325 / ~24.5k); ~116 / ~13k at one level any band | runs the per-human update, HUD upkeep, seat bookkeeping |
| Locomotion bridge (shared with NPCs) | `0x0095b1a0` (573), `0x00958690` (1187), `0x0099ebe0` (1482); `0x007a5760` (239), `0x00961d70` (980) | 3.2k own for the first three; ~70 / ~11k at one level | intent → animation parameters → character controller |
| Camera core | mode table `0x0111a1f0`; `0x00565c60`, `0x00566a40` (464), `0x00565a40`, `0x00566830`; stack `0x00564880`/`0x00564910`/`0x005649d0`; mode 0 `0x005732b0`, `0x005779b0`, `0x00577cb0`, `0x00575e30` (1558); panning `0x0056f8b0`/`0x0056fdb0`; submodes `0x005753e0`/`0x005750f0`/`0x00574180` | 3.9k roots; closure ~170 functions / ~15.8k (band `0x00564000`–`0x00583000` 442 / ~31.9k) | follow camera, panning, submode blend, mode stack |
| Vehicle entry / exit / ride | handler `0x009e5e80`; `0x009e1e60`, `0x00b105c0`, `0x00b0fdf0`, submitter `0x00af22a0`, step `0x00afe570`; tick `0x00af9260` → `0x00b01f90` → `0x00b01630`/`0x00affc10` → `0x00afbd90` (1551), `0x00afd4c0` (976); ride `0x00af6310` (2149); exit `0x00af3cd0`, `0x00af3e10`, `0x00b058f0`; camera blend `0x00b0bc10` (381) | 7.5k roots; closure ~170 functions / ~30k (band 298 / ~40k incl. the specified loaders `0x00b07090`…`0x00b07c40`) | occupancy slots, seat-state steps, animation sequencing, camera blend |
| AI shell | mode 43 begin `0x004f13a0`, enter `0x004f1350`, tick `0x004f1440` (906) | ~1.2k | turnaround and scripted moves for the player |
| Streaming focus feed | `0x009e7cb0` (120) → `0x0085bce0` | 0.1k | per-frame focus point from camera / vehicle / player |

**Data structures** (scoping identifications only; no layouts claimed beyond what the cited specs state):
- the control context `0x01411b18` (per-key 8-byte records from `+0xa4`), the button binding table `0x0140ff98` (164 × `0x20`) and the axis binding table `0x01411418` (34 × `0x34`);
- the control-state table `0x012e6bb8` (8 × `0x14`), current/previous `0x0140e62c`/`0x0140e628`, request stack `0x012e6c58` (capacity 5, 12-byte entries), handle counter `0x0140e634`;
- the control profile table `0x0111f1d0` (2 × 3 slots) indexed by `0x012fd5c8`, and the condition table `0x0111f1e8` (19 × 3 slots: classify, move, look) indexed by human `+0xcc8`;
- the player block `+0x1d90`…`+0x28cf` (§1.5), with the context words `+0x2070`…`+0x2084` and the desired-move block `+0x2048`…;
- the camera mode table `0x0111a1f0` (18 × `0x14`), mode stack `0x013c87c8`…`0x013c87f0`, outputs `0x013c8740`…`0x013c87c4`; the `camera_free.xtbl` records (specified);
- the per-vehicle occupancy slots (8, `spec-lua-api-behaviour.md` §38.1), the seat-transition registry `0x02845cb0`, the human's `+0x16c0`…`+0x16dc` vehicle handles and `+0x16d4` state.

**Totals.**

| Scope | Functions | Instructions |
|---|---|---|
| Input (device + latch + evaluation + queries + state machine) | ~110 | ~7.8k |
| Player controls (in band) | ~50 | ~8.4k |
| Player object + per-player update (core) | ~40 | ~7.5k |
| Locomotion bridge (own bodies) | 3 | ~3.2k |
| Camera core | ~170 | ~15.8k |
| Vehicle entry / exit / ride | ~170 | ~30k |
| AI shell (mode 43) | 8 | ~1.2k |
| **Measured root set, de-duplicated** | **~550** | **~74k** |
| Bands in full (control, player controls, player module, camera, vehicle interaction) | ~1,360 | ~122k (includes specified loaders, UI remap, HUD and network code) |

**Reading the totals.** The ~74k root set is the analogue of the AI note's ~33k root set: it is the system's own code at one or two call levels. It leans on three large neighbours that other scoping notes already own: the per-human update and AI mode machine (`review/scoping-ai.md`), the character controller and vehicle model (`review/scoping-physics-vehicle.md` P4/P5), and the animation network that consumes the control parameters (no note yet). A port that already has those neighbours needs the ~74k; one that has none of them pulls in a good part of the ~120k AI runtime as well, because the player *is* a human.

---

## 3. Where the boundary actually sits — no third-party component in this path

The physics and audio notes each found a licensed component and drew its edge (Havok, Wwise); the AI note checked for one and found none. This note checked the same way rather than assuming.

| Check | Result |
|---|---|
| PE imports touching input | `XINPUT1_3` ordinals 2 and 3 (thunks `0x00ecb400`/`0x00ecb406`), `DINPUT8.DirectInput8Create` (thunk `0x00ecb40c`), `USER32` `RegisterRawInputDevices`, `GetRawInputData`, `GetCursorPos`, `ClipCursor`, `ShowCursor`. CONFIRMED (import table). These are operating-system and DirectX APIs, not licensed middleware; a port replaces them with its own platform layer. |
| Who calls them | The engine library (`0x00dd0fa0`, `0x00dd13b0`, `0x00dd19e0`, `0x00dd1a70`, `0x00de2d40`, `0x00de2dc0`, `0x00dc48c0`), plus three control-layer functions that call `ClipCursor` for mouse capture (`0x005bac90`, `0x005bc4a0`, `0x005c1f70`). CONFIRMED (census). No gameplay function touches a device API. |
| Steam | The 17 `steam_api` imports are callbacks, matchmaking, user, friends, stats, utils, apps and remote storage. None is a controller interface. CONFIRMED. |
| Animation or camera middleware | No RTTI class and no string matching common animation or camera middleware families. The only animation-adjacent RTTI classes are the game's own (`control_filter`, `control_filter_info`, `dc_human_movement_mode_compressor`, `dc_human_steering_compressor`, `dc_camera_compressor`). CONFIRMED (RTTI census of 1,579 named classes, string census). |
| Havok | Reached only through the character proxy (`hk_custom_character_proxy`, `character_controller`, `MyCharacterListener`; `spec-physics-runtime.md` §2.1 rows 21–25) and the vehicle model. Both are already scoped (`review/scoping-physics-vehicle.md` P4/P5). In the control, player-controls, player-module, camera and vehicle-interaction bands, the only direct calls into Havok's ranges are of two kinds. One is its container allocator `0x00be6620` (inlined array growth, 15 functions — the same pattern the physics note found program-wide) plus two memory helpers. The other is **one world/shape query** in the vehicle-interaction band, `0x00b10ba0` (124) → `0x00c19330`, `0x00d4f8d0`, `0x00d56c60` (HYPOTHESIS: an exit-space check). Everything else reaches Havok through game code (`0x007a5760`, the vehicle object). CONFIRMED (call census of the bands, callees over 3 instructions). |

**So the boundary is the platform layer, not a vendor library:**
- below it, the engine library's device block (`0x029f4440`…) and the four device APIs;
- above it, everything is Volition code that a clean-room pass may read.

The one seam that behaves like a vendor boundary is **the control context `0x01411b18` plus the binding tables**. Every gameplay consumer reads input only through the ~10 query functions of §1.1, never through the device block. A port can therefore implement the query API over its own input stack and leave every consumer unchanged. That makes the query API the natural first contract to write down (§5, U0).

---

## 4. Specified versus missing

### 4.1 What is already specified (cite it; do not redo it)

A substantial share of the player's *edges* is already documented — more than for any of the other four scoped systems.

- **Name resolution — the player's front door.** The `#PLAYER#`, `#PLAYER1#`, `#PLAYER2#`, `#PLAYER1_VEHICLE#` and `#FOLLOWER…#` sentinels and the generic resolver chain `0x00a280c0`/`0x00a28150`/`0x00a281a0`, resolving to the local-player singleton read by `0x009da4e0` (`spec-lua-api-behaviour.md` §3 preamble; §2.4 for `#PLAYER1_VEHICLE#`). The "is a player" test is `0x00853b30(obj, 0x21)` = descriptor-row `+0xa` bit `0x2` (§28.20 for the test; §35.3 for "player class"). Nothing here re-derives it.
- **Co-op and host gating.** `coop_is_active` and its session predicate (§3.1), the host predicate `+0x5c` = `+0x58` (§8.27), the local-authority test `0x008addb0` (§35.5), and the co-op player lookup `0x009df3d0` (§14.31).
- **Teleport.** `teleport` / `teleport_coop`, the scripted-request pool, and **game mode 8**, the asynchronous player teleport: registered by `0x007a8530`, with enter `0x007a7cb0`, exit `0x007a7de0`, may-leave `0x007a7db0`, per-frame `0x007a82c0` (§35, fully traced). The general character teleport `0x00996b50` and the player path `0x009e2220` (§22.4). This pass adds only that game modes are an 11-entry table at `0x01503bc0` (`0x24` bytes per mode, filled by `0x00706a40`, run by `0x00706be0`), and that mode 5's per-frame `0x00703c00` is what calls the world update.
- **Player state flags from Lua.** Controls / movement / actions enable and disable and their `+0x1d99` bits (§7.7, §7.8, §7.22, §7.25, §15.21, §18.27); sprint (§18.26); script control `+0x2888`/`+0x28ac` (§14.29, §22.3); can-enter-exit vehicles (§4.6); forced seat (§17.6); human-shield flags (§14.4/§14.5).
- **Spawn defaults** for any character, the player included: max HP, ignore-AI, the dead predicate `0x0096f4f0` (§34).
- **The AI-side shell.** The 44-mode machine, the request/apply/transition protocol, the acceptance test's vehicle whitelist containing `Player`, and mode 43's begin/enter/tick addresses (`spec-ai-runtime.md` §2–§6). The per-human update that the player update calls (§1.1 there).
- **Control bindings — the data.** `control_binding_sets.xtbl`, `control_schemes.xtbl`, the 166-entry CBA and 34-entry CAA vocabularies, key / mouse / pad / stick tables, and the runtime binding-set slot layout (`spec-tables-ui-controls.md` §2–§5), plus the saved binding tables (`spec-save-format.md` §7.6/§7.7).
- **Camera data.** `camera_free.xtbl` (panning, misc, submodes with the 61-name table, vehicle fallbacks, vehicle aims; `spec-tables-environment.md` §12.2/§17.5), `camera_shake.xtbl` (§12.1), `vehicle_cameras.xtbl`/`vehicle_group_cameras.xtbl` (`spec-tables-ui-controls.md` §8.1), the customization cameras (§8.2), and the **cutscene camera** in full, mode 7's update included (`spec-cutscene-camera-format.md` §10). Lua camera bindings: shake, restrict, look-through (`spec-lua-api-behaviour.md` §8.12, §12.23, §13.15/§13.16, §14.21, §17.14/§17.15, §59.4).
- **Vehicle-entry data and script surface** (§1.7 above): the interaction tables (`spec-tables-vehicle-world.md` §2–§4), the enter / exit / check-done natives and the submitter with its 8 occupancy slots (`spec-lua-api-behaviour.md` §21.11, §22.3, §38.1), and the human's `+0x16d4` vehicle state.
- **Locomotion data.** `control_filters.xtbl`/`control_parameters.xtbl` schemas (`spec-tables-ui-controls.md` §14), the animation tables (`spec-tables-animation.md`) and `.anim_pc` (`spec-anim-format.md`).
- **The character controller's Havok classes and slots** (`spec-physics-runtime.md` §2.1).

### 4.2 What is missing, and whether it needs RE

| Gap | Needs RE? | Notes |
|---|---|---|
| **The input query contract**: control-context layout, the latch `0x005bcde0`, held / pressed / axis semantics (dead zones, the float range, the device-family out-value), the binding-application step `0x005c1f70` | **Yes, small** (~5k) | The seam every consumer uses (§3). It also bears on `spec-tables-ui-controls.md` §2.2's open question of which slot-array reader builds the 164-entry table: `0x005c1f70` fills the 164 × `0x20` runtime table. Whether that is also the save-format table was not checked. |
| Device layer (engine library) | **Mostly no** — write a platform layer | Only the semantics the context exposes matter (key edge bits, pad masks, stick ranges); ~2k to confirm them. |
| **Control-state machine** (8 rows, push/pop, reset) | **Yes, trivial** (~0.1k) | Settles `spec-lua-api-behaviour.md` §7.22's OPEN on `0x005aa780`/`0x005aa740` (§1.2). |
| **Per-frame player controls**: the gates, the profile table, the 19-row condition table, the context words, row 0 in full | **Yes, medium** (~3k for row 0 and the dispatchers) | Entirely unspecified; no spec mentions either table. |
| Desired-move and stance encoding (`+0x2048`…, `+0x1d94`…`+0x1d97`) and how `0x00958690` turns them into animation parameters | **Yes, medium** | The contract between the controller and locomotion. Shared with NPCs: the same writer reads AI steering for them. |
| Other 18 condition rows (downed, human shield, swimming and so on) | **Yes, medium** (~3k + helpers) | Specify on demand. Row identities are HYPOTHESIS until `+0xcc8`'s 19 values are named. |
| On-foot and in-vehicle action handlers (sprint, crouch, weapons, melee, grenades, interaction prompts) | **Yes, medium** (~4k own, much more in helpers) | Fan out into weapon, melee and HUD code. |
| **Follow camera** (mode 0): pre / update slots, `0x00575e30`, panning, submode selection and blend, collision | **Yes, medium–large** (~6–8k with helpers) | Data side done; runtime entirely unspecified. `spec-render-pipeline.md` §11's view-matrix OPEN sits downstream. |
| Camera mode machine (18 rows, stack, base mode, slot roles `+0x0c`/`+0x10`) | **Yes, small** | Row identities other than 0 and 7 are OPEN. |
| Other camera modes (free/debug, satellite, look-through, cinema editor, …) | **Yes, small each** | Specify on demand. |
| Per-player update `0x009e91c0` (ordering, seat bookkeeping, HUD upkeep) | **Yes, small–medium** | Its per-human part is `spec-ai-runtime.md` §1. |
| Streaming focus choice `0x009e7cb0` | **Yes, trivial** | Answers `spec-world-streaming.md` §11.3's OPEN (§1.6; to raise for review). |
| **Vehicle entry / exit / ride runtime**: occupancy step `0x00afe570`, seat-state step `0x00afbd90`, the per-slot tick, the `vi_enter`/`vi_exit` element-selection rule, the exit apply `0x00b058f0`, the ride animation `0x00af6310`, the camera blend `0x00b0bc10` | **Yes, large** (~30k) | Data and Lua surface specified, runtime not. `spec-ai-runtime.md` §8 item 4's question (why `0x00afd4c0` requests `Veh enter`) belongs to this unit: `0x00afd4c0` is one of the occupancy step's state functions. |
| Driving input → vehicle model | **Yes, small** | `0x00a91800`/`0x00a91440` read the axes. The hand-off into the vehicle model is the P4 unit of `review/scoping-physics-vehicle.md`. |
| AI mode 43 tick (turnaround, scripted moves) | **Yes, small** (~1.2k) | Not needed until a mission moves the player by script or the player hits the map edge. |
| Player sync flags not yet in the Lua spec | **Yes, trivial** | One-line setters of a known shape (§1.5). |
| Player start placement on a new game | **Blocked** | `Local_Player_Start`/`Player_Start` are named placed objects (HYPOTHESIS: `.czn_pc` zone data, parked). Not opened. Checkpoint and save spawns are not blocked. |
| The animation network itself (state machine, blend trees, root motion) | **Yes — separate system** | Not scoped here. It decides how the parameters of §1.4 become motion. It needs its own scoping note. |
| Network replication of player state (the 23 sync-flag records, `dc_human_*` compressors) | **Yes, if MP matters** | Skip for single player. |

**Plain answer.** The *edges* of the player — naming, flags, teleport, bindings, camera data, vehicle-interaction data — are about **one quarter** of the work and are done. The *middle* — the query contract, the control-state machine, the two dispatch tables, the intent fields, the follow camera and the vehicle-interaction runtime — is entirely unspecified. It decomposes cleanly by layer, and the first two layers are small.

**OPEN items collected from this pass** (each one is a scoping-level question, not a spec defect):
1. What the 19 values of human `+0xcc8` are called, and so what each condition row is for.
2. Whether the control profile index `0x012fd5c8` can be −1 when state 2 runs (§1.3 step 3).
3. The roles of camera slots `+0x0c`/`+0x10`, and the identities of camera rows 1–3, 6 and 9–17.
4. Whether the three camera update calls per world update are phases of one frame.
5. The role of `0x009ebde0` (camera assist is a HYPOTHESIS) and of the `Idle` tick's walk-axis reads.
6. Where driving input enters `hk_custom_vehicle_driver_input` `0x00ae30c0`.
7. Whether the player start objects are zone-placed (blocked on the parked hold, not opened).
8. Where the per-vehicle camera blocks of `vehicle_cameras.xtbl` enter the follow camera.
9. Which button the interaction-prompt pairs test before calling the enter/exit handler `0x009e5e80`.

---

## 5. Proposed order of specification

Dependency first, then how directly each unit unblocks "move the player and have the game notice". The U-numbers are local to this note; they are unrelated to `review/scoping-ui-render.md`'s U-units.

1. **U0 — Input query contract** (§1.1; `0x005bcde0`, `0x005c1f70`, the ten query functions, the context layout; ~5k).
   - *Why first:* every consumer (178 functions) reads input only through it, so a port can implement it over its own platform layer and leave every consumer as specified. The binding data it evaluates is already done.
   - *Validation:* the shipped binding sets and schemes (`spec-tables-ui-controls.md` §2–§3) predict, for each CBA/CAA action, which keys and pad inputs must satisfy the query.
2. **U1 — Control-state machine and the per-frame dispatch skeleton** (§1.2–§1.3: `0x005aa6c0` and its stack, `0x005b7430`, the profile table, the condition table's three slots, the context words; ~1k).
   - *Why here:* it decides *whether* the player is in control this frame (menus, cutscenes, script control, `player_controls_disable`). Missions toggle those states constantly, so the stack semantics matter even before anything moves.
   - *Validation:* `player_controls_disable` pushes state 1 and `player_controls_enable` pops it (`spec-lua-api-behaviour.md` §7.7/§7.22).
3. **U2 — Normal-condition movement: row 0 and the intent fields** (`0x005ac620`, `0x005b3e80` → `0x005ad510` → `0x005ad130` → `0x005aee30`, `0x005ab6a0`; the `+0x2048` block and stance bytes; ~2.5k).
   - *Why here:* this is the unit that actually moves the player. With U0–U1 it produces intent; it needs no camera beyond the camera vectors it reads.
4. **U3 — Locomotion bridge contract** (`0x00958690`'s reading of the intent fields into the named control parameters; the character-movement step's ordering; ~1.5k of the 3.2k own; coordinate with physics P5).
   - *Why here:* intent becomes motion only through it. It is shared with NPCs, so it pays off for the AI runtime's A3/A5 units too.
   - **Decision point:** a port without the original animation network can drive a capsule directly from the U2 intent and treat this unit as a contract only.
5. **U4 — Follow camera (mode 0) and the camera mode machine** (`0x00565c60`, `0x00566a40`, the stack, `0x005732b0`/`0x005779b0`/`0x00577cb0`/`0x00575e30`, panning, submode blend; ~6–8k with helpers), plus the streaming focus choice `0x009e7cb0` (~0.1k).
   - *Why here:* U2 moves relative to the camera (it reads the camera vectors), and the streaming focus follows the camera by default (§1.6). **U0–U4 together (~15–20k) are the "walk around, camera follows, world streams, scripts see the position" slice.**
6. **U5 — Per-player update** (`0x009e91c0` ordering and its seat/HUD hooks; ~2–3k beyond the per-human part `spec-ai-runtime.md` already covers).
   - *Why here:* it is mostly glue around units already specified elsewhere; ordering is what matters.
7. **U6 — Vehicle entry, ride and exit** (§1.7; ~30k, probably its own spec). Sub-order:
   - (a) the enter trigger and vehicle/seat choice: `0x009e5e80`, `0x00b105c0`, `0x00b0fdf0`;
   - (b) the occupancy step `0x00afe570` and the per-slot tick, keyed by `+0x16d4`;
   - (c) the seat-state step `0x00afbd90`/`0x00afd4c0` with the `vi_enter`/`vi_exit` element-selection rule — the core missing rule;
   - (d) the exit apply `0x00b058f0` and its kinds;
   - (e) the camera blend `0x00b0bc10`;
   - (f) the ride animation `0x00af6310`.
   - *Why after U0–U5:* entry is triggered from the controls (U1's prompt routine), ends by flipping the controls' context to "driving", and blends the camera (U4).
   - *Decision point:* a first drivable build can use **instant placement**, the same path `vehicle_enter_do`'s "instant" flag already takes (`spec-lua-api-behaviour.md` §38.1), and skip (c)–(f).
   - **Refined in §5a (2026-10-03):** the sub-unit breakdown U6.0–U6.8, the corrected roles of these addresses, and a revised decision point. Instant placement still runs through the state machine, and the state-9 commit `0x00afbd90` cannot be skipped.
8. **U7 — In-vehicle controls** (`0x005b5c50` and the driving-input readers `0x00a91800`/`0x00a91440`; ~1.5k), meeting physics P4.
9. **U8 — Other condition rows, action handlers, other camera modes, AI mode 43's tick** — on demand, as missions need downed, human-shield, swimming, the turnaround drive or scripted player moves.

**Recommended to park:**
- **The animation network itself.** It needs its own scoping note; U3 defines only its inputs.
- **Network replication** of player state.
- **New-game player placement**, which depends on zone-placed start objects (`.czn_pc`, parked).

**Not needed:** any further work on the binding, camera or vehicle-interaction *data*; any of the `#PLAYER#` / teleport / co-op material (§4.1).

**Rough weight against the other candidates.** The walk-and-look slice (U0–U4, ~15–20k) is about twice the UI-render core and has a clean seam at the query API. The whole player system (~74k) is comparable to the game-side physics work. Most of its weight is the vehicle-interaction runtime (U6), which decomposes by state and can be deferred behind instant placement.

---

## 5a. U6 sub-unit breakdown (vehicle entry, ride and exit)

**Added 2026-10-03** at the orchestrator's request: plan U6's own sub-units before anyone writes spec content for it, because ~30k instructions is too much for one pass. U0–U4 were 1–8k each and landed as one spec section apiece. This section is a map, not a spec. Apart from a one-line pointer added under §5 point 7, nothing else in this note was edited: §1.7 and §5 keep their numbering and text, and the corrections to them are collected in §5a.1.

**Method.** The same method as the rest of this note:
- census queries (own-body counts, callers and callees, string references);
- capstone listings of the dispatchers and of each sub-part's entry points;
- static reads of four jump tables and of the 83-entry token table;
- a raw `E8`/`E9` rel32 scan of `.text` for the callers of `0x00afe570`, `0x00b058f0`, `0x00b09ff0`, `0x00b099d0`, `0x00afbd90`, `0x00af6310`, `0x00b0bc10` and `0x00b0ad10`. It agrees with the census caller lists exactly.

No Ghidra project was opened or copied.

**State of the upstream units when this was written:** `spec-player-runtime.md` holds U0, U1, U2 and U4 (§1–§4). **Player U3 (the locomotion bridge) has not landed.** U6 needs U3 for one thing only, the rider parameters (§5a.4, U6.8). The animation runtime's play/query API (`spec-anim-runtime.md` §6, its U1) and writer census (§4) *have* landed, and U6 calls both.

### 5a.1 What the closer look changed (corrections to §1.7 and §5 point 7)

The address list in §1.7 is accurate as a list. All 17 roots exist in the census, and every call edge drawn there is real. **Several of the roles were wrong, though, and the shape is a cleaner state machine than §1.7 suggests.** CONFIRMED by listing and table read unless marked.

1. **The "seat-transition registry" `0x02845cb0` is an array of 48 per-vehicle records, `0x1640` bytes apart** (`0x02845cb0`…`0x028888b0`, the loop bounds of both per-frame walkers `0x00af9260` and `0x00af8f70`).
   - Each record holds 8 occupancy slots of `0x100` bytes at `+0x6a0 + 0x100·i`.
   - Slot layout: `+0x00` the occupant's handle (8 bytes); `+0x08` the **phase** (1 enter, 2 exit, 3 ride); `+0x0c` the **state**; `+0x5c` a copy of the 0x44-byte request record that `spec-lua-api-behaviour.md` §38.1 describes (the submitter `0x00af22a0` copies it with `rep movsd`, 0x11 dwords).
   - The "instant" flag is request `+0x20` bit 0, i.e. slot `+0x7c` bit 0.
2. **`0x00afe570` is not a per-frame "occupancy step". It is the *set-enter-state* transition**, with arguments (record, slot, state, flag).
   - It logs the transition through `0x00b0c280` (a debug trace that carries the string `Set Enter` and both network ids).
   - It then jumps through a **20-entry table at `0x00afe924`** to the new state's on-entry handler. Its 27 call sites in 22 functions are the transitions themselves.
   - Its two siblings work the same way:
     - **`0x00b058f0` is set-exit-state**: states `0x14`–`0x21`, a 14-entry table at `0x00b05ac4`, string `Set Exit`. The "mode `0x14`" of §1.7 is simply the state that the exit request `0x00af3cd0` asks for.
     - **`0x00b09ff0` is set-ride-state**: a single state `0x22`, whose on-entry handler is `0x00b099d0`.
   - **So every slot runs one 35-value state number: enter `0x00`–`0x13`, exit `0x14`–`0x21`, ride `0x22`.**
3. **The per-frame side is a phase dispatch, not one step.**
   - `0x00af9260` walks all 48 records. It first releases slots whose occupant is gone (`0x00b086d0`) and runs two record-level upkeep calls (`0x00af5cb0`, `0x00af5bd0`). Then, per slot, it dispatches on the phase:
     - 1 → `0x00b01f90`, the enter per-frame dispatcher (table `0x00b02100`);
     - 2 → `0x00b06ef0`, the exit per-frame dispatcher (table `0x00b06fdc`);
     - 3 → `0x00b0a0c0`, the ride per-frame step (state `0x22` only → `0x00b09ab0`, the `Collision Bool`/`Collision Dir` writer).
   - `0x00b01630` and `0x00affc10` are the per-frame handlers of **enter states `0x13` and `0x07`**, not peers of the per-slot tick. §1.7's diagram omitted the exit and ride dispatchers.
4. **`0x00afbd90` (1551) is the on-entry handler of enter state 9, the *seating commit*.**
   - It is the **only local caller of set-ride** (raw scan: `0x00afbd90` and the network applier `0x00af2950`, nothing else).
   - It is also reached directly from the per-frame handlers of states `0x07` and `0x13` (raw scan, three sites).
   - **`0x00afd4c0` (976) is the on-entry handler of enter state 1**, a routing hub that moves the slot to state 2, 4, 5, 8, `0x0f` or `0x13` (push survey; state 5 added **per Player U6.0**, a shared call tail at `0x00afe05e` that was missed by the original push survey). `spec-ai-runtime.md` §8 item 4 (why `0x00afd4c0` requests `Veh enter`) therefore belongs to U6.3 below.
5. **`0x00af6310` (2149) is not specific to riding.**
   - The world update's ride-animation hook `0x00af8f70` calls it **for every occupied slot in every phase**. Inside, it switches on the phase (`+0x08`) and the state (`+0x0c`), through three jump tables at `0x00af8798`, `0x00af87b4` and `0x00af87d8`, covering enter states 3–`0x13`, exit states `0x1b`–`0x21` and ride `0x22`.
   - It drives the occupant's pose and attachment through the animation API (`0x004b44a0`, `0x004b4320`, `0x004b46c0`, `0x004b2710`, …), three camera-band helpers and the character-proxy enable/disable pair `0x0079ef60`/`0x0079efb0`/`0x0079efe0` (HIGH).
   - A second caller, `0x00af9030`, is reached from vehicle code (`0x00a9e1d0`).
6. **The human's `+0x16d4` is written at exactly four places in the band**, each at a phase boundary (CONFIRMED by listing):
   - **1** in enter state 0's on-entry handler `0x00af9a90` (`0x00af9ae6`);
   - **2** in exit state `0x14`'s on-entry handler `0x00b021c0` (`0x00b0220c`);
   - **3** in the ride on-entry handler `0x00b099d0` (`0x00b09a20`, which also sets the slot phase to 3);
   - **0** on slot release, in `0x00b086d0` (`0x00b0880b`).

   So **2 means "exiting", not "mid-entry"**. That refines `spec-lua-api-behaviour.md`'s reading of 1/2 as "mid-entry" (§21.11 and the `+0x16d4` note after §22). Raise it for review; that file is not edited here.
7. **The "core missing rule" (the element selection) is a small leaf of its own, not part of the seat-state step.**
   - It is `0x00b0ad10` (90; the `Couldn't find animation for %s -> %s -> %s -> %s -> %s` error message), the key builder `0x00b0a6f0` (326), ~~the matchers `0x00b0a510` (23) and `0x00b0a650` (65)~~ the matcher **`0x00b0a650`** (65) — **corrected by Player U6.1**: `0x00b0a510` (23) is a seat-index validator (0–7 kept, else −1), not a matcher; the ride side's own exact six-token matcher is a different function, `0x00b0a580` —, and a sibling caller `0x00b0ac60` (55).
   - **The key builder is a pure function of the slot record.** A 24-way switch on state − `0x0a` writes token indices from the 83-entry vocabulary at `0x0130e638`. Its first key token per state:

     | States | First key token |
     |---|---|
     | `0x0a` | `prepare` |
     | `0x0b` | `traverse` |
     | `0x0c` | `open` |
     | `0x0d`, `0x0e` | `extract` |
     | `0x0f` | `enter` |
     | `0x10` | `close` |
     | `0x11` | `change seat` |
     | `0x12` | `activate` |
     | `0x13` | `fast` |
     | `0x14`–`0x19` | none |
     | `0x1a` | `deactivate` |
     | `0x1b` | `change seat` |
     | `0x1c` | `open` |
     | `0x1d`, `0x1e` | `extract` |
     | `0x1f` | `exit` |
     | `0x20` | `close` |
     | `0x21` | `traverse` |

   - ~~The later key tokens come from slot fields `+0x10`, `+0x14`, `+0x24` and `+0x7d`.~~ **Corrected by Player U6.1**: the key also reads `+0x1c` (surface: air/ground/vehicle/water), `+0x20` (side, via `0x00b0a5f0`) and exit-request byte `+0xd1` bit 0 (`convertible` exit variants); `+0x7d` is bits 2/3/6, not bit 0. These give the variants (`open standard`/`shady`/`shopper`, `extract standard`…`extract fail`, `prepare lift`/`pull`, `traverse standard`/`no door`, …).
   - ~~**So the animated states are exactly enter `0x0a`–`0x13` and exit `0x1a`–`0x21`.** Enter states 0–9 and exit states `0x14`–`0x19` choose no animation element.~~ **Narrower, per Player U6.1**: the states that actually consult the rule are enter `0x0a`–`0x0f`, `0x11` and `0x13`, and exit `0x1b`, `0x1d`, `0x1f` and `0x21`. `0x10`/`0x12`/`0x1a`/`0x1e`/`0x20` are never held (no on-entry handler, `spec-player-runtime.md` §7.5) and `0x1c`'s handlers never reach the rule — but `0x1e`'s case is still live, reached only through a temporary slot the failed-hijack pick builds on its own stack (§9.5 there), not through a held state.
   - The rule has 23 call sites in 16 functions. One of them is the seat-scoring helper `0x00b0eb90`, used by seat choice.
8. **Entry can be aborted by the stick.** Enter state 0's per-frame handler `0x00afeb30` reads `CAA_WALK_FORWARD_BACKWARD`/`CAA_WALK_TURN_LEFT_RIGHT` (axis query `0x005bb530`, actions 2 and 3, absolute values). Unless the request is instant, any deflection moves the slot to **state 8**, whose on-entry handler `0x00afb930` (355) is the cancel. ~~The state-8 entry is inlined, with the prelude `0x00afa400`.~~ **Corrected by Player U6.0**: `0x00afa400` is not an on-entry prelude — it is the transition's network send (snapshots the `0x690`-byte record and posts it through `0x00af2eb0` when this machine owns the occupant, unless replaying from the network), `spec-player-runtime.md` §7.7. This is the "reads the walk axes to … abort an entry" of §1.7, now located.

### 5a.2 Real sizes of the six sub-parts as §5 framed them

Own-body counts from the census. Closures are counted as in the front matter: callees over 1,500 instructions are not expanded. In addition, each part's in-band closure **stops at the other parts' roots**, so shared helpers are counted in every part that reaches them and **the rows do not sum** (the union is given below the table). "In-band" means `0x00af0000`–`0x00b12000`.

| Part (§5 point 7) | Roots | Own | In-band d1 | In-band d2 | In-band full | Any band d1 | Any band d2 |
|---|---|---|---|---|---|---|---|
| (a) trigger + vehicle/seat choice: `0x009e5e80`, `0x009e1e60`, `0x00b105c0`, `0x00b0fdf0` | 4 | 1.0k | 9 / 1.7k | 13 / 2.0k | 22 / 2.9k | 56 / 2.8k | 92 / 5.3k |
| (b) registry, tick, phase dispatch, the three setters, submitter | 14 | 2.0k | 33 / 3.8k | 40 / 4.1k | 49 / 4.9k | 54 / 4.5k | 103 / 6.4k |
| (c) as framed — enter on-entry handlers (19, incl. `0x00afbd90`, `0x00afd4c0`) | 19 | 5.1k | 51 / 8.2k | 67 / 9.4k | 81 / 10.6k | 248 / 20.0k | 683 / 47.0k |
| (c) as framed — enter per-frame handlers (14) | 14 | 4.0k | 33 / 5.5k | 42 / 6.2k | 51 / 6.9k | 115 / 11.6k | 395 / 31.2k |
| (c) as framed — element-selection rule | 4 | 0.5k | 5 / 0.5k | 5 / 0.5k | 5 / 0.5k | 7 / 0.6k | 12 / 1.5k |
| (d) exit request + all exit states | 23 | 4.4k | 56 / 8.2k | 71 / 9.7k | 85 / 10.9k | 224 / 19.0k | 655 / 44.4k |
| (e) camera blend `0x00b0bc10` | 1 | 0.4k | 1 / 0.4k | 1 / 0.4k | 1 / 0.4k | 26 / 1.8k | 73 / 3.3k |
| (f) per-slot pose driver `0x00af6310` (+ `0x00af9030`) | 2 | 2.2k | 11 / 2.9k | 16 / 3.1k | 25 / 3.9k | 64 / 5.3k | 142 / 10.1k |
| *not in §5's list:* ride state `0x00b099d0`, `0x00b09ab0`, rider writer `0x00af4b80` | 3 | 0.9k | 8 / 1.1k | 8 / 1.1k | 8 / 1.1k | 36 / 2.1k | 56 / 2.8k |
| *not in §5's list:* network replication (`0x00af2eb0` `Send Packet`, `0x00af90e0` `Received VI Packet`, `0x00af8810`, `0x00af2950`, `0x00b0c280`) | 5 | 1.3k | 21 / 1.9k | 23 / 2.0k | 23 / 2.0k | 51 / 2.9k | 77 / 4.9k |

- **Union of the in-band closures: 214 functions / ~34.3k.** That is §1.7's ~170 / ~30k, re-measured with every state handler as a root instead of 17 roots at depth 2.
- The band in full is 298 functions / ~40.4k. Of that, **86 functions / ~6.3k are reached from no part**:
  - the specified table loaders `0x00b07090`…`0x00b07c40` (~1k);
  - unrelated vehicle code at the band's lower edge (`nitro_start`, the UFO transition, `0x00af0000`–`0x00af1f00`);
  - a cluster of small public accessors with many outside callers (`0x00b108c0`, 23 callers, and `0x00b10900`, 21; ~~HYPOTHESIS: "is this character in / entering a vehicle" queries for missions and the UI~~ **not vehicle queries — corrected by Player U6.0**: `0x00b108c0` initialises a `0x1c`-byte options block and `0x00b10900` is a caller of the submitter `0x00af22a0`. The real 15-function query-surface family is `0x00af1770`…`0x00af1e50` (13 character-side predicates on `0x00b084f0`+`0x00b08390`, 2 vehicle-side on `0x00b08440`), `spec-player-runtime.md` §7.9).

**What the sizes say about complexity.**
- **(b) and (e) are thin.** They are dispatch tables and a small camera call.
- **(c) and (d) are where the weight is, and they fan out hard.** At one level outside the band, the enter on-entry handlers touch ~12k of other code and the exit states ~11k, spread over:
  - the human (~3k: `0x0096fa20` (1502), `0x009a48e0` (906), `0x00993e90`);
  - vehicle code (~2k: doors, engine, driver assignment);
  - the weapon/item band (~1.5k; HYPOTHESIS: holster and equip; HIGH for `0x00b036c0`, which carries the `Rifle-…` weapon names behind the `Equip Rifle on Exit` seat flag; ~~also `0x00b4d260`~~ **not `0x00b4d260`: Player U6.4 found it operates on the vehicle's own AI object `+0xc68`, not weapon/item — the real weapon/item calls there are `0x005fe3e0`, `0x00600720`, `0x005ffaf0`, `0x00b85fa0` and `0x00b869b0`**);
  - a widely shared helper in the activity band (`0x00600720`, 892 instructions, 101 callers);
  - AI requests and HUD.

  At two levels this reaches 30–47k per part, which is mostly the shared human / vehicle / weapon runtime. **Those callees are contracts to cite, not code for U6 to re-derive.** Each sub-unit should list them as named external calls, the way `spec-ai-runtime.md` treats its own out-of-band callees.
- **The state logic itself is a flat table-driven machine:** two jump tables per phase (on-entry and per-frame), one setter per phase, and one element-lookup rule. The per-state handlers are 36–760 instructions each, apart from the routing hub (976) and the commit (1551).
- **(f) is one large function** that leans on the animation API (~0.8k of callees) and on vector math.

### 5a.3 Proposed sub-units

Sized by the in-band code each one owns ("exclusive": reached from that unit only). Shared helpers are assigned explicitly:
- the **29 helpers reached by three or more units (~2.2k)** go to U6.0;
- the **30 helpers reached by exactly two (~2.7k)** go to whichever of the pair runs first.

Any-band d1/d2 is the out-of-band fan-out to cite as contracts.

| Unit | Roots | Own | In-band (full / exclusive) | Spec weight | Any band d1 / d2 |
|---|---|---|---|---|---|
| **U6.0 Slot record, registry and state skeleton**: the 48 × `0x1640` registry; slot layout; find / allocate / claim / release (`0x00b08400`, `0x00b083d0`, `0x00b085d0`, `0x00b08560`, `0x00b086d0`, `0x00b08860`); the submitter `0x00af22a0` completed beyond §38.1; the three setters and the **numbering of all 35 states**; the phase tick `0x00af9260` with `0x00af5cb0`/`0x00af5bd0`/`0x00af9210`; the three phase dispatchers; the on-entry prelude `0x00afa400`; the four `+0x16d4` writes | 18 | 2.2k | 48 / 4.6k ; 23 / 2.9k | **~5k** with the 29 common helpers | 56 / 4.3k ; 94 / 6.0k |
| **U6.1 Element-selection rule**: `0x00b0a6f0`, `0x00b0a510`, `0x00b0a650`, `0x00b0ad10`, `0x00b0ac60`; the 83-token vocabulary; **validated against every `Element` of the shipped `vi_enter`/`vi_exit`/`vi_ride` (657 / 813 / 454)** | 5 | 0.6k | 6 / 0.6k ; 6 / 0.6k | **~0.6k** + data validation | 8 / 0.6k ; 13 / 1.6k (CRT formatting only) |
| **U6.2 Entry trigger and vehicle/seat choice**: prompt entry 15 → `0x009e5e80` → `0x009e1e60` → `0x00b105c0` → `0x00b0fdf0`, plus `0x00b0eb90` (HYPOTHESIS: seat scoring), `0x00b0da10`, `0x00b0dc20`; closes OPEN item 9 | 5 | 1.3k | 22 / 2.9k ; 8 / 1.7k | **~2k** | 67 / 3.3k ; 98 / 5.7k |
| **U6.3 Enter route states 0–8**: approach, the routing hub `0x00afd4c0`, cancel (state 8, the stick abort), and how "instant" is routed | 15 | 3.8k | 59 / 7.1k ; 25 / 4.5k | **~4.8k** | 158 / 12.1k ; 444 / 29.6k |
| **U6.4 Seating commit and ride state**: state 9's `0x00afbd90`, set-ride, the ride on-entry `0x00b099d0` and per-frame `0x00b09ab0`, and the **camera blend `0x00b0bc10`** (sub-part (e)) | 4 | 2.3k | 27 / 4.5k ; 8 / 2.6k | **~3.6k** (with `0x00b09400`, `0x00b08ea0`, `0x00b08b80`, shared with U6.8) | 169 / 12.1k ; 471 / 27.4k |
| **U6.5 Enter animated states `0x0a`–`0x13`** (prepare, traverse, open, extract (hijack), enter, close, change seat, activate, fast) | 16 | 3.6k | 42 / 6.2k ; 22 / 4.4k | **~4.8k** | 114 / 11.8k ; 386 / 30.3k |
| **U6.6 Exit request and non-animated exit states `0x14`–`0x19`**: the exit request `0x00af3cd0` (70 callers); ~~the cancel request `0x00af3e10` (16 callers; it sets *enter* state 8)~~ `0x00af3e10` is phase-generic (79 sites / 70 callers; dispatches by phase, not enter-only — U6.6 §6.4); the enter-only cancel is `0x00af2460` (16 sites); ~~`0x00af24d0` (from mid-entry into exit `0x15`/`0x19`)~~ `0x00af24d0` acts only on already-exiting slots, fast-forwarding `0x14`→`0x15` or any other exit state→`0x19` — there is no mid-entry → exit route (**corrected by U6.6**, `spec-player-runtime.md` §6.4); state `0x19`'s on-entry handler `0x00b03d80` (928; confirmed as the exit finaliser by U6.6); `0x00b036c0` (confirmed as state `0x15`'s on-entry handler, equip-on-exit pinned down, by U6.6) | 13 | 2.5k | 53 / 6.1k ; 22 / 4.3k | **~4.3k** | 160 / 12.5k ; 479 / 31.4k |
| **U6.7 Exit animated states `0x1a`–`0x21`** (including exit-at-speed, dive and eject; `0x00b063c0` (696) reads the walk axes) | 10 | 1.9k | 53 / 6.2k ; 21 / 3.8k | **~3.8–4.2k** | 81 / 7.3k ; 297 / 19.8k |
| **U6.8 Per-slot pose driver and rider parameters**: `0x00af6310` and its two walkers; the rider writer `0x00af4b80` (called from `0x00958690`); `0x00af4150` (from `0x00952760`); `0x00af4760` (from `0x0095cbc0`) | 6 | 3.4k | 37 / 6.3k ; 13 / 4.0k | **~4.0k** | 104 / 9.4k ; 311 / 24.3k |
| *(parked)* network replication | 5 | 1.3k | 19 / 1.8k ; 10 / 1.5k | ~1.5k | 47 / 2.7k ; 73 / 4.7k |

- **The union of all ten units is 217 functions / ~35.0k in band.** No unit owns more than ~5k, so all of them fit the U0–U4 scale; U6.1 is smaller than any of those units.
- The camera blend (e) is too small to stand alone (381 own; its d1 callees are mostly the submode routines U4 already specifies, `spec-player-runtime.md` §4.8). It joins U6.4 because the commit is where an entry first switches the camera. Its other callers (AI mode 43's tick, enter states `0x0b`, `0x0c` and `0x0f`, exit state `0x1d`, vehicle code `0x00a93070`/`0x00a93320`, and `0x0060c3f0` in the activity band, a caller §1.7 did not list) reuse it unchanged.

### 5a.4 Order, dependencies and what can run in parallel

```
            U6.0 skeleton ─────────┬──────────┬──────────┬───────────┐
                 │                 │          │          │           │
   U6.1 rule ─┐  │ (parallel with  U6.2       U6.3       U6.4        U6.6
   (parallel  │  │  U6.0)          trigger    route      commit+     exit request +
   with U6.0) │  │                 /seat      0–8        ride+blend  non-animated exits
              ├──┴──────────────────────────────┐                    │
              │                    U6.5 animated enter (needs U6.1, U6.3)
              └─────────────────── U6.7 animated exit  (needs U6.1, U6.6)
                                   U6.8 pose driver    (needs U6.4; rider part needs Player U3)
```

1. **U6.0 — first.** Every other unit transitions slots through its three setters, reads the slot record, and is dispatched from its tables. Lua natives that are already specified touch the same record: `vehicle_enter_check_done` (§22.3) and `vehicle_exit_do`'s exit/cancel pair (§21.11). U6.0 is what makes their descriptions exact.
   - *Validation:* the 35-state numbering must account for every immediate value in the push survey of the three setters (27 + 22 + 2 call sites). Every slot written by the submitter must start at state 0.
2. **U6.1 — in parallel with U6.0.**
   - *Why independent:* it is a pure function from slot fields (`+0x0c`, `+0x10`, `+0x14`, `+0x24`, `+0x7d`) to a token tuple, then a match against data `spec-tables-vehicle-world.md` §2 already specifies. It can cite those fields by offset and leave their meanings to U6.0.
   - *Why early:* it is cheap, and it can be checked against shipped data more completely than anything else in U6. ~~Every shipped `Element` tuple should be producible by some state and field combination, and every key the builder can produce should either match or hit the error string.~~ **Checked by Player U6.1**: true for `vi_enter.xtbl` (657/657); 5 of 813 `vi_exit.xtbl` Elements cannot be produced by any key (a third token the builder never pairs with their second token); `vi_ride.xtbl` is outside this rule entirely — it's matched exactly by a separate pair of functions this rule never calls. Every key that matches nothing does hit the error string, formatted but never printed or logged.
   - **The answer to "does everything depend on (c)": no.** Only the animated states (U6.5, U6.7) call the rule, plus seat scoring in U6.2 (one call site in `0x00b0eb90`). Entry, the commit and the non-animated exit do not choose elements.
3. **U6.2 and U6.3 — after U6.0, in parallel with each other.**
   - U6.2 ends at the submitter. It is shared with the AI: `0x00b0fdf0` is also called from `0x0051d5b0` and `0x00521200`, so NPC `Veh enter` uses the same seat choice.
   - U6.3 starts where the submitter leaves the slot (state 0).
4. **U6.4 — after U6.0, in parallel with U6.2/U6.3.** It is the single path to "seated" (§5a.1 item 4). It owns the camera hand-off, which needs U4 (landed). It does **not** need U6.1.
5. **U6.6 — after U6.0, in parallel with U6.2–U6.4.**
   - Exit states are disjoint from enter states.
   - ~~Exit is entered from three places: the ride state, through the exit request `0x00af3cd0`; mid-entry, through `0x00af24d0` (→ `0x15` or `0x19`); and the network applier.~~ Exit is entered from two places: any phase, through the exit request `0x00af3cd0` (always → state `0x14`); and the network applier. **There is no mid-entry → exit route** — `0x00af24d0` only fast-forwards slots already exiting, to `0x15` or `0x19` (**corrected by U6.6**, `spec-player-runtime.md` §6.4.3).
   - A cancelled entry is different: the enter-only canceller `0x00af2460` sends it to *enter* state 8, which is U6.3's. ~~`0x00af2460`/`0x00af3e10` send it to *enter* state 8~~ `0x00af3e10` is phase-generic, not enter-only: it dispatches on the slot's phase (enter → `0x00af2460`'s state-8 path; exit → `0x00af24d0`; ride → a fresh instant exit request) (**corrected by U6.6**, `spec-player-runtime.md` §6.4).
   - None of these needs the internals of the enter states.
6. **U6.5 — after U6.1 and U6.3.** U6.3's hub decides which animated state runs next. **U6.7 — after U6.1 and U6.6.** U6.5 and U6.7 can run in parallel with each other.
   - They share `0x00afe0d0` (219), `0x00b01460` (168) and `0x00b0c170`. Whichever lands first owns them.
7. **U6.8 — last.**
   - It runs for every phase, so its cases are easiest to name once the state meanings exist.
   - Its rider-parameter part (`0x00af4b80`) is called from inside the human animation-parameter writer `0x00958690`, so it **depends on Player U3, which has not landed**. The parameter *names* are already in `spec-anim-runtime.md` §4.1.
   - The pose half depends only on the animation API (`spec-anim-runtime.md` §6, landed).
8. **Network replication — park**, with the rest of network replication (§5's park list).
   - It hooks in at known points: every setter logs through `0x00b0c280`, state 9 sends a `0x690`-byte record snapshot through `0x00af2eb0`, and the receiver is `0x00af90e0` → `0x00af8810` → `0x00af2950`. It can be added later without touching the state logic.

### 5a.5 The decision point, re-checked

§5 point 7 says a first drivable build "can use **instant placement** … and skip (c)–(f)". **That does not hold as written.**

- **Instant placement is not a separate code path.** `vehicle_enter_do` reaches the submitter `0x00af22a0` on every path. The submitter copies the request into the slot and calls set-enter with state 0 (CONFIRMED by listing). "Instant" is a flag (slot `+0x7c` bit 0) that only three places test:
  - the tick upkeep `0x00af5cb0` (`0x00af5fa8`);
  - state 0's per-frame handler (`0x00afeb64`, where it disables the stick abort);
  - state `0x0a`'s per-frame handler `0x00affdb0` (`0x00afff52`).
- **"Seated" is reachable only through state 9's commit `0x00afbd90`**, which is in the old (c). It is the only local caller of set-ride, so it is the only local way to reach phase 3, state `0x22` and `+0x16d4` = 3. It also changes the vehicle's own state (39 vehicle-band callees at one level; HIGH), calls into the weapon/item band (HYPOTHESIS: holstering) and starts the camera blend (CONFIRMED call). **So a drivable build cannot skip it.**
- **Revised decision point.** A first drivable build needs:
  - U6.0, the skeleton (~5k);
  - U6.4, the commit and the ride state (~3.6k);
  - the instant route through U6.3. How an instant request is routed from the state-1 hub to state 9 is **OPEN**; the hub's targets are 2, 4, 5, 8, `0x0f` and `0x13` (5 added per Player U6.0, a shared call tail the original push survey missed), and `0x0f`'s on-entry handler `0x00afd0a0` goes straight to 9 (push survey; HYPOTHESIS that this is the instant/snap route). Reading only states 0, 1 and the instant branch is ~1.5k.
  - U6.6, so the player can get out again (~4.3k).

  That is **about 13–15k, against ~35k for all of U6**.
- **What it can skip:**
  - **U6.1, U6.5 and U6.7** (every animated state, and with them the element rule);
  - **U6.2** (a script or debug key can submit the request directly, as `vehicle_enter_do` does);
  - **U6.8**, if the port pins the occupant to the seat transform itself;
  - the network unit.
- **Cheaper alternative.** A port willing to stub can go further: specify U6.0, then write a port-side commit that produces U6.4's *observable end state* and nothing else. That end state is:
  - slot phase 3, state `0x22`;
  - ~~human `+0x16d4` = 3, plus the vehicle handle `+0x16c0`/`+0x16c4` (the context classifier `0x005ac620` sets "driving" from these, §1.7)~~ **incomplete — corrected by Player U6.4**: `+0x16d4` = 3 and the vehicle handle are set, but what actually hands control to the vehicle handler is the human's **condition row `+0xcc8` = 8 (driver) or 9 (passenger)**, written through the shared locomotion setter `0x00942110`; rows 8/9/17 run the classify `0x005b3750`, which sets context bit 4, which routes the action dispatch to the in-vehicle handler `0x005b5c50`. A stub that skips `+0xcc8` leaves the player on foot controls (`spec-player-runtime.md` §8.0 item 2);
  - the camera submode from U4.

  That keeps the first slice near U6.0's ~5k, but it leaves the vehicle-side effects of the commit undone. Those effects are not yet listed, and listing them is U6.4's first job.

### 5a.6 Other findings recorded for whoever picks up the first sub-unit (not chased)

- **The token vocabulary at `0x0130e638` is fully readable: 83 entries.**
  - Entries 0–53 are actions and variants (`none`, `any`, `activate`, …, `exit fast convertible`).
  - Entries 54–57 are surfaces (`water`, `ground`, `air`, `vehicle`).
  - Entries 58–67 are sides and seats (`left`…`back`, `passenger`, `hostage a`/`b`/`c`).
  - Entries 68–76 are motion (`rocker`, `reverse`, `stopped`, `forward`, `forward fast`, `wheelie`, `stoppie`, `turn left`, `turn right`).
  - Entries 77–82 are combat (`combat ready`, `aim forward`, `aim back`, `aim left`, `aim right`, `alternate`).

  This answers the OPEN in `spec-tables-vehicle-world.md` §2.1 (entries 2–82 "pending a data dump"). Raise it for review there; that file is not edited here. The dump script is `scratchpad/u6plan/tabs.py` (session scratchpad).
- **Two enter states and three exit states have no on-entry handler** (enter `0x10` and `0x12`, which jump to the shared return; exit `0x1a`, `0x1e` and `0x20`). Six enter states and seven exit states have no per-frame handler. Whether `0x10` and `0x12` are ever set is not known: no immediate `0x10` or `0x12` appears in the push survey, but several transitions pass the state in a register.
- **The state-9 path also sends a network packet** when the caller's flag is set and the slot's phase is 3 after the commit (`0x00afe7ba`…`0x00afe80d`). The network receiver `0x00af90e0` (a table slot with no callers) and the tick's queue drain `0x00af9210` both lead to `0x00af8810` and then to the applier `0x00af2950`.
- **`0x00af3cd0` (the exit request) has 70 callers** across missions, AI, the human and the HUD band. It is the most widely used entry into U6 from outside, so U6.6's contract will be cited widely.
- ~~**`0x00b10900` and `0x00b108c0` (21 and 23 outside callers) are not reached from any U6 part.** If they turn out to be "in/entering vehicle" queries, they belong in U6.0's public surface. HYPOTHESIS.~~ **Resolved by Player U6.0**: neither is a vehicle query. `0x00b108c0` initialises a `0x1c`-byte options block; `0x00b10900` just calls the submitter `0x00af22a0`. The real public query surface is the 15-function family `0x00af1770`…`0x00af1e50`, `spec-player-runtime.md` §7.9.
- **The note's §1.3 list of "seven vehicle-interaction functions" that read the walk axes** includes at least `0x00afeb30` (state 0's stick abort), `0x00aff1a0` (state 5's per-frame handler) and `0x00b063c0` (exit `0x1f`'s per-frame handler). The other four were not re-attributed.
- Helper scripts for this pass are in the session scratchpad: `u6plan/sizes6.py`, `units.py`, `member.py`, `cases.py`, `pushes.py`, `keyb.py`, `fan1.py`. Census tables are as for the rest of this note.

---

## 6. Notes for whoever takes it up

- **Tooling.** Everything here came from the session's census tables plus capstone listings of single functions. No Ghidra run was needed for a scoping pass.
  - Several key dispatchers are not functions in the shared project: the profile pre-hook `0x005b3d30`, the AI `Player` mode's enter `0x004f1350`, the control-state rows' enter/per-frame slots (`0x005aa5f0`, `0x005b2170`, `0x005ac4f0`, `0x005b69c0`), and most condition-table `+0x00` slots (`0x005acc80`, `0x005ab190`, …). They do not appear in caller lists; find them by a raw dword scan for their addresses (they sit in `.rdata`/`.data` tables).
  - The input queries take the action index as the first stack argument, so a push survey of each query's call sites names every consumer's action (for example, the survey in §1.3 for the walk axes).
- **Identical-code folding.** `0x00754410` (single `RET`) fills empty slots in the control-state and camera-mode tables, the same stub `spec-ai-runtime.md` §2.2 notes for the AI mode table.
- **Frame order is load-bearing.** The camera runs before the controls in the world update, so the controls read last frame's camera vectors. Animation parameters are written in jobs before physics; character movement runs after physics. A port that reorders these will see one-frame lags in camera-relative movement.
- **Side findings to raise for review** (no spec was edited):
  - `spec-lua-api-behaviour.md` §7.22: the input-lock "object" is the control-state stack (push `0x005aa780(1)`, pop `0x005aa740(handle)`, handle stored at `0x0263ae3c`).
  - `spec-ai-runtime.md` §8 item 7: the `+0x1f0` list walked by `0x009ea630` is the player list (HIGH, via `0x009df3d0`).
  - `spec-world-streaming.md` §11.3: the focus is set every frame from `0x009e7cb0` (§1.6).
  - `spec-tables-ui-controls.md` §2.2: `0x005c1f70` is the binding applier that fills the runtime tables `0x0140ff98`/`0x01411418` (HIGH; whether it also builds the save-format table was not checked).
  - `spec-lua-api-behaviour.md` §2.6: the "active input is a gamepad" byte `0x0141250d` has a writer after all — `0x005bc9d0`, through the context pointer `0x01411b18` + `0x9f5`, which is why no direct reference to the absolute address exists.
- **Out of bounds.** Nothing here opened `.czn_pc` zone data or named-object placement. The player start objects (§1.6) and any zone-authored vehicle or seat overrides are flagged, not examined.
- Every label above is from this scoping pass only. A spec pass must re-derive each claim from the executable before recording it.

## 7. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names and no pasted pseudocode; only listings (capstone and the census) were read, never decompiler output.
- Names quoted as data: RTTI class names (`player`, `human`, `npc`, `rl_camera`, `character_controller`, `hk_custom_character_proxy`, …), CBA/CAA action names read from the engine's own action tables, HUD keys and literal strings (`HUD_BORDER_TURNAROUND`, `Local_Player_Start`, the animation control-parameter names), network field tags (`sync_flags`, `script_mode_params`, …), and Win32/DirectX import names from the PE import table.
- Self-check, run on the finished file as the last step: the project's standard auto-name regex (decompiler variable, stack, parameter, register-input, label, function and data auto-names), plus the plain-substring backstop list from the brief, run with Python `re`. **Regex: 0 hits. Backstop: 0 hits.** A known-positive control string matched 5/5 on the regex and 5 on the backstop in the same run, so the zeros are real. The check was re-run after this line was written, with the same 0/0 result.
- No spec file was edited. No Ghidra project copy was made for this pass, so none needed deleting; the census tables and helper scripts used live in the session scratchpad, not in the repository.
- **U6 sub-unit breakdown (§5a, 2026-10-03).**
  - Same conventions as the rest of the note: plain hex addresses only; no auto-generated names, decompiler variable names or pseudocode. Only capstone listings, census queries and static table reads were used.
  - Quoted as data: the engine's own strings (`Set Enter`, `Set Exit`, `Set Ride`, `Send Packet`, `Received VI Packet`, `Collision Bool`, the `Couldn't find animation …` format string, the `Rifle-…` weapon names), the 83 interaction tokens read from `0x0130e638` (`prepare`, `extract punch`, `hostage a`, …), `vehicle_interaction_info.xtbl` flag names, and the CAA action names.
  - `spec-tables-vehicle-world.md` §2 uses auto-generated names for some of the same loader addresses; this section restates them as plain hex.
  - Self-check on the finished file, run as the last step: the project's standard auto-name regex plus the plain-substring backstop list, with Python `re`. **Regex: 0 hits. Backstop: 0 hits.** In the same run a known-positive control string matched 5/5 on the regex and 5 on the backstop, so the zeros are real. The check was re-run after this line was written.
  - No spec file was edited. No `.czn_pc` data was opened. No Ghidra project was opened or copied (no `tools/gp_playerU6plan` was created), so there was nothing to delete. The helper scripts live in the session scratchpad (`u6plan/`), not in the repository.
